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

ASTERISK_REGISTER_FILE(__FILE__);

#include "asterisk/astobj2.h"
#include "asterisk/stasis_internal.h"
#include "asterisk/stasis.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadpool.h"
#include "asterisk/utils.h"
#include "asterisk/uuid.h"
#include "asterisk/vector.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/config_options.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="UserEvent">
		<managerEventInstance class="EVENT_FLAG_USER">
			<synopsis>A user defined event raised from the dialplan.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="UserEvent">
					<para>The event name, as specified in the dialplan.</para>
				</parameter>
			</syntax>
			<description>
				<para>Event may contain additional arbitrary parameters in addition to optional bridge and endpoint snapshots.  Multiple snapshots of the same type are prefixed with a numeric value.</para>
			</description>
			<see-also>
				<ref type="application">UserEvent</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<configInfo name="stasis" language="en_US">
		<configFile name="stasis.conf">
			<configObject name="threadpool">
				<synopsis>Settings that configure the threadpool Stasis uses to deliver some messages.</synopsis>
				<configOption name="initial_size" default="5">
					<synopsis>Initial number of threads in the message bus threadpool.</synopsis>
				</configOption>
				<configOption name="idle_timeout_sec" default="20">
					<synopsis>Number of seconds before an idle thread is disposed of.</synopsis>
				</configOption>
				<configOption name="max_size" default="50">
					<synopsis>Maximum number of threads in the threadpool.</synopsis>
				</configOption>
			</configObject>
			<configObject name="declined_message_types">
				<synopsis>Stasis message types for which to decline creation.</synopsis>
				<configOption name="decline">
					<synopsis>The message type to decline.</synopsis>
					<description>
						<para>This configuration option defines the name of the Stasis
						message type that Asterisk is forbidden from creating and can be
						specified as many times as necessary to achieve the desired result.</para>
						<enumlist>
							<enum name="stasis_app_recording_snapshot_type" />
							<enum name="stasis_app_playback_snapshot_type" />
							<enum name="stasis_test_message_type" />
							<enum name="confbridge_start_type" />
							<enum name="confbridge_end_type" />
							<enum name="confbridge_join_type" />
							<enum name="confbridge_leave_type" />
							<enum name="confbridge_start_record_type" />
							<enum name="confbridge_stop_record_type" />
							<enum name="confbridge_mute_type" />
							<enum name="confbridge_unmute_type" />
							<enum name="confbridge_talking_type" />
							<enum name="cel_generic_type" />
							<enum name="ast_bridge_snapshot_type" />
							<enum name="ast_bridge_merge_message_type" />
							<enum name="ast_channel_entered_bridge_type" />
							<enum name="ast_channel_left_bridge_type" />
							<enum name="ast_blind_transfer_type" />
							<enum name="ast_attended_transfer_type" />
							<enum name="ast_endpoint_snapshot_type" />
							<enum name="ast_endpoint_state_type" />
							<enum name="ast_device_state_message_type" />
							<enum name="ast_test_suite_message_type" />
							<enum name="ast_mwi_state_type" />
							<enum name="ast_mwi_vm_app_type" />
							<enum name="ast_format_register_type" />
							<enum name="ast_format_unregister_type" />
							<enum name="ast_manager_get_generic_type" />
							<enum name="ast_parked_call_type" />
							<enum name="ast_channel_snapshot_type" />
							<enum name="ast_channel_dial_type" />
							<enum name="ast_channel_varset_type" />
							<enum name="ast_channel_hangup_request_type" />
							<enum name="ast_channel_dtmf_begin_type" />
							<enum name="ast_channel_dtmf_end_type" />
							<enum name="ast_channel_hold_type" />
							<enum name="ast_channel_unhold_type" />
							<enum name="ast_channel_chanspy_start_type" />
							<enum name="ast_channel_chanspy_stop_type" />
							<enum name="ast_channel_fax_type" />
							<enum name="ast_channel_hangup_handler_type" />
							<enum name="ast_channel_moh_start_type" />
							<enum name="ast_channel_moh_stop_type" />
							<enum name="ast_channel_monitor_start_type" />
							<enum name="ast_channel_monitor_stop_type" />
							<enum name="ast_channel_agent_login_type" />
							<enum name="ast_channel_agent_logoff_type" />
							<enum name="ast_channel_talking_start" />
							<enum name="ast_channel_talking_stop" />
							<enum name="ast_security_event_type" />
							<enum name="ast_named_acl_change_type" />
							<enum name="ast_local_bridge_type" />
							<enum name="ast_local_optimization_begin_type" />
							<enum name="ast_local_optimization_end_type" />
							<enum name="stasis_subscription_change_type" />
							<enum name="ast_multi_user_event_type" />
							<enum name="stasis_cache_clear_type" />
							<enum name="stasis_cache_update_type" />
							<enum name="ast_network_change_type" />
							<enum name="ast_system_registry_type" />
							<enum name="ast_cc_available_type" />
							<enum name="ast_cc_offertimerstart_type" />
							<enum name="ast_cc_requested_type" />
							<enum name="ast_cc_requestacknowledged_type" />
							<enum name="ast_cc_callerstopmonitoring_type" />
							<enum name="ast_cc_callerstartmonitoring_type" />
							<enum name="ast_cc_callerrecalling_type" />
							<enum name="ast_cc_recallcomplete_type" />
							<enum name="ast_cc_failure_type" />
							<enum name="ast_cc_monitorfailed_type" />
							<enum name="ast_presence_state_message_type" />
							<enum name="ast_rtp_rtcp_sent_type" />
							<enum name="ast_rtp_rtcp_received_type" />
							<enum name="ast_call_pickup_type" />
							<enum name="aoc_s_type" />
							<enum name="aoc_d_type" />
							<enum name="aoc_e_type" />
							<enum name="dahdichannel_type" />
							<enum name="mcid_type" />
							<enum name="session_timeout_type" />
							<enum name="cdr_read_message_type" />
							<enum name="cdr_write_message_type" />
							<enum name="cdr_prop_write_message_type" />
							<enum name="corosync_ping_message_type" />
							<enum name="agi_exec_start_type" />
							<enum name="agi_exec_end_type" />
							<enum name="agi_async_start_type" />
							<enum name="agi_async_exec_type" />
							<enum name="agi_async_end_type" />
							<enum name="queue_caller_join_type" />
							<enum name="queue_caller_leave_type" />
							<enum name="queue_caller_abandon_type" />
							<enum name="queue_member_status_type" />
							<enum name="queue_member_added_type" />
							<enum name="queue_member_removed_type" />
							<enum name="queue_member_pause_type" />
							<enum name="queue_member_penalty_type" />
							<enum name="queue_member_ringinuse_type" />
							<enum name="queue_agent_called_type" />
							<enum name="queue_agent_connect_type" />
							<enum name="queue_agent_complete_type" />
							<enum name="queue_agent_dump_type" />
							<enum name="queue_agent_ringnoanswer_type" />
							<enum name="meetme_join_type" />
							<enum name="meetme_leave_type" />
							<enum name="meetme_end_type" />
							<enum name="meetme_mute_type" />
							<enum name="meetme_talking_type" />
							<enum name="meetme_talk_request_type" />
							<enum name="appcdr_message_type" />
							<enum name="forkcdr_message_type" />
							<enum name="cdr_sync_message_type" />
						</enumlist>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

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

