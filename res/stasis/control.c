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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis_channels.h"

#include "command.h"
#include "control.h"
#include "app.h"
#include "asterisk/dial.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_after.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"

AST_LIST_HEAD(app_control_rules, stasis_app_control_rule);

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

	AST_LIST_HEAD_DESTROY(&control->add_rules);
	AST_LIST_HEAD_DESTROY(&control->remove_rules);

	/* We may have a lingering silence generator; free it */
	ast_channel_stop_silence_generator(control->channel, control->silgen);
	control->silgen = NULL;

	ao2_cleanup(control->command_queue);
	ast_cond_destroy(&control->wait_cond);
	ao2_cleanup(control->app);
}

struct stasis_app_control *control_create(struct ast_channel *channel, struct stasis_app *app)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	int res;

	control = ao2_alloc(sizeof(*control), control_dtor);
	if (!control) {
		return NULL;
	}

	control->app = ao2_bump(app);

	res = ast_cond_init(&control->wait_cond, NULL);
	if (res != 0) {
		ast_log(LOG_ERROR, "Error initializing ast_cond_t: %s\n",
			strerror(errno));
		return NULL;
	}

	control->command_queue = ao2_container_alloc_list(
		AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);

	if (!control->command_queue) {
		return NULL;
	}

	control->channel = channel;

	AST_LIST_HEAD_INIT(&control->add_rules);
	AST_LIST_HEAD_INIT(&control->remove_rules);

	ao2_ref(control, +1);
	return control;
}

static void app_control_register_rule(
	const struct stasis_app_control *control,
	struct app_control_rules *list, struct stasis_app_control_rule *obj)
{
	SCOPED_AO2LOCK(lock, control->command_queue);
	AST_LIST_INSERT_TAIL(list, obj, next);
}

