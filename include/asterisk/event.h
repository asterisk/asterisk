/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file
 * \author Russell Bryant <russell@digium.com>
 * \ref AstGenericEvents
 * \page AstGenericEvents Generic event system
 *
 * The purpose of this API is to provide a generic way to share events between
 * Asterisk modules.  Code can generate events, and other code can subscribe to
 * them.
 *
 * Events have an associated event type, as well as information elements.  The
 * information elements are the meta data that go along with each event.  For
 * example, in the case of message waiting indication, the event type is MWI,
 * and each MWI event contains at least three information elements: the
 * mailbox, the number of new messages, and the number of old messages.
 *
 * Subscriptions to events consist of an event type and information elements,
 * as well.  Subscriptions can be to all events, or a certain subset of events.
 * If an event type is provided, only events of that type will be sent to this
 * subscriber.  Furthermore, if information elements are supplied with the
 * subscription, only events that contain the specified information elements
 * with specified values will be sent to the subscriber.  For example, when a
 * SIP phone subscribes to MWI for mailbox 1234, then chan_sip can subscribe
 * to internal Asterisk MWI events with the MAILBOX information element with
 * a value of "1234".
 *
 * Another key feature of this event system is the ability to cache events.
 * It is useful for some types of events to be able to remember the last known
 * value.  These are usually events that indicate some kind of state change.
 * In the example of MWI, app_voicemail can instruct the event core to cache
 * these events based on the mailbox.  So, the last known MWI state of each
 * mailbox will be cached, and other modules can retrieve this information
 * on demand without having to poll the mailbox directly.
 */

#ifndef AST_EVENT_H
#define AST_EVENT_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/event_defs.h"

/*! 
 * \brief Subscriber event callback type
 *
 * \param event the event being passed to the subscriber
 * \param userdata the data provider in the call to ast_event_subscribe()
 *
 * \return The event callbacks do not return anything.
 */
typedef void (*ast_event_cb_t)(const struct ast_event *event, void *userdata);

/*! 
 * \brief Subscribe to events
 *
 * \param event_type The type of events to subscribe to
 * \param cb The function to be called with events
 * \param userdata data to be passed to the event callback
 *
 * The rest of the arguments to this function specify additional parameters for
 * the subscription to filter which events are passed to this subscriber.  The
 * arguments must be in sets of:
 * \code
 *    <enum ast_event_ie_type>, [enum ast_event_ie_pltype, [payload] ]
 * \endcode
 * and must end with AST_EVENT_IE_END.
 *
 * If the ie_type specified is *not* AST_EVENT_IE_END, then it must be followed
 * by a valid IE payload type.  If the payload type specified is
 * AST_EVENT_IE_PLTYPE_EXISTS, then the 3rd argument should not be provided.
 * Otherwise, a payload must also be specified.
 *
 * \return This returns a reference to the subscription for use with
 *         un-subscribing later.  If there is a failure in creating the
 *         subscription, NULL will be returned.
 *
 * Example usage:
 *
 * \code
 * peer->mwi_event_sub = ast_event_subscribe(AST_EVENT_MWI, mwi_event_cb, peer,
 *     AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, peer->mailbox,
 *     AST_EVENT_IE_END);
 * \endcode
 *
 * This creates a subscription to AST_EVENT_MWI events that contain an
 * information element, AST_EVENT_IE_MAILBOX, with the same string value
 * contained in peer->mailbox.  Also, the event callback will be passed a
 * pointer to the peer.
 */
struct ast_event_sub *ast_event_subscribe(enum ast_event_type event_type, 
	ast_event_cb_t cb, void *userdata, ...);

/*!
 * \brief Un-subscribe from events
 *
 * \param event_sub This is the reference to the subscription returned by
 *        ast_event_subscribe.
 * 
 * \return Nothing
 */
void ast_event_unsubscribe(struct ast_event_sub *event_sub);

/*!
 * \brief Check if subscribers exist
 *
 * \param event_type This is the type of event that the caller would like to
 *        check for subscribers to.
 *
 * The rest of the arguments to this function specify additional parameters for
 * checking for subscriptions to subsets of an event type. The arguments must
 * in sets of:
 * \code
 *    <enum ast_event_ie_type>, [enum ast_event_ie_pltype, [payload] ]
 * \endcode
 * and must end with AST_EVENT_IE_END.
 *
 * If the ie_type specified is *not* AST_EVENT_IE_END, then it must be followed
 * by a valid IE payload type.  If the payload type specified is
 * AST_EVENT_IE_PLTYPE_EXISTS, then the 3rd argument should not be provided.
 * Otherwise, a payload must also be specified.
 *
 * \return This returns one of the values defined in the ast_event_subscriber_res
 *         enum which will indicate if subscribers exist that match the given
 *         criteria.
 *
 * Example usage:
 *
 * \code
 * if (ast_event_check_subscriber(AST_EVENT_MWI,
 *     AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
 *     AST_EVENT_IE_END) == AST_EVENT_SUB_NONE) {
 *       return;
 * }
 * \endcode
 *
 * This example will check if there are any subscribers to MWI events for the 
 * mailbox defined in the "mailbox" variable.
 */
