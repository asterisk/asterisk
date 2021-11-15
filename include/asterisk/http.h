/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

#ifndef _ASTERISK_HTTP_H
#define _ASTERISK_HTTP_H

#include "asterisk/config.h"
#include "asterisk/tcptls.h"
#include "asterisk/linkedlists.h"

/*!
 * \file
 *
 * \brief Support for Private Asterisk HTTP Servers.
 *
 * \note Note: The Asterisk HTTP servers are extremely simple and minimal and
 *      only support the "GET" method.
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note In order to have TLS/SSL support, we need the openssl libraries.
 * Still we can decide whether or not to use them by commenting
 * in or out the DO_SSL macro.
 * TLS/SSL support is basically implemented by reading from a config file
 * (currently http.conf) the names of the certificate and cipher to use,
 * and then run ssl_setup() to create an appropriate SSL_CTX (ssl_ctx)
 * If we support multiple domains, presumably we need to read multiple
 * certificates.
 * When we are requested to open a TLS socket, we run make_file_from_fd()
 * on the socket, to do the necessary setup. At the moment the context's name
 * is hardwired in the function, but we can certainly make it into an extra
 * parameter to the function.
 * We declare most of ssl support variables unconditionally,
 * because their number is small and this simplifies the code.
 *
 * \note: the ssl-support variables (ssl_ctx, do_ssl, certfile, cipher)
 * and their setup should be moved to a more central place, e.g. asterisk.conf
 * and the source files that processes it. Similarly, ssl_setup() should
 * be run earlier in the startup process so modules have it available.
 */

/*! \brief HTTP Request methods known by Asterisk */
enum ast_http_method {
	AST_HTTP_UNKNOWN = -1,   /*!< Unknown response */
	AST_HTTP_GET = 0,
	AST_HTTP_POST,
	AST_HTTP_HEAD,
	AST_HTTP_PUT,
	AST_HTTP_DELETE,
	AST_HTTP_OPTIONS,
	AST_HTTP_MAX_METHOD, /*!< Last entry in ast_http_method enum */
};

struct ast_http_uri;

/*!
 * \brief HTTP Callbacks
 *
 * \param ser TCP/TLS session object
 * \param urih Registered URI handler struct for the URI.
 * \param uri Remaining request URI path (also with the get_params removed).
 * \param method enum ast_http_method GET, POST, etc.
 * \param get_params URI argument list passed with the HTTP request.
 * \param headers HTTP request header-name/value pair list
 *
 * \note Should use the ast_http_send() function for sending content
 * allocated with ast_str and/or content from an opened file descriptor.
 *
 * Status and status text should be sent as arguments to the ast_http_send()
 * function to reflect the status of the request (200 or 304, for example).
 * Content length is calculated by ast_http_send() automatically.
 *
 * Static content may be indicated to the ast_http_send() function,
 * to indicate that it may be cached.
 *
 * For a need authentication response, the ast_http_auth() function
 * should be used.
 *
 * For an error response, the ast_http_error() function should be used.
 *
 * \retval 0 Continue and process the next HTTP request.
 * \retval -1 Fatal HTTP connection error.  Force the HTTP connection closed.
 */
typedef int (*ast_http_callback)(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers);

/*! \brief Definition of a URI handler */
struct ast_http_uri {
	AST_LIST_ENTRY(ast_http_uri) entry;
	const char *description;
	const char *uri;
	const char *prefix;
	ast_http_callback callback;
	unsigned int has_subtree:1;
	/*! Structure is malloc'd */
	unsigned int mallocd:1;
	/*! Data structure is malloc'd */
	unsigned int dmallocd:1;
	/*! Don't automatically decode URI before passing it to the callback */
	unsigned int no_decode_uri:1;
	/*! Data to bind to the uri if needed */
	void *data;
	/*! Key to be used for unlinking if multiple URIs registered */
	const char *key;
};

/*! \brief Get cookie from Request headers */
struct ast_variable *ast_http_get_cookies(struct ast_variable *headers);

/*! \brief HTTP authentication information. */
struct ast_http_auth {
	/*! Provided userid. */
	char *userid;
	/*! For Basic auth, the provided password. */
	char *password;
};

/*!
 * \brief Get HTTP authentication information from headers.
 *
 * The returned object is AO2 managed, so clean up with ao2_cleanup().
 *
 * \param headers HTTP request headers.
 * \return HTTP auth structure.
 * \retval NULL if no supported HTTP auth headers present.
 * \since 12
 */
