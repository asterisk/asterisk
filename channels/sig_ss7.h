/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Interface header for SS7 signaling module.
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_SIG_SS7_H
#define _ASTERISK_SIG_SS7_H

#include "asterisk/channel.h"
#include <libss7.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------- */

/*! SS7 debug message flags when SS7 debugging is turned on at the command line. */
#define SIG_SS7_DEBUG	\
	(SS7_DEBUG_MTP2 | SS7_DEBUG_MTP3 | SS7_DEBUG_ISUP)

#if 0
/*! SS7 debug message flags set on initial startup. */
#define SIG_SS7_DEBUG_DEFAULT	SIG_SS7_DEBUG
#else
/*! SS7 debug message flags set on initial startup. */
#define SIG_SS7_DEBUG_DEFAULT	0
#endif

/* ------------------------------------------------------------------- */

#define SIG_SS7_NUM_DCHANS		4		/*!< No more than 4 d-channels */
#define SIG_SS7_MAX_CHANNELS	672		/*!< No more than a DS3 per trunk group */

#define SIG_SS7		(0x1000000 | DAHDI_SIG_CLEAR)

#define LINKSTATE_INALARM	(1 << 0)
#define LINKSTATE_STARTING	(1 << 1)
#define LINKSTATE_UP		(1 << 2)
#define LINKSTATE_DOWN		(1 << 3)

#define SS7_NAI_DYNAMIC		-1

#define LINKSET_FLAG_EXPLICITACM (1 << 0)
#define LINKSET_FLAG_INITIALHWBLO (1 << 1)
#define LINKSET_FLAG_USEECHOCONTROL (1 << 2)
#define LINKSET_FLAG_DEFAULTECHOCONTROL (1 << 3)
#define LINKSET_FLAG_AUTOACM (1 << 4)

#define SS7_BLOCKED_MAINTENANCE	(1 << 0)
#define SS7_BLOCKED_HARDWARE	(1 << 1)


enum sig_ss7_tone {
	SIG_SS7_TONE_RINGTONE = 0,
	SIG_SS7_TONE_STUTTER,
	SIG_SS7_TONE_CONGESTION,
	SIG_SS7_TONE_DIALTONE,
	SIG_SS7_TONE_DIALRECALL,
	SIG_SS7_TONE_INFO,
	SIG_SS7_TONE_BUSY,
};

enum sig_ss7_law {
	SIG_SS7_DEFLAW = 0,
	SIG_SS7_ULAW,
	SIG_SS7_ALAW
};

enum sig_ss7_redirect_indication {
	SS7_INDICATION_NO_REDIRECTION = 0,
	SS7_INDICATION_REROUTED_PRES_ALLOWED,
	SS7_INDICATION_REROUTED_INFO_RESTRICTED,
	SS7_INDICATION_DIVERTED_PRES_ALLOWED,
	SS7_INDICATION_DIVERTED_INFO_RESTRICTED,
	SS7_INDICATION_REROUTED_PRES_RESTRICTED,
	SS7_INDICATION_DIVERTED_PRES_RESTRICTED,
	SS7_INDICATION_SPARE
};

enum sig_ss7_redirect_reason {
	SS7_REDIRECTING_REASON_UNKNOWN = 0,
	SS7_REDIRECTING_REASON_USER_BUSY,
	SS7_REDIRECTING_REASON_NO_ANSWER,
	SS7_REDIRECTING_REASON_UNCONDITIONAL,
	SS7_REDIRECTING_REASON_DEFLECTION_DURING_ALERTING,
	SS7_REDIRECTING_REASON_DEFLECTION_IMMEDIATE_RESPONSE,
	SS7_REDIRECTING_REASON_UNAVAILABLE
};

