/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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

/*! \file
 *
 * \brief PJSIP Configuration Wizard
 *
 * \author George Joseph <george.joseph@fairview5.com>
  */

/*! \li \ref res_pjsip_config_wizard.c uses the configuration file \ref pjsip_wizard.conf
 */

/*!
 * \page pjsip_wizard.conf pjsip_wizard.conf
 * \verbinclude pjsip_wizard.conf.sample
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <regex.h>
#include <pjsip.h>

#include "asterisk/astobj2.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sorcery.h"
#include "asterisk/vector.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_config_wizard" language="en_US">
		<synopsis>Module that privides simple configuration wizard capabilities.</synopsis>
		<description><para>
			<emphasis>PJSIP Configuration Wizard</emphasis>
			</para>
			<para>This module allows creation of common PJSIP configuration scenarios
			without having to specify individual endpoint, aor, auth, identify and registration objects.
			</para>
			<para> </para>

			<para>For example, the following configuration snippet would create the
			endpoint, aor, contact, auth and phoneprov objects necessary for a phone to
			get phone provisioning information, register, and make and receive calls.
			A hint is also created in the default context for extension 1000.</para>
			<para> </para>

			<para>[myphone]</para>
			<para>type = wizard</para>
			<para>sends_auth = no</para>
			<para>accepts_auth = yes</para>
			<para>sends_registrations = no</para>
			<para>accepts_registrations = yes</para>
			<para>has_phoneprov = yes</para>
			<para>transport = ipv4</para>
			<para>has_hint = yes</para>
			<para>hint_exten = 1000</para>
			<para>inbound_auth/username = testname</para>
			<para>inbound_auth/password = test password</para>
			<para>endpoint/allow = ulaw</para>
			<para>endpoint/context = default</para>
			<para>phoneprov/MAC = 001122aa4455</para>
			<para>phoneprov/PROFILE = profile1</para>
			<para> </para>

			<para>The first 8 items are specific to the wizard.  The rest of the items
			are passed verbatim to the underlying objects.</para>
			<para> </para>

			<para>The following configuration snippet would create the
			endpoint, aor, contact, auth, identify and registration objects necessary for a trunk
			to another pbx or ITSP that requires registration.</para>
			<para> </para>

			<para>[mytrunk]</para>
			<para>type = wizard</para>
			<para>sends_auth = yes</para>
			<para>accepts_auth = no</para>
			<para>sends_registrations = yes</para>
			<para>accepts_registrations = no</para>
			<para>transport = ipv4</para>
			<para>remote_hosts = sip1.myitsp.com:5060,sip2.myitsp.com:5060</para>
			<para>outbound_auth/username = testname</para>
			<para>outbound_auth/password = test password</para>
			<para>endpoint/allow = ulaw</para>
			<para>endpoint/context = default</para>
			<para> </para>

			<para>Of course, any of the items in either example could be placed into
			templates and shared among wizard objects.</para>

			<para> </para>
			<para>For more information, visit:</para>
			<para><literal>https://wiki.asterisk.org/wiki/display/AST/PJSIP+Configuration+Wizard</literal></para>
		</description>

		<configFile name="pjsip_wizard.conf">
			<configObject name="wizard">
				<synopsis>Provides config wizard.</synopsis>
				<description>
				<para>For more information, visit:</para>
				<para><literal>https://wiki.asterisk.org/wiki/display/AST/PJSIP+Configuration+Wizard</literal></para>
				</description>
				<configOption name="type">
					<synopsis>Must be 'wizard'.</synopsis>
				</configOption>
				<configOption name="transport">
					<synopsis>The name of a transport to use for this object.</synopsis>
					<description><para>If not specified,
					the default will be used.</para></description>
				</configOption>
				<configOption name="remote_hosts">
					<synopsis>List of remote hosts.</synopsis>
					<description><para>A comma-separated list of remote hosts in the form of
					<replaceable>host</replaceable>[:<replaceable>port</replaceable>].
					If set, an aor static contact and an identify match will be created for each
					entry in the list.  If send_registrations is also set, a registration will
					also be created for each.</para></description>
				</configOption>
				<configOption name="sends_auth" default="no">
					<synopsis>Send outbound authentication to remote hosts.</synopsis>
					<description><para>At least outbound_auth/username is required.</para></description>
				</configOption>
				<configOption name="accepts_auth" default="no">
					<synopsis>Accept incoming authentication from remote hosts.</synopsis>
					<description><para>At least inbound_auth/username is required.</para></description>
				</configOption>
				<configOption name="sends_registrations" default="no">
					<synopsis>Send outbound registrations to remote hosts.</synopsis>
					<description><para>remote_hosts is required and a registration object will
					be created for each host in the remote _hosts string.  If authentication is required,
					sends_auth and an outbound_auth/username must also be supplied.</para></description>
				</configOption>
				<configOption name="accepts_registrations" default="no">
					<synopsis>Accept inbound registration from remote hosts.</synopsis>
					<description><para>An AOR with dynamic contacts will be created.  If
					the number of contacts nneds to be limited, set aor/max_contacts.</para></description>
				</configOption>
				<configOption name="has_phoneprov" default="no">
					<synopsis>Create a phoneprov object for this endpoint.</synopsis>
					<description><para>A phoneprov object will be created.  phoneprov/MAC
					must be specified.</para></description>
				</configOption>
				<configOption name="server_uri_pattern" default="sip:${REMOTE_HOST}">
					<synopsis>A pattern to use for constructing outbound registration server_uris.</synopsis>
					<description><para>
					The literal <literal>${REMOTE_HOST}</literal> will be substituted with the
					appropriate remote_host for each registration.</para></description>
				</configOption>
				<configOption name="client_uri_pattern" default="sip:${USERNAME}@${REMOTE_HOST}">
					<synopsis>A pattern to use for constructing outbound registration client_uris.</synopsis>
					<description><para>
					The literals <literal>${REMOTE_HOST}</literal> and <literal>${USERNAME}</literal>
					will be substituted with the appropriate remote_host and outbound_auth/username.</para></description>
				</configOption>
				<configOption name="contact_pattern" default="sip:${REMOTE_HOST}">
					<synopsis>A pattern to use for constructing outbound contact uris.</synopsis>
					<description><para>
					The literal <literal>${REMOTE_HOST}</literal> will be substituted with the
					appropriate remote_host for each contact.</para></description>
				</configOption>
				<configOption name="has_hint" default="no">
					<synopsis>Create hint and optionally a default application.</synopsis>
					<description><para>Create hint and optionally a default application.</para></description>
				</configOption>
				<configOption name="hint_context" default="endpoint/context or 'default'">
					<synopsis>The context in which to place hints.</synopsis>
					<description>
					<para>Ignored if <literal>hint_exten</literal> is not specified otherwise specifies the
					context into which the dialplan hints will be placed.  If not specified,
					defaults to the endpoint's context or <literal>default</literal> if that isn't
					found.
					</para></description>
				</configOption>
				<configOption name="hint_exten">
					<synopsis>Extension to map a PJSIP hint to.</synopsis>
					<description>
					<para>Will create the following entry in <literal>hint_context</literal>:</para>
					<para>   <literal>exten =&gt; &lt;hint_exten&gt;,hint,PJSIP/&lt;wizard_id&gt;</literal></para>
					<para> </para>
					<para>Normal dialplan precedence rules apply so if there's already a hint for
					this extension in <literal>hint_context</literal>, this one will be ignored.
					For more information, visit: </para>
					<para><literal>https://wiki.asterisk.org/wiki/display/AST/PJSIP+Configuration+Wizard</literal></para>
					</description>
				</configOption>
				<configOption name="hint_application">
					<synopsis>Application to call when 'hint_exten' is dialed.</synopsis>
					<description>
					<para>Ignored if <literal>hint_exten</literal> isn't specified otherwise
					will create the following priority 1 extension in <literal>hint_context</literal>:</para>
					<para>   <literal>exten =&gt; &lt;hint_exten&gt;,1,&lt;hint_application&gt;</literal></para>
					<para> </para>
					<para>You can specify any valid extensions.conf application expression.</para>
					<para>Examples: </para>
					<para>   <literal>Dial(${HINT})</literal></para>
					<para>   <literal>Gosub(stdexten,${EXTEN},1(${HINT}))</literal></para>
					<para> </para>
					<para>Any extensions.conf style variables specified are passed directly to the
					dialplan.</para>
					<para> </para>
					<para>Normal dialplan precedence rules apply so if there's already a priority 1
					application for this specific extension in <literal>hint_context</literal>,
					this one will be ignored. For more information, visit: </para>
					<para><literal>https://wiki.asterisk.org/wiki/display/AST/PJSIP+Configuration+Wizard</literal></para>
					</description>
				</configOption>
				<configOption name="endpoint&#47;*">
					<synopsis>Variables to be passed directly to the endpoint.</synopsis>
				</configOption>
				<configOption name="aor&#47;*">
					<synopsis>Variables to be passed directly to the aor.</synopsis>
					<description><para>If an aor/contact is explicitly defined then remote_hosts
					will not be used to create contacts automatically.</para></description>
				</configOption>
				<configOption name="inbound_auth&#47;*">
					<synopsis>Variables to be passed directly to the inbound auth.</synopsis>
				</configOption>
				<configOption name="outbound_auth&#47;*">
					<synopsis>Variables to be passed directly to the outbound auth.</synopsis>
				</configOption>
				<configOption name="identify&#47;*">
					<synopsis>Variables to be passed directly to the identify.</synopsis>
					<description><para>If an identify/match is explicitly defined then remote_hosts
					will not be used to create matches automatically.</para></description>
				</configOption>
				<configOption name="registration&#47;*">
					<synopsis>Variables to be passed directly to the outbound registrations.</synopsis>
				</configOption>
				<configOption name="phoneprov&#47;*">
					<synopsis>Variables to be passed directly to the phoneprov object.</synopsis>
					<description><para>
					To activate phoneprov, at least phoneprov/MAC must be set.</para></description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

 /*! \brief Defines the maximum number of characters that can be added to a wizard id. */
