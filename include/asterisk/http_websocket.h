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

#include "asterisk/optional_api.h"

/*!
 * \file http_websocket.h
 * \brief Support for WebSocket connections within the Asterisk HTTP server.
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
 * \brief Opaque structure for WebSocket sessions
 */
struct ast_websocket;

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
 * \brief Add a sub-protocol handler to the server
 *
 * \param name Name of the sub-protocol to register
 * \param callback Callback called when a new connection requesting the sub-protocol is established
 *
 * \retval 0 success
 * \retval -1 if sub-protocol handler could not be registered
 */
AST_OPTIONAL_API(int, ast_websocket_add_protocol, (const char *name, ast_websocket_callback callback), {return -1;});

/*!
 * \brief Remove a sub-protocol handler from the server
 *
 * \param name Name of the sub-protocol to unregister
 * \param callback Callback that was previously registered with the sub-protocol
 *
 * \retval 0 success
 * \retval -1 if sub-protocol was not found or if callback did not match
 */
AST_OPTIONAL_API(int, ast_websocket_remove_protocol, (const char *name, ast_websocket_callback callback), {return -1;});

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
AST_OPTIONAL_API(int, ast_websocket_read, (struct ast_websocket *session, char **payload, uint64_t *payload_len, enum ast_websocket_opcode *opcode, int *fragmented), {return -1;});

/*!
 * \brief Construct and transmit a WebSocket frame
 *
 * \param session Pointer to the WebSocket session
 * \param opcode WebSocket operation code to place in the frame
 * \param payload Optional pointer to a payload to add to the frame
 * \param actual_length Length of the payload (0 if no payload)
 *
 * \retval 0 if successfully written
 * \retval -1 if error occurred
 */
AST_OPTIONAL_API(int, ast_websocket_write, (struct ast_websocket *session, enum ast_websocket_opcode opcode, char *payload, uint64_t actual_length), {return -1;});

/*!
 * \brief Close a WebSocket session by sending a message with the CLOSE opcode and an optional code
 *
 * \param session Pointer to the WebSocket session
 * \param reason Reason code for closing the session as defined in the RFC
 *
 * \retval 0 if successfully written
 * \retval -1 if error occurred
 */
AST_OPTIONAL_API(int, ast_websocket_close, (struct ast_websocket *session, uint16_t reason), {return -1;});

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
AST_OPTIONAL_API(int, ast_websocket_fd, (struct ast_websocket *session), {return -1;});

/*!
 * \brief Get the remote address for a WebSocket connected session.
 *
 * \retval ast_sockaddr Remote address
 */
AST_OPTIONAL_API(struct ast_sockaddr *, ast_websocket_remote_address, (struct ast_websocket *session), {return NULL;});

/*!
 * \brief Get whether the WebSocket session is using a secure transport or not.
 *
 * \retval 0 if unsecure
 * \retval 1 if secure
 */
AST_OPTIONAL_API(int, ast_websocket_is_secure, (struct ast_websocket *session), {return -1;});

/*!
 * \brief Set the socket of a WebSocket session to be non-blocking.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
AST_OPTIONAL_API(int, ast_websocket_set_nonblock, (struct ast_websocket *session), {return -1;});

#endif
