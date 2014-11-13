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
 * new value being put into the cache. For this, we have stasis_cache_create()
 * and stasis_caching_topic_create(), providing them with the topic which
 * publishes the messages that you wish to cache, and a function that can
 * identify cacheable messages.
 *
 * The \ref stasis_cache is designed so that it may be shared amongst several
 * \ref stasis_caching_topic objects. This allows you to have individual caching
 * topics per-object (i.e. so you can subscribe to updates for a single object),
 * and still have a single cache to query for the state of all objects. While a
 * cache may be shared amongst different message types, such a usage is probably
 * not a good idea.
 *
 * The \ref stasis_cache can only be written to by \ref stasis_caching_topics.
 * It's a thread safe container, so freely use the stasis_cache_get() and
 * stasis_cache_dump() to query the cache.
 *
 * The \ref stasis_caching_topic discards non-cacheable messages. A cacheable
 * message is wrapped in a \ref stasis_cache_update message which provides the
 * old snapshot (or \c NULL if this is a new cache entry), and the new snapshot
 * (or \c NULL if the entry was removed from the cache). A
 * stasis_cache_clear_create() message must be sent to the topic in order to
 * remove entries from the cache.
 *
 * In order to unsubscribe a \ref stasis_caching_topic from the upstream topic,
 * call stasis_caching_unsubscribe(). Due to cyclic references, the \ref
 * stasis_caching_topic will not be freed until after it has been unsubscribed,
 * and all other ao2_ref()'s have been cleaned up.
 *
 * The \ref stasis_cache object is a normal AO2 managed object, which can be
 * release with ao2_cleanup().
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

#include "asterisk/json.h"
#include "asterisk/manager.h"
#include "asterisk/utils.h"
#include "asterisk/event.h"

/*! @{ */

/*!
 * \brief Metadata about a \ref stasis_message.
 * \since 12
 */
struct stasis_message_type;

/*!
 * \brief Opaque type for a Stasis message.
 * \since 12
 */
struct stasis_message;

/*!
 * \brief Opaque type for a Stasis subscription.
 * \since 12
 */
struct stasis_subscription;

/*!
 * \brief Structure containing callbacks for Stasis message sanitization
 *
 * \note If either callback is implemented, both should be implemented since
 * not all callers may have access to the full snapshot.
 */
struct stasis_message_sanitizer {
	/*!
	 * \brief Callback which determines whether a channel should be sanitized from
	 * a message based on the channel's unique ID
	 *
	 * \param channel_id The unique ID of the channel
	 *
	 * \retval non-zero if the channel should be left out of the message
	 * \retval zero if the channel should remain in the message
	 */
	int (*channel_id)(const char *channel_id);

	/*!
	 * \brief Callback which determines whether a channel should be sanitized from
	 * a message based on the channel's snapshot
	 *
	 * \param snapshot A snapshot generated from the channel
	 *
	 * \retval non-zero if the channel should be left out of the message
	 * \retval zero if the channel should remain in the message
	 */
	int (*channel_snapshot)(const struct ast_channel_snapshot *snapshot);

	/*!
	 * \brief Callback which determines whether a channel should be sanitized from
	 * a message based on the channel
	 *
	 * \param chan The channel to be checked
	 *
	 * \retval non-zero if the channel should be left out of the message
	 * \retval zero if the channel should remain in the message
	 */
	int (*channel)(const struct ast_channel *chan);
};

/*!
 * \brief Virtual table providing methods for messages.
 * \since 12
 */
struct stasis_message_vtable {
	/*!
	 * \brief Build the JSON representation of the message.
	 *
	 * May be \c NULL, or may return \c NULL, to indicate no representation.
	 * The returned object should be ast_json_unref()'ed.
	 *
	 * \param message Message to convert to JSON string.
	 * \param sanitize Snapshot sanitization callback.
	 *
	 * \return Newly allocated JSON message.
	 * \return \c NULL on error.
	 * \return \c NULL if JSON format is not supported.
	 */
	struct ast_json *(*to_json)(struct stasis_message *message, const struct stasis_message_sanitizer *sanitize);

