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
 * \brief Stasis application support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 *
 * <code>res_stasis.so</code> brings together the various components of the
 * Stasis application infrastructure.
 *
 * First, there's the Stasis application handler, stasis_app_exec(). This is
 * called by <code>app_stasis.so</code> to give control of a channel to the
 * Stasis application code from the dialplan.
 *
 * While a channel is in stasis_app_exec(), it has a \ref stasis_app_control
 * object, which may be used to control the channel.
 *
 * To control the channel, commands may be sent to channel using
 * stasis_app_send_command() and stasis_app_send_async_command().
 *
 * Alongside this, applications may be registered/unregistered using
 * stasis_app_register()/stasis_app_unregister(). While a channel is in Stasis,
 * events received on the channel's topic are converted to JSON and forwarded to
 * the \ref stasis_app_cb. The application may also subscribe to the channel to
 * continue to receive messages even after the channel has left Stasis, but it
 * will not be able to control it.
 *
 * Given all the stuff that comes together in this module, it's been broken up
 * into several pieces that are in <code>res/stasis/</code> and compiled into
 * <code>res_stasis.so</code>.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/callerid.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/strings.h"
#include "stasis/app.h"
#include "stasis/control.h"
#include "stasis/messaging.h"
#include "stasis/stasis_bridge.h"
#include "asterisk/core_unreal.h"
#include "asterisk/musiconhold.h"
#include "asterisk/causes.h"
#include "asterisk/stringfields.h"
#include "asterisk/bridge_after.h"
#include "asterisk/format_cache.h"

/*! Time to wait for a frame in the application */
#define MAX_WAIT_MS 200

/*!
 * \brief Number of buckets for the Stasis application hash table.  Remember to
 * keep it a prime number!
 */
#define APPS_NUM_BUCKETS 127

/*!
 * \brief Number of buckets for the Stasis application hash table.  Remember to
 * keep it a prime number!
 */
#define CONTROLS_NUM_BUCKETS 127

/*!
 * \brief Number of buckets for the Stasis bridges hash table.  Remember to
 * keep it a prime number!
 */
#define BRIDGES_NUM_BUCKETS 127

/*!
 * \brief Stasis application container.
 */
struct ao2_container *apps_registry;

struct ao2_container *app_controls;

struct ao2_container *app_bridges;

struct ao2_container *app_bridges_moh;

struct ao2_container *app_bridges_playback;

static struct ast_json *stasis_end_json_payload(struct ast_channel_snapshot *snapshot,
		const struct stasis_message_sanitizer *sanitize)
{
	return ast_json_pack("{s: s, s: o, s: o}",
		"type", "StasisEnd",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"channel", ast_channel_snapshot_to_json(snapshot, sanitize));
}

static struct ast_json *stasis_end_to_json(struct stasis_message *message,
		const struct stasis_message_sanitizer *sanitize)
{
	struct ast_channel_blob *payload = stasis_message_data(message);

	if (sanitize && sanitize->channel_snapshot &&
			sanitize->channel_snapshot(payload->snapshot)) {
		return NULL;
	}

	return stasis_end_json_payload(payload->snapshot, sanitize);
}

STASIS_MESSAGE_TYPE_DEFN(ast_stasis_end_message_type,
	.to_json = stasis_end_to_json);

const char *stasis_app_name(const struct stasis_app *app)
{
	return app_name(app);
}

