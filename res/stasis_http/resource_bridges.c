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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "resource_bridges.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_bridging.h"
#include "asterisk/stasis_app.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"

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
	struct stasis_http_response *response,
	const char *bridge_id)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	bridge = stasis_app_bridge_find_by_id(bridge_id);
	if (bridge == NULL) {
		RAII_VAR(struct ast_bridge_snapshot *, snapshot,
			ast_bridge_snapshot_get_latest(bridge_id), ao2_cleanup);
		if (!snapshot) {
			stasis_http_response_error(response, 404, "Not found",
				"Bridge not found");
			return NULL;
		}

		stasis_http_response_error(response, 409, "Conflict",
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
	struct stasis_http_response *response,
	const char *channel_id)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	ast_assert(response != NULL);

	control = stasis_app_control_find_by_channel_id(channel_id);
	if (control == NULL) {
		stasis_http_response_error(response, 422, "Unprocessable Entity",
			"Channel not in Stasis application");
		return NULL;
	}

	ao2_ref(control, +1);
	return control;
}

void stasis_http_add_channel_to_bridge(struct ast_variable *headers, struct ast_add_channel_to_bridge_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	if (!bridge) {
		return;
	}

	control = find_channel_control(response, args->channel);
	if (!control) {
		return;
	}

	stasis_app_control_add_channel_to_bridge(control, bridge);
	stasis_http_response_no_content(response);
}

void stasis_http_remove_channel_from_bridge(struct ast_variable *headers, struct ast_remove_channel_from_bridge_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	if (!bridge) {
		return;
	}

	control = find_channel_control(response, args->channel);
	if (!control) {
		return;
	}

	/* BUGBUG this should make sure the bridge requested for removal is actually
	 * the bridge the channel is in. This will be possible once the bridge uniqueid
	 * is added to the channel snapshot. A 409 response should be issued if the bridge
	 * uniqueids don't match */
	if (stasis_app_control_remove_channel_from_bridge(control, bridge)) {
		stasis_http_response_error(response, 500, "Internal Error",
			"Could not remove channel from bridge");
		return;
	}

	stasis_http_response_no_content(response);
}

void stasis_http_record_bridge(struct ast_variable *headers, struct ast_record_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_record_bridge\n");
}

void stasis_http_get_bridge(struct ast_variable *headers, struct ast_get_bridge_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, ast_bridge_snapshot_get_latest(args->bridge_id), ao2_cleanup);
	if (!snapshot) {
		stasis_http_response_error(
			response, 404, "Not Found",
			"Bridge not found");
		return;
	}

	stasis_http_response_ok(response,
		ast_bridge_snapshot_to_json(snapshot));
}

void stasis_http_delete_bridge(struct ast_variable *headers, struct ast_delete_bridge_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, find_bridge(response, args->bridge_id), ao2_cleanup);
	if (!bridge) {
		return;
	}

	stasis_app_bridge_destroy(args->bridge_id);
	stasis_http_response_no_content(response);
}

void stasis_http_get_bridges(struct ast_variable *headers, struct ast_get_bridges_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, snapshots, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;

	caching_topic = ast_bridge_topic_all_cached();
	if (!caching_topic) {
		stasis_http_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}
	ao2_ref(caching_topic, +1);

	snapshots = stasis_cache_dump(caching_topic, ast_bridge_snapshot_type());
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
		struct ast_bridge_snapshot *snapshot = stasis_message_data(msg);
		if (ast_json_array_append(json, ast_bridge_snapshot_to_json(snapshot))) {
			stasis_http_response_alloc_failed(response);
			return;
		}
	}
	ao2_iterator_destroy(&i);

	stasis_http_response_ok(response, ast_json_ref(json));
}

void stasis_http_new_bridge(struct ast_variable *headers, struct ast_new_bridge_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct ast_bridge *, bridge, stasis_app_bridge_create(args->type), ao2_cleanup);
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);

	if (!bridge) {
		stasis_http_response_error(
			response, 500, "Internal Error",
			"Unable to create bridge");
		return;
	}

	snapshot = ast_bridge_snapshot_create(bridge);
	if (!snapshot) {
		stasis_http_response_error(
			response, 500, "Internal Error",
			"Unable to create snapshot for new bridge");
		return;
	}

	stasis_http_response_ok(response,
		ast_bridge_snapshot_to_json(snapshot));
}
