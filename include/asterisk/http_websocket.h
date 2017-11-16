/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

#ifndef _ASTERISK_HTTP_WEBSOCKET_H
#define _ASTERISK_HTTP_WEBSOCKET_H

#include "asterisk/http.h"
#include "asterisk/optional_api.h"

#include <errno.h>

/*! \brief Default websocket write timeout, in ms */
#define AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT 100

/*! \brief Default websocket write timeout, in ms (as a string) */
#define AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT_STR "100"

/*!
 * \file http_websocket.h
 * \brief Support for WebSocket connections within the Asterisk HTTP server and client
 *        WebSocket connections to a server.
 *
 * Supported WebSocket versions in server implementation:
 *     Version 7 defined in specification http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-07
 *     Version 8 defined in specification http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-10
 *     Version 13 defined in specification http://tools.ietf.org/html/rfc6455
 * Supported WebSocket versions in client implementation:
 *     Version 13 defined in specification http://tools.ietf.org/html/rfc6455
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*! \brief WebSocket operation codes */
enum ast_websocket_opcode {
	AST_WEBSOCKET_OPCODE_TEXT = 0x1,         /*!< Text frame */
	AST_WEBSOCKET_OPCODE_BINARY = 0x2,       /*!< Binary frame */
	AST_WEBSOCKET_OPCODE_PING = 0x9,         /*!< Request that the other side respond with a pong */
	AST_WEBSOCKET_OPCODE_PONG = 0xA,         /*!< Response to a ping */
	AST_WEBSOCKET_OPCODE_CLOSE = 0x8,        /*!< Connection is being closed */
	AST_WEBSOCKET_OPCODE_CONTINUATION = 0x0, /*!< Continuation of a previous frame */
};

/*!
 * \brief Opaque structure for WebSocket server.
 * \since 12
 */
struct ast_websocket_server;

/*!
 * \brief Opaque structure for WebSocket sessions.
 */
struct ast_websocket;

/*!
 * \brief Callback from the HTTP request attempting to establish a websocket connection
 *
 * This callback occurs when an HTTP request is made to establish a websocket connection.
 * Implementers of \ref ast_websocket_protocol can use this to deny a request, or to
 * set up application specific data before invocation of \ref ast_websocket_callback.
 *
 * \param ser The TCP/TLS session
 * \param parameters Parameters extracted from the request URI
 * \param headers Headers included in the request
 * \param session_id The id of the current session.
 *
 * \retval 0 The session should be accepted
 * \retval -1 The session should be rejected. Note that the caller must send an error
 * response using \ref ast_http_error.
 * \since 13.5.0
 */
typedef int (*ast_websocket_pre_callback)(struct ast_tcptls_session_instance *ser, struct ast_variable *parameters, struct ast_variable *headers, const char *session_id);

/*!
 * \brief Callback for when a new connection for a sub-protocol is established
 *
 * \param session A WebSocket session structure
 * \param parameters Parameters extracted from the request URI
 * \param headers Headers included in the request
 *
 * \note Once called the ownership of the session is transferred to the sub-protocol handler. It
 *       is responsible for closing and cleaning up.
 *
 */
typedef void (*ast_websocket_callback)(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers);

/*!
 * \brief A websocket protocol implementation
 *
 * Users of the Websocket API can register themselves as a websocket
 * protocol. See \ref ast_websocket_add_protocol2 and \ref ast_websocket_server_add_protocol2.
 * Simpler implementations may use only \ref ast_websocket_add_protocol and
 * \ref ast_websocket_server_add_protocol.
 *
 * \since 13.5.0
 */
struct ast_websocket_protocol {
	/*! \brief Name of the protocol */
	char *name;
/*!
 * \brief Protocol version. This prevents dynamically loadable modules from registering
 * if this struct is changed.
 */
#define AST_WEBSOCKET_PROTOCOL_VERSION 1
	/*! \brief Protocol version. Should be set to /ref AST_WEBSOCKET_PROTOCOL_VERSION */
	unsigned int version;
	/*! \brief Callback called when a new session is attempted. Optional. */
	ast_websocket_pre_callback session_attempted;
	/* \brief Callback called when a new session is established. Mandatory. */
	ast_websocket_callback session_established;
};

