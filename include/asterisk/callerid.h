/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * CallerID Generation support 
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#ifndef _CALLERID_H
#define _CALLERID_H

#define MAX_CALLERID_SIZE 32000

#define CID_PRIVATE_NAME 		(1 << 0)
#define CID_PRIVATE_NUMBER		(1 << 1)
#define CID_UNKNOWN_NAME		(1 << 2)
#define CID_UNKNOWN_NUMBER		(1 << 3)

struct callerid_state;
typedef struct callerid_state CIDSTATE;

/* CallerID Initialization */
extern void callerid_init(void);

/* Generates a CallerID FSK stream in ulaw
   format suitable for transmission.  Assumes 8000 Hz.  Use NULL
   for no number or "P" for "private".  If "buf" is supplied it will
   use that buffer instead of allocating its own.  "buf" must be
   at least 32000 bytes in size if you want to be sure you dont
   have an overrun however.  Returns # of bytes written to buffer.
   Use "O" or "P" in name */
extern int callerid_generate(unsigned char *buf, char *number, char *name, int flags);

/* Create a callerID state machine */
extern struct callerid_state *callerid_new(void);

/* Read samples into the state machine.  Returns -1 on error, 0 for "needs more samples", and 1 for
   callerID stuff complete */
extern int callerid_feed(struct callerid_state *cid, unsigned char *buf, int samples);

/* Extract info out of callerID state machine.  Flags are listed above */
void callerid_get(struct callerid_state *cid, char **number, char **name, int *flags);

/* Free a callerID state */
extern void callerid_free(struct callerid_state *cid);

/* Generate Caller-ID spill from the "callerid" field of asterisk (in e-mail address like format) */
extern int ast_callerid_generate(unsigned char *buf, char *astcid);

/* Destructively parse inbuf into name and location (or number) */
extern int ast_callerid_parse(char *inbuf, char **name, char **location);

/* Shrink a phone number in place to just digits (more accurately it just removes ()'s, .'s, and -'s...  */
extern void ast_shrink_phone_number(char *n);

/* Check if a string consists only of digits.  Returns non-zero if so */
extern int ast_isphonenumber(char *n);
#endif
