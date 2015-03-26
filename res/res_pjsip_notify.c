/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/manager.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/sorcery.h"

/*** DOCUMENTATION
	<manager name="PJSIPNotify" language="en_US">
		<synopsis>
			Send a NOTIFY to either an endpoint or an arbitrary URI.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Endpoint" required="false">
				<para>The endpoint to which to send the NOTIFY.</para>
			</parameter>
			<parameter name="URI" required="false">
				<para>Abritrary URI to which to send the NOTIFY.</para>
			</parameter>
			<parameter name="Variable" required="true">
				<para>Appends variables as headers/content to the NOTIFY. If the variable is
				named <literal>Content</literal>, then the value will compose the body
				of the message if another variable sets <literal>Content-Type</literal>.
				<replaceable>name</replaceable>=<replaceable>value</replaceable></para>
			</parameter>
		</syntax>
		<description>
			<para>Sends a NOTIFY to an endpoint or an arbitrary URI.</para>
			<para>All parameters for this event must be specified in the body of this
			request	via multiple <literal>Variable: name=value</literal> sequences.</para>
			<note><para>One (and only one) of <literal>Endpoint</literal> or
			<literal>URI</literal> must be specified. If <literal>URI</literal> is used,
			the	default outbound endpoint will be used to send the message. If the default
			outbound endpoint isn't configured, this command can not send to an arbitrary
			URI.</para></note>
		</description>
	</manager>
	<configInfo name="res_pjsip_notify" language="en_US">
		<synopsis>Module that supports sending NOTIFY requests to endpoints from external sources</synopsis>
		<configFile name="pjsip_notify.conf">
			<configObject name="general">
				<synopsis>Unused, but reserved.</synopsis>
			</configObject>
			<configObject name="notify">
				<synopsis>Configuration of a NOTIFY request.</synopsis>
				<description>
					<para>Each key-value pair in a <literal>notify</literal>
					configuration section defines either a SIP header to send
					in the request or a line of content in the request message
					body. A key of <literal>Content</literal> is treated
					as part of the message body and is appended in sequential
					order; any other header is treated as part of the SIP
					request.</para>
				</description>
				<configOption name="^.*$">
					<synopsis>A key/value pair to add to a NOTIFY request.</synopsis>
					<description>
						<para>If the key is <literal>Content</literal>,
						it will be treated as part of the message body. Otherwise,
						it will be added as a header in the NOTIFY request.</para>
						<para>The following headers are reserved and cannot be
						specified:</para>
						<enumlist>
							<enum name="Call-ID" />
							<enum name="Contact" />
							<enum name="CSeq" />
							<enum name="To" />
							<enum name="From" />
							<enum name="Record-Route" />
							<enum name="Route" />
							<enum name="Via" />
						</enumlist>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

#define CONTENT_TYPE_SIZE 64
#define CONTENT_SIZE 512

/*!
 * \internal
 * \brief The configuration file containing NOTIFY payload types to send.
 */
static const char notify_config[] = "pjsip_notify.conf";

struct notify_option_item {
	const char *name;
	const char *value;
	char buf[0];
};

struct notify_option {
	/*! Contains header and/or content information */
	struct ao2_container *items;
	/*! The name of the notify option */
	char name[0];
};

static int notify_option_hash(const void *obj, int flags)
{
	const struct notify_option *option = obj;
	return ast_str_case_hash(flags & OBJ_KEY ? obj : option->name);
}

static int notify_option_cmp(void *obj, void *arg, int flags)
{
	struct notify_option *option1 = obj;
	struct notify_option *option2 = arg;
	const char *key = flags & OBJ_KEY ? arg : option2->name;

	return strcasecmp(option1->name, key) ? 0 : CMP_MATCH;
}

static void notify_option_destroy(void *obj)
{
	struct notify_option *option = obj;
	ao2_cleanup(option->items);
}

