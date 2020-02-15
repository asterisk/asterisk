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
#include "asterisk/cli.h"

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
				<ref type="managerEvent">UserEvent</ref>
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
static struct ast_threadpool *threadpool;

STASIS_MESSAGE_TYPE_DEFN(stasis_subscription_change_type);

#if defined(LOW_MEMORY)

#define TOPIC_ALL_BUCKETS 257

#else

#define TOPIC_ALL_BUCKETS 997

#endif

#ifdef AST_DEVMODE

/*! The number of buckets to use for topic statistics */
#define TOPIC_STATISTICS_BUCKETS 57

/*! The number of buckets to use for subscription statistics */
#define SUBSCRIPTION_STATISTICS_BUCKETS 57

/*! Global container which stores statistics for topics */
static AO2_GLOBAL_OBJ_STATIC(topic_statistics);

/*! Global container which stores statistics for subscriptions */
static AO2_GLOBAL_OBJ_STATIC(subscription_statistics);

/*! \internal */
struct stasis_message_type_statistics {
	/*! \brief The number of messages of this published */
	int published;
	/*! \brief The number of messages of this that did not reach a subscriber */
	int unused;
	/*! \brief The stasis message type */
	struct stasis_message_type *message_type;
};

/*! Lock to protect the message types vector */
AST_MUTEX_DEFINE_STATIC(message_type_statistics_lock);

/*! Vector containing message type information */
static AST_VECTOR(, struct stasis_message_type_statistics) message_type_statistics;

/*! \internal */
struct stasis_topic_statistics {
	/*! \brief Highest time spent dispatching messages to subscribers */
	long highest_time_dispatched;
	/*! \brief Lowest time spent dispatching messages to subscribers */
	long lowest_time_dispatched;
	/*! \brief The number of messages that were not dispatched to any subscriber */
	int messages_not_dispatched;
	/*! \brief The number of messages that were dispatched to at least 1 subscriber */
	int messages_dispatched;
	/*! \brief The ids of the subscribers to this topic */
	struct ao2_container *subscribers;
	/*! \brief Pointer to the topic (NOT refcounted, and must NOT be accessed) */
	struct stasis_topic *topic;
	/*! \brief Name of the topic */
	char name[0];
};
#endif

/*! \internal */
struct stasis_topic {
	/*! Variable length array of the subscribers */
	AST_VECTOR(, struct stasis_subscription *) subscribers;

	/*! Topics forwarding into this topic */
	AST_VECTOR(, struct stasis_topic *) upstream_topics;

#ifdef AST_DEVMODE
	struct stasis_topic_statistics *statistics;
#endif

	/*! Unique incrementing integer for subscriber ids */
	int subscriber_id;

	/*! Name of the topic */
	char *name;

	/*! Detail of the topic */
	char *detail;

	/*! Creation time */
	struct timeval *creationtime;
};

struct ao2_container *topic_all;

struct topic_proxy {
	AO2_WEAKPROXY();

	char *name;
	char *detail;

	struct timeval creationtime;

	char buf[0];
};

AO2_STRING_FIELD_HASH_FN(topic_proxy, name);
AO2_STRING_FIELD_CMP_FN(topic_proxy, name);
AO2_STRING_FIELD_CASE_SORT_FN(topic_proxy, name);

static void proxy_dtor(void *weakproxy, void *container)
{
	ao2_unlink(container, weakproxy);
	ao2_cleanup(container);
}

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
#ifdef AST_DEVMODE
	struct ao2_container *topic_stats;
#endif

	ast_debug(2, "Destroying topic. name: %s, detail: %s\n",
			topic->name, topic->detail);

	/* Subscribers hold a reference to topics, so they should all be
	 * unsubscribed before we get here. */
	ast_assert(AST_VECTOR_SIZE(&topic->subscribers) == 0);

	AST_VECTOR_FREE(&topic->subscribers);
	AST_VECTOR_FREE(&topic->upstream_topics);
	ast_debug(1, "Topic '%s': %p destroyed\n", topic->name, topic);

#ifdef AST_DEVMODE
	if (topic->statistics) {
		topic_stats = ao2_global_obj_ref(topic_statistics);
		if (topic_stats) {
			ao2_unlink(topic_stats, topic->statistics);
			ao2_ref(topic_stats, -1);
		}
		ao2_ref(topic->statistics, -1);
	}
#endif
}

#ifdef AST_DEVMODE
static void topic_statistics_destroy(void *obj)
{
	struct stasis_topic_statistics *statistics = obj;

	ao2_cleanup(statistics->subscribers);
}

static struct stasis_topic_statistics *stasis_topic_statistics_create(struct stasis_topic *topic)
{
	struct stasis_topic_statistics *statistics;
	RAII_VAR(struct ao2_container *, topic_stats, ao2_global_obj_ref(topic_statistics), ao2_cleanup);

	if (!topic_stats) {
		return NULL;
	}

	statistics = ao2_alloc(sizeof(*statistics) + strlen(topic->name) + 1, topic_statistics_destroy);
	if (!statistics) {
		return NULL;
	}

	statistics->subscribers = ast_str_container_alloc(1);
	if (!statistics->subscribers) {
		ao2_ref(statistics, -1);
		return NULL;
	}

	/* This is strictly used for the pointer address when showing the topic */
	statistics->topic = topic;
	strcpy(statistics->name, topic->name); /* SAFE */
	ao2_link(topic_stats, statistics);

	return statistics;
}
#endif

static int link_topic_proxy(struct stasis_topic *topic, const char *name, const char *detail)
{
	struct topic_proxy *proxy;
	struct stasis_topic* topic_tmp;

	if (!topic || !name || !strlen(name) || !detail) {
		return -1;
	}

	ao2_wrlock(topic_all);

	topic_tmp = stasis_topic_get(name);
	if (topic_tmp) {
		ast_log(LOG_ERROR, "The same topic is already exist. name: %s\n", name);
		ao2_ref(topic_tmp, -1);
		ao2_unlock(topic_all);

		return -1;
	}

	proxy = ao2_t_weakproxy_alloc(
			sizeof(*proxy) + strlen(name) + 1 + strlen(detail) + 1, NULL, name);
	if (!proxy) {
		ao2_unlock(topic_all);

		return -1;
	}

	/* set the proxy info */
	proxy->name = proxy->buf;
	proxy->detail = proxy->name + strlen(name) + 1;

	strcpy(proxy->name, name); /* SAFE */
	strcpy(proxy->detail, detail); /* SAFE */
	proxy->creationtime = ast_tvnow();

	/* We have exclusive access to proxy, no need for locking here. */
	if (ao2_t_weakproxy_set_object(proxy, topic, OBJ_NOLOCK, "weakproxy link")) {
		ao2_cleanup(proxy);
		ao2_unlock(topic_all);

		return -1;
	}

	if (ao2_weakproxy_subscribe(proxy, proxy_dtor, ao2_bump(topic_all), OBJ_NOLOCK)) {
		ao2_cleanup(proxy);
		ao2_unlock(topic_all);
		ao2_cleanup(topic_all);

		return -1;
	}

	/* setting the topic point to the proxy */
	topic->name = proxy->name;
	topic->detail = proxy->detail;
	topic->creationtime = &(proxy->creationtime);

	ao2_link_flags(topic_all, proxy, OBJ_NOLOCK);
	ao2_ref(proxy, -1);

	ao2_unlock(topic_all);

	return 0;
}

