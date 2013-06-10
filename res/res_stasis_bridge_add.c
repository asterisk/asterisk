/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief res_stasis bridge add channel support.
 *
 * \author Kinsey Moore <kmoore@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/bridging.h"

static void *app_control_join_bridge(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge_features features;
	struct ast_bridge *bridge = data;
	ast_bridge_features_init(&features);
	ast_bridge_join(bridge,	chan, NULL, &features, NULL, 0);
	ast_bridge_features_cleanup(&features);

	return NULL;
}

void stasis_app_control_add_channel_to_bridge(struct stasis_app_control *control, struct ast_bridge *bridge)
{
	ast_debug(3, "%s: Sending channel add_to_bridge command\n",
			stasis_app_control_get_channel_id(control));

	stasis_app_send_command_async(control, app_control_join_bridge, bridge);
}

static int load_module(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS,
	"Stasis application bridge add channel support",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis");
