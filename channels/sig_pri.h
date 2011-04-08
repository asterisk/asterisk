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
#include "asterisk/event.h"
#include "asterisk/ccss.h"
#include <libpri.h>
#include <dahdi/user.h>

#if defined(HAVE_PRI_CCSS)
/*! PRI debug message flags when normal PRI debugging is turned on at the command line. */
#define SIG_PRI_DEBUG_NORMAL	\
	(PRI_DEBUG_APDU | PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE | PRI_DEBUG_Q921_STATE \
	| PRI_DEBUG_CC)

/*! PRI debug message flags when intense PRI debugging is turned on at the command line. */
#define SIG_PRI_DEBUG_INTENSE	\
	(PRI_DEBUG_APDU | PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE | PRI_DEBUG_Q921_STATE \
	| PRI_DEBUG_CC | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_DUMP)

#else

/*! PRI debug message flags when normal PRI debugging is turned on at the command line. */
#define SIG_PRI_DEBUG_NORMAL	\
	(PRI_DEBUG_APDU | PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE | PRI_DEBUG_Q921_STATE)

/*! PRI debug message flags when intense PRI debugging is turned on at the command line. */
#define SIG_PRI_DEBUG_INTENSE	\
	(PRI_DEBUG_APDU | PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE | PRI_DEBUG_Q921_STATE \
	| PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_DUMP)
#endif	/* !defined(HAVE_PRI_CCSS) */

#if 0
/*! PRI debug message flags set on initial startup. */
#define SIG_PRI_DEBUG_DEFAULT	SIG_PRI_DEBUG_NORMAL
#else
/*! PRI debug message flags set on initial startup. */
#define SIG_PRI_DEBUG_DEFAULT	0
#endif

#define SIG_PRI_AOC_GRANT_S    (1 << 0)
#define SIG_PRI_AOC_GRANT_D    (1 << 1)
#define SIG_PRI_AOC_GRANT_E    (1 << 2)

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

enum sig_pri_moh_signaling {
	/*! Generate MOH to the remote party. */
	SIG_PRI_MOH_SIGNALING_MOH,
	/*! Send hold notification signaling to the remote party. */
	SIG_PRI_MOH_SIGNALING_NOTIFY,
#if defined(HAVE_PRI_CALL_HOLD)
	/*! Use HOLD/RETRIEVE signaling to release the B channel while on hold. */
	SIG_PRI_MOH_SIGNALING_HOLD,
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
};

enum sig_pri_moh_state {
	/*! Bridged peer has not put us on hold. */
	SIG_PRI_MOH_STATE_IDLE,
	/*! Bridged peer has put us on hold and we were to notify the remote party. */
	SIG_PRI_MOH_STATE_NOTIFY,
	/*! Bridged peer has put us on hold and we were to play MOH or HOLD/RETRIEVE fallback. */
	SIG_PRI_MOH_STATE_MOH,
#if defined(HAVE_PRI_CALL_HOLD)
	/*! Requesting to put channel on hold. */
	SIG_PRI_MOH_STATE_HOLD_REQ,
	/*! Trying to go on hold when bridged peer requested to unhold. */
	SIG_PRI_MOH_STATE_PEND_UNHOLD,
	/*! Channel is held. */
	SIG_PRI_MOH_STATE_HOLD,
	/*! Requesting to take channel out of hold. */
	SIG_PRI_MOH_STATE_RETRIEVE_REQ,
	/*! Trying to take channel out of hold when bridged peer requested to hold. */
	SIG_PRI_MOH_STATE_PEND_HOLD,
	/*! Failed to take the channel out of hold. No B channels were available? */
	SIG_PRI_MOH_STATE_RETRIEVE_FAIL,
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

	/*! Number of MOH states.  Must be last in enum. */
	SIG_PRI_MOH_STATE_NUM
};