enum ast_event_subscriber_res ast_event_check_subscriber(enum ast_event_type event_type, ...);

/*!
 * \brief Report current subscriptions to a subscription subscriber
 *
 * \arg sub the subscription subscriber
 *
 * \return nothing
 *
 * This reports all of the current subscribers to a subscriber of
 * subscribers to a specific event type.  (Try saying that a few times fast).
 *
 * The idea here is that it is sometimes very useful for a module to know when
 * someone subscribes to events.  However, when they first subscribe, this
 * provides that module the ability to request the event core report to them
 * all of the subscriptions to that event type that already exist.
 */
void ast_event_report_subs(const struct ast_event_sub *sub);

/*!
 * \brief Create a new event
 *
 * \param event_type The type of event to create
 *
 * The rest of the arguments to this function specify information elements to
 * add to the event.  They are specified in the form:
 * \code
 *    <enum ast_event_ie_type>, [enum ast_event_ie_pltype, [payload] ]
 * \endcode
 * and must end with AST_EVENT_IE_END.
 *
 * If the ie_type specified is *not* AST_EVENT_IE_END, then it must be followed
 * by a valid IE payload type.  The payload type, EXISTS, should not be used here
 * because it makes no sense to do so.  So, a payload must also be specified
 * after the IE payload type.
 *
 * \return This returns the event that has been created.  If there is an error
 *         creating the event, NULL will be returned.
 *
 * Example usage:
 *
 * \code
 * if (!(event = ast_event_new(AST_EVENT_MWI,
 *     AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
 *     AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, new,
 *     AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, old,
 *     AST_EVENT_IE_END))) {
 *       return;
 * }
 * \endcode
 *
 * This creates a MWI event with 3 information elements, a mailbox which is
 * a string, and the number of new and old messages, specified as integers.
 */
struct ast_event *ast_event_new(enum ast_event_type event_type, ...);

/*!
 * \brief Destroy an event
 *
 * \param event the event to destroy
 *
 * \return Nothing
 *
 * \note Events that have been queued should *not* be destroyed by the code that
 *       created the event.  It will be automatically destroyed after being
 *       dispatched to the appropriate subscribers.
 */
void ast_event_destroy(struct ast_event *event);

/*!
 * \brief Queue an event
 *
 * \param event the event to be queued
 *
 * \retval zero success
 * \retval non-zero failure
 *
 * This function queues an event to be dispatched to all of the appropriate
 * subscribers.  This function will not block while the event is being
 * dispatched because a pool of event dispatching threads handle the event 
 * queue.
 */
int ast_event_queue(struct ast_event *event);

/*!
 * \brief Queue and cache an event
 *
 * \param event the event to be queued and cached
 *
 * The rest of the arguments to this function specify information elements to
 * use for determining which events in the cache that this event should replace.
 * All events in the cache that match the specified criteria will be removed from
 * the cache and then this one will be added.  The arguments are specified in 
 * the form:
 *
 * \code
 *    <enum ast_event_ie_type>, [enum ast_event_ie_pltype]
 * \endcode
 * and must end with AST_EVENT_IE_END.
 *
 * If the ie_type specified is *not* AST_EVENT_IE_END, then it must be followed
 * by a valid IE payload type.  If the payload type given is EXISTS, then all
 * events that contain that information element will be removed from the cache.
 * Otherwise, all events in the cache that contain an information element with
 * the same value as the new event will be removed.
 *
 * \note If more than one IE parameter is specified, they *all* must match for
 *       the event to be removed from the cache.
 *
 * Example usage:
 *
 * \code
 * ast_event_queue_and_cache(event,
 *     AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR,
 *     AST_EVENT_IE_END);
 * \endcode
 *
 * This example queues and caches an event.  Any events in the cache that have
 * the same MAILBOX information element as this event will be removed.
 *
 * The purpose of caching events is so that the core can retain the last known
 * information for events that represent some sort of state.  That way, when
 * code needs to find out the current state, it can query the cache.
 */
int ast_event_queue_and_cache(struct ast_event *event, ...);

/*!
 * \brief Retrieve an event from the cache
 *
 * \param ast_event_type The type of event to retrieve from the cache
 *
 * The rest of the arguments to this function specify information elements to
 * match for retrieving events from the cache.  They are specified in the form:
 * \code
 *    <enum ast_event_ie_type>, [enum ast_event_ie_pltype, [payload] ]
 * \endcode
 * and must end with AST_EVENT_IE_END.
 *
 * If the ie_type specified is *not* AST_EVENT_IE_END, then it must be followed
 * by a valid IE payload type.  If the payload type specified is
 * AST_EVENT_IE_PLTYPE_EXISTS, then the 3rd argument should not be provided.
 * Otherwise, a payload must also be specified.
 *
 * \return A reference to an event retrieved from the cache.  If no event was
 *         found that matches the specified criteria, then NULL will be returned.
 *
 * \note If more than one event in the cache matches the specified criteria, only
 *       one will be returned, and it is undefined which one it will be.
 *
 * \note The caller of this function *must* call ast_event_destroy() on the
 *       returned event after it is done using it.
 *
 * Example Usage:
 *
 * \code
 * event = ast_event_get_cached(AST_EVENT_MWI,
 *     AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
 *     AST_EVENT_IE_END);
 * \endcode
 *
 * This example will check for an MWI event in the cache that matches the
 * specified mailbox.  This would be the way to find out the last known state
 * of a mailbox without having to poll the mailbox directly.
 */