struct stasis_topic *stasis_topic_create_with_detail(
		const char *name, const char* detail
		)
{
	struct stasis_topic *topic;
	int res = 0;

	if (!name|| !strlen(name) || !detail) {
		return NULL;
	}
	ast_debug(2, "Creating topic. name: %s, detail: %s\n", name, detail);

	topic = stasis_topic_get(name);
	if (topic) {
		ast_debug(2, "Topic is already exist. name: %s, detail: %s\n",
				name, detail);
		return topic;
	}

	topic = ao2_t_alloc(sizeof(*topic), topic_dtor, name);
	if (!topic) {
		return NULL;
	}

	res |= AST_VECTOR_INIT(&topic->subscribers, INITIAL_SUBSCRIBERS_MAX);
	res |= AST_VECTOR_INIT(&topic->upstream_topics, 0);
	if (res) {
		ao2_ref(topic, -1);
		return NULL;
	}

	/* link to the proxy */
	if (link_topic_proxy(topic, name, detail)) {
		ao2_ref(topic, -1);
		return NULL;
	}

#ifdef AST_DEVMODE
	topic->statistics = stasis_topic_statistics_create(topic);
	if (!topic->statistics) {
		ao2_ref(topic, -1);
		return NULL;
	}
#endif
	ast_debug(1, "Topic '%s': %p created\n", topic->name, topic);

	return topic;
}

struct stasis_topic *stasis_topic_create(const char *name)
{
	return stasis_topic_create_with_detail(name, "");
}

struct stasis_topic *stasis_topic_get(const char *name)
{
	return ao2_weakproxy_find(topic_all, name, OBJ_SEARCH_KEY, "");
}

const char *stasis_topic_name(const struct stasis_topic *topic)
{
	if (!topic) {
		return NULL;
	}
	return topic->name;
}

const char *stasis_topic_detail(const struct stasis_topic *topic)
{
	if (!topic) {
		return NULL;
	}
	return topic->detail;
}

size_t stasis_topic_subscribers(const struct stasis_topic *topic)
{
	return AST_VECTOR_SIZE(&topic->subscribers);
}

#ifdef AST_DEVMODE
struct stasis_subscription_statistics {
	/*! \brief The filename where the subscription originates */
	const char *file;
	/*! \brief The function where the subscription originates */
	const char *func;
	/*! \brief Names of the topics we are subscribed to */
	struct ao2_container *topics;
	/*! \brief The message type that currently took the longest to process */
	struct stasis_message_type *highest_time_message_type;
	/*! \brief Highest time spent invoking a message */
	long highest_time_invoked;
	/*! \brief Lowest time spent invoking a message */
	long lowest_time_invoked;
	/*! \brief The number of messages that were filtered out */
	int messages_dropped;
	/*! \brief The number of messages that passed filtering */
	int messages_passed;
	/*! \brief Using a mailbox to queue messages */
	int uses_mailbox;
	/*! \brief Using stasis threadpool for handling messages */
	int uses_threadpool;
	/*! \brief The line number where the subscription originates */
	int lineno;
	/*! \brief Pointer to the subscription (NOT refcounted, and must NOT be accessed) */
	struct stasis_subscription *sub;
	/*! \brief Unique ID of the subscription */
	char uniqueid[0];
};
#endif

/*! \internal */
struct stasis_subscription {
	/*! Unique ID for this subscription */
	char *uniqueid;
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

	/*! The message types this subscription is accepting */
	AST_VECTOR(, char) accepted_message_types;
	/*! The message formatters this subscription is accepting */
	enum stasis_subscription_message_formatters accepted_formatters;
	/*! The message filter currently in use */
	enum stasis_subscription_message_filter filter;

#ifdef AST_DEVMODE
	/*! Statistics information */
	struct stasis_subscription_statistics *statistics;
#endif
};

static void subscription_dtor(void *obj)
{
	struct stasis_subscription *sub = obj;
#ifdef AST_DEVMODE
	struct ao2_container *subscription_stats;
#endif

	/* Subscriptions need to be manually unsubscribed before destruction
	 * b/c there's a cyclic reference between topics and subscriptions */
	ast_assert(!stasis_subscription_is_subscribed(sub));
	/* If there are any messages in flight to this subscription; that would
	 * be bad. */
	ast_assert(stasis_subscription_is_done(sub));

	ast_free(sub->uniqueid);
	ao2_cleanup(sub->topic);
	sub->topic = NULL;
	ast_taskprocessor_unreference(sub->mailbox);
	sub->mailbox = NULL;
	ast_cond_destroy(&sub->join_cond);

	AST_VECTOR_FREE(&sub->accepted_message_types);

#ifdef AST_DEVMODE
	if (sub->statistics) {
		subscription_stats = ao2_global_obj_ref(subscription_statistics);
		if (subscription_stats) {
			ao2_unlink(subscription_stats, sub->statistics);
			ao2_ref(subscription_stats, -1);
		}
		ao2_ref(sub->statistics, -1);
	}
#endif
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
	unsigned int final = stasis_subscription_final_message(sub, message);
	int message_type_id = stasis_message_type_id(stasis_subscription_change_type());
#ifdef AST_DEVMODE
	struct timeval start;
	long elapsed;

	start = ast_tvnow();
#endif

	/* Notify that the final message has been received */
	if (final) {
		ao2_lock(sub);
		sub->final_message_rxed = 1;
		ast_cond_signal(&sub->join_cond);
		ao2_unlock(sub);
	}

	/*
	 * If filtering is turned on and this is a 'final' message, we only invoke the callback
	 * if the subscriber accepts subscription_change message types.
	 */
	if (!final || sub->filter != STASIS_SUBSCRIPTION_FILTER_SELECTIVE ||
		(message_type_id < AST_VECTOR_SIZE(&sub->accepted_message_types) && AST_VECTOR_GET(&sub->accepted_message_types, message_type_id))) {
		/* Since sub is mostly immutable, no need to lock sub */
		sub->callback(sub->data, sub, message);
	}

	/* Notify that the final message has been processed */
	if (final) {
		ao2_lock(sub);
		sub->final_message_processed = 1;
		ast_cond_signal(&sub->join_cond);
		ao2_unlock(sub);
	}

#ifdef AST_DEVMODE
	elapsed = ast_tvdiff_ms(ast_tvnow(), start);
	if (elapsed > sub->statistics->highest_time_invoked) {
		sub->statistics->highest_time_invoked = elapsed;
		ao2_lock(sub->statistics);
		sub->statistics->highest_time_message_type = stasis_message_type(message);
		ao2_unlock(sub->statistics);
	}
	if (elapsed < sub->statistics->lowest_time_invoked) {
		sub->statistics->lowest_time_invoked = elapsed;
	}
#endif
}

static void send_subscription_subscribe(struct stasis_topic *topic, struct stasis_subscription *sub);
static void send_subscription_unsubscribe(struct stasis_topic *topic, struct stasis_subscription *sub);

void stasis_subscription_cb_noop(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
}

#ifdef AST_DEVMODE
static void subscription_statistics_destroy(void *obj)
{
	struct stasis_subscription_statistics *statistics = obj;

	ao2_cleanup(statistics->topics);
}

static struct stasis_subscription_statistics *stasis_subscription_statistics_create(struct stasis_subscription *sub,
	int needs_mailbox, int use_thread_pool, const char *file, int lineno,
	const char *func)
{
	struct stasis_subscription_statistics *statistics;
	RAII_VAR(struct ao2_container *, subscription_stats, ao2_global_obj_ref(subscription_statistics), ao2_cleanup);

	if (!subscription_stats) {
		return NULL;
	}

	statistics = ao2_alloc(sizeof(*statistics) + strlen(sub->uniqueid) + 1, subscription_statistics_destroy);
	if (!statistics) {
		return NULL;
	}

	statistics->topics = ast_str_container_alloc(1);
	if (!statistics->topics) {
		ao2_ref(statistics, -1);
		return NULL;
	}

	statistics->file = file;
	statistics->lineno = lineno;
	statistics->func = func;
	statistics->uses_mailbox = needs_mailbox;
	statistics->uses_threadpool = use_thread_pool;
	strcpy(statistics->uniqueid, sub->uniqueid); /* SAFE */
	statistics->sub = sub;
	ao2_link(subscription_stats, statistics);

	return statistics;
}
#endif