/*!
 * \brief Creates a \ref websocket_server
 *
 * \retval New \ref websocket_server instance
 * \retval \c NULL on error
 * \since 12
 */
AST_OPTIONAL_API(struct ast_websocket_server *, ast_websocket_server_create, (void), { return NULL; });

/*!
 * \brief Callback suitable for use with a \ref ast_http_uri.
 *
 * Set the data field of the ast_http_uri to \ref ast_websocket_server.
 * \since 12
 */
AST_OPTIONAL_API(int, ast_websocket_uri_cb, (struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers), { return -1; });

/*!
 * \brief Allocate a websocket sub-protocol instance
 *
 * \retval An instance of \ref ast_websocket_protocol on success
 * \retval NULL on error
 * \since 13.5.0
 */
AST_OPTIONAL_API(struct ast_websocket_protocol *, ast_websocket_sub_protocol_alloc, (const char *name), {return NULL;});

/*!
 * \brief Add a sub-protocol handler to the default /ws server
 *
 * \param name Name of the sub-protocol to register
 * \param callback Callback called when a new connection requesting the sub-protocol is established
 *
 * \retval 0 success
 * \retval -1 if sub-protocol handler could not be registered
 */
AST_OPTIONAL_API(int, ast_websocket_add_protocol, (const char *name, ast_websocket_callback callback), {return -1;});

/*!
 * \brief Add a sub-protocol handler to the default /ws server
 *
 * \param protocol The sub-protocol to register. Note that this must
 * be allocated using /ref ast_websocket_sub_protocol_alloc.
 *
 * \note This method is reference stealing. It will steal the reference to \ref protocol
 * on success.
 *
 * \retval 0 success
 * \retval -1 if sub-protocol handler could not be registered
 * \since 13.5.0
 */
AST_OPTIONAL_API(int, ast_websocket_add_protocol2, (struct ast_websocket_protocol *protocol), {return -1;});

/*!
 * \brief Remove a sub-protocol handler from the default /ws server.
 *
 * \param name Name of the sub-protocol to unregister
 * \param callback Session Established callback that was previously registered with the sub-protocol
 *
 * \retval 0 success
 * \retval -1 if sub-protocol was not found or if callback did not match
 */
AST_OPTIONAL_API(int, ast_websocket_remove_protocol, (const char *name, ast_websocket_callback callback), {return -1;});

/*!
 * \brief Add a sub-protocol handler to the given server.
 *
 * \param name Name of the sub-protocol to register
 * \param callback Callback called when a new connection requesting the sub-protocol is established
 *
 * \retval 0 success
 * \retval -1 if sub-protocol handler could not be registered
 * \since 12
 */
AST_OPTIONAL_API(int, ast_websocket_server_add_protocol, (struct ast_websocket_server *server, const char *name, ast_websocket_callback callback), {return -1;});

/*!
 * \brief Add a sub-protocol handler to the given server.
 *
 * \param server The server to add the sub-protocol to.
 * \param protocol The sub-protocol to register. Note that this must
 * be allocated using /ref ast_websocket_sub_protocol_alloc.
 *
 * \note This method is reference stealing. It will steal the reference to \ref protocol
 * on success.
 *
 * \retval 0 success
 * \retval -1 if sub-protocol handler could not be registered
 * \since 13.5.0
 */
AST_OPTIONAL_API(int, ast_websocket_server_add_protocol2, (struct ast_websocket_server *server, struct ast_websocket_protocol *protocol), {return -1;});

/*!
 * \brief Remove a sub-protocol handler from the given server.
 *
 * \param name Name of the sub-protocol to unregister
 * \param callback Callback that was previously registered with the sub-protocol
 *
 * \retval 0 success
 * \retval -1 if sub-protocol was not found or if callback did not match
 * \since 12
 */
AST_OPTIONAL_API(int, ast_websocket_server_remove_protocol, (struct ast_websocket_server *server, const char *name, ast_websocket_callback callback), {return -1;});