enum sig_pri_moh_event {
	/*! Reset the MOH state machine. (Because of hangup.) */
	SIG_PRI_MOH_EVENT_RESET,
	/*! Bridged peer placed this channel on hold. */
	SIG_PRI_MOH_EVENT_HOLD,
	/*! Bridged peer took this channel off hold. */
	SIG_PRI_MOH_EVENT_UNHOLD,
#if defined(HAVE_PRI_CALL_HOLD)
	/*! The hold request was successfully acknowledged. */
	SIG_PRI_MOH_EVENT_HOLD_ACK,
	/*! The hold request was rejected. */
	SIG_PRI_MOH_EVENT_HOLD_REJ,
	/*! The unhold request was successfully acknowledged. */
	SIG_PRI_MOH_EVENT_RETRIEVE_ACK,
	/*! The unhold request was rejected. */
	SIG_PRI_MOH_EVENT_RETRIEVE_REJ,
	/*! The remote party took this channel off hold. */
	SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK,
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

	/*! Number of MOH events.  Must be last in enum. */
	SIG_PRI_MOH_EVENT_NUM
};

/*! Call establishment life cycle level for simple comparisons. */
enum sig_pri_call_level {
	/*! Call does not exist. */
	SIG_PRI_CALL_LEVEL_IDLE,
	/*! Call is present but has no response yet. (SETUP) */
	SIG_PRI_CALL_LEVEL_SETUP,
	/*! Call is collecting digits for overlap dialing. (SETUP ACKNOWLEDGE) */
	SIG_PRI_CALL_LEVEL_OVERLAP,
	/*! Call routing is happening. (PROCEEDING) */
	SIG_PRI_CALL_LEVEL_PROCEEDING,
	/*! Called party is being alerted of the call. (ALERTING) */
	SIG_PRI_CALL_LEVEL_ALERTING,
	/*! Call is connected/answered. (CONNECT) */
	SIG_PRI_CALL_LEVEL_CONNECT,
};

struct sig_pri_span;

struct sig_pri_callback {
	/* Unlock the private in the signalling private structure.  This is used for three way calling madness. */
	void (* const unlock_private)(void *pvt);
	/* Lock the private in the signalling private structure.  ... */
	void (* const lock_private)(void *pvt);
	/* Do deadlock avoidance for the private signaling structure lock.  */
	void (* const deadlock_avoidance_private)(void *pvt);
	/* Function which is called back to handle any other DTMF events that are received.  Called by analog_handle_event.  Why is this
	 * important to use, instead of just directly using events received before they are passed into the library?  Because sometimes,
	 * (CWCID) the library absorbs DTMF events received. */
	//void (* const handle_dtmf)(void *pvt, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest);

	//int (* const dial_digits)(void *pvt, enum analog_sub sub, struct analog_dialoperation *dop);
	int (* const play_tone)(void *pvt, enum sig_pri_tone tone);

	int (* const set_echocanceller)(void *pvt, int enable);
	int (* const train_echocanceller)(void *pvt);
	int (* const dsp_reset_and_flush_digits)(void *pvt);

	struct ast_channel * (* const new_ast_channel)(void *pvt, int state, enum sig_pri_law law, char *exten, const struct ast_channel *chan);

	void (* const fixup_chans)(void *old_chan, void *new_chan);

	/* Note: Called with PRI lock held */
	void (* const handle_dchan_exception)(struct sig_pri_span *pri, int index);
	void (* const set_alarm)(void *pvt, int in_alarm);
	void (* const set_dialing)(void *pvt, int is_dialing);
	void (* const set_digital)(void *pvt, int is_digital);
	void (* const set_callerid)(void *pvt, const struct ast_party_caller *caller);
	void (* const set_dnid)(void *pvt, const char *dnid);
	void (* const set_rdnis)(void *pvt, const char *rdnis);
	void (* const queue_control)(void *pvt, int subclass);
	int (* const new_nobch_intf)(struct sig_pri_span *pri);
	void (* const init_config)(void *pvt, struct sig_pri_span *pri);
	const char *(* const get_orig_dialstring)(void *pvt);
	void (* const make_cc_dialstring)(void *pvt, char *buf, size_t buf_size);
	void (* const update_span_devstate)(struct sig_pri_span *pri);

	void (* const open_media)(void *pvt);

	/*!
	 * \brief Post an AMI B channel association event.
	 *
	 * \param pvt Private structure of the user of this module.
	 * \param chan Channel associated with the private pointer
	 *
	 * \return Nothing
	 */
	void (* const ami_channel_event)(void *pvt, struct ast_channel *chan);

	/*! Reference the parent module. */
	void (*module_ref)(void);
	/*! Unreference the parent module. */
	void (*module_unref)(void);
};

