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
struct stasis_app;

/*!
 * \brief Create a res_stasis application.
 *
 * \param name Name of the application.
 * \param handler Callback for messages sent to the application.
 * \param data Data pointer provided to the callback.
 * \return New \c res_stasis application.
 * \return \c NULL on error.
 */
struct stasis_app *app_create(const char *name, stasis_app_cb handler, void *data);

/*!
 * \brief Tears down an application.
 *
 * It should be finished before calling this.
 *
 * \param app Application to unsubscribe.
 */
void app_shutdown(struct stasis_app *app);

/*!
 * \brief Deactivates an application.
 *
 * Any channels currently in the application remain active (since the app might
 * come back), but new channels are rejected.
 *
 * \param app Application to deactivate.
 */
void app_deactivate(struct stasis_app *app);

/*!
 * \brief Checks whether an app is active.
 *
 * \param app Application to check.
 * \return True (non-zero) if app is active.
 * \return False (zero) if app has been deactivated.
 */
int app_is_active(struct stasis_app *app);

/*!
 * \brief Checks whether a deactivated app has no channels.
 *
 * \param app Application to check.
 * \param True (non-zero) if app is deactivated, and has no associated channels.
 * \param False (zero) otherwise.
 */
int app_is_finished(struct stasis_app *app);

/*!
 * \brief Update the handler and data for a \c res_stasis application.
 *
 * If app has been deactivated, this will reactivate it.
 *
 * \param app Application to update.
 * \param handler New application callback.
 * \param data New data pointer for the callback.
 */
void app_update(struct stasis_app *app, stasis_app_cb handler, void *data);

/*!
 * \brief Return an application's name.
 *
 * \param app Application.
 * \return Name of the application.
 * \return \c NULL is \a app is \c NULL.
 */
const char *app_name(const struct stasis_app *app);

/*!
 * \brief Send a message to an application.
 *
 * \param app Application.
 * \param message Message to send.
 */
void app_send(struct stasis_app *app, struct ast_json *message);

struct app_forwards;

struct ast_json *app_to_json(const struct stasis_app *app);

/*!
 * \brief Subscribes an application to a channel.
 *
 * \param app Application.
 * \param chan Channel to subscribe to.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_subscribe_channel(struct stasis_app *app, struct ast_channel *chan);

/*!
 * \brief Cancel the subscription an app has for a channel.
 *
 * \param app Subscribing application.
 * \param chan Channel to unsubscribe from.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_unsubscribe_channel(struct stasis_app *app, struct ast_channel *chan);

/*!
 * \brief Cancel the subscription an app has for a channel.
 *
 * \param app Subscribing application.
 * \param channel_id Id of channel to unsubscribe from.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_unsubscribe_channel_id(struct stasis_app *app, const char *channel_id);

/*!
 * \brief Test if an app is subscribed to a channel.
 *
 * \param app Subscribing application.
 * \param channel_id Id of channel to check.
 * \return True (non-zero) if channel is subscribed to \a app.
 * \return False (zero) if channel is not subscribed.
 */
int app_is_subscribed_channel_id(struct stasis_app *app, const char *channel_id);

/*!
 * \brief Add a bridge subscription to an existing channel subscription.
 *
 * \param app Application.
 * \param bridge Bridge to subscribe to.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_subscribe_bridge(struct stasis_app *app, struct ast_bridge *bridge);

/*!
 * \brief Cancel the bridge subscription for an application.
 *
 * \param forwards Return from app_subscribe_channel().
 * \param bridge Bridge to subscribe to.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_unsubscribe_bridge(struct stasis_app *app, struct ast_bridge *bridge);

/*!
 * \brief Cancel the subscription an app has for a bridge.
 *
 * \param app Subscribing application.
 * \param bridge_id Id of bridge to unsubscribe from.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_unsubscribe_bridge_id(struct stasis_app *app, const char *bridge_id);

/*!
 * \brief Test if an app is subscribed to a bridge.
 *
 * \param app Subscribing application.
 * \param bridge_id Id of bridge to check.
 * \return True (non-zero) if bridge is subscribed to \a app.
 * \return False (zero) if bridge is not subscribed.
 */
int app_is_subscribed_bridge_id(struct stasis_app *app, const char *bridge_id);

/*!
 * \brief Subscribes an application to a endpoint.
 *
 * \param app Application.
 * \param chan Endpoint to subscribe to.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_subscribe_endpoint(struct stasis_app *app, struct ast_endpoint *endpoint);

/*!
 * \brief Cancel the subscription an app has for a endpoint.
 *
 * \param app Subscribing application.
 * \param endpoint_id Id of endpoint to unsubscribe from.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int app_unsubscribe_endpoint_id(struct stasis_app *app, const char *endpoint_id);

/*!
 * \brief Test if an app is subscribed to a endpoint.
 *
 * \param app Subscribing application.
 * \param endpoint_id Id of endpoint to check.
 * \return True (non-zero) if endpoint is subscribed to \a app.
 * \return False (zero) if endpoint is not subscribed.
 */
int app_is_subscribed_endpoint_id(struct stasis_app *app, const char *endpoint_id);

/*!
 * \brief Set the snapshot of the channel that this channel will replace
 *
 * \param channel The channel on which this will be set
 * \param replace_snapshot The snapshot of the channel that is being replaced
 *
 * \retval zero success
 * \retval non-zero failure
 */
int app_set_replace_channel_snapshot(struct ast_channel *chan, struct ast_channel_snapshot *replace_snapshot);

/*!
 * \brief Set the app that the replacement channel will be controlled by
 *
 * \param channel The channel on which this will be set
 * \param replace_app The app that will be controlling this channel
 *
 * \retval zero success
 * \retval non-zero failure
 */
int app_set_replace_channel_app(struct ast_channel *chan, const char *replace_app);

/*!
 * \brief Get the app that the replacement channel will be controlled by
 *
 * \param channel The channel on which this will be set
 *
 * \retval NULL on error
 * \return the name of the controlling app (must be ast_free()d)
 */
char *app_get_replace_channel_app(struct ast_channel *chan);

/*!
 * \brief Send StasisEnd message to the listening app
 *
 * \param app The app that owns the channel
 * \param chan The channel for which the message is being sent
 *
 * \retval zero on success
 * \return non-zero on failure
 */
int app_send_end_msg(struct stasis_app *app, struct ast_channel *chan);

#endif /* _ASTERISK_RES_STASIS_APP_H */
