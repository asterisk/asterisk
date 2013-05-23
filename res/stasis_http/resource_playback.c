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
	RAII_VAR(struct ast_json *, playback, NULL, ast_json_unref);
	playback = stasis_app_playback_find_by_id(args->playback_id);
	if (playback == NULL) {
		stasis_http_response_error(response, 404, "Not Found",
			"Playback not found");
		return;
	}

	stasis_http_response_ok(response, ast_json_ref(playback));
}
void stasis_http_stop_playback(struct ast_variable *headers, struct ast_stop_playback_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_stop_playback\n");
}
void stasis_http_control_playback(struct ast_variable *headers, struct ast_control_playback_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_control_playback\n");
}
