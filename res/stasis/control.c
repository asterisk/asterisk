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
 * \brief Stasis application control support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_app.h"

#include "command.h"
#include "control.h"
#include "app.h"
#include "asterisk/dial.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_after.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/bridge_features.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"

AST_LIST_HEAD(app_control_rules, stasis_app_control_rule);

/*!
 * \brief Indicates if the Stasis app internals are being shut down
 */
static int shutting_down;

struct stasis_app_control {
	ast_cond_t wait_cond;
	/*! Queue of commands to dispatch on the channel */
	struct ao2_container *command_queue;
	/*!
	 * The associated channel.
	 * Be very careful with the threading associated w/ manipulating
	 * the channel.
	 */
	struct ast_channel *channel;
	/*!
	 * When a channel is in a bridge, the bridge that it is in.
	 */
	struct ast_bridge *bridge;
	/*!
	 * Bridge features which should be applied to the channel when it enters the next bridge.  These only apply to the next bridge and will be emptied thereafter.
	 */
	struct ast_bridge_features *bridge_features;
	/*!
	 * Holding place for channel's PBX while imparted to a bridge.
	 */
	struct ast_pbx *pbx;
	/*!
	 * A list of rules to check before adding a channel to a bridge.
	 */
	struct app_control_rules add_rules;
	/*!
	 * A list of rules to check before removing a channel from a bridge.
	 */
	struct app_control_rules remove_rules;
	/*!
	 * Silence generator, when silence is being generated.
	 */
	struct ast_silence_generator *silgen;
	/*!
	 * The app for which this control was created
	 */
	struct stasis_app *app;
	/*!
	 * When set, /c app_stasis should exit and continue in the dialplan.
	 */
	int is_done:1;
};

static void control_dtor(void *obj)
{
	struct stasis_app_control *control = obj;

	ao2_cleanup(control->command_queue);

	ast_channel_cleanup(control->channel);
	ao2_cleanup(control->app);

	ast_cond_destroy(&control->wait_cond);
	AST_LIST_HEAD_DESTROY(&control->add_rules);
	AST_LIST_HEAD_DESTROY(&control->remove_rules);
	ast_bridge_features_destroy(control->bridge_features);

}

struct stasis_app_control *control_create(struct ast_channel *channel, struct stasis_app *app)
{
	struct stasis_app_control *control;
	int res;

	control = ao2_alloc(sizeof(*control), control_dtor);
	if (!control) {
		return NULL;
	}

	AST_LIST_HEAD_INIT(&control->add_rules);
	AST_LIST_HEAD_INIT(&control->remove_rules);

	res = ast_cond_init(&control->wait_cond, NULL);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error initializing ast_cond_t: %s\n",
			strerror(errno));
		ao2_ref(control, -1);
		return NULL;
	}

	control->app = ao2_bump(app);

	ast_channel_ref(channel);
	control->channel = channel;

	control->command_queue = ao2_container_alloc_list(
		AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	if (!control->command_queue) {
		ao2_ref(control, -1);
		return NULL;
	}

	return control;
}

static void app_control_register_rule(
	struct stasis_app_control *control,
	struct app_control_rules *list, struct stasis_app_control_rule *obj)
{
	ao2_lock(control->command_queue);
	AST_LIST_INSERT_TAIL(list, obj, next);
	ao2_unlock(control->command_queue);
}

static void app_control_unregister_rule(
	struct stasis_app_control *control,
	struct app_control_rules *list, struct stasis_app_control_rule *obj)
{
	struct stasis_app_control_rule *rule;

	ao2_lock(control->command_queue);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(list, rule, next) {
		if (rule == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	ao2_unlock(control->command_queue);
}

/*!
 * \internal
 * \brief Checks to make sure each rule in the given list passes.
 *
 * \details Loops over a list of rules checking for rejections or failures.
 *          If one rule fails its resulting error code is returned.
 *
 * \note Command queue should be locked before calling this function.
 *
 * \param control The stasis application control
 * \param list The list of rules to check
 *
 * \retval 0 if all rules pass
 * \retval non-zero error code if a rule fails
 */
static enum stasis_app_control_channel_result app_control_check_rules(
	const struct stasis_app_control *control,
	struct app_control_rules *list)
{
	int res = 0;
	struct stasis_app_control_rule *rule;
	AST_LIST_TRAVERSE(list, rule, next) {
		if ((res = rule->check_rule(control))) {
			return res;
		}
	}
	return res;
}

void stasis_app_control_register_add_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule)
{
	return app_control_register_rule(control, &control->add_rules, rule);
}

void stasis_app_control_unregister_add_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule)
{
	app_control_unregister_rule(control, &control->add_rules, rule);
}

void stasis_app_control_register_remove_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule)
{
	return app_control_register_rule(control, &control->remove_rules, rule);
}

void stasis_app_control_unregister_remove_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule)
{
	app_control_unregister_rule(control, &control->remove_rules, rule);
}

static int app_control_can_add_channel_to_bridge(
	struct stasis_app_control *control)
{
	return app_control_check_rules(control, &control->add_rules);
}

static int app_control_can_remove_channel_from_bridge(
	struct stasis_app_control *control)
{
	return app_control_check_rules(control, &control->remove_rules);
}

