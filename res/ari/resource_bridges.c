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

#include "resource_bridges.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_playback.h"
#include "asterisk/stasis_app_recording.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/core_unreal.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/format_cap.h"
#include "asterisk/file.h"
#include "asterisk/musiconhold.h"
#include "asterisk/format_cache.h"

/*!
 * \brief Finds a bridge, filling the response with an error, if appropriate.
 *
 * \param[out] response Response to fill with an error if control is not found.
 * \param bridge_id ID of the bridge to lookup.
 *
 * \return Bridget.
 * \return \c NULL if bridge does not exist.
 */
static struct ast_bridge *find_bridge(
	struct ast_ari_response *response,
	const char *bridge_id)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	bridge = stasis_app_bridge_find_by_id(bridge_id);
	if (bridge == NULL) {
		RAII_VAR(struct ast_bridge_snapshot *, snapshot,
			ast_bridge_get_snapshot_by_uniqueid(bridge_id), ao2_cleanup);
		if (!snapshot) {
			ast_ari_response_error(response, 404, "Not found",
				"Bridge not found");
			return NULL;
		}

		ast_ari_response_error(response, 409, "Conflict",
			"Bridge not in Stasis application");
		return NULL;
	}

	ao2_ref(bridge, +1);
	return bridge;
}

/*!
 * \brief Finds the control object for a channel, filling the response with an
 * error, if appropriate.
 * \param[out] response Response to fill with an error if control is not found.
 * \param channel_id ID of the channel to lookup.
 * \return Channel control object.
 * \return \c NULL if control object does not exist.
 */
static struct stasis_app_control *find_channel_control(
	struct ast_ari_response *response,
	const char *channel_id)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	control = stasis_app_control_find_by_channel_id(channel_id);
	if (control == NULL) {
		/* Distinguish between 400 and 422 errors */
		RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL,
			ao2_cleanup);
		snapshot = ast_channel_snapshot_get_latest(channel_id);
		if (snapshot == NULL) {
			ast_log(LOG_DEBUG, "Couldn't find '%s'\n", channel_id);
			ast_ari_response_error(response, 400, "Bad Request",
				"Channel not found");
			return NULL;
		}

		ast_log(LOG_DEBUG, "Found non-stasis '%s'\n", channel_id);
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Channel not in Stasis application");
		return NULL;
	}

	ao2_ref(control, +1);
	return control;
}

struct control_list {
	size_t count;
	struct stasis_app_control *controls[];
};

static void control_list_dtor(void *obj) {
	struct control_list *list = obj;
	size_t i;

	for (i = 0; i < list->count; ++i) {
		ao2_cleanup(list->controls[i]);
		list->controls[i] = NULL;
	}
}

static struct control_list *control_list_create(struct ast_ari_response *response, size_t count, const char **channels) {
	RAII_VAR(struct control_list *, list, NULL, ao2_cleanup);
	size_t i;

	if (count == 0 || !channels) {
		ast_ari_response_error(response, 400, "Bad Request", "Missing parameter channel");
		return NULL;
	}

	list = ao2_alloc(sizeof(*list) + count * sizeof(list->controls[0]), control_list_dtor);
	if (!list) {
		ast_ari_response_alloc_failed(response);
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		if (ast_strlen_zero(channels[i])) {
			continue;
		}
		list->controls[list->count] =
			find_channel_control(response, channels[i]);
		if (!list->controls[list->count]) {
			/* response filled in by find_channel_control() */
			return NULL;
		}
		++list->count;
	}

	if (list->count == 0) {
		ast_ari_response_error(response, 400, "Bad Request", "Missing parameter channel");
		return NULL;
	}

	ao2_ref(list, +1);
	return list;
}

static int check_add_remove_channel(struct ast_ari_response *response,
				    struct stasis_app_control *control,
				    enum stasis_app_control_channel_result result)
{
	switch (result) {
	case STASIS_APP_CHANNEL_RECORDING :
		ast_ari_response_error(
			response, 409, "Conflict", "Channel %s currently recording",
			stasis_app_control_get_channel_id(control));
		return -1;
	case STASIS_APP_CHANNEL_OKAY:
		return 0;
	}
	return 0;
}

