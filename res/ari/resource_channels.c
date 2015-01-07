/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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
 * \brief Implementation for ARI stubs.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis_app_playback</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/bridge.h"
#include "asterisk/callerid.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_app_playback.h"
#include "asterisk/stasis_app_recording.h"
#include "asterisk/stasis_app_snoop.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/causes.h"
#include "asterisk/format_cache.h"
#include "asterisk/core_local.h"
#include "asterisk/dial.h"
#include "resource_channels.h"

#include <limits.h>

/*!
 * \brief Finds the control object for a channel, filling the response with an
 * error, if appropriate.
 * \param[out] response Response to fill with an error if control is not found.
 * \param channel_id ID of the channel to lookup.
 * \return Channel control object.
 * \return \c NULL if control object does not exist.
 */
static struct stasis_app_control *find_control(
	struct ast_ari_response *response,
	const char *channel_id)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	control = stasis_app_control_find_by_channel_id(channel_id);
	if (control == NULL) {
		/* Distinguish between 404 and 409 errors */
		RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);
		chan = ast_channel_get_by_name(channel_id);
		if (chan == NULL) {
			ast_ari_response_error(response, 404, "Not Found",
				   "Channel not found");
			return NULL;
		}

		ast_ari_response_error(response, 409, "Conflict",
			   "Channel not in Stasis application");
		return NULL;
	}

	ao2_ref(control, +1);
	return control;
}

