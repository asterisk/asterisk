/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \par See also:
 *  \arg \ref Def_Channel
 *  \arg \ref channel_drivers
 */

/*! \page Def_Channel Asterisk Channels
	\par What is a Channel?
	A phone call through Asterisk consists of an incoming
	connection and an outbound connection. Each call comes
	in through a channel driver that supports one technology,
	like SIP, ZAP, IAX2 etc. 
	\par
	Each channel driver, technology, has it's own private
	channel or dialog structure, that is technology-dependent.
	Each private structure is "owned" by a generic Asterisk
	channel structure, defined in channel.h and handled by
	channel.c .
	\par Call scenario
	This happens when an incoming call arrives to Asterisk
	-# Call arrives on a channel driver interface
	-# Channel driver creates a PBX channel and starts a 
	   pbx thread on the channel
	-# The dial plan is executed
	-# At this point at least two things can happen:
		-# The call is answered by Asterisk and 
		   Asterisk plays a media stream or reads media
		-# The dial plan forces Asterisk to create an outbound 
		   call somewhere with the dial (see \ref app_dial.c)
		   application
	.

	\par Bridging channels
	If Asterisk dials out this happens:
	-# Dial creates an outbound PBX channel and asks one of the
	   channel drivers to create a call
	-# When the call is answered, Asterisk bridges the media streams
	   so the caller on the first channel can speak with the callee
	   on the second, outbound channel
	-# In some cases where we have the same technology on both
	   channels and compatible codecs, a native bridge is used.
	   In a native bridge, the channel driver handles forwarding
	   of incoming audio to the outbound stream internally, without
	   sending audio frames through the PBX.
	-# In SIP, theres an "external native bridge" where Asterisk
	   redirects the endpoint, so audio flows directly between the
	   caller's phone and the callee's phone. Signalling stays in
	   Asterisk in order to be able to provide a proper CDR record
	   for the call.

	
	\par Masquerading channels
	In some cases, a channel can masquerade itself into another
	channel. This happens frequently in call transfers, where 
	a new channel takes over a channel that is already involved
	in a call. The new channel sneaks in and takes over the bridge
	and the old channel, now a zombie, is hung up.
	
	\par Reference
	\arg channel.c - generic functions
 	\arg channel.h - declarations of functions, flags and structures
	\arg translate.h - Transcoding support functions
	\arg \ref channel_drivers - Implemented channel drivers
	\arg \ref Def_Frame Asterisk Multimedia Frames

*/

#ifndef _ASTERISK_CHANNEL_H
#define _ASTERISK_CHANNEL_H

#include "asterisk/abstract_jb.h"

#include <unistd.h>

#include "asterisk/poll-compat.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_MAX_EXTENSION	80	/*!< Max length of an extension */
#define AST_MAX_CONTEXT		80	/*!< Max length of a context */
#define AST_CHANNEL_NAME	80	/*!< Max length of an ast_channel name */
#define MAX_LANGUAGE		20	/*!< Max length of the language setting */
#define MAX_MUSICCLASS		80	/*!< Max length of the music class setting */

#include "asterisk/compat.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/chanvars.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/cdr.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stringfields.h"
#include "asterisk/compiler.h"

#define DATASTORE_INHERIT_FOREVER	INT_MAX

#define AST_MAX_FDS		8
/*
 * We have AST_MAX_FDS file descriptors in a channel.
 * Some of them have a fixed use:
 */
#define AST_ALERT_FD	(AST_MAX_FDS-1)		/*!< used for alertpipe */
#define AST_TIMING_FD	(AST_MAX_FDS-2)		/*!< used for timingfd */
#define AST_AGENT_FD	(AST_MAX_FDS-3)		/*!< used by agents for pass through */
#define AST_GENERATOR_FD	(AST_MAX_FDS-4)	/*!< used by generator */

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
	/*! This function gets called with the channel unlocked, but is called in
	 *  the context of the channel thread so we know the channel is not going
	 *  to disappear.  This callback is responsible for locking the channel as
	 *  necessary. */
	int (*generate)(struct ast_channel *chan, void *data, int len, int samples);
};

/*! \brief Structure for a data store type */
struct ast_datastore_info {
	const char *type;		/*!< Type of data store */
	void *(*duplicate)(void *data); /*!< Duplicate item data (used for inheritance) */
	void (*destroy)(void *data);	/*!< Destroy function */
	/*!
	 * \brief Fix up channel references
	 *
	 * \arg data The datastore data
	 * \arg old_chan The old channel owning the datastore
	 * \arg new_chan The new channel owning the datastore
	 *
	 * This is exactly like the fixup callback of the channel technology interface.
	 * It allows a datastore to fix any pointers it saved to the owning channel
	 * in case that the owning channel has changed.  Generally, this would happen
	 * when the datastore is set to be inherited, and a masquerade occurs.
	 *
	 * \return nothing.
	 */
	void (*chan_fixup)(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan);
};

/*! \brief Structure for a channel data store */
struct ast_datastore {
	char *uid;		/*!< Unique data store identifier */
	void *data;		/*!< Contained data */
	const struct ast_datastore_info *info;	/*!< Data store type information */
	unsigned int inheritance;	/*!Number of levels this item will continue to be inherited */
	AST_LIST_ENTRY(ast_datastore) entry; /*!< Used for easy linking */
};

/*! \brief Structure for all kinds of caller ID identifications.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * Also, NULL and "" must be considered equivalent.
 */
struct ast_callerid {
	char *cid_dnid;		/*!< Malloc'd Dialed Number Identifier */
	char *cid_num;		/*!< Malloc'd Caller Number */
	char *cid_name;		/*!< Malloc'd Caller Name */
	char *cid_ani;		/*!< Malloc'd ANI */
	char *cid_rdnis;	/*!< Malloc'd RDNIS */
	int cid_pres;		/*!< Callerid presentation/screening */
	int cid_ani2;		/*!< Callerid ANI 2 (Info digits) */
	int cid_ton;		/*!< Callerid Type of Number */
	int cid_tns;		/*!< Callerid Transit Network Select */
};

