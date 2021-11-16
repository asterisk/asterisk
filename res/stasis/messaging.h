/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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

#ifndef _ASTERISK_RES_STASIS_MESSAGING_H
#define _ASTERISK_RES_STASIS_MESSAGING_H

/*!
 * \file
 *
 * \brief Stasis out-of-call text message support
 *
 * \author Matt Jordan <mjordan@digium.com>
 * \since 12.4.0
 */

/*!
 * \brief Callback handler for when a message is received from the core
 *
 * \param endpoint_id The ID of the endpoint that we received the message from
 * \param json_msg JSON representation of the text message
 * \param pvt ao2 ref counted pvt passed during registration
 *
 * \retval 0 the message was handled
 * \retval non-zero the message was not handled
 */
typedef int (* message_received_cb)(const char *endpoint_id, struct ast_json *json_msg, void *pvt);

/*!
 * \brief Subscribe for messages from a particular endpoint
 *
 * \param app_name Name of the stasis application to unsubscribe from messaging
 * \param endpoint_id The ID of the endpoint we no longer care about
 */
void messaging_app_unsubscribe_endpoint(const char *app_name, const char *endpoint_id);

/*!
 * \brief Subscribe an application to an endpoint for messages
 *
 * \param app_name The name of the \ref stasis application to subscribe to \c endpoint
 * \param endpoint The endpoint object to subscribe to
 * \param callback The callback to call when a message is received
 * \param pvt An ao2 ref counted object that will be passed to the callback.
 *
 * \retval 0 subscription was successful
 * \retval -1 subscription failed
 */
int messaging_app_subscribe_endpoint(const char *app_name, struct ast_endpoint *endpoint, message_received_cb callback, void *pvt);

/*!
 * \brief Tidy up the messaging layer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int messaging_cleanup(void);

/*!
 * \brief Initialize the messaging layer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int messaging_init(void);

#endif /* #define _ASTERISK_RES_STASIS_MESSAGING_H  */