#define MAX_ID_SUFFIX 20

#define BASE_REGISTRAR "res_pjsip_config_wizard"

/*! \brief A generic char * vector definition. */
AST_VECTOR(string_vector, char *);

/*! \brief Keeps track of the sorcery wizard and last config for each object type */
struct object_type_wizard {
	struct ast_sorcery *sorcery;
	struct ast_sorcery_wizard *wizard;
	void *wizard_data;
	struct ast_config *last_config;
	char object_type[];
};
static AST_VECTOR(object_type_wizards, struct object_type_wizard *) object_type_wizards;

/*! \brief Callbacks for vector deletes */
#define NOT_EQUALS(a, b) (a != b)
#define OTW_DELETE_CB(otw) ({ \
	ast_config_destroy(otw->last_config); \
	ast_free(otw); \
})

const static char *object_types[] = {"phoneprov", "registration", "identify", "endpoint", "aor", "auth", NULL};

static int is_one_of(const char *needle, const char *haystack[])
{
	int i;
	for (i = 0; haystack[i]; i++) {
		if (!strcmp(needle, haystack[i])) {
			return 1;
		}
	}

	return 0;
}

/*! \brief Finds the otw for the object type */
static struct object_type_wizard *find_wizard(const char *object_type)
{
	int idx;

