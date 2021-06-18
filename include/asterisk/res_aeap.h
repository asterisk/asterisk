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
 * \brief Asterisk External Application Protocol API
 */

#ifndef AST_RES_AEAP_H
#define AST_RES_AEAP_H

#include <stdint.h>

struct ao2_container;
struct ast_sorcery;
struct ast_variable;

struct ast_aeap_client_config;
struct ast_aeap_message;

#define AEAP_CONFIG_CLIENT "client"

/*!
 * \brief Retrieve the AEAP sorcery object
 *
 * \returns the AEAP sorcery object
 */
struct ast_sorcery *ast_aeap_sorcery(void);

/*!
 * \brief Retrieve a listing of all client configuration objects by protocol.
 *
 * \note Caller is responsible for the returned container's reference.
 *
 * \param protocol An optional protocol to filter on (if NULL returns all client configs)
 *
 * \returns A container of client configuration objects
 */
struct ao2_container *ast_aeap_client_configs_get(const char *protocol);

/*!
 * \brief Retrieve codec capabilities from the configuration
 *
 * \param config A configuration object
 *
 * \returns The configuration's codec capabilities
 */
const struct ast_format_cap *ast_aeap_client_config_codecs(const struct ast_aeap_client_config *cfg);

/*!
 * \brief Check a given protocol against that in an Asterisk external application configuration
 *
 * \param config A configuration object
 * \param protocol The protocol to check
 *
 * \returns True if the configuration's protocol matches, false otherwise
 */
int ast_aeap_client_config_has_protocol(const struct ast_aeap_client_config *cfg,
	const char *protocol);

/*!
 * \brief Retrieve a list of custom configuration fields
 *
 * \param id configuration id/sorcery lookup key
 *
 * \returns variables, or NULL on error
 */
struct ast_variable *ast_aeap_custom_fields_get(const char *id);

/*!
 * \brief An Asterisk external application object
 *
 * Connects to an external application, sending and receiving data, and
 * dispatches received data to registered handlers.
 */
struct ast_aeap;

/*!
 * \brief Event raised when a message is received
 *
 * \param aeap An Asterisk external application object
 * \param message The received message
 * \param obj Associated user object
 *
 * \returns 0 on if message handled, otherwise non-zero
 */
typedef int (*ast_aeap_on_message)(struct ast_aeap *aeap, struct ast_aeap_message *message, void *obj);

/*!
 * \brief An Asterisk external application message handler
 *
 * Used to register message handlers with an AEAP object.
 */
struct ast_aeap_message_handler {
	/*! The handler name */
	const char *name;
	/*! Callback triggered when on a name match */
	ast_aeap_on_message on_message;
};

/*!
 * \brief Event raised when a sent message does not receive a reply within
 *        a specified time interval
 *
 * \param aeap An Asterisk external application object
 * \param message The message sent that received no response
 * \param obj Associated user object
 */
typedef void (*ast_aeap_on_timeout)(struct ast_aeap *aeap, struct ast_aeap_message *message, void *obj);

/*!
 * \brief Callback to cleanup a user object
 *
 * \param obj The user object
 */
typedef void (*ast_aeap_user_obj_cleanup)(void *obj);

/*!
 * \brief Supported Asterisk external application data types
 */
enum AST_AEAP_DATA_TYPE {
	AST_AEAP_DATA_TYPE_NONE,
	AST_AEAP_DATA_TYPE_BINARY,
	AST_AEAP_DATA_TYPE_STRING,
};

/*!
 * \brief Callbacks and other parameters used by an Asterisk external application object
 */
struct ast_aeap_params {
	/*!
	 * If true pass along error messages to the implementation.
	 * Otherwise log it only, and consider it handled.
	 */
	unsigned int emit_error;

	/*! The message type used for communication */
	const struct ast_aeap_message_type *msg_type;

	/*! Response handlers array */
	const struct ast_aeap_message_handler *response_handlers;
	/*! The number of response handlers */
	uintmax_t response_handlers_size;

	/*! Request handlers array */
	const struct ast_aeap_message_handler *request_handlers;
	/*! The number of request handlers */
	uintmax_t request_handlers_size;

	/*!
	 * \brief Raised when binary data is received
	 *
	 * \param aeap An Asterisk external application object
	 * \param buf The buffer containing binary data
	 * \param size The size of the buffer
	 */
	void (*on_binary)(struct ast_aeap *aeap, const void *buf, intmax_t size);

	/*!
	 * \brief Raised when string data is received
	 *
	 * \param aeap An Asterisk external application object
	 * \param buf The buffer containing string data
	 * \param size The size/length of the string
	 */
	void (*on_string)(struct ast_aeap *aeap, const char *buf, intmax_t size);

	/*!
	 * \brief Raised when an error occurs during reading
	 *
	 * \note This is an AEAP transport level read error event
	 *
	 * \note When this event is triggered the client has also
	 *       been disconnected.
	 *
	 * \param aeap An Asterisk external application object
	 */
	void (*on_error)(struct ast_aeap *aeap);
};

/*!
 * \brief Create an Asterisk external application object
 *
 * \param type The type of underlying transport
 * \param params Callbacks and other parameters to use
 *
 * \returns A new ao2 reference counted aeap object, or NULL on error
 */
