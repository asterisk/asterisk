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

#ifndef _ASTERISK_RES_STASIS_APP_H
#define _ASTERISK_RES_STASIS_APP_H

/*! \file
 *
 * \brief Internal API for the Stasis application controller.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/channel.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_app.h"

/*!
 * \brief Opaque pointer to \c res_stasis app structure.
 */
struct app;

/*!
 * \brief Create a res_stasis application.
 *
 * \param name Name of the application.
 * \param handler Callback for messages sent to the application.
 * \param data Data pointer provided to the callback.
 * \return New \c res_stasis application.
 * \return \c NULL on error.
 */
struct app *app_create(const char *name, stasis_app_cb handler, void *data);

/*!
 * \brief Update the handler and data for a \c res_stasis application.
 *
 * \param app Application to update.
 * \param handler New application callback.
 * \param data New data pointer for the callback.
 */
void app_update(struct app *app, stasis_app_cb handler, void *data);

const char *app_name(const struct app *app);

struct stasis_subscription *app_subscribe(struct app *app,
	struct stasis_topic *topic);

void app_send(struct app *app, struct ast_json *message);

int app_send_start_msg(struct app *app, struct ast_channel *chan, int argc,
	char *argv[]);

int app_send_end_msg(struct app *app, struct ast_channel *chan);

int app_is_watching_channel(struct app *app, const char *uniqueid);

int app_add_channel(struct app* app, const struct ast_channel *chan);

void app_remove_channel(struct app *app, const struct ast_channel *chan);

#endif /* _ASTERISK_RES_STASIS_APP_H */