	/*!
	 * \brief Build the AMI representation of the message.
	 *
	 * May be \c NULL, or may return \c NULL, to indicate no representation.
	 * The returned object should be ao2_cleanup()'ed.
	 *
	 * \param message Message to convert to AMI string.
	 * \return Newly allocated \ref ast_manager_event_blob.
	 * \return \c NULL on error.
	 * \return \c NULL if AMI format is not supported.
	 */
	struct ast_manager_event_blob *(*to_ami)(
		struct stasis_message *message);

	/*!
	 * \since 12.3.0
	 * \brief Build the \ref ast_event representation of the message.
	 *
	 * May be \c NULL, or may return \c NULL, to indicate no representation.
	 * The returned object should be free'd.
	 *
	 * \param message Message to convert to an \ref ast_event.
	 * \return Newly allocated \ref ast_event.
	 * \return \c NULL on error.
	 * \return \c NULL if AMI format is not supported.
	 */
	struct ast_event *(*to_event)(
		struct stasis_message *message);
};

/*!
 * \brief Return code for Stasis message type creation attempts
 */
enum stasis_message_type_result {
	STASIS_MESSAGE_TYPE_ERROR = -1,	/*!< Message type was not created due to allocation failure */
	STASIS_MESSAGE_TYPE_SUCCESS,	/*!< Message type was created successfully */
	STASIS_MESSAGE_TYPE_DECLINED,	/*!< Message type was not created due to configuration */
};

/*!
 * \brief Create a new message type.
 *
 * \ref stasis_message_type is an AO2 object, so ao2_cleanup() when you're done
 * with it.
 *
 * \param name Name of the new type.
 * \param vtable Virtual table of message methods. May be \c NULL.
 * \param[out] result The location where the new message type will be placed
 *
 * \note Stasis message type creation may be declined if the message type is disabled
 *
 * \returns A stasis_message_type_result enum
 * \since 12
 */
enum stasis_message_type_result stasis_message_type_create(const char *name,
	struct stasis_message_vtable *vtable, struct stasis_message_type **result);

/*!
 * \brief Gets the name of a given message type
 * \param type The type to get.
 * \return Name of the type.
 * \return \c NULL if \a type is \c NULL.
 * \since 12
 */
const char *stasis_message_type_name(const struct stasis_message_type *type);

/*!
 * \brief Check whether a message type is declined
 *
 * \param name The name of the message type to check
 *
 * \retval zero The message type is not declined
 * \retval non-zero The message type is declined
 */
int stasis_message_type_declined(const char *name);

/*!
 * \brief Create a new message.
 *
 * This message is an \c ao2 object, and must be ao2_cleanup()'ed when you are done
 * with it. Messages are also immutable, and must not be modified after they
 * are initialized. Especially the \a data in the message.
 *
 * \param type Type of the message
 * \param data Immutable data that is the actual contents of the message
 *
 * \return New message
 * \return \c NULL on error
 *
 * \since 12
 */
struct stasis_message *stasis_message_create(struct stasis_message_type *type, void *data);

/*!
 * \brief Create a new message for an entity.
 *
 * This message is an \c ao2 object, and must be ao2_cleanup()'ed when you are done
 * with it. Messages are also immutable, and must not be modified after they
 * are initialized. Especially the \a data in the message.
 *
 * \param type Type of the message
 * \param data Immutable data that is the actual contents of the message
 * \param eid What entity originated this message. (NULL for aggregate)
 *
 * \note An aggregate message is a combined representation of the local
 * and remote entities publishing the message data.  e.g., An aggregate
 * device state represents the combined device state from the local and
 * any remote entities publishing state for a device.  e.g., An aggregate
 * MWI message is the old/new MWI counts accumulated from the local and
 * any remote entities publishing to a mailbox.
 *
 * \retval New message
 * \retval \c NULL on error
 *
 * \since 12.2.0
 */
struct stasis_message *stasis_message_create_full(struct stasis_message_type *type, void *data, const struct ast_eid *eid);

/*!
 * \brief Get the entity id for a \ref stasis_message.
 * \since 12.2.0
 *
 * \param msg Message to get eid.
 *
 * \retval Entity id of \a msg
 * \retval \c NULL if \a msg is an aggregate or \a msg is \c NULL.
 */
