/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
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

/*! \file
 *
 * \brief Built in bridging features
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<use type="module">res_monitor</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/frame.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/parking.h"
#include "asterisk/features_config.h"
#include "asterisk/monitor.h"
#include "asterisk/mixmonitor.h"
#include "asterisk/audiohook.h"
#include "asterisk/causes.h"

enum set_touch_variables_res {
	SET_TOUCH_SUCCESS,
	SET_TOUCH_UNSET,
	SET_TOUCH_ALLOC_FAILURE,
};

static void set_touch_variable(enum set_touch_variables_res *res, struct ast_channel *chan, const char *var_name, char **touch)
{
	const char *c_touch;

	if (*res == SET_TOUCH_ALLOC_FAILURE) {
		return;
	}
	c_touch = pbx_builtin_getvar_helper(chan, var_name);
	if (!ast_strlen_zero(c_touch)) {
		*touch = ast_strdup(c_touch);
		if (!*touch) {
			*res = SET_TOUCH_ALLOC_FAILURE;
		} else {
			*res = SET_TOUCH_SUCCESS;
		}
	}
}

static enum set_touch_variables_res set_touch_variables(struct ast_channel *chan, int is_mixmonitor, char **touch_format, char **touch_monitor, char **touch_monitor_prefix)
{
	enum set_touch_variables_res res = SET_TOUCH_UNSET;
	const char *var_format;
	const char *var_monitor;
	const char *var_prefix;

	SCOPED_CHANNELLOCK(lock, chan);

	if (is_mixmonitor) {
		var_format = "TOUCH_MIXMONITOR_FORMAT";
		var_monitor = "TOUCH_MIXMONITOR";
		var_prefix = "TOUCH_MIXMONITOR_PREFIX";
	} else {
		var_format = "TOUCH_MONITOR_FORMAT";
		var_monitor = "TOUCH_MONITOR";
		var_prefix = "TOUCH_MONITOR_PREFIX";
	}
	set_touch_variable(&res, chan, var_format, touch_format);
	set_touch_variable(&res, chan, var_monitor, touch_monitor);
	set_touch_variable(&res, chan, var_prefix, touch_monitor_prefix);

	return res;
}

static void stop_automonitor(struct ast_bridge_channel *bridge_channel, struct ast_channel *peer_chan, struct ast_features_general_config *features_cfg, const char *stop_message)
{
	ast_verb(4, "AutoMonitor used to stop recording call.\n");

	ast_channel_lock(peer_chan);
	if (ast_channel_monitor(peer_chan)) {
		if (ast_channel_monitor(peer_chan)->stop(peer_chan, 1)) {
			ast_verb(4, "Cannot stop AutoMonitor for %s\n", ast_channel_name(bridge_channel->chan));
			if (features_cfg && !(ast_strlen_zero(features_cfg->recordingfailsound))) {
				ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->recordingfailsound, NULL);
			}
			ast_channel_unlock(peer_chan);
			return;
		}
	} else {
		/* Something else removed the Monitor before we got to it. */
		ast_channel_unlock(peer_chan);
		return;
	}

	ast_channel_unlock(peer_chan);

	if (features_cfg && !(ast_strlen_zero(features_cfg->courtesytone))) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
	}

	if (!ast_strlen_zero(stop_message)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, stop_message, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, stop_message, NULL);
	}
}

