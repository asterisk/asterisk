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
 * \brief Stasis application container.
 */
struct ao2_container *apps_registry;

struct ao2_container *app_controls;

struct ao2_container *app_bridges;

/*! \brief Message router for the channel caching topic */
struct stasis_message_router *channel_router;

/*! \brief Message router for the bridge caching topic */
struct stasis_message_router *bridge_router;

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

struct ast_bridge *stasis_app_bridge_find_by_id(
	const char *bridge_id)
{
	return ao2_find(app_bridges, bridge_id, OBJ_KEY);
}

/*! \brief Typedef for blob handler callbacks */
typedef struct ast_json *(*channel_blob_handler_cb)(struct ast_channel_blob *);

/*! \brief Callback to check whether an app is watching a given channel */
static int app_watching_channel_cb(void *obj, void *arg, int flags)
{
	struct app *app = obj;
	char *uniqueid = arg;

	return app_is_watching_channel(app, uniqueid) ? CMP_MATCH : 0;
}

/*! \brief Get a container full of apps that are interested in the specified channel */
static struct ao2_container *get_apps_watching_channel(const char *uniqueid)
{
	struct ao2_container *watching_apps;
	char *uniqueid_dup;
	RAII_VAR(struct ao2_iterator *,watching_apps_iter, NULL, ao2_iterator_destroy);
	ast_assert(uniqueid != NULL);

	uniqueid_dup = ast_strdupa(uniqueid);

	watching_apps_iter = ao2_callback(apps_registry, OBJ_MULTIPLE, app_watching_channel_cb, uniqueid_dup);
	watching_apps = watching_apps_iter->c;

	if (!ao2_container_count(watching_apps)) {
		return NULL;
	}

	ao2_ref(watching_apps, +1);
	return watching_apps_iter->c;
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
	return ast_json_pack("{s: s, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"channel", ast_channel_snapshot_to_json(snapshot));
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
	return ast_json_pack("{s: s, s: o, s: i, s: s, s: o}",
		"type", "ChannelDestroyed",
		"timestamp", ast_json_timeval(*tv, NULL),
		"cause", snapshot->hangupcause,
		"cause_txt", ast_cause2str(snapshot->hangupcause),
		"channel", ast_channel_snapshot_to_json(snapshot));
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
	struct ast_channel_snapshot *snapshot = new_snapshot ? new_snapshot : old_snapshot;

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
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	/* No Newexten event on cache clear */
	if (!new_snapshot) {
		return NULL;
	}

	/* Empty application is not valid for a Newexten event */
	if (ast_strlen_zero(new_snapshot->appl)) {
		return NULL;
	}

	if (old_snapshot && ast_channel_snapshot_cep_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: s, s: s, s: o}",
		"type", "ChannelDialplan",
		"timestamp", ast_json_timeval(*tv, NULL),
		"dialplan_app", new_snapshot->appl,
		"dialplan_app_data", new_snapshot->data,
		"channel", ast_channel_snapshot_to_json(new_snapshot));
}

static struct ast_json *channel_callerid(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot,
	const struct timeval *tv)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	/* No NewCallerid event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (ast_channel_snapshot_caller_id_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	return ast_json_pack("{s: s, s: o, s: i, s: s, s: o}",
		"type", "ChannelCallerId",
		"timestamp", ast_json_timeval(*tv, NULL),
		"caller_presentation", new_snapshot->caller_pres,
		"caller_presentation_txt", ast_describe_caller_presentation(
			new_snapshot->caller_pres),
		"channel", ast_channel_snapshot_to_json(new_snapshot));
}

channel_snapshot_monitor channel_monitors[] = {
	channel_state,
	channel_dialplan,
	channel_callerid
};

static int app_send_cb(void *obj, void *arg, int flags)
{
	struct app *app = obj;
	struct ast_json *msg = arg;

	app_send(app, msg);
	return 0;
}

static void sub_channel_snapshot_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ao2_container *, watching_apps, NULL, ao2_cleanup);
	struct stasis_cache_update *update = stasis_message_data(message);
	struct ast_channel_snapshot *new_snapshot = stasis_message_data(update->new_snapshot);
	struct ast_channel_snapshot *old_snapshot = stasis_message_data(update->old_snapshot);
	/* Pull timestamp from the new snapshot, or from the update message
	 * when there isn't one. */
	const struct timeval *tv = update->new_snapshot ? stasis_message_timestamp(update->new_snapshot) : stasis_message_timestamp(message);
	int i;

	watching_apps = get_apps_watching_channel(new_snapshot ? new_snapshot->uniqueid : old_snapshot->uniqueid);
	if (!watching_apps) {
		return;
	}

	for (i = 0; i < ARRAY_LEN(channel_monitors); ++i) {
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

		msg = channel_monitors[i](old_snapshot, new_snapshot, tv);
		if (msg) {
			ao2_callback(watching_apps, OBJ_NODATA, app_send_cb, msg);
		}
	}
}

