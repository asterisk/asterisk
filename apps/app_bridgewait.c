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
			<parameter name="role" required="false">
				<para>Defines the channel's purpose for entering the holding bridge. Values are case sensitive.
				</para>
				<enumlist>
					<enum name="participant">
						<para>The channel will enter the holding bridge to be placed on hold
						until it is removed from the bridge for some reason. (default)</para>
					</enum>
					<enum name="announcer">
						<para>The channel will enter the holding bridge to make announcements
						to channels that are currently in the holding bridge. While an
						announcer is present, holding for the participants will be
						suspended.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="m">
						<argument name="class" required="true" />
						<para>The specified MOH class will be used/suggested for
						music on hold operations. This option will only be useful for
						entertainment modes that use it (m and h).</para>
					</option>
					<option name="e">
						<para>Which entertainment mechanism should be used while on hold
						in the holding bridge. Only the first letter is read.</para>
						<enumlist>
							<enum name="m"><para>Play music on hold (default)</para></enum>
							<enum name="r"><para>Ring without pause</para></enum>
							<enum name="s"><para>Generate silent audio</para></enum>
							<enum name="h"><para>Put the channel on hold</para></enum>
							<enum name="n"><para>No entertainment</para></enum>
						</enumlist>
					</option>
					<option name="S">
						<argument name="duration" required="true" />
						<para>Automatically exit the bridge and return to the PBX after
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
/* BUGBUG The channel may or may not be answered with the r option. */
/* BUGBUG You should not place an announcer into a holding bridge with unanswered channels. */

static char *app = "BridgeWait";
static struct ast_bridge *holding_bridge;

AST_MUTEX_DEFINE_STATIC(bridgewait_lock);

enum bridgewait_flags {
	MUXFLAG_MOHCLASS = (1 << 0),
	MUXFLAG_ENTERTAINMENT = (1 << 1),
	MUXFLAG_TIMEOUT = (1 << 2),
};

enum bridgewait_args {
	OPT_ARG_ENTERTAINMENT,
	OPT_ARG_MOHCLASS,
	OPT_ARG_TIMEOUT,
	OPT_ARG_ARRAY_SIZE, /* Always the last element of the enum */
};

AST_APP_OPTIONS(bridgewait_opts, {
	AST_APP_OPTION_ARG('e', MUXFLAG_ENTERTAINMENT, OPT_ARG_ENTERTAINMENT),
	AST_APP_OPTION_ARG('m', MUXFLAG_MOHCLASS, OPT_ARG_MOHCLASS),
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
	ast_bridge_features_set_limits(features, &hold_limits, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
	ast_bridge_features_limits_destroy(&hold_limits);

	return 0;
}

static int apply_option_moh(struct ast_channel *chan, const char *class_arg)
{
	return ast_channel_set_bridge_role_option(chan, "holding_participant", "moh_class", class_arg);
}

static int apply_option_entertainment(struct ast_channel *chan, const char *entertainment_arg)
{
	char entertainment = entertainment_arg[0];
	switch (entertainment) {
	case 'm':
		return ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "musiconhold");
	case 'r':
		return ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "ringing");
	case 's':
		return ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "silence");
	case 'h':
		return ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "hold");
	case 'n':
		return ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "none");
	default:
		ast_log(LOG_ERROR, "Invalid argument for BridgeWait entertainment '%s'\n", entertainment_arg);
		return -1;
	}
}

enum wait_bridge_roles {
	ROLE_PARTICIPANT = 0,
	ROLE_ANNOUNCER,
	ROLE_INVALID,
};

static int process_options(struct ast_channel *chan, struct ast_flags *flags, char **opts, struct ast_bridge_features *features, enum wait_bridge_roles role)
{
	if (ast_test_flag(flags, MUXFLAG_TIMEOUT)) {
		if (apply_option_timeout(features, opts[OPT_ARG_TIMEOUT])) {
			return -1;
		}
	}

	switch (role) {
	case ROLE_PARTICIPANT:
		if (ast_channel_add_bridge_role(chan, "holding_participant")) {
			return -1;
		}

		if (ast_test_flag(flags, MUXFLAG_MOHCLASS)) {
			if (apply_option_moh(chan, opts[OPT_ARG_MOHCLASS])) {
				return -1;
			}
		}

		if (ast_test_flag(flags, MUXFLAG_ENTERTAINMENT)) {
			if (apply_option_entertainment(chan, opts[OPT_ARG_ENTERTAINMENT])) {
				return -1;
			}
		}

		break;
	case ROLE_ANNOUNCER:
		if (ast_channel_add_bridge_role(chan, "announcer")) {
			return -1;
		}
		break;
	case ROLE_INVALID:
		ast_assert(0);
		return -1;
	}

	return 0;
}

static enum wait_bridge_roles validate_role(const char *role)
{
	if (!strcmp(role, "participant")) {
		return ROLE_PARTICIPANT;
	} else if (!strcmp(role, "announcer")) {
		return ROLE_ANNOUNCER;
	} else {
		return ROLE_INVALID;
	}
}

static int bridgewait_exec(struct ast_channel *chan, const char *data)
{
	struct ast_bridge_features chan_features;
	struct ast_flags flags = { 0 };
	char *parse;
	enum wait_bridge_roles role = ROLE_PARTICIPANT;
	char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(role);
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

	if (!ast_strlen_zero(args.role)) {
		role = validate_role(args.role);
		if (role == ROLE_INVALID) {
			ast_log(LOG_ERROR, "Requested waiting bridge role '%s' is invalid.\n", args.role);
			return -1;
		}
	}

	if (ast_bridge_features_init(&chan_features)) {
		ast_bridge_features_cleanup(&chan_features);
		return -1;
	}

	if (args.options) {
		ast_app_parse_options(bridgewait_opts, &flags, opts, args.options);
	}

	if (process_options(chan, &flags, opts, &chan_features, role)) {
		ast_bridge_features_cleanup(&chan_features);
		return -1;
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
