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
 * \brief PJSIP Phoneprov Configuration Provider
 *
 * \author George Joseph <george.joseph@fairview5.com>
  */

/*! \li \ref res_pjsip_phoneprov_provider.c uses the configuration file \ref pjsip.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page pjsip.conf pjsip.conf
 * \verbinclude pjsip.conf.sample
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_phoneprov</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/phoneprov.h"
#include "res_pjsip/include/res_pjsip_private.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_phoneprov_provider" language="en_US">
		<synopsis>Module that integrates res_pjsip with res_phoneprov.</synopsis>
		<description><para>
			<emphasis>PJSIP Phoneprov Provider</emphasis>
			</para>
			<para>This module creates the integration between <literal>res_pjsip</literal> and
			<literal>res_phoneprov</literal>.
			</para>
			<para>Each user to be integrated requires a <literal>phoneprov</literal>
			section defined in <filename>pjsip.conf</filename>.  Each section identifies
			the endpoint associated with the user and any other name/value pairs to be passed
			on to res_phoneprov's template substitution.  Only <literal>MAC</literal> and
			<literal>PROFILE</literal> variables are required.  Any other variables
			supplied will be passed through.</para>
			<para> </para>
			<para>Example:</para>
			<para>[1000]</para>
			<para>type = phoneprovr</para>
			<para>endpoint = ep1000</para>
			<para>MAC = deadbeef4dad</para>
			<para>PROFILE = grandstream2</para>
			<para>LINEKEYS = 2</para>
			<para>LINE = 1</para>
			<para>OTHERVAR = othervalue</para>
			<para> </para>
			<para>The following variables are automatically defined if an endpoint
			is defined for the user:</para>
			<enumlist>
				<enum name="USERNAME"><para>Source: The user_name defined in the first auth reference
				in the endpoint.</para></enum>
				<enum name="SECRET"><para>Source: The user_pass defined in the first auth reference
				in the endpoint.</para></enum>
				<enum name="CALLERID"><para>Source: The number part of the callerid defined in
				the endpoint.</para></enum>
				<enum name="DISPLAY_NAME"><para>Source: The name part of the callerid defined in
				the endpoint.</para></enum>
				<enum name="LABEL"><para>Source: The id of the phoneprov section.</para></enum>
			</enumlist>
			<para> </para>
			<para>In addition to the standard variables, the following are also automatically defined:</para>
			<enumlist>
				<enum name="ENDPOINT_ID"><para>Source: The id of the endpoint.</para></enum>
				<enum name="TRANSPORT_ID"><para>Source: The id of the transport used by the endpoint.</para></enum>
				<enum name="AUTH_ID"><para>Source: The id of the auth used by the endpoint.</para></enum>
			</enumlist>
			<para> </para>
			<para>All other template substitution variables must be explicitly defined in the
			phoneprov_default or phoneprov sections.</para>
		</description>

		<configFile name="pjsip.conf">
			<configObject name="phoneprov">
				<synopsis>Provides variables for each user.</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'phoneprov'.</synopsis>
				</configOption>
				<configOption name="endpoint">
					<synopsis>The endpoint from which variables will be retrieved.</synopsis>
				</configOption>
				<configOption name="MAC">
					<synopsis>The mac address for this user. (required)</synopsis>
				</configOption>
				<configOption name="PROFILE">
					<synopsis>The phoneprov profile to use for this user. (required)</synopsis>
				</configOption>
				<configOption name="*">
					<synopsis>Other name/value pairs to be passed through for use in templates.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

static struct ast_sorcery *sorcery;

/*! \brief Structure for a phoneprov object */
struct phoneprov {
	SORCERY_OBJECT(details);
	struct varshead *vars;
};

/*! \brief Destructor function for phoneprov */
static void phoneprov_destroy(void *obj)
{
	struct phoneprov *pp = obj;
	char *mac = ast_var_find(pp->vars, "MAC");

	if (mac) {
		ast_phoneprov_delete_extension(AST_MODULE, mac);
	}

	ast_var_list_destroy(pp->vars);
}

