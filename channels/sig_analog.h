#ifndef _SIG_ANALOG_H
#define _SIG_ANALOG_H
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
 * \brief Interface header for analog signaling module
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 */

#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/smdi.h"

#define ANALOG_SMDI_MD_WAIT_TIMEOUT 1500 /* 1.5 seconds */
#define ANALOG_MAX_CID 300
#define READ_SIZE 160
#define RING_PATTERNS 3

/*! \brief Default time (ms) to detect first digit */
#define ANALOG_FIRST_DIGIT_TIMEOUT	16000
/*! \brief Default time (ms) to detect following digits */
#define ANALOG_INTER_DIGIT_TIMEOUT	8000
/*! \brief Default time (ms) to wait, in case of ambiguous match */
#define ANALOG_MATCH_DIGIT_TIMEOUT	3000

/* Signalling types supported */
enum analog_sigtype {
	ANALOG_SIG_NONE = -1,
	ANALOG_SIG_FXOLS = 1,
	ANALOG_SIG_FXOKS,
	ANALOG_SIG_FXOGS,
	ANALOG_SIG_FXSLS,
	ANALOG_SIG_FXSKS,
	ANALOG_SIG_FXSGS,
	ANALOG_SIG_EMWINK,
	ANALOG_SIG_EM,
	ANALOG_SIG_EM_E1,
	ANALOG_SIG_FEATD,
	ANALOG_SIG_FEATDMF,
	ANALOG_SIG_E911,
	ANALOG_SIG_FGC_CAMA,
	ANALOG_SIG_FGC_CAMAMF,
	ANALOG_SIG_FEATB,
	ANALOG_SIG_SFWINK,
	ANALOG_SIG_SF,
	ANALOG_SIG_SF_FEATD,
	ANALOG_SIG_SF_FEATDMF,
	ANALOG_SIG_FEATDMF_TA,
	ANALOG_SIG_SF_FEATB,
};

enum analog_tone {
	ANALOG_TONE_RINGTONE = 0,
	ANALOG_TONE_STUTTER,
	ANALOG_TONE_CONGESTION,
	ANALOG_TONE_DIALTONE,
	ANALOG_TONE_DIALRECALL,
	ANALOG_TONE_INFO,
};

enum analog_event {
	ANALOG_EVENT_NONE = 0,
	ANALOG_EVENT_ONHOOK,
	ANALOG_EVENT_RINGOFFHOOK,
	ANALOG_EVENT_WINKFLASH,
	ANALOG_EVENT_ALARM,
	ANALOG_EVENT_NOALARM,
	ANALOG_EVENT_DIALCOMPLETE,
	ANALOG_EVENT_RINGERON,
	ANALOG_EVENT_RINGEROFF,
	ANALOG_EVENT_HOOKCOMPLETE,
	ANALOG_EVENT_PULSE_START,
	ANALOG_EVENT_POLARITY,
	ANALOG_EVENT_RINGBEGIN,
	ANALOG_EVENT_EC_DISABLED,
	ANALOG_EVENT_REMOVED,
	ANALOG_EVENT_NEONMWI_ACTIVE,
	ANALOG_EVENT_NEONMWI_INACTIVE,
	ANALOG_EVENT_TX_CED_DETECTED,
	ANALOG_EVENT_RX_CED_DETECTED,
	ANALOG_EVENT_EC_NLP_DISABLED,
	ANALOG_EVENT_EC_NLP_ENABLED,
	ANALOG_EVENT_ERROR, /* not a DAHDI event */
	ANALOG_EVENT_DTMFCID, /* not a DAHDI event */
	ANALOG_EVENT_PULSEDIGIT = (1 << 16),
	ANALOG_EVENT_DTMFDOWN = (1 << 17),
	ANALOG_EVENT_DTMFUP = (1 << 18),
};

enum analog_sub {
	ANALOG_SUB_REAL = 0,			/*!< Active call */
	ANALOG_SUB_CALLWAIT,			/*!< Call-Waiting call on hold */
	ANALOG_SUB_THREEWAY,			/*!< Three-way call */
};

enum analog_dsp_digitmode {
	ANALOG_DIGITMODE_DTMF = 1,
	ANALOG_DIGITMODE_MF,
};

enum analog_cid_start {
	ANALOG_CID_START_POLARITY = 1,
	ANALOG_CID_START_POLARITY_IN,
	ANALOG_CID_START_RING,
	ANALOG_CID_START_DTMF_NOALERT,
};

enum dialop {
	ANALOG_DIAL_OP_REPLACE = 2,
};


