/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
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
 * \brief DAHDI internal API definitions.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_CHAN_DAHDI_H
#define _ASTERISK_CHAN_DAHDI_H

#if defined(HAVE_OPENR2)
#include <openr2.h>
#endif	/* defined(HAVE_OPENR2) */

#include <dahdi/user.h>
#include <dahdi/tonezone.h>

#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/app.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* ------------------------------------------------------------------- */

#if defined(HAVE_PRI)
struct sig_pri_span;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
struct sig_ss7_linkset;
#endif	/* defined(HAVE_SS7) */

#define SUB_REAL		0			/*!< Active call */
#define SUB_CALLWAIT	1			/*!< Call-Waiting call on hold */
#define SUB_THREEWAY	2			/*!< Three-way call */


struct distRingData {
	int ring[3];
	int range;
};
struct ringContextData {
	char contextData[AST_MAX_CONTEXT];
};
struct dahdi_distRings {
	struct distRingData ringnum[3];
	struct ringContextData ringContext[3];
};


extern const char * const subnames[];

struct dahdi_subchannel {
	int dfd;
	struct ast_channel *owner;
	int chan;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct ast_frame f;		/*!< One frame for each channel.  How did this ever work before? */
	unsigned int needringing:1;
	unsigned int needbusy:1;
	unsigned int needcongestion:1;
	unsigned int needanswer:1;
	unsigned int needflash:1;
	unsigned int needhold:1;
	unsigned int needunhold:1;
	unsigned int linear:1;
	unsigned int inthreeway:1;
	struct dahdi_confinfo curconf;
};

#define MAX_SLAVES	4

/* States for sending MWI message
 * First three states are required for send Ring Pulse Alert Signal
 */
typedef enum {
	MWI_SEND_NULL = 0,
	MWI_SEND_SA,
	MWI_SEND_SA_WAIT,
	MWI_SEND_PAUSE,
	MWI_SEND_SPILL,
	MWI_SEND_CLEANUP,
	MWI_SEND_DONE,
} mwisend_states;

struct mwisend_info {
	struct timeval pause;
	mwisend_states mwisend_current;
};

/*! Specify the lists dahdi_pvt can be put in. */
enum DAHDI_IFLIST {
	DAHDI_IFLIST_NONE,	/*!< The dahdi_pvt is not in any list. */
	DAHDI_IFLIST_MAIN,	/*!< The dahdi_pvt is in the main interface list */
#if defined(HAVE_PRI)
	DAHDI_IFLIST_NO_B_CHAN,	/*!< The dahdi_pvt is in a no B channel interface list */
#endif	/* defined(HAVE_PRI) */
};

struct dahdi_pvt {
	ast_mutex_t lock;					/*!< Channel private lock. */
	struct callerid_state *cs;
	struct ast_channel *owner;			/*!< Our current active owner (if applicable) */
							/*!< Up to three channels can be associated with this call */

	struct dahdi_subchannel sub_unused;		/*!< Just a safety precaution */
	struct dahdi_subchannel subs[3];			/*!< Sub-channels */
	struct dahdi_confinfo saveconf;			/*!< Saved conference info */

	struct dahdi_pvt *slaves[MAX_SLAVES];		/*!< Slave to us (follows our conferencing) */
	struct dahdi_pvt *master;				/*!< Master to us (we follow their conferencing) */
	int inconference;				/*!< If our real should be in the conference */

	int bufsize;                /*!< Size of the buffers */
	int buf_no;					/*!< Number of buffers */
	int buf_policy;				/*!< Buffer policy */
	int faxbuf_no;              /*!< Number of Fax buffers */
	int faxbuf_policy;          /*!< Fax buffer policy */
	int sig;					/*!< Signalling style */
	/*!
	 * \brief Nonzero if the signaling type is sent over a radio.
	 * \note Set to a couple of nonzero values but it is only tested like a boolean.
	 */
	int radio;
	int outsigmod;					/*!< Outbound Signalling style (modifier) */
	int oprmode;					/*!< "Operator Services" mode */
	struct dahdi_pvt *oprpeer;				/*!< "Operator Services" peer tech_pvt ptr */
	/*! \brief Hardware Rx gain set by chan_dahdi.conf */
	float hwrxgain;
	/*! \brief Hardware Tx gain set by chan_dahdi.conf */
	float hwtxgain;
	/*! \brief Amount of gain to increase during caller id */
	float cid_rxgain;
	/*! \brief Software Rx gain set by chan_dahdi.conf */
	float rxgain;
	/*! \brief Software Tx gain set by chan_dahdi.conf */
	float txgain;

