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
 * \file
 *
 * \brief Generic support for tcp/tls servers in Asterisk.
 *
 * \note In order to have TLS/SSL support, we need the openssl libraries.
 * Still we can decide whether or not to use them by commenting
 * in or out the DO_SSL macro.
 *
 * TLS/SSL support is basically implemented by reading from a config file
 * (currently manager.conf, http.conf and sip.conf) the names of the certificate
 * files and cipher to use, and then run ssl_setup() to create an appropriate
 * data structure named ssl_ctx.
 *
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
 * \ref AstTlsOverview
 */

#ifndef _ASTERISK_TCPTLS_H
#define _ASTERISK_TCPTLS_H

#include <pthread.h>            /* for pthread_t */
#include <sys/param.h>          /* for MAXHOSTNAMELEN */

#include "asterisk/iostream.h"
#include "asterisk/netsock2.h"  /* for ast_sockaddr */
#include "asterisk/utils.h"     /* for ast_flags */

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
	AST_SSL_TLSV1_CLIENT = (1 << 5),
	/*! Use server cipher order instead of the client order */
	AST_SSL_SERVER_CIPHER_ORDER = (1 << 6),
	/*! Disable TLSv1 support */
	AST_SSL_DISABLE_TLSV1 = (1 << 7),
	/*! Disable TLSv1.1 support */
	AST_SSL_DISABLE_TLSV11 = (1 << 8),
	/*! Disable TLSv1.2 support */
	AST_SSL_DISABLE_TLSV12 = (1 << 9),
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
	char certhash[41];
	char pvthash[41];
	char cahash[41];
};

/*! \page AstTlsOverview TLS Implementation Overview
 *
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
	/*! Server accept_fn thread ID used for external shutdown requests. */
	pthread_t master;
	void *(*accept_fn)(void *); /*!< the function in charge of doing the accept */
	void (*periodic_fn)(void *);/*!< something we may want to run before after select on the accept socket */
	void *(*worker_fn)(void *); /*!< the function in charge of doing the actual work */
	const char *name;
	struct ast_tls_config *old_tls_cfg; /*!< copy of the SSL configuration to determine whether changes have been made */
};

/*! \brief
 * describes a server instance
 */
struct ast_tcptls_session_instance {
	int client;
	struct ast_sockaddr remote_address;
	struct ast_tcptls_session_args *parent;
	/* Sometimes, when an entity reads TCP data, multiple
	 * logical messages might be read at the same time. In such
	 * a circumstance, there needs to be a place to stash the
	 * extra data.
	 */
	struct ast_str *overflow_buf;
	/*! ao2 stream object associated with this session. */
	struct ast_iostream *stream;
	/*! ao2 object private data of parent->worker_fn */
	void *private_data;
};

/*!
  * \brief Attempt to connect and start a tcptls session within the given timeout
  *
  * \note On error the tcptls_session's ref count is decremented, fd and file
  * are closed, and NULL is returned.
  *
  * \param tcptls_session The session instance to connect and start
  * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
  *
  * \return The tcptls_session, or NULL on error
  */
struct ast_tcptls_session_instance *ast_tcptls_client_start_timeout(
	struct ast_tcptls_session_instance *tcptls_session, int timeout);

/*!
  * \brief Attempt to connect and start a tcptls session
  *
  * Blocks until a connection is established, or an error occurs.
  *
  * \note On error the tcptls_session's ref count is decremented, fd and file
  * are closed, and NULL is returned.
  *
  * \param tcptls_session The session instance to connect and start
  *
  * \return The tcptls_session, or NULL on error
  */
struct ast_tcptls_session_instance *ast_tcptls_client_start(struct ast_tcptls_session_instance *tcptls_session);

/*! \brief Creates a client connection's ast_tcptls_session_instance. */
struct ast_tcptls_session_instance *ast_tcptls_client_create(struct ast_tcptls_session_args *desc);

void *ast_tcptls_server_root(void *);

/*!
 * \brief Closes a tcptls session instance's file and/or file descriptor.
 * The tcptls_session will be set to NULL and it's file descriptor will be set to -1
 * by this function.
 */
void ast_tcptls_close_session_file(struct ast_tcptls_session_instance *tcptls_session);

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

/*!
 * \brief Set up an SSL server
 *
 * \param cfg Configuration for the SSL server
 * \retval 1 Success
 * \retval 0 Failure
 */
int ast_ssl_setup(struct ast_tls_config *cfg);

/*!
 * \brief free resources used by an SSL server
 *
 * \note This only needs to be called if ast_ssl_setup() was
 * directly called first.
 * \param cfg Configuration for the SSL server
 */
void ast_ssl_teardown(struct ast_tls_config *cfg);

/*!
 * \brief Used to parse conf files containing tls/ssl options.
 */
int ast_tls_read_conf(struct ast_tls_config *tls_cfg, struct ast_tcptls_session_args *tls_desc, const char *varname, const char *value);

#endif /* _ASTERISK_TCPTLS_H */