static void *notify_option_alloc(const char *category)
{
	int category_size = strlen(category) + 1;

	struct notify_option *option = ao2_alloc(
		sizeof(*option) + category_size, notify_option_destroy);

	if (!option) {
		return NULL;
	}

	ast_copy_string(option->name, category, category_size);

	if (!(option->items = ao2_container_alloc_list(
		      AO2_ALLOC_OPT_LOCK_NOLOCK,
		      AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW, NULL, NULL))) {
		ao2_cleanup(option);
		return NULL;
	}

	return option;
}

static void *notify_option_find(struct ao2_container *container, const char *category)
{
	return ao2_find(container, category, OBJ_KEY);
}

static int notify_option_handler(const struct aco_option *opt,
				 struct ast_variable *var, void *obj)
{
	struct notify_option *option = obj;

	int name_size = strlen(var->name) + 1;
	int value_size = strlen(var->value) + 1;

	RAII_VAR(struct notify_option_item *, item,
		 ao2_alloc(sizeof(*item) + name_size + value_size,
			   NULL), ao2_cleanup);

	item->name = item->buf;
	item->value = item->buf + name_size;

	ast_copy_string(item->buf, var->name, name_size);
	ast_copy_string(item->buf + name_size, var->value, value_size);

	if (!ao2_link(option->items, item)) {
		return -1;
	}

	return 0;
}

struct notify_cfg {
	struct ao2_container *notify_options;
};

static void notify_cfg_destroy(void *obj)
{
	struct notify_cfg *cfg = obj;
	ao2_cleanup(cfg->notify_options);
}

static void *notify_cfg_alloc(void)
{
	struct notify_cfg *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), notify_cfg_destroy))) {
		return NULL;
	}

	if (!(cfg->notify_options = ao2_container_alloc_options(
		      AO2_ALLOC_OPT_LOCK_NOLOCK, 20, notify_option_hash,
		      notify_option_cmp))) {
		ao2_cleanup(cfg);
		return NULL;
	}

	return cfg;
}

static struct aco_type notify_option = {
	.type = ACO_ITEM,
	.name = "notify",
	.category_match = ACO_BLACKLIST,
	.category = "^general$",
	.item_offset = offsetof(struct notify_cfg, notify_options),
	.item_alloc = notify_option_alloc,
	.item_find = notify_option_find
};

static struct aco_type *notify_options[] = ACO_TYPES(&notify_option);

static struct aco_file module_conf = {
	.filename = notify_config,
	.types = ACO_TYPES(&notify_option),
};

AO2_GLOBAL_OBJ_STATIC(globals);

CONFIG_INFO_STANDARD(notify_cfg, globals, notify_cfg_alloc,
	.files = ACO_FILES(&module_conf)
);

/*!
 * \internal
 * \brief Structure to hold task data for notifications.
 */
struct notify_data {
	/*! The endpoint being notified */
	struct ast_sip_endpoint *endpoint;
	/*! The info of headers, types and content */
	void *info;
	/*! Function to help build notify request */
	void (*build_notify)(pjsip_tx_data *, void *);
};

/*!
 * \internal
 * \brief Destroy the notify CLI data releasing any resources.
 */
static void notify_cli_data_destroy(void *obj)
{
	struct notify_data *data = obj;

	ao2_cleanup(data->endpoint);
	ao2_cleanup(data->info);
}

/*!
 * \internal
 * \brief Structure to hold task data for notifications (URI variant)
 */
struct notify_uri_data {
	char *uri;
	void *info;
	void (*build_notify)(pjsip_tx_data *, void *);
};

static void notify_cli_uri_data_destroy(void *obj)
{
	struct notify_uri_data *data = obj;

	ast_free(data->uri);
	ao2_cleanup(data->info);
}

/*!
 * \internal
 * \brief Destroy the notify CLI data releasing any resources (URI variant)
 */
static void build_cli_notify(pjsip_tx_data *tdata, void *info);

/*!
 * \internal
 * \brief Construct a notify data object for CLI.
 */