/*! \brief 
	Structure to describe a channel "technology", ie a channel driver 
	See for examples:
	\arg chan_iax2.c - The Inter-Asterisk exchange protocol
	\arg chan_sip.c - The SIP channel driver
	\arg chan_zap.c - PSTN connectivity (TDM, PRI, T1/E1, FXO, FXS)

	If you develop your own channel driver, this is where you
	tell the PBX at registration of your driver what properties
	this driver supports and where different callbacks are 
	implemented.
*/
struct ast_channel_tech {
	const char * const type;
	const char * const description;

	int capabilities;		/*!< Bitmap of formats this channel can handle */

	int properties;			/*!< Technology Properties */

	/*! \brief Requester - to set up call data structures (pvt's) */
	struct ast_channel *(* const requester)(const char *type, int format, void *data, int *cause);

	int (* const devicestate)(void *data);	/*!< Devicestate call back */

	/*! \brief Start sending a literal DTMF digit */
	int (* const send_digit_begin)(struct ast_channel *chan, char digit);

	/*! \brief Stop sending a literal DTMF digit */
	int (* const send_digit_end)(struct ast_channel *chan, char digit, unsigned int duration);

	/*! \brief Call a given phone number (address, etc), but don't
	   take longer than timeout seconds to do so.  */
	int (* const call)(struct ast_channel *chan, char *addr, int timeout);

	/*! \brief Hangup (and possibly destroy) the channel */
	int (* const hangup)(struct ast_channel *chan);

	/*! \brief Answer the channel */
	int (* const answer)(struct ast_channel *chan);

	/*! \brief Read a frame, in standard format (see frame.h) */
	struct ast_frame * (* const read)(struct ast_channel *chan);

	/*! \brief Write a frame, in standard format (see frame.h) */
	int (* const write)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Display or transmit text */
	int (* const send_text)(struct ast_channel *chan, const char *text);

	/*! \brief Display or send an image */
	int (* const send_image)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Send HTML data */
	int (* const send_html)(struct ast_channel *chan, int subclass, const char *data, int len);

	/*! \brief Handle an exception, reading a frame */
	struct ast_frame * (* const exception)(struct ast_channel *chan);

	/*! \brief Bridge two channels of the same type together */
	enum ast_bridge_result (* const bridge)(struct ast_channel *c0, struct ast_channel *c1, int flags,
						struct ast_frame **fo, struct ast_channel **rc, int timeoutms);

	/*! \brief Indicate a particular condition (e.g. AST_CONTROL_BUSY or AST_CONTROL_RINGING or AST_CONTROL_CONGESTION */
	int (* const indicate)(struct ast_channel *c, int condition, const void *data, size_t datalen);

	/*! \brief Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links */
	int (* const fixup)(struct ast_channel *oldchan, struct ast_channel *newchan);

	/*! \brief Set a given option */
	int (* const setoption)(struct ast_channel *chan, int option, void *data, int datalen);

	/*! \brief Query a given option */
	int (* const queryoption)(struct ast_channel *chan, int option, void *data, int *datalen);

	/*! \brief Blind transfer other side (see app_transfer.c and ast_transfer() */
	int (* const transfer)(struct ast_channel *chan, const char *newdest);

	/*! \brief Write a frame, in standard format */
	int (* const write_video)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Find bridged channel */
	struct ast_channel *(* const bridged_channel)(struct ast_channel *chan, struct ast_channel *bridge);

	/*! \brief Provide additional read items for CHANNEL() dialplan function */
	int (* func_channel_read)(struct ast_channel *chan, char *function, char *data, char *buf, size_t len);

	/*! \brief Provide additional write items for CHANNEL() dialplan function */
	int (* func_channel_write)(struct ast_channel *chan, char *function, char *data, const char *value);

	/*! \brief Retrieve base channel (agent and local) */
	struct ast_channel* (* get_base_channel)(struct ast_channel *chan);
	
	/*! \brief Set base channel (agent and local) */
	int (* set_base_channel)(struct ast_channel *chan, struct ast_channel *base);
};

#define	DEBUGCHAN_FLAG  0x80000000
#define	FRAMECOUNT_INC(x)	( ((x) & DEBUGCHAN_FLAG) | (((x)+1) & ~DEBUGCHAN_FLAG) )

enum ast_channel_adsicpe {
	AST_ADSI_UNKNOWN,
	AST_ADSI_AVAILABLE,
	AST_ADSI_UNAVAILABLE,
	AST_ADSI_OFFHOOKONLY,
};

/*! 
 * \brief ast_channel states
 *
 * \note Bits 0-15 of state are reserved for the state (up/down) of the line
 *       Bits 16-32 of state are reserved for flags
 */
enum ast_channel_state {
	/*! Channel is down and available */
	AST_STATE_DOWN,
	/*! Channel is down, but reserved */
	AST_STATE_RESERVED,
	/*! Channel is off hook */
	AST_STATE_OFFHOOK,
	/*! Digits (or equivalent) have been dialed */
	AST_STATE_DIALING,
	/*! Line is ringing */
	AST_STATE_RING,
	/*! Remote end is ringing */
	AST_STATE_RINGING,
	/*! Line is up */
	AST_STATE_UP,
	/*! Line is busy */
	AST_STATE_BUSY,
	/*! Digits (or equivalent) have been dialed while offhook */
	AST_STATE_DIALING_OFFHOOK,
	/*! Channel has detected an incoming call and is waiting for ring */
	AST_STATE_PRERING,

	/*! Do not transmit voice data */
	AST_STATE_MUTE = (1 << 16),
};

/*! \brief Main Channel structure associated with a channel. 
 * This is the side of it mostly used by the pbx and call management.
 *
 * \note XXX It is important to remember to increment .cleancount each time
 *       this structure is changed. XXX
 */
struct ast_channel {
	/*! \brief Technology (point to channel driver) */
	const struct ast_channel_tech *tech;

