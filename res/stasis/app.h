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
 * \brief Deactivates an application.
 *
 * Any channels currently in the application remain active (since the app might
 * come back), but new channels are rejected.
 *
 * \param app Application to deactivate.
 */
void app_deactivate(struct app *app);

/*!
 * \brief Checks whether an app is active.
 *
 * \param app Application to check.
 * \return True (non-zero) if app is active.
 * \return False (zero) if app has been deactivated.
 */
int app_is_active(struct app *app);

/*!
 * \brief Checks whether a deactivated app has no channels.
 *
 * \param app Application to check.
 * \param True (non-zero) if app is deactivated, and has no associated channels.
 * \param False (zero) otherwise.
 */
int app_is_finished(struct app *app);

/*!
 * \brief Update the handler and data for a \c res_stasis application.
 *
 * If app has been deactivated, this will reactivate it.
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

/*!
 * \brief Add a bridge to an application's watch list by uniqueid.
 *
 * \param app Application.
 * \param bridge Bridge to watch.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_add_bridge(struct app *app, const char *uniqueid);

/*!
 * \brief Remove a bridge from an application's watch list by uniqueid.
 *
 * \param app Application.
 * \param bridge Bridge to remove.
 */
void app_remove_bridge(struct app* app, const char *uniqueid);

/*!
 * \brief Checks if an application is watching a given bridge.
 *
 * \param app Application.
 * \param uniqueid Uniqueid of the bridge to check.
 * \return True (non-zero) if \a app is watching bridge with given \a uniqueid
 * \return False (zero) if \a app isn't.
 */
int app_is_watching_bridge(struct app *app, const char *uniqueid);

#endif /* _ASTERISK_RES_STASIS_APP_H */
