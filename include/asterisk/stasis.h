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

#ifndef _ASTERISK_STASIS_H
#define _ASTERISK_STASIS_H

/*! \file
 *
 * \brief Stasis Message Bus API. See \ref stasis "Stasis Message Bus API" for
 * detailed documentation.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 *
 * \page stasis Stasis Message Bus API
 *
 * \par Intro
 *
 * The Stasis Message Bus is a loosely typed mechanism for distributing messages
 * within Asterisk. It is designed to be:
 *  - Loosely coupled; new message types can be added in seperate modules.
 *  - Easy to use; publishing and subscribing are straightforward operations.
 *
 * There are three main concepts for using the Stasis Message Bus:
 *  - \ref stasis_message
 *  - \ref stasis_topic
 *  - \ref stasis_subscription
 *
 * \par stasis_message
 *
 * Central to the Stasis Message Bus is the \ref stasis_message, the messages
 * that are sent on the bus. These messages have:
 *  - a type (as defined by a \ref stasis_message_type)
 *  - a value - a \c void pointer to an AO2 object
 *  - a timestamp when it was created
 *
 * Once a \ref stasis_message has been created, it is immutable and cannot
 * change. The same goes for the value of the message (although this cannot be
 * enforced in code). Messages themselves are reference-counted, AO2 objects,
 * along with their values. By being both reference counted and immutable,
 * messages can be shared throughout the system without any concerns for
 * threading.
 *
 * The type of a message is defined by an instance of \ref stasis_message_type,
 * which can be created by calling stasis_message_type_create(). Message types
 * are named, which is useful in debugging. It is recommended that the string
 * name for a message type match the name of the struct that's stored in the
 * message. For example, name for \ref stasis_cache_update's message type is \c
 * "stasis_cache_update".
 *
 * \par stasis_topic
 *
 * A \ref stasis_topic is an object to which \ref stasis_subscriber's may be
 * subscribed, and \ref stasis_message's may be published. Any message published
 * to the topic is dispatched to all of its subscribers. The topic itself may be
 * named, which is useful in debugging.
 *
 * Topics themselves are reference counted objects. Since topics are referred to
 * by their subscibers, they will not be freed until all of their subscribers
 * have unsubscribed. Topics are also thread safe, so no worries about
 * publishing/subscribing/unsubscribing to a topic concurrently from multiple
 * threads. It's also designed to handle the case of unsubscribing from a topic
 * from within the subscription handler.
 *
 * \par Forwarding
 *
 * There is one special case of topics that's worth noting: forwarding
 * messages. It's a fairly common use case to want to forward all the messages
 * published on one topic to another one (for example, an aggregator topic that
 * publishes all the events from a set of other topics). This can be
 * accomplished easily using stasis_forward_all(). This sets up the forwarding
 * between the two topics, and returns a \ref stasis_subscription, which can be
 * unsubscribed to stop the forwarding.
 *
 * \par Caching
 *
 * Another common use case is to want to cache certain messages that are
 * published on the bus. Usually these events are snapshots of the current state
 * in the system, and it's desirable to query that state from the cache without
 * locking the original object. It's also desirable for subscribers of the
 * caching topic to receive messages that have both the old cache value and the
 * new value being put into the cache. For this, we have
 * stasis_caching_topic_create(), providing it with the topic which publishes
 * the messages that you wish to cache, and a function that can identify
 * cacheable messages.
 *
 * The returned \ref stasis_caching_topic provides a topic that forwards
 * non-cacheable messages unchanged. A cacheable message is wrapped in a \ref
 * stasis_cache_update message which provides the old snapshot (or \c NULL if
 * this is a new cache entry), and the new snapshot (or \c NULL if the entry was
 * removed from the cache). A stasis_cache_clear_create() message must be sent
 * to the topic in order to remove entries from the cache.
 *
 * In order to unsubscribe a \ref stasis_caching_topic from the upstream topic,
 * call stasis_caching_unsubscribe(). Due to cyclic references, the \ref
 * stasis_caching_topic will not be freed until after it has been unsubscribed,
 * and all other ao2_ref()'s have been cleaned up.
 *
 * \par stasis_subscriber
 *
 * Any topic may be subscribed to by simply providing stasis_subscribe() the
 * \ref stasis_topic to subscribe to, a handler function and \c void pointer to
 * data that is passed back to the handler. Invocations on the subscription's
 * handler are serialized, but different invocations may occur on different
 * threads (this usually isn't important unless you use thread locals or
 * something similar).
 *
 * In order to stop receiving messages, call stasis_unsubscribe() with your \ref
 * stasis_subscription. Due to cyclic references, the \ref
 * stasis_subscription will not be freed until after it has been unsubscribed,
 * and all other ao2_ref()'s have been cleaned up.
 *
 * \par Shutdown
 *
 * Subscriptions have two options for unsubscribing, depending upon the context
 * in which you need to unsubscribe.
 *
 * If your subscription is owned by a module, and you must unsubscribe from the
 * module_unload() function, then you'll want to use the
 * stasis_unsubscribe_and_join() function. This will block until the final
 * message has been received on the subscription. Otherwise, there's the danger
 * of invoking the callback function after it has been unloaded.
 *
 * If your subscription is owned by an object, then your object should have an
 * explicit shutdown() function, which calls stasis_unsubscribe(). In your
 * subscription handler, when the stasis_subscription_final_message() has been
 * received, decrement the refcount on your object. In your object's destructor,
 * you may assert that stasis_subscription_is_done() to validate that the
 * subscription's callback will no longer be invoked.
 *
 * \b Note: You may be tempted to simply call stasis_unsubscribe_and_join() from
 * an object's destructor. While code that does this may work most of the time,
 * it's got one big downside. There's a general assumption that object
 * destruction is non-blocking. If you block the destruction waiting for the
 * subscription to complete, there's the danger that the subscription may
 * process a message which will bump the refcount up by one. Then it does
 * whatever it does, decrements the refcount, which then proceeds to re-destroy
 * the object. Now you've got hard to reproduce bugs that only show up under
 * certain loads.
 */

