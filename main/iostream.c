/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2015, Digium, Inc.
 *
 * Timo Teräs <timo.teras@iki.fi>
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

#include "asterisk.h"

#include "asterisk/iostream.h"          /* for DO_SSL */

#include <fcntl.h>                      /* for O_NONBLOCK */
#ifdef DO_SSL
#include <openssl/err.h>                /* for ERR_error_string */
#include <openssl/opensslv.h>           /* for OPENSSL_VERSION_NUMBER */
#include <openssl/ssl.h>                /* for SSL_get_error, SSL_free, SSL_... */
#endif
#include <sys/socket.h>                 /* for shutdown, SHUT_RDWR */
#include <sys/time.h>                   /* for timeval */

#include "asterisk/astobj2.h"           /* for ao2_alloc_options, ao2_alloc_... */
#include "asterisk/logger.h"            /* for ast_debug, ast_log, LOG_ERROR */
#include "asterisk/strings.h"           /* for asterisk/threadstorage.h */
#include "asterisk/threadstorage.h"     /* for ast_threadstorage_get, AST_TH... */
#include "asterisk/time.h"              /* for ast_remaining_ms, ast_tvnow */
#include "asterisk/utils.h"             /* for ast_wait_for_input, ast_wait_... */

struct ast_iostream {
	SSL *ssl;
	struct timeval start;
	int fd;
	int timeout;
	int timeout_reset;
	int exclusive_input;
	int rbuflen;
	char *rbufhead;
	char rbuf[2048];
};

#if defined(DO_SSL)
AST_THREADSTORAGE(err2str_threadbuf);
#define ERR2STR_BUFSIZE   128

static const char *ssl_error_to_string(int sslerr, int ret)
{
	switch (sslerr) {
	case SSL_ERROR_SSL:
		return "Internal SSL error";
	case SSL_ERROR_SYSCALL:
		if (!ret) {
			return "System call EOF";
		} else if (ret == -1) {
			char *buf;

			buf = ast_threadstorage_get(&err2str_threadbuf, ERR2STR_BUFSIZE);
			if (!buf) {
				return "Unknown";
			}

			snprintf(buf, ERR2STR_BUFSIZE, "Underlying BIO error: %s", strerror(errno));
			return buf;
		} else {
			return "System call other";
		}
	default:
		break;
	}

	return "Unknown";
}
#endif

int ast_iostream_get_fd(struct ast_iostream *stream)
{
	return stream->fd;
}

void ast_iostream_nonblock(struct ast_iostream *stream)
{
	ast_fd_set_flags(stream->fd, O_NONBLOCK);
}

SSL *ast_iostream_get_ssl(struct ast_iostream *stream)
{
	return stream->ssl;
}

void ast_iostream_set_timeout_disable(struct ast_iostream *stream)
{
	ast_assert(stream != NULL);

	stream->timeout = -1;
	stream->timeout_reset = -1;
}

void ast_iostream_set_timeout_inactivity(struct ast_iostream *stream, int timeout)
{
	ast_assert(stream != NULL);

	stream->start.tv_sec = 0;
	stream->timeout = timeout;
	stream->timeout_reset = timeout;
}

void ast_iostream_set_timeout_idle_inactivity(struct ast_iostream *stream, int timeout, int timeout_reset)
{
	ast_assert(stream != NULL);

	stream->start.tv_sec = 0;
	stream->timeout = timeout;
	stream->timeout_reset = timeout_reset;
}

void ast_iostream_set_timeout_sequence(struct ast_iostream *stream, struct timeval start, int timeout)
{
	ast_assert(stream != NULL);

	stream->start = start;
	stream->timeout = timeout;
	stream->timeout_reset = timeout;
}

void ast_iostream_set_exclusive_input(struct ast_iostream *stream, int exclusive_input)
{
	ast_assert(stream != NULL);

	stream->exclusive_input = exclusive_input;
}

