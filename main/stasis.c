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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/astobj2.h"
#include "asterisk/stasis_internal.h"
#include "asterisk/stasis.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/utils.h"
#include "asterisk/uuid.h"
#include "asterisk/vector.h"

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

STASIS_MESSAGE_TYPE_DEFN(stasis_subscription_change_type);

/*! \internal */
struct stasis_topic {
	char *name;
	/*! Variable length array of the subscribers */
	AST_VECTOR(, struct stasis_subscription *) subscribers;

	/*! Topics forwarding into this topic */
	AST_VECTOR(, struct stasis_topic *) upstream_topics;
};

/* Forward declarations for the tightly-coupled subscription object */
static int topic_add_subscription(struct stasis_topic *topic,
	struct stasis_subscription *sub);

static int topic_remove_subscription(struct stasis_topic *topic, struct stasis_subscription *sub);

/*! \brief Lock two topics. */
#define topic_lock_both(topic1, topic2) \
	do { \
		ao2_lock(topic1); \
		while (ao2_trylock(topic2)) { \
			AO2_DEADLOCK_AVOIDANCE(topic1); \
		} \
	} while (0)

static void topic_dtor(void *obj)
{
	struct stasis_topic *topic = obj;

	/* Subscribers hold a reference to topics, so they should all be
	 * unsubscribed before we get here. */
	ast_assert(AST_VECTOR_SIZE(&topic->subscribers) == 0);

	ast_free(topic->name);
	topic->name = NULL;

	AST_VECTOR_FREE(&topic->subscribers);
	AST_VECTOR_FREE(&topic->upstream_topics);
}

struct stasis_topic *stasis_topic_create(const char *name)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	int res = 0;

	topic = ao2_alloc(sizeof(*topic), topic_dtor);

	if (!topic) {
		return NULL;
	}

	topic->name = ast_strdup(name);
	if (!topic->name) {
		return NULL;
	}

	res |= AST_VECTOR_INIT(&topic->subscribers, INITIAL_SUBSCRIBERS_MAX);
	res |= AST_VECTOR_INIT(&topic->upstream_topics, 0);

	if (res != 0) {
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
				  struct stasis_message *message)
{
	/* Notify that the final message has been received */
	if (stasis_subscription_final_message(sub, message)) {
		SCOPED_MUTEX(lock, &sub->join_lock);
		sub->final_message_rxed = 1;
		ast_cond_signal(&sub->join_cond);
	}

	/* Since sub is mostly immutable, no need to lock sub */
	sub->callback(sub->data, sub, message);

	/* Notify that the final message has been processed */
	if (stasis_subscription_final_message(sub, message)) {
		SCOPED_MUTEX(lock, &sub->join_lock);
		sub->final_message_processed = 1;
		ast_cond_signal(&sub->join_cond);
	}
}

static void send_subscription_subscribe(struct stasis_topic *topic, struct stasis_subscription *sub);
static void send_subscription_unsubscribe(struct stasis_topic *topic, struct stasis_subscription *sub);

