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

void ast_ari_recordings_list_stored(struct ast_variable *headers,
	struct ast_ari_recordings_list_stored_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ao2_container *, recordings, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;

	recordings = stasis_app_stored_recording_find_all();

	if (!recordings) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	json = ast_json_array_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(recordings, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_app_stored_recording *, recording, obj,
			ao2_cleanup);

		int r = ast_json_array_append(
			json, stasis_app_stored_recording_to_json(recording));
		if (r != 0) {
			ast_ari_response_alloc_failed(response);
			ao2_iterator_destroy(&i);
			return;
		}
	}
	ao2_iterator_destroy(&i);

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_recordings_get_stored(struct ast_variable *headers,
	struct ast_ari_recordings_get_stored_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_stored_recording *, recording, NULL,
		ao2_cleanup);
	struct ast_json *json;

	recording = stasis_app_stored_recording_find_by_name(
		args->recording_name);
	if (recording == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Recording not found");
		return;
	}

	json = stasis_app_stored_recording_to_json(recording);
	if (json == NULL) {
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	ast_ari_response_ok(response, json);
}

void ast_ari_recordings_delete_stored(struct ast_variable *headers,
	struct ast_ari_recordings_delete_stored_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_stored_recording *, recording, NULL,
		ao2_cleanup);
	int res;

	recording = stasis_app_stored_recording_find_by_name(
		args->recording_name);
	if (recording == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Recording not found");
		return;
	}

	res = stasis_app_stored_recording_delete(recording);

	if (res != 0) {
		switch (errno) {
		case EACCES:
		case EPERM:
			ast_ari_response_error(response, 500,
				"Internal Server Error",
				"Delete failed");
			break;
		default:
			ast_log(LOG_WARNING,
				"Unexpected error deleting recording %s: %s\n",
				args->recording_name, strerror(errno));
			ast_ari_response_error(response, 500,
				"Internal Server Error",
				"Delete failed");
			break;
		}
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_recordings_get_live(struct ast_variable *headers,
	struct ast_ari_recordings_get_live_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	struct ast_json *json;

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

	ast_ari_response_ok(response, json);
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

void ast_ari_recordings_cancel(struct ast_variable *headers,
	struct ast_ari_recordings_cancel_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_CANCEL,
		response);
}

void ast_ari_recordings_stop(struct ast_variable *headers,
	struct ast_ari_recordings_stop_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_STOP,
		response);
}

void ast_ari_recordings_pause(struct ast_variable *headers,
	struct ast_ari_recordings_pause_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_PAUSE,
		response);
}

void ast_ari_recordings_unpause(struct ast_variable *headers,
	struct ast_ari_recordings_unpause_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_UNPAUSE,
		response);
}

void ast_ari_recordings_mute(struct ast_variable *headers,
	struct ast_ari_recordings_mute_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_MUTE,
		response);
}

void ast_ari_recordings_unmute(struct ast_variable *headers,
	struct ast_ari_recordings_unmute_args *args,
	struct ast_ari_response *response)
{
	control_recording(args->recording_name, STASIS_APP_RECORDING_UNMUTE,
		response);
}
