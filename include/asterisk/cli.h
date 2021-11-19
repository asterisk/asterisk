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

#include "asterisk/linkedlists.h"
#include "asterisk/strings.h"

void ast_cli(int fd, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/* dont check permissions while passing this option as a 'uid'
 * to the cli_has_permissions() function. */
#define CLI_NO_PERMS		-1

#define RESULT_SUCCESS		0
#define RESULT_SHOWUSAGE	1
#define RESULT_FAILURE		2

#define CLI_SUCCESS	(char *)RESULT_SUCCESS
#define CLI_SHOWUSAGE	(char *)RESULT_SHOWUSAGE
#define CLI_FAILURE	(char *)RESULT_FAILURE

#define AST_MAX_CMD_LEN 	16

#define AST_MAX_ARGS 64

#define AST_CLI_COMPLETE_EOF	"_EOF_"

/*!
 * In many cases we need to print singular or plural
 * words depending on a count. This macro helps us e.g.
 *     printf("we have %d object%s", n, ESS(n));
 */
#define ESS(x) ((x) == 1 ? "" : "s")

/*!
 * \brief Return Yes or No depending on the argument.
 *
 * Note that this should probably still be used for CLI commands instead of
 * AST_YESNO(), in the off chance we someday want to translate the CLI.
 *
 * \param x Boolean value
 * \return "Yes" if x is true (non-zero)
 * \return "No" if x is false (zero)
 */
#define AST_CLI_YESNO(x) AST_YESNO(x)

/*! \brief return On or Off depending on the argument.
 * This is used in many places in CLI command, having a function to generate
 * this helps maintaining a consistent output (and possibly emitting the
 * output in other languages, at some point).
 */
#define AST_CLI_ONOFF(x) (x) ? "On" : "Off"

/*! \page CLI_command_API CLI command API

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
   The prototype is the following:

	char *new_setdebug(const struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

	...
	// this is how we create the entry to register
	AST_CLI_DEFINE(new_setdebug, "short description")
	...

   To help the transition, we make the pointer to the struct ast_cli_entry
   available to old-style handlers via argv[-1].

   An example of new-style handler is the following

\code
static char *test_new_cli(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static const char * const choices[] = { "one", "two", "three", NULL };

        switch (cmd) {
        case CLI_INIT:
		e->command = "do this well";
                e->usage =
			"Usage: do this well <arg>\n"
			"	typically multiline with body indented\n";
		return NULL;

        case CLI_GENERATE:
                if (a->pos > e->args)
                        return NULL;
        	return ast_cli_complete(a->word, choices, a->n);

        default:
                // we are guaranteed to be called with argc >= e->args;
                if (a->argc > e->args + 1) // we accept one extra argument
                        return CLI_SHOWUSAGE;
                ast_cli(a->fd, "done this well for %s\n", e->args[argc-1]);
                return CLI_SUCCESS;
        }
}

\endcode

 */

/*! \brief calling arguments for new-style handlers.
* \arg \ref CLI_command_API
*/
enum ast_cli_command {
	CLI_INIT = -2,		/* return the usage string */
	CLI_GENERATE = -3,	/* behave as 'generator', remap argv to struct ast_cli_args */
	CLI_HANDLER = -4,	/* run the normal handler */
};

/* argument for new-style CLI handler */
struct ast_cli_args {
	const int fd;
	const int argc;
	const char * const *argv;
	const char *line;	/* the current input line */
	const char *word;	/* the word we want to complete */
	const int pos;		/* position of the word to complete */
	const int n;		/* the iteration count (n-th entry we generate) */
};

/*! \brief descriptor for a cli entry.
 * \arg \ref CLI_command_API
 */
struct ast_cli_entry {
	const char * const cmda[AST_MAX_CMD_LEN];	/*!< words making up the command.
							 * set the first entry to NULL for a new-style entry.
							 */

	const char * const summary; 			/*!< Summary of the command (< 60 characters) */
	const char * usage; 				/*!< Detailed usage information */

	int inuse; 				/*!< For keeping track of usage */
	struct ast_module *module;			/*!< module this belongs to */
	char *_full_cmd;			/*!< built at load time from cmda[] */
	int cmdlen;				/*!< len up to the first invalid char [<{% */
	/*! \brief This gets set in ast_cli_register()
	 */
	int args;				/*!< number of non-null entries in cmda */
	char *command;				/*!< command, non-null for new-style entries */
	char *(*handler)(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
	/*! For linking */
	AST_LIST_ENTRY(ast_cli_entry) list;
};

#if defined(__cplusplus) || defined(c_plusplus)
#define AST_CLI_DEFINE(fn, txt) { { "" }, txt, NULL, 0, NULL, NULL, 0, 0, NULL, fn }
#else
/* XXX the parser in gcc 2.95 gets confused if you don't put a space
 * between the last arg before VA_ARGS and the comma */
#define AST_CLI_DEFINE(fn, txt , ... )	{ .handler = fn, .summary = txt, ## __VA_ARGS__ }
#endif

/*!
 * Helper function to generate cli entries from a NULL-terminated array.
 * Returns the n-th matching entry from the array, or NULL if not found.
 * Can be used to implement generate() for static entries as below
 * (in this example we complete the word in position 2):
  \code
    char *my_generate(const char *line, const char *word, int pos, int n)
    {
        static const char * const choices[] = { "one", "two", "three", NULL };
	if (pos == 2)
        	return ast_cli_complete(word, choices, n);
	else
		return NULL;
    }
  \endcode
 */
char *ast_cli_complete(const char *word, const char * const choices[], int pos);

/*!
 * \brief Interprets a command
 * Interpret a command s, sending output to fd if uid:gid has permissions
 * to run this command. uid = CLI_NO_PERMS to avoid checking user permissions
 * gid = CLI_NO_PERMS to avoid checking group permissions.
 * \param uid User ID that is trying to run the command.
 * \param gid Group ID that is trying to run the command.
 * \param fd pipe
 * \param s incoming string
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_cli_command_full(int uid, int gid, int fd, const char *s);

#define ast_cli_command(fd,s) ast_cli_command_full(CLI_NO_PERMS, CLI_NO_PERMS, fd, s)

/*!
 * \brief Executes multiple CLI commands
 * Interpret strings separated by NULL and execute each one, sending output to fd
 * if uid has permissions, uid = CLI_NO_PERMS to avoid checking users permissions.
 * gid = CLI_NO_PERMS to avoid checking group permissions.
 * \param uid User ID that is trying to run the command.
 * \param gid Group ID that is trying to run the command.
 * \param fd pipe
 * \param size is the total size of the string
 * \param s incoming string
 * \return number of commands executed
 */
int ast_cli_command_multiple_full(int uid, int gid, int fd, size_t size, const char *s);

#define ast_cli_command_multiple(fd,size,s) ast_cli_command_multiple_full(CLI_NO_PERMS, CLI_NO_PERMS, fd, size, s)

/*! \brief Registers a command or an array of commands
 * \param e which cli entry to register.
 * Register your own command
 * \retval 0 on success
 * \retval -1 on failure
 */
#define ast_cli_register(e) __ast_cli_register(e, AST_MODULE_SELF)

int __ast_cli_register(struct ast_cli_entry *e, struct ast_module *mod);

/*!
 * \brief Register multiple commands
 * \param e pointer to first cli entry to register
 * \param len number of entries to register
 */
#define ast_cli_register_multiple(e, len) \
	__ast_cli_register_multiple(e, len, AST_MODULE_SELF)

int __ast_cli_register_multiple(struct ast_cli_entry *e, int len, struct ast_module *mod);

/*!
 * \brief Unregisters a command or an array of commands
 * \param e which cli entry to unregister
 * Unregister your own command.  You must pass a completed ast_cli_entry structure
 * \return 0
 */
int ast_cli_unregister(struct ast_cli_entry *e);

/*!
 * \brief Unregister multiple commands
 * \param e pointer to first cli entry to unregister
 * \param len number of entries to unregister
 */
int ast_cli_unregister_multiple(struct ast_cli_entry *e, int len);

/*!
 * \brief Readline madness
 * Useful for readline, that's about it
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Only call this function to proxy the CLI generator to
 * another.
 */
char *ast_cli_generator(const char *, const char *, int);

/*!
 * \brief Generates a NULL-terminated array of strings that
 * 1) begin with the string in the second parameter, and
 * 2) are valid in a command after the string in the first parameter.
 *
 * The first entry (offset 0) of the result is the longest common substring
 * in the results, useful to extend the string that has been completed.
 * Subsequent entries are all possible values, followed by a NULL.
 * All strings and the array itself are malloc'ed and must be freed
 * by the caller.
 *
 * \warning This function cannot be called recursively so it will always
 *          fail if called from a CLI_GENERATE callback.
 */
char **ast_cli_completion_matches(const char *, const char *);

/*!
 * \brief Generates a vector of strings for CLI completion.
 *
 * \param text Complete input being matched.
 * \param word Current word being matched
 *
 * The results contain strings that both:
 * 1) Begin with the string in \a word.
 * 2) Are valid in a command after the string in \a text.
 *
 * The first entry (offset 0) of the result is the longest common substring
 * in the results, useful to extend the string that has been completed.
 * Subsequent entries are all possible values.
 *
 * \note All strings and the vector itself are malloc'ed and must be freed
 *       by the caller.
 *
 * \note The vector is sorted and does not contain any duplicates.
 *
 * \warning This function cannot be called recursively so it will always
 *          fail if called from a CLI_GENERATE callback.
 */
struct ast_vector_string *ast_cli_completion_vector(const char *text, const char *word);

/*!
 * \brief Add a result to a request for completion options.
 *
 * \param value A completion option text.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * This is an alternative to returning individual values from CLI_GENERATE.  Instead
 * of repeatedly being asked for the next match and having to start over, you can
 * call this function repeatedly from your own stateful loop.  When all matches have
 * been added you can return NULL from the CLI_GENERATE function.
 *
 * \note This function always eventually results in calling ast_free on \a value.
 */
int ast_cli_completion_add(char *value);

/*!
 * \brief Command completion for the list of active channels.
 *
 * This can be called from a CLI command completion function that wants to
 * complete from the list of active channels.  'rpos' is the required
 * position in the command.  This function will return NULL immediately if
 * 'rpos' is not the same as the current position, 'pos'.
 */
char *ast_complete_channels(const char *line, const char *word, int pos, int state, int rpos);

/*!
 * \since 13.8
 * \brief Print on cli a duration in seconds in format
 * %s year(s), %s week(s), %s day(s), %s hour(s), %s second(s)
 *
 * \param fd fd to print by ast_cli
 * \param seconds The time (in seconds) to print
 * \param prefix A Prefix string to add before of duration formatted
 */
void ast_cli_print_timestr_fromseconds(int fd, int seconds, const char *prefix);

/*!
 * \brief Allow a CLI command to be executed while Asterisk is shutting down.
 *
 * CLI commands by defeault are disabled when Asterisk is shutting down. This is
 * to ensure the safety of the shutdown since CLI commands may attempt to access
 * resources that have been freed as a result of the shutdown.
 *
 * If a CLI command should be allowed at shutdown, then the best way to enable this
 * is to call ast_cli_allow_at_shutdown during the CLI_INIT state of the CLI handler.
 */
int ast_cli_allow_at_shutdown(struct ast_cli_entry *e);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CLI_H */
