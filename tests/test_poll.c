/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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
 * \brief Poll Tests
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 *
 * Verify that the various poll implementations work as desired (ast_poll, ast_poll2)
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/poll-compat.h"

static void *failsafe_cancel(void *vparent)
{
	pthread_t parent = (pthread_t) (long) vparent;

	sleep(1);
	pthread_testcancel();
	pthread_kill(parent, SIGURG);
	sleep(1);
	pthread_testcancel();
	pthread_kill(parent, SIGURG);
	sleep(1);
	pthread_testcancel();
	pthread_kill(parent, SIGURG);
	pthread_exit(NULL);
}

#define RESET for (i = 0; i < 4; i++) { pfd[i].revents = 0; }
AST_TEST_DEFINE(poll_test)
{
#define FDNO 3
	int fd[2], res = AST_TEST_PASS, i, res2;
	int rdblocker[2];
#if FDNO > 3
	int wrblocker[2], consec_interrupt = 0;
#endif
	struct pollfd pfd[4] = { { .events = POLLOUT, }, { .events = POLLIN, }, { .events = POLLIN }, { .events = POLLOUT } };
	pthread_t failsafe_tid;
	struct timeval tv = { 0, 0 };
#if FDNO > 3
	char garbage[256] =
		"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@/"
		"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@/"
		"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@/"
		"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@/";
#endif

	switch (cmd) {
	case TEST_INIT:
		info->name = "poll_test";
		info->category = "main/poll/";
		info->summary = "unit test for the ast_poll() API";
		info->description =
			"Verifies behavior for the ast_poll() API call\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating handle that should NEVER block on write\n");
	if ((fd[0] = open("/dev/null", O_WRONLY)) < 0) {
		ast_test_status_update(test, "Unable to open a writable handle to /dev/null: %s\n", strerror(errno));
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Creating handle that should NEVER block on read\n");
	if ((fd[1] = open("/dev/zero", O_RDONLY)) < 0) {
		ast_test_status_update(test, "Unable to open a readable handle to /dev/zero: %s\n", strerror(errno));
		close(fd[0]);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Creating handle that should block on read\n");
	if (pipe(rdblocker) < 0) {
		ast_test_status_update(test, "Unable to open a pipe: %s\n", strerror(errno));
		close(fd[0]);
		close(fd[1]);
		return AST_TEST_FAIL;
	}

#if FDNO > 3
	ast_test_status_update(test, "Creating handle that should block on write\n");
	if (pipe(wrblocker) < 0) {
		ast_test_status_update(test, "Unable to open a pipe: %s\n", strerror(errno));
		close(fd[0]);
		close(fd[1]);
		close(rdblocker[0]);
		close(rdblocker[1]);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Starting thread to ensure we don't block forever\n");
	if (ast_pthread_create_background(&failsafe_tid, NULL, failsafe_cancel, (void *) (long) pthread_self())) {
		ast_test_status_update(test, "Unable to start failsafe thread\n");
		close(fd[0]);
		close(fd[1]);
		close(fd[2]);
		close(rdblocker[0]);
		close(rdblocker[1]);
		close(wrblocker[0]);
		close(wrblocker[1]);
		return AST_TEST_FAIL;
	}

	/* Fill the pipe full of data */
	ast_test_status_update(test, "Making pipe block on write\n");
	for (i = 0; i < 4096; i++) { /* 1MB of data should be more than enough for any pipe */
		errno = 0;
		if (write(wrblocker[1], garbage, sizeof(garbage)) < sizeof(garbage)) {
			ast_test_status_update(test, "Got %d\n", errno);
			if (errno == EINTR && ++consec_interrupt > 1) {
				break;
			}
		} else {
			consec_interrupt = 0;
		}
	}

	ast_test_status_update(test, "Cancelling failsafe thread.\n");
	pthread_cancel(failsafe_tid);
	pthread_kill(failsafe_tid, SIGURG);
	pthread_join(failsafe_tid, NULL);
#endif

	pfd[0].fd = fd[0];
	pfd[1].fd = fd[1];
	pfd[2].fd = rdblocker[0];
#if FDNO > 3
	pfd[3].fd = wrblocker[1];
#endif

	/* Need to ensure the infinite timeout doesn't stall the process */
	ast_test_status_update(test, "Starting thread to ensure we don't block forever\n");
	if (ast_pthread_create_background(&failsafe_tid, NULL, failsafe_cancel, (void *) (long) pthread_self())) {
		ast_test_status_update(test, "Unable to start failsafe thread\n");
		close(fd[0]);
		close(fd[1]);
		close(rdblocker[0]);
		close(rdblocker[1]);
#if FDNO > 3
		close(wrblocker[0]);
		close(wrblocker[1]);
#endif
		return AST_TEST_FAIL;
	}

	RESET;
	if ((res2 = ast_poll(pfd, FDNO, -1)) != 2) {
		ast_test_status_update(test, "ast_poll does not return that only two handles are available (inf timeout): %d, %s\n", res2, res2 == -1 ? strerror(errno) : "");
		res = AST_TEST_FAIL;
	}

	RESET;
	if ((res2 = ast_poll2(pfd, FDNO, NULL)) != 2) {
		ast_test_status_update(test, "ast_poll2 does not return that only two handles are available (inf timeout): %d %s\n", res2, res2 == -1 ? strerror(errno) : "");
		res = AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Cancelling failsafe thread.\n");
	pthread_cancel(failsafe_tid);
	pthread_kill(failsafe_tid, SIGURG);
	pthread_join(failsafe_tid, NULL);

	RESET;
	if (ast_poll(pfd, FDNO, 0) != 2) {
		ast_test_status_update(test, "ast_poll does not return that only two handles are available (0 timeout): %d, %s\n", res2, res2 == -1 ? strerror(errno) : "");
		res = AST_TEST_FAIL;
	}

	RESET;
	if (ast_poll2(pfd, FDNO, &tv) != 2) {
		ast_test_status_update(test, "ast_poll2 does not return that only two handles are available (0 timeout): %d, %s\n", res2, res2 == -1 ? strerror(errno) : "");
		res = AST_TEST_FAIL;
	}

	RESET;
	if (ast_poll(pfd, FDNO, 1) != 2) {
		ast_test_status_update(test, "ast_poll does not return that only two handles are available (1ms timeout): %d, %s\n", res2, res2 == -1 ? strerror(errno) : "");
		res = AST_TEST_FAIL;
	}

	tv.tv_usec = 1000;
	if (ast_poll2(pfd, FDNO, &tv) != 2) {
		ast_test_status_update(test, "ast_poll2 does not return that only two handles are available (1ms timeout): %d, %s\n", res2, res2 == -1 ? strerror(errno) : "");
		res = AST_TEST_FAIL;
	}

	close(fd[0]);
	close(fd[1]);
	close(rdblocker[0]);
	close(rdblocker[1]);
#if FDNO > 3
	close(wrblocker[0]);
	close(wrblocker[1]);
#endif
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(poll_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(poll_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Poll test");
