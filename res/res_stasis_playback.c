/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief res_stasis playback support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<depend type="module">res_stasis_recording</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_playback.h"
#include "asterisk/stasis_app_recording.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stringfields.h"
#include "asterisk/uuid.h"
#include "asterisk/say.h"
#include "asterisk/indications.h"

/*! Number of hash buckets for playback container. Keep it prime! */
#define PLAYBACK_BUCKETS 127

/*! Default number of milliseconds of media to skip */
#define PLAYBACK_DEFAULT_SKIPMS 3000

#define SOUND_URI_SCHEME "sound:"
#define RECORDING_URI_SCHEME "recording:"
#define NUMBER_URI_SCHEME "number:"
#define DIGITS_URI_SCHEME "digits:"
#define CHARACTERS_URI_SCHEME "characters:"
#define TONE_URI_SCHEME "tone:"

/*! Container of all current playbacks */
static struct ao2_container *playbacks;

/*! Playback control object for res_stasis */
struct stasis_app_playback {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(id);	/*!< Playback unique id */
		AST_STRING_FIELD(media);	/*!< Playback media uri */
		AST_STRING_FIELD(language);	/*!< Preferred language */
		AST_STRING_FIELD(target);       /*!< Playback device uri */
		);
	/*! Control object for the channel we're playing back to */
	struct stasis_app_control *control;
	/*! Number of milliseconds to skip before playing */
	long offsetms;
	/*! Number of milliseconds to skip for forward/reverse operations */
	int skipms;
	/*! Number of milliseconds of media that has been played */
	long playedms;
	/*! Current playback state */
	enum stasis_app_playback_state state;
	/*! Set when the playback can be controlled */
	unsigned int controllable:1;
};

static struct ast_json *playback_to_json(struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct ast_json *blob = channel_blob->blob;
	const char *state =
		ast_json_string_get(ast_json_object_get(blob, "state"));
	const char *type;

	if (!strcmp(state, "playing")) {
		type = "PlaybackStarted";
	} else if (!strcmp(state, "done")) {
		type = "PlaybackFinished";
	} else {
		return NULL;
	}

	return ast_json_pack("{s: s, s: O}",
		"type", type,
		"playback", blob);
}

STASIS_MESSAGE_TYPE_DEFN(stasis_app_playback_snapshot_type,
	.to_json = playback_to_json,
);

static void playback_dtor(void *obj)
{
	struct stasis_app_playback *playback = obj;

	ast_string_field_free_memory(playback);
}

static struct stasis_app_playback *playback_create(
	struct stasis_app_control *control, const char *id)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	char uuid[AST_UUID_STR_LEN];

	if (!control) {
		return NULL;
	}

	playback = ao2_alloc(sizeof(*playback), playback_dtor);
	if (!playback || ast_string_field_init(playback, 128)) {
		return NULL;
	}

	if (!ast_strlen_zero(id)) {
		ast_string_field_set(playback, id, id);
	} else {
		ast_uuid_generate_str(uuid, sizeof(uuid));
		ast_string_field_set(playback, id, uuid);
	}

	playback->control = control;

	ao2_ref(playback, +1);
	return playback;
}

static int playback_hash(const void *obj, int flags)
{
	const struct stasis_app_playback *playback = obj;
	const char *id = flags & OBJ_KEY ? obj : playback->id;
	return ast_str_hash(id);
}

