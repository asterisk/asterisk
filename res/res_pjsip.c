/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#include "asterisk.h"

#include <pjsip.h>
/* Needed for SUBSCRIBE, NOTIFY, and PUBLISH method definitions */
#include <pjsip_simple.h>
#include <pjsip/sip_transaction.h>
#include <pj/timer.h>
#include <pjlib.h>
#include <pjmedia/errno.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/strings.h"
#include "pjsip/sip_parser.h"
#include "res_pjsip/include/res_pjsip_private.h"
#include "asterisk/linkedlists.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/serializer.h"
#include "asterisk/threadpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/uuid.h"
#include "asterisk/sorcery.h"
#include "asterisk/file.h"
#include "asterisk/causes.h"
#include "asterisk/cli.h"
#include "asterisk/callerid.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/test.h"
#include "asterisk/res_pjsip_presence_xml.h"
#include "asterisk/res_pjproject.h"
#include "asterisk/utf8.h"
#include "asterisk/acl.h"

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjproject</depend>
	<depend>res_sorcery_config</depend>
	<depend>res_sorcery_memory</depend>
	<depend>res_sorcery_astdb</depend>
	<use type="module">res_statsd</use>
	<use type="module">res_geolocation</use>
	<support_level>core</support_level>
 ***/

#define MOD_DATA_CONTACT "contact"

/*! Number of serializers in pool if one not supplied. */
#define SERIALIZER_POOL_SIZE		8

/*! Pool of serializers to use if not supplied. */
static struct ast_serializer_pool *sip_serializer_pool;

static pjsip_endpoint *ast_pjsip_endpoint;

static struct ast_threadpool *sip_threadpool;

/*! Local host address for IPv4 */
static pj_sockaddr host_ip_ipv4;

/*! Local host address for IPv4 (string form) */
static char host_ip_ipv4_string[PJ_INET6_ADDRSTRLEN];

/*! Local host address for IPv6 */
static pj_sockaddr host_ip_ipv6;

/*! Local host address for IPv6 (string form) */
static char host_ip_ipv6_string[PJ_INET6_ADDRSTRLEN];

void ast_sip_add_date_header(pjsip_tx_data *tdata)
{
	char date[256];
	struct tm tm;
	time_t t = time(NULL);

	gmtime_r(&t, &tm);
	strftime(date, sizeof(date), "%a, %d %b %Y %T GMT", &tm);

	ast_sip_add_header(tdata, "Date", date);
}

static int register_service(void *data)
{
	pjsip_module **module = data;
	if (!ast_pjsip_endpoint) {
		ast_log(LOG_ERROR, "There is no PJSIP endpoint. Unable to register services\n");
		return -1;
	}
	if (pjsip_endpt_register_module(ast_pjsip_endpoint, *module) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to register module %.*s\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name));
		return -1;
	}
	ast_debug(1, "Registered SIP service %.*s (%p)\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name), *module);
	return 0;
}

int ast_sip_register_service(pjsip_module *module)
{
	return ast_sip_push_task_wait_servant(NULL, register_service, &module);
}

static int unregister_service(void *data)
{
	pjsip_module **module = data;
	if (!ast_pjsip_endpoint) {
		return -1;
	}
	pjsip_endpt_unregister_module(ast_pjsip_endpoint, *module);
	ast_debug(1, "Unregistered SIP service %.*s\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name));
	return 0;
}

void ast_sip_unregister_service(pjsip_module *module)
{
	ast_sip_push_task_wait_servant(NULL, unregister_service, &module);
}

static struct ast_sip_authenticator *registered_authenticator;

int ast_sip_register_authenticator(struct ast_sip_authenticator *auth)
{
	if (registered_authenticator) {
		ast_log(LOG_WARNING, "Authenticator %p is already registered. Cannot register a new one\n", registered_authenticator);
		return -1;
	}
	registered_authenticator = auth;
	ast_debug(1, "Registered SIP authenticator module %p\n", auth);

	return 0;
}

void ast_sip_unregister_authenticator(struct ast_sip_authenticator *auth)
{
	if (registered_authenticator != auth) {
		ast_log(LOG_WARNING, "Trying to unregister authenticator %p but authenticator %p registered\n",
				auth, registered_authenticator);
		return;
	}
	registered_authenticator = NULL;
	ast_debug(1, "Unregistered SIP authenticator %p\n", auth);
}

int ast_sip_requires_authentication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	if (endpoint->allow_unauthenticated_options
		&& !pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_options_method)) {
		ast_debug(3, "Skipping OPTIONS authentication due to endpoint configuration\n");
		return 0;
	}

	if (!registered_authenticator) {
		ast_log(LOG_WARNING, "No SIP authenticator registered. Assuming authentication is not required\n");
		return 0;
	}

	return registered_authenticator->requires_authentication(endpoint, rdata);
}

enum ast_sip_check_auth_result ast_sip_check_authentication(struct ast_sip_endpoint *endpoint,
		pjsip_rx_data *rdata, pjsip_tx_data *tdata)
{
	if (!registered_authenticator) {
		ast_log(LOG_WARNING, "No SIP authenticator registered. Assuming authentication is successful\n");
		return AST_SIP_AUTHENTICATION_SUCCESS;
	}
	return registered_authenticator->check_authentication(endpoint, rdata, tdata);
}

static struct ast_sip_outbound_authenticator *registered_outbound_authenticator;

int ast_sip_register_outbound_authenticator(struct ast_sip_outbound_authenticator *auth)
{
	if (registered_outbound_authenticator) {
		ast_log(LOG_WARNING, "Outbound authenticator %p is already registered. Cannot register a new one\n", registered_outbound_authenticator);
		return -1;
	}
	registered_outbound_authenticator = auth;
	ast_debug(1, "Registered SIP outbound authenticator module %p\n", auth);

	return 0;
}

void ast_sip_unregister_outbound_authenticator(struct ast_sip_outbound_authenticator *auth)
{
	if (registered_outbound_authenticator != auth) {
		ast_log(LOG_WARNING, "Trying to unregister outbound authenticator %p but outbound authenticator %p registered\n",
				auth, registered_outbound_authenticator);
		return;
	}
	registered_outbound_authenticator = NULL;
	ast_debug(1, "Unregistered SIP outbound authenticator %p\n", auth);
}

int ast_sip_create_request_with_auth(const struct ast_sip_auth_vector *auths, pjsip_rx_data *challenge,
		pjsip_tx_data *old_request, pjsip_tx_data **new_request)
{
	if (!registered_outbound_authenticator) {
		ast_log(LOG_WARNING, "No SIP outbound authenticator registered. Cannot respond to authentication challenge\n");
		return -1;
	}
	return registered_outbound_authenticator->create_request_with_auth(auths, challenge, old_request, new_request);
}

struct endpoint_identifier_list {
	const char *name;
	unsigned int priority;
	struct ast_sip_endpoint_identifier *identifier;
	AST_RWLIST_ENTRY(endpoint_identifier_list) list;
};

static AST_RWLIST_HEAD_STATIC(endpoint_identifiers, endpoint_identifier_list);

int ast_sip_register_endpoint_identifier_with_name(struct ast_sip_endpoint_identifier *identifier,
						 const char *name)
{
	char *prev, *current, *identifier_order;
	struct endpoint_identifier_list *iter, *id_list_item;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	id_list_item = ast_calloc(1, sizeof(*id_list_item));
	if (!id_list_item) {
		ast_log(LOG_ERROR, "Unable to add endpoint identifier. Out of memory.\n");
		return -1;
	}
	id_list_item->identifier = identifier;
	id_list_item->name = name;

	ast_debug(1, "Register endpoint identifier %s(%p)\n", name ?: "", identifier);

	if (ast_strlen_zero(name)) {
		/* if an identifier has no name then place in front */
		AST_RWLIST_INSERT_HEAD(&endpoint_identifiers, id_list_item, list);
		return 0;
	}

	/* see if the name of the identifier is in the global endpoint_identifier_order list */
	identifier_order = prev = current = ast_sip_get_endpoint_identifier_order();

	if (ast_strlen_zero(identifier_order)) {
		id_list_item->priority = UINT_MAX;
		AST_RWLIST_INSERT_TAIL(&endpoint_identifiers, id_list_item, list);
		ast_free(identifier_order);
		return 0;
	}

	id_list_item->priority = 0;
	while ((current = strchr(current, ','))) {
		++id_list_item->priority;
		if (!strncmp(prev, name, current - prev)
			&& strlen(name) == current - prev) {
			break;
		}
		prev = ++current;
	}

	if (!current) {
		/* check to see if it is the only or last item */
		if (!strcmp(prev, name)) {
			++id_list_item->priority;
		} else {
			id_list_item->priority = UINT_MAX;
		}
	}

	if (id_list_item->priority == UINT_MAX || AST_RWLIST_EMPTY(&endpoint_identifiers)) {
		/* if not in the endpoint_identifier_order list then consider it less in
		   priority and add it to the end */
		AST_RWLIST_INSERT_TAIL(&endpoint_identifiers, id_list_item, list);
		ast_free(identifier_order);
		return 0;
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_identifiers, iter, list) {
		if (id_list_item->priority < iter->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(id_list_item, list);
			break;
		}

		if (!AST_RWLIST_NEXT(iter, list)) {
			AST_RWLIST_INSERT_AFTER(&endpoint_identifiers, iter, id_list_item, list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	ast_free(identifier_order);
	return 0;
}

int ast_sip_register_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier)
{
	return ast_sip_register_endpoint_identifier_with_name(identifier, NULL);
}

void ast_sip_unregister_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier)
{
	struct endpoint_identifier_list *iter;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_identifiers, iter, list) {
		if (iter->identifier == identifier) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(iter);
			ast_debug(1, "Unregistered endpoint identifier %p\n", identifier);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

struct ast_sip_endpoint *ast_sip_identify_endpoint(pjsip_rx_data *rdata)
{
	struct endpoint_identifier_list *iter;
	struct ast_sip_endpoint *endpoint = NULL;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE(&endpoint_identifiers, iter, list) {
		ast_assert(iter->identifier->identify_endpoint != NULL);
		endpoint = iter->identifier->identify_endpoint(rdata);
		if (endpoint) {
			break;
		}
	}
	return endpoint;
}

char *ast_sip_rdata_get_header_value(pjsip_rx_data *rdata, const pj_str_t str)
{
	pjsip_generic_string_hdr *hdr;
	pj_str_t hdr_val;

	hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str, NULL);
	if (!hdr) {
		return NULL;
	}

	pj_strdup_with_null(rdata->tp_info.pool, &hdr_val, &hdr->hvalue);

	return hdr_val.ptr;
}

static int do_cli_dump_endpt(void *v_a)
{
	struct ast_cli_args *a = v_a;

	ast_pjproject_log_intercept_begin(a->fd);
	pjsip_endpt_dump(ast_sip_get_pjsip_endpoint(), a->argc == 4 ? PJ_TRUE : PJ_FALSE);
	ast_pjproject_log_intercept_end();

	return 0;
}

static char *cli_dump_endpt(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
#ifdef AST_DEVMODE
		e->command = "pjsip dump endpt [details]";
		e->usage =
			"Usage: pjsip dump endpt [details]\n"
			"       Dump the res_pjsip endpt internals.\n"
			"\n"
			"Warning: PJPROJECT documents that the function used by this\n"
			"CLI command may cause a crash when asking for details because\n"
			"it tries to access all active memory pools.\n";
#else
		/*
		 * In non-developer mode we will not document or make easily accessible
		 * the details option even though it is still available.  The user has
		 * to know it exists to use it.  Presumably they would also be aware of
		 * the potential crash warning.
		 */
		e->command = "pjsip dump endpt";
		e->usage =
			"Usage: pjsip dump endpt\n"
			"       Dump the res_pjsip endpt internals.\n";
#endif /* AST_DEVMODE */
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (4 < a->argc
		|| (a->argc == 4 && strcasecmp(a->argv[3], "details"))) {
		return CLI_SHOWUSAGE;
	}

	ast_sip_push_task_wait_servant(NULL, do_cli_dump_endpt, a);

	return CLI_SUCCESS;
}

static char *cli_show_endpoint_identifiers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define ENDPOINT_IDENTIFIER_FORMAT "%-20.20s\n"
	struct endpoint_identifier_list *iter;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show identifiers";
		e->usage = "Usage: pjsip show identifiers\n"
		            "      List all registered endpoint identifiers\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
                return CLI_SHOWUSAGE;
        }

	ast_cli(a->fd, ENDPOINT_IDENTIFIER_FORMAT, "Identifier Names:");
	{
		SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
		AST_RWLIST_TRAVERSE(&endpoint_identifiers, iter, list) {
			ast_cli(a->fd, ENDPOINT_IDENTIFIER_FORMAT,
				iter->name ? iter->name : "name not specified");
		}
	}
	return CLI_SUCCESS;
#undef ENDPOINT_IDENTIFIER_FORMAT
}

static char *cli_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_sip_cli_context context;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show settings";
		e->usage = "Usage: pjsip show settings\n"
		            "      Show global and system configuration options\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	context.output_buffer = ast_str_create(256);
	if (!context.output_buffer) {
		ast_cli(a->fd, "Could not allocate output buffer.\n");
		return CLI_FAILURE;
	}

	if (sip_cli_print_global(&context) || sip_cli_print_system(&context)) {
		ast_free(context.output_buffer);
		ast_cli(a->fd, "Error retrieving settings.\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "%s", ast_str_buffer(context.output_buffer));
	ast_free(context.output_buffer);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_dump_endpt, "Dump the res_pjsip endpt internals"),
	AST_CLI_DEFINE(cli_show_settings, "Show global and system configuration options"),
	AST_CLI_DEFINE(cli_show_endpoint_identifiers, "List registered endpoint identifiers")
};

AST_RWLIST_HEAD_STATIC(endpoint_formatters, ast_sip_endpoint_formatter);

void ast_sip_register_endpoint_formatter(struct ast_sip_endpoint_formatter *obj)
{
	SCOPED_LOCK(lock, &endpoint_formatters, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&endpoint_formatters, obj, next);
}

