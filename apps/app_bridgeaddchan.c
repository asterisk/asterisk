/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Alec Davis <sivad.a@paradise.net.nz>
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
 * \brief Application to place the channel into an existing Bridge
 *
 * \author Alec Davis
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/features.h"

/*** DOCUMENTATION
	<application name="BridgeAdd" language="en_US">
		<synopsis>
			Join a bridge that contains the specified channel.
		</synopsis>
		<syntax>
			<parameter name="name">
				<para>Name of the channel in an existing bridge
				</para>
			</parameter>
		</syntax>
		<description>
			<para>This application places the incoming channel into
			the bridge containing the specified channel. The specified
			channel only needs to be the prefix of a full channel name
			IE. 'SIP/cisco0001'.
			</para>
		</description>
	</application>
 ***/

static const char app[] = "BridgeAdd";

static int bridgeadd_exec(struct ast_channel *chan, const char *data)
{
	struct ast_channel *c_ref;
	struct ast_bridge_features chan_features;
	struct ast_bridge *bridge;
	char *c_name;

	/* Answer the channel if needed */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (!(c_ref = ast_channel_get_by_name_prefix(data, strlen(data)))) {
		ast_log(LOG_WARNING, "Channel %s not found\n", data);
		return -1;
	}

	c_name = ast_strdupa(ast_channel_name(c_ref));

	ast_channel_lock(c_ref);
	bridge = ast_channel_get_bridge(c_ref);
	ast_channel_unlock(c_ref);

	ast_channel_unref(c_ref);

	if (!bridge) {
		ast_log(LOG_WARNING, "Channel %s is not in a bridge\n", c_name);
		return -1;
	}

	ast_verb(3, "%s is joining %s in bridge %s\n", ast_channel_name(chan),
		c_name, bridge->uniqueid);

	if (ast_bridge_features_init(&chan_features)
		|| ast_bridge_join(bridge, chan, NULL, &chan_features, NULL, 0)) {

		ast_log(LOG_WARNING, "%s failed to join %s in bridge %s\n", ast_channel_name(chan),
			 c_name, bridge->uniqueid);

		ast_bridge_features_cleanup(&chan_features);
		ao2_cleanup(bridge);
		return -1;
	}

	ast_bridge_features_cleanup(&chan_features);
	ao2_cleanup(bridge);
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, bridgeadd_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Bridge Add Channel Application");
