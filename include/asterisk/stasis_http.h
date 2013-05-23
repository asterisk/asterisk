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

#ifndef _ASTERISK_STASIS_HTTP_H
#define _ASTERISK_STASIS_HTTP_H

/*! \file
 *
 * \brief Stasis RESTful API hooks.
 *
 * This header file is used mostly as glue code between generated declarations
 * and res_stasis_http.c.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk/http.h"
#include "asterisk/json.h"
#include "asterisk/http_websocket.h"

struct stasis_http_response;

/*!
 * \brief Callback type for RESTful method handlers.
 * \param get_params GET parameters from the HTTP request.
 * \param path_vars Path variables from any wildcard path segments.
 * \param headers HTTP headers from the HTTP requiest.
 * \param[out] response The RESTful response.
 */
typedef void (*stasis_rest_callback)(struct ast_variable *get_params,
				     struct ast_variable *path_vars,
				     struct ast_variable *headers,
				     struct stasis_http_response *response);

/*!
 * \brief Handler for a single RESTful path segment.
 */
struct stasis_rest_handlers {
	/*! Path segement to handle */
	const char *path_segment;
	/*! If true (non-zero), path_segment is a wildcard, and will match all values.
	 *
	 * Value of the segement will be passed into the \a path_vars parameter of the callback.
	 */
	int is_wildcard;
	/*! Callbacks for all handled HTTP methods. */
	stasis_rest_callback callbacks[AST_HTTP_MAX_METHOD];
	/*! Number of children in the children array */
	size_t num_children;
	/*! Handlers for sub-paths */
	struct stasis_rest_handlers *children[];
};

/*!
 * Response type for RESTful requests
 */
struct stasis_http_response {
	/*! Response message */
	struct ast_json *message;
	/*! \r\n seperated response headers */
	struct ast_str *headers;
	/*! HTTP response code.
	 * See http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html */
	int response_code;
	/*! Corresponding text for the response code */
	const char *response_text; // Shouldn't http.c handle this?
};

/*!
 * Add a resource for REST handling.
 * \param handler Handler to add.
 * \return 0 on success.
 * \return non-zero on failure.
 */
int stasis_http_add_handler(struct stasis_rest_handlers *handler);

/*!
 * Remove a resource for REST handling.
 * \param handler Handler to add.
 * \return 0 on success.
 * \return non-zero on failure.
 */
int stasis_http_remove_handler(struct stasis_rest_handlers *handler);

/*!
 * \internal
 * \brief Stasis RESTful invocation handler.
 *
 * Only call from res_stasis_http and test_stasis_http. Only public to allow
 * for unit testing.
 *
 * \param uri HTTP URI, relative to the API path.
 * \param method HTTP method.
 * \param get_params HTTP \c GET parameters.
 * \param headers HTTP headers.
 * \param[out] response RESTful HTTP response.
 */
void stasis_http_invoke(const char *uri, enum ast_http_method method, struct ast_variable *get_params,
			struct ast_variable *headers, struct stasis_http_response *response);

/*!
 * \internal
 * \brief Service function for API declarations.
 *
 * Only call from res_stasis_http and test_stasis_http. Only public to allow
 * for unit testing.
 *
 * \param uri Requested URI, relative to the docs path.
 * \param headers HTTP headers.
 * \param[out] response RESTful HTTP response.
 */
void stasis_http_get_docs(const char *uri, struct ast_variable *headers, struct stasis_http_response *response);

/*!
 * \internal
 * \brief Stasis WebSocket connection handler
 * \param session WebSocket session.
 * \param parameters HTTP \c GET parameters.
 * \param headers HTTP headers.
 */
void stasis_websocket_callback(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers);

/*!
 * \brief Fill in an error \a stasis_http_response.
 * \param response Response to fill in.
 * \param response_code HTTP response code.
 * \param response_text Text corresponding to the HTTP response code.
 * \param message_fmt Error message format string.
 */
void stasis_http_response_error(struct stasis_http_response *response,
				int response_code,
				const char *response_text,
				const char *message_fmt, ...)
__attribute__((format(printf, 4, 5)));

/*!
 * \brief Fill in an \c OK (200) \a stasis_http_response.
 * \param response Response to fill in.
 * \param message JSON response.  This reference is stolen, so just \ref
 *                ast_json_incref if you need to keep a reference to it.
 */
void stasis_http_response_ok(struct stasis_http_response *response,
			     struct ast_json *message);

/*!
 * \brief Fill in a <tt>No Content</tt> (204) \a stasis_http_response.
 */
void stasis_http_response_no_content(struct stasis_http_response *response);

/*!
 * \brief Fill in a <tt>Created</tt> (201) \a stasis_http_response.
 */
void stasis_http_response_created(struct stasis_http_response *response,
	const char *url, struct ast_json *message);

/*!
 * \brief Fill in \a response with a 500 message for allocation failures.
 * \param response Response to fill in.
 */
void stasis_http_response_alloc_failed(struct stasis_http_response *response);

#endif /* _ASTERISK_STASIS_HTTP_H */