static struct notify_data* notify_cli_data_create(
	struct ast_sip_endpoint *endpoint, void *info)
{
	struct notify_data *data = ao2_alloc(sizeof(*data),
					     notify_cli_data_destroy);
	if (!data) {
		return NULL;
	}

	data->endpoint = endpoint;
	ao2_ref(data->endpoint, +1);

	data->info = info;
	ao2_ref(data->info, +1);

	data->build_notify = build_cli_notify;

	return data;
}

/*!
 * \internal
 * \brief Construct a notify URI data object for CLI.
 */
static struct notify_uri_data* notify_cli_uri_data_create(
	const char *uri, void *info)
{
	struct notify_uri_data *data = ao2_alloc(sizeof(*data),
		notify_cli_uri_data_destroy);

	if (!data) {
		return NULL;
	}

	data->uri = ast_strdup(uri);
	if (!data->uri) {
		ao2_ref(data, -1);
		return NULL;
	}

	data->info = info;
	ao2_ref(data->info, +1);

	data->build_notify = build_cli_notify;

	return data;
}

/*!
 * \internal
 * \brief Destroy the notify AMI data releasing any resources.
 */
static void notify_ami_data_destroy(void *obj)
{
	struct notify_data *data = obj;
	struct ast_variable *info = data->info;

	ao2_cleanup(data->endpoint);
	ast_variables_destroy(info);
}

/*!
 * \internal
 * \brief Destroy the notify AMI URI data releasing any resources.
 */
static void notify_ami_uri_data_destroy(void *obj)
{
	struct notify_uri_data *data = obj;
	struct ast_variable *info = data->info;

	ast_free(data->uri);
	ast_variables_destroy(info);
}

static void build_ami_notify(pjsip_tx_data *tdata, void *info);

/*!
 * \internal
 * \brief Construct a notify data object for AMI.
 */
static struct notify_data* notify_ami_data_create(
	struct ast_sip_endpoint *endpoint, void *info)
{
	struct notify_data *data = ao2_alloc(sizeof(*data),
					     notify_ami_data_destroy);
	if (!data) {
		return NULL;
	}

	data->endpoint = endpoint;
	ao2_ref(data->endpoint, +1);

	data->info = info;
	data->build_notify = build_ami_notify;

	return data;
}

/*!
 * \internal
 * \brief Construct a notify URI data object for AMI.
 */
static struct notify_uri_data* notify_ami_uri_data_create(
	const char *uri, void *info)
{
	struct notify_uri_data *data = ao2_alloc(sizeof(*data),
							notify_ami_uri_data_destroy);
	if (!data) {
		return NULL;
	}

	data->uri = ast_strdup(uri);
	if (!data->uri) {
		ao2_ref(data, -1);
		return NULL;
	}

	data->info = info;
	data->build_notify = build_ami_notify;

	return data;
}

/*!
 * \internal
 * \brief Checks if the given header name is not allowed.
 *
 * \details Some headers are not allowed to be set by the user within the
 *          scope of a NOTIFY request.  If the given var header name is
 *          found in the "not allowed" list then return true.
 */