void ast_ari_channels_continue_in_dialplan(
	struct ast_variable *headers,
	struct ast_ari_channels_continue_in_dialplan_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	int ipri;
	const char *context;
	const char *exten;

	ast_assert(response != NULL);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	snapshot = stasis_app_control_get_snapshot(control);
	if (!snapshot) {
		return;
	}

	if (ast_strlen_zero(args->context)) {
		context = snapshot->context;
		exten = S_OR(args->extension, snapshot->exten);
	} else {
		context = args->context;
		exten = S_OR(args->extension, "s");
	}

	if (!ast_strlen_zero(args->label)) {
		/* A label was provided in the request, use that */

		if (sscanf(args->label, "%30d", &ipri) != 1) {
			ipri = ast_findlabel_extension(NULL, context, exten, args->label, NULL);
			if (ipri == -1) {
				ast_log(AST_LOG_ERROR, "Requested label: %s can not be found in context: %s\n", args->label, context);
				ast_ari_response_error(response, 404, "Not Found", "Requested label can not be found");
				return;
			}
		} else {
			ast_debug(3, "Numeric value provided for label, jumping to that priority\n");
		}

		if (ipri == 0) {
			ast_log(AST_LOG_ERROR, "Invalid priority label '%s' specified for extension %s in context: %s\n",
					args->label, exten, context);
			ast_ari_response_error(response, 400, "Bad Request", "Requested priority is illegal");
			return;
		}

	} else if (args->priority) {
		/* No label provided, use provided priority */
		ipri = args->priority;
	} else if (ast_strlen_zero(args->context) && ast_strlen_zero(args->extension)) {
		/* Special case. No exten, context, or priority provided, then move on to the next priority */
		ipri = snapshot->priority + 1;
	} else {
		ipri = 1;
	}


	if (stasis_app_control_continue(control, context, exten, ipri)) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_channels_answer(struct ast_variable *headers,
	struct ast_ari_channels_answer_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	if (stasis_app_control_answer(control) != 0) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Failed to answer channel");
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_channels_ring(struct ast_variable *headers,
	struct ast_ari_channels_ring_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	stasis_app_control_ring(control);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_ring_stop(struct ast_variable *headers,
	struct ast_ari_channels_ring_stop_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	stasis_app_control_ring_stop(control);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_mute(struct ast_variable *headers,
	struct ast_ari_channels_mute_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	unsigned int direction = 0;
	enum ast_frame_type frametype = AST_FRAME_VOICE;

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	if (ast_strlen_zero(args->direction)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Direction is required");
		return;
	}

	if (!strcmp(args->direction, "in")) {
		direction = AST_MUTE_DIRECTION_READ;
	} else if (!strcmp(args->direction, "out")) {
		direction = AST_MUTE_DIRECTION_WRITE;
	} else if (!strcmp(args->direction, "both")) {
		direction = AST_MUTE_DIRECTION_READ | AST_MUTE_DIRECTION_WRITE;
	} else {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Invalid direction specified");
		return;
	}

	stasis_app_control_mute(control, direction, frametype);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_unmute(struct ast_variable *headers,
	struct ast_ari_channels_unmute_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	unsigned int direction = 0;
	enum ast_frame_type frametype = AST_FRAME_VOICE;

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	if (ast_strlen_zero(args->direction)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Direction is required");
		return;
	}

	if (!strcmp(args->direction, "in")) {
		direction = AST_MUTE_DIRECTION_READ;
	} else if (!strcmp(args->direction, "out")) {
		direction = AST_MUTE_DIRECTION_WRITE;
	} else if (!strcmp(args->direction, "both")) {
		direction = AST_MUTE_DIRECTION_READ | AST_MUTE_DIRECTION_WRITE;
	} else {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Invalid direction specified");
		return;
	}

	stasis_app_control_unmute(control, direction, frametype);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_send_dtmf(struct ast_variable *headers,
	struct ast_ari_channels_send_dtmf_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	if (ast_strlen_zero(args->dtmf)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"DTMF is required");
		return;
	}

	stasis_app_control_dtmf(control, args->dtmf, args->before, args->between, args->duration, args->after);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_hold(struct ast_variable *headers,
	struct ast_ari_channels_hold_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	stasis_app_control_hold(control);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_unhold(struct ast_variable *headers,
	struct ast_ari_channels_unhold_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	stasis_app_control_unhold(control);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_start_moh(struct ast_variable *headers,
	struct ast_ari_channels_start_moh_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	stasis_app_control_moh_start(control, args->moh_class);
	ast_ari_response_no_content(response);
}

void ast_ari_channels_stop_moh(struct ast_variable *headers,
	struct ast_ari_channels_stop_moh_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	stasis_app_control_moh_stop(control);
	ast_ari_response_no_content(response);
}

void ast_ari_channels_start_silence(struct ast_variable *headers,
	struct ast_ari_channels_start_silence_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	stasis_app_control_silence_start(control);
	ast_ari_response_no_content(response);
}

void ast_ari_channels_stop_silence(struct ast_variable *headers,
	struct ast_ari_channels_stop_silence_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	stasis_app_control_silence_stop(control);
	ast_ari_response_no_content(response);
}

static void ari_channels_handle_play(
	const char *args_channel_id,
	const char *args_media,
	const char *args_lang,
	int args_offsetms,
	int args_skipms,
	const char *args_playback_id,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	RAII_VAR(char *, playback_url, NULL, ast_free);
	struct ast_json *json;
	const char *language;

	ast_assert(response != NULL);

	control = find_control(response, args_channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	snapshot = stasis_app_control_get_snapshot(control);
	if (!snapshot) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	if (args_skipms < 0) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"skipms cannot be negative");
		return;
	}

	if (args_offsetms < 0) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"offsetms cannot be negative");
		return;
	}

	language = S_OR(args_lang, snapshot->language);

	playback = stasis_app_control_play_uri(control, args_media, language,
		args_channel_id, STASIS_PLAYBACK_TARGET_CHANNEL, args_skipms, args_offsetms, args_playback_id);
	if (!playback) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Failed to queue media for playback");
		return;
	}

	if (ast_asprintf(&playback_url, "/playback/%s",
			stasis_app_playback_get_id(playback)) == -1) {
		playback_url = NULL;
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}

	json = stasis_app_playback_to_json(playback);
	if (!json) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}

	ast_ari_response_created(response, playback_url, json);
}

void ast_ari_channels_play(struct ast_variable *headers,
	struct ast_ari_channels_play_args *args,
	struct ast_ari_response *response)
{
	ari_channels_handle_play(
		args->channel_id,
		args->media,
		args->lang,
		args->offsetms,
		args->skipms,
		args->playback_id,
		response);
}

void ast_ari_channels_play_with_id(struct ast_variable *headers,
	struct ast_ari_channels_play_with_id_args *args,
	struct ast_ari_response *response)
{
	ari_channels_handle_play(
		args->channel_id,
		args->media,
		args->lang,
		args->offsetms,
		args->skipms,
		args->playback_id,
		response);
}

void ast_ari_channels_record(struct ast_variable *headers,
	struct ast_ari_channels_record_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	RAII_VAR(char *, recording_url, NULL, ast_free);
	struct ast_json *json;
	RAII_VAR(struct stasis_app_recording_options *, options, NULL,
		ao2_cleanup);
	RAII_VAR(char *, uri_encoded_name, NULL, ast_free);
	size_t uri_name_maxlen;

	ast_assert(response != NULL);

	if (args->max_duration_seconds < 0) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"max_duration_seconds cannot be negative");
		return;
	}

	if (args->max_silence_seconds < 0) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"max_silence_seconds cannot be negative");
		return;
	}

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	options = stasis_app_recording_options_create(args->name, args->format);
	if (options == NULL) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
	}
	ast_string_field_build(options, target, "channel:%s", args->channel_id);
	options->max_silence_seconds = args->max_silence_seconds;
	options->max_duration_seconds = args->max_duration_seconds;
	options->terminate_on =
		stasis_app_recording_termination_parse(args->terminate_on);
	options->if_exists =
		stasis_app_recording_if_exists_parse(args->if_exists);
	options->beep = args->beep;

	if (options->terminate_on == STASIS_APP_RECORDING_TERMINATE_INVALID) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"terminateOn invalid");
		return;
	}

	if (options->if_exists == -1) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"ifExists invalid");
		return;
	}

	if (!ast_get_format_for_file_ext(options->format)) {
		ast_ari_response_error(
			response, 422, "Unprocessable Entity",
			"specified format is unknown on this system");
		return;
	}

	recording = stasis_app_control_record(control, options);
	if (recording == NULL) {
		switch(errno) {
		case EINVAL:
			/* While the arguments are invalid, we should have
			 * caught them prior to calling record.
			 */
			ast_ari_response_error(
				response, 500, "Internal Server Error",
				"Error parsing request");
			break;
		case EEXIST:
			ast_ari_response_error(response, 409, "Conflict",
				"Recording '%s' already exists and can not be overwritten",
				args->name);
			break;
		case ENOMEM:
			ast_ari_response_error(
				response, 500, "Internal Server Error",
				"Out of memory");
			break;
		case EPERM:
			ast_ari_response_error(
				response, 400, "Bad Request",
				"Recording name invalid");
			break;
		default:
			ast_log(LOG_WARNING,
				"Unrecognized recording error: %s\n",
				strerror(errno));
			ast_ari_response_error(
				response, 500, "Internal Server Error",
				"Internal Server Error");
			break;
		}
		return;
	}

	uri_name_maxlen = strlen(args->name) * 3;
	uri_encoded_name = ast_malloc(uri_name_maxlen);
	if (!uri_encoded_name) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}
	ast_uri_encode(args->name, uri_encoded_name, uri_name_maxlen,
		ast_uri_http);

	if (ast_asprintf(&recording_url, "/recordings/live/%s",
			uri_encoded_name) == -1) {
		recording_url = NULL;
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}

	json = stasis_app_recording_to_json(recording);
	if (!json) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}

	ast_ari_response_created(response, recording_url, json);
}

