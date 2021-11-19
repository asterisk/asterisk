/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

#ifndef _ASTERISK_MWI_H
#define _ASTERISK_MWI_H

/*! \file
 *
 * \brief Asterisk MWI API.
 *
 * \par Intro
 *
 * This module manages, and processes all things MWI. Defined are mechanisms for subscribing
 * and publishing to MWI topics. User modules wishing to receive MWI updates for a particular
 * mailbox should do so by adding an MWI subscriber to that mailbox, followed by subscribing
 * to the mailbox's topic. Likewise, user modules that want to publish MWI updates about a
 * particular mailbox should first add a publisher for that mailbox prior to publishing.
 *
 * MWI state is managed via an underlying \ref stasis_state_manager (if interested see the
 * stasis_state.c module for the gory details). As such all last known mailbox state can be
 * retrieve and iterated over by using the \ref ast_mwi_subscribe_pool function.
 *
 * \par ast_mwi_subscriber
 *
 * Created via \ref ast_mwi_add_subscriber, a subscriber subscribes to a given mailbox in
 * order to receive updates about the given mailbox. Adding a subscriber will create the
 * underlying topic, and associated state data if those do not already exist for it. The
 * topic, and last known state data is guaranteed to exist for the lifetime of the subscriber.
 * State data can be NULL if nothing has been published to the mailbox's topic yet.
 *
 * NOTE, currently adding a subscriber here will either create, or add a reference to the
 * underlying stasis state (and associated topic). However, it does not actually subscribe to
 * the stasis topic itself. You still need to explicitly call \ref stasis_subscribe, or
 * similar on the topic if you wish to receive published event updates.
 *
 * So given that when subscribing to an MWI topic the following order should be adhered to:
 *
 *   1. Add an MWI state subscriber using \ref ast_mwi_add_subscriber
 *   2. Retrieve the topic from the subscriber using \ref ast_mwi_subscriber_topic
 *   3. Subscribe to the topic itself using \ref stasis_subscribe or \ref stasis_subscribe_pool
 *
 * Or simply call \ref ast_mwi_subscribe_pool, which combines those steps into a single call and
 * returns the subscriber that is now subscribed to both the stasis topic and state.
 *
 * Similarly, releasing the subscriber's reference removes a reference to the underlying state,
 * but does not unsubscribe from the MWI topic. This should be done separately and prior to
 * removing the subscriber's state reference:
 *
 *   1. Unsubscribe from the stasis topic subscription using \ref stasis_unsubscribe or
 *      \ref stasis_unsubscribe_and_join
 *   2. Remove the MWI subscriber reference
 *
 * Or call \ref ast_mwi_unsubscribe (or _and_join), which combines those two steps into a
 * single call.
 *
 * \par ast_mwi_publisher
 *
 * Before publishing to a particular topic a publisher should be created. This can be achieved
 * by using \ref ast_mwi_add_publisher. Publishing to a mailbox should then be done using the
 * \ref ast_mwi_publish function. This ensures the message is published to the appropriate
 * topic, and the last known state is maintained.
 *
 * Publishing by mailbox id alone is also allowed. However, it is not recommended to do so,
 * and exists mainly for backwards compatibility, and legacy subsystems. If, and when this
 * method of publishing is employed a call to one of the \ref ast_delete_mwi_state functions
 * should also be called for a given mailbox id after no more publishing will be done for
 * that id. Otherwise a memory leak on the underlying stasis_state object will occur.
 *
 * \par ast_mwi_observer
 *
 * Add an observer in order to watch for particular MWI module related events. For instance if
 * a submodule needs to know when a subscription is added to any mailbox an observer can be
 * added to watch for that.
 */

#include "asterisk/utils.h"
#include "asterisk/stasis_state.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_json;
struct stasis_message_type;
struct ast_mwi_state;