/*! \brief Allocator for phoneprov */
static void *phoneprov_alloc(const char *name)
{
	struct phoneprov *pp = ast_sorcery_generic_alloc(sizeof(*pp), phoneprov_destroy);

	if (!pp || !(pp->vars = ast_var_list_create())) {
		ast_log(LOG_ERROR, "Unable to allocate memory for phoneprov structure %s\n",
			name);
		ao2_cleanup(pp);
		return NULL;
	}

	return pp;
}

/*! \brief Helper that creates an ast_var_t and inserts it into the list */
static int assign_and_insert(const char *name, const char *value, struct varshead *vars)
{
	struct ast_var_t *var;

	if (ast_strlen_zero(name) || !vars) {
		return -1;
	}

	/* Just ignore if the value is NULL or empty */
	if (ast_strlen_zero(value)) {
		return 0;
	}

	var = ast_var_assign(name, value);
	if (!var) {
		ast_log(LOG_ERROR, "Could not allocate variable memory for variable.\n");
		return -1;
	}
	AST_VAR_LIST_INSERT_TAIL(vars, var);

	return 0;
}

/*! \brief Adds a config name/value pair to the phoneprov object */
static int aco_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct phoneprov *pp = obj;

	return assign_and_insert(var->name, var->value, pp->vars);
}

/*! \brief Converts the phoneprov varlist to an ast_variable list */
static int fields_handler(const void *obj, struct ast_variable **fields)
{
	const struct phoneprov *pp = obj;
	struct ast_var_t *pvar;
	struct ast_variable *head = NULL;
	struct ast_variable *tail = NULL;
	struct ast_variable *var;

	AST_VAR_LIST_TRAVERSE(pp->vars, pvar) {
		var = ast_variable_new(pvar->name, pvar->value, "");
		if (!var) {
			ast_variables_destroy(head);
			return -1;
		}
		if (!head) {
			head = var;
			tail = var;
			continue;
		}
		tail->next = var;
		tail = var;
	}

	*fields = head;

	return 0;
}

static int load_endpoint(const char *id, const char *endpoint_name, struct varshead *vars,
	char *port_string)
{
	struct ast_sip_auth *auth;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);
	const char *auth_name;

	*port_string = '\0';

	/* We need to use res_pjsip's sorcery instance instead of our own to
	 * get endpoint and auth.
	 */
	endpoint = ast_sorcery_retrieve_by_id(sorcery, "endpoint",
		endpoint_name);
	if (!endpoint) {
		ast_log(LOG_ERROR, "phoneprov %s contained invalid endpoint %s.\n", id,
			endpoint_name);
		return -1;
	}

	assign_and_insert("ENDPOINT_ID", endpoint_name, vars);
	assign_and_insert("TRANSPORT_ID", endpoint->transport, vars);

	if (endpoint->id.self.number.valid && !ast_strlen_zero(endpoint->id.self.number.str)) {
		assign_and_insert(ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_CALLERID),
			endpoint->id.self.number.str, vars);
	}

	if (endpoint->id.self.name.valid && !ast_strlen_zero(endpoint->id.self.name.str)) {
		assign_and_insert(
			ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_DISPLAY_NAME),
			endpoint->id.self.name.str, vars);
	}

	transport = ast_sorcery_retrieve_by_id(sorcery, "transport",
		endpoint->transport);
	if (!transport) {
		ast_log(LOG_ERROR, "Endpoint %s contained invalid transport %s.\n", endpoint_name,
			endpoint->transport);
		return -1;
	}
	snprintf(port_string, 6, "%d", pj_sockaddr_get_port(&transport->host));

	if (!AST_VECTOR_SIZE(&endpoint->inbound_auths)) {
		return 0;
	}
	auth_name = AST_VECTOR_GET(&endpoint->inbound_auths, 0);

	auth = ast_sorcery_retrieve_by_id(sorcery, "auth", auth_name);
	if (!auth) {
		ast_log(LOG_ERROR, "phoneprov %s contained invalid auth %s.\n", id, auth_name);
		return -1;
	}

	assign_and_insert("AUTH_ID", auth_name, vars);
	assign_and_insert(ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_USERNAME),
		auth->auth_user, vars);
	assign_and_insert(ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_SECRET),
		auth->auth_pass, vars);
	ao2_ref(auth, -1);

	return 0;
}

