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
 * \brief /api-docs/recordings.{format} implementation- Recording resources
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis_app_recording.h"
#include "resource_recordings.h"

void ast_ari_get_stored_recordings(struct ast_variable *headers, struct ast_get_stored_recordings_args *args, struct ast_ari_response *response)
{
	ast_log(LOG_ERROR, "TODO: ast_ari_get_stored_recordings\n");
}
void ast_ari_get_stored_recording(struct ast_variable *headers, struct ast_get_stored_recording_args *args, struct ast_ari_response *response)
{
	ast_log(LOG_ERROR, "TODO: ast_ari_get_stored_recording\n");
}
void ast_ari_delete_stored_recording(struct ast_variable *headers, struct ast_delete_stored_recording_args *args, struct ast_ari_response *response)
{
	ast_log(LOG_ERROR, "TODO: ast_ari_delete_stored_recording\n");
}
void ast_ari_get_live_recordings(struct ast_variable *headers, struct ast_get_live_recordings_args *args, struct ast_ari_response *response)
{
	ast_log(LOG_ERROR, "TODO: ast_ari_get_live_recordings\n");
}

void ast_ari_get_live_recording(struct ast_variable *headers,
	struct ast_get_live_recording_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	recording = stasis_app_recording_find_by_name(args->recording_name);
	if (recording == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Recording not found");
		return;
	}

	json = stasis_app_recording_to_json(recording);
	if (json == NULL) {
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

static void control_recording(const char *name,
	enum stasis_app_recording_media_operation operation,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	enum stasis_app_recording_oper_results res;

	recording = stasis_app_recording_find_by_name(name);
	if (recording == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Recording not found");
		return;
	}

	res = stasis_app_recording_operation(recording, operation);

	switch (res) {
	case STASIS_APP_RECORDING_OPER_OK:
		ast_ari_response_no_content(response);
		return;
	case STASIS_APP_RECORDING_OPER_FAILED:
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Recording operation failed");
		return;
	case STASIS_APP_RECORDING_OPER_NOT_RECORDING:
		ast_ari_response_error(response, 409,
			"Conflict", "Recording not in session");
	}
}

void ast_ari_cancel_recording(struct ast_variable *headers,
	struct ast_cancel_recording_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_CANCEL,
		response);
}

void ast_ari_stop_recording(struct ast_variable *headers,
	struct ast_stop_recording_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_STOP,
		response);
}

void ast_ari_pause_recording(struct ast_variable *headers,
	struct ast_pause_recording_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_PAUSE,
		response);
}

void ast_ari_unpause_recording(struct ast_variable *headers,
	struct ast_unpause_recording_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_UNPAUSE,
		response);
}

void ast_ari_mute_recording(struct ast_variable *headers,
	struct ast_mute_recording_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_MUTE,
		response);
}

void ast_ari_unmute_recording(struct ast_variable *headers,
	struct ast_unmute_recording_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_UNMUTE,
		response);
}