void ast_sip_unregister_endpoint_formatter(struct ast_sip_endpoint_formatter *obj)
{
	struct ast_sip_endpoint_formatter *i;
	SCOPED_LOCK(lock, &endpoint_formatters, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_formatters, i, next) {
		if (i == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

int ast_sip_format_endpoint_ami(struct ast_sip_endpoint *endpoint,
				struct ast_sip_ami *ami, int *count)
{
	int res = 0;
	struct ast_sip_endpoint_formatter *i;
	SCOPED_LOCK(lock, &endpoint_formatters, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	*count = 0;
	AST_RWLIST_TRAVERSE(&endpoint_formatters, i, next) {
		if (i->format_ami && ((res = i->format_ami(endpoint, ami)) < 0)) {
			return res;
		}

		if (!res) {
			(*count)++;
		}
	}
	return 0;
}

pjsip_endpoint *ast_sip_get_pjsip_endpoint(void)
{
	return ast_pjsip_endpoint;
}

int ast_sip_will_uri_survive_restart(pjsip_sip_uri *uri, struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata)
{
	pj_str_t host_name;
	int result = 1;

	/* Determine if the contact cannot survive a restart/boot. */
	if (uri->port == rdata->pkt_info.src_port
		&& !pj_strcmp(&uri->host,
			pj_cstr(&host_name, rdata->pkt_info.src_name))
		/* We have already checked if the URI scheme is sip: or sips: */
		&& PJSIP_TRANSPORT_IS_RELIABLE(rdata->tp_info.transport)) {
		pj_str_t type_name;

		/* Determine the transport parameter value */
		if (!strcasecmp("WSS", rdata->tp_info.transport->type_name)) {
			/* WSS is special, as it needs to be ws. */
			pj_cstr(&type_name, "ws");
		} else {
			pj_cstr(&type_name, rdata->tp_info.transport->type_name);
		}

		if (!pj_stricmp(&uri->transport_param, &type_name)
			&& (endpoint->nat.rewrite_contact
				/* Websockets are always rewritten */
				|| !pj_stricmp(&uri->transport_param,
					pj_cstr(&type_name, "ws")))) {
			/*
			 * The contact was rewritten to the reliable transport's
			 * source address.  Disconnecting the transport for any
			 * reason invalidates the contact.
			 */
			result = 0;
		}
	}

	return result;
}

pjsip_sip_uri *ast_sip_get_contact_sip_uri(pjsip_tx_data *tdata)
{
	pjsip_contact_hdr *contact = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL);

	if (!contact || (!PJSIP_URI_SCHEME_IS_SIP(contact->uri) && !PJSIP_URI_SCHEME_IS_SIPS(contact->uri))) {
		return NULL;
	}

	return pjsip_uri_get_uri(contact->uri);
}

/*! \brief Callback function for finding the transport the request is going out on */
static int find_transport_state_in_use(void *obj, void *arg, int flags)
{
	struct ast_sip_transport_state *transport_state = obj;
	struct ast_sip_request_transport_details *details = arg;

	/* If an explicit transport or factory matches then this is what is in use, if we are unavailable
	 * to compare based on that we make sure that the type is the same and the source IP address/port are the same
	 */
	if (transport_state && ((details->transport && details->transport == transport_state->transport) ||
		(details->factory && details->factory == transport_state->factory) ||
		((details->type == transport_state->type) && (transport_state->factory) &&
			!pj_strcmp(&transport_state->factory->addr_name.host, &details->local_address) &&
			transport_state->factory->addr_name.port == details->local_port))) {
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

struct ast_sip_transport_state *ast_sip_find_transport_state_in_use(struct ast_sip_request_transport_details *details) {
	RAII_VAR(struct ao2_container *, transport_states, NULL, ao2_cleanup);

	if (!(transport_states = ast_sip_get_transport_states())) {
		return NULL;
	}

	return ao2_callback(transport_states, 0, find_transport_state_in_use, details);
}

int ast_sip_rewrite_uri_to_local(pjsip_sip_uri *uri, pjsip_tx_data *tdata) {
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_transport_state *, transport_state, NULL, ao2_cleanup);
	struct ast_sip_request_transport_details details;
	pjsip_sip_uri *tmp_uri;
	pjsip_dialog *dlg;
	struct ast_sockaddr addr = { { 0, } };

	if ((tmp_uri = ast_sip_get_contact_sip_uri(tdata))) {
		pj_strdup(tdata->pool, &uri->host, &tmp_uri->host);
		uri->port = tmp_uri->port;
	} else if ((dlg = pjsip_tdata_get_dlg(tdata))
		&& (tmp_uri = pjsip_uri_get_uri(dlg->local.info->uri))
		&& (PJSIP_URI_SCHEME_IS_SIP(tmp_uri) || PJSIP_URI_SCHEME_IS_SIPS(tmp_uri))) {
		pj_strdup(tdata->pool, &uri->host, &tmp_uri->host);
		uri->port = tmp_uri->port;
	}

	if (ast_sip_set_request_transport_details(&details, tdata, 1)
		|| !(transport_state = ast_sip_find_transport_state_in_use(&details))
		|| !(transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_state->id))) {
		return 0;
	}

	if (transport_state->localnet) {
		ast_sockaddr_parse(&addr, tdata->tp_info.dst_name, PARSE_PORT_FORBID);
		ast_sockaddr_set_port(&addr, tdata->tp_info.dst_port);
		if (ast_sip_transport_is_local(transport_state, &addr)) {
			return 0;
		}
	}

	if (!ast_sockaddr_isnull(&transport_state->external_signaling_address)) {
		pj_strdup2(tdata->pool, &uri->host, ast_sockaddr_stringify_host(&transport_state->external_signaling_address));
	}

	if (transport->external_signaling_port) {
		uri->port = transport->external_signaling_port;
	}

	return 0;
}

int ast_sip_set_request_transport_details(struct ast_sip_request_transport_details *details, pjsip_tx_data *tdata,
	int use_ipv6) {
	pjsip_sip_uri *uri;
	pjsip_via_hdr *via;
	long transport_type;

	if (!details || !tdata) {
		return -1;
	}

	/* If IPv6 should be considered, un-set Bit 7 to make TCP6 equal to TCP and TLS6 equal to TLS */
	transport_type = use_ipv6 ? tdata->tp_info.transport->key.type & ~(PJSIP_TRANSPORT_IPV6)
		: tdata->tp_info.transport->key.type;

	if (tdata->tp_sel.type == PJSIP_TPSELECTOR_TRANSPORT) {
		details->transport = tdata->tp_sel.u.transport;
	} else if (tdata->tp_sel.type == PJSIP_TPSELECTOR_LISTENER) {
		details->factory = tdata->tp_sel.u.listener;
	} else if (transport_type == PJSIP_TRANSPORT_UDP || transport_type == PJSIP_TRANSPORT_UDP6) {
		/* Connectionless uses the same transport for all requests */
		details->type = AST_TRANSPORT_UDP;
		details->transport = tdata->tp_info.transport;
	} else {
		if (transport_type == PJSIP_TRANSPORT_TCP) {
			details->type = AST_TRANSPORT_TCP;
		} else if (transport_type == PJSIP_TRANSPORT_TLS) {
			details->type = AST_TRANSPORT_TLS;
		} else {
			/* Unknown transport type, we can't map. */
			return -1;
		}

		if ((uri = ast_sip_get_contact_sip_uri(tdata))) {
			details->local_address = uri->host;
			details->local_port = uri->port;
		} else if ((tdata->msg->type == PJSIP_REQUEST_MSG) &&
			(via = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_VIA, NULL))) {
			details->local_address = via->sent_by.host;
			details->local_port = via->sent_by.port;
		} else {
			return -1;
		}

		if (!details->local_port) {
			details->local_port = (details->type == AST_TRANSPORT_TLS) ? 5061 : 5060;
		}
	}
	return 0;
}

int ast_sip_get_transport_name(const struct ast_sip_endpoint *endpoint,
	pjsip_sip_uri *sip_uri, char *buf, size_t buf_len)
{
	char *host = NULL;
	static const pj_str_t x_name = { AST_SIP_X_AST_TXP, AST_SIP_X_AST_TXP_LEN };
	pjsip_param *x_transport;

	if (!ast_strlen_zero(endpoint->transport)) {
		ast_copy_string(buf, endpoint->transport, buf_len);
		return 0;
	}

	x_transport = pjsip_param_find(&sip_uri->other_param, &x_name);
	if (!x_transport) {
		return -1;
	}

	/* Only use x_transport if the uri host is an ip (4 or 6) address */
	host = ast_alloca(sip_uri->host.slen + 1);
	ast_copy_pj_str(host, &sip_uri->host, sip_uri->host.slen + 1);
	if (!ast_sockaddr_parse(NULL, host, PARSE_PORT_FORBID)) {
		return -1;
	}

	ast_copy_pj_str(buf, &x_transport->value, buf_len);

	return 0;
}

int ast_sip_dlg_set_transport(const struct ast_sip_endpoint *endpoint, pjsip_dialog *dlg,
	pjsip_tpselector *selector)
{
	pjsip_sip_uri *uri;
	pjsip_tpselector sel = { .type = PJSIP_TPSELECTOR_NONE, };

	uri = pjsip_uri_get_uri(dlg->target);
	if (!selector) {
		selector = &sel;
	}

	ast_sip_set_tpselector_from_ep_or_uri(endpoint, uri, selector);

	pjsip_dlg_set_transport(dlg, selector);

	if (selector == &sel) {
		ast_sip_tpselector_unref(&sel);
	}

	return 0;
}

static int sip_dialog_create_from(pj_pool_t *pool, pj_str_t *from, const char *user,
	const char *domain, const pj_str_t *target, pjsip_tpselector *selector)
{
	pj_str_t tmp, local_addr;
	pjsip_uri *uri;
	pjsip_sip_uri *sip_uri;
	pjsip_transport_type_e type;
	int local_port;
	char default_user[PJSIP_MAX_URL_SIZE];

	if (ast_strlen_zero(user)) {
		ast_sip_get_default_from_user(default_user, sizeof(default_user));
		user = default_user;
	}

	/* Parse the provided target URI so we can determine what transport it will end up using */
	pj_strdup_with_null(pool, &tmp, target);

	if (!(uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0)) ||
	    (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return -1;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	/* Determine the transport type to use */
	type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
	if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri)) {
		if (type == PJSIP_TRANSPORT_UNSPECIFIED
			|| !(pjsip_transport_get_flag_from_type(type) & PJSIP_TRANSPORT_SECURE)) {
			type = PJSIP_TRANSPORT_TLS;
		}
	} else if (!sip_uri->transport_param.slen) {
		type = PJSIP_TRANSPORT_UDP;
	} else if (type == PJSIP_TRANSPORT_UNSPECIFIED) {
		return -1;
	}

	/* If the host is IPv6 turn the transport into an IPv6 version */
	if (pj_strchr(&sip_uri->host, ':')) {
		type |= PJSIP_TRANSPORT_IPV6;
	}

	/* In multidomain scenario, username may contain @ with domain info */
	if (!ast_sip_get_disable_multi_domain() && strchr(user, '@')) {
		from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
		from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				"<sip:%s%s%s>",
				user,
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");
		return 0;
	}

	if (!ast_strlen_zero(domain)) {
		from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
		from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				"<sip:%s@%s%s%s>",
				user,
				domain,
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");
		return 0;
	}

	/* Get the local bound address for the transport that will be used when communicating with the provided URI */
	if (pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), pool, type, selector,
							      &local_addr, &local_port) != PJ_SUCCESS) {

		/* If no local address can be retrieved using the transport manager use the host one */
		pj_strdup(pool, &local_addr, pj_gethostname());
		local_port = pjsip_transport_get_default_port_for_type(PJSIP_TRANSPORT_UDP);
	}

	/* If IPv6 was specified in the transport, set the proper type */
	if (pj_strchr(&local_addr, ':')) {
		type |= PJSIP_TRANSPORT_IPV6;
	}

	from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
	from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				      "<sip:%s@%s%.*s%s:%d%s%s>",
				      user,
				      (type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
				      (int)local_addr.slen,
				      local_addr.ptr,
				      (type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
				      local_port,
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");

	return 0;
}

int ast_sip_set_tpselector_from_transport(const struct ast_sip_transport *transport, pjsip_tpselector *selector)
{
	int res = 0;
	struct ast_sip_transport_state *transport_state;

	transport_state = ast_sip_get_transport_state(ast_sorcery_object_get_id(transport));
	if (!transport_state) {
		ast_log(LOG_ERROR, "Unable to retrieve PJSIP transport state for '%s'\n",
			ast_sorcery_object_get_id(transport));
		return -1;
	}

	/* Only flows maintain dynamic state which needs protection */
	if (transport_state->flow) {
		ao2_lock(transport_state);
	}

	if (transport_state->transport) {
		selector->type = PJSIP_TPSELECTOR_TRANSPORT;
		selector->u.transport = transport_state->transport;
		pjsip_transport_add_ref(selector->u.transport);
	} else if (transport_state->factory) {
		selector->type = PJSIP_TPSELECTOR_LISTENER;
		selector->u.listener = transport_state->factory;
	} else if (transport->type == AST_TRANSPORT_WS || transport->type == AST_TRANSPORT_WSS) {
		/* The WebSocket transport has no factory as it can not create outgoing connections, so
		 * even if an endpoint is locked to a WebSocket transport we let the PJSIP logic
		 * find the existing connection if available and use it.
		 */
	} else if (transport->flow) {
		/* This is a child of another transport, so we need to establish a new connection */
#ifdef HAVE_PJSIP_TRANSPORT_DISABLE_CONNECTION_REUSE
		selector->disable_connection_reuse = PJ_TRUE;
#else
		ast_log(LOG_WARNING, "Connection reuse could not be disabled on transport '%s' as support is not available\n",
			ast_sorcery_object_get_id(transport));
#endif
	} else {
		res = -1;
	}

	if (transport_state->flow) {
		ao2_unlock(transport_state);
	}

	ao2_ref(transport_state, -1);

	return res;
}

int ast_sip_set_tpselector_from_transport_name(const char *transport_name, pjsip_tpselector *selector)
{
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);

	if (ast_strlen_zero(transport_name)) {
		return 0;
	}

	transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_name);
	if (!transport) {
		ast_log(LOG_ERROR, "Unable to retrieve PJSIP transport '%s'\n",
			transport_name);
		return -1;
	}

	return ast_sip_set_tpselector_from_transport(transport, selector);
}

int ast_sip_set_tpselector_from_ep_or_uri(const struct ast_sip_endpoint *endpoint,
	pjsip_sip_uri *sip_uri, pjsip_tpselector *selector)
{
	char transport_name[128];

	if (ast_sip_get_transport_name(endpoint, sip_uri, transport_name, sizeof(transport_name))) {
		return 0;
	}

	return ast_sip_set_tpselector_from_transport_name(transport_name, selector);
}

void ast_sip_tpselector_unref(pjsip_tpselector *selector)
{
	if (selector->type == PJSIP_TPSELECTOR_TRANSPORT && selector->u.transport) {
		pjsip_transport_dec_ref(selector->u.transport);
	}
}

