/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
	<depend>libxml2</depend>
	<depend>libxslt</depend>
	<support_level>core</support_level>
 ***/


#include "asterisk.h"
#define AST_API_MODULE
#include "asterisk/res_geolocation.h"
#include "res_geolocation/geoloc_private.h"

static int reload_module(void)
{
	int res = 0;

	res = geoloc_civicaddr_reload();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}
	res = geoloc_gml_reload();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}
	res = geoloc_config_reload();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}
	res = geoloc_eprofile_reload();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}
	res = geoloc_dialplan_reload();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}
	res = geoloc_channel_reload();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res = 0;

	res += geoloc_channel_unload();
	res += geoloc_dialplan_unload();
	res += geoloc_eprofile_unload();
	res += geoloc_config_unload();
	res += geoloc_gml_unload();
	res += geoloc_civicaddr_unload();

	return (res != 0);
}

static int load_module(void)
{
	int res = 0;

	res = geoloc_civicaddr_load();
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = geoloc_gml_load();
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}

	res = geoloc_config_load();
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = geoloc_eprofile_load();
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = geoloc_dialplan_load();
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = geoloc_channel_load();
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "res_geolocation Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 10,
);
