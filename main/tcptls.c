/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
 *
 * Luigi Rizzo (TCP and TLS server code)
 * Brett Bryant <brettbryant@gmail.com> (updated for client support)
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
 * \brief Code to support TCP and TLS server/client
 *
 * \author Luigi Rizzo
 * \author Brett Bryant <brettbryant@gmail.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <signal.h>
#include <sys/signal.h>

#include "asterisk/compat.h"
#include "asterisk/tcptls.h"
#include "asterisk/http.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/options.h"
#include "asterisk/manager.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"

/*! ao2 object used for the FILE stream fopencookie()/funopen() cookie. */
struct ast_tcptls_stream {
	/*! SSL state if not NULL */
	SSL *ssl;
	/*!
	 * \brief Start time from when an I/O sequence must complete
	 * by struct ast_tcptls_stream.timeout.
	 *
	 * \note If struct ast_tcptls_stream.start.tv_sec is zero then
	 * start time is the current I/O request.
	 */
	struct timeval start;
	/*!
	 * \brief The socket returned by accept().
	 *
	 * \note Set to -1 if the stream is closed.
	 */
	int fd;
	/*!
	 * \brief Timeout in ms relative to struct ast_tcptls_stream.start
	 * to wait for an event on struct ast_tcptls_stream.fd.
	 *
	 * \note Set to -1 to disable timeout.
	 * \note The socket needs to be set to non-blocking for the timeout
	 * feature to work correctly.
	 */
	int timeout;
	/*! TRUE if stream can exclusively wait for fd input. */
	int exclusive_input;
};

void ast_tcptls_stream_set_timeout_disable(struct ast_tcptls_stream *stream)
{
	ast_assert(stream != NULL);

	stream->timeout = -1;
}

void ast_tcptls_stream_set_timeout_inactivity(struct ast_tcptls_stream *stream, int timeout)
{
	ast_assert(stream != NULL);

	stream->start.tv_sec = 0;
	stream->timeout = timeout;
}

void ast_tcptls_stream_set_timeout_sequence(struct ast_tcptls_stream *stream, struct timeval start, int timeout)
{
	ast_assert(stream != NULL);

	stream->start = start;
	stream->timeout = timeout;
}

void ast_tcptls_stream_set_exclusive_input(struct ast_tcptls_stream *stream, int exclusive_input)
{
	ast_assert(stream != NULL);

	stream->exclusive_input = exclusive_input;
}

/*!
 * \internal
 * \brief fopencookie()/funopen() stream read function.
 *
 * \param cookie Stream control data.
 * \param buf Where to put read data.
 * \param size Size of the buffer.
 *
 * \retval number of bytes put into buf.
 * \retval 0 on end of file.
 * \retval -1 on error.
 */
static HOOK_T tcptls_stream_read(void *cookie, char *buf, LEN_T size)
{
	struct ast_tcptls_stream *stream = cookie;
	struct timeval start;
	int ms;
	int res;

	if (!size) {
		/* You asked for no data you got no data. */
		return 0;
	}

	if (!stream || stream->fd == -1) {
		errno = EBADF;
		return -1;
	}

	if (stream->start.tv_sec) {
		start = stream->start;
	} else {
		start = ast_tvnow();
	}

#if defined(DO_SSL)
	if (stream->ssl) {
		for (;;) {
			res = SSL_read(stream->ssl, buf, size);
			if (0 < res) {
				/* We read some payload data. */
				return res;
			}
			switch (SSL_get_error(stream->ssl, res)) {
			case SSL_ERROR_ZERO_RETURN:
				/* Report EOF for a shutdown */
				ast_debug(1, "TLS clean shutdown alert reading data\n");
				return 0;
			case SSL_ERROR_WANT_READ:
				if (!stream->exclusive_input) {
					/* We cannot wait for data now. */
					errno = EAGAIN;
					return -1;
				}
				while ((ms = ast_remaining_ms(start, stream->timeout))) {
					res = ast_wait_for_input(stream->fd, ms);
					if (0 < res) {
						/* Socket is ready to be read. */
						break;
					}
					if (res < 0) {
						if (errno == EINTR || errno == EAGAIN) {
							/* Try again. */
							continue;
						}
						ast_debug(1, "TLS socket error waiting for read data: %s\n",
							strerror(errno));
						return -1;
					}
				}
				break;
			case SSL_ERROR_WANT_WRITE:
				while ((ms = ast_remaining_ms(start, stream->timeout))) {
					res = ast_wait_for_output(stream->fd, ms);
					if (0 < res) {
						/* Socket is ready to be written. */
						break;
					}
					if (res < 0) {
						if (errno == EINTR || errno == EAGAIN) {
							/* Try again. */
							continue;
						}
						ast_debug(1, "TLS socket error waiting for write space: %s\n",
							strerror(errno));
						return -1;
					}
				}
				break;
			default:
				/* Report EOF for an undecoded SSL or transport error. */
				ast_debug(1, "TLS transport or SSL error reading data\n");
				return 0;
			}
			if (!ms) {
				/* Report EOF for a timeout */
				ast_debug(1, "TLS timeout reading data\n");
				return 0;
			}
		}
	}
#endif	/* defined(DO_SSL) */

	for (;;) {
		res = read(stream->fd, buf, size);
		if (0 <= res || !stream->exclusive_input) {
			/* Got data or we cannot wait for it. */
			return res;
		}
		if (errno != EINTR && errno != EAGAIN) {
			/* Not a retryable error. */
			ast_debug(1, "TCP socket error reading data: %s\n",
				strerror(errno));
			return -1;
		}
		ms = ast_remaining_ms(start, stream->timeout);
		if (!ms) {
			/* Report EOF for a timeout */
			ast_debug(1, "TCP timeout reading data\n");
			return 0;
		}
		ast_wait_for_input(stream->fd, ms);
	}
}

