/*
 * Chan_Misdn -- Channel Driver for Asterisk
 *
 * Interface to mISDN
 *
 * Copyright (C) 2004, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief Interface to mISDN
 *
 * \author Christian Richter <crich@beronet.com>
 */

#ifndef TE_LIB
#define TE_LIB

#include <mISDNuser/suppserv.h>

/** For initialization usage **/
/* typedef int ie_nothing_t ;*/
/** end of init usage **/


/*
 * uncomment the following to make chan_misdn create
 * record files in /tmp/misdn-{rx|tx}-PortChannel format
 * */

/*#define MISDN_SAVE_DATA*/

#ifdef WITH_BEROEC
typedef int beroec_t;


enum beroec_type {
	BEROEC_FULLBAND=0,
	BEROEC_SUBBAND,
	BEROEC_FASTSUBBAND
};

void beroec_init(void);
void beroec_exit(void);
beroec_t *beroec_new(int tail, enum beroec_type type, int anti_howl,
		     int tonedisable, int zerocoeff, int adapt, int nlp);

void beroec_destroy(beroec_t *ec);
int beroec_cancel_alaw_chunk(beroec_t *ec,
	char *send,
	char *receive,
	int len);

int beroec_version(void);
#endif



enum tone_e {
	TONE_NONE=0,
	TONE_DIAL,
	TONE_ALERTING,
	TONE_FAR_ALERTING,
	TONE_BUSY,
	TONE_HANGUP,
	TONE_CUSTOM,
	TONE_FILE
};



#define MAX_BCHANS 31

enum bchannel_state {
	BCHAN_CLEANED=0,
	BCHAN_EMPTY,
	BCHAN_SETUP,
	BCHAN_SETUPED,
	BCHAN_ACTIVE,
	BCHAN_ACTIVATED,
	BCHAN_BRIDGE,
	BCHAN_BRIDGED,
	BCHAN_RELEASE,
	BCHAN_RELEASED,
	BCHAN_CLEAN,
	BCHAN_CLEAN_REQUEST,
	BCHAN_ERROR
};


enum misdn_err_e {
	ENOCHAN=1
};

enum mISDN_NUMBER_PLAN {
	NUMPLAN_UNKNOWN = 0x0,
	NUMPLAN_ISDN = 0x1,		/* ISDN/Telephony numbering plan E.164 */
	NUMPLAN_DATA = 0x3,		/* Data numbering plan X.121 */
	NUMPLAN_TELEX = 0x4,	/* Telex numbering plan F.69 */
	NUMPLAN_NATIONAL = 0x8,
	NUMPLAN_PRIVATE = 0x9
};

enum mISDN_NUMBER_TYPE {
	NUMTYPE_UNKNOWN = 0x0,
	NUMTYPE_INTERNATIONAL = 0x1,
	NUMTYPE_NATIONAL = 0x2,
	NUMTYPE_NETWORK_SPECIFIC = 0x3,
	NUMTYPE_SUBSCRIBER = 0x4,
	NUMTYPE_ABBREVIATED = 0x5
};

enum event_response_e {
	RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE,
	RESPONSE_IGNORE_SETUP,
	RESPONSE_RELEASE_SETUP,
	RESPONSE_ERR,
	RESPONSE_OK
};


enum event_e {
	EVENT_NOTHING,
	EVENT_TONE_GENERATE,
	EVENT_BCHAN_DATA,
	EVENT_BCHAN_ACTIVATED,
	EVENT_BCHAN_ERROR,
	EVENT_CLEANUP,
	EVENT_PROCEEDING,
	EVENT_PROGRESS,
	EVENT_SETUP,
	EVENT_REGISTER,
	EVENT_ALERTING,
	EVENT_CONNECT,
	EVENT_SETUP_ACKNOWLEDGE,
	EVENT_CONNECT_ACKNOWLEDGE ,
	EVENT_USER_INFORMATION,
	EVENT_SUSPEND_REJECT,
	EVENT_RESUME_REJECT,
	EVENT_HOLD,
	EVENT_SUSPEND,
	EVENT_RESUME,
	EVENT_HOLD_ACKNOWLEDGE,
	EVENT_SUSPEND_ACKNOWLEDGE,
	EVENT_RESUME_ACKNOWLEDGE,
	EVENT_HOLD_REJECT,
	EVENT_RETRIEVE,
	EVENT_RETRIEVE_ACKNOWLEDGE,
	EVENT_RETRIEVE_REJECT,
	EVENT_DISCONNECT,
	EVENT_RESTART,
	EVENT_RELEASE,
	EVENT_RELEASE_COMPLETE,
	EVENT_FACILITY,
	EVENT_NOTIFY,
	EVENT_STATUS_ENQUIRY,
	EVENT_INFORMATION,
	EVENT_STATUS,
	EVENT_TIMEOUT,
	EVENT_DTMF_TONE,
	EVENT_NEW_L3ID,
	EVENT_NEW_BC,
	EVENT_PORT_ALARM,
	EVENT_NEW_CHANNEL,
	EVENT_UNKNOWN
};