	/*! \brief Private data used by the technology driver */
	void *tech_pvt;

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);			/*!< ASCII unique channel name */
		AST_STRING_FIELD(language);		/*!< Language requested for voice prompts */
		AST_STRING_FIELD(musicclass);		/*!< Default music class */
		AST_STRING_FIELD(accountcode);		/*!< Account code for billing */
		AST_STRING_FIELD(call_forward);		/*!< Where to forward to if asked to dial on this interface */
		AST_STRING_FIELD(uniqueid);		/*!< Unique Channel Identifier */
	);
	
	/*! \brief File descriptor for channel -- Drivers will poll on these file descriptors, so at least one must be non -1.  */
	int fds[AST_MAX_FDS];			

	void *music_state;				/*!< Music State*/
	void *generatordata;				/*!< Current generator data if there is any */
	struct ast_generator *generator;		/*!< Current active data generator */

	/*! \brief Who are we bridged to, if we're bridged. Who is proxying for us,
	  if we are proxied (i.e. chan_agent).
	  Do not access directly, use ast_bridged_channel(chan) */
	struct ast_channel *_bridge;
	struct ast_channel *masq;			/*!< Channel that will masquerade as us */
	struct ast_channel *masqr;			/*!< Who we are masquerading as */
	int cdrflags;					/*!< Call Detail Record Flags */

	/*! \brief Whether or not we have been hung up...  Do not set this value
	    directly, use ast_softhangup */
	int _softhangup;
	time_t	whentohangup;				/*!< Non-zero, set to actual time when channel is to be hung up */
	pthread_t blocker;				/*!< If anyone is blocking, this is them */
	ast_mutex_t lock;				/*!< Lock, can be used to lock a channel for some operations */
	const char *blockproc;				/*!< Procedure causing blocking */

	const char *appl;				/*!< Current application */
	const char *data;				/*!< Data passed to current application */
	int fdno;					/*!< Which fd had an event detected on */
	struct sched_context *sched;			/*!< Schedule context */
	int streamid;					/*!< For streaming playback, the schedule ID */
	struct ast_filestream *stream;			/*!< Stream itself. */
	int vstreamid;					/*!< For streaming video playback, the schedule ID */
	struct ast_filestream *vstream;			/*!< Video Stream itself. */
	int oldwriteformat;				/*!< Original writer format */
	
	int timingfd;					/*!< Timing fd */
	int (*timingfunc)(const void *data);
	void *timingdata;

	enum ast_channel_state _state;			/*!< State of line -- Don't write directly, use ast_setstate */
	int rings;					/*!< Number of rings so far */
	struct ast_callerid cid;			/*!< Caller ID, name, presentation etc */
	char unused_old_dtmfq[AST_MAX_EXTENSION];	/*!< The DTMFQ is deprecated.  All frames should go to the readq. */
	struct ast_frame dtmff;				/*!< DTMF frame */

	char context[AST_MAX_CONTEXT];			/*!< Dialplan: Current extension context */
	char exten[AST_MAX_EXTENSION];			/*!< Dialplan: Current extension number */
	int priority;					/*!< Dialplan: Current extension priority */
	char macrocontext[AST_MAX_CONTEXT];		/*!< Macro: Current non-macro context. See app_macro.c */
	char macroexten[AST_MAX_EXTENSION];		/*!< Macro: Current non-macro extension. See app_macro.c */
	int macropriority;				/*!< Macro: Current non-macro priority. See app_macro.c */
	char dialcontext[AST_MAX_CONTEXT];              /*!< Dial: Extension context that we were called from */

	struct ast_pbx *pbx;				/*!< PBX private structure for this channel */
	int amaflags;					/*!< Set BEFORE PBX is started to determine AMA flags */
	struct ast_cdr *cdr;				/*!< Call Detail Record */
	enum ast_channel_adsicpe adsicpe;		/*!< Whether or not ADSI is detected on CPE */

	struct tone_zone *zone;				/*!< Tone zone as set in indications.conf or
								in the CHANNEL dialplan function */

	struct ast_channel_monitor *monitor;		/*!< Channel monitoring */

	/*! Track the read/written samples for monitor use */
	unsigned long insmpl;
	unsigned long outsmpl;

	/* Frames in/out counters. The high bit is a debug mask, so
	 * the counter is only in the remaining bits
	 */
	unsigned int fin;
	unsigned int fout;
	int hangupcause;				/*!< Why is the channel hanged up. See causes.h */
	struct varshead varshead;			/*!< A linked list for channel variables */
	ast_group_t callgroup;				/*!< Call group for call pickups */
	ast_group_t pickupgroup;			/*!< Pickup group - which calls groups can be picked up? */
	unsigned int flags;				/*!< channel flags of AST_FLAG_ type */
	unsigned short transfercapability;		/*!< ISDN Transfer Capbility - AST_FLAG_DIGITAL is not enough */
	AST_LIST_HEAD_NOLOCK(, ast_frame) readq;
	int alertpipe[2];

	int nativeformats;				/*!< Kinds of data this channel can natively handle */
	int readformat;					/*!< Requested read format */
	int writeformat;				/*!< Requested write format */
	struct ast_trans_pvt *writetrans;		/*!< Write translation path */
	struct ast_trans_pvt *readtrans;		/*!< Read translation path */
	int rawreadformat;				/*!< Raw read format */
	int rawwriteformat;				/*!< Raw write format */

	struct ast_audiohook_list *audiohooks;
	void *unused; /*! This pointer should stay for Asterisk 1.4.  It just keeps the struct size the same
			 *  for the sake of ABI compatability. */

	AST_LIST_ENTRY(ast_channel) chan_list;		/*!< For easy linking */
	
	struct ast_jb jb;				/*!< The jitterbuffer state  */

	char emulate_dtmf_digit;			/*!< Digit being emulated */
	unsigned int emulate_dtmf_duration;	/*!< Number of ms left to emulate DTMF for */
	struct timeval dtmf_tv;       /*!< The time that an in process digit began, or the last digit ended */

	int visible_indication;                         /*!< Indication currently playing on the channel */

	/*! \brief Data stores on the channel */
	AST_LIST_HEAD_NOLOCK(datastores, ast_datastore) datastores;
};

/*! \brief ast_channel_tech Properties */
enum {
	/*! \brief Channels have this property if they can accept input with jitter; 
	 *         i.e. most VoIP channels */
	AST_CHAN_TP_WANTSJITTER = (1 << 0),
	/*! \brief Channels have this property if they can create jitter; 
	 *         i.e. most VoIP channels */
	AST_CHAN_TP_CREATESJITTER = (1 << 1),
};