void ast_ari_channels_get(struct ast_variable *headers,
	struct ast_ari_channels_get_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct stasis_cache *cache;
	struct ast_channel_snapshot *snapshot;

	cache = ast_channel_cache();
	if (!cache) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}

	msg = stasis_cache_get(cache, ast_channel_snapshot_type(),
				   args->channel_id);
	if (!msg) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	snapshot = stasis_message_data(msg);
	ast_assert(snapshot != NULL);

	ast_ari_response_ok(response,
				ast_channel_snapshot_to_json(snapshot, NULL));
}

void ast_ari_channels_hangup(struct ast_variable *headers,
	struct ast_ari_channels_hangup_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);
	int cause;

	chan = ast_channel_get_by_name(args->channel_id);
	if (chan == NULL) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	if (ast_strlen_zero(args->reason) || !strcmp(args->reason, "normal")) {
		cause = AST_CAUSE_NORMAL;
	} else if (!strcmp(args->reason, "busy")) {
		cause = AST_CAUSE_BUSY;
	} else if (!strcmp(args->reason, "congestion")) {
		cause = AST_CAUSE_CONGESTION;
	} else {
		ast_ari_response_error(
			response, 400, "Invalid Reason",
			"Invalid reason for hangup provided");
		return;
	}

	ast_channel_hangupcause_set(chan, cause);
	ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_list(struct ast_variable *headers,
	struct ast_ari_channels_list_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, snapshots, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;
	struct stasis_message_sanitizer *sanitize = stasis_app_get_sanitizer();

	cache = ast_channel_cache();
	if (!cache) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}
	ao2_ref(cache, +1);

	snapshots = stasis_cache_dump(cache, ast_channel_snapshot_type());
	if (!snapshots) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	json = ast_json_array_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(snapshots, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, msg, obj, ao2_cleanup);
		struct ast_channel_snapshot *snapshot = stasis_message_data(msg);
		int r;

		if (sanitize && sanitize->channel_snapshot
			&& sanitize->channel_snapshot(snapshot)) {
			continue;
		}

		r = ast_json_array_append(
			json, ast_channel_snapshot_to_json(snapshot, NULL));
		if (r != 0) {
			ast_ari_response_alloc_failed(response);
			ao2_iterator_destroy(&i);
			return;
		}
	}
	ao2_iterator_destroy(&i);

	ast_ari_response_ok(response, ast_json_ref(json));
}

