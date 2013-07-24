/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
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
 * \brief Bridging Channel API
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Richard Mudgett <rmudgett@digium.com>
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <signal.h>

#include "asterisk/heap.h"
#include "asterisk/astobj2.h"
#include "asterisk/stringfields.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/timing.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_channel.h"
#include "asterisk/bridging_channel_internal.h"
#include "asterisk/bridging_internal.h"
#include "asterisk/stasis_bridging.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/musiconhold.h"
#include "asterisk/features_config.h"
#include "asterisk/parking.h"

/*!
 * \brief Used to queue an action frame onto a bridge channel and write an action frame into a bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel work with.
 * \param action Type of bridge action frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
typedef int (*ast_bridge_channel_post_action_data)(struct ast_bridge_channel *bridge_channel, enum bridge_channel_action_type action, const void *data, size_t datalen);

struct ast_bridge *ast_bridge_channel_merge_inhibit(struct ast_bridge_channel *bridge_channel, int request)
{
	struct ast_bridge *bridge;

	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge = bridge_channel->bridge;
	ao2_ref(bridge, +1);
	bridge_merge_inhibit_nolock(bridge, request);
	ast_bridge_unlock(bridge);
	return bridge;
}

void ast_bridge_channel_lock_bridge(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge;

	for (;;) {
		/* Safely get the bridge pointer */
		ast_bridge_channel_lock(bridge_channel);
		bridge = bridge_channel->bridge;
		ao2_ref(bridge, +1);
		ast_bridge_channel_unlock(bridge_channel);

		/* Lock the bridge and see if it is still the bridge we need to lock. */
		ast_bridge_lock(bridge);
		if (bridge == bridge_channel->bridge) {
			ao2_ref(bridge, -1);
			return;
		}
		ast_bridge_unlock(bridge);
		ao2_ref(bridge, -1);
	}
}

static void bridge_channel_poke(struct ast_bridge_channel *bridge_channel)
{
	if (!pthread_equal(pthread_self(), bridge_channel->thread)) {
		while (bridge_channel->waiting) {
			pthread_kill(bridge_channel->thread, SIGURG);
			sched_yield();
		}
	}
}

void ast_bridge_channel_update_accountcodes(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *other = NULL;

	AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
		if (other == swap) {
			continue;
		}

		if (!ast_strlen_zero(ast_channel_accountcode(bridge_channel->chan)) && ast_strlen_zero(ast_channel_peeraccount(other->chan))) {
			ast_debug(1, "Setting peeraccount to %s for %s from data on channel %s\n",
					ast_channel_accountcode(bridge_channel->chan), ast_channel_name(other->chan), ast_channel_name(bridge_channel->chan));
			ast_channel_peeraccount_set(other->chan, ast_channel_accountcode(bridge_channel->chan));
		}
		if (!ast_strlen_zero(ast_channel_accountcode(other->chan)) && ast_strlen_zero(ast_channel_peeraccount(bridge_channel->chan))) {
			ast_debug(1, "Setting peeraccount to %s for %s from data on channel %s\n",
					ast_channel_accountcode(other->chan), ast_channel_name(bridge_channel->chan), ast_channel_name(other->chan));
			ast_channel_peeraccount_set(bridge_channel->chan, ast_channel_accountcode(other->chan));
		}
		if (!ast_strlen_zero(ast_channel_peeraccount(bridge_channel->chan)) && ast_strlen_zero(ast_channel_accountcode(other->chan))) {
			ast_debug(1, "Setting accountcode to %s for %s from data on channel %s\n",
					ast_channel_peeraccount(bridge_channel->chan), ast_channel_name(other->chan), ast_channel_name(bridge_channel->chan));
			ast_channel_accountcode_set(other->chan, ast_channel_peeraccount(bridge_channel->chan));
		}
		if (!ast_strlen_zero(ast_channel_peeraccount(other->chan)) && ast_strlen_zero(ast_channel_accountcode(bridge_channel->chan))) {
			ast_debug(1, "Setting accountcode to %s for %s from data on channel %s\n",
					ast_channel_peeraccount(other->chan), ast_channel_name(bridge_channel->chan), ast_channel_name(other->chan));
			ast_channel_accountcode_set(bridge_channel->chan, ast_channel_peeraccount(other->chan));
		}
		if (bridge->num_channels == 2) {
			if (strcmp(ast_channel_accountcode(bridge_channel->chan), ast_channel_peeraccount(other->chan))) {
				ast_debug(1, "Changing peeraccount from %s to %s on %s to match channel %s\n",
						ast_channel_peeraccount(other->chan), ast_channel_peeraccount(bridge_channel->chan), ast_channel_name(other->chan), ast_channel_name(bridge_channel->chan));
				ast_channel_peeraccount_set(other->chan, ast_channel_accountcode(bridge_channel->chan));
			}
			if (strcmp(ast_channel_accountcode(other->chan), ast_channel_peeraccount(bridge_channel->chan))) {
				ast_debug(1, "Changing peeraccount from %s to %s on %s to match channel %s\n",
						ast_channel_peeraccount(bridge_channel->chan), ast_channel_peeraccount(other->chan), ast_channel_name(bridge_channel->chan), ast_channel_name(other->chan));
				ast_channel_peeraccount_set(bridge_channel->chan, ast_channel_accountcode(other->chan));
			}
		}
	}
}

void ast_bridge_channel_update_linkedids(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct ast_bridge_channel *other = NULL;
	struct ast_bridge *bridge = bridge_channel->bridge;
	const char *oldest_linkedid = ast_channel_linkedid(bridge_channel->chan);

	AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
		if (other == swap) {
			continue;
		}
		oldest_linkedid = ast_channel_oldest_linkedid(oldest_linkedid, ast_channel_linkedid(other->chan));
	}

	if (ast_strlen_zero(oldest_linkedid)) {
		return;
	}

	ast_channel_linkedid_set(bridge_channel->chan, oldest_linkedid);
	AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
		if (other == swap) {
			continue;
		}
		ast_channel_linkedid_set(other->chan, oldest_linkedid);
	}
}

int ast_bridge_channel_queue_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr)
{
	struct ast_frame *dup;
	char nudge = 0;

	if (bridge_channel->suspended
		/* Also defer DTMF frames. */
		&& fr->frametype != AST_FRAME_DTMF_BEGIN
		&& fr->frametype != AST_FRAME_DTMF_END
		&& !ast_is_deferrable_frame(fr)) {
		/* Drop non-deferable frames when suspended. */
		return 0;
	}
	if (fr->frametype == AST_FRAME_NULL) {
		/* "Accept" the frame and discard it. */
		return 0;
	}

	dup = ast_frdup(fr);
	if (!dup) {
		return -1;
	}

	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/* Drop frames on channels leaving the bridge. */
		ast_bridge_channel_unlock(bridge_channel);
		ast_frfree(dup);
		return 0;
	}

	AST_LIST_INSERT_TAIL(&bridge_channel->wr_queue, dup, frame_list);
	if (write(bridge_channel->alert_pipe[1], &nudge, sizeof(nudge)) != sizeof(nudge)) {
		ast_log(LOG_ERROR, "We couldn't write alert pipe for %p(%s)... something is VERY wrong\n",
			bridge_channel, ast_channel_name(bridge_channel->chan));
	}
	ast_bridge_channel_unlock(bridge_channel);
	return 0;
}