/*! AO2 hash function for \ref app */
static int app_hash(const void *obj, const int flags)
{
	const struct stasis_app *app;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		app = obj;
		key = stasis_app_name(app);
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! AO2 comparison function for \ref app */
static int app_compare(void *obj, void *arg, int flags)
{
	const struct stasis_app *object_left = obj;
	const struct stasis_app *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = stasis_app_name(object_right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(stasis_app_name(object_left), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(stasis_app_name(object_left), right_key, strlen(right_key));
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

/*! AO2 hash function for \ref stasis_app_control */
static int control_hash(const void *obj, const int flags)
{
	const struct stasis_app_control *control;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		control = obj;
		key = stasis_app_control_get_channel_id(control);
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! AO2 comparison function for \ref stasis_app_control */
static int control_compare(void *obj, void *arg, int flags)
{
	const struct stasis_app_control *object_left = obj;
	const struct stasis_app_control *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = stasis_app_control_get_channel_id(object_right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(stasis_app_control_get_channel_id(object_left), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(stasis_app_control_get_channel_id(object_left), right_key, strlen(right_key));
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

static int cleanup_cb(void *obj, void *arg, int flags)
{
	struct stasis_app *app = obj;

	if (!app_is_finished(app)) {
		return 0;
	}

	ast_verb(1, "Shutting down application '%s'\n", stasis_app_name(app));
	app_shutdown(app);

	return CMP_MATCH;

}

/*!
 * \brief Clean up any old apps that we don't need any more.
 */
static void cleanup(void)
{
	ao2_callback(apps_registry, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK,
		cleanup_cb, NULL);
}

struct stasis_app_control *stasis_app_control_create(struct ast_channel *chan)
{
	return control_create(chan, NULL);
}

struct stasis_app_control *stasis_app_control_find_by_channel(
	const struct ast_channel *chan)
{
	if (chan == NULL) {
		return NULL;
	}

	return stasis_app_control_find_by_channel_id(
		ast_channel_uniqueid(chan));
}

struct stasis_app_control *stasis_app_control_find_by_channel_id(
	const char *channel_id)
{
	return ao2_find(app_controls, channel_id, OBJ_SEARCH_KEY);
}

/*! AO2 hash function for bridges container  */
static int bridges_hash(const void *obj, const int flags)
{
	const struct ast_bridge *bridge;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		bridge = obj;
		key = bridge->uniqueid;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! AO2 comparison function for bridges container */
static int bridges_compare(void *obj, void *arg, int flags)
{
	const struct ast_bridge *object_left = obj;
	const struct ast_bridge *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->uniqueid;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->uniqueid, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(object_left->uniqueid, right_key, strlen(right_key));
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

/*!
 *  Used with app_bridges_moh and app_bridge_control, they provide links
 *  between bridges and channels used for ARI application purposes
 */
struct stasis_app_bridge_channel_wrapper {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(channel_id);
		AST_STRING_FIELD(bridge_id);
	);
};

static void stasis_app_bridge_channel_wrapper_destructor(void *obj)
{
	struct stasis_app_bridge_channel_wrapper *wrapper = obj;
	ast_string_field_free_memory(wrapper);
}

/*! AO2 hash function for the bridges moh container */
static int bridges_channel_hash_fn(const void *obj, const int flags)
{
	const struct stasis_app_bridge_channel_wrapper *wrapper;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		wrapper = obj;
		key = wrapper->bridge_id;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

static int bridges_channel_sort_fn(const void *obj_left, const void *obj_right, const int flags)
{
	const struct stasis_app_bridge_channel_wrapper *left = obj_left;
	const struct stasis_app_bridge_channel_wrapper *right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right->bridge_id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left->bridge_id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left->bridge_id, right_key, strlen(right_key));
		break;
	default:
		/* Sort can only work on something with a full or partial key. */
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp;
}

/*! Removes the bridge to music on hold channel link */
static void remove_bridge_moh(char *bridge_id)
{
	ao2_find(app_bridges_moh, bridge_id, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
	ast_free(bridge_id);
}

/*! After bridge failure callback for moh channels */
static void moh_after_bridge_cb_failed(enum ast_bridge_after_cb_reason reason, void *data)
{
	char *bridge_id = data;

	remove_bridge_moh(bridge_id);
}

/*! After bridge callback for moh channels */
static void moh_after_bridge_cb(struct ast_channel *chan, void *data)
{
	char *bridge_id = data;

	remove_bridge_moh(bridge_id);
}

/*! Request a bridge MOH channel */
static struct ast_channel *prepare_bridge_moh_channel(void)
{
	RAII_VAR(struct ast_format_cap *, cap, NULL, ao2_cleanup);

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		return NULL;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	return ast_request("Announcer", cap, NULL, NULL, "ARI_MOH", NULL);
}

/*! Provides the moh channel with a thread so it can actually play its music */
static void *moh_channel_thread(void *data)
{
	struct ast_channel *moh_channel = data;

	while (!ast_safe_sleep(moh_channel, 1000)) {
	}

	ast_moh_stop(moh_channel);
	ast_hangup(moh_channel);

	return NULL;
}

/*!
 * \internal
 * \brief Creates, pushes, and links a channel for playing music on hold to bridge
 *
 * \param bridge Which bridge this moh channel exists for
 *
 * \retval NULL if the channel could not be created, pushed, or linked
 * \retval Reference to the channel on success
 */
static struct ast_channel *bridge_moh_create(struct ast_bridge *bridge)
{
	RAII_VAR(struct stasis_app_bridge_channel_wrapper *, new_wrapper, NULL, ao2_cleanup);
	RAII_VAR(char *, bridge_id, ast_strdup(bridge->uniqueid), ast_free);
	struct ast_channel *chan;
	pthread_t threadid;

	if (!bridge_id) {
		return NULL;
	}

	chan = prepare_bridge_moh_channel();
	if (!chan) {
		return NULL;
	}

	/* The after bridge callback assumes responsibility of the bridge_id. */
	if (ast_bridge_set_after_callback(chan,
		moh_after_bridge_cb, moh_after_bridge_cb_failed, bridge_id)) {
		ast_hangup(chan);
		return NULL;
	}
	bridge_id = NULL;

	if (ast_unreal_channel_push_to_bridge(chan, bridge,
		AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE | AST_BRIDGE_CHANNEL_FLAG_LONELY)) {
		ast_hangup(chan);
		return NULL;
	}

	new_wrapper = ao2_alloc_options(sizeof(*new_wrapper),
		stasis_app_bridge_channel_wrapper_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!new_wrapper) {
		ast_hangup(chan);
		return NULL;
	}

	if (ast_string_field_init(new_wrapper, 32)) {
		ast_hangup(chan);
		return NULL;
	}
	ast_string_field_set(new_wrapper, bridge_id, bridge->uniqueid);
	ast_string_field_set(new_wrapper, channel_id, ast_channel_uniqueid(chan));

	if (!ao2_link_flags(app_bridges_moh, new_wrapper, OBJ_NOLOCK)) {
		ast_hangup(chan);
		return NULL;
	}

	if (ast_pthread_create_detached(&threadid, NULL, moh_channel_thread, chan)) {
		ast_log(LOG_ERROR, "Failed to create channel thread. Abandoning MOH channel creation.\n");
		ao2_unlink_flags(app_bridges_moh, new_wrapper, OBJ_NOLOCK);
		ast_hangup(chan);
		return NULL;
	}

	return chan;
}

struct ast_channel *stasis_app_bridge_moh_channel(struct ast_bridge *bridge)
{
	RAII_VAR(struct stasis_app_bridge_channel_wrapper *, moh_wrapper, NULL, ao2_cleanup);

	{
		SCOPED_AO2LOCK(lock, app_bridges_moh);

		moh_wrapper = ao2_find(app_bridges_moh, bridge->uniqueid, OBJ_SEARCH_KEY | OBJ_NOLOCK);
		if (!moh_wrapper) {
			return bridge_moh_create(bridge);
		}
	}

	return ast_channel_get_by_name(moh_wrapper->channel_id);
}

int stasis_app_bridge_moh_stop(struct ast_bridge *bridge)
{
	RAII_VAR(struct stasis_app_bridge_channel_wrapper *, moh_wrapper, NULL, ao2_cleanup);
	struct ast_channel *chan;

	moh_wrapper = ao2_find(app_bridges_moh, bridge->uniqueid, OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!moh_wrapper) {
		return -1;
	}

	chan = ast_channel_get_by_name(moh_wrapper->channel_id);
	if (!chan) {
		return -1;
	}

	ast_moh_stop(chan);
	ast_softhangup(chan, AST_CAUSE_NORMAL_CLEARING);
	ao2_cleanup(chan);

	return 0;
}

/*! Removes the bridge to playback channel link */
static void remove_bridge_playback(char *bridge_id)
{
	struct stasis_app_bridge_channel_wrapper *wrapper;
	struct stasis_app_control *control;

	wrapper = ao2_find(app_bridges_playback, bridge_id, OBJ_SEARCH_KEY | OBJ_UNLINK);

	if (wrapper) {
		control = stasis_app_control_find_by_channel_id(wrapper->channel_id);
		if (control) {
			ao2_unlink(app_controls, control);
			ao2_ref(control, -1);
		}
		ao2_ref(wrapper, -1);
	}
	ast_free(bridge_id);
}

static void playback_after_bridge_cb_failed(enum ast_bridge_after_cb_reason reason, void *data)
{
	char *bridge_id = data;

	remove_bridge_playback(bridge_id);
}

static void playback_after_bridge_cb(struct ast_channel *chan, void *data)
{
	char *bridge_id = data;

	remove_bridge_playback(bridge_id);
}

int stasis_app_bridge_playback_channel_add(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct stasis_app_control *control)
{
	RAII_VAR(struct stasis_app_bridge_channel_wrapper *, new_wrapper, NULL, ao2_cleanup);
	char *bridge_id = ast_strdup(bridge->uniqueid);

	if (!bridge_id) {
		return -1;
	}

	if (ast_bridge_set_after_callback(chan,
		playback_after_bridge_cb, playback_after_bridge_cb_failed, bridge_id)) {
		ast_free(bridge_id);
		return -1;
	}

	new_wrapper = ao2_alloc_options(sizeof(*new_wrapper),
		stasis_app_bridge_channel_wrapper_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!new_wrapper) {
		return -1;
	}

	if (ast_string_field_init(new_wrapper, 32)) {
		return -1;
	}

	ast_string_field_set(new_wrapper, bridge_id, bridge->uniqueid);
	ast_string_field_set(new_wrapper, channel_id, ast_channel_uniqueid(chan));

	if (!ao2_link(app_bridges_playback, new_wrapper)) {
		return -1;
	}

	ao2_link(app_controls, control);
	return 0;
}

struct ast_channel *stasis_app_bridge_playback_channel_find(struct ast_bridge *bridge)
{
	struct stasis_app_bridge_channel_wrapper *playback_wrapper;
	struct ast_channel *chan;

	playback_wrapper = ao2_find(app_bridges_playback, bridge->uniqueid, OBJ_SEARCH_KEY);
	if (!playback_wrapper) {
		return NULL;
	}

	chan = ast_channel_get_by_name(playback_wrapper->channel_id);
	ao2_ref(playback_wrapper, -1);
	return chan;
}

struct ast_bridge *stasis_app_bridge_find_by_id(
	const char *bridge_id)
{
	return ao2_find(app_bridges, bridge_id, OBJ_SEARCH_KEY);
}


/*!
 * \brief In addition to running ao2_cleanup(), this function also removes the
 * object from the app_controls container.
 */
static void control_unlink(struct stasis_app_control *control)
{
	if (!control) {
		return;
	}

	ao2_unlink(app_controls, control);
	ao2_cleanup(control);
}

struct ast_bridge *stasis_app_bridge_create(const char *type, const char *name, const char *id)
{
	struct ast_bridge *bridge;
	char *requested_type, *requested_types = ast_strdupa(S_OR(type, "mixing"));
	int capabilities = 0;
	int flags = AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM | AST_BRIDGE_FLAG_MERGE_INHIBIT_TO
		| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_SWAP_INHIBIT_TO
		| AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY;

	while ((requested_type = strsep(&requested_types, ","))) {
		requested_type = ast_strip(requested_type);

		if (!strcmp(requested_type, "mixing")) {
			capabilities |= STASIS_BRIDGE_MIXING_CAPABILITIES;
			flags |= AST_BRIDGE_FLAG_SMART;
		} else if (!strcmp(requested_type, "holding")) {
			capabilities |= AST_BRIDGE_CAPABILITY_HOLDING;
		} else if (!strcmp(requested_type, "dtmf_events") ||
			!strcmp(requested_type, "proxy_media")) {
			capabilities &= ~AST_BRIDGE_CAPABILITY_NATIVE;
		}
	}

	if (!capabilities
		/* Holding and mixing capabilities don't mix. */
		|| ((capabilities & AST_BRIDGE_CAPABILITY_HOLDING)
			&& (capabilities & (STASIS_BRIDGE_MIXING_CAPABILITIES)))) {
		return NULL;
	}

	bridge = bridge_stasis_new(capabilities, flags, name, id);
	if (bridge) {
		if (!ao2_link(app_bridges, bridge)) {
			ast_bridge_destroy(bridge, 0);
			bridge = NULL;
		}
	}
	return bridge;
}

void stasis_app_bridge_destroy(const char *bridge_id)
{
	struct ast_bridge *bridge = stasis_app_bridge_find_by_id(bridge_id);
	if (!bridge) {
		return;
	}
	ao2_unlink(app_bridges, bridge);
	ast_bridge_destroy(bridge, 0);
}

struct replace_channel_store {
	struct ast_channel_snapshot *snapshot;
	char *app;
};

static void replace_channel_destroy(void *obj)
{
	struct replace_channel_store *replace = obj;

	ao2_cleanup(replace->snapshot);
	ast_free(replace->app);
	ast_free(replace);
}

static const struct ast_datastore_info replace_channel_store_info = {
	.type = "replace-channel-store",
	.destroy = replace_channel_destroy,
};

static struct replace_channel_store *get_replace_channel_store(struct ast_channel *chan, int no_create)
{
	struct ast_datastore *datastore;

	SCOPED_CHANNELLOCK(lock, chan);
	datastore = ast_channel_datastore_find(chan, &replace_channel_store_info, NULL);
	if (!datastore) {
		if (no_create) {
			return NULL;
		}

		datastore = ast_datastore_alloc(&replace_channel_store_info, NULL);
		if (!datastore) {
			return NULL;
		}
		ast_channel_datastore_add(chan, datastore);
	}

	if (!datastore->data) {
		datastore->data = ast_calloc(1, sizeof(struct replace_channel_store));
	}
	return datastore->data;
}

int app_set_replace_channel_snapshot(struct ast_channel *chan, struct ast_channel_snapshot *replace_snapshot)
{
	struct replace_channel_store *replace = get_replace_channel_store(chan, 0);

	if (!replace) {
		return -1;
	}

	ao2_replace(replace->snapshot, replace_snapshot);
	return 0;
}

int app_set_replace_channel_app(struct ast_channel *chan, const char *replace_app)
{
	struct replace_channel_store *replace = get_replace_channel_store(chan, 0);

	if (!replace) {
		return -1;
	}

	ast_free(replace->app);
	replace->app = NULL;

	if (replace_app) {
		replace->app = ast_strdup(replace_app);
		if (!replace->app) {
			return -1;
		}
	}

	return 0;
}

static struct ast_channel_snapshot *get_replace_channel_snapshot(struct ast_channel *chan)
{
	struct replace_channel_store *replace = get_replace_channel_store(chan, 1);
	struct ast_channel_snapshot *replace_channel_snapshot;

	if (!replace) {
		return NULL;
	}

	replace_channel_snapshot = replace->snapshot;
	replace->snapshot = NULL;

	return replace_channel_snapshot;
}

char *app_get_replace_channel_app(struct ast_channel *chan)
{
	struct replace_channel_store *replace = get_replace_channel_store(chan, 1);
	char *replace_channel_app;

	if (!replace) {
		return NULL;
	}

	replace_channel_app = replace->app;
	replace->app = NULL;

	return replace_channel_app;
}

static int send_start_msg_snapshots(struct stasis_app *app,
	int argc, char *argv[], struct ast_channel_snapshot *snapshot,
	struct ast_channel_snapshot *replace_channel_snapshot)
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	struct ast_json *json_args;
	struct stasis_message_sanitizer *sanitize = stasis_app_get_sanitizer();
	int i;

	if (sanitize && sanitize->channel_snapshot
		&& sanitize->channel_snapshot(snapshot)) {
		return 0;
	}

	msg = ast_json_pack("{s: s, s: o, s: [], s: o}",
		"type", "StasisStart",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"args",
		"channel", ast_channel_snapshot_to_json(snapshot, NULL));
	if (!msg) {
		return -1;
	}

	if (replace_channel_snapshot) {
		int res = ast_json_object_set(msg, "replace_channel",
			ast_channel_snapshot_to_json(replace_channel_snapshot, NULL));

		if (res) {
			return -1;
		}
	}

	/* Append arguments to args array */
	json_args = ast_json_object_get(msg, "args");
	ast_assert(json_args != NULL);
	for (i = 0; i < argc; ++i) {
		int r = ast_json_array_append(json_args,
					      ast_json_string_create(argv[i]));
		if (r != 0) {
			ast_log(LOG_ERROR, "Error appending start message\n");
			return -1;
		}
	}

	app_send(app, msg);
	return 0;
}

static int send_start_msg(struct stasis_app *app, struct ast_channel *chan,
	int argc, char *argv[])
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, replace_channel_snapshot,
		NULL, ao2_cleanup);

	ast_assert(chan != NULL);

	replace_channel_snapshot = get_replace_channel_snapshot(chan);

	/* Set channel info */
	ast_channel_lock(chan);
	snapshot = ast_channel_snapshot_create(chan);
	ast_channel_unlock(chan);
	if (!snapshot) {
		return -1;
	}
	return send_start_msg_snapshots(app, argc, argv, snapshot, replace_channel_snapshot);
}

static int send_end_msg_snapshot(struct stasis_app *app, struct ast_channel_snapshot *snapshot)
{
	struct stasis_message_sanitizer *sanitize = stasis_app_get_sanitizer();
	struct ast_json *msg;

	if (sanitize && sanitize->channel_snapshot
		&& sanitize->channel_snapshot(snapshot)) {
		return 0;
	}

	msg = stasis_end_json_payload(snapshot, sanitize);
	if (!msg) {
		return -1;
	}

	app_send(app, msg);
	ast_json_unref(msg);
	return 0;
}

static void remove_masquerade_store(struct ast_channel *chan);

static int masq_match_cb(void *obj, void *data, int flags)
{
	struct stasis_app_control *control = obj;
	struct ast_channel *chan = data;

	if (!strcmp(ast_channel_uniqueid(chan),
		stasis_app_control_get_channel_id(control))) {
		return CMP_MATCH;
	}

	return 0;
}

static void channel_stolen_cb(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct ast_channel_snapshot *snapshot;
	struct stasis_app_control *control;

	/* grab a snapshot */
	snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(new_chan));
	if (!snapshot) {
		ast_log(LOG_ERROR, "Could not get snapshot for masqueraded channel\n");
		return;
	}

	/* find control */
	control = ao2_callback(app_controls, 0, masq_match_cb, old_chan);
	if (!control) {
		ast_log(LOG_ERROR, "Could not find control for masqueraded channel\n");
		ao2_cleanup(snapshot);
		return;
	}

	/* send the StasisEnd message to the app */
	send_end_msg_snapshot(control_app(control), snapshot);

	/* remove the datastore */
	remove_masquerade_store(old_chan);

	ao2_cleanup(control);
	ao2_cleanup(snapshot);
}

static void channel_replaced_cb(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	RAII_VAR(struct ast_channel_snapshot *, new_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, old_snapshot, NULL, ao2_cleanup);
	struct stasis_app_control *control;

	/* At this point, new_chan is the channel pointer that is in Stasis() and
	 * has the unknown channel's name in it while old_chan is the channel pointer
	 * that is not in Stasis(), but has the guts of the channel that Stasis() knows
	 * about */

	/* grab a snapshot for the channel that is jumping into Stasis() */
	new_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(new_chan));
	if (!new_snapshot) {
		ast_log(LOG_ERROR, "Could not get snapshot for masquerading channel\n");
		return;
	}

	/* grab a snapshot for the channel that has been kicked out of Stasis() */
	old_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(old_chan));
	if (!old_snapshot) {
		ast_log(LOG_ERROR, "Could not get snapshot for masqueraded channel\n");
		return;
	}

	/* find, unlink, and relink control since the channel has a new name and
	 * its hash has likely changed */
	control = ao2_callback(app_controls, OBJ_UNLINK, masq_match_cb, new_chan);
	if (!control) {
		ast_log(LOG_ERROR, "Could not find control for masquerading channel\n");
		return;
	}
	ao2_link(app_controls, control);


	/* send the StasisStart with replace_channel to the app */
	send_start_msg_snapshots(control_app(control), 0, NULL, new_snapshot,
		old_snapshot);
	/* send the StasisEnd message to the app */
	send_end_msg_snapshot(control_app(control), old_snapshot);

	/* fixup channel topic forwards */
	if (app_replace_channel_forwards(control_app(control), old_snapshot->uniqueid, new_chan)) {
		ast_log(LOG_ERROR, "Failed to fixup channel topic forwards for %s(%s) owned by %s\n",
			old_snapshot->name, old_snapshot->uniqueid, app_name(control_app(control)));
	}
	ao2_cleanup(control);
}

static const struct ast_datastore_info masquerade_store_info = {
	.type = "stasis-masqerade",
	.chan_fixup = channel_stolen_cb,
	.chan_breakdown = channel_replaced_cb,
};

static int has_masquerade_store(struct ast_channel *chan)
{
	SCOPED_CHANNELLOCK(lock, chan);
	return !!ast_channel_datastore_find(chan, &masquerade_store_info, NULL);
}

static int add_masquerade_store(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	SCOPED_CHANNELLOCK(lock, chan);
	if (ast_channel_datastore_find(chan, &masquerade_store_info, NULL)) {
		return 0;
	}

	datastore = ast_datastore_alloc(&masquerade_store_info, NULL);
	if (!datastore) {
		return -1;
	}

	ast_channel_datastore_add(chan, datastore);

	return 0;
}

static void remove_masquerade_store(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	SCOPED_CHANNELLOCK(lock, chan);
	datastore = ast_channel_datastore_find(chan, &masquerade_store_info, NULL);
	if (!datastore) {
		return;
	}

	ast_channel_datastore_remove(chan, datastore);
	ast_datastore_free(datastore);
}

static int send_end_msg(struct stasis_app *app, struct ast_channel *chan)
{
	struct ast_channel_snapshot *snapshot;
	int res = 0;

	ast_assert(chan != NULL);

	/* A masquerade has occurred and this message will be wrong so it
	 * has already been sent elsewhere. */
	if (!has_masquerade_store(chan)) {
		return 0;
	}

	/* Set channel info */
	snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan));
	if (!snapshot) {
		return -1;
	}

	if (send_end_msg_snapshot(app, snapshot)) {
		res = -1;
	}

	ao2_cleanup(snapshot);
	return res;
}

void stasis_app_control_execute_until_exhausted(struct ast_channel *chan, struct stasis_app_control *control)
{
	while (!control_is_done(control)) {
		int command_count;
		command_count = control_dispatch_all(control, chan);

		ao2_lock(control);

		if (control_command_count(control)) {
			/* If the command queue isn't empty, something added to the queue before it was locked. */
			ao2_unlock(control);
			continue;
		}

		if (command_count == 0 || ast_channel_fdno(chan) == -1) {
			control_mark_done(control);
			ao2_unlock(control);
			break;
		}
		ao2_unlock(control);
	}
}

int stasis_app_control_is_done(struct stasis_app_control *control)
{
	return control_is_done(control);
}

struct ast_datastore_info set_end_published_info = {
	.type = "stasis_end_published",
};

void stasis_app_channel_set_stasis_end_published(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_datastore_alloc(&set_end_published_info, NULL);

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
}

int stasis_app_channel_is_stasis_end_published(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &set_end_published_info, NULL);
	ast_channel_unlock(chan);

	return datastore ? 1 : 0;
}

static void remove_stasis_end_published(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &set_end_published_info, NULL);
	ast_channel_unlock(chan);

	if (datastore) {
		ast_channel_datastore_remove(chan, datastore);
		ast_datastore_free(datastore);
	}
}

