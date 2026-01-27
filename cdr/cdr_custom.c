/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Includes code and algorithms from the Zapata library.
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

/*!
 * \file
 * \brief Custom Comma Separated Value CDR records.
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg See also \ref AstCDR
 *
 * Logs in LOG_DIR/cdr_custom
 * \ingroup cdr_drivers
 *
 * The logic for this module now resides in res/res_cdrel_custom.c.
 *
 */

/*! \li \ref cdr_custom.c uses the configuration file \ref cdr_custom.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr_custom.conf cdr_custom.conf
 * \verbinclude cdr_custom.conf.sample
 */

/*** MODULEINFO
	<depend>res_cdrel_custom</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/res_cdrel_custom.h"

#define CONFIG "cdr_custom.conf"

#define CUSTOM_BACKEND_NAME "CDR File custom backend"

static struct cdrel_configs *configs;

/*!
 * Protects in-flight log transactions from reloads.
 */
static ast_rwlock_t configs_lock;

#define CDREL_RECORD_TYPE cdrel_record_cdr
#define CDREL_BACKEND_TYPE cdrel_backend_text

static int custom_log(struct ast_cdr *cdr)
{
	int res = 0;

	ast_rwlock_rdlock(&configs_lock);
	res = cdrel_logger(configs, cdr);
	ast_rwlock_unlock(&configs_lock);

	return res;
}

static int unload_module(void)
{
	int res = 0;

	ast_rwlock_wrlock(&configs_lock);
	res = cdrel_unload_module(CDREL_BACKEND_TYPE, CDREL_RECORD_TYPE, configs, CUSTOM_BACKEND_NAME);
	ast_rwlock_unlock(&configs_lock);
	if (res == 0) {
		ast_rwlock_destroy(&configs_lock);
	}

	return res;
}

static enum ast_module_load_result load_module(void)
{
	if (ast_rwlock_init(&configs_lock) != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	configs = cdrel_load_module(CDREL_BACKEND_TYPE, CDREL_RECORD_TYPE, CONFIG, CUSTOM_BACKEND_NAME, custom_log);

	return configs ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

static int reload(void)
{
	int res = 0;
	ast_rwlock_wrlock(&configs_lock);
	res = cdrel_reload_module(CDREL_BACKEND_TYPE, CDREL_RECORD_TYPE, &configs, CONFIG);
	ast_rwlock_unlock(&configs_lock);
	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Customizable Comma Separated Values CDR Backend",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cdr,res_cdrel_custom",
);
