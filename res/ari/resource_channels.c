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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

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
#include "asterisk/max_forwards.h"
#include "asterisk/rtp_engine.h"
#include "resource_channels.h"

#include <limits.h>


/*! \brief Return the corresponded hangup code of the given reason */
static int convert_reason_to_hangup_code(const char* reason)
{
	if (!strcmp(reason, "normal")) {
		return AST_CAUSE_NORMAL;
	} else if (!strcmp(reason, "busy")) {
		return AST_CAUSE_BUSY;
	} else if (!strcmp(reason, "congestion")) {
		return AST_CAUSE_CONGESTION;
	} else if (!strcmp(reason, "no_answer")) {
		return AST_CAUSE_NOANSWER;
	} else if (!strcmp(reason, "timeout")) {
		return AST_CAUSE_NO_USER_RESPONSE;
	} else if (!strcmp(reason, "rejected")) {
		return AST_CAUSE_CALL_REJECTED;
	} else if (!strcmp(reason, "unallocated")) {
		return AST_CAUSE_UNALLOCATED;
	} else if (!strcmp(reason, "normal_unspecified")) {
		return AST_CAUSE_NORMAL_UNSPECIFIED;
	} else if (!strcmp(reason, "number_incomplete")) {
		return AST_CAUSE_INVALID_NUMBER_FORMAT;
	} else if (!strcmp(reason, "codec_mismatch")) {
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	} else if (!strcmp(reason, "interworking")) {
		return AST_CAUSE_INTERWORKING;
	} else if (!strcmp(reason, "failure")) {
		return AST_CAUSE_FAILURE;
	} else if(!strcmp(reason, "answered_elsewhere")) {
		return AST_CAUSE_ANSWERED_ELSEWHERE;
	}

	return -1;
}

/*!
 * \brief Ensure channel is in a state that allows operation to be performed.
 *
 * Since Asterisk 14, it has been possible for down channels, as well as unanswered
 * outbound channels to enter Stasis. While some operations are fine to perform on
 * such channels, operations that
 *
 * - Attempt to manipulate channel state
 * - Attempt to play media
 * - Attempt to control the channel's location in the dialplan
 *
 * are invalid. This function can be used to determine if the channel is in an
 * appropriate state.
 *
 * \note When this function returns an error, the HTTP response is taken care of.
 *
 * \param control The app control
 * \param response Response to fill in if there is an error
 *
 * \retval 0 Channel is in a valid state. Continue on!
 * \retval non-zero Channel is in an invalid state. Bail!
 */
static int channel_state_invalid(struct stasis_app_control *control,
	struct ast_ari_response *response)
{
	struct ast_channel_snapshot *snapshot;

	snapshot = stasis_app_control_get_snapshot(control);
	if (!snapshot) {
		ast_ari_response_error(response, 404, "Not Found", "Channel not found");
		return -1;
	}

	/* These channel states apply only to outbound channels:
	 * - Down: Channel has been created, and nothing else has been done
	 * - Reserved: For a PRI, an underlying B-channel is reserved,
	 *   but the channel is not yet dialed
	 * - Ringing: The channel has been dialed.
	 *
	 * This does not affect inbound channels. Inbound channels, when they
	 * enter the dialplan, are in the "Ring" state. If they have already
	 * been answered, then they are in the "Up" state.
	 */
	if (snapshot->state == AST_STATE_DOWN
		|| snapshot->state == AST_STATE_RESERVED
		|| snapshot->state == AST_STATE_RINGING) {
		ast_ari_response_error(response, 412, "Precondition Failed",
			"Channel in invalid state");
		ao2_ref(snapshot, -1);

		return -1;
	}

	ao2_ref(snapshot, -1);

	return 0;
}

