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

ASTERISK_REGISTER_FILE()

#include "asterisk/astobj2.h"
#include "asterisk/endpoints.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stringfields.h"
#include "asterisk/_private.h"

/*! Buckets for endpoint->channel mappings. Keep it prime! */
#define ENDPOINT_CHANNEL_BUCKETS 127

/*! Buckets for endpoint hash. Keep it prime! */
#define ENDPOINT_BUCKETS 127

/*! Buckets for technology endpoints. */
#define TECH_ENDPOINT_BUCKETS 11

static struct ao2_container *endpoints;

static struct ao2_container *tech_endpoints;

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
	struct stasis_cp_single *topics;
	/*! Router for handling this endpoint's messages */
	struct stasis_message_router *router;
	/*! ast_str_container of channels associated with this endpoint */
	struct ao2_container *channel_ids;
};

static int endpoint_hash(const void *obj, int flags)
{
	const struct ast_endpoint *endpoint;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_SEARCH_OBJECT:
		endpoint = obj;
		return ast_str_hash(endpoint->id);
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int endpoint_cmp(void *obj, void *arg, int flags)
{
	const struct ast_endpoint *left = obj;
	const struct ast_endpoint *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right->id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left->id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left->id, right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}

	return CMP_MATCH;
}

struct ast_endpoint *ast_endpoint_find_by_id(const char *id)
{
	struct ast_endpoint *endpoint = ao2_find(endpoints, id, OBJ_KEY);

	if (!endpoint) {
		endpoint = ao2_find(tech_endpoints, id, OBJ_KEY);
	}

	return endpoint;
}

struct stasis_topic *ast_endpoint_topic(struct ast_endpoint *endpoint)
{
	if (!endpoint) {
		return ast_endpoint_topic_all();
	}
	return stasis_cp_single_topic(endpoint->topics);
}

struct stasis_topic *ast_endpoint_topic_cached(struct ast_endpoint *endpoint)
{
	if (!endpoint) {
		return ast_endpoint_topic_all_cached();
	}
	return stasis_cp_single_topic_cached(endpoint->topics);
}

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
	ast_assert(endpoint->topics != NULL);

	if (!ast_endpoint_snapshot_type()) {
		return;
	}

	snapshot = ast_endpoint_snapshot_create(endpoint);
	if (!snapshot) {
		return;
	}
	message = stasis_message_create(ast_endpoint_snapshot_type(), snapshot);
	if (!message) {
		return;
	}
	stasis_publish(ast_endpoint_topic(endpoint), message);
}

static void endpoint_dtor(void *obj)
{
	struct ast_endpoint *endpoint = obj;

	/* The router should be shut down already */
	ast_assert(stasis_message_router_is_done(endpoint->router));
	ao2_cleanup(endpoint->router);
	endpoint->router = NULL;

	stasis_cp_single_unsubscribe(endpoint->topics);
	endpoint->topics = NULL;

	ao2_cleanup(endpoint->channel_ids);
	endpoint->channel_ids = NULL;

	ast_string_field_free_memory(endpoint);
}


int ast_endpoint_add_channel(struct ast_endpoint *endpoint,
	struct ast_channel *chan)
{
	ast_assert(chan != NULL);
	ast_assert(endpoint != NULL);
	ast_assert(!ast_strlen_zero(endpoint->resource));

	ast_channel_forward_endpoint(chan, endpoint);

	ao2_lock(endpoint);
	ast_str_container_add(endpoint->channel_ids, ast_channel_uniqueid(chan));
	ao2_unlock(endpoint);

	endpoint_publish_snapshot(endpoint);

	return 0;
}