void ast_ari_bridges_add_channel(struct ast_variable *headers,
	struct ast_ari_bridges_add_channel_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct control_list *, list, NULL, ao2_cleanup);
	size_t i;
	int has_error = 0;

	if (!bridge) {
		/* Response filled in by find_bridge() */
		return;
	}

	list = control_list_create(response, args->channel_count, args->channel);
	if (!list) {
		/* Response filled in by control_list_create() */
		return;
	}

	for (i = 0; i < list->count; ++i) {
		stasis_app_control_clear_roles(list->controls[i]);
		if (!ast_strlen_zero(args->role)) {
			if (stasis_app_control_add_role(list->controls[i], args->role)) {
				ast_ari_response_alloc_failed(response);
				return;
			}
		}

		/* Apply bridge features to each of the channel controls */
		if (!stasis_app_control_bridge_features_init(list->controls[i])) {
			stasis_app_control_absorb_dtmf_in_bridge(list->controls[i], args->absorb_dtmf);
			stasis_app_control_mute_in_bridge(list->controls[i], args->mute);
			stasis_app_control_inhibit_colp_in_bridge(list->controls[i], args->inhibit_connected_line_updates);
		}
	}

	for (i = 0; i < list->count; ++i) {
		if ((has_error = check_add_remove_channel(response, list->controls[i],
			     stasis_app_control_add_channel_to_bridge(
				    list->controls[i], bridge)))) {
			break;
		}
	}

	if (!has_error) {
		ast_ari_response_no_content(response);
	}
}

void ast_ari_bridges_remove_channel(struct ast_variable *headers,
	struct ast_ari_bridges_remove_channel_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct control_list *, list, NULL, ao2_cleanup);
	size_t i;

	if (!bridge) {
		/* Response filled in by find_bridge() */
		return;
	}

	list = control_list_create(response, args->channel_count, args->channel);
	if (!list) {
		/* Response filled in by control_list_create() */
		return;
	}

	/* Make sure all of the channels are in this bridge */
	for (i = 0; i < list->count; ++i) {
		if (stasis_app_get_bridge(list->controls[i]) != bridge) {
			ast_log(LOG_WARNING, "Channel %s not in bridge %s\n",
				args->channel[i], args->bridge_id);
			ast_ari_response_error(response, 422,
				"Unprocessable Entity",
				"Channel not in this bridge");
			return;
		}
	}

	/* Now actually remove it */
	for (i = 0; i < list->count; ++i) {
		stasis_app_control_remove_channel_from_bridge(list->controls[i],
			bridge);
	}

	ast_ari_response_no_content(response);
}

struct bridge_channel_control_thread_data {
	struct ast_channel *bridge_channel;
	struct stasis_app_control *control;
	struct stasis_forward *forward;
	char bridge_id[0];
};

static void *bridge_channel_control_thread(void *data)
{
	struct bridge_channel_control_thread_data *thread_data = data;
	struct ast_channel *bridge_channel = thread_data->bridge_channel;
	struct stasis_app_control *control = thread_data->control;
	struct stasis_forward *forward = thread_data->forward;
	ast_callid callid = ast_channel_callid(bridge_channel);
	char *bridge_id = ast_strdupa(thread_data->bridge_id);

	if (callid) {
		ast_callid_threadassoc_add(callid);
	}

	ast_free(thread_data);
	thread_data = NULL;

	stasis_app_control_execute_until_exhausted(bridge_channel, control);
	stasis_app_control_flush_queue(control);

	stasis_app_bridge_playback_channel_remove(bridge_id, control);
	stasis_forward_cancel(forward);
	ao2_cleanup(control);
	ast_hangup(bridge_channel);
	return NULL;
}