static void distribute_message(struct ao2_container *apps, struct ast_json *msg)
{
	ao2_callback(apps, OBJ_NODATA, app_send_cb, msg);
}

static void sub_channel_blob_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ao2_container *, watching_apps, NULL, ao2_cleanup);
	struct ast_channel_blob *obj = stasis_message_data(message);

	if (!obj->snapshot) {
		return;
	}

	msg = stasis_message_to_json(message);
	if (!msg) {
		return;
	}

	watching_apps = get_apps_watching_channel(obj->snapshot->uniqueid);
	if (!watching_apps) {
		return;
	}

	distribute_message(watching_apps, msg);
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
	ast_bridge_destroy(bridge);
}

int app_send_start_msg(struct app *app, struct ast_channel *chan,
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

	msg = ast_json_pack("{s: s, s: [], s: o}",
		"type", "StasisStart",
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

int app_send_end_msg(struct app *app, struct ast_channel *chan)
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	ast_assert(chan != NULL);

	/* Set channel info */
	snapshot = ast_channel_snapshot_create(chan);
	if (snapshot == NULL) {
		return -1;
	}

	msg = ast_json_pack("{s: s, s: o}",
		"type", "StasisEnd",
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

	res = app_send_start_msg(app, chan, argc, argv);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending start message to '%s'\n", app_name);
		return res;
	}

	if (app_add_channel(app, chan)) {
		ast_log(LOG_ERROR, "Error adding listener for channel %s to app %s\n", ast_channel_name(chan), app_name);
		return -1;
	}

	while (!control_is_done(control)) {
		RAII_VAR(struct ast_frame *, f, NULL, ast_frame_dtor);
		int r;
		int command_count;

		/* Check to see if a bridge absorbed our hangup frame */
		if (ast_check_hangup_locked(chan)) {
			break;
		}

		if (stasis_app_get_bridge(control)) {
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

	app_remove_channel(app, chan);
	res = app_send_end_msg(app, chan);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending end message to %s\n", app_name);
		return res;
	}

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

static int cleanup_cb(void *obj, void *arg, int flags)
{
	struct app *app = obj;

	if (!app_is_finished(app)) {
		return 0;
	}

	ast_verb(1, "Cleaning up application '%s'\n", app_name(app));

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

/*! \brief Callback to check whether an app is watching a given bridge */
static int app_watching_bridge_cb(void *obj, void *arg, int flags)
{
	struct app *app = obj;
	char *uniqueid = arg;

	return app_is_watching_bridge(app, uniqueid) ? CMP_MATCH : 0;
}

/*! \brief Get a container full of apps that are interested in the specified bridge */
static struct ao2_container *get_apps_watching_bridge(const char *uniqueid)
{
	struct ao2_container *watching_apps;
	char *uniqueid_dup;
	RAII_VAR(struct ao2_iterator *,watching_apps_iter, NULL, ao2_iterator_destroy);
	ast_assert(uniqueid != NULL);

	uniqueid_dup = ast_strdupa(uniqueid);

	watching_apps_iter = ao2_callback(apps_registry, OBJ_MULTIPLE, app_watching_bridge_cb, uniqueid_dup);
	watching_apps = watching_apps_iter->c;

	if (!ao2_container_count(watching_apps)) {
		return NULL;
	}

	ao2_ref(watching_apps, +1);
	return watching_apps_iter->c;
}

/*! Callback used to remove an app's interest in a bridge */
static int remove_bridge_cb(void *obj, void *arg, int flags)
{
	app_remove_bridge(obj, arg);
	return 0;
}

static struct ast_json *simple_bridge_event(
	const char *type,
	struct ast_bridge_snapshot *snapshot,
	const struct timeval *tv)
{
	return ast_json_pack("{s: s, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"bridge", ast_bridge_snapshot_to_json(snapshot));
}

static struct ast_json *simple_bridge_channel_event(
	const char *type,
	struct ast_bridge_snapshot *bridge_snapshot,
	struct ast_channel_snapshot *channel_snapshot,
	const struct timeval *tv)
{
	return ast_json_pack("{s: s, s: o, s: o, s: o}",
		"type", type,
		"timestamp", ast_json_timeval(*tv, NULL),
		"bridge", ast_bridge_snapshot_to_json(bridge_snapshot),
		"channel", ast_channel_snapshot_to_json(channel_snapshot));
}

static void sub_bridge_snapshot_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ao2_container *, watching_apps, NULL, ao2_cleanup);
	struct stasis_cache_update *update = stasis_message_data(message);
	struct ast_bridge_snapshot *new_snapshot = stasis_message_data(update->new_snapshot);
	struct ast_bridge_snapshot *old_snapshot = stasis_message_data(update->old_snapshot);
	const struct timeval *tv = update->new_snapshot ? stasis_message_timestamp(update->new_snapshot) : stasis_message_timestamp(message);

	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

	watching_apps = get_apps_watching_bridge(new_snapshot ? new_snapshot->uniqueid : old_snapshot->uniqueid);
	if (!watching_apps || !ao2_container_count(watching_apps)) {
		return;
	}

	if (!new_snapshot) {
		RAII_VAR(char *, bridge_id, ast_strdup(old_snapshot->uniqueid), ast_free);

		/* The bridge has gone away. Create the message, make sure no apps are
		 * watching this bridge anymore, and destroy the bridge's control
		 * structure */
		msg = simple_bridge_event("BridgeDestroyed", old_snapshot, tv);
		ao2_callback(watching_apps, OBJ_NODATA, remove_bridge_cb, bridge_id);
		stasis_app_bridge_destroy(old_snapshot->uniqueid);
	} else if (!old_snapshot) {
		msg = simple_bridge_event("BridgeCreated", old_snapshot, tv);
	}

	if (!msg) {
		return;
	}

	distribute_message(watching_apps, msg);
}

/*! \brief Callback used to merge two containers of applications */
static int list_merge_cb(void *obj, void *arg, int flags)
{
	/* remove any current entries for this app */
	ao2_find(arg, obj, OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE);
	/* relink as the only entry */
	ao2_link(arg, obj);
	return 0;
}

/*! \brief Merge container src into container dst without modifying src */
static void update_apps_list(struct ao2_container *dst, struct ao2_container *src)
{
	ao2_callback(src, OBJ_NODATA, list_merge_cb, dst);
}

/*! \brief Callback for adding to an app's bridges of interest */
static int app_add_bridge_cb(void *obj, void *arg, int flags)
{
	app_add_bridge(obj, arg);
	return 0;
}

/*! \brief Add interest in the given bridge to all apps in the container */
static void update_bridge_interest(struct ao2_container *apps, const char *bridge_id)
{
	RAII_VAR(char *, bridge_id_dup, ast_strdup(bridge_id), ast_free);
	ao2_callback(apps, OBJ_NODATA, app_add_bridge_cb, bridge_id_dup);
}

static void sub_bridge_merge_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ao2_container *, watching_apps_to, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, watching_apps_from, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, watching_apps_all, ao2_container_alloc(1, NULL, NULL), ao2_cleanup);
	struct ast_bridge_merge_message *merge = stasis_message_data(message);
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	const struct timeval *tv = stasis_message_timestamp(message);

	watching_apps_to = get_apps_watching_bridge(merge->to->uniqueid);
	if (watching_apps_to) {
		update_apps_list(watching_apps_all, watching_apps_to);
	}

	watching_apps_from = get_apps_watching_bridge(merge->from->uniqueid);
	if (watching_apps_from) {
		update_bridge_interest(watching_apps_from, merge->to->uniqueid);
		update_apps_list(watching_apps_all, watching_apps_from);
	}

	if (!ao2_container_count(watching_apps_all)) {
		return;
	}

	msg = ast_json_pack("{s: s, s: o, s: o, s: o}",
		"type", "BridgeMerged",
		"timestamp", ast_json_timeval(*tv, NULL),
		"bridge", ast_bridge_snapshot_to_json(merge->to),
		"bridge_from", ast_bridge_snapshot_to_json(merge->from));

	if (!msg) {
		return;
	}

	distribute_message(watching_apps_all, msg);
}

static void sub_bridge_enter_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ao2_container *, watching_apps_channel, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, watching_apps_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, watching_apps_all, ao2_container_alloc(1, NULL, NULL), ao2_cleanup);
	struct ast_bridge_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

	watching_apps_bridge = get_apps_watching_bridge(obj->bridge->uniqueid);
	if (watching_apps_bridge) {
		update_apps_list(watching_apps_all, watching_apps_bridge);
	}

	watching_apps_channel = get_apps_watching_channel(obj->channel->uniqueid);
	if (watching_apps_channel) {
		update_bridge_interest(watching_apps_channel, obj->bridge->uniqueid);
		update_apps_list(watching_apps_all, watching_apps_channel);
	}

	if (!ao2_container_count(watching_apps_all)) {
		return;
	}

	msg = simple_bridge_channel_event("ChannelEnteredBridge", obj->bridge,
		obj->channel, stasis_message_timestamp(message));

	distribute_message(watching_apps_all, msg);
}