enum ie_name_e {
	IE_DUMMY,
	IE_LAST
};

enum { /* bearer capability */
	INFO_CAPABILITY_SPEECH=0,
	INFO_CAPABILITY_AUDIO_3_1K=0x10 ,
	INFO_CAPABILITY_AUDIO_7K=0x11 ,
	INFO_CAPABILITY_VIDEO =0x18,
	INFO_CAPABILITY_DIGITAL_UNRESTRICTED =0x8,
	INFO_CAPABILITY_DIGITAL_RESTRICTED =0x09,
	INFO_CAPABILITY_DIGITAL_UNRESTRICTED_TONES
};

enum { /* progress indicators */
	INFO_PI_CALL_NOT_E2E_ISDN =0x01,
	INFO_PI_CALLED_NOT_ISDN =0x02,
	INFO_PI_CALLER_NOT_ISDN =0x03,
	INFO_PI_CALLER_RETURNED_TO_ISDN =0x04,
	INFO_PI_INBAND_AVAILABLE =0x08,
	INFO_PI_DELAY_AT_INTERF =0x0a,
	INFO_PI_INTERWORKING_WITH_PUBLIC =0x10,
	INFO_PI_INTERWORKING_NO_RELEASE =0x11,
	INFO_PI_INTERWORKING_NO_RELEASE_PRE_ANSWER =0x12,
	INFO_PI_INTERWORKING_NO_RELEASE_POST_ANSWER =0x13
};

/*!
 * \brief Q.931 encoded redirecting reason
 */
enum mISDN_REDIRECTING_REASON {
	mISDN_REDIRECTING_REASON_UNKNOWN = 0x0,
	/*! Call forwarding busy or called DTE busy */
	mISDN_REDIRECTING_REASON_CALL_FWD_BUSY = 0x1,
	/*! Call forwarding no reply */
	mISDN_REDIRECTING_REASON_NO_REPLY = 0x2,
	/*! Call deflection */
	mISDN_REDIRECTING_REASON_DEFLECTION = 0x4,
	/*! Called DTE out of order */
	mISDN_REDIRECTING_REASON_OUT_OF_ORDER = 0x9,
	/*! Call forwarding by the called DTE */
	mISDN_REDIRECTING_REASON_CALL_FWD_DTE = 0xA,
	/*! Call forwarding unconditional or systematic call redirection */
	mISDN_REDIRECTING_REASON_CALL_FWD = 0xF
};

/*!
 * \brief Notification description code enumeration
 */
enum mISDN_NOTIFY_CODE {
	mISDN_NOTIFY_CODE_INVALID = -1,
	/*! Call is placed on hold (Q.931) */
	mISDN_NOTIFY_CODE_USER_SUSPEND = 0x00,
	/*! Call is taken off of hold (Q.931) */
	mISDN_NOTIFY_CODE_USER_RESUME = 0x01,
	/*! Call is diverting (EN 300 207-1 Section 7.2.1) */
	mISDN_NOTIFY_CODE_CALL_IS_DIVERTING = 0x7B,
	/*! Call diversion is enabled (cfu, cfb, cfnr) (EN 300 207-1 Section 7.2.1) */
	mISDN_NOTIFY_CODE_DIVERSION_ACTIVATED = 0x68,
	/*! Call transfer, alerting (EN 300 369-1 Section 7.2) */
	mISDN_NOTIFY_CODE_CALL_TRANSFER_ALERTING = 0x69,
	/*! Call transfer, active(answered) (EN 300 369-1 Section 7.2) */
	mISDN_NOTIFY_CODE_CALL_TRANSFER_ACTIVE = 0x6A,
};

