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
#include "asterisk/dial.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_after.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/musiconhold.h"

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
	 * When set, /c app_stasis should exit and continue in the dialplan.
	 */
	int is_done:1;
};

static void control_dtor(void *obj)
{
	struct stasis_app_control *control = obj;

	ao2_cleanup(control->command_queue);
	ast_cond_destroy(&control->wait_cond);
}

struct stasis_app_control *control_create(struct ast_channel *channel)
{
	RAII_VAR(struct stasis_app_control *, control, NULL, ao2_cleanup);
	int res;

	control = ao2_alloc(sizeof(*control), control_dtor);
	if (!control) {
		return NULL;
	}

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

	ao2_ref(control, +1);
	return control;
}

static void *noop_cb(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	return NULL;
}


static struct stasis_app_command *exec_command(
	struct stasis_app_control *control, stasis_app_command_cb command_fn,
	void *data)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);

	command_fn = command_fn ? : noop_cb;

	command = command_create(command_fn, data);

	if (!command) {
		return NULL;
	}

	ao2_lock(control->command_queue);
	ao2_link_flags(control->command_queue, command, OBJ_NOLOCK);
	ast_cond_signal(&control->wait_cond);
	ao2_unlock(control->command_queue);

	ao2_ref(command, +1);
	return command;
}

struct stasis_app_control_dial_data {
	char endpoint[AST_CHANNEL_NAME];
	int timeout;
};

static void *app_control_dial(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct ast_dial *, dial, ast_dial_create(), ast_dial_destroy);
	RAII_VAR(struct stasis_app_control_dial_data *, dial_data, data, ast_free);
	enum ast_dial_result res;
	char *tech, *resource;

	struct ast_channel *new_chan;
	struct ast_bridge *bridge;

	tech = dial_data->endpoint;
	if (!(resource = strchr(tech, '/'))) {
		return NULL;
	}
	*resource++ = '\0';

	if (!dial) {
		ast_log(LOG_ERROR, "Failed to create dialing structure.\n");
		return NULL;
	}

	if (ast_dial_append(dial, tech, resource) < 0) {
		ast_log(LOG_ERROR, "Failed to add %s/%s to dialing structure.\n", tech, resource);
		return NULL;
	}

	ast_dial_set_global_timeout(dial, dial_data->timeout);

	res = ast_dial_run(dial, NULL, 0);

	if (res != AST_DIAL_RESULT_ANSWERED || !(new_chan = ast_dial_answered_steal(dial))) {
		return NULL;
	}

	if (!(bridge = ast_bridge_basic_new())) {
		ast_log(LOG_ERROR, "Failed to create basic bridge.\n");
		return NULL;
	}

	ast_bridge_impart(bridge, new_chan, NULL, NULL, 1);
	stasis_app_control_add_channel_to_bridge(control, bridge);

	return NULL;
}

int stasis_app_control_dial(struct stasis_app_control *control, const char *endpoint, int timeout)
{
	struct stasis_app_control_dial_data *dial_data;

	if (!(dial_data = ast_calloc(1, sizeof(*dial_data)))) {
		return -1;
	}

	ast_copy_string(dial_data->endpoint, endpoint, sizeof(dial_data->endpoint));

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

int control_is_done(struct stasis_app_control *control)
{
	/* Called from stasis_app_exec thread; no lock needed */
	return control->is_done;
}

struct stasis_app_control_continue_data {
	char context[AST_MAX_CONTEXT];
	char extension[AST_MAX_EXTENSION];
	int priority;
};

static void *app_control_continue(struct stasis_app_control *control,
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

	return NULL;
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

struct stasis_app_control_mute_data {
	enum ast_frame_type frametype;
	unsigned int direction;
};

static void *app_control_mute(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_control_mute_data *, mute_data, data, ast_free);
	SCOPED_CHANNELLOCK(lockvar, chan);

	ast_channel_suppress(control->channel, mute_data->direction, mute_data->frametype);

	return NULL;
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

static void *app_control_unmute(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	RAII_VAR(struct stasis_app_control_mute_data *, mute_data, data, ast_free);
	SCOPED_CHANNELLOCK(lockvar, chan);

	ast_channel_unsuppress(control->channel, mute_data->direction, mute_data->frametype);

	return NULL;
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
	SCOPED_CHANNELLOCK(lockvar, control->channel);

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

static void *app_control_hold(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_HOLD);

	return NULL;
}

void stasis_app_control_hold(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_hold, NULL);
}

static void *app_control_unhold(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_indicate(control->channel, AST_CONTROL_UNHOLD);

	return NULL;
}

void stasis_app_control_unhold(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_unhold, NULL);
}