static void sub_bridge_leave_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ao2_container *, watching_apps_bridge, NULL, ao2_cleanup);
	struct ast_bridge_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

	watching_apps_bridge = get_apps_watching_bridge(obj->bridge->uniqueid);
	if (!watching_apps_bridge) {
		return;
	}

	msg = simple_bridge_channel_event("ChannelLeftBridge", obj->bridge,
		obj->channel, stasis_message_timestamp(message));

	distribute_message(watching_apps_bridge, msg);
}

static int load_module(void)
{
	int r = 0;

	apps_registry =
		ao2_container_alloc(APPS_NUM_BUCKETS, app_hash, app_compare);
	if (apps_registry == NULL) {
		return AST_MODULE_LOAD_FAILURE;
	}

	app_controls = ao2_container_alloc(CONTROLS_NUM_BUCKETS,
					     control_hash, control_compare);
	if (app_controls == NULL) {
		return AST_MODULE_LOAD_FAILURE;
	}

	app_bridges = ao2_container_alloc(CONTROLS_NUM_BUCKETS,
					     bridges_hash, bridges_compare);
	if (app_bridges == NULL) {
		return AST_MODULE_LOAD_FAILURE;
	}

	channel_router = stasis_message_router_create(ast_channel_topic_all_cached());
	if (!channel_router) {
		return AST_MODULE_LOAD_FAILURE;
	}

	r |= stasis_message_router_add(channel_router, stasis_cache_update_type(), sub_channel_snapshot_handler, NULL);
	/* TODO: This could be handled a lot better. Instead of subscribing to
	 * the one caching topic and filtering out messages by channel id, we
	 * should have individual caching topics per-channel, with a shared
	 * back-end cache. That would simplify a lot of what's going on right
	 * here.
	 */
	r |= stasis_message_router_add(channel_router, ast_channel_user_event_type(), sub_channel_blob_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_varset_type(), sub_channel_blob_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_dtmf_end_type(), sub_channel_blob_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_hangup_request_type(), sub_channel_blob_handler, NULL);
	if (r) {
		return AST_MODULE_LOAD_FAILURE;
	}

	bridge_router = stasis_message_router_create(ast_bridge_topic_all_cached());
	if (!bridge_router) {
		return AST_MODULE_LOAD_FAILURE;
	}

	r |= stasis_message_router_add(bridge_router, stasis_cache_update_type(), sub_bridge_snapshot_handler, NULL);
	r |= stasis_message_router_add(bridge_router, ast_bridge_merge_message_type(), sub_bridge_merge_handler, NULL);
	r |= stasis_message_router_add(bridge_router, ast_channel_entered_bridge_type(), sub_bridge_enter_handler, NULL);
	r |= stasis_message_router_add(bridge_router, ast_channel_left_bridge_type(), sub_bridge_leave_handler, NULL);
	if (r) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int r = 0;

	stasis_message_router_unsubscribe_and_join(channel_router);
	channel_router = NULL;

	stasis_message_router_unsubscribe_and_join(bridge_router);
	bridge_router = NULL;

	ao2_cleanup(apps_registry);
	apps_registry = NULL;

	ao2_cleanup(app_controls);
	app_controls = NULL;

	ao2_cleanup(app_bridges);
	app_bridges = NULL;

	return r;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application support",
	.load = load_module,
	.unload = unload_module,
	);