/*! \brief Callback that validates the phoneprov object */
static void users_apply_handler(struct phoneprov *pp)
{
	const char *id = ast_sorcery_object_get_id(pp);
	const char *endpoint_name;
	char port_string[6];

	if (!ast_var_find(pp->vars,
		ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_MAC))) {
		ast_log(LOG_ERROR, "phoneprov %s must contain a MAC entry.\n", id);
		return;
	}

	if (!ast_var_find(pp->vars,
		ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_PROFILE))) {
		ast_log(LOG_ERROR, "phoneprov %s must contain a PROFILE entry.\n", id);
		return;
	}

	endpoint_name = ast_var_find(pp->vars, "endpoint");
	if (endpoint_name) {
		if (load_endpoint(id, endpoint_name, pp->vars, port_string)) {
			return;
		}
	}

	if (!ast_var_find(pp->vars,
		ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_USERNAME))) {
		assign_and_insert(
			ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_USERNAME), id,
			pp->vars);
	}

	if (!ast_var_find(pp->vars,
		ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_LABEL))) {
		assign_and_insert(ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_LABEL),
			id, pp->vars);
	}

	if (!ast_var_find(pp->vars,
		ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_SERVER_PORT))) {
		assign_and_insert("SERVER_PORT", S_OR(port_string, "5060"), pp->vars);
	}

	if (!ast_var_find(pp->vars,
		ast_phoneprov_std_variable_lookup(AST_PHONEPROV_STD_PROFILE))) {
		ast_log(LOG_ERROR, "phoneprov %s didn't contain a PROFILE entry.\n", id);
	}

	ast_phoneprov_add_extension(AST_MODULE, pp->vars);

	return;
}

/*! \brief Callback that loads the users from phoneprov sections */
static int load_users(void)
{
	struct ao2_container *users;
	struct ao2_iterator i;
	struct phoneprov *pp;

	ast_sorcery_reload_object(sorcery, "phoneprov");

	users = ast_sorcery_retrieve_by_fields(sorcery, "phoneprov",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!users) {
		return 0;
	}

	i = ao2_iterator_init(users, 0);
	while ((pp = ao2_iterator_next(&i))) {
		users_apply_handler(pp);
		ao2_ref(pp, -1);
	}
	ao2_iterator_destroy(&i);
	ao2_ref(users, -1);

	return 0;
}

static int load_module(void)
{
	sorcery = ast_sip_get_sorcery();

	ast_sorcery_apply_config(sorcery, "res_pjsip_phoneprov_provider");
	ast_sorcery_apply_default(sorcery, "phoneprov", "config",
		"pjsip.conf,criteria=type=phoneprov");

	ast_sorcery_object_register(sorcery, "phoneprov", phoneprov_alloc, NULL,
		NULL);

	ast_sorcery_object_field_register(sorcery, "phoneprov", "type", "", OPT_NOOP_T, 0,
		0);
	ast_sorcery_object_fields_register(sorcery, "phoneprov", "^", aco_handler,
		fields_handler);

	ast_sorcery_load_object(sorcery, "phoneprov");

	if (ast_phoneprov_provider_register(AST_MODULE, load_users)) {
		ast_log(LOG_ERROR, "Unable to register pjsip phoneprov provider.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_phoneprov_provider_unregister(AST_MODULE);

	return 0;
}

static int reload_module(void)
{
	ast_phoneprov_provider_unregister(AST_MODULE);

	if (ast_phoneprov_provider_register(AST_MODULE, load_users)) {
		ast_log(LOG_ERROR, "Unable to register pjsip phoneprov provider.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Phoneprov Provider",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_phoneprov",
);