enum { /*CODECS*/
	INFO_CODEC_ULAW=2,
	INFO_CODEC_ALAW=3
};


enum layer_e {
	L3,
	L2,
	L1,
	UNKNOWN
};

/*! Maximum phone number (address) length plus null terminator */
#define MISDN_MAX_NUMBER_LEN		(31 + 1)

/*! Maximum name length plus null terminator (From ECMA-164) */
#define MISDN_MAX_NAME_LEN			(50 + 1)

/*! Maximum subaddress length plus null terminator */
#define MISDN_MAX_SUBADDRESS_LEN	(23 + 1)

/*! Maximum keypad facility content length plus null terminator */
#define MISDN_MAX_KEYPAD_LEN		(31 + 1)

/*! \brief Dialed/Called information struct */
struct misdn_party_dialing {
	/*! \brief Type-of-number in ISDN terms for the dialed/called number */
	enum mISDN_NUMBER_TYPE number_type;

	/*! \brief Type-of-number numbering plan. */
	enum mISDN_NUMBER_PLAN number_plan;

	/*! \brief Dialed/Called Phone Number (Address) */
	char number[MISDN_MAX_NUMBER_LEN];

	/*! \brief Dialed/Called Subaddress number */
	char subaddress[MISDN_MAX_SUBADDRESS_LEN];
};

/*! \brief Connected-Line/Calling/Redirecting ID info struct */
struct misdn_party_id {
	/*! \brief Number presentation restriction code
	 * 0=Allowed, 1=Restricted, 2=Unavailable
	 */
	int presentation;

	/*! \brief Number screening code
	 * 0=Unscreened, 1=Passed Screen, 2=Failed Screen, 3=Network Number
	 */
	int screening;

	/*! \brief Type-of-number in ISDN terms for the number */
	enum mISDN_NUMBER_TYPE number_type;

	/*! \brief Type-of-number numbering plan. */
	enum mISDN_NUMBER_PLAN number_plan;

	/*! \brief Subscriber Name
	 * \note The name is currently obtained from Asterisk for
	 * potential use in display ie's since basic ISDN does
	 * not support names directly.
	 */
	char name[MISDN_MAX_NAME_LEN];

	/*! \brief Phone number (Address) */
	char number[MISDN_MAX_NUMBER_LEN];

	/*! \brief Subaddress number */
	char subaddress[MISDN_MAX_SUBADDRESS_LEN];
};

/*! \brief Redirecting information struct */
struct misdn_party_redirecting {
	/*! \brief Who is redirecting the call (Sent to the party the call is redirected toward) */
	struct misdn_party_id from;

	/*! \brief Where the call is being redirected toward (Sent to the calling party) */
	struct misdn_party_id to;

	/*! \brief Reason a call is being redirected (Q.931 field value) */
	enum mISDN_REDIRECTING_REASON reason;

	/*! \brief Number of times the call has been redirected */
	int count;

	/*! \brief TRUE if the redirecting.to information has changed */
	int to_changed;
};


/*! \brief B channel control structure */
struct misdn_bchannel {
	/*! \brief B channel send locking structure */
	struct send_lock *send_lock;

#if defined(AST_MISDN_ENHANCEMENTS)
	/*! \brief The BC, HLC (optional) and LLC (optional) contents from the SETUP message. */
	struct Q931_Bc_Hlc_Llc setup_bc_hlc_llc;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	/*!
	 * \brief Dialed/Called information struct
	 * \note The number_type element is set to "dialplan" in /etc/asterisk/misdn.conf for outgoing calls
	 */
	struct misdn_party_dialing dialed;

	/*! \brief Originating/Caller ID information struct
	 * \note The number_type element can be set to "localdialplan" in /etc/asterisk/misdn.conf for outgoing calls
	 * \note The number element can be set to "callerid" in /etc/asterisk/misdn.conf for outgoing calls
	 */
	struct misdn_party_id caller;

