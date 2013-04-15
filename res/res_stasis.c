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
 * \brief Stasis application container. Please call apps_registry() instead of
 * directly accessing.
 */
struct ao2_container *__apps_registry;

struct ao2_container *__app_controls;

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
	/*! Name of the Stasis application */
	char name[];
};

static void app_dtor(void *obj)
{
	struct app *app = obj;

	ao2_cleanup(app->data);
	app->data = NULL;
}

/*! Constructor for \ref app. */
static struct app *app_create(const char *name, stasis_app_cb handler, void *data)
{
	struct app *app;
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

/*!
 * \brief Send a message to the given application.
 * \param app App to send the message to.
 * \param message Message to send.
 */
static void app_send(struct app *app, struct ast_json *message)
{
	app->handler(app->data, app->name, message);
}

struct stasis_app_control {
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

	strncpy(control->channel_id, uniqueid, size - sizeof(*control));

	return control;
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
	RAII_VAR(struct ao2_container *, controls, NULL, ao2_cleanup);
	if (chan == NULL) {
		return NULL;
	}

	controls = app_controls();
	return ao2_find(controls, ast_channel_uniqueid(chan), OBJ_KEY);
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

static struct ast_json *app_event_create(
	const char *event_name,
	const struct ast_channel_snapshot *snapshot,
	const struct ast_json *extra_info)
{
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, event, NULL, ast_json_unref);

	if (extra_info) {
		event = ast_json_deep_copy(extra_info);
	} else {
		event = ast_json_object_create();
	}

	if (snapshot) {
		int ret;

		/* Mustn't already have a channel field */
		ast_assert(ast_json_object_get(event, "channel") == NULL);

		ret = ast_json_object_set(
			event,
			"channel", ast_channel_snapshot_to_json(snapshot));
		if (ret != 0) {
			return NULL;
		}
	}

	message = ast_json_pack("{s: o}", event_name, ast_json_ref(event));

	return ast_json_ref(message);
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

	msg = ast_json_pack("{s: {s: [], s: o}}",
			    "stasis-start",
			    "args",
			    "channel", ast_channel_snapshot_to_json(snapshot));

	if (!msg) {
		return -1;
	}

	/* Append arguments to args array */
	json_args = ast_json_object_get(
		ast_json_object_get(msg, "stasis-start"),
		"args");
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
	msg = app_event_create("stasis-end", snapshot, NULL);
	if (!msg) {
		return -1;
	}

	app_send(app, msg);
	return 0;
}

static void dtmf_handler(struct app *app, struct ast_channel_blob *obj)
{
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
	const char *direction;

	/* To simplify events, we'll only generate on receive */
	direction = ast_json_string_get(
		ast_json_object_get(obj->blob, "direction"));

	if (strcmp("Received", direction) != 0) {
		return;
	}

	extra = ast_json_pack(
		"{s: o}",
		"digit", ast_json_ref(ast_json_object_get(obj->blob, "digit")));
	if (!extra) {
		return;
	}

	msg = app_event_create("dtmf-received", obj->snapshot, extra);
	if (!msg) {
		return;
	}

	app_send(app, msg);
}

static void blob_handler(struct app *app, struct ast_channel_blob *blob)
{
	/* To simplify events, we'll only generate on DTMF end */
	if (strcmp(ast_channel_blob_json_type(blob), "dtmf_end") == 0) {
		dtmf_handler(app, blob);
	}
}

static void sub_handler(void *data, struct stasis_subscription *sub,
			struct stasis_topic *topic,
			struct stasis_message *message)
{
	struct app *app = data;
	if (ast_channel_snapshot_type() == stasis_message_type(message)) {
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);
		struct ast_channel_snapshot *snapshot =
			stasis_message_data(message);

		msg = app_event_create("channel-state-change", snapshot, NULL);
		if (!msg) {
			return;
		}
		app_send(app, msg);
	} else if (ast_channel_blob_type() == stasis_message_type(message)) {
		struct ast_channel_blob *blob = stasis_message_data(message);
		blob_handler(app, blob);
	}
	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(data);
	}
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

/*! /brief Stasis dialplan application callback */
int stasis_app_exec(struct ast_channel *chan, const char *app_name, int argc,
		    char *argv[])
{
	RAII_VAR(struct ao2_container *, apps, apps_registry(), ao2_cleanup);
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_app_control *, control, NULL, control_unlink);
	RAII_VAR(struct stasis_subscription *, subscription, NULL,
		 stasis_unsubscribe);
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

	subscription =
		stasis_subscribe(ast_channel_topic(chan), sub_handler, app);
	if (subscription == NULL) {
		ast_log(LOG_ERROR, "Error subscribing app %s to channel %s\n",
			app_name, ast_channel_name(chan));
		return -1;
	}
	ao2_ref(app, +1); /* subscription now has a reference */

	res = send_start_msg(app, chan, argc, argv);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error sending start message to %s\n", app_name);
		return res;
	}

	while (!hungup && !control_continue_test_and_reset(control) && ast_waitfor(chan, -1) > -1) {
		RAII_VAR(struct ast_frame *, f, ast_read(chan), ast_frame_dtor);
		if (!f) {
			ast_debug(3, "%s: No more frames. Must be done, I guess.\n", ast_channel_uniqueid(chan));
			break;
		}

		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass.integer == AST_CONTROL_HANGUP) {
				ast_debug(3, "%s: Received hangup\n",
					  ast_channel_uniqueid(chan));
				hungup = 1;
			}
			break;
		default:
			/* Not handled; discard */
			break;
		}
	}

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
		SCOPED_LOCK(app_lock, app, ao2_lock, ao2_unlock);

		msg = app_event_create("application-replaced", NULL, NULL);
		app->handler(app->data, app_name, msg);

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

	return r;
}

static int unload_module(void)
{
	int r = 0;

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