static struct ast_channel *prepare_bridge_media_channel(const char *type)
{
	RAII_VAR(struct ast_format_cap *, cap, NULL, ao2_cleanup);
	struct ast_channel *chan;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		return NULL;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	chan = ast_request(type, cap, NULL, NULL, "ARI", NULL);
	if (!chan) {
		return NULL;
	}

	if (stasis_app_channel_unreal_set_internal(chan)) {
		ast_channel_cleanup(chan);
		return NULL;
	}
	return chan;
}

/*!
 * \brief Performs common setup for a bridge playback operation
 * with both new controls and when existing controls are  found.
 *
 * \param args_media medias to play
 * \param args_media_count number of media items in \c media
 * \param args_lang language string split from arguments
 * \param args_offset_ms milliseconds offset split from arguments
 * \param args_playback_id string to use for playback split from
 *        arguments (null valid)
 * \param response ARI response being built
 * \param bridge Bridge the playback is being performed on
 * \param control Control being used for the playback channel
 * \param json contents of the response to ARI
 * \param playback_url stores playback URL for use with response
 *
 * \retval -1 operation failed
 * \retval operation was successful
 */
static int ari_bridges_play_helper(const char **args_media,
	size_t args_media_count,
	const char *args_lang,
	int args_offset_ms,
	int args_skipms,
	const char *args_playback_id,
	struct ast_ari_response *response,
	struct ast_bridge *bridge,
	struct stasis_app_control *control,
	struct ast_json **json,
	char **playback_url)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_playback *, playback, NULL, ao2_cleanup);

	const char *language;

	snapshot = stasis_app_control_get_snapshot(control);
	if (!snapshot) {
		ast_ari_response_error(
			response, 500, "Internal Error", "Failed to get control snapshot");
		return -1;
	}

	language = S_OR(args_lang, snapshot->base->language);

	playback = stasis_app_control_play_uri(control, args_media, args_media_count,
		language, bridge->uniqueid, STASIS_PLAYBACK_TARGET_BRIDGE, args_skipms,
		args_offset_ms, args_playback_id);

	if (!playback) {
		ast_ari_response_alloc_failed(response);
		return -1;
	}

	if (ast_asprintf(playback_url, "/playbacks/%s",
			stasis_app_playback_get_id(playback)) == -1) {
		ast_ari_response_alloc_failed(response);
		return -1;
	}

	*json = stasis_app_playback_to_json(playback);
	if (!*json) {
		ast_ari_response_alloc_failed(response);
		return -1;
	}

	return 0;
}

