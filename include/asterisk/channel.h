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
 * \brief General Asterisk PBX channel definitions.
 */

#ifndef _ASTERISK_CHANNEL_H
#define _ASTERISK_CHANNEL_H

#include <unistd.h>
#include <setjmp.h>
#ifdef POLLCOMPAT 
#include "asterisk/poll-compat.h"
#else
#include <sys/poll.h>
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Max length of an extension */
#define AST_MAX_EXTENSION	80

#define AST_MAX_CONTEXT		80

#define AST_CHANNEL_NAME	80

#include "asterisk/compat.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/chanvars.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/cdr.h"
#include "asterisk/monitor.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"

#define MAX_LANGUAGE		20

#define MAX_MUSICCLASS		20

#define AST_MAX_FDS		8

enum ast_bridge_result {
	AST_BRIDGE_COMPLETE = 0,
	AST_BRIDGE_FAILED = -1,
	AST_BRIDGE_FAILED_NOWARN = -2,
	AST_BRIDGE_RETRY = -3,
};

typedef unsigned long long ast_group_t;

struct ast_generator {
	void *(*alloc)(struct ast_channel *chan, void *params);
	void (*release)(struct ast_channel *chan, void *data);
	int (*generate)(struct ast_channel *chan, void *data, int len, int samples);
};

struct ast_callerid {
	/*! Malloc'd Dialed Number Identifier */
	char *cid_dnid;				
	/*! Malloc'd Caller Number */
	char *cid_num;
	/*! Malloc'd Caller Name */
	char *cid_name;
	/*! Malloc'd ANI */
	char *cid_ani;			
	/*! Malloc'd RDNIS */
	char *cid_rdnis;
	/*! Callerid presentation/screening */
	int cid_pres;
	/*! Callerid ANI 2 (Info digits) */
	int cid_ani2;
	/*! Callerid Type of Number */
	int cid_ton;
	/*! Callerid Transit Network Select */
	int cid_tns;
};

/*! Structure to describe a channel "technology" */

struct ast_channel_tech {
	const char * const type;
	const char * const description;

	/*! Bitmap of formats this channel can handle */
	int capabilities;

	/*! Technology Properties */
	int properties;

	struct ast_channel *(* const requester)(const char *type, int format, void *data, int *cause);

	int (* const devicestate)(void *data);

	/*! Send a literal DTMF digit */
	int (* const send_digit)(struct ast_channel *chan, char digit);

	/*! Call a given phone number (address, etc), but don't
	   take longer than timeout seconds to do so.  */
	int (* const call)(struct ast_channel *chan, char *addr, int timeout);

	/*! Hangup (and possibly destroy) the channel */
	int (* const hangup)(struct ast_channel *chan);

	/*! Answer the line */
	int (* const answer)(struct ast_channel *chan);

	/*! Read a frame, in standard format */
	struct ast_frame * (* const read)(struct ast_channel *chan);

	/*! Write a frame, in standard format */
	int (* const write)(struct ast_channel *chan, struct ast_frame *frame);

	/*! Display or transmit text */
	int (* const send_text)(struct ast_channel *chan, const char *text);

	/*! Display or send an image */
	int (* const send_image)(struct ast_channel *chan, struct ast_frame *frame);

	/*! Send HTML data */
	int (* const send_html)(struct ast_channel *chan, int subclass, const char *data, int len);

	/*! Handle an exception, reading a frame */
	struct ast_frame * (* const exception)(struct ast_channel *chan);

	/*! Bridge two channels of the same type together */
	enum ast_bridge_result (* const bridge)(struct ast_channel *c0, struct ast_channel *c1, int flags,
						struct ast_frame **fo, struct ast_channel **rc, int timeoutms);

	/*! Indicate a particular condition (e.g. AST_CONTROL_BUSY or AST_CONTROL_RINGING or AST_CONTROL_CONGESTION */
	int (* const indicate)(struct ast_channel *c, int condition);

	/*! Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links */
	int (* const fixup)(struct ast_channel *oldchan, struct ast_channel *newchan);

	/*! Set a given option */
	int (* const setoption)(struct ast_channel *chan, int option, void *data, int datalen);

	/*! Query a given option */
	int (* const queryoption)(struct ast_channel *chan, int option, void *data, int *datalen);

	/*! Blind transfer other side */
	int (* const transfer)(struct ast_channel *chan, const char *newdest);

	/*! Write a frame, in standard format */
	int (* const write_video)(struct ast_channel *chan, struct ast_frame *frame);

	/*! Find bridged channel */
	struct ast_channel *(* const bridged_channel)(struct ast_channel *chan, struct ast_channel *bridge);
};


enum chanspy_states {
	CHANSPY_NEW = 0,
	CHANSPY_RUNNING = 1,
	CHANSPY_DONE = 2,
};

enum chanspy_flags {
	CHANSPY_MIXAUDIO = (1 << 0),
	CHANSPY_READ_VOLADJUST = (1 << 1),
	CHANSPY_WRITE_VOLADJUST = (1 << 2),
	CHANSPY_FORMAT_AUDIO = (1 << 3),
	CHANSPY_TRIGGER_MODE = (3 << 4),
	CHANSPY_TRIGGER_READ = (1 << 4),
	CHANSPY_TRIGGER_WRITE = (2 << 4),
	CHANSPY_TRIGGER_NONE = (3 << 4),
	CHANSPY_TRIGGER_FLUSH = (1 << 6),
};