/*! \brief Structure used for origination */
struct ari_origination {
	/*! \brief Dialplan context */
	char context[AST_MAX_CONTEXT];
	/*! \brief Dialplan extension */
	char exten[AST_MAX_EXTENSION];
	/*! \brief Dialplan priority */
	int priority;
	/*! \brief Application data to pass to Stasis application */
	char appdata[0];
};

/*! \brief Thread which dials and executes upon answer */
static void *ari_originate_dial(void *data)
{
	struct ast_dial *dial = data;
	struct ari_origination *origination = ast_dial_get_user_data(dial);
	enum ast_dial_result res;

	res = ast_dial_run(dial, NULL, 0);
	if (res != AST_DIAL_RESULT_ANSWERED) {
		goto end;
	}

	if (!ast_strlen_zero(origination->appdata)) {
		struct ast_app *app = pbx_findapp("Stasis");

		if (app) {
			ast_verb(4, "Launching Stasis(%s) on %s\n", origination->appdata,
				ast_channel_name(ast_dial_answered(dial)));
			pbx_exec(ast_dial_answered(dial), app, origination->appdata);
		} else {
			ast_log(LOG_WARNING, "No such application 'Stasis'\n");
		}
	} else {
		struct ast_channel *answered = ast_dial_answered(dial);

		if (!ast_strlen_zero(origination->context)) {
			ast_channel_context_set(answered, origination->context);
		}

		if (!ast_strlen_zero(origination->exten)) {
			ast_channel_exten_set(answered, origination->exten);
		}

		if (origination->priority > 0) {
			ast_channel_priority_set(answered, origination->priority);
		}

		if (ast_pbx_run(answered)) {
			ast_log(LOG_ERROR, "Failed to start PBX on %s\n", ast_channel_name(answered));
		} else {
			/* PBX will have taken care of hanging up, so we steal the answered channel so dial doesn't do it */
			ast_dial_answered_steal(dial);
		}
	}

end:
	ast_dial_destroy(dial);
	ast_free(origination);
	return NULL;
}