#define SIG_PRI_NUM_DCHANS		4		/*!< No more than 4 d-channels */
#define SIG_PRI_MAX_CHANNELS	672		/*!< No more than a DS3 per trunk group */

#define SIG_PRI		DAHDI_SIG_CLEAR
#define SIG_BRI		(0x2000000 | DAHDI_SIG_CLEAR)
#define SIG_BRI_PTMP	(0X4000000 | DAHDI_SIG_CLEAR)

/* QSIG channel mapping option types */
#define DAHDI_CHAN_MAPPING_PHYSICAL	0
#define DAHDI_CHAN_MAPPING_LOGICAL	1

/* Overlap dialing option types */
#define DAHDI_OVERLAPDIAL_NONE 0
#define DAHDI_OVERLAPDIAL_OUTGOING 1
#define DAHDI_OVERLAPDIAL_INCOMING 2
#define DAHDI_OVERLAPDIAL_BOTH (DAHDI_OVERLAPDIAL_INCOMING|DAHDI_OVERLAPDIAL_OUTGOING)

#if defined(HAVE_PRI_SERVICE_MESSAGES)
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
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

struct sig_pri_chan {
	/* Options to be set by user */
	unsigned int hidecallerid:1;
	unsigned int hidecalleridname:1;      /*!< Hide just the name not the number for legacy PBX use */
	unsigned int immediate:1;			/*!< Answer before getting digits? */
	unsigned int priexclusive:1;			/*!< Whether or not to override and use exculsive mode for channel selection */
	unsigned int priindication_oob:1;
	unsigned int use_callerid:1;			/*!< Whether or not to use caller id on this channel */
	unsigned int use_callingpres:1;			/*!< Whether to use the callingpres the calling switch sends */
	char context[AST_MAX_CONTEXT];
	char mohinterpret[MAX_MUSICCLASS];
	int stripmsd;
	int channel;					/*!< Channel Number or CRV */

	/* Options to be checked by user */
	int cid_ani2;						/*!< Automatic Number Identification number (Alternate PRI caller ID number) */
	int cid_ton;					/*!< Type Of Number (TON) */
	int callingpres;				/*!< The value of calling presentation that we're going to use when placing a PRI call */
	char cid_num[AST_MAX_EXTENSION];
	char cid_subaddr[AST_MAX_EXTENSION];
	char cid_name[AST_MAX_EXTENSION];
	char cid_ani[AST_MAX_EXTENSION];
	/*! \brief User tag for party id's sent from this device driver. */
	char user_tag[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];

	/* Internal variables -- Don't touch */
	/* Probably will need DS0 number, DS1 number, and a few other things */
	char dialdest[256];				/* Queued up digits for overlap dialing.  They will be sent out as information messages when setup ACK is received */
#if defined(HAVE_PRI_SETUP_KEYPAD)
	/*! \brief Keypad digits that came in with the SETUP message. */
	char keypad_digits[AST_MAX_EXTENSION];
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
	/*! Music class suggested with AST_CONTROL_HOLD. */
	char moh_suggested[MAX_MUSICCLASS];
	enum sig_pri_moh_state moh_state;

#if defined(HAVE_PRI_AOC_EVENTS)
	struct pri_subcmd_aoc_e aoc_e;
	int aoc_s_request_invoke_id;     /*!< If an AOC-S request was present for the call, this is the invoke_id to use for the response */
	unsigned int aoc_s_request_invoke_id_valid:1; /*!< This is set when the AOC-S invoke id is present */
	unsigned int waiting_for_aoce:1; /*!< Delaying hangup for AOC-E msg. If this is set and AOC-E is received, continue with hangup before timeout period. */
	unsigned int holding_aoce:1;     /*!< received AOC-E msg from asterisk. holding for disconnect/release */
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
	unsigned int inalarm:1;
	unsigned int alreadyhungup:1;	/*!< TRUE if the call has already gone/hungup */
	unsigned int isidlecall:1;		/*!< TRUE if this is an idle call */
	unsigned int progress:1;		/*!< TRUE if the call has seen inband-information progress through the network */
	unsigned int resetting:1;		/*!< TRUE if this channel is being reset/restarted */

