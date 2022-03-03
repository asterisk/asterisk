/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 *
 * \brief Out-of-call text message support
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * The purpose of this API is to provide support for text messages that
 * are not session based.  The messages are passed into the Asterisk core
 * to be routed through the dialplan or another interface and potentially
 * sent back out through a message technology that has been registered
 * through this API.
 */

#ifndef __AST_MESSAGE_H__
#define __AST_MESSAGE_H__

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief A text message.
 *
 * This is an opaque type that represents a text message.
 */
struct ast_msg;

/*!
 * \brief A message technology
 *
 * A message technology is capable of transmitting text messages.
 */
struct ast_msg_tech {
	/*!
	 * \brief Name of this message technology
	 *
	 * This is the name that comes at the beginning of a URI for messages
	 * that should be sent to this message technology implementation.
	 * For example, messages sent to "xmpp:rbryant@digium.com" would be
	 * passed to the ast_msg_tech with a name of "xmpp".
	 */
	const char * const name;
	/*!
	 * \brief Send a message.
	 *
	 * \param msg the message to send
	 * \param to the URI of where the message is being sent
	 * \param from the URI of where the message was sent from
	 *
	 * The fields of the ast_msg are guaranteed not to change during the
	 * duration of this function call.
	 *
	 * \retval 0 success
	 * \retval non-zero failure
	 */
	int (* const msg_send)(const struct ast_msg *msg, const char *to, const char *from);
};

/*!
 * \brief Register a message technology
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_msg_tech_register(const struct ast_msg_tech *tech);

/*!
 * \brief Unregister a message technology.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_msg_tech_unregister(const struct ast_msg_tech *tech);

/*!
 * \brief An external processor of received messages
 * \since 12.5.0
 */
struct ast_msg_handler {
	/*!
	 * \brief Name of the message handler
	 */
	const char *name;

	/*!
	 * \brief The function callback that will handle the message
	 *
	 * \param msg The message to handle
	 *
	 * \retval 0 The handler processed the message successfull
	 * \retval non-zero The handler passed or could not process the message
	 */
	int (* const handle_msg)(struct ast_msg *msg);

	/*!
	 * \brief Return whether or not the message has a valid destination
	 *
	 * A message may be delivered to the dialplan and/or other locations,
	 * depending on whether or not other handlers have been registered. This
	 * function is called by the message core to determine if any handler can
	 * process a message.
	 *
	 * \param msg The message to inspect
	 *
	 * \retval 0 The message does not have a valid destination
	 * \retval 1 The message has a valid destination
	 */
	int (* const has_destination)(const struct ast_msg *msg);
};

/*!
 * \brief Register a \c ast_msg_handler
 * \since 12.5.0
 *
 * \param handler The handler to register
 *
 * \retval 0 Success
 * \retval non-zero Error
 */
int ast_msg_handler_register(const struct ast_msg_handler *handler);

/*!
 * \brief Unregister a \c ast_msg_handler
 * \since 12.5.0
 *
 * \param handler The handler to unregister
 *
 * \retval 0 Success
 * \retval non-zero Error
 */
int ast_msg_handler_unregister(const struct ast_msg_handler *handler);

/*!
 * \brief Allocate a message.
 *
 * Allocate a message for the purposes of passing it into the Asterisk core
 * to be routed through the dialplan.  If ast_msg_queue() is not called, this
 * message must be destroyed using ast_msg_destroy().  Otherwise, the message
 * core code will take care of it.
 *
 * \return A message object. This function will return NULL if an allocation
 *         error occurs.
 */
struct ast_msg *ast_msg_alloc(void);

/*!
 * \brief Destroy an ast_msg
 *
 * This should only be called on a message if it was not
 * passed on to ast_msg_queue().
 *
 * \retval NULL always.
 */
struct ast_msg *ast_msg_destroy(struct ast_msg *msg);

/*!
 * \brief Bump a msg's ref count
 */
struct ast_msg *ast_msg_ref(struct ast_msg *msg);

/*!
 * \brief Set the 'to' URI of a message
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_to(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set the 'from' URI of a message
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_from(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set the 'body' text of a message (in UTF-8)
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_body(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set the dialplan context for this message
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_context(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set the dialplan extension for this message
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_exten(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set the technology associated with this message
 *
 * \since 12.5.0
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_tech(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set the technology's endpoint associated with this message
 *
 * \since 12.5.0
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_msg_set_endpoint(struct ast_msg *msg, const char *fmt, ...);

/*!
 * \brief Set a variable on the message going to the dialplan.
 * \note Setting a variable that already exists overwrites the existing variable value
 *
 * \param msg
 * \param name Name of variable to set
 * \param value Value of variable to set
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_msg_set_var(struct ast_msg *msg, const char *name, const char *value);

/*!
 * \brief Set a variable on the message being sent to a message tech directly.
 * \note Setting a variable that already exists overwrites the existing variable value
 *
 * \param msg
 * \param name Name of variable to set
 * \param value Value of variable to set
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_msg_set_var_outbound(struct ast_msg *msg, const char *name, const char *value);

/*!
 * \brief Get the specified variable on the message
 * \note The return value is valid only as long as the ast_message is valid. Hold a reference
 *       to the message if you plan on storing the return value. Do re-set the same
 *       message var name while holding a pointer to the result of this function.
 *
 * \return The value associated with variable "name". NULL if variable not found.
 */
