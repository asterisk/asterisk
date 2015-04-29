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

ASTERISK_REGISTER_FILE()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/test.h"
#include "asterisk/say.h"
#include "asterisk/stringfields.h"
#include "asterisk/musiconhold.h"
#include "asterisk/causes.h"

static int bridge_features_duration_callback(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_limits *limits = hook_pvt;

	if (!ast_strlen_zero(limits->duration_sound)) {
		ast_stream_and_wait(bridge_channel->chan, limits->duration_sound, AST_DIGIT_NONE);
	}

	ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
		AST_CAUSE_NORMAL_CLEARING);

	ast_test_suite_event_notify("BRIDGE_TIMELIMIT", "Channel1: %s",
		ast_channel_name(bridge_channel->chan));
	return -1;
}

static void limits_interval_playback(struct ast_bridge_channel *bridge_channel, struct ast_bridge_features_limits *limits, const char *file)
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
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_MOH)) {
		const char *latest_musicclass;

		ast_channel_lock(bridge_channel->chan);
		latest_musicclass = ast_strdupa(ast_channel_latest_musicclass(bridge_channel->chan));
		ast_channel_unlock(bridge_channel->chan);
		ast_moh_start(bridge_channel->chan, latest_musicclass, NULL);
	}
}

static int bridge_features_connect_callback(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_limits *limits = hook_pvt;

	limits_interval_playback(bridge_channel, limits, limits->connect_sound);
	return -1;
}

static int bridge_features_warning_callback(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_limits *limits = hook_pvt;

	limits_interval_playback(bridge_channel, limits, limits->warning_sound);
	return limits->frequency ?: -1;
}

static void bridge_features_limits_copy(struct ast_bridge_features_limits *dst, struct ast_bridge_features_limits *src)
{
	ast_string_fields_copy(dst, src);
	dst->quitting_time = src->quitting_time;
	dst->duration = src->duration;
	dst->warning = src->warning;
	dst->frequency = src->frequency;
}

static void bridge_features_limits_dtor(void *vdoomed)
{
	struct ast_bridge_features_limits *doomed = vdoomed;

	ast_bridge_features_limits_destroy(doomed);
}

static int bridge_builtin_set_limits(struct ast_bridge_features *features,
	struct ast_bridge_features_limits *limits,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	RAII_VAR(struct ast_bridge_features_limits *, feature_limits, NULL, ao2_cleanup);

	if (!limits->duration) {
		return -1;
	}

	/* Create limits hook_pvt data. */
	feature_limits = ao2_alloc_options(sizeof(*feature_limits),
		bridge_features_limits_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!feature_limits) {
		return -1;
	}
	if (ast_bridge_features_limits_construct(feature_limits)) {
		return -1;
	}
	bridge_features_limits_copy(feature_limits, limits);
	feature_limits->quitting_time = ast_tvadd(ast_tvnow(),
		ast_samp2tv(feature_limits->duration, 1000));

	/* Install limit hooks. */
	ao2_ref(feature_limits, +1);
	if (ast_bridge_interval_hook(features, AST_BRIDGE_HOOK_TIMER_OPTION_MEDIA,
		feature_limits->duration,
		bridge_features_duration_callback, feature_limits, __ao2_cleanup, remove_flags)) {
		ast_log(LOG_ERROR, "Failed to schedule the duration limiter to the bridge channel.\n");
		ao2_ref(feature_limits, -1);
		return -1;
	}
	if (!ast_strlen_zero(feature_limits->connect_sound)) {
		ao2_ref(feature_limits, +1);
		if (ast_bridge_interval_hook(features, AST_BRIDGE_HOOK_TIMER_OPTION_MEDIA, 1,
			bridge_features_connect_callback, feature_limits, __ao2_cleanup, remove_flags)) {
			ast_log(LOG_WARNING, "Failed to schedule connect sound to the bridge channel.\n");
			ao2_ref(feature_limits, -1);
		}
	}
	if (feature_limits->warning && feature_limits->warning < feature_limits->duration) {
		ao2_ref(feature_limits, +1);
		if (ast_bridge_interval_hook(features, AST_BRIDGE_HOOK_TIMER_OPTION_MEDIA,
			feature_limits->duration - feature_limits->warning,
			bridge_features_warning_callback, feature_limits, __ao2_cleanup, remove_flags)) {
			ast_log(LOG_WARNING, "Failed to schedule warning sound playback to the bridge channel.\n");
			ao2_ref(feature_limits, -1);
		}
	}

	return 0;
}

static void unload_module(void)
{
	ast_bridge_interval_unregister(AST_BRIDGE_BUILTIN_INTERVAL_LIMITS);
	ast_module_block_unload(AST_MODULE_SELF);
}

static int load_module(void)
{
	return ast_bridge_interval_register(AST_BRIDGE_BUILTIN_INTERVAL_LIMITS,
		bridge_builtin_set_limits);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Built in bridging interval features");
