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
 * \brief Asterisk endpoint API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/endpoints.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stringfields.h"

/*! Buckets for endpoint->channel mappings. Keep it prime! */
#define ENDPOINT_BUCKETS 127

struct ast_endpoint {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(tech);	/*!< Technology (SIP, IAX2, etc.). */
		AST_STRING_FIELD(resource);	/*!< Name, unique to the tech. */
		AST_STRING_FIELD(id);	/*!< tech/resource id */
		);
	/*! Endpoint's current state */
	enum ast_endpoint_state state;
	/*!
	 * \brief Max channels for this endpoint. -1 means unlimited or unknown.
	 *
	 * Note that this simply documents the limits of an endpoint, and does
	 * nothing to try to enforce the limit.
	 */
	int max_channels;
	/*! Topic for this endpoint's messages */
	struct stasis_topic *topic;
	/*!
	 * Forwarding subscription sending messages to ast_endpoint_topic_all()
	 */
	struct stasis_subscription *forward;
	/*! Router for handling this endpoint's messages */
	struct stasis_message_router *router;
	/*! ast_str_container of channels associated with this endpoint */
	struct ao2_container *channel_ids;
};

const char *ast_endpoint_state_to_string(enum ast_endpoint_state state)
{
	switch (state) {
	case AST_ENDPOINT_UNKNOWN:
		return "unknown";
	case AST_ENDPOINT_OFFLINE:
		return "offline";
	case AST_ENDPOINT_ONLINE:
		return "online";
	}
	return "?";
}

static void endpoint_publish_snapshot(struct ast_endpoint *endpoint)
{
	RAII_VAR(struct ast_endpoint_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	ast_assert(endpoint != NULL);
	ast_assert(endpoint->topic != NULL);

	snapshot = ast_endpoint_snapshot_create(endpoint);
	if (!snapshot) {
		return;
	}
	message = stasis_message_create(ast_endpoint_snapshot_type(), snapshot);
	if (!message) {
		return;
	}
	stasis_publish(endpoint->topic, message);
}

static void endpoint_dtor(void *obj)
{
	struct ast_endpoint *endpoint = obj;

	/* The router should be shut down already */
	ast_assert(endpoint->router == NULL);

	stasis_unsubscribe(endpoint->forward);
	endpoint->forward = NULL;

	ao2_cleanup(endpoint->topic);
	endpoint->topic = NULL;

	ast_string_field_free_memory(endpoint);
}

static void endpoint_channel_snapshot(void *data,
	struct stasis_subscription *sub, struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_endpoint *endpoint = data;
	struct ast_channel_snapshot *snapshot = stasis_message_data(message);
	RAII_VAR(char *, existing_id, NULL, ao2_cleanup);
	int publish = 0;

	ast_assert(endpoint != NULL);
	ast_assert(snapshot != NULL);

	ao2_lock(endpoint);
	existing_id = ao2_find(endpoint->channel_ids, snapshot->uniqueid,
		OBJ_POINTER);
	if (!existing_id) {
		ast_str_container_add(endpoint->channel_ids,
			snapshot->uniqueid);
		publish = 1;
	}
	ao2_unlock(endpoint);
	if (publish) {
		endpoint_publish_snapshot(endpoint);
	}
}

static void endpoint_cache_clear(void *data,
	struct stasis_subscription *sub, struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_endpoint *endpoint = data;
	struct stasis_cache_clear *clear = stasis_message_data(message);

	ast_assert(endpoint != NULL);
	ast_assert(clear != NULL);

	ao2_lock(endpoint);
	ao2_find(endpoint->channel_ids, clear->id, OBJ_POINTER | OBJ_NODATA | OBJ_UNLINK);
	ao2_unlock(endpoint);
	endpoint_publish_snapshot(endpoint);
}

static void endpoint_default(void *data,
	struct stasis_subscription *sub, struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct stasis_endpoint *endpoint = data;

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(endpoint);
	}
}

struct ast_endpoint *ast_endpoint_create(const char *tech, const char *resource)
{
	RAII_VAR(struct ast_endpoint *, endpoint, NULL, ao2_cleanup);
	int r = 0;

	if (ast_strlen_zero(tech)) {
		ast_log(LOG_ERROR, "Endpoint tech cannot be empty\n");
		return NULL;
	}

	if (ast_strlen_zero(resource)) {
		ast_log(LOG_ERROR, "Endpoint resource cannot be empty\n");
		return NULL;
	}

	endpoint = ao2_alloc(sizeof(*endpoint), endpoint_dtor);
	if (!endpoint) {
		return NULL;
	}

