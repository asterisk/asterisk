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

#ifndef _ASTERISK_STASIS_MESSAGE_ROUTER_H
#define _ASTERISK_STASIS_MESSAGE_ROUTER_H

/*!
 * \brief A simplistic router for \ref stasis_message's.
 *
 * Often times, when subscribing to a topic, one wants to handle different
 * message types differently. While one could cascade if/else statements through
 * the subscription handler, it is much cleaner to specify a different callback
 * for each message type. The \ref stasis_message_router is here to help!
 *
 * A \ref stasis_message_router is constructed for a particular \ref
 * stasis_topic, which is subscribes to. Call
 * stasis_message_router_unsubscribe() to cancel that subscription.
 *
 * Once constructed, routes can be added using stasis_message_router_add() (or
 * stasis_message_router_set_default() for any messages not handled by other
 * routes). There may be only one route per \ref stasis_message_type. The
 * route's \a callback is invoked just as if it were a callback for a
 * subscription; but it only gets called for messages of the specified type.
 *
 * \since 12
 */

#include "asterisk/stasis.h"

/*! \brief Stasis message routing object */
struct stasis_message_router;

/*!
 * \brief Create a new message router object.
 * \param topic Topic to subscribe route to.
 * \return New \ref stasis_message_router.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_message_router *stasis_message_router_create(
	struct stasis_topic *topic);

/*!
 * \brief Unsubscribe the router from the upstream topic.
 *
 * \param router Router to unsubscribe.
 * \since 12
 */
void stasis_message_router_unsubscribe(struct stasis_message_router *router);

/*!
 * \brief Unsubscribe the router from the upstream topic, blocking until the
 * final message has been processed.
 *
 * See stasis_unsubscribe_and_join() for info on when to use this
 * vs. stasis_message_router_unsubscribe().
 *
 * \param router Router to unsubscribe.
 * \since 12
 */
void stasis_message_router_unsubscribe_and_join(
	struct stasis_message_router *router);

/*!
 * \brief Returns whether \a router has received its final message.
 *
 * \param router Router.
 * \return True (non-zero) if stasis_subscription_final_message() has been
 *         received.
 * \return False (zero) if waiting for the end.
 */
int stasis_message_router_is_done(struct stasis_message_router *router);

/*!
 * \brief Add a route to a message router.
 * \param router Router to add the route to.
 * \param message_type Type of message to route.
 * \param callback Callback to forard messages of \a message_type to.
 * \param data Data pointer to pass to \a callback.
 * \since 12
 */
int stasis_message_router_add(struct stasis_message_router *router,
			      struct stasis_message_type *message_type,
			      stasis_subscription_cb callback,
			      void *data);

/*!
 * \brief Sets the default route of a router.
 * \param router Router to set the default route of.
 * \param callback Callback to forard messages which otherwise have no home.
 * \param data Data pointer to pass to \a callback.
 * \since 12
 */
int stasis_message_router_set_default(struct stasis_message_router *router,
				      stasis_subscription_cb callback,
				      void *data);

#endif /* _ASTERISK_STASIS_MESSAGE_ROUTER_H */