static void start_automonitor(struct ast_bridge_channel *bridge_channel, struct ast_channel *peer_chan, struct ast_features_general_config *features_cfg, const char *start_message)
{
	char *touch_filename;
	size_t len;
	int x;
	enum set_touch_variables_res set_touch_res;

	RAII_VAR(char *, touch_format, NULL, ast_free);
	RAII_VAR(char *, touch_monitor, NULL, ast_free);
	RAII_VAR(char *, touch_monitor_prefix, NULL, ast_free);

	set_touch_res = set_touch_variables(bridge_channel->chan, 0, &touch_format,
		&touch_monitor, &touch_monitor_prefix);
	switch (set_touch_res) {
	case SET_TOUCH_SUCCESS:
		break;
	case SET_TOUCH_UNSET:
		set_touch_res = set_touch_variables(peer_chan, 0, &touch_format, &touch_monitor,
			&touch_monitor_prefix);
		if (set_touch_res == SET_TOUCH_ALLOC_FAILURE) {
			return;
		}
		break;
	case SET_TOUCH_ALLOC_FAILURE:
		return;
	}

	if (!ast_strlen_zero(touch_monitor)) {
		len = strlen(touch_monitor) + 50;
		touch_filename = ast_alloca(len);
		snprintf(touch_filename, len, "%s-%ld-%s",
			S_OR(touch_monitor_prefix, "auto"),
			(long) time(NULL),
			touch_monitor);
	} else {
		char *caller_chan_id;
		char *peer_chan_id;

		caller_chan_id = ast_strdupa(S_COR(ast_channel_caller(bridge_channel->chan)->id.number.valid,
			ast_channel_caller(bridge_channel->chan)->id.number.str, ast_channel_name(bridge_channel->chan)));
		peer_chan_id = ast_strdupa(S_COR(ast_channel_caller(peer_chan)->id.number.valid,
			ast_channel_caller(peer_chan)->id.number.str, ast_channel_name(peer_chan)));
		len = strlen(caller_chan_id) + strlen(peer_chan_id) + 50;
		touch_filename = ast_alloca(len);
		snprintf(touch_filename, len, "%s-%ld-%s-%s",
			S_OR(touch_monitor_prefix, "auto"),
			(long) time(NULL),
			caller_chan_id,
			peer_chan_id);
	}

	for (x = 0; x < strlen(touch_filename); x++) {
		if (touch_filename[x] == '/') {
			touch_filename[x] = '-';
		}
	}

	ast_verb(4, "AutoMonitor used to record call. Filename: %s\n", touch_filename);

	if (ast_monitor_start(peer_chan, touch_format, touch_filename, 1, X_REC_IN | X_REC_OUT, NULL)) {
		ast_verb(4, "AutoMonitor feature was tried by '%s' but monitor failed to start.\n",
			ast_channel_name(bridge_channel->chan));
		return;
	}

	ast_monitor_setjoinfiles(peer_chan, 1);

	if (features_cfg && !ast_strlen_zero(features_cfg->courtesytone)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
	}

	if (!ast_strlen_zero(start_message)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, start_message, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, start_message, NULL);
	}

	pbx_builtin_setvar_helper(bridge_channel->chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
	pbx_builtin_setvar_helper(peer_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
}

static int feature_automonitor(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	const char *start_message;
	const char *stop_message;
	struct ast_bridge_features_automonitor *options = hook_pvt;
	enum ast_bridge_features_monitor start_stop = options ? options->start_stop : AUTO_MONITOR_TOGGLE;
	int is_monitoring;

	RAII_VAR(struct ast_channel *, peer_chan, NULL, ast_channel_cleanup);
	RAII_VAR(struct ast_features_general_config *, features_cfg, NULL, ao2_cleanup);

	ast_channel_lock(bridge_channel->chan);
	features_cfg = ast_get_chan_features_general_config(bridge_channel->chan);
	ast_channel_unlock(bridge_channel->chan);
	ast_bridge_channel_lock_bridge(bridge_channel);
	peer_chan = ast_bridge_peer_nolock(bridge_channel->bridge, bridge_channel->chan);
	ast_bridge_unlock(bridge_channel->bridge);

	if (!peer_chan) {
		ast_verb(4, "Cannot start AutoMonitor for %s - can not determine peer in bridge.\n",
			ast_channel_name(bridge_channel->chan));
		if (features_cfg && !ast_strlen_zero(features_cfg->recordingfailsound)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->recordingfailsound, NULL);
		}
		return 0;
	}

	ast_channel_lock(bridge_channel->chan);
	start_message = pbx_builtin_getvar_helper(bridge_channel->chan,
		"TOUCH_MONITOR_MESSAGE_START");
	start_message = ast_strdupa(S_OR(start_message, ""));
	stop_message = pbx_builtin_getvar_helper(bridge_channel->chan,
		"TOUCH_MONITOR_MESSAGE_STOP");
	stop_message = ast_strdupa(S_OR(stop_message, ""));
	ast_channel_unlock(bridge_channel->chan);

	is_monitoring = ast_channel_monitor(peer_chan) != NULL;
	switch (start_stop) {
	case AUTO_MONITOR_TOGGLE:
		if (is_monitoring) {
			stop_automonitor(bridge_channel, peer_chan, features_cfg, stop_message);
		} else {
			start_automonitor(bridge_channel, peer_chan, features_cfg, start_message);
		}
		return 0;
	case AUTO_MONITOR_START:
		if (!is_monitoring) {
			start_automonitor(bridge_channel, peer_chan, features_cfg, start_message);
			return 0;
		}
		ast_verb(4, "AutoMonitor already recording call.\n");
		break;
	case AUTO_MONITOR_STOP:
		if (is_monitoring) {
			stop_automonitor(bridge_channel, peer_chan, features_cfg, stop_message);
			return 0;
		}
		ast_verb(4, "AutoMonitor already stopped on call.\n");
		break;
	}

	/*
	 * Fake start/stop to invoker so will think it did something but
	 * was already in that mode.
	 */
	if (features_cfg && !ast_strlen_zero(features_cfg->courtesytone)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
	}
	if (is_monitoring) {
		if (!ast_strlen_zero(start_message)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, start_message, NULL);
		}
	} else {
		if (!ast_strlen_zero(stop_message)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, stop_message, NULL);
		}
	}
	return 0;
}

