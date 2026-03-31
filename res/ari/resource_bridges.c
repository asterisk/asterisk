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
 * \retval NULL if bridge does not exist.
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
 * \retval NULL if control object does not exist.
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
	char *bridge_id;
};

static void *bridge_channel_control_thread(void *data)
{
	struct bridge_channel_control_thread_data *thread_data = data;
	struct ast_channel *bridge_channel = thread_data->bridge_channel;
	struct stasis_app_control *control = thread_data->control;
	struct stasis_forward *forward = thread_data->forward;
	ast_callid callid = ast_channel_callid(bridge_channel);
	char *bridge_id = thread_data->bridge_id;

	if (callid) {
		ast_callid_threadassoc_add(callid);
	}

	ast_free(thread_data);
	thread_data = NULL;

	stasis_app_control_execute_until_exhausted(bridge_channel, control);
	stasis_app_control_flush_queue(control);

	if (bridge_id) {
		stasis_app_bridge_playback_channel_control_remove(bridge_id, control);
		ast_free(bridge_id);
	}
	stasis_forward_cancel(forward);
	ao2_cleanup(control);
	ast_hangup(bridge_channel);
	return NULL;
}

static struct ast_channel *prepare_bridge_media_channel(const char *type,
	struct ast_format *channel_format)
{
	RAII_VAR(struct ast_format_cap *, cap, NULL, ao2_cleanup);
	struct ast_channel *chan;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		return NULL;
	}

	/* This bumps the format's refcount */
	ast_format_cap_append(cap, channel_format, 0);

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
 * \param args_skipms
 * \param args_playback_id string to use for playback split from
 *        arguments (null valid)
 * \param response ARI response being built
 * \param bridge Bridge the playback is being performed on
 * \param control Control being used for the playback channel
 * \param json contents of the response to ARI
 * \param playback_url stores playback URL for use with response
 *
 * \retval -1 operation failed
 * \return operation was successful
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
	const char *args_format,
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
	struct ast_format *channel_format = NULL;

	struct ast_frame prog = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_PROGRESS,
	};

	/*
	 * Determine the format for the playback channel.
	 * If a format was specified, use that if it's valid.
	 * Otherwise, if the bridge is empty, use slin.
	 * If the bridge has one channel, use that channel's raw write format.
	 * If the bridge has multiple channels, use the slin format that
	 * will handle the highest sample rate of the raw write format of all the channels.
	 */
	if (!ast_strlen_zero(args_format)) {
		channel_format = ast_format_cache_get(args_format);
		if (!channel_format) {
			ast_ari_response_error(
				response, 422, "Unprocessable Entity",
				"specified announcer_format is unknown on this system");
			return;
		}
	} else {
		ast_bridge_lock(bridge);
		if (bridge->num_channels == 0) {
			channel_format = ao2_bump(ast_format_slin);
		} else if (bridge->num_channels == 1) {
			struct ast_bridge_channel *bc = NULL;
			bc = AST_LIST_FIRST(&bridge->channels);
			if (bc) {
				channel_format = ast_channel_rawwriteformat(bc->chan);
				if (channel_format) {
					channel_format = ao2_bump(channel_format);
				}
			}
		} else {
			struct ast_bridge_channel *bc = NULL;
			unsigned int max_sample_rate = 0;
			AST_LIST_TRAVERSE(&bridge->channels, bc, entry) {
				struct ast_format *fmt = ast_channel_rawwriteformat(bc->chan);
				max_sample_rate = MAX(ast_format_get_sample_rate(fmt), max_sample_rate);
			}
			channel_format = ao2_bump(ast_format_cache_get_slin_by_rate(max_sample_rate));
		}
		ast_bridge_unlock(bridge);
	}

	if (!channel_format) {
		channel_format = ao2_bump(ast_format_slin);
	}

	play_channel = prepare_bridge_media_channel("Announcer", channel_format);
	ao2_cleanup(channel_format);

	if (!play_channel) {
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

	ast_bridge_queue_everyone_else(bridge, NULL, &prog);

	/* Give play_channel and control reference to the thread data */
	thread_data = ast_calloc(1, sizeof(*thread_data));
	if (!thread_data) {
		stasis_app_bridge_playback_channel_remove((char *)bridge->uniqueid, control);
		ast_ari_response_alloc_failed(response);
		return;
	}

	thread_data->bridge_channel = play_channel;
	thread_data->control = control;
	thread_data->forward = channel_forward;
	thread_data->bridge_id = ast_strdup(bridge->uniqueid);
	if (!thread_data->bridge_id) {
		stasis_app_bridge_playback_channel_remove((char *)bridge->uniqueid, control);
		ast_ari_response_alloc_failed(response);
		ast_free(thread_data);
		return;
	}

	if (ast_pthread_create_detached(&threadid, NULL, bridge_channel_control_thread, thread_data)) {
		stasis_app_bridge_playback_channel_remove((char *)bridge->uniqueid, control);
		ast_free(thread_data->bridge_id);
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
 * \param args_skipms
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
	const char *args_format,
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
				args_offset_ms, args_skipms, args_playback_id, response, bridge,
				play_channel) == PLAY_FOUND_CHANNEL_UNAVAILABLE) {
			continue;
		}
		return;
	}

	ari_bridges_play_new(args_media, args_media_count, args_format, args_lang, args_offset_ms,
		args_skipms, args_playback_id, response, bridge);
}


