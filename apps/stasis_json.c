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
 * \brief Stasis application JSON converters.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/app_stasis.h"
#include "asterisk/stasis_channels.h"

struct ast_json *ast_channel_snapshot_to_json(const struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct ast_json *, json_chan, NULL, ast_json_unref);
	int r = 0;

	if (snapshot == NULL) {
		return NULL;
	}

	json_chan = ast_json_object_create();
	if (!json_chan) { ast_log(LOG_ERROR, "Error creating channel json object\n"); return NULL; }

	r = ast_json_object_set(json_chan, "name", ast_json_string_create(snapshot->name));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "state", ast_json_string_create(ast_state2str(snapshot->state)));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "accountcode", ast_json_string_create(snapshot->accountcode));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "peeraccount", ast_json_string_create(snapshot->peeraccount));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "userfield", ast_json_string_create(snapshot->userfield));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "uniqueid", ast_json_string_create(snapshot->uniqueid));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "linkedid", ast_json_string_create(snapshot->linkedid));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "parkinglot", ast_json_string_create(snapshot->parkinglot));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "hangupsource", ast_json_string_create(snapshot->hangupsource));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "appl", ast_json_string_create(snapshot->appl));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "data", ast_json_string_create(snapshot->data));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "dialplan", ast_json_dialplan_cep(snapshot->context, snapshot->exten, snapshot->priority));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "caller", ast_json_name_number(snapshot->caller_name, snapshot->caller_number));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "connected", ast_json_name_number(snapshot->connected_name, snapshot->connected_number));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "creationtime", ast_json_timeval(&snapshot->creationtime, NULL));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }

	return ast_json_ref(json_chan);
}