struct ast_event *ast_event_get_cached(enum ast_event_type, ...);

/*!
 * \brief Append an information element that has a string payload
 *
 * \param event the event that the IE will be appended to
 * \param ie_type the type of IE to append
 * \param str The string for the payload of the IE
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * The pointer to the event will get updated with the new location for the event
 * that now contains the appended information element.  If the re-allocation of
 * the memory for this event fails, it will be set to NULL.
 */
int ast_event_append_ie_str(struct ast_event **event, enum ast_event_ie_type ie_type,
	const char *str);

/*!
 * \brief Append an information element that has an integer payload
 *
 * \param event the event that the IE will be appended to
 * \param ie_type the type of IE to append
 * \param data The integer for the payload of the IE
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * The pointer to the event will get updated with the new location for the event
 * that now contains the appended information element.  If the re-allocation of
 * the memory for this event fails, it will be set to NULL.
 */
int ast_event_append_ie_uint(struct ast_event **event, enum ast_event_ie_type ie_type,
	uint32_t data);

/*!
 * \brief Append an information element that has a raw payload
 *
 * \param event the event that the IE will be appended to
 * \param ie_type the type of IE to append
 * \param data A pointer to the raw data for the payload of the IE
 * \param data_len The amount of data to copy into the payload
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * The pointer to the event will get updated with the new location for the event
 * that now contains the appended information element.  If the re-allocation of
 * the memory for this event fails, it will be set to NULL.
 */
int ast_event_append_ie_raw(struct ast_event **event, enum ast_event_ie_type ie_type,
	const void *data, size_t data_len);

/*!
 * \brief Get the value of an information element that has an integer payload
 *
 * \param event The event to get the IE from
 * \param ie_type the type of information element to retrieve
 * 
 * \return This returns the payload of the information element with the given type.
 *         However, an IE with a payload of 0, and the case where no IE is found
 *         yield the same return value.
 */
uint32_t ast_event_get_ie_uint(const struct ast_event *event, enum ast_event_ie_type ie_type);

/*!
 * \brief Get the value of an information element that has a string payload
 *
 * \param event The event to get the IE from
 * \param ie_type the type of information element to retrieve
 * 
 * \return This returns the payload of the information element with the given type.
 *         If the information element isn't found, NULL will be returned.
 */
const char *ast_event_get_ie_str(const struct ast_event *event, enum ast_event_ie_type ie_type);

/*!
 * \brief Get the value of an information element that has a raw payload
 *
 * \param event The event to get the IE from
 * \param ie_type the type of information element to retrieve
 * 
 * \return This returns the payload of the information element with the given type.
 *         If the information element isn't found, NULL will be returned.
 */
const void *ast_event_get_ie_raw(const struct ast_event *event, enum ast_event_ie_type ie_type);

/*!
 * \brief Get the type for an event
 *
 * \param event the event to get the type for
 *
 * \return the event type as represented by one of the values in the
 *         ast_event_type enum
 */
enum ast_event_type ast_event_get_type(const struct ast_event *event);

/*!
 * \brief Initialize an event iterator instance
 *
 * \param iterator The iterator instance to initialize
 * \param event The event that will be iterated through
 *
 * \return Nothing
 */
void ast_event_iterator_init(struct ast_event_iterator *iterator, const struct ast_event *event);

/*!
 * \brief Move iterator instance to next IE
 *
 * \param iterator The iterator instance
 *
 * \retval 0 on success
 * \retval -1 if end is reached
 */
int ast_event_iterator_next(struct ast_event_iterator *iterator);

/*!
 * \brief Get the type of the current IE in the iterator instance
 *
 * \param iterator The iterator instance
 *
 * \return the ie type as represented by one of the value sin the
 *         ast_event_ie_type enum
 */
enum ast_event_ie_type ast_event_iterator_get_ie_type(struct ast_event_iterator *iterator);

/*!
 * \brief Get the value of the current IE in the ierator as an integer payload
 *
 * \param iterator The iterator instance
 *
 * \return This returns the payload of the information element as a uint.
 */
uint32_t ast_event_iterator_get_ie_uint(struct ast_event_iterator *iterator);

/*!
 * \brief Get the value of the current IE in the iterator as a string payload
 *
 * \param iterator The iterator instance
 *
 * \return This returns the payload of the information element as a string.
 */
const char *ast_event_iterator_get_ie_str(struct ast_event_iterator *iterator);

/*!
 * \brief Get the value of the current IE in the iterator instance that has a raw payload
 *
 * \param iterator The iterator instance
 *
 * \return This returns the payload of the information element as type raw.
 */
void *ast_event_iterator_get_ie_raw(struct ast_event_iterator *iterator);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* AST_EVENT_H */