/*! Call establishment life cycle level for simple comparisons. */
enum sig_ss7_call_level {
	/*! Call does not exist. */
	SIG_SS7_CALL_LEVEL_IDLE,
	/*!
	 * Call is allocated to the channel.
	 * We have not sent or responded to IAM yet.
	 */
	SIG_SS7_CALL_LEVEL_ALLOCATED,
	/*!
	 * Call is performing continuity check after receiving IAM.
	 * We are waiting for COT to proceed further.
	 */
	SIG_SS7_CALL_LEVEL_CONTINUITY,
	/*!
	 * Call is present.
	 * We have not seen a response or sent further call progress to an IAM yet.
	 */
	SIG_SS7_CALL_LEVEL_SETUP,
	/*!
	 * Call routing is happening.
	 * We have sent or received ACM.
	 */
	SIG_SS7_CALL_LEVEL_PROCEEDING,
	/*!
	 * Called party is being alerted of the call.
	 * We have sent or received CPG(ALERTING)/ACM(ALERTING).
	 */
	SIG_SS7_CALL_LEVEL_ALERTING,
	/*!
	 * Call is connected/answered.
	 * We have sent or received CON/ANM.
	 */
	SIG_SS7_CALL_LEVEL_CONNECT,
};

struct sig_ss7_linkset;

struct sig_ss7_callback {
	/* Unlock the private in the signaling private structure. */
	void (* const unlock_private)(void *pvt);
	/* Lock the private in the signaling private structure. */
	void (* const lock_private)(void *pvt);
	/* Do deadlock avoidance for the private signaling structure lock.  */
	void (* const deadlock_avoidance_private)(void *pvt);

	int (* const set_echocanceller)(void *pvt, int enable);
	void (* const set_loopback)(void *pvt, int enable);

	struct ast_channel * (* const new_ast_channel)(void *pvt, int state,
		enum sig_ss7_law law, char *exten, const struct ast_assigned_ids *assignedids,
		const struct ast_channel *requestor);
	int (* const play_tone)(void *pvt, enum sig_ss7_tone tone);

	void (* const handle_link_exception)(struct sig_ss7_linkset *linkset, int which);
	void (* const set_alarm)(void *pvt, int in_alarm);
	void (* const set_dialing)(void *pvt, int is_dialing);
	void (* const set_digital)(void *pvt, int is_digital);
	void (* const set_outgoing)(void *pvt, int is_outgoing);
	void (* const set_inservice)(void *pvt, int is_inservice);
	void (* const set_locallyblocked)(void *pvt, int is_blocked);
	void (* const set_remotelyblocked)(void *pvt, int is_blocked);
	void (* const set_callerid)(void *pvt, const struct ast_party_caller *caller);
	void (* const set_dnid)(void *pvt, const char *dnid);

	void (* const queue_control)(void *pvt, int subclass);
	void (* const open_media)(void *pvt);

	struct sig_ss7_linkset *(* const find_linkset)(struct ss7 *ss7);
};

/*! Global sig_ss7 callbacks to the upper layer. */
extern struct sig_ss7_callback sig_ss7_callbacks;

struct sig_ss7_chan {
	void *chan_pvt;					/*!< Private structure of the user of this module. */
	struct sig_ss7_linkset *ss7;
	struct ast_channel *owner;

	/*! \brief Opaque libss7 call control structure */
	struct isup_call *ss7call;

	/*! Call establishment life cycle level for simple comparisons. */
	enum sig_ss7_call_level call_level;

	int channel;					/*!< Channel Number */
	int cic;						/*!< CIC associated with channel */
	unsigned int dpc;				/*!< CIC's DPC */

	/* Options to be set by user */
	/*!
	 * \brief Number of most significant digits/characters to strip from the dialed number.
	 * \note Feature is deprecated.  Use dialplan logic.
	 */
	int stripmsd;
	/*!
	 * \brief TRUE if the outgoing caller ID is blocked/hidden.
	 */
	unsigned int hidecallerid:1;
	/*! \brief TRUE if caller ID is used on this channel. */
	unsigned int use_callerid:1;
	/*!
	 * \brief TRUE if we will use the calling presentation setting
	 * from the Asterisk channel for outgoing calls.
	 */
	unsigned int use_callingpres:1;
	unsigned int immediate:1;		/*!< Answer before getting digits? */

