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
 * Prior to the creation of \ref stasis, the purpose of this API was to provide
 * a generic way to share events between Asterisk modules. Once there was a need
 * to disseminate data whose definition was provided by the producers/consumers,
 * it was no longer possible to use the binary representation in the generic
 * event system.
 *
 * That aside, the generic event system is still useful and used by several
 * modules in Asterisk.
 *  - CEL uses the \ref ast_event representation to pass information to registered
 *    backends.
 *  - The \file res_corosync.c module publishes \ref ast_event representations of
 *    information to other Asterisk instances in a cluster.
 *  - Security event represent their event types and data using this system.
 *  - Theoretically, any \ref stasis message can use this system to pass
 *    information around in a binary format.
 *
 * Events have an associated event type, as well as information elements.  The
 * information elements are the meta data that go along with each event.  For
 * example, in the case of message waiting indication, the event type is MWI,
 * and each MWI event contains at least three information elements: the
 * mailbox, the number of new messages, and the number of old messages.
 */

#ifndef AST_EVENT_H
#define AST_EVENT_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/event_defs.h"

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
 * by a valid IE payload type.  A payload must also be specified
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
 */
void ast_event_destroy(struct ast_event *event);

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
 * \brief Get the string representation of the type of the given event
 *
 * \arg event the event to get the type of
 *
 * \return the string representation of the event type of the provided event
 * \since 1.6.1
 */
const char *ast_event_get_type_name(const struct ast_event *event);

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
 * \brief Get the value of the current IE in the iterator as a string payload
 *
 * \param iterator The iterator instance
 *
 * \return This returns the payload of the information element as a string.
 */
const char *ast_event_iterator_get_ie_str(struct ast_event_iterator *iterator);

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
