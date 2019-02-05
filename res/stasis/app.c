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
 * \brief Stasis application support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

#include "app.h"
#include "control.h"
#include "messaging.h"

#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_message_router.h"

#define BRIDGE_ALL "__AST_BRIDGE_ALL_TOPIC"
#define CHANNEL_ALL "__AST_CHANNEL_ALL_TOPIC"
#define ENDPOINT_ALL "__AST_ENDPOINT_ALL_TOPIC"

/*! Global debug flag.  No need for locking */
int global_debug;

static int unsubscribe(struct stasis_app *app, const char *kind, const char *id, int terminate);

struct stasis_app {
	/*! Aggregation topic for this application. */
	struct stasis_topic *topic;
	/*! Router for handling messages forwarded to \a topic. */
	struct stasis_message_router *router;
	/*! Router for handling messages to the bridge all \a topic. */
	struct stasis_message_router *bridge_router;
	/*! Optional router for handling endpoint messages in 'all' subscriptions */
	struct stasis_message_router *endpoint_router;
	/*! Container of the channel forwards to this app's topic. */
	struct ao2_container *forwards;
	/*! Callback function for this application. */
	stasis_app_cb handler;
	/*! Opaque data to hand to callback function. */
	void *data;
	/*! Subscription model for the application */
	enum stasis_app_subscription_model subscription_model;
	/*! Whether or not someone wants to see debug messages about this app */
	int debug;
	/*! Name of the Stasis application */
	char name[];
};

enum forward_type {
	FORWARD_CHANNEL,
	FORWARD_BRIDGE,
	FORWARD_ENDPOINT,
};

/*! Subscription info for a particular channel/bridge. */
struct app_forwards {
	/*! Count of number of times this channel/bridge has been subscribed */
	int interested;

	/*! Forward for the regular topic */
	struct stasis_forward *topic_forward;
	/*! Forward for the caching topic */
	struct stasis_forward *topic_cached_forward;

	/* Type of object being forwarded */
	enum forward_type forward_type;
	/*! Unique id of the object being forwarded */
	char id[];
};

static void forwards_dtor(void *obj)
{
#ifdef AST_DEVMODE
	struct app_forwards *forwards = obj;
#endif /* AST_DEVMODE */

	ast_assert(forwards->topic_forward == NULL);
	ast_assert(forwards->topic_cached_forward == NULL);
}

static void forwards_unsubscribe(struct app_forwards *forwards)
{
	stasis_forward_cancel(forwards->topic_forward);
	forwards->topic_forward = NULL;
	stasis_forward_cancel(forwards->topic_cached_forward);
	forwards->topic_cached_forward = NULL;
}

static struct app_forwards *forwards_create(struct stasis_app *app,
	const char *id)
{
	struct app_forwards *forwards;

	if (!app || ast_strlen_zero(id)) {
		return NULL;
	}

	forwards = ao2_t_alloc(sizeof(*forwards) + strlen(id) + 1, forwards_dtor, id);
	if (!forwards) {
		return NULL;
	}

	strcpy(forwards->id, id); /* SAFE */

	return forwards;
}

/*! Forward a channel's topics to an app */
static struct app_forwards *forwards_create_channel(struct stasis_app *app,
	struct ast_channel *chan)
{
	struct app_forwards *forwards;

	if (!app) {
		return NULL;
	}

	forwards = forwards_create(app, chan ? ast_channel_uniqueid(chan) : CHANNEL_ALL);
	if (!forwards) {
		return NULL;
	}

	forwards->forward_type = FORWARD_CHANNEL;
	if (chan) {
		forwards->topic_forward = stasis_forward_all(ast_channel_topic(chan),
			app->topic);
	}
	forwards->topic_cached_forward = stasis_forward_all(
		chan ? ast_channel_topic_cached(chan) : ast_channel_topic_all_cached(),
		app->topic);

	if ((!forwards->topic_forward && chan) || !forwards->topic_cached_forward) {
		/* Half-subscribed is a bad thing */
		forwards_unsubscribe(forwards);
		ao2_ref(forwards, -1);
		return NULL;
	}

	return forwards;
}

/*! Forward a bridge's topics to an app */
static struct app_forwards *forwards_create_bridge(struct stasis_app *app,
	struct ast_bridge *bridge)
{
	struct app_forwards *forwards;

	if (!app) {
		return NULL;
	}

	forwards = forwards_create(app, bridge ? bridge->uniqueid : BRIDGE_ALL);
	if (!forwards) {
		return NULL;
	}

	forwards->forward_type = FORWARD_BRIDGE;
	if (bridge) {
		forwards->topic_forward = stasis_forward_all(ast_bridge_topic(bridge),
			app->topic);
	}
	forwards->topic_cached_forward = stasis_forward_all(
		bridge ? ast_bridge_topic_cached(bridge) : ast_bridge_topic_all_cached(),
		app->topic);

	if ((!forwards->topic_forward && bridge) || !forwards->topic_cached_forward) {
		/* Half-subscribed is a bad thing */
		forwards_unsubscribe(forwards);
		ao2_ref(forwards, -1);
		return NULL;
	}

	return forwards;
}

static void endpoint_state_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;

	stasis_publish(app->topic, message);
}

/*! Forward a endpoint's topics to an app */
static struct app_forwards *forwards_create_endpoint(struct stasis_app *app,
	struct ast_endpoint *endpoint)
{
	struct app_forwards *forwards;
	int ret = 0;

	if (!app) {
		return NULL;
	}

	forwards = forwards_create(app, endpoint ? ast_endpoint_get_id(endpoint) : ENDPOINT_ALL);
	if (!forwards) {
		return NULL;
	}

