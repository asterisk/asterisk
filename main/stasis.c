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

/*! \file
 *
 * \brief Stasis Message Bus API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis_internal.h"
#include "asterisk/stasis.h"
#include "asterisk/threadpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/utils.h"
#include "asterisk/uuid.h"

/*!
 * \page stasis-impl Stasis Implementation Notes
 *
 * \par Reference counting
 *
 * Stasis introduces a number of objects, which are tightly related to one
 * another. Because we rely on ref-counting for memory management, understanding
 * these relationships is important to understanding this code.
 *
 * \code{.txt}
 *
 *   stasis_topic <----> stasis_subscription
 *             ^          ^
 *              \        /
 *               \      /
 *               dispatch
 *                  |
 *                  |
 *                  v
 *            stasis_message
 *                  |
 *                  |
 *                  v
 *          stasis_message_type
 *
 * \endcode
 *
 * The most troubling thing in this chart is the cyclic reference between
 * stasis_topic and stasis_subscription. This is both unfortunate, and
 * necessary. Topics need the subscription in order to dispatch messages;
 * subscriptions need the topics to unsubscribe and check subscription status.
 *
 * The cycle is broken by stasis_unsubscribe(). The unsubscribe will remove the
 * topic's reference to a subscription. When the subcription is destroyed, it
 * will remove its reference to the topic.
 *
 * This means that until a subscription has be explicitly unsubscribed, it will
 * not be destroyed. Neither will a topic be destroyed while it has subscribers.
 * The destructors of both have assertions regarding this to catch ref-counting
 * problems where a subscription or topic has had an extra ao2_cleanup().
 *
 * The \ref dispatch object is a transient object, which is posted to a
 * subscription's taskprocessor to send a message to the subscriber. They have
 * short life cycles, allocated on one thread, destroyed on another.
 *
 * During shutdown, or the deletion of a domain object, there are a flurry of
 * ao2_cleanup()s on subscriptions and topics, as the final in-flight messages
 * are processed. Any one of these cleanups could be the one to actually destroy
 * a given object, so care must be taken to ensure that an object isn't
 * referenced after an ao2_cleanup(). This includes the implicit ao2_unlock()
 * that might happen when a RAII_VAR() goes out of scope.
 *
 * \par Typical life cycles
 *
 *  \li stasis_topic - There are several topics which live for the duration of
 *      the Asterisk process (ast_channel_topic_all(), etc.) but most of these
 *      are actually fed by shorter-lived topics whose lifetime is associated
 *      with some domain object (like ast_channel_topic() for a given
 *      ast_channel).
 *
 *  \li stasis_subscription - Subscriptions have a similar mix of lifetimes as
 *      topics, for similar reasons.
 *
 *  \li dispatch - Very short lived; just long enough to post a message to a
 *      subscriber.
 *
 *  \li stasis_message - Short to intermediate lifetimes, but that is mostly
 *      irrelevant. Messages are strictly data and have no behavior associated
 *      with them, so it doesn't really matter if/when they are destroyed. By
 *      design, a component could hold a ref to a message forever without any
 *      ill consequences (aside from consuming more memory).
 *
 *  \li stasis_message_type - Long life cycles, typically only destroyed on
 *      module unloading or _clean_ process exit.
 *
 * \par Subscriber shutdown sequencing
 *
 * Subscribers are sensitive to shutdown sequencing, specifically in how the
 * reference message types. This is fully detailed on the wiki at
 * https://wiki.asterisk.org/wiki/x/K4BqAQ.
 *
 * In short, the lifetime of the \a data (and \a callback, if in a module) must
 * be held until the stasis_subscription_final_message() has been received.
 * Depending on the structure of the subscriber code, this can be handled by
 * using stasis_subscription_final_message() to free resources on the final
 * message, or using stasis_subscription_join()/stasis_unsubscribe_and_join() to
 * block until the unsubscribe has completed.
 */

/*! Initial size of the subscribers list. */
#define INITIAL_SUBSCRIBERS_MAX 4