/*!
 * \brief An MWI state subscriber
 *
 * An ao2 managed object. Holds a reference to the latest MWI state for its lifetime.
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct ast_mwi_subscriber;

/*!
 * \brief Add an MWI state subscriber to the mailbox
 *
 * Adding a subscriber to a mailbox will create a stasis topic for the mailbox if one
 * does not already exist. It does not however subscribe to the topic itself. This is
 * done separately using a call to \ref stasis_subscribe or \ref stasis_subscribe_pool.
 *
 * A subscriber can be removed by releasing its reference. Doing so releases its underlying
 * reference to the MWI state. It does not unsubscribe from the topic. Unsubscribing from
 * a topic should be done prior to unsubscribing the state.
 *
 * \param mailbox The subscription state mailbox id
 *
 * \return An MWI subscriber object
 * \retval NULL on error
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct ast_mwi_subscriber *ast_mwi_add_subscriber(const char *mailbox);

/*!
 * \brief Add an MWI state subscriber, and stasis subscription to the mailbox
 *
 * Adding a subscriber to a mailbox will create a stasis topic for the mailbox if one
 * does not already exist. Once successfully create the underlying stasis topic is then
 * subscribed to as well.
 *
 * A subscriber can be removed by releasing its reference. Doing so releases its underlying
 * reference to the MWI state. It does not unsubscribe from the topic. Unsubscribing from
 * a topic should be done prior to unsubscribing the state.
 *
 * \param mailbox The subscription state mailbox id
 * \param callback The stasis subscription callback
 * \param data A user data object passed to the stasis subscription
 *
 * \return An MWI subscriber object
 * \retval NULL on error
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct ast_mwi_subscriber *ast_mwi_subscribe_pool(const char *mailbox,
	stasis_subscription_cb callback, void *data);

/*!
 * \brief Unsubscribe from the stasis topic and MWI.
 *
 * \param sub An MWI subscriber
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void *ast_mwi_unsubscribe(struct ast_mwi_subscriber *sub);

/*!
 * \brief Unsubscribe from the stasis topic, block until the final message
 * is received, and then unsubscribe from MWI.
 *
 * \param sub An MWI subscriber
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void *ast_mwi_unsubscribe_and_join(struct ast_mwi_subscriber *sub);

/*!
 * \brief Retrieves the MWI subscriber's topic
 *
 * \note Returned topic's reference count is NOT incremented. However, the topic is
 * guaranteed to live for the lifetime of the subscriber.
 *
 * \param sub An MWI subscriber
 *
 * \return A stasis topic subscribed to by the subscriber
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_topic *ast_mwi_subscriber_topic(struct ast_mwi_subscriber *sub);

/*!
 * \brief Retrieves the state data object associated with the MWI subscriber
 *
 * \note Returned data's reference count is incremented
 *
 * \param sub An MWI subscriber
 *
 * \return The state data object
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct ast_mwi_state *ast_mwi_subscriber_data(struct ast_mwi_subscriber *sub);

/*!
 * \brief Retrieve the stasis MWI topic subscription if available.
 *
 * \param sub An MWI subscriber
 *
 * \return The subscriber's stasis subscription
 * \retval NULL if no subscription available
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_subscription *ast_mwi_subscriber_subscription(struct ast_mwi_subscriber *sub);

/*!
 * \brief An MWI state publisher
 *
 * An ao2 managed object. Holds a reference to the latest MWI state for its lifetime.
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct ast_mwi_publisher;

/*!
 * \brief Add an MWI state publisher to the mailbox
 *
 * Adding a publisher to a mailbox will create a stasis topic for the mailbox if one
 * does not already exist. A publisher can be removed by releasing its reference. Doing
 * so releases its underlying reference to the MWI state.
 *
 * \param mailbox The mailbox id to publish to
 *
 * \return An MWI publisher object
 * \retval NULl on error
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct ast_mwi_publisher *ast_mwi_add_publisher(const char *mailbox);

/*! \brief MWI state event interface */
struct ast_mwi_observer {
	/*!
	 * \brief Raised when MWI is being subscribed
	 *
	 * \param mailbox The mailbox id subscribed
	 * \param sub The subscriber subscribed
	 */
	void (*on_subscribe)(const char *mailbox, struct ast_mwi_subscriber *sub);