struct stasis_subscription *internal_stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data,
	int needs_mailbox,
	int use_thread_pool,
	const char *file,
	int lineno,
	const char *func)
{
	struct stasis_subscription *sub;
	int ret;

	if (!topic) {
		return NULL;
	}

	/* The ao2 lock is used for join_cond. */
	sub = ao2_t_alloc(sizeof(*sub), subscription_dtor, stasis_topic_name(topic));
	if (!sub) {
		return NULL;
	}

#ifdef AST_DEVMODE
	ret = ast_asprintf(&sub->uniqueid, "%s:%s-%d", file, stasis_topic_name(topic), ast_atomic_fetchadd_int(&topic->subscriber_id, +1));
	sub->statistics = stasis_subscription_statistics_create(sub, needs_mailbox, use_thread_pool, file, lineno, func);
	if (ret < 0 || !sub->statistics) {
		ao2_ref(sub, -1);
		return NULL;
	}
#else
	ret = ast_asprintf(&sub->uniqueid, "%s-%d", stasis_topic_name(topic), ast_atomic_fetchadd_int(&topic->subscriber_id, +1));
	if (ret < 0) {
		ao2_ref(sub, -1);
		return NULL;
	}
#endif

	if (needs_mailbox) {
		char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

		/* Create name with seq number appended. */
		ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "stasis/%c:%s",
			use_thread_pool ? 'p' : 'm',
			stasis_topic_name(topic));

		/*
		 * With a small number of subscribers, a thread-per-sub is
		 * acceptable. For a large number of subscribers, a thread
		 * pool should be used.
		 */
		if (use_thread_pool) {
			sub->mailbox = ast_threadpool_serializer(tps_name, threadpool);
		} else {
			sub->mailbox = ast_taskprocessor_get(tps_name, TPS_REF_DEFAULT);
		}
		if (!sub->mailbox) {
			ao2_ref(sub, -1);

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
	sub->filter = STASIS_SUBSCRIPTION_FILTER_NONE;
	AST_VECTOR_INIT(&sub->accepted_message_types, 0);
	sub->accepted_formatters = STASIS_SUBSCRIPTION_FORMATTER_NONE;

	if (topic_add_subscription(topic, sub) != 0) {
		ao2_ref(sub, -1);
		ao2_ref(topic, -1);

		return NULL;
	}
	send_subscription_subscribe(topic, sub);

	return sub;
}

struct stasis_subscription *__stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data,
	const char *file,
	int lineno,
	const char *func)
{
	return internal_stasis_subscribe(topic, callback, data, 1, 0, file, lineno, func);
}

struct stasis_subscription *__stasis_subscribe_pool(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data,
	const char *file,
	int lineno,
	const char *func)
{
	return internal_stasis_subscribe(topic, callback, data, 1, 1, file, lineno, func);
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
	struct stasis_topic *topic;

	if (!sub) {
		return NULL;
	}

	topic = ao2_bump(sub->topic);

	/* We have to remove the subscription first, to ensure the unsubscribe
	 * is the final message */
	if (topic_remove_subscription(sub->topic, sub) != 0) {
		ast_log(LOG_ERROR,
			"Internal error: subscription has invalid topic\n");
		ao2_cleanup(topic);

		return NULL;
	}

	/* Now let everyone know about the unsubscribe */
	send_subscription_unsubscribe(topic, sub);

	/* When all that's done, remove the ref the mailbox has on the sub */
	if (sub->mailbox) {
		if (ast_taskprocessor_push(sub->mailbox, sub_cleanup, sub)) {
			/* Nothing we can do here, the conditional is just to keep
			 * the compiler happy that we're not ignoring the result. */
		}
	}

	/* Unsubscribing unrefs the subscription */
	ao2_cleanup(sub);
	ao2_cleanup(topic);

	return NULL;
}

int stasis_subscription_set_congestion_limits(struct stasis_subscription *subscription,
	long low_water, long high_water)
{
	int res = -1;

	if (subscription) {
		res = ast_taskprocessor_alert_set_levels(subscription->mailbox,
			low_water, high_water);
	}
	return res;
}

int stasis_subscription_accept_message_type(struct stasis_subscription *subscription,
	const struct stasis_message_type *type)
{
	if (!subscription) {
		return -1;
	}

	ast_assert(type != NULL);
	ast_assert(stasis_message_type_name(type) != NULL);

	if (!type || !stasis_message_type_name(type)) {
		/* Filtering is unreliable as this message type is not yet initialized
		 * so force all messages through.
		 */
		subscription->filter = STASIS_SUBSCRIPTION_FILTER_FORCED_NONE;
		return 0;
	}

	ao2_lock(subscription->topic);
	if (AST_VECTOR_REPLACE(&subscription->accepted_message_types, stasis_message_type_id(type), 1)) {
		/* We do this for the same reason as above. The subscription can still operate, so allow
		 * it to do so by forcing all messages through.
		 */
		subscription->filter = STASIS_SUBSCRIPTION_FILTER_FORCED_NONE;
	}
	ao2_unlock(subscription->topic);

	return 0;
}

int stasis_subscription_decline_message_type(struct stasis_subscription *subscription,
	const struct stasis_message_type *type)
{
	if (!subscription) {
		return -1;
	}

	ast_assert(type != NULL);
	ast_assert(stasis_message_type_name(type) != NULL);

	if (!type || !stasis_message_type_name(type)) {
		return 0;
	}

	ao2_lock(subscription->topic);
	if (stasis_message_type_id(type) < AST_VECTOR_SIZE(&subscription->accepted_message_types)) {
		/* The memory is already allocated so this can't fail */
		AST_VECTOR_REPLACE(&subscription->accepted_message_types, stasis_message_type_id(type), 0);
	}
	ao2_unlock(subscription->topic);

	return 0;
}

int stasis_subscription_set_filter(struct stasis_subscription *subscription,
	enum stasis_subscription_message_filter filter)
{
	if (!subscription) {
		return -1;
	}

	ao2_lock(subscription->topic);
	if (subscription->filter != STASIS_SUBSCRIPTION_FILTER_FORCED_NONE) {
		subscription->filter = filter;
	}
	ao2_unlock(subscription->topic);

	return 0;
}

void stasis_subscription_accept_formatters(struct stasis_subscription *subscription,
	enum stasis_subscription_message_formatters formatters)
{
	ast_assert(subscription != NULL);

	ao2_lock(subscription->topic);
	subscription->accepted_formatters = formatters;
	ao2_unlock(subscription->topic);

	return;
}

void stasis_subscription_join(struct stasis_subscription *subscription)
{
	if (subscription) {
		ao2_lock(subscription);
		/* Wait until the processed flag has been set */
		while (!subscription->final_message_processed) {
			ast_cond_wait(&subscription->join_cond,
				ao2_object_get_lockaddr(subscription));
		}
		ao2_unlock(subscription);
	}
}

