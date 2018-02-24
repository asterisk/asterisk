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

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>deprecated</support_level>
	<replacement>res_pjsip_registrar</replacement>
 ***/

/*
 * This module has been refactored into res_pjsip_registrar
 */

#include "asterisk.h"

#include "asterisk/module.h"

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "OBSOLETE PJSIP Contact Auto-Expiration",
	.support_level = AST_MODULE_SUPPORT_DEPRECATED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