#include "asterisk/utils.h"

/*! @{ */

/*!
 * \brief Metadata about a \ref stasis_message.
 * \since 12
 */
struct stasis_message_type;

/*!
 * \brief Register a new message type.
 *
 * \ref stasis_message_type is an AO2 object, so ao2_cleanup() when you're done
 * with it.
 *
 * \param name Name of the new type.
 * \return Pointer to the new type.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_message_type *stasis_message_type_create(const char *name);

/*!
 * \brief Gets the name of a given message type
 * \param type The type to get.
 * \return Name of the type.
 * \return \c NULL if \a type is \c NULL.
 * \since 12
 */
const char *stasis_message_type_name(const struct stasis_message_type *type);

/*!
 * \brief Opaque type for a Stasis message.
 * \since 12
 */
struct stasis_message;

/*!
 * \brief Create a new message.
 *
 * This message is an \c ao2 object, and must be ao2_cleanup()'ed when you are done
 * with it. Messages are also immutable, and must not be modified after they
 * are initialized. Especially the \a data in the message.
 *
 * \param type Type of the message
 * \param data Immutable data that is the actual contents of the message
 * \return New message
 * \return \c NULL on error
 * \since 12
 */
struct stasis_message *stasis_message_create(struct stasis_message_type *type, void *data);

/*!
 * \brief Get the message type for a \ref stasis_message.
 * \param msg Message to type
 * \return Type of \a msg
 * \return \c NULL if \a msg is \c NULL.
 * \since 12
 */
struct stasis_message_type *stasis_message_type(const struct stasis_message *msg);

/*!
 * \brief Get the data contained in a message.
 * \param msg Message.
 * \return Immutable data pointer
 * \return \c NULL if msg is \c NULL.
 * \since 12
 */
void *stasis_message_data(const struct stasis_message *msg);

/*!
 * \brief Get the time when a message was created.
 * \param msg Message.
 * \return Pointer to the \a timeval when the message was created.
 * \return \c NULL if msg is \c NULL.
 * \since 12
 */
const struct timeval *stasis_message_timestamp(const struct stasis_message *msg);

/*! @} */

/*! @{ */

/*!
 * \brief A topic to which messages may be posted, and subscribers, well, subscribe
 * \since 12
 */
struct stasis_topic;

/*!
 * \brief Create a new topic.
 * \param name Name of the new topic.
 * \return New topic instance.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_topic *stasis_topic_create(const char *name);

/*!
 * \brief Return the name of a topic.
 * \param topic Topic.
 * \return Name of the topic.
 * \return \c NULL if topic is \c NULL.
 * \since 12
 */
const char *stasis_topic_name(const struct stasis_topic *topic);

/*!
 * \brief Publish a message to a topic's subscribers.
 * \param topic Topic.
 * \param message Message to publish.
 * \since 12
 */