	forwards->forward_type = FORWARD_ENDPOINT;
	if (endpoint) {
		forwards->topic_forward = stasis_forward_all(ast_endpoint_topic(endpoint),
			app->topic);
		forwards->topic_cached_forward = stasis_forward_all(
			ast_endpoint_topic_cached(endpoint), app->topic);

		if (!forwards->topic_forward || !forwards->topic_cached_forward) {
			/* Half-subscribed is a bad thing */
			forwards_unsubscribe(forwards);
			ao2_ref(forwards, -1);
			return NULL;
		}
	} else {
		/* Since endpoint subscriptions also subscribe to channels, in the case
		 * of all endpoint subscriptions, we only want messages for the endpoints.
		 * As such, we route those particular messages and then re-publish them
		 * on the app's topic.
		 */
		ast_assert(app->endpoint_router == NULL);
		app->endpoint_router = stasis_message_router_create(ast_endpoint_topic_all_cached());
		if (!app->endpoint_router) {
			forwards_unsubscribe(forwards);
			ao2_ref(forwards, -1);
			return NULL;
		}

		ret |= stasis_message_router_add(app->endpoint_router,
			ast_endpoint_state_type(), endpoint_state_cb, app);
		ret |= stasis_message_router_add(app->endpoint_router,
			ast_endpoint_contact_state_type(), endpoint_state_cb, app);

		if (ret) {
			ao2_ref(app->endpoint_router, -1);
			app->endpoint_router = NULL;
			ao2_ref(forwards, -1);
			return NULL;
		}
	}

	return forwards;
}

static int forwards_sort(const void *obj_left, const void *obj_right, int flags)
{
	const struct app_forwards *object_left = obj_left;
	const struct app_forwards *object_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_POINTER:
		right_key = object_right->id;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(object_left->id, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(object_left->id, right_key, strlen(right_key));
		break;
	default:
		/* Sort can only work on something with a full or partial key. */
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp;
}

static void app_dtor(void *obj)
{
	struct stasis_app *app = obj;
	size_t size = strlen("stasis-") + strlen(app->name) + 1;
	char context_name[size];

	ast_verb(1, "Destroying Stasis app %s\n", app->name);

	ast_assert(app->router == NULL);
	ast_assert(app->bridge_router == NULL);
	ast_assert(app->endpoint_router == NULL);

	/* If we created a context for this application, remove it */
	strcpy(context_name, "stasis-");
	strcat(context_name, app->name);
	ast_context_destroy_by_name(context_name, "res_stasis");

	ao2_cleanup(app->topic);
	app->topic = NULL;
	ao2_cleanup(app->forwards);
	app->forwards = NULL;
	ao2_cleanup(app->data);
	app->data = NULL;
}

static void call_forwarded_handler(struct stasis_app *app, struct stasis_message *message)
{
	struct ast_multi_channel_blob *payload = stasis_message_data(message);
	struct ast_channel_snapshot *snapshot = ast_multi_channel_blob_get_channel(payload, "forwarded");
	struct ast_channel *chan;

	if (!snapshot) {
		return;
	}

	chan = ast_channel_get_by_name(snapshot->uniqueid);
	if (!chan) {
		return;
	}

	app_subscribe_channel(app, chan);
	ast_channel_unref(chan);
}

static void sub_subscription_change_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(app);
	}
}

static void sub_default_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;
	struct ast_json *json;

	/* The dial type can be converted to JSON so it will always be passed
	 * here.
	 */
	if (stasis_message_type(message) == ast_channel_dial_type()) {
		call_forwarded_handler(app, message);
	}

	/* By default, send any message that has a JSON representation */
	json = stasis_message_to_json(message, stasis_app_get_sanitizer());
	if (!json) {
		return;
	}

	app_send(app, json);
	ast_json_unref(json);
}

/*! \brief Typedef for callbacks that get called on channel snapshot updates */
typedef struct ast_json *(*channel_snapshot_monitor)(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *tv);

static struct ast_json *simple_channel_event(
	const char *type,
	struct ast_channel_snapshot *snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_channel = ast_channel_snapshot_to_json(snapshot, stasis_app_get_sanitizer());

	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"channel", json_channel);
}

static struct ast_json *channel_created_event(
	struct ast_channel_snapshot *snapshot,
	const struct timeval *tv)
{
	return simple_channel_event("ChannelCreated", snapshot, tv);
}

static struct ast_json *channel_destroyed_event(
	struct ast_channel_snapshot *snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_channel = ast_channel_snapshot_to_json(snapshot, stasis_app_get_sanitizer());

	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: i, s: s, s: o}",
		"type", "ChannelDestroyed",
		"timestamp", ast_json_timeval(*tv, NULL),
		"cause", snapshot->hangupcause,
		"cause_txt", ast_cause2str(snapshot->hangupcause),
		"channel", json_channel);
}

static struct ast_json *channel_state_change_event(
	struct ast_channel_snapshot *snapshot,
	const struct timeval *tv)
{
	return simple_channel_event("ChannelStateChange", snapshot, tv);
}

/*! \brief Handle channel state changes */
static struct ast_json *channel_state(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *tv)
{
	struct ast_channel_snapshot *snapshot = new_snapshot ?
		new_snapshot : old_snapshot;

	if (!old_snapshot) {
		return channel_created_event(snapshot, tv);
	} else if (!new_snapshot) {
		return channel_destroyed_event(snapshot, tv);
	} else if (old_snapshot->state != new_snapshot->state) {
		return channel_state_change_event(snapshot, tv);
	}

	return NULL;
}