static int noop_cb(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	return 0;
}

/*! Callback type to see if the command can execute
    note: command_queue is locked during callback */
typedef int (*app_command_can_exec_cb)(struct stasis_app_control *control);

static struct stasis_app_command *exec_command_on_condition(
	struct stasis_app_control *control, stasis_app_command_cb command_fn,
	void *data, command_data_destructor_fn data_destructor,
	app_command_can_exec_cb can_exec_fn)
{
	int retval;
	struct stasis_app_command *command;

	command_fn = command_fn ? : noop_cb;

	command = command_create(command_fn, data, data_destructor);
	if (!command) {
		return NULL;
	}

	ao2_lock(control->command_queue);
	if (control->is_done) {
		ao2_unlock(control->command_queue);
		ao2_ref(command, -1);
		return NULL;
	}
	if (can_exec_fn && (retval = can_exec_fn(control))) {
		ao2_unlock(control->command_queue);
		command_complete(command, retval);
		return command;
	}

	ao2_link_flags(control->command_queue, command, OBJ_NOLOCK);
	ast_cond_signal(&control->wait_cond);
	ao2_unlock(control->command_queue);

	return command;
}

static struct stasis_app_command *exec_command(
	struct stasis_app_control *control, stasis_app_command_cb command_fn,
	void *data, command_data_destructor_fn data_destructor)
{
	return exec_command_on_condition(control, command_fn, data, data_destructor, NULL);
}

static int app_control_add_role(struct stasis_app_control *control,
		struct ast_channel *chan, void *data)
{
	char *role = data;

	return ast_channel_add_bridge_role(chan, role);
}

int stasis_app_control_add_role(struct stasis_app_control *control, const char *role)
{
	char *role_dup;

	role_dup = ast_strdup(role);
	if (!role_dup) {
		return -1;
	}

	stasis_app_send_command_async(control, app_control_add_role, role_dup, ast_free_ptr);

	return 0;
}

static int app_control_clear_roles(struct stasis_app_control *control,
		struct ast_channel *chan, void *data)
{
	ast_channel_clear_bridge_roles(chan);

	return 0;
}

void stasis_app_control_clear_roles(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_clear_roles, NULL, NULL);
}

int control_command_count(struct stasis_app_control *control)
{
	return ao2_container_count(control->command_queue);
}

int control_is_done(struct stasis_app_control *control)
{
	/* Called from stasis_app_exec thread; no lock needed */
	return control->is_done;
}

void control_mark_done(struct stasis_app_control *control)
{
	/* Locking necessary to sync with other threads adding commands to the queue. */
	ao2_lock(control->command_queue);
	control->is_done = 1;
	ao2_unlock(control->command_queue);
}

struct stasis_app_control_continue_data {
	char context[AST_MAX_CONTEXT];
	char extension[AST_MAX_EXTENSION];
	int priority;
};

static int app_control_continue(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct stasis_app_control_continue_data *continue_data = data;

	ast_assert(control->channel != NULL);

	/* If we're in a Stasis bridge, depart it before going back to the
	 * dialplan */
	if (stasis_app_get_bridge(control)) {
		ast_bridge_depart(control->channel);
	}

	/* Called from stasis_app_exec thread; no lock needed */
	ast_explicit_goto(control->channel, continue_data->context, continue_data->extension, continue_data->priority);

	control_mark_done(control);

	return 0;
}

int stasis_app_control_continue(struct stasis_app_control *control, const char *context, const char *extension, int priority)
{
	struct stasis_app_control_continue_data *continue_data;

	if (!(continue_data = ast_calloc(1, sizeof(*continue_data)))) {
		return -1;
	}
	ast_copy_string(continue_data->context, S_OR(context, ""), sizeof(continue_data->context));
	ast_copy_string(continue_data->extension, S_OR(extension, ""), sizeof(continue_data->extension));
	if (priority > 0) {
		continue_data->priority = priority;
	} else {
		continue_data->priority = -1;
	}

	stasis_app_send_command_async(control, app_control_continue, continue_data, ast_free_ptr);

	return 0;
}

static int app_control_redirect(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	char *endpoint = data;
	int res;

	ast_assert(control->channel != NULL);
	ast_assert(endpoint != NULL);

	res = ast_transfer(control->channel, endpoint);
	if (!res) {
		ast_log(LOG_NOTICE, "Unsupported transfer requested on channel '%s'\n",
			ast_channel_name(control->channel));
		return 0;
	}

	return 0;
}

int stasis_app_control_redirect(struct stasis_app_control *control, const char *endpoint)
{
	char *endpoint_data = ast_strdup(endpoint);

	if (!endpoint_data) {
		return -1;
	}

	stasis_app_send_command_async(control, app_control_redirect, endpoint_data, ast_free_ptr);

	return 0;
}

struct stasis_app_control_dtmf_data {
	int before;
	int between;
	unsigned int duration;
	int after;
	char dtmf[];
};