/*!
 * \brief Finds the control object for a channel, filling the response with an
 * error, if appropriate.
 * \param[out] response Response to fill with an error if control is not found.
 * \param channel_id ID of the channel to lookup.
 * \return Channel control object.
 * \retval NULL if control object does not exist.
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

	if (channel_state_invalid(control, response)) {
		return;
	}

	snapshot = stasis_app_control_get_snapshot(control);
	if (!snapshot) {
		ast_ari_response_error(response, 404, "Not Found", "Channel not found");
		return;
	}

	if (ast_strlen_zero(args->context)) {
		context = snapshot->dialplan->context;
		exten = S_OR(args->extension, snapshot->dialplan->exten);
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
		ipri = snapshot->dialplan->priority + 1;
	} else {
		ipri = 1;
	}


	if (stasis_app_control_continue(control, context, exten, ipri)) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_channels_move(struct ast_variable *headers,
	struct ast_ari_channels_move_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (!control) {
		return;
	}

	if (stasis_app_control_move(control, args->app, args->app_args)) {
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Failed to switch Stasis applications");
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_channels_redirect(struct ast_variable *headers,
	struct ast_ari_channels_redirect_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, chan_snapshot, NULL, ao2_cleanup);
	char *tech;
	char *resource;
	int tech_len;

	control = find_control(response, args->channel_id);
	if (!control) {
		return;
	}

	if (channel_state_invalid(control, response)) {
		return;
	}

	if (ast_strlen_zero(args->endpoint)) {
		ast_ari_response_error(response, 400, "Not Found",
			"Required parameter 'endpoint' not provided.");
		return;
	}

	tech = ast_strdupa(args->endpoint);
	if (!(resource = strchr(tech, '/')) || !(tech_len = resource - tech)) {
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Endpoint parameter '%s' does not contain tech/resource", args->endpoint);
		return;
	}

	*resource++ = '\0';
	if (ast_strlen_zero(resource)) {
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"No resource provided in endpoint parameter '%s'", args->endpoint);
		return;
	}

	chan_snapshot = ast_channel_snapshot_get_latest(args->channel_id);
	if (!chan_snapshot) {
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Unable to find channel snapshot for '%s'", args->channel_id);
		return;
	}

	if (strncasecmp(chan_snapshot->base->type, tech, tech_len)) {
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Endpoint technology '%s' does not match channel technology '%s'",
			tech, chan_snapshot->base->type);
		return;
	}

	if (stasis_app_control_redirect(control, resource)) {
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Failed to redirect channel");
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
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

	if (channel_state_invalid(control, response)) {
		return;
	}

	stasis_app_control_silence_stop(control);
	ast_ari_response_no_content(response);
}

static void ari_channels_handle_play(
	const char *args_channel_id,
	const char **args_media,
	size_t args_media_count,
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

	if (channel_state_invalid(control, response)) {
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

	language = S_OR(args_lang, snapshot->base->language);

	playback = stasis_app_control_play_uri(control, args_media, args_media_count, language,
		args_channel_id, STASIS_PLAYBACK_TARGET_CHANNEL, args_skipms, args_offsetms, args_playback_id);
	if (!playback) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Failed to queue media for playback");
		return;
	}

	if (ast_asprintf(&playback_url, "/playbacks/%s",
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
		args->media_count,
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
		args->media_count,
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

	if (options->if_exists == AST_RECORD_IF_EXISTS_ERROR) {
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
	struct ast_channel_snapshot *snapshot;

	snapshot = ast_channel_snapshot_get_latest(args->channel_id);
	if (!snapshot) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	ast_ari_response_ok(response,
				ast_channel_snapshot_to_json(snapshot, NULL));
	ao2_ref(snapshot, -1);
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

	if (!ast_strlen_zero(args->reason) && !ast_strlen_zero(args->reason_code)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"The reason and reason_code can't both be specified");
		return;
	}

	if (!ast_strlen_zero(args->reason_code)) {
		/* reason_code allows any hangup code */
		if (sscanf(args->reason_code, "%30d", &cause) != 1) {
			ast_ari_response_error(
				response, 400, "Invalid Reason Code",
				"Invalid reason for hangup reason code provided");
			return;
		}
	} else if (!ast_strlen_zero(args->reason)) {
		/* reason allows only listed hangup reason */
		cause = convert_reason_to_hangup_code(args->reason);
		if (cause == -1) {
			ast_ari_response_error(
				response, 400, "Invalid Reason",
				"Invalid reason for hangup reason provided");
			return;
		}
	} else {
		/* not specified. set default hangup */
		cause = AST_CAUSE_NORMAL;
	}

	ast_channel_hangupcause_set(chan, cause);
	ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);

	ast_ari_response_no_content(response);
}