/*! \brief ast_channel flags */
enum {
	/*! Queue incoming dtmf, to be released when this flag is turned off */
	AST_FLAG_DEFER_DTMF =    (1 << 1),
	/*! write should be interrupt generator */
	AST_FLAG_WRITE_INT =     (1 << 2),
	/*! a thread is blocking on this channel */
	AST_FLAG_BLOCKING =      (1 << 3),
	/*! This is a zombie channel */
	AST_FLAG_ZOMBIE =        (1 << 4),
	/*! There is an exception pending */
	AST_FLAG_EXCEPTION =     (1 << 5),
	/*! Listening to moh XXX anthm promises me this will disappear XXX */
	AST_FLAG_MOH =           (1 << 6),
	/*! This channel is spying on another channel */
	AST_FLAG_SPYING =        (1 << 7),
	/*! This channel is in a native bridge */
	AST_FLAG_NBRIDGE =       (1 << 8),
	/*! the channel is in an auto-incrementing dialplan processor,
	 *  so when ->priority is set, it will get incremented before
	 *  finding the next priority to run */
	AST_FLAG_IN_AUTOLOOP =   (1 << 9),
	/*! This is an outgoing call */
	AST_FLAG_OUTGOING =      (1 << 10),
	/*! This channel is being whispered on */
	AST_FLAG_WHISPER =       (1 << 11),
	/*! A DTMF_BEGIN frame has been read from this channel, but not yet an END */
	AST_FLAG_IN_DTMF =       (1 << 12),
	/*! A DTMF_END was received when not IN_DTMF, so the length of the digit is 
	 *  currently being emulated */
	AST_FLAG_EMULATE_DTMF =  (1 << 13),
	/*! This is set to tell the channel not to generate DTMF begin frames, and
	 *  to instead only generate END frames. */
	AST_FLAG_END_DTMF_ONLY = (1 << 14),
	/*! This flag indicates that on a masquerade, an active stream should not
	 *  be carried over */
	AST_FLAG_MASQ_NOSTREAM = (1 << 15),
	/*! This flag indicates that the hangup exten was run when the bridge terminated,
	 *  a message aimed at preventing a subsequent hangup exten being run at the pbx_run
	 *  level */
	AST_FLAG_BRIDGE_HANGUP_RUN = (1 << 16),
	/*! This flag indicates that the hangup exten should NOT be run when the 
	 *  bridge terminates, this will allow the hangup in the pbx loop to be run instead.
	 *  */
	AST_FLAG_BRIDGE_HANGUP_DONT = (1 << 17),
};

/*! \brief ast_bridge_config flags */
enum {
	AST_FEATURE_PLAY_WARNING = (1 << 0),
	AST_FEATURE_REDIRECT =     (1 << 1),
	AST_FEATURE_DISCONNECT =   (1 << 2),
	AST_FEATURE_ATXFER =       (1 << 3),
	AST_FEATURE_AUTOMON =      (1 << 4),
	AST_FEATURE_PARKCALL =     (1 << 5),
	AST_FEATURE_NO_H_EXTEN =   (1 << 6),
	AST_FEATURE_WARNING_ACTIVE = (1 << 7),
};

struct ast_bridge_config {
	struct ast_flags features_caller;
	struct ast_flags features_callee;
	struct timeval start_time;
	struct timeval nexteventts;
	struct timeval partialfeature_timer;
	long feature_timer;
	long timelimit;
	long play_warning;
	long warning_freq;
	const char *warning_sound;
	const char *end_sound;
	const char *start_sound;
	int firstpass;
	unsigned int flags;
	void (* end_bridge_callback)(void *);   /*!< A callback that is called after a bridge attempt */
	void *end_bridge_callback_data;         /*!< Data passed to the callback */
	/*! If the end_bridge_callback_data refers to a channel which no longer is going to
	 * exist when the end_bridge_callback is called, then it needs to be fixed up properly
	 */
	void (*end_bridge_callback_data_fixup)(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator);
};

struct chanmon;

#define LOAD_OH(oh) {	\
	oh.context = context; \
	oh.exten = exten; \
	oh.priority = priority; \
	oh.cid_num = cid_num; \
	oh.cid_name = cid_name; \
	oh.account = account; \
	oh.vars = vars; \
	oh.parent_channel = NULL; \
} 

struct outgoing_helper {
	const char *context;
	const char *exten;
	int priority;
	const char *cid_num;
	const char *cid_name;
	const char *account;
	struct ast_variable *vars;
	struct ast_channel *parent_channel;
};

enum {
	AST_CDR_TRANSFER =   (1 << 0),
	AST_CDR_FORWARD =    (1 << 1),
	AST_CDR_CALLWAIT =   (1 << 2),
	AST_CDR_CONFERENCE = (1 << 3),
};

enum {
	/*! Soft hangup by device */
	AST_SOFTHANGUP_DEV =       (1 << 0),
	/*! Soft hangup for async goto */
	AST_SOFTHANGUP_ASYNCGOTO = (1 << 1),
	AST_SOFTHANGUP_SHUTDOWN =  (1 << 2),
	AST_SOFTHANGUP_TIMEOUT =   (1 << 3),
	AST_SOFTHANGUP_APPUNLOAD = (1 << 4),
	AST_SOFTHANGUP_EXPLICIT =  (1 << 5),
	AST_SOFTHANGUP_UNBRIDGE =  (1 << 6),
};


/*! \brief Channel reload reasons for manager events at load or reload of configuration */
enum channelreloadreason {
	CHANNEL_MODULE_LOAD,
	CHANNEL_MODULE_RELOAD,
	CHANNEL_CLI_RELOAD,
	CHANNEL_MANAGER_RELOAD,
};

/*! \brief Create a channel datastore structure */
struct ast_datastore *ast_channel_datastore_alloc(const struct ast_datastore_info *info, char *uid);

/*! \brief Free a channel datastore structure */
int ast_channel_datastore_free(struct ast_datastore *datastore);

/*! \brief Inherit datastores from a parent to a child. */
int ast_channel_datastore_inherit(struct ast_channel *from, struct ast_channel *to);

