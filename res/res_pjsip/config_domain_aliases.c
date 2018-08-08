/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
#include "include/res_pjsip_private.h"
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"

static void domain_alias_destroy(void *obj)
{
	struct ast_sip_domain_alias *alias = obj;

	ast_string_field_free_memory(alias);
}

static void *domain_alias_alloc(const char *name)
{
	struct ast_sip_domain_alias *alias;

	alias = ast_sorcery_generic_alloc(sizeof(*alias), domain_alias_destroy);
	if (!alias) {
		return NULL;
	}

	if (ast_string_field_init(alias, 256)) {
		ao2_cleanup(alias);
		return NULL;
	}

	return alias;
}

/*! \brief Apply handler for domain_alias type */
static int domain_alias_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_domain_alias *alias = obj;

	if (ast_strlen_zero(alias->domain)) {
		/*
		 * What is the point of defining an alias and not saying
		 * what is being aliased?
		 */
		ast_log(LOG_ERROR, "%s '%s' missing required domain being aliased.\n",
			SIP_SORCERY_DOMAIN_ALIAS_TYPE, ast_sorcery_object_get_id(alias));
		return -1;
	}
	return 0;
}

/*! \brief Initialize sorcery with domain alias support */
int ast_sip_initialize_sorcery_domain_alias(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	ast_sorcery_apply_default(sorcery, SIP_SORCERY_DOMAIN_ALIAS_TYPE, "config", "pjsip.conf,criteria=type=domain_alias");

	if (ast_sorcery_object_register(sorcery, SIP_SORCERY_DOMAIN_ALIAS_TYPE,
		domain_alias_alloc, NULL, domain_alias_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_DOMAIN_ALIAS_TYPE, "type", "",
			OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_DOMAIN_ALIAS_TYPE, "domain",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_domain_alias, domain));

	return 0;
}