/*!
 * \brief Queue an action frame onto the bridge channel with data.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to queue the frame onto.
 * \param action Type of bridge action frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int ast_bridge_channel_queue_action_data(struct ast_bridge_channel *bridge_channel, enum bridge_channel_action_type action, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = action,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return ast_bridge_channel_queue_frame(bridge_channel, &frame);
}

int ast_bridge_channel_queue_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = control,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return ast_bridge_channel_queue_frame(bridge_channel, &frame);
}

int ast_bridge_queue_everyone_else(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *cur;
	int not_written = -1;

	if (frame->frametype == AST_FRAME_NULL) {
		/* "Accept" the frame and discard it. */
		return 0;
	}

	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (cur == bridge_channel) {
			continue;
		}
		if (!ast_bridge_channel_queue_frame(cur, frame)) {
			not_written = 0;
		}
	}
	return not_written;
}

void ast_bridge_channel_restore_formats(struct ast_bridge_channel *bridge_channel)
{
	/* Restore original formats of the channel as they came in */
	if (ast_format_cmp(ast_channel_readformat(bridge_channel->chan), &bridge_channel->read_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p(%s) to read format %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&bridge_channel->read_format));
		if (ast_set_read_format(bridge_channel->chan, &bridge_channel->read_format)) {
			ast_debug(1, "Bridge failed to return %p(%s) to read format %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				ast_getformatname(&bridge_channel->read_format));
		}
	}
	if (ast_format_cmp(ast_channel_writeformat(bridge_channel->chan), &bridge_channel->write_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p(%s) to write format %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&bridge_channel->write_format));
		if (ast_set_write_format(bridge_channel->chan, &bridge_channel->write_format)) {
			ast_debug(1, "Bridge failed to return %p(%s) to write format %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				ast_getformatname(&bridge_channel->write_format));
		}
	}
}

static int bridge_channel_write_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
/*
 * BUGBUG need to implement a deferred write queue for when there is no peer channel in the bridge (yet or it was kicked).
 *
 * The tech decides if a frame needs to be pushed back for deferral.
 * simple_bridge/native_bridge are likely the only techs that will do this.
 */
	bridge_channel->bridge->technology->write(bridge_channel->bridge, bridge_channel, frame);
	ast_bridge_unlock(bridge_channel->bridge);

	/*
	 * Claim successful write to bridge.  If deferred frame
	 * support is added, claim successfully deferred.
	 */
	return 0;
}

static int ast_bridge_channel_write_action_data(struct ast_bridge_channel *bridge_channel, enum bridge_channel_action_type action, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = action,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return bridge_channel_write_frame(bridge_channel, &frame);
}

int ast_bridge_channel_write_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = control,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return bridge_channel_write_frame(bridge_channel, &frame);
}

int ast_bridge_channel_write_hold(struct ast_bridge_channel *bridge_channel, const char *moh_class)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	size_t datalen;

	if (!ast_strlen_zero(moh_class)) {
		datalen = strlen(moh_class) + 1;

		blob = ast_json_pack("{s: s}",
			"musicclass", moh_class);
	} else {
		moh_class = NULL;
		datalen = 0;
	}

	ast_channel_publish_blob(bridge_channel->chan, ast_channel_hold_type(), blob);
	return ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_HOLD,
		moh_class, datalen);
}

int ast_bridge_channel_write_unhold(struct ast_bridge_channel *bridge_channel)
{
	ast_channel_publish_blob(bridge_channel->chan, ast_channel_unhold_type(), NULL);
	return ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_UNHOLD, NULL, 0);
}

static int run_app_helper(struct ast_channel *chan, const char *app_name, const char *app_args)
{
	int res = 0;

	if (!strcasecmp("Gosub", app_name)) {
		ast_app_exec_sub(NULL, chan, app_args, 0);
	} else if (!strcasecmp("Macro", app_name)) {
		ast_app_exec_macro(NULL, chan, app_args);
	} else {
		struct ast_app *app;

		app = pbx_findapp(app_name);
		if (!app) {
			ast_log(LOG_WARNING, "Could not find application (%s)\n", app_name);
		} else {
			res = pbx_exec(chan, app, app_args);
		}
	}
	return res;
}

/*!
* \internal
* \brief Handle bridge hangup event.
* \since 12.0.0
*
* \param bridge_channel Which channel is hanging up.
*
* \return Nothing
*/
static void bridge_channel_handle_hangup(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any hangup hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		int remove_me;

		if (hook->type != AST_BRIDGE_HOOK_TYPE_HANGUP) {
			continue;
		}
		remove_me = hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);
		if (remove_me) {
			ast_debug(1, "Hangup hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->other_hooks, hook);
		}
	}
	ao2_iterator_destroy(&iter);

	/* Default hangup action. */
	ast_bridge_channel_leave_bridge(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END);
}

void ast_bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	if (moh_class) {
		ast_bridge_channel_write_hold(bridge_channel, moh_class);
	}
	if (run_app_helper(bridge_channel->chan, app_name, S_OR(app_args, ""))) {
		/* Break the bridge if the app returns non-zero. */
		bridge_channel_handle_hangup(bridge_channel);
	}
	if (moh_class) {
		ast_bridge_channel_write_unhold(bridge_channel);
	}
}


struct bridge_run_app {
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

/*!
 * \internal
 * \brief Handle the run application bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to run the application on.
 * \param data Action frame data to run the application.
 *
 * \return Nothing
 */
static void bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, struct bridge_run_app *data)
{
	ast_bridge_channel_run_app(bridge_channel, data->app_name,
		data->app_args_offset ? &data->app_name[data->app_args_offset] : NULL,
		data->moh_offset ? &data->app_name[data->moh_offset] : NULL);
}

static int payload_helper_app(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	struct bridge_run_app *app_data;
	size_t len_name = strlen(app_name) + 1;
	size_t len_args = ast_strlen_zero(app_args) ? 0 : strlen(app_args) + 1;
	size_t len_moh = !moh_class ? 0 : strlen(moh_class) + 1;
	size_t len_data = sizeof(*app_data) + len_name + len_args + len_moh;

	/* Fill in application run frame data. */
	app_data = alloca(len_data);
	app_data->app_args_offset = len_args ? len_name : 0;
	app_data->moh_offset = len_moh ? len_name + len_args : 0;
	strcpy(app_data->app_name, app_name);/* Safe */
	if (len_args) {
		strcpy(&app_data->app_name[app_data->app_args_offset], app_args);/* Safe */
	}
	if (moh_class) {
		strcpy(&app_data->app_name[app_data->moh_offset], moh_class);/* Safe */
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_RUN_APP, app_data, len_data);
}

int ast_bridge_channel_write_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	return payload_helper_app(ast_bridge_channel_write_action_data,
		bridge_channel, app_name, app_args, moh_class);
}

int ast_bridge_channel_queue_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	return payload_helper_app(ast_bridge_channel_queue_action_data,
		bridge_channel, app_name, app_args, moh_class);
}