	endpoint->max_channels = -1;
	endpoint->state = AST_ENDPOINT_UNKNOWN;

	if (ast_string_field_init(endpoint, 80) != 0) {
		return NULL;
	}

	ast_string_field_set(endpoint, tech, tech);
	ast_string_field_set(endpoint, resource, resource);
	ast_string_field_build(endpoint, id, "%s/%s", tech, resource);

	/* All access to channel_ids should be covered by the endpoint's
	 * lock; no extra lock needed. */
	endpoint->channel_ids = ast_str_container_alloc_options(
		AO2_ALLOC_OPT_LOCK_NOLOCK, ENDPOINT_BUCKETS);
	if (!endpoint->channel_ids) {
		return NULL;
	}

	endpoint->topic = stasis_topic_create(endpoint->id);
	if (!endpoint->topic) {
		return NULL;
	}

	endpoint->forward =
		stasis_forward_all(endpoint->topic, ast_endpoint_topic_all());
	if (!endpoint->forward) {
		return NULL;
	}

	endpoint->router = stasis_message_router_create(endpoint->topic);
	if (!endpoint->router) {
		return NULL;
	}
	r |= stasis_message_router_add(endpoint->router,
		ast_channel_snapshot_type(), endpoint_channel_snapshot,
		endpoint);
	r |= stasis_message_router_add(endpoint->router,
		stasis_cache_clear_type(), endpoint_cache_clear,
		endpoint);
	r |= stasis_message_router_set_default(endpoint->router,
		endpoint_default, endpoint);

	endpoint_publish_snapshot(endpoint);

	ao2_ref(endpoint, +1);
	return endpoint;
}

const char *ast_endpoint_get_tech(const struct ast_endpoint *endpoint)
{
	ast_assert(endpoint != NULL);
	return endpoint->tech;
}

void ast_endpoint_shutdown(struct ast_endpoint *endpoint)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	if (endpoint == NULL) {
		return;
	}

	message = stasis_cache_clear_create(ast_endpoint_snapshot_type(), endpoint->id);
	if (message) {
		stasis_publish(endpoint->topic, message);
	}

	stasis_message_router_unsubscribe(endpoint->router);
	endpoint->router = NULL;
}

const char *ast_endpoint_get_resource(const struct ast_endpoint *endpoint)
{
	return endpoint->resource;
}

struct stasis_topic *ast_endpoint_topic(struct ast_endpoint *endpoint)
{
	return endpoint ? endpoint->topic : ast_endpoint_topic_all();
}

void ast_endpoint_set_state(struct ast_endpoint *endpoint,
	enum ast_endpoint_state state)
{
	ast_assert(endpoint != NULL);
	ao2_lock(endpoint);
	endpoint->state = state;
	ao2_unlock(endpoint);
	endpoint_publish_snapshot(endpoint);
}

void ast_endpoint_set_max_channels(struct ast_endpoint *endpoint,
	int max_channels)
{
	ast_assert(endpoint != NULL);
	ao2_lock(endpoint);
	endpoint->max_channels = max_channels;
	ao2_unlock(endpoint);
	endpoint_publish_snapshot(endpoint);
}

static void endpoint_snapshot_dtor(void *obj)
{
	struct ast_endpoint_snapshot *snapshot = obj;

	ast_assert(snapshot != NULL);
	ast_string_field_free_memory(snapshot);
}

struct ast_endpoint_snapshot *ast_endpoint_snapshot_create(
	struct ast_endpoint *endpoint)
{
	RAII_VAR(struct ast_endpoint_snapshot *, snapshot, NULL, ao2_cleanup);
	int channel_count;
	struct ao2_iterator i;
	void *obj;
	SCOPED_AO2LOCK(lock, endpoint);

	channel_count = ao2_container_count(endpoint->channel_ids);

	snapshot = ao2_alloc(
		sizeof(*snapshot) + channel_count * sizeof(char *),
		endpoint_snapshot_dtor);

	if (ast_string_field_init(snapshot, 80) != 0) {
		return NULL;
	}

	ast_string_field_build(snapshot, id, "%s/%s", endpoint->tech,
		endpoint->resource);
	ast_string_field_set(snapshot, tech, endpoint->tech);
	ast_string_field_set(snapshot, resource, endpoint->resource);

	snapshot->state = endpoint->state;
	snapshot->max_channels = endpoint->max_channels;

	i = ao2_iterator_init(endpoint->channel_ids, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(char *, channel_id, obj, ao2_cleanup);
		snapshot->channel_ids[snapshot->num_channels++] = channel_id;
	}

	ao2_ref(snapshot, +1);
	return snapshot;
}
