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
	stasis_app_command_cb callback, void *data);

void command_invoke(struct stasis_app_command *command,
	struct stasis_app_control *control, struct ast_channel *chan);

void *command_join(struct stasis_app_command *command);

#endif /* _ASTERISK_RES_STASIS_CONTROL_H */