static struct ast_json *channel_dialplan(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_channel;

	/* No Newexten event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	/* Empty application is not valid for a Newexten event */
	if (ast_strlen_zero(new_snapshot->appl)) {
		return NULL;
	}

	if (ast_channel_snapshot_cep_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	json_channel = ast_channel_snapshot_to_json(new_snapshot, stasis_app_get_sanitizer());
	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: s, s: s, s: o}",
		"type", "ChannelDialplan",
		"timestamp", ast_json_timeval(*tv, NULL),
		"dialplan_app", new_snapshot->appl,
		"dialplan_app_data", AST_JSON_UTF8_VALIDATE(new_snapshot->data),
		"channel", json_channel);
}

static struct ast_json *channel_callerid(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_channel;

	/* No NewCallerid event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (ast_channel_snapshot_caller_id_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	json_channel = ast_channel_snapshot_to_json(new_snapshot, stasis_app_get_sanitizer());
	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: i, s: s, s: o}",
		"type", "ChannelCallerId",
		"timestamp", ast_json_timeval(*tv, NULL),
		"caller_presentation", new_snapshot->caller_pres,
		"caller_presentation_txt", ast_describe_caller_presentation(
			new_snapshot->caller_pres),
		"channel", json_channel);
}

static struct ast_json *channel_connected_line(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_channel;

	/* No ChannelConnectedLine event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (ast_channel_snapshot_connected_line_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	json_channel = ast_channel_snapshot_to_json(new_snapshot, stasis_app_get_sanitizer());
	if (!json_channel) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o}",
		"type", "ChannelConnectedLine",
		"timestamp", ast_json_timeval(*tv, NULL),
		"channel", json_channel);
}

static channel_snapshot_monitor channel_monitors[] = {
	channel_state,
	channel_dialplan,
	channel_callerid,
	channel_connected_line,
};

static void sub_channel_update_handler(void *data,
	struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;
	struct stasis_cache_update *update;
	struct ast_channel_snapshot *new_snapshot;
	struct ast_channel_snapshot *old_snapshot;
	const struct timeval *tv;
	int i;

	ast_assert(stasis_message_type(message) == stasis_cache_update_type());

	update = stasis_message_data(message);

	ast_assert(update->type == ast_channel_snapshot_type());

	new_snapshot = stasis_message_data(update->new_snapshot);
	old_snapshot = stasis_message_data(update->old_snapshot);

	/* Pull timestamp from the new snapshot, or from the update message
	 * when there isn't one. */
	tv = update->new_snapshot ?
		stasis_message_timestamp(update->new_snapshot) :
		stasis_message_timestamp(message);

	for (i = 0; i < ARRAY_LEN(channel_monitors); ++i) {
		struct ast_json *msg;

		msg = channel_monitors[i](old_snapshot, new_snapshot, tv);
		if (msg) {
			app_send(app, msg);
			ast_json_unref(msg);
		}
	}

	if (!new_snapshot && old_snapshot) {
		unsubscribe(app, "channel", old_snapshot->uniqueid, 1);
	}
}

static struct ast_json *simple_endpoint_event(
	const char *type,
	struct ast_endpoint_snapshot *snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_endpoint = ast_endpoint_snapshot_to_json(snapshot, stasis_app_get_sanitizer());

	if (!json_endpoint) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"endpoint", json_endpoint);
}

static int message_received_handler(const char *endpoint_id, struct ast_json *json_msg, void *pvt)
{
	struct ast_endpoint_snapshot *snapshot;
	struct ast_json *json_endpoint;
	struct ast_json *message;
	struct stasis_app *app = pvt;
	char *tech;
	char *resource;

	tech = ast_strdupa(endpoint_id);
	resource = strchr(tech, '/');
	if (resource) {
		resource[0] = '\0';
		resource++;
	}

	if (ast_strlen_zero(tech) || ast_strlen_zero(resource)) {
		return -1;
	}

	snapshot = ast_endpoint_latest_snapshot(tech, resource);
	if (!snapshot) {
		return -1;
	}

	json_endpoint = ast_endpoint_snapshot_to_json(snapshot, stasis_app_get_sanitizer());
	ao2_ref(snapshot, -1);
	if (!json_endpoint) {
		return -1;
	}

	message = ast_json_pack("{s: s, s: o, s: o, s: o}",
		"type", "TextMessageReceived",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"endpoint", json_endpoint,
		"message", ast_json_ref(json_msg));
	if (message) {
		app_send(app, message);
		ast_json_unref(message);
	}

	return 0;
}

static void sub_endpoint_update_handler(void *data,
	struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;
	struct stasis_cache_update *update;
	struct ast_endpoint_snapshot *new_snapshot;
	struct ast_endpoint_snapshot *old_snapshot;
	const struct timeval *tv;

	ast_assert(stasis_message_type(message) == stasis_cache_update_type());

	update = stasis_message_data(message);

	ast_assert(update->type == ast_endpoint_snapshot_type());

	new_snapshot = stasis_message_data(update->new_snapshot);
	old_snapshot = stasis_message_data(update->old_snapshot);

	if (new_snapshot) {
		struct ast_json *json;

		tv = stasis_message_timestamp(update->new_snapshot);

		json = simple_endpoint_event("EndpointStateChange", new_snapshot, tv);
		if (!json) {
			return;
		}

		app_send(app, json);
		ast_json_unref(json);
	}

	if (!new_snapshot && old_snapshot) {
		unsubscribe(app, "endpoint", old_snapshot->id, 1);
	}
}

static struct ast_json *simple_bridge_event(
	const char *type,
	struct ast_bridge_snapshot *snapshot,
	const struct timeval *tv)
{
	struct ast_json *json_bridge = ast_bridge_snapshot_to_json(snapshot, stasis_app_get_sanitizer());
	if (!json_bridge) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"bridge", json_bridge);
}