	for(idx = 0; idx < AST_VECTOR_SIZE(&object_type_wizards); idx++) {
		struct object_type_wizard *otw = AST_VECTOR_GET(&object_type_wizards, idx);
		if (!strcmp(otw->object_type, object_type)) {
			return otw;
		}
	}

	return NULL;
}

/*! \brief Creates a sorcery object and applies a variable list */
static void *create_object(const struct ast_sorcery *sorcery,
	const char *id, const char *type, struct ast_variable *vars)
{
	struct ast_sorcery_object *obj = ast_sorcery_alloc(sorcery, type, id);

	if (!obj) {
		ast_log(LOG_ERROR, "Unable to allocate an object of type '%s' with id '%s'.\n", type, id);
		return NULL;
	}

	if (ast_sorcery_objectset_apply(sorcery, obj, vars)) {
		ast_log(LOG_ERROR, "Unable to apply object type '%s' with id '%s'.  Check preceeding errors.\n", type, id);
		ao2_ref(obj, -1);
		return NULL;
	}

	return obj;
}

/*! \brief Finds a variable in a list and tests it */
static int is_variable_true(struct ast_variable *vars, const char *name)
{
	return ast_true(ast_variable_find_in_list(vars, name));
}

/*! \brief Appends a variable to the end of an existing list */
static int variable_list_append(struct ast_variable **existing, const char *name, const char *value)
{
	struct ast_variable *new = ast_variable_new(name, value, "");

	if (!new) {
		ast_log(LOG_ERROR, "Unable to allocate memory for new variable '%s'.\n", name);
		return -1;
	}

	ast_variable_list_append(existing, new);

	return 0;
}

/*! \brief Appends a variable to the end of an existing list.  On failure, cause the calling
 * function to return -1 */
#define variable_list_append_return(existing, name, value) ({ \
	struct ast_variable *new = ast_variable_new(name, value, ""); \
	if (!new) { \
		ast_log(LOG_ERROR, "Unable to allocate memory for new variable '%s'.\n", name); \
		return -1; \
	} \
	ast_variable_list_append(existing, new); \
})

/*! \brief We need to strip off the prefix from the name of each variable
 * so they're suitable for objectset_apply.
 * I.E.  will transform outbound_auth/username to username.
 */
static struct ast_variable *get_object_variables(struct ast_variable *vars, char *prefix)
{
	struct ast_variable *return_vars = NULL;
	struct ast_variable *v = vars;
	int plen = strlen(prefix);

	for(; v; v = v->next) {
		if (ast_begins_with(v->name, prefix) && strlen(v->name) > plen) {
			if (variable_list_append(&return_vars, v->name + plen, v->value)) {
				ast_variables_destroy(return_vars);
				return NULL;
			}
		}
	}

	return return_vars;
}

/* Don't call while holding context locks. */
static int delete_extens(const char *context, const char *exten)
{
	struct pbx_find_info find_info = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */

	if (pbx_find_extension(NULL, NULL, &find_info, context, exten, PRIORITY_HINT, NULL, NULL, E_MATCH)) {
		ast_context_remove_extension(context, exten, PRIORITY_HINT, BASE_REGISTRAR);
	}

	if (pbx_find_extension(NULL, NULL, &find_info, context, exten, 1, NULL, NULL, E_MATCH)) {
		ast_context_remove_extension(context, exten, 1, BASE_REGISTRAR);
	}

	return 0;
}

static int add_extension(struct ast_context *context, const char *exten,
	int priority, const char *application)
{
	struct pbx_find_info find_info = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	struct ast_exten *existing_exten;
	char *data = NULL;
	char *app = NULL;
	void *free_ptr = NULL;
	char *paren;
	const char *context_name;

	if (!context || ast_strlen_zero(exten) || ast_strlen_zero(application)) {
		return -1;
	}

	/* The incoming application has to be split into the app name and the
	 * arguments (data).  The app name can be any storage type as add_extension
	 * copies it into its own buffer.  Data however, needs to be dynamically
	 * allocated and a free function provided.
	 */

	paren = strchr(application, '(');
	if (!paren) {
		app = (char *)application;
	} else {
		app = ast_strdupa(application);
		app[paren - application] = '\0';
		data = ast_strdup(paren + 1);
		if (!data) {
			return -1;
		}
		data[strlen(data) - 1] = '\0';
		free_ptr = ast_free_ptr;
		if (ast_strlen_zero(app) || ast_strlen_zero(data)) {
			ast_free(data);
			return -1;
		}
	}

	/* Don't disturb existing, exact-match, entries. */
	context_name = ast_get_context_name(context);
	if ((existing_exten = pbx_find_extension(NULL, NULL, &find_info, context_name, exten,
		priority, NULL, NULL, E_MATCH))) {
		const char *existing_app = ast_get_extension_app(existing_exten);
		const char *existing_data = ast_get_extension_app_data(existing_exten);
		if (!strcmp(existing_app, app)
			&& !strcmp(existing_data ? existing_data : "", data ? data : "")) {
			ast_free(data);
			return 0;
		}

		ast_context_remove_extension2(context, exten, priority, BASE_REGISTRAR, 1);
	}

	if (ast_add_extension2_nolock(context, 0, exten, priority, NULL, NULL,
			app, data, free_ptr, BASE_REGISTRAR)) {
		ast_free(data);
		return -1;
	}

	return 0;
}