struct analog_dialoperation {
	enum dialop op;
	char dialstr[256];
};

struct analog_callback {
	/* Unlock the private in the signalling private structure.  This is used for three way calling madness. */
	void (* const unlock_private)(void *pvt);
	/* Lock the private in the signalling private structure.  ... */
	void (* const lock_private)(void *pvt);
	/* Do deadlock avoidance for the private signaling structure lock.  */
	void (* const deadlock_avoidance_private)(void *pvt);

	/* Function which is called back to handle any other DTMF events that are received.  Called by analog_handle_event.  Why is this
	 * important to use, instead of just directly using events received before they are passed into the library?  Because sometimes,
	 * (CWCID) the library absorbs DTMF events received. */
	void (* const handle_dtmf)(void *pvt, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest);

	int (* const get_event)(void *pvt);
	int (* const wait_event)(void *pvt);
	int (* const is_off_hook)(void *pvt);
	int (* const is_dialing)(void *pvt, enum analog_sub sub);
	/* Start a trunk type signalling protocol (everything except phone ports basically */
	int (* const start)(void *pvt);
	int (* const ring)(void *pvt);
	int (* const flash)(void *pvt);
	/*! \brief Set channel on hook */
	int (* const on_hook)(void *pvt);
	/*! \brief Set channel off hook */
	int (* const off_hook)(void *pvt);
	void (* const set_needringing)(void *pvt, int value);
	/*! \brief Set FXS line polarity to 0=IDLE NZ=REVERSED */
	void (* const set_polarity)(void *pvt, int value);
	/*! \brief Reset FXS line polarity to IDLE, based on answeronpolarityswitch and hanguponpolarityswitch */
	void (* const start_polarityswitch)(void *pvt);
	/*! \brief Switch FXS line polarity, based on answeronpolarityswitch=yes */
	void (* const answer_polarityswitch)(void *pvt);
	/*! \brief Switch FXS line polarity, based on answeronpolarityswitch and hanguponpolarityswitch */
	void (* const hangup_polarityswitch)(void *pvt);
	/* We're assuming that we're going to only wink on ANALOG_SUB_REAL - even though in the code there's an argument to the index
	 * function */
	int (* const wink)(void *pvt, enum analog_sub sub);
	int (* const dial_digits)(void *pvt, enum analog_sub sub, struct analog_dialoperation *dop);
	int (* const send_fsk)(void *pvt, struct ast_channel *ast, char *fsk);
	int (* const play_tone)(void *pvt, enum analog_sub sub, enum analog_tone tone);

	int (* const set_echocanceller)(void *pvt, int enable);
	int (* const train_echocanceller)(void *pvt);
	int (* const dsp_set_digitmode)(void *pvt, enum analog_dsp_digitmode mode);
	int (* const dsp_reset_and_flush_digits)(void *pvt);
	int (* const send_callerid)(void *pvt, int cwcid, struct ast_party_caller *caller);
	/* Returns 0 if CID received.  Returns 1 if event received, and -1 if error.  name and num are size ANALOG_MAX_CID */
	int (* const get_callerid)(void *pvt, char *name, char *num, enum analog_event *ev, size_t timeout);
	/* Start CID detection */
	int (* const start_cid_detect)(void *pvt, int cid_signalling);
	/* Stop CID detection */
	int (* const stop_cid_detect)(void *pvt);

	/* Play the CAS callwait tone on the REAL sub, then repeat after 10 seconds, and then stop */
	int (* const callwait)(void *pvt);
	/* Stop playing any CAS call waiting announcement tones that might be running on the REAL sub */
	int (* const stop_callwait)(void *pvt);

	/* Bearer control related (non signalling) callbacks */
	int (* const allocate_sub)(void *pvt, enum analog_sub sub);
	int (* const unallocate_sub)(void *pvt, enum analog_sub sub);
	/*! This function is for swapping of the owners with the underlying subs.  Typically it means you need to change the fds
	 * of the new owner to be the fds of the sub specified, for each of the two subs given */
	void (* const swap_subs)(void *pvt, enum analog_sub a, struct ast_channel *new_a_owner, enum analog_sub b, struct ast_channel *new_b_owner);
	struct ast_channel * (* const new_ast_channel)(void *pvt, int state, int startpbx, enum analog_sub sub, const struct ast_channel *requestor);

	/* Add the given sub to a conference */
	int (* const conf_add)(void *pvt, enum analog_sub sub);
	/* Delete the given sub from any conference that might be running on the channels */
	int (* const conf_del)(void *pvt, enum analog_sub sub);