static void dtmf_in_bridge(struct ast_channel *chan, struct stasis_app_control_dtmf_data *dtmf_data)
{
	if (dtmf_data->before) {
		usleep(dtmf_data->before * 1000);
	}

	ast_dtmf_stream_external(chan, dtmf_data->dtmf, dtmf_data->between, dtmf_data->duration);

	if (dtmf_data->after) {
		usleep(dtmf_data->after * 1000);
	}
}

static void dtmf_no_bridge(struct ast_channel *chan, struct stasis_app_control_dtmf_data *dtmf_data)
{
	if (dtmf_data->before) {
		ast_safe_sleep(chan, dtmf_data->before);
	}

	ast_dtmf_stream(chan, NULL, dtmf_data->dtmf, dtmf_data->between, dtmf_data->duration);

	if (dtmf_data->after) {
		ast_safe_sleep(chan, dtmf_data->after);
	}
}

static int app_control_dtmf(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct stasis_app_control_dtmf_data *dtmf_data = data;

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_indicate(chan, AST_CONTROL_PROGRESS);
	}

	if (stasis_app_get_bridge(control)) {
		dtmf_in_bridge(chan, dtmf_data);
	} else {
		dtmf_no_bridge(chan, dtmf_data);
	}

	return 0;
}

int stasis_app_control_dtmf(struct stasis_app_control *control, const char *dtmf, int before, int between, unsigned int duration, int after)
{
	struct stasis_app_control_dtmf_data *dtmf_data;

	if (!(dtmf_data = ast_calloc(1, sizeof(*dtmf_data) + strlen(dtmf) + 1))) {
		return -1;
	}

	dtmf_data->before = before;
	dtmf_data->between = between;
	dtmf_data->duration = duration;
	dtmf_data->after = after;
	strcpy(dtmf_data->dtmf, dtmf);

	stasis_app_send_command_async(control, app_control_dtmf, dtmf_data, ast_free_ptr);

	return 0;
}

static int app_control_ring(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_RINGING);

	return 0;
}

int stasis_app_control_ring(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_ring, NULL, NULL);

	return 0;
}

static int app_control_ring_stop(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, -1);

	return 0;
}

int stasis_app_control_ring_stop(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_ring_stop, NULL, NULL);

	return 0;
}

struct stasis_app_control_mute_data {
	enum ast_frame_type frametype;
	unsigned int direction;
};

static int app_control_mute(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct stasis_app_control_mute_data *mute_data = data;

	ast_channel_lock(chan);
	ast_channel_suppress(control->channel, mute_data->direction, mute_data->frametype);
	ast_channel_unlock(chan);

	return 0;
}

int stasis_app_control_mute(struct stasis_app_control *control, unsigned int direction, enum ast_frame_type frametype)
{
	struct stasis_app_control_mute_data *mute_data;

	if (!(mute_data = ast_calloc(1, sizeof(*mute_data)))) {
		return -1;
	}

	mute_data->direction = direction;
	mute_data->frametype = frametype;

	stasis_app_send_command_async(control, app_control_mute, mute_data, ast_free_ptr);

	return 0;
}

static int app_control_unmute(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct stasis_app_control_mute_data *mute_data = data;

	ast_channel_lock(chan);
	ast_channel_unsuppress(control->channel, mute_data->direction, mute_data->frametype);
	ast_channel_unlock(chan);

	return 0;
}

int stasis_app_control_unmute(struct stasis_app_control *control, unsigned int direction, enum ast_frame_type frametype)
{
	struct stasis_app_control_mute_data *mute_data;

	if (!(mute_data = ast_calloc(1, sizeof(*mute_data)))) {
		return -1;
	}

	mute_data->direction = direction;
	mute_data->frametype = frametype;

	stasis_app_send_command_async(control, app_control_unmute, mute_data, ast_free_ptr);

	return 0;
}

/*!
 * \brief structure for queuing ARI channel variable setting
 *
 * It may seem weird to define this custom structure given that we already have
 * ast_var_t and ast_variable defined elsewhere. The problem with those is that
 * they are not tolerant of NULL channel variable value pointers. In fact, in both
 * cases, the best they could do is to have a zero-length variable value. However,
 * when un-setting a channel variable, it is important to pass a NULL value, not
 * a zero-length string.
 */
struct chanvar {
	/*! Name of variable to set/unset */
	char *name;
	/*! Value of variable to set. If unsetting, this will be NULL */
	char *value;
};

static void free_chanvar(void *data)
{
	struct chanvar *var = data;

	ast_free(var->name);
	ast_free(var->value);
	ast_free(var);
}

static int app_control_set_channel_var(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct chanvar *var = data;

	pbx_builtin_setvar_helper(control->channel, var->name, var->value);

	return 0;
}

int stasis_app_control_set_channel_var(struct stasis_app_control *control, const char *variable, const char *value)
{
	struct chanvar *var;

	var = ast_calloc(1, sizeof(*var));
	if (!var) {
		return -1;
	}

	var->name = ast_strdup(variable);
	if (!var->name) {
		free_chanvar(var);
		return -1;
	}

	/* It's kosher for value to be NULL. It means the variable is being unset */
	if (value) {
		var->value = ast_strdup(value);
		if (!var->value) {
			free_chanvar(var);
			return -1;
		}
	}

	stasis_app_send_command_async(control, app_control_set_channel_var, var, free_chanvar);

	return 0;
}