const struct ast_eid *stasis_message_eid(const struct stasis_message *msg);

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

/*!
 * \brief Build the JSON representation of the message.
 *
 * May return \c NULL, to indicate no representation. The returned object should
 * be ast_json_unref()'ed.
 *
 * \param message Message to convert to JSON string.
 * \param sanitize Snapshot sanitization callback.
 *
 * \return Newly allocated string with JSON message.
 * \return \c NULL on error.
 * \return \c NULL if JSON format is not supported.
 */
struct ast_json *stasis_message_to_json(struct stasis_message *message, struct stasis_message_sanitizer *sanitize);

/*!
 * \brief Build the AMI representation of the message.
 *
 * May return \c NULL, to indicate no representation. The returned object should
 * be ao2_cleanup()'ed.
 *
 * \param message Message to convert to AMI.
 * \return \c NULL on error.
 * \return \c NULL if AMI format is not supported.
 */
struct ast_manager_event_blob *stasis_message_to_ami(
	struct stasis_message *message);

/*!
 * \brief Build the \ref AstGenericEvents representation of the message.
 *
 * May return \c NULL, to indicate no representation. The returned object should
 * be disposed of via \ref ast_event_destroy.
 *
 * \param message Message to convert to AMI.
 * \return \c NULL on error.
 * \return \c NULL if AMI format is not supported.
 */
struct ast_event *stasis_message_to_event(
	struct stasis_message *message);

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
 *
 * This call is asynchronous and will return immediately upon queueing
 * the message for delivery to the topic's subscribers.
 *
 * \since 12
 */
void stasis_publish(struct stasis_topic *topic, struct stasis_message *message);

/*!
 * \brief Publish a message to a topic's subscribers, synchronizing
 * on the specified subscriber
 * \param sub Subscription to synchronize on.
 * \param message Message to publish.
 *
 * The caller of stasis_publish_sync will block until the specified
 * subscriber completes handling of the message.
 *
 * All other subscribers to the topic the \ref stasis_subpscription
 * is subscribed to are also delivered the message; this delivery however
 * happens asynchronously.
 *
 * \since 12.1.0
 */
void stasis_publish_sync(struct stasis_subscription *sub, struct stasis_message *message);

/*! @} */

/*! @{ */

/*!
 * \brief Callback function type for Stasis subscriptions.
 * \param data Data field provided with subscription.
 * \param message Published message.
 * \since 12
 */
typedef void (*stasis_subscription_cb)(void *data, struct stasis_subscription *sub, struct stasis_message *message);

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

struct stasis_forward;

/*!
 * \brief Create a subscription which forwards all messages from one topic to
 * another.
 *
 * Note that the \a topic parameter of the invoked callback will the be the
 * \a topic the message was sent to, not the topic the subscriber subscribed to.
 *
 * \param from_topic Topic to forward.
 * \param to_topic Destination topic of forwarded messages.
 * \return New forwarding subscription.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_forward *stasis_forward_all(struct stasis_topic *from_topic,
	struct stasis_topic *to_topic);

struct stasis_forward *stasis_forward_cancel(struct stasis_forward *forward);

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

/*! @{ */

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
	/*! \brief Convenience reference to snapshot type */
	struct stasis_message_type *type;
	/*! \brief Old value from the cache */
	struct stasis_message *old_snapshot;
	/*! \brief New value */
	struct stasis_message *new_snapshot;
};

/*!
 * \brief Message type for clearing a message from a stasis cache.
 * \since 12
 */
struct stasis_message_type *stasis_cache_clear_type(void);

/*! @} */

/*! @{ */

/*!
 * \brief A message cache, for use with \ref stasis_caching_topic.
 * \since 12
 */
struct stasis_cache;

