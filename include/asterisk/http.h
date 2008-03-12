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


/*! \brief HTTP Callbacks take the socket

   \note The method and the path as arguments and should
   return the content, allocated with malloc().  Status should be changed to reflect
   the status of the request if it isn't 200 and title may be set to a malloc()'d string
   to an appropriate title for non-200 responses.  Content length may also be specified. 
\verbatim   
   The return value may include additional headers at the front and MUST include a blank 
   line with \r\n to provide separation between user headers and content (even if no
   content is specified) 
\endverbatim
*/
typedef struct ast_str *(*ast_http_callback)(struct ast_tcptls_session_instance *ser, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength);

/*! \brief Definition of a URI reachable in the embedded HTTP server */
struct ast_http_uri {
	AST_LIST_ENTRY(ast_http_uri) entry;
	const char *description;
	const char *uri;
	unsigned int has_subtree:1;
	/*! This URI mapping serves static content */
	unsigned int static_content:1;
	ast_http_callback callback;
};

/*! \brief Link into the Asterisk HTTP server */
int ast_http_uri_link(struct ast_http_uri *urihandler);

/*! \brief Return an ast_str malloc()'d string containing an HTTP error message */
struct ast_str *ast_http_error(int status, const char *title, const char *extra_header, const char *text);

/*! \brief Destroy an HTTP server */
void ast_http_uri_unlink(struct ast_http_uri *urihandler);

int ast_http_init(void);
int ast_http_reload(void);

#endif /* _ASTERISK_SRV_H */
