/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Application convenience functions, designed to give consistent
 look and feel to Asterisk apps.
 */

#ifndef _ASTERISK_APP_H
#define _ASTERISK_APP_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* IVR stuff */

/* Callback function for IVR, returns 0 on completion, -1 on hangup or digit if
   interrupted */
typedef int (*ast_ivr_callback)(struct ast_channel *chan, char *option, void *cbdata);

typedef enum {
	AST_ACTION_UPONE,		/* adata is unused */
	AST_ACTION_EXIT,		/* adata is the return value for ast_ivr_menu_run if channel was not hungup */
	AST_ACTION_CALLBACK,	/* adata is an ast_ivr_callback */
	AST_ACTION_PLAYBACK,	/* adata is file to play */
	AST_ACTION_BACKGROUND,	/* adata is file to play */
	AST_ACTION_PLAYLIST,	/* adata is list of files, separated by ; to play */
	AST_ACTION_MENU,		/* adata is a pointer to an ast_ivr_menu */
	AST_ACTION_REPEAT,		/* adata is max # of repeats, cast to a pointer */
	AST_ACTION_RESTART,		/* adata is like repeat, but resets repeats to 0 */
	AST_ACTION_TRANSFER,	/* adata is a string with exten[@context] */
	AST_ACTION_WAITOPTION,	/* adata is a timeout, or 0 for defaults */
	AST_ACTION_NOOP,		/* adata is unused */
	AST_ACTION_BACKLIST,	/* adata is list of files separated by ; allows interruption */
} ast_ivr_action;

struct ast_ivr_option {
	char *option;
	ast_ivr_action action;
	void *adata;	
};

/* 
    Special "options" are: 
   "s" - "start here (one time greeting)"
   "g" - "greeting/instructions"
   "t" - "timeout"
   "h" - "hangup"
   "i" - "invalid selection"

*/

struct ast_ivr_menu {
	char *title;		/* Title of menu */
	unsigned int flags;	/* Flags */
	struct ast_ivr_option *options;	/* All options */
};

#define AST_IVR_FLAG_AUTORESTART (1 << 0)

#define AST_IVR_DECLARE_MENU(holder, title, flags, foo...) \
	static struct ast_ivr_option __options_##holder[] = foo;\
	static struct ast_ivr_menu holder = { title, flags, __options_##holder }
	

/*! Runs an IVR menu, returns 0 on successful completion, -1 on hangup, or -2 on user error in menu */
extern int ast_ivr_menu_run(struct ast_channel *c, struct ast_ivr_menu *menu, void *cbdata);

/*! Plays a stream and gets DTMF data from a channel */
/*!
 * \param c Which channel one is interacting with
 * \param prompt File to pass to ast_streamfile (the one that you wish to play)
 * \param s The location where the DTMF data will be stored
 * \param maxlen Max Length of the data
 * \param timeout Timeout length waiting for data(in milliseconds).  Set to 0 for standard timeout(six seconds), or -1 for no time out.
 *
 *  This function was designed for application programmers for situations where they need 
 *  to play a message and then get some DTMF data in response to the message.  If a digit 
 *  is pressed during playback, it will immediately break out of the message and continue
 *  execution of your code.
 */
extern int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout);

/* Full version with audiofd and controlfd.  NOTE: returns '2' on ctrlfd available, not '1' like other full functions */
extern int ast_app_getdata_full(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout, int audiofd, int ctrlfd);

/*! Record voice (after playing prompt if specified), waiting for silence (in ms) up to a given timeout (in s) or '#' */
int ast_app_getvoice(struct ast_channel *c, char *dest, char *dstfmt, char *prompt, int silence, int maxsec);

void ast_install_vm_functions(int (*has_voicemail_func)(const char *mailbox, const char *folder),
			      int (*messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs));
  
void ast_uninstall_vm_functions(void);

/*! Determine if a given mailbox has any voicemail */
int ast_app_has_voicemail(const char *mailbox, const char *folder);

/*! Determine number of new/old messages in a mailbox */
int ast_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs);

/*! Safely spawn an external program while closingn file descriptors */
extern int ast_safe_system(const char *s);