void ast_sip_add_usereqphone(const struct ast_sip_endpoint *endpoint, pj_pool_t *pool, pjsip_uri *uri)
{
	pjsip_sip_uri *sip_uri;
	int i = 0;
	static const pj_str_t STR_PHONE = { "phone", 5 };

	if (!endpoint || !endpoint->usereqphone || (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	if (!pj_strlen(&sip_uri->user)) {
		return;
	}

	if (pj_strbuf(&sip_uri->user)[0] == '+') {
		i = 1;
	}

	/* Test URI user against allowed characters in AST_DIGIT_ANY */
	for (; i < pj_strlen(&sip_uri->user); i++) {
		if (!strchr(AST_DIGIT_ANY, pj_strbuf(&sip_uri->user)[i])) {
			break;
		}
	}

	if (i < pj_strlen(&sip_uri->user)) {
		return;
	}

	sip_uri->user_param = STR_PHONE;
}

pjsip_dialog *ast_sip_create_dialog_uac(const struct ast_sip_endpoint *endpoint,
	const char *uri, const char *request_user)
{
	char enclosed_uri[PJSIP_MAX_URL_SIZE];
	pj_str_t local_uri = { "sip:temp@temp", 13 }, remote_uri, target_uri;
	pj_status_t res;
	pjsip_dialog *dlg = NULL;
	const char *outbound_proxy = endpoint->outbound_proxy;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	static const pj_str_t HCONTACT = { "Contact", 7 };

	if (!ast_begins_with(uri, "<")) {
		snprintf(enclosed_uri, sizeof(enclosed_uri), "<%s>", uri);
	} else {
		snprintf(enclosed_uri, sizeof(enclosed_uri), "%s", uri);
	}
	pj_cstr(&remote_uri, enclosed_uri);

	pj_cstr(&target_uri, uri);

	res = pjsip_dlg_create_uac(pjsip_ua_instance(), &local_uri, NULL, &remote_uri, &target_uri, &dlg);
	if (res == PJ_SUCCESS && !(PJSIP_URI_SCHEME_IS_SIP(dlg->target) || PJSIP_URI_SCHEME_IS_SIPS(dlg->target))) {
		/* dlg->target is a pjsip_other_uri, but it's assumed to be a
		 * pjsip_sip_uri below. Fail fast. */
		res = PJSIP_EINVALIDURI;
		pjsip_dlg_terminate(dlg);
	}
	if (res != PJ_SUCCESS) {
		if (res == PJSIP_EINVALIDURI) {
			ast_log(LOG_ERROR,
				"Endpoint '%s': Could not create dialog to invalid URI '%s'.  Is endpoint registered and reachable?\n",
				ast_sorcery_object_get_id(endpoint), uri);
		}
		return NULL;
	}

	/* We have to temporarily bump up the sess_count here so the dialog is not prematurely destroyed */
	dlg->sess_count++;

	ast_sip_dlg_set_transport(endpoint, dlg, &selector);

	if (sip_dialog_create_from(dlg->pool, &local_uri, endpoint->fromuser, endpoint->fromdomain, &remote_uri, &selector)) {
		dlg->sess_count--;
		pjsip_dlg_terminate(dlg);
		ast_sip_tpselector_unref(&selector);
		return NULL;
	}

	ast_sip_tpselector_unref(&selector);

	/* Update the dialog with the new local URI, we do it afterwards so we can use the dialog pool for construction */
	pj_strdup_with_null(dlg->pool, &dlg->local.info_str, &local_uri);
	dlg->local.info->uri = pjsip_parse_uri(dlg->pool, dlg->local.info_str.ptr, dlg->local.info_str.slen, 0);
	if (!dlg->local.info->uri) {
		ast_log(LOG_ERROR,
			"Could not parse URI '%s' for endpoint '%s'\n",
			dlg->local.info_str.ptr, ast_sorcery_object_get_id(endpoint));
		dlg->sess_count--;
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

	dlg->local.contact = pjsip_parse_hdr(dlg->pool, &HCONTACT, local_uri.ptr, local_uri.slen, NULL);

	if (!ast_strlen_zero(endpoint->contact_user)) {
		pjsip_sip_uri *sip_uri;

		sip_uri = pjsip_uri_get_uri(dlg->local.contact->uri);
		pj_strdup2(dlg->pool, &sip_uri->user, endpoint->contact_user);
	}

	/* If a request user has been specified and we are permitted to change it, do so */
	if (!ast_strlen_zero(request_user)) {
		pjsip_sip_uri *sip_uri;

		if (PJSIP_URI_SCHEME_IS_SIP(dlg->target) || PJSIP_URI_SCHEME_IS_SIPS(dlg->target)) {
			sip_uri = pjsip_uri_get_uri(dlg->target);
			pj_strdup2(dlg->pool, &sip_uri->user, request_user);
		}
		if (PJSIP_URI_SCHEME_IS_SIP(dlg->remote.info->uri) || PJSIP_URI_SCHEME_IS_SIPS(dlg->remote.info->uri)) {
			sip_uri = pjsip_uri_get_uri(dlg->remote.info->uri);
			pj_strdup2(dlg->pool, &sip_uri->user, request_user);
		}
	}

	/* Add the user=phone parameter if applicable */
	ast_sip_add_usereqphone(endpoint, dlg->pool, dlg->target);
	ast_sip_add_usereqphone(endpoint, dlg->pool, dlg->remote.info->uri);
	ast_sip_add_usereqphone(endpoint, dlg->pool, dlg->local.info->uri);

	if (!ast_strlen_zero(outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };
		pj_str_t tmp;

		pj_list_init(&route_set);

		pj_strdup2_with_null(dlg->pool, &tmp, outbound_proxy);
		if (!(route = pjsip_parse_hdr(dlg->pool, &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL))) {
			ast_log(LOG_ERROR, "Could not create dialog to endpoint '%s' as outbound proxy URI '%s' is not valid\n",
				ast_sorcery_object_get_id(endpoint), outbound_proxy);
			dlg->sess_count--;
			pjsip_dlg_terminate(dlg);
			return NULL;
		}
		pj_list_insert_nodes_before(&route_set, route);

		pjsip_dlg_set_route_set(dlg, &route_set);
	}

	dlg->sess_count--;

	return dlg;
}

/*!
 * \brief Determine if a SIPS Contact header is required.
 *
 * This uses the guideline provided in RFC 3261 Section 12.1.1 to
 * determine if the Contact header must be a sips: URI.
 *
 * \param rdata The incoming dialog-starting request
 * \retval 0 SIPS not required
 * \retval 1 SIPS required
 */
static int uas_use_sips_contact(pjsip_rx_data *rdata)
{
	pjsip_rr_hdr *record_route;

	if (PJSIP_URI_SCHEME_IS_SIPS(rdata->msg_info.msg->line.req.uri)) {
		return 1;
	}

	record_route = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_RECORD_ROUTE, NULL);
	if (record_route) {
		if (PJSIP_URI_SCHEME_IS_SIPS(&record_route->name_addr)) {
			return 1;
		}
	} else {
		pjsip_contact_hdr *contact;

		contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
		ast_assert(contact != NULL);
		if (PJSIP_URI_SCHEME_IS_SIPS(contact->uri)) {
			return 1;
		}
	}

	return 0;
}

typedef pj_status_t (*create_dlg_uac)(pjsip_user_agent *ua, pjsip_rx_data *rdata,
	const pj_str_t *contact, pjsip_dialog **p_dlg);

static pjsip_dialog *create_dialog_uas(const struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata, pj_status_t *status, create_dlg_uac create_fun)
{
	pjsip_dialog *dlg;
	pj_str_t contact;
	pjsip_transport_type_e type = rdata->tp_info.transport->key.type;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	pjsip_transport *transport;
	pjsip_contact_hdr *contact_hdr;

	ast_assert(status != NULL);

	contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
	if (!contact_hdr || ast_sip_set_tpselector_from_ep_or_uri(endpoint, pjsip_uri_get_uri(contact_hdr->uri),
		&selector)) {
		return NULL;
	}

	transport = rdata->tp_info.transport;
	if (selector.type == PJSIP_TPSELECTOR_TRANSPORT) {
		transport = selector.u.transport;
	}
	type = transport->key.type;

	contact.ptr = pj_pool_alloc(rdata->tp_info.pool, PJSIP_MAX_URL_SIZE);
	contact.slen = pj_ansi_snprintf(contact.ptr, PJSIP_MAX_URL_SIZE,
			"<%s:%s%s%s%.*s%s:%d%s%s>",
			uas_use_sips_contact(rdata) ? "sips" : "sip",
			(type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
			S_OR(endpoint->contact_user, ""),
                        (!ast_strlen_zero(endpoint->contact_user)) ? "@" : "",
			(int)transport->local_name.host.slen,
			transport->local_name.host.ptr,
			(type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
			transport->local_name.port,
			(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
			(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");

	*status = create_fun(pjsip_ua_instance(), rdata, &contact, &dlg);
	if (*status != PJ_SUCCESS) {
		char err[PJ_ERR_MSG_SIZE];

		pj_strerror(*status, err, sizeof(err));
		ast_log(LOG_ERROR, "Could not create dialog with endpoint %s. %s\n",
				ast_sorcery_object_get_id(endpoint), err);
		ast_sip_tpselector_unref(&selector);
		return NULL;
	}

	dlg->sess_count++;
	pjsip_dlg_set_transport(dlg, &selector);
	dlg->sess_count--;

	ast_sip_tpselector_unref(&selector);

	return dlg;
}

pjsip_dialog *ast_sip_create_dialog_uas(const struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pj_status_t *status)
{
#ifdef HAVE_PJSIP_DLG_CREATE_UAS_AND_INC_LOCK
	pjsip_dialog *dlg;

	dlg = create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas_and_inc_lock);
	if (dlg) {
		pjsip_dlg_dec_lock(dlg);
	}

	return dlg;
#else
	return create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas);
#endif
}

pjsip_dialog *ast_sip_create_dialog_uas_locked(const struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata, pj_status_t *status)
{
#ifdef HAVE_PJSIP_DLG_CREATE_UAS_AND_INC_LOCK
	return create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas_and_inc_lock);
#else
	/*
	 * This is put here in order to be compatible with older versions of pjproject.
	 * Best we can do in this case is immediately lock after getting the dialog.
	 * However, that does leave a "gap" between creating and locking.
	 */
	pjsip_dialog *dlg;

	dlg = create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas);
	if (dlg) {
		pjsip_dlg_inc_lock(dlg);
	}

	return dlg;
#endif
 }

int ast_sip_create_rdata_with_contact(pjsip_rx_data *rdata, char *packet, const char *src_name, int src_port,
	char *transport_type, const char *local_name, int local_port, const char *contact)
{
	pj_str_t tmp;

	/*
	 * Initialize the error list in case there is a parse error
	 * in the given packet.
	 */
	pj_list_init(&rdata->msg_info.parse_err);

	rdata->tp_info.transport = PJ_POOL_ZALLOC_T(rdata->tp_info.pool, pjsip_transport);
	if (!rdata->tp_info.transport) {
		return -1;
	}

	ast_copy_string(rdata->pkt_info.packet, packet, sizeof(rdata->pkt_info.packet));
	ast_copy_string(rdata->pkt_info.src_name, src_name, sizeof(rdata->pkt_info.src_name));
	rdata->pkt_info.src_port = src_port;
	pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&tmp, src_name), &rdata->pkt_info.src_addr);
	pj_sockaddr_set_port(&rdata->pkt_info.src_addr, src_port);

	pjsip_parse_rdata(packet, strlen(packet), rdata);
	if (!rdata->msg_info.msg || !pj_list_empty(&rdata->msg_info.parse_err)) {
		return -1;
	}

	if (!ast_strlen_zero(contact)) {
		pjsip_contact_hdr *contact_hdr;

		contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
		if (contact_hdr) {
			contact_hdr->uri = pjsip_parse_uri(rdata->tp_info.pool, (char *)contact,
				strlen(contact), PJSIP_PARSE_URI_AS_NAMEADDR);
			if (!contact_hdr->uri) {
				ast_log(LOG_WARNING, "Unable to parse contact URI from '%s'.\n", contact);
				return -1;
			}
		}
	}

	pj_strdup2(rdata->tp_info.pool, &rdata->msg_info.via->recvd_param, rdata->pkt_info.src_name);
	rdata->msg_info.via->rport_param = -1;

	rdata->tp_info.transport->key.type = pjsip_transport_get_type_from_name(pj_cstr(&tmp, transport_type));
	rdata->tp_info.transport->type_name = transport_type;
	pj_strdup2(rdata->tp_info.pool, &rdata->tp_info.transport->local_name.host, local_name);
	rdata->tp_info.transport->local_name.port = local_port;

	return 0;
}

int ast_sip_create_rdata(pjsip_rx_data *rdata, char *packet, const char *src_name, int src_port,
	char *transport_type, const char *local_name, int local_port)
{
	return ast_sip_create_rdata_with_contact(rdata, packet, src_name, src_port, transport_type,
		local_name, local_port, NULL);
}

/* PJSIP doesn't know about the INFO method, so we have to define it ourselves */
static const pjsip_method info_method = {PJSIP_OTHER_METHOD, {"INFO", 4} };
static const pjsip_method message_method = {PJSIP_OTHER_METHOD, {"MESSAGE", 7} };
static const pjsip_method refer_method = {PJSIP_OTHER_METHOD, {"REFER", 5} };

static struct {
	const char *method;
	const pjsip_method *pmethod;
} methods [] = {
	{ "INVITE", &pjsip_invite_method },
	{ "CANCEL", &pjsip_cancel_method },
	{ "ACK", &pjsip_ack_method },
	{ "BYE", &pjsip_bye_method },
	{ "REGISTER", &pjsip_register_method },
	{ "OPTIONS", &pjsip_options_method },
	{ "SUBSCRIBE", &pjsip_subscribe_method },
	{ "NOTIFY", &pjsip_notify_method },
	{ "PUBLISH", &pjsip_publish_method },
	{ "INFO", &info_method },
	{ "MESSAGE", &message_method },
	{ "REFER", &refer_method },
};

static const pjsip_method *get_pjsip_method(const char *method)
{
	int i;
	for (i = 0; i < ARRAY_LEN(methods); ++i) {
		if (!strcmp(method, methods[i].method)) {
			return methods[i].pmethod;
		}
	}
	return NULL;
}

static int create_in_dialog_request(const pjsip_method *method, struct pjsip_dialog *dlg, pjsip_tx_data **tdata)
{
	if (pjsip_dlg_create_request(dlg, method, -1, tdata) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to create in-dialog request.\n");
		return -1;
	}

	return 0;
}

static pj_bool_t supplement_on_rx_request(pjsip_rx_data *rdata);
static pjsip_module supplement_module = {
	.name = { "Out of dialog supplement hook", 29 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION - 1,
	.on_rx_request = supplement_on_rx_request,
};

static int create_out_of_dialog_request(const pjsip_method *method, struct ast_sip_endpoint *endpoint,
		const char *uri, struct ast_sip_contact *provided_contact, pjsip_tx_data **tdata)
{
	RAII_VAR(struct ast_sip_contact *, contact, ao2_bump(provided_contact), ao2_cleanup);
	pj_str_t remote_uri;
	pj_str_t from;
	pj_pool_t *pool;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	pjsip_uri *sip_uri;
	const char *fromuser;

	if (ast_strlen_zero(uri)) {
		if (!endpoint && (!contact || ast_strlen_zero(contact->uri))) {
			ast_log(LOG_ERROR, "An endpoint and/or uri must be specified\n");
			return -1;
		}

		if (!contact) {
			contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
		}
		if (!contact || ast_strlen_zero(contact->uri)) {
			ast_log(LOG_WARNING, "Unable to retrieve contact for endpoint %s\n",
					ast_sorcery_object_get_id(endpoint));
			return -1;
		}

		pj_cstr(&remote_uri, contact->uri);
	} else {
		pj_cstr(&remote_uri, uri);
	}

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Outbound request", 256, 256);

	if (!pool) {
		ast_log(LOG_ERROR, "Unable to create PJLIB memory pool\n");
		return -1;
	}

	sip_uri = pjsip_parse_uri(pool, remote_uri.ptr, remote_uri.slen, 0);
	if (!sip_uri || (!PJSIP_URI_SCHEME_IS_SIP(sip_uri) && !PJSIP_URI_SCHEME_IS_SIPS(sip_uri))) {
		ast_log(LOG_ERROR, "Unable to create outbound %.*s request to endpoint %s as URI '%s' is not valid\n",
			(int) pj_strlen(&method->name), pj_strbuf(&method->name),
			endpoint ? ast_sorcery_object_get_id(endpoint) : "<none>",
			pj_strbuf(&remote_uri));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	ast_sip_set_tpselector_from_ep_or_uri(endpoint, pjsip_uri_get_uri(sip_uri), &selector);

	fromuser = endpoint ? (!ast_strlen_zero(endpoint->fromuser) ? endpoint->fromuser : ast_sorcery_object_get_id(endpoint)) : NULL;
	if (sip_dialog_create_from(pool, &from, fromuser,
				endpoint ? endpoint->fromdomain : NULL, &remote_uri, &selector)) {
		ast_log(LOG_ERROR, "Unable to create From header for %.*s request to endpoint %s\n",
				(int) pj_strlen(&method->name), pj_strbuf(&method->name),
				endpoint ? ast_sorcery_object_get_id(endpoint) : "<none>");
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		ast_sip_tpselector_unref(&selector);
		return -1;
	}

	if (pjsip_endpt_create_request(ast_sip_get_pjsip_endpoint(), method, &remote_uri,
			&from, &remote_uri, &from, NULL, -1, NULL, tdata) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create outbound %.*s request to endpoint %s\n",
				(int) pj_strlen(&method->name), pj_strbuf(&method->name),
				endpoint ? ast_sorcery_object_get_id(endpoint) : "<none>");
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		ast_sip_tpselector_unref(&selector);
		return -1;
	}

	pjsip_tx_data_set_transport(*tdata, &selector);

	ast_sip_tpselector_unref(&selector);

	if (endpoint && !ast_strlen_zero(endpoint->contact_user)){
		pjsip_contact_hdr *contact_hdr;
		pjsip_sip_uri *contact_uri;
		static const pj_str_t HCONTACT = { "Contact", 7 };
		static const pj_str_t HCONTACTSHORT = { "m", 1 };

		contact_hdr = pjsip_msg_find_hdr_by_names((*tdata)->msg, &HCONTACT, &HCONTACTSHORT, NULL);
		if (contact_hdr) {
			contact_uri = pjsip_uri_get_uri(contact_hdr->uri);
			pj_strdup2((*tdata)->pool, &contact_uri->user, endpoint->contact_user);
		}
	}

	/* Add the user=phone parameter if applicable */
	ast_sip_add_usereqphone(endpoint, (*tdata)->pool, (*tdata)->msg->line.req.uri);

	/* If an outbound proxy is specified on the endpoint apply it to this request */
	if (endpoint && !ast_strlen_zero(endpoint->outbound_proxy) &&
		ast_sip_set_outbound_proxy((*tdata), endpoint->outbound_proxy)) {
		ast_log(LOG_ERROR, "Unable to apply outbound proxy on request %.*s to endpoint %s as outbound proxy URI '%s' is not valid\n",
			(int) pj_strlen(&method->name), pj_strbuf(&method->name), ast_sorcery_object_get_id(endpoint),
			endpoint->outbound_proxy);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	ast_sip_mod_data_set((*tdata)->pool, (*tdata)->mod_data, supplement_module.id, MOD_DATA_CONTACT, ao2_bump(contact));

	/* We can release this pool since request creation copied all the necessary
	 * data into the outbound request's pool
	 */
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return 0;
}

int ast_sip_create_request(const char *method, struct pjsip_dialog *dlg,
		struct ast_sip_endpoint *endpoint, const char *uri,
		struct ast_sip_contact *contact, pjsip_tx_data **tdata)
{
	const pjsip_method *pmethod = get_pjsip_method(method);

	if (!pmethod) {
		ast_log(LOG_WARNING, "Unknown method '%s'. Cannot send request\n", method);
		return -1;
	}

	if (dlg) {
		return create_in_dialog_request(pmethod, dlg, tdata);
	} else {
		ast_assert(endpoint != NULL);
		return create_out_of_dialog_request(pmethod, endpoint, uri, contact, tdata);
	}
}

AST_RWLIST_HEAD_STATIC(supplements, ast_sip_supplement);

void ast_sip_register_supplement(struct ast_sip_supplement *supplement)
{
	struct ast_sip_supplement *iter;
	int inserted = 0;
	SCOPED_LOCK(lock, &supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&supplements, iter, next) {
		if (iter->priority > supplement->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(supplement, next);
			inserted = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!inserted) {
		AST_RWLIST_INSERT_TAIL(&supplements, supplement, next);
	}
}

void ast_sip_unregister_supplement(struct ast_sip_supplement *supplement)
{
	struct ast_sip_supplement *iter;
	SCOPED_LOCK(lock, &supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&supplements, iter, next) {
		if (supplement == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static int send_in_dialog_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg)
{
	if (pjsip_dlg_send_request(dlg, tdata, -1, NULL) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to send in-dialog request.\n");
		return -1;
	}
	return 0;
}

static pj_bool_t does_method_match(const pj_str_t *message_method, const char *supplement_method)
{
	pj_str_t method;

	if (ast_strlen_zero(supplement_method)) {
		return PJ_TRUE;
	}

	pj_cstr(&method, supplement_method);

	return pj_stristr(&method, message_method) ? PJ_TRUE : PJ_FALSE;
}

#define TIMER_INACTIVE		0
#define TIMEOUT_TIMER2		5

/*! \brief Structure to hold information about an outbound request */
struct send_request_data {
	/*! The endpoint associated with this request */
	struct ast_sip_endpoint *endpoint;
	/*! Information to be provided to the callback upon receipt of a response */
	void *token;
	/*! The callback to be called upon receipt of a response */
	void (*callback)(void *token, pjsip_event *e);
	/*! Number of challenges received. */
	unsigned int challenge_count;
};

static void send_request_data_destroy(void *obj)
{
	struct send_request_data *req_data = obj;

	ao2_cleanup(req_data->endpoint);
}

static struct send_request_data *send_request_data_alloc(struct ast_sip_endpoint *endpoint,
	void *token, void (*callback)(void *token, pjsip_event *e))
{
	struct send_request_data *req_data;

	req_data = ao2_alloc_options(sizeof(*req_data), send_request_data_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!req_data) {
		return NULL;
	}

	req_data->endpoint = ao2_bump(endpoint);
	req_data->token = token;
	req_data->callback = callback;

	return req_data;
}

struct send_request_wrapper {
	/*! Information to be provided to the callback upon receipt of a response */
	void *token;
	/*! The callback to be called upon receipt of a response */
	void (*callback)(void *token, pjsip_event *e);
	/*! Non-zero when the callback is called. */
	unsigned int cb_called;
	/*! Non-zero if endpt_send_request_cb() was called. */
	unsigned int send_cb_called;
	/*! Timeout timer. */
	pj_timer_entry *timeout_timer;
	/*! Original timeout. */
	pj_int32_t timeout;
	/*! The transmit data. */
	pjsip_tx_data *tdata;
};

/*! \internal This function gets called by pjsip when the transaction ends,
 * even if it timed out.  The lock prevents a race condition if both the pjsip
 * transaction timer and our own timer expire simultaneously.
 */
static void endpt_send_request_cb(void *token, pjsip_event *e)
{
	struct send_request_wrapper *req_wrapper = token;
	unsigned int cb_called;

	/*
	 * Needed because we cannot otherwise tell if this callback was
	 * called when pjsip_endpt_send_request() returns error.
	 */
	req_wrapper->send_cb_called = 1;

	if (e->body.tsx_state.type == PJSIP_EVENT_TIMER) {
		ast_debug(2, "%p: PJSIP tsx timer expired\n", req_wrapper);

		if (req_wrapper->timeout_timer
			&& req_wrapper->timeout_timer->id != TIMEOUT_TIMER2) {
			ast_debug(3, "%p: Timeout already handled\n", req_wrapper);
			ao2_ref(req_wrapper, -1);
			return;
		}
	} else {
		ast_debug(2, "%p: PJSIP tsx response received\n", req_wrapper);
	}

	ao2_lock(req_wrapper);

	/* It's possible that our own timer was already processing while
	 * we were waiting on the lock so check the timer id.  If it's
	 * still TIMER2 then we still need to process.
	 */
	if (req_wrapper->timeout_timer
		&& req_wrapper->timeout_timer->id == TIMEOUT_TIMER2) {
		int timers_cancelled = 0;

		ast_debug(3, "%p: Cancelling timer\n", req_wrapper);

		timers_cancelled = pj_timer_heap_cancel_if_active(
			pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()),
			req_wrapper->timeout_timer, TIMER_INACTIVE);
		if (timers_cancelled > 0) {
			/* If the timer was cancelled the callback will never run so
			 * clean up its reference to the wrapper.
			 */
			ast_debug(3, "%p: Timer cancelled\n", req_wrapper);
			ao2_ref(req_wrapper, -1);
		} else {
			/*
			 * If it wasn't cancelled, it MAY be in the callback already
			 * waiting on the lock.  When we release the lock, it will
			 * now know not to proceed.
			 */
			ast_debug(3, "%p: Timer already expired\n", req_wrapper);
		}
	}

	cb_called = req_wrapper->cb_called;
	req_wrapper->cb_called = 1;
	ao2_unlock(req_wrapper);

	/* It's possible that our own timer expired and called the callbacks
	 * so no need to call them again.
	 */
	if (!cb_called && req_wrapper->callback) {
		req_wrapper->callback(req_wrapper->token, e);
		ast_debug(2, "%p: Callbacks executed\n", req_wrapper);
	}

	ao2_ref(req_wrapper, -1);
}

/*! \internal This function gets called by our own timer when it expires.
 * If the timer is cancelled however, the function does NOT get called.
 * The lock prevents a race condition if both the pjsip transaction timer
 * and our own timer expire simultaneously.
 */
static void send_request_timer_callback(pj_timer_heap_t *theap, pj_timer_entry *entry)
{
	struct send_request_wrapper *req_wrapper = entry->user_data;
	unsigned int cb_called;

	ast_debug(2, "%p: Internal tsx timer expired after %d msec\n",
		req_wrapper, req_wrapper->timeout);

	ao2_lock(req_wrapper);
	/*
	 * If the id is not TIMEOUT_TIMER2 then the timer was cancelled
	 * before we got the lock or it was already handled so just clean up.
	 */
	if (entry->id != TIMEOUT_TIMER2) {
		ao2_unlock(req_wrapper);
		ast_debug(3, "%p: Timeout already handled\n", req_wrapper);
		ao2_ref(req_wrapper, -1);
		return;
	}
	entry->id = TIMER_INACTIVE;

	ast_debug(3, "%p: Timer handled here\n", req_wrapper);

	cb_called = req_wrapper->cb_called;
	req_wrapper->cb_called = 1;
	ao2_unlock(req_wrapper);

	if (!cb_called && req_wrapper->callback) {
		pjsip_event event;

		PJSIP_EVENT_INIT_TX_MSG(event, req_wrapper->tdata);
		event.body.tsx_state.type = PJSIP_EVENT_TIMER;

		req_wrapper->callback(req_wrapper->token, &event);
		ast_debug(2, "%p: Callbacks executed\n", req_wrapper);
	}

	ao2_ref(req_wrapper, -1);
}

static void send_request_wrapper_destructor(void *obj)
{
	struct send_request_wrapper *req_wrapper = obj;

	pjsip_tx_data_dec_ref(req_wrapper->tdata);
	ast_debug(2, "%p: wrapper destroyed\n", req_wrapper);
}

static pj_status_t endpt_send_request(struct ast_sip_endpoint *endpoint,
	pjsip_tx_data *tdata, pj_int32_t timeout, void *token, pjsip_endpt_send_callback cb)
{
	struct send_request_wrapper *req_wrapper;
	pj_status_t ret_val;
	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();

	if (!cb && token) {
		/* Silly.  Without a callback we cannot do anything with token. */
		pjsip_tx_data_dec_ref(tdata);
		return PJ_EINVAL;
	}

	/* Create wrapper to detect if the callback was actually called on an error. */
	req_wrapper = ao2_alloc(sizeof(*req_wrapper), send_request_wrapper_destructor);
	if (!req_wrapper) {
		pjsip_tx_data_dec_ref(tdata);
		return PJ_ENOMEM;
	}

	ast_debug(2, "%p: Wrapper created\n", req_wrapper);

	req_wrapper->token = token;
	req_wrapper->callback = cb;
	req_wrapper->timeout = timeout;
	req_wrapper->timeout_timer = NULL;
	req_wrapper->tdata = tdata;
	/* Add a reference to tdata.  The wrapper destructor cleans it up. */
	pjsip_tx_data_add_ref(tdata);

	if (timeout > 0) {
		pj_time_val timeout_timer_val = { timeout / 1000, timeout % 1000 };

		req_wrapper->timeout_timer = PJ_POOL_ALLOC_T(tdata->pool, pj_timer_entry);

		ast_debug(2, "%p: Set timer to %d msec\n", req_wrapper, timeout);

		pj_timer_entry_init(req_wrapper->timeout_timer, TIMEOUT_TIMER2,
			req_wrapper, send_request_timer_callback);

		/* We need to insure that the wrapper and tdata are available if/when the
		 * timer callback is executed.
		 */
		ao2_ref(req_wrapper, +1);
		ret_val = pj_timer_heap_schedule(pjsip_endpt_get_timer_heap(endpt),
			req_wrapper->timeout_timer, &timeout_timer_val);
		if (ret_val != PJ_SUCCESS) {
			ast_log(LOG_ERROR,
				"Failed to set timer.  Not sending %.*s request to endpoint %s.\n",
				(int) pj_strlen(&tdata->msg->line.req.method.name),
				pj_strbuf(&tdata->msg->line.req.method.name),
				endpoint ? ast_sorcery_object_get_id(endpoint) : "<unknown>");
			ao2_t_ref(req_wrapper, -2, "Drop timer and routine ref");
			pjsip_tx_data_dec_ref(tdata);
			return ret_val;
		}
	}

	/* We need to insure that the wrapper and tdata are available when the
	 * transaction callback is executed.
	 */
	ao2_ref(req_wrapper, +1);
	ret_val = pjsip_endpt_send_request(endpt, tdata, -1, req_wrapper, endpt_send_request_cb);
	if (ret_val != PJ_SUCCESS) {
		char errmsg[PJ_ERR_MSG_SIZE];

		if (!req_wrapper->send_cb_called) {
			/* endpt_send_request_cb is not expected to ever be called now. */
			ao2_ref(req_wrapper, -1);
		}

		/* Complain of failure to send the request. */
		pj_strerror(ret_val, errmsg, sizeof(errmsg));
		ast_log(LOG_ERROR, "Error %d '%s' sending %.*s request to endpoint %s\n",
			(int) ret_val, errmsg, (int) pj_strlen(&tdata->msg->line.req.method.name),
			pj_strbuf(&tdata->msg->line.req.method.name),
			endpoint ? ast_sorcery_object_get_id(endpoint) : "<unknown>");

		if (timeout > 0) {
			int timers_cancelled;

			ao2_lock(req_wrapper);
			timers_cancelled = pj_timer_heap_cancel_if_active(
				pjsip_endpt_get_timer_heap(endpt),
				req_wrapper->timeout_timer, TIMER_INACTIVE);
			if (timers_cancelled > 0) {
				ao2_ref(req_wrapper, -1);
			}

			/* Was the callback called? */
			if (req_wrapper->cb_called) {
				/*
				 * Yes so we cannot report any error.  The callback
				 * has already freed any resources associated with
				 * token.
				 */
				ret_val = PJ_SUCCESS;
			} else {
				/*
				 * No so we claim it is called so our caller can free
				 * any resources associated with token because of
				 * failure.
				 */
				req_wrapper->cb_called = 1;
			}
			ao2_unlock(req_wrapper);
		} else if (req_wrapper->cb_called) {
			/*
			 * We cannot report any error.  The callback has
			 * already freed any resources associated with
			 * token.
			 */
			ret_val = PJ_SUCCESS;
		}
	}

	ao2_ref(req_wrapper, -1);
	return ret_val;
}

int ast_sip_failover_request(pjsip_tx_data *tdata)
{
	pjsip_via_hdr *via;

	if (!tdata || !tdata->dest_info.addr.count
		|| (tdata->dest_info.cur_addr == tdata->dest_info.addr.count - 1)) {
		/* No more addresses to try */
		return 0;
	}

	/* Try next address */
	++tdata->dest_info.cur_addr;

	via = (pjsip_via_hdr*)pjsip_msg_find_hdr(tdata->msg, PJSIP_H_VIA, NULL);
	via->branch_param.slen = 0;

	pjsip_tx_data_invalidate_msg(tdata);

	return 1;
}

static void send_request_cb(void *token, pjsip_event *e);

static int check_request_status(struct send_request_data *req_data, pjsip_event *e)
{
	struct ast_sip_endpoint *endpoint;
	pjsip_transaction *tsx;
	pjsip_tx_data *tdata;
	int res = 0;

	if (!(endpoint = ao2_bump(req_data->endpoint))) {
		return 0;
	}

	tsx = e->body.tsx_state.tsx;

	switch (tsx->status_code) {
	case 401:
	case 407:
		/* Resend the request with a challenge response if we are challenged. */
		res = ++req_data->challenge_count < MAX_RX_CHALLENGES /* Not in a challenge loop */
			&& !ast_sip_create_request_with_auth(&endpoint->outbound_auths,
				e->body.tsx_state.src.rdata, tsx->last_tx, &tdata);
		break;
	case 408:
	case 503:
		if ((res = ast_sip_failover_request(tsx->last_tx))) {
			tdata = tsx->last_tx;
			/*
			 * Bump the ref since it will be on a new transaction and
			 * we don't want it to go away along with the old transaction.
			 */
			pjsip_tx_data_add_ref(tdata);
		}
		break;
	}

	if (res) {
		res = endpt_send_request(endpoint, tdata, -1,
					 req_data, send_request_cb) == PJ_SUCCESS;
	}

	ao2_ref(endpoint, -1);
	return res;
}

static void send_request_cb(void *token, pjsip_event *e)
{
	struct send_request_data *req_data = token;
	pjsip_rx_data *challenge;
	struct ast_sip_supplement *supplement;

	if (e->type == PJSIP_EVENT_TSX_STATE) {
		switch(e->body.tsx_state.type) {
		case PJSIP_EVENT_TRANSPORT_ERROR:
		case PJSIP_EVENT_TIMER:
			/*
			 * Check the request status on transport error or timeout. A transport
			 * error can occur when a TCP socket closes and that can be the result
			 * of a 503. Also we may need to failover on a timeout (408).
			 */
			if (check_request_status(req_data, e)) {
				return;
			}
			break;
		case PJSIP_EVENT_RX_MSG:
			challenge = e->body.tsx_state.src.rdata;

			/*
			 * Call any supplements that want to know about a response
			 * with any received data.
			 */
			AST_RWLIST_RDLOCK(&supplements);
			AST_LIST_TRAVERSE(&supplements, supplement, next) {
				if (supplement->incoming_response
					&& does_method_match(&challenge->msg_info.cseq->method.name,
						supplement->method)) {
					supplement->incoming_response(req_data->endpoint, challenge);
				}
			}
			AST_RWLIST_UNLOCK(&supplements);

			if (check_request_status(req_data, e)) {
				/*
				 * Request with challenge response or failover sent.
				 * Passed our req_data ref to the new request.
				 */
				return;
			}
			break;
		default:
			ast_log(LOG_ERROR, "Unexpected PJSIP event %u\n", e->body.tsx_state.type);
			break;
		}
	}

	if (req_data->callback) {
		req_data->callback(req_data->token, e);
	}
	ao2_ref(req_data, -1);
}

int ast_sip_send_out_of_dialog_request(pjsip_tx_data *tdata,
	struct ast_sip_endpoint *endpoint, int timeout, void *token,
	void (*callback)(void *token, pjsip_event *e))
{
	struct ast_sip_supplement *supplement;
	struct send_request_data *req_data;
	struct ast_sip_contact *contact;

	req_data = send_request_data_alloc(endpoint, token, callback);
	if (!req_data) {
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}

	if (endpoint) {
		ast_sip_message_apply_transport(endpoint->transport, tdata);
	}

	contact = ast_sip_mod_data_get(tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT);

	AST_RWLIST_RDLOCK(&supplements);
	AST_LIST_TRAVERSE(&supplements, supplement, next) {
		if (supplement->outgoing_request
			&& does_method_match(&tdata->msg->line.req.method.name, supplement->method)) {
			supplement->outgoing_request(endpoint, contact, tdata);
		}
	}
	AST_RWLIST_UNLOCK(&supplements);

	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT, NULL);
	ao2_cleanup(contact);

	if (endpt_send_request(endpoint, tdata, timeout, req_data, send_request_cb)
		!= PJ_SUCCESS) {
		ao2_cleanup(req_data);
		return -1;
	}

	return 0;
}

int ast_sip_send_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg,
	struct ast_sip_endpoint *endpoint, void *token,
	void (*callback)(void *token, pjsip_event *e))
{
	ast_assert(tdata->msg->type == PJSIP_REQUEST_MSG);

	if (dlg) {
		return send_in_dialog_request(tdata, dlg);
	} else {
		return ast_sip_send_out_of_dialog_request(tdata, endpoint, -1, token, callback);
	}
}

int ast_sip_set_outbound_proxy(pjsip_tx_data *tdata, const char *proxy)
{
	pjsip_route_hdr *route;
	static const pj_str_t ROUTE_HNAME = { "Route", 5 };
	pj_str_t tmp;

	pj_strdup2_with_null(tdata->pool, &tmp, proxy);
	if (!(route = pjsip_parse_hdr(tdata->pool, &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL))) {
		return -1;
	}

	pj_list_insert_nodes_before(&tdata->msg->hdr, (pjsip_hdr*)route);

	return 0;
}

int ast_sip_add_header(pjsip_tx_data *tdata, const char *name, const char *value)
{
	pj_str_t hdr_name;
	pj_str_t hdr_value;
	pjsip_generic_string_hdr *hdr;

	pj_cstr(&hdr_name, name);
	pj_cstr(&hdr_value, value);

	hdr = pjsip_generic_string_hdr_create(tdata->pool, &hdr_name, &hdr_value);

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
	return 0;
}

pjsip_generic_string_hdr *ast_sip_add_header2(pjsip_tx_data *tdata,
	const char *name, const char *value)
{
	pj_str_t hdr_name;
	pj_str_t hdr_value;
	pjsip_generic_string_hdr *hdr;

	pj_cstr(&hdr_name, name);
	pj_cstr(&hdr_value, value);

	hdr = pjsip_generic_string_hdr_create(tdata->pool, &hdr_name, &hdr_value);

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
	return hdr;
}

static pjsip_msg_body *ast_body_to_pjsip_body(pj_pool_t *pool, const struct ast_sip_body *body)
{
	pj_str_t type;
	pj_str_t subtype;
	pj_str_t body_text;

	pj_cstr(&type, body->type);
	pj_cstr(&subtype, body->subtype);
	pj_cstr(&body_text, body->body_text);

	return pjsip_msg_body_create(pool, &type, &subtype, &body_text);
}

int ast_sip_add_body(pjsip_tx_data *tdata, const struct ast_sip_body *body)
{
	pjsip_msg_body *pjsip_body = ast_body_to_pjsip_body(tdata->pool, body);
	tdata->msg->body = pjsip_body;
	return 0;
}

int ast_sip_add_body_multipart(pjsip_tx_data *tdata, const struct ast_sip_body *bodies[], int num_bodies)
{
	int i;
	/* NULL for type and subtype automatically creates "multipart/mixed" */
	pjsip_msg_body *body = pjsip_multipart_create(tdata->pool, NULL, NULL);

	for (i = 0; i < num_bodies; ++i) {
		pjsip_multipart_part *part = pjsip_multipart_create_part(tdata->pool);
		part->body = ast_body_to_pjsip_body(tdata->pool, bodies[i]);
		pjsip_multipart_add_part(tdata->pool, body, part);
	}

	tdata->msg->body = body;
	return 0;
}

int ast_sip_append_body(pjsip_tx_data *tdata, const char *body_text)
{
	size_t combined_size = strlen(body_text) + tdata->msg->body->len;
	struct ast_str *body_buffer = ast_str_alloca(combined_size);

	ast_str_set(&body_buffer, 0, "%.*s%s", (int) tdata->msg->body->len, (char *) tdata->msg->body->data, body_text);

	tdata->msg->body->data = pj_pool_alloc(tdata->pool, combined_size);
	pj_memcpy(tdata->msg->body->data, ast_str_buffer(body_buffer), combined_size);
	tdata->msg->body->len = combined_size;

	return 0;
}

struct ast_taskprocessor *ast_sip_create_serializer_group(const char *name, struct ast_serializer_shutdown_group *shutdown_group)
{
	return ast_threadpool_serializer_group(name, sip_threadpool, shutdown_group);
}

struct ast_taskprocessor *ast_sip_create_serializer(const char *name)
{
	return ast_sip_create_serializer_group(name, NULL);
}

int ast_sip_push_task(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (!serializer) {
		serializer = ast_serializer_pool_get(sip_serializer_pool);
	}

	return ast_taskprocessor_push(serializer, sip_task, task_data);
}

struct sync_task_data {
	ast_mutex_t lock;
	ast_cond_t cond;
	int complete;
	int fail;
	int (*task)(void *);
	void *task_data;
};

static int sync_task(void *data)
{
	struct sync_task_data *std = data;
	int ret;

	std->fail = std->task(std->task_data);

	/*
	 * Once we unlock std->lock after signaling, we cannot access
	 * std again.  The thread waiting within ast_sip_push_task_wait()
	 * is free to continue and release its local variable (std).
	 */
	ast_mutex_lock(&std->lock);
	std->complete = 1;
	ast_cond_signal(&std->cond);
	ret = std->fail;
	ast_mutex_unlock(&std->lock);
	return ret;
}

static int ast_sip_push_task_wait(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	/* This method is an onion */
	struct sync_task_data std;

	memset(&std, 0, sizeof(std));
	ast_mutex_init(&std.lock);
	ast_cond_init(&std.cond, NULL);
	std.task = sip_task;
	std.task_data = task_data;

	if (ast_sip_push_task(serializer, sync_task, &std)) {
		ast_mutex_destroy(&std.lock);
		ast_cond_destroy(&std.cond);
		return -1;
	}

	ast_mutex_lock(&std.lock);
	while (!std.complete) {
		ast_cond_wait(&std.cond, &std.lock);
	}
	ast_mutex_unlock(&std.lock);

	ast_mutex_destroy(&std.lock);
	ast_cond_destroy(&std.cond);
	return std.fail;
}

int ast_sip_push_task_wait_servant(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (ast_sip_thread_is_servant()) {
		return sip_task(task_data);
	}

	return ast_sip_push_task_wait(serializer, sip_task, task_data);
}

int ast_sip_push_task_synchronous(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	return ast_sip_push_task_wait_servant(serializer, sip_task, task_data);
}

int ast_sip_push_task_wait_serializer(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (!serializer) {
		/* Caller doesn't care which PJSIP serializer the task executes under. */
		serializer = ast_serializer_pool_get(sip_serializer_pool);
		if (!serializer) {
			/* No serializer picked to execute the task */
			return -1;
		}
	}
	if (ast_taskprocessor_is_task(serializer)) {
		/*
		 * We are the requested serializer so we must execute
		 * the task now or deadlock waiting on ourself to
		 * execute it.
		 */
		return sip_task(task_data);
	}

	return ast_sip_push_task_wait(serializer, sip_task, task_data);
}

void ast_copy_pj_str(char *dest, const pj_str_t *src, size_t size)
{
	size_t chars_to_copy = MIN(size - 1, pj_strlen(src));
	memcpy(dest, pj_strbuf(src), chars_to_copy);
	dest[chars_to_copy] = '\0';
}

int ast_copy_pj_str2(char **dest, const pj_str_t *src)
{
	int res = ast_asprintf(dest, "%.*s", (int)pj_strlen(src), pj_strbuf(src));

	if (res < 0) {
		*dest = NULL;
	}

	return res;
}

int ast_sip_are_media_types_equal(pjsip_media_type *a, pjsip_media_type *b)
{
	int rc = 0;
	if (a != NULL && b != NULL) {
	    rc = pjsip_media_type_cmp(a, b, 0) ? 0 : 1;
	}
	return rc;
}

int ast_sip_is_media_type_in(pjsip_media_type *a, ...)
{
	int rc = 0;
	pjsip_media_type *b = NULL;
	va_list ap;

	ast_assert(a != NULL);
	va_start(ap, a);

	while ((b = va_arg(ap, pjsip_media_type *)) != (pjsip_media_type *)SENTINEL) {
		if (pjsip_media_type_cmp(a, b, 0) == 0) {
			rc = 1;
			break;
		}
	}
	va_end(ap);

	return rc;
}

int ast_sip_is_content_type(pjsip_media_type *content_type, char *type, char *subtype)
{
	pjsip_media_type compare;

	if (!content_type) {
		return 0;
	}

	pjsip_media_type_init2(&compare, type, subtype);

	return pjsip_media_type_cmp(content_type, &compare, 0) ? 0 : -1;
}

pj_caching_pool caching_pool;
pj_pool_t *memory_pool;
pj_thread_t *monitor_thread;
static int monitor_continue;

static void *monitor_thread_exec(void *endpt)
{
	while (monitor_continue) {
		const pj_time_val delay = {0, 10};
		pjsip_endpt_handle_events(ast_pjsip_endpoint, &delay);
	}
	return NULL;
}

static void stop_monitor_thread(void)
{
	monitor_continue = 0;
	pj_thread_join(monitor_thread);
}

AST_THREADSTORAGE(pj_thread_storage);
AST_THREADSTORAGE(servant_id_storage);
#define SIP_SERVANT_ID 0x5E2F1D

static void sip_thread_start(void)
{
	pj_thread_desc *desc;
	pj_thread_t *thread;
	uint32_t *servant_id;

	servant_id = ast_threadstorage_get(&servant_id_storage, sizeof(*servant_id));
	if (!servant_id) {
		ast_log(LOG_ERROR, "Could not set SIP servant ID in thread-local storage.\n");
		return;
	}
	*servant_id = SIP_SERVANT_ID;

	desc = ast_threadstorage_get(&pj_thread_storage, sizeof(pj_thread_desc));
	if (!desc) {
		ast_log(LOG_ERROR, "Could not get thread desc from thread-local storage. Expect awful things to occur\n");
		return;
	}
	pj_bzero(*desc, sizeof(*desc));

	if (pj_thread_register("Asterisk Thread", *desc, &thread) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Couldn't register thread with PJLIB.\n");
	}
}

int ast_sip_thread_is_servant(void)
{
	uint32_t *servant_id;

	if (monitor_thread &&
			pthread_self() == *(pthread_t *)pj_thread_get_os_handle(monitor_thread)) {
		return 1;
	}

	servant_id = ast_threadstorage_get(&servant_id_storage, sizeof(*servant_id));
	if (!servant_id) {
		return 0;
	}

	return *servant_id == SIP_SERVANT_ID;
}

void *ast_sip_dict_get(void *ht, const char *key)
{
	unsigned int hval = 0;

	if (!ht) {
		return NULL;
	}

	return pj_hash_get(ht, key, PJ_HASH_KEY_STRING, &hval);
}

void *ast_sip_dict_set(pj_pool_t* pool, void *ht,
		       const char *key, void *val)
{
	if (!ht) {
		ht = pj_hash_create(pool, 11);
	}

	pj_hash_set(pool, ht, key, PJ_HASH_KEY_STRING, 0, val);

	return ht;
}

static pj_bool_t supplement_on_rx_request(pjsip_rx_data *rdata)
{
	struct ast_sip_supplement *supplement;

	if (pjsip_rdata_get_dlg(rdata)) {
		return PJ_FALSE;
	}

	AST_RWLIST_RDLOCK(&supplements);
	AST_LIST_TRAVERSE(&supplements, supplement, next) {
		if (supplement->incoming_request
			&& does_method_match(&rdata->msg_info.msg->line.req.method.name, supplement->method)) {
			struct ast_sip_endpoint *endpoint;

			endpoint = ast_pjsip_rdata_get_endpoint(rdata);
			supplement->incoming_request(endpoint, rdata);
			ao2_cleanup(endpoint);
		}
	}
	AST_RWLIST_UNLOCK(&supplements);

	return PJ_FALSE;
}

static void supplement_outgoing_response(pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint)
{
	struct ast_sip_supplement *supplement;
	pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);
	struct ast_sip_contact *contact = ast_sip_mod_data_get(tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT);

	if (sip_endpoint) {
		ast_sip_message_apply_transport(sip_endpoint->transport, tdata);
	}

	AST_RWLIST_RDLOCK(&supplements);
	AST_LIST_TRAVERSE(&supplements, supplement, next) {
		if (supplement->outgoing_response && does_method_match(&cseq->method.name, supplement->method)) {
			supplement->outgoing_response(sip_endpoint, contact, tdata);
		}
	}
	AST_RWLIST_UNLOCK(&supplements);

	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT, NULL);
	ao2_cleanup(contact);
}

int ast_sip_send_response(pjsip_response_addr *res_addr, pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint)
{
	pj_status_t status;

	supplement_outgoing_response(tdata, sip_endpoint);
	status = pjsip_endpt_send_response(ast_sip_get_pjsip_endpoint(), res_addr, tdata, NULL, NULL);
	if (status != PJ_SUCCESS) {
		pjsip_tx_data_dec_ref(tdata);
	}

	return status == PJ_SUCCESS ? 0 : -1;
}

static void pool_destroy_callback(void *arg)
{
	pj_pool_t *pool = (pj_pool_t *)arg;
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
}

static void clean_contact_from_tdata(pjsip_tx_data *tdata)
{
	struct ast_sip_contact *contact;
	contact = ast_sip_mod_data_get(tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT);
	ao2_cleanup(contact);
	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT, NULL);
	pjsip_tx_data_dec_ref(tdata);
}