/*! \brief Handler for channel snapshot cache clears */
static void endpoint_cache_clear(void *data,
	struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_endpoint *endpoint = data;
	struct stasis_message *clear_msg = stasis_message_data(message);
	struct ast_channel_snapshot *clear_snapshot;

	if (stasis_message_type(clear_msg) != ast_channel_snapshot_type()) {
		return;
	}

	clear_snapshot = stasis_message_data(clear_msg);

	ast_assert(endpoint != NULL);

	ao2_lock(endpoint);
	ast_str_container_remove(endpoint->channel_ids, clear_snapshot->uniqueid);
	ao2_unlock(endpoint);
	endpoint_publish_snapshot(endpoint);
}

static void endpoint_default(void *data,
	struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_endpoint *endpoint = data;

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(endpoint);
	}
}

static struct ast_endpoint *endpoint_internal_create(const char *tech, const char *resource)
{
	RAII_VAR(struct ast_endpoint *, endpoint, NULL, ao2_cleanup);
	RAII_VAR(struct ast_endpoint *, tech_endpoint, NULL, ao2_cleanup);
	int r = 0;

	/* Get/create the technology endpoint */
	if (!ast_strlen_zero(resource)) {
		tech_endpoint = ao2_find(tech_endpoints, tech, OBJ_KEY);
		if (!tech_endpoint) {
			tech_endpoint = endpoint_internal_create(tech, NULL);
			if (!tech_endpoint) {
				return NULL;
			}
		}
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
	ast_string_field_set(endpoint, resource, S_OR(resource, ""));
	ast_string_field_build(endpoint, id, "%s%s%s",
		tech,
		!ast_strlen_zero(resource) ? "/" : "",
		S_OR(resource, ""));

	/* All access to channel_ids should be covered by the endpoint's
	 * lock; no extra lock needed. */
	endpoint->channel_ids = ast_str_container_alloc_options(
		AO2_ALLOC_OPT_LOCK_NOLOCK, ENDPOINT_CHANNEL_BUCKETS);
	if (!endpoint->channel_ids) {
		return NULL;
	}

	if (!ast_strlen_zero(resource)) {

		endpoint->topics = stasis_cp_single_create_only(ast_endpoint_cache_all(),
			endpoint->id);
		if (!endpoint->topics) {
			return NULL;
		}

		endpoint->router = stasis_message_router_create_pool(ast_endpoint_topic(endpoint));
		if (!endpoint->router) {
			return NULL;
		}
		r |= stasis_message_router_add(endpoint->router,
			stasis_cache_clear_type(), endpoint_cache_clear,
			endpoint);
		r |= stasis_message_router_set_default(endpoint->router,
			endpoint_default, endpoint);
		if (r) {
			return NULL;
		}

		if (stasis_cp_single_forward(endpoint->topics, tech_endpoint->topics)) {
			return NULL;
		}

		endpoint_publish_snapshot(endpoint);
		ao2_link(endpoints, endpoint);
	} else {
		endpoint->topics = stasis_cp_single_create(ast_endpoint_cache_all(),
			endpoint->id);
		if (!endpoint->topics) {
			return NULL;
		}

		ao2_link(tech_endpoints, endpoint);
	}

	ao2_ref(endpoint, +1);
	return endpoint;
}

struct ast_endpoint *ast_endpoint_create(const char *tech, const char *resource)
{
	if (ast_strlen_zero(tech)) {
		ast_log(LOG_ERROR, "Endpoint tech cannot be empty\n");
		return NULL;
	}

	if (ast_strlen_zero(resource)) {
		ast_log(LOG_ERROR, "Endpoint resource cannot be empty\n");
		return NULL;
	}

	return endpoint_internal_create(tech, resource);
}

static struct stasis_message *create_endpoint_snapshot_message(struct ast_endpoint *endpoint)
{
	RAII_VAR(struct ast_endpoint_snapshot *, snapshot, NULL, ao2_cleanup);

	if (!ast_endpoint_snapshot_type()) {
		return NULL;
	}

	snapshot = ast_endpoint_snapshot_create(endpoint);
	if (!snapshot) {
		return NULL;
	}