void stasis_publish(struct stasis_topic *topic, struct stasis_message *message);

/*!
 * \brief Publish a message from a specified topic to all the subscribers of a
 * possibly different topic.
 * \param topic Topic to publish message to.
 * \param topic Original topic message was from.
 * \param message Message
 * \since 12
 */
void stasis_forward_message(struct stasis_topic *topic,
			    struct stasis_topic *publisher_topic,
			    struct stasis_message *message);

/*! @} */

/*! @{ */

/*!
 * \brief Opaque type for a Stasis subscription.
 * \since 12
 */
struct stasis_subscription;

/*!
 * \brief Callback function type for Stasis subscriptions.
 * \param data Data field provided with subscription.
 * \param topic Topic to which the message was published.
 * \param message Published message.
 * \since 12
 */
typedef void (*stasis_subscription_cb)(void *data, struct stasis_subscription *sub, struct stasis_topic *topic, struct stasis_message *message);

/*!
 * \brief Create a subscription.
 *
 * In addition to being AO2 managed memory (requiring an ao2_cleanup() to free
 * up this reference), the subscription must be explicitly unsubscribed from its
 * topic using stasis_unsubscribe().
 *
 * The invocations of the callback are serialized, but may not always occur on
 * the same thread. The invocation order of different subscriptions is
 * unspecified.
 *
 * \param topic Topic to subscribe to.
 * \param callback Callback function for subscription messages.
 * \param data Data to be passed to the callback, in addition to the message.
 * \return New \ref stasis_subscription object.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_subscription *stasis_subscribe(struct stasis_topic *topic,
	stasis_subscription_cb callback, void *data);

/*!
 * \brief Cancel a subscription.
 *
 * Note that in an asynchronous system, there may still be messages queued or
 * in transit to the subscription's callback. These will still be delivered.
 * There will be a final 'SubscriptionCancelled' message, indicating the
 * delivery of the final message.
 *
 * \param subscription Subscription to cancel.
 * \return \c NULL for convenience
 * \since 12
 */
struct stasis_subscription *stasis_unsubscribe(
	struct stasis_subscription *subscription);

/*!
 * \brief Block until the last message is processed on a subscription.
 *
 * This function will not return until the \a subscription's callback for the
 * stasis_subscription_final_message() completes. This allows cleanup routines
 * to run before unblocking the joining thread.
 *
 * \param subscription Subscription to block on.
 * \since 12
 */
void stasis_subscription_join(struct stasis_subscription *subscription);

/*!
 * \brief Returns whether \a subscription has received its final message.
 *
 * Note that a subscription is considered done even while the
 * stasis_subscription_final_message() is being processed. This allows cleanup
 * routines to check the status of the subscription.
 *
 * \param subscription Subscription.
 * \return True (non-zero) if stasis_subscription_final_message() has been
 *         received.
 * \return False (zero) if waiting for the end.
 */
int stasis_subscription_is_done(struct stasis_subscription *subscription);

/*!
 * \brief Cancel a subscription, blocking until the last message is processed.
 *
 * While normally it's recommended to stasis_unsubscribe() and wait for
 * stasis_subscription_final_message(), there are times (like during a module
 * unload) where you have to wait for the final message (otherwise you'll call
 * a function in a shared module that no longer exists).
 *
 * \param subscription Subscription to cancel.
 * \return \c NULL for convenience
 * \since 12
 */
struct stasis_subscription *stasis_unsubscribe_and_join(
	struct stasis_subscription *subscription);

/*!
 * \brief Create a subscription which forwards all messages from one topic to
 * another.
 *
 * Note that the \a topic parameter of the invoked callback will the be \a topic
 * the message was sent to, not the topic the subscriber subscribed to.
 *
 * \param from_topic Topic to forward.
 * \param to_topic Destination topic of forwarded messages.
 * \return New forwarding subscription.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_subscription *stasis_forward_all(struct stasis_topic *from_topic,
	struct stasis_topic *to_topic);

/*!
 * \brief Get the unique ID for the subscription.
 *
 * \param sub Subscription for which to get the unique ID.
 * \return Unique ID for the subscription.
 * \since 12
 */
const char *stasis_subscription_uniqueid(const struct stasis_subscription *sub);

