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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_playback.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stringfields.h"
#include "asterisk/uuid.h"

/*! Number of hash buckets for playback container. Keep it prime! */
#define PLAYBACK_BUCKETS 127

/*! Number of milliseconds of media to skip */
#define PLAYBACK_SKIPMS 250

#define SOUND_URI_SCHEME "sound:"
#define RECORDING_URI_SCHEME "recording:"

STASIS_MESSAGE_TYPE_DEFN(stasis_app_playback_snapshot_type);

/*! Container of all current playbacks */
static struct ao2_container *playbacks;

/*! Playback control object for res_stasis */
struct stasis_app_playback {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(id);	/*!< Playback unique id */
		AST_STRING_FIELD(media);	/*!< Playback media uri */
		AST_STRING_FIELD(language);	/*!< Preferred language */
		);
	/*! Current playback state */
	enum stasis_app_playback_state state;
	/*! Control object for the channel we're playing back to */
	struct stasis_app_control *control;
};

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
	case STASIS_PLAYBACK_STATE_COMPLETE:
		return "done";
	}

	return "?";
}

static struct ast_json *playback_to_json(struct stasis_app_playback *playback)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	if (playback == NULL) {
		return NULL;
	}

	json = ast_json_pack("{s: s, s: s, s: s, s: s}",
		"id", playback->id,
		"media_uri", playback->media,
		"language", playback->language,
		"state", state_to_string(playback->state));

	return ast_json_ref(json);
}

static void playback_publish(struct stasis_app_playback *playback)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	ast_assert(playback != NULL);

	json = playback_to_json(playback);
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

static void playback_set_state(struct stasis_app_playback *playback,
	enum stasis_app_playback_state state)
{
	SCOPED_AO2LOCK(lock, playback);

	playback->state = state;
	playback_publish(playback);
}

static void playback_cleanup(struct stasis_app_playback *playback)
{
	playback_set_state(playback, STASIS_PLAYBACK_STATE_COMPLETE);

	ao2_unlink_flags(playbacks, playback,
		OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
}

static void *__app_control_play_uri(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL,
		playback_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	const char *file;
	int res;
	/* Even though these local variables look fairly pointless, the avoid
	 * having a bunch of NULL's passed directly into
	 * ast_control_streamfile() */
	const char *fwd = NULL;
	const char *rev = NULL;
	const char *stop = NULL;
	const char *pause = NULL;
	const char *restart = NULL;
	int skipms = PLAYBACK_SKIPMS;
	long offsetms = 0;

	playback = data;
	ast_assert(playback != NULL);

	playback_set_state(playback, STASIS_PLAYBACK_STATE_PLAYING);

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ast_begins_with(playback->media, SOUND_URI_SCHEME)) {
		/* Play sound */
		file = playback->media + strlen(SOUND_URI_SCHEME);
	} else if (ast_begins_with(playback->media, RECORDING_URI_SCHEME)) {
		/* Play recording */
		file = playback->media + strlen(RECORDING_URI_SCHEME);
	} else {
		/* Play URL */
		ast_log(LOG_ERROR, "Unimplemented\n");
		return NULL;
	}

	res = ast_control_streamfile(chan, file, fwd, rev, stop, pause,
		restart, skipms, &offsetms);

	if (res != 0) {
		ast_log(LOG_WARNING, "%s: Playback failed for %s",
			ast_channel_uniqueid(chan), playback->media);
	}

	return NULL;
}

static void playback_dtor(void *obj)
{
	struct stasis_app_playback *playback = obj;

	ast_string_field_free_memory(playback);
}

struct stasis_app_playback *stasis_app_control_play_uri(
	struct stasis_app_control *control, const char *uri,
	const char *language)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	char id[AST_UUID_STR_LEN];

	ast_debug(3, "%s: Sending play(%s) command\n",
		stasis_app_control_get_channel_id(control), uri);

	playback = ao2_alloc(sizeof(*playback), playback_dtor);
	if (!playback || ast_string_field_init(playback, 128) ){
		return NULL;
	}

	ast_uuid_generate_str(id, sizeof(id));
	ast_string_field_set(playback, id, id);
	ast_string_field_set(playback, media, uri);
	ast_string_field_set(playback, language, language);
	playback->control = control;
	ao2_link(playbacks, playback);

	playback_set_state(playback, STASIS_PLAYBACK_STATE_QUEUED);

	ao2_ref(playback, +1);
	stasis_app_send_command_async(
		control, __app_control_play_uri, playback);


	ao2_ref(playback, +1);
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

struct ast_json *stasis_app_playback_find_by_id(const char *id)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	playback = ao2_find(playbacks, id, OBJ_KEY);
	if (playback == NULL) {
		return NULL;
	}

	json = playback_to_json(playback);
	return ast_json_ref(json);
}

int stasis_app_playback_control(struct stasis_app_playback *playback,
	enum stasis_app_playback_media_control control)
{
	SCOPED_AO2LOCK(lock, playback);
	ast_assert(0); /* TODO */
	return -1;
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS,
	"Stasis application playback support",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis");