	float txdrc; /*!< Dynamic Range Compression factor. a number between 1 and 6ish */
	float rxdrc;

	int tonezone;					/*!< tone zone for this chan, or -1 for default */
	enum DAHDI_IFLIST which_iflist;	/*!< Which interface list is this structure listed? */
	struct dahdi_pvt *next;				/*!< Next channel in list */
	struct dahdi_pvt *prev;				/*!< Prev channel in list */

	/* flags */

	/*!
	 * \brief TRUE if ADSI (Analog Display Services Interface) available
	 * \note Set from the "adsi" value read in from chan_dahdi.conf
	 */
	unsigned int adsi:1;
	/*!
	 * \brief TRUE if we can use a polarity reversal to mark when an outgoing
	 * call is answered by the remote party.
	 * \note Set from the "answeronpolarityswitch" value read in from chan_dahdi.conf
	 */
	unsigned int answeronpolarityswitch:1;
	/*!
	 * \brief TRUE if busy detection is enabled.
	 * (Listens for the beep-beep busy pattern.)
	 * \note Set from the "busydetect" value read in from chan_dahdi.conf
	 */
	unsigned int busydetect:1;
	/*!
	 * \brief TRUE if call return is enabled.
	 * (*69, if your dialplan doesn't catch this first)
	 * \note Set from the "callreturn" value read in from chan_dahdi.conf
	 */
	unsigned int callreturn:1;
	/*!
	 * \brief TRUE if busy extensions will hear the call-waiting tone
	 * and can use hook-flash to switch between callers.
	 * \note Can be disabled by dialing *70.
	 * \note Initialized with the "callwaiting" value read in from chan_dahdi.conf
	 */
	unsigned int callwaiting:1;
	/*!
	 * \brief TRUE if send caller ID for Call Waiting
	 * \note Set from the "callwaitingcallerid" value read in from chan_dahdi.conf
	 */
	unsigned int callwaitingcallerid:1;
	/*!
	 * \brief TRUE if support for call forwarding enabled.
	 * Dial *72 to enable call forwarding.
	 * Dial *73 to disable call forwarding.
	 * \note Set from the "cancallforward" value read in from chan_dahdi.conf
	 */
	unsigned int cancallforward:1;
	/*!
	 * \brief TRUE if support for call parking is enabled.
	 * \note Set from the "canpark" value read in from chan_dahdi.conf
	 */
	unsigned int canpark:1;
	/*! \brief TRUE if to wait for a DTMF digit to confirm answer */
	unsigned int confirmanswer:1;
	/*!
	 * \brief TRUE if the channel is to be destroyed on hangup.
	 * (Used by pseudo channels.)
	 */
	unsigned int destroy:1;
	unsigned int didtdd:1;				/*!< flag to say its done it once */
	/*! \brief TRUE if analog type line dialed no digits in Dial() */
	unsigned int dialednone:1;
	/*!
	 * \brief TRUE if in the process of dialing digits or sending something.
	 * \note This is used as a receive squelch for ISDN until connected.
	 */
	unsigned int dialing:1;
	/*! \brief TRUE if the transfer capability of the call is digital. */
	unsigned int digital:1;
	/*! \brief TRUE if Do-Not-Disturb is enabled, present only for non sig_analog */
	unsigned int dnd:1;
	/*! \brief XXX BOOLEAN Purpose??? */
	unsigned int echobreak:1;
	/*!
	 * \brief TRUE if echo cancellation enabled when bridged.
	 * \note Initialized with the "echocancelwhenbridged" value read in from chan_dahdi.conf
	 * \note Disabled if the echo canceller is not setup.
	 */
	unsigned int echocanbridged:1;
	/*! \brief TRUE if echo cancellation is turned on. */
	unsigned int echocanon:1;
	/*! \brief TRUE if a fax tone has already been handled. */
	unsigned int faxhandled:1;
	/*! TRUE if dynamic faxbuffers are configured for use, default is OFF */
	unsigned int usefaxbuffers:1;
	/*! TRUE while buffer configuration override is in use */
	unsigned int bufferoverrideinuse:1;
	/*! \brief TRUE if over a radio and dahdi_read() has been called. */
	unsigned int firstradio:1;
	/*!
	 * \brief TRUE if the call will be considered "hung up" on a polarity reversal.
	 * \note Set from the "hanguponpolarityswitch" value read in from chan_dahdi.conf
	 */
	unsigned int hanguponpolarityswitch:1;
	/*! \brief TRUE if DTMF detection needs to be done by hardware. */
	unsigned int hardwaredtmf:1;
	/*!
	 * \brief TRUE if the outgoing caller ID is blocked/hidden.
	 * \note Caller ID can be disabled by dialing *67.
	 * \note Caller ID can be enabled by dialing *82.
	 * \note Initialized with the "hidecallerid" value read in from chan_dahdi.conf
	 */
	unsigned int hidecallerid:1;
	/*!
	 * \brief TRUE if hide just the name not the number for legacy PBX use.
	 * \note Only applies to PRI channels.
	 * \note Set from the "hidecalleridname" value read in from chan_dahdi.conf
	 */
	unsigned int hidecalleridname:1;
	/*! \brief TRUE if DTMF detection is disabled. */
	unsigned int ignoredtmf:1;
	/*!
	 * \brief TRUE if the channel should be answered immediately
	 * without attempting to gather any digits.
	 * \note Set from the "immediate" value read in from chan_dahdi.conf
	 */
	unsigned int immediate:1;
	/*! \brief TRUE if in an alarm condition. */
	unsigned int inalarm:1;
	/*! \brief TRUE if TDD in MATE mode */
	unsigned int mate:1;
	/*! \brief TRUE if we originated the call leg. */
	unsigned int outgoing:1;
	/*!
	 * \brief TRUE if busy extensions will hear the call-waiting tone
	 * and can use hook-flash to switch between callers.
	 * \note Set from the "callwaiting" value read in from chan_dahdi.conf
	 */
	unsigned int permcallwaiting:1;
	/*!
	 * \brief TRUE if the outgoing caller ID is blocked/restricted/hidden.
	 * \note Set from the "hidecallerid" value read in from chan_dahdi.conf
	 */
	unsigned int permhidecallerid:1;
	/*!
	 * \brief TRUE if PRI congestion/busy indications are sent out-of-band.
	 * \note Set from the "priindication" value read in from chan_dahdi.conf
	 */
	unsigned int priindication_oob:1;
	/*!
	 * \brief TRUE if PRI B channels are always exclusively selected.
	 * \note Set from the "priexclusive" value read in from chan_dahdi.conf
	 */
	unsigned int priexclusive:1;
	/*!
	 * \brief TRUE if we will pulse dial.
	 * \note Set from the "pulsedial" value read in from chan_dahdi.conf
	 */
	unsigned int pulse:1;
	/*! \brief TRUE if a pulsed digit was detected. (Pulse dial phone detected) */
	unsigned int pulsedial:1;
	unsigned int restartpending:1;		/*!< flag to ensure counted only once for restart */
	/*!
	 * \brief TRUE if caller ID is restricted.
	 * \note Set but not used.  Should be deleted.  Redundant with permhidecallerid.
	 * \note Set from the "restrictcid" value read in from chan_dahdi.conf
	 */
	unsigned int restrictcid:1;
	/*!
	 * \brief TRUE if three way calling is enabled
	 * \note Set from the "threewaycalling" value read in from chan_dahdi.conf
	 */
	unsigned int threewaycalling:1;
	/*!
	 * \brief TRUE if call transfer is enabled
	 * \note For FXS ports (either direct analog or over T1/E1):
	 *   Support flash-hook call transfer
	 * \note For digital ports using ISDN PRI protocols:
	 *   Support switch-side transfer (called 2BCT, RLT or other names)
	 * \note Set from the "transfer" value read in from chan_dahdi.conf
	 */
	unsigned int transfer:1;
	/*!
	 * \brief TRUE if caller ID is used on this channel.
	 * \note PRI and SS7 spans will save caller ID from the networking peer.
	 * \note FXS ports will generate the caller ID spill.
	 * \note FXO ports will listen for the caller ID spill.
	 * \note Set from the "usecallerid" value read in from chan_dahdi.conf
	 */
	unsigned int use_callerid:1;
	/*!
	 * \brief TRUE if we will use the calling presentation setting
	 * from the Asterisk channel for outgoing calls.
	 * \note Only applies to PRI and SS7 channels.
	 * \note Set from the "usecallingpres" value read in from chan_dahdi.conf
	 */
	unsigned int use_callingpres:1;
	/*!
	 * \brief TRUE if distinctive rings are to be detected.
	 * \note For FXO lines
	 * \note Set indirectly from the "usedistinctiveringdetection" value read in from chan_dahdi.conf
	 */
	unsigned int usedistinctiveringdetection:1;
	/*!
	 * \brief TRUE if we should use the callerid from incoming call on dahdi transfer.
	 * \note Set from the "useincomingcalleridondahditransfer" value read in from chan_dahdi.conf
	 */
	unsigned int dahditrcallerid:1;
	/*!
	 * \brief TRUE if allowed to flash-transfer to busy channels.
	 * \note Set from the "transfertobusy" value read in from chan_dahdi.conf
	 */
	unsigned int transfertobusy:1;
	/*!
	 * \brief TRUE if the FXO port monitors for neon type MWI indications from the other end.
	 * \note Set if the "mwimonitor" value read in contains "neon" from chan_dahdi.conf
	 */
	unsigned int mwimonitor_neon:1;
	/*!
	 * \brief TRUE if the FXO port monitors for fsk type MWI indications from the other end.
	 * \note Set if the "mwimonitor" value read in contains "fsk" from chan_dahdi.conf
	 */
	unsigned int mwimonitor_fsk:1;
	/*!
	 * \brief TRUE if the FXO port monitors for rpas precursor to fsk MWI indications from the other end.
	 * \note RPAS - Ring Pulse Alert Signal
	 * \note Set if the "mwimonitor" value read in contains "rpas" from chan_dahdi.conf
	 */
	unsigned int mwimonitor_rpas:1;
	/*! \brief TRUE if an MWI monitor thread is currently active */
	unsigned int mwimonitoractive:1;
	/*! \brief TRUE if a MWI message sending thread is active */
	unsigned int mwisendactive:1;
	/*!
	 * \brief TRUE if channel is out of reset and ready
	 * \note Used by SS7.  Otherwise set but not used.
	 */
	unsigned int inservice:1;
	/*!
	 * \brief Bitmask for the channel being locally blocked.
	 * \note Applies to SS7 and MFCR2 channels.
	 * \note For MFCR2 only the first bit is used - TRUE if blocked
	 * \note For SS7 two bits are used
	 * \note Bit 0 - TRUE if maintenance blocked
	 * \note Bit 1 - TRUE if hardware blocked
	 */
	unsigned int locallyblocked:2;
	/*!
	 * \brief Bitmask for the channel being remotely blocked. 1 maintenance, 2 blocked in hardware.
	 * \note Applies to SS7 and MFCR2 channels.
	 * \note For MFCR2 only the first bit is used - TRUE if blocked
	 * \note For SS7 two bits are used
	 * \note Bit 0 - TRUE if maintenance blocked
	 * \note Bit 1 - TRUE if hardware blocked
	 */
	unsigned int remotelyblocked:2;
	/*!
	 * \brief TRUE if the channel alarms will be managed also as Span ones
	 * \note Applies to all channels
	 */
	unsigned int manages_span_alarms:1;
	/*! \brief TRUE if hardware Rx gain set by Asterisk */
	unsigned int hwrxgain_enabled;
	/*! \brief TRUE if hardware Tx gain set by Asterisk */
	unsigned int hwtxgain_enabled;

#if defined(HAVE_PRI)
	struct sig_pri_span *pri;
	int logicalspan;
#endif	/* defined(HAVE_PRI) */
	/*!
	 * \brief TRUE if SMDI (Simplified Message Desk Interface) is enabled
	 * \note Set from the "usesmdi" value read in from chan_dahdi.conf
	 */
	unsigned int use_smdi:1;
	struct mwisend_info mwisend_data;
	/*! \brief The SMDI interface to get SMDI messages from. */
	struct ast_smdi_interface *smdi_iface;

