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

/*! \page CLI_command_api CLI command API

   CLI commands are described by a struct ast_cli_entry that contains
   all the components for their implementation.

   In the "old-style" format, the record must contain:
   - a NULL-terminated array of words constituting the command, e.g.
	{ "set", "debug", "on", NULL },
   - a summary string (short) and a usage string (longer);
   - a handler which implements the command itself, invoked with
     a file descriptor and argc/argv as typed by the user
   - a 'generator' function which, given a partial string, can
     generate legal completions for it.
   An example is

	int old_setdebug(int fd, int argc, char *argv[]);
	char *dbg_complete(const char *line, const char *word, int pos, int n);

	{ { "set", "debug", "on", NULL }, do_setdebug, "Enable debugging",
	set_debug_usage, dbg_complete },

   In the "new-style" format, all the above functionalities are implemented
   by a single function, and the arguments tell which output is required.

   \note \b Note: ideally, the new-style handler would have a different prototype,
   i.e. something like

	int new_setdebug(const struct ast_cli *e, int function,
	    int fd, int argc, char *argv[],	// handler args
	    int n, int pos, const char *line, const char *word // -complete args)

   but at this moment we want to help the transition from old-style to new-style
   functions so we keep the same interface and override some of the traditional
   arguments.

   To help the transition, a new-style entry has the same interface as the old one,
   but it is declared as follows:

	int new_setdebug(int fd, int argc, char *argv[]);

	...
	// this is how we create the entry to register 
	NEW_CLI(new_setdebug, "short description")
	...

   Called with the default arguments (argc > 0), the new_handler implements
   the command as before.
   A negative argc indicates one of the other functions, namely
   generate the usage string, the full command, or implement the generator.
   As a trick to extend the interface while being backward compatible,
   argv[-1] points to a struct ast_cli_args, and, for the generator,
   argv[0] is really a pointer to a struct ast_cli_args.
   The return string is obtained by casting the result to char *

   An example of new-style handler is the following

\code
static int test_new_cli(int fd, int argc, char *argv[])
{
        struct ast_cli_entry *e = (struct ast_cli_entry *)argv[-1];
        struct ast_cli_args *a;
	static char *choices = { "one", "two", "three", NULL };

        switch(argc) {
        case CLI_USAGE:
                return (int)
			"Usage: do this well <arg>\n"
			"	typically multiline with body indented\n";

        case CLI_CMD_STRING:
                return (int)"do this well";

        case CLI_GENERATE:
                a = (struct ast_cli_args *)argv[0];
                if (a->pos > e->args)
                        return NULL;
        	return ast_cli_complete(a->word, choices, a->n);

        default:        
                // we are guaranteed to be called with argc >= e->args;
                if (argc > e->args + 1) // we accept one extra argument
                        return RESULT_SHOWUSAGE;
                ast_cli(fd, "done this well for %s\n", e->args[argc-1]);
                return RESULT_SUCCESS;
        }
}

\endcode
 
 */

/*! \brief calling arguments for new-style handlers 
	See \ref CLI_command_API
*/
enum ast_cli_fn {
	CLI_USAGE = -1,		/* return the usage string */
	CLI_CMD_STRING = -2,	/* return the command string */
	CLI_GENERATE = -3,	/* behave as 'generator', remap argv to struct ast_cli_args */
};

typedef int (*old_cli_fn)(int fd, int argc, char *argv[]);

/*! \brief descriptor for a cli entry 
	See \ref CLI_command_API
 */
struct ast_cli_entry {
	char * const cmda[AST_MAX_CMD_LEN];	/*!< words making up the command.
		* set the first entry to NULL for a new-style entry. */

	/*! Handler for the command (fd for output, # of args, argument list).
	  Returns RESULT_SHOWUSAGE for improper arguments.
	  argv[] has argc 'useful' entries, and an additional NULL entry
	  at the end so that clients requiring NULL terminated arrays
	  can use it without need for copies.
	  You can overwrite argv or the strings it points to, but remember
	  that this memory is deallocated after the handler returns.
	 */
	int (*handler)(int fd, int argc, char *argv[]);

	const char *summary; /*!< Summary of the command (< 60 characters) */
	const char *usage; /*!< Detailed usage information */

	/*! Generate the n-th (starting from 0) possible completion
	  for a given 'word' following 'line' in position 'pos'.
	  'line' and 'word' must not be modified.
	  Must return a malloc'ed string with the n-th value when available,
	  or NULL if the n-th completion does not exist.
	  Typically, the function is called with increasing values for n
	  until a NULL is returned.
	 */
	char *(*generator)(const char *line, const char *word, int pos, int n);
	struct ast_cli_entry *deprecate_cmd;

	int inuse; /*!< For keeping track of usage */
	struct module *module;	/*!< module this belongs to */
	char *_full_cmd;	/*!< built at load time from cmda[] */

	/*! \brief This gets set in ast_cli_register()
	  It then gets set to something different when the deprecated command
	  is run for the first time (ie; after we warn the user that it's deprecated)
	 */
	int args;		/*!< number of non-null entries in cmda */
	char *command;		/*!< command, non-null for new-style entries */
	int deprecated;
	char *_deprecated_by;	/*!< copied from the "parent" _full_cmd, on deprecated commands */
	/*! For linking */
	AST_LIST_ENTRY(ast_cli_entry) list;
};

#define NEW_CLI(fn, txt)	{ .handler = (old_cli_fn)fn, .summary = txt }

/* argument for new-style CLI handler */
struct ast_cli_args {
	char fake[4];		/* a fake string, in the first position, for safety */
	const char *line;	/* the current input line */
	const char *word;	/* the word we want to complete */
	int pos;		/* position of the word to complete */
	int n;			/* the iteration count (n-th entry we generate) */
};

/*!
 * Helper function to generate cli entries from a NULL-terminated array.
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