static void sub_bridge_update_handler(void *data,
	struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_json *json = NULL;
	struct stasis_app *app = data;
	struct stasis_cache_update *update;
	struct ast_bridge_snapshot *new_snapshot;
	struct ast_bridge_snapshot *old_snapshot;
	const struct timeval *tv;

	ast_assert(stasis_message_type(message) == stasis_cache_update_type());

	update = stasis_message_data(message);

	ast_assert(update->type == ast_bridge_snapshot_type());

	new_snapshot = stasis_message_data(update->new_snapshot);
	old_snapshot = stasis_message_data(update->old_snapshot);
	tv = update->new_snapshot ?
		stasis_message_timestamp(update->new_snapshot) :
		stasis_message_timestamp(message);

	if (!new_snapshot) {
		json = simple_bridge_event("BridgeDestroyed", old_snapshot, tv);
	} else if (!old_snapshot) {
		json = simple_bridge_event("BridgeCreated", new_snapshot, tv);
	} else if (new_snapshot && old_snapshot
		&& strcmp(new_snapshot->video_source_id, old_snapshot->video_source_id)) {
		json = simple_bridge_event("BridgeVideoSourceChanged", new_snapshot, tv);
		if (json && !ast_strlen_zero(old_snapshot->video_source_id)) {
			ast_json_object_set(json, "old_video_source_id",
				ast_json_string_create(old_snapshot->video_source_id));
		}
	}

	if (json) {
		app_send(app, json);
		ast_json_unref(json);
	}

	if (!new_snapshot && old_snapshot) {
		unsubscribe(app, "bridge", old_snapshot->uniqueid, 1);
	}
}


/*! \brief Helper function for determining if the application is subscribed to a given entity */
static int bridge_app_subscribed(struct stasis_app *app, const char *uniqueid)
{
	struct app_forwards *forwards = NULL;

	forwards = ao2_find(app->forwards, uniqueid, OBJ_SEARCH_KEY);
	if (!forwards) {
		return 0;
	}

	ao2_ref(forwards, -1);
	return 1;
}

static void bridge_merge_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;
	struct ast_bridge_merge_message *merge;

	merge = stasis_message_data(message);

	/* Find out if we're subscribed to either bridge */
	if (bridge_app_subscribed(app, merge->from->uniqueid) ||
		bridge_app_subscribed(app, merge->to->uniqueid)) {
		/* Forward the message to the app */
		stasis_publish(app->topic, message);
	}
}

/*! \brief Callback function for checking if channels in a bridge are subscribed to */
static int bridge_app_subscribed_involved(struct stasis_app *app, struct ast_bridge_snapshot *snapshot)
{
	int subscribed = 0;
	struct ao2_iterator iter;
	char *uniqueid;

	if (bridge_app_subscribed(app, snapshot->uniqueid)) {
		return 1;
	}

	iter = ao2_iterator_init(snapshot->channels, 0);
	for (; (uniqueid = ao2_iterator_next(&iter)); ao2_ref(uniqueid, -1)) {
		if (bridge_app_subscribed(app, uniqueid)) {
			subscribed = 1;
			ao2_ref(uniqueid, -1);
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	return subscribed;
}

static void bridge_blind_transfer_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;
	struct ast_blind_transfer_message *transfer_msg = stasis_message_data(message);
	struct ast_bridge_snapshot *bridge = transfer_msg->bridge;

	if (bridge_app_subscribed(app, transfer_msg->transferer->uniqueid) ||
		(bridge && bridge_app_subscribed_involved(app, bridge))) {
		stasis_publish(app->topic, message);
	}
}

static void bridge_attended_transfer_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;
	struct ast_attended_transfer_message *transfer_msg = stasis_message_data(message);
	int subscribed = 0;

	subscribed = bridge_app_subscribed(app, transfer_msg->to_transferee.channel_snapshot->uniqueid);
	if (!subscribed) {
		subscribed = bridge_app_subscribed(app, transfer_msg->to_transfer_target.channel_snapshot->uniqueid);
	}
	if (!subscribed && transfer_msg->to_transferee.bridge_snapshot) {
		subscribed = bridge_app_subscribed_involved(app, transfer_msg->to_transferee.bridge_snapshot);
	}
	if (!subscribed && transfer_msg->to_transfer_target.bridge_snapshot) {
		subscribed = bridge_app_subscribed_involved(app, transfer_msg->to_transfer_target.bridge_snapshot);
	}

	if (!subscribed) {
		switch (transfer_msg->dest_type) {
		case AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE:
			subscribed = bridge_app_subscribed(app, transfer_msg->dest.bridge);
			break;
		case AST_ATTENDED_TRANSFER_DEST_LINK:
			subscribed = bridge_app_subscribed(app, transfer_msg->dest.links[0]->uniqueid);
			if (!subscribed) {
				subscribed = bridge_app_subscribed(app, transfer_msg->dest.links[1]->uniqueid);
			}
			break;
		break;
		case AST_ATTENDED_TRANSFER_DEST_THREEWAY:
			subscribed = bridge_app_subscribed_involved(app, transfer_msg->dest.threeway.bridge_snapshot);
			if (!subscribed) {
				subscribed = bridge_app_subscribed(app, transfer_msg->dest.threeway.channel_snapshot->uniqueid);
			}
			break;
		default:
			break;
		}
	}

	if (subscribed) {
		stasis_publish(app->topic, message);
	}
}

static void bridge_subscription_change_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_app *app = data;

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(app);
	}
}