static void ari_channels_handle_originate_with_id(const char *args_endpoint,
	const char *args_extension,
	const char *args_context,
	long args_priority,
	const char *args_label,
	const char *args_app,
	const char *args_app_args,
	const char *args_caller_id,
	int args_timeout,
	struct ast_variable *variables,
	const char *args_channel_id,
	const char *args_other_channel_id,
	const char *args_originator,
	struct ast_ari_response *response)
{
	char *dialtech;
	char dialdevice[AST_CHANNEL_NAME];
	struct ast_dial *dial;
	char *caller_id = NULL;
	char *cid_num = NULL;
	char *cid_name = NULL;
	RAII_VAR(struct ast_format_cap *, cap,
		ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	char *stuff;
	struct ast_channel *other = NULL;
	struct ast_channel *chan = NULL;
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	struct ast_assigned_ids assignedids = {
		.uniqueid = args_channel_id,
		.uniqueid2 = args_other_channel_id,
	};
	struct ari_origination *origination;
	pthread_t thread;

	if (!cap) {
		ast_ari_response_alloc_failed(response);
		return;
	}
	ast_format_cap_append(cap, ast_format_slin, 0);

	if ((assignedids.uniqueid && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid))
		|| (assignedids.uniqueid2 && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid2))) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Uniqueid length exceeds maximum of %d", AST_MAX_PUBLIC_UNIQUEID);
		return;
	}

	if (ast_strlen_zero(args_endpoint)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Endpoint must be specified");
		return;
	}

	dialtech = ast_strdupa(args_endpoint);
	if ((stuff = strchr(dialtech, '/'))) {
		*stuff++ = '\0';
		ast_copy_string(dialdevice, stuff, sizeof(dialdevice));
	}

	if (ast_strlen_zero(dialtech) || ast_strlen_zero(dialdevice)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid endpoint specified");
		return;
	}

	if (!ast_strlen_zero(args_app)) {
		RAII_VAR(struct ast_str *, appdata, ast_str_create(64), ast_free);

		if (!appdata) {
			ast_ari_response_alloc_failed(response);
			return;
		}

		ast_str_set(&appdata, 0, "%s", args_app);
		if (!ast_strlen_zero(args_app_args)) {
			ast_str_append(&appdata, 0, ",%s", args_app_args);
		}

		origination = ast_calloc(1, sizeof(*origination) + ast_str_size(appdata) + 1);
		if (!origination) {
			ast_ari_response_alloc_failed(response);
			return;
		}

		strcpy(origination->appdata, ast_str_buffer(appdata));
	} else if (!ast_strlen_zero(args_extension)) {
		origination = ast_calloc(1, sizeof(*origination) + 1);
		if (!origination) {
			ast_ari_response_alloc_failed(response);
			return;
		}

		ast_copy_string(origination->context, S_OR(args_context, "default"), sizeof(origination->context));
		ast_copy_string(origination->exten, args_extension, sizeof(origination->exten));

		if (!ast_strlen_zero(args_label)) {
			/* A label was provided in the request, use that */
			int ipri = 1;
			if (sscanf(args_label, "%30d", &ipri) != 1) {
				ipri = ast_findlabel_extension(chan, origination->context, origination->exten, args_label, args_caller_id);

				if (ipri == -1) {
					ast_log(AST_LOG_ERROR, "Requested label: %s can not be found in context: %s\n", args_label, args_context);
					ast_ari_response_error(response, 404, "Not Found", "Requested label can not be found");
					return;
				}
			} else {
				ast_debug(3, "Numeric value provided for label, jumping to that priority\n");
			}

			if (ipri == 0) {
				ast_log(AST_LOG_ERROR, "Invalid priority label '%s' specified for extension %s in context: %s\n",
						args_label, args_extension, args_context);
				ast_ari_response_error(response, 400, "Bad Request", "Requested priority is illegal");
				return;
			}

			/* Our priority was provided by a label */
			origination->priority =  ipri;
		} else {
			/* No label provided, use provided priority */
			origination->priority = args_priority ? args_priority : 1;
		}

		origination->appdata[0] = '\0';
	} else {
		ast_ari_response_error(response, 400, "Bad Request",
			"Application or extension must be specified");
		return;
	}

	dial = ast_dial_create();
	if (!dial) {
		ast_ari_response_alloc_failed(response);
		ast_free(origination);
		return;
	}
	ast_dial_set_user_data(dial, origination);

	if (ast_dial_append(dial, dialtech, dialdevice, &assignedids)) {
		ast_ari_response_alloc_failed(response);
		ast_dial_destroy(dial);
		ast_free(origination);
		return;
	}

	if (args_timeout > 0) {
		ast_dial_set_global_timeout(dial, args_timeout * 1000);
	} else if (args_timeout == -1) {
		ast_dial_set_global_timeout(dial, -1);
	} else {
		ast_dial_set_global_timeout(dial, 30000);
	}

	if (!ast_strlen_zero(args_caller_id)) {
		caller_id = ast_strdupa(args_caller_id);
		ast_callerid_parse(caller_id, &cid_name, &cid_num);

		if (ast_is_shrinkable_phonenumber(cid_num)) {
			ast_shrink_phone_number(cid_num);
		}
	}

	if (!ast_strlen_zero(args_originator)) {
		other = ast_channel_get_by_name(args_originator);
		if (!other) {
			ast_ari_response_error(
				response, 400, "Bad Request",
				"Provided originator channel was not found");
			ast_dial_destroy(dial);
			ast_free(origination);
			return;
		}
	}

	if (ast_dial_prerun(dial, other, cap)) {
		ast_ari_response_alloc_failed(response);
		ast_dial_destroy(dial);
		ast_free(origination);
		ast_channel_cleanup(other);
		return;
	}

	ast_channel_cleanup(other);

	chan = ast_dial_get_channel(dial, 0);
	if (!chan) {
		ast_ari_response_alloc_failed(response);
		ast_dial_destroy(dial);
		ast_free(origination);
		return;
	}

	if (!ast_strlen_zero(cid_num) || !ast_strlen_zero(cid_name)) {
		struct ast_party_connected_line connected;

		/*
		 * It seems strange to set the CallerID on an outgoing call leg
		 * to whom we are calling, but this function's callers are doing
		 * various Originate methods.  This call leg goes to the local
		 * user.  Once the called party answers, the dialplan needs to
		 * be able to access the CallerID from the CALLERID function as
		 * if the called party had placed this call.
		 */
		ast_set_callerid(chan, cid_num, cid_name, cid_num);

		ast_party_connected_line_set_init(&connected, ast_channel_connected(chan));
		if (!ast_strlen_zero(cid_num)) {
			connected.id.number.valid = 1;
			connected.id.number.str = (char *) cid_num;
			connected.id.number.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		}
		if (!ast_strlen_zero(cid_name)) {
			connected.id.name.valid = 1;
			connected.id.name.str = (char *) cid_name;
			connected.id.name.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		}
		ast_channel_set_connected_line(chan, &connected, NULL);
	}

	ast_channel_lock(chan);
	if (variables) {
		ast_set_variables(chan, variables);
	}
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_ORIGINATED);

	if (!ast_strlen_zero(args_app)) {
		struct ast_channel *local_peer;

		stasis_app_subscribe_channel(args_app, chan);

		/* Subscribe to the Local channel peer also. */
		local_peer = ast_local_get_peer(chan);
		if (local_peer) {
			stasis_app_subscribe_channel(args_app, local_peer);
			ast_channel_unref(local_peer);
		}
	}

	snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan));
	ast_channel_unlock(chan);

	/* Before starting the async dial bump the ref in case the dial quickly goes away and takes
	 * the reference with it
	 */
	ast_channel_ref(chan);

	if (ast_pthread_create_detached(&thread, NULL, ari_originate_dial, dial)) {
		ast_ari_response_alloc_failed(response);
		ast_dial_destroy(dial);
		ast_free(origination);
	} else {
		ast_ari_response_ok(response, ast_channel_snapshot_to_json(snapshot, NULL));
	}

	ast_channel_unref(chan);
	return;
}