static int playback_cmp(void *obj, void *arg, int flags)
{
	struct stasis_app_playback *lhs = obj;
	struct stasis_app_playback *rhs = arg;
	const char *rhs_id = flags & OBJ_KEY ? arg : rhs->id;

	if (strcmp(lhs->id, rhs_id) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
}

static const char *state_to_string(enum stasis_app_playback_state state)
{
	switch (state) {
	case STASIS_PLAYBACK_STATE_QUEUED:
		return "queued";
	case STASIS_PLAYBACK_STATE_PLAYING:
		return "playing";
	case STASIS_PLAYBACK_STATE_PAUSED:
		return "paused";
	case STASIS_PLAYBACK_STATE_STOPPED:
	case STASIS_PLAYBACK_STATE_COMPLETE:
	case STASIS_PLAYBACK_STATE_CANCELED:
		/* It doesn't really matter how we got here, but all of these
		 * states really just mean 'done' */
		return "done";
	case STASIS_PLAYBACK_STATE_MAX:
		break;
	}

	return "?";
}

static void playback_publish(struct stasis_app_playback *playback)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	ast_assert(playback != NULL);

	json = stasis_app_playback_to_json(playback);
	if (json == NULL) {
		return;
	}

	message = ast_channel_blob_create_from_cache(
		stasis_app_control_get_channel_id(playback->control),
		stasis_app_playback_snapshot_type(), json);
	if (message == NULL) {
		return;
	}

	stasis_app_control_publish(playback->control, message);
}

static int playback_first_update(struct stasis_app_playback *playback,
	const char *uniqueid)
{
	int res;
	SCOPED_AO2LOCK(lock, playback);

	if (playback->state == STASIS_PLAYBACK_STATE_CANCELED) {
		ast_log(LOG_NOTICE, "%s: Playback canceled for %s\n",
			uniqueid, playback->media);
		res = -1;
	} else {
		res = 0;
		playback->state = STASIS_PLAYBACK_STATE_PLAYING;
	}

	playback_publish(playback);
	return res;
}

static void playback_final_update(struct stasis_app_playback *playback,
	long playedms, int res, const char *uniqueid)
{
	SCOPED_AO2LOCK(lock, playback);

	playback->playedms = playedms;
	if (res == 0) {
		playback->state = STASIS_PLAYBACK_STATE_COMPLETE;
	} else {
		if (playback->state == STASIS_PLAYBACK_STATE_STOPPED) {
			ast_log(LOG_NOTICE, "%s: Playback stopped for %s\n",
				uniqueid, playback->media);
		} else {
			ast_log(LOG_WARNING, "%s: Playback failed for %s\n",
				uniqueid, playback->media);
			playback->state = STASIS_PLAYBACK_STATE_STOPPED;
		}
	}

	playback_publish(playback);
}

static void play_on_channel(struct stasis_app_playback *playback,
	struct ast_channel *chan)
{
	int res;
	long offsetms;

	/* Even though these local variables look fairly pointless, the avoid
	 * having a bunch of NULL's passed directly into
	 * ast_control_streamfile() */
	const char *fwd = NULL;
	const char *rev = NULL;
	const char *stop = NULL;
	const char *pause = NULL;
	const char *restart = NULL;

	ast_assert(playback != NULL);

	offsetms = playback->offsetms;

	res = playback_first_update(playback, ast_channel_uniqueid(chan));

	if (res != 0) {
		return;
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_indicate(chan, AST_CONTROL_PROGRESS);
	}

