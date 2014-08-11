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
 * \brief Stasis application command support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "command.h"

#include "asterisk/lock.h"
#include "asterisk/stasis_app_impl.h"

struct stasis_app_command {
	ast_mutex_t lock;
	ast_cond_t condition;
	stasis_app_command_cb callback;
	void *data;
	int retval;
	int is_done:1;
};

static void command_dtor(void *obj)
{
	struct stasis_app_command *command = obj;
	ast_mutex_destroy(&command->lock);
	ast_cond_destroy(&command->condition);
}

struct stasis_app_command *command_create(
	stasis_app_command_cb callback, void *data)
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

void command_complete(struct stasis_app_command *command, int retval)
{
	SCOPED_MUTEX(lock, &command->lock);

	command->is_done = 1;
	command->retval = retval;
	ast_cond_signal(&command->condition);
}

int command_join(struct stasis_app_command *command)
{
	SCOPED_MUTEX(lock, &command->lock);
	while (!command->is_done) {
		ast_cond_wait(&command->condition, &command->lock);
	}

	return command->retval;
}

void command_invoke(struct stasis_app_command *command,
	struct stasis_app_control *control, struct ast_channel *chan)
{
	int retval = command->callback(control, chan, command->data);
	command_complete(command, retval);
}

static void command_queue_prestart_destroy(void *obj)
{
	/* Clean up the container */
	ao2_cleanup(obj);
}

static const struct ast_datastore_info command_queue_prestart = {
	.type = "stasis-command-prestart-queue",
	.destroy = command_queue_prestart_destroy,
};

int command_prestart_queue_command(struct ast_channel *chan,
	stasis_app_command_cb command_fn, void *data)
{
	struct ast_datastore *datastore;
	struct ao2_container *command_queue;
	RAII_VAR(struct stasis_app_command *, command,
		command_create(command_fn, data), ao2_cleanup);

	if (!command) {
		return -1;
	}

	datastore = ast_channel_datastore_find(chan, &command_queue_prestart, NULL);
	if (datastore) {
		command_queue = datastore->data;
		ao2_link(command_queue, command);
		return 0;
	}

	command_queue = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	if (!command_queue) {
		return -1;
	}

	datastore = ast_datastore_alloc(&command_queue_prestart, NULL);
	if (!datastore) {
		ao2_cleanup(command_queue);
		return -1;
	}
	ast_channel_datastore_add(chan, datastore);

	datastore->data = command_queue;
	ao2_link(command_queue, command);

	return 0;
}

struct ao2_container *command_prestart_get_container(struct ast_channel *chan)
{
	struct ast_datastore *datastore = ast_channel_datastore_find(chan, &command_queue_prestart, NULL);

	if (!datastore) {
		return NULL;
	}

	return ao2_bump(datastore->data);
}