/*!
 * \internal
 * \brief fopencookie()/funopen() stream write function.
 *
 * \param cookie Stream control data.
 * \param buf Where to get data to write.
 * \param size Size of the buffer.
 *
 * \retval number of bytes written from buf.
 * \retval -1 on error.
 */
static HOOK_T tcptls_stream_write(void *cookie, const char *buf, LEN_T size)
{
	struct ast_tcptls_stream *stream = cookie;
	struct timeval start;
	int ms;
	int res;
	int written;
	int remaining;

	if (!size) {
		/* You asked to write no data you wrote no data. */
		return 0;
	}

	if (!stream || stream->fd == -1) {
		errno = EBADF;
		return -1;
	}

	if (stream->start.tv_sec) {
		start = stream->start;
	} else {
		start = ast_tvnow();
	}

#if defined(DO_SSL)
	if (stream->ssl) {
		written = 0;
		remaining = size;
		for (;;) {
			res = SSL_write(stream->ssl, buf + written, remaining);
			if (res == remaining) {
				/* Everything was written. */
				return size;
			}
			if (0 < res) {
				/* Successfully wrote part of the buffer.  Try to write the rest. */
				written += res;
				remaining -= res;
				continue;
			}
			switch (SSL_get_error(stream->ssl, res)) {
			case SSL_ERROR_ZERO_RETURN:
				ast_debug(1, "TLS clean shutdown alert writing data\n");
				if (written) {
					/* Report partial write. */
					return written;
				}
				errno = EBADF;
				return -1;
			case SSL_ERROR_WANT_READ:
				ms = ast_remaining_ms(start, stream->timeout);
				if (!ms) {
					/* Report partial write. */
					ast_debug(1, "TLS timeout writing data (want read)\n");
					return written;
				}
				ast_wait_for_input(stream->fd, ms);
				break;
			case SSL_ERROR_WANT_WRITE:
				ms = ast_remaining_ms(start, stream->timeout);
				if (!ms) {
					/* Report partial write. */
					ast_debug(1, "TLS timeout writing data (want write)\n");
					return written;
				}
				ast_wait_for_output(stream->fd, ms);
				break;
			default:
				/* Undecoded SSL or transport error. */
				ast_debug(1, "TLS transport or SSL error writing data\n");
				if (written) {
					/* Report partial write. */
					return written;
				}
				errno = EBADF;
				return -1;
			}
		}
	}
#endif	/* defined(DO_SSL) */

	written = 0;
	remaining = size;
	for (;;) {
		res = write(stream->fd, buf + written, remaining);
		if (res == remaining) {
			/* Yay everything was written. */
			return size;
		}
		if (0 < res) {
			/* Successfully wrote part of the buffer.  Try to write the rest. */
			written += res;
			remaining -= res;
			continue;
		}
		if (errno != EINTR && errno != EAGAIN) {
			/* Not a retryable error. */
			ast_debug(1, "TCP socket error writing: %s\n", strerror(errno));
			if (written) {
				return written;
			}
			return -1;
		}
		ms = ast_remaining_ms(start, stream->timeout);
		if (!ms) {
			/* Report partial write. */
			ast_debug(1, "TCP timeout writing data\n");
			return written;
		}
		ast_wait_for_output(stream->fd, ms);
	}
}