/*! \brief Add a datastore to a channel */
int ast_channel_datastore_add(struct ast_channel *chan, struct ast_datastore *datastore);

/*! \brief Remove a datastore from a channel */
int ast_channel_datastore_remove(struct ast_channel *chan, struct ast_datastore *datastore);

/*! \brief Find a datastore on a channel */
struct ast_datastore *ast_channel_datastore_find(struct ast_channel *chan, const struct ast_datastore_info *info, char *uid);

/*! \brief Change the state of a channel */
int ast_setstate(struct ast_channel *chan, enum ast_channel_state);

/*! \brief Create a channel structure 
    \return Returns NULL on failure to allocate.
	\note New channels are 
	by default set to the "default" context and
	extension "s"
 */
struct ast_channel *ast_channel_alloc(int needqueue, int state, const char *cid_num, const char *cid_name, const char *acctcode, const char *exten, const char *context, const int amaflag, const char *name_fmt, ...) __attribute__((format(printf, 9, 10)));

/*!
 * \brief Queue one or more frames to a channel's frame queue
 *
 * \param chan the channel to queue the frame(s) on
 * \param f the frame(s) to queue.  Note that the frame(s) will be duplicated
 *        by this function.  It is the responsibility of the caller to handle
 *        freeing the memory associated with the frame(s) being passed if
 *        necessary.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_queue_frame(struct ast_channel *chan, struct ast_frame *f);

/*!
 * \brief Queue one or more frames to the head of a channel's frame queue
 *
 * \param chan the channel to queue the frame(s) on
 * \param f the frame(s) to queue.  Note that the frame(s) will be duplicated
 *        by this function.  It is the responsibility of the caller to handle
 *        freeing the memory associated with the frame(s) being passed if
 *        necessary.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_queue_frame_head(struct ast_channel *chan, struct ast_frame *f);

/*! \brief Queue a hangup frame */
int ast_queue_hangup(struct ast_channel *chan);

/*!
  \brief Queue a control frame with payload
  \param chan channel to queue frame onto
  \param control type of control frame
  \return zero on success, non-zero on failure
*/
int ast_queue_control(struct ast_channel *chan, enum ast_control_frame_type control);

/*!
  \brief Queue a control frame with payload
  \param chan channel to queue frame onto
  \param control type of control frame
  \param data pointer to payload data to be included in frame
  \param datalen number of bytes of payload data
  \return zero on success, non-zero on failure

  The supplied payload data is copied into the frame, so the caller's copy
  is not modified nor freed, and the resulting frame will retain a copy of
  the data even if the caller frees their local copy.

  \note This method should be treated as a 'network transport'; in other
  words, your frames may be transferred across an IAX2 channel to another
  system, which may be a different endianness than yours. Because of this,
  you should ensure that either your frames will never be expected to work
  across systems, or that you always put your payload data into 'network byte
  order' before calling this function.
*/
int ast_queue_control_data(struct ast_channel *chan, enum ast_control_frame_type control,
			   const void *data, size_t datalen);

/*! \brief Change channel name */
void ast_change_name(struct ast_channel *chan, char *newname);

/*! \brief Free a channel structure */
void  ast_channel_free(struct ast_channel *);

/*! \brief Requests a channel 
 * \param type type of channel to request
 * \param format requested channel format (codec)
 * \param data data to pass to the channel requester
 * \param status status
 * Request a channel of a given type, with data as optional information used 
 * by the low level module
 * \return Returns an ast_channel on success, NULL on failure.
 */
struct ast_channel *ast_request(const char *type, int format, void *data, int *status);

/*!
 * \brief Request a channel of a given type, with data as optional information used 
 * by the low level module and attempt to place a call on it
 * \param type type of channel to request
 * \param format requested channel format
 * \param data data to pass to the channel requester
 * \param timeout maximum amount of time to wait for an answer
 * \param reason why unsuccessful (if unsuceessful)
 * \param cidnum Caller-ID Number
 * \param cidname Caller-ID Name
 * \return Returns an ast_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct ast_channel *ast_request_and_dial(const char *type, int format, void *data, int timeout, int *reason, const char *cidnum, const char *cidname);

struct ast_channel *__ast_request_and_dial(const char *type, int format, void *data, int timeout, int *reason, const char *cidnum, const char *cidname, struct outgoing_helper *oh);

/*!
* \brief Forwards a call to a new channel specified by the original channel's call_forward str.  If possible, the new forwarded channel is created and returned while the original one is terminated.
* \param caller in channel that requested orig
* \param orig channel being replaced by the call forward channel
* \param timeout maximum amount of time to wait for setup of new forward channel
* \param format requested channel format
* \param oh outgoing helper used with original channel
* \param outstate reason why unsuccessful (if uncuccessful)
* \return Returns the forwarded call's ast_channel on success or NULL on failure
*/
struct ast_channel *ast_call_forward(struct ast_channel *caller, struct ast_channel *orig, int *timeout, int format, struct outgoing_helper *oh, int *outstate);

/*!\brief Register a channel technology (a new channel driver)
 * Called by a channel module to register the kind of channels it supports.
 * \param tech Structure defining channel technology or "type"
 * \return Returns 0 on success, -1 on failure.
 */
int ast_channel_register(const struct ast_channel_tech *tech);

/*! \brief Unregister a channel technology 
 * \param tech Structure defining channel technology or "type" that was previously registered
 * \return No return value.
 */
void ast_channel_unregister(const struct ast_channel_tech *tech);

/*! \brief Get a channel technology structure by name
 * \param name name of technology to find
 * \return a pointer to the structure, or NULL if no matching technology found
 */
const struct ast_channel_tech *ast_get_channel_tech(const char *name);

/*! \brief Hang up a channel  
 * \note This function performs a hard hangup on a channel.  Unlike the soft-hangup, this function
 * performs all stream stopping, etc, on the channel that needs to end.
 * chan is no longer valid after this call.
 * \param chan channel to hang up
 * \return Returns 0 on success, -1 on failure.
 */
int ast_hangup(struct ast_channel *chan);