	/*! \brief Distinctive Ring data */
	struct dahdi_distRings drings;

	/*!
	 * \brief The configured context for incoming calls.
	 * \note The "context" string read in from chan_dahdi.conf
	 */
	char context[AST_MAX_CONTEXT];
	/*!
	 * \brief A description for the channel configuration
	 * \note The "description" string read in from chan_dahdi.conf
	 */
	char description[32];
	/*!
	 * \brief Default distinctive ring context.
	 */
	char defcontext[AST_MAX_CONTEXT];
	/*! \brief Extension to use in the dialplan. */
	char exten[AST_MAX_EXTENSION];
	/*!
	 * \brief Language configured for calls.
	 * \note The "language" string read in from chan_dahdi.conf
	 */
	char language[MAX_LANGUAGE];
	/*!
	 * \brief The configured music-on-hold class to use for calls.
	 * \note The "musicclass" or "mohinterpret" or "musiconhold" string read in from chan_dahdi.conf
	 */
	char mohinterpret[MAX_MUSICCLASS];
	/*!
	 * \brief Suggested music-on-hold class for peer channel to use for calls.
	 * \note The "mohsuggest" string read in from chan_dahdi.conf
	 */
	char mohsuggest[MAX_MUSICCLASS];
	char parkinglot[AST_MAX_EXTENSION]; /*!< Parking lot for this channel */
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	/*! \brief Automatic Number Identification number (Alternate PRI caller ID number) */
	char cid_ani[AST_MAX_EXTENSION];
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
	/*! \brief Automatic Number Identification code from PRI */
	int cid_ani2;
	/*! \brief Caller ID number from an incoming call. */
	char cid_num[AST_MAX_EXTENSION];
	/*!
	 * \brief Caller ID tag from incoming call
	 * \note the "cid_tag" string read in from chan_dahdi.conf
	 */
	char cid_tag[AST_MAX_EXTENSION];
	/*! \brief Caller ID Q.931 TON/NPI field values.  Set by PRI. Zero otherwise. */
	int cid_ton;
	/*! \brief Caller ID name from an incoming call. */
	char cid_name[AST_MAX_EXTENSION];
	/*! \brief Caller ID subaddress from an incoming call. */
	char cid_subaddr[AST_MAX_EXTENSION];
	char *origcid_num;				/*!< malloced original callerid */
	char *origcid_name;				/*!< malloced original callerid */
	/*! \brief Call waiting number. */
	char callwait_num[AST_MAX_EXTENSION];
	/*! \brief Call waiting name. */
	char callwait_name[AST_MAX_EXTENSION];
	/*! \brief Redirecting Directory Number Information Service (RDNIS) number */
	char rdnis[AST_MAX_EXTENSION];
	/*! \brief Dialed Number Identifier */
	char dnid[AST_MAX_EXTENSION];
	/*!
	 * \brief Bitmapped groups this belongs to.
	 * \note The "group" bitmapped group string read in from chan_dahdi.conf
	 */
	ast_group_t group;
	/*! \brief Default call PCM encoding format: DAHDI_LAW_ALAW or DAHDI_LAW_MULAW. */
	int law_default;
	/*! \brief Active PCM encoding format: DAHDI_LAW_ALAW or DAHDI_LAW_MULAW */
	int law;
	int confno;					/*!< Our conference */
	int confusers;					/*!< Who is using our conference */
	int propconfno;					/*!< Propagated conference number */
	/*!
	 * \brief Bitmapped call groups this belongs to.
	 * \note The "callgroup" bitmapped group string read in from chan_dahdi.conf
	 */
	ast_group_t callgroup;
	/*!
	 * \brief Bitmapped pickup groups this belongs to.
	 * \note The "pickupgroup" bitmapped group string read in from chan_dahdi.conf
	 */
	ast_group_t pickupgroup;
	/*!
	 * \brief Named call groups this belongs to.
	 * \note The "namedcallgroup" string read in from chan_dahdi.conf
	 */
	struct ast_namedgroups *named_callgroups;
	/*!
	 * \brief Named pickup groups this belongs to.
	 * \note The "namedpickupgroup" string read in from chan_dahdi.conf
	 */
	struct ast_namedgroups *named_pickupgroups;
	/*!
	 * \brief Channel variable list with associated values to set when a channel is created.
	 * \note The "setvar" strings read in from chan_dahdi.conf
	 */
	struct ast_variable *vars;
	int channel;					/*!< Channel Number */
	int span;					/*!< Span number */
	time_t guardtime;				/*!< Must wait this much time before using for new call */
	int cid_signalling;				/*!< CID signalling type bell202 or v23 */
	int cid_start;					/*!< CID start indicator, polarity or ring or DTMF without warning event */
	int dtmfcid_holdoff_state;		/*!< State indicator that allows for line to settle before checking for dtmf energy */
	struct timeval	dtmfcid_delay;  /*!< Time value used for allow line to settle */
	int callingpres;				/*!< The value of calling presentation that we're going to use when placing a PRI call */
	int callwaitingrepeat;				/*!< How many samples to wait before repeating call waiting */
	int cidcwexpire;				/*!< When to stop waiting for CID/CW CAS response (In samples) */
	int cid_suppress_expire;		/*!< How many samples to suppress after a CID spill. */
	/*! \brief Analog caller ID waveform sample buffer */
	unsigned char *cidspill;
	/*! \brief Position in the cidspill buffer to send out next. */
	int cidpos;
	/*! \brief Length of the cidspill buffer containing samples. */
	int cidlen;
	/*! \brief Ring timeout timer?? */
	int ringt;
	/*!
	 * \brief Ring timeout base.
	 * \note Value computed indirectly from "ringtimeout" read in from chan_dahdi.conf
	 */
	int ringt_base;
	/*!
	 * \brief Number of most significant digits/characters to strip from the dialed number.
	 * \note Feature is deprecated.  Use dialplan logic.
	 * \note The characters are stripped before the PRI TON/NPI prefix
	 * characters are processed.
	 */
	int stripmsd;
	/*!
	 * \brief TRUE if Call Waiting (CW) CPE Alert Signal (CAS) is being sent.
	 * \note
	 * After CAS is sent, the call waiting caller id will be sent if the phone
	 * gives a positive reply.
	 */
	int callwaitcas;
	/*! \brief Number of call waiting rings. */
	int callwaitrings;
	/*! \brief Echo cancel parameters. */
	struct {
		struct dahdi_echocanparams head;
		struct dahdi_echocanparam params[DAHDI_MAX_ECHOCANPARAMS];
	} echocancel;
	/*!
	 * \brief Echo training time. 0 = disabled
	 * \note Set from the "echotraining" value read in from chan_dahdi.conf
	 */
	int echotraining;
	/*! \brief Filled with 'w'.  XXX Purpose?? */
	char echorest[20];
	/*!
	 * \brief Number of times to see "busy" tone before hanging up.
	 * \note Set from the "busycount" value read in from chan_dahdi.conf
	 */
	int busycount;
	/*!
	 * \brief Busy cadence pattern description.
	 * \note Set from the "busypattern" value read from chan_dahdi.conf
	 */
	struct ast_dsp_busy_pattern busy_cadence;
	/*!
	 * \brief Bitmapped call progress detection flags. CALLPROGRESS_xxx values.
	 * \note Bits set from the "callprogress" and "faxdetect" values read in from chan_dahdi.conf
	 */
	int callprogress;
	/*!
	 * \brief Number of milliseconds to wait for dialtone.
	 * \note Set from the "waitfordialtone" value read in from chan_dahdi.conf
	 */
	int waitfordialtone;
	/*!
	 * \brief Number of frames to watch for dialtone in incoming calls
	 * \note Set from the "dialtone_detect" value read in from chan_dahdi.conf
	 */
	int dialtone_detect;
	int dialtone_scanning_time_elapsed;	/*!< Amount of audio scanned for dialtone, in frames */
	struct timeval waitingfordt;			/*!< Time we started waiting for dialtone */
	struct timeval flashtime;			/*!< Last flash-hook time */
	/*! \brief Opaque DSP configuration structure. */
	struct ast_dsp *dsp;
	/*! \brief DAHDI dial operation command struct for ioctl() call. */
	struct dahdi_dialoperation dop;
	int whichwink;					/*!< SIG_FEATDMF_TA Which wink are we on? */
	/*! \brief Second part of SIG_FEATDMF_TA wink operation. */
	char finaldial[64];
	char accountcode[AST_MAX_ACCOUNT_CODE];		/*!< Account code */
	int amaflags;					/*!< AMA Flags */
	struct tdd_state *tdd;				/*!< TDD flag */
	/*! \brief Accumulated call forwarding number. */
	char call_forward[AST_MAX_EXTENSION];
	/*!
	 * \brief Voice mailbox location.
	 * \note Set from the "mailbox" string read in from chan_dahdi.conf
	 */
	char mailbox[AST_MAX_MAILBOX_UNIQUEID];
	/*! \brief Opaque event subscription parameters for message waiting indication support. */
	struct stasis_subscription *mwi_event_sub;
	/*! \brief Delayed dialing for E911.  Overlap digits for ISDN. */
	char dialdest[256];
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
	struct dahdi_vmwi_info mwisend_setting;				/*!< Which VMWI methods to use */
	unsigned int mwisend_fsk: 1;		/*! Variable for enabling FSK MWI handling in chan_dahdi */
	unsigned int mwisend_rpas:1;		/*! Variable for enabling Ring Pulse Alert before MWI FSK Spill */
#endif
	int distinctivering;				/*!< Which distinctivering to use */
	int dtmfrelax;					/*!< whether to run in relaxed DTMF mode */
	/*! \brief Holding place for event injected from outside normal operation. */
	int fake_event;
	/*!
	 * \brief Minimal time period (ms) between the answer polarity
	 * switch and hangup polarity switch.
	 */
	int polarityonanswerdelay;
	/*! \brief Start delay time if polarityonanswerdelay is nonzero. */
	struct timeval polaritydelaytv;
	/*!
	 * \brief Send caller ID on FXS after this many rings. Set to 1 for US.
	 * \note Set from the "sendcalleridafter" value read in from chan_dahdi.conf
	 */
	int sendcalleridafter;
	/*! \brief Current line interface polarity. POLARITY_IDLE, POLARITY_REV */
	int polarity;
	/*! \brief DSP feature flags: DSP_FEATURE_xxx */
	int dsp_features;
#if defined(HAVE_SS7)
	/*! \brief SS7 control parameters */
	struct sig_ss7_linkset *ss7;
#endif	/* defined(HAVE_SS7) */
#if defined(HAVE_OPENR2)
	struct dahdi_mfcr2 *mfcr2;
	openr2_chan_t *r2chan;
	openr2_calling_party_category_t mfcr2_recvd_category;
	openr2_calling_party_category_t mfcr2_category;
	int mfcr2_dnis_index;
	int mfcr2_ani_index;
	int mfcr2call:1;
	int mfcr2_answer_pending:1;
	int mfcr2_charge_calls:1;
	int mfcr2_allow_collect_calls:1;
	int mfcr2_forced_release:1;
	int mfcr2_dnis_matched:1;
	int mfcr2_call_accepted:1;
	int mfcr2_accept_on_offer:1;
	int mfcr2_progress_sent:1;
#endif	/* defined(HAVE_OPENR2) */
	/*! \brief DTMF digit in progress.  0 when no digit in progress. */
	char begindigit;
	/*! \brief TRUE if confrence is muted. */
	int muting;
	void *sig_pvt;
	struct ast_cc_config_params *cc_params;
	/* DAHDI channel names may differ greatly from the
	 * string that was provided to an app such as Dial. We
	 * need to save the original string passed to dahdi_request
	 * for call completion purposes. This way, we can replicate
	 * the original dialed string later.
	 */
	char dialstring[AST_CHANNEL_NAME];
};


