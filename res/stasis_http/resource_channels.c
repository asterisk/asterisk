/* -*- C -*-
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
 * \brief Implementation for stasis-http stubs.
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
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_app_playback.h"
#include "asterisk/stasis_channels.h"
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
	struct stasis_http_response *response,
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
			stasis_http_response_error(response, 404, "Not Found",
				   "Channel not found");
			return NULL;
		}

		stasis_http_response_error(response, 409, "Conflict",
			   "Channel not in Stasis application");
		return NULL;
	}

	ao2_ref(control, +1);
	return control;
}

void stasis_http_dial(struct ast_variable *headers, struct ast_dial_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_dial\n");
}

void stasis_http_continue_in_dialplan(
	struct ast_variable *headers,
	struct ast_continue_in_dialplan_args *args,
	struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	stasis_app_control_continue(control);
	stasis_http_response_no_content(response);
}

void stasis_http_answer_channel(struct ast_variable *headers,
				struct ast_answer_channel_args *args,
				struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		return;
	}

	if (stasis_app_control_answer(control) != 0) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Failed to answer channel");
		return;
	}

	stasis_http_response_no_content(response);
}

void stasis_http_mute_channel(struct ast_variable *headers, struct ast_mute_channel_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_mute_channel\n");
}
void stasis_http_unmute_channel(struct ast_variable *headers, struct ast_unmute_channel_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_unmute_channel\n");
}
void stasis_http_hold_channel(struct ast_variable *headers, struct ast_hold_channel_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_hold_channel\n");
}
void stasis_http_unhold_channel(struct ast_variable *headers, struct ast_unhold_channel_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_unhold_channel\n");
}

void stasis_http_play_on_channel(struct ast_variable *headers,
	struct ast_play_on_channel_args *args,
	struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);
	RAII_VAR(char *, playback_url, NULL, ast_free);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	const char *language;

	ast_assert(response != NULL);

	control = find_control(response, args->channel_id);
	if (control == NULL) {
		/* Response filled in by find_control */
		return;
	}

	snapshot = stasis_app_control_get_snapshot(control);
	if (!snapshot) {
		stasis_http_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	if (args->skipms < 0) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"skipms cannot be negative");
		return;
	}

	if (args->offsetms < 0) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"offsetms cannot be negative");
		return;
	}

	language = S_OR(args->lang, snapshot->language);

	playback = stasis_app_control_play_uri(control, args->media, language,
		args->skipms, args->offsetms);
	if (!playback) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Failed to queue media for playback");
		return;
	}

	ast_asprintf(&playback_url, "/playback/%s",
		stasis_app_playback_get_id(playback));
	if (!playback_url) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}

	json = stasis_app_playback_to_json(playback);
	if (!json) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Out of memory");
		return;
	}

	stasis_http_response_created(response, playback_url, json);
}
void stasis_http_record_channel(struct ast_variable *headers, struct ast_record_channel_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_record_channel\n");
}
void stasis_http_get_channel(struct ast_variable *headers,
			     struct ast_get_channel_args *args,
			     struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct stasis_caching_topic *caching_topic;
	struct ast_channel_snapshot *snapshot;

	caching_topic = ast_channel_topic_all_cached();
	if (!caching_topic) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}

	msg = stasis_cache_get(caching_topic, ast_channel_snapshot_type(),
			       args->channel_id);
	if (!msg) {
		stasis_http_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	snapshot = stasis_message_data(msg);
	ast_assert(snapshot != NULL);

	stasis_http_response_ok(response,
				ast_channel_snapshot_to_json(snapshot));
}

void stasis_http_delete_channel(struct ast_variable *headers,
				struct ast_delete_channel_args *args,
				struct stasis_http_response *response)
{
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);

	chan = ast_channel_get_by_name(args->channel_id);
	if (chan == NULL) {
		stasis_http_response_error(
			response, 404, "Not Found",
			"Channel not found");
		return;
	}

	ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);

	stasis_http_response_no_content(response);
}

void stasis_http_get_channels(struct ast_variable *headers,
			      struct ast_get_channels_args *args,
			      struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, snapshots, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;

	caching_topic = ast_channel_topic_all_cached();
	if (!caching_topic) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}
	ao2_ref(caching_topic, +1);

	snapshots = stasis_cache_dump(caching_topic, ast_channel_snapshot_type());
	if (!snapshots) {
		stasis_http_response_alloc_failed(response);
		return;
	}

	json = ast_json_array_create();
	if (!json) {
		stasis_http_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(snapshots, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, msg, obj, ao2_cleanup);
		struct ast_channel_snapshot *snapshot = stasis_message_data(msg);
		int r = ast_json_array_append(
			json, ast_channel_snapshot_to_json(snapshot));
		if (r != 0) {
			stasis_http_response_alloc_failed(response);
			return;
		}
	}
	ao2_iterator_destroy(&i);

	stasis_http_response_ok(response, ast_json_ref(json));
}

void stasis_http_originate(struct ast_variable *headers,
			   struct ast_originate_args *args,
			   struct stasis_http_response *response)
{
	if (args->endpoint) {
		ast_log(LOG_DEBUG, "Dialing specific endpoint %s\n", args->endpoint);
	}

	ast_log(LOG_DEBUG, "Dialing %s@%s\n", args->extension, args->context);
	/* ast_pbx_outgoing_app - originates a channel, putting it into an application */
}