void ast_ari_channels_list(struct ast_variable *headers,
	struct ast_ari_channels_list_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ao2_container *, snapshots, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;
	struct stasis_message_sanitizer *sanitize = stasis_app_get_sanitizer();

	snapshots = ast_channel_cache_all();

	json = ast_json_array_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(snapshots, 0);
	while ((obj = ao2_iterator_next(&i))) {
		struct ast_channel_snapshot *snapshot = obj;
		int r;

		if (sanitize && sanitize->channel_snapshot
			&& sanitize->channel_snapshot(snapshot)) {
			ao2_ref(snapshot, -1);
			continue;
		}

		r = ast_json_array_append(
			json, ast_channel_snapshot_to_json(snapshot, NULL));
		if (r != 0) {
			ast_ari_response_alloc_failed(response);
			ao2_iterator_destroy(&i);
			ao2_ref(snapshot, -1);
			return;
		}
		ao2_ref(snapshot, -1);
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

static struct ast_channel *ari_channels_handle_originate_with_id(const char *args_endpoint,
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
	const char *args_formats,
	struct ast_ari_response *response)
{
	char *dialtech;
	char *dialdevice = NULL;
	struct ast_dial *dial;
	char *caller_id = NULL;
	char *cid_num = NULL;
	char *cid_name = NULL;
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
	struct ast_format_cap *format_cap = NULL;

	if ((assignedids.uniqueid && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid))
		|| (assignedids.uniqueid2 && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid2))) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Uniqueid length exceeds maximum of %d", AST_MAX_PUBLIC_UNIQUEID);
		return NULL;
	}

	if (ast_strlen_zero(args_endpoint)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Endpoint must be specified");
		return NULL;
	}

	if (!ast_strlen_zero(args_originator) && !ast_strlen_zero(args_formats)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Originator and formats can't both be specified");
		return NULL;
	}

	dialtech = ast_strdupa(args_endpoint);
	if ((stuff = strchr(dialtech, '/'))) {
		*stuff++ = '\0';
		dialdevice = stuff;
	}

	if (ast_strlen_zero(dialtech) || ast_strlen_zero(dialdevice)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid endpoint specified");
		return NULL;
	}

	if (!ast_strlen_zero(args_app)) {
		RAII_VAR(struct ast_str *, appdata, ast_str_create(64), ast_free);

		if (!appdata) {
			ast_ari_response_alloc_failed(response);
			return NULL;
		}

		ast_str_set(&appdata, 0, "%s", args_app);
		if (!ast_strlen_zero(args_app_args)) {
			ast_str_append(&appdata, 0, ",%s", args_app_args);
		}

		origination = ast_calloc(1, sizeof(*origination) + ast_str_size(appdata) + 1);
		if (!origination) {
			ast_ari_response_alloc_failed(response);
			return NULL;
		}

		strcpy(origination->appdata, ast_str_buffer(appdata));
	} else if (!ast_strlen_zero(args_extension)) {
		origination = ast_calloc(1, sizeof(*origination) + 1);
		if (!origination) {
			ast_ari_response_alloc_failed(response);
			return NULL;
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
					return NULL;
				}
			} else {
				ast_debug(3, "Numeric value provided for label, jumping to that priority\n");
			}

			if (ipri == 0) {
				ast_log(AST_LOG_ERROR, "Invalid priority label '%s' specified for extension %s in context: %s\n",
						args_label, args_extension, args_context);
				ast_ari_response_error(response, 400, "Bad Request", "Requested priority is illegal");
				return NULL;
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
		return NULL;
	}

	dial = ast_dial_create();
	if (!dial) {
		ast_ari_response_alloc_failed(response);
		ast_free(origination);
		return NULL;
	}
	ast_dial_set_user_data(dial, origination);

	if (ast_dial_append(dial, dialtech, dialdevice, &assignedids)) {
		ast_ari_response_alloc_failed(response);
		ast_dial_destroy(dial);
		ast_free(origination);
		return NULL;
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
			return NULL;
		}
	}

	if (!ast_strlen_zero(args_formats)) {
		char *format_name;
		char *formats_copy = ast_strdupa(args_formats);

		if (!(format_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
			ast_ari_response_alloc_failed(response);
			ast_dial_destroy(dial);
			ast_free(origination);
			ast_channel_cleanup(other);
			return NULL;
		}

		while ((format_name = ast_strip(strsep(&formats_copy, ",")))) {
			struct ast_format *fmt = ast_format_cache_get(format_name);

			if (!fmt || ast_format_cap_append(format_cap, fmt, 0)) {
				if (!fmt) {
					ast_ari_response_error(
						response, 400, "Bad Request",
						"Provided format (%s) was not found", format_name);
				} else {
					ast_ari_response_alloc_failed(response);
				}
				ast_dial_destroy(dial);
				ast_free(origination);
				ast_channel_cleanup(other);
				ao2_ref(format_cap, -1);
				ao2_cleanup(fmt);
				return NULL;
			}
			ao2_ref(fmt, -1);
		}
	}

	if (ast_dial_prerun(dial, other, format_cap)) {
		if (ast_channel_errno() == AST_CHANNEL_ERROR_ID_EXISTS) {
			ast_ari_response_error(response, 409, "Conflict",
				"Channel with given unique ID already exists");
		} else {
			ast_ari_response_alloc_failed(response);
		}
		ast_dial_destroy(dial);
		ast_free(origination);
		ast_channel_cleanup(other);
		return NULL;
	}

	ast_channel_cleanup(other);
	ao2_cleanup(format_cap);

	chan = ast_dial_get_channel(dial, 0);
	if (!chan) {
		ast_ari_response_alloc_failed(response);
		ast_dial_destroy(dial);
		ast_free(origination);
		return NULL;
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

	return chan;
}

/*!
 * \internal
 * \brief Convert a \c ast_json list of key/value pair tuples into a \c ast_variable list
 * \since 13.3.0
 *
 * \param[out] response HTTP response if error
 * \param json_variables The JSON blob containing the variable
 * \param[out] variables An out reference to the variables to populate.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int json_to_ast_variables(struct ast_ari_response *response, struct ast_json *json_variables, struct ast_variable **variables)
{
	enum ast_json_to_ast_vars_code res;

	res = ast_json_to_ast_variables(json_variables, variables);
	switch (res) {
	case AST_JSON_TO_AST_VARS_CODE_SUCCESS:
		return 0;
	case AST_JSON_TO_AST_VARS_CODE_INVALID_TYPE:
		ast_ari_response_error(response, 400, "Bad Request",
			"Only string values in the 'variables' object allowed");
		break;
	case AST_JSON_TO_AST_VARS_CODE_OOM:
		ast_ari_response_alloc_failed(response);
		break;
	}
	ast_log(AST_LOG_ERROR, "Unable to convert 'variables' in JSON body to channel variables\n");

	return -1;
}

void ast_ari_channels_originate_with_id(struct ast_variable *headers,
	struct ast_ari_channels_originate_with_id_args *args,
	struct ast_ari_response *response)
{
	struct ast_variable *variables = NULL;
	struct ast_channel *chan;

	/* Parse any query parameters out of the body parameter */
	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_channels_originate_with_id_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables
			&& json_to_ast_variables(response, json_variables, &variables)) {
			return;
		}
	}

	chan = ari_channels_handle_originate_with_id(
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
		args->formats,
		response);
	ast_channel_cleanup(chan);
	ast_variables_destroy(variables);
}

