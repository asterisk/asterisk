/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief Stasis Messages and Data Types for Bridge Objects
 *
 * \author Kinsey Moore <kmoore@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/channel.h"
#include "asterisk/stasis_bridging.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_technology.h"

#define SNAPSHOT_CHANNELS_BUCKETS 13

/*!
 * @{ \brief Define bridge message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_bridge_snapshot_type);
STASIS_MESSAGE_TYPE_DEFN(ast_bridge_merge_message_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_entered_bridge_type);
STASIS_MESSAGE_TYPE_DEFN(ast_channel_left_bridge_type);
/*! @} */

/*! \brief Aggregate topic for bridge messages */
static struct stasis_topic *bridge_topic_all;

/*! \brief Caching aggregate topic for bridge snapshots */
static struct stasis_caching_topic *bridge_topic_all_cached;

/*! \brief Topic pool for individual bridge topics */
static struct stasis_topic_pool *bridge_topic_pool;

/*! \brief Destructor for bridge snapshots */
static void bridge_snapshot_dtor(void *obj)
{
	struct ast_bridge_snapshot *snapshot = obj;
	ast_string_field_free_memory(snapshot);
	ao2_cleanup(snapshot->channels);
	snapshot->channels = NULL;
}

struct ast_bridge_snapshot *ast_bridge_snapshot_create(struct ast_bridge *bridge)
{
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);
	struct ast_bridge_channel *bridge_channel;

	snapshot = ao2_alloc(sizeof(*snapshot), bridge_snapshot_dtor);
	if (!snapshot || ast_string_field_init(snapshot, 128)) {
		return NULL;
	}

	snapshot->channels = ast_str_container_alloc(SNAPSHOT_CHANNELS_BUCKETS);
	if (!snapshot->channels) {
		return NULL;
	}

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (ast_str_container_add(snapshot->channels,
				ast_channel_uniqueid(bridge_channel->chan))) {
			return NULL;
		}
	}

	ast_string_field_set(snapshot, uniqueid, bridge->uniqueid);
	ast_string_field_set(snapshot, technology, bridge->technology->name);

	snapshot->feature_flags = bridge->feature_flags;
	snapshot->num_channels = bridge->num_channels;
	snapshot->num_active = bridge->num_active;

	ao2_ref(snapshot, +1);
	return snapshot;
}

struct stasis_topic *ast_bridge_topic(struct ast_bridge *bridge)
{
	struct stasis_topic *bridge_topic = stasis_topic_pool_get_topic(bridge_topic_pool, bridge->uniqueid);
	if (!bridge_topic) {
		return ast_bridge_topic_all();
	}
	return bridge_topic;
}

struct stasis_topic *ast_bridge_topic_all(void)
{
	return bridge_topic_all;
}

struct stasis_caching_topic *ast_bridge_topic_all_cached(void)
{
	return bridge_topic_all_cached;
}

void ast_bridge_publish_state(struct ast_bridge *bridge)
{
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	ast_assert(bridge != NULL);

	snapshot = ast_bridge_snapshot_create(bridge);
	if (!snapshot) {
		return;
	}

	msg = stasis_message_create(ast_bridge_snapshot_type(), snapshot);
	if (!msg) {
		return;
	}

	stasis_publish(ast_bridge_topic(bridge), msg);
}

static void bridge_publish_state_from_blob(struct ast_bridge_blob *obj)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	ast_assert(obj != NULL);

	msg = stasis_message_create(ast_bridge_snapshot_type(), obj->bridge);
	if (!msg) {
		return;
	}

	stasis_publish(stasis_topic_pool_get_topic(bridge_topic_pool, obj->bridge->uniqueid), msg);
}

/*! \brief Destructor for bridge merge messages */
static void bridge_merge_message_dtor(void *obj)
{
	struct ast_bridge_merge_message *msg = obj;

	ao2_cleanup(msg->to);
	msg->to = NULL;
	ao2_cleanup(msg->from);
	msg->from = NULL;
}

/*! \brief Bridge merge message creation helper */
static struct ast_bridge_merge_message *bridge_merge_message_create(struct ast_bridge *to, struct ast_bridge *from)
{
	RAII_VAR(struct ast_bridge_merge_message *, msg, NULL, ao2_cleanup);

	msg = ao2_alloc(sizeof(*msg), bridge_merge_message_dtor);
	if (!msg) {
		return NULL;
	}

	msg->to = ast_bridge_snapshot_create(to);
	if (!msg->to) {
		return NULL;
	}

	msg->from = ast_bridge_snapshot_create(from);
	if (!msg->from) {
		return NULL;
	}

	ao2_ref(msg, +1);
	return msg;
}

