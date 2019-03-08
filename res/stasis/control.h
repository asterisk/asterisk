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

#ifndef _ASTERISK_RES_STASIS_CONTROL_H
#define _ASTERISK_RES_STASIS_CONTROL_H

/*! \file
 *
 * \brief Internal API for the Stasis application controller.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/stasis_app.h"

/*!
 * \brief Create a control object.
 *
 * \param channel Channel to control.
 * \param app stasis_app for which this control is being created.
 *
 * \return New control object.
 * \return \c NULL on error.
 */
struct stasis_app_control *control_create(struct ast_channel *channel, struct stasis_app *app);

/*!
 * \brief Flush the control command queue.
 * \since 13.9.0
 *
 * \param control Control object to flush command queue.
 *
 * \return Nothing
 */
void control_flush_queue(struct stasis_app_control *control);

/*!
 * \brief Dispatch all commands enqueued to this control.
 *
 * \param control Control object to dispatch.
 * \param chan Associated channel.
 * \return Number of commands executed
 */
int control_dispatch_all(struct stasis_app_control *control,
	struct ast_channel *chan);

/*!
 * \brief Blocks until \a control's command queue has a command available.
 *
 * \param control Control to block on.
 */
void control_wait(struct stasis_app_control *control);

/*!
 * \brief Returns the count of items in a control's command queue.
 *
 * \param control Control to count commands on
 *
 * \retval number of commands in the command que
 */
int control_command_count(struct stasis_app_control *control);

/*!
 * \brief Returns true if control_continue() has been called on this \a control.
 *
 * \param control Control to query.
 * \return True (non-zero) if control_continue() has been called.
 * \return False (zero) otherwise.
 */
int control_is_done(struct stasis_app_control *control);

void control_mark_done(struct stasis_app_control *control);

/*!
 * \brief Dispatch all queued prestart commands
 *
 * \param control The control for chan
 * \param channel The channel on which commands should be executed
 *
 * \return The number of commands executed
 */
int control_prestart_dispatch_all(struct stasis_app_control *control,
	struct ast_channel *chan);

/*!
 * \brief Returns the pointer (non-reffed) to the app associated with this control
 *
 * \param control Control to query.
 *
 * \returns A pointer to the associated stasis_app
 */
struct stasis_app *control_app(struct stasis_app_control *control);

/*!
 * \brief Set the application the control object belongs to
 *
 * \param control The control for the channel
 * \param app The application this control will now belong to
 *
 * \note This will unref control's previous app by 1, and bump app by 1
 */
void control_set_app(struct stasis_app_control *control, struct stasis_app *app);

/*!
 * \brief Returns the name of the application we are moving to
 *
 * \param control The control for the channel
 *
 * \return The name of the application we are moving to
 */
char *control_next_app(struct stasis_app_control *control);

/*!
 * \brief Free any memory that was allocated for switching applications via
 * /channels/{channelId}/move
 *
 * \param control The control for the channel
 */
void control_move_cleanup(struct stasis_app_control *control);

/*!
 * \brief Returns the list of arguments to pass to the application we are moving to
 *
 * \note If you wish to get the size of the list, control_next_app_args_size should be
 * called before this, as this function will steal the elements from the string vector
 * and set the size to 0.
 *
 * \param control The control for the channel
 *
 * \return The arguments to pass to the application we are moving to
 */
char **control_next_app_args(struct stasis_app_control *control);

/*!
 * \brief Returns the number of arguments to be passed to the application we are moving to
 *
 * \note This should always be called before control_next_app_args, as calling that function
 * will steal all elements from the string vector and set the size to 0.
 *
 * \param control The control for the channel
 *
 * \return The number of arguments to be passed to the application we are moving to
 */
int control_next_app_args_size(struct stasis_app_control *control);

/*!
 * \brief Command callback for adding a channel to a bridge
 *
 * \param control The control for chan
 * \param chan The channel on which commands should be executed
 * \param data Bridge to be passed to the callback
 */
int control_add_channel_to_bridge(struct stasis_app_control *control, struct ast_channel *chan, void *data);

/*!
 * \brief Command for swapping a channel in a bridge
 *
 * \param control The control for chan
 * \param chan The channel on which commands should be executed
 * \param bridge Bridge to be passed to the callback
 * \param swap Channel to swap with when joining the bridge
 */
int control_swap_channel_in_bridge(struct stasis_app_control *control, struct ast_bridge *bridge, struct ast_channel *chan, struct ast_channel *swap);

/*!
 * \brief Stop playing silence to a channel right now.
 * \since 13.9.0
 *
 * \param control The control for chan
 */
void control_silence_stop_now(struct stasis_app_control *control);


#endif /* _ASTERISK_RES_STASIS_CONTROL_H */