static int add_hints(const char *context, const char *exten, const char *application, const char *id)
{
	struct ast_context *hint_context;
	char *hint_device;

	hint_device = ast_alloca(strlen("PJSIP/") + strlen(id) + 1);
	sprintf(hint_device, "PJSIP/%s", id);

	/* We need the contexts list locked to safely be able to both read and lock the specific context within */
	if (ast_wrlock_contexts()) {
		ast_log(LOG_ERROR, "Failed to lock the contexts list.\n");
		return -1;
	}

	if (!(hint_context = ast_context_find_or_create(NULL, NULL, context, BASE_REGISTRAR))) {
		ast_log(LOG_ERROR, "Unable to find or create hint context '%s'\n", context);
		if (ast_unlock_contexts()) {
			ast_assert(0);
		}
		return -1;
	}

	/* Transfer the all-contexts lock to the specific context */
	if (ast_wrlock_context(hint_context)) {
		ast_unlock_contexts();
		ast_log(LOG_ERROR, "failed to obtain write lock on context\n");
		return -1;
	}
	ast_unlock_contexts();

	if (add_extension(hint_context, exten, PRIORITY_HINT, hint_device)) {
		ast_log(LOG_ERROR, "Failed to add hint '%s@%s' to the PBX.\n",
		        exten, context);
	}

	if (!ast_strlen_zero(application)) {
		if (add_extension(hint_context, exten, 1, application)) {
			ast_log(LOG_ERROR, "Failed to add hint '%s@%s' to the PBX.\n",
			        exten, context);
		}
	} else {
		ast_context_remove_extension2(hint_context, exten, 1, BASE_REGISTRAR, 1);
	}

	ast_unlock_context(hint_context);

	return 0;
}

static int handle_auth(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz, char *direction)
{
	struct ast_variable *wizvars = ast_category_first(wiz);
	struct ast_sorcery_object *obj = NULL;
	const char *id = ast_category_get_name(wiz);
	char new_id[strlen(id) + MAX_ID_SUFFIX];
	char prefix[strlen(direction) + strlen("_auth/") + 1];
	char *test_variable = NULL;
	RAII_VAR(struct ast_variable *, vars, NULL, ast_variables_destroy);

	snprintf(prefix, sizeof(prefix), "%s_auth/", direction);
	vars = get_object_variables(wizvars, prefix);

	if (!strcmp(direction, "outbound")) {
		snprintf(new_id, sizeof(new_id), "%s-oauth", id);
		test_variable = "sends_auth";
	} else {
		snprintf(new_id, sizeof(new_id), "%s-iauth", id);
		test_variable = "accepts_auth";
	}

	if (is_variable_true(wizvars, test_variable)) {
		if (!ast_variable_find_in_list(vars, "username")) {
			ast_log(LOG_ERROR,
				"Wizard '%s' must have '%s_auth/username' if it %s.\n", id, direction, test_variable);
			return -1;
		}
	} else {
		/* Delete auth if sends or accepts is now false. */
		obj = otw->wizard->retrieve_id(sorcery, otw->wizard_data, "auth", new_id);
		if (obj) {
			otw->wizard->delete(sorcery, otw->wizard_data, obj);
			ao2_ref(obj, -1);
		}
		return 0;
	}

	variable_list_append_return(&vars, "@pjsip_wizard", id);

	/* If the user set auth_type, don't override it. */
	if (!ast_variable_find_in_list(vars, "auth_type")) {
		variable_list_append_return(&vars, "auth_type", "userpass");
	}

	obj = create_object(sorcery, new_id, "auth", vars);
	if (!obj) {
		return -1;
	}

	if (otw->wizard->update(sorcery, otw->wizard_data, obj)) {
		otw->wizard->create(sorcery, otw->wizard_data, obj);
	}
	ao2_ref(obj, -1);

	return 0;
}

static int handle_auths(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz)
{
	int rc;

	if ((rc = handle_auth(sorcery, otw, wiz, "outbound"))) {
		return rc;
	}

	return handle_auth(sorcery, otw, wiz, "inbound");
}

static int handle_aor(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz, struct string_vector *remote_hosts_vector)
{
	struct ast_variable *wizvars = ast_category_first(wiz);
	struct ast_sorcery_object *obj = NULL;
	const char *id = ast_category_get_name(wiz);
	const char *contact_pattern;
	int host_count = AST_VECTOR_SIZE(remote_hosts_vector);
	RAII_VAR(struct ast_variable *, vars, get_object_variables(wizvars, "aor/"), ast_variables_destroy);

	variable_list_append(&vars, "@pjsip_wizard", id);

	/* If the user explicitly specified an aor/contact, don't use remote hosts. */
	if (!ast_variable_find_in_list(vars, "contact")) {
		if (!(contact_pattern = ast_variable_find_in_list(wizvars, "contact_pattern"))) {
			contact_pattern = "sip:${REMOTE_HOST}";
		}

		if (host_count > 0 && !ast_strlen_zero(contact_pattern)) {
			int host_counter;

			/* ast_str_substitute_variables operate on a varshead list so we have
			 * to create one to hold the REPORT_HOST substitution, do the substitution,
			 * then append the result to the ast_variable list.
			 */
			for (host_counter = 0; host_counter < host_count; host_counter++) {
				RAII_VAR(struct ast_str *, new_str, ast_str_create(64), ast_free);
				RAII_VAR(struct varshead *, subst_vars, ast_var_list_create(), ast_var_list_destroy);
				struct ast_var_t *var = ast_var_assign("REMOTE_HOST",
					AST_VECTOR_GET(remote_hosts_vector, host_counter));

				AST_VAR_LIST_INSERT_TAIL(subst_vars, var);
				ast_str_substitute_variables_varshead(&new_str, 0, subst_vars,
					contact_pattern);

				variable_list_append_return(&vars, "contact", ast_str_buffer(new_str));
			}
		}
	}

	obj = create_object(sorcery, id, "aor", vars);
	if (!obj) {
		return -1;
	}

	if (otw->wizard->update(sorcery, otw->wizard_data, obj)) {
		otw->wizard->create(sorcery, otw->wizard_data, obj);
	}
	ao2_ref(obj, -1);

	return 0;
}