static void app_control_unregister_rule(
	const struct stasis_app_control *control,
	struct app_control_rules *list, struct stasis_app_control_rule *obj)
{
	struct stasis_app_control_rule *rule;
	SCOPED_AO2LOCK(lock, control->command_queue);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(list, rule, next) {
		if (rule == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
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
	void *data, app_command_can_exec_cb can_exec_fn)
{
	int retval;
	struct stasis_app_command *command;

	command_fn = command_fn ? : noop_cb;

	command = command_create(command_fn, data);
	if (!command) {
		return NULL;
	}

	ao2_lock(control->command_queue);
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
	void *data)
{
	return exec_command_on_condition(control, command_fn, data, NULL);
}

struct stasis_app_control_dial_data {
	char endpoint[AST_CHANNEL_NAME];
	int timeout;
};

static int app_control_dial(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct ast_dial *, dial, ast_dial_create(), ast_dial_destroy);
	RAII_VAR(struct stasis_app_control_dial_data *, dial_data, data, ast_free);
	enum ast_dial_result res;
	char *tech, *resource;
	struct ast_channel *new_chan;
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	tech = dial_data->endpoint;
	if (!(resource = strchr(tech, '/'))) {
		return -1;
	}
	*resource++ = '\0';

	if (!dial) {
		ast_log(LOG_ERROR, "Failed to create dialing structure.\n");
		return -1;
	}

	if (ast_dial_append(dial, tech, resource, NULL) < 0) {
		ast_log(LOG_ERROR, "Failed to add %s/%s to dialing structure.\n", tech, resource);
		return -1;
	}

	ast_dial_set_global_timeout(dial, dial_data->timeout);

	res = ast_dial_run(dial, NULL, 0);
	if (res != AST_DIAL_RESULT_ANSWERED || !(new_chan = ast_dial_answered_steal(dial))) {
		return -1;
	}

	if (!(bridge = ast_bridge_basic_new())) {
		ast_log(LOG_ERROR, "Failed to create basic bridge.\n");
		return -1;
	}

	if (ast_bridge_impart(bridge, new_chan, NULL, NULL,
		AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		ast_hangup(new_chan);
	} else {
		control_add_channel_to_bridge(control, chan, bridge);
	}

	return 0;
}

int stasis_app_control_dial(struct stasis_app_control *control, const char *endpoint, const char *exten, const char *context,
			    int timeout)
{
	struct stasis_app_control_dial_data *dial_data;

	if (!(dial_data = ast_calloc(1, sizeof(*dial_data)))) {
		return -1;
	}

	if (!ast_strlen_zero(endpoint)) {
		ast_copy_string(dial_data->endpoint, endpoint, sizeof(dial_data->endpoint));
	} else if (!ast_strlen_zero(exten) && !ast_strlen_zero(context)) {
		snprintf(dial_data->endpoint, sizeof(dial_data->endpoint), "Local/%s@%s", exten, context);
	} else {
		return -1;
	}

	if (timeout > 0) {
		dial_data->timeout = timeout * 1000;
	} else if (timeout == -1) {
		dial_data->timeout = -1;
	} else {
		dial_data->timeout = 30000;
	}

	stasis_app_send_command_async(control, app_control_dial, dial_data);

	return 0;
}

int stasis_app_control_add_role(struct stasis_app_control *control, const char *role)
{
	return ast_channel_add_bridge_role(control->channel, role);
}

void stasis_app_control_clear_roles(struct stasis_app_control *control)
{
	ast_channel_clear_bridge_roles(control->channel);
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
	control->is_done = 1;
}

struct stasis_app_control_continue_data {
	char context[AST_MAX_CONTEXT];
	char extension[AST_MAX_EXTENSION];
	int priority;
};

static int app_control_continue(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_control_continue_data *, continue_data, data, ast_free);

	ast_assert(control->channel != NULL);

	/* If we're in a Stasis bridge, depart it before going back to the
	 * dialplan */
	if (stasis_app_get_bridge(control)) {
		ast_bridge_depart(control->channel);
	}

	/* Called from stasis_app_exec thread; no lock needed */
	ast_explicit_goto(control->channel, continue_data->context, continue_data->extension, continue_data->priority);

	control->is_done = 1;

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

	stasis_app_send_command_async(control, app_control_continue, continue_data);

	return 0;
}

struct stasis_app_control_dtmf_data {
	int before;
	int between;
	unsigned int duration;
	int after;
	char dtmf[];
};

static int app_control_dtmf(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_control_dtmf_data *, dtmf_data, data, ast_free);

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_indicate(chan, AST_CONTROL_PROGRESS);
	}

	if (dtmf_data->before) {
		ast_safe_sleep(chan, dtmf_data->before);
	}

	ast_dtmf_stream(chan, NULL, dtmf_data->dtmf, dtmf_data->between, dtmf_data->duration);

	if (dtmf_data->after) {
		ast_safe_sleep(chan, dtmf_data->after);
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

	stasis_app_send_command_async(control, app_control_dtmf, dtmf_data);

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
	stasis_app_send_command_async(control, app_control_ring, NULL);

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
	stasis_app_send_command_async(control, app_control_ring_stop, NULL);

	return 0;
}

struct stasis_app_control_mute_data {
	enum ast_frame_type frametype;
	unsigned int direction;
};

static int app_control_mute(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_control_mute_data *, mute_data, data, ast_free);
	SCOPED_CHANNELLOCK(lockvar, chan);

	ast_channel_suppress(control->channel, mute_data->direction, mute_data->frametype);

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

	stasis_app_send_command_async(control, app_control_mute, mute_data);

	return 0;
}

static int app_control_unmute(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_control_mute_data *, mute_data, data, ast_free);
	SCOPED_CHANNELLOCK(lockvar, chan);

	ast_channel_unsuppress(control->channel, mute_data->direction, mute_data->frametype);

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

	stasis_app_send_command_async(control, app_control_unmute, mute_data);

	return 0;
}