static void stop_automixmonitor(struct ast_bridge_channel *bridge_channel, struct ast_channel *peer_chan, struct ast_features_general_config *features_cfg, const char *stop_message)
{
	ast_verb(4, "AutoMixMonitor used to stop recording call.\n");

	if (ast_stop_mixmonitor(peer_chan, NULL)) {
		ast_verb(4, "Failed to stop AutoMixMonitor for %s.\n", ast_channel_name(bridge_channel->chan));
		if (features_cfg && !(ast_strlen_zero(features_cfg->recordingfailsound))) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->recordingfailsound, NULL);
		}
		return;
	}

	if (features_cfg && !ast_strlen_zero(features_cfg->courtesytone)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
	}

	if (!ast_strlen_zero(stop_message)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, stop_message, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, stop_message, NULL);
	}
}

static void start_automixmonitor(struct ast_bridge_channel *bridge_channel, struct ast_channel *peer_chan, struct ast_features_general_config *features_cfg, const char *start_message)
{
	char *touch_filename;
	size_t len;
	int x;
	enum set_touch_variables_res set_touch_res;

	RAII_VAR(char *, touch_format, NULL, ast_free);
	RAII_VAR(char *, touch_monitor, NULL, ast_free);
	RAII_VAR(char *, touch_monitor_prefix, NULL, ast_free);

	set_touch_res = set_touch_variables(bridge_channel->chan, 1, &touch_format,
		&touch_monitor, &touch_monitor_prefix);
	switch (set_touch_res) {
	case SET_TOUCH_SUCCESS:
		break;
	case SET_TOUCH_UNSET:
		set_touch_res = set_touch_variables(peer_chan, 1, &touch_format, &touch_monitor,
			&touch_monitor_prefix);
		if (set_touch_res == SET_TOUCH_ALLOC_FAILURE) {
			return;
		}
		break;
	case SET_TOUCH_ALLOC_FAILURE:
		return;
	}

	if (!ast_strlen_zero(touch_monitor)) {
		len = strlen(touch_monitor) + 50;
		touch_filename = ast_alloca(len);
		snprintf(touch_filename, len, "%s-%ld-%s.%s",
			S_OR(touch_monitor_prefix, "auto"),
			(long) time(NULL),
			touch_monitor,
			S_OR(touch_format, "wav"));
	} else {
		char *caller_chan_id;
		char *peer_chan_id;

		caller_chan_id = ast_strdupa(S_COR(ast_channel_caller(bridge_channel->chan)->id.number.valid,
			ast_channel_caller(bridge_channel->chan)->id.number.str, ast_channel_name(bridge_channel->chan)));
		peer_chan_id = ast_strdupa(S_COR(ast_channel_caller(peer_chan)->id.number.valid,
			ast_channel_caller(peer_chan)->id.number.str, ast_channel_name(peer_chan)));
		len = strlen(caller_chan_id) + strlen(peer_chan_id) + 50;
		touch_filename = ast_alloca(len);
		snprintf(touch_filename, len, "%s-%ld-%s-%s.%s",
			S_OR(touch_monitor_prefix, "auto"),
			(long) time(NULL),
			caller_chan_id,
			peer_chan_id,
			S_OR(touch_format, "wav"));
	}

	for (x = 0; x < strlen(touch_filename); x++) {
		if (touch_filename[x] == '/') {
			touch_filename[x] = '-';
		}
	}

	ast_verb(4, "AutoMixMonitor used to record call. Filename: %s\n", touch_filename);

	if (ast_start_mixmonitor(peer_chan, touch_filename, "b")) {
		ast_verb(4, "AutoMixMonitor feature was tried by '%s' but MixMonitor failed to start.\n",
			ast_channel_name(bridge_channel->chan));

		if (features_cfg && !ast_strlen_zero(features_cfg->recordingfailsound)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->recordingfailsound, NULL);
		}
		return;
	}

	if (features_cfg && !ast_strlen_zero(features_cfg->courtesytone)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
	}

	if (!ast_strlen_zero(start_message)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, start_message, NULL);
		ast_bridge_channel_write_playfile(bridge_channel, NULL, start_message, NULL);
	}

	pbx_builtin_setvar_helper(bridge_channel->chan, "TOUCH_MIXMONITOR_OUTPUT", touch_filename);
	pbx_builtin_setvar_helper(peer_chan, "TOUCH_MIXMONITOR_OUTPUT", touch_filename);
}

