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
 *
 * \param topic Topic to subscribe route to.
 *
 * \return New \ref stasis_message_router.
 * \return \c NULL on error.
 *
 * \since 12
 */
struct stasis_message_router *__stasis_message_router_create(
	struct stasis_topic *topic, const char *file, int lineno, const char *func);
#ifdef AST_DEVMODE
#define stasis_message_router_create(topic) __stasis_message_router_create(topic, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#else
struct stasis_message_router *stasis_message_router_create(
	struct stasis_topic *topic);
#endif

/*!
 * \brief Create a new message router object.
 *
 * The subscription created for this message router will dispatch
 * callbacks on a thread pool.
 *
 * \param topic Topic to subscribe route to.
 *
 * \return New \ref stasis_message_router.
 * \return \c NULL on error.
 *
 * \since 12.8.0
 */
struct stasis_message_router *__stasis_message_router_create_pool(
	struct stasis_topic *topic, const char *file, int lineno, const char *func);
#ifdef AST_DEVMODE
#define stasis_message_router_create_pool(topic) __stasis_message_router_create_pool(topic, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#else
struct stasis_message_router *stasis_message_router_create_pool(
	struct stasis_topic *topic);
#endif

/*!
 * \brief Unsubscribe the router from the upstream topic.
 *
 * \param router Router to unsubscribe.
 *
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
 *
 * \since 12
 */
void stasis_message_router_unsubscribe_and_join(
	struct stasis_message_router *router);

/*!
 * \brief Returns whether \a router has received its final message.
 *
 * \param router Router.
 *
 * \return True (non-zero) if stasis_subscription_final_message() has been
 *         received.
 * \return False (zero) if waiting for the end.
 */
int stasis_message_router_is_done(struct stasis_message_router *router);

/*!
 * \brief Publish a message to a message router's subscription synchronously
 *
 * \param router Router
 * \param message The \ref stasis message
 *
 * This should be used when a message needs to be published synchronously to
 * the underlying subscription created by a message router. This is analagous
 * to \ref stasis_publish_sync.
 *
 * Note that the caller will be blocked until the thread servicing the message
 * on the message router's subscription completes handling of the message.
 *
 * \since 12.1.0
 */
void stasis_message_router_publish_sync(struct stasis_message_router *router,
	struct stasis_message *message);

/*!
 * \brief Set the high and low alert water marks of the stasis message router.
 * \since 13.10.0
 *
 * \param router Pointer to a stasis message router
 * \param low_water New queue low water mark. (-1 to set as 90% of high_water)
 * \param high_water New queue high water mark.
 *
 * \retval 0 on success.
 * \retval -1 on error (water marks not changed).
 */
int stasis_message_router_set_congestion_limits(struct stasis_message_router *router,
	long low_water, long high_water);

/*!
 * \brief Add a route to a message router.
 *
 * A particular \a message_type may have at most one route per \a router. If
 * you route \ref stasis_cache_update messages, the callback will only receive
 * updates for types not handled by routes added with
 * stasis_message_router_add_cache_update().
 *
 * Adding multiple routes for the same message type results in undefined
 * behavior.
 *
 * \param router Router to add the route to.
 * \param message_type Type of message to route.
 * \param callback Callback to forard messages of \a message_type to.
 * \param data Data pointer to pass to \a callback.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \since 12
 */
int stasis_message_router_add(struct stasis_message_router *router,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data);

/*!
 * \brief Add a route for \ref stasis_cache_update messages to a message router.
 *
 * A particular \a message_type may have at most one cache route per \a router.
 * These are distinct from regular routes, so one could have both a regular
 * route and a cache route for the same \a message_type.
 *
 * Adding multiple routes for the same message type results in undefined
 * behavior.
 *
 * \param router Router to add the route to.
 * \param message_type Subtype of cache update to route.
 * \param callback Callback to forard messages of \a message_type to.
 * \param data Data pointer to pass to \a callback.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \since 12
 */
int stasis_message_router_add_cache_update(struct stasis_message_router *router,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data);

/*!
 * \brief Remove a route from a message router.
 *
 * If a route is removed from another thread, there is no notification that
 * all messages using this route have been processed. This typically means that
 * the associated \c data pointer for this route must be kept until the
 * route itself is disposed of.
 *
 * \param router Router to remove the route from.
 * \param message_type Type of message to route.
 *
 * \since 12
 */
void stasis_message_router_remove(struct stasis_message_router *router,
	struct stasis_message_type *message_type);

/*!
 * \brief Remove a cache route from a message router.
 *
 * If a route is removed from another thread, there is no notification that
 * all messages using this route have been processed. This typically means that
 * the associated \c data pointer for this route must be kept until the
 * route itself is disposed of.
 *
 * \param router Router to remove the route from.
 * \param message_type Type of message to route.
 *
 * \since 12
 */
void stasis_message_router_remove_cache_update(
	struct stasis_message_router *router,
	struct stasis_message_type *message_type);

/*!
 * \brief Sets the default route of a router.
 *
 * \param router Router to set the default route of.
 * \param callback Callback to forward messages which otherwise have no home.
 * \param data Data pointer to pass to \a callback.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \since 12
 *
 * \note Setting a default callback will automatically cause the underlying
 * subscription to receive all messages and not be filtered. If filtering is
 * desired then a specific route for each message type should be provided.
 */
int stasis_message_router_set_default(struct stasis_message_router *router,
				      stasis_subscription_cb callback,
				      void *data);

/*!
 * \brief Sets the default route of a router with formatters.
 *
 * \param router Router to set the default route of.
 * \param callback Callback to forward messages which otherwise have no home.
 * \param data Data pointer to pass to \a callback.
 * \param formatters A bitmap of \ref stasis_subscription_message_formatters we wish to receive.
 *
 * \since 13.26.0
 * \since 16.3.0
 *
 * \note If formatters are specified then the message router will remain in a selective
 * filtering state. Any explicit routes will receive messages of their message type and
 * the default callback will only receive messages that have one of the given formatters.
 * Explicit routes will not be filtered according to the given formatters.
 */
void stasis_message_router_set_formatters_default(struct stasis_message_router *router,
	stasis_subscription_cb callback,
	void *data,
	enum stasis_subscription_message_formatters formatters);

/*!
 * \brief Indicate to a message router that we are interested in messages with one or more formatters.
 *
 * The formatters are passed on to the underlying subscription.
 *
 * \warning With direct subscriptions, adding a formatter filter is an OR operation
 * with any message type filters.  In the current implementation of message router however,
 * it's an AND operation.  Even when setting a default route, the callback will only get
 * messages that have the formatters provides in this call.
 *
 * \param router Router to set the formatters of.
 * \param formatters A bitmap of \ref stasis_subscription_message_formatters we wish to receive.
 *
 * \since 13.25.0
 * \since 16.2.0
 */
void stasis_message_router_accept_formatters(struct stasis_message_router *router,
	enum stasis_subscription_message_formatters formatters);

#endif /* _ASTERISK_STASIS_MESSAGE_ROUTER_H */