/*! Cache entry used for calculating the aggregate snapshot. */
struct stasis_cache_entry;

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
 * \brief Callback to calculate the aggregate cache entry.
 * \since 12.2.0
 *
 * \param entry Cache entry to calculate a new aggregate snapshot.
 * \param new_snapshot The shapshot that is being updated.
 *
 * \note Return a ref bumped pointer from stasis_cache_entry_get_aggregate()
 * if a new aggregate could not be calculated because of error.
 *
 * \note An aggregate message is a combined representation of the local
 * and remote entities publishing the message data.  e.g., An aggregate
 * device state represents the combined device state from the local and
 * any remote entities publishing state for a device.  e.g., An aggregate
 * MWI message is the old/new MWI counts accumulated from the local and
 * any remote entities publishing to a mailbox.
 *
 * \return New aggregate-snapshot calculated on success.
 * Caller has a reference on return.
 */
typedef struct stasis_message *(*cache_aggregate_calc_fn)(struct stasis_cache_entry *entry, struct stasis_message *new_snapshot);

/*!
 * \brief Callback to publish the aggregate cache entry message.
 * \since 12.2.0
 *
 * \details
 * Once an aggregate message is calculated.  This callback publishes the
 * message so subscribers will know the new value of an aggregated state.
 *
 * \param topic The aggregate message may be published to this topic.
 *        It is the topic to which the cache itself is subscribed.
 * \param aggregate The aggregate shapshot message to publish.
 *
 * \note It is up to the function to determine if there is a better topic
 * the aggregate message should be published over.
 *
 * \note An aggregate message is a combined representation of the local
 * and remote entities publishing the message data.  e.g., An aggregate
 * device state represents the combined device state from the local and
 * any remote entities publishing state for a device.  e.g., An aggregate
 * MWI message is the old/new MWI counts accumulated from the local and
 * any remote entities publishing to a mailbox.
 *
 * \return Nothing
 */
typedef void (*cache_aggregate_publish_fn)(struct stasis_topic *topic, struct stasis_message *aggregate);

/*!
 * \brief Get the aggregate cache entry snapshot.
 * \since 12.2.0
 *
 * \param entry Cache entry to get the aggregate snapshot.
 *
 * \note A reference is not given to the returned pointer so don't unref it.
 *
 * \note An aggregate message is a combined representation of the local
 * and remote entities publishing the message data.  e.g., An aggregate
 * device state represents the combined device state from the local and
 * any remote entities publishing state for a device.  e.g., An aggregate
 * MWI message is the old/new MWI counts accumulated from the local and
 * any remote entities publishing to a mailbox.
 *
 * \retval Aggregate-snapshot in cache.
 * \retval NULL if not present.
 */
struct stasis_message *stasis_cache_entry_get_aggregate(struct stasis_cache_entry *entry);

/*!
 * \brief Get the local entity's cache entry snapshot.
 * \since 12.2.0
 *
 * \param entry Cache entry to get the local entity's snapshot.
 *
 * \note A reference is not given to the returned pointer so don't unref it.
 *
 * \retval Internal-snapshot in cache.
 * \retval NULL if not present.
 */
struct stasis_message *stasis_cache_entry_get_local(struct stasis_cache_entry *entry);

/*!
 * \brief Get a remote entity's cache entry snapshot by index.
 * \since 12.2.0
 *
 * \param entry Cache entry to get a remote entity's snapshot.
 * \param idx Which remote entity's snapshot to get.
 *
 * \note A reference is not given to the returned pointer so don't unref it.
 *
 * \retval Remote-entity-snapshot in cache.
 * \retval NULL if not present.
 */
struct stasis_message *stasis_cache_entry_get_remote(struct stasis_cache_entry *entry, int idx);

/*!
 * \brief Create a cache.
 *
 * This is the backend store for a \ref stasis_caching_topic. The cache is
 * thread safe, allowing concurrent reads and writes.
 *
 * The returned object is AO2 managed, so ao2_cleanup() when you're done.
 *
 * \param id_fn Callback to extract the id from a snapshot message.
 *
 * \retval New cache indexed by \a id_fn.
 * \retval \c NULL on error
 *
 * \since 12
 */
struct stasis_cache *stasis_cache_create(snapshot_get_id id_fn);

