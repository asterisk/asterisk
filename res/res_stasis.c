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
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/strings.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/callerid.h"
#include "stasis_http/resource_events.h"

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
 * \brief Number of buckets for the channels container for app instances.  Remember
 * to keep it a prime number!
 */
#define APP_CHANNELS_BUCKETS 7

/*!
 * \brief Number of buckets for the blob_handlers container.  Remember to keep
 * it a prime number!
 */
#define BLOB_HANDLER_BUCKETS 7

/*!
 * \brief Stasis application container. Please call apps_registry() instead of
 * directly accessing.
 */
struct ao2_container *__apps_registry;

struct ao2_container *__app_controls;

/*! \brief Message router for the channel caching topic */
struct stasis_message_router *channel_router;

/*! Ref-counting accessor for the stasis applications container */
static struct ao2_container *apps_registry(void)
{
	ao2_ref(__apps_registry, +1);
	return __apps_registry;
}

static struct ao2_container *app_controls(void)
{
	ao2_ref(__app_controls, +1);
	return __app_controls;
}

struct app {
	/*! Callback function for this application. */
	stasis_app_cb handler;
	/*! Opaque data to hand to callback function. */
	void *data;
	/*! List of channel identifiers this app instance is interested in */
	struct ao2_container *channels;
	/*! Name of the Stasis application */
	char name[];
};

static void app_dtor(void *obj)
{
	struct app *app = obj;

	ao2_cleanup(app->data);
	app->data = NULL;
	ao2_cleanup(app->channels);
	app->channels = NULL;
}

/*! Constructor for \ref app. */
static struct app *app_create(const char *name, stasis_app_cb handler, void *data)
{
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);
	size_t size;

	ast_assert(name != NULL);
	ast_assert(handler != NULL);

	size = sizeof(*app) + strlen(name) + 1;
	app = ao2_alloc_options(size, app_dtor, AO2_ALLOC_OPT_LOCK_MUTEX);

	if (!app) {
		return NULL;
	}

	strncpy(app->name, name, size - sizeof(*app));
	app->handler = handler;
	ao2_ref(data, +1);
	app->data = data;

	app->channels = ast_str_container_alloc(APP_CHANNELS_BUCKETS);
	if (!app->channels) {
		return NULL;
	}

	ao2_ref(app, +1);
	return app;
}

/*! AO2 hash function for \ref app */
static int app_hash(const void *obj, const int flags)
{
	const struct app *app = obj;
	const char *name = flags & OBJ_KEY ? obj : app->name;

	return ast_str_hash(name);
}

/*! AO2 comparison function for \ref app */
static int app_compare(void *lhs, void *rhs, int flags)
{
	const struct app *lhs_app = lhs;
	const struct app *rhs_app = rhs;
	const char *rhs_name = flags & OBJ_KEY ? rhs : rhs_app->name;

	if (strcmp(lhs_app->name, rhs_name) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
}

static int app_add_channel(struct app* app, const struct ast_channel *chan)
{
	const char *uniqueid;
	ast_assert(chan != NULL);
	ast_assert(app != NULL);

	uniqueid = ast_channel_uniqueid(chan);
	if (!ast_str_container_add(app->channels, uniqueid)) {
		return -1;
	}
	return 0;
}

static void app_remove_channel(struct app* app, const struct ast_channel *chan)
{
	ast_assert(chan != NULL);
	ast_assert(app != NULL);

	ao2_find(app->channels, ast_channel_uniqueid(chan), OBJ_KEY | OBJ_NODATA | OBJ_UNLINK);
}

/*!
 * \brief Send a message to the given application.
 * \param app App to send the message to.
 * \param message Message to send.
 */
static void app_send(struct app *app, struct ast_json *message)
{
	app->handler(app->data, app->name, message);
}

typedef void* (*stasis_app_command_cb)(struct stasis_app_control *control,
				       struct ast_channel *chan,
				       void *data);

struct stasis_app_command {
	ast_mutex_t lock;
	ast_cond_t condition;
	stasis_app_command_cb callback;
	void *data;
	void *retval;
	int is_done:1;
};

static void command_dtor(void *obj)
{
	struct stasis_app_command *command = obj;
	ast_mutex_destroy(&command->lock);
	ast_cond_destroy(&command->condition);
}

static struct stasis_app_command *command_create(stasis_app_command_cb callback,
						 void *data)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);

	command = ao2_alloc(sizeof(*command), command_dtor);
	if (!command) {
		return NULL;
	}

	ast_mutex_init(&command->lock);
	ast_cond_init(&command->condition, 0);
	command->callback = callback;
	command->data = data;

	ao2_ref(command, +1);
	return command;
}

static void command_complete(struct stasis_app_command *command, void *retval)
{
	SCOPED_MUTEX(lock, &command->lock);

	command->is_done = 1;
	command->retval = retval;
	ast_cond_signal(&command->condition);
}

static void *wait_for_command(struct stasis_app_command *command)
{
	SCOPED_MUTEX(lock, &command->lock);
	while (!command->is_done) {
		ast_cond_wait(&command->condition, &command->lock);
	}

	return command->retval;
}