static ssize_t iostream_read(struct ast_iostream *stream, void *buf, size_t size)
{
	struct timeval start;
	int ms;
	int res;

	if (stream->start.tv_sec) {
		start = stream->start;
	} else {
		start = ast_tvnow();
	}

#if defined(DO_SSL)
	if (stream->ssl) {
		for (;;) {
			int sslerr;
			char err[256];
			res = SSL_read(stream->ssl, buf, size);
			if (0 < res) {
				/* We read some payload data. */
				stream->timeout = stream->timeout_reset;
				return res;
			}
			sslerr = SSL_get_error(stream->ssl, res);
			switch (sslerr) {
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
			case SSL_ERROR_SYSCALL:
				/* Some non-recoverable I/O error occurred. The OpenSSL error queue may
				 * contain more information on the error. For socket I/O on Unix systems,
				 * consult errno for details. */
				ast_debug(1, "TLS non-recoverable I/O error occurred: %s, %s\n", ERR_error_string(sslerr, err),
					ssl_error_to_string(sslerr, res));
				return -1;
			default:
				/* Report EOF for an undecoded SSL or transport error. */
				ast_debug(1, "TLS transport or SSL error reading data:  %s, %s\n", ERR_error_string(sslerr, err),
					ssl_error_to_string(sslerr, res));
				return -1;
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
		if (0 <= res) {
			/* Got data or we cannot wait for it. */
			stream->timeout = stream->timeout_reset;
			return res;
		}
		if (!stream->exclusive_input) {
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

ssize_t ast_iostream_read(struct ast_iostream *stream, void *buffer, size_t count)
{
	if (!count) {
		/* You asked for no data you got no data. */
		return 0;
	}

	if (!stream || stream->fd == -1) {
		errno = EBADF;
		return -1;
	}

	/* Get any remains from the read buffer */
	if (stream->rbuflen) {
		size_t r = count;
		if (r > stream->rbuflen) {
			r = stream->rbuflen;
		}
		memcpy(buffer, stream->rbufhead, r);
		stream->rbuflen -= r;
		stream->rbufhead += r;
		return r;
	}

	return iostream_read(stream, buffer, count);
}

ssize_t ast_iostream_gets(struct ast_iostream *stream, char *buffer, size_t size)
{
	size_t remaining = size;
	ssize_t accum_size = 0;
	ssize_t len;
	char *newline;

	for (;;) {
		/* Search for newline */
		newline = memchr(stream->rbufhead, '\n', stream->rbuflen);
		if (newline) {
			len = newline - stream->rbufhead + 1;
			if (len > remaining - 1) {
				len = remaining - 1;
			}
			break;
		}

		/* Enough buffered line data to fill request buffer? */
		if (stream->rbuflen >= remaining - 1) {
			len = remaining - 1;
			break;
		}
		if (stream->rbuflen) {
			/* Put leftover buffered line data into request buffer */
			memcpy(buffer + accum_size, stream->rbufhead, stream->rbuflen);
			remaining -= stream->rbuflen;
			accum_size += stream->rbuflen;
			stream->rbuflen = 0;
		}
		stream->rbufhead = stream->rbuf;

		len = iostream_read(stream, stream->rbuf, sizeof(stream->rbuf));
		if (len == 0) {
			/* Nothing new was read.  Return whatever we have accumulated. */
			break;
		}
		if (len < 0) {
			if (accum_size) {
				/* We have an accumulated buffer so return that instead. */
				len = 0;
				break;
			}
			return len;
		}
		stream->rbuflen += len;
	}

	/* Return read buffer string length */
	memcpy(buffer + accum_size, stream->rbufhead, len);
	buffer[accum_size + len] = 0;
	stream->rbuflen -= len;
	stream->rbufhead += len;

	return accum_size + len;
}

ssize_t ast_iostream_discard(struct ast_iostream *stream, size_t size)
{
	char buf[1024];
	size_t remaining = size;
	ssize_t ret;

	while (remaining) {
		ret = ast_iostream_read(stream, buf, remaining > sizeof(buf) ? sizeof(buf) : remaining);
		if (ret <= 0) {
			return ret;
		}
		remaining -= ret;
	}

	return size;
}

ssize_t ast_iostream_write(struct ast_iostream *stream, const void *buffer, size_t size)
{
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
			int sslerr;
			char err[256];
			res = SSL_write(stream->ssl, buffer + written, remaining);
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
			sslerr = SSL_get_error(stream->ssl, res);
			switch (sslerr) {
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
				ast_debug(1, "TLS transport or SSL error writing data: %s, %s\n", ERR_error_string(sslerr, err),
					ssl_error_to_string(sslerr, res));
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
		res = write(stream->fd, buffer + written, remaining);
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

ssize_t ast_iostream_printf(struct ast_iostream *stream, const char *format, ...)
{
	char sbuf[512], *buf = sbuf;
	int len, len2, ret = -1;
	va_list va;

	va_start(va, format);
	len = vsnprintf(buf, sizeof(sbuf), format, va);
	va_end(va);

	if (len > sizeof(sbuf) - 1) {
		/* Add one to the string length to accommodate the NULL byte */
		size_t buf_len = len + 1;

		buf = ast_malloc(buf_len);
		if (!buf) {
			return -1;
		}
		va_start(va, format);
		len2 = vsnprintf(buf, buf_len, format, va);
		va_end(va);
		if (len2 != len) {
			goto error;
		}
	}

	if (ast_iostream_write(stream, buf, len) == len)
		ret = len;

error:
	if (buf != sbuf) {
		ast_free(buf);
	}

	return ret;
}

int ast_iostream_close(struct ast_iostream *stream)
{
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
				int sslerr = SSL_get_error(stream->ssl, res);
				char err[256];
				ast_log(LOG_ERROR, "SSL_shutdown() failed: %s, %s\n",
					ERR_error_string(sslerr, err), ssl_error_to_string(sslerr, res));
			}

#if !defined(LIBRESSL_VERSION_NUMBER) && (OPENSSL_VERSION_NUMBER >= 0x10100000L)
			if (!SSL_is_server(stream->ssl)) {
#else
			if (!stream->ssl->server) {
#endif
				/* For client threads, ensure that the error stack is cleared */
#if defined(LIBRESSL_VERSION_NUMBER) || (OPENSSL_VERSION_NUMBER < 0x10100000L)
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
				ERR_remove_thread_state(NULL);
#else
				ERR_remove_state(0);
#endif	/* OPENSSL_VERSION_NUMBER >= 0x10000000L */
#endif  /* OPENSSL_VERSION_NUMBER  < 0x10100000L */
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
	ao2_t_ref(stream, -1, "Closed ast_iostream");

	return 0;
}

static void iostream_dtor(void *cookie)
{
#ifdef AST_DEVMODE
	/* Since the ast_assert below is the only one using stream,
	 * and ast_assert is only available with AST_DEVMODE, we
	 * put this in a conditional to avoid compiler warnings. */
	struct ast_iostream *stream = cookie;
#endif

	ast_assert(stream->fd == -1);
}

struct ast_iostream *ast_iostream_from_fd(int *fd)
{
	struct ast_iostream *stream;

	stream = ao2_alloc_options(sizeof(*stream), iostream_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (stream) {
		stream->timeout = -1;
		stream->timeout_reset = -1;
		stream->fd = *fd;
		*fd = -1;
	}

	return stream;
}

int ast_iostream_start_tls(struct ast_iostream **pstream, SSL_CTX *ssl_ctx, int client)
{
#ifdef DO_SSL
	struct ast_iostream *stream = *pstream;
	int (*ssl_setup)(SSL *) = client ? SSL_connect : SSL_accept;
	int res;

	stream->ssl = SSL_new(ssl_ctx);
	if (!stream->ssl) {
		ast_log(LOG_ERROR, "Unable to create new SSL connection\n");
		errno = ENOMEM;
		return -1;
	}

	/*
	 * This function takes struct ast_iostream **, so it can chain
	 * SSL over any ast_iostream. For now we assume it's a file descriptor.
	 * But later this should instead use BIO wrapper to tie SSL to another
	 * ast_iostream.
	 */
	SSL_set_fd(stream->ssl, stream->fd);

	res = ssl_setup(stream->ssl);
	if (res <= 0) {
		int sslerr = SSL_get_error(stream->ssl, res);
		char err[256];

		ast_log(LOG_ERROR, "Problem setting up ssl connection: %s, %s\n",
			ERR_error_string(sslerr, err), ssl_error_to_string(sslerr, res));
		errno = EIO;
		return -1;
	}

	return 0;
#else
	ast_log(LOG_ERROR, "SSL not enabled in this build\n");
	errno = ENOTSUP;
	return -1;
#endif
}
