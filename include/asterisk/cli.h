/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Standard Command Line Interface
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_CLI_H
#define _ASTERISK_CLI_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <stdarg.h>

extern void ast_cli(int fd, char *fmt, ...);

#define RESULT_SUCCESS		0
#define RESULT_SHOWUSAGE	1
#define RESULT_FAILURE		2

#define AST_MAX_CMD_LEN 	16

/* A command line entry */
#define AST_MAX_ARGS 64

struct ast_cli_entry {
	/* Null terminated list of the words of the command */
	char *cmda[AST_MAX_CMD_LEN];
	/* Handler for the command (fd for output, # of arguments, argument list). 
	    Returns RESULT_SHOWUSAGE for improper arguments */
	int (*handler)(int fd, int argc, char *argv[]);
	/* Summary of the command (< 60 characters) */
	char *summary;
	/* Detailed usage information */
	char *usage;
	/* Generate a list of possible completions for a given word */
	char *(*generator)(char *line, char *word, int pos, int state);
	/* For linking */
	struct ast_cli_entry *next;
};

/* Interpret a command s, sending output to fd */
extern int ast_cli_command(int fd, char *s);

/* Register your own command */
extern int ast_cli_register(struct ast_cli_entry *e);

/* Unregister your own command */
extern int ast_cli_unregister(struct ast_cli_entry *e);

/* Useful for readline, that's about it */
extern char *ast_cli_generator(char *, char *, int);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