void ast_bridge_publish_merge(struct ast_bridge *to, struct ast_bridge *from)
{
	RAII_VAR(struct ast_bridge_merge_message *, merge_msg, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	ast_assert(to != NULL);
	ast_assert(from != NULL);

	merge_msg = bridge_merge_message_create(to, from);
	if (!merge_msg) {
		return;
	}

	msg = stasis_message_create(ast_bridge_merge_message_type(), merge_msg);
	if (!msg) {
		return;
	}

	stasis_publish(ast_bridge_topic_all(), msg);
}

static void bridge_blob_dtor(void *obj)
{
	struct ast_bridge_blob *event = obj;
	ao2_cleanup(event->bridge);
	event->bridge = NULL;
	ao2_cleanup(event->channel);
	event->channel = NULL;
	ast_json_unref(event->blob);
	event->blob = NULL;
}

struct stasis_message *ast_bridge_blob_create(
	struct stasis_message_type *message_type,
	struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_json *blob)
{
	RAII_VAR(struct ast_bridge_blob *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	obj = ao2_alloc(sizeof(*obj), bridge_blob_dtor);
	if (!obj) {
		return NULL;
	}

	if (bridge) {
		obj->bridge = ast_bridge_snapshot_create(bridge);
		if (obj->bridge == NULL) {
			return NULL;
		}
	}

	if (chan) {
		obj->channel = ast_channel_snapshot_create(chan);
		if (obj->channel == NULL) {
			return NULL;
		}
	}

	if (blob) {
		obj->blob = ast_json_ref(blob);
	}

	msg = stasis_message_create(message_type, obj);
	if (!msg) {
		return NULL;
	}

	ao2_ref(msg, +1);
	return msg;
}

const char *ast_bridge_blob_json_type(struct ast_bridge_blob *obj)
{
	if (obj == NULL) {
		return NULL;
	}

	return ast_json_string_get(ast_json_object_get(obj->blob, "type"));
}

void ast_bridge_publish_enter(struct ast_bridge *bridge, struct ast_channel *chan)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	msg = ast_bridge_blob_create(ast_channel_entered_bridge_type(), bridge, chan, NULL);
	if (!msg) {
		return;
	}

	/* enter blob first, then state */
	stasis_publish(ast_bridge_topic(bridge), msg);
	bridge_publish_state_from_blob(stasis_message_data(msg));
}

void ast_bridge_publish_leave(struct ast_bridge *bridge, struct ast_channel *chan)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	msg = ast_bridge_blob_create(ast_channel_left_bridge_type(), bridge, chan, NULL);
	if (!msg) {
		return;
	}

	/* state first, then leave blob (opposite of enter, preserves nesting of events) */
	bridge_publish_state_from_blob(stasis_message_data(msg));
	stasis_publish(ast_bridge_topic(bridge), msg);
}

struct ast_json *ast_bridge_snapshot_to_json(const struct ast_bridge_snapshot *snapshot)
{
	RAII_VAR(struct ast_json *, json_chan, NULL, ast_json_unref);
	int r = 0;

	if (snapshot == NULL) {
		return NULL;
	}

	json_chan = ast_json_object_create();
	if (!json_chan) { ast_log(LOG_ERROR, "Error creating channel json object\n"); return NULL; }

	r = ast_json_object_set(json_chan, "bridge-uniqueid", ast_json_string_create(snapshot->uniqueid));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }
	r = ast_json_object_set(json_chan, "bridge-technology", ast_json_string_create(snapshot->technology));
	if (r) { ast_log(LOG_ERROR, "Error adding attrib to channel json object\n"); return NULL; }

	return ast_json_ref(json_chan);
}

void ast_stasis_bridging_shutdown(void)
{
	ao2_cleanup(bridge_topic_all);
	bridge_topic_all = NULL;
	bridge_topic_all_cached = stasis_caching_unsubscribe_and_join(
		bridge_topic_all_cached);
	ao2_cleanup(bridge_topic_pool);
	bridge_topic_pool = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(ast_bridge_snapshot_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_bridge_merge_message_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_entered_bridge_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_channel_left_bridge_type);
}

/*! \brief snapshot ID getter for caching topic */
static const char *bridge_snapshot_get_id(struct stasis_message *msg)
{
	struct ast_bridge_snapshot *snapshot;
	if (stasis_message_type(msg) != ast_bridge_snapshot_type()) {
		return NULL;
	}
	snapshot = stasis_message_data(msg);
	return snapshot->uniqueid;
}

int ast_stasis_bridging_init(void)
{
	STASIS_MESSAGE_TYPE_INIT(ast_bridge_snapshot_type);
	STASIS_MESSAGE_TYPE_INIT(ast_bridge_merge_message_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_entered_bridge_type);
	STASIS_MESSAGE_TYPE_INIT(ast_channel_left_bridge_type);
	bridge_topic_all = stasis_topic_create("ast_bridge_topic_all");
	bridge_topic_all_cached = stasis_caching_topic_create(bridge_topic_all, bridge_snapshot_get_id);
	bridge_topic_pool = stasis_topic_pool_create(bridge_topic_all);
	return !bridge_topic_all
		|| !bridge_topic_all_cached
		|| !bridge_topic_pool ? -1 : 0;
}