	/*!
	 * \brief TRUE when this channel is allocated.
	 *
	 * \details
	 * Needed to hold an outgoing channel allocation before the
	 * owner pointer is created.
	 *
	 * \note This is one of several items to check to see if a
	 * channel is available for use.
	 */
	unsigned int allocated:1;
	unsigned int outgoing:1;
	unsigned int digital:1;
	/*! \brief TRUE if this interface has no B channel.  (call hold and call waiting) */
	unsigned int no_b_channel:1;
#if defined(HAVE_PRI_CALL_WAITING)
	/*! \brief TRUE if this is a call waiting call */
	unsigned int is_call_waiting:1;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */

	struct ast_channel *owner;

	struct sig_pri_span *pri;
	q931_call *call;				/*!< opaque libpri call control structure */

	/*! Call establishment life cycle level for simple comparisons. */
	enum sig_pri_call_level call_level;
	int prioffset;					/*!< channel number in span */
	int logicalspan;				/*!< logical span number within trunk group */
	int mastertrunkgroup;			/*!< what trunk group is our master */
#if defined(HAVE_PRI_SERVICE_MESSAGES)
	/*! \brief Active SRVST_DBKEY out-of-service status value. */
	unsigned service_status;
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

	struct sig_pri_callback *calls;
	void *chan_pvt;					/*!< Private structure of the user of this module. */
#if defined(HAVE_PRI_REVERSE_CHARGE)
	/*!
	 * \brief Reverse charging indication
	 * \details
	 * -1 - No reverse charging,
	 *  1 - Reverse charging,
	 * 0,2-7 - Reserved for future use
	 */
	int reverse_charging_indication;
#endif
};

#if defined(HAVE_PRI_MWI)
/*! Maximum number of mailboxes per span. */
#define SIG_PRI_MAX_MWI_MAILBOXES			8
/*! Typical maximum length of mwi mailbox number */
#define SIG_PRI_MAX_MWI_MBOX_NUMBER_LEN		10	/* digits in number */
/*! Typical maximum length of mwi mailbox context */
#define SIG_PRI_MAX_MWI_CONTEXT_LEN			10
/*!
 * \brief Maximum mwi_mailbox string length.
 * \details
 * max_length = #mailboxes * (mbox_number + '@' + context + ',')
 * The last ',' is a null terminator instead.
 */
#define SIG_PRI_MAX_MWI_MAILBOX_STR		(SIG_PRI_MAX_MWI_MAILBOXES	\
	* (SIG_PRI_MAX_MWI_MBOX_NUMBER_LEN + 1 + SIG_PRI_MAX_MWI_CONTEXT_LEN + 1))

struct sig_pri_mbox {
	/*!
	 * \brief MWI mailbox event subscription.
	 * \note NULL if mailbox not configured.
	 */
	struct ast_event_sub *sub;
	/*! \brief Mailbox number */
	const char *number;
	/*! \brief Mailbox context. */
	const char *context;
};
#endif	/* defined(HAVE_PRI_MWI) */

struct sig_pri_span {
	/* Should be set by user */
	struct ast_cc_config_params *cc_params;			/*!< CC config parameters for each new call. */
	int	pritimers[PRI_MAX_TIMERS];
	int overlapdial;								/*!< In overlap dialing mode */
	int qsigchannelmapping;							/*!< QSIG channel mapping type */
	int discardremoteholdretrieval;					/*!< shall remote hold or remote retrieval notifications be discarded? */
	int facilityenable;								/*!< Enable facility IEs */
	int dchan_logical_span[SIG_PRI_NUM_DCHANS];		/*!< Logical offset the DCHAN sits in */
	int fds[SIG_PRI_NUM_DCHANS];					/*!< FD's for d-channels */

#if defined(HAVE_PRI_AOC_EVENTS)
	int aoc_passthrough_flag;						/*!< Represents what AOC messages (S,D,E) are allowed to pass-through */
	unsigned int aoce_delayhangup:1;				/*!< defines whether the aoce_delayhangup option is enabled or not */
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_SERVICE_MESSAGES)
	unsigned int enable_service_message_support:1;	/*!< enable SERVICE message support */
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
#ifdef HAVE_PRI_INBANDDISCONNECT
	unsigned int inbanddisconnect:1;				/*!< Should we support inband audio after receiving DISCONNECT? */
