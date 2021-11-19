/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Sean Bright
 *
 * Sean Bright <sean.bright@gmail.com>
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

#ifndef ASTERISK_ALERTPIPE_H
#define ASTERISK_ALERTPIPE_H

#include "asterisk/utils.h"

typedef enum {
	AST_ALERT_READ_SUCCESS = 0,
	AST_ALERT_NOT_READABLE,
	AST_ALERT_READ_FAIL,
	AST_ALERT_READ_FATAL,
} ast_alert_status_t;

/*!
 * \brief Initialize an alert pipe
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array to hold the alert pipe's file descriptors
 *
 * \retval non-zero if a failure occurred.
 * \retval zero otherwise.
 */
int ast_alertpipe_init(int alert_pipe[2]);

/*!
 * \brief Close an alert pipe
 * \since 13.16.0
 *
 * \param alert_pipe a two-element containing the alert pipe's file descriptors
 */
void ast_alertpipe_close(int alert_pipe[2]);

/*!
 * \brief Read an event from an alert pipe
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 *
 * \retval AST_ALERT_READ_SUCCESS on success
 * \retval AST_ALERT_NOT_READABLE if the alert pipe is not readable
 * \retval AST_ALERT_READ_FATAL if the alert pipe's file descriptors are in
 *         blocking mode, or a read error occurs.
 */
ast_alert_status_t ast_alertpipe_read(int alert_pipe[2]);

/*!
 * \brief Write an event to an alert pipe
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 *
 * \retval 0 Success
 * \retval 1 Failure
 */
ssize_t ast_alertpipe_write(int alert_pipe[2]);

/*!
 * \brief Consume all alerts written to the alert pipe
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 *
 * \retval AST_ALERT_READ_SUCCESS on success
 * \retval AST_ALERT_NOT_READABLE if the alert pipe is not readable
 * \retval AST_ALERT_READ_FATAL if the alert pipe's file descriptors are in
 *         blocking mode, or a read error occurs.
 */
ast_alert_status_t ast_alertpipe_flush(int alert_pipe[2]);

/*!
 * \brief Sets the alert pipe file descriptors to default values
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 */
AST_INLINE_API(
void ast_alertpipe_clear(int alert_pipe[2]),
{
	alert_pipe[0] = alert_pipe[1] = -1;
}
)

/*!
 * \brief Determine if the alert pipe is readable
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 *
 * \retval non-zero if the alert pipe is readable.
 * \retval zero otherwise.
 */
AST_INLINE_API(
int attribute_pure ast_alertpipe_readable(int alert_pipe[2]),
{
	return alert_pipe[0] > -1;
}
)

/*!
 * \brief Determine if the alert pipe is writable
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 *
 * \retval non-zero if the alert pipe is writable.
 * \retval zero otherwise.
 */
AST_INLINE_API(
int attribute_pure ast_alertpipe_writable(int alert_pipe[2]),
{
	return alert_pipe[1] > -1;
}
)

/*!
 * \brief Get the alert pipe's read file descriptor
 * \since 13.16.0
 *
 * \param alert_pipe a two-element array containing the alert pipe's file descriptors
 *
 * \retval -1 if the file descriptor is not initialized.
 * \retval non-negative otherwise.
 */
AST_INLINE_API(
int attribute_pure ast_alertpipe_readfd(int alert_pipe[2]),
{
	return alert_pipe[0];
}
)

/*!
 * \brief Swap the file descriptors from two alert pipes
 * \since 13.16.0
 *
 * \param alert_pipe_1 a two-element array containing an alert pipe's file descriptors
 * \param alert_pipe_2 a two-element array containing an alert pipe's file descriptors
 */
AST_INLINE_API(
void ast_alertpipe_swap(int alert_pipe_1[2], int alert_pipe_2[2]),
{
	SWAP(alert_pipe_1[0], alert_pipe_2[0]);
	SWAP(alert_pipe_1[1], alert_pipe_2[1]);
}
)

#endif /* ASTERISK_ALERTPIPE_H */
