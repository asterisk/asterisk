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

#ifndef _ASTERISK_RES_STASIS_H
#define _ASTERISK_RES_STASIS_H

/*! \file
 *
 * \brief Backend API for implementing components of res_stasis.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 *
 * This file defines functions useful for defining new commands to execute
 * on channels while they are in Stasis.
 */

#include "asterisk/stasis_app.h"

/*!
 * \since 12
 * \brief Control a channel using \c stasis_app.
 *
 * This function blocks until the channel hangs up, or
 * stasis_app_control_continue() is called on the channel's \ref
 * stasis_app_control struct.
 *
 * \param chan Channel to control with Stasis.
 * \param app_name Application controlling the channel.
 * \param argc Number of arguments for the application.
 * \param argv Arguments for the application.
 */
int stasis_app_exec(struct ast_channel *chan, const char *app_name, int argc,
	char *argv[]);

/*!
 * \brief Typedef for data destructor for stasis app commands
 *
 * \param data Data to destroy.
 *
 * \details
 * This is called during destruction of the command or if we fail to schedule
 * a command. It is passed a pointer to the user-defined data of the command.
 *
 * \return Nothing
 */
typedef void (*command_data_destructor_fn)(void *data);

/*! Callback type for stasis app commands */
typedef int (*stasis_app_command_cb)(struct stasis_app_control *control,
	struct ast_channel *chan, void *data);

/*!
 * \since 12
 * \brief Invokes a \a command on a \a control's channel.
 *
 * This function dispatches the command to be executed in the context of
 * stasis_app_exec(), so this command will block waiting for the results of
 * the command.
 *
 * \param control Control object for the channel to send the command to.
 * \param command Command function to execute.
 * \param data Optional data to pass along with the control function.
 * \param data_destructor Optional function which will be called on
 *        the data in either the event of command completion or failure
 *        to schedule or complete the command
 *
 * \return zero on success.
 * \return error code otherwise.
 */
int stasis_app_send_command(struct stasis_app_control *control,
	stasis_app_command_cb command, void *data, command_data_destructor_fn data_destructor);

/*!
 * \since 12
 * \brief Asynchronous version of stasis_app_send_command().
 *
 * This function enqueues a command for execution, but returns immediately
 * without waiting for the response.
 *
 * \param control Control object for the channel to send the command to.
 * \param command Command function to execute.
 * \param data Optional data to pass along with the control function.
 * \param data_destructor Optional function which will be called on
 *        the data in either the event of command completion or failure
 *        to schedule or complete the command
 * \return 0 on success.
 * \return Non-zero on error.
 */
int stasis_app_send_command_async(struct stasis_app_control *control,
	stasis_app_command_cb command, void *data, command_data_destructor_fn data_destructor);

#endif /* _ASTERISK_RES_STASIS_H */