/*! /brief Stasis dialplan application callback */
int stasis_app_exec(struct ast_channel *chan, const char *app_name, int argc,
		    char *argv[])
{
	SCOPED_MODULE_USE(ast_module_info->self);

	RAII_VAR(struct stasis_app *, app, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, control_unlink);
	struct ast_bridge *bridge = NULL;
	int res = 0;

	ast_assert(chan != NULL);

	/* Just in case there's a lingering indication that the channel has had a stasis
	 * end published on it, remove that now.
	 */
	remove_stasis_end_published(chan);

	app = ao2_find(apps_registry, app_name, OBJ_SEARCH_KEY);
	if (!app) {
		ast_log(LOG_ERROR,
			"Stasis app '%s' not registered\n", app_name);
		return -1;
	}
	if (!app_is_active(app)) {
		ast_log(LOG_ERROR,
			"Stasis app '%s' not active\n", app_name);
		return -1;
	}

	control = control_create(chan, app);
	if (!control) {
		ast_log(LOG_ERROR, "Allocated failed\n");
		return -1;
	}
	ao2_link(app_controls, control);

	if (add_masquerade_store(chan)) {
		ast_log(LOG_ERROR, "Failed to attach masquerade detector\n");
		return -1;
	}

	res = send_start_msg(app, chan, argc, argv);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending start message to '%s'\n", app_name);
		remove_masquerade_store(chan);
		return -1;
	}

	res = app_subscribe_channel(app, chan);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error subscribing app '%s' to channel '%s'\n",
			app_name, ast_channel_name(chan));
		remove_masquerade_store(chan);
		return -1;
	}

	/* Pull queued prestart commands and execute */
	control_prestart_dispatch_all(control, chan);

	while (!control_is_done(control)) {
		RAII_VAR(struct ast_frame *, f, NULL, ast_frame_dtor);
		int r;
		int command_count;
		RAII_VAR(struct ast_bridge *, last_bridge, NULL, ao2_cleanup);

		/* Check to see if a bridge absorbed our hangup frame */
		if (ast_check_hangup_locked(chan)) {
			break;
		}

		last_bridge = bridge;
		bridge = ao2_bump(stasis_app_get_bridge(control));

		if (bridge != last_bridge) {
			app_unsubscribe_bridge(app, last_bridge);
			app_subscribe_bridge(app, bridge);
		}

		if (bridge) {
			/* Bridge is handling channel frames */
			control_wait(control);
			control_dispatch_all(control, chan);
			continue;
		}

		r = ast_waitfor(chan, MAX_WAIT_MS);

		if (r < 0) {
			ast_debug(3, "%s: Poll error\n",
				  ast_channel_uniqueid(chan));
			break;
		}

		command_count = control_dispatch_all(control, chan);

		if (command_count > 0 && ast_channel_fdno(chan) == -1) {
			/* Command drained the channel; wait for next frame */
			continue;
		}

		if (r == 0) {
			/* Timeout */
			continue;
		}

		f = ast_read(chan);
		if (!f) {
			/* Continue on in the dialplan */
			ast_debug(3, "%s: Hangup (no more frames)\n",
				ast_channel_uniqueid(chan));
			break;
		}

		if (f->frametype == AST_FRAME_CONTROL) {
			if (f->subclass.integer == AST_CONTROL_HANGUP) {
				/* Continue on in the dialplan */
				ast_debug(3, "%s: Hangup\n",
					ast_channel_uniqueid(chan));
				break;
			}
		}
	}

	app_unsubscribe_bridge(app, stasis_app_get_bridge(control));
	ao2_cleanup(bridge);

	/* Only publish a stasis_end event if it hasn't already been published */
	if (!stasis_app_channel_is_stasis_end_published(chan)) {
		app_unsubscribe_channel(app, chan);
		res = send_end_msg(app, chan);
		remove_masquerade_store(chan);
		if (res != 0) {
			ast_log(LOG_ERROR,
				"Error sending end message to %s\n", app_name);
			return res;
		}
	} else {
		remove_stasis_end_published(chan);
	}

	/* There's an off chance that app is ready for cleanup. Go ahead
	 * and clean up, just in case
	 */
	cleanup();

	/* The control needs to be removed from the controls container in
	 * case a new PBX is started and ends up coming back into Stasis.
	 */
	ao2_cleanup(app);
	app = NULL;
	control_unlink(control);
	control = NULL;

	if (!ast_check_hangup_locked(chan) && !ast_channel_pbx(chan)) {
		struct ast_pbx_args pbx_args = {
			.no_hangup_chan = 1,
		};

		res = ast_pbx_run_args(chan, &pbx_args);
	}

	return res;
}

