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

#ifndef _ASTERISK_STASIS_CACHE_PATTERN_H
#define _ASTERISK_STASIS_CACHE_PATTERN_H

/*! \file
 *
 * \brief Caching pattern for \ref stasis topics.
 *
 * A typical pattern for Stasis objects is to have individual objects, which
 * have their own topic and caching topic. These individual topics feed an
 * upstream aggregate topics, and a shared cache.
 *
 * The \ref stasis_cp_all object contains the aggregate topics and shared cache.
 * This is built with the base name for the topics, and the identity function to
 * identify messages in the cache.
 *
 * The \ref stasis_cp_single object contains the \ref stasis_topic for a single
 * instance, and the corresponding \ref stasis_caching_topic.
 *
 * Since the \ref stasis_cp_single object has subscriptions for forwarding
 * and caching, it must be disposed of using stasis_cp_single_unsubscribe()
 * instead of simply ao2_cleanup().
 */

#include "asterisk/stasis.h"

/*!
 * \brief The 'all' side of the cache pattern. These are typically built as
 * global objects for specific modules.
 */
struct stasis_cp_all;

/*!
 * \brief Create an all instance of the cache pattern.
 *
 * This object is AO2 managed, so dispose of it with ao2_cleanup().
 *
 * \param name Base name of the topics.
 * \param id_fn Identity function for the cache.
 * \return All side instance.
 * \retval NULL on error.
 */
struct stasis_cp_all *stasis_cp_all_create(const char *name,
	snapshot_get_id id_fn);

/*!
 * \brief Get the aggregate topic.
 *
 * This topic aggregates all messages published to corresponding
 * stasis_cp_single_topic() topics.
 *
 * \param all All side caching pattern object.
 * \return The aggregate topic.
 * \retval NULL if \a all is \c NULL
 */
struct stasis_topic *stasis_cp_all_topic(struct stasis_cp_all *all);

/*!
 * \brief Get the caching topic.
 *
 * This topic aggregates all messages from the corresponding
 * stasis_cp_single_topic_cached() topics.
 *
 * Note that one normally only subscribes to the caching topic, since data
 * is fed to it from its upstream topic.
 *
 * \param all All side caching pattern object.
 * \return The aggregate caching topic.
 * \retval NULL if \a all is \c NULL
 */
struct stasis_topic *stasis_cp_all_topic_cached(
	struct stasis_cp_all *all);

/*!
 * \brief Get the cache.
 *
 * This is the shared cache for all corresponding \ref stasis_cp_single objects.
 *
 * \param all All side caching pattern object.
 * \return The cache.
 * \retval NULL if \a all is \c NULL
 */
struct stasis_cache *stasis_cp_all_cache(struct stasis_cp_all *all);

/*!
 * \brief The 'one' side of the cache pattern. These are built per-instance for
 * some corresponding object, and must be explicitly disposed of using
 * stasis_cp_single_unsubscribe().
 */
struct stasis_cp_single;

/*!
 * \brief Create the 'one' side of the cache pattern.
 *
 * Create the 'one' and forward to all's topic and topic_cached.
 *
 * Dispose of using stasis_cp_single_unsubscribe().
 *
 * \param all Corresponding all side.
 * \param name Base name for the topics.
 * \return One side instance
 */
struct stasis_cp_single *stasis_cp_single_create(struct stasis_cp_all *all,
	const char *name);

/*!
 * \brief Create a sink in the cache pattern
 *
 * Create the 'one' but do not automatically forward to the all's topic.
 * This is useful when aggregating other topic's messages created with
 * \c stasis_cp_single_create in another caching topic without replicating
 * those messages in the all's topics.
 *
 * Dispose of using stasis_cp_single_unsubscribe().
 *
 * \param all Corresponding all side.
 * \param name Base name for the topics.
 * \return One side instance
 */
struct stasis_cp_single *stasis_cp_sink_create(struct stasis_cp_all *all,
	const char *name);

/*!
 * \brief Stops caching and forwarding messages.
 *
 * \param one One side of the cache pattern.
 */
void stasis_cp_single_unsubscribe(struct stasis_cp_single *one);

/*!
 * \brief Get the topic for this instance.
 *
 * This is the topic to which one would post instance-specific messages, or
 * subscribe for single-instance, uncached messages.
 *
 * \param one One side of the cache pattern.
 * \return The main topic.
 * \retval NULL if \a one is \c NULL
 */
struct stasis_topic *stasis_cp_single_topic(struct stasis_cp_single *one);

/*!
 * \brief Get the caching topic for this instance.
 *
 * Note that one normally only subscribes to the caching topic, since data
 * is fed to it from its upstream topic.
 *
 * \param one One side of the cache pattern.
 * \return The caching topic.
 * \retval NULL if \a one is \c NULL
 */
struct stasis_topic *stasis_cp_single_topic_cached(
	struct stasis_cp_single *one);

/*!
 * \brief Indicate to an instance that we are interested in a message type.
 *
 * This will cause the caching topic to receive messages of the given message
 * type. This enables internal filtering in the stasis message bus to reduce
 * messages.
 *
 * \param one One side of the cache pattern.
 * \param type The message type we wish to receive.
 * \retval 0 on success
 * \retval -1 failure
 *
 * \since 17.0.0
 */
int stasis_cp_single_accept_message_type(struct stasis_cp_single *one,
	struct stasis_message_type *type);

/*!
 * \brief Set the message type filtering level on a cache
 *
 * This will cause the underlying subscription to filter messages according to the
 * provided filter level. For example if selective is used then only
 * messages matching those provided to \ref stasis_subscription_accept_message_type
 * will be raised to the subscription callback.
 *
 * \param one One side of the cache pattern.
 * \param filter What filter to use
 * \retval 0 on success
 * \retval -1 failure
 *
 * \since 17.0.0
 */
int stasis_cp_single_set_filter(struct stasis_cp_single *one,
	enum stasis_subscription_message_filter filter);

#endif /* _ASTERISK_STASIS_CACHE_PATTERN_H */