/*!
 * \brief Read a WebSocket frame and handle it
 *
 * \param session Pointer to the WebSocket session
 * \param payload Pointer to a char* which will be populated with a pointer to the payload if present
 * \param payload_len Pointer to a uint64_t which will be populated with the length of the payload if present
 * \param opcode Pointer to an enum which will be populated with the opcode of the frame
 * \param fragmented Pointer to an int which is set to 1 if payload is fragmented and 0 if not
 *
 * \retval -1 on error
 * \retval 0 on success
 *
 * \note Once an AST_WEBSOCKET_OPCODE_CLOSE opcode is received the socket will be closed
 */
AST_OPTIONAL_API(int, ast_websocket_read, (struct ast_websocket *session, char **payload, uint64_t *payload_len, enum ast_websocket_opcode *opcode, int *fragmented), { errno = ENOSYS; return -1;});

/*!
 * \brief Read a WebSocket frame containing string data.
 *
 * \note The caller is responsible for freeing the output "buf".
 *
 * \param ws pointer to the websocket
 * \param buf string buffer to populate with data read from socket
 * \retval -1 on error
 * \retval number of bytes read on success
 *
 * \note Once an AST_WEBSOCKET_OPCODE_CLOSE opcode is received the socket will be closed
 */
AST_OPTIONAL_API(int, ast_websocket_read_string,
		 (struct ast_websocket *ws, char **buf),
		 { errno = ENOSYS; return -1;});

/*!
 * \brief Construct and transmit a WebSocket frame
 *
 * \param session Pointer to the WebSocket session
 * \param opcode WebSocket operation code to place in the frame
 * \param payload Optional pointer to a payload to add to the frame
 * \param payload_size Length of the payload (0 if no payload)
 *
 * \retval 0 if successfully written
 * \retval -1 if error occurred
 */
AST_OPTIONAL_API(int, ast_websocket_write, (struct ast_websocket *session, enum ast_websocket_opcode opcode, char *payload, uint64_t payload_size), { errno = ENOSYS; return -1;});

/*!
 * \brief Construct and transmit a WebSocket frame containing string data.
 *
 * \param ws pointer to the websocket
 * \param buf string data to write to socket
 * \retval 0 if successfully written
 * \retval -1 if error occurred
 */
AST_OPTIONAL_API(int, ast_websocket_write_string,
		 (struct ast_websocket *ws, const char *buf),
		 { errno = ENOSYS; return -1;});
/*!
 * \brief Close a WebSocket session by sending a message with the CLOSE opcode and an optional code
 *
 * \param session Pointer to the WebSocket session
 * \param reason Reason code for closing the session as defined in the RFC
 *
 * \retval 0 if successfully written
 * \retval -1 if error occurred
 */
AST_OPTIONAL_API(int, ast_websocket_close, (struct ast_websocket *session, uint16_t reason), { errno = ENOSYS; return -1;});

/*!
 * \brief Enable multi-frame reconstruction up to a certain number of bytes
 *
 * \param session Pointer to the WebSocket session
 * \param bytes If a reconstructed payload exceeds the specified number of bytes the payload will be returned
 *              and upon reception of the next multi-frame a new reconstructed payload will begin.
 */
AST_OPTIONAL_API(void, ast_websocket_reconstruct_enable, (struct ast_websocket *session, size_t bytes), {return;});

/*!
 * \brief Disable multi-frame reconstruction
 *
 * \param session Pointer to the WebSocket session
 *
 * \note If reconstruction is disabled each message that is part of a multi-frame message will be sent up to
 *       the user when ast_websocket_read is called.
 */
AST_OPTIONAL_API(void, ast_websocket_reconstruct_disable, (struct ast_websocket *session), {return;});

/*!
 * \brief Increase the reference count for a WebSocket session
 *
 * \param session Pointer to the WebSocket session
 */
AST_OPTIONAL_API(void, ast_websocket_ref, (struct ast_websocket *session), {return;});

/*!
 * \brief Decrease the reference count for a WebSocket session
 *
 * \param session Pointer to the WebSocket session
 */