	/*! \brief Connected-Party/Connected-Line ID information struct
	 * \note The number_type element can be set to "cpndialplan" in /etc/asterisk/misdn.conf for outgoing calls
	 */
	struct misdn_party_id connected;

	/*! \brief Redirecting information struct (Where a call diversion or transfer was invoked)
	 * \note The redirecting subaddress is not defined in Q.931 so it is not used.
	 */
	struct misdn_party_redirecting redirecting;

	/*! \brief TRUE if this is a dummy BC record */
	int dummy;

	/*! \brief TRUE if NT side of protocol (TE otherwise) */
	int nt;

	/*! \brief TRUE if ISDN-PRI (ISDN-BRI otherwise) */
	int pri;

	/*! \brief Logical Layer 1 port associated with this B channel */
	int port;

	/** init stuff **/
	/*! \brief B Channel mISDN driver stack ID */
	int b_stid;

	/* int b_addr; */

	/*! \brief B Channel mISDN driver layer ID from mISDN_new_layer() */
	int layer_id;

	/*! \brief B channel layer; set to 3 or 4 */
	int layer;

	/* state stuff */
	/*! \brief TRUE if DISCONNECT needs to be sent to clear a call */
	int need_disconnect;

	/*! \brief TRUE if RELEASE needs to be sent to clear a call */
	int need_release;

	/*! \brief TRUE if RELEASE_COMPLETE needs to be sent to clear a call */
	int need_release_complete;

	/*! \brief TRUE if allocate higher B channels first */
	int dec;

	/* var stuff */
	/*! \brief Layer 3 process ID */
	int l3_id;

	/*! \brief B channel process ID (1-5000) */
	int pid;

	/*! \brief Not used. Saved mISDN stack CONNECT_t ces value */
	int ces;

	/*! \brief B channel to restart if received a RESTART message */
	int restart_channel;

	/*! \brief Assigned B channel number B1, B2... 0 if not assigned */
	int channel;

	/*! \brief TRUE if the B channel number is preselected */
	int channel_preselected;

	/*! \brief TRUE if the B channel is allocated from the REGISTER pool */
	int is_register_pool;

	/*! \brief TRUE if B channel record is in use */
	int in_use;

	/*! \brief Time when empty_bc() last called on this record */
	struct timeval last_used;

	/*! \brief TRUE if call waiting */
	int cw;

	/*! \brief B Channel mISDN driver layer ID from mISDN_get_layerid() */
	int addr;

	/*! \brief B channel speech sample data buffer */
	char *bframe;

	/*! \brief B channel speech sample data buffer size */
	int bframe_len;
	int time_usec;	/* Not used */

	/*! \brief Not used. Contents are setup but not used. */
	void *astbuf;

	/*! \brief TRUE if the TE side should choose the B channel to use
	 * \note This value is user configurable in /etc/asterisk/misdn.conf
	 */
	int te_choose_channel;

	/*! \brief TRUE if the call progress indicators can indicate an inband audio message for the user to listen to
	 * \note This value is user configurable in /etc/asterisk/misdn.conf
	 */
	int early_bconnect;

	/*! \brief Last decoded DTMF digit from mISDN driver */
	int dtmf;

	/*! \brief TRUE if we should produce DTMF tones ourselves
	 * \note This value is user configurable in /etc/asterisk/misdn.conf
	 */
	int send_dtmf;

	/*! \brief TRUE if we send SETUP_ACKNOWLEDGE on incoming calls anyway (instead of PROCEEDING).
	 *
	 * This requests additional INFORMATION messages, so we can
	 * wait for digits without issues.
	 * \note This value is user configurable in /etc/asterisk/misdn.conf
	 */
	int need_more_infos;

	/*! \brief TRUE if all digits necessary to complete the call are available.
	 * No more INFORMATION messages are needed.
	 */
	int sending_complete;


	/*! \brief TRUE if we will not use jollys dsp */
	int nodsp;

	/*! \brief TRUE if we will not use the jitter buffer system */
	int nojitter;

	/*! \brief Progress Indicator IE coding standard field.
	 * \note Collected from the incoming messages but not used.
	 */
	int progress_coding;

	/*! \brief Progress Indicator IE location field.
	 * \note Collected from the incoming messages but not used.
	 */
	int progress_location;

