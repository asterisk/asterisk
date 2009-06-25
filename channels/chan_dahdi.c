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
 *
 * \brief DAHDI for Pseudo TDM
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * Connects to the DAHDI telephony library as well as
 * libpri. Libpri is optional and needed only if you are
 * going to use ISDN connections.
 *
 * You need to install libraries before you attempt to compile
 * and install the DAHDI channel.
 *
 * \par See also
 * \arg \ref Config_dahdi
 *
 * \ingroup channel_drivers
 *
 * \todo Deprecate the "musiconhold" configuration option post 1.4
 */

/*** MODULEINFO
	<depend>res_smdi</depend>
	<depend>dahdi</depend>
	<depend>tonezone</depend>
	<use>pri</use>
	<use>ss7</use>
	<use>openr2</use>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#ifdef __NetBSD__
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <sys/ioctl.h>
#include <math.h>
#include <ctype.h>

#include <dahdi/user.h>
#include <dahdi/tonezone.h>
#include "sig_analog.h"

#ifdef HAVE_PRI
#include "sig_pri.h"
#endif

#ifdef HAVE_SS7
#include <libss7.h>
#endif

#ifdef HAVE_OPENR2
#include <openr2.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"
#include "asterisk/adsi.h"
#include "asterisk/cli.h"
#include "asterisk/cdr.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/tdd.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/astdb.h"
#include "asterisk/manager.h"
#include "asterisk/causes.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"
#include "asterisk/transcap.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/smdi.h"
#include "asterisk/astobj.h"
#include "asterisk/event.h"
#include "asterisk/devicestate.h"
#include "asterisk/paths.h"

/*** DOCUMENTATION
	<application name="DAHDISendKeypadFacility" language="en_US">
		<synopsis>
			Send digits out of band over a PRI.
		</synopsis>
		<syntax>
			<parameter name="digits" required="true" />
		</syntax>
		<description>
			<para>This application will send the given string of digits in a Keypad
			Facility IE over the current channel.</para>
		</description>
	</application>
	<application name="DAHDISendCallreroutingFacility" language="en_US">
		<synopsis>
			Send QSIG call rerouting facility over a PRI.
		</synopsis>
		<syntax argsep=",">
			<parameter name="destination" required="true">
				<para>Destination number.</para>
			</parameter>
			<parameter name="original">
				<para>Original called number.</para>
			</parameter>
			<parameter name="reason">
				<para>Diversion reason, if not specified defaults to <literal>unknown</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>This application will send a Callrerouting Facility IE over the
			current channel.</para>
		</description>
	</application>
	<application name="DAHDIAcceptR2Call" language="en_US">
		<synopsis>
			Accept an R2 call if its not already accepted (you still need to answer it)
		</synopsis>
		<syntax>
			<parameter name="charge" required="true">
				<para>Yes or No.</para>
				<para>Whether you want to accept the call with charge or without charge.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will Accept the R2 call either with charge or no charge.</para>
		</description>
	</application>
	<manager name="DAHDITransfer" language="en_US">
		<synopsis>
			Transfer DAHDI Channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true">
				<para>DAHDI channel name to transfer.</para>
			</parameter>
		</syntax>
		<description>
			<para>Transfer a DAHDI channel.</para>
		</description>
	</manager>
	<manager name="DAHDIHangup" language="en_US">
		<synopsis>
			Hangup DAHDI Channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true">
				<para>DAHDI channel name to hangup.</para>
			</parameter>
		</syntax>
		<description>
			<para>Hangup a DAHDI channel.</para>
		</description>
	</manager>
	<manager name="DAHDIDialOffhook" language="en_US">
		<synopsis>
			Dial over DAHDI channel while offhook.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true" />
			<parameter name="Number" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DAHDIDNDon" language="en_US">
		<synopsis>
			Toggle DAHDI channel Do Not Disturb status ON.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DAHDIDNDoff" language="en_US">
		<synopsis>
			Toggle DAHDI channel Do Not Disturb status OFF.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DAHDIShowChannels" language="en_US">
		<synopsis>
			Show status DAHDI channels.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DAHDIRestart" language="en_US">
		<synopsis>
			Fully Restart DAHDI channels (terminates calls).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
		</description>
	</manager>
 ***/

#define SMDI_MD_WAIT_TIMEOUT 1500 /* 1.5 seconds */

static const char * const lbostr[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
};

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

/* define this to send PRI user-user information elements */
#undef SUPPORT_USERUSER

/*!
 * \note Define ZHONE_HACK to cause us to go off hook and then back on hook when
 * the user hangs up to reset the state machine so ring works properly.
 * This is used to be able to support kewlstart by putting the zhone in
 * groundstart mode since their forward disconnect supervision is entirely
 * broken even though their documentation says it isn't and their support
 * is entirely unwilling to provide any assistance with their channel banks
 * even though their web site says they support their products for life.
 */
/* #define ZHONE_HACK */

/*! \note
 * Define if you want to check the hook state for an FXO (FXS signalled) interface
 * before dialing on it.  Certain FXO interfaces always think they're out of
 * service with this method however.
 */
/* #define DAHDI_CHECK_HOOKSTATE */

/*! \brief Typically, how many rings before we should send Caller*ID */
#define DEFAULT_CIDRINGS 1

#define CHANNEL_PSEUDO -12

#define AST_LAW(p) (((p)->law == DAHDI_LAW_ALAW) ? AST_FORMAT_ALAW : AST_FORMAT_ULAW)


/*! \brief Signaling types that need to use MF detection should be placed in this macro */
#define NEED_MFDETECT(p) (((p)->sig == SIG_FEATDMF) || ((p)->sig == SIG_FEATDMF_TA) || ((p)->sig == SIG_E911) || ((p)->sig == SIG_FGC_CAMA) || ((p)->sig == SIG_FGC_CAMAMF) || ((p)->sig == SIG_FEATB))

static const char tdesc[] = "DAHDI Telephony Driver"
#if defined(HAVE_PRI) || defined(HAVE_SS7) || defined(HAVE_OPENR2)
	" w/"
#endif
#ifdef HAVE_PRI
	"PRI"
#endif
#ifdef HAVE_SS7
	#ifdef HAVE_PRI
	" & SS7"
	#else
	"SS7"
	#endif
#endif
#ifdef HAVE_OPENR2
	#if defined(HAVE_PRI) || defined(HAVE_SS7)
	" & MFC/R2"
	#else
	"MFC/R2"
	#endif
#endif
;

static const char config[] = "chan_dahdi.conf";

#define SIG_EM		DAHDI_SIG_EM
#define SIG_EMWINK 	(0x0100000 | DAHDI_SIG_EM)
#define SIG_FEATD	(0x0200000 | DAHDI_SIG_EM)
#define	SIG_FEATDMF	(0x0400000 | DAHDI_SIG_EM)
#define	SIG_FEATB	(0x0800000 | DAHDI_SIG_EM)
#define	SIG_E911	(0x1000000 | DAHDI_SIG_EM)
#define	SIG_FEATDMF_TA	(0x2000000 | DAHDI_SIG_EM)
#define	SIG_FGC_CAMA	(0x4000000 | DAHDI_SIG_EM)
#define	SIG_FGC_CAMAMF	(0x8000000 | DAHDI_SIG_EM)
#define SIG_FXSLS	DAHDI_SIG_FXSLS
#define SIG_FXSGS	DAHDI_SIG_FXSGS
#define SIG_FXSKS	DAHDI_SIG_FXSKS
#define SIG_FXOLS	DAHDI_SIG_FXOLS
#define SIG_FXOGS	DAHDI_SIG_FXOGS
#define SIG_FXOKS	DAHDI_SIG_FXOKS
#define SIG_PRI		DAHDI_SIG_CLEAR
#define SIG_BRI		(0x2000000 | DAHDI_SIG_CLEAR)
#define SIG_BRI_PTMP	(0X4000000 | DAHDI_SIG_CLEAR)
#define SIG_SS7		(0x1000000 | DAHDI_SIG_CLEAR)
#define SIG_MFCR2 	DAHDI_SIG_CAS
#define	SIG_SF		DAHDI_SIG_SF
#define SIG_SFWINK 	(0x0100000 | DAHDI_SIG_SF)
#define SIG_SF_FEATD	(0x0200000 | DAHDI_SIG_SF)
#define	SIG_SF_FEATDMF	(0x0400000 | DAHDI_SIG_SF)
#define	SIG_SF_FEATB	(0x0800000 | DAHDI_SIG_SF)
#define SIG_EM_E1	DAHDI_SIG_EM_E1

#ifdef LOTS_OF_SPANS
#define NUM_SPANS	DAHDI_MAX_SPANS
#else
#define NUM_SPANS 		32
#endif

#define CHAN_PSEUDO	-2

#define CALLPROGRESS_PROGRESS		1
#define CALLPROGRESS_FAX_OUTGOING	2
#define CALLPROGRESS_FAX_INCOMING	4
#define CALLPROGRESS_FAX		(CALLPROGRESS_FAX_INCOMING | CALLPROGRESS_FAX_OUTGOING)

static char defaultcic[64] = "";
static char defaultozz[64] = "";

static char parkinglot[AST_MAX_EXTENSION] = "";		/*!< Default parking lot for this channel */

/*! Run this script when the MWI state changes on an FXO line, if mwimonitor is enabled */
static char mwimonitornotify[PATH_MAX] = "";
#ifndef HAVE_DAHDI_LINEREVERSE_VMWI
static int  mwisend_rpas = 0;
#endif

static char progzone[10] = "";

static int usedistinctiveringdetection = 0;
static int distinctiveringaftercid = 0;

static int numbufs = 4;

static int mwilevel = 512;

#ifdef HAVE_PRI
#ifdef PRI_GETSET_TIMERS
static int pritimers[PRI_MAX_TIMERS];
#endif
static int pridebugfd = -1;
static char pridebugfilename[1024] = "";
#endif

/*! \brief Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/*! \brief How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

/*! \brief How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

/*! \brief Protect the interface list (of dahdi_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* QSIG channel mapping option types */
#define DAHDI_CHAN_MAPPING_PHYSICAL	0
#define DAHDI_CHAN_MAPPING_LOGICAL	1


static int ifcount = 0;

#ifdef HAVE_PRI
AST_MUTEX_DEFINE_STATIC(pridebugfdlock);
#endif

/*! \brief Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/*! \brief This is the thread for the monitor which checks for input on the channels
   which are not currently in use. */
static pthread_t monitor_thread = AST_PTHREADT_NULL;
static ast_cond_t ss_thread_complete;
AST_MUTEX_DEFINE_STATIC(ss_thread_lock);
AST_MUTEX_DEFINE_STATIC(restart_lock);
static int ss_thread_count = 0;
static int num_restart_pending = 0;

static int restart_monitor(void);

static enum ast_bridge_result dahdi_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms);

static int dahdi_sendtext(struct ast_channel *c, const char *text);

static int analog_lib_handles(int signalling, int radio, int oprmode);

static void mwi_event_cb(const struct ast_event *event, void *userdata)
{
	/* This module does not handle MWI in an event-based manner.  However, it
	 * subscribes to MWI for each mailbox that is configured so that the core
	 * knows that we care about it.  Then, chan_dahdi will get the MWI from the
	 * event cache instead of checking the mailbox directly. */
}

/*! \brief Avoid the silly dahdi_getevent which ignores a bunch of events */
static inline int dahdi_get_event(int fd)
{
	int j;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1)
		return -1;
	return j;
}

/*! \brief Avoid the silly dahdi_waitevent which ignores a bunch of events */
static inline int dahdi_wait_event(int fd)
{
	int i, j = 0;
	i = DAHDI_IOMUX_SIGEVENT;
	if (ioctl(fd, DAHDI_IOMUX, &i) == -1)
		return -1;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1)
		return -1;
	return j;
}

/*! Chunk size to read -- we use 20ms chunks to make things happy. */
#define READ_SIZE 160

#define MASK_AVAIL		(1 << 0)	/*!< Channel available for PRI use */
#define MASK_INUSE		(1 << 1)	/*!< Channel currently in use */

#define CALLWAITING_SILENT_SAMPLES	( (300 * 8) / READ_SIZE) /*!< 300 ms */
#define CALLWAITING_REPEAT_SAMPLES	( (10000 * 8) / READ_SIZE) /*!< 10,000 ms */
#define CIDCW_EXPIRE_SAMPLES		( (500 * 8) / READ_SIZE) /*!< 500 ms */
#define MIN_MS_SINCE_FLASH			( (2000) )	/*!< 2000 ms */
#define DEFAULT_RINGT 				( (8000 * 8) / READ_SIZE) /*!< 8,000 ms */

struct dahdi_pvt;

/*!
 * \brief Configured ring timeout base.
 * \note Value computed from "ringtimeout" read in from chan_dahdi.conf if it exists.
 */
static int ringt_base = DEFAULT_RINGT;

#ifdef HAVE_SS7

#define LINKSTATE_INALARM	(1 << 0)
#define LINKSTATE_STARTING	(1 << 1)
#define LINKSTATE_UP		(1 << 2)
#define LINKSTATE_DOWN		(1 << 3)

#define SS7_NAI_DYNAMIC		-1

#define LINKSET_FLAG_EXPLICITACM (1 << 0)

struct dahdi_ss7 {
	pthread_t master;						/*!< Thread of master */
	ast_mutex_t lock;
	int fds[NUM_DCHANS];
	int numsigchans;
	int linkstate[NUM_DCHANS];
	int numchans;
	int type;
	enum {
		LINKSET_STATE_DOWN = 0,
		LINKSET_STATE_UP
	} state;
	char called_nai;						/*!< Called Nature of Address Indicator */
	char calling_nai;						/*!< Calling Nature of Address Indicator */
	char internationalprefix[10];					/*!< country access code ('00' for european dialplans) */
	char nationalprefix[10];					/*!< area access code ('0' for european dialplans) */
	char subscriberprefix[20];					/*!< area access code + area code ('0'+area code for european dialplans) */
	char unknownprefix[20];						/*!< for unknown dialplans */
	struct ss7 *ss7;
	struct dahdi_pvt *pvts[MAX_CHANNELS];				/*!< Member channel pvt structs */
	int flags;							/*!< Linkset flags */
};

static struct dahdi_ss7 linksets[NUM_SPANS];

static int cur_ss7type = -1;
static int cur_linkset = -1;
static int cur_pointcode = -1;
static int cur_cicbeginswith = -1;
static int cur_adjpointcode = -1;
static int cur_networkindicator = -1;
static int cur_defaultdpc = -1;
#endif /* HAVE_SS7 */

#ifdef HAVE_OPENR2
struct dahdi_mfcr2 {
	pthread_t r2master;		       /*!< Thread of master */
	openr2_context_t *protocol_context;    /*!< OpenR2 context handle */
	struct dahdi_pvt *pvts[MAX_CHANNELS];     /*!< Member channel pvt structs */
	int numchans;                          /*!< Number of channels in this R2 block */
	int monitored_count;                   /*!< Number of channels being monitored */
	ast_mutex_t monitored_count_lock;      /*!< lock access to the counter */
	ast_cond_t do_monitor;                 /*!< Condition to wake up the monitor thread when there's work to do */

};
struct dahdi_mfcr2_conf {
	openr2_variant_t variant;
	int mfback_timeout;
	int metering_pulse_timeout;
	int max_ani;
	int max_dnis;
	int get_ani_first:1;
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
	int skip_category_request:1;
#endif
	int call_files:1;
	int allow_collect_calls:1;
	int charge_calls:1;
	int accept_on_offer:1;
	int forced_release:1;
	int double_answer:1;
	int immediate_accept:1;
	char logdir[OR2_MAX_PATH];
	char r2proto_file[OR2_MAX_PATH];
	openr2_log_level_t loglevel;
	openr2_calling_party_category_t category;
};

/* malloc'd array of malloc'd r2links */
static struct dahdi_mfcr2 **r2links;
/* how many r2links have been malloc'd */
static int r2links_count = 0;

#endif /* HAVE_OPENR2 */

#ifdef HAVE_PRI

struct dahdi_pri {
	int dchannels[NUM_DCHANS];					/*!< What channel are the dchannels on */
	int mastertrunkgroup;						/*!< What trunk group is our master */
	int prilogicalspan;						/*!< Logical span number within trunk group */
	struct sig_pri_pri pri;
};

static struct dahdi_pri pris[NUM_SPANS];

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
#endif

#else
/*! Shut up the compiler */
struct dahdi_pri;
#endif

#define SUB_REAL	0			/*!< Active call */
#define SUB_CALLWAIT	1			/*!< Call-Waiting call on hold */
#define SUB_THREEWAY	2			/*!< Three-way call */

/* Polarity states */
#define POLARITY_IDLE   0
#define POLARITY_REV    1


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

static const char * const subnames[] = {
	"Real",
	"Callwait",
	"Threeway"
};

struct dahdi_subchannel {
	int dfd;
	struct ast_channel *owner;
	int chan;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct ast_frame f;		/*!< One frame for each channel.  How did this ever work before? */
	unsigned int needringing:1;
	unsigned int needbusy:1;
	unsigned int needcongestion:1;
	unsigned int needcallerid:1;
	unsigned int needanswer:1;
	unsigned int needflash:1;
	unsigned int needhold:1;
	unsigned int needunhold:1;
	unsigned int linear:1;
	unsigned int inthreeway:1;
	struct dahdi_confinfo curconf;
};

#define CONF_USER_REAL		(1 << 0)
#define CONF_USER_THIRDCALL	(1 << 1)

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
	struct	timeval	pause;
	mwisend_states 	mwisend_current;
};

static struct dahdi_pvt {
	ast_mutex_t lock;
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
	/*! \brief Amount of gain to increase during caller id */
	float cid_rxgain;
	/*! \brief Rx gain set by chan_dahdi.conf */
	float rxgain;
	/*! \brief Tx gain set by chan_dahdi.conf */
	float txgain;
	int tonezone;					/*!< tone zone for this chan, or -1 for default */
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
	/*! \brief TRUE if in the process of dialing digits or sending something. */
	unsigned int dialing:1;
	/*! \brief TRUE if the transfer capability of the call is digital. */
	unsigned int digital:1;
	/*! \brief TRUE if Do-Not-Disturb is enabled. */
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
	/* unsigned int overlapdial:1; 			unused and potentially confusing */
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
	 * \note Set but not used.
	 */
	unsigned int inservice:1;
	/*!
	 * \brief TRUE if the channel is locally blocked.
	 * \note Applies to SS7 channels.
	 */
	unsigned int locallyblocked:1;
	/*!
	 * \brief TRUE if the channel is remotely blocked.
	 * \note Applies to SS7 channels.
	 */
	unsigned int remotelyblocked:1;
#if defined(HAVE_PRI)
	struct sig_pri_pri *pri;
	int prioffset;
	int logicalspan;
#endif
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	/*!
	 * \brief XXX BOOLEAN Purpose???
	 * \note Applies to SS7 channels.
	 */
	unsigned int rlt:1;
	/*! \brief TRUE if channel is alerting/ringing */
	unsigned int alerting:1;
	/*! \brief TRUE if the call has already gone/hungup */
	unsigned int alreadyhungup:1;
	/*!
	 * \brief TRUE if this is an idle call
	 * \note Applies to PRI channels.
	 */
	unsigned int isidlecall:1;
	/*!
	 * \brief TRUE if call is in a proceeding state.
	 * The call has started working its way through the network.
	 */
	unsigned int proceeding:1;
	/*! \brief TRUE if the call has seen progress through the network. */
	unsigned int progress:1;
	/*!
	 * \brief TRUE if this channel is being reset/restarted
	 * \note Applies to PRI channels.
	 */
	unsigned int resetting:1;
	/*!
	 * \brief TRUE if this channel has received a SETUP_ACKNOWLEDGE
	 * \note Applies to PRI channels.
	 */
	unsigned int setup_ack:1;
#endif
	/*!
	 * \brief TRUE if SMDI (Simplified Message Desk Interface) is enabled
	 * \note Set from the "usesmdi" value read in from chan_dahdi.conf
	 */
	unsigned int use_smdi:1;
	struct mwisend_info mwisend_data;
	/*! \brief The serial port to listen for SMDI data on */
	struct ast_smdi_interface *smdi_iface;

	/*! \brief Distinctive Ring data */
	struct dahdi_distRings drings;

	/*!
	 * \brief The configured context for incoming calls.
	 * \note The "context" string read in from chan_dahdi.conf
	 */
	char context[AST_MAX_CONTEXT];
	/*!
	 * \brief Saved context string.
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
	 * \brief Sugggested music-on-hold class for peer channel to use for calls.
	 * \note The "mohsuggest" string read in from chan_dahdi.conf
	 */
	char mohsuggest[MAX_MUSICCLASS];
	char parkinglot[AST_MAX_EXTENSION]; /*!< Parking lot for this channel */
#if defined(PRI_ANI) || defined(HAVE_SS7)
	/*! \brief Automatic Number Identification number (Alternate PRI caller ID number) */
	char cid_ani[AST_MAX_EXTENSION];
#endif
	/*! \brief Automatic Number Identification code from PRI */
	int cid_ani2;
	/*! \brief Caller ID number from an incoming call. */
	char cid_num[AST_MAX_EXTENSION];
	/*! \brief Caller ID Q.931 TON/NPI field values.  Set by PRI. Zero otherwise. */
	int cid_ton;
	/*! \brief Caller ID name from an incoming call. */
	char cid_name[AST_MAX_EXTENSION];
	/*! \brief Last Caller ID number from an incoming call. */
	char lastcid_num[AST_MAX_EXTENSION];
	/*! \brief Last Caller ID name from an incoming call. */
	char lastcid_name[AST_MAX_EXTENSION];
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
	 * \brief Channel variable list with associated values to set when a channel is created.
	 * \note The "setvar" strings read in from chan_dahdi.conf
	 */
	struct ast_variable *vars;
	int channel;					/*!< Channel Number */
	int span;					/*!< Span number */
	time_t guardtime;				/*!< Must wait this much time before using for new call */
	int cid_signalling;				/*!< CID signalling type bell202 or v23 */
	int cid_start;					/*!< CID start indicator, polarity or ring */
	int callingpres;				/*!< The value of calling presentation that we're going to use when placing a PRI call */
	int callwaitingrepeat;				/*!< How many samples to wait before repeating call waiting */
	int cidcwexpire;				/*!< When to expire our muting for CID/CW */
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
	/*! \brief BOOLEAN. XXX Meaning what?? */
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
	 * \brief Length of "busy" tone on time.
	 * \note Set from the "busypattern" value read in from chan_dahdi.conf
	 */
	int busy_tonelength;
	/*!
	 * \brief Length of "busy" tone off time.
	 * \note Set from the "busypattern" value read in from chan_dahdi.conf
	 */
	int busy_quietlength;
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
	char mailbox[AST_MAX_EXTENSION];
	/*! \brief Opaque event subscription parameters for message waiting indication support. */
	struct ast_event_sub *mwi_event_sub;
	/*! \brief Delayed dialing for E911.  Overlap digits for ISDN. */
	char dialdest[256];
	/*! \brief Time the interface went on-hook. */
	int onhooktime;
	/*! \brief TRUE if the FXS port is off-hook */
	int fxsoffhookstate;
	/*! \brief -1 = unknown, 0 = no messages, 1 = new messages available */
	int msgstate;
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
	struct dahdi_vmwi_info mwisend_setting;				/*!< Which VMWI methods to use */
	unsigned int mwisend_fsk: 1;		/*! Variable for enabling FSK MWI handling in chan_dahdi */
	unsigned int mwisend_rpas:1;		/*! Variable for enabling Ring Pulse Alert before MWI FSK Spill */
#endif
	int distinctivering;				/*!< Which distinctivering to use */
	int cidrings;					/*!< Which ring to deliver CID on */
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
	 * \brief Send caller ID after this many rings.
	 * \note Set from the "sendcalleridafter" value read in from chan_dahdi.conf
	 */
	int sendcalleridafter;
	/*! \brief Current line interface polarity. POLARITY_IDLE, POLARITY_REV */
	int polarity;
	/*! \brief DSP feature flags: DSP_FEATURE_xxx */
	int dsp_features;
#ifdef HAVE_SS7
	/*! \brief SS7 control parameters */
	struct dahdi_ss7 *ss7;
	/*! \brief Opaque libss7 call control structure */
	struct isup_call *ss7call;
	char charge_number[50];
	char gen_add_number[50];
	char gen_dig_number[50];
	char orig_called_num[50];
	char redirecting_num[50];
	char generic_name[50];
	unsigned char gen_add_num_plan;
	unsigned char gen_add_nai;
	unsigned char gen_add_pres_ind;
	unsigned char gen_add_type;
	unsigned char gen_dig_type;
	unsigned char gen_dig_scheme;
	char jip_number[50];
	unsigned char lspi_type;
	unsigned char lspi_scheme;
	unsigned char lspi_context;
	char lspi_ident[50];
	unsigned int call_ref_ident;
	unsigned int call_ref_pc;
	unsigned char calling_party_cat;
	int transcap;
	int cic;							/*!< CIC associated with channel */
	unsigned int dpc;						/*!< CIC's DPC */
	unsigned int loopedback:1;
#endif
#ifdef HAVE_OPENR2
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
#endif
	/*! \brief DTMF digit in progress.  0 when no digit in progress. */
	char begindigit;
	/*! \brief TRUE if confrence is muted. */
	int muting;
	void *sig_pvt;
} *iflist = NULL, *ifend = NULL;

/*! \brief Channel configuration from chan_dahdi.conf .
 * This struct is used for parsing the [channels] section of chan_dahdi.conf.
 * Generally there is a field here for every possible configuration item.
 *
 * The state of fields is saved along the parsing and whenever a 'channel'
 * statement is reached, the current dahdi_chan_conf is used to configure the
 * channel (struct dahdi_pvt)
 *
 * \see dahdi_chan_init for the default values.
 */
struct dahdi_chan_conf {
	struct dahdi_pvt chan;
#ifdef HAVE_PRI
	struct dahdi_pri pri;
#endif

#ifdef HAVE_SS7
	struct dahdi_ss7 ss7;
#endif

#ifdef HAVE_OPENR2
	struct dahdi_mfcr2_conf mfcr2;
#endif
	struct dahdi_params timing;
	int is_sig_auto; /*!< Use channel signalling from DAHDI? */

	/*!
	 * \brief The serial port to listen for SMDI data on
	 * \note Set from the "smdiport" string read in from chan_dahdi.conf
	 */
	char smdi_port[SMDI_MAX_FILENAME_LEN];
};

/*! returns a new dahdi_chan_conf with default values (by-value) */
static struct dahdi_chan_conf dahdi_chan_conf_default(void)
{
	/* recall that if a field is not included here it is initialized
	 * to 0 or equivalent
	 */
	struct dahdi_chan_conf conf = {
#ifdef HAVE_PRI
		.pri.pri = {
			.nsf = PRI_NSF_NONE,
			.switchtype = PRI_SWITCH_NI2,
			.dialplan = PRI_UNKNOWN + 1,
			.localdialplan = PRI_NATIONAL_ISDN + 1,
			.nodetype = PRI_CPE,
			.qsigchannelmapping = DAHDI_CHAN_MAPPING_PHYSICAL,

			.minunused = 2,
			.idleext = "",
			.idledial = "",
			.internationalprefix = "",
			.nationalprefix = "",
			.localprefix = "",
			.privateprefix = "",
			.unknownprefix = "",
			.resetinterval = -1,
		},
#endif
#ifdef HAVE_SS7
		.ss7 = {
			.called_nai = SS7_NAI_NATIONAL,
			.calling_nai = SS7_NAI_NATIONAL,
			.internationalprefix = "",
			.nationalprefix = "",
			.subscriberprefix = "",
			.unknownprefix = ""
		},
#endif
#ifdef HAVE_OPENR2
		.mfcr2 = {
			.variant = OR2_VAR_ITU,
			.mfback_timeout = -1,
			.metering_pulse_timeout = -1,
			.max_ani = 10,
			.max_dnis = 4,
			.get_ani_first = -1,
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
			.skip_category_request = -1,
#endif
			.call_files = 0,
			.allow_collect_calls = 0,
			.charge_calls = 1,
			.accept_on_offer = 1,
			.forced_release = 0,
			.double_answer = 0,
			.immediate_accept = -1,
			.logdir = "",
			.r2proto_file = "",
			.loglevel = OR2_LOG_ERROR | OR2_LOG_WARNING,
			.category = OR2_CALLING_PARTY_CATEGORY_NATIONAL_SUBSCRIBER
		},
#endif
		.chan = {
			.context = "default",
			.cid_num = "",
			.cid_name = "",
			.mohinterpret = "default",
			.mohsuggest = "",
			.parkinglot = "",
			.transfertobusy = 1,

			.cid_signalling = CID_SIG_BELL,
			.cid_start = CID_START_RING,
			.dahditrcallerid = 0,
			.use_callerid = 1,
			.sig = -1,
			.outsigmod = -1,

			.cid_rxgain = +5.0,

			.tonezone = -1,

			.echocancel.head.tap_length = 1,

			.busycount = 3,

			.accountcode = "",

			.mailbox = "",

#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
			.mwisend_fsk = 1,
#endif
			.polarityonanswerdelay = 600,

			.sendcalleridafter = DEFAULT_CIDRINGS,

			.buf_policy = DAHDI_POLICY_IMMEDIATE,
			.buf_no = numbufs,
			.usefaxbuffers = 0,
		},
		.timing = {
			.prewinktime = -1,
			.preflashtime = -1,
			.winktime = -1,
			.flashtime = -1,
			.starttime = -1,
			.rxwinktime = -1,
			.rxflashtime = -1,
			.debouncetime = -1
		},
		.is_sig_auto = 1,
		.smdi_port = "/dev/ttyS0",
	};

	return conf;
}


static struct ast_channel *dahdi_request(const char *type, int format, void *data, int *cause);
static int dahdi_digit_begin(struct ast_channel *ast, char digit);
static int dahdi_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int dahdi_sendtext(struct ast_channel *c, const char *text);
static int dahdi_call(struct ast_channel *ast, char *rdest, int timeout);
static int dahdi_hangup(struct ast_channel *ast);
static int dahdi_answer(struct ast_channel *ast);
static struct ast_frame *dahdi_read(struct ast_channel *ast);
static int dahdi_write(struct ast_channel *ast, struct ast_frame *frame);
static struct ast_frame *dahdi_exception(struct ast_channel *ast);
static int dahdi_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen);
static int dahdi_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int dahdi_setoption(struct ast_channel *chan, int option, void *data, int datalen);
static int dahdi_queryoption(struct ast_channel *chan, int option, void *data, int *datalen);
static int dahdi_func_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len);
static int handle_init_event(struct dahdi_pvt *i, int event);
static int dahdi_func_write(struct ast_channel *chan, const char *function, char *data, const char *value);

static const struct ast_channel_tech dahdi_tech = {
	.type = "DAHDI",
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR | AST_FORMAT_ULAW | AST_FORMAT_ALAW,
	.requester = dahdi_request,
	.send_digit_begin = dahdi_digit_begin,
	.send_digit_end = dahdi_digit_end,
	.send_text = dahdi_sendtext,
	.call = dahdi_call,
	.hangup = dahdi_hangup,
	.answer = dahdi_answer,
	.read = dahdi_read,
	.write = dahdi_write,
	.bridge = dahdi_bridge,
	.exception = dahdi_exception,
	.indicate = dahdi_indicate,
	.fixup = dahdi_fixup,
	.setoption = dahdi_setoption,
	.queryoption = dahdi_queryoption,
	.func_channel_read = dahdi_func_read,
	.func_channel_write = dahdi_func_write,
};

#define GET_CHANNEL(p) ((p)->channel)

static enum analog_sigtype dahdisig_to_analogsig(int sig)
{
	switch (sig) {
	case SIG_FXOLS:
		return ANALOG_SIG_FXOLS;
	case SIG_FXOGS:
		return ANALOG_SIG_FXOGS;
	case SIG_FXOKS:
		return ANALOG_SIG_FXOKS;
	case SIG_FXSLS:
		return ANALOG_SIG_FXSLS;
	case SIG_FXSGS:
		return ANALOG_SIG_FXSGS;
	case SIG_FXSKS:
		return ANALOG_SIG_FXSKS;
	case SIG_EMWINK:
		return ANALOG_SIG_EMWINK;
	case SIG_EM:
		return ANALOG_SIG_EM;
	case SIG_EM_E1:
		return ANALOG_SIG_EM_E1;
	case SIG_FEATD:
		return ANALOG_SIG_FEATD;
	case SIG_FEATDMF:
		return ANALOG_SIG_FEATDMF;
	case SIG_E911:
		return SIG_E911;
	case SIG_FGC_CAMA:
		return ANALOG_SIG_FGC_CAMA;
	case SIG_FGC_CAMAMF:
		return ANALOG_SIG_FGC_CAMAMF;
	case SIG_FEATB:
		return ANALOG_SIG_FEATB;
	case SIG_SFWINK:
		return ANALOG_SIG_SFWINK;
	case SIG_SF:
		return ANALOG_SIG_SF;
	case SIG_SF_FEATD:
		return ANALOG_SIG_SF_FEATD;
	case SIG_SF_FEATDMF:
		return ANALOG_SIG_SF_FEATDMF;
	case SIG_FEATDMF_TA:
		return ANALOG_SIG_FEATDMF_TA;
	case SIG_SF_FEATB:
		return ANALOG_SIG_FEATB;
	default:
		return -1;
	}
}


static int analog_tone_to_dahditone(enum analog_tone tone)
{
	switch (tone) {
	case ANALOG_TONE_RINGTONE:
		return DAHDI_TONE_RINGTONE;
	case ANALOG_TONE_STUTTER:
		return DAHDI_TONE_STUTTER;
	case ANALOG_TONE_CONGESTION:
		return DAHDI_TONE_CONGESTION;
	case ANALOG_TONE_DIALTONE:
		return DAHDI_TONE_DIALTONE;
	case ANALOG_TONE_DIALRECALL:
		return DAHDI_TONE_DIALRECALL;
	case ANALOG_TONE_INFO:
		return DAHDI_TONE_INFO;
	default:
		return -1;
	}
}

static int analogsub_to_dahdisub(enum analog_sub analogsub)
{
	int index;

	switch (analogsub) {
	case ANALOG_SUB_REAL:
		index = SUB_REAL;
		break;
	case ANALOG_SUB_CALLWAIT:
		index = SUB_CALLWAIT;
		break;
	case ANALOG_SUB_THREEWAY:
		index = SUB_THREEWAY;
		break;
	default:
		ast_log(LOG_ERROR, "Unidentified sub!\n");
		index = SUB_REAL;
	}

	return index;
}

static enum analog_event dahdievent_to_analogevent(int event);
static int bump_gains(struct dahdi_pvt *p);
static int dahdi_setlinear(int dfd, int linear);

static int my_start_cid_detect(void *pvt, int cid_signalling)
{
	struct dahdi_pvt *p = pvt;
	int index = SUB_REAL;
	p->cs = callerid_new(cid_signalling);
	if (!p->cs) {
		ast_log(LOG_ERROR, "Unable to alloc callerid\n");
		return -1;
	}
	bump_gains(p);
	dahdi_setlinear(p->subs[index].dfd, 0);

	return 0;
}

static int my_stop_cid_detect(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int index = SUB_REAL;
	if (p->cs)
		callerid_free(p->cs);
	dahdi_setlinear(p->subs[index].dfd, p->subs[index].linear);
	return 0;
}

static int my_get_callerid(void *pvt, char *namebuf, char *numbuf, enum analog_event *ev, size_t timeout)
{
	struct dahdi_pvt *p = pvt;
	struct pollfd poller;
	char *name, *num;
	int index = SUB_REAL;
	int res;
	unsigned char buf[256];
	int flags;

	poller.fd = p->subs[SUB_REAL].dfd;
	poller.events = POLLPRI | POLLIN;
	poller.revents = 0;

	res = poll(&poller, 1, timeout);

	if (poller.revents & POLLPRI) {
		*ev = dahdievent_to_analogevent(dahdi_get_event(p->subs[SUB_REAL].dfd));
		return 1;
	}

	if (poller.revents & POLLIN) {
		/*** NOTES ***/
		/* Change API: remove cid_signalling from get_callerid, add a new start_cid_detect and stop_cid_detect function
		 * to enable slin mode and allocate cid detector.  get_callerid should be able to be called any number of times until
		 * either a timeout occurss or CID is detected (returns 0).  returning 1 should be event received, and -1 should be fail
		 * and die */
		res = read(p->subs[index].dfd, buf, sizeof(buf));
		if (res < 0) {
			if (errno != ELAST) {
				ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
				callerid_free(p->cs);
				return -1;
			}
		}
		res = callerid_feed(p->cs, buf, res, AST_LAW(p));
		if (res < 0) {
			ast_log(LOG_WARNING, "CallerID feed failed: %s\n", strerror(errno));
			return -1;
		}

		if (res == 1) {
			callerid_get(p->cs, &name, &num, &flags);
			if (name)
				ast_copy_string(namebuf, name, ANALOG_MAX_CID);
			if (num)
				ast_copy_string(numbuf, num, ANALOG_MAX_CID);

			ast_log(LOG_DEBUG, "CallerID number: %s, name: %s, flags=%d\n", num, name, flags);
			return 0;
		}
	}

	*ev = ANALOG_EVENT_NONE;
	return 1;
}

static int send_callerid(struct dahdi_pvt *p);

static int my_stop_callwait(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;

	return 0;
}

static int save_conference(struct dahdi_pvt *p);

static int my_callwait(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill) {
		ast_log(LOG_WARNING, "Spill already exists?!?\n");
		free(p->cidspill);
	}
	if (!(p->cidspill = ast_malloc(2400 /* SAS */ + 680 /* CAS */ + READ_SIZE * 4)))
		return -1;
	save_conference(p);
	/* Silence */
	memset(p->cidspill, 0x7f, 2400 + 600 + READ_SIZE * 4);
	if (!p->callwaitrings && p->callwaitingcallerid) {
		ast_gen_cas(p->cidspill, 1, 2400 + 680, AST_LAW(p));
		p->callwaitcas = 1;
		p->cidlen = 2400 + 680 + READ_SIZE * 4;
	} else {
		ast_gen_cas(p->cidspill, 1, 2400, AST_LAW(p));
		p->callwaitcas = 0;
		p->cidlen = 2400 + READ_SIZE * 4;
	}
	p->cidpos = 0;
	send_callerid(p);

	return 0;
}

static int my_send_callerid(void *pvt, int cwcid, struct ast_callerid *cid)
{
	struct dahdi_pvt *p = pvt;

	ast_log(LOG_ERROR, "Starting cid spill\n");

	if (p->cidspill) {
		ast_log(LOG_WARNING, "cidspill already exists??\n");
		free(p->cidspill);
	}

	if ((p->cidspill = ast_malloc(MAX_CALLERID_SIZE))) {
		if (cwcid == 0) {
			p->cidlen = ast_callerid_generate(p->cidspill, cid->cid_name, cid->cid_num, AST_LAW(p));
		} else {
			p->callwaitcas = 0;
			p->cidcwexpire = 0;
			p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, cid->cid_name, cid->cid_num, AST_LAW(p));
			p->cidlen += READ_SIZE * 4;
		}
		p->cidpos = 0;
		send_callerid(p);
	}
	return 0;
}

static int my_dsp_reset_and_flush_digits(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	if (p->dsp)
		ast_dsp_digitreset(p->dsp);

	return 0;
}

static int my_dsp_set_digitmode(void *pvt, enum analog_dsp_digitmode mode)
{
	struct dahdi_pvt *p = pvt;

	if (p->channel == CHAN_PSEUDO)
		ast_log(LOG_ERROR, "You have assumed incorrectly sir!\n");

	if (mode == ANALOG_DIGITMODE_DTMF) {
		/* If we do hardware dtmf, no need for a DSP */
		if (p->hardwaredtmf) {
			if (p->dsp) {
				ast_dsp_free(p->dsp);
				p->dsp = NULL;
			}
			return 0;
		}

		if (!p->dsp) {
			p->dsp = ast_dsp_new();
			if (!p->dsp) {
				ast_log(LOG_ERROR, "Unable to allocate DSP\n");
				return -1;
			}
		}

		ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
	} else if (mode == ANALOG_DIGITMODE_MF) {
		if (!p->dsp) {
			p->dsp = ast_dsp_new();
			if (!p->dsp) {
				ast_log(LOG_ERROR, "Unable to allocate DSP\n");
				return -1;
			}
		}
		ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MF | p->dtmfrelax);
	}
	return 0;
}

static int dahdi_wink(struct dahdi_pvt *p, int index);

static int my_wink(void *pvt, enum analog_sub sub)
{
	struct dahdi_pvt *p = pvt;
	int index = analogsub_to_dahdisub(sub);
	if (index != SUB_REAL) {
		ast_log(LOG_ERROR, "We used a sub other than SUB_REAL (incorrect assumption sir)\n");
	}
	return dahdi_wink(p, index);
}

static void wakeup_sub(struct dahdi_pvt *p, int a);

static int reset_conf(struct dahdi_pvt *p);

static inline int dahdi_confmute(struct dahdi_pvt *p, int muted);

static void my_handle_dtmfup(void *pvt, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest)
{
	struct ast_frame *f = *dest;
	struct dahdi_pvt *p = pvt;
	int idx = analogsub_to_dahdisub(analog_index);

	ast_debug(1, "DTMF digit: %c on %s\n", f->subclass, ast->name);

	if (f->subclass == 'f') {
		/* Fax tone -- Handle and return NULL */
		if ((p->callprogress & CALLPROGRESS_FAX) && !p->faxhandled) {
			/* If faxbuffers are configured, use them for the fax transmission */
			if (p->usefaxbuffers && !p->bufferoverrideinuse) {
				struct dahdi_bufferinfo bi = {
					.txbufpolicy = p->faxbuf_policy,
					.bufsize = p->bufsize,
					.numbufs = p->faxbuf_no
				};
				int res;

				if ((res = ioctl(p->subs[idx].dfd, DAHDI_SET_BUFINFO, &bi)) < 0) {
					ast_log(LOG_WARNING, "Channel '%s' unable to set buffer policy, reason: %s\n", ast->name, strerror(errno));
				} else {
					p->bufferoverrideinuse = 1;
				}
			}
			p->faxhandled = 1;
			if (strcmp(ast->exten, "fax")) {
				const char *target_context = S_OR(ast->macrocontext, ast->context);

				/* We need to unlock 'ast' here because ast_exists_extension has the
				 * potential to start autoservice on the channel. Such action is prone
				 * to deadlock.
				 */
				ast_mutex_unlock(&p->lock);
				ast_channel_unlock(ast);
				if (ast_exists_extension(ast, target_context, "fax", 1, ast->cid.cid_num)) {
					ast_channel_lock(ast);
					ast_mutex_lock(&p->lock);
					ast_verb(3, "Redirecting %s to fax extension\n", ast->name);
					/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
					pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast->exten);
					if (ast_async_goto(ast, target_context, "fax", 1))
						ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, target_context);
				} else {
					ast_channel_lock(ast);
					ast_mutex_lock(&p->lock);
					ast_log(LOG_NOTICE, "Fax detected, but no fax extension\n");
				}
			} else {
				ast_debug(1, "Already in a fax extension, not redirecting\n");
			}
		} else {
			ast_debug(1, "Fax already handled\n");
		}
		dahdi_confmute(p, 0);
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass = 0;
		*dest = &p->subs[idx].f;
	}
}

static void my_lock_private(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	ast_mutex_lock(&p->lock);
}

static void my_unlock_private(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	ast_mutex_unlock(&p->lock);
}

static void my_increase_ss_count(void)
{
	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count++;
	ast_mutex_unlock(&ss_thread_lock);
}

static void my_decrease_ss_count(void)
{
	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count--;
	ast_cond_signal(&ss_thread_complete);
	ast_mutex_unlock(&ss_thread_lock);
}

static void my_all_subchannels_hungup(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int res, law;

	p->faxhandled = 0;
	p->didtdd = 0;

	if (p->dsp) {
		ast_dsp_free(p->dsp);
		p->dsp = NULL;
	}

	law = DAHDI_LAW_DEFAULT;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
	if (res < 0) 
		ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n", p->channel, strerror(errno));

	dahdi_setlinear(p->subs[SUB_REAL].dfd, 0);

#if 1
	{
	int i;
	p->owner = NULL;
	/* Cleanup owners here */
	for (i = 0; i < 3; i++) {
		p->subs[i].owner = NULL;
	}
	}
#endif

	reset_conf(p);
	if (num_restart_pending == 0) {
		restart_monitor();
	}
}

static int conf_del(struct dahdi_pvt *p, struct dahdi_subchannel *c, int index);

static int my_conf_del(void *pvt, enum analog_sub sub)
{
	struct dahdi_pvt *p = pvt;
	int x = analogsub_to_dahdisub(sub);

	return conf_del(p, &p->subs[x], x);
}

static int conf_add(struct dahdi_pvt *p, struct dahdi_subchannel *c, int index, int slavechannel);

static int my_conf_add(void *pvt, enum analog_sub sub)
{
	struct dahdi_pvt *p = pvt;
	int x = analogsub_to_dahdisub(sub);

	return conf_add(p, &p->subs[x], x, 0);
}

static int isslavenative(struct dahdi_pvt *p, struct dahdi_pvt **out);

static int my_complete_conference_update(void *pvt, int needconference)
{
	struct dahdi_pvt *p = pvt;
	int needconf = needconference;
	int x;
	int useslavenative;
	struct dahdi_pvt *slave = NULL;

	useslavenative = isslavenative(p, &slave);

	/* If we have a slave, add him to our conference now. or DAX
	   if this is slave native */
	for (x = 0; x < MAX_SLAVES; x++) {
		if (p->slaves[x]) {
			if (useslavenative)
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p));
			else {
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, 0);
				needconf++;
			}
		}
	}
	/* If we're supposed to be in there, do so now */
	if (p->inconference && !p->subs[SUB_REAL].inthreeway) {
		if (useslavenative)
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(slave));
		else {
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, 0);
			needconf++;
		}
	}
	/* If we have a master, add ourselves to his conference */
	if (p->master) {
		if (isslavenative(p->master, NULL)) {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p->master));
		} else {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, 0);
		}
	}
	if (!needconf) {
		/* Nobody is left (or should be left) in our conference.
		   Kill it. */
		p->confno = -1;
	}

	return 0;
}

static int check_for_conference(struct dahdi_pvt *p);

static int my_check_for_conference(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	return check_for_conference(p);
}

static void my_swap_subchannels(void *pvt, enum analog_sub a, struct ast_channel *ast_a,  enum analog_sub b, struct ast_channel *ast_b)
{
	struct dahdi_pvt *p = pvt;
	int da, db;
	int tchan;

	da = analogsub_to_dahdisub(a);
	db = analogsub_to_dahdisub(b);

	tchan = p->subs[da].chan;

	p->subs[da].chan = p->subs[db].chan;

	p->subs[db].chan = tchan;

	if (ast_a)
		ast_a->fds[0] = p->subs[da].dfd;
	if (ast_b)
		ast_b->fds[0] = p->subs[db].dfd;

	p->subs[da].owner = ast_a;
	p->subs[db].owner = ast_b;

	wakeup_sub(p, a);
	wakeup_sub(p, b);

	return;
}

static struct ast_channel *dahdi_new(struct dahdi_pvt *, int, int, int, int, int);

static struct ast_channel *my_new_analog_ast_channel(void *pvt, int state, int startpbx, enum analog_sub sub)
{
	struct dahdi_pvt *p = pvt;
	int dsub = analogsub_to_dahdisub(sub);

	return dahdi_new(p, state, startpbx, dsub, 0, 0);
}

#if defined(HAVE_PRI) || defined(HAVE_SS7)
static int dahdi_setlaw(int dfd, int law)
{
	int res;
	res = ioctl(dfd, DAHDI_SETLAW, &law);
	if (res)
		return res;
	return 0;
}
#endif

#ifdef HAVE_PRI
static struct ast_channel *my_new_pri_ast_channel(void *pvt, int state, int startpbx, enum sig_pri_law law, int transfercapability, char *exten)
{
	struct dahdi_pvt *p = pvt;
	int audio;
	int newlaw = -1;

	/* Set to audio mode at this point */
	audio = 1;
	if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &audio) == -1)
		ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", p->channel, audio, strerror(errno));

	if (law != SIG_PRI_DEFLAW) {
		dahdi_setlaw(p->subs[SUB_REAL].dfd, (law == SIG_PRI_ULAW) ? DAHDI_LAW_MULAW : DAHDI_LAW_ALAW);
	}

	ast_copy_string(p->exten, exten, sizeof(p->exten));

	switch (law) {
		case SIG_PRI_DEFLAW:
			newlaw = 0;
			break;
		case SIG_PRI_ALAW:
			newlaw = DAHDI_LAW_ALAW;
			break;
		case SIG_PRI_ULAW:
			newlaw = DAHDI_LAW_MULAW;
			break;
	}
	return dahdi_new(p, state, startpbx, SUB_REAL, newlaw, transfercapability);
}
#endif

static int unalloc_sub(struct dahdi_pvt *p, int x);

static int my_unallocate_sub(void *pvt, enum analog_sub analogsub)
{
	struct dahdi_pvt *p = pvt;

	return unalloc_sub(p, analogsub_to_dahdisub(analogsub));
}

static int alloc_sub(struct dahdi_pvt *p, int x);

static int my_allocate_sub(void *pvt, enum analog_sub analogsub)
{
	struct dahdi_pvt *p = pvt;

	return alloc_sub(p, analogsub_to_dahdisub(analogsub));
}

static int has_voicemail(struct dahdi_pvt *p);

static int my_has_voicemail(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	return has_voicemail(p);
}

static int my_play_tone(void *pvt, enum analog_sub sub, enum analog_tone tone)
{
	struct dahdi_pvt *p = pvt;
	int index;

	index = analogsub_to_dahdisub(sub);

	return tone_zone_play_tone(p->subs[index].dfd, analog_tone_to_dahditone(tone));
}

static enum analog_event dahdievent_to_analogevent(int event)
{
	enum analog_event res = ANALOG_EVENT_ERROR;

	switch (event) {
	case DAHDI_EVENT_DIALCOMPLETE:
		res = ANALOG_EVENT_DIALCOMPLETE;
		break;
	case DAHDI_EVENT_WINKFLASH:
		res = ANALOG_EVENT_WINKFLASH;
		break;
	case DAHDI_EVENT_ONHOOK:
		res = ANALOG_EVENT_ONHOOK;
		break;
	case DAHDI_EVENT_RINGOFFHOOK:
		res = ANALOG_EVENT_RINGOFFHOOK;
		break;
	case DAHDI_EVENT_ALARM:
		res = ANALOG_EVENT_ALARM;
		break;
	case DAHDI_EVENT_NOALARM:
		res = ANALOG_EVENT_NOALARM;
		break;
	case DAHDI_EVENT_HOOKCOMPLETE:
		res = ANALOG_EVENT_HOOKCOMPLETE;
		break;
	case DAHDI_EVENT_POLARITY:
		res = ANALOG_EVENT_POLARITY;
		break;
	case DAHDI_EVENT_RINGERON:
		res = ANALOG_EVENT_RINGERON;
		break;
	case DAHDI_EVENT_RINGEROFF:
		res = ANALOG_EVENT_RINGEROFF;
		break;
	case DAHDI_EVENT_RINGBEGIN:
		res = ANALOG_EVENT_RINGBEGIN;
		break;
	case DAHDI_EVENT_PULSE_START:
		res = ANALOG_EVENT_PULSE_START;
	break;
	case DAHDI_EVENT_NEONMWI_ACTIVE:
		res = ANALOG_EVENT_NEONMWI_ACTIVE;
		break;
	case DAHDI_EVENT_NEONMWI_INACTIVE:
		res = ANALOG_EVENT_NEONMWI_INACTIVE;
		break;
	}

	return res;
}

static inline int dahdi_wait_event(int fd);

static int my_wait_event(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	return dahdi_wait_event(p->subs[SUB_REAL].dfd);
}

static int my_get_event(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int res;

	if (p->fake_event) {
		res = p->fake_event;
		p->fake_event = 0;
	} else
		res = dahdi_get_event(p->subs[SUB_REAL].dfd);

	return dahdievent_to_analogevent(res);
}

static int my_is_off_hook(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int res;
	struct dahdi_params par;

	if (p->subs[SUB_REAL].dfd > -1)
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &par);
	else {
		/* Assume not off hook on CVRS */
		res = 0;
		par.rxisoffhook = 0;
	}
	if (res) {
		ast_log(LOG_WARNING, "Unable to check hook state on channel %d: %s\n", p->channel, strerror(errno));
	}

	return (par.rxbits > -1) || par.rxisoffhook;
}

static void dahdi_enable_ec(struct dahdi_pvt *p);
static void dahdi_disable_ec(struct dahdi_pvt *p);

static int my_set_echocanceller(void *pvt, int enable)
{
	struct dahdi_pvt *p = pvt;

	if (enable)
		dahdi_enable_ec(p);
	else
		dahdi_disable_ec(p);

	return 0;
}

static int dahdi_ring_phone(struct dahdi_pvt *p);

static int my_ring(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	return dahdi_ring_phone(p);
}

static inline int dahdi_set_hook(int fd, int hs);

static int my_off_hook(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	return dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
}

static int my_start(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int x = DAHDI_START;

	return ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
}

static int my_dial_digits(void *pvt, enum analog_sub sub, struct analog_dialoperation *dop)
{
	int index = analogsub_to_dahdisub(sub);
	int res;
	struct dahdi_pvt *p = pvt;
	struct dahdi_dialoperation ddop;

	if (dop->op != ANALOG_DIAL_OP_REPLACE) {
		ast_log(LOG_ERROR, "Fix the dial_digits callback!\n");
		return -1;
	}

	if (sub != ANALOG_SUB_REAL)
		printf("Trying to dial digits on sub %d\n", sub);

	ddop.op = DAHDI_DIAL_OP_REPLACE;
	strncpy(ddop.dialstr, dop->dialstr, sizeof(ddop.dialstr));

	printf("Dialing %s on %d\n", ddop.dialstr, p->channel);

	res = ioctl(p->subs[index].dfd, DAHDI_DIAL, &ddop);

	if (res == -1)
		ast_log(LOG_DEBUG, "DAHDI_DIAL ioctl failed on %s: %s\n", p->owner->name, strerror(errno));

	return res;
}

static void dahdi_train_ec(struct dahdi_pvt *p);

static int my_train_echocanceller(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	dahdi_train_ec(p);

	return 0;
}

static int my_is_dialing(void *pvt, enum analog_sub sub)
{
	struct dahdi_pvt *p = pvt;
	int index;
	int x;

	index = analogsub_to_dahdisub(sub);

	if (ioctl(p->subs[index].dfd, DAHDI_DIALING, &x)) {
		ast_log(LOG_DEBUG, "DAHDI_DIALING ioctl failed!\n");
		return -1;
	}

	return x;
}

static int my_on_hook(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int x = DAHDI_ONHOOK;

	return ioctl(p->subs[ANALOG_SUB_REAL].dfd, DAHDI_HOOK, &x);
}

#ifdef HAVE_PRI
static void my_pri_fixup_chans(void *old_chan, void *new_chan)
{
	struct dahdi_pvt *old = old_chan;
	struct dahdi_pvt *new = new_chan;
	struct sig_pri_chan *pchan = new->sig_pvt;
	struct sig_pri_pri *pri = pchan->pri;

	new->owner = old->owner;
	old->owner = NULL;
	if (new->owner) {
		char newname[AST_CHANNEL_NAME];
		snprintf(newname, sizeof(newname), "DAHDI/%d:%d-%d", pri->trunkgroup, new->channel, 1);
		ast_change_name(new->owner, newname);

		new->owner->tech_pvt = new;
		new->owner->fds[0] = new->subs[SUB_REAL].dfd;
		new->subs[SUB_REAL].owner = old->subs[SUB_REAL].owner;
		old->subs[SUB_REAL].owner = NULL;
	}
	/* Copy any DSP that may be present */
	new->dsp = old->dsp;
	new->dsp_features = old->dsp_features;
	old->dsp = NULL;
	old->dsp_features = 0;
}

static int sig_pri_tone_to_dahditone(enum analog_tone tone)
{
	switch (tone) {
	case SIG_PRI_TONE_RINGTONE:
		return DAHDI_TONE_RINGTONE;
	case SIG_PRI_TONE_STUTTER:
		return DAHDI_TONE_STUTTER;
	case SIG_PRI_TONE_CONGESTION:
		return DAHDI_TONE_CONGESTION;
	case SIG_PRI_TONE_DIALTONE:
		return DAHDI_TONE_DIALTONE;
	case SIG_PRI_TONE_DIALRECALL:
		return DAHDI_TONE_DIALRECALL;
	case SIG_PRI_TONE_INFO:
		return DAHDI_TONE_INFO;
	case SIG_PRI_TONE_BUSY:
		return DAHDI_TONE_BUSY;
	default:
		return -1;
	}
}

static const char *event2str(int event);

static void my_handle_dchan_exception(struct sig_pri_pri *pri, int index)
{
	int x, res;

	res = ioctl(pri->fds[index], DAHDI_GETEVENT, &x);
	if (x) {
		ast_log(LOG_NOTICE, "PRI got event: %s (%d) on D-channel of span %d\n", event2str(x), x, pri->span);
	}
	/* Keep track of alarm state */	
	if (x == DAHDI_EVENT_ALARM) {
		pri_event_alarm(pri, index, 0);
	} else if (x == DAHDI_EVENT_NOALARM) {
		pri_event_noalarm(pri, index, 0);
	}
}

static int my_pri_play_tone(void *pvt, enum sig_pri_tone tone)
{
	struct dahdi_pvt *p = pvt;

	return tone_zone_play_tone(p->subs[SUB_REAL].dfd, sig_pri_tone_to_dahditone(tone));
}

static struct sig_pri_callback dahdi_pri_callbacks =
{
	.handle_dchan_exception = my_handle_dchan_exception,
	.play_tone = my_pri_play_tone,
	.set_echocanceller = my_set_echocanceller,
	.lock_private = my_lock_private,
	.unlock_private = my_unlock_private,
	.new_ast_channel = my_new_pri_ast_channel,
	.fixup_chans = my_pri_fixup_chans,
};
#endif /* HAVE_PRI */

/*!
 * \brief Send MWI state change
 *
 * \arg mailbox_full This is the mailbox associated with the FXO line that the
 *      MWI state has changed on.
 * \arg thereornot This argument should simply be set to 1 or 0, to indicate
 *      whether there are messages waiting or not.
 *
 *  \return nothing
 *
 * This function does two things:
 *
 * 1) It generates an internal Asterisk event notifying any other module that
 *    cares about MWI that the state of a mailbox has changed.
 *
 * 2) It runs the script specified by the mwimonitornotify option to allow
 *    some custom handling of the state change.
 */
static void notify_message(char *mailbox_full, int thereornot)
{
	char s[sizeof(mwimonitornotify) + 80];
	struct ast_event *event;
	char *mailbox, *context;

	/* Strip off @default */
	context = mailbox = ast_strdupa(mailbox_full);
	strsep(&context, "@");
	if (ast_strlen_zero(context))
		context = "default";

	if (!(event = ast_event_new(AST_EVENT_MWI,
			AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
			AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
			AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, thereornot,
			AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, thereornot,
			AST_EVENT_IE_END))) {
		return;
	}

	ast_event_queue_and_cache(event);

	if (!ast_strlen_zero(mailbox) && !ast_strlen_zero(mwimonitornotify)) {
		snprintf(s, sizeof(s), "%s %s %d", mwimonitornotify, mailbox, thereornot);
		ast_safe_system(s);
	}
}

static void my_handle_notify_message(struct ast_channel *chan, void *pvt, int cid_flags, int neon_mwievent)
{
	struct dahdi_pvt *p = pvt;

	if (neon_mwievent > -1 && !p->mwimonitor_neon)
		return;

	if (neon_mwievent == ANALOG_EVENT_NEONMWI_ACTIVE || cid_flags & CID_MSGWAITING) {
		ast_log(LOG_NOTICE, "MWI: Channel %d message waiting, mailbox %s\n", p->channel, p->mailbox);
		notify_message(p->mailbox, 1);
	} else if (neon_mwievent == ANALOG_EVENT_NEONMWI_INACTIVE || cid_flags & CID_NOMSGWAITING) {
		ast_log(LOG_NOTICE, "MWI: Channel %d no message waiting, mailbox %s\n", p->channel, p->mailbox);
		notify_message(p->mailbox, 0);
	}
	/* If the CID had Message waiting payload, assume that this for MWI only and hangup the call */
	/* If generated using Ring Pulse Alert, then ring has been answered as a call and needs to be hungup */
	if (neon_mwievent == -1 && p->mwimonitor_rpas) {
		ast_hangup(chan);
		return;
	}
}



static struct analog_callback dahdi_analog_callbacks =
{
	.play_tone = my_play_tone,
	.get_event = my_get_event,
	.wait_event = my_wait_event,
	.is_off_hook = my_is_off_hook,
	.set_echocanceller = my_set_echocanceller,
	.ring = my_ring,
	.off_hook = my_off_hook,
	.dial_digits = my_dial_digits,
	.train_echocanceller = my_train_echocanceller,
	.on_hook = my_on_hook,
	.is_dialing = my_is_dialing,
	.allocate_sub = my_allocate_sub,
	.unallocate_sub = my_unallocate_sub,
	.swap_subs = my_swap_subchannels,
	.has_voicemail = my_has_voicemail,
	.check_for_conference = my_check_for_conference,
	.conf_add = my_conf_add,
	.conf_del = my_conf_del,
	.complete_conference_update = my_complete_conference_update,
	.start = my_start,
	.all_subchannels_hungup = my_all_subchannels_hungup,
	.lock_private = my_lock_private,
	.unlock_private = my_unlock_private,
	.handle_dtmfup = my_handle_dtmfup,
	.wink = my_wink,
	.new_ast_channel = my_new_analog_ast_channel,
	.dsp_set_digitmode = my_dsp_set_digitmode,
	.dsp_reset_and_flush_digits = my_dsp_reset_and_flush_digits,
	.send_callerid = my_send_callerid,
	.callwait = my_callwait,
	.stop_callwait = my_stop_callwait,
	.get_callerid = my_get_callerid,
	.start_cid_detect = my_start_cid_detect,
	.stop_cid_detect = my_stop_cid_detect,
	.handle_notify_message = my_handle_notify_message,
	.increase_ss_count = my_increase_ss_count,
	.decrease_ss_count = my_decrease_ss_count,
};

static struct dahdi_pvt *round_robin[32];

#if defined(HAVE_SS7)
static inline void ss7_rel(struct dahdi_ss7 *ss7)
{
	ast_mutex_unlock(&ss7->lock);
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static inline int ss7_grab(struct dahdi_pvt *pvt, struct dahdi_ss7 *pri)
{
	int res;
	/* Grab the lock first */
	do {
		res = ast_mutex_trylock(&pri->lock);
		if (res) {
			DEADLOCK_AVOIDANCE(&pvt->lock);
		}
	} while (res);
	/* Then break the poll */
	if (pri->master != AST_PTHREADT_NULL)
		pthread_kill(pri->master, SIGURG);
	return 0;
}
#endif	/* defined(HAVE_SS7) */
#define NUM_CADENCE_MAX 25
static int num_cadence = 4;
static int user_has_defined_cadences = 0;

static struct dahdi_ring_cadence cadences[NUM_CADENCE_MAX] = {
	{ { 125, 125, 2000, 4000 } },			/*!< Quick chirp followed by normal ring */
	{ { 250, 250, 500, 1000, 250, 250, 500, 4000 } }, /*!< British style ring */
	{ { 125, 125, 125, 125, 125, 4000 } },	/*!< Three short bursts */
	{ { 1000, 500, 2500, 5000 } },	/*!< Long ring */
};

/*! \brief cidrings says in which pause to transmit the cid information, where the first pause
 * is 1, the second pause is 2 and so on.
 */

static int cidrings[NUM_CADENCE_MAX] = {
	2,										/*!< Right after first long ring */
	4,										/*!< Right after long part */
	3,										/*!< After third chirp */
	2,										/*!< Second spell */
};

/* ETSI EN300 659-1 specifies the ring pulse between 200 and 300 mS */
static struct dahdi_ring_cadence AS_RP_cadence = {{250, 10000}};

#define ISTRUNK(p) ((p->sig == SIG_FXSLS) || (p->sig == SIG_FXSKS) || \
			(p->sig == SIG_FXSGS) || (p->sig == SIG_PRI))

#define CANBUSYDETECT(p) (ISTRUNK(p) || (p->sig & (SIG_EM | SIG_EM_E1 | SIG_SF)) /* || (p->sig & __DAHDI_SIG_FXO) */)
#define CANPROGRESSDETECT(p) (ISTRUNK(p) || (p->sig & (SIG_EM | SIG_EM_E1 | SIG_SF)) /* || (p->sig & __DAHDI_SIG_FXO) */)

static int dahdi_get_index(struct ast_channel *ast, struct dahdi_pvt *p, int nullok)
{
	int res;
	if (p->subs[SUB_REAL].owner == ast)
		res = 0;
	else if (p->subs[SUB_CALLWAIT].owner == ast)
		res = 1;
	else if (p->subs[SUB_THREEWAY].owner == ast)
		res = 2;
	else {
		res = -1;
		if (!nullok)
			ast_log(LOG_WARNING, "Unable to get index, and nullok is not asserted\n");
	}
	return res;
}

static void wakeup_sub(struct dahdi_pvt *p, int a)
{
	for (;;) {
		if (p->subs[a].owner) {
			if (ast_channel_trylock(p->subs[a].owner)) {
				DEADLOCK_AVOIDANCE(&p->lock);
			} else {
				ast_queue_frame(p->subs[a].owner, &ast_null_frame);
				ast_channel_unlock(p->subs[a].owner);
				break;
			}
		} else
			break;
	}
}

static void dahdi_queue_frame(struct dahdi_pvt *p, struct ast_frame *f, void *data)
{
#ifdef HAVE_SS7
	struct dahdi_ss7 *ss7 = (struct dahdi_ss7*) data;

	if (data) {
		switch (p->sig) {
		case SIG_SS7:
			ast_mutex_unlock(&ss7->lock);
			break;
		default:
			break;
		}
	}
#endif
	for (;;) {
		if (p->owner) {
			if (ast_channel_trylock(p->owner)) {
				DEADLOCK_AVOIDANCE(&p->lock);
			} else {
				ast_queue_frame(p->owner, f);
				ast_channel_unlock(p->owner);
				break;
			}
		} else
			break;
	}
#if defined(HAVE_SS7)
	if (data) {
		switch (p->sig) {
		case SIG_SS7:
			ast_mutex_lock(&ss7->lock);
			break;
		default:
			break;
		}
	}
#endif
}

static struct ast_channel *dahdi_new(struct dahdi_pvt *, int, int, int, int, int);
#ifdef HAVE_OPENR2

static int dahdi_r2_answer(struct dahdi_pvt *p)
{
	int res = 0;
	/* openr2 1.1.0 and older does not even define OR2_LIB_INTERFACE
	* and does not has support for openr2_chan_answer_call_with_mode
	*  */
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
	const char *double_answer = pbx_builtin_getvar_helper(p->owner, "MFCR2_DOUBLE_ANSWER");
	int wants_double_answer = ast_true(double_answer) ? 1 : 0;
	if (!double_answer) {
		/* this still can result in double answer if the channel context
		* was configured that way */
		res = openr2_chan_answer_call(p->r2chan);
	} else if (wants_double_answer) {
		res = openr2_chan_answer_call_with_mode(p->r2chan, OR2_ANSWER_DOUBLE);
	} else {
		res = openr2_chan_answer_call_with_mode(p->r2chan, OR2_ANSWER_SIMPLE);
	}
#else
	res = openr2_chan_answer_call(p->r2chan);
#endif
	return res;
}



/* should be called with the ast_channel locked */
static openr2_calling_party_category_t dahdi_r2_get_channel_category(struct ast_channel *c)
{
	openr2_calling_party_category_t cat;
	const char *catstr = pbx_builtin_getvar_helper(c, "MFCR2_CATEGORY");
	struct dahdi_pvt *p = c->tech_pvt;
	if (ast_strlen_zero(catstr)) {
		ast_debug(1, "No MFC/R2 category specified for chan %s, using default %s\n",
				c->name, openr2_proto_get_category_string(p->mfcr2_category));
		return p->mfcr2_category;
	}
	if ((cat = openr2_proto_get_category(catstr)) == OR2_CALLING_PARTY_CATEGORY_UNKNOWN) {
		ast_log(LOG_WARNING, "Invalid category specified '%s' for chan %s, using default %s\n",
				catstr, c->name, openr2_proto_get_category_string(p->mfcr2_category));
		return p->mfcr2_category;
	}
	ast_debug(1, "Using category %s\n", catstr);
	return cat;
}

static void dahdi_r2_on_call_init(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_mutex_lock(&p->lock);
	if (p->mfcr2call) {
		ast_mutex_unlock(&p->lock);
		/* TODO: This can happen when some other thread just finished dahdi_request requesting this very same
		   interface but has not yet seized the line (dahdi_call), and the far end wins and seize the line,
		   can we avoid this somehow?, at this point when dahdi_call send the seize, it is likely that since
		   the other end will see our seize as a forced release and drop the call, we will see an invalid
		   pattern that will be seen and treated as protocol error. */
		ast_log(LOG_ERROR, "Collision of calls on chan %d detected!.\n", openr2_chan_get_number(r2chan));
		return;
	}
	p->mfcr2call = 1;
	/* better safe than sorry ... */
	p->cid_name[0] = '\0';
	p->cid_num[0] = '\0';
	p->rdnis[0] = '\0';
	p->exten[0] = '\0';
	p->mfcr2_ani_index = '\0';
	p->mfcr2_dnis_index = '\0';
	p->mfcr2_dnis_matched = 0;
	p->mfcr2_answer_pending = 0;
	p->mfcr2_call_accepted = 0;
	ast_mutex_unlock(&p->lock);
	ast_log(LOG_NOTICE, "New MFC/R2 call detected on chan %d.\n", openr2_chan_get_number(r2chan));
}

static int get_alarms(struct dahdi_pvt *p);
static void handle_alarms(struct dahdi_pvt *p, int alms);
static void dahdi_r2_on_hardware_alarm(openr2_chan_t *r2chan, int alarm)
{
	int res;
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_mutex_lock(&p->lock);
	p->inalarm = alarm ? 1 : 0;
	if (p->inalarm) {
		res = get_alarms(p);
		handle_alarms(p, res);
	} else {
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear", "Channel: %d\r\n", p->channel);
	}
	ast_mutex_unlock(&p->lock);
}

static void dahdi_r2_on_os_error(openr2_chan_t *r2chan, int errorcode)
{
	ast_log(LOG_ERROR, "OS error on chan %d: %s\n", openr2_chan_get_number(r2chan), strerror(errorcode));
}

static void dahdi_r2_on_protocol_error(openr2_chan_t *r2chan, openr2_protocol_error_t reason)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_log(LOG_ERROR, "MFC/R2 protocol error on chan %d: %s\n", openr2_chan_get_number(r2chan), openr2_proto_get_error(reason));
	if (p->owner) {
		p->owner->hangupcause = AST_CAUSE_PROTOCOL_ERROR;
		p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
	}
	ast_mutex_lock(&p->lock);
	p->mfcr2call = 0;
	ast_mutex_unlock(&p->lock);
}

static void dahdi_r2_update_monitor_count(struct dahdi_mfcr2 *mfcr2, int increment)
{
	ast_mutex_lock(&mfcr2->monitored_count_lock);
	if (increment) {
		mfcr2->monitored_count++;
		if (mfcr2->monitored_count == 1) {
			ast_log(LOG_DEBUG, "At least one device needs monitoring, let's wake up the monitor thread.\n");
			ast_cond_signal(&mfcr2->do_monitor);
		}
	} else {
		mfcr2->monitored_count--;
		if (mfcr2->monitored_count < 0) {
			ast_log(LOG_ERROR, "we have a bug here!.\n");
		}
	}
	ast_mutex_unlock(&mfcr2->monitored_count_lock);
}

static void dahdi_r2_on_call_offered(openr2_chan_t *r2chan, const char *ani, const char *dnis, openr2_calling_party_category_t category)
{
	struct dahdi_pvt *p;
	struct ast_channel *c;
	ast_log(LOG_NOTICE, "MFC/R2 call offered on chan %d. ANI = %s, DNIS = %s, Category = %s\n",
			openr2_chan_get_number(r2chan), ani ? ani : "(restricted)", dnis,
			openr2_proto_get_category_string(category));
	p = openr2_chan_get_client_data(r2chan);
	/* if collect calls are not allowed and this is a collect call, reject it! */
	if (!p->mfcr2_allow_collect_calls && category == OR2_CALLING_PARTY_CATEGORY_COLLECT_CALL) {
		ast_log(LOG_NOTICE, "Rejecting MFC/R2 collect call\n");
		openr2_chan_disconnect_call(r2chan, OR2_CAUSE_COLLECT_CALL_REJECTED);
		return;
	}
	ast_mutex_lock(&p->lock);
	p->mfcr2_recvd_category = category;
	/* if we're not supposed to use CID, clear whatever we have */
	if (!p->use_callerid) {
		ast_log(LOG_DEBUG, "No CID allowed in configuration, CID is being cleared!\n");
		p->cid_num[0] = 0;
		p->cid_name[0] = 0;
	}
	/* if we're supposed to answer immediately, clear DNIS and set 's' exten */
	if (p->immediate || !openr2_context_get_max_dnis(openr2_chan_get_context(r2chan))) {
		ast_log(LOG_DEBUG, "Setting exten => s because of immediate or 0 DNIS configured\n");
		p->exten[0] = 's';
		p->exten[1] = 0;
	}
	ast_mutex_unlock(&p->lock);
	if (!ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num)) {
		ast_log(LOG_NOTICE, "MFC/R2 call on channel %d requested non-existent extension '%s' in context '%s'. Rejecting call.\n",
				p->channel, p->exten, p->context);
		openr2_chan_disconnect_call(r2chan, OR2_CAUSE_UNALLOCATED_NUMBER);
		return;
	}
	if (!p->mfcr2_accept_on_offer) {
		/* The user wants us to start the PBX thread right away without accepting the call first */
		c = dahdi_new(p, AST_STATE_RING, 1, SUB_REAL, DAHDI_LAW_ALAW, 0);
		if (c) {
			dahdi_r2_update_monitor_count(p->mfcr2, 0);
			/* Done here, don't disable reading now since we still need to generate MF tones to accept
			   the call or reject it and detect the tone off condition of the other end, all of this
			   will be done in the PBX thread now */
			return;
		}
		ast_log(LOG_WARNING, "Unable to create PBX channel in DAHDI channel %d\n", p->channel);
		openr2_chan_disconnect_call(r2chan, OR2_CAUSE_OUT_OF_ORDER);
	} else if (p->mfcr2_charge_calls) {
		ast_log(LOG_DEBUG, "Accepting MFC/R2 call with charge on chan %d\n", p->channel);
		openr2_chan_accept_call(r2chan, OR2_CALL_WITH_CHARGE);
	} else {
		ast_log(LOG_DEBUG, "Accepting MFC/R2 call with no charge on chan %d\n", p->channel);
		openr2_chan_accept_call(r2chan, OR2_CALL_NO_CHARGE);
	}
}

static void dahdi_r2_on_call_end(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_log(LOG_NOTICE, "MFC/R2 call end on chan %d\n", p->channel);
	ast_mutex_lock(&p->lock);
	p->mfcr2call = 0;
	ast_mutex_unlock(&p->lock);
}

static void dahdi_enable_ec(struct dahdi_pvt *p);
static void dahdi_r2_on_call_accepted(openr2_chan_t *r2chan, openr2_call_mode_t mode)
{
	struct dahdi_pvt *p = NULL;
	struct ast_channel *c = NULL;
	ast_log(LOG_NOTICE, "MFC/R2 call has been accepted on chan %d\n", openr2_chan_get_number(r2chan));
	p = openr2_chan_get_client_data(r2chan);
	dahdi_enable_ec(p);
	p->mfcr2_call_accepted = 1;
	/* if it's an incoming call ... */
	if (OR2_DIR_BACKWARD == openr2_chan_get_direction(r2chan)) {
		/* If accept on offer is not set, it means at this point the PBX thread is already
		   launched (was launched in the 'on call offered' handler) and therefore this callback
		   is being executed already in the PBX thread rather than the monitor thread, don't launch
		   any other thread, just disable the openr2 reading and answer the call if needed */
		if (!p->mfcr2_accept_on_offer) {
			openr2_chan_disable_read(r2chan);
			if (p->mfcr2_answer_pending) {
				ast_log(LOG_DEBUG, "Answering MFC/R2 call after accepting it on chan %d\n", openr2_chan_get_number(r2chan));
				dahdi_r2_answer(p);
			}
			return;
		}
		c = dahdi_new(p, AST_STATE_RING, 1, SUB_REAL, DAHDI_LAW_ALAW, 0);
		if (c) {
			dahdi_r2_update_monitor_count(p->mfcr2, 0);
			/* chan_dahdi will take care of reading from now on in the PBX thread, tell the
			   library to forget about it */
			openr2_chan_disable_read(r2chan);
			return;
		}
		ast_log(LOG_WARNING, "Unable to create PBX channel in DAHDI channel %d\n", p->channel);
		/* failed to create the channel, bail out and report it as an out of order line */
		openr2_chan_disconnect_call(r2chan, OR2_CAUSE_OUT_OF_ORDER);
		return;
	}
	/* this is an outgoing call, no need to launch the PBX thread, most likely we're in one already */
	ast_log(LOG_NOTICE, "Call accepted on forward channel %d\n", p->channel);
	p->subs[SUB_REAL].needringing = 1;
	p->dialing = 0;
	/* chan_dahdi will take care of reading from now on in the PBX thread, tell the
	   library to forget about it */
	openr2_chan_disable_read(r2chan);
}

static void dahdi_r2_on_call_answered(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_log(LOG_DEBUG, "MFC/R2 call has been answered on chan %d\n", openr2_chan_get_number(r2chan));
	p->subs[SUB_REAL].needanswer = 1;
}

static void dahdi_r2_on_call_read(openr2_chan_t *r2chan, const unsigned char *buf, int buflen)
{
	/*ast_log(LOG_DEBUG, "Read data from dahdi channel %d\n", openr2_chan_get_number(r2chan));*/
}

static int dahdi_r2_cause_to_ast_cause(openr2_call_disconnect_cause_t cause)
{
	switch (cause) {
	case OR2_CAUSE_BUSY_NUMBER:
		return AST_CAUSE_BUSY;
	case OR2_CAUSE_NETWORK_CONGESTION:
		return AST_CAUSE_CONGESTION;
	case OR2_CAUSE_OUT_OF_ORDER:
		return AST_CAUSE_DESTINATION_OUT_OF_ORDER;
	case OR2_CAUSE_UNALLOCATED_NUMBER:
		return AST_CAUSE_UNREGISTERED;
	case OR2_CAUSE_NO_ANSWER:
		return AST_CAUSE_NO_ANSWER;
	case OR2_CAUSE_NORMAL_CLEARING:
		return AST_CAUSE_NORMAL_CLEARING;
	case OR2_CAUSE_UNSPECIFIED:
	default:
		return AST_CAUSE_NOTDEFINED;
	}
}

static void dahdi_r2_on_call_disconnect(openr2_chan_t *r2chan, openr2_call_disconnect_cause_t cause)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_verb(3, "MFC/R2 call disconnected on chan %d\n", openr2_chan_get_number(r2chan));
	ast_mutex_lock(&p->lock);
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		/* no owner, therefore we can't use dahdi_hangup to disconnect, do it right now */
		openr2_chan_disconnect_call(r2chan, OR2_CAUSE_NORMAL_CLEARING);
		return;
	}
	/* when we have an owner we don't call openr2_chan_disconnect_call here, that will
	   be done in dahdi_hangup */
	if (p->owner->_state == AST_STATE_UP) {
		p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
		ast_mutex_unlock(&p->lock);
	} else if (openr2_chan_get_direction(r2chan) == OR2_DIR_FORWARD) {
		/* being the forward side we must report what happened to the call to whoever requested it */
		switch (cause) {
		case OR2_CAUSE_BUSY_NUMBER:
			p->subs[SUB_REAL].needbusy = 1;
			break;
		case OR2_CAUSE_NETWORK_CONGESTION:
		case OR2_CAUSE_OUT_OF_ORDER:
		case OR2_CAUSE_UNALLOCATED_NUMBER:
		case OR2_CAUSE_NO_ANSWER:
		case OR2_CAUSE_UNSPECIFIED:
		case OR2_CAUSE_NORMAL_CLEARING:
			p->subs[SUB_REAL].needcongestion = 1;
			break;
		default:
			p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
		}
		ast_mutex_unlock(&p->lock);
	} else {
		ast_mutex_unlock(&p->lock);
		/* being the backward side and not UP yet, we only need to request hangup */
		/* TODO: what about doing this same thing when were AST_STATE_UP? */
		ast_queue_hangup_with_cause(p->owner, dahdi_r2_cause_to_ast_cause(cause));
	}
}

static void dahdi_r2_write_log(openr2_log_level_t level, char *logmessage)
{
	switch (level) {
	case OR2_LOG_NOTICE:
		ast_log(LOG_NOTICE, "%s", logmessage);
		break;
	case OR2_LOG_WARNING:
		ast_log(LOG_WARNING, "%s", logmessage);
		break;
	case OR2_LOG_ERROR:
		ast_log(LOG_ERROR, "%s", logmessage);
		break;
	case OR2_LOG_STACK_TRACE:
	case OR2_LOG_MF_TRACE:
	case OR2_LOG_CAS_TRACE:
	case OR2_LOG_DEBUG:
	case OR2_LOG_EX_DEBUG:
		ast_log(LOG_DEBUG, "%s", logmessage);
		break;
	default:
		ast_log(LOG_WARNING, "We should handle logging level %d here.\n", level);
		ast_log(LOG_DEBUG, "%s", logmessage);
		break;
	}
}

static void dahdi_r2_on_line_blocked(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_mutex_lock(&p->lock);
	p->remotelyblocked = 1;
	ast_mutex_unlock(&p->lock);
	ast_log(LOG_NOTICE, "Far end blocked on chan %d\n", openr2_chan_get_number(r2chan));
}

static void dahdi_r2_on_line_idle(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_mutex_lock(&p->lock);
	p->remotelyblocked = 0;
	ast_mutex_unlock(&p->lock);
	ast_log(LOG_NOTICE, "Far end unblocked on chan %d\n", openr2_chan_get_number(r2chan));
}

static void dahdi_r2_on_context_log(openr2_context_t *r2context, openr2_log_level_t level, const char *fmt, va_list ap)
	__attribute__((format (printf, 3, 0)));
static void dahdi_r2_on_context_log(openr2_context_t *r2context, openr2_log_level_t level, const char *fmt, va_list ap)
{
#define CONTEXT_TAG "Context - "
	char logmsg[256];
	char completemsg[sizeof(logmsg) + sizeof(CONTEXT_TAG) - 1];
	vsnprintf(logmsg, sizeof(logmsg), fmt, ap);
	snprintf(completemsg, sizeof(completemsg), CONTEXT_TAG "%s", logmsg);
	dahdi_r2_write_log(level, completemsg);
#undef CONTEXT_TAG
}

static void dahdi_r2_on_chan_log(openr2_chan_t *r2chan, openr2_log_level_t level, const char *fmt, va_list ap)
	__attribute__((format (printf, 3, 0)));
static void dahdi_r2_on_chan_log(openr2_chan_t *r2chan, openr2_log_level_t level, const char *fmt, va_list ap)
{
#define CHAN_TAG "Chan "
	char logmsg[256];
	char completemsg[sizeof(logmsg) + sizeof(CHAN_TAG) - 1];
	vsnprintf(logmsg, sizeof(logmsg), fmt, ap);
	snprintf(completemsg, sizeof(completemsg), CHAN_TAG "%d - %s", openr2_chan_get_number(r2chan), logmsg);
	dahdi_r2_write_log(level, completemsg);
}

static int dahdi_r2_on_dnis_digit_received(openr2_chan_t *r2chan, char digit)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	/* if 'immediate' is set, let's stop requesting DNIS */
	if (p->immediate) {
		return 0;
	}
	p->exten[p->mfcr2_dnis_index] = digit;
	p->rdnis[p->mfcr2_dnis_index] = digit;
	p->mfcr2_dnis_index++;
	p->exten[p->mfcr2_dnis_index] = 0;
	p->rdnis[p->mfcr2_dnis_index] = 0;
	/* if the DNIS is a match and cannot match more, stop requesting DNIS */
	if ((p->mfcr2_dnis_matched ||
	    (ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num) && (p->mfcr2_dnis_matched = 1))) &&
	    !ast_matchmore_extension(NULL, p->context, p->exten, 1, p->cid_num)) {
		return 0;
	}
	/* otherwise keep going */
	return 1;
}

static void dahdi_r2_on_ani_digit_received(openr2_chan_t *r2chan, char digit)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	p->cid_num[p->mfcr2_ani_index] = digit;
	p->cid_name[p->mfcr2_ani_index] = digit;
	p->mfcr2_ani_index++;
	p->cid_num[p->mfcr2_ani_index] = 0;
	p->cid_name[p->mfcr2_ani_index] = 0;
}

static openr2_event_interface_t dahdi_r2_event_iface = {
	.on_call_init = dahdi_r2_on_call_init,
	.on_call_offered = dahdi_r2_on_call_offered,
	.on_call_accepted = dahdi_r2_on_call_accepted,
	.on_call_answered = dahdi_r2_on_call_answered,
	.on_call_disconnect = dahdi_r2_on_call_disconnect,
	.on_call_end = dahdi_r2_on_call_end,
	.on_call_read = dahdi_r2_on_call_read,
	.on_hardware_alarm = dahdi_r2_on_hardware_alarm,
	.on_os_error = dahdi_r2_on_os_error,
	.on_protocol_error = dahdi_r2_on_protocol_error,
	.on_line_blocked = dahdi_r2_on_line_blocked,
	.on_line_idle = dahdi_r2_on_line_idle,
	/* cast seems to be needed to get rid of the annoying warning regarding format attribute  */
	.on_context_log = (openr2_handle_context_logging_func)dahdi_r2_on_context_log,
	.on_dnis_digit_received = dahdi_r2_on_dnis_digit_received,
	.on_ani_digit_received = dahdi_r2_on_ani_digit_received,
	/* so far we do nothing with billing pulses */
	.on_billing_pulse_received = NULL
};

static inline int16_t dahdi_r2_alaw_to_linear(uint8_t sample)
{
	return AST_ALAW(sample);
}

static inline uint8_t dahdi_r2_linear_to_alaw(int sample)
{
	return AST_LIN2A(sample);
}

static openr2_transcoder_interface_t dahdi_r2_transcode_iface = {
	dahdi_r2_alaw_to_linear,
	dahdi_r2_linear_to_alaw
};

#endif /* HAVE_OPENR2 */

static int restore_gains(struct dahdi_pvt *p);

static void swap_subs(struct dahdi_pvt *p, int a, int b)
{
	int tchan;
	int tinthreeway;
	struct ast_channel *towner;

	ast_debug(1, "Swapping %d and %d\n", a, b);

	tchan = p->subs[a].chan;
	towner = p->subs[a].owner;
	tinthreeway = p->subs[a].inthreeway;

	p->subs[a].chan = p->subs[b].chan;
	p->subs[a].owner = p->subs[b].owner;
	p->subs[a].inthreeway = p->subs[b].inthreeway;

	p->subs[b].chan = tchan;
	p->subs[b].owner = towner;
	p->subs[b].inthreeway = tinthreeway;

	if (p->subs[a].owner)
		ast_channel_set_fd(p->subs[a].owner, 0, p->subs[a].dfd);
	if (p->subs[b].owner)
		ast_channel_set_fd(p->subs[b].owner, 0, p->subs[b].dfd);
	wakeup_sub(p, a);
	wakeup_sub(p, b);
}

static int dahdi_open(char *fn)
{
	int fd;
	int isnum;
	int chan = 0;
	int bs;
	int x;
	isnum = 1;
	for (x = 0; x < strlen(fn); x++) {
		if (!isdigit(fn[x])) {
			isnum = 0;
			break;
		}
	}
	if (isnum) {
		chan = atoi(fn);
		if (chan < 1) {
			ast_log(LOG_WARNING, "Invalid channel number '%s'\n", fn);
			return -1;
		}
		fn = "/dev/dahdi/channel";
	}
	fd = open(fn, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to open '%s': %s\n", fn, strerror(errno));
		return -1;
	}
	if (chan) {
		if (ioctl(fd, DAHDI_SPECIFY, &chan)) {
			x = errno;
			close(fd);
			errno = x;
			ast_log(LOG_WARNING, "Unable to specify channel %d: %s\n", chan, strerror(errno));
			return -1;
		}
	}
	bs = READ_SIZE;
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs) == -1) {
		ast_log(LOG_WARNING, "Unable to set blocksize '%d': %s\n", bs,  strerror(errno));
		x = errno;
		close(fd);
		errno = x;
		return -1;
	}
	return fd;
}

static void dahdi_close(int fd)
{
	if (fd > 0)
		close(fd);
}

static void dahdi_close_sub(struct dahdi_pvt *chan_pvt, int sub_num)
{
	dahdi_close(chan_pvt->subs[sub_num].dfd);
	chan_pvt->subs[sub_num].dfd = -1;
}

#if defined(HAVE_PRI)
static void dahdi_close_pri_fd(struct dahdi_pri *pri, int fd_num)
{
	dahdi_close(pri->pri.fds[fd_num]);
	pri->pri.fds[fd_num] = -1;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
static void dahdi_close_ss7_fd(struct dahdi_ss7 *ss7, int fd_num)
{
	dahdi_close(ss7->fds[fd_num]);
	ss7->fds[fd_num] = -1;
}
#endif	/* defined(HAVE_SS7) */

static int dahdi_setlinear(int dfd, int linear)
{
	int res;
	res = ioctl(dfd, DAHDI_SETLINEAR, &linear);
	if (res)
		return res;
	return 0;
}


static int alloc_sub(struct dahdi_pvt *p, int x)
{
	struct dahdi_bufferinfo bi;
	int res;
	if (p->subs[x].dfd >= 0) {
		ast_log(LOG_WARNING, "%s subchannel of %d already in use\n", subnames[x], p->channel);
		return -1;
	}

	p->subs[x].dfd = dahdi_open("/dev/dahdi/pseudo");
	if (p->subs[x].dfd <= -1) {
		ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
		return -1;
	}

	res = ioctl(p->subs[x].dfd, DAHDI_GET_BUFINFO, &bi);
	if (!res) {
		bi.txbufpolicy = p->buf_policy;
		bi.rxbufpolicy = p->buf_policy;
		bi.numbufs = p->buf_no;
		res = ioctl(p->subs[x].dfd, DAHDI_SET_BUFINFO, &bi);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d: %s\n", x, strerror(errno));
		}
	} else
		ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d: %s\n", x, strerror(errno));

	if (ioctl(p->subs[x].dfd, DAHDI_CHANNO, &p->subs[x].chan) == 1) {
		ast_log(LOG_WARNING, "Unable to get channel number for pseudo channel on FD %d: %s\n", p->subs[x].dfd, strerror(errno));
		dahdi_close_sub(p, x);
		p->subs[x].dfd = -1;
		return -1;
	}
	ast_debug(1, "Allocated %s subchannel on FD %d channel %d\n", subnames[x], p->subs[x].dfd, p->subs[x].chan);
	return 0;
}

static int unalloc_sub(struct dahdi_pvt *p, int x)
{
	if (!x) {
		ast_log(LOG_WARNING, "Trying to unalloc the real channel %d?!?\n", p->channel);
		return -1;
	}
	ast_debug(1, "Released sub %d of channel %d\n", x, p->channel);
	dahdi_close_sub(p, x);
	p->subs[x].linear = 0;
	p->subs[x].chan = 0;
	p->subs[x].owner = NULL;
	p->subs[x].inthreeway = 0;
	p->polarity = POLARITY_IDLE;
	memset(&p->subs[x].curconf, 0, sizeof(p->subs[x].curconf));
	return 0;
}

static int digit_to_dtmfindex(char digit)
{
	if (isdigit(digit))
		return DAHDI_TONE_DTMF_BASE + (digit - '0');
	else if (digit >= 'A' && digit <= 'D')
		return DAHDI_TONE_DTMF_A + (digit - 'A');
	else if (digit >= 'a' && digit <= 'd')
		return DAHDI_TONE_DTMF_A + (digit - 'a');
	else if (digit == '*')
		return DAHDI_TONE_DTMF_s;
	else if (digit == '#')
		return DAHDI_TONE_DTMF_p;
	else
		return -1;
}

static int dahdi_digit_begin(struct ast_channel *chan, char digit)
{
	struct dahdi_pvt *pvt;
	int idx;
	int dtmf = -1;
	int res;

	pvt = chan->tech_pvt;

	ast_mutex_lock(&pvt->lock);

	idx = dahdi_get_index(chan, pvt, 0);

	if ((idx != SUB_REAL) || !pvt->owner)
		goto out;

#ifdef HAVE_PRI
	if (pvt->sig == SIG_PRI || pvt->sig == SIG_BRI || pvt->sig == SIG_BRI_PTMP) {
		res = sig_pri_digit_begin(pvt->sig_pvt, chan, digit);
		if (!res)
			goto out;
	}
#endif
	if ((dtmf = digit_to_dtmfindex(digit)) == -1)
		goto out;

	if (pvt->pulse || ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SENDTONE, &dtmf)) {
		struct dahdi_dialoperation zo = {
			.op = DAHDI_DIAL_OP_APPEND,
		};

		zo.dialstr[0] = 'T';
		zo.dialstr[1] = digit;
		zo.dialstr[2] = '\0';
		if ((res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_DIAL, &zo)))
			ast_log(LOG_WARNING, "Couldn't dial digit %c: %s\n", digit, strerror(errno));
		else
			pvt->dialing = 1;
	} else {
		ast_debug(1, "Started VLDTMF digit '%c'\n", digit);
		pvt->dialing = 1;
		pvt->begindigit = digit;
	}

out:
	ast_mutex_unlock(&pvt->lock);

	return 0;
}

static int dahdi_digit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	struct dahdi_pvt *pvt;
	int res = 0;
	int idx;
	int x;

	pvt = chan->tech_pvt;

	ast_mutex_lock(&pvt->lock);

	idx = dahdi_get_index(chan, pvt, 0);

	if ((idx != SUB_REAL) || !pvt->owner || pvt->pulse)
		goto out;

#ifdef HAVE_PRI
	/* This means that the digit was already sent via PRI signalling */
	if (((pvt->sig == SIG_PRI) || (pvt->sig == SIG_BRI) || (pvt->sig == SIG_BRI_PTMP))
			&& !pvt->begindigit)
		goto out;
#endif

	if (pvt->begindigit) {
		x = -1;
		ast_debug(1, "Ending VLDTMF digit '%c'\n", digit);
		res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SENDTONE, &x);
		pvt->dialing = 0;
		pvt->begindigit = 0;
	}

out:
	ast_mutex_unlock(&pvt->lock);

	return res;
}

static const char * const events[] = {
	"No event",
	"On hook",
	"Ring/Answered",
	"Wink/Flash",
	"Alarm",
	"No more alarm",
	"HDLC Abort",
	"HDLC Overrun",
	"HDLC Bad FCS",
	"Dial Complete",
	"Ringer On",
	"Ringer Off",
	"Hook Transition Complete",
	"Bits Changed",
	"Pulse Start",
	"Timer Expired",
	"Timer Ping",
	"Polarity Reversal",
	"Ring Begin",
};

static struct {
	int alarm;
	char *name;
} alarms[] = {
	{ DAHDI_ALARM_RED, "Red Alarm" },
	{ DAHDI_ALARM_YELLOW, "Yellow Alarm" },
	{ DAHDI_ALARM_BLUE, "Blue Alarm" },
	{ DAHDI_ALARM_RECOVER, "Recovering" },
	{ DAHDI_ALARM_LOOPBACK, "Loopback" },
	{ DAHDI_ALARM_NOTOPEN, "Not Open" },
	{ DAHDI_ALARM_NONE, "None" },
};

static char *alarm2str(int alm)
{
	int x;
	for (x = 0; x < ARRAY_LEN(alarms); x++) {
		if (alarms[x].alarm & alm)
			return alarms[x].name;
	}
	return alm ? "Unknown Alarm" : "No Alarm";
}

static const char *event2str(int event)
{
	static char buf[256];
	if ((event < (ARRAY_LEN(events))) && (event > -1))
		return events[event];
	sprintf(buf, "Event %d", event); /* safe */
	return buf;
}

static char *dahdi_sig2str(int sig)
{
	static char buf[256];
	switch (sig) {
	case SIG_EM:
		return "E & M Immediate";
	case SIG_EMWINK:
		return "E & M Wink";
	case SIG_EM_E1:
		return "E & M E1";
	case SIG_FEATD:
		return "Feature Group D (DTMF)";
	case SIG_FEATDMF:
		return "Feature Group D (MF)";
	case SIG_FEATDMF_TA:
		return "Feature Groud D (MF) Tandem Access";
	case SIG_FEATB:
		return "Feature Group B (MF)";
	case SIG_E911:
		return "E911 (MF)";
	case SIG_FGC_CAMA:
		return "FGC/CAMA (Dialpulse)";
	case SIG_FGC_CAMAMF:
		return "FGC/CAMA (MF)";
	case SIG_FXSLS:
		return "FXS Loopstart";
	case SIG_FXSGS:
		return "FXS Groundstart";
	case SIG_FXSKS:
		return "FXS Kewlstart";
	case SIG_FXOLS:
		return "FXO Loopstart";
	case SIG_FXOGS:
		return "FXO Groundstart";
	case SIG_FXOKS:
		return "FXO Kewlstart";
	case SIG_PRI:
		return "ISDN PRI";
	case SIG_BRI:
		return "ISDN BRI Point to Point";
	case SIG_BRI_PTMP:
		return "ISDN BRI Point to MultiPoint";
	case SIG_SS7:
		return "SS7";
	case SIG_MFCR2:
		return "MFC/R2";
	case SIG_SF:
		return "SF (Tone) Immediate";
	case SIG_SFWINK:
		return "SF (Tone) Wink";
	case SIG_SF_FEATD:
		return "SF (Tone) with Feature Group D (DTMF)";
	case SIG_SF_FEATDMF:
		return "SF (Tone) with Feature Group D (MF)";
	case SIG_SF_FEATB:
		return "SF (Tone) with Feature Group B (MF)";
	case 0:
		return "Pseudo";
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d", sig);
		return buf;
	}
}

int analog_lib_handles(int signalling, int radio, int oprmode)
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

	if (radio)
		return 0;

	if (oprmode)
		return 0;

	return 1;
}

#define sig2str dahdi_sig2str

static int conf_add(struct dahdi_pvt *p, struct dahdi_subchannel *c, int idx, int slavechannel)
{
	/* If the conference already exists, and we're already in it
	   don't bother doing anything */
	struct dahdi_confinfo zi;

	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;

	if (slavechannel > 0) {
		/* If we have only one slave, do a digital mon */
		zi.confmode = DAHDI_CONF_DIGITALMON;
		zi.confno = slavechannel;
	} else {
		if (!idx) {
			/* Real-side and pseudo-side both participate in conference */
			zi.confmode = DAHDI_CONF_REALANDPSEUDO | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER |
				DAHDI_CONF_PSEUDO_TALKER | DAHDI_CONF_PSEUDO_LISTENER;
		} else
			zi.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
		zi.confno = p->confno;
	}
	if ((zi.confno == c->curconf.confno) && (zi.confmode == c->curconf.confmode))
		return 0;
	if (c->dfd < 0)
		return 0;
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to add %d to conference %d/%d: %s\n", c->dfd, zi.confmode, zi.confno, strerror(errno));
		return -1;
	}
	if (slavechannel < 1) {
		p->confno = zi.confno;
	}
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	ast_debug(1, "Added %d to conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	return 0;
}

static int isourconf(struct dahdi_pvt *p, struct dahdi_subchannel *c)
{
	/* If they're listening to our channel, they're ours */
	if ((p->channel == c->curconf.confno) && (c->curconf.confmode == DAHDI_CONF_DIGITALMON))
		return 1;
	/* If they're a talker on our (allocated) conference, they're ours */
	if ((p->confno > 0) && (p->confno == c->curconf.confno) && (c->curconf.confmode & DAHDI_CONF_TALKER))
		return 1;
	return 0;
}

static int conf_del(struct dahdi_pvt *p, struct dahdi_subchannel *c, int idx)
{
	struct dahdi_confinfo zi;
	if (/* Can't delete if there's no dfd */
		(c->dfd < 0) ||
		/* Don't delete from the conference if it's not our conference */
		!isourconf(p, c)
		/* Don't delete if we don't think it's conferenced at all (implied) */
		) return 0;
	memset(&zi, 0, sizeof(zi));
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to drop %d from conference %d/%d: %s\n", c->dfd, c->curconf.confmode, c->curconf.confno, strerror(errno));
		return -1;
	}
	ast_debug(1, "Removed %d from conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	return 0;
}

static int isslavenative(struct dahdi_pvt *p, struct dahdi_pvt **out)
{
	int x;
	int useslavenative;
	struct dahdi_pvt *slave = NULL;
	/* Start out optimistic */
	useslavenative = 1;
	/* Update conference state in a stateless fashion */
	for (x = 0; x < 3; x++) {
		/* Any three-way calling makes slave native mode *definitely* out
		   of the question */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway)
			useslavenative = 0;
	}
	/* If we don't have any 3-way calls, check to see if we have
	   precisely one slave */
	if (useslavenative) {
		for (x = 0; x < MAX_SLAVES; x++) {
			if (p->slaves[x]) {
				if (slave) {
					/* Whoops already have a slave!  No
					   slave native and stop right away */
					slave = NULL;
					useslavenative = 0;
					break;
				} else {
					/* We have one slave so far */
					slave = p->slaves[x];
				}
			}
		}
	}
	/* If no slave, slave native definitely out */
	if (!slave)
		useslavenative = 0;
	else if (slave->law != p->law) {
		useslavenative = 0;
		slave = NULL;
	}
	if (out)
		*out = slave;
	return useslavenative;
}

static int reset_conf(struct dahdi_pvt *p)
{
	p->confno = -1;
	memset(&p->subs[SUB_REAL].curconf, 0, sizeof(p->subs[SUB_REAL].curconf));
	if (p->subs[SUB_REAL].dfd > -1) {
		struct dahdi_confinfo zi;

		memset(&zi, 0, sizeof(zi));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &zi))
			ast_log(LOG_WARNING, "Failed to reset conferencing on channel %d: %s\n", p->channel, strerror(errno));
	}
	return 0;
}

static int update_conf(struct dahdi_pvt *p)
{
	int needconf = 0;
	int x;
	int useslavenative;
	struct dahdi_pvt *slave = NULL;

	useslavenative = isslavenative(p, &slave);
	/* Start with the obvious, general stuff */
	for (x = 0; x < 3; x++) {
		/* Look for three way calls */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway) {
			conf_add(p, &p->subs[x], x, 0);
			needconf++;
		} else {
			conf_del(p, &p->subs[x], x);
		}
	}
	/* If we have a slave, add him to our conference now. or DAX
	   if this is slave native */
	for (x = 0; x < MAX_SLAVES; x++) {
		if (p->slaves[x]) {
			if (useslavenative)
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p));
			else {
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, 0);
				needconf++;
			}
		}
	}
	/* If we're supposed to be in there, do so now */
	if (p->inconference && !p->subs[SUB_REAL].inthreeway) {
		if (useslavenative)
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(slave));
		else {
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, 0);
			needconf++;
		}
	}
	/* If we have a master, add ourselves to his conference */
	if (p->master) {
		if (isslavenative(p->master, NULL)) {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p->master));
		} else {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, 0);
		}
	}
	if (!needconf) {
		/* Nobody is left (or should be left) in our conference.
		   Kill it. */
		p->confno = -1;
	}
	ast_debug(1, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);
	return 0;
}

static void dahdi_enable_ec(struct dahdi_pvt *p)
{
	int x;
	int res;
	if (!p)
		return;
	if (p->echocanon) {
		ast_debug(1, "Echo cancellation already on\n");
		return;
	}
	if (p->digital) {
		ast_debug(1, "Echo cancellation isn't required on digital connection\n");
		return;
	}
	if (p->echocancel.head.tap_length) {
		if ((p->sig == SIG_BRI) || (p->sig == SIG_BRI_PTMP) || (p->sig == SIG_PRI) || (p->sig == SIG_SS7)) {
			x = 1;
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x);
			if (res)
				ast_log(LOG_WARNING, "Unable to enable audio mode on channel %d (%s)\n", p->channel, strerror(errno));
		}
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_PARAMS, &p->echocancel);
		if (res) {
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d (%s)\n", p->channel, strerror(errno));
		} else {
			p->echocanon = 1;
			ast_debug(1, "Enabled echo cancellation on channel %d\n", p->channel);
		}
	} else
		ast_debug(1, "No echo cancellation requested\n");
}

static void dahdi_train_ec(struct dahdi_pvt *p)
{
	int x;
	int res;

	if (p && p->echocanon && p->echotraining) {
		x = p->echotraining;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOTRAIN, &x);
		if (res)
			ast_log(LOG_WARNING, "Unable to request echo training on channel %d: %s\n", p->channel, strerror(errno));
		else
			ast_debug(1, "Engaged echo training on channel %d\n", p->channel);
	} else {
		ast_debug(1, "No echo training requested\n");
	}
}

static void dahdi_disable_ec(struct dahdi_pvt *p)
{
	int res;

	if (p->echocanon) {
		struct dahdi_echocanparams ecp = { .tap_length = 0 };

		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_PARAMS, &ecp);

		if (res)
			ast_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d: %s\n", p->channel, strerror(errno));
		else
			ast_debug(1, "Disabled echo cancellation on channel %d\n", p->channel);
	}

	p->echocanon = 0;
}

static void fill_txgain(struct dahdi_gains *g, float gain, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++) {
			if (gain) {
				k = (int) (((float) AST_ALAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = AST_LIN2A(k);
			} else {
				g->txgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++) {
			if (gain) {
				k = (int) (((float) AST_MULAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = AST_LIN2MU(k);
			} else {
				g->txgain[j] = j;
			}
		}
		break;
	}
}

static void fill_rxgain(struct dahdi_gains *g, float gain, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++) {
			if (gain) {
				k = (int) (((float) AST_ALAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->rxgain[j] = AST_LIN2A(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++) {
			if (gain) {
				k = (int) (((float) AST_MULAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->rxgain[j] = AST_LIN2MU(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	}
}

static int set_actual_txgain(int fd, int chan, float gain, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	g.chan = chan;
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		ast_debug(1, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_txgain(&g, gain, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_rxgain(int fd, int chan, float gain, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	g.chan = chan;
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		ast_debug(1, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_rxgain(&g, gain, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_gain(int fd, int chan, float rxgain, float txgain, int law)
{
	return set_actual_txgain(fd, chan, txgain, law) | set_actual_rxgain(fd, chan, rxgain, law);
}

static int bump_gains(struct dahdi_pvt *p)
{
	int res;

	/* Bump receive gain by value stored in cid_rxgain */
	res = set_actual_gain(p->subs[SUB_REAL].dfd, 0, p->rxgain + p->cid_rxgain, p->txgain, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to bump gain: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int restore_gains(struct dahdi_pvt *p)
{
	int res;

	res = set_actual_gain(p->subs[SUB_REAL].dfd, 0, p->rxgain, p->txgain, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static inline int dahdi_set_hook(int fd, int hs)
{
	int x, res;

	x = hs;
	res = ioctl(fd, DAHDI_HOOK, &x);

	if (res < 0) {
		if (errno == EINPROGRESS)
			return 0;
		ast_log(LOG_WARNING, "DAHDI hook failed returned %d (trying %d): %s\n", res, hs, strerror(errno));
		/* will expectedly fail if phone is off hook during operation, such as during a restart */
	}

	return res;
}

static inline int dahdi_confmute(struct dahdi_pvt *p, int muted)
{
	int x, y, res;
	x = muted;
	if ((p->sig == SIG_PRI) || (p->sig == SIG_SS7) || (p->sig == SIG_BRI) || (p->sig == SIG_BRI_PTMP)) {
		y = 1;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &y);
		if (res)
			ast_log(LOG_WARNING, "Unable to set audio mode on %d: %s\n", p->channel, strerror(errno));
	}
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_CONFMUTE, &x);
	if (res < 0)
		ast_log(LOG_WARNING, "DAHDI confmute(%d) failed on channel %d: %s\n", muted, p->channel, strerror(errno));
	return res;
}

static int save_conference(struct dahdi_pvt *p)
{
	struct dahdi_confinfo c;
	int res;
	if (p->saveconf.confmode) {
		ast_log(LOG_WARNING, "Can't save conference -- already in use\n");
		return -1;
	}
	p->saveconf.chan = 0;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GETCONF, &p->saveconf);
	if (res) {
		ast_log(LOG_WARNING, "Unable to get conference info: %s\n", strerror(errno));
		p->saveconf.confmode = 0;
		return -1;
	}
	memset(&c, 0, sizeof(c));
	c.confmode = DAHDI_CONF_NORMAL;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "Unable to set conference info: %s\n", strerror(errno));
		return -1;
	}
	ast_debug(1, "Disabled conferencing\n");
	return 0;
}

static int restore_conference(struct dahdi_pvt *p)
{
	int res;
	if (p->saveconf.confmode) {
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &p->saveconf);
		p->saveconf.confmode = 0;
		if (res) {
			ast_log(LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
			return -1;
		}
	}
	ast_debug(1, "Restored conferencing\n");
	return 0;
}

static int send_callerid(struct dahdi_pvt *p);

static int send_cwcidspill(struct dahdi_pvt *p)
{
	p->callwaitcas = 0;
	p->cidcwexpire = 0;
	if (!(p->cidspill = ast_malloc(MAX_CALLERID_SIZE)))
		return -1;
	p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, p->callwait_name, p->callwait_num, AST_LAW(p));
	/* Make sure we account for the end */
	p->cidlen += READ_SIZE * 4;
	p->cidpos = 0;
	send_callerid(p);
	ast_verb(3, "CPE supports Call Waiting Caller*ID.  Sending '%s/%s'\n", p->callwait_name, p->callwait_num);
	return 0;
}

static int has_voicemail(struct dahdi_pvt *p)
{
	int new_msgs;
	struct ast_event *event;
	char *mailbox, *context;

	mailbox = context = ast_strdupa(p->mailbox);
	strsep(&context, "@");
	if (ast_strlen_zero(context))
		context = "default";

	event = ast_event_get_cached(AST_EVENT_MWI,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
		AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
		AST_EVENT_IE_END);

	if (event) {
		new_msgs = ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS);
		ast_event_destroy(event);
	} else
		new_msgs = ast_app_has_voicemail(p->mailbox, NULL);

	return new_msgs;
}



static int send_callerid(struct dahdi_pvt *p)
{
	/* Assumes spill in p->cidspill, p->cidlen in length and we're p->cidpos into it */
	int res;
	/* Take out of linear mode if necessary */
	if (p->subs[SUB_REAL].linear) {
		p->subs[SUB_REAL].linear = 0;
		dahdi_setlinear(p->subs[SUB_REAL].dfd, 0);
	}
	while (p->cidpos < p->cidlen) {
		res = write(p->subs[SUB_REAL].dfd, p->cidspill + p->cidpos, p->cidlen - p->cidpos);
		if (res < 0) {
			if (errno == EAGAIN)
				return 0;
			else {
				ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
				return -1;
			}
		}
		if (!res)
			return 0;
		p->cidpos += res;
	}
	ast_free(p->cidspill);
	p->cidspill = NULL;
	if (p->callwaitcas) {
		/* Wait for CID/CW to expire */
		p->cidcwexpire = CIDCW_EXPIRE_SAMPLES;
	} else
		restore_conference(p);
	return 0;
}

static int dahdi_callwait(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill) {
		ast_log(LOG_WARNING, "Spill already exists?!?\n");
		ast_free(p->cidspill);
	}
	if (!(p->cidspill = ast_malloc(2400 /* SAS */ + 680 /* CAS */ + READ_SIZE * 4)))
		return -1;
	save_conference(p);
	/* Silence */
	memset(p->cidspill, 0x7f, 2400 + 600 + READ_SIZE * 4);
	if (!p->callwaitrings && p->callwaitingcallerid) {
		ast_gen_cas(p->cidspill, 1, 2400 + 680, AST_LAW(p));
		p->callwaitcas = 1;
		p->cidlen = 2400 + 680 + READ_SIZE * 4;
	} else {
		ast_gen_cas(p->cidspill, 1, 2400, AST_LAW(p));
		p->callwaitcas = 0;
		p->cidlen = 2400 + READ_SIZE * 4;
	}
	p->cidpos = 0;
	send_callerid(p);

	return 0;
}

#if defined(HAVE_SS7)
static unsigned char cid_pres2ss7pres(int cid_pres)
{
	 return (cid_pres >> 5) & 0x03;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static unsigned char cid_pres2ss7screen(int cid_pres)
{
	return cid_pres & 0x03;
}
#endif	/* defined(HAVE_SS7) */

static int dahdi_call(struct ast_channel *ast, char *rdest, int timeout)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	int x, res, mysig;
	char dest[256]; /* must be same length as p->dialdest */
#ifdef HAVE_SS7
	char *c, *l;
#endif
	ast_mutex_lock(&p->lock);
	ast_copy_string(dest, rdest, sizeof(dest));
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));
	if ((ast->_state == AST_STATE_BUSY)) {
		p->subs[SUB_REAL].needbusy = 1;
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "dahdi_call called on %s, neither down nor reserved\n", ast->name);
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	p->waitingfordt.tv_sec = 0;
	p->dialednone = 0;
	if ((p->radio || (p->oprmode < 0)))  /* if a radio channel, up immediately */
	{
		/* Special pseudo -- automatically up */
		ast_setstate(ast, AST_STATE_UP);
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	x = DAHDI_FLUSH_READ | DAHDI_FLUSH_WRITE;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
	if (res)
		ast_log(LOG_WARNING, "Unable to flush input on channel %d: %s\n", p->channel, strerror(errno));
	p->outgoing = 1;

	set_actual_gain(p->subs[SUB_REAL].dfd, 0, p->rxgain, p->txgain, p->law);
	
#ifdef HAVE_PRI
	if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
		struct dahdi_params ps;
		memset(&ps, 0, sizeof(ps));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps)) {
			ast_log(LOG_ERROR, "Could not get params\n");
		}
		res = sig_pri_call(p->sig_pvt, ast, rdest, timeout, (ps.curlaw == DAHDI_LAW_MULAW) ? PRI_LAYER_1_ULAW : PRI_LAYER_1_ALAW);
		ast_mutex_unlock(&p->lock);
		return res;
	}
#endif

	/* Set the ring cadence */
	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;
	switch (mysig) {
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		if (p->owner == ast) {
			/* Choose proper cadence */
			if ((p->distinctivering > 0) && (p->distinctivering <= num_cadence)) {
				if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCADENCE, &cadences[p->distinctivering - 1]))
					ast_log(LOG_WARNING, "Unable to set distinctive ring cadence %d on '%s': %s\n", p->distinctivering, ast->name, strerror(errno));
				p->cidrings = cidrings[p->distinctivering - 1];
			} else {
				if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCADENCE, NULL))
					ast_log(LOG_WARNING, "Unable to reset default ring on '%s': %s\n", ast->name, strerror(errno));
				p->cidrings = p->sendcalleridafter;
			}
		}
		break;
	}

	/* If this is analog signalling we can exit here */
	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		p->callwaitrings = 0;
		res = analog_call(p->sig_pvt, ast, rdest, timeout);
		ast_mutex_unlock(&p->lock);
		return res;
	}

	switch (mysig) {
	case 0:
		/* Special pseudo -- automatically up*/
		ast_setstate(ast, AST_STATE_UP);
		break;
	case SIG_SS7:
	case SIG_MFCR2:
		/* We'll get it in a moment -- but use dialdest to store pre-setup_ack digits */
		p->dialdest[0] = '\0';
		p->dialing = 1;
		break;
	default:
		ast_debug(1, "not yet implemented\n");
		ast_mutex_unlock(&p->lock);
		return -1;
	}
#ifdef HAVE_SS7
	if (p->ss7) {
		char ss7_called_nai;
		int called_nai_strip;
		char ss7_calling_nai;
		int calling_nai_strip;
		const char *charge_str = NULL;
		const char *gen_address = NULL;
		const char *gen_digits = NULL;
		const char *gen_dig_type = NULL;
		const char *gen_dig_scheme = NULL;
		const char *gen_name = NULL;
		const char *jip_digits = NULL;
		const char *lspi_ident = NULL;
		const char *rlt_flag = NULL;
		const char *call_ref_id = NULL;
		const char *call_ref_pc = NULL;
		const char *send_far = NULL;

		c = strchr(dest, '/');
		if (c) {
			c++;
		} else {
			c = "";
		}
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			ast_mutex_unlock(&p->lock);
			return -1;
		}

		if (!p->hidecallerid) {
			l = ast->connected.id.number;
		} else {
			l = NULL;
		}

		if (ss7_grab(p, p->ss7)) {
			ast_log(LOG_WARNING, "Failed to grab SS7!\n");
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		p->digital = IS_DIGITAL(ast->transfercapability);
		p->ss7call = isup_new_call(p->ss7->ss7);

		if (!p->ss7call) {
			ss7_rel(p->ss7);
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "Unable to allocate new SS7 call!\n");
			return -1;
		}

		called_nai_strip = 0;
 		ss7_called_nai = p->ss7->called_nai;
		if (ss7_called_nai == SS7_NAI_DYNAMIC) { /* compute dynamically */
 			if (strncmp(c + p->stripmsd, p->ss7->internationalprefix, strlen(p->ss7->internationalprefix)) == 0) {
 				called_nai_strip = strlen(p->ss7->internationalprefix);
 				ss7_called_nai = SS7_NAI_INTERNATIONAL;
 			} else if (strncmp(c + p->stripmsd, p->ss7->nationalprefix, strlen(p->ss7->nationalprefix)) == 0) {
				called_nai_strip = strlen(p->ss7->nationalprefix);
 				ss7_called_nai = SS7_NAI_NATIONAL;
 			} else {
				ss7_called_nai = SS7_NAI_SUBSCRIBER;
 			}
 		}
 		isup_set_called(p->ss7call, c + p->stripmsd + called_nai_strip, ss7_called_nai, p->ss7->ss7);

		calling_nai_strip = 0;
		ss7_calling_nai = p->ss7->calling_nai;
		if ((l != NULL) && (ss7_calling_nai == SS7_NAI_DYNAMIC)) { /* compute dynamically */
			if (strncmp(l, p->ss7->internationalprefix, strlen(p->ss7->internationalprefix)) == 0) {
				calling_nai_strip = strlen(p->ss7->internationalprefix);
				ss7_calling_nai = SS7_NAI_INTERNATIONAL;
			} else if (strncmp(l, p->ss7->nationalprefix, strlen(p->ss7->nationalprefix)) == 0) {
				calling_nai_strip = strlen(p->ss7->nationalprefix);
				ss7_calling_nai = SS7_NAI_NATIONAL;
			} else {
				ss7_calling_nai = SS7_NAI_SUBSCRIBER;
			}
		}
		isup_set_calling(p->ss7call, l ? (l + calling_nai_strip) : NULL, ss7_calling_nai,
			p->use_callingpres ? cid_pres2ss7pres(ast->connected.id.number_presentation) : (l ? SS7_PRESENTATION_ALLOWED : SS7_PRESENTATION_RESTRICTED),
			p->use_callingpres ? cid_pres2ss7screen(ast->connected.id.number_presentation) : SS7_SCREENING_USER_PROVIDED );

		isup_set_oli(p->ss7call, ast->connected.ani2);
		isup_init_call(p->ss7->ss7, p->ss7call, p->cic, p->dpc);

		ast_channel_lock(ast);
		/* Set the charge number if it is set */
		charge_str = pbx_builtin_getvar_helper(ast, "SS7_CHARGE_NUMBER");
		if (charge_str)
			isup_set_charge(p->ss7call, charge_str, SS7_ANI_CALLING_PARTY_SUB_NUMBER, 0x10);

		gen_address = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_ADDRESS");
		if (gen_address)
			isup_set_gen_address(p->ss7call, gen_address, p->gen_add_nai,p->gen_add_pres_ind, p->gen_add_num_plan,p->gen_add_type); /* need to add some types here for NAI,PRES,TYPE */

		gen_digits = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_DIGITS");
		gen_dig_type = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_DIGTYPE");
		gen_dig_scheme = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_DIGSCHEME");
		if (gen_digits)
			isup_set_gen_digits(p->ss7call, gen_digits, atoi(gen_dig_type), atoi(gen_dig_scheme));

		gen_name = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_NAME");
		if (gen_name)
			isup_set_generic_name(p->ss7call, gen_name, GEN_NAME_TYPE_CALLING_NAME, GEN_NAME_AVAIL_AVAILABLE, GEN_NAME_PRES_ALLOWED);

		jip_digits = pbx_builtin_getvar_helper(ast, "SS7_JIP");
		if (jip_digits)
			isup_set_jip_digits(p->ss7call, jip_digits);

		lspi_ident = pbx_builtin_getvar_helper(ast, "SS7_LSPI_IDENT");
		if (lspi_ident)
			isup_set_lspi(p->ss7call, lspi_ident, 0x18, 0x7, 0x00);

		rlt_flag = pbx_builtin_getvar_helper(ast, "SS7_RLT_ON");
		if ((rlt_flag) && ((strncmp("NO", rlt_flag, strlen(rlt_flag))) != 0 )) {
			isup_set_lspi(p->ss7call, rlt_flag, 0x18, 0x7, 0x00); /* Setting for Nortel DMS-250/500 */
		}

		call_ref_id = pbx_builtin_getvar_helper(ast, "SS7_CALLREF_IDENT");
		call_ref_pc = pbx_builtin_getvar_helper(ast, "SS7_CALLREF_PC");
		if (call_ref_id && call_ref_pc) {
			isup_set_callref(p->ss7call, atoi(call_ref_id),
					 call_ref_pc ? atoi(call_ref_pc) : 0);
		}

		send_far = pbx_builtin_getvar_helper(ast, "SS7_SEND_FAR");
		if ((send_far) && ((strncmp("NO", send_far, strlen(send_far))) != 0 ))
			(isup_far(p->ss7->ss7, p->ss7call));

		ast_channel_unlock(ast);

		isup_iam(p->ss7->ss7, p->ss7call);
		ast_setstate(ast, AST_STATE_DIALING);
		ss7_rel(p->ss7);
	}
#endif /* HAVE_SS7 */
#ifdef HAVE_OPENR2
	if (p->mfcr2) {
		openr2_calling_party_category_t chancat;
		int callres = 0;

		c = strchr(dest, '/');
		if (c) {
			c++;
		} else {
			c = "";
		}
		if (!p->hidecallerid) {
			l = ast->cid.cid_num;
		} else {
			l = NULL;
		}
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		p->dialing = 1;
		ast_channel_lock(ast);
		chancat = dahdi_r2_get_channel_category(ast);
		ast_channel_unlock(ast);
		callres = openr2_chan_make_call(p->r2chan, l, (c + p->stripmsd), chancat);
		if (-1 == callres) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "unable to make new MFC/R2 call!\n");
			return -1;
		}
		ast_setstate(ast, AST_STATE_DIALING);
	}
#endif /* HAVE_OPENR2 */
	ast_mutex_unlock(&p->lock);
	return 0;
}

static void destroy_dahdi_pvt(struct dahdi_pvt **pvt)
{
	struct dahdi_pvt *p = *pvt;
	/* Remove channel from the list */
	if (p->prev)
		p->prev->next = p->next;
	if (p->next)
		p->next->prev = p->prev;
	if (p->use_smdi)
		ast_smdi_interface_unref(p->smdi_iface);
	if (p->mwi_event_sub)
		ast_event_unsubscribe(p->mwi_event_sub);
	if (p->vars)
		ast_variables_destroy(p->vars);
	ast_mutex_destroy(&p->lock);
	dahdi_close_sub(p, SUB_REAL);
	if (p->owner)
		p->owner->tech_pvt = NULL;
	free(p);
	*pvt = NULL;
}

static int destroy_channel(struct dahdi_pvt *prev, struct dahdi_pvt *cur, int now)
{
	int owned = 0;
	int i = 0;

	if (!now) {
		if (cur->owner) {
			owned = 1;
		}

		for (i = 0; i < 3; i++) {
			if (cur->subs[i].owner) {
				owned = 1;
			}
		}
		if (!owned) {
			if (prev) {
				prev->next = cur->next;
				if (prev->next)
					prev->next->prev = prev;
				else
					ifend = prev;
			} else {
				iflist = cur->next;
				if (iflist)
					iflist->prev = NULL;
				else
					ifend = NULL;
			}
			destroy_dahdi_pvt(&cur);
		}
	} else {
		if (prev) {
			prev->next = cur->next;
			if (prev->next)
				prev->next->prev = prev;
			else
				ifend = prev;
		} else {
			iflist = cur->next;
			if (iflist)
				iflist->prev = NULL;
			else
				ifend = NULL;
		}
		destroy_dahdi_pvt(&cur);
	}
	return 0;
}

static void destroy_all_channels(void)
{
	int x;
	struct dahdi_pvt *p, *pl;

	while (num_restart_pending) {
		usleep(1);
	}

	ast_mutex_lock(&iflock);
	/* Destroy all the interfaces and free their memory */
	p = iflist;
	while (p) {
		/* Free any callerid */
		if (p->cidspill)
			ast_free(p->cidspill);
		pl = p;
		p = p->next;
		x = pl->channel;
#ifdef HAVE_PRI_SERVICE_MESSAGES
		{
			char db_chan_name[20], db_answer[5], state;
			int why = -1;

			snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, pl->span, x);
			if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
				sscanf(db_answer, "%c:%d", &state, &why);
			}
			if (!why) {
				/* SRVST persistence is not required */
				ast_db_del(db_chan_name, SRVST_DBKEY);
			}
		}
#endif
		/* Free associated memory */
		if (pl)
			destroy_dahdi_pvt(&pl);
		ast_verb(3, "Unregistered channel %d\n", x);
	}
	iflist = NULL;
	ifcount = 0;
	ast_mutex_unlock(&iflock);
}

#if defined(HAVE_PRI)
static char *dahdi_send_keypad_facility_app = "DAHDISendKeypadFacility";

static int dahdi_send_keypad_facility_exec(struct ast_channel *chan, const char *digits)
{
	/* Data will be our digit string */
	struct dahdi_pvt *p;

	if (ast_strlen_zero(digits)) {
		ast_debug(1, "No digit string sent to application!\n");
		return -1;
	}

	p = (struct dahdi_pvt *)chan->tech_pvt;

	if (!p) {
		ast_debug(1, "Unable to find technology private\n");
		return -1;
	}

	pri_send_keypad_facility_exec(p->sig_pvt, digits);

	return 0;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_PROG_W_CAUSE)
static char *dahdi_send_callrerouting_facility_app = "DAHDISendCallreroutingFacility";

static int dahdi_send_callrerouting_facility_exec(struct ast_channel *chan, const char *data)
{
	/* Data will be our digit string */
	struct dahdi_pvt *p;
	char *parse;
	int res = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(destination);
		AST_APP_ARG(original);
		AST_APP_ARG(reason);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_DEBUG, "No data sent to application!\n");
		return -1;
	}

	p = (struct dahdi_pvt *)chan->tech_pvt;

	if (!p) {
		ast_log(LOG_DEBUG, "Unable to find technology private\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.destination)) {
		ast_log(LOG_WARNING, "callrerouting facility requires at least destination number argument\n");
		return -1;
	}

	if (ast_strlen_zero(args.original)) {
		ast_log(LOG_WARNING, "Callrerouting Facility without original called number argument\n");
		args.original = NULL;
	}

	if (ast_strlen_zero(args.reason)) {
		ast_log(LOG_NOTICE, "Callrerouting Facility without diversion reason argument, defaulting to unknown\n");
		args.reason = NULL;
	}

	pri_send_callrerouting_facility_exec(p->sig_pvt, chan->_state, args.destination, args.original, args.reason);

	return res;
}
#endif	/* defined(HAVE_PRI_PROG_W_CAUSE) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_OPENR2)
static const char * const dahdi_accept_r2_call_app = "DAHDIAcceptR2Call";

static int dahdi_accept_r2_call_exec(struct ast_channel *chan, const char *data)
{
	/* data is whether to accept with charge or no charge */
	openr2_call_mode_t accept_mode;
	int res, timeout, maxloops;
	struct ast_frame *f;
	struct dahdi_pvt *p;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(charge);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_DEBUG, "No data sent to application!\n");
		return -1;
	}

	if (chan->tech != &dahdi_tech) {
		ast_log(LOG_DEBUG, "Only DAHDI technology accepted!\n");
		return -1;
	}

	p = (struct dahdi_pvt *)chan->tech_pvt;
	if (!p) {
		ast_log(LOG_DEBUG, "Unable to find technology private!\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.charge)) {
		ast_log(LOG_WARNING, "DAHDIAcceptR2Call requires 'yes' or 'no' for the charge parameter\n");
		return -1;
	}

	ast_mutex_lock(&p->lock);
	if (!p->mfcr2 || !p->mfcr2call) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_DEBUG, "Channel %s does not seems to be an R2 active channel!\n", chan->name);
		return -1;
	}

	if (p->mfcr2_call_accepted) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_DEBUG, "MFC/R2 call already accepted on channel %s!\n", chan->name);
		return 0;
	}
	accept_mode = ast_true(args.charge) ? OR2_CALL_WITH_CHARGE : OR2_CALL_NO_CHARGE;
	if (openr2_chan_accept_call(p->r2chan, accept_mode)) {
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_WARNING, "Failed to accept MFC/R2 call!\n");
		return -1;
	}
	ast_mutex_unlock(&p->lock);

	res = 0;
	timeout = 100;
	maxloops = 50; /* wait up to 5 seconds */
	/* we need to read() until the call is accepted */
	while (maxloops > 0) {
		maxloops--;
		if (ast_check_hangup(chan)) {
			break;
		}
		res = ast_waitfor(chan, timeout);
		if (res < 0) {
			ast_log(LOG_DEBUG, "ast_waitfor failed on channel %s, going out ...\n", chan->name);
			res = -1;
			break;
		}
		if (res == 0) {
			continue;
		}
		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_DEBUG, "No frame read on channel %s, going out ...\n", chan->name);
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_CONTROL && f->subclass == AST_CONTROL_HANGUP) {
			ast_log(LOG_DEBUG, "Got HANGUP frame on channel %s, going out ...\n", chan->name);
			ast_frfree(f);
			res = -1;
			break;
		}
		ast_frfree(f);
		ast_mutex_lock(&p->lock);
		if (p->mfcr2_call_accepted) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_DEBUG, "Accepted MFC/R2 call!\n");
			break;
		}
		ast_mutex_unlock(&p->lock);
	}
	if (res == -1) {
		ast_log(LOG_WARNING, "Failed to accept MFC/R2 call!\n");
	}
	return res;
}

static openr2_call_disconnect_cause_t dahdi_ast_cause_to_r2_cause(int cause)
{
	openr2_call_disconnect_cause_t r2cause = OR2_CAUSE_NORMAL_CLEARING;
	switch (cause) {
	case AST_CAUSE_USER_BUSY:
	case AST_CAUSE_CALL_REJECTED:
	case AST_CAUSE_INTERWORKING: /* I don't know wtf is this but is used sometimes when ekiga rejects a call */
		r2cause = OR2_CAUSE_BUSY_NUMBER;
		break;

	case AST_CAUSE_NORMAL_CIRCUIT_CONGESTION:
	case AST_CAUSE_SWITCH_CONGESTION:
		r2cause = OR2_CAUSE_NETWORK_CONGESTION;
		break;

	case AST_CAUSE_UNALLOCATED:
		r2cause = OR2_CAUSE_UNALLOCATED_NUMBER;
		break;

	case AST_CAUSE_NETWORK_OUT_OF_ORDER:
	case AST_CAUSE_DESTINATION_OUT_OF_ORDER:
		r2cause = OR2_CAUSE_OUT_OF_ORDER;
		break;

	case AST_CAUSE_NO_ANSWER:
	case AST_CAUSE_NO_USER_RESPONSE:
		r2cause = OR2_CAUSE_NO_ANSWER;
		break;

	default:
		r2cause = OR2_CAUSE_NORMAL_CLEARING;
		break;
	}
	ast_log(LOG_DEBUG, "ast cause %d resulted in openr2 cause %d/%s\n",
			cause, r2cause, openr2_proto_get_disconnect_string(r2cause));
	return r2cause;
}
#endif

static int dahdi_hangup(struct ast_channel *ast)
{
	int res = 0;
	int idx,x;
	/*static int restore_gains(struct dahdi_pvt *p);*/
	struct dahdi_pvt *p = ast->tech_pvt;
	struct dahdi_pvt *tmp = NULL;
	struct dahdi_pvt *prev = NULL;
	struct dahdi_params par;

	ast_debug(1, "dahdi_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		ast_mutex_lock(&p->lock);

		dahdi_confmute(p, 0);
		restore_gains(p);
		p->ignoredtmf = 0;

		if (p->bufferoverrideinuse) {
			/* faxbuffers are in use, revert them */
			struct dahdi_bufferinfo bi = {
				.txbufpolicy = p->buf_policy,
				.rxbufpolicy = p->buf_policy,
				.bufsize = p->bufsize,
				.numbufs = p->buf_no
			};
			int bpres;

			if ((bpres = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi)) < 0) {
				ast_log(LOG_WARNING, "Channel '%s' unable to revert buffer policy: %s\n", ast->name, strerror(errno));
			}
			p->bufferoverrideinuse = 0;
		}

		res = analog_hangup(p->sig_pvt, ast);

		goto hangup_out;
	}

	ast_mutex_lock(&p->lock);

	idx = dahdi_get_index(ast, p, 1);

#ifdef HAVE_PRI
	if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
		int law;
		x = 1;
		ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
		dahdi_confmute(p, 0);
		restore_gains(p);
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}
		dahdi_setlinear(p->subs[SUB_REAL].dfd, 0);
		law = DAHDI_LAW_DEFAULT;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (p->sig)
			dahdi_disable_ec(p);
		update_conf(p);
		reset_conf(p);
		sig_pri_hangup(p->sig_pvt, ast);
		p->subs[SUB_REAL].owner = NULL;
		p->owner = NULL;
		goto hangup_out;
	}
#endif

	if (p->sig == SIG_SS7) {
		x = 1;
		ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
	}

	x = 0;
	dahdi_confmute(p, 0);
	p->muting = 0;
	restore_gains(p);
	if (p->origcid_num) {
		ast_copy_string(p->cid_num, p->origcid_num, sizeof(p->cid_num));
		ast_free(p->origcid_num);
		p->origcid_num = NULL;
	}
	if (p->origcid_name) {
		ast_copy_string(p->cid_name, p->origcid_name, sizeof(p->cid_name));
		ast_free(p->origcid_name);
		p->origcid_name = NULL;
	}
	if (p->dsp)
		ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
	p->exten[0] = '\0';

	ast_debug(1, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, idx, p->subs[SUB_REAL].dfd, p->subs[SUB_CALLWAIT].dfd, p->subs[SUB_THREEWAY].dfd);
	p->ignoredtmf = 0;

	if (idx > -1) {
		/* Real channel, do some fixup */
		p->subs[idx].owner = NULL;
		p->subs[idx].needanswer = 0;
		p->subs[idx].needflash = 0;
		p->subs[idx].needringing = 0;
		p->subs[idx].needbusy = 0;
		p->subs[idx].needcongestion = 0;
		p->subs[idx].linear = 0;
		p->subs[idx].needcallerid = 0;
		p->polarity = POLARITY_IDLE;
		dahdi_setlinear(p->subs[idx].dfd, 0);
		if (idx == SUB_REAL) {
			if ((p->subs[SUB_CALLWAIT].dfd > -1) && (p->subs[SUB_THREEWAY].dfd > -1)) {
				ast_debug(1, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[SUB_CALLWAIT].inthreeway) {
					/* We had flipped over to answer a callwait and now it's gone */
					ast_debug(1, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					unalloc_sub(p, SUB_CALLWAIT);
					p->owner = NULL;
				} else {
					/* The three way hung up, but we still have a call wait */
					ast_debug(1, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					swap_subs(p, SUB_THREEWAY, SUB_REAL);
					unalloc_sub(p, SUB_THREEWAY);
					if (p->subs[SUB_REAL].inthreeway) {
						/* This was part of a three way call.  Immediately make way for
						   another call */
						ast_debug(1, "Call was complete, setting owner to former third call\n");
						p->owner = p->subs[SUB_REAL].owner;
					} else {
						/* This call hasn't been completed yet...  Set owner to NULL */
						ast_debug(1, "Call was incomplete, setting owner to NULL\n");
						p->owner = NULL;
					}
					p->subs[SUB_REAL].inthreeway = 0;
				}
			} else if (p->subs[SUB_CALLWAIT].dfd > -1) {
				/* Move to the call-wait and switch back to them. */
				swap_subs(p, SUB_CALLWAIT, SUB_REAL);
				unalloc_sub(p, SUB_CALLWAIT);
				p->owner = p->subs[SUB_REAL].owner;
				if (p->owner->_state != AST_STATE_UP)
					p->subs[SUB_REAL].needanswer = 1;
				if (ast_bridged_channel(p->subs[SUB_REAL].owner))
					ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
			} else if (p->subs[SUB_THREEWAY].dfd > -1) {
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				unalloc_sub(p, SUB_THREEWAY);
				if (p->subs[SUB_REAL].inthreeway) {
					/* This was part of a three way call.  Immediately make way for
					   another call */
					ast_debug(1, "Call was complete, setting owner to former third call\n");
					p->owner = p->subs[SUB_REAL].owner;
				} else {
					/* This call hasn't been completed yet...  Set owner to NULL */
					ast_debug(1, "Call was incomplete, setting owner to NULL\n");
					p->owner = NULL;
				}
				p->subs[SUB_REAL].inthreeway = 0;
			}
		} else if (idx == SUB_CALLWAIT) {
			/* Ditch the holding callwait call, and immediately make it availabe */
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* This is actually part of a three way, placed on hold.  Place the third part
				   on music on hold now */
				if (p->subs[SUB_THREEWAY].owner && ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
					ast_queue_control_data(p->subs[SUB_THREEWAY].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				p->subs[SUB_THREEWAY].inthreeway = 0;
				/* Make it the call wait now */
				swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
				unalloc_sub(p, SUB_THREEWAY);
			} else
				unalloc_sub(p, SUB_CALLWAIT);
		} else if (idx == SUB_THREEWAY) {
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				if (p->subs[SUB_CALLWAIT].owner && ast_bridged_channel(p->subs[SUB_CALLWAIT].owner)) {
					ast_queue_control_data(p->subs[SUB_CALLWAIT].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				p->subs[SUB_CALLWAIT].inthreeway = 0;
			}
			p->subs[SUB_REAL].inthreeway = 0;
			/* If this was part of a three way call index, let us make
			   another three way call */
			unalloc_sub(p, SUB_THREEWAY);
		} else {
			/* This wasn't any sort of call, but how are we an index? */
			ast_log(LOG_WARNING, "Index found but not any type of call?\n");
		}
	}

	if (!p->subs[SUB_REAL].owner && !p->subs[SUB_CALLWAIT].owner && !p->subs[SUB_THREEWAY].owner) {
		p->owner = NULL;
		p->ringt = 0;
		p->distinctivering = 0;
		p->confirmanswer = 0;
		p->cidrings = 1;
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;
		p->onhooktime = time(NULL);
#if defined(HAVE_PRI) || defined(HAVE_SS7)
		p->proceeding = 0;
		p->dialing = 0;
		p->progress = 0;
		p->alerting = 0;
		p->setup_ack = 0;
		p->rlt = 0;
#endif
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}

		if (p->bufferoverrideinuse) {
			/* faxbuffers are in use, revert them */
			struct dahdi_bufferinfo bi = {
				.txbufpolicy = p->buf_policy,
				.rxbufpolicy = p->buf_policy,
				.bufsize = p->bufsize,
				.numbufs = p->buf_no
			};
			int bpres;

			if ((bpres = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi)) < 0) {
				ast_log(LOG_WARNING, "Channel '%s' unable to revert buffer policy: %s\n", ast->name, strerror(errno));
			}
			p->bufferoverrideinuse = 0;
		}

		if (res < 0)
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n", p->channel, strerror(errno));
		/* Perform low level hangup if no owner left */
#ifdef HAVE_SS7
		if (p->ss7) {
			if (p->ss7call) {
				if (!ss7_grab(p, p->ss7)) {
					if (!p->alreadyhungup) {
						const char *cause = pbx_builtin_getvar_helper(ast,"SS7_CAUSE");
						int icause = ast->hangupcause ? ast->hangupcause : -1;

						if (cause) {
							if (atoi(cause))
								icause = atoi(cause);
						}
						isup_rel(p->ss7->ss7, p->ss7call, icause);
						ss7_rel(p->ss7);
						p->alreadyhungup = 1;
					} else
						ast_log(LOG_WARNING, "Trying to hangup twice!\n");
				} else {
					ast_log(LOG_WARNING, "Unable to grab SS7 on CIC %d\n", p->cic);
					res = -1;
				}
			}
		}
#endif
#ifdef HAVE_OPENR2
		if (p->mfcr2) {
			ast_log(LOG_DEBUG, "disconnecting MFC/R2 call on chan %d\n", p->channel);
			/* If it's an incoming call, check the mfcr2_forced_release setting */
			if (openr2_chan_get_direction(p->r2chan) == OR2_DIR_BACKWARD && p->mfcr2_forced_release) {
				openr2_chan_disconnect_call(p->r2chan, OR2_CAUSE_FORCED_RELEASE);
			} else {
				const char *r2causestr = pbx_builtin_getvar_helper(ast, "MFCR2_CAUSE");
				int r2cause_user = r2causestr ? atoi(r2causestr) : 0;
				openr2_call_disconnect_cause_t r2cause = r2cause_user ? dahdi_ast_cause_to_r2_cause(r2cause_user)
					                                              : dahdi_ast_cause_to_r2_cause(ast->hangupcause);
				openr2_chan_disconnect_call(p->r2chan, r2cause);
			}
			dahdi_r2_update_monitor_count(p->mfcr2, 1);
		}
#endif
		if (p->sig && ((p->sig != SIG_PRI) && (p->sig != SIG_SS7)
			&& (p->sig != SIG_BRI)
			&& (p->sig != SIG_BRI_PTMP))
			&& (p->sig != SIG_MFCR2))
			res = dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
		}
		switch (p->sig) {
		case SIG_FXOGS:
		case SIG_FXOLS:
		case SIG_FXOKS:
			memset(&par, 0, sizeof(par));
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &par);
			if (!res) {
#if 0
				ast_debug(1, "Hanging up channel %d, offhook = %d\n", p->channel, par.rxisoffhook);
#endif
				/* If they're off hook, try playing congestion */
				if ((par.rxisoffhook) && (!(p->radio || (p->oprmode < 0))))
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
				else
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
				p->fxsoffhookstate = par.rxisoffhook;
			}
			break;
		case SIG_FXSGS:
		case SIG_FXSLS:
		case SIG_FXSKS:
			/* Make sure we're not made available for at least two seconds assuming
			   we were actually used for an inbound or outbound call. */
			if (ast->_state != AST_STATE_RESERVED) {
				time(&p->guardtime);
				p->guardtime += 2;
			}
			break;
		default:
			tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
		}
		if (p->sig)
			dahdi_disable_ec(p);
		x = 0;
		ast_channel_setoption(ast,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		ast_channel_setoption(ast,AST_OPTION_TDD,&x,sizeof(char),0);
		p->didtdd = 0;
		p->callwaitcas = 0;
		p->callwaiting = p->permcallwaiting;
		p->hidecallerid = p->permhidecallerid;
		p->waitingfordt.tv_sec = 0;
		p->dialing = 0;
		p->rdnis[0] = '\0';
		update_conf(p);
		reset_conf(p);
		/* Restore data mode */
		if ((p->sig == SIG_PRI) || (p->sig == SIG_SS7) || (p->sig == SIG_BRI) || (p->sig == SIG_BRI_PTMP)) {
			x = 0;
			ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
		}
		if (num_restart_pending == 0)
			restart_monitor();
	}

	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	p->oprmode = 0;
	ast->tech_pvt = NULL;
hangup_out:
	if (p->cidspill)
		ast_free(p->cidspill);
	p->cidspill = NULL;
	
	ast_mutex_unlock(&p->lock);
	ast_module_unref(ast_module_info->self);
	ast_verb(3, "Hungup '%s'\n", ast->name);

	ast_mutex_lock(&iflock);

	if (p->restartpending) {
		num_restart_pending--;
	}

	tmp = iflist;
	prev = NULL;
	if (p->destroy) {
		while (tmp) {
			if (tmp == p) {
				destroy_channel(prev, tmp, 0);
				break;
			} else {
				prev = tmp;
				tmp = tmp->next;
			}
		}
	}
	ast_mutex_unlock(&iflock);
	return 0;
}

static int dahdi_answer(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	int res = 0;
	int idx;
	ast_setstate(ast, AST_STATE_UP);
	ast_mutex_lock(&p->lock);
	idx = dahdi_get_index(ast, p, 0);
	if (idx < 0)
		idx = SUB_REAL;
	/* nothing to do if a radio channel */
	if ((p->radio || (p->oprmode < 0))) {
		ast_mutex_unlock(&p->lock);
		return 0;
	}

	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		res = analog_answer(p->sig_pvt, ast);
		ast_mutex_unlock(&p->lock);
		return res;
	}

	switch (p->sig) {
#ifdef HAVE_PRI
	case SIG_BRI:
	case SIG_BRI_PTMP:
	case SIG_PRI:
		res = sig_pri_answer(p->sig_pvt, ast);
		ast_mutex_unlock(&p->lock);
		return res;
#endif
#ifdef HAVE_SS7
	case SIG_SS7:
		if (!ss7_grab(p, p->ss7)) {
			p->proceeding = 1;
			res = isup_anm(p->ss7->ss7, p->ss7call);
			ss7_rel(p->ss7);
		} else {
			ast_log(LOG_WARNING, "Unable to grab SS7 on span %d\n", p->span);
			res = -1;
		}
		break;
#endif
#ifdef HAVE_OPENR2
	case SIG_MFCR2:
		if (!p->mfcr2_call_accepted) {
			/* The call was not accepted on offer nor the user, so it must be accepted now before answering,
			   openr2_chan_answer_call will be called when the callback on_call_accepted is executed */
			p->mfcr2_answer_pending = 1;
			if (p->mfcr2_charge_calls) {
				ast_log(LOG_DEBUG, "Accepting MFC/R2 call with charge before answering on chan %d\n", p->channel);
				openr2_chan_accept_call(p->r2chan, OR2_CALL_WITH_CHARGE);
			} else {
				ast_log(LOG_DEBUG, "Accepting MFC/R2 call with no charge before answering on chan %d\n", p->channel);
				openr2_chan_accept_call(p->r2chan, OR2_CALL_NO_CHARGE);
			}
		} else {
			ast_log(LOG_DEBUG, "Answering MFC/R2 call on chan %d\n", p->channel);
			dahdi_r2_answer(p);
		}
		break;
#endif
	case 0:
		ast_mutex_unlock(&p->lock);
		return 0;
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		res = -1;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

static void disable_dtmf_detect(struct dahdi_pvt *p)
{
	int val = 0;

	p->ignoredtmf = 1;

	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);

	if (!p->hardwaredtmf && p->dsp) {
		p->dsp_features &= ~DSP_FEATURE_DIGIT_DETECT;
		ast_dsp_set_features(p->dsp, p->dsp_features);
	}
}

static void enable_dtmf_detect(struct dahdi_pvt *p)
{
	int val = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;

	if (p->channel == CHAN_PSEUDO)
		return;

	p->ignoredtmf = 0;

	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);

	if (!p->hardwaredtmf && p->dsp) {
		p->dsp_features |= DSP_FEATURE_DIGIT_DETECT;
		ast_dsp_set_features(p->dsp, p->dsp_features);
	}
}

static int dahdi_queryoption(struct ast_channel *chan, int option, void *data, int *datalen)
{
	char *cp;
	struct dahdi_pvt *p = chan->tech_pvt;

	/* all supported options require data */
	if (!data || (*datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		*cp = p->ignoredtmf ? 0 : 1;
		ast_debug(1, "Reporting digit detection %sabled on %s\n", *cp ? "en" : "dis", chan->name);
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		*cp = (p->callprogress & CALLPROGRESS_FAX) ? 0 : 1;
		ast_debug(1, "Reporting fax tone detection %sabled on %s\n", *cp ? "en" : "dis", chan->name);
		break;
	}

	errno = 0;

	return 0;
}

static int dahdi_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	signed char *scp;
	int x;
	int idx;
	struct dahdi_pvt *p = chan->tech_pvt, *pp;
	struct oprmode *oprmode;


	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TXGAIN:
		scp = (signed char *) data;
		idx = dahdi_get_index(chan, p, 0);
		if (idx < 0) {
			ast_log(LOG_WARNING, "No index in TXGAIN?\n");
			return -1;
		}
		ast_debug(1, "Setting actual tx gain on %s to %f\n", chan->name, p->txgain + (float) *scp);
		return set_actual_txgain(p->subs[idx].dfd, 0, p->txgain + (float) *scp, p->law);
	case AST_OPTION_RXGAIN:
		scp = (signed char *) data;
		idx = dahdi_get_index(chan, p, 0);
		if (idx < 0) {
			ast_log(LOG_WARNING, "No index in RXGAIN?\n");
			return -1;
		}
		ast_debug(1, "Setting actual rx gain on %s to %f\n", chan->name, p->rxgain + (float) *scp);
		return set_actual_rxgain(p->subs[idx].dfd, 0, p->rxgain + (float) *scp, p->law);
	case AST_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",chan->name);
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		case 2:
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
			break;
		default:
			ast_debug(1, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		}
		break;
	case AST_OPTION_TDD:
		/* turn on or off TDD */
		cp = (char *) data;
		p->mate = 0;
		if (!*cp) { /* turn it off */
			ast_debug(1, "Set option TDD MODE, value: OFF(0) on %s\n",chan->name);
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			break;
		}
		ast_debug(1, "Set option TDD MODE, value: %s(%d) on %s\n",
			(*cp == 2) ? "MATE" : "ON", (int) *cp, chan->name);
		dahdi_disable_ec(p);
		/* otherwise, turn it on */
		if (!p->didtdd) { /* if havent done it yet */
			unsigned char mybuf[41000];/*! \todo XXX This is an abuse of the stack!! */
			unsigned char *buf;
			int size, res, fd, len;
			struct pollfd fds[1];

			buf = mybuf;
			memset(buf, 0x7f, sizeof(mybuf)); /* set to silence */
			ast_tdd_gen_ecdisa(buf + 16000, 16000);  /* put in tone */
			len = 40000;
			idx = dahdi_get_index(chan, p, 0);
			if (idx < 0) {
				ast_log(LOG_WARNING, "No index in TDD?\n");
				return -1;
			}
			fd = p->subs[idx].dfd;
			while (len) {
				if (ast_check_hangup(chan))
					return -1;
				size = len;
				if (size > READ_SIZE)
					size = READ_SIZE;
				fds[0].fd = fd;
				fds[0].events = POLLPRI | POLLOUT;
				fds[0].revents = 0;
				res = poll(fds, 1, -1);
				if (!res) {
					ast_debug(1, "poll (for write) ret. 0 on channel %d\n", p->channel);
					continue;
				}
				/* if got exception */
				if (fds[0].revents & POLLPRI)
					return -1;
				if (!(fds[0].revents & POLLOUT)) {
					ast_debug(1, "write fd not ready on channel %d\n", p->channel);
					continue;
				}
				res = write(fd, buf, size);
				if (res != size) {
					if (res == -1) return -1;
					ast_debug(1, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
					break;
				}
				len -= size;
				buf += size;
			}
			p->didtdd = 1; /* set to have done it now */
		}
		if (*cp == 2) { /* Mate mode */
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			p->mate = 1;
			break;
		}
		if (!p->tdd) { /* if we dont have one yet */
			p->tdd = tdd_new(); /* allocate one */
		}
		break;
	case AST_OPTION_RELAXDTMF:  /* Relax DTMF decoding (or not) */
		if (!p->dsp)
			break;
		cp = (char *) data;
		ast_debug(1, "Set option RELAX DTMF, value: %s(%d) on %s\n",
			*cp ? "ON" : "OFF", (int) *cp, chan->name);
		ast_dsp_set_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
		break;
	case AST_OPTION_AUDIO_MODE:  /* Set AUDIO mode (or not) */
		cp = (char *) data;
		if (!*cp) {
			ast_debug(1, "Set option AUDIO MODE, value: OFF(0) on %s\n", chan->name);
			x = 0;
			dahdi_disable_ec(p);
		} else {
			ast_debug(1, "Set option AUDIO MODE, value: ON(1) on %s\n", chan->name);
			x = 1;
		}
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x) == -1)
			ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", p->channel, x, strerror(errno));
		break;
	case AST_OPTION_OPRMODE:  /* Operator services mode */
		oprmode = (struct oprmode *) data;
		/* We don't support operator mode across technologies */
		if (strcasecmp(chan->tech->type, oprmode->peer->tech->type)) {
			ast_log(LOG_NOTICE, "Operator mode not supported on %s to %s calls.\n",
					chan->tech->type, oprmode->peer->tech->type);
			errno = EINVAL;
			return -1;
		}
		pp = oprmode->peer->tech_pvt;
		p->oprmode = pp->oprmode = 0;
		/* setup peers */
		p->oprpeer = pp;
		pp->oprpeer = p;
		/* setup modes, if any */
		if (oprmode->mode)
		{
			pp->oprmode = oprmode->mode;
			p->oprmode = -oprmode->mode;
		}
		ast_debug(1, "Set Operator Services mode, value: %d on %s/%s\n",
			oprmode->mode, chan->name,oprmode->peer->name);
		break;
	case AST_OPTION_ECHOCAN:
		cp = (char *) data;
		if (*cp) {
			ast_debug(1, "Enabling echo cancelation on %s\n", chan->name);
			dahdi_enable_ec(p);
		} else {
			ast_debug(1, "Disabling echo cancelation on %s\n", chan->name);
			dahdi_disable_ec(p);
		}
		break;
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		ast_debug(1, "%sabling digit detection on %s\n", *cp ? "En" : "Dis", chan->name);
		if (*cp) {
			enable_dtmf_detect(p);
		} else {
			disable_dtmf_detect(p);
		}
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		if (p->dsp) {
			ast_debug(1, "%sabling fax tone detection on %s\n", *cp ? "En" : "Dis", chan->name);
			if (*cp) {
				p->callprogress |= CALLPROGRESS_FAX;
				p->dsp_features |= DSP_FEATURE_FAX_DETECT;
			} else {
				p->callprogress &= ~CALLPROGRESS_FAX;
				p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
			}
			ast_dsp_set_features(p->dsp, p->dsp_features);
		}
		break;
	default:
		return -1;
	}
	errno = 0;

	return 0;
}

static int dahdi_func_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	struct dahdi_pvt *p = chan->tech_pvt;
	int res = 0;
	
	if (!strcasecmp(data, "rxgain")) {
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%f", p->rxgain);
		ast_mutex_unlock(&p->lock);
	} else if (!strcasecmp(data, "txgain")) {
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%f", p->txgain);
		ast_mutex_unlock(&p->lock);
	} else {
		ast_copy_string(buf, "", len);
		res = -1;
	}

	return res;
}


static int parse_buffers_policy(const char *parse, int *num_buffers, int *policy)
{
	int res;
	char policy_str[21] = "";
	
	if ((res = sscanf(parse, "%d,%20s", num_buffers, policy_str)) != 2) {
		ast_log(LOG_WARNING, "Parsing buffer string '%s' failed.\n", parse);
		return 1;
	}
	if (*num_buffers < 0) {
		ast_log(LOG_WARNING, "Invalid buffer count given '%d'.\n", *num_buffers);
		return -1;
	}
	if (!strcasecmp(policy_str, "full")) {
		*policy = DAHDI_POLICY_WHEN_FULL;
	} else if (!strcasecmp(policy_str, "immediate")) {
		*policy = DAHDI_POLICY_IMMEDIATE;
#if defined(HAVE_DAHDI_HALF_FULL)
	} else if (!strcasecmp(policy_str, "half")) {
		*policy = DAHDI_POLICY_HALF_FULL;
#endif
	} else {
		ast_log(LOG_WARNING, "Invalid policy name given '%s'.\n", policy_str);
		return -1;
	}

	return 0;
}

static int dahdi_func_write(struct ast_channel *chan, const char *function, char *data, const char *value)
{
	struct dahdi_pvt *p = chan->tech_pvt;
	int res = 0;
	
	if (!strcasecmp(data, "buffers")) {
		int num_bufs, policy;

		if (!(parse_buffers_policy(value, &num_bufs, &policy))) {
			struct dahdi_bufferinfo bi = {
				.txbufpolicy = policy,
				.rxbufpolicy = policy,
				.bufsize = p->bufsize,
				.numbufs = num_bufs,
			};
			int bpres;

			if ((bpres = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi)) < 0) {
				ast_log(LOG_WARNING, "Channel '%d' unable to override buffer policy: %s\n", p->channel, strerror(errno));
			} else {
				p->bufferoverrideinuse = 1;
			}
		} else {
			res = -1;
		}
	} else if (!strcasecmp(data, "echocan_mode")) {
		if (!strcasecmp(value, "on")) {
			ast_mutex_lock(&p->lock);
			dahdi_enable_ec(p);
			ast_mutex_unlock(&p->lock);	
		} else if (!strcasecmp(value, "off")) {
			ast_mutex_lock(&p->lock);
			dahdi_disable_ec(p);
			ast_mutex_unlock(&p->lock);	
#ifdef HAVE_DAHDI_ECHOCANCEL_FAX_MODE
		} else if (!strcasecmp(value, "fax")) {
			int blah = 1;

			ast_mutex_lock(&p->lock);
			if (!p->echocanon) {
				dahdi_enable_ec(p);
			}
			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_FAX_MODE, &blah)) {
				ast_log(LOG_WARNING, "Unable to place echocan into fax mode on channel %d: %s\n", p->channel, strerror(errno));
			}
			ast_mutex_unlock(&p->lock);	
		} else if (!strcasecmp(value, "voice")) {
			int blah = 0;

			ast_mutex_lock(&p->lock);
			if (!p->echocanon) {
				dahdi_enable_ec(p);
			}
			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_FAX_MODE, &blah)) {
				ast_log(LOG_WARNING, "Unable to place echocan into voice mode on channel %d: %s\n", p->channel, strerror(errno));
			}
			ast_mutex_unlock(&p->lock);	
#endif
		} else {
			ast_log(LOG_WARNING, "Unsupported value '%s' provided for '%s' item.\n", value, data);
			res = -1;
		}
	} else {
		res = -1;
	}

	return res;
}

static void dahdi_unlink(struct dahdi_pvt *slave, struct dahdi_pvt *master, int needlock)
{
	/* Unlink a specific slave or all slaves/masters from a given master */
	int x;
	int hasslaves;
	if (!master)
		return;
	if (needlock) {
		ast_mutex_lock(&master->lock);
		if (slave) {
			while (ast_mutex_trylock(&slave->lock)) {
				DEADLOCK_AVOIDANCE(&master->lock);
			}
		}
	}
	hasslaves = 0;
	for (x = 0; x < MAX_SLAVES; x++) {
		if (master->slaves[x]) {
			if (!slave || (master->slaves[x] == slave)) {
				/* Take slave out of the conference */
				ast_debug(1, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
				conf_del(master, &master->slaves[x]->subs[SUB_REAL], SUB_REAL);
				conf_del(master->slaves[x], &master->subs[SUB_REAL], SUB_REAL);
				master->slaves[x]->master = NULL;
				master->slaves[x] = NULL;
			} else
				hasslaves = 1;
		}
		if (!hasslaves)
			master->inconference = 0;
	}
	if (!slave) {
		if (master->master) {
			/* Take master out of the conference */
			conf_del(master->master, &master->subs[SUB_REAL], SUB_REAL);
			conf_del(master, &master->master->subs[SUB_REAL], SUB_REAL);
			hasslaves = 0;
			for (x = 0; x < MAX_SLAVES; x++) {
				if (master->master->slaves[x] == master)
					master->master->slaves[x] = NULL;
				else if (master->master->slaves[x])
					hasslaves = 1;
			}
			if (!hasslaves)
				master->master->inconference = 0;
		}
		master->master = NULL;
	}
	update_conf(master);
	if (needlock) {
		if (slave)
			ast_mutex_unlock(&slave->lock);
		ast_mutex_unlock(&master->lock);
	}
}

static void dahdi_link(struct dahdi_pvt *slave, struct dahdi_pvt *master) {
	int x;
	if (!slave || !master) {
		ast_log(LOG_WARNING, "Tried to link to/from NULL??\n");
		return;
	}
	for (x = 0; x < MAX_SLAVES; x++) {
		if (!master->slaves[x]) {
			master->slaves[x] = slave;
			break;
		}
	}
	if (x >= MAX_SLAVES) {
		ast_log(LOG_WARNING, "Replacing slave %d with new slave, %d\n", master->slaves[MAX_SLAVES - 1]->channel, slave->channel);
		master->slaves[MAX_SLAVES - 1] = slave;
	}
	if (slave->master)
		ast_log(LOG_WARNING, "Replacing master %d with new master, %d\n", slave->master->channel, master->channel);
	slave->master = master;

	ast_debug(1, "Making %d slave to master %d at %d\n", slave->channel, master->channel, x);
}

static enum ast_bridge_result dahdi_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms)
{
	struct ast_channel *who;
	struct dahdi_pvt *p0, *p1, *op0, *op1;
	struct dahdi_pvt *master = NULL, *slave = NULL;
	struct ast_frame *f;
	int inconf = 0;
	int nothingok = 1;
	int ofd0, ofd1;
	int oi0, oi1, i0 = -1, i1 = -1, t0, t1;
	int os0 = -1, os1 = -1;
	int priority = 0;
	struct ast_channel *oc0, *oc1;
	enum ast_bridge_result res;

#ifdef PRI_2BCT
	int triedtopribridge = 0;
	q931_call *q931c0 = NULL, *q931c1 = NULL;
#endif

	/* For now, don't attempt to native bridge if either channel needs DTMF detection.
	   There is code below to handle it properly until DTMF is actually seen,
	   but due to currently unresolved issues it's ignored...
	*/

	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return AST_BRIDGE_FAILED_NOWARN;

	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		CHANNEL_DEADLOCK_AVOIDANCE(c0);
	}

	p0 = c0->tech_pvt;
	p1 = c1->tech_pvt;
	/* cant do pseudo-channels here */
	if (!p0 || (!p0->sig) || !p1 || (!p1->sig)) {
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}

	oi0 = dahdi_get_index(c0, p0, 0);
	oi1 = dahdi_get_index(c1, p1, 0);
	if ((oi0 < 0) || (oi1 < 0)) {
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED;
	}

	op0 = p0 = c0->tech_pvt;
	op1 = p1 = c1->tech_pvt;
	ofd0 = c0->fds[0];
	ofd1 = c1->fds[0];
	oc0 = p0->owner;
	oc1 = p1->owner;

	if (ast_mutex_trylock(&p0->lock)) {
		/* Don't block, due to potential for deadlock */
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		ast_log(LOG_NOTICE, "Avoiding deadlock...\n");
		return AST_BRIDGE_RETRY;
	}
	if (ast_mutex_trylock(&p1->lock)) {
		/* Don't block, due to potential for deadlock */
		ast_mutex_unlock(&p0->lock);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		ast_log(LOG_NOTICE, "Avoiding deadlock...\n");
		return AST_BRIDGE_RETRY;
	}

	if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL)) {
		if (p0->owner && p1->owner) {
			/* If we don't have a call-wait in a 3-way, and we aren't in a 3-way, we can be master */
			if (!p0->subs[SUB_CALLWAIT].inthreeway && !p1->subs[SUB_REAL].inthreeway) {
				master = p0;
				slave = p1;
				inconf = 1;
			} else if (!p1->subs[SUB_CALLWAIT].inthreeway && !p0->subs[SUB_REAL].inthreeway) {
				master = p1;
				slave = p0;
				inconf = 1;
			} else {
				ast_log(LOG_WARNING, "Huh?  Both calls are callwaits or 3-ways?  That's clever...?\n");
				ast_log(LOG_WARNING, "p0: chan %d/%d/CW%d/3W%d, p1: chan %d/%d/CW%d/3W%d\n",
					p0->channel,
					oi0, (p0->subs[SUB_CALLWAIT].dfd > -1) ? 1 : 0,
					p0->subs[SUB_REAL].inthreeway, p0->channel,
					oi0, (p1->subs[SUB_CALLWAIT].dfd > -1) ? 1 : 0,
					p1->subs[SUB_REAL].inthreeway);
			}
			nothingok = 0;
		}
	} else if ((oi0 == SUB_REAL) && (oi1 == SUB_THREEWAY)) {
		if (p1->subs[SUB_THREEWAY].inthreeway) {
			master = p1;
			slave = p0;
			nothingok = 0;
		}
	} else if ((oi0 == SUB_THREEWAY) && (oi1 == SUB_REAL)) {
		if (p0->subs[SUB_THREEWAY].inthreeway) {
			master = p0;
			slave = p1;
			nothingok = 0;
		}
	} else if ((oi0 == SUB_REAL) && (oi1 == SUB_CALLWAIT)) {
		/* We have a real and a call wait.  If we're in a three way call, put us in it, otherwise,
		   don't put us in anything */
		if (p1->subs[SUB_CALLWAIT].inthreeway) {
			master = p1;
			slave = p0;
			nothingok = 0;
		}
	} else if ((oi0 == SUB_CALLWAIT) && (oi1 == SUB_REAL)) {
		/* Same as previous */
		if (p0->subs[SUB_CALLWAIT].inthreeway) {
			master = p0;
			slave = p1;
			nothingok = 0;
		}
	}
	ast_debug(1, "master: %d, slave: %d, nothingok: %d\n",
		master ? master->channel : 0, slave ? slave->channel : 0, nothingok);
	if (master && slave) {
		/* Stop any tones, or play ringtone as appropriate.  If they're bridged
		   in an active threeway call with a channel that is ringing, we should
		   indicate ringing. */
		if ((oi1 == SUB_THREEWAY) &&
			p1->subs[SUB_THREEWAY].inthreeway &&
			p1->subs[SUB_REAL].owner &&
			p1->subs[SUB_REAL].inthreeway &&
			(p1->subs[SUB_REAL].owner->_state == AST_STATE_RINGING)) {
			ast_debug(1, "Playing ringback on %s since %s is in a ringing three-way\n", c0->name, c1->name);
			tone_zone_play_tone(p0->subs[oi0].dfd, DAHDI_TONE_RINGTONE);
			os1 = p1->subs[SUB_REAL].owner->_state;
		} else {
			ast_debug(1, "Stopping tones on %d/%d talking to %d/%d\n", p0->channel, oi0, p1->channel, oi1);
			tone_zone_play_tone(p0->subs[oi0].dfd, -1);
		}
		if ((oi0 == SUB_THREEWAY) &&
			p0->subs[SUB_THREEWAY].inthreeway &&
			p0->subs[SUB_REAL].owner &&
			p0->subs[SUB_REAL].inthreeway &&
			(p0->subs[SUB_REAL].owner->_state == AST_STATE_RINGING)) {
			ast_debug(1, "Playing ringback on %s since %s is in a ringing three-way\n", c1->name, c0->name);
			tone_zone_play_tone(p1->subs[oi1].dfd, DAHDI_TONE_RINGTONE);
			os0 = p0->subs[SUB_REAL].owner->_state;
		} else {
			ast_debug(1, "Stopping tones on %d/%d talking to %d/%d\n", p1->channel, oi1, p0->channel, oi0);
			tone_zone_play_tone(p1->subs[oi0].dfd, -1);
		}
		if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL)) {
			if (!p0->echocanbridged || !p1->echocanbridged) {
				/* Disable echo cancellation if appropriate */
				dahdi_disable_ec(p0);
				dahdi_disable_ec(p1);
			}
		}
		dahdi_link(slave, master);
		master->inconference = inconf;
	} else if (!nothingok)
		ast_log(LOG_WARNING, "Can't link %d/%s with %d/%s\n", p0->channel, subnames[oi0], p1->channel, subnames[oi1]);

	update_conf(p0);
	update_conf(p1);
	t0 = p0->subs[SUB_REAL].inthreeway;
	t1 = p1->subs[SUB_REAL].inthreeway;

	ast_mutex_unlock(&p0->lock);
	ast_mutex_unlock(&p1->lock);

	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	/* Native bridge failed */
	if ((!master || !slave) && !nothingok) {
		dahdi_enable_ec(p0);
		dahdi_enable_ec(p1);
		return AST_BRIDGE_FAILED;
	}

	ast_verb(3, "Native bridging %s and %s\n", c0->name, c1->name);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0) && (oi0 == SUB_REAL))
		disable_dtmf_detect(op0);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1) && (oi1 == SUB_REAL))
		disable_dtmf_detect(op1);

	for (;;) {
		struct ast_channel *c0_priority[2] = {c0, c1};
		struct ast_channel *c1_priority[2] = {c1, c0};

		/* Here's our main loop...  Start by locking things, looking for private parts,
		   and then balking if anything is wrong */

		ast_channel_lock(c0);
		while (ast_channel_trylock(c1)) {
			CHANNEL_DEADLOCK_AVOIDANCE(c0);
		}

		p0 = c0->tech_pvt;
		p1 = c1->tech_pvt;

		if (op0 == p0)
			i0 = dahdi_get_index(c0, p0, 1);
		if (op1 == p1)
			i1 = dahdi_get_index(c1, p1, 1);

		ast_channel_unlock(c0);
		ast_channel_unlock(c1);

		if (!timeoutms ||
			(op0 != p0) ||
			(op1 != p1) ||
			(ofd0 != c0->fds[0]) ||
			(ofd1 != c1->fds[0]) ||
			(p0->subs[SUB_REAL].owner && (os0 > -1) && (os0 != p0->subs[SUB_REAL].owner->_state)) ||
			(p1->subs[SUB_REAL].owner && (os1 > -1) && (os1 != p1->subs[SUB_REAL].owner->_state)) ||
			(oc0 != p0->owner) ||
			(oc1 != p1->owner) ||
			(t0 != p0->subs[SUB_REAL].inthreeway) ||
			(t1 != p1->subs[SUB_REAL].inthreeway) ||
			(oi0 != i0) ||
			(oi1 != i1)) {
			ast_debug(1, "Something changed out on %d/%d to %d/%d, returning -3 to restart\n",
				op0->channel, oi0, op1->channel, oi1);
			res = AST_BRIDGE_RETRY;
			goto return_from_bridge;
		}

#ifdef PRI_2BCT
		q931c0 = ((struct sig_pri_chan *)(p0->sig_pvt))->call;
		q931c1 = ((struct sig_pri_chan *)(p1->sig_pvt))->call;
		if (p0->transfer && p1->transfer
			&& q931c0 && q931c1
			&& !triedtopribridge) {
			pri_channel_bridge(q931c0, q931c1);
			triedtopribridge = 1;
		}
#endif

		who = ast_waitfor_n(priority ? c0_priority : c1_priority, 2, &timeoutms);
		if (!who) {
			ast_debug(1, "Ooh, empty read...\n");
			continue;
		}
		f = ast_read(who);
		if (!f || (f->frametype == AST_FRAME_CONTROL)) {
			*fo = f;
			*rc = who;
			res = AST_BRIDGE_COMPLETE;
			goto return_from_bridge;
		}
		if (f->frametype == AST_FRAME_DTMF) {
			if ((who == c0) && p0->pulsedial) {
				ast_write(c1, f);
			} else if ((who == c1) && p1->pulsedial) {
				ast_write(c0, f);
			} else {
				*fo = f;
				*rc = who;
				res = AST_BRIDGE_COMPLETE;
				goto return_from_bridge;
			}
		}
		ast_frfree(f);

		/* Swap who gets priority */
		priority = !priority;
	}

return_from_bridge:
	if (op0 == p0)
		dahdi_enable_ec(p0);

	if (op1 == p1)
		dahdi_enable_ec(p1);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0) && (oi0 == SUB_REAL))
		enable_dtmf_detect(op0);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1) && (oi1 == SUB_REAL))
		enable_dtmf_detect(op1);

	dahdi_unlink(slave, master, 1);

	return res;
}

static int dahdi_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct dahdi_pvt *p = newchan->tech_pvt;
	int x;
	ast_mutex_lock(&p->lock);
	ast_debug(1, "New owner for channel %d is %s\n", p->channel, newchan->name);
	if (p->owner == oldchan) {
		p->owner = newchan;
	}
	for (x = 0; x < 3; x++)
		if (p->subs[x].owner == oldchan) {
			if (!x)
				dahdi_unlink(NULL, p, 0);
			p->subs[x].owner = newchan;
		}
	if (newchan->_state == AST_STATE_RINGING)
		dahdi_indicate(newchan, AST_CONTROL_RINGING, NULL, 0);

	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		analog_fixup(oldchan, newchan, p->sig_pvt);
	}

	update_conf(p);
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int dahdi_ring_phone(struct dahdi_pvt *p)
{
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = DAHDI_ONHOOK;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
	do {
		x = DAHDI_RING;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
		if (res) {
			switch (errno) {
			case EBUSY:
			case EINTR:
				/* Wait just in case */
				usleep(10000);
				continue;
			case EINPROGRESS:
				res = 0;
				break;
			default:
				ast_log(LOG_WARNING, "Couldn't ring the phone: %s\n", strerror(errno));
				res = 0;
			}
		}
	} while (res);
	return res;
}

static void *analog_ss_thread(void *data);

static int attempt_transfer(struct dahdi_pvt *p)
{
	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if (ast_bridged_channel(p->subs[SUB_REAL].owner)) {
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner))
			ast_queue_control(p->subs[SUB_THREEWAY].owner, AST_CONTROL_UNHOLD);
		if (p->subs[SUB_REAL].owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[SUB_REAL].owner), AST_CONTROL_RINGING);
		}
		if (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_RING) {
			tone_zone_play_tone(p->subs[SUB_THREEWAY].dfd, DAHDI_TONE_RINGTONE);
		}
		 if (ast_channel_masquerade(p->subs[SUB_THREEWAY].owner, ast_bridged_channel(p->subs[SUB_REAL].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(p->subs[SUB_REAL].owner)->name, p->subs[SUB_THREEWAY].owner->name);
			return -1;
		}
		/* Orphan the channel after releasing the lock */
		ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
		unalloc_sub(p, SUB_THREEWAY);
	} else if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
		ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
		if (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[SUB_THREEWAY].owner), AST_CONTROL_RINGING);
		}
		if (p->subs[SUB_REAL].owner->_state == AST_STATE_RING) {
			tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
		}
		if (ast_channel_masquerade(p->subs[SUB_REAL].owner, ast_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(p->subs[SUB_THREEWAY].owner)->name, p->subs[SUB_REAL].owner->name);
			return -1;
		}
		/* Three-way is now the REAL */
		swap_subs(p, SUB_THREEWAY, SUB_REAL);
		ast_channel_unlock(p->subs[SUB_REAL].owner);
		unalloc_sub(p, SUB_THREEWAY);
		/* Tell the caller not to hangup */
		return 1;
	} else {
		ast_debug(1, "Neither %s nor %s are in a bridge, nothing to transfer\n",
			p->subs[SUB_REAL].owner->name, p->subs[SUB_THREEWAY].owner->name);
		p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
		return -1;
	}
	return 0;
}

static int check_for_conference(struct dahdi_pvt *p)
{
	struct dahdi_confinfo ci;
	/* Fine if we already have a master, etc */
	if (p->master || (p->confno > -1))
		return 0;
	memset(&ci, 0, sizeof(ci));
	if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GETCONF, &ci)) {
		ast_log(LOG_WARNING, "Failed to get conference info on channel %d: %s\n", p->channel, strerror(errno));
		return 0;
	}
	/* If we have no master and don't have a confno, then
	   if we're in a conference, it's probably a MeetMe room or
	   some such, so don't let us 3-way out! */
	if ((p->subs[SUB_REAL].curconf.confno != ci.confno) || (p->subs[SUB_REAL].curconf.confmode != ci.confmode)) {
		ast_verb(3, "Avoiding 3-way call when in an external conference\n");
		return 1;
	}
	return 0;
}

/*! Checks channel for alarms
 * \param p a channel to check for alarms.
 * \returns the alarms on the span to which the channel belongs, or alarms on
 *          the channel if no span alarms.
 */
static int get_alarms(struct dahdi_pvt *p)
{
	int res;
	struct dahdi_spaninfo zi;
	struct dahdi_params params;

	memset(&zi, 0, sizeof(zi));
	zi.spanno = p->span;

	if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SPANSTAT, &zi)) >= 0) {
		if (zi.alarms != DAHDI_ALARM_NONE)
			return zi.alarms;
	} else {
		ast_log(LOG_WARNING, "Unable to determine alarm on channel %d: %s\n", p->channel, strerror(errno));
		return 0;
	}

	/* No alarms on the span. Check for channel alarms. */
	memset(&params, 0, sizeof(params));
	if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &params)) >= 0)
		return params.chan_alarms;

	ast_log(LOG_WARNING, "Unable to determine alarm on channel %d\n", p->channel);

	return DAHDI_ALARM_NONE;
}

static void dahdi_handle_dtmfup(struct ast_channel *ast, int idx, struct ast_frame **dest)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	struct ast_frame *f = *dest;

	ast_debug(1, "DTMF digit: %c on %s\n", f->subclass, ast->name);

	if (p->confirmanswer) {
		ast_debug(1, "Confirm answer on %s!\n", ast->name);
		/* Upon receiving a DTMF digit, consider this an answer confirmation instead
		   of a DTMF digit */
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
		*dest = &p->subs[idx].f;
		/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
		p->confirmanswer = 0;
	} else if (p->callwaitcas) {
		if ((f->subclass == 'A') || (f->subclass == 'D')) {
			ast_debug(1, "Got some DTMF, but it's for the CAS\n");
			if (p->cidspill)
				ast_free(p->cidspill);
			send_cwcidspill(p);
		}
		p->callwaitcas = 0;
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass = 0;
		*dest = &p->subs[idx].f;
	} else if (f->subclass == 'f') {
		/* Fax tone -- Handle and return NULL */
		if ((p->callprogress & CALLPROGRESS_FAX) && !p->faxhandled) {
			/* If faxbuffers are configured, use them for the fax transmission */
			if (p->usefaxbuffers && !p->bufferoverrideinuse) {
				struct dahdi_bufferinfo bi = {
					.txbufpolicy = p->faxbuf_policy,
					.bufsize = p->bufsize,
					.numbufs = p->faxbuf_no
				};
				int res;

				if ((res = ioctl(p->subs[idx].dfd, DAHDI_SET_BUFINFO, &bi)) < 0) {
					ast_log(LOG_WARNING, "Channel '%s' unable to set buffer policy, reason: %s\n", ast->name, strerror(errno));
				} else {
					p->bufferoverrideinuse = 1;
				}
			}
			p->faxhandled = 1;
			p->callprogress &= ~CALLPROGRESS_FAX;
			p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
			ast_dsp_set_features(p->dsp, p->dsp_features);
			ast_debug(1, "Disabling FAX tone detection on %s after tone received\n", ast->name);
			if (strcmp(ast->exten, "fax")) {
				const char *target_context = S_OR(ast->macrocontext, ast->context);

				/* We need to unlock 'ast' here because ast_exists_extension has the
				 * potential to start autoservice on the channel. Such action is prone
				 * to deadlock.
				 */
				ast_mutex_unlock(&p->lock);
				ast_channel_unlock(ast);
				if (ast_exists_extension(ast, target_context, "fax", 1, ast->cid.cid_num)) {
					ast_channel_lock(ast);
					ast_mutex_lock(&p->lock);
					ast_verb(3, "Redirecting %s to fax extension\n", ast->name);
					/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
					pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast->exten);
					if (ast_async_goto(ast, target_context, "fax", 1))
						ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, target_context);
				} else {
					ast_channel_lock(ast);
					ast_mutex_lock(&p->lock);
					ast_log(LOG_NOTICE, "Fax detected, but no fax extension\n");
				}
			} else {
				ast_debug(1, "Already in a fax extension, not redirecting\n");
			}
		} else {
			ast_debug(1, "Fax already handled\n");
		}
		dahdi_confmute(p, 0);
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass = 0;
		*dest = &p->subs[idx].f;
	}
}

static void handle_alarms(struct dahdi_pvt *p, int alms)
{
	const char *alarm_str = alarm2str(alms);

	ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm_str);
	manager_event(EVENT_FLAG_SYSTEM, "Alarm",
		"Alarm: %s\r\n"
		"Channel: %d\r\n",
		alarm_str, p->channel);
}

static struct ast_frame *dahdi_handle_event(struct ast_channel *ast)
{
	int res, x;
	int idx, mysig;
	char *c;
	struct dahdi_pvt *p = ast->tech_pvt;
	pthread_t threadid;
	struct ast_channel *chan;
	struct ast_frame *f;

	idx = dahdi_get_index(ast, p, 0);
	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;
	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.subclass = 0;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.src = "dahdi_handle_event";
	p->subs[idx].f.data.ptr = NULL;
	f = &p->subs[idx].f;

	if (idx < 0)
		return &p->subs[idx].f;
	if (p->fake_event) {
		res = p->fake_event;
		p->fake_event = 0;
	} else
		res = dahdi_get_event(p->subs[idx].dfd);

	ast_debug(1, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, idx);

	if (res & (DAHDI_EVENT_PULSEDIGIT | DAHDI_EVENT_DTMFUP)) {
		p->pulsedial = (res & DAHDI_EVENT_PULSEDIGIT) ? 1 : 0;
		ast_debug(1, "Detected %sdigit '%c'\n", p->pulsedial ? "pulse ": "", res & 0xff);
#ifdef HAVE_PRI
		if (!((struct sig_pri_chan *)(p->sig_pvt))->proceeding && ((p->sig == SIG_PRI) || (p->sig == SIG_BRI) || (p->sig == SIG_BRI_PTMP)) && p->pri && (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
			/* absorb event */
		} else {
#endif
			dahdi_confmute(p, 0);
			p->subs[idx].f.frametype = AST_FRAME_DTMF_END;
			p->subs[idx].f.subclass = res & 0xff;
#ifdef HAVE_PRI
		}
#endif
		dahdi_handle_dtmfup(ast, idx, &f);
		return f;
	}

	if (res & DAHDI_EVENT_DTMFDOWN) {
		ast_debug(1, "DTMF Down '%c'\n", res & 0xff);
		/* Mute conference */
		dahdi_confmute(p, 1);
		p->subs[idx].f.frametype = AST_FRAME_DTMF_BEGIN;
		p->subs[idx].f.subclass = res & 0xff;
		return &p->subs[idx].f;
	}

	switch (res) {
	case DAHDI_EVENT_EC_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled.\n", p->channel);
		p->echocanon = 0;
		break;
#ifdef HAVE_DAHDI_ECHOCANCEL_FAX_MODE
	case DAHDI_EVENT_TX_CED_DETECTED:
		ast_verb(3, "Channel %d detected a CED tone towards the network.\n", p->channel);
		break;
	case DAHDI_EVENT_RX_CED_DETECTED:
		ast_verb(3, "Channel %d detected a CED tone from the network.\n", p->channel);
		break;
	case DAHDI_EVENT_EC_NLP_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled its NLP.\n", p->channel);
		break;
	case DAHDI_EVENT_EC_NLP_ENABLED:
		ast_verb(3, "Channel %d echo canceler enabled its NLP.\n", p->channel);
		break;
#endif
	case DAHDI_EVENT_BITSCHANGED:
#ifdef HAVE_OPENR2
		if (p->sig != SIG_MFCR2) {
			ast_log(LOG_WARNING, "Recieved bits changed on %s signalling?\n", sig2str(p->sig));
		} else {
			ast_log(LOG_DEBUG, "bits changed in chan %d\n", p->channel);
			openr2_chan_handle_cas(p->r2chan);
		}
#else
		ast_log(LOG_WARNING, "Recieved bits changed on %s signalling?\n", sig2str(p->sig));
#endif
	case DAHDI_EVENT_PULSE_START:
		/* Stop tone if there's a pulse start and the PBX isn't started */
		if (!ast->pbx)
			tone_zone_play_tone(p->subs[idx].dfd, -1);
		break;
	case DAHDI_EVENT_DIALCOMPLETE:
#ifdef HAVE_OPENR2
		if ((p->sig & SIG_MFCR2) && p->r2chan && ast->_state != AST_STATE_UP) {
			/* we don't need to do anything for this event for R2 signaling
			   if the call is being setup */
			break;
		}
#endif
		if (p->inalarm) break;
		if ((p->radio || (p->oprmode < 0))) break;
		if (ioctl(p->subs[idx].dfd,DAHDI_DIALING,&x) == -1) {
			ast_log(LOG_DEBUG, "DAHDI_DIALING ioctl failed on %s: %s\n",ast->name, strerror(errno));
			return NULL;
		}
		if (!x) { /* if not still dialing in driver */
			dahdi_enable_ec(p);
			if (p->echobreak) {
				dahdi_train_ec(p);
				ast_copy_string(p->dop.dialstr, p->echorest, sizeof(p->dop.dialstr));
				p->dop.op = DAHDI_DIAL_OP_REPLACE;
				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				p->echobreak = 0;
			} else {
				p->dialing = 0;
				if ((mysig == SIG_E911) || (mysig == SIG_FGC_CAMA) || (mysig == SIG_FGC_CAMAMF)) {
					/* if thru with dialing after offhook */
					if (ast->_state == AST_STATE_DIALING_OFFHOOK) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
						break;
					} else { /* if to state wait for offhook to dial rest */
						/* we now wait for off hook */
						ast_setstate(ast,AST_STATE_DIALING_OFFHOOK);
					}
				}
				if (ast->_state == AST_STATE_DIALING) {
					if ((p->callprogress & CALLPROGRESS_PROGRESS) && CANPROGRESSDETECT(p) && p->dsp && p->outgoing) {
						ast_debug(1, "Done dialing, but waiting for progress detection before doing more...\n");
					} else if (p->confirmanswer || (!p->dialednone
						&& ((mysig == SIG_EM) || (mysig == SIG_EM_E1)
							|| (mysig == SIG_EMWINK) || (mysig == SIG_FEATD)
							|| (mysig == SIG_FEATDMF_TA) || (mysig == SIG_FEATDMF)
							|| (mysig == SIG_E911) || (mysig == SIG_FGC_CAMA)
							|| (mysig == SIG_FGC_CAMAMF) || (mysig == SIG_FEATB)
							|| (mysig == SIG_SF) || (mysig == SIG_SFWINK)
							|| (mysig == SIG_SF_FEATD) || (mysig == SIG_SF_FEATDMF)
							|| (mysig == SIG_SF_FEATB)))) {
						ast_setstate(ast, AST_STATE_RINGING);
					} else if (!p->answeronpolarityswitch) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
						/* If aops=0 and hops=1, this is necessary */
						p->polarity = POLARITY_REV;
					} else {
						/* Start clean, so we can catch the change to REV polarity when party answers */
						p->polarity = POLARITY_IDLE;
					}
				}
			}
		}
		break;
	case DAHDI_EVENT_ALARM:
#ifdef HAVE_PRI
			if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
				sig_pri_chan_alarm_notify(p->sig_pvt, 0);

			} else {
				if (p->owner)
					p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
			}
#endif
		p->inalarm = 1;
		res = get_alarms(p);
		handle_alarms(p, res);
#ifdef HAVE_PRI
		if (!p->pri || !p->pri->pri || pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0) {
			/* fall through intentionally */
		} else {
			break;
		}
#endif
#ifdef HAVE_SS7
		if (p->sig == SIG_SS7)
			break;
#endif
#ifdef HAVE_OPENR2
		if (p->sig == SIG_MFCR2)
			break;
#endif
	case DAHDI_EVENT_ONHOOK:
		if (p->radio) {
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_UNKEY;
			break;
		}
		if (p->oprmode < 0)
		{
			if (p->oprmode != -1) break;
			if ((p->sig == SIG_FXOLS) || (p->sig == SIG_FXOKS) || (p->sig == SIG_FXOGS))
			{
				/* Make sure it starts ringing */
				dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_RINGOFF);
				dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_RING);
				save_conference(p->oprpeer);
				tone_zone_play_tone(p->oprpeer->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
			}
			break;
		}
		switch (p->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			p->onhooktime = time(NULL);
			p->fxsoffhookstate = 0;
			p->msgstate = -1;
			/* Check for some special conditions regarding call waiting */
			if (idx == SUB_REAL) {
				/* The normal line was hung up */
				if (p->subs[SUB_CALLWAIT].owner) {
					/* There's a call waiting call, so ring the phone, but make it unowned in the mean time */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					ast_verb(3, "Channel %d still has (callwait) call, ringing phone\n", p->channel);
					unalloc_sub(p, SUB_CALLWAIT);
#if 0
					p->subs[idx].needanswer = 0;
					p->subs[idx].needringing = 0;
#endif
					p->callwaitingrepeat = 0;
					p->cidcwexpire = 0;
					p->owner = NULL;
					/* Don't start streaming audio yet if the incoming call isn't up yet */
					if (p->subs[SUB_REAL].owner->_state != AST_STATE_UP)
						p->dialing = 1;
					dahdi_ring_phone(p);
				} else if (p->subs[SUB_THREEWAY].owner) {
					unsigned int mssinceflash;
					/* Here we have to retain the lock on both the main channel, the 3-way channel, and
					   the private structure -- not especially easy or clean */
					while (p->subs[SUB_THREEWAY].owner && ast_channel_trylock(p->subs[SUB_THREEWAY].owner)) {
						/* Yuck, didn't get the lock on the 3-way, gotta release everything and re-grab! */
						DLA_UNLOCK(&p->lock);
						CHANNEL_DEADLOCK_AVOIDANCE(ast);
						/* We can grab ast and p in that order, without worry.  We should make sure
						   nothing seriously bad has happened though like some sort of bizarre double
						   masquerade! */
						DLA_LOCK(&p->lock);
						if (p->owner != ast) {
							ast_log(LOG_WARNING, "This isn't good...\n");
							return NULL;
						}
					}
					if (!p->subs[SUB_THREEWAY].owner) {
						ast_log(LOG_NOTICE, "Whoa, threeway disappeared kinda randomly.\n");
						return NULL;
					}
					mssinceflash = ast_tvdiff_ms(ast_tvnow(), p->flashtime);
					ast_debug(1, "Last flash was %d ms ago\n", mssinceflash);
					if (mssinceflash < MIN_MS_SINCE_FLASH) {
						/* It hasn't been long enough since the last flashook.  This is probably a bounce on
						   hanging up.  Hangup both channels now */
						if (p->subs[SUB_THREEWAY].owner)
							ast_queue_hangup_with_cause(p->subs[SUB_THREEWAY].owner, AST_CAUSE_NO_ANSWER);
						p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
						ast_debug(1, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
						ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
					} else if ((ast->pbx) || (ast->_state == AST_STATE_UP)) {
						if (p->transfer) {
							/* In any case this isn't a threeway call anymore */
							p->subs[SUB_REAL].inthreeway = 0;
							p->subs[SUB_THREEWAY].inthreeway = 0;
							/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
							if (!p->transfertobusy && ast->_state == AST_STATE_BUSY) {
								ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
								/* Swap subs and dis-own channel */
								swap_subs(p, SUB_THREEWAY, SUB_REAL);
								p->owner = NULL;
								/* Ring the phone */
								dahdi_ring_phone(p);
							} else {
								if ((res = attempt_transfer(p)) < 0) {
									p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
									if (p->subs[SUB_THREEWAY].owner)
										ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
								} else if (res) {
									/* Don't actually hang up at this point */
									if (p->subs[SUB_THREEWAY].owner)
										ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
									break;
								}
							}
						} else {
							p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
							if (p->subs[SUB_THREEWAY].owner)
								ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
						}
					} else {
						ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
						/* Swap subs and dis-own channel */
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						p->owner = NULL;
						/* Ring the phone */
						dahdi_ring_phone(p);
					}
				}
			} else {
				ast_log(LOG_WARNING, "Got a hangup and my index is %d?\n", idx);
			}
			/* Fall through */
		default:
			dahdi_disable_ec(p);
			return NULL;
		}
		break;
	case DAHDI_EVENT_RINGOFFHOOK:
		if (p->inalarm) break;
		if (p->oprmode < 0)
		{
			if ((p->sig == SIG_FXOLS) || (p->sig == SIG_FXOKS) || (p->sig == SIG_FXOGS))
			{
				/* Make sure it stops ringing */
				dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_RINGOFF);
				tone_zone_play_tone(p->oprpeer->subs[SUB_REAL].dfd, -1);
				restore_conference(p->oprpeer);
			}
			break;
		}
		if (p->radio)
		{
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_KEY;
			break;
 		}
		/* for E911, its supposed to wait for offhook then dial
		   the second half of the dial string */
		if (((mysig == SIG_E911) || (mysig == SIG_FGC_CAMA) || (mysig == SIG_FGC_CAMAMF)) && (ast->_state == AST_STATE_DIALING_OFFHOOK)) {
			c = strchr(p->dialdest, '/');
			if (c)
				c++;
			else
				c = p->dialdest;
			if (*c) snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*0%s#", c);
			else ast_copy_string(p->dop.dialstr,"M*2#", sizeof(p->dop.dialstr));
			if (strlen(p->dop.dialstr) > 4) {
				memset(p->echorest, 'w', sizeof(p->echorest) - 1);
				strcpy(p->echorest + (p->echotraining / 401) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
				p->echorest[sizeof(p->echorest) - 1] = '\0';
				p->echobreak = 1;
				p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
			} else
				p->echobreak = 0;
			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop)) {
				int saveerr = errno;

				x = DAHDI_ONHOOK;
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(saveerr));
				return NULL;
				}
			p->dialing = 1;
			return &p->subs[idx].f;
		}
		switch (p->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			p->fxsoffhookstate = 1;
			switch (ast->_state) {
			case AST_STATE_RINGING:
				dahdi_enable_ec(p);
				dahdi_train_ec(p);
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
				/* Make sure it stops ringing */
				dahdi_set_hook(p->subs[idx].dfd, DAHDI_OFFHOOK);
				ast_debug(1, "channel %d answered\n", p->channel);
				if (p->cidspill) {
					/* Cancel any running CallerID spill */
					ast_free(p->cidspill);
					p->cidspill = NULL;
				}
				p->dialing = 0;
				p->callwaitcas = 0;
				if (p->confirmanswer) {
					/* Ignore answer if "confirm answer" is enabled */
					p->subs[idx].f.frametype = AST_FRAME_NULL;
					p->subs[idx].f.subclass = 0;
				} else if (!ast_strlen_zero(p->dop.dialstr)) {
					/* nick@dccinc.com 4/3/03 - fxo should be able to do deferred dialing */
					res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
					if (res < 0) {
						ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
						p->dop.dialstr[0] = '\0';
						return NULL;
					} else {
						ast_debug(1, "Sent FXO deferred digit string: %s\n", p->dop.dialstr);
						p->subs[idx].f.frametype = AST_FRAME_NULL;
						p->subs[idx].f.subclass = 0;
						p->dialing = 1;
					}
					p->dop.dialstr[0] = '\0';
					ast_setstate(ast, AST_STATE_DIALING);
				} else
					ast_setstate(ast, AST_STATE_UP);
				return &p->subs[idx].f;
			case AST_STATE_DOWN:
				ast_setstate(ast, AST_STATE_RING);
				ast->rings = 1;
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass = AST_CONTROL_OFFHOOK;
				ast_debug(1, "channel %d picked up\n", p->channel);
				return &p->subs[idx].f;
			case AST_STATE_UP:
				/* Make sure it stops ringing */
				dahdi_set_hook(p->subs[idx].dfd, DAHDI_OFFHOOK);
				/* Okay -- probably call waiting*/
				if (ast_bridged_channel(p->owner))
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
				p->subs[idx].needunhold = 1;
				break;
			case AST_STATE_RESERVED:
				/* Start up dialtone */
				if (has_voicemail(p))
					res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_STUTTER);
				else
					res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_DIALTONE);
				break;
			default:
				ast_log(LOG_WARNING, "FXO phone off hook in weird state %d??\n", ast->_state);
			}
			break;
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
			if (ast->_state == AST_STATE_RING) {
				p->ringt = p->ringt_base;
			}

			/* If we get a ring then we cannot be in
			 * reversed polarity. So we reset to idle */
			ast_debug(1, "Setting IDLE polarity due "
				"to ring. Old polarity was %d\n",
				p->polarity);
			p->polarity = POLARITY_IDLE;

			/* Fall through */
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_FEATDMF_TA:
		case SIG_E911:
		case SIG_FGC_CAMA:
		case SIG_FGC_CAMAMF:
		case SIG_FEATB:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
			if (ast->_state == AST_STATE_PRERING)
				ast_setstate(ast, AST_STATE_RING);
			if ((ast->_state == AST_STATE_DOWN) || (ast->_state == AST_STATE_RING)) {
				ast_debug(1, "Ring detected\n");
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass = AST_CONTROL_RING;
			} else if (p->outgoing && ((ast->_state == AST_STATE_RINGING) || (ast->_state == AST_STATE_DIALING))) {
				ast_debug(1, "Line answered\n");
				if (p->confirmanswer) {
					p->subs[idx].f.frametype = AST_FRAME_NULL;
					p->subs[idx].f.subclass = 0;
				} else {
					p->subs[idx].f.frametype = AST_FRAME_CONTROL;
					p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
					ast_setstate(ast, AST_STATE_UP);
				}
			} else if (ast->_state != AST_STATE_RING)
				ast_log(LOG_WARNING, "Ring/Off-hook in strange state %d on channel %d\n", ast->_state, p->channel);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
		}
		break;
	case DAHDI_EVENT_RINGBEGIN:
		switch (p->sig) {
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
			if (ast->_state == AST_STATE_RING) {
				p->ringt = p->ringt_base;
			}
			break;
		}
		break;
	case DAHDI_EVENT_RINGERON:
		break;
	case DAHDI_EVENT_NOALARM:
#ifdef HAVE_PRI
		if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
			sig_pri_chan_alarm_notify(p->sig_pvt, 1);
		}
#endif
		p->inalarm = 0;
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
							"Channel: %d\r\n", p->channel);
		break;
	case DAHDI_EVENT_WINKFLASH:
		if (p->inalarm) break;
		if (p->radio) break;
		if (p->oprmode < 0) break;
		if (p->oprmode > 1)
		{
			struct dahdi_params par;

			memset(&par, 0, sizeof(par));
			if (ioctl(p->oprpeer->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &par) != -1)
			{
				if (!par.rxisoffhook)
				{
					/* Make sure it stops ringing */
					dahdi_set_hook(p->oprpeer->subs[SUB_REAL].dfd, DAHDI_RINGOFF);
					dahdi_set_hook(p->oprpeer->subs[SUB_REAL].dfd, DAHDI_RING);
					save_conference(p);
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
				}
			}
			break;
		}
		/* Remember last time we got a flash-hook */
		p->flashtime = ast_tvnow();
		switch (mysig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			ast_debug(1, "Winkflash, index: %d, normal: %d, callwait: %d, thirdcall: %d\n",
				idx, p->subs[SUB_REAL].dfd, p->subs[SUB_CALLWAIT].dfd, p->subs[SUB_THREEWAY].dfd);
			p->callwaitcas = 0;

			if (idx != SUB_REAL) {
				ast_log(LOG_WARNING, "Got flash hook with index %d on channel %d?!?\n", idx, p->channel);
				goto winkflashdone;
			}

			if (p->subs[SUB_CALLWAIT].owner) {
				/* Swap to call-wait */
				swap_subs(p, SUB_REAL, SUB_CALLWAIT);
				tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
				p->owner = p->subs[SUB_REAL].owner;
				ast_debug(1, "Making %s the new owner\n", p->owner->name);
				if (p->owner->_state == AST_STATE_RINGING) {
					ast_setstate(p->owner, AST_STATE_UP);
					p->subs[SUB_REAL].needanswer = 1;
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				/* Start music on hold if appropriate */
				if (!p->subs[SUB_CALLWAIT].inthreeway && ast_bridged_channel(p->subs[SUB_CALLWAIT].owner)) {
					ast_queue_control_data(p->subs[SUB_CALLWAIT].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				p->subs[SUB_CALLWAIT].needhold = 1;
				if (ast_bridged_channel(p->subs[SUB_REAL].owner)) {
					ast_queue_control_data(p->subs[SUB_REAL].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				p->subs[SUB_REAL].needunhold = 1;
			} else if (!p->subs[SUB_THREEWAY].owner) {
				if (!p->threewaycalling) {
					/* Just send a flash if no 3-way calling */
					p->subs[SUB_REAL].needflash = 1;
					goto winkflashdone;
				} else if (!check_for_conference(p)) {
					char cid_num[256];
					char cid_name[256];

					cid_num[0] = 0;
					cid_name[0] = 0;
					if (p->dahditrcallerid && p->owner) {
						if (p->owner->cid.cid_num)
							ast_copy_string(cid_num, p->owner->cid.cid_num, sizeof(cid_num));
						if (p->owner->cid.cid_name)
							ast_copy_string(cid_name, p->owner->cid.cid_name, sizeof(cid_name));
					}
					/* XXX This section needs much more error checking!!! XXX */
					/* Start a 3-way call if feasible */
					if (!((ast->pbx) ||
						(ast->_state == AST_STATE_UP) ||
						(ast->_state == AST_STATE_RING))) {
						ast_debug(1, "Flash when call not up or ringing\n");
						goto winkflashdone;
					}
					if (alloc_sub(p, SUB_THREEWAY)) {
						ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
						goto winkflashdone;
					}
					/* Make new channel */
					chan = dahdi_new(p, AST_STATE_RESERVED, 0, SUB_THREEWAY, 0, 0);
					if (p->dahditrcallerid) {
						if (!p->origcid_num)
							p->origcid_num = ast_strdup(p->cid_num);
						if (!p->origcid_name)
							p->origcid_name = ast_strdup(p->cid_name);
						ast_copy_string(p->cid_num, cid_num, sizeof(p->cid_num));
						ast_copy_string(p->cid_name, cid_name, sizeof(p->cid_name));
					}
					/* Swap things around between the three-way and real call */
					swap_subs(p, SUB_THREEWAY, SUB_REAL);
					/* Disable echo canceller for better dialing */
					dahdi_disable_ec(p);
					res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_DIALRECALL);
					if (res)
						ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
					p->owner = chan;
					if (!chan) {
						ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", p->channel);
					} else if (ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan)) {
						ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
						res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
						dahdi_enable_ec(p);
						ast_hangup(chan);
					} else {
 						struct ast_channel *other = ast_bridged_channel(p->subs[SUB_THREEWAY].owner);
 						int way3bridge = 0, cdr3way = 0;

							if (!other) {
 							other = ast_bridged_channel(p->subs[SUB_REAL].owner);
 						} else
 							way3bridge = 1;

							if (p->subs[SUB_THREEWAY].owner->cdr)
 							cdr3way = 1;

						ast_verb(3, "Started three way call on channel %d\n", p->channel);

						/* Start music on hold if appropriate */
						if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
							ast_queue_control_data(p->subs[SUB_THREEWAY].owner, AST_CONTROL_HOLD,
								S_OR(p->mohsuggest, NULL),
								!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
						}
						p->subs[SUB_THREEWAY].needhold = 1;
					}
				}
			} else {
				/* Already have a 3 way call */
				if (p->subs[SUB_THREEWAY].inthreeway) {
					/* Call is already up, drop the last person */
					ast_debug(1, "Got flash with three way call up, dropping last call on %d\n", p->channel);
					/* If the primary call isn't answered yet, use it */
					if ((p->subs[SUB_REAL].owner->_state != AST_STATE_UP) && (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_UP)) {
						/* Swap back -- we're dropping the real 3-way that isn't finished yet*/
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						p->owner = p->subs[SUB_REAL].owner;
					}
					/* Drop the last call and stop the conference */
					ast_verb(3, "Dropping three-way call on %s\n", p->subs[SUB_THREEWAY].owner->name);
					p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
					p->subs[SUB_REAL].inthreeway = 0;
					p->subs[SUB_THREEWAY].inthreeway = 0;
				} else {
					/* Lets see what we're up to */
					if (((ast->pbx) || (ast->_state == AST_STATE_UP)) &&
						(p->transfertobusy || (ast->_state != AST_STATE_BUSY))) {
						int otherindex = SUB_THREEWAY;
						struct ast_channel *other = ast_bridged_channel(p->subs[SUB_THREEWAY].owner);
						int way3bridge = 0, cdr3way = 0;

						if (!other) {
							other = ast_bridged_channel(p->subs[SUB_REAL].owner);
						} else
							way3bridge = 1;

						if (p->subs[SUB_THREEWAY].owner->cdr)
							cdr3way = 1;

						ast_verb(3, "Building conference on call on %s and %s\n", p->subs[SUB_THREEWAY].owner->name, p->subs[SUB_REAL].owner->name);
						/* Put them in the threeway, and flip */
						p->subs[SUB_THREEWAY].inthreeway = 1;
						p->subs[SUB_REAL].inthreeway = 1;
						if (ast->_state == AST_STATE_UP) {
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							otherindex = SUB_REAL;
						}
						if (p->subs[otherindex].owner && ast_bridged_channel(p->subs[otherindex].owner))
							ast_queue_control(p->subs[otherindex].owner, AST_CONTROL_UNHOLD);
						p->subs[otherindex].needunhold = 1;
						p->owner = p->subs[SUB_REAL].owner;
						if (ast->_state == AST_STATE_RINGING) {
							ast_debug(1, "Enabling ringtone on real and threeway\n");
							res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
							res = tone_zone_play_tone(p->subs[SUB_THREEWAY].dfd, DAHDI_TONE_RINGTONE);
						}
					} else {
						ast_verb(3, "Dumping incomplete call on on %s\n", p->subs[SUB_THREEWAY].owner->name);
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
						p->owner = p->subs[SUB_REAL].owner;
						if (p->subs[SUB_REAL].owner && ast_bridged_channel(p->subs[SUB_REAL].owner))
							ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
						p->subs[SUB_REAL].needunhold = 1;
						dahdi_enable_ec(p);
					}
				}
			}
winkflashdone:
			update_conf(p);
			break;
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
		case SIG_FXSLS:
		case SIG_FXSGS:
			if (p->dialing)
				ast_debug(1, "Ignoring wink on channel %d\n", p->channel);
			else
				ast_debug(1, "Got wink in weird state %d on channel %d\n", ast->_state, p->channel);
			break;
		case SIG_FEATDMF_TA:
			switch (p->whichwink) {
			case 0:
				ast_debug(1, "ANI2 set to '%d' and ANI is '%s'\n", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%d%s#", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
				break;
			case 1:
				ast_copy_string(p->dop.dialstr, p->finaldial, sizeof(p->dop.dialstr));
				break;
			case 2:
				ast_log(LOG_WARNING, "Received unexpected wink on channel of type SIG_FEATDMF_TA\n");
				return NULL;
			}
			p->whichwink++;
			/* Fall through */
		case SIG_FEATDMF:
		case SIG_E911:
		case SIG_FGC_CAMAMF:
		case SIG_FGC_CAMA:
		case SIG_FEATB:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
			/* FGD MF *Must* wait for wink */
			if (!ast_strlen_zero(p->dop.dialstr)) {
				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else
					ast_debug(1, "Sent deferred digit string: %s\n", p->dop.dialstr);
			}
			p->dop.dialstr[0] = '\0';
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
		}
		break;
	case DAHDI_EVENT_HOOKCOMPLETE:
		if (p->inalarm) break;
		if ((p->radio || (p->oprmode < 0))) break;
		if (p->waitingfordt.tv_sec) break;
		switch (mysig) {
		case SIG_FXSLS:  /* only interesting for FXS */
		case SIG_FXSGS:
		case SIG_FXSKS:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
			if (!ast_strlen_zero(p->dop.dialstr)) {
				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else
					ast_debug(1, "Sent deferred digit string: %s\n", p->dop.dialstr);
			}
			p->dop.dialstr[0] = '\0';
			p->dop.op = DAHDI_DIAL_OP_REPLACE;
			break;
		case SIG_FEATDMF:
		case SIG_FEATDMF_TA:
		case SIG_E911:
		case SIG_FGC_CAMA:
		case SIG_FGC_CAMAMF:
		case SIG_FEATB:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
			ast_debug(1, "Got hook complete in MF FGD, waiting for wink now on channel %d\n",p->channel);
			break;
		default:
			break;
		}
		break;
	case DAHDI_EVENT_POLARITY:
		/*
		 * If we get a Polarity Switch event, check to see
		 * if we should change the polarity state and
		 * mark the channel as UP or if this is an indication
		 * of remote end disconnect.
		 */
		if (p->polarity == POLARITY_IDLE) {
			p->polarity = POLARITY_REV;
			if (p->answeronpolarityswitch &&
				((ast->_state == AST_STATE_DIALING) ||
				(ast->_state == AST_STATE_RINGING))) {
				ast_debug(1, "Answering on polarity switch!\n");
				ast_setstate(p->owner, AST_STATE_UP);
				if (p->hanguponpolarityswitch) {
					p->polaritydelaytv = ast_tvnow();
				}
			} else
				ast_debug(1, "Ignore switch to REVERSED Polarity on channel %d, state %d\n", p->channel, ast->_state);
		}
		/* Removed else statement from here as it was preventing hangups from ever happening*/
		/* Added AST_STATE_RING in if statement below to deal with calling party hangups that take place when ringing */
		if (p->hanguponpolarityswitch &&
			(p->polarityonanswerdelay > 0) &&
			(p->polarity == POLARITY_REV) &&
			((ast->_state == AST_STATE_UP) || (ast->_state == AST_STATE_RING)) ) {
			/* Added log_debug information below to provide a better indication of what is going on */
			ast_debug(1, "Polarity Reversal event occured - DEBUG 1: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %d\n", p->channel, ast->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );

			if (ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay) {
				ast_debug(1, "Polarity Reversal detected and now Hanging up on channel %d\n", p->channel);
				ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
				p->polarity = POLARITY_IDLE;
			} else
				ast_debug(1, "Polarity Reversal detected but NOT hanging up (too close to answer event) on channel %d, state %d\n", p->channel, ast->_state);

		} else {
			p->polarity = POLARITY_IDLE;
			ast_debug(1, "Ignoring Polarity switch to IDLE on channel %d, state %d\n", p->channel, ast->_state);
		}
		/* Added more log_debug information below to provide a better indication of what is going on */
		ast_debug(1, "Polarity Reversal event occured - DEBUG 2: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %d\n", p->channel, ast->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
		break;
	default:
		ast_debug(1, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[idx].f;
}

static struct ast_frame *__dahdi_exception(struct ast_channel *ast)
{
	int res;
	int idx;
	struct ast_frame *f;
	int usedindex = -1;
	struct dahdi_pvt *p = ast->tech_pvt;

	idx = dahdi_get_index(ast, p, 1);

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass = 0;
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "dahdi_exception";
	p->subs[idx].f.data.ptr = NULL;


	if ((!p->owner) && (!(p->radio || (p->oprmode < 0)))) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		if (p->fake_event) {
			res = p->fake_event;
			p->fake_event = 0;
		} else
			res = dahdi_get_event(p->subs[SUB_REAL].dfd);
		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != DAHDI_EVENT_RINGEROFF) && (res != DAHDI_EVENT_RINGERON) &&
			(res != DAHDI_EVENT_HOOKCOMPLETE)) {
			ast_debug(1, "Restoring owner of channel %d on event %d\n", p->channel, res);
			p->owner = p->subs[SUB_REAL].owner;
			if (p->owner && ast_bridged_channel(p->owner))
				ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
			p->subs[SUB_REAL].needunhold = 1;
		}
		switch (res) {
		case DAHDI_EVENT_ONHOOK:
			dahdi_disable_ec(p);
			if (p->owner) {
				ast_verb(3, "Channel %s still has call, ringing phone\n", p->owner->name);
				dahdi_ring_phone(p);
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		case DAHDI_EVENT_RINGOFFHOOK:
			dahdi_enable_ec(p);
			dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			if (p->owner && (p->owner->_state == AST_STATE_RINGING)) {
				p->subs[SUB_REAL].needanswer = 1;
				p->dialing = 0;
			}
			break;
		case DAHDI_EVENT_HOOKCOMPLETE:
		case DAHDI_EVENT_RINGERON:
		case DAHDI_EVENT_RINGEROFF:
			/* Do nothing */
			break;
		case DAHDI_EVENT_WINKFLASH:
			p->flashtime = ast_tvnow();
			if (p->owner) {
				ast_verb(3, "Channel %d flashed to other channel %s\n", p->channel, p->owner->name);
				if (p->owner->_state != AST_STATE_UP) {
					/* Answer if necessary */
					usedindex = dahdi_get_index(p->owner, p, 0);
					if (usedindex > -1) {
						p->subs[usedindex].needanswer = 1;
					}
					ast_setstate(p->owner, AST_STATE_UP);
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				if (ast_bridged_channel(p->owner))
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
				p->subs[SUB_REAL].needunhold = 1;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		f = &p->subs[idx].f;
		return f;
	}
	if (!(p->radio || (p->oprmode < 0)))
		ast_debug(1, "Exception on %d, channel %d\n", ast->fds[0],p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast->name, p->owner->name);
		f = &p->subs[idx].f;
		return f;
	}
	f = dahdi_handle_event(ast);
	return f;
}

static struct ast_frame *dahdi_exception(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	struct ast_frame *f;
	ast_mutex_lock(&p->lock);
	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		struct analog_pvt *analog_p = p->sig_pvt;
		f = analog_exception(analog_p, ast);
	} else {
		f = __dahdi_exception(ast);
	}
	ast_mutex_unlock(&p->lock);
	return f;
}

static struct ast_frame *dahdi_read(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	int res;
	int idx;
	void *readbuf;
	struct ast_frame *f;

	while (ast_mutex_trylock(&p->lock)) {
		CHANNEL_DEADLOCK_AVOIDANCE(ast);
	}

	idx = dahdi_get_index(ast, p, 0);

	/* Hang up if we don't really exist */
	if (idx < 0)	{
		ast_log(LOG_WARNING, "We dont exist?\n");
		ast_mutex_unlock(&p->lock);
		return NULL;
	}

	if ((p->radio || (p->oprmode < 0)) && p->inalarm) return NULL;

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass = 0;
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "dahdi_read";
	p->subs[idx].f.data.ptr = NULL;

	/* make sure it sends initial key state as first frame */
	if ((p->radio || (p->oprmode < 0)) && (!p->firstradio))
	{
		struct dahdi_params ps;

		memset(&ps, 0, sizeof(ps));
		ps.channo = p->channel;
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		p->firstradio = 1;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		if (ps.rxisoffhook)
		{
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_KEY;
		}
		else
		{
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_UNKEY;
		}
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->ringt == 1) {
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	else if (p->ringt > 0)
		p->ringt--;

#ifdef HAVE_OPENR2
	if (p->mfcr2) {
		openr2_chan_process_event(p->r2chan);
	}
#endif

	if (p->subs[idx].needringing) {
		/* Send ringing frame if requested */
		p->subs[idx].needringing = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_RINGING;
		ast_setstate(ast, AST_STATE_RINGING);
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needbusy) {
		/* Send busy frame if requested */
		p->subs[idx].needbusy = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_BUSY;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needcongestion) {
		/* Send congestion frame if requested */
		p->subs[idx].needcongestion = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_CONGESTION;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needcallerid) {
		ast_set_callerid(ast, S_OR(p->lastcid_num, NULL),
							S_OR(p->lastcid_name, NULL),
							S_OR(p->lastcid_num, NULL)
							);
		p->subs[idx].needcallerid = 0;
	}

	if (p->subs[idx].needanswer) {
		/* Send answer frame if requested */
		p->subs[idx].needanswer = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
#ifdef HAVE_OPENR2
	if (p->mfcr2 && openr2_chan_get_read_enabled(p->r2chan)) {
		/* openr2 took care of reading and handling any event
		  (needanswer, needbusy etc), if we continue we will read()
		  twice, lets just return a null frame. This should only
		  happen when openr2 is dialing out */
		ast_mutex_unlock(&p->lock);
		return &ast_null_frame;
	}
#endif

	if (p->subs[idx].needflash) {
		/* Send answer frame if requested */
		p->subs[idx].needflash = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_FLASH;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needhold) {
		/* Send answer frame if requested */
		p->subs[idx].needhold = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_HOLD;
		ast_mutex_unlock(&p->lock);
		ast_debug(1, "Sending hold on '%s'\n", ast->name);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needunhold) {
		/* Send answer frame if requested */
		p->subs[idx].needunhold = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass = AST_CONTROL_UNHOLD;
		ast_mutex_unlock(&p->lock);
		ast_debug(1, "Sending unhold on '%s'\n", ast->name);
		return &p->subs[idx].f;
	}

	if (ast->rawreadformat == AST_FORMAT_SLINEAR) {
		if (!p->subs[idx].linear) {
			p->subs[idx].linear = 1;
			res = dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, idx);
		}
	} else if ((ast->rawreadformat == AST_FORMAT_ULAW) ||
		(ast->rawreadformat == AST_FORMAT_ALAW)) {
		if (p->subs[idx].linear) {
			p->subs[idx].linear = 0;
			res = dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to companded mode.\n", p->channel, idx);
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_getformatname(ast->rawreadformat));
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->subs[idx].buffer) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	res = read(p->subs[idx].dfd, readbuf, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
	ast_clear_flag(ast, AST_FLAG_BLOCKING);
	/* Check for hangup */
	if (res < 0) {
		f = NULL;
		if (res == -1) {
			if (errno == EAGAIN) {
				/* Return "NULL" frame if there is nobody there */
				ast_mutex_unlock(&p->lock);
				return &p->subs[idx].f;
			} else if (errno == ELAST) {
				f = __dahdi_exception(ast);
			} else
				ast_log(LOG_WARNING, "dahdi_rec: %s\n", strerror(errno));
		}
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (res != (p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE)) {
		ast_debug(1, "Short read (%d/%d), must be an event...\n", res, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
		f = __dahdi_exception(ast);
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (p->tdd) { /* if in TDD mode, see if we receive that */
		int c;

		c = tdd_feed(p->tdd,readbuf,READ_SIZE);
		if (c < 0) {
			ast_debug(1,"tdd_feed failed\n");
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		if (c) { /* if a char to return */
			p->subs[idx].f.subclass = 0;
			p->subs[idx].f.frametype = AST_FRAME_TEXT;
			p->subs[idx].f.mallocd = 0;
			p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
			p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET;
			p->subs[idx].f.datalen = 1;
			*((char *) p->subs[idx].f.data.ptr) = c;
			ast_mutex_unlock(&p->lock);
			return &p->subs[idx].f;
		}
	}
	/* Ensure the CW timer decrements only on a single subchannel */
	if (p->callwaitingrepeat && dahdi_get_index(ast, p, 1) == SUB_REAL) {
		p->callwaitingrepeat--;
	}
	if (p->cidcwexpire)
		p->cidcwexpire--;
	/* Repeat callwaiting */
	if (p->callwaitingrepeat == 1) {
		p->callwaitrings++;
		dahdi_callwait(ast);
	}
	/* Expire CID/CW */
	if (p->cidcwexpire == 1) {
		ast_verb(3, "CPE does not support Call Waiting Caller*ID.\n");
		restore_conference(p);
	}
	if (p->subs[idx].linear) {
		p->subs[idx].f.datalen = READ_SIZE * 2;
	} else
		p->subs[idx].f.datalen = READ_SIZE;

	/* Handle CallerID Transmission */
	if ((p->owner == ast) && p->cidspill) {
		send_callerid(p);
	}

	p->subs[idx].f.frametype = AST_FRAME_VOICE;
	p->subs[idx].f.subclass = ast->rawreadformat;
	p->subs[idx].f.samples = READ_SIZE;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
	p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET / sizeof(p->subs[idx].buffer[0]);
#if 0
	ast_debug(1, "Read %d of voice on %s\n", p->subs[idx].f.datalen, ast->name);
#endif
	{
	struct analog_pvt *ap = p->sig_pvt;
	if ((analog_lib_handles(p->sig ,p->radio, p->oprmode) && ap->dialing) || p->dialing ||  p->radio || /* Transmitting something */
		(idx && (ast->_state != AST_STATE_UP)) || /* Three-way or callwait that isn't up */
		((idx == SUB_CALLWAIT) && !p->subs[SUB_CALLWAIT].inthreeway) /* Inactive and non-confed call-wait */
		) {
		/* Whoops, we're still dialing, or in a state where we shouldn't transmit....
		   don't send anything */
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass = 0;
		p->subs[idx].f.samples = 0;
		p->subs[idx].f.mallocd = 0;
		p->subs[idx].f.offset = 0;
		p->subs[idx].f.data.ptr = NULL;
		p->subs[idx].f.datalen= 0;
	}
	}
	if (p->dsp && (!p->ignoredtmf || p->callwaitcas || p->busydetect || p->callprogress || p->waitingfordt.tv_sec) && !idx) {
		/* Perform busy detection etc on the dahdi line */
		int mute;

		f = ast_dsp_process(ast, p->dsp, &p->subs[idx].f);

		/* Check if DSP code thinks we should be muting this frame and mute the conference if so */
		mute = ast_dsp_was_muted(p->dsp);
		if (p->muting != mute) {
			p->muting = mute;
			dahdi_confmute(p, mute);
		}

		if (f) {
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_BUSY)) {
				if ((ast->_state == AST_STATE_UP) && !p->outgoing) {
					/* Treat this as a "hangup" instead of a "busy" on the assumption that
					   a busy */
					f = NULL;
				}
			} else if (f->frametype == AST_FRAME_DTMF) {
#ifdef HAVE_PRI
				if (!((struct sig_pri_chan *)(p->sig_pvt))->proceeding && ((p->sig == SIG_PRI) || (p->sig == SIG_BRI) || (p->sig == SIG_BRI_PTMP)) && p->pri &&
					((!p->outgoing && (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) ||
					(p->outgoing && (p->pri->overlapdial & DAHDI_OVERLAPDIAL_OUTGOING)))) {
					/* Don't accept in-band DTMF when in overlap dial mode */
					f->frametype = AST_FRAME_NULL;
					f->subclass = 0;
				}
#endif
				/* DSP clears us of being pulse */
				p->pulsedial = 0;
			} else if (p->waitingfordt.tv_sec) {
				if (ast_tvdiff_ms(ast_tvnow(), p->waitingfordt) >= p->waitfordialtone ) {
					p->waitingfordt.tv_sec = 0;
					ast_log(LOG_WARNING, "Never saw dialtone on channel %d\n", p->channel);
					f=NULL;
				} else if (f->frametype == AST_FRAME_VOICE) {
					f->frametype = AST_FRAME_NULL;
					f->subclass = 0;
					if ((ast_dsp_get_tstate(p->dsp) == DSP_TONE_STATE_DIALTONE || ast_dsp_get_tstate(p->dsp) == DSP_TONE_STATE_RINGING) && ast_dsp_get_tcount(p->dsp) > 9) {
						p->waitingfordt.tv_sec = 0;
						p->dsp_features &= ~DSP_FEATURE_WAITDIALTONE;
						ast_dsp_set_features(p->dsp, p->dsp_features);
						ast_log(LOG_DEBUG, "Got 10 samples of dialtone!\n");
						if (!ast_strlen_zero(p->dop.dialstr)) { /* Dial deferred digits */
							res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
							if (res < 0) {
								ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
								p->dop.dialstr[0] = '\0';
								return NULL;
							} else {
								ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
								p->dialing = 1;
								p->dop.dialstr[0] = '\0';
								p->dop.op = DAHDI_DIAL_OP_REPLACE;
								ast_setstate(ast, AST_STATE_DIALING);
							}
						}
					}
				}
			}
		}
	} else
		f = &p->subs[idx].f;

	if (f && (f->frametype == AST_FRAME_DTMF)) {
		if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
			analog_handle_dtmfup(p->sig_pvt, ast, idx, &f);
		} else 
			dahdi_handle_dtmfup(ast, idx, &f);
	}

	/* If we have a fake_event, trigger exception to handle it */
	if (p->fake_event)
		ast_set_flag(ast, AST_FLAG_EXCEPTION);

	ast_mutex_unlock(&p->lock);
	return f;
}

static int my_dahdi_write(struct dahdi_pvt *p, unsigned char *buf, int len, int idx, int linear)
{
	int sent=0;
	int size;
	int res;
	int fd;
	fd = p->subs[idx].dfd;
	while (len) {
		size = len;
		if (size > (linear ? READ_SIZE * 2 : READ_SIZE))
			size = (linear ? READ_SIZE * 2 : READ_SIZE);
		res = write(fd, buf, size);
		if (res != size) {
			ast_debug(1, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			return sent;
		}
		len -= size;
		buf += size;
	}
	return sent;
}

static int dahdi_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct dahdi_pvt *p = ast->tech_pvt;
	int res;
	int idx;
	idx = dahdi_get_index(ast, p, 0);
	if (idx < 0) {
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast->name);
		return -1;
	}

#if 0
#ifdef HAVE_PRI
	ast_mutex_lock(&p->lock);
	if (!p->proceeding && p->sig==SIG_PRI && p->pri && !p->outgoing) {
		if (p->pri->pri) {
			if (!pri_grab(p, p->pri)) {
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
					pri_rel(p->pri);
			} else
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
		}
		p->proceeding=1;
	}
	ast_mutex_unlock(&p->lock);
#endif
#endif
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING, "Don't know what to do with frame type '%d'\n", frame->frametype);
		return 0;
	}
	if ((frame->subclass != AST_FORMAT_SLINEAR) &&
		(frame->subclass != AST_FORMAT_ULAW) &&
		(frame->subclass != AST_FORMAT_ALAW)) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return -1;
	}
	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		struct analog_pvt *ap = p->sig_pvt;

		if (ap->dialing)
			return 0;
	}
	if (p->dialing) {
		ast_debug(1, "Dropping frame since I'm still dialing on %s...\n",ast->name);
		return 0;
	}
	if (!p->owner) {
		ast_debug(1, "Dropping frame since there is no active owner on %s...\n",ast->name);
		return 0;
	}
	if (p->cidspill) {
		ast_debug(1, "Dropping frame since I've still got a callerid spill\n");
		return 0;
	}
	/* Return if it's not valid data */
	if (!frame->data.ptr || !frame->datalen)
		return 0;

	if (frame->subclass == AST_FORMAT_SLINEAR) {
		if (!p->subs[idx].linear) {
			p->subs[idx].linear = 1;
			res = dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
		res = my_dahdi_write(p, (unsigned char *)frame->data.ptr, frame->datalen, idx, 1);
	} else {
		/* x-law already */
		if (p->subs[idx].linear) {
			p->subs[idx].linear = 0;
			res = dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set companded mode on channel %d\n", p->channel);
		}
		res = my_dahdi_write(p, (unsigned char *)frame->data.ptr, frame->datalen, idx, 0);
	}
	if (res < 0) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int dahdi_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	struct dahdi_pvt *p = chan->tech_pvt;
	int res=-1;
	int idx;
	int func = DAHDI_FLASH;
	ast_mutex_lock(&p->lock);
	idx = dahdi_get_index(chan, p, 0);
	ast_debug(1, "Requested indication %d on channel %s\n", condition, chan->name);
#ifdef HAVE_OPENR2
	if (p->mfcr2 && !p->mfcr2_call_accepted) {
		ast_mutex_unlock(&p->lock);
		/* if this is an R2 call and the call is not yet accepted, we don't want the
		   tone indications to mess up with the MF tones */
		return 0;
	}
#endif
#ifdef HAVE_PRI
	if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
		res = sig_pri_indicate(p->sig_pvt, chan, condition, data, datalen);
		ast_mutex_unlock(&p->lock);
		return res;
	}
#endif
	if (idx == SUB_REAL) {
		switch (condition) {
		case AST_CONTROL_BUSY:
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_BUSY);
			break;
		case AST_CONTROL_RINGING:
#ifdef HAVE_SS7
			if ((!p->alerting) && (p->sig == SIG_SS7) && p->ss7 && !p->outgoing && (chan->_state != AST_STATE_UP)) {
				if (p->ss7->ss7) {
					ss7_grab(p, p->ss7);

					if ((isup_far(p->ss7->ss7, p->ss7call)) != -1)
						p->rlt = 1;
					if (p->rlt != 1) /* No need to send CPG if call will be RELEASE */
						isup_cpg(p->ss7->ss7, p->ss7call, CPG_EVENT_ALERTING);
					p->alerting = 1;
					ss7_rel(p->ss7);
				}
			}
#endif

			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_RINGTONE);

			if (chan->_state != AST_STATE_UP) {
				if ((chan->_state != AST_STATE_RING) ||
					((p->sig != SIG_FXSKS) &&
				 (p->sig != SIG_FXSLS) &&
				 (p->sig != SIG_FXSGS)))
				ast_setstate(chan, AST_STATE_RINGING);
			}
			break;
		case AST_CONTROL_PROCEEDING:
			ast_debug(1,"Received AST_CONTROL_PROCEEDING on %s\n",chan->name);
#ifdef HAVE_SS7
			/* This IF sends the FAR for an answered ALEG call */
			if (chan->_state == AST_STATE_UP && (p->rlt != 1) && (p->sig == SIG_SS7)){
				if ((isup_far(p->ss7->ss7, p->ss7call)) != -1)
					p->rlt = 1;
			}

			if (!p->proceeding && p->sig == SIG_SS7 && p->ss7 && !p->outgoing) {
				if (p->ss7->ss7) {
					ss7_grab(p, p->ss7);
					isup_acm(p->ss7->ss7, p->ss7call);
					p->proceeding = 1;
					ss7_rel(p->ss7);

				}
			}
#endif
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_PROGRESS:
			ast_debug(1,"Received AST_CONTROL_PROGRESS on %s\n",chan->name);
#ifdef HAVE_SS7
			if (!p->progress && p->sig==SIG_SS7 && p->ss7 && !p->outgoing) {
				if (p->ss7->ss7) {
					ss7_grab(p, p->ss7);
					isup_cpg(p->ss7->ss7, p->ss7call, CPG_EVENT_INBANDINFO);
					p->progress = 1;
					ss7_rel(p->ss7);
					/* enable echo canceler here on SS7 calls */
					dahdi_enable_ec(p);

				}
			}
#endif
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_CONGESTION:
			chan->hangupcause = AST_CAUSE_CONGESTION;
			break;
		case AST_CONTROL_HOLD:
			ast_moh_start(chan, data, p->mohinterpret);
			break;
		case AST_CONTROL_UNHOLD:
			ast_moh_stop(chan);
			break;
		case AST_CONTROL_RADIO_KEY:
			if (p->radio)
				res = dahdi_set_hook(p->subs[idx].dfd, DAHDI_OFFHOOK);
			res = 0;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			if (p->radio)
				res = dahdi_set_hook(p->subs[idx].dfd, DAHDI_RINGOFF);
			res = 0;
			break;
		case AST_CONTROL_FLASH:
			/* flash hookswitch */
			if (ISTRUNK(p) && (p->sig != SIG_PRI)) {
				/* Clear out the dial buffer */
				p->dop.dialstr[0] = '\0';
				if ((ioctl(p->subs[SUB_REAL].dfd,DAHDI_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
					ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n",
						chan->name, strerror(errno));
				} else
					res = 0;
			} else
				res = 0;
			break;
		case AST_CONTROL_SRCUPDATE:
			res = 0;
			break;
		case -1:
			res = tone_zone_play_tone(p->subs[idx].dfd, -1);
			break;
		}
	} else
		res = 0;
	ast_mutex_unlock(&p->lock);
	return res;
}

static struct ast_channel *dahdi_new(struct dahdi_pvt *i, int state, int startpbx, int idx, int law, int transfercapability)
{
	struct ast_channel *tmp;
	int deflaw;
	int res;
	int x,y;
	int features;
	struct ast_str *chan_name;
	struct ast_variable *v;
	struct dahdi_params ps;
	if (i->subs[idx].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[idx]);
		return NULL;
	}
	y = 1;
	chan_name = ast_str_alloca(32);
	do {
		if (i->channel == CHAN_PSEUDO)
			ast_str_set(&chan_name, 0, "pseudo-%ld", ast_random());
		else
			ast_str_set(&chan_name, 0, "%d-%d", i->channel, y);
		for (x = 0; x < 3; x++) {
			if ((idx != x) && i->subs[x].owner && !strcasecmp(ast_str_buffer(chan_name), i->subs[x].owner->name + 6))
				break;
		}
		y++;
	} while (x < 3);
	tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, i->amaflags, "DAHDI/%s", ast_str_buffer(chan_name));
	if (!tmp)
		return NULL;
	tmp->tech = &dahdi_tech;
	memset(&ps, 0, sizeof(ps));
	ps.channo = i->channel;
	res = ioctl(i->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps);
	if (res) {
		ast_log(LOG_WARNING, "Unable to get parameters, assuming MULAW: %s\n", strerror(errno));
		ps.curlaw = DAHDI_LAW_MULAW;
	}
	if (ps.curlaw == DAHDI_LAW_ALAW)
		deflaw = AST_FORMAT_ALAW;
	else
		deflaw = AST_FORMAT_ULAW;
	if (law) {
		if (law == DAHDI_LAW_ALAW)
			deflaw = AST_FORMAT_ALAW;
		else
			deflaw = AST_FORMAT_ULAW;
	}
	ast_channel_set_fd(tmp, 0, i->subs[idx].dfd);
	tmp->nativeformats = deflaw;
	/* Start out assuming ulaw since it's smaller :) */
	tmp->rawreadformat = deflaw;
	tmp->readformat = deflaw;
	tmp->rawwriteformat = deflaw;
	tmp->writeformat = deflaw;
	i->subs[idx].linear = 0;
	dahdi_setlinear(i->subs[idx].dfd, i->subs[idx].linear);
	features = 0;
	if (idx == SUB_REAL) {
		if (i->busydetect && CANBUSYDETECT(i))
			features |= DSP_FEATURE_BUSY_DETECT;
		if ((i->callprogress & CALLPROGRESS_PROGRESS) && CANPROGRESSDETECT(i))
			features |= DSP_FEATURE_CALL_PROGRESS;
		if ((i->waitfordialtone) && CANPROGRESSDETECT(i))
			features |= DSP_FEATURE_WAITDIALTONE;
		if ((!i->outgoing && (i->callprogress & CALLPROGRESS_FAX_INCOMING)) ||
			(i->outgoing && (i->callprogress & CALLPROGRESS_FAX_OUTGOING))) {
			features |= DSP_FEATURE_FAX_DETECT;
		}
		x = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;
		if (ioctl(i->subs[idx].dfd, DAHDI_TONEDETECT, &x)) {
			i->hardwaredtmf = 0;
			features |= DSP_FEATURE_DIGIT_DETECT;
		} else if (NEED_MFDETECT(i)) {
			i->hardwaredtmf = 1;
			features |= DSP_FEATURE_DIGIT_DETECT;
		}
	}
	if (features) {
		if (i->dsp) {
			ast_debug(1, "Already have a dsp on %s?\n", tmp->name);
		} else {
			if (i->channel != CHAN_PSEUDO)
				i->dsp = ast_dsp_new();
			else
				i->dsp = NULL;
			if (i->dsp) {
				i->dsp_features = features;
#if defined(HAVE_SS7)
				/* We cannot do progress detection until receives PROGRESS message */
				if (i->outgoing && (i->sig == SIG_SS7)) {
					/* Remember requested DSP features, don't treat
					   talking as ANSWER */
					i->dsp_features = features & ~DSP_PROGRESS_TALK;
					features = 0;
				}
#endif
				ast_dsp_set_features(i->dsp, features);
				ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
				if (!ast_strlen_zero(progzone))
					ast_dsp_set_call_progress_zone(i->dsp, progzone);
				if (i->busydetect && CANBUSYDETECT(i)) {
					ast_dsp_set_busy_count(i->dsp, i->busycount);
					ast_dsp_set_busy_pattern(i->dsp, i->busy_tonelength, i->busy_quietlength);
				}
			}
		}
	}

	if (state == AST_STATE_RING)
		tmp->rings = 1;
	tmp->tech_pvt = i;
	if ((i->sig == SIG_FXOKS) || (i->sig == SIG_FXOGS) || (i->sig == SIG_FXOLS)) {
		/* Only FXO signalled stuff can be picked up */
		tmp->callgroup = i->callgroup;
		tmp->pickupgroup = i->pickupgroup;
	}
	if (!ast_strlen_zero(i->parkinglot))
		ast_string_field_set(tmp, parkinglot, i->parkinglot);
	if (!ast_strlen_zero(i->language))
		ast_string_field_set(tmp, language, i->language);
	if (!i->owner)
		i->owner = tmp;
	if (!ast_strlen_zero(i->accountcode))
		ast_string_field_set(tmp, accountcode, i->accountcode);
	if (i->amaflags)
		tmp->amaflags = i->amaflags;
	i->subs[idx].owner = tmp;
	ast_copy_string(tmp->context, i->context, sizeof(tmp->context));
	ast_string_field_set(tmp, call_forward, i->call_forward);
	/* If we've been told "no ADSI" then enforce it */
	if (!i->adsi)
		tmp->adsicpe = AST_ADSI_UNAVAILABLE;
	if (!ast_strlen_zero(i->exten))
		ast_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));
	if (!ast_strlen_zero(i->rdnis))
		tmp->cid.cid_rdnis = ast_strdup(i->rdnis);
	if (!ast_strlen_zero(i->dnid))
		tmp->cid.cid_dnid = ast_strdup(i->dnid);

	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
#ifdef PRI_ANI
	if (!ast_strlen_zero(i->cid_ani))
		tmp->cid.cid_ani = ast_strdup(i->cid_ani);
	else
		tmp->cid.cid_ani = ast_strdup(i->cid_num);
#else
	tmp->cid.cid_ani = ast_strdup(i->cid_num);
#endif
	tmp->cid.cid_pres = i->callingpres;
	tmp->cid.cid_ton = i->cid_ton;
	tmp->cid.cid_ani2 = i->cid_ani2;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	tmp->transfercapability = transfercapability;
	pbx_builtin_setvar_helper(tmp, "TRANSFERCAPABILITY", ast_transfercapability2str(transfercapability));
	if (transfercapability & AST_TRANS_CAP_DIGITAL)
		i->digital = 1;
	/* Assume calls are not idle calls unless we're told differently */
	i->isidlecall = 0;
	i->alreadyhungup = 0;
#endif
	/* clear the fake event in case we posted one before we had ast_channel */
	i->fake_event = 0;
	/* Assure there is no confmute on this channel */
	dahdi_confmute(i, 0);
	i->muting = 0;
	/* Configure the new channel jb */
	ast_jb_configure(tmp, &global_jbconf);

	ast_devstate_changed_literal(ast_state_chan2dev(state), tmp->name);

	for (v = i->vars ; v ; v = v->next)
		pbx_builtin_setvar_helper(tmp, v->name, v->value);

	if (startpbx) {
#ifdef HAVE_OPENR2
		if (i->mfcr2call) {
			pbx_builtin_setvar_helper(tmp, "MFCR2_CATEGORY", openr2_proto_get_category_string(i->mfcr2_recvd_category));
		}
#endif
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
			ast_hangup(tmp);
			i->owner = NULL;
			return NULL;
		}
	}

	ast_module_ref(ast_module_info->self);
	return tmp;
}


static int my_getsigstr(struct ast_channel *chan, char *str, const char *term, int ms)
{
	char c;

	*str = 0; /* start with empty output buffer */
	for (;;)
	{
		/* Wait for the first digit (up to specified ms). */
		c = ast_waitfordigit(chan, ms);
		/* if timeout, hangup or error, return as such */
		if (c < 1)
			return c;
		*str++ = c;
		*str = 0;
		if (strchr(term, c))
			return 1;
	}
}

static int dahdi_wink(struct dahdi_pvt *p, int idx)
{
	int j;
	dahdi_set_hook(p->subs[idx].dfd, DAHDI_WINK);
	for (;;)
	{
		/* set bits of interest */
		j = DAHDI_IOMUX_SIGEVENT;
		/* wait for some happening */
		if (ioctl(p->subs[idx].dfd,DAHDI_IOMUX,&j) == -1) return(-1);
		/* exit loop if we have it */
		if (j & DAHDI_IOMUX_SIGEVENT) break;
	}
	/* get the event info */
	if (ioctl(p->subs[idx].dfd,DAHDI_GETEVENT,&j) == -1) return(-1);
	return 0;
}

/*! \brief enable or disable the chan_dahdi Do-Not-Disturb mode for a DAHDI channel
 * \param dahdichan "Physical" DAHDI channel (e.g: DAHDI/5)
 * \param on 1 to enable, 0 to disable
 *
 * chan_dahdi has a DND (Do Not Disturb) mode for each dahdichan (physical
 * DAHDI channel). Use this to enable or disable it.
 *
 * \bug the use of the word "channel" for those dahdichans is really confusing.
 */
static void dahdi_dnd(struct dahdi_pvt *dahdichan, int on)
{
	/* Do not disturb */
	dahdichan->dnd = on;
	ast_verb(3, "%s DND on channel %d\n",
			on? "Enabled" : "Disabled",
			dahdichan->channel);
	manager_event(EVENT_FLAG_SYSTEM, "DNDState",
			"Channel: DAHDI/%d\r\n"
			"Status: %s\r\n", dahdichan->channel,
			on? "enabled" : "disabled");
}

static void *analog_ss_thread(void *data)
{
	struct ast_channel *chan = data;
	struct dahdi_pvt *p = chan->tech_pvt;
	char exten[AST_MAX_EXTENSION] = "";
	char exten2[AST_MAX_EXTENSION] = "";
	unsigned char buf[256];
	char dtmfcid[300];
	char dtmfbuf[300];
	struct callerid_state *cs = NULL;
	char *name = NULL, *number = NULL;
	int distMatches;
	int curRingData[3];
	int receivedRingT;
	int counter1;
	int counter;
	int samples = 0;
	struct ast_smdi_md_message *smdi_msg = NULL;
	int flags;
	int i;
	int timeout;
	int getforward = 0;
	char *s1, *s2;
	int len = 0;
	int res;
	int idx;

	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count++;
	ast_mutex_unlock(&ss_thread_lock);
	/* in the bizarre case where the channel has become a zombie before we
	   even get started here, abort safely
	*/
	if (!p) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", chan->name);
		ast_hangup(chan);
		goto quit;
	}
	ast_verb(3, "Starting simple switch on '%s'\n", chan->name);
	idx = dahdi_get_index(chan, p, 1);
	if (idx < 0) {
		ast_log(LOG_WARNING, "Huh?\n");
		ast_hangup(chan);
		goto quit;
	}
	if (p->dsp)
		ast_dsp_digitreset(p->dsp);
	switch (p->sig) {
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_FEATDMF_TA:
	case SIG_E911:
	case SIG_FGC_CAMAMF:
	case SIG_FEATB:
	case SIG_EMWINK:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_SF_FEATB:
	case SIG_SFWINK:
		if (dahdi_wink(p, idx))
			goto quit;
		/* Fall through */
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_SF:
	case SIG_FGC_CAMA:
		res = tone_zone_play_tone(p->subs[idx].dfd, -1);
		if (p->dsp)
			ast_dsp_digitreset(p->dsp);
		/* set digit mode appropriately */
		if (p->dsp) {
			if (NEED_MFDETECT(p))
				ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MF | p->dtmfrelax);
			else
				ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
		}
		memset(dtmfbuf, 0, sizeof(dtmfbuf));
		/* Wait for the first digit only if immediate=no */
		if (!p->immediate)
			/* Wait for the first digit (up to 5 seconds). */
			res = ast_waitfordigit(chan, 5000);
		else
			res = 0;
		if (res > 0) {
			/* save first char */
			dtmfbuf[0] = res;
			switch (p->sig) {
			case SIG_FEATD:
			case SIG_SF_FEATD:
				res = my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
				if (res > 0)
					res = my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				break;
			case SIG_FEATDMF_TA:
				res = my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				if (dahdi_wink(p, idx)) goto quit;
				dtmfbuf[0] = 0;
				/* Wait for the first digit (up to 5 seconds). */
				res = ast_waitfordigit(chan, 5000);
				if (res <= 0) break;
				dtmfbuf[0] = res;
				/* fall through intentionally */
			case SIG_FEATDMF:
			case SIG_E911:
			case SIG_FGC_CAMAMF:
			case SIG_SF_FEATDMF:
				res = my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				/* if international caca, do it again to get real ANO */
				if ((p->sig == SIG_FEATDMF) && (dtmfbuf[1] != '0') && (strlen(dtmfbuf) != 14))
				{
					if (dahdi_wink(p, idx)) goto quit;
					dtmfbuf[0] = 0;
					/* Wait for the first digit (up to 5 seconds). */
					res = ast_waitfordigit(chan, 5000);
					if (res <= 0) break;
					dtmfbuf[0] = res;
					res = my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				}
				if (res > 0) {
					/* if E911, take off hook */
					if (p->sig == SIG_E911)
						dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
					res = my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "#", 3000);
				}
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				break;
			case SIG_FEATB:
			case SIG_SF_FEATB:
				res = my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				break;
			case SIG_EMWINK:
				/* if we received a '*', we are actually receiving Feature Group D
				   dial syntax, so use that mode; otherwise, fall through to normal
				   mode
				*/
				if (res == '*') {
					res = my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
					if (res > 0)
						res = my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
					if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
					break;
				}
			default:
				/* If we got the first digit, get the rest */
				len = 1;
				dtmfbuf[len] = '\0';
				while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
					if (ast_exists_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
						timeout = matchdigittimeout;
					} else {
						timeout = gendigittimeout;
					}
					res = ast_waitfordigit(chan, timeout);
					if (res < 0) {
						ast_debug(1, "waitfordigit returned < 0...\n");
						ast_hangup(chan);
						goto quit;
					} else if (res) {
						dtmfbuf[len++] = res;
						dtmfbuf[len] = '\0';
					} else {
						break;
					}
				}
				break;
			}
		}
		if (res == -1) {
			ast_log(LOG_WARNING, "getdtmf on channel %d: %s\n", p->channel, strerror(errno));
			ast_hangup(chan);
			goto quit;
		} else if (res < 0) {
			ast_debug(1, "Got hung up before digits finished\n");
			ast_hangup(chan);
			goto quit;
		}

		if (p->sig == SIG_FGC_CAMA) {
			char anibuf[100];

			if (ast_safe_sleep(chan,1000) == -1) {
				ast_hangup(chan);
				goto quit;
			}
			dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MF | p->dtmfrelax);
			res = my_getsigstr(chan, anibuf, "#", 10000);
			if ((res > 0) && (strlen(anibuf) > 2)) {
				if (anibuf[strlen(anibuf) - 1] == '#')
					anibuf[strlen(anibuf) - 1] = 0;
				ast_set_callerid(chan, anibuf + 2, NULL, anibuf + 2);
			}
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
		}

		ast_copy_string(exten, dtmfbuf, sizeof(exten));
		if (ast_strlen_zero(exten))
			ast_copy_string(exten, "s", sizeof(exten));
		if (p->sig == SIG_FEATD || p->sig == SIG_EMWINK) {
			/* Look for Feature Group D on all E&M Wink and Feature Group D trunks */
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "*");
				s2 = strsep(&stringp, "*");
				if (s2) {
					if (!ast_strlen_zero(p->cid_num))
						ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					else
						ast_set_callerid(chan, s1, NULL, s1);
					ast_copy_string(exten, s2, sizeof(exten));
				} else
					ast_copy_string(exten, s1, sizeof(exten));
			} else if (p->sig == SIG_FEATD)
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if ((p->sig == SIG_FEATDMF) || (p->sig == SIG_FEATDMF_TA)) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2) {
					if (!ast_strlen_zero(p->cid_num))
						ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					else
						if (*(s1 + 2))
							ast_set_callerid(chan, s1 + 2, NULL, s1 + 2);
					ast_copy_string(exten, s2 + 1, sizeof(exten));
				} else
					ast_copy_string(exten, s1 + 2, sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if ((p->sig == SIG_E911) || (p->sig == SIG_FGC_CAMAMF)) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2 && (*(s2 + 1) == '0')) {
					if (*(s2 + 2))
						ast_set_callerid(chan, s2 + 2, NULL, s2 + 2);
				}
				if (s1)	ast_copy_string(exten, s1, sizeof(exten));
				else ast_copy_string(exten, "911", sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-E911/FGC CAMA input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_FEATB) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				ast_copy_string(exten, exten2 + 1, sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group B input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if ((p->sig == SIG_FEATDMF) || (p->sig == SIG_FEATDMF_TA)) {
			dahdi_wink(p, idx);
			/* some switches require a minimum guard time between
			   the last FGD wink and something that answers
			   immediately. This ensures it */
			if (ast_safe_sleep(chan,100)) goto quit;
		}
		dahdi_enable_ec(p);
		if (NEED_MFDETECT(p)) {
			if (p->dsp) {
				if (!p->hardwaredtmf)
					ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
				else {
					ast_dsp_free(p->dsp);
					p->dsp = NULL;
				}
			}
		}

		if (ast_exists_extension(chan, chan->context, exten, 1, chan->cid.cid_num)) {
			ast_copy_string(chan->exten, exten, sizeof(chan->exten));
			if (p->dsp) ast_dsp_digitreset(p->dsp);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
			}
			goto quit;
		} else {
			ast_verb(2, "Unknown extension '%s' in context '%s' requested\n", exten, chan->context);
			sleep(2);
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_INFO);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = ast_streamfile(chan, "ss-noservice", chan->language);
			if (res >= 0)
				ast_waitstream(chan, "");
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
			ast_hangup(chan);
			goto quit;
		}
		break;
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Read the first digit */
		timeout = firstdigittimeout;
		/* If starting a threeway call, never timeout on the first digit so someone
		   can use flash-hook as a "hold" feature */
		if (p->subs[SUB_THREEWAY].owner)
			timeout = 999999;
		while (len < AST_MAX_EXTENSION-1) {
			/* Read digit unless it's supposed to be immediate, in which case the
			   only answer is 's' */
			if (p->immediate)
				res = 's';
			else
				res = ast_waitfordigit(chan, timeout);
			timeout = 0;
			if (res < 0) {
				ast_debug(1, "waitfordigit returned < 0...\n");
				res = tone_zone_play_tone(p->subs[idx].dfd, -1);
				ast_hangup(chan);
				goto quit;
			} else if (res) {
				ast_debug(1,"waitfordigit returned '%c' (%d), timeout = %d\n", res, res, timeout);
				exten[len++]=res;
				exten[len] = '\0';
			}
			if (!ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[idx].dfd, -1);
			else
				tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALTONE);
			if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num) && strcmp(exten, ast_parking_ext())) {
				if (!res || !ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
					if (getforward) {
						/* Record this as the forwarding extension */
						ast_copy_string(p->call_forward, exten, sizeof(p->call_forward));
						ast_verb(3, "Setting call forward to '%s' on channel %d\n", p->call_forward, p->channel);
						res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
						if (res)
							break;
						usleep(500000);
						res = tone_zone_play_tone(p->subs[idx].dfd, -1);
						sleep(1);
						memset(exten, 0, sizeof(exten));
						res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALTONE);
						len = 0;
						getforward = 0;
					} else {
						res = tone_zone_play_tone(p->subs[idx].dfd, -1);
						ast_copy_string(chan->exten, exten, sizeof(chan->exten));
						if (!ast_strlen_zero(p->cid_num)) {
							if (!p->hidecallerid)
								ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
							else
								ast_set_callerid(chan, NULL, NULL, p->cid_num);
						}
						if (!ast_strlen_zero(p->cid_name)) {
							if (!p->hidecallerid)
								ast_set_callerid(chan, NULL, p->cid_name, NULL);
						}
						ast_setstate(chan, AST_STATE_RING);
						dahdi_enable_ec(p);
						res = ast_pbx_run(chan);
						if (res) {
							ast_log(LOG_WARNING, "PBX exited non-zero\n");
							res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
						}
						goto quit;
					}
				} else {
					/* It's a match, but they just typed a digit, and there is an ambiguous match,
					   so just set the timeout to matchdigittimeout and wait some more */
					timeout = matchdigittimeout;
				}
			} else if (res == 0) {
				ast_debug(1, "not enough digits (and no ambiguous match)...\n");
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
				dahdi_wait_event(p->subs[idx].dfd);
				ast_hangup(chan);
				goto quit;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				ast_verb(3, "Disabling call waiting on %s\n", chan->name);
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						chan->name, strerror(errno));
				}
				len = 0;
				ioctl(p->subs[idx].dfd,DAHDI_CONFDIAG,&len);
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;

			} else if (!strcmp(exten,ast_pickup_ext())) {
				/* Scan all channels and see if there are any
				 * ringing channels that have call groups
				 * that equal this channels pickup group
				 */
				if (idx == SUB_REAL) {
					/* Switch us from Third call to Call Wait */
					if (p->subs[SUB_THREEWAY].owner) {
						/* If you make a threeway call and the *8# a call, it should actually
						   look like a callwait */
						alloc_sub(p, SUB_CALLWAIT);
						swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
						unalloc_sub(p, SUB_THREEWAY);
					}
					dahdi_enable_ec(p);
					if (ast_pickup_call(chan)) {
						ast_debug(1, "No call pickup possible...\n");
						res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
						dahdi_wait_event(p->subs[idx].dfd);
					}
					ast_hangup(chan);
					goto quit;
				} else {
					ast_log(LOG_WARNING, "Huh?  Got *8# on call not on real\n");
					ast_hangup(chan);
					goto quit;
				}

			} else if (!p->hidecallerid && !strcmp(exten, "*67")) {
				ast_verb(3, "Disabling Caller*ID on %s\n", chan->name);
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				if (chan->cid.cid_num)
					ast_free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				if (chan->cid.cid_name)
					ast_free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (p->callreturn && !strcmp(exten, "*69")) {
				res = 0;
				if (!ast_strlen_zero(p->lastcid_num)) {
					res = ast_say_digit_str(chan, p->lastcid_num, "", chan->language);
				}
				if (!res)
					res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				break;
			} else if (!strcmp(exten, "*78")) {
				dahdi_dnd(p, 1);
				/* Do not disturb */
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (!strcmp(exten, "*79")) {
				dahdi_dnd(p, 0);
				/* Do not disturb */
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*72")) {
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				getforward = 1;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*73")) {
				ast_verb(3, "Cancelling call forwarding on channel %d\n", p->channel);
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				memset(p->call_forward, 0, sizeof(p->call_forward));
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if ((p->transfer || p->canpark) && !strcmp(exten, ast_parking_ext()) &&
						p->subs[SUB_THREEWAY].owner &&
						ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
				/* This is a three way call, the main call being a real channel,
					and we're parking the first call. */
				ast_masq_park_call(ast_bridged_channel(p->subs[SUB_THREEWAY].owner), chan, 0, NULL);
				ast_verb(3, "Parking call to '%s'\n", chan->name);
				break;
			} else if (!ast_strlen_zero(p->lastcid_num) && !strcmp(exten, "*60")) {
				ast_verb(3, "Blacklisting number %s\n", p->lastcid_num);
				res = ast_db_put("blacklist", p->lastcid_num, "1");
				if (!res) {
					res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
					memset(exten, 0, sizeof(exten));
					len = 0;
				}
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				ast_verb(3, "Enabling Caller*ID on %s\n", chan->name);
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				if (chan->cid.cid_num)
					ast_free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				if (chan->cid.cid_name)
					ast_free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (!strcmp(exten, "*0")) {
				struct ast_channel *nbridge =
					p->subs[SUB_THREEWAY].owner;
				struct dahdi_pvt *pbridge = NULL;
				/* set up the private struct of the bridged one, if any */
				if (nbridge && ast_bridged_channel(nbridge))
					pbridge = ast_bridged_channel(nbridge)->tech_pvt;
				if (nbridge && pbridge &&
					(nbridge->tech == &dahdi_tech) &&
					(ast_bridged_channel(nbridge)->tech == &dahdi_tech) &&
					ISTRUNK(pbridge)) {
					int func = DAHDI_FLASH;
					/* Clear out the dial buffer */
					p->dop.dialstr[0] = '\0';
					/* flash hookswitch */
					if ((ioctl(pbridge->subs[SUB_REAL].dfd,DAHDI_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
						ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n",
							nbridge->name, strerror(errno));
					}
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
					if (ast_bridged_channel(p->subs[SUB_REAL].owner))
						ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
					ast_hangup(chan);
					goto quit;
				} else {
					tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
					dahdi_wait_event(p->subs[idx].dfd);
					tone_zone_play_tone(p->subs[idx].dfd, -1);
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
					ast_hangup(chan);
					goto quit;
				}
			} else if (!ast_canmatch_extension(chan, chan->context, exten, 1, chan->cid.cid_num) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				ast_debug(1, "Can't match %s from '%s' in context %s\n", exten, chan->cid.cid_num ? chan->cid.cid_num : "<Unknown Caller>", chan->context);
				break;
			}
			if (!timeout)
				timeout = gendigittimeout;
			if (len && !ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[idx].dfd, -1);
		}
		break;
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
		/* check for SMDI messages */
		if (p->use_smdi && p->smdi_iface) {
			smdi_msg = ast_smdi_md_message_wait(p->smdi_iface, SMDI_MD_WAIT_TIMEOUT);

			if (smdi_msg != NULL) {
				ast_copy_string(chan->exten, smdi_msg->fwd_st, sizeof(chan->exten));

				if (smdi_msg->type == 'B')
					pbx_builtin_setvar_helper(chan, "_SMDI_VM_TYPE", "b");
				else if (smdi_msg->type == 'N')
					pbx_builtin_setvar_helper(chan, "_SMDI_VM_TYPE", "u");

				ast_debug(1, "Recieved SMDI message on %s\n", chan->name);
			} else {
				ast_log(LOG_WARNING, "SMDI enabled but no SMDI message present\n");
			}
		}

		if (p->use_callerid && (p->cid_signalling == CID_SIG_SMDI && smdi_msg)) {
			number = smdi_msg->calling_st;

		/* If we want caller id, we're in a prering state due to a polarity reversal
		 * and we're set to use a polarity reversal to trigger the start of caller id,
		 * grab the caller id and wait for ringing to start... */
		} else if (p->use_callerid && (chan->_state == AST_STATE_PRERING && (p->cid_start == CID_START_POLARITY || p->cid_start == CID_START_POLARITY_IN))) {
			/* If set to use DTMF CID signalling, listen for DTMF */
			if (p->cid_signalling == CID_SIG_DTMF) {
				int k = 0;
				cs = NULL;
				ast_debug(1, "Receiving DTMF cid on "
					"channel %s\n", chan->name);
				dahdi_setlinear(p->subs[idx].dfd, 0);
				res = 2000;
				for (;;) {
					struct ast_frame *f;
					res = ast_waitfor(chan, res);
					if (res <= 0) {
						ast_log(LOG_WARNING, "DTMFCID timed out waiting for ring. "
							"Exiting simple switch\n");
						ast_hangup(chan);
						goto quit;
					}
					f = ast_read(chan);
					if (!f)
						break;
					if (f->frametype == AST_FRAME_DTMF) {
						dtmfbuf[k++] = f->subclass;
						ast_debug(1, "CID got digit '%c'\n", f->subclass);
						res = 2000;
					}
					ast_frfree(f);
					if (chan->_state == AST_STATE_RING ||
						chan->_state == AST_STATE_RINGING)
						break; /* Got ring */
				}
				dtmfbuf[k] = '\0';
				dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
				/* Got cid and ring. */
				ast_debug(1, "CID got string '%s'\n", dtmfbuf);
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_debug(1, "CID is '%s', flags %d\n",
					dtmfcid, flags);
				/* If first byte is NULL, we have no cid */
				if (!ast_strlen_zero(dtmfcid))
					number = dtmfcid;
				else
					number = NULL;
			/* If set to use V23 Signalling, launch our FSK gubbins and listen for it */
			} else if ((p->cid_signalling == CID_SIG_V23) || (p->cid_signalling == CID_SIG_V23_JP)) {
				cs = callerid_new(p->cid_signalling);
				if (cs) {
					samples = 0;
#if 1
					bump_gains(p);
#endif
					/* Take out of linear mode for Caller*ID processing */
					dahdi_setlinear(p->subs[idx].dfd, 0);

					/* First we wait and listen for the Caller*ID */
					for (;;) {
						i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
						if ((res = ioctl(p->subs[idx].dfd, DAHDI_IOMUX, &i))) {
							ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
							callerid_free(cs);
							ast_hangup(chan);
							goto quit;
						}
						if (i & DAHDI_IOMUX_SIGEVENT) {
							res = dahdi_get_event(p->subs[idx].dfd);
							ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));

							if (p->cid_signalling == CID_SIG_V23_JP) {
								if (res == DAHDI_EVENT_RINGBEGIN) {
									res = dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
									usleep(1);
								}
							} else {
								res = 0;
								break;
							}
						} else if (i & DAHDI_IOMUX_READ) {
							res = read(p->subs[idx].dfd, buf, sizeof(buf));
							if (res < 0) {
								if (errno != ELAST) {
									ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
									callerid_free(cs);
									ast_hangup(chan);
									goto quit;
								}
								break;
							}
							samples += res;

							if (p->cid_signalling == CID_SIG_V23_JP) {
								res = callerid_feed_jp(cs, buf, res, AST_LAW(p));
							} else {
								res = callerid_feed(cs, buf, res, AST_LAW(p));
							}

							if (res < 0) {
								ast_log(LOG_WARNING, "CallerID feed failed on channel '%s'\n", chan->name);
								break;
							} else if (res)
								break;
							else if (samples > (8000 * 10))
								break;
						}
					}
					if (res == 1) {
						callerid_get(cs, &name, &number, &flags);
						ast_log(LOG_NOTICE, "CallerID number: %s, name: %s, flags=%d\n", number, name, flags);
					}

					if (p->cid_signalling == CID_SIG_V23_JP) {
						res = dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
						usleep(1);
						res = 4000;
					} else {

						/* Finished with Caller*ID, now wait for a ring to make sure there really is a call coming */
						res = 2000;
					}

					for (;;) {
						struct ast_frame *f;
						res = ast_waitfor(chan, res);
						if (res <= 0) {
							ast_log(LOG_WARNING, "CID timed out waiting for ring. "
								"Exiting simple switch\n");
							ast_hangup(chan);
							goto quit;
						}
						if (!(f = ast_read(chan))) {
							ast_log(LOG_WARNING, "Hangup received waiting for ring. Exiting simple switch\n");
							ast_hangup(chan);
							goto quit;
						}
						ast_frfree(f);
						if (chan->_state == AST_STATE_RING ||
							chan->_state == AST_STATE_RINGING)
							break; /* Got ring */
					}

					/* We must have a ring by now, so, if configured, lets try to listen for
					 * distinctive ringing */
					if (p->usedistinctiveringdetection) {
						len = 0;
						distMatches = 0;
						/* Clear the current ring data array so we dont have old data in it. */
						for (receivedRingT = 0; receivedRingT < ARRAY_LEN(curRingData); receivedRingT++)
							curRingData[receivedRingT] = 0;
						receivedRingT = 0;
						counter = 0;
						counter1 = 0;
						/* Check to see if context is what it should be, if not set to be. */
						if (strcmp(p->context,p->defcontext) != 0) {
							ast_copy_string(p->context, p->defcontext, sizeof(p->context));
							ast_copy_string(chan->context,p->defcontext,sizeof(chan->context));
						}

						for (;;) {
							i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
							if ((res = ioctl(p->subs[idx].dfd, DAHDI_IOMUX, &i))) {
								ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
								callerid_free(cs);
								ast_hangup(chan);
								goto quit;
							}
							if (i & DAHDI_IOMUX_SIGEVENT) {
								res = dahdi_get_event(p->subs[idx].dfd);
								ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
								res = 0;
								/* Let us detect distinctive ring */

								curRingData[receivedRingT] = p->ringt;

								if (p->ringt < p->ringt_base/2)
									break;
								/* Increment the ringT counter so we can match it against
								   values in chan_dahdi.conf for distinctive ring */
								if (++receivedRingT == ARRAY_LEN(curRingData))
									break;
							} else if (i & DAHDI_IOMUX_READ) {
								res = read(p->subs[idx].dfd, buf, sizeof(buf));
								if (res < 0) {
									if (errno != ELAST) {
										ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
										callerid_free(cs);
										ast_hangup(chan);
										goto quit;
									}
									break;
								}
								if (p->ringt)
									p->ringt--;
								if (p->ringt == 1) {
									res = -1;
									break;
								}
							}
						}
							/* this only shows up if you have n of the dring patterns filled in */
						ast_verb(3, "Detected ring pattern: %d,%d,%d\n",curRingData[0],curRingData[1],curRingData[2]);
						for (counter = 0; counter < 3; counter++) {
							/* Check to see if the rings we received match any of the ones in chan_dahdi.conf for this
							channel */
							distMatches = 0;
							for (counter1 = 0; counter1 < 3; counter1++) {
								ast_verb(3, "Ring pattern check range: %d\n", p->drings.ringnum[counter].range);
								if (p->drings.ringnum[counter].ring[counter1] == -1) {
									ast_verb(3, "Pattern ignore (-1) detected, so matching pattern %d regardless.\n",
									curRingData[counter1]);
									distMatches++;
								} else if (curRingData[counter1] <= (p->drings.ringnum[counter].ring[counter1] + p->drings.ringnum[counter].range) &&
										curRingData[counter1] >= (p->drings.ringnum[counter].ring[counter1] - p->drings.ringnum[counter].range)) {
									ast_verb(3, "Ring pattern matched in range: %d to %d\n",
									(p->drings.ringnum[counter].ring[counter1] - p->drings.ringnum[counter].range),
									(p->drings.ringnum[counter].ring[counter1] + p->drings.ringnum[counter].range));
									distMatches++;
								}
							}

							if (distMatches == 3) {
								/* The ring matches, set the context to whatever is for distinctive ring.. */
								ast_copy_string(p->context, p->drings.ringContext[counter].contextData, sizeof(p->context));
								ast_copy_string(chan->context, p->drings.ringContext[counter].contextData, sizeof(chan->context));
								ast_verb(3, "Distinctive Ring matched context %s\n",p->context);
								break;
							}
						}
					}
					/* Restore linear mode (if appropriate) for Caller*ID processing */
					dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
#if 1
					restore_gains(p);
#endif
				} else
					ast_log(LOG_WARNING, "Unable to get caller ID space\n");
			} else {
				ast_log(LOG_WARNING, "Channel %s in prering "
					"state, but I have nothing to do. "
					"Terminating simple switch, should be "
					"restarted by the actual ring.\n",
					chan->name);
				ast_hangup(chan);
				goto quit;
			}
		} else if (p->use_callerid && p->cid_start == CID_START_RING) {
			if (p->cid_signalling == CID_SIG_DTMF) {
				int k = 0;
				cs = NULL;
				dahdi_setlinear(p->subs[idx].dfd, 0);
				res = 2000;
				for (;;) {
					struct ast_frame *f;
					res = ast_waitfor(chan, res);
					if (res <= 0) {
						ast_log(LOG_WARNING, "DTMFCID timed out waiting for ring. "
							"Exiting simple switch\n");
						ast_hangup(chan);
						return NULL;
					}
					f = ast_read(chan);
					if (f->frametype == AST_FRAME_DTMF) {
						dtmfbuf[k++] = f->subclass;
						ast_log(LOG_DEBUG, "CID got digit '%c'\n", f->subclass);
						res = 2000;
					}
					ast_frfree(f);

					if (p->ringt_base == p->ringt)
						break;
				}
				dtmfbuf[k] = '\0';
				dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
				/* Got cid and ring. */
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_log(LOG_DEBUG, "CID is '%s', flags %d\n",
					dtmfcid, flags);
				/* If first byte is NULL, we have no cid */
				if (!ast_strlen_zero(dtmfcid))
					number = dtmfcid;
				else
					number = NULL;
				/* If set to use V23 Signalling, launch our FSK gubbins and listen for it */
			} else {
				/* FSK Bell202 callerID */
				cs = callerid_new(p->cid_signalling);
				if (cs) {
#if 1
					bump_gains(p);
#endif
					samples = 0;
					len = 0;
					distMatches = 0;
					/* Clear the current ring data array so we dont have old data in it. */
					for (receivedRingT = 0; receivedRingT < ARRAY_LEN(curRingData); receivedRingT++)
						curRingData[receivedRingT] = 0;
					receivedRingT = 0;
					counter = 0;
					counter1 = 0;
					/* Check to see if context is what it should be, if not set to be. */
					if (strcmp(p->context,p->defcontext) != 0) {
						ast_copy_string(p->context, p->defcontext, sizeof(p->context));
						ast_copy_string(chan->context,p->defcontext,sizeof(chan->context));
					}

					/* Take out of linear mode for Caller*ID processing */
					dahdi_setlinear(p->subs[idx].dfd, 0);
					for (;;) {
						i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
						if ((res = ioctl(p->subs[idx].dfd, DAHDI_IOMUX, &i))) {
							ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
							callerid_free(cs);
							ast_hangup(chan);
							goto quit;
						}
						if (i & DAHDI_IOMUX_SIGEVENT) {
							res = dahdi_get_event(p->subs[idx].dfd);
							ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
							/* If we get a PR event, they hung up while processing calerid */
							if ( res == DAHDI_EVENT_POLARITY && p->hanguponpolarityswitch && p->polarity == POLARITY_REV) {
								ast_log(LOG_DEBUG, "Hanging up due to polarity reversal on channel %d while detecting callerid\n", p->channel);
								p->polarity = POLARITY_IDLE;
								callerid_free(cs);
								ast_hangup(chan);
								goto quit;
							}
							res = 0;
							/* Let us detect callerid when the telco uses distinctive ring */

							curRingData[receivedRingT] = p->ringt;

							if (p->ringt < p->ringt_base/2)
								break;
							/* Increment the ringT counter so we can match it against
							   values in chan_dahdi.conf for distinctive ring */
							if (++receivedRingT == ARRAY_LEN(curRingData))
								break;
						} else if (i & DAHDI_IOMUX_READ) {
							res = read(p->subs[idx].dfd, buf, sizeof(buf));
							if (res < 0) {
								if (errno != ELAST) {
									ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
									callerid_free(cs);
									ast_hangup(chan);
									goto quit;
								}
								break;
							}
							if (p->ringt)
								p->ringt--;
							if (p->ringt == 1) {
								res = -1;
								break;
							}
							samples += res;
							res = callerid_feed(cs, buf, res, AST_LAW(p));
							if (res < 0) {
								ast_log(LOG_WARNING, "CallerID feed failed: %s\n", strerror(errno));
								break;
							} else if (res)
								break;
							else if (samples > (8000 * 10))
								break;
						}
					}
					if (res == 1) {
						callerid_get(cs, &name, &number, &flags);
						ast_debug(1, "CallerID number: %s, name: %s, flags=%d\n", number, name, flags);
					}
					if (distinctiveringaftercid == 1) {
						/* Clear the current ring data array so we dont have old data in it. */
						for (receivedRingT = 0; receivedRingT < 3; receivedRingT++) {
							curRingData[receivedRingT] = 0;
						}
						receivedRingT = 0;
						ast_verb(3, "Detecting post-CID distinctive ring\n");
						for (;;) {
							i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
							if ((res = ioctl(p->subs[idx].dfd, DAHDI_IOMUX, &i))) {
								ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
								callerid_free(cs);
								ast_hangup(chan);
								goto quit;
							}
							if (i & DAHDI_IOMUX_SIGEVENT) {
								res = dahdi_get_event(p->subs[idx].dfd);
								ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
								res = 0;
								/* Let us detect callerid when the telco uses distinctive ring */

								curRingData[receivedRingT] = p->ringt;

								if (p->ringt < p->ringt_base/2)
									break;
								/* Increment the ringT counter so we can match it against
								   values in chan_dahdi.conf for distinctive ring */
								if (++receivedRingT == ARRAY_LEN(curRingData))
									break;
							} else if (i & DAHDI_IOMUX_READ) {
								res = read(p->subs[idx].dfd, buf, sizeof(buf));
								if (res < 0) {
									if (errno != ELAST) {
										ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
										callerid_free(cs);
										ast_hangup(chan);
										goto quit;
									}
									break;
								}
							if (p->ringt)
								p->ringt--;
								if (p->ringt == 1) {
									res = -1;
									break;
								}
							}
						}
					}
					if (p->usedistinctiveringdetection) {
						/* this only shows up if you have n of the dring patterns filled in */
						ast_verb(3, "Detected ring pattern: %d,%d,%d\n",curRingData[0],curRingData[1],curRingData[2]);

						for (counter = 0; counter < 3; counter++) {
							/* Check to see if the rings we received match any of the ones in chan_dahdi.conf for this
							channel */
							/* this only shows up if you have n of the dring patterns filled in */
							ast_verb(3, "Checking %d,%d,%d\n",
									p->drings.ringnum[counter].ring[0],
									p->drings.ringnum[counter].ring[1],
									p->drings.ringnum[counter].ring[2]);
							distMatches = 0;
							for (counter1 = 0; counter1 < 3; counter1++) {
								ast_verb(3, "Ring pattern check range: %d\n", p->drings.ringnum[counter].range);
								if (p->drings.ringnum[counter].ring[counter1] == -1) {
									ast_verb(3, "Pattern ignore (-1) detected, so matching pattern %d regardless.\n",
									curRingData[counter1]);
									distMatches++;
								}
								else if (curRingData[counter1] <= (p->drings.ringnum[counter].ring[counter1] + p->drings.ringnum[counter].range) &&
									curRingData[counter1] >= (p->drings.ringnum[counter].ring[counter1] - p->drings.ringnum[counter].range)) {
									ast_verb(3, "Ring pattern matched in range: %d to %d\n",
									(p->drings.ringnum[counter].ring[counter1] - p->drings.ringnum[counter].range),
									(p->drings.ringnum[counter].ring[counter1] + p->drings.ringnum[counter].range));
									distMatches++;
								}
							}
							if (distMatches == 3) {
								/* The ring matches, set the context to whatever is for distinctive ring.. */
								ast_copy_string(p->context, p->drings.ringContext[counter].contextData, sizeof(p->context));
								ast_copy_string(chan->context, p->drings.ringContext[counter].contextData, sizeof(chan->context));
								ast_verb(3, "Distinctive Ring matched context %s\n",p->context);
								break;
							}
						}
					}
					/* Restore linear mode (if appropriate) for Caller*ID processing */
					dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
#if 1
					restore_gains(p);
#endif
					if (res < 0) {
						ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", chan->name);
					}
				} else
					ast_log(LOG_WARNING, "Unable to get caller ID space\n");
			}
		} else
			cs = NULL;

		if (number)
			ast_shrink_phone_number(number);
		ast_set_callerid(chan, number, name, number);

		if (smdi_msg)
			ASTOBJ_UNREF(smdi_msg, ast_smdi_md_message_destroy);

		if (cs)
			callerid_free(cs);

		my_handle_notify_message(chan, p, flags, -1);

		ast_setstate(chan, AST_STATE_RING);
		chan->rings = 1;
		p->ringt = p->ringt_base;
		res = ast_pbx_run(chan);
		if (res) {
			ast_hangup(chan);
			ast_log(LOG_WARNING, "PBX exited non-zero\n");
		}
		goto quit;
	default:
		ast_log(LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", sig2str(p->sig), p->channel);
		res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
		if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
	if (res < 0)
			ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
quit:
	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count--;
	ast_cond_signal(&ss_thread_complete);
	ast_mutex_unlock(&ss_thread_lock);
	return NULL;
}

struct mwi_thread_data {
	struct dahdi_pvt *pvt;
	unsigned char buf[READ_SIZE];
	size_t len;
};

static int calc_energy(const unsigned char *buf, int len, int law)
{
	int x;
	int sum = 0;

	if (!len)
		return 0;

	for (x = 0; x < len; x++)
		sum += abs(law == AST_FORMAT_ULAW ? AST_MULAW(buf[x]) : AST_ALAW(buf[x]));

	return sum / len;
}

static void *mwi_thread(void *data)
{
	struct mwi_thread_data *mtd = data;
	struct callerid_state *cs;
	pthread_t threadid;
	int samples = 0;
	char *name, *number;
	int flags;
	int i, res;
	unsigned int spill_done = 0;
	int spill_result = -1;

	if (!(cs = callerid_new(mtd->pvt->cid_signalling))) {
		mtd->pvt->mwimonitoractive = 0;

		return NULL;
	}

	callerid_feed(cs, mtd->buf, mtd->len, AST_LAW(mtd->pvt));

	bump_gains(mtd->pvt);

	for (;;) {
		i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
		if ((res = ioctl(mtd->pvt->subs[SUB_REAL].dfd, DAHDI_IOMUX, &i))) {
			ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
			goto quit;
		}

		if (i & DAHDI_IOMUX_SIGEVENT) {
			struct ast_channel *chan;

			/* If we get an event, screen out events that we do not act on.
			 * Otherwise, cancel and go to the simple switch to let it deal with it.
			 */
			res = dahdi_get_event(mtd->pvt->subs[SUB_REAL].dfd);

			switch (res) {
			case DAHDI_EVENT_NEONMWI_ACTIVE:
			case DAHDI_EVENT_NEONMWI_INACTIVE:
			case DAHDI_EVENT_NONE:
			case DAHDI_EVENT_BITSCHANGED:
				break;
			case DAHDI_EVENT_NOALARM:
				mtd->pvt->inalarm = 0;
				ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", mtd->pvt->channel);
				manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
					"Channel: %d\r\n", mtd->pvt->channel);
				break;
			case DAHDI_EVENT_ALARM:
				mtd->pvt->inalarm = 1;
				res = get_alarms(mtd->pvt);
				handle_alarms(mtd->pvt, res);
				break; /* What to do on channel alarm ???? -- fall thru intentionally?? */
			default:
				ast_log(LOG_NOTICE, "Got event %d (%s)...  Passing along to analog_ss_thread\n", res, event2str(res));
				callerid_free(cs);

				restore_gains(mtd->pvt);
				mtd->pvt->ringt = mtd->pvt->ringt_base;

				if ((chan = dahdi_new(mtd->pvt, AST_STATE_RING, 0, SUB_REAL, 0, 0))) {
					int result;
					if (analog_lib_handles(mtd->pvt->sig, mtd->pvt->radio, mtd->pvt->oprmode)) {
						result = analog_ss_thread_start(mtd->pvt->sig_pvt, chan);
					} else {
						result = ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan);
					}
					if (result) {
						ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", mtd->pvt->channel);
						res = tone_zone_play_tone(mtd->pvt->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", mtd->pvt->channel);
						ast_hangup(chan);
						goto quit;
					}
					goto quit_no_clean;

				} else {
					ast_log(LOG_WARNING, "Could not create channel to handle call\n");
				}
			}
		} else if (i & DAHDI_IOMUX_READ) {
			if ((res = read(mtd->pvt->subs[SUB_REAL].dfd, mtd->buf, sizeof(mtd->buf))) < 0) {
				if (errno != ELAST) {
					ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
					goto quit;
				}
				break;
			}
			samples += res;
			if (!spill_done) {
				if ((spill_result = callerid_feed(cs, mtd->buf, res, AST_LAW(mtd->pvt))) < 0) {
					ast_log(LOG_WARNING, "CallerID feed failed: %s\n", strerror(errno));
					break;
				} else if (spill_result) {
					spill_done = 1;
				}
			} else {
				/* keep reading data until the energy level drops below the threshold
				   so we don't get another 'trigger' on the remaining carrier signal
				*/
				if (calc_energy(mtd->buf, res, AST_LAW(mtd->pvt)) <= mwilevel)
					break;
			}
			if (samples > (8000 * 4)) /*Termination case - time to give up*/
				break;
		}
	}

	if (spill_result == 1) {
		callerid_get(cs, &name, &number, &flags);
		if (flags & CID_MSGWAITING) {
			ast_log(LOG_NOTICE, "mwi: Have Messages on channel %d\n", mtd->pvt->channel);
			notify_message(mtd->pvt->mailbox, 1);
		} else if (flags & CID_NOMSGWAITING) {
			ast_log(LOG_NOTICE, "mwi: No Messages on channel %d\n", mtd->pvt->channel);
			notify_message(mtd->pvt->mailbox, 0);
		} else {
			ast_log(LOG_NOTICE, "mwi: Status unknown on channel %d\n", mtd->pvt->channel);
		}
	}


quit:
	callerid_free(cs);

	restore_gains(mtd->pvt);

quit_no_clean:
	mtd->pvt->mwimonitoractive = 0;

	ast_free(mtd);

	return NULL;
}

/*
* The following three functions (mwi_send_init, mwi_send_process_buffer,
* mwi_send_process_event) work with the do_monitor thread to generate mwi spills
* that are sent out via FXA port on voicemail state change.  The execution of
* the mwi send is state driven and can either generate a ring pulse prior to
* sending the fsk spill or simply send an fsk spill.
*/
static int mwi_send_init(struct dahdi_pvt * pvt)
{
	int x, res;

#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
	/* Determine how this spill is to be sent */
	if (pvt->mwisend_rpas) {
		pvt->mwisend_data.mwisend_current = MWI_SEND_SA;
		pvt->mwisendactive = 1;
	} else if (pvt->mwisend_fsk) {
		pvt->mwisend_data.mwisend_current = MWI_SEND_SPILL;
		pvt->mwisendactive = 1;
	} else {
		pvt->mwisendactive = 0;
		return 0;
	}
#else
	if (mwisend_rpas) {
		pvt->mwisend_data.mwisend_current = MWI_SEND_SA;
	} else {
		pvt->mwisend_data.mwisend_current = MWI_SEND_SPILL;
	}
	pvt->mwisendactive = 1;
#endif

	if (pvt->cidspill) {
		ast_log(LOG_WARNING, "cidspill already exists when trying to send FSK MWI\n");
		ast_free(pvt->cidspill);
		pvt->cidspill = NULL;
		pvt->cidpos = 0;
		pvt->cidlen = 0;
	}
	pvt->cidspill = ast_calloc(1, MAX_CALLERID_SIZE);
	if (!pvt->cidspill) {
		pvt->mwisendactive = 0;
		return -1;
	}
	x = DAHDI_FLUSH_BOTH;
	res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
	x = 3000;
	ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_ONHOOKTRANSFER, &x);
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
	if (pvt->mwisend_fsk) {
#endif
		pvt->cidlen = ast_callerid_vmwi_generate(pvt->cidspill, has_voicemail(pvt), CID_MWI_TYPE_MDMF_FULL,
							 AST_LAW(pvt), pvt->cid_name, pvt->cid_num, 0);
		pvt->cidpos = 0;
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
	}
#endif
	return 0;
}

static int mwi_send_process_buffer(struct dahdi_pvt * pvt, int num_read)
{
	struct timeval 	now;
	int 			res;

	/* sanity check to catch if this had been interrupted previously
	*	i.e. state says there is more to do but there is no spill allocated
	*/
	if (MWI_SEND_DONE != pvt->mwisend_data.mwisend_current && !pvt->cidspill) {
		pvt->mwisend_data.mwisend_current = MWI_SEND_DONE;
	} else if (MWI_SEND_DONE != pvt->mwisend_data.mwisend_current) {
		/* Normal processing -- Perform mwi send action */
		switch ( pvt->mwisend_data.mwisend_current) {
		case MWI_SEND_SA:
			/* Send the Ring Pulse Signal Alert */
			res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SETCADENCE, &AS_RP_cadence);
			if (res) {
				ast_log(LOG_WARNING, "Unable to set RP-AS ring cadence: %s\n", strerror(errno));
				goto quit;
			}
			res = dahdi_set_hook(pvt->subs[SUB_REAL].dfd, DAHDI_RING);
			pvt->mwisend_data.mwisend_current = MWI_SEND_SA_WAIT;
			break;
		case MWI_SEND_SA_WAIT:  /* do nothing until I get RINGEROFF event */
			break;
		case MWI_SEND_PAUSE:  /* Wait between alert and spill - min of 500 mS*/
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
			if (pvt->mwisend_fsk) {
#endif
				gettimeofday(&now, NULL);
				if ((int)(now.tv_sec - pvt->mwisend_data.pause.tv_sec) * 1000000 + (int)now.tv_usec - (int)pvt->mwisend_data.pause.tv_usec > 500000) {
					pvt->mwisend_data.mwisend_current = MWI_SEND_SPILL;
				}
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
			} else { /* support for mwisendtype=nofsk */
				pvt->mwisend_data.mwisend_current = MWI_SEND_CLEANUP;
			}
#endif
			break;
		case MWI_SEND_SPILL:
			/* We read some number of bytes.  Write an equal amount of data */
			if(0 < num_read) {
				if (num_read > pvt->cidlen - pvt->cidpos)
					num_read = pvt->cidlen - pvt->cidpos;
				res = write(pvt->subs[SUB_REAL].dfd, pvt->cidspill + pvt->cidpos, num_read);
				if (res > 0) {
					pvt->cidpos += res;
					if (pvt->cidpos >= pvt->cidlen) {
						pvt->mwisend_data.mwisend_current = MWI_SEND_CLEANUP;
					}
				} else {
					ast_log(LOG_WARNING, "MWI FSK Send Write failed: %s\n", strerror(errno));
					goto quit;
				}
			}
			break;
		case MWI_SEND_CLEANUP:
			/* For now, do nothing */
			pvt->mwisend_data.mwisend_current = MWI_SEND_DONE;
			break;
		default:
			/* Should not get here, punt*/
			goto quit;
		}
	}

	if (MWI_SEND_DONE == pvt->mwisend_data.mwisend_current) {
		if (pvt->cidspill) {
			ast_free(pvt->cidspill);
			pvt->cidspill = NULL;
			pvt->cidpos = 0;
			pvt->cidlen = 0;
		}
		pvt->mwisendactive = 0;
	}
	return 0;
quit:
	if (pvt->cidspill) {
		ast_free(pvt->cidspill);
		pvt->cidspill = NULL;
		pvt->cidpos = 0;
		pvt->cidlen = 0;
	}
	pvt->mwisendactive = 0;
	return -1;
}

static int mwi_send_process_event(struct dahdi_pvt * pvt, int event)
{
	int handled = 0;

	if (MWI_SEND_DONE != pvt->mwisend_data.mwisend_current) {
		switch (event) {
		case DAHDI_EVENT_RINGEROFF:
			if(pvt->mwisend_data.mwisend_current == MWI_SEND_SA_WAIT) {
				handled = 1;

				if (dahdi_set_hook(pvt->subs[SUB_REAL].dfd, DAHDI_RINGOFF) ) {
					ast_log(LOG_WARNING, "Unable to finsh RP-AS: %s mwi send aborted\n", strerror(errno));
					if(pvt->cidspill) {
						ast_free(pvt->cidspill);
						pvt->cidspill = NULL;
					}
					pvt->mwisend_data.mwisend_current = MWI_SEND_DONE;
					pvt->mwisendactive = 0;
				} else {
					pvt->mwisend_data.mwisend_current = MWI_SEND_PAUSE;
					gettimeofday(&pvt->mwisend_data.pause, NULL);
				}
			}
			break;
		/* Going off hook, I need to punt this spill */
		case DAHDI_EVENT_RINGOFFHOOK:
			if (pvt->cidspill) {
				ast_free(pvt->cidspill);
				pvt->cidspill = NULL;
				pvt->cidpos = 0;
				pvt->cidlen = 0;
			}
			pvt->mwisend_data.mwisend_current = MWI_SEND_DONE;
			pvt->mwisendactive = 0;
			break;
		case DAHDI_EVENT_RINGERON:
		case DAHDI_EVENT_HOOKCOMPLETE:
			break;
		default:
			break;
		}
	}
	return handled;
}

/* destroy a DAHDI channel, identified by its number */
static int dahdi_destroy_channel_bynum(int channel)
{
	struct dahdi_pvt *tmp = NULL;
	struct dahdi_pvt *prev = NULL;

	tmp = iflist;
	while (tmp) {
		if (tmp->channel == channel) {
			int x = DAHDI_FLASH;
			ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_HOOK, &x); /* important to create an event for dahdi_wait_event to register so that all analog_ss_threads terminate */
			destroy_channel(prev, tmp, 1);
			ast_module_unref(ast_module_info->self);
			return RESULT_SUCCESS;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	return RESULT_FAILURE;
}

/* returns < 0 = error, 0 event handled, >0 event handled and thread spawned */
static int handle_init_event(struct dahdi_pvt *i, int event)
{
	int res;
	int thread_spawned = 0;
	pthread_t threadid;
	struct ast_channel *chan;

	/* Handle an event on a given channel for the monitor thread. */

	switch (event) {
	case DAHDI_EVENT_NONE:
	case DAHDI_EVENT_BITSCHANGED:
		break;
	case DAHDI_EVENT_WINKFLASH:
	case DAHDI_EVENT_RINGOFFHOOK:
		if (i->inalarm) break;
		if (i->radio) break;
		/* Got a ring/answer.  What kind of channel are we? */
		switch (i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			res = dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			i->fxsoffhookstate = 1;
			if (res && (errno == EBUSY))
				break;
			if (i->cidspill) {
				/* Cancel VMWI spill */
				ast_free(i->cidspill);
				i->cidspill = NULL;
			}
			if (i->immediate) {
				dahdi_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
				chan = dahdi_new(i, AST_STATE_RING, 1, SUB_REAL, 0, 0);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = dahdi_new(i, AST_STATE_RESERVED, 0, SUB_REAL, 0, 0);
				if (chan) {
					if (has_voicemail(i))
						res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_STUTTER);
					else
						res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_DIALTONE);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play dialtone on channel %d, do you have defaultzone and loadzone defined?\n", i->channel);
					if (ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan)) {
						ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
						res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
						ast_hangup(chan);
					} else {
						thread_spawned = 1;
					}
				} else
					ast_log(LOG_WARNING, "Unable to create channel\n");
			}
			break;
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
				i->ringt = i->ringt_base;
				/* Fall through */
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_FEATDMF_TA:
		case SIG_E911:
		case SIG_FGC_CAMA:
		case SIG_FGC_CAMAMF:
		case SIG_FEATB:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
		case SIG_SF:
			/* Check for callerid, digits, etc */
			if (i->cid_start == CID_START_POLARITY_IN) {
				chan = dahdi_new(i, AST_STATE_PRERING, 0, SUB_REAL, 0, 0);
			} else {
				chan = dahdi_new(i, AST_STATE_RING, 0, SUB_REAL, 0, 0);
			}

			if (!chan) {
				ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
			} else if (ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan)) {
				ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
				res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
				ast_hangup(chan);
			} else {
				thread_spawned = 1;
			}
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/answer with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			return -1;
		}
		break;
	case DAHDI_EVENT_NOALARM:
		i->inalarm = 0;
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", i->channel);
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
			"Channel: %d\r\n", i->channel);
		break;
	case DAHDI_EVENT_ALARM:
		i->inalarm = 1;
		res = get_alarms(i);
		handle_alarms(i, res);
		/* fall thru intentionally */
	case DAHDI_EVENT_ONHOOK:
		if (i->radio)
			break;
		/* Back on hook.  Hang up. */
		switch (i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_FEATDMF_TA:
		case SIG_E911:
		case SIG_FGC_CAMA:
		case SIG_FGC_CAMAMF:
		case SIG_FEATB:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_EMWINK:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
		case SIG_FXOKS:
			dahdi_disable_ec(i);
			/* Diddle the battery for the zhone */
#ifdef ZHONE_HACK
			dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			usleep(1);
#endif
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
			break;
		case SIG_PRI:
		case SIG_SS7:
		case SIG_BRI:
		case SIG_BRI_PTMP:
			dahdi_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle on hook with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			return -1;
		}
		if (i->sig & __DAHDI_SIG_FXO) {
			i->fxsoffhookstate = 0;
		}
		break;
	case DAHDI_EVENT_POLARITY:
		switch (i->sig) {
		case SIG_FXSLS:
		case SIG_FXSKS:
		case SIG_FXSGS:
			/* We have already got a PR before the channel was
			   created, but it wasn't handled. We need polarity
			   to be REV for remote hangup detection to work.
			   At least in Spain */
			if (i->hanguponpolarityswitch)
				i->polarity = POLARITY_REV;
			if (i->cid_start == CID_START_POLARITY || i->cid_start == CID_START_POLARITY_IN) {
				i->polarity = POLARITY_REV;
				ast_verb(2, "Starting post polarity "
					"CID detection on channel %d\n",
					i->channel);
				chan = dahdi_new(i, AST_STATE_PRERING, 0, SUB_REAL, 0, 0);
				if (!chan) {
					ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				} else if (ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
				} else {
					thread_spawned = 1;
				}
			}
			break;
		default:
			ast_log(LOG_WARNING, "handle_init_event detected "
				"polarity reversal on non-FXO (SIG_FXS) "
				"interface %d\n", i->channel);
		}
		break;
	case DAHDI_EVENT_REMOVED: /* destroy channel */
		ast_log(LOG_NOTICE,
				"Got DAHDI_EVENT_REMOVED. Destroying channel %d\n",
				i->channel);
		dahdi_destroy_channel_bynum(i->channel);
		break;
	case DAHDI_EVENT_NEONMWI_ACTIVE:
		if (i->mwimonitor_neon) {
			notify_message(i->mailbox, 1);
			ast_log(LOG_NOTICE, "NEON MWI set for channel %d, mailbox %s \n", i->channel, i->mailbox);
		}
		break;
	case DAHDI_EVENT_NEONMWI_INACTIVE:
		if (i->mwimonitor_neon) {
			notify_message(i->mailbox, 0);
			ast_log(LOG_NOTICE, "NEON MWI cleared for channel %d, mailbox %s\n", i->channel, i->mailbox);
		}
		break;
	}
	return thread_spawned;
}

static void *do_monitor(void *data)
{
	int count, res, res2, spoint, pollres=0;
	struct dahdi_pvt *i;
	struct dahdi_pvt *last = NULL;
	time_t thispass = 0, lastpass = 0;
	int found;
	char buf[1024];
	struct pollfd *pfds=NULL;
	int lastalloc = -1;
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
#if 0
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL)) {
		ast_log(LOG_WARNING, "Unable to set cancel type to asynchronous\n");
		return NULL;
	}
	ast_debug(1, "Monitor starting...\n");
#endif
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	for (;;) {
		/* Lock the interface list */
		ast_mutex_lock(&iflock);
		if (!pfds || (lastalloc != ifcount)) {
			if (pfds) {
				ast_free(pfds);
				pfds = NULL;
			}
			if (ifcount) {
				if (!(pfds = ast_calloc(1, ifcount * sizeof(*pfds)))) {
					ast_mutex_unlock(&iflock);
					return NULL;
				}
			}
			lastalloc = ifcount;
		}
		/* Build the stuff we're going to poll on, that is the socket of every
		   dahdi_pvt that does not have an associated owner channel */
		count = 0;
		i = iflist;
		while (i) {
			ast_mutex_lock(&i->lock);
			if ((i->subs[SUB_REAL].dfd > -1) && i->sig && (!i->radio) && !(i->sig & SIG_MFCR2)) {
				if (analog_lib_handles(i->sig, i->radio, i->oprmode)) {
					struct analog_pvt *p = i->sig_pvt;

					if (!p)
						ast_log(LOG_ERROR, "No sig_pvt?\n");

					if (!p->owner && !p->subs[SUB_REAL].owner) {
						/* This needs to be watched, as it lacks an owner */
						pfds[count].fd = i->subs[SUB_REAL].dfd;
						pfds[count].events = POLLPRI;
						pfds[count].revents = 0;
						/* Message waiting or r2 channels also get watched for reading */
						if (i->cidspill || i->mwisendactive || i->mwimonitor_fsk)
							pfds[count].events |= POLLIN;
						count++;
					}
				} else {
					if (!i->owner && !i->subs[SUB_REAL].owner && !i->mwimonitoractive ) {
						/* This needs to be watched, as it lacks an owner */
						pfds[count].fd = i->subs[SUB_REAL].dfd;
						pfds[count].events = POLLPRI;
						pfds[count].revents = 0;
						/* If we are monitoring for VMWI or sending CID, we need to
						   read from the channel as well */
						if (i->cidspill || i->mwisendactive || i->mwimonitor_fsk)
							pfds[count].events |= POLLIN;
						count++;
					}
				}
			}
			ast_mutex_unlock(&i->lock);
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_mutex_unlock(&iflock);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		/* Wait at least a second for something to happen */
		res = poll(pfds, count, 1000);
		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* Okay, poll has finished.  Let's see what happened.  */
		if (res < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "poll return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		ast_mutex_lock(&iflock);
		found = 0;
		spoint = 0;
		lastpass = thispass;
		thispass = time(NULL);
		i = iflist;
		while (i) {
			if (thispass != lastpass) {
				if (!found && ((i == last) || ((i == iflist) && !last))) {
					last = i;
					if (last) {
						/* Only allow MWI to be initiated on a quiescent fxs port */
						if (!last->mwisendactive &&	last->sig & __DAHDI_SIG_FXO &&
								!last->fxsoffhookstate && !last->owner &&
								!ast_strlen_zero(last->mailbox) && (thispass - last->onhooktime > 3)) {
							res = has_voicemail(last);
							if (last->msgstate != res) {
								/* Set driver resources for signalling VMWI */
								res2 = ioctl(last->subs[SUB_REAL].dfd, DAHDI_VMWI, &res);
								if (res2) {
									/* TODO: This message will ALWAYS be generated on some cards; any way to restrict it to those cards where it is interesting? */
									ast_debug(3, "Unable to control message waiting led on channel %d: %s\n", last->channel, strerror(errno));
								}
								/* If enabled for FSK spill then initiate it */
								if (mwi_send_init(last)) {
									ast_log(LOG_WARNING, "Unable to initiate mwi send sequence on channel %d\n", last->channel);
								}
								last->msgstate = res;
								found ++;
							}
						}
						last = last->next;
					}
				}
			}
			if ((i->subs[SUB_REAL].dfd > -1) && i->sig) {
				if (i->radio && !i->owner)
				{
					res = dahdi_get_event(i->subs[SUB_REAL].dfd);
					if (res)
					{
						ast_debug(1, "Monitor doohicky got event %s on radio channel %d\n", event2str(res), i->channel);
						/* Don't hold iflock while handling init events */
						ast_mutex_unlock(&iflock);
						if (analog_lib_handles(i->sig, i->radio, i->oprmode))
							analog_handle_init_event(i->sig_pvt, dahdievent_to_analogevent(res));
						else
							handle_init_event(i, res);
						ast_mutex_lock(&iflock);
					}
					i = i->next;
					continue;
				}
				pollres = ast_fdisset(pfds, i->subs[SUB_REAL].dfd, count, &spoint);
				if (pollres & POLLIN) {
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef HAVE_PRI
						if (!i->pri)
#endif
							ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d) in read...\n", i->subs[SUB_REAL].dfd);
						i = i->next;
						continue;
					}
					if (!i->mwimonitor_fsk && !i->mwisendactive) {
						ast_log(LOG_WARNING, "Whoa....  I'm not looking for MWI or sending MWI but am reading (%d)...\n", i->subs[SUB_REAL].dfd);
						i = i->next;
						continue;
					}
					res = read(i->subs[SUB_REAL].dfd, buf, sizeof(buf));
					if (res > 0) {
						if (i->mwimonitor_fsk) {
							if (calc_energy((unsigned char *) buf, res, AST_LAW(i)) > mwilevel) {
								pthread_attr_t attr;
								pthread_t threadid;
								struct mwi_thread_data *mtd;

								pthread_attr_init(&attr);
								pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

								ast_log(LOG_DEBUG, "Maybe some MWI on port %d!\n", i->channel);
								if ((mtd = ast_calloc(1, sizeof(*mtd)))) {
									mtd->pvt = i;
									memcpy(mtd->buf, buf, res);
									mtd->len = res;
									if (ast_pthread_create_background(&threadid, &attr, mwi_thread, mtd)) {
										ast_log(LOG_WARNING, "Unable to start mwi thread on channel %d\n", i->channel);
										ast_free(mtd);
									}
									i->mwimonitoractive = 1;
								}
							}
						}
						if (i->mwisendactive) {
							mwi_send_process_buffer(i, res);
						}
					} else {
						ast_log(LOG_WARNING, "Read failed with %d: %s\n", res, strerror(errno));
					}
				}
				if (pollres & POLLPRI) {
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef HAVE_PRI
						if (!i->pri)
#endif
							ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d)...\n", i->subs[SUB_REAL].dfd);
						i = i->next;
						continue;
					}
					res = dahdi_get_event(i->subs[SUB_REAL].dfd);
					ast_debug(1, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
					/* Don't hold iflock while handling init events */
					ast_mutex_unlock(&iflock);
					if (0 == i->mwisendactive || 0 == mwi_send_process_event(i, res)) {
						if (analog_lib_handles(i->sig, i->radio, i->oprmode))
							analog_handle_init_event(i->sig_pvt, dahdievent_to_analogevent(res));
						else
							handle_init_event(i, res);
					}
					ast_mutex_lock(&iflock);
				}
			}
			i=i->next;
		}
		ast_mutex_unlock(&iflock);
	}
	/* Never reached */
	return NULL;

}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	ast_mutex_lock(&monlock);
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread != AST_PTHREADT_NULL) {
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (ast_pthread_create_background(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

#if defined(HAVE_PRI)
static int pri_resolve_span(int *span, int channel, int offset, struct dahdi_spaninfo *si)
{
	int x;
	int trunkgroup;
	/* Get appropriate trunk group if there is one */
	trunkgroup = pris[*span].mastertrunkgroup;
	if (trunkgroup) {
		/* Select a specific trunk group */
		for (x = 0; x < NUM_SPANS; x++) {
			if (pris[x].pri.trunkgroup == trunkgroup) {
				*span = x;
				return 0;
			}
		}
		ast_log(LOG_WARNING, "Channel %d on span %d configured to use nonexistent trunk group %d\n", channel, *span, trunkgroup);
		*span = -1;
	} else {
		if (pris[*span].pri.trunkgroup) {
			ast_log(LOG_WARNING, "Unable to use span %d implicitly since it is trunk group %d (please use spanmap)\n", *span, pris[*span].pri.trunkgroup);
			*span = -1;
		} else if (pris[*span].mastertrunkgroup) {
			ast_log(LOG_WARNING, "Unable to use span %d implicitly since it is already part of trunk group %d\n", *span, pris[*span].mastertrunkgroup);
			*span = -1;
		} else {
			if (si->totalchans == 31) {
				/* E1 */
				pris[*span].dchannels[0] = 16 + offset;
			} else if (si->totalchans == 24) {
				/* T1 or J1 */
				pris[*span].dchannels[0] = 24 + offset;
			} else if (si->totalchans == 3) {
				/* BRI */
				pris[*span].dchannels[0] = 3 + offset;
			} else {
				ast_log(LOG_WARNING, "Unable to use span %d, since the D-channel cannot be located (unexpected span size of %d channels)\n", *span, si->totalchans);
				*span = -1;
				return 0;
			}
			pris[*span].pri.span = *span + 1;
		}
	}
	return 0;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static int pri_create_trunkgroup(int trunkgroup, int *channels)
{
	struct dahdi_spaninfo si;
	struct dahdi_params p;
	int fd;
	int span;
	int ospan=0;
	int x,y;
	for (x = 0; x < NUM_SPANS; x++) {
		if (pris[x].pri.trunkgroup == trunkgroup) {
			ast_log(LOG_WARNING, "Trunk group %d already exists on span %d, Primary d-channel %d\n", trunkgroup, x + 1, pris[x].dchannels[0]);
			return -1;
		}
	}
	for (y = 0; y < NUM_DCHANS; y++) {
		if (!channels[y])
			break;
		memset(&si, 0, sizeof(si));
		memset(&p, 0, sizeof(p));
		fd = open("/dev/dahdi/channel", O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Failed to open channel: %s\n", strerror(errno));
			return -1;
		}
		x = channels[y];
		if (ioctl(fd, DAHDI_SPECIFY, &x)) {
			ast_log(LOG_WARNING, "Failed to specify channel %d: %s\n", channels[y], strerror(errno));
			close(fd);
			return -1;
		}
		if (ioctl(fd, DAHDI_GET_PARAMS, &p)) {
			ast_log(LOG_WARNING, "Failed to get channel parameters for channel %d: %s\n", channels[y], strerror(errno));
			return -1;
		}
		if (ioctl(fd, DAHDI_SPANSTAT, &si)) {
			ast_log(LOG_WARNING, "Failed go get span information on channel %d (span %d): %s\n", channels[y], p.spanno, strerror(errno));
			close(fd);
			return -1;
		}
		span = p.spanno - 1;
		if (pris[span].pri.trunkgroup) {
			ast_log(LOG_WARNING, "Span %d is already provisioned for trunk group %d\n", span + 1, pris[span].pri.trunkgroup);
			close(fd);
			return -1;
		}
		if (pris[span].pri.pvts[0]) {
			ast_log(LOG_WARNING, "Span %d is already provisioned with channels (implicit PRI maybe?)\n", span + 1);
			close(fd);
			return -1;
		}
		if (!y) {
			pris[span].pri.trunkgroup = trunkgroup;
			ospan = span;
		}
		pris[ospan].dchannels[y] = channels[y];
		pris[span].pri.span = span + 1;
		close(fd);
	}
	return 0;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static int pri_create_spanmap(int span, int trunkgroup, int logicalspan)
{
	if (pris[span].mastertrunkgroup) {
		ast_log(LOG_WARNING, "Span %d is already part of trunk group %d, cannot add to trunk group %d\n", span + 1, pris[span].mastertrunkgroup, trunkgroup);
		return -1;
	}
	pris[span].mastertrunkgroup = trunkgroup;
	pris[span].prilogicalspan = logicalspan;
	return 0;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
static unsigned int parse_pointcode(const char *pcstring)
{
	unsigned int code1, code2, code3;
	int numvals;

	numvals = sscanf(pcstring, "%d-%d-%d", &code1, &code2, &code3);
	if (numvals == 1)
		return code1;
	if (numvals == 3)
		return (code1 << 16) | (code2 << 8) | code3;

	return 0;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static struct dahdi_ss7 * ss7_resolve_linkset(int linkset)
{
	if ((linkset < 0) || (linkset >= NUM_SPANS))
		return NULL;
	else
		return &linksets[linkset - 1];
}
#endif	/* defined(HAVE_SS7) */

#ifdef HAVE_OPENR2
static void dahdi_r2_destroy_links(void)
{
	int i = 0;
	if (!r2links) {
		return;
	}
	for (; i < r2links_count; i++) {
		if (r2links[i]->r2master != AST_PTHREADT_NULL) {
			pthread_cancel(r2links[i]->r2master);
			pthread_join(r2links[i]->r2master, NULL);
			openr2_context_delete(r2links[i]->protocol_context);
		}
		ast_free(r2links[i]);
	}
	ast_free(r2links);
	r2links = NULL;
	r2links_count = 0;
}

#define R2_LINK_CAPACITY 10
static struct dahdi_mfcr2 *dahdi_r2_get_link(void)
{
	struct dahdi_mfcr2 *new_r2link = NULL;
	struct dahdi_mfcr2 **new_r2links = NULL;
	/* this function is called just when starting up and no monitor threads have been launched,
	   no need to lock monitored_count member */
	if (!r2links_count || (r2links[r2links_count - 1]->monitored_count == R2_LINK_CAPACITY)) {
		new_r2link = ast_calloc(1, sizeof(**r2links));
		if (!new_r2link) {
			ast_log(LOG_ERROR, "Cannot allocate R2 link!\n");
			return NULL;
		}
		new_r2links = ast_realloc(r2links, ((r2links_count + 1) * sizeof(*r2links)));
		if (!new_r2links) {
			ast_log(LOG_ERROR, "Cannot allocate R2 link!\n");
			ast_free(new_r2link);
			return NULL;
		}
		r2links = new_r2links;
		new_r2link->r2master = AST_PTHREADT_NULL;
		r2links[r2links_count] = new_r2link;
		r2links_count++;
		ast_log(LOG_DEBUG, "Created new R2 link!\n");
	}
	return r2links[r2links_count - 1];
}

static int dahdi_r2_set_context(struct dahdi_mfcr2 *r2_link, const struct dahdi_chan_conf *conf)
{
	char tmplogdir[] = "/tmp";
	char logdir[OR2_MAX_PATH];
	int threshold = 0;
	int snres = 0;
	r2_link->protocol_context = openr2_context_new(NULL, &dahdi_r2_event_iface,
			&dahdi_r2_transcode_iface, conf->mfcr2.variant, conf->mfcr2.max_ani,
			conf->mfcr2.max_dnis);
	if (!r2_link->protocol_context) {
		return -1;
	}
	openr2_context_set_log_level(r2_link->protocol_context, conf->mfcr2.loglevel);
	openr2_context_set_ani_first(r2_link->protocol_context, conf->mfcr2.get_ani_first);
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
	openr2_context_set_skip_category_request(r2_link->protocol_context, conf->mfcr2.skip_category_request);
#endif
	openr2_context_set_mf_threshold(r2_link->protocol_context, threshold);
	openr2_context_set_mf_back_timeout(r2_link->protocol_context, conf->mfcr2.mfback_timeout);
	openr2_context_set_metering_pulse_timeout(r2_link->protocol_context, conf->mfcr2.metering_pulse_timeout);
	openr2_context_set_double_answer(r2_link->protocol_context, conf->mfcr2.double_answer);
	openr2_context_set_immediate_accept(r2_link->protocol_context, conf->mfcr2.immediate_accept);
	if (ast_strlen_zero(conf->mfcr2.logdir)) {
		if (openr2_context_set_log_directory(r2_link->protocol_context, tmplogdir)) {
			ast_log(LOG_ERROR, "Failed setting default MFC/R2 log directory %s\n", tmplogdir);
		}
	} else {
		snres = snprintf(logdir, sizeof(logdir), "%s/%s/%s", ast_config_AST_LOG_DIR, "mfcr2", conf->mfcr2.logdir);
		if (snres >= sizeof(logdir)) {
			ast_log(LOG_ERROR, "MFC/R2 logging directory truncated, using %s\n", tmplogdir);
			if (openr2_context_set_log_directory(r2_link->protocol_context, tmplogdir)) {
				ast_log(LOG_ERROR, "Failed setting default MFC/R2 log directory %s\n", tmplogdir);
			}
		} else {
			if (openr2_context_set_log_directory(r2_link->protocol_context, logdir)) {
				ast_log(LOG_ERROR, "Failed setting MFC/R2 log directory %s\n", logdir);
			}
		}
	}
	if (!ast_strlen_zero(conf->mfcr2.r2proto_file)) {
		if (openr2_context_configure_from_advanced_file(r2_link->protocol_context, conf->mfcr2.r2proto_file)) {
			ast_log(LOG_ERROR, "Failed to configure r2context from advanced configuration file %s\n", conf->mfcr2.r2proto_file);
		}
	}
	ast_cond_init(&r2_link->do_monitor, NULL);
	ast_mutex_init(&r2_link->monitored_count_lock);
	r2_link->monitored_count = 0;
	return 0;
}
#endif

/* converts a DAHDI sigtype to signalling as can be configured from
 * chan_dahdi.conf.
 * While both have basically the same values, this will later be the
 * place to add filters and sanity checks
 */
static int sigtype_to_signalling(int sigtype)
{
	return sigtype;
}

static struct dahdi_pvt *mkintf(int channel, const struct dahdi_chan_conf *conf, int reloading)
{
	/* Make a dahdi_pvt structure for this interface */
	struct dahdi_pvt *tmp = NULL, *tmp2, *prev = NULL;
	char fn[80];
	struct dahdi_bufferinfo bi;

	int res;
	int span = 0;
	int here = 0;
	int x;
	struct dahdi_pvt **wlist;
	struct dahdi_pvt **wend;
	struct analog_pvt *analog_p = NULL;
	struct dahdi_params p;
#ifdef HAVE_PRI
	struct dahdi_spaninfo si;
	struct sig_pri_chan *pchan = NULL;
#endif

	wlist = &iflist;
	wend = &ifend;

	tmp2 = *wlist;
	prev = NULL;

	while (tmp2) {
		if (!tmp2->destroy) {
			if (tmp2->channel == channel) {
				tmp = tmp2;
				here = 1;
				break;
			}
			if (tmp2->channel > channel) {
				break;
			}
		}
		prev = tmp2;
		tmp2 = tmp2->next;
	}

	if (!here && reloading != 1) {
		if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
			return NULL;
		}
		ast_mutex_init(&tmp->lock);
		ifcount++;
		for (x = 0; x < 3; x++)
			tmp->subs[x].dfd = -1;
		tmp->channel = channel;
	}

	if (tmp) {
		int chan_sig = conf->chan.sig;
		if (!here) {
			if ((channel != CHAN_PSEUDO)) {
				int count = 0;
				snprintf(fn, sizeof(fn), "%d", channel);
				/* Open non-blocking */
				tmp->subs[SUB_REAL].dfd = dahdi_open(fn);
				while (tmp->subs[SUB_REAL].dfd < 0 && reloading == 2 && count < 1000) { /* the kernel may not call dahdi_release fast enough for the open flagbit to be cleared in time */
					usleep(1);
					tmp->subs[SUB_REAL].dfd = dahdi_open(fn);
					count++;
				}
				/* Allocate a DAHDI structure */
				if (tmp->subs[SUB_REAL].dfd < 0) {
					ast_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
				if (res < 0) {
					ast_log(LOG_ERROR, "Unable to get parameters: %s\n", strerror(errno));
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (conf->is_sig_auto)
					chan_sig = sigtype_to_signalling(p.sigtype);
				if (p.sigtype != (chan_sig & 0x3ffff)) {
					ast_log(LOG_ERROR, "Signalling requested on channel %d is %s but line is in %s signalling\n", channel, sig2str(chan_sig), sig2str(p.sigtype));
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				tmp->law = p.curlaw;
				tmp->span = p.spanno;
				span = p.spanno - 1;
			} else {
				chan_sig = 0;
			}

			if (analog_lib_handles(chan_sig, tmp->radio, tmp->oprmode)) {
				if (tmp->sig_pvt) {
					ast_log(LOG_WARNING, "Private already exists!\n");
					analog_p = tmp->sig_pvt;
				} else
					analog_p = analog_new(dahdisig_to_analogsig(chan_sig), &dahdi_analog_callbacks, tmp);
				if (!analog_p) {
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				tmp->sig_pvt = analog_p;

			}
#ifdef HAVE_SS7
			if (chan_sig == SIG_SS7) {
				struct dahdi_ss7 *ss7;
				int clear = 0;
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &clear)) {
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}

				ss7 = ss7_resolve_linkset(cur_linkset);
				if (!ss7) {
					ast_log(LOG_ERROR, "Unable to find linkset %d\n", cur_linkset);
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (cur_cicbeginswith < 0) {
					ast_log(LOG_ERROR, "Need to set cicbeginswith for the channels!\n");
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}

				tmp->cic = cur_cicbeginswith++;

				/* DB: Add CIC's DPC information */
				tmp->dpc = cur_defaultdpc;

				tmp->ss7 = ss7;
				tmp->ss7call = NULL;
				ss7->pvts[ss7->numchans++] = tmp;

				ast_copy_string(ss7->internationalprefix, conf->ss7.internationalprefix, sizeof(ss7->internationalprefix));
				ast_copy_string(ss7->nationalprefix, conf->ss7.nationalprefix, sizeof(ss7->nationalprefix));
				ast_copy_string(ss7->subscriberprefix, conf->ss7.subscriberprefix, sizeof(ss7->subscriberprefix));
				ast_copy_string(ss7->unknownprefix, conf->ss7.unknownprefix, sizeof(ss7->unknownprefix));

				ss7->called_nai = conf->ss7.called_nai;
				ss7->calling_nai = conf->ss7.calling_nai;
			}
#endif
#ifdef HAVE_OPENR2
			if (chan_sig == SIG_MFCR2 && reloading != 1) {
				struct dahdi_mfcr2 *r2_link;
				r2_link = dahdi_r2_get_link();
				if (!r2_link) {
					ast_log(LOG_WARNING, "Cannot get another R2 DAHDI context!\n");
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (!r2_link->protocol_context && dahdi_r2_set_context(r2_link, conf)) {
					ast_log(LOG_ERROR, "Cannot create OpenR2 protocol context.\n");
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (r2_link->numchans == (sizeof(r2_link->pvts)/sizeof(r2_link->pvts[0]))) {
					ast_log(LOG_ERROR, "Cannot add more channels to this link!\n");
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				r2_link->pvts[r2_link->numchans++] = tmp;
				tmp->r2chan = openr2_chan_new_from_fd(r2_link->protocol_context,
						                      tmp->subs[SUB_REAL].dfd,
						                      NULL, NULL);
				if (!tmp->r2chan) {
					openr2_liberr_t err = openr2_context_get_last_error(r2_link->protocol_context);
					ast_log(LOG_ERROR, "Cannot create OpenR2 channel: %s\n", openr2_context_error_string(err));
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				tmp->mfcr2 = r2_link;
				if (conf->mfcr2.call_files) {
					openr2_chan_enable_call_files(tmp->r2chan);
				}
				openr2_chan_set_client_data(tmp->r2chan, tmp);
				/* cast seems to be needed to get rid of the annoying warning regarding format attribute  */
				openr2_chan_set_logging_func(tmp->r2chan, (openr2_logging_func_t)dahdi_r2_on_chan_log);
				openr2_chan_set_log_level(tmp->r2chan, conf->mfcr2.loglevel);
				tmp->mfcr2_category = conf->mfcr2.category;
				tmp->mfcr2_charge_calls = conf->mfcr2.charge_calls;
				tmp->mfcr2_allow_collect_calls = conf->mfcr2.allow_collect_calls;
				tmp->mfcr2_forced_release = conf->mfcr2.forced_release;
				tmp->mfcr2_accept_on_offer = conf->mfcr2.accept_on_offer;
				tmp->mfcr2call = 0;
				tmp->mfcr2_dnis_index = 0;
				tmp->mfcr2_ani_index = 0;
				r2_link->monitored_count++;
			}
#endif
#ifdef HAVE_PRI
			if ((chan_sig == SIG_PRI) || (chan_sig == SIG_BRI) || (chan_sig == SIG_BRI_PTMP)) {
				int offset;
				int matchesdchan;
				int x,y;
				int myswitchtype = 0;
				offset = 0;
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &offset)) {
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (span >= NUM_SPANS) {
					ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
					destroy_dahdi_pvt(&tmp);
					return NULL;
				} else {
					si.spanno = 0;
					if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1) {
						ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
						destroy_dahdi_pvt(&tmp);
						return NULL;
					}
					/* Store the logical span first based upon the real span */
					tmp->logicalspan = pris[span].prilogicalspan;
					pri_resolve_span(&span, channel, (channel - p.chanpos), &si);
					if (span < 0) {
						ast_log(LOG_WARNING, "Channel %d: Unable to find locate channel/trunk group!\n", channel);
						destroy_dahdi_pvt(&tmp);
						return NULL;
					}
					myswitchtype = conf->pri.pri.switchtype;
					/* Make sure this isn't a d-channel */
					matchesdchan=0;
					for (x = 0; x < NUM_SPANS; x++) {
						for (y = 0; y < NUM_DCHANS; y++) {
							if (pris[x].dchannels[y] == tmp->channel) {
								matchesdchan = 1;
								break;
							}
						}
					}
					if (!matchesdchan) {
						if (pris[span].pri.nodetype && (pris[span].pri.nodetype != conf->pri.pri.nodetype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s node\n", span + 1, pri_node2str(pris[span].pri.nodetype));
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].pri.switchtype && (pris[span].pri.switchtype != myswitchtype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, pri_switch2str(pris[span].pri.switchtype));
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if ((pris[span].pri.dialplan) && (pris[span].pri.dialplan != conf->pri.pri.dialplan)) {
							ast_log(LOG_ERROR, "Span %d is already a %s dialing plan\n", span + 1, pris[span].pri.dialplan == -1 ? "Dynamically set dialplan in ISDN" : pri_plan2str(pris[span].pri.dialplan));
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (!ast_strlen_zero(pris[span].pri.idledial) && strcmp(pris[span].pri.idledial, conf->pri.pri.idledial)) {
							ast_log(LOG_ERROR, "Span %d already has idledial '%s'.\n", span + 1, conf->pri.pri.idledial);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (!ast_strlen_zero(pris[span].pri.idleext) && strcmp(pris[span].pri.idleext, conf->pri.pri.idleext)) {
							ast_log(LOG_ERROR, "Span %d already has idleext '%s'.\n", span + 1, conf->pri.pri.idleext);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].pri.minunused && (pris[span].pri.minunused != conf->pri.pri.minunused)) {
							ast_log(LOG_ERROR, "Span %d already has minunused of %d.\n", span + 1, conf->pri.pri.minunused);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].pri.minidle && (pris[span].pri.minidle != conf->pri.pri.minidle)) {
							ast_log(LOG_ERROR, "Span %d already has minidle of %d.\n", span + 1, conf->pri.pri.minidle);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].pri.numchans >= MAX_CHANNELS) {
							ast_log(LOG_ERROR, "Unable to add channel %d: Too many channels in trunk group %d!\n", channel,
								pris[span].pri.trunkgroup);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}

						ast_debug(4, "Adding callbacks %p to chan %d\n", &dahdi_pri_callbacks, tmp->channel);
						pchan = sig_pri_chan_new(tmp, &dahdi_pri_callbacks, &pris[span].pri, tmp->logicalspan, p.chanpos);
						if (!pchan) {
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						tmp->sig_pvt = pchan;
						tmp->pri = &pris[span].pri;

						pris[span].pri.sig = chan_sig;
						pris[span].pri.nodetype = conf->pri.pri.nodetype;
						pris[span].pri.switchtype = myswitchtype;
						pris[span].pri.nsf = conf->pri.pri.nsf;
						pris[span].pri.dialplan = conf->pri.pri.dialplan;
						pris[span].pri.localdialplan = conf->pri.pri.localdialplan;
						pris[span].pri.pvts[pris[span].pri.numchans++] = tmp->sig_pvt;
						pris[span].pri.minunused = conf->pri.pri.minunused;
						pris[span].pri.minidle = conf->pri.pri.minidle;
						pris[span].pri.overlapdial = conf->pri.pri.overlapdial;
						pris[span].pri.qsigchannelmapping = conf->pri.pri.qsigchannelmapping;
						pris[span].pri.discardremoteholdretrieval = conf->pri.pri.discardremoteholdretrieval;
#ifdef HAVE_PRI_SERVICE_MESSAGES
						pris[span].pri.enable_service_message_support = conf->pri.pri.enable_service_message_support;
#endif
#ifdef HAVE_PRI_INBANDDISCONNECT
						pris[span].pri.inbanddisconnect = conf->pri.pri.inbanddisconnect;
#endif
						pris[span].pri.facilityenable = conf->pri.pri.facilityenable;
						ast_copy_string(pris[span].pri.idledial, conf->pri.pri.idledial, sizeof(pris[span].pri.idledial));
						ast_copy_string(pris[span].pri.idleext, conf->pri.pri.idleext, sizeof(pris[span].pri.idleext));
						ast_copy_string(pris[span].pri.internationalprefix, conf->pri.pri.internationalprefix, sizeof(pris[span].pri.internationalprefix));
						ast_copy_string(pris[span].pri.nationalprefix, conf->pri.pri.nationalprefix, sizeof(pris[span].pri.nationalprefix));
						ast_copy_string(pris[span].pri.localprefix, conf->pri.pri.localprefix, sizeof(pris[span].pri.localprefix));
						ast_copy_string(pris[span].pri.privateprefix, conf->pri.pri.privateprefix, sizeof(pris[span].pri.privateprefix));
						ast_copy_string(pris[span].pri.unknownprefix, conf->pri.pri.unknownprefix, sizeof(pris[span].pri.unknownprefix));
						pris[span].pri.resetinterval = conf->pri.pri.resetinterval;

						if (si.spanno != span + 1) { /* in another trunkgroup */
							tmp->prioffset = pris[span].pri.numchans;
						} else {
							tmp->prioffset = p.chanpos;
						}
					} else {
						ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", p.chanpos);
						destroy_dahdi_pvt(&tmp);
						return NULL;
					}
				}
			} else {
				tmp->prioffset = 0;
			}
#endif
		} else {
			/* already exists in interface list */
			ast_log(LOG_WARNING, "Attempt to configure channel %d with signaling %s ignored because it is already configured to be %s.\n", tmp->channel, dahdi_sig2str(chan_sig), dahdi_sig2str(tmp->sig));
			chan_sig = tmp->sig;
			if (tmp->subs[SUB_REAL].dfd > -1) {
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
			}
		}
		/* Adjust starttime on loopstart and kewlstart trunks to reasonable values */
		switch (chan_sig) {
		case SIG_FXSKS:
		case SIG_FXSLS:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_FEATDMF_TA:
		case SIG_FEATB:
		case SIG_E911:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_FGC_CAMA:
		case SIG_FGC_CAMAMF:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
			p.starttime = 250;
			break;
		}

		if (tmp->radio) {
			/* XXX Waiting to hear back from Jim if these should be adjustable XXX */
			p.channo = channel;
			p.rxwinktime = 1;
			p.rxflashtime = 1;
			p.starttime = 1;
			p.debouncetime = 5;
		}
		if (!tmp->radio) {
			p.channo = channel;
			/* Override timing settings based on config file */
			if (conf->timing.prewinktime >= 0)
				p.prewinktime = conf->timing.prewinktime;
			if (conf->timing.preflashtime >= 0)
				p.preflashtime = conf->timing.preflashtime;
			if (conf->timing.winktime >= 0)
				p.winktime = conf->timing.winktime;
			if (conf->timing.flashtime >= 0)
				p.flashtime = conf->timing.flashtime;
			if (conf->timing.starttime >= 0)
				p.starttime = conf->timing.starttime;
			if (conf->timing.rxwinktime >= 0)
				p.rxwinktime = conf->timing.rxwinktime;
			if (conf->timing.rxflashtime >= 0)
				p.rxflashtime = conf->timing.rxflashtime;
			if (conf->timing.debouncetime >= 0)
				p.debouncetime = conf->timing.debouncetime;
		}

		/* dont set parms on a pseudo-channel */
		if (tmp->subs[SUB_REAL].dfd >= 0)
		{
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters: %s\n", strerror(errno));
				destroy_dahdi_pvt(&tmp);
				return NULL;
			}
		}
#if 1
		if (!here && (tmp->subs[SUB_REAL].dfd > -1)) {
			memset(&bi, 0, sizeof(bi));
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = conf->chan.buf_policy;
				bi.rxbufpolicy = conf->chan.buf_policy;
				bi.numbufs = conf->chan.buf_no;
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d: %s\n", channel, strerror(errno));
				}
			} else {
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d: %s\n", channel, strerror(errno));
			}
			tmp->buf_policy = conf->chan.buf_policy;
			tmp->buf_no = conf->chan.buf_no;
			tmp->usefaxbuffers = conf->chan.usefaxbuffers;
			tmp->faxbuf_policy = conf->chan.faxbuf_policy;
			tmp->faxbuf_no = conf->chan.faxbuf_no;
			/* This is not as gnarly as it may first appear.  If the ioctl above failed, we'd be setting
			 * tmp->bufsize to zero which would cause subsequent faxbuffer-related ioctl calls to fail.
			 * The reason the ioctl call above failed should to be determined before worrying about the
			 * faxbuffer-related ioctl calls */
			tmp->bufsize = bi.bufsize;
		}
#endif
		tmp->immediate = conf->chan.immediate;
		tmp->transfertobusy = conf->chan.transfertobusy;
		if (chan_sig & __DAHDI_SIG_FXS) {
			tmp->mwimonitor_fsk = conf->chan.mwimonitor_fsk;
			tmp->mwimonitor_neon = conf->chan.mwimonitor_neon;
			tmp->mwimonitor_rpas = conf->chan.mwimonitor_rpas;
		}
		tmp->sig = chan_sig;
		tmp->outsigmod = conf->chan.outsigmod;
		tmp->ringt_base = ringt_base;
		tmp->firstradio = 0;
		if ((chan_sig == SIG_FXOKS) || (chan_sig == SIG_FXOLS) || (chan_sig == SIG_FXOGS))
			tmp->permcallwaiting = conf->chan.callwaiting;
		else
			tmp->permcallwaiting = 0;
		/* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
		tmp->destroy = 0;
		tmp->drings = conf->chan.drings;

		/* 10 is a nice default. */
		if (tmp->drings.ringnum[0].range == 0)
			tmp->drings.ringnum[0].range = 10;
		if (tmp->drings.ringnum[1].range == 0)
			tmp->drings.ringnum[1].range = 10;
		if (tmp->drings.ringnum[2].range == 0)
			tmp->drings.ringnum[2].range = 10;

		tmp->usedistinctiveringdetection = usedistinctiveringdetection;
		tmp->callwaitingcallerid = conf->chan.callwaitingcallerid;
		tmp->threewaycalling = conf->chan.threewaycalling;
		tmp->adsi = conf->chan.adsi;
		tmp->use_smdi = conf->chan.use_smdi;
		tmp->permhidecallerid = conf->chan.hidecallerid;
		tmp->callreturn = conf->chan.callreturn;
		tmp->echocancel = conf->chan.echocancel;
		tmp->echotraining = conf->chan.echotraining;
		tmp->pulse = conf->chan.pulse;
		if (tmp->echocancel.head.tap_length) {
			tmp->echocanbridged = conf->chan.echocanbridged;
		} else {
			if (conf->chan.echocanbridged)
				ast_log(LOG_NOTICE, "echocancelwhenbridged requires echocancel to be enabled; ignoring\n");
			tmp->echocanbridged = 0;
		}
		tmp->busydetect = conf->chan.busydetect;
		tmp->busycount = conf->chan.busycount;
		tmp->busy_tonelength = conf->chan.busy_tonelength;
		tmp->busy_quietlength = conf->chan.busy_quietlength;
		tmp->callprogress = conf->chan.callprogress;
		tmp->waitfordialtone = conf->chan.waitfordialtone;
		tmp->cancallforward = conf->chan.cancallforward;
		tmp->dtmfrelax = conf->chan.dtmfrelax;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->hidecallerid = tmp->permhidecallerid;
		tmp->channel = channel;
		tmp->stripmsd = conf->chan.stripmsd;
		tmp->use_callerid = conf->chan.use_callerid;
		tmp->cid_signalling = conf->chan.cid_signalling;
		tmp->cid_start = conf->chan.cid_start;
		tmp->dahditrcallerid = conf->chan.dahditrcallerid;
		tmp->restrictcid = conf->chan.restrictcid;
		tmp->use_callingpres = conf->chan.use_callingpres;
		tmp->priindication_oob = conf->chan.priindication_oob;
		tmp->priexclusive = conf->chan.priexclusive;
		if (tmp->usedistinctiveringdetection) {
			if (!tmp->use_callerid) {
				ast_log(LOG_NOTICE, "Distinctive Ring detect requires 'usecallerid' be on\n");
				tmp->use_callerid = 1;
			}
		}

		if (tmp->cid_signalling == CID_SIG_SMDI) {
			if (!tmp->use_smdi) {
				ast_log(LOG_WARNING, "SMDI callerid requires SMDI to be enabled, enabling...\n");
				tmp->use_smdi = 1;
			}
		}
		if (tmp->use_smdi) {
			tmp->smdi_iface = ast_smdi_interface_find(conf->smdi_port);
			if (!(tmp->smdi_iface)) {
				ast_log(LOG_ERROR, "Invalid SMDI port specfied, disabling SMDI support\n");
				tmp->use_smdi = 0;
			}
		}

		ast_copy_string(tmp->accountcode, conf->chan.accountcode, sizeof(tmp->accountcode));
		tmp->amaflags = conf->chan.amaflags;
		if (!here) {
			tmp->confno = -1;
			tmp->propconfno = -1;
		}
		tmp->canpark = conf->chan.canpark;
		tmp->transfer = conf->chan.transfer;
		ast_copy_string(tmp->defcontext,conf->chan.context,sizeof(tmp->defcontext));
		ast_copy_string(tmp->language, conf->chan.language, sizeof(tmp->language));
		ast_copy_string(tmp->mohinterpret, conf->chan.mohinterpret, sizeof(tmp->mohinterpret));
		ast_copy_string(tmp->mohsuggest, conf->chan.mohsuggest, sizeof(tmp->mohsuggest));
		ast_copy_string(tmp->context, conf->chan.context, sizeof(tmp->context));
		ast_copy_string(tmp->cid_num, conf->chan.cid_num, sizeof(tmp->cid_num));
		ast_copy_string(tmp->parkinglot, conf->chan.parkinglot, sizeof(tmp->parkinglot));
		tmp->cid_ton = 0;
		ast_copy_string(tmp->cid_name, conf->chan.cid_name, sizeof(tmp->cid_name));
		ast_copy_string(tmp->mailbox, conf->chan.mailbox, sizeof(tmp->mailbox));
		if (channel != CHAN_PSEUDO && !ast_strlen_zero(tmp->mailbox)) {
			char *mailbox, *context;
			mailbox = context = ast_strdupa(tmp->mailbox);
			strsep(&context, "@");
			if (ast_strlen_zero(context))
				context = "default";
			tmp->mwi_event_sub = ast_event_subscribe(AST_EVENT_MWI, mwi_event_cb, NULL,
				AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
				AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
				AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_EXISTS,
				AST_EVENT_IE_END);
		}
		tmp->msgstate = -1;
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
		tmp->mwisend_setting = conf->chan.mwisend_setting;
		tmp->mwisend_fsk  = conf->chan.mwisend_fsk;
		tmp->mwisend_rpas = conf->chan.mwisend_rpas;
#endif
		if (chan_sig & __DAHDI_SIG_FXO) {
			memset(&p, 0, sizeof(p));
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
			if (!res) {
				tmp->fxsoffhookstate = p.rxisoffhook;
			}
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_VMWI_CONFIG, &tmp->mwisend_setting);
#endif
		}
		tmp->onhooktime = time(NULL);
		tmp->group = conf->chan.group;
		tmp->callgroup = conf->chan.callgroup;
		tmp->pickupgroup= conf->chan.pickupgroup;
		if (conf->chan.vars) {
			tmp->vars = conf->chan.vars;
		}
		tmp->cid_rxgain = conf->chan.cid_rxgain;
		tmp->rxgain = conf->chan.rxgain;
		tmp->txgain = conf->chan.txgain;
		tmp->tonezone = conf->chan.tonezone;
		if (tmp->subs[SUB_REAL].dfd > -1) {
			set_actual_gain(tmp->subs[SUB_REAL].dfd, 0, tmp->rxgain, tmp->txgain, tmp->law);
			if (tmp->dsp)
				ast_dsp_set_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
			update_conf(tmp);
			if (!here) {
				if ((chan_sig != SIG_BRI) && (chan_sig != SIG_BRI_PTMP) && (chan_sig != SIG_PRI)
				    && (chan_sig != SIG_SS7) && (chan_sig != SIG_MFCR2))
					/* Hang it up to be sure it's good */
					dahdi_set_hook(tmp->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
			}
			ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SETTONEZONE,&tmp->tonezone);
#ifdef HAVE_PRI
			memset(&si, 0, sizeof(si));
			if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1) {
				ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
				destroy_dahdi_pvt(&tmp);
				return NULL;
			}
#endif
			if ((res = get_alarms(tmp)) != DAHDI_ALARM_NONE) {
			/* the dchannel is down so put the channel in alarm */
#ifdef HAVE_PRI
				if (tmp->sig == SIG_PRI || tmp->sig == SIG_BRI || tmp->sig == SIG_BRI_PTMP)
					sig_pri_chan_alarm_notify(tmp->sig_pvt, si.alarms);
				else {
#endif
					tmp->inalarm = 1;
					handle_alarms(tmp, res);
#ifdef HAVE_PRI
				}
#endif
			}
		}

		tmp->polarityonanswerdelay = conf->chan.polarityonanswerdelay;
		tmp->answeronpolarityswitch = conf->chan.answeronpolarityswitch;
		tmp->hanguponpolarityswitch = conf->chan.hanguponpolarityswitch;
		tmp->sendcalleridafter = conf->chan.sendcalleridafter;

		if (!here) {
			tmp->locallyblocked = tmp->remotelyblocked = 0;
			if ((chan_sig == SIG_PRI) || (chan_sig == SIG_BRI) || (chan_sig == SIG_BRI_PTMP) || (chan_sig == SIG_SS7)) {
				tmp->inservice = 0;
#ifdef HAVE_PRI_SERVICE_MESSAGES
				if (chan_sig == SIG_PRI) {
					char db_chan_name[20], db_answer[5];

					snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, tmp->span, tmp->channel);
					if (ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
						snprintf(db_answer, sizeof(db_answer), "%s:%d", SRVST_TYPE_OOS, SRVST_INITIALIZED);
						ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
					}
				}
#endif
			} else {
				 /* We default to in service on protocols that don't have a reset */
				tmp->inservice = 1;
			}
		}

		if (tmp->sig != SIG_PRI && tmp->sig != SIG_BRI && tmp->sig != SIG_BRI_PTMP) {
			analog_p = tmp->sig_pvt;
			if (analog_p) {
				analog_p->channel = tmp->channel;
				analog_p->polarityonanswerdelay = conf->chan.polarityonanswerdelay;
				analog_p->answeronpolarityswitch = conf->chan.answeronpolarityswitch;
				analog_p->hanguponpolarityswitch = conf->chan.hanguponpolarityswitch;
				analog_p->sendcalleridafter = conf->chan.sendcalleridafter;
				analog_p->permcallwaiting = 1;
				analog_p->callreturn = conf->chan.callreturn;
				analog_p->cancallforward = conf->chan.cancallforward;
				analog_p->canpark = conf->chan.canpark;
				analog_p->dahditrcallerid = conf->chan.dahditrcallerid;
				analog_p->immediate = conf->chan.immediate;
				analog_p->permhidecallerid = conf->chan.permhidecallerid;
				analog_p->pulse = conf->chan.pulse;
				analog_p->threewaycalling = conf->chan.threewaycalling;
				analog_p->transfer = conf->chan.transfer;
				analog_p->transfertobusy = conf->chan.transfertobusy;
				analog_p->use_callerid = conf->chan.use_callerid;
				analog_p->outsigmod = ANALOG_SIG_NONE;
				analog_p->echotraining = conf->chan.echotraining;
				analog_p->cid_signalling = conf->chan.cid_signalling;
				analog_p->stripmsd = conf->chan.stripmsd;
				analog_p->cid_start = ANALOG_CID_START_RING;
				tmp->callwaitingcallerid = analog_p->callwaitingcallerid = 1;
	
				ast_copy_string(analog_p->mohsuggest, conf->chan.mohsuggest, sizeof(analog_p->mohsuggest));
				ast_copy_string(analog_p->cid_num, conf->chan.cid_num, sizeof(analog_p->cid_num));
				ast_copy_string(analog_p->cid_name, conf->chan.cid_name, sizeof(analog_p->cid_name));

				analog_config_complete(analog_p);
			}
		} 
#ifdef HAVE_PRI
		else if (pchan != NULL) {
			pchan->channel = tmp->channel;
			pchan->hidecallerid = tmp->hidecallerid;
			pchan->hidecalleridname = tmp->hidecalleridname;
			pchan->immediate = tmp->immediate;
			pchan->inalarm = tmp->inalarm;
			pchan->priexclusive = tmp->priexclusive;
			pchan->priindication_oob = tmp->priindication_oob;
			pchan->use_callerid = tmp->use_callerid;
			pchan->use_callingpres = tmp->use_callingpres;
			ast_copy_string(pchan->context, tmp->context, sizeof(pchan->context));
			ast_copy_string(pchan->mohinterpret, tmp->mohinterpret, sizeof(pchan->mohinterpret));
			pchan->stripmsd = tmp->stripmsd;
		}
#endif
	}
	if (tmp && !here) {
		/* nothing on the iflist */
		if (!*wlist) {
			*wlist = tmp;
			tmp->prev = NULL;
			tmp->next = NULL;
			*wend = tmp;
		} else {
			/* at least one member on the iflist */
			struct dahdi_pvt *working = *wlist;

			/* check if we maybe have to put it on the begining */
			if (working->channel > tmp->channel) {
				tmp->next = *wlist;
				tmp->prev = NULL;
				(*wlist)->prev = tmp;
				*wlist = tmp;
			} else {
			/* go through all the members and put the member in the right place */
				while (working) {
					/* in the middle */
					if (working->next) {
						if (working->channel < tmp->channel && working->next->channel > tmp->channel) {
							tmp->next = working->next;
							tmp->prev = working;
							working->next->prev = tmp;
							working->next = tmp;
							break;
						}
					} else {
					/* the last */
						if (working->channel < tmp->channel) {
							working->next = tmp;
							tmp->next = NULL;
							tmp->prev = working;
							*wend = tmp;
							break;
						}
					}
					working = working->next;
				}
			}
		}
	}
	return tmp;
}

static inline int available(struct dahdi_pvt *p, int channelmatch, ast_group_t groupmatch, int *reason, int *channelmatched, int *groupmatched)
{
	/* First, check group matching */
	if (groupmatch) {
		if ((p->group & groupmatch) != groupmatch)
			return 0;
		*groupmatched = 1;
	}
	/* Check to see if we have a channel match */
	if (channelmatch != -1) {
		if (p->channel != channelmatch)
			return 0;
		*channelmatched = 1;
	}

	if (p->inalarm)
		return 0;

	if (analog_lib_handles(p->sig, p->radio, p->oprmode))
		return analog_available(p->sig_pvt, channelmatch, groupmatch, reason, channelmatched, groupmatched);

#ifdef HAVE_PRI
	if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
		return sig_pri_available(p->sig_pvt, channelmatch, groupmatch, reason, channelmatched, groupmatched);
	}
#endif

#ifdef HAVE_SS7
		/* Trust SS7 */
		if (p->ss7) {
			if (p->ss7call)
				return 0;
			else
				return 1;
		}
#endif
#ifdef HAVE_OPENR2
		/* Trust MFC/R2 */
		if (p->mfcr2) {
			if (p->mfcr2call)
				return 0;
			else
				return 1;
		}
#endif

	return 0;
}

/* This function can *ONLY* be used for copying pseudo (CHAN_PSEUDO) private
   structures; it makes no attempt to safely copy regular channel private
   structures that might contain reference-counted object pointers and other
   scary bits
*/
static struct dahdi_pvt *duplicate_pseudo(struct dahdi_pvt *src)
{
	struct dahdi_pvt *p;
	struct dahdi_bufferinfo bi;
	int res;

	if ((p = ast_malloc(sizeof(*p)))) {
		memcpy(p, src, sizeof(struct dahdi_pvt));
		ast_mutex_init(&p->lock);
		p->subs[SUB_REAL].dfd = dahdi_open("/dev/dahdi/pseudo");
		if (p->subs[SUB_REAL].dfd < 0) {
			ast_log(LOG_ERROR, "Unable to dup channel: %s\n", strerror(errno));
			destroy_dahdi_pvt(&p);
			return NULL;
		}
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
		if (!res) {
			bi.txbufpolicy = src->buf_policy;
			bi.rxbufpolicy = src->buf_policy;
			bi.numbufs = src->buf_no;
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to set buffer policy on dup channel: %s\n", strerror(errno));
			}
		} else
			ast_log(LOG_WARNING, "Unable to check buffer policy on dup channel: %s\n", strerror(errno));
	}
	p->destroy = 1;
	p->next = iflist;
	p->prev = NULL;
	iflist = p;
	if (iflist->next)
		iflist->next->prev = p;
	return p;
}

static struct ast_channel *dahdi_request(const char *type, int format, void *data, int *cause)
{
	ast_group_t groupmatch = 0;
	int channelmatch = -1;
	int roundrobin = 0;
	int callwait = 0;
	int unavailreason = 0;
	struct dahdi_pvt *p;
	struct ast_channel *tmp = NULL;
	char *dest=NULL;
	int x;
	char *s;
	char opt=0;
	int res=0, y=0;
	int backwards = 0;
	struct dahdi_pvt *exitpvt, *start, *end;
	ast_mutex_t *lock;
	int channelmatched = 0;
	int groupmatched = 0;

	/*
	 * data is ---v
	 * Dial(DAHDI/pseudo[/extension])
	 * Dial(DAHDI/<channel#>[c|r<cadance#>|d][/extension])
	 * Dial(DAHDI/(g|G|r|R)<group#(0-63)>[c|r<cadance#>|d][/extension])
	 *
	 * g - channel group allocation search forward
	 * G - channel group allocation search backward
	 * r - channel group allocation round robin search forward
	 * R - channel group allocation round robin search backward
	 *
	 * c - Wait for DTMF digit to confirm answer
	 * r<cadance#> - Set distintive ring cadance number
	 * d - Force bearer capability for ISDN/SS7 call to digital.
	 */

	/* Assume we're locking the iflock */
	lock = &iflock;
	start = iflist;
	end = ifend;
	if (data) {
		dest = ast_strdupa((char *)data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	if (toupper(dest[0]) == 'G' || toupper(dest[0])=='R') {
		/* Retrieve the group number */
		char *stringp;

		stringp = dest + 1;
		s = strsep(&stringp, "/");
		if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", (char *)data);
			return NULL;
		}
		groupmatch = ((ast_group_t) 1 << x);
		if (toupper(dest[0]) == 'G') {
			if (dest[0] == 'G') {
				backwards = 1;
				p = ifend;
			} else
				p = iflist;
		} else {
			if (dest[0] == 'R') {
				backwards = 1;
				p = round_robin[x]?round_robin[x]->prev:ifend;
				if (!p)
					p = ifend;
			} else {
				p = round_robin[x]?round_robin[x]->next:iflist;
				if (!p)
					p = iflist;
			}
			roundrobin = 1;
		}
	} else {
		char *stringp;

		stringp = dest;
		s = strsep(&stringp, "/");
		p = iflist;
		if (!strcasecmp(s, "pseudo")) {
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
			channelmatch = x;
		}
		else if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			ast_log(LOG_WARNING, "Unable to determine channel for data %s\n", (char *)data);
			return NULL;
		} else {
			channelmatch = x;
		}
	}
	/* Search for an unowned channel */
	ast_mutex_lock(lock);
	exitpvt = p;
	while (p && !tmp) {
		if (roundrobin)
			round_robin[x] = p;
#if 0
		ast_verbose("name = %s, %d, %d, %d\n",p->owner ? p->owner->name : "<none>", p->channel, channelmatch, groupmatch);
#endif

		if (p && available(p, channelmatch, groupmatch, &unavailreason, &channelmatched, &groupmatched)) {
			ast_debug(1, "Using channel %d\n", p->channel);

			callwait = (p->owner != NULL);
#ifdef HAVE_OPENR2
			if (p->mfcr2) {
				ast_mutex_lock(&p->lock);
				if (p->mfcr2call) {
					ast_mutex_unlock(&p->lock);
					ast_log(LOG_DEBUG, "Yay!, someone just beat us in the race for channel %d.\n", p->channel);
					goto next;
				}
				p->mfcr2call = 1;
				ast_mutex_unlock(&p->lock);
				dahdi_r2_update_monitor_count(p->mfcr2, 0);
			}
#endif
			if (p->channel == CHAN_PSEUDO) {
				p = duplicate_pseudo(p);
				if (!p) {
					break;
				}
			}
			if (p->owner) {
				if (alloc_sub(p, SUB_CALLWAIT)) {
					p = NULL;
					break;
				}
			}
			p->outgoing = 1;
			if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
				tmp = analog_request(p->sig_pvt, &callwait);
			} 
#ifdef HAVE_PRI
			else if (p->sig == SIG_PRI || p->sig == SIG_BRI || p->sig == SIG_BRI_PTMP) {
				tmp = sig_pri_request(p->sig_pvt, SIG_PRI_DEFLAW);
			}
#endif   
			else {
				tmp = dahdi_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, 0);
			}

			/* Make special notes */
			if (res > 1) {
				if (opt == 'c') {
					/* Confirm answer */
					p->confirmanswer = 1;
				} else if (opt == 'r') {
					/* Distinctive ring */
					if (res < 3)
						ast_log(LOG_WARNING, "Distinctive ring missing identifier in '%s'\n", (char *)data);
					else
						p->distinctivering = y;
				} else if (opt == 'd') {
					/* If this is an ISDN call, make it digital */
					p->digital = 1;
					if (tmp)
						tmp->transfercapability = AST_TRANS_CAP_DIGITAL;
				} else {
					ast_log(LOG_WARNING, "Unknown option '%c' in '%s'\n", opt, (char *)data);
				}
			}
			/* Note if the call is a call waiting call */
			if (tmp && callwait)
				tmp->cdrflags |= AST_CDR_CALLWAIT;
			break;
		}
		if (backwards) {
			p = p->prev;
			if (!p)
				p = end;
		} else {
			p = p->next;
			if (!p)
				p = start;
		}
		/* stop when you roll to the one that we started from */
		if (p == exitpvt)
			break;
	}
	ast_mutex_unlock(lock);
	restart_monitor();
	if (callwait)
		*cause = AST_CAUSE_BUSY;
	else if (!tmp) {
		if (channelmatched) {
			if (unavailreason)
				*cause = AST_CAUSE_BUSY;
		} else if (groupmatched) {
			*cause = (unavailreason) ? unavailreason : AST_CAUSE_CONGESTION;
		}
	}

	return tmp;
}

#if defined(HAVE_SS7)
static int ss7_find_cic(struct dahdi_ss7 *linkset, int cic, unsigned int dpc)
{
	int i;
	int winner = -1;
	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && linkset->pvts[i]->cic == cic)) {
			winner = i;
			break;
		}
	}
	return winner;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void ss7_handle_cqm(struct dahdi_ss7 *linkset, int startcic, int endcic, unsigned int dpc)
{
	unsigned char status[32];
	struct dahdi_pvt *p = NULL;
	int i, offset;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic)))) {
			p = linkset->pvts[i];
			offset = p->cic - startcic;
			status[offset] = 0;
			if (p->locallyblocked)
				status[offset] |= (1 << 0) | (1 << 4);
			if (p->remotelyblocked)
				status[offset] |= (1 << 1) | (1 << 5);
			if (p->ss7call) {
				if (p->outgoing)
					status[offset] |= (1 << 3);
				else
					status[offset] |= (1 << 2);
			} else
				status[offset] |= 0x3 << 2;
		}
	}

	if (p)
		isup_cqr(linkset->ss7, startcic, endcic, dpc, status);
	else
		ast_log(LOG_WARNING, "Could not find any equipped circuits within CQM CICs\n");

}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static inline void ss7_hangup_cics(struct dahdi_ss7 *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic)))) {
			ast_mutex_lock(&linkset->pvts[i]->lock);
			if (linkset->pvts[i]->owner)
				linkset->pvts[i]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
			ast_mutex_unlock(&linkset->pvts[i]->lock);
		}
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static inline void ss7_block_cics(struct dahdi_ss7 *linkset, int startcic, int endcic, unsigned int dpc, unsigned char state[], int block)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic)))) {
			if (state) {
				if (state[i])
					linkset->pvts[i]->remotelyblocked = block;
			} else
				linkset->pvts[i]->remotelyblocked = block;
		}
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void ss7_inservice(struct dahdi_ss7 *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic))))
			linkset->pvts[i]->inservice = 1;
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void ss7_reset_linkset(struct dahdi_ss7 *linkset)
{
	int i, startcic = -1, endcic, dpc;

	if (linkset->numchans <= 0)
		return;

	startcic = linkset->pvts[0]->cic;
	/* DB: CIC's DPC fix */
	dpc = linkset->pvts[0]->dpc;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i+1] && linkset->pvts[i+1]->dpc == dpc && ((linkset->pvts[i+1]->cic - linkset->pvts[i]->cic) == 1) && (linkset->pvts[i]->cic - startcic < 31)) {
			continue;
		} else {
			endcic = linkset->pvts[i]->cic;
			ast_verbose("Resetting CICs %d to %d\n", startcic, endcic);
			isup_grs(linkset->ss7, startcic, endcic, dpc);

			/* DB: CIC's DPC fix */
			if (linkset->pvts[i+1]) {
				startcic = linkset->pvts[i+1]->cic;
				dpc = linkset->pvts[i+1]->dpc;
			}
		}
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void dahdi_loopback(struct dahdi_pvt *p, int enable)
{
	if (p->loopedback != enable) {
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_LOOPBACK, &enable)) {
			ast_log(LOG_WARNING, "Unable to set loopback on channel %d: %s\n", p->channel, strerror(errno));
			return;
		}
		p->loopedback = enable;
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
/* XXX: This function is assumed to be called with the private channel lock and linkset lock held */
static void ss7_start_call(struct dahdi_pvt *p, struct dahdi_ss7 *linkset)
{
	struct ss7 *ss7 = linkset->ss7;
	int res;
	int law = 1;
	struct ast_channel *c;
	char tmp[256];

	if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &law) == -1)
		ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", p->channel, law, strerror(errno));

	if (linkset->type == SS7_ITU)
		law = DAHDI_LAW_ALAW;
	else
		law = DAHDI_LAW_MULAW;

	res = dahdi_setlaw(p->subs[SUB_REAL].dfd, law);
	if (res < 0)
		ast_log(LOG_WARNING, "Unable to set law on channel %d\n", p->channel);

	if (!(linkset->flags & LINKSET_FLAG_EXPLICITACM)) {
		p->proceeding = 1;
		isup_acm(ss7, p->ss7call);
	}

	ast_mutex_unlock(&linkset->lock);
	c = dahdi_new(p, AST_STATE_RING, 1, SUB_REAL, law, 0);

	if (!c) {
		ast_log(LOG_WARNING, "Unable to start PBX on CIC %d\n", p->cic);
		/* Holding this lock is assumed entering the function */
		ast_mutex_lock(&linkset->lock);
		return;
	} else
		ast_verb(3, "Accepting call to '%s' on CIC %d\n", p->exten, p->cic);

	dahdi_enable_ec(p);

	/* We only reference these variables in the context of the ss7_linkset function
	 * when receiving either and IAM or a COT message.  Since they are only accessed
	 * from this context, we should be safe to unlock around them */

	ast_mutex_unlock(&p->lock);

	if (!ast_strlen_zero(p->charge_number)) {
		pbx_builtin_setvar_helper(c, "SS7_CHARGE_NUMBER", p->charge_number);
		/* Clear this after we set it */
		p->charge_number[0] = 0;
	}
	if (!ast_strlen_zero(p->gen_add_number)) {
		pbx_builtin_setvar_helper(c, "SS7_GENERIC_ADDRESS", p->gen_add_number);
		/* Clear this after we set it */
		p->gen_add_number[0] = 0;
	}
	if (!ast_strlen_zero(p->jip_number)) {
		pbx_builtin_setvar_helper(c, "SS7_JIP", p->jip_number);
		/* Clear this after we set it */
		p->jip_number[0] = 0;
	}
	if (!ast_strlen_zero(p->gen_dig_number)) {
		pbx_builtin_setvar_helper(c, "SS7_GENERIC_DIGITS", p->gen_dig_number);
		/* Clear this after we set it */
		p->gen_dig_number[0] = 0;
	}
	if (!ast_strlen_zero(p->orig_called_num)) {
		pbx_builtin_setvar_helper(c, "SS7_ORIG_CALLED_NUM", p->orig_called_num);
		/* Clear this after we set it */
		p->orig_called_num[0] = 0;
	}

	snprintf(tmp, sizeof(tmp), "%d", p->gen_dig_type);
	pbx_builtin_setvar_helper(c, "SS7_GENERIC_DIGTYPE", tmp);
	/* Clear this after we set it */
	p->gen_dig_type = 0;

	snprintf(tmp, sizeof(tmp), "%d", p->gen_dig_scheme);
	pbx_builtin_setvar_helper(c, "SS7_GENERIC_DIGSCHEME", tmp);
	/* Clear this after we set it */
	p->gen_dig_scheme = 0;

	if (!ast_strlen_zero(p->lspi_ident)) {
		pbx_builtin_setvar_helper(c, "SS7_LSPI_IDENT", p->lspi_ident);
		/* Clear this after we set it */
		p->lspi_ident[0] = 0;
	}

	snprintf(tmp, sizeof(tmp), "%d", p->call_ref_ident);
	pbx_builtin_setvar_helper(c, "SS7_CALLREF_IDENT", tmp);
	/* Clear this after we set it */
	p->call_ref_ident = 0;

	snprintf(tmp, sizeof(tmp), "%d", p->call_ref_pc);
	pbx_builtin_setvar_helper(c, "SS7_CALLREF_PC", tmp);
	/* Clear this after we set it */
	p->call_ref_pc = 0;

	snprintf(tmp, sizeof(tmp), "%d", p->calling_party_cat);
	pbx_builtin_setvar_helper(c, "SS7_CALLING_PARTY_CATEGORY", tmp);
	/* Clear this after we set it */
	p->calling_party_cat = 0;

	if (!ast_strlen_zero(p->redirecting_num)) {
		pbx_builtin_setvar_helper(c, "SS7_REDIRECTING_NUMBER", p->redirecting_num);
		/* Clear this after we set it */
		p->redirecting_num[0] = 0;
	}
	if (!ast_strlen_zero(p->generic_name)) {
		pbx_builtin_setvar_helper(c, "SS7_GENERIC_NAME", p->generic_name);
		/* Clear this after we set it */
		p->generic_name[0] = 0;
	}

	ast_mutex_lock(&p->lock);
	ast_mutex_lock(&linkset->lock);
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void ss7_apply_plan_to_number(char *buf, size_t size, const struct dahdi_ss7 *ss7, const char *number, const unsigned nai)
{
	if (ast_strlen_zero(number)) { /* make sure a number exists so prefix isn't placed on an empty string */
		if (size) {
			*buf = '\0';
		}
		return;
	}
	switch (nai) {
	case SS7_NAI_INTERNATIONAL:
		snprintf(buf, size, "%s%s", ss7->internationalprefix, number);
		break;
	case SS7_NAI_NATIONAL:
		snprintf(buf, size, "%s%s", ss7->nationalprefix, number);
		break;
	case SS7_NAI_SUBSCRIBER:
		snprintf(buf, size, "%s%s", ss7->subscriberprefix, number);
		break;
	case SS7_NAI_UNKNOWN:
		snprintf(buf, size, "%s%s", ss7->unknownprefix, number);
		break;
	default:
		snprintf(buf, size, "%s", number);
		break;
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static int ss7_pres_scr2cid_pres(char presentation_ind, char screening_ind)
{
	return ((presentation_ind & 0x3) << 5) | (screening_ind & 0x3);
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void *ss7_linkset(void *data)
{
	int res, i;
	struct timeval *next = NULL, tv;
	struct dahdi_ss7 *linkset = (struct dahdi_ss7 *) data;
	struct ss7 *ss7 = linkset->ss7;
	ss7_event *e = NULL;
	struct dahdi_pvt *p;
	int chanpos;
	struct pollfd pollers[NUM_DCHANS];
	int cic;
	unsigned int dpc;
	int nextms = 0;

	ss7_start(ss7);

	while(1) {
		ast_mutex_lock(&linkset->lock);
		if ((next = ss7_schedule_next(ss7))) {
			tv = ast_tvnow();
			tv.tv_sec = next->tv_sec - tv.tv_sec;
			tv.tv_usec = next->tv_usec - tv.tv_usec;
			if (tv.tv_usec < 0) {
				tv.tv_usec += 1000000;
				tv.tv_sec -= 1;
			}
			if (tv.tv_sec < 0) {
				tv.tv_sec = 0;
				tv.tv_usec = 0;
			}
			nextms = tv.tv_sec * 1000;
			nextms += tv.tv_usec / 1000;
		}
		ast_mutex_unlock(&linkset->lock);

		for (i = 0; i < linkset->numsigchans; i++) {
			pollers[i].fd = linkset->fds[i];
			pollers[i].events = ss7_pollflags(ss7, linkset->fds[i]);
			pollers[i].revents = 0;
		}

		res = poll(pollers, linkset->numsigchans, nextms);
		if ((res < 0) && (errno != EINTR)) {
			ast_log(LOG_ERROR, "poll(%s)\n", strerror(errno));
		} else if (!res) {
			ast_mutex_lock(&linkset->lock);
			ss7_schedule_run(ss7);
			ast_mutex_unlock(&linkset->lock);
			continue;
		}

		ast_mutex_lock(&linkset->lock);
		for (i = 0; i < linkset->numsigchans; i++) {
			if (pollers[i].revents & POLLPRI) {
				int x;
				if (ioctl(pollers[i].fd, DAHDI_GETEVENT, &x)) {
					ast_log(LOG_ERROR, "Error in exception retrieval!\n");
				}
				switch (x) {
				case DAHDI_EVENT_OVERRUN:
					ast_debug(1, "Overrun detected!\n");
					break;
				case DAHDI_EVENT_BADFCS:
					ast_debug(1, "Bad FCS\n");
					break;
				case DAHDI_EVENT_ABORT:
					ast_debug(1, "HDLC Abort\n");
					break;
				case DAHDI_EVENT_ALARM:
					ast_log(LOG_ERROR, "Alarm on link!\n");
					linkset->linkstate[i] |= (LINKSTATE_DOWN | LINKSTATE_INALARM);
					linkset->linkstate[i] &= ~LINKSTATE_UP;
					ss7_link_alarm(ss7, pollers[i].fd);
					break;
				case DAHDI_EVENT_NOALARM:
					ast_log(LOG_ERROR, "Alarm cleared on link\n");
					linkset->linkstate[i] &= ~(LINKSTATE_INALARM | LINKSTATE_DOWN);
					linkset->linkstate[i] |= LINKSTATE_STARTING;
					ss7_link_noalarm(ss7, pollers[i].fd);
					break;
				default:
					ast_log(LOG_ERROR, "Got exception %d!\n", x);
					break;
				}
			}

			if (pollers[i].revents & POLLIN) {
				res = ss7_read(ss7, pollers[i].fd);
			}

			if (pollers[i].revents & POLLOUT) {
				res = ss7_write(ss7, pollers[i].fd);
				if (res < 0) {
					ast_debug(1, "Error in write %s\n", strerror(errno));
				}
			}
		}

		while ((e = ss7_check_event(ss7))) {
			switch (e->e) {
			case SS7_EVENT_UP:
				if (linkset->state != LINKSET_STATE_UP) {
					ast_verbose("--- SS7 Up ---\n");
					ss7_reset_linkset(linkset);
				}
				linkset->state = LINKSET_STATE_UP;
				break;
			case SS7_EVENT_DOWN:
				ast_verbose("--- SS7 Down ---\n");
				linkset->state = LINKSET_STATE_DOWN;
				for (i = 0; i < linkset->numchans; i++) {
					struct dahdi_pvt *p = linkset->pvts[i];
					if (p)
						p->inalarm = 1;
				}
				break;
			case MTP2_LINK_UP:
				ast_verbose("MTP2 link up (SLC %d)\n", e->gen.data);
				break;
			case MTP2_LINK_DOWN:
				ast_log(LOG_WARNING, "MTP2 link down (SLC %d)\n", e->gen.data);
				break;
			case ISUP_EVENT_CPG:
				chanpos = ss7_find_cic(linkset, e->cpg.cic, e->cpg.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "CPG on unconfigured CIC %d\n", e->cpg.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_mutex_lock(&p->lock);
				switch (e->cpg.event) {
				case CPG_EVENT_ALERTING:
					p->alerting = 1;
					p->subs[SUB_REAL].needringing = 1;
					break;
				case CPG_EVENT_PROGRESS:
				case CPG_EVENT_INBANDINFO:
					{
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROGRESS, };
						ast_debug(1, "Queuing frame PROGRESS on CIC %d\n", p->cic);
						dahdi_queue_frame(p, &f, linkset);
						p->progress = 1;
						p->dialing = 0;
						if (p->dsp && p->dsp_features) {
							ast_dsp_set_features(p->dsp, p->dsp_features);
							p->dsp_features = 0;
						}
					}
					break;
				default:
					ast_debug(1, "Do not handle CPG with event type 0x%x\n", e->cpg.event);
				}

				ast_mutex_unlock(&p->lock);
				break;
			case ISUP_EVENT_RSC:
				ast_verbose("Resetting CIC %d\n", e->rsc.cic);
				chanpos = ss7_find_cic(linkset, e->rsc.cic, e->rsc.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "RSC on unconfigured CIC %d\n", e->rsc.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_mutex_lock(&p->lock);
				p->inservice = 1;
				p->remotelyblocked = 0;
				dpc = p->dpc;
				isup_set_call_dpc(e->rsc.call, dpc);
				if (p->ss7call)
					p->ss7call = NULL;
				if (p->owner)
					p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
				ast_mutex_unlock(&p->lock);
				isup_rlc(ss7, e->rsc.call);
				break;
			case ISUP_EVENT_GRS:
				ast_debug(1, "Got Reset for CICs %d to %d: Acknowledging\n", e->grs.startcic, e->grs.endcic);
				chanpos = ss7_find_cic(linkset, e->grs.startcic, e->grs.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "GRS on unconfigured CIC %d\n", e->grs.startcic);
					break;
				}
				p = linkset->pvts[chanpos];
				isup_gra(ss7, e->grs.startcic, e->grs.endcic, e->grs.opc);
				ss7_block_cics(linkset, e->grs.startcic, e->grs.endcic, e->grs.opc, NULL, 0);
				ss7_hangup_cics(linkset, e->grs.startcic, e->grs.endcic, e->grs.opc);
				break;
			case ISUP_EVENT_CQM:
				ast_debug(1, "Got Circuit group query message from CICs %d to %d\n", e->cqm.startcic, e->cqm.endcic);
				ss7_handle_cqm(linkset, e->cqm.startcic, e->cqm.endcic, e->cqm.opc);
				break;
			case ISUP_EVENT_GRA:
				ast_verbose("Got reset acknowledgement from CIC %d to %d.\n", e->gra.startcic, e->gra.endcic);
				ss7_inservice(linkset, e->gra.startcic, e->gra.endcic, e->gra.opc);
				ss7_block_cics(linkset, e->gra.startcic, e->gra.endcic, e->gra.opc, e->gra.status, 1);
				break;
			case ISUP_EVENT_IAM:
 				ast_debug(1, "Got IAM for CIC %d and called number %s, calling number %s\n", e->iam.cic, e->iam.called_party_num, e->iam.calling_party_num);
				chanpos = ss7_find_cic(linkset, e->iam.cic, e->iam.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "IAM on unconfigured CIC %d\n", e->iam.cic);
					isup_rel(ss7, e->iam.call, -1);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_mutex_lock(&p->lock);
				if (p->owner) {
					if (p->ss7call == e->iam.call) {
						ast_mutex_unlock(&p->lock);
						ast_log(LOG_WARNING, "Duplicate IAM requested on CIC %d\n", e->iam.cic);
						break;
					} else {
						ast_mutex_unlock(&p->lock);
						ast_log(LOG_WARNING, "Ring requested on CIC %d already in use!\n", e->iam.cic);
						break;
					}
				}

				dpc = p->dpc;
				p->ss7call = e->iam.call;
				isup_set_call_dpc(p->ss7call, dpc);

				if ((p->use_callerid) && (!ast_strlen_zero(e->iam.calling_party_num))) {
					ss7_apply_plan_to_number(p->cid_num, sizeof(p->cid_num), linkset, e->iam.calling_party_num, e->iam.calling_nai);
					p->callingpres = ss7_pres_scr2cid_pres(e->iam.presentation_ind, e->iam.screening_ind);
				} else
					p->cid_num[0] = 0;

				if (p->immediate) {
					p->exten[0] = 's';
					p->exten[1] = '\0';
				} else if (!ast_strlen_zero(e->iam.called_party_num)) {
					char *st;
					ss7_apply_plan_to_number(p->exten, sizeof(p->exten), linkset, e->iam.called_party_num, e->iam.called_nai);
					st = strchr(p->exten, '#');
					if (st)
						*st = '\0';
					} else
						p->exten[0] = '\0';

				p->cid_ani[0] = '\0';
				if ((p->use_callerid) && (!ast_strlen_zero(e->iam.generic_name)))
					ast_copy_string(p->cid_name, e->iam.generic_name, sizeof(p->cid_name));
				else
					p->cid_name[0] = '\0';

				p->cid_ani2 = e->iam.oli_ani2;
				p->cid_ton = 0;
				ast_copy_string(p->charge_number, e->iam.charge_number, sizeof(p->charge_number));
				ast_copy_string(p->gen_add_number, e->iam.gen_add_number, sizeof(p->gen_add_number));
				p->gen_add_type = e->iam.gen_add_type;
				p->gen_add_nai = e->iam.gen_add_nai;
				p->gen_add_pres_ind = e->iam.gen_add_pres_ind;
				p->gen_add_num_plan = e->iam.gen_add_num_plan;
				ast_copy_string(p->gen_dig_number, e->iam.gen_dig_number, sizeof(p->gen_dig_number));
				p->gen_dig_type = e->iam.gen_dig_type;
				p->gen_dig_scheme = e->iam.gen_dig_scheme;
				ast_copy_string(p->jip_number, e->iam.jip_number, sizeof(p->jip_number));
				ast_copy_string(p->orig_called_num, e->iam.orig_called_num, sizeof(p->orig_called_num));
				ast_copy_string(p->redirecting_num, e->iam.redirecting_num, sizeof(p->redirecting_num));
				ast_copy_string(p->generic_name, e->iam.generic_name, sizeof(p->generic_name));
				p->calling_party_cat = e->iam.calling_party_cat;

				/* Set DNID */
				if (!ast_strlen_zero(e->iam.called_party_num))
					ss7_apply_plan_to_number(p->dnid, sizeof(p->dnid), linkset, e->iam.called_party_num, e->iam.called_nai);

				if (ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num)) {

					if (e->iam.cot_check_required) {
						dahdi_loopback(p, 1);
					} else
						ss7_start_call(p, linkset);
				} else {
					ast_debug(1, "Call on CIC for unconfigured extension %s\n", p->exten);
					p->alreadyhungup = 1;
					isup_rel(ss7, e->iam.call, AST_CAUSE_UNALLOCATED);
				}
				ast_mutex_unlock(&p->lock);
				break;
			case ISUP_EVENT_COT:
				chanpos = ss7_find_cic(linkset, e->cot.cic, e->cot.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "COT on unconfigured CIC %d\n", e->cot.cic);
					isup_rel(ss7, e->cot.call, -1);
					break;
				}
				p = linkset->pvts[chanpos];

				ast_mutex_lock(&p->lock);

				if (p->loopedback) {
					dahdi_loopback(p, 0);
					ss7_start_call(p, linkset);
				}

				ast_mutex_unlock(&p->lock);

				break;
			case ISUP_EVENT_CCR:
				ast_debug(1, "Got CCR request on CIC %d\n", e->ccr.cic);
				chanpos = ss7_find_cic(linkset, e->ccr.cic, e->ccr.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "CCR on unconfigured CIC %d\n", e->ccr.cic);
					break;
				}

				p = linkset->pvts[chanpos];

				ast_mutex_lock(&p->lock);
				dahdi_loopback(p, 1);
				ast_mutex_unlock(&p->lock);

				isup_lpa(linkset->ss7, e->ccr.cic, p->dpc);
				break;
			case ISUP_EVENT_CVT:
				ast_debug(1, "Got CVT request on CIC %d\n", e->cvt.cic);
				chanpos = ss7_find_cic(linkset, e->cvt.cic, e->cvt.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "CVT on unconfigured CIC %d\n", e->cvt.cic);
					break;
				}

				p = linkset->pvts[chanpos];

				ast_mutex_lock(&p->lock);
				dahdi_loopback(p, 1);
				ast_mutex_unlock(&p->lock);

				isup_cvr(linkset->ss7, e->cvt.cic, p->dpc);
				break;
			case ISUP_EVENT_REL:
				chanpos = ss7_find_cic(linkset, e->rel.cic, e->rel.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "REL on unconfigured CIC %d\n", e->rel.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_mutex_lock(&p->lock);
				if (p->owner) {
					p->owner->hangupcause = e->rel.cause;
					p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
				} else if (!p->restartpending)
					ast_log(LOG_WARNING, "REL on channel (CIC %d) without owner!\n", p->cic);

				/* End the loopback if we have one */
				dahdi_loopback(p, 0);

				isup_rlc(ss7, e->rel.call);
				p->ss7call = NULL;

				ast_mutex_unlock(&p->lock);
				break;
			case ISUP_EVENT_ACM:
				chanpos = ss7_find_cic(linkset, e->acm.cic, e->acm.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "ACM on unconfigured CIC %d\n", e->acm.cic);
					isup_rel(ss7, e->acm.call, -1);
					break;
				} else {
					struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROCEEDING, };

					p = linkset->pvts[chanpos];

					ast_debug(1, "Queueing frame from SS7_EVENT_ACM on CIC %d\n", p->cic);

					if (e->acm.call_ref_ident > 0) {
						p->rlt = 1; /* Setting it but not using it here*/
					}

					ast_mutex_lock(&p->lock);
					dahdi_queue_frame(p, &f, linkset);
					p->proceeding = 1;
					p->dialing = 0;
					/* Send alerting if subscriber is free */
					if (e->acm.called_party_status_ind == 1) {
						p->alerting = 1;
						p->subs[SUB_REAL].needringing = 1;
					}
					ast_mutex_unlock(&p->lock);
				}
				break;
			case ISUP_EVENT_CGB:
 				chanpos = ss7_find_cic(linkset, e->cgb.startcic, e->cgb.opc);
 				if (chanpos < 0) {
 					ast_log(LOG_WARNING, "CGB on unconfigured CIC %d\n", e->cgb.startcic);
 					break;
 				}
 				p = linkset->pvts[chanpos];
				ss7_block_cics(linkset, e->cgb.startcic, e->cgb.endcic, e->cgb.opc, e->cgb.status, 1);
 				isup_cgba(linkset->ss7, e->cgb.startcic, e->cgb.endcic, e->cgb.opc, e->cgb.status, e->cgb.type);
				break;
			case ISUP_EVENT_CGU:
 				chanpos = ss7_find_cic(linkset, e->cgu.startcic, e->cgu.opc);
 				if (chanpos < 0) {
 					ast_log(LOG_WARNING, "CGU on unconfigured CIC %d\n", e->cgu.startcic);
 					break;
 				}
 				p = linkset->pvts[chanpos];
				ss7_block_cics(linkset, e->cgu.startcic, e->cgu.endcic, e->cgu.opc, e->cgu.status, 0);
 				isup_cgua(linkset->ss7, e->cgu.startcic, e->cgu.endcic, e->cgu.opc, e->cgu.status, e->cgu.type);
				break;
			case ISUP_EVENT_UCIC:
				chanpos = ss7_find_cic(linkset, e->ucic.cic, e->ucic.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "UCIC on unconfigured CIC %d\n", e->ucic.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unequiped Circuit Id Code on CIC %d\n", e->ucic.cic);
				ast_mutex_lock(&p->lock);
				p->remotelyblocked = 1;
				p->inservice = 0;
				ast_mutex_unlock(&p->lock);			/* doesn't require a SS7 acknowledgement */
				break;
			case ISUP_EVENT_BLO:
				chanpos = ss7_find_cic(linkset, e->blo.cic, e->blo.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "BLO on unconfigured CIC %d\n", e->blo.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Blocking CIC %d\n", e->blo.cic);
				ast_mutex_lock(&p->lock);
				p->remotelyblocked = 1;
				ast_mutex_unlock(&p->lock);
				isup_bla(linkset->ss7, e->blo.cic, p->dpc);
				break;
			case ISUP_EVENT_BLA:
				chanpos = ss7_find_cic(linkset, e->bla.cic, e->bla.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "BLA on unconfigured CIC %d\n", e->bla.cic);
					break;
				}
				ast_debug(1, "Blocking CIC %d\n", e->bla.cic);
				p = linkset->pvts[chanpos];
				ast_mutex_lock(&p->lock);
				p->locallyblocked = 1;
				ast_mutex_unlock(&p->lock);
				break;
			case ISUP_EVENT_UBL:
				chanpos = ss7_find_cic(linkset, e->ubl.cic, e->ubl.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "UBL on unconfigured CIC %d\n", e->ubl.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unblocking CIC %d\n", e->ubl.cic);
				ast_mutex_lock(&p->lock);
				p->remotelyblocked = 0;
				ast_mutex_unlock(&p->lock);
				isup_uba(linkset->ss7, e->ubl.cic, p->dpc);
				break;
			case ISUP_EVENT_UBA:
				chanpos = ss7_find_cic(linkset, e->uba.cic, e->uba.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "UBA on unconfigured CIC %d\n", e->uba.cic);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unblocking CIC %d\n", e->uba.cic);
				ast_mutex_lock(&p->lock);
				p->locallyblocked = 0;
				ast_mutex_unlock(&p->lock);
				break;
			case ISUP_EVENT_CON:
			case ISUP_EVENT_ANM:
				if (e->e == ISUP_EVENT_CON)
					cic = e->con.cic;
				else
					cic = e->anm.cic;

				chanpos = ss7_find_cic(linkset, cic, (e->e == ISUP_EVENT_ANM) ? e->anm.opc : e->con.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "ANM/CON on unconfigured CIC %d\n", cic);
					isup_rel(ss7, (e->e == ISUP_EVENT_ANM) ? e->anm.call : e->con.call, -1);
					break;
				} else {
					p = linkset->pvts[chanpos];
					ast_mutex_lock(&p->lock);
					p->subs[SUB_REAL].needanswer = 1;
					if (p->dsp && p->dsp_features) {
						ast_dsp_set_features(p->dsp, p->dsp_features);
						p->dsp_features = 0;
					}
					dahdi_enable_ec(p);
					ast_mutex_unlock(&p->lock);
				}
				break;
			case ISUP_EVENT_RLC:
				chanpos = ss7_find_cic(linkset, e->rlc.cic, e->rlc.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "RLC on unconfigured CIC %d\n", e->rlc.cic);
					break;
				} else {
					p = linkset->pvts[chanpos];
					ast_mutex_lock(&p->lock);
					if (p->alreadyhungup)
						p->ss7call = NULL;
					else
						ast_log(LOG_NOTICE, "Received RLC out and we haven't sent REL.  Ignoring.\n");
					ast_mutex_unlock(&p->lock);
					}
					break;
			case ISUP_EVENT_FAA:
				chanpos = ss7_find_cic(linkset, e->faa.cic, e->faa.opc);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "FAA on unconfigured CIC %d\n", e->faa.cic);
					break;
				} else {
					p = linkset->pvts[chanpos];
					ast_debug(1, "FAA received on CIC %d\n", e->faa.cic);
					ast_mutex_lock(&p->lock);
					if (p->alreadyhungup){
						p->ss7call = NULL;
						ast_log(LOG_NOTICE, "Received FAA and we haven't sent FAR.  Ignoring.\n");
					}
					ast_mutex_unlock(&p->lock);
				}
				break;
			default:
				ast_debug(1, "Unknown event %s\n", ss7_event2str(e->e));
				break;
			}
		}
		ast_mutex_unlock(&linkset->lock);
	}

	return 0;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void dahdi_ss7_message(struct ss7 *ss7, char *s)
{
#if 0
	int i;

	for (i = 0; i < NUM_SPANS; i++)
		if (linksets[i].ss7 == ss7)
			break;

	ast_verbose("[%d] %s", i+1, s);
#else
	ast_verbose("%s", s);
#endif
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void dahdi_ss7_error(struct ss7 *ss7, char *s)
{
#if 0
	int i;

	for (i = 0; i < NUM_SPANS; i++)
		if (linksets[i].ss7 == ss7)
			break;

#else
	ast_log(LOG_ERROR, "%s", s);
#endif
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_OPENR2)
static void *mfcr2_monitor(void *data)
{
	struct dahdi_mfcr2 *mfcr2 = data;
	/* we should be using pthread_key_create
	   and allocate pollers dynamically.
	   I think do_monitor() could be leaking, since it
	   could be cancelled at any time and is not
	   using thread keys, why?, */
	struct pollfd pollers[sizeof(mfcr2->pvts)];
	int nextms = 0;
	int res = 0;
	int i = 0;
	int oldstate = 0;
	int quit_loop = 0;
	/* now that we're ready to get calls, unblock our side and
	   get current line state */
	for (i = 0; i < mfcr2->numchans; i++) {
		openr2_chan_set_idle(mfcr2->pvts[i]->r2chan);
		openr2_chan_handle_cas(mfcr2->pvts[i]->r2chan);
	}
	while (1) {
		/* we trust here that the mfcr2 channel list will not ever change once
		   the module is loaded */
		ast_mutex_lock(&mfcr2->monitored_count_lock);
		if (mfcr2->monitored_count == 0) {
			ast_log(LOG_DEBUG, "No one requires my monitoring services :-(\n");
			ast_cond_wait(&mfcr2->do_monitor, &mfcr2->monitored_count_lock);
			ast_log(LOG_DEBUG, "Alright, back to work!\n");
		}

		for (i = 0; i < mfcr2->numchans; i++) {
			pollers[i].revents = 0;
			pollers[i].events = 0;
			if (mfcr2->pvts[i]->owner) {
				continue;
			}
			if (!mfcr2->pvts[i]->r2chan) {
				ast_log(LOG_DEBUG, "Wow, no r2chan on channel %d\n", mfcr2->pvts[i]->channel);
				quit_loop = 1;
				break;
			}
			openr2_chan_enable_read(mfcr2->pvts[i]->r2chan);
			pollers[i].events = POLLIN | POLLPRI;
			pollers[i].fd = mfcr2->pvts[i]->subs[SUB_REAL].dfd;
		}
		ast_mutex_unlock(&mfcr2->monitored_count_lock);
		if (quit_loop) {
			break;
		}
		nextms = openr2_context_get_time_to_next_event(mfcr2->protocol_context);
		/* probably poll() is a valid cancel point, lets just be on the safe side
		   by calling pthread_testcancel */
		pthread_testcancel();
		res = poll(pollers, mfcr2->numchans, nextms);
		pthread_testcancel();
		if ((res < 0) && (errno != EINTR)) {
			ast_log(LOG_ERROR, "going out, poll failed: %s\n", strerror(errno));
			break;
		}
		/* do we want to allow to cancel while processing events? */
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
		for (i = 0; i < mfcr2->numchans; i++) {
			if (pollers[i].revents & POLLPRI || pollers[i].revents & POLLIN) {
				openr2_chan_process_event(mfcr2->pvts[i]->r2chan);
			}
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	}
	ast_log(LOG_NOTICE, "Quitting MFC/R2 monitor thread\n");
	return 0;
}
#endif /* HAVE_OPENR2 */

#if defined(HAVE_PRI)
#ifndef PRI_RESTART
#error "Upgrade your libpri"
#endif
static void dahdi_pri_message(struct pri *pri, char *s)
{
	int x, y;
	int dchan = -1, span = -1, dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < NUM_DCHANS; y++) {
				if (pris[x].pri.dchans[y])
					dchancount++;

				if (pris[x].pri.dchans[y] == pri)
					dchan = y;
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if (dchancount > 1 && (span > -1))
			ast_verbose("[Span %d D-Channel %d]%s", span, dchan, s);
		else
			ast_verbose("%s", s);
	} else
		ast_verbose("%s", s);

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0) {
		if (write(pridebugfd, s, strlen(s)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
	}

	ast_mutex_unlock(&pridebugfdlock);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static void dahdi_pri_error(struct pri *pri, char *s)
{
	int x, y;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < NUM_DCHANS; y++) {
				if (pris[x].pri.dchans[y])
					dchancount++;

				if (pris[x].pri.dchans[y] == pri)
					dchan = y;
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if ((dchancount > 1) && (span > -1))
			ast_log(LOG_ERROR, "[Span %d D-Channel %d] PRI: %s", span, dchan, s);
		else
			ast_log(LOG_ERROR, "%s", s);
	} else
		ast_log(LOG_ERROR, "%s", s);

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0) {
		if (write(pridebugfd, s, strlen(s)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
	}

	ast_mutex_unlock(&pridebugfdlock);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static int prepare_pri(struct dahdi_pri *pri)
{
	int i, res, x;
	struct dahdi_params p;
	struct dahdi_bufferinfo bi;
	struct dahdi_spaninfo si;

	pri->pri.calls = &dahdi_pri_callbacks;

	for (i = 0; i < NUM_DCHANS; i++) {
		if (!pri->dchannels[i])
			break;
		pri->pri.fds[i] = open("/dev/dahdi/channel", O_RDWR);
		x = pri->dchannels[i];
		if ((pri->pri.fds[i] < 0) || (ioctl(pri->pri.fds[i],DAHDI_SPECIFY,&x) == -1)) {
			ast_log(LOG_ERROR, "Unable to open D-channel %d (%s)\n", x, strerror(errno));
			return -1;
		}
		memset(&p, 0, sizeof(p));
		res = ioctl(pri->pri.fds[i], DAHDI_GET_PARAMS, &p);
		if (res) {
			dahdi_close_pri_fd(pri, i);
			ast_log(LOG_ERROR, "Unable to get parameters for D-channel %d (%s)\n", x, strerror(errno));
			return -1;
		}
		if ((p.sigtype != DAHDI_SIG_HDLCFCS) && (p.sigtype != DAHDI_SIG_HARDHDLC)) {
			dahdi_close_pri_fd(pri, i);
			ast_log(LOG_ERROR, "D-channel %d is not in HDLC/FCS mode.\n", x);
			return -1;
		}
		memset(&si, 0, sizeof(si));
		res = ioctl(pri->pri.fds[i], DAHDI_SPANSTAT, &si);
		if (res) {
			dahdi_close_pri_fd(pri, i);
			ast_log(LOG_ERROR, "Unable to get span state for D-channel %d (%s)\n", x, strerror(errno));
		}
		if (!si.alarms) {
			pri_event_noalarm(&pri->pri, i, 1);
		} else {
			pri_event_alarm(&pri->pri, i, 1);
		}
		memset(&bi, 0, sizeof(bi));
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = 32;
		bi.bufsize = 1024;
		if (ioctl(pri->pri.fds[i], DAHDI_SET_BUFINFO, &bi)) {
			ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d: %s\n", x, strerror(errno));
			dahdi_close_pri_fd(pri, i);
			return -1;
		}
		pri->pri.dchan_logical_span[i] = pris[p.spanno - 1].prilogicalspan;
	}
	return 0;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *complete_span_helper(const char *line, const char *word, int pos, int state, int rpos)
{
	int which, span;
	char *ret = NULL;

	if (pos != rpos)
		return ret;

	for (which = span = 0; span < NUM_SPANS; span++) {
		if (pris[span].pri.pri && ++which > state) {
			if (asprintf(&ret, "%d", span + 1) < 0) {	/* user indexes start from 1 */
				ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			}
			break;
		}
	}
	return ret;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *complete_span_4(const char *line, const char *word, int pos, int state)
{
	return complete_span_helper(line,word,pos,state,3);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_set_debug_file(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int myfd;
	switch (cmd) {
	case CLI_INIT:
		e->command = "pri set debug file";
		e->usage = "Usage: pri set debug file [output-file]\n"
			"       Sends PRI debug output to the specified output file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 5)
		return CLI_SHOWUSAGE;

	if (ast_strlen_zero(a->argv[4]))
		return CLI_SHOWUSAGE;

	myfd = open(a->argv[4], O_CREAT|O_WRONLY, AST_FILE_MODE);
	if (myfd < 0) {
		ast_cli(a->fd, "Unable to open '%s' for writing\n", a->argv[4]);
		return CLI_SUCCESS;
	}

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0)
		close(pridebugfd);

	pridebugfd = myfd;
	ast_copy_string(pridebugfilename,a->argv[4],sizeof(pridebugfilename));
	ast_mutex_unlock(&pridebugfdlock);
	ast_cli(a->fd, "PRI debug output will be sent to '%s'\n", a->argv[4]);
	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;
	int x;
	int level = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "pri set debug {on|off|0|1|2} span";
		e->usage =
			"Usage: pri set debug {<level>|on|off} span <span>\n"
			"       Enables debugging on a given PRI span\n";
		return NULL;
	case CLI_GENERATE:
		return complete_span_4(a->line, a->word, a->pos, a->n);
	}
	if (a->argc < 6) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "on")) {
		level = 1;
	} else if (!strcasecmp(a->argv[3], "off")) {
		level = 0;
	} else {
		level = atoi(a->argv[3]);
	}
	span = atoi(a->argv[5]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid span %s.  Should be a number %d to %d\n", a->argv[5], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!pris[span-1].pri.pri) {
		ast_cli(a->fd, "No PRI running on span %d\n", span);
		return CLI_SUCCESS;
	}
	for (x = 0; x < NUM_DCHANS; x++) {
		if (pris[span-1].pri.dchans[x]) {
			if (level == 1) {
				pri_set_debug(pris[span-1].pri.dchans[x], PRI_DEBUG_APDU |
					PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE |
					PRI_DEBUG_Q921_STATE);
				ast_cli(a->fd, "Enabled debugging on span %d\n", span);
			} else if (level == 0) {
				pri_set_debug(pris[span-1].pri.dchans[x], 0);
				/* close the file if it's set */
				ast_mutex_lock(&pridebugfdlock);
				close(pridebugfd);
				pridebugfd = -1;
				ast_cli(a->fd, "PRI debug output to file disabled\n");
				ast_mutex_unlock(&pridebugfdlock);
			} else {
				pri_set_debug(pris[span-1].pri.dchans[x], PRI_DEBUG_APDU |
					PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE |
					PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_STATE);
				ast_cli(a->fd, "Enabled debugging on span %d\n", span);
			}
		}
	}
	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#ifdef HAVE_PRI_SERVICE_MESSAGES
static char *handle_pri_service_generic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a, int changestatus)
{
	int why;
	int channel;
	int trunkgroup;
	int x, y, fd = a->fd;
	int interfaceid = 0;
	char *c;
	char state;
	char db_chan_name[20], db_answer[5];
	struct dahdi_pvt *start, *tmp = NULL;
	struct dahdi_pri *pri = NULL;
	ast_mutex_t *lock;
	
	lock = &iflock;
	start = iflist;

	if (a->argc < 5 || a->argc > 6)
		return CLI_SHOWUSAGE;
	if ((c = strchr(a->argv[4], ':'))) {
		if (sscanf(a->argv[4], "%d:%d", &trunkgroup, &channel) != 2)
			return CLI_SHOWUSAGE;
		if ((trunkgroup < 1) || (channel < 1))
			return CLI_SHOWUSAGE;
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].pri.trunkgroup == trunkgroup) {
				pri = pris + x;
				break;
			}
		}
		if (pri) {
			lock = &pri->pri.lock;
		} else {
			ast_cli(fd, "No such trunk group %d\n", trunkgroup);
			return CLI_FAILURE;
		}
	} else
		channel = atoi(a->argv[4]);
	
	if (a->argc == 6)
		interfaceid = atoi(a->argv[5]);

	/* either servicing a D-Channel */
	for (x = 0; x < NUM_SPANS; x++) {
		for (y = 0; y < NUM_DCHANS; y++) {
			if (pris[x].dchannels[y] == channel) {
				pri = pris + x;
				pri_maintenance_service(pri->pri.pri, interfaceid, -1, changestatus);
				return CLI_SUCCESS;
			}
		}
	}

	/* or servicing a B-Channel */
	ast_mutex_lock(lock);
	tmp = start;
	while (tmp) {
		if (tmp->pri && tmp->channel == channel) {
			if (!tmp->pri->enable_service_message_support) {
				ast_cli(fd, "\n\tThis operation has not been enabled in chan_dahdi.conf, set 'service_message_support=yes' to use this operation.\n\tNote only 4ESS and 5ESS switch types are supported.\n\n");
				return CLI_SUCCESS;
			}
			why = -1;
			snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, tmp->span, channel);
			if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
				sscanf(db_answer, "%c:%d", &state, &why);
				ast_db_del(db_chan_name, SRVST_DBKEY);
			}
			switch(changestatus) {
			case 0: /* enable */
				if (why > -1) {
					if (why & SRVST_FAREND) {
						why = SRVST_FAREND;
						snprintf(db_answer, sizeof(db_answer), "%s:%d", SRVST_TYPE_OOS, why);
						ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
						ast_debug(2, "channel '%d' service state { near: in-service,  far: out-of-service }\n", channel);
					}
				}
				break;
			/* case 1:  -- loop */
			case 2: /* disable */
				if (why == -1) {
					why = SRVST_NEAREND;
				} else {
					why |= SRVST_NEAREND;
				}
				snprintf(db_answer, sizeof(db_answer), "%s:%d", SRVST_TYPE_OOS, why);
				ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
				break;
			/* case 3:  -- continuity */
			/* case 4:  -- shutdown */
			default:
				ast_log(LOG_WARNING, "Unsupported changestatus: '%d'\n", changestatus);
			}
			pri_maintenance_bservice(tmp->pri->pri, tmp->sig_pvt, changestatus);
			ast_mutex_unlock(lock);
			return CLI_SUCCESS;
		}
		tmp = tmp->next;
	}
	ast_mutex_unlock(lock);

	ast_cli(fd, "Unable to find given channel %d, possibly not a PRI\n", channel);
	return CLI_FAILURE;
}

static char *handle_pri_service_enable_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:	
		e->command = "pri service enable channel";
		e->usage = 
			"Usage: pri service enable channel <channel> [<interface id>]\n"
			"       Send an AT&T / NFAS / CCS ANSI T1.607 maintenance message\n"
			"	to restore a channel to service, with optional interface id\n"
			"	as agreed upon with remote switch operator\n";
		return NULL;
	case CLI_GENERATE:	
		return NULL;
	}
	return handle_pri_service_generic(e, cmd, a, 0);
}

static char *handle_pri_service_disable_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:	
		e->command = "pri service disable channel";
		e->usage = 
			"Usage: pri service disable channel <chan num> [<interface id>]\n"
			"	Send an AT&T / NFAS / CCS ANSI T1.607 maintenance message\n"
			"	to remove a channel from service, with optional interface id\n"
			"	as agreed upon with remote switch operator\n";
		return NULL;
	case CLI_GENERATE:	
		return NULL;
	}
	return handle_pri_service_generic(e, cmd, a, 2);
}
#endif

#if defined(HAVE_PRI)
static char *handle_pri_show_spans(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pri show spans";
		e->usage =
			"Usage: pri show spans\n"
			"       Displays PRI Information\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	for (span = 0; span < NUM_SPANS; span++) {
		if (pris[span].pri.pri) {
			sig_pri_cli_show_spans(a->fd, span + 1, &pris[span].pri);
		}
	}
	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_show_span(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pri show span";
		e->usage =
			"Usage: pri show span <span>\n"
			"       Displays PRI Information on a given PRI span\n";
		return NULL;
	case CLI_GENERATE:
		return complete_span_4(a->line, a->word, a->pos, a->n);
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;
	span = atoi(a->argv[3]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid span '%s'.  Should be a number from %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!pris[span-1].pri.pri) {
		ast_cli(a->fd, "No PRI running on span %d\n", span);
		return CLI_SUCCESS;
	}

	sig_pri_cli_show_span(a->fd, pris[span-1].dchannels, &pris[span-1].pri);

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_show_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int x;
	int span;
	int count=0;
	int debug=0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pri show debug";
		e->usage =
			"Usage: pri show debug\n"
			"	Show the debug state of pri spans\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	for (span = 0; span < NUM_SPANS; span++) {
		if (pris[span].pri.pri) {
			for (x = 0; x < NUM_DCHANS; x++) {
				debug = 0;
				if (pris[span].pri.dchans[x]) {
					debug = pri_get_debug(pris[span].pri.dchans[x]);
					ast_cli(a->fd, "Span %d: Debug: %s\tIntense: %s\n", span+1, (debug&PRI_DEBUG_Q931_STATE)? "Yes" : "No" ,(debug&PRI_DEBUG_Q921_RAW)? "Yes" : "No" );
					count++;
				}
			}
		}

	}
	ast_mutex_lock(&pridebugfdlock);
	if (pridebugfd >= 0)
		ast_cli(a->fd, "Logging PRI debug to file %s\n", pridebugfilename);
	ast_mutex_unlock(&pridebugfdlock);

	if (!count)
		ast_cli(a->fd, "No debug set or no PRI running\n");
	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "pri show version";
		e->usage =
			"Usage: pri show version\n"
			"Show libpri version information\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "libpri version: %s\n", pri_get_version());

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static struct ast_cli_entry dahdi_pri_cli[] = {
	AST_CLI_DEFINE(handle_pri_debug, "Enables PRI debugging on a span"),
#ifdef HAVE_PRI_SERVICE_MESSAGES
 	AST_CLI_DEFINE(handle_pri_service_enable_channel, "Return a channel to service"),
 	AST_CLI_DEFINE(handle_pri_service_disable_channel, "Remove a channel from service"),
#endif
	AST_CLI_DEFINE(handle_pri_show_spans, "Displays PRI Information"),
	AST_CLI_DEFINE(handle_pri_show_span, "Displays PRI Information"),
	AST_CLI_DEFINE(handle_pri_show_debug, "Displays current PRI debug settings"),
	AST_CLI_DEFINE(handle_pri_set_debug_file, "Sends PRI debug output to the specified file"),
	AST_CLI_DEFINE(handle_pri_version, "Displays libpri version"),
};
#endif	/* defined(HAVE_PRI) */

#ifdef HAVE_OPENR2

static char *handle_mfcr2_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 show version";
		e->usage =
			"Usage: mfcr2 show version\n"
			"       Shows the version of the OpenR2 library being used.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	ast_cli(a->fd, "OpenR2 version: %s, revision: %s\n", openr2_get_version(), openr2_get_revision());
	return CLI_SUCCESS;
}

static char *handle_mfcr2_show_variants(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%4s %40s\n"
	int i = 0;
	int numvariants = 0;
	const openr2_variant_entry_t *variants;
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 show variants";
		e->usage =
			"Usage: mfcr2 show variants\n"
			"       Shows the list of MFC/R2 variants supported.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (!(variants = openr2_proto_get_variant_list(&numvariants))) {
		ast_cli(a->fd, "Failed to get list of variants.\n");
		return CLI_FAILURE;
	}
	ast_cli(a->fd, FORMAT, "Variant Code", "Country");
	for (i = 0; i < numvariants; i++) {
		ast_cli(a->fd, FORMAT, variants[i].name, variants[i].country);
	}
	return CLI_SUCCESS;
#undef FORMAT
}

static char *handle_mfcr2_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%4s %-7.7s %-7.7s %-8.8s %-9.9s %-16.16s %-8.8s %-8.8s\n"
	int filtertype = 0;
	int targetnum = 0;
	char channo[5];
	char anino[5];
	char dnisno[5];
	struct dahdi_pvt *p;
	openr2_context_t *r2context;
	openr2_variant_t r2variant;
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 show channels [group|context]";
		e->usage =
			"Usage: mfcr2 show channels [group <group> | context <context>]\n"
			"       Shows the DAHDI channels configured with MFC/R2 signaling.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (!((a->argc == 3) || (a->argc == 5))) {
		return CLI_SHOWUSAGE;
	}
	if (a->argc == 5) {
		if (!strcasecmp(a->argv[3], "group")) {
			targetnum = atoi(a->argv[4]);
			if ((targetnum < 0) || (targetnum > 63))
				return CLI_SHOWUSAGE;
			targetnum = 1 << targetnum;
			filtertype = 1;
		} else if (!strcasecmp(a->argv[3], "context")) {
			filtertype = 2;
		} else {
			return CLI_SHOWUSAGE;
		}
	}
	ast_cli(a->fd, FORMAT, "Chan", "Variant", "Max ANI", "Max DNIS", "ANI First", "Immediate Accept", "Tx CAS", "Rx CAS");
	ast_mutex_lock(&iflock);
	p = iflist;
	for (p = iflist; p; p = p->next) {
		if (!(p->sig & SIG_MFCR2) || !p->r2chan) {
			continue;
		}
		if (filtertype) {
			switch(filtertype) {
			case 1: /* mfcr2 show channels group <group> */
				if (p->group != targetnum) {
					continue;
				}
				break;
			case 2: /* mfcr2 show channels context <context> */
				if (strcasecmp(p->context, a->argv[4])) {
					continue;
				}
				break;
			default:
				;
			}
		}
		r2context = openr2_chan_get_context(p->r2chan);
		r2variant = openr2_context_get_variant(r2context);
		snprintf(channo, sizeof(channo), "%d", p->channel);
		snprintf(anino, sizeof(anino), "%d", openr2_context_get_max_ani(r2context));
		snprintf(dnisno, sizeof(dnisno), "%d", openr2_context_get_max_dnis(r2context));
		ast_cli(a->fd, FORMAT, channo, openr2_proto_get_variant_string(r2variant),
				anino, dnisno, openr2_context_get_ani_first(r2context) ? "Yes" : "No",
				openr2_context_get_immediate_accept(r2context) ? "Yes" : "No",
				openr2_chan_get_tx_cas_string(p->r2chan), openr2_chan_get_rx_cas_string(p->r2chan));
	}
	ast_mutex_unlock(&iflock);
	return CLI_SUCCESS;
#undef FORMAT
}

static char *handle_mfcr2_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct dahdi_pvt *p = NULL;
	int channo = 0;
	char *toklevel = NULL;
	char *saveptr = NULL;
	char *logval = NULL;
	openr2_log_level_t loglevel = OR2_LOG_NOTHING;
	openr2_log_level_t tmplevel = OR2_LOG_NOTHING;
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 set debug";
		e->usage =
			"Usage: mfcr2 set debug <loglevel> <channel>\n"
			"       Set a new logging level for the specified channel.\n"
			"       If no channel is specified the logging level will be applied to all channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}
	channo = (a->argc == 5) ? atoi(a->argv[4]) : -1;
	logval = ast_strdupa(a->argv[3]);
	toklevel = strtok_r(logval, ",", &saveptr);
	if (-1 == (tmplevel = openr2_log_get_level(toklevel))) {
		ast_cli(a->fd, "Invalid MFC/R2 logging level '%s'.\n", a->argv[3]);
		return CLI_FAILURE;
	} else if (OR2_LOG_NOTHING == tmplevel) {
		loglevel = tmplevel;
	} else {
		loglevel |= tmplevel;
		while ((toklevel = strtok_r(NULL, ",", &saveptr))) {
			if (-1 == (tmplevel = openr2_log_get_level(toklevel))) {
				ast_cli(a->fd, "Ignoring invalid logging level: '%s'.\n", toklevel);
				continue;
			}
			loglevel |= tmplevel;
		}
	}
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (!(p->sig & SIG_MFCR2) || !p->r2chan) {
			continue;
		}
		if ((channo != -1) && (p->channel != channo )) {
			continue;
		}
		openr2_chan_set_log_level(p->r2chan, loglevel);
		if (channo != -1) {
			ast_cli(a->fd, "MFC/R2 debugging set to '%s' for channel %d.\n", a->argv[3], p->channel);
			break;
		}
	}
	if ((channo != -1) && !p) {
		ast_cli(a->fd, "MFC/R2 channel %d not found.\n", channo);
	}
	if (channo == -1) {
		ast_cli(a->fd, "MFC/R2 debugging set to '%s' for all channels.\n", a->argv[3]);
	}
	ast_mutex_unlock(&iflock);
	return CLI_SUCCESS;
}

static char *handle_mfcr2_call_files(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct dahdi_pvt *p = NULL;
	int channo = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 call files [on|off]";
		e->usage =
			"Usage: mfcr2 call files [on|off] <channel>\n"
			"       Enable call files creation on the specified channel.\n"
			"       If no channel is specified call files creation policy will be applied to all channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}
	channo = (a->argc == 5) ? atoi(a->argv[4]) : -1;
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (!(p->sig & SIG_MFCR2) || !p->r2chan) {
			continue;
		}
		if ((channo != -1) && (p->channel != channo )) {
			continue;
		}
		if (ast_true(a->argv[3])) {
			openr2_chan_enable_call_files(p->r2chan);
		} else {
			openr2_chan_disable_call_files(p->r2chan);
		}
		if (channo != -1) {
			if (ast_true(a->argv[3])) {
				ast_cli(a->fd, "MFC/R2 call files enabled for channel %d.\n", p->channel);
			} else {
				ast_cli(a->fd, "MFC/R2 call files disabled for channel %d.\n", p->channel);
			}
			break;
		}
	}
	if ((channo != -1) && !p) {
		ast_cli(a->fd, "MFC/R2 channel %d not found.\n", channo);
	}
	if (channo == -1) {
		if (ast_true(a->argv[3])) {
			ast_cli(a->fd, "MFC/R2 Call files enabled for all channels.\n");
		} else {
			ast_cli(a->fd, "MFC/R2 Call files disabled for all channels.\n");
		}
	}
	ast_mutex_unlock(&iflock);
	return CLI_SUCCESS;
}

static char *handle_mfcr2_set_idle(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct dahdi_pvt *p = NULL;
	int channo = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 set idle";
		e->usage =
			"Usage: mfcr2 set idle <channel>\n"
			"       DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.\n"
			"       Force the given channel into IDLE state.\n"
			"       If no channel is specified, all channels will be set to IDLE.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	channo = (a->argc == 4) ? atoi(a->argv[3]) : -1;
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (!(p->sig & SIG_MFCR2) || !p->r2chan) {
			continue;
		}
		if ((channo != -1) && (p->channel != channo )) {
			continue;
		}
		openr2_chan_set_idle(p->r2chan);
		ast_mutex_lock(&p->lock);
		p->locallyblocked = 0;
		ast_mutex_unlock(&p->lock);
		if (channo != -1) {
			break;
		}
	}
	if ((channo != -1) && !p) {
		ast_cli(a->fd, "MFC/R2 channel %d not found.\n", channo);
	}
	ast_mutex_unlock(&iflock);
	return CLI_SUCCESS;
}

static char *handle_mfcr2_set_blocked(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct dahdi_pvt *p = NULL;
	int channo = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "mfcr2 set blocked";
		e->usage =
			"Usage: mfcr2 set blocked <channel>\n"
			"       DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.\n"
			"       Force the given channel into BLOCKED state.\n"
			"       If no channel is specified, all channels will be set to BLOCKED.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	channo = (a->argc == 4) ? atoi(a->argv[3]) : -1;
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (!(p->sig & SIG_MFCR2) || !p->r2chan) {
			continue;
		}
		if ((channo != -1) && (p->channel != channo )) {
			continue;
		}
		openr2_chan_set_blocked(p->r2chan);
		ast_mutex_lock(&p->lock);
		p->locallyblocked = 1;
		ast_mutex_unlock(&p->lock);
		if (channo != -1) {
			break;
		}
	}
	if ((channo != -1) && !p) {
		ast_cli(a->fd, "MFC/R2 channel %d not found.\n", channo);
	}
	ast_mutex_unlock(&iflock);
	return CLI_SUCCESS;
}

static struct ast_cli_entry dahdi_mfcr2_cli[] = {
	AST_CLI_DEFINE(handle_mfcr2_version, "Show OpenR2 library version"),
	AST_CLI_DEFINE(handle_mfcr2_show_variants, "Show supported MFC/R2 variants"),
	AST_CLI_DEFINE(handle_mfcr2_show_channels, "Show MFC/R2 channels"),
	AST_CLI_DEFINE(handle_mfcr2_set_debug, "Set MFC/R2 channel logging level"),
	AST_CLI_DEFINE(handle_mfcr2_call_files, "Enable/Disable MFC/R2 call files"),
	AST_CLI_DEFINE(handle_mfcr2_set_idle, "Reset MFC/R2 channel forcing it to IDLE"),
	AST_CLI_DEFINE(handle_mfcr2_set_blocked, "Reset MFC/R2 channel forcing it to BLOCKED"),
};

#endif /* HAVE_OPENR2 */

static char *dahdi_destroy_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int channel;
	int ret;
	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi destroy channel";
		e->usage =
			"Usage: dahdi destroy channel <chan num>\n"
			"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	channel = atoi(a->argv[3]);
	ret = dahdi_destroy_channel_bynum(channel);
	return ( RESULT_SUCCESS == ret ) ? CLI_SUCCESS : CLI_FAILURE;
}

static void dahdi_softhangup_all(void)
{
	struct dahdi_pvt *p;
retry:
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		ast_mutex_lock(&p->lock);
		if (p->owner && !p->restartpending) {
			if (ast_channel_trylock(p->owner)) {
				if (option_debug > 2)
					ast_verbose("Avoiding deadlock\n");
				/* Avoid deadlock since you're not supposed to lock iflock or pvt before a channel */
				ast_mutex_unlock(&p->lock);
				ast_mutex_unlock(&iflock);
				goto retry;
			}
			if (option_debug > 2)
				ast_verbose("Softhanging up on %s\n", p->owner->name);
			ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_EXPLICIT);
			p->restartpending = 1;
			num_restart_pending++;
			ast_channel_unlock(p->owner);
		}
		ast_mutex_unlock(&p->lock);
	}
	ast_mutex_unlock(&iflock);
}

static int setup_dahdi(int reload);
static int dahdi_restart(void)
{
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int i, j;
#endif
	int cancel_code;
	struct dahdi_pvt *p;

	ast_mutex_lock(&restart_lock);
	ast_verb(1, "Destroying channels and reloading DAHDI configuration.\n");
	dahdi_softhangup_all();
	ast_verb(4, "Initial softhangup of all DAHDI channels complete.\n");
#ifdef HAVE_OPENR2
	dahdi_r2_destroy_links();
#endif

#if defined(HAVE_PRI)
	for (i = 0; i < NUM_SPANS; i++) {
		if (pris[i].pri.master && (pris[i].pri.master != AST_PTHREADT_NULL)) {
			cancel_code = pthread_cancel(pris[i].pri.master);
			pthread_kill(pris[i].pri.master, SIGURG);
			ast_debug(4, "Waiting to join thread of span %d with pid=%p, cancel_code=%d\n", i, (void *) pris[i].pri.master, cancel_code);
			pthread_join(pris[i].pri.master, NULL);
			ast_debug(4, "Joined thread of span %d\n", i);
		}
	}
#endif

#if defined(HAVE_SS7)
	for (i = 0; i < NUM_SPANS; i++) {
		if (linksets[i].master && (linksets[i].master != AST_PTHREADT_NULL)) {
			cancel_code = pthread_cancel(linksets[i].master);
			pthread_kill(linksets[i].master, SIGURG);
			ast_debug(4, "Waiting to join thread of span %d with pid=%p, cancel_code=%d\n", i, (void *) linksets[i].master, cancel_code);
			pthread_join(linksets[i].master, NULL);
			ast_debug(4, "Joined thread of span %d\n", i);
		}
	}
#endif

	ast_mutex_lock(&monlock);
	if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL)) {
		cancel_code = pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		ast_debug(4, "Waiting to join monitor thread with pid=%p, cancel_code=%d\n", (void *) monitor_thread, cancel_code);
		pthread_join(monitor_thread, NULL);
		ast_debug(4, "Joined monitor thread\n");
	}
	monitor_thread = AST_PTHREADT_NULL; /* prepare to restart thread in setup_dahdi once channels are reconfigured */

	ast_mutex_lock(&ss_thread_lock);
	while (ss_thread_count > 0) { /* let ss_threads finish and run dahdi_hangup before dahvi_pvts are destroyed */
		int x = DAHDI_FLASH;
		ast_debug(3, "Waiting on %d analog_ss_thread(s) to finish\n", ss_thread_count);

		for (p = iflist; p; p = p->next) {
			if (p->owner)
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x); /* important to create an event for dahdi_wait_event to register so that all analog_ss_threads terminate */
			}
		ast_cond_wait(&ss_thread_complete, &ss_thread_lock);
	}

	/* ensure any created channels before monitor threads were stopped are hungup */
	dahdi_softhangup_all();
	ast_verb(4, "Final softhangup of all DAHDI channels complete.\n");
	destroy_all_channels();
	ast_debug(1, "Channels destroyed. Now re-reading config. %d active channels remaining.\n", ast_active_channels());

	ast_mutex_unlock(&monlock);

#ifdef HAVE_PRI
	for (i = 0; i < NUM_SPANS; i++) {
		for (j = 0; j < NUM_DCHANS; j++)
			dahdi_close_pri_fd(&(pris[i]), j);
	}

	memset(pris, 0, sizeof(pris));
	for (i = 0; i < NUM_SPANS; i++) {
		ast_mutex_init(&pris[i].pri.lock);
		pris[i].pri.master = AST_PTHREADT_NULL;
		for (j = 0; j < NUM_DCHANS; j++)
			pris[i].pri.fds[j] = -1;
		}
	pri_set_error(dahdi_pri_error);
	pri_set_message(dahdi_pri_message);
#endif
#ifdef HAVE_SS7
	for (i = 0; i < NUM_SPANS; i++) {
		for (j = 0; j < NUM_DCHANS; j++)
			dahdi_close_ss7_fd(&(linksets[i]), j);
	}

	memset(linksets, 0, sizeof(linksets));
	for (i = 0; i < NUM_SPANS; i++) {
		ast_mutex_init(&linksets[i].lock);
		linksets[i].master = AST_PTHREADT_NULL;
		for (j = 0; j < NUM_DCHANS; j++)
			linksets[i].fds[j] = -1;
	}
	ss7_set_error(dahdi_ss7_error);
	ss7_set_message(dahdi_ss7_message);
#endif

	if (setup_dahdi(2) != 0) {
		ast_log(LOG_WARNING, "Reload channels from dahdi config failed!\n");
		ast_mutex_unlock(&ss_thread_lock);
		return 1;
	}
	ast_mutex_unlock(&ss_thread_lock);
	ast_mutex_unlock(&restart_lock);
	return 0;
}

static char *dahdi_restart_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi restart";
		e->usage =
			"Usage: dahdi restart\n"
			"	Restarts the DAHDI channels: destroys them all and then\n"
			"	re-reads them from chan_dahdi.conf.\n"
			"	Note that this will STOP any running CALL on DAHDI channels.\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	if (dahdi_restart() != 0)
		return CLI_FAILURE;
	return CLI_SUCCESS;
}

static int action_dahdirestart(struct mansession *s, const struct message *m)
{
	if (dahdi_restart() != 0) {
		astman_send_error(s, m, "Failed rereading DAHDI configuration");
		return 1;
	}
	astman_send_ack(s, m, "DAHDIRestart: Success");
	return 0;
}

static char *dahdi_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%7s %-10.10s %-15.15s %-10.10s %-20.20s %-10.10s %-10.10s\n"
#define FORMAT2 "%7s %-10.10s %-15.15s %-10.10s %-20.20s %-10.10s %-10.10s\n"
	unsigned int targetnum = 0;
	int filtertype = 0;
	struct dahdi_pvt *tmp = NULL;
	char tmps[20] = "";
	char statestr[20] = "";
	char blockstr[20] = "";
	ast_mutex_t *lock;
	struct dahdi_pvt *start;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi show channels [group|context]";
		e->usage =
			"Usage: dahdi show channels [ group <group> | context <context> ]\n"
			"	Shows a list of available channels with optional filtering\n"
			"	<group> must be a number between 0 and 63\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	lock = &iflock;
	start = iflist;

	/* syntax: dahdi show channels [ group <group> | context <context> ] */

	if (!((a->argc == 3) || (a->argc == 5)))
		return CLI_SHOWUSAGE;

	if (a->argc == 5) {
		if (!strcasecmp(a->argv[3], "group")) {
			targetnum = atoi(a->argv[4]);
			if ((targetnum < 0) || (targetnum > 63))
				return CLI_SHOWUSAGE;
			targetnum = 1 << targetnum;
			filtertype = 1;
		} else if (!strcasecmp(a->argv[3], "context")) {
			filtertype = 2;
		}
	}

	ast_mutex_lock(lock);
	ast_cli(a->fd, FORMAT2, "Chan", "Extension", "Context", "Language", "MOH Interpret", "Blocked", "State");

	tmp = start;
	while (tmp) {
		if (filtertype) {
			switch(filtertype) {
			case 1: /* dahdi show channels group <group> */
				if (!(tmp->group & targetnum)) {
					tmp = tmp->next;
					continue;
				}
				break;
			case 2: /* dahdi show channels context <context> */
				if (strcasecmp(tmp->context, a->argv[4])) {
					tmp = tmp->next;
					continue;
				}
				break;
			default:
				;
			}
		}
		if (tmp->channel > 0) {
			snprintf(tmps, sizeof(tmps), "%d", tmp->channel);
		} else
			ast_copy_string(tmps, "pseudo", sizeof(tmps));

		if (tmp->locallyblocked)
			blockstr[0] = 'L';
		else
			blockstr[0] = ' ';

		if (tmp->remotelyblocked)
			blockstr[1] = 'R';
		else
			blockstr[1] = ' ';

		blockstr[2] = '\0';

		snprintf(statestr, sizeof(statestr), "%s", "In Service");

		ast_cli(a->fd, FORMAT, tmps, tmp->exten, tmp->context, tmp->language, tmp->mohinterpret, blockstr, statestr);
		tmp = tmp->next;
	}
	ast_mutex_unlock(lock);
	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char *dahdi_show_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int channel;
	struct dahdi_pvt *tmp = NULL;
	struct dahdi_confinfo ci;
	struct dahdi_params ps;
	int x;
	ast_mutex_t *lock;
	struct dahdi_pvt *start;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi show channel";
		e->usage =
			"Usage: dahdi show channel <chan num>\n"
			"	Detailed information about a given channel\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	lock = &iflock;
	start = iflist;

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	channel = atoi(a->argv[3]);

	ast_mutex_lock(lock);
	tmp = start;
	while (tmp) {
		if (tmp->channel == channel) {
			ast_cli(a->fd, "Channel: %d\n", tmp->channel);
			ast_cli(a->fd, "File Descriptor: %d\n", tmp->subs[SUB_REAL].dfd);
			ast_cli(a->fd, "Span: %d\n", tmp->span);
			ast_cli(a->fd, "Extension: %s\n", tmp->exten);
			ast_cli(a->fd, "Dialing: %s\n", tmp->dialing ? "yes" : "no");
			ast_cli(a->fd, "Context: %s\n", tmp->context);
			ast_cli(a->fd, "Caller ID: %s\n", tmp->cid_num);
			ast_cli(a->fd, "Calling TON: %d\n", tmp->cid_ton);
			ast_cli(a->fd, "Caller ID name: %s\n", tmp->cid_name);
			ast_cli(a->fd, "Mailbox: %s\n", S_OR(tmp->mailbox, "none"));
			if (tmp->vars) {
				struct ast_variable *v;
				ast_cli(a->fd, "Variables:\n");
				for (v = tmp->vars ; v ; v = v->next)
					ast_cli(a->fd, "       %s = %s\n", v->name, v->value);
			}
			ast_cli(a->fd, "Destroy: %d\n", tmp->destroy);
			ast_cli(a->fd, "InAlarm: %d\n", tmp->inalarm);
			ast_cli(a->fd, "Signalling Type: %s\n", sig2str(tmp->sig));
			ast_cli(a->fd, "Radio: %d\n", tmp->radio);
			ast_cli(a->fd, "Owner: %s\n", tmp->owner ? tmp->owner->name : "<None>");
			ast_cli(a->fd, "Real: %s%s%s\n", tmp->subs[SUB_REAL].owner ? tmp->subs[SUB_REAL].owner->name : "<None>", tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : "", tmp->subs[SUB_REAL].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Callwait: %s%s%s\n", tmp->subs[SUB_CALLWAIT].owner ? tmp->subs[SUB_CALLWAIT].owner->name : "<None>", tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : "", tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Threeway: %s%s%s\n", tmp->subs[SUB_THREEWAY].owner ? tmp->subs[SUB_THREEWAY].owner->name : "<None>", tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : "", tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Confno: %d\n", tmp->confno);
			ast_cli(a->fd, "Propagated Conference: %d\n", tmp->propconfno);
			ast_cli(a->fd, "Real in conference: %d\n", tmp->inconference);
			ast_cli(a->fd, "DSP: %s\n", tmp->dsp ? "yes" : "no");
			ast_cli(a->fd, "Busy Detection: %s\n", tmp->busydetect ? "yes" : "no");
			if (tmp->busydetect) {
#if defined(BUSYDETECT_TONEONLY)
				ast_cli(a->fd, "    Busy Detector Helper: BUSYDETECT_TONEONLY\n");
#elif defined(BUSYDETECT_COMPARE_TONE_AND_SILENCE)
				ast_cli(a->fd, "    Busy Detector Helper: BUSYDETECT_COMPARE_TONE_AND_SILENCE\n");
#endif
#ifdef BUSYDETECT_DEBUG
				ast_cli(a->fd, "    Busy Detector Debug: Enabled\n");
#endif
				ast_cli(a->fd, "    Busy Count: %d\n", tmp->busycount);
				ast_cli(a->fd, "    Busy Pattern: %d,%d\n", tmp->busy_tonelength, tmp->busy_quietlength);
			}
			ast_cli(a->fd, "TDD: %s\n", tmp->tdd ? "yes" : "no");
			ast_cli(a->fd, "Relax DTMF: %s\n", tmp->dtmfrelax ? "yes" : "no");
			ast_cli(a->fd, "Dialing/CallwaitCAS: %d/%d\n", tmp->dialing, tmp->callwaitcas);
			ast_cli(a->fd, "Default law: %s\n", tmp->law == DAHDI_LAW_MULAW ? "ulaw" : tmp->law == DAHDI_LAW_ALAW ? "alaw" : "unknown");
			ast_cli(a->fd, "Fax Handled: %s\n", tmp->faxhandled ? "yes" : "no");
			ast_cli(a->fd, "Pulse phone: %s\n", tmp->pulsedial ? "yes" : "no");
			ast_cli(a->fd, "DND: %s\n", tmp->dnd ? "yes" : "no");
			ast_cli(a->fd, "Echo Cancellation:\n");

			if (tmp->echocancel.head.tap_length) {
				ast_cli(a->fd, "\t%d taps\n", tmp->echocancel.head.tap_length);
				for (x = 0; x < tmp->echocancel.head.param_count; x++) {
					ast_cli(a->fd, "\t\t%s: %ud\n", tmp->echocancel.params[x].name, tmp->echocancel.params[x].value);
				}
				ast_cli(a->fd, "\t%scurrently %s\n", tmp->echocanbridged ? "" : "(unless TDM bridged) ", tmp->echocanon ? "ON" : "OFF");
			} else {
				ast_cli(a->fd, "\tnone\n");
			}
			ast_cli(a->fd, "Wait for dialtone: %dms\n", tmp->waitfordialtone);
			if (tmp->master)
				ast_cli(a->fd, "Master Channel: %d\n", tmp->master->channel);
			for (x = 0; x < MAX_SLAVES; x++) {
				if (tmp->slaves[x])
					ast_cli(a->fd, "Slave Channel: %d\n", tmp->slaves[x]->channel);
			}
#ifdef HAVE_OPENR2
			if (tmp->mfcr2) {
				char calldir[OR2_MAX_PATH];
				openr2_context_t *r2context = openr2_chan_get_context(tmp->r2chan);
				openr2_variant_t r2variant = openr2_context_get_variant(r2context);
				ast_cli(a->fd, "MFC/R2 MF State: %s\n", openr2_chan_get_mf_state_string(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 MF Group: %s\n", openr2_chan_get_mf_group_string(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 State: %s\n", openr2_chan_get_r2_state_string(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 Call State: %s\n", openr2_chan_get_call_state_string(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 Call Files Enabled: %s\n", openr2_chan_get_call_files_enabled(tmp->r2chan) ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 Variant: %s\n", openr2_proto_get_variant_string(r2variant));
				ast_cli(a->fd, "MFC/R2 Max ANI: %d\n", openr2_context_get_max_ani(r2context));
				ast_cli(a->fd, "MFC/R2 Max DNIS: %d\n", openr2_context_get_max_dnis(r2context));
				ast_cli(a->fd, "MFC/R2 Get ANI First: %s\n", openr2_context_get_ani_first(r2context) ? "Yes" : "No");
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
				ast_cli(a->fd, "MFC/R2 Skip Category Request: %s\n", openr2_context_get_skip_category_request(r2context) ? "Yes" : "No");
#endif
				ast_cli(a->fd, "MFC/R2 Immediate Accept: %s\n", openr2_context_get_immediate_accept(r2context) ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 Accept on Offer: %s\n", tmp->mfcr2_accept_on_offer ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 Charge Calls: %s\n", tmp->mfcr2_charge_calls ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 Allow Collect Calls: %s\n", tmp->mfcr2_allow_collect_calls ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 Forced Release: %s\n", tmp->mfcr2_forced_release ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 MF Back Timeout: %dms\n", openr2_context_get_mf_back_timeout(r2context));
				ast_cli(a->fd, "MFC/R2 R2 Metering Pulse Timeout: %dms\n", openr2_context_get_metering_pulse_timeout(r2context));
				ast_cli(a->fd, "MFC/R2 Rx CAS: %s\n", openr2_chan_get_rx_cas_string(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 Tx CAS: %s\n", openr2_chan_get_tx_cas_string(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 MF Tx Signal: %d\n", openr2_chan_get_tx_mf_signal(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 MF Rx Signal: %d\n", openr2_chan_get_rx_mf_signal(tmp->r2chan));
				ast_cli(a->fd, "MFC/R2 Call Files Directory: %s\n", openr2_context_get_log_directory(r2context, calldir, sizeof(calldir)));
			}
#endif
#ifdef HAVE_SS7
			if (tmp->ss7) {
				ast_cli(a->fd, "CIC: %d\n", tmp->cic);
			}
#endif
#ifdef HAVE_PRI
			if (tmp->pri) {
				struct sig_pri_chan *chan = tmp->sig_pvt;
				ast_cli(a->fd, "PRI Flags: ");
				if (chan->resetting)
					ast_cli(a->fd, "Resetting ");
				if (chan->call)
					ast_cli(a->fd, "Call ");
				ast_cli(a->fd, "\n");
				if (tmp->logicalspan)
					ast_cli(a->fd, "PRI Logical Span: %d\n", tmp->logicalspan);
				else
					ast_cli(a->fd, "PRI Logical Span: Implicit\n");
			}
#endif
			memset(&ci, 0, sizeof(ci));
			ps.channo = tmp->channel;
			if (tmp->subs[SUB_REAL].dfd > -1) {
				memset(&ci, 0, sizeof(ci));
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONF, &ci)) {
					ast_cli(a->fd, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, ci.confmode);
				}
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONFMUTE, &x)) {
					ast_cli(a->fd, "Actual Confmute: %s\n", x ? "Yes" : "No");
				}
				memset(&ps, 0, sizeof(ps));
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
					ast_log(LOG_WARNING, "Failed to get parameters on channel %d: %s\n", tmp->channel, strerror(errno));
				} else {
					ast_cli(a->fd, "Hookstate (FXS only): %s\n", ps.rxisoffhook ? "Offhook" : "Onhook");
				}
			}
			ast_mutex_unlock(lock);
			return CLI_SUCCESS;
		}
		tmp = tmp->next;
	}

	ast_cli(a->fd, "Unable to find given channel %d\n", channel);
	ast_mutex_unlock(lock);
	return CLI_FAILURE;
}

static char *handle_dahdi_show_cadences(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i, j;
	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi show cadences";
		e->usage =
			"Usage: dahdi show cadences\n"
			"       Shows all cadences currently defined\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	for (i = 0; i < num_cadence; i++) {
		char output[1024];
		char tmp[16], tmp2[64];
		snprintf(tmp, sizeof(tmp), "r%d: ", i + 1);
		term_color(output, tmp, COLOR_GREEN, COLOR_BLACK, sizeof(output));

		for (j = 0; j < 16; j++) {
			if (cadences[i].ringcadence[j] == 0)
				break;
			snprintf(tmp, sizeof(tmp), "%d", cadences[i].ringcadence[j]);
			if (cidrings[i] * 2 - 1 == j)
				term_color(tmp2, tmp, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp2) - 1);
			else
				term_color(tmp2, tmp, COLOR_GREEN, COLOR_BLACK, sizeof(tmp2) - 1);
			if (j != 0)
				strncat(output, ",", sizeof(output) - strlen(output) - 1);
			strncat(output, tmp2, sizeof(output) - strlen(output) - 1);
		}
		ast_cli(a->fd,"%s\n",output);
	}
	return CLI_SUCCESS;
}

/* Based on irqmiss.c */
static char *dahdi_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	#define FORMAT "%-40.40s %-7.7s %-6d %-6d %-6d %-3.3s %-4.4s %-8.8s %s\n"
	#define FORMAT2 "%-40.40s %-7.7s %-6.6s %-6.6s %-6.6s %-3.3s %-4.4s %-8.8s %s\n"
	int span;
	int res;
	char alarmstr[50];

	int ctl;
	struct dahdi_spaninfo s;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi show status";
		e->usage =
			"Usage: dahdi show status\n"
			"       Shows a list of DAHDI cards with status\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0) {
		ast_cli(a->fd, "No DAHDI found. Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		return CLI_FAILURE;
	}
	ast_cli(a->fd, FORMAT2, "Description", "Alarms", "IRQ", "bpviol", "CRC4", "Framing", "Coding", "Options", "LBO");

	for (span = 1; span < DAHDI_MAX_SPANS; ++span) {
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res) {
			continue;
		}
		alarmstr[0] = '\0';
		if (s.alarms > 0) {
			if (s.alarms & DAHDI_ALARM_BLUE)
				strcat(alarmstr, "BLU/");
			if (s.alarms & DAHDI_ALARM_YELLOW)
				strcat(alarmstr, "YEL/");
			if (s.alarms & DAHDI_ALARM_RED)
				strcat(alarmstr, "RED/");
			if (s.alarms & DAHDI_ALARM_LOOPBACK)
				strcat(alarmstr, "LB/");
			if (s.alarms & DAHDI_ALARM_RECOVER)
				strcat(alarmstr, "REC/");
			if (s.alarms & DAHDI_ALARM_NOTOPEN)
				strcat(alarmstr, "NOP/");
			if (!strlen(alarmstr))
				strcat(alarmstr, "UUU/");
			if (strlen(alarmstr)) {
				/* Strip trailing / */
				alarmstr[strlen(alarmstr) - 1] = '\0';
			}
		} else {
			if (s.numchans)
				strcpy(alarmstr, "OK");
			else
				strcpy(alarmstr, "UNCONFIGURED");
		}

		ast_cli(a->fd, FORMAT, s.desc, alarmstr, s.irqmisses, s.bpvcount, s.crc4count,
			s.lineconfig & DAHDI_CONFIG_D4 ? "D4" :
			s.lineconfig & DAHDI_CONFIG_ESF ? "ESF" :
			s.lineconfig & DAHDI_CONFIG_CCS ? "CCS" :
			"CAS",
			s.lineconfig & DAHDI_CONFIG_B8ZS ? "B8ZS" :
			s.lineconfig & DAHDI_CONFIG_HDB3 ? "HDB3" :
			s.lineconfig & DAHDI_CONFIG_AMI ? "AMI" :
			"Unk",
			s.lineconfig & DAHDI_CONFIG_CRC4 ?
			s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" : "YEL",
			lbostr[s.lbo]
			);
	}
	close(ctl);

	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char *dahdi_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int pseudo_fd = -1;
	struct dahdi_versioninfo vi;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi show version";
		e->usage =
			"Usage: dahdi show version\n"
			"       Shows the DAHDI version in use\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if ((pseudo_fd = open("/dev/dahdi/ctl", O_RDONLY)) < 0) {
		ast_cli(a->fd, "Failed to open control file to get version.\n");
		return CLI_SUCCESS;
	}

	strcpy(vi.version, "Unknown");
	strcpy(vi.echo_canceller, "Unknown");

	if (ioctl(pseudo_fd, DAHDI_GETVERSION, &vi))
		ast_cli(a->fd, "Failed to get DAHDI version: %s\n", strerror(errno));
	else
		ast_cli(a->fd, "DAHDI Version: %s Echo Canceller: %s\n", vi.version, vi.echo_canceller);

	close(pseudo_fd);

	return CLI_SUCCESS;
}

static char *dahdi_set_hwgain(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int channel;
	int gain;
	int tx;
	struct dahdi_hwgain hwgain;
	struct dahdi_pvt *tmp = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi set hwgain";
		e->usage =
			"Usage: dahdi set hwgain <rx|tx> <chan#> <gain>\n"
			"	Sets the hardware gain on a a given channel, overriding the\n"
			"   value provided at module loadtime, whether the channel is in\n"
			"   use or not.  Changes take effect immediately.\n"
			"   <rx|tx> which direction do you want to change (relative to our module)\n"
			"   <chan num> is the channel number relative to the device\n"
			"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 6)
		return CLI_SHOWUSAGE;

	if (!strcasecmp("rx", a->argv[3]))
		tx = 0; /* rx */
	else if (!strcasecmp("tx", a->argv[3]))
		tx = 1; /* tx */
	else
		return CLI_SHOWUSAGE;

	channel = atoi(a->argv[4]);
	gain = atof(a->argv[5])*10.0;

	ast_mutex_lock(&iflock);

	for (tmp = iflist; tmp; tmp = tmp->next) {

		if (tmp->channel != channel)
			continue;

		if (tmp->subs[SUB_REAL].dfd == -1)
			break;

		hwgain.newgain = gain;
		hwgain.tx = tx;
		if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_HWGAIN, &hwgain) < 0) {
			ast_cli(a->fd, "Unable to set the hardware gain for channel %d: %s\n", channel, strerror(errno));
			ast_mutex_unlock(&iflock);
			return CLI_FAILURE;
		}
		ast_cli(a->fd, "hardware %s gain set to %d (%.1f dB) on channel %d\n",
			tx ? "tx" : "rx", gain, (float)gain/10.0, channel);
		break;
	}

	ast_mutex_unlock(&iflock);

	if (tmp)
		return CLI_SUCCESS;

	ast_cli(a->fd, "Unable to find given channel %d\n", channel);
	return CLI_FAILURE;

}

static char *dahdi_set_swgain(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int channel;
	float gain;
	int tx;
	int res;
	ast_mutex_t *lock;
	struct dahdi_pvt *tmp = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi set swgain";
		e->usage =
			"Usage: dahdi set swgain <rx|tx> <chan#> <gain>\n"
			"	Sets the software gain on a a given channel, overriding the\n"
			"   value provided at module loadtime, whether the channel is in\n"
			"   use or not.  Changes take effect immediately.\n"
			"   <rx|tx> which direction do you want to change (relative to our module)\n"
			"   <chan num> is the channel number relative to the device\n"
			"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	lock = &iflock;

	if (a->argc != 6)
		return CLI_SHOWUSAGE;

	if (!strcasecmp("rx", a->argv[3]))
		tx = 0; /* rx */
	else if (!strcasecmp("tx", a->argv[3]))
		tx = 1; /* tx */
	else
		return CLI_SHOWUSAGE;

	channel = atoi(a->argv[4]);
	gain = atof(a->argv[5]);

	ast_mutex_lock(lock);
	for (tmp = iflist; tmp; tmp = tmp->next) {

		if (tmp->channel != channel)
			continue;

		if (tmp->subs[SUB_REAL].dfd == -1)
			break;

		if (tx)
			res = set_actual_txgain(tmp->subs[SUB_REAL].dfd, channel, gain, tmp->law);
		else
			res = set_actual_rxgain(tmp->subs[SUB_REAL].dfd, channel, gain, tmp->law);

		if (res) {
			ast_cli(a->fd, "Unable to set the software gain for channel %d\n", channel);
			ast_mutex_unlock(lock);
			return CLI_FAILURE;
		}

		ast_cli(a->fd, "software %s gain set to %.1f on channel %d\n",
			tx ? "tx" : "rx", gain, channel);
		break;
	}
	ast_mutex_unlock(lock);

	if (tmp)
		return CLI_SUCCESS;

	ast_cli(a->fd, "Unable to find given channel %d\n", channel);
	return CLI_FAILURE;

}

static char *dahdi_set_dnd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int channel;
	int on;
	struct dahdi_pvt *dahdi_chan = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi set dnd";
		e->usage =
			"Usage: dahdi set dnd <chan#> <on|off>\n"
			"	Sets/resets DND (Do Not Disturb) mode on a channel.\n"
			"	Changes take effect immediately.\n"
			"	<chan num> is the channel number\n"
			" 	<on|off> Enable or disable DND mode?\n"
			;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5)
		return CLI_SHOWUSAGE;

	if ((channel = atoi(a->argv[3])) <= 0) {
		ast_cli(a->fd, "Expected channel number, got '%s'\n", a->argv[3]);
		return CLI_SHOWUSAGE;
	}

	if (ast_true(a->argv[4]))
		on = 1;
	else if (ast_false(a->argv[4]))
		on = 0;
	else {
		ast_cli(a->fd, "Expected 'on' or 'off', got '%s'\n", a->argv[4]);
		return CLI_SHOWUSAGE;
	}

	ast_mutex_lock(&iflock);
	for (dahdi_chan = iflist; dahdi_chan; dahdi_chan = dahdi_chan->next) {
		if (dahdi_chan->channel != channel)
			continue;

		/* Found the channel. Actually set it */
		dahdi_dnd(dahdi_chan, on);
		break;
	}
	ast_mutex_unlock(&iflock);

	if (!dahdi_chan) {
		ast_cli(a->fd, "Unable to find given channel %d\n", channel);
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry dahdi_cli[] = {
	AST_CLI_DEFINE(handle_dahdi_show_cadences, "List cadences"),
	AST_CLI_DEFINE(dahdi_show_channels, "Show active DAHDI channels"),
	AST_CLI_DEFINE(dahdi_show_channel, "Show information on a channel"),
	AST_CLI_DEFINE(dahdi_destroy_channel, "Destroy a channel"),
	AST_CLI_DEFINE(dahdi_restart_cmd, "Fully restart DAHDI channels"),
	AST_CLI_DEFINE(dahdi_show_status, "Show all DAHDI cards status"),
	AST_CLI_DEFINE(dahdi_show_version, "Show the DAHDI version in use"),
	AST_CLI_DEFINE(dahdi_set_hwgain, "Set hardware gain on a channel"),
	AST_CLI_DEFINE(dahdi_set_swgain, "Set software gain on a channel"),
	AST_CLI_DEFINE(dahdi_set_dnd, "Sets/resets DND (Do Not Disturb) mode on a channel"),
};

#define TRANSFER	0
#define HANGUP		1

static int dahdi_fake_event(struct dahdi_pvt *p, int mode)
{
	if (p) {
		switch (mode) {
		case TRANSFER:
			p->fake_event = DAHDI_EVENT_WINKFLASH;
			break;
		case HANGUP:
			p->fake_event = DAHDI_EVENT_ONHOOK;
			break;
		default:
			ast_log(LOG_WARNING, "I don't know how to handle transfer event with this: %d on channel %s\n",mode, p->owner->name);
		}
	}
	return 0;
}
static struct dahdi_pvt *find_channel(int channel)
{
	struct dahdi_pvt *p = iflist;
	while (p) {
		if (p->channel == channel) {
			break;
		}
		p = p->next;
	}
	return p;
}

static int action_dahdidndon(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p = NULL;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	p->dnd = 1;
	astman_send_ack(s, m, "DND Enabled");
	return 0;
}

static int action_dahdidndoff(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p = NULL;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	p->dnd = 0;
	astman_send_ack(s, m, "DND Disabled");
	return 0;
}

static int action_transfer(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p = NULL;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	dahdi_fake_event(p,TRANSFER);
	astman_send_ack(s, m, "DAHDITransfer");
	return 0;
}

static int action_transferhangup(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p = NULL;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	dahdi_fake_event(p,HANGUP);
	astman_send_ack(s, m, "DAHDIHangup");
	return 0;
}

static int action_dahdidialoffhook(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p = NULL;
	const char *channel = astman_get_header(m, "DAHDIChannel");
	const char *number = astman_get_header(m, "Number");
	int i;

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (ast_strlen_zero(number)) {
		astman_send_error(s, m, "No number specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if (!p->owner) {
		astman_send_error(s, m, "Channel does not have it's owner");
		return 0;
	}
	for (i = 0; i < strlen(number); i++) {
		struct ast_frame f = { AST_FRAME_DTMF, number[i] };
		dahdi_queue_frame(p, &f, NULL);
	}
	astman_send_ack(s, m, "DAHDIDialOffhook");
	return 0;
}

static int action_dahdishowchannels(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *tmp = NULL;
	const char *id = astman_get_header(m, "ActionID");
	const char *dahdichannel = astman_get_header(m, "DAHDIChannel");
	char idText[256] = "";
	int channels = 0;
	int dahdichanquery = -1;
	if (!ast_strlen_zero(dahdichannel)) {
		dahdichanquery = atoi(dahdichannel);
	}

	astman_send_ack(s, m, "DAHDI channel status will follow");
	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	ast_mutex_lock(&iflock);

	tmp = iflist;
	while (tmp) {
		if (tmp->channel > 0) {
			int alm = get_alarms(tmp);

			/* If a specific channel is queried for, only deliver status for that channel */
			if (dahdichanquery > 0 && tmp->channel != dahdichanquery)
				continue;

			channels++;
			if (tmp->owner) {
				/* Add data if we have a current call */
				astman_append(s,
					"Event: DAHDIShowChannels\r\n"
					"DAHDIChannel: %d\r\n"
					"Channel: %s\r\n"
					"Uniqueid: %s\r\n"
					"AccountCode: %s\r\n"
					"Signalling: %s\r\n"
					"SignallingCode: %d\r\n"
					"Context: %s\r\n"
					"DND: %s\r\n"
					"Alarm: %s\r\n"
					"%s"
					"\r\n",
					tmp->channel,
					tmp->owner->name,
					tmp->owner->uniqueid,
					tmp->owner->accountcode,
					sig2str(tmp->sig),
					tmp->sig,
					tmp->context,
					tmp->dnd ? "Enabled" : "Disabled",
					alarm2str(alm), idText);
			} else {
				astman_append(s,
					"Event: DAHDIShowChannels\r\n"
					"DAHDIChannel: %d\r\n"
					"Signalling: %s\r\n"
					"SignallingCode: %d\r\n"
					"Context: %s\r\n"
					"DND: %s\r\n"
					"Alarm: %s\r\n"
					"%s"
					"\r\n",
					tmp->channel, sig2str(tmp->sig), tmp->sig,
					tmp->context,
					tmp->dnd ? "Enabled" : "Disabled",
					alarm2str(alm), idText);
			}
		}

		tmp = tmp->next;
	}

	ast_mutex_unlock(&iflock);

	astman_append(s,
		"Event: DAHDIShowChannelsComplete\r\n"
		"%s"
		"Items: %d\r\n"
		"\r\n",
		idText,
		channels);
	return 0;
}

#if defined(HAVE_SS7)
static int linkset_addsigchan(int sigchan)
{
	struct dahdi_ss7 *link;
	int res;
	int curfd;
	struct dahdi_params p;
	struct dahdi_bufferinfo bi;
	struct dahdi_spaninfo si;


	link = ss7_resolve_linkset(cur_linkset);
	if (!link) {
		ast_log(LOG_ERROR, "Invalid linkset number.  Must be between 1 and %d\n", NUM_SPANS + 1);
		return -1;
	}

	if (cur_ss7type < 0) {
		ast_log(LOG_ERROR, "Unspecified or invalid ss7type\n");
		return -1;
	}

	if (!link->ss7)
		link->ss7 = ss7_new(cur_ss7type);

	if (!link->ss7) {
		ast_log(LOG_ERROR, "Can't create new SS7!\n");
		return -1;
	}

	link->type = cur_ss7type;

	if (cur_pointcode < 0) {
		ast_log(LOG_ERROR, "Unspecified pointcode!\n");
		return -1;
	} else
		ss7_set_pc(link->ss7, cur_pointcode);

	if (sigchan < 0) {
		ast_log(LOG_ERROR, "Invalid sigchan!\n");
		return -1;
	} else {
		if (link->numsigchans >= NUM_DCHANS) {
			ast_log(LOG_ERROR, "Too many sigchans on linkset %d\n", cur_linkset);
			return -1;
		}
		curfd = link->numsigchans;

		link->fds[curfd] = open("/dev/dahdi/channel", O_RDWR, 0600);
		if ((link->fds[curfd] < 0) || (ioctl(link->fds[curfd],DAHDI_SPECIFY,&sigchan) == -1)) {
			ast_log(LOG_ERROR, "Unable to open SS7 sigchan %d (%s)\n", sigchan, strerror(errno));
			return -1;
		}
		memset(&p, 0, sizeof(p));
		res = ioctl(link->fds[curfd], DAHDI_GET_PARAMS, &p);
		if (res) {
			dahdi_close_ss7_fd(link, curfd);
			ast_log(LOG_ERROR, "Unable to get parameters for sigchan %d (%s)\n", sigchan, strerror(errno));
			return -1;
		}
		if ((p.sigtype != DAHDI_SIG_HDLCFCS) && (p.sigtype != DAHDI_SIG_HARDHDLC) && (p.sigtype != DAHDI_SIG_MTP2)) {
			dahdi_close_ss7_fd(link, curfd);
			ast_log(LOG_ERROR, "sigchan %d is not in HDLC/FCS mode.\n", sigchan);
			return -1;
		}

		memset(&bi, 0, sizeof(bi));
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = 32;
		bi.bufsize = 512;

		if (ioctl(link->fds[curfd], DAHDI_SET_BUFINFO, &bi)) {
			ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d: %s\n", sigchan, strerror(errno));
			dahdi_close_ss7_fd(link, curfd);
			return -1;
		}

		if (p.sigtype == DAHDI_SIG_MTP2)
			ss7_add_link(link->ss7, SS7_TRANSPORT_DAHDIMTP2, link->fds[curfd]);
		else
			ss7_add_link(link->ss7, SS7_TRANSPORT_DAHDIDCHAN, link->fds[curfd]);

		link->numsigchans++;

		memset(&si, 0, sizeof(si));
		res = ioctl(link->fds[curfd], DAHDI_SPANSTAT, &si);
		if (res) {
			dahdi_close_ss7_fd(link, curfd);
			ast_log(LOG_ERROR, "Unable to get span state for sigchan %d (%s)\n", sigchan, strerror(errno));
		}

		if (!si.alarms) {
			link->linkstate[curfd] = LINKSTATE_DOWN;
			ss7_link_noalarm(link->ss7, link->fds[curfd]);
		} else {
			link->linkstate[curfd] = LINKSTATE_DOWN | LINKSTATE_INALARM;
			ss7_link_alarm(link->ss7, link->fds[curfd]);
		}
	}

	if (cur_adjpointcode < 0) {
		ast_log(LOG_ERROR, "Unspecified adjpointcode!\n");
		return -1;
	} else {
		ss7_set_adjpc(link->ss7, link->fds[curfd], cur_adjpointcode);
	}

	if (cur_defaultdpc < 0) {
		ast_log(LOG_ERROR, "Unspecified defaultdpc!\n");
		return -1;
	}

	if (cur_networkindicator < 0) {
		ast_log(LOG_ERROR, "Invalid networkindicator!\n");
		return -1;
	} else
		ss7_set_network_ind(link->ss7, cur_networkindicator);

	return 0;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 set debug {on|off} linkset";
		e->usage =
			"Usage: ss7 set debug {on|off} linkset <linkset>\n"
			"       Enables debugging on a given SS7 linkset\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 6)
		return CLI_SHOWUSAGE;
	span = atoi(a->argv[5]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid linkset %s.  Should be a number from %d to %d\n", a->argv[5], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!linksets[span-1].ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", span);
		return CLI_SUCCESS;
	}
	if (linksets[span-1].ss7) {
		if (strcasecmp(a->argv[3], "on")) {
			ss7_set_debug(linksets[span-1].ss7, SS7_DEBUG_MTP2 | SS7_DEBUG_MTP3 | SS7_DEBUG_ISUP);
			ast_cli(a->fd, "Enabled debugging on linkset %d\n", span);
		} else {
			ss7_set_debug(linksets[span-1].ss7, 0);
			ast_cli(a->fd, "Disabled debugging on linkset %d\n", span);
		}
	}

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_block_cic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset, cic;
	int blocked = -1, i;
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 block cic";
		e->usage =
			"Usage: ss7 block cic <linkset> <CIC>\n"
			"       Sends a remote blocking request for the given CIC on the specified linkset\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc == 5)
		linkset = atoi(a->argv[3]);
	else
		return CLI_SHOWUSAGE;

	if ((linkset < 1) || (linkset > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid linkset %s.  Should be a number %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}

	if (!linksets[linkset-1].ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	cic = atoi(a->argv[4]);

	if (cic < 1) {
		ast_cli(a->fd, "Invalid CIC specified!\n");
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].numchans; i++) {
		if (linksets[linkset-1].pvts[i]->cic == cic) {
			blocked = linksets[linkset-1].pvts[i]->locallyblocked;
			if (!blocked) {
				ast_mutex_lock(&linksets[linkset-1].lock);
				isup_blo(linksets[linkset-1].ss7, cic, linksets[linkset-1].pvts[i]->dpc);
				ast_mutex_unlock(&linksets[linkset-1].lock);
			}
		}
	}

	if (blocked < 0) {
		ast_cli(a->fd, "Invalid CIC specified!\n");
		return CLI_SUCCESS;
	}

	if (!blocked)
		ast_cli(a->fd, "Sent blocking request for linkset %d on CIC %d\n", linkset, cic);
	else
		ast_cli(a->fd, "CIC %d already locally blocked\n", cic);

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].master, SIGURG);

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_block_linkset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset;
	int i;
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 block linkset";
		e->usage =
			"Usage: ss7 block linkset <linkset number>\n"
			"       Sends a remote blocking request for all CICs on the given linkset\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc == 4)
		linkset = atoi(a->argv[3]);
	else
		return CLI_SHOWUSAGE;

	if ((linkset < 1) || (linkset > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid linkset %s.  Should be a number %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}

	if (!linksets[linkset-1].ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].numchans; i++) {
		ast_cli(a->fd, "Sending remote blocking request on CIC %d\n", linksets[linkset-1].pvts[i]->cic);
		ast_mutex_lock(&linksets[linkset-1].lock);
		isup_blo(linksets[linkset-1].ss7, linksets[linkset-1].pvts[i]->cic, linksets[linkset-1].pvts[i]->dpc);
		ast_mutex_unlock(&linksets[linkset-1].lock);
	}

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].master, SIGURG);

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_unblock_cic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset, cic;
	int i, blocked = -1;
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 unblock cic";
		e->usage =
			"Usage: ss7 unblock cic <linkset> <CIC>\n"
			"       Sends a remote unblocking request for the given CIC on the specified linkset\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 5)
		linkset = atoi(a->argv[3]);
	else
		return CLI_SHOWUSAGE;

	if ((linkset < 1) || (linkset > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid linkset %s.  Should be a number %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}

	if (!linksets[linkset-1].ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	cic = atoi(a->argv[4]);

	if (cic < 1) {
		ast_cli(a->fd, "Invalid CIC specified!\n");
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].numchans; i++) {
		if (linksets[linkset-1].pvts[i]->cic == cic) {
			blocked = linksets[linkset-1].pvts[i]->locallyblocked;
			if (blocked) {
				ast_mutex_lock(&linksets[linkset-1].lock);
				isup_ubl(linksets[linkset-1].ss7, cic, linksets[linkset-1].pvts[i]->dpc);
				ast_mutex_unlock(&linksets[linkset-1].lock);
			}
		}
	}

	if (blocked > 0)
		ast_cli(a->fd, "Sent unblocking request for linkset %d on CIC %d\n", linkset, cic);

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].master, SIGURG);

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_unblock_linkset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset;
	int i;
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 unblock linkset";
		e->usage =
			"Usage: ss7 unblock linkset <linkset number>\n"
			"       Sends a remote unblocking request for all CICs on the specified linkset\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 4)
		linkset = atoi(a->argv[3]);
	else
		return CLI_SHOWUSAGE;

	if ((linkset < 1) || (linkset > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid linkset %s.  Should be a number %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}

	if (!linksets[linkset-1].ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].numchans; i++) {
		ast_cli(a->fd, "Sending remote unblock request on CIC %d\n", linksets[linkset-1].pvts[i]->cic);
		ast_mutex_lock(&linksets[linkset-1].lock);
		isup_ubl(linksets[linkset-1].ss7, linksets[linkset-1].pvts[i]->cic, linksets[linkset-1].pvts[i]->dpc);
		ast_mutex_unlock(&linksets[linkset-1].lock);
	}

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].master, SIGURG);

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_show_linkset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset;
	struct dahdi_ss7 *ss7;
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 show linkset";
		e->usage =
			"Usage: ss7 show linkset <span>\n"
			"       Shows the status of an SS7 linkset.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;
	linkset = atoi(a->argv[3]);
	if ((linkset < 1) || (linkset > NUM_SPANS)) {
		ast_cli(a->fd, "Invalid linkset %s.  Should be a number %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!linksets[linkset-1].ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}
	if (linksets[linkset-1].ss7)
		ss7 = &linksets[linkset-1];

	ast_cli(a->fd, "SS7 linkset %d status: %s\n", linkset, (ss7->state == LINKSET_STATE_UP) ? "Up" : "Down");

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 show version";
		e->usage =
			"Usage: ss7 show version\n"
			"	Show the libss7 version\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "libss7 version: %s\n", ss7_get_version());

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static struct ast_cli_entry dahdi_ss7_cli[] = {
	AST_CLI_DEFINE(handle_ss7_debug, "Enables SS7 debugging on a linkset"),
	AST_CLI_DEFINE(handle_ss7_block_cic, "Blocks the given CIC"),
	AST_CLI_DEFINE(handle_ss7_unblock_cic, "Unblocks the given CIC"),
	AST_CLI_DEFINE(handle_ss7_block_linkset, "Blocks all CICs on a linkset"),
	AST_CLI_DEFINE(handle_ss7_unblock_linkset, "Unblocks all CICs on a linkset"),
	AST_CLI_DEFINE(handle_ss7_show_linkset, "Shows the status of a linkset"),
	AST_CLI_DEFINE(handle_ss7_version, "Displays libss7 version"),
};
#endif	/* defined(HAVE_SS7) */

static int __unload_module(void)
{
	struct dahdi_pvt *p;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int i, j;
#endif

#ifdef HAVE_PRI
	for (i = 0; i < NUM_SPANS; i++) {
		if (pris[i].pri.master != AST_PTHREADT_NULL)
			pthread_cancel(pris[i].pri.master);
	}
	ast_cli_unregister_multiple(dahdi_pri_cli, ARRAY_LEN(dahdi_pri_cli));
	ast_unregister_application(dahdi_send_keypad_facility_app);
#ifdef HAVE_PRI_PROG_W_CAUSE
	ast_unregister_application(dahdi_send_callrerouting_facility_app);
#endif
#endif
#if defined(HAVE_SS7)
	for (i = 0; i < NUM_SPANS; i++) {
		if (linksets[i].master != AST_PTHREADT_NULL)
			pthread_cancel(linksets[i].master);
		}
	ast_cli_unregister_multiple(dahdi_ss7_cli, ARRAY_LEN(dahdi_ss7_cli));
#endif
#if defined(HAVE_OPENR2)
	dahdi_r2_destroy_links();
	ast_cli_unregister_multiple(dahdi_mfcr2_cli, ARRAY_LEN(dahdi_mfcr2_cli));
	ast_unregister_application(dahdi_accept_r2_call_app);
#endif

	ast_cli_unregister_multiple(dahdi_cli, ARRAY_LEN(dahdi_cli));
	ast_manager_unregister("DAHDIDialOffhook");
	ast_manager_unregister("DAHDIHangup");
	ast_manager_unregister("DAHDITransfer");
	ast_manager_unregister("DAHDIDNDoff");
	ast_manager_unregister("DAHDIDNDon");
	ast_manager_unregister("DAHDIShowChannels");
	ast_manager_unregister("DAHDIRestart");
	ast_channel_unregister(&dahdi_tech);
	ast_mutex_lock(&iflock);
	/* Hangup all interfaces if they have an owner */
	p = iflist;
	while (p) {
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
		p = p->next;
	}
	ast_mutex_unlock(&iflock);
	ast_mutex_lock(&monlock);
	if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL)) {
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}
	monitor_thread = AST_PTHREADT_STOP;
	ast_mutex_unlock(&monlock);

	destroy_all_channels();

#if defined(HAVE_PRI)
	for (i = 0; i < NUM_SPANS; i++) {
		if (pris[i].pri.master && (pris[i].pri.master != AST_PTHREADT_NULL))
			pthread_join(pris[i].pri.master, NULL);
		for (j = 0; j < NUM_DCHANS; j++) {
			dahdi_close_pri_fd(&(pris[i]), j);
		}
	}
#endif

#if defined(HAVE_SS7)
	for (i = 0; i < NUM_SPANS; i++) {
		if (linksets[i].master && (linksets[i].master != AST_PTHREADT_NULL))
			pthread_join(linksets[i].master, NULL);
		for (j = 0; j < NUM_DCHANS; j++) {
			dahdi_close_ss7_fd(&(linksets[i]), j);
		}
	}
#endif
	ast_cond_destroy(&ss_thread_complete);
	return 0;
}

static int unload_module(void)
{
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int y;
#endif
#ifdef HAVE_PRI
	for (y = 0; y < NUM_SPANS; y++)
		ast_mutex_destroy(&pris[y].pri.lock);
#endif
#ifdef HAVE_SS7
	for (y = 0; y < NUM_SPANS; y++)
		ast_mutex_destroy(&linksets[y].lock);
#endif /* HAVE_SS7 */
	return __unload_module();
}

static int build_channels(struct dahdi_chan_conf *conf, const char *value, int reload, int lineno, int *found_pseudo)
{
	char *c, *chan;
	int x, start, finish;
	struct dahdi_pvt *tmp;

	if ((reload == 0) && (conf->chan.sig < 0) && !conf->is_sig_auto) {
		ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
		return -1;
	}

	c = ast_strdupa(value);

	while ((chan = strsep(&c, ","))) {
		if (sscanf(chan, "%d-%d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(chan, "%d", &start)) {
			/* Just one */
			finish = start;
		} else if (!strcasecmp(chan, "pseudo")) {
			finish = start = CHAN_PSEUDO;
			if (found_pseudo)
				*found_pseudo = 1;
		} else {
			ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", value, chan);
			return -1;
		}
		if (finish < start) {
			ast_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
			x = finish;
			finish = start;
			start = x;
		}

		for (x = start; x <= finish; x++) {
			tmp = mkintf(x, conf, reload);

			if (tmp) {
				ast_verb(3, "%s channel %d, %s signalling\n", reload ? "Reconfigured" : "Registered", x, sig2str(tmp->sig));
			} else {
				ast_log(LOG_ERROR, "Unable to %s channel '%s'\n",
					(reload == 1) ? "reconfigure" : "register", value);
				return -1;
			}
		}
	}

	return 0;
}

/** The length of the parameters list of 'dahdichan'.
 * \todo Move definition of MAX_CHANLIST_LEN to a proper place. */
#define MAX_CHANLIST_LEN 80

static void process_echocancel(struct dahdi_chan_conf *confp, const char *data, unsigned int line)
{
	char *parse = ast_strdupa(data);
	char *params[DAHDI_MAX_ECHOCANPARAMS + 1];
	unsigned int param_count;
	unsigned int x;

	if (!(param_count = ast_app_separate_args(parse, ',', params, ARRAY_LEN(params))))
		return;

	memset(&confp->chan.echocancel, 0, sizeof(confp->chan.echocancel));

	/* first parameter is tap length, process it here */

	x = ast_strlen_zero(params[0]) ? 0 : atoi(params[0]);

	if ((x == 32) || (x == 64) || (x == 128) || (x == 256) || (x == 512) || (x == 1024))
		confp->chan.echocancel.head.tap_length = x;
	else if ((confp->chan.echocancel.head.tap_length = ast_true(params[0])))
		confp->chan.echocancel.head.tap_length = 128;

	/* now process any remaining parameters */

	for (x = 1; x < param_count; x++) {
		struct {
			char *name;
			char *value;
		} param;

		if (ast_app_separate_args(params[x], '=', (char **) &param, 2) < 1) {
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %d: '%s'\n", line, params[x]);
			continue;
		}

		if (ast_strlen_zero(param.name) || (strlen(param.name) > sizeof(confp->chan.echocancel.params[0].name)-1)) {
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %d: '%s'\n", line, param.name);
			continue;
		}

		strcpy(confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].name, param.name);

		if (param.value) {
			if (sscanf(param.value, "%d", &confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].value) != 1) {
				ast_log(LOG_WARNING, "Invalid echocancel parameter value supplied at line %d: '%s'\n", line, param.value);
				continue;
			}
		}
		confp->chan.echocancel.head.param_count++;
	}
}

/*! process_dahdi() - ignore keyword 'channel' and similar */
#define PROC_DAHDI_OPT_NOCHAN  (1 << 0)
/*! process_dahdi() - No warnings on non-existing cofiguration keywords */
#define PROC_DAHDI_OPT_NOWARN  (1 << 1)

static int process_dahdi(struct dahdi_chan_conf *confp, const char *cat, struct ast_variable *v, int reload, int options)
{
	struct dahdi_pvt *tmp;
	int y;
	int found_pseudo = 0;
	char dahdichan[MAX_CHANLIST_LEN] = {};

	for (; v; v = v->next) {
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;

		/* must have parkinglot in confp before build_channels is called */
		if (!strcasecmp(v->name, "parkinglot")) {
			ast_copy_string(confp->chan.parkinglot, v->value, sizeof(confp->chan.parkinglot));
		}

		/* Create the interface list */
		if (!strcasecmp(v->name, "channel")) {
 			if (options & PROC_DAHDI_OPT_NOCHAN) {
				ast_log(LOG_WARNING, "Channel '%s' ignored.\n", v->value);
 				continue;
			}
 			if (build_channels(confp, v->value, reload, v->lineno, &found_pseudo))
 					return -1;
			ast_log(LOG_DEBUG, "Channel '%s' configured.\n", v->value);
		} else if (!strcasecmp(v->name, "buffers")) {
			if (parse_buffers_policy(v->value, &confp->chan.buf_no, &confp->chan.buf_policy)) {
				ast_log(LOG_WARNING, "Using default buffer policy.\n");
				confp->chan.buf_no = numbufs;
				confp->chan.buf_policy = DAHDI_POLICY_IMMEDIATE;
			}
		} else if (!strcasecmp(v->name, "faxbuffers")) {
			if (!parse_buffers_policy(v->value, &confp->chan.faxbuf_no, &confp->chan.faxbuf_policy)) {
				confp->chan.usefaxbuffers = 1;
			}
 		} else if (!strcasecmp(v->name, "dahdichan")) {
 			ast_copy_string(dahdichan, v->value, sizeof(dahdichan));
		} else if (!strcasecmp(v->name, "usedistinctiveringdetection")) {
			usedistinctiveringdetection = ast_true(v->value);
		} else if (!strcasecmp(v->name, "distinctiveringaftercid")) {
			distinctiveringaftercid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "dring1context")) {
			ast_copy_string(confp->chan.drings.ringContext[0].contextData,v->value,sizeof(confp->chan.drings.ringContext[0].contextData));
		} else if (!strcasecmp(v->name, "dring2context")) {
			ast_copy_string(confp->chan.drings.ringContext[1].contextData,v->value,sizeof(confp->chan.drings.ringContext[1].contextData));
		} else if (!strcasecmp(v->name, "dring3context")) {
			ast_copy_string(confp->chan.drings.ringContext[2].contextData,v->value,sizeof(confp->chan.drings.ringContext[2].contextData));
		} else if (!strcasecmp(v->name, "dring1range")) {
			confp->chan.drings.ringnum[0].range = atoi(v->value);
		} else if (!strcasecmp(v->name, "dring2range")) {
			confp->chan.drings.ringnum[1].range = atoi(v->value);
		} else if (!strcasecmp(v->name, "dring3range")) {
			confp->chan.drings.ringnum[2].range = atoi(v->value);
		} else if (!strcasecmp(v->name, "dring1")) {
			sscanf(v->value, "%d,%d,%d", &confp->chan.drings.ringnum[0].ring[0], &confp->chan.drings.ringnum[0].ring[1], &confp->chan.drings.ringnum[0].ring[2]);
		} else if (!strcasecmp(v->name, "dring2")) {
			sscanf(v->value,"%d,%d,%d", &confp->chan.drings.ringnum[1].ring[0], &confp->chan.drings.ringnum[1].ring[1], &confp->chan.drings.ringnum[1].ring[2]);
		} else if (!strcasecmp(v->name, "dring3")) {
			sscanf(v->value, "%d,%d,%d", &confp->chan.drings.ringnum[2].ring[0], &confp->chan.drings.ringnum[2].ring[1], &confp->chan.drings.ringnum[2].ring[2]);
		} else if (!strcasecmp(v->name, "usecallerid")) {
			confp->chan.use_callerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "cidsignalling")) {
			if (!strcasecmp(v->value, "bell"))
				confp->chan.cid_signalling = CID_SIG_BELL;
			else if (!strcasecmp(v->value, "v23"))
				confp->chan.cid_signalling = CID_SIG_V23;
			else if (!strcasecmp(v->value, "dtmf"))
				confp->chan.cid_signalling = CID_SIG_DTMF;
			else if (!strcasecmp(v->value, "smdi"))
				confp->chan.cid_signalling = CID_SIG_SMDI;
			else if (!strcasecmp(v->value, "v23_jp"))
				confp->chan.cid_signalling = CID_SIG_V23_JP;
			else if (ast_true(v->value))
				confp->chan.cid_signalling = CID_SIG_BELL;
		} else if (!strcasecmp(v->name, "cidstart")) {
			if (!strcasecmp(v->value, "ring"))
				confp->chan.cid_start = CID_START_RING;
			else if (!strcasecmp(v->value, "polarity_in"))
				confp->chan.cid_start = CID_START_POLARITY_IN;
			else if (!strcasecmp(v->value, "polarity"))
				confp->chan.cid_start = CID_START_POLARITY;
			else if (ast_true(v->value))
				confp->chan.cid_start = CID_START_RING;
		} else if (!strcasecmp(v->name, "threewaycalling")) {
			confp->chan.threewaycalling = ast_true(v->value);
		} else if (!strcasecmp(v->name, "cancallforward")) {
			confp->chan.cancallforward = ast_true(v->value);
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			if (ast_true(v->value))
				confp->chan.dtmfrelax = DSP_DIGITMODE_RELAXDTMF;
			else
				confp->chan.dtmfrelax = 0;
		} else if (!strcasecmp(v->name, "mailbox")) {
			ast_copy_string(confp->chan.mailbox, v->value, sizeof(confp->chan.mailbox));
		} else if (!strcasecmp(v->name, "hasvoicemail")) {
			if (ast_true(v->value) && ast_strlen_zero(confp->chan.mailbox)) {
				ast_copy_string(confp->chan.mailbox, cat, sizeof(confp->chan.mailbox));
			}
		} else if (!strcasecmp(v->name, "adsi")) {
			confp->chan.adsi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "usesmdi")) {
			confp->chan.use_smdi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "smdiport")) {
			ast_copy_string(confp->smdi_port, v->value, sizeof(confp->smdi_port));
		} else if (!strcasecmp(v->name, "transfer")) {
			confp->chan.transfer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "canpark")) {
			confp->chan.canpark = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancelwhenbridged")) {
			confp->chan.echocanbridged = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busydetect")) {
			confp->chan.busydetect = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busycount")) {
			confp->chan.busycount = atoi(v->value);
		} else if (!strcasecmp(v->name, "busypattern")) {
			if (sscanf(v->value, "%d,%d", &confp->chan.busy_tonelength, &confp->chan.busy_quietlength) != 2) {
				ast_log(LOG_ERROR, "busypattern= expects busypattern=tonelength,quietlength at line %d.\n", v->lineno);
			}
		} else if (!strcasecmp(v->name, "callprogress")) {
			confp->chan.callprogress &= ~CALLPROGRESS_PROGRESS;
			if (ast_true(v->value))
				confp->chan.callprogress |= CALLPROGRESS_PROGRESS;
		} else if (!strcasecmp(v->name, "waitfordialtone")) {
			confp->chan.waitfordialtone = atoi(v->value);
		} else if (!strcasecmp(v->name, "faxdetect")) {
			confp->chan.callprogress &= ~CALLPROGRESS_FAX;
			if (!strcasecmp(v->value, "incoming")) {
				confp->chan.callprogress |= CALLPROGRESS_FAX_INCOMING;
			} else if (!strcasecmp(v->value, "outgoing")) {
				confp->chan.callprogress |= CALLPROGRESS_FAX_OUTGOING;
			} else if (!strcasecmp(v->value, "both") || ast_true(v->value))
				confp->chan.callprogress |= CALLPROGRESS_FAX_INCOMING | CALLPROGRESS_FAX_OUTGOING;
		} else if (!strcasecmp(v->name, "echocancel")) {
			process_echocancel(confp, v->value, v->lineno);
		} else if (!strcasecmp(v->name, "echotraining")) {
			if (sscanf(v->value, "%d", &y) == 1) {
				if ((y < 10) || (y > 4000)) {
					ast_log(LOG_WARNING, "Echo training time must be within the range of 10 to 4000 ms at line %d.\n", v->lineno);
				} else {
					confp->chan.echotraining = y;
				}
			} else if (ast_true(v->value)) {
				confp->chan.echotraining = 400;
			} else
				confp->chan.echotraining = 0;
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			confp->chan.hidecallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hidecalleridname")) {
			confp->chan.hidecalleridname = ast_true(v->value);
 		} else if (!strcasecmp(v->name, "pulsedial")) {
 			confp->chan.pulse = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callreturn")) {
			confp->chan.callreturn = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			confp->chan.callwaiting = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			confp->chan.callwaitingcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(confp->chan.context, v->value, sizeof(confp->chan.context));
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(confp->chan.language, v->value, sizeof(confp->chan.language));
		} else if (!strcasecmp(v->name, "progzone")) {
			ast_copy_string(progzone, v->value, sizeof(progzone));
		} else if (!strcasecmp(v->name, "mohinterpret")
			||!strcasecmp(v->name, "musiconhold") || !strcasecmp(v->name, "musicclass")) {
			ast_copy_string(confp->chan.mohinterpret, v->value, sizeof(confp->chan.mohinterpret));
		} else if (!strcasecmp(v->name, "mohsuggest")) {
			ast_copy_string(confp->chan.mohsuggest, v->value, sizeof(confp->chan.mohsuggest));
		} else if (!strcasecmp(v->name, "parkinglot")) {
			ast_copy_string(parkinglot, v->value, sizeof(parkinglot));
		} else if (!strcasecmp(v->name, "stripmsd")) {
			ast_log(LOG_NOTICE, "Configuration option \"%s\" has been deprecated. Please use dialplan instead\n", v->name);
			confp->chan.stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "jitterbuffers")) {
			numbufs = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			confp->chan.group = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "callgroup")) {
			if (!strcasecmp(v->value, "none"))
				confp->chan.callgroup = 0;
			else
				confp->chan.callgroup = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "pickupgroup")) {
			if (!strcasecmp(v->value, "none"))
				confp->chan.pickupgroup = 0;
			else
				confp->chan.pickupgroup = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "setvar")) {
			char *varname = ast_strdupa(v->value), *varval = NULL;
			struct ast_variable *tmpvar;
			if (varname && (varval = strchr(varname, '='))) {
				*varval++ = '\0';
				if ((tmpvar = ast_variable_new(varname, varval, ""))) {
					tmpvar->next = confp->chan.vars;
					confp->chan.vars = tmpvar;
				}
			}
		} else if (!strcasecmp(v->name, "immediate")) {
			confp->chan.immediate = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfertobusy")) {
			confp->chan.transfertobusy = ast_true(v->value);
		} else if (!strcasecmp(v->name, "mwimonitor")) {
			confp->chan.mwimonitor_neon = 0;
			confp->chan.mwimonitor_fsk = 0;
			confp->chan.mwimonitor_rpas = 0;
			if (strcasestr(v->value, "fsk")) {
				confp->chan.mwimonitor_fsk = 1;
			}
			if (strcasestr(v->value, "rpas")) {
				confp->chan.mwimonitor_rpas = 1;
			}
			if (strcasestr(v->value, "neon")) {
				confp->chan.mwimonitor_neon = 1;
			}
			/* If set to true or yes, assume that simple fsk is desired */
			if (ast_true(v->value)) {
				confp->chan.mwimonitor_fsk = 1;
			}
		} else if (!strcasecmp(v->name, "cid_rxgain")) {
			if (sscanf(v->value, "%f", &confp->chan.cid_rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid cid_rxgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%f", &confp->chan.rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid rxgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%f", &confp->chan.txgain) != 1) {
				ast_log(LOG_WARNING, "Invalid txgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "tonezone")) {
			if (sscanf(v->value, "%d", &confp->chan.tonezone) != 1) {
				ast_log(LOG_WARNING, "Invalid tonezone: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived")) {
				confp->chan.cid_num[0] = '\0';
				confp->chan.cid_name[0] = '\0';
			} else {
				ast_callerid_split(v->value, confp->chan.cid_name, sizeof(confp->chan.cid_name), confp->chan.cid_num, sizeof(confp->chan.cid_num));
			}
		} else if (!strcasecmp(v->name, "fullname")) {
			ast_copy_string(confp->chan.cid_name, v->value, sizeof(confp->chan.cid_name));
		} else if (!strcasecmp(v->name, "cid_number")) {
			ast_copy_string(confp->chan.cid_num, v->value, sizeof(confp->chan.cid_num));
		} else if (!strcasecmp(v->name, "useincomingcalleridondahditransfer")) {
			confp->chan.dahditrcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "restrictcid")) {
			confp->chan.restrictcid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "usecallingpres")) {
			confp->chan.use_callingpres = ast_true(v->value);
		} else if (!strcasecmp(v->name, "accountcode")) {
			ast_copy_string(confp->chan.accountcode, v->value, sizeof(confp->chan.accountcode));
		} else if (!strcasecmp(v->name, "amaflags")) {
			y = ast_cdr_amaflags2int(v->value);
			if (y < 0)
				ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d.\n", v->value, v->lineno);
			else
				confp->chan.amaflags = y;
		} else if (!strcasecmp(v->name, "polarityonanswerdelay")) {
			confp->chan.polarityonanswerdelay = atoi(v->value);
		} else if (!strcasecmp(v->name, "answeronpolarityswitch")) {
			confp->chan.answeronpolarityswitch = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hanguponpolarityswitch")) {
			confp->chan.hanguponpolarityswitch = ast_true(v->value);
		} else if (!strcasecmp(v->name, "sendcalleridafter")) {
			confp->chan.sendcalleridafter = atoi(v->value);
		} else if (!strcasecmp(v->name, "mwimonitornotify")) {
			ast_copy_string(mwimonitornotify, v->value, sizeof(mwimonitornotify));
		} else if (!strcasecmp(v->name, "mwisendtype")) {
#ifndef HAVE_DAHDI_LINEREVERSE_VMWI  /* backward compatibility for older dahdi VMWI implementation */
			if (!strcasecmp(v->value, "rpas")) { /* Ring Pulse Alert Signal */
				mwisend_rpas = 1;
			} else {
				mwisend_rpas = 0;
			}
#else
			/* Default is fsk, to turn it off you must specify nofsk */
			memset(&confp->chan.mwisend_setting, 0, sizeof(confp->chan.mwisend_setting));
			if (strcasestr(v->value, "nofsk")) { 		/* NoFSK */
				confp->chan.mwisend_fsk = 0;
			} else {					/* Default FSK */
				confp->chan.mwisend_fsk = 1;
			}
			if (strcasestr(v->value, "rpas")) { 		/* Ring Pulse Alert Signal, normally followed by FSK */
				confp->chan.mwisend_rpas = 1;
			} else {
				confp->chan.mwisend_rpas = 0;
			}
			if (strcasestr(v->value, "lrev")) { 		/* Line Reversal */
				confp->chan.mwisend_setting.vmwi_type |= DAHDI_VMWI_LREV;
			}
			if (strcasestr(v->value, "hvdc")) { 		/* HV 90VDC */
				confp->chan.mwisend_setting.vmwi_type |= DAHDI_VMWI_HVDC;
			}
			if ( (strcasestr(v->value, "neon")) || (strcasestr(v->value, "hvac")) ){ 	/* 90V DC pulses */
				confp->chan.mwisend_setting.vmwi_type |= DAHDI_VMWI_HVAC;
			}
#endif
		} else if (reload != 1) {
			 if (!strcasecmp(v->name, "signalling") || !strcasecmp(v->name, "signaling")) {
				int orig_radio = confp->chan.radio;
				int orig_outsigmod = confp->chan.outsigmod;
				int orig_auto = confp->is_sig_auto;

				confp->chan.radio = 0;
				confp->chan.outsigmod = -1;
				confp->is_sig_auto = 0;
				if (!strcasecmp(v->value, "em")) {
					confp->chan.sig = SIG_EM;
				} else if (!strcasecmp(v->value, "em_e1")) {
					confp->chan.sig = SIG_EM_E1;
				} else if (!strcasecmp(v->value, "em_w")) {
					confp->chan.sig = SIG_EMWINK;
				} else if (!strcasecmp(v->value, "fxs_ls")) {
					confp->chan.sig = SIG_FXSLS;
				} else if (!strcasecmp(v->value, "fxs_gs")) {
					confp->chan.sig = SIG_FXSGS;
				} else if (!strcasecmp(v->value, "fxs_ks")) {
					confp->chan.sig = SIG_FXSKS;
				} else if (!strcasecmp(v->value, "fxo_ls")) {
					confp->chan.sig = SIG_FXOLS;
				} else if (!strcasecmp(v->value, "fxo_gs")) {
					confp->chan.sig = SIG_FXOGS;
				} else if (!strcasecmp(v->value, "fxo_ks")) {
					confp->chan.sig = SIG_FXOKS;
				} else if (!strcasecmp(v->value, "fxs_rx")) {
					confp->chan.sig = SIG_FXSKS;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "fxo_rx")) {
					confp->chan.sig = SIG_FXOLS;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "fxs_tx")) {
					confp->chan.sig = SIG_FXSLS;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "fxo_tx")) {
					confp->chan.sig = SIG_FXOGS;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "em_rx")) {
					confp->chan.sig = SIG_EM;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "em_tx")) {
					confp->chan.sig = SIG_EM;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "em_rxtx")) {
					confp->chan.sig = SIG_EM;
					confp->chan.radio = 2;
				} else if (!strcasecmp(v->value, "em_txrx")) {
					confp->chan.sig = SIG_EM;
					confp->chan.radio = 2;
				} else if (!strcasecmp(v->value, "sf")) {
					confp->chan.sig = SIG_SF;
				} else if (!strcasecmp(v->value, "sf_w")) {
					confp->chan.sig = SIG_SFWINK;
				} else if (!strcasecmp(v->value, "sf_featd")) {
					confp->chan.sig = SIG_FEATD;
				} else if (!strcasecmp(v->value, "sf_featdmf")) {
					confp->chan.sig = SIG_FEATDMF;
				} else if (!strcasecmp(v->value, "sf_featb")) {
					confp->chan.sig = SIG_SF_FEATB;
				} else if (!strcasecmp(v->value, "sf")) {
					confp->chan.sig = SIG_SF;
				} else if (!strcasecmp(v->value, "sf_rx")) {
					confp->chan.sig = SIG_SF;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "sf_tx")) {
					confp->chan.sig = SIG_SF;
					confp->chan.radio = 1;
				} else if (!strcasecmp(v->value, "sf_rxtx")) {
					confp->chan.sig = SIG_SF;
					confp->chan.radio = 2;
				} else if (!strcasecmp(v->value, "sf_txrx")) {
					confp->chan.sig = SIG_SF;
					confp->chan.radio = 2;
				} else if (!strcasecmp(v->value, "featd")) {
					confp->chan.sig = SIG_FEATD;
				} else if (!strcasecmp(v->value, "featdmf")) {
					confp->chan.sig = SIG_FEATDMF;
				} else if (!strcasecmp(v->value, "featdmf_ta")) {
					confp->chan.sig = SIG_FEATDMF_TA;
				} else if (!strcasecmp(v->value, "e911")) {
					confp->chan.sig = SIG_E911;
				} else if (!strcasecmp(v->value, "fgccama")) {
					confp->chan.sig = SIG_FGC_CAMA;
				} else if (!strcasecmp(v->value, "fgccamamf")) {
					confp->chan.sig = SIG_FGC_CAMAMF;
				} else if (!strcasecmp(v->value, "featb")) {
					confp->chan.sig = SIG_FEATB;
#ifdef HAVE_PRI
				} else if (!strcasecmp(v->value, "pri_net")) {
					confp->chan.sig = SIG_PRI;
					confp->pri.pri.nodetype = PRI_NETWORK;
				} else if (!strcasecmp(v->value, "pri_cpe")) {
					confp->chan.sig = SIG_PRI;
					confp->pri.pri.nodetype = PRI_CPE;
				} else if (!strcasecmp(v->value, "bri_cpe")) {
					confp->chan.sig = SIG_BRI;
					confp->pri.pri.nodetype = PRI_CPE;
				} else if (!strcasecmp(v->value, "bri_net")) {
					confp->chan.sig = SIG_BRI;
					confp->pri.pri.nodetype = PRI_NETWORK;
				} else if (!strcasecmp(v->value, "bri_cpe_ptmp")) {
					confp->chan.sig = SIG_BRI_PTMP;
					confp->pri.pri.nodetype = PRI_CPE;
				} else if (!strcasecmp(v->value, "bri_net_ptmp")) {
					ast_log(LOG_WARNING, "How cool would it be if someone implemented this mode!  For now, sucks for you. (line %d)\n", v->lineno);
#endif
#ifdef HAVE_SS7
				} else if (!strcasecmp(v->value, "ss7")) {
					confp->chan.sig = SIG_SS7;
#endif
#ifdef HAVE_OPENR2
				} else if (!strcasecmp(v->value, "mfcr2")) {
					confp->chan.sig = SIG_MFCR2;
#endif
				} else if (!strcasecmp(v->value, "auto")) {
					confp->is_sig_auto = 1;
				} else {
					confp->chan.outsigmod = orig_outsigmod;
					confp->chan.radio = orig_radio;
					confp->is_sig_auto = orig_auto;
					ast_log(LOG_ERROR, "Unknown signalling method '%s' at line %d.\n", v->value, v->lineno);
				}
			 } else if (!strcasecmp(v->name, "outsignalling") || !strcasecmp(v->name, "outsignaling")) {
				if (!strcasecmp(v->value, "em")) {
					confp->chan.outsigmod = SIG_EM;
				} else if (!strcasecmp(v->value, "em_e1")) {
					confp->chan.outsigmod = SIG_EM_E1;
				} else if (!strcasecmp(v->value, "em_w")) {
					confp->chan.outsigmod = SIG_EMWINK;
				} else if (!strcasecmp(v->value, "sf")) {
					confp->chan.outsigmod = SIG_SF;
				} else if (!strcasecmp(v->value, "sf_w")) {
					confp->chan.outsigmod = SIG_SFWINK;
				} else if (!strcasecmp(v->value, "sf_featd")) {
					confp->chan.outsigmod = SIG_FEATD;
				} else if (!strcasecmp(v->value, "sf_featdmf")) {
					confp->chan.outsigmod = SIG_FEATDMF;
				} else if (!strcasecmp(v->value, "sf_featb")) {
					confp->chan.outsigmod = SIG_SF_FEATB;
				} else if (!strcasecmp(v->value, "sf")) {
					confp->chan.outsigmod = SIG_SF;
				} else if (!strcasecmp(v->value, "featd")) {
					confp->chan.outsigmod = SIG_FEATD;
				} else if (!strcasecmp(v->value, "featdmf")) {
					confp->chan.outsigmod = SIG_FEATDMF;
				} else if (!strcasecmp(v->value, "featdmf_ta")) {
					confp->chan.outsigmod = SIG_FEATDMF_TA;
				} else if (!strcasecmp(v->value, "e911")) {
					confp->chan.outsigmod = SIG_E911;
				} else if (!strcasecmp(v->value, "fgccama")) {
					confp->chan.outsigmod = SIG_FGC_CAMA;
				} else if (!strcasecmp(v->value, "fgccamamf")) {
					confp->chan.outsigmod = SIG_FGC_CAMAMF;
				} else if (!strcasecmp(v->value, "featb")) {
					confp->chan.outsigmod = SIG_FEATB;
				} else {
					ast_log(LOG_ERROR, "Unknown signalling method '%s' at line %d.\n", v->value, v->lineno);
				}
#ifdef HAVE_PRI
			} else if (!strcasecmp(v->name, "pridialplan")) {
				if (!strcasecmp(v->value, "national")) {
					confp->pri.pri.dialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					confp->pri.pri.dialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					confp->pri.pri.dialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					confp->pri.pri.dialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					confp->pri.pri.dialplan = PRI_LOCAL_ISDN + 1;
	 			} else if (!strcasecmp(v->value, "dynamic")) {
 					confp->pri.pri.dialplan = -1;
				} else if (!strcasecmp(v->value, "redundant")) {
					confp->pri.pri.dialplan = -2;
				} else {
					ast_log(LOG_WARNING, "Unknown PRI dialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "prilocaldialplan")) {
				if (!strcasecmp(v->value, "national")) {
					confp->pri.pri.localdialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					confp->pri.pri.localdialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					confp->pri.pri.localdialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					confp->pri.pri.localdialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					confp->pri.pri.localdialplan = PRI_LOCAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "dynamic")) {
					confp->pri.pri.localdialplan = -1;
				} else if (!strcasecmp(v->value, "redundant")) {
					confp->pri.pri.localdialplan = -2;
				} else {
					ast_log(LOG_WARNING, "Unknown PRI localdialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "switchtype")) {
				if (!strcasecmp(v->value, "national"))
					confp->pri.pri.switchtype = PRI_SWITCH_NI2;
				else if (!strcasecmp(v->value, "ni1"))
					confp->pri.pri.switchtype = PRI_SWITCH_NI1;
				else if (!strcasecmp(v->value, "dms100"))
					confp->pri.pri.switchtype = PRI_SWITCH_DMS100;
				else if (!strcasecmp(v->value, "4ess"))
					confp->pri.pri.switchtype = PRI_SWITCH_ATT4ESS;
				else if (!strcasecmp(v->value, "5ess"))
					confp->pri.pri.switchtype = PRI_SWITCH_LUCENT5E;
				else if (!strcasecmp(v->value, "euroisdn"))
					confp->pri.pri.switchtype = PRI_SWITCH_EUROISDN_E1;
				else if (!strcasecmp(v->value, "qsig"))
					confp->pri.pri.switchtype = PRI_SWITCH_QSIG;
				else {
					ast_log(LOG_ERROR, "Unknown switchtype '%s' at line %d.\n", v->value, v->lineno);
					return -1;
				}
			} else if (!strcasecmp(v->name, "nsf")) {
				if (!strcasecmp(v->value, "sdn"))
					confp->pri.pri.nsf = PRI_NSF_SDN;
				else if (!strcasecmp(v->value, "megacom"))
					confp->pri.pri.nsf = PRI_NSF_MEGACOM;
				else if (!strcasecmp(v->value, "tollfreemegacom"))
					confp->pri.pri.nsf = PRI_NSF_TOLL_FREE_MEGACOM;
				else if (!strcasecmp(v->value, "accunet"))
					confp->pri.pri.nsf = PRI_NSF_ACCUNET;
				else if (!strcasecmp(v->value, "none"))
					confp->pri.pri.nsf = PRI_NSF_NONE;
				else {
					ast_log(LOG_WARNING, "Unknown network-specific facility '%s' at line %d.\n", v->value, v->lineno);
					confp->pri.pri.nsf = PRI_NSF_NONE;
				}
			} else if (!strcasecmp(v->name, "priindication")) {
				if (!strcasecmp(v->value, "outofband"))
					confp->chan.priindication_oob = 1;
				else if (!strcasecmp(v->value, "inband"))
					confp->chan.priindication_oob = 0;
				else
					ast_log(LOG_WARNING, "'%s' is not a valid pri indication value, should be 'inband' or 'outofband' at line %d.\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "priexclusive")) {
				confp->chan.priexclusive = ast_true(v->value);
			} else if (!strcasecmp(v->name, "internationalprefix")) {
				ast_copy_string(confp->pri.pri.internationalprefix, v->value, sizeof(confp->pri.pri.internationalprefix));
			} else if (!strcasecmp(v->name, "nationalprefix")) {
				ast_copy_string(confp->pri.pri.nationalprefix, v->value, sizeof(confp->pri.pri.nationalprefix));
			} else if (!strcasecmp(v->name, "localprefix")) {
				ast_copy_string(confp->pri.pri.localprefix, v->value, sizeof(confp->pri.pri.localprefix));
			} else if (!strcasecmp(v->name, "privateprefix")) {
				ast_copy_string(confp->pri.pri.privateprefix, v->value, sizeof(confp->pri.pri.privateprefix));
			} else if (!strcasecmp(v->name, "unknownprefix")) {
				ast_copy_string(confp->pri.pri.unknownprefix, v->value, sizeof(confp->pri.pri.unknownprefix));
			} else if (!strcasecmp(v->name, "resetinterval")) {
				if (!strcasecmp(v->value, "never"))
					confp->pri.pri.resetinterval = -1;
				else if (atoi(v->value) >= 60)
					confp->pri.pri.resetinterval = atoi(v->value);
				else
					ast_log(LOG_WARNING, "'%s' is not a valid reset interval, should be >= 60 seconds or 'never' at line %d.\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "minunused")) {
				confp->pri.pri.minunused = atoi(v->value);
			} else if (!strcasecmp(v->name, "minidle")) {
				confp->pri.pri.minidle = atoi(v->value);
			} else if (!strcasecmp(v->name, "idleext")) {
				ast_copy_string(confp->pri.pri.idleext, v->value, sizeof(confp->pri.pri.idleext));
			} else if (!strcasecmp(v->name, "idledial")) {
				ast_copy_string(confp->pri.pri.idledial, v->value, sizeof(confp->pri.pri.idledial));
			} else if (!strcasecmp(v->name, "overlapdial")) {
				if (ast_true(v->value)) {
					confp->pri.pri.overlapdial = DAHDI_OVERLAPDIAL_BOTH;
				} else if (!strcasecmp(v->value, "incoming")) {
					confp->pri.pri.overlapdial = DAHDI_OVERLAPDIAL_INCOMING;
				} else if (!strcasecmp(v->value, "outgoing")) {
					confp->pri.pri.overlapdial = DAHDI_OVERLAPDIAL_OUTGOING;
				} else if (!strcasecmp(v->value, "both") || ast_true(v->value)) {
					confp->pri.pri.overlapdial = DAHDI_OVERLAPDIAL_BOTH;
				} else {
					confp->pri.pri.overlapdial = DAHDI_OVERLAPDIAL_NONE;
				}
#ifdef HAVE_PRI_PROG_W_CAUSE
			} else if (!strcasecmp(v->name, "qsigchannelmapping")) {
				if (!strcasecmp(v->value, "logical")) {
					confp->pri.pri.qsigchannelmapping = DAHDI_CHAN_MAPPING_LOGICAL;
				} else if (!strcasecmp(v->value, "physical")) {
					confp->pri.pri.qsigchannelmapping = DAHDI_CHAN_MAPPING_PHYSICAL;
				} else {
					confp->pri.pri.qsigchannelmapping = DAHDI_CHAN_MAPPING_PHYSICAL;
				}
#endif
			} else if (!strcasecmp(v->name, "discardremoteholdretrieval")) {
				confp->pri.pri.discardremoteholdretrieval = ast_true(v->value);
#ifdef HAVE_PRI_SERVICE_MESSAGES
			} else if (!strcasecmp(v->name, "service_message_support")) {
				/* assuming switchtype for this channel group has been configured already */
				if ((confp->pri.pri.switchtype == PRI_SWITCH_ATT4ESS || confp->pri.pri.switchtype == PRI_SWITCH_LUCENT5E) && ast_true(v->value))
					confp->pri.pri.enable_service_message_support = 1;
				else
					confp->pri.pri.enable_service_message_support = 0;
#endif
#ifdef HAVE_PRI_INBANDDISCONNECT
			} else if (!strcasecmp(v->name, "inbanddisconnect")) {
				confp->pri.pri.inbanddisconnect = ast_true(v->value);
#endif
			} else if (!strcasecmp(v->name, "pritimer")) {
#ifdef PRI_GETSET_TIMERS
				char tmp[20];
				char *timerc;
				char *c;
				int timer;
				int timeridx;

				ast_copy_string(tmp, v->value, sizeof(tmp));
				c = tmp;
				timerc = strsep(&c, ",");
				if (!ast_strlen_zero(timerc) && !ast_strlen_zero(c)) {
					timeridx = pri_timer2idx(timerc);
					timer = atoi(c);
					if (timeridx < 0 || PRI_MAX_TIMERS <= timeridx) {
						ast_log(LOG_WARNING,
							"'%s' is not a valid ISDN timer at line %d.\n", timerc,
							v->lineno);
					} else if (!timer) {
						ast_log(LOG_WARNING,
							"'%s' is not a valid value for ISDN timer '%s' at line %d.\n",
							c, timerc, v->lineno);
					} else {
						pritimers[timeridx] = timer;
					}
				} else {
					ast_log(LOG_WARNING,
						"'%s' is not a valid ISDN timer configuration string at line %d.\n",
						v->value, v->lineno);
				}
#endif /* PRI_GETSET_TIMERS */
			} else if (!strcasecmp(v->name, "facilityenable")) {
				confp->pri.pri.facilityenable = ast_true(v->value);
#endif /* HAVE_PRI */
#ifdef HAVE_SS7
			} else if (!strcasecmp(v->name, "ss7type")) {
				if (!strcasecmp(v->value, "itu")) {
					cur_ss7type = SS7_ITU;
				} else if (!strcasecmp(v->value, "ansi")) {
					cur_ss7type = SS7_ANSI;
				} else
					ast_log(LOG_WARNING, "'%s' is an unknown ss7 switch type at line %d.!\n", v->value, v->lineno);
			} else if (!strcasecmp(v->name, "linkset")) {
				cur_linkset = atoi(v->value);
			} else if (!strcasecmp(v->name, "pointcode")) {
				cur_pointcode = parse_pointcode(v->value);
			} else if (!strcasecmp(v->name, "adjpointcode")) {
				cur_adjpointcode = parse_pointcode(v->value);
			} else if (!strcasecmp(v->name, "defaultdpc")) {
				cur_defaultdpc = parse_pointcode(v->value);
			} else if (!strcasecmp(v->name, "cicbeginswith")) {
				cur_cicbeginswith = atoi(v->value);
			} else if (!strcasecmp(v->name, "networkindicator")) {
				if (!strcasecmp(v->value, "national"))
					cur_networkindicator = SS7_NI_NAT;
				else if (!strcasecmp(v->value, "national_spare"))
					cur_networkindicator = SS7_NI_NAT_SPARE;
				else if (!strcasecmp(v->value, "international"))
					cur_networkindicator = SS7_NI_INT;
				else if (!strcasecmp(v->value, "international_spare"))
					cur_networkindicator = SS7_NI_INT_SPARE;
				else
					cur_networkindicator = -1;
			} else if (!strcasecmp(v->name, "ss7_internationalprefix")) {
				ast_copy_string(confp->ss7.internationalprefix, v->value, sizeof(confp->ss7.internationalprefix));
			} else if (!strcasecmp(v->name, "ss7_nationalprefix")) {
				ast_copy_string(confp->ss7.nationalprefix, v->value, sizeof(confp->ss7.nationalprefix));
			} else if (!strcasecmp(v->name, "ss7_subscriberprefix")) {
				ast_copy_string(confp->ss7.subscriberprefix, v->value, sizeof(confp->ss7.subscriberprefix));
			} else if (!strcasecmp(v->name, "ss7_unknownprefix")) {
				ast_copy_string(confp->ss7.unknownprefix, v->value, sizeof(confp->ss7.unknownprefix));
			} else if (!strcasecmp(v->name, "ss7_called_nai")) {
				if (!strcasecmp(v->value, "national")) {
					confp->ss7.called_nai = SS7_NAI_NATIONAL;
				} else if (!strcasecmp(v->value, "international")) {
					confp->ss7.called_nai = SS7_NAI_INTERNATIONAL;
				} else if (!strcasecmp(v->value, "subscriber")) {
					confp->ss7.called_nai = SS7_NAI_SUBSCRIBER;
	 			} else if (!strcasecmp(v->value, "dynamic")) {
 					confp->ss7.called_nai = SS7_NAI_DYNAMIC;
				} else {
					ast_log(LOG_WARNING, "Unknown SS7 called_nai '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "ss7_calling_nai")) {
				if (!strcasecmp(v->value, "national")) {
					confp->ss7.calling_nai = SS7_NAI_NATIONAL;
				} else if (!strcasecmp(v->value, "international")) {
					confp->ss7.calling_nai = SS7_NAI_INTERNATIONAL;
				} else if (!strcasecmp(v->value, "subscriber")) {
					confp->ss7.calling_nai = SS7_NAI_SUBSCRIBER;
				} else if (!strcasecmp(v->value, "dynamic")) {
					confp->ss7.calling_nai = SS7_NAI_DYNAMIC;
				} else {
					ast_log(LOG_WARNING, "Unknown SS7 calling_nai '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "sigchan")) {
				int sigchan, res;
				sigchan = atoi(v->value);
				res = linkset_addsigchan(sigchan);
				if (res < 0)
					return -1;

			} else if (!strcasecmp(v->name, "ss7_explicitacm")) {
				struct dahdi_ss7 *link;
				link = ss7_resolve_linkset(cur_linkset);
				if (!link) {
					ast_log(LOG_ERROR, "Invalid linkset number.  Must be between 1 and %d\n", NUM_SPANS + 1);
					return -1;
				}
				if (ast_true(v->value))
					link->flags |= LINKSET_FLAG_EXPLICITACM;
#endif /* HAVE_SS7 */
#ifdef HAVE_OPENR2
			} else if (!strcasecmp(v->name, "mfcr2_advanced_protocol_file")) {
				ast_copy_string(confp->mfcr2.r2proto_file, v->value, sizeof(confp->mfcr2.r2proto_file));
				ast_log(LOG_WARNING, "MFC/R2 Protocol file '%s' will be used, you only should use this if you *REALLY KNOW WHAT YOU ARE DOING*.\n", confp->mfcr2.r2proto_file);
			} else if (!strcasecmp(v->name, "mfcr2_logdir")) {
				ast_copy_string(confp->mfcr2.logdir, v->value, sizeof(confp->mfcr2.logdir));
			} else if (!strcasecmp(v->name, "mfcr2_variant")) {
				confp->mfcr2.variant = openr2_proto_get_variant(v->value);
				if (OR2_VAR_UNKNOWN == confp->mfcr2.variant) {
					ast_log(LOG_WARNING, "Unknown MFC/R2 variant '%s' at line %d, defaulting to ITU.\n", v->value, v->lineno);
					confp->mfcr2.variant = OR2_VAR_ITU;
				}
			} else if (!strcasecmp(v->name, "mfcr2_mfback_timeout")) {
				confp->mfcr2.mfback_timeout = atoi(v->value);
				if (!confp->mfcr2.mfback_timeout) {
					ast_log(LOG_WARNING, "MF timeout of 0? hum, I will protect you from your ignorance. Setting default.\n");
					confp->mfcr2.mfback_timeout = -1;
				} else if (confp->mfcr2.mfback_timeout > 0 && confp->mfcr2.mfback_timeout < 500) {
					ast_log(LOG_WARNING, "MF timeout less than 500ms is not recommended, you have been warned!\n");
				}
			} else if (!strcasecmp(v->name, "mfcr2_metering_pulse_timeout")) {
				confp->mfcr2.metering_pulse_timeout = atoi(v->value);
				if (confp->mfcr2.metering_pulse_timeout > 500) {
					ast_log(LOG_WARNING, "Metering pulse timeout greater than 500ms is not recommended, you have been warned!\n");
				}
			} else if (!strcasecmp(v->name, "mfcr2_get_ani_first")) {
				confp->mfcr2.get_ani_first = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_double_answer")) {
				confp->mfcr2.double_answer = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_charge_calls")) {
				confp->mfcr2.charge_calls = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_accept_on_offer")) {
				confp->mfcr2.accept_on_offer = ast_true(v->value);
			} else if (!strcasecmp(v->name, "mfcr2_allow_collect_calls")) {
				confp->mfcr2.allow_collect_calls = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_forced_release")) {
				confp->mfcr2.forced_release = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_immediate_accept")) {
				confp->mfcr2.immediate_accept = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_call_files")) {
				confp->mfcr2.call_files = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_max_ani")) {
				confp->mfcr2.max_ani = atoi(v->value);
				if (confp->mfcr2.max_ani >= AST_MAX_EXTENSION){
					confp->mfcr2.max_ani = AST_MAX_EXTENSION - 1;
				}
			} else if (!strcasecmp(v->name, "mfcr2_max_dnis")) {
				confp->mfcr2.max_dnis = atoi(v->value);
				if (confp->mfcr2.max_dnis >= AST_MAX_EXTENSION){
					confp->mfcr2.max_dnis = AST_MAX_EXTENSION - 1;
				}
			} else if (!strcasecmp(v->name, "mfcr2_category")) {
				confp->mfcr2.category = openr2_proto_get_category(v->value);
				if (OR2_CALLING_PARTY_CATEGORY_UNKNOWN == confp->mfcr2.category) {
					confp->mfcr2.category = OR2_CALLING_PARTY_CATEGORY_NATIONAL_SUBSCRIBER;
					ast_log(LOG_WARNING, "Invalid MFC/R2 caller category '%s' at line %d. Using national subscriber as default.\n",
							v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "mfcr2_logging")) {
				openr2_log_level_t tmplevel;
				char *clevel;
				char *logval = ast_strdupa(v->value);
				while (logval) {
 					clevel = strsep(&logval,",");
					if (-1 == (tmplevel = openr2_log_get_level(clevel))) {
						ast_log(LOG_WARNING, "Ignoring invalid logging level: '%s' at line %d.\n", clevel, v->lineno);
						continue;
					}
					confp->mfcr2.loglevel |= tmplevel;
				}
#endif /* HAVE_OPENR2 */
			} else if (!strcasecmp(v->name, "cadence")) {
				/* setup to scan our argument */
				int element_count, c[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
				int i;
				struct dahdi_ring_cadence new_cadence;
				int cid_location = -1;
				int firstcadencepos = 0;
				char original_args[80];
				int cadence_is_ok = 1;

				ast_copy_string(original_args, v->value, sizeof(original_args));
				/* 16 cadences allowed (8 pairs) */
				element_count = sscanf(v->value, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8], &c[9], &c[10], &c[11], &c[12], &c[13], &c[14], &c[15]);

				/* Cadence must be even (on/off) */
				if (element_count % 2 == 1) {
					ast_log(LOG_ERROR, "Must be a silence duration for each ring duration: %s at line %d.\n", original_args, v->lineno);
					cadence_is_ok = 0;
				}

				/* Ring cadences cannot be negative */
				for (i = 0; i < element_count; i++) {
					if (c[i] == 0) {
						ast_log(LOG_ERROR, "Ring or silence duration cannot be zero: %s at line %d.\n", original_args, v->lineno);
						cadence_is_ok = 0;
						break;
					} else if (c[i] < 0) {
						if (i % 2 == 1) {
							/* Silence duration, negative possibly okay */
							if (cid_location == -1) {
								cid_location = i;
								c[i] *= -1;
							} else {
								ast_log(LOG_ERROR, "CID location specified twice: %s at line %d.\n", original_args, v->lineno);
								cadence_is_ok = 0;
								break;
							}
						} else {
							if (firstcadencepos == 0) {
								firstcadencepos = i; /* only recorded to avoid duplicate specification */
											/* duration will be passed negative to the DAHDI driver */
							} else {
								 ast_log(LOG_ERROR, "First cadence position specified twice: %s at line %d.\n", original_args, v->lineno);
								cadence_is_ok = 0;
								break;
							}
						}
					}
				}

				/* Substitute our scanned cadence */
				for (i = 0; i < 16; i++) {
					new_cadence.ringcadence[i] = c[i];
				}

				if (cadence_is_ok) {
					/* ---we scanned it without getting annoyed; now some sanity checks--- */
					if (element_count < 2) {
						ast_log(LOG_ERROR, "Minimum cadence is ring,pause: %s at line %d.\n", original_args, v->lineno);
					} else {
						if (cid_location == -1) {
							/* user didn't say; default to first pause */
							cid_location = 1;
						} else {
							/* convert element_index to cidrings value */
							cid_location = (cid_location + 1) / 2;
						}
						/* ---we like their cadence; try to install it--- */
						if (!user_has_defined_cadences++)
							/* this is the first user-defined cadence; clear the default user cadences */
							num_cadence = 0;
						if ((num_cadence+1) >= NUM_CADENCE_MAX)
							ast_log(LOG_ERROR, "Already %d cadences; can't add another: %s at line %d.\n", NUM_CADENCE_MAX, original_args, v->lineno);
						else {
							cadences[num_cadence] = new_cadence;
							cidrings[num_cadence++] = cid_location;
							ast_verb(3, "cadence 'r%d' added: %s\n",num_cadence,original_args);
						}
					}
				}
			} else if (!strcasecmp(v->name, "ringtimeout")) {
				ringt_base = (atoi(v->value) * 8) / READ_SIZE;
			} else if (!strcasecmp(v->name, "prewink")) {
				confp->timing.prewinktime = atoi(v->value);
			} else if (!strcasecmp(v->name, "preflash")) {
				confp->timing.preflashtime = atoi(v->value);
			} else if (!strcasecmp(v->name, "wink")) {
				confp->timing.winktime = atoi(v->value);
			} else if (!strcasecmp(v->name, "flash")) {
				confp->timing.flashtime = atoi(v->value);
			} else if (!strcasecmp(v->name, "start")) {
				confp->timing.starttime = atoi(v->value);
			} else if (!strcasecmp(v->name, "rxwink")) {
				confp->timing.rxwinktime = atoi(v->value);
			} else if (!strcasecmp(v->name, "rxflash")) {
				confp->timing.rxflashtime = atoi(v->value);
			} else if (!strcasecmp(v->name, "debounce")) {
				confp->timing.debouncetime = atoi(v->value);
			} else if (!strcasecmp(v->name, "toneduration")) {
				int toneduration;
				int ctlfd;
				int res;
				struct dahdi_dialparams dps;

				ctlfd = open("/dev/dahdi/ctl", O_RDWR);
				if (ctlfd == -1) {
					ast_log(LOG_ERROR, "Unable to open /dev/dahdi/ctl to set toneduration at line %d.\n", v->lineno);
					return -1;
				}

				toneduration = atoi(v->value);
				if (toneduration > -1) {
					memset(&dps, 0, sizeof(dps));

					dps.dtmf_tonelen = dps.mfv1_tonelen = toneduration;
					res = ioctl(ctlfd, DAHDI_SET_DIALPARAMS, &dps);
					if (res < 0) {
						ast_log(LOG_ERROR, "Invalid tone duration: %d ms at line %d: %s\n", toneduration, v->lineno, strerror(errno));
						return -1;
					}
				}
				close(ctlfd);
			} else if (!strcasecmp(v->name, "defaultcic")) {
				ast_copy_string(defaultcic, v->value, sizeof(defaultcic));
			} else if (!strcasecmp(v->name, "defaultozz")) {
				ast_copy_string(defaultozz, v->value, sizeof(defaultozz));
			} else if (!strcasecmp(v->name, "mwilevel")) {
				mwilevel = atoi(v->value);
			}
		} else if (!(options & PROC_DAHDI_OPT_NOWARN) )
			ast_log(LOG_WARNING, "Ignoring any changes to '%s' (on reload) at line %d.\n", v->name, v->lineno);
	}
	if (dahdichan[0]) {
		/* The user has set 'dahdichan' */
		/*< \todo pass proper line number instead of 0 */
		if (build_channels(confp, dahdichan, reload, 0, &found_pseudo)) {
			return -1;
		}
	}
	/*< \todo why check for the pseudo in the per-channel section.
	 * Any actual use for manual setup of the pseudo channel? */
	if (!found_pseudo && reload == 0) {
		/* use the default configuration for a channel, so
		   that any settings from real configured channels
		   don't "leak" into the pseudo channel config
		*/
		struct dahdi_chan_conf conf = dahdi_chan_conf_default();

		tmp = mkintf(CHAN_PSEUDO, &conf, reload);

		if (tmp) {
			ast_verb(3, "Automatically generated pseudo channel\n");
		} else {
			ast_log(LOG_WARNING, "Unable to register pseudo channel!\n");
		}
	}
	return 0;
}

static int setup_dahdi(int reload)
{
	struct ast_config *cfg, *ucfg;
	struct ast_variable *v;
 	struct dahdi_chan_conf base_conf = dahdi_chan_conf_default();
 	struct dahdi_chan_conf conf;
	struct ast_flags config_flags = { reload == 1 ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *cat;
	int res;

#ifdef HAVE_PRI
	char *c;
	int spanno;
	int i;
	int logicalspan;
	int trunkgroup;
	int dchannels[NUM_DCHANS];
#endif

	cfg = ast_config_load(config, config_flags);

	/* Error if we have no config file */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED) {
			return 0;
		} else if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			return 0;
		}
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		if ((cfg = ast_config_load(config, config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
			ast_config_destroy(ucfg);
			return 0;
		}
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
		return 0;
	} else {
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		if ((ucfg = ast_config_load("users.conf", config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
	}

	/* It's a little silly to lock it, but we mind as well just to be sure */
	ast_mutex_lock(&iflock);
#ifdef HAVE_PRI
	if (reload != 1) {
		/* Process trunkgroups first */
		v = ast_variable_browse(cfg, "trunkgroups");
		while (v) {
			if (!strcasecmp(v->name, "trunkgroup")) {
				trunkgroup = atoi(v->value);
				if (trunkgroup > 0) {
					if ((c = strchr(v->value, ','))) {
						i = 0;
						memset(dchannels, 0, sizeof(dchannels));
						while (c && (i < NUM_DCHANS)) {
							dchannels[i] = atoi(c + 1);
							if (dchannels[i] < 0) {
								ast_log(LOG_WARNING, "D-channel for trunk group %d must be a postiive number at line %d of chan_dahdi.conf\n", trunkgroup, v->lineno);
							} else
								i++;
							c = strchr(c + 1, ',');
						}
						if (i) {
							if (pri_create_trunkgroup(trunkgroup, dchannels)) {
								ast_log(LOG_WARNING, "Unable to create trunk group %d with Primary D-channel %d at line %d of chan_dahdi.conf\n", trunkgroup, dchannels[0], v->lineno);
						} else
								ast_verb(2, "Created trunk group %d with Primary D-channel %d and %d backup%s\n", trunkgroup, dchannels[0], i - 1, (i == 1) ? "" : "s");
						} else
							ast_log(LOG_WARNING, "Trunk group %d lacks any valid D-channels at line %d of chan_dahdi.conf\n", trunkgroup, v->lineno);
					} else
						ast_log(LOG_WARNING, "Trunk group %d lacks a primary D-channel at line %d of chan_dahdi.conf\n", trunkgroup, v->lineno);
				} else
					ast_log(LOG_WARNING, "Trunk group identifier must be a positive integer at line %d of chan_dahdi.conf\n", v->lineno);
			} else if (!strcasecmp(v->name, "spanmap")) {
				spanno = atoi(v->value);
				if (spanno > 0) {
					if ((c = strchr(v->value, ','))) {
						trunkgroup = atoi(c + 1);
						if (trunkgroup > 0) {
							if ((c = strchr(c + 1, ',')))
								logicalspan = atoi(c + 1);
							else
								logicalspan = 0;
							if (logicalspan >= 0) {
								if (pri_create_spanmap(spanno - 1, trunkgroup, logicalspan)) {
									ast_log(LOG_WARNING, "Failed to map span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
							} else
									ast_verb(2, "Mapped span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
							} else
								ast_log(LOG_WARNING, "Logical span must be a postive number, or '0' (for unspecified) at line %d of chan_dahdi.conf\n", v->lineno);
						} else
							ast_log(LOG_WARNING, "Trunk group must be a postive number at line %d of chan_dahdi.conf\n", v->lineno);
					} else
						ast_log(LOG_WARNING, "Missing trunk group for span map at line %d of chan_dahdi.conf\n", v->lineno);
				} else
					ast_log(LOG_WARNING, "Span number must be a postive integer at line %d of chan_dahdi.conf\n", v->lineno);
			} else {
				ast_log(LOG_NOTICE, "Ignoring unknown keyword '%s' in trunkgroups\n", v->name);
			}
			v = v->next;
		}
	}
#endif

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(global_jbconf));

	mwimonitornotify[0] = '\0';

	v = ast_variable_browse(cfg, "channels");
	if ((res = process_dahdi(&base_conf, "", v, reload, 0))) {
		ast_mutex_unlock(&iflock);
		ast_config_destroy(cfg);
		if (ucfg) {
			ast_config_destroy(ucfg);
		}
		return res;
	}

	/* Now get configuration from all normal sections in chan_dahdi.conf: */
	for (cat = ast_category_browse(cfg, NULL); cat ; cat = ast_category_browse(cfg, cat)) {
		/* [channels] and [trunkgroups] are used. Let's also reserve
		 * [globals] and [general] for future use
		 */
		if (!strcasecmp(cat, "general") ||
			!strcasecmp(cat, "trunkgroups") ||
			!strcasecmp(cat, "globals") ||
			!strcasecmp(cat, "channels")) {
			continue;
		}

		memcpy(&conf, &base_conf, sizeof(conf));

		if ((res = process_dahdi(&conf, cat, ast_variable_browse(cfg, cat), reload, PROC_DAHDI_OPT_NOCHAN))) {
			ast_mutex_unlock(&iflock);
			ast_config_destroy(cfg);
			if (ucfg) {
				ast_config_destroy(cfg);
			}
			return res;
		}
	}

	ast_config_destroy(cfg);

	if (ucfg) {
		const char *chans;

		process_dahdi(&base_conf, "", ast_variable_browse(ucfg, "general"), 1, 0);

		for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
			if (!strcasecmp(cat, "general")) {
				continue;
			}

			chans = ast_variable_retrieve(ucfg, cat, "dahdichan");

			if (ast_strlen_zero(chans)) {
				continue;
			}

			memcpy(&conf, &base_conf, sizeof(conf));

			if ((res = process_dahdi(&conf, cat, ast_variable_browse(ucfg, cat), reload, PROC_DAHDI_OPT_NOCHAN | PROC_DAHDI_OPT_NOWARN))) {
				ast_config_destroy(ucfg);
				ast_mutex_unlock(&iflock);
				return res;
			}
		}
		ast_config_destroy(ucfg);
	}
	ast_mutex_unlock(&iflock);

#ifdef HAVE_PRI
	if (reload != 1) {
		int x;
		for (x = 0; x < NUM_SPANS; x++) {
			if (pris[x].pri.pvts[0]) {
				prepare_pri(pris + x);
				if (sig_pri_start_pri(&pris[x].pri)) {
					ast_log(LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
				} else
					ast_verb(2, "Starting D-Channel on span %d\n", x + 1);
			}
		}
	}
#endif
#ifdef HAVE_SS7
	if (reload != 1) {
		int x;
		for (x = 0; x < NUM_SPANS; x++) {
			if (linksets[x].ss7) {
				if (ast_pthread_create(&linksets[x].master, NULL, ss7_linkset, &linksets[x])) {
					ast_log(LOG_ERROR, "Unable to start SS7 linkset on span %d\n", x + 1);
					return -1;
				} else
					ast_verb(2, "Starting SS7 linkset on span %d\n", x + 1);
			}
		}
	}
#endif
#ifdef HAVE_OPENR2
	if (reload != 1) {
		int x;
		for (x = 0; x < r2links_count; x++) {
			if (ast_pthread_create(&r2links[x]->r2master, NULL, mfcr2_monitor, r2links[x])) {
				ast_log(LOG_ERROR, "Unable to start R2 monitor on channel group %d\n", x + 1);
				return -1;
			} else {
				ast_verb(2, "Starting R2 monitor on channel group %d\n", x + 1);
			}
		}
	}
#endif
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

static int load_module(void)
{
	int res;
#if defined(HAVE_PRI)
	int y;
#endif
#if defined(HAVE_SS7)
	int i;
#endif
#ifdef HAVE_PRI
	memset(pris, 0, sizeof(pris));
	for (y = 0; y < NUM_SPANS; y++) {
		sig_pri_init_pri(&pris[y].pri);
	}
	pri_set_error(dahdi_pri_error);
	pri_set_message(dahdi_pri_message);
	ast_register_application_xml(dahdi_send_keypad_facility_app, dahdi_send_keypad_facility_exec);
#ifdef HAVE_PRI_PROG_W_CAUSE
	ast_register_application_xml(dahdi_send_callrerouting_facility_app, dahdi_send_callrerouting_facility_exec);
#endif
#endif
#ifdef HAVE_SS7
	memset(linksets, 0, sizeof(linksets));
	for (y = 0; y < NUM_SPANS; y++) {
		ast_mutex_init(&linksets[y].lock);
		linksets[y].master = AST_PTHREADT_NULL;
		for (i = 0; i < NUM_DCHANS; i++)
			linksets[y].fds[i] = -1;
	}
	ss7_set_error(dahdi_ss7_error);
	ss7_set_message(dahdi_ss7_message);
#endif /* HAVE_SS7 */
	res = setup_dahdi(0);
	/* Make sure we can register our DAHDI channel type */
	if (res)
		return AST_MODULE_LOAD_DECLINE;
	if (ast_channel_register(&dahdi_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'DAHDI'\n");
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}
#ifdef HAVE_PRI
	ast_cli_register_multiple(dahdi_pri_cli, ARRAY_LEN(dahdi_pri_cli));
#endif
#ifdef HAVE_SS7
	ast_cli_register_multiple(dahdi_ss7_cli, ARRAY_LEN(dahdi_ss7_cli));
#endif
#ifdef HAVE_OPENR2
	ast_cli_register_multiple(dahdi_mfcr2_cli, sizeof(dahdi_mfcr2_cli)/sizeof(dahdi_mfcr2_cli[0]));
	ast_register_application_xml(dahdi_accept_r2_call_app, dahdi_accept_r2_call_exec);
#endif

	ast_cli_register_multiple(dahdi_cli, ARRAY_LEN(dahdi_cli));

	memset(round_robin, 0, sizeof(round_robin));
	ast_manager_register_xml("DAHDITransfer", 0, action_transfer);
	ast_manager_register_xml("DAHDIHangup", 0, action_transferhangup);
	ast_manager_register_xml("DAHDIDialOffhook", 0, action_dahdidialoffhook);
	ast_manager_register_xml("DAHDIDNDon", 0, action_dahdidndon);
	ast_manager_register_xml("DAHDIDNDoff", 0, action_dahdidndoff);
	ast_manager_register_xml("DAHDIShowChannels", 0, action_dahdishowchannels);
	ast_manager_register_xml("DAHDIRestart", 0, action_dahdirestart);

	ast_cond_init(&ss_thread_complete, NULL);

	return res;
}

static int dahdi_sendtext(struct ast_channel *c, const char *text)
{
#define	END_SILENCE_LEN 400
#define	HEADER_MS 50
#define	TRAILER_MS 5
#define	HEADER_LEN ((HEADER_MS + TRAILER_MS) * 8)
#define	ASCII_BYTES_PER_CHAR 80

	unsigned char *buf,*mybuf;
	struct dahdi_pvt *p = c->tech_pvt;
	struct pollfd fds[1];
	int size,res,fd,len,x;
	int bytes=0;
	/* Initial carrier (imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	int idx;

	idx = dahdi_get_index(c, p, 0);
	if (idx < 0) {
		ast_log(LOG_WARNING, "Huh?  I don't exist?\n");
		return -1;
	}
	if (!text[0]) return(0); /* if nothing to send, dont */
	if ((!p->tdd) && (!p->mate)) return(0);  /* if not in TDD mode, just return */
	if (p->mate)
		buf = ast_malloc(((strlen(text) + 1) * ASCII_BYTES_PER_CHAR) + END_SILENCE_LEN + HEADER_LEN);
	else
		buf = ast_malloc(((strlen(text) + 1) * TDD_BYTES_PER_CHAR) + END_SILENCE_LEN);
	if (!buf)
		return -1;
	mybuf = buf;
	if (p->mate) {
		int codec = AST_LAW(p);
		for (x = 0; x < HEADER_MS; x++) {	/* 50 ms of Mark */
			PUT_CLID_MARKMS;
		}
		/* Put actual message */
		for (x = 0; text[x]; x++) {
			PUT_CLID(text[x]);
		}
		for (x = 0; x < TRAILER_MS; x++) {	/* 5 ms of Mark */
			PUT_CLID_MARKMS;
		}
		len = bytes;
		buf = mybuf;
	} else {
		len = tdd_generate(p->tdd, buf, text);
		if (len < 1) {
			ast_log(LOG_ERROR, "TDD generate (len %d) failed!!\n", (int)strlen(text));
			ast_free(mybuf);
			return -1;
		}
	}
	memset(buf + len, 0x7f, END_SILENCE_LEN);
	len += END_SILENCE_LEN;
	fd = p->subs[idx].dfd;
	while (len) {
		if (ast_check_hangup(c)) {
			ast_free(mybuf);
			return -1;
		}
		size = len;
		if (size > READ_SIZE)
			size = READ_SIZE;
		fds[0].fd = fd;
		fds[0].events = POLLOUT | POLLPRI;
		fds[0].revents = 0;
		res = poll(fds, 1, -1);
		if (!res) {
			ast_debug(1, "poll (for write) ret. 0 on channel %d\n", p->channel);
			continue;
		}
		/* if got exception */
		if (fds[0].revents & POLLPRI) {
			ast_free(mybuf);
			return -1;
		}
		if (!(fds[0].revents & POLLOUT)) {
			ast_debug(1, "write fd not ready on channel %d\n", p->channel);
			continue;
		}
		res = write(fd, buf, size);
		if (res != size) {
			if (res == -1) {
				ast_free(mybuf);
				return -1;
			}
			ast_debug(1, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			break;
		}
		len -= size;
		buf += size;
	}
	ast_free(mybuf);
	return(0);
}


static int reload(void)
{
	int res = 0;

	res = setup_dahdi(1);
	if (res) {
		ast_log(LOG_WARNING, "Reload of chan_dahdi.so is unsuccessful!\n");
		return -1;
	}
	return 0;
}


/* This is a workaround so that menuselect displays a proper description
 * AST_MODULE_INFO(, , "DAHDI Telephony"
 */

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, tdesc,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	);