int ast_sip_send_stateful_response(pjsip_rx_data *rdata, pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint)
{
	pjsip_transaction *tsx;
	pj_grp_lock_t *tsx_glock;
	pj_pool_t *pool;

	/* Create and initialize global lock pool */
	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "stateful response", PJSIP_POOL_TSX_LEN, PJSIP_POOL_TSX_INC);
	if (!pool){
		/* ast_sip_create_response bumps the refcount of the contact and adds it to the tdata.
		 * We'll leak that reference if we don't get rid of it here.
		 */
		clean_contact_from_tdata(tdata);
		return -1;
	}
	/* Create with handler so that we can release the pool once the glock derefs out */
	if(pj_grp_lock_create_w_handler(pool, NULL, pool, &pool_destroy_callback, &tsx_glock) != PJ_SUCCESS) {
		clean_contact_from_tdata(tdata);
		pool_destroy_callback((void *) pool);
		return -1;
	}
	/* We need an additional reference as the qualify thread may destroy this out
	 * from under us. Add it now before it gets added to the tsx. */
	pj_grp_lock_add_ref(tsx_glock);

	if (pjsip_tsx_create_uas2(NULL, rdata, tsx_glock, &tsx) != PJ_SUCCESS) {
		clean_contact_from_tdata(tdata);
		pj_grp_lock_dec_ref(tsx_glock);
		return -1;
	}

	pjsip_tsx_recv_msg(tsx, rdata);
	supplement_outgoing_response(tdata, sip_endpoint);

	if (pjsip_tsx_send_msg(tsx, tdata) != PJ_SUCCESS) {
		pj_grp_lock_dec_ref(tsx_glock);
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}

	pj_grp_lock_dec_ref(tsx_glock);
	return 0;
}