static int app_control_hold(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_HOLD);

	return 0;
}

void stasis_app_control_hold(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_hold, NULL, NULL);
}

static int app_control_unhold(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_UNHOLD);

	return 0;
}

void stasis_app_control_unhold(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_unhold, NULL, NULL);
}

static int app_control_moh_start(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	char *moh_class = data;

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_indicate(chan, AST_CONTROL_PROGRESS);
	}

	ast_moh_start(chan, moh_class, NULL);

	return 0;
}

void stasis_app_control_moh_start(struct stasis_app_control *control, const char *moh_class)
{
	char *data = NULL;

	if (!ast_strlen_zero(moh_class)) {
		data = ast_strdup(moh_class);
	}

	stasis_app_send_command_async(control, app_control_moh_start, data, ast_free_ptr);
}

static int app_control_moh_stop(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_moh_stop(chan);
	return 0;
}

void stasis_app_control_moh_stop(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_moh_stop, NULL, NULL);
}

static int app_control_silence_start(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_indicate(chan, AST_CONTROL_PROGRESS);
	}

	if (control->silgen) {
		/* We have a silence generator, but it may have been implicitly
		 * disabled by media actions (music on hold, playing media,
		 * etc.) Just stop it and restart a new one.
		 */
		ast_channel_stop_silence_generator(
			control->channel, control->silgen);
	}

	ast_debug(3, "%s: Starting silence generator\n",
		stasis_app_control_get_channel_id(control));
	control->silgen = ast_channel_start_silence_generator(control->channel);

	if (!control->silgen) {
		ast_log(LOG_WARNING,
			"%s: Failed to start silence generator.\n",
			stasis_app_control_get_channel_id(control));
	}

	return 0;
}

void stasis_app_control_silence_start(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_silence_start, NULL, NULL);
}

void control_silence_stop_now(struct stasis_app_control *control)
{
	if (control->silgen) {
		ast_debug(3, "%s: Stopping silence generator\n",
			stasis_app_control_get_channel_id(control));
		ast_channel_stop_silence_generator(
			control->channel, control->silgen);
		control->silgen = NULL;
	}
}

static int app_control_silence_stop(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	control_silence_stop_now(control);
	return 0;
}

void stasis_app_control_silence_stop(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_silence_stop, NULL, NULL);
}

struct ast_channel_snapshot *stasis_app_control_get_snapshot(
	const struct stasis_app_control *control)
{
	struct stasis_message *msg;
	struct ast_channel_snapshot *snapshot;

	msg = stasis_cache_get(ast_channel_cache(), ast_channel_snapshot_type(),
		stasis_app_control_get_channel_id(control));
	if (!msg) {
		return NULL;
	}

	snapshot = stasis_message_data(msg);
	ast_assert(snapshot != NULL);

	ao2_ref(snapshot, +1);
	ao2_ref(msg, -1);

	return snapshot;
}

static int app_send_command_on_condition(struct stasis_app_control *control,
					 stasis_app_command_cb command_fn, void *data,
					 command_data_destructor_fn data_destructor,
					 app_command_can_exec_cb can_exec_fn)
{
	int ret;
	struct stasis_app_command *command;

	if (control == NULL || control->is_done) {
		/* If exec_command_on_condition fails, it calls the data_destructor.
		 * In order to provide consistent behavior, we'll also call the data_destructor
		 * on this error path. This way, callers never have to call the
		 * data_destructor themselves.
		 */
		if (data_destructor) {
			data_destructor(data);
		}
		return -1;
	}

	command = exec_command_on_condition(
		control, command_fn, data, data_destructor, can_exec_fn);
	if (!command) {
		return -1;
	}

	ret = command_join(command);
	ao2_ref(command, -1);

	return ret;
}

int stasis_app_send_command(struct stasis_app_control *control,
	stasis_app_command_cb command_fn, void *data, command_data_destructor_fn data_destructor)
{
	return app_send_command_on_condition(control, command_fn, data, data_destructor, NULL);
}

int stasis_app_send_command_async(struct stasis_app_control *control,
	stasis_app_command_cb command_fn, void *data,
	command_data_destructor_fn data_destructor)
{
	struct stasis_app_command *command;

	if (control == NULL || control->is_done) {
		/* If exec_command fails, it calls the data_destructor. In order to
		 * provide consistent behavior, we'll also call the data_destructor
		 * on this error path. This way, callers never have to call the
		 * data_destructor themselves.
		 */
		if (data_destructor) {
			data_destructor(data);
		}
		return -1;
	}

	command = exec_command(control, command_fn, data, data_destructor);
	if (!command) {
		return -1;
	}
	ao2_ref(command, -1);

	return 0;
}

struct ast_bridge *stasis_app_get_bridge(struct stasis_app_control *control)
{
	struct ast_bridge *ret;

	if (!control) {
		return NULL;
	}

	ao2_lock(control);
	ret = control->bridge;
	ao2_unlock(control);

	return ret;
}