	/*!
	 * \brief Bitmask for the channel being locally blocked.
	 * \note 1 maintenance blocked, 2 blocked in hardware.
	 * \note Set by user and link.
	 */
	unsigned int locallyblocked:2;

	/*!
	 * \brief Bitmask for the channel being remotely blocked.
	 * \note 1 maintenance blocked, 2 blocked in hardware.
	 * \note Set by user and link.
	 */
	unsigned int remotelyblocked:2;

	char context[AST_MAX_CONTEXT];
	char mohinterpret[MAX_MUSICCLASS];

	/* Options to be checked by user */
	int cid_ani2;					/*!< Automatic Number Identification number (Alternate PRI caller ID number) */
	int cid_ton;					/*!< Type Of Number (TON) */
	int callingpres;				/*!< The value of calling presentation that we're going to use when placing a PRI call */
	char cid_num[AST_MAX_EXTENSION];
	char cid_subaddr[AST_MAX_EXTENSION];/*!< XXX SS7 may not support. */
	char cid_name[AST_MAX_EXTENSION];
	char cid_ani[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];

	/* Options to be checked by user that are stuffed into channel variables. */
	char charge_number[50];
	char gen_add_number[50];
	char gen_dig_number[50];
	char orig_called_num[50];
	int orig_called_presentation;
	char redirecting_num[50];
	int redirecting_presentation;
	unsigned char redirect_counter;
	unsigned char redirect_info;
	unsigned char redirect_info_ind;
	unsigned char redirect_info_orig_reas;
	unsigned char redirect_info_counter;
	unsigned char redirect_info_reas;
	char generic_name[50];
	unsigned char gen_add_num_plan;
	unsigned char gen_add_nai;
	unsigned char gen_add_pres_ind;
	unsigned char gen_add_type;
	unsigned char gen_dig_type;
	unsigned char gen_dig_scheme;
	char jip_number[50];
#if 0
	unsigned char lspi_type;
	unsigned char lspi_scheme;
	unsigned char lspi_context;
#endif
	char lspi_ident[50];
	unsigned int call_ref_ident;
	unsigned int call_ref_pc;
	unsigned char calling_party_cat;
	unsigned int do_hangup;	/* What we have to do to clear the call */
	unsigned int echocontrol_ind;

	/*
	 * Channel status bits.
	 */
	/*! \brief TRUE if channel is associated with a link that is down. */
	unsigned int inalarm:1;
	/*! \brief TRUE if channel is in service. */
	unsigned int inservice:1;
	/*! \brief TRUE if this channel is being used for an outgoing call. */
	unsigned int outgoing:1;
	/*! \brief TRUE if the channel has completed collecting digits. */
	unsigned int called_complete:1;
	/*! \brief TRUE if the call has seen inband-information progress through the network. */
	unsigned int progress:1;
	/*! \brief XXX BOOLEAN Purpose??? */
	unsigned int rlt:1;
	/*! \brief TRUE if this channel is in loopback. */
	unsigned int loopedback:1;

	/*
	 * Closed User Group fields Q.735.1
	 */
	/*! \brief Network Identify Code as per Q.763 3.15.a */
	char cug_interlock_ni[5];
	/*! \brief Binari Code to uniquely identify a CUG inside the network. */
	unsigned short cug_interlock_code;
	/*!
	 * \brief Indication of the call being a CUG call and its permissions.
	 * \note 0 or 1 - non-CUG call
	 * \note 2 - CUG call, outgoing access allowed
	 * \note 3 - CUG call, outgoing access not allowed
	 */
	unsigned char cug_indicator;
};