static void ari_bridges_play_new(const char **args_media,
	size_t args_media_count,
	const char *args_lang,
	int args_offset_ms,
	int args_skipms,
	const char *args_playback_id,
	struct ast_ari_response *response,
	struct ast_bridge *bridge)
{
	RAII_VAR(struct ast_channel *, play_channel, NULL, ast_hangup);
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct stasis_forward *, channel_forward, NULL, stasis_forward_cancel);
	RAII_VAR(char *, playback_url, NULL, ast_free);

	struct stasis_topic *channel_topic;
	struct stasis_topic *bridge_topic;
	struct bridge_channel_control_thread_data *thread_data;
	pthread_t threadid;

	if (!(play_channel = prepare_bridge_media_channel("Announcer"))) {
		ast_ari_response_error(
			response, 500, "Internal Error", "Could not create playback channel");
		return;
	}
	ast_debug(1, "Created announcer channel '%s'\n", ast_channel_name(play_channel));

	bridge_topic = ast_bridge_topic(bridge);
	channel_topic = ast_channel_topic(play_channel);

	/* Forward messages from the playback channel topic to the bridge topic so that anything listening for
	 * messages on the bridge topic will receive the playback start/stop messages. Other messages that would
	 * go to this channel will be suppressed since the channel is marked as internal.
	 */
	if (!bridge_topic || !channel_topic || !(channel_forward = stasis_forward_all(channel_topic, bridge_topic))) {
		ast_ari_response_error(
			response, 500, "Internal Error", "Could not forward playback channel stasis messages to bridge topic");
		return;
	}

	if (ast_unreal_channel_push_to_bridge(play_channel, bridge,
		AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE | AST_BRIDGE_CHANNEL_FLAG_LONELY)) {
		ast_ari_response_error(
			response, 500, "Internal Error", "Failed to put playback channel into the bridge");
		return;
	}

	control = stasis_app_control_create(play_channel);
	if (control == NULL) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ao2_lock(control);
	if (ari_bridges_play_helper(args_media, args_media_count, args_lang,
			args_offset_ms, args_skipms, args_playback_id, response, bridge,
			control, &json, &playback_url)) {
		ao2_unlock(control);
		return;
	}
	ao2_unlock(control);

	if (stasis_app_bridge_playback_channel_add(bridge, play_channel, control)) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	/* Give play_channel and control reference to the thread data */
	thread_data = ast_malloc(sizeof(*thread_data) + strlen(bridge->uniqueid) + 1);
	if (!thread_data) {
		stasis_app_bridge_playback_channel_remove((char *)bridge->uniqueid, control);
		ast_ari_response_alloc_failed(response);
		return;
	}

	thread_data->bridge_channel = play_channel;
	thread_data->control = control;
	thread_data->forward = channel_forward;
	/* Safe */
	strcpy(thread_data->bridge_id, bridge->uniqueid);

	if (ast_pthread_create_detached(&threadid, NULL, bridge_channel_control_thread, thread_data)) {
		stasis_app_bridge_playback_channel_remove((char *)bridge->uniqueid, control);
		ast_ari_response_alloc_failed(response);
		ast_free(thread_data);
		return;
	}

	/* These are owned by the other thread now, so we don't want RAII_VAR disposing of them. */
	play_channel = NULL;
	control = NULL;
	channel_forward = NULL;

	ast_ari_response_created(response, playback_url, ast_json_ref(json));
}

enum play_found_result {
	PLAY_FOUND_SUCCESS,
	PLAY_FOUND_FAILURE,
	PLAY_FOUND_CHANNEL_UNAVAILABLE,
};

/*!
 * \brief Performs common setup for a bridge playback operation
 * with both new controls and when existing controls are  found.
 *
 * \param args_media medias to play
 * \param args_media_count number of media items in \c media
 * \param args_lang language string split from arguments
 * \param args_offset_ms milliseconds offset split from arguments
 * \param args_playback_id string to use for playback split from
 *        arguments (null valid)
 * \param response ARI response being built
 * \param bridge Bridge the playback is being performed on
 * \param found_channel The channel that was found controlling playback
 *
 * \retval PLAY_FOUND_SUCCESS The operation was successful
 * \retval PLAY_FOUND_FAILURE The operation failed (terminal failure)
 * \retval PLAY_FOUND_CHANNEL_UNAVAILABLE The operation failed because
 * the channel requested to playback with is breaking down.
 */