void ast_ari_channels_originate(struct ast_variable *headers,
	struct ast_ari_channels_originate_args *args,
	struct ast_ari_response *response)
{
	struct ast_variable *variables = NULL;
	struct ast_channel *chan;

	/* Parse any query parameters out of the body parameter */
	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_channels_originate_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables
			&& json_to_ast_variables(response, json_variables, &variables)) {
			return;
		}
	}

	chan = ari_channels_handle_originate_with_id(
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
		args->formats,
		response);
	ast_channel_cleanup(chan);
	ast_variables_destroy(variables);
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

	if (!value) {
		ast_ari_response_alloc_failed(response);
		return;
	}

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
			ast_ari_response_error(
				response, 404, "Variable Not Found",
				"Provided variable was not found");
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

struct ari_channel_thread_data {
	struct ast_channel *chan;
	struct ast_str *stasis_stuff;
};

static void chan_data_destroy(struct ari_channel_thread_data *chan_data)
{
	ast_free(chan_data->stasis_stuff);
	ast_hangup(chan_data->chan);
	ast_free(chan_data);
}

/*!
 * \brief Thread that owns stasis-created channel.
 *
 * The channel enters into a Stasis application immediately upon creation. In this
 * way, the channel can be manipulated by the Stasis application. Once the channel
 * exits the Stasis application, it is hung up.
 */