const char *ast_msg_get_var(struct ast_msg *msg, const char *name);

/*!
 * \brief Get the body of a message.
 * \note The return value is valid only as long as the ast_message is valid. Hold a reference
 *       to the message if you plan on storing the return value.
 *
 * \return The body of the messsage, encoded in UTF-8.
 */
const char *ast_msg_get_body(const struct ast_msg *msg);

/*!
 * \brief Retrieve the source of this message
 *
 * \since 12.5.0
 *
 * \param msg The message to get the soure from
 *
 * \return The source of the message
 * \retval NULL or empty string if the message has no source
 */
const char *ast_msg_get_from(const struct ast_msg *msg);

/*!
 * \brief Retrieve the destination of this message
 *
 * \since 12.5.0
 *
 * \param msg The message to get the destination from
 *
 * \return The destination of the message
 * \retval NULL or empty string if the message has no destination
 */
const char *ast_msg_get_to(const struct ast_msg *msg);

/*!
 * \brief Retrieve the technology associated with this message
 *
 * \since 12.5.0
 *
 * \param msg The message to get the technology from
 *
 * \return The technology of the message
 * \retval NULL or empty string if the message has no associated technology
 */
const char *ast_msg_get_tech(const struct ast_msg *msg);

/*!
 * \brief Retrieve the endpoint associated with this message
 *
 * \since 12.5.0
 *
 * \param msg The message to get the endpoint from
 *
 * \return The endpoint associated with the message
 * \retval NULL or empty string if the message has no associated endpoint
 */
const char *ast_msg_get_endpoint(const struct ast_msg *msg);

/*!
 * \brief Determine if a particular message has a destination via some handler
 *
 * \since 12.5.0
 *
 * \param msg The message to check
 *
 * \retval 0 if the message has no handler that can find a destination
 * \retval 1 if the message has a handler that can find a destination
 */
int ast_msg_has_destination(const struct ast_msg *msg);

/*!
 * \brief Queue a message for routing through the dialplan.
 *
 * Regardless of the return value of this function, this funciton will take
 * care of ensuring that the message object is properly destroyed when needed.
 *
 * \retval 0 message successfully queued
 * \retval non-zero failure, message not sent to dialplan
 */
int ast_msg_queue(struct ast_msg *msg);

/*!
 * \brief Send a msg directly to an endpoint.
 *
 * Regardless of the return value of this function, this funciton will take
 * care of ensuring that the message object is properly destroyed when needed.
 *
 * \retval 0 message successfully queued to be sent out
 * \retval non-zero failure, message not get sent out.
 */
int ast_msg_send(struct ast_msg *msg, const char *to, const char *from);

/*!
 * \brief Opaque iterator for msg variables
 */
struct ast_msg_var_iterator;

/*!
 * \brief Create a new message variable iterator
 * \param msg A message whose variables are to be iterated over
 *
 * \return An opaque pointer to the new iterator
 */
struct ast_msg_var_iterator *ast_msg_var_iterator_init(const struct ast_msg *msg);

/*!
 * \brief Get the next variable name and value that is set for sending outbound
 * \param msg The message with the variables
 * \param iter An iterator created with ast_msg_var_iterator_init
 * \param name A pointer to the name result pointer
 * \param value A pointer to the value result pointer
 *
 * \retval 0 No more entries
 * \retval 1 Valid entry
 */
int ast_msg_var_iterator_next(const struct ast_msg *msg, struct ast_msg_var_iterator *iter, const char **name, const char **value);

/*!
 * \brief Get the next variable name and value that was set on a received message
 * \param msg The message with the variables
 * \param iter An iterator created with ast_msg_var_iterator_init
 * \param name A pointer to the name result pointer
 * \param value A pointer to the value result pointer
 *
 * \retval 0 No more entries
 * \retval 1 Valid entry
 */
int ast_msg_var_iterator_next_received(const struct ast_msg *msg,
	struct ast_msg_var_iterator *iter, const char **name, const char **value);