	return stasis_message_create(ast_endpoint_snapshot_type(), snapshot);
}

void ast_endpoint_shutdown(struct ast_endpoint *endpoint)
{
	RAII_VAR(struct stasis_message *, clear_msg, NULL, ao2_cleanup);

	if (endpoint == NULL) {
		return;
	}

	ao2_unlink(endpoints, endpoint);

	clear_msg = create_endpoint_snapshot_message(endpoint);
	if (clear_msg) {
		RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
		message = stasis_cache_clear_create(clear_msg);
		if (message) {
			stasis_publish(ast_endpoint_topic(endpoint), message);
		}
	}

	/* Bump refcount to hold on to the router */
	ao2_ref(endpoint->router, +1);
	stasis_message_router_unsubscribe(endpoint->router);
}

const char *ast_endpoint_get_tech(const struct ast_endpoint *endpoint)
{
	if (!endpoint) {
		return NULL;
	}
	return endpoint->tech;
}

const char *ast_endpoint_get_resource(const struct ast_endpoint *endpoint)
{
	if (!endpoint) {
		return NULL;
	}
	return endpoint->resource;
}

const char *ast_endpoint_get_id(const struct ast_endpoint *endpoint)
{
	if (!endpoint) {
		return NULL;
	}
	return endpoint->id;
}

enum ast_endpoint_state ast_endpoint_get_state(const struct ast_endpoint *endpoint)
{
	if (!endpoint) {
		return AST_ENDPOINT_UNKNOWN;
	}
	return endpoint->state;
}

void ast_endpoint_set_state(struct ast_endpoint *endpoint,
	enum ast_endpoint_state state)
{
	ast_assert(endpoint != NULL);
	ast_assert(!ast_strlen_zero(endpoint->resource));

	ao2_lock(endpoint);
	endpoint->state = state;
	ao2_unlock(endpoint);
	endpoint_publish_snapshot(endpoint);
}

void ast_endpoint_set_max_channels(struct ast_endpoint *endpoint,
	int max_channels)
{
	ast_assert(endpoint != NULL);
	ast_assert(!ast_strlen_zero(endpoint->resource));

	ao2_lock(endpoint);
	endpoint->max_channels = max_channels;
	ao2_unlock(endpoint);
	endpoint_publish_snapshot(endpoint);
}

static void endpoint_snapshot_dtor(void *obj)
{
	struct ast_endpoint_snapshot *snapshot = obj;
	int channel;

	ast_assert(snapshot != NULL);

	for (channel = 0; channel < snapshot->num_channels; channel++) {
		ao2_ref(snapshot->channel_ids[channel], -1);
	}

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

	ast_assert(endpoint != NULL);
	ast_assert(!ast_strlen_zero(endpoint->resource));

	channel_count = ao2_container_count(endpoint->channel_ids);

	snapshot = ao2_alloc_options(
		sizeof(*snapshot) + channel_count * sizeof(char *),
		endpoint_snapshot_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);

	if (!snapshot || ast_string_field_init(snapshot, 80) != 0) {
		ao2_cleanup(snapshot);
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
		/* The reference is kept so the channel id does not go away until the snapshot is gone */
		snapshot->channel_ids[snapshot->num_channels++] = obj;
	}
	ao2_iterator_destroy(&i);

	ao2_ref(snapshot, +1);
	return snapshot;
}

static void endpoint_cleanup(void)
{
	ao2_cleanup(endpoints);
	endpoints = NULL;

	ao2_cleanup(tech_endpoints);
	tech_endpoints = NULL;
}

int ast_endpoint_init(void)
{
	ast_register_cleanup(endpoint_cleanup);

	endpoints = ao2_container_alloc(ENDPOINT_BUCKETS, endpoint_hash,
		endpoint_cmp);
	if (!endpoints) {
		return -1;
	}

	tech_endpoints = ao2_container_alloc(TECH_ENDPOINT_BUCKETS, endpoint_hash,
		endpoint_cmp);
	if (!tech_endpoints) {
		return -1;
	}

	return 0;
}
