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

struct stasis_app_control {
	/*! Queue of commands to dispatch on the channel */
	struct ao2_container *command_queue;
	/*!
	 * When set, /c app_stasis should exit and continue in the dialplan.
	 */
	int is_done:1;
	/*!
	 * The associated channel.
	 * Be very careful with the threading associated w/ manipulating
	 * the channel.
	 */
	struct ast_channel *channel;
};

struct stasis_app_control *control_create(struct ast_channel *channel)
{
	struct stasis_app_control *control;

	control = ao2_alloc(sizeof(*control), NULL);
	if (!control) {
		return NULL;
	}

	control->command_queue = ao2_container_alloc_list(
		AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);

	control->channel = channel;

	return control;
}

static struct stasis_app_command *exec_command(
	struct stasis_app_control *control, stasis_app_command_cb command_fn,
	void *data)
{
	RAII_VAR(struct stasis_app_command *, command, NULL, ao2_cleanup);

	command = command_create(command_fn, data);

	if (!command) {
		return NULL;
	}

	/* command_queue is a thread safe list; no lock needed */
	ao2_link(control->command_queue, command);

	ao2_ref(command, +1);
	return command;
}

int control_is_done(struct stasis_app_control *control)
{
	/* Called from stasis_app_exec thread; no lock needed */
	return control->is_done;
}

static void *app_control_continue(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	/* Called from stasis_app_exec thread; no lock needed */
	control->is_done = 1;
	return NULL;
}

void stasis_app_control_continue(struct stasis_app_control *control)
{
	stasis_app_send_command_async(control, app_control_continue, NULL);
}

struct ast_channel_snapshot *stasis_app_control_get_snapshot(
	const struct stasis_app_control *control)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct stasis_caching_topic *caching_topic;
	struct ast_channel_snapshot *snapshot;

	caching_topic = ast_channel_topic_all_cached();
	ast_assert(caching_topic != NULL);

	msg = stasis_cache_get(caching_topic, ast_channel_snapshot_type(),
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