struct ast_channel_spy_queue {
	struct ast_frame *head;
	unsigned int samples;
	unsigned int format;
};

struct ast_channel_spy {
	ast_mutex_t lock;
	ast_cond_t trigger;
	struct ast_channel_spy_queue read_queue;
	struct ast_channel_spy_queue write_queue;
	unsigned int flags;
	enum chanspy_states status;
	const char *type;
	/* The volume adjustment values are very straightforward:
	   positive values cause the samples to be multiplied by that amount
	   negative values cause the samples to be divided by the absolute value of that amount
	*/
	int read_vol_adjustment;
	int write_vol_adjustment;
	AST_LIST_ENTRY(ast_channel_spy) list;
};

struct ast_channel_spy_list;

/*! Main Channel structure associated with a channel. */
/*! 
 * This is the side of it mostly used by the pbx and call management.
 */
struct ast_channel {
	/*! ASCII Description of channel name */
	char name[AST_CHANNEL_NAME];
	
	/*! Technology */
	const struct ast_channel_tech *tech;
	/*! Private data used by the technology driver */
	void *tech_pvt;

	/*! Language requested */
	char language[MAX_LANGUAGE];		
	/*! Type of channel */
	const char *type;				
	/*! File descriptor for channel -- Drivers will poll on these file descriptors, so at least one must be non -1.  */
	int fds[AST_MAX_FDS];			

	/*! Default music class */
	char musicclass[MAX_MUSICCLASS];
	/*! Music State*/
	void *music_state;
	/*! Current generator data if there is any */
	void *generatordata;
	/*! Current active data generator */
	struct ast_generator *generator;

	/*! Who are we bridged to, if we're bridged  Do not access directly,
	    use ast_bridged_channel(chan) */
	struct ast_channel *_bridge;
	/*! Channel that will masquerade as us */
	struct ast_channel *masq;		
	/*! Who we are masquerading as */
	struct ast_channel *masqr;		
	/*! Call Detail Record Flags */
	int cdrflags;										   
	/*! Whether or not we have been hung up...  Do not set this value
	    directly, use ast_softhangup */
	int _softhangup;				
	/*! Non-zero, set to actual time when channel is to be hung up */
	time_t	whentohangup;
	/*! If anyone is blocking, this is them */
	pthread_t blocker;			
	/*! Lock, can be used to lock a channel for some operations */
	ast_mutex_t lock;			
	/*! Procedure causing blocking */
	const char *blockproc;			

	/*! Current application */
	char *appl;				
	/*! Data passed to current application */
	char *data;				
	
	/*! Which fd had an event detected on */
	int fdno;				
	/*! Schedule context */
	struct sched_context *sched;		
	/*! For streaming playback, the schedule ID */
	int streamid;				
	/*! Stream itself. */
	struct ast_filestream *stream;		
	/*! For streaming playback, the schedule ID */
	int vstreamid;				
	/*! Stream itself. */
	struct ast_filestream *vstream;		
	/*! Original writer format */
	int oldwriteformat;			
	
	/*! Timing fd */
	int timingfd;
	int (*timingfunc)(void *data);
	void *timingdata;

	/*! State of line -- Don't write directly, use ast_setstate */
	int _state;				
	/*! Number of rings so far */
	int rings;				

	/*! Kinds of data this channel can natively handle */
	int nativeformats;			
	/*! Requested read format */
	int readformat;				
	/*! Requested write format */
	int writeformat;			

	struct ast_callerid cid;
		
	/*! Current extension context */
	char context[AST_MAX_CONTEXT];
	/*! Current non-macro context */
	char macrocontext[AST_MAX_CONTEXT];	
	/*! Current non-macro extension */
	char macroexten[AST_MAX_EXTENSION];
	/*! Current non-macro priority */
	int macropriority;
	/*! Current extension number */
	char exten[AST_MAX_EXTENSION];		
	/* Current extension priority */
	int priority;						
	/*! Any/all queued DTMF characters */
	char dtmfq[AST_MAX_EXTENSION];		
	/*! DTMF frame */
	struct ast_frame dtmff;			

	/*! PBX private structure */
	struct ast_pbx *pbx;
	/*! Set BEFORE PBX is started to determine AMA flags */
	int 	amaflags;			
	/*! Account code for billing */
	char 	accountcode[AST_MAX_ACCOUNT_CODE];		
	/*! Call Detail Record */
	struct ast_cdr *cdr;			
	/*! Whether or not ADSI is detected on CPE */
	int	adsicpe;
	/*! Where to forward to if asked to dial on this interface */
	char call_forward[AST_MAX_EXTENSION];

	/*! Tone zone */
	struct tone_zone *zone;

	/* Channel monitoring */
	struct ast_channel_monitor *monitor;

	/*! Track the read/written samples for monitor use */
	unsigned long insmpl;
	unsigned long outsmpl;

	/* Frames in/out counters */
	unsigned int fin;
	unsigned int fout;

	/* Unique Channel Identifier */
	char uniqueid[32];

	/* Why is the channel hanged up */
	int hangupcause;
	
	/* A linked list for variables */
	struct varshead varshead;

	unsigned int callgroup;
	unsigned int pickupgroup;

