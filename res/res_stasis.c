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
	<depend>res_stasis_json_events</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/callerid.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/strings.h"
#include "stasis/app.h"
#include "stasis/control.h"
#include "stasis_json/resource_events.h"

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
 * \brief Number of buckets for the blob_handlers container.  Remember to keep
 * it a prime number!
 */
#define BLOB_HANDLER_BUCKETS 7

/*!
 * \brief Stasis application container.
 */
struct ao2_container *apps_registry;

struct ao2_container *app_controls;

/*! \brief Message router for the channel caching topic */
struct stasis_message_router *channel_router;

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

/*! \brief Typedef for blob handler callbacks */
typedef struct ast_json *(*channel_blob_handler_cb)(struct ast_channel_blob *);

static int app_watching_channel_cb(void *obj, void *arg, int flags)
{
	struct app *app = obj;
	char *uniqueid = arg;

	return app_is_watching_channel(app, uniqueid) ? CMP_MATCH : 0;
}

static struct ao2_container *get_watching_apps(const char *uniqueid)
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
	struct ast_channel_snapshot *new_snapshot);

/*! \brief Handle channel state changes */
static struct ast_json *channel_state(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_channel_snapshot *snapshot = new_snapshot ? new_snapshot : old_snapshot;

	if (!old_snapshot) {
		return stasis_json_event_channel_created_create(snapshot);
	} else if (!new_snapshot) {
		json = ast_json_pack("{s: i, s: s}",
			"cause", snapshot->hangupcause,
			"cause_txt", ast_cause2str(snapshot->hangupcause));
		if (!json) {
			return NULL;
		}
		return stasis_json_event_channel_destroyed_create(snapshot, json);
	} else if (old_snapshot->state != new_snapshot->state) {
		return stasis_json_event_channel_state_change_create(snapshot);
	}

	return NULL;
}

static struct ast_json *channel_dialplan(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
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

	json = ast_json_pack("{s: s, s: s}",
		"application", new_snapshot->appl,
		"application_data", new_snapshot->data);
	if (!json) {
		return NULL;
	}

	return stasis_json_event_channel_dialplan_create(new_snapshot, json);
}

static struct ast_json *channel_callerid(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	/* No NewCallerid event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (ast_channel_snapshot_caller_id_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	json = ast_json_pack("{s: i, s: s}",
		"caller_presentation", new_snapshot->caller_pres,
		"caller_presentation_txt", ast_describe_caller_presentation(new_snapshot->caller_pres));
	if (!json) {
		return NULL;
	}

	return stasis_json_event_channel_caller_id_create(new_snapshot, json);
}

static struct ast_json *channel_snapshot(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	if (!new_snapshot) {
		return NULL;
	}

	return stasis_json_event_channel_snapshot_create(new_snapshot);
}

channel_snapshot_monitor channel_monitors[] = {
	channel_snapshot,
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

static void sub_snapshot_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	RAII_VAR(struct ao2_container *, watching_apps, NULL, ao2_cleanup);
	struct stasis_cache_update *update = stasis_message_data(message);
	struct ast_channel_snapshot *new_snapshot = stasis_message_data(update->new_snapshot);
	struct ast_channel_snapshot *old_snapshot = stasis_message_data(update->old_snapshot);
	int i;

	watching_apps = get_watching_apps(new_snapshot ? new_snapshot->uniqueid : old_snapshot->uniqueid);
	if (!watching_apps) {
		return;
	}

	for (i = 0; i < ARRAY_LEN(channel_monitors); ++i) {
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

		msg = channel_monitors[i](old_snapshot, new_snapshot);
		if (msg) {
			ao2_callback(watching_apps, OBJ_NODATA, app_send_cb, msg);
		}
	}
}

static void distribute_message(struct ao2_container *apps, struct ast_json *msg)
{
	ao2_callback(apps, OBJ_NODATA, app_send_cb, msg);
}

static void generic_blob_handler(struct ast_channel_blob *obj, channel_blob_handler_cb handler_cb)
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ao2_container *, watching_apps, NULL, ao2_cleanup);

	if (!obj->snapshot) {
		return;
	}

	watching_apps = get_watching_apps(obj->snapshot->uniqueid);
	if (!watching_apps) {
		return;
	}

	msg = handler_cb(obj);
	if (!msg) {
		return;
	}

	distribute_message(watching_apps, msg);
}

/*!
 * \brief In addition to running ao2_cleanup(), this function also removes the
 * object from the app_controls() container.
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

int app_send_start_msg(struct app *app, struct ast_channel *chan,
	int argc, char *argv[])
{
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);

	struct ast_json *json_args;
	int i;

	ast_assert(chan != NULL);

	/* Set channel info */
	snapshot = ast_channel_snapshot_create(chan);
	if (!snapshot) {
		return -1;
	}

	blob = ast_json_pack("{s: []}", "args");
	if (!blob) {
		return -1;
	}

	/* Append arguments to args array */
	json_args = ast_json_object_get(blob, "args");
	ast_assert(json_args != NULL);
	for (i = 0; i < argc; ++i) {
		int r = ast_json_array_append(json_args,
					      ast_json_string_create(argv[i]));
		if (r != 0) {
			ast_log(LOG_ERROR, "Error appending start message\n");
			return -1;
		}
	}

	msg = stasis_json_event_stasis_start_create(snapshot, blob);
	if (!msg) {
		return -1;
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

	msg = stasis_json_event_stasis_end_create(snapshot);
	if (!msg) {
		return -1;
	}

	app_send(app, msg);
	return 0;
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

	control = control_create(chan);
	if (!control) {
		ast_log(LOG_ERROR, "Allocated failed\n");
		return -1;
	}
	ao2_link(app_controls, control);

	res = app_send_start_msg(app, chan, argc, argv);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending start message to %s\n", app_name);
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
			ast_debug(3,
				"%s: No more frames. Must be done, I guess.\n",
				ast_channel_uniqueid(chan));
			break;
		}

		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass.integer == AST_CONTROL_HANGUP) {
				/* Continue on in the dialplan */
				ast_debug(3, "%s: Hangup\n",
					ast_channel_uniqueid(chan));
				control_continue(control);
			}
			break;
		default:
			/* Not handled; discard */
			break;
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

