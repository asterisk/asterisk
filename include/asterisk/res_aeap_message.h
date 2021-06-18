/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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
 * \brief Asterisk External Application Protocol Message API
 */

#ifndef AST_AEAP_MESSAGE_H
#define AST_AEAP_MESSAGE_H

#include <stdint.h>

#include "asterisk/res_aeap.h"

struct ast_aeap_message;

/*!
 * \brief Message type virtual method table
 */
struct ast_aeap_message_type {
	/*! The size of the message implementation type. Used for allocations. */
	size_t type_size;
	/*! The name of this type */
	const char *type_name;
	/*! The type to serialize to, and de-serialize from */
	enum AST_AEAP_DATA_TYPE serial_type;

	/*!
	 * \brief Construct/Initialize a message object
	 *
	 * \param self The message object to initialize
	 * \param params Other optional parameter(s) to possibly use
	 *
	 * \returns 0 on success, -1 on error
	 */
	int (*construct1)(struct ast_aeap_message *self, const void *params);

	/*!
	 * \brief Construct/Initialize a message object
	 *
	 * \param self The message object to initialize
	 * \param msg_type The type of message (e.g. request or response)
	 * \param name The name of the message
	 * \param id The message id
	 * \param params Other optional parameter(s) to possibly use
	 *
	 * \returns 0 on success, -1 on error
	 */
	int (*construct2)(struct ast_aeap_message *self, const char *msg_type, const char *name,
		const char *id, const void *params);

	/*!
	 * \brief Destruct/Cleanup object resources
	 *
	 * \param self The message object being destructed
	 */
	void (*destruct)(struct ast_aeap_message *self);

	/*!
	 * \brief Deserialize the given buffer into a message object
	 *
	 * \param self The message object to deserialize into
	 * \param buf The buffer to deserialize
	 * \param size The size/length of the buffer
	 *
	 * \returns 0 on success, -1 on error
	 */
	int (*deserialize)(struct ast_aeap_message *self, const void *buf, intmax_t size);

	/*!
	 * \brief Serialize the message object into byte/char buffer
	 *
	 * \param self The message object to serialize
	 * \param buf [out] The buffer to hold the "packed" data
	 * \param size [out] The size of the data written to the buffer
	 *
	 * \returns 0 on success, -1 on error
	 */
	int (*serialize)(const struct ast_aeap_message *self, void **buf, intmax_t *size);

	/*!
	 * \brief Retrieve a message id
	 *
	 * \param self The message object
	 *
	 * \returns The message id
	 */
	const char *(*id)(const struct ast_aeap_message *self);

	/*!
	 * \brief Set a message id.
	 *
	 * \param self The message object
	 * \param id The id to set
	 *
	 * \returns 0 on success, -1 on error
	 */
	int (*id_set)(struct ast_aeap_message *self, const char *id);

	/*!
	 * \brief Retrieve a message name
	 *
	 * \param self The message object
	 *
	 * \returns The message name
	 */
	const char *(*name)(const struct ast_aeap_message *self);

	/*!
	 * \brief Retrieve the core message data/body
	 *
	 * \param self This message object
	 */
	void *(*data)(struct ast_aeap_message *self);

	/*!
	 * \brief Retrieve whether or not this is a request message
	 *
	 * \param self The message object
	 *
	 * \returns True if message is a request, false otherwise
	 */
	int (*is_request)(const struct ast_aeap_message *self);

	/*!
	 * \brief Retrieve whether or not this is a response message
	 *
	 * \param self The message object
	 *
	 * \returns True if message is a response, false otherwise
	 */
	int (*is_response)(const struct ast_aeap_message *self);

	/*!
	 * \brief Retrieve the error message if it has one
	 *
	 * \param self The message object
	 *
	 * \returns The error message if available, or NULL
	 */
	const char *(*error_msg)(const struct ast_aeap_message *self);

	/*!
	 * \brief Set an error message
	 *
	 * \param self The message object
	 * \param error_msg The error message string to set
	 *
	 * \returns 0 on success, -1 on error
	 */
	int (*error_msg_set)(struct ast_aeap_message *self, const char *error_msg);
};

/*!
 * \brief Asterisk external application base message
 */
struct ast_aeap_message {
	/*! The type virtual table */
	const struct ast_aeap_message_type *type;
};

/*!
 * \brief Retrieve the serial type a message type
 *
 * \param type A message type
 *
 * \returns The type's serial type
 */
enum AST_AEAP_DATA_TYPE ast_aeap_message_serial_type(const struct ast_aeap_message_type *type);

/*!
 * \brief Create an Asterisk external application message object
 *
 * \param type The type of message object to create
 * \param params Any parameter(s) to pass to the type's constructor
 *
 * \returns An ao2 reference counted AEAP message object, or NULL on error
 */
struct ast_aeap_message *ast_aeap_message_create1(const struct ast_aeap_message_type *type,
	const void *params);