/*!
 * \internal
 * \brief fopencookie()/funopen() stream close function.
 *
 * \param cookie Stream control data.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int tcptls_stream_close(void *cookie)
{
	struct ast_tcptls_stream *stream = cookie;

	if (!stream) {
		errno = EBADF;
		return -1;
	}

	if (stream->fd != -1) {
#if defined(DO_SSL)
		if (stream->ssl) {
			int res;

			/*
			 * According to the TLS standard, it is acceptable for an
			 * application to only send its shutdown alert and then
			 * close the underlying connection without waiting for
			 * the peer's response (this way resources can be saved,
			 * as the process can already terminate or serve another
			 * connection).
			 */
			res = SSL_shutdown(stream->ssl);
			if (res < 0) {
				ast_log(LOG_ERROR, "SSL_shutdown() failed: %d\n",
					SSL_get_error(stream->ssl, res));
			}

			if (!stream->ssl->server) {
				/* For client threads, ensure that the error stack is cleared */
				ERR_remove_state(0);
			}

			SSL_free(stream->ssl);
			stream->ssl = NULL;
		}
#endif	/* defined(DO_SSL) */

		/*
		 * Issuing shutdown() is necessary here to avoid a race
		 * condition where the last data written may not appear
		 * in the TCP stream.  See ASTERISK-23548
		 */
		shutdown(stream->fd, SHUT_RDWR);
		if (close(stream->fd)) {
			ast_log(LOG_ERROR, "close() failed: %s\n", strerror(errno));
		}
		stream->fd = -1;
	}
	ao2_t_ref(stream, -1, "Closed tcptls stream cookie");

	return 0;
}

/*!
 * \internal
 * \brief fopencookie()/funopen() stream destructor function.
 *
 * \param cookie Stream control data.
 *
 * \return Nothing
 */
static void tcptls_stream_dtor(void *cookie)
{
	struct ast_tcptls_stream *stream = cookie;

	ast_assert(stream->fd == -1);
}

/*!
 * \internal
 * \brief fopencookie()/funopen() stream allocation function.
 *
 * \retval stream_cookie on success.
 * \retval NULL on error.
 */
static struct ast_tcptls_stream *tcptls_stream_alloc(void)
{
	struct ast_tcptls_stream *stream;