char *stasis_app_control_get_channel_var(struct stasis_app_control *control, const char *variable)
{
	RAII_VAR(struct ast_str *, tmp, ast_str_create(32), ast_free);

	/* You may be tempted to lock the channel you're about to read from. You
	 * would be wrong. Some dialplan functions put the channel into
	 * autoservice, which deadlocks if the channel is already locked.
	 * ast_str_retrieve_variable() does its own locking, and the dialplan
	 * functions need to as well. We should be fine without the lock.
	 */

	if (!tmp) {
		return NULL;
	}

	if (variable[strlen(variable) - 1] == ')') {
		if (ast_func_read2(control->channel, variable, &tmp, 0)) {
			return NULL;
		}
	} else {
		if (!ast_str_retrieve_variable(&tmp, 0, control->channel, NULL, variable)) {
			return NULL;
		}
	}

	return ast_strdup(ast_str_buffer(tmp));
}

int stasis_app_control_set_channel_var(struct stasis_app_control *control, const char *variable, const char *value)
{
	return pbx_builtin_setvar_helper(control->channel, variable, value);
}

static int app_control_hold(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_HOLD);

	return 0;
}

void stasis_app_control_hold(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_hold, NULL);
}

static int app_control_unhold(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_UNHOLD);

	return 0;
}

void stasis_app_control_unhold(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_unhold, NULL);
}

static int app_control_moh_start(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	char *moh_class = data;

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_indicate(chan, AST_CONTROL_PROGRESS);
	}

	ast_moh_start(chan, moh_class, NULL);

	ast_free(moh_class);
	return 0;
}

void stasis_app_control_moh_start(struct stasis_app_control *control, const char *moh_class)
{
	char *data = NULL;

	if (!ast_strlen_zero(moh_class)) {
		data = ast_strdup(moh_class);
	}

	stasis_app_send_command_async(control, app_control_moh_start, data);
}

static int app_control_moh_stop(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_moh_stop(chan);
	return 0;
}

void stasis_app_control_moh_stop(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_moh_stop, NULL);
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
	stasis_app_send_command_async(control, app_control_silence_start, NULL);
}

static int app_control_silence_stop(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	if (control->silgen) {
		ast_debug(3, "%s: Stopping silence generator\n",
			stasis_app_control_get_channel_id(control));
		ast_channel_stop_silence_generator(
			control->channel, control->silgen);
		control->silgen = NULL;
	}

	return 0;
}

void stasis_app_control_silence_stop(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_silence_stop, NULL);
}

struct ast_channel_snapshot *stasis_app_control_get_snapshot(
	const struct stasis_app_control *control)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_channel_snapshot *snapshot;

	msg = stasis_cache_get(ast_channel_cache(), ast_channel_snapshot_type(),
		stasis_app_control_get_channel_id(control));
	if (!msg) {
		return NULL;
	}

	snapshot = stasis_message_data(msg);
	ast_assert(snapshot != NULL);

	ao2_ref(snapshot, +1);
	return snapshot;
}

static int app_send_command_on_condition(struct stasis_app_control *control,
					 stasis_app_command_cb command_fn, void *data,
					 app_command_can_exec_cb can_exec_fn)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);

	if (control == NULL) {
		return -1;
	}

	command = exec_command_on_condition(
		control, command_fn, data, can_exec_fn);
	if (!command) {
		return -1;
	}

	return command_join(command);
}

int stasis_app_send_command(struct stasis_app_control *control,
	stasis_app_command_cb command_fn, void *data)
{
	return app_send_command_on_condition(control, command_fn, data, NULL);
}

int stasis_app_send_command_async(struct stasis_app_control *control,
	stasis_app_command_cb command_fn, void *data)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);

	if (control == NULL) {
		return -1;
	}

	command = exec_command(control, command_fn, data);
	if (!command) {
		return -1;
	}

	return 0;
}