	/* If you would like to do any optimizations after the conference members have been added and removed,
	 * you can do so here */
	int (* const complete_conference_update)(void *pvt, int needconf);

	/* This is called when there are no more subchannels on the given private that are left up,
	 * for any cleanup or whatever else you would like to do.  Called from analog_hangup() */
	void (* const all_subchannels_hungup)(void *pvt);

	int (* const has_voicemail)(void *pvt);
	int (* const check_for_conference)(void *pvt);
	void (* const handle_notify_message)(struct ast_channel *chan, void *pvt, int cid_flags, int neon_mwievent);

	/* callbacks for increasing and decreasing ss_thread_count, will handle locking and condition signal */
	void (* const increase_ss_count)(void);
	void (* const decrease_ss_count)(void);

	int (* const distinctive_ring)(struct ast_channel *chan, void *pvt, int idx, int *ringdata);
	/* Sets the specified sub-channel in and out of signed linear mode, returns the value that was overwritten */
	int (* const set_linear_mode)(void *pvt, enum analog_sub sub, int linear_mode);
	void (* const set_inthreeway)(void *pvt, enum analog_sub sub, int inthreeway);
	void (* const get_and_handle_alarms)(void *pvt);
	void * (* const get_sigpvt_bridged_channel)(struct ast_channel *chan);
	int (* const get_sub_fd)(void *pvt, enum analog_sub sub);
	void (* const set_cadence)(void *pvt, int *cidrings, struct ast_channel *chan);
	void (* const set_alarm)(void *pvt, int in_alarm);
	void (* const set_dialing)(void *pvt, int is_dialing);
	void (* const set_outgoing)(void *pvt, int is_outgoing);
	void (* const set_ringtimeout)(void *pvt, int ringt);
	void (* const set_waitingfordt)(void *pvt, struct ast_channel *ast);
	int (* const check_waitingfordt)(void *pvt);
	void (* const set_confirmanswer)(void *pvt, int flag);
	int (* const check_confirmanswer)(void *pvt);
	void (* const set_callwaiting)(void *pvt, int callwaiting_enable);
	void (* const cancel_cidspill)(void *pvt);
	int (* const confmute)(void *pvt, int mute);
	void (* const set_pulsedial)(void *pvt, int flag);
	void (* const set_new_owner)(void *pvt, struct ast_channel *new_owner);

	const char *(* const get_orig_dialstring)(void *pvt);
	int (* const have_progressdetect)(void *pvt);
	int (* const get_firstdigit_timeout)(void *pvt);
	int (* const get_interdigit_timeout)(void *pvt);
	int (* const get_matchdigit_timeout)(void *pvt);
};

/*! Global analog callbacks to the upper layer. */
extern struct analog_callback analog_callbacks;


struct analog_subchannel {
	struct ast_channel *owner;
	struct ast_frame f;		/*!< One frame for each channel.  How did this ever work before? */
	unsigned int inthreeway:1;
	/* Have we allocated a subchannel yet or not */
	unsigned int allocd:1;
};

struct analog_pvt {
	/* Analog signalling type used in this private */
	enum analog_sigtype sig;
	/* To contain the private structure passed into the channel callbacks */
	void *chan_pvt;
	/* All members after this are giong to be transient, and most will probably change */
	struct ast_channel *owner;			/*!< Our current active owner (if applicable) */

	struct analog_subchannel subs[3];		/*!< Sub-channels */
	struct analog_dialoperation dop;
	int onhooktime;							/*< Time the interface went on-hook. */
	int fxsoffhookstate;					/*< TRUE if the FXS port is off-hook */
	/*! \brief -1 = unknown, 0 = no messages, 1 = new messages available */
	int msgstate;

	/* XXX: Option Variables - Set by allocator of private structure */
	unsigned int answeronpolarityswitch:1;
	unsigned int callreturn:1;
	unsigned int cancallforward:1;
	unsigned int canpark:1;
	unsigned int dahditrcallerid:1;			/*!< should we use the callerid from incoming call on dahdi transfer or not */
	unsigned int hanguponpolarityswitch:1;
	unsigned int immediate:1;
	unsigned int permcallwaiting:1;			/*!< TRUE if call waiting is enabled. (Configured option) */
	unsigned int permhidecallerid:1;		/*!< Whether to hide our outgoing caller ID or not */
	unsigned int pulse:1;
	unsigned int threewaycalling:1;
	unsigned int transfer:1;
	unsigned int transfertobusy:1;			/*!< allow flash-transfers to busy channels */
	unsigned int use_callerid:1;			/*!< Whether or not to use caller id on this channel */
	unsigned int usedistinctiveringdetection:1;
	unsigned int callwaitingcallerid:1;		/*!< TRUE if send caller ID for Call Waiting */
	/*!
	 * \brief TRUE if SMDI (Simplified Message Desk Interface) is enabled
	 */
	unsigned int use_smdi:1;
	/*! \brief The SMDI interface to get SMDI messages from. */
	struct ast_smdi_interface *smdi_iface;

