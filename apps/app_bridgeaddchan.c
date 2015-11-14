/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

ASTERISK_REGISTER_FILE()

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/features.h"

/*** DOCUMENTATION
	<application name="BridgeAdd" language="en_US">
		<synopsis>
			Join a bridge that contains the specified bridged channel.
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
	struct ast_bridge_channel *bridge_channel;

	if (!(c_ref = ast_channel_get_by_name_prefix(data, strlen(data)))) {
		ast_log(LOG_NOTICE, "Channel %s not found\n", data);
		return -1;
	}

	bridge_channel = ast_channel_get_bridge_channel(c_ref);

	if (!bridge_channel || !bridge_channel->bridge) {
		ast_log(LOG_NOTICE, "no bridge\n");
		ast_channel_unref(c_ref);
		return -1;
	}

	if ( ast_bridge_add_channel( bridge_channel->bridge, chan, NULL, 0, NULL ) ) {
		ast_log(LOG_NOTICE, "Failed to join bridge\n");
		ao2_cleanup(bridge_channel->bridge);
		ast_channel_unref(c_ref);
		return -1;
	}

	ao2_cleanup(bridge_channel->bridge);
	ast_channel_unref(c_ref);
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