int stasis_app_send(const char *app_name, struct ast_json *message)
{
	RAII_VAR(struct stasis_app *, app, NULL, ao2_cleanup);

	app = ao2_find(apps_registry, app_name, OBJ_SEARCH_KEY);
	if (!app) {
		/* XXX We can do a better job handling late binding, queueing up
		 * the call for a few seconds to wait for the app to register.
		 */
		ast_log(LOG_WARNING,
			"Stasis app '%s' not registered\n", app_name);
		return -1;
	}
	app_send(app, message);
	return 0;
}

static struct stasis_app *find_app_by_name(const char *app_name)
{
	struct stasis_app *res = NULL;

	if (!ast_strlen_zero(app_name)) {
		res = ao2_find(apps_registry, app_name, OBJ_SEARCH_KEY);
	}

	if (!res) {
		ast_log(LOG_WARNING, "Could not find app '%s'\n",
			app_name ? : "(null)");
	}
	return res;
}

static int append_name(void *obj, void *arg, int flags)
{
	struct stasis_app *app = obj;
	struct ao2_container *apps = arg;

	ast_str_container_add(apps, stasis_app_name(app));
	return 0;
}

struct ao2_container *stasis_app_get_all(void)
{
	RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);

	apps = ast_str_container_alloc(1);
	if (!apps) {
		return NULL;
	}

	ao2_callback(apps_registry, OBJ_NODATA, append_name, apps);

	return ao2_bump(apps);
}

