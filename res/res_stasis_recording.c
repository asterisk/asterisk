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

/*! Container of all current recordings */
static struct ao2_container *recordings;

struct stasis_app_recording {
	/*! Recording options. */
	struct stasis_app_recording_options *options;
	/*! Absolute path (minus extension) of the recording */
	char *absolute_name;
	/*! Control object for the channel we're recording */
	struct stasis_app_control *control;
	/*! Current state of the recording. */
	enum stasis_app_recording_state state;
	/*! Duration calculations */
	struct {
		/*! Total duration */
		int total;
		/*! Duration minus any silence */
		int energy_only;
	} duration;
	/*! Indicates whether the recording is currently muted */
	int muted:1;
};

static struct ast_json *recording_to_json(struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct ast_json *blob = channel_blob->blob;
	const char *state =
		ast_json_string_get(ast_json_object_get(blob, "state"));
	const char *type;

	if (!strcmp(state, "recording")) {
		type = "RecordingStarted";
	} else if (!strcmp(state, "done") || !strcasecmp(state, "canceled")) {
		type = "RecordingFinished";
	} else if (!strcmp(state, "failed")) {
		type = "RecordingFailed";
	} else {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o?, s: O}",
		"type", type,
		"timestamp", ast_json_timeval(*stasis_message_timestamp(message), NULL),
		"recording", blob);
}

STASIS_MESSAGE_TYPE_DEFN(stasis_app_recording_snapshot_type,
	.to_json = recording_to_json,
);

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
	case STASIS_APP_RECORDING_STATE_CANCELED:
		return "canceled";
	case STASIS_APP_RECORDING_STATE_MAX:
		return "?";
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

	return AST_RECORD_IF_EXISTS_ERROR;
}

static void recording_publish(struct stasis_app_recording *recording, const char *cause)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	ast_assert(recording != NULL);

	json = stasis_app_recording_to_json(recording);
	if (json == NULL) {
		return;
	}

	if (!ast_strlen_zero(cause)) {
		struct ast_json *failure_cause = ast_json_string_create(cause);

		if (!failure_cause) {
			return;
		}

		if (ast_json_object_set(json, "cause", failure_cause)) {
			return;
		}
	}

	message = ast_channel_blob_create_from_cache(
		stasis_app_control_get_channel_id(recording->control),
		stasis_app_recording_snapshot_type(), json);
	if (message == NULL) {
		return;
	}

	stasis_app_control_publish(recording->control, message);
}


static void recording_set_state(struct stasis_app_recording *recording,
				enum stasis_app_recording_state state,
				const char *cause)
{
	SCOPED_AO2LOCK(lock, recording);
	recording->state = state;
	recording_publish(recording, cause);
}

static enum stasis_app_control_channel_result check_rule_recording(
	const struct stasis_app_control *control)
{
	return STASIS_APP_CHANNEL_RECORDING;
}

/*
 * XXX This only works because there is one and only one rule in
 * the system so it can be added to any number of channels
 * without issue.  However, as soon as there is another rule then
 * watch out for weirdness because of cross linked lists.
 */
static struct stasis_app_control_rule rule_recording = {
	.check_rule = check_rule_recording
};

static void recording_fail(struct stasis_app_control *control,
			   struct stasis_app_recording *recording,
			   const char *cause)
{
	stasis_app_control_unregister_add_rule(control, &rule_recording);

	recording_set_state(
		recording, STASIS_APP_RECORDING_STATE_FAILED, cause);
}

static void recording_cleanup(void *data)
{
	struct stasis_app_recording *recording = data;

	ao2_unlink_flags(recordings, recording,
		OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
	ao2_ref(recording, -1);
}

static int record_file(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct stasis_app_recording *recording = data;
	char *acceptdtmf;
	int res;

	ast_assert(recording != NULL);

	if (stasis_app_get_bridge(control)) {
		ast_log(LOG_ERROR, "Cannot record channel while in bridge\n");
		recording_fail(control, recording, "Cannot record channel while in bridge");
		return -1;
	}

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
		recording_fail(control, recording, "Failed to answer channel");
		return -1;
	}

	recording_set_state(
		recording, STASIS_APP_RECORDING_STATE_RECORDING, NULL);
	ast_play_and_record_full(chan,
		NULL, /* playfile */
		recording->absolute_name,
		recording->options->max_duration_seconds,
		recording->options->format,
		&recording->duration.total,
		recording->options->max_silence_seconds ? &recording->duration.energy_only : NULL,
		recording->options->beep,
		-1, /* silencethreshold */
		recording->options->max_silence_seconds * 1000,
		NULL, /* path */
		acceptdtmf,
		NULL, /* canceldtmf */
		1, /* skip_confirmation_sound */
		recording->options->if_exists);

	ast_debug(3, "%s: Recording complete\n", ast_channel_uniqueid(chan));

	recording_set_state(
		recording, STASIS_APP_RECORDING_STATE_COMPLETE, NULL);

	stasis_app_control_unregister_add_rule(control, &rule_recording);

	return 0;
}

