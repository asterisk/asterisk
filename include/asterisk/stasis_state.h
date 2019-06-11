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

#ifndef _STASIS_STATE_H
#define _STASIS_STATE_H

/*! \file
 *
 * \brief Stasis State API.
 *
 * \par Intro
 *
 * This module defines the data structures, and handling of "state" for topics within
 * stasis. State is defined as the last stasis message, and its contained message data,
 * published on a given topic.
 *
 * Concepts to know:
 *  - \ref stasis_state_manager
 *  - \ref stasis_state_subscriber
 *  - \ref stasis_state_publisher
 *  - \ref stasis_state_observer
 *
 * \par stasis_state_manager
 *
 * The manager stores and well, manages state data. Each state is an association of
 * a unique stasis topic, and the last known published stasis message on that topic.
 * There is only ever one managed state object per topic. For each topic all messages
 * are forwarded to an "all" topic also maintained by the manager. This allows
 * subscriptions to all managed topics, and their state. Managed state is created in
 * one of several ways:
 *
 *   Adding an explicit subscriber
 *   Adding an explicit publisher
 *   Adding an implicit publisher
 *   Retrieving a stasis state topic from the manager via the \ref stasis_state_topic
 *     function prior to doing one of the above (DO NOT DO THIS).
 *
 * More on the first three options later (see relevant section descriptions below). The
 * last option, creation through retrieving a topic is not only NOT recommended, but
 * should NOT even BE DONE. Doing so will inevitably result in a memory leak. Why then
 * is this even allowed? The short answer is backwards compatibility. The slightly longer
 * answer is at the time of this module's creation that's how things were historically
 * done using a combination of stasis topic management spread throughout various other
 * modules, and stasis caching. And yes it did cause a memory leak.
 *
 * Preferably, any new code wishing to track topics and states should do so by adding
 * either an explicit subscriber and/or publisher.
 *
 * \par stasis_state_subscriber
 *
 * As mentioned, topic and state can be created, or referenced within the manager by adding
 * a \ref stasis_state_subscriber. When adding a subscriber if no state currently exists
 * new managed state is immediately created. If managed state already exists then a new
 * subscriber is created referencing that state. The managed state is guaranteed to live
 * throughout the subscriber's lifetime. State is only removed from the manager when no
 * other entities require it (no more subscribers, or publishers).
 *
 * Subscribers are ao2 objects. Therefore there is no explicit cleanup required aside from
 * dereferencing the subscriber object using normal ao2 dereferencing methods.
 *
 * \par stasis_state_publisher
 *
 * There are two ways of tracking publishers: explicitly and implicitly.
 *
 * Topic and state can be created, or referenced within the manager by also explicitly adding
 * a \ref stasis_state_publisher. When adding a publisher if no state currently exists new
 * managed state is created. If managed state already exists then a new publisher is created
 * referencing that state. The managed state is guaranteed to live throughout the publisher's
 * lifetime. State is only removed from the manager when no other entities require it (no more
 * publishers, or subscribers).
 *
 * Explicit publishers are ao2 objects. Therefore there is no cleanup required aside from
 * dereferencing the publisher object using normal ao2 dereferencing methods.
 *
 * When adding an explicit publisher, messages should be published using the \ref
 * stasis_state_publish function. This not only skips a lookup, but doesn't add an implicit
 * publisher. They are not necessarily mutually exclusive it's just that the two ways exist
 * to solve two different problems.
 *
 * For example (using an explicit publisher):
 *
 * // Add an explicit publisher to topic/state "8675309" within
 * // a given manager context
 * pub = stasis_state_add_publisher(manager, "8675309");
 *
 * // Publish a stasis message to the topic/state
 * stasis_state_publish(pub, msg);
 *
 * // Publish another a stasis message to the topic/state
 * stasis_state_publish(pub, msg);
 *
 * // Done with the publisher release the reference
 * ao2_ref(pub, -1);
 *
 * An implicit publisher can also be created by calling \ref stasis_state_publish_by_id. Calling
 * this function not only publishes the message within stasis (creating managed state if needed)
 * it also sets up internal tracking of the publishing module using an \ref ast_eid. However, a
 * final call to \ref stasis_state_remove_publish_by_id must be done in order to remove the eid
 * reference, which will subsequently allow the underlying managed state to be eventually deleted.
 *
 * For example (using an implicit publisher):
 *
 *  // Publish a stasis message to topic/state 8675309 within a
 *  // given manager context and use the system's default eid
 *  stasis_state_publish_by_id(manager, "8675309", NULL, msg);
 *
 *  // Do some stuff and then publish again
 *  stasis_state_publish_by_id(manager, "8675309", NULL, msg);
 *
 *  // Done with all our publishing, so post a final clearing
 *  // message and remove the implicit publisher
 *  stasis_state_remove_publish_by_id(manager, "8675309", NULL, msg);
 *
 * Explicit publisher/publishing is preferred. However, implicit publishing is allowed for those
 * situations where it makes more sense to do so, but has been implemented mostly for backwards
 * compatibility with some modules (using implicit publishing required less initial code changes
 * to some legacy subsystems).
 *
 * \par stasis_state_observer
 *
 * Some modules may wish to watch for, and react to managed state events. By registering a state
 * observer, and implementing handlers for the desired callbacks those modules can do so.
 */