void ast_ari_channels_originate_with_id(struct ast_variable *headers,
	struct ast_ari_channels_originate_with_id_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_variable *, variables, NULL, ast_variables_destroy);

	/* Parse any query parameters out of the body parameter */
	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_channels_originate_with_id_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables) {
			if (ast_json_to_ast_variables(json_variables, &variables)) {
				ast_log(AST_LOG_ERROR, "Unable to convert 'variables' in JSON body to channel variables\n");
				ast_ari_response_alloc_failed(response);
				return;
			}
		}
	}

	ari_channels_handle_originate_with_id(
		args->endpoint,
		args->extension,
		args->context,
		args->priority,
		args->label,
		args->app,
		args->app_args,
		args->caller_id,
		args->timeout,
		variables,
		args->channel_id,
		args->other_channel_id,
		args->originator,
		response);
}

void ast_ari_channels_originate(struct ast_variable *headers,
	struct ast_ari_channels_originate_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_variable *, variables, NULL, ast_variables_destroy);

	/* Parse any query parameters out of the body parameter */
	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_channels_originate_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables) {
			if (ast_json_to_ast_variables(json_variables, &variables)) {
				ast_log(AST_LOG_ERROR, "Unable to convert 'variables' in JSON body to channel variables\n");
				ast_ari_response_alloc_failed(response);
				return;
			}
		}
	}

	ari_channels_handle_originate_with_id(
		args->endpoint,
		args->extension,
		args->context,
		args->priority,
		args->label,
		args->app,
		args->app_args,
		args->caller_id,
		args->timeout,
		variables,
		args->channel_id,
		args->other_channel_id,
		args->originator,
		response);
}

