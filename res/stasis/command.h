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

#ifndef _ASTERISK_RES_STASIS_COMMAND_H
#define _ASTERISK_RES_STASIS_COMMAND_H

/*! \file
 *
 * \brief Internal API for the Stasis application commands.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/stasis_app_impl.h"

struct stasis_app_command;

struct stasis_app_command *command_create(
	stasis_app_command_cb callback, void *data,
	command_data_destructor_fn data_destructor);

void command_complete(struct stasis_app_command *command, int retval);

void command_invoke(struct stasis_app_command *command,
	struct stasis_app_control *control, struct ast_channel *chan);

int command_join(struct stasis_app_command *command);

/*!
 * \brief Queue a Stasis() prestart command for a channel
 *
 * \pre chan must be locked
 *
 * \param chan The channel on which to queue the prestart command
 * \param command_fn The callback to call for the command
 * \param data The data to pass to the command callback
 * \param data_destructor Optional function which will be called on
 *        the data in either the event of command completion or failure
 *        to schedule or complete the command
 *
 * \retval zero on success
 * \retval non-zero on failure
 */
int command_prestart_queue_command(struct ast_channel *chan,
	stasis_app_command_cb command_fn, void *data,
	command_data_destructor_fn data_destructor);

/*!
 * \brief Get the Stasis() prestart commands for a channel
 *
 * \pre chan must be locked
 *
 * \param chan The channel from which to get prestart commands
 *
 * \return The command prestart container for chan (must be ao2_cleanup()'d)
 */
struct ao2_container *command_prestart_get_container(struct ast_channel *chan);


#endif /* _ASTERISK_RES_STASIS_CONTROL_H */