	stream = ao2_alloc_options(sizeof(*stream), tcptls_stream_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (stream) {
		stream->fd = -1;
		stream->timeout = -1;
	}
	return stream;
}

/*!
 * \internal
 * \brief Open a custom FILE stream for tcptls.
 *
 * \param stream Stream cookie control data.
 * \param ssl SSL state if not NULL.
 * \param fd Socket file descriptor.
 * \param timeout ms to wait for an event on fd. -1 if timeout disabled.
 *
 * \retval fp on success.
 * \retval NULL on error.
 */
static FILE *tcptls_stream_fopen(struct ast_tcptls_stream *stream, SSL *ssl, int fd, int timeout)
{
	FILE *fp;

#if defined(HAVE_FOPENCOOKIE)	/* the glibc/linux interface */
	static const cookie_io_functions_t cookie_funcs = {
		tcptls_stream_read,
		tcptls_stream_write,
		NULL,
		tcptls_stream_close
	};
#endif	/* defined(HAVE_FOPENCOOKIE) */

	if (fd == -1) {
		/* Socket not open. */
		return NULL;
	}

	stream->ssl = ssl;
	stream->fd = fd;
	stream->timeout = timeout;
	ao2_t_ref(stream, +1, "Opening tcptls stream cookie");

#if defined(HAVE_FUNOPEN)	/* the BSD interface */
	fp = funopen(stream, tcptls_stream_read, tcptls_stream_write, NULL,
		tcptls_stream_close);
#elif defined(HAVE_FOPENCOOKIE)	/* the glibc/linux interface */
	fp = fopencookie(stream, "w+", cookie_funcs);
#else
	/* could add other methods here */
	ast_debug(2, "No stream FILE methods attempted!\n");
	fp = NULL;
#endif

	if (!fp) {
		stream->fd = -1;
		ao2_t_ref(stream, -1, "Failed to open tcptls stream cookie");
	}
	return fp;
}

HOOK_T ast_tcptls_server_read(struct ast_tcptls_session_instance *tcptls_session, void *buf, size_t count)
{
	if (!tcptls_session->stream_cookie || tcptls_session->stream_cookie->fd == -1) {
		ast_log(LOG_ERROR, "TCP/TLS read called on invalid stream.\n");
		errno = EIO;
		return -1;
	}

	return tcptls_stream_read(tcptls_session->stream_cookie, buf, count);
}

HOOK_T ast_tcptls_server_write(struct ast_tcptls_session_instance *tcptls_session, const void *buf, size_t count)
{
	if (!tcptls_session->stream_cookie || tcptls_session->stream_cookie->fd == -1) {
		ast_log(LOG_ERROR, "TCP/TLS write called on invalid stream.\n");
		errno = EIO;
		return -1;
	}

	return tcptls_stream_write(tcptls_session->stream_cookie, buf, count);
}

static void session_instance_destructor(void *obj)
{
	struct ast_tcptls_session_instance *i = obj;

	if (i->stream_cookie) {
		ao2_t_ref(i->stream_cookie, -1, "Destroying tcptls session instance");
		i->stream_cookie = NULL;
	}
	ast_free(i->overflow_buf);
}

/*! \brief
* creates a FILE * from the fd passed by the accept thread.
* This operation is potentially expensive (certificate verification),
* so we do it in the child thread context.
*
* \note must decrement ref count before returning NULL on error
*/
static void *handle_tcptls_connection(void *data)
{
	struct ast_tcptls_session_instance *tcptls_session = data;
#ifdef DO_SSL
	int (*ssl_setup)(SSL *) = (tcptls_session->client) ? SSL_connect : SSL_accept;
	int ret;
	char err[256];
#endif

	/* TCP/TLS connections are associated with external protocols, and
	 * should not be allowed to execute 'dangerous' functions. This may
	 * need to be pushed down into the individual protocol handlers, but
	 * this seems like a good general policy.
	 */
	if (ast_thread_inhibit_escalations()) {
		ast_log(LOG_ERROR, "Failed to inhibit privilege escalations; killing connection\n");
		ast_tcptls_close_session_file(tcptls_session);
		ao2_ref(tcptls_session, -1);
		return NULL;
	}

	tcptls_session->stream_cookie = tcptls_stream_alloc();
	if (!tcptls_session->stream_cookie) {
		ast_tcptls_close_session_file(tcptls_session);
		ao2_ref(tcptls_session, -1);
		return NULL;
	}

	/*
	* open a FILE * as appropriate.
	*/
	if (!tcptls_session->parent->tls_cfg) {
		tcptls_session->f = tcptls_stream_fopen(tcptls_session->stream_cookie, NULL,
			tcptls_session->fd, -1);
		if (tcptls_session->f) {
			if (setvbuf(tcptls_session->f, NULL, _IONBF, 0)) {
				ast_tcptls_close_session_file(tcptls_session);
			}
		}
	}
#ifdef DO_SSL
	else if ( (tcptls_session->ssl = SSL_new(tcptls_session->parent->tls_cfg->ssl_ctx)) ) {
		SSL_set_fd(tcptls_session->ssl, tcptls_session->fd);
		if ((ret = ssl_setup(tcptls_session->ssl)) <= 0) {
			ast_verb(2, "Problem setting up ssl connection: %s\n", ERR_error_string(ERR_get_error(), err));
		} else if ((tcptls_session->f = tcptls_stream_fopen(tcptls_session->stream_cookie,
			tcptls_session->ssl, tcptls_session->fd, -1))) {
			if ((tcptls_session->client && !ast_test_flag(&tcptls_session->parent->tls_cfg->flags, AST_SSL_DONT_VERIFY_SERVER))
				|| (!tcptls_session->client && ast_test_flag(&tcptls_session->parent->tls_cfg->flags, AST_SSL_VERIFY_CLIENT))) {
				X509 *peer;
				long res;
				peer = SSL_get_peer_certificate(tcptls_session->ssl);
				if (!peer) {
					ast_log(LOG_ERROR, "No peer SSL certificate to verify\n");
					ast_tcptls_close_session_file(tcptls_session);
					ao2_ref(tcptls_session, -1);
					return NULL;
				}

				res = SSL_get_verify_result(tcptls_session->ssl);
				if (res != X509_V_OK) {
					ast_log(LOG_ERROR, "Certificate did not verify: %s\n", X509_verify_cert_error_string(res));
					X509_free(peer);
					ast_tcptls_close_session_file(tcptls_session);
					ao2_ref(tcptls_session, -1);
					return NULL;
				}
				if (!ast_test_flag(&tcptls_session->parent->tls_cfg->flags, AST_SSL_IGNORE_COMMON_NAME)) {
					ASN1_STRING *str;
					unsigned char *str2;
					X509_NAME *name = X509_get_subject_name(peer);
					int pos = -1;
					int found = 0;

					for (;;) {
						/* Walk the certificate to check all available "Common Name" */
						/* XXX Probably should do a gethostbyname on the hostname and compare that as well */
						pos = X509_NAME_get_index_by_NID(name, NID_commonName, pos);
						if (pos < 0) {
							break;
						}
						str = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, pos));
						ret = ASN1_STRING_to_UTF8(&str2, str);
						if (ret < 0) {
							continue;
						}

						if (str2) {
							if (strlen((char *) str2) != ret) {
								ast_log(LOG_WARNING, "Invalid certificate common name length (contains NULL bytes?)\n");
							} else if (!strcasecmp(tcptls_session->parent->hostname, (char *) str2)) {
								found = 1;
							}
							ast_debug(3, "SSL Common Name compare s1='%s' s2='%s'\n", tcptls_session->parent->hostname, str2);
							OPENSSL_free(str2);
						}
						if (found) {
							break;
						}
					}
					if (!found) {
						ast_log(LOG_ERROR, "Certificate common name did not match (%s)\n", tcptls_session->parent->hostname);
						X509_free(peer);
						ast_tcptls_close_session_file(tcptls_session);
						ao2_ref(tcptls_session, -1);
						return NULL;
					}
				}
				X509_free(peer);
			}
		}
		if (!tcptls_session->f) {	/* no success opening descriptor stacking */
			SSL_free(tcptls_session->ssl);
		}
	}
