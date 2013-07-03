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

void stasis_http_get_stored_recordings(struct ast_variable *headers, struct ast_get_stored_recordings_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_stored_recordings\n");
}
void stasis_http_get_stored_recording(struct ast_variable *headers, struct ast_get_stored_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_stored_recording\n");
}
void stasis_http_delete_stored_recording(struct ast_variable *headers, struct ast_delete_stored_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_delete_stored_recording\n");
}
void stasis_http_get_live_recordings(struct ast_variable *headers, struct ast_get_live_recordings_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_live_recordings\n");
}

void stasis_http_get_live_recording(struct ast_variable *headers,
	struct ast_get_live_recording_args *args,
	struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	recording = stasis_app_recording_find_by_name(args->recording_name);
	if (recording == NULL) {
		stasis_http_response_error(response, 404, "Not Found",
			"Recording not found");
		return;
	}

	json = stasis_app_recording_to_json(recording);
	if (json == NULL) {
		stasis_http_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	stasis_http_response_ok(response, ast_json_ref(json));
}

void stasis_http_cancel_recording(struct ast_variable *headers, struct ast_cancel_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_cancel_recording\n");
}
void stasis_http_stop_recording(struct ast_variable *headers, struct ast_stop_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_stop_recording\n");
}
void stasis_http_pause_recording(struct ast_variable *headers, struct ast_pause_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_pause_recording\n");
}
void stasis_http_unpause_recording(struct ast_variable *headers, struct ast_unpause_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_unpause_recording\n");
}
void stasis_http_mute_recording(struct ast_variable *headers, struct ast_mute_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_mute_recording\n");
}
void stasis_http_unmute_recording(struct ast_variable *headers, struct ast_unmute_recording_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_unmute_recording\n");
}
