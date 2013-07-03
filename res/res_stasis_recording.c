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
 * \brief res_stasis recording support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/dsp.h"
#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_recording.h"
#include "asterisk/stasis_channels.h"

/*! Number of hash buckets for recording container. Keep it prime! */
#define RECORDING_BUCKETS 127

/*! Comment is ignored by most formats, so we will ignore it, too. */
#define RECORDING_COMMENT NULL

/*! Recording check is unimplemented. le sigh */
#define RECORDING_CHECK 0

STASIS_MESSAGE_TYPE_DEFN(stasis_app_recording_snapshot_type);

/*! Container of all current recordings */
static struct ao2_container *recordings;

struct stasis_app_recording {
	/*! Recording options. */
	struct stasis_app_recording_options *options;
	/*! Absolute path (minus extension) of the recording */
	char *absolute_name;
	/*! Control object for the channel we're playing back to */
	struct stasis_app_control *control;

	/*! Current state of the recording. */
	enum stasis_app_recording_state state;
};

static int recording_hash(const void *obj, int flags)
{
	const struct stasis_app_recording *recording = obj;
	const char *id = flags & OBJ_KEY ? obj : recording->options->name;
	return ast_str_hash(id);
}

static int recording_cmp(void *obj, void *arg, int flags)
{
	struct stasis_app_recording *lhs = obj;
	struct stasis_app_recording *rhs = arg;
	const char *rhs_id = flags & OBJ_KEY ? arg : rhs->options->name;

	if (strcmp(lhs->options->name, rhs_id) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
}

static const char *state_to_string(enum stasis_app_recording_state state)
{
	switch (state) {
	case STASIS_APP_RECORDING_STATE_QUEUED:
		return "queued";
	case STASIS_APP_RECORDING_STATE_RECORDING:
		return "recording";
	case STASIS_APP_RECORDING_STATE_PAUSED:
		return "paused";
	case STASIS_APP_RECORDING_STATE_COMPLETE:
		return "done";
	case STASIS_APP_RECORDING_STATE_FAILED:
		return "failed";
	}

	return "?";
}

static void recording_options_dtor(void *obj)
{
	struct stasis_app_recording_options *options = obj;

	ast_string_field_free_memory(options);
}

struct stasis_app_recording_options *stasis_app_recording_options_create(
	const char *name, const char *format)
{
	RAII_VAR(struct stasis_app_recording_options *, options, NULL,
		ao2_cleanup);

	options = ao2_alloc(sizeof(*options), recording_options_dtor);

	if (!options || ast_string_field_init(options, 128)) {
		return NULL;
	}
	ast_string_field_set(options, name, name);
	ast_string_field_set(options, format, format);

	ao2_ref(options, +1);
	return options;
}

char stasis_app_recording_termination_parse(const char *str)
{
	if (ast_strlen_zero(str)) {
		return STASIS_APP_RECORDING_TERMINATE_NONE;
	}

	if (strcasecmp(str, "none") == 0) {
		return STASIS_APP_RECORDING_TERMINATE_NONE;
	}

	if (strcasecmp(str, "any") == 0) {
		return STASIS_APP_RECORDING_TERMINATE_ANY;
	}

	if (strcasecmp(str, "#") == 0) {
		return '#';
	}

	if (strcasecmp(str, "*") == 0) {
		return '*';
	}

	return STASIS_APP_RECORDING_TERMINATE_INVALID;
}

enum ast_record_if_exists stasis_app_recording_if_exists_parse(
	const char *str)
{
	if (ast_strlen_zero(str)) {
		/* Default value */
		return AST_RECORD_IF_EXISTS_FAIL;
	}

	if (strcasecmp(str, "fail") == 0) {
		return AST_RECORD_IF_EXISTS_FAIL;
	}

	if (strcasecmp(str, "overwrite") == 0) {
		return AST_RECORD_IF_EXISTS_OVERWRITE;
	}

	if (strcasecmp(str, "append") == 0) {
		return AST_RECORD_IF_EXISTS_APPEND;
	}

	return -1;
}

static void recording_publish(struct stasis_app_recording *recording)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	ast_assert(recording != NULL);

	json = stasis_app_recording_to_json(recording);
	if (json == NULL) {
		return;
	}

	message = ast_channel_blob_create_from_cache(
		stasis_app_control_get_channel_id(recording->control),
		stasis_app_recording_snapshot_type(), json);
	if (message == NULL) {
		return;
	}

	stasis_app_control_publish(recording->control, message);
}

static void recording_fail(struct stasis_app_recording *recording)
{
	SCOPED_AO2LOCK(lock, recording);
	recording->state = STASIS_APP_RECORDING_STATE_FAILED;
	recording_publish(recording);
}

