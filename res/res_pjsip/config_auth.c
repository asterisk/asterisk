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
#include <pjlib.h>
#include "asterisk/res_pjsip.h"
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/cli.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"

static void auth_destroy(void *obj)
{
	struct ast_sip_auth *auth = obj;
	ast_string_field_free_memory(auth);
}

static void *auth_alloc(const char *name)
{
	struct ast_sip_auth *auth = ast_sorcery_generic_alloc(sizeof(*auth), auth_destroy);

	if (!auth) {
		return NULL;
	}

	if (ast_string_field_init(auth, 64)) {
		ao2_cleanup(auth);
		return NULL;
	}

	return auth;
}

static int auth_type_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_auth *auth = obj;
	if (!strcasecmp(var->value, "userpass")) {
		auth->type = AST_SIP_AUTH_TYPE_USER_PASS;
	} else if (!strcasecmp(var->value, "md5")) {
		auth->type = AST_SIP_AUTH_TYPE_MD5;
	} else {
		ast_log(LOG_WARNING, "Unknown authentication storage type '%s' specified for %s\n",
				var->value, var->name);
		return -1;
	}
	return 0;
}

static const char *auth_types_map[] = {
	[AST_SIP_AUTH_TYPE_USER_PASS] = "userpass",
	[AST_SIP_AUTH_TYPE_MD5] = "md5"
};

const char *ast_sip_auth_type_to_str(enum ast_sip_auth_type type)
{
	return ARRAY_IN_BOUNDS(type, auth_types_map) ?
		auth_types_map[type] : "";
}

static int auth_type_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_auth *auth = obj;
	*buf = ast_strdup(ast_sip_auth_type_to_str(auth->type));
	return 0;
}

static int auth_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_auth *auth = obj;
	int res = 0;

	if (ast_strlen_zero(auth->auth_user)) {
		ast_log(LOG_ERROR, "No authentication username for auth '%s'\n",
				ast_sorcery_object_get_id(auth));
		return -1;
	}

	switch (auth->type) {
	case AST_SIP_AUTH_TYPE_MD5:
		if (ast_strlen_zero(auth->md5_creds)) {
			ast_log(LOG_ERROR, "'md5' authentication specified but no md5_cred "
					"specified for auth '%s'\n", ast_sorcery_object_get_id(auth));
			res = -1;
		} else if (strlen(auth->md5_creds) != PJSIP_MD5STRLEN) {
			ast_log(LOG_ERROR, "'md5' authentication requires digest of size '%d', but "
				"digest is '%d' in size for auth '%s'\n", PJSIP_MD5STRLEN, (int)strlen(auth->md5_creds),
				ast_sorcery_object_get_id(auth));
			res = -1;
		}
		break;
	case AST_SIP_AUTH_TYPE_USER_PASS:
	case AST_SIP_AUTH_TYPE_ARTIFICIAL:
		break;
	}

	return res;
}

int ast_sip_for_each_auth(const struct ast_sip_auth_vector *vector,
			  ao2_callback_fn on_auth, void *arg)
{
	int i;

	if (!vector || !AST_VECTOR_SIZE(vector)) {
		return 0;
	}

	for (i = 0; i < AST_VECTOR_SIZE(vector); ++i) {
		/* AST_VECTOR_GET is safe to use since the vector is immutable */
		RAII_VAR(struct ast_sip_auth *, auth, ast_sorcery_retrieve_by_id(
				 ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE,
				 AST_VECTOR_GET(vector,i)), ao2_cleanup);

		if (!auth) {
			continue;
		}

		if (on_auth(auth, arg, 0)) {
			return -1;
		}
	}

	return 0;
}

static int sip_auth_to_ami(const struct ast_sip_auth *auth,
			   struct ast_str **buf)
{
	return ast_sip_sorcery_object_to_ami(auth, buf);
}

