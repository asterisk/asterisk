/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2008, Digium, Inc.
 *
 * Tilghman Lesher <res_curl_v1@the-tilghman.com>
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
 * \brief curl resource engine
 *
 * \author Tilghman Lesher <res_curl_v1@the-tilghman.com>
 *
 * Depends on the CURL library  - http://curl.haxx.se/
 * 
 */

/*! \li \ref res_curl.c uses the configuration file \ref res_curl.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page res_curl.conf res_curl.conf
 * \verbinclude res_curl.conf.sample
 */

/*** MODULEINFO
	<depend>curl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <curl/curl.h>

#include "asterisk/module.h"

static const char *dependents[] = {
	"func_curl.so",
	"res_config_curl.so",
};

static int unload_module(void)
{
	int res = 0;
	size_t i;

	/* If the dependent modules are still in memory, forbid unload */
	for (i = 0; i < ARRAY_LEN(dependents); i++) {
		if (ast_module_check(dependents[i])) {
			ast_log(LOG_ERROR, "%s (dependent module) is still loaded.  Cannot unload res_curl.so\n", dependents[i]);
			res = -1;
		}
	}

	if (res)
		return -1;

	curl_global_cleanup();

	return res;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	int res = 0;

	if (curl_global_init(CURL_GLOBAL_ALL)) {
		ast_log(LOG_ERROR, "Unable to initialize the cURL library. Cannot load res_curl.so\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "cURL Resource Module",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_REALTIME_DEPEND,
	);
