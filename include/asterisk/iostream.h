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
 * \file
 *
 * \brief Generic abstraction for input/output streams.
 */

#include "asterisk.h"           /* for size_t, ssize_t, HAVE_OPENSSL */

#if defined(HAVE_OPENSSL)
#define DO_SSL  /* comment in/out if you want to support ssl */
#endif

struct ssl_st;                  /* forward declaration */
struct ssl_ctx_st;              /* forward declaration */
struct timeval;                 /* forward declaration */
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

struct ast_iostream;            /* forward declaration */

/*!
 * \brief Disable the iostream timeout timer.
 *
 * \param stream A pointer to an iostream
 */
void ast_iostream_set_timeout_disable(struct ast_iostream *stream);

/*!
 * \brief Set the iostream inactivity timeout timer.
 *
 * \param stream A pointer to an iostream
 * \param timeout Number of milliseconds to wait for data transfer with the peer.
 *
 * \details This is basically how much time we are willing to spend
 * in an I/O call before we declare the peer unresponsive.
 *
 * \note Setting timeout to -1 disables the timeout.
 * \note Setting this timeout replaces the I/O sequence timeout timer.
 */
void ast_iostream_set_timeout_inactivity(struct ast_iostream *stream, int timeout);

/*!
 * \brief Set the iostream inactivity & idle timeout timers.
 *
 * \param stream A pointer to an iostream
 * \param timeout Number of milliseconds to wait for initial data transfer with
 *                the peer.
 * \param timeout_reset Number of milliseconds to wait for subsequent data
 *                      transfer with the peer.
 *
 * \details As an example, if you want to timeout a peer if they do not send an
 *          initial message within 5 seconds or if they do not send a message at
 *          least every 30 seconds, you would set \a timeout to \c 5000 and
 *          \a timeout_reset to \c 30000.
 *
 * \note Setting either of these timeouts to -1 will disable them.
 */
void ast_iostream_set_timeout_idle_inactivity(struct ast_iostream *stream, int timeout, int timeout_reset);

/*!
 * \brief Set the iostream I/O sequence timeout timer.
 *
 * \param stream A pointer to an iostream
 * \param start Time the I/O sequence timer starts.
 * \param timeout Number of milliseconds from the start time before timeout.
 *
 * \details This is how much time are we willing to allow the peer
 * to complete an operation that can take several I/O calls.  The
 * main use is as an authentication timer with us.
 *
 * \note Setting timeout to -1 disables the timeout.
 * \note Setting this timeout replaces the inactivity timeout timer.
 */
void ast_iostream_set_timeout_sequence(struct ast_iostream *stream, struct timeval start, int timeout);

/*!
 * \brief Set the iostream if it can exclusively depend upon the set timeouts.
 *
 * \param stream A pointer to an iostream
 * \param exclusive_input TRUE if stream can exclusively wait for fd input.
 * Otherwise, the stream will not wait for fd input.  It will wait while
 * trying to send data.
 *
 * \note The stream timeouts still need to be set.
 */
void ast_iostream_set_exclusive_input(struct ast_iostream *stream, int exclusive_input);

/*!
 * \brief Get an iostream's file descriptor.
 *
 * \param stream A pointer to an iostream
 *
 * \return The file descriptor for the given iostream
 * \retval -1 if the iostream has no open file descriptor.
 */
int ast_iostream_get_fd(struct ast_iostream *stream);

/*!
 * \brief Wait for input on the iostream's file descriptor
 * \since 16.8.0
 * \since 17.2.0
 *
 * \param stream A pointer to an iostream
 * \param timeout the number of milliseconds to wait
 *
 * \retval -1 if error occurred
 * \retval 0 if the timeout expired
 * \retval 1 if the stream is ready for reading
 */
int ast_iostream_wait_for_input(struct ast_iostream *stream, int timeout);

/*!
 * \brief Make an iostream non-blocking.
 *
 * \param stream A pointer to an iostream
 */
void ast_iostream_nonblock(struct ast_iostream *stream);