void stasis_app_set_debug(struct stasis_app *app, int debug)
{
	if (!app) {
		return;
	}

	app->debug = debug;
}

void stasis_app_set_debug_by_name(const char *app_name, int debug)
{
	struct stasis_app *app = stasis_app_get_by_name(app_name);

	if (!app) {
		return;
	}

	app->debug = debug;
	ao2_cleanup(app);
}

int stasis_app_get_debug(struct stasis_app *app)
{
	return (app ? app->debug : 0) || global_debug;
}

int stasis_app_get_debug_by_name(const char *app_name)
{
	int debug_enabled = 0;

	if (global_debug) {
		debug_enabled = 1;
	} else {
		struct stasis_app *app = stasis_app_get_by_name(app_name);

		if (app) {
			if (app->debug) {
				debug_enabled = 1;
			}
			ao2_ref(app, -1);
		}
	}
	return debug_enabled;
}

void stasis_app_set_global_debug(int debug)
{
	global_debug = debug;
	if (!global_debug) {
		struct ao2_container *app_names = stasis_app_get_all();
		struct ao2_iterator it_app_names;
		char *app_name;
		struct stasis_app *app;

		if (!app_names || !ao2_container_count(app_names)) {
			ao2_cleanup(app_names);
			return;
		}

		it_app_names = ao2_iterator_init(app_names, 0);
		while ((app_name = ao2_iterator_next(&it_app_names))) {
			if ((app = stasis_app_get_by_name(app_name))) {
				stasis_app_set_debug(app, 0);
			}

			ao2_cleanup(app_name);
			ao2_cleanup(app);
		}
		ao2_iterator_cleanup(&it_app_names);
		ao2_cleanup(app_names);
	}
}

struct stasis_app *app_create(const char *name, stasis_app_cb handler, void *data, enum stasis_app_subscription_model subscription_model)
{
	RAII_VAR(struct stasis_app *, app, NULL, ao2_cleanup);
	size_t size;
	int res = 0;
	size_t context_size = strlen("stasis-") + strlen(name) + 1;
	char context_name[context_size];

	ast_assert(name != NULL);
	ast_assert(handler != NULL);

	ast_verb(1, "Creating Stasis app '%s'\n", name);

	size = sizeof(*app) + strlen(name) + 1;
	app = ao2_alloc_options(size, app_dtor, AO2_ALLOC_OPT_LOCK_MUTEX);
	if (!app) {
		return NULL;
	}
	app->subscription_model = subscription_model;

	app->forwards = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT,
		forwards_sort, NULL);
	if (!app->forwards) {
		return NULL;
	}

	app->topic = stasis_topic_create(name);
	if (!app->topic) {
		return NULL;
	}

	app->bridge_router = stasis_message_router_create(ast_bridge_topic_all());
	if (!app->bridge_router) {
		return NULL;
	}

	res |= stasis_message_router_add(app->bridge_router,
		ast_bridge_merge_message_type(), bridge_merge_handler, app);

	res |= stasis_message_router_add(app->bridge_router,
		ast_blind_transfer_type(), bridge_blind_transfer_handler, app);

	res |= stasis_message_router_add(app->bridge_router,
		ast_attended_transfer_type(), bridge_attended_transfer_handler, app);

	res |= stasis_message_router_add(app->bridge_router,
		stasis_subscription_change_type(), bridge_subscription_change_handler, app);

	if (res != 0) {
		return NULL;
	}
	/* Bridge router holds a reference */
	ao2_ref(app, +1);

	app->router = stasis_message_router_create(app->topic);
	if (!app->router) {
		return NULL;
	}

	res |= stasis_message_router_add_cache_update(app->router,
		ast_bridge_snapshot_type(), sub_bridge_update_handler, app);

	res |= stasis_message_router_add_cache_update(app->router,
		ast_channel_snapshot_type(), sub_channel_update_handler, app);

	res |= stasis_message_router_add_cache_update(app->router,
		ast_endpoint_snapshot_type(), sub_endpoint_update_handler, app);

	res |= stasis_message_router_add(app->router,
		stasis_subscription_change_type(), sub_subscription_change_handler, app);

	stasis_message_router_set_formatters_default(app->router,
		sub_default_handler, app, STASIS_SUBSCRIPTION_FORMATTER_JSON);

	if (res != 0) {
		return NULL;
	}
	/* Router holds a reference */
	ao2_ref(app, +1);

	strncpy(app->name, name, size - sizeof(*app));
	app->handler = handler;
	app->data = ao2_bump(data);

	/* Create a context, a match-all extension, and a 'h' extension for this application. Note that
	 * this should only be done if a context does not already exist. */
	strcpy(context_name, "stasis-");
	strcat(context_name, name);
	if (!ast_context_find(context_name)) {
		if (!ast_context_find_or_create(NULL, NULL, context_name, "res_stasis")) {
			ast_log(LOG_WARNING, "Could not create context '%s' for Stasis application '%s'\n", context_name, name);
		} else {
			ast_add_extension(context_name, 0, "_.", 1, NULL, NULL, "Stasis", ast_strdup(name), ast_free_ptr, "res_stasis");
			ast_add_extension(context_name, 0, "h", 1, NULL, NULL, "NoOp", NULL, NULL, "res_stasis");
		}
	} else {
		ast_log(LOG_WARNING, "Not creating context '%s' for Stasis application '%s' because it already exists\n",
			context_name, name);
	}

	ao2_ref(app, +1);
	return app;
}

struct stasis_topic *ast_app_get_topic(struct stasis_app *app)
{
	return app->topic;
}

/*!
 * \brief Send a message to the given application.
 * \param app App to send the message to.
 * \param message Message to send.
 */
