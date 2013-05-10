/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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

#ifndef _ASTERISK_STASIS_APP_H
#define _ASTERISK_STASIS_APP_H

/*! \file
 *
 * \brief Stasis Application API. See \ref res_stasis "Stasis Application API"
 * for detailed documentation.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 *
 * \page res_stasis Stasis Application API
 *
 * This is the API that binds the Stasis dialplan application to external
 * Stasis applications, such as \c res_stasis_websocket.
 *
 * The associated \c res_stasis module registers a dialplan function named \c
 * Stasis, which uses \c res_stasis to put a channel into the named Stasis
 * app. As a channel enters and leaves the Stasis diaplan application, the
 * Stasis app receives a \c 'stasis-start' and \c 'stasis-end' events.
 *
 * Stasis apps register themselves using the \ref stasis_app_register and
 * stasis_app_unregister functions. Messages are sent to an appliction using
 * \ref stasis_app_send.
 *
 * Finally, Stasis apps control channels through the use of the \ref
 * stasis_app_control object, and the family of \c stasis_app_control_*
 * functions.
 *
 * Since module unload order is based on reference counting, any module that
 * uses the API defined in this file must call stasis_app_ref() when loaded,
 * and stasis_app_unref() when unloaded.
 */

#include "asterisk/channel.h"
#include "asterisk/json.h"

struct ast_channel_snapshot;

/*! @{ */

/*!
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

/*! @} */

/*! @{ */

/*!
 * \brief Callback for Stasis application handler.
 *
 * The message given to the handler is a borrowed copy. If you want to keep a
 * reference to it, you should use \c ao2_ref() to keep it around.
 *
 * \param data Data ptr given when registered.
 * \param app_name Name of the application being dispatched to.
 * \param message Message to handle. (borrowed copy)
 */
typedef void (*stasis_app_cb)(void *data, const char *app_name,
			      struct ast_json *message);

/*!
 * \brief Register a new Stasis application.
 *
 * If an application is already registered with the given name, the old
 * application is sent a 'replaced' message and unregistered.
 *
 * \param app_name Name of this application.
 * \param handler Callback for application messages.
 * \param data Data blob to pass to the callback. Must be AO2 managed.
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_register(const char *app_name, stasis_app_cb handler, void *data);

/*!
 * \brief Unregister a Stasis application.
 * \param app_name Name of the application to unregister.
 */
void stasis_app_unregister(const char *app_name);

/*!
 * \brief Send a message to the given Stasis application.
 *
 * The message given to the handler is a borrowed copy. If you want to keep a
 * reference to it, you should use \c ao2_ref() to keep it around.
 *
 * \param app_name Name of the application to invoke.
 * \param message Message to send (borrowed reference)
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_send(const char *app_name, struct ast_json *message);

/*! @} */

/*! @{ */

/*! \brief Handler for controlling a channel that's in a Stasis application */
struct stasis_app_control;

/*!
 * \brief Returns the handler for the given channel.
 * \param chan Channel to handle.
 * \return NULL channel not in Stasis application.
 * \return Pointer to \c res_stasis handler.
 */
struct stasis_app_control *stasis_app_control_find_by_channel(
	const struct ast_channel *chan);

/*!
 * \brief Returns the handler for the channel with the given id.
 * \param channel_id Uniqueid of the channel.
 * \return NULL channel not in Stasis application, or channel does not exist.
 * \return Pointer to \c res_stasis handler.
 */
struct stasis_app_control *stasis_app_control_find_by_channel_id(
	const char *channel_id);

/*!
 * \brief Exit \c res_stasis and continue execution in the dialplan.
 *
 * If the channel is no longer in \c res_stasis, this function does nothing.
 *
 * \param control Control for \c res_stasis
 */
void stasis_app_control_continue(struct stasis_app_control *control);

/*!
 * \brief Answer the channel associated with this control.
 * \param control Control for \c res_stasis.
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_control_answer(struct stasis_app_control *control);

/*! @} */

/*! @{ */

/*!
 * \brief Build a JSON object from a \ref ast_channel_snapshot.
 * \return JSON object representing channel snapshot.
 * \return \c NULL on error
 */
struct ast_json *ast_channel_snapshot_to_json(const struct ast_channel_snapshot *snapshot);

/*!
 * \brief Increment the res_stasis reference count.
 *
 * This ensures graceful shutdown happens in the proper order.
 */
void stasis_app_ref(void);

/*!
 * \brief Decrement the res_stasis reference count.
 *
 * This ensures graceful shutdown happens in the proper order.
 */
void stasis_app_unref(void);

/*! @} */

#endif /* _ASTERISK_STASIS_APP_H */