static int not_allowed(const char *name)
{
	int i;
	static const char *names[] = {
		"Call-ID",
		"Contact",
		"CSeq",
		"To",
		"From",
		"Record-Route",
		"Route",
		"Request-URI",
		"Via",
	};

	for (i = 0; i < ARRAY_LEN(names); ++i) {
		if (!strcasecmp(name, names[i])) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief If a content type was specified add it and the content body to the
 *        NOTIFY request.
 */
static void build_notify_body(pjsip_tx_data *tdata, struct ast_str *content_type,
			      struct ast_str *content)
{
	if (content_type) {
		char *p;
		struct ast_sip_body body;

		if (content) {
			body.body_text = ast_str_buffer(content);
		}

		body.type = ast_str_buffer(content_type);
		if ((p = strchr(body.type, '/'))) {
			*p++ = '\0';
			body.subtype = p;
		}
		ast_sip_add_body(tdata, &body);
	}
}

/*!
 * \internal
 * \brief Build the NOTIFY request adding content or header info.
 */
static void build_notify(pjsip_tx_data *tdata, const char *name, const char *value,
			 struct ast_str **content_type, struct ast_str **content)
{
	if (not_allowed(name)) {
		ast_log(LOG_WARNING, "Cannot specify %s header, "
			"ignoring\n", name);
		return;
	}

	if (!strcasecmp(name, "Content-type")) {
		if (!(*content_type)) {
			*content_type = ast_str_create(CONTENT_TYPE_SIZE);
		}
		ast_str_set(content_type, 0,"%s", value);
	} else if (!strcasecmp(name, "Content")) {
		if (!(*content)) {
			*content = ast_str_create(CONTENT_SIZE);
		}

		if (ast_str_strlen(*content)) {
			ast_str_append(content, 0, "\r\n");
		}
		ast_str_append(content, 0, "%s", value);
	} else {
		ast_sip_add_header(tdata, name, value);
	}
}

/*!
 * \internal
 * \brief Build the NOTIFY request from CLI info adding header and content
 *        when specified.
 */
static void build_cli_notify(pjsip_tx_data *tdata, void *info)
{
	struct notify_option *option = info;
	RAII_VAR(struct ast_str *, content_type, NULL, ast_free);
	RAII_VAR(struct ast_str *, content, NULL, ast_free);

	struct notify_option_item *item;
	struct ao2_iterator i = ao2_iterator_init(option->items, 0);

	while ((item = ao2_iterator_next(&i))) {
		build_notify(tdata, item->name, item->value,
			     &content_type, &content);
		ao2_cleanup(item);
	}
	ao2_iterator_destroy(&i);

	build_notify_body(tdata, content_type, content);
}

/*!
 * \internal
 * \brief Build the NOTIFY request from AMI info adding header and content
 *        when specified.
 */
static void build_ami_notify(pjsip_tx_data *tdata, void *info)
{
	struct ast_variable *vars = info;
	RAII_VAR(struct ast_str *, content_type, NULL, ast_free);
	RAII_VAR(struct ast_str *, content, NULL, ast_free);
	struct ast_variable *i;

	for (i = vars; i; i = i->next) {
		if (!strcasecmp(i->name, "Content-Length")) {
			ast_log(LOG_NOTICE, "It is not necessary to specify Content-Length, ignoring.\n");
			continue;
		}
		build_notify(tdata, i->name, i->value,
			     &content_type, &content);
	}

	build_notify_body(tdata, content_type, content);
}

/*!
 * \internal
 * \brief Build and send a NOTIFY request to a contact.
 */
static int notify_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct notify_data *data = arg;
	pjsip_tx_data *tdata;

	if (ast_sip_create_request("NOTIFY", NULL, data->endpoint,
				   NULL, contact, &tdata)) {
		ast_log(LOG_WARNING, "SIP NOTIFY - Unable to create request for "
			"contact %s\n",	contact->uri);
		return -1;
	}

	ast_sip_add_header(tdata, "Subscription-State", "terminated");
	data->build_notify(tdata, data->info);

	if (ast_sip_send_request(tdata, NULL, data->endpoint, NULL, NULL)) {
		ast_log(LOG_ERROR, "SIP NOTIFY - Unable to send request for "
			"contact %s\n",	contact->uri);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Send a NOTIFY request to the endpoint.
 *
 * \detail Iterates over an endpoint's AORs sending a NOTIFY request
 *         with the appropriate payload information to each contact.
 */
static int notify_endpoint(void *obj)
{
	RAII_VAR(struct notify_data *, data, obj, ao2_cleanup);
	char *aor_name, *aors;

	if (ast_strlen_zero(data->endpoint->aors)) {
		ast_log(LOG_WARNING, "Unable to NOTIFY - "
			"endpoint has no configured AORs\n");
		return -1;
	}

	aors = ast_strdupa(data->endpoint->aors);

	while ((aor_name = strsep(&aors, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

		if (!aor || !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			continue;
		}

		ao2_callback(contacts, OBJ_NODATA, notify_contact, data);
	}

	return 0;
}

/*!
 * \internal
 * \brief Send a notify request to the URI.
 */
static int notify_uri(void *obj)
{
	RAII_VAR(struct notify_uri_data *, data, obj, ao2_cleanup);
	RAII_VAR(struct ast_sip_endpoint *, endpoint,
		ast_sip_default_outbound_endpoint(), ao2_cleanup);
	pjsip_tx_data *tdata;

	if (!endpoint) {
		ast_log(LOG_WARNING, "No default outbound endpoint set, can not send "
			"NOTIFY requests to arbitrary URIs.\n");
		return -1;
	}

	if (ast_strlen_zero(data->uri)) {
		ast_log(LOG_WARNING, "Unable to NOTIFY - URI is blank.\n");
		return -1;
	}

	if (ast_sip_create_request("NOTIFY", NULL, endpoint,
				   data->uri, NULL, &tdata)) {
		ast_log(LOG_WARNING, "SIP NOTIFY - Unable to create request for "
			"uri %s\n",	data->uri);
		return -1;
	}

	ast_sip_add_header(tdata, "Subscription-State", "terminated");

	data->build_notify(tdata, data->info);

	if (ast_sip_send_request(tdata, NULL, endpoint, NULL, NULL)) {
		ast_log(LOG_ERROR, "SIP NOTIFY - Unable to send request for "
			"uri %s\n",	data->uri);
		return -1;
	}

	return 0;
}

enum notify_result {
	SUCCESS,
	INVALID_ENDPOINT,
	ALLOC_ERROR,
	TASK_PUSH_ERROR
};

typedef struct notify_data *(*task_data_create)(
	struct ast_sip_endpoint *, void *info);

typedef struct notify_uri_data *(*task_uri_data_create)(
	const char *uri, void *info);
/*!
 * \internal
 * \brief Send a NOTIFY request to the endpoint within a threaded task.
 */
static enum notify_result push_notify(const char *endpoint_name, void *info,
				      task_data_create data_create)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct notify_data *data;

	if (!(endpoint = ast_sorcery_retrieve_by_id(
		      ast_sip_get_sorcery(), "endpoint", endpoint_name))) {
		return INVALID_ENDPOINT;
	}

	if (!(data = data_create(endpoint, info))) {
		return ALLOC_ERROR;
	}

	if (ast_sip_push_task(NULL, notify_endpoint, data)) {
		ao2_cleanup(data);
		return TASK_PUSH_ERROR;
	}

	return SUCCESS;
}

/*!
 * \internal
 * \brief Send a NOTIFY request to the URI within an threaded task.
 */
static enum notify_result push_notify_uri(const char *uri, void *info,
	task_uri_data_create data_create)
{
	struct notify_uri_data *data;

	if (!(data = data_create(uri, info))) {
		return ALLOC_ERROR;
	}

	if (ast_sip_push_task(NULL, notify_uri, data)) {
		ao2_cleanup(data);
		return TASK_PUSH_ERROR;
	}

	return SUCCESS;
}

/*!
 * \internal
 * \brief Do completion on the endpoint.
 */
static char *cli_complete_endpoint(const char *word, int state)
{
	char *result = NULL;
	int wordlen = strlen(word);
	int which = 0;

	struct ast_sip_endpoint *endpoint;
	RAII_VAR(struct ao2_container *, endpoints,
		 ast_sip_get_endpoints(), ao2_cleanup);

	struct ao2_iterator i = ao2_iterator_init(endpoints, 0);
	while ((endpoint = ao2_iterator_next(&i))) {
		const char *name = ast_sorcery_object_get_id(endpoint);
		if (!strncasecmp(word, name, wordlen) && ++which > state) {
			result = ast_strdup(name);
		}

		ao2_cleanup(endpoint);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&i);
	return result;
}

/*!
 * \internal
 * \brief Do completion on the notify CLI command.
 */
static char *cli_complete_notify(const char *line, const char *word,
				 int pos, int state, int using_uri)
{
	char *c = NULL;

	if (pos == 3) {
		int which = 0;
		int wordlen = strlen(word);

		RAII_VAR(struct notify_cfg *, cfg,
			 ao2_global_obj_ref(globals), ao2_cleanup);
		struct notify_option *option;

		/* do completion for notify type */
		struct ao2_iterator i = ao2_iterator_init(cfg->notify_options, 0);
		while ((option = ao2_iterator_next(&i))) {
			if (!strncasecmp(word, option->name, wordlen) && ++which > state) {
				c = ast_strdup(option->name);
			}

			ao2_cleanup(option);
			if (c) {
				break;
			}
		}
		ao2_iterator_destroy(&i);
		return c;
	}

	if (pos == 4) {
		int wordlen = strlen(word);

		if (ast_strlen_zero(word)) {
		    if (state == 0) {
		        c = ast_strdup("endpoint");
		    } else if (state == 1) {
		        c = ast_strdup("uri");
		    }
		} else if (state == 0) {
		    if (!strncasecmp(word, "endpoint", wordlen)) {
		        c = ast_strdup("endpoint");
		    } else if (!strncasecmp(word, "uri", wordlen)) {
		        c = ast_strdup("uri");
		    }
		}

		return c;
	}

	return pos > 4 && !using_uri ? cli_complete_endpoint(word, state) : NULL;
}

/*!
 * \internal
 * \brief CLI command to send a SIP notify to an endpoint.
 *
 * \details Attempts to match the "type" given in the CLI command to a
 *          configured one.  If found, sends a NOTIFY to the endpoint
 *          with the associated payload.
 */
static char *cli_notify(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct notify_cfg *, cfg, NULL, ao2_cleanup);
	RAII_VAR(struct notify_option *, option, NULL, ao2_cleanup);

	int i;
	int using_uri = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip send notify";
		e->usage =
			"Usage: pjsip send notify <type> {endpoint|uri} <peer> [<peer>...]\n"
			"       Send a NOTIFY request to an endpoint\n"
			"       Message types are defined in sip_notify.conf\n";
		return NULL;
	case CLI_GENERATE:
		if (a->argc > 4 && (!strcasecmp(a->argv[4], "uri"))) {
			using_uri = 1;
		}

		return cli_complete_notify(a->line, a->word, a->pos, a->n, using_uri);
	}

	if (a->argc < 6) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[4], "uri")) {
		using_uri = 1;
	} else if (strcasecmp(a->argv[4], "endpoint")) {
		return CLI_SHOWUSAGE;
	}

	cfg = ao2_global_obj_ref(globals);

	if (!(option = notify_option_find(cfg->notify_options, a->argv[3])))
	{
		ast_cli(a->fd, "Unable to find notify type '%s'\n",
			a->argv[3]);
		return CLI_FAILURE;
	}

	for (i = 5; i < a->argc; ++i) {
		ast_cli(a->fd, "Sending NOTIFY of type '%s' to '%s'\n",
			a->argv[3], a->argv[i]);

		switch (using_uri ? push_notify_uri(a->argv[i], option, notify_cli_uri_data_create) :
		                    push_notify(a->argv[i], option, notify_cli_data_create)) {
		case INVALID_ENDPOINT:
			ast_cli(a->fd, "Unable to retrieve endpoint %s\n",
				a->argv[i]);
			break;
		case ALLOC_ERROR:
			ast_cli(a->fd, "Unable to allocate NOTIFY task data\n");
			return CLI_FAILURE;
		case TASK_PUSH_ERROR:
			ast_cli(a->fd, "Unable to push NOTIFY task\n");
			return CLI_FAILURE;
		default:
			break;
		}
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_options[] = {
	AST_CLI_DEFINE(cli_notify, "Send a NOTIFY request to a SIP endpoint")
};

/*!
 * \interanl
 * \brief Completes SIPNotify AMI command in Endpoint mode.
 */
static void manager_notify_endpoint(struct mansession *s,
	const struct message *m, const char *endpoint_name)
{
	struct ast_variable *vars = astman_get_variables_order(m, ORDER_NATURAL);

	if (!strncasecmp(endpoint_name, "sip/", 4)) {
		endpoint_name += 4;
	}

	if (!strncasecmp(endpoint_name, "pjsip/", 6)) {
		endpoint_name += 6;
	}

	switch (push_notify(endpoint_name, vars, notify_ami_data_create)) {
	case INVALID_ENDPOINT:
		ast_variables_destroy(vars);
		astman_send_error_va(s, m, "Unable to retrieve endpoint %s",
			endpoint_name);
		break;
	case ALLOC_ERROR:
		ast_variables_destroy(vars);
		astman_send_error(s, m, "Unable to allocate NOTIFY task data");
		break;
	case TASK_PUSH_ERROR:
		/* Don't need to destroy vars since it is handled by cleanup in push_notify */
		astman_send_error(s, m, "Unable to push NOTIFY task");
		break;
	case SUCCESS:
		astman_send_ack(s, m, "NOTIFY sent");
		break;
	}
}

/*!
 * \internal
 * \brief Completes SIPNotify AMI command in URI mode.
 */
static void manager_notify_uri(struct mansession *s,
	const struct message *m, const char *uri)
{
	struct ast_variable *vars = astman_get_variables_order(m, ORDER_NATURAL);

	switch (push_notify_uri(uri, vars, notify_ami_uri_data_create)) {
	case INVALID_ENDPOINT:
		/* Shouldn't be possible. */
		ast_assert(0);
		break;
	case ALLOC_ERROR:
		ast_variables_destroy(vars);
		astman_send_error(s, m, "Unable to allocate NOTIFY task data");
		break;
	case TASK_PUSH_ERROR:
		/* Don't need to destroy vars since it is handled by cleanup in push_notify_uri */
		astman_send_error(s, m, "Unable to push Notify task");
		break;
	case SUCCESS:
		astman_send_ack(s, m, "NOTIFY sent");
		break;
	}
}

/*!
 * \internal
 * \brief AMI entry point to send a SIP notify to an endpoint.
 */
static int manager_notify(struct mansession *s, const struct message *m)
{
	const char *endpoint_name = astman_get_header(m, "Endpoint");
	const char *uri = astman_get_header(m, "URI");

	if (!ast_strlen_zero(endpoint_name) && !ast_strlen_zero(uri)) {
		astman_send_error(s, m, "PJSIPNotify action can not handle a request specifying "
			"both 'URI' and 'Endpoint'. You must use only one of the two.\n");
	} else if (!ast_strlen_zero(endpoint_name)) {
		manager_notify_endpoint(s, m, endpoint_name);
	} else if (!ast_strlen_zero(uri)) {
		manager_notify_uri(s, m, uri);
	} else {
		astman_send_error(s, m, "PJSIPNotify requires either an endpoint name or a SIP URI.");
	}

	return 0;
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	if (aco_info_init(&notify_cfg)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	aco_option_register_custom(&notify_cfg, "^.*$", ACO_REGEX, notify_options,
				   "", notify_option_handler, 0);

	if (aco_process_config(&notify_cfg, 0)) {
		aco_info_destroy(&notify_cfg);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cli_options, ARRAY_LEN(cli_options));
	ast_manager_register_xml("PJSIPNotify", EVENT_FLAG_SYSTEM, manager_notify);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	if (aco_process_config(&notify_cfg, 1) == ACO_PROCESS_ERROR) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

static int unload_module(void)
{
	ast_manager_unregister("PJSIPNotify");
	ast_cli_unregister_multiple(cli_options, ARRAY_LEN(cli_options));
	aco_info_destroy(&notify_cfg);
	ao2_global_obj_release(globals);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "CLI/AMI PJSIP NOTIFY Support",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