#endif /* DO_SSL */

	if (!tcptls_session->f) {
		ast_tcptls_close_session_file(tcptls_session);
		ast_log(LOG_WARNING, "FILE * open failed!\n");
#ifndef DO_SSL
		if (tcptls_session->parent->tls_cfg) {
			ast_log(LOG_WARNING, "Attempted a TLS connection without OpenSSL support. This will not work!\n");
		}
#endif
		ao2_ref(tcptls_session, -1);
		return NULL;
	}

	if (tcptls_session->parent->worker_fn) {
		return tcptls_session->parent->worker_fn(tcptls_session);
	} else {
		return tcptls_session;
	}
}

void *ast_tcptls_server_root(void *data)
{
	struct ast_tcptls_session_args *desc = data;
	int fd;
	struct ast_sockaddr addr;
	struct ast_tcptls_session_instance *tcptls_session;
	pthread_t launched;

	for (;;) {
		int i, flags;

		if (desc->periodic_fn) {
			desc->periodic_fn(desc);
		}
		i = ast_wait_for_input(desc->accept_fd, desc->poll_timeout);
		if (i <= 0) {
			continue;
		}
		fd = ast_accept(desc->accept_fd, &addr);
		if (fd < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				ast_log(LOG_WARNING, "Accept failed: %s\n", strerror(errno));
			}
			continue;
		}
		tcptls_session = ao2_alloc(sizeof(*tcptls_session), session_instance_destructor);
		if (!tcptls_session) {
			ast_log(LOG_WARNING, "No memory for new session: %s\n", strerror(errno));
			if (close(fd)) {
				ast_log(LOG_ERROR, "close() failed: %s\n", strerror(errno));
			}
			continue;
		}

		tcptls_session->overflow_buf = ast_str_create(128);
		flags = fcntl(fd, F_GETFL);
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
		tcptls_session->fd = fd;
		tcptls_session->parent = desc;
		ast_sockaddr_copy(&tcptls_session->remote_address, &addr);

		tcptls_session->client = 0;

		/* This thread is now the only place that controls the single ref to tcptls_session */
		if (ast_pthread_create_detached_background(&launched, NULL, handle_tcptls_connection, tcptls_session)) {
			ast_log(LOG_WARNING, "Unable to launch helper thread: %s\n", strerror(errno));
			ast_tcptls_close_session_file(tcptls_session);
			ao2_ref(tcptls_session, -1);
		}
	}
	return NULL;
}