static void *app_control_moh_start(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	char *moh_class = data;

	ast_moh_start(chan, moh_class, NULL);

	ast_free(moh_class);
	return NULL;
}

void stasis_app_control_moh_start(struct stasis_app_control *control, const char *moh_class)
{
	char *data = NULL;

	if (!ast_strlen_zero(moh_class)) {
		data = ast_strdup(moh_class);
	}

	stasis_app_send_command_async(control, app_control_moh_start, data);
}

static void *app_control_moh_stop(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_moh_stop(chan);
	return NULL;
}

void stasis_app_control_moh_stop(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_moh_stop, NULL);
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

void *stasis_app_send_command(struct stasis_app_control *control,
	stasis_app_command_cb command_fn, void *data)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);

	if (control == NULL) {
		return NULL;
	}

	command = exec_command(control, command_fn, data);
	if (!command) {
		return NULL;
	}

	return command_join(command);
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


static void bridge_after_cb(struct ast_channel *chan, void *data)
{
	struct stasis_app_control *control = data;
	SCOPED_AO2LOCK(lock, control);

	ast_debug(3, "%s, %s: Channel leaving bridge\n",
		ast_channel_uniqueid(chan), control->bridge->uniqueid);

	ast_assert(chan == control->channel);

	/* Restore the channel's PBX */
	ast_channel_pbx_set(control->channel, control->pbx);
	control->pbx = NULL;

	/* No longer in the bridge */
	control->bridge = NULL;

	/* Wakeup the command_queue loop */
	exec_command(control, NULL, NULL);
}

static void bridge_after_cb_failed(enum ast_bridge_after_cb_reason reason,
	void *data)
{
	struct stasis_app_control *control = data;

	bridge_after_cb(control->channel, data);

	ast_debug(3, "  reason: %s\n",
		ast_bridge_after_cb_reason_string(reason));
}

static int OK = 0;
static int FAIL = -1;

static void *app_control_add_channel_to_bridge(
	struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge *bridge = data;
	int res;

	if (!control || !bridge) {
		return NULL;
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
		return &FAIL;
	}

	{
		/* pbx and bridge are modified by the bridging impart thread.
		 * It shouldn't happen concurrently, but we still need to lock
		 * for the memory fence.
		 */
		SCOPED_AO2LOCK(lock, control);

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
			0); /* independent - false allows us to ast_bridge_depart() */

		if (res != 0) {
			ast_log(LOG_ERROR, "Error adding channel to bridge\n");
			ast_channel_pbx_set(chan, control->pbx);
			control->pbx = NULL;
			return &FAIL;
		}

		ast_assert(stasis_app_get_bridge(control) == NULL);
		control->bridge = bridge;
	}
	return &OK;
}

int stasis_app_control_add_channel_to_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge)
{
	int *res;
	ast_debug(3, "%s: Sending channel add_to_bridge command\n",
			stasis_app_control_get_channel_id(control));
	res = stasis_app_send_command(control,
		app_control_add_channel_to_bridge, bridge);
	return *res;
}

static void *app_control_remove_channel_from_bridge(
	struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	struct ast_bridge *bridge = data;

	if (!control) {
		return &FAIL;
	}

	/* We should only depart from our own bridge */
	ast_debug(3, "%s: Departing bridge %s\n",
		stasis_app_control_get_channel_id(control),
		bridge->uniqueid);

	if (bridge != stasis_app_get_bridge(control)) {
		ast_log(LOG_WARNING, "%s: Not in bridge %s; not removing\n",
			stasis_app_control_get_channel_id(control),
			bridge->uniqueid);
		return &FAIL;
	}

	ast_bridge_depart(chan);
	return &OK;
}

int stasis_app_control_remove_channel_from_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge)
{
	int *res;
	ast_debug(3, "%s: Sending channel remove_from_bridge command\n",
			stasis_app_control_get_channel_id(control));
	res = stasis_app_send_command(control,
		app_control_remove_channel_from_bridge, bridge);
	return *res;
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
