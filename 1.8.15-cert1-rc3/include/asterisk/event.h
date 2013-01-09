/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
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
 */

/*!
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
 * \param description Description of the subscription.
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
 *
 * \note A NULL description will cause this function to crash, so watch out!
 */
struct ast_event_sub *ast_event_subscribe(enum ast_event_type event_type,
       ast_event_cb_t cb, const char *description, void *userdata, ...);

/*!
 * \brief Allocate a subscription, but do not activate it
 *
 * \param type the event type to subscribe to
 * \param cb the function to call when an event matches this subscription
 * \param userdata data to pass to the provided callback
 *
 * This function should be used when you want to dynamically build a
 * subscription.
 *
 * \return the allocated subscription, or NULL on failure
 * \since 1.6.1
 */
struct ast_event_sub *ast_event_subscribe_new(enum ast_event_type type,
	ast_event_cb_t cb, void *userdata);

/*!
 * \brief Destroy an allocated subscription
 *
 * \param sub the subscription to destroy
 *
 * This function should be used when a subscription is allocated with
 * ast_event_subscribe_new(), but for some reason, you want to destroy it
 * instead of activating it.  This could be because of an error when
 * reading in the configuration for the dynamically built subscription.
 * \since 1.6.1
 */
void ast_event_sub_destroy(struct ast_event_sub *sub);

/*!
 * \brief Append a uint parameter to a subscription
 *
 * \param sub the dynamic subscription allocated with ast_event_subscribe_new()
 * \param ie_type the information element type for the parameter
 * \param uint the value that must be present in the event to match this subscription
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_sub_append_ie_uint(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, uint32_t uint);

/*!
 * \brief Append a bitflags parameter to a subscription
 *
 * \param sub the dynamic subscription allocated with ast_event_subscribe_new()
 * \param ie_type the information element type for the parameter
 * \param flags the flags that must be present in the event to match this subscription
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.8
 */
int ast_event_sub_append_ie_bitflags(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, uint32_t flags);

/*!
 * \brief Append a string parameter to a subscription
 *
 * \param sub the dynamic subscription allocated with ast_event_subscribe_new()
 * \param ie_type the information element type for the parameter
 * \param str the string that must be present in the event to match this subscription
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_sub_append_ie_str(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, const char *str);

/*!
 * \brief Append a raw parameter to a subscription
 *
 * \param sub the dynamic subscription allocated with ast_event_subscribe_new()
 * \param ie_type the information element type for the parameter
 * \param data the data that must be present in the event to match this subscription
 * \param raw_datalen length of data
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_sub_append_ie_raw(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, void *data, size_t raw_datalen);

/*!
 * \brief Append an 'exists' parameter to a subscription
 *
 * \param sub the dynamic subscription allocated with ast_event_subscribe_new()
 * \param ie_type the information element type that must be present in the event
 *      for it to match this subscription.
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_sub_append_ie_exists(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type);

/*!
 * \brief Activate a dynamically built subscription
 *
 * \param sub the subscription to activate that was allocated using
 *      ast_event_subscribe_new()
 *
 * Once a dynamically built subscription has had all of the parameters added
 * to it, it should be activated using this function.
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_sub_activate(struct ast_event_sub *sub);

/*!
 * \brief Un-subscribe from events
 *
 * \param event_sub This is the reference to the subscription returned by
 *        ast_event_subscribe.
 *
 * This function will remove a subscription and free the associated data
 * structures.
 *
 * \return NULL for convenience.
 * \version 1.6.1 return changed to NULL
 */
struct ast_event_sub *ast_event_unsubscribe(struct ast_event_sub *event_sub);

/*!
 * \brief Get description for a subscription
 *
 * \param sub subscription
 *
 * \return string description of the subscription
 */
const char *ast_event_subscriber_get_description(struct ast_event_sub *sub);

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
 * \brief Dump the event cache for the subscriber
 * \since 1.6.1
 */
void ast_event_dump_cache(const struct ast_event_sub *event_sub);

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
 * \note The EID IE will be appended automatically when this function is used
 *       with at least one IE specified.
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
 * \retval non-zero failure.  Note that the caller of this function is
 *         responsible for destroying the event in the case of a failure.
 *
 * This function queues an event to be dispatched to all of the appropriate
 * subscribers.  This function will not block while the event is being
 * dispatched because the event is queued up for a dispatching thread 
 * to handle.
 */
int ast_event_queue(struct ast_event *event);

/*!
 * \brief Queue and cache an event
 *
 * \param event the event to be queued and cached
 *
 * \details
 * The purpose of caching events is so that the core can retain the last known
 * information for events that represent some sort of state.  That way, when
 * code needs to find out the current state, it can query the cache.
 *
 * The event API already knows which events can be cached and how to cache them.
 *
 * \retval 0 success
 * \retval non-zero failure.
 */
