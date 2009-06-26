#ifndef _SIG_PRI_H
#define _SIG_PRI_H
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
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
 *
 * \brief Interface header for PRI signaling module
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 */

#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include <libpri.h>
#include <dahdi/user.h>

enum sig_pri_tone {
	SIG_PRI_TONE_RINGTONE = 0,
	SIG_PRI_TONE_STUTTER,
	SIG_PRI_TONE_CONGESTION,
	SIG_PRI_TONE_DIALTONE,
	SIG_PRI_TONE_DIALRECALL,
	SIG_PRI_TONE_INFO,
	SIG_PRI_TONE_BUSY,
};

enum sig_pri_law {
	SIG_PRI_DEFLAW = 0,
	SIG_PRI_ULAW,
	SIG_PRI_ALAW
};

struct sig_pri_pri;

struct sig_pri_callback {
	/* Unlock the private in the signalling private structure.  This is used for three way calling madness. */
	void (* const unlock_private)(void *pvt);
	/* Lock the private in the signalling private structure.  ... */
	void (* const lock_private)(void *pvt);
	/* Function which is called back to handle any other DTMF up events that are received.  Called by analog_handle_event.  Why is this
	 * important to use, instead of just directly using events received before they are passed into the library?  Because sometimes,
	 * (CWCID) the library absorbs DTMF events received. */
	//void (* const handle_dtmfup)(void *pvt, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest);

	//int (* const dial_digits)(void *pvt, enum analog_sub sub, struct analog_dialoperation *dop);
	int (* const play_tone)(void *pvt, enum sig_pri_tone tone);

	int (* const set_echocanceller)(void *pvt, int enable);
	int (* const train_echocanceller)(void *pvt);

	struct ast_channel * (* const new_ast_channel)(void *pvt, int state, int startpbx, enum sig_pri_law law, int transfercapability, char *exten, const struct ast_channel *chan);

	void (* const fixup_chans)(void *old_chan, void *new_chan);

	/* Note: Called with PRI lock held */
	void (* const handle_dchan_exception)(struct sig_pri_pri *pri, int index);
};

#define NUM_DCHANS		4	/*!< No more than 4 d-channels */
#define MAX_CHANNELS	672		/*!< No more than a DS3 per trunk group */

#define SIG_PRI		DAHDI_SIG_CLEAR
#define SIG_BRI		(0x2000000 | DAHDI_SIG_CLEAR)
#define SIG_BRI_PTMP	(0X4000000 | DAHDI_SIG_CLEAR)

/* Overlap dialing option types */
#define DAHDI_OVERLAPDIAL_NONE 0
#define DAHDI_OVERLAPDIAL_OUTGOING 1
#define DAHDI_OVERLAPDIAL_INCOMING 2
#define DAHDI_OVERLAPDIAL_BOTH (DAHDI_OVERLAPDIAL_INCOMING|DAHDI_OVERLAPDIAL_OUTGOING)

#ifdef HAVE_PRI_SERVICE_MESSAGES
/*! \brief Persistent Service State */
#define SRVST_DBKEY "service-state"
/*! \brief The out-of-service SERVICE state */
#define SRVST_TYPE_OOS "O"
/*! \brief SRVST_INITIALIZED is used to indicate a channel being out-of-service 
 *  The SRVST_INITIALIZED is mostly used maintain backwards compatibility but also may
 *  mean that the channel has not yet received a RESTART message.  If a channel is
 *  out-of-service with this reason a RESTART message will result in the channel
 *  being put into service. */
#define SRVST_INITIALIZED 0
/*! \brief SRVST_NEAREND is used to indicate that the near end was put out-of-service */
#define SRVST_NEAREND  (1 << 0)
/*! \brief SRVST_FAREND is used to indicate that the far end was taken out-of-service */
#define SRVST_FAREND   (1 << 1)
/*! \brief SRVST_BOTH is used to indicate that both sides of the channel are out-of-service */
#define SRVST_BOTH (SRVST_NEAREND | SRVST_FAREND)

/*! \brief The AstDB family */
static const char dahdi_db[] = "dahdi/registry";
#endif