int ast_sip_create_response(const pjsip_rx_data *rdata, int st_code,
	struct ast_sip_contact *contact, pjsip_tx_data **tdata)
{
	int res = pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, st_code, NULL, tdata);

	if (!res) {
		ast_sip_mod_data_set((*tdata)->pool, (*tdata)->mod_data, supplement_module.id, MOD_DATA_CONTACT, ao2_bump(contact));
	}

	return res;
}

int ast_sip_get_host_ip(int af, pj_sockaddr *addr)
{
	if (af == pj_AF_INET() && !ast_strlen_zero(host_ip_ipv4_string)) {
		pj_sockaddr_copy_addr(addr, &host_ip_ipv4);
		return 0;
	} else if (af == pj_AF_INET6() && !ast_strlen_zero(host_ip_ipv6_string)) {
		pj_sockaddr_copy_addr(addr, &host_ip_ipv6);
		return 0;
	}

	return -1;
}

const char *ast_sip_get_host_ip_string(int af)
{
	if (af == pj_AF_INET()) {
		return host_ip_ipv4_string;
	} else if (af == pj_AF_INET6()) {
		return host_ip_ipv6_string;
	}

	return NULL;
}

int ast_sip_dtmf_to_str(const enum ast_sip_dtmf_mode dtmf,
		        char *buf, size_t buf_len)
{
	switch (dtmf) {
	case AST_SIP_DTMF_NONE:
		ast_copy_string(buf, "none", buf_len);
		break;
	case AST_SIP_DTMF_RFC_4733:
		ast_copy_string(buf, "rfc4733", buf_len);
		break;
	case AST_SIP_DTMF_INBAND:
		ast_copy_string(buf, "inband", buf_len);
		break;
	case AST_SIP_DTMF_INFO:
		ast_copy_string(buf, "info", buf_len);
		break;
	case AST_SIP_DTMF_AUTO:
		ast_copy_string(buf, "auto", buf_len);
		break;
	case AST_SIP_DTMF_AUTO_INFO:
		ast_copy_string(buf, "auto_info", buf_len);
		break;
	default:
		buf[0] = '\0';
		return -1;
	}
	return 0;
}

int ast_sip_str_to_dtmf(const char * dtmf_mode)
{
	int result = -1;

	if (!strcasecmp(dtmf_mode, "info")) {
		result = AST_SIP_DTMF_INFO;
	} else if (!strcasecmp(dtmf_mode, "rfc4733")) {
		result = AST_SIP_DTMF_RFC_4733;
	} else if (!strcasecmp(dtmf_mode, "inband")) {
		result = AST_SIP_DTMF_INBAND;
	} else if (!strcasecmp(dtmf_mode, "none")) {
		result = AST_SIP_DTMF_NONE;
	} else if (!strcasecmp(dtmf_mode, "auto")) {
		result = AST_SIP_DTMF_AUTO;
	} else if (!strcasecmp(dtmf_mode, "auto_info")) {
		result = AST_SIP_DTMF_AUTO_INFO;
	}

	return result;
}

const char *ast_sip_call_codec_pref_to_str(struct ast_flags pref)
{
	const char *value;

	if (ast_sip_call_codec_pref_test(pref, LOCAL) &&  ast_sip_call_codec_pref_test(pref, INTERSECT) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "local";
	} else if (ast_sip_call_codec_pref_test(pref, LOCAL) &&  ast_sip_call_codec_pref_test(pref, UNION) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "local_merge";
	} else if (ast_sip_call_codec_pref_test(pref, LOCAL) &&  ast_sip_call_codec_pref_test(pref, INTERSECT) && ast_sip_call_codec_pref_test(pref, FIRST)) {
		value = "local_first";
	} else if (ast_sip_call_codec_pref_test(pref, REMOTE) &&  ast_sip_call_codec_pref_test(pref, INTERSECT) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "remote";
	} else if (ast_sip_call_codec_pref_test(pref, REMOTE) &&  ast_sip_call_codec_pref_test(pref, UNION) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "remote_merge";
	} else if (ast_sip_call_codec_pref_test(pref, REMOTE) &&  ast_sip_call_codec_pref_test(pref, UNION) && ast_sip_call_codec_pref_test(pref, FIRST)) {
		value = "remote_first";
	} else {
		value = "unknown";
	}

	return value;
}

int ast_sip_call_codec_str_to_pref(struct ast_flags *pref, const char *pref_str, int is_outgoing)
{
	pref->flags = 0;

	if (strcmp(pref_str, "local") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_LOCAL | AST_SIP_CALL_CODEC_PREF_INTERSECT | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (is_outgoing && strcmp(pref_str, "local_merge") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_LOCAL | AST_SIP_CALL_CODEC_PREF_UNION | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (strcmp(pref_str, "local_first") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_LOCAL | AST_SIP_CALL_CODEC_PREF_INTERSECT | AST_SIP_CALL_CODEC_PREF_FIRST);
	} else if (strcmp(pref_str, "remote") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_REMOTE | AST_SIP_CALL_CODEC_PREF_INTERSECT | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (is_outgoing && strcmp(pref_str, "remote_merge") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_REMOTE | AST_SIP_CALL_CODEC_PREF_UNION | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (strcmp(pref_str, "remote_first") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_REMOTE | AST_SIP_CALL_CODEC_PREF_UNION | AST_SIP_CALL_CODEC_PREF_FIRST);
	} else {
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Set an ast_party_id name and number based on an identity header.
 * \param hdr From, P-Asserted-Identity, or Remote-Party-ID header on incoming message
 * \param[out] id The ID to set data on
 */
static void set_id_from_hdr(pjsip_fromto_hdr *hdr, struct ast_party_id *id)
{
	char cid_name[AST_CHANNEL_NAME];
	char cid_num[AST_CHANNEL_NAME];
	size_t cid_name_size = AST_CHANNEL_NAME;
	pjsip_name_addr *id_name_addr = (pjsip_name_addr *) hdr->uri;
	char *semi;
	enum ast_utf8_replace_result result;

	ast_copy_pj_str(cid_num, ast_sip_pjsip_uri_get_username(hdr->uri), sizeof(cid_num));
	/* Always truncate caller-id number at a semicolon. */
	semi = strchr(cid_num, ';');
	if (semi) {
		/*
		 * We need to be able to handle URI's looking like
		 * "sip:1235557890;phone-context=national@x.x.x.x;user=phone"
		 *
		 * Where the uri->user field will result in:
		 * "1235557890;phone-context=national"
		 *
		 * People don't care about anything after the semicolon
		 * showing up on their displays even though the RFC
		 * allows the semicolon.
		 */
		*semi = '\0';
	}

	/*
	 * It's safe to pass a NULL or empty string as the source.
	 * The result will be an empty string assuming the destination
	 * size was at least 1.
	 */
	result = ast_utf8_replace_invalid_chars(cid_name, &cid_name_size,
		id_name_addr->display.ptr, id_name_addr->display.slen);

	if (result != AST_UTF8_REPLACE_VALID) {
		ast_log(LOG_WARNING, "CallerID Name '" PJSTR_PRINTF_SPEC
			"' for number '%s' has invalid UTF-8 characters which "
			"were replaced",
			PJSTR_PRINTF_VAR(id_name_addr->display), cid_num);
	}

	ast_free(id->name.str);
	id->name.str = ast_strdup(cid_name);
	if (!ast_strlen_zero(cid_name)) {
		id->name.valid = 1;
	}
	ast_free(id->number.str);
	id->number.str = ast_strdup(cid_num);
	if (!ast_strlen_zero(cid_num)) {
		id->number.valid = 1;
	}
}

/*!
 * \internal
 * \brief Get a P-Asserted-Identity or Remote-Party-ID header from an incoming message
 *
 * This function will parse the header as if it were a From header. This allows for us
 * to easily manipulate the URI, as well as add, modify, or remove parameters from the
 * header
 *
 * \param rdata The incoming message
 * \param header_name The name of the ID header to find
 * \retval NULL No ID header present or unable to parse ID header
 * \retval non-NULL The parsed ID header
 */
static pjsip_fromto_hdr *get_id_header(pjsip_rx_data *rdata, const pj_str_t *header_name)
{
	static const pj_str_t from = { "From", 4 };
	pj_str_t header_content;
	pjsip_fromto_hdr *parsed_hdr;
	pjsip_generic_string_hdr *ident = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg,
			header_name, NULL);
	int parsed_len;

	if (!ident) {
		return NULL;
	}

	pj_strdup_with_null(rdata->tp_info.pool, &header_content, &ident->hvalue);

	parsed_hdr = pjsip_parse_hdr(rdata->tp_info.pool, &from, header_content.ptr,
			pj_strlen(&header_content), &parsed_len);

	if (!parsed_hdr) {
		return NULL;
	}

	return parsed_hdr;
}

/*!
 * \internal
 * \brief Set an ast_party_id structure based on data in a P-Asserted-Identity header
 *
 * This makes use of \ref set_id_from_hdr for setting name and number. It uses
 * the contents of a Privacy header in order to set presentation information.
 *
 * \param rdata The incoming message
 * \param[out] id The ID to set
 * \retval 0 Successfully set the party ID
 * \retval non-zero Could not set the party ID
 */
static int set_id_from_pai(pjsip_rx_data *rdata, struct ast_party_id *id)
{
	static const pj_str_t pai_str = { "P-Asserted-Identity", 19 };
	static const pj_str_t privacy_str = { "Privacy", 7 };
	pjsip_fromto_hdr *pai_hdr = get_id_header(rdata, &pai_str);
	pjsip_generic_string_hdr *privacy;

	if (!pai_hdr) {
		return -1;
	}

	set_id_from_hdr(pai_hdr, id);

	if (!id->number.valid) {
		return -1;
	}

	privacy = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &privacy_str, NULL);
	if (!privacy || !pj_stricmp2(&privacy->hvalue, "none")) {
		id->number.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		id->name.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	} else {
		id->number.presentation = AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
		id->name.presentation = AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
	}

	return 0;
}

/*!
 * \internal
 * \brief Set an ast_party_id structure based on data in a Remote-Party-ID header
 *
 * This makes use of \ref set_id_from_hdr for setting name and number. It uses
 * the privacy and screen parameters in order to set presentation information.
 *
 * \param rdata The incoming message
 * \param[out] id The ID to set
 * \retval 0 Succesfully set the party ID
 * \retval non-zero Could not set the party ID
 */
static int set_id_from_rpid(pjsip_rx_data *rdata, struct ast_party_id *id)
{
	static const pj_str_t rpid_str = { "Remote-Party-ID", 15 };
	static const pj_str_t privacy_str = { "privacy", 7 };
	static const pj_str_t screen_str = { "screen", 6 };
	pjsip_fromto_hdr *rpid_hdr = get_id_header(rdata, &rpid_str);
	pjsip_param *screen;
	pjsip_param *privacy;

	if (!rpid_hdr) {
		return -1;
	}

	set_id_from_hdr(rpid_hdr, id);

	if (!id->number.valid) {
		return -1;
	}

	privacy = pjsip_param_find(&rpid_hdr->other_param, &privacy_str);
	screen = pjsip_param_find(&rpid_hdr->other_param, &screen_str);
	if (privacy && !pj_stricmp2(&privacy->value, "full")) {
		id->number.presentation = AST_PRES_RESTRICTED;
		id->name.presentation = AST_PRES_RESTRICTED;
	} else {
		id->number.presentation = AST_PRES_ALLOWED;
		id->name.presentation = AST_PRES_ALLOWED;
	}
	if (screen && !pj_stricmp2(&screen->value, "yes")) {
		id->number.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
		id->name.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
	} else {
		id->number.presentation |= AST_PRES_USER_NUMBER_UNSCREENED;
		id->name.presentation |= AST_PRES_USER_NUMBER_UNSCREENED;
	}

	return 0;
}

/*!
 * \internal
 * \brief Set an ast_party_id structure based on data in a From
 *
 * This makes use of \ref set_id_from_hdr for setting name and number. It uses
 * no information from the message in order to set privacy. It relies on endpoint
 * configuration for privacy information.
 *
 * \param rdata The incoming message
 * \param[out] id The ID to set
 * \retval 0 Succesfully set the party ID
 * \retval non-zero Could not set the party ID
 */
static int set_id_from_from(struct pjsip_rx_data *rdata, struct ast_party_id *id)
{
	pjsip_fromto_hdr *from = pjsip_msg_find_hdr(rdata->msg_info.msg,
			PJSIP_H_FROM, rdata->msg_info.msg->hdr.next);

	if (!from) {
		/* This had better not happen */
		return -1;
	}

	set_id_from_hdr(from, id);

	if (!id->number.valid) {
		return -1;
	}

	return 0;
}

int ast_sip_set_id_connected_line(struct pjsip_rx_data *rdata, struct ast_party_id *id)
{
	return !set_id_from_pai(rdata, id) || !set_id_from_rpid(rdata, id) ? 0 : -1;
}

int ast_sip_set_id_from_invite(struct pjsip_rx_data *rdata, struct ast_party_id *id, struct ast_party_id *default_id, int trust_inbound)
{
	if (trust_inbound && (!set_id_from_pai(rdata, id) || !set_id_from_rpid(rdata, id))) {
		/* Trusted: Check PAI and RPID */
		ast_free(id->tag);
		id->tag = ast_strdup(default_id->tag);
		return 0;
	}
	/* Not trusted: check the endpoint config or use From. */
	ast_party_id_copy(id, default_id);
	if (!default_id->number.valid) {
		set_id_from_from(rdata, id);
	}
	return 0;
}

/*!
 * \brief Set name and number information on an identity header.
 *
 * \param pool Memory pool to use for string duplication
 * \param id_hdr A From, P-Asserted-Identity, or Remote-Party-ID header to modify
 * \param id The identity information to apply to the header
 */
void ast_sip_modify_id_header(pj_pool_t *pool, pjsip_fromto_hdr *id_hdr, const struct ast_party_id *id)
{
	pjsip_name_addr *id_name_addr;
	pjsip_sip_uri *id_uri;

	id_name_addr = (pjsip_name_addr *) id_hdr->uri;
	id_uri = pjsip_uri_get_uri(id_name_addr->uri);

	if (id->name.valid) {
		if (!ast_strlen_zero(id->name.str)) {
			int name_buf_len = strlen(id->name.str) * 2 + 1;
			char *name_buf = ast_alloca(name_buf_len);

			ast_escape_quoted(id->name.str, name_buf, name_buf_len);
			pj_strdup2(pool, &id_name_addr->display, name_buf);
		} else {
			pj_strdup2(pool, &id_name_addr->display, NULL);
		}
	}

	if (id->number.valid) {
		pj_strdup2(pool, &id_uri->user, id->number.str);
	}
}

/*!
 * \brief Find a contact and insert a "user@" into its URI.
 *
 * \param to Original destination (for error messages only)
 * \param endpoint_name Endpoint name (for error messages only)
 * \param aors Command separated list of AORs
 * \param user The user to insert in the contact URI
 * \param uri Pointer to buffer in which to return the URI. Must be freed by caller.
 *
 * \return  0 Success
 * \return -1 Fail
 *
 * \note If the contact URI found for the endpoint already has a user in
 * its URI, it will be replaced by the user passed as an argument to this function.
 */
static int insert_user_in_contact_uri(const char *to, const char *endpoint_name, const char *aors,
	const char *user, char **uri)
{
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);
	pj_pool_t *pool;
	pjsip_name_addr *name_addr;
	pjsip_sip_uri *sip_uri;
	int err = 0;

	contact = ast_sip_location_retrieve_contact_from_aor_list(aors);
	if (!contact) {
		ast_log(LOG_WARNING, "Dest: '%s'. Couldn't find contact for endpoint '%s'\n",
			to, endpoint_name);
		return -1;
	}

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "uri-user-insert", 128, 128);
	if (!pool) {
		ast_log(LOG_WARNING, "Failed to allocate ParseUri endpoint pool.\n");
		return -1;
	}

	name_addr = (pjsip_name_addr *) pjsip_parse_uri(pool, (char*)contact->uri, strlen(contact->uri), PJSIP_PARSE_URI_AS_NAMEADDR);
	if (!name_addr || (!PJSIP_URI_SCHEME_IS_SIP(name_addr->uri) && !PJSIP_URI_SCHEME_IS_SIPS(name_addr->uri))) {
		ast_log(LOG_WARNING, "Failed to parse URI '%s'\n", contact->uri);
		err = -1;
		goto out;
	}

	ast_debug(3, "Dest: '%s' User: '%s'  Endpoint: '%s'  ContactURI: '%s'\n", to, user, endpoint_name, contact->uri);

	sip_uri = pjsip_uri_get_uri(name_addr->uri);
	pj_strset2(&sip_uri->user, (char*)user);

	*uri = ast_malloc(PJSIP_MAX_URL_SIZE);
	if (!(*uri)) {
		err = -1;
		goto out;
	}
	pjsip_uri_print(PJSIP_URI_IN_REQ_URI, name_addr, *uri, PJSIP_MAX_URL_SIZE);

out:
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);

	return err;
}