static void recording_cleanup(struct stasis_app_recording *recording)
{
	ao2_unlink_flags(recordings, recording,
		OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
}

static void *record_file(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_recording *, recording,
		NULL, recording_cleanup);
	char *acceptdtmf;
	int res;
	int duration = 0;

	recording = data;
	ast_assert(recording != NULL);

	ao2_lock(recording);
	recording->state = STASIS_APP_RECORDING_STATE_RECORDING;
	recording_publish(recording);
	ao2_unlock(recording);

	switch (recording->options->terminate_on) {
	case STASIS_APP_RECORDING_TERMINATE_NONE:
	case STASIS_APP_RECORDING_TERMINATE_INVALID:
		acceptdtmf = "";
		break;
	case STASIS_APP_RECORDING_TERMINATE_ANY:
		acceptdtmf = "#*0123456789abcd";
		break;
	default:
		acceptdtmf = ast_alloca(2);
		acceptdtmf[0] = recording->options->terminate_on;
		acceptdtmf[1] = '\0';
	}

	res = ast_auto_answer(chan);
	if (res != 0) {
		ast_debug(3, "%s: Failed to answer\n",
			ast_channel_uniqueid(chan));
		recording_fail(recording);
		return NULL;
	}

	ast_play_and_record_full(chan,
		recording->options->beep ? "beep" : NULL,
		recording->absolute_name,
		recording->options->max_duration_seconds,
		recording->options->format,
		&duration,
		NULL, /* sound_duration */
		-1, /* silencethreshold */
		recording->options->max_silence_seconds * 1000,
		NULL, /* path */
		acceptdtmf,
		NULL, /* canceldtmf */
		1, /* skip_confirmation_sound */
		recording->options->if_exists);

	ast_debug(3, "%s: Recording complete\n", ast_channel_uniqueid(chan));

	ao2_lock(recording);
	recording->state = STASIS_APP_RECORDING_STATE_COMPLETE;
	recording_publish(recording);
	ao2_unlock(recording);

	return NULL;
}

static void recording_dtor(void *obj)
{
	struct stasis_app_recording *recording = obj;

	ao2_cleanup(recording->options);
}

struct stasis_app_recording *stasis_app_control_record(
	struct stasis_app_control *control,
	struct stasis_app_recording_options *options)
{
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	char *last_slash;

	errno = 0;

	if (options == NULL ||
		ast_strlen_zero(options->name) ||
		ast_strlen_zero(options->format) ||
		options->max_silence_seconds < 0 ||
		options->max_duration_seconds < 0) {
		errno = EINVAL;
		return NULL;
	}

	ast_debug(3, "%s: Sending record(%s.%s) command\n",
		stasis_app_control_get_channel_id(control), options->name,
		options->format);

	recording = ao2_alloc(sizeof(*recording), recording_dtor);
	if (!recording) {
		errno = ENOMEM;
		return NULL;
	}

	ast_asprintf(&recording->absolute_name, "%s/%s",
		ast_config_AST_RECORDING_DIR, options->name);

	if (recording->absolute_name == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	if ((last_slash = strrchr(recording->absolute_name, '/'))) {
		*last_slash = '\0';
		if (ast_safe_mkdir(ast_config_AST_RECORDING_DIR,
				recording->absolute_name, 0777) != 0) {
			/* errno set by ast_mkdir */
			return NULL;
		}
		*last_slash = '/';
	}

	ao2_ref(options, +1);
	recording->options = options;
	recording->control = control;
	recording->state = STASIS_APP_RECORDING_STATE_QUEUED;

	{
		RAII_VAR(struct stasis_app_recording *, old_recording, NULL,
			ao2_cleanup);

		SCOPED_AO2LOCK(lock, recordings);

		old_recording = ao2_find(recordings, options->name,
			OBJ_KEY | OBJ_NOLOCK);
		if (old_recording) {
			ast_log(LOG_WARNING,
				"Recording %s already in progress\n",
				recording->options->name);
			errno = EEXIST;
			return NULL;
		}
		ao2_link(recordings, recording);
	}

	/* A ref is kept in the recordings container; no need to bump */
	stasis_app_send_command_async(control, record_file, recording);

	/* Although this should be bumped for the caller */
	ao2_ref(recording, +1);
	return recording;
}

enum stasis_app_recording_state stasis_app_recording_get_state(
	struct stasis_app_recording *recording)
{
	return recording->state;
}

const char *stasis_app_recording_get_name(
	struct stasis_app_recording *recording)
{
	return recording->options->name;
}

struct stasis_app_recording *stasis_app_recording_find_by_name(const char *name)
{
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);

	recording = ao2_find(recordings, name, OBJ_KEY);
	if (recording == NULL) {
		return NULL;
	}

	ao2_ref(recording, +1);
	return recording;
}

struct ast_json *stasis_app_recording_to_json(
	const struct stasis_app_recording *recording)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	if (recording == NULL) {
		return NULL;
	}

	json = ast_json_pack("{s: s, s: s, s: s}",
		"name", recording->options->name,
		"format", recording->options->format,
		"state", state_to_string(recording->state));

	return ast_json_ref(json);
}

enum stasis_app_recording_oper_results stasis_app_recording_operation(
	struct stasis_app_recording *recording,
	enum stasis_app_recording_media_operation operation)
{
	ast_assert(0); // TODO
	return STASIS_APP_RECORDING_OPER_FAILED;
}

static int load_module(void)
{
	int r;

	r = STASIS_MESSAGE_TYPE_INIT(stasis_app_recording_snapshot_type);
	if (r != 0) {
		return AST_MODULE_LOAD_FAILURE;
	}

	recordings = ao2_container_alloc(RECORDING_BUCKETS, recording_hash,
		recording_cmp);
	if (!recordings) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ao2_cleanup(recordings);
	recordings = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_app_recording_snapshot_type);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS,
	"Stasis application recording support",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis");