#endif
#if defined(HAVE_PRI_CALL_HOLD)
	/*! \brief TRUE if held calls are transferred on disconnect. */
	unsigned int hold_disconnect_transfer:1;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
	/*!
	 * \brief TRUE if call transfer is enabled for the span.
	 * \note Support switch-side transfer (called 2BCT, RLT or other names)
	 */
	unsigned int transfer:1;
#if defined(HAVE_PRI_CALL_WAITING)
	/*! \brief TRUE if we will allow incoming ISDN call waiting calls. */
	unsigned int allow_call_waiting_calls:1;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	/*!
	 * TRUE if a new call's sig_pri_chan.user_tag[] has the MSN
	 * appended to the initial_user_tag[].
	 */
	unsigned int append_msn_to_user_tag:1;
#if defined(HAVE_PRI_MCID)
	/*! \brief TRUE if allow sending MCID request on this span. */
	unsigned int mcid_send:1;
#endif	/* defined(HAVE_PRI_MCID) */
	int dialplan;							/*!< Dialing plan */
	int localdialplan;						/*!< Local dialing plan */
	int cpndialplan;						/*!< Connected party dialing plan */
	char internationalprefix[10];			/*!< country access code ('00' for european dialplans) */
	char nationalprefix[10];				/*!< area access code ('0' for european dialplans) */
	char localprefix[20];					/*!< area access code + area code ('0'+area code for european dialplans) */
	char privateprefix[20];					/*!< for private dialplans */
	char unknownprefix[20];					/*!< for unknown dialplans */
	enum sig_pri_moh_signaling moh_signaling;
	long resetinterval;						/*!< Interval (in seconds) for resetting unused channels */
#if defined(HAVE_PRI_DISPLAY_TEXT)
	unsigned long display_flags_send;		/*!< PRI_DISPLAY_OPTION_xxx flags for display text sending */
	unsigned long display_flags_receive;	/*!< PRI_DISPLAY_OPTION_xxx flags for display text receiving */
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
#if defined(HAVE_PRI_MWI)
	/*! \brief Active MWI mailboxes */
	struct sig_pri_mbox mbox[SIG_PRI_MAX_MWI_MAILBOXES];
	/*!
	 * \brief Comma separated list of mailboxes to indicate MWI.
	 * \note Empty if disabled.
	 * \note Format: mailbox_number[@context]{,mailbox_number[@context]}
	 * \note String is split apart when span is started.
	 */
	char mwi_mailboxes[SIG_PRI_MAX_MWI_MAILBOX_STR];
#endif	/* defined(HAVE_PRI_MWI) */
	/*!
	 * \brief Initial user tag for party id's sent from this device driver.
	 * \note String set by config file.
	 */
	char initial_user_tag[AST_MAX_EXTENSION];
	char msn_list[AST_MAX_EXTENSION];		/*!< Comma separated list of MSNs to handle.  Empty if disabled. */
	char idleext[AST_MAX_EXTENSION];		/*!< Where to idle extra calls */
	char idlecontext[AST_MAX_CONTEXT];		/*!< What context to use for idle */
	char idledial[AST_MAX_EXTENSION];		/*!< What to dial before dumping */
	int minunused;							/*!< Min # of channels to keep empty */
	int minidle;							/*!< Min # of "idling" calls to keep active */
	int nodetype;							/*!< Node type */
	int switchtype;							/*!< Type of switch to emulate */
	int nsf;								/*!< Network-Specific Facilities */
	int trunkgroup;							/*!< What our trunkgroup is */