int ast_event_queue_and_cache(struct ast_event *event);

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
 * \brief Append an information element that has a bitflags payload
 *
 * \param event the event that the IE will be appended to
 * \param ie_type the type of IE to append
 * \param bitflags the flags that are the payload of the IE
 *
 * \retval 0 success
 * \retval -1 failure
 * \since 1.8
 *
 * The pointer to the event will get updated with the new location for the event
 * that now contains the appended information element.  If the re-allocation of
 * the memory for this event fails, it will be set to NULL.
 */
int ast_event_append_ie_bitflags(struct ast_event **event, enum ast_event_ie_type ie_type,
	uint32_t bitflags);

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
 * \brief Append the global EID IE
 *
 * \param event the event to append IE to
 *
 * \note For ast_event_new() that includes IEs, this is done automatically
 *       for you.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_event_append_eid(struct ast_event **event);

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
 * \brief Get the value of an information element that has a bitflags payload
 *
 * \param event The event to get the IE from
 * \param ie_type the type of information element to retrieve
 *
 * \return This returns the payload of the information element with the given type.
 *         However, an IE with a payload of 0, and the case where no IE is found
 *         yield the same return value.
 */
uint32_t ast_event_get_ie_bitflags(const struct ast_event *event, enum ast_event_ie_type ie_type);

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
 * \brief Get the hash for the string payload of an IE
 *
 * \param event The event to get the IE from
 * \param ie_type the type of information element to retrieve the hash for
 *
 * \return This function returns the hash value as calculated by ast_str_hash()
 *         for the string payload.  This is stored in the event to avoid
 *         unnecessary string comparisons.
 */
uint32_t ast_event_get_ie_str_hash(const struct ast_event *event, enum ast_event_ie_type ie_type);

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
 * \brief Get the length of the raw payload for a particular IE
 *
 * \param event The event to get the IE payload length from
 * \param ie_type the type of information element to get the length of
 *
 * \return If an IE of type ie_type is found, its payload length is returned.
 *         Otherwise, 0 is returned.
 */
uint16_t ast_event_get_ie_raw_payload_len(const struct ast_event *event, enum ast_event_ie_type ie_type);

/*!
 * \brief Get the string representation of an information element type
 *
 * \param ie_type the information element type to get the string representation of
 *
 * \return the string representation of the information element type
 * \since 1.6.1
 */
const char *ast_event_get_ie_type_name(enum ast_event_ie_type ie_type);

/*!
 * \brief Get the payload type for a given information element type
 *
 * \param ie_type the information element type to get the payload type of
 *
 * \return the payload type for the provided IE type
 * \since 1.6.1
 */
enum ast_event_ie_pltype ast_event_get_ie_pltype(enum ast_event_ie_type ie_type);

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
 * \brief Get the string representation of the type of the given event
 *
 * \arg event the event to get the type of
 *
 * \return the string representation of the event type of the provided event
 * \since 1.6.1
 */
const char *ast_event_get_type_name(const struct ast_event *event);

/*!
 * \brief Convert a string into an event type
 *
 * \param str the string to convert
 * \param event_type an output parameter for the event type
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_str_to_event_type(const char *str, enum ast_event_type *event_type);

/*!
 * \brief Convert a string to an IE type
 *
 * \param str the string to convert
 * \param ie_type an output parameter for the IE type
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int ast_event_str_to_ie_type(const char *str, enum ast_event_ie_type *ie_type);

/*!
 * \brief Get the size of an event
 *
 * \param event the event to get the size of
 *
 * \return the number of bytes contained in the event
 * \since 1.6.1
 */
size_t ast_event_get_size(const struct ast_event *event);

/*!
 * \brief Initialize an event iterator instance
 *
 * \param iterator The iterator instance to initialize
 * \param event The event that will be iterated through
 *
 * \retval 0 Success, there are IEs available to iterate
 * \retval -1 Failure, there are no IEs in the event to iterate
 */
int ast_event_iterator_init(struct ast_event_iterator *iterator, const struct ast_event *event);

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
 * \brief Get the value of the current IE in the iterator as an integer payload
 *
 * \param iterator The iterator instance
 *
 * \return This returns the payload of the information element as a uint.
 */
uint32_t ast_event_iterator_get_ie_uint(struct ast_event_iterator *iterator);

/*!
 * \brief Get the value of the current IE in the iterator as a bitflags payload
 *
 * \param iterator The iterator instance
 *
 * \return This returns the payload of the information element as bitflags.
 */
uint32_t ast_event_iterator_get_ie_bitflags(struct ast_event_iterator *iterator);

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

/*!
 * \brief Get the length of the raw payload for the current IE for an iterator
 *
 * \param iterator The IE iterator
 *
 * \return The payload length of the current IE
 */
uint16_t ast_event_iterator_get_ie_raw_payload_len(struct ast_event_iterator *iterator);

/*!
 * \brief Get the minimum length of an ast_event.
 *
 * \return minimum amount of memory that will be consumed by any ast_event.
 */
size_t ast_event_minimum_length(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* AST_EVENT_H */