	/*! channel flags of AST_FLAG_ type */
	unsigned int flags;
	
	/* ISDN Transfer Capbility - AST_FLAG_DIGITAL is not enough */
	unsigned short transfercapability;

	struct ast_frame *readq;
	int alertpipe[2];
	/*! Write translation path */
	struct ast_trans_pvt *writetrans;
	/*! Read translation path */
	struct ast_trans_pvt *readtrans;
	/*! Raw read format */
	int rawreadformat;
	/*! Raw write format */
	int rawwriteformat;

	/*! Chan Spy stuff */
	struct ast_channel_spy_list *spies;

	/*! For easy linking */
	struct ast_channel *next;
};

/* Channel tech properties: */
/* Channels have this property if they can accept input with jitter; i.e. most VoIP channels */
#define AST_CHAN_TP_WANTSJITTER	(1 << 0)	

/* This flag has been deprecated by the transfercapbilty data member in struct ast_channel */
/* #define AST_FLAG_DIGITAL	(1 << 0) */	/* if the call is a digital ISDN call */
#define AST_FLAG_DEFER_DTMF	(1 << 1)	/* if dtmf should be deferred */
#define AST_FLAG_WRITE_INT	(1 << 2)	/* if write should be interrupt generator */
#define AST_FLAG_BLOCKING	(1 << 3)	/* if we are blocking */
#define AST_FLAG_ZOMBIE		(1 << 4)	/* if we are a zombie */
#define AST_FLAG_EXCEPTION	(1 << 5)	/* if there is a pending exception */
#define AST_FLAG_MOH		(1 << 6)	/* XXX anthm promises me this will disappear XXX listening to moh */
#define AST_FLAG_SPYING		(1 << 7)	/* XXX might also go away XXX is spying on someone */
#define AST_FLAG_NBRIDGE	(1 << 8)	/* is it in a native bridge */
#define AST_FLAG_IN_AUTOLOOP	(1 << 9)	/* the channel is in an auto-incrementing dialplan processor,
						   so when ->priority is set, it will get incremented before
						   finding the next priority to run
						*/

#define AST_FEATURE_PLAY_WARNING	(1 << 0)
#define AST_FEATURE_REDIRECT		(1 << 1)
#define AST_FEATURE_DISCONNECT		(1 << 2)
#define AST_FEATURE_ATXFER		(1 << 3)
#define AST_FEATURE_AUTOMON		(1 << 4)

#define AST_FEATURE_FLAG_NEEDSDTMF	(1 << 0)
#define AST_FEATURE_FLAG_CALLEE		(1 << 1)
#define AST_FEATURE_FLAG_CALLER		(1 << 2)

struct ast_bridge_config {
	struct ast_flags features_caller;
	struct ast_flags features_callee;
	struct timeval start_time;
	long feature_timer;
	long timelimit;
	long play_warning;
	long warning_freq;
	char *warning_sound;
	char *end_sound;
	char *start_sound;
	int firstpass;
	unsigned int flags;
};

struct chanmon;

#define LOAD_OH(oh) {	\
	oh.context = context; \
	oh.exten = exten; \
	oh.priority = priority; \
	oh.cid_num = cid_num; \
	oh.cid_name = cid_name; \
	oh.vars = vars; \
	oh.parent_channel = NULL; \
} 

struct outgoing_helper {
	const char *context;
	const char *exten;
	int priority;
	const char *cid_num;
	const char *cid_name;
	struct ast_variable *vars;
	struct ast_channel *parent_channel;
};

#define AST_CDR_TRANSFER	(1 << 0)
#define AST_CDR_FORWARD		(1 << 1)
#define AST_CDR_CALLWAIT	(1 << 2)
#define AST_CDR_CONFERENCE	(1 << 3)

#define AST_ADSI_UNKNOWN	(0)
#define AST_ADSI_AVAILABLE	(1)
#define AST_ADSI_UNAVAILABLE	(2)
#define AST_ADSI_OFFHOOKONLY	(3)

#define AST_SOFTHANGUP_DEV			(1 << 0)	/* Soft hangup by device */
#define AST_SOFTHANGUP_ASYNCGOTO	(1 << 1)	/* Soft hangup for async goto */
#define AST_SOFTHANGUP_SHUTDOWN		(1 << 2)
#define AST_SOFTHANGUP_TIMEOUT		(1 << 3)
#define AST_SOFTHANGUP_APPUNLOAD	(1 << 4)
#define AST_SOFTHANGUP_EXPLICIT		(1 << 5)
#define AST_SOFTHANGUP_UNBRIDGE     (1 << 6)

/* Bits 0-15 of state are reserved for the state (up/down) of the line */
/*! Channel is down and available */
#define AST_STATE_DOWN		0		
/*! Channel is down, but reserved */
#define AST_STATE_RESERVED	1		
/*! Channel is off hook */
#define AST_STATE_OFFHOOK	2		
/*! Digits (or equivalent) have been dialed */
#define AST_STATE_DIALING	3		
/*! Line is ringing */
#define AST_STATE_RING		4		
/*! Remote end is ringing */
#define AST_STATE_RINGING	5		
/*! Line is up */
#define AST_STATE_UP		6		
/*! Line is busy */
#define AST_STATE_BUSY  	7		
/*! Digits (or equivalent) have been dialed while offhook */
#define AST_STATE_DIALING_OFFHOOK	8
/*! Channel has detected an incoming call and is waiting for ring */
#define AST_STATE_PRERING       9

