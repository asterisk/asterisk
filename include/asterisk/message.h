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
 * to be routed through the dialplan and potentially sent back out through
 * a message technology that has been registered through this API.
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
 * \return NULL, always.
 */
struct ast_msg *ast_msg_destroy(struct ast_msg *msg);

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
 * \brief Set a variable on the message
 * \note Setting a variable that already exists overwrites the existing variable value
 *
 * \param name Name of variable to set
 * \param value Value of variable to set
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_msg_set_var(struct ast_msg *msg, const char *name, const char *value);

/*!
 * \brief Get the specified variable on the message
 * \note The return value is valid only as long as the ast_message is valid. Hold a reference
 *       to the message if you plan on storing the return value. 
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
 * \param i An iterator created with ast_msg_var_iterator_init
 * \param name A pointer to the name result pointer
 * \param value A pointer to the value result pointer
 *
 * \retval 0 No more entries
 * \retval 1 Valid entry
 */
int ast_msg_var_iterator_next(const struct ast_msg *msg, struct ast_msg_var_iterator *i, const char **name, const char **value);

/*!
 * \brief Destroy a message variable iterator
 * \param i Iterator to be destroyed
 */
void ast_msg_var_iterator_destroy(struct ast_msg_var_iterator *i);

/*!
 * \brief Unref a message var from inside an iterator loop
 */
void ast_msg_var_unref_current(struct ast_msg_var_iterator *i);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* __AST_MESSAGE_H__ */