/* Analog signaling */
#define SIG_EM          DAHDI_SIG_EM
#define SIG_EMWINK      (0x0100000 | DAHDI_SIG_EM)
#define SIG_FEATD       (0x0200000 | DAHDI_SIG_EM)
#define SIG_FEATDMF     (0x0400000 | DAHDI_SIG_EM)
#define SIG_FEATB       (0x0800000 | DAHDI_SIG_EM)
#define SIG_E911        (0x1000000 | DAHDI_SIG_EM)
#define SIG_FEATDMF_TA  (0x2000000 | DAHDI_SIG_EM)
#define SIG_FGC_CAMA    (0x4000000 | DAHDI_SIG_EM)
#define SIG_FGC_CAMAMF  (0x8000000 | DAHDI_SIG_EM)
#define SIG_FXSLS       DAHDI_SIG_FXSLS
#define SIG_FXSGS       DAHDI_SIG_FXSGS
#define SIG_FXSKS       DAHDI_SIG_FXSKS
#define SIG_FXOLS       DAHDI_SIG_FXOLS
#define SIG_FXOGS       DAHDI_SIG_FXOGS
#define SIG_FXOKS       DAHDI_SIG_FXOKS
#define SIG_SF          DAHDI_SIG_SF
#define SIG_SFWINK      (0x0100000 | DAHDI_SIG_SF)
#define SIG_SF_FEATD    (0x0200000 | DAHDI_SIG_SF)
#define SIG_SF_FEATDMF  (0x0400000 | DAHDI_SIG_SF)
#define SIG_SF_FEATB    (0x0800000 | DAHDI_SIG_SF)
#define SIG_EM_E1       DAHDI_SIG_EM_E1