struct stasis_subscription *internal_stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data,
	int needs_mailbox)
{
	RAII_VAR(struct stasis_subscription *, sub, NULL, ao2_cleanup);

	if (!topic) {
		return NULL;
	}

	sub = ao2_alloc(sizeof(*sub), subscription_dtor);
	if (!sub) {
		return NULL;
	}

	ast_uuid_generate_str(sub->uniqueid, sizeof(sub->uniqueid));

	if (needs_mailbox) {
		/* With a small number of subscribers, a thread-per-sub is
		 * acceptable. If our usage changes so that we have larger
		 * numbers of subscribers, we'll probably want to consider
		 * a threadpool. We had that originally, but with so few
		 * subscribers it was actually a performance loss instead of
		 * a gain.
		 */
		sub->mailbox = ast_taskprocessor_get(sub->uniqueid,
			TPS_REF_DEFAULT);
		if (!sub->mailbox) {
			return NULL;
		}
		ast_taskprocessor_set_local(sub->mailbox, sub);
		/* Taskprocessor has a reference */
		ao2_ref(sub, +1);
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
	send_subscription_subscribe(topic, sub);

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

static int sub_cleanup(void *data)
{
	struct stasis_subscription *sub = data;
	ao2_cleanup(sub);
	return 0;
}

struct stasis_subscription *stasis_unsubscribe(struct stasis_subscription *sub)
{
	/* The subscription may be the last ref to this topic. Hold
	 * the topic ref open until after the unlock. */
	RAII_VAR(struct stasis_topic *, topic,
		ao2_bump(sub ? sub->topic : NULL), ao2_cleanup);

	if (!sub) {
		return NULL;
	}

	/* We have to remove the subscription first, to ensure the unsubscribe
	 * is the final message */
	if (topic_remove_subscription(sub->topic, sub) != 0) {
		ast_log(LOG_ERROR,
			"Internal error: subscription has invalid topic\n");
		return NULL;
	}

	/* Now let everyone know about the unsubscribe */
	send_subscription_unsubscribe(topic, sub);

	/* When all that's done, remove the ref the mailbox has on the sub */
	if (sub->mailbox) {
		ast_taskprocessor_push(sub->mailbox, sub_cleanup, sub);
	}

	/* Unsubscribing unrefs the subscription */
	ao2_cleanup(sub);
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

		for (i = 0; i < AST_VECTOR_SIZE(&topic->subscribers); ++i) {
			if (AST_VECTOR_GET(&topic->subscribers, i) == sub) {
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
	size_t idx;
	SCOPED_AO2LOCK(lock, topic);

	/* The reference from the topic to the subscription is shared with
	 * the owner of the subscription, which will explicitly unsubscribe
	 * to release it.
	 *
	 * If we bumped the refcount here, the owner would have to unsubscribe
	 * and cleanup, which is a bit awkward. */
	AST_VECTOR_APPEND(&topic->subscribers, sub);

	for (idx = 0; idx < AST_VECTOR_SIZE(&topic->upstream_topics); ++idx) {
		topic_add_subscription(
			AST_VECTOR_GET(&topic->upstream_topics, idx), sub);
	}

	return 0;
}

static int topic_remove_subscription(struct stasis_topic *topic, struct stasis_subscription *sub)
{
	size_t idx;
	SCOPED_AO2LOCK(lock_topic, topic);

	for (idx = 0; idx < AST_VECTOR_SIZE(&topic->upstream_topics); ++idx) {
		topic_remove_subscription(
			AST_VECTOR_GET(&topic->upstream_topics, idx), sub);
	}

	return AST_VECTOR_REMOVE_ELEM_UNORDERED(&topic->subscribers, sub,
		AST_VECTOR_ELEM_CLEANUP_NOOP);
}

/*!
 * \internal \brief Dispatch a message to a subscriber asynchronously
 * \param local \ref ast_taskprocessor_local object
 * \return 0
 */
static int dispatch_exec_async(struct ast_taskprocessor_local *local)
{
	struct stasis_subscription *sub = local->local_data;
	struct stasis_message *message = local->data;

	subscription_invoke(sub, message);
	ao2_cleanup(message);

	return 0;
}

/*!
 * \internal \brief Data passed to \ref dispatch_exec_sync to synchronize
 * a published message to a subscriber
 */
struct sync_task_data {
	ast_mutex_t lock;
	ast_cond_t cond;
	int complete;
	void *task_data;
};

/*!
 * \internal \brief Dispatch a message to a subscriber synchronously
 * \param local \ref ast_taskprocessor_local object
 * \return 0
 */
static int dispatch_exec_sync(struct ast_taskprocessor_local *local)
{
	struct stasis_subscription *sub = local->local_data;
	struct sync_task_data *std = local->data;
	struct stasis_message *message = std->task_data;

	subscription_invoke(sub, message);
	ao2_cleanup(message);

	ast_mutex_lock(&std->lock);
	std->complete = 1;
	ast_cond_signal(&std->cond);
	ast_mutex_unlock(&std->lock);

	return 0;
}

/*!
 * \internal \brief Dispatch a message to a subscriber
 * \param sub The subscriber to dispatch to
 * \param message The message to send
 * \param synchronous If non-zero, synchronize on the subscriber receiving
 * the message
 */
static void dispatch_message(struct stasis_subscription *sub,
	struct stasis_message *message,
	int synchronous)
{
	if (!sub->mailbox) {
		/* Dispatch directly */
		subscription_invoke(sub, message);
		return;
	}

	/* Bump the message for the taskprocessor push. This will get de-ref'd
	 * by the task processor callback.
	 */
	ao2_bump(message);
	if (!synchronous) {
		if (ast_taskprocessor_push_local(sub->mailbox,
			                             dispatch_exec_async,
			                             message) != 0) {
			/* Push failed; ugh. */
			ast_log(LOG_ERROR, "Dropping async dispatch\n");
			ao2_cleanup(message);
		}
	} else {
		struct sync_task_data std;

		ast_mutex_init(&std.lock);
		ast_cond_init(&std.cond, NULL);
		std.complete = 0;
		std.task_data = message;

		if (ast_taskprocessor_push_local(sub->mailbox,
			                             dispatch_exec_sync,
			                             &std)) {
			/* Push failed; ugh. */
			ast_log(LOG_ERROR, "Dropping sync dispatch\n");
			ao2_cleanup(message);
			return;
		}

		ast_mutex_lock(&std.lock);
		while (!std.complete) {
			ast_cond_wait(&std.cond, &std.lock);
		}
		ast_mutex_unlock(&std.lock);

		ast_mutex_destroy(&std.lock);
		ast_cond_destroy(&std.cond);
	}
}

/*!
 * \internal \brief Publish a message to a topic's subscribers
 * \brief topic The topic to publish to
 * \brief message The message to publish
 * \brief sync_sub An optional subscriber of the topic to publish synchronously
 * to
 */
static void publish_msg(struct stasis_topic *topic,
	struct stasis_message *message, struct stasis_subscription *sync_sub)
{
	size_t i;

	ast_assert(topic != NULL);
	ast_assert(message != NULL);

	/*
	 * The topic may be unref'ed by the subscription invocation.
	 * Make sure we hold onto a reference while dispatching.
	 */
	ao2_ref(topic, +1);
	ao2_lock(topic);
	for (i = 0; i < AST_VECTOR_SIZE(&topic->subscribers); ++i) {
		struct stasis_subscription *sub = AST_VECTOR_GET(&topic->subscribers, i);

		ast_assert(sub != NULL);

		dispatch_message(sub, message, (sub == sync_sub));
	}
	ao2_unlock(topic);
	ao2_ref(topic, -1);
}

void stasis_publish(struct stasis_topic *topic, struct stasis_message *message)
{
	publish_msg(topic, message, NULL);
}

void stasis_publish_sync(struct stasis_subscription *sub, struct stasis_message *message)
{
	ast_assert(sub != NULL);

	publish_msg(sub->topic, message, sub);
}

/*!
 * \brief Forwarding information
 *
 * Any message posted to \a from_topic is forwarded to \a to_topic.
 *
 * In cases where both the \a from_topic and \a to_topic need to be locked,
 * always lock the \a to_topic first, then the \a from_topic. Lest you deadlock.
 */
struct stasis_forward {
	/*! Originating topic */
	struct stasis_topic *from_topic;
	/*! Destination topic */
	struct stasis_topic *to_topic;
};

static void forward_dtor(void *obj)
{
	struct stasis_forward *forward = obj;

	ao2_cleanup(forward->from_topic);
	forward->from_topic = NULL;
	ao2_cleanup(forward->to_topic);
	forward->to_topic = NULL;
}

struct stasis_forward *stasis_forward_cancel(struct stasis_forward *forward)
{
	int idx;
	struct stasis_topic *from;
	struct stasis_topic *to;

	if (!forward) {
		return NULL;
	}

	from = forward->from_topic;
	to = forward->to_topic;

	topic_lock_both(to, from);
	AST_VECTOR_REMOVE_ELEM_UNORDERED(&to->upstream_topics, from,
		AST_VECTOR_ELEM_CLEANUP_NOOP);

	for (idx = 0; idx < AST_VECTOR_SIZE(&to->subscribers); ++idx) {
		topic_remove_subscription(from, AST_VECTOR_GET(&to->subscribers, idx));
	}
	ao2_unlock(from);
	ao2_unlock(to);

	ao2_cleanup(forward);

	return NULL;
}

struct stasis_forward *stasis_forward_all(struct stasis_topic *from_topic,
	struct stasis_topic *to_topic)
{
	int res;
	size_t idx;
	RAII_VAR(struct stasis_forward *, forward, NULL, ao2_cleanup);

	if (!from_topic || !to_topic) {
		return NULL;
	}

	forward = ao2_alloc(sizeof(*forward), forward_dtor);
	if (!forward) {
		return NULL;
	}

	forward->from_topic = ao2_bump(from_topic);
	forward->to_topic = ao2_bump(to_topic);

	topic_lock_both(to_topic, from_topic);
	res = AST_VECTOR_APPEND(&to_topic->upstream_topics, from_topic);
	if (res != 0) {
		ao2_unlock(from_topic);
		ao2_unlock(to_topic);
		return NULL;
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&to_topic->subscribers); ++idx) {
		topic_add_subscription(from_topic, AST_VECTOR_GET(&to_topic->subscribers, idx));
	}
	ao2_unlock(from_topic);
	ao2_unlock(to_topic);

	return ao2_bump(forward);
}

static void subscription_change_dtor(void *obj)
{
	struct stasis_subscription_change *change = obj;
	ast_string_field_free_memory(change);
	ao2_cleanup(change->topic);
}

static struct stasis_subscription_change *subscription_change_alloc(struct stasis_topic *topic, const char *uniqueid, const char *description)
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

static void send_subscription_subscribe(struct stasis_topic *topic, struct stasis_subscription *sub)
{
	RAII_VAR(struct stasis_subscription_change *, change, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	/* This assumes that we have already unsubscribed */
	ast_assert(stasis_subscription_is_subscribed(sub));

	change = subscription_change_alloc(topic, sub->uniqueid, "Subscribe");

	if (!change) {
		return;
	}

	msg = stasis_message_create(stasis_subscription_change_type(), change);

	if (!msg) {
		return;
	}

	stasis_publish(topic, msg);
}

static void send_subscription_unsubscribe(struct stasis_topic *topic,
	struct stasis_subscription *sub)
{
	RAII_VAR(struct stasis_subscription_change *, change, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	/* This assumes that we have already unsubscribed */
	ast_assert(!stasis_subscription_is_subscribed(sub));

	change = subscription_change_alloc(topic, sub->uniqueid, "Unsubscribe");

	if (!change) {
		return;
	}

	msg = stasis_message_create(stasis_subscription_change_type(), change);

	if (!msg) {
		return;
	}

	stasis_publish(topic, msg);

	/* Now we have to dispatch to the subscription itself */
	dispatch_message(sub, msg, 0);
}

struct topic_pool_entry {
	struct stasis_forward *forward;
	struct stasis_topic *topic;
};

static void topic_pool_entry_dtor(void *obj)
{
	struct topic_pool_entry *entry = obj;
	entry->forward = stasis_forward_cancel(entry->forward);
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

/*! \brief Cleanup function for graceful shutdowns */
static void stasis_cleanup(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_subscription_change_type);
}

int stasis_init(void)
{
	int cache_init;

	/* Be sure the types are cleaned up after the message bus */
	ast_register_cleanup(stasis_cleanup);

	cache_init = stasis_cache_init();
	if (cache_init != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(stasis_subscription_change_type) != 0) {
		return -1;
	}

	return 0;
}