/* Bits 16-32 of state are reserved for flags */
/*! Do not transmit voice data */
#define AST_STATE_MUTE		(1 << 16)	

/*! Create a channel structure */
/*! Returns NULL on failure to allocate. New channels are 
	by default set to the "default" context and
	extension "s"
 */
struct ast_channel *ast_channel_alloc(int needalertpipe);

/*! Queue an outgoing frame */
int ast_queue_frame(struct ast_channel *chan, struct ast_frame *f);

/*! Queue a hangup frame */
int ast_queue_hangup(struct ast_channel *chan);

/*! Queue a control frame */
int ast_queue_control(struct ast_channel *chan, int control);

/*! Change the state of a channel */
int ast_setstate(struct ast_channel *chan, int state);

void ast_change_name(struct ast_channel *chan, char *newname);

/*! Free a channel structure */
void  ast_channel_free(struct ast_channel *);

/*! Requests a channel */
/*! 
 * \param type type of channel to request
 * \param format requested channel format
 * \param data data to pass to the channel requester
 * \param status status
 * Request a channel of a given type, with data as optional information used 
 * by the low level module
 * Returns an ast_channel on success, NULL on failure.
 */
struct ast_channel *ast_request(const char *type, int format, void *data, int *status);

/*!
 * \param type type of channel to request
 * \param format requested channel format
 * \param data data to pass to the channel requester
 * \param timeout maximum amount of time to wait for an answer
 * \param reason why unsuccessful (if unsuceessful)
 * \param cidnum Caller-ID Number
 * \param cidname Caller-ID Name
 * Request a channel of a given type, with data as optional information used 
 * by the low level module and attempt to place a call on it
 * Returns an ast_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct ast_channel *ast_request_and_dial(const char *type, int format, void *data, int timeout, int *reason, const char *cidnum, const char *cidname);

struct ast_channel *__ast_request_and_dial(const char *type, int format, void *data, int timeout, int *reason, const char *cidnum, const char *cidname, struct outgoing_helper *oh);

/*! Register a channel technology */
/*! 
 * \param tech Structure defining channel technology or "type"
 * Called by a channel module to register the kind of channels it supports.
 * Returns 0 on success, -1 on failure.
 */
int ast_channel_register(const struct ast_channel_tech *tech);

/*! Unregister a channel technology */
/*
 * \param tech Structure defining channel technology or "type" that was previously registered
 * No return value.
 */
void ast_channel_unregister(const struct ast_channel_tech *tech);

/*! Get a channel technology structure by name
 * \param name name of technology to find
 * \return a pointer to the structure, or NULL if no matching technology found
 */
const struct ast_channel_tech *ast_get_channel_tech(const char *name);

/*! Hang up a channel  */
/*! 
 * \param chan channel to hang up
 * This function performs a hard hangup on a channel.  Unlike the soft-hangup, this function
 * performs all stream stopping, etc, on the channel that needs to end.
 * chan is no longer valid after this call.
 * Returns 0 on success, -1 on failure.
 */
int ast_hangup(struct ast_channel *chan);

/*! Softly hangup up a channel */
/*! 
 * \param chan channel to be soft-hung-up
 * Call the protocol layer, but don't destroy the channel structure (use this if you are trying to
 * safely hangup a channel managed by another thread.
 * \param cause	Ast hangupcause for hangup
 * Returns 0 regardless
 */
int ast_softhangup(struct ast_channel *chan, int cause);

/*! Softly hangup up a channel (no channel lock) 
 * \param chan channel to be soft-hung-up
 * \param cause	Ast hangupcause for hangup */
int ast_softhangup_nolock(struct ast_channel *chan, int cause);

/*! Check to see if a channel is needing hang up */
/*! 
 * \param chan channel on which to check for hang up
 * This function determines if the channel is being requested to be hung up.
 * Returns 0 if not, or 1 if hang up is requested (including time-out).
 */
int ast_check_hangup(struct ast_channel *chan);

/*! Compare a offset with the settings of when to hang a channel up */
/*! 
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time
 * \return 1, 0, or -1
 * This function compares a offset from current time with the absolute time 
 * out on a channel (when to hang up). If the absolute time out on a channel
 * is earlier than current time plus the offset, it returns 1, if the two
 * time values are equal, it return 0, otherwise, it retturn -1.
 */
int ast_channel_cmpwhentohangup(struct ast_channel *chan, time_t offset);

/*! Set when to hang a channel up */
/*! 
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time of when to hang up
 * This function sets the absolute time out on a channel (when to hang up).
 */
void ast_channel_setwhentohangup(struct ast_channel *chan, time_t offset);

/*! Answer a ringing call */
/*!
 * \param chan channel to answer
 * This function answers a channel and handles all necessary call
 * setup functions.
 * Returns 0 on success, -1 on failure
 */
int ast_answer(struct ast_channel *chan);

/*! Make a call */
/*! 
 * \param chan which channel to make the call on
 * \param addr destination of the call
 * \param timeout time to wait on for connect
 * Place a call, take no longer than timeout ms.  Returns -1 on failure, 
   0 on not enough time (does not auto matically stop ringing), and  
   the number of seconds the connect took otherwise.
   Returns 0 on success, -1 on failure
   */
