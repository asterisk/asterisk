/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/sorcery.h"

#include "asterisk/res_stir_shaken.h"
#include "res_stir_shaken/stir_shaken.h"
#include "res_stir_shaken/general.h"
#include "res_stir_shaken/store.h"
#include "res_stir_shaken/certificate.h"

static struct ast_sorcery *stir_shaken_sorcery;

struct ast_sorcery *ast_stir_shaken_sorcery(void)
{
	return stir_shaken_sorcery;
}

EVP_PKEY *ast_stir_shaken_get_private_key(const char *caller_id_number)
{
	return stir_shaken_certificate_get_private_key(caller_id_number);
}

static int reload_module(void)
{
	if (stir_shaken_sorcery) {
		ast_sorcery_reload(stir_shaken_sorcery);
	}

	return 0;
}

static int unload_module(void)
{
	stir_shaken_certificate_unload();
	stir_shaken_store_unload();
	stir_shaken_general_unload();

	ast_sorcery_unref(stir_shaken_sorcery);
	stir_shaken_sorcery = NULL;

	return 0;
}

static int load_module(void)
{
	if (!(stir_shaken_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "stir/shaken - failed to open sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stir_shaken_general_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stir_shaken_store_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stir_shaken_certificate_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_load(ast_stir_shaken_sorcery());

	return AST_MODULE_LOAD_SUCCESS;
}

#undef AST_BUILDOPT_SUM
#define AST_BUILDOPT_SUM ""

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
				"STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
);
