/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Asterisk channel definitions.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_CHANNEL_H
#define _ASTERISK_CHANNEL_H

#include <asterisk/frame.h>
#include <asterisk/sched.h>
#include <setjmp.h>
#include <pthread.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <pthread.h>

#ifdef DEBUG_THREADS

#define TRIES 500

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

struct mutex_info {
	pthread_mutex_t *mutex;
	char *file;
	int lineno;
	char *func;
	struct mutex_info *next;
};

static inline int __ast_pthread_mutex_lock(char *filename, int lineno, char *func, pthread_mutex_t *t) {
	int res;
	int tries = TRIES;
	do {
		res = pthread_mutex_trylock(t);
		/* If we can't run, yield */
		if (res) {
			sched_yield();
			usleep(1);
		}
	} while(res && tries--);
	if (res) {
		fprintf(stderr, "%s line %d (%s): Error obtaining mutex: %s\n", 
				filename, lineno, func, strerror(res));
		res = pthread_mutex_lock(t);
		fprintf(stderr, "%s line %d (%s): Got it eventually...\n",
				filename, lineno, func);
	}
	return res;
}

#define ast_pthread_mutex_lock(a) __ast_pthread_mutex_lock(__FILE__, __LINE__, __PRETTY_FUNCTION__, a)

static inline int __ast_pthread_mutex_unlock(char *filename, int lineno, char *func, pthread_mutex_t *t) {
	int res;
	res = pthread_mutex_unlock(t);
	if (res) 
		fprintf(stderr, "%s line %d (%s): Error releasing mutex: %s\n", 
				filename, lineno, func, strerror(res));
	return res;
}
#define ast_pthread_mutex_unlock(a) __ast_pthread_mutex_unlock(__FILE__, __LINE__, __PRETTY_FUNCTION__, a)
#else
#define ast_pthread_mutex_lock pthread_mutex_lock
#define ast_pthread_mutex_unlock pthread_mutex_unlock
#endif

#define AST_CHANNEL_NAME 80
#define AST_CHANNEL_MAX_STACK 32

#define MAX_LANGUAGE 20

/* Max length an extension can be (unique) is this number */
#define AST_MAX_EXTENSION 80

#define AST_MAX_FDS 4

struct ast_channel {
	char name[AST_CHANNEL_NAME];		/* ASCII Description of channel name */
	char language[MAX_LANGUAGE];		/* Language requested */
	char *type;				/* Type of channel */
	int fds[AST_MAX_FDS];			/* File descriptor for channel -- Drivers will poll
								   on these file descriptors, so at least one must be
								   non -1.  */
						   
	struct ast_channel *bridge;			/* Who are we bridged to, if we're bridged */
	struct ast_channel *masq;			/* Channel that will masquerade as us */
	struct ast_channel *masqr;			/* Who we are masquerading as */
	int cdrflags;						/* Call Detail Record Flags */						   

	int blocking;						/* Whether or not we're blocking */
	int softhangup;						/* Whether or not we have been hung up */
	int zombie;							/* Non-zero if this is a zombie channel */	
	pthread_t blocker;					/* If anyone is blocking, this is them */
	pthread_mutex_t lock;				/* Lock, can be used to lock a channel for some operations */
	char *blockproc;					/* Procedure causing blocking */
	
	char *appl;							/* Current application */
	char *data;							/* Data passed to current application */
	
	int exception;						/* Has an exception been detected */
	int fdno;							/* Which fd had an event detected on */
	struct sched_context *sched;		/* Schedule context */

	int streamid;					/* For streaming playback, the schedule ID */
	struct ast_filestream *stream;	/* Stream itself. */
	int oldwriteformat;				/* Original writer format */

	int state;				/* State of line */
	int rings;				/* Number of rings so far */
	int stack;				/* Current level of application */

	int nativeformats;		/* Kinds of data this channel can
						   	   natively handle */
	int readformat;			/* Requested read format */
	int writeformat;		/* Requested write format */
	
	char *dnid;				/* Malloc'd Dialed Number Identifier */
	char *callerid;			/* Malloc'd Caller ID */
	
	char context[AST_MAX_EXTENSION];	/* Current extension context */
	char exten[AST_MAX_EXTENSION];		/* Current extension number */
	int priority;						/* Current extension priority */
	void *app[AST_CHANNEL_MAX_STACK];	/* Application information -- see assigned numbers */
	struct ast_channel_pvt *pvt;
						/* Private channel implementation details */
	jmp_buf jmp[AST_CHANNEL_MAX_STACK];		/* Jump buffer used for returning from applications */
	struct ast_pbx *pbx;
	struct ast_channel *next;		/* For easy linking */
};


#define AST_CDR_TRANSFER	(1 << 0)
#define AST_CDR_FORWARD		(1 << 1)
#define AST_CDR_CALLWAIT	(1 << 2)
#define AST_CDR_CONFERENCE	(1 << 3)

/* Bits 0-15 of state are reserved for the state (up/down) of the line */

#define AST_STATE_DOWN		0		/* Channel is down and available */
#define AST_STATE_RESERVED	1		/* Channel is down, but reserved */
#define AST_STATE_OFFHOOK	2		/* Channel is off hook */
#define AST_STATE_DIALING	3		/* Digits (or equivalent) have been dialed */
#define AST_STATE_RING		4		/* Line is ringing */
#define AST_STATE_RINGING	5		/* Remote end is ringing */
#define AST_STATE_UP		6		/* Line is up */
#define AST_STATE_BUSY  	7		/* Line is busy */

/* Bits 16-32 of state are reserved for flags */

#define AST_STATE_MUTE		(1 << 16)	/* Do not transmit voice data */

/* Request a channel of a given type, with data as optional information used
   by the low level module */