int stasis_app_register(const char *app_name, stasis_app_cb handler, void *data)
{
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);

	SCOPED_LOCK(apps_lock, apps_registry, ao2_lock, ao2_unlock);

	app = ao2_find(apps_registry, app_name, OBJ_KEY | OBJ_NOLOCK);

	if (app) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

		blob = ast_json_pack("{s: s}", "application", app_name);
		if (blob) {
			msg = stasis_json_event_application_replaced_create(blob);
			if (msg) {
				app_send(app, msg);
			}
		}

		app_update(app, handler, data);
	} else {
		app = app_create(app_name, handler, data);
		if (app) {
			ao2_link_flags(apps_registry, app, OBJ_NOLOCK);
		} else {
			return -1;
		}
	}

	return 0;
}

void stasis_app_unregister(const char *app_name)
{
	if (app_name) {
		ao2_cleanup(ao2_find(
				apps_registry, app_name, OBJ_KEY | OBJ_UNLINK));
	}
}

static struct ast_json *handle_blob_dtmf(struct ast_channel_blob *obj)
{
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	const char *direction;

	/* To simplify events, we'll only generate on receive */
	direction = ast_json_string_get(
		ast_json_object_get(obj->blob, "direction"));

	if (strcmp("Received", direction) != 0) {
		return NULL;
	}

	extra = ast_json_pack(
		"{s: o}",
		"digit", ast_json_ref(ast_json_object_get(obj->blob, "digit")));
	if (!extra) {
		return NULL;
	}

	return stasis_json_event_channel_dtmf_received_create(obj->snapshot, extra);
}

/* To simplify events, we'll only generate on DTMF end (dtmf_end type) */
static void sub_dtmf_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	generic_blob_handler(obj, handle_blob_dtmf);
}

static struct ast_json *handle_blob_userevent(struct ast_channel_blob *obj)
{
	return stasis_json_event_channel_userevent_create(obj->snapshot, obj->blob);
}

static void sub_userevent_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	generic_blob_handler(obj, handle_blob_userevent);
}

static struct ast_json *handle_blob_hangup_request(struct ast_channel_blob *obj)
{
	return stasis_json_event_channel_hangup_request_create(obj->snapshot, obj->blob);
}

static void sub_hangup_request_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	generic_blob_handler(obj, handle_blob_hangup_request);
}

static struct ast_json *handle_blob_varset(struct ast_channel_blob *obj)
{
	return stasis_json_event_channel_varset_create(obj->snapshot, obj->blob);
}

static void sub_varset_handler(void *data,
		struct stasis_subscription *sub,
		struct stasis_topic *topic,
		struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	generic_blob_handler(obj, handle_blob_varset);
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

	channel_router = stasis_message_router_create(stasis_caching_get_topic(ast_channel_topic_all_cached()));
	if (!channel_router) {
		return AST_MODULE_LOAD_FAILURE;
	}

	r |= stasis_message_router_add(channel_router, stasis_cache_update_type(), sub_snapshot_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_user_event_type(), sub_userevent_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_varset_type(), sub_varset_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_dtmf_begin_type(), sub_dtmf_handler, NULL);
	r |= stasis_message_router_add(channel_router, ast_channel_hangup_request_type(), sub_hangup_request_handler, NULL);
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

	ao2_cleanup(apps_registry);
	apps_registry = NULL;

	ao2_cleanup(app_controls);
	app_controls = NULL;

	return r;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS,
		"Stasis application support",
		.load = load_module,
		.unload = unload_module);