/*! Send DTMF to chan (optionally entertain peer)   */
int ast_dtmf_stream(struct ast_channel *chan, struct ast_channel *peer, char *digits, int between);

/*! Stream a filename (or file descriptor) as a generator. */
int ast_linear_stream(struct ast_channel *chan, const char *filename, int fd, int allowoverride);

/*! Stream a file with fast forward, pause, reverse, restart. */
int ast_control_streamfile(struct ast_channel *chan, const char *file, const char *fwd, const char *rev, const char *stop, const char *pause, const char *restart, int skipms);

/*! Play a stream and wait for a digit, returning the digit that was pressed */
int ast_play_and_wait(struct ast_channel *chan, const char *fn);

/*! Record a file for a max amount of time (in seconds), in a given list of formats separated by '|', outputting the duration of the recording, and with a maximum */
/*   permitted silence time in milliseconds of 'maxsilence' under 'silencethreshold' or use '-1' for either or both parameters for defaults. 
     calls ast_unlock_path() on 'path' if passed */
int ast_play_and_record(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime_sec, const char *fmt, int *duration, int silencethreshold, int maxsilence_ms, const char *path);

/*! Record a message and prepend the message to the given record file after playing the optional playfile (or a beep), storing the duration in 'duration' and with a maximum */
/*   permitted silence time in milliseconds of 'maxsilence' under 'silencethreshold' or use '-1' for either or both parameters for defaults. */
int ast_play_and_prepend(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime_sec, char *fmt, int *duration, int beep, int silencethreshold, int maxsilence_ms);

enum AST_LOCK_RESULT {
	AST_LOCK_SUCCESS = 0,
	AST_LOCK_TIMEOUT = -1,
	AST_LOCK_PATH_NOT_FOUND = -2,
	AST_LOCK_FAILURE = -3,
};

/*
 * \brief Lock a filesystem path.
 * \param path the path to be locked
 * \return one of AST_LOCK_RESULT values
 */
enum AST_LOCK_RESULT ast_lock_path(const char *path);

/* Unlock a path */
int ast_unlock_path(const char *path);

/*Read a file into asterisk*/
char *ast_read_textfile(const char *file);

#define GROUP_CATEGORY_PREFIX "GROUP"

/*! Split a group string into group and category, returning a default category if none is provided. */
int ast_app_group_split_group(char *data, char *group, int group_max, char *category, int category_max);

/*! Set the group for a channel, splitting the provided data into group and category, if specified. */
int ast_app_group_set_channel(struct ast_channel *chan, char *data);

/*! Get the current channel count of the specified group and category. */
int ast_app_group_get_count(char *group, char *category);

/*! Get the current channel count of all groups that match the specified pattern and category. */
int ast_app_group_match_get_count(char *groupmatch, char *category);

/*!
  \brief Define an application argument
  \param name The name of the argument
*/
#define AST_APP_ARG(name) char *name

/*!
  \brief Declare a structure to hold the application's arguments.
  \param name The name of the structure
  \param arglist The list of arguments, defined using AST_APP_ARG

  This macro defines a structure intended to be used in a call
  to ast_separate_app_args(). The structure includes all the
  arguments specified, plus an argv array that overlays them and an
  argc argument counter. The arguments must be declared using AST_APP_ARG,
  and they will all be character pointers (strings).

  Note: The structure is <b>not</b> initialized, as the call to
  ast_separate_app_args() will perform that function before parsing
  the arguments.
 */
#define AST_DECLARE_APP_ARGS(name, arglist) \
	struct { \
		unsigned int argc; \
		char *argv[0]; \
		arglist \
	} name;

/*!
  \brief Performs the 'standard' argument separation process for an application.
  \param args An argument structure defined using AST_DECLARE_APP_ARGS
  \param parse A modifiable buffer containing the input to be parsed

  This function will separate the input string using the standard argument
  separator character '|' and fill in the provided structure, including
  the argc argument counter field.
 */
#define AST_STANDARD_APP_ARGS(args, parse) \
	args.argc = ast_app_separate_args(parse, '|', args.argv, (sizeof(args) - sizeof(args.argc)) / sizeof(args.argv[0]))
	
