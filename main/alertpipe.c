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

/*! \file
 *
 * \brief Alert Pipe API
 *
 * \author Sean Bright
 */

#include "asterisk.h"

#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_EVENTFD
# include <sys/eventfd.h>
#endif

#include "asterisk/alertpipe.h"
#include "asterisk/logger.h"

int ast_alertpipe_init(int alert_pipe[2])
{
#ifdef HAVE_EVENTFD

	int fd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
	if (fd > -1) {
		alert_pipe[0] = alert_pipe[1] = fd;
		return 0;
	}

	ast_log(LOG_WARNING, "Failed to create alert pipe with eventfd(), falling back to pipe(): %s\n",
		strerror(errno));
	ast_alertpipe_clear(alert_pipe);

#endif

#ifdef HAVE_PIPE2

	if (pipe2(alert_pipe, O_NONBLOCK)) {
		ast_log(LOG_WARNING, "Failed to create alert pipe: %s\n", strerror(errno));
		return -1;
	}

#else

	if (pipe(alert_pipe)) {
		ast_log(LOG_WARNING, "Failed to create alert pipe: %s\n", strerror(errno));
		return -1;
	} else {
		if (ast_fd_set_flags(alert_pipe[0], O_NONBLOCK)
		   || ast_fd_set_flags(alert_pipe[1], O_NONBLOCK)) {
			ast_alertpipe_close(alert_pipe);
			return -1;
		}
	}

#endif

	return 0;
}

void ast_alertpipe_close(int alert_pipe[2])
{
#ifdef HAVE_EVENTFD

	if (alert_pipe[0] == alert_pipe[1]) {
		if (alert_pipe[0] > -1) {
			close(alert_pipe[0]);
			ast_alertpipe_clear(alert_pipe);
		}
		return;
	}

#endif

	if (alert_pipe[0] > -1) {
		close(alert_pipe[0]);
	}
	if (alert_pipe[1] > -1) {
		close(alert_pipe[1]);
	}
	ast_alertpipe_clear(alert_pipe);
}

ast_alert_status_t ast_alertpipe_read(int alert_pipe[2])
{
	uint64_t tmp;

	if (!ast_alertpipe_readable(alert_pipe)) {
		return AST_ALERT_NOT_READABLE;
	}

	if (read(alert_pipe[0], &tmp, sizeof(tmp)) < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
			return AST_ALERT_READ_FAIL;
		}
	}

	return AST_ALERT_READ_SUCCESS;
}

ssize_t ast_alertpipe_write(int alert_pipe[2])
{
	uint64_t tmp = 1;

	if (!ast_alertpipe_writable(alert_pipe)) {
		errno = EBADF;
		return 0;
	}

	/* preset errno in case returned size does not match */
	errno = EPIPE;
	return write(alert_pipe[1], &tmp, sizeof(tmp)) != sizeof(tmp);
}

ast_alert_status_t ast_alertpipe_flush(int alert_pipe[2])
{
	int bytes_read;
	uint64_t tmp[16];

	if (!ast_alertpipe_readable(alert_pipe)) {
		return AST_ALERT_NOT_READABLE;
	}

	/* Read the alertpipe until it is exhausted. */
	for (;;) {
		bytes_read = read(alert_pipe[0], tmp, sizeof(tmp));
		if (bytes_read < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				/*
				 * Would block so nothing left to read.
				 * This is the normal loop exit.
				 */
				break;
			}
			ast_log(LOG_WARNING, "read() failed flushing alertpipe: %s\n",
				strerror(errno));
			return AST_ALERT_READ_FAIL;
		}
		if (!bytes_read) {
			/* Read nothing so we are done */
			break;
		}
	}

	return AST_ALERT_READ_SUCCESS;
}