static void recording_dtor(void *obj)
{
	struct stasis_app_recording *recording = obj;

	ast_free(recording->absolute_name);
	ao2_cleanup(recording->control);
	ao2_cleanup(recording->options);
}

struct stasis_app_recording *stasis_app_control_record(
	struct stasis_app_control *control,
	struct stasis_app_recording_options *options)
{
	struct stasis_app_recording *recording;
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
	recording->duration.total = -1;
	recording->duration.energy_only = -1;

	ast_asprintf(&recording->absolute_name, "%s/%s",
		ast_config_AST_RECORDING_DIR, options->name);

	if (recording->absolute_name == NULL) {
		errno = ENOMEM;
		ao2_ref(recording, -1);
		return NULL;
	}

	if ((last_slash = strrchr(recording->absolute_name, '/'))) {
		*last_slash = '\0';
		if (ast_safe_mkdir(ast_config_AST_RECORDING_DIR,
				recording->absolute_name, 0777) != 0) {
			/* errno set by ast_mkdir */
			ao2_ref(recording, -1);
			return NULL;
		}
		*last_slash = '/';
	}

	ao2_ref(options, +1);
	recording->options = options;
	ao2_ref(control, +1);
	recording->control = control;
	recording->state = STASIS_APP_RECORDING_STATE_QUEUED;

	if ((recording->options->if_exists == AST_RECORD_IF_EXISTS_FAIL) &&
			(ast_fileexists(recording->absolute_name, NULL, NULL))) {
		ast_log(LOG_WARNING, "Recording file '%s' already exists and ifExists option is failure.\n",
			recording->absolute_name);
		errno = EEXIST;
		ao2_ref(recording, -1);
		return NULL;
	}

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
			ao2_ref(recording, -1);
			return NULL;
		}
		ao2_link(recordings, recording);
	}

	stasis_app_control_register_add_rule(control, &rule_recording);

	stasis_app_send_command_async(control, record_file, ao2_bump(recording), recording_cleanup);

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
	return ao2_find(recordings, name, OBJ_KEY);
}

struct ast_json *stasis_app_recording_to_json(
	const struct stasis_app_recording *recording)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	if (recording == NULL) {
		return NULL;
	}

	json = ast_json_pack("{s: s, s: s, s: s, s: s}",
		"name", recording->options->name,
		"format", recording->options->format,
		"state", state_to_string(recording->state),
		"target_uri", recording->options->target);
	if (json && recording->duration.total > -1) {
		ast_json_object_set(json, "duration",
			ast_json_integer_create(recording->duration.total));
	}
	if (json && recording->duration.energy_only > -1) {
		ast_json_object_set(json, "talking_duration",
			ast_json_integer_create(recording->duration.energy_only));
		ast_json_object_set(json, "silence_duration",
			ast_json_integer_create(recording->duration.total - recording->duration.energy_only));
	}

	return ast_json_ref(json);
}

typedef int (*recording_operation_cb)(struct stasis_app_recording *recording);

static int recording_noop(struct stasis_app_recording *recording)
{
	return 0;
}

static int recording_disregard(struct stasis_app_recording *recording)
{
	recording->state = STASIS_APP_RECORDING_STATE_CANCELED;
	return 0;
}

static int recording_cancel(struct stasis_app_recording *recording)
{
	int res = 0;
	recording->state = STASIS_APP_RECORDING_STATE_CANCELED;
	res |= stasis_app_control_queue_control(recording->control,
		AST_CONTROL_RECORD_CANCEL);
	res |= ast_filedelete(recording->absolute_name, NULL);
	return res;
}

static int recording_stop(struct stasis_app_recording *recording)
{
	recording->state = STASIS_APP_RECORDING_STATE_COMPLETE;
	return stasis_app_control_queue_control(recording->control,
		AST_CONTROL_RECORD_STOP);
}

static int recording_pause(struct stasis_app_recording *recording)
{
	recording->state = STASIS_APP_RECORDING_STATE_PAUSED;
	return stasis_app_control_queue_control(recording->control,
		AST_CONTROL_RECORD_SUSPEND);
}

static int recording_unpause(struct stasis_app_recording *recording)
{
	recording->state = STASIS_APP_RECORDING_STATE_RECORDING;
	return stasis_app_control_queue_control(recording->control,
		AST_CONTROL_RECORD_SUSPEND);
}