/*!
 * \brief Returns whether a subscription is currently subscribed.
 *
 * Note that there may still be messages queued up to be dispatched to this
 * subscription, but the stasis_subscription_final_message() has been enqueued.
 *
 * \param sub Subscription to check
 * \return False (zero) if subscription is not subscribed.
 * \return True (non-zero) if still subscribed.
 */
int stasis_subscription_is_subscribed(const struct stasis_subscription *sub);

/*!
 * \brief Determine whether a message is the final message to be received on a subscription.
 *
 * \param sub Subscription on which the message was received.
 * \param msg Message to check.
 * \return zero if the provided message is not the final message.
 * \return non-zero if the provided message is the final message.
 * \since 12
 */
int stasis_subscription_final_message(struct stasis_subscription *sub, struct stasis_message *msg);

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief Holds details about changes to subscriptions for the specified topic
 * \since 12
 */
struct stasis_subscription_change {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uniqueid);	/*!< The unique ID associated with this subscription */
		AST_STRING_FIELD(description);	/*!< The description of the change to the subscription associated with the uniqueid */
	);
	struct stasis_topic *topic;		/*!< The topic the subscription is/was subscribing to */
};

/*!
 * \brief Gets the message type for subscription change notices
 * \return The stasis_message_type for subscription change notices
 * \since 12
 */
struct stasis_message_type *stasis_subscription_change_type(void);

/*! @} */

/*!
 * \brief Pool for topic aggregation
 */
struct stasis_topic_pool;

/*!
 * \brief Create a topic pool that routes messages from dynamically generated topics to the given topic
 * \param pooled_topic Topic to which messages will be routed
 * \return the new stasis_topic_pool
 * \return \c NULL on failure
 */
struct stasis_topic_pool *stasis_topic_pool_create(struct stasis_topic *pooled_topic);

/*!
 * \brief Find or create a topic in the pool
 * \param pool Pool for which to get the topic
 * \param topic_name Name of the topic to get
 * \return The already stored or newly allocated topic
 * \return \c NULL if the topic was not found and could not be allocated
 */
struct stasis_topic *stasis_topic_pool_get_topic(struct stasis_topic_pool *pool, const char *topic_name);

/*! @} */

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief Message type for cache update messages.
 * \return Message type for cache update messages.
 * \since 12
 */
struct stasis_message_type *stasis_cache_update_type(void);

/*!
 * \brief Cache update message
 * \since 12
 */
struct stasis_cache_update {
	/*! \brief Topic that published \c new_snapshot */
	struct stasis_topic *topic;
	/*! \brief Convenience reference to snapshot type */
	struct stasis_message_type *type;
	/*! \brief Old value from the cache */
	struct stasis_message *old_snapshot;
	/*! \brief New value */
	struct stasis_message *new_snapshot;
};

/*!
 * \brief Cache clear message.
 */
struct stasis_cache_clear {
	/*! Type of object being cleared from the cache */
	struct stasis_message_type *type;
	/*! Id of the object being cleared from the cache */
	char id[];
};

/*!
 * \brief Message type for \ref stasis_cache_clear.
 * \since 12
 */
struct stasis_message_type *stasis_cache_clear_type(void);

/*!
 * \brief A message which instructs the caching topic to remove an entry from its cache.
 * \param type Message type.
 * \param id Unique id of the snapshot to clear.
 * \return Message which, when sent to the \a topic, will clear the item from the cache.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_message *stasis_cache_clear_create(struct stasis_message_type *type, const char *id);

/*! @} */

/*! @{ */

/*!
 * \brief A topic wrapper, which caches certain messages.
 * \since 12
 */
struct stasis_caching_topic;

/*!
 * \brief Callback extract a unique identity from a snapshot message.
 *
 * This identity is unique to the underlying object of the snapshot, such as the
 * UniqueId field of a channel.
 *
 * \param message Message to extract id from.
 * \return String representing the snapshot's id.
 * \return \c NULL if the message_type of the message isn't a handled snapshot.
 * \since 12
 */
typedef const char *(*snapshot_get_id)(struct stasis_message *message);

/*!
 * \brief Create a topic which monitors and caches messages from another topic.
 *
 * The idea is that some topics publish 'snapshots' of some other object's state
 * that should be cached. When these snapshot messages are received, the cache
 * is updated, and a stasis_cache_update() message is forwarded, which has both
 * the original snapshot message and the new message.
 *
 * \param original_topic Topic publishing snapshot messages.
 * \param id_fn Callback to extract the id from a snapshot message.
 * \return New topic which changes snapshot messages to stasis_cache_update()
 *         messages, and forwards all other messages from the original topic.
 * \since 12
 */
