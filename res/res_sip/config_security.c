/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
	<depend>res_sip</depend>
	<support_level>core</support_level>
 ***/
#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_sip.h"
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/acl.h"

static int acl_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_security *security = obj;
	int error = 0;
	int ignore;
	if (!strncmp(var->name, "contact", 7)) {
		ast_append_acl(var->name + 7, var->value, &security->contact_acl, &error, &ignore);
	} else {
		ast_append_acl(var->name, var->value, &security->acl, &error, &ignore);
	}

	return error;
}

static void security_destroy(void *obj)
{
	struct ast_sip_security *security = obj;
	security->acl = ast_free_acl_list(security->acl);
	security->contact_acl = ast_free_acl_list(security->contact_acl);
}

static void *security_alloc(const char *name)
{
	struct ast_sip_security *security =
		ast_sorcery_generic_alloc(sizeof(*security), security_destroy);

	if (!security) {
		return NULL;
	}

	return security;
}

int ast_sip_initialize_sorcery_security(struct ast_sorcery *sorcery)
{
	ast_sorcery_apply_default(sorcery, SIP_SORCERY_SECURITY_TYPE,
				  "config", "res_sip.conf,criteria=type=security");

	if (ast_sorcery_object_register(sorcery, SIP_SORCERY_SECURITY_TYPE,
					security_alloc, NULL, NULL)) {

		ast_log(LOG_ERROR, "Failed to register SIP %s object with sorcery\n",
			SIP_SORCERY_SECURITY_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_SECURITY_TYPE, "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_SECURITY_TYPE, "permit", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_SECURITY_TYPE, "deny", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_SECURITY_TYPE, "acl", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_SECURITY_TYPE, "contactpermit", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_SECURITY_TYPE, "contactdeny", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_SECURITY_TYPE, "contactacl", "", acl_handler, NULL, 0, 0);
	return 0;
}
