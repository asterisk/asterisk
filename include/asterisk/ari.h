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
 * \param ser TCP/TLS session object (Maybe NULL if not available).
 * \param get_params GET parameters from the HTTP request.
 * \param path_vars Path variables from any wildcard path segments.
 * \param headers HTTP headers from the HTTP requiest.
 * \param body
 * \param[out] response The RESTful response.
 */
typedef void (*stasis_rest_callback)(
	struct ast_tcptls_session_instance *ser,
	struct ast_variable *get_params, struct ast_variable *path_vars,
	struct ast_variable *headers, struct ast_json *body,
	struct ast_ari_response *response);

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
	/*!
	 * ws_server is no longer needed to indicate if a path should cause
	 * an Upgrade to websocket but is kept for backwards compatability.
	 * Instead, simply set is_websocket to true.
	 */
	union {
		/*! \deprecated WebSocket server for handling WebSocket upgrades. */
		struct ast_websocket_server *ws_server;
		/*! The path segment is handled by the websocket */
		int is_websocket;
	};
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
	/*! \\r\\n seperated response headers */
	struct ast_str *headers;
	/*! HTTP response code.
	 * See http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html */
	int response_code;
	/*! File descriptor for whatever file we want to respond with */
	int fd;
	/*! Corresponding text for the response code */
	const char *response_text; /* Shouldn't http.c handle this? */
	/*! Flag to indicate that no further response is needed */
	unsigned int no_response:1;
};

/*!
 * Add a resource for REST handling.
 * \param handler Handler to add.
 * \retval 0 on success.
 * \retval non-zero on failure.
 */
int ast_ari_add_handler(struct stasis_rest_handlers *handler);

/*!
 * Remove a resource for REST handling.
 * \param handler Handler to add.
 * \retval 0 on success.
 * \retval non-zero on failure.
 */
int ast_ari_remove_handler(struct stasis_rest_handlers *handler);

/*!
 * \internal
 * \brief Stasis RESTful invocation handler response codes.
 */
enum ast_ari_invoke_result {
	ARI_INVOKE_RESULT_SUCCESS = 0,
	ARI_INVOKE_RESULT_ERROR_CONTINUE = -1,
	ARI_INVOKE_RESULT_ERROR_CLOSE = -2,
};

/*!
 * \internal
 * \brief How was Stasis RESTful invocation handler invoked?
 */
enum ast_ari_invoke_source {
	ARI_INVOKE_SOURCE_REST = 0,
	ARI_INVOKE_SOURCE_WEBSOCKET,
	ARI_INVOKE_SOURCE_TEST,
};

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
 * \param body
 * \param[out] response RESTful HTTP response.
 * \param is_websocket Flag to indicate if this is a WebSocket request.
 */
enum ast_ari_invoke_result ast_ari_invoke(struct ast_tcptls_session_instance *ser,
	enum ast_ari_invoke_source source, const struct ast_http_uri *urih,
	const char *uri, enum ast_http_method method,
	struct ast_variable *get_params, struct ast_variable *headers,
	struct ast_json *body, struct ast_ari_response *response);

/*!
 * \internal
 * \brief Service function for API declarations.
 *
 * Only call from res_ari and test_ari. Only public to allow
 * for unit testing.
 *
 * \param uri Requested URI, relative to the docs path.
 * \param prefix prefix that prefixes all http requests
 * \param headers HTTP headers.
 * \param[out] response RESTful HTTP response.
 */
void ast_ari_get_docs(const char *uri, const char *prefix, struct ast_variable *headers, struct ast_ari_response *response);

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
 * \brief Fill in a <tt>Accepted</tt> (202) \a ast_ari_response.
 */
void ast_ari_response_accepted(struct ast_ari_response *response);

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

/*!
 * \brief Create a per-call outbound websocket connection.
 *
 * \param app_name The app name.
 * \param channel The channel to create the websocket for.
 *
 * This function should really only be called by app_stasis.
 *
 * A "per_call" websocket configuration must already exist in
 * ari.conf that has 'app_name' in its 'apps' parameter.
 *
 * The channel uniqueid is used to create a unique app_id
 * composed of "<app_name>-<channel_uniqueid>" which will be
 * returned from this call.  This ID will be used to register
 * an ephemeral Stasis application and should be used as the
 * app_name for the call to stasis_app_exec().  When
 * stasis_app_exec() returns, ast_ari_close_per_call_websocket()
 * must be called with the app_id to close the websocket.
 *
 * The channel unique id is also used to detect when the
 * StasisEnd event is sent for the channel.  It's how
 * ast_ari_close_per_call_websocket() knows that all
 * messages for the channel have been sent and it's safe
 * to close the websocket.
 *
 * \retval The ephemeral application id or NULL if one could
 *         not be created. This pointer will be freed by
 *         ast_ari_close_per_call_websocket().  Do not free
 *         it yourself.
 */
char *ast_ari_create_per_call_websocket(const char *app_name,
	struct ast_channel *channel);

/*!
 * \brief Close a per-call outbound websocket connection.
 *
 * \param app_id The ephemeral application id returned by
 *               ast_ari_create_per_call_websocket().
 *
 * This function should really only be called by app_stasis.
 *
 * \note This call will block until all messages for the
 *       channel have been sent or 5 seconds has elapsed.
 *       After that, the websocket will be closed.
 */
void ast_ari_close_per_call_websocket(char *app_id);

#endif /* _ASTERISK_ARI_H */