AST_OPTIONAL_API(void, ast_websocket_unref, (struct ast_websocket *session), {return;});

/*!
 * \brief Get the file descriptor for a WebSocket session.
 *
 * \retval file descriptor
 *
 * \note You must *not* directly read from or write to this file descriptor. It should only be used for polling.
 */
AST_OPTIONAL_API(int, ast_websocket_fd, (struct ast_websocket *session), { errno = ENOSYS; return -1;});

/*!
 * \brief Get the remote address for a WebSocket connected session.
 *
 * \retval ast_sockaddr Remote address
 */
AST_OPTIONAL_API(struct ast_sockaddr *, ast_websocket_remote_address, (struct ast_websocket *session), {return NULL;});

/*!
 * \brief Get the local address for a WebSocket connection session.
 *
 * \retval ast_sockaddr Local address
 *
 * \since 13.19.0
 */
AST_OPTIONAL_API(struct ast_sockaddr *, ast_websocket_local_address, (struct ast_websocket *session), {return NULL;});

/*!
 * \brief Get whether the WebSocket session is using a secure transport or not.
 *
 * \retval 0 if unsecure
 * \retval 1 if secure
 */
AST_OPTIONAL_API(int, ast_websocket_is_secure, (struct ast_websocket *session), { errno = ENOSYS; return -1;});

/*!
 * \brief Set the socket of a WebSocket session to be non-blocking.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
AST_OPTIONAL_API(int, ast_websocket_set_nonblock, (struct ast_websocket *session), { errno = ENOSYS; return -1;});

/*!
 * \brief Get the session ID for a WebSocket session.
 *
 * \retval session id
 */
AST_OPTIONAL_API(const char *, ast_websocket_session_id, (struct ast_websocket *session), { errno = ENOSYS; return NULL;});

/*!
 * \brief Result code for a websocket client.
 */
enum ast_websocket_result {
	WS_OK,
	WS_ALLOCATE_ERROR,
	WS_KEY_ERROR,
	WS_URI_PARSE_ERROR,
	WS_URI_RESOLVE_ERROR,
	WS_BAD_STATUS,
	WS_INVALID_RESPONSE,
	WS_BAD_REQUEST,
	WS_URL_NOT_FOUND,
	WS_HEADER_MISMATCH,
	WS_HEADER_MISSING,
	WS_NOT_SUPPORTED,
	WS_WRITE_ERROR,
	WS_CLIENT_START_ERROR,
};

/*!
 * \brief Create, and connect, a websocket client.
 *
 * \detail If the client websocket successfully connects, then the accepted protocol
 *         can be checked via a call to ast_websocket_client_accept_protocol.
 *
 * \note While connecting this *will* block until a response is
 *       received from the remote host.
 * \note Expected uri form: ws[s]://<address>[:port][/<path>] The address (can be a
 *       host name) and port are parsed out and used to connect to the remote server.
 *       If multiple IPs are returned during address resolution then the first one is
 *       chosen.
 *
 * \param uri uri to connect to
 * \param protocols a comma separated string of supported protocols
 * \param tls_cfg secure websocket credentials
 * \param result result code set on client failure
 * \retval a client websocket.
 * \retval NULL if object could not be created or connected
 * \since 13
 */
AST_OPTIONAL_API(struct ast_websocket *, ast_websocket_client_create,
		 (const char *uri, const char *protocols,
		  struct ast_tls_config *tls_cfg,
		  enum ast_websocket_result *result), { return NULL;});

/*!
 * \brief Retrieve the server accepted sub-protocol on the client.
 *
 * \param ws the websocket client
 * \retval the accepted client sub-protocol.
 * \since 13
 */
AST_OPTIONAL_API(const char *, ast_websocket_client_accept_protocol,
		 (struct ast_websocket *ws), { return NULL;});

/*!
 * \brief Set the timeout on a non-blocking WebSocket session.
 *
 * \since 11.11.0
 * \since 12.4.0
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
AST_OPTIONAL_API(int, ast_websocket_set_timeout, (struct ast_websocket *session, int timeout), {return -1;});

#endif