	if (ast_begins_with(playback->media, SOUND_URI_SCHEME)) {
		playback->controllable = 1;

		/* Play sound */
		res = ast_control_streamfile_lang(chan, playback->media + strlen(SOUND_URI_SCHEME),
				fwd, rev, stop, pause, restart, playback->skipms, playback->language,
				&offsetms);
	} else if (ast_begins_with(playback->media, RECORDING_URI_SCHEME)) {
		/* Play recording */
		RAII_VAR(struct stasis_app_stored_recording *, recording, NULL,
			ao2_cleanup);
		const char *relname =
			playback->media + strlen(RECORDING_URI_SCHEME);
		recording = stasis_app_stored_recording_find_by_name(relname);

		if (!recording) {
			ast_log(LOG_ERROR, "Attempted to play recording '%s' on channel '%s' but recording does not exist",
				relname, ast_channel_name(chan));
			return;
		}

		playback->controllable = 1;

		res = ast_control_streamfile_lang(chan,
			stasis_app_stored_recording_get_file(recording), fwd, rev, stop, pause,
			restart, playback->skipms, playback->language, &offsetms);
	} else if (ast_begins_with(playback->media, NUMBER_URI_SCHEME)) {
		int number;

		if (sscanf(playback->media + strlen(NUMBER_URI_SCHEME), "%30d", &number) != 1) {
			ast_log(LOG_ERROR, "Attempted to play number '%s' on channel '%s' but number is invalid",
				playback->media + strlen(NUMBER_URI_SCHEME), ast_channel_name(chan));
			return;
		}

		res = ast_say_number(chan, number, stop, playback->language, NULL);
	} else if (ast_begins_with(playback->media, DIGITS_URI_SCHEME)) {
		res = ast_say_digit_str(chan, playback->media + strlen(DIGITS_URI_SCHEME),
			stop, playback->language);
	} else if (ast_begins_with(playback->media, CHARACTERS_URI_SCHEME)) {
		res = ast_say_character_str(chan, playback->media + strlen(CHARACTERS_URI_SCHEME),
			stop, playback->language, AST_SAY_CASE_NONE);
	} else if (ast_begins_with(playback->media, TONE_URI_SCHEME)) {
		playback->controllable = 1;
		res = ast_control_tone(chan, playback->media + strlen(TONE_URI_SCHEME));
	} else {
		/* Play URL */
		ast_log(LOG_ERROR, "Attempted to play URI '%s' on channel '%s' but scheme is unsupported\n",
			playback->media, ast_channel_name(chan));
		return;
	}

	playback_final_update(playback, offsetms, res,
		ast_channel_uniqueid(chan));

	return;
}

/*!
 * \brief Special case code to play while a channel is in a bridge.
 *
 * \param bridge_channel The channel's bridge_channel.
 * \param playback_id Id of the playback to start.
 */
static void play_on_channel_in_bridge(struct ast_bridge_channel *bridge_channel,
	const char *playback_id)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);

	playback = stasis_app_playback_find_by_id(playback_id);
	if (!playback) {
		ast_log(LOG_ERROR, "Couldn't find playback %s\n",
			playback_id);
		return;
	}

	play_on_channel(playback, bridge_channel->chan);
}

/*!
 * \brief \ref RAII_VAR function to remove a playback from the global list when
 * leaving scope.
 */