#include "asterisk/stasis.h"

struct ast_eid;

/*!
 * \brief Manages a collection of stasis states.
 *
 * Maintains data related to stasis state. Managed state is an association of a unique stasis
 * topic (named by a given unique id), and the last known published message.
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_manager;

/*!
 * \brief Create a stasis state manager.
 *
 * \note The state manager is an ao2_object. When done simply decrement its reference
 * for object cleanup.
 *
 * \param topic_name The name of the topic to create that all state topics
 *        get forwarded to
 *
 * \retval A stasis state manager
 * \retval NULL if an error occurred
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_manager *stasis_state_manager_create(const char *topic_name);

/*!
 * \brief Retrieve the manager's topic (the topic that all state topics get forwarded to)
 *
 * \param manager The manager object
 *
 * \retval The manager's topic.
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_topic *stasis_state_all_topic(struct stasis_state_manager *manager);

/*!
 * \brief Retrieve a managed topic creating one if not currently managed.
 *
 * WARNING This function should not be called before adding a publisher or subscriber or
 * it will cause a memory leak within the stasis state manager. This function is here in
 * order to allow for compatibility with how things used to work.
 *
 * Also much like the similar functionality from before it returns the stasis topic, but
 * does not bump its reference.
 *
 * \param manager The manager object
 * \param id The unique id of/for the topic
 *
 * \retval A managed stasis topic.
 * \retval NULL if an error occurred
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_topic *stasis_state_topic(struct stasis_state_manager *manager, const char *id);

/*!
 * \brief A stasis state subscriber
 *
 * A subscriber to a particular stasis state. As such it holds a reference to the
 * underlying stasis state, so that managed state is guaranteed to exist for the
 * lifetime of the subscriber.
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_subscriber;

/*!
 * \brief Add a subscriber to the managed stasis state for the given id
 *
 * Adds a subscriber to a managed state based on id. If managed state does not already
 * exists for the given id then new managed state is created. Otherwise the existing
 * state is subscribed to.
 *
 * \param manager The manager object
 * \param id The unique id of a managed state
 *
 * \retval A stasis state subscriber
 * \retval NULL if an error occurred
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_subscriber *stasis_state_add_subscriber(
	struct stasis_state_manager *manager, const char *id);

/*!
 * \brief Add a subscriber, and subscribe to its underlying stasis topic.
 *
 * Adds a subscriber to a managed state based on id. If managed state does not already
 * exists for the given id then new managed state is created. Otherwise the existing
 * state is subscribed to. If the state is successfully subscribed to then a stasis
 * subscription is subsequently created as well.
 *
 * \param manager The manager object
 * \param id The unique id of a managed state
 * \param callback The stasis subscription callback
 * \param data A user data object passed to the stasis subscription
 *
 * \retval A stasis state subscriber
 * \retval NULL if an error occurred
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_subscriber *stasis_state_subscribe_pool(struct stasis_state_manager *manager,
	const char *id, stasis_subscription_cb callback, void *data);

/*!
 * \brief Unsubscribe from the stasis topic and stasis state.
 *
 * \param sub A stasis state subscriber
 *
 * \retval NULL
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void *stasis_state_unsubscribe(struct stasis_state_subscriber *sub);

/*!
 * \brief Unsubscribe from the stasis topic, block until the final message
 * is received, and then unsubscribe from stasis state.
 *
 * \param sub A stasis state subscriber
 *
 * \retval NULL
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void *stasis_state_unsubscribe_and_join(struct stasis_state_subscriber *sub);

/*!
 * \brief Retrieve the underlying subscribed to state's unique id
 *
 * \param sub A stasis state subscriber
 *
 * \retval The managed state's id
 *
 * \since 13.28.0
 * \since 16.5.0
 */
