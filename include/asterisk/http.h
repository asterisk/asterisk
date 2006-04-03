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

/*!
  \file http.h
  \brief Support for Private Asterisk HTTP Servers.
  \note Note: The Asterisk HTTP servers are extremely simple and minimal and
        only support the "GET" method.
  \author Mark Spencer <markster@digium.com>
*/

/*! \brief HTTP Callbacks take the socket, the method and the path as arguments and should
   return the content, allocated with malloc().  Status should be changed to reflect
   the status of the request if it isn't 200 and title may be set to a malloc()'d string
   to an appropriate title for non-200 responses.  Content length may also be specified. 
   The return value may include additional headers at the front and MUST include a blank 
   line with \r\n to provide separation between user headers and content (even if no
   content is specified) */
typedef char *(*ast_http_callback)(struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength);

struct ast_http_uri {
	struct ast_http_uri *next;
	const char *description;
	const char *uri;
	int has_subtree;
	ast_http_callback callback;
};

/*! \brief Link into the Asterisk HTTP server */
int ast_http_uri_link(struct ast_http_uri *urihandler);

/*! \brief Return a malloc()'d string containing an HTTP error message */
char *ast_http_error(int status, const char *title, const char *extra_header, const char *text);

/*! \brief Destroy an HTTP server */
void ast_http_uri_unlink(struct ast_http_uri *urihandler);

char *ast_http_setcookie(const char *var, const char *val, int expires, char *buf, size_t buflen);

int ast_http_init(void);
int ast_http_reload(void);

#endif /* _ASTERISK_SRV_H */