static void *ari_channel_thread(void *data)
{
	struct ari_channel_thread_data *chan_data = data;
	struct ast_app *stasis_app;

	stasis_app = pbx_findapp("Stasis");
	if (!stasis_app) {
		ast_log(LOG_ERROR, "Stasis dialplan application is not registered");
		chan_data_destroy(chan_data);
		return NULL;
	}

	pbx_exec(chan_data->chan, stasis_app, ast_str_buffer(chan_data->stasis_stuff));

	chan_data_destroy(chan_data);

	return NULL;
}

struct ast_datastore_info dialstring_info = {
	.type = "ARI Dialstring",
	.destroy = ast_free_ptr,
};

/*!
 * \brief Save dialstring onto a channel datastore
 *
 * This will later be retrieved when it comes time to actually dial the channel
 *
 * \param chan The channel on which to save the dialstring
 * \param dialstring The dialstring to save
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int save_dialstring(struct ast_channel *chan, const char *dialstring)
{
	struct ast_datastore *datastore;

	datastore = ast_datastore_alloc(&dialstring_info, NULL);
	if (!datastore) {
		return -1;
	}

	datastore->data = ast_strdup(dialstring);
	if (!datastore->data) {
		ast_datastore_free(datastore);
		return -1;
	}

	ast_channel_lock(chan);
	if (ast_channel_datastore_add(chan, datastore)) {
		ast_channel_unlock(chan);
		ast_datastore_free(datastore);
		return -1;
	}
	ast_channel_unlock(chan);

	return 0;
}

/*!
 * \brief Retrieve the dialstring from the channel datastore
 *
 * \pre chan is locked
 * \param chan Channel that was previously created in ARI
 * \retval NULL Failed to find datastore
 * \retval non-NULL The dialstring
 */
static char *restore_dialstring(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &dialstring_info, NULL);
	if (!datastore) {
		return NULL;
	}

	return datastore->data;
}

void ast_ari_channels_create(struct ast_variable *headers,
	struct ast_ari_channels_create_args *args,
	struct ast_ari_response *response)
{
	struct ast_variable *variables = NULL;
	struct ast_assigned_ids assignedids;
	struct ari_channel_thread_data *chan_data;
	struct ast_channel_snapshot *snapshot;
	pthread_t thread;
	char *dialtech;
	char *dialdevice = NULL;
	char *stuff;
	int cause;
	struct ast_format_cap *request_cap;
	struct ast_channel *originator;