struct sig_pri_chan {
	/* Options to be set by user */
	unsigned int hidecallerid:1;
	unsigned int hidecalleridname:1;      /*!< Hide just the name not the number for legacy PBX use */
	unsigned int immediate:1;			/*!< Answer before getting digits? */
	unsigned int inalarm:1;
	unsigned int priexclusive:1;			/*!< Whether or not to override and use exculsive mode for channel selection */
	unsigned int priindication_oob:1;
	unsigned int use_callerid:1;			/*!< Whether or not to use caller id on this channel */
	unsigned int use_callingpres:1;			/*!< Whether to use the callingpres the calling switch sends */
	char context[AST_MAX_CONTEXT];
	int channel;					/*!< Channel Number or CRV */
	char mohinterpret[MAX_MUSICCLASS];
	int stripmsd;

	/* Options to be  checked by user */
	int cid_ani2;						/*!< Automatic Number Identification number (Alternate PRI caller ID number) */
	char cid_num[AST_MAX_EXTENSION];
	int cid_ton;					/*!< Type Of Number (TON) */
	char cid_name[AST_MAX_EXTENSION];
	char cid_ani[AST_MAX_EXTENSION];
	char rdnis[AST_MAX_EXTENSION];
	char dnid[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	int callingpres;				/*!< The value of callling presentation that we're going to use when placing a PRI call */
	char lastcid_num[AST_MAX_EXTENSION];
	char lastcid_name[AST_MAX_EXTENSION];

	/* Internal variables -- Don't touch */
	/* Probably will need DS0 number, DS1 number, and a few other things */
	char dialdest[256];				/* Queued up digits for overlap dialing.  They will be sent out as information messages when setup ACK is received */
	int mastertrunkgroup;

	unsigned int alerting:1;		/*!< TRUE if channel is alerting/ringing */
	unsigned int alreadyhungup:1;	/*!< TRUE if the call has already gone/hungup */
	unsigned int isidlecall:1;		/*!< TRUE if this is an idle call */
	unsigned int proceeding:1;		/*!< TRUE if call is in a proceeding state */
	unsigned int progress:1;		/*!< TRUE if the call has seen progress through the network */
	unsigned int resetting:1;		/*!< TRUE if this channel is being reset/restarted */
	unsigned int setup_ack:1;		/*!< TRUE if this channel has received a SETUP_ACKNOWLEDGE */

	unsigned int outgoing:1;
	unsigned int digital:1;

	struct ast_channel *owner;

	struct sig_pri_pri *pri;		
	q931_call *call;				/*!< opaque libpri call control structure */

	int prioffset;					/*!< channel number in span */
	int logicalspan;				/*!< logical span number within trunk group */

	struct sig_pri_callback *calls;
	void *chan_pvt;
	ast_mutex_t service_lock;						/*!< Mutex for service messages */
};

struct sig_pri_pri {
	/* Should be set by user */
	int	pritimers[PRI_MAX_TIMERS];
	int overlapdial;								/*!< In overlap dialing mode */
	int qsigchannelmapping;                     	/*!< QSIG channel mapping type */
    int discardremoteholdretrieval;                 /*!< shall remote hold or remote retrieval notifications be discarded? */
	int facilityenable;								/*!< Enable facility IEs */
	int dchan_logical_span[NUM_DCHANS];				/*!< Logical offset the DCHAN sits in */
	int fds[NUM_DCHANS];							/*!< FD's for d-channels */
#ifdef HAVE_PRI_SERVICE_MESSAGES
	unsigned int enable_service_message_support:1;	/*!< enable SERVICE message support */
#endif
#ifdef HAVE_PRI_INBANDDISCONNECT
	unsigned int inbanddisconnect:1;				/*!< Should we support inband audio after receiving DISCONNECT? */
#endif
	int dialplan;							/*!< Dialing plan */
	int localdialplan;						/*!< Local dialing plan */
	char internationalprefix[10];			/*!< country access code ('00' for european dialplans) */
	char nationalprefix[10];				/*!< area access code ('0' for european dialplans) */
	char localprefix[20];					/*!< area access code + area code ('0'+area code for european dialplans) */
	char privateprefix[20];					/*!< for private dialplans */
	char unknownprefix[20];					/*!< for unknown dialplans */
	long resetinterval;						/*!< Interval (in seconds) for resetting unused channels */
	char idleext[AST_MAX_EXTENSION];		/*!< Where to idle extra calls */
	char idlecontext[AST_MAX_CONTEXT];		/*!< What context to use for idle */
	char idledial[AST_MAX_EXTENSION];		/*!< What to dial before dumping */
	int minunused;							/*!< Min # of channels to keep empty */
	int minidle;							/*!< Min # of "idling" calls to keep active */
	int nodetype;							/*!< Node type */
	int switchtype;							/*!< Type of switch to emulate */
	int nsf;								/*!< Network-Specific Facilities */
	int trunkgroup;							/*!< What our trunkgroup is */