/* PRI signaling */
#define SIG_PRI         DAHDI_SIG_CLEAR
#define SIG_BRI         (0x2000000 | DAHDI_SIG_CLEAR)
#define SIG_BRI_PTMP    (0X4000000 | DAHDI_SIG_CLEAR)

/* SS7 signaling */
#define SIG_SS7         (0x1000000 | DAHDI_SIG_CLEAR)

/* MFC/R2 signaling */
#define SIG_MFCR2       DAHDI_SIG_CAS


#define SIG_PRI_LIB_HANDLE_CASES	\
	SIG_PRI:						\
	case SIG_BRI:					\
	case SIG_BRI_PTMP

/*!
 * \internal
 * \brief Determine if sig_pri handles the signaling.
 * \since 1.8
 *
 * \param signaling Signaling to determine if is for sig_pri.
 *
 * \return TRUE if the signaling is for sig_pri.
 */
static inline int dahdi_sig_pri_lib_handles(int signaling)
{
	int handles;

	switch (signaling) {
	case SIG_PRI_LIB_HANDLE_CASES:
		handles = 1;
		break;
	default:
		handles = 0;
		break;
	}

	return handles;
}

static inline int dahdi_analog_lib_handles(int signalling, int radio, int oprmode)
{
	switch (signalling) {
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
	case SIG_EMWINK:
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FGC_CAMA:
	case SIG_FGC_CAMAMF:
	case SIG_FEATB:
	case SIG_SFWINK:
	case SIG_SF:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_FEATDMF_TA:
	case SIG_SF_FEATB:
		break;
	default:
		/* The rest of the function should cover the remainder of signalling types */
		return 0;
	}

	if (radio) {
		return 0;
	}

	if (oprmode) {
		return 0;
	}

	return 1;
}

#define dahdi_get_index(ast, p, nullok)	_dahdi_get_index(ast, p, nullok, __PRETTY_FUNCTION__, __LINE__)
int _dahdi_get_index(struct ast_channel *ast, struct dahdi_pvt *p, int nullok, const char *fname, unsigned long line);

void dahdi_dtmf_detect_disable(struct dahdi_pvt *p);
void dahdi_dtmf_detect_enable(struct dahdi_pvt *p);

void dahdi_ec_enable(struct dahdi_pvt *p);
void dahdi_ec_disable(struct dahdi_pvt *p);

void dahdi_conf_update(struct dahdi_pvt *p);
void dahdi_master_slave_link(struct dahdi_pvt *slave, struct dahdi_pvt *master);
void dahdi_master_slave_unlink(struct dahdi_pvt *slave, struct dahdi_pvt *master, int needlock);

/* ------------------------------------------------------------------- */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_CHAN_DAHDI_H */