	/* Parse any query parameters out of the body parameter */
	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_channels_create_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables
			&& json_to_ast_variables(response, json_variables, &variables)) {
			return;
		}
	}

	assignedids.uniqueid = args->channel_id;
	assignedids.uniqueid2 = args->other_channel_id;

	if (!ast_strlen_zero(args->originator) && !ast_strlen_zero(args->formats)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Originator and formats can't both be specified");
		return;
	}

	if (ast_strlen_zero(args->endpoint)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Endpoint must be specified");
		return;
	}

	chan_data = ast_calloc(1, sizeof(*chan_data));
	if (!chan_data) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	chan_data->stasis_stuff = ast_str_create(32);
	if (!chan_data->stasis_stuff) {
		ast_ari_response_alloc_failed(response);
		chan_data_destroy(chan_data);
		return;
	}

	ast_str_append(&chan_data->stasis_stuff, 0, "%s", args->app);
	if (!ast_strlen_zero(args->app_args)) {
		ast_str_append(&chan_data->stasis_stuff, 0, ",%s", args->app_args);
	}

	dialtech = ast_strdupa(args->endpoint);
	if ((stuff = strchr(dialtech, '/'))) {
		*stuff++ = '\0';
		dialdevice = stuff;
	}

	if (ast_strlen_zero(dialtech) || ast_strlen_zero(dialdevice)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid endpoint specified");
		chan_data_destroy(chan_data);
		return;
	}

	originator = ast_channel_get_by_name(args->originator);
	if (originator) {
		request_cap = ao2_bump(ast_channel_nativeformats(originator));
		if (!ast_strlen_zero(args->app)) {
			stasis_app_subscribe_channel(args->app, originator);
		}
	} else if (!ast_strlen_zero(args->formats)) {
		char *format_name;
		char *formats_copy = ast_strdupa(args->formats);

		if (!(request_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
			ast_ari_response_alloc_failed(response);
			chan_data_destroy(chan_data);
			return;
		}

		while ((format_name = ast_strip(strsep(&formats_copy, ",")))) {
			struct ast_format *fmt = ast_format_cache_get(format_name);

			if (!fmt || ast_format_cap_append(request_cap, fmt, 0)) {
				if (!fmt) {
					ast_ari_response_error(
						response, 400, "Bad Request",
						"Provided format (%s) was not found", format_name);
				} else {
					ast_ari_response_alloc_failed(response);
				}
				ao2_ref(request_cap, -1);
				ao2_cleanup(fmt);
				chan_data_destroy(chan_data);
				return;
			}
			ao2_ref(fmt, -1);
		}
	} else {
		if (!(request_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
			ast_ari_response_alloc_failed(response);
			chan_data_destroy(chan_data);
			return;
		}

		ast_format_cap_append_by_type(request_cap, AST_MEDIA_TYPE_AUDIO);
	}

	chan_data->chan = ast_request(dialtech, request_cap, &assignedids, originator, dialdevice, &cause);
	ao2_cleanup(request_cap);

	if (!chan_data->chan) {
		if (ast_channel_errno() == AST_CHANNEL_ERROR_ID_EXISTS) {
			ast_ari_response_error(response, 409, "Conflict",
				"Channel with given unique ID already exists");
		} else {
			ast_ari_response_alloc_failed(response);
		}
		ast_channel_cleanup(originator);
		chan_data_destroy(chan_data);
		return;
	}

	if (!ast_strlen_zero(args->app)) {
		stasis_app_subscribe_channel(args->app, chan_data->chan);
	}

	if (variables) {
		ast_set_variables(chan_data->chan, variables);
	}

	ast_channel_cleanup(originator);

	if (save_dialstring(chan_data->chan, stuff)) {
		ast_ari_response_alloc_failed(response);
		chan_data_destroy(chan_data);
		return;
	}

	snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan_data->chan));

	if (ast_pthread_create_detached(&thread, NULL, ari_channel_thread, chan_data)) {
		ast_ari_response_alloc_failed(response);
		chan_data_destroy(chan_data);
	} else {
		ast_ari_response_ok(response, ast_channel_snapshot_to_json(snapshot, NULL));
	}

	ao2_ref(snapshot, -1);
}