int stasis_subscription_is_done(struct stasis_subscription *subscription)
{
	if (subscription) {
		int ret;

		ao2_lock(subscription);
		ret = subscription->final_message_rxed;
		ao2_unlock(subscription);

		return ret;
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

		ao2_lock(topic);
		for (i = 0; i < AST_VECTOR_SIZE(&topic->subscribers); ++i) {
			if (AST_VECTOR_GET(&topic->subscribers, i) == sub) {
				ao2_unlock(topic);
				return 1;
			}
		}
		ao2_unlock(topic);
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

	ao2_lock(topic);
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

#ifdef AST_DEVMODE
	ast_str_container_add(topic->statistics->subscribers, stasis_subscription_uniqueid(sub));
	ast_str_container_add(sub->statistics->topics, stasis_topic_name(topic));
#endif

	ao2_unlock(topic);

	return 0;
}

static int topic_remove_subscription(struct stasis_topic *topic, struct stasis_subscription *sub)
{
	size_t idx;
	int res;

	ao2_lock(topic);
	for (idx = 0; idx < AST_VECTOR_SIZE(&topic->upstream_topics); ++idx) {
		topic_remove_subscription(
			AST_VECTOR_GET(&topic->upstream_topics, idx), sub);
	}
	res = AST_VECTOR_REMOVE_ELEM_UNORDERED(&topic->subscribers, sub,
		AST_VECTOR_ELEM_CLEANUP_NOOP);

#ifdef AST_DEVMODE
	if (!res) {
		ast_str_container_remove(topic->statistics->subscribers, stasis_subscription_uniqueid(sub));
		ast_str_container_remove(sub->statistics->topics, stasis_topic_name(topic));
	}
#endif

	ao2_unlock(topic);

	return res;
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
 * \retval 0 if message was not dispatched
 * \retval 1 if message was dispatched
 */
static unsigned int dispatch_message(struct stasis_subscription *sub,
	struct stasis_message *message,
	int synchronous)
{
	int is_final = stasis_subscription_final_message(sub, message);

	/*
	 * The 'do while' gives us an easy way to skip remaining logic once
	 * we determine the message should be accepted.
	 * The code looks more verbose than it needs to be but it optimizes
	 * down very nicely.  It's just easier to understand and debug this way.
	 */
	do {
		struct stasis_message_type *message_type = stasis_message_type(message);
		int type_id = stasis_message_type_id(message_type);
		int type_filter_specified = 0;
		int formatter_filter_specified = 0;
		int type_filter_passed = 0;
		int formatter_filter_passed = 0;

		/* We always accept final messages so only run the filter logic if not final */
		if (is_final) {
			break;
		}

		type_filter_specified = sub->filter & STASIS_SUBSCRIPTION_FILTER_SELECTIVE;
		formatter_filter_specified = sub->accepted_formatters != STASIS_SUBSCRIPTION_FORMATTER_NONE;

		/* Accept if no filters of either type were specified */
		if (!type_filter_specified && !formatter_filter_specified) {
			break;
		}

		type_filter_passed = type_filter_specified
			&& type_id < AST_VECTOR_SIZE(&sub->accepted_message_types)
			&& AST_VECTOR_GET(&sub->accepted_message_types, type_id);

		/*
		 * Since the type and formatter filters are OR'd, we can skip
		 * the formatter check if the type check passes.
		 */
		if (type_filter_passed) {
			break;
		}

		formatter_filter_passed = formatter_filter_specified
			&& (sub->accepted_formatters & stasis_message_type_available_formatters(message_type));

		if (formatter_filter_passed) {
			break;
		}

#ifdef AST_DEVMODE
		ast_atomic_fetchadd_int(&sub->statistics->messages_dropped, +1);
#endif

		return 0;

	} while (0);

#ifdef AST_DEVMODE
	ast_atomic_fetchadd_int(&sub->statistics->messages_passed, +1);
#endif

	if (!sub->mailbox) {
		/* Dispatch directly */
		subscription_invoke(sub, message);
		return 1;
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
			return 0;
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
			return 0;
		}

		ast_mutex_lock(&std.lock);
		while (!std.complete) {
			ast_cond_wait(&std.cond, &std.lock);
		}
		ast_mutex_unlock(&std.lock);

		ast_mutex_destroy(&std.lock);
		ast_cond_destroy(&std.cond);
	}

	return 1;
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
	unsigned int dispatched = 0;
#ifdef AST_DEVMODE
	int message_type_id = stasis_message_type_id(stasis_message_type(message));
	struct stasis_message_type_statistics *statistics;
	struct timeval start;
	long elapsed;
#endif

	ast_assert(topic != NULL);
	ast_assert(message != NULL);

#ifdef AST_DEVMODE
	ast_mutex_lock(&message_type_statistics_lock);
	if (message_type_id >= AST_VECTOR_SIZE(&message_type_statistics)) {
		struct stasis_message_type_statistics new_statistics = {
			.published = 0,
		};
		if (AST_VECTOR_REPLACE(&message_type_statistics, message_type_id, new_statistics)) {
			ast_mutex_unlock(&message_type_statistics_lock);
			return;
		}
	}
	statistics = AST_VECTOR_GET_ADDR(&message_type_statistics, message_type_id);
	statistics->message_type = stasis_message_type(message);
	ast_mutex_unlock(&message_type_statistics_lock);

	ast_atomic_fetchadd_int(&statistics->published, +1);
#endif

	/* If there are no subscribers don't bother */
	if (!stasis_topic_subscribers(topic)) {
#ifdef AST_DEVMODE
		ast_atomic_fetchadd_int(&statistics->unused, +1);
		ast_atomic_fetchadd_int(&topic->statistics->messages_not_dispatched, +1);
#endif
		return;
	}

	/*
	 * The topic may be unref'ed by the subscription invocation.
	 * Make sure we hold onto a reference while dispatching.
	 */
	ao2_ref(topic, +1);
#ifdef AST_DEVMODE
	start = ast_tvnow();
#endif
	ao2_lock(topic);
	for (i = 0; i < AST_VECTOR_SIZE(&topic->subscribers); ++i) {
		struct stasis_subscription *sub = AST_VECTOR_GET(&topic->subscribers, i);

		ast_assert(sub != NULL);

		dispatched += dispatch_message(sub, message, (sub == sync_sub));
	}
	ao2_unlock(topic);

#ifdef AST_DEVMODE
	elapsed = ast_tvdiff_ms(ast_tvnow(), start);
	if (elapsed > topic->statistics->highest_time_dispatched) {
		topic->statistics->highest_time_dispatched = elapsed;
	}
	if (elapsed < topic->statistics->lowest_time_dispatched) {
		topic->statistics->lowest_time_dispatched = elapsed;
	}
	if (dispatched) {
		ast_atomic_fetchadd_int(&topic->statistics->messages_dispatched, +1);
	} else {
		ast_atomic_fetchadd_int(&statistics->unused, +1);
		ast_atomic_fetchadd_int(&topic->statistics->messages_not_dispatched, +1);
	}
#endif

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
	struct stasis_forward *forward;

	if (!from_topic || !to_topic) {
		return NULL;
	}

	forward = ao2_alloc_options(sizeof(*forward), forward_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!forward) {
		return NULL;
	}

	/* Forwards to ourselves are implicit. */
	if (to_topic == from_topic) {
		return forward;
	}

	forward->from_topic = ao2_bump(from_topic);
	forward->to_topic = ao2_bump(to_topic);

	topic_lock_both(to_topic, from_topic);
	res = AST_VECTOR_APPEND(&to_topic->upstream_topics, from_topic);
	if (res != 0) {
		ao2_unlock(from_topic);
		ao2_unlock(to_topic);
		ao2_ref(forward, -1);
		return NULL;
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&to_topic->subscribers); ++idx) {
		topic_add_subscription(from_topic, AST_VECTOR_GET(&to_topic->subscribers, idx));
	}
	ao2_unlock(from_topic);
	ao2_unlock(to_topic);

	return forward;
}

static void subscription_change_dtor(void *obj)
{
	struct stasis_subscription_change *change = obj;

	ao2_cleanup(change->topic);
}

static struct stasis_subscription_change *subscription_change_alloc(struct stasis_topic *topic, const char *uniqueid, const char *description)
{
	size_t description_len = strlen(description) + 1;
	struct stasis_subscription_change *change;