struct ast_http_auth *ast_http_get_auth(struct ast_variable *headers);

/*! \brief Register a URI handler */
int ast_http_uri_link(struct ast_http_uri *urihandler);

/*! \brief Unregister a URI handler */
void ast_http_uri_unlink(struct ast_http_uri *urihandler);

/*! \brief Unregister all handlers with matching key */
void ast_http_uri_unlink_all_with_key(const char *key);

/*!
 * \brief Return http method name string
 * \since 1.8
 */
const char *ast_get_http_method(enum ast_http_method method) attribute_pure;

/*!
 * \brief Return mime type based on extension
 * \param ftype filename extension
 * \return String containing associated MIME type
 * \since 1.8
 */
const char *ast_http_ftype2mtype(const char *ftype) attribute_pure;

/*!
 * \brief Return manager id, if exist, from request headers
 * \param headers List of HTTP headers
 * \return 32-bit associated manager session identifier
 * \since 1.8
 */
uint32_t ast_http_manid_from_vars(struct ast_variable *headers) attribute_pure;

/*!
 * \brief Generic function for sending HTTP/1.1 response.
 * \param ser TCP/TLS session object
 * \param method GET/POST/HEAD
 * \param status_code HTTP response code (200/401/403/404/500)
 * \param status_title English equivalent to the status_code parameter
 * \param http_header An ast_str object containing all headers
 * \param out An ast_str object containing the body of the response
 * \param fd If out is NULL, a file descriptor where the body of the response is held (otherwise -1)
 * \param static_content Zero if the content is dynamically generated and should not be cached; nonzero otherwise
 *
 * \note Function determines the HTTP response header from status_code,
 * status_header, and http_header.
 *
 * Extra HTTP headers MUST be present only in the http_header argument.  The
 * argument "out" should contain only content of the response (no headers!).
 *
 * HTTP content can be constructed from the argument "out", if it is not NULL;
 * otherwise, the function will read content from FD.
 *
 * This function calculates the content-length http header itself.
 *
 * Both the http_header and out arguments will be freed by this function;
 * however, if FD is open, it will remain open.
 *
 * \since 1.8
 */
void ast_http_send(struct ast_tcptls_session_instance *ser, enum ast_http_method method,
	int status_code, const char *status_title, struct ast_str *http_header,
	struct ast_str *out, int fd, unsigned int static_content);

/*!
 * \brief Creates and sends a formatted http response message.
 * \param ser                   TCP/TLS session object
 * \param status_code           HTTP response code (200/401/403/404/500)
 * \param status_title          English equivalent to the status_code parameter
 * \param http_header_data      The formatted text to use in the http header
 * \param text                  Additional informational text to use in the
 *                              response
 *
 * \note Function constructs response headers from the status_code, status_title and
 * http_header_data parameters.
 *
 * The response body is created as HTML content, from the status_code,
 * status_title, and the text parameters.
 *
 * The http_header_data parameter will be freed as a result of calling function.
 *
 * \since 13.2.0
 */
void ast_http_create_response(struct ast_tcptls_session_instance *ser, int status_code,
	const char *status_title, struct ast_str *http_header_data, const char *text);

/*! \brief Send http "401 Unauthorized" response and close socket */
void ast_http_auth(struct ast_tcptls_session_instance *ser, const char *realm, const unsigned long nonce, const unsigned long opaque, int stale, const char *text);

/*! \brief Send HTTP error message and close socket */
void ast_http_error(struct ast_tcptls_session_instance *ser, int status, const char *title, const char *text);

/*!
 * \brief Return the current prefix
 * \param[out] buf destination buffer for previous
 * \param[in] len length of prefix to copy
 * \since 1.6.1
 */
void ast_http_prefix(char *buf, int len);

/*!
 * \brief Request the HTTP connection be closed after this HTTP request.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 *
 * \note Call before ast_http_error() to make the connection close.
 */
void ast_http_request_close_on_completion(struct ast_tcptls_session_instance *ser);

/*!
 * \brief Update the body read success status.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 * \param read_success TRUE if body was read successfully.
 */
void ast_http_body_read_status(struct ast_tcptls_session_instance *ser, int read_success);