/*!
 * \brief Create a cache.
 *
 * This is the backend store for a \ref stasis_caching_topic. The cache is
 * thread safe, allowing concurrent reads and writes.
 *
 * The returned object is AO2 managed, so ao2_cleanup() when you're done.
 *
 * \param id_fn Callback to extract the id from a snapshot message.
 * \param aggregate_calc_fn Callback to calculate the aggregate cache entry.
 * \param aggregate_publish_fn Callback to publish the aggregate cache entry.
 *
 * \note An aggregate message is a combined representation of the local
 * and remote entities publishing the message data.  e.g., An aggregate
 * device state represents the combined device state from the local and
 * any remote entities publishing state for a device.  e.g., An aggregate
 * MWI message is the old/new MWI counts accumulated from the local and
 * any remote entities publishing to a mailbox.
 *
 * \retval New cache indexed by \a id_fn.
 * \retval \c NULL on error
 *
 * \since 12.2.0
 */
struct stasis_cache *stasis_cache_create_full(snapshot_get_id id_fn, cache_aggregate_calc_fn aggregate_calc_fn, cache_aggregate_publish_fn aggregate_publish_fn);

/*!
 * \brief Create a topic which monitors and caches messages from another topic.
 *
 * The idea is that some topics publish 'snapshots' of some other object's state
 * that should be cached. When these snapshot messages are received, the cache
 * is updated, and a stasis_cache_update() message is forwarded, which has both
 * the original snapshot message and the new message.
 *
 * The returned object is AO2 managed, so ao2_cleanup() when done with it.
 *
 * \param original_topic Topic publishing snapshot messages.
 * \param cache Backend cache in which to keep snapshots.
 * \return New topic which changes snapshot messages to stasis_cache_update()
 *         messages, and forwards all other messages from the original topic.
 * \return \c NULL on error
 * \since 12
 */
struct stasis_caching_topic *stasis_caching_topic_create(
	struct stasis_topic *original_topic, struct stasis_cache *cache);

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
 * \return The topic that publishes cache update events, along with passthrough
 *         events from the underlying topic.
 * \return \c NULL if \a caching_topic is \c NULL.
 * \since 12
 */
struct stasis_topic *stasis_caching_get_topic(
	struct stasis_caching_topic *caching_topic);

/*!
 * \brief A message which instructs the caching topic to remove an entry from
 * its cache.
 *
 * \param message Message representative of the cache entry that should be
 *                cleared. This will become the data held in the
 *                stasis_cache_clear message.
 *
 * \return Message which, when sent to a \ref stasis_caching_topic, will clear
 *         the item from the cache.
 * \return \c NULL on error.
 * \since 12
 */
struct stasis_message *stasis_cache_clear_create(struct stasis_message *message);

/*!
 * \brief Retrieve an item from the cache for the ast_eid_default entity.
 *
 * The returned item is AO2 managed, so ao2_cleanup() when you're done with it.
 *
 * \param cache The cache to query.
 * \param type Type of message to retrieve.
 * \param id Identity of the snapshot to retrieve.
 *
 * \retval Message from the cache.
 * \retval \c NULL if message is not found.
 *
 * \since 12
 */
struct stasis_message *stasis_cache_get(struct stasis_cache *cache, struct stasis_message_type *type, const char *id);

/*!
 * \brief Retrieve an item from the cache for a specific entity.
 *
 * The returned item is AO2 managed, so ao2_cleanup() when you're done with it.
 *
 * \param cache The cache to query.
 * \param type Type of message to retrieve.
 * \param id Identity of the snapshot to retrieve.
 * \param eid Specific entity id to retrieve.  NULL for aggregate.
 *
 * \note An aggregate message is a combined representation of the local
 * and remote entities publishing the message data.  e.g., An aggregate
 * device state represents the combined device state from the local and
 * any remote entities publishing state for a device.  e.g., An aggregate
 * MWI message is the old/new MWI counts accumulated from the local and
 * any remote entities publishing to a mailbox.
 *
 * \retval Message from the cache.
 * \retval \c NULL if message is not found.
 *
 * \since 12.2.0
 */
struct stasis_message *stasis_cache_get_by_eid(struct stasis_cache *cache, struct stasis_message_type *type, const char *id, const struct ast_eid *eid);

