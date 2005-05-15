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

extern void ast_cli(int fd, char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

#define RESULT_SUCCESS		0
#define RESULT_SHOWUSAGE	1
#define RESULT_FAILURE		2

#define AST_MAX_CMD_LEN 	16

#define AST_MAX_ARGS 64

#define AST_CLI_COMPLETE_EOF	"_EOF_"

/*! A command line entry */ 
struct ast_cli_entry {
	/*! Null terminated list of the words of the command */
	char *cmda[AST_MAX_CMD_LEN];
	/*! Handler for the command (fd for output, # of arguments, argument list).  Returns RESULT_SHOWUSAGE for improper arguments */
	int (*handler)(int fd, int argc, char *argv[]);
	/*! Summary of the command (< 60 characters) */
	char *summary;
	/*! Detailed usage information */
	char *usage;
	/*! Generate a list of possible completions for a given word */
	char *(*generator)(char *line, char *word, int pos, int state);
	/*! For linking */
	struct ast_cli_entry *next;
	/*! For keeping track of usage */
	int inuse;
};

/*! Interprets a command */
/*! Interpret a command s, sending output to fd
 * Returns 0 on succes, -1 on failure 
 */
extern int ast_cli_command(int fd, char *s);

/*! Registers a command or an array of commands */
/*! 
 * \param e which cli entry to register
 * Register your own command
 * Returns 0 on success, -1 on failure
 */
extern int ast_cli_register(struct ast_cli_entry *e);

/*! 
 * \param e pointer to first cli entry to register
 * \param len number of entries to register
 * Register multiple commands
 */
extern void ast_cli_register_multiple(struct ast_cli_entry *e, int len);

/*! Unregisters a command or an array of commands */
/*!
 * \param e which cli entry to unregister
 * Unregister your own command.  You must pass a completed ast_cli_entry structure
 * Returns 0.
 */
extern int ast_cli_unregister(struct ast_cli_entry *e);

/*!
 * \param e pointer to first cli entry to unregister
 * \param len number of entries to unregister
 * Unregister multiple commands
 */
extern void ast_cli_unregister_multiple(struct ast_cli_entry *e, int len);

/*! Readline madness */
/* Useful for readline, that's about it
 * Returns 0 on success, -1 on failure
 */
extern char *ast_cli_generator(char *, char *, int);

extern int ast_cli_generatornummatches(char *, char *);
extern char **ast_cli_completion_matches(char *, char *);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