static enum play_found_result ari_bridges_play_found(const char **args_media,
	size_t args_media_count,
	const char *args_lang,
	int args_offset_ms,
	int args_skipms,
	const char *args_playback_id,
	struct ast_ari_response *response,
	struct ast_bridge *bridge,
	struct ast_channel *found_channel)
{
	RAII_VAR(struct ast_channel *, play_channel, found_channel, ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(char *, playback_url, NULL, ast_free);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	control = stasis_app_control_find_by_channel(play_channel);
	if (!control) {
		return PLAY_FOUND_CHANNEL_UNAVAILABLE;
	}

	ao2_lock(control);
	if (stasis_app_control_is_done(control)) {
		/* We failed to queue the action. Bailout and return that we aren't terminal. */
		ao2_unlock(control);
		return PLAY_FOUND_CHANNEL_UNAVAILABLE;
	}

	if (ari_bridges_play_helper(args_media, args_media_count,
			args_lang, args_offset_ms, args_skipms, args_playback_id,
			response, bridge, control, &json, &playback_url)) {
		ao2_unlock(control);
		return PLAY_FOUND_FAILURE;
	}
	ao2_unlock(control);

	ast_ari_response_created(response, playback_url, ast_json_ref(json));
	return PLAY_FOUND_SUCCESS;
}

static void ari_bridges_handle_play(
	const char *args_bridge_id,
	const char **args_media,
	size_t args_media_count,
	const char *args_lang,
	int args_offset_ms,
	int args_skipms,
	const char *args_playback_id,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args_bridge_id), ao2_cleanup);
	struct ast_channel *play_channel;

	ast_assert(response != NULL);

	if (!bridge) {
		return;
	}

	while ((play_channel = stasis_app_bridge_playback_channel_find(bridge))) {
		/* If ari_bridges_play_found fails because the channel is unavailable for
		 * playback, The channel will be removed from the playback list soon. We
		 * can keep trying to get channels from the list until we either get one
		 * that will work or else there isn't a channel for this bridge anymore,
		 * in which case we'll revert to ari_bridges_play_new.
		 */
		if (ari_bridges_play_found(args_media, args_media_count, args_lang,
				args_offset_ms, args_skipms, args_playback_id, response,bridge,
				play_channel) == PLAY_FOUND_CHANNEL_UNAVAILABLE) {
			continue;
		}
		return;
	}

	ari_bridges_play_new(args_media, args_media_count, args_lang, args_offset_ms,
		args_skipms, args_playback_id, response, bridge);
}


void ast_ari_bridges_play(struct ast_variable *headers,
	struct ast_ari_bridges_play_args *args,
	struct ast_ari_response *response)
{
	ari_bridges_handle_play(args->bridge_id,
	args->media,
	args->media_count,
	args->lang,
	args->offsetms,
	args->skipms,
	args->playback_id,
	response);
}

void ast_ari_bridges_play_with_id(struct ast_variable *headers,
	struct ast_ari_bridges_play_with_id_args *args,
	struct ast_ari_response *response)
{
	ari_bridges_handle_play(args->bridge_id,
	args->media,
	args->media_count,
	args->lang,
	args->offsetms,
	args->skipms,
	args->playback_id,
	response);
}

void ast_ari_bridges_record(struct ast_variable *headers,
	struct ast_ari_bridges_record_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct ast_channel *, record_channel, NULL, ast_hangup);
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_recording *, recording, NULL, ao2_cleanup);
	RAII_VAR(char *, recording_url, NULL, ast_free);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct stasis_app_recording_options *, options, NULL, ao2_cleanup);
	RAII_VAR(char *, uri_encoded_name, NULL, ast_free);
	RAII_VAR(struct stasis_forward *, channel_forward, NULL, stasis_forward_cancel);

	struct stasis_topic *channel_topic;
	struct stasis_topic *bridge_topic;
	size_t uri_name_maxlen;
	struct bridge_channel_control_thread_data *thread_data;
	pthread_t threadid;

	ast_assert(response != NULL);

	if (bridge == NULL) {
		return;
	}

	if (!(record_channel = prepare_bridge_media_channel("Recorder"))) {
		ast_ari_response_error(
			response, 500, "Internal Server Error", "Failed to create recording channel");
		return;
	}

	bridge_topic = ast_bridge_topic(bridge);
	channel_topic = ast_channel_topic(record_channel);

	/* Forward messages from the recording channel topic to the bridge topic so that anything listening for
	 * messages on the bridge topic will receive the recording start/stop messages. Other messages that would
	 * go to this channel will be suppressed since the channel is marked as internal.
	 */
	if (!bridge_topic || !channel_topic || !(channel_forward = stasis_forward_all(channel_topic, bridge_topic))) {
		ast_ari_response_error(
			response, 500, "Internal Error", "Could not forward record channel stasis messages to bridge topic");
		return;
	}

	if (ast_unreal_channel_push_to_bridge(record_channel, bridge,
		AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE | AST_BRIDGE_CHANNEL_FLAG_LONELY)) {
		ast_ari_response_error(
			response, 500, "Internal Error", "Failed to put recording channel into the bridge");
		return;
	}

	control = stasis_app_control_create(record_channel);
	if (control == NULL) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	options = stasis_app_recording_options_create(args->name, args->format);
	if (options == NULL) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_string_field_build(options, target, "bridge:%s", args->bridge_id);
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
			ast_ari_response_alloc_failed(response);
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
		ast_ari_response_alloc_failed(response);
		return;
	}
	ast_uri_encode(args->name, uri_encoded_name, uri_name_maxlen, ast_uri_http);

	if (ast_asprintf(&recording_url, "/recordings/live/%s",
			uri_encoded_name) == -1) {
		recording_url = NULL;
		ast_ari_response_alloc_failed(response);
		return;
	}

	json = stasis_app_recording_to_json(recording);
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	thread_data = ast_calloc(1, sizeof(*thread_data));
	if (!thread_data) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	thread_data->bridge_channel = record_channel;
	thread_data->control = control;
	thread_data->forward = channel_forward;

	if (ast_pthread_create_detached(&threadid, NULL, bridge_channel_control_thread, thread_data)) {
		ast_ari_response_alloc_failed(response);
		ast_free(thread_data);
		return;
	}

	/* These are owned by the other thread now, so we don't want RAII_VAR disposing of them. */
	record_channel = NULL;
	control = NULL;
	channel_forward = NULL;

	ast_ari_response_created(response, recording_url, ast_json_ref(json));
}