int ast_call(struct ast_channel *chan, char *addr, int timeout);

/*! Indicates condition of channel */
/*! 
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * Indicate a condition such as AST_CONTROL_BUSY, AST_CONTROL_RINGING, or AST_CONTROL_CONGESTION on a channel
 * Returns 0 on success, -1 on failure
 */
int ast_indicate(struct ast_channel *chan, int condition);

/* Misc stuff */

/*! Wait for input on a channel */
/*! 
 * \param chan channel to wait on
 * \param ms length of time to wait on the channel
 * Wait for input on a channel for a given # of milliseconds (<0 for indefinite). 
  Returns < 0 on  failure, 0 if nothing ever arrived, and the # of ms remaining otherwise */
int ast_waitfor(struct ast_channel *chan, int ms);

/*! Wait for a specied amount of time, looking for hangups */
/*!
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
 * Waits for a specified amount of time, servicing the channel as required.
 * returns -1 on hangup, otherwise 0.
 */
int ast_safe_sleep(struct ast_channel *chan, int ms);

/*! Wait for a specied amount of time, looking for hangups and a condition argument */
/*!
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
 * \param cond a function pointer for testing continue condition
 * \param data argument to be passed to the condition test function
 * Waits for a specified amount of time, servicing the channel as required. If cond
 * returns 0, this function returns.
 * returns -1 on hangup, otherwise 0.
 */
int ast_safe_sleep_conditional(struct ast_channel *chan, int ms, int (*cond)(void*), void *data );

/*! Waits for activity on a group of channels */
/*! 
 * \param chan an array of pointers to channels
 * \param n number of channels that are to be waited upon
 * \param fds an array of fds to wait upon
 * \param nfds the number of fds to wait upon
 * \param exception exception flag
 * \param outfd fd that had activity on it
 * \param ms how long the wait was
 * Big momma function here.  Wait for activity on any of the n channels, or any of the nfds
   file descriptors.  Returns the channel with activity, or NULL on error or if an FD
   came first.  If the FD came first, it will be returned in outfd, otherwise, outfd
   will be -1 */
struct ast_channel *ast_waitfor_nandfds(struct ast_channel **chan, int n, int *fds, int nfds, int *exception, int *outfd, int *ms);

/*! Waits for input on a group of channels */
/*! Wait for input on an array of channels for a given # of milliseconds. Return channel
   with activity, or NULL if none has activity.  time "ms" is modified in-place, if applicable */
struct ast_channel *ast_waitfor_n(struct ast_channel **chan, int n, int *ms);

/*! Waits for input on an fd */
/*! This version works on fd's only.  Be careful with it. */
int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception);


/*! Reads a frame */
/*!
 * \param chan channel to read a frame from
 * Read a frame.  Returns a frame, or NULL on error.  If it returns NULL, you
   best just stop reading frames and assume the channel has been
   disconnected. */
struct ast_frame *ast_read(struct ast_channel *chan);

/*! Write a frame to a channel */
/*!
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * This function writes the given frame to the indicated channel.
 * It returns 0 on success, -1 on failure.
 */
int ast_write(struct ast_channel *chan, struct ast_frame *frame);

/*! Write video frame to a channel */
/*!
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * This function writes the given frame to the indicated channel.
 * It returns 1 on success, 0 if not implemented, and -1 on failure.
 */
int ast_write_video(struct ast_channel *chan, struct ast_frame *frame);

/* Send empty audio to prime a channel driver */
int ast_prod(struct ast_channel *chan);

/*! Sets read format on channel chan */
/*! 
 * \param chan channel to change
 * \param format format to change to
 * Set read format for channel to whichever component of "format" is best. 
 * Returns 0 on success, -1 on failure
 */
int ast_set_read_format(struct ast_channel *chan, int format);

/*! Sets write format on channel chan */
/*! 
 * \param chan channel to change
 * \param format new format for writing
 * Set write format for channel to whichever compoent of "format" is best. 
 * Returns 0 on success, -1 on failure
 */
int ast_set_write_format(struct ast_channel *chan, int format);

/*! Sends text to a channel */
/*! 
 * \param chan channel to act upon
 * \param text string of text to send on the channel
 * Write text to a display on a channel
 * Returns 0 on success, -1 on failure
 */
int ast_sendtext(struct ast_channel *chan, const char *text);

/*! Receives a text character from a channel */
/*! 
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * Read a char of text from a channel
 * Returns 0 on success, -1 on failure
 */
int ast_recvchar(struct ast_channel *chan, int timeout);

/*! Send a DTMF digit to a channel */
/*! 
 * \param chan channel to act upon
 * \param digit the DTMF digit to send, encoded in ASCII
 * Send a DTMF digit to a channel.
 * Returns 0 on success, -1 on failure
 */
int ast_senddigit(struct ast_channel *chan, char digit);

/*! Receives a text string from a channel */
/*! 
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * \return the received text, or NULL to signify failure.
 * Read a string of text from a channel
 */
char *ast_recvtext(struct ast_channel *chan, int timeout);

/*! Browse channels in use */
/*! 
 * \param prev where you want to start in the channel list
 * Browse the channels currently in use 
 * Returns the next channel in the list, NULL on end.
 * If it returns a channel, that channel *has been locked*!
 */