/*!
  \brief Separate a string into arguments in an array
  \param buf The string to be parsed (this must be a writable copy, as it will be modified)
  \param delim The character to be used to delimit arguments
  \param array An array of 'char *' to be filled in with pointers to the found arguments
  \param arraylen The number of elements in the array (i.e. the number of arguments you will accept)

  Note: if there are more arguments in the string than the array will hold, the last element of
  the array will contain the remaining arguments, not separated.

  The array will be completely zeroed by this function before it populates any entries.

  \return The number of arguments found, or zero if the function arguments are not valid.
*/
unsigned int ast_app_separate_args(char *buf, char delim, char **array, int arraylen);

/*!
  \brief A structure to hold the description of an application 'option'.

  Application 'options' are single-character flags that can be supplied
  to the application to affect its behavior; they can also optionally
  accept arguments enclosed in parenthesis.

  These structures are used by the ast_app_parse_options function, uses
  this data to fill in a flags structure (to indicate which options were
  supplied) and array of argument pointers (for those options that had
  arguments supplied).
 */
struct ast_app_option {
	/*! \brief The flag bit that represents this option. */
	unsigned int flag;
	/*! \brief The index of the entry in the arguments array
	  that should be used for this option's argument. */
	unsigned int arg_index;
};

/*!
  \brief Declares an array of options for an application.
  \param holder The name of the array to be created
  \param options The actual options to be placed into the array
  \sa ast_app_parse_options

  This macro declares a 'static const' array of \c struct \c ast_option
  elements to hold the list of available options for an application.
  Each option must be declared using either the AST_APP_OPTION()
  or AST_APP_OPTION_ARG() macros.

  Example usage:
  \code
  enum {
        OPT_JUMP = (1 << 0),
        OPT_BLAH = (1 << 1),
        OPT_BLORT = (1 << 2),
  } my_app_option_flags;

  enum {
        OPT_ARG_BLAH = 0,
        OPT_ARG_BLORT,
        !! this entry tells how many possible arguments there are,
           and must be the last entry in the list
        OPT_ARG_ARRAY_SIZE,
  } my_app_option_args;

  AST_APP_OPTIONS(my_app_options, {
        AST_APP_OPTION('j', OPT_JUMP),
        AST_APP_OPTION_ARG('b', OPT_BLAH, OPT_ARG_BLAH),
        AST_APP_OPTION_BLORT('B', OPT_BLORT, OPT_ARG_BLORT),
  });

  static int my_app_exec(struct ast_channel *chan, void *data)
  {
  	char *options;
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];

  	... do any argument parsing here ...

	if (ast_parseoptions(my_app_options, &opts, opt_args, options)) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}
  }
  \endcode
 */
#define AST_APP_OPTIONS(holder, options...) \
	static const struct ast_app_option holder[128] = options

/*!
  \brief Declares an application option that does not accept an argument.
  \param option The single character representing the option
  \param flagno The flag index to be set if this option is present
  \sa AST_APP_OPTIONS, ast_app_parse_options
 */
#define AST_APP_OPTION(option, flagno) \
	[option] = { .flag = flagno }

/*!
  \brief Declares an application option that accepts an argument.
  \param option The single character representing the option
  \param flagno The flag index to be set if this option is present
  \param argno The index into the argument array where the argument should
  be placed
  \sa AST_APP_OPTIONS, ast_app_parse_options
 */
#define AST_APP_OPTION_ARG(option, flagno, argno) \
	[option] = { .flag = flagno, .arg_index = argno }

/*!
  \brief Parses a string containing application options and sets flags/arguments.
  \param options The array of possible options declared with AST_APP_OPTIONS
  \param flags The flag structure to have option flags set
  \param args The array of argument pointers to hold arguments found
  \param optstr The string containing the options to be parsed
  \return zero for success, non-zero if an error occurs
  \sa AST_APP_OPTIONS
 */
int ast_app_parse_options(const struct ast_app_option *options, struct ast_flags *flags, char **args, char *optstr);

/*! Present a dialtone and collect a certain length extension.  Returns 1 on valid extension entered, -1 on hangup, or 0 on invalid extension. Note that if 'collect' holds digits already, new digits will be appended, so be sure it's initialized properly */
int ast_app_dtget(struct ast_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout);

/*! Allow to record message and have a review option */
int ast_record_review(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_APP_H */