/*!
 * \internal
 * \brief Get endpoint and URI when the destination is only a single token
 *
 * "destination" could be one of the following:
 * \verbatim
		endpoint_name
		hostname
 * \endverbatim
 *
 * \param to
 * \param destination
 * \param get_default_outbound If nonzero, try to retrieve the default
 * 			       outbound endpoint if no endpoint was found.
 * 			       Otherwise, return NULL if no endpoint was found.
 * \param uri Pointer to URI variable.  Must be freed by caller - even if the return value is NULL!
 * \return endpoint
 */
static struct ast_sip_endpoint *handle_single_token(const char *to, char *destination, int get_default_outbound, char **uri) {
	RAII_VAR(struct ast_sip_contact*, contact, NULL, ao2_cleanup);
	char *endpoint_name = NULL;
	struct ast_sip_endpoint *endpoint = NULL;

	/*
	 * If "destination" is just one token, it could be an endpoint name
	 * or a hostname without a scheme.
	 */

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", destination);
	if (!endpoint) {
		/*
		 * We can only assume it's a hostname.
		 */
		char *temp_uri = ast_malloc(strlen(destination) + strlen("sip:") + 1);
		if (!temp_uri) {
			goto failure;
		}
		sprintf(temp_uri, "sip:%s", destination);
		*uri = temp_uri;
		if (get_default_outbound) {
			endpoint = ast_sip_default_outbound_endpoint();
		}
		ast_debug(3, "Dest: '%s' Didn't find endpoint so adding scheme and using URI '%s'%s\n",
			to, *uri, get_default_outbound ? " with default endpoint" : "");
		return endpoint;
	}

	/*
	 * It's an endpoint
	 */

	endpoint_name = destination;
	contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
	if (!contact) {
		ast_log(LOG_WARNING, "Dest: '%s'. Found endpoint '%s' but didn't find an aor/contact for it\n",
			to, endpoint_name);
		ao2_cleanup(endpoint);
		goto failure;
	}

	*uri = ast_strdup(contact->uri);
	if (!(*uri)) {
		ao2_cleanup(endpoint);
		goto failure;
	}

	ast_debug(3, "Dest: '%s' Found endpoint '%s' and found contact with URI '%s'\n",
		to, endpoint_name, *uri);
	return endpoint;

failure:
	*uri = NULL;
	return NULL;
}

/*!
 * \internal
 * \brief Get endpoint and URI when the destination contained a '/'
 *
 * "to" could be one of the following:
 * \verbatim
		endpoint/aor
		endpoint/<sip[s]:host>
		endpoint/<sip[s]:user@host>
		endpoint/"Bob" <sip[s]:host>
		endpoint/"Bob" <sip[s]:user@host>
		endpoint/sip[s]:host
		endpoint/sip[s]:user@host
		endpoint/host
		endpoint/user@host
 * \endverbatim
 *
 * \param to Destination
 * \param uri Pointer to URI variable.  Must be freed by caller - even if the return value is NULL!
 * \param destination, slash, atsign, scheme
 * \return endpoint
 */
static struct ast_sip_endpoint *handle_slash(const char *to, char *destination, char **uri,
	char *slash, char *atsign, char *scheme)
{
	char *endpoint_name = NULL;
	struct ast_sip_endpoint *endpoint = NULL;
	struct ast_sip_contact *contact = NULL;
	char *user = NULL;
	char *afterslash = slash + 1;
	struct ast_sip_aor *aor;

	if (ast_begins_with(destination, "PJSIP/")) {
		ast_debug(3, "Dest: '%s' Dialplan format'\n", to);
		/*
		 * This has to be the form PJSIP/user@endpoint
		 */
		if (!atsign || strchr(afterslash, '/')) {
			/*
			 * If there's no "user@" or there's a slash somewhere after
			 * "PJSIP/" then we go no further.
			 */
			ast_log(LOG_WARNING,
				"Dest: '%s'. Destinations beginning with 'PJSIP/' must be in the form of 'PJSIP/user@endpoint'\n",
				to);
			goto failure;
		}
		*atsign = '\0';
		user = afterslash;
		endpoint_name = atsign + 1;
		ast_debug(3, "Dest: '%s' User: '%s'  Endpoint: '%s'\n", to, user, endpoint_name);
	} else {
		/*
		 * Either...
		 *	endpoint/aor
		 *	endpoint/uri
		 */
		*slash = '\0';
		endpoint_name = destination;
		ast_debug(3, "Dest: '%s' Endpoint: '%s'\n", to, endpoint_name);
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);
	if (!endpoint) {
		ast_log(LOG_WARNING, "Dest: '%s'. Didn't find endpoint with name '%s'\n",
			to, endpoint_name);
		goto failure;
	}

	if (scheme) {
		/*
		 * If we found a scheme, then everything after the slash MUST be a URI.
		 * We don't need to do any further modification.
		 */
		*uri = ast_strdup(afterslash);
		if (!(*uri)) {
			goto failure;
		}
		ast_debug(3, "Dest: '%s' Found endpoint '%s' and found URI '%s' after '/'\n",
			to, endpoint_name, *uri);
		return endpoint;
	}

	if (user) {
		/*
		 * This has to be the form PJSIP/user@endpoint
		 */
		int rc;

		/*
		 * Set the return URI to be the endpoint's contact URI with the user
		 * portion set to the user that was specified before the endpoint name.
		 */
		rc = insert_user_in_contact_uri(to, endpoint_name, endpoint->aors, user, uri);
		if (rc != 0) {
			/*
			 * insert_user_in_contact_uri prints the warning message.
			 */
			goto failure;
		}
		ast_debug(3, "Dest: '%s' User: '%s'  Endpoint: '%s'  URI: '%s'\n", to, user,
			endpoint_name, *uri);

		return endpoint;
	}

	/*
	 * We're now left with two possibilities...
	 * 	endpoint/aor
	 *	endpoint/uri-without-scheme
	 */
	aor = ast_sip_location_retrieve_aor(afterslash);
	if (!aor) {
		/*
		 * It's probably a URI without a scheme but we don't have a way to tell
		 * for sure.  We're going to assume it is and prepend it with a scheme.
		 */
		*uri = ast_malloc(strlen(afterslash) + strlen("sip:") + 1);
		if (!(*uri)) {
			goto failure;
		}
		sprintf(*uri, "sip:%s", afterslash);
		ast_debug(3, "Dest: '%s' Found endpoint '%s' but didn't find aor after '/' so using URI '%s'\n",
			to, endpoint_name, *uri);
		return endpoint;
	}

	/*
	 * Only one possibility left... There was an aor name after the slash.
	 */
	ast_debug(3, "Dest: '%s' Found endpoint '%s' and found aor '%s' after '/'\n",
		to, endpoint_name, ast_sorcery_object_get_id(aor));

	contact = ast_sip_location_retrieve_first_aor_contact(aor);
	if (!contact) {
		ast_log(LOG_WARNING, "Dest: '%s'. Found endpoint '%s' but didn't find contact for aor '%s'\n",
			to, endpoint_name, ast_sorcery_object_get_id(aor));
		ao2_cleanup(aor);
		goto failure;
	}

	*uri = ast_strdup(contact->uri);
	ao2_cleanup(contact);
	ao2_cleanup(aor);
	if (!(*uri)) {
		goto failure;
	}

	ast_debug(3, "Dest: '%s' Found endpoint '%s' and found contact with URI '%s' for aor '%s'\n",
		to, endpoint_name, *uri, ast_sorcery_object_get_id(aor));

	return endpoint;

failure:
	ao2_cleanup(endpoint);
	*uri = NULL;
	return NULL;
}

/*!
 * \internal
 * \brief Get endpoint and URI when the destination contained a '@' but no '/' or scheme
 *
 * "to" could be one of the following:
 * \verbatim
		<sip[s]:user@host>
		"Bob" <sip[s]:user@host>
		sip[s]:user@host
		user@host
 * \endverbatim
 *
 * \param to Destination
 * \param uri Pointer to URI variable.  Must be freed by caller - even if the return value is NULL!
 * \param destination, slash, atsign, scheme
 * \param get_default_outbound If nonzero, try to retrieve the default
 * 			       outbound endpoint if no endpoint was found.
 * 			       Otherwise, return NULL if no endpoint was found.
 * \return endpoint
 */
static struct ast_sip_endpoint *handle_atsign(const char *to, char *destination, char **uri,
	char *slash, char *atsign, char *scheme, int get_default_outbound)
{
	char *endpoint_name = NULL;
	struct ast_sip_endpoint *endpoint = NULL;
	struct ast_sip_contact *contact = NULL;
	char *afterat = atsign + 1;

	*atsign = '\0';
	endpoint_name = destination;

	/* Apparently there may be ';<user_options>' after the endpoint name ??? */
	AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(endpoint_name);
	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);
	if (!endpoint) {
		/*
		 * It's probably a uri with a user but without a scheme but we don't have a way to tell.
		 * We're going to assume it is and prepend it with a scheme.
		 */
		*uri = ast_malloc(strlen(to) + strlen("sip:") + 1);
		if (!(*uri)) {
			goto failure;
		}
		sprintf(*uri, "sip:%s", to);
		if (get_default_outbound) {
			endpoint = ast_sip_default_outbound_endpoint();
		}
		ast_debug(3, "Dest: '%s' Didn't find endpoint before the '@' so using URI '%s'%s\n",
			to, *uri, get_default_outbound ? " with default endpoint" : "");
		return endpoint;
	}

	/*
	 * OK, it's an endpoint and a domain (which we ignore)
	 */
	contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
	if (!contact) {
		ast_log(LOG_WARNING, "Dest: '%s'. Found endpoint '%s' but didn't find contact\n",
			to, endpoint_name);
		goto failure;
	}

	*uri = ast_strdup(contact->uri);
	ao2_cleanup(contact);
	if (!(*uri)) {
		goto failure;
	}
	ast_debug(3, "Dest: '%s' Found endpoint '%s' and found contact with URI '%s' (discarding domain %s)\n",
		to, endpoint_name, *uri, afterat);

	return endpoint;

failure:
	ao2_cleanup(endpoint);
	*uri = NULL;
	return NULL;
}

struct ast_sip_endpoint *ast_sip_get_endpoint(const char *to, int get_default_outbound, char **uri)
{
	char *destination;
	char *slash = NULL;
	char *atsign = NULL;
	char *scheme = NULL;
	struct ast_sip_endpoint *endpoint = NULL;

	destination = ast_strdupa(to);

	slash = strchr(destination, '/');
	atsign = strchr(destination, '@');
	scheme = S_OR(strstr(destination, "sip:"), strstr(destination, "sips:"));

	if (!slash && !atsign && !scheme) {
		/*
		 * If there's only a single token, it can be either...
		 * 	endpoint
		 * 	host
		 */
		return handle_single_token(to, destination, get_default_outbound, uri);
	}

	if (slash) {
		/*
		 * If there's a '/', then the form must be one of the following...
		 * 	PJSIP/user@endpoint
		 * 	endpoint/aor
		 * 	endpoint/uri
		 */
		return handle_slash(to, destination, uri, slash, atsign, scheme);
	}

	if (atsign && !scheme) {
		/*
		 * If there's an '@' but no scheme then it's either following an endpoint name
		 * and being followed by a domain name (which we discard).
		 * OR is's a user@host uri without a scheme.  It's probably the latter but because
		 * endpoint@domain looks just like user@host, we'll test for endpoint first.
		 */
		return handle_atsign(to, destination, uri, slash, atsign, scheme, get_default_outbound);
	}

	/*
	 * If all else fails, we assume it's a URI or just a hostname.
	 */
	if (scheme) {
		*uri = ast_strdup(destination);
		if (!(*uri)) {
			goto failure;
		}
		ast_debug(3, "Dest: '%s' Didn't find an endpoint but did find a scheme so using URI '%s'%s\n",
			to, *uri, get_default_outbound ? " with default endpoint" : "");
	} else {
		*uri = ast_malloc(strlen(destination) + strlen("sip:") + 1);
		if (!(*uri)) {
			goto failure;
		}
		sprintf(*uri, "sip:%s", destination);
		ast_debug(3, "Dest: '%s' Didn't find an endpoint and didn't find scheme so adding scheme and using URI '%s'%s\n",
			to, *uri, get_default_outbound ? " with default endpoint" : "");
	}
	if (get_default_outbound) {
		endpoint = ast_sip_default_outbound_endpoint();
	}

	return endpoint;

failure:
	ao2_cleanup(endpoint);
	*uri = NULL;
	return NULL;
}

