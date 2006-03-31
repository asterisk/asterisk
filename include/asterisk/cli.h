/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Standard Command Line Interface
 */

#ifndef _ASTERISK_CLI_H
#define _ASTERISK_CLI_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <stdarg.h>

#include "asterisk/linkedlists.h"

void ast_cli(int fd, char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

#define RESULT_SUCCESS		0
#define RESULT_SHOWUSAGE	1
#define RESULT_FAILURE		2

#define AST_MAX_CMD_LEN 	16

#define AST_MAX_ARGS 64

#define AST_CLI_COMPLETE_EOF	"_EOF_"

/*! \brief A command line entry */
struct ast_cli_entry {
	char * const cmda[AST_MAX_CMD_LEN];
	/*! Handler for the command (fd for output, # of args, argument list).
	  Returns RESULT_SHOWUSAGE for improper arguments.
	  argv[] has argc 'useful' entries, and an additional NULL entry
	  at the end so that clients requiring NULL terminated arrays
	  can use it without need for copies.
	  You can overwrite argv or the strings it points to, but remember
	  that this memory is deallocated after the handler returns.
	 */
	int (*handler)(int fd, int argc, char *argv[]);
	/*! Summary of the command (< 60 characters) */
	const char *summary;
	/*! Detailed usage information */
	const char *usage;
	/*! Generate the n-th (starting from 0) possible completion
	  for a given 'word' following 'line' in position 'pos'.
	  'line' and 'word' must not be modified.
	  Must return a malloc'ed string with the n-th value when available,
	  or NULL if the n-th completion does not exist.
	  Typically, the function is called with increasing values for n
	  until a NULL is returned.
	 */
	char *(*generator)(const char *line, const char *word, int pos, int n);
	/*! For keeping track of usage */
	int inuse;
	struct module *module;	/*! module this belongs to */
	char *_full_cmd;	/* built at load time from cmda[] */
	/*! For linking */
	AST_LIST_ENTRY(ast_cli_entry) list;
};

/*!
 * \brief Helper function to generate cli entries from a NULL-terminated array.
 * Returns the n-th matching entry from the array, or NULL if not found.
 * Can be used to implement generate() for static entries as below
 * (in this example we complete the word in position 2):
  \code
    char *my_generate(const char *line, const char *word, int pos, int n)
    {
        static char *choices = { "one", "two", "three", NULL };
	if (pos == 2)
        	return ast_cli_complete(word, choices, n);
	else
		return NULL;
    }
  \endcode
 */
char *ast_cli_complete(const char *word, char *const choices[], int pos);

/*! \brief Interprets a command
 * Interpret a command s, sending output to fd
 * Returns 0 on succes, -1 on failure
 */
int ast_cli_command(int fd, const char *s);

/*! \brief Registers a command or an array of commands
 * \param e which cli entry to register
 * Register your own command
 * Returns 0 on success, -1 on failure
 */
int ast_cli_register(struct ast_cli_entry *e);

/*!
 * \brief Register multiple commands
 * \param e pointer to first cli entry to register
 * \param len number of entries to register
 */
void ast_cli_register_multiple(struct ast_cli_entry *e, int len);

/*! \brief Unregisters a command or an array of commands
 *
 * \param e which cli entry to unregister
 * Unregister your own command.  You must pass a completed ast_cli_entry structure
 * Returns 0.
 */
int ast_cli_unregister(struct ast_cli_entry *e);

/*!
 * \brief Unregister multiple commands
 * \param e pointer to first cli entry to unregister
 * \param len number of entries to unregister
 */
void ast_cli_unregister_multiple(struct ast_cli_entry *e, int len);

/*! \brief Readline madness
 * Useful for readline, that's about it
 * Returns 0 on success, -1 on failure
 */
char *ast_cli_generator(const char *, const char *, int);

int ast_cli_generatornummatches(const char *, const char *);

/*!
 * \brief Generates a NULL-terminated array of strings that
 * 1) begin with the string in the second parameter, and
 * 2) are valid in a command after the string in the first parameter.
 *
 * The first entry (offset 0) of the result is the longest common substring
 * in the results, useful to extend the string that has been completed.
 * Subsequent entries are all possible values, followe by a NULL.
 * All strings and the array itself are malloc'ed and must be freed
 * by the caller.
 */
char **ast_cli_completion_matches(const char *, const char *);

/*!
 * \brief Command completion for the list of active channels
 *
 * This can be called from a CLI command completion function that wants to
 * complete from the list of active channels.  'rpos' is the required
 * position in the command.  This function will return NULL immediately if
 * 'rpos' is not the same as the current position, 'pos'.
 */
char *ast_complete_channels(const char *line, const char *word, int pos, int state, int rpos);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CLI_H */