/*! \brief Softly hangup up a channel 
 * \param chan channel to be soft-hung-up
 * Call the protocol layer, but don't destroy the channel structure (use this if you are trying to
 * safely hangup a channel managed by another thread.
 * \param cause	Ast hangupcause for hangup
 * \return Returns 0 regardless
 */
int ast_softhangup(struct ast_channel *chan, int cause);

/*! \brief Softly hangup up a channel (no channel lock) 
 * \param chan channel to be soft-hung-up
 * \param cause	Ast hangupcause for hangup (see cause.h) */
int ast_softhangup_nolock(struct ast_channel *chan, int cause);

/*! \brief Check to see if a channel is needing hang up 
 * \param chan channel on which to check for hang up
 * This function determines if the channel is being requested to be hung up.
 * \return Returns 0 if not, or 1 if hang up is requested (including time-out).
 */
int ast_check_hangup(struct ast_channel *chan);

/*! \brief Compare a offset with the settings of when to hang a channel up 
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time
 * \return 1, 0, or -1
 * This function compares a offset from current time with the absolute time 
 * out on a channel (when to hang up). If the absolute time out on a channel
 * is earlier than current time plus the offset, it returns 1, if the two
 * time values are equal, it return 0, otherwise, it retturn -1.
 */
int ast_channel_cmpwhentohangup(struct ast_channel *chan, time_t offset);

/*! \brief Set when to hang a channel up 
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time of when to hang up
 * This function sets the absolute time out on a channel (when to hang up).
 */
void ast_channel_setwhentohangup(struct ast_channel *chan, time_t offset);

/*! \brief Answer a ringing call 
 * \param chan channel to answer
 * This function answers a channel and handles all necessary call
 * setup functions.
 * \return Returns 0 on success, -1 on failure
 */
int ast_answer(struct ast_channel *chan);

/*! \brief Make a call 
 * \param chan which channel to make the call on
 * \param addr destination of the call
 * \param timeout time to wait on for connect
 * Place a call, take no longer than timeout ms. 
   \return Returns -1 on failure, 0 on not enough time 
   (does not automatically stop ringing), and  
   the number of seconds the connect took otherwise.
   */
int ast_call(struct ast_channel *chan, char *addr, int timeout);

/*! \brief Indicates condition of channel 
 * \note Indicate a condition such as AST_CONTROL_BUSY, AST_CONTROL_RINGING, or AST_CONTROL_CONGESTION on a channel
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * \return Returns 0 on success, -1 on failure
 */
int ast_indicate(struct ast_channel *chan, int condition);

/*! \brief Indicates condition of channel, with payload
 * \note Indicate a condition such as AST_CONTROL_BUSY, AST_CONTROL_RINGING, or AST_CONTROL_CONGESTION on a channel
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * \param data pointer to payload data
 * \param datalen size of payload data
 * \return Returns 0 on success, -1 on failure
 */
int ast_indicate_data(struct ast_channel *chan, int condition, const void *data, size_t datalen);

/* Misc stuff ------------------------------------------------ */

/*! \brief Wait for input on a channel 
 * \param chan channel to wait on
 * \param ms length of time to wait on the channel
 * Wait for input on a channel for a given # of milliseconds (<0 for indefinite). 
  \return Returns < 0 on  failure, 0 if nothing ever arrived, and the # of ms remaining otherwise */
int ast_waitfor(struct ast_channel *chan, int ms);

/*! \brief Wait for a specied amount of time, looking for hangups 
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
 * Waits for a specified amount of time, servicing the channel as required.
 * \return returns -1 on hangup, otherwise 0.
 */
int ast_safe_sleep(struct ast_channel *chan, int ms);

/*! \brief Wait for a specied amount of time, looking for hangups and a condition argument 
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
 * \param cond a function pointer for testing continue condition
 * \param data argument to be passed to the condition test function
 * \return returns -1 on hangup, otherwise 0.
 * Waits for a specified amount of time, servicing the channel as required. If cond
 * returns 0, this function returns.
 */
int ast_safe_sleep_conditional(struct ast_channel *chan, int ms, int (*cond)(void*), void *data );

/*! \brief Waits for activity on a group of channels 
 * \param chan an array of pointers to channels
 * \param n number of channels that are to be waited upon
 * \param fds an array of fds to wait upon
 * \param nfds the number of fds to wait upon
 * \param exception exception flag
 * \param outfd fd that had activity on it
 * \param ms how long the wait was
 * Big momma function here.  Wait for activity on any of the n channels, or any of the nfds
   file descriptors.
   \return Returns the channel with activity, or NULL on error or if an FD
   came first.  If the FD came first, it will be returned in outfd, otherwise, outfd
   will be -1 */
struct ast_channel *ast_waitfor_nandfds(struct ast_channel **chan, int n, int *fds, int nfds, int *exception, int *outfd, int *ms);

/*! \brief Waits for input on a group of channels
   Wait for input on an array of channels for a given # of milliseconds. 
	\return Return channel with activity, or NULL if none has activity.  
	\param chan an array of pointers to channels
	\param n number of channels that are to be waited upon
	\param ms time "ms" is modified in-place, if applicable */
struct ast_channel *ast_waitfor_n(struct ast_channel **chan, int n, int *ms);

/*! \brief Waits for input on an fd
	This version works on fd's only.  Be careful with it. */
int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception);


/*! \brief Reads a frame
 * \param chan channel to read a frame from
	Read a frame.  
	\return Returns a frame, or NULL on error.  If it returns NULL, you
		best just stop reading frames and assume the channel has been
		disconnected. */
struct ast_frame *ast_read(struct ast_channel *chan);

/*! \brief Reads a frame, returning AST_FRAME_NULL frame if audio. 
 * Read a frame. 
 	\param chan channel to read a frame from
	\return  Returns a frame, or NULL on error.  If it returns NULL, you
		best just stop reading frames and assume the channel has been
		disconnected.  
	\note Audio is replaced with AST_FRAME_NULL to avoid 
	transcode when the resulting audio is not necessary. */
struct ast_frame *ast_read_noaudio(struct ast_channel *chan);

/*! \brief Write a frame to a channel 
 * This function writes the given frame to the indicated channel.
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * \return It returns 0 on success, -1 on failure.
 */
