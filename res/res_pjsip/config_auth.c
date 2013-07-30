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
	case AST_SIP_AUTH_TYPE_USER_PASS:
		if (ast_strlen_zero(auth->auth_pass)) {
			ast_log(LOG_ERROR, "'userpass' authentication specified but no"
					"password specified for auth '%s'\n", ast_sorcery_object_get_id(auth));
			res = -1;
		}
		break;
	case AST_SIP_AUTH_TYPE_MD5:
		if (ast_strlen_zero(auth->md5_creds)) {
			ast_log(LOG_ERROR, "'md5' authentication specified but no md5_cred"
					"specified for auth '%s'\n", ast_sorcery_object_get_id(auth));
			res = -1;
		} else if (strlen(auth->md5_creds) != PJSIP_MD5STRLEN) {
			ast_log(LOG_ERROR, "'md5' authentication requires digest of size '%d', but"
				"digest is '%d' in size for auth '%s'\n", PJSIP_MD5STRLEN, (int)strlen(auth->md5_creds),
				ast_sorcery_object_get_id(auth));
			res = -1;
		}
		break;
	case AST_SIP_AUTH_TYPE_ARTIFICIAL:
		break;
	}

	return res;
}

/*! \brief Initialize sorcery with auth support */
int ast_sip_initialize_sorcery_auth(struct ast_sorcery *sorcery)
{
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
			"asterisk", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, realm));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "nonce_lifetime",
			"32", OPT_UINT_T, 0, FLDSET(struct ast_sip_auth, nonce_lifetime));
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "auth_type",
			"userpass", auth_type_handler, NULL, 0, 0);

	return 0;
}