/*!
 * \brief Singleton dial bridge
 *
 * The dial bridge is a holding bridge used to hold all
 * outbound dialed channels that are not in any "real" ARI-created
 * bridge. The dial bridge is invisible, meaning that it does not
 * show up in channel snapshots, AMI or ARI output, and no events
 * get raised for it.
 *
 * This is used to keep dialed channels confined to the bridging system
 * and unify the threading model used for dialing outbound channels.
 */
static struct ast_bridge *dial_bridge;
AST_MUTEX_DEFINE_STATIC(dial_bridge_lock);

/*!
 * \brief Retrieve a reference to the dial bridge.
 *
 * If the dial bridge has not been created yet, it will
 * be created, otherwise, a reference to the existing bridge
 * will be returned.
 *
 * The caller will need to unreference the dial bridge once
 * they are finished with it.
 *
 * \retval NULL Unable to find/create the dial bridge
 * \retval non-NULL A reference to the dial bridge
 */
static struct ast_bridge *get_dial_bridge(void)
{
	struct ast_bridge *ret_bridge = NULL;

	ast_mutex_lock(&dial_bridge_lock);

	if (shutting_down) {
		goto end;
	}

	if (dial_bridge) {
		ret_bridge = ao2_bump(dial_bridge);
		goto end;
	}

	dial_bridge = stasis_app_bridge_create_invisible("holding", "dial_bridge", NULL);
	if (!dial_bridge) {
		goto end;
	}
	ret_bridge = ao2_bump(dial_bridge);

end:
	ast_mutex_unlock(&dial_bridge_lock);
	return ret_bridge;
}

static int bridge_channel_depart(struct stasis_app_control *control,
	struct ast_channel *chan, void *data);

/*!
 * \brief after bridge callback for the dial bridge
 *
 * The only purpose of this callback is to ensure that the control structure's
 * bridge pointer is NULLed
 */
static void dial_bridge_after_cb(struct ast_channel *chan, void *data)
{
	struct stasis_app_control *control = data;
	struct ast_bridge_channel *bridge_channel;

	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);

	ast_debug(3, "Channel: <%s>  Reason: %d\n", ast_channel_name(control->channel), ast_channel_hangupcause(chan));

	stasis_app_send_command_async(control, bridge_channel_depart, bridge_channel, __ao2_cleanup);

	control->bridge = NULL;
}

static void dial_bridge_after_cb_failed(enum ast_bridge_after_cb_reason reason, void *data)
{
	struct stasis_app_control *control = data;

	ast_debug(3, "Channel: <%s>  Reason: %d\n", ast_channel_name(control->channel), reason);
	dial_bridge_after_cb(control->channel, data);
}

/*!
 * \brief Add a channel to the singleton dial bridge.
 *
 * \param control The Stasis control structure
 * \param chan The channel to add to the bridge
 * \retval -1 Failed
 * \retval 0 Success
 */
static int add_to_dial_bridge(struct stasis_app_control *control, struct ast_channel *chan)
{
	struct ast_bridge *bridge;

	bridge = get_dial_bridge();
	if (!bridge) {
		return -1;
	}

	control->bridge = bridge;
	ast_bridge_set_after_callback(chan, dial_bridge_after_cb, dial_bridge_after_cb_failed, control);
	if (ast_bridge_impart(bridge, chan, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE)) {
		control->bridge = NULL;
		ao2_ref(bridge, -1);
		return -1;
	}

	ao2_ref(bridge, -1);

	return 0;
}

/*!
 * \brief Depart a channel from a bridge, and potentially add it back to the dial bridge
 *
 * \param control Take a guess
 * \param chan Take another guess
 */
static int depart_channel(struct stasis_app_control *control, struct ast_channel *chan)
{
	ast_bridge_depart(chan);

	if (!ast_check_hangup(chan) && ast_channel_state(chan) != AST_STATE_UP) {
		/* Channel is still being dialed, so put it back in the dialing bridge */
		add_to_dial_bridge(control, chan);
	}

	return 0;
}

static int bridge_channel_depart(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge_channel *bridge_channel;

	ast_channel_lock(chan);
	bridge_channel = ast_channel_internal_bridge_channel(chan);
	ast_channel_unlock(chan);

	if (bridge_channel != data) {
		ast_debug(3, "%s: Channel is no longer in departable state\n",
			ast_channel_uniqueid(chan));
		return -1;
	}

	ast_debug(3, "%s: Channel departing bridge\n",
		ast_channel_uniqueid(chan));

	depart_channel(control, chan);

	return 0;
}

static void internal_bridge_after_cb(struct ast_channel *chan, void *data,
	enum ast_bridge_after_cb_reason reason)
{
	struct stasis_app_control *control = data;
	struct ast_bridge_channel *bridge_channel;

	ao2_lock(control);
	ast_debug(3, "%s, %s: %s\n",
		ast_channel_uniqueid(chan), control->bridge ? control->bridge->uniqueid : "unknown",
			ast_bridge_after_cb_reason_string(reason));

	if (reason == AST_BRIDGE_AFTER_CB_REASON_IMPART_FAILED) {
		/* The impart actually failed so control->bridge isn't valid. */
		control->bridge = NULL;
	}

	ast_assert(chan == control->channel);

	/* Restore the channel's PBX */
	ast_channel_pbx_set(control->channel, control->pbx);
	control->pbx = NULL;