	change = ao2_alloc_options(sizeof(*change) + description_len + strlen(uniqueid) + 1,
		subscription_change_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!change) {
		return NULL;
	}

	strcpy(change->description, description); /* SAFE */
	change->uniqueid = change->description + description_len;
	strcpy(change->uniqueid, uniqueid); /* SAFE */
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
	char name[0];
};

static void topic_pool_entry_dtor(void *obj)
{
	struct topic_pool_entry *entry = obj;

	entry->forward = stasis_forward_cancel(entry->forward);
	ao2_cleanup(entry->topic);
	entry->topic = NULL;
}

static struct topic_pool_entry *topic_pool_entry_alloc(const char *topic_name)
{
	struct topic_pool_entry *topic_pool_entry;

	topic_pool_entry = ao2_alloc_options(sizeof(*topic_pool_entry) + strlen(topic_name) + 1,
		topic_pool_entry_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!topic_pool_entry) {
		return NULL;
	}

	strcpy(topic_pool_entry->name, topic_name); /* Safe */

	return topic_pool_entry;
}

struct stasis_topic_pool {
	struct ao2_container *pool_container;
	struct stasis_topic *pool_topic;
};

static void topic_pool_dtor(void *obj)
{
	struct stasis_topic_pool *pool = obj;

#ifdef AO2_DEBUG
	{
		char *container_name =
			ast_alloca(strlen(stasis_topic_name(pool->pool_topic)) + strlen("-pool") + 1);
		sprintf(container_name, "%s-pool", stasis_topic_name(pool->pool_topic));
		ao2_container_unregister(container_name);
	}
#endif

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
		key = object->name;
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
		right_key = object_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->name, right_key);
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

#ifdef AO2_DEBUG
static void topic_pool_prnt_obj(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct topic_pool_entry *entry = v_obj;

	if (!entry) {
		return;
	}
	prnt(where, "%s", stasis_topic_name(entry->topic));
}
#endif

struct stasis_topic_pool *stasis_topic_pool_create(struct stasis_topic *pooled_topic)
{
	struct stasis_topic_pool *pool;

	pool = ao2_alloc_options(sizeof(*pool), topic_pool_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!pool) {
		return NULL;
	}

	pool->pool_container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		TOPIC_POOL_BUCKETS, topic_pool_entry_hash, NULL, topic_pool_entry_cmp);
	if (!pool->pool_container) {
		ao2_cleanup(pool);
		return NULL;
	}

#ifdef AO2_DEBUG
	{
		char *container_name =
			ast_alloca(strlen(stasis_topic_name(pooled_topic)) + strlen("-pool") + 1);
		sprintf(container_name, "%s-pool", stasis_topic_name(pooled_topic));
		ao2_container_register(container_name, pool->pool_container, topic_pool_prnt_obj);
	}
#endif

	ao2_ref(pooled_topic, +1);
	pool->pool_topic = pooled_topic;

	return pool;
}

void stasis_topic_pool_delete_topic(struct stasis_topic_pool *pool, const char *topic_name)
{
	/*
	 * The topic_name passed in could be a fully-qualified name like <pool_topic_name>/<topic_name>
	 * or just <topic_name>.  If it's fully qualified, we need to skip past <pool_topic_name>
	 * name and search only on <topic_name>.
	 */
	const char *pool_topic_name = stasis_topic_name(pool->pool_topic);
	int pool_topic_name_len = strlen(pool_topic_name);
	const char *search_topic_name;

	if (strncmp(pool_topic_name, topic_name, pool_topic_name_len) == 0) {
		search_topic_name = topic_name + pool_topic_name_len + 1;
	} else {
		search_topic_name = topic_name;
	}

	ao2_find(pool->pool_container, search_topic_name, OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);
}

struct stasis_topic *stasis_topic_pool_get_topic(struct stasis_topic_pool *pool, const char *topic_name)
{
	RAII_VAR(struct topic_pool_entry *, topic_pool_entry, NULL, ao2_cleanup);
	SCOPED_AO2LOCK(topic_container_lock, pool->pool_container);
	char *new_topic_name;
	int ret;

