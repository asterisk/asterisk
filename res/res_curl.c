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
 * \extref Depends on the CURL library  - http://curl.haxx.se/
 * 
 */

/*** MODULEINFO
	<depend>curl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <curl/curl.h>

#include "asterisk/module.h"

static int unload_module(void)
{
	int res = 0;

	/* If the dependent modules are still in memory, forbid unload */
	if (ast_module_check("func_curl.so")) {
		ast_log(LOG_ERROR, "func_curl.so (dependent module) is still loaded.  Cannot unload res_curl.so\n");
		return -1;
	}

	if (ast_module_check("res_config_curl.so")) {
		ast_log(LOG_ERROR, "res_config_curl.so (dependent module) is still loaded.  Cannot unload res_curl.so\n");
		return -1;
	}

	curl_global_cleanup();

	return res;
}

static int load_module(void)
{
	int res = 0;

	if (curl_global_init(CURL_GLOBAL_ALL)) {
		ast_log(LOG_ERROR, "Unable to initialize the CURL library. Cannot load res_curl\n");
		return AST_MODULE_LOAD_DECLINE;
	}	

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "cURL Resource Module");