/*! The number of buckets to use for topic pools */
#define TOPIC_POOL_BUCKETS 57

/*! Threadpool for dispatching notifications to subscribers */
static struct ast_threadpool *pool;

STASIS_MESSAGE_TYPE_DEFN(stasis_subscription_change_type);

/*! \internal */
struct stasis_topic {
	char *name;
	/*! Variable length array of the subscribers */
	struct stasis_subscription **subscribers;
	/*! Allocated length of the subscribers array */
	size_t num_subscribers_max;
	/*! Current size of the subscribers array */
	size_t num_subscribers_current;
};

/* Forward declarations for the tightly-coupled subscription object */
static int topic_add_subscription(struct stasis_topic *topic, struct stasis_subscription *sub);

static void topic_dtor(void *obj)
{
	struct stasis_topic *topic = obj;

	/* Subscribers hold a reference to topics, so they should all be
	 * unsubscribed before we get here. */
	ast_assert(topic->num_subscribers_current == 0);
	ast_free(topic->name);
	topic->name = NULL;
	ast_free(topic->subscribers);
	topic->subscribers = NULL;
}

struct stasis_topic *stasis_topic_create(const char *name)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);

	topic = ao2_alloc(sizeof(*topic), topic_dtor);

	if (!topic) {
		return NULL;
	}

	topic->name = ast_strdup(name);
	if (!topic->name) {
		return NULL;
	}

	topic->num_subscribers_max = INITIAL_SUBSCRIBERS_MAX;
	topic->subscribers = ast_calloc(topic->num_subscribers_max, sizeof(*topic->subscribers));
	if (!topic->subscribers) {
		return NULL;
	}

	ao2_ref(topic, +1);
	return topic;
}

const char *stasis_topic_name(const struct stasis_topic *topic)
{
	return topic->name;
}

/*! \internal */
struct stasis_subscription {
	/*! Unique ID for this subscription */
	char uniqueid[AST_UUID_STR_LEN];
	/*! Topic subscribed to. */
	struct stasis_topic *topic;
	/*! Mailbox for processing incoming messages. */
	struct ast_taskprocessor *mailbox;
	/*! Callback function for incoming message processing. */
	stasis_subscription_cb callback;
	/*! Data pointer to be handed to the callback. */
	void *data;

	/*! Lock for completion flags \c final_message_{rxed,processed}. */
	ast_mutex_t join_lock;
	/*! Condition for joining with subscription. */
	ast_cond_t join_cond;
	/*! Flag set when final message for sub has been received.
	 *  Be sure join_lock is held before reading/setting. */
	int final_message_rxed;
	/*! Flag set when final message for sub has been processed.
	 *  Be sure join_lock is held before reading/setting. */
	int final_message_processed;
};

static void subscription_dtor(void *obj)
{
	struct stasis_subscription *sub = obj;

	/* Subscriptions need to be manually unsubscribed before destruction
	 * b/c there's a cyclic reference between topics and subscriptions */
	ast_assert(!stasis_subscription_is_subscribed(sub));
	/* If there are any messages in flight to this subscription; that would
	 * be bad. */
	ast_assert(stasis_subscription_is_done(sub));

	ao2_cleanup(sub->topic);
	sub->topic = NULL;
	ast_taskprocessor_unreference(sub->mailbox);
	sub->mailbox = NULL;
	ast_mutex_destroy(&sub->join_lock);
	ast_cond_destroy(&sub->join_cond);
}

/*!
 * \brief Invoke the subscription's callback.
 * \param sub Subscription to invoke.
 * \param topic Topic message was published to.
 * \param message Message to send.
 */
static void subscription_invoke(struct stasis_subscription *sub,
				  struct stasis_topic *topic,
				  struct stasis_message *message)
{
	/* Notify that the final message has been received */
	if (stasis_subscription_final_message(sub, message)) {
		SCOPED_MUTEX(lock, &sub->join_lock);
		sub->final_message_rxed = 1;
		ast_cond_signal(&sub->join_cond);
	}