/*!
 * \brief Create an Asterisk external application message object
 *
 * \param type The type of message object to create
 * \param msg_type The type of message (e.g. request or response)
 * \param name The name of the message
 * \param id The message id
 * \param params Other optional parameter(s) to possibly use
 *
 * \returns An ao2 reference counted AEAP message object, or NULL on error
 */
struct ast_aeap_message *ast_aeap_message_create2(const struct ast_aeap_message_type *type,
	const char *msg_type, const char *name, const char *id, const void *params);

/*!
 * \brief Create an Asterisk external application request object
 *
 * \param type The type of message object to create
 * \param name The name of the message
 * \param id Optional id (if NULL an id is generated)
 * \param params Other optional parameter(s) to possibly use
 *
 * \returns An ao2 reference counted AEAP request object, or NULL on error
 */
struct ast_aeap_message *ast_aeap_message_create_request(const struct ast_aeap_message_type *type,
	const char *name, const char *id, const void *params);

/*!
 * \brief Create an Asterisk external application response object
 *
 * \param type The type of message object to create
 * \param name The name of the message
 * \param id Optional id
 * \param params Other optional parameter(s) to possibly use
 *
 * \returns An ao2 reference counted AEAP response object, or NULL on error
 */
struct ast_aeap_message *ast_aeap_message_create_response(const struct ast_aeap_message_type *type,
	const char *name, const char *id, const void *params);

/*!
 * \brief Create an Asterisk external application error response object
 *
 * \param type The type of message object to create
 * \param name The name of the message
 * \param id Optional id
 * \param error_msg Error message to set
 * \param params Other optional parameter(s) to possibly use
 *
 * \returns An ao2 reference counted AEAP response object, or NULL on error
 */
struct ast_aeap_message *ast_aeap_message_create_error(const struct ast_aeap_message_type *type,
	const char *name, const char *id, const char *error_msg);

/*!
 * \brief Deserialize the given buffer into an Asterisk external application message object
 *
 * \param type The message type to create, and deserialize to
 * \param buf The buffer to deserialize
 * \param size The size/length of the buffer
 *
 * \returns An ao2 reference counted AEAP message object, or NULL on error
 */
struct ast_aeap_message *ast_aeap_message_deserialize(const struct ast_aeap_message_type *type,
	const void *buf, intmax_t size);

/*!
 * \brief Serialize the given message object into a byte/char buffer
 *
 * \param message The message object to serialize
 * \param buf [out] The buffer to hold the "packed" data
 * \param size [out] The size of the data written to the buffer
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_message_serialize(const struct ast_aeap_message *message,
	void **buf, intmax_t *size);

/*!
 * \brief Retrieve a message id
 *
 * \param message A message object
 *
 * \returns The message id, or an empty string
 */
const char *ast_aeap_message_id(const struct ast_aeap_message *message);

/*!
 * \brief Set a message id.
 *
 * \param message A message object
 * \param id The id to set
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_message_id_set(struct ast_aeap_message *message, const char *id);

/*!
 * \brief Generate an id, and set it for the message
 *
 * \param message A message object
 *
 * \returns the generated id on success, or NULL on error
 */
const char *ast_aeap_message_id_generate(struct ast_aeap_message *message);

/*!
 * \brief Retrieve a message name
 *
 * \param message A message object
 *
 * \returns The message name, or an empty string
 */
const char *ast_aeap_message_name(const struct ast_aeap_message *message);

/*!
 * \brief Check whether or not a message's name matches the given one
 *
 * \note Case insensitive
 *
 * \param message A message object
 * \param message name The name to check against
 *
 * \returns True if matched, false otherwise
 */
int ast_aeap_message_is_named(const struct ast_aeap_message *message, const char *name);

/*!
 * \brief Retrieve the core message data/body
 *
 * \param message A message object
 */
void *ast_aeap_message_data(struct ast_aeap_message *message);

/*!
 * \brief Retrieve whether or not this is a request message
 *
 * \param message A message object
 *
 * \returns True if the message is a request, false otherwise
 */
int ast_aeap_message_is_request(const struct ast_aeap_message *message);

/*!
 * \brief Retrieve whether or not this is a response message
 *
 * \param message A message object
 *
 * \returns True if the message is a response, false otherwise
 */
int ast_aeap_message_is_response(const struct ast_aeap_message *message);

/*!
 * \brief Retrieve the error message if it has one
 *
 * \param message A message object
 *
 * \returns The error message if available, or NULL
 */
const char *ast_aeap_message_error_msg(const struct ast_aeap_message *message);

/*!
 * \brief Set an error message.
 *
 * \param message A message object
 * \param error_msg The error string to set
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_message_error_msg_set(struct ast_aeap_message *message,
	const char *error_msg);

/*!
 * \brief Asterisk external application JSON message type
 */
extern const struct ast_aeap_message_type *ast_aeap_message_type_json;

#endif /* AST_AEAP_MESSAGE_H */