	/*! \brief Progress Indicator IE progress description field.
	 * Used to determine if there is an inband audio message present.
	 */
	int progress_indicator;

#if defined(AST_MISDN_ENHANCEMENTS)
	/*!
	 * \brief TRUE if waiting for DivertingLegInformation3 to queue redirecting update.
	 */
	int div_leg_3_rx_wanted;

	/*!
	 * \brief TRUE if a DivertingLegInformation3 needs to be sent with CONNECT.
	 */
	int div_leg_3_tx_pending;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	/*! \brief Inbound FACILITY message function type and contents */
	struct FacParm fac_in;

	/*! \brief Outbound FACILITY message function type and contents.
	 * \note Filled in by misdn facility commands before FACILITY message sent.
	 */
	struct FacParm fac_out;

	/* storing the current AOCD info here */
	enum FacFunction AOCDtype;
	union {
		struct FacAOCDCurrency currency;
		struct FacAOCDChargingUnit chargingUnit;
	} AOCD;
	/*! \brief TRUE if AOCDtype and AOCD data are ready to export to Asterisk */
	int AOCD_need_export;

	/*! \brief Event waiting for Layer 1 to come up */
	enum event_e evq;

	/*** CRYPTING STUFF ***/
	int crypt;		/* Initialized, Not used */
	int curprx;		/* Initialized, Not used */
	int curptx;		/* Initialized, Not used */

	/*! \brief Blowfish encryption key string (secret) */
	char crypt_key[255];

	int crypt_state;	/* Not used */
	/*** CRYPTING STUFF END***/

	/*! \brief Seems to have been intended for something to do with the jitter buffer.
	 * \note Used as a boolean.  Only initialized to 0 and referenced in a couple places
	 */
	int active;
	int upset;	/* Not used */

	/*! \brief TRUE if tone generator allowed to start */
	int generate_tone;

	/*! \brief Number of tone samples to generate */
	int tone_cnt;

	/*! \brief Current B Channel state */
	enum bchannel_state bc_state;

	/*! \brief This is used as a pending bridge join request for when bc_state becomes BCHAN_ACTIVATED */
	enum bchannel_state next_bc_state;

	/*! \brief Bridging conference ID */
	int conf_id;

	/*! \brief TRUE if this channel is on hold */
	int holded;

	/*! \brief TRUE if this channel is on the misdn_stack->holding list
	 * \note If TRUE this implies that the structure is also malloced.
	 */
	int stack_holder;

	/*!
	 * \brief Put a display ie in the CONNECT message
	 * \details
	 * Put a display ie in the CONNECT message containing the following
	 * information if it is available (nt port only):
	 * 0 - Do not put the connected line information in the display ie.
	 * 1 - Put the available connected line name in the display ie.
	 * 2 - Put the available connected line number in the display ie.
	 * 3 - Put the available connected line name and number in the display ie.
	 */
	int display_connected;

	/*!
	 * \brief Put a display ie in the SETUP message
	 * \details
	 * Put a display ie in the SETUP message containing the following
	 * information if it is available (nt port only):
	 * 0 - Do not put the caller information in the display ie.
	 * 1 - Put the available caller name in the display ie.
	 * 2 - Put the available caller number in the display ie.
	 * 3 - Put the available caller name and number in the display ie.
	 */
	int display_setup;

	/*!
	 * \brief Select what to do with outgoing COLP information.
	 * \details
	 * 0 - pass (Send out COLP information unaltered.)
	 * 1 - restricted (Force COLP to restricted on all outgoing COLP information.)
	 * 2 - block (Do not send COLP information.)
	 */
	int outgoing_colp;

	/*! \brief User set presentation restriction code
	 * 0=Allowed, 1=Restricted, 2=Unavailable
	 * \note It is settable by the misdn_set_opt() application.
	 */
	int presentation;

	/*! \brief TRUE if the user set the presentation restriction code */
	int set_presentation;

	/*! \brief Notification indicator ie description code */
	enum mISDN_NOTIFY_CODE notify_description_code;

	/*! \brief SETUP message bearer capability field code value */
	int capability;

	/*! \brief Companding ALaw/uLaw encoding (INFO_CODEC_ALAW / INFO_CODEC_ULAW) */
	int law;