	topic_pool_entry = ao2_find(pool->pool_container, topic_name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (topic_pool_entry) {
		return topic_pool_entry->topic;
	}

	topic_pool_entry = topic_pool_entry_alloc(topic_name);
	if (!topic_pool_entry) {
		return NULL;
	}

	/* To provide further detail and to ensure that the topic is unique within the scope of the
	 * system we prefix it with the pooling topic name, which should itself already be unique.
	 */
	ret = ast_asprintf(&new_topic_name, "%s/%s", stasis_topic_name(pool->pool_topic), topic_name);
	if (ret < 0) {
		return NULL;
	}

	topic_pool_entry->topic = stasis_topic_create(new_topic_name);
	ast_free(new_topic_name);
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

int stasis_topic_pool_topic_exists(const struct stasis_topic_pool *pool, const char *topic_name)
{
	struct topic_pool_entry *topic_pool_entry;

	topic_pool_entry = ao2_find(pool->pool_container, topic_name, OBJ_SEARCH_KEY);
	if (!topic_pool_entry) {
		return 0;
	}

	ao2_ref(topic_pool_entry, -1);
	return 1;
}

void stasis_log_bad_type_access(const char *name)
{
#ifdef AST_DEVMODE
	if (!stasis_message_type_declined(name)) {
		ast_log(LOG_ERROR, "Use of %s() before init/after destruction\n", name);
	}
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
	struct ast_multi_object_blob *multi;

	ast_assert(blob != NULL);

	multi = ao2_alloc(sizeof(*multi), multi_object_blob_dtor);
	if (!multi) {
		return NULL;
	}

	for (type = 0; type < STASIS_UMOS_MAX; ++type) {
		if (AST_VECTOR_INIT(&multi->snapshots[type], 0)) {
			ao2_ref(multi, -1);

			return NULL;
		}
	}

	multi->blob = ast_json_ref(blob);

	return multi;
}

/*! \brief Add an object (snapshot) to the blob */
void ast_multi_object_blob_add(struct ast_multi_object_blob *multi,
	enum stasis_user_multi_object_snapshot_type type, void *object)
{
	if (!multi || !object || AST_VECTOR_APPEND(&multi->snapshots[type], object)) {
		ao2_cleanup(object);
	}
}

/*! \brief Publish single channel user event (for app_userevent compatibility) */
void ast_multi_object_blob_single_channel_publish(struct ast_channel *chan,
	struct stasis_message_type *type, struct ast_json *blob)
{
	struct stasis_message *message;
	struct ast_channel_snapshot *channel_snapshot;
	struct ast_multi_object_blob *multi;

	if (!type) {
		return;
	}

	multi = ast_multi_object_blob_create(blob);
	if (!multi) {
		return;
	}

	channel_snapshot = ast_channel_snapshot_create(chan);
	if (!channel_snapshot) {
		ao2_ref(multi, -1);
		return;
	}

	/* this call steals the channel_snapshot reference */
	ast_multi_object_blob_add(multi, STASIS_UMOS_CHANNEL, channel_snapshot);

	message = stasis_message_create(type, multi);
	ao2_ref(multi, -1);
	if (message) {
		/* app_userevent still publishes to channel */
		stasis_publish(ast_channel_topic(chan), message);
		ao2_ref(message, -1);
	}
}

/*! \internal \brief convert multi object blob to ari json */
static struct ast_json *multi_user_event_to_json(
	struct stasis_message *message,
	const struct stasis_message_sanitizer *sanitize)
{
	struct ast_json *out;
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
	ast_json_object_set(out, "userevent", ast_json_ref(blob));

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

	return out;
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
			char *name = NULL;
			void *snapshot = AST_VECTOR_GET(&multi->snapshots[type], i);
			ami_snapshot = NULL;

			if (i > 0) {
				ast_asprintf(&name, "%d", i + 1);
			}

			switch (type) {
			case STASIS_UMOS_CHANNEL:
				ami_snapshot = ast_manager_build_channel_state_string_prefix(snapshot, name ?: "");
				break;

			case STASIS_UMOS_BRIDGE:
				ami_snapshot = ast_manager_build_bridge_state_string_prefix(snapshot, name ?: "");
				break;

			case STASIS_UMOS_ENDPOINT:
				/* currently not sending endpoint snapshots to AMI */
				break;
			}
			if (ami_snapshot) {
				ast_str_append(&ami_str, 0, "%s", ast_str_buffer(ami_snapshot));
				ast_free(ami_snapshot);
			}
			ast_free(name);
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
	.category = "threadpool",
	.category_match = ACO_WHITELIST_EXACT,
};

static struct aco_type *threadpool_options[] = ACO_TYPES(&threadpool_option);

/*! \brief An aco_type structure to link the "declined_message_types" category to the stasis_declined_config type */
static struct aco_type declined_option = {
	.type = ACO_GLOBAL,
	.name = "declined_message_types",
	.item_offset = offsetof(struct stasis_config, declined_message_types),
	.category_match = ACO_WHITELIST_EXACT,
	.category = "declined_message_types",
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
	struct stasis_config *cfg = ao2_global_obj_ref(globals);
	char *name_in_declined;
	int res;

	if (!cfg || !cfg->declined_message_types) {
		ao2_cleanup(cfg);
		return 0;
	}

	name_in_declined = ao2_find(cfg->declined_message_types->declined, name, OBJ_SEARCH_KEY);
	res = name_in_declined ? 1 : 0;
	ao2_cleanup(name_in_declined);
	ao2_ref(cfg, -1);
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

/*!
 * \internal
 * \brief CLI command implementation for 'stasis show topics'
 */
static char *stasis_show_topics(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct topic_proxy *topic;
	struct ao2_container *tmp_container;
	int count = 0;
#define FMT_HEADERS		"%-64s %-64s\n"
#define FMT_FIELDS		"%-64s %-64s\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis show topics";
		e->usage =
			"Usage: stasis show topics\n"
			"	Shows a list of topics\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\n" FMT_HEADERS, "Name", "Detail");

	tmp_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
				topic_proxy_sort_fn, NULL);

	if (!tmp_container || ao2_container_dup(tmp_container, topic_all, OBJ_SEARCH_OBJECT)) {
		ao2_cleanup(tmp_container);

		return NULL;
	}

	/* getting all topic in order */
	iter = ao2_iterator_init(tmp_container, AO2_ITERATOR_UNLINK);
	while ((topic = ao2_iterator_next(&iter))) {
		ast_cli(a->fd, FMT_FIELDS, topic->name, topic->detail);
		ao2_ref(topic, -1);
		++count;
	}
	ao2_iterator_destroy(&iter);
	ao2_cleanup(tmp_container);

	ast_cli(a->fd, "\n%d Total topics\n\n", count);

#undef FMT_HEADERS
#undef FMT_FIELDS

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI tab completion for topic names
 */
static char *topic_complete_name(const char *word)
{
	struct topic_proxy *topic;
	struct ao2_iterator it;
	int wordlen = strlen(word);
	int ret;

	it = ao2_iterator_init(topic_all, 0);
	while ((topic = ao2_iterator_next(&it))) {
		if (!strncasecmp(word, topic->name, wordlen)) {
			ret = ast_cli_completion_add(ast_strdup(topic->name));
			if (ret) {
				ao2_ref(topic, -1);
				break;
			}
		}
		ao2_ref(topic, -1);
	}
	ao2_iterator_destroy(&it);
	return NULL;
}

/*!
 * \internal
 * \brief CLI command implementation for 'stasis show topic'
 */
static char *stasis_show_topic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct stasis_topic *topic;
	char print_time[32];

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis show topic";
		e->usage =
		    "Usage: stasis show topic <name>\n"
		    "       Show stasis topic detail info.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return topic_complete_name(a->word);
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	topic = stasis_topic_get(a->argv[3]);
	if (!topic) {
		ast_cli(a->fd, "Specified topic '%s' does not exist\n", a->argv[3]);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Name: %s\n", topic->name);
	ast_cli(a->fd, "Detail: %s\n", topic->detail);
	ast_cli(a->fd, "Subscribers count: %zu\n", AST_VECTOR_SIZE(&topic->subscribers));
	ast_cli(a->fd, "Forwarding topic count: %zu\n", AST_VECTOR_SIZE(&topic->upstream_topics));
	ast_format_duration_hh_mm_ss(ast_tvnow().tv_sec - topic->creationtime->tv_sec, print_time, sizeof(print_time));
	ast_cli(a->fd, "Duration time: %s\n", print_time);

	ao2_ref(topic, -1);

	return CLI_SUCCESS;
}


static struct ast_cli_entry cli_stasis[] = {
	AST_CLI_DEFINE(stasis_show_topics, "Show all topics"),
	AST_CLI_DEFINE(stasis_show_topic, "Show topic"),
};


#ifdef AST_DEVMODE

AO2_STRING_FIELD_SORT_FN(stasis_subscription_statistics, uniqueid);

/*!
 * \internal
 * \brief CLI command implementation for 'stasis statistics show subscriptions'
 */
static char *statistics_show_subscriptions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *sorted_subscriptions;
	struct ao2_container *subscription_stats;
	struct ao2_iterator iter;
	struct stasis_subscription_statistics *statistics;
	int count = 0;
	int dropped = 0;
	int passed = 0;
#define FMT_HEADERS		"%-64s %10s %10s %16s %16s\n"
#define FMT_FIELDS		"%-64s %10d %10d %16ld %16ld\n"
#define FMT_FIELDS2		"%-64s %10d %10d\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis statistics show subscriptions";
		e->usage =
			"Usage: stasis statistics show subscriptions\n"
			"	Shows a list of subscriptions and their general statistics\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	subscription_stats = ao2_global_obj_ref(subscription_statistics);
	if (!subscription_stats) {
		ast_cli(a->fd, "Could not fetch subscription_statistics container\n");
		return CLI_FAILURE;
	}

	sorted_subscriptions = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		stasis_subscription_statistics_sort_fn, NULL);
	if (!sorted_subscriptions) {
		ao2_ref(subscription_stats, -1);
		ast_cli(a->fd, "Could not create container for sorting subscription statistics\n");
		return CLI_SUCCESS;
	}

	if (ao2_container_dup(sorted_subscriptions, subscription_stats, 0)) {
		ao2_ref(sorted_subscriptions, -1);
		ao2_ref(subscription_stats, -1);
		ast_cli(a->fd, "Could not sort subscription statistics\n");
		return CLI_SUCCESS;
	}

	ao2_ref(subscription_stats, -1);

	ast_cli(a->fd, "\n" FMT_HEADERS, "Subscription", "Dropped", "Passed", "Lowest Invoke", "Highest Invoke");

	iter = ao2_iterator_init(sorted_subscriptions, 0);
	while ((statistics = ao2_iterator_next(&iter))) {
		ast_cli(a->fd, FMT_FIELDS, statistics->uniqueid, statistics->messages_dropped, statistics->messages_passed,
			statistics->lowest_time_invoked, statistics->highest_time_invoked);
		dropped += statistics->messages_dropped;
		passed += statistics->messages_passed;
		ao2_ref(statistics, -1);
		++count;
	}
	ao2_iterator_destroy(&iter);

	ao2_ref(sorted_subscriptions, -1);

