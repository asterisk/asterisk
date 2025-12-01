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
 * \brief /api-docs/events.{format} implementation- WebSocket resource
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "resource_events.h"
#include "internal.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_app_broadcast.h"

void ast_ari_events_user_event(struct ast_variable *headers,
	struct ast_ari_events_user_event_args *args,
	struct ast_ari_response *response)
{
	enum stasis_app_user_event_res res;
	struct ast_json *json_variables = NULL;

	if (args->variables) {
		ast_ari_events_user_event_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
	}

	if (ast_strlen_zero(args->application)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Missing parameter application");
		return;
	}

	res = stasis_app_user_event(args->application,
		args->event_name,
		args->source, args->source_count,
		json_variables);

	switch (res) {
	case STASIS_APP_USER_OK:
		ast_ari_response_no_content(response);
		break;

	case STASIS_APP_USER_APP_NOT_FOUND:
		ast_ari_response_error(response, 404, "Not Found",
			"Application not found");
		break;

	case STASIS_APP_USER_EVENT_SOURCE_NOT_FOUND:
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Event source was not found");
		break;

	case STASIS_APP_USER_EVENT_SOURCE_BAD_SCHEME:
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid event source URI scheme");
		break;

	case STASIS_APP_USER_USEREVENT_INVALID:
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid userevent data");
		break;

	case STASIS_APP_USER_INTERNAL_ERROR:
	default:
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Error processing request");
	}
}

void ast_ari_events_claim_channel(struct ast_variable *headers,
	struct ast_ari_events_claim_channel_args *args,
	struct ast_ari_response *response)
{
	int res;

	if (ast_strlen_zero(args->channel_id)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"channelId parameter is required");
		return;
	}

	if (ast_strlen_zero(args->application)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"application parameter is required");
		return;
	}

	res = stasis_app_claim_channel(args->channel_id, args->application);

	switch (res) {
	case 0:
		/* Success */
		ast_ari_response_no_content(response);
		break;
	case -1:
		/* Channel not found */
		ast_ari_response_error(response, 404, "Not Found",
			"Channel not found or not in broadcast state");
		break;
	case -2:
		/* Already claimed */
		ast_ari_response_error(response, 409, "Conflict",
			"Channel has already been claimed by another application");
		break;
	case AST_OPTIONAL_API_UNAVAILABLE:
		/* Module not loaded */
		ast_ari_response_error(response, 501, "Not Implemented",
			"Broadcast functionality not available (res_stasis_broadcast not loaded)");
		break;
	default:
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Failed to claim channel");
		break;
	}
}