	/* Not used for anything but log messages.  Could be just the TCID */
	int channel;					/*!< Channel Number */

	enum analog_sigtype outsigmod;
	int echotraining;
	int cid_signalling;				/*!< Asterisk callerid type we're using */
	int polarityonanswerdelay;
	int stripmsd;
	enum analog_cid_start cid_start;
	char mohsuggest[MAX_MUSICCLASS];
	char cid_num[AST_MAX_EXTENSION];
	char cid_name[AST_MAX_EXTENSION];


	/* XXX: All variables after this are internal */
	unsigned int callwaiting:1;		/*!< TRUE if call waiting is enabled. (Active option) */
	unsigned int dialednone:1;
	unsigned int dialing:1;			/*!< TRUE if in the process of dialing digits or sending something */
	unsigned int dnd:1;				/*!< TRUE if Do-Not-Disturb is enabled. */
	unsigned int echobreak:1;
	unsigned int hidecallerid:1;
	unsigned int outgoing:1;
	unsigned int inalarm:1;
	/*!
	 * \brief TRUE if Call Waiting (CW) CPE Alert Signal (CAS) is being sent.
	 * \note
	 * After CAS is sent, the call waiting caller id will be sent if the phone
	 * gives a positive reply.
	 */
	unsigned int callwaitcas:1;

	char callwait_num[AST_MAX_EXTENSION];
	char callwait_name[AST_MAX_EXTENSION];
	char lastcid_num[AST_MAX_EXTENSION];
	char lastcid_name[AST_MAX_EXTENSION];
	struct ast_party_caller caller;
	int cidrings;					/*!< Which ring to deliver CID on */
	char echorest[20];
	int polarity;
	struct timeval polaritydelaytv;
	char dialdest[256];
	time_t guardtime;				/*!< Must wait this much time before using for new call */
	struct timeval flashtime;			/*!< Last flash-hook time */
	int whichwink;					/*!< SIG_FEATDMF_TA Which wink are we on? */
	char finaldial[64];
	char *origcid_num;				/*!< malloced original callerid */
	char *origcid_name;				/*!< malloced original callerid */
	char call_forward[AST_MAX_EXTENSION];

	/* Ast channel to pass to __ss_analog_thread */
	struct ast_channel *ss_astchan;

	/* All variables after this are definitely going to be audited */
	int ringt;
	int ringt_base;
};

struct analog_pvt *analog_new(enum analog_sigtype signallingtype, void *private_data);
void analog_delete(struct analog_pvt *doomed);

void analog_free(struct analog_pvt *p);

int analog_call(struct analog_pvt *p, struct ast_channel *ast, const char *rdest, int timeout);

int analog_hangup(struct analog_pvt *p, struct ast_channel *ast);

int analog_answer(struct analog_pvt *p, struct ast_channel *ast);

struct ast_frame *analog_exception(struct analog_pvt *p, struct ast_channel *ast);

struct ast_channel * analog_request(struct analog_pvt *p, int *callwait, const struct ast_channel *requestor);

int analog_available(struct analog_pvt *p);

void *analog_handle_init_event(struct analog_pvt *i, int event);

int analog_config_complete(struct analog_pvt *p);

void analog_handle_dtmf(struct analog_pvt *p, struct ast_channel *ast, enum analog_sub index, struct ast_frame **dest);

enum analog_cid_start analog_str_to_cidstart(const char *value);

const char *analog_cidstart_to_str(enum analog_cid_start cid_start);

enum analog_sigtype analog_str_to_sigtype(const char *name);

const char *analog_sigtype_to_str(enum analog_sigtype sigtype);

unsigned int analog_str_to_cidtype(const char *name);

const char *analog_cidtype_to_str(unsigned int cid_type);

int analog_ss_thread_start(struct analog_pvt *p, struct ast_channel *ast);

int analog_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, void *newp);

int analog_dnd(struct analog_pvt *p, int flag);

#endif /* _SIG_ANSLOG_H */
