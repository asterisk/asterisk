/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Application convenience functions, designed to give consistent
 * look and feel to asterisk apps.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
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

//! Determine if a given mailbox has any voicemail
extern int ast_app_has_voicemail(const char *mailbox);

//! Determine number of new/old messages in a mailbox
extern int ast_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs);

//! Safely spawn an external program while closingn file descriptors
extern int ast_safe_system(const char *s);

//! Send DTMF to chan (optionally entertain peer)  
int ast_dtmf_stream(struct ast_channel *chan, struct ast_channel *peer, char *digits, int between);

//! Stream a filename (or file descriptor) as a generator.
int ast_linear_stream(struct ast_channel *chan, const char *filename, int fd, int allowoverride);

//! Stream a file with fast forward and reverse.
int ast_control_streamfile(struct ast_channel *chan, char *file,char *f,char *r,int skipms);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