	/* V110 Stuff */
	/*! \brief Q.931 Bearer Capability IE Information Transfer Rate field. Initialized to 0x10 (64kbit). Altered by incoming SETUP messages. */
	int rate;

	/*! \brief Q.931 Bearer Capability IE Transfer Mode field. Initialized to 0 (Circuit). Altered by incoming SETUP messages. */
	int mode;

	/*! \brief Q.931 Bearer Capability IE User Information Layer 1 Protocol field code.
	 * \note Collected from the incoming SETUP message but not used.
	 */
	int user1;

	/*! \brief Q.931 Bearer Capability IE Layer 1 User Rate field.
	 * \note Collected from the incoming SETUP message and exported to Asterisk variable MISDN_URATE.
	 */
	int urate;

	/*! \brief TRUE if call made in digital HDLC mode
	 * \note This value is user configurable in /etc/asterisk/misdn.conf.
	 * It is also settable by the misdn_set_opt() application.
	 */
	int hdlc;
	/* V110 */

	/*! \brief Display message that can be displayed by the user phone.
	 * \note Maximum displayable length is 34 or 82 octets.
	 * It is also settable by the misdn_set_opt() application.
	 */
	char display[84];

	/*! \brief Q.931 Keypad Facility IE contents
	 * \note Contents exported and imported to Asterisk variable MISDN_KEYPAD
	 */
	char keypad[MISDN_MAX_KEYPAD_LEN];

	/*! \brief Current overlap dialing digits to/from INFORMATION messages */
	char info_dad[MISDN_MAX_NUMBER_LEN];

	/*! \brief Collected digits to go into info_dad[] while waiting for a SETUP_ACKNOWLEDGE to come in. */
	char infos_pending[MISDN_MAX_NUMBER_LEN];

/* 	unsigned char info_keypad[MISDN_MAX_KEYPAD_LEN]; */
/* 	unsigned char clisub[24]; */
/* 	unsigned char cldsub[24]; */

	/*! \brief User-User information string.
	 * \note Contents exported and imported to Asterisk variable MISDN_USERUSER
	 * \note We only support ASCII strings (IA5 characters).
	 */
 	char uu[256];

	/*! \brief User-User information string length in uu[] */
	int uulen;

	/*! \brief Q.931 Cause for disconnection code (received)
	 * \note Need to use the AST_CAUSE_xxx code definitions in causes.h
	 */
	int cause;

	/*! \brief Q.931 Cause for disconnection code (sent)
	 * \note Need to use the AST_CAUSE_xxx code definitions in causes.h
	 * \note -1 is used to suppress including the cause code in the RELEASE message.
	 */
	int out_cause;

	/* struct misdn_bchannel hold_bc; */

	/** list stuf **/

#ifdef MISDN_1_2
	/*! \brief The configuration string for the mISDN dsp pipeline in /etc/asterisk/misdn.conf. */
	char pipeline[128];
#else
	/*! \brief TRUE if the echo cancellor is enabled */
	int ec_enable;

	/*! \brief Number of taps in the echo cancellor when enabled.
	 * \note This value is user configurable in /etc/asterisk/misdn.conf (echocancel)
	 */
	int ec_deftaps;
#endif

	/*! \brief TRUE if the channel was allocated from the available B channels */
	int channel_found;

	/*! \brief Who originated the call (ORG_AST, ORG_MISDN)
	 * \note Set but not used when the misdn_set_opt() application enables echo cancellation.
	 */
	int orig;

	/*! \brief Tx gain setting (range -8 to 8)
	 * \note This value is user configurable in /etc/asterisk/misdn.conf.
	 * It is also settable by the misdn_set_opt() application.
	 */
	int txgain;

	/*! \brief Rx gain setting (range -8 to 8)
	 * \note This value is user configurable in /etc/asterisk/misdn.conf.
	 * It is also settable by the misdn_set_opt() application.
	 */
	int rxgain;

	/*! \brief Next node in the misdn_stack.holding list */
	struct misdn_bchannel *next;
};


extern enum event_response_e (*cb_event) (enum event_e event, struct misdn_bchannel *bc, void *user_data);

extern void (*cb_log) (int level, int port, char *tmpl, ...)
	__attribute__ ((format (printf, 3, 4)));