int stasis_app_register(const char *app_name, stasis_app_cb handler, void *data)
{
	RAII_VAR(struct stasis_app *, app, NULL, ao2_cleanup);

	SCOPED_LOCK(apps_lock, apps_registry, ao2_lock, ao2_unlock);

	app = ao2_find(apps_registry, app_name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (app) {
		app_update(app, handler, data);
	} else {
		app = app_create(app_name, handler, data);
		if (app) {
			ao2_link_flags(apps_registry, app, OBJ_NOLOCK);
		} else {
			return -1;
		}
	}

	/* We lazily clean up the apps_registry, because it's good enough to
	 * prevent memory leaks, and we're lazy.
	 */
	cleanup();
	return 0;
}

void stasis_app_unregister(const char *app_name)
{
	RAII_VAR(struct stasis_app *, app, NULL, ao2_cleanup);

	if (!app_name) {
		return;
	}

	app = ao2_find(apps_registry, app_name, OBJ_SEARCH_KEY);
	if (!app) {
		ast_log(LOG_ERROR,
			"Stasis app '%s' not registered\n", app_name);
		return;
	}

	app_deactivate(app);

	/* There's a decent chance that app is ready for cleanup. Go ahead
	 * and clean up, just in case
	 */
	cleanup();
}

/*!
 * \internal \brief List of registered event sources.
 */
AST_RWLIST_HEAD_STATIC(event_sources, stasis_app_event_source);

void stasis_app_register_event_source(struct stasis_app_event_source *obj)
{
	SCOPED_LOCK(lock, &event_sources, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_LIST_INSERT_TAIL(&event_sources, obj, next);
	/* only need to bump the module ref on non-core sources because the
	   core ones are [un]registered by this module. */
	if (!stasis_app_is_core_event_source(obj)) {
		ast_module_ref(ast_module_info->self);
	}
}

void stasis_app_unregister_event_source(struct stasis_app_event_source *obj)
{
	struct stasis_app_event_source *source;
	SCOPED_LOCK(lock, &event_sources, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&event_sources, source, next) {
		if (source == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			if (!stasis_app_is_core_event_source(obj)) {
				ast_module_unref(ast_module_info->self);
			}
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

/*!
 * \internal
 * \brief Convert event source data to JSON.
 *
 * Calls each event source that has a "to_json" handler allowing each
 * source to add data to the given JSON object.
 *
 * \param app application associated with the event source
 * \param json a json object to "fill"
 *
 * \retval The given json object.
 */
static struct ast_json *app_event_sources_to_json(
	const struct stasis_app *app, struct ast_json *json)
{
	struct stasis_app_event_source *source;
	SCOPED_LOCK(lock, &event_sources, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_LIST_TRAVERSE(&event_sources, source, next) {
		if (source->to_json) {
			source->to_json(app, json);
		}
	}
	return json;
}

static struct ast_json *stasis_app_object_to_json(struct stasis_app *app)
{
	if (!app) {
		return NULL;
	}

	return app_event_sources_to_json(app, app_to_json(app));
}

struct ast_json *stasis_app_to_json(const char *app_name)
{
	RAII_VAR(struct stasis_app *, app, find_app_by_name(app_name), ao2_cleanup);

	return stasis_app_object_to_json(app);
}

/*!
 * \internal
 * \brief Finds an event source that matches a uri scheme.
 *
 * Uri(s) should begin with a particular scheme that can be matched
 * against an event source.
 *
 * \param uri uri containing a scheme to match
 *
 * \retval an event source if found, NULL otherwise.
 */
static struct stasis_app_event_source *app_event_source_find(const char *uri)
{
	struct stasis_app_event_source *source;
	SCOPED_LOCK(lock, &event_sources, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_LIST_TRAVERSE(&event_sources, source, next) {
		if (ast_begins_with(uri, source->scheme)) {
			return source;
		}
	}
	return NULL;
}

/*!
 * \internal
 * \brief Callback for subscription handling
 *
 * \param app [un]subscribing application
 * \param uri scheme:id of an event source
 * \param event_source being [un]subscribed [from]to
 *
 * \retval stasis_app_subscribe_res return code.
 */
typedef enum stasis_app_subscribe_res (*app_subscription_handler)(
	struct stasis_app *app, const char *uri,
	struct stasis_app_event_source *event_source);

/*!
 * \internal
 * \brief Subscriptions handler for application [un]subscribing.
 *
 * \param app_name Name of the application to subscribe.
 * \param event_source_uris URIs for the event sources to subscribe to.
 * \param event_sources_count Array size of event_source_uris.
 * \param json Optional output pointer for JSON representation of the app
 *             after adding the subscription.
 * \param handler [un]subscribe handler
 *
 * \retval stasis_app_subscribe_res return code.
 */
static enum stasis_app_subscribe_res app_handle_subscriptions(
	const char *app_name, const char **event_source_uris,
	int event_sources_count, struct ast_json **json,
	app_subscription_handler handler)
{
	RAII_VAR(struct stasis_app *, app, find_app_by_name(app_name), ao2_cleanup);
	int i;

	if (!app) {
		return STASIS_ASR_APP_NOT_FOUND;
	}

	for (i = 0; i < event_sources_count; ++i) {
		const char *uri = event_source_uris[i];
		enum stasis_app_subscribe_res res = STASIS_ASR_INTERNAL_ERROR;
		struct stasis_app_event_source *event_source;

		if (!(event_source = app_event_source_find(uri))) {
			ast_log(LOG_WARNING, "Invalid scheme: %s\n", uri);
			return STASIS_ASR_EVENT_SOURCE_BAD_SCHEME;
		}

		if (handler &&
		    ((res = handler(app, uri, event_source)))) {
			return res;
		}
	}

	if (json) {
		ast_debug(3, "%s: Successful; setting results\n", app_name);
		*json = stasis_app_object_to_json(app);
	}

	return STASIS_ASR_OK;
}

enum stasis_app_subscribe_res stasis_app_subscribe_channel(const char *app_name,
	struct ast_channel *chan)
{
	RAII_VAR(struct stasis_app *, app, find_app_by_name(app_name), ao2_cleanup);
	int res;

	if (!app) {
		return STASIS_ASR_APP_NOT_FOUND;
	}

	ast_debug(3, "%s: Subscribing to %s\n", app_name, ast_channel_uniqueid(chan));

	res = app_subscribe_channel(app, chan);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error subscribing app '%s' to channel '%s'\n",
			app_name, ast_channel_uniqueid(chan));
		return STASIS_ASR_INTERNAL_ERROR;
	}

	return STASIS_ASR_OK;
}


/*!
 * \internal
 * \brief Subscribe an app to an event source.
 *
 * \param app subscribing application
 * \param uri scheme:id of an event source
 * \param event_source being subscribed to
 *
 * \retval stasis_app_subscribe_res return code.
 */
static enum stasis_app_subscribe_res app_subscribe(
	struct stasis_app *app, const char *uri,
	struct stasis_app_event_source *event_source)
{
	const char *app_name = stasis_app_name(app);
	RAII_VAR(void *, obj, NULL, ao2_cleanup);

	ast_debug(3, "%s: Checking %s\n", app_name, uri);

	if (!event_source->find ||
	    (!(obj = event_source->find(app, uri + strlen(event_source->scheme))))) {
		ast_log(LOG_WARNING, "Event source not found: %s\n", uri);
		return STASIS_ASR_EVENT_SOURCE_NOT_FOUND;
	}

	ast_debug(3, "%s: Subscribing to %s\n", app_name, uri);

	if (!event_source->subscribe || (event_source->subscribe(app, obj))) {
		ast_log(LOG_WARNING, "Error subscribing app '%s' to '%s'\n",
			app_name, uri);
		return STASIS_ASR_INTERNAL_ERROR;
	}

	return STASIS_ASR_OK;
}

enum stasis_app_subscribe_res stasis_app_subscribe(const char *app_name,
	const char **event_source_uris, int event_sources_count,
	struct ast_json **json)
{
	return app_handle_subscriptions(
		app_name, event_source_uris, event_sources_count,
		json, app_subscribe);
}

/*!
 * \internal
 * \brief Unsubscribe an app from an event source.
 *
 * \param app application to unsubscribe
 * \param uri scheme:id of an event source
 * \param event_source being unsubscribed from
 *
 * \retval stasis_app_subscribe_res return code.
 */
static enum stasis_app_subscribe_res app_unsubscribe(
	struct stasis_app *app, const char *uri,
	struct stasis_app_event_source *event_source)
{
	const char *app_name = stasis_app_name(app);
	const char *id = uri + strlen(event_source->scheme);

	if (!event_source->is_subscribed ||
	    (!event_source->is_subscribed(app, id))) {
		return STASIS_ASR_EVENT_SOURCE_NOT_FOUND;
	}

	ast_debug(3, "%s: Unsubscribing from %s\n", app_name, uri);

	if (!event_source->unsubscribe || (event_source->unsubscribe(app, id))) {
		ast_log(LOG_WARNING, "Error unsubscribing app '%s' to '%s'\n",
			app_name, uri);
		return -1;
	}
	return 0;
}

enum stasis_app_subscribe_res stasis_app_unsubscribe(const char *app_name,
	const char **event_source_uris, int event_sources_count,
	struct ast_json **json)
{
	return app_handle_subscriptions(
		app_name, event_source_uris, event_sources_count,
		json, app_unsubscribe);
}

enum stasis_app_user_event_res stasis_app_user_event(const char *app_name,
	const char *event_name,
	const char **source_uris, int sources_count,
	struct ast_json *json_variables)
{
	RAII_VAR(struct stasis_app *, app, find_app_by_name(app_name), ao2_cleanup);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	RAII_VAR(struct ast_multi_object_blob *, multi, NULL, ao2_cleanup);
	RAII_VAR(void *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	enum stasis_app_subscribe_res res = STASIS_APP_USER_INTERNAL_ERROR;
	struct ast_json *json_value;
	int have_channel = 0;
	int i;

	if (!app) {
		ast_log(LOG_WARNING, "App %s not found\n", app_name);
		return STASIS_APP_USER_APP_NOT_FOUND;
	}

	if (!ast_multi_user_event_type()) {
		return res;
	}

	blob = json_variables;
	if (!blob) {
		blob = ast_json_pack("{}");
	}
	json_value = ast_json_string_create(event_name);
	if (!json_value) {
		ast_log(LOG_ERROR, "unable to create json string\n");
		return res;
	}
	if (ast_json_object_set(blob, "eventname", json_value)) {
		ast_log(LOG_ERROR, "unable to set eventname to blob\n");
		return res;
	}

	multi = ast_multi_object_blob_create(blob);

	for (i = 0; i < sources_count; ++i) {
		const char *uri = source_uris[i];
		void *snapshot=NULL;
		enum stasis_user_multi_object_snapshot_type type;

		if (ast_begins_with(uri, "channel:")) {
			type = STASIS_UMOS_CHANNEL;
			snapshot = ast_channel_snapshot_get_latest(uri + 8);
			have_channel = 1;
		} else if (ast_begins_with(uri, "bridge:")) {
			type = STASIS_UMOS_BRIDGE;
			snapshot = ast_bridge_snapshot_get_latest(uri + 7);
		} else if (ast_begins_with(uri, "endpoint:")) {
			type = STASIS_UMOS_ENDPOINT;
			snapshot = ast_endpoint_latest_snapshot(uri + 9, NULL);
		} else {
			ast_log(LOG_WARNING, "Invalid scheme: %s\n", uri);
			return STASIS_APP_USER_EVENT_SOURCE_BAD_SCHEME;
		}
		if (!snapshot) {
			ast_log(LOG_ERROR, "Unable to get snapshot for %s\n", uri);
			return STASIS_APP_USER_EVENT_SOURCE_NOT_FOUND;
		}
		ast_multi_object_blob_add(multi, type, snapshot);
	}

	message = stasis_message_create(ast_multi_user_event_type(), multi);
	if (!message) {
		ast_log(LOG_ERROR, "Unable to create stasis user event message\n");
		return res;
	}

	/*
	 * Publishing to two different topics is normally to be avoided -- except
	 * in this case both are final destinations with no forwards (only listeners).
	 * The message has to be delivered to the application topic for ARI, but a
	 * copy is also delivered directly to the manager for AMI if there is a channel.
	 */
	stasis_publish(ast_app_get_topic(app), message);

	if (have_channel) {
		stasis_publish(ast_manager_get_topic(), message);
	}

	return STASIS_APP_USER_OK;
}

void stasis_app_ref(void)
{
	ast_module_ref(ast_module_info->self);
}

void stasis_app_unref(void)
{
	ast_module_unref(ast_module_info->self);
}

/*!
 * \brief Subscription to StasisEnd events
 */
struct stasis_subscription *stasis_end_sub;

static int unload_module(void)
{
	stasis_end_sub = stasis_unsubscribe(stasis_end_sub);

	stasis_app_unregister_event_sources();

	messaging_cleanup();

	ao2_cleanup(apps_registry);
	apps_registry = NULL;

	ao2_cleanup(app_controls);
	app_controls = NULL;

	ao2_cleanup(app_bridges);
	app_bridges = NULL;

	ao2_cleanup(app_bridges_moh);
	app_bridges_moh = NULL;

	ao2_cleanup(app_bridges_playback);
	app_bridges_playback = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(ast_stasis_end_message_type);

	return 0;
}

/* \brief Sanitization callback for channel snapshots */
static int channel_snapshot_sanitizer(const struct ast_channel_snapshot *snapshot)
{
	if (!snapshot || !(snapshot->tech_properties & AST_CHAN_TP_INTERNAL)) {
		return 0;
	}
	return 1;
}

/* \brief Sanitization callback for channel unique IDs */
static int channel_id_sanitizer(const char *id)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, ast_channel_snapshot_get_latest(id), ao2_cleanup);

	return channel_snapshot_sanitizer(snapshot);
}

/* \brief Sanitization callbacks for communication to Stasis applications */
struct stasis_message_sanitizer app_sanitizer = {
	.channel_id = channel_id_sanitizer,
	.channel_snapshot = channel_snapshot_sanitizer,
};

struct stasis_message_sanitizer *stasis_app_get_sanitizer(void)
{
	return &app_sanitizer;
}

static void remove_masquerade_store_by_name(const char *channel_name)
{
	struct ast_channel *chan;

	chan = ast_channel_get_by_name(channel_name);
	if (!chan) {
		return;
	}

	remove_masquerade_store(chan);
	ast_channel_unref(chan);
}

static void check_for_stasis_end(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_channel_blob *payload;
	struct ast_channel_snapshot *snapshot;
	const char *app_name;
	char *channel_uri;
	size_t alloc_size;
	const char *channels[1];

	if (stasis_message_type(message) != ast_stasis_end_message_type()) {
		return;
	}

	payload = stasis_message_data(message);
	snapshot = payload->snapshot;
	app_name = ast_json_string_get(ast_json_object_get(payload->blob, "app"));

	/* +8 is for the length of "channel:" */
	alloc_size = AST_MAX_UNIQUEID + 8;
	channel_uri = ast_alloca(alloc_size);
	snprintf(channel_uri, alloc_size, "channel:%s", snapshot->uniqueid);

	channels[0] = channel_uri;
	stasis_app_unsubscribe(app_name, channels, ARRAY_LEN(channels), NULL);

	remove_masquerade_store_by_name(snapshot->name);
}

static const struct ast_datastore_info stasis_internal_channel_info = {
	.type = "stasis-internal-channel",
};

static int set_internal_datastore(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &stasis_internal_channel_info, NULL);
	if (!datastore) {
		datastore = ast_datastore_alloc(&stasis_internal_channel_info, NULL);
		if (!datastore) {
			return -1;
		}
		ast_channel_datastore_add(chan, datastore);
	}
	return 0;
}

int stasis_app_channel_unreal_set_internal(struct ast_channel *chan)
{
	struct ast_channel *outchan = NULL, *outowner = NULL;
	int res = 0;
	struct ast_unreal_pvt *unreal_pvt = ast_channel_tech_pvt(chan);

	ao2_ref(unreal_pvt, +1);
	ast_unreal_lock_all(unreal_pvt, &outowner, &outchan);
	if (outowner) {
		res |= set_internal_datastore(outowner);
		ast_channel_unlock(outowner);
		ast_channel_unref(outowner);
	}
	if (outchan) {
		res |= set_internal_datastore(outchan);
		ast_channel_unlock(outchan);
		ast_channel_unref(outchan);
	}
	ao2_unlock(unreal_pvt);
	ao2_ref(unreal_pvt, -1);
	return res;
}

int stasis_app_channel_set_internal(struct ast_channel *chan)
{
	int res;

	ast_channel_lock(chan);
	res = set_internal_datastore(chan);
	ast_channel_unlock(chan);

	return res;
}

int stasis_app_channel_is_internal(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	int res = 0;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &stasis_internal_channel_info, NULL);
	if (datastore) {
		res = 1;
	}
	ast_channel_unlock(chan);

	return res;
}

static int load_module(void)
{
	if (STASIS_MESSAGE_TYPE_INIT(ast_stasis_end_message_type) != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}
	apps_registry = ao2_container_alloc(APPS_NUM_BUCKETS, app_hash, app_compare);
	app_controls = ao2_container_alloc(CONTROLS_NUM_BUCKETS, control_hash, control_compare);
	app_bridges = ao2_container_alloc(BRIDGES_NUM_BUCKETS, bridges_hash, bridges_compare);
	app_bridges_moh = ao2_container_alloc_hash(
		AO2_ALLOC_OPT_LOCK_MUTEX, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		37, bridges_channel_hash_fn, bridges_channel_sort_fn, NULL);
	app_bridges_playback = ao2_container_alloc_hash(
		AO2_ALLOC_OPT_LOCK_MUTEX, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		37, bridges_channel_hash_fn, bridges_channel_sort_fn, NULL);
	if (!apps_registry || !app_controls || !app_bridges || !app_bridges_moh || !app_bridges_playback) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	if (messaging_init()) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	bridge_stasis_init();

	stasis_app_register_event_sources();

	stasis_end_sub = stasis_subscribe(ast_channel_topic_all(), check_for_stasis_end, NULL);
	if (!stasis_end_sub) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	);