struct stasis_app_control {
	/*! Queue of commands to dispatch on the channel */
	struct ao2_container *command_queue;
	/*!
	 * When set, /c app_stasis should exit and continue in the dialplan.
	 */
	int continue_to_dialplan:1;
	/*! Uniqueid of the associated channel */
	char channel_id[];
};

static struct stasis_app_control *control_create(const char *uniqueid)
{
	struct stasis_app_control *control;
	size_t size;

	size = sizeof(*control) + strlen(uniqueid) + 1;
	control = ao2_alloc(size, NULL);
	if (!control) {
		return NULL;
	}

	control->command_queue = ao2_container_alloc_list(0, 0, NULL, NULL);

	strncpy(control->channel_id, uniqueid, size - sizeof(*control));

	return control;
}

static void *exec_command(struct stasis_app_control *control,
			  struct stasis_app_command *command)
{
	ao2_lock(control);
	ao2_ref(command, +1);
	ao2_link(control->command_queue, command);
	ao2_unlock(control);

	return wait_for_command(command);
}

/*! AO2 hash function for \ref stasis_app_control */
static int control_hash(const void *obj, const int flags)
{
	const struct stasis_app_control *control = obj;
	const char *id = flags & OBJ_KEY ? obj : control->channel_id;

	return ast_str_hash(id);
}

/*! AO2 comparison function for \ref stasis_app_control */
static int control_compare(void *lhs, void *rhs, int flags)
{
	const struct stasis_app_control *lhs_control = lhs;
	const struct stasis_app_control *rhs_control = rhs;
	const char *rhs_name =
		flags & OBJ_KEY ? rhs : rhs_control->channel_id;

	if (strcmp(lhs_control->channel_id, rhs_name) == 0) {
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
	RAII_VAR(struct ao2_container *, controls, NULL, ao2_cleanup);
	controls = app_controls();
	return ao2_find(controls, channel_id, OBJ_KEY);
}

/*!
 * \brief Test the \c continue_to_dialplan bit for the given \a app.
 *
 * The bit is also reset for the next call.
 *
 * \param app Application to check the \c continue_to_dialplan bit.
 * \return Zero to remain in \c Stasis
 * \return Non-zero to continue in the dialplan
 */
static int control_continue_test_and_reset(struct stasis_app_control *control)
{
	int r;
	SCOPED_AO2LOCK(lock, control);

	r = control->continue_to_dialplan;
	control->continue_to_dialplan = 0;
	return r;
}

void stasis_app_control_continue(struct stasis_app_control *control)
{
	SCOPED_AO2LOCK(lock, control);
	control->continue_to_dialplan = 1;
}

/*! \brief Typedef for blob handler callbacks */
typedef struct ast_json *(*channel_blob_handler_cb)(struct ast_channel_blob *);

static int OK = 0;
static int FAIL = -1;

static void *__app_control_answer(struct stasis_app_control *control,
				  struct ast_channel *chan, void *data)
{
	ast_debug(3, "%s: Answering", control->channel_id);
	return __ast_answer(chan, 0, 1) == 0 ? &OK : &FAIL;
}

int stasis_app_control_answer(struct stasis_app_control *control)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);
	int *retval;

	ast_debug(3, "%s: Sending answer command\n", control->channel_id);

	command = command_create(__app_control_answer, NULL);
	retval = exec_command(control, command);

	if (*retval != 0) {
		ast_log(LOG_WARNING, "Failed to answer channel");
	}

	return *retval;
}

static int send_start_msg(struct app *app, struct ast_channel *chan,
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

	msg = stasis_json_event_stasis_end_create(snapshot);
	if (!msg) {
		return -1;
	}

	app_send(app, msg);
	return 0;
}

static int app_watching_channel_cb(void *obj, void *arg, int flags)
{
	RAII_VAR(char *, uniqueid, NULL, ao2_cleanup);
	struct app *app = obj;
	char *chan_uniqueid = arg;

	uniqueid = ao2_find(app->channels, chan_uniqueid, OBJ_KEY);
	return uniqueid ? CMP_MATCH : 0;
}

static struct ao2_container *get_watching_apps(const char *uniqueid)
{
	RAII_VAR(struct ao2_container *, apps, apps_registry(), ao2_cleanup);
	struct ao2_container *watching_apps;
	char *uniqueid_dup;
	RAII_VAR(struct ao2_iterator *,watching_apps_iter, NULL, ao2_iterator_destroy);
	ast_assert(uniqueid != NULL);
	ast_assert(apps != NULL);

	uniqueid_dup = ast_strdupa(uniqueid);

	watching_apps_iter = ao2_callback(apps, OBJ_MULTIPLE, app_watching_channel_cb, uniqueid_dup);
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
	RAII_VAR(struct ao2_container *, controls, NULL, ao2_cleanup);

	if (!control) {
		return;
	}

	controls = app_controls();
	ao2_unlink_flags(controls, control,
			 OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
	ao2_cleanup(control);
}

static void dispatch_commands(struct stasis_app_control *control,
			      struct ast_channel *chan)
{
	struct ao2_iterator i;
	void *obj;