extern int (*cb_jb_empty)(struct misdn_bchannel *bc, char *buffer, int len);

struct misdn_lib_iface {
	enum event_response_e (*cb_event)(enum event_e event, struct misdn_bchannel *bc, void *user_data);
	void (*cb_log)(int level, int port, char *tmpl, ...)
		__attribute__ ((format (printf, 3, 4)));
	int (*cb_jb_empty)(struct misdn_bchannel *bc, char *buffer, int len);
};

/***** USER IFACE **********/

void misdn_lib_nt_keepcalls(int kc);

void misdn_lib_nt_debug_init( int flags, char *file );

int misdn_lib_init(char *portlist, struct misdn_lib_iface* iface, void *user_data);
int misdn_lib_send_event(struct misdn_bchannel *bc, enum event_e event );
void misdn_lib_destroy(void);

void misdn_lib_isdn_l1watcher(int port);

void misdn_lib_log_ies(struct misdn_bchannel *bc);

char *manager_isdn_get_info(enum event_e event);

void misdn_lib_transfer(struct misdn_bchannel* holded_bc);

struct misdn_bchannel* misdn_lib_get_free_bc(int port, int channel, int inout, int dec);
#if defined(AST_MISDN_ENHANCEMENTS)
struct misdn_bchannel *misdn_lib_get_register_bc(int port);
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

void manager_bchannel_activate(struct misdn_bchannel *bc);
void manager_bchannel_deactivate(struct misdn_bchannel * bc);

int misdn_lib_tx2misdn_frm(struct misdn_bchannel *bc, void *data, int len);

void manager_ph_control(struct misdn_bchannel *bc, int c1, int c2);

void isdn_lib_update_rxgain (struct misdn_bchannel *bc);
void isdn_lib_update_txgain (struct misdn_bchannel *bc);
void isdn_lib_update_ec (struct misdn_bchannel *bc);
void isdn_lib_stop_dtmf (struct misdn_bchannel *bc);

int misdn_lib_port_restart(int port);
int misdn_lib_pid_restart(int pid);
int misdn_lib_send_restart(int port, int channel);

int misdn_lib_get_port_info(int port);

int misdn_lib_is_port_blocked(int port);
int misdn_lib_port_block(int port);
int misdn_lib_port_unblock(int port);

int misdn_lib_port_is_pri(int port);
int misdn_lib_port_is_nt(int port);

int misdn_lib_port_up(int port, int notcheck);

int misdn_lib_get_port_down(int port);

int misdn_lib_get_port_up (int port) ;

int misdn_lib_maxports_get(void) ;

void misdn_lib_release(struct misdn_bchannel *bc);

int misdn_cap_is_speech(int cap);
int misdn_inband_avail(struct misdn_bchannel *bc);

void manager_ec_enable(struct misdn_bchannel *bc);
void manager_ec_disable(struct misdn_bchannel *bc);

void misdn_lib_send_tone(struct misdn_bchannel *bc, enum tone_e tone);

void get_show_stack_details(int port, char *buf);


void misdn_lib_tone_generator_start(struct misdn_bchannel *bc);
void misdn_lib_tone_generator_stop(struct misdn_bchannel *bc);


void misdn_lib_setup_bc(struct misdn_bchannel *bc);

void misdn_lib_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2);
void misdn_lib_split_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2);

void misdn_lib_echo(struct misdn_bchannel *bc, int onoff);

int misdn_lib_is_ptp(int port);
int misdn_lib_get_maxchans(int port);

void misdn_lib_reinit_nt_stack(int port);

#define PRI_TRANS_CAP_SPEECH                                    0x0
#define PRI_TRANS_CAP_DIGITAL                                   0x08
#define PRI_TRANS_CAP_RESTRICTED_DIGITAL                        0x09
#define PRI_TRANS_CAP_3_1K_AUDIO                                0x10
#define PRI_TRANS_CAP_7K_AUDIO                                  0x11



char *bc_state2str(enum bchannel_state state);
void bc_state_change(struct misdn_bchannel *bc, enum bchannel_state state);

void misdn_dump_chanlist(void);

void misdn_make_dummy(struct misdn_bchannel *dummybc, int port, int l3id, int nt, int channel);


#endif	/* TE_LIB */