struct ast_channel *ast_channel_walk_locked(const struct ast_channel *prev);

/*! Get channel by name (locks channel) */
struct ast_channel *ast_get_channel_by_name_locked(const char *chan);

/*! Get channel by name prefix (locks channel) */
struct ast_channel *ast_get_channel_by_name_prefix_locked(const char *name, const int namelen);

/*! Get channel by name prefix (locks channel) */
struct ast_channel *ast_walk_channel_by_name_prefix_locked(struct ast_channel *chan, const char *name, const int namelen);

/*--- ast_get_channel_by_exten_locked: Get channel by exten (and optionally context) and lock it */
struct ast_channel *ast_get_channel_by_exten_locked(const char *exten, const char *context);

/*! Waits for a digit */
/*! 
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait
 * Wait for a digit.  Returns <0 on error, 0 on no entry, and the digit on success. */
int ast_waitfordigit(struct ast_channel *c, int ms);

/* Same as above with audio fd for outputing read audio and ctrlfd to monitor for
   reading. Returns 1 if ctrlfd becomes available */
int ast_waitfordigit_full(struct ast_channel *c, int ms, int audiofd, int ctrlfd);

/*! Reads multiple digits */
/*! 
 * \param c channel to read from
 * \param s string to read in to.  Must be at least the size of your length
 * \param len how many digits to read (maximum)
 * \param timeout how long to timeout between digits
 * \param rtimeout timeout to wait on the first digit
 * \param enders digits to end the string
 * Read in a digit string "s", max length "len", maximum timeout between 
   digits "timeout" (-1 for none), terminated by anything in "enders".  Give them rtimeout
   for the first digit.  Returns 0 on normal return, or 1 on a timeout.  In the case of
   a timeout, any digits that were read before the timeout will still be available in s.  
   RETURNS 2 in full version when ctrlfd is available, NOT 1*/
int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int rtimeout, char *enders);
int ast_readstring_full(struct ast_channel *c, char *s, int len, int timeout, int rtimeout, char *enders, int audiofd, int ctrlfd);

/*! Report DTMF on channel 0 */
#define AST_BRIDGE_DTMF_CHANNEL_0		(1 << 0)		
/*! Report DTMF on channel 1 */
#define AST_BRIDGE_DTMF_CHANNEL_1		(1 << 1)		
/*! Return all voice frames on channel 0 */
#define AST_BRIDGE_REC_CHANNEL_0		(1 << 2)		
/*! Return all voice frames on channel 1 */
#define AST_BRIDGE_REC_CHANNEL_1		(1 << 3)		
/*! Ignore all signal frames except NULL */
#define AST_BRIDGE_IGNORE_SIGS			(1 << 4)		


/*! Makes two channel formats compatible */
/*! 
 * \param c0 first channel to make compatible
 * \param c1 other channel to make compatible
 * Set two channels to compatible formats -- call before ast_channel_bridge in general .  Returns 0 on success
   and -1 if it could not be done */
int ast_channel_make_compatible(struct ast_channel *c0, struct ast_channel *c1);

/*! Bridge two channels together */
/*! 
 * \param c0 first channel to bridge
 * \param c1 second channel to bridge
 * \param config config for the channels
 * \param fo destination frame(?)
 * \param rc destination channel(?)
 * Bridge two channels (c0 and c1) together.  If an important frame occurs, we return that frame in
   *rf (remember, it could be NULL) and which channel (0 or 1) in rc */
/* int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc); */
int ast_channel_bridge(struct ast_channel *c0,struct ast_channel *c1,struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc);

/*! Weird function made for call transfers */
/*! 
 * \param original channel to make a copy of
 * \param clone copy of the original channel
 * This is a very strange and freaky function used primarily for transfer.  Suppose that
   "original" and "clone" are two channels in random situations.  This function takes
   the guts out of "clone" and puts them into the "original" channel, then alerts the
   channel driver of the change, asking it to fixup any private information (like the
   p->owner pointer) that is affected by the change.  The physical layer of the original
   channel is hung up.  */
int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone);

/*! Gives the string form of a given cause code */
/*! 
 * \param state cause to get the description of
 * Give a name to a cause code
 * Returns the text form of the binary cause code given
 */
const char *ast_cause2str(int state);

/*! Gives the string form of a given channel state */
/*! 
 * \param state state to get the name of
 * Give a name to a state 
 * Returns the text form of the binary state given
 */
char *ast_state2str(int state);

/*! Gives the string form of a given transfer capability */
/*!
 * \param transfercapability transfercapabilty to get the name of
 * Give a name to a transfercapbility
 * See above
 * Returns the text form of the binary transfer capbility
 */
char *ast_transfercapability2str(int transfercapability);

/* Options: Some low-level drivers may implement "options" allowing fine tuning of the
   low level channel.  See frame.h for options.  Note that many channel drivers may support
   none or a subset of those features, and you should not count on this if you want your
   asterisk application to be portable.  They're mainly useful for tweaking performance */

/*! Sets an option on a channel */
/*! 
 * \param channel channel to set options on
 * \param option option to change
 * \param data data specific to option
 * \param datalen length of the data
 * \param block blocking or not
 * Set an option on a channel (see frame.h), optionally blocking awaiting the reply 
 * Returns 0 on success and -1 on failure
 */