void ast_ari_channels_get_channel_var(struct ast_variable *headers,
	struct ast_ari_channels_get_channel_var_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, value, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_channel *, channel, NULL, ast_channel_cleanup);

	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	if (ast_strlen_zero(args->channel_id)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Channel ID is required");
		return;
	}

	channel = ast_channel_get_by_name(args->channel_id);
	if (!channel) {
		ast_ari_response_error(
			response, 404, "Channel Not Found",
			"Provided channel was not found");
		return;
	}

	/* You may be tempted to lock the channel you're about to read from. You
	 * would be wrong. Some dialplan functions put the channel into
	 * autoservice, which deadlocks if the channel is already locked.
	 * ast_str_retrieve_variable() does its own locking, and the dialplan
	 * functions need to as well. We should be fine without the lock.
	 */

	if (args->variable[strlen(args->variable) - 1] == ')') {
		if (ast_func_read2(channel, args->variable, &value, 0)) {
			ast_ari_response_error(
				response, 500, "Error With Function",
				"Unable to read provided function");
			return;
		}
	} else {
		if (!ast_str_retrieve_variable(&value, 0, channel, NULL, args->variable)) {
			ast_ari_response_alloc_failed(response);
			return;
		}
	}

	if (!(json = ast_json_pack("{s: s}", "value", S_OR(ast_str_buffer(value), "")))) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_channels_set_channel_var(struct ast_variable *headers,
	struct ast_ari_channels_set_channel_var_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* response filled in by find_control */
		return;
	}

	if (stasis_app_control_set_channel_var(control, args->variable, args->value)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Failed to execute function");
		return;
	}

	ast_ari_response_no_content(response);
}

static void ari_channels_handle_snoop_channel(
	const char *args_channel_id,
	const char *args_spy,
	const char *args_whisper,
	const char *args_app,
	const char *args_app_args,
	const char *args_snoop_id,
	struct ast_ari_response *response)
{
	enum stasis_app_snoop_direction spy, whisper;
	RAII_VAR(struct ast_channel *, chan, NULL, ast_channel_cleanup);
	RAII_VAR(struct ast_channel *, snoop, NULL, ast_channel_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	if (ast_strlen_zero(args_spy) || !strcmp(args_spy, "none")) {
		spy = STASIS_SNOOP_DIRECTION_NONE;
	} else if (!strcmp(args_spy, "both")) {
		spy = STASIS_SNOOP_DIRECTION_BOTH;
	} else if (!strcmp(args_spy, "out")) {
		spy = STASIS_SNOOP_DIRECTION_OUT;
	} else if (!strcmp(args_spy, "in")) {
		spy = STASIS_SNOOP_DIRECTION_IN;
	} else {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Invalid direction specified for spy");
		return;
	}

	if (ast_strlen_zero(args_whisper) || !strcmp(args_whisper, "none")) {
		whisper = STASIS_SNOOP_DIRECTION_NONE;
	} else if (!strcmp(args_whisper, "both")) {
		whisper = STASIS_SNOOP_DIRECTION_BOTH;
	} else if (!strcmp(args_whisper, "out")) {
		whisper = STASIS_SNOOP_DIRECTION_OUT;
	} else if (!strcmp(args_whisper, "in")) {
		whisper = STASIS_SNOOP_DIRECTION_IN;
	} else {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Invalid direction specified for whisper");
		return;
	}

	if (spy == STASIS_SNOOP_DIRECTION_NONE && whisper == STASIS_SNOOP_DIRECTION_NONE) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Direction must be specified for at least spy or whisper");
		return;
	} else if (ast_strlen_zero(args_app)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Application name is required");
		return;
	}

	chan = ast_channel_get_by_name(args_channel_id);
	if (chan == NULL) {
		ast_ari_response_error(
			response, 404, "Channel Not Found",
			"Provided channel was not found");
		return;
	}

	snoop = stasis_app_control_snoop(chan, spy, whisper, args_app, args_app_args,
		args_snoop_id);
	if (snoop == NULL) {
		ast_ari_response_error(
			response, 500, "Internal error",
			"Snoop channel could not be created");
		return;
	}

	snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(snoop));
	ast_ari_response_ok(response, ast_channel_snapshot_to_json(snapshot, NULL));
}

void ast_ari_channels_snoop_channel(struct ast_variable *headers,
	struct ast_ari_channels_snoop_channel_args *args,
	struct ast_ari_response *response)
{
	ari_channels_handle_snoop_channel(
		args->channel_id,
		args->spy,
		args->whisper,
		args->app,
		args->app_args,
		args->snoop_id,
		response);
}

void ast_ari_channels_snoop_channel_with_id(struct ast_variable *headers,
	struct ast_ari_channels_snoop_channel_with_id_args *args,
	struct ast_ari_response *response)
{
	ari_channels_handle_snoop_channel(
		args->channel_id,
		args->spy,
		args->whisper,
		args->app,
		args->app_args,
		args->snoop_id,
		response);
}
