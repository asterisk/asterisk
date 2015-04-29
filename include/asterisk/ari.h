/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef _ASTERISK_ARI_H
#define _ASTERISK_ARI_H

/*! \file
 *
 * \brief Asterisk RESTful API hooks.
 *
 * This header file is used mostly as glue code between generated declarations
 * and res_ari.c.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk/http.h"
#include "asterisk/json.h"

/* Forward-declare websocket structs. This avoids including http_websocket.h,
 * which causes optional_api stuff to happen, which makes optional_api more
 * difficult to debug. */

struct ast_websocket_server;

struct ast_websocket;

/*!
 * \brief Configured encoding format for JSON output.
 * \return JSON output encoding (compact, pretty, etc.)
 */
enum ast_json_encoding_format ast_ari_json_format(void);

struct ast_ari_response;

/*!
 * \brief Callback type for RESTful method handlers.
 * \param ser TCP/TLS session object
 * \param get_params GET parameters from the HTTP request.
 * \param path_vars Path variables from any wildcard path segments.
 * \param headers HTTP headers from the HTTP requiest.
 * \param[out] response The RESTful response.
 */
typedef void (*stasis_rest_callback)(
	struct ast_tcptls_session_instance *ser,
	struct ast_variable *get_params, struct ast_variable *path_vars,
	struct ast_variable *headers, struct ast_ari_response *response);

/*!
 * \brief Handler for a single RESTful path segment.
 */
struct stasis_rest_handlers {
	/*! Path segement to handle */
	const char *path_segment;
	/*! If true (non-zero), path_segment is a wildcard, and will match all
	 * values.
	 *
	 * Value of the segement will be passed into the \a path_vars parameter
	 * of the callback.
	 */
	int is_wildcard;
	/*! Callbacks for all handled HTTP methods. */
	stasis_rest_callback callbacks[AST_HTTP_MAX_METHOD];
	/*! WebSocket server for handling WebSocket upgrades. */
	struct ast_websocket_server *ws_server;
	/*! Number of children in the children array */
	size_t num_children;
	/*! Handlers for sub-paths */
	struct stasis_rest_handlers *children[];
};

/*!
 * Response type for RESTful requests
 */
struct ast_ari_response {
	/*! Response message */
	struct ast_json *message;
	/*! \r\n seperated response headers */
	struct ast_str *headers;
	/*! HTTP response code.
	 * See http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html */
	int response_code;
	/*! Corresponding text for the response code */
	const char *response_text; /* Shouldn't http.c handle this? */
	/*! Flag to indicate that no further response is needed */
	int no_response:1;
};

/*!
 * Add a resource for REST handling.
 * \param handler Handler to add.
 * \return 0 on success.
 * \return non-zero on failure.
 */
#define ast_ari_add_handler(handler) __ast_ari_add_handler(handler, AST_MODULE_SELF)
int __ast_ari_add_handler(struct stasis_rest_handlers *handler, struct ast_module *module);

/*!
 * Remove a resource for REST handling.
 * \param handler Handler to add.
 * \return 0 on success.
 * \return non-zero on failure.
 */
int ast_ari_remove_handler(struct stasis_rest_handlers *handler);

/*!
 * \internal
 * \brief Stasis RESTful invocation handler.
 *
 * Only call from res_ari and test_ari. Only public to allow
 * for unit testing.
 *
 * \param ser TCP/TLS connection.
 * \param uri HTTP URI, relative to the API path.
 * \param method HTTP method.
 * \param get_params HTTP \c GET parameters.
 * \param headers HTTP headers.
 * \param[out] response RESTful HTTP response.
 */
void ast_ari_invoke(struct ast_tcptls_session_instance *ser,
	const char *uri, enum ast_http_method method,
	struct ast_variable *get_params, struct ast_variable *headers,
	struct ast_ari_response *response);

/*!
 * \internal
 * \brief Service function for API declarations.
 *
 * Only call from res_ari and test_ari. Only public to allow
 * for unit testing.
 *
 * \param uri Requested URI, relative to the docs path.
 * \param headers HTTP headers.
 * \param[out] response RESTful HTTP response.
 */
void ast_ari_get_docs(const char *uri, struct ast_variable *headers, struct ast_ari_response *response);

/*! \brief Abstraction for reading/writing JSON to a WebSocket */
struct ast_ari_websocket_session;

/*!
 * \brief Create an ARI WebSocket session.
 *
 * If \c NULL is given for the validator function, no validation will be
 * performed.
 *
 * \param ws_session Underlying WebSocket session.
 * \param validator Function to validate outgoing messages.
 * \return New ARI WebSocket session.
 * \return \c NULL on error.
 */
struct ast_ari_websocket_session *ast_ari_websocket_session_create(
	struct ast_websocket *ws_session, int (*validator)(struct ast_json *));

/*!
 * \brief Read a message from an ARI WebSocket.
 *
 * \param session Session to read from.
 * \return Message received.
 * \return \c NULL if WebSocket could not be read.
 */
struct ast_json *ast_ari_websocket_session_read(
	struct ast_ari_websocket_session *session);

/*!
 * \brief Send a message to an ARI WebSocket.
 *
 * \param session Session to write to.
 * \param message Message to send.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int ast_ari_websocket_session_write(struct ast_ari_websocket_session *session,
	struct ast_json *message);

/*!
 * \brief The stock message to return when out of memory.
 *
 * The refcount is NOT bumped on this object, so ast_json_ref() if you want to
 * keep the reference.
 *
 * \return JSON message specifying an out-of-memory error.
 */
struct ast_json *ast_ari_oom_json(void);

/*!
 * \brief Fill in an error \a ast_ari_response.
 * \param response Response to fill in.
 * \param response_code HTTP response code.
 * \param response_text Text corresponding to the HTTP response code.
 * \param message_fmt Error message format string.
 */
void ast_ari_response_error(struct ast_ari_response *response,
				int response_code,
				const char *response_text,
				const char *message_fmt, ...)
__attribute__((format(printf, 4, 5)));

/*!
 * \brief Fill in an \c OK (200) \a ast_ari_response.
 * \param response Response to fill in.
 * \param message JSON response.  This reference is stolen, so just \ref
 *                ast_json_ref if you need to keep a reference to it.
 */
void ast_ari_response_ok(struct ast_ari_response *response,
			     struct ast_json *message);

/*!
 * \brief Fill in a <tt>No Content</tt> (204) \a ast_ari_response.
 */
void ast_ari_response_no_content(struct ast_ari_response *response);

/*!
 * \brief Fill in a <tt>Created</tt> (201) \a ast_ari_response.
 * \param response Response to fill in.
 * \param url URL to the created resource.
 * \param message JSON response.  This reference is stolen, so just \ref
 *                ast_json_ref if you need to keep a reference to it.
 */
void ast_ari_response_created(struct ast_ari_response *response,
	const char *url, struct ast_json *message);

/*!
 * \brief Fill in \a response with a 500 message for allocation failures.
 * \param response Response to fill in.
 */
void ast_ari_response_alloc_failed(struct ast_ari_response *response);

#endif /* _ASTERISK_ARI_H */