static int handle_endpoint(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz)
{
	struct ast_variable *wizvars = ast_category_first(wiz);
	struct ast_sorcery_object *obj = NULL;
	const char *id = ast_category_get_name(wiz);
	const char *transport = ast_variable_find_in_list(wizvars, "transport");
	const char *hint_context = hint_context = ast_variable_find_in_list(wizvars, "hint_context");
	const char *hint_exten = ast_variable_find_in_list(wizvars, "hint_exten");
	const char *hint_application= ast_variable_find_in_list(wizvars, "hint_application");
	char new_id[strlen(id) + MAX_ID_SUFFIX];
	RAII_VAR(struct ast_variable *, vars, get_object_variables(wizvars, "endpoint/"), ast_variables_destroy);

	variable_list_append_return(&vars, "@pjsip_wizard", id);
	variable_list_append_return(&vars, "aors", id);

	if (ast_strlen_zero(hint_context)) {
		hint_context = ast_variable_find_in_list(vars, "context");
	}

	if (ast_strlen_zero(hint_context)) {
		hint_context = "default";
	}

	if (!ast_strlen_zero(hint_exten)) {
		/* These are added so we can find and delete the hints when the endpoint gets deleted */
		variable_list_append_return(&vars, "@hint_context", hint_context);
		variable_list_append_return(&vars, "@hint_exten", hint_exten);
	}

	if (!ast_strlen_zero(transport)) {
		variable_list_append_return(&vars, "transport", transport);
	}

	if (is_variable_true(wizvars, "sends_auth")) {
		snprintf(new_id, sizeof(new_id), "%s-oauth", id);
		variable_list_append_return(&vars, "outbound_auth", new_id);
	}

	if (is_variable_true(wizvars, "accepts_auth")) {
		snprintf(new_id, sizeof(new_id), "%s-iauth", id);
		variable_list_append_return(&vars, "auth", new_id);
	}

	obj = create_object(sorcery, id, "endpoint", vars);
	if (!obj) {
		return -1;
	}

	if (otw->wizard->update(sorcery, otw->wizard_data, obj)) {
		otw->wizard->create(sorcery, otw->wizard_data, obj);
	}
	ao2_ref(obj, -1);

	if (!ast_strlen_zero(hint_exten)) {
		if (is_variable_true(wizvars, "has_hint")) {
			add_hints(hint_context, hint_exten, hint_application, id);
		} else {
			delete_extens(hint_context, hint_exten);
		}
	}

	return 0;
}

static int handle_identify(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz, struct string_vector *remote_hosts_vector)
{
	struct ast_variable *wizvars = ast_category_first(wiz);
	struct ast_sorcery_object *obj = NULL;
	const char *id = ast_category_get_name(wiz);
	char new_id[strlen(id) + MAX_ID_SUFFIX];
	int host_count = AST_VECTOR_SIZE(remote_hosts_vector);
	int host_counter;
	RAII_VAR(struct ast_variable *, vars, get_object_variables(wizvars, "identify/"), ast_variables_destroy);

	snprintf(new_id, sizeof(new_id), "%s-identify", id);

	/* If accepting registrations, we don't need an identify. */
	if (is_variable_true(wizvars, "accepts_registrations")) {
		/* If one exists, delete it. */
		obj = otw->wizard->retrieve_id(sorcery, otw->wizard_data, "identify", new_id);
		if (obj) {
			otw->wizard->delete(sorcery, otw->wizard_data, obj);
			ao2_ref(obj, -1);
		}
		return 0;
	}

	if (!host_count) {
		ast_log(LOG_ERROR,
			"Wizard '%s' must have 'remote_hosts' if it doesn't accept registrations.\n", id);
		return -1;
	}

	variable_list_append_return(&vars, "endpoint", id);
	variable_list_append_return(&vars, "@pjsip_wizard", id);

	if (!ast_variable_find_in_list(vars, "match")) {
		for (host_counter = 0; host_counter < host_count; host_counter++) {
			char *rhost = AST_VECTOR_GET(remote_hosts_vector, host_counter);
			char host[strlen(rhost) + 1];
			char *colon;

			/* If there's a :port specified, we have to remove it. */
			strcpy(host, rhost); /* Safe */
			colon = strchr(host, ':');
			if (colon) {
				*colon = '\0';
			}

			variable_list_append_return(&vars, "match", host);
		}
	}

	obj = create_object(sorcery, new_id, "identify", vars);
	if (!obj) {
		return -1;
	}

	if (otw->wizard->update(sorcery, otw->wizard_data, obj)) {
		otw->wizard->create(sorcery, otw->wizard_data, obj);
	}
	ao2_ref(obj, -1);

	return 0;
}