	/* Since sub is mostly immutable, no need to lock sub */
	sub->callback(sub->data, sub, topic, message);

	/* Notify that the final message has been processed */
	if (stasis_subscription_final_message(sub, message)) {
		SCOPED_MUTEX(lock, &sub->join_lock);
		sub->final_message_processed = 1;
		ast_cond_signal(&sub->join_cond);
	}
}

static void send_subscription_change_message(struct stasis_topic *topic, char *uniqueid, char *description);

struct stasis_subscription *internal_stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data,
	int needs_mailbox)
{
	RAII_VAR(struct stasis_subscription *, sub, NULL, ao2_cleanup);

	sub = ao2_alloc(sizeof(*sub), subscription_dtor);
	if (!sub) {
		return NULL;
	}

	ast_uuid_generate_str(sub->uniqueid, sizeof(sub->uniqueid));

	if (needs_mailbox) {
		sub->mailbox = ast_threadpool_serializer(sub->uniqueid, pool);
		if (!sub->mailbox) {
			return NULL;
		}
	}

	ao2_ref(topic, +1);
	sub->topic = topic;
	sub->callback = callback;
	sub->data = data;
	ast_mutex_init(&sub->join_lock);
	ast_cond_init(&sub->join_cond, NULL);

	if (topic_add_subscription(topic, sub) != 0) {
		return NULL;
	}
	send_subscription_change_message(topic, sub->uniqueid, "Subscribe");

	ao2_ref(sub, +1);
	return sub;
}

struct stasis_subscription *stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data)
{
	return internal_stasis_subscribe(topic, callback, data, 1);
}

struct stasis_subscription *stasis_unsubscribe(struct stasis_subscription *sub)
{
	if (sub) {
		size_t i;
		/* The subscription may be the last ref to this topic. Hold
		 * the topic ref open until after the unlock. */
		RAII_VAR(struct stasis_topic *, topic, ao2_bump(sub->topic),
			ao2_cleanup);
		SCOPED_AO2LOCK(lock_topic, topic);

		for (i = 0; i < topic->num_subscribers_current; ++i) {
			if (topic->subscribers[i] == sub) {
				send_subscription_change_message(topic, sub->uniqueid, "Unsubscribe");
				/* swap [i] with last entry; remove last entry */
				topic->subscribers[i] = topic->subscribers[--topic->num_subscribers_current];
				/* Unsubscribing unrefs the subscription */
				ao2_cleanup(sub);
				return NULL;
			}
		}

		ast_log(LOG_ERROR, "Internal error: subscription has invalid topic\n");
	}
	return NULL;
}

void stasis_subscription_join(struct stasis_subscription *subscription)
{
	if (subscription) {
		SCOPED_MUTEX(lock, &subscription->join_lock);
		/* Wait until the processed flag has been set */
		while (!subscription->final_message_processed) {
			ast_cond_wait(&subscription->join_cond,
				&subscription->join_lock);
		}
	}
}

int stasis_subscription_is_done(struct stasis_subscription *subscription)
{
	if (subscription) {
		SCOPED_MUTEX(lock, &subscription->join_lock);
		return subscription->final_message_rxed;
	}

	/* Null subscription is about as done as you can get */
	return 1;
}

struct stasis_subscription *stasis_unsubscribe_and_join(
	struct stasis_subscription *subscription)
{
	if (!subscription) {
		return NULL;
	}

	/* Bump refcount to hold it past the unsubscribe */
	ao2_ref(subscription, +1);
	stasis_unsubscribe(subscription);
	stasis_subscription_join(subscription);
	/* Now decrement the refcount back */
	ao2_cleanup(subscription);
	return NULL;
}

int stasis_subscription_is_subscribed(const struct stasis_subscription *sub)
{
	if (sub) {
		size_t i;
		struct stasis_topic *topic = sub->topic;
		SCOPED_AO2LOCK(lock_topic, topic);

		for (i = 0; i < topic->num_subscribers_current; ++i) {
			if (topic->subscribers[i] == sub) {
				return 1;
			}
		}
	}

	return 0;
}