	if (control->bridge) {
		app_unsubscribe_bridge(control->app, control->bridge);

		/* No longer in the bridge */
		control->bridge = NULL;

		/* Get the bridge channel so we don't depart from the wrong bridge */
		ast_channel_lock(chan);
		bridge_channel = ast_channel_get_bridge_channel(chan);
		ast_channel_unlock(chan);

		/* Depart this channel from the bridge using the command queue if possible */
		stasis_app_send_command_async(control, bridge_channel_depart, bridge_channel, __ao2_cleanup);
	}

	if (stasis_app_channel_is_stasis_end_published(chan)) {
		/* The channel has had a StasisEnd published on it, but until now had remained in
		 * the bridging system. This means that the channel moved from a Stasis bridge to a
		 * non-Stasis bridge and is now exiting the bridging system. Because of this, the
		 * channel needs to exit the Stasis application and go to wherever the non-Stasis
		 * bridge has directed it to go. If the non-Stasis bridge has not set up an after
		 * bridge destination, then the channel should be hung up.
		 */
		int hangup_flag;

		hangup_flag = ast_bridge_setup_after_goto(chan) ? AST_SOFTHANGUP_DEV : AST_SOFTHANGUP_ASYNCGOTO;
		ast_channel_lock(chan);
		ast_softhangup_nolock(chan, hangup_flag);
		ast_channel_unlock(chan);
	}
	ao2_unlock(control);
}

static void bridge_after_cb(struct ast_channel *chan, void *data)
{
	struct stasis_app_control *control = data;

	internal_bridge_after_cb(control->channel, data, AST_BRIDGE_AFTER_CB_REASON_DEPART);
}

static void bridge_after_cb_failed(enum ast_bridge_after_cb_reason reason,
	void *data)
{
	struct stasis_app_control *control = data;

	internal_bridge_after_cb(control->channel, data, reason);

	ast_debug(3, "  reason: %s\n",
		ast_bridge_after_cb_reason_string(reason));
}

/*!
 * \brief Dial timeout datastore
 *
 * A datastore is used because a channel may change
 * bridges during the course of a dial attempt. This
 * may be because the channel changes from the dial bridge
 * to a standard bridge, or it may move between standard
 * bridges. In order to keep the dial timeout, we need
 * to keep the timeout information local to the channel.
 * That is what this datastore is for
 */
struct ast_datastore_info timeout_datastore = {
	.type = "ARI dial timeout",
};

static int hangup_channel(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);
	return 0;
}

/*!
 * \brief Dial timeout
 *
 * This is a bridge interval hook callback. The interval hook triggering
 * means that the dial timeout has been reached. If the channel has not
 * been answered by the time this callback is called, then the channel
 * is hung up
 *
 * \param bridge_channel Bridge channel on which interval hook has been called
 * \param ignore Ignored
 * \return -1 (i.e. remove the interval hook)
 */
static int bridge_timeout(struct ast_bridge_channel *bridge_channel, void *ignore)
{
	struct ast_datastore *datastore;
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);

	control = stasis_app_control_find_by_channel(bridge_channel->chan);

	ast_channel_lock(bridge_channel->chan);
	if (ast_channel_state(bridge_channel->chan) != AST_STATE_UP) {
		/* Don't bother removing the datastore because it will happen when the channel is hung up */
		ast_channel_unlock(bridge_channel->chan);
		stasis_app_send_command_async(control, hangup_channel, NULL, NULL);
		return -1;
	}

	datastore = ast_channel_datastore_find(bridge_channel->chan, &timeout_datastore, NULL);
	if (!datastore) {
		ast_channel_unlock(bridge_channel->chan);
		return -1;
	}
	ast_channel_datastore_remove(bridge_channel->chan, datastore);
	ast_channel_unlock(bridge_channel->chan);
	ast_datastore_free(datastore);

	return -1;
}

/*!
 * \brief Set a dial timeout interval hook on the channel.
 *
 * The absolute time that the timeout should occur is stored on
 * a datastore on the channel. This time is converted into a relative
 * number of milliseconds in the future. Then an interval hook is set
 * to trigger in that number of milliseconds.
 *
 * \pre chan is locked
 *
 * \param chan The channel on which to set the interval hook
 */
static void set_interval_hook(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct timeval *hangup_time;
	int64_t ms;
	struct ast_bridge_channel *bridge_channel;

	datastore = ast_channel_datastore_find(chan, &timeout_datastore, NULL);
	if (!datastore) {
		return;
	}

	hangup_time = datastore->data;

	ms = ast_tvdiff_ms(*hangup_time, ast_tvnow());
	bridge_channel = ast_channel_get_bridge_channel(chan);
	if (!bridge_channel) {
		return;
	}

	if (ast_bridge_interval_hook(bridge_channel->features, 0, ms > 0 ? ms : 1,
			bridge_timeout, NULL, NULL, 0)) {
		ao2_ref(bridge_channel, -1);
		return;
	}

	ast_queue_frame(bridge_channel->chan, &ast_null_frame);
	ao2_ref(bridge_channel, -1);
}