int ast_sip_update_to_uri(pjsip_tx_data *tdata, const char *to)
{
	pjsip_name_addr *parsed_name_addr;
	pjsip_sip_uri *sip_uri;
	pjsip_name_addr *tdata_name_addr;
	pjsip_sip_uri *tdata_sip_uri;
	pjsip_to_hdr *to_hdr;
	char *buf = NULL;
#define DEBUG_BUF_SIZE 256

	parsed_name_addr = (pjsip_name_addr *) pjsip_parse_uri(tdata->pool, (char*)to, strlen(to),
		PJSIP_PARSE_URI_AS_NAMEADDR);

	if (!parsed_name_addr || (!PJSIP_URI_SCHEME_IS_SIP(parsed_name_addr->uri)
			&& !PJSIP_URI_SCHEME_IS_SIPS(parsed_name_addr->uri))) {
		ast_log(LOG_WARNING, "To address '%s' is not a valid SIP/SIPS URI\n", to);
		return -1;
	}

	sip_uri = pjsip_uri_get_uri(parsed_name_addr->uri);
	if (DEBUG_ATLEAST(3)) {
		buf = ast_alloca(DEBUG_BUF_SIZE);
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, sip_uri, buf, DEBUG_BUF_SIZE);
		ast_debug(3, "Parsed To: %.*s  %s\n", (int)parsed_name_addr->display.slen,
			parsed_name_addr->display.ptr, buf);
	}

	to_hdr = PJSIP_MSG_TO_HDR(tdata->msg);
	tdata_name_addr = to_hdr ? (pjsip_name_addr *) to_hdr->uri : NULL;
	if (!tdata_name_addr || (!PJSIP_URI_SCHEME_IS_SIP(tdata_name_addr->uri)
			&& !PJSIP_URI_SCHEME_IS_SIPS(tdata_name_addr->uri))) {
		/* Highly unlikely but we have to check */
		ast_log(LOG_WARNING, "tdata To address '%s' is not a valid SIP/SIPS URI\n", to);
		return -1;
	}

	tdata_sip_uri = pjsip_uri_get_uri(tdata_name_addr->uri);
	if (DEBUG_ATLEAST(3)) {
		buf[0] = '\0';
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, tdata_sip_uri, buf, DEBUG_BUF_SIZE);
		ast_debug(3, "Original tdata To: %.*s  %s\n", (int)tdata_name_addr->display.slen,
			tdata_name_addr->display.ptr, buf);
	}

	/* Replace the uri */
	pjsip_sip_uri_assign(tdata->pool, tdata_sip_uri, sip_uri);
	/* The display name isn't part of the URI so we need to replace it separately */
	pj_strdup(tdata->pool, &tdata_name_addr->display, &parsed_name_addr->display);

	if (DEBUG_ATLEAST(3)) {
		buf[0] = '\0';
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, tdata_sip_uri, buf, 256);
		ast_debug(3, "New tdata To: %.*s  %s\n", (int)tdata_name_addr->display.slen,
			tdata_name_addr->display.ptr, buf);
	}

	return 0;
#undef DEBUG_BUF_SIZE
}

int ast_sip_update_from(pjsip_tx_data *tdata, char *from)
{
	pjsip_name_addr *name_addr;
	pjsip_sip_uri *uri;
	pjsip_name_addr *parsed_name_addr;
	pjsip_from_hdr *from_hdr;

	if (ast_strlen_zero(from)) {
		return 0;
	}

	from_hdr = PJSIP_MSG_FROM_HDR(tdata->msg);
	if (!from_hdr) {
		return -1;
	}
	name_addr = (pjsip_name_addr *) from_hdr->uri;
	uri = pjsip_uri_get_uri(name_addr);

	parsed_name_addr = (pjsip_name_addr *) pjsip_parse_uri(tdata->pool, from,
		strlen(from), PJSIP_PARSE_URI_AS_NAMEADDR);
	if (parsed_name_addr) {
		pjsip_sip_uri *parsed_uri;

		if (!PJSIP_URI_SCHEME_IS_SIP(parsed_name_addr->uri)
				&& !PJSIP_URI_SCHEME_IS_SIPS(parsed_name_addr->uri)) {
			ast_log(LOG_WARNING, "From address '%s' is not a valid SIP/SIPS URI\n", from);
			return -1;
		}

		parsed_uri = pjsip_uri_get_uri(parsed_name_addr->uri);

		if (pj_strlen(&parsed_name_addr->display)) {
			pj_strdup(tdata->pool, &name_addr->display, &parsed_name_addr->display);
		}

		/* Unlike the To header, we only want to replace the user, host and port */
		pj_strdup(tdata->pool, &uri->user, &parsed_uri->user);
		pj_strdup(tdata->pool, &uri->host, &parsed_uri->host);
		uri->port = parsed_uri->port;

		return 0;
	} else {
		/* assume it is 'user[@domain]' format */
		char *domain = strchr(from, '@');

		if (domain) {
			pj_str_t pj_from;

			pj_strset3(&pj_from, from, domain);
			pj_strdup(tdata->pool, &uri->user, &pj_from);

			pj_strdup2(tdata->pool, &uri->host, domain + 1);
		} else {
			pj_strdup2(tdata->pool, &uri->user, from);
		}

		return 0;
	}

	return -1;
}

static void remove_request_headers(pjsip_endpoint *endpt)
{
	const pjsip_hdr *request_headers = pjsip_endpt_get_request_headers(endpt);
	pjsip_hdr *iter = request_headers->next;

	while (iter != request_headers) {
		pjsip_hdr *to_erase = iter;
		iter = iter->next;
		pj_list_erase(to_erase);
	}
}

long ast_sip_threadpool_queue_size(void)
{
	return ast_threadpool_queue_size(sip_threadpool);
}

struct ast_threadpool *ast_sip_threadpool(void)
{
	return sip_threadpool;
}

int ast_sip_is_uri_sip_sips(pjsip_uri *uri)
{
	return (PJSIP_URI_SCHEME_IS_SIP(uri) || PJSIP_URI_SCHEME_IS_SIPS(uri));
}

int ast_sip_is_allowed_uri(pjsip_uri *uri)
{
	return (ast_sip_is_uri_sip_sips(uri) || PJSIP_URI_SCHEME_IS_TEL(uri));
}

const pj_str_t *ast_sip_pjsip_uri_get_username(pjsip_uri *uri)
{
	if (ast_sip_is_uri_sip_sips(uri)) {
		pjsip_sip_uri *sip_uri = pjsip_uri_get_uri(uri);
		if (!sip_uri) {
			return &AST_PJ_STR_EMPTY;
		}
		return &sip_uri->user;
	} else if (PJSIP_URI_SCHEME_IS_TEL(uri)) {
		pjsip_tel_uri *tel_uri = pjsip_uri_get_uri(uri);
		if (!tel_uri) {
			return &AST_PJ_STR_EMPTY;
		}
		return &tel_uri->number;
	}

	return &AST_PJ_STR_EMPTY;
}

const pj_str_t *ast_sip_pjsip_uri_get_hostname(pjsip_uri *uri)
{
	if (ast_sip_is_uri_sip_sips(uri)) {
		pjsip_sip_uri *sip_uri = pjsip_uri_get_uri(uri);
		if (!sip_uri) {
			return &AST_PJ_STR_EMPTY;
		}
		return &sip_uri->host;
	} else if (PJSIP_URI_SCHEME_IS_TEL(uri)) {
		return &AST_PJ_STR_EMPTY;
	}

	return &AST_PJ_STR_EMPTY;
}

struct pjsip_param *ast_sip_pjsip_uri_get_other_param(pjsip_uri *uri, const pj_str_t *param_str)
{
	if (ast_sip_is_uri_sip_sips(uri)) {
		pjsip_sip_uri *sip_uri = pjsip_uri_get_uri(uri);
		if (!sip_uri) {
			return NULL;
		}
		return pjsip_param_find(&sip_uri->other_param, param_str);
	} else if (PJSIP_URI_SCHEME_IS_TEL(uri)) {
		pjsip_tel_uri *tel_uri = pjsip_uri_get_uri(uri);
		if (!tel_uri) {
			return NULL;
		}
		return pjsip_param_find(&tel_uri->other_param, param_str);
	}

	return NULL;
}

/*! \brief Convert SIP hangup causes to Asterisk hangup causes */
const int ast_sip_hangup_sip2cause(int cause)
{
	/* Possible values taken from causes.h */

	switch(cause) {
	case 401:       /* Unauthorized */
		return AST_CAUSE_CALL_REJECTED;
	case 403:       /* Not found */
		return AST_CAUSE_CALL_REJECTED;
	case 404:       /* Not found */
		return AST_CAUSE_UNALLOCATED;
	case 405:       /* Method not allowed */
		return AST_CAUSE_INTERWORKING;
	case 407:       /* Proxy authentication required */
		return AST_CAUSE_CALL_REJECTED;
	case 408:       /* No reaction */
		return AST_CAUSE_NO_USER_RESPONSE;
	case 409:       /* Conflict */
		return AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
	case 410:       /* Gone */
		return AST_CAUSE_NUMBER_CHANGED;
	case 411:       /* Length required */
		return AST_CAUSE_INTERWORKING;
	case 413:       /* Request entity too large */
		return AST_CAUSE_INTERWORKING;
	case 414:       /* Request URI too large */
		return AST_CAUSE_INTERWORKING;
	case 415:       /* Unsupported media type */
		return AST_CAUSE_INTERWORKING;
	case 420:       /* Bad extension */
		return AST_CAUSE_NO_ROUTE_DESTINATION;
	case 480:       /* No answer */
		return AST_CAUSE_NO_ANSWER;
	case 481:       /* No answer */
		return AST_CAUSE_INTERWORKING;
	case 482:       /* Loop detected */
		return AST_CAUSE_INTERWORKING;
	case 483:       /* Too many hops */
		return AST_CAUSE_NO_ANSWER;
	case 484:       /* Address incomplete */
		return AST_CAUSE_INVALID_NUMBER_FORMAT;
	case 485:       /* Ambiguous */
		return AST_CAUSE_UNALLOCATED;
	case 486:       /* Busy everywhere */
		return AST_CAUSE_BUSY;
	case 487:       /* Request terminated */
		return AST_CAUSE_INTERWORKING;
	case 488:       /* No codecs approved */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	case 491:       /* Request pending */
		return AST_CAUSE_INTERWORKING;
	case 493:       /* Undecipherable */
		return AST_CAUSE_INTERWORKING;
	case 500:       /* Server internal failure */
		return AST_CAUSE_FAILURE;
	case 501:       /* Call rejected */
		return AST_CAUSE_FACILITY_REJECTED;
	case 502:
		return AST_CAUSE_DESTINATION_OUT_OF_ORDER;
	case 503:       /* Service unavailable */
		return AST_CAUSE_CONGESTION;
	case 504:       /* Gateway timeout */
		return AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE;
	case 505:       /* SIP version not supported */
		return AST_CAUSE_INTERWORKING;
	case 600:       /* Busy everywhere */
		return AST_CAUSE_USER_BUSY;
	case 603:       /* Decline */
		return AST_CAUSE_CALL_REJECTED;
	case 604:       /* Does not exist anywhere */
		return AST_CAUSE_UNALLOCATED;
	case 606:       /* Not acceptable */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	default:
		if (cause < 500 && cause >= 400) {
			/* 4xx class error that is unknown - someting wrong with our request */
			return AST_CAUSE_INTERWORKING;
		} else if (cause < 600 && cause >= 500) {
			/* 5xx class error - problem in the remote end */
			return AST_CAUSE_CONGESTION;
		} else if (cause < 700 && cause >= 600) {
			/* 6xx - global errors in the 4xx class */
			return AST_CAUSE_INTERWORKING;
		}
		return AST_CAUSE_NORMAL;
	}
	/* Never reached */
	return 0;
}

struct response_code_map {
	int code;
	const char *long_name;
	const char *short_name;
};

/*
 * This map was generated from sip_msg.h with
 *
 * sed -n -r -e 's/^\s+(PJSIP_SC_([^ =]+))\s*=\s*[0-9]+,/{ \1, "\1", "\2" },/gp' \
 *     third-party/pjproject/source/pjsip/include/pjsip/sip_msg.h
 *
 */
static const struct response_code_map rc_map[] = {
	{ PJSIP_SC_NULL, "PJSIP_SC_NULL", "NULL" },
	{ PJSIP_SC_TRYING, "PJSIP_SC_TRYING", "TRYING" },
	{ PJSIP_SC_RINGING, "PJSIP_SC_RINGING", "RINGING" },
	{ PJSIP_SC_CALL_BEING_FORWARDED, "PJSIP_SC_CALL_BEING_FORWARDED", "CALL_BEING_FORWARDED" },
	{ PJSIP_SC_QUEUED, "PJSIP_SC_QUEUED", "QUEUED" },
	{ PJSIP_SC_PROGRESS, "PJSIP_SC_PROGRESS", "PROGRESS" },
	{ PJSIP_SC_EARLY_DIALOG_TERMINATED, "PJSIP_SC_EARLY_DIALOG_TERMINATED", "EARLY_DIALOG_TERMINATED" },
	{ PJSIP_SC_OK, "PJSIP_SC_OK", "OK" },
	{ PJSIP_SC_ACCEPTED, "PJSIP_SC_ACCEPTED", "ACCEPTED" },
	{ PJSIP_SC_NO_NOTIFICATION, "PJSIP_SC_NO_NOTIFICATION", "NO_NOTIFICATION" },
	{ PJSIP_SC_MULTIPLE_CHOICES, "PJSIP_SC_MULTIPLE_CHOICES", "MULTIPLE_CHOICES" },
	{ PJSIP_SC_MOVED_PERMANENTLY, "PJSIP_SC_MOVED_PERMANENTLY", "MOVED_PERMANENTLY" },
	{ PJSIP_SC_MOVED_TEMPORARILY, "PJSIP_SC_MOVED_TEMPORARILY", "MOVED_TEMPORARILY" },
	{ PJSIP_SC_USE_PROXY, "PJSIP_SC_USE_PROXY", "USE_PROXY" },
	{ PJSIP_SC_ALTERNATIVE_SERVICE, "PJSIP_SC_ALTERNATIVE_SERVICE", "ALTERNATIVE_SERVICE" },
	{ PJSIP_SC_BAD_REQUEST, "PJSIP_SC_BAD_REQUEST", "BAD_REQUEST" },
	{ PJSIP_SC_UNAUTHORIZED, "PJSIP_SC_UNAUTHORIZED", "UNAUTHORIZED" },
	{ PJSIP_SC_PAYMENT_REQUIRED, "PJSIP_SC_PAYMENT_REQUIRED", "PAYMENT_REQUIRED" },
	{ PJSIP_SC_FORBIDDEN, "PJSIP_SC_FORBIDDEN", "FORBIDDEN" },
	{ PJSIP_SC_NOT_FOUND, "PJSIP_SC_NOT_FOUND", "NOT_FOUND" },
	{ PJSIP_SC_METHOD_NOT_ALLOWED, "PJSIP_SC_METHOD_NOT_ALLOWED", "METHOD_NOT_ALLOWED" },
	{ PJSIP_SC_NOT_ACCEPTABLE, "PJSIP_SC_NOT_ACCEPTABLE", "NOT_ACCEPTABLE" },
	{ PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED, "PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED", "PROXY_AUTHENTICATION_REQUIRED" },
	{ PJSIP_SC_REQUEST_TIMEOUT, "PJSIP_SC_REQUEST_TIMEOUT", "REQUEST_TIMEOUT" },
	{ PJSIP_SC_CONFLICT, "PJSIP_SC_CONFLICT", "CONFLICT" },
	{ PJSIP_SC_GONE, "PJSIP_SC_GONE", "GONE" },
	{ PJSIP_SC_LENGTH_REQUIRED, "PJSIP_SC_LENGTH_REQUIRED", "LENGTH_REQUIRED" },
	{ PJSIP_SC_CONDITIONAL_REQUEST_FAILED, "PJSIP_SC_CONDITIONAL_REQUEST_FAILED", "CONDITIONAL_REQUEST_FAILED" },
	{ PJSIP_SC_REQUEST_ENTITY_TOO_LARGE, "PJSIP_SC_REQUEST_ENTITY_TOO_LARGE", "REQUEST_ENTITY_TOO_LARGE" },
	{ PJSIP_SC_REQUEST_URI_TOO_LONG, "PJSIP_SC_REQUEST_URI_TOO_LONG", "REQUEST_URI_TOO_LONG" },
	{ PJSIP_SC_UNSUPPORTED_MEDIA_TYPE, "PJSIP_SC_UNSUPPORTED_MEDIA_TYPE", "UNSUPPORTED_MEDIA_TYPE" },
	{ PJSIP_SC_UNSUPPORTED_URI_SCHEME, "PJSIP_SC_UNSUPPORTED_URI_SCHEME", "UNSUPPORTED_URI_SCHEME" },
	{ PJSIP_SC_UNKNOWN_RESOURCE_PRIORITY, "PJSIP_SC_UNKNOWN_RESOURCE_PRIORITY", "UNKNOWN_RESOURCE_PRIORITY" },
	{ PJSIP_SC_BAD_EXTENSION, "PJSIP_SC_BAD_EXTENSION", "BAD_EXTENSION" },
	{ PJSIP_SC_EXTENSION_REQUIRED, "PJSIP_SC_EXTENSION_REQUIRED", "EXTENSION_REQUIRED" },
	{ PJSIP_SC_SESSION_TIMER_TOO_SMALL, "PJSIP_SC_SESSION_TIMER_TOO_SMALL", "SESSION_TIMER_TOO_SMALL" },
	{ PJSIP_SC_INTERVAL_TOO_BRIEF, "PJSIP_SC_INTERVAL_TOO_BRIEF", "INTERVAL_TOO_BRIEF" },
	{ PJSIP_SC_BAD_LOCATION_INFORMATION, "PJSIP_SC_BAD_LOCATION_INFORMATION", "BAD_LOCATION_INFORMATION" },
	{ PJSIP_SC_USE_IDENTITY_HEADER, "PJSIP_SC_USE_IDENTITY_HEADER", "USE_IDENTITY_HEADER" },
	{ PJSIP_SC_PROVIDE_REFERRER_HEADER, "PJSIP_SC_PROVIDE_REFERRER_HEADER", "PROVIDE_REFERRER_HEADER" },
	{ PJSIP_SC_FLOW_FAILED, "PJSIP_SC_FLOW_FAILED", "FLOW_FAILED" },
	{ PJSIP_SC_ANONIMITY_DISALLOWED, "PJSIP_SC_ANONIMITY_DISALLOWED", "ANONIMITY_DISALLOWED" },
	{ PJSIP_SC_BAD_IDENTITY_INFO, "PJSIP_SC_BAD_IDENTITY_INFO", "BAD_IDENTITY_INFO" },
	{ PJSIP_SC_UNSUPPORTED_CERTIFICATE, "PJSIP_SC_UNSUPPORTED_CERTIFICATE", "UNSUPPORTED_CERTIFICATE" },
	{ PJSIP_SC_INVALID_IDENTITY_HEADER, "PJSIP_SC_INVALID_IDENTITY_HEADER", "INVALID_IDENTITY_HEADER" },
	{ PJSIP_SC_FIRST_HOP_LACKS_OUTBOUND_SUPPORT, "PJSIP_SC_FIRST_HOP_LACKS_OUTBOUND_SUPPORT", "FIRST_HOP_LACKS_OUTBOUND_SUPPORT" },
	{ PJSIP_SC_MAX_BREADTH_EXCEEDED, "PJSIP_SC_MAX_BREADTH_EXCEEDED", "MAX_BREADTH_EXCEEDED" },
	{ PJSIP_SC_BAD_INFO_PACKAGE, "PJSIP_SC_BAD_INFO_PACKAGE", "BAD_INFO_PACKAGE" },
	{ PJSIP_SC_CONSENT_NEEDED, "PJSIP_SC_CONSENT_NEEDED", "CONSENT_NEEDED" },
	{ PJSIP_SC_TEMPORARILY_UNAVAILABLE, "PJSIP_SC_TEMPORARILY_UNAVAILABLE", "TEMPORARILY_UNAVAILABLE" },
	{ PJSIP_SC_CALL_TSX_DOES_NOT_EXIST, "PJSIP_SC_CALL_TSX_DOES_NOT_EXIST", "CALL_TSX_DOES_NOT_EXIST" },
	{ PJSIP_SC_LOOP_DETECTED, "PJSIP_SC_LOOP_DETECTED", "LOOP_DETECTED" },
	{ PJSIP_SC_TOO_MANY_HOPS, "PJSIP_SC_TOO_MANY_HOPS", "TOO_MANY_HOPS" },
	{ PJSIP_SC_ADDRESS_INCOMPLETE, "PJSIP_SC_ADDRESS_INCOMPLETE", "ADDRESS_INCOMPLETE" },
	{ PJSIP_SC_BUSY_HERE, "PJSIP_SC_BUSY_HERE", "BUSY_HERE" },
	{ PJSIP_SC_REQUEST_TERMINATED, "PJSIP_SC_REQUEST_TERMINATED", "REQUEST_TERMINATED" },
	{ PJSIP_SC_NOT_ACCEPTABLE_HERE, "PJSIP_SC_NOT_ACCEPTABLE_HERE", "NOT_ACCEPTABLE_HERE" },
	{ PJSIP_SC_BAD_EVENT, "PJSIP_SC_BAD_EVENT", "BAD_EVENT" },
	{ PJSIP_SC_REQUEST_UPDATED, "PJSIP_SC_REQUEST_UPDATED", "REQUEST_UPDATED" },
	{ PJSIP_SC_REQUEST_PENDING, "PJSIP_SC_REQUEST_PENDING", "REQUEST_PENDING" },
	{ PJSIP_SC_UNDECIPHERABLE, "PJSIP_SC_UNDECIPHERABLE", "UNDECIPHERABLE" },
	{ PJSIP_SC_SECURITY_AGREEMENT_NEEDED, "PJSIP_SC_SECURITY_AGREEMENT_NEEDED", "SECURITY_AGREEMENT_NEEDED" },
	{ PJSIP_SC_INTERNAL_SERVER_ERROR, "PJSIP_SC_INTERNAL_SERVER_ERROR", "INTERNAL_SERVER_ERROR" },
	{ PJSIP_SC_NOT_IMPLEMENTED, "PJSIP_SC_NOT_IMPLEMENTED", "NOT_IMPLEMENTED" },
	{ PJSIP_SC_BAD_GATEWAY, "PJSIP_SC_BAD_GATEWAY", "BAD_GATEWAY" },
	{ PJSIP_SC_SERVICE_UNAVAILABLE, "PJSIP_SC_SERVICE_UNAVAILABLE", "SERVICE_UNAVAILABLE" },
	{ PJSIP_SC_SERVER_TIMEOUT, "PJSIP_SC_SERVER_TIMEOUT", "SERVER_TIMEOUT" },
	{ PJSIP_SC_VERSION_NOT_SUPPORTED, "PJSIP_SC_VERSION_NOT_SUPPORTED", "VERSION_NOT_SUPPORTED" },
	{ PJSIP_SC_MESSAGE_TOO_LARGE, "PJSIP_SC_MESSAGE_TOO_LARGE", "MESSAGE_TOO_LARGE" },
	{ PJSIP_SC_PUSH_NOTIFICATION_SERVICE_NOT_SUPPORTED, "PJSIP_SC_PUSH_NOTIFICATION_SERVICE_NOT_SUPPORTED", "PUSH_NOTIFICATION_SERVICE_NOT_SUPPORTED" },
	{ PJSIP_SC_PRECONDITION_FAILURE, "PJSIP_SC_PRECONDITION_FAILURE", "PRECONDITION_FAILURE" },
	{ PJSIP_SC_BUSY_EVERYWHERE, "PJSIP_SC_BUSY_EVERYWHERE", "BUSY_EVERYWHERE" },
	{ PJSIP_SC_DECLINE, "PJSIP_SC_DECLINE", "DECLINE" },
	{ PJSIP_SC_DOES_NOT_EXIST_ANYWHERE, "PJSIP_SC_DOES_NOT_EXIST_ANYWHERE", "DOES_NOT_EXIST_ANYWHERE" },
	{ PJSIP_SC_NOT_ACCEPTABLE_ANYWHERE, "PJSIP_SC_NOT_ACCEPTABLE_ANYWHERE", "NOT_ACCEPTABLE_ANYWHERE" },
	{ PJSIP_SC_UNWANTED, "PJSIP_SC_UNWANTED", "UNWANTED" },
	{ PJSIP_SC_REJECTED, "PJSIP_SC_REJECTED", "REJECTED" },
};