const char *stasis_subscription_uniqueid(const struct stasis_subscription *sub)
{
	return sub->uniqueid;
}

int stasis_subscription_final_message(struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct stasis_subscription_change *change;
	if (stasis_message_type(msg) != stasis_subscription_change_type()) {
		return 0;
	}

	change = stasis_message_data(msg);
	if (strcmp("Unsubscribe", change->description)) {
		return 0;
	}

	if (strcmp(stasis_subscription_uniqueid(sub), change->uniqueid)) {
		return 0;
	}

	return 1;
}

/*!
 * \brief Add a subscriber to a topic.
 * \param topic Topic
 * \param sub Subscriber
 * \return 0 on success
 * \return Non-zero on error
 */
static int topic_add_subscription(struct stasis_topic *topic, struct stasis_subscription *sub)
{
	struct stasis_subscription **subscribers;
	SCOPED_AO2LOCK(lock, topic);

	/* Increase list size, if needed */
	if (topic->num_subscribers_current + 1 > topic->num_subscribers_max) {
		subscribers = realloc(topic->subscribers, 2 * topic->num_subscribers_max * sizeof(*subscribers));
		if (!subscribers) {
			return -1;
		}
		topic->subscribers = subscribers;
		topic->num_subscribers_max *= 2;
	}

	/* The reference from the topic to the subscription is shared with
	 * the owner of the subscription, which will explicitly unsubscribe
	 * to release it.
	 *
	 * If we bumped the refcount here, the owner would have to unsubscribe
	 * and cleanup, which is a bit awkward. */
	topic->subscribers[topic->num_subscribers_current++] = sub;
	return 0;
}

/*!
 * \internal
 * \brief Information needed to dispatch a message to a subscription
 */
struct dispatch {
	/*! Topic message was published to */
	struct stasis_topic *topic;
	/*! The message itself */
	struct stasis_message *message;
	/*! Subscription receiving the message */
	struct stasis_subscription *sub;
};

static void dispatch_dtor(void *data)
{
	struct dispatch *dispatch = data;
	ao2_cleanup(dispatch->topic);
	ao2_cleanup(dispatch->message);
	ao2_cleanup(dispatch->sub);
}

static struct dispatch *dispatch_create(struct stasis_topic *topic, struct stasis_message *message, struct stasis_subscription *sub)
{
	RAII_VAR(struct dispatch *, dispatch, NULL, ao2_cleanup);

	ast_assert(topic != NULL);
	ast_assert(message != NULL);
	ast_assert(sub != NULL);

	dispatch = ao2_alloc(sizeof(*dispatch), dispatch_dtor);
	if (!dispatch) {
		return NULL;
	}

	dispatch->topic = topic;
	ao2_ref(topic, +1);

	dispatch->message = message;
	ao2_ref(message, +1);

	dispatch->sub = sub;
	ao2_ref(sub, +1);

	ao2_ref(dispatch, +1);
	return dispatch;
}

/*!
 * \brief Dispatch a message to a subscriber
 * \param data \ref dispatch object
 * \return 0
 */
static int dispatch_exec(void *data)
{
	RAII_VAR(struct dispatch *, dispatch, data, ao2_cleanup);

	subscription_invoke(dispatch->sub, dispatch->topic, dispatch->message);

	return 0;
}

void stasis_forward_message(struct stasis_topic *_topic, struct stasis_topic *publisher_topic, struct stasis_message *message)
{
	size_t i;
	/* The topic may be unref'ed by the subscription invocation.
	 * Make sure we hold onto a reference while dispatching. */
	RAII_VAR(struct stasis_topic *, topic, ao2_bump(_topic),
		ao2_cleanup);
	SCOPED_AO2LOCK(lock, topic);

	ast_assert(topic != NULL);
	ast_assert(publisher_topic != NULL);
	ast_assert(message != NULL);

	for (i = 0; i < topic->num_subscribers_current; ++i) {
		struct stasis_subscription *sub = topic->subscribers[i];

		ast_assert(sub != NULL);

		if (sub->mailbox) {
			RAII_VAR(struct dispatch *, dispatch, NULL, ao2_cleanup);

			dispatch = dispatch_create(publisher_topic, message, sub);
			if (!dispatch) {
				ast_log(LOG_DEBUG, "Dropping dispatch\n");
				break;
			}

			if (ast_taskprocessor_push(sub->mailbox, dispatch_exec, dispatch) == 0) {
				/* Ownership transferred to mailbox.
				 * Don't increment ref, b/c the task processor
				 * may have already gotten rid of the object.
				 */
				dispatch = NULL;
			}
		} else {
			/* Dispatch directly */
			subscription_invoke(sub, publisher_topic, message);
		}
	}
}