int control_swap_channel_in_bridge(struct stasis_app_control *control, struct ast_bridge *bridge, struct ast_channel *chan, struct ast_channel *swap)
{
	int res;
	struct ast_bridge_features *features;

	if (!control || !bridge) {
		return -1;
	}

	ast_debug(3, "%s: Adding to bridge %s\n",
		stasis_app_control_get_channel_id(control),
		bridge->uniqueid);

	ast_assert(chan != NULL);

	/* Depart whatever Stasis bridge we're currently in. */
	if (stasis_app_get_bridge(control)) {
		/* Note that it looks like there's a race condition here, since
		 * we don't have control locked. But this happens from the
		 * control callback thread, so there won't be any other
		 * concurrent attempts to bridge.
		 */
		ast_bridge_depart(chan);
	}


	res = ast_bridge_set_after_callback(chan, bridge_after_cb,
		bridge_after_cb_failed, control);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error setting after-bridge callback\n");
		return -1;
	}

	ao2_lock(control);

	/* Ensure the controlling application is subscribed early enough
	 * to receive the ChannelEnteredBridge message. This works in concert
	 * with the subscription handled in the Stasis application execution
	 * loop */
	app_subscribe_bridge(control->app, bridge);

	/* Save off the channel's PBX */
	ast_assert(control->pbx == NULL);
	if (!control->pbx) {
		control->pbx = ast_channel_pbx(chan);
		ast_channel_pbx_set(chan, NULL);
	}

	/* Pull bridge features from the control */
	features = control->bridge_features;
	control->bridge_features = NULL;

	ast_assert(stasis_app_get_bridge(control) == NULL);
	/* We need to set control->bridge here since bridge_after_cb may be run
	 * before ast_bridge_impart returns.  bridge_after_cb gets a reason
	 * code so it can tell if the bridge is actually valid or not.
	 */
	control->bridge = bridge;

	/* We can't be holding the control lock while impart is running
	 * or we could create a deadlock with bridge_after_cb which also
	 * tries to lock control.
	 */
	ao2_unlock(control);
	res = ast_bridge_impart(bridge,
		chan,
		swap,
		features, /* features */
		AST_BRIDGE_IMPART_CHAN_DEPARTABLE);
	if (res != 0) {
		/* ast_bridge_impart failed before it could spawn the depart
		 * thread.  The callbacks aren't called in this case.
		 * The impart could still fail even if ast_bridge_impart returned
		 * ok but that's handled by bridge_after_cb.
		 */
		ast_log(LOG_ERROR, "Error adding channel to bridge\n");
		ao2_lock(control);
		ast_channel_pbx_set(chan, control->pbx);
		control->pbx = NULL;
		control->bridge = NULL;
		ao2_unlock(control);
	} else {
		ast_channel_lock(chan);
		set_interval_hook(chan);
		ast_channel_unlock(chan);
	}

	return res;
}

int control_add_channel_to_bridge(struct stasis_app_control *control, struct ast_channel *chan, void *data)
{
	return control_swap_channel_in_bridge(control, data, chan, NULL);
}

int stasis_app_control_add_channel_to_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge)
{
	ast_debug(3, "%s: Sending channel add_to_bridge command\n",
			stasis_app_control_get_channel_id(control));

	return app_send_command_on_condition(
		control, control_add_channel_to_bridge, bridge, NULL,
		app_control_can_add_channel_to_bridge);
}

static int app_control_remove_channel_from_bridge(
	struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge *bridge = data;

	if (!control) {
		return -1;
	}

	/* We should only depart from our own bridge */
	ast_debug(3, "%s: Departing bridge %s\n",
		stasis_app_control_get_channel_id(control),
		bridge->uniqueid);

	if (bridge != stasis_app_get_bridge(control)) {
		ast_log(LOG_WARNING, "%s: Not in bridge %s; not removing\n",
			stasis_app_control_get_channel_id(control),
			bridge->uniqueid);
		return -1;
	}

	depart_channel(control, chan);
	return 0;
}

int stasis_app_control_remove_channel_from_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge)
{
	ast_debug(3, "%s: Sending channel remove_from_bridge command\n",
			stasis_app_control_get_channel_id(control));
	return app_send_command_on_condition(
		control, app_control_remove_channel_from_bridge, bridge, NULL,
		app_control_can_remove_channel_from_bridge);
}

const char *stasis_app_control_get_channel_id(
	const struct stasis_app_control *control)
{
	return ast_channel_uniqueid(control->channel);
}

void stasis_app_control_publish(
	struct stasis_app_control *control, struct stasis_message *message)
{
	if (!control || !control->channel || !message) {
		return;
	}
	stasis_publish(ast_channel_topic(control->channel), message);
}

int stasis_app_control_queue_control(struct stasis_app_control *control,
	enum ast_control_frame_type frame_type)
{
	return ast_queue_control(control->channel, frame_type);
}

int stasis_app_control_bridge_features_init(
	struct stasis_app_control *control)
{
	struct ast_bridge_features *features;

	features = ast_bridge_features_new();
	if (!features) {
		return 1;
	}
	control->bridge_features = features;
	return 0;
}

void stasis_app_control_absorb_dtmf_in_bridge(
	struct stasis_app_control *control, int absorb)
{
	control->bridge_features->dtmf_passthrough = !absorb;
}