void ast_ari_channels_dial(struct ast_variable *headers,
	struct ast_ari_channels_dial_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, caller, NULL, ast_channel_cleanup);
	RAII_VAR(struct ast_channel *, callee, NULL, ast_channel_cleanup);
	char *dialstring;

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	caller = ast_channel_get_by_name(args->caller);

	callee = ast_channel_get_by_name(args->channel_id);
	if (!callee) {
		ast_ari_response_error(response, 404, "Not Found",
			"Callee not found");
		return;
	}

	if (ast_channel_state(callee) != AST_STATE_DOWN
		&& ast_channel_state(callee) != AST_STATE_RESERVED) {
		ast_ari_response_error(response, 409, "Conflict",
			"Channel is not in the 'Down' state");
		return;
	}

	/* XXX This is straight up copied from main/dial.c. It's probably good
	 * to separate this to some common method.
	 */
	if (caller) {
		ast_channel_lock_both(caller, callee);
	} else {
		ast_channel_lock(callee);
	}

	dialstring = restore_dialstring(callee);
	if (!dialstring) {
		ast_channel_unlock(callee);
		if (caller) {
			ast_channel_unlock(caller);
		}
		ast_ari_response_error(response, 409, "Conflict",
			"Dialing a channel not created by ARI");
		return;
	}
	/* Make a copy of the dialstring just in case some jerk tries to hang up the
	 * channel before we can actually dial
	 */
	dialstring = ast_strdupa(dialstring);

	ast_channel_stage_snapshot(callee);
	if (caller) {
		ast_channel_inherit_variables(caller, callee);
		ast_channel_datastore_inherit(caller, callee);
		ast_max_forwards_decrement(callee);

		/* Copy over callerid information */
		ast_party_redirecting_copy(ast_channel_redirecting(callee), ast_channel_redirecting(caller));

		ast_channel_dialed(callee)->transit_network_select = ast_channel_dialed(caller)->transit_network_select;

		ast_connected_line_copy_from_caller(ast_channel_connected(callee), ast_channel_caller(caller));

		ast_channel_language_set(callee, ast_channel_language(caller));
		ast_channel_req_accountcodes(callee, caller, AST_CHANNEL_REQUESTOR_BRIDGE_PEER);
		if (ast_strlen_zero(ast_channel_musicclass(callee)))
			ast_channel_musicclass_set(callee, ast_channel_musicclass(caller));

		ast_channel_adsicpe_set(callee, ast_channel_adsicpe(caller));
		ast_channel_transfercapability_set(callee, ast_channel_transfercapability(caller));
		ast_channel_unlock(caller);
	}

	ast_channel_stage_snapshot_done(callee);
	ast_channel_unlock(callee);

	if (stasis_app_control_dial(control, dialstring, args->timeout)) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_channels_rtpstatistics(struct ast_variable *headers,
		struct ast_ari_channels_rtpstatistics_args *args,
		struct ast_ari_response *response)
{
	RAII_VAR(struct ast_channel *, chan, NULL, ast_channel_cleanup);
	RAII_VAR(struct ast_rtp_instance *, rtp, NULL, ao2_cleanup);
	struct ast_json *j_res;
	const struct ast_channel_tech *tech;
	struct ast_rtp_glue *glue;

	chan = ast_channel_get_by_name(args->channel_id);
	if (!chan) {
		ast_ari_response_error(response, 404, "Not Found",
			"Channel not found");
		return;
	}

	ast_channel_lock(chan);
	tech = ast_channel_tech(chan);
	if (!tech) {
		ast_channel_unlock(chan);
		ast_ari_response_error(response, 404, "Not Found",
			"Channel's tech not found");
		return;
	}

	glue = ast_rtp_instance_get_glue(tech->type);
	if (!glue) {
		ast_channel_unlock(chan);
		ast_ari_response_error(response, 403, "Forbidden",
			"Unsupported channel type");
		return;
	}

	glue->get_rtp_info(chan, &rtp);
	if (!rtp) {
		ast_channel_unlock(chan);
		ast_ari_response_error(response, 404, "Not Found",
			"RTP info not found");
		return;
	}

	j_res = ast_rtp_instance_get_stats_all_json(rtp);
	if (!j_res) {
		ast_channel_unlock(chan);
		ast_ari_response_error(response, 404, "Not Found",
			"Statistics not found");
		return;
	}

	ast_channel_unlock(chan);
	ast_ari_response_ok(response, j_res);

	return;
}

static void external_media_rtp_udp(struct ast_ari_channels_external_media_args *args,
	struct ast_variable *variables,
	struct ast_ari_response *response)
{
	size_t endpoint_len;
	char *endpoint;
	struct ast_channel *chan;
	struct varshead *vars;

	endpoint_len = strlen("UnicastRTP/") + strlen(args->external_host) + 1;
	endpoint = ast_alloca(endpoint_len);
	snprintf(endpoint, endpoint_len, "UnicastRTP/%s", args->external_host);

