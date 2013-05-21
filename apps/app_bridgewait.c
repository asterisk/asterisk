/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Author: Jonathan Rose <jrose@digium.com>
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
 * \brief Application to place the channel into a holding Bridge
 *
 * \author Jonathan Rose <jrose@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>bridge_holding</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/features.h"
#include "asterisk/say.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/bridging.h"
#include "asterisk/musiconhold.h"

/*** DOCUMENTATION
	<application name="BridgeWait" language="en_US">
		<synopsis>
			Put a call into the holding bridge.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="A">
						<para>The channel will join the holding bridge as an
						announcer</para>
					</option>
					<option name="m">
						<argument name="class" required="false" />
						<para>Play music on hold to the entering channel while it is
						on hold. If the <emphasis>class</emphasis> is included, then
						that class of music on hold will take priority over the
						channel default.</para>
					</option>
					<option name="r">
						<para>Play a ringing tone to the entering channel while it is
						on hold.</para>
					</option>
					<option name="S">
						<argument name="duration" required="true" />
						<para>Automatically end the hold and return to the PBX after
						<emphasis>duration</emphasis> seconds.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application places the incoming channel into a holding bridge.
			The channel will then wait in the holding bridge until some
			event occurs which removes it from the holding bridge.</para>
		</description>
	</application>
 ***/
/* BUGBUG Add bridge name/id parameter to specify which holding bridge to join (required) */
/* BUGBUG Add h(moh-class) option to put channel on hold using AST_CONTROL_HOLD/AST_CONTROL_UNHOLD while in bridge */
/* BUGBUG Add s option to send silence media frames to channel while in bridge (uses a silence generator) */
/* BUGBUG Add n option to send no media to channel while in bridge (Channel should not be answered yet) */
/* BUGBUG The channel may or may not be answered with the r option. */
/* BUGBUG You should not place an announcer into a holding bridge with unanswered channels. */
/* BUGBUG Not supplying any option flags will assume the m option with the default music class. */

static char *app = "BridgeWait";
static struct ast_bridge *holding_bridge;

AST_MUTEX_DEFINE_STATIC(bridgewait_lock);

enum bridgewait_flags {
	MUXFLAG_PLAYMOH = (1 << 0),
	MUXFLAG_RINGING = (1 << 1),
	MUXFLAG_TIMEOUT = (1 << 2),
	MUXFLAG_ANNOUNCER = (1 << 3),
};

enum bridgewait_args {
	OPT_ARG_MOHCLASS,
	OPT_ARG_TIMEOUT,
	OPT_ARG_ARRAY_SIZE, /* Always the last element of the enum */
};

AST_APP_OPTIONS(bridgewait_opts, {
	AST_APP_OPTION('A', MUXFLAG_ANNOUNCER),
	AST_APP_OPTION('r', MUXFLAG_RINGING),
	AST_APP_OPTION_ARG('m', MUXFLAG_PLAYMOH, OPT_ARG_MOHCLASS),
	AST_APP_OPTION_ARG('S', MUXFLAG_TIMEOUT, OPT_ARG_TIMEOUT),
});

static int apply_option_timeout(struct ast_bridge_features *features, char *duration_arg)
{
	struct ast_bridge_features_limits hold_limits;

	if (ast_strlen_zero(duration_arg)) {
		ast_log(LOG_ERROR, "No duration value provided for the timeout ('S') option.\n");
		return -1;
	}

	if (ast_bridge_features_limits_construct(&hold_limits)) {
		ast_log(LOG_ERROR, "Could not construct duration limits. Bridge canceled.\n");
		return -1;
	}

	if (sscanf(duration_arg, "%u", &(hold_limits.duration)) != 1 || hold_limits.duration == 0) {
		ast_log(LOG_ERROR, "Duration value provided for the timeout ('S') option must be greater than 0\n");
		ast_bridge_features_limits_destroy(&hold_limits);
		return -1;
	}

	/* Limits struct holds time as milliseconds, so muliply 1000x */
	hold_limits.duration *= 1000;
	ast_bridge_features_set_limits(features, &hold_limits, 1 /* remove_on_pull */);
	ast_bridge_features_limits_destroy(&hold_limits);

	return 0;
}

static void apply_option_moh(struct ast_channel *chan, char *class_arg)
{
	ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "musiconhold");
	ast_channel_set_bridge_role_option(chan, "holding_participant", "moh_class", class_arg);
}

static void apply_option_ringing(struct ast_channel *chan)
{
	ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "ringing");
}

static int process_options(struct ast_channel *chan, struct ast_flags *flags, char **opts, struct ast_bridge_features *features)
{
	if (ast_test_flag(flags, MUXFLAG_TIMEOUT)) {
		if (apply_option_timeout(features, opts[OPT_ARG_TIMEOUT])) {
			return -1;
		}
	}

	if (ast_test_flag(flags, MUXFLAG_ANNOUNCER)) {
		/* Announcer specific stuff */
		ast_channel_add_bridge_role(chan, "announcer");
	} else {
		/* Non Announcer specific stuff */
		ast_channel_add_bridge_role(chan, "holding_participant");

		if (ast_test_flag(flags, MUXFLAG_PLAYMOH)) {
			apply_option_moh(chan, opts[OPT_ARG_MOHCLASS]);
		} else if (ast_test_flag(flags, MUXFLAG_RINGING)) {
			apply_option_ringing(chan);
		}
	}

	return 0;
}

static int bridgewait_exec(struct ast_channel *chan, const char *data)
{
	struct ast_bridge_features chan_features;
	struct ast_flags flags = { 0 };
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
		AST_APP_ARG(other);		/* Any remaining unused arguments */
	);

	ast_mutex_lock(&bridgewait_lock);
	if (!holding_bridge) {
		holding_bridge = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_HOLDING,
			AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM
				| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_PROHIBITED);
	}
	ast_mutex_unlock(&bridgewait_lock);
	if (!holding_bridge) {
		ast_log(LOG_ERROR, "Could not create holding bridge for '%s'.\n", ast_channel_name(chan));
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_bridge_features_init(&chan_features)) {
		ast_bridge_features_cleanup(&chan_features);
		return -1;
	}

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };
		ast_app_parse_options(bridgewait_opts, &flags, opts, args.options);
		if (process_options(chan, &flags, opts, &chan_features)) {
			ast_bridge_features_cleanup(&chan_features);
			return -1;
		}
	}

	ast_bridge_join(holding_bridge, chan, NULL, &chan_features, NULL, 0);

	ast_bridge_features_cleanup(&chan_features);
	return ast_check_hangup_locked(chan) ? -1 : 0;
}

static int unload_module(void)
{
	ao2_cleanup(holding_bridge);
	holding_bridge = NULL;

	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, bridgewait_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Place the channel into a holding bridge application");