struct ast_bridge *stasis_app_get_bridge(struct stasis_app_control *control)
{
	if (!control) {
		return NULL;
	} else {
		SCOPED_AO2LOCK(lock, control);
		return control->bridge;
	}
}

static int bridge_channel_depart(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, data, ao2_cleanup);

	{
		SCOPED_CHANNELLOCK(lock, chan);

		if (bridge_channel != ast_channel_internal_bridge_channel(chan)) {
			ast_debug(3, "%s: Channel is no longer in departable state\n",
				ast_channel_uniqueid(chan));
			return -1;
		}
	}

	ast_debug(3, "%s: Channel departing bridge\n",
		ast_channel_uniqueid(chan));

	ast_bridge_depart(chan);

	return 0;
}

static void bridge_after_cb(struct ast_channel *chan, void *data)
{
	struct stasis_app_control *control = data;
	SCOPED_AO2LOCK(lock, control);
	struct ast_bridge_channel *bridge_channel;

	ast_debug(3, "%s, %s: Channel leaving bridge\n",
		ast_channel_uniqueid(chan), control->bridge->uniqueid);

	ast_assert(chan == control->channel);

	/* Restore the channel's PBX */
	ast_channel_pbx_set(control->channel, control->pbx);
	control->pbx = NULL;

	app_unsubscribe_bridge(control->app, control->bridge);

	/* No longer in the bridge */
	control->bridge = NULL;

	/* Get the bridge channel so we don't depart from the wrong bridge */
	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);

	/* Depart this channel from the bridge using the command queue if possible */
	if (stasis_app_send_command_async(control, bridge_channel_depart, bridge_channel)) {
		ao2_cleanup(bridge_channel);
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
}

static void bridge_after_cb_failed(enum ast_bridge_after_cb_reason reason,
	void *data)
{
	struct stasis_app_control *control = data;

	bridge_after_cb(control->channel, data);

	ast_debug(3, "  reason: %s\n",
		ast_bridge_after_cb_reason_string(reason));
}

int control_add_channel_to_bridge(
	struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge *bridge = data;
	int res;

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

	{
		/* pbx and bridge are modified by the bridging impart thread.
		 * It shouldn't happen concurrently, but we still need to lock
		 * for the memory fence.
		 */
		SCOPED_AO2LOCK(lock, control);

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

		res = ast_bridge_impart(bridge,
			chan,
			NULL, /* swap channel */
			NULL, /* features */
			AST_BRIDGE_IMPART_CHAN_DEPARTABLE);
		if (res != 0) {
			ast_log(LOG_ERROR, "Error adding channel to bridge\n");
			ast_channel_pbx_set(chan, control->pbx);
			control->pbx = NULL;
			return -1;
		}

		ast_assert(stasis_app_get_bridge(control) == NULL);
		control->bridge = bridge;
	}
	return 0;
}

int stasis_app_control_add_channel_to_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge)
{
	ast_debug(3, "%s: Sending channel add_to_bridge command\n",
			stasis_app_control_get_channel_id(control));

	return app_send_command_on_condition(
		control, control_add_channel_to_bridge, bridge,
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

	ast_bridge_depart(chan);
	return 0;
}

int stasis_app_control_remove_channel_from_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge)
{
	ast_debug(3, "%s: Sending channel remove_from_bridge command\n",
			stasis_app_control_get_channel_id(control));
	return app_send_command_on_condition(
		control, app_control_remove_channel_from_bridge, bridge,
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

int control_dispatch_all(struct stasis_app_control *control,
	struct ast_channel *chan)
{
	int count = 0;
	struct ao2_iterator i;
	void *obj;

	ast_assert(control->channel == chan);

	i = ao2_iterator_init(control->command_queue, AO2_ITERATOR_UNLINK);

	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_app_command *, command, obj, ao2_cleanup);
		command_invoke(command, control, chan);
		++count;
	}

	ao2_iterator_destroy(&i);
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