/*!
 * \brief Get a pointer to an iostream's OpenSSL \c SSL structure
 *
 * \param stream A pointer to an iostream
 *
 * \return A pointer to the OpenSSL \c SSL structure for the given iostream
 * \retval NULL if TLS has not been initiated.
 *
 * \note If OpenSSL support is not included in the build, this will always return
 *       \c NULL.
 */
SSL *ast_iostream_get_ssl(struct ast_iostream *stream);

/*!
 * \brief Read data from an iostream.
 *
 * \param stream A pointer to an iostream
 * \param buffer Pointer to a buffer to store the read bytes.
 * \param count The number of bytes to read.
 *
 * \return Upon successful completion, returns a non-negative integer indicating
 *         the number of bytes actually read. Otherwise, returns -1 and may set
 *         errno to indicate the error.
 */
ssize_t ast_iostream_read(struct ast_iostream *stream, void *buffer, size_t count);

/*!
 * \brief Read a LF-terminated string from an iostream.
 *
 * \param stream A pointer to an iostream
 * \param buffer Pointer to a buffer to store the read bytes.
 * \param size The total size of \a buffer in bytes.
 *
 * \return The number of bytes stored in \a buffer, excluding the null byte used
 *         to terminate the string. If the size of \a buffer (indicated by the
 *         caller with the \a size argument) is not sufficient to store the
 *         entire line it will be truncated to fit the available space. The
 *         contents of \a buffer will always be terminated with a null byte. In
 *         the case of an error, \c -1 will be returned and \c errno may be set
 *         indicating the error.
 */
ssize_t ast_iostream_gets(struct ast_iostream *stream, char *buffer, size_t size);

/*!
 * \brief Discard the specified number of bytes from an iostream.
 *
 * \param stream A pointer to an iostream
 * \param count The number of bytes to discard.
 *
 * \return Upon successful completion, returns the number of bytes discarded.
 *         Otherwise, \c -1 is returned and \c errno may be set indicating the
 *         error.
 */
ssize_t ast_iostream_discard(struct ast_iostream *stream, size_t count);

/*!
 * \brief Write data to an iostream.
 *
 * \param stream A pointer to an iostream
 * \param buffer Pointer to a buffer from which to read bytes.
 * \param count The number of bytes from \a buffer to write.
 *
 * \return Upon successful completion, returns the number of bytes actually
 *         written to the iostream. This number shall never be greater than
 *         \a count. Otherwise, returns \c -1 and may set \c errno to indicate
 *         the error.
 */
ssize_t ast_iostream_write(struct ast_iostream *stream, const void *buffer, size_t count);

/*!
 * \brief Write a formatted string to an iostream.
 *
 * \param stream A pointer to an iostream
 * \param format A format string, as documented by printf(3)
 * \param ... Arguments for the provided \a format string
 *
 * \return The number of bytes written, or \c -1 if an error occurs. Note that if
 *         \c -1 is returned, the number of bytes written to the iostream is
 *         unspecified.
 */
ssize_t __attribute__((format(printf, 2, 3))) ast_iostream_printf(
	struct ast_iostream *stream, const char *format, ...);

/*!
 * \brief Create an iostream from a file descriptor.
 *
 * \param fd A pointer to an open file descriptor
 *
 * \return A newly allocated iostream or \c NULL if allocation fails.
 */
struct ast_iostream *ast_iostream_from_fd(int *fd);

/*!
 * \brief Begin TLS on an iostream.
 *
 * \param stream A pointer to an iostream pointer
 * \param ctx A pointer to an \c SSL_CTX which will be passed to \c SSL_new()
 * \param client Non-zero to indicate that we are the client, zero to indicate
 *               that we are the server.
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note The iostream that is passed in \a stream may be replaced with a
 *       different one before this function returns.
 * \note On failure, \c errno may be set providing additional information on why
 *       the failure occurred.
 */
int ast_iostream_start_tls(struct ast_iostream **stream, SSL_CTX *ctx, int client);

/*!
 * \brief Close an iostream.
 *
 * \param stream A pointer to an iostream
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note On failure, \c errno may be set providing additional information on why
 *       the failure occurred.
 */
int ast_iostream_close(struct ast_iostream *stream);

#endif /* _ASTERISK_IOSTREAM_H */