/*!
 * \brief Retrieve all matching entity items from the cache.
 * \since 12.2.0
 *
 * \param cache The cache to query.
 * \param type Type of message to retrieve.
 * \param id Identity of the snapshot to retrieve.
 *
 * \retval Container of matching items found.
 * \retval \c NULL if error.
 */
struct ao2_container *stasis_cache_get_all(struct stasis_cache *cache, struct stasis_message_type *type, const char *id);

/*!
 * \brief Dump cached items to a subscription for the ast_eid_default entity.
 *
 * \param cache The cache to query.
 * \param type Type of message to dump (any type if \c NULL).
 *
 * \retval ao2_container containing all matches (must be unreffed by caller)
 * \retval \c NULL on allocation error
 *
 * \since 12
 */
struct ao2_container *stasis_cache_dump(struct stasis_cache *cache, struct stasis_message_type *type);

/*!
 * \brief Dump cached items to a subscription for a specific entity.
 * \since 12.2.0
 *
 * \param cache The cache to query.
 * \param type Type of message to dump (any type if \c NULL).
 * \param eid Specific entity id to retrieve.  NULL for aggregate.
 *
 * \retval ao2_container containing all matches (must be unreffed by caller)
 * \retval \c NULL on allocation error
 */
struct ao2_container *stasis_cache_dump_by_eid(struct stasis_cache *cache, struct stasis_message_type *type, const struct ast_eid *eid);

/*!
 * \brief Dump all entity items from the cache to a subscription.
 * \since 12.2.0
 *
 * \param cache The cache to query.
 * \param type Type of message to dump (any type if \c NULL).
 *
 * \retval ao2_container containing all matches (must be unreffed by caller)
 * \retval \c NULL on allocation error
 */
struct ao2_container *stasis_cache_dump_all(struct stasis_cache *cache, struct stasis_message_type *type);

/*!
 * \brief Object type code for multi user object snapshots
 */
enum stasis_user_multi_object_snapshot_type {
	STASIS_UMOS_CHANNEL = 0,     /*!< Channel Snapshots */
	STASIS_UMOS_BRIDGE,          /*!< Bridge Snapshots */
	STASIS_UMOS_ENDPOINT,        /*!< Endpoint Snapshots */
};

/*! \brief Number of snapshot types */
#define STASIS_UMOS_MAX (STASIS_UMOS_ENDPOINT + 1)

/*!
 * \brief Message type for custom user defined events with multi object blobs
 * \return The stasis_message_type for user event
 * \since 12.3.0
 */
struct stasis_message_type *ast_multi_user_event_type(void);

/*!
 * \brief Create a stasis multi object blob
 * \since 12.3.0
 *
 * \details
 * Multi object blob can store a combination of arbitrary json values
 * (the blob) and also snapshots of various other system objects (such
 * as channels, bridges, etc) for delivery through a stasis message.
 * The multi object blob is first created, then optionally objects
 * are added to it, before being attached to a message and delivered
 * to stasis topic.
 *
 * \param blob Json blob
 *
 * \note When used for an ast_multi_user_event_type message, the
 * json blob should contain at minimum {eventname: name}.
 *
 * \retval ast_multi_object_blob* if succeeded
 * \retval NULL if creation failed
 */
struct ast_multi_object_blob *ast_multi_object_blob_create(struct ast_json *blob);

/*!
 * \brief Add an object to a multi object blob previously created
 * \since 12.3.0
 *
 * \param multi The multi object blob previously created
 * \param type Type code for the object such as channel, bridge, etc.
 * \param object Snapshot object of the type supplied to typename
 *
 * \return Nothing
 */
void ast_multi_object_blob_add(struct ast_multi_object_blob *multi, enum stasis_user_multi_object_snapshot_type type, void *object);

/*!
 * \brief Create and publish a stasis message blob on a channel with it's snapshot
 * \since 12.3.0
 *
 * \details
 * For compatibility with app_userevent, this creates a multi object
 * blob message, attaches the channel snapshot to it, and publishes it
 * to the channel's topic.
 *
 * \param chan The channel to snapshot and publish event to
 * \param type The message type
 * \param blob A json blob to publish with the snapshot
 *
 * \return Nothing
 */
void ast_multi_object_blob_single_channel_publish(struct ast_channel *chan, struct stasis_message_type *type, struct ast_json *blob);


