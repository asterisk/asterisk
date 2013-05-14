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
 * \return New control object.
 * \return \c NULL on error.
 */
struct stasis_app_control *control_create(struct ast_channel *channel);

/*!
 * \brief Dispatch all commands enqueued to this control.
 *
 * \param control Control object to dispatch.
 * \param chan Associated channel.
 * \return Number of commands executed
 */
int control_dispatch_all(struct stasis_app_control *control,
	struct ast_channel *chan);

int control_is_done(struct stasis_app_control *control);

void control_continue(struct stasis_app_control *control);

#endif /* _ASTERISK_RES_STASIS_CONTROL_H */