static int format_ami_auth_handler(void *obj, void *arg, int flags)
{
	const struct ast_sip_auth *auth = obj;
	struct ast_sip_ami *ami = arg;
	const struct ast_sip_endpoint *endpoint = ami->arg;
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("AuthDetail", ami), ast_free);

	if (!buf) {
		return -1;
	}

	if (sip_auth_to_ami(auth, &buf)) {
		return -1;
	}

	if (endpoint) {
		ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));
	}

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	return 0;
}

int ast_sip_format_auths_ami(const struct ast_sip_auth_vector *auths,
			     struct ast_sip_ami *ami)
{
	return ast_sip_for_each_auth(auths, format_ami_auth_handler, ami);
}

static int format_ami_endpoint_auth(const struct ast_sip_endpoint *endpoint,
				    struct ast_sip_ami *ami)
{
	ami->arg = (void *)endpoint;
	if (ast_sip_format_auths_ami(&endpoint->inbound_auths, ami)) {
		return -1;
	}

	return ast_sip_format_auths_ami(&endpoint->outbound_auths, ami);
}

static struct ast_sip_endpoint_formatter endpoint_auth_formatter = {
	.format_ami = format_ami_endpoint_auth
};

static struct ao2_container *cli_get_container(void)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "auth",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static int cli_iterator(void *container, ao2_callback_fn callback, void *args)
{
	return ast_sip_for_each_auth(container, callback, args);
}

static void *cli_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE, id);
}

static int cli_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_MAX_WIDTH - indent - 20;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <AuthId/UserName%*.*s>\n", indent, "I/OAuth", filler, filler,
		CLI_HEADER_FILLER);

	return 0;
}

static int cli_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_auth *auth = obj;
	struct ast_sip_cli_context *context = arg;
	char title[32];

	ast_assert(context->output_buffer != NULL);

	snprintf(title, sizeof(title), "%sAuth",
		context->auth_direction ? context->auth_direction : "");

	ast_str_append(&context->output_buffer, 0, "%*s:  %s/%s\n",
		CLI_INDENT_TO_SPACES(context->indent_level), title,
		ast_sorcery_object_get_id(auth), auth->auth_user);

	if (context->show_details
		|| (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(auth, context, 0);
	}

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Auths",
		.command = "pjsip list auths",
		.usage = "Usage: pjsip list auths\n"
				 "       List the configured PJSIP Auths\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Auths",
		.command = "pjsip show auths",
		.usage = "Usage: pjsip show auths\n"
				 "       Show the configured PJSIP Auths\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Auth",
		.command = "pjsip show auth",
		.usage = "Usage: pjsip show auth <id>\n"
				 "       Show the configured PJSIP Auth\n"),
};

static struct ast_sip_cli_formatter_entry *cli_formatter;

/*! \brief Initialize sorcery with auth support */
int ast_sip_initialize_sorcery_auth(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	ast_sorcery_apply_default(sorcery, SIP_SORCERY_AUTH_TYPE, "config", "pjsip.conf,criteria=type=auth");

	if (ast_sorcery_object_register(sorcery, SIP_SORCERY_AUTH_TYPE, auth_alloc, NULL, auth_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "type", "",
			OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "username",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, auth_user));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "password",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, auth_pass));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "md5_cred",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, md5_creds));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "realm",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, realm));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "nonce_lifetime",
			"32", OPT_UINT_T, 0, FLDSET(struct ast_sip_auth, nonce_lifetime));
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "auth_type",
			"userpass", auth_type_handler, auth_type_to_str, NULL, 0, 0);

	ast_sip_register_endpoint_formatter(&endpoint_auth_formatter);

	cli_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!cli_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for cli formatter\n");
		return -1;
	}
	cli_formatter->name = SIP_SORCERY_AUTH_TYPE;
	cli_formatter->print_header = cli_print_header;
	cli_formatter->print_body = cli_print_body;
	cli_formatter->get_container = cli_get_container;
	cli_formatter->iterate = cli_iterator;
	cli_formatter->get_id = ast_sorcery_object_get_id;
	cli_formatter->retrieve_by_id = cli_retrieve_by_id;

	ast_sip_register_cli_formatter(cli_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return 0;
}

int ast_sip_destroy_sorcery_auth(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(cli_formatter);

	return 0;
}