void stasis_app_control_mute_in_bridge(
	struct stasis_app_control *control, int mute)
{
	control->bridge_features->mute = mute;
}

void control_flush_queue(struct stasis_app_control *control)
{
	struct ao2_iterator iter;
	struct stasis_app_command *command;

	iter = ao2_iterator_init(control->command_queue, AO2_ITERATOR_UNLINK);
	while ((command = ao2_iterator_next(&iter))) {
		command_complete(command, -1);
		ao2_ref(command, -1);
	}
	ao2_iterator_destroy(&iter);
}

int control_dispatch_all(struct stasis_app_control *control,
	struct ast_channel *chan)
{
	int count = 0;
	struct ao2_iterator iter;
	struct stasis_app_command *command;

	ast_assert(control->channel == chan);

	iter = ao2_iterator_init(control->command_queue, AO2_ITERATOR_UNLINK);
	while ((command = ao2_iterator_next(&iter))) {
		command_invoke(command, control, chan);
		ao2_ref(command, -1);
		++count;
	}
	ao2_iterator_destroy(&iter);

	return count;
}

void control_wait(struct stasis_app_control *control)
{
	if (!control) {
		return;
	}

	ast_assert(control->command_queue != NULL);

	ao2_lock(control->command_queue);
	while (ao2_container_count(control->command_queue) == 0) {
		int res = ast_cond_wait(&control->wait_cond,
			ao2_object_get_lockaddr(control->command_queue));
		if (res < 0) {
			ast_log(LOG_ERROR, "Error waiting on command queue\n");
			break;
		}
	}
	ao2_unlock(control->command_queue);
}

int control_prestart_dispatch_all(struct stasis_app_control *control,
	struct ast_channel *chan)
{
	struct ao2_container *command_queue;
	int count = 0;
	struct ao2_iterator iter;
	struct stasis_app_command *command;

	ast_channel_lock(chan);
	command_queue = command_prestart_get_container(chan);
	ast_channel_unlock(chan);
	if (!command_queue) {
		return 0;
	}

	iter = ao2_iterator_init(command_queue, AO2_ITERATOR_UNLINK);

	while ((command = ao2_iterator_next(&iter))) {
		command_invoke(command, control, chan);
		ao2_cleanup(command);
		++count;
	}

	ao2_iterator_destroy(&iter);
	ao2_cleanup(command_queue);
	return count;
}

struct stasis_app *control_app(struct stasis_app_control *control)
{
	return control->app;
}

struct control_dial_args {
	unsigned int timeout;
	char dialstring[0];
};

static struct control_dial_args *control_dial_args_alloc(const char *dialstring,
	unsigned int timeout)
{
	struct control_dial_args *args;

	args = ast_malloc(sizeof(*args) + strlen(dialstring) + 1);
	if (!args) {
		return NULL;
	}

	args->timeout = timeout;
	/* Safe */
	strcpy(args->dialstring, dialstring);

	return args;
}

static void control_dial_args_destroy(void *data)
{
	struct control_dial_args *args = data;

	ast_free(args);
}

/*!
 * \brief Set dial timeout on a channel to be dialed.
 *
 * \param chan The channel on which to set the dial timeout
 * \param timeout The timeout in seconds
 */
static int set_timeout(struct ast_channel *chan, unsigned int timeout)
{
	struct ast_datastore *datastore;
	struct timeval *hangup_time;

	hangup_time = ast_malloc(sizeof(struct timeval));

	datastore = ast_datastore_alloc(&timeout_datastore, NULL);
	if (!datastore) {
		return -1;
	}
	*hangup_time = ast_tvadd(ast_tvnow(), ast_samp2tv(timeout, 1));
	datastore->data = hangup_time;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);

	if (ast_channel_is_bridged(chan)) {
		set_interval_hook(chan);
	}
	ast_channel_unlock(chan);

	return 0;
}

static int app_control_dial(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct control_dial_args *args = data;
	int bridged;

	ast_channel_lock(chan);
	bridged = ast_channel_is_bridged(chan);
	ast_channel_unlock(chan);

	if (!bridged && add_to_dial_bridge(control, chan)) {
		return -1;
	}

	if (args->timeout && set_timeout(chan, args->timeout)) {
		return -1;
	}

	if (ast_call(chan, args->dialstring, 0)) {
		return -1;
	}

	ast_channel_publish_dial(NULL, chan, args->dialstring, NULL);

	return 0;
}

int stasis_app_control_dial(struct stasis_app_control *control,
		const char *dialstring, unsigned int timeout)
{
	struct control_dial_args *args;

	args = control_dial_args_alloc(dialstring, timeout);
	if (!args) {
		return -1;
	}

	return stasis_app_send_command_async(control, app_control_dial,
		args, control_dial_args_destroy);
}

void stasis_app_control_shutdown(void)
{
	ast_mutex_lock(&dial_bridge_lock);
	shutting_down = 1;
	if (dial_bridge) {
		ast_bridge_destroy(dial_bridge, 0);
		dial_bridge = NULL;
	}
	ast_mutex_unlock(&dial_bridge_lock);
}