void app_send(struct stasis_app *app, struct ast_json *message)
{
	stasis_app_cb handler;
	char eid[20];
	void *data;

	if (ast_json_object_set(message, "asterisk_id", ast_json_string_create(
			ast_eid_to_str(eid, sizeof(eid), &ast_eid_default)))) {
		ast_log(AST_LOG_WARNING, "Failed to append EID to outgoing event %s\n",
			ast_json_string_get(ast_json_object_get(message, "type")));
	}

	/* Copy off mutable state with lock held */
	ao2_lock(app);
	handler = app->handler;
	data = ao2_bump(app->data);
	ao2_unlock(app);
	/* Name is immutable; no need to copy */

	if (handler) {
		handler(data, app->name, message);
	} else {
		ast_verb(3,
			"Inactive Stasis app '%s' missed message\n", app->name);
	}
	ao2_cleanup(data);
}

void app_deactivate(struct stasis_app *app)
{
	ao2_lock(app);

	ast_verb(1, "Deactivating Stasis app '%s'\n", app->name);
	app->handler = NULL;
	ao2_cleanup(app->data);
	app->data = NULL;

	ao2_unlock(app);
}

void app_shutdown(struct stasis_app *app)
{
	ao2_lock(app);

	ast_assert(app_is_finished(app));

	stasis_message_router_unsubscribe(app->router);
	app->router = NULL;
	stasis_message_router_unsubscribe(app->bridge_router);
	app->bridge_router = NULL;
	stasis_message_router_unsubscribe(app->endpoint_router);
	app->endpoint_router = NULL;

	ao2_unlock(app);
}

int app_is_active(struct stasis_app *app)
{
	int ret;

	ao2_lock(app);
	ret = app->handler != NULL;
	ao2_unlock(app);

	return ret;
}

int app_is_finished(struct stasis_app *app)
{
	int ret;

	ao2_lock(app);
	ret = app->handler == NULL && ao2_container_count(app->forwards) == 0;
	ao2_unlock(app);

	return ret;
}

void app_update(struct stasis_app *app, stasis_app_cb handler, void *data)
{
	ao2_lock(app);
	if (app->handler && app->data) {
		struct ast_json *msg;

		ast_verb(1, "Replacing Stasis app '%s'\n", app->name);

		msg = ast_json_pack("{s: s, s: s}",
			"type", "ApplicationReplaced",
			"application", app->name);
		if (msg) {
			app_send(app, msg);
			ast_json_unref(msg);
		}
	} else {
		ast_verb(1, "Activating Stasis app '%s'\n", app->name);
	}

	app->handler = handler;
	ao2_replace(app->data, data);
	ao2_unlock(app);
}

const char *stasis_app_name(const struct stasis_app *app)
{
	return app->name;
}

static int forwards_filter_by_type(void *obj, void *arg, int flags)
{
	struct app_forwards *forward = obj;
	enum forward_type *forward_type = arg;

	if (forward->forward_type == *forward_type) {
		return CMP_MATCH;
	}

	return 0;
}

void stasis_app_to_cli(const struct stasis_app *app, struct ast_cli_args *a)
{
	struct ao2_iterator *channels;
	struct ao2_iterator *endpoints;
	struct ao2_iterator *bridges;
	struct app_forwards *forward;
	enum forward_type forward_type;

	ast_cli(a->fd, "Name: %s\n"
		"  Debug: %s\n"
		"  Subscription Model: %s\n",
		app->name,
		app->debug ? "Yes" : "No",
		app->subscription_model == STASIS_APP_SUBSCRIBE_ALL ?
			"Global Resource Subscription" :
			"Application/Explicit Resource Subscription");
	ast_cli(a->fd, "  Subscriptions: %d\n", ao2_container_count(app->forwards));

	ast_cli(a->fd, "    Channels:\n");
	forward_type = FORWARD_CHANNEL;
	channels = ao2_callback(app->forwards, OBJ_MULTIPLE,
		forwards_filter_by_type, &forward_type);
	if (channels) {
		while ((forward = ao2_iterator_next(channels))) {
			ast_cli(a->fd, "      %s (%d)\n", forward->id, forward->interested);
			ao2_ref(forward, -1);
		}
		ao2_iterator_destroy(channels);
	}

	ast_cli(a->fd, "    Bridges:\n");
	forward_type = FORWARD_BRIDGE;
	bridges = ao2_callback(app->forwards, OBJ_MULTIPLE,
		forwards_filter_by_type, &forward_type);
	if (bridges) {
		while ((forward = ao2_iterator_next(bridges))) {
			ast_cli(a->fd, "      %s (%d)\n", forward->id, forward->interested);
			ao2_ref(forward, -1);
		}
		ao2_iterator_destroy(bridges);
	}

	ast_cli(a->fd, "    Endpoints:\n");
	forward_type = FORWARD_ENDPOINT;
	endpoints = ao2_callback(app->forwards, OBJ_MULTIPLE,
		forwards_filter_by_type, &forward_type);
	if (endpoints) {
		while ((forward = ao2_iterator_next(endpoints))) {
			ast_cli(a->fd, "      %s (%d)\n", forward->id, forward->interested);
			ao2_ref(forward, -1);
		}
		ao2_iterator_destroy(endpoints);
	}
}

struct ast_json *app_to_json(const struct stasis_app *app)
{
	struct ast_json *json;
	struct ast_json *channels;
	struct ast_json *bridges;
	struct ast_json *endpoints;
	struct ao2_iterator i;
	struct app_forwards *forwards;