void ast_ari_bridges_play(struct ast_variable *headers,
	struct ast_ari_bridges_play_args *args,
	struct ast_ari_response *response)
{
	ari_bridges_handle_play(args->bridge_id,
	args->media,
	args->media_count,
	args->announcer_format,
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
	args->announcer_format,
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
	struct ast_format *file_format = NULL;
	struct ast_format *channel_format = NULL;

	ast_assert(response != NULL);

	if (bridge == NULL) {
		return;
	}

	file_format = ast_get_format_for_file_ext(args->format);
	if (!file_format) {
		ast_ari_response_error(
			response, 422, "Unprocessable Entity",
			"specified format is unknown on this system");
		return;
	}

	if (!ast_strlen_zero(args->recorder_format)) {
		channel_format = ast_format_cache_get(args->recorder_format);
		if (!channel_format) {
			ast_ari_response_error(
				response, 422, "Unprocessable Entity",
				"specified recorder_format is unknown on this system");
			return;
		}
	} else {
		channel_format = ao2_bump(file_format);
	}

	if (!(record_channel = prepare_bridge_media_channel("Recorder", channel_format))) {
		ast_ari_response_error(
			response, 500, "Internal Server Error", "Failed to create recording channel");
		return;
	}

	ao2_cleanup(channel_format);

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

static int json_to_ast_variables(struct ast_ari_response *response, struct ast_json *json_variables,
	struct ast_variable **variables, struct ast_variable **report_event_variables)
{
	struct ast_json_iter *it_json_var;
	struct ast_variable *var_tail = NULL;
	struct ast_variable *report_var_tail = NULL;

	*variables = NULL;
	*report_event_variables = NULL;

	for (it_json_var = ast_json_object_iter(json_variables); it_json_var;
		it_json_var = ast_json_object_iter_next(json_variables, it_json_var)) {
		struct ast_variable *new_var;
		const char *key = ast_json_object_iter_key(it_json_var);
		const char *value = NULL;
		struct ast_json *json_value = ast_json_object_iter_value(it_json_var);
		int report_events = 0;

		if (ast_strlen_zero(key)) {
			continue;
		}

		if (ast_json_typeof(json_value) == AST_JSON_STRING) {
			value = ast_json_string_get(json_value);
		} else if (ast_json_typeof(json_value) == AST_JSON_OBJECT) {
			struct ast_json *value_field = ast_json_object_get(json_value, "value");
			struct ast_json *report_field = ast_json_object_get(json_value, "report_events");
			ast_log(LOG_DEBUG, "Processing variable '%s' with report_events: %s\n", key,
				report_field ? (ast_json_is_true(report_field) ? "true" : "false") : "not set");

			if (!value_field || ast_json_typeof(value_field) != AST_JSON_STRING) {
				ast_ari_response_error(response, 400, "Bad Request",
					"Each object value in 'variables' must include string field 'value'");
				ast_log(LOG_WARNING, "Missing or invalid 'value' field for variable '%s'\n", key);
				if (!value_field) {
					ast_log(LOG_WARNING, "Missing 'value' field for variable '%s'\n", key);
				} else if (ast_json_typeof(value_field) != AST_JSON_STRING) {
					ast_log(LOG_WARNING, "Invalid 'value' field for variable '%s' (bad type)\n", key);
				}
				goto error;
			}

			value = ast_json_string_get(value_field);

			if (report_field) {
				enum ast_json_type report_type = ast_json_typeof(report_field);

				if (report_type != AST_JSON_TRUE && report_type != AST_JSON_FALSE) {
					ast_ari_response_error(response, 400, "Bad Request",
						"Field 'report_events' in 'variables' entries must be boolean");
					ast_log(LOG_WARNING, "Invalid 'report_events' field for variable '%s' (bad type)\n", key);
					goto error;
				}

				report_events = ast_json_is_true(report_field);
			}
		} else {
			ast_ari_response_error(response, 400, "Bad Request",
				"Each value in 'variables' must be a string or an object with 'value' and optional 'report_events'");
			ast_log(LOG_WARNING, "Invalid value for variable '%s'\n", key);
			goto error;
		}

		if (!value) {
			continue;
		}

		new_var = ast_variable_new(key, value, "");
		if (!new_var) {
			ast_ari_response_alloc_failed(response);
			goto error;
		}

		var_tail = ast_variable_list_append_hint(variables, var_tail, new_var);

		if (report_events && report_event_variables) {
			struct ast_variable *report_var = ast_variable_new(key, "1", "");

			if (!report_var) {
				ast_ari_response_alloc_failed(response);
				goto error;
			}

			report_var_tail = ast_variable_list_append_hint(report_event_variables,
				report_var_tail, report_var);
		}
	}

	return 0;

error:
	ast_variables_destroy(*variables);
	*variables = NULL;
	if (report_event_variables) {
		ast_variables_destroy(*report_event_variables);
		*report_event_variables = NULL;
	}
	return -1;
}

void ast_ari_bridges_create(struct ast_variable *headers,
	struct ast_ari_bridges_create_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);
	struct ast_variable *variables = NULL;
	struct ast_variable *report_event_variables = NULL;

	if (ast_bridge_topic_exists(args->bridge_id)) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Bridge with id '%s' already exists", args->bridge_id);
		return;
	}