	chan = ari_channels_handle_originate_with_id(
		endpoint,
		NULL,
		NULL,
		0,
		NULL,
		args->app,
		args->data,
		NULL,
		0,
		variables,
		args->channel_id,
		NULL,
		NULL,
		args->format,
		response);
	ast_variables_destroy(variables);

	if (!chan) {
		return;
	}

	ast_channel_lock(chan);
	vars = ast_channel_varshead(chan);
	if (vars && !AST_LIST_EMPTY(vars)) {
		ast_json_object_set(response->message, "channelvars", ast_json_channel_vars(vars));
	}
	ast_channel_unlock(chan);
	ast_channel_unref(chan);
}

static void external_media_audiosocket_tcp(struct ast_ari_channels_external_media_args *args,
	struct ast_variable *variables,
	struct ast_ari_response *response)
{
	size_t endpoint_len;
	char *endpoint;
	struct ast_channel *chan;
	struct varshead *vars;

	if (ast_strlen_zero(args->data)) {
		ast_ari_response_error(response, 400, "Bad Request", "data can not be empty");
		return;
	}

	endpoint_len = strlen("AudioSocket/") + strlen(args->external_host) + 1 + strlen(args->data) + 1;
	endpoint = ast_alloca(endpoint_len);
	/* The UUID is stored in the arbitrary data field */
	snprintf(endpoint, endpoint_len, "AudioSocket/%s/%s", args->external_host, args->data);

	chan = ari_channels_handle_originate_with_id(
		endpoint,
		NULL,
		NULL,
		0,
		NULL,
		args->app,
		args->data,
		NULL,
		0,
		variables,
		args->channel_id,
		NULL,
		NULL,
		args->format,
		response);
	ast_variables_destroy(variables);

	if (!chan) {
		return;
	}

	ast_channel_lock(chan);
	vars = ast_channel_varshead(chan);
	if (vars && !AST_LIST_EMPTY(vars)) {
		ast_json_object_set(response->message, "channelvars", ast_json_channel_vars(vars));
	}
	ast_channel_unlock(chan);
	ast_channel_unref(chan);
}

#include "asterisk/config.h"
#include "asterisk/netsock2.h"

void ast_ari_channels_external_media(struct ast_variable *headers,
	struct ast_ari_channels_external_media_args *args, struct ast_ari_response *response)
{
	struct ast_variable *variables = NULL;
	char *external_host;
	char *host = NULL;
	char *port = NULL;

	ast_assert(response != NULL);

	/* Parse any query parameters out of the body parameter */
	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_channels_external_media_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables
			&& json_to_ast_variables(response, json_variables, &variables)) {
			return;
		}
	}

	if (ast_strlen_zero(args->app)) {
		ast_ari_response_error(response, 400, "Bad Request", "app cannot be empty");
		return;
	}

	if (ast_strlen_zero(args->external_host)) {
		ast_ari_response_error(response, 400, "Bad Request", "external_host cannot be empty");
		return;
	}

	external_host = ast_strdupa(args->external_host);
	if (!ast_sockaddr_split_hostport(external_host, &host, &port, PARSE_PORT_REQUIRE)) {
		ast_ari_response_error(response, 400, "Bad Request", "external_host must be <host>:<port>");
		return;
	}

	if (ast_strlen_zero(args->format)) {
		ast_ari_response_error(response, 400, "Bad Request", "format cannot be empty");
		return;
	}

	if (ast_strlen_zero(args->encapsulation)) {
		args->encapsulation = "rtp";
	}
	if (ast_strlen_zero(args->transport)) {
		args->transport = "udp";
	}
	if (ast_strlen_zero(args->connection_type)) {
		args->connection_type = "client";
	}
	if (ast_strlen_zero(args->direction)) {
		args->direction = "both";
	}

	if (strcasecmp(args->encapsulation, "rtp") == 0 && strcasecmp(args->transport, "udp") == 0) {
		external_media_rtp_udp(args, variables, response);
	} else if (strcasecmp(args->encapsulation, "audiosocket") == 0 && strcasecmp(args->transport, "tcp") == 0) {
		external_media_audiosocket_tcp(args, variables, response);
	} else {
		ast_ari_response_error(
			response, 501, "Not Implemented",
			"The encapsulation and/or transport is not supported");
	}
}
