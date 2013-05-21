/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Built in bridging interval features
 *
 * \author Jonathan Rose <jrose@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$REVISION: 381278 $")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/test.h"

#include "asterisk/say.h"
#include "asterisk/stringfields.h"
#include "asterisk/musiconhold.h"

static int bridge_features_duration_callback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_limits *limits = hook_pvt;

	if (!ast_strlen_zero(limits->duration_sound)) {
		ast_stream_and_wait(bridge_channel->chan, limits->duration_sound, AST_DIGIT_NONE);
	}

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END);

	ast_test_suite_event_notify("BRIDGE_TIMELIMIT", "Channel1: %s", ast_channel_name(bridge_channel->chan));
	return -1;
}

static void limits_interval_playback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_bridge_features_limits *limits, const char *file)
{
	if (!strcasecmp(file, "timeleft")) {
		unsigned int remaining = ast_tvdiff_ms(limits->quitting_time, ast_tvnow()) / 1000;
		unsigned int min;
		unsigned int sec;

		if (remaining <= 0) {
			return;
		}

		if ((remaining / 60) > 1) {
			min = remaining / 60;
			sec = remaining % 60;
		} else {
			min = 0;
			sec = remaining;
		}

		ast_stream_and_wait(bridge_channel->chan, "vm-youhave", AST_DIGIT_NONE);
		if (min) {
			ast_say_number(bridge_channel->chan, min, AST_DIGIT_NONE,
				ast_channel_language(bridge_channel->chan), NULL);
			ast_stream_and_wait(bridge_channel->chan, "queue-minutes", AST_DIGIT_NONE);
		}
		if (sec) {
			ast_say_number(bridge_channel->chan, sec, AST_DIGIT_NONE,
				ast_channel_language(bridge_channel->chan), NULL);
			ast_stream_and_wait(bridge_channel->chan, "queue-seconds", AST_DIGIT_NONE);
		}
	} else {
		ast_stream_and_wait(bridge_channel->chan, file, AST_DIGIT_NONE);
	}

	/*
	 * It may be necessary to resume music on hold after we finish
	 * playing the announcment.
	 *
	 * XXX We have no idea what MOH class was in use before playing
	 * the file.
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_MOH)) {
		ast_moh_start(bridge_channel->chan, NULL, NULL);
	}
}

static int bridge_features_connect_callback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_limits *limits = hook_pvt;

	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		return -1;
	}

	limits_interval_playback(bridge, bridge_channel, limits, limits->connect_sound);
	return -1;
}

static int bridge_features_warning_callback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_limits *limits = hook_pvt;

	if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/* If we aren't in the wait state, something more important than this warning is happening and we should skip it. */
		limits_interval_playback(bridge, bridge_channel, limits, limits->warning_sound);
	}

	return !limits->frequency ? -1 : limits->frequency;
}

static void copy_bridge_features_limits(struct ast_bridge_features_limits *dst, struct ast_bridge_features_limits *src)
{
	dst->duration = src->duration;
	dst->warning = src->warning;
	dst->frequency = src->frequency;
	dst->quitting_time = src->quitting_time;

	ast_string_field_set(dst, duration_sound, src->duration_sound);
	ast_string_field_set(dst, warning_sound, src->warning_sound);
	ast_string_field_set(dst, connect_sound, src->connect_sound);
}

static int bridge_builtin_set_limits(struct ast_bridge_features *features, struct ast_bridge_features_limits *limits, int remove_on_pull)
{
	struct ast_bridge_features_limits *feature_limits;

	if (!limits->duration) {
		return -1;
	}

	if (features->limits) {
		ast_log(LOG_ERROR, "Tried to apply limits to a feature set that already has limits.\n");
		return -1;
	}

	feature_limits = ast_malloc(sizeof(*feature_limits));
	if (!feature_limits) {
		return -1;
	}

	if (ast_bridge_features_limits_construct(feature_limits)) {
		return -1;
	}

	copy_bridge_features_limits(feature_limits, limits);
	features->limits = feature_limits;

/* BUGBUG feature interval hooks need to be reimplemented to be more stand alone. */
	if (ast_bridge_interval_hook(features, feature_limits->duration,
		bridge_features_duration_callback, feature_limits, NULL, remove_on_pull)) {
		ast_log(LOG_ERROR, "Failed to schedule the duration limiter to the bridge channel.\n");
		return -1;
	}

	feature_limits->quitting_time = ast_tvadd(ast_tvnow(), ast_samp2tv(feature_limits->duration, 1000));

	if (!ast_strlen_zero(feature_limits->connect_sound)) {
		if (ast_bridge_interval_hook(features, 1,
			bridge_features_connect_callback, feature_limits, NULL, remove_on_pull)) {
			ast_log(LOG_WARNING, "Failed to schedule connect sound to the bridge channel.\n");
		}
	}

	if (feature_limits->warning && feature_limits->warning < feature_limits->duration) {
		if (ast_bridge_interval_hook(features, feature_limits->duration - feature_limits->warning,
			bridge_features_warning_callback, feature_limits, NULL, remove_on_pull)) {
			ast_log(LOG_WARNING, "Failed to schedule warning sound playback to the bridge channel.\n");
		}
	}

	return 0;
}

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	ast_bridge_interval_register(AST_BRIDGE_BUILTIN_INTERVAL_LIMITS, bridge_builtin_set_limits);

	/* Bump up our reference count so we can't be unloaded. */
	ast_module_ref(ast_module_info->self);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Built in bridging interval features");