void ast_bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	if (moh_class) {
		ast_bridge_channel_write_hold(bridge_channel, moh_class);
	}
	if (custom_play) {
		custom_play(bridge_channel, playfile);
	} else {
		ast_stream_and_wait(bridge_channel->chan, playfile, AST_DIGIT_NONE);
	}
	if (moh_class) {
		ast_bridge_channel_write_unhold(bridge_channel);
	}

	/*
	 * It may be necessary to resume music on hold after we finish
	 * playing the announcment.
	 *
	 * XXX We have no idea what MOH class was in use before playing
	 * the file. This method also fails to restore ringing indications.
	 * the proposed solution is to create a resume_entertainment callback
	 * for the bridge technology and execute it here.
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_MOH)) {
		ast_moh_start(bridge_channel->chan, NULL, NULL);
	}
}

struct bridge_playfile {
	/*! Call this function to play the playfile. (NULL if normal sound file to play) */
	ast_bridge_custom_play_fn custom_play;
	/*! Offset into playfile[] where the MOH class name starts.  (zero if no MOH)*/
	int moh_offset;
	/*! Filename to play. */
	char playfile[0];
};

/*!
 * \internal
 * \brief Handle the playfile bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to play a file on.
 * \param payload Action frame payload to play a file.
 *
 * \return Nothing
 */
static void bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, struct bridge_playfile *payload)
{
	ast_bridge_channel_playfile(bridge_channel, payload->custom_play, payload->playfile,
		payload->moh_offset ? &payload->playfile[payload->moh_offset] : NULL);
}

static int payload_helper_playfile(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	struct bridge_playfile *payload;
	size_t len_name = strlen(playfile) + 1;
	size_t len_moh = !moh_class ? 0 : strlen(moh_class) + 1;
	size_t len_payload = sizeof(*payload) + len_name + len_moh;

	/* Fill in play file frame data. */
	payload = alloca(len_payload);
	payload->custom_play = custom_play;
	payload->moh_offset = len_moh ? len_name : 0;
	strcpy(payload->playfile, playfile);/* Safe */
	if (moh_class) {
		strcpy(&payload->playfile[payload->moh_offset], moh_class);/* Safe */
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_PLAY_FILE, payload, len_payload);
}

int ast_bridge_channel_write_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	return payload_helper_playfile(ast_bridge_channel_write_action_data,
		bridge_channel, custom_play, playfile, moh_class);
}

int ast_bridge_channel_queue_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	return payload_helper_playfile(ast_bridge_channel_queue_action_data,
		bridge_channel, custom_play, playfile, moh_class);
}

struct bridge_custom_callback {
	/*! Call this function on the bridge channel thread. */
	ast_bridge_custom_callback_fn callback;
	/*! Size of the payload if it exists.  A number otherwise. */
	size_t payload_size;
	/*! Nonzero if the payload exists. */
	char payload_exists;
	/*! Payload to give to callback. */
	char payload[0];
};

/*!
 * \internal
 * \brief Handle the do custom callback bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to run the application on.
 * \param data Action frame data to run the application.
 *
 * \return Nothing
 */
static void bridge_channel_do_callback(struct ast_bridge_channel *bridge_channel, struct bridge_custom_callback *data)
{
	data->callback(bridge_channel, data->payload_exists ? data->payload : NULL, data->payload_size);
}

static int payload_helper_cb(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	struct bridge_custom_callback *cb_data;
	size_t len_data = sizeof(*cb_data) + (payload ? payload_size : 0);

	/* Sanity check. */
	if (!callback) {
		ast_assert(0);
		return -1;
	}

	/* Fill in custom callback frame data. */
	cb_data = alloca(len_data);
	cb_data->callback = callback;
	cb_data->payload_size = payload_size;
	cb_data->payload_exists = payload && payload_size;
	if (cb_data->payload_exists) {
		memcpy(cb_data->payload, payload, payload_size);/* Safe */
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_CALLBACK, cb_data, len_data);
}

int ast_bridge_channel_write_callback(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	return payload_helper_cb(ast_bridge_channel_write_action_data,
		bridge_channel, callback, payload, payload_size);
}

int ast_bridge_channel_queue_callback(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	return payload_helper_cb(ast_bridge_channel_queue_action_data,
		bridge_channel, callback, payload, payload_size);
}

struct bridge_park {
	int parker_uuid_offset;
	int app_data_offset;
	/* buffer used for holding those strings */
	char parkee_uuid[0];
};

static void bridge_channel_park(struct ast_bridge_channel *bridge_channel, struct bridge_park *payload)
{
	ast_bridge_channel_park(bridge_channel, payload->parkee_uuid,
		&payload->parkee_uuid[payload->parker_uuid_offset],
		payload->app_data_offset ? &payload->parkee_uuid[payload->app_data_offset] : NULL);
}

static int payload_helper_park(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel,
	const char *parkee_uuid,
	const char *parker_uuid,
	const char *app_data)
{
	struct bridge_park *payload;
	size_t len_parkee_uuid = strlen(parkee_uuid) + 1;
	size_t len_parker_uuid = strlen(parker_uuid) + 1;
	size_t len_app_data = !app_data ? 0 : strlen(app_data) + 1;
	size_t len_payload = sizeof(*payload) + len_parker_uuid + len_parkee_uuid + len_app_data;

	payload = alloca(len_payload);
	payload->app_data_offset = len_app_data ? len_parkee_uuid + len_parker_uuid : 0;
	payload->parker_uuid_offset = len_parkee_uuid;
	strcpy(payload->parkee_uuid, parkee_uuid);
	strcpy(&payload->parkee_uuid[payload->parker_uuid_offset], parker_uuid);
	if (app_data) {
		strcpy(&payload->parkee_uuid[payload->app_data_offset], app_data);
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_PARK, payload, len_payload);
}

int ast_bridge_channel_write_park(struct ast_bridge_channel *bridge_channel, const char *parkee_uuid, const char *parker_uuid, const char *app_data)
{
	return payload_helper_park(ast_bridge_channel_write_action_data,
		bridge_channel, parkee_uuid, parker_uuid, app_data);
}

int ast_bridge_notify_talking(struct ast_bridge_channel *bridge_channel, int started_talking)
{
	struct ast_frame action = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = started_talking
			? BRIDGE_CHANNEL_ACTION_TALKING_START : BRIDGE_CHANNEL_ACTION_TALKING_STOP,
	};

	return ast_bridge_channel_queue_frame(bridge_channel, &action);
}

struct ast_bridge_channel *ast_bridge_channel_peer(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *other = NULL;

	if (bridge_channel->in_bridge && bridge->num_channels == 2) {
		AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
			if (other != bridge_channel) {
				break;
			}
		}
	}

	return other;
}

struct ast_bridge_channel *bridge_find_channel(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (bridge_channel->chan == chan) {
			break;
		}
	}

	return bridge_channel;
}

/*!
 * \internal
 * \brief Suspend a channel from a bridge.
 *
 * \param bridge_channel Channel to suspend.
 *
 * \note This function assumes bridge_channel->bridge is locked.
 *
 * \return Nothing
 */
void bridge_channel_suspend_nolock(struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 1;
	if (bridge_channel->in_bridge) {
		--bridge_channel->bridge->num_active;
	}

	/* Get technology bridge threads off of the channel. */
	if (bridge_channel->bridge->technology->suspend) {
		bridge_channel->bridge->technology->suspend(bridge_channel->bridge, bridge_channel);
	}
}

/*!
 * \internal
 * \brief Suspend a channel from a bridge.
 *
 * \param bridge_channel Channel to suspend.
 *
 * \return Nothing
 */
