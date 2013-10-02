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

/*! AO2 hash function for \ref app */
static int app_hash(const void *obj, const int flags)
{
	const struct app *app = obj;
	const char *name = flags & OBJ_KEY ? obj : app_name(app);

	return ast_str_hash(name);
}

/*! AO2 comparison function for \ref app */
static int app_compare(void *lhs, void *rhs, int flags)
{
	const struct app *lhs_app = lhs;
	const struct app *rhs_app = rhs;
	const char *lhs_name = app_name(lhs_app);
	const char *rhs_name = flags & OBJ_KEY ? rhs : app_name(rhs_app);

	if (strcmp(lhs_name, rhs_name) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
}

/*! AO2 hash function for \ref stasis_app_control */
static int control_hash(const void *obj, const int flags)
{
	const struct stasis_app_control *control = obj;
	const char *id = flags & OBJ_KEY ?
		obj : stasis_app_control_get_channel_id(control);

	return ast_str_hash(id);
}

/*! AO2 comparison function for \ref stasis_app_control */
static int control_compare(void *lhs, void *rhs, int flags)
{
	const struct stasis_app_control *lhs_control = lhs;
	const struct stasis_app_control *rhs_control = rhs;
	const char *lhs_id = stasis_app_control_get_channel_id(lhs_control);
	const char *rhs_id = flags & OBJ_KEY ?
		rhs : stasis_app_control_get_channel_id(rhs_control);

	if (strcmp(lhs_id, rhs_id) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
}

static int cleanup_cb(void *obj, void *arg, int flags)
{
	struct app *app = obj;

	if (!app_is_finished(app)) {
		return 0;
	}

	ast_verb(1, "Shutting down application '%s'\n", app_name(app));
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
	return ao2_find(app_controls, channel_id, OBJ_KEY);
}

/*! AO2 hash function for bridges container  */
static int bridges_hash(const void *obj, const int flags)
{
	const struct ast_bridge *bridge = obj;
	const char *id = flags & OBJ_KEY ?
		obj : bridge->uniqueid;

	return ast_str_hash(id);
}

/*! AO2 comparison function for bridges container */
static int bridges_compare(void *lhs, void *rhs, int flags)
{
	const struct ast_bridge *lhs_bridge = lhs;
	const struct ast_bridge *rhs_bridge = rhs;
	const char *lhs_id = lhs_bridge->uniqueid;
	const char *rhs_id = flags & OBJ_KEY ?
		rhs : rhs_bridge->uniqueid;

	if (strcmp(lhs_id, rhs_id) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
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

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_POINTER:
		wrapper = obj;
		return ast_str_hash(wrapper->bridge_id);
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int bridges_moh_sort_fn(const void *obj_left, const void *obj_right, const int flags)
{
	const struct stasis_app_bridge_moh_wrapper *left = obj_left;
	const struct stasis_app_bridge_moh_wrapper *right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_POINTER:
		right_key = right->bridge_id;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(left->bridge_id, right_key);
		break;
	case OBJ_PARTIAL_KEY:
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
	RAII_VAR(struct stasis_app_bridge_moh_wrapper *, moh_wrapper, ao2_find(app_bridges_moh, bridge_id, OBJ_KEY), ao2_cleanup);

	if (moh_wrapper) {
		ao2_unlink_flags(app_bridges_moh, moh_wrapper, OBJ_NOLOCK);
	}
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

	while (!ast_safe_sleep(moh_channel, 1000));

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
	ast_bridge_set_after_callback(chan, moh_after_bridge_cb, moh_after_bridge_cb_failed, bridge_id);

	bridge_id = NULL;

	if (ast_unreal_channel_push_to_bridge(chan, bridge,
		AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE | AST_BRIDGE_CHANNEL_FLAG_LONELY)) {
		ast_hangup(chan);
		return NULL;
	}

	new_wrapper = ao2_alloc_options(sizeof(*new_wrapper), stasis_app_bridge_moh_wrapper_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
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

	if (!ao2_link(app_bridges_moh, new_wrapper)) {
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

	SCOPED_AO2LOCK(lock, app_bridges_moh);

	moh_wrapper = ao2_find(app_bridges_moh, bridge->uniqueid, OBJ_KEY | OBJ_NOLOCK);

	if (!moh_wrapper) {
		struct ast_channel *bridge_moh_channel = bridge_moh_create(bridge);
		return bridge_moh_channel;
	}

	return ast_channel_get_by_name(moh_wrapper->channel_id);
}

int stasis_app_bridge_moh_stop(struct ast_bridge *bridge)
{
	RAII_VAR(struct stasis_app_bridge_moh_wrapper *, moh_wrapper, NULL, ao2_cleanup);
	struct ast_channel *chan;

	SCOPED_AO2LOCK(lock, app_bridges_moh);

	moh_wrapper = ao2_find(app_bridges_moh, bridge->uniqueid, OBJ_KEY | OBJ_NOLOCK);

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

	ao2_unlink_flags(app_bridges_moh, moh_wrapper, OBJ_NOLOCK);

	return 0;
}

struct ast_bridge *stasis_app_bridge_find_by_id(
	const char *bridge_id)
{
	return ao2_find(app_bridges, bridge_id, OBJ_KEY);
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

	ao2_unlink_flags(app_controls, control,
		OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
	ao2_cleanup(control);
}

struct ast_bridge *stasis_app_bridge_create(const char *type)
{
	struct ast_bridge *bridge;
	int capabilities, flags = AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM | AST_BRIDGE_FLAG_MERGE_INHIBIT_TO
		| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_SWAP_INHIBIT_TO
		| AST_BRIDGE_FLAG_TRANSFER_PROHIBITED;

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

	bridge = ast_bridge_base_new(capabilities, flags);
	if (bridge) {
		ao2_link(app_bridges, bridge);
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

static int send_start_msg(struct app *app, struct ast_channel *chan,
	int argc, char *argv[])
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	struct ast_json *json_args;
	int i;

	ast_assert(chan != NULL);

	/* Set channel info */
	snapshot = ast_channel_snapshot_create(chan);
	if (!snapshot) {
		return -1;
	}

	msg = ast_json_pack("{s: s, s: o, s: [], s: o}",
		"type", "StasisStart",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"args",
		"channel", ast_channel_snapshot_to_json(snapshot));
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

static int send_end_msg(struct app *app, struct ast_channel *chan)
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	ast_assert(chan != NULL);

	/* Set channel info */
	snapshot = ast_channel_snapshot_create(chan);
	if (snapshot == NULL) {
		return -1;
	}

	msg = ast_json_pack("{s: s, s: o, s: o}",
		"type", "StasisEnd",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"channel", ast_channel_snapshot_to_json(snapshot));
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

	RAII_VAR(struct app *, app, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, control_unlink);
	int res = 0;

	ast_assert(chan != NULL);

	app = ao2_find(apps_registry, app_name, OBJ_KEY);
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
		struct ast_bridge *last_bridge = NULL;
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
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);

	app = ao2_find(apps_registry, app_name, OBJ_KEY);

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

int stasis_app_register(const char *app_name, stasis_app_cb handler, void *data)
{
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);

	SCOPED_LOCK(apps_lock, apps_registry, ao2_lock, ao2_unlock);

	app = ao2_find(apps_registry, app_name, OBJ_KEY | OBJ_NOLOCK);

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
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);

	if (!app_name) {
		return;
	}

	app = ao2_find(apps_registry, app_name, OBJ_KEY);
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

void stasis_app_ref(void)
{
	ast_module_ref(ast_module_info->self);
}

void stasis_app_unref(void)
{
	ast_module_unref(ast_module_info->self);
}

static int load_module(void)
{
	apps_registry =	ao2_container_alloc(APPS_NUM_BUCKETS, app_hash,
		app_compare);
	if (apps_registry == NULL) {
		return AST_MODULE_LOAD_FAILURE;
	}

	app_controls = ao2_container_alloc(CONTROLS_NUM_BUCKETS, control_hash,
		control_compare);
	if (app_controls == NULL) {
		return AST_MODULE_LOAD_FAILURE;
	}

        app_bridges = ao2_container_alloc(BRIDGES_NUM_BUCKETS, bridges_hash,
		bridges_compare);

	app_bridges_moh = ao2_container_alloc_hash(
		AO2_ALLOC_OPT_LOCK_MUTEX, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		37, bridges_moh_hash_fn, bridges_moh_sort_fn, NULL);

	if (!app_bridges_moh) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application support",
	.load = load_module,
	.unload = unload_module,
	);