void stasis_publish(struct stasis_topic *topic, struct stasis_message *message)
{
	stasis_forward_message(topic, topic, message);
}

/*! \brief Forwarding subscriber */
static void stasis_forward_cb(void *data, struct stasis_subscription *sub, struct stasis_topic *topic, struct stasis_message *message)
{
	struct stasis_topic *to_topic = data;
	stasis_forward_message(to_topic, topic, message);

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(to_topic);
	}
}

struct stasis_subscription *stasis_forward_all(struct stasis_topic *from_topic, struct stasis_topic *to_topic)
{
	struct stasis_subscription *sub;
	if (!from_topic || !to_topic) {
		return NULL;
	}

	/* Forwarding subscriptions should dispatch directly instead of having a
	 * mailbox. Otherwise, messages forwarded to the same topic from
	 * different topics may get reordered. Which is bad.
	 */
	sub = internal_stasis_subscribe(from_topic, stasis_forward_cb, to_topic, 0);
	if (sub) {
		/* hold a ref to to_topic for this forwarding subscription */
		ao2_ref(to_topic, +1);
	}
	return sub;
}

static void subscription_change_dtor(void *obj)
{
	struct stasis_subscription_change *change = obj;
	ast_string_field_free_memory(change);
	ao2_cleanup(change->topic);
}

static struct stasis_subscription_change *subscription_change_alloc(struct stasis_topic *topic, char *uniqueid, char *description)
{
	RAII_VAR(struct stasis_subscription_change *, change, NULL, ao2_cleanup);

	change = ao2_alloc(sizeof(struct stasis_subscription_change), subscription_change_dtor);
	if (ast_string_field_init(change, 128)) {
		return NULL;
	}

	ast_string_field_set(change, uniqueid, uniqueid);
	ast_string_field_set(change, description, description);
	ao2_ref(topic, +1);
	change->topic = topic;

	ao2_ref(change, +1);
	return change;
}