static int toggle_recording_mute(struct stasis_app_recording *recording, int desired_mute_state)
{
	if (recording->muted == desired_mute_state) {
		/* already in desired state */
		return 0;
	}

	recording->muted = desired_mute_state;

	return stasis_app_control_queue_control(recording->control,
		AST_CONTROL_RECORD_MUTE);
}

static int recording_mute(struct stasis_app_recording *recording)
{
	return toggle_recording_mute(recording, 1);
}

static int recording_unmute(struct stasis_app_recording *recording)
{
	return toggle_recording_mute(recording, 0);
}

recording_operation_cb operations[STASIS_APP_RECORDING_STATE_MAX][STASIS_APP_RECORDING_OPER_MAX] = {
	[STASIS_APP_RECORDING_STATE_QUEUED][STASIS_APP_RECORDING_CANCEL] = recording_disregard,
	[STASIS_APP_RECORDING_STATE_QUEUED][STASIS_APP_RECORDING_STOP] = recording_disregard,
	[STASIS_APP_RECORDING_STATE_RECORDING][STASIS_APP_RECORDING_CANCEL] = recording_cancel,
	[STASIS_APP_RECORDING_STATE_RECORDING][STASIS_APP_RECORDING_STOP] = recording_stop,
	[STASIS_APP_RECORDING_STATE_RECORDING][STASIS_APP_RECORDING_PAUSE] = recording_pause,
	[STASIS_APP_RECORDING_STATE_RECORDING][STASIS_APP_RECORDING_UNPAUSE] = recording_noop,
	[STASIS_APP_RECORDING_STATE_RECORDING][STASIS_APP_RECORDING_MUTE] = recording_mute,
	[STASIS_APP_RECORDING_STATE_RECORDING][STASIS_APP_RECORDING_UNMUTE] = recording_unmute,
	[STASIS_APP_RECORDING_STATE_PAUSED][STASIS_APP_RECORDING_CANCEL] = recording_cancel,
	[STASIS_APP_RECORDING_STATE_PAUSED][STASIS_APP_RECORDING_STOP] = recording_stop,
	[STASIS_APP_RECORDING_STATE_PAUSED][STASIS_APP_RECORDING_PAUSE] = recording_noop,
	[STASIS_APP_RECORDING_STATE_PAUSED][STASIS_APP_RECORDING_UNPAUSE] = recording_unpause,
	[STASIS_APP_RECORDING_STATE_PAUSED][STASIS_APP_RECORDING_MUTE] = recording_mute,
	[STASIS_APP_RECORDING_STATE_PAUSED][STASIS_APP_RECORDING_UNMUTE] = recording_unmute,
};

enum stasis_app_recording_oper_results stasis_app_recording_operation(
	struct stasis_app_recording *recording,
	enum stasis_app_recording_media_operation operation)
{
	recording_operation_cb cb;
	SCOPED_AO2LOCK(lock, recording);

	if ((unsigned int)recording->state >= STASIS_APP_RECORDING_STATE_MAX) {
		ast_log(LOG_WARNING, "Invalid recording state %u\n",
			recording->state);
		return -1;
	}

	if ((unsigned int)operation >= STASIS_APP_RECORDING_OPER_MAX) {
		ast_log(LOG_WARNING, "Invalid recording operation %u\n",
			operation);
		return -1;
	}

	cb = operations[recording->state][operation];

	if (!cb) {
		if (recording->state != STASIS_APP_RECORDING_STATE_RECORDING) {
			/* So we can be specific in our error message. */
			return STASIS_APP_RECORDING_OPER_NOT_RECORDING;
		} else {
			/* And, really, all operations should be valid during
			 * recording */
			ast_log(LOG_ERROR,
				"Unhandled operation during recording: %u\n",
				operation);
			return STASIS_APP_RECORDING_OPER_FAILED;
		}
	}

	return cb(recording) ?
		STASIS_APP_RECORDING_OPER_FAILED : STASIS_APP_RECORDING_OPER_OK;
}

static int load_module(void)
{
	int r;

	r = STASIS_MESSAGE_TYPE_INIT(stasis_app_recording_snapshot_type);
	if (r != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	recordings = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, RECORDING_BUCKETS,
		recording_hash, NULL, recording_cmp);
	if (!recordings) {
		STASIS_MESSAGE_TYPE_CLEANUP(stasis_app_recording_snapshot_type);
		return AST_MODULE_LOAD_DECLINE;
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Stasis application recording support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_stasis",
	.load_pri = AST_MODPRI_APP_DEPEND
);