void ast_ari_bridges_start_moh(struct ast_variable *headers,
	struct ast_ari_bridges_start_moh_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	struct ast_channel *moh_channel;
	const char *moh_class = args->moh_class;

	if (!bridge) {
		/* The response is provided by find_bridge() */
		return;
	}

	moh_channel = stasis_app_bridge_moh_channel(bridge);
	if (!moh_channel) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_moh_start(moh_channel, moh_class, NULL);
	ast_channel_cleanup(moh_channel);

	ast_ari_response_no_content(response);

}

void ast_ari_bridges_stop_moh(struct ast_variable *headers,
	struct ast_ari_bridges_stop_moh_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);

	if (!bridge) {
		/* the response is provided by find_bridge() */
		return;
	}

	if (stasis_app_bridge_moh_stop(bridge)) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Bridge isn't playing music");
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_bridges_get(struct ast_variable *headers,
	struct ast_ari_bridges_get_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, ast_bridge_get_snapshot_by_uniqueid(args->bridge_id), ao2_cleanup);
	if (!snapshot) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Bridge not found");
		return;
	}

	ast_ari_response_ok(response,
		ast_bridge_snapshot_to_json(snapshot, stasis_app_get_sanitizer()));
}

void ast_ari_bridges_destroy(struct ast_variable *headers,
	struct ast_ari_bridges_destroy_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	if (!bridge) {
		return;
	}

	stasis_app_bridge_destroy(args->bridge_id);
	ast_ari_response_no_content(response);
}

void ast_ari_bridges_list(struct ast_variable *headers,
	struct ast_ari_bridges_list_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ao2_container *, bridges, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	struct ast_bridge *bridge;

	bridges = ast_bridges();
	if (!bridges) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	json = ast_json_array_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(bridges, 0);
	while ((bridge = ao2_iterator_next(&i))) {
		struct ast_bridge_snapshot *snapshot;
		struct ast_json *json_bridge = NULL;

		/* Invisible bridges don't get shown externally and have no snapshot */
		if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_INVISIBLE)) {
			ao2_ref(bridge, -1);
			continue;
		}

		snapshot = ast_bridge_get_snapshot(bridge);
		if (snapshot) {
			json_bridge = ast_bridge_snapshot_to_json(snapshot, stasis_app_get_sanitizer());
			ao2_ref(snapshot, -1);
		}

		ao2_ref(bridge, -1);

		if (!json_bridge || ast_json_array_append(json, json_bridge)) {
			ao2_iterator_destroy(&i);
			ast_ari_response_alloc_failed(response);
			return;
		}
	}
	ao2_iterator_destroy(&i);

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_bridges_create(struct ast_variable *headers,
	struct ast_ari_bridges_create_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, stasis_app_bridge_create(args->type, args->name, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);

	if (!bridge) {
		ast_ari_response_error(
			response, 500, "Internal Error",
			"Unable to create bridge");
		return;
	}

	ast_bridge_lock(bridge);
	snapshot = ast_bridge_snapshot_create(bridge);
	ast_bridge_unlock(bridge);

	if (!snapshot) {
		ast_ari_response_error(
			response, 500, "Internal Error",
			"Unable to create snapshot for new bridge");
		return;
	}

	ast_ari_response_ok(response,
		ast_bridge_snapshot_to_json(snapshot, stasis_app_get_sanitizer()));
}