	/*!
	 * \brief Raised when MWI is being unsubscribed
	 *
	 * \param mailbox The mailbox id being unsubscribed
	 * \param sub The subscriber to unsubscribe
	 */
	void (*on_unsubscribe)(const char *mailbox, struct ast_mwi_subscriber *sub);
};

/*!
 * \brief Add an observer to receive MWI state related events.
 *
 * \param observer The observer handling events
 *
 * \retval 0 if successfully registered
 * \retval -1 otherwise
 *
 * \since 13.28.0
 * \since 16.5.0
 */
int ast_mwi_add_observer(struct ast_mwi_observer *observer);

/*!
 * \brief Remove an MWI state observer.
 *
 * \param observer The observer being removed
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void ast_mwi_remove_observer(struct ast_mwi_observer *observer);

/*!
 * \brief The delegate called for each managed mailbox state.
 *
 * \param mwi_state The mwi state object
 * \param data User data passed in by the initiator
 *
 * \retval 0 to continue traversing
 * \retval CMP_STOP (2) to stop traversing
 *
 * \since 13.28.0
 * \since 16.5.0
 */
typedef int (*on_mwi_state)(struct ast_mwi_state *mwi_state, void *data);

/*!
 * \brief For each managed mailbox call the given handler.
 *
 * \param handler The mwi state handler to call for each managed mailbox
 * \param data User to data to pass on to the handler
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void ast_mwi_state_callback_all(on_mwi_state handler, void *data);

/*!
 * \brief For each managed mailbox that has a subscriber call the given handler.
 *
 * \param handler The mwi state handler to call for each managed mailbox
 * \param data User to data to pass on to the handler
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void ast_mwi_state_callback_subscribed(on_mwi_state handler, void *data);

/*!
 * \brief Publish MWI for the given mailbox.
 *
 * \param publisher The publisher to publish a mailbox update on
 * \param urgent_msgs The number of urgent messages in this mailbox
 * \param new_msgs The number of new messages in this mailbox
 * \param old_msgs The number of old messages in this mailbox
 * \param channel_id A unique identifier for a channel associated with this
 *        change in mailbox state
 * \param eid The EID of the server that originally published the message
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \since 13.28.0
 * \since 16.5.0
 */
int ast_mwi_publish(struct ast_mwi_publisher *publisher, int urgent_msgs,
	int new_msgs, int old_msgs, const char *channel_id, struct ast_eid *eid);

/*!
 * \brief Publish MWI for the given mailbox.
 *
 * \param mailbox The mailbox identifier string.
 * \param context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param urgent_msgs The number of urgent messages in this mailbox
 * \param new_msgs The number of new messages in this mailbox
 * \param old_msgs The number of old messages in this mailbox
 * \param channel_id A unique identifier for a channel associated with this
 *        change in mailbox state
 * \param eid The EID of the server that originally published the message
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \since 13.28.0
 * \since 16.5.0
 */
int ast_mwi_publish_by_mailbox(const char *mailbox, const char *context, int urgent_msgs,
	int new_msgs, int old_msgs, const char *channel_id, struct ast_eid *eid);

/*!
 * \since 12
 * \brief Publish a MWI state update via stasis
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_publish_mwi_state(mailbox, context, new_msgs, old_msgs) \
	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, NULL, NULL)

/*!
 * \since 12
 * \brief Publish a MWI state update associated with some channel
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_publish_mwi_state_channel(mailbox, context, new_msgs, old_msgs, channel_id) \
	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, channel_id, NULL)

/*!
 * \since 12
 * \brief Publish a MWI state update via stasis with all parameters
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_publish_mwi_state_full(
	const char *mailbox,
	const char *context,
	int new_msgs,
	int old_msgs,
	const char *channel_id,
	struct ast_eid *eid);

/*!
 * \since 12.2.0
 * \brief Delete MWI state cached by stasis
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_delete_mwi_state(mailbox, context) \
	ast_delete_mwi_state_full(mailbox, context, NULL)

/*!
 * \since 12.2.0
 * \brief Delete MWI state cached by stasis with all parameters
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_delete_mwi_state_full(const char *mailbox, const char *context, struct ast_eid *eid);

/*!
 * \addtogroup StasisTopicsAndMessages
 */

