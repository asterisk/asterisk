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

/*!
 * \file tcptls.h
 *
 * \brief Generic support for tcp/tls servers in Asterisk.
 * \note In order to have TLS/SSL support, we need the openssl libraries.
 * Still we can decide whether or not to use them by commenting
 * in or out the DO_SSL macro.
 *
 * TLS/SSL support is basically implemented by reading from a config file
 * (currently http.conf and sip.conf) the names of the certificate and cipher to use,
 * and then run ssl_setup() to create an appropriate SSL_CTX (ssl_ctx)
 * If we support multiple domains, presumably we need to read multiple
 * certificates.
 *
 * When we are requested to open a TLS socket, we run make_file_from_fd()
 * on the socket, to do the necessary setup. At the moment the context's name
 * is hardwired in the function, but we can certainly make it into an extra
 * parameter to the function.
 *
 * We declare most of ssl support variables unconditionally,
 * because their number is small and this simplifies the code.
 *
 * \note The ssl-support variables (ssl_ctx, do_ssl, certfile, cipher)
 * and their setup should be moved to a more central place, e.g. asterisk.conf
 * and the source files that processes it. Similarly, ssl_setup() should
 * be run earlier in the startup process so modules have it available.
 *
 */

#ifndef _ASTERISK_TCPTLS_H
#define _ASTERISK_TCPTLS_H

#include "asterisk/netsock2.h"
#include "asterisk/utils.h"

#if defined(HAVE_OPENSSL) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define DO_SSL  /* comment in/out if you want to support ssl */
#endif

#ifdef DO_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#else
/* declare dummy types so we can define a pointer to them */
typedef struct {} SSL;
typedef struct {} SSL_CTX;
#endif /* DO_SSL */

/*! SSL support */
#define AST_CERTFILE "asterisk.pem"

enum ast_ssl_flags {
	/*! Verify certificate when acting as server */
	AST_SSL_VERIFY_CLIENT = (1 << 0),
	/*! Don't verify certificate when connecting to a server */
	AST_SSL_DONT_VERIFY_SERVER = (1 << 1),
	/*! Don't compare "Common Name" against IP or hostname */
	AST_SSL_IGNORE_COMMON_NAME = (1 << 2),
	/*! Use SSLv2 for outgoing client connections */
	AST_SSL_SSLV2_CLIENT = (1 << 3),
	/*! Use SSLv3 for outgoing client connections */
	AST_SSL_SSLV3_CLIENT = (1 << 4),
	/*! Use TLSv1 for outgoing client connections */
	AST_SSL_TLSV1_CLIENT = (1 << 5)
};

struct ast_tls_config {
	int enabled;
	char *certfile;
	char *pvtfile;
	char *cipher;
	char *cafile;
	char *capath;
	struct ast_flags flags;
	SSL_CTX *ssl_ctx;
};

/*!
 * The following code implements a generic mechanism for starting
 * services on a TCP or TLS socket.
 * The service is configured in the struct session_args, and
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
 * worker_fn() is a struct ast_tcptls_session_instance, which contains the address
 * of the other party, a pointer to desc, the file descriptors (fd) on which
 * we can do a select/poll (but NOT I/O), and a FILE *on which we can do I/O.
 * We have both because we want to support plain and SSL sockets, and
 * going through a FILE * lets us provide the encryption/decryption
 * on the stream without using an auxiliary thread.
 */

/*! \brief
 * arguments for the accepting thread
 */
struct ast_tcptls_session_args {
	struct ast_sockaddr local_address;
	struct ast_sockaddr old_address; /*!< copy of the local or remote address depending on if its a client or server session */
	struct ast_sockaddr remote_address;
	char hostname[MAXHOSTNAMELEN]; /*!< only necessary for SSL clients so we can compare to common name */
	struct ast_tls_config *tls_cfg; /*!< points to the SSL configuration if any */
	int accept_fd;
	int poll_timeout;
	pthread_t master;
	void *(*accept_fn)(void *); /*!< the function in charge of doing the accept */
	void (*periodic_fn)(void *);/*!< something we may want to run before after select on the accept socket */
	void *(*worker_fn)(void *); /*!< the function in charge of doing the actual work */
	const char *name;
};

/*
 * describes a server instance
 */
struct ast_tcptls_session_instance {
	FILE *f;    /* fopen/funopen result */
	int fd;     /* the socket returned by accept() */
	SSL *ssl;   /* ssl state */
/*	iint (*ssl_setup)(SSL *); */
	int client;
	struct ast_sockaddr remote_address;
	struct ast_tcptls_session_args *parent;
	ast_mutex_t lock;
};

#if defined(HAVE_FUNOPEN)
#define HOOK_T int
#define LEN_T int
#else
#define HOOK_T ssize_t
#define LEN_T size_t
#endif

/*! 
  * \brief attempts to connect and start tcptls session, on error the tcptls_session's
  * ref count is decremented, fd and file are closed, and NULL is returned.
  */
struct ast_tcptls_session_instance *ast_tcptls_client_start(struct ast_tcptls_session_instance *tcptls_session);

/* \brief Creates a client connection's ast_tcptls_session_instance. */
struct ast_tcptls_session_instance *ast_tcptls_client_create(struct ast_tcptls_session_args *desc);

void *ast_tcptls_server_root(void *);

/*!
 * \brief This is a generic (re)start routine for a TCP server,
 * which does the socket/bind/listen and starts a thread for handling
 * accept().
 * \version 1.6.1 changed desc parameter to be of ast_tcptls_session_args type
 */
void ast_tcptls_server_start(struct ast_tcptls_session_args *desc);

/*!
 * \brief Shutdown a running server if there is one
 * \version 1.6.1 changed desc parameter to be of ast_tcptls_session_args type
 */
void ast_tcptls_server_stop(struct ast_tcptls_session_args *desc);
int ast_ssl_setup(struct ast_tls_config *cfg);

/*!
 * \brief Used to parse conf files containing tls/ssl options.
 */
int ast_tls_read_conf(struct ast_tls_config *tls_cfg, struct ast_tcptls_session_args *tls_desc, const char *varname, const char *value);

HOOK_T ast_tcptls_server_read(struct ast_tcptls_session_instance *ser, void *buf, size_t count);
HOOK_T ast_tcptls_server_write(struct ast_tcptls_session_instance *ser, const void *buf, size_t count);

#endif /* _ASTERISK_TCPTLS_H */