static int __ssl_setup(struct ast_tls_config *cfg, int client)
{
#ifndef DO_SSL
	cfg->enabled = 0;
	return 0;
#else
	int disable_ssl = 0;
 
	if (!cfg->enabled) {
		return 0;
	}

	/* Get rid of an old SSL_CTX since we're about to
	 * allocate a new one
	 */
	if (cfg->ssl_ctx) {
		SSL_CTX_free(cfg->ssl_ctx);
		cfg->ssl_ctx = NULL;
	}

	if (client) {
#ifndef OPENSSL_NO_SSL2
		if (ast_test_flag(&cfg->flags, AST_SSL_SSLV2_CLIENT)) {
			ast_log(LOG_WARNING, "Usage of SSLv2 is discouraged due to known vulnerabilities. Please use 'tlsv1' or leave the TLS method unspecified!\n");
			cfg->ssl_ctx = SSL_CTX_new(SSLv2_client_method());
		} else
#endif
		if (ast_test_flag(&cfg->flags, AST_SSL_SSLV3_CLIENT)) {
			ast_log(LOG_WARNING, "Usage of SSLv3 is discouraged due to known vulnerabilities. Please use 'tlsv1' or leave the TLS method unspecified!\n");
			cfg->ssl_ctx = SSL_CTX_new(SSLv3_client_method());
		} else if (ast_test_flag(&cfg->flags, AST_SSL_TLSV1_CLIENT)) {
			cfg->ssl_ctx = SSL_CTX_new(TLSv1_client_method());
		} else {
			disable_ssl = 1;
			cfg->ssl_ctx = SSL_CTX_new(SSLv23_client_method());
		}
	} else {
		disable_ssl = 1;
		cfg->ssl_ctx = SSL_CTX_new(SSLv23_server_method());
	}

	if (!cfg->ssl_ctx) {
		ast_debug(1, "Sorry, SSL_CTX_new call returned null...\n");
		cfg->enabled = 0;
		return 0;
	}

	/* Due to the POODLE vulnerability, completely disable
	 * SSLv2 and SSLv3 if we are not explicitly told to use
	 * them. SSLv23_*_method supports TLSv1+.
	 */
	if (disable_ssl) {
		long ssl_opts;

		ssl_opts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
		SSL_CTX_set_options(cfg->ssl_ctx, ssl_opts);
	}

	SSL_CTX_set_verify(cfg->ssl_ctx,
		ast_test_flag(&cfg->flags, AST_SSL_VERIFY_CLIENT) ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT : SSL_VERIFY_NONE,
		NULL);

	if (!ast_strlen_zero(cfg->certfile)) {
		char *tmpprivate = ast_strlen_zero(cfg->pvtfile) ? cfg->certfile : cfg->pvtfile;
		if (SSL_CTX_use_certificate_file(cfg->ssl_ctx, cfg->certfile, SSL_FILETYPE_PEM) == 0) {
			if (!client) {
				/* Clients don't need a certificate, but if its setup we can use it */
				ast_verb(0, "SSL error loading cert file. <%s>\n", cfg->certfile);
				cfg->enabled = 0;
				SSL_CTX_free(cfg->ssl_ctx);
				cfg->ssl_ctx = NULL;
				return 0;
			}
		}
		if ((SSL_CTX_use_PrivateKey_file(cfg->ssl_ctx, tmpprivate, SSL_FILETYPE_PEM) == 0) || (SSL_CTX_check_private_key(cfg->ssl_ctx) == 0 )) {
			if (!client) {
				/* Clients don't need a private key, but if its setup we can use it */
				ast_verb(0, "SSL error loading private key file. <%s>\n", tmpprivate);
				cfg->enabled = 0;
				SSL_CTX_free(cfg->ssl_ctx);
				cfg->ssl_ctx = NULL;
				return 0;
			}
		}
	}
	if (!ast_strlen_zero(cfg->cipher)) {
		if (SSL_CTX_set_cipher_list(cfg->ssl_ctx, cfg->cipher) == 0 ) {
			if (!client) {
				ast_verb(0, "SSL cipher error <%s>\n", cfg->cipher);
				cfg->enabled = 0;
				SSL_CTX_free(cfg->ssl_ctx);
				cfg->ssl_ctx = NULL;
				return 0;
			}
		}
	}
	if (!ast_strlen_zero(cfg->cafile) || !ast_strlen_zero(cfg->capath)) {
		if (SSL_CTX_load_verify_locations(cfg->ssl_ctx, S_OR(cfg->cafile, NULL), S_OR(cfg->capath,NULL)) == 0) {
			ast_verb(0, "SSL CA file(%s)/path(%s) error\n", cfg->cafile, cfg->capath);
		}
	}

	ast_verb(0, "SSL certificate ok\n");
	return 1;
#endif
}

int ast_ssl_setup(struct ast_tls_config *cfg)
{
	return __ssl_setup(cfg, 0);
}

void ast_ssl_teardown(struct ast_tls_config *cfg)
{
#ifdef DO_SSL
	if (cfg->ssl_ctx) {
		SSL_CTX_free(cfg->ssl_ctx);
		cfg->ssl_ctx = NULL;
	}
#endif
}

struct ast_tcptls_session_instance *ast_tcptls_client_start(struct ast_tcptls_session_instance *tcptls_session)
{
	struct ast_tcptls_session_args *desc;
	int flags;

	if (!(desc = tcptls_session->parent)) {
		goto client_start_error;
	}