const char *stasis_state_subscriber_id(const struct stasis_state_subscriber *sub);

/*!
 * \brief Retrieve the subscriber's topic
 *
 * \note Returned topic's reference count is NOT incremented. However, the topic is
 * guaranteed to live for the lifetime of the subscriber.
 *
 * \param sub A stasis state subscriber
 *
 * \retval The subscriber's topic
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_topic *stasis_state_subscriber_topic(struct stasis_state_subscriber *sub);

/*!
 * \brief Retrieve the last known state stasis message payload for the subscriber
 *
 * If a stasis message has been published to this state, this function returns
 * that message's payload object. If no stasis message has been published on the
 * state, or the message's payload does not exist then NULL is returned.
 *
 * \note Returned data's reference count is incremented
 *
 * \param sub A stasis state subscriber
 *
 * \retval The subscriber's state message data
 * \retval NULL if no data has been published yet
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void *stasis_state_subscriber_data(struct stasis_state_subscriber *sub);

/*!
 * \brief Retrieve the stasis topic subscription if available.
 *
 * \param sub A stasis state subscriber
 *
 * \retval The subscriber's stasis subscription
 * \retval NULL if no subscription available
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_subscription *stasis_state_subscriber_subscription(
	struct stasis_state_subscriber *sub);

/*!
 * \brief A stasis state publisher
 *
 * A publisher to a particular stasis state and topic. As such it holds a reference to
 * the underlying stasis state, so that managed state is guaranteed to exist for the
 * lifetime of the publisher.
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_publisher;

/*!
 * \brief Add a publisher to the managed state for the given id
 *
 * Adds a publisher to a managed state based on id. If managed state does not already
 * exists for the given id then new managed state is created. Otherwise the existing
 * state is used.
 *
 * \param manager The manager object
 * \param id The unique id of a managed state
 *
 * \retval A stasis state publisher
 * \retval NULL if an error occurred
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_state_publisher *stasis_state_add_publisher(
	struct stasis_state_manager *manager, const char *id);

/*!
 * \brief Retrieve the publisher's underlying state's unique id
 *
 * \param pub A stasis state publisher
 *
 * \retval The managed state's id
 *
 * \since 13.28.0
 * \since 16.5.0
 */
const char *stasis_state_publisher_id(const struct stasis_state_publisher *pub);

/*!
 * \brief Retrieve the publisher's topic
 *
 * \note Returned topic's reference count is NOT incremented. However, the topic is
 * guaranteed to live for the lifetime of the publisher.
 *
 * \param pub A stasis state publisher
 *
 * \retval The publisher's topic
 *
 * \since 13.28.0
 * \since 16.5.0
 */
struct stasis_topic *stasis_state_publisher_topic(struct stasis_state_publisher *pub);