void ast_ari_bridges_create_with_id(struct ast_variable *headers,
	struct ast_ari_bridges_create_with_id_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);

	if (bridge) {
		/* update */
		if (!ast_strlen_zero(args->name)
			&& strcmp(args->name, bridge->name)) {
			ast_ari_response_error(
				response, 500, "Internal Error",
				"Changing bridge name is not implemented");
			return;
		}
		if (!ast_strlen_zero(args->type)) {
			ast_ari_response_error(
				response, 500, "Internal Error",
				"Supplying a bridge type when updating a bridge is not allowed.");
			return;
		}
		ast_ari_response_ok(response,
			ast_bridge_snapshot_to_json(snapshot, stasis_app_get_sanitizer()));
		return;
	}

	bridge = stasis_app_bridge_create(args->type, args->name, args->bridge_id);
	if (!bridge) {
		ast_ari_response_error(
			response, 500, "Internal Error",
			"Unable to create bridge");
		return;
	}

	ast_bridge_lock(bridge);
	snapshot = ast_bridge_snapshot_create(bridge);
	ast_bridge_unlock(bridge);

	if (!snapshot) {
		ast_ari_response_error(
			response, 500, "Internal Error",
			"Unable to create snapshot for new bridge");
		return;
	}

	ast_ari_response_ok(response,
		ast_bridge_snapshot_to_json(snapshot, stasis_app_get_sanitizer()));
}

static int bridge_set_video_source_cb(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge *bridge = data;

	ast_bridge_lock(bridge);
	ast_bridge_set_single_src_video_mode(bridge, chan);
	ast_bridge_unlock(bridge);

	return 0;
}

void ast_ari_bridges_set_video_source(struct ast_variable *headers,
	struct ast_ari_bridges_set_video_source_args *args, struct ast_ari_response *response)
{
	struct ast_bridge *bridge;
	struct stasis_app_control *control;

	bridge = find_bridge(response, args->bridge_id);
	if (!bridge) {
		return;
	}

	control = find_channel_control(response, args->channel_id);
	if (!control) {
		ao2_ref(bridge, -1);
		return;
	}

	if (stasis_app_get_bridge(control) != bridge) {
		ast_ari_response_error(response, 422,
			"Unprocessable Entity",
			"Channel not in this bridge");
		ao2_ref(bridge, -1);
		ao2_ref(control, -1);
		return;
	}

	stasis_app_send_command(control, bridge_set_video_source_cb,
		ao2_bump(bridge), __ao2_cleanup);

	ao2_ref(bridge, -1);
	ao2_ref(control, -1);

	ast_ari_response_no_content(response);
}

void ast_ari_bridges_clear_video_source(struct ast_variable *headers,
	struct ast_ari_bridges_clear_video_source_args *args, struct ast_ari_response *response)
{
	struct ast_bridge *bridge;

	bridge = find_bridge(response, args->bridge_id);
	if (!bridge) {
		return;
	}

	ast_bridge_lock(bridge);
	ast_bridge_set_talker_src_video_mode(bridge);
	ast_bridge_unlock(bridge);

	ao2_ref(bridge, -1);
	ast_ari_response_no_content(response);
}