	ast_ari_bridges_create_parse_body(args->variables, args);

	bridge = stasis_app_bridge_create(args->type, args->name, args->bridge_id);
	if (!bridge) {
		ast_ari_response_error(
			response, 500, "Internal Error",
			"Unable to create bridge. Possible duplicate bridge id '%s'", args->bridge_id);
		return;
	}

	if (args->variables && json_to_ast_variables(response, args->variables,
			&variables, &report_event_variables)) {
		return;
	}

	ast_bridge_lock(bridge);
	if (variables) {
		struct ast_variable *var;

		for (var = variables; var; var = var->next) {
			int report_events = 0;
			struct ast_variable *report_var;
			char buf[strlen(var->name) + 1];
			char *variable;
			strcpy(buf, var->name);
			/* Strip whitespace from the variable name */
			variable = ast_strip(buf);

			for (report_var = report_event_variables; report_var;
				report_var = report_var->next) {
				if (!strcmp(report_var->name, var->name)) {
					report_events = 1;
					break;
				}
			}

			if (ast_bridge_set_variable(bridge, variable, var->value, report_events)) {
				ast_bridge_unlock(bridge);
				ast_variables_destroy(variables);
				ast_variables_destroy(report_event_variables);
				ast_ari_response_alloc_failed(response);
				return;
			}
		}
	}
	snapshot = ast_bridge_snapshot_create(bridge);
	ast_bridge_unlock(bridge);
	ast_variables_destroy(variables);
	ast_variables_destroy(report_event_variables);

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
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);
	struct ast_variable *variables = NULL;
	struct ast_variable *report_event_variables = NULL;

	if (ast_bridge_topic_exists(args->bridge_id)) {
		ast_ari_response_error(
			response, 409, "Conflict",
			"Bridge with id '%s' already exists", args->bridge_id);
		return;
	}

	ast_ari_bridges_create_with_id_parse_body(args->variables, args);

	bridge = stasis_app_bridge_create(args->type, args->name, args->bridge_id);
	if (!bridge) {
		ast_ari_response_error(
			response, 500, "Internal Error",
			"Unable to create bridge");
		return;
	}

	if (args->variables) {
		struct ast_json *json_variables;

		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables && json_to_ast_variables(response, json_variables,
			&variables, &report_event_variables)) {
			if (args->variables) {
				ast_log(LOG_WARNING, "Failed to parse variables for new bridge '%s'\n", args->bridge_id);
			} else {
				ast_log(LOG_WARNING, "Failed to find variables for new bridge '%s'\n", args->bridge_id);
			}
			return;
		}
	}

	ast_bridge_lock(bridge);
	if (variables) {
		struct ast_variable *var;

		for (var = variables; var; var = var->next) {
			int report_events = 0;
			struct ast_variable *report_var;
			char buf[strlen(var->name) + 1];
			char *variable;
			strcpy(buf, var->name);
			/* Strip whitespace from the variable name */
			variable = ast_strip(buf);

			report_events = 0;
			for (report_var = report_event_variables; report_var;
				report_var = report_var->next) {
				if (!strcmp(report_var->name, var->name)) {
					report_events = 1;
					break;
				}
			}

			if (ast_bridge_set_variable(bridge, variable, var->value, report_events)) {
				ast_bridge_unlock(bridge);
				ast_variables_destroy(variables);
				ast_variables_destroy(report_event_variables);
				ast_ari_response_alloc_failed(response);
				return;
			}
		}
	}
	snapshot = ast_bridge_snapshot_create(bridge);
	ast_bridge_unlock(bridge);
	ast_variables_destroy(variables);
	ast_variables_destroy(report_event_variables);

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