static int handle_phoneprov(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz)
{
	struct ast_variable *wizvars = ast_category_first(wiz);
	struct ast_sorcery_object *obj = NULL;
	const char *id = ast_category_get_name(wiz);
	char new_id[strlen(id) + MAX_ID_SUFFIX];
	RAII_VAR(struct ast_variable *, vars, get_object_variables(wizvars, "phoneprov/"), ast_variables_destroy);

	snprintf(new_id, sizeof(new_id), "%s-phoneprov", id);

	if (!is_variable_true(wizvars, "has_phoneprov")) {
		obj = otw->wizard->retrieve_id(sorcery, otw->wizard_data, "phoneprov", new_id);
		if (obj) {
			otw->wizard->delete(sorcery, otw->wizard_data, obj);
			ao2_ref(obj, -1);
		}
		return 0;
	}

	if (!ast_variable_find_in_list(wizvars, "phoneprov/MAC")) {
		ast_log(LOG_ERROR,
			"Wizard '%s' must have 'phoneprov/MAC' if it has_phoneprov.\n", id);
		return -1;
	}

	variable_list_append_return(&vars, "endpoint", id);
	variable_list_append_return(&vars, "@pjsip_wizard", id);

	obj = create_object(sorcery, new_id, "phoneprov", vars);
	if (!obj) {
		return -1;
	}

	if (otw->wizard->update(sorcery, otw->wizard_data, obj)) {
		otw->wizard->create(sorcery, otw->wizard_data, obj);
	}
	ao2_ref(obj, -1);

	return 0;
}

static int delete_existing_cb(void *obj, void *arg, int flags)
{
	struct object_type_wizard *otw = arg;

	if (!strcmp(otw->object_type, "endpoint")) {
		const char *context = ast_sorcery_object_get_extended(obj, "hint_context");
		const char *exten = ast_sorcery_object_get_extended(obj, "hint_exten");
		if (!ast_strlen_zero(context) && !ast_strlen_zero(exten)) {
			delete_extens(context, exten);
		}
	}

	otw->wizard->delete(otw->sorcery, otw->wizard_data, obj);

	return CMP_MATCH;
}

static int handle_registrations(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz, struct string_vector *remote_hosts_vector)
{
	struct ast_variable *search;
	struct ast_variable *wizvars = ast_category_first(wiz);
	const char *id = ast_category_get_name(wiz);
	const char *server_uri_pattern;
	const char *client_uri_pattern;
	const char *transport = ast_variable_find_in_list(wizvars, "transport");
	const char *username;
	char new_id[strlen(id) + MAX_ID_SUFFIX];
	int host_count = AST_VECTOR_SIZE(remote_hosts_vector);
	int host_counter;
	RAII_VAR(struct ast_variable *, vars, get_object_variables(wizvars, "registration/"), ast_variables_destroy);
	RAII_VAR(struct ao2_container *, existing,
		ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL), ao2_cleanup);

	if (!existing) {
		return -1;
	}

	/* Find any existing registrations. */
	search = ast_variable_new("@pjsip_wizard", id, "");
	if (!search) {
		return -1;
	}

	otw->wizard->retrieve_multiple(sorcery, otw->wizard_data, "registration", existing, search);
	ast_variables_destroy(search);

	/* If not sending registrations, delete ALL existing registrations for this wizard. */
	if (!is_variable_true(wizvars, "sends_registrations")) {
		if (ao2_container_count(existing) > 0) {
			ao2_callback(existing, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, delete_existing_cb, otw);
		}
		return 0;
	}

	if (!host_count) {
		ast_log(LOG_ERROR, "Wizard '%s' must have 'remote_hosts' if it sends registrations.\n", id);
		return -1;
	}

	variable_list_append_return(&vars, "@pjsip_wizard", id);

	if (!(server_uri_pattern = ast_variable_find_in_list(wizvars, "server_uri_pattern"))) {
		server_uri_pattern = "sip:${REMOTE_HOST}";
	}

	if (!(client_uri_pattern = ast_variable_find_in_list(wizvars, "client_uri_pattern"))) {
		client_uri_pattern = "sip:${USERNAME}@${REMOTE_HOST}";
	}

	if(is_variable_true(wizvars, "sends_auth")) {
		username = ast_variable_find_in_list(wizvars, "outbound_auth/username");
	} else {
		username = id;
	}


	/* Unlike aor and identify, we need to create a separate registration object
	 * for each remote host.
	 */
	for (host_counter = 0; host_counter < host_count; host_counter++) {
		struct ast_var_t *rh = ast_var_assign("REMOTE_HOST",
			AST_VECTOR_GET(remote_hosts_vector, host_counter));
		struct ast_var_t *un = ast_var_assign("USERNAME", username);
		struct ast_sorcery_object *obj;
		RAII_VAR(struct ast_str *, uri, ast_str_create(64), ast_free);
		RAII_VAR(struct varshead *, subst_vars, ast_var_list_create(), ast_var_list_destroy);
		RAII_VAR(struct ast_variable *, registration_vars, vars ? ast_variables_dup(vars) : NULL, ast_variables_destroy);

		AST_VAR_LIST_INSERT_TAIL(subst_vars, rh);
		AST_VAR_LIST_INSERT_TAIL(subst_vars, un);

		if (!ast_strlen_zero(server_uri_pattern)) {
			ast_str_substitute_variables_varshead(&uri, 0, subst_vars,
				server_uri_pattern);
			variable_list_append_return(&registration_vars, "server_uri", ast_str_buffer(uri));
		}

		if (!ast_strlen_zero(client_uri_pattern)) {
			ast_str_reset(uri);
			ast_str_substitute_variables_varshead(&uri, 0, subst_vars,
				client_uri_pattern);
			variable_list_append_return(&registration_vars, "client_uri", ast_str_buffer(uri));
		}

		if (is_variable_true(wizvars, "sends_auth")) {
			snprintf(new_id, sizeof(new_id), "%s-oauth", id);
			variable_list_append_return(&registration_vars, "outbound_auth", new_id);
		}

		if (!ast_strlen_zero(transport)) {
			variable_list_append_return(&registration_vars, "transport", transport);
		}

		snprintf(new_id, sizeof(new_id), "%s-reg-%d", id, host_counter);

		obj = create_object(sorcery, new_id, "registration", registration_vars);
		if (!obj) {
			return -1;
		}

		if (otw->wizard->update(sorcery, otw->wizard_data, obj)) {
			otw->wizard->create(sorcery, otw->wizard_data, obj);
		}
		ao2_ref(obj, -1);

		/* Unlink it from the 'existing' container.  Any left will be deleted from
		 * sorcery.  If it wasn't in the existing container, no harm.
		 */
		ao2_callback(existing, OBJ_NODATA | OBJ_UNLINK | OBJ_SEARCH_KEY, ast_sorcery_object_id_compare, new_id);
	}

	/* If there are any excess registrations, delete them. */
	if (ao2_container_count(existing) > 0) {
		ao2_callback(existing, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, delete_existing_cb, otw);
	}

	return 0;
}