/*!
 * \brief Destroy a message variable iterator
 * \param iter Iterator to be destroyed
 */
void ast_msg_var_iterator_destroy(struct ast_msg_var_iterator *iter);

/*!
 * \brief Unref a message var from inside an iterator loop
 */
void ast_msg_var_unref_current(struct ast_msg_var_iterator *iter);


/*! \defgroup ast_msg_data Enhanced Messaging
 * @{
 * \page Messaging Enhanced Messaging
 *
 * The basic messaging framework has a basic drawback... It can only pass
 * a text string through the core.  This causes several issues:
 * \li Only a content type of text/plain can be passed.
 * \li If a softmix bridge is used, the original sender identity is lost.
 *
 * The Enhanced Messaging framework allows attributes, such as "From", "To"
 * and "Content-Type" to be attached to the message by the incoming channel
 * tech which can then be used by the outgoing channel tech to construct
 * the appropriate technology-specific outgoing message.
 */

/*!
 * \brief Structure used to transport an enhanced message through the frame core
 * \since 13.22.0
 * \since 15.5.0
 */
struct ast_msg_data;

enum ast_msg_data_source_type {
	AST_MSG_DATA_SOURCE_TYPE_UNKNOWN = 0,
	AST_MSG_DATA_SOURCE_TYPE_T140,
	AST_MSG_DATA_SOURCE_TYPE_IN_DIALOG,
	AST_MSG_DATA_SOURCE_TYPE_OUT_OF_DIALOG,
	__AST_MSG_DATA_SOURCE_TYPE_LAST,
};

enum ast_msg_data_attribute_type {
	AST_MSG_DATA_ATTR_TO = 0,
	AST_MSG_DATA_ATTR_FROM,
	AST_MSG_DATA_ATTR_CONTENT_TYPE,
	AST_MSG_DATA_ATTR_BODY,
	__AST_MSG_DATA_ATTR_LAST,
};

struct ast_msg_data_attribute {
	enum ast_msg_data_attribute_type type;
	char *value;
};

/*!
 * \brief Allocates an ast_msg_data structure.
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param source The source type of the message
 * \param attributes A pointer to an array of ast_msg_data_attribute structures
 * \param count The number of elements in the array
 *
 * \return Pointer to msg structure or NULL on allocation failure.
 *         Caller must call ast_free when done.
 */
struct ast_msg_data *ast_msg_data_alloc(enum ast_msg_data_source_type source,
	struct ast_msg_data_attribute attributes[], size_t count);

/*!
 * \brief Allocates an ast_msg_data structure.
 * \since 13.35.0
 * \since 16.12.0
 * \since 17.6.0
 *
 * \param source_type The source type of the message
 * \param to Where the message is sent to
 * \param from Where the message is sent from
 * \param content_type Content type of the body
 * \param body The message body
 *
 * \return Pointer to msg structure or NULL on allocation failure.
 *         Caller must call ast_free when done.
 */
struct ast_msg_data *ast_msg_data_alloc2(enum ast_msg_data_source_type source_type,
	const char *to, const char *from, const char *content_type, const char *body);

/*!
 * \brief Clone an ast_msg_data structure
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param msg The message to clone
 *
 * \return New message structure or NULL if there was an allocation failure.
 *         Caller must call ast_free when done.
 */
struct ast_msg_data *ast_msg_data_dup(struct ast_msg_data *msg);

/*!
 * \brief Get length of the structure
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param msg Pointer to ast_msg_data structure
 *
 * \return The length of the structure itself plus the dynamically allocated attribute buffer.
 */
size_t ast_msg_data_get_length(struct ast_msg_data *msg);

/*!
 * \brief Get "source type" from ast_msg_data
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param msg Pointer to ast_msg_data structure
 *
 * \return The source type field.
 */
enum ast_msg_data_source_type ast_msg_data_get_source_type(struct ast_msg_data *msg);

/*!
 * \brief Get attribute from ast_msg_data
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param msg Pointer to ast_msg_data structure
 * \param attribute_type One of ast_msg_data_attribute_type
 *
 * \return The attribute or an empty string ("") if the attribute wasn't set.
 */
const char *ast_msg_data_get_attribute(struct ast_msg_data *msg,
	enum ast_msg_data_attribute_type attribute_type);

/*!
 * \brief Queue an AST_FRAME_TEXT_DATA frame containing an ast_msg_data structure
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param channel  The channel on which to queue the frame
 * \param msg Pointer to ast_msg_data structure
 *
 * \retval -1 Error
 * \retval  0 Success
 */
int ast_msg_data_queue_frame(struct ast_channel *channel, struct ast_msg_data *msg);

/*!
 *  @}
 */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* __AST_MESSAGE_H__ */
