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

/*!
 * In order to have TLS/SSL support, we need the openssl libraries.
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
 *
 * We declare most of ssl support variables unconditionally,
 * because their number is small and this simplifies the code.
 *
 * NOTE: the ssl-support variables (ssl_ctx, do_ssl, certfile, cipher)
 * and their setup should be moved to a more central place, e.g. asterisk.conf
 * and the source files that processes it. Similarly, ssl_setup() should
 * be run earlier in the startup process so modules have it available.
 */

#if defined(HAVE_OPENSSL) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define	DO_SSL	/* comment in/out if you want to support ssl */
#endif

#ifdef DO_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#else
/* declare dummy types so we can define a pointer to them */
typedef struct {} SSL;
typedef struct {} SSL_CTX;
#endif /* DO_SSL */

/* SSL support */  
#define AST_CERTFILE "asterisk.pem"

struct tls_config {
	int enabled;
	char *certfile;
	char *cipher;
	SSL_CTX *ssl_ctx;
};

/*!
 * The following code implements a generic mechanism for starting
 * services on a TCP or TLS socket.
 * The service is configured in the struct server_args, and
 * then started by calling server_start(desc) on the descriptor.
 * server_start() first verifies if an instance of the service is active,
 * and in case shuts it down. Then, if the service must be started, creates
 * a socket and a thread in charge of doing the accept().
 *
 * The body of the thread is desc->accept_fn(desc), which the user can define
 * freely. We supply a sample implementation, server_root(), structured as an
 * infinite loop. At the beginning of each iteration it runs periodic_fn()
 * if defined (e.g. to perform some cleanup etc.) then issues a poll()
 * or equivalent with a timeout of 'poll_timeout' milliseconds, and if the
 * following accept() is successful it creates a thread in charge of
 * running the session, whose body is desc->worker_fn(). The argument of
 * worker_fn() is a struct server_instance, which contains the address
 * of the other party, a pointer to desc, the file descriptors (fd) on which
 * we can do a select/poll (but NOT IO/, and a FILE * on which we can do I/O.
 * We have both because we want to support plain and SSL sockets, and
 * going through a FILE * lets us provide the encryption/decryption
 * on the stream without using an auxiliary thread.
 *
 * NOTE: in order to let other parts of asterisk use these services,
 * we need to do the following:
 *    + move struct server_instance and struct server_args to
 *	a common header file, together with prototypes for
 *	server_start() and server_root().
 *    +
 */
 
/*!
 * describes a server instance
 */
struct server_instance {
	FILE *f;	/* fopen/funopen result */
	int fd;		/* the socket returned by accept() */
	SSL *ssl;	/* ssl state */
	struct sockaddr_in requestor;
	struct server_args *parent;
};

/*!
 * arguments for the accepting thread
 */
struct server_args {
	struct sockaddr_in sin;
	struct sockaddr_in oldsin;
	struct tls_config *tls_cfg;	/* points to the SSL configuration if any */
	int accept_fd;
	int poll_timeout;
	pthread_t master;
	void *(*accept_fn)(void *);	/* the function in charge of doing the accept */
	void (*periodic_fn)(void *);	/* something we may want to run before after select on the accept socket */
	void *(*worker_fn)(void *);	/* the function in charge of doing the actual work */
	const char *name;
};

void *server_root(void *);
void server_start(struct server_args *desc);
int ssl_setup(struct tls_config *cfg);

/*! \brief HTTP Callbacks take the socket, the method and the path as arguments and should
   return the content, allocated with malloc().  Status should be changed to reflect
   the status of the request if it isn't 200 and title may be set to a malloc()'d string
   to an appropriate title for non-200 responses.  Content length may also be specified. 
   The return value may include additional headers at the front and MUST include a blank 
   line with \r\n to provide separation between user headers and content (even if no
   content is specified) */
typedef struct ast_str *(*ast_http_callback)(struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength);

struct ast_http_uri {
	struct ast_http_uri *next;
	const char *description;
	const char *uri;
	int has_subtree;
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