static void send_subscription_change_message(struct stasis_topic *topic, char *uniqueid, char *description)
{
	RAII_VAR(struct stasis_subscription_change *, change, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	change = subscription_change_alloc(topic, uniqueid, description);

	if (!change) {
		return;
	}

	msg = stasis_message_create(stasis_subscription_change_type(), change);

	if (!msg) {
		return;
	}

	stasis_publish(topic, msg);
}

struct topic_pool_entry {
	struct stasis_subscription *forward;
	struct stasis_topic *topic;
};

static void topic_pool_entry_dtor(void *obj)
{
	struct topic_pool_entry *entry = obj;
	entry->forward = stasis_unsubscribe(entry->forward);
	ao2_cleanup(entry->topic);
	entry->topic = NULL;
}

static struct topic_pool_entry *topic_pool_entry_alloc(void)
{
	return ao2_alloc(sizeof(struct topic_pool_entry), topic_pool_entry_dtor);
}

struct stasis_topic_pool {
	struct ao2_container *pool_container;
	struct stasis_topic *pool_topic;
};

static void topic_pool_dtor(void *obj)
{
	struct stasis_topic_pool *pool = obj;
	ao2_cleanup(pool->pool_container);
	pool->pool_container = NULL;
	ao2_cleanup(pool->pool_topic);
	pool->pool_topic = NULL;
}

static int topic_pool_entry_hash(const void *obj, const int flags)
{
	const char *topic_name = (flags & OBJ_KEY) ? obj : stasis_topic_name(((struct topic_pool_entry*) obj)->topic);
	return ast_str_case_hash(topic_name);
}

static int topic_pool_entry_cmp(void *obj, void *arg, int flags)
{
	struct topic_pool_entry *opt1 = obj, *opt2 = arg;
	const char *topic_name = (flags & OBJ_KEY) ? arg : stasis_topic_name(opt2->topic);
	return strcasecmp(stasis_topic_name(opt1->topic), topic_name) ? 0 : CMP_MATCH | CMP_STOP;
}

struct stasis_topic_pool *stasis_topic_pool_create(struct stasis_topic *pooled_topic)
{
	RAII_VAR(struct stasis_topic_pool *, pool, ao2_alloc(sizeof(*pool), topic_pool_dtor), ao2_cleanup);
	if (!pool) {
		return NULL;
	}
	pool->pool_container = ao2_container_alloc(TOPIC_POOL_BUCKETS, topic_pool_entry_hash, topic_pool_entry_cmp);
	ao2_ref(pooled_topic, +1);
	pool->pool_topic = pooled_topic;

	ao2_ref(pool, +1);
	return pool;
}

struct stasis_topic *stasis_topic_pool_get_topic(struct stasis_topic_pool *pool, const char *topic_name)
{
	RAII_VAR(struct topic_pool_entry *, topic_pool_entry, NULL, ao2_cleanup);
	SCOPED_AO2LOCK(topic_container_lock, pool->pool_container);
	topic_pool_entry = ao2_find(pool->pool_container, topic_name, OBJ_KEY | OBJ_NOLOCK);

	if (topic_pool_entry) {
		return topic_pool_entry->topic;
	}

	topic_pool_entry = topic_pool_entry_alloc();

	if (!topic_pool_entry) {
		return NULL;
	}

	topic_pool_entry->topic = stasis_topic_create(topic_name);
	if (!topic_pool_entry->topic) {
		return NULL;
	}

	topic_pool_entry->forward = stasis_forward_all(topic_pool_entry->topic, pool->pool_topic);
	if (!topic_pool_entry->forward) {
		return NULL;
	}

	ao2_link_flags(pool->pool_container, topic_pool_entry, OBJ_NOLOCK);

	return topic_pool_entry->topic;
}

void stasis_log_bad_type_access(const char *name)
{
	ast_log(LOG_ERROR, "Use of %s() before init/after destruction\n", name);
}

/*! \brief Shutdown function */
static void stasis_exit(void)
{
	ast_threadpool_shutdown(pool);
	pool = NULL;
}

/*! \brief Cleanup function for graceful shutdowns */
static void stasis_cleanup(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_subscription_change_type);
}

int stasis_init(void)
{
	int cache_init;

	struct ast_threadpool_options opts;

	/* Be sure the types are cleaned up after the message bus */
	ast_register_cleanup(stasis_cleanup);
	ast_register_atexit(stasis_exit);

	if (stasis_config_init() != 0) {
		ast_log(LOG_ERROR, "Stasis configuration failed\n");
		return -1;
	}

	if (stasis_wait_init() != 0) {
		ast_log(LOG_ERROR, "Stasis initialization failed\n");
		return -1;
	}

	if (pool) {
		ast_log(LOG_ERROR, "Stasis double-initialized\n");
		return -1;
	}

	stasis_config_get_threadpool_options(&opts);
	ast_debug(3, "Creating Stasis threadpool: initial_size = %d, max_size = %d, idle_timeout_secs = %d\n",
		opts.initial_size, opts.max_size, opts.idle_timeout);
	pool = ast_threadpool_create("stasis-core", NULL, &opts);
	if (!pool) {
		ast_log(LOG_ERROR, "Stasis threadpool allocation failed\n");
		return -1;
	}

	cache_init = stasis_cache_init();
	if (cache_init != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(stasis_subscription_change_type) != 0) {
		return -1;
	}

	return 0;
}