/*!
 * \brief Publish to a managed state (topic) using a publisher.
 *
 * \param pub The publisher to use to publish the message
 * \param msg The message to publish
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void stasis_state_publish(struct stasis_state_publisher *pub, struct stasis_message *msg);

/*!
 * \brief Publish to a managed named by id topic, and add an implicit subscriber.
 *
 * \note It is recommended when adding new publisher functionality within a module
 * to create and use an explicit publisher instead of using this method.
 *
 * This creates an implicit publisher keyed off the eid. This ability was mainly
 * implemented in order to maintain compatibility with already established code.
 * Allowing the creation of an implicit publisher made is so less changes were
 * required when stasis state module was initially added.
 *
 * There should only ever be one publisher for a specifically named managed topic
 * within the system. This being the case we can use the eid to implicitly track
 * the publisher. However once publishing is no longer needed for a topic a call
 * to stasis_state_remove_publish_by_id is required in order to remove the implicit
 * publisher. Thus allowing for its eventual destruction. Without the call to remove
 * a memory leak will occur.
 *
 * \param manager The state manager
 * \param id A state's unique id
 * \param eid The unique system id
 * \param msg The message to publish
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void stasis_state_publish_by_id(struct stasis_state_manager *manager, const char *id,
	const struct ast_eid *eid, struct stasis_message *msg);

/*!
 * \brief Publish to a managed named by id topic, and remove an implicit publisher.
 *
 * This function should be called after calling stasis_state_publish_by_id at least once
 * for the same manager, id, and eid. If the given stasis message is NULL then the implicit
 * publisher is removed, but no last message is published.
 *
 * See note and description on stasis_state_publish_by_id for more details about if, and
 * when this function should be used.
 *
 * \param manager The state manager
 * \param id A state's unique id
 * \param eid The unique system id
 * \param msg The message to publish (can be NULL)
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void stasis_state_remove_publish_by_id(struct stasis_state_manager *manager,
	const char *id, const struct ast_eid *eid, struct stasis_message *msg);

/*! \brief Managed stasis state event interface */
struct stasis_state_observer {
	/*!
	 * \brief Raised when any managed state is being subscribed.
	 *
	 * \param id The unique id of the managed state
	 * \param sub The subscriber subscribed
	 */
	void (*on_subscribe)(const char *id, struct stasis_state_subscriber *sub);

	/*!
	 * \brief Raised when any managed state is being unsubscribed.
	 *
	 * \param id The unique id of the managed state
	 * \param sub The subscriber to unsubscribe
	 */
	void (*on_unsubscribe)(const char *id, struct stasis_state_subscriber *sub);
};

/*!
 * \brief Add an observer to receive managed state related events.
 *
 * \param manager The state manager
 * \param observer The observer handling events
 *
 * \retval 0 if successfully registered
 * \retval -1 on failure
 *
 * \since 13.28.0
 * \since 16.5.0
 */
int stasis_state_add_observer(struct stasis_state_manager *manager,
	struct stasis_state_observer *observer);

/*!
 * \brief Remove an observer (will no longer receive managed state related events).
 *
 * \param manager The state manager
 * \param observer The observer being removed
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void stasis_state_remove_observer(struct stasis_state_manager *manager,
	struct stasis_state_observer *observer);

/*!
 * \brief The delegate called for each managed state.
 *
 * \param id The unique id of a managed state object
 * \param msg The last published message on the state, or NULL
 * \param user_data Data object the user passed into the manager callback
 *
 * \retval 0 to continue traversing
 * \retval CMP_STOP (2) to stop traversing
 *
 * \since 13.28.0
 * \since 16.5.0
 */
typedef int (*on_stasis_state)(const char *id, struct stasis_message *msg, void *user_data);

/*!
 * \brief For each managed state call the given handler.
 *
 * \param manager The state manager
 * \param handler The handler to call for each managed state
 * \param data User to data to pass on to the handler
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void stasis_state_callback_all(struct stasis_state_manager *manager, on_stasis_state handler,
	void *data);

/*!
 * \brief For each managed, and explicitly subscribed state call the given handler.
 *
 * \param manager The state manager
 * \param handler The handler to call for each managed state
 * \param data User to data to pass on to the handler
 *
 * \since 13.28.0
 * \since 16.5.0
 */
void stasis_state_callback_subscribed(struct stasis_state_manager *manager, on_stasis_state handler,
	void *data);

#endif /* _STASIS_STATE_H */