/*! Thread pool for topics that don't want a dedicated taskprocessor */
static struct ast_threadpool *pool;

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
	struct stasis_topic *topic;
	int res = 0;

	topic = ao2_t_alloc(sizeof(*topic), topic_dtor, name);
	if (!topic) {
		return NULL;
	}

	topic->name = ast_strdup(name);
	res |= AST_VECTOR_INIT(&topic->subscribers, INITIAL_SUBSCRIBERS_MAX);
	res |= AST_VECTOR_INIT(&topic->upstream_topics, 0);
	if (!topic->name || res) {
		ao2_cleanup(topic);
		return NULL;
	}

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
		SCOPED_AO2LOCK(lock, sub);

		sub->final_message_rxed = 1;
		ast_cond_signal(&sub->join_cond);
	}

	/* Since sub is mostly immutable, no need to lock sub */
	sub->callback(sub->data, sub, message);

	/* Notify that the final message has been processed */
	if (stasis_subscription_final_message(sub, message)) {
		SCOPED_AO2LOCK(lock, sub);

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
	int needs_mailbox,
	int use_thread_pool)
{
	RAII_VAR(struct stasis_subscription *, sub, NULL, ao2_cleanup);

	if (!topic) {
		return NULL;
	}

	/* The ao2 lock is used for join_cond. */
	sub = ao2_t_alloc(sizeof(*sub), subscription_dtor, topic->name);
	if (!sub) {
		return NULL;
	}
	ast_uuid_generate_str(sub->uniqueid, sizeof(sub->uniqueid));

	if (needs_mailbox) {
		/* With a small number of subscribers, a thread-per-sub is
		 * acceptable. For larger number of subscribers, a thread
		 * pool should be used.
		 */
		if (use_thread_pool) {
			sub->mailbox = ast_threadpool_serializer(sub->uniqueid, pool);
		} else {
			sub->mailbox = ast_taskprocessor_get(sub->uniqueid,
				TPS_REF_DEFAULT);
		}
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
	return internal_stasis_subscribe(topic, callback, data, 1, 0);
}

struct stasis_subscription *stasis_subscribe_pool(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data)
{
	return internal_stasis_subscribe(topic, callback, data, 1, 1);
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
		SCOPED_AO2LOCK(lock, subscription);

		/* Wait until the processed flag has been set */
		while (!subscription->final_message_processed) {
			ast_cond_wait(&subscription->join_cond,
				ao2_object_get_lockaddr(subscription));
		}
	}
}