struct stasis_caching_topic *stasis_caching_topic_create(struct stasis_topic *original_topic, snapshot_get_id id_fn);

/*!
 * \brief Unsubscribes a caching topic from its upstream topic.
 *
 * This function returns immediately, so be sure to cleanup when
 * stasis_subscription_final_message() is received.
 *
 * \param caching_topic Caching topic to unsubscribe
 * \return \c NULL for convenience
 * \since 12
 */
struct stasis_caching_topic *stasis_caching_unsubscribe(
	struct stasis_caching_topic *caching_topic);

/*!
 * \brief Unsubscribes a caching topic from its upstream topic, blocking until
 * all messages have been forwarded.
 *
 * See stasis_unsubscriben_and_join() for more info on when to use this as
 * opposed to stasis_caching_unsubscribe().
 *
 * \param caching_topic Caching topic to unsubscribe
 * \return \c NULL for convenience
 * \since 12
 */
struct stasis_caching_topic *stasis_caching_unsubscribe_and_join(
	struct stasis_caching_topic *caching_topic);

/*!
 * \brief Returns the topic of cached events from a caching topics.
 * \param caching_topic The caching topic.
 * \return The topic that publishes cache update events, along with passthrough events
 *         from the underlying topic.
 * \return \c NULL if \a caching_topic is \c NULL.
 * \since 12
 */
struct stasis_topic *stasis_caching_get_topic(struct stasis_caching_topic *caching_topic);

/*!
 * \brief Retrieve an item from the cache.
 * \param caching_topic The topic returned from stasis_caching_topic_create().
 * \param type Type of message to retrieve.
 * \param id Identity of the snapshot to retrieve.
 * \return Message from the cache. The cache still owns the message, so
 *         ao2_ref() if you want to keep it.
 * \return \c NULL if message is not found.
 * \since 12
 */
struct stasis_message *stasis_cache_get(struct stasis_caching_topic *caching_topic,
					struct stasis_message_type *type,
					const char *id);

/*!
 * \brief Dump cached items to a subscription
 * \param caching_topic The topic returned from stasis_caching_topic_create().
 * \param type Type of message to dump (any type if \c NULL).
 * \return ao2_container containing all matches (must be unreffed by caller)
 * \return \c NULL on allocation error
 * \since 12
 */
struct ao2_container *stasis_cache_dump(struct stasis_caching_topic *caching_topic,
					struct stasis_message_type *type);

/*! @} */

/*! @{ */

/*!
 * \brief Boiler-plate removing macro for defining message types.
 *
 * \param name Name of message type.
 * \since 12
 */
#define STASIS_MESSAGE_TYPE_DEFN(name)				\
	static struct stasis_message_type *_priv_ ## name;	\
	struct stasis_message_type *name(void) {		\
		ast_assert(_priv_ ## name != NULL);		\
		return _priv_ ## name;				\
	}

/*!
 * \brief Boiler-plate removing macro for initializing message types.
 *
 * \param name Name of message type.
 * \return 0 if initialization is successful.
 * \return Non-zero on failure.
 * \since 12
 */
#define STASIS_MESSAGE_TYPE_INIT(name)					\
	({								\
		ast_assert(_priv_ ## name == NULL);			\
		_priv_ ## name = stasis_message_type_create(#name);	\
			_priv_ ## name ? 0 : -1;			\
	})

/*!
 * \brief Boiler-plate removing macro for cleaning up message types.
 *
 * \param name Name of message type.
 * \since 12
 */
#define STASIS_MESSAGE_TYPE_CLEANUP(name)	\
	({					\
		ao2_cleanup(_priv_ ## name);	\
		_priv_ ## name = NULL;		\
	})

/*! @} */

/*! @{ */

/*!
 * \brief Initialize the Stasis subsystem
 * \return 0 on success.
 * \return Non-zero on error.
 * \since 12
 */
int stasis_init(void);

/*!
 * \private
 * \brief called by stasis_init() for cache initialization.
 * \return 0 on success.
 * \return Non-zero on error.
 * \since 12
 */
int stasis_cache_init(void);

/*! @} */

/*!
 * \defgroup StasisTopicsAndMessages Stasis topics, and their messages.
 *
 * This group contains the topics, messages and corresponding message types
 * found within Asterisk.
 */

#endif /* _ASTERISK_STASIS_H */