	if (ast_connect(desc->accept_fd, &desc->remote_address)) {
		ast_log(LOG_ERROR, "Unable to connect %s to %s: %s\n",
			desc->name,
			ast_sockaddr_stringify(&desc->remote_address),
			strerror(errno));
		goto client_start_error;
	}

	flags = fcntl(desc->accept_fd, F_GETFL);
	fcntl(desc->accept_fd, F_SETFL, flags & ~O_NONBLOCK);

	if (desc->tls_cfg) {
		desc->tls_cfg->enabled = 1;
		__ssl_setup(desc->tls_cfg, 1);
	}

	return handle_tcptls_connection(tcptls_session);

client_start_error:
	if (desc) {
		close(desc->accept_fd);
		desc->accept_fd = -1;
	}
	ao2_ref(tcptls_session, -1);
	return NULL;

}

struct ast_tcptls_session_instance *ast_tcptls_client_create(struct ast_tcptls_session_args *desc)
{
	int x = 1;
	struct ast_tcptls_session_instance *tcptls_session = NULL;

	/* Do nothing if nothing has changed */
	if (!ast_sockaddr_cmp(&desc->old_address, &desc->remote_address)) {
		ast_debug(1, "Nothing changed in %s\n", desc->name);
		return NULL;
	}

	/* If we return early, there is no connection */
	ast_sockaddr_setnull(&desc->old_address);

	if (desc->accept_fd != -1) {
		close(desc->accept_fd);
	}