static void bridge_channel_suspend(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge_channel_suspend_nolock(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

/*!
 * \internal
 * \brief Unsuspend a channel from a bridge.
 *
 * \param bridge_channel Channel to unsuspend.
 *
 * \note This function assumes bridge_channel->bridge is locked.
 *
 * \return Nothing
 */
void bridge_channel_unsuspend_nolock(struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 0;
	if (bridge_channel->in_bridge) {
		++bridge_channel->bridge->num_active;
	}

	/* Wake technology bridge threads to take care of channel again. */
	if (bridge_channel->bridge->technology->unsuspend) {
		bridge_channel->bridge->technology->unsuspend(bridge_channel->bridge, bridge_channel);
	}

	/* Wake suspended channel. */
	ast_bridge_channel_lock(bridge_channel);
	ast_cond_signal(&bridge_channel->cond);
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Unsuspend a channel from a bridge.
 *
 * \param bridge_channel Channel to unsuspend.
 *
 * \return Nothing
 */
static void bridge_channel_unsuspend(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge_channel_unsuspend_nolock(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

/*!
 * \internal
 * \brief Handle bridge channel interval expiration.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to run expired intervals on.
 *
 * \return Nothing
 */
static void bridge_channel_handle_interval(struct ast_bridge_channel *bridge_channel)
{
	struct ast_heap *interval_hooks;
	struct ast_bridge_hook_timer *hook;
	struct timeval start;
	int hook_run = 0;

	interval_hooks = bridge_channel->features->interval_hooks;
	ast_heap_wrlock(interval_hooks);
	start = ast_tvnow();
	while ((hook = ast_heap_peek(interval_hooks, 1))) {
		int interval;
		unsigned int execution_time;

		if (ast_tvdiff_ms(hook->timer.trip_time, start) > 0) {
			ast_debug(1, "Hook %p on %p(%s) wants to happen in the future, stopping our traversal\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		ao2_ref(hook, +1);
		ast_heap_unlock(interval_hooks);

		if (!hook_run) {
			hook_run = 1;
			bridge_channel_suspend(bridge_channel);
			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		}

		ast_debug(1, "Executing hook %p on %p(%s)\n",
			hook, bridge_channel, ast_channel_name(bridge_channel->chan));
		interval = hook->generic.callback(bridge_channel->bridge, bridge_channel,
			hook->generic.hook_pvt);

		ast_heap_wrlock(interval_hooks);
		if (ast_heap_peek(interval_hooks, hook->timer.heap_index) != hook
			|| !ast_heap_remove(interval_hooks, hook)) {
			/* Interval hook is already removed from the bridge_channel. */
			ao2_ref(hook, -1);
			continue;
		}
		ao2_ref(hook, -1);

		if (interval < 0) {
			ast_debug(1, "Removed interval hook %p from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_ref(hook, -1);
			continue;
		}
		if (interval) {
			/* Set new interval for the hook. */
			hook->timer.interval = interval;
		}

		ast_debug(1, "Updating interval hook %p with interval %u on %p(%s)\n",
			hook, hook->timer.interval, bridge_channel,
			ast_channel_name(bridge_channel->chan));

		/* resetting start */
		start = ast_tvnow();

		/*
		 * Resetup the interval hook for the next interval.  We may need
		 * to skip over any missed intervals because the hook was
		 * delayed or took too long.
		 */
		execution_time = ast_tvdiff_ms(start, hook->timer.trip_time);
		while (hook->timer.interval < execution_time) {
			execution_time -= hook->timer.interval;
		}
		hook->timer.trip_time = ast_tvadd(start, ast_samp2tv(hook->timer.interval - execution_time, 1000));
		hook->timer.seqno = ast_atomic_fetchadd_int((int *) &bridge_channel->features->interval_sequence, +1);

		if (ast_heap_push(interval_hooks, hook)) {
			/* Could not push the hook back onto the heap. */
			ao2_ref(hook, -1);
		}
	}
	ast_heap_unlock(interval_hooks);

	if (hook_run) {
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
}

static int bridge_channel_write_dtmf_stream(struct ast_bridge_channel *bridge_channel, const char *dtmf)
{
	return ast_bridge_channel_write_action_data(bridge_channel,
		BRIDGE_CHANNEL_ACTION_DTMF_STREAM, dtmf, strlen(dtmf) + 1);
}

/*!
 * \brief Internal function that executes a feature on a bridge channel
 * \note Neither the bridge nor the bridge_channel locks should be held when entering
 * this function.
 */
static void bridge_channel_feature(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook_dtmf *hook = NULL;
	char dtmf[MAXIMUM_DTMF_FEATURE_STRING] = "";
	size_t dtmf_len = 0;
	unsigned int digit_timeout;
	RAII_VAR(struct ast_features_general_config *, gen_cfg, NULL, ao2_cleanup);

	ast_channel_lock(bridge_channel->chan);
	gen_cfg = ast_get_chan_features_general_config(bridge_channel->chan);
	if (!gen_cfg) {
		ast_log(LOG_ERROR, "Unable to retrieve features configuration.\n");
		ast_channel_unlock(bridge_channel->chan);
		return;
	}
	digit_timeout = gen_cfg->featuredigittimeout;
	ast_channel_unlock(bridge_channel->chan);

	/* The channel is now under our control and we don't really want any begin frames to do our DTMF matching so disable 'em at the core level */
	ast_set_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_END_DTMF_ONLY);

	/* Wait for DTMF on the channel and put it into a buffer. If the buffer matches any feature hook execute the hook. */
	do {
		int res;

		/* If the above timed out simply exit */
		res = ast_waitfordigit(bridge_channel->chan, digit_timeout);
		if (!res) {
			ast_debug(1, "DTMF feature string collection on %p(%s) timed out\n",
				bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		if (res < 0) {
			ast_debug(1, "DTMF feature string collection failed on %p(%s) for some reason\n",
				bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}

/* BUGBUG need to record the duration of DTMF digits so when the string is played back, they are reproduced. */
		/* Add the above DTMF into the DTMF string so we can do our matching */
		dtmf[dtmf_len++] = res;
		ast_debug(1, "DTMF feature string on %p(%s) is now '%s'\n",
			bridge_channel, ast_channel_name(bridge_channel->chan), dtmf);

		/* See if a DTMF feature hook matches or can match */
		hook = ao2_find(features->dtmf_hooks, dtmf, OBJ_PARTIAL_KEY);
		if (!hook) {
			ast_debug(1, "No DTMF feature hooks on %p(%s) match '%s'\n",
				bridge_channel, ast_channel_name(bridge_channel->chan), dtmf);
			break;
		}
		if (strlen(hook->dtmf.code) == dtmf_len) {
			ast_debug(1, "DTMF feature hook %p matched DTMF string '%s' on %p(%s)\n",
				hook, dtmf, bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		ao2_ref(hook, -1);
		hook = NULL;

		/* Stop if we have reached the maximum length of a DTMF feature string. */
	} while (dtmf_len < ARRAY_LEN(dtmf) - 1);

	/* Since we are done bringing DTMF in return to using both begin and end frames */
	ast_clear_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_END_DTMF_ONLY);

	/* If a hook was actually matched execute it on this channel, otherwise stream up the DTMF to the other channels */
	if (hook) {
		int remove_me;

		remove_me = hook->generic.callback(bridge_channel->bridge, bridge_channel,
			hook->generic.hook_pvt);
		if (remove_me) {
			ast_debug(1, "DTMF hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->dtmf_hooks, hook);
		}
		ao2_ref(hook, -1);

		/*
		 * If we are handing the channel off to an external hook for
		 * ownership, we are not guaranteed what kind of state it will
		 * come back in.  If the channel hungup, we need to detect that
		 * here if the hook did not already change the state.
		 */
		if (bridge_channel->chan && ast_check_hangup_locked(bridge_channel->chan)) {
			bridge_channel_handle_hangup(bridge_channel);
		}
	} else if (features->dtmf_passthrough) {
		bridge_channel_write_dtmf_stream(bridge_channel, dtmf);
	}
}

static void bridge_channel_talking(struct ast_bridge_channel *bridge_channel, int talking)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any talk detection hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		int remove_me;
		ast_bridge_talking_indicate_callback talk_cb;

		if (hook->type != AST_BRIDGE_HOOK_TYPE_TALK) {
			continue;
		}
		talk_cb = (ast_bridge_talking_indicate_callback) hook->callback;
		remove_me = talk_cb(bridge_channel, hook->hook_pvt, talking);
		if (remove_me) {
			ast_debug(1, "Talk detection hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->other_hooks, hook);
		}
	}
	ao2_iterator_destroy(&iter);
}

/*! \brief Internal function that plays back DTMF on a bridge channel */
static void bridge_channel_dtmf_stream(struct ast_bridge_channel *bridge_channel, const char *dtmf)
{
	ast_debug(1, "Playing DTMF stream '%s' out to %p(%s)\n",
		dtmf, bridge_channel, ast_channel_name(bridge_channel->chan));
	ast_dtmf_stream(bridge_channel->chan, NULL, dtmf, 0, 0);
}

static void bridge_channel_blind_transfer(struct ast_bridge_channel *bridge_channel,
		struct blind_transfer_data *blind_data)
{
	ast_async_goto(bridge_channel->chan, blind_data->context, blind_data->exten, 1);
	bridge_channel_handle_hangup(bridge_channel);
}

static void after_bridge_move_channel(struct ast_channel *chan_bridged, void *data)
{
	RAII_VAR(struct ast_channel *, chan_target, data, ao2_cleanup);
	struct ast_party_connected_line connected_target;
	unsigned char connected_line_data[1024];
	int payload_size;

	ast_party_connected_line_init(&connected_target);

	ast_channel_lock(chan_target);
	ast_party_connected_line_copy(&connected_target, ast_channel_connected(chan_target));
	ast_channel_unlock(chan_target);
	ast_party_id_reset(&connected_target.priv);

	if (ast_channel_move(chan_target, chan_bridged)) {
		ast_softhangup(chan_target, AST_SOFTHANGUP_DEV);
		ast_party_connected_line_free(&connected_target);
		return;
	}

	if ((payload_size = ast_connected_line_build_data(connected_line_data,
		sizeof(connected_line_data), &connected_target, NULL)) != -1) {
		struct ast_control_read_action_payload *frame_payload;
		int frame_size;

		frame_size = payload_size + sizeof(*frame_payload);
		frame_payload = ast_alloca(frame_size);
		frame_payload->action = AST_FRAME_READ_ACTION_CONNECTED_LINE_MACRO;
		frame_payload->payload_size = payload_size;
		memcpy(frame_payload->payload, connected_line_data, payload_size);
		ast_queue_control_data(chan_target, AST_CONTROL_READ_ACTION, frame_payload, frame_size);
	}

	ast_party_connected_line_free(&connected_target);
}

static void after_bridge_move_channel_fail(enum ast_after_bridge_cb_reason reason, void *data)
{
	RAII_VAR(struct ast_channel *, chan_target, data, ao2_cleanup);

	ast_log(LOG_WARNING, "Unable to complete transfer: %s\n",
		ast_after_bridge_cb_reason_string(reason));
	ast_softhangup(chan_target, AST_SOFTHANGUP_DEV);
}

static void bridge_channel_attended_transfer(struct ast_bridge_channel *bridge_channel,
		const char *target_chan_name)
{
	RAII_VAR(struct ast_channel *, chan_target, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan_bridged, NULL, ao2_cleanup);

	chan_target = ast_channel_get_by_name(target_chan_name);
	if (!chan_target) {
		/* Dang, it disappeared somehow */
		bridge_channel_handle_hangup(bridge_channel);
		return;
	}

	ast_bridge_channel_lock(bridge_channel);
	chan_bridged = bridge_channel->chan;
	ast_assert(chan_bridged != NULL);
	ao2_ref(chan_bridged, +1);
	ast_bridge_channel_unlock(bridge_channel);

	if (ast_after_bridge_callback_set(chan_bridged, after_bridge_move_channel,
		after_bridge_move_channel_fail, ast_channel_ref(chan_target))) {
		ast_softhangup(chan_target, AST_SOFTHANGUP_DEV);

		/* Release the ref we tried to pass to ast_after_bridge_callback_set(). */
		ast_channel_unref(chan_target);
	}
	bridge_channel_handle_hangup(bridge_channel);
}

/*!
 * \internal
 * \brief Handle bridge channel bridge action frame.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to execute the action on.
 * \param action What to do.
 *
 * \return Nothing
 */
static void bridge_channel_handle_action(struct ast_bridge_channel *bridge_channel, struct ast_frame *action)
{
	switch (action->subclass.integer) {
	case BRIDGE_CHANNEL_ACTION_FEATURE:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_feature(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_DTMF_STREAM:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_dtmf_stream(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_TALKING_START:
	case BRIDGE_CHANNEL_ACTION_TALKING_STOP:
		bridge_channel_talking(bridge_channel,
			action->subclass.integer == BRIDGE_CHANNEL_ACTION_TALKING_START);
		break;
	case BRIDGE_CHANNEL_ACTION_PLAY_FILE:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_playfile(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_RUN_APP:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_run_app(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_CALLBACK:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_do_callback(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_PARK:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_park(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_BLIND_TRANSFER:
		bridge_channel_blind_transfer(bridge_channel, action->data.ptr);
		break;
	case BRIDGE_CHANNEL_ACTION_ATTENDED_TRANSFER:
		bridge_channel_attended_transfer(bridge_channel, action->data.ptr);
		break;
	default:
		break;
	}
}

/*!
 * \internal
 * \brief Check if a bridge should dissolve and do it.
 * \since 12.0.0
 *
 * \param bridge_channel Channel causing the check.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_dissolve_check(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;

	if (bridge->dissolved) {
		return;
	}

	if (!bridge->num_channels
		&& ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_EMPTY)) {
		/* Last channel leaving the bridge turns off the lights. */
		bridge_dissolve(bridge);
		return;
	}

	switch (bridge_channel->state) {
	case AST_BRIDGE_CHANNEL_STATE_END:
		/* Do we need to dissolve the bridge because this channel hung up? */
		if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_HANGUP)
			|| (bridge_channel->features->usable
				&& ast_test_flag(&bridge_channel->features->feature_flags,
					AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP))) {
			bridge_dissolve(bridge);
			return;
		}
		break;
	default:
		break;
	}
/* BUGBUG need to implement AST_BRIDGE_CHANNEL_FLAG_LONELY support here */
}

/*!
 * \internal
 * \brief Pull the bridge channel out of its current bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to pull.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \return Nothing
 */
void bridge_channel_pull(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;

	if (!bridge_channel->in_bridge) {
		return;
	}
	bridge_channel->in_bridge = 0;

	ast_debug(1, "Bridge %s: pulling %p(%s)\n",
		bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));

	ast_verb(3, "Channel %s left '%s' %s-bridge <%s>\n",
		ast_channel_name(bridge_channel->chan),
		bridge->technology->name,
		bridge->v_table->name,
		bridge->uniqueid);

/* BUGBUG This is where incoming HOLD/UNHOLD memory should write UNHOLD into bridge. (if not local optimizing) */
/* BUGBUG This is where incoming DTMF begin/end memory should write DTMF end into bridge. (if not local optimizing) */
	if (!bridge_channel->just_joined) {
		/* Tell the bridge technology we are leaving so they tear us down */
		ast_debug(1, "Bridge %s: %p(%s) is leaving %s technology\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			bridge->technology->name);
		if (bridge->technology->leave) {
			bridge->technology->leave(bridge, bridge_channel);
		}
	}

	/* Remove channel from the bridge */
	if (!bridge_channel->suspended) {
		--bridge->num_active;
	}
	--bridge->num_channels;
	AST_LIST_REMOVE(&bridge->channels, bridge_channel, entry);
	bridge->v_table->pull(bridge, bridge_channel);

	ast_bridge_channel_clear_roles(bridge_channel);

	/* If we are not going to be hung up after leaving a bridge, and we were an
	 * outgoing channel, clear the outgoing flag.
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_OUTGOING)
			&& (ast_channel_softhangup_internal_flag(bridge_channel->chan) &
				(AST_SOFTHANGUP_ASYNCGOTO | AST_SOFTHANGUP_UNBRIDGE))) {
		ast_clear_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_OUTGOING);
	}

	bridge_dissolve_check(bridge_channel);

	bridge->reconfigured = 1;
	ast_bridge_publish_leave(bridge, bridge_channel->chan);
}

/*!
 * \internal
 * \brief Push the bridge channel into its specified bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to push.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 */
int bridge_channel_push(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *swap;

	ast_assert(!bridge_channel->in_bridge);

	swap = bridge_find_channel(bridge, bridge_channel->swap);
	bridge_channel->swap = NULL;

	if (swap) {
		ast_debug(1, "Bridge %s: pushing %p(%s) by swapping with %p(%s)\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			swap, ast_channel_name(swap->chan));
	} else {
		ast_debug(1, "Bridge %s: pushing %p(%s)\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));
	}

	/* Add channel to the bridge */
	if (bridge->dissolved
		|| bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT
		|| (swap && swap->state != AST_BRIDGE_CHANNEL_STATE_WAIT)
		|| bridge->v_table->push(bridge, bridge_channel, swap)
		|| ast_bridge_channel_establish_roles(bridge_channel)) {
		ast_debug(1, "Bridge %s: pushing %p(%s) into bridge failed\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));
		ast_bridge_features_remove(bridge_channel->features, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
		return -1;
	}
	bridge_channel->in_bridge = 1;
	bridge_channel->just_joined = 1;
	AST_LIST_INSERT_TAIL(&bridge->channels, bridge_channel, entry);
	++bridge->num_channels;
	if (!bridge_channel->suspended) {
		++bridge->num_active;
	}

	ast_verb(3, "Channel %s %s%s%s '%s' %s-bridge <%s>\n",
		ast_channel_name(bridge_channel->chan),
		swap ? "swapped with " : "joined",
		swap ? ast_channel_name(swap->chan) : "",
		swap ? " into" : "",
		bridge->technology->name,
		bridge->v_table->name,
		bridge->uniqueid);

	ast_bridge_publish_enter(bridge, bridge_channel->chan);
	if (swap) {
		ast_bridge_channel_leave_bridge(swap, AST_BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE);
		bridge_channel_pull(swap);
	}

	/* Clear any BLINDTRANSFER and ATTENDEDTRANSFER since the transfer has completed. */
	pbx_builtin_setvar_helper(bridge_channel->chan, "BLINDTRANSFER", NULL);
	pbx_builtin_setvar_helper(bridge_channel->chan, "ATTENDEDTRANSFER", NULL);

	/* Wake up the bridge channel thread to reevaluate any interval timers. */
	ast_queue_frame(bridge_channel->chan, &ast_null_frame);

	bridge->reconfigured = 1;
	return 0;
}

/*!
 * \internal
 * \brief Handle bridge channel control frame action.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to execute the control frame action on.
 * \param fr Control frame to handle.
 *
 * \return Nothing
 */
static void bridge_channel_handle_control(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr)
{
	struct ast_channel *chan;
	struct ast_option_header *aoh;
	int is_caller;
	int intercept_failed;

	chan = bridge_channel->chan;
	switch (fr->subclass.integer) {
	case AST_CONTROL_REDIRECTING:
		is_caller = !ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
		bridge_channel_suspend(bridge_channel);
		intercept_failed = ast_channel_redirecting_sub(NULL, chan, fr, 1)
			&& ast_channel_redirecting_macro(NULL, chan, fr, is_caller, 1);
		bridge_channel_unsuspend(bridge_channel);
		if (intercept_failed) {
			ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		}
		break;
	case AST_CONTROL_CONNECTED_LINE:
		is_caller = !ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
		bridge_channel_suspend(bridge_channel);
		intercept_failed = ast_channel_connected_line_sub(NULL, chan, fr, 1)
			&& ast_channel_connected_line_macro(NULL, chan, fr, is_caller, 1);
		bridge_channel_unsuspend(bridge_channel);
		if (intercept_failed) {
			ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		}
		break;
	case AST_CONTROL_HOLD:
	case AST_CONTROL_UNHOLD:
/*
 * BUGBUG bridge_channels should remember sending/receiving an outstanding HOLD to/from the bridge
 *
 * When the sending channel is pulled from the bridge it needs to write into the bridge an UNHOLD before being pulled.
 * When the receiving channel is pulled from the bridge it needs to generate its own UNHOLD.
 * Something similar needs to be done for DTMF begin/end.
 */
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_SRCUPDATE:
	case AST_CONTROL_SRCCHANGE:
	case AST_CONTROL_T38_PARAMETERS:
/* BUGBUG may have to do something with a jitter buffer for these. */
		ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	case AST_CONTROL_OPTION:
		/*
		 * Forward option Requests, but only ones we know are safe These
		 * are ONLY sent by chan_iax2 and I'm not convinced that they
		 * are useful. I haven't deleted them entirely because I just am
		 * not sure of the ramifications of removing them.
		 */
		aoh = fr->data.ptr;
		if (aoh && aoh->flag == AST_OPTION_FLAG_REQUEST) {
			switch (ntohs(aoh->option)) {
			case AST_OPTION_TONE_VERIFY:
			case AST_OPTION_TDD:
			case AST_OPTION_RELAXDTMF:
			case AST_OPTION_AUDIO_MODE:
			case AST_OPTION_DIGIT_DETECT:
			case AST_OPTION_FAX_DETECT:
				ast_channel_setoption(chan, ntohs(aoh->option), aoh->data,
					fr->datalen - sizeof(*aoh), 0);
				break;
			default:
				break;
			}
		}
		break;
	case AST_CONTROL_ANSWER:
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
		} else {
			ast_indicate(chan, -1);
		}
		break;
	default:
		ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	}
}

/*!
 * \internal
 * \brief Handle bridge channel write frame to channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to write outgoing frame.
 *
 * \return Nothing
 */
static void bridge_channel_handle_write(struct ast_bridge_channel *bridge_channel)
{
	struct ast_frame *fr;
	char nudge;

	ast_bridge_channel_lock(bridge_channel);
	if (read(bridge_channel->alert_pipe[0], &nudge, sizeof(nudge)) < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_WARNING, "read() failed for alert pipe on %p(%s): %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan), strerror(errno));
		}
	}
	fr = AST_LIST_REMOVE_HEAD(&bridge_channel->wr_queue, frame_list);
	ast_bridge_channel_unlock(bridge_channel);
	if (!fr) {
		return;
	}
	switch (fr->frametype) {
	case AST_FRAME_BRIDGE_ACTION:
		bridge_channel_handle_action(bridge_channel, fr);
		break;
	case AST_FRAME_CONTROL:
		bridge_channel_handle_control(bridge_channel, fr);
		break;
	case AST_FRAME_NULL:
		break;
	default:
		/* Write the frame to the channel. */
		bridge_channel->activity = AST_BRIDGE_CHANNEL_THREAD_SIMPLE;
		ast_write(bridge_channel->chan, fr);
		break;
	}
	ast_frfree(fr);
}

/*! \brief Internal function to handle DTMF from a channel */
static struct ast_frame *bridge_handle_dtmf(struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook_dtmf *hook;
	char dtmf[2];

/* BUGBUG the feature hook matching needs to be done here.  Any matching feature hook needs to be queued onto the bridge_channel.  Also the feature hook digit timeout needs to be handled. */
/* BUGBUG the AMI atxfer action just sends DTMF end events to initiate DTMF atxfer and dial the extension.  Another reason the DTMF hook matching needs rework. */
	/* See if this DTMF matches the beginnings of any feature hooks, if so we switch to the feature state to either execute the feature or collect more DTMF */
	dtmf[0] = frame->subclass.integer;
	dtmf[1] = '\0';
	hook = ao2_find(features->dtmf_hooks, dtmf, OBJ_PARTIAL_KEY);
	if (hook) {
		struct ast_frame action = {
			.frametype = AST_FRAME_BRIDGE_ACTION,
			.subclass.integer = BRIDGE_CHANNEL_ACTION_FEATURE,
		};

		ast_frfree(frame);
		frame = NULL;
		ast_bridge_channel_queue_frame(bridge_channel, &action);
		ao2_ref(hook, -1);
	}

	return frame;
}


/*!
 * \internal
 * \brief Feed notification that a frame is waiting on a channel into the bridging core
 *
 * \param bridge_channel Bridge channel the notification was received on
 */
static void bridge_handle_trip(struct ast_bridge_channel *bridge_channel)
{
	struct ast_frame *frame;

	if (bridge_channel->features->mute) {
		frame = ast_read_noaudio(bridge_channel->chan);
	} else {
		frame = ast_read(bridge_channel->chan);
	}

	if (!frame) {
		bridge_channel_handle_hangup(bridge_channel);
		return;
	}
	switch (frame->frametype) {
	case AST_FRAME_CONTROL:
		switch (frame->subclass.integer) {
		case AST_CONTROL_HANGUP:
			bridge_channel_handle_hangup(bridge_channel);
			ast_frfree(frame);
			return;
/* BUGBUG This is where incoming HOLD/UNHOLD memory should register.  Write UNHOLD into bridge when this channel is pulled. */
		default:
			break;
		}
		break;
	case AST_FRAME_DTMF_BEGIN:
		frame = bridge_handle_dtmf(bridge_channel, frame);
		if (!frame) {
			return;
		}
		/* Fall through */
	case AST_FRAME_DTMF_END:
		if (!bridge_channel->features->dtmf_passthrough) {
			ast_frfree(frame);
			return;
		}
/* BUGBUG This is where incoming DTMF begin/end memory should register.  Write DTMF end into bridge when this channel is pulled. */
		break;
	default:
		break;
	}

	/* Simply write the frame out to the bridge technology. */
/* BUGBUG The tech is where AST_CONTROL_ANSWER hook should go. (early bridge) */
/* BUGBUG The tech is where incoming BUSY/CONGESTION hangup should happen? (early bridge) */
	bridge_channel_write_frame(bridge_channel, frame);
	ast_frfree(frame);
}

/*!
 * \internal
 * \brief Determine how long till the next timer interval.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to determine how long can wait.
 *
 * \retval ms Number of milliseconds to wait.
 * \retval -1 to wait forever.
 */
static int bridge_channel_next_interval(struct ast_bridge_channel *bridge_channel)
{
	struct ast_heap *interval_hooks = bridge_channel->features->interval_hooks;
	struct ast_bridge_hook_timer *hook;
	int ms;

	ast_heap_wrlock(interval_hooks);
	hook = ast_heap_peek(interval_hooks, 1);
	if (hook) {
		ms = ast_tvdiff_ms(hook->timer.trip_time, ast_tvnow());
		if (ms < 0) {
			/* Expire immediately.  An interval hook is ready to run. */
			ms = 0;
		}
	} else {
		/* No hook so wait forever. */
		ms = -1;
	}
	ast_heap_unlock(interval_hooks);

	return ms;
}

/*!
 * \internal
 * \brief Wait for something to happen on the bridge channel and handle it.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to wait.
 *
 * \note Each channel does writing/reading in their own thread.
 *
 * \return Nothing
 */
static void bridge_channel_wait(struct ast_bridge_channel *bridge_channel)
{
	int ms;
	int outfd;
	struct ast_channel *chan;

	/* Wait for data to either come from the channel or us to be signaled */
	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
	} else if (bridge_channel->suspended) {
/* BUGBUG the external party use of suspended will go away as will these references because this is the bridge channel thread */
		ast_debug(1, "Bridge %s: %p(%s) is going into a signal wait\n",
			bridge_channel->bridge->uniqueid, bridge_channel,
			ast_channel_name(bridge_channel->chan));
		ast_cond_wait(&bridge_channel->cond, ao2_object_get_lockaddr(bridge_channel));
	} else {
		ast_debug(10, "Bridge %s: %p(%s) is going into a waitfor\n",
			bridge_channel->bridge->uniqueid, bridge_channel,
			ast_channel_name(bridge_channel->chan));
		bridge_channel->waiting = 1;
		ast_bridge_channel_unlock(bridge_channel);
		outfd = -1;
		ms = bridge_channel_next_interval(bridge_channel);
		chan = ast_waitfor_nandfds(&bridge_channel->chan, 1,
			&bridge_channel->alert_pipe[0], 1, NULL, &outfd, &ms);
		bridge_channel->waiting = 0;
		if (ast_channel_softhangup_internal_flag(bridge_channel->chan) & AST_SOFTHANGUP_UNBRIDGE) {
			ast_channel_clear_softhangup(bridge_channel->chan, AST_SOFTHANGUP_UNBRIDGE);
			ast_bridge_channel_lock_bridge(bridge_channel);
			bridge_channel->bridge->reconfigured = 1;
			bridge_reconfigured(bridge_channel->bridge, 0);
			ast_bridge_unlock(bridge_channel->bridge);
		}
		ast_bridge_channel_lock(bridge_channel);
		bridge_channel->activity = AST_BRIDGE_CHANNEL_THREAD_FRAME;
		ast_bridge_channel_unlock(bridge_channel);
		if (!bridge_channel->suspended
			&& bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
			if (chan) {
				bridge_handle_trip(bridge_channel);
			} else if (-1 < outfd) {
				bridge_channel_handle_write(bridge_channel);
			} else if (ms == 0) {
				/* An interval expired. */
				bridge_channel_handle_interval(bridge_channel);
			}
		}
		bridge_channel->activity = AST_BRIDGE_CHANNEL_THREAD_IDLE;
		return;
	}
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Handle bridge channel join/leave event.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is involved.
 * \param type Specified join/leave event.
 *
 * \return Nothing
 */
static void bridge_channel_event_join_leave(struct ast_bridge_channel *bridge_channel, enum ast_bridge_hook_type type)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run the specified hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		if (hook->type == type) {
			break;
		}
	}
	if (hook) {
		/* Found the first specified hook to run. */
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		do {
			if (hook->type == type) {
				hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);
				ao2_unlink(features->other_hooks, hook);
			}
			ao2_ref(hook, -1);
		} while ((hook = ao2_iterator_next(&iter)));
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
	ao2_iterator_destroy(&iter);
}

/*! \brief Join a channel to a bridge and handle anything the bridge may want us to do */
void bridge_channel_join(struct ast_bridge_channel *bridge_channel)
{
	ast_format_copy(&bridge_channel->read_format, ast_channel_readformat(bridge_channel->chan));
	ast_format_copy(&bridge_channel->write_format, ast_channel_writeformat(bridge_channel->chan));

	ast_debug(1, "Bridge %s: %p(%s) is joining\n",
		bridge_channel->bridge->uniqueid,
		bridge_channel, ast_channel_name(bridge_channel->chan));

	/*
	 * Get "in the bridge" before pushing the channel for any
	 * masquerades on the channel to happen before bridging.
	 */
	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_bridge_set(bridge_channel->chan, bridge_channel->bridge);
	ast_channel_unlock(bridge_channel->chan);

	/* Add the jitterbuffer if the channel requires it */
	ast_jb_enable_for_channel(bridge_channel->chan);

	/*
	 * Directly locking the bridge is safe here because nobody else
	 * knows about this bridge_channel yet.
	 */
	ast_bridge_lock(bridge_channel->bridge);

	if (!bridge_channel->bridge->callid) {
		bridge_channel->bridge->callid = ast_read_threadstorage_callid();
	}

	if (bridge_channel_push(bridge_channel)) {
		ast_bridge_channel_leave_bridge(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE);
	}
	bridge_reconfigured(bridge_channel->bridge, 1);

	if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/*
		 * Indicate a source change since this channel is entering the
		 * bridge system only if the bridge technology is not MULTIMIX
		 * capable.  The MULTIMIX technology has already done it.
		 */
		if (!(bridge_channel->bridge->technology->capabilities
			& AST_BRIDGE_CAPABILITY_MULTIMIX)) {
			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCCHANGE);
		}

		ast_bridge_unlock(bridge_channel->bridge);
		bridge_channel_event_join_leave(bridge_channel, AST_BRIDGE_HOOK_TYPE_JOIN);
		while (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
			/* Wait for something to do. */
			bridge_channel_wait(bridge_channel);
		}
		bridge_channel_event_join_leave(bridge_channel, AST_BRIDGE_HOOK_TYPE_LEAVE);
		ast_bridge_channel_lock_bridge(bridge_channel);
	}

	bridge_channel_pull(bridge_channel);
	bridge_reconfigured(bridge_channel->bridge, 1);

	ast_bridge_unlock(bridge_channel->bridge);

	/* Indicate a source change since this channel is leaving the bridge system. */
	ast_indicate(bridge_channel->chan, AST_CONTROL_SRCCHANGE);

/* BUGBUG Revisit in regards to moving channels between bridges and local channel optimization. */
/* BUGBUG This is where outgoing HOLD/UNHOLD memory should write UNHOLD to channel. */
	/* Complete any partial DTMF digit before exiting the bridge. */
	if (ast_channel_sending_dtmf_digit(bridge_channel->chan)) {
		ast_channel_end_dtmf(bridge_channel->chan,
			ast_channel_sending_dtmf_digit(bridge_channel->chan),
			ast_channel_sending_dtmf_tv(bridge_channel->chan), "bridge end");
	}

	/*
	 * Wait for any dual redirect to complete.
	 *
	 * Must be done while "still in the bridge" for ast_async_goto()
	 * to work right.
	 */
	while (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT)) {
		sched_yield();
	}
	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_bridge_set(bridge_channel->chan, NULL);
	ast_channel_unlock(bridge_channel->chan);

	ast_bridge_channel_restore_formats(bridge_channel);
}

void ast_bridge_channel_leave_bridge(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state)
{
	ast_bridge_channel_lock(bridge_channel);
	ast_bridge_channel_leave_bridge_nolock(bridge_channel, new_state);
	ast_bridge_channel_unlock(bridge_channel);
}

void ast_bridge_channel_leave_bridge_nolock(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state)
{
/* BUGBUG need cause code for the bridge_channel leaving the bridge. */
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		return;
	}

	ast_debug(1, "Setting %p(%s) state from:%d to:%d\n",
		bridge_channel, ast_channel_name(bridge_channel->chan), bridge_channel->state,
		new_state);

	/* Change the state on the bridge channel */
	bridge_channel->state = new_state;

	bridge_channel_poke(bridge_channel);
}

/*!
 * \internal
 * \brief Queue a blind transfer action on a transferee bridge channel
 *
 * This is only relevant for when a blind transfer is performed on a two-party
 * bridge. The transferee's bridge channel will have a blind transfer bridge
 * action queued onto it, resulting in the party being redirected to a new
 * destination
 *
 * \param transferee The channel to have the action queued on
 * \param exten The destination extension for the transferee
 * \param context The destination context for the transferee
 * \param hook Frame hook to attach to transferee
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int bridge_channel_queue_blind_transfer(struct ast_channel *transferee,
		const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data)
{
	RAII_VAR(struct ast_bridge_channel *, transferee_bridge_channel, NULL, ao2_cleanup);
	struct blind_transfer_data blind_data;

	ast_channel_lock(transferee);
	transferee_bridge_channel = ast_channel_get_bridge_channel(transferee);
	ast_channel_unlock(transferee);

	if (!transferee_bridge_channel) {
		return -1;
	}

	if (new_channel_cb) {
		new_channel_cb(transferee, user_data, AST_BRIDGE_TRANSFER_SINGLE_PARTY);
	}

	ast_copy_string(blind_data.exten, exten, sizeof(blind_data.exten));
	ast_copy_string(blind_data.context, context, sizeof(blind_data.context));

	return ast_bridge_channel_queue_action_data(transferee_bridge_channel,
		BRIDGE_CHANNEL_ACTION_BLIND_TRANSFER, &blind_data, sizeof(blind_data));
}

int bridge_channel_queue_attended_transfer(struct ast_channel *transferee,
		struct ast_channel *unbridged_chan)
{
	RAII_VAR(struct ast_bridge_channel *, transferee_bridge_channel, NULL, ao2_cleanup);
	char unbridged_chan_name[AST_CHANNEL_NAME];

	ast_channel_lock(transferee);
	transferee_bridge_channel = ast_channel_get_bridge_channel(transferee);
	ast_channel_unlock(transferee);

	if (!transferee_bridge_channel) {
		return -1;
	}

	ast_copy_string(unbridged_chan_name, ast_channel_name(unbridged_chan),
		sizeof(unbridged_chan_name));

	return ast_bridge_channel_queue_action_data(transferee_bridge_channel,
		BRIDGE_CHANNEL_ACTION_ATTENDED_TRANSFER, unbridged_chan_name,
		sizeof(unbridged_chan_name));
}