/*! @} */

/*! @{ */

/*!
 * \internal
 * \brief Log a message about invalid attempt to access a type.
 */
void stasis_log_bad_type_access(const char *name);

/*!
 * \brief Boiler-plate messaging macro for defining public message types.
 *
 * \code
 *	STASIS_MESSAGE_TYPE_DEFN(ast_foo_type,
 *		.to_ami = foo_to_ami,
 *		.to_json = foo_to_json,
 *		.to_event = foo_to_event,
 *		);
 * \endcode
 *
 * \param name Name of message type.
 * \param ... Virtual table methods for messages of this type.
 * \since 12
 */
#define STASIS_MESSAGE_TYPE_DEFN(name, ...)				\
	static struct stasis_message_vtable _priv_ ## name ## _v = {	\
		__VA_ARGS__						\
	};								\
	static struct stasis_message_type *_priv_ ## name;		\
	struct stasis_message_type *name(void) {			\
		if (_priv_ ## name == NULL) {				\
			stasis_log_bad_type_access(#name);		\
		}							\
		return _priv_ ## name;					\
	}

/*!
 * \brief Boiler-plate messaging macro for defining local message types.
 *
 * \code
 *	STASIS_MESSAGE_TYPE_DEFN_LOCAL(ast_foo_type,
 *		.to_ami = foo_to_ami,
 *		.to_json = foo_to_json,
 *		.to_event = foo_to_event,
 *		);
 * \endcode
 *
 * \param name Name of message type.
 * \param ... Virtual table methods for messages of this type.
 * \since 12
 */
#define STASIS_MESSAGE_TYPE_DEFN_LOCAL(name, ...)			\
	static struct stasis_message_vtable _priv_ ## name ## _v = {	\
		__VA_ARGS__						\
	};								\
	static struct stasis_message_type *_priv_ ## name;		\
	static struct stasis_message_type *name(void) {			\
		if (_priv_ ## name == NULL) {				\
			stasis_log_bad_type_access(#name);		\
		}							\
		return _priv_ ## name;					\
	}

/*!
* \brief Boiler-plate messaging macro for initializing message types.
 *
 * \code
 *	if (STASIS_MESSAGE_TYPE_INIT(ast_foo_type) != 0) {
 *		return -1;
 *	}
 * \endcode
 *
 * \param name Name of message type.
 * \return 0 if initialization is successful.
 * \return Non-zero on failure.
 * \since 12
 */
#define STASIS_MESSAGE_TYPE_INIT(name)					\
	({								\
		ast_assert(_priv_ ## name == NULL);			\
		stasis_message_type_create(#name,	\
			&_priv_ ## name ## _v, &_priv_ ## name) == STASIS_MESSAGE_TYPE_ERROR ? 1 : 0;	\
	})

/*!
 * \brief Boiler-plate messaging macro for cleaning up message types.
 *
 * Note that if your type is defined in core instead of a loadable module, you
 * should call message type cleanup from an ast_register_cleanup() handler
 * instead of an ast_register_atexit() handler.
 *
 * The reason is that during an immediate shutdown, loadable modules (which may
 * refer to core message types) are not unloaded. While the atexit handlers are
 * run, there's a window of time where a module subscription might reference a
 * core message type after it's been cleaned up. Which is bad.
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
 * \brief Initialize the Stasis subsystem.
 * \return 0 on success.
 * \return Non-zero on error.
 * \since 12
 */
int stasis_init(void);

/*! @} */

/*! @{ */

/*!
 * \internal
 * \brief called by stasis_init() for cache initialization.
 * \return 0 on success.
 * \return Non-zero on error.
 * \since 12
 */
int stasis_cache_init(void);

/*!
 * \internal
 * \brief called by stasis_init() for config initialization.
 * \return 0 on success.
 * \return Non-zero on error.
 * \since 12
 */
int stasis_config_init(void);

/*! @} */

/*!
 * \defgroup StasisTopicsAndMessages Stasis topics, and their messages.
 *
 * This group contains the topics, messages and corresponding message types
 * found within Asterisk.
 */

#endif /* _ASTERISK_STASIS_H */
