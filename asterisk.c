/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Top level source file for asterisk
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <unistd.h>
#include <stdlib.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <stdio.h>
#include <signal.h>
#include "asterisk.h"

int option_verbose=0;
int option_debug=0;
int option_nofork=0;
int option_quiet=0;

static void urg_handler(int num)
{
	/* Called by soft_hangup to interrupt the select, read, or other
	   system call.  We don't actually need to do anything though.  */
	if (option_debug)
		ast_log(LOG_DEBUG, "Urgent handler\n");
	return;
}

static void quit_handler(int num)
{
	/* Called on exit */
	if (option_verbose)
		ast_verbose("Asterisk ending (%d).\n", num);
	else if (option_debug)
		ast_log(LOG_DEBUG, "Asterisk ending (%d).\n", num);
	exit(0);
}

int main(int argc, char *argv[])
{
	char c;
	/* Check if we're root */
	if (geteuid()) {
		ast_log(LOG_ERROR, "Must be run as root\n");
		exit(1);
	}
	/* Check for options */
	while((c=getopt(argc, argv, "dvq")) != EOF) {
		switch(c) {
		case 'd':
			option_debug++;
			option_nofork++;
			option_verbose++;
			break;
		case 'v':
			option_verbose++;
			break;
		case 'q':
			option_quiet++;
			break;
		case '?':
			exit(1);
		}
	}
	/* Print a welcome message if desired */
	if (option_verbose) {
		ast_verbose( "Asterisk, Copyright (C) 1999 Adtran, Inc. and Linux Support Services, LLC\n");
		ast_verbose( "Written by Mark Spencer <markster@linux-support.net>\n");
		ast_verbose( "=========================================================================\n");
	}
	signal(SIGURG, urg_handler);
	signal(SIGINT, quit_handler);
	signal(SIGTERM, quit_handler);
	signal(SIGHUP, quit_handler);
	if (init_logger())
		exit(1);
	if (load_pbx())
		exit(1);
	if (load_modules())
		exit(1);
	/* We might have the option of showing a console, but for now just
	   do nothing... */
	if (option_verbose)
		ast_verbose( "Asterisk Ready.\n");
	select(0,NULL,NULL,NULL,NULL);
	return 0;
}