int ast_write(struct ast_channel *chan, struct ast_frame *frame);

/*! \brief Write video frame to a channel 
 * This function writes the given frame to the indicated channel.
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * \return It returns 1 on success, 0 if not implemented, and -1 on failure.
 */
int ast_write_video(struct ast_channel *chan, struct ast_frame *frame);

/*! \brief Send empty audio to prime a channel driver */
int ast_prod(struct ast_channel *chan);

/*! \brief Sets read format on channel chan
 * Set read format for channel to whichever component of "format" is best. 
 * \param chan channel to change
 * \param format format to change to
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_read_format(struct ast_channel *chan, int format);

/*! \brief Sets write format on channel chan
 * Set write format for channel to whichever compoent of "format" is best. 
 * \param chan channel to change
 * \param format new format for writing
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_write_format(struct ast_channel *chan, int format);

/*! \brief Sends text to a channel 
 * Write text to a display on a channel
 * \param chan channel to act upon
 * \param text string of text to send on the channel
 * \return Returns 0 on success, -1 on failure
 */
int ast_sendtext(struct ast_channel *chan, const char *text);

/*! \brief Receives a text character from a channel
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * Read a char of text from a channel
 * Returns 0 on success, -1 on failure
 */
int ast_recvchar(struct ast_channel *chan, int timeout);

/*! \brief Send a DTMF digit to a channel
 * Send a DTMF digit to a channel.
 * \param chan channel to act upon
 * \param digit the DTMF digit to send, encoded in ASCII
 * \return Returns 0 on success, -1 on failure
 */
int ast_senddigit(struct ast_channel *chan, char digit);

int ast_senddigit_begin(struct ast_channel *chan, char digit);
int ast_senddigit_end(struct ast_channel *chan, char digit, unsigned int duration);

/*! \brief Receives a text string from a channel
 * Read a string of text from a channel
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * \return the received text, or NULL to signify failure.
 */
char *ast_recvtext(struct ast_channel *chan, int timeout);

/*! \brief Browse channels in use
 * Browse the channels currently in use 
 * \param prev where you want to start in the channel list
 * \return Returns the next channel in the list, NULL on end.
 * 	If it returns a channel, that channel *has been locked*!
 */
struct ast_channel *ast_channel_walk_locked(const struct ast_channel *prev);

/*! \brief Get channel by name (locks channel) */
struct ast_channel *ast_get_channel_by_name_locked(const char *chan);

/*! \brief Get channel by name prefix (locks channel) */
struct ast_channel *ast_get_channel_by_name_prefix_locked(const char *name, const int namelen);

/*! \brief Get channel by name prefix (locks channel) */
struct ast_channel *ast_walk_channel_by_name_prefix_locked(const struct ast_channel *chan, const char *name, const int namelen);

/*! \brief Get channel by exten (and optionally context) and lock it */
struct ast_channel *ast_get_channel_by_exten_locked(const char *exten, const char *context);

/*! \brief Get next channel by exten (and optionally context) and lock it */
struct ast_channel *ast_walk_channel_by_exten_locked(const struct ast_channel *chan, const char *exten,
						     const char *context);

/*! ! \brief Waits for a digit
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait
 * \return Returns <0 on error, 0 on no entry, and the digit on success. */
int ast_waitfordigit(struct ast_channel *c, int ms);

/*! \brief Wait for a digit
 Same as ast_waitfordigit() with audio fd for outputting read audio and ctrlfd to monitor for reading. 
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait
 * \param audiofd audio file descriptor to write to if audio frames are received
 * \param ctrlfd control file descriptor to monitor for reading
 * \return Returns 1 if ctrlfd becomes available */
int ast_waitfordigit_full(struct ast_channel *c, int ms, int audiofd, int ctrlfd);

/*! Reads multiple digits 
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

/*! \brief Report DTMF on channel 0 */
#define AST_BRIDGE_DTMF_CHANNEL_0		(1 << 0)		
/*! \brief Report DTMF on channel 1 */
#define AST_BRIDGE_DTMF_CHANNEL_1		(1 << 1)		
/*! \brief Return all voice frames on channel 0 */
#define AST_BRIDGE_REC_CHANNEL_0		(1 << 2)		
/*! \brief Return all voice frames on channel 1 */
#define AST_BRIDGE_REC_CHANNEL_1		(1 << 3)		
/*! \brief Ignore all signal frames except NULL */
#define AST_BRIDGE_IGNORE_SIGS			(1 << 4)		


/*! \brief Makes two channel formats compatible 
 * \param c0 first channel to make compatible
 * \param c1 other channel to make compatible
 * Set two channels to compatible formats -- call before ast_channel_bridge in general .  
 * \return Returns 0 on success and -1 if it could not be done */
int ast_channel_make_compatible(struct ast_channel *c0, struct ast_channel *c1);

/*! Bridge two channels together 
 * \param c0 first channel to bridge
 * \param c1 second channel to bridge
 * \param config config for the channels
 * \param fo destination frame(?)
 * \param rc destination channel(?)
 * Bridge two channels (c0 and c1) together.  If an important frame occurs, we return that frame in
   *rf (remember, it could be NULL) and which channel (0 or 1) in rc */
/* int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc); */
int ast_channel_bridge(struct ast_channel *c0,struct ast_channel *c1,struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc);

/*! \brief Weird function made for call transfers
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
const char *ast_cause2str(int state) attribute_pure;

/*! Convert the string form of a cause code to a number */
/*! 
 * \param name string form of the cause
 * Returns the cause code
 */
int ast_str2cause(const char *name) attribute_pure;

/*! Gives the string form of a given channel state */
/*! 
 * \param ast_channel_state state to get the name of
 * Give a name to a state 
 * Returns the text form of the binary state given
 */
char *ast_state2str(enum ast_channel_state);

/*! Gives the string form of a given transfer capability */
/*!
 * \param transfercapability transfercapabilty to get the name of
 * Give a name to a transfercapbility
 * See above
 * Returns the text form of the binary transfer capbility
 */
char *ast_transfercapability2str(int transfercapability) attribute_const;

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
int ast_best_codec(int fmts);


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

