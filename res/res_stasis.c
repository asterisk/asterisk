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
#include "asterisk/stasis_message_router.h"
#include "asterisk/strings.h"
#include "stasis/app.h"
#include "stasis/control.h"
#include "asterisk/core_unreal.h"
#include "asterisk/musiconhold.h"
#include "asterisk/causes.h"
#include "asterisk/stringfields.h"
#include "asterisk/bridge_after.h"

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
	return control_create(chan);
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
 *  Used with app_bridges_moh, provides links between bridges and existing music
 *  on hold channels that are being used with them.
 */
struct stasis_app_bridge_moh_wrapper {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(channel_id);
		AST_STRING_FIELD(bridge_id);
	);
};

static void stasis_app_bridge_moh_wrapper_destructor(void *obj)
{
	struct stasis_app_bridge_moh_wrapper *wrapper = obj;
	ast_string_field_free_memory(wrapper);
}

/*! AO2 hash function for the bridges moh container */
static int bridges_moh_hash_fn(const void *obj, const int flags)
{
	const struct stasis_app_bridge_moh_wrapper *wrapper;
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

static int bridges_moh_sort_fn(const void *obj_left, const void *obj_right, const int flags)
{
	const struct stasis_app_bridge_moh_wrapper *left = obj_left;
	const struct stasis_app_bridge_moh_wrapper *right = obj_right;
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
	RAII_VAR(struct ast_format_cap *, cap, NULL, ast_format_cap_destroy);
	struct ast_format format;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK);
	if (!cap) {
		return NULL;
	}

	ast_format_cap_add(cap, ast_format_set(&format, AST_FORMAT_SLINEAR, 0));

	return ast_request("Announcer", cap, NULL, "ARI_MOH", NULL);
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
	RAII_VAR(struct stasis_app_bridge_moh_wrapper *, new_wrapper, NULL, ao2_cleanup);
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
		stasis_app_bridge_moh_wrapper_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
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
	RAII_VAR(struct stasis_app_bridge_moh_wrapper *, moh_wrapper, NULL, ao2_cleanup);

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
	RAII_VAR(struct stasis_app_bridge_moh_wrapper *, moh_wrapper, NULL, ao2_cleanup);
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

struct ast_bridge *stasis_app_bridge_create(const char *type, const char *name)
{
	struct ast_bridge *bridge;
	int capabilities;
	int flags = AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM | AST_BRIDGE_FLAG_MERGE_INHIBIT_TO
		| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_SWAP_INHIBIT_TO
		| AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY;

	if (ast_strlen_zero(type) || !strcmp(type, "mixing")) {
		capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX |
			AST_BRIDGE_CAPABILITY_MULTIMIX |
			AST_BRIDGE_CAPABILITY_NATIVE;
		flags |= AST_BRIDGE_FLAG_SMART;
	} else if (!strcmp(type, "holding")) {
		capabilities = AST_BRIDGE_CAPABILITY_HOLDING;
	} else {
		return NULL;
	}

	bridge = ast_bridge_base_new(capabilities, flags, "Stasis", name);
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

static int send_start_msg(struct stasis_app *app, struct ast_channel *chan,
	int argc, char *argv[])
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	struct ast_json *json_args;
	int i;
	struct stasis_message_sanitizer *sanitize = stasis_app_get_sanitizer();

	ast_assert(chan != NULL);

	/* Set channel info */
	ast_channel_lock(chan);
	snapshot = ast_channel_snapshot_create(chan);
	ast_channel_unlock(chan);
	if (!snapshot) {
		return -1;
	}

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

static int send_end_msg(struct stasis_app *app, struct ast_channel *chan)
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	struct stasis_message_sanitizer *sanitize = stasis_app_get_sanitizer();

	ast_assert(chan != NULL);

	/* Set channel info */
	ast_channel_lock(chan);
	snapshot = ast_channel_snapshot_create(chan);
	ast_channel_unlock(chan);
	if (snapshot == NULL) {
		return -1;
	}

	if (sanitize && sanitize->channel_snapshot
		&& sanitize->channel_snapshot(snapshot)) {
		return 0;
	}

	msg = ast_json_pack("{s: s, s: o, s: o}",
		"type", "StasisEnd",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"channel", ast_channel_snapshot_to_json(snapshot, NULL));
	if (!msg) {
		return -1;
	}

	app_send(app, msg);
	return 0;
}

void stasis_app_control_execute_until_exhausted(struct ast_channel *chan, struct stasis_app_control *control)
{
	while (!control_is_done(control)) {
		int command_count = control_dispatch_all(control, chan);
		if (command_count == 0 || ast_channel_fdno(chan) == -1) {
			break;
		}
	}
}

/*! /brief Stasis dialplan application callback */
int stasis_app_exec(struct ast_channel *chan, const char *app_name, int argc,
		    char *argv[])
{
	SCOPED_MODULE_USE(ast_module_info->self);

	RAII_VAR(struct stasis_app *, app, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, control_unlink);
	struct ast_bridge *last_bridge = NULL;
	int res = 0;

	ast_assert(chan != NULL);

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

	control = control_create(chan);
	if (!control) {
		ast_log(LOG_ERROR, "Allocated failed\n");
		return -1;
	}
	ao2_link(app_controls, control);

	res = send_start_msg(app, chan, argc, argv);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending start message to '%s'\n", app_name);
		return -1;
	}

	res = app_subscribe_channel(app, chan);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error subscribing app '%s' to channel '%s'\n",
			app_name, ast_channel_name(chan));
		return -1;
	}

	while (!control_is_done(control)) {
		RAII_VAR(struct ast_frame *, f, NULL, ast_frame_dtor);
		int r;
		int command_count;
		struct ast_bridge *bridge = NULL;

		/* Check to see if a bridge absorbed our hangup frame */
		if (ast_check_hangup_locked(chan)) {
			break;
		}

		last_bridge = bridge;
		bridge = stasis_app_get_bridge(control);

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
	app_unsubscribe_channel(app, chan);

	res = send_end_msg(app, chan);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending end message to %s\n", app_name);
		return res;
	}

	/* There's an off chance that app is ready for cleanup. Go ahead
	 * and clean up, just in case
	 */
	cleanup();

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

void stasis_app_ref(void)
{
	ast_module_ref(ast_module_info->self);
}

void stasis_app_unref(void)
{
	ast_module_unref(ast_module_info->self);
}

static int unload_module(void)
{
	stasis_app_unregister_event_sources();

	ao2_cleanup(apps_registry);
	apps_registry = NULL;

	ao2_cleanup(app_controls);
	app_controls = NULL;

	ao2_cleanup(app_bridges);
	app_bridges = NULL;

	ao2_cleanup(app_bridges_moh);
	app_bridges_moh = NULL;

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

static int load_module(void)
{
	apps_registry = ao2_container_alloc(APPS_NUM_BUCKETS, app_hash, app_compare);
	app_controls = ao2_container_alloc(CONTROLS_NUM_BUCKETS, control_hash, control_compare);
	app_bridges = ao2_container_alloc(BRIDGES_NUM_BUCKETS, bridges_hash, bridges_compare);
	app_bridges_moh = ao2_container_alloc_hash(
		AO2_ALLOC_OPT_LOCK_MUTEX, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		37, bridges_moh_hash_fn, bridges_moh_sort_fn, NULL);
	if (!apps_registry || !app_controls || !app_bridges || !app_bridges_moh) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	stasis_app_register_event_sources();

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application support",
	.load = load_module,
	.unload = unload_module,
	);
