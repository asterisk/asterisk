/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Top level source file for asterisk
 * 
 * Copyright (C) 1999, Mark Spencer
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
#include <asterisk/cli.h>
#include <asterisk/channel.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <pthread.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "asterisk.h"

int option_verbose=0;
int option_debug=0;
int option_nofork=0;
int option_quiet=0;
int option_console=0;
int option_highpriority=0;
int fully_booted = 0;

char defaultlanguage[MAX_LANGUAGE] = DEFAULT_LANGUAGE;

#define HIGH_PRIORITY 1
#define HIGH_PRIORITY_SCHED SCHED_RR

static void urg_handler(int num)
{
	/* Called by soft_hangup to interrupt the select, read, or other
	   system call.  We don't actually need to do anything though.  */
	if (option_debug)
		ast_log(LOG_DEBUG, "Urgent handler\n");
	signal(num, urg_handler);
	return;
}

static void set_title(char *text)
{
	/* Set an X-term or screen title */
	if (getenv("TERM") && strstr(getenv("TERM"), "xterm"))
		fprintf(stdout, "\033]2;%s\007", text);
}

static void set_icon(char *text)
{
	if (getenv("TERM") && strstr(getenv("TERM"), "xterm"))
		fprintf(stdout, "\033]1;%s\007", text);
}

static int set_priority(int pri)
{
	struct sched_param sched;
	/* We set ourselves to a high priority, that we might pre-empt everything
	   else.  If your PBX has heavy activity on it, this is a good thing.  */
	if (pri) {  
		sched.sched_priority = HIGH_PRIORITY;
		if (sched_setscheduler(0, HIGH_PRIORITY_SCHED, &sched)) {
			ast_log(LOG_WARNING, "Unable to set high priority\n");
			return -1;
		}
	} else {
		sched.sched_priority = 0;
		if (sched_setscheduler(0, SCHED_OTHER, &sched)) {
			ast_log(LOG_WARNING, "Unable to set normal priority\n");
			return -1;
		}
	}
	return 0;
}

static void quit_handler(int num)
{
	static pthread_mutex_t quitlock = PTHREAD_MUTEX_INITIALIZER;
	char filename[80] = "";
	if (getenv("HOME")) 
		snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
	/* Quit only once */
	pthread_mutex_lock(&quitlock);
	/* Called on exit */
	if (option_verbose)
		ast_verbose("Asterisk ending (%d).\n", num);
	else if (option_debug)
		ast_log(LOG_DEBUG, "Asterisk ending (%d).\n", num);
	if (strlen(filename))
		write_history(filename);
	exit(0);
}

static pthread_t consolethread = -1;

static void console_verboser(char *s, int pos, int replace, int complete)
{
	/* Return to the beginning of the line */
	if (!pos)
		fprintf(stdout, "\r");
	fprintf(stdout, s + pos);
	fflush(stdout);
	if (complete)
	/* Wake up a select()ing console */
		pthread_kill(consolethread, SIGURG);
}

static void consolehandler(char *s)
{
	/* Called when readline data is available */
	if (s && strlen(s))
		add_history(s);
	/* Give the console access to the shell */
	if (s) {
		if (s[0] == '!') {
			if (s[1])
				system(s+1);
			else
				system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
		} else 
		ast_cli_command(STDOUT_FILENO, s);
		if (!strcasecmp(s, "help"))
			fprintf(stdout, "          !<command>   Executes a given shell command\n");
	} else
		fprintf(stdout, "\nUse \"quit\" to exit\n");
}

static char quit_help[] = 
"Usage: quit\n"
"       Exits Asterisk.\n";

static int handle_quit(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	quit_handler(0);
	return RESULT_SUCCESS;
}

#define ASTERISK_PROMPT "*CLI> "

static struct ast_cli_entry quit = 	{ { "quit", NULL }, handle_quit, "Exit Asterisk", quit_help };

static char *cli_generator(char *text, int state)
{
	return ast_cli_generator(rl_line_buffer, text, state);
}

int main(int argc, char *argv[])
{
	char c;
	fd_set rfds;
	int res;
	char filename[80] = "";
	if (getenv("HOME")) 
		snprintf(filename, sizeof(filename), "%s/.asterisk_history", getenv("HOME"));
	/* Check if we're root */
	if (geteuid()) {
		ast_log(LOG_ERROR, "Must be run as root\n");
		exit(1);
	}
	/* Check for options */
	while((c=getopt(argc, argv, "dvqpc")) != EOF) {
		switch(c) {
		case 'd':
			option_debug++;
			option_nofork++;
			break;
		case 'c':
			option_console++;
			option_nofork++;
		case 'p':
			option_highpriority++;
			break;
		case 'v':
			option_verbose++;
			option_nofork++;
			break;
		case 'q':
			option_quiet++;
			break;
		case '?':
			exit(1);
		}
	}
	ast_register_verbose(console_verboser);
	/* Print a welcome message if desired */
	if (option_verbose || option_console) {
		ast_verbose( "Asterisk, Copyright (C) 1999 Mark Spencer\n");
		ast_verbose( "Written by Mark Spencer <markster@linux-support.net>\n");
		ast_verbose( "=========================================================================\n");
	}
	if (option_console && !option_verbose) 
		ast_verbose("[ Booting...");
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
	if (set_priority(option_highpriority))
		exit(1);
	/* We might have the option of showing a console, but for now just
	   do nothing... */
	if (option_console && !option_verbose)
		ast_verbose(" ]\n");
	if (option_verbose || option_console)
		ast_verbose( "Asterisk Ready.\n");
	fully_booted = 1;
	if (option_console) {
		/* Console stuff now... */
		/* Register our quit function */
		char title[256];
		set_icon("Asterisk");
		snprintf(title, sizeof(title), "Asterisk Console (pid %d)", getpid());
		set_title(title);
	    ast_cli_register(&quit);
		consolethread = pthread_self();
		if (strlen(filename))
			read_history(filename);
		rl_callback_handler_install(ASTERISK_PROMPT, consolehandler);
		rl_completion_entry_function = (Function *)cli_generator;
		for(;;) {
			FD_ZERO(&rfds);
			FD_SET(STDIN_FILENO, &rfds);
			res = select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL);
			if (res > 0) {
				rl_callback_read_char();
			} else if (res < 1) {
				rl_forced_update_display();
			}
	
		}	
	} else {
		/* Do nothing */
		select(0,NULL,NULL,NULL,NULL);
	}
	return 0;
}