int stasis_subscription_is_done(struct stasis_subscription *subscription)
{
	if (subscription) {
		SCOPED_AO2LOCK(lock, subscription);

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
		if (ast_taskprocessor_push_local(sub->mailbox, dispatch_exec_async, message)) {
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

		if (ast_taskprocessor_push_local(sub->mailbox, dispatch_exec_sync, &std)) {
			/* Push failed; ugh. */
			ast_log(LOG_ERROR, "Dropping sync dispatch\n");
			ao2_cleanup(message);
			ast_mutex_destroy(&std.lock);
			ast_cond_destroy(&std.cond);
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

	if (from && to) {
		topic_lock_both(to, from);
		AST_VECTOR_REMOVE_ELEM_UNORDERED(&to->upstream_topics, from,
			AST_VECTOR_ELEM_CLEANUP_NOOP);

		for (idx = 0; idx < AST_VECTOR_SIZE(&to->subscribers); ++idx) {
			topic_remove_subscription(from, AST_VECTOR_GET(&to->subscribers, idx));
		}
		ao2_unlock(from);
		ao2_unlock(to);
	}

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

	forward = ao2_alloc_options(sizeof(*forward), forward_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!forward) {
		return NULL;
	}

	/* Forwards to ourselves are implicit. */
	if (to_topic == from_topic) {
		return ao2_bump(forward);
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
	struct stasis_subscription_change *change;

	change = ao2_alloc(sizeof(struct stasis_subscription_change), subscription_change_dtor);
	if (!change || ast_string_field_init(change, 128)) {
		ao2_cleanup(change);
		return NULL;
	}

	ast_string_field_set(change, uniqueid, uniqueid);
	ast_string_field_set(change, description, description);
	ao2_ref(topic, +1);
	change->topic = topic;

	return change;
}

static void send_subscription_subscribe(struct stasis_topic *topic, struct stasis_subscription *sub)
{
	struct stasis_subscription_change *change;
	struct stasis_message *msg;

	/* This assumes that we have already unsubscribed */
	ast_assert(stasis_subscription_is_subscribed(sub));

	if (!stasis_subscription_change_type()) {
		return;
	}

	change = subscription_change_alloc(topic, sub->uniqueid, "Subscribe");
	if (!change) {
		return;
	}

	msg = stasis_message_create(stasis_subscription_change_type(), change);
	if (!msg) {
		ao2_cleanup(change);
		return;
	}

	stasis_publish(topic, msg);
	ao2_cleanup(msg);
	ao2_cleanup(change);
}

static void send_subscription_unsubscribe(struct stasis_topic *topic,
	struct stasis_subscription *sub)
{
	struct stasis_subscription_change *change;
	struct stasis_message *msg;

	/* This assumes that we have already unsubscribed */
	ast_assert(!stasis_subscription_is_subscribed(sub));

	if (!stasis_subscription_change_type()) {
		return;
	}

	change = subscription_change_alloc(topic, sub->uniqueid, "Unsubscribe");
	if (!change) {
		return;
	}

	msg = stasis_message_create(stasis_subscription_change_type(), change);
	if (!msg) {
		ao2_cleanup(change);
		return;
	}

	stasis_publish(topic, msg);

	/* Now we have to dispatch to the subscription itself */
	dispatch_message(sub, msg, 0);

	ao2_cleanup(msg);
	ao2_cleanup(change);
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
	return ao2_alloc_options(sizeof(struct topic_pool_entry), topic_pool_entry_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
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
	const struct topic_pool_entry *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = stasis_topic_name(object->topic);
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

static int topic_pool_entry_cmp(void *obj, void *arg, int flags)
{
	const struct topic_pool_entry *object_left = obj;
	const struct topic_pool_entry *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = stasis_topic_name(object_right->topic);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(stasis_topic_name(object_left->topic), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container */
		ast_assert(0);
		cmp = -1;
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

struct stasis_topic_pool *stasis_topic_pool_create(struct stasis_topic *pooled_topic)
{
	struct stasis_topic_pool *pool;

	pool = ao2_alloc_options(sizeof(*pool), topic_pool_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!pool) {
		return NULL;
	}

	pool->pool_container = ao2_container_alloc(TOPIC_POOL_BUCKETS,
		topic_pool_entry_hash, topic_pool_entry_cmp);
	if (!pool->pool_container) {
		ao2_cleanup(pool);
		return NULL;
	}
	ao2_ref(pooled_topic, +1);
	pool->pool_topic = pooled_topic;

	return pool;
}

struct stasis_topic *stasis_topic_pool_get_topic(struct stasis_topic_pool *pool, const char *topic_name)
{
	RAII_VAR(struct topic_pool_entry *, topic_pool_entry, NULL, ao2_cleanup);
	SCOPED_AO2LOCK(topic_container_lock, pool->pool_container);

	topic_pool_entry = ao2_find(pool->pool_container, topic_name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
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

	if (!ao2_link_flags(pool->pool_container, topic_pool_entry, OBJ_NOLOCK)) {
		return NULL;
	}

	return topic_pool_entry->topic;
}

void stasis_log_bad_type_access(const char *name)
{
#ifdef AST_DEVMODE
	ast_log(LOG_ERROR, "Use of %s() before init/after destruction\n", name);
#endif
}

/*! \brief A multi object blob data structure to carry user event stasis messages */
struct ast_multi_object_blob {
	struct ast_json *blob;                             /*< A blob of JSON data */
	AST_VECTOR(, void *) snapshots[STASIS_UMOS_MAX];   /*< Vector of snapshots for each type */
};

/*!
 * \internal
 * \brief Destructor for \ref ast_multi_object_blob objects
 */
static void multi_object_blob_dtor(void *obj)
{
	struct ast_multi_object_blob *multi = obj;
	int type;
	int i;

	for (type = 0; type < STASIS_UMOS_MAX; ++type) {
		for (i = 0; i < AST_VECTOR_SIZE(&multi->snapshots[type]); ++i) {
			ao2_cleanup(AST_VECTOR_GET(&multi->snapshots[type], i));
		}
		AST_VECTOR_FREE(&multi->snapshots[type]);
	}
	ast_json_unref(multi->blob);
}

/*! \brief Create a stasis user event multi object blob */
struct ast_multi_object_blob *ast_multi_object_blob_create(struct ast_json *blob)
{
	int type;
	RAII_VAR(struct ast_multi_object_blob *, multi,
			ao2_alloc(sizeof(*multi), multi_object_blob_dtor),
			ao2_cleanup);

	ast_assert(blob != NULL);

	if (!multi) {
		return NULL;
	}

	for (type = 0; type < STASIS_UMOS_MAX; ++type) {
		if (AST_VECTOR_INIT(&multi->snapshots[type], 0)) {
			return NULL;
		}
	}

	multi->blob = ast_json_ref(blob);

	ao2_ref(multi, +1);
	return multi;
}

/*! \brief Add an object (snapshot) to the blob */
void ast_multi_object_blob_add(struct ast_multi_object_blob *multi,
	enum stasis_user_multi_object_snapshot_type type, void *object)
{
	if (!multi || !object) {
		return;
	}
	AST_VECTOR_APPEND(&multi->snapshots[type],object);
}

/*! \brief Publish single channel user event (for app_userevent compatibility) */
void ast_multi_object_blob_single_channel_publish(struct ast_channel *chan,
	struct stasis_message_type *type, struct ast_json *blob)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, channel_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_multi_object_blob *, multi, NULL, ao2_cleanup);

	if (!type) {
		return;
	}

	multi = ast_multi_object_blob_create(blob);
	if (!multi) {
		return;
	}

	channel_snapshot = ast_channel_snapshot_create(chan);
	ao2_ref(channel_snapshot, +1);
	ast_multi_object_blob_add(multi, STASIS_UMOS_CHANNEL, channel_snapshot);

	message = stasis_message_create(type, multi);
	if (message) {
		/* app_userevent still publishes to channel */
		stasis_publish(ast_channel_topic(chan), message);
	}
}

/*! \internal \brief convert multi object blob to ari json */
static struct ast_json *multi_user_event_to_json(
	struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	RAII_VAR(struct ast_json *, out, NULL, ast_json_unref);
	struct ast_multi_object_blob *multi = stasis_message_data(message);
	struct ast_json *blob = multi->blob;
	const struct timeval *tv = stasis_message_timestamp(message);
	enum stasis_user_multi_object_snapshot_type type;
	int i;

	out = ast_json_object_create();
	if (!out) {
		return NULL;
	}

	ast_json_object_set(out, "type", ast_json_string_create("ChannelUserevent"));
	ast_json_object_set(out, "timestamp", ast_json_timeval(*tv, NULL));
	ast_json_object_set(out, "eventname", ast_json_ref(ast_json_object_get(blob, "eventname")));
	ast_json_object_set(out, "userevent", ast_json_ref(blob)); /* eventname gets duplicated, that's ok */

	for (type = 0; type < STASIS_UMOS_MAX; ++type) {
		for (i = 0; i < AST_VECTOR_SIZE(&multi->snapshots[type]); ++i) {
			struct ast_json *json_object = NULL;
			char *name = NULL;
			void *snapshot = AST_VECTOR_GET(&multi->snapshots[type], i);

			switch (type) {
			case STASIS_UMOS_CHANNEL:
				json_object = ast_channel_snapshot_to_json(snapshot, sanitize);
				name = "channel";
				break;
			case STASIS_UMOS_BRIDGE:
				json_object = ast_bridge_snapshot_to_json(snapshot, sanitize);
				name = "bridge";
				break;
			case STASIS_UMOS_ENDPOINT:
				json_object = ast_endpoint_snapshot_to_json(snapshot, sanitize);
				name = "endpoint";
				break;
			}
			if (json_object) {
				ast_json_object_set(out, name, json_object);
			}
		}
	}
	return ast_json_ref(out);
}

/*! \internal \brief convert multi object blob to ami string */
static struct ast_str *multi_object_blob_to_ami(void *obj)
{
	struct ast_str *ami_str=ast_str_create(1024);
	struct ast_str *ami_snapshot;
	const struct ast_multi_object_blob *multi = obj;
	enum stasis_user_multi_object_snapshot_type type;
	int i;

	if (!ami_str) {
		return NULL;
	}
	if (!multi) {
		ast_free(ami_str);
		return NULL;
	}

	for (type = 0; type < STASIS_UMOS_MAX; ++type) {
		for (i = 0; i < AST_VECTOR_SIZE(&multi->snapshots[type]); ++i) {
			char *name = "";
			void *snapshot = AST_VECTOR_GET(&multi->snapshots[type], i);
			ami_snapshot = NULL;

			if (i > 0) {
				ast_asprintf(&name, "%d", i + 1);
			}

			switch (type) {
			case STASIS_UMOS_CHANNEL:
				ami_snapshot = ast_manager_build_channel_state_string_prefix(snapshot, name);
				break;

			case STASIS_UMOS_BRIDGE:
				ami_snapshot = ast_manager_build_bridge_state_string_prefix(snapshot, name);
				break;

			case STASIS_UMOS_ENDPOINT:
				/* currently not sending endpoint snapshots to AMI */
				break;
			}
			if (ami_snapshot) {
				ast_str_append(&ami_str, 0, "%s", ast_str_buffer(ami_snapshot));
				ast_free(ami_snapshot);
			}
		}
	}

	return ami_str;
}

/*! \internal \brief Callback to pass only user defined parameters from blob */
static int userevent_exclusion_cb(const char *key)
{
	if (!strcmp("eventname", key)) {
		return 1;
	}
	return 0;
}

static struct ast_manager_event_blob *multi_user_event_to_ami(
	struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, object_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, body, NULL, ast_free);
	struct ast_multi_object_blob *multi = stasis_message_data(message);
	const char *eventname;

	eventname = ast_json_string_get(ast_json_object_get(multi->blob, "eventname"));
	body = ast_manager_str_from_json_object(multi->blob, userevent_exclusion_cb);
	object_string = multi_object_blob_to_ami(multi);
	if (!object_string || !body) {
		return NULL;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_USER, "UserEvent",
		"%s"
		"UserEvent: %s\r\n"
		"%s",
		ast_str_buffer(object_string),
		eventname,
		ast_str_buffer(body));
}

/*! \brief A structure to hold global configuration-related options */
struct stasis_declined_config {
	/*! The list of message types to decline */
	struct ao2_container *declined;
};

/*! \brief Threadpool configuration options */
struct stasis_threadpool_conf {
	/*! Initial size of the thread pool */
	int initial_size;
	/*! Time, in seconds, before we expire a thread */
	int idle_timeout_sec;
	/*! Maximum number of thread to allow */
	int max_size;
};

struct stasis_config {
	/*! Thread pool configuration options */
	struct stasis_threadpool_conf *threadpool_options;
	/*! Declined message types */
	struct stasis_declined_config *declined_message_types;
};

static struct aco_type threadpool_option = {
	.type = ACO_GLOBAL,
	.name = "threadpool",
	.item_offset = offsetof(struct stasis_config, threadpool_options),
	.category = "^threadpool$",
	.category_match = ACO_WHITELIST,
};

static struct aco_type *threadpool_options[] = ACO_TYPES(&threadpool_option);

/*! \brief An aco_type structure to link the "declined_message_types" category to the stasis_declined_config type */
static struct aco_type declined_option = {
	.type = ACO_GLOBAL,
	.name = "declined_message_types",
	.item_offset = offsetof(struct stasis_config, declined_message_types),
	.category_match = ACO_WHITELIST,
	.category = "^declined_message_types$",
};

struct aco_type *declined_options[] = ACO_TYPES(&declined_option);

struct aco_file stasis_conf = {
        .filename = "stasis.conf",
	.types = ACO_TYPES(&declined_option, &threadpool_option),
};

/*! \brief A global object container that will contain the stasis_config that gets swapped out on reloads */
static AO2_GLOBAL_OBJ_STATIC(globals);

static void *stasis_config_alloc(void);

/*! \brief Register information about the configs being processed by this module */
CONFIG_INFO_CORE("stasis", cfg_info, globals, stasis_config_alloc,
        .files = ACO_FILES(&stasis_conf),
);

static void stasis_declined_config_destructor(void *obj)
{
	struct stasis_declined_config *declined = obj;

	ao2_cleanup(declined->declined);
}

static void stasis_config_destructor(void *obj)
{
	struct stasis_config *cfg = obj;

	ao2_cleanup(cfg->declined_message_types);
	ast_free(cfg->threadpool_options);
}

static void *stasis_config_alloc(void)
{
	struct stasis_config *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), stasis_config_destructor))) {
		return NULL;
	}

	cfg->threadpool_options = ast_calloc(1, sizeof(*cfg->threadpool_options));
	if (!cfg->threadpool_options) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	cfg->declined_message_types = ao2_alloc(sizeof(*cfg->declined_message_types),
		stasis_declined_config_destructor);
	if (!cfg->declined_message_types) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	cfg->declined_message_types->declined = ast_str_container_alloc(13);
	if (!cfg->declined_message_types->declined) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

int stasis_message_type_declined(const char *name)
{
	RAII_VAR(struct stasis_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	char *name_in_declined;
	int res;

	if (!cfg || !cfg->declined_message_types) {
		return 0;
	}

	name_in_declined = ao2_find(cfg->declined_message_types->declined, name, OBJ_SEARCH_KEY);
	res = name_in_declined ? 1 : 0;
	ao2_cleanup(name_in_declined);
	if (res) {
		ast_log(LOG_NOTICE, "Declining to allocate Stasis message type '%s' due to configuration\n", name);
	}
	return res;
}

static int declined_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct stasis_declined_config *declined = obj;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	if (ast_str_container_add(declined->declined, var->value)) {
		return -1;
	}

	return 0;
}

/*!
 * @{ \brief Define multi user event message type(s).
 */

STASIS_MESSAGE_TYPE_DEFN(ast_multi_user_event_type,
	.to_json = multi_user_event_to_json,
	.to_ami = multi_user_event_to_ami,
	);

/*! @} */

/*! \brief Cleanup function for graceful shutdowns */
static void stasis_cleanup(void)
{
	ast_threadpool_shutdown(pool);
	pool = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_subscription_change_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_multi_user_event_type);
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);
}

int stasis_init(void)
{
	RAII_VAR(struct stasis_config *, cfg, NULL, ao2_cleanup);
	int cache_init;
	struct ast_threadpool_options threadpool_opts = { 0, };

	/* Be sure the types are cleaned up after the message bus */
	ast_register_cleanup(stasis_cleanup);

	if (aco_info_init(&cfg_info)) {
		return -1;
	}

	aco_option_register_custom(&cfg_info, "decline", ACO_EXACT,
		declined_options, "", declined_handler, 0);
	aco_option_register(&cfg_info, "initial_size", ACO_EXACT,
		threadpool_options, "5", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct stasis_threadpool_conf, initial_size), 0,
		INT_MAX);
	aco_option_register(&cfg_info, "idle_timeout_sec", ACO_EXACT,
		threadpool_options, "20", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct stasis_threadpool_conf, idle_timeout_sec), 0,
		INT_MAX);
	aco_option_register(&cfg_info, "max_size", ACO_EXACT,
		threadpool_options, "50", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct stasis_threadpool_conf, max_size), 0,
		INT_MAX);

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		struct stasis_config *default_cfg = stasis_config_alloc();

		if (!default_cfg) {
			return -1;
		}

		if (aco_set_defaults(&threadpool_option, "threadpool", default_cfg->threadpool_options)) {
			ast_log(LOG_ERROR, "Failed to initialize defaults on Stasis configuration object\n");
			ao2_ref(default_cfg, -1);
			return -1;
		}

		if (aco_set_defaults(&declined_option, "declined_message_types", default_cfg->declined_message_types)) {
			ast_log(LOG_ERROR, "Failed to load stasis.conf and failed to initialize defaults.\n");
			return -1;
		}

		ast_log(LOG_NOTICE, "Could not load Stasis configuration; using defaults\n");
		ao2_global_obj_replace_unref(globals, default_cfg);
		cfg = default_cfg;
	} else {
		cfg = ao2_global_obj_ref(globals);
		if (!cfg) {
			ast_log(LOG_ERROR, "Failed to obtain Stasis configuration object\n");
			return -1;
		}
	}

	threadpool_opts.version = AST_THREADPOOL_OPTIONS_VERSION;
	threadpool_opts.initial_size = cfg->threadpool_options->initial_size;
	threadpool_opts.auto_increment = 1;
	threadpool_opts.max_size = cfg->threadpool_options->max_size;
	threadpool_opts.idle_timeout = cfg->threadpool_options->idle_timeout_sec;
	pool = ast_threadpool_create("stasis-core", NULL, &threadpool_opts);
	if (!pool) {
		ast_log(LOG_ERROR, "Failed to create 'stasis-core' threadpool\n");
		return -1;
	}

	cache_init = stasis_cache_init();
	if (cache_init != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(stasis_subscription_change_type) != 0) {
		return -1;
	}
	if (STASIS_MESSAGE_TYPE_INIT(ast_multi_user_event_type) != 0) {
		return -1;
	}

	return 0;
}

