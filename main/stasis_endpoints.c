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
 * \brief Stasis endpoint API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_endpoints.h"

STASIS_MESSAGE_TYPE_DEFN(ast_endpoint_snapshot_type);

static struct stasis_topic *endpoint_topic_all;

static struct stasis_caching_topic *endpoint_topic_all_cached;

struct stasis_topic *ast_endpoint_topic_all(void)
{
	return endpoint_topic_all;
}

struct stasis_caching_topic *ast_endpoint_topic_all_cached(void)
{
	return endpoint_topic_all_cached;
}

struct ast_endpoint_snapshot *ast_endpoint_latest_snapshot(const char *tech,
	const char *name)
{
	RAII_VAR(char *, id, NULL, ast_free);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_endpoint_snapshot *snapshot;

	ast_asprintf(&id, "%s/%s", tech, name);
	if (!id) {
		return NULL;
	}

	msg = stasis_cache_get(ast_endpoint_topic_all_cached(),
		ast_endpoint_snapshot_type(), id);
	if (!msg) {
		return NULL;
	}

	snapshot = stasis_message_data(msg);
	ast_assert(snapshot != NULL);

	ao2_ref(snapshot, +1);
	return snapshot;
}

/*!
 * \brief Callback extract a unique identity from a snapshot message.
 *
 * This identity is unique to the underlying object of the snapshot, such as the
 * UniqueId field of a channel.
 *
 * \param message Message to extract id from.
 * \return String representing the snapshot's id.
 * \return \c NULL if the message_type of the message isn't a handled snapshot.
 * \since 12
 */
static const char *endpoint_snapshot_get_id(struct stasis_message *message)
{
	struct ast_endpoint_snapshot *snapshot;

	if (ast_endpoint_snapshot_type() != stasis_message_type(message)) {
		return NULL;
	}

	snapshot = stasis_message_data(message);

	return snapshot->id;
}


static void endpoints_stasis_shutdown(void)
{
	ao2_cleanup(endpoint_topic_all);
	endpoint_topic_all = NULL;

	stasis_caching_unsubscribe(endpoint_topic_all_cached);
	endpoint_topic_all_cached = NULL;
}

struct ast_json *ast_endpoint_snapshot_to_json(
	const struct ast_endpoint_snapshot *snapshot)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_json *channel_array;
	int i;

	json = ast_json_pack("{s: s, s: s, s: s, s: []}",
		"technology", snapshot->tech,
		"resource", snapshot->resource,
		"state", ast_endpoint_state_to_string(snapshot->state),
		"channels");

	if (json == NULL) {
		return NULL;
	}

	if (snapshot->max_channels != -1) {
		int res = ast_json_object_set(json, "max_channels",
			ast_json_integer_create(snapshot->max_channels));
		if (res != 0) {
			return NULL;
		}
	}

	channel_array = ast_json_object_get(json, "channels");
	ast_assert(channel_array != NULL);
	for (i = 0; i < snapshot->num_channels; ++i) {
		int res = ast_json_array_append(channel_array,
			ast_json_stringf("channel:%s",
				snapshot->channel_ids[i]));
		if (res != 0) {
			return NULL;
		}
	}

	return ast_json_ref(json);
}

int ast_endpoint_stasis_init(void)
{
	ast_register_atexit(endpoints_stasis_shutdown);

	if (!endpoint_topic_all) {
		endpoint_topic_all = stasis_topic_create("endpoint_topic_all");
	}

	if (!endpoint_topic_all) {
		return -1;
	}

	if (!endpoint_topic_all_cached) {
		endpoint_topic_all_cached =
			stasis_caching_topic_create(
				endpoint_topic_all, endpoint_snapshot_get_id);
	}

	if (!endpoint_topic_all_cached) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_endpoint_snapshot_type) != 0) {
		return -1;
	}

	return 0;
}