static int wizard_apply_handler(const struct ast_sorcery *sorcery, struct object_type_wizard *otw,
	struct ast_category *wiz)
{
	struct ast_variable *wizvars = ast_category_first(wiz);
	struct string_vector remote_hosts_vector;
	const char *remote_hosts;
	int rc = -1;

	AST_VECTOR_INIT(&remote_hosts_vector, 16);
	remote_hosts = ast_variable_find_in_list(wizvars, "remote_hosts");

	if (!ast_strlen_zero(remote_hosts)) {
		char *host;
		char *hosts = ast_strdupa(remote_hosts);

		while ((host = ast_strsep(&hosts, ',', AST_STRSEP_TRIM))) {
			AST_VECTOR_APPEND(&remote_hosts_vector, ast_strdup(host));
		}
	}

	ast_debug(4, "%s handler starting.\n", otw->object_type);

	if (!strcmp(otw->object_type, "auth")) {
		rc = handle_auths(sorcery, otw, wiz);
	} else if (!strcmp(otw->object_type, "aor")) {
		rc = handle_aor(sorcery, otw, wiz, &remote_hosts_vector);
	} else if (!strcmp(otw->object_type, "endpoint")) {
		rc = handle_endpoint(sorcery, otw, wiz);
	} else if (!strcmp(otw->object_type, "identify")) {
		rc = handle_identify(sorcery, otw, wiz, &remote_hosts_vector);
	} else if (!strcmp(otw->object_type, "phoneprov")) {
		rc = handle_phoneprov(sorcery, otw, wiz);
	} else if (!strcmp(otw->object_type, "registration")) {
		rc = handle_registrations(sorcery, otw, wiz, &remote_hosts_vector);
	}

	AST_VECTOR_REMOVE_CMP_UNORDERED(&remote_hosts_vector, NULL, NOT_EQUALS, ast_free);
	AST_VECTOR_FREE(&remote_hosts_vector);

	ast_debug(4, "%s handler complete.  rc: %d\n", otw->object_type, rc);

	return rc;
}

/*
 * Everything below are the sorcery observers.
 */
static void instance_created_observer(const char *name, struct ast_sorcery *sorcery);
static void object_type_loaded_observer(const char *name,
	const struct ast_sorcery *sorcery, const char *object_type, int reloaded);
static void wizard_mapped_observer(const char *name, struct ast_sorcery *sorcery,
	const char *object_type, struct ast_sorcery_wizard *wizard,
	const char *wizard_args, void *wizard_data);
static void object_type_registered_observer(const char *name,
	struct ast_sorcery *sorcery, const char *object_type);

const static struct ast_sorcery_global_observer global_observer = {
	.instance_created = instance_created_observer,
};

struct ast_sorcery_instance_observer observer = {
	.wizard_mapped = wizard_mapped_observer,
	.object_type_registered = object_type_registered_observer,
	.object_type_loaded = object_type_loaded_observer,
};

