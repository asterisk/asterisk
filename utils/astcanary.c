/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <utime.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/*!\brief
 * At one time, canaries were carried along with coal miners down
 * into a mine.  Their purpose was to alert the miners when they
 * had drilled into a pocket of methane gas or another noxious
 * substance.  The canary, being the most sensitive animal, would
 * immediately fall over.  Seeing this, the miners could take
 * action to escape the mine, seeing an imminent danger.
 *
 * This process serves a similar purpose, though with the realtime
 * priority being the reason.  When a thread starts running away
 * with the processor, it is typically difficult to tell what
 * thread caused the problem, as the machine acts as if it is
 * locked up (in fact, what has happened is that Asterisk runs at
 * a higher priority than even the login shell, so the runaway
 * thread hogs all available CPU time.
 *
 * If that happens, this canary process will cease to get any
 * process time, which we can monitor with a realtime thread in
 * Asterisk.  Should that happen, that monitoring thread may take
 * immediate action to slow down Asterisk to regular priority,
 * thus allowing an administrator to login to the system and
 * restart Asterisk or perhaps take another course of action
 * (such as retrieving a backtrace to let the developers know
 * what precisely went wrong).
 *
 * Note that according to POSIX.1, all threads inside a single
 * process must share the same priority, so when the monitoring
 * thread deprioritizes itself, it deprioritizes all threads at
 * the same time.  This is also why this canary must exist as a
 * completely separate process and not simply as a thread within
 * Asterisk itself.
 *
 * Quote:
 * "The nice value set with setpriority() shall be applied to the
 * process. If the process is multi-threaded, the nice value shall
 * affect all system scope threads in the process."
 *
 * Source:
 * http://www.opengroup.org/onlinepubs/000095399/functions/setpriority.html
 *
 * In answer to the question, what aren't system scope threads, the
 * answer is, in Asterisk, nothing.  Process scope threads are the
 * alternative, but they aren't supported in Linux.
 */

static const char explanation[] =
"This file is created when Asterisk is run with a realtime priority (-p).  It\n"
"must continue to exist, and the astcanary process must be allowed to continue\n"
"running, or else the Asterisk process will, within a short period of time,\n"
"slow itself down to regular priority.\n\n"
"The technical explanation for this file is to provide an assurance to Asterisk\n"
"that there are no threads that have gone into runaway mode, thus hogging the\n"
"CPU, and making the Asterisk machine seem to be unresponsive.  When that\n"
"happens, the astcanary process will be unable to update the timestamp on this\n"
"file, and Asterisk will notice within 120 seconds and react.  Slowing the\n"
"Asterisk process down to regular priority will permit an administrator to\n"
"intervene, thus avoiding a need to reboot the entire machine.\n";

int main(int argc, char *argv[])
{
	int fd;
	/* Run at normal priority */
	setpriority(PRIO_PROCESS, 0, 0);
	for (;;) {
		/* Update the modification times (checked from Asterisk) */
		if (utime(argv[1], NULL)) {
			/* Recreate the file if it doesn't exist */
			if ((fd = open(argv[1], O_RDWR | O_TRUNC | O_CREAT, 0777)) > -1) {
				if (write(fd, explanation, strlen(explanation)) < 0) {
					exit(1);
				}
				close(fd);
			} else {
				exit(1);
			}
			continue;
		}

		/* Run occasionally */
		sleep(5);
	}

	/* Never reached */
	return 0;
}