static void remove_from_playbacks(void *data)
{
	struct stasis_app_playback *playback = data;

	ao2_unlink_flags(playbacks, playback,
		OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
	ao2_ref(playback, -1);
}

static int play_uri(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct stasis_app_playback *playback = data;
	struct ast_bridge *bridge;

	if (!control) {
		return -1;
	}

	bridge = stasis_app_get_bridge(control);
	if (bridge) {
		struct ast_bridge_channel *bridge_chan;

		/* Queue up playback on the bridge */
		ast_bridge_lock(bridge);
		bridge_chan = ao2_bump(bridge_find_channel(bridge, chan));
		ast_bridge_unlock(bridge);
		if (bridge_chan) {
			ast_bridge_channel_queue_playfile_sync(
				bridge_chan,
				play_on_channel_in_bridge,
				playback->id,
				NULL); /* moh_class */
		}
		ao2_cleanup(bridge_chan);
	} else {
		play_on_channel(playback, chan);
	}

	return 0;
}

static void set_target_uri(
	struct stasis_app_playback *playback,
	enum stasis_app_playback_target_type target_type,
	const char *target_id)
{
	const char *type = NULL;
	switch (target_type) {
	case STASIS_PLAYBACK_TARGET_CHANNEL:
		type = "channel";
		break;
	case STASIS_PLAYBACK_TARGET_BRIDGE:
		type = "bridge";
		break;
	}

	ast_assert(type != NULL);

	ast_string_field_build(playback, target, "%s:%s", type, target_id);
}

struct stasis_app_playback *stasis_app_control_play_uri(
	struct stasis_app_control *control, const char *uri,
	const char *language, const char *target_id,
	enum stasis_app_playback_target_type target_type,
	int skipms, long offsetms, const char *id)
{
	struct stasis_app_playback *playback;

	if (skipms < 0 || offsetms < 0) {
		return NULL;
	}

	ast_debug(3, "%s: Sending play(%s) command\n",
		stasis_app_control_get_channel_id(control), uri);

	playback = playback_create(control, id);
	if (!playback) {
		return NULL;
	}

	if (skipms == 0) {
		skipms = PLAYBACK_DEFAULT_SKIPMS;
	}

	ast_string_field_set(playback, media, uri);
	ast_string_field_set(playback, language, language);
	set_target_uri(playback, target_type, target_id);
	playback->skipms = skipms;
	playback->offsetms = offsetms;
	ao2_link(playbacks, playback);

	playback->state = STASIS_PLAYBACK_STATE_QUEUED;
	playback_publish(playback);

	stasis_app_send_command_async(control, play_uri, ao2_bump(playback), remove_from_playbacks);

	return playback;
}

enum stasis_app_playback_state stasis_app_playback_get_state(
	struct stasis_app_playback *control)
{
	SCOPED_AO2LOCK(lock, control);
	return control->state;
}

const char *stasis_app_playback_get_id(
	struct stasis_app_playback *control)
{
	/* id is immutable; no lock needed */
	return control->id;
}

struct stasis_app_playback *stasis_app_playback_find_by_id(const char *id)
{
	return ao2_find(playbacks, id, OBJ_KEY);
}

struct ast_json *stasis_app_playback_to_json(
	const struct stasis_app_playback *playback)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	if (playback == NULL) {
		return NULL;
	}

	json = ast_json_pack("{s: s, s: s, s: s, s: s, s: s}",
		"id", playback->id,
		"media_uri", playback->media,
		"target_uri", playback->target,
		"language", playback->language,
		"state", state_to_string(playback->state));

	return ast_json_ref(json);
}

typedef int (*playback_opreation_cb)(struct stasis_app_playback *playback);

static int playback_noop(struct stasis_app_playback *playback)
{
	return 0;
}

static int playback_cancel(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);
	playback->state = STASIS_PLAYBACK_STATE_CANCELED;
	return 0;
}

static int playback_stop(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);

	if (!playback->controllable) {
		return -1;
	}

	playback->state = STASIS_PLAYBACK_STATE_STOPPED;
	return stasis_app_control_queue_control(playback->control,
		AST_CONTROL_STREAM_STOP);
}

static int playback_restart(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);

	if (!playback->controllable) {
		return -1;
	}

	return stasis_app_control_queue_control(playback->control,
		AST_CONTROL_STREAM_RESTART);
}

static int playback_pause(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);

	if (!playback->controllable) {
		return -1;
	}

	playback->state = STASIS_PLAYBACK_STATE_PAUSED;
	playback_publish(playback);

	return stasis_app_control_queue_control(playback->control,
		AST_CONTROL_STREAM_SUSPEND);
}

static int playback_unpause(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);

	if (!playback->controllable) {
		return -1;
	}

	playback->state = STASIS_PLAYBACK_STATE_PLAYING;
	playback_publish(playback);

	return stasis_app_control_queue_control(playback->control,
		AST_CONTROL_STREAM_SUSPEND);
}

static int playback_reverse(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);

	if (!playback->controllable) {
		return -1;
	}

	return stasis_app_control_queue_control(playback->control,
		AST_CONTROL_STREAM_REVERSE);
}

static int playback_forward(struct stasis_app_playback *playback)
{
	SCOPED_AO2LOCK(lock, playback);

	if (!playback->controllable) {
		return -1;
	}

	return stasis_app_control_queue_control(playback->control,
		AST_CONTROL_STREAM_FORWARD);
}