/*!
 * \brief The structure that contains MWI state
 * \since 12
 */
struct ast_mwi_state {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uniqueid);  /*!< Unique identifier for this mailbox */
	);
	int new_msgs;                    /*!< The current number of new messages for this mailbox */
	int old_msgs;                    /*!< The current number of old messages for this mailbox */
	/*! If applicable, a snapshot of the channel that caused this MWI change */
	struct ast_channel_snapshot *snapshot;
	struct ast_eid eid;              /*!< The EID of the server where this message originated */
	int urgent_msgs;                 /*!< The current number of urgent messages for this mailbox */
};

/*!
 * \brief Object that represents an MWI update with some additional application
 * defined data
 */
struct ast_mwi_blob {
	struct ast_mwi_state *mwi_state;    /*!< MWI state */
	struct ast_json *blob;              /*!< JSON blob of data */
};

/*!
 * \since 12
 * \brief Create a \ref ast_mwi_state object
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 *
 * \return \ref ast_mwi_state object on success
 * \retval NULL on error
 */
struct ast_mwi_state *ast_mwi_create(const char *mailbox, const char *context);

/*!
 * \since 12
 * \brief Creates a \ref ast_mwi_blob message.
 *
 * The \a blob JSON object requires a \c "type" field describing the blob. It
 * should also be treated as immutable and not modified after it is put into the
 * message.
 *
 * \param mwi_state MWI state associated with the update
 * \param message_type The type of message to create
 * \param blob JSON object representing the data.
 * \return \ref ast_mwi_blob message.
 * \retval NULL on error
 */
struct stasis_message *ast_mwi_blob_create(struct ast_mwi_state *mwi_state,
					   struct stasis_message_type *message_type,
					   struct ast_json *blob);

/*!
 * \brief Get the \ref stasis topic for MWI messages
 * \return The topic structure for MWI messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic_all(void);

/*!
 * \brief Get the \ref stasis topic for MWI messages on a unique ID
 * \param uniqueid The unique id for which to get the topic
 * \return The topic structure for MWI messages for a given uniqueid
 * \retval NULL if it failed to be found or allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic(const char *uniqueid);

/*!
 * \brief Get the \ref stasis caching topic for MWI messages
 * \return The caching topic structure for MWI messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic_cached(void);

/*!
 * \brief Backend cache for ast_mwi_topic_cached().
 * \return Cache of \ref ast_mwi_state.
 */
struct stasis_cache *ast_mwi_state_cache(void);

/*!
 * \brief Get the \ref stasis message type for MWI messages
 * \return The message type structure for MWI messages
 * \retval NULL on error
 * \since 12
 */
struct stasis_message_type *ast_mwi_state_type(void);

/*!
 * \brief Get the \ref stasis message type for voicemail application specific messages
 *
 * This message type exists for those messages a voicemail application may wish to send
 * that have no logical relationship with other voicemail applications. Voicemail apps
 * that use this message type must pass a \ref ast_mwi_blob. Any extraneous information
 * in the JSON blob must be packed as key/value pair tuples of strings.
 *
 * At least one key/value tuple must have a key value of "Event".
 *
 * \return The \ref stasis_message_type for voicemail application specific messages
 * \retval NULL on error
 * \since 12
 */
struct stasis_message_type *ast_mwi_vm_app_type(void);

/*!
 * \brief Initialize the mwi core
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 13.27.0
 * \since 16.4.0
 */
int mwi_init(void);

#define AST_MAX_MAILBOX_UNIQUEID (AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2)

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MWI_H */