#if defined(HAVE_PRI_CCSS)
	int cc_ptmp_recall_mode;				/*!< CC PTMP recall mode. globalRecall(0), specificRecall(1) */
	int cc_qsig_signaling_link_req;			/*!< CC Q.SIG signaling link retention (Party A) release(0), retain(1), do-not-care(2) */
	int cc_qsig_signaling_link_rsp;			/*!< CC Q.SIG signaling link retention (Party B) release(0), retain(1) */
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CALL_WAITING)
	/*!
	 * \brief Number of extra outgoing calls to allow on a span before
	 * considering that span congested.
	 */
	int max_call_waiting_calls;
	struct {
		int stripmsd;
		unsigned int hidecallerid:1;
		unsigned int hidecalleridname:1;      /*!< Hide just the name not the number for legacy PBX use */
		unsigned int immediate:1;			/*!< Answer before getting digits? */
		unsigned int priexclusive:1;			/*!< Whether or not to override and use exculsive mode for channel selection */
		unsigned int priindication_oob:1;
		unsigned int use_callerid:1;			/*!< Whether or not to use caller id on this channel */
		unsigned int use_callingpres:1;			/*!< Whether to use the callingpres the calling switch sends */
		char context[AST_MAX_CONTEXT];
		char mohinterpret[MAX_MUSICCLASS];
	} ch_cfg;

	/*!
	 * \brief Number of outstanding call waiting calls.
	 * \note Must be zero to allow new calls from asterisk to
	 * immediately allocate a B channel.
	 */
	int num_call_waiting_calls;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	int dchanavail[SIG_PRI_NUM_DCHANS];		/*!< Whether each channel is available */
	int debug;								/*!< set to true if to dump PRI event info */
	int span;								/*!< span number put into user output messages */
	int resetting;							/*!< true if span is being reset/restarted */
	int resetpos;							/*!< current position during a reset (-1 if not started) */
	int sig;								/*!< ISDN signalling type (SIG_PRI, SIG_BRI, SIG_BRI_PTMP, etc...) */
	int new_chan_seq;						/*!< New struct ast_channel sequence number */
	/*! TRUE if we have already whined about no D channels available. */
	unsigned int no_d_channels:1;

	/* Everything after here is internally set */
	struct pri *dchans[SIG_PRI_NUM_DCHANS];		/*!< Actual d-channels */
	struct pri *pri;							/*!< Currently active D-channel */
	/*!
	 * List of private structures of the user of this module for no B channel
	 * interfaces. (hold and call waiting interfaces)
	 */
	void *no_b_chan_iflist;
	/*!
	 * List of private structures of the user of this module for no B channel
	 * interfaces. (hold and call waiting interfaces)
	 */
	void *no_b_chan_end;
	int numchans;								/*!< Num of channels we represent */
	struct sig_pri_chan *pvts[SIG_PRI_MAX_CHANNELS];/*!< Member channel pvt structs */
	pthread_t master;							/*!< Thread of master */
	ast_mutex_t lock;							/*!< libpri access Mutex */
	time_t lastreset;							/*!< time when unused channels were last reset */
	struct sig_pri_callback *calls;
	/*!
	 * \brief Congestion device state of the span.
	 * \details
	 * AST_DEVICE_NOT_INUSE - Span does not have all B channels in use.
	 * AST_DEVICE_BUSY - All B channels are in use.
	 * AST_DEVICE_UNAVAILABLE - Span is in alarm.
	 * \note
	 * Device name: \startverbatim DAHDI/I<span>/congestion. \endverbatim
	 */
	int congestion_devstate;
#if defined(THRESHOLD_DEVSTATE_PLACEHOLDER)
	/*! \todo An ISDN span threshold device state could be useful in determining how often a span utilization goes over a configurable threshold. */
	/*!
	 * \brief User threshold device state of the span.
	 * \details
	 * AST_DEVICE_NOT_INUSE - There are no B channels in use.
	 * AST_DEVICE_INUSE - The number of B channels in use is less than
	 *    the configured threshold but not zero.
	 * AST_DEVICE_BUSY - The number of B channels in use meets or exceeds
	 *    the configured threshold.
	 * AST_DEVICE_UNAVAILABLE - Span is in alarm.
	 * \note
	 * Device name:  DAHDI/I<span>/threshold
	 */
	int threshold_devstate;
	/*!
	 * \brief Number of B channels in use to consider the span in a busy state.
	 * \note Setting the threshold to zero is interpreted as all B channels.
	 */
	int user_busy_threshold;
#endif	/* defined(THRESHOLD_DEVSTATE_PLACEHOLDER) */
};

void sig_pri_extract_called_num_subaddr(struct sig_pri_chan *p, const char *rdest, char *called, size_t called_buff_size);
int sig_pri_call(struct sig_pri_chan *p, struct ast_channel *ast, char *rdest, int timeout, int layer1);

int sig_pri_hangup(struct sig_pri_chan *p, struct ast_channel *ast);