/*! \brief Called after an object type is loaded/reloaded */
static void object_type_loaded_observer(const char *name,
	const struct ast_sorcery *sorcery, const char *object_type, int reloaded)
{
	struct ast_category *category = NULL;
	struct object_type_wizard *otw = NULL;
	char *filename = "pjsip_wizard.conf";
	struct ast_flags flags = { 0 };
	struct ast_config *cfg;

	if (!strstr("auth aor endpoint identify registration phoneprov", object_type)) {
		/* Not interested. */
		return;
	}

	otw = find_wizard(object_type);
	if (!otw) {
		ast_log(LOG_ERROR, "There was no wizard for object type '%s'\n", object_type);
		return;
	}

	if (reloaded && otw->last_config) {
		flags.flags = CONFIG_FLAG_FILEUNCHANGED;
	}

	cfg = ast_config_load2(filename, object_type, flags);

	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config file '%s'\n", filename);
		return;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(2, "Config file '%s' was unchanged for '%s'.\n", filename, object_type);
		return;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Contents of config file '%s' are invalid and cannot be parsed\n", filename);
		return;
	}

	while ((category = ast_category_browse_filtered(cfg, NULL, category, "type=^wizard$"))) {
		const char *id = ast_category_get_name(category);
		struct ast_category *last_cat = NULL;
		struct ast_variable *change_set = NULL;

		if (otw->last_config) {
			last_cat = ast_category_get(otw->last_config, id, "type=^wizard$");
			ast_sorcery_changeset_create(ast_category_first(category), ast_category_first(last_cat), &change_set);
			if (last_cat) {
				ast_category_delete(otw->last_config, last_cat);
			}
		}

		if (!last_cat || change_set) {
			ast_variables_destroy(change_set);
			ast_debug(3, "%s: %s(s) for wizard '%s'\n", reloaded ? "Reload" : "Load", object_type, id);
			if (wizard_apply_handler(sorcery, otw, category)) {
				ast_log(LOG_ERROR, "Unable to create objects for wizard '%s'\n", id);
			}
		}
	}

	if (!otw->last_config) {
		otw->last_config = cfg;
		return;
	}

	/* Only wizards that weren't in the new config are left in last_config now so we need to delete
	 * all objects belonging to them.
	 */
	category = NULL;
	while ((category = ast_category_browse_filtered(otw->last_config, NULL, category, "type=^wizard$"))) {
		const char *id = ast_category_get_name(category);
		struct ast_variable *search;
		RAII_VAR(struct ao2_container *, existing,
			ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL), ao2_cleanup);

		if (!existing) {
			ast_log(LOG_ERROR, "Unable to allocate temporary container.\n");
			break;
		}

		search = ast_variable_new("@pjsip_wizard", id, "");
		if (!search) {
			ast_log(LOG_ERROR, "Unable to allocate memory for vaiable '@pjsip_wizard'.\n");
			break;
		}
		otw->wizard->retrieve_multiple(sorcery, otw->wizard_data, object_type, existing, search);
		ast_variables_destroy(search);

		if (ao2_container_count(existing) > 0) {
			ast_debug(3, "Delete on %s: %d %s(s) for wizard: %s\n",
				reloaded ? "Reload" : "Load", ao2_container_count(existing), object_type, id);
			ao2_callback(existing, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE,
				delete_existing_cb, otw);
		}
	}

	ast_config_destroy(otw->last_config);
	otw->last_config = cfg;
}

/*! \brief When each wizard is mapped, save it off to the vector. */
static void wizard_mapped_observer(const char *name, struct ast_sorcery *sorcery,
	const char *object_type, struct ast_sorcery_wizard *wizard,
	const char *wizard_args, void *wizard_data)
{
	struct object_type_wizard *otw;

	if (!is_one_of(object_type, object_types)) {
		/* Not interested. */
		return;
	}

	/* We're only interested in memory wizards with the pjsip_wizard tag. */
	if (wizard_args && !strcmp(wizard_args, "pjsip_wizard")) {
		otw = ast_malloc(sizeof(*otw) + strlen(object_type) + 1);
		otw->sorcery = sorcery;
		otw->wizard = wizard;
		otw->wizard_data = wizard_data;
		otw->last_config = NULL;
		strcpy(otw->object_type, object_type); /* Safe */
		AST_VECTOR_APPEND(&object_type_wizards, otw);
		ast_debug(1, "Wizard mapped for object_type '%s'\n", object_type);
	}
}

/*! \brief When each object type is registered, map a memory wizard to it. */
static void object_type_registered_observer(const char *name,
	struct ast_sorcery *sorcery, const char *object_type)
{
	if (is_one_of(object_type, object_types)) {
		ast_sorcery_apply_wizard_mapping(sorcery, object_type, "memory", "pjsip_wizard", 0);
	}
}

/*! \brief When the res_pjsip instance is created, add an observer to it and initialize the wizard vector.
 * Since you can't unload res_pjsip, this will only ever be called once.
 */
static void instance_created_observer(const char *name, struct ast_sorcery *sorcery)
{
	if (strcmp(name, "res_pjsip")) {
		return;
	}

	ast_sorcery_instance_observer_add(sorcery, &observer);
}

static int load_module(void)
{
	struct ast_sorcery *sorcery = NULL;
	int i;

	AST_VECTOR_INIT(&object_type_wizards, 12);
	ast_sorcery_global_observer_add(&global_observer);

	/* If this module is loading AFTER res_pjsip, we need to manually add the instance observer
	 * and map the wizards because the observers will never get triggered.
	 * The we neeed to schedule a reload.
	 */
	if (ast_module_check("res_pjsip.so") && ast_sip_get_pjsip_endpoint()) {
		sorcery = ast_sip_get_sorcery();
		if (sorcery) {
			/* Clean up and add the observer. */
			ast_sorcery_instance_observer_remove(sorcery, &observer);
			ast_sorcery_instance_observer_add(sorcery, &observer);

			for (i = 0; object_types[i]; i++) {
				ast_sorcery_apply_wizard_mapping(sorcery, object_types[i], "memory",
					"pjsip_wizard", 0);
			}

			ast_module_reload("res_pjsip.so");
		}
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	struct object_type_wizard *otw;
	int i;

	ast_sorcery_global_observer_remove(&global_observer);

	for (i = 0; object_types[i]; i++) {
		RAII_VAR(struct ao2_container *, existing,
			ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL), ao2_cleanup);

		otw = find_wizard(object_types[i]);
		if (otw) {
			if (otw->sorcery) {
				ast_sorcery_instance_observer_remove(otw->sorcery, &observer);
			}
			otw->wizard->retrieve_multiple(otw->sorcery, otw->wizard_data, object_types[i], existing, NULL);
			ao2_callback(existing, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, delete_existing_cb, otw);
		}
	}

	AST_VECTOR_REMOVE_CMP_UNORDERED(&object_type_wizards, NULL, NOT_EQUALS, OTW_DELETE_CB);
	AST_VECTOR_FREE(&object_type_wizards);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Config Wizard",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_REALTIME_DRIVER,
		);