int ast_channel_setoption(struct ast_channel *channel, int option, void *data, int datalen, int block);

/*! Pick the best codec  */
/* Choose the best codec...  Uhhh...   Yah. */
extern int ast_best_codec(int fmts);


/*! Checks the value of an option */
/*! 
 * Query the value of an option, optionally blocking until a reply is received
 * Works similarly to setoption except only reads the options.
 */
struct ast_frame *ast_channel_queryoption(struct ast_channel *channel, int option, void *data, int *datalen, int block);

/*! Checks for HTML support on a channel */
/*! Returns 0 if channel does not support HTML or non-zero if it does */
int ast_channel_supports_html(struct ast_channel *channel);

/*! Sends HTML on given channel */
/*! Send HTML or URL on link.  Returns 0 on success or -1 on failure */
int ast_channel_sendhtml(struct ast_channel *channel, int subclass, const char *data, int datalen);

/*! Sends a URL on a given link */
/*! Send URL on link.  Returns 0 on success or -1 on failure */
int ast_channel_sendurl(struct ast_channel *channel, const char *url);

/*! Defers DTMF */
/*! Defer DTMF so that you only read things like hangups and audio.  Returns
   non-zero if channel was already DTMF-deferred or 0 if channel is just now
   being DTMF-deferred */
int ast_channel_defer_dtmf(struct ast_channel *chan);

/*! Undeos a defer */
/*! Undo defer.  ast_read will return any dtmf characters that were queued */
void ast_channel_undefer_dtmf(struct ast_channel *chan);

/*! Initiate system shutdown -- prevents new channels from being allocated.
    If "hangup" is non-zero, all existing channels will receive soft
     hangups */
void ast_begin_shutdown(int hangup);

/*! Cancels an existing shutdown and returns to normal operation */
void ast_cancel_shutdown(void);

/*! Returns number of active/allocated channels */
int ast_active_channels(void);

/*! Returns non-zero if Asterisk is being shut down */
int ast_shutting_down(void);

/*! Activate a given generator */
int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params);

/*! Deactive an active generator */
void ast_deactivate_generator(struct ast_channel *chan);

void ast_set_callerid(struct ast_channel *chan, const char *cidnum, const char *cidname, const char *ani);

/*! Start a tone going */
int ast_tonepair_start(struct ast_channel *chan, int freq1, int freq2, int duration, int vol);
/*! Stop a tone from playing */
void ast_tonepair_stop(struct ast_channel *chan);
/*! Play a tone pair for a given amount of time */
int ast_tonepair(struct ast_channel *chan, int freq1, int freq2, int duration, int vol);

/*! Automatically service a channel for us... */
int ast_autoservice_start(struct ast_channel *chan);

/*! Stop servicing a channel for us...  Returns -1 on error or if channel has been hungup */
int ast_autoservice_stop(struct ast_channel *chan);

/* If built with zaptel optimizations, force a scheduled expiration on the
   timer fd, at which point we call the callback function / data */
int ast_settimeout(struct ast_channel *c, int samples, int (*func)(void *data), void *data);

/*!	\brief Transfer a channel (if supported).  Returns -1 on error, 0 if not supported
   and 1 if supported and requested 
	\param chan current channel
	\param dest destination extension for transfer
*/
int ast_transfer(struct ast_channel *chan, char *dest);

/*!	\brief  Start masquerading a channel
	XXX This is a seriously wacked out operation.  We're essentially putting the guts of
           the clone channel into the original channel.  Start by killing off the original
           channel's backend.   I'm not sure we're going to keep this function, because
           while the features are nice, the cost is very high in terms of pure nastiness. XXX
	\param chan 	Channel to masquerade
*/
int ast_do_masquerade(struct ast_channel *chan);

/*!	\brief Find bridged channel 
	\param chan Current channel
*/
struct ast_channel *ast_bridged_channel(struct ast_channel *chan);

/*!
  \brief Inherits channel variable from parent to child channel
  \param parent Parent channel
  \param child Child channel

  Scans all channel variables in the parent channel, looking for those
  that should be copied into the child channel.
  Variables whose names begin with a single '_' are copied into the
  child channel with the prefix removed.
  Variables whose names begin with '__' are copied into the child
  channel with their names unchanged.
*/
void ast_channel_inherit_variables(const struct ast_channel *parent, struct ast_channel *child);

/*!
  \brief adds a list of channel variables to a channel
  \param chan the channel
  \param vars a linked list of variables

  Variable names can be for a regular channel variable or a dialplan function
  that has the ability to be written to.
*/
void ast_set_variables(struct ast_channel *chan, struct ast_variable *vars);