int sig_pri_indicate(struct sig_pri_chan *p, struct ast_channel *chan, int condition, const void *data, size_t datalen);

int sig_pri_answer(struct sig_pri_chan *p, struct ast_channel *ast);

int sig_pri_is_chan_available(struct sig_pri_chan *pvt);
int sig_pri_available(struct sig_pri_chan **pvt, int is_specific_channel);

void sig_pri_init_pri(struct sig_pri_span *pri);

/* If return 0, it means this function was able to handle it (pre setup digits).  If non zero, the user of this
 * functions should handle it normally (generate inband DTMF) */
int sig_pri_digit_begin(struct sig_pri_chan *pvt, struct ast_channel *ast, char digit);

void sig_pri_stop_pri(struct sig_pri_span *pri);
int sig_pri_start_pri(struct sig_pri_span *pri);

void sig_pri_set_alarm(struct sig_pri_chan *p, int in_alarm);
void sig_pri_chan_alarm_notify(struct sig_pri_chan *p, int noalarm);

void pri_event_alarm(struct sig_pri_span *pri, int index, int before_start_pri);

void pri_event_noalarm(struct sig_pri_span *pri, int index, int before_start_pri);

struct ast_channel *sig_pri_request(struct sig_pri_chan *p, enum sig_pri_law law, const struct ast_channel *requestor, int transfercapability);

struct sig_pri_chan *sig_pri_chan_new(void *pvt_data, struct sig_pri_callback *callback, struct sig_pri_span *pri, int logicalspan, int channo, int trunkgroup);
void sig_pri_chan_delete(struct sig_pri_chan *doomed);

int pri_is_up(struct sig_pri_span *pri);

void sig_pri_cli_show_channels_header(int fd);
void sig_pri_cli_show_channels(int fd, struct sig_pri_span *pri);
void sig_pri_cli_show_spans(int fd, int span, struct sig_pri_span *pri);

void sig_pri_cli_show_span(int fd, int *dchannels, struct sig_pri_span *pri);

int pri_send_keypad_facility_exec(struct sig_pri_chan *p, const char *digits);
int pri_send_callrerouting_facility_exec(struct sig_pri_chan *p, enum ast_channel_state chanstate, const char *destination, const char *original, const char *reason);

#if defined(HAVE_PRI_SERVICE_MESSAGES)
int pri_maintenance_bservice(struct pri *pri, struct sig_pri_chan *p, int changestatus);
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

void sig_pri_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, struct sig_pri_chan *pchan);
#if defined(HAVE_PRI_DISPLAY_TEXT)
void sig_pri_sendtext(struct sig_pri_chan *pchan, const char *text);
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */

int sig_pri_cc_agent_init(struct ast_cc_agent *agent, struct sig_pri_chan *pvt_chan);
int sig_pri_cc_agent_start_offer_timer(struct ast_cc_agent *agent);
int sig_pri_cc_agent_stop_offer_timer(struct ast_cc_agent *agent);
void sig_pri_cc_agent_req_rsp(struct ast_cc_agent *agent, enum ast_cc_agent_response_reason reason);
int sig_pri_cc_agent_status_req(struct ast_cc_agent *agent);
int sig_pri_cc_agent_stop_ringing(struct ast_cc_agent *agent);
int sig_pri_cc_agent_party_b_free(struct ast_cc_agent *agent);
int sig_pri_cc_agent_start_monitoring(struct ast_cc_agent *agent);
int sig_pri_cc_agent_callee_available(struct ast_cc_agent *agent);
void sig_pri_cc_agent_destructor(struct ast_cc_agent *agent);

int sig_pri_cc_monitor_req_cc(struct ast_cc_monitor *monitor, int *available_timer_id);
int sig_pri_cc_monitor_suspend(struct ast_cc_monitor *monitor);
int sig_pri_cc_monitor_unsuspend(struct ast_cc_monitor *monitor);
int sig_pri_cc_monitor_status_rsp(struct ast_cc_monitor *monitor, enum ast_device_state devstate);
int sig_pri_cc_monitor_cancel_available_timer(struct ast_cc_monitor *monitor, int *sched_id);
void sig_pri_cc_monitor_destructor(void *monitor_pvt);

int sig_pri_load(const char *cc_type_name);
void sig_pri_unload(void);

#endif /* _SIG_PRI_H */
