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
 * \file http.h
 * \brief Support for Private Asterisk HTTP Servers.
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
	AST_HTTP_PUT,            /*!< Not supported in Asterisk */
};

struct ast_http_uri;

/*! \brief HTTP Callbacks
 *
 * \note The callback function receives server instance, uri, http method,
 * get method (if present in URI), and http headers as arguments and should
 * use the ast_http_send() function for sending content allocated with ast_str
 * and/or content from an opened file descriptor.
 *
 * Status and status text should be sent as arguments to the ast_http_send()
 * function to reflect the status of the request (200 or 304, for example).
 * Content length is calculated by ast_http_send() automatically.
 *
 * Static content may be indicated to the ast_http_send() function, to indicate
 * that it may be cached.
 *
 * \verbatim
 * The return value may include additional headers at the front and MUST
 * include a blank line with \r\n to provide separation between user headers
 * and content (even if no content is specified)
 * \endverbatim
 *
 * For an error response, the ast_http_error() function may be used.
*/
typedef int (*ast_http_callback)(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers);

/*! \brief Definition of a URI handler */
struct ast_http_uri {
	AST_LIST_ENTRY(ast_http_uri) entry;
	const char *description;
	const char *uri;
	ast_http_callback callback;
	unsigned int has_subtree:1;
	/*! Structure is malloc'd */
	unsigned int mallocd:1;
	/*! Data structure is malloc'd */
	unsigned int dmallocd:1;
	/*! Data to bind to the uri if needed */
	void *data;
	/*! Key to be used for unlinking if multiple URIs registered */
	const char *key;
};

/*! \brief Get cookie from Request headers */
struct ast_variable *ast_http_get_cookies(struct ast_variable *headers);

/*! \brief Register a URI handler */
int ast_http_uri_link(struct ast_http_uri *urihandler);

/*! \brief Unregister a URI handler */
void ast_http_uri_unlink(struct ast_http_uri *urihandler);

/*! \brief Unregister all handlers with matching key */
void ast_http_uri_unlink_all_with_key(const char *key);

/*!\brief Return http method name string
 * \since 1.6.3
 */
const char *ast_get_http_method(enum ast_http_method method) attribute_pure;

/*!\brief Return mime type based on extension
 * \param ftype filename extension
 * \return String containing associated MIME type
 * \since 1.6.3
 */
const char *ast_http_ftype2mtype(const char *ftype) attribute_pure;

/*!\brief Return manager id, if exist, from request headers
 * \param headers List of HTTP headers
 * \return 32-bit associated manager session identifier
 * \since 1.6.3
 */
uint32_t ast_http_manid_from_vars(struct ast_variable *headers) attribute_pure;

/*! \brief Generic function for sending http/1.1 response.
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
 * \since 1.6.3
 */
void ast_http_send(struct ast_tcptls_session_instance *ser, enum ast_http_method method, int status_code, const char *status_title, struct ast_str *http_header, struct ast_str *out, const int fd, unsigned int static_content);

/*!\brief Send http "401 Unauthorized" response and close socket */
void ast_http_auth(struct ast_tcptls_session_instance *ser, const char *realm, const unsigned long nonce, const unsigned long opaque, int stale, const char *text);

/*!\brief Send HTTP error message and close socket */
void ast_http_error(struct ast_tcptls_session_instance *ser, int status, const char *title, const char *text);

/*!
 * \brief Return the current prefix
 * \param buf[out] destination buffer for previous
 * \param len[in] length of prefix to copy
 * \since 1.6.1
 */
void ast_http_prefix(char *buf, int len);


/*!\brief Get post variables from client Request Entity-Body, if content type is application/x-www-form-urlencoded.
 * \param ser TCP/TLS session object
 * \param headers List of HTTP headers
 * \return List of variables within the POST body
 * \note Since returned list is malloc'd, list should be free'd by the calling function
 * \since 1.6.3
 */
struct ast_variable *ast_http_get_post_vars(struct ast_tcptls_session_instance *ser, struct ast_variable *headers);


#endif /* _ASTERISK_SRV_H */
