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

/*!
 * \brief Return an application's name.
 *
 * \param app Application.
 * \return Name of the application.
 * \return \c NULL is \a app is \c NULL.
 */
const char *app_name(const struct app *app);

/*!
 * \brief Subscribe an application to a topic.
 *
 * \param app Application.
 * \param topic Topic to subscribe to.
 * \return New subscription.
 * \return \c NULL on error.
 */
struct stasis_subscription *app_subscribe(struct app *app,
	struct stasis_topic *topic);

/*!
 * \brief Send a message to an application.
 *
 * \param app Application.
 * \param message Message to send.
 */
void app_send(struct app *app, struct ast_json *message);

/*!
 * \brief Send the start message to an application.
 *
 * \param app Application.
 * \param chan The channel entering the application.
 * \param argc The number of arguments for the application.
 * \param argv The arguments for the application.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_send_start_msg(struct app *app, struct ast_channel *chan, int argc,
	char *argv[]);

/*!
 * \brief Send the end message to an application.
 *
 * \param app Application.
 * \param chan The channel leaving the application.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_send_end_msg(struct app *app, struct ast_channel *chan);

/*!
 * \brief Checks if an application is watching a given channel.
 *
 * \param app Application.
 * \param uniqueid Uniqueid of the channel to check about.
 * \return True (non-zero) if \a app is watching channel with given \a uniqueid
 * \return False (zero) if \a app isn't.
 */
int app_is_watching_channel(struct app *app, const char *uniqueid);

/*!
 * \brief Add a channel to an application's watch list.
 *
 * \param app Application.
 * \param chan Channel to watch.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_add_channel(struct app *app, const struct ast_channel *chan);

/*!
 * \brief Remove a channel from an application's watch list.
 *
 * \param app Application.
 * \param chan Channel to watch.
 */
void app_remove_channel(struct app *app, const struct ast_channel *chan);

#endif /* _ASTERISK_RES_STASIS_APP_H */