	SCOPED_AO2LOCK(lock, control);

	i = ao2_iterator_init(control->command_queue, AO2_ITERATOR_UNLINK);

	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_app_command *, command, obj, ao2_cleanup);
		void *retval = command->callback(control, chan, command->data);
		command_complete(command, retval);
	}

	ao2_iterator_destroy(&i);
}


/*! /brief Stasis dialplan application callback */
int stasis_app_exec(struct ast_channel *chan, const char *app_name, int argc,
		    char *argv[])
{
	RAII_VAR(struct ao2_container *, apps, apps_registry(), ao2_cleanup);
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, control_unlink);
	int res = 0;
	int hungup = 0;

	ast_assert(chan != NULL);

	app = ao2_find(apps, app_name, OBJ_KEY);
	if (!app) {
		ast_log(LOG_ERROR,
			"Stasis app '%s' not registered\n", app_name);
		return -1;
	}

	{
		RAII_VAR(struct ao2_container *, controls, NULL, ao2_cleanup);

		controls = app_controls();
		control = control_create(ast_channel_uniqueid(chan));
		if (!control) {
			ast_log(LOG_ERROR, "Allocated failed\n");
			return -1;
		}
		ao2_link(controls, control);
	}

	res = send_start_msg(app, chan, argc, argv);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error sending start message to %s\n", app_name);
		return res;
	}

	if (app_add_channel(app, chan)) {
		ast_log(LOG_ERROR, "Error adding listener for channel %s to app %s\n", ast_channel_name(chan), app_name);
		return -1;
	}

	while (1) {
		RAII_VAR(struct ast_frame *, f, NULL, ast_frame_dtor);
		int r;

		if (hungup) {
			ast_debug(3, "%s: Hangup\n",
				  ast_channel_uniqueid(chan));
			break;
		}

		if (control_continue_test_and_reset(control)) {
			ast_debug(3, "%s: Continue\n",
				  ast_channel_uniqueid(chan));
			break;
		}

		r = ast_waitfor(chan, MAX_WAIT_MS);

		if (r < 0) {
			ast_debug(3, "%s: Poll error\n",
				  ast_channel_uniqueid(chan));
			break;
		}

		dispatch_commands(control, chan);

		if (r == 0) {
			/* Timeout */
			continue;
		}

		f = ast_read(chan);
		if (!f) {
			ast_debug(3, "%s: No more frames. Must be done, I guess.\n", ast_channel_uniqueid(chan));
			break;
		}

		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass.integer == AST_CONTROL_HANGUP) {
				hungup = 1;
			}
			break;
		default:
			/* Not handled; discard */
			break;
		}
	}

	app_remove_channel(app, chan);
	res = send_end_msg(app, chan);
	if (res != 0) {
		ast_log(LOG_ERROR,
			"Error sending end message to %s\n", app_name);
		return res;
	}

	return res;
}

int stasis_app_send(const char *app_name, struct ast_json *message)
{
	RAII_VAR(struct ao2_container *, apps, apps_registry(), ao2_cleanup);
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);

	app = ao2_find(apps, app_name, OBJ_KEY);

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
	RAII_VAR(struct ao2_container *, apps, apps_registry(), ao2_cleanup);
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);

	SCOPED_LOCK(apps_lock, apps, ao2_lock, ao2_unlock);

	app = ao2_find(apps, app_name, OBJ_KEY | OBJ_NOLOCK);

	if (app) {
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
		SCOPED_LOCK(app_lock, app, ao2_lock, ao2_unlock);

		blob = ast_json_pack("{s: s}", "application", app_name);
		if (blob) {
			msg = stasis_json_event_application_replaced_create(blob);
			if (msg) {
				app->handler(app->data, app_name, msg);
			}
		}

		app->handler = handler;
		ao2_cleanup(app->data);
		ao2_ref(data, +1);
		app->data = data;
	} else {
		app = app_create(app_name, handler, data);
		if (app) {
			ao2_link_flags(apps, app, OBJ_NOLOCK);
		} else {
			return -1;
		}
	}

	return 0;
}

void stasis_app_unregister(const char *app_name)
{
	RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);

	if (app_name) {
		apps = apps_registry();
		ao2_cleanup(ao2_find(apps, app_name, OBJ_KEY | OBJ_UNLINK));
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

	__apps_registry =
		ao2_container_alloc(APPS_NUM_BUCKETS, app_hash, app_compare);
	if (__apps_registry == NULL) {
		return AST_MODULE_LOAD_FAILURE;
	}

	__app_controls = ao2_container_alloc(CONTROLS_NUM_BUCKETS,
					     control_hash, control_compare);
	if (__app_controls == NULL) {
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

	stasis_message_router_unsubscribe(channel_router);
	channel_router = NULL;

	ao2_cleanup(__apps_registry);
	__apps_registry = NULL;

	ao2_cleanup(__app_controls);
	__app_controls = NULL;

	return r;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS,
		"Stasis application support",
		.load = load_module,
		.unload = unload_module);
