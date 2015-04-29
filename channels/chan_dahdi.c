/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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
	<use type="module">res_smdi</use>
	<depend>dahdi</depend>
	<depend>tonezone</depend>
	<use type="external">pri</use>
	<use type="external">ss7</use>
	<use type="external">openr2</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <math.h>
#include <ctype.h>

#include <dahdi/user.h>
#include <dahdi/tonezone.h>
#include "sig_analog.h"
/* Analog signaling is currently still present in chan_dahdi for use with
 * radio. Sig_analog does not currently handle any radio operations. If
 * radio only uses analog signaling, then the radio handling logic could
 * be placed in sig_analog and the duplicated code could be removed.
 */

#if defined(HAVE_PRI)
#include "sig_pri.h"
#ifndef PRI_RESTART
#error "Upgrade your libpri"
#endif
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
#include "sig_ss7.h"
#if defined(LIBSS7_ABI_COMPATIBILITY)
#error "Your installed libss7 is not compatible"
#endif
#endif	/* defined(HAVE_SS7) */

#ifdef HAVE_OPENR2
/* put this here until sig_mfcr2 comes along */
#define SIG_MFCR2_MAX_CHANNELS	672		/*!< No more than a DS3 per trunk group */
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
#include "asterisk/cel.h"
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
#include "asterisk/ccss.h"
#include "asterisk/data.h"

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
			Send an ISDN call rerouting/deflection facility message.
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
			<para>This application will send an ISDN switch specific call
			rerouting/deflection facility message over the current channel.
			Supported switches depend upon the version of libpri in use.</para>
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
				<para>DAHDI channel number to transfer.</para>
			</parameter>
		</syntax>
		<description>
			<para>Simulate a flash hook event by the user connected to the channel.</para>
			<note><para>Valid only for analog channels.</para></note>
		</description>
	</manager>
	<manager name="DAHDIHangup" language="en_US">
		<synopsis>
			Hangup DAHDI Channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true">
				<para>DAHDI channel number to hangup.</para>
			</parameter>
		</syntax>
		<description>
			<para>Simulate an on-hook event by the user connected to the channel.</para>
			<note><para>Valid only for analog channels.</para></note>
		</description>
	</manager>
	<manager name="DAHDIDialOffhook" language="en_US">
		<synopsis>
			Dial over DAHDI channel while offhook.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true">
				<para>DAHDI channel number to dial digits.</para>
			</parameter>
			<parameter name="Number" required="true">
				<para>Digits to dial.</para>
			</parameter>
		</syntax>
		<description>
			<para>Generate DTMF control frames to the bridged peer.</para>
		</description>
	</manager>
	<manager name="DAHDIDNDon" language="en_US">
		<synopsis>
			Toggle DAHDI channel Do Not Disturb status ON.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true">
				<para>DAHDI channel number to set DND on.</para>
			</parameter>
		</syntax>
		<description>
			<para>Equivalent to the CLI command "dahdi set dnd <variable>channel</variable> on".</para>
			<note><para>Feature only supported by analog channels.</para></note>
		</description>
	</manager>
	<manager name="DAHDIDNDoff" language="en_US">
		<synopsis>
			Toggle DAHDI channel Do Not Disturb status OFF.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel" required="true">
				<para>DAHDI channel number to set DND off.</para>
			</parameter>
		</syntax>
		<description>
			<para>Equivalent to the CLI command "dahdi set dnd <variable>channel</variable> off".</para>
			<note><para>Feature only supported by analog channels.</para></note>
		</description>
	</manager>
	<manager name="DAHDIShowChannels" language="en_US">
		<synopsis>
			Show status of DAHDI channels.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="DAHDIChannel">
				<para>Specify the specific channel number to show.  Show all channels if zero or not present.</para>
			</parameter>
		</syntax>
		<description>
			<para>Similar to the CLI command "dahdi show channels".</para>
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
			<para>Equivalent to the CLI command "dahdi restart".</para>
		</description>
	</manager>
	<manager name="PRIShowSpans" language="en_US">
		<synopsis>
			Show status of PRI spans.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Span">
				<para>Specify the specific span to show.  Show all spans if zero or not present.</para>
			</parameter>
		</syntax>
		<description>
			<para>Similar to the CLI command "pri show spans".</para>
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

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in chan_dahdi.conf.sample */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};
static struct ast_jb_conf global_jbconf;

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

/*! \brief Typically, how many rings before we should send Caller*ID */
#define DEFAULT_CIDRINGS 1

#define AST_LAW(p) (((p)->law == DAHDI_LAW_ALAW) ? AST_FORMAT_ALAW : AST_FORMAT_ULAW)


/*! \brief Signaling types that need to use MF detection should be placed in this macro */
#define NEED_MFDETECT(p) (((p)->sig == SIG_FEATDMF) || ((p)->sig == SIG_FEATDMF_TA) || ((p)->sig == SIG_E911) || ((p)->sig == SIG_FGC_CAMA) || ((p)->sig == SIG_FGC_CAMAMF) || ((p)->sig == SIG_FEATB))

static const char tdesc[] = "DAHDI Telephony Driver"
#if defined(HAVE_PRI) || defined(HAVE_SS7) || defined(HAVE_OPENR2)
	" w/"
	#if defined(HAVE_PRI)
		"PRI"
	#endif	/* defined(HAVE_PRI) */
	#if defined(HAVE_SS7)
		#if defined(HAVE_PRI)
		" & "
		#endif	/* defined(HAVE_PRI) */
		"SS7"
	#endif	/* defined(HAVE_SS7) */
	#if defined(HAVE_OPENR2)
		#if defined(HAVE_PRI) || defined(HAVE_SS7)
		" & "
		#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
		"MFC/R2"
	#endif	/* defined(HAVE_OPENR2) */
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) || defined(HAVE_OPENR2) */
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

static char defaultcic[64] = "";
static char defaultozz[64] = "";

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
static int dtmfcid_level = 256;

#define REPORT_CHANNEL_ALARMS 1
#define REPORT_SPAN_ALARMS    2 
static int report_alarms = REPORT_CHANNEL_ALARMS;

#ifdef HAVE_PRI
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

#define CALLWAITING_SILENT_SAMPLES		((300 * 8) / READ_SIZE) /*!< 300 ms */
#define CALLWAITING_REPEAT_SAMPLES		((10000 * 8) / READ_SIZE) /*!< 10,000 ms */
#define CALLWAITING_SUPPRESS_SAMPLES	((100 * 8) / READ_SIZE) /*!< 100 ms */
#define CIDCW_EXPIRE_SAMPLES			((500 * 8) / READ_SIZE) /*!< 500 ms */
#define MIN_MS_SINCE_FLASH				((2000) )	/*!< 2000 ms */
#define DEFAULT_RINGT 					((8000 * 8) / READ_SIZE) /*!< 8,000 ms */
#define DEFAULT_DIALTONE_DETECT_TIMEOUT ((10000 * 8) / READ_SIZE) /*!< 10,000 ms */

struct dahdi_pvt;

/*!
 * \brief Configured ring timeout base.
 * \note Value computed from "ringtimeout" read in from chan_dahdi.conf if it exists.
 */
static int ringt_base = DEFAULT_RINGT;

#if defined(HAVE_SS7)

struct dahdi_ss7 {
	struct sig_ss7_linkset ss7;
};

static struct dahdi_ss7 linksets[NUM_SPANS];

static int cur_ss7type = -1;
static int cur_linkset = -1;
static int cur_pointcode = -1;
static int cur_cicbeginswith = -1;
static int cur_adjpointcode = -1;
static int cur_networkindicator = -1;
static int cur_defaultdpc = -1;
#endif	/* defined(HAVE_SS7) */

#ifdef HAVE_OPENR2
struct dahdi_mfcr2_conf {
	openr2_variant_t variant;
	int mfback_timeout;
	int metering_pulse_timeout;
	int max_ani;
	int max_dnis;
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 2
	int dtmf_time_on;
	int dtmf_time_off;
#endif
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 3
	int dtmf_end_timeout;
#endif
	signed int get_ani_first:2;
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
	signed int skip_category_request:2;
#endif
	unsigned int call_files:1;
	unsigned int allow_collect_calls:1;
	unsigned int charge_calls:1;
	unsigned int accept_on_offer:1;
	unsigned int forced_release:1;
	unsigned int double_answer:1;
	signed int immediate_accept:2;
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 2
	signed int dtmf_dialing:2;
	signed int dtmf_detection:2;
#endif
	char logdir[OR2_MAX_PATH];
	char r2proto_file[OR2_MAX_PATH];
	openr2_log_level_t loglevel;
	openr2_calling_party_category_t category;
};

/* MFC-R2 pseudo-link structure */
struct dahdi_mfcr2 {
	pthread_t r2master;		       /*!< Thread of master */
	openr2_context_t *protocol_context;    /*!< OpenR2 context handle */
	struct dahdi_pvt *pvts[SIG_MFCR2_MAX_CHANNELS];     /*!< Member channel pvt structs */
	int numchans;                          /*!< Number of channels in this R2 block */
	struct dahdi_mfcr2_conf conf;         /*!< Configuration used to setup this pseudo-link */
};

/* malloc'd array of malloc'd r2links */
static struct dahdi_mfcr2 **r2links;
/* how many r2links have been malloc'd */
static int r2links_count = 0;

#endif /* HAVE_OPENR2 */

#ifdef HAVE_PRI

struct dahdi_pri {
	int dchannels[SIG_PRI_NUM_DCHANS];		/*!< What channel are the dchannels on */
	int mastertrunkgroup;					/*!< What trunk group is our master */
	int prilogicalspan;						/*!< Logical span number within trunk group */
	struct sig_pri_span pri;
};

static struct dahdi_pri pris[NUM_SPANS];

#if defined(HAVE_PRI_CCSS)
/*! DAHDI PRI CCSS agent and monitor type name. */
static const char dahdi_pri_cc_type[] = "DAHDI/PRI";
#endif	/* defined(HAVE_PRI_CCSS) */

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
	 * \note Applies to SS7 and MFCR2 channels.
	 */
	unsigned int locallyblocked:1;
	/*!
	 * \brief TRUE if the channel is remotely blocked.
	 * \note Applies to SS7 and MFCR2 channels.
	 */
	unsigned int remotelyblocked:1;
	/*!
	 * \brief TRUE if the channel alarms will be managed also as Span ones
	 * \note Applies to all channels
	 */
	unsigned int manages_span_alarms:1;

#if defined(HAVE_PRI)
	struct sig_pri_span *pri;
	int logicalspan;
#endif
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
	char mailbox[AST_MAX_EXTENSION];
	/*! \brief Opaque event subscription parameters for message waiting indication support. */
	struct ast_event_sub *mwi_event_sub;
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
	int mfcr2_progress_sent:1;
#endif
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

#define DATA_EXPORT_DAHDI_PVT(MEMBER)					\
	MEMBER(dahdi_pvt, cid_rxgain, AST_DATA_DOUBLE)			\
	MEMBER(dahdi_pvt, rxgain, AST_DATA_DOUBLE)			\
	MEMBER(dahdi_pvt, txgain, AST_DATA_DOUBLE)			\
	MEMBER(dahdi_pvt, txdrc, AST_DATA_DOUBLE)			\
	MEMBER(dahdi_pvt, rxdrc, AST_DATA_DOUBLE)			\
	MEMBER(dahdi_pvt, adsi, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, answeronpolarityswitch, AST_DATA_BOOLEAN)	\
	MEMBER(dahdi_pvt, busydetect, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, callreturn, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, callwaiting, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, callwaitingcallerid, AST_DATA_BOOLEAN)	\
	MEMBER(dahdi_pvt, cancallforward, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, canpark, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, confirmanswer, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, destroy, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, didtdd, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, dialednone, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, dialing, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, digital, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, dnd, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, echobreak, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, echocanbridged, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, echocanon, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, faxhandled, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, usefaxbuffers, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, bufferoverrideinuse, AST_DATA_BOOLEAN)	\
	MEMBER(dahdi_pvt, firstradio, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, hanguponpolarityswitch, AST_DATA_BOOLEAN)	\
	MEMBER(dahdi_pvt, hardwaredtmf, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, hidecallerid, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, hidecalleridname, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, ignoredtmf, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, immediate, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, inalarm, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, mate, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, outgoing, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, permcallwaiting, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, priindication_oob, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, priexclusive, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, pulse, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, pulsedial, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, restartpending, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, restrictcid, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, threewaycalling, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, transfer, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, use_callerid, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, use_callingpres, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, usedistinctiveringdetection, AST_DATA_BOOLEAN)	\
	MEMBER(dahdi_pvt, dahditrcallerid, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, transfertobusy, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, mwimonitor_neon, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, mwimonitor_fsk, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, mwimonitor_rpas, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, mwimonitoractive, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, mwisendactive, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, inservice, AST_DATA_BOOLEAN)				\
	MEMBER(dahdi_pvt, locallyblocked, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, remotelyblocked, AST_DATA_BOOLEAN)			\
	MEMBER(dahdi_pvt, manages_span_alarms, AST_DATA_BOOLEAN)		\
	MEMBER(dahdi_pvt, use_smdi, AST_DATA_BOOLEAN)				\
	MEMBER(dahdi_pvt, context, AST_DATA_STRING)				\
	MEMBER(dahdi_pvt, defcontext, AST_DATA_STRING)				\
	MEMBER(dahdi_pvt, description, AST_DATA_STRING)				\
	MEMBER(dahdi_pvt, exten, AST_DATA_STRING)				\
	MEMBER(dahdi_pvt, language, AST_DATA_STRING)				\
	MEMBER(dahdi_pvt, mohinterpret, AST_DATA_STRING)			\
	MEMBER(dahdi_pvt, mohsuggest, AST_DATA_STRING)				\
	MEMBER(dahdi_pvt, parkinglot, AST_DATA_STRING)

AST_DATA_STRUCTURE(dahdi_pvt, DATA_EXPORT_DAHDI_PVT);

static struct dahdi_pvt *iflist = NULL;	/*!< Main interface list start */
static struct dahdi_pvt *ifend = NULL;	/*!< Main interface list end */

#if defined(HAVE_PRI)
static struct dahdi_parms_pseudo {
	int buf_no;					/*!< Number of buffers */
	int buf_policy;				/*!< Buffer policy */
	int faxbuf_no;              /*!< Number of Fax buffers */
	int faxbuf_policy;          /*!< Fax buffer policy */
} dahdi_pseudo_parms;
#endif	/* defined(HAVE_PRI) */

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

#if defined(HAVE_SS7)
	struct dahdi_ss7 ss7;
#endif	/* defined(HAVE_SS7) */

#ifdef HAVE_OPENR2
	struct dahdi_mfcr2_conf mfcr2;
#endif
	struct dahdi_params timing;
	int is_sig_auto; /*!< Use channel signalling from DAHDI? */
	/*! Continue configuration even if a channel is not there. */
	int ignore_failed_channels;

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
			.inband_on_setup_ack = 1,
			.inband_on_proceeding = 1,

#if defined(HAVE_PRI_CCSS)
			.cc_ptmp_recall_mode = 1,/* specificRecall */
			.cc_qsig_signaling_link_req = 1,/* retain */
			.cc_qsig_signaling_link_rsp = 1,/* retain */
#endif	/* defined(HAVE_PRI_CCSS) */

			.minunused = 2,
			.idleext = "",
			.idledial = "",
			.internationalprefix = "",
			.nationalprefix = "",
			.localprefix = "",
			.privateprefix = "",
			.unknownprefix = "",
			.colp_send = SIG_PRI_COLP_UPDATE,
			.force_restart_unavailable_chans = 1,
			.resetinterval = -1,
		},
#endif
#if defined(HAVE_SS7)
		.ss7.ss7 = {
			.called_nai = SS7_NAI_NATIONAL,
			.calling_nai = SS7_NAI_NATIONAL,
			.internationalprefix = "",
			.nationalprefix = "",
			.subscriberprefix = "",
			.unknownprefix = ""
		},
#endif	/* defined(HAVE_SS7) */
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
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 2
			.dtmf_dialing = -1,
			.dtmf_detection = -1,
			.dtmf_time_on = OR2_DEFAULT_DTMF_ON,
			.dtmf_time_off = OR2_DEFAULT_DTMF_OFF,
#endif
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 3
			.dtmf_end_timeout = -1,
#endif
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
			.cid_tag = "",
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
			.cc_params = ast_cc_config_params_init(),
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


static struct ast_channel *dahdi_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
static int dahdi_digit_begin(struct ast_channel *ast, char digit);
static int dahdi_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int dahdi_sendtext(struct ast_channel *c, const char *text);
static int dahdi_call(struct ast_channel *ast, const char *rdest, int timeout);
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
static int dahdi_func_write(struct ast_channel *chan, const char *function, char *data, const char *value);
static int dahdi_devicestate(const char *data);
static int dahdi_cc_callback(struct ast_channel *inbound, const char *dest, ast_cc_callback_fn callback);

static struct ast_channel_tech dahdi_tech = {
	.type = "DAHDI",
	.description = tdesc,
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
	.devicestate = dahdi_devicestate,
	.cc_callback = dahdi_cc_callback,
};

#define GET_CHANNEL(p) ((p)->channel)

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

static int analog_lib_handles(int signalling, int radio, int oprmode)
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

static int restore_gains(struct dahdi_pvt *p);

static int my_stop_cid_detect(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int index = SUB_REAL;

	if (p->cs) {
		callerid_free(p->cs);
	}

	/* Restore linear mode after Caller*ID processing */
	dahdi_setlinear(p->subs[index].dfd, p->subs[index].linear);
	restore_gains(p);

	return 0;
}

static int my_get_callerid(void *pvt, char *namebuf, char *numbuf, enum analog_event *ev, size_t timeout)
{
	struct dahdi_pvt *p = pvt;
	struct analog_pvt *analog_p = p->sig_pvt;
	struct pollfd poller;
	char *name, *num;
	int index = SUB_REAL;
	int res;
	unsigned char buf[256];
	int flags;
	struct ast_format tmpfmt;

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
		 * to enable slin mode and allocate cid detector. get_callerid should be able to be called any number of times until
		 * either a timeout occurs or CID is detected (returns 0). returning 1 should be event received, and -1 should be
		 * a failure and die, and returning 2 means no event was received. */
		res = read(p->subs[index].dfd, buf, sizeof(buf));
		if (res < 0) {
			ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
			return -1;
		}

		if (analog_p->ringt > 0) {
			if (!(--analog_p->ringt)) {
				/* only return if we timeout from a ring event */
				return -1;
			}
		}

		if (p->cid_signalling == CID_SIG_V23_JP) {
			res = callerid_feed_jp(p->cs, buf, res, ast_format_set(&tmpfmt, AST_LAW(p), 0));
		} else {
			res = callerid_feed(p->cs, buf, res, ast_format_set(&tmpfmt, AST_LAW(p), 0));
		}
		if (res < 0) {
			/*
			 * The previous diagnostic message output likely
			 * explains why it failed.
			 */
			ast_log(LOG_WARNING, "Failed to decode CallerID\n");
			return -1;
		}

		if (res == 1) {
			callerid_get(p->cs, &name, &num, &flags);
			if (name)
				ast_copy_string(namebuf, name, ANALOG_MAX_CID);
			if (num)
				ast_copy_string(numbuf, num, ANALOG_MAX_CID);

			ast_debug(1, "CallerID number: %s, name: %s, flags=%d\n", num, name, flags);
			return 0;
		}
	}

	*ev = ANALOG_EVENT_NONE;
	return 2;
}

static const char *event2str(int event);

static int my_distinctive_ring(struct ast_channel *chan, void *pvt, int idx, int *ringdata)
{
	unsigned char buf[256];
	int distMatches;
	int curRingData[RING_PATTERNS];
	int receivedRingT;
	int counter1;
	int counter;
	int i;
	int res;
	int checkaftercid = 0;
	const char *matched_context;
	struct dahdi_pvt *p = pvt;
	struct analog_pvt *analog_p = p->sig_pvt;

	if (ringdata == NULL) {
		ringdata = curRingData;
	} else {
		checkaftercid = 1;
	}

	/* We must have a ring by now so lets try to listen for distinctive ringing */
	if ((checkaftercid && distinctiveringaftercid) || !checkaftercid) {
		/* Clear the current ring data array so we don't have old data in it. */
		for (receivedRingT = 0; receivedRingT < RING_PATTERNS; receivedRingT++)
			ringdata[receivedRingT] = 0;
		receivedRingT = 0;

		if (checkaftercid && distinctiveringaftercid) {
			ast_verb(3, "Detecting post-CID distinctive ring\n");
		}

		for (;;) {
			i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
			res = ioctl(p->subs[idx].dfd, DAHDI_IOMUX, &i);
			if (res) {
				ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
				ast_hangup(chan);
				return 1;
			}
			if (i & DAHDI_IOMUX_SIGEVENT) {
				res = dahdi_get_event(p->subs[idx].dfd);
				ast_debug(3, "Got event %d (%s)...\n", res, event2str(res));
				if (res == DAHDI_EVENT_NOALARM) {
					p->inalarm = 0;
					analog_p->inalarm = 0;
				} else if (res == DAHDI_EVENT_RINGOFFHOOK) {
					/* Let us detect distinctive ring */
					ringdata[receivedRingT] = analog_p->ringt;

					if (analog_p->ringt < analog_p->ringt_base / 2) {
						break;
					}
					/* Increment the ringT counter so we can match it against
					   values in chan_dahdi.conf for distinctive ring */
					if (++receivedRingT == RING_PATTERNS) {
						break;
					}
				}
			} else if (i & DAHDI_IOMUX_READ) {
				res = read(p->subs[idx].dfd, buf, sizeof(buf));
				if (res < 0) {
					if (errno != ELAST) {
						ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
						ast_hangup(chan);
						return 1;
					}
					break;
				}
				if (analog_p->ringt > 0) {
					if (!(--analog_p->ringt)) {
						break;
					}
				}
			}
		}
	}

	/* Check to see if the rings we received match any of the ones in chan_dahdi.conf for this channel */
	ast_verb(3, "Detected ring pattern: %d,%d,%d\n", ringdata[0], ringdata[1], ringdata[2]);
	matched_context = p->defcontext;
	for (counter = 0; counter < 3; counter++) {
		int range = p->drings.ringnum[counter].range;

		distMatches = 0;
		ast_verb(3, "Checking %d,%d,%d with +/- %d range\n",
			p->drings.ringnum[counter].ring[0],
			p->drings.ringnum[counter].ring[1],
			p->drings.ringnum[counter].ring[2],
			range);
		for (counter1 = 0; counter1 < 3; counter1++) {
			int ring = p->drings.ringnum[counter].ring[counter1];

			if (ring == -1) {
				ast_verb(3, "Pattern ignore (-1) detected, so matching pattern %d regardless.\n",
					ringdata[counter1]);
				distMatches++;
			} else if (ring - range <= ringdata[counter1] && ringdata[counter1] <= ring + range) {
				ast_verb(3, "Ring pattern %d is in range: %d to %d\n",
					ringdata[counter1], ring - range, ring + range);
				distMatches++;
			} else {
				/* The current dring pattern cannot match. */
				break;
			}
		}

		if (distMatches == 3) {
			/* The ring matches, set the context to whatever is for distinctive ring.. */
			matched_context = S_OR(p->drings.ringContext[counter].contextData, p->defcontext);
			ast_verb(3, "Matched Distinctive Ring context %s\n", matched_context);
			break;
		}
	}

	/* Set selected distinctive ring context if not already set. */
	if (strcmp(p->context, matched_context) != 0) {
		ast_copy_string(p->context, matched_context, sizeof(p->context));
		ast_channel_context_set(chan, matched_context);
	}

	return 0;
}

static int my_stop_callwait(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	p->cid_suppress_expire = 0;

	return 0;
}

static int send_callerid(struct dahdi_pvt *p);
static int save_conference(struct dahdi_pvt *p);
static int restore_conference(struct dahdi_pvt *p);

static int my_callwait(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	struct ast_format tmpfmt;
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill) {
		ast_log(LOG_WARNING, "Spill already exists?!?\n");
		ast_free(p->cidspill);
	}

	/*
	 * SAS: Subscriber Alert Signal, 440Hz for 300ms
	 * CAS: CPE Alert Signal, 2130Hz * 2750Hz sine waves
	 */
	if (!(p->cidspill = ast_malloc(2400 /* SAS */ + 680 /* CAS */ + READ_SIZE * 4)))
		return -1;
	save_conference(p);
	/* Silence */
	memset(p->cidspill, 0x7f, 2400 + 600 + READ_SIZE * 4);
	if (!p->callwaitrings && p->callwaitingcallerid) {
		ast_gen_cas(p->cidspill, 1, 2400 + 680, ast_format_set(&tmpfmt, AST_LAW(p), 0));
		p->callwaitcas = 1;
		p->cidlen = 2400 + 680 + READ_SIZE * 4;
	} else {
		ast_gen_cas(p->cidspill, 1, 2400, ast_format_set(&tmpfmt, AST_LAW(p), 0));
		p->callwaitcas = 0;
		p->cidlen = 2400 + READ_SIZE * 4;
	}
	p->cidpos = 0;
	send_callerid(p);

	return 0;
}

static int my_send_callerid(void *pvt, int cwcid, struct ast_party_caller *caller)
{
	struct dahdi_pvt *p = pvt;
	struct ast_format tmpfmt;

	ast_debug(2, "Starting cid spill\n");

	if (p->cidspill) {
		ast_log(LOG_WARNING, "cidspill already exists??\n");
		ast_free(p->cidspill);
	}

	if ((p->cidspill = ast_malloc(MAX_CALLERID_SIZE))) {
		if (cwcid == 0) {
			p->cidlen = ast_callerid_generate(p->cidspill,
				caller->id.name.str,
				caller->id.number.str,
				ast_format_set(&tmpfmt, AST_LAW(p), 0));
		} else {
			ast_verb(3, "CPE supports Call Waiting Caller*ID.  Sending '%s/%s'\n",
				caller->id.name.str, caller->id.number.str);
			p->callwaitcas = 0;
			p->cidcwexpire = 0;
			p->cidlen = ast_callerid_callwaiting_generate(p->cidspill,
				caller->id.name.str,
				caller->id.number.str,
				ast_format_set(&tmpfmt, AST_LAW(p), 0));
			p->cidlen += READ_SIZE * 4;
		}
		p->cidpos = 0;
		p->cid_suppress_expire = 0;
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

static void my_handle_dtmf(void *pvt, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest)
{
	struct ast_frame *f = *dest;
	struct dahdi_pvt *p = pvt;
	int idx = analogsub_to_dahdisub(analog_index);

	ast_debug(1, "%s DTMF digit: 0x%02X '%c' on %s\n",
		f->frametype == AST_FRAME_DTMF_BEGIN ? "Begin" : "End",
		(unsigned)f->subclass.integer, f->subclass.integer, ast_channel_name(ast));

	if (f->subclass.integer == 'f') {
		if (f->frametype == AST_FRAME_DTMF_END) {
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
						ast_log(LOG_WARNING, "Channel '%s' unable to set buffer policy, reason: %s\n", ast_channel_name(ast), strerror(errno));
					} else {
						p->bufferoverrideinuse = 1;
					}
				}
				p->faxhandled = 1;
				if (p->dsp) {
					p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
					ast_dsp_set_features(p->dsp, p->dsp_features);
					ast_debug(1, "Disabling FAX tone detection on %s after tone received\n", ast_channel_name(ast));
				}
				if (strcmp(ast_channel_exten(ast), "fax")) {
					const char *target_context = S_OR(ast_channel_macrocontext(ast), ast_channel_context(ast));

					/* We need to unlock 'ast' here because ast_exists_extension has the
					 * potential to start autoservice on the channel. Such action is prone
					 * to deadlock.
					 */
					ast_mutex_unlock(&p->lock);
					ast_channel_unlock(ast);
					if (ast_exists_extension(ast, target_context, "fax", 1,
						S_COR(ast_channel_caller(ast)->id.number.valid, ast_channel_caller(ast)->id.number.str, NULL))) {
						ast_channel_lock(ast);
						ast_mutex_lock(&p->lock);
						ast_verb(3, "Redirecting %s to fax extension\n", ast_channel_name(ast));
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast_channel_exten(ast));
						if (ast_async_goto(ast, target_context, "fax", 1))
							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast_channel_name(ast), target_context);
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
		}
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
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

static void my_deadlock_avoidance_private(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	DEADLOCK_AVOIDANCE(&p->lock);
}

/*!
 * \internal
 * \brief Post an AMI DAHDI channel association event.
 * \since 1.8
 *
 * \param p DAHDI private pointer
 * \param chan Channel associated with the private pointer
 *
 * \return Nothing
 */
static void dahdi_ami_channel_event(struct dahdi_pvt *p, struct ast_channel *chan)
{
	char ch_name[20];

	if (p->channel < CHAN_PSEUDO) {
		/* No B channel */
		snprintf(ch_name, sizeof(ch_name), "no-media (%d)", p->channel);
	} else if (p->channel == CHAN_PSEUDO) {
		/* Pseudo channel */
		strcpy(ch_name, "pseudo");
	} else {
		/* Real channel */
		snprintf(ch_name, sizeof(ch_name), "%d", p->channel);
	}
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a DAHDI channel is created or an underlying technology is associated with a DAHDI channel.</synopsis>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "DAHDIChannel",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"DAHDISpan: %d\r\n"
		"DAHDIChannel: %s\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		p->span,
		ch_name);
}

#ifdef HAVE_PRI
/*!
 * \internal
 * \brief Post an AMI DAHDI channel association event.
 * \since 1.8
 *
 * \param pvt DAHDI private pointer
 * \param chan Channel associated with the private pointer
 *
 * \return Nothing
 */
static void my_ami_channel_event(void *pvt, struct ast_channel *chan)
{
	struct dahdi_pvt *p = pvt;

	dahdi_ami_channel_event(p, chan);
}
#endif

/* linear_mode = 0 - turn linear mode off, >0 - turn linear mode on
* 	returns the last value of the linear setting 
*/ 
static int my_set_linear_mode(void *pvt, enum analog_sub sub, int linear_mode)
{
	struct dahdi_pvt *p = pvt;
	int oldval;
	int idx = analogsub_to_dahdisub(sub);
	
	dahdi_setlinear(p->subs[idx].dfd, linear_mode);
	oldval = p->subs[idx].linear;
	p->subs[idx].linear = linear_mode ? 1 : 0;
	return oldval;
}

static void my_set_inthreeway(void *pvt, enum analog_sub sub, int inthreeway)
{
	struct dahdi_pvt *p = pvt;
	int idx = analogsub_to_dahdisub(sub);

	p->subs[idx].inthreeway = inthreeway;
}

static int get_alarms(struct dahdi_pvt *p);
static void handle_alarms(struct dahdi_pvt *p, int alms);
static void my_get_and_handle_alarms(void *pvt)
{
	int res;
	struct dahdi_pvt *p = pvt;

	res = get_alarms(p);
	handle_alarms(p, res);
}

static void *my_get_sigpvt_bridged_channel(struct ast_channel *chan)
{
	struct ast_channel *bridged = ast_bridged_channel(chan);

	if (bridged && ast_channel_tech(bridged) == &dahdi_tech) {
		struct dahdi_pvt *p = ast_channel_tech_pvt(bridged);

		if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
			return p->sig_pvt;
		}
	}
	return NULL;
}

static int my_get_sub_fd(void *pvt, enum analog_sub sub)
{
	struct dahdi_pvt *p = pvt;
	int dahdi_sub = analogsub_to_dahdisub(sub);
	return p->subs[dahdi_sub].dfd;
}

static void my_set_cadence(void *pvt, int *cid_rings, struct ast_channel *ast)
{
	struct dahdi_pvt *p = pvt;

	/* Choose proper cadence */
	if ((p->distinctivering > 0) && (p->distinctivering <= num_cadence)) {
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCADENCE, &cadences[p->distinctivering - 1]))
			ast_log(LOG_WARNING, "Unable to set distinctive ring cadence %d on '%s': %s\n", p->distinctivering, ast_channel_name(ast), strerror(errno));
		*cid_rings = cidrings[p->distinctivering - 1];
	} else {
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCADENCE, NULL))
			ast_log(LOG_WARNING, "Unable to reset default ring on '%s': %s\n", ast_channel_name(ast), strerror(errno));
		*cid_rings = p->sendcalleridafter;
	}
}

static void my_set_alarm(void *pvt, int in_alarm)
{
	struct dahdi_pvt *p = pvt;

	p->inalarm = in_alarm;
}

static void my_set_dialing(void *pvt, int is_dialing)
{
	struct dahdi_pvt *p = pvt;

	p->dialing = is_dialing;
}

static void my_set_outgoing(void *pvt, int is_outgoing)
{
	struct dahdi_pvt *p = pvt;

	p->outgoing = is_outgoing;
}

#if defined(HAVE_PRI) || defined(HAVE_SS7)
static void my_set_digital(void *pvt, int is_digital)
{
	struct dahdi_pvt *p = pvt;

	p->digital = is_digital;
}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void my_set_inservice(void *pvt, int is_inservice)
{
	struct dahdi_pvt *p = pvt;

	p->inservice = is_inservice;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void my_set_locallyblocked(void *pvt, int is_blocked)
{
	struct dahdi_pvt *p = pvt;

	p->locallyblocked = is_blocked;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void my_set_remotelyblocked(void *pvt, int is_blocked)
{
	struct dahdi_pvt *p = pvt;

	p->remotelyblocked = is_blocked;
}
#endif	/* defined(HAVE_SS7) */

static void my_set_ringtimeout(void *pvt, int ringt)
{
	struct dahdi_pvt *p = pvt;
	p->ringt = ringt;
}

static void my_set_waitingfordt(void *pvt, struct ast_channel *ast)
{
	struct dahdi_pvt *p = pvt;

	if (p->waitfordialtone && CANPROGRESSDETECT(p) && p->dsp) {
		ast_debug(1, "Defer dialing for %dms or dialtone\n", p->waitfordialtone);
		gettimeofday(&p->waitingfordt, NULL);
		ast_setstate(ast, AST_STATE_OFFHOOK);
	}
}

static int my_check_waitingfordt(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	if (p->waitingfordt.tv_sec) {
		return 1;
	}

	return 0;
}

static void my_set_confirmanswer(void *pvt, int flag)
{
	struct dahdi_pvt *p = pvt;
	p->confirmanswer = flag;
}

static int my_check_confirmanswer(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	if (p->confirmanswer) {
		return 1;
	}

	return 0;
}

static void my_set_callwaiting(void *pvt, int callwaiting_enable)
{
	struct dahdi_pvt *p = pvt;

	p->callwaiting = callwaiting_enable;
}

static void my_cancel_cidspill(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	ast_free(p->cidspill);
	p->cidspill = NULL;
	restore_conference(p);
}

static int my_confmute(void *pvt, int mute)
{
	struct dahdi_pvt *p = pvt;
	return dahdi_confmute(p, mute);
}

static void my_set_pulsedial(void *pvt, int flag)
{
	struct dahdi_pvt *p = pvt;
	p->pulsedial = flag;
}

static void my_set_new_owner(void *pvt, struct ast_channel *new_owner)
{
	struct dahdi_pvt *p = pvt;

	p->owner = new_owner;
}

static const char *my_get_orig_dialstring(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	return p->dialstring;
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

	p->law = p->law_default;
	law = p->law_default;
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
	int tinthreeway;

	da = analogsub_to_dahdisub(a);
	db = analogsub_to_dahdisub(b);

	tchan = p->subs[da].chan;
	p->subs[da].chan = p->subs[db].chan;
	p->subs[db].chan = tchan;

	tinthreeway = p->subs[da].inthreeway;
	p->subs[da].inthreeway = p->subs[db].inthreeway;
	p->subs[db].inthreeway = tinthreeway;

	p->subs[da].owner = ast_a;
	p->subs[db].owner = ast_b;

	if (ast_a)
		ast_channel_set_fd(ast_a, 0, p->subs[da].dfd);
	if (ast_b)
		ast_channel_set_fd(ast_b, 0, p->subs[db].dfd);

	wakeup_sub(p, a);
	wakeup_sub(p, b);

	return;
}

/*!
 * \internal
 * \brief performs duties of dahdi_new, but also removes and possibly unbinds (if callid_created is 1) before returning
 * \note this variant of dahdi should only be used in conjunction with ast_callid_threadstorage_auto()
 *
 * \param callid_created value returned from ast_callid_threadstorage_auto()
 */
static struct ast_channel *dahdi_new_callid_clean(struct dahdi_pvt *i, int state, int startpbx, int idx, int law, const char *linked, struct ast_callid *callid, int callid_created);

static struct ast_channel *dahdi_new(struct dahdi_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid, struct ast_callid *callid);

static struct ast_channel *my_new_analog_ast_channel(void *pvt, int state, int startpbx, enum analog_sub sub, const struct ast_channel *requestor)
{
	struct ast_callid *callid = NULL;
	int callid_created = ast_callid_threadstorage_auto(&callid);
	struct dahdi_pvt *p = pvt;
	int dsub = analogsub_to_dahdisub(sub);

	return dahdi_new_callid_clean(p, state, startpbx, dsub, 0, requestor ? ast_channel_linkedid(requestor) : "", callid, callid_created);
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
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

#if defined(HAVE_PRI)
static struct ast_channel *my_new_pri_ast_channel(void *pvt, int state, enum sig_pri_law law, char *exten, const struct ast_channel *requestor)
{
	struct dahdi_pvt *p = pvt;
	int audio;
	int newlaw = -1;
	struct ast_callid *callid = NULL;
	int callid_created = ast_callid_threadstorage_auto(&callid);

	switch (p->sig) {
	case SIG_PRI_LIB_HANDLE_CASES:
		if (((struct sig_pri_chan *) p->sig_pvt)->no_b_channel) {
			/* PRI nobch pseudo channel.  Does not handle ioctl(DAHDI_AUDIOMODE) */
			break;
		}
		/* Fall through */
	default:
		/* Set to audio mode at this point */
		audio = 1;
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &audio) == -1) {
			ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n",
				p->channel, audio, strerror(errno));
		}
		break;
	}

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

	return dahdi_new_callid_clean(p, state, 0, SUB_REAL, newlaw, requestor ? ast_channel_linkedid(requestor) : "", callid, callid_created);
}
#endif	/* defined(HAVE_PRI) */

static int set_actual_gain(int fd, float rxgain, float txgain, float rxdrc, float txdrc, int law);

#if defined(HAVE_PRI) || defined(HAVE_SS7)
/*!
 * \internal
 * \brief Open the PRI/SS7 channel media path.
 * \since 1.8
 *
 * \param p Channel private control structure.
 *
 * \return Nothing
 */
static void my_pri_ss7_open_media(void *p)
{
	struct dahdi_pvt *pvt = p;
	int res;
	int dfd;
	int set_val;

	dfd = pvt->subs[SUB_REAL].dfd;

	/* Open the media path. */
	set_val = 1;
	res = ioctl(dfd, DAHDI_AUDIOMODE, &set_val);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to enable audio mode on channel %d (%s)\n",
			pvt->channel, strerror(errno));
	}

	/* Set correct companding law for this call. */
	res = dahdi_setlaw(dfd, pvt->law);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set law on channel %d\n", pvt->channel);
	}

	/* Set correct gain for this call. */
	if (pvt->digital) {
		res = set_actual_gain(dfd, 0, 0, pvt->rxdrc, pvt->txdrc, pvt->law);
	} else {
		res = set_actual_gain(dfd, pvt->rxgain, pvt->txgain, pvt->rxdrc, pvt->txdrc,
			pvt->law);
	}
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set gains on channel %d\n", pvt->channel);
	}

	if (pvt->dsp_features && pvt->dsp) {
		ast_dsp_set_features(pvt->dsp, pvt->dsp_features);
		pvt->dsp_features = 0;
	}
}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Ask DAHDI to dial the given dial string.
 * \since 1.8.11
 *
 * \param p Channel private control structure.
 * \param dial_string String to pass to DAHDI to dial.
 *
 * \note The channel private lock needs to be held when calling.
 *
 * \return Nothing
 */
static void my_pri_dial_digits(void *p, const char *dial_string)
{
	struct dahdi_dialoperation zo = {
		.op = DAHDI_DIAL_OP_APPEND,
	};
	struct dahdi_pvt *pvt = p;
	int res;

	snprintf(zo.dialstr, sizeof(zo.dialstr), "T%s", dial_string);
	ast_debug(1, "Channel %d: Sending '%s' to DAHDI_DIAL.\n", pvt->channel, zo.dialstr);
	res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_DIAL, &zo);
	if (res) {
		ast_log(LOG_WARNING, "Channel %d: Couldn't dial '%s': %s\n",
			pvt->channel, dial_string, strerror(errno));
	} else {
		pvt->dialing = 1;
	}
}
#endif	/* defined(HAVE_PRI) */

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
	enum analog_event res;

	switch (event) {
	case DAHDI_EVENT_ONHOOK:
		res = ANALOG_EVENT_ONHOOK;
		break;
	case DAHDI_EVENT_RINGOFFHOOK:
		res = ANALOG_EVENT_RINGOFFHOOK;
		break;
	case DAHDI_EVENT_WINKFLASH:
		res = ANALOG_EVENT_WINKFLASH;
		break;
	case DAHDI_EVENT_ALARM:
		res = ANALOG_EVENT_ALARM;
		break;
	case DAHDI_EVENT_NOALARM:
		res = ANALOG_EVENT_NOALARM;
		break;
	case DAHDI_EVENT_DIALCOMPLETE:
		res = ANALOG_EVENT_DIALCOMPLETE;
		break;
	case DAHDI_EVENT_RINGERON:
		res = ANALOG_EVENT_RINGERON;
		break;
	case DAHDI_EVENT_RINGEROFF:
		res = ANALOG_EVENT_RINGEROFF;
		break;
	case DAHDI_EVENT_HOOKCOMPLETE:
		res = ANALOG_EVENT_HOOKCOMPLETE;
		break;
	case DAHDI_EVENT_PULSE_START:
		res = ANALOG_EVENT_PULSE_START;
		break;
	case DAHDI_EVENT_POLARITY:
		res = ANALOG_EVENT_POLARITY;
		break;
	case DAHDI_EVENT_RINGBEGIN:
		res = ANALOG_EVENT_RINGBEGIN;
		break;
	case DAHDI_EVENT_EC_DISABLED:
		res = ANALOG_EVENT_EC_DISABLED;
		break;
	case DAHDI_EVENT_REMOVED:
		res = ANALOG_EVENT_REMOVED;
		break;
	case DAHDI_EVENT_NEONMWI_ACTIVE:
		res = ANALOG_EVENT_NEONMWI_ACTIVE;
		break;
	case DAHDI_EVENT_NEONMWI_INACTIVE:
		res = ANALOG_EVENT_NEONMWI_INACTIVE;
		break;
#ifdef HAVE_DAHDI_ECHOCANCEL_FAX_MODE
	case DAHDI_EVENT_TX_CED_DETECTED:
		res = ANALOG_EVENT_TX_CED_DETECTED;
		break;
	case DAHDI_EVENT_RX_CED_DETECTED:
		res = ANALOG_EVENT_RX_CED_DETECTED;
		break;
	case DAHDI_EVENT_EC_NLP_DISABLED:
		res = ANALOG_EVENT_EC_NLP_DISABLED;
		break;
	case DAHDI_EVENT_EC_NLP_ENABLED:
		res = ANALOG_EVENT_EC_NLP_ENABLED;
		break;
#endif
	case DAHDI_EVENT_PULSEDIGIT:
		res = ANALOG_EVENT_PULSEDIGIT;
		break;
	case DAHDI_EVENT_DTMFDOWN:
		res = ANALOG_EVENT_DTMFDOWN;
		break;
	case DAHDI_EVENT_DTMFUP:
		res = ANALOG_EVENT_DTMFUP;
		break;
	default:
		switch(event & 0xFFFF0000) {
		case DAHDI_EVENT_PULSEDIGIT:
		case DAHDI_EVENT_DTMFDOWN:
		case DAHDI_EVENT_DTMFUP:
			/* The event includes a digit number in the low word.
			 * Converting it to a 'enum analog_event' would remove
			 * that information. Thus it is returned as-is.
			 */
			return event;
		}

		res = ANALOG_EVENT_ERROR;
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

	memset(&par, 0, sizeof(par));

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

	if ((p->sig == SIG_FXSKS) || (p->sig == SIG_FXSGS)) {
		/* When "onhook" that means no battery on the line, and thus
		it is out of service..., if it's on a TDM card... If it's a channel
		bank, there is no telling... */
		return (par.rxbits > -1) || par.rxisoffhook;
	}

	return par.rxisoffhook;
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

static int my_flash(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	int func = DAHDI_FLASH;
	return ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &func);
}

static inline int dahdi_set_hook(int fd, int hs);

static int my_off_hook(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	return dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
}

static void my_set_needringing(void *pvt, int value)
{
	struct dahdi_pvt *p = pvt;
	p->subs[SUB_REAL].needringing = value;
}

static void my_set_polarity(void *pvt, int value)
{
	struct dahdi_pvt *p = pvt;

	if (p->channel == CHAN_PSEUDO) {
		return;
	}
	p->polarity = value;
	ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETPOLARITY, &value);
}

static void my_start_polarityswitch(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	if (p->answeronpolarityswitch || p->hanguponpolarityswitch) {
		my_set_polarity(pvt, 0);
	}
}

static void my_answer_polarityswitch(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	if (!p->answeronpolarityswitch) {
		return;
	}

	my_set_polarity(pvt, 1);
}

static void my_hangup_polarityswitch(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	if (!p->hanguponpolarityswitch) {
		return;
	}

	if (p->answeronpolarityswitch) {
		my_set_polarity(pvt, 0);
	} else {
		my_set_polarity(pvt, 1);
	}
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

	if (sub != ANALOG_SUB_REAL) {
		ast_log(LOG_ERROR, "Trying to dial_digits '%s' on channel %d subchannel %u\n",
			dop->dialstr, p->channel, sub);
		return -1;
	}

	ddop.op = DAHDI_DIAL_OP_REPLACE;
	ast_copy_string(ddop.dialstr, dop->dialstr, sizeof(ddop.dialstr));

	ast_debug(1, "Channel %d: Sending '%s' to DAHDI_DIAL.\n", p->channel, ddop.dialstr);

	res = ioctl(p->subs[index].dfd, DAHDI_DIAL, &ddop);
	if (res == -1) {
		ast_debug(1, "DAHDI_DIAL ioctl failed on %s: %s\n", ast_channel_name(p->owner), strerror(errno));
	}

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
		ast_debug(1, "DAHDI_DIALING ioctl failed!\n");
		return -1;
	}

	return x;
}

static int my_on_hook(void *pvt)
{
	struct dahdi_pvt *p = pvt;
	return dahdi_set_hook(p->subs[ANALOG_SUB_REAL].dfd, DAHDI_ONHOOK);
}

#if defined(HAVE_PRI)
static void my_pri_fixup_chans(void *chan_old, void *chan_new)
{
	struct dahdi_pvt *old_chan = chan_old;
	struct dahdi_pvt *new_chan = chan_new;

	new_chan->owner = old_chan->owner;
	old_chan->owner = NULL;
	if (new_chan->owner) {
		ast_channel_tech_pvt_set(new_chan->owner, new_chan);
		ast_channel_internal_fd_set(new_chan->owner, 0, new_chan->subs[SUB_REAL].dfd);
		new_chan->subs[SUB_REAL].owner = old_chan->subs[SUB_REAL].owner;
		old_chan->subs[SUB_REAL].owner = NULL;
	}
	/* Copy any DSP that may be present */
	new_chan->dsp = old_chan->dsp;
	new_chan->dsp_features = old_chan->dsp_features;
	old_chan->dsp = NULL;
	old_chan->dsp_features = 0;

	/* Transfer flags from the old channel. */
	new_chan->dialing = old_chan->dialing;
	new_chan->digital = old_chan->digital;
	new_chan->outgoing = old_chan->outgoing;
	old_chan->dialing = 0;
	old_chan->digital = 0;
	old_chan->outgoing = 0;

	/* More stuff to transfer to the new channel. */
	new_chan->law = old_chan->law;
	strcpy(new_chan->dialstring, old_chan->dialstring);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static int sig_pri_tone_to_dahditone(enum sig_pri_tone tone)
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
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static int pri_destroy_dchan(struct sig_pri_span *pri);

static void my_handle_dchan_exception(struct sig_pri_span *pri, int index)
{
	int x;

	ioctl(pri->fds[index], DAHDI_GETEVENT, &x);
	switch (x) {
	case DAHDI_EVENT_NONE:
		break;
	case DAHDI_EVENT_ALARM:
	case DAHDI_EVENT_NOALARM:
		if (sig_pri_is_alarm_ignored(pri)) {
			break;
		}
		/* Fall through */
	default:
		ast_log(LOG_NOTICE, "PRI got event: %s (%d) on D-channel of span %d\n",
			event2str(x), x, pri->span);
		break;
	}
	/* Keep track of alarm state */
	switch (x) {
	case DAHDI_EVENT_ALARM:
		pri_event_alarm(pri, index, 0);
		break;
	case DAHDI_EVENT_NOALARM:
		pri_event_noalarm(pri, index, 0);
		break;
	case DAHDI_EVENT_REMOVED:
		pri_destroy_dchan(pri);
		break;
	default:
		break;
	}
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static int my_pri_play_tone(void *pvt, enum sig_pri_tone tone)
{
	struct dahdi_pvt *p = pvt;

	return tone_zone_play_tone(p->subs[SUB_REAL].dfd, sig_pri_tone_to_dahditone(tone));
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI) || defined(HAVE_SS7)
/*!
 * \internal
 * \brief Set the caller id information.
 * \since 1.8
 *
 * \param pvt DAHDI private structure
 * \param caller Caller-id information to set.
 *
 * \return Nothing
 */
static void my_set_callerid(void *pvt, const struct ast_party_caller *caller)
{
	struct dahdi_pvt *p = pvt;

	ast_copy_string(p->cid_num,
		S_COR(caller->id.number.valid, caller->id.number.str, ""),
		sizeof(p->cid_num));
	ast_copy_string(p->cid_name,
		S_COR(caller->id.name.valid, caller->id.name.str, ""),
		sizeof(p->cid_name));
	ast_copy_string(p->cid_subaddr,
		S_COR(caller->id.subaddress.valid, caller->id.subaddress.str, ""),
		sizeof(p->cid_subaddr));
	p->cid_ton = caller->id.number.plan;
	p->callingpres = ast_party_id_presentation(&caller->id);
	if (caller->id.tag) {
		ast_copy_string(p->cid_tag, caller->id.tag, sizeof(p->cid_tag));
	}
	ast_copy_string(p->cid_ani,
		S_COR(caller->ani.number.valid, caller->ani.number.str, ""),
		sizeof(p->cid_ani));
	p->cid_ani2 = caller->ani2;
}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

#if defined(HAVE_PRI) || defined(HAVE_SS7)
/*!
 * \internal
 * \brief Set the Dialed Number Identifier.
 * \since 1.8
 *
 * \param pvt DAHDI private structure
 * \param dnid Dialed Number Identifier string.
 *
 * \return Nothing
 */
static void my_set_dnid(void *pvt, const char *dnid)
{
	struct dahdi_pvt *p = pvt;

	ast_copy_string(p->dnid, dnid, sizeof(p->dnid));
}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Set the Redirecting Directory Number Information Service (RDNIS).
 * \since 1.8
 *
 * \param pvt DAHDI private structure
 * \param rdnis Redirecting Directory Number Information Service (RDNIS) string.
 *
 * \return Nothing
 */
static void my_set_rdnis(void *pvt, const char *rdnis)
{
	struct dahdi_pvt *p = pvt;

	ast_copy_string(p->rdnis, rdnis, sizeof(p->rdnis));
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Make a dialstring for native ISDN CC to recall properly.
 * \since 1.8
 *
 * \param priv Channel private control structure.
 * \param buf Where to put the modified dialstring.
 * \param buf_size Size of modified dialstring buffer.
 *
 * \details
 * original dialstring:
 * DAHDI/[i<span>-](g|G|r|R)<group#(0-63)>[c|r<cadance#>|d][/extension[/options]]
 *
 * The modified dialstring will have prefixed the channel-group section
 * with the ISDN channel restriction.
 *
 * buf:
 * DAHDI/i<span>-(g|G|r|R)<group#(0-63)>[c|r<cadance#>|d][/extension[/options]]
 *
 * The routine will check to see if the ISDN channel restriction is already
 * in the original dialstring.
 *
 * \return Nothing
 */
static void my_pri_make_cc_dialstring(void *priv, char *buf, size_t buf_size)
{
	char *dial;
	struct dahdi_pvt *pvt;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(tech);	/* channel technology token */
		AST_APP_ARG(group);	/* channel/group token */
		//AST_APP_ARG(ext);	/* extension token */
		//AST_APP_ARG(opts);	/* options token */
		//AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	pvt = priv;
	dial = ast_strdupa(pvt->dialstring);
	AST_NONSTANDARD_APP_ARGS(args, dial, '/');
	if (!args.tech) {
		ast_copy_string(buf, pvt->dialstring, buf_size);
		return;
	}
	if (!args.group) {
		/* Append the ISDN span channel restriction to the dialstring. */
		snprintf(buf, buf_size, "%s/i%d-", args.tech, pvt->pri->span);
		return;
	}
	if (isdigit(args.group[0]) || args.group[0] == 'i' || strchr(args.group, '!')) {
		/* The ISDN span channel restriction is not needed or already
		 * in the dialstring. */
		ast_copy_string(buf, pvt->dialstring, buf_size);
		return;
	}
	/* Insert the ISDN span channel restriction into the dialstring. */
	snprintf(buf, buf_size, "%s/i%d-%s", args.tech, pvt->pri->span, args.group);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Reevaluate the PRI span device state.
 * \since 1.8
 *
 * \param pri Asterisk D channel control structure.
 *
 * \return Nothing
 *
 * \note Assumes the pri->lock is already obtained.
 */
static void dahdi_pri_update_span_devstate(struct sig_pri_span *pri)
{
	unsigned idx;
	unsigned num_b_chans;	/* Number of B channels provisioned on the span. */
	unsigned in_use;		/* Number of B channels in use on the span. */
	unsigned in_alarm;		/* TRUE if the span is in alarm condition. */
	enum ast_device_state new_state;

	/* Count the number of B channels and the number of B channels in use. */
	num_b_chans = 0;
	in_use = 0;
	in_alarm = 1;
	for (idx = pri->numchans; idx--;) {
		if (pri->pvts[idx] && !pri->pvts[idx]->no_b_channel) {
			/* This is a B channel interface. */
			++num_b_chans;
			if (!sig_pri_is_chan_available(pri->pvts[idx])) {
				++in_use;
			}
			if (!pri->pvts[idx]->inalarm) {
				/* There is a channel that is not in alarm. */
				in_alarm = 0;
			}
		}
	}

	/* Update the span congestion device state and report any change. */
	if (in_alarm) {
		new_state = AST_DEVICE_UNAVAILABLE;
	} else {
		new_state = num_b_chans == in_use ? AST_DEVICE_BUSY : AST_DEVICE_NOT_INUSE;
	}
	if (pri->congestion_devstate != new_state) {
		pri->congestion_devstate = new_state;
		ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_NOT_CACHABLE, "DAHDI/I%d/congestion", pri->span);
	}
#if defined(THRESHOLD_DEVSTATE_PLACEHOLDER)
	/* Update the span threshold device state and report any change. */
	if (in_alarm) {
		new_state = AST_DEVICE_UNAVAILABLE;
	} else if (!in_use) {
		new_state = AST_DEVICE_NOT_INUSE;
	} else if (!pri->user_busy_threshold) {
		new_state = in_use < num_b_chans ? AST_DEVICE_INUSE : AST_DEVICE_BUSY;
	} else {
		new_state = in_use < pri->user_busy_threshold ? AST_DEVICE_INUSE
			: AST_DEVICE_BUSY;
	}
	if (pri->threshold_devstate != new_state) {
		pri->threshold_devstate = new_state;
		ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_NOT_CACHABLE, "DAHDI/I%d/threshold", pri->span);
	}
#endif	/* defined(THRESHOLD_DEVSTATE_PLACEHOLDER) */
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Reference this module.
 * \since 1.8
 *
 * \return Nothing
 */
static void my_module_ref(void)
{
	ast_module_ref(ast_module_info->self);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Unreference this module.
 * \since 1.8
 *
 * \return Nothing
 */
static void my_module_unref(void)
{
	ast_module_unref(ast_module_info->self);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CALL_WAITING)
static void my_pri_init_config(void *priv, struct sig_pri_span *pri);
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
static int dahdi_new_pri_nobch_channel(struct sig_pri_span *pri);

struct sig_pri_callback sig_pri_callbacks =
{
	.handle_dchan_exception = my_handle_dchan_exception,
	.play_tone = my_pri_play_tone,
	.set_echocanceller = my_set_echocanceller,
	.dsp_reset_and_flush_digits = my_dsp_reset_and_flush_digits,
	.lock_private = my_lock_private,
	.unlock_private = my_unlock_private,
	.deadlock_avoidance_private = my_deadlock_avoidance_private,
	.new_ast_channel = my_new_pri_ast_channel,
	.fixup_chans = my_pri_fixup_chans,
	.set_alarm = my_set_alarm,
	.set_dialing = my_set_dialing,
	.set_outgoing = my_set_outgoing,
	.set_digital = my_set_digital,
	.set_callerid = my_set_callerid,
	.set_dnid = my_set_dnid,
	.set_rdnis = my_set_rdnis,
	.new_nobch_intf = dahdi_new_pri_nobch_channel,
#if defined(HAVE_PRI_CALL_WAITING)
	.init_config = my_pri_init_config,
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	.get_orig_dialstring = my_get_orig_dialstring,
	.make_cc_dialstring = my_pri_make_cc_dialstring,
	.update_span_devstate = dahdi_pri_update_span_devstate,
	.module_ref = my_module_ref,
	.module_unref = my_module_unref,
	.dial_digits = my_pri_dial_digits,
	.open_media = my_pri_ss7_open_media,
	.ami_channel_event = my_ami_channel_event,
};
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
/*!
 * \internal
 * \brief Handle the SS7 link exception.
 * \since 1.8
 *
 * \param linkset Controlling linkset for the channel.
 * \param which Link index of the signaling channel.
 *
 * \return Nothing
 */
static void my_handle_link_exception(struct sig_ss7_linkset *linkset, int which)
{
	int event;

	if (ioctl(linkset->fds[which], DAHDI_GETEVENT, &event)) {
		ast_log(LOG_ERROR, "SS7: Error in exception retrieval on span %d/%d!\n",
			linkset->span, which);
		return;
	}
	switch (event) {
	case DAHDI_EVENT_NONE:
		break;
	case DAHDI_EVENT_ALARM:
		ast_log(LOG_ERROR, "SS7 got event: %s(%d) on span %d/%d\n",
			event2str(event), event, linkset->span, which);
		sig_ss7_link_alarm(linkset, which);
		break;
	case DAHDI_EVENT_NOALARM:
		ast_log(LOG_ERROR, "SS7 got event: %s(%d) on span %d/%d\n",
			event2str(event), event, linkset->span, which);
		sig_ss7_link_noalarm(linkset, which);
		break;
	default:
		ast_log(LOG_NOTICE, "SS7 got event: %s(%d) on span %d/%d\n",
			event2str(event), event, linkset->span, which);
		break;
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void my_ss7_set_loopback(void *pvt, int enable)
{
	struct dahdi_pvt *p = pvt;

	if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_LOOPBACK, &enable)) {
		ast_log(LOG_WARNING, "Unable to set loopback on channel %d: %s\n", p->channel,
			strerror(errno));
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
/*!
 * \internal
 * \brief Create a new asterisk channel structure for SS7.
 * \since 1.8
 *
 * \param pvt Private channel structure.
 * \param state Initial state of new channel.
 * \param law Combanding law to use.
 * \param exten Dialplan extension for incoming call.
 * \param requestor Channel requesting this new channel.
 *
 * \retval ast_channel on success.
 * \retval NULL on error.
 */
static struct ast_channel *my_new_ss7_ast_channel(void *pvt, int state, enum sig_ss7_law law, char *exten, const struct ast_channel *requestor)
{
	struct dahdi_pvt *p = pvt;
	int audio;
	int newlaw;
	struct ast_callid *callid = NULL;
	int callid_created = ast_callid_threadstorage_auto(&callid);

	/* Set to audio mode at this point */
	audio = 1;
	if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &audio) == -1)
		ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n",
			p->channel, audio, strerror(errno));

	if (law != SIG_SS7_DEFLAW) {
		dahdi_setlaw(p->subs[SUB_REAL].dfd,
			(law == SIG_SS7_ULAW) ? DAHDI_LAW_MULAW : DAHDI_LAW_ALAW);
	}

	ast_copy_string(p->exten, exten, sizeof(p->exten));

	newlaw = -1;
	switch (law) {
	case SIG_SS7_DEFLAW:
		newlaw = 0;
		break;
	case SIG_SS7_ALAW:
		newlaw = DAHDI_LAW_ALAW;
		break;
	case SIG_SS7_ULAW:
		newlaw = DAHDI_LAW_MULAW;
		break;
	}
	return dahdi_new_callid_clean(p, state, 0, SUB_REAL, newlaw, requestor ? ast_channel_linkedid(requestor) : "", callid, callid_created);
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static int sig_ss7_tone_to_dahditone(enum sig_ss7_tone tone)
{
	switch (tone) {
	case SIG_SS7_TONE_RINGTONE:
		return DAHDI_TONE_RINGTONE;
	case SIG_SS7_TONE_STUTTER:
		return DAHDI_TONE_STUTTER;
	case SIG_SS7_TONE_CONGESTION:
		return DAHDI_TONE_CONGESTION;
	case SIG_SS7_TONE_DIALTONE:
		return DAHDI_TONE_DIALTONE;
	case SIG_SS7_TONE_DIALRECALL:
		return DAHDI_TONE_DIALRECALL;
	case SIG_SS7_TONE_INFO:
		return DAHDI_TONE_INFO;
	case SIG_SS7_TONE_BUSY:
		return DAHDI_TONE_BUSY;
	default:
		return -1;
	}
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static int my_ss7_play_tone(void *pvt, enum sig_ss7_tone tone)
{
	struct dahdi_pvt *p = pvt;

	return tone_zone_play_tone(p->subs[SUB_REAL].dfd, sig_ss7_tone_to_dahditone(tone));
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
struct sig_ss7_callback sig_ss7_callbacks =
{
	.lock_private = my_lock_private,
	.unlock_private = my_unlock_private,
	.deadlock_avoidance_private = my_deadlock_avoidance_private,

	.set_echocanceller = my_set_echocanceller,
	.set_loopback = my_ss7_set_loopback,

	.new_ast_channel = my_new_ss7_ast_channel,
	.play_tone = my_ss7_play_tone,

	.handle_link_exception = my_handle_link_exception,
	.set_alarm = my_set_alarm,
	.set_dialing = my_set_dialing,
	.set_outgoing = my_set_outgoing,
	.set_digital = my_set_digital,
	.set_inservice = my_set_inservice,
	.set_locallyblocked = my_set_locallyblocked,
	.set_remotelyblocked = my_set_remotelyblocked,
	.set_callerid = my_set_callerid,
	.set_dnid = my_set_dnid,
	.open_media = my_pri_ss7_open_media,
};
#endif	/* defined(HAVE_SS7) */

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

static int my_have_progressdetect(void *pvt)
{
	struct dahdi_pvt *p = pvt;

	if ((p->callprogress & CALLPROGRESS_PROGRESS)
		&& CANPROGRESSDETECT(p) && p->dsp && p->outgoing) {
		return 1;
	} else {
		/* Don't have progress detection. */
		return 0;
	}
}

struct analog_callback analog_callbacks =
{
	.play_tone = my_play_tone,
	.get_event = my_get_event,
	.wait_event = my_wait_event,
	.is_off_hook = my_is_off_hook,
	.set_echocanceller = my_set_echocanceller,
	.ring = my_ring,
	.flash = my_flash,
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
	.deadlock_avoidance_private = my_deadlock_avoidance_private,
	.handle_dtmf = my_handle_dtmf,
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
	.distinctive_ring = my_distinctive_ring,
	.set_linear_mode = my_set_linear_mode,
	.set_inthreeway = my_set_inthreeway,
	.get_and_handle_alarms = my_get_and_handle_alarms,
	.get_sigpvt_bridged_channel = my_get_sigpvt_bridged_channel,
	.get_sub_fd = my_get_sub_fd,
	.set_cadence = my_set_cadence,
	.set_alarm = my_set_alarm,
	.set_dialing = my_set_dialing,
	.set_outgoing = my_set_outgoing,
	.set_ringtimeout = my_set_ringtimeout,
	.set_waitingfordt = my_set_waitingfordt,
	.check_waitingfordt = my_check_waitingfordt,
	.set_confirmanswer = my_set_confirmanswer,
	.check_confirmanswer = my_check_confirmanswer,
	.set_callwaiting = my_set_callwaiting,
	.cancel_cidspill = my_cancel_cidspill,
	.confmute = my_confmute,
	.set_pulsedial = my_set_pulsedial,
	.set_new_owner = my_set_new_owner,
	.get_orig_dialstring = my_get_orig_dialstring,
	.set_needringing = my_set_needringing,
	.set_polarity = my_set_polarity,
	.start_polarityswitch = my_start_polarityswitch,
	.answer_polarityswitch = my_answer_polarityswitch,
	.hangup_polarityswitch = my_hangup_polarityswitch,
	.have_progressdetect = my_have_progressdetect,
};

/*! Round robin search locations. */
static struct dahdi_pvt *round_robin[32];

#define dahdi_get_index(ast, p, nullok)	_dahdi_get_index(ast, p, nullok, __PRETTY_FUNCTION__, __LINE__)
static int _dahdi_get_index(struct ast_channel *ast, struct dahdi_pvt *p, int nullok, const char *fname, unsigned long line)
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
			ast_log(LOG_WARNING,
				"Unable to get index for '%s' on channel %d (%s(), line %lu)\n",
				ast ? ast_channel_name(ast) : "", p->channel, fname, line);
	}
	return res;
}

/*!
 * \internal
 * \brief Obtain the specified subchannel owner lock if the owner exists.
 *
 * \param pvt Channel private struct.
 * \param sub_idx Subchannel owner to lock.
 *
 * \note Assumes the pvt->lock is already obtained.
 *
 * \note
 * Because deadlock avoidance may have been necessary, you need to confirm
 * the state of things before continuing.
 *
 * \return Nothing
 */
static void dahdi_lock_sub_owner(struct dahdi_pvt *pvt, int sub_idx)
{
	for (;;) {
		if (!pvt->subs[sub_idx].owner) {
			/* No subchannel owner pointer */
			break;
		}
		if (!ast_channel_trylock(pvt->subs[sub_idx].owner)) {
			/* Got subchannel owner lock */
			break;
		}
		/* We must unlock the private to avoid the possibility of a deadlock */
		DEADLOCK_AVOIDANCE(&pvt->lock);
	}
}

static void wakeup_sub(struct dahdi_pvt *p, int a)
{
	dahdi_lock_sub_owner(p, a);
	if (p->subs[a].owner) {
		ast_queue_frame(p->subs[a].owner, &ast_null_frame);
		ast_channel_unlock(p->subs[a].owner);
	}
}

static void dahdi_queue_frame(struct dahdi_pvt *p, struct ast_frame *f)
{
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
}

static void handle_clear_alarms(struct dahdi_pvt *p)
{
#if defined(HAVE_PRI)
	if (dahdi_sig_pri_lib_handles(p->sig) && sig_pri_is_alarm_ignored(p->pri)) {
		return;
	}
#endif	/* defined(HAVE_PRI) */

	if (report_alarms & REPORT_CHANNEL_ALARMS) {
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an alarm is cleared on a DAHDI channel.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear", "Channel: %d\r\n", p->channel);
	}
	if (report_alarms & REPORT_SPAN_ALARMS && p->manages_span_alarms) {
		ast_log(LOG_NOTICE, "Alarm cleared on span %d\n", p->span);
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an alarm is cleared on a DAHDI span.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_SYSTEM, "SpanAlarmClear", "Span: %d\r\n", p->span);
	}
}

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
	struct dahdi_pvt *p = ast_channel_tech_pvt(c);
	if (ast_strlen_zero(catstr)) {
		ast_debug(1, "No MFC/R2 category specified for chan %s, using default %s\n",
				ast_channel_name(c), openr2_proto_get_category_string(p->mfcr2_category));
		return p->mfcr2_category;
	}
	if ((cat = openr2_proto_get_category(catstr)) == OR2_CALLING_PARTY_CATEGORY_UNKNOWN) {
		ast_log(LOG_WARNING, "Invalid category specified '%s' for chan %s, using default %s\n",
				catstr, ast_channel_name(c), openr2_proto_get_category_string(p->mfcr2_category));
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
	p->cid_subaddr[0] = '\0';
	p->rdnis[0] = '\0';
	p->exten[0] = '\0';
	p->mfcr2_ani_index = '\0';
	p->mfcr2_dnis_index = '\0';
	p->mfcr2_dnis_matched = 0;
	p->mfcr2_answer_pending = 0;
	p->mfcr2_call_accepted = 0;
	ast_mutex_unlock(&p->lock);
	ast_verbose("New MFC/R2 call detected on chan %d.\n", openr2_chan_get_number(r2chan));
}

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
		handle_clear_alarms(p);
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
		ast_channel_hangupcause_set(p->owner, AST_CAUSE_PROTOCOL_ERROR);
		ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
	}
	ast_mutex_lock(&p->lock);
	p->mfcr2call = 0;
	ast_mutex_unlock(&p->lock);
}

static void dahdi_r2_disconnect_call(struct dahdi_pvt *p, openr2_call_disconnect_cause_t cause)
{
	if (openr2_chan_disconnect_call(p->r2chan, cause)) {
		ast_log(LOG_NOTICE, "Bad! failed to disconnect call on channel %d with reason %s, hope for the best!\n",
		   p->channel, openr2_proto_get_disconnect_string(cause));
		/* force the chan to idle and release the call flag now since we will not see a clean on_call_end */
		openr2_chan_set_idle(p->r2chan);
		ast_mutex_lock(&p->lock);
		p->mfcr2call = 0;
		ast_mutex_unlock(&p->lock);
	}
}

static void dahdi_r2_on_call_offered(openr2_chan_t *r2chan, const char *ani, const char *dnis, openr2_calling_party_category_t category)
{
	struct dahdi_pvt *p;
	struct ast_channel *c;
	struct ast_callid *callid = NULL;
	int callid_created = ast_callid_threadstorage_auto(&callid);
	ast_verbose("MFC/R2 call offered on chan %d. ANI = %s, DNIS = %s, Category = %s\n",
			openr2_chan_get_number(r2chan), ani ? ani : "(restricted)", dnis,
			openr2_proto_get_category_string(category));
	p = openr2_chan_get_client_data(r2chan);
	/* if collect calls are not allowed and this is a collect call, reject it! */
	if (!p->mfcr2_allow_collect_calls && category == OR2_CALLING_PARTY_CATEGORY_COLLECT_CALL) {
		ast_log(LOG_NOTICE, "Rejecting MFC/R2 collect call\n");
		dahdi_r2_disconnect_call(p, OR2_CAUSE_COLLECT_CALL_REJECTED);
		goto dahdi_r2_on_call_offered_cleanup;
	}
	ast_mutex_lock(&p->lock);
	p->mfcr2_recvd_category = category;
	/* if we're not supposed to use CID, clear whatever we have */
	if (!p->use_callerid) {
		ast_debug(1, "No CID allowed in configuration, CID is being cleared!\n");
		p->cid_num[0] = 0;
		p->cid_name[0] = 0;
	}
	/* if we're supposed to answer immediately, clear DNIS and set 's' exten */
	if (p->immediate || !openr2_context_get_max_dnis(openr2_chan_get_context(r2chan))) {
		ast_debug(1, "Setting exten => s because of immediate or 0 DNIS configured\n");
		p->exten[0] = 's';
		p->exten[1] = 0;
	}
	ast_mutex_unlock(&p->lock);
	if (!ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num)) {
		ast_log(LOG_NOTICE, "MFC/R2 call on channel %d requested non-existent extension '%s' in context '%s'. Rejecting call.\n",
				p->channel, p->exten, p->context);
		dahdi_r2_disconnect_call(p, OR2_CAUSE_UNALLOCATED_NUMBER);
		goto dahdi_r2_on_call_offered_cleanup;
	}
	if (!p->mfcr2_accept_on_offer) {
		/* The user wants us to start the PBX thread right away without accepting the call first */
		c = dahdi_new(p, AST_STATE_RING, 1, SUB_REAL, DAHDI_LAW_ALAW, NULL, callid);
		if (c) {
			/* Done here, don't disable reading now since we still need to generate MF tones to accept
			   the call or reject it and detect the tone off condition of the other end, all of this
			   will be done in the PBX thread now */
			goto dahdi_r2_on_call_offered_cleanup;
		}
		ast_log(LOG_WARNING, "Unable to create PBX channel in DAHDI channel %d\n", p->channel);
		dahdi_r2_disconnect_call(p, OR2_CAUSE_OUT_OF_ORDER);
	} else if (p->mfcr2_charge_calls) {
		ast_debug(1, "Accepting MFC/R2 call with charge on chan %d\n", p->channel);
		openr2_chan_accept_call(r2chan, OR2_CALL_WITH_CHARGE);
	} else {
		ast_debug(1, "Accepting MFC/R2 call with no charge on chan %d\n", p->channel);
		openr2_chan_accept_call(r2chan, OR2_CALL_NO_CHARGE);
	}

dahdi_r2_on_call_offered_cleanup:
	ast_callid_threadstorage_auto_clean(callid, callid_created);
}

static void dahdi_r2_on_call_end(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_verbose("MFC/R2 call end on channel %d\n", p->channel);
	ast_mutex_lock(&p->lock);
	p->mfcr2call = 0;
	ast_mutex_unlock(&p->lock);
}

static void dahdi_enable_ec(struct dahdi_pvt *p);
static void dahdi_r2_on_call_accepted(openr2_chan_t *r2chan, openr2_call_mode_t mode)
{
	struct dahdi_pvt *p = NULL;
	struct ast_channel *c = NULL;
	struct ast_callid *callid = NULL;
	int callid_created = ast_callid_threadstorage_auto(&callid);
	p = openr2_chan_get_client_data(r2chan);
	dahdi_enable_ec(p);
	p->mfcr2_call_accepted = 1;
	/* if it's an incoming call ... */
	if (OR2_DIR_BACKWARD == openr2_chan_get_direction(r2chan)) {
		ast_verbose("MFC/R2 call has been accepted on backward channel %d\n", openr2_chan_get_number(r2chan));
		/* If accept on offer is not set, it means at this point the PBX thread is already
		   launched (was launched in the 'on call offered' handler) and therefore this callback
		   is being executed already in the PBX thread rather than the monitor thread, don't launch
		   any other thread, just disable the openr2 reading and answer the call if needed */
		if (!p->mfcr2_accept_on_offer) {
			openr2_chan_disable_read(r2chan);
			if (p->mfcr2_answer_pending) {
				ast_debug(1, "Answering MFC/R2 call after accepting it on chan %d\n", openr2_chan_get_number(r2chan));
				dahdi_r2_answer(p);
			}
			goto dahdi_r2_on_call_accepted_cleanup;
		}
		c = dahdi_new(p, AST_STATE_RING, 1, SUB_REAL, DAHDI_LAW_ALAW, NULL, callid);
		if (c) {
			/* chan_dahdi will take care of reading from now on in the PBX thread, tell the
			   library to forget about it */
			openr2_chan_disable_read(r2chan);
			goto dahdi_r2_on_call_accepted_cleanup;
		}
		ast_log(LOG_WARNING, "Unable to create PBX channel in DAHDI channel %d\n", p->channel);
		/* failed to create the channel, bail out and report it as an out of order line */
		dahdi_r2_disconnect_call(p, OR2_CAUSE_OUT_OF_ORDER);
		goto dahdi_r2_on_call_accepted_cleanup;
	}
	/* this is an outgoing call, no need to launch the PBX thread, most likely we're in one already */
	ast_verbose("MFC/R2 call has been accepted on forward channel %d\n", p->channel);
	p->subs[SUB_REAL].needringing = 1;
	p->dialing = 0;
	/* chan_dahdi will take care of reading from now on in the PBX thread, tell the library to forget about it */
	openr2_chan_disable_read(r2chan);

dahdi_r2_on_call_accepted_cleanup:
	ast_callid_threadstorage_auto_clean(callid, callid_created);
}

static void dahdi_r2_on_call_answered(openr2_chan_t *r2chan)
{
	struct dahdi_pvt *p = openr2_chan_get_client_data(r2chan);
	ast_verbose("MFC/R2 call has been answered on channel %d\n", openr2_chan_get_number(r2chan));
	p->subs[SUB_REAL].needanswer = 1;
}

static void dahdi_r2_on_call_read(openr2_chan_t *r2chan, const unsigned char *buf, int buflen)
{
	/*ast_debug(1, "Read data from dahdi channel %d\n", openr2_chan_get_number(r2chan));*/
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
	char cause_str[50];
	struct ast_control_pvt_cause_code *cause_code;
	int datalen = sizeof(*cause_code);

	ast_verbose("MFC/R2 call disconnected on channel %d\n", openr2_chan_get_number(r2chan));
	ast_mutex_lock(&p->lock);
	if (!p->owner) {
		ast_mutex_unlock(&p->lock);
		/* no owner, therefore we can't use dahdi_hangup to disconnect, do it right now */
		dahdi_r2_disconnect_call(p, OR2_CAUSE_NORMAL_CLEARING);
		return;
	}

	snprintf(cause_str, sizeof(cause_str), "R2 DISCONNECT (%s)", openr2_proto_get_disconnect_string(cause));
	datalen += strlen(cause_str);
	cause_code = ast_alloca(datalen);
	memset(cause_code, 0, datalen);
	cause_code->ast_cause = dahdi_r2_cause_to_ast_cause(cause);
	ast_copy_string(cause_code->chan_name, ast_channel_name(p->owner), AST_CHANNEL_NAME);
	ast_copy_string(cause_code->code, cause_str, datalen + 1 - sizeof(*cause_code));
	ast_queue_control_data(p->owner, AST_CONTROL_PVT_CAUSE_CODE, cause_code, datalen);
	ast_channel_hangupcause_hash_set(p->owner, cause_code, datalen);

	/* when we have an owner we don't call dahdi_r2_disconnect_call here, that will
	   be done in dahdi_hangup */
	if (ast_channel_state(p->owner) == AST_STATE_UP) {
		ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
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
			ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
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
		ast_verbose("%s", logmessage);
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
		ast_debug(1, "%s", logmessage);
		break;
	default:
		ast_log(LOG_WARNING, "We should handle logging level %d here.\n", level);
		ast_debug(1, "%s", logmessage);
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

static void dahdi_r2_on_billing_pulse_received(openr2_chan_t *r2chan)
{
	ast_verbose("MFC/R2 billing pulse received on channel %d\n", openr2_chan_get_number(r2chan));
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
	.on_billing_pulse_received = dahdi_r2_on_billing_pulse_received
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
	dahdi_close(ss7->ss7.fds[fd_num]);
	ss7->ss7.fds[fd_num] = -1;
}
#endif	/* defined(HAVE_SS7) */

static int dahdi_setlinear(int dfd, int linear)
{
	return ioctl(dfd, DAHDI_SETLINEAR, &linear);
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

	pvt = ast_channel_tech_pvt(chan);

	ast_mutex_lock(&pvt->lock);

	idx = dahdi_get_index(chan, pvt, 0);

	if ((idx != SUB_REAL) || !pvt->owner)
		goto out;

#ifdef HAVE_PRI
	switch (pvt->sig) {
	case SIG_PRI_LIB_HANDLE_CASES:
		res = sig_pri_digit_begin(pvt->sig_pvt, chan, digit);
		if (!res)
			goto out;
		break;
	default:
		break;
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
			ast_log(LOG_WARNING, "Channel %s couldn't dial digit %c: %s\n",
				ast_channel_name(chan), digit, strerror(errno));
		else
			pvt->dialing = 1;
	} else {
		ast_debug(1, "Channel %s started VLDTMF digit '%c'\n",
			ast_channel_name(chan), digit);
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

	pvt = ast_channel_tech_pvt(chan);

	ast_mutex_lock(&pvt->lock);

	idx = dahdi_get_index(chan, pvt, 0);

	if ((idx != SUB_REAL) || !pvt->owner || pvt->pulse)
		goto out;

#ifdef HAVE_PRI
	/* This means that the digit was already sent via PRI signalling */
	if (dahdi_sig_pri_lib_handles(pvt->sig) && !pvt->begindigit) {
		goto out;
	}
#endif

	if (pvt->begindigit) {
		x = -1;
		ast_debug(1, "Channel %s ending VLDTMF digit '%c'\n",
			ast_channel_name(chan), digit);
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
	c->curconf = zi;
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
#if defined(HAVE_PRI) || defined(HAVE_SS7)
		switch (p->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			if (((struct sig_pri_chan *) p->sig_pvt)->no_b_channel) {
				/*
				 * PRI nobch pseudo channel.  Does not need ec anyway.
				 * Does not handle ioctl(DAHDI_AUDIOMODE)
				 */
				return;
			}
			/* Fall through */
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
#endif	/* defined(HAVE_SS7) */
			{
				int x = 1;

				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x);
				if (res)
					ast_log(LOG_WARNING,
						"Unable to enable audio mode on channel %d (%s)\n",
						p->channel, strerror(errno));
			}
			break;
		default:
			break;
		}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
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

/* perform a dynamic range compression transform on the given sample */
static int drc_sample(int sample, float drc)
{
	float neg;
	float shallow, steep;
	float max = SHRT_MAX;
	
	neg = (sample < 0 ? -1 : 1);
	steep = drc*sample;
	shallow = neg*(max-max/drc)+(float)sample/drc;
	if (abs(steep) < abs(shallow)) {
		sample = steep;
	}
	else {
		sample = shallow;
	}

	return sample;
}


static void fill_txgain(struct dahdi_gains *g, float gain, float drc, int law)
{
	int j;
	int k;

	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++) {
			if (gain || drc) {
				k = AST_ALAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k * linear_gain;
				if (k > 32767) {
					k = 32767;
				} else if (k < -32768) {
					k = -32768;
				}
				g->txgain[j] = AST_LIN2A(k);
			} else {
				g->txgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++) {
			if (gain || drc) {
				k = AST_MULAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k * linear_gain;
				if (k > 32767) {
					k = 32767;
				} else if (k < -32768) {
					k = -32768;
				}
				g->txgain[j] = AST_LIN2MU(k);

			} else {
				g->txgain[j] = j;
			}
		}
		break;
	}
}

static void fill_rxgain(struct dahdi_gains *g, float gain, float drc, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++) {
			if (gain || drc) {
				k = AST_ALAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k * linear_gain;
				if (k > 32767) {
					k = 32767;
				} else if (k < -32768) {
					k = -32768;
				}
				g->rxgain[j] = AST_LIN2A(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++) {
			if (gain || drc) {
				k = AST_MULAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k * linear_gain;
				if (k > 32767) {
					k = 32767;
				} else if (k < -32768) {
					k = -32768;
				}
				g->rxgain[j] = AST_LIN2MU(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	}
}

static int set_actual_txgain(int fd, float gain, float drc, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		ast_debug(1, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_txgain(&g, gain, drc, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_rxgain(int fd, float gain, float drc, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		ast_debug(1, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_rxgain(&g, gain, drc, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_gain(int fd, float rxgain, float txgain, float rxdrc, float txdrc, int law)
{
	return set_actual_txgain(fd, txgain, txdrc, law) | set_actual_rxgain(fd, rxgain, rxdrc, law);
}

static int bump_gains(struct dahdi_pvt *p)
{
	int res;

	/* Bump receive gain by value stored in cid_rxgain */
	res = set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain + p->cid_rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to bump gain: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int restore_gains(struct dahdi_pvt *p)
{
	int res;

	res = set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
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
	int x, res;

	x = muted;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	switch (p->sig) {
#if defined(HAVE_PRI)
	case SIG_PRI_LIB_HANDLE_CASES:
		if (((struct sig_pri_chan *) p->sig_pvt)->no_b_channel) {
			/* PRI nobch pseudo channel.  Does not handle ioctl(DAHDI_AUDIOMODE) */
			break;
		}
		/* Fall through */
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	case SIG_SS7:
#endif	/* defined(HAVE_SS7) */
		{
			int y = 1;

			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &y);
			if (res)
				ast_log(LOG_WARNING, "Unable to set audio mode on %d: %s\n",
					p->channel, strerror(errno));
		}
		break;
	default:
		break;
	}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
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
		ast_debug(1, "Restored conferencing\n");
	}
	return 0;
}

static int send_cwcidspill(struct dahdi_pvt *p)
{
	struct ast_format tmpfmt;

	p->callwaitcas = 0;
	p->cidcwexpire = 0;
	p->cid_suppress_expire = 0;
	if (!(p->cidspill = ast_malloc(MAX_CALLERID_SIZE)))
		return -1;
	p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, p->callwait_name, p->callwait_num, ast_format_set(&tmpfmt, AST_LAW(p), 0));
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
		ast_debug(4, "writing callerid at pos %d of %d, res = %d\n", p->cidpos, p->cidlen, res);
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
	p->cid_suppress_expire = CALLWAITING_SUPPRESS_SAMPLES;
	ast_free(p->cidspill);
	p->cidspill = NULL;
	if (p->callwaitcas) {
		/* Wait for CID/CW to expire */
		p->cidcwexpire = CIDCW_EXPIRE_SAMPLES;
		p->cid_suppress_expire = p->cidcwexpire;
	} else
		restore_conference(p);
	return 0;
}

static int dahdi_callwait(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_format tmpfmt;
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill) {
		ast_log(LOG_WARNING, "Spill already exists?!?\n");
		ast_free(p->cidspill);
	}

	/*
	 * SAS: Subscriber Alert Signal, 440Hz for 300ms
	 * CAS: CPE Alert Signal, 2130Hz * 2750Hz sine waves
	 */
	if (!(p->cidspill = ast_malloc(2400 /* SAS */ + 680 /* CAS */ + READ_SIZE * 4)))
		return -1;
	save_conference(p);
	/* Silence */
	memset(p->cidspill, 0x7f, 2400 + 600 + READ_SIZE * 4);
	if (!p->callwaitrings && p->callwaitingcallerid) {
		ast_gen_cas(p->cidspill, 1, 2400 + 680, ast_format_set(&tmpfmt, AST_LAW(p), 0));
		p->callwaitcas = 1;
		p->cidlen = 2400 + 680 + READ_SIZE * 4;
	} else {
		ast_gen_cas(p->cidspill, 1, 2400, ast_format_set(&tmpfmt, AST_LAW(p), 0));
		p->callwaitcas = 0;
		p->cidlen = 2400 + READ_SIZE * 4;
	}
	p->cidpos = 0;
	send_callerid(p);

	return 0;
}

static int dahdi_call(struct ast_channel *ast, const char *rdest, int timeout)
{
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	int x, res, mysig;
	char *dest;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(ext);	/* extension token */
		//AST_APP_ARG(opts);	/* options token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	ast_mutex_lock(&p->lock);
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));

	/* Split the dialstring */
	dest = ast_strdupa(rdest);
	AST_NONSTANDARD_APP_ARGS(args, dest, '/');
	if (!args.ext) {
		args.ext = "";
	}

#if defined(HAVE_PRI)
	if (dahdi_sig_pri_lib_handles(p->sig)) {
		char *subaddr;

		sig_pri_extract_called_num_subaddr(p->sig_pvt, rdest, p->exten, sizeof(p->exten));

		/* Remove any subaddress for uniformity with incoming calls. */
		subaddr = strchr(p->exten, ':');
		if (subaddr) {
			*subaddr = '\0';
		}
	} else
#endif	/* defined(HAVE_PRI) */
	{
		ast_copy_string(p->exten, args.ext, sizeof(p->exten));
	}

	if ((ast_channel_state(ast) == AST_STATE_BUSY)) {
		p->subs[SUB_REAL].needbusy = 1;
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "dahdi_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
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

	if (IS_DIGITAL(ast_channel_transfercapability(ast))){
		set_actual_gain(p->subs[SUB_REAL].dfd, 0, 0, p->rxdrc, p->txdrc, p->law);
	} else {
		set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
	}	

#ifdef HAVE_PRI
	if (dahdi_sig_pri_lib_handles(p->sig)) {
		res = sig_pri_call(p->sig_pvt, ast, rdest, timeout,
			(p->law == DAHDI_LAW_ALAW) ? PRI_LAYER_1_ALAW : PRI_LAYER_1_ULAW);
		ast_mutex_unlock(&p->lock);
		return res;
	}
#endif

#if defined(HAVE_SS7)
	if (p->sig == SIG_SS7) {
		res = sig_ss7_call(p->sig_pvt, ast, rdest);
		ast_mutex_unlock(&p->lock);
		return res;
	}
#endif	/* defined(HAVE_SS7) */

	/* If this is analog signalling we can exit here */
	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		p->callwaitrings = 0;
		res = analog_call(p->sig_pvt, ast, rdest, timeout);
		ast_mutex_unlock(&p->lock);
		return res;
	}

	mysig = p->outsigmod > -1 ? p->outsigmod : p->sig;
	switch (mysig) {
	case 0:
		/* Special pseudo -- automatically up*/
		ast_setstate(ast, AST_STATE_UP);
		break;
	case SIG_MFCR2:
		break;
	default:
		ast_debug(1, "not yet implemented\n");
		ast_mutex_unlock(&p->lock);
		return -1;
	}

#ifdef HAVE_OPENR2
	if (p->mfcr2) {
		openr2_calling_party_category_t chancat;
		int callres = 0;
		char *c, *l;

		/* We'll get it in a moment -- but use dialdest to store pre-setup_ack digits */
		p->dialdest[0] = '\0';

		c = args.ext;
		if (!p->hidecallerid) {
			l = ast_channel_connected(ast)->id.number.valid ? ast_channel_connected(ast)->id.number.str : NULL;
		} else {
			l = NULL;
		}
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		p->dialing = 1;
		chancat = dahdi_r2_get_channel_category(ast);
		callres = openr2_chan_make_call(p->r2chan, l, (c + p->stripmsd), chancat);
		if (-1 == callres) {
			ast_mutex_unlock(&p->lock);
			ast_log(LOG_ERROR, "unable to make new MFC/R2 call!\n");
			return -1;
		}
		p->mfcr2_call_accepted = 0;
		p->mfcr2_progress_sent = 0;
		ast_setstate(ast, AST_STATE_DIALING);
	}
#endif /* HAVE_OPENR2 */
	ast_mutex_unlock(&p->lock);
	return 0;
}

/*!
 * \internal
 * \brief Insert the given chan_dahdi interface structure into the interface list.
 * \since 1.8
 *
 * \param pvt chan_dahdi private interface structure to insert.
 *
 * \details
 * The interface list is a doubly linked list sorted by the chan_dahdi channel number.
 * Any duplicates are inserted after the existing entries.
 *
 * \note The new interface must not already be in the list.
 *
 * \return Nothing
 */
static void dahdi_iflist_insert(struct dahdi_pvt *pvt)
{
	struct dahdi_pvt *cur;

	pvt->which_iflist = DAHDI_IFLIST_MAIN;

	/* Find place in middle of list for the new interface. */
	for (cur = iflist; cur; cur = cur->next) {
		if (pvt->channel < cur->channel) {
			/* New interface goes before the current interface. */
			pvt->prev = cur->prev;
			pvt->next = cur;
			if (cur->prev) {
				/* Insert into the middle of the list. */
				cur->prev->next = pvt;
			} else {
				/* Insert at head of list. */
				iflist = pvt;
			}
			cur->prev = pvt;
			return;
		}
	}

	/* New interface goes onto the end of the list */
	pvt->prev = ifend;
	pvt->next = NULL;
	if (ifend) {
		ifend->next = pvt;
	}
	ifend = pvt;
	if (!iflist) {
		/* List was empty */
		iflist = pvt;
	}
}

/*!
 * \internal
 * \brief Extract the given chan_dahdi interface structure from the interface list.
 * \since 1.8
 *
 * \param pvt chan_dahdi private interface structure to extract.
 *
 * \note
 * The given interface structure can be either in the interface list or a stand alone
 * structure that has not been put in the list if the next and prev pointers are NULL.
 *
 * \return Nothing
 */
static void dahdi_iflist_extract(struct dahdi_pvt *pvt)
{
	/* Extract from the forward chain. */
	if (pvt->prev) {
		pvt->prev->next = pvt->next;
	} else if (iflist == pvt) {
		/* Node is at the head of the list. */
		iflist = pvt->next;
	}

	/* Extract from the reverse chain. */
	if (pvt->next) {
		pvt->next->prev = pvt->prev;
	} else if (ifend == pvt) {
		/* Node is at the end of the list. */
		ifend = pvt->prev;
	}

	/* Node is no longer in the list. */
	pvt->which_iflist = DAHDI_IFLIST_NONE;
	pvt->prev = NULL;
	pvt->next = NULL;
}

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Insert the given chan_dahdi interface structure into the no B channel list.
 * \since 1.8
 *
 * \param pri sig_pri span control structure holding no B channel list.
 * \param pvt chan_dahdi private interface structure to insert.
 *
 * \details
 * The interface list is a doubly linked list sorted by the chan_dahdi channel number.
 * Any duplicates are inserted after the existing entries.
 *
 * \note The new interface must not already be in the list.
 *
 * \return Nothing
 */
static void dahdi_nobch_insert(struct sig_pri_span *pri, struct dahdi_pvt *pvt)
{
	struct dahdi_pvt *cur;

	pvt->which_iflist = DAHDI_IFLIST_NO_B_CHAN;

	/* Find place in middle of list for the new interface. */
	for (cur = pri->no_b_chan_iflist; cur; cur = cur->next) {
		if (pvt->channel < cur->channel) {
			/* New interface goes before the current interface. */
			pvt->prev = cur->prev;
			pvt->next = cur;
			if (cur->prev) {
				/* Insert into the middle of the list. */
				cur->prev->next = pvt;
			} else {
				/* Insert at head of list. */
				pri->no_b_chan_iflist = pvt;
			}
			cur->prev = pvt;
			return;
		}
	}

	/* New interface goes onto the end of the list */
	pvt->prev = pri->no_b_chan_end;
	pvt->next = NULL;
	if (pri->no_b_chan_end) {
		((struct dahdi_pvt *) pri->no_b_chan_end)->next = pvt;
	}
	pri->no_b_chan_end = pvt;
	if (!pri->no_b_chan_iflist) {
		/* List was empty */
		pri->no_b_chan_iflist = pvt;
	}
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Extract the given chan_dahdi interface structure from the no B channel list.
 * \since 1.8
 *
 * \param pri sig_pri span control structure holding no B channel list.
 * \param pvt chan_dahdi private interface structure to extract.
 *
 * \note
 * The given interface structure can be either in the interface list or a stand alone
 * structure that has not been put in the list if the next and prev pointers are NULL.
 *
 * \return Nothing
 */
static void dahdi_nobch_extract(struct sig_pri_span *pri, struct dahdi_pvt *pvt)
{
	/* Extract from the forward chain. */
	if (pvt->prev) {
		pvt->prev->next = pvt->next;
	} else if (pri->no_b_chan_iflist == pvt) {
		/* Node is at the head of the list. */
		pri->no_b_chan_iflist = pvt->next;
	}

	/* Extract from the reverse chain. */
	if (pvt->next) {
		pvt->next->prev = pvt->prev;
	} else if (pri->no_b_chan_end == pvt) {
		/* Node is at the end of the list. */
		pri->no_b_chan_end = pvt->prev;
	}

	/* Node is no longer in the list. */
	pvt->which_iflist = DAHDI_IFLIST_NONE;
	pvt->prev = NULL;
	pvt->next = NULL;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Unlink the channel interface from the PRI private pointer array.
 * \since 1.8
 *
 * \param pvt chan_dahdi private interface structure to unlink.
 *
 * \return Nothing
 */
static void dahdi_unlink_pri_pvt(struct dahdi_pvt *pvt)
{
	unsigned idx;
	struct sig_pri_span *pri;

	pri = pvt->pri;
	if (!pri) {
		/* Not PRI signaling so cannot be in a PRI private pointer array. */
		return;
	}
	ast_mutex_lock(&pri->lock);
	for (idx = 0; idx < pri->numchans; ++idx) {
		if (pri->pvts[idx] == pvt->sig_pvt) {
			pri->pvts[idx] = NULL;
			ast_mutex_unlock(&pri->lock);
			return;
		}
	}
	ast_mutex_unlock(&pri->lock);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
/*!
 * \internal
 * \brief Unlink the channel interface from the SS7 private pointer array.
 * \since 1.8
 *
 * \param pvt chan_dahdi private interface structure to unlink.
 *
 * \return Nothing
 */
static void dahdi_unlink_ss7_pvt(struct dahdi_pvt *pvt)
{
	unsigned idx;
	struct sig_ss7_linkset *ss7;

	ss7 = pvt->ss7;
	if (!ss7) {
		/* Not SS7 signaling so cannot be in a SS7 private pointer array. */
		return;
	}
	ast_mutex_lock(&ss7->lock);
	for (idx = 0; idx < ss7->numchans; ++idx) {
		if (ss7->pvts[idx] == pvt->sig_pvt) {
			ss7->pvts[idx] = NULL;
			ast_mutex_unlock(&ss7->lock);
			return;
		}
	}
	ast_mutex_unlock(&ss7->lock);
}
#endif	/* defined(HAVE_SS7) */

static struct dahdi_pvt *find_next_iface_in_span(struct dahdi_pvt *cur)
{
	if (cur->next && cur->next->span == cur->span) {
		return cur->next;
	} else if (cur->prev && cur->prev->span == cur->span) {
		return cur->prev;
	}

	return NULL;
}

static void destroy_dahdi_pvt(struct dahdi_pvt *pvt)
{
	struct dahdi_pvt *p = pvt;

	if (p->manages_span_alarms) {
		struct dahdi_pvt *next = find_next_iface_in_span(p);
		if (next) {
			next->manages_span_alarms = 1;
		}
	}

	/* Remove channel from the list */
#if defined(HAVE_PRI)
	dahdi_unlink_pri_pvt(p);
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	dahdi_unlink_ss7_pvt(p);
#endif	/* defined(HAVE_SS7) */
	switch (pvt->which_iflist) {
	case DAHDI_IFLIST_NONE:
		break;
	case DAHDI_IFLIST_MAIN:
		dahdi_iflist_extract(p);
		break;
#if defined(HAVE_PRI)
	case DAHDI_IFLIST_NO_B_CHAN:
		if (p->pri) {
			dahdi_nobch_extract(p->pri, p);
		}
		break;
#endif	/* defined(HAVE_PRI) */
	}

	if (p->sig_pvt) {
		if (analog_lib_handles(p->sig, 0, 0)) {
			analog_delete(p->sig_pvt);
		}
		switch (p->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			sig_pri_chan_delete(p->sig_pvt);
			break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
			sig_ss7_chan_delete(p->sig_pvt);
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			break;
		}
	}
	ast_free(p->cidspill);
	if (p->use_smdi)
		ast_smdi_interface_unref(p->smdi_iface);
	if (p->mwi_event_sub)
		ast_event_unsubscribe(p->mwi_event_sub);
	if (p->vars) {
		ast_variables_destroy(p->vars);
	}
	if (p->cc_params) {
		ast_cc_config_params_destroy(p->cc_params);
	}

	p->named_callgroups = ast_unref_namedgroups(p->named_callgroups);
	p->named_pickupgroups = ast_unref_namedgroups(p->named_pickupgroups);

	ast_mutex_destroy(&p->lock);
	dahdi_close_sub(p, SUB_REAL);
	if (p->owner)
		ast_channel_tech_pvt_set(p->owner, NULL);
	ast_free(p);
}

static void destroy_channel(struct dahdi_pvt *cur, int now)
{
	int i;

	if (!now) {
		/* Do not destroy the channel now if it is owned by someone. */
		if (cur->owner) {
			return;
		}
		for (i = 0; i < 3; i++) {
			if (cur->subs[i].owner) {
				return;
			}
		}
	}
	destroy_dahdi_pvt(cur);
}

static void destroy_all_channels(void)
{
	int chan;
#if defined(HAVE_PRI)
	unsigned span;
	struct sig_pri_span *pri;
#endif	/* defined(HAVE_PRI) */
	struct dahdi_pvt *p;

	while (num_restart_pending) {
		usleep(1);
	}

	ast_mutex_lock(&iflock);
	/* Destroy all the interfaces and free their memory */
	while (iflist) {
		p = iflist;

		chan = p->channel;
#if defined(HAVE_PRI_SERVICE_MESSAGES)
		{
			char db_chan_name[20];
			char db_answer[5];
			char state;
			int why = -1;

			snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, p->span, chan);
			if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
				sscanf(db_answer, "%1c:%30d", &state, &why);
			}
			if (!why) {
				/* SRVST persistence is not required */
				ast_db_del(db_chan_name, SRVST_DBKEY);
			}
		}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
		/* Free associated memory */
		destroy_dahdi_pvt(p);
		ast_verb(3, "Unregistered channel %d\n", chan);
	}
	ifcount = 0;
	ast_mutex_unlock(&iflock);

#if defined(HAVE_PRI)
	/* Destroy all of the no B channel interface lists */
	for (span = 0; span < NUM_SPANS; ++span) {
		if (!pris[span].dchannels[0]) {
			break;
		}
		pri = &pris[span].pri;
		ast_mutex_lock(&pri->lock);
		while (pri->no_b_chan_iflist) {
			p = pri->no_b_chan_iflist;

			/* Free associated memory */
			destroy_dahdi_pvt(p);
		}
		ast_mutex_unlock(&pri->lock);
	}
#endif	/* defined(HAVE_PRI) */
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

	p = (struct dahdi_pvt *)ast_channel_tech_pvt(chan);

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
	struct dahdi_pvt *pvt;
	char *parse;
	int res;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(destination);
		AST_APP_ARG(original);
		AST_APP_ARG(reason);
	);

	if (ast_strlen_zero(data)) {
		ast_debug(1, "No data sent to application!\n");
		return -1;
	}
	if (ast_channel_tech(chan) != &dahdi_tech) {
		ast_debug(1, "Only DAHDI technology accepted!\n");
		return -1;
	}
	pvt = (struct dahdi_pvt *) ast_channel_tech_pvt(chan);
	if (!pvt) {
		ast_debug(1, "Unable to find technology private\n");
		return -1;
	}
	switch (pvt->sig) {
	case SIG_PRI_LIB_HANDLE_CASES:
		break;
	default:
		ast_debug(1, "callrerouting attempted on non-ISDN channel %s\n",
			ast_channel_name(chan));
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

	res = pri_send_callrerouting_facility_exec(pvt->sig_pvt, ast_channel_state(chan),
		args.destination, args.original, args.reason);
	if (!res) {
		/*
		 * Wait up to 5 seconds for a reply before hanging up this call
		 * leg if the peer does not disconnect first.
		 */
		ast_safe_sleep(chan, 5000);
	}

	return -1;
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
		ast_debug(1, "No data sent to application!\n");
		return -1;
	}

	if (ast_channel_tech(chan) != &dahdi_tech) {
		ast_debug(1, "Only DAHDI technology accepted!\n");
		return -1;
	}

	p = (struct dahdi_pvt *)ast_channel_tech_pvt(chan);
	if (!p) {
		ast_debug(1, "Unable to find technology private!\n");
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
		ast_debug(1, "Channel %s does not seems to be an R2 active channel!\n", ast_channel_name(chan));
		return -1;
	}

	if (p->mfcr2_call_accepted) {
		ast_mutex_unlock(&p->lock);
		ast_debug(1, "MFC/R2 call already accepted on channel %s!\n", ast_channel_name(chan));
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
			ast_debug(1, "ast_waitfor failed on channel %s, going out ...\n", ast_channel_name(chan));
			res = -1;
			break;
		}
		if (res == 0) {
			continue;
		}
		res = 0;
		f = ast_read(chan);
		if (!f) {
			ast_debug(1, "No frame read on channel %s, going out ...\n", ast_channel_name(chan));
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(1, "Got HANGUP frame on channel %s, going out ...\n", ast_channel_name(chan));
			ast_frfree(f);
			res = -1;
			break;
		}
		ast_frfree(f);
		ast_mutex_lock(&p->lock);
		if (p->mfcr2_call_accepted) {
			ast_mutex_unlock(&p->lock);
			ast_debug(1, "Accepted MFC/R2 call!\n");
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
	ast_debug(1, "ast cause %d resulted in openr2 cause %d/%s\n",
			cause, r2cause, openr2_proto_get_disconnect_string(r2cause));
	return r2cause;
}
#endif

static int revert_fax_buffers(struct dahdi_pvt *p, struct ast_channel *ast)
{
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
			ast_log(LOG_WARNING, "Channel '%s' unable to revert buffer policy: %s\n", ast_channel_name(ast), strerror(errno));
		}
		p->bufferoverrideinuse = 0;
		return bpres;
	}

	return -1;
}

static int dahdi_hangup(struct ast_channel *ast)
{
	int res = 0;
	int idx,x;
	int law;
	/*static int restore_gains(struct dahdi_pvt *p);*/
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	struct dahdi_params par;

	ast_debug(1, "dahdi_hangup(%s)\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	ast_mutex_lock(&p->lock);
	p->exten[0] = '\0';
	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		dahdi_confmute(p, 0);
		restore_gains(p);
		p->ignoredtmf = 0;
		p->waitingfordt.tv_sec = 0;

		res = analog_hangup(p->sig_pvt, ast);
		revert_fax_buffers(p, ast);

		goto hangup_out;
	} else {
		p->cid_num[0] = '\0';
		p->cid_name[0] = '\0';
		p->cid_subaddr[0] = '\0';
	}

#if defined(HAVE_PRI)
	if (dahdi_sig_pri_lib_handles(p->sig)) {
		x = 1;
		ast_channel_setoption(ast, AST_OPTION_AUDIO_MODE, &x, sizeof(char), 0);

		dahdi_confmute(p, 0);
		p->muting = 0;
		restore_gains(p);
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}
		p->ignoredtmf = 0;

		/* Real channel, do some fixup */
		p->subs[SUB_REAL].owner = NULL;
		p->subs[SUB_REAL].needbusy = 0;
		dahdi_setlinear(p->subs[SUB_REAL].dfd, 0);

		p->owner = NULL;
		p->cid_tag[0] = '\0';
		p->ringt = 0;/* Probably not used in this mode.  Reset anyway. */
		p->distinctivering = 0;/* Probably not used in this mode. Reset anyway. */
		p->confirmanswer = 0;/* Probably not used in this mode. Reset anyway. */
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;/* Probably not used in this mode. Reset anyway. */

		revert_fax_buffers(p, ast);

		p->law = p->law_default;
		law = p->law_default;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n",
				p->channel, strerror(errno));
		}

		sig_pri_hangup(p->sig_pvt, ast);

		tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
		dahdi_disable_ec(p);

		x = 0;
		ast_channel_setoption(ast, AST_OPTION_TDD, &x, sizeof(char), 0);
		p->didtdd = 0;/* Probably not used in this mode. Reset anyway. */

		p->rdnis[0] = '\0';
		update_conf(p);
		reset_conf(p);

		/* Restore data mode */
		x = 0;
		ast_channel_setoption(ast, AST_OPTION_AUDIO_MODE, &x, sizeof(char), 0);

		if (num_restart_pending == 0) {
			restart_monitor();
		}
		goto hangup_out;
	}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
	if (p->sig == SIG_SS7) {
		x = 1;
		ast_channel_setoption(ast, AST_OPTION_AUDIO_MODE, &x, sizeof(char), 0);

		dahdi_confmute(p, 0);
		p->muting = 0;
		restore_gains(p);
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}
		p->ignoredtmf = 0;

		/* Real channel, do some fixup */
		p->subs[SUB_REAL].owner = NULL;
		p->subs[SUB_REAL].needbusy = 0;
		dahdi_setlinear(p->subs[SUB_REAL].dfd, 0);

		p->owner = NULL;
		p->ringt = 0;/* Probably not used in this mode.  Reset anyway. */
		p->distinctivering = 0;/* Probably not used in this mode. Reset anyway. */
		p->confirmanswer = 0;/* Probably not used in this mode. Reset anyway. */
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;/* Probably not used in this mode. Reset anyway. */

		revert_fax_buffers(p, ast);

		p->law = p->law_default;
		law = p->law_default;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n",
				p->channel, strerror(errno));
		}

		sig_ss7_hangup(p->sig_pvt, ast);

		tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
		dahdi_disable_ec(p);

		x = 0;
		ast_channel_setoption(ast, AST_OPTION_TDD, &x, sizeof(char), 0);
		p->didtdd = 0;/* Probably not used in this mode. Reset anyway. */

		update_conf(p);
		reset_conf(p);

		/* Restore data mode */
		x = 0;
		ast_channel_setoption(ast, AST_OPTION_AUDIO_MODE, &x, sizeof(char), 0);

		if (num_restart_pending == 0) {
			restart_monitor();
		}
		goto hangup_out;
	}
#endif	/* defined(HAVE_SS7) */

	idx = dahdi_get_index(ast, p, 1);

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
				if (ast_channel_state(p->owner) != AST_STATE_UP)
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
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}

		revert_fax_buffers(p, ast);

		p->law = p->law_default;
		law = p->law_default;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (res < 0)
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n", p->channel, strerror(errno));
		/* Perform low level hangup if no owner left */
#ifdef HAVE_OPENR2
		if (p->mfcr2 && p->mfcr2call && openr2_chan_get_direction(p->r2chan) != OR2_DIR_STOPPED) {
			ast_debug(1, "disconnecting MFC/R2 call on chan %d\n", p->channel);
			/* If it's an incoming call, check the mfcr2_forced_release setting */
			if (openr2_chan_get_direction(p->r2chan) == OR2_DIR_BACKWARD && p->mfcr2_forced_release) {
				dahdi_r2_disconnect_call(p, OR2_CAUSE_FORCED_RELEASE);
			} else {
				const char *r2causestr = pbx_builtin_getvar_helper(ast, "MFCR2_CAUSE");
				int r2cause_user = r2causestr ? atoi(r2causestr) : 0;
				openr2_call_disconnect_cause_t r2cause = r2cause_user ? dahdi_ast_cause_to_r2_cause(r2cause_user)
					                                              : dahdi_ast_cause_to_r2_cause(ast_channel_hangupcause(ast));
				dahdi_r2_disconnect_call(p, r2cause);
			}
		} else if (p->mfcr2call) {
			ast_debug(1, "Clearing call request on channel %d\n", p->channel);
			/* since ast_request() was called but not ast_call() we have not yet dialed
			and the openr2 stack will not call on_call_end callback, we need to unset
			the mfcr2call flag and bump the monitor count so the monitor thread can take
			care of this channel events from now on */
			p->mfcr2call = 0;
		}
#endif
		switch (p->sig) {
		case SIG_SS7:
		case SIG_MFCR2:
		case SIG_PRI_LIB_HANDLE_CASES:
		case 0:
			break;
		default:
			res = dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
			break;
		}
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast_channel_name(ast));
		}
		switch (p->sig) {
		case SIG_FXOGS:
		case SIG_FXOLS:
		case SIG_FXOKS:
			memset(&par, 0, sizeof(par));
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &par);
			if (!res) {
				struct analog_pvt *analog_p = p->sig_pvt;
#if 0
				ast_debug(1, "Hanging up channel %d, offhook = %d\n", p->channel, par.rxisoffhook);
#endif
				/* If they're off hook, try playing congestion */
				if ((par.rxisoffhook) && (!(p->radio || (p->oprmode < 0))))
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
				else
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
				analog_p->fxsoffhookstate = par.rxisoffhook;
			}
			break;
		case SIG_FXSGS:
		case SIG_FXSLS:
		case SIG_FXSKS:
			/* Make sure we're not made available for at least two seconds assuming
			we were actually used for an inbound or outbound call. */
			if (ast_channel_state(ast) != AST_STATE_RESERVED) {
				time(&p->guardtime);
				p->guardtime += 2;
			}
			break;
		default:
			tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
			break;
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
		switch (p->sig) {
		case SIG_PRI_LIB_HANDLE_CASES:
		case SIG_SS7:
			x = 0;
			ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
			break;
		default:
			break;
		}
		if (num_restart_pending == 0)
			restart_monitor();
	}

	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	p->cid_suppress_expire = 0;
	p->oprmode = 0;
hangup_out:
	ast_channel_tech_pvt_set(ast, NULL);
	ast_free(p->cidspill);
	p->cidspill = NULL;

	ast_mutex_unlock(&p->lock);
	ast_verb(3, "Hungup '%s'\n", ast_channel_name(ast));

	ast_mutex_lock(&iflock);
	if (p->restartpending) {
		num_restart_pending--;
	}

	if (p->destroy) {
		destroy_channel(p, 0);
	}
	ast_mutex_unlock(&iflock);

	ast_module_unref(ast_module_info->self);
	return 0;
}

static int dahdi_answer(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	int res = 0;
	int idx;
	ast_setstate(ast, AST_STATE_UP);/*! \todo XXX this is redundantly set by the analog and PRI submodules! */
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
#if defined(HAVE_PRI)
	case SIG_PRI_LIB_HANDLE_CASES:
		res = sig_pri_answer(p->sig_pvt, ast);
		break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	case SIG_SS7:
		res = sig_ss7_answer(p->sig_pvt, ast);
		break;
#endif	/* defined(HAVE_SS7) */
#ifdef HAVE_OPENR2
	case SIG_MFCR2:
		if (!p->mfcr2_call_accepted) {
			/* The call was not accepted on offer nor the user, so it must be accepted now before answering,
			   openr2_chan_answer_call will be called when the callback on_call_accepted is executed */
			p->mfcr2_answer_pending = 1;
			if (p->mfcr2_charge_calls) {
				ast_debug(1, "Accepting MFC/R2 call with charge before answering on chan %d\n", p->channel);
				openr2_chan_accept_call(p->r2chan, OR2_CALL_WITH_CHARGE);
			} else {
				ast_debug(1, "Accepting MFC/R2 call with no charge before answering on chan %d\n", p->channel);
				openr2_chan_accept_call(p->r2chan, OR2_CALL_NO_CHARGE);
			}
		} else {
			ast_debug(1, "Answering MFC/R2 call on chan %d\n", p->channel);
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
		break;
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(chan);

	/* all supported options require data */
	if (!p || !data || (*datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		*cp = p->ignoredtmf ? 0 : 1;
		ast_debug(1, "Reporting digit detection %sabled on %s\n", *cp ? "en" : "dis", ast_channel_name(chan));
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		*cp = (p->dsp_features & DSP_FEATURE_FAX_DETECT) ? 0 : 1;
		ast_debug(1, "Reporting fax tone detection %sabled on %s\n", *cp ? "en" : "dis", ast_channel_name(chan));
		break;
	case AST_OPTION_CC_AGENT_TYPE:
#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CCSS)
		if (dahdi_sig_pri_lib_handles(p->sig)) {
			ast_copy_string((char *) data, dahdi_pri_cc_type, *datalen);
			break;
		}
#endif	/* defined(HAVE_PRI_CCSS) */
#endif	/* defined(HAVE_PRI) */
		return -1;
	default:
		return -1;
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(chan), *pp;
	struct oprmode *oprmode;


	/* all supported options require data */
	if (!p || !data || (datalen < 1)) {
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
		ast_debug(1, "Setting actual tx gain on %s to %f\n", ast_channel_name(chan), p->txgain + (float) *scp);
		return set_actual_txgain(p->subs[idx].dfd, p->txgain + (float) *scp, p->txdrc, p->law);
	case AST_OPTION_RXGAIN:
		scp = (signed char *) data;
		idx = dahdi_get_index(chan, p, 0);
		if (idx < 0) {
			ast_log(LOG_WARNING, "No index in RXGAIN?\n");
			return -1;
		}
		ast_debug(1, "Setting actual rx gain on %s to %f\n", ast_channel_name(chan), p->rxgain + (float) *scp);
		return set_actual_rxgain(p->subs[idx].dfd, p->rxgain + (float) *scp, p->rxdrc, p->law);
	case AST_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",ast_channel_name(chan));
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		case 2:
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",ast_channel_name(chan));
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
			break;
		default:
			ast_debug(1, "Set option TONE VERIFY, mode: OFF(0) on %s\n",ast_channel_name(chan));
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		}
		break;
	case AST_OPTION_TDD:
		/* turn on or off TDD */
		cp = (char *) data;
		p->mate = 0;
		if (!*cp) { /* turn it off */
			ast_debug(1, "Set option TDD MODE, value: OFF(0) on %s\n",ast_channel_name(chan));
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			break;
		}
		ast_debug(1, "Set option TDD MODE, value: %s(%d) on %s\n",
			(*cp == 2) ? "MATE" : "ON", (int) *cp, ast_channel_name(chan));
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
		if (!p->tdd) { /* if we don't have one yet */
			p->tdd = tdd_new(); /* allocate one */
		}
		break;
	case AST_OPTION_RELAXDTMF:  /* Relax DTMF decoding (or not) */
		if (!p->dsp)
			break;
		cp = (char *) data;
		ast_debug(1, "Set option RELAX DTMF, value: %s(%d) on %s\n",
			*cp ? "ON" : "OFF", (int) *cp, ast_channel_name(chan));
		ast_dsp_set_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
		break;
	case AST_OPTION_AUDIO_MODE:  /* Set AUDIO mode (or not) */
#if defined(HAVE_PRI)
		if (dahdi_sig_pri_lib_handles(p->sig)
			&& ((struct sig_pri_chan *) p->sig_pvt)->no_b_channel) {
			/* PRI nobch pseudo channel.  Does not handle ioctl(DAHDI_AUDIOMODE) */
			break;
		}
#endif	/* defined(HAVE_PRI) */

		cp = (char *) data;
		if (!*cp) {
			ast_debug(1, "Set option AUDIO MODE, value: OFF(0) on %s\n", ast_channel_name(chan));
			x = 0;
			dahdi_disable_ec(p);
		} else {
			ast_debug(1, "Set option AUDIO MODE, value: ON(1) on %s\n", ast_channel_name(chan));
			x = 1;
		}
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x) == -1)
			ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", p->channel, x, strerror(errno));
		break;
	case AST_OPTION_OPRMODE:  /* Operator services mode */
		oprmode = (struct oprmode *) data;
		/* We don't support operator mode across technologies */
		if (strcasecmp(ast_channel_tech(chan)->type, ast_channel_tech(oprmode->peer)->type)) {
			ast_log(LOG_NOTICE, "Operator mode not supported on %s to %s calls.\n",
					ast_channel_tech(chan)->type, ast_channel_tech(oprmode->peer)->type);
			errno = EINVAL;
			return -1;
		}
		pp = ast_channel_tech_pvt(oprmode->peer);
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
			oprmode->mode, ast_channel_name(chan),ast_channel_name(oprmode->peer));
		break;
	case AST_OPTION_ECHOCAN:
		cp = (char *) data;
		if (*cp) {
			ast_debug(1, "Enabling echo cancellation on %s\n", ast_channel_name(chan));
			dahdi_enable_ec(p);
		} else {
			ast_debug(1, "Disabling echo cancellation on %s\n", ast_channel_name(chan));
			dahdi_disable_ec(p);
		}
		break;
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		ast_debug(1, "%sabling digit detection on %s\n", *cp ? "En" : "Dis", ast_channel_name(chan));
		if (*cp) {
			enable_dtmf_detect(p);
		} else {
			disable_dtmf_detect(p);
		}
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		if (p->dsp) {
			ast_debug(1, "%sabling fax tone detection on %s\n", *cp ? "En" : "Dis", ast_channel_name(chan));
			if (*cp) {
				p->dsp_features |= DSP_FEATURE_FAX_DETECT;
			} else {
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(chan);
	int res = 0;

	if (!p) {
		/* No private structure! */
		*buf = '\0';
		return -1;
	}

	if (!strcasecmp(data, "rxgain")) {
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%f", p->rxgain);
		ast_mutex_unlock(&p->lock);
	} else if (!strcasecmp(data, "txgain")) {
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%f", p->txgain);
		ast_mutex_unlock(&p->lock);
	} else if (!strcasecmp(data, "dahdi_channel")) {
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%d", p->channel);
		ast_mutex_unlock(&p->lock);
	} else if (!strcasecmp(data, "dahdi_span")) {
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%d", p->span);
		ast_mutex_unlock(&p->lock);
	} else if (!strcasecmp(data, "dahdi_type")) {
		ast_mutex_lock(&p->lock);
		switch (p->sig) {
#if defined(HAVE_OPENR2)
		case SIG_MFCR2:
			ast_copy_string(buf, "mfc/r2", len);
			break;
#endif	/* defined(HAVE_OPENR2) */
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			ast_copy_string(buf, "pri", len);
			break;
#endif	/* defined(HAVE_PRI) */
		case 0:
			ast_copy_string(buf, "pseudo", len);
			break;
#if defined(HAVE_SS7)
		case SIG_SS7:
			ast_copy_string(buf, "ss7", len);
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			/* The only thing left is analog ports. */
			ast_copy_string(buf, "analog", len);
			break;
		}
		ast_mutex_unlock(&p->lock);
#if defined(HAVE_PRI)
#if defined(HAVE_PRI_REVERSE_CHARGE)
	} else if (!strcasecmp(data, "reversecharge")) {
		ast_mutex_lock(&p->lock);
		switch (p->sig) {
		case SIG_PRI_LIB_HANDLE_CASES:
			snprintf(buf, len, "%d", ((struct sig_pri_chan *) p->sig_pvt)->reverse_charging_indication);
			break;
		default:
			*buf = '\0';
			res = -1;
			break;
		}
		ast_mutex_unlock(&p->lock);
#endif
#if defined(HAVE_PRI_SETUP_KEYPAD)
	} else if (!strcasecmp(data, "keypad_digits")) {
		ast_mutex_lock(&p->lock);
		switch (p->sig) {
		case SIG_PRI_LIB_HANDLE_CASES:
			ast_copy_string(buf, ((struct sig_pri_chan *) p->sig_pvt)->keypad_digits,
				len);
			break;
		default:
			*buf = '\0';
			res = -1;
			break;
		}
		ast_mutex_unlock(&p->lock);
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
	} else if (!strcasecmp(data, "no_media_path")) {
		ast_mutex_lock(&p->lock);
		switch (p->sig) {
		case SIG_PRI_LIB_HANDLE_CASES:
			/*
			 * TRUE if the call is on hold or is call waiting because
			 * there is no media path available.
			 */
			snprintf(buf, len, "%d", ((struct sig_pri_chan *) p->sig_pvt)->no_b_channel);
			break;
		default:
			*buf = '\0';
			res = -1;
			break;
		}
		ast_mutex_unlock(&p->lock);
#endif	/* defined(HAVE_PRI) */
	} else {
		*buf = '\0';
		res = -1;
	}

	return res;
}


static int parse_buffers_policy(const char *parse, int *num_buffers, int *policy)
{
	int res;
	char policy_str[21] = "";

	if ((res = sscanf(parse, "%30d,%20s", num_buffers, policy_str)) != 2) {
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(chan);
	int res = 0;

	if (!p) {
		/* No private structure! */
		return -1;
	}

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
	struct timeval start = ast_tvnow();
#ifdef PRI_2BCT
	int triedtopribridge = 0;
	q931_call *q931c0;
	q931_call *q931c1;
#endif

	/* For now, don't attempt to native bridge if either channel needs DTMF detection.
	   There is code below to handle it properly until DTMF is actually seen,
	   but due to currently unresolved issues it's ignored...
	*/

	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return AST_BRIDGE_FAILED_NOWARN;

	ast_channel_lock_both(c0, c1);

	p0 = ast_channel_tech_pvt(c0);
	p1 = ast_channel_tech_pvt(c1);
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

	op0 = p0 = ast_channel_tech_pvt(c0);
	op1 = p1 = ast_channel_tech_pvt(c1);
	ofd0 = ast_channel_fd(c0, 0);
	ofd1 = ast_channel_fd(c1, 0);
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

	if ((p0->callwaiting && p0->callwaitingcallerid)
		|| (p1->callwaiting && p1->callwaitingcallerid)) {
		/*
		 * Call Waiting Caller ID requires DTMF detection to know if it
		 * can send the CID spill.
		 *
		 * For now, don't attempt to native bridge if either channel
		 * needs DTMF detection.  There is code below to handle it
		 * properly until DTMF is actually seen, but due to currently
		 * unresolved issues it's ignored...
		 */
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&p1->lock);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}

#if defined(HAVE_PRI)
	if ((dahdi_sig_pri_lib_handles(p0->sig)
			&& ((struct sig_pri_chan *) p0->sig_pvt)->no_b_channel)
		|| (dahdi_sig_pri_lib_handles(p1->sig)
			&& ((struct sig_pri_chan *) p1->sig_pvt)->no_b_channel)) {
		/*
		 * PRI nobch channels (hold and call waiting) are equivalent to
		 * pseudo channels and cannot be done here.
		 */
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&p1->lock);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}
#endif	/* defined(HAVE_PRI) */

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
			(ast_channel_state(p1->subs[SUB_REAL].owner) == AST_STATE_RINGING)) {
			ast_debug(1,
				"Playing ringback on %d/%d(%s) since %d/%d(%s) is in a ringing three-way\n",
				p0->channel, oi0, ast_channel_name(c0), p1->channel, oi1, ast_channel_name(c1));
			tone_zone_play_tone(p0->subs[oi0].dfd, DAHDI_TONE_RINGTONE);
			os1 = ast_channel_state(p1->subs[SUB_REAL].owner);
		} else {
			ast_debug(1, "Stopping tones on %d/%d(%s) talking to %d/%d(%s)\n",
				p0->channel, oi0, ast_channel_name(c0), p1->channel, oi1, ast_channel_name(c1));
			tone_zone_play_tone(p0->subs[oi0].dfd, -1);
		}
		if ((oi0 == SUB_THREEWAY) &&
			p0->subs[SUB_THREEWAY].inthreeway &&
			p0->subs[SUB_REAL].owner &&
			p0->subs[SUB_REAL].inthreeway &&
			(ast_channel_state(p0->subs[SUB_REAL].owner) == AST_STATE_RINGING)) {
			ast_debug(1,
				"Playing ringback on %d/%d(%s) since %d/%d(%s) is in a ringing three-way\n",
				p1->channel, oi1, ast_channel_name(c1), p0->channel, oi0, ast_channel_name(c0));
			tone_zone_play_tone(p1->subs[oi1].dfd, DAHDI_TONE_RINGTONE);
			os0 = ast_channel_state(p0->subs[SUB_REAL].owner);
		} else {
			ast_debug(1, "Stopping tones on %d/%d(%s) talking to %d/%d(%s)\n",
				p1->channel, oi1, ast_channel_name(c1), p0->channel, oi0, ast_channel_name(c0));
			tone_zone_play_tone(p1->subs[oi1].dfd, -1);
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

	ast_verb(3, "Native bridging %s and %s\n", ast_channel_name(c0), ast_channel_name(c1));

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0) && (oi0 == SUB_REAL))
		disable_dtmf_detect(op0);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1) && (oi1 == SUB_REAL))
		disable_dtmf_detect(op1);

	for (;;) {
		struct ast_channel *c0_priority[2] = {c0, c1};
		struct ast_channel *c1_priority[2] = {c1, c0};
		int ms;

		/* Here's our main loop...  Start by locking things, looking for private parts,
		   and then balking if anything is wrong */

		ast_channel_lock_both(c0, c1);

		p0 = ast_channel_tech_pvt(c0);
		p1 = ast_channel_tech_pvt(c1);

		if (op0 == p0)
			i0 = dahdi_get_index(c0, p0, 1);
		if (op1 == p1)
			i1 = dahdi_get_index(c1, p1, 1);

		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		ms = ast_remaining_ms(start, timeoutms);
		if (!ms ||
			(op0 != p0) ||
			(op1 != p1) ||
			(ofd0 != ast_channel_fd(c0, 0)) ||
			(ofd1 != ast_channel_fd(c1, 0)) ||
			(p0->subs[SUB_REAL].owner && (os0 > -1) && (os0 != ast_channel_state(p0->subs[SUB_REAL].owner))) ||
			(p1->subs[SUB_REAL].owner && (os1 > -1) && (os1 != ast_channel_state(p1->subs[SUB_REAL].owner))) ||
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
		if (!triedtopribridge) {
			triedtopribridge = 1;
			if (p0->pri && p0->pri == p1->pri && p0->pri->transfer) {
				ast_mutex_lock(&p0->pri->lock);
				switch (p0->sig) {
				case SIG_PRI_LIB_HANDLE_CASES:
					q931c0 = ((struct sig_pri_chan *) (p0->sig_pvt))->call;
					break;
				default:
					q931c0 = NULL;
					break;
				}
				switch (p1->sig) {
				case SIG_PRI_LIB_HANDLE_CASES:
					q931c1 = ((struct sig_pri_chan *) (p1->sig_pvt))->call;
					break;
				default:
					q931c1 = NULL;
					break;
				}
				if (q931c0 && q931c1) {
					pri_channel_bridge(q931c0, q931c1);
				}
				ast_mutex_unlock(&p0->pri->lock);
			}
		}
#endif

		who = ast_waitfor_n(priority ? c0_priority : c1_priority, 2, &ms);
		if (!who) {
			ast_debug(1, "Ooh, empty read...\n");
			continue;
		}
		f = ast_read(who);
		switch (f ? f->frametype : AST_FRAME_CONTROL) {
		case AST_FRAME_CONTROL:
			if (f && f->subclass.integer == AST_CONTROL_PVT_CAUSE_CODE) {
				ast_channel_hangupcause_hash_set((who == c0) ? c1 : c0, f->data.ptr, f->datalen);
				break;
			}
			*fo = f;
			*rc = who;
			res = AST_BRIDGE_COMPLETE;
			goto return_from_bridge;
		case AST_FRAME_DTMF_END:
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
			break;
		case AST_FRAME_TEXT:
			if (who == c0) {
				ast_write(c1, f);
			} else {
				ast_write(c0, f);
			}
			break;
		case AST_FRAME_VOICE:
			/* Native bridge handles voice frames in hardware. */
		case AST_FRAME_NULL:
			break;
		default:
			ast_debug(1, "Chan '%s' is discarding frame of frametype:%u\n",
				ast_channel_name(who), f->frametype);
			break;
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(newchan);
	int x;

	ast_mutex_lock(&p->lock);

	ast_debug(1, "New owner for channel %d is %s\n", p->channel, ast_channel_name(newchan));
	if (p->owner == oldchan) {
		p->owner = newchan;
	}
	for (x = 0; x < 3; x++) {
		if (p->subs[x].owner == oldchan) {
			if (!x) {
				dahdi_unlink(NULL, p, 0);
			}
			p->subs[x].owner = newchan;
		}
	}
	if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
		analog_fixup(oldchan, newchan, p->sig_pvt);
#if defined(HAVE_PRI)
	} else if (dahdi_sig_pri_lib_handles(p->sig)) {
		sig_pri_fixup(oldchan, newchan, p->sig_pvt);
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	} else if (p->sig == SIG_SS7) {
		sig_ss7_fixup(oldchan, newchan, p->sig_pvt);
#endif	/* defined(HAVE_SS7) */
	}
	update_conf(p);

	ast_mutex_unlock(&p->lock);

	if (ast_channel_state(newchan) == AST_STATE_RINGING) {
		dahdi_indicate(newchan, AST_CONTROL_RINGING, NULL, 0);
	}
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
		if (ast_channel_state(p->subs[SUB_REAL].owner) == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[SUB_REAL].owner), AST_CONTROL_RINGING);
		}
		if (ast_channel_state(p->subs[SUB_THREEWAY].owner) == AST_STATE_RING) {
			tone_zone_play_tone(p->subs[SUB_THREEWAY].dfd, DAHDI_TONE_RINGTONE);
		}
		 if (ast_channel_masquerade(p->subs[SUB_THREEWAY].owner, ast_bridged_channel(p->subs[SUB_REAL].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_channel_name(ast_bridged_channel(p->subs[SUB_REAL].owner)), ast_channel_name(p->subs[SUB_THREEWAY].owner));
			return -1;
		}
		/* Orphan the channel after releasing the lock */
		ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
		unalloc_sub(p, SUB_THREEWAY);
	} else if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
		ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
		if (ast_channel_state(p->subs[SUB_THREEWAY].owner) == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[SUB_THREEWAY].owner), AST_CONTROL_RINGING);
		}
		if (ast_channel_state(p->subs[SUB_REAL].owner) == AST_STATE_RING) {
			tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
		}
		if (ast_channel_masquerade(p->subs[SUB_REAL].owner, ast_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_channel_name(ast_bridged_channel(p->subs[SUB_THREEWAY].owner)), ast_channel_name(p->subs[SUB_REAL].owner));
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
			ast_channel_name(p->subs[SUB_REAL].owner), ast_channel_name(p->subs[SUB_THREEWAY].owner));
		ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
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

static void dahdi_handle_dtmf(struct ast_channel *ast, int idx, struct ast_frame **dest)
{
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_frame *f = *dest;

	ast_debug(1, "%s DTMF digit: 0x%02X '%c' on %s\n",
		f->frametype == AST_FRAME_DTMF_BEGIN ? "Begin" : "End",
		(unsigned)f->subclass.integer, f->subclass.integer, ast_channel_name(ast));

	if (p->confirmanswer) {
		if (f->frametype == AST_FRAME_DTMF_END) {
			ast_debug(1, "Confirm answer on %s!\n", ast_channel_name(ast));
			/* Upon receiving a DTMF digit, consider this an answer confirmation instead
			   of a DTMF digit */
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
			/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
			p->confirmanswer = 0;
		} else {
			p->subs[idx].f.frametype = AST_FRAME_NULL;
			p->subs[idx].f.subclass.integer = 0;
		}
		*dest = &p->subs[idx].f;
	} else if (p->callwaitcas) {
		if (f->frametype == AST_FRAME_DTMF_END) {
			if ((f->subclass.integer == 'A') || (f->subclass.integer == 'D')) {
				ast_debug(1, "Got some DTMF, but it's for the CAS\n");
				ast_free(p->cidspill);
				p->cidspill = NULL;
				send_cwcidspill(p);
			}
			p->callwaitcas = 0;
		}
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		*dest = &p->subs[idx].f;
	} else if (f->subclass.integer == 'f') {
		if (f->frametype == AST_FRAME_DTMF_END) {
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
						ast_log(LOG_WARNING, "Channel '%s' unable to set buffer policy, reason: %s\n", ast_channel_name(ast), strerror(errno));
					} else {
						p->bufferoverrideinuse = 1;
					}
				}
				p->faxhandled = 1;
				if (p->dsp) {
					p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
					ast_dsp_set_features(p->dsp, p->dsp_features);
					ast_debug(1, "Disabling FAX tone detection on %s after tone received\n", ast_channel_name(ast));
				}
				if (strcmp(ast_channel_exten(ast), "fax")) {
					const char *target_context = S_OR(ast_channel_macrocontext(ast), ast_channel_context(ast));

					/* We need to unlock 'ast' here because ast_exists_extension has the
					 * potential to start autoservice on the channel. Such action is prone
					 * to deadlock.
					 */
					ast_mutex_unlock(&p->lock);
					ast_channel_unlock(ast);
					if (ast_exists_extension(ast, target_context, "fax", 1,
						S_COR(ast_channel_caller(ast)->id.number.valid, ast_channel_caller(ast)->id.number.str, NULL))) {
						ast_channel_lock(ast);
						ast_mutex_lock(&p->lock);
						ast_verb(3, "Redirecting %s to fax extension\n", ast_channel_name(ast));
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast_channel_exten(ast));
						if (ast_async_goto(ast, target_context, "fax", 1))
							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast_channel_name(ast), target_context);
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
		}
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		*dest = &p->subs[idx].f;
	}
}

static void handle_alarms(struct dahdi_pvt *p, int alms)
{
	const char *alarm_str;

#if defined(HAVE_PRI)
	if (dahdi_sig_pri_lib_handles(p->sig) && sig_pri_is_alarm_ignored(p->pri)) {
		return;
	}
#endif	/* defined(HAVE_PRI) */

	alarm_str = alarm2str(alms);
	if (report_alarms & REPORT_CHANNEL_ALARMS) {
		ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm_str);
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an alarm is set on a DAHDI channel.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_SYSTEM, "Alarm",
					  "Alarm: %s\r\n"
					  "Channel: %d\r\n",
					  alarm_str, p->channel);
	}

	if (report_alarms & REPORT_SPAN_ALARMS && p->manages_span_alarms) {
		ast_log(LOG_WARNING, "Detected alarm on span %d: %s\n", p->span, alarm_str);
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an alarm is set on a DAHDI span.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_SYSTEM, "SpanAlarm",
					  "Alarm: %s\r\n"
					  "Span: %d\r\n",
					  alarm_str, p->span);
	}
}

static struct ast_frame *dahdi_handle_event(struct ast_channel *ast)
{
	int res, x;
	int idx, mysig;
	char *c;
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	pthread_t threadid;
	struct ast_channel *chan;
	struct ast_frame *f;

	idx = dahdi_get_index(ast, p, 0);
	if (idx < 0) {
		return &ast_null_frame;
	}
	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;
	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.src = "dahdi_handle_event";
	p->subs[idx].f.data.ptr = NULL;
	f = &p->subs[idx].f;

	if (p->fake_event) {
		res = p->fake_event;
		p->fake_event = 0;
	} else
		res = dahdi_get_event(p->subs[idx].dfd);

	ast_debug(1, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, idx);

	if (res & (DAHDI_EVENT_PULSEDIGIT | DAHDI_EVENT_DTMFUP)) {
		p->pulsedial = (res & DAHDI_EVENT_PULSEDIGIT) ? 1 : 0;
		ast_debug(1, "Detected %sdigit '%c'\n", p->pulsedial ? "pulse ": "", res & 0xff);
#if defined(HAVE_PRI)
		if (dahdi_sig_pri_lib_handles(p->sig)
			&& ((struct sig_pri_chan *) p->sig_pvt)->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING
			&& p->pri
			&& (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
			/* absorb event */
		} else
#endif	/* defined(HAVE_PRI) */
		{
			/* Unmute conference */
			dahdi_confmute(p, 0);
			p->subs[idx].f.frametype = AST_FRAME_DTMF_END;
			p->subs[idx].f.subclass.integer = res & 0xff;
			dahdi_handle_dtmf(ast, idx, &f);
		}
		return f;
	}

	if (res & DAHDI_EVENT_DTMFDOWN) {
		ast_debug(1, "DTMF Down '%c'\n", res & 0xff);
#if defined(HAVE_PRI)
		if (dahdi_sig_pri_lib_handles(p->sig)
			&& ((struct sig_pri_chan *) p->sig_pvt)->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING
			&& p->pri
			&& (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
			/* absorb event */
		} else
#endif	/* defined(HAVE_PRI) */
		{
			/* Mute conference */
			dahdi_confmute(p, 1);
			p->subs[idx].f.frametype = AST_FRAME_DTMF_BEGIN;
			p->subs[idx].f.subclass.integer = res & 0xff;
			dahdi_handle_dtmf(ast, idx, &f);
		}
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
			ast_log(LOG_WARNING, "Received bits changed on %s signalling?\n", sig2str(p->sig));
		} else {
			ast_debug(1, "bits changed in chan %d\n", p->channel);
			openr2_chan_handle_cas(p->r2chan);
		}
#else
		ast_log(LOG_WARNING, "Received bits changed on %s signalling?\n", sig2str(p->sig));
#endif
		break;
	case DAHDI_EVENT_PULSE_START:
		/* Stop tone if there's a pulse start and the PBX isn't started */
		if (!ast_channel_pbx(ast))
			tone_zone_play_tone(p->subs[idx].dfd, -1);
		break;
	case DAHDI_EVENT_DIALCOMPLETE:
		/* DAHDI has completed dialing all digits sent using DAHDI_DIAL. */
#if defined(HAVE_PRI)
		if (dahdi_sig_pri_lib_handles(p->sig)) {
			if (p->inalarm) {
				break;
			}
			if (ioctl(p->subs[idx].dfd, DAHDI_DIALING, &x) == -1) {
				ast_debug(1, "DAHDI_DIALING ioctl failed on %s: %s\n",
					ast_channel_name(ast), strerror(errno));
				return NULL;
			}
			if (x) {
				/* Still dialing in DAHDI driver */
				break;
			}
			/*
			 * The ast channel is locked and the private may be locked more
			 * than once.
			 */
			sig_pri_dial_complete(p->sig_pvt, ast);
			break;
		}
#endif	/* defined(HAVE_PRI) */
#ifdef HAVE_OPENR2
		if ((p->sig & SIG_MFCR2) && p->r2chan && ast_channel_state(ast) != AST_STATE_UP) {
			/* we don't need to do anything for this event for R2 signaling
			   if the call is being setup */
			break;
		}
#endif
		if (p->inalarm) break;
		if ((p->radio || (p->oprmode < 0))) break;
		if (ioctl(p->subs[idx].dfd,DAHDI_DIALING,&x) == -1) {
			ast_debug(1, "DAHDI_DIALING ioctl failed on %s: %s\n",ast_channel_name(ast), strerror(errno));
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
					if (ast_channel_state(ast) == AST_STATE_DIALING_OFFHOOK) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
						break;
					} else { /* if to state wait for offhook to dial rest */
						/* we now wait for off hook */
						ast_setstate(ast,AST_STATE_DIALING_OFFHOOK);
					}
				}
				if (ast_channel_state(ast) == AST_STATE_DIALING) {
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
						p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
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
		switch (p->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			sig_pri_chan_alarm_notify(p->sig_pvt, 0);
			break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
			sig_ss7_set_alarm(p->sig_pvt, 1);
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			p->inalarm = 1;
			break;
		}
		res = get_alarms(p);
		handle_alarms(p, res);
#ifdef HAVE_PRI
		if (!p->pri || !p->pri->pri || pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0) {
			/* fall through intentionally */
		} else {
			break;
		}
#endif
#if defined(HAVE_SS7)
		if (p->sig == SIG_SS7)
			break;
#endif	/* defined(HAVE_SS7) */
#ifdef HAVE_OPENR2
		if (p->sig == SIG_MFCR2)
			break;
#endif
	case DAHDI_EVENT_ONHOOK:
		if (p->radio) {
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_UNKEY;
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
					p->cid_suppress_expire = 0;
					p->owner = NULL;
					/* Don't start streaming audio yet if the incoming call isn't up yet */
					if (ast_channel_state(p->subs[SUB_REAL].owner) != AST_STATE_UP)
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
					ast_debug(1, "Last flash was %u ms ago\n", mssinceflash);
					if (mssinceflash < MIN_MS_SINCE_FLASH) {
						/* It hasn't been long enough since the last flashook.  This is probably a bounce on
						   hanging up.  Hangup both channels now */
						if (p->subs[SUB_THREEWAY].owner)
							ast_queue_hangup_with_cause(p->subs[SUB_THREEWAY].owner, AST_CAUSE_NO_ANSWER);
						ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
						ast_debug(1, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
						ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
					} else if ((ast_channel_pbx(ast)) || (ast_channel_state(ast) == AST_STATE_UP)) {
						if (p->transfer) {
							/* In any case this isn't a threeway call anymore */
							p->subs[SUB_REAL].inthreeway = 0;
							p->subs[SUB_THREEWAY].inthreeway = 0;
							/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
							if (!p->transfertobusy && ast_channel_state(ast) == AST_STATE_BUSY) {
								ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
								/* Swap subs and dis-own channel */
								swap_subs(p, SUB_THREEWAY, SUB_REAL);
								p->owner = NULL;
								/* Ring the phone */
								dahdi_ring_phone(p);
							} else {
								if ((res = attempt_transfer(p)) < 0) {
									ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
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
							ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
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
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_KEY;
			break;
 		}
		/* for E911, its supposed to wait for offhook then dial
		   the second half of the dial string */
		if (((mysig == SIG_E911) || (mysig == SIG_FGC_CAMA) || (mysig == SIG_FGC_CAMAMF)) && (ast_channel_state(ast) == AST_STATE_DIALING_OFFHOOK)) {
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
			switch (ast_channel_state(ast)) {
			case AST_STATE_RINGING:
				dahdi_enable_ec(p);
				dahdi_train_ec(p);
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
				/* Make sure it stops ringing */
				p->subs[SUB_REAL].needringing = 0;
				dahdi_set_hook(p->subs[idx].dfd, DAHDI_OFFHOOK);
				ast_debug(1, "channel %d answered\n", p->channel);

				/* Cancel any running CallerID spill */
				ast_free(p->cidspill);
				p->cidspill = NULL;
				restore_conference(p);

				p->dialing = 0;
				p->callwaitcas = 0;
				if (p->confirmanswer) {
					/* Ignore answer if "confirm answer" is enabled */
					p->subs[idx].f.frametype = AST_FRAME_NULL;
					p->subs[idx].f.subclass.integer = 0;
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
						p->subs[idx].f.subclass.integer = 0;
						p->dialing = 1;
					}
					p->dop.dialstr[0] = '\0';
					ast_setstate(ast, AST_STATE_DIALING);
				} else
					ast_setstate(ast, AST_STATE_UP);
				return &p->subs[idx].f;
			case AST_STATE_DOWN:
				ast_setstate(ast, AST_STATE_RING);
				ast_channel_rings_set(ast, 1);
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass.integer = AST_CONTROL_OFFHOOK;
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
				ast_log(LOG_WARNING, "FXO phone off hook in weird state %u??\n", ast_channel_state(ast));
			}
			break;
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
			if (ast_channel_state(ast) == AST_STATE_RING) {
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
			if (ast_channel_state(ast) == AST_STATE_PRERING)
				ast_setstate(ast, AST_STATE_RING);
			if ((ast_channel_state(ast) == AST_STATE_DOWN) || (ast_channel_state(ast) == AST_STATE_RING)) {
				ast_debug(1, "Ring detected\n");
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass.integer = AST_CONTROL_RING;
			} else if (p->outgoing && ((ast_channel_state(ast) == AST_STATE_RINGING) || (ast_channel_state(ast) == AST_STATE_DIALING))) {
				ast_debug(1, "Line answered\n");
				if (p->confirmanswer) {
					p->subs[idx].f.frametype = AST_FRAME_NULL;
					p->subs[idx].f.subclass.integer = 0;
				} else {
					p->subs[idx].f.frametype = AST_FRAME_CONTROL;
					p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
					ast_setstate(ast, AST_STATE_UP);
				}
			} else if (ast_channel_state(ast) != AST_STATE_RING)
				ast_log(LOG_WARNING, "Ring/Off-hook in strange state %u on channel %d\n", ast_channel_state(ast), p->channel);
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
			if (ast_channel_state(ast) == AST_STATE_RING) {
				p->ringt = p->ringt_base;
			}
			break;
		}
		break;
	case DAHDI_EVENT_RINGERON:
		break;
	case DAHDI_EVENT_NOALARM:
		switch (p->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			sig_pri_chan_alarm_notify(p->sig_pvt, 1);
			break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
			sig_ss7_set_alarm(p->sig_pvt, 0);
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			p->inalarm = 0;
			break;
		}
		handle_clear_alarms(p);
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

			/* Cancel any running CallerID spill */
			ast_free(p->cidspill);
			p->cidspill = NULL;
			restore_conference(p);
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
				ast_debug(1, "Making %s the new owner\n", ast_channel_name(p->owner));
				if (ast_channel_state(p->owner) == AST_STATE_RINGING) {
					ast_setstate(p->owner, AST_STATE_UP);
					p->subs[SUB_REAL].needanswer = 1;
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				p->cid_suppress_expire = 0;
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
					struct ast_callid *callid = NULL;
					int callid_created;
					char cid_num[256];
					char cid_name[256];

					cid_num[0] = 0;
					cid_name[0] = 0;
					if (p->dahditrcallerid && p->owner) {
						if (ast_channel_caller(p->owner)->id.number.valid
							&& ast_channel_caller(p->owner)->id.number.str) {
							ast_copy_string(cid_num, ast_channel_caller(p->owner)->id.number.str,
								sizeof(cid_num));
						}
						if (ast_channel_caller(p->owner)->id.name.valid
							&& ast_channel_caller(p->owner)->id.name.str) {
							ast_copy_string(cid_name, ast_channel_caller(p->owner)->id.name.str,
								sizeof(cid_name));
						}
					}
					/* XXX This section needs much more error checking!!! XXX */
					/* Start a 3-way call if feasible */
					if (!((ast_channel_pbx(ast)) ||
						(ast_channel_state(ast) == AST_STATE_UP) ||
						(ast_channel_state(ast) == AST_STATE_RING))) {
						ast_debug(1, "Flash when call not up or ringing\n");
						goto winkflashdone;
					}
					if (alloc_sub(p, SUB_THREEWAY)) {
						ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
						goto winkflashdone;
					}
					callid_created = ast_callid_threadstorage_auto(&callid);
					/*
					 * Make new channel
					 *
					 * We cannot hold the p or ast locks while creating a new
					 * channel.
					 */
					ast_mutex_unlock(&p->lock);
					ast_channel_unlock(ast);
					chan = dahdi_new(p, AST_STATE_RESERVED, 0, SUB_THREEWAY, 0, NULL, callid);
					ast_channel_lock(ast);
					ast_mutex_lock(&p->lock);
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
						ast_verb(3, "Started three way call on channel %d\n", p->channel);

						/* Start music on hold if appropriate */
						if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
							ast_queue_control_data(p->subs[SUB_THREEWAY].owner, AST_CONTROL_HOLD,
								S_OR(p->mohsuggest, NULL),
								!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
						}
						p->subs[SUB_THREEWAY].needhold = 1;
					}
					ast_callid_threadstorage_auto_clean(callid, callid_created);
				}
			} else {
				/* Already have a 3 way call */
				if (p->subs[SUB_THREEWAY].inthreeway) {
					/* Call is already up, drop the last person */
					ast_debug(1, "Got flash with three way call up, dropping last call on %d\n", p->channel);
					/* If the primary call isn't answered yet, use it */
					if ((ast_channel_state(p->subs[SUB_REAL].owner) != AST_STATE_UP) && (ast_channel_state(p->subs[SUB_THREEWAY].owner) == AST_STATE_UP)) {
						/* Swap back -- we're dropping the real 3-way that isn't finished yet*/
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						p->owner = p->subs[SUB_REAL].owner;
					}
					/* Drop the last call and stop the conference */
					ast_verb(3, "Dropping three-way call on %s\n", ast_channel_name(p->subs[SUB_THREEWAY].owner));
					ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
					p->subs[SUB_REAL].inthreeway = 0;
					p->subs[SUB_THREEWAY].inthreeway = 0;
				} else {
					/* Lets see what we're up to */
					if (((ast_channel_pbx(ast)) || (ast_channel_state(ast) == AST_STATE_UP)) &&
						(p->transfertobusy || (ast_channel_state(ast) != AST_STATE_BUSY))) {
						int otherindex = SUB_THREEWAY;

						ast_verb(3, "Building conference call with %s and %s\n",
							ast_channel_name(p->subs[SUB_THREEWAY].owner),
							ast_channel_name(p->subs[SUB_REAL].owner));
						/* Put them in the threeway, and flip */
						p->subs[SUB_THREEWAY].inthreeway = 1;
						p->subs[SUB_REAL].inthreeway = 1;
						if (ast_channel_state(ast) == AST_STATE_UP) {
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							otherindex = SUB_REAL;
						}
						if (p->subs[otherindex].owner && ast_bridged_channel(p->subs[otherindex].owner))
							ast_queue_control(p->subs[otherindex].owner, AST_CONTROL_UNHOLD);
						p->subs[otherindex].needunhold = 1;
						p->owner = p->subs[SUB_REAL].owner;
					} else {
						ast_verb(3, "Dumping incomplete call on on %s\n", ast_channel_name(p->subs[SUB_THREEWAY].owner));
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
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
		case SIG_FEATD:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
		case SIG_FXSLS:
		case SIG_FXSGS:
			if (p->dialing)
				ast_debug(1, "Ignoring wink on channel %d\n", p->channel);
			else
				ast_debug(1, "Got wink in weird state %u on channel %d\n", ast_channel_state(ast), p->channel);
			break;
		case SIG_FEATDMF_TA:
			switch (p->whichwink) {
			case 0:
				ast_debug(1, "ANI2 set to '%d' and ANI is '%s'\n", ast_channel_caller(p->owner)->ani2,
					S_COR(ast_channel_caller(p->owner)->ani.number.valid,
						ast_channel_caller(p->owner)->ani.number.str, ""));
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%d%s#",
					ast_channel_caller(p->owner)->ani2,
					S_COR(ast_channel_caller(p->owner)->ani.number.valid,
						ast_channel_caller(p->owner)->ani.number.str, ""));
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
		case SIG_EMWINK:
			/* FGD MF and EMWINK *Must* wait for wink */
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
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
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
				((ast_channel_state(ast) == AST_STATE_DIALING) ||
				(ast_channel_state(ast) == AST_STATE_RINGING))) {
				ast_debug(1, "Answering on polarity switch!\n");
				ast_setstate(p->owner, AST_STATE_UP);
				if (p->hanguponpolarityswitch) {
					p->polaritydelaytv = ast_tvnow();
				}
			} else
				ast_debug(1, "Ignore switch to REVERSED Polarity on channel %d, state %u\n", p->channel, ast_channel_state(ast));
		}
		/* Removed else statement from here as it was preventing hangups from ever happening*/
		/* Added AST_STATE_RING in if statement below to deal with calling party hangups that take place when ringing */
		if (p->hanguponpolarityswitch &&
			(p->polarityonanswerdelay > 0) &&
			(p->polarity == POLARITY_REV) &&
			((ast_channel_state(ast) == AST_STATE_UP) || (ast_channel_state(ast) == AST_STATE_RING)) ) {
			/* Added log_debug information below to provide a better indication of what is going on */
			ast_debug(1, "Polarity Reversal event occured - DEBUG 1: channel %d, state %u, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %" PRIi64 "\n", p->channel, ast_channel_state(ast), p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );

			if (ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay) {
				ast_debug(1, "Polarity Reversal detected and now Hanging up on channel %d\n", p->channel);
				ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
				p->polarity = POLARITY_IDLE;
			} else
				ast_debug(1, "Polarity Reversal detected but NOT hanging up (too close to answer event) on channel %d, state %u\n", p->channel, ast_channel_state(ast));

		} else {
			p->polarity = POLARITY_IDLE;
			ast_debug(1, "Ignoring Polarity switch to IDLE on channel %d, state %u\n", p->channel, ast_channel_state(ast));
		}
		/* Added more log_debug information below to provide a better indication of what is going on */
		ast_debug(1, "Polarity Reversal event occured - DEBUG 2: channel %d, state %u, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %" PRIi64 "\n", p->channel, ast_channel_state(ast), p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);

	if ((idx = dahdi_get_index(ast, p, 0)) < 0) {
		idx = SUB_REAL;
	}

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass.integer = 0;
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
				ast_verb(3, "Channel %s still has call, ringing phone\n", ast_channel_name(p->owner));
				dahdi_ring_phone(p);
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				p->cid_suppress_expire = 0;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		case DAHDI_EVENT_RINGOFFHOOK:
			dahdi_enable_ec(p);
			dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			if (p->owner && (ast_channel_state(p->owner) == AST_STATE_RINGING)) {
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
				ast_verb(3, "Channel %d flashed to other channel %s\n", p->channel, ast_channel_name(p->owner));
				if (ast_channel_state(p->owner) != AST_STATE_UP) {
					/* Answer if necessary */
					usedindex = dahdi_get_index(p->owner, p, 0);
					if (usedindex > -1) {
						p->subs[usedindex].needanswer = 1;
					}
					ast_setstate(p->owner, AST_STATE_UP);
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				p->cid_suppress_expire = 0;
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
		ast_debug(1, "Exception on %d, channel %d\n", ast_channel_fd(ast, 0), p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
		if (p->owner) {
			ast_log(LOG_WARNING, "We're %s, not %s\n", ast_channel_name(ast), ast_channel_name(p->owner));
		}
		f = &p->subs[idx].f;
		return f;
	}

	f = dahdi_handle_event(ast);
	if (!f) {
		const char *name = ast_strdupa(ast_channel_name(ast));

		/* Tell the CDR this DAHDI device hung up */
		ast_mutex_unlock(&p->lock);
		ast_channel_unlock(ast);
		ast_set_hangupsource(ast, name, 0);
		ast_channel_lock(ast);
		ast_mutex_lock(&p->lock);
	}
	return f;
}

static struct ast_frame *dahdi_exception(struct ast_channel *ast)
{
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
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
	struct dahdi_pvt *p;
	int res;
	int idx;
	void *readbuf;
	struct ast_frame *f;

	/*
	 * For analog channels, we must do deadlock avoidance because
	 * analog ports can have more than one Asterisk channel using
	 * the same private structure.
	 */
	p = ast_channel_tech_pvt(ast);
	while (ast_mutex_trylock(&p->lock)) {
		CHANNEL_DEADLOCK_AVOIDANCE(ast);

		/*
		 * Check to see if the channel is still associated with the same
		 * private structure.  While the Asterisk channel was unlocked
		 * the following events may have occured:
		 *
		 * 1) A masquerade may have associated the channel with another
		 * technology or private structure.
		 *
		 * 2) For PRI calls, call signaling could change the channel
		 * association to another B channel (private structure).
		 */
		if (ast_channel_tech_pvt(ast) != p) {
			/* The channel is no longer associated.  Quit gracefully. */
			return &ast_null_frame;
		}
	}

	idx = dahdi_get_index(ast, p, 0);

	/* Hang up if we don't really exist */
	if (idx < 0)	{
		ast_log(LOG_WARNING, "We don't exist?\n");
		ast_mutex_unlock(&p->lock);
		return NULL;
	}

	if ((p->radio || (p->oprmode < 0)) && p->inalarm) {
		ast_mutex_unlock(&p->lock);
		return NULL;
	}

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "dahdi_read";
	p->subs[idx].f.data.ptr = NULL;

	/* make sure it sends initial key state as first frame */
	if ((p->radio || (p->oprmode < 0)) && (!p->firstradio))
	{
		struct dahdi_params ps;

		memset(&ps, 0, sizeof(ps));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		p->firstradio = 1;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		if (ps.rxisoffhook)
		{
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_KEY;
		}
		else
		{
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_UNKEY;
		}
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->ringt > 0) {
		if (!(--p->ringt)) {
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
	}

#ifdef HAVE_OPENR2
	if (p->mfcr2) {
		openr2_chan_process_event(p->r2chan);
		if (OR2_DIR_FORWARD == openr2_chan_get_direction(p->r2chan)) {
			struct ast_frame fr = { AST_FRAME_CONTROL, { AST_CONTROL_PROGRESS } };
			/* if the call is already accepted and we already delivered AST_CONTROL_RINGING
			 * now enqueue a progress frame to bridge the media up */
			if (p->mfcr2_call_accepted &&
				!p->mfcr2_progress_sent && 
				ast_channel_state(ast) == AST_STATE_RINGING) {
				ast_debug(1, "Enqueuing progress frame after R2 accept in chan %d\n", p->channel);
				ast_queue_frame(p->owner, &fr);
				p->mfcr2_progress_sent = 1;
			}
		}
	}
#endif

	if (p->subs[idx].needringing) {
		/* Send ringing frame if requested */
		p->subs[idx].needringing = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_RINGING;
		ast_setstate(ast, AST_STATE_RINGING);
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needbusy) {
		/* Send busy frame if requested */
		p->subs[idx].needbusy = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_BUSY;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needcongestion) {
		/* Send congestion frame if requested */
		p->subs[idx].needcongestion = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_CONGESTION;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needanswer) {
		/* Send answer frame if requested */
		p->subs[idx].needanswer = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
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
		p->subs[idx].f.subclass.integer = AST_CONTROL_FLASH;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needhold) {
		/* Send answer frame if requested */
		p->subs[idx].needhold = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_HOLD;
		ast_mutex_unlock(&p->lock);
		ast_debug(1, "Sending hold on '%s'\n", ast_channel_name(ast));
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needunhold) {
		/* Send answer frame if requested */
		p->subs[idx].needunhold = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_UNHOLD;
		ast_mutex_unlock(&p->lock);
		ast_debug(1, "Sending unhold on '%s'\n", ast_channel_name(ast));
		return &p->subs[idx].f;
	}

	/*
	 * If we have a fake_event, fake an exception to handle it only
	 * if this channel owns the private.
	 */
	if (p->fake_event && p->owner == ast) {
		if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
			struct analog_pvt *analog_p = p->sig_pvt;

			f = analog_exception(analog_p, ast);
		} else {
			f = __dahdi_exception(ast);
		}
		ast_mutex_unlock(&p->lock);
		return f;
	}

	if (ast_channel_rawreadformat(ast)->id == AST_FORMAT_SLINEAR) {
		if (!p->subs[idx].linear) {
			p->subs[idx].linear = 1;
			res = dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, idx);
		}
	} else if ((ast_channel_rawreadformat(ast)->id == AST_FORMAT_ULAW) ||
		(ast_channel_rawreadformat(ast)->id == AST_FORMAT_ALAW)) {
		if (p->subs[idx].linear) {
			p->subs[idx].linear = 0;
			res = dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to companded mode.\n", p->channel, idx);
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_getformatname(ast_channel_rawreadformat(ast)));
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->subs[idx].buffer) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	res = read(p->subs[idx].dfd, readbuf, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
	ast_clear_flag(ast_channel_flags(ast), AST_FLAG_BLOCKING);
	/* Check for hangup */
	if (res < 0) {
		f = NULL;
		if (res == -1) {
			if (errno == EAGAIN) {
				/* Return "NULL" frame if there is nobody there */
				ast_mutex_unlock(&p->lock);
				return &p->subs[idx].f;
			} else if (errno == ELAST) {
				if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
					struct analog_pvt *analog_p = p->sig_pvt;
					f = analog_exception(analog_p, ast);
				} else {
					f = __dahdi_exception(ast);
				}
			} else
				ast_log(LOG_WARNING, "dahdi_rec: %s\n", strerror(errno));
		}
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (res != (p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE)) {
		ast_debug(1, "Short read (%d/%d), must be an event...\n", res, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
		if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
			struct analog_pvt *analog_p = p->sig_pvt;
			f = analog_exception(analog_p, ast);
		} else {
			f = __dahdi_exception(ast);
		}
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
			p->subs[idx].f.subclass.integer = 0;
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
	if (idx == SUB_REAL) {
		/* Ensure the CW timers decrement only on a single subchannel */
		if (p->cidcwexpire) {
			if (!--p->cidcwexpire) {
				/* Expired CID/CW */
				ast_verb(3, "CPE does not support Call Waiting Caller*ID.\n");
				restore_conference(p);
			}
		}
		if (p->cid_suppress_expire) {
			--p->cid_suppress_expire;
		}
		if (p->callwaitingrepeat) {
			if (!--p->callwaitingrepeat) {
				/* Expired, Repeat callwaiting tone */
				++p->callwaitrings;
				dahdi_callwait(ast);
			}
		}
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
	ast_format_copy(&p->subs[idx].f.subclass.format, ast_channel_rawreadformat(ast));
	p->subs[idx].f.samples = READ_SIZE;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
	p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET / sizeof(p->subs[idx].buffer[0]);
#if 0
	ast_debug(1, "Read %d of voice on %s\n", p->subs[idx].f.datalen, ast->name);
#endif
	if ((p->dialing && !p->waitingfordt.tv_sec) ||  p->radio || /* Transmitting something */
		(idx && (ast_channel_state(ast) != AST_STATE_UP)) || /* Three-way or callwait that isn't up */
		((idx == SUB_CALLWAIT) && !p->subs[SUB_CALLWAIT].inthreeway) /* Inactive and non-confed call-wait */
		) {
		/* Whoops, we're still dialing, or in a state where we shouldn't transmit....
		   don't send anything */
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		p->subs[idx].f.samples = 0;
		p->subs[idx].f.mallocd = 0;
		p->subs[idx].f.offset = 0;
		p->subs[idx].f.data.ptr = NULL;
		p->subs[idx].f.datalen= 0;
	}
	if (p->dsp && (!p->ignoredtmf || p->callwaitcas || p->busydetect || p->callprogress || p->waitingfordt.tv_sec || p->dialtone_detect) && !idx) {
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
			if ((p->dsp_features & DSP_FEATURE_WAITDIALTONE) && (p->dialtone_detect > 0)
				&& !p->outgoing && ast_channel_state(ast) == AST_STATE_UP) {
				if (++p->dialtone_scanning_time_elapsed >= p->dialtone_detect) {
					p->dsp_features &= ~DSP_FEATURE_WAITDIALTONE;
					ast_dsp_set_features(p->dsp, p->dsp_features);
				}
			}
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_BUSY)) {
				if ((ast_channel_state(ast) == AST_STATE_UP) && !p->outgoing) {
					/*
					 * Treat this as a "hangup" instead of a "busy" on the
					 * assumption that a busy means the incoming call went away.
					 */
					ast_frfree(f);
					f = NULL;
				}
			} else if (p->dialtone_detect && !p->outgoing && f->frametype == AST_FRAME_VOICE) {
				if ((ast_dsp_get_tstate(p->dsp) == DSP_TONE_STATE_DIALTONE) && (ast_dsp_get_tcount(p->dsp) > 9)) {
					/* Dialtone detected on inbound call; hangup the channel */
					ast_frfree(f);
					f = NULL;
				}
			} else if (f->frametype == AST_FRAME_DTMF_BEGIN
				|| f->frametype == AST_FRAME_DTMF_END) {
#ifdef HAVE_PRI
				if (dahdi_sig_pri_lib_handles(p->sig)
					&& ((struct sig_pri_chan *) p->sig_pvt)->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING
					&& p->pri
					&& ((!p->outgoing && (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING))
						|| (p->outgoing && (p->pri->overlapdial & DAHDI_OVERLAPDIAL_OUTGOING)))) {
					/* Don't accept in-band DTMF when in overlap dial mode */
					ast_debug(1, "Absorbing inband %s DTMF digit: 0x%02X '%c' on %s\n",
						f->frametype == AST_FRAME_DTMF_BEGIN ? "begin" : "end",
						(unsigned)f->subclass.integer, f->subclass.integer, ast_channel_name(ast));

					f->frametype = AST_FRAME_NULL;
					f->subclass.integer = 0;
				}
#endif
				/* DSP clears us of being pulse */
				p->pulsedial = 0;
			} else if (p->waitingfordt.tv_sec) {
				if (ast_tvdiff_ms(ast_tvnow(), p->waitingfordt) >= p->waitfordialtone ) {
					p->waitingfordt.tv_sec = 0;
					ast_log(LOG_WARNING, "Never saw dialtone on channel %d\n", p->channel);
					ast_frfree(f);
					f = NULL;
				} else if (f->frametype == AST_FRAME_VOICE) {
					f->frametype = AST_FRAME_NULL;
					f->subclass.integer = 0;
					if ((ast_dsp_get_tstate(p->dsp) == DSP_TONE_STATE_DIALTONE || ast_dsp_get_tstate(p->dsp) == DSP_TONE_STATE_RINGING) && ast_dsp_get_tcount(p->dsp) > 9) {
						p->waitingfordt.tv_sec = 0;
						p->dsp_features &= ~DSP_FEATURE_WAITDIALTONE;
						ast_dsp_set_features(p->dsp, p->dsp_features);
						ast_debug(1, "Got 10 samples of dialtone!\n");
						if (!ast_strlen_zero(p->dop.dialstr)) { /* Dial deferred digits */
							res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
							if (res < 0) {
								ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
								p->dop.dialstr[0] = '\0';
								ast_mutex_unlock(&p->lock);
								ast_frfree(f);
								return NULL;
							} else {
								ast_debug(1, "Sent deferred digit string: %s\n", p->dop.dialstr);
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

	if (f) {
		switch (f->frametype) {
		case AST_FRAME_DTMF_BEGIN:
		case AST_FRAME_DTMF_END:
			if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
				analog_handle_dtmf(p->sig_pvt, ast, idx, &f);
			} else {
				dahdi_handle_dtmf(ast, idx, &f);
			}
			break;
		case AST_FRAME_VOICE:
			if (p->cidspill || p->cid_suppress_expire) {
				/* We are/were sending a caller id spill.  Suppress any echo. */
				p->subs[idx].f.frametype = AST_FRAME_NULL;
				p->subs[idx].f.subclass.integer = 0;
				p->subs[idx].f.samples = 0;
				p->subs[idx].f.mallocd = 0;
				p->subs[idx].f.offset = 0;
				p->subs[idx].f.data.ptr = NULL;
				p->subs[idx].f.datalen= 0;
			}
			break;
		default:
			break;
		}
	}

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
	struct dahdi_pvt *p = ast_channel_tech_pvt(ast);
	int res;
	int idx;
	idx = dahdi_get_index(ast, p, 0);
	if (idx < 0) {
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast_channel_name(ast));
		return -1;
	}

	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING, "Don't know what to do with frame type '%u'\n", frame->frametype);
		return 0;
	}
	if ((frame->subclass.format.id != AST_FORMAT_SLINEAR) &&
		(frame->subclass.format.id != AST_FORMAT_ULAW) &&
		(frame->subclass.format.id != AST_FORMAT_ALAW)) {
		ast_log(LOG_WARNING, "Cannot handle frames in %s format\n", ast_getformatname(&frame->subclass.format));
		return -1;
	}
	if (p->dialing) {
		ast_debug(5, "Dropping frame since I'm still dialing on %s...\n",
			ast_channel_name(ast));
		return 0;
	}
	if (!p->owner) {
		ast_debug(5, "Dropping frame since there is no active owner on %s...\n",
			ast_channel_name(ast));
		return 0;
	}
	if (p->cidspill) {
		ast_debug(5, "Dropping frame since I've still got a callerid spill on %s...\n",
			ast_channel_name(ast));
		return 0;
	}
	/* Return if it's not valid data */
	if (!frame->data.ptr || !frame->datalen)
		return 0;

	if (frame->subclass.format.id == AST_FORMAT_SLINEAR) {
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
	struct dahdi_pvt *p = ast_channel_tech_pvt(chan);
	int res=-1;
	int idx;
	int func = DAHDI_FLASH;

	ast_mutex_lock(&p->lock);
	ast_debug(1, "Requested indication %d on channel %s\n", condition, ast_channel_name(chan));
	switch (p->sig) {
#if defined(HAVE_PRI)
	case SIG_PRI_LIB_HANDLE_CASES:
		res = sig_pri_indicate(p->sig_pvt, chan, condition, data, datalen);
		ast_mutex_unlock(&p->lock);
		return res;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	case SIG_SS7:
		res = sig_ss7_indicate(p->sig_pvt, chan, condition, data, datalen);
		ast_mutex_unlock(&p->lock);
		return res;
#endif	/* defined(HAVE_SS7) */
	default:
		break;
	}
#ifdef HAVE_OPENR2
	if (p->mfcr2 && !p->mfcr2_call_accepted) {
		ast_mutex_unlock(&p->lock);
		/* if this is an R2 call and the call is not yet accepted, we don't want the
		   tone indications to mess up with the MF tones */
		return 0;
	}
#endif
	idx = dahdi_get_index(chan, p, 0);
	if (idx == SUB_REAL) {
		switch (condition) {
		case AST_CONTROL_BUSY:
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_BUSY);
			break;
		case AST_CONTROL_RINGING:
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_RINGTONE);

			if (ast_channel_state(chan) != AST_STATE_UP) {
				if ((ast_channel_state(chan) != AST_STATE_RING) ||
					((p->sig != SIG_FXSKS) &&
				 (p->sig != SIG_FXSLS) &&
				 (p->sig != SIG_FXSGS)))
				ast_setstate(chan, AST_STATE_RINGING);
			}
			break;
		case AST_CONTROL_INCOMPLETE:
			ast_debug(1, "Received AST_CONTROL_INCOMPLETE on %s\n", ast_channel_name(chan));
			/* act as a progress or proceeding, allowing the caller to enter additional numbers */
			res = 0;
			break;
		case AST_CONTROL_PROCEEDING:
			ast_debug(1, "Received AST_CONTROL_PROCEEDING on %s\n", ast_channel_name(chan));
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_PROGRESS:
			ast_debug(1, "Received AST_CONTROL_PROGRESS on %s\n", ast_channel_name(chan));
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_CONGESTION:
			/* There are many cause codes that generate an AST_CONTROL_CONGESTION. */
			switch (ast_channel_hangupcause(chan)) {
			case AST_CAUSE_USER_BUSY:
			case AST_CAUSE_NORMAL_CLEARING:
			case 0:/* Cause has not been set. */
				/* Supply a more appropriate cause. */
				ast_channel_hangupcause_set(chan, AST_CAUSE_CONGESTION);
				break;
			default:
				break;
			}
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
						ast_channel_name(chan), strerror(errno));
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
	} else {
		res = 0;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

#if defined(HAVE_PRI)
static struct ast_str *create_channel_name(struct dahdi_pvt *i, int is_outgoing, char *address)
#else
static struct ast_str *create_channel_name(struct dahdi_pvt *i)
#endif	/* defined(HAVE_PRI) */
{
	struct ast_str *chan_name;
	int x, y;

	/* Create the new channel name tail. */
	if (!(chan_name = ast_str_create(32))) {
		return NULL;
	}
	if (i->channel == CHAN_PSEUDO) {
		ast_str_set(&chan_name, 0, "pseudo-%ld", ast_random());
#if defined(HAVE_PRI)
	} else if (i->pri) {
		ast_mutex_lock(&i->pri->lock);
		y = ++i->pri->new_chan_seq;
		if (is_outgoing) {
			ast_str_set(&chan_name, 0, "i%d/%s-%x", i->pri->span, address, (unsigned)y);
			address[0] = '\0';
		} else if (ast_strlen_zero(i->cid_subaddr)) {
			/* Put in caller-id number only since there is no subaddress. */
			ast_str_set(&chan_name, 0, "i%d/%s-%x", i->pri->span, i->cid_num, (unsigned)y);
		} else {
			/* Put in caller-id number and subaddress. */
			ast_str_set(&chan_name, 0, "i%d/%s:%s-%x", i->pri->span, i->cid_num,
				i->cid_subaddr, (unsigned)y);
		}
		ast_mutex_unlock(&i->pri->lock);
#endif	/* defined(HAVE_PRI) */
	} else {
		y = 1;
		do {
			ast_str_set(&chan_name, 0, "%d-%d", i->channel, y);
			for (x = 0; x < 3; ++x) {
				if (i->subs[x].owner && !strcasecmp(ast_str_buffer(chan_name),
					ast_channel_name(i->subs[x].owner) + 6)) {
					break;
				}
			}
			++y;
		} while (x < 3);
	}
	return chan_name;
}

static struct ast_channel *dahdi_new_callid_clean(struct dahdi_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid, struct ast_callid *callid, int callid_created)
{
	struct ast_channel *new_channel = dahdi_new(i, state, startpbx, idx, law, linkedid, callid);

	ast_callid_threadstorage_auto_clean(callid, callid_created);

	return new_channel;
}

static struct ast_channel *dahdi_new(struct dahdi_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid, struct ast_callid *callid)
{
	struct ast_channel *tmp;
	struct ast_format deflaw;
	int x;
	int features;
	struct ast_str *chan_name;
	struct ast_variable *v;
	char *dashptr;
	char device_name[AST_CHANNEL_NAME];

	if (i->subs[idx].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[idx]);
		return NULL;
	}

	ast_format_clear(&deflaw);
#if defined(HAVE_PRI)
	/*
	 * The dnid has been stuffed with the called-number[:subaddress]
	 * by dahdi_request() for outgoing calls.
	 */
	chan_name = create_channel_name(i, i->outgoing, i->dnid);
#else
	chan_name = create_channel_name(i);
#endif	/* defined(HAVE_PRI) */
	if (!chan_name) {
		return NULL;
	}

	tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, linkedid, i->amaflags, "DAHDI/%s", ast_str_buffer(chan_name));
	ast_free(chan_name);
	if (!tmp) {
		return NULL;
	}

	if (callid) {
		ast_channel_callid_set(tmp, callid);
	}

	ast_channel_tech_set(tmp, &dahdi_tech);
#if defined(HAVE_PRI)
	if (i->pri) {
		ast_cc_copy_config_params(i->cc_params, i->pri->cc_params);
	}
#endif	/* defined(HAVE_PRI) */
	ast_channel_cc_params_init(tmp, i->cc_params);
	if (law) {
		i->law = law;
		if (law == DAHDI_LAW_ALAW) {
			ast_format_set(&deflaw, AST_FORMAT_ALAW, 0);
		} else {
			ast_format_set(&deflaw, AST_FORMAT_ULAW, 0);
		}
	} else {
		switch (i->sig) {
		case SIG_PRI_LIB_HANDLE_CASES:
			/* Make sure companding law is known. */
			i->law = (i->law_default == DAHDI_LAW_ALAW)
				? DAHDI_LAW_ALAW : DAHDI_LAW_MULAW;
			break;
		default:
			i->law = i->law_default;
			break;
		}
		if (i->law_default == DAHDI_LAW_ALAW) {
			ast_format_set(&deflaw, AST_FORMAT_ALAW, 0);
		} else {
			ast_format_set(&deflaw, AST_FORMAT_ULAW, 0);
		}
	}
	ast_channel_set_fd(tmp, 0, i->subs[idx].dfd);
	ast_format_cap_add(ast_channel_nativeformats(tmp), &deflaw);
	/* Start out assuming ulaw since it's smaller :) */
	ast_format_copy(ast_channel_rawreadformat(tmp), &deflaw);
	ast_format_copy(ast_channel_readformat(tmp), &deflaw);
	ast_format_copy(ast_channel_rawwriteformat(tmp), &deflaw);
	ast_format_copy(ast_channel_writeformat(tmp), &deflaw);
	i->subs[idx].linear = 0;
	dahdi_setlinear(i->subs[idx].dfd, i->subs[idx].linear);
	features = 0;
	if (idx == SUB_REAL) {
		if (i->busydetect && CANBUSYDETECT(i))
			features |= DSP_FEATURE_BUSY_DETECT;
		if ((i->callprogress & CALLPROGRESS_PROGRESS) && CANPROGRESSDETECT(i))
			features |= DSP_FEATURE_CALL_PROGRESS;
		if ((i->waitfordialtone || i->dialtone_detect) && CANPROGRESSDETECT(i))
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
			ast_debug(1, "Already have a dsp on %s?\n", ast_channel_name(tmp));
		} else {
			if (i->channel != CHAN_PSEUDO)
				i->dsp = ast_dsp_new();
			else
				i->dsp = NULL;
			if (i->dsp) {
				i->dsp_features = features;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
				/* We cannot do progress detection until receive PROGRESS message */
				if (i->outgoing && (dahdi_sig_pri_lib_handles(i->sig) || (i->sig == SIG_SS7))) {
					/* Remember requested DSP features, don't treat
					   talking as ANSWER */
					i->dsp_features = features & ~DSP_PROGRESS_TALK;
					features = 0;
				}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
				ast_dsp_set_features(i->dsp, features);
				ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
				if (!ast_strlen_zero(progzone))
					ast_dsp_set_call_progress_zone(i->dsp, progzone);
				if (i->busydetect && CANBUSYDETECT(i)) {
					ast_dsp_set_busy_count(i->dsp, i->busycount);
					ast_dsp_set_busy_pattern(i->dsp, &i->busy_cadence);
				}
			}
		}
	}

	i->dialtone_scanning_time_elapsed = 0;

	if (state == AST_STATE_RING)
		ast_channel_rings_set(tmp, 1);
	ast_channel_tech_pvt_set(tmp, i);
	if ((i->sig == SIG_FXOKS) || (i->sig == SIG_FXOGS) || (i->sig == SIG_FXOLS)) {
		/* Only FXO signalled stuff can be picked up */
		ast_channel_callgroup_set(tmp, i->callgroup);
		ast_channel_pickupgroup_set(tmp, i->pickupgroup);
		ast_channel_named_callgroups_set(tmp, i->named_callgroups);
		ast_channel_named_pickupgroups_set(tmp, i->named_pickupgroups);
	}
	if (!ast_strlen_zero(i->parkinglot))
		ast_channel_parkinglot_set(tmp, i->parkinglot);
	if (!ast_strlen_zero(i->language))
		ast_channel_language_set(tmp, i->language);
	if (!i->owner)
		i->owner = tmp;
	if (!ast_strlen_zero(i->accountcode))
		ast_channel_accountcode_set(tmp, i->accountcode);
	if (i->amaflags)
		ast_channel_amaflags_set(tmp, i->amaflags);
	i->subs[idx].owner = tmp;
	ast_channel_context_set(tmp, i->context);
	if (!analog_lib_handles(i->sig, i->radio, i->oprmode)) {
		ast_channel_call_forward_set(tmp, i->call_forward);
	}
	/* If we've been told "no ADSI" then enforce it */
	if (!i->adsi)
		ast_channel_adsicpe_set(tmp, AST_ADSI_UNAVAILABLE);
	if (!ast_strlen_zero(i->exten))
		ast_channel_exten_set(tmp, i->exten);
	if (!ast_strlen_zero(i->rdnis)) {
		ast_channel_redirecting(tmp)->from.number.valid = 1;
		ast_channel_redirecting(tmp)->from.number.str = ast_strdup(i->rdnis);
	}
	if (!ast_strlen_zero(i->dnid)) {
		ast_channel_dialed(tmp)->number.str = ast_strdup(i->dnid);
	}

	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	if (!ast_strlen_zero(i->cid_ani)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_ani);
	} else if (!ast_strlen_zero(i->cid_num)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}
#else
	if (!ast_strlen_zero(i->cid_num)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
	ast_channel_caller(tmp)->id.name.presentation = i->callingpres;
	ast_channel_caller(tmp)->id.number.presentation = i->callingpres;
	ast_channel_caller(tmp)->id.number.plan = i->cid_ton;
	ast_channel_caller(tmp)->ani2 = i->cid_ani2;
	ast_channel_caller(tmp)->id.tag = ast_strdup(i->cid_tag);
	/* clear the fake event in case we posted one before we had ast_channel */
	i->fake_event = 0;
	/* Assure there is no confmute on this channel */
	dahdi_confmute(i, 0);
	i->muting = 0;
	/* Configure the new channel jb */
	ast_jb_configure(tmp, &global_jbconf);

	/* Set initial device state */
	ast_copy_string(device_name, ast_channel_name(tmp), sizeof(device_name));
	dashptr = strrchr(device_name, '-');
	if (dashptr) {
		*dashptr = '\0';
	}
	ast_set_flag(ast_channel_flags(tmp), AST_FLAG_DISABLE_DEVSTATE_CACHE);
	ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, AST_DEVSTATE_NOT_CACHABLE, device_name);

	for (v = i->vars ; v ; v = v->next)
		pbx_builtin_setvar_helper(tmp, v->name, v->value);

	ast_module_ref(ast_module_info->self);

	dahdi_ami_channel_event(i, tmp);
	if (startpbx) {
#ifdef HAVE_OPENR2
		if (i->mfcr2call) {
			pbx_builtin_setvar_helper(tmp, "MFCR2_CATEGORY", openr2_proto_get_category_string(i->mfcr2_recvd_category));
		}
#endif
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
			ast_hangup(tmp);
			return NULL;
		}
	}
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
 * \param flag on 1 to enable, 0 to disable, -1 return dnd value
 *
 * chan_dahdi has a DND (Do Not Disturb) mode for each dahdichan (physical
 * DAHDI channel). Use this to enable or disable it.
 *
 * \bug the use of the word "channel" for those dahdichans is really confusing.
 */
static int dahdi_dnd(struct dahdi_pvt *dahdichan, int flag)
{
	if (analog_lib_handles(dahdichan->sig, dahdichan->radio, dahdichan->oprmode)) {
		return analog_dnd(dahdichan->sig_pvt, flag);
	}

	if (flag == -1) {
		return dahdichan->dnd;
	}

	/* Do not disturb */
	dahdichan->dnd = flag;
	ast_verb(3, "%s DND on channel %d\n",
			flag? "Enabled" : "Disabled",
			dahdichan->channel);
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when the Do Not Disturb state is changed on a DAHDI channel.</synopsis>
			<syntax>
				<parameter name="Status">
					<enumlist>
						<enum name="enabled"/>
						<enum name="disabled"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_SYSTEM, "DNDState",
			"Channel: DAHDI/%d\r\n"
			"Status: %s\r\n", dahdichan->channel,
			flag? "enabled" : "disabled");

	return 0;
}

static int canmatch_featurecode(const char *exten)
{
	int extlen = strlen(exten);
	const char *pickup_ext;
	if (!extlen) {
		return 1;
	}
	pickup_ext = ast_pickup_ext();
	if (extlen < strlen(pickup_ext) && !strncmp(pickup_ext, exten, extlen)) {
		return 1;
	}
	/* hardcoded features are *60, *67, *69, *70, *72, *73, *78, *79, *82, *0 */
	if (exten[0] == '*' && extlen < 3) {
		if (extlen == 1) {
			return 1;
		}
		/* "*0" should be processed before it gets here */
		switch (exten[1]) {
		case '6':
		case '7':
		case '8':
			return 1;
		}
	}
	return 0;
}

static void *analog_ss_thread(void *data)
{
	struct ast_channel *chan = data;
	struct dahdi_pvt *p = ast_channel_tech_pvt(chan);
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
	int flags = 0;
	int i;
	int timeout;
	int getforward = 0;
	char *s1, *s2;
	int len = 0;
	int res;
	int idx;
	struct ast_format tmpfmt;

	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count++;
	ast_mutex_unlock(&ss_thread_lock);
	/* in the bizarre case where the channel has become a zombie before we
	   even get started here, abort safely
	*/
	if (!p) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", ast_channel_name(chan));
		ast_hangup(chan);
		goto quit;
	}
	ast_verb(3, "Starting simple switch on '%s'\n", ast_channel_name(chan));
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
				while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, ast_channel_context(chan), dtmfbuf, 1, p->cid_num)) {
					if (ast_exists_extension(chan, ast_channel_context(chan), dtmfbuf, 1, p->cid_num)) {
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
			if (ast_safe_sleep(chan, 100)) {
				ast_hangup(chan);
				goto quit;
			}
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

		if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_channel_exten_set(chan, exten);
			if (p->dsp) ast_dsp_digitreset(p->dsp);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
			}
			goto quit;
		} else {
			ast_verb(2, "Unknown extension '%s' in context '%s' requested\n", exten, ast_channel_context(chan));
			sleep(2);
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_INFO);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = ast_streamfile(chan, "ss-noservice", ast_channel_language(chan));
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
			if (!ast_ignore_pattern(ast_channel_context(chan), exten))
				tone_zone_play_tone(p->subs[idx].dfd, -1);
			else
				tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALTONE);
			if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num) && !ast_parking_ext_valid(exten, chan, ast_channel_context(chan))) {
				if (!res || !ast_matchmore_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
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
						ast_channel_exten_set(chan, exten);
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
				ast_verb(3, "Disabling call waiting on %s\n", ast_channel_name(chan));
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						ast_channel_name(chan), strerror(errno));
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
				ast_verb(3, "Disabling Caller*ID on %s\n", ast_channel_name(chan));
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				ast_party_number_free(&ast_channel_caller(chan)->id.number);
				ast_party_number_init(&ast_channel_caller(chan)->id.number);
				ast_party_name_free(&ast_channel_caller(chan)->id.name);
				ast_party_name_init(&ast_channel_caller(chan)->id.name);
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						ast_channel_name(chan), strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (p->callreturn && !strcmp(exten, "*69")) {
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
			} else if ((p->transfer || p->canpark) && ast_parking_ext_valid(exten, chan, ast_channel_context(chan)) &&
						p->subs[SUB_THREEWAY].owner &&
						ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
				/* This is a three way call, the main call being a real channel,
					and we're parking the first call. */
				ast_masq_park_call_exten(ast_bridged_channel(p->subs[SUB_THREEWAY].owner),
					chan, exten, ast_channel_context(chan), 0, NULL);
				ast_verb(3, "Parking call to '%s'\n", ast_channel_name(chan));
				break;
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				ast_verb(3, "Enabling Caller*ID on %s\n", ast_channel_name(chan));
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						ast_channel_name(chan), strerror(errno));
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
					pbridge = ast_channel_tech_pvt(ast_bridged_channel(nbridge));
				if (nbridge && pbridge &&
					(ast_channel_tech(nbridge) == &dahdi_tech) &&
					(ast_channel_tech(ast_bridged_channel(nbridge)) == &dahdi_tech) &&
					ISTRUNK(pbridge)) {
					int func = DAHDI_FLASH;
					/* Clear out the dial buffer */
					p->dop.dialstr[0] = '\0';
					/* flash hookswitch */
					if ((ioctl(pbridge->subs[SUB_REAL].dfd,DAHDI_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
						ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n",
							ast_channel_name(nbridge), strerror(errno));
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
			} else if (!ast_canmatch_extension(chan, ast_channel_context(chan), exten, 1,
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))
				&& !canmatch_featurecode(exten)) {
				ast_debug(1, "Can't match %s from '%s' in context %s\n", exten,
					S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<Unknown Caller>"),
					ast_channel_context(chan));
				break;
			}
			if (!timeout)
				timeout = gendigittimeout;
			if (len && !ast_ignore_pattern(ast_channel_context(chan), exten))
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
				ast_channel_exten_set(chan, smdi_msg->fwd_st);

				if (smdi_msg->type == 'B')
					pbx_builtin_setvar_helper(chan, "_SMDI_VM_TYPE", "b");
				else if (smdi_msg->type == 'N')
					pbx_builtin_setvar_helper(chan, "_SMDI_VM_TYPE", "u");

				ast_debug(1, "Received SMDI message on %s\n", ast_channel_name(chan));
			} else {
				ast_log(LOG_WARNING, "SMDI enabled but no SMDI message present\n");
			}
		}

		if (p->use_callerid && (p->cid_signalling == CID_SIG_SMDI && smdi_msg)) {
			number = smdi_msg->calling_st;

		/* If we want caller id, we're in a prering state due to a polarity reversal
		 * and we're set to use a polarity reversal to trigger the start of caller id,
		 * grab the caller id and wait for ringing to start... */
		} else if (p->use_callerid && (ast_channel_state(chan) == AST_STATE_PRERING &&
						 (p->cid_start == CID_START_POLARITY || p->cid_start == CID_START_POLARITY_IN || p->cid_start == CID_START_DTMF_NOALERT))) {
			/* If set to use DTMF CID signalling, listen for DTMF */
			if (p->cid_signalling == CID_SIG_DTMF) {
				int k = 0;
				int off_ms;
				struct timeval start = ast_tvnow();
				int ms;
				cs = NULL;
				ast_debug(1, "Receiving DTMF cid on channel %s\n", ast_channel_name(chan));
				dahdi_setlinear(p->subs[idx].dfd, 0);
				/*
				 * We are the only party interested in the Rx stream since
				 * we have not answered yet.  We don't need or even want DTMF
				 * emulation.  The DTMF digits can come so fast that emulation
				 * can drop some of them.
				 */
				ast_set_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
				off_ms = 4000;/* This is a typical OFF time between rings. */
				for (;;) {
					struct ast_frame *f;

					ms = ast_remaining_ms(start, off_ms);
					res = ast_waitfor(chan, ms);
					if (res <= 0) {
						/*
						 * We do not need to restore the dahdi_setlinear()
						 * or AST_FLAG_END_DTMF_ONLY flag settings since we
						 * are hanging up the channel.
						 */
						ast_log(LOG_WARNING, "DTMFCID timed out waiting for ring. "
							"Exiting simple switch\n");
						ast_hangup(chan);
						goto quit;
					}
					f = ast_read(chan);
					if (!f)
						break;
					if (f->frametype == AST_FRAME_DTMF) {
						if (k < ARRAY_LEN(dtmfbuf) - 1) {
							dtmfbuf[k++] = f->subclass.integer;
						}
						ast_debug(1, "CID got digit '%c'\n", f->subclass.integer);
						start = ast_tvnow();
					}
					ast_frfree(f);
					if (ast_channel_state(chan) == AST_STATE_RING ||
						ast_channel_state(chan) == AST_STATE_RINGING)
						break; /* Got ring */
				}
				ast_clear_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
				dtmfbuf[k] = '\0';
				dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
				/* Got cid and ring. */
				ast_debug(1, "CID got string '%s'\n", dtmfbuf);
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_debug(1, "CID is '%s', flags %d\n", dtmfcid, flags);
				/* If first byte is NULL, we have no cid */
				if (!ast_strlen_zero(dtmfcid))
					number = dtmfcid;
				else
					number = NULL;
			/* If set to use V23 Signalling, launch our FSK gubbins and listen for it */
			} else if ((p->cid_signalling == CID_SIG_V23) || (p->cid_signalling == CID_SIG_V23_JP)) {
				cs = callerid_new(p->cid_signalling);
				if (cs) {
					int off_ms;
					struct timeval start;
					int ms;
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
							if (res == DAHDI_EVENT_NOALARM) {
								p->inalarm = 0;
							}

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
								res = callerid_feed_jp(cs, buf, res, ast_format_set(&tmpfmt, AST_LAW(p), 0));
							} else {
								res = callerid_feed(cs, buf, res, ast_format_set(&tmpfmt, AST_LAW(p), 0));
							}
							if (res < 0) {
								/*
								 * The previous diagnostic message output likely
								 * explains why it failed.
								 */
								ast_log(LOG_WARNING,
									"Failed to decode CallerID on channel '%s'\n",
									ast_channel_name(chan));
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
					}

					/* Finished with Caller*ID, now wait for a ring to make sure there really is a call coming */
					start = ast_tvnow();
					off_ms = 4000;/* This is a typical OFF time between rings. */
					for (;;) {
						struct ast_frame *f;

						ms = ast_remaining_ms(start, off_ms);
						res = ast_waitfor(chan, ms);
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
						if (ast_channel_state(chan) == AST_STATE_RING ||
							ast_channel_state(chan) == AST_STATE_RINGING)
							break; /* Got ring */
					}

					/* We must have a ring by now, so, if configured, lets try to listen for
					 * distinctive ringing */
					if (p->usedistinctiveringdetection) {
						len = 0;
						distMatches = 0;
						/* Clear the current ring data array so we don't have old data in it. */
						for (receivedRingT = 0; receivedRingT < ARRAY_LEN(curRingData); receivedRingT++)
							curRingData[receivedRingT] = 0;
						receivedRingT = 0;
						counter = 0;
						counter1 = 0;
						/* Check to see if context is what it should be, if not set to be. */
						if (strcmp(p->context,p->defcontext) != 0) {
							ast_copy_string(p->context, p->defcontext, sizeof(p->context));
							ast_channel_context_set(chan, p->defcontext);
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
								if (res == DAHDI_EVENT_NOALARM) {
									p->inalarm = 0;
								}
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
								if (p->ringt > 0) {
									if (!(--p->ringt)) {
										res = -1;
										break;
									}
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
								ast_copy_string(p->context, S_OR(p->drings.ringContext[counter].contextData, p->defcontext), sizeof(p->context));
								ast_channel_context_set(chan, S_OR(p->drings.ringContext[counter].contextData, p->defcontext));
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
					ast_channel_name(chan));
				ast_hangup(chan);
				goto quit;
			}
		} else if (p->use_callerid && p->cid_start == CID_START_RING) {
			if (p->cid_signalling == CID_SIG_DTMF) {
				int k = 0;
				int off_ms;
				struct timeval start;
				int ms;
				cs = NULL;
				dahdi_setlinear(p->subs[idx].dfd, 0);
				off_ms = 2000;
				start = ast_tvnow();
				for (;;) {
					struct ast_frame *f;

					ms = ast_remaining_ms(start, off_ms);
					res = ast_waitfor(chan, ms);
					if (res <= 0) {
						ast_log(LOG_WARNING, "DTMFCID timed out waiting for ring. "
							"Exiting simple switch\n");
						ast_hangup(chan);
						goto quit;
					}
					f = ast_read(chan);
					if (!f) {
						/* Hangup received waiting for DTMFCID. Exiting simple switch. */
						ast_hangup(chan);
						goto quit;
					}
					if (f->frametype == AST_FRAME_DTMF) {
						dtmfbuf[k++] = f->subclass.integer;
						ast_debug(1, "CID got digit '%c'\n", f->subclass.integer);
						start = ast_tvnow();
					}
					ast_frfree(f);

					if (p->ringt_base == p->ringt)
						break;
				}
				dtmfbuf[k] = '\0';
				dahdi_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
				/* Got cid and ring. */
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_debug(1, "CID is '%s', flags %d\n",
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
					/* Clear the current ring data array so we don't have old data in it. */
					for (receivedRingT = 0; receivedRingT < ARRAY_LEN(curRingData); receivedRingT++)
						curRingData[receivedRingT] = 0;
					receivedRingT = 0;
					counter = 0;
					counter1 = 0;
					/* Check to see if context is what it should be, if not set to be. */
					if (strcmp(p->context,p->defcontext) != 0) {
						ast_copy_string(p->context, p->defcontext, sizeof(p->context));
						ast_channel_context_set(chan, p->defcontext);
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
							if (res == DAHDI_EVENT_NOALARM) {
								p->inalarm = 0;
							}
							/* If we get a PR event, they hung up while processing calerid */
							if ( res == DAHDI_EVENT_POLARITY && p->hanguponpolarityswitch && p->polarity == POLARITY_REV) {
								ast_debug(1, "Hanging up due to polarity reversal on channel %d while detecting callerid\n", p->channel);
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
							if (p->ringt > 0) {
								if (!(--p->ringt)) {
									res = -1;
									break;
								}
							}
							samples += res;
							res = callerid_feed(cs, buf, res, ast_format_set(&tmpfmt, AST_LAW(p), 0));
							if (res < 0) {
								/*
								 * The previous diagnostic message output likely
								 * explains why it failed.
								 */
								ast_log(LOG_WARNING,
									"Failed to decode CallerID on channel '%s'\n",
									ast_channel_name(chan));
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
						/* Clear the current ring data array so we don't have old data in it. */
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
								if (res == DAHDI_EVENT_NOALARM) {
									p->inalarm = 0;
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
								if (p->ringt > 0) {
									if (!(--p->ringt)) {
										res = -1;
										break;
									}
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
								ast_copy_string(p->context, S_OR(p->drings.ringContext[counter].contextData, p->defcontext), sizeof(p->context));
								ast_channel_context_set(chan, S_OR(p->drings.ringContext[counter].contextData, p->defcontext));
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
						ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", ast_channel_name(chan));
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
		ast_channel_rings_set(chan, 1);
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

static int calc_energy(const unsigned char *buf, int len, enum ast_format_id law)
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
	struct ast_format tmpfmt;

	if (!(cs = callerid_new(mtd->pvt->cid_signalling))) {
		goto quit_no_clean;
	}

	callerid_feed(cs, mtd->buf, mtd->len, ast_format_set(&tmpfmt, AST_LAW(mtd->pvt), 0));

	bump_gains(mtd->pvt);

	for (;;) {
		i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
		if ((res = ioctl(mtd->pvt->subs[SUB_REAL].dfd, DAHDI_IOMUX, &i))) {
			ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
			goto quit;
		}

		if (i & DAHDI_IOMUX_SIGEVENT) {
			struct ast_channel *chan;
			struct ast_callid *callid = NULL;
			int callid_created;

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
				if (analog_lib_handles(mtd->pvt->sig, mtd->pvt->radio, mtd->pvt->oprmode)) {
					struct analog_pvt *analog_p = mtd->pvt->sig_pvt;

					analog_p->inalarm = 0;
				}
				mtd->pvt->inalarm = 0;
				handle_clear_alarms(mtd->pvt);
				break;
			case DAHDI_EVENT_ALARM:
				if (analog_lib_handles(mtd->pvt->sig, mtd->pvt->radio, mtd->pvt->oprmode)) {
					struct analog_pvt *analog_p = mtd->pvt->sig_pvt;

					analog_p->inalarm = 1;
				}
				mtd->pvt->inalarm = 1;
				res = get_alarms(mtd->pvt);
				handle_alarms(mtd->pvt, res);
				break; /* What to do on channel alarm ???? -- fall thru intentionally?? */
			default:
				callid_created = ast_callid_threadstorage_auto(&callid);
				ast_log(LOG_NOTICE, "Got event %d (%s)...  Passing along to analog_ss_thread\n", res, event2str(res));
				callerid_free(cs);

				restore_gains(mtd->pvt);
				mtd->pvt->ringt = mtd->pvt->ringt_base;

				if ((chan = dahdi_new(mtd->pvt, AST_STATE_RING, 0, SUB_REAL, 0, NULL, callid))) {
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
					}
				} else {
					ast_log(LOG_WARNING, "Could not create channel to handle call\n");
				}

				ast_callid_threadstorage_auto_clean(callid, callid_created);
				goto quit_no_clean;
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
				if ((spill_result = callerid_feed(cs, mtd->buf, res, ast_format_set(&tmpfmt, AST_LAW(mtd->pvt), 0))) < 0) {
					/*
					 * The previous diagnostic message output likely
					 * explains why it failed.
					 */
					ast_log(LOG_WARNING, "Failed to decode CallerID\n");
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
* that are sent out via FXS port on voicemail state change.  The execution of
* the mwi send is state driven and can either generate a ring pulse prior to
* sending the fsk spill or simply send an fsk spill.
*/
static int mwi_send_init(struct dahdi_pvt * pvt)
{
	int x;
	struct ast_format tmpfmt;

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
	ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
	x = 3000;
	ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_ONHOOKTRANSFER, &x);
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
	if (pvt->mwisend_fsk) {
#endif
		pvt->cidlen = ast_callerid_vmwi_generate(pvt->cidspill, has_voicemail(pvt), CID_MWI_TYPE_MDMF_FULL,
							 ast_format_set(&tmpfmt, AST_LAW(pvt), 0), pvt->cid_name, pvt->cid_num, 0);
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
					ast_log(LOG_WARNING, "Unable to finish RP-AS: %s mwi send aborted\n", strerror(errno));
					ast_free(pvt->cidspill);
					pvt->cidspill = NULL;
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
	struct dahdi_pvt *cur;

	ast_mutex_lock(&iflock);
	for (cur = iflist; cur; cur = cur->next) {
		if (cur->channel == channel) {
			int x = DAHDI_FLASH;

			/* important to create an event for dahdi_wait_event to register so that all analog_ss_threads terminate */
			ioctl(cur->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);

			destroy_channel(cur, 1);
			ast_mutex_unlock(&iflock);
			ast_module_unref(ast_module_info->self);
			return RESULT_SUCCESS;
		}
	}
	ast_mutex_unlock(&iflock);
	return RESULT_FAILURE;
}

static struct dahdi_pvt *handle_init_event(struct dahdi_pvt *i, int event)
{
	int res;
	pthread_t threadid;
	struct ast_channel *chan;
	struct ast_callid *callid = NULL;
	int callid_created;

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
			if (res && (errno == EBUSY)) {
				break;
			}

			callid_created = ast_callid_threadstorage_auto(&callid);

			/* Cancel VMWI spill */
			ast_free(i->cidspill);
			i->cidspill = NULL;
			restore_conference(i);

			if (i->immediate) {
				dahdi_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
				chan = dahdi_new(i, AST_STATE_RING, 1, SUB_REAL, 0, NULL, callid);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = dahdi_new(i, AST_STATE_RESERVED, 0, SUB_REAL, 0, NULL, callid);
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
					}
				} else
					ast_log(LOG_WARNING, "Unable to create channel\n");
			}

			ast_callid_threadstorage_auto_clean(callid, callid_created);
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
			callid_created = ast_callid_threadstorage_auto(&callid);
			if (i->cid_start == CID_START_POLARITY_IN) {
				chan = dahdi_new(i, AST_STATE_PRERING, 0, SUB_REAL, 0, NULL, callid);
			} else {
				chan = dahdi_new(i, AST_STATE_RING, 0, SUB_REAL, 0, NULL, callid);
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
			}

			ast_callid_threadstorage_auto_clean(callid, callid_created);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/answer with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			return NULL;
		}
		break;
	case DAHDI_EVENT_NOALARM:
		switch (i->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			ast_mutex_lock(&i->lock);
			sig_pri_chan_alarm_notify(i->sig_pvt, 1);
			ast_mutex_unlock(&i->lock);
			break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
			sig_ss7_set_alarm(i->sig_pvt, 0);
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			i->inalarm = 0;
			break;
		}
		handle_clear_alarms(i);
		break;
	case DAHDI_EVENT_ALARM:
		switch (i->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			ast_mutex_lock(&i->lock);
			sig_pri_chan_alarm_notify(i->sig_pvt, 0);
			ast_mutex_unlock(&i->lock);
			break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
			sig_ss7_set_alarm(i->sig_pvt, 1);
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			i->inalarm = 1;
			break;
		}
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
		case SIG_SS7:
		case SIG_PRI_LIB_HANDLE_CASES:
			dahdi_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle on hook with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			return NULL;
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
			callid_created = ast_callid_threadstorage_auto(&callid);
			if (i->hanguponpolarityswitch)
				i->polarity = POLARITY_REV;
			if (i->cid_start == CID_START_POLARITY || i->cid_start == CID_START_POLARITY_IN) {
				i->polarity = POLARITY_REV;
				ast_verb(2, "Starting post polarity "
					"CID detection on channel %d\n",
					i->channel);
				chan = dahdi_new(i, AST_STATE_PRERING, 0, SUB_REAL, 0, NULL, callid);
				if (!chan) {
					ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				} else if (ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					ast_hangup(chan);
				}
			}
			ast_callid_threadstorage_auto_clean(callid, callid_created);
			break;
		default:
			ast_log(LOG_WARNING, "handle_init_event detected "
				"polarity reversal on non-FXO (SIG_FXS) "
				"interface %d\n", i->channel);
		}
		break;
	case DAHDI_EVENT_REMOVED: /* destroy channel, will actually do so in do_monitor */
		ast_log(LOG_NOTICE,
				"Got DAHDI_EVENT_REMOVED. Destroying channel %d\n",
				i->channel);
		return i;
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
	return NULL;
}

static void monitor_pfds_clean(void *arg) {
	struct pollfd **pfds = arg;
	ast_free(*pfds);
}

static void *do_monitor(void *data)
{
	int count, res, res2, spoint, pollres=0;
	struct dahdi_pvt *i;
	struct dahdi_pvt *last = NULL;
	struct dahdi_pvt *doomed;
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

	pthread_cleanup_push(monitor_pfds_clean, &pfds);
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
		for (i = iflist; i; i = i->next) {
			ast_mutex_lock(&i->lock);
			if (pfds && (i->subs[SUB_REAL].dfd > -1) && i->sig && (!i->radio) && !(i->sig & SIG_MFCR2)) {
				if (analog_lib_handles(i->sig, i->radio, i->oprmode)) {
					struct analog_pvt *p = i->sig_pvt;

					if (!p) {
						ast_log(LOG_ERROR, "No sig_pvt?\n");
					} else if (!p->owner && !p->subs[SUB_REAL].owner) {
						/* This needs to be watched, as it lacks an owner */
						pfds[count].fd = i->subs[SUB_REAL].dfd;
						pfds[count].events = POLLPRI;
						pfds[count].revents = 0;
						/* Message waiting or r2 channels also get watched for reading */
						if (i->cidspill || i->mwisendactive || i->mwimonitor_fsk || 
							(i->cid_start == CID_START_DTMF_NOALERT && (i->sig == SIG_FXSLS || i->sig == SIG_FXSGS || i->sig == SIG_FXSKS))) {
							pfds[count].events |= POLLIN;
						}
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
						if (i->cidspill || i->mwisendactive || i->mwimonitor_fsk ||
							(i->cid_start == CID_START_DTMF_NOALERT && (i->sig == SIG_FXSLS || i->sig == SIG_FXSGS || i->sig == SIG_FXSKS))) {
							pfds[count].events |= POLLIN;
						}
						count++;
					}
				}
			}
			ast_mutex_unlock(&i->lock);
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
		doomed = NULL;
		for (i = iflist;; i = i->next) {
			if (doomed) {
				int res;
				res = dahdi_destroy_channel_bynum(doomed->channel);
				if (res != RESULT_SUCCESS) {
					ast_log(LOG_WARNING, "Couldn't find channel to destroy, hopefully another destroy operation just happened.\n");
				}
				doomed = NULL;
			}
			if (!i) {
				break;
			}

			if (thispass != lastpass) {
				if (!found && ((i == last) || ((i == iflist) && !last))) {
					last = i;
					if (last) {
						struct analog_pvt *analog_p = last->sig_pvt;
						/* Only allow MWI to be initiated on a quiescent fxs port */
						if (analog_p
							&& !last->mwisendactive
							&& (last->sig & __DAHDI_SIG_FXO)
							&& !analog_p->fxsoffhookstate
							&& !last->owner
							&& !ast_strlen_zero(last->mailbox)
							&& (thispass - analog_p->onhooktime > 3)) {
							res = has_voicemail(last);
							if (analog_p->msgstate != res) {
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
								analog_p->msgstate = res;
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
							doomed = (struct dahdi_pvt *) analog_handle_init_event(i->sig_pvt, dahdievent_to_analogevent(res));
						else
							doomed = handle_init_event(i, res);
						ast_mutex_lock(&iflock);
					}
					continue;
				}
				pollres = ast_fdisset(pfds, i->subs[SUB_REAL].dfd, count, &spoint);
				if (pollres & POLLIN) {
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef HAVE_PRI
						if (!i->pri)
#endif
							ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d) in read...\n", i->subs[SUB_REAL].dfd);
						continue;
					}
					if (!i->mwimonitor_fsk && !i->mwisendactive  && i->cid_start != CID_START_DTMF_NOALERT) {
						ast_log(LOG_WARNING, "Whoa....  I'm not looking for MWI or sending MWI but am reading (%d)...\n", i->subs[SUB_REAL].dfd);
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

								ast_debug(1, "Maybe some MWI on port %d!\n", i->channel);
								if ((mtd = ast_calloc(1, sizeof(*mtd)))) {
									mtd->pvt = i;
									memcpy(mtd->buf, buf, res);
									mtd->len = res;
									i->mwimonitoractive = 1;
									if (ast_pthread_create_background(&threadid, &attr, mwi_thread, mtd)) {
										ast_log(LOG_WARNING, "Unable to start mwi thread on channel %d\n", i->channel);
										i->mwimonitoractive = 0;
										ast_free(mtd);
									}
								}
							}
						/* If configured to check for a DTMF CID spill that comes without alert (e.g no polarity reversal) */
						} else if (i->cid_start == CID_START_DTMF_NOALERT) {
							int energy;
							struct timeval now;
							/* State machine dtmfcid_holdoff_state allows for the line to settle
							 * before checking agin for dtmf energy.  Presently waits for 500 mS before checking again 
							*/
							if (1 == i->dtmfcid_holdoff_state) {
								gettimeofday(&i->dtmfcid_delay, NULL);
								i->dtmfcid_holdoff_state = 2;
							} else if (2 == i->dtmfcid_holdoff_state) {
								gettimeofday(&now, NULL);
								if ((int)(now.tv_sec - i->dtmfcid_delay.tv_sec) * 1000000 + (int)now.tv_usec - (int)i->dtmfcid_delay.tv_usec > 500000) {
									i->dtmfcid_holdoff_state = 0;
								}
							} else {
								energy = calc_energy((unsigned char *) buf, res, AST_LAW(i));
								if (!i->mwisendactive && energy > dtmfcid_level) {
									pthread_t threadid;
									struct ast_channel *chan;
									ast_mutex_unlock(&iflock);
									if (analog_lib_handles(i->sig, i->radio, i->oprmode)) {
										/* just in case this event changes or somehow destroys a channel, set doomed here too */
										doomed = analog_handle_init_event(i->sig_pvt, ANALOG_EVENT_DTMFCID);  
										i->dtmfcid_holdoff_state = 1;
									} else {
										struct ast_callid *callid = NULL;
										int callid_created = ast_callid_threadstorage_auto(&callid);
										chan = dahdi_new(i, AST_STATE_PRERING, 0, SUB_REAL, 0, NULL, callid);
										if (!chan) {
											ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
										} else {
											res = ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, chan);
											if (res) {
												ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
												ast_hangup(chan);
											} else {
												i->dtmfcid_holdoff_state = 1;
											}
										}
										ast_callid_threadstorage_auto_clean(callid, callid_created);
									}
									ast_mutex_lock(&iflock);
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
						continue;
					}
					res = dahdi_get_event(i->subs[SUB_REAL].dfd);
					ast_debug(1, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
					/* Don't hold iflock while handling init events */
					ast_mutex_unlock(&iflock);
					if (0 == i->mwisendactive || 0 == mwi_send_process_event(i, res)) {
						if (analog_lib_handles(i->sig, i->radio, i->oprmode))
							doomed = (struct dahdi_pvt *) analog_handle_init_event(i->sig_pvt, dahdievent_to_analogevent(res));
						else
							doomed = handle_init_event(i, res);
					}
					ast_mutex_lock(&iflock);
				}
			}
		}
		ast_mutex_unlock(&iflock);
	}
	/* Never reached */
	pthread_cleanup_pop(1);
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
	for (y = 0; y < SIG_PRI_NUM_DCHANS; y++) {
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
			close(fd);
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

	numvals = sscanf(pcstring, "%30d-%30d-%30d", &code1, &code2, &code3);
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

/* This is an artificial convenient capacity, to keep at most a full E1 of channels in a single thread */
#define R2_LINK_CAPACITY 30
static struct dahdi_mfcr2 *dahdi_r2_get_link(const struct dahdi_chan_conf *conf)
{
	struct dahdi_mfcr2 *new_r2link = NULL;
	struct dahdi_mfcr2 **new_r2links = NULL;

	/* Only create a new R2 link if 
	   1. This is the first link requested
	   2. Configuration changed 
	   3. We got more channels than supported per link */
	if (!r2links_count ||
	    memcmp(&conf->mfcr2, &r2links[r2links_count - 1]->conf, sizeof(conf->mfcr2)) ||
	   (r2links[r2links_count - 1]->numchans == R2_LINK_CAPACITY)) {
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
		ast_debug(1, "Created new R2 link!\n");
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
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 2
	openr2_context_set_dtmf_dialing(r2_link->protocol_context, conf->mfcr2.dtmf_dialing, conf->mfcr2.dtmf_time_on, conf->mfcr2.dtmf_time_off);
	openr2_context_set_dtmf_detection(r2_link->protocol_context, conf->mfcr2.dtmf_detection);
#endif
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 3
	openr2_context_set_dtmf_detection_end_timeout(r2_link->protocol_context, conf->mfcr2.dtmf_end_timeout);
#endif
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
	/* Save the configuration used to setup this link */
	memcpy(&r2_link->conf, conf, sizeof(r2_link->conf));
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

/*!
 * \internal
 * \brief Get file name and channel number from (subdir,number)
 *
 * \param subdir name of the subdirectory under /dev/dahdi/
 * \param channel name of device file under /dev/dahdi/<subdir>/
 * \param path buffer to put file name in
 * \param pathlen maximal length of path
 *
 * \retval minor number of dahdi channel.
 * \retval -errno on error.
 */
static int device2chan(const char *subdir, int channel, char *path, int pathlen)
{
	struct stat	stbuf;
	int		num;

	snprintf(path, pathlen, "/dev/dahdi/%s/%d", subdir, channel);
	if (stat(path, &stbuf) < 0) {
		ast_log(LOG_ERROR, "stat(%s) failed: %s\n", path, strerror(errno));
		return -errno;
	}
	if (!S_ISCHR(stbuf.st_mode)) {
		ast_log(LOG_ERROR, "%s: Not a character device file\n", path);
		return -EINVAL;
	}
	num = minor(stbuf.st_rdev);
	ast_debug(1, "%s -> %d\n", path, num);
	return num;

}

/*!
 * \internal
 * \brief Initialize/create a channel interface.
 *
 * \param channel Channel interface number to initialize/create.
 * \param conf Configuration parameters to initialize interface with.
 * \param reloading What we are doing now:
 * 0 - initial module load,
 * 1 - module reload,
 * 2 - module restart
 *
 * \retval Interface-pointer initialized/created
 * \retval NULL if error
 */
static struct dahdi_pvt *mkintf(int channel, const struct dahdi_chan_conf *conf, int reloading)
{
	/* Make a dahdi_pvt structure for this interface */
	struct dahdi_pvt *tmp;/*!< Current channel structure initializing */
	char fn[80];
	struct dahdi_bufferinfo bi;

	int res;
#if defined(HAVE_PRI)
	int span = 0;
#endif	/* defined(HAVE_PRI) */
	int here = 0;/*!< TRUE if the channel interface already exists. */
	int x;
	struct analog_pvt *analog_p = NULL;
	struct dahdi_params p;
#if defined(HAVE_PRI)
	struct dahdi_spaninfo si;
	struct sig_pri_chan *pri_chan = NULL;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	struct sig_ss7_chan *ss7_chan = NULL;
#endif	/* defined(HAVE_SS7) */

	/* Search channel interface list to see if it already exists. */
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (!tmp->destroy) {
			if (tmp->channel == channel) {
				/* The channel interface already exists. */
				here = 1;
				break;
			}
			if (tmp->channel > channel) {
				/* No way it can be in the sorted list. */
				tmp = NULL;
				break;
			}
		}
	}

	if (!here && reloading != 1) {
		tmp = ast_calloc(1, sizeof(*tmp));
		if (!tmp) {
			return NULL;
		}
		tmp->cc_params = ast_cc_config_params_init();
		if (!tmp->cc_params) {
			ast_free(tmp);
			return NULL;
		}
		ast_mutex_init(&tmp->lock);
		ifcount++;
		for (x = 0; x < 3; x++)
			tmp->subs[x].dfd = -1;
		tmp->channel = channel;
		tmp->priindication_oob = conf->chan.priindication_oob;
	}

	if (tmp) {
		int chan_sig = conf->chan.sig;

		/* If there are variables in tmp before it is updated to match the new config, clear them */
		if (reloading && tmp->vars) {
			ast_variables_destroy(tmp->vars);
			tmp->vars = NULL;
		}

		if (!here) {
			/* Can only get here if this is a new channel interface being created. */
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
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
				if (res < 0) {
					ast_log(LOG_ERROR, "Unable to get parameters: %s\n", strerror(errno));
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				if (conf->is_sig_auto)
					chan_sig = sigtype_to_signalling(p.sigtype);
				if (p.sigtype != (chan_sig & 0x3ffff)) {
					ast_log(LOG_ERROR, "Signalling requested on channel %d is %s but line is in %s signalling\n", channel, sig2str(chan_sig), sig2str(p.sigtype));
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				tmp->law_default = p.curlaw;
				tmp->law = p.curlaw;
				tmp->span = p.spanno;
#if defined(HAVE_PRI)
				span = p.spanno - 1;
#endif	/* defined(HAVE_PRI) */
			} else {
				chan_sig = 0;
			}
			tmp->sig = chan_sig;
			tmp->outsigmod = conf->chan.outsigmod;

			if (analog_lib_handles(chan_sig, tmp->radio, tmp->oprmode)) {
				analog_p = analog_new(dahdisig_to_analogsig(chan_sig), tmp);
				if (!analog_p) {
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				tmp->sig_pvt = analog_p;
			}
#if defined(HAVE_SS7)
			if (chan_sig == SIG_SS7) {
				struct dahdi_ss7 *ss7;
				int clear = 0;

				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &clear)) {
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_dahdi_pvt(tmp);
					return NULL;
				}

				ss7 = ss7_resolve_linkset(cur_linkset);
				if (!ss7) {
					ast_log(LOG_ERROR, "Unable to find linkset %d\n", cur_linkset);
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				ss7->ss7.span = cur_linkset;
				if (cur_cicbeginswith < 0) {
					ast_log(LOG_ERROR, "Need to set cicbeginswith for the channels!\n");
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				ss7_chan = sig_ss7_chan_new(tmp, &ss7->ss7);
				if (!ss7_chan) {
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				tmp->sig_pvt = ss7_chan;
				tmp->ss7 = &ss7->ss7;

				ss7_chan->channel = tmp->channel;
				ss7_chan->cic = cur_cicbeginswith++;

				/* DB: Add CIC's DPC information */
				ss7_chan->dpc = cur_defaultdpc;

				ss7->ss7.pvts[ss7->ss7.numchans++] = ss7_chan;

				ast_copy_string(ss7->ss7.internationalprefix, conf->ss7.ss7.internationalprefix, sizeof(ss7->ss7.internationalprefix));
				ast_copy_string(ss7->ss7.nationalprefix, conf->ss7.ss7.nationalprefix, sizeof(ss7->ss7.nationalprefix));
				ast_copy_string(ss7->ss7.subscriberprefix, conf->ss7.ss7.subscriberprefix, sizeof(ss7->ss7.subscriberprefix));
				ast_copy_string(ss7->ss7.unknownprefix, conf->ss7.ss7.unknownprefix, sizeof(ss7->ss7.unknownprefix));

				ss7->ss7.called_nai = conf->ss7.ss7.called_nai;
				ss7->ss7.calling_nai = conf->ss7.ss7.calling_nai;
			}
#endif	/* defined(HAVE_SS7) */
#ifdef HAVE_OPENR2
			if (chan_sig == SIG_MFCR2) {
				struct dahdi_mfcr2 *r2_link;
				r2_link = dahdi_r2_get_link(conf);
				if (!r2_link) {
					ast_log(LOG_WARNING, "Cannot get another R2 DAHDI context!\n");
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				if (!r2_link->protocol_context && dahdi_r2_set_context(r2_link, conf)) {
					ast_log(LOG_ERROR, "Cannot create OpenR2 protocol context.\n");
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				if (r2_link->numchans == ARRAY_LEN(r2_link->pvts)) {
					ast_log(LOG_ERROR, "Cannot add more channels to this link!\n");
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				r2_link->pvts[r2_link->numchans++] = tmp;
				tmp->r2chan = openr2_chan_new_from_fd(r2_link->protocol_context,
						                      tmp->subs[SUB_REAL].dfd,
						                      NULL, NULL);
				if (!tmp->r2chan) {
					openr2_liberr_t err = openr2_context_get_last_error(r2_link->protocol_context);
					ast_log(LOG_ERROR, "Cannot create OpenR2 channel: %s\n", openr2_context_error_string(err));
					destroy_dahdi_pvt(tmp);
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
			}
#endif
#ifdef HAVE_PRI
			if (dahdi_sig_pri_lib_handles(chan_sig)) {
				int offset;
				int matchesdchan;
				int x,y;
				int myswitchtype = 0;

				offset = 0;
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &offset)) {
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_dahdi_pvt(tmp);
					return NULL;
				}
				if (span >= NUM_SPANS) {
					ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
					destroy_dahdi_pvt(tmp);
					return NULL;
				} else {
					si.spanno = 0;
					if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1) {
						ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
						destroy_dahdi_pvt(tmp);
						return NULL;
					}
					/* Store the logical span first based upon the real span */
					tmp->logicalspan = pris[span].prilogicalspan;
					pri_resolve_span(&span, channel, (channel - p.chanpos), &si);
					if (span < 0) {
						ast_log(LOG_WARNING, "Channel %d: Unable to find locate channel/trunk group!\n", channel);
						destroy_dahdi_pvt(tmp);
						return NULL;
					}
					myswitchtype = conf->pri.pri.switchtype;
					/* Make sure this isn't a d-channel */
					matchesdchan=0;
					for (x = 0; x < NUM_SPANS; x++) {
						for (y = 0; y < SIG_PRI_NUM_DCHANS; y++) {
							if (pris[x].dchannels[y] == tmp->channel) {
								matchesdchan = 1;
								break;
							}
						}
					}
					if (!matchesdchan) {
						if (pris[span].pri.nodetype && (pris[span].pri.nodetype != conf->pri.pri.nodetype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s node\n", span + 1, pri_node2str(pris[span].pri.nodetype));
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if (pris[span].pri.switchtype && (pris[span].pri.switchtype != myswitchtype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, pri_switch2str(pris[span].pri.switchtype));
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if ((pris[span].pri.dialplan) && (pris[span].pri.dialplan != conf->pri.pri.dialplan)) {
							ast_log(LOG_ERROR, "Span %d is already a %s dialing plan\n", span + 1, pris[span].pri.dialplan == -1 ? "Dynamically set dialplan in ISDN" : pri_plan2str(pris[span].pri.dialplan));
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if (!ast_strlen_zero(pris[span].pri.idledial) && strcmp(pris[span].pri.idledial, conf->pri.pri.idledial)) {
							ast_log(LOG_ERROR, "Span %d already has idledial '%s'.\n", span + 1, conf->pri.pri.idledial);
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if (!ast_strlen_zero(pris[span].pri.idleext) && strcmp(pris[span].pri.idleext, conf->pri.pri.idleext)) {
							ast_log(LOG_ERROR, "Span %d already has idleext '%s'.\n", span + 1, conf->pri.pri.idleext);
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if (pris[span].pri.minunused && (pris[span].pri.minunused != conf->pri.pri.minunused)) {
							ast_log(LOG_ERROR, "Span %d already has minunused of %d.\n", span + 1, conf->pri.pri.minunused);
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if (pris[span].pri.minidle && (pris[span].pri.minidle != conf->pri.pri.minidle)) {
							ast_log(LOG_ERROR, "Span %d already has minidle of %d.\n", span + 1, conf->pri.pri.minidle);
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						if (pris[span].pri.numchans >= ARRAY_LEN(pris[span].pri.pvts)) {
							ast_log(LOG_ERROR, "Unable to add channel %d: Too many channels in trunk group %d!\n", channel,
								pris[span].pri.trunkgroup);
							destroy_dahdi_pvt(tmp);
							return NULL;
						}

						pri_chan = sig_pri_chan_new(tmp, &pris[span].pri, tmp->logicalspan, p.chanpos, pris[span].mastertrunkgroup);
						if (!pri_chan) {
							destroy_dahdi_pvt(tmp);
							return NULL;
						}
						tmp->sig_pvt = pri_chan;
						tmp->pri = &pris[span].pri;

						tmp->priexclusive = conf->chan.priexclusive;

						if (!tmp->pri->cc_params) {
							tmp->pri->cc_params = ast_cc_config_params_init();
							if (!tmp->pri->cc_params) {
								destroy_dahdi_pvt(tmp);
								return NULL;
							}
						}
						ast_cc_copy_config_params(tmp->pri->cc_params,
							conf->chan.cc_params);

						pris[span].pri.sig = chan_sig;
						pris[span].pri.nodetype = conf->pri.pri.nodetype;
						pris[span].pri.switchtype = myswitchtype;
						pris[span].pri.nsf = conf->pri.pri.nsf;
						pris[span].pri.dialplan = conf->pri.pri.dialplan;
						pris[span].pri.localdialplan = conf->pri.pri.localdialplan;
						pris[span].pri.cpndialplan = conf->pri.pri.cpndialplan;
						pris[span].pri.pvts[pris[span].pri.numchans++] = tmp->sig_pvt;
						pris[span].pri.minunused = conf->pri.pri.minunused;
						pris[span].pri.minidle = conf->pri.pri.minidle;
						pris[span].pri.overlapdial = conf->pri.pri.overlapdial;
						pris[span].pri.qsigchannelmapping = conf->pri.pri.qsigchannelmapping;
						pris[span].pri.discardremoteholdretrieval = conf->pri.pri.discardremoteholdretrieval;
#if defined(HAVE_PRI_SERVICE_MESSAGES)
						pris[span].pri.enable_service_message_support = conf->pri.pri.enable_service_message_support;
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
#ifdef HAVE_PRI_INBANDDISCONNECT
						pris[span].pri.inbanddisconnect = conf->pri.pri.inbanddisconnect;
#endif
#if defined(HAVE_PRI_CALL_HOLD)
						pris[span].pri.hold_disconnect_transfer =
							conf->pri.pri.hold_disconnect_transfer;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CCSS)
						pris[span].pri.cc_ptmp_recall_mode =
							conf->pri.pri.cc_ptmp_recall_mode;
						pris[span].pri.cc_qsig_signaling_link_req =
							conf->pri.pri.cc_qsig_signaling_link_req;
						pris[span].pri.cc_qsig_signaling_link_rsp =
							conf->pri.pri.cc_qsig_signaling_link_rsp;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CALL_WAITING)
						pris[span].pri.max_call_waiting_calls =
							conf->pri.pri.max_call_waiting_calls;
						pris[span].pri.allow_call_waiting_calls =
							conf->pri.pri.allow_call_waiting_calls;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
						pris[span].pri.transfer = conf->chan.transfer;
						pris[span].pri.facilityenable = conf->pri.pri.facilityenable;
#if defined(HAVE_PRI_L2_PERSISTENCE)
						pris[span].pri.l2_persistence = conf->pri.pri.l2_persistence;
#endif	/* defined(HAVE_PRI_L2_PERSISTENCE) */
						pris[span].pri.colp_send = conf->pri.pri.colp_send;
#if defined(HAVE_PRI_AOC_EVENTS)
						pris[span].pri.aoc_passthrough_flag = conf->pri.pri.aoc_passthrough_flag;
						pris[span].pri.aoce_delayhangup = conf->pri.pri.aoce_delayhangup;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
						if (chan_sig == SIG_BRI_PTMP) {
							pris[span].pri.layer1_ignored = conf->pri.pri.layer1_ignored;
						} else {
							/* Option does not apply to this line type. */
							pris[span].pri.layer1_ignored = 0;
						}
						pris[span].pri.append_msn_to_user_tag = conf->pri.pri.append_msn_to_user_tag;
						pris[span].pri.inband_on_setup_ack = conf->pri.pri.inband_on_setup_ack;
						pris[span].pri.inband_on_proceeding = conf->pri.pri.inband_on_proceeding;
						ast_copy_string(pris[span].pri.initial_user_tag, conf->chan.cid_tag, sizeof(pris[span].pri.initial_user_tag));
						ast_copy_string(pris[span].pri.msn_list, conf->pri.pri.msn_list, sizeof(pris[span].pri.msn_list));
#if defined(HAVE_PRI_MWI)
						ast_copy_string(pris[span].pri.mwi_mailboxes,
							conf->pri.pri.mwi_mailboxes,
							sizeof(pris[span].pri.mwi_mailboxes));
						ast_copy_string(pris[span].pri.mwi_vm_numbers,
							conf->pri.pri.mwi_vm_numbers,
							sizeof(pris[span].pri.mwi_vm_numbers));
#endif	/* defined(HAVE_PRI_MWI) */
						ast_copy_string(pris[span].pri.idledial, conf->pri.pri.idledial, sizeof(pris[span].pri.idledial));
						ast_copy_string(pris[span].pri.idleext, conf->pri.pri.idleext, sizeof(pris[span].pri.idleext));
						ast_copy_string(pris[span].pri.internationalprefix, conf->pri.pri.internationalprefix, sizeof(pris[span].pri.internationalprefix));
						ast_copy_string(pris[span].pri.nationalprefix, conf->pri.pri.nationalprefix, sizeof(pris[span].pri.nationalprefix));
						ast_copy_string(pris[span].pri.localprefix, conf->pri.pri.localprefix, sizeof(pris[span].pri.localprefix));
						ast_copy_string(pris[span].pri.privateprefix, conf->pri.pri.privateprefix, sizeof(pris[span].pri.privateprefix));
						ast_copy_string(pris[span].pri.unknownprefix, conf->pri.pri.unknownprefix, sizeof(pris[span].pri.unknownprefix));
						pris[span].pri.moh_signaling = conf->pri.pri.moh_signaling;
						pris[span].pri.resetinterval = conf->pri.pri.resetinterval;
#if defined(HAVE_PRI_DISPLAY_TEXT)
						pris[span].pri.display_flags_send = conf->pri.pri.display_flags_send;
						pris[span].pri.display_flags_receive = conf->pri.pri.display_flags_receive;
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
#if defined(HAVE_PRI_MCID)
						pris[span].pri.mcid_send = conf->pri.pri.mcid_send;
#endif	/* defined(HAVE_PRI_MCID) */
						pris[span].pri.force_restart_unavailable_chans = conf->pri.pri.force_restart_unavailable_chans;
#if defined(HAVE_PRI_DATETIME_SEND)
						pris[span].pri.datetime_send = conf->pri.pri.datetime_send;
#endif	/* defined(HAVE_PRI_DATETIME_SEND) */

						for (x = 0; x < PRI_MAX_TIMERS; x++) {
							pris[span].pri.pritimers[x] = conf->pri.pri.pritimers[x];
						}

#if defined(HAVE_PRI_CALL_WAITING)
						/* Channel initial config parameters. */
						pris[span].pri.ch_cfg.stripmsd = conf->chan.stripmsd;
						pris[span].pri.ch_cfg.hidecallerid = conf->chan.hidecallerid;
						pris[span].pri.ch_cfg.hidecalleridname = conf->chan.hidecalleridname;
						pris[span].pri.ch_cfg.immediate = conf->chan.immediate;
						pris[span].pri.ch_cfg.priexclusive = conf->chan.priexclusive;
						pris[span].pri.ch_cfg.priindication_oob = conf->chan.priindication_oob;
						pris[span].pri.ch_cfg.use_callerid = conf->chan.use_callerid;
						pris[span].pri.ch_cfg.use_callingpres = conf->chan.use_callingpres;
						ast_copy_string(pris[span].pri.ch_cfg.context, conf->chan.context, sizeof(pris[span].pri.ch_cfg.context));
						ast_copy_string(pris[span].pri.ch_cfg.mohinterpret, conf->chan.mohinterpret, sizeof(pris[span].pri.ch_cfg.mohinterpret));
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
					} else {
						ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", p.chanpos);
						destroy_dahdi_pvt(tmp);
						return NULL;
					}
				}
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
		} else {
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

		/* don't set parms on a pseudo-channel */
		if (tmp->subs[SUB_REAL].dfd >= 0)
		{
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters: %s\n", strerror(errno));
				destroy_dahdi_pvt(tmp);
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
		tmp->hidecalleridname = conf->chan.hidecalleridname;
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
		tmp->busy_cadence = conf->chan.busy_cadence;
		tmp->callprogress = conf->chan.callprogress;
		tmp->waitfordialtone = conf->chan.waitfordialtone;
		tmp->dialtone_detect = conf->chan.dialtone_detect;
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
		ast_copy_string(tmp->description, conf->chan.description, sizeof(tmp->description));
		ast_copy_string(tmp->parkinglot, conf->chan.parkinglot, sizeof(tmp->parkinglot));
		tmp->cid_ton = 0;
		if (analog_lib_handles(tmp->sig, tmp->radio, tmp->oprmode)) {
			ast_copy_string(tmp->cid_num, conf->chan.cid_num, sizeof(tmp->cid_num));
			ast_copy_string(tmp->cid_name, conf->chan.cid_name, sizeof(tmp->cid_name));
		} else {
			tmp->cid_num[0] = '\0';
			tmp->cid_name[0] = '\0';
		}
#if defined(HAVE_PRI)
		if (dahdi_sig_pri_lib_handles(tmp->sig)) {
			tmp->cid_tag[0] = '\0';
		} else
#endif	/* defined(HAVE_PRI) */
		{
			ast_copy_string(tmp->cid_tag, conf->chan.cid_tag, sizeof(tmp->cid_tag));
		}
		tmp->cid_subaddr[0] = '\0';
		ast_copy_string(tmp->mailbox, conf->chan.mailbox, sizeof(tmp->mailbox));
		if (channel != CHAN_PSEUDO && !ast_strlen_zero(tmp->mailbox)) {
			char *mailbox, *context;
			mailbox = context = ast_strdupa(tmp->mailbox);
			strsep(&context, "@");
			if (ast_strlen_zero(context))
				context = "default";
			tmp->mwi_event_sub = ast_event_subscribe(AST_EVENT_MWI, mwi_event_cb, "Dahdi MWI subscription", NULL,
				AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
				AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
				AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_EXISTS,
				AST_EVENT_IE_END);
		}
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
		tmp->mwisend_setting = conf->chan.mwisend_setting;
		tmp->mwisend_fsk  = conf->chan.mwisend_fsk;
		tmp->mwisend_rpas = conf->chan.mwisend_rpas;
#endif

		tmp->group = conf->chan.group;
		tmp->callgroup = conf->chan.callgroup;
		tmp->pickupgroup= conf->chan.pickupgroup;
		ast_unref_namedgroups(tmp->named_callgroups);
		tmp->named_callgroups = ast_ref_namedgroups(conf->chan.named_callgroups);
		ast_unref_namedgroups(tmp->named_pickupgroups);
		tmp->named_pickupgroups = ast_ref_namedgroups(conf->chan.named_pickupgroups);
		if (conf->chan.vars) {
			struct ast_variable *v, *tmpvar;
	                for (v = conf->chan.vars ; v ; v = v->next) {
        	                if ((tmpvar = ast_variable_new(v->name, v->value, v->file))) {
                	                tmpvar->next = tmp->vars;
                        	        tmp->vars = tmpvar;
                        	}
                	}
		}
		tmp->cid_rxgain = conf->chan.cid_rxgain;
		tmp->rxgain = conf->chan.rxgain;
		tmp->txgain = conf->chan.txgain;
		tmp->txdrc = conf->chan.txdrc;
		tmp->rxdrc = conf->chan.rxdrc;
		tmp->tonezone = conf->chan.tonezone;
		if (tmp->subs[SUB_REAL].dfd > -1) {
			set_actual_gain(tmp->subs[SUB_REAL].dfd, tmp->rxgain, tmp->txgain, tmp->rxdrc, tmp->txdrc, tmp->law);
			if (tmp->dsp)
				ast_dsp_set_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
			update_conf(tmp);
			if (!here) {
				switch (chan_sig) {
				case SIG_PRI_LIB_HANDLE_CASES:
				case SIG_SS7:
				case SIG_MFCR2:
					break;
				default:
					/* Hang it up to be sure it's good */
					dahdi_set_hook(tmp->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
					break;
				}
			}
			ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SETTONEZONE,&tmp->tonezone);
			if ((res = get_alarms(tmp)) != DAHDI_ALARM_NONE) {
				/* the dchannel is down so put the channel in alarm */
				switch (tmp->sig) {
#ifdef HAVE_PRI
				case SIG_PRI_LIB_HANDLE_CASES:
					sig_pri_set_alarm(tmp->sig_pvt, 1);
					break;
#endif
#if defined(HAVE_SS7)
				case SIG_SS7:
					sig_ss7_set_alarm(tmp->sig_pvt, 1);
					break;
#endif	/* defined(HAVE_SS7) */
				default:
					/* The only sig submodule left should be sig_analog. */
					analog_p = tmp->sig_pvt;
					if (analog_p) {
						analog_p->inalarm = 1;
					}
					tmp->inalarm = 1;
					break;
				}
				handle_alarms(tmp, res);
			}
		}

		tmp->polarityonanswerdelay = conf->chan.polarityonanswerdelay;
		tmp->answeronpolarityswitch = conf->chan.answeronpolarityswitch;
		tmp->hanguponpolarityswitch = conf->chan.hanguponpolarityswitch;
		tmp->sendcalleridafter = conf->chan.sendcalleridafter;
		ast_cc_copy_config_params(tmp->cc_params, conf->chan.cc_params);

		if (!here) {
			tmp->locallyblocked = 0;
			tmp->remotelyblocked = 0;
			switch (tmp->sig) {
#if defined(HAVE_PRI)
			case SIG_PRI_LIB_HANDLE_CASES:
				tmp->inservice = 1;/* Inservice until actually implemented. */
#if defined(HAVE_PRI_SERVICE_MESSAGES)
				((struct sig_pri_chan *) tmp->sig_pvt)->service_status = 0;
				if (chan_sig == SIG_PRI) {
					char db_chan_name[20];
					char db_answer[5];

					/*
					 * Initialize the active out-of-service status
					 * and delete any record if the feature is not enabled.
					 */
					snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, tmp->span, tmp->channel);
					if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
						unsigned *why;

						why = &((struct sig_pri_chan *) tmp->sig_pvt)->service_status;
						if (tmp->pri->enable_service_message_support) {
							char state;

							sscanf(db_answer, "%1c:%30u", &state, why);

							/* Ensure that only the implemented bits could be set.*/
							*why &= (SRVST_NEAREND | SRVST_FAREND);
						}
						if (!*why) {
							ast_db_del(db_chan_name, SRVST_DBKEY);
						}
					}
				}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
				break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
			case SIG_SS7:
				tmp->inservice = 0;
				break;
#endif	/* defined(HAVE_SS7) */
			default:
				 /* We default to in service on protocols that don't have a reset */
				tmp->inservice = 1;
				break;
			}
		}

		switch (tmp->sig) {
#if defined(HAVE_PRI)
		case SIG_PRI_LIB_HANDLE_CASES:
			if (pri_chan) {
				pri_chan->channel = tmp->channel;
				pri_chan->hidecallerid = tmp->hidecallerid;
				pri_chan->hidecalleridname = tmp->hidecalleridname;
				pri_chan->immediate = tmp->immediate;
				pri_chan->inalarm = tmp->inalarm;
				pri_chan->priexclusive = tmp->priexclusive;
				pri_chan->priindication_oob = tmp->priindication_oob;
				pri_chan->use_callerid = tmp->use_callerid;
				pri_chan->use_callingpres = tmp->use_callingpres;
				ast_copy_string(pri_chan->context, tmp->context,
					sizeof(pri_chan->context));
				ast_copy_string(pri_chan->mohinterpret, tmp->mohinterpret,
					sizeof(pri_chan->mohinterpret));
				pri_chan->stripmsd = tmp->stripmsd;
			}
			break;
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
		case SIG_SS7:
			if (ss7_chan) {
				ss7_chan->inalarm = tmp->inalarm;

				ss7_chan->stripmsd = tmp->stripmsd;
				ss7_chan->hidecallerid = tmp->hidecallerid;
				ss7_chan->use_callerid = tmp->use_callerid;
				ss7_chan->use_callingpres = tmp->use_callingpres;
				ss7_chan->immediate = tmp->immediate;
				ss7_chan->locallyblocked = tmp->locallyblocked;
				ss7_chan->remotelyblocked = tmp->remotelyblocked;
				ast_copy_string(ss7_chan->context, tmp->context,
					sizeof(ss7_chan->context));
				ast_copy_string(ss7_chan->mohinterpret, tmp->mohinterpret,
					sizeof(ss7_chan->mohinterpret));
			}
			break;
#endif	/* defined(HAVE_SS7) */
		default:
			/* The only sig submodule left should be sig_analog. */
			analog_p = tmp->sig_pvt;
			if (analog_p) {
				analog_p->channel = tmp->channel;
				analog_p->polarityonanswerdelay = conf->chan.polarityonanswerdelay;
				analog_p->answeronpolarityswitch = conf->chan.answeronpolarityswitch;
				analog_p->hanguponpolarityswitch = conf->chan.hanguponpolarityswitch;
				analog_p->permcallwaiting = conf->chan.callwaiting; /* permcallwaiting possibly modified in analog_config_complete */
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
				analog_p->use_callerid = tmp->use_callerid;
				analog_p->usedistinctiveringdetection = tmp->usedistinctiveringdetection;
				analog_p->use_smdi = tmp->use_smdi;
				analog_p->smdi_iface = tmp->smdi_iface;
				analog_p->outsigmod = ANALOG_SIG_NONE;
				analog_p->echotraining = conf->chan.echotraining;
				analog_p->cid_signalling = conf->chan.cid_signalling;
				analog_p->stripmsd = conf->chan.stripmsd;
				switch (conf->chan.cid_start) {
				case CID_START_POLARITY:
					analog_p->cid_start = ANALOG_CID_START_POLARITY;
					break;
				case CID_START_POLARITY_IN:
					analog_p->cid_start = ANALOG_CID_START_POLARITY_IN;
					break;
				case CID_START_DTMF_NOALERT:
					analog_p->cid_start = ANALOG_CID_START_DTMF_NOALERT;
					break;
				default:
					analog_p->cid_start = ANALOG_CID_START_RING;
					break;
				}
				analog_p->callwaitingcallerid = conf->chan.callwaitingcallerid;
				analog_p->ringt = conf->chan.ringt;
				analog_p->ringt_base = ringt_base;
				analog_p->onhooktime = time(NULL);
				if (chan_sig & __DAHDI_SIG_FXO) {
					memset(&p, 0, sizeof(p));
					res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
					if (!res) {
						analog_p->fxsoffhookstate = p.rxisoffhook;
					}
#ifdef HAVE_DAHDI_LINEREVERSE_VMWI
					res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_VMWI_CONFIG, &tmp->mwisend_setting);
#endif
				}
				analog_p->msgstate = -1;

				ast_copy_string(analog_p->mohsuggest, conf->chan.mohsuggest, sizeof(analog_p->mohsuggest));
				ast_copy_string(analog_p->cid_num, conf->chan.cid_num, sizeof(analog_p->cid_num));
				ast_copy_string(analog_p->cid_name, conf->chan.cid_name, sizeof(analog_p->cid_name));

				analog_config_complete(analog_p);
			}
			break;
		}
#if defined(HAVE_PRI)
		if (tmp->channel == CHAN_PSEUDO) {
			/*
			 * Save off pseudo channel buffer policy values for dynamic creation of
			 * no B channel interfaces.
			 */
			dahdi_pseudo_parms.buf_no = tmp->buf_no;
			dahdi_pseudo_parms.buf_policy = tmp->buf_policy;
			dahdi_pseudo_parms.faxbuf_no = tmp->faxbuf_no;
			dahdi_pseudo_parms.faxbuf_policy = tmp->faxbuf_policy;
		}
#endif	/* defined(HAVE_PRI) */
	}
	if (tmp && !here) {
		/* Add the new channel interface to the sorted channel interface list. */
		dahdi_iflist_insert(tmp);
	}
	return tmp;
}

static int is_group_or_channel_match(struct dahdi_pvt *p, int span, ast_group_t groupmatch, int *groupmatched, int channelmatch, int *channelmatched)
{
#if defined(HAVE_PRI)
	if (0 < span) {
		/* The channel must be on the specified PRI span. */
		if (!p->pri || p->pri->span != span) {
			return 0;
		}
		if (!groupmatch && channelmatch == -1) {
			/* Match any group since it only needs to be on the PRI span. */
			*groupmatched = 1;
			return 1;
		}
	}
#endif	/* defined(HAVE_PRI) */
	/* check group matching */
	if (groupmatch) {
		if ((p->group & groupmatch) != groupmatch)
			/* Doesn't match the specified group, try the next one */
			return 0;
		*groupmatched = 1;
	}
	/* Check to see if we have a channel match */
	if (channelmatch != -1) {
		if (p->channel != channelmatch)
			/* Doesn't match the specified channel, try the next one */
			return 0;
		*channelmatched = 1;
	}

	return 1;
}

static int available(struct dahdi_pvt **pvt, int is_specific_channel)
{
	struct dahdi_pvt *p = *pvt;

	if (p->inalarm)
		return 0;

	if (analog_lib_handles(p->sig, p->radio, p->oprmode))
		return analog_available(p->sig_pvt);

	switch (p->sig) {
#if defined(HAVE_PRI)
	case SIG_PRI_LIB_HANDLE_CASES:
		{
			struct sig_pri_chan *pvt_chan;
			int res;

			pvt_chan = p->sig_pvt;
			res = sig_pri_available(&pvt_chan, is_specific_channel);
			*pvt = pvt_chan->chan_pvt;
			return res;
		}
#endif	/* defined(HAVE_PRI) */
#if defined(HAVE_SS7)
	case SIG_SS7:
		return sig_ss7_available(p->sig_pvt);
#endif	/* defined(HAVE_SS7) */
	default:
		break;
	}

	if (p->locallyblocked || p->remotelyblocked) {
		return 0;
	}

	/* If no owner definitely available */
	if (!p->owner) {
#ifdef HAVE_OPENR2
		/* Trust MFC/R2 */
		if (p->mfcr2) {
			if (p->mfcr2call) {
				return 0;
			} else {
				return 1;
			}
		}
#endif
		return 1;
	}

	return 0;
}

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CALL_WAITING)
/*!
 * \internal
 * \brief Init the private channel configuration using the span controller.
 * \since 1.8
 *
 * \param priv Channel to init the configuration.
 * \param pri sig_pri PRI control structure.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void my_pri_init_config(void *priv, struct sig_pri_span *pri)
{
	struct dahdi_pvt *pvt = priv;

	pvt->stripmsd = pri->ch_cfg.stripmsd;
	pvt->hidecallerid = pri->ch_cfg.hidecallerid;
	pvt->hidecalleridname = pri->ch_cfg.hidecalleridname;
	pvt->immediate = pri->ch_cfg.immediate;
	pvt->priexclusive = pri->ch_cfg.priexclusive;
	pvt->priindication_oob = pri->ch_cfg.priindication_oob;
	pvt->use_callerid = pri->ch_cfg.use_callerid;
	pvt->use_callingpres = pri->ch_cfg.use_callingpres;
	ast_copy_string(pvt->context, pri->ch_cfg.context, sizeof(pvt->context));
	ast_copy_string(pvt->mohinterpret, pri->ch_cfg.mohinterpret, sizeof(pvt->mohinterpret));
}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
/*!
 * \internal
 * \brief Create a no B channel interface.
 * \since 1.8
 *
 * \param pri sig_pri span controller to add interface.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int dahdi_new_pri_nobch_channel(struct sig_pri_span *pri)
{
	int pvt_idx;
	int res;
	unsigned idx;
	struct dahdi_pvt *pvt;
	struct sig_pri_chan *chan;
	struct dahdi_bufferinfo bi;

	static int nobch_channel = CHAN_PSEUDO;

	/* Find spot in the private pointer array for new interface. */
	for (pvt_idx = 0; pvt_idx < pri->numchans; ++pvt_idx) {
		if (!pri->pvts[pvt_idx]) {
			break;
		}
	}
	if (pri->numchans == pvt_idx) {
		if (ARRAY_LEN(pri->pvts) <= pvt_idx) {
			ast_log(LOG_ERROR, "Unable to add a no-B-channel interface!\n");
			return -1;
		}

		/* Add new spot to the private pointer array. */
		pri->pvts[pvt_idx] = NULL;
		++pri->numchans;
	}

	pvt = ast_calloc(1, sizeof(*pvt));
	if (!pvt) {
		return -1;
	}
	pvt->cc_params = ast_cc_config_params_init();
	if (!pvt->cc_params) {
		ast_free(pvt);
		return -1;
	}
	ast_mutex_init(&pvt->lock);
	for (idx = 0; idx < ARRAY_LEN(pvt->subs); ++idx) {
		pvt->subs[idx].dfd = -1;
	}
	pvt->buf_no = dahdi_pseudo_parms.buf_no;
	pvt->buf_policy = dahdi_pseudo_parms.buf_policy;
	pvt->faxbuf_no = dahdi_pseudo_parms.faxbuf_no;
	pvt->faxbuf_policy = dahdi_pseudo_parms.faxbuf_policy;

	chan = sig_pri_chan_new(pvt, pri, 0, 0, 0);
	if (!chan) {
		destroy_dahdi_pvt(pvt);
		return -1;
	}
	chan->no_b_channel = 1;

	/*
	 * Pseudo channel companding law.
	 * Needed for outgoing call waiting calls.
	 * XXX May need to make this determined by switchtype or user option.
	 */
	pvt->law_default = DAHDI_LAW_ALAW;

	pvt->sig = pri->sig;
	pvt->outsigmod = -1;
	pvt->pri = pri;
	pvt->sig_pvt = chan;
	pri->pvts[pvt_idx] = chan;

	pvt->subs[SUB_REAL].dfd = dahdi_open("/dev/dahdi/pseudo");
	if (pvt->subs[SUB_REAL].dfd < 0) {
		ast_log(LOG_ERROR, "Unable to open no B channel interface pseudo channel: %s\n",
			strerror(errno));
		destroy_dahdi_pvt(pvt);
		return -1;
	}
	memset(&bi, 0, sizeof(bi));
	res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
	if (!res) {
		pvt->bufsize = bi.bufsize;
		bi.txbufpolicy = pvt->buf_policy;
		bi.rxbufpolicy = pvt->buf_policy;
		bi.numbufs = pvt->buf_no;
		res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
		if (res < 0) {
			ast_log(LOG_WARNING,
				"Unable to set buffer policy on no B channel interface: %s\n",
				strerror(errno));
		}
	} else
		ast_log(LOG_WARNING,
			"Unable to check buffer policy on no B channel interface: %s\n",
			strerror(errno));

	--nobch_channel;
	if (CHAN_PSEUDO < nobch_channel) {
		nobch_channel = CHAN_PSEUDO - 1;
	}
	pvt->channel = nobch_channel;
	pvt->span = pri->span;
	chan->channel = pvt->channel;

	dahdi_nobch_insert(pri, pvt);

	return pvt_idx;
}
#endif	/* defined(HAVE_PRI) */

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

	p = ast_malloc(sizeof(*p));
	if (!p) {
		return NULL;
	}
	*p = *src;

	/* Must deep copy the cc_params. */
	p->cc_params = ast_cc_config_params_init();
	if (!p->cc_params) {
		ast_free(p);
		return NULL;
	}
	ast_cc_copy_config_params(p->cc_params, src->cc_params);

	p->which_iflist = DAHDI_IFLIST_NONE;
	p->next = NULL;
	p->prev = NULL;
	ast_mutex_init(&p->lock);
	p->subs[SUB_REAL].dfd = dahdi_open("/dev/dahdi/pseudo");
	if (p->subs[SUB_REAL].dfd < 0) {
		ast_log(LOG_ERROR, "Unable to dup channel: %s\n", strerror(errno));
		destroy_dahdi_pvt(p);
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
	p->destroy = 1;
	dahdi_iflist_insert(p);
	return p;
}

struct dahdi_starting_point {
	/*! Group matching mask.  Zero if not specified. */
	ast_group_t groupmatch;
	/*! DAHDI channel to match with.  -1 if not specified. */
	int channelmatch;
	/*! Round robin saved search location index. (Valid if roundrobin TRUE) */
	int rr_starting_point;
	/*! ISDN span where channels can be picked (Zero if not specified) */
	int span;
	/*! Analog channel distinctive ring cadance index. */
	int cadance;
	/*! Dialing option. c/r/d if present and valid. */
	char opt;
	/*! TRUE if to search the channel list backwards. */
	char backwards;
	/*! TRUE if search is done with round robin sequence. */
	char roundrobin;
};
static struct dahdi_pvt *determine_starting_point(const char *data, struct dahdi_starting_point *param)
{
	char *dest;
	char *s;
	int x;
	int res = 0;
	struct dahdi_pvt *p;
	char *subdir = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		//AST_APP_ARG(ext);	/* extension token */
		//AST_APP_ARG(opts);	/* options token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	/*
	 * data is ---v
	 * Dial(DAHDI/pseudo[/extension[/options]])
	 * Dial(DAHDI/<channel#>[c|r<cadance#>|d][/extension[/options]])
	 * Dial(DAHDI/<subdir>!<channel#>[c|r<cadance#>|d][/extension[/options]])
	 * Dial(DAHDI/i<span>[/extension[/options]])
	 * Dial(DAHDI/[i<span>-](g|G|r|R)<group#(0-63)>[c|r<cadance#>|d][/extension[/options]])
	 *
	 * i - ISDN span channel restriction.
	 *     Used by CC to ensure that the CC recall goes out the same span.
	 *     Also to make ISDN channel names dialable when the sequence number
	 *     is stripped off.  (Used by DTMF attended transfer feature.)
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

	if (data) {
		dest = ast_strdupa(data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	AST_NONSTANDARD_APP_ARGS(args, dest, '/');
	if (!args.argc || ast_strlen_zero(args.group)) {
		ast_log(LOG_WARNING, "No channel/group specified\n");
		return NULL;
	}

	/* Initialize the output parameters */
	memset(param, 0, sizeof(*param));
	param->channelmatch = -1;

	if (strchr(args.group, '!') != NULL) {
		char *prev = args.group;
		while ((s = strchr(prev, '!')) != NULL) {
			*s++ = '/';
			prev = s;
		}
		*(prev - 1) = '\0';
		subdir = args.group;
		args.group = prev;
	} else if (args.group[0] == 'i') {
		/* Extract the ISDN span channel restriction specifier. */
		res = sscanf(args.group + 1, "%30d", &x);
		if (res < 1) {
			ast_log(LOG_WARNING, "Unable to determine ISDN span for data %s\n", data);
			return NULL;
		}
		param->span = x;

		/* Remove the ISDN span channel restriction specifier. */
		s = strchr(args.group, '-');
		if (!s) {
			/* Search all groups since we are ISDN span restricted. */
			return iflist;
		}
		args.group = s + 1;
		res = 0;
	}
	if (toupper(args.group[0]) == 'G' || toupper(args.group[0])=='R') {
		/* Retrieve the group number */
		s = args.group + 1;
		res = sscanf(s, "%30d%1c%30d", &x, &param->opt, &param->cadance);
		if (res < 1) {
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", data);
			return NULL;
		}
		param->groupmatch = ((ast_group_t) 1 << x);

		if (toupper(args.group[0]) == 'G') {
			if (args.group[0] == 'G') {
				param->backwards = 1;
				p = ifend;
			} else
				p = iflist;
		} else {
			if (ARRAY_LEN(round_robin) <= x) {
				ast_log(LOG_WARNING, "Round robin index %d out of range for data %s\n",
					x, data);
				return NULL;
			}
			if (args.group[0] == 'R') {
				param->backwards = 1;
				p = round_robin[x] ? round_robin[x]->prev : ifend;
				if (!p)
					p = ifend;
			} else {
				p = round_robin[x] ? round_robin[x]->next : iflist;
				if (!p)
					p = iflist;
			}
			param->roundrobin = 1;
			param->rr_starting_point = x;
		}
	} else {
		s = args.group;
		if (!strcasecmp(s, "pseudo")) {
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
			param->channelmatch = x;
		} else {
			res = sscanf(s, "%30d%1c%30d", &x, &param->opt, &param->cadance);
			if (res < 1) {
				ast_log(LOG_WARNING, "Unable to determine channel for data %s\n", data);
				return NULL;
			} else {
				param->channelmatch = x;
			}
		}
		if (subdir) {
			char path[PATH_MAX];
			struct stat stbuf;

			snprintf(path, sizeof(path), "/dev/dahdi/%s/%d",
					subdir, param->channelmatch);
			if (stat(path, &stbuf) < 0) {
				ast_log(LOG_WARNING, "stat(%s) failed: %s\n",
						path, strerror(errno));
				return NULL;
			}
			if (!S_ISCHR(stbuf.st_mode)) {
				ast_log(LOG_ERROR, "%s: Not a character device file\n",
						path);
				return NULL;
			}
			param->channelmatch = minor(stbuf.st_rdev);
		}

		p = iflist;
	}

	if (param->opt == 'r' && res < 3) {
		ast_log(LOG_WARNING, "Distinctive ring missing identifier in '%s'\n", data);
		param->opt = '\0';
	}

	return p;
}

static struct ast_channel *dahdi_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
{
	int callwait = 0;
	struct dahdi_pvt *p;
	struct ast_channel *tmp = NULL;
	struct dahdi_pvt *exitpvt;
	int channelmatched = 0;
	int groupmatched = 0;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int transcapdigital = 0;
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
	struct dahdi_starting_point start;
	struct ast_callid *callid = NULL;
	int callid_created = ast_callid_threadstorage_auto(&callid);

	ast_mutex_lock(&iflock);
	p = determine_starting_point(data, &start);
	if (!p) {
		/* We couldn't determine a starting point, which likely means badly-formatted channel name. Abort! */
		ast_mutex_unlock(&iflock);
		ast_callid_threadstorage_auto_clean(callid, callid_created);
		return NULL;
	}

	/* Search for an unowned channel */
	exitpvt = p;
	while (p && !tmp) {
		if (start.roundrobin)
			round_robin[start.rr_starting_point] = p;

		if (is_group_or_channel_match(p, start.span, start.groupmatch, &groupmatched, start.channelmatch, &channelmatched)
			&& available(&p, channelmatched)) {
			ast_debug(1, "Using channel %d\n", p->channel);

			callwait = (p->owner != NULL);
#ifdef HAVE_OPENR2
			if (p->mfcr2) {
				ast_mutex_lock(&p->lock);
				if (p->mfcr2call) {
					ast_mutex_unlock(&p->lock);
					ast_debug(1, "Yay!, someone just beat us in the race for channel %d.\n", p->channel);
					goto next;
				}
				p->mfcr2call = 1;
				ast_mutex_unlock(&p->lock);
			}
#endif
			if (p->channel == CHAN_PSEUDO) {
				p = duplicate_pseudo(p);
				if (!p) {
					break;
				}
			}

			p->distinctivering = 0;
			/* Make special notes */
			switch (start.opt) {
			case '\0':
				/* No option present. */
				break;
			case 'c':
				/* Confirm answer */
				p->confirmanswer = 1;
				break;
			case 'r':
				/* Distinctive ring */
				p->distinctivering = start.cadance;
				break;
			case 'd':
#if defined(HAVE_PRI) || defined(HAVE_SS7)
				/* If this is an ISDN call, make it digital */
				transcapdigital = AST_TRANS_CAP_DIGITAL;
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
				break;
			default:
				ast_log(LOG_WARNING, "Unknown option '%c' in '%s'\n", start.opt, data);
				break;
			}

			p->outgoing = 1;
			if (analog_lib_handles(p->sig, p->radio, p->oprmode)) {
				tmp = analog_request(p->sig_pvt, &callwait, requestor);
#ifdef HAVE_PRI
			} else if (dahdi_sig_pri_lib_handles(p->sig)) {
				/*
				 * We already have the B channel reserved for this call.  We
				 * just need to make sure that dahdi_hangup() has completed
				 * cleaning up before continuing.
				 */
				ast_mutex_lock(&p->lock);
				ast_mutex_unlock(&p->lock);

				sig_pri_extract_called_num_subaddr(p->sig_pvt, data, p->dnid,
					sizeof(p->dnid));
				tmp = sig_pri_request(p->sig_pvt, SIG_PRI_DEFLAW, requestor, transcapdigital);
#endif
#if defined(HAVE_SS7)
			} else if (p->sig == SIG_SS7) {
				tmp = sig_ss7_request(p->sig_pvt, SIG_SS7_DEFLAW, requestor, transcapdigital);
#endif	/* defined(HAVE_SS7) */
			} else {
				tmp = dahdi_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, requestor ? ast_channel_linkedid(requestor) : "", callid);
			}
			if (!tmp) {
				p->outgoing = 0;
#if defined(HAVE_PRI)
				switch (p->sig) {
				case SIG_PRI_LIB_HANDLE_CASES:
#if defined(HAVE_PRI_CALL_WAITING)
					if (((struct sig_pri_chan *) p->sig_pvt)->is_call_waiting) {
						((struct sig_pri_chan *) p->sig_pvt)->is_call_waiting = 0;
						ast_atomic_fetchadd_int(&p->pri->num_call_waiting_calls, -1);
					}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
					/*
					 * This should be the last thing to clear when we are done with
					 * the channel.
					 */
					((struct sig_pri_chan *) p->sig_pvt)->allocated = 0;
					break;
				default:
					break;
				}
#endif	/* defined(HAVE_PRI) */
			} else {
				snprintf(p->dialstring, sizeof(p->dialstring), "DAHDI/%s", data);
			}
			break;
		}
#ifdef HAVE_OPENR2
next:
#endif
		if (start.backwards) {
			p = p->prev;
			if (!p)
				p = ifend;
		} else {
			p = p->next;
			if (!p)
				p = iflist;
		}
		/* stop when you roll to the one that we started from */
		if (p == exitpvt)
			break;
	}
	ast_mutex_unlock(&iflock);
	restart_monitor();
	if (cause && !tmp) {
		if (callwait || channelmatched) {
			*cause = AST_CAUSE_BUSY;
		} else if (groupmatched) {
			*cause = AST_CAUSE_CONGESTION;
		} else {
			/*
			 * We did not match any channel requested.
			 * Dialplan error requesting non-existant channel?
			 */
		}
	}

	ast_callid_threadstorage_auto_clean(callid, callid_created);
	return tmp;
}

/*!
 * \internal
 * \brief Determine the device state for a given DAHDI device if we can.
 * \since 1.8
 *
 * \param data DAHDI device name after "DAHDI/".
 *
 * \retval device_state enum ast_device_state value.
 * \retval AST_DEVICE_UNKNOWN if we could not determine the device's state.
 */
static int dahdi_devicestate(const char *data)
{
#if defined(HAVE_PRI)
	const char *device;
	unsigned span;
	int res;

	device = data;

	if (*device != 'I') {
		/* The request is not for an ISDN span device. */
		return AST_DEVICE_UNKNOWN;
	}
	res = sscanf(device, "I%30u", &span);
	if (res != 1 || !span || NUM_SPANS < span) {
		/* Bad format for ISDN span device name. */
		return AST_DEVICE_UNKNOWN;
	}
	device = strchr(device, '/');
	if (!device) {
		/* Bad format for ISDN span device name. */
		return AST_DEVICE_UNKNOWN;
	}

	/*
	 * Since there are currently no other span devstate's defined,
	 * it must be congestion.
	 */
#if defined(THRESHOLD_DEVSTATE_PLACEHOLDER)
	++device;
	if (!strcmp(device, "congestion"))
#endif	/* defined(THRESHOLD_DEVSTATE_PLACEHOLDER) */
	{
		return pris[span - 1].pri.congestion_devstate;
	}
#if defined(THRESHOLD_DEVSTATE_PLACEHOLDER)
	else if (!strcmp(device, "threshold")) {
		return pris[span - 1].pri.threshold_devstate;
	}
	return AST_DEVICE_UNKNOWN;
#endif	/* defined(THRESHOLD_DEVSTATE_PLACEHOLDER) */
#else
	return AST_DEVICE_UNKNOWN;
#endif	/* defined(HAVE_PRI) */
}

/*!
 * \brief Callback made when dial failed to get a channel out of dahdi_request().
 * \since 1.8
 *
 * \param inbound Incoming asterisk channel.
 * \param dest Same dial string passed to dahdi_request().
 * \param callback Callback into CC core to announce a busy channel available for CC.
 *
 * \details
 * This callback acts like a forked dial with all prongs of the fork busy.
 * Essentially, for each channel that could have taken the call, indicate that
 * it is busy.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dahdi_cc_callback(struct ast_channel *inbound, const char *dest, ast_cc_callback_fn callback)
{
	struct dahdi_pvt *p;
	struct dahdi_pvt *exitpvt;
	struct dahdi_starting_point start;
	int groupmatched = 0;
	int channelmatched = 0;

	ast_mutex_lock(&iflock);
	p = determine_starting_point(dest, &start);
	if (!p) {
		ast_mutex_unlock(&iflock);
		return -1;
	}
	exitpvt = p;
	for (;;) {
		if (is_group_or_channel_match(p, start.span, start.groupmatch, &groupmatched, start.channelmatch, &channelmatched)) {
			/* We found a potential match. call the callback */
			struct ast_str *device_name;
			char *dash;
			const char *monitor_type;
			char dialstring[AST_CHANNEL_NAME];
			char full_device_name[AST_CHANNEL_NAME];

			switch (ast_get_cc_monitor_policy(p->cc_params)) {
			case AST_CC_MONITOR_NEVER:
				break;
			case AST_CC_MONITOR_NATIVE:
			case AST_CC_MONITOR_ALWAYS:
			case AST_CC_MONITOR_GENERIC:
#if defined(HAVE_PRI)
				if (dahdi_sig_pri_lib_handles(p->sig)) {
					/*
					 * ISDN is in a trunk busy condition so we need to monitor
					 * the span congestion device state.
					 */
					snprintf(full_device_name, sizeof(full_device_name),
						"DAHDI/I%d/congestion", p->pri->span);
				} else
#endif	/* defined(HAVE_PRI) */
				{
#if defined(HAVE_PRI)
					device_name = create_channel_name(p, 1, "");
#else
					device_name = create_channel_name(p);
#endif	/* defined(HAVE_PRI) */
					snprintf(full_device_name, sizeof(full_device_name), "DAHDI/%s",
						device_name ? ast_str_buffer(device_name) : "");
					ast_free(device_name);
					/*
					 * The portion after the '-' in the channel name is either a random
					 * number, a sequence number, or a subchannel number. None are
					 * necessary so strip them off.
					 */
					dash = strrchr(full_device_name, '-');
					if (dash) {
						*dash = '\0';
					}
				}
				snprintf(dialstring, sizeof(dialstring), "DAHDI/%s", dest);

				/*
				 * Analog can only do generic monitoring.
				 * ISDN is in a trunk busy condition and any "device" is going
				 * to be busy until a B channel becomes available.  The generic
				 * monitor can do this task.
				 */
				monitor_type = AST_CC_GENERIC_MONITOR_TYPE;
				callback(inbound,
#if defined(HAVE_PRI)
					p->pri ? p->pri->cc_params : p->cc_params,
#else
					p->cc_params,
#endif	/* defined(HAVE_PRI) */
					monitor_type, full_device_name, dialstring, NULL);
				break;
			}
		}
		p = start.backwards ? p->prev : p->next;
		if (!p) {
			p = start.backwards ? ifend : iflist;
		}
		if (p == exitpvt) {
			break;
		}
	}
	ast_mutex_unlock(&iflock);
	return 0;
}

#if defined(HAVE_SS7)
static void dahdi_ss7_message(struct ss7 *ss7, char *s)
{
	int i;

	if (ss7) {
		for (i = 0; i < NUM_SPANS; i++) {
			if (linksets[i].ss7.ss7 == ss7) {
				ast_verbose_callid(NULL, "[%d] %s", i + 1, s);
				return;
			}
		}
	}
	ast_verbose_callid(NULL, "%s", s);
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static void dahdi_ss7_error(struct ss7 *ss7, char *s)
{
	int i;

	if (ss7) {
		for (i = 0; i < NUM_SPANS; i++) {
			if (linksets[i].ss7.ss7 == ss7) {
				ast_log_callid(LOG_ERROR, NULL, "[%d] %s", i + 1, s);
				return;
			}
		}
	}
	ast_log_callid(LOG_ERROR, NULL, "%s", s);
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
	struct pollfd pollers[ARRAY_LEN(mfcr2->pvts)];
	int res = 0;
	int i = 0;
	int oldstate = 0;
	int quit_loop = 0;
	int maxsleep = 20;
	int was_idle = 0;
	int pollsize = 0;
	/* now that we're ready to get calls, unblock our side and
	   get current line state */
	for (i = 0; i < mfcr2->numchans; i++) {
		openr2_chan_set_idle(mfcr2->pvts[i]->r2chan);
		openr2_chan_handle_cas(mfcr2->pvts[i]->r2chan);
	}
	while (1) {
		/* we trust here that the mfcr2 channel list will not ever change once
		   the module is loaded */
		pollsize = 0;
		for (i = 0; i < mfcr2->numchans; i++) {
			pollers[i].revents = 0;
			pollers[i].events = 0;
			if (mfcr2->pvts[i]->owner) {
				continue;
			}
			if (!mfcr2->pvts[i]->r2chan) {
				ast_debug(1, "Wow, no r2chan on channel %d\n", mfcr2->pvts[i]->channel);
				quit_loop = 1;
				break;
			}
			openr2_chan_enable_read(mfcr2->pvts[i]->r2chan);
			pollers[i].events = POLLIN | POLLPRI;
			pollers[i].fd = mfcr2->pvts[i]->subs[SUB_REAL].dfd;
			pollsize++;
		}
		if (quit_loop) {
			break;
		}
		if (pollsize == 0) {
			if (!was_idle) {
				ast_debug(1, "Monitor thread going idle since everybody has an owner\n");
				was_idle = 1;
			}
			poll(NULL, 0, maxsleep);
			continue;
		}
		was_idle = 0;
		/* probably poll() is a valid cancel point, lets just be on the safe side
		   by calling pthread_testcancel */
		pthread_testcancel();
		res = poll(pollers, mfcr2->numchans, maxsleep);
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
static void dahdi_pri_message(struct pri *pri, char *s)
{
	int x;
	int y;
	int dchan = -1;
	int span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < SIG_PRI_NUM_DCHANS; y++) {
				if (pris[x].pri.dchans[y]) {
					dchancount++;
				}

				if (pris[x].pri.dchans[y] == pri) {
					dchan = y;
				}
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if (-1 < span) {
			if (1 < dchancount) {
				ast_verbose_callid(NULL, "[PRI Span: %d D-Channel: %d] %s", span + 1, dchan, s);
			} else {
				ast_verbose_callid(NULL, "PRI Span: %d %s", span + 1, s);
			}
		} else {
			ast_verbose_callid(NULL, "PRI Span: ? %s", s);
		}
	} else {
		ast_verbose_callid(NULL, "PRI Span: ? %s", s);
	}

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0) {
		if (write(pridebugfd, s, strlen(s)) < 0) {
			ast_log_callid(LOG_WARNING, NULL, "write() failed: %s\n", strerror(errno));
		}
	}

	ast_mutex_unlock(&pridebugfdlock);
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static void dahdi_pri_error(struct pri *pri, char *s)
{
	int x;
	int y;
	int dchan = -1;
	int span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < SIG_PRI_NUM_DCHANS; y++) {
				if (pris[x].pri.dchans[y]) {
					dchancount++;
				}

				if (pris[x].pri.dchans[y] == pri) {
					dchan = y;
				}
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if (-1 < span) {
			if (1 < dchancount) {
				ast_log_callid(LOG_ERROR, NULL, "[PRI Span: %d D-Channel: %d] %s", span + 1, dchan, s);
			} else {
				ast_log_callid(LOG_ERROR, NULL, "PRI Span: %d %s", span + 1, s);
			}
		} else {
			ast_log_callid(LOG_ERROR, NULL, "PRI Span: ? %s", s);
		}
	} else {
		ast_log_callid(LOG_ERROR, NULL, "PRI Span: ? %s", s);
	}

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0) {
		if (write(pridebugfd, s, strlen(s)) < 0) {
			ast_log_callid(LOG_WARNING, NULL, "write() failed: %s\n", strerror(errno));
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

	for (i = 0; i < SIG_PRI_NUM_DCHANS; i++) {
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
			if (ast_asprintf(&ret, "%d", span + 1) < 0) {	/* user indexes start from 1 */
				ret = NULL;
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
	int debugmask = 0;
	int level = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "pri set debug {on|off|hex|intense|0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15} span";
		e->usage =
			"Usage: pri set debug {<level>|on|off|hex|intense} span <span>\n"
			"       Enables debugging on a given PRI span\n"
			"	Level is a bitmap of the following values:\n"
			"	1 General debugging incl. state changes\n"
			"	2 Decoded Q.931 messages\n"
			"	4 Decoded Q.921 messages\n"
			"	8 Raw hex dumps of Q.921 frames\n"
			"       on - equivalent to 3\n"
			"       hex - equivalent to 8\n"
			"       intense - equivalent to 15\n";
		return NULL;
	case CLI_GENERATE:
		return complete_span_4(a->line, a->word, a->pos, a->n);
	}
	if (a->argc < 6) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "on")) {
		level = 3;
	} else if (!strcasecmp(a->argv[3], "off")) {
		level = 0;
	} else if (!strcasecmp(a->argv[3], "intense")) {
		level = 15;
	} else if (!strcasecmp(a->argv[3], "hex")) {
		level = 8;
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

	if (level & 1) debugmask |= SIG_PRI_DEBUG_NORMAL;
	if (level & 2) debugmask |= PRI_DEBUG_Q931_DUMP;
	if (level & 4) debugmask |= PRI_DEBUG_Q921_DUMP;
	if (level & 8) debugmask |= PRI_DEBUG_Q921_RAW;

	/* Set debug level in libpri */
	for (x = 0; x < SIG_PRI_NUM_DCHANS; x++) {
		if (pris[span - 1].pri.dchans[x]) {
			pri_set_debug(pris[span - 1].pri.dchans[x], debugmask);
		}
	}
	if (level == 0) {
		/* Close the debugging file if it's set */
		ast_mutex_lock(&pridebugfdlock);
		if (0 <= pridebugfd) {
			close(pridebugfd);
			pridebugfd = -1;
			ast_cli(a->fd, "Disabled PRI debug output to file '%s'\n",
				pridebugfilename);
		}
		ast_mutex_unlock(&pridebugfdlock);
	}
	pris[span - 1].pri.debug = (level) ? 1 : 0;
	ast_cli(a->fd, "%s debugging on span %d\n", (level) ? "Enabled" : "Disabled", span);
	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_SERVICE_MESSAGES)
static char *handle_pri_service_generic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a, int changestatus)
{
	unsigned *why;
	int channel;
	int trunkgroup;
	int x, y, fd = a->fd;
	int interfaceid = 0;
	char db_chan_name[20], db_answer[5];
	struct dahdi_pvt *tmp;
	struct dahdi_pri *pri;

	if (a->argc < 5 || a->argc > 6)
		return CLI_SHOWUSAGE;
	if (strchr(a->argv[4], ':')) {
		if (sscanf(a->argv[4], "%30d:%30d", &trunkgroup, &channel) != 2)
			return CLI_SHOWUSAGE;
		if ((trunkgroup < 1) || (channel < 1))
			return CLI_SHOWUSAGE;
		pri = NULL;
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].pri.trunkgroup == trunkgroup) {
				pri = pris + x;
				break;
			}
		}
		if (!pri) {
			ast_cli(fd, "No such trunk group %d\n", trunkgroup);
			return CLI_FAILURE;
		}
	} else
		channel = atoi(a->argv[4]);

	if (a->argc == 6)
		interfaceid = atoi(a->argv[5]);

	/* either servicing a D-Channel */
	for (x = 0; x < NUM_SPANS; x++) {
		for (y = 0; y < SIG_PRI_NUM_DCHANS; y++) {
			if (pris[x].dchannels[y] == channel) {
				pri = pris + x;
				if (pri->pri.enable_service_message_support) {
					ast_mutex_lock(&pri->pri.lock);
					pri_maintenance_service(pri->pri.pri, interfaceid, -1, changestatus);
					ast_mutex_unlock(&pri->pri.lock);
				} else {
					ast_cli(fd,
						"\n\tThis operation has not been enabled in chan_dahdi.conf, set 'service_message_support=yes' to use this operation.\n"
						"\tNote only 4ESS, 5ESS, and NI2 switch types are supported.\n\n");
				}
				return CLI_SUCCESS;
			}
		}
	}

	/* or servicing a B-Channel */
	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (tmp->pri && tmp->channel == channel) {
			ast_mutex_unlock(&iflock);
			ast_mutex_lock(&tmp->pri->lock);
			if (!tmp->pri->enable_service_message_support) {
				ast_mutex_unlock(&tmp->pri->lock);
				ast_cli(fd,
					"\n\tThis operation has not been enabled in chan_dahdi.conf, set 'service_message_support=yes' to use this operation.\n"
					"\tNote only 4ESS, 5ESS, and NI2 switch types are supported.\n\n");
				return CLI_SUCCESS;
			}
			snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, tmp->span, channel);
			why = &((struct sig_pri_chan *) tmp->sig_pvt)->service_status;
			switch(changestatus) {
			case 0: /* enable */
				/* Near end wants to be in service now. */
				ast_db_del(db_chan_name, SRVST_DBKEY);
				*why &= ~SRVST_NEAREND;
				if (*why) {
					snprintf(db_answer, sizeof(db_answer), "%s:%u", SRVST_TYPE_OOS, *why);
					ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
				} else {
					dahdi_pri_update_span_devstate(tmp->pri);
				}
				break;
			/* case 1:  -- loop */
			case 2: /* disable */
				/* Near end wants to be out-of-service now. */
				ast_db_del(db_chan_name, SRVST_DBKEY);
				*why |= SRVST_NEAREND;
				snprintf(db_answer, sizeof(db_answer), "%s:%u", SRVST_TYPE_OOS, *why);
				ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
				dahdi_pri_update_span_devstate(tmp->pri);
				break;
			/* case 3:  -- continuity */
			/* case 4:  -- shutdown */
			default:
				ast_log(LOG_WARNING, "Unsupported changestatus: '%d'\n", changestatus);
				break;
			}
			pri_maintenance_bservice(tmp->pri->pri, tmp->sig_pvt, changestatus);
			ast_mutex_unlock(&tmp->pri->lock);
			return CLI_SUCCESS;
		}
	}
	ast_mutex_unlock(&iflock);

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
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pri show channels";
		e->usage =
			"Usage: pri show channels\n"
			"       Displays PRI channel information such as the current mapping\n"
			"       of DAHDI B channels to Asterisk channel names and which calls\n"
			"       are on hold or call-waiting.  Calls on hold or call-waiting\n"
			"       are not associated with any B channel.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	sig_pri_cli_show_channels_header(a->fd);
	for (span = 0; span < NUM_SPANS; ++span) {
		if (pris[span].pri.pri) {
			sig_pri_cli_show_channels(a->fd, &pris[span].pri);
		}
	}
	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
static char *handle_pri_show_spans(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pri show spans";
		e->usage =
			"Usage: pri show spans\n"
			"       Displays PRI span information\n";
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
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
/*!
 * \internal
 * \brief Destroy a D-Channel of a PRI span
 * \since 12
 *
 * \param pri the pri span
 *
 * \return TRUE if the span was valid and we attempted destroying.
 *
 * Shuts down a span and destroys its D-Channel. Further destruction
 * of the B-channels using dahdi_destroy_channel() would probably be required
 * for the B-Channels.
 */
static int pri_destroy_dchan(struct sig_pri_span *pri)
{
	int i;
	struct dahdi_pri* dahdi_pri;

	if (!pri->master || (pri->master == AST_PTHREADT_NULL)) {
		return 0;
	}
	pthread_cancel(pri->master);
	pthread_join(pri->master, NULL);

	/* The 'struct dahdi_pri' that contains our 'struct sig_pri_span' */
	dahdi_pri = container_of(pri, struct dahdi_pri, pri);
	for (i = 0; i < SIG_PRI_NUM_DCHANS; i++) {
		ast_debug(4, "closing pri_fd %d\n", i);
		dahdi_close_pri_fd(dahdi_pri, i);
	}
	pri->pri = NULL;
	ast_debug(1, "PRI span %d destroyed\n", pri->span);
	return 1;
}

static char *handle_pri_destroy_span(struct ast_cli_entry *e, int cmd,
		struct ast_cli_args *a)
{
	int span;
	int i;
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pri destroy span";
		e->usage =
			"Usage: pri destroy span <span>\n"
			"       Destorys D-channel of span and its B-channels.\n"
			"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_span_4(a->line, a->word, a->pos, a->n);
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}
	res = sscanf(a->argv[3], "%30d", &span);
	if ((res != 1) || span < 1 || span > NUM_SPANS) {
		ast_cli(a->fd,
			"Invalid span '%s'.  Should be a number from %d to %d\n",
			a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!pris[span - 1].pri.pri) {
		ast_cli(a->fd, "No PRI running on span %d\n", span);
		return CLI_SUCCESS;
	}

	for (i = 0; i < pris[span - 1].pri.numchans; i++) {
		int channel;
		struct sig_pri_chan *pvt = pris[span - 1].pri.pvts[i];

		if (!pvt) {
			continue;
		}
		channel = pvt->channel;
		ast_debug(2, "About to destroy B-channel %d.\n", channel);
		dahdi_destroy_channel_bynum(channel);
	}
	ast_debug(2, "About to destroy D-channel of span %d.\n", span);
	pri_destroy_dchan(&pris[span - 1].pri);

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
	int debug;

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
			for (x = 0; x < SIG_PRI_NUM_DCHANS; x++) {
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
		ast_cli(a->fd, "No PRI running\n");
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
#if defined(HAVE_PRI_SERVICE_MESSAGES)
 	AST_CLI_DEFINE(handle_pri_service_enable_channel, "Return a channel to service"),
 	AST_CLI_DEFINE(handle_pri_service_disable_channel, "Remove a channel from service"),
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
	AST_CLI_DEFINE(handle_pri_show_channels, "Displays PRI channel information"),
	AST_CLI_DEFINE(handle_pri_show_spans, "Displays PRI span information"),
	AST_CLI_DEFINE(handle_pri_show_span, "Displays PRI span information"),
	AST_CLI_DEFINE(handle_pri_destroy_span, "Destroy a PRI span"),
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
		p->mfcr2call = 0;
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
				ast_verbose("Softhanging up on %s\n", ast_channel_name(p->owner));
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
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
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
		if (linksets[i].ss7.master && (linksets[i].ss7.master != AST_PTHREADT_NULL)) {
			cancel_code = pthread_cancel(linksets[i].ss7.master);
			pthread_kill(linksets[i].ss7.master, SIGURG);
			ast_debug(4, "Waiting to join thread of span %d with pid=%p, cancel_code=%d\n", i, (void *) linksets[i].ss7.master, cancel_code);
			pthread_join(linksets[i].ss7.master, NULL);
			ast_debug(4, "Joined thread of span %d\n", i);
		}
	}
#endif	/* defined(HAVE_SS7) */

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

		ast_mutex_lock(&iflock);
		for (p = iflist; p; p = p->next) {
			if (p->owner) {
				/* important to create an event for dahdi_wait_event to register so that all analog_ss_threads terminate */
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
			}
		}
		ast_mutex_unlock(&iflock);
		ast_cond_wait(&ss_thread_complete, &ss_thread_lock);
	}

	/* ensure any created channels before monitor threads were stopped are hungup */
	dahdi_softhangup_all();
	ast_verb(4, "Final softhangup of all DAHDI channels complete.\n");
	destroy_all_channels();
	memset(round_robin, 0, sizeof(round_robin));
	ast_debug(1, "Channels destroyed. Now re-reading config. %d active channels remaining.\n", ast_active_channels());

	ast_mutex_unlock(&monlock);

#ifdef HAVE_PRI
	for (i = 0; i < NUM_SPANS; i++) {
		for (j = 0; j < SIG_PRI_NUM_DCHANS; j++)
			dahdi_close_pri_fd(&(pris[i]), j);
	}

	memset(pris, 0, sizeof(pris));
	for (i = 0; i < NUM_SPANS; i++) {
		sig_pri_init_pri(&pris[i].pri);
	}
	pri_set_error(dahdi_pri_error);
	pri_set_message(dahdi_pri_message);
#endif
#if defined(HAVE_SS7)
	for (i = 0; i < NUM_SPANS; i++) {
		for (j = 0; j < SIG_SS7_NUM_DCHANS; j++)
			dahdi_close_ss7_fd(&(linksets[i]), j);
	}

	memset(linksets, 0, sizeof(linksets));
	for (i = 0; i < NUM_SPANS; i++) {
		sig_ss7_init_linkset(&linksets[i].ss7);
	}
	ss7_set_error(dahdi_ss7_error);
	ss7_set_message(dahdi_ss7_message);
#endif	/* defined(HAVE_SS7) */

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
#define FORMAT "%7s %-15.15s %-15.15s %-10.10s %-20.20s %-10.10s %-10.10s %-32.32s\n"
#define FORMAT2 "%7s %-15.15s %-15.15s %-10.10s %-20.20s %-10.10s %-10.10s %-32.32s\n"
	ast_group_t targetnum = 0;
	int filtertype = 0;
	struct dahdi_pvt *tmp = NULL;
	char tmps[20] = "";
	char statestr[20] = "";
	char blockstr[20] = "";

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

	/* syntax: dahdi show channels [ group <group> | context <context> ] */

	if (!((a->argc == 3) || (a->argc == 5)))
		return CLI_SHOWUSAGE;

	if (a->argc == 5) {
		if (!strcasecmp(a->argv[3], "group")) {
			targetnum = atoi(a->argv[4]);
			if (63 < targetnum) {
				return CLI_SHOWUSAGE;
			}
			targetnum = ((ast_group_t) 1) << targetnum;
			filtertype = 1;
		} else if (!strcasecmp(a->argv[3], "context")) {
			filtertype = 2;
		}
	}

	ast_cli(a->fd, FORMAT2, "Chan", "Extension", "Context", "Language", "MOH Interpret", "Blocked", "State", "Description");
	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (filtertype) {
			switch(filtertype) {
			case 1: /* dahdi show channels group <group> */
				if (!(tmp->group & targetnum)) {
					continue;
				}
				break;
			case 2: /* dahdi show channels context <context> */
				if (strcasecmp(tmp->context, a->argv[4])) {
					continue;
				}
				break;
			default:
				break;
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

		ast_cli(a->fd, FORMAT, tmps, tmp->exten, tmp->context, tmp->language, tmp->mohinterpret, blockstr, statestr, tmp->description);
	}
	ast_mutex_unlock(&iflock);
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

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	channel = atoi(a->argv[3]);

	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (tmp->channel == channel) {
			ast_cli(a->fd, "Channel: %d\n", tmp->channel);
			ast_cli(a->fd, "Description: %s\n", tmp->description);
			ast_cli(a->fd, "File Descriptor: %d\n", tmp->subs[SUB_REAL].dfd);
			ast_cli(a->fd, "Span: %d\n", tmp->span);
			ast_cli(a->fd, "Extension: %s\n", tmp->exten);
			ast_cli(a->fd, "Dialing: %s\n", tmp->dialing ? "yes" : "no");
			ast_cli(a->fd, "Context: %s\n", tmp->context);
			ast_cli(a->fd, "Caller ID: %s\n", tmp->cid_num);
			ast_cli(a->fd, "Calling TON: %d\n", tmp->cid_ton);
#if defined(HAVE_PRI)
#if defined(HAVE_PRI_SUBADDR)
			ast_cli(a->fd, "Caller ID subaddress: %s\n", tmp->cid_subaddr);
#endif	/* defined(HAVE_PRI_SUBADDR) */
#endif	/* defined(HAVE_PRI) */
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
			ast_cli(a->fd, "Owner: %s\n", tmp->owner ? ast_channel_name(tmp->owner) : "<None>");
			ast_cli(a->fd, "Real: %s%s%s\n", tmp->subs[SUB_REAL].owner ? ast_channel_name(tmp->subs[SUB_REAL].owner) : "<None>", tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : "", tmp->subs[SUB_REAL].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Callwait: %s%s%s\n", tmp->subs[SUB_CALLWAIT].owner ? ast_channel_name(tmp->subs[SUB_CALLWAIT].owner) : "<None>", tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : "", tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Threeway: %s%s%s\n", tmp->subs[SUB_THREEWAY].owner ? ast_channel_name(tmp->subs[SUB_THREEWAY].owner) : "<None>", tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : "", tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "");
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
				ast_cli(a->fd, "    Busy Pattern: %d,%d,%d,%d\n", tmp->busy_cadence.pattern[0], tmp->busy_cadence.pattern[1], (tmp->busy_cadence.length == 4) ? tmp->busy_cadence.pattern[2] : 0, (tmp->busy_cadence.length == 4) ? tmp->busy_cadence.pattern[3] : 0);
			}
			ast_cli(a->fd, "TDD: %s\n", tmp->tdd ? "yes" : "no");
			ast_cli(a->fd, "Relax DTMF: %s\n", tmp->dtmfrelax ? "yes" : "no");
			ast_cli(a->fd, "Dialing/CallwaitCAS: %d/%d\n", tmp->dialing, tmp->callwaitcas);
			ast_cli(a->fd, "Default law: %s\n", tmp->law_default == DAHDI_LAW_MULAW ? "ulaw" : tmp->law_default == DAHDI_LAW_ALAW ? "alaw" : "unknown");
			ast_cli(a->fd, "Fax Handled: %s\n", tmp->faxhandled ? "yes" : "no");
			ast_cli(a->fd, "Pulse phone: %s\n", tmp->pulsedial ? "yes" : "no");
			ast_cli(a->fd, "Gains (RX/TX): %.2f/%.2f\n", tmp->rxgain, tmp->txgain);
			ast_cli(a->fd, "Dynamic Range Compression (RX/TX): %.2f/%.2f\n", tmp->rxdrc, tmp->txdrc);
			ast_cli(a->fd, "DND: %s\n", dahdi_dnd(tmp, -1) ? "yes" : "no");
			ast_cli(a->fd, "Echo Cancellation:\n");

			if (tmp->echocancel.head.tap_length) {
				ast_cli(a->fd, "\t%u taps\n", tmp->echocancel.head.tap_length);
				for (x = 0; x < tmp->echocancel.head.param_count; x++) {
					ast_cli(a->fd, "\t\t%s: %dd\n", tmp->echocancel.params[x].name, tmp->echocancel.params[x].value);
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
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 2
				ast_cli(a->fd, "MFC/R2 DTMF Dialing: %s\n", openr2_context_get_dtmf_dialing(r2context, NULL, NULL) ? "Yes" : "No");
				ast_cli(a->fd, "MFC/R2 DTMF Detection: %s\n", openr2_context_get_dtmf_detection(r2context) ? "Yes" : "No");
#endif
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
#if defined(HAVE_SS7)
			if (tmp->ss7) {
				struct sig_ss7_chan *chan = tmp->sig_pvt;

				ast_cli(a->fd, "CIC: %d\n", chan->cic);
			}
#endif	/* defined(HAVE_SS7) */
#ifdef HAVE_PRI
			if (tmp->pri) {
				struct sig_pri_chan *chan = tmp->sig_pvt;

				ast_cli(a->fd, "PRI Flags: ");
				if (chan->resetting != SIG_PRI_RESET_IDLE) {
					ast_cli(a->fd, "Resetting=%u ", chan->resetting);
				}
				if (chan->call)
					ast_cli(a->fd, "Call ");
				if (chan->allocated) {
					ast_cli(a->fd, "Allocated ");
				}
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
					ast_cli(a->fd, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, (unsigned)ci.confmode);
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
			ast_mutex_unlock(&iflock);
			return CLI_SUCCESS;
		}
	}
	ast_mutex_unlock(&iflock);

	ast_cli(a->fd, "Unable to find given channel %d\n", channel);
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
	ast_cli(a->fd, FORMAT2, "Description", "Alarms", "IRQ", "bpviol", "CRC", "Framing", "Coding", "Options", "LBO");

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
				s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" :
				s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "YEL" : "",
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
		e->command = "dahdi set hwgain {rx|tx}";
		e->usage =
			"Usage: dahdi set hwgain <rx|tx> <chan#> <gain>\n"
			"   Sets the hardware gain on a given channel.  Changes take effect\n"
			"   immediately whether the channel is in use or not.\n"
			"\n"
			"   <rx|tx> which direction do you want to change (relative to our module)\n"
			"   <chan num> is the channel number relative to the device\n"
			"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n"
			"\n"
			"   Please note:\n"
			"   * This is currently the only way to set hwgain by the channel driver.\n"
			"   * hwgain is only supportable by hardware with analog ports because\n"
			"     hwgain works on the analog side of an analog-digital conversion.\n";
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
	struct dahdi_pvt *tmp = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dahdi set swgain {rx|tx}";
		e->usage =
			"Usage: dahdi set swgain <rx|tx> <chan#> <gain>\n"
			"   Sets the software gain on a given channel and overrides the\n"
			"   value provided at module loadtime.  Changes take effect\n"
			"   immediately whether the channel is in use or not.\n"
			"\n"
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
	gain = atof(a->argv[5]);

	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {

		if (tmp->channel != channel)
			continue;

		if (tmp->subs[SUB_REAL].dfd == -1)
			break;

		if (tx)
			res = set_actual_txgain(tmp->subs[SUB_REAL].dfd, gain, tmp->txdrc, tmp->law);
		else
			res = set_actual_rxgain(tmp->subs[SUB_REAL].dfd, gain, tmp->rxdrc, tmp->law);

		if (res) {
			ast_cli(a->fd, "Unable to set the software gain for channel %d\n", channel);
			ast_mutex_unlock(&iflock);
			return CLI_FAILURE;
		}

		ast_cli(a->fd, "software %s gain set to %.1f on channel %d\n",
			tx ? "tx" : "rx", gain, channel);

		if (tx) {
			tmp->txgain = gain;
		} else {
			tmp->rxgain = gain;
		}
		break;
	}
	ast_mutex_unlock(&iflock);

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
			ast_log(LOG_WARNING, "I don't know how to handle transfer event with this: %d on channel %s\n",mode, ast_channel_name(p->owner));
		}
	}
	return 0;
}
static struct dahdi_pvt *find_channel(int channel)
{
	struct dahdi_pvt *p;

	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (p->channel == channel) {
			break;
		}
	}
	ast_mutex_unlock(&iflock);
	return p;
}

/*!
 * \internal
 * \brief Get private struct using given numeric channel string.
 *
 * \param channel Numeric channel number string get private struct.
 *
 * \retval pvt on success.
 * \retval NULL on error.
 */
static struct dahdi_pvt *find_channel_from_str(const char *channel)
{
	int chan_num;

	if (sscanf(channel, "%30d", &chan_num) != 1) {
		/* Not numeric string. */
		return NULL;
	}

	return find_channel(chan_num);
}

static int action_dahdidndon(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel_from_str(channel);
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	dahdi_dnd(p, 1);
	astman_send_ack(s, m, "DND Enabled");
	return 0;
}

static int action_dahdidndoff(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel_from_str(channel);
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	dahdi_dnd(p, 0);
	astman_send_ack(s, m, "DND Disabled");
	return 0;
}

static int action_transfer(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel_from_str(channel);
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if (!analog_lib_handles(p->sig, 0, 0)) {
		astman_send_error(s, m, "Channel signaling is not analog");
		return 0;
	}
	dahdi_fake_event(p,TRANSFER);
	astman_send_ack(s, m, "DAHDITransfer");
	return 0;
}

static int action_transferhangup(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p;
	const char *channel = astman_get_header(m, "DAHDIChannel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel_from_str(channel);
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if (!analog_lib_handles(p->sig, 0, 0)) {
		astman_send_error(s, m, "Channel signaling is not analog");
		return 0;
	}
	dahdi_fake_event(p,HANGUP);
	astman_send_ack(s, m, "DAHDIHangup");
	return 0;
}

static int action_dahdidialoffhook(struct mansession *s, const struct message *m)
{
	struct dahdi_pvt *p;
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
	p = find_channel_from_str(channel);
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if (!p->owner) {
		astman_send_error(s, m, "Channel does not have it's owner");
		return 0;
	}
	for (i = 0; i < strlen(number); i++) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = number[i] };
		dahdi_queue_frame(p, &f);
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
	int dahdichanquery;

	if (!dahdichannel || sscanf(dahdichannel, "%30d", &dahdichanquery) != 1) {
		/* Not numeric string. */
		dahdichanquery = -1;
	}

	astman_send_ack(s, m, "DAHDI channel status will follow");
	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	ast_mutex_lock(&iflock);

	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (tmp->channel > 0) {
			int alm;

			/* If a specific channel is queried for, only deliver status for that channel */
			if (dahdichanquery > 0 && tmp->channel != dahdichanquery)
				continue;

			alm = get_alarms(tmp);
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
					"Description: %s\r\n"
					"%s"
					"\r\n",
					tmp->channel,
					ast_channel_name(tmp->owner),
					ast_channel_uniqueid(tmp->owner),
					ast_channel_accountcode(tmp->owner),
					sig2str(tmp->sig),
					tmp->sig,
					tmp->context,
					dahdi_dnd(tmp, -1) ? "Enabled" : "Disabled",
					alarm2str(alm),
					tmp->description, idText);
			} else {
				astman_append(s,
					"Event: DAHDIShowChannels\r\n"
					"DAHDIChannel: %d\r\n"
					"Signalling: %s\r\n"
					"SignallingCode: %d\r\n"
					"Context: %s\r\n"
					"DND: %s\r\n"
					"Alarm: %s\r\n"
					"Description: %s\r\n"
					"%s"
					"\r\n",
					tmp->channel, sig2str(tmp->sig), tmp->sig,
					tmp->context,
					dahdi_dnd(tmp, -1) ? "Enabled" : "Disabled",
					alarm2str(alm),
					tmp->description, idText);
			}
		}
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

#if defined(HAVE_PRI)
static int action_prishowspans(struct mansession *s, const struct message *m)
{
	int count;
	int idx;
	int span_query;
	struct dahdi_pri *dspan;
	const char *id = astman_get_header(m, "ActionID");
	const char *span_str = astman_get_header(m, "Span");
	char action_id[256];
	const char *show_cmd = "PRIShowSpans";

	/* NOTE: Asking for span 0 gets all spans. */
	if (!ast_strlen_zero(span_str)) {
		span_query = atoi(span_str);
	} else {
		span_query = 0;
	}

	if (!ast_strlen_zero(id)) {
		snprintf(action_id, sizeof(action_id), "ActionID: %s\r\n", id);
	} else {
		action_id[0] = '\0';
	}

	astman_send_ack(s, m, "Span status will follow");

	count = 0;
	for (idx = 0; idx < ARRAY_LEN(pris); ++idx) {
		dspan = &pris[idx];

		/* If a specific span is asked for, only deliver status for that span. */
		if (0 < span_query && dspan->pri.span != span_query) {
			continue;
		}

		if (dspan->pri.pri) {
			count += sig_pri_ami_show_spans(s, show_cmd, &dspan->pri, dspan->dchannels,
				action_id);
		}
	}

	astman_append(s,
		"Event: %sComplete\r\n"
		"Items: %d\r\n"
		"%s"
		"\r\n",
		show_cmd,
		count,
		action_id);
	return 0;
}
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_SS7)
static int linkset_addsigchan(int sigchan)
{
	struct dahdi_ss7 *link;
	int res;
	int curfd;
	struct dahdi_params params;
	struct dahdi_bufferinfo bi;
	struct dahdi_spaninfo si;

	if (sigchan < 0) {
		ast_log(LOG_ERROR, "Invalid sigchan!\n");
		return -1;
	}
	if (cur_ss7type < 0) {
		ast_log(LOG_ERROR, "Unspecified or invalid ss7type\n");
		return -1;
	}
	if (cur_pointcode < 0) {
		ast_log(LOG_ERROR, "Unspecified pointcode!\n");
		return -1;
	}
	if (cur_adjpointcode < 0) {
		ast_log(LOG_ERROR, "Unspecified adjpointcode!\n");
		return -1;
	}
	if (cur_defaultdpc < 0) {
		ast_log(LOG_ERROR, "Unspecified defaultdpc!\n");
		return -1;
	}
	if (cur_networkindicator < 0) {
		ast_log(LOG_ERROR, "Invalid networkindicator!\n");
		return -1;
	}
	link = ss7_resolve_linkset(cur_linkset);
	if (!link) {
		ast_log(LOG_ERROR, "Invalid linkset number.  Must be between 1 and %d\n", NUM_SPANS + 1);
		return -1;
	}
	if (link->ss7.numsigchans >= SIG_SS7_NUM_DCHANS) {
		ast_log(LOG_ERROR, "Too many sigchans on linkset %d\n", cur_linkset);
		return -1;
	}

	curfd = link->ss7.numsigchans;

	/* Open signaling channel */
	link->ss7.fds[curfd] = open("/dev/dahdi/channel", O_RDWR, 0600);
	if (link->ss7.fds[curfd] < 0) {
		ast_log(LOG_ERROR, "Unable to open SS7 sigchan %d (%s)\n", sigchan,
			strerror(errno));
		return -1;
	}
	if (ioctl(link->ss7.fds[curfd], DAHDI_SPECIFY, &sigchan) == -1) {
		dahdi_close_ss7_fd(link, curfd);
		ast_log(LOG_ERROR, "Unable to specify SS7 sigchan %d (%s)\n", sigchan,
			strerror(errno));
		return -1;
	}

	/* Get signaling channel parameters */
	memset(&params, 0, sizeof(params));
	res = ioctl(link->ss7.fds[curfd], DAHDI_GET_PARAMS, &params);
	if (res) {
		dahdi_close_ss7_fd(link, curfd);
		ast_log(LOG_ERROR, "Unable to get parameters for sigchan %d (%s)\n", sigchan,
			strerror(errno));
		return -1;
	}
	if (params.sigtype != DAHDI_SIG_HDLCFCS
		&& params.sigtype != DAHDI_SIG_HARDHDLC
		&& params.sigtype != DAHDI_SIG_MTP2) {
		dahdi_close_ss7_fd(link, curfd);
		ast_log(LOG_ERROR, "sigchan %d is not in HDLC/FCS mode.\n", sigchan);
		return -1;
	}

	/* Set signaling channel buffer policy. */
	memset(&bi, 0, sizeof(bi));
	bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
	bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
	bi.numbufs = 32;
	bi.bufsize = 512;
	if (ioctl(link->ss7.fds[curfd], DAHDI_SET_BUFINFO, &bi)) {
		ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d: %s\n",
			sigchan, strerror(errno));
		dahdi_close_ss7_fd(link, curfd);
		return -1;
	}

	/* Get current signaling channel alarm status. */
	memset(&si, 0, sizeof(si));
	res = ioctl(link->ss7.fds[curfd], DAHDI_SPANSTAT, &si);
	if (res) {
		dahdi_close_ss7_fd(link, curfd);
		ast_log(LOG_ERROR, "Unable to get span state for sigchan %d (%s)\n", sigchan,
			strerror(errno));
	}

	res = sig_ss7_add_sigchan(&link->ss7, curfd, cur_ss7type,
		(params.sigtype == DAHDI_SIG_MTP2)
			? SS7_TRANSPORT_DAHDIMTP2
			: SS7_TRANSPORT_DAHDIDCHAN,
		si.alarms, cur_networkindicator, cur_pointcode, cur_adjpointcode);
	if (res) {
		dahdi_close_ss7_fd(link, curfd);
		return -1;
	}

	++link->ss7.numsigchans;

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
	if (!linksets[span-1].ss7.ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", span);
	} else {
		if (!strcasecmp(a->argv[3], "on")) {
			linksets[span - 1].ss7.debug = 1;
			ss7_set_debug(linksets[span-1].ss7.ss7, SIG_SS7_DEBUG);
			ast_cli(a->fd, "Enabled debugging on linkset %d\n", span);
		} else {
			linksets[span - 1].ss7.debug = 0;
			ss7_set_debug(linksets[span-1].ss7.ss7, 0);
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

	if (!linksets[linkset-1].ss7.ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	cic = atoi(a->argv[4]);

	if (cic < 1) {
		ast_cli(a->fd, "Invalid CIC specified!\n");
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].ss7.numchans; i++) {
		if (linksets[linkset-1].ss7.pvts[i]->cic == cic) {
			blocked = linksets[linkset-1].ss7.pvts[i]->locallyblocked;
			if (!blocked) {
				ast_mutex_lock(&linksets[linkset-1].ss7.lock);
				isup_blo(linksets[linkset-1].ss7.ss7, cic, linksets[linkset-1].ss7.pvts[i]->dpc);
				ast_mutex_unlock(&linksets[linkset-1].ss7.lock);
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
	pthread_kill(linksets[linkset-1].ss7.master, SIGURG);

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

	if (!linksets[linkset-1].ss7.ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].ss7.numchans; i++) {
		ast_cli(a->fd, "Sending remote blocking request on CIC %d\n", linksets[linkset-1].ss7.pvts[i]->cic);
		ast_mutex_lock(&linksets[linkset-1].ss7.lock);
		isup_blo(linksets[linkset-1].ss7.ss7, linksets[linkset-1].ss7.pvts[i]->cic, linksets[linkset-1].ss7.pvts[i]->dpc);
		ast_mutex_unlock(&linksets[linkset-1].ss7.lock);
	}

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].ss7.master, SIGURG);

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

	if (!linksets[linkset-1].ss7.ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	cic = atoi(a->argv[4]);

	if (cic < 1) {
		ast_cli(a->fd, "Invalid CIC specified!\n");
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].ss7.numchans; i++) {
		if (linksets[linkset-1].ss7.pvts[i]->cic == cic) {
			blocked = linksets[linkset-1].ss7.pvts[i]->locallyblocked;
			if (blocked) {
				ast_mutex_lock(&linksets[linkset-1].ss7.lock);
				isup_ubl(linksets[linkset-1].ss7.ss7, cic, linksets[linkset-1].ss7.pvts[i]->dpc);
				ast_mutex_unlock(&linksets[linkset-1].ss7.lock);
			}
		}
	}

	if (blocked > 0)
		ast_cli(a->fd, "Sent unblocking request for linkset %d on CIC %d\n", linkset, cic);

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].ss7.master, SIGURG);

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

	if (!linksets[linkset-1].ss7.ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	for (i = 0; i < linksets[linkset-1].ss7.numchans; i++) {
		ast_cli(a->fd, "Sending remote unblock request on CIC %d\n", linksets[linkset-1].ss7.pvts[i]->cic);
		ast_mutex_lock(&linksets[linkset-1].ss7.lock);
		isup_ubl(linksets[linkset-1].ss7.ss7, linksets[linkset-1].ss7.pvts[i]->cic, linksets[linkset-1].ss7.pvts[i]->dpc);
		ast_mutex_unlock(&linksets[linkset-1].ss7.lock);
	}

	/* Break poll on the linkset so it sends our messages */
	pthread_kill(linksets[linkset-1].ss7.master, SIGURG);

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_show_linkset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset;
	struct sig_ss7_linkset *ss7;
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
	ss7 = &linksets[linkset - 1].ss7;
	if (!ss7->ss7) {
		ast_cli(a->fd, "No SS7 running on linkset %d\n", linkset);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "SS7 linkset %d status: %s\n", linkset, (ss7->state == LINKSET_STATE_UP) ? "Up" : "Down");

	return CLI_SUCCESS;
}
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_SS7)
static char *handle_ss7_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int linkset;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ss7 show channels";
		e->usage =
			"Usage: ss7 show channels\n"
			"       Displays SS7 channel information at a glance.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	sig_ss7_cli_show_channels_header(a->fd);
	for (linkset = 0; linkset < NUM_SPANS; ++linkset) {
		if (linksets[linkset].ss7.ss7) {
			sig_ss7_cli_show_channels(a->fd, &linksets[linkset].ss7);
		}
	}
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
	AST_CLI_DEFINE(handle_ss7_show_channels, "Displays SS7 channel information"),
	AST_CLI_DEFINE(handle_ss7_version, "Displays libss7 version"),
};
#endif	/* defined(HAVE_SS7) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief CC agent initialization.
 * \since 1.8
 *
 * \param agent CC core agent control.
 * \param chan Original channel the agent will attempt to recall.
 *
 * \details
 * This callback is called when the CC core is initialized.  Agents should allocate
 * any private data necessary for the call and assign it to the private_data
 * on the agent.  Additionally, if any ast_cc_agent_flags are pertinent to the
 * specific agent type, they should be set in this function as well.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dahdi_pri_cc_agent_init(struct ast_cc_agent *agent, struct ast_channel *chan)
{
	struct dahdi_pvt *pvt;
	struct sig_pri_chan *pvt_chan;
	int res;

	ast_assert(!strcmp(ast_channel_tech(chan)->type, "DAHDI"));

	pvt = ast_channel_tech_pvt(chan);
	if (dahdi_sig_pri_lib_handles(pvt->sig)) {
		pvt_chan = pvt->sig_pvt;
	} else {
		pvt_chan = NULL;
	}
	if (!pvt_chan) {
		return -1;
	}

	ast_module_ref(ast_module_info->self);

	res = sig_pri_cc_agent_init(agent, pvt_chan);
	if (res) {
		ast_module_unref(ast_module_info->self);
	}
	return res;
}
#endif	/* defined(HAVE_PRI_CCSS) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Destroy private data on the agent.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * The core will call this function upon completion
 * or failure of CC.
 *
 * \return Nothing
 */
static void dahdi_pri_cc_agent_destructor(struct ast_cc_agent *agent)
{
	sig_pri_cc_agent_destructor(agent);

	ast_module_unref(ast_module_info->self);
}
#endif	/* defined(HAVE_PRI_CCSS) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CCSS)
static struct ast_cc_agent_callbacks dahdi_pri_cc_agent_callbacks = {
	.type = dahdi_pri_cc_type,
	.init = dahdi_pri_cc_agent_init,
	.start_offer_timer = sig_pri_cc_agent_start_offer_timer,
	.stop_offer_timer = sig_pri_cc_agent_stop_offer_timer,
	.respond = sig_pri_cc_agent_req_rsp,
	.status_request = sig_pri_cc_agent_status_req,
	.stop_ringing = sig_pri_cc_agent_stop_ringing,
	.party_b_free = sig_pri_cc_agent_party_b_free,
	.start_monitoring = sig_pri_cc_agent_start_monitoring,
	.callee_available = sig_pri_cc_agent_callee_available,
	.destructor = dahdi_pri_cc_agent_destructor,
};
#endif	/* defined(HAVE_PRI_CCSS) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_CCSS)
static struct ast_cc_monitor_callbacks dahdi_pri_cc_monitor_callbacks = {
	.type = dahdi_pri_cc_type,
	.request_cc = sig_pri_cc_monitor_req_cc,
	.suspend = sig_pri_cc_monitor_suspend,
	.unsuspend = sig_pri_cc_monitor_unsuspend,
	.status_response = sig_pri_cc_monitor_status_rsp,
	.cancel_available_timer = sig_pri_cc_monitor_cancel_available_timer,
	.destructor = sig_pri_cc_monitor_destructor,
};
#endif	/* defined(HAVE_PRI_CCSS) */
#endif	/* defined(HAVE_PRI) */

static int __unload_module(void)
{
	struct dahdi_pvt *p;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int i, j;
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

#ifdef HAVE_PRI
	for (i = 0; i < NUM_SPANS; i++) {
		if (pris[i].pri.master != AST_PTHREADT_NULL) {
			pthread_cancel(pris[i].pri.master);
			pthread_kill(pris[i].pri.master, SIGURG);
		}
	}
	ast_cli_unregister_multiple(dahdi_pri_cli, ARRAY_LEN(dahdi_pri_cli));
	ast_unregister_application(dahdi_send_keypad_facility_app);
#ifdef HAVE_PRI_PROG_W_CAUSE
	ast_unregister_application(dahdi_send_callrerouting_facility_app);
#endif
#endif
#if defined(HAVE_SS7)
	for (i = 0; i < NUM_SPANS; i++) {
		if (linksets[i].ss7.master != AST_PTHREADT_NULL) {
			pthread_cancel(linksets[i].ss7.master);
			pthread_kill(linksets[i].ss7.master, SIGURG);
		}
	}
	ast_cli_unregister_multiple(dahdi_ss7_cli, ARRAY_LEN(dahdi_ss7_cli));
#endif	/* defined(HAVE_SS7) */
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
#if defined(HAVE_PRI)
	ast_manager_unregister("PRIShowSpans");
#endif	/* defined(HAVE_PRI) */
	ast_data_unregister(NULL);
	ast_channel_unregister(&dahdi_tech);

	/* Hangup all interfaces if they have an owner */
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
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
		if (pris[i].pri.master && (pris[i].pri.master != AST_PTHREADT_NULL)) {
			pthread_join(pris[i].pri.master, NULL);
		}
		for (j = 0; j < SIG_PRI_NUM_DCHANS; j++) {
			dahdi_close_pri_fd(&(pris[i]), j);
		}
		sig_pri_stop_pri(&pris[i].pri);
	}
#if defined(HAVE_PRI_CCSS)
	ast_cc_agent_unregister(&dahdi_pri_cc_agent_callbacks);
	ast_cc_monitor_unregister(&dahdi_pri_cc_monitor_callbacks);
#endif	/* defined(HAVE_PRI_CCSS) */
	sig_pri_unload();
#endif

#if defined(HAVE_SS7)
	for (i = 0; i < NUM_SPANS; i++) {
		if (linksets[i].ss7.master && (linksets[i].ss7.master != AST_PTHREADT_NULL)) {
			pthread_join(linksets[i].ss7.master, NULL);
		}
		for (j = 0; j < SIG_SS7_NUM_DCHANS; j++) {
			dahdi_close_ss7_fd(&(linksets[i]), j);
		}
	}
#endif	/* defined(HAVE_SS7) */
	ast_cond_destroy(&ss_thread_complete);

	dahdi_tech.capabilities = ast_format_cap_destroy(dahdi_tech.capabilities);
	return 0;
}

static int unload_module(void)
{
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int y;
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */
#ifdef HAVE_PRI
	for (y = 0; y < NUM_SPANS; y++)
		ast_mutex_destroy(&pris[y].pri.lock);
#endif
#if defined(HAVE_SS7)
	for (y = 0; y < NUM_SPANS; y++)
		ast_mutex_destroy(&linksets[y].ss7.lock);
#endif	/* defined(HAVE_SS7) */
	return __unload_module();
}

static void string_replace(char *str, int char1, int char2)
{
	for (; *str; str++) {
		if (*str == char1) {
			*str = char2;
		}
	}
}

static char *parse_spanchan(char *chanstr, char **subdir)
{
	char *p;

	if ((p = strrchr(chanstr, '!')) == NULL) {
		*subdir = NULL;
		return chanstr;
	}
	*p++ = '\0';
	string_replace(chanstr, '!', '/');
	*subdir = chanstr;
	return p;
}

static int build_channels(struct dahdi_chan_conf *conf, const char *value, int reload, int lineno, int *found_pseudo)
{
	char *c, *chan;
	char *subdir;
	int x, start, finish;
	struct dahdi_pvt *tmp;

	if ((reload == 0) && (conf->chan.sig < 0) && !conf->is_sig_auto) {
		ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
		return -1;
	}

	c = ast_strdupa(value);
	c = parse_spanchan(c, &subdir);

	while ((chan = strsep(&c, ","))) {
		if (sscanf(chan, "%30d-%30d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(chan, "%30d", &start)) {
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
			char fn[PATH_MAX];
			int real_channel = x;

			if (!ast_strlen_zero(subdir)) {
				real_channel = device2chan(subdir, x, fn, sizeof(fn));
				if (real_channel < 0) {
					if (conf->ignore_failed_channels) {
						ast_log(LOG_WARNING, "Failed configuring %s!%d, (got %d). But moving on to others.\n",
								subdir, x, real_channel);
						continue;
					} else {
						ast_log(LOG_ERROR, "Failed configuring %s!%d, (got %d).\n",
								subdir, x, real_channel);
						return -1;
					}
				}
			}
			tmp = mkintf(real_channel, conf, reload);

			if (tmp) {
				ast_verb(3, "%s channel %d, %s signalling\n", reload ? "Reconfigured" : "Registered", real_channel, sig2str(tmp->sig));
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
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %u: '%s'\n", line, params[x]);
			continue;
		}

		if (ast_strlen_zero(param.name) || (strlen(param.name) > sizeof(confp->chan.echocancel.params[0].name)-1)) {
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %u: '%s'\n", line, param.name);
			continue;
		}

		strcpy(confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].name, param.name);

		if (param.value) {
			if (sscanf(param.value, "%30d", &confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].value) != 1) {
				ast_log(LOG_WARNING, "Invalid echocancel parameter value supplied at line %u: '%s'\n", line, param.value);
				continue;
			}
		}
		confp->chan.echocancel.head.param_count++;
	}
}

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_DISPLAY_TEXT)
/*!
 * \internal
 * \brief Determine the configured display text options.
 * \since 10.0
 *
 * \param value Configuration value string.
 *
 * \return Configured display text option flags.
 */
static unsigned long dahdi_display_text_option(const char *value)
{
	char *val_str;
	char *opt_str;
	unsigned long options;

	options = 0;
	val_str = ast_strdupa(value);

	for (;;) {
		opt_str = strsep(&val_str, ",");
		if (!opt_str) {
			break;
		}
		opt_str = ast_strip(opt_str);
		if (!*opt_str) {
			continue;
		}

		if (!strcasecmp(opt_str, "block")) {
			options |= PRI_DISPLAY_OPTION_BLOCK;
		} else if (!strcasecmp(opt_str, "name_initial")) {
			options |= PRI_DISPLAY_OPTION_NAME_INITIAL;
		} else if (!strcasecmp(opt_str, "name_update")) {
			options |= PRI_DISPLAY_OPTION_NAME_UPDATE;
		} else if (!strcasecmp(opt_str, "name")) {
			options |= (PRI_DISPLAY_OPTION_NAME_INITIAL | PRI_DISPLAY_OPTION_NAME_UPDATE);
		} else if (!strcasecmp(opt_str, "text")) {
			options |= PRI_DISPLAY_OPTION_TEXT;
		}
	}
	return options;
}
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
#endif	/* defined(HAVE_PRI) */

#if defined(HAVE_PRI)
#if defined(HAVE_PRI_DATETIME_SEND)
/*!
 * \internal
 * \brief Determine the configured date/time send policy option.
 * \since 10.0
 *
 * \param value Configuration value string.
 *
 * \return Configured date/time send policy option.
 */
static int dahdi_datetime_send_option(const char *value)
{
	int option;

	option = PRI_DATE_TIME_SEND_DEFAULT;

	if (ast_false(value)) {
		option = PRI_DATE_TIME_SEND_NO;
	} else if (!strcasecmp(value, "date")) {
		option = PRI_DATE_TIME_SEND_DATE;
	} else if (!strcasecmp(value, "date_hh")) {
		option = PRI_DATE_TIME_SEND_DATE_HH;
	} else if (!strcasecmp(value, "date_hhmm")) {
		option = PRI_DATE_TIME_SEND_DATE_HHMM;
	} else if (!strcasecmp(value, "date_hhmmss")) {
		option = PRI_DATE_TIME_SEND_DATE_HHMMSS;
	}

	return option;
}
#endif	/* defined(HAVE_PRI_DATETIME_SEND) */
#endif	/* defined(HAVE_PRI) */

/*! process_dahdi() - ignore keyword 'channel' and similar */
#define PROC_DAHDI_OPT_NOCHAN  (1 << 0)
/*! process_dahdi() - No warnings on non-existing cofiguration keywords */
#define PROC_DAHDI_OPT_NOWARN  (1 << 1)

static void parse_busy_pattern(struct ast_variable *v, struct ast_dsp_busy_pattern *busy_cadence)
{
	int count_pattern = 0;
	int norval = 0;
	char *temp = NULL;

	for (; ;) {
		/* Scans the string for the next value in the pattern. If none, it checks to see if any have been entered so far. */
		if(!sscanf(v->value, "%30d", &norval) && count_pattern == 0) { 
			ast_log(LOG_ERROR, "busypattern= expects either busypattern=tonelength,quietlength or busypattern=t1length, q1length, t2length, q2length at line %d.\n", v->lineno);
			break;
		}

		busy_cadence->pattern[count_pattern] = norval; 
		
		count_pattern++;
		if (count_pattern == 4) {
			break;
		}

		temp = strchr(v->value, ',');
		if (temp == NULL) {
			break;
		}
		v->value = temp + 1;
	}
	busy_cadence->length = count_pattern;

	if (count_pattern % 2 != 0) { 
		/* The pattern length must be divisible by two */
		ast_log(LOG_ERROR, "busypattern= expects either busypattern=tonelength,quietlength or busypattern=t1length, q1length, t2length, q2length at line %d.\n", v->lineno);
	}
	
}

static int process_dahdi(struct dahdi_chan_conf *confp, const char *cat, struct ast_variable *v, int reload, int options)
{
	struct dahdi_pvt *tmp;
	int y;
	int found_pseudo = 0;
	struct ast_variable *dahdichan = NULL;

	for (; v; v = v->next) {
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;

		/* Create the interface list */
		if (!strcasecmp(v->name, "channel") || !strcasecmp(v->name, "channels")) {
			if (options & PROC_DAHDI_OPT_NOCHAN) {
				ast_log(LOG_WARNING, "Channel '%s' ignored.\n", v->value);
				continue;
			}
			if (build_channels(confp, v->value, reload, v->lineno, &found_pseudo)) {
				if (confp->ignore_failed_channels) {
					ast_log(LOG_WARNING, "Channel '%s' failure ignored: ignore_failed_channels.\n", v->value);
					continue;
				} else {
					return -1;
				}
			}
			ast_debug(1, "Channel '%s' configured.\n", v->value);
		} else if (!strcasecmp(v->name, "ignore_failed_channels")) {
			confp->ignore_failed_channels = ast_true(v->value);
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
			/* Only process the last dahdichan value. */
			dahdichan = v;
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
			sscanf(v->value, "%30d,%30d,%30d", &confp->chan.drings.ringnum[0].ring[0], &confp->chan.drings.ringnum[0].ring[1], &confp->chan.drings.ringnum[0].ring[2]);
		} else if (!strcasecmp(v->name, "dring2")) {
			sscanf(v->value, "%30d,%30d,%30d", &confp->chan.drings.ringnum[1].ring[0], &confp->chan.drings.ringnum[1].ring[1], &confp->chan.drings.ringnum[1].ring[2]);
		} else if (!strcasecmp(v->name, "dring3")) {
			sscanf(v->value, "%30d,%30d,%30d", &confp->chan.drings.ringnum[2].ring[0], &confp->chan.drings.ringnum[2].ring[1], &confp->chan.drings.ringnum[2].ring[2]);
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
			else if (!strcasecmp(v->value, "dtmf"))
				confp->chan.cid_start = CID_START_DTMF_NOALERT;
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
		} else if (!strcasecmp(v->name, "description")) {
			ast_copy_string(confp->chan.description, v->value, sizeof(confp->chan.description));
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
			parse_busy_pattern(v, &confp->chan.busy_cadence);
		} else if (!strcasecmp(v->name, "callprogress")) {
			confp->chan.callprogress &= ~CALLPROGRESS_PROGRESS;
			if (ast_true(v->value))
				confp->chan.callprogress |= CALLPROGRESS_PROGRESS;
		} else if (!strcasecmp(v->name, "waitfordialtone")) {
			confp->chan.waitfordialtone = atoi(v->value);
		} else if (!strcasecmp(v->name, "dialtone_detect")) {
			if (!strcasecmp(v->value, "always")) {
				confp->chan.dialtone_detect = -1;
			} else if (ast_true(v->value)) {
				confp->chan.dialtone_detect = DEFAULT_DIALTONE_DETECT_TIMEOUT;
			} else if (ast_false(v->value)) {
				confp->chan.dialtone_detect = 0;
			} else {
				confp->chan.dialtone_detect = ast_strlen_zero(v->value) ? 0 : (8 * atoi(v->value)) / READ_SIZE;
			}
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
			if (sscanf(v->value, "%30d", &y) == 1) {
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
			ast_copy_string(confp->chan.parkinglot, v->value, sizeof(confp->chan.parkinglot));
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
		} else if (!strcasecmp(v->name, "namedcallgroup")) {
			confp->chan.named_callgroups = ast_get_namedgroups(v->value);
		} else if (!strcasecmp(v->name, "namedpickupgroup")) {
			confp->chan.named_pickupgroups = ast_get_namedgroups(v->value);
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
			if (sscanf(v->value, "%30f", &confp->chan.cid_rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid cid_rxgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%30f", &confp->chan.rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid rxgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%30f", &confp->chan.txgain) != 1) {
				ast_log(LOG_WARNING, "Invalid txgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "txdrc")) {
			if (sscanf(v->value, "%f", &confp->chan.txdrc) != 1) {
				ast_log(LOG_WARNING, "Invalid txdrc: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "rxdrc")) {
			if (sscanf(v->value, "%f", &confp->chan.rxdrc) != 1) {
				ast_log(LOG_WARNING, "Invalid rxdrc: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "tonezone")) {
			if (sscanf(v->value, "%30d", &confp->chan.tonezone) != 1) {
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
		} else if (!strcasecmp(v->name, "cid_tag")) {
			ast_copy_string(confp->chan.cid_tag, v->value, sizeof(confp->chan.cid_tag));
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
		} else if (ast_cc_is_config_param(v->name)) {
			ast_cc_set_param(confp->chan.cc_params, v->name, v->value);
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
#if defined(HAVE_PRI_CALL_HOLD)
					confp->chan.sig = SIG_BRI_PTMP;
					confp->pri.pri.nodetype = PRI_NETWORK;
#else
					ast_log(LOG_WARNING, "How cool would it be if someone implemented this mode!  For now, sucks for you. (line %d)\n", v->lineno);
#endif	/* !defined(HAVE_PRI_CALL_HOLD) */
#endif
#if defined(HAVE_SS7)
				} else if (!strcasecmp(v->value, "ss7")) {
					confp->chan.sig = SIG_SS7;
#endif	/* defined(HAVE_SS7) */
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
				} else if (!strcasecmp(v->value, "from_channel")) {
					confp->pri.pri.localdialplan = 0;
				} else if (!strcasecmp(v->value, "dynamic")) {
					confp->pri.pri.localdialplan = -1;
				} else if (!strcasecmp(v->value, "redundant")) {
					confp->pri.pri.localdialplan = -2;
				} else {
					ast_log(LOG_WARNING, "Unknown PRI localdialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "pricpndialplan")) {
				if (!strcasecmp(v->value, "national")) {
					confp->pri.pri.cpndialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					confp->pri.pri.cpndialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					confp->pri.pri.cpndialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					confp->pri.pri.cpndialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					confp->pri.pri.cpndialplan = PRI_LOCAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "from_channel")) {
					confp->pri.pri.cpndialplan = 0;
				} else if (!strcasecmp(v->value, "dynamic")) {
					confp->pri.pri.cpndialplan = -1;
				} else if (!strcasecmp(v->value, "redundant")) {
					confp->pri.pri.cpndialplan = -2;
				} else {
					ast_log(LOG_WARNING, "Unknown PRI cpndialplan '%s' at line %d.\n", v->value, v->lineno);
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
			} else if (!strcasecmp(v->name, "msn")) {
				ast_copy_string(confp->pri.pri.msn_list, v->value,
					sizeof(confp->pri.pri.msn_list));
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
			} else if (!strcasecmp(v->name, "force_restart_unavailable_chans")) {
				confp->pri.pri.force_restart_unavailable_chans = ast_true(v->value);
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
#if defined(HAVE_PRI_SERVICE_MESSAGES)
			} else if (!strcasecmp(v->name, "service_message_support")) {
				/* assuming switchtype for this channel group has been configured already */
				if ((confp->pri.pri.switchtype == PRI_SWITCH_ATT4ESS 
					|| confp->pri.pri.switchtype == PRI_SWITCH_LUCENT5E
					|| confp->pri.pri.switchtype == PRI_SWITCH_NI2) && ast_true(v->value)) {
					confp->pri.pri.enable_service_message_support = 1;
				} else {
					confp->pri.pri.enable_service_message_support = 0;
				}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
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
						confp->pri.pri.pritimers[timeridx] = timer;
					}
				} else {
					ast_log(LOG_WARNING,
						"'%s' is not a valid ISDN timer configuration string at line %d.\n",
						v->value, v->lineno);
				}
#endif /* PRI_GETSET_TIMERS */
			} else if (!strcasecmp(v->name, "facilityenable")) {
				confp->pri.pri.facilityenable = ast_true(v->value);
#if defined(HAVE_PRI_AOC_EVENTS)
			} else if (!strcasecmp(v->name, "aoc_enable")) {
				confp->pri.pri.aoc_passthrough_flag = 0;
				if (strchr(v->value, 's') || strchr(v->value, 'S')) {
					confp->pri.pri.aoc_passthrough_flag |= SIG_PRI_AOC_GRANT_S;
				}
				if (strchr(v->value, 'd') || strchr(v->value, 'D')) {
					confp->pri.pri.aoc_passthrough_flag |= SIG_PRI_AOC_GRANT_D;
				}
				if (strchr(v->value, 'e') || strchr(v->value, 'E')) {
					confp->pri.pri.aoc_passthrough_flag |= SIG_PRI_AOC_GRANT_E;
				}
			} else if (!strcasecmp(v->name, "aoce_delayhangup")) {
				confp->pri.pri.aoce_delayhangup = ast_true(v->value);
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_CALL_HOLD)
			} else if (!strcasecmp(v->name, "hold_disconnect_transfer")) {
				confp->pri.pri.hold_disconnect_transfer = ast_true(v->value);
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
			} else if (!strcasecmp(v->name, "moh_signaling")
				|| !strcasecmp(v->name, "moh_signalling")) {
				if (!strcasecmp(v->value, "moh")) {
					confp->pri.pri.moh_signaling = SIG_PRI_MOH_SIGNALING_MOH;
				} else if (!strcasecmp(v->value, "notify")) {
					confp->pri.pri.moh_signaling = SIG_PRI_MOH_SIGNALING_NOTIFY;
#if defined(HAVE_PRI_CALL_HOLD)
				} else if (!strcasecmp(v->value, "hold")) {
					confp->pri.pri.moh_signaling = SIG_PRI_MOH_SIGNALING_HOLD;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
				} else {
					confp->pri.pri.moh_signaling = SIG_PRI_MOH_SIGNALING_MOH;
				}
#if defined(HAVE_PRI_CCSS)
			} else if (!strcasecmp(v->name, "cc_ptmp_recall_mode")) {
				if (!strcasecmp(v->value, "global")) {
					confp->pri.pri.cc_ptmp_recall_mode = 0;/* globalRecall */
				} else if (!strcasecmp(v->value, "specific")) {
					confp->pri.pri.cc_ptmp_recall_mode = 1;/* specificRecall */
				} else {
					confp->pri.pri.cc_ptmp_recall_mode = 1;/* specificRecall */
				}
			} else if (!strcasecmp(v->name, "cc_qsig_signaling_link_req")) {
				if (!strcasecmp(v->value, "release")) {
					confp->pri.pri.cc_qsig_signaling_link_req = 0;/* release */
				} else if (!strcasecmp(v->value, "retain")) {
					confp->pri.pri.cc_qsig_signaling_link_req = 1;/* retain */
				} else if (!strcasecmp(v->value, "do_not_care")) {
					confp->pri.pri.cc_qsig_signaling_link_req = 2;/* do-not-care */
				} else {
					confp->pri.pri.cc_qsig_signaling_link_req = 1;/* retain */
				}
			} else if (!strcasecmp(v->name, "cc_qsig_signaling_link_rsp")) {
				if (!strcasecmp(v->value, "release")) {
					confp->pri.pri.cc_qsig_signaling_link_rsp = 0;/* release */
				} else if (!strcasecmp(v->value, "retain")) {
					confp->pri.pri.cc_qsig_signaling_link_rsp = 1;/* retain */
				} else {
					confp->pri.pri.cc_qsig_signaling_link_rsp = 1;/* retain */
				}
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CALL_WAITING)
			} else if (!strcasecmp(v->name, "max_call_waiting_calls")) {
				confp->pri.pri.max_call_waiting_calls = atoi(v->value);
				if (confp->pri.pri.max_call_waiting_calls < 0) {
					/* Negative values are not allowed. */
					confp->pri.pri.max_call_waiting_calls = 0;
				}
			} else if (!strcasecmp(v->name, "allow_call_waiting_calls")) {
				confp->pri.pri.allow_call_waiting_calls = ast_true(v->value);
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
#if defined(HAVE_PRI_MWI)
			} else if (!strcasecmp(v->name, "mwi_mailboxes")) {
				ast_copy_string(confp->pri.pri.mwi_mailboxes, v->value,
					sizeof(confp->pri.pri.mwi_mailboxes));
			} else if (!strcasecmp(v->name, "mwi_vm_numbers")) {
				ast_copy_string(confp->pri.pri.mwi_vm_numbers, v->value,
					sizeof(confp->pri.pri.mwi_vm_numbers));
#endif	/* defined(HAVE_PRI_MWI) */
			} else if (!strcasecmp(v->name, "append_msn_to_cid_tag")) {
				confp->pri.pri.append_msn_to_user_tag = ast_true(v->value);
			} else if (!strcasecmp(v->name, "inband_on_setup_ack")) {
				confp->pri.pri.inband_on_setup_ack = ast_true(v->value);
			} else if (!strcasecmp(v->name, "inband_on_proceeding")) {
				confp->pri.pri.inband_on_proceeding = ast_true(v->value);
#if defined(HAVE_PRI_DISPLAY_TEXT)
			} else if (!strcasecmp(v->name, "display_send")) {
				confp->pri.pri.display_flags_send = dahdi_display_text_option(v->value);
			} else if (!strcasecmp(v->name, "display_receive")) {
				confp->pri.pri.display_flags_receive = dahdi_display_text_option(v->value);
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
#if defined(HAVE_PRI_MCID)
			} else if (!strcasecmp(v->name, "mcid_send")) {
				confp->pri.pri.mcid_send = ast_true(v->value);
#endif	/* defined(HAVE_PRI_MCID) */
#if defined(HAVE_PRI_DATETIME_SEND)
			} else if (!strcasecmp(v->name, "datetime_send")) {
				confp->pri.pri.datetime_send = dahdi_datetime_send_option(v->value);
#endif	/* defined(HAVE_PRI_DATETIME_SEND) */
			} else if (!strcasecmp(v->name, "layer1_presence")) {
				if (!strcasecmp(v->value, "required")) {
					confp->pri.pri.layer1_ignored = 0;
				} else if (!strcasecmp(v->value, "ignore")) {
					confp->pri.pri.layer1_ignored = 1;
				} else {
					/* Default */
					confp->pri.pri.layer1_ignored = 0;
				}
#if defined(HAVE_PRI_L2_PERSISTENCE)
			} else if (!strcasecmp(v->name, "layer2_persistence")) {
				if (!strcasecmp(v->value, "keep_up")) {
					confp->pri.pri.l2_persistence = PRI_L2_PERSISTENCE_KEEP_UP;
				} else if (!strcasecmp(v->value, "leave_down")) {
					confp->pri.pri.l2_persistence = PRI_L2_PERSISTENCE_LEAVE_DOWN;
				} else {
					confp->pri.pri.l2_persistence = PRI_L2_PERSISTENCE_DEFAULT;
				}
#endif	/* defined(HAVE_PRI_L2_PERSISTENCE) */
			} else if (!strcasecmp(v->name, "colp_send")) {
				if (!strcasecmp(v->value, "block")) {
					confp->pri.pri.colp_send = SIG_PRI_COLP_BLOCK;
				} else if (!strcasecmp(v->value, "connect")) {
					confp->pri.pri.colp_send = SIG_PRI_COLP_CONNECT;
				} else if (!strcasecmp(v->value, "update")) {
					confp->pri.pri.colp_send = SIG_PRI_COLP_UPDATE;
				} else {
					confp->pri.pri.colp_send = SIG_PRI_COLP_UPDATE;
				}
#endif /* HAVE_PRI */
#if defined(HAVE_SS7)
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
				ast_copy_string(confp->ss7.ss7.internationalprefix, v->value, sizeof(confp->ss7.ss7.internationalprefix));
			} else if (!strcasecmp(v->name, "ss7_nationalprefix")) {
				ast_copy_string(confp->ss7.ss7.nationalprefix, v->value, sizeof(confp->ss7.ss7.nationalprefix));
			} else if (!strcasecmp(v->name, "ss7_subscriberprefix")) {
				ast_copy_string(confp->ss7.ss7.subscriberprefix, v->value, sizeof(confp->ss7.ss7.subscriberprefix));
			} else if (!strcasecmp(v->name, "ss7_unknownprefix")) {
				ast_copy_string(confp->ss7.ss7.unknownprefix, v->value, sizeof(confp->ss7.ss7.unknownprefix));
			} else if (!strcasecmp(v->name, "ss7_called_nai")) {
				if (!strcasecmp(v->value, "national")) {
					confp->ss7.ss7.called_nai = SS7_NAI_NATIONAL;
				} else if (!strcasecmp(v->value, "international")) {
					confp->ss7.ss7.called_nai = SS7_NAI_INTERNATIONAL;
				} else if (!strcasecmp(v->value, "subscriber")) {
					confp->ss7.ss7.called_nai = SS7_NAI_SUBSCRIBER;
				} else if (!strcasecmp(v->value, "unknown")) {
					confp->ss7.ss7.called_nai = SS7_NAI_UNKNOWN;
				} else if (!strcasecmp(v->value, "dynamic")) {
					confp->ss7.ss7.called_nai = SS7_NAI_DYNAMIC;
				} else {
					ast_log(LOG_WARNING, "Unknown SS7 called_nai '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "ss7_calling_nai")) {
				if (!strcasecmp(v->value, "national")) {
					confp->ss7.ss7.calling_nai = SS7_NAI_NATIONAL;
				} else if (!strcasecmp(v->value, "international")) {
					confp->ss7.ss7.calling_nai = SS7_NAI_INTERNATIONAL;
				} else if (!strcasecmp(v->value, "subscriber")) {
					confp->ss7.ss7.calling_nai = SS7_NAI_SUBSCRIBER;
				} else if (!strcasecmp(v->value, "unknown")) {
					confp->ss7.ss7.calling_nai = SS7_NAI_UNKNOWN;
				} else if (!strcasecmp(v->value, "dynamic")) {
					confp->ss7.ss7.calling_nai = SS7_NAI_DYNAMIC;
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
					link->ss7.flags |= LINKSET_FLAG_EXPLICITACM;
#endif	/* defined(HAVE_SS7) */
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
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 2
			} else if (!strcasecmp(v->name, "mfcr2_dtmf_detection")) {
				confp->mfcr2.dtmf_detection = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_dtmf_dialing")) {
				confp->mfcr2.dtmf_dialing = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_dtmf_time_on")) {
				confp->mfcr2.dtmf_time_on = atoi(v->value);
			} else if (!strcasecmp(v->name, "mfcr2_dtmf_time_off")) {
				confp->mfcr2.dtmf_time_off = atoi(v->value);
#endif
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 3
			} else if (!strcasecmp(v->name, "mfcr2_dtmf_end_timeout")) {
				confp->mfcr2.dtmf_end_timeout = atoi(v->value);
#endif
			} else if (!strcasecmp(v->name, "mfcr2_get_ani_first")) {
				confp->mfcr2.get_ani_first = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_double_answer")) {
				confp->mfcr2.double_answer = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_charge_calls")) {
				confp->mfcr2.charge_calls = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_accept_on_offer")) {
				confp->mfcr2.accept_on_offer = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_allow_collect_calls")) {
				confp->mfcr2.allow_collect_calls = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_forced_release")) {
				confp->mfcr2.forced_release = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "mfcr2_immediate_accept")) {
				confp->mfcr2.immediate_accept = ast_true(v->value) ? 1 : 0;
#if defined(OR2_LIB_INTERFACE) && OR2_LIB_INTERFACE > 1
			} else if (!strcasecmp(v->name, "mfcr2_skip_category")) {
				confp->mfcr2.skip_category_request = ast_true(v->value) ? 1 : 0;
#endif
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
				element_count = sscanf(v->value, "%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d,%30d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8], &c[9], &c[10], &c[11], &c[12], &c[13], &c[14], &c[15]);

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
						close(ctlfd);
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
			} else if (!strcasecmp(v->name, "dtmfcidlevel")) {
				dtmfcid_level = atoi(v->value);
			} else if (!strcasecmp(v->name, "reportalarms")) {
				if (!strcasecmp(v->value, "all"))
					report_alarms = REPORT_CHANNEL_ALARMS | REPORT_SPAN_ALARMS;
				if (!strcasecmp(v->value, "none"))
					report_alarms = 0;
				else if (!strcasecmp(v->value, "channels"))
					report_alarms = REPORT_CHANNEL_ALARMS;
			   else if (!strcasecmp(v->value, "spans"))
					report_alarms = REPORT_SPAN_ALARMS;
			 }
		} else if (!(options & PROC_DAHDI_OPT_NOWARN) )
			ast_log(LOG_WARNING, "Ignoring any changes to '%s' (on reload) at line %d.\n", v->name, v->lineno);
	}

	if (dahdichan) {
		/* Process the deferred dahdichan value. */
		if (build_channels(confp, dahdichan->value, reload, dahdichan->lineno,
			&found_pseudo)) {
			if (confp->ignore_failed_channels) {
				ast_log(LOG_WARNING,
					"Dahdichan '%s' failure ignored: ignore_failed_channels.\n",
					dahdichan->value);
			} else {
				return -1;
			}
		}
	}

	/*
	 * Since confp has already filled individual dahdi_pvt objects with channels
	 * at this point, clear the variables in confp's pvt.
	 */
	if (confp->chan.vars) {
		ast_variables_destroy(confp->chan.vars);
		confp->chan.vars = NULL;
	}

	/* mark the first channels of each DAHDI span to watch for their span alarms */
	for (tmp = iflist, y=-1; tmp; tmp = tmp->next) {
		if (!tmp->destroy && tmp->span != y) {
			tmp->manages_span_alarms = 1;
			y = tmp->span; 
		} else {
			tmp->manages_span_alarms = 0;
		}
	}

	/*< \todo why check for the pseudo in the per-channel section.
	 * Any actual use for manual setup of the pseudo channel? */
	if (!found_pseudo && reload != 1 && !(options & PROC_DAHDI_OPT_NOCHAN)) {
		/* use the default configuration for a channel, so
		   that any settings from real configured channels
		   don't "leak" into the pseudo channel config
		*/
		struct dahdi_chan_conf conf = dahdi_chan_conf_default();

		if (conf.chan.cc_params) {
			tmp = mkintf(CHAN_PSEUDO, &conf, reload);
		} else {
			tmp = NULL;
		}
		if (tmp) {
			ast_verb(3, "Automatically generated pseudo channel\n");
		} else {
			ast_log(LOG_WARNING, "Unable to register pseudo channel!\n");
		}
		ast_cc_config_params_destroy(conf.chan.cc_params);
	}

	/* Since named callgroup and named pickup group are ref'd to dahdi_pvt at this point, unref container in confp's pvt. */
	confp->chan.named_callgroups = ast_unref_namedgroups(confp->chan.named_callgroups);
	confp->chan.named_pickupgroups = ast_unref_namedgroups(confp->chan.named_pickupgroups);

	return 0;
}

/*!
 * \internal
 * \brief Deep copy struct dahdi_chan_conf.
 * \since 1.8
 *
 * \param dest Destination.
 * \param src Source.
 *
 * \return Nothing
 */
static void deep_copy_dahdi_chan_conf(struct dahdi_chan_conf *dest, const struct dahdi_chan_conf *src)
{
	struct ast_cc_config_params *cc_params;

	cc_params = dest->chan.cc_params;
	*dest = *src;
	dest->chan.cc_params = cc_params;
	ast_cc_copy_config_params(dest->chan.cc_params, src->chan.cc_params);
}

/*!
 * \internal
 * \brief Setup DAHDI channel driver.
 *
 * \param reload enum: load_module(0), reload(1), restart(2).
 * \param default_conf Default config parameters.  So cc_params can be properly destroyed.
 * \param base_conf Default config parameters per section.  So cc_params can be properly destroyed.
 * \param conf Local config parameters.  So cc_params can be properly destroyed.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_dahdi_int(int reload, struct dahdi_chan_conf *default_conf, struct dahdi_chan_conf *base_conf, struct dahdi_chan_conf *conf)
{
	struct ast_config *cfg;
	struct ast_config *ucfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload == 1 ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *chans;
	const char *cat;
	int res;

#ifdef HAVE_PRI
	char *c;
	int spanno;
	int i;
	int logicalspan;
	int trunkgroup;
	int dchannels[SIG_PRI_NUM_DCHANS];
#endif
	int have_cfg_now;
	static int had_cfg_before = 1;/* So initial load will complain if we don't have cfg. */

	cfg = ast_config_load(config, config_flags);
	have_cfg_now = !!cfg;
	if (!cfg) {
		/* Error if we have no config file */
		if (had_cfg_before) {
			ast_log(LOG_ERROR, "Unable to load config %s\n", config);
			ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		}
		cfg = ast_config_new();/* Dummy config */
		if (!cfg) {
			return 0;
		}
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED) {
			ast_config_destroy(cfg);
			return 0;
		}
		if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED) {
			return 0;
		}
		if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			return 0;
		}
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		cfg = ast_config_load(config, config_flags);
		have_cfg_now = !!cfg;
		if (!cfg) {
			if (had_cfg_before) {
				/* We should have been able to load the config. */
				ast_log(LOG_ERROR, "Bad. Unable to load config %s\n", config);
				ast_config_destroy(ucfg);
				return 0;
			}
			cfg = ast_config_new();/* Dummy config */
			if (!cfg) {
				ast_config_destroy(ucfg);
				return 0;
			}
		} else if (cfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
			ast_config_destroy(ucfg);
			return 0;
		}
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
		return 0;
	} else {
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
	}
	had_cfg_before = have_cfg_now;

	/* It's a little silly to lock it, but we might as well just to be sure */
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
						while (c && (i < SIG_PRI_NUM_DCHANS)) {
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
	if ((res = process_dahdi(base_conf,
		"" /* Must be empty for the channels category.  Silly voicemail mailbox. */,
		v, reload, 0))) {
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

		chans = ast_variable_retrieve(cfg, cat, "dahdichan");
		if (ast_strlen_zero(chans)) {
			/* Section is useless without a dahdichan value present. */
			continue;
		}

		/* Copy base_conf to conf. */
		deep_copy_dahdi_chan_conf(conf, base_conf);

		if ((res = process_dahdi(conf, cat, ast_variable_browse(cfg, cat), reload, PROC_DAHDI_OPT_NOCHAN))) {
			ast_mutex_unlock(&iflock);
			ast_config_destroy(cfg);
			if (ucfg) {
				ast_config_destroy(ucfg);
			}
			return res;
		}
	}

	ast_config_destroy(cfg);

	if (ucfg) {
		/* Reset base_conf, so things don't leak from chan_dahdi.conf */
		deep_copy_dahdi_chan_conf(base_conf, default_conf);
		process_dahdi(base_conf,
			"" /* Must be empty for the general category.  Silly voicemail mailbox. */,
			ast_variable_browse(ucfg, "general"), 1, 0);

		for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
			if (!strcasecmp(cat, "general")) {
				continue;
			}

			chans = ast_variable_retrieve(ucfg, cat, "dahdichan");
			if (ast_strlen_zero(chans)) {
				/* Section is useless without a dahdichan value present. */
				continue;
			}

			/* Copy base_conf to conf. */
			deep_copy_dahdi_chan_conf(conf, base_conf);

			if ((res = process_dahdi(conf, cat, ast_variable_browse(ucfg, cat), reload, PROC_DAHDI_OPT_NOCHAN | PROC_DAHDI_OPT_NOWARN))) {
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
#if defined(HAVE_SS7)
	if (reload != 1) {
		int x;
		for (x = 0; x < NUM_SPANS; x++) {
			if (linksets[x].ss7.ss7) {
				if (ast_pthread_create(&linksets[x].ss7.master, NULL, ss7_linkset, &linksets[x].ss7)) {
					ast_log(LOG_ERROR, "Unable to start SS7 linkset on span %d\n", x + 1);
					return -1;
				} else
					ast_verb(2, "Starting SS7 linkset on span %d\n", x + 1);
			}
		}
	}
#endif	/* defined(HAVE_SS7) */
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

/*!
 * \internal
 * \brief Setup DAHDI channel driver.
 *
 * \param reload enum: load_module(0), reload(1), restart(2).
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_dahdi(int reload)
{
	int res;
	struct dahdi_chan_conf default_conf = dahdi_chan_conf_default();
	struct dahdi_chan_conf base_conf = dahdi_chan_conf_default();
	struct dahdi_chan_conf conf = dahdi_chan_conf_default();

	if (default_conf.chan.cc_params && base_conf.chan.cc_params && conf.chan.cc_params) {
		res = setup_dahdi_int(reload, &default_conf, &base_conf, &conf);
	} else {
		res = -1;
	}
	ast_cc_config_params_destroy(default_conf.chan.cc_params);
	ast_cc_config_params_destroy(base_conf.chan.cc_params);
	ast_cc_config_params_destroy(conf.chan.cc_params);

	return res;
}

/*!
 * \internal
 * \brief Callback used to generate the dahdi status tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int dahdi_status_data_provider_get(const struct ast_data_search *search,
		struct ast_data *data_root)
{
	int ctl, res, span;
	struct ast_data *data_span, *data_alarms;
	struct dahdi_spaninfo s;

	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0) {
		ast_log(LOG_ERROR, "No DAHDI found. Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		return -1;
	}
	for (span = 1; span < DAHDI_MAX_SPANS; ++span) {
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res) {
			continue;
		}

		data_span = ast_data_add_node(data_root, "span");
		if (!data_span) {
			continue;
		}
		ast_data_add_str(data_span, "description", s.desc);

		/* insert the alarms status */
		data_alarms = ast_data_add_node(data_span, "alarms");
		if (!data_alarms) {
			continue;
		}

		ast_data_add_bool(data_alarms, "BLUE", s.alarms & DAHDI_ALARM_BLUE);
		ast_data_add_bool(data_alarms, "YELLOW", s.alarms & DAHDI_ALARM_YELLOW);
		ast_data_add_bool(data_alarms, "RED", s.alarms & DAHDI_ALARM_RED);
		ast_data_add_bool(data_alarms, "LOOPBACK", s.alarms & DAHDI_ALARM_LOOPBACK);
		ast_data_add_bool(data_alarms, "RECOVER", s.alarms & DAHDI_ALARM_RECOVER);
		ast_data_add_bool(data_alarms, "NOTOPEN", s.alarms & DAHDI_ALARM_NOTOPEN);

		ast_data_add_int(data_span, "irqmisses", s.irqmisses);
		ast_data_add_int(data_span, "bpviol", s.bpvcount);
		ast_data_add_int(data_span, "crc4", s.crc4count);
		ast_data_add_str(data_span, "framing",	s.lineconfig & DAHDI_CONFIG_D4 ? "D4" :
							s.lineconfig & DAHDI_CONFIG_ESF ? "ESF" :
							s.lineconfig & DAHDI_CONFIG_CCS ? "CCS" :
							"CAS");
		ast_data_add_str(data_span, "coding",	s.lineconfig & DAHDI_CONFIG_B8ZS ? "B8ZS" :
							s.lineconfig & DAHDI_CONFIG_HDB3 ? "HDB3" :
							s.lineconfig & DAHDI_CONFIG_AMI ? "AMI" :
							"Unknown");
		ast_data_add_str(data_span, "options",	s.lineconfig & DAHDI_CONFIG_CRC4 ?
							s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" :
							s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "YEL" : "");
		ast_data_add_str(data_span, "lbo", lbostr[s.lbo]);

		/* if this span doesn't match remove it. */
		if (!ast_data_search_match(search, data_span)) {
			ast_data_remove_node(data_root, data_span);
		}
	}
	close(ctl);

	return 0;
}

/*!
 * \internal
 * \brief Callback used to generate the dahdi channels tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int dahdi_channels_data_provider_get(const struct ast_data_search *search,
		struct ast_data *data_root)
{
	struct dahdi_pvt *tmp;
	struct ast_data *data_channel;

	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		data_channel = ast_data_add_node(data_root, "channel");
		if (!data_channel) {
			continue;
		}

		ast_data_add_structure(dahdi_pvt, data_channel, tmp);

		/* if this channel doesn't match remove it. */
		if (!ast_data_search_match(search, data_channel)) {
			ast_data_remove_node(data_root, data_channel);
		}
	}
	ast_mutex_unlock(&iflock);

	return 0;
}

/*!
 * \internal
 * \brief Callback used to generate the dahdi channels tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int dahdi_version_data_provider_get(const struct ast_data_search *search,
		struct ast_data *data_root)
{
	int pseudo_fd = -1;
	struct dahdi_versioninfo vi = {
		.version = "Unknown",
		.echo_canceller = "Unknown"
	};

	if ((pseudo_fd = open("/dev/dahdi/ctl", O_RDONLY)) < 0) {
		ast_log(LOG_ERROR, "Failed to open control file to get version.\n");
		return -1;
	}

	if (ioctl(pseudo_fd, DAHDI_GETVERSION, &vi)) {
		ast_log(LOG_ERROR, "Failed to get DAHDI version: %s\n", strerror(errno));
	}

	close(pseudo_fd);

	ast_data_add_str(data_root, "value", vi.version);
	ast_data_add_str(data_root, "echocanceller", vi.echo_canceller);

	return 0;
}

static const struct ast_data_handler dahdi_status_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = dahdi_status_data_provider_get
};

static const struct ast_data_handler dahdi_channels_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = dahdi_channels_data_provider_get
};

static const struct ast_data_handler dahdi_version_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = dahdi_version_data_provider_get
};

static const struct ast_data_entry dahdi_data_providers[] = {
	AST_DATA_ENTRY("asterisk/channel/dahdi/status", &dahdi_status_data_provider),
	AST_DATA_ENTRY("asterisk/channel/dahdi/channels", &dahdi_channels_data_provider),
	AST_DATA_ENTRY("asterisk/channel/dahdi/version", &dahdi_version_data_provider)
};

static int load_module(void)
{
	int res;
	struct ast_format tmpfmt;
#if defined(HAVE_PRI) || defined(HAVE_SS7)
	int y;
#endif	/* defined(HAVE_PRI) || defined(HAVE_SS7) */

	if (!(dahdi_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_add(dahdi_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));
	ast_format_cap_add(dahdi_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(dahdi_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));

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
#if defined(HAVE_PRI_CCSS)
	if (ast_cc_agent_register(&dahdi_pri_cc_agent_callbacks)
		|| ast_cc_monitor_register(&dahdi_pri_cc_monitor_callbacks)) {
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}
#endif	/* defined(HAVE_PRI_CCSS) */
	if (sig_pri_load(
#if defined(HAVE_PRI_CCSS)
		dahdi_pri_cc_type
#else
		NULL
#endif	/* defined(HAVE_PRI_CCSS) */
		)) {
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}
#endif
#if defined(HAVE_SS7)
	memset(linksets, 0, sizeof(linksets));
	for (y = 0; y < NUM_SPANS; y++) {
		sig_ss7_init_linkset(&linksets[y].ss7);
	}
	ss7_set_error(dahdi_ss7_error);
	ss7_set_message(dahdi_ss7_message);
#endif	/* defined(HAVE_SS7) */
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
#if defined(HAVE_SS7)
	ast_cli_register_multiple(dahdi_ss7_cli, ARRAY_LEN(dahdi_ss7_cli));
#endif	/* defined(HAVE_SS7) */
#ifdef HAVE_OPENR2
	ast_cli_register_multiple(dahdi_mfcr2_cli, ARRAY_LEN(dahdi_mfcr2_cli));
	ast_register_application_xml(dahdi_accept_r2_call_app, dahdi_accept_r2_call_exec);
#endif

	ast_cli_register_multiple(dahdi_cli, ARRAY_LEN(dahdi_cli));
	/* register all the data providers */
	ast_data_register_multiple(dahdi_data_providers, ARRAY_LEN(dahdi_data_providers));
	memset(round_robin, 0, sizeof(round_robin));
	ast_manager_register_xml("DAHDITransfer", 0, action_transfer);
	ast_manager_register_xml("DAHDIHangup", 0, action_transferhangup);
	ast_manager_register_xml("DAHDIDialOffhook", 0, action_dahdidialoffhook);
	ast_manager_register_xml("DAHDIDNDon", 0, action_dahdidndon);
	ast_manager_register_xml("DAHDIDNDoff", 0, action_dahdidndoff);
	ast_manager_register_xml("DAHDIShowChannels", 0, action_dahdishowchannels);
	ast_manager_register_xml("DAHDIRestart", 0, action_dahdirestart);
#if defined(HAVE_PRI)
	ast_manager_register_xml("PRIShowSpans", 0, action_prishowspans);
#endif	/* defined(HAVE_PRI) */

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
	struct dahdi_pvt *p = ast_channel_tech_pvt(c);
	struct pollfd fds[1];
	int size,res,fd,len,x;
	int bytes=0;
	int idx;

	/*
	 * Initial carrier (imaginary)
	 *
	 * Note: The following float variables are used by the
	 * PUT_CLID_MARKMS and PUT_CLID() macros.
	 */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;

	if (!text[0]) {
		return(0); /* if nothing to send, don't */
	}
	idx = dahdi_get_index(c, p, 0);
	if (idx < 0) {
		ast_log(LOG_WARNING, "Huh?  I don't exist?\n");
		return -1;
	}
	if ((!p->tdd) && (!p->mate)) {
#if defined(HAVE_PRI)
#if defined(HAVE_PRI_DISPLAY_TEXT)
		ast_mutex_lock(&p->lock);
		if (dahdi_sig_pri_lib_handles(p->sig)) {
			sig_pri_sendtext(p->sig_pvt, text);
		}
		ast_mutex_unlock(&p->lock);
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
#endif	/* defined(HAVE_PRI) */
		return(0);  /* if not in TDD mode, just return */
	}
	if (p->mate)
		buf = ast_malloc(((strlen(text) + 1) * ASCII_BYTES_PER_CHAR) + END_SILENCE_LEN + HEADER_LEN);
	else
		buf = ast_malloc(((strlen(text) + 1) * TDD_BYTES_PER_CHAR) + END_SILENCE_LEN);
	if (!buf)
		return -1;
	mybuf = buf;
	if (p->mate) {
		struct ast_format tmp;
		/* PUT_CLI_MARKMS is a macro and requires a format ptr called codec to be present */
		struct ast_format *codec = &tmp;
		ast_format_set(codec, AST_LAW(p), 0);
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, tdesc,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
		.nonoptreq = "res_smdi",
	);
