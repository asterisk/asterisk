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

#ifndef _ASTERISK_IOSTREAM_H
#define _ASTERISK_IOSTREAM_H

/*!
 * \file iostream.h
 *
 * \brief Generic abstraction for input/output streams.
 */

#if defined(HAVE_OPENSSL)
#define DO_SSL  /* comment in/out if you want to support ssl */
#endif

#ifdef DO_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#else
/* declare dummy types so we can define a pointer to them */
typedef struct {} SSL;
typedef struct {} SSL_CTX;
#endif /* DO_SSL */

struct ast_iostream;

/*!
 * \brief Disable the iostream timeout timer.
 *
 * \param stream iostream control data.
 *
 * \return Nothing
 */
void ast_iostream_set_timeout_disable(struct ast_iostream *stream);

/*!
 * \brief Set the iostream inactivity timeout timer.
 *
 * \param stream iostream control data.
 * \param timeout Number of milliseconds to wait for data transfer with the peer.
 *
 * \details This is basically how much time we are willing to spend
 * in an I/O call before we declare the peer unresponsive.
 *
 * \note Setting timeout to -1 disables the timeout.
 * \note Setting this timeout replaces the I/O sequence timeout timer.
 *
 * \return Nothing
 */
void ast_iostream_set_timeout_inactivity(struct ast_iostream *stream, int timeout);

void ast_iostream_set_timeout_idle_inactivity(struct ast_iostream *stream, int timeout, int timeout_reset);

/*!
 * \brief Set the iostream I/O sequence timeout timer.
 *
 * \param stream iostream control data.
 * \param start Time the I/O sequence timer starts.
 * \param timeout Number of milliseconds from the start time before timeout.
 *
 * \details This is how much time are we willing to allow the peer
 * to complete an operation that can take several I/O calls.  The
 * main use is as an authentication timer with us.
 *
 * \note Setting timeout to -1 disables the timeout.
 * \note Setting this timeout replaces the inactivity timeout timer.
 *
 * \return Nothing
 */
void ast_iostream_set_timeout_sequence(struct ast_iostream *stream, struct timeval start, int timeout);

/*!
 * \brief Set the iostream if it can exclusively depend upon the set timeouts.
 *
 * \param stream iostream control data.
 * \param exclusive_input TRUE if stream can exclusively wait for fd input.
 * Otherwise, the stream will not wait for fd input.  It will wait while
 * trying to send data.
 *
 * \note The stream timeouts still need to be set.
 *
 * \return Nothing
 */
void ast_iostream_set_exclusive_input(struct ast_iostream *stream, int exclusive_input);

int ast_iostream_get_fd(struct ast_iostream *stream);
void ast_iostream_nonblock(struct ast_iostream *stream);

SSL* ast_iostream_get_ssl(struct ast_iostream *stream);

ssize_t ast_iostream_read(struct ast_iostream *stream, void *buf, size_t count);
ssize_t ast_iostream_gets(struct ast_iostream *stream, char *buf, size_t count);
ssize_t ast_iostream_discard(struct ast_iostream *stream, size_t count);
ssize_t ast_iostream_write(struct ast_iostream *stream, const void *buf, size_t count);
ssize_t __attribute__((format(printf, 2, 3))) ast_iostream_printf(
	struct ast_iostream *stream, const char *fmt, ...);

struct ast_iostream* ast_iostream_from_fd(int *fd);
int ast_iostream_start_tls(struct ast_iostream **stream, SSL_CTX *ctx, int client);
int ast_iostream_close(struct ast_iostream *stream);

#endif /* _ASTERISK_IOSTREAM_H */
