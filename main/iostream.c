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

#include <fcntl.h>

#include "asterisk.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/iostream.h"

struct ast_iostream {
	SSL *ssl;
	struct timeval start;
	int fd;
	int timeout;
	int timeout_reset;
	int exclusive_input;
	int rbuflen;
	char rbuf[512];
};

int ast_iostream_get_fd(struct ast_iostream *stream)
{
	return stream->fd;
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
			res = SSL_read(stream->ssl, buf, size);
			if (0 < res) {
				/* We read some payload data. */
				stream->timeout = stream->timeout_reset;
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

ssize_t ast_iostream_read(struct ast_iostream *stream, void *buf, size_t size)
{
	if (!size) {
		/* You asked for no data you got no data. */
		return 0;
	}

	if (!stream || stream->fd == -1) {
		errno = EBADF;
		return -1;
	}

	/* Get any remains from the read buffer */
	if (stream->rbuflen) {
		size_t r = size;
		if (stream->rbuflen < r) {
			r = stream->rbuflen;
		}
		memcpy(buf, stream->rbuf, r);
		stream->rbuflen -= r;
		if (stream->rbuflen) {
			memmove(stream->rbuf, &stream->rbuf[r], stream->rbuflen);
		}
		return r;
	}

	return iostream_read(stream, buf, size);
}

ssize_t ast_iostream_gets(struct ast_iostream *stream, char *buf, size_t count)
{
	ssize_t r;
	char *newline;

	do {
		/* Search for newline */
		newline = memchr(stream->rbuf, '\n', stream->rbuflen);
		if (newline) {
			r = newline - stream->rbuf;
			if (r > count-1) {
				r = count-1;
			}
			break;
		}

		/* Enough data? */
		if (stream->rbuflen >= count - 1) {
			r = count - 1;
			break;
		}

		/* Try to fill in line buffer */
		r = iostream_read(stream, &stream->rbuf[stream->rbuflen], sizeof(stream->rbuf) - stream->rbuflen);
		if (r <= 0) {
			return r;
		}
		stream->rbuflen += r;
	} while (1);

	/* Return r bytes with termination byte */
	memcpy(buf, stream->rbuf, r);
	buf[r] = 0;
	stream->rbuflen -= r;
	if (stream->rbuflen) {
		memmove(stream->rbuf, &stream->rbuf[r], stream->rbuflen);
	}

	return r + 1;
}

ssize_t ast_iostream_discard(struct ast_iostream *stream, size_t size)
{
	char buf[1024];
	size_t remaining = size;
	ssize_t ret;

	while (remaining) {
		ret = ast_iostream_read(stream, buf, remaining > sizeof(buf) ? sizeof(buf) : remaining);
		if (ret < 0) {
			return ret;
		}
		remaining -= ret;
	}

	return size;
}

ssize_t ast_iostream_write(struct ast_iostream *stream, const void *buf, size_t size)
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
	ao2_t_ref(stream, -1, "Closed ast_iostream");

	return 0;
}

static void tcptls_stream_dtor(void *cookie)
{
#ifdef AST_DEVMODE
	/* Since the ast_assert below is the only one using stream,
	 * and ast_assert is only available with AST_DEVMODE, we
	 * put this in a conditional to avoid compiler warnings. */
	struct ast_iostream *stream = cookie;
#endif

	ast_assert(stream->fd == -1);
}

static struct ast_iostream *tcptls_stream_alloc(void)
{
	struct ast_iostream *stream;

	stream = ao2_alloc_options(sizeof(*stream), tcptls_stream_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (stream) {
		stream->fd = -1;
		stream->timeout = -1;
		stream->timeout_reset = -1;
	}
	return stream;
}

struct ast_iostream* ast_iostream_from_fd(int *fd)
{
	struct ast_iostream *stream;
	int flags;

	stream = tcptls_stream_alloc();
	if (stream) {
		stream->fd = *fd;
		*fd = -1;

		/* make sure socket is non-blocking */
		flags = fcntl(stream->fd, F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(stream->fd, F_SETFL, flags);
	}

	return stream;
}

int ast_iostream_start_tls(struct ast_iostream **pstream, SSL_CTX *ssl_ctx, int client)
{
#ifdef DO_SSL
	struct ast_iostream *stream = *pstream;
	int (*ssl_setup)(SSL *) = client ? SSL_connect : SSL_accept;
	char err[256];

	stream->ssl = SSL_new(ssl_ctx);
	if (!stream->ssl) {
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

	if (ssl_setup(stream->ssl) <= 0) {
		ast_log(LOG_ERROR, "Problem setting up ssl connection: %s\n",
			ERR_error_string(ERR_get_error(), err));
		errno = EIO;
		return -1;
	} 

	return 0;
#else
	errno = ENOTSUP;
	return -1;
#endif
}