int ast_sip_str2rc(const char *name)
{
	int i;

	if (ast_strlen_zero(name)) {
		return -1;
	}

	for (i = 0; i < ARRAY_LEN(rc_map); i++) {
		if (strcasecmp(rc_map[i].short_name, name) == 0 ||
			strcasecmp(rc_map[i].long_name, name) == 0) {
			return rc_map[i].code;
		}
	}

	return -1;
}


#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(xml_sanitization_end_null)
{
	char sanitized[8];

	switch (cmd) {
	case TEST_INIT:
		info->name = "xml_sanitization_end_null";
		info->category = "/res/res_pjsip/";
		info->summary = "Ensure XML sanitization works as expected with a long string";
		info->description = "This test sanitizes a string which exceeds the output\n"
			"buffer size. Once done the string is confirmed to be NULL terminated.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_sip_sanitize_xml("aaaaaaaaaaaa", sanitized, sizeof(sanitized));
	if (sanitized[7] != '\0') {
		ast_test_status_update(test, "Sanitized XML string is not null-terminated when it should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(xml_sanitization_exceeds_buffer)
{
	char sanitized[8];

	switch (cmd) {
	case TEST_INIT:
		info->name = "xml_sanitization_exceeds_buffer";
		info->category = "/res/res_pjsip/";
		info->summary = "Ensure XML sanitization does not exceed buffer when output won't fit";
		info->description = "This test sanitizes a string which before sanitization would\n"
			"fit within the output buffer. After sanitization, however, the string would\n"
			"exceed the buffer. Once done the string is confirmed to be NULL terminated.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_sip_sanitize_xml("<><><>&", sanitized, sizeof(sanitized));
	if (sanitized[7] != '\0') {
		ast_test_status_update(test, "Sanitized XML string is not null-terminated when it should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}
#endif

/*!
 * \internal
 * \brief Reload configuration within a PJSIP thread
 */
static int reload_configuration_task(void *obj)
{
	ast_res_pjsip_reload_configuration();
	ast_res_pjsip_init_options_handling(1);
	ast_sip_initialize_dns();
	return 0;
}

static int unload_pjsip(void *data)
{
	/*
	 * These calls need the pjsip endpoint and serializer to clean up.
	 * If they're not set, then there's nothing to clean up anyway.
	 */
	if (ast_pjsip_endpoint && sip_serializer_pool) {
		ast_res_pjsip_cleanup_options_handling();
		ast_res_pjsip_cleanup_message_filter();
		ast_sip_destroy_distributor();
		ast_sip_destroy_transport_management();
		ast_res_pjsip_destroy_configuration();
		ast_sip_destroy_system();
		ast_sip_destroy_global_headers();
		ast_sip_unregister_service(&supplement_module);
		ast_sip_destroy_transport_events();
	}

	if (monitor_thread) {
		stop_monitor_thread();
		monitor_thread = NULL;
	}

	if (memory_pool) {
		/* This mimics the behavior of pj_pool_safe_release
		 * which was introduced in pjproject 2.6.
		 */
		pj_pool_t *temp_pool = memory_pool;

		memory_pool = NULL;
		pj_pool_release(temp_pool);
	}

	ast_pjsip_endpoint = NULL;

	if (caching_pool.lock) {
		ast_pjproject_caching_pool_destroy(&caching_pool);
	}

	pj_shutdown();

	return 0;
}

static int load_pjsip(void)
{
	const unsigned int flags = 0; /* no port, no brackets */
	pj_status_t status;

	/* The third parameter is just copied from
	 * example code from PJLIB. This can be adjusted
	 * if necessary.
	 */
	ast_pjproject_caching_pool_init(&caching_pool, NULL, 1024 * 1024);
	if (pjsip_endpt_create(&caching_pool.factory, "SIP", &ast_pjsip_endpoint) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to create PJSIP endpoint structure. Aborting load\n");
		goto error;
	}

	/* PJSIP will automatically try to add a Max-Forwards header. Since we want to control that,
	 * we need to stop PJSIP from doing it automatically
	 */
	remove_request_headers(ast_pjsip_endpoint);

	memory_pool = pj_pool_create(&caching_pool.factory, "SIP", 1024, 1024, NULL);
	if (!memory_pool) {
		ast_log(LOG_ERROR, "Failed to create memory pool for SIP. Aborting load\n");
		goto error;
	}

	if (!pj_gethostip(pj_AF_INET(), &host_ip_ipv4)) {
		pj_sockaddr_print(&host_ip_ipv4, host_ip_ipv4_string, sizeof(host_ip_ipv4_string), flags);
		ast_verb(3, "Local IPv4 address determined to be: %s\n", host_ip_ipv4_string);
	}

	if (!pj_gethostip(pj_AF_INET6(), &host_ip_ipv6)) {
		pj_sockaddr_print(&host_ip_ipv6, host_ip_ipv6_string, sizeof(host_ip_ipv6_string), flags);
		ast_verb(3, "Local IPv6 address determined to be: %s\n", host_ip_ipv6_string);
	}

	pjsip_tsx_layer_init_module(ast_pjsip_endpoint);
	pjsip_ua_init_module(ast_pjsip_endpoint, NULL);

	monitor_continue = 1;
	status = pj_thread_create(memory_pool, "SIP", (pj_thread_proc *) &monitor_thread_exec,
			NULL, PJ_THREAD_DEFAULT_STACK_SIZE * 2, 0, &monitor_thread);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to start SIP monitor thread. Aborting load\n");
		goto error;
	}

	return AST_MODULE_LOAD_SUCCESS;

error:
	return AST_MODULE_LOAD_DECLINE;
}

/*
 * This is a place holder function to ensure that pjmedia_strerr() is at
 * least directly referenced by this module to ensure that the loader
 * linker will link to the function.  If a module only indirectly
 * references a function from another module, such as a callback parameter
 * to a function, the loader linker has been known to miss the link.
 */
void never_called_res_pjsip(void);
void never_called_res_pjsip(void)
{
	pjmedia_strerror(0, NULL, 0);
}

/* Definitions of media types declared "extern" in res_pjsip.h */
pjsip_media_type pjsip_media_type_application_json;
pjsip_media_type pjsip_media_type_application_media_control_xml;
pjsip_media_type pjsip_media_type_application_pidf_xml;
pjsip_media_type pjsip_media_type_application_xpidf_xml;
pjsip_media_type pjsip_media_type_application_cpim_xpidf_xml;
pjsip_media_type pjsip_media_type_application_rlmi_xml;
pjsip_media_type pjsip_media_type_application_simple_message_summary;
pjsip_media_type pjsip_media_type_application_sdp;
pjsip_media_type pjsip_media_type_multipart_alternative;
pjsip_media_type pjsip_media_type_multipart_mixed;
pjsip_media_type pjsip_media_type_multipart_related;
pjsip_media_type pjsip_media_type_text_plain;

static int load_module(void)
{
	struct ast_threadpool_options options;

	/* pjproject and config_system need to be initialized before all else */
	if (pj_init() != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjlib_util_init() != PJ_SUCCESS) {
		goto error;
	}

	/* Register PJMEDIA error codes for SDP parsing errors */
	if (pj_register_strerror(PJMEDIA_ERRNO_START, PJ_ERRNO_SPACE_SIZE, pjmedia_strerror)
		!= PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to register pjmedia error codes.  Codes will not be decoded.\n");
	}

	/* Initialize common media types */
	pjsip_media_type_init2(&pjsip_media_type_application_json, "application", "json");
	pjsip_media_type_init2(&pjsip_media_type_application_media_control_xml, "application", "media_control+xml");
	pjsip_media_type_init2(&pjsip_media_type_application_pidf_xml, "application", "pidf+xml");
	pjsip_media_type_init2(&pjsip_media_type_application_xpidf_xml, "application", "xpidf+xml");
	pjsip_media_type_init2(&pjsip_media_type_application_cpim_xpidf_xml, "application", "cpim-xpidf+xml");
	pjsip_media_type_init2(&pjsip_media_type_application_rlmi_xml, "application", "rlmi+xml");
	pjsip_media_type_init2(&pjsip_media_type_application_sdp, "application", "sdp");
	pjsip_media_type_init2(&pjsip_media_type_application_simple_message_summary, "application",	"simple-message-summary");
	pjsip_media_type_init2(&pjsip_media_type_multipart_alternative, "multipart", "alternative");
	pjsip_media_type_init2(&pjsip_media_type_multipart_mixed, "multipart", "mixed");
	pjsip_media_type_init2(&pjsip_media_type_multipart_related, "multipart", "related");
	pjsip_media_type_init2(&pjsip_media_type_text_plain, "text", "plain");


	if (ast_sip_initialize_system()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP 'system' configuration section. Aborting load\n");
		goto error;
	}

	/* The serializer needs threadpool and threadpool needs pjproject to be initialized so it's next */
	sip_get_threadpool_options(&options);
	options.thread_start = sip_thread_start;
	sip_threadpool = ast_threadpool_create("pjsip", NULL, &options);
	if (!sip_threadpool) {
		goto error;
	}

	sip_serializer_pool = ast_serializer_pool_create(
		"pjsip/default", SERIALIZER_POOL_SIZE, sip_threadpool, -1);
	if (!sip_serializer_pool) {
		ast_log(LOG_ERROR, "Failed to create SIP serializer pool. Aborting load\n");
		goto error;
	}

	if (ast_sip_initialize_scheduler()) {
		ast_log(LOG_ERROR, "Failed to start scheduler. Aborting load\n");
		goto error;
	}

	/* Now load all the pjproject infrastructure. */
	if (load_pjsip()) {
		goto error;
	}

	if (ast_sip_initialize_transport_events()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP transport monitor. Aborting load\n");
		goto error;
	}

	ast_sip_initialize_dns();
	ast_sip_initialize_global_headers();

	if (ast_res_pjsip_preinit_options_handling()) {
		ast_log(LOG_ERROR, "Failed to pre-initialize OPTIONS handling. Aborting load\n");
		goto error;
	}

	if (ast_res_pjsip_initialize_configuration()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP configuration. Aborting load\n");
		goto error;
	}

	ast_sip_initialize_resolver();
	ast_sip_initialize_dns();

	if (ast_sip_initialize_transport_management()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP transport management. Aborting load\n");
		goto error;
	}

	if (ast_sip_initialize_distributor()) {
		ast_log(LOG_ERROR, "Failed to register distributor module. Aborting load\n");
		goto error;
	}

	if (ast_sip_register_service(&supplement_module)) {
		ast_log(LOG_ERROR, "Failed to initialize supplement hooks. Aborting load\n");
		goto error;
	}

	if (ast_res_pjsip_init_options_handling(0)) {
		ast_log(LOG_ERROR, "Failed to initialize OPTIONS handling. Aborting load\n");
		goto error;
	}

	/*
	 * It is OK to prune the contacts now that
	 * ast_res_pjsip_init_options_handling() has added the contact observer
	 * of res/res_pjsip/pjsip_options.c to sorcery (to ensure that any
	 * pruned contacts are removed from this module's data structure).
	 */
	ast_sip_location_prune_boot_contacts();

	if (ast_res_pjsip_init_message_filter()) {
		ast_log(LOG_ERROR, "Failed to initialize message IP updating. Aborting load\n");
		goto error;
	}

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	AST_TEST_REGISTER(xml_sanitization_end_null);
	AST_TEST_REGISTER(xml_sanitization_exceeds_buffer);

	return AST_MODULE_LOAD_SUCCESS;

error:
	unload_pjsip(NULL);

	/* These functions all check for NULLs and are safe to call at any time */
	ast_sip_destroy_scheduler();
	ast_serializer_pool_destroy(sip_serializer_pool);
	ast_threadpool_shutdown(sip_threadpool);

	return AST_MODULE_LOAD_DECLINE;
}

static int reload_module(void)
{
	/*
	 * We must wait for the reload to complete so multiple
	 * reloads cannot happen at the same time.
	 */
	if (ast_sip_push_task_wait_servant(NULL, reload_configuration_task, NULL)) {
		ast_log(LOG_WARNING, "Failed to reload PJSIP\n");
		return -1;
	}

	return 0;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(xml_sanitization_end_null);
	AST_TEST_UNREGISTER(xml_sanitization_exceeds_buffer);
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));

	/* The thread this is called from cannot call PJSIP/PJLIB functions,
	 * so we have to push the work to the threadpool to handle
	 */
	ast_sip_push_task_wait_servant(NULL, unload_pjsip, NULL);
	ast_sip_destroy_scheduler();
	ast_serializer_pool_destroy(sip_serializer_pool);
	ast_threadpool_shutdown(sip_threadpool);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Basic SIP resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
	.requires = "dnsmgr,res_pjproject,res_sorcery_config,res_sorcery_memory,res_sorcery_astdb",
	.optional_modules = "res_geolocation,res_statsd",
);