void ast_ari_bridges_get_bridge_var(struct ast_variable *headers,
	struct ast_ari_bridges_get_bridge_var_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_bridge *bridge;
	const char *value;

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	bridge = find_bridge(response, args->bridge_id);
	if (!bridge) {
		return;
	}

	ast_bridge_lock(bridge);
	value = ast_bridge_get_variable(bridge, args->variable);
	ast_bridge_unlock(bridge);

	if (!value) {
		ao2_ref(bridge, -1);
		ast_ari_response_error(response, 404, "Not Found",
			"Provided variable was not found");
		return;
	}

	json = ast_json_pack("{s: s}", "value", value);
	ao2_ref(bridge, -1);

	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_bridges_set_bridge_var(struct ast_variable *headers,
	struct ast_ari_bridges_set_bridge_var_args *args,
	struct ast_ari_response *response)
{
	struct ast_bridge *bridge;
	char buf[strlen(args->variable) + 1];
	char *variable;

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	bridge = find_bridge(response, args->bridge_id);
	if (!bridge) {
		return;
	}
	ao2_ref(bridge, -1);

	strcpy(buf, args->variable);
	/* Strip whitespace from the variable name */
	variable = ast_strip(buf);

	if (stasis_app_bridge_set_var_reportable(args->bridge_id, variable, args->value,
			args->report_events)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Failed to execute function");
		return;
	}

	ast_ari_response_no_content(response);
}

void ast_ari_bridges_get_bridge_vars(struct ast_variable *headers,
	struct ast_ari_bridges_get_bridge_vars_args *args,
	struct ast_ari_response *response)
{
	int res;
	RAII_VAR(struct ast_json *, json, ast_json_object_create(), ast_json_unref);
	RAII_VAR(struct ast_json *, inner_json, ast_json_object_create(), ast_json_unref);
	RAII_VAR(struct ast_str *, value, ast_str_create(32), ast_free);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	if (!json || !inner_json || !value) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	if (args->variables_count == 0) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"At least one variable name is required");
		return;
	}

	if (ast_strlen_zero(args->bridge_id)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Bridge ID is required");
		return;
	}

	bridge = stasis_app_bridge_find_by_id(args->bridge_id);
	if (!bridge) {
		ast_ari_response_error(
			response, 404, "Bridge Not Found",
			"Provided bridge was not found");
		return;
	}

	for (int i = 0; i < args->variables_count; i++) {
		struct ast_json *json_str;
		char buf[strlen(args->variables[i]) + 1];
		char *variable;
		const char *var_value;

		strcpy(buf, args->variables[i]);
		variable = ast_strip(buf);
		if (ast_strlen_zero(variable)) {
			ast_ari_response_error(
				response, 400, "Bad Request",
				"Variable names are required");
			return;
		}

		if (variable[strlen(variable) - 1] == ')') {
			if (ast_func_read2(NULL, variable, &value, 0)) {
				ast_ari_response_error(
					response, 500, "Error With Function",
					"Unable to read provided function");
				return;
			}
		} else {
			ast_bridge_lock(bridge);
			var_value = ast_bridge_get_variable(bridge, variable);
			ast_bridge_unlock(bridge);
			if (!var_value) {
				ast_ari_response_error(
					response, 404, "Variable Not Found",
					"Provided variable was not found");
				return;
			}
			ast_str_set(&value, 0, "%s", var_value);
		}

		json_str = ast_json_string_create(ast_str_buffer(value));
		if (!json_str) {
			ast_ari_response_alloc_failed(response);
			return;
		}

		res = ast_json_object_set(inner_json, variable, json_str);
		if (res) {
			ast_ari_response_alloc_failed(response);
			ast_json_unref(json_str);
			return;
		}
	}

	res = ast_json_object_set(json, "variables", ast_json_ref(inner_json));
	if (res) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_bridges_set_bridge_vars(struct ast_variable *headers,
	struct ast_ari_bridges_set_bridge_vars_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json_variables;
	struct ast_variable *var;
	RAII_VAR(struct ast_variable *, variables, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, report_event_variables, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	if (!args->variables) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"The 'variables' field is required");
		return;
	}

	bridge = stasis_app_bridge_find_by_id(args->bridge_id);
	if (!bridge) {
		ast_ari_response_error(
			response, 404, "Bridge Not Found",
			"Provided bridge was not found");
		return;
	}

	json_variables = ast_json_object_get(args->variables, "variables");
	if (!json_variables || ast_json_typeof(json_variables) != AST_JSON_OBJECT) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"The 'variables' field must be a JSON object");
		return;
	}

	if (json_to_ast_variables(response, json_variables, &variables,
		&report_event_variables)) {
		return;
	}

	for (var = variables; var; var = var->next) {
		int report_events = 0;
		struct ast_variable *report_var;
		char buf[strlen(var->name) + 1];
		char *variable;
		strcpy(buf, var->name);
		/* Strip whitespace from the variable name */
		variable = ast_strip(buf);

		/* See if the variable is in the report event list */
		for (report_var = report_event_variables; report_var;
			report_var = report_var->next) {
			if (!strcmp(report_var->name, var->name)) {
				report_events = 1;
				break;
			}
		}

		if (stasis_app_bridge_set_var_reportable(args->bridge_id, variable, var->value,
			report_events)) {
			ast_ari_response_error(
				response, 400, "Bad Request",
				"Failed to execute function");
			return;
		}
	}

	ast_ari_response_no_content(response);
}
