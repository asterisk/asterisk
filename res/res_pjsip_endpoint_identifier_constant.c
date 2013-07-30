/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"

static struct ast_sip_endpoint *constant_identify(pjsip_rx_data *rdata)
{
	/* This endpoint identifier always returns the same endpoint. It's used
	 * simply for testing. It allocates an endpoint from sorcery so default values
	 * do get applied.
	 */
	struct ast_sip_endpoint *endpoint = ast_sorcery_alloc(ast_sip_get_sorcery(), "endpoint", NULL);
	if (!endpoint) {
		return NULL;
	}
	ast_parse_allow_disallow(&endpoint->prefs, endpoint->codecs, "ulaw", 1);
	return endpoint;
}

static struct ast_sip_endpoint_identifier constant_identifier = {
	.identify_endpoint = constant_identify,
};

static int load_module(void)
{
	ast_sip_register_endpoint_identifier(&constant_identifier);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Constant Endpoint Identifier",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