struct sig_ss7_linkset {
	pthread_t master;					/*!< Thread of master */
	ast_mutex_t lock;					/*!< libss7 access lock */
	struct ss7 *ss7;
	struct sig_ss7_chan *pvts[SIG_SS7_MAX_CHANNELS];/*!< Member channel pvt structs */
	int fds[SIG_SS7_NUM_DCHANS];
	int numsigchans;
	int linkstate[SIG_SS7_NUM_DCHANS];
	int numchans;
	int span;							/*!< span number put into user output messages */
	int debug;							/*!< set to true if to dump SS7 event info */
	enum {
		LINKSET_STATE_DOWN = 0,
		LINKSET_STATE_UP
	} state;

	/* Options to be set by user */
	int flags;							/*!< Linkset flags (LINKSET_FLAG_EXPLICITACM) */
	int type;							/*!< SS7 type ITU/ANSI. Used for companding selection. */
	char called_nai;					/*!< Called Nature of Address Indicator */
	char calling_nai;					/*!< Calling Nature of Address Indicator */
	char internationalprefix[10];		/*!< country access code ('00' for european dialplans) */
	char nationalprefix[10];			/*!< area access code ('0' for european dialplans) */
	char subscriberprefix[20];			/*!< area access code + area code ('0'+area code for european dialplans) */
	char unknownprefix[20];				/*!< for unknown dialplans */
	char networkroutedprefix[20];
};

void sig_ss7_set_alarm(struct sig_ss7_chan *p, int in_alarm);

void *ss7_linkset(void *data);

void sig_ss7_link_alarm(struct sig_ss7_linkset *linkset, int which);
void sig_ss7_link_noalarm(struct sig_ss7_linkset *linkset, int which);
int sig_ss7_add_sigchan(struct sig_ss7_linkset *linkset, int which, int ss7type, int transport, int inalarm, int networkindicator, int pointcode, int adjpointcode, int cur_slc);

int sig_ss7_reset_cic(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc);
int sig_ss7_reset_group(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc, int range);
int sig_ss7_cic_blocking(struct sig_ss7_linkset *linkset, int do_block, int cic);
int sig_ss7_group_blocking(struct sig_ss7_linkset *linkset, int do_block, int startcic, int endcic, unsigned char state[], int type);

int sig_ss7_available(struct sig_ss7_chan *p);
int sig_ss7_call(struct sig_ss7_chan *p, struct ast_channel *ast, const char *rdest);
int sig_ss7_hangup(struct sig_ss7_chan *p, struct ast_channel *ast);
int sig_ss7_answer(struct sig_ss7_chan *p, struct ast_channel *ast);
int sig_ss7_find_cic(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc);
int sig_ss7_find_cic_range(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc);
void sig_ss7_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, struct sig_ss7_chan *pchan);
int sig_ss7_indicate(struct sig_ss7_chan *p, struct ast_channel *chan, int condition, const void *data, size_t datalen);
struct ast_channel *sig_ss7_request(struct sig_ss7_chan *p, enum sig_ss7_law law,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor,
	int transfercapability);
void sig_ss7_chan_delete(struct sig_ss7_chan *doomed);
struct sig_ss7_chan *sig_ss7_chan_new(void *pvt_data, struct sig_ss7_linkset *ss7);
void sig_ss7_init_linkset(struct sig_ss7_linkset *ss7);
void sig_ss7_free_isup_call(struct sig_ss7_linkset *linkset, int channel);

void sig_ss7_cli_show_channels_header(int fd);
void sig_ss7_cli_show_channels(int fd, struct sig_ss7_linkset *linkset);

int sig_ss7_cb_hangup(struct ss7 *ss7, int cic, unsigned int dpc, int cause, int do_hangup);
void sig_ss7_cb_call_null(struct ss7 *ss7, struct isup_call *c, int lock);
void sig_ss7_cb_notinservice(struct ss7 *ss7, int cic, unsigned int dpc);

/* ------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif	/* _ASTERISK_SIG_SS7_H */
/* ------------------------------------------------------------------- */
/* end sig_ss7.h */
