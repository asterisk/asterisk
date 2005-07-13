/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Application convenience functions, designed to give consistent
 * look and feel to asterisk apps.
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
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

struct ast_option {
	unsigned int flag;
	int argoption;
};

extern int ast_parseoptions(const struct ast_option *options, struct ast_flags *flags, char **args, char *optstr);

#define AST_DECLARE_OPTIONS(holder,args...) \
	static struct ast_option holder[128] = args

#define AST_IVR_DECLARE_MENU(holder,title,flags,foo...) \
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

/*! Stream a file with fast forward, pause, reverse. */
int ast_control_streamfile(struct ast_channel *chan, const char *file, const char *fwd, const char *rev, const char *stop, const char *pause, int skipms);

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
  \brief Separate a string into arguments in an array
  \param buf The string to be parsed (this must be a writable copy, as it will be modified)
  \param delim The character to be used to delimit arguments
  \param array An array of 'char *' to be filled in with pointers to the found arguments
  \param arraylen The number of elements in the array (i.e. the number of arguments you will accept)

  Note: if there are more arguments in the string than the array will hold, the last element of
  the array will contain the remaining arguments, not separated.

  \return The number of arguments found, or zero if the function arguments are not valid.
*/
int ast_separate_app_args(char *buf, char delim, char **array, int arraylen);

/*! Present a dialtone and collect a certain length extension.  Returns 1 on valid extension entered, -1 on hangup, or 0 on invalid extension. Note that if 'collect' holds digits already, new digits will be appended, so be sure it's initialized properly */
int ast_app_dtget(struct ast_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout);

/*! Allow to record message and have a review option */
int ast_record_review(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