	json = ast_json_pack("{s: s, s: [], s: [], s: []}",
		"name", app->name,
		"channel_ids", "bridge_ids", "endpoint_ids");
	if (!json) {
		return NULL;
	}
	channels = ast_json_object_get(json, "channel_ids");
	bridges = ast_json_object_get(json, "bridge_ids");
	endpoints = ast_json_object_get(json, "endpoint_ids");

	i = ao2_iterator_init(app->forwards, 0);
	while ((forwards = ao2_iterator_next(&i))) {
		struct ast_json *array = NULL;
		int append_res;

		switch (forwards->forward_type) {
		case FORWARD_CHANNEL:
			array = channels;
			break;
		case FORWARD_BRIDGE:
			array = bridges;
			break;
		case FORWARD_ENDPOINT:
			array = endpoints;
			break;
		}

		/* If forward_type value is unexpected this will safely return an error. */
		append_res = ast_json_array_append(array, ast_json_string_create(forwards->id));
		ao2_ref(forwards, -1);

		if (append_res != 0) {
			ast_log(LOG_ERROR, "Error building response\n");
			ao2_iterator_destroy(&i);
			ast_json_unref(json);

			return NULL;
		}
	}
	ao2_iterator_destroy(&i);

	return json;
}

int app_subscribe_channel(struct stasis_app *app, struct ast_channel *chan)
{
	struct app_forwards *forwards;

	if (!app) {
		return -1;
	}

	ao2_lock(app->forwards);
	/* If subscribed to all, don't subscribe again */
	forwards = ao2_find(app->forwards, CHANNEL_ALL, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (forwards) {
		ao2_unlock(app->forwards);
		ao2_ref(forwards, -1);

		return 0;
	}

	forwards = ao2_find(app->forwards,
		chan ? ast_channel_uniqueid(chan) : CHANNEL_ALL,
		OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!forwards) {
		int res;

		/* Forwards not found, create one */
		forwards = forwards_create_channel(app, chan);
		if (!forwards) {
			ao2_unlock(app->forwards);

			return -1;
		}

		res = ao2_link_flags(app->forwards, forwards, OBJ_NOLOCK);
		if (!res) {
			ao2_unlock(app->forwards);
			ao2_ref(forwards, -1);

			return -1;
		}
	}

	++forwards->interested;
	ast_debug(3, "Channel '%s' is %d interested in %s\n",
		chan ? ast_channel_uniqueid(chan) : "ALL",
		forwards->interested,
		app->name);

	ao2_unlock(app->forwards);
	ao2_ref(forwards, -1);

	return 0;
}

static int subscribe_channel(struct stasis_app *app, void *obj)
{
	return app_subscribe_channel(app, obj);
}

static int unsubscribe(struct stasis_app *app, const char *kind, const char *id, int terminate)
{
	struct app_forwards *forwards;

	if (!id) {
		if (!strcmp(kind, "bridge")) {
			id = BRIDGE_ALL;
		} else if (!strcmp(kind, "channel")) {
			id = CHANNEL_ALL;
		} else if (!strcmp(kind, "endpoint")) {
			id = ENDPOINT_ALL;
		} else {
			ast_log(LOG_WARNING, "Unknown subscription kind '%s'\n", kind);
			return -1;
		}
	}

	ao2_lock(app->forwards);
	forwards = ao2_find(app->forwards, id, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!forwards) {
		ao2_unlock(app->forwards);
		ast_debug(3, "App '%s' not subscribed to %s '%s'\n", app->name, kind, id);
		return -1;
	}
	forwards->interested--;

	ast_debug(3, "%s '%s': is %d interested in %s\n", kind, id, forwards->interested, app->name);
	if (forwards->interested == 0 || terminate) {
		/* No one is interested any more; unsubscribe */
		ast_debug(3, "%s '%s' unsubscribed from %s\n", kind, id, app->name);
		forwards_unsubscribe(forwards);
		ao2_find(app->forwards, forwards,
			OBJ_POINTER | OBJ_NOLOCK | OBJ_UNLINK |
			OBJ_NODATA);

		if (!strcmp(kind, "endpoint")) {
			messaging_app_unsubscribe_endpoint(app->name, id);
		}
	}
	ao2_unlock(app->forwards);
	ao2_ref(forwards, -1);

	return 0;
}

int app_unsubscribe_channel(struct stasis_app *app, struct ast_channel *chan)
{
	if (!app) {
		return -1;
	}

	return app_unsubscribe_channel_id(app, chan ? ast_channel_uniqueid(chan) : CHANNEL_ALL);
}

int app_unsubscribe_channel_id(struct stasis_app *app, const char *channel_id)
{
	if (!app) {
		return -1;
	}

	return unsubscribe(app, "channel", channel_id, 0);
}

int app_is_subscribed_channel_id(struct stasis_app *app, const char *channel_id)
{
	struct app_forwards *forwards;

	if (ast_strlen_zero(channel_id)) {
		channel_id = CHANNEL_ALL;
	}
	forwards = ao2_find(app->forwards, channel_id, OBJ_SEARCH_KEY);
	ao2_cleanup(forwards);

	return forwards != NULL;
}

static void *channel_find(const struct stasis_app *app, const char *id)
{
	return ast_channel_get_by_name(id);
}

struct stasis_app_event_source channel_event_source = {
	.scheme = "channel:",
	.find = channel_find,
	.subscribe = subscribe_channel,
	.unsubscribe = app_unsubscribe_channel_id,
	.is_subscribed = app_is_subscribed_channel_id
};

int app_subscribe_bridge(struct stasis_app *app, struct ast_bridge *bridge)
{
	struct app_forwards *forwards;

	if (!app) {
		return -1;
	}

	ao2_lock(app->forwards);
	/* If subscribed to all, don't subscribe again */
	forwards = ao2_find(app->forwards, BRIDGE_ALL, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (forwards) {
		ao2_unlock(app->forwards);
		ao2_ref(forwards, -1);

		return 0;
	}

	forwards = ao2_find(app->forwards,
		bridge ? bridge->uniqueid : BRIDGE_ALL,
		OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!forwards) {
		int res;

		/* Forwards not found, create one */
		forwards = forwards_create_bridge(app, bridge);
		if (!forwards) {
			ao2_unlock(app->forwards);

			return -1;
		}

		res = ao2_link_flags(app->forwards, forwards, OBJ_NOLOCK);
		if (!res) {
			ao2_unlock(app->forwards);
			ao2_ref(forwards, -1);

			return -1;
		}
	}

	++forwards->interested;
	ast_debug(3, "Bridge '%s' is %d interested in %s\n",
		bridge ? bridge->uniqueid : "ALL",
		forwards->interested,
		app->name);

	ao2_unlock(app->forwards);
	ao2_ref(forwards, -1);

	return 0;
}

static int subscribe_bridge(struct stasis_app *app, void *obj)
{
	return app_subscribe_bridge(app, obj);
}

int app_unsubscribe_bridge(struct stasis_app *app, struct ast_bridge *bridge)
{
	if (!app) {
		return -1;
	}

	return app_unsubscribe_bridge_id(app, bridge ? bridge->uniqueid : BRIDGE_ALL);
}

int app_unsubscribe_bridge_id(struct stasis_app *app, const char *bridge_id)
{
	if (!app) {
		return -1;
	}

	return unsubscribe(app, "bridge", bridge_id, 0);
}

int app_is_subscribed_bridge_id(struct stasis_app *app, const char *bridge_id)
{
	struct app_forwards *forwards;

	if (ast_strlen_zero(bridge_id)) {
		bridge_id = BRIDGE_ALL;
	}

	forwards = ao2_find(app->forwards, bridge_id, OBJ_SEARCH_KEY);
	ao2_cleanup(forwards);

	return forwards != NULL;
}

static void *bridge_find(const struct stasis_app *app, const char *id)
{
	return stasis_app_bridge_find_by_id(id);
}

struct stasis_app_event_source bridge_event_source = {
	.scheme = "bridge:",
	.find = bridge_find,
	.subscribe = subscribe_bridge,
	.unsubscribe = app_unsubscribe_bridge_id,
	.is_subscribed = app_is_subscribed_bridge_id
};

int app_subscribe_endpoint(struct stasis_app *app, struct ast_endpoint *endpoint)
{
	struct app_forwards *forwards;

	if (!app) {
		return -1;
	}

	ao2_lock(app->forwards);
	/* If subscribed to all, don't subscribe again */
	forwards = ao2_find(app->forwards, ENDPOINT_ALL, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (forwards) {
		ao2_unlock(app->forwards);
		ao2_ref(forwards, -1);

		return 0;
	}

	forwards = ao2_find(app->forwards,
		endpoint ? ast_endpoint_get_id(endpoint) : ENDPOINT_ALL,
		OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!forwards) {
		int res;

		/* Forwards not found, create one */
		forwards = forwards_create_endpoint(app, endpoint);
		if (!forwards) {
			ao2_unlock(app->forwards);

			return -1;
		}

		res = ao2_link_flags(app->forwards, forwards, OBJ_NOLOCK);
		if (!res) {
			ao2_unlock(app->forwards);
			ao2_ref(forwards, -1);

			return -1;
		}

		/* Subscribe for messages */
		messaging_app_subscribe_endpoint(app->name, endpoint, &message_received_handler, app);
	}

	++forwards->interested;
	ast_debug(3, "Endpoint '%s' is %d interested in %s\n",
		endpoint ? ast_endpoint_get_id(endpoint) : "ALL",
		forwards->interested,
		app->name);

	ao2_unlock(app->forwards);
	ao2_ref(forwards, -1);

	return 0;
}

static int subscribe_endpoint(struct stasis_app *app, void *obj)
{
	return app_subscribe_endpoint(app, obj);
}

int app_unsubscribe_endpoint_id(struct stasis_app *app, const char *endpoint_id)
{
	if (!app) {
		return -1;
	}

	return unsubscribe(app, "endpoint", endpoint_id, 0);
}

int app_is_subscribed_endpoint_id(struct stasis_app *app, const char *endpoint_id)
{
	struct app_forwards *forwards;

	if (ast_strlen_zero(endpoint_id)) {
		endpoint_id = ENDPOINT_ALL;
	}
	forwards = ao2_find(app->forwards, endpoint_id, OBJ_SEARCH_KEY);
	ao2_cleanup(forwards);

	return forwards != NULL;
}

static void *endpoint_find(const struct stasis_app *app, const char *id)
{
	return ast_endpoint_find_by_id(id);
}

struct stasis_app_event_source endpoint_event_source = {
	.scheme = "endpoint:",
	.find = endpoint_find,
	.subscribe = subscribe_endpoint,
	.unsubscribe = app_unsubscribe_endpoint_id,
	.is_subscribed = app_is_subscribed_endpoint_id
};

void stasis_app_register_event_sources(void)
{
	stasis_app_register_event_source(&channel_event_source);
	stasis_app_register_event_source(&bridge_event_source);
	stasis_app_register_event_source(&endpoint_event_source);
}

void stasis_app_unregister_event_sources(void)
{
	stasis_app_unregister_event_source(&endpoint_event_source);
	stasis_app_unregister_event_source(&bridge_event_source);
	stasis_app_unregister_event_source(&channel_event_source);
}
