/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Application convenience functions, designed to give consistent
 * look and feel to asterisk apps.
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
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
//! Plays a stream and gets DTMF data from a channel
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

//! Record voice (after playing prompt if specified), waiting for silence (in ms) up to a given timeout (in s) or '#'
int ast_app_getvoice(struct ast_channel *c, char *dest, char *dstfmt, char *prompt, int silence, int maxsec);

void ast_install_vm_functions(int (*has_voicemail_func)(const char *mailbox, const char *folder),
			      int (*messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs));
  
void ast_uninstall_vm_functions(void);

//! Determine if a given mailbox has any voicemail
int ast_app_has_voicemail(const char *mailbox, const char *folder);

//! Determine number of new/old messages in a mailbox
int ast_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs);

//! Safely spawn an external program while closingn file descriptors
extern int ast_safe_system(const char *s);

//! Send DTMF to chan (optionally entertain peer)  
int ast_dtmf_stream(struct ast_channel *chan, struct ast_channel *peer, char *digits, int between);

//! Stream a filename (or file descriptor) as a generator.
int ast_linear_stream(struct ast_channel *chan, const char *filename, int fd, int allowoverride);

//! Stream a file with fast forward, pause, reverse.
int ast_control_streamfile(struct ast_channel *chan, char *file, char *fwd, char *rev, char *stop, char *pause, int skipms);

//! Play a stream and wait for a digit, returning the digit that was pressed
int ast_play_and_wait(struct ast_channel *chan, char *fn);

//! Record a file for a max amount of time (in seconds), in a given list of formats separated by '|', outputting the duration of the recording, and with a maximum
//  permitted silence time in milliseconds of 'maxsilence' under 'silencethreshold' or use '-1' for either or both parameters for defaults.
int ast_play_and_record(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime_sec, char *fmt, int *duration, int silencethreshold, int maxsilence_ms);

//! Record a message and prepend the message to the given record file after playing the optional playfile (or a beep), storing the duration in 'duration' and with a maximum
//  permitted silence time in milliseconds of 'maxsilence' under 'silencethreshold' or use '-1' for either or both parameters for defaults.
int ast_play_and_prepend(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime_sec, char *fmt, int *duration, int beep, int silencethreshold, int maxsilence_ms);

#define GROUP_CATEGORY_PREFIX "GROUP"

//! Split a group string into group and category, returning a default category if none is provided.
int ast_app_group_split_group(char *data, char *group, int group_max, char *category, int category_max);

//! Set the group for a channel, splitting the provided data into group and category, if specified.
int ast_app_group_set_channel(struct ast_channel *chan, char *data);

//! Get the current channel count of the specified group and category.
int ast_app_group_get_count(char *group, char *category);

//! Get the current channel count of all groups that match the specified pattern and category.
int ast_app_group_match_get_count(char *groupmatch, char *category);

//! Create an argc argv type structure for app args
int ast_seperate_app_args(char *buf, char delim, char **array, int arraylen);

//! Present a dialtone and collect a certain length extension.  Returns 1 on valid extension entered, -1 on hangup, or 0 on invalid extension.
int ast_app_dtget(struct ast_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
