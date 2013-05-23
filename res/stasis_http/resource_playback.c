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
 * \brief /api-docs/playback.{format} implementation- Playback control resources
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis_app_playback.h"
#include "resource_playback.h"

void stasis_http_get_playback(struct ast_variable *headers,
	struct ast_get_playback_args *args,
	struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		stasis_http_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	json = stasis_app_playback_to_json(playback);
	if (json == NULL) {
		stasis_http_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	stasis_http_response_ok(response, ast_json_ref(json));
}
void stasis_http_stop_playback(struct ast_variable *headers,
	struct ast_stop_playback_args *args,
	struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	enum stasis_playback_oper_results res;

	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		stasis_http_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	res = stasis_app_playback_operation(playback, STASIS_PLAYBACK_STOP);

	switch (res) {
	case STASIS_PLAYBACK_OPER_OK:
		stasis_http_response_no_content(response);
		return;
	case STASIS_PLAYBACK_OPER_FAILED:
		stasis_http_response_error(response, 500,
			"Internal Server Error", "Could not stop playback");
		return;
	case STASIS_PLAYBACK_OPER_NOT_PLAYING:
		/* Stop operation should be valid even when not playing */
		ast_assert(0);
		stasis_http_response_error(response, 500,
			"Internal Server Error", "Could not stop playback");
		return;
	}
}
void stasis_http_control_playback(struct ast_variable *headers,
	struct ast_control_playback_args *args,
	struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	enum stasis_app_playback_media_operation oper;
	enum stasis_playback_oper_results res;

	if (strcmp(args->operation, "unpause") == 0) {
		oper = STASIS_PLAYBACK_UNPAUSE;
	} else if (strcmp(args->operation, "pause") == 0) {
		oper = STASIS_PLAYBACK_PAUSE;
	} else if (strcmp(args->operation, "restart") == 0) {
		oper = STASIS_PLAYBACK_RESTART;
	} else if (strcmp(args->operation, "reverse") == 0) {
		oper = STASIS_PLAYBACK_REVERSE;
	} else if (strcmp(args->operation, "forward") == 0) {
		oper = STASIS_PLAYBACK_FORWARD;
	} else {
		stasis_http_response_error(response, 400,
			"Bad Request", "Invalid operation %s",
			args->operation);
		return;

	}

	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		stasis_http_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	res = stasis_app_playback_operation(playback, oper);

	switch (res) {
	case STASIS_PLAYBACK_OPER_OK:
		stasis_http_response_no_content(response);
		return;
	case STASIS_PLAYBACK_OPER_FAILED:
		stasis_http_response_error(response, 500,
			"Internal Server Error", "Could not %s playback",
			args->operation);
		return;
	case STASIS_PLAYBACK_OPER_NOT_PLAYING:
		stasis_http_response_error(response, 409, "Conflict",
			"Can only %s while media is playing", args->operation);
		return;
	}
}