/*!
 * \brief A sparse array detailing how commands should be handled in the
 * various playback states. Unset entries imply invalid operations.
 */
playback_opreation_cb operations[STASIS_PLAYBACK_STATE_MAX][STASIS_PLAYBACK_MEDIA_OP_MAX] = {
	[STASIS_PLAYBACK_STATE_QUEUED][STASIS_PLAYBACK_STOP] = playback_cancel,
	[STASIS_PLAYBACK_STATE_QUEUED][STASIS_PLAYBACK_RESTART] = playback_noop,

	[STASIS_PLAYBACK_STATE_PLAYING][STASIS_PLAYBACK_STOP] = playback_stop,
	[STASIS_PLAYBACK_STATE_PLAYING][STASIS_PLAYBACK_RESTART] = playback_restart,
	[STASIS_PLAYBACK_STATE_PLAYING][STASIS_PLAYBACK_PAUSE] = playback_pause,
	[STASIS_PLAYBACK_STATE_PLAYING][STASIS_PLAYBACK_UNPAUSE] = playback_noop,
	[STASIS_PLAYBACK_STATE_PLAYING][STASIS_PLAYBACK_REVERSE] = playback_reverse,
	[STASIS_PLAYBACK_STATE_PLAYING][STASIS_PLAYBACK_FORWARD] = playback_forward,

	[STASIS_PLAYBACK_STATE_PAUSED][STASIS_PLAYBACK_STOP] = playback_stop,
	[STASIS_PLAYBACK_STATE_PAUSED][STASIS_PLAYBACK_PAUSE] = playback_noop,
	[STASIS_PLAYBACK_STATE_PAUSED][STASIS_PLAYBACK_UNPAUSE] = playback_unpause,

	[STASIS_PLAYBACK_STATE_COMPLETE][STASIS_PLAYBACK_STOP] = playback_noop,
	[STASIS_PLAYBACK_STATE_CANCELED][STASIS_PLAYBACK_STOP] = playback_noop,
	[STASIS_PLAYBACK_STATE_STOPPED][STASIS_PLAYBACK_STOP] = playback_noop,
};

enum stasis_playback_oper_results stasis_app_playback_operation(
	struct stasis_app_playback *playback,
	enum stasis_app_playback_media_operation operation)
{
	playback_opreation_cb cb;
	SCOPED_AO2LOCK(lock, playback);

	ast_assert(playback->state < STASIS_PLAYBACK_STATE_MAX);

	if (operation >= STASIS_PLAYBACK_MEDIA_OP_MAX) {
		ast_log(LOG_ERROR, "Invalid playback operation %u\n", operation);
		return -1;
	}

	cb = operations[playback->state][operation];

	if (!cb) {
		if (playback->state != STASIS_PLAYBACK_STATE_PLAYING) {
			/* So we can be specific in our error message. */
			return STASIS_PLAYBACK_OPER_NOT_PLAYING;
		} else {
			/* And, really, all operations should be valid during
			 * playback */
			ast_log(LOG_ERROR,
				"Unhandled operation during playback: %u\n",
				operation);
			return STASIS_PLAYBACK_OPER_FAILED;
		}
	}

	return cb(playback) ?
		STASIS_PLAYBACK_OPER_FAILED : STASIS_PLAYBACK_OPER_OK;
}

static int load_module(void)
{
	int r;

	r = STASIS_MESSAGE_TYPE_INIT(stasis_app_playback_snapshot_type);
	if (r != 0) {
		return AST_MODULE_LOAD_FAILURE;
	}

	playbacks = ao2_container_alloc(PLAYBACK_BUCKETS, playback_hash,
		playback_cmp);
	if (!playbacks) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ao2_cleanup(playbacks);
	playbacks = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_app_playback_snapshot_type);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application playback support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis,res_stasis_recording");