	ast_cli(a->fd, FMT_FIELDS2, "Total", dropped, passed);
	ast_cli(a->fd, "\n%d subscriptions\n\n", count);

#undef FMT_HEADERS
#undef FMT_FIELDS
#undef FMT_FIELDS2

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI tab completion for subscription statistics names
 */
static char *subscription_statistics_complete_name(const char *word, int state)
{
	struct stasis_subscription_statistics *statistics;
	struct ao2_container *subscription_stats;
	struct ao2_iterator it_statistics;
	int wordlen = strlen(word);
	int which = 0;
	char *result = NULL;

	subscription_stats = ao2_global_obj_ref(subscription_statistics);
	if (!subscription_stats) {
		return result;
	}

	it_statistics = ao2_iterator_init(subscription_stats, 0);
	while ((statistics = ao2_iterator_next(&it_statistics))) {
		if (!strncasecmp(word, statistics->uniqueid, wordlen)
			&& ++which > state) {
			result = ast_strdup(statistics->uniqueid);
		}
		ao2_ref(statistics, -1);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&it_statistics);
	ao2_ref(subscription_stats, -1);
	return result;
}

/*!
 * \internal
 * \brief CLI command implementation for 'stasis statistics show subscription'
 */
static char *statistics_show_subscription(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct stasis_subscription_statistics *statistics;
	struct ao2_container *subscription_stats;
	struct ao2_iterator i;
	char *name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis statistics show subscription";
		e->usage =
		    "Usage: stasis statistics show subscription <uniqueid>\n"
		    "       Show stasis subscription statistics.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return subscription_statistics_complete_name(a->word, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	subscription_stats = ao2_global_obj_ref(subscription_statistics);
	if (!subscription_stats) {
		ast_cli(a->fd, "Could not fetch subcription_statistics container\n");
		return CLI_FAILURE;
	}

	statistics = ao2_find(subscription_stats, a->argv[4], OBJ_SEARCH_KEY);
	if (!statistics) {
		ao2_ref(subscription_stats, -1);
		ast_cli(a->fd, "Specified subscription '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	ao2_ref(subscription_stats, -1);

	ast_cli(a->fd, "Subscription: %s\n", statistics->uniqueid);
	ast_cli(a->fd, "Pointer Address: %p\n", statistics->sub);
	ast_cli(a->fd, "Source filename: %s\n", S_OR(statistics->file, "<unavailable>"));
	ast_cli(a->fd, "Source line number: %d\n", statistics->lineno);
	ast_cli(a->fd, "Source function: %s\n", S_OR(statistics->func, "<unavailable>"));
	ast_cli(a->fd, "Number of messages dropped due to filtering: %d\n", statistics->messages_dropped);
	ast_cli(a->fd, "Number of messages passed to subscriber callback: %d\n", statistics->messages_passed);
	ast_cli(a->fd, "Using mailbox to queue messages: %s\n", statistics->uses_mailbox ? "Yes" : "No");
	ast_cli(a->fd, "Using stasis threadpool for handling messages: %s\n", statistics->uses_threadpool ? "Yes" : "No");
	ast_cli(a->fd, "Lowest amount of time (in milliseconds) spent invoking message: %ld\n", statistics->lowest_time_invoked);
	ast_cli(a->fd, "Highest amount of time (in milliseconds) spent invoking message: %ld\n", statistics->highest_time_invoked);

	ao2_lock(statistics);
	if (statistics->highest_time_message_type) {
		ast_cli(a->fd, "Offender message type for highest invoking time: %s\n", stasis_message_type_name(statistics->highest_time_message_type));
	}
	ao2_unlock(statistics);

	ast_cli(a->fd, "Number of topics: %d\n", ao2_container_count(statistics->topics));

	ast_cli(a->fd, "Subscribed topics:\n");
	i = ao2_iterator_init(statistics->topics, 0);
	while ((name = ao2_iterator_next(&i))) {
		ast_cli(a->fd, "\t%s\n", name);
		ao2_ref(name, -1);
	}
	ao2_iterator_destroy(&i);

	ao2_ref(statistics, -1);

	return CLI_SUCCESS;
}

AO2_STRING_FIELD_SORT_FN(stasis_topic_statistics, name);

/*!
 * \internal
 * \brief CLI command implementation for 'stasis statistics show topics'
 */
static char *statistics_show_topics(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *sorted_topics;
	struct ao2_container *topic_stats;
	struct ao2_iterator iter;
	struct stasis_topic_statistics *statistics;
	int count = 0;
	int not_dispatched = 0;
	int dispatched = 0;
#define FMT_HEADERS		"%-64s %10s %10s %10s %16s %16s\n"
#define FMT_FIELDS		"%-64s %10d %10d %10d %16ld %16ld\n"
#define FMT_FIELDS2		"%-64s %10s %10d %10d\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis statistics show topics";
		e->usage =
			"Usage: stasis statistics show topics\n"
			"	Shows a list of topics and their general statistics\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	topic_stats = ao2_global_obj_ref(topic_statistics);
	if (!topic_stats) {
		ast_cli(a->fd, "Could not fetch topic_statistics container\n");
		return CLI_FAILURE;
	}

	sorted_topics = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		stasis_topic_statistics_sort_fn, NULL);
	if (!sorted_topics) {
		ao2_ref(topic_stats, -1);
		ast_cli(a->fd, "Could not create container for sorting topic statistics\n");
		return CLI_SUCCESS;
	}

	if (ao2_container_dup(sorted_topics, topic_stats, 0)) {
		ao2_ref(sorted_topics, -1);
		ao2_ref(topic_stats, -1);
		ast_cli(a->fd, "Could not sort topic statistics\n");
		return CLI_SUCCESS;
	}

	ao2_ref(topic_stats, -1);

	ast_cli(a->fd, "\n" FMT_HEADERS, "Topic", "Subscribers", "Dropped", "Dispatched", "Lowest Dispatch", "Highest Dispatch");

	iter = ao2_iterator_init(sorted_topics, 0);
	while ((statistics = ao2_iterator_next(&iter))) {
		ast_cli(a->fd, FMT_FIELDS, statistics->name, ao2_container_count(statistics->subscribers),
			statistics->messages_not_dispatched, statistics->messages_dispatched,
			statistics->lowest_time_dispatched, statistics->highest_time_dispatched);
		not_dispatched += statistics->messages_not_dispatched;
		dispatched += statistics->messages_dispatched;
		ao2_ref(statistics, -1);
		++count;
	}
	ao2_iterator_destroy(&iter);

	ao2_ref(sorted_topics, -1);

	ast_cli(a->fd, FMT_FIELDS2, "Total", "", not_dispatched, dispatched);
	ast_cli(a->fd, "\n%d topics\n\n", count);

#undef FMT_HEADERS
#undef FMT_FIELDS
#undef FMT_FIELDS2

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI tab completion for topic statistics names
 */
static char *topic_statistics_complete_name(const char *word, int state)
{
	struct stasis_topic_statistics *statistics;
	struct ao2_container *topic_stats;
	struct ao2_iterator it_statistics;
	int wordlen = strlen(word);
	int which = 0;
	char *result = NULL;

	topic_stats = ao2_global_obj_ref(topic_statistics);
	if (!topic_stats) {
		return result;
	}

	it_statistics = ao2_iterator_init(topic_stats, 0);
	while ((statistics = ao2_iterator_next(&it_statistics))) {
		if (!strncasecmp(word, statistics->name, wordlen)
			&& ++which > state) {
			result = ast_strdup(statistics->name);
		}
		ao2_ref(statistics, -1);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&it_statistics);
	ao2_ref(topic_stats, -1);
	return result;
}

/*!
 * \internal
 * \brief CLI command implementation for 'stasis statistics show topic'
 */
static char *statistics_show_topic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct stasis_topic_statistics *statistics;
	struct ao2_container *topic_stats;
	struct ao2_iterator i;
	char *uniqueid;

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis statistics show topic";
		e->usage =
		    "Usage: stasis statistics show topic <name>\n"
		    "       Show stasis topic statistics.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return topic_statistics_complete_name(a->word, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	topic_stats = ao2_global_obj_ref(topic_statistics);
	if (!topic_stats) {
		ast_cli(a->fd, "Could not fetch topic_statistics container\n");
		return CLI_FAILURE;
	}

	statistics = ao2_find(topic_stats, a->argv[4], OBJ_SEARCH_KEY);
	if (!statistics) {
		ao2_ref(topic_stats, -1);
		ast_cli(a->fd, "Specified topic '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	ao2_ref(topic_stats, -1);

	ast_cli(a->fd, "Topic: %s\n", statistics->name);
	ast_cli(a->fd, "Pointer Address: %p\n", statistics->topic);
	ast_cli(a->fd, "Number of messages published that went to no subscriber: %d\n", statistics->messages_not_dispatched);
	ast_cli(a->fd, "Number of messages that went to at least one subscriber: %d\n", statistics->messages_dispatched);
	ast_cli(a->fd, "Lowest amount of time (in milliseconds) spent dispatching message: %ld\n", statistics->lowest_time_dispatched);
	ast_cli(a->fd, "Highest amount of time (in milliseconds) spent dispatching messages: %ld\n", statistics->highest_time_dispatched);
	ast_cli(a->fd, "Number of subscribers: %d\n", ao2_container_count(statistics->subscribers));

	ast_cli(a->fd, "Subscribers:\n");
	i = ao2_iterator_init(statistics->subscribers, 0);
	while ((uniqueid = ao2_iterator_next(&i))) {
		ast_cli(a->fd, "\t%s\n", uniqueid);
		ao2_ref(uniqueid, -1);
	}
	ao2_iterator_destroy(&i);

	ao2_ref(statistics, -1);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI command implementation for 'stasis statistics show messages'
 */
static char *statistics_show_messages(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	int count = 0;
	int published = 0;
	int unused = 0;
#define FMT_HEADERS		"%-64s %10s %10s\n"
#define FMT_FIELDS		"%-64s %10d %10d\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "stasis statistics show messages";
		e->usage =
			"Usage: stasis statistics show messages\n"
			"	Shows a list of message types and their general statistics\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\n" FMT_HEADERS, "Message Type", "Published", "Unused");

	ast_mutex_lock(&message_type_statistics_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&message_type_statistics); ++i) {
		struct stasis_message_type_statistics *statistics = AST_VECTOR_GET_ADDR(&message_type_statistics, i);

		if (!statistics->message_type) {
			continue;
		}

		ast_cli(a->fd, FMT_FIELDS, stasis_message_type_name(statistics->message_type), statistics->published,
			statistics->unused);
		published += statistics->published;
		unused += statistics->unused;
		++count;
	}
	ast_mutex_unlock(&message_type_statistics_lock);

	ast_cli(a->fd, FMT_FIELDS, "Total", published, unused);
	ast_cli(a->fd, "\n%d seen message types\n\n", count);

#undef FMT_HEADERS
#undef FMT_FIELDS

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_stasis_statistics[] = {
	AST_CLI_DEFINE(statistics_show_subscriptions, "Show subscriptions with general statistics"),
	AST_CLI_DEFINE(statistics_show_subscription, "Show subscription statistics"),
	AST_CLI_DEFINE(statistics_show_topics, "Show topics with general statistics"),
	AST_CLI_DEFINE(statistics_show_topic, "Show topic statistics"),
	AST_CLI_DEFINE(statistics_show_messages, "Show message types with general statistics"),
};

static int subscription_statistics_hash(const void *obj, const int flags)
{
	const struct stasis_subscription_statistics *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->uniqueid;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

static int subscription_statistics_cmp(void *obj, void *arg, int flags)
{
	const struct stasis_subscription_statistics *object_left = obj;
	const struct stasis_subscription_statistics *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->uniqueid;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->uniqueid, right_key);
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

static int topic_statistics_hash(const void *obj, const int flags)
{
	const struct stasis_topic_statistics *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->name;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

static int topic_statistics_cmp(void *obj, void *arg, int flags)
{
	const struct stasis_topic_statistics *object_left = obj;
	const struct stasis_topic_statistics *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->name, right_key);
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
#endif

/*! \brief Cleanup function for graceful shutdowns */
static void stasis_cleanup(void)
{
#ifdef AST_DEVMODE
	ast_cli_unregister_multiple(cli_stasis_statistics, ARRAY_LEN(cli_stasis_statistics));
	AST_VECTOR_FREE(&message_type_statistics);
	ao2_global_obj_release(subscription_statistics);
	ao2_global_obj_release(topic_statistics);
#endif
	ast_cli_unregister_multiple(cli_stasis, ARRAY_LEN(cli_stasis));
	ao2_cleanup(topic_all);
	topic_all = NULL;
	ast_threadpool_shutdown(threadpool);
	threadpool = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_subscription_change_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_multi_user_event_type);
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);
}

int stasis_init(void)
{
	struct stasis_config *cfg;
	int cache_init;
	struct ast_threadpool_options threadpool_opts = { 0, };
#ifdef AST_DEVMODE
	struct ao2_container *subscription_stats;
	struct ao2_container *topic_stats;
#endif

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
			ao2_ref(default_cfg, -1);

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
	threadpool = ast_threadpool_create("stasis", NULL, &threadpool_opts);
	ao2_ref(cfg, -1);
	if (!threadpool) {
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

	topic_all = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, TOPIC_ALL_BUCKETS,
			topic_proxy_hash_fn, 0, topic_proxy_cmp_fn);
	if (!topic_all) {
		return -1;
	}

	if (ast_cli_register_multiple(cli_stasis, ARRAY_LEN(cli_stasis))) {
		return -1;
	}

#ifdef AST_DEVMODE
	/* Statistics information is stored separately so that we don't alter or interrupt the lifetime of the underlying
	 * topic or subscripton.
	 */
	subscription_stats = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, SUBSCRIPTION_STATISTICS_BUCKETS,
		subscription_statistics_hash, 0, subscription_statistics_cmp);
	if (!subscription_stats) {
		return -1;
	}
	ao2_global_obj_replace_unref(subscription_statistics, subscription_stats);
	ao2_cleanup(subscription_stats);

	topic_stats = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, TOPIC_STATISTICS_BUCKETS,
		topic_statistics_hash, 0, topic_statistics_cmp);
	if (!topic_stats) {
		return -1;
	}
	ao2_global_obj_replace_unref(topic_statistics, topic_stats);
	ao2_cleanup(topic_stats);
	if (!topic_stats) {
		return -1;
	}

	AST_VECTOR_INIT(&message_type_statistics, 0);

	if (ast_cli_register_multiple(cli_stasis_statistics, ARRAY_LEN(cli_stasis_statistics))) {
		return -1;
	}
#endif

	return 0;
}

#ifdef AST_DEVMODE
#undef stasis_subscribe
struct stasis_subscription *stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data);
#undef stasis_subscribe_pool
struct stasis_subscription *stasis_subscribe_pool(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data);
#endif
struct stasis_subscription *stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data)
{
	return internal_stasis_subscribe(topic, callback, data, 1, 0, __FILE__, __LINE__, __PRETTY_FUNCTION__);
}

struct stasis_subscription *stasis_subscribe_pool(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data)
{
	return internal_stasis_subscribe(topic, callback, data, 1, 1, __FILE__, __LINE__, __PRETTY_FUNCTION__);
}
