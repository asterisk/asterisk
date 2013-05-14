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
	void *retval;
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

static void command_complete(struct stasis_app_command *command, void *retval)
{
	SCOPED_MUTEX(lock, &command->lock);

	command->is_done = 1;
	command->retval = retval;
	ast_cond_signal(&command->condition);
}

void *command_join(struct stasis_app_command *command)
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
	void *retval = command->callback(control, chan, command->data);
	command_complete(command, retval);
}