/*!
 * \brief Read and discard any unread HTTP request body.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_http_body_discard(struct ast_tcptls_session_instance *ser);

/*!
 * \brief Get post variables from client Request Entity-Body, if content type is application/x-www-form-urlencoded.
 * \param ser TCP/TLS session object
 * \param headers List of HTTP headers
 * \return List of variables within the POST body
 * \note Since returned list is malloc'd, list should be free'd by the calling function
 * \since 1.8
 */
struct ast_variable *ast_http_get_post_vars(struct ast_tcptls_session_instance *ser, struct ast_variable *headers);

struct ast_json;

/*!
 * \brief Get JSON from client Request Entity-Body, if content type is
 *        application/json.
 * \param ser TCP/TLS session object
 * \param headers List of HTTP headers
 * \return Parsed JSON content body
 * \retval NULL on error, if no content, or if different content type.
 * \since 12
 */
struct ast_json *ast_http_get_json(
	struct ast_tcptls_session_instance *ser, struct ast_variable *headers);

/*!\brief Parse the http response status line.
 *
 * \param buf the http response line information
 * \param version the expected http version (e.g. HTTP/1.1)
 * \param code the expected status code
 * \retval -1 if version didn't match or status code conversion fails.
 * \return status code (>0)
 * \since 13
 */
int ast_http_response_status_line(const char *buf, const char *version, int code);

/*!\brief Parse a header into the given name/value strings.
 *
 * \note This modifies the given buffer and the out parameters point (not
 *       allocated) to the start of the header name and header value,
 *       respectively.
 *
 * \param buf a string containing the name/value to point to
 * \param[out] name header name
 * \param[out] value header value
 * \retval -1 if buf is empty
 * \retval 0 if buf could be separated into name and value
 * \retval 1 if name or value portion don't exist
 * \since 13
 */
int ast_http_header_parse(char *buf, char **name, char **value);

/*!\brief Check if the header and value match (case insensitive) their
 *        associated expected values.
 *
 * \param name header name to check
 * \param expected_name the expected name of the header
 * \param value header value to check
 * \param expected_value the expected value of the header
 * \retval 0 if the name and expected name do not match
 * \retval -1 if the value and expected value do not match
 * \retval 1 if the both the name and value match their expected value
 * \since 13
 */
int ast_http_header_match(const char *name, const char *expected_name,
			  const char *value, const char *expected_value);

/*!\brief Check if the header name matches the expected header name.  If so,
 *        then check to see if the value can be located in the expected value.
 *
 * \note Both header and value checks are case insensitive.
 *
 * \param name header name to check
 * \param expected_name the expected name of the header
 * \param value header value to check if in expected value
 * \param expected_value the expected value(s)
 * \retval 0 if the name and expected name do not match
 * \retval -1 if the value and is not in the expected value
 * \retval 1 if the name matches expected name and value is in expected value
 * \since 13
 */
int ast_http_header_match_in(const char *name, const char *expected_name,
			     const char *value, const char *expected_value);

#ifdef TEST_FRAMEWORK

/*!
 * Currently multiple HTTP servers are only allowed when the TEST_FRAMEWORK
 * is enabled, so putting this here:
 *
 * If a server is listening on 'any' (i.e. 0.0.0.0), and another server attempts
 * to listen on 'localhost' on the same port (and vice versa) then you'll get an
 * "Address already in use" error. For now use a different port, or match the
 * addresses exactly.
 */

struct ast_http_server;

/*!
 * \brief Retrieve a HTTP server listening at the given host
 *
 * A given host can include the port, e.g. <host>[:<port>]. If no port is specified
 * then the port defaults to '8088'. If a host parameter is NULL, or empty and a
 * configured server is already listening then that server is returned. If no
 * server is and enabled then the host defaults to 'localhost:8088'.
 *
 * \note When finished with a successfully returned server object
 *       ast_http_test_server_discard MUST be called on the object
 *       in order for proper 'cleanup' to occur.
 *
 * \param name Optional name for the server (default 'http test server')
 * \param host Optional host, or address with port to bind to (default 'localhost:8088')
 *
 * \return a HTTP server object, or NULL on error
 */
struct ast_http_server *ast_http_test_server_get(const char *name, const char *host);

/*!
 * \brief Discard, or drop a HTTP server
 *
 * This function MUST eventually be called for every successful call to
 * ast_http_test_server_get.
 *
 * \note NULL tolerant
 *
 * \param server The HTTP server to discard
 */
void ast_http_test_server_discard(struct ast_http_server *server);

#endif

#endif /* _ASTERISK_SRV_H */