/*!
 * \note The channel does not need to be locked before calling this function.
 */
void ast_set_callerid(struct ast_channel *chan, const char *cidnum, const char *cidname, const char *ani);


/*! return a mallocd string with the result of sprintf of the fmt and following args */
char __attribute__((format(printf, 1, 2))) *ast_safe_string_alloc(const char *fmt, ...);


/*! Start a tone going */
int ast_tonepair_start(struct ast_channel *chan, int freq1, int freq2, int duration, int vol);
/*! Stop a tone from playing */
void ast_tonepair_stop(struct ast_channel *chan);
/*! Play a tone pair for a given amount of time */
int ast_tonepair(struct ast_channel *chan, int freq1, int freq2, int duration, int vol);

/*!
 * \brief Automatically service a channel for us... 
 *
 * \retval 0 success
 * \retval -1 failure, or the channel is already being autoserviced
 */
int ast_autoservice_start(struct ast_channel *chan);

/*! 
 * \brief Stop servicing a channel for us...  
 *
 * \note if chan is locked prior to calling ast_autoservice_stop, it
 * is likely that there will be a deadlock between the thread that calls
 * ast_autoservice_stop and the autoservice thread. It is important
 * that chan is not locked prior to this call
 *
 * \retval 0 success
 * \retval -1 error, or the channel has been hungup 
 */
int ast_autoservice_stop(struct ast_channel *chan);

/* If built with DAHDI optimizations, force a scheduled expiration on the
   timer fd, at which point we call the callback function / data */
int ast_settimeout(struct ast_channel *c, int samples, int (*func)(const void *data), void *data);

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
  \brief An opaque 'object' structure use by silence generators on channels.
 */
struct ast_silence_generator;

/*!
  \brief Starts a silence generator on the given channel.
  \param chan The channel to generate silence on
  \return An ast_silence_generator pointer, or NULL if an error occurs

  This function will cause SLINEAR silence to be generated on the supplied
  channel until it is disabled; if the channel cannot be put into SLINEAR
  mode then the function will fail.

  The pointer returned by this function must be preserved and passed to
  ast_channel_stop_silence_generator when you wish to stop the silence
  generation.
 */
struct ast_silence_generator *ast_channel_start_silence_generator(struct ast_channel *chan);

/*!
  \brief Stops a previously-started silence generator on the given channel.
  \param chan The channel to operate on
  \param state The ast_silence_generator pointer return by a previous call to
  ast_channel_start_silence_generator.
  \return nothing

  This function will stop the operating silence generator and return the channel
  to its previous write format.
 */
void ast_channel_stop_silence_generator(struct ast_channel *chan, struct ast_silence_generator *state);

/*!
  \brief Check if the channel can run in internal timing mode.
  \param chan The channel to check
  \return boolean

  This function will return 1 if internal timing is enabled and the timing
  device is available.
 */
int ast_internal_timing_enabled(struct ast_channel *chan);

/* Misc. functions below */

/*! \brief if fd is a valid descriptor, set *pfd with the descriptor
 * \return Return 1 (not -1!) if added, 0 otherwise (so we can add the
 * return value to the index into the array)
 */
static inline int ast_add_fd(struct pollfd *pfd, int fd)
{
	pfd->fd = fd;
	pfd->events = POLLIN | POLLPRI;
	return fd >= 0;
}

/*! \brief Helper function for migrating select to poll */
static inline int ast_fdisset(struct pollfd *pfds, int fd, int max, int *start)
{
	int x;
	int dummy=0;

	if (fd < 0)
		return 0;
	if (!start)
		start = &dummy;
	for (x = *start; x<max; x++)
		if (pfds[x].fd == fd) {
			if (x == *start)
				(*start)++;
			return pfds[x].revents;
		}
	return 0;
}

#ifndef HAVE_TIMERSUB
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

/*! \brief Waits for activity on a group of channels 
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

#define CHECK_BLOCKING(c) do { 	 \
	if (ast_test_flag(c, AST_FLAG_BLOCKING)) {\
		if (option_debug) \
			ast_log(LOG_DEBUG, "Thread %ld Blocking '%s', already blocked by thread %ld in procedure %s\n", (long) pthread_self(), (c)->name, (long) (c)->blocker, (c)->blockproc); \
	} else { \
		(c)->blocker = pthread_self(); \
		(c)->blockproc = __PRETTY_FUNCTION__; \
		ast_set_flag(c, AST_FLAG_BLOCKING); \
	} } while (0)

ast_group_t ast_get_group(const char *s);

/*! \brief print call- and pickup groups into buffer */
char *ast_print_group(char *buf, int buflen, ast_group_t group);

/*! \brief Convert enum channelreloadreason to text string for manager event
	\param reason	Enum channelreloadreason - reason for reload (manager, cli, start etc)
*/
const char *channelreloadreason2txt(enum channelreloadreason reason);

/*! \brief return an ast_variable list of channeltypes */
struct ast_variable *ast_channeltype_list(void);

/*!
  \brief Begin 'whispering' onto a channel
  \param chan The channel to whisper onto
  \return 0 for success, non-zero for failure

  This function will add a whisper buffer onto a channel and set a flag
  causing writes to the channel to reduce the volume level of the written
  audio samples, and then to mix in audio from the whisper buffer if it
  is available.

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
int ast_channel_whisper_start(struct ast_channel *chan);

/*!
  \brief Feed an audio frame into the whisper buffer on a channel
  \param chan The channel to whisper onto
  \param f The frame to to whisper onto chan
  \return 0 for success, non-zero for failure
 */
int ast_channel_whisper_feed(struct ast_channel *chan, struct ast_frame *f);

/*!
  \brief Stop 'whispering' onto a channel
  \param chan The channel to whisper onto
  \return 0 for success, non-zero for failure

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
void ast_channel_whisper_stop(struct ast_channel *chan);



/*!
  \brief return an english explanation of the code returned thru __ast_request_and_dial's 'outstate' argument
  \param reason  The integer argument, usually taken from AST_CONTROL_ macros
  \return char pointer explaining the code
 */
char *ast_channel_reason2str(int reason);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CHANNEL_H */
