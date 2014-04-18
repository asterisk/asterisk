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


#endif /* _ASTERISK_RES_STASIS_CONTROL_H */
