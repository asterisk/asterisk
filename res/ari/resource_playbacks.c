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
 * \brief /api-docs/playbacks.{format} implementation- Playback control resources
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis_playback</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis_app_playback.h"
#include "resource_playbacks.h"

void ast_ari_playbacks_get(struct ast_variable *headers,
	struct ast_ari_playbacks_get_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	struct ast_json *json;

	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	json = stasis_app_playback_to_json(playback);
	if (json == NULL) {
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	ast_ari_response_ok(response, json);
}
void ast_ari_playbacks_stop(struct ast_variable *headers,
	struct ast_ari_playbacks_stop_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	enum stasis_playback_oper_results res;

	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	res = stasis_app_playback_operation(playback, STASIS_PLAYBACK_STOP);
	switch (res) {
	case STASIS_PLAYBACK_OPER_OK:
		ast_ari_response_no_content(response);
		return;
	case STASIS_PLAYBACK_OPER_FAILED:
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Could not stop playback");
		return;
	case STASIS_PLAYBACK_OPER_NOT_PLAYING:
		/* Stop operation should be valid even when not playing */
		ast_assert(0);
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Could not stop playback");
		return;
	}
}
void ast_ari_playbacks_control(struct ast_variable *headers,
	struct ast_ari_playbacks_control_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	enum stasis_app_playback_media_operation oper;
	enum stasis_playback_oper_results res;

	if (!args->operation) {
		ast_ari_response_error(response, 400,
			"Bad Request", "Missing operation");
		return;
	}
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
		ast_ari_response_error(response, 400,
			"Bad Request", "Invalid operation %s",
			args->operation);
		return;
	}

	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		ast_ari_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	res = stasis_app_playback_operation(playback, oper);
	switch (res) {
	case STASIS_PLAYBACK_OPER_OK:
		ast_ari_response_no_content(response);
		return;
	case STASIS_PLAYBACK_OPER_FAILED:
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Could not %s playback",
			args->operation);
		return;
	case STASIS_PLAYBACK_OPER_NOT_PLAYING:
		ast_ari_response_error(response, 409, "Conflict",
			"Can only %s while media is playing", args->operation);
		return;
	}
}