struct ast_channel *ast_request(char *type, int format, void *data);

/* Called by a channel module to register the kind of channels it supports.
   It supplies a brief type, a longer, but still short description, and a
   routine that creates a channel */
int ast_channel_register(char *type, char *description, int capabilities, 
			struct ast_channel* (*requester)(char *type, int format, void *data));

/* Unregister a channel class */
void ast_channel_unregister(char *type);

/* Hang up a channel -- chan is no longer valid after this call! */
int ast_hangup(struct ast_channel *chan);

/* Softly hangup up a channel -- call the protocol layer, but don't
   destroy the channel structure (use this if you are trying to
   safely hangup a channel managed by another thread. */
int ast_softhangup(struct ast_channel *chan);

/* Answer a ringing call */
int ast_answer(struct ast_channel *chan);

/* Place a call, take no longer than timeout ms.  Returns -1 on failure, 
   0 on not enough time (does not auto matically stop ringing), and  
   the number of seconds the connect took otherwise.  */
int ast_call(struct ast_channel *chan, char *addr, int timeout);

/* Indicate a condition such as AST_CONTROL_BUSY, AST_CONTROL_RINGING, or AST_CONTROL_CONGESTION on a channel */
int ast_indicate(struct ast_channel *chan, int condition);

/* Misc stuff */

/* Wait for input on a channel for a given # of milliseconds (<0 for indefinite).  
  Returns < 0 on  failure, 0 if nothing ever arrived, and the # of ms remaining otherwise */
int ast_waitfor(struct ast_channel *chan, int ms);

/* Wait for input on an array of channels for a given # of milliseconds. Return channel
   with activity, or NULL if none has activity.  time "ms" is modified in-place, if applicable */

struct ast_channel *ast_waitfor_n(struct ast_channel **chan, int n, int *ms);

/* This version works on fd's only.  Be careful with it. */
int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception);

/* Read a frame.  Returns a frame, or NULL on error.  If it returns NULL, you
   best just stop reading frames and assume the channel has been
   disconnected. */
struct ast_frame *ast_read(struct ast_channel *chan);

/* Write a frame to a channel */
int ast_write(struct ast_channel *chan, struct ast_frame *frame);

/* Set read format for channelto whichever component of "format" is best. */
int ast_set_read_format(struct ast_channel *chan, int format);

/* Set write format for channel to whichever compoent of "format" is best. */
int ast_set_write_format(struct ast_channel *chan, int format);

/* Write text to a display on a channel */
int ast_sendtext(struct ast_channel *chan, char *text);

/* Browse the channels currently in use */
struct ast_channel *ast_channel_walk(struct ast_channel *prev);

/* Wait for a digit.  Returns <0 on error, 0 on no entry, and the digit on success. */
char ast_waitfordigit(struct ast_channel *c, int ms);

/* Read in a digit string "s", max length "len", maximum timeout between 
   digits "timeout" (-1 for none), terminated by anything in "enders".  Give them rtimeout
   for the first digit */
int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int rtimeout, char *enders);

#define AST_BRIDGE_DTMF_CHANNEL_0		(1 << 0)		/* Report DTMF on channel 0 */
#define AST_BRIDGE_DTMF_CHANNEL_1		(1 << 1)		/* Report DTMF on channel 1 */
#define AST_BRIDGE_REC_CHANNEL_0		(1 << 2)		/* Return all voice frames on channel 0 */
#define AST_BRIDGE_REC_CHANNEL_1		(1 << 3)		/* Return all voice frames on channel 1 */
#define AST_BRIDGE_IGNORE_SIGS			(1 << 4)		/* Ignore all signal frames except NULL */


/* Set two channels to compatible formats -- call before ast_channel_bridge in general .  Returns 0 on success
   and -1 if it could not be done */
int ast_channel_make_compatible(struct ast_channel *c0, struct ast_channel *c1);

/* Bridge two channels (c0 and c1) together.  If an important frame occurs, we return that frame in
   *rf (remember, it could be NULL) and which channel (0 or 1) in rc */
int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc);

/* This is a very strange and freaky function used primarily for transfer.  Suppose that
   "original" and "clone" are two channels in random situations.  This function takes
   the guts out of "clone" and puts them into the "original" channel, then alerts the
   channel driver of the change, asking it to fixup any private information (like the
   p->owner pointer) that is affected by the change.  The physical layer of the original
   channel is hung up.  */
int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone);

/* Give a name to a state */
char *ast_state2str(int state);

/* Options: Some low-level drivers may implement "options" allowing fine tuning of the
   low level channel.  See frame.h for options.  Note that many channel drivers may support
   none or a subset of those features, and you should not count on this if you want your
   asterisk application to be portable.  They're mainly useful for tweaking performance */

/* Set an option on a channel (see frame.h), optionally blocking awaiting the reply */
int ast_channel_setoption(struct ast_channel *channel, int option, void *data, int datalen, int block);

/* Query the value of an option, optionally blocking until a reply is received */
struct ast_frame *ast_channel_queryoption(struct ast_channel *channel, int option, void *data, int *datalen, int block);

#ifdef DO_CRASH
#define CRASH do { *((int *)0) = 0; } while(0)
#else
#define CRASH do { } while(0)
#endif

#define CHECK_BLOCKING(c) { 	 \
							if ((c)->blocking) {\
								ast_log(LOG_WARNING, "Thread %ld Blocking '%s', already blocked by thread %ld in procedure %s\n", pthread_self(), (c)->name, (c)->blocker, (c)->blockproc); \
								CRASH; \
							} else { \
								(c)->blocker = pthread_self(); \
								(c)->blockproc = __PRETTY_FUNCTION__; \
									c->blocking = -1; \
									} }

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