/*!
  \brief Adds a spy to a channel, to begin receiving copies of the channel's audio frames.
  \param chan The channel to add the spy to.
  \param spy A pointer to ast_channel_spy structure describing how the spy is to be used.
  \return 0 for success, non-zero for failure

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
int ast_channel_spy_add(struct ast_channel *chan, struct ast_channel_spy *spy);

/*!
  \brief Remove a spy from a channel.
  \param chan The channel to remove the spy from
  \param spy The spy to be removed
  \return nothing

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
void ast_channel_spy_remove(struct ast_channel *chan, struct ast_channel_spy *spy);

/*!
  \brief Find all spies of a particular type on a channel and stop them.
  \param chan The channel to operate on
  \param type A character string identifying the type of spies to be stopped
  \return nothing

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
void ast_channel_spy_stop_by_type(struct ast_channel *chan, const char *type);

/*!
  \brief Read one (or more) frames of audio from a channel being spied upon.
  \param spy The spy to operate on
  \param samples The number of audio samples to read
  \return NULL for failure, one ast_frame pointer, or a chain of ast_frame pointers

  This function can return multiple frames if the spy structure needs to be 'flushed'
  due to mismatched queue lengths, or if the spy structure is configured to return
  unmixed audio (in which case each call to this function will return a frame of audio
  from each side of channel).

  Note: This function performs no locking; you must hold the spy's lock before calling
  this function. You must <b>not</b> hold the channel's lock at the same time.
 */
struct ast_frame *ast_channel_spy_read_frame(struct ast_channel_spy *spy, unsigned int samples);

/*!
  \brief Efficiently wait until audio is available for a spy, or an exception occurs.
  \param spy The spy to wait on
  \return nothing

  Note: The locking rules for this function are non-obvious... first, you must <b>not</b>
  hold the channel's lock when calling this function. Second, you must hold the spy's lock
  before making the function call; while the function runs the lock will be released, and
  when the trigger event occurs, the lock will be re-obtained. This means that when control
  returns to your code, you will again hold the spy's lock.
 */
void ast_channel_spy_trigger_wait(struct ast_channel_spy *spy);

struct ast_silence_generator;
struct ast_silence_generator *ast_channel_start_silence_generator(struct ast_channel *chan);
void ast_channel_stop_silence_generator(struct ast_channel *chan, struct ast_silence_generator *state);

/* Misc. functions below */

/* Helper function for migrating select to poll */
static inline int ast_fdisset(struct pollfd *pfds, int fd, int max, int *start)
{
	int x;
	for (x=start ? *start : 0;x<max;x++)
		if (pfds[x].fd == fd) {
			if (start) {
				if (x==*start)
					(*start)++;
			}
			return pfds[x].revents;
		}
	return 0;
}

#ifdef SOLARIS
static inline void timersub(struct timeval *tvend, struct timeval *tvstart, struct timeval *tvdiff)
{
	tvdiff->tv_sec = tvend->tv_sec - tvstart->tv_sec;
	tvdiff->tv_usec = tvend->tv_usec - tvstart->tv_usec;
	if (tvdiff->tv_usec < 0) {
		tvdiff->tv_sec --;
		tvdiff->tv_usec += 1000000;
	}

}
#endif

/*! Waits for activity on a group of channels */
/*! 
 * \param nfds the maximum number of file descriptors in the sets
 * \param rfds file descriptors to check for read availability
 * \param wfds file descriptors to check for write availability
 * \param efds file descriptors to check for exceptions (OOB data)
 * \param tvp timeout while waiting for events
 * This is the same as a standard select(), except it guarantees the
 * behaviour where the passed struct timeval is updated with how much
 * time was not slept while waiting for the specified events
 */
static inline int ast_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tvp)
{
#ifdef __linux__
	return select(nfds, rfds, wfds, efds, tvp);
#else
	if (tvp) {
		struct timeval tv, tvstart, tvend, tvlen;
		int res;

		tv = *tvp;
		gettimeofday(&tvstart, NULL);
		res = select(nfds, rfds, wfds, efds, tvp);
		gettimeofday(&tvend, NULL);
		timersub(&tvend, &tvstart, &tvlen);
		timersub(&tv, &tvlen, tvp);
		if (tvp->tv_sec < 0 || (tvp->tv_sec == 0 && tvp->tv_usec < 0)) {
			tvp->tv_sec = 0;
			tvp->tv_usec = 0;
		}
		return res;
	}
	else
		return select(nfds, rfds, wfds, efds, NULL);
#endif
}

#if !defined(ast_strdupa) && defined(__GNUC__)
# define ast_strdupa(s)									\
  (__extension__										\
    ({													\
      __const char *__old = (s);						\
      size_t __len = strlen (__old) + 1;				\
      char *__new = (char *) __builtin_alloca (__len);	\
      (char *) memcpy (__new, __old, __len);			\
    }))
#endif

#ifdef DO_CRASH
#define CRASH do { fprintf(stderr, "!! Forcing immediate crash a-la abort !!\n"); *((int *)0) = 0; } while(0)
#else
#define CRASH do { } while(0)
#endif

#define CHECK_BLOCKING(c) { 	 \
							if (ast_test_flag(c, AST_FLAG_BLOCKING)) {\
								ast_log(LOG_WARNING, "Thread %ld Blocking '%s', already blocked by thread %ld in procedure %s\n", (long) pthread_self(), (c)->name, (long) (c)->blocker, (c)->blockproc); \
								CRASH; \
							} else { \
								(c)->blocker = pthread_self(); \
								(c)->blockproc = __PRETTY_FUNCTION__; \
									ast_set_flag(c, AST_FLAG_BLOCKING); \
									} }

extern ast_group_t ast_get_group(char *s);
/* print call- and pickup groups into buffer */
extern char *ast_print_group(char *buf, int buflen, ast_group_t group);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CHANNEL_H */