	int dchanavail[NUM_DCHANS];				/*!< Whether each channel is available */
	int debug;								/*!< set to true if to dump PRI event info (tested but never set) */
	int span;                               /*!< span number put into user output messages */
	int resetting;							/*!< true if span is being reset/restarted */
	int resetpos;							/*!< current position during a reset (-1 if not started) */
	int sig;								/*!< ISDN signalling type (SIG_PRI, SIG_BRI, SIG_BRI_PTMP, etc...) */

	/* Everything after here is internally set */
	struct pri *dchans[NUM_DCHANS];				/*!< Actual d-channels */
	struct pri *pri;							/*!< Currently active D-channel */
	int numchans;								/*!< Num of channels we represent */
	struct sig_pri_chan *pvts[MAX_CHANNELS];	/*!< Member channel pvt structs */
	pthread_t master;							/*!< Thread of master */
	ast_mutex_t lock;							/*!< Mutex */
	time_t lastreset;							/*!< time when unused channels were last reset */
	struct sig_pri_callback *calls;
};

int sig_pri_call(struct sig_pri_chan *p, struct ast_channel *ast, char *rdest, int timeout, int layer1);

int sig_pri_hangup(struct sig_pri_chan *p, struct ast_channel *ast);

int sig_pri_indicate(struct sig_pri_chan *p, struct ast_channel *chan, int condition, const void *data, size_t datalen);

int sig_pri_answer(struct sig_pri_chan *p, struct ast_channel *ast);

int sig_pri_available(struct sig_pri_chan *p, int channelmatch, ast_group_t groupmatch, int *busy, int *channelmatched, int *groupmatched);

void sig_pri_init_pri(struct sig_pri_pri *pri);

/* If return 0, it means this function was able to handle it (pre setup digits).  If non zero, the user of this
 * functions should handle it normally (generate inband DTMF) */
int sig_pri_digit_begin(struct sig_pri_chan *pvt, struct ast_channel *ast, char digit);

int sig_pri_start_pri(struct sig_pri_pri *pri);

void sig_pri_chan_alarm_notify(struct sig_pri_chan *p, int noalarm);

void pri_event_alarm(struct sig_pri_pri *pri, int index, int before_start_pri);

void pri_event_noalarm(struct sig_pri_pri *pri, int index, int before_start_pri);

struct ast_channel *sig_pri_request(struct sig_pri_chan *p, enum sig_pri_law law, const struct ast_channel *requestor);

struct sig_pri_chan *sig_pri_chan_new(void *pvt_data, struct sig_pri_callback *callback, struct sig_pri_pri *pri, int logicalspan, int channo);

int pri_is_up(struct sig_pri_pri *pri);

void sig_pri_cli_show_spans(int fd, int span, struct sig_pri_pri *pri);

void sig_pri_cli_show_span(int fd, int *dchannels, struct sig_pri_pri *pri);

int pri_send_keypad_facility_exec(struct sig_pri_chan *p, const char *digits);
int pri_send_callrerouting_facility_exec(struct sig_pri_chan *p, enum ast_channel_state chanstate, const char *destination, const char *original, const char *reason);

int pri_maintenance_bservice(struct pri *pri, struct sig_pri_chan *p, int changestatus);

#endif /* _SIG_PRI_H */