	desc->accept_fd = socket(ast_sockaddr_is_ipv6(&desc->remote_address) ?
				 AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (desc->accept_fd < 0) {
		ast_log(LOG_WARNING, "Unable to allocate socket for %s: %s\n",
			desc->name, strerror(errno));
		return NULL;
	}

	/* if a local address was specified, bind to it so the connection will
	   originate from the desired address */
	if (!ast_sockaddr_isnull(&desc->local_address)) {
		setsockopt(desc->accept_fd, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
		if (ast_bind(desc->accept_fd, &desc->local_address)) {
			ast_log(LOG_ERROR, "Unable to bind %s to %s: %s\n",
				desc->name,
				ast_sockaddr_stringify(&desc->local_address),
				strerror(errno));
			goto error;
		}
	}

	if (!(tcptls_session = ao2_alloc(sizeof(*tcptls_session), session_instance_destructor))) {
		goto error;
	}

	tcptls_session->overflow_buf = ast_str_create(128);
	tcptls_session->client = 1;
	tcptls_session->fd = desc->accept_fd;
	tcptls_session->parent = desc;
	tcptls_session->parent->worker_fn = NULL;
	ast_sockaddr_copy(&tcptls_session->remote_address,
			  &desc->remote_address);

	/* Set current info */
	ast_sockaddr_copy(&desc->old_address, &desc->remote_address);
	return tcptls_session;

error:
	close(desc->accept_fd);
	desc->accept_fd = -1;
	if (tcptls_session) {
		ao2_ref(tcptls_session, -1);
	}
	return NULL;
}

void ast_tcptls_server_start(struct ast_tcptls_session_args *desc)
{
	int flags;
	int x = 1;

	/* Do nothing if nothing has changed */
	if (!ast_sockaddr_cmp(&desc->old_address, &desc->local_address)) {
		ast_debug(1, "Nothing changed in %s\n", desc->name);
		return;
	}

	/* If we return early, there is no one listening */
	ast_sockaddr_setnull(&desc->old_address);

	/* Shutdown a running server if there is one */
	if (desc->master != AST_PTHREADT_NULL) {
		pthread_cancel(desc->master);
		pthread_kill(desc->master, SIGURG);
		pthread_join(desc->master, NULL);
	}

	if (desc->accept_fd != -1) {
		close(desc->accept_fd);
	}

	/* If there's no new server, stop here */
	if (ast_sockaddr_isnull(&desc->local_address)) {
		ast_debug(2, "Server disabled:  %s\n", desc->name);
		return;
	}

	desc->accept_fd = socket(ast_sockaddr_is_ipv6(&desc->local_address) ?
				 AF_INET6 : AF_INET, SOCK_STREAM, 0);
	if (desc->accept_fd < 0) {
		ast_log(LOG_ERROR, "Unable to allocate socket for %s: %s\n", desc->name, strerror(errno));
		return;
	}

	setsockopt(desc->accept_fd, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
	if (ast_bind(desc->accept_fd, &desc->local_address)) {
		ast_log(LOG_ERROR, "Unable to bind %s to %s: %s\n",
			desc->name,
			ast_sockaddr_stringify(&desc->local_address),
			strerror(errno));
		goto error;
	}
	if (listen(desc->accept_fd, 10)) {
		ast_log(LOG_ERROR, "Unable to listen for %s!\n", desc->name);
		goto error;
	}
	flags = fcntl(desc->accept_fd, F_GETFL);
	fcntl(desc->accept_fd, F_SETFL, flags | O_NONBLOCK);
	if (ast_pthread_create_background(&desc->master, NULL, desc->accept_fn, desc)) {
		ast_log(LOG_ERROR, "Unable to launch thread for %s on %s: %s\n",
			desc->name,
			ast_sockaddr_stringify(&desc->local_address),
			strerror(errno));
		goto error;
	}

	/* Set current info */
	ast_sockaddr_copy(&desc->old_address, &desc->local_address);

	return;

error:
	close(desc->accept_fd);
	desc->accept_fd = -1;
}

void ast_tcptls_close_session_file(struct ast_tcptls_session_instance *tcptls_session)
{
	if (tcptls_session->f) {
		fflush(tcptls_session->f);
		if (fclose(tcptls_session->f)) {
			ast_log(LOG_ERROR, "fclose() failed: %s\n", strerror(errno));
		}
		tcptls_session->f = NULL;
		tcptls_session->fd = -1;
	} else if (tcptls_session->fd != -1) {
		/*
		 * Issuing shutdown() is necessary here to avoid a race
		 * condition where the last data written may not appear
		 * in the TCP stream.  See ASTERISK-23548
		 */
		shutdown(tcptls_session->fd, SHUT_RDWR);
		if (close(tcptls_session->fd)) {
			ast_log(LOG_ERROR, "close() failed: %s\n", strerror(errno));
		}
		tcptls_session->fd = -1;
	} else {
		ast_log(LOG_ERROR, "ast_tcptls_close_session_file invoked on session instance without file or file descriptor\n");
	}
}

void ast_tcptls_server_stop(struct ast_tcptls_session_args *desc)
{
	if (desc->master != AST_PTHREADT_NULL) {
		pthread_cancel(desc->master);
		pthread_kill(desc->master, SIGURG);
		pthread_join(desc->master, NULL);
		desc->master = AST_PTHREADT_NULL;
	}
	if (desc->accept_fd != -1) {
		close(desc->accept_fd);
	}
	desc->accept_fd = -1;
	ast_debug(2, "Stopped server :: %s\n", desc->name);
}

int ast_tls_read_conf(struct ast_tls_config *tls_cfg, struct ast_tcptls_session_args *tls_desc, const char *varname, const char *value)
{
	if (!strcasecmp(varname, "tlsenable") || !strcasecmp(varname, "sslenable")) {
		tls_cfg->enabled = ast_true(value) ? 1 : 0;
	} else if (!strcasecmp(varname, "tlscertfile") || !strcasecmp(varname, "sslcert") || !strcasecmp(varname, "tlscert")) {
		ast_free(tls_cfg->certfile);
		tls_cfg->certfile = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlsprivatekey") || !strcasecmp(varname, "sslprivatekey")) {
		ast_free(tls_cfg->pvtfile);
		tls_cfg->pvtfile = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlscipher") || !strcasecmp(varname, "sslcipher")) {
		ast_free(tls_cfg->cipher);
		tls_cfg->cipher = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlscafile")) {
		ast_free(tls_cfg->cafile);
		tls_cfg->cafile = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlscapath") || !strcasecmp(varname, "tlscadir")) {
		ast_free(tls_cfg->capath);
		tls_cfg->capath = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlsverifyclient")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_VERIFY_CLIENT);
	} else if (!strcasecmp(varname, "tlsdontverifyserver")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_DONT_VERIFY_SERVER);
	} else if (!strcasecmp(varname, "tlsbindaddr") || !strcasecmp(varname, "sslbindaddr")) {
		if (ast_parse_arg(value, PARSE_ADDR, &tls_desc->local_address))
			ast_log(LOG_WARNING, "Invalid %s '%s'\n", varname, value);
	} else if (!strcasecmp(varname, "tlsclientmethod") || !strcasecmp(varname, "sslclientmethod")) {
		if (!strcasecmp(value, "tlsv1")) {
			ast_set_flag(&tls_cfg->flags, AST_SSL_TLSV1_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV3_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV2_CLIENT);
		} else if (!strcasecmp(value, "sslv3")) {
			ast_set_flag(&tls_cfg->flags, AST_SSL_SSLV3_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV2_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_TLSV1_CLIENT);
		} else if (!strcasecmp(value, "sslv2")) {
			ast_set_flag(&tls_cfg->flags, AST_SSL_SSLV2_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_TLSV1_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV3_CLIENT);
		}
	} else {
		return -1;
	}

	return 0;
}