static int feature_automixmonitor(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	static const char *mixmonitor_spy_type = "MixMonitor";
	const char *stop_message;
	const char *start_message;
	struct ast_bridge_features_automixmonitor *options = hook_pvt;
	enum ast_bridge_features_monitor start_stop = options ? options->start_stop : AUTO_MONITOR_TOGGLE;
	int is_monitoring;

	RAII_VAR(struct ast_channel *, peer_chan, NULL, ast_channel_cleanup);
	RAII_VAR(struct ast_features_general_config *, features_cfg, NULL, ao2_cleanup);

	ast_channel_lock(bridge_channel->chan);
	features_cfg = ast_get_chan_features_general_config(bridge_channel->chan);
	ast_channel_unlock(bridge_channel->chan);
	ast_bridge_channel_lock_bridge(bridge_channel);
	peer_chan = ast_bridge_peer_nolock(bridge_channel->bridge, bridge_channel->chan);
	ast_bridge_unlock(bridge_channel->bridge);

	if (!peer_chan) {
		ast_verb(4, "Cannot start AutoMixMonitor for %s - cannot determine peer in bridge.\n",
			ast_channel_name(bridge_channel->chan));
		if (features_cfg && !ast_strlen_zero(features_cfg->recordingfailsound)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->recordingfailsound, NULL);
		}
		return 0;
	}

	ast_channel_lock(bridge_channel->chan);
	start_message = pbx_builtin_getvar_helper(bridge_channel->chan,
		"TOUCH_MIXMONITOR_MESSAGE_START");
	start_message = ast_strdupa(S_OR(start_message, ""));
	stop_message = pbx_builtin_getvar_helper(bridge_channel->chan,
		"TOUCH_MIXMONITOR_MESSAGE_STOP");
	stop_message = ast_strdupa(S_OR(stop_message, ""));
	ast_channel_unlock(bridge_channel->chan);

	is_monitoring =
		0 < ast_channel_audiohook_count_by_source(peer_chan, mixmonitor_spy_type, AST_AUDIOHOOK_TYPE_SPY);
	switch (start_stop) {
	case AUTO_MONITOR_TOGGLE:
		if (is_monitoring) {
			stop_automixmonitor(bridge_channel, peer_chan, features_cfg, stop_message);
		} else {
			start_automixmonitor(bridge_channel, peer_chan, features_cfg, start_message);
		}
		return 0;
	case AUTO_MONITOR_START:
		if (!is_monitoring) {
			start_automixmonitor(bridge_channel, peer_chan, features_cfg, start_message);
			return 0;
		}
		ast_verb(4, "AutoMixMonitor already recording call.\n");
		break;
	case AUTO_MONITOR_STOP:
		if (is_monitoring) {
			stop_automixmonitor(bridge_channel, peer_chan, features_cfg, stop_message);
			return 0;
		}
		ast_verb(4, "AutoMixMonitor already stopped on call.\n");
		break;
	}

	/*
	 * Fake start/stop to invoker so will think it did something but
	 * was already in that mode.
	 */
	if (features_cfg && !ast_strlen_zero(features_cfg->courtesytone)) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, features_cfg->courtesytone, NULL);
	}
	if (is_monitoring) {
		if (!ast_strlen_zero(start_message)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, start_message, NULL);
		}
	} else {
		if (!ast_strlen_zero(stop_message)) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, stop_message, NULL);
		}
	}
	return 0;
}

/*! \brief Internal built in feature for hangup */
static int feature_hangup(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	/*
	 * This is very simple, we simply change the state on the
	 * bridge_channel to force the channel out of the bridge and the
	 * core takes care of the rest.
	 */
	ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
		AST_CAUSE_NORMAL_CLEARING);
	return 0;
}

static int unload_module(void)
{
	ast_bridge_features_unregister(AST_BRIDGE_BUILTIN_HANGUP);
	ast_bridge_features_unregister(AST_BRIDGE_BUILTIN_AUTOMON);
	ast_bridge_features_unregister(AST_BRIDGE_BUILTIN_AUTOMIXMON);

	return 0;
}

static int load_module(void)
{
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_HANGUP, feature_hangup, NULL);
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_AUTOMON, feature_automonitor, NULL);
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_AUTOMIXMON, feature_automixmonitor, NULL);

	/* This module cannot be unloaded until shutdown */
	ast_module_shutdown_ref(ast_module_info->self);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Built in bridging features",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.optional_modules = "res_monitor",
);