struct ast_aeap *ast_aeap_create(const char *type, const struct ast_aeap_params *params);

/*!
 * \brief Create an Asterisk external application object by sorcery id
 *
 * \param id The sorcery id to lookup
 * \param params Callbacks and other parameters to use
 *
 * \returns A new ao2 reference counted aeap object, or NULL on error
 */
struct ast_aeap *ast_aeap_create_by_id(const char *id, const struct ast_aeap_params *params);

/*!
 * \brief Connect to an external application
 *
 * \param aeap An Asterisk external application object
 * \param url The url to connect to
 * \param protocol A protocol to use
 * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
 *
 * \returns 0 if able to connect, -1 on error
 */
int ast_aeap_connect(struct ast_aeap *aeap, const char *url, const char *protocol, int timeout);

/*!
 * \brief Create and connect to an Asterisk external application by sorcery id
 *
 * \param id The sorcery id to lookup
 * \param params Callbacks and other parameters to use
 * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
 *
 * \returns A new ao2 reference counted aeap object, or NULL on error
 */
struct ast_aeap *ast_aeap_create_and_connect_by_id(const char *id,
	const struct ast_aeap_params *params, int timeout);

/*!
 * \brief Create and connect to an Asterisk external application
 *
 * \param type The type of client connection to make
 * \param params Callbacks and other parameters to use
 * \param url The url to connect to
 * \param protocol A protocol to use
 * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
 *
 * \returns A new ao2 reference counted aeap object, or NULL on error
 */
struct ast_aeap *ast_aeap_create_and_connect(const char *type,
	const struct ast_aeap_params *params, const char *url, const char *protocol, int timeout);

/*!
 * \brief Disconnect an Asterisk external application object
 *
 * \note Depending on the underlying transport this call may block
 *
 * \param aeap An Asterisk external application object
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_disconnect(struct ast_aeap *aeap);

/*!
 * \brief Register a user data object
 *
 * \note The "cleanup" is called on un-register, if one is specified
 *
 * \param aeap An Asterisk external application object
 * \param id The look up id for the object
 * \param obj The user object to register
 * \param cleanup Optional user object clean up callback
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_user_data_register(struct ast_aeap *aeap, const char *id, void *obj,
	ast_aeap_user_obj_cleanup cleanup);

/*!
 * \brief Un-register a user data object
 *
 * \note If specified on register, the "cleanup" callback is called during unregister.
 *
 * \param aeap An Asterisk external application object
 * \param id The look up id for the object
 */
void ast_aeap_user_data_unregister(struct ast_aeap *aeap, const char *id);

/*!
 * \brief Retrieve a registered user data object by its id
 *
 * \note Depending on how it was registered the returned user data object's lifetime
 *       may be managed by the given "aeap" object. If it was registered with a cleanup
 *       handler that [potentially] frees it the caller of this function must ensure
 *       it's done using the returned object before it's unregistered.
 *
 * \param data A user data object
 *
 * \returns A user data object
 */
void *ast_aeap_user_data_object_by_id(struct ast_aeap *aeap, const char *id);

/*!
 * \brief Send a binary data to an external application
 *
 * \param aeap An Asterisk external application object
 * \param buf Binary data to send
 * \param size The size of the binary data
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_send_binary(struct ast_aeap *aeap, const void *buf, uintmax_t size);

/*!
 * \brief Send a message to an external application
 *
 * \note "Steals" the given message reference, thus callers are not required to un-ref
 *       the message object after calling this function.
 *
 * \param aeap An Asterisk external application object
 * \param msg The message to send
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_send_msg(struct ast_aeap *aeap, struct ast_aeap_message *msg);

/*!
 * \brief Parameters to be used when sending a transaction based message
 */
struct ast_aeap_tsx_params {
	/*! The message to send */
	struct ast_aeap_message *msg;
	/*! The amount of time (in milliseconds) to wait for a received message */
	int timeout;
	/*! Optional callback raised when no message is received in an allotted time */
	ast_aeap_on_timeout on_timeout;
	/*! Whether or not to block the current thread, and wait for a received message */
	int wait;
	/*!
	 * Optional user object to pass to handlers. User is responsible for object's lifetime
	 * unless an obj_cleanup callback is specified that handles its cleanup (e.g. freeing
	 * of memory).
	 */
	void *obj;
	/*!
	 * Optional user object cleanup callback. If specified, called upon "this" param's
	 * destruction (including on error).
	 */
	ast_aeap_user_obj_cleanup obj_cleanup;
};

/*!
 * \brief Send a transaction based message to an external application using the given parameters
 *
 * \note "Steals" the given message reference, thus callers are not required to un-ref
 *       the message object after calling this function.
 *
 * \note Also handles cleaning up the user object if the obj_cleanup callback
 *       is specified in "params".
 *
 * \param aeap An Asterisk external application object
 * \param msg The message to send
 * \param params (optional) Additional parameters to consider when sending. Heap allocation
 *     not required.
 *
 * \returns 0 on success, -1 on error
 */
int ast_aeap_send_msg_tsx(struct ast_aeap *aeap, struct ast_aeap_tsx_params *params);

#endif /* AST_RES_AEAP_H */
