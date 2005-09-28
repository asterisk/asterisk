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

/*
 *
 * Zaptel Pseudo TDM interface 
 * 
 */

#include <stdio.h>
#include <string.h>
#ifdef __NetBSD__
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <errno.h>
#include <stdlib.h>
#if !defined(SOLARIS) && !defined(__FreeBSD__)
#include <stdint.h>
#endif
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/zaptel.h>
#else
#include <zaptel.h>
#endif /* __linux__ */
#include <math.h>
#include <tonezone.h>
#include <ctype.h>
#ifdef ZAPATA_PRI
#include <libpri.h>
#ifndef PRI_CALLINGPLANRDNIS
#error "You need newer libpri"
#endif
#endif
#ifdef ZAPATA_R2
#include <libmfcr2.h>
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
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

#ifndef ZT_SIG_EM_E1
#error "Your zaptel is too old.  please cvs update"
#endif

#ifndef ZT_TONEDETECT
/* Work around older code with no tone detect */
#define ZT_EVENT_DTMFDOWN 0
#define ZT_EVENT_DTMFUP 0
#endif

/*
 * Define ZHONE_HACK to cause us to go off hook and then back on hook when
 * the user hangs up to reset the state machine so ring works properly.
 * This is used to be able to support kewlstart by putting the zhone in
 * groundstart mode since their forward disconnect supervision is entirely
 * broken even though their documentation says it isn't and their support
 * is entirely unwilling to provide any assistance with their channel banks
 * even though their web site says they support their products for life.
 */

/* #define ZHONE_HACK */

/*
 * Define if you want to check the hook state for an FXO (FXS signalled) interface
 * before dialing on it.  Certain FXO interfaces always think they're out of
 * service with this method however.
 */
/* #define ZAP_CHECK_HOOKSTATE */

/* Typically, how many rings before we should send Caller*ID */
#define DEFAULT_CIDRINGS 1

#define CHANNEL_PSEUDO -12

#define AST_LAW(p) (((p)->law == ZT_LAW_ALAW) ? AST_FORMAT_ALAW : AST_FORMAT_ULAW)

/* Signaling types that need to use MF detection should be placed in this macro */
#define NEED_MFDETECT(p) (((p)->sig == SIG_FEATDMF) || ((p)->sig == SIG_FEATDMF_TA) || ((p)->sig == SIG_E911) || ((p)->sig == SIG_FEATB)) 

static const char desc[] = "Zapata Telephony"
#ifdef ZAPATA_PRI
               " w/PRI"
#endif
#ifdef ZAPATA_R2
               " w/R2"
#endif
;

static const char tdesc[] = "Zapata Telephony Driver"
#ifdef ZAPATA_PRI
               " w/PRI"
#endif
#ifdef ZAPATA_R2
               " w/R2"
#endif
;

static const char type[] = "Zap";
static const char config[] = "zapata.conf";

#define SIG_EM		ZT_SIG_EM
#define SIG_EMWINK 	(0x0100000 | ZT_SIG_EM)
#define SIG_FEATD	(0x0200000 | ZT_SIG_EM)
#define	SIG_FEATDMF	(0x0400000 | ZT_SIG_EM)
#define	SIG_FEATB	(0x0800000 | ZT_SIG_EM)
#define	SIG_E911	(0x1000000 | ZT_SIG_EM)
#define	SIG_FEATDMF_TA	(0x2000000 | ZT_SIG_EM)
#define SIG_FXSLS	ZT_SIG_FXSLS
#define SIG_FXSGS	ZT_SIG_FXSGS
#define SIG_FXSKS	ZT_SIG_FXSKS
#define SIG_FXOLS	ZT_SIG_FXOLS
#define SIG_FXOGS	ZT_SIG_FXOGS
#define SIG_FXOKS	ZT_SIG_FXOKS
#define SIG_PRI		ZT_SIG_CLEAR
#define SIG_R2		ZT_SIG_CAS
#define	SIG_SF		ZT_SIG_SF
#define SIG_SFWINK 	(0x0100000 | ZT_SIG_SF)
#define SIG_SF_FEATD	(0x0200000 | ZT_SIG_SF)
#define	SIG_SF_FEATDMF	(0x0400000 | ZT_SIG_SF)
#define	SIG_SF_FEATB	(0x0800000 | ZT_SIG_SF)
#define SIG_EM_E1	ZT_SIG_EM_E1
#define SIG_GR303FXOKS	(0x0100000 | ZT_SIG_FXOKS)
#define SIG_GR303FXSKS	(0x0100000 | ZT_SIG_FXSKS)

#define NUM_SPANS 		32
#define NUM_DCHANS		4		/* No more than 4 d-channels */
#define MAX_CHANNELS	672		/* No more than a DS3 per trunk group */

#define CHAN_PSEUDO	-2

#define DCHAN_PROVISIONED (1 << 0)
#define DCHAN_NOTINALARM  (1 << 1)
#define DCHAN_UP          (1 << 2)

#define DCHAN_AVAILABLE	(DCHAN_PROVISIONED | DCHAN_NOTINALARM | DCHAN_UP)

static char context[AST_MAX_CONTEXT] = "default";
static char cid_num[256] = "";
static char cid_name[256] = "";
static char defaultcic[64] = "";
static char defaultozz[64] = "";

static char language[MAX_LANGUAGE] = "";
static char musicclass[MAX_MUSICCLASS] = "";
static char progzone[10]= "";

static int usedistinctiveringdetection = 0;

static int transfertobusy = 1;

static int use_callerid = 1;
static int cid_signalling = CID_SIG_BELL;
static int cid_start = CID_START_RING;
static int zaptrcallerid = 0;
static int cur_signalling = -1;

static ast_group_t cur_group = 0;
static ast_group_t cur_callergroup = 0;
static ast_group_t cur_pickupgroup = 0;
static int relaxdtmf = 0;

static int immediate = 0;

static int stripmsd = 0;

static int callwaiting = 0;

static int callwaitingcallerid = 0;

static int hidecallerid = 0;

static int restrictcid = 0;

static int use_callingpres = 0;

static int callreturn = 0;

static int threewaycalling = 0;

static int transfer = 0;

static int canpark = 0;

static int cancallforward = 0;

static float rxgain = 0.0;

static float txgain = 0.0;

static int tonezone = -1;

static int echocancel;

static int echotraining;

static int pulse;

static int echocanbridged = 0;

static int busydetect = 0;

static int busycount = 3;
static int busy_tonelength = 0;
static int busy_quietlength = 0;

static int callprogress = 0;

static char accountcode[AST_MAX_ACCOUNT_CODE] = "";

static char mailbox[AST_MAX_EXTENSION];

static int amaflags = 0;

static int adsi = 0;

static int numbufs = 4;

static int cur_prewink = -1;
static int cur_preflash = -1;
static int cur_wink = -1;
static int cur_flash = -1;
static int cur_start = -1;
static int cur_rxwink = -1;
static int cur_rxflash = -1;
static int cur_debounce = -1;
static int cur_priexclusive = 0;

static int priindication_oob = 0;

#ifdef ZAPATA_PRI
static int minunused = 2;
static int minidle = 0;
static char idleext[AST_MAX_EXTENSION];
static char idledial[AST_MAX_EXTENSION];
static int overlapdial = 0;
static int facilityenable = 0;
static char internationalprefix[10] = "";
static char nationalprefix[10] = "";
static char localprefix[20] = "";
static char privateprefix[20] = "";
static char unknownprefix[20] = "";
static long resetinterval = 3600;			/* How often (in seconds) to reset unused channels. Default 1 hour. */
static struct ast_channel inuse = { "GR-303InUse" };
#ifdef PRI_GETSET_TIMERS
static int pritimers[PRI_MAX_TIMERS];
#endif
static int pridebugfd = -1;
static char pridebugfilename[1024]="";
#endif

/* Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/* How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

/* How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

static int usecnt =0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of zt_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);


static int ifcount = 0;

#ifdef ZAPATA_PRI
AST_MUTEX_DEFINE_STATIC(pridebugfdlock);
#endif

/* Whether we answer on a Polarity Switch event */
static int answeronpolarityswitch = 0;

/* Whether we hang up on a Polarity Switch event */
static int hanguponpolarityswitch = 0;

/* How long (ms) to ignore Polarity Switch events after we answer a call */
static int polarityonanswerdelay = 600;

/* When to send the CallerID signals (rings) */
static int sendcalleridafter = DEFAULT_CIDRINGS;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

static enum ast_bridge_result zt_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc);

static int zt_sendtext(struct ast_channel *c, const char *text);

static inline int zt_get_event(int fd)
{
	/* Avoid the silly zt_getevent which ignores a bunch of events */
	int j;
	if (ioctl(fd, ZT_GETEVENT, &j) == -1) return -1;
	return j;
}

static inline int zt_wait_event(int fd)
{
	/* Avoid the silly zt_waitevent which ignores a bunch of events */
	int i,j=0;
	i = ZT_IOMUX_SIGEVENT;
	if (ioctl(fd, ZT_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, ZT_GETEVENT, &j) == -1) return -1;
	return j;
}

/* Chunk size to read -- we use 20ms chunks to make things happy.  */   
#define READ_SIZE 160

#define MASK_AVAIL		(1 << 0)		/* Channel available for PRI use */
#define MASK_INUSE		(1 << 1)		/* Channel currently in use */

#define CALLWAITING_SILENT_SAMPLES	( (300 * 8) / READ_SIZE) /* 300 ms */
#define CALLWAITING_REPEAT_SAMPLES	( (10000 * 8) / READ_SIZE) /* 300 ms */
#define CIDCW_EXPIRE_SAMPLES		( (500 * 8) / READ_SIZE) /* 500 ms */
#define MIN_MS_SINCE_FLASH			( (2000) )	/* 2000 ms */
#define DEFAULT_RINGT 				( (8000 * 8) / READ_SIZE)

struct zt_pvt;


#ifdef ZAPATA_R2
static int r2prot = -1;
#endif

static int ringt_base = DEFAULT_RINGT;

#ifdef ZAPATA_PRI

#define PVT_TO_CHANNEL(p) (((p)->prioffset) | ((p)->logicalspan << 8) | (p->pri->mastertrunkgroup ? 0x10000 : 0))
#define PRI_CHANNEL(p) ((p) & 0xff)
#define PRI_SPAN(p) (((p) >> 8) & 0xff)
#define PRI_EXPLICIT(p) (((p) >> 16) & 0x01)

struct zt_pri {
	pthread_t master;						/* Thread of master */
	ast_mutex_t lock;						/* Mutex */
	char idleext[AST_MAX_EXTENSION];				/* Where to idle extra calls */
	char idlecontext[AST_MAX_CONTEXT];				/* What context to use for idle */
	char idledial[AST_MAX_EXTENSION];				/* What to dial before dumping */
	int minunused;							/* Min # of channels to keep empty */
	int minidle;							/* Min # of "idling" calls to keep active */
	int nodetype;							/* Node type */
	int switchtype;							/* Type of switch to emulate */
	int nsf;							/* Network-Specific Facilities */
	int dialplan;							/* Dialing plan */
	int localdialplan;						/* Local dialing plan */
	char internationalprefix[10];					/* country access code ('00' for european dialplans) */
	char nationalprefix[10];					/* area access code ('0' for european dialplans) */
	char localprefix[20];						/* area access code + area code ('0'+area code for european dialplans) */
	char privateprefix[20];						/* for private dialplans */
	char unknownprefix[20];						/* for unknown dialplans */
	int dchannels[NUM_DCHANS];					/* What channel are the dchannels on */
	int trunkgroup;							/* What our trunkgroup is */
	int mastertrunkgroup;						/* What trunk group is our master */
	int prilogicalspan;						/* Logical span number within trunk group */
	int numchans;							/* Num of channels we represent */
	int overlapdial;						/* In overlap dialing mode */
	int facilityenable;						/* Enable facility IEs */
	struct pri *dchans[NUM_DCHANS];					/* Actual d-channels */
	int dchanavail[NUM_DCHANS];					/* Whether each channel is available */
	struct pri *pri;						/* Currently active D-channel */
	int debug;
	int fds[NUM_DCHANS];						/* FD's for d-channels */
	int offset;
	int span;
	int resetting;
	int resetpos;
	time_t lastreset;						/* time when unused channels were last reset */
	long resetinterval;						/* Interval (in seconds) for resetting unused channels */
	struct zt_pvt *pvts[MAX_CHANNELS];				/* Member channel pvt structs */
	struct zt_pvt *crvs;						/* Member CRV structs */
	struct zt_pvt *crvend;						/* Pointer to end of CRV structs */
};


static struct zt_pri pris[NUM_SPANS];

static int pritype = PRI_CPE;

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
#endif

static inline void pri_rel(struct zt_pri *pri)
{
	ast_mutex_unlock(&pri->lock);
}

static int switchtype = PRI_SWITCH_NI2;
static int nsf = PRI_NSF_NONE;
static int dialplan = PRI_NATIONAL_ISDN + 1;
static int localdialplan = PRI_NATIONAL_ISDN + 1;

#else
/* Shut up the compiler */
struct zt_pri;
#endif

#define SUB_REAL		0			/* Active call */
#define SUB_CALLWAIT	1			/* Call-Waiting call on hold */
#define SUB_THREEWAY	2			/* Three-way call */

/* Polarity states */
#define POLARITY_IDLE   0
#define POLARITY_REV    1


static struct zt_distRings drings;

struct distRingData {
	int ring[3];
};
struct ringContextData {
	char contextData[AST_MAX_CONTEXT];
};
struct zt_distRings {
	struct distRingData ringnum[3];
	struct ringContextData ringContext[3];
};

static char *subnames[] = {
	"Real",
	"Callwait",
	"Threeway"
};

struct zt_subchannel {
	int zfd;
	struct ast_channel *owner;
	int chan;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct ast_frame f;		/* One frame for each channel.  How did this ever work before? */
	unsigned int needringing:1;
	unsigned int needbusy:1;
	unsigned int needcongestion:1;
	unsigned int needcallerid:1;
	unsigned int needanswer:1;
	unsigned int needflash:1;
	unsigned int linear:1;
	unsigned int inthreeway:1;
	ZT_CONFINFO curconf;
};

#define CONF_USER_REAL		(1 << 0)
#define CONF_USER_THIRDCALL	(1 << 1)

#define MAX_SLAVES	4

static struct zt_pvt {
	ast_mutex_t lock;
	struct ast_channel *owner;			/* Our current active owner (if applicable) */
							/* Up to three channels can be associated with this call */
		
	struct zt_subchannel sub_unused;		/* Just a safety precaution */
	struct zt_subchannel subs[3];			/* Sub-channels */
	struct zt_confinfo saveconf;			/* Saved conference info */

	struct zt_pvt *slaves[MAX_SLAVES];		/* Slave to us (follows our conferencing) */
	struct zt_pvt *master;				/* Master to us (we follow their conferencing) */
	int inconference;				/* If our real should be in the conference */
	
	int sig;					/* Signalling style */
	int radio;					/* radio type */
	float rxgain;
	float txgain;
	int tonezone;					/* tone zone for this chan, or -1 for default */
	struct zt_pvt *next;				/* Next channel in list */
	struct zt_pvt *prev;				/* Prev channel in list */

	/* flags */
	unsigned int adsi:1;
	unsigned int answeronpolarityswitch:1;
	unsigned int busydetect:1;
	unsigned int callreturn:1;
	unsigned int callwaiting:1;
	unsigned int callwaitingcallerid:1;
	unsigned int cancallforward:1;
	unsigned int canpark:1;
	unsigned int confirmanswer:1;			/* Wait for '#' to confirm answer */
	unsigned int destroy:1;
	unsigned int didtdd:1;				/* flag to say its done it once */
	unsigned int dialednone:1;
	unsigned int dialing:1;
	unsigned int digital:1;
	unsigned int dnd:1;
	unsigned int echobreak:1;
	unsigned int echocanbridged:1;
	unsigned int echocanon:1;
	unsigned int faxhandled:1;			/* Has a fax tone already been handled? */
	unsigned int firstradio:1;
	unsigned int hanguponpolarityswitch:1;
	unsigned int hardwaredtmf:1;
	unsigned int hidecallerid;
	unsigned int ignoredtmf:1;
	unsigned int immediate:1;			/* Answer before getting digits? */
	unsigned int inalarm:1;
	unsigned int mate:1;				/* flag to say its in MATE mode */
	unsigned int outgoing:1;
	unsigned int overlapdial:1;
	unsigned int permcallwaiting:1;
	unsigned int permhidecallerid:1;		/* Whether to hide our outgoing caller ID or not */
	unsigned int priindication_oob:1;
	unsigned int priexclusive:1;
	unsigned int pulse:1;
	unsigned int pulsedial:1;			/* whether a pulse dial phone is detected */
	unsigned int restrictcid:1;			/* Whether restrict the callerid -> only send ANI */
	unsigned int threewaycalling:1;
	unsigned int transfer:1;
	unsigned int use_callerid:1;			/* Whether or not to use caller id on this channel */
	unsigned int use_callingpres:1;			/* Whether to use the callingpres the calling switch sends */
	unsigned int usedistinctiveringdetection:1;
	unsigned int zaptrcallerid:1;			/* should we use the callerid from incoming call on zap transfer or not */
	unsigned int transfertobusy:1;			/* allow flash-transfers to busy channels */
#if defined(ZAPATA_PRI)
	unsigned int alerting:1;
	unsigned int alreadyhungup:1;
	unsigned int isidlecall:1;
	unsigned int resetting:1;
	unsigned int setup_ack:1;
#endif
#if defined(ZAPATA_R2)
	unsigned int hasr2call:1;
	unsigned int r2blocked:1;
	unsigned int sigchecked:1;
#endif

	struct zt_distRings drings;

	char context[AST_MAX_CONTEXT];
	char defcontext[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char musicclass[MAX_MUSICCLASS];
#ifdef PRI_ANI
	char cid_ani[AST_MAX_EXTENSION];
#endif
	char cid_num[AST_MAX_EXTENSION];
	int cid_ton;					/* Type Of Number (TON) */
	char cid_name[AST_MAX_EXTENSION];
	char lastcid_num[AST_MAX_EXTENSION];
	char lastcid_name[AST_MAX_EXTENSION];
	char *origcid_num;				/* malloced original callerid */
	char *origcid_name;				/* malloced original callerid */
	char callwait_num[AST_MAX_EXTENSION];
	char callwait_name[AST_MAX_EXTENSION];
	char rdnis[AST_MAX_EXTENSION];
	char dnid[AST_MAX_EXTENSION];
	unsigned int group;
	int law;
	int confno;					/* Our conference */
	int confusers;					/* Who is using our conference */
	int propconfno;					/* Propagated conference number */
	ast_group_t callgroup;
	ast_group_t pickupgroup;
	int channel;					/* Channel Number or CRV */
	int span;					/* Span number */
	time_t guardtime;				/* Must wait this much time before using for new call */
	int cid_signalling;				/* CID signalling type bell202 or v23 */
	int cid_start;					/* CID start indicator, polarity or ring */
	int callingpres;				/* The value of callling presentation that we're going to use when placing a PRI call */
	int callwaitingrepeat;				/* How many samples to wait before repeating call waiting */
	int cidcwexpire;				/* When to expire our muting for CID/CW */
	unsigned char *cidspill;
	int cidpos;
	int cidlen;
	int ringt;
	int ringt_base;
	int stripmsd;
	int callwaitcas;
	int callwaitrings;
	int echocancel;
	int echotraining;
	char echorest[20];
	int busycount;
	int busy_tonelength;
	int busy_quietlength;
	int callprogress;
	struct timeval flashtime;			/* Last flash-hook time */
	struct ast_dsp *dsp;
	int cref;					/* Call reference number */
	ZT_DIAL_OPERATION dop;
	int whichwink;					/* SIG_FEATDMF_TA Which wink are we on? */
	char finaldial[64];
	char accountcode[AST_MAX_ACCOUNT_CODE];		/* Account code */
	int amaflags;					/* AMA Flags */
	struct tdd_state *tdd;				/* TDD flag */
	char call_forward[AST_MAX_EXTENSION];
	char mailbox[AST_MAX_EXTENSION];
	char dialdest[256];
	int onhooktime;
	int msgstate;
	int distinctivering;				/* Which distinctivering to use */
	int cidrings;					/* Which ring to deliver CID on */
	int dtmfrelax;					/* whether to run in relaxed DTMF mode */
	int fake_event;
	int polarityonanswerdelay;
	struct timeval polaritydelaytv;
	int sendcalleridafter;
#ifdef ZAPATA_PRI
	struct zt_pri *pri;
	struct zt_pvt *bearer;
	struct zt_pvt *realcall;
	q931_call *call;
	int prioffset;
	int logicalspan;
	int proceeding;
	int dsp_features;
#endif	
#ifdef ZAPATA_R2
	int r2prot;
	mfcr2_t *r2;
#endif	
	int polarity;

} *iflist = NULL, *ifend = NULL;

static struct ast_channel *zt_request(const char *type, int format, void *data, int *cause);
static int zt_digit(struct ast_channel *ast, char digit);
static int zt_sendtext(struct ast_channel *c, const char *text);
static int zt_call(struct ast_channel *ast, char *rdest, int timeout);
static int zt_hangup(struct ast_channel *ast);
static int zt_answer(struct ast_channel *ast);
struct ast_frame *zt_read(struct ast_channel *ast);
static int zt_write(struct ast_channel *ast, struct ast_frame *frame);
struct ast_frame *zt_exception(struct ast_channel *ast);
static int zt_indicate(struct ast_channel *chan, int condition);
static int zt_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int zt_setoption(struct ast_channel *chan, int option, void *data, int datalen);

static const struct ast_channel_tech zap_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR | AST_FORMAT_ULAW,
	.requester = zt_request,
	.send_digit = zt_digit,
	.send_text = zt_sendtext,
	.call = zt_call,
	.hangup = zt_hangup,
	.answer = zt_answer,
	.read = zt_read,
	.write = zt_write,
	.bridge = zt_bridge,
	.exception = zt_exception,
	.indicate = zt_indicate,
	.fixup = zt_fixup,
	.setoption = zt_setoption,
};

#ifdef ZAPATA_PRI
#define GET_CHANNEL(p) ((p)->bearer ? (p)->bearer->channel : p->channel)
#else
#define GET_CHANNEL(p) ((p)->channel)
#endif

struct zt_pvt *round_robin[32];

#ifdef ZAPATA_PRI
static inline int pri_grab(struct zt_pvt *pvt, struct zt_pri *pri)
{
	int res;
	/* Grab the lock first */
	do {
	    res = ast_mutex_trylock(&pri->lock);
		if (res) {
			ast_mutex_unlock(&pvt->lock);
			/* Release the lock and try again */
			usleep(1);
			ast_mutex_lock(&pvt->lock);
		}
	} while(res);
	/* Then break the poll */
	pthread_kill(pri->master, SIGURG);
	return 0;
}
#endif

#define NUM_CADENCE_MAX 25
static int num_cadence = 4;
static int user_has_defined_cadences = 0;

static struct zt_ring_cadence cadences[NUM_CADENCE_MAX] = {
	{ { 125, 125, 2000, 4000 } },			/* Quick chirp followed by normal ring */
	{ { 250, 250, 500, 1000, 250, 250, 500, 4000 } }, /* British style ring */
	{ { 125, 125, 125, 125, 125, 4000 } },	/* Three short bursts */
	{ { 1000, 500, 2500, 5000 } },	/* Long ring */
};

int receivedRingT; /* Used to find out what ringtone we are on */

/* cidrings says in which pause to transmit the cid information, where the first pause
 * is 1, the second pause is 2 and so on.
 */

static int cidrings[NUM_CADENCE_MAX] = {
	2,										/* Right after first long ring */
	4,										/* Right after long part */
	3,										/* After third chirp */
	2,										/* Second spell */
};

#define ISTRUNK(p) ((p->sig == SIG_FXSLS) || (p->sig == SIG_FXSKS) || \
			(p->sig == SIG_FXSGS) || (p->sig == SIG_PRI))

#define CANBUSYDETECT(p) (ISTRUNK(p) || (p->sig & (SIG_EM | SIG_EM_E1 | SIG_SF)) /* || (p->sig & __ZT_SIG_FXO) */)
#define CANPROGRESSDETECT(p) (ISTRUNK(p) || (p->sig & (SIG_EM | SIG_EM_E1 | SIG_SF)) /* || (p->sig & __ZT_SIG_FXO) */)

static int zt_get_index(struct ast_channel *ast, struct zt_pvt *p, int nullok)
{
	int res;
	if (p->subs[0].owner == ast)
		res = 0;
	else if (p->subs[1].owner == ast)
		res = 1;
	else if (p->subs[2].owner == ast)
		res = 2;
	else {
		res = -1;
		if (!nullok)
			ast_log(LOG_WARNING, "Unable to get index, and nullok is not asserted\n");
	}
	return res;
}

#ifdef ZAPATA_PRI
static void wakeup_sub(struct zt_pvt *p, int a, struct zt_pri *pri)
#else
static void wakeup_sub(struct zt_pvt *p, int a, void *pri)
#endif
{
	struct ast_frame null = { AST_FRAME_NULL, };
#ifdef ZAPATA_PRI
	if (pri)
		ast_mutex_unlock(&pri->lock);
#endif			
	for (;;) {
		if (p->subs[a].owner) {
			if (ast_mutex_trylock(&p->subs[a].owner->lock)) {
				ast_mutex_unlock(&p->lock);
				usleep(1);
				ast_mutex_lock(&p->lock);
			} else {
				ast_queue_frame(p->subs[a].owner, &null);
				ast_mutex_unlock(&p->subs[a].owner->lock);
				break;
			}
		} else
			break;
	}
#ifdef ZAPATA_PRI
	if (pri)
		ast_mutex_lock(&pri->lock);
#endif			
}

#ifdef ZAPATA_PRI
static void zap_queue_frame(struct zt_pvt *p, struct ast_frame *f, struct zt_pri *pri)
#else
static void zap_queue_frame(struct zt_pvt *p, struct ast_frame *f, void *pri)
#endif
{
	/* We must unlock the PRI to avoid the possibility of a deadlock */
#ifdef ZAPATA_PRI
	if (pri)
		ast_mutex_unlock(&pri->lock);
#endif		
	for (;;) {
		if (p->owner) {
			if (ast_mutex_trylock(&p->owner->lock)) {
				ast_mutex_unlock(&p->lock);
				usleep(1);
				ast_mutex_lock(&p->lock);
			} else {
				ast_queue_frame(p->owner, f);
				ast_mutex_unlock(&p->owner->lock);
				break;
			}
		} else
			break;
	}
#ifdef ZAPATA_PRI
	if (pri)
		ast_mutex_lock(&pri->lock);
#endif		
}

static int restore_gains(struct zt_pvt *p);

static void swap_subs(struct zt_pvt *p, int a, int b)
{
	int tchan;
	int tinthreeway;
	struct ast_channel *towner;

	ast_log(LOG_DEBUG, "Swapping %d and %d\n", a, b);

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
		p->subs[a].owner->fds[0] = p->subs[a].zfd;
	if (p->subs[b].owner) 
		p->subs[b].owner->fds[0] = p->subs[b].zfd;
	wakeup_sub(p, a, NULL);
	wakeup_sub(p, b, NULL);
}

static int zt_open(char *fn)
{
	int fd;
	int isnum;
	int chan = 0;
	int bs;
	int x;
	isnum = 1;
	for (x=0;x<strlen(fn);x++) {
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
		fn = "/dev/zap/channel";
	}
	fd = open(fn, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to open '%s': %s\n", fn, strerror(errno));
		return -1;
	}
	if (chan) {
		if (ioctl(fd, ZT_SPECIFY, &chan)) {
			x = errno;
			close(fd);
			errno = x;
			ast_log(LOG_WARNING, "Unable to specify channel %d: %s\n", chan, strerror(errno));
			return -1;
		}
	}
	bs = READ_SIZE;
	if (ioctl(fd, ZT_SET_BLOCKSIZE, &bs) == -1) return -1;
	return fd;
}

static void zt_close(int fd)
{
	if(fd > 0)
		close(fd);
}

int zt_setlinear(int zfd, int linear)
{
	int res;
	res = ioctl(zfd, ZT_SETLINEAR, &linear);
	if (res)
		return res;
	return 0;
}


int zt_setlaw(int zfd, int law)
{
	int res;
	res = ioctl(zfd, ZT_SETLAW, &law);
	if (res)
		return res;
	return 0;
}

static int alloc_sub(struct zt_pvt *p, int x)
{
	ZT_BUFFERINFO bi;
	int res;
	if (p->subs[x].zfd < 0) {
		p->subs[x].zfd = zt_open("/dev/zap/pseudo");
		if (p->subs[x].zfd > -1) {
			res = ioctl(p->subs[x].zfd, ZT_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
				bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
				bi.numbufs = numbufs;
				res = ioctl(p->subs[x].zfd, ZT_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", x);
				}
			} else 
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", x);
			if (ioctl(p->subs[x].zfd, ZT_CHANNO, &p->subs[x].chan) == 1) {
				ast_log(LOG_WARNING,"Unable to get channel number for pseudo channel on FD %d\n",p->subs[x].zfd);
				zt_close(p->subs[x].zfd);
				p->subs[x].zfd = -1;
				return -1;
			}
			if (option_debug)
				ast_log(LOG_DEBUG, "Allocated %s subchannel on FD %d channel %d\n", subnames[x], p->subs[x].zfd, p->subs[x].chan);
			return 0;
		} else
			ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
		return -1;
	}
	ast_log(LOG_WARNING, "%s subchannel of %d already in use\n", subnames[x], p->channel);
	return -1;
}

static int unalloc_sub(struct zt_pvt *p, int x)
{
	if (!x) {
		ast_log(LOG_WARNING, "Trying to unalloc the real channel %d?!?\n", p->channel);
		return -1;
	}
	ast_log(LOG_DEBUG, "Released sub %d of channel %d\n", x, p->channel);
	if (p->subs[x].zfd > -1) {
		zt_close(p->subs[x].zfd);
	}
	p->subs[x].zfd = -1;
	p->subs[x].linear = 0;
	p->subs[x].chan = 0;
	p->subs[x].owner = NULL;
	p->subs[x].inthreeway = 0;
	p->polarity = POLARITY_IDLE;
	memset(&p->subs[x].curconf, 0, sizeof(p->subs[x].curconf));
	return 0;
}

static int zt_digit(struct ast_channel *ast, char digit)
{
	ZT_DIAL_OPERATION zo;
	struct zt_pvt *p;
	int res = 0;
	int index;
	p = ast->tech_pvt;
	ast_mutex_lock(&p->lock);
	index = zt_get_index(ast, p, 0);
	if ((index == SUB_REAL) && p->owner) {
#ifdef ZAPATA_PRI
		if (p->sig == SIG_PRI && ast->_state == AST_STATE_DIALING && (p->proceeding < 2)) {
			if (p->setup_ack) {
				if (!pri_grab(p, p->pri)) {
					pri_information(p->pri->pri,p->call,digit);
					pri_rel(p->pri);
				} else
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
			} else if (strlen(p->dialdest) < sizeof(p->dialdest) - 1) {
				ast_log(LOG_DEBUG, "Queueing digit '%c' since setup_ack not yet received\n", digit);
				res = strlen(p->dialdest);
				p->dialdest[res++] = digit;
				p->dialdest[res] = '\0';
			}
		} else {
#else
		{
#endif
			zo.op = ZT_DIAL_OP_APPEND;
			zo.dialstr[0] = 'T';
			zo.dialstr[1] = digit;
			zo.dialstr[2] = 0;
			if ((res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &zo)))
				ast_log(LOG_WARNING, "Couldn't dial digit %c\n", digit);
			else
				p->dialing = 1;
		}
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

static char *events[] = {
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
	{ ZT_ALARM_RED, "Red Alarm" },
	{ ZT_ALARM_YELLOW, "Yellow Alarm" },
	{ ZT_ALARM_BLUE, "Blue Alarm" },
	{ ZT_ALARM_RECOVER, "Recovering" },
	{ ZT_ALARM_LOOPBACK, "Loopback" },
	{ ZT_ALARM_NOTOPEN, "Not Open" },
	{ ZT_ALARM_NONE, "None" },
};

static char *alarm2str(int alarm)
{
	int x;
	for (x=0;x<sizeof(alarms) / sizeof(alarms[0]); x++) {
		if (alarms[x].alarm & alarm)
			return alarms[x].name;
	}
	return alarm ? "Unknown Alarm" : "No Alarm";
}

static char *event2str(int event)
{
        static char buf[256];
        if ((event < (sizeof(events) / sizeof(events[0]))) && (event > -1))
                return events[event];
        sprintf(buf, "Event %d", event); /* safe */
        return buf;
}

#ifdef ZAPATA_PRI
static char *dialplan2str(int dialplan)
{
	if (dialplan == -1) {
		return("Dynamically set dialplan in ISDN");
	}
	return(pri_plan2str(dialplan));
}
#endif

#ifdef ZAPATA_R2
static int str2r2prot(char *swtype)
{
    if (!strcasecmp(swtype, "ar"))
        return MFCR2_PROT_ARGENTINA;
    /*endif*/
    if (!strcasecmp(swtype, "cn"))
        return MFCR2_PROT_CHINA;
    /*endif*/
    if (!strcasecmp(swtype, "kr"))
        return MFCR2_PROT_KOREA;
    /*endif*/
    return -1;
}
#endif

static char *zap_sig2str(int sig)
{
	static char buf[256];
	switch(sig) {
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
		return "PRI Signalling";
	case SIG_R2:
		return "R2 Signalling";
	case SIG_SF:
		return "SF (Tone) Signalling Immediate";
	case SIG_SFWINK:
		return "SF (Tone) Signalling Wink";
	case SIG_SF_FEATD:
		return "SF (Tone) Signalling with Feature Group D (DTMF)";
	case SIG_SF_FEATDMF:
		return "SF (Tone) Signalling with Feature Group D (MF)";
	case SIG_SF_FEATB:
		return "SF (Tone) Signalling with Feature Group B (MF)";
	case SIG_GR303FXOKS:
		return "GR-303 Signalling with FXOKS";
	case SIG_GR303FXSKS:
		return "GR-303 Signalling with FXSKS";
	case 0:
		return "Pseudo Signalling";
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d", sig);
		return buf;
	}
}

#define sig2str zap_sig2str

static int conf_add(struct zt_pvt *p, struct zt_subchannel *c, int index, int slavechannel)
{
	/* If the conference already exists, and we're already in it
	   don't bother doing anything */
	ZT_CONFINFO zi;
	
	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;

	if (slavechannel > 0) {
		/* If we have only one slave, do a digital mon */
		zi.confmode = ZT_CONF_DIGITALMON;
		zi.confno = slavechannel;
	} else {
		if (!index) {
			/* Real-side and pseudo-side both participate in conference */
			zi.confmode = ZT_CONF_REALANDPSEUDO | ZT_CONF_TALKER | ZT_CONF_LISTENER |
								ZT_CONF_PSEUDO_TALKER | ZT_CONF_PSEUDO_LISTENER;
		} else
			zi.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
		zi.confno = p->confno;
	}
	if ((zi.confno == c->curconf.confno) && (zi.confmode == c->curconf.confmode))
		return 0;
	if (c->zfd < 0)
		return 0;
	if (ioctl(c->zfd, ZT_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to add %d to conference %d/%d\n", c->zfd, zi.confmode, zi.confno);
		return -1;
	}
	if (slavechannel < 1) {
		p->confno = zi.confno;
	}
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	ast_log(LOG_DEBUG, "Added %d to conference %d/%d\n", c->zfd, c->curconf.confmode, c->curconf.confno);
	return 0;
}

static int isourconf(struct zt_pvt *p, struct zt_subchannel *c)
{
	/* If they're listening to our channel, they're ours */	
	if ((p->channel == c->curconf.confno) && (c->curconf.confmode == ZT_CONF_DIGITALMON))
		return 1;
	/* If they're a talker on our (allocated) conference, they're ours */
	if ((p->confno > 0) && (p->confno == c->curconf.confno) && (c->curconf.confmode & ZT_CONF_TALKER))
		return 1;
	return 0;
}

static int conf_del(struct zt_pvt *p, struct zt_subchannel *c, int index)
{
	ZT_CONFINFO zi;
	if (/* Can't delete if there's no zfd */
		(c->zfd < 0) ||
		/* Don't delete from the conference if it's not our conference */
		!isourconf(p, c)
		/* Don't delete if we don't think it's conferenced at all (implied) */
		) return 0;
	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;
	zi.confno = 0;
	zi.confmode = 0;
	if (ioctl(c->zfd, ZT_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to drop %d from conference %d/%d\n", c->zfd, c->curconf.confmode, c->curconf.confno);
		return -1;
	}
	ast_log(LOG_DEBUG, "Removed %d from conference %d/%d\n", c->zfd, c->curconf.confmode, c->curconf.confno);
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	return 0;
}

static int isslavenative(struct zt_pvt *p, struct zt_pvt **out)
{
	int x;
	int useslavenative;
	struct zt_pvt *slave = NULL;
	/* Start out optimistic */
	useslavenative = 1;
	/* Update conference state in a stateless fashion */
	for (x=0;x<3;x++) {
		/* Any three-way calling makes slave native mode *definitely* out
		   of the question */
		if ((p->subs[x].zfd > -1) && p->subs[x].inthreeway)
			useslavenative = 0;
	}
	/* If we don't have any 3-way calls, check to see if we have
	   precisely one slave */
	if (useslavenative) {
		for (x=0;x<MAX_SLAVES;x++) {
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

static int reset_conf(struct zt_pvt *p)
{
	ZT_CONFINFO zi;
	memset(&zi, 0, sizeof(zi));
	p->confno = -1;
	memset(&p->subs[SUB_REAL].curconf, 0, sizeof(p->subs[SUB_REAL].curconf));
	if (p->subs[SUB_REAL].zfd > -1) {
		if (ioctl(p->subs[SUB_REAL].zfd, ZT_SETCONF, &zi))
			ast_log(LOG_WARNING, "Failed to reset conferencing on channel %d!\n", p->channel);
	}
	return 0;
}

static int update_conf(struct zt_pvt *p)
{
	int needconf = 0;
	int x;
	int useslavenative;
	struct zt_pvt *slave = NULL;

	useslavenative = isslavenative(p, &slave);
	/* Start with the obvious, general stuff */
	for (x=0;x<3;x++) {
		/* Look for three way calls */
		if ((p->subs[x].zfd > -1) && p->subs[x].inthreeway) {
			conf_add(p, &p->subs[x], x, 0);
			needconf++;
		} else {
			conf_del(p, &p->subs[x], x);
		}
	}
	/* If we have a slave, add him to our conference now. or DAX
	   if this is slave native */
	for (x=0;x<MAX_SLAVES;x++) {
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
		   Kill it.  */
		p->confno = -1;
	}
	ast_log(LOG_DEBUG, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);
	return 0;
}

static void zt_enable_ec(struct zt_pvt *p)
{
	int x;
	int res;
	if (!p)
		return;
	if (p->echocanon) {
		ast_log(LOG_DEBUG, "Echo cancellation already on\n");
		return;
	}
	if (p->digital) {
		ast_log(LOG_DEBUG, "Echo cancellation isn't required on digital connection\n");
		return;
	}
	if (p->echocancel) {
		if (p->sig == SIG_PRI) {
			x = 1;
			res = ioctl(p->subs[SUB_REAL].zfd, ZT_AUDIOMODE, &x);
			if (res)
				ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		}
		x = p->echocancel;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		else {
			p->echocanon = 1;
			ast_log(LOG_DEBUG, "Enabled echo cancellation on channel %d\n", p->channel);
		}
	} else
		ast_log(LOG_DEBUG, "No echo cancellation requested\n");
}

static void zt_train_ec(struct zt_pvt *p)
{
	int x;
	int res;
	if (p && p->echocancel && p->echotraining) {
		x = p->echotraining;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_ECHOTRAIN, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to request echo training on channel %d\n", p->channel);
		else {
			ast_log(LOG_DEBUG, "Engaged echo training on channel %d\n", p->channel);
		}
	} else
		ast_log(LOG_DEBUG, "No echo training requested\n");
}

static void zt_disable_ec(struct zt_pvt *p)
{
	int x;
	int res;
	if (p->echocancel) {
		x = 0;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d\n", p->channel);
		else
			ast_log(LOG_DEBUG, "disabled echo cancellation on channel %d\n", p->channel);
	}
	p->echocanon = 0;
}

static void fill_txgain(struct zt_gains *g, float gain, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case ZT_LAW_ALAW:
		for (j = 0; j < (sizeof(g->txgain) / sizeof(g->txgain[0])); j++) {
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
	case ZT_LAW_MULAW:
		for (j = 0; j < (sizeof(g->txgain) / sizeof(g->txgain[0])); j++) {
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

static void fill_rxgain(struct zt_gains *g, float gain, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case ZT_LAW_ALAW:
		for (j = 0; j < (sizeof(g->rxgain) / sizeof(g->rxgain[0])); j++) {
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
	case ZT_LAW_MULAW:
		for (j = 0; j < (sizeof(g->rxgain) / sizeof(g->rxgain[0])); j++) {
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

int set_actual_txgain(int fd, int chan, float gain, int law)
{
	struct zt_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	g.chan = chan;
	res = ioctl(fd, ZT_GETGAINS, &g);
	if (res) {
		ast_log(LOG_DEBUG, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_txgain(&g, gain, law);

	return ioctl(fd, ZT_SETGAINS, &g);
}

int set_actual_rxgain(int fd, int chan, float gain, int law)
{
	struct zt_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	g.chan = chan;
	res = ioctl(fd, ZT_GETGAINS, &g);
	if (res) {
		ast_log(LOG_DEBUG, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_rxgain(&g, gain, law);

	return ioctl(fd, ZT_SETGAINS, &g);
}

int set_actual_gain(int fd, int chan, float rxgain, float txgain, int law)
{
	return set_actual_txgain(fd, chan, txgain, law) | set_actual_rxgain(fd, chan, rxgain, law);
}

static int bump_gains(struct zt_pvt *p)
{
	int res;

	/* Bump receive gain by 5.0db */
	res = set_actual_gain(p->subs[SUB_REAL].zfd, 0, p->rxgain + 5.0, p->txgain, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to bump gain: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int restore_gains(struct zt_pvt *p)
{
	int res;

	res = set_actual_gain(p->subs[SUB_REAL].zfd, 0, p->rxgain, p->txgain, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static inline int zt_set_hook(int fd, int hs)
{
	int x, res;
	x = hs;
	res = ioctl(fd, ZT_HOOK, &x);
	if (res < 0) 
	{
		if (errno == EINPROGRESS) return 0;
		ast_log(LOG_WARNING, "zt hook failed: %s\n", strerror(errno));
	}
	return res;
}

static inline int zt_confmute(struct zt_pvt *p, int muted)
{
	int x, y, res;
	x = muted;
	if (p->sig == SIG_PRI) {
		y = 1;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_AUDIOMODE, &y);
		if (res)
			ast_log(LOG_WARNING, "Unable to set audio mode on '%d'\n", p->channel);
	}
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_CONFMUTE, &x);
	if (res < 0) 
		ast_log(LOG_WARNING, "zt confmute(%d) failed on channel %d: %s\n", muted, p->channel, strerror(errno));
	return res;
}

static int save_conference(struct zt_pvt *p)
{
	struct zt_confinfo c;
	int res;
	if (p->saveconf.confmode) {
		ast_log(LOG_WARNING, "Can't save conference -- already in use\n");
		return -1;
	}
	p->saveconf.chan = 0;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_GETCONF, &p->saveconf);
	if (res) {
		ast_log(LOG_WARNING, "Unable to get conference info: %s\n", strerror(errno));
		p->saveconf.confmode = 0;
		return -1;
	}
	c.chan = 0;
	c.confno = 0;
	c.confmode = ZT_CONF_NORMAL;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "Unable to set conference info: %s\n", strerror(errno));
		return -1;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Disabled conferencing\n");
	return 0;
}

static int restore_conference(struct zt_pvt *p)
{
	int res;
	if (p->saveconf.confmode) {
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_SETCONF, &p->saveconf);
		p->saveconf.confmode = 0;
		if (res) {
			ast_log(LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
			return -1;
		}
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Restored conferencing\n");
	return 0;
}

static int send_callerid(struct zt_pvt *p);

int send_cwcidspill(struct zt_pvt *p)
{
	p->callwaitcas = 0;
	p->cidcwexpire = 0;
	p->cidspill = malloc(MAX_CALLERID_SIZE);
	if (p->cidspill) {
		memset(p->cidspill, 0x7f, MAX_CALLERID_SIZE);
		p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, p->callwait_name, p->callwait_num, AST_LAW(p));
		/* Make sure we account for the end */
		p->cidlen += READ_SIZE * 4;
		p->cidpos = 0;
		send_callerid(p);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "CPE supports Call Waiting Caller*ID.  Sending '%s/%s'\n", p->callwait_name, p->callwait_num);
	} else return -1;
	return 0;
}

static int has_voicemail(struct zt_pvt *p)
{

	return ast_app_has_voicemail(p->mailbox, NULL);
}

static int send_callerid(struct zt_pvt *p)
{
	/* Assumes spill in p->cidspill, p->cidlen in length and we're p->cidpos into it */
	int res;
	/* Take out of linear mode if necessary */
	if (p->subs[SUB_REAL].linear) {
		p->subs[SUB_REAL].linear = 0;
		zt_setlinear(p->subs[SUB_REAL].zfd, 0);
	}
	while(p->cidpos < p->cidlen) {
		res = write(p->subs[SUB_REAL].zfd, p->cidspill + p->cidpos, p->cidlen - p->cidpos);
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
	free(p->cidspill);
	p->cidspill = NULL;
	if (p->callwaitcas) {
		/* Wait for CID/CW to expire */
		p->cidcwexpire = CIDCW_EXPIRE_SAMPLES;
	} else
		restore_conference(p);
	return 0;
}

static int zt_callwait(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->tech_pvt;
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill) {
		ast_log(LOG_WARNING, "Spill already exists?!?\n");
		free(p->cidspill);
	}
	p->cidspill = malloc(2400 /* SAS */ + 680 /* CAS */ + READ_SIZE * 4);
	if (p->cidspill) {
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
	} else {
		ast_log(LOG_WARNING, "Unable to create SAS/CAS spill\n");
		return -1;
	}
	return 0;
}

static int zt_call(struct ast_channel *ast, char *rdest, int timeout)
{
	struct zt_pvt *p = ast->tech_pvt;
	int x, res, index;
	char *c, *n, *l;
#ifdef ZAPATA_PRI
	char *s=NULL;
#endif
	char dest[256]; /* must be same length as p->dialdest */
	ast_mutex_lock(&p->lock);
	ast_copy_string(dest, rdest, sizeof(dest));
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));
	if ((ast->_state == AST_STATE_BUSY)) {
		p->subs[SUB_REAL].needbusy = 1;
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "zt_call called on %s, neither down nor reserved\n", ast->name);
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	p->dialednone = 0;
	if (p->radio)  /* if a radio channel, up immediately */
	{
		/* Special pseudo -- automatically up */
		ast_setstate(ast, AST_STATE_UP); 
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	x = ZT_FLUSH_READ | ZT_FLUSH_WRITE;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_FLUSH, &x);
	if (res)
		ast_log(LOG_WARNING, "Unable to flush input on channel %d\n", p->channel);
	p->outgoing = 1;

	set_actual_gain(p->subs[SUB_REAL].zfd, 0, p->rxgain, p->txgain, p->law);

	switch(p->sig) {
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		if (p->owner == ast) {
			/* Normal ring, on hook */
			
			/* Don't send audio while on hook, until the call is answered */
			p->dialing = 1;
			if (p->use_callerid) {
				/* Generate the Caller-ID spill if desired */
				if (p->cidspill) {
					ast_log(LOG_WARNING, "cidspill already exists??\n");
					free(p->cidspill);
				}
				p->cidspill = malloc(MAX_CALLERID_SIZE);
				p->callwaitcas = 0;
				if (p->cidspill) {
					p->cidlen = ast_callerid_generate(p->cidspill, ast->cid.cid_name, ast->cid.cid_num, AST_LAW(p));
					p->cidpos = 0;
					send_callerid(p);
				} else
					ast_log(LOG_WARNING, "Unable to generate CallerID spill\n");
			}
			/* Choose proper cadence */
			if ((p->distinctivering > 0) && (p->distinctivering <= num_cadence)) {
				if (ioctl(p->subs[SUB_REAL].zfd, ZT_SETCADENCE, &cadences[p->distinctivering-1]))
					ast_log(LOG_WARNING, "Unable to set distinctive ring cadence %d on '%s'\n", p->distinctivering, ast->name);
				p->cidrings = cidrings[p->distinctivering - 1];
			} else {
				if (ioctl(p->subs[SUB_REAL].zfd, ZT_SETCADENCE, NULL))
					ast_log(LOG_WARNING, "Unable to reset default ring on '%s'\n", ast->name);
				p->cidrings = p->sendcalleridafter;
			}


			/* nick@dccinc.com 4/3/03 mods to allow for deferred dialing */
			c = strchr(dest, '/');
			if (c)
				c++;
			if (c && (strlen(c) < p->stripmsd)) {
				ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
				c = NULL;
			}
			if (c) {
				p->dop.op = ZT_DIAL_OP_REPLACE;
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "Tw%s", c);
				ast_log(LOG_DEBUG, "FXO: setup deferred dialstring: %s\n", c);
			} else {
				p->dop.dialstr[0] = '\0';
			}
			x = ZT_RING;
			if (ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x) && (errno != EINPROGRESS)) {
				ast_log(LOG_WARNING, "Unable to ring phone: %s\n", strerror(errno));
				ast_mutex_unlock(&p->lock);
				return -1;
			}
			p->dialing = 1;
		} else {
			/* Call waiting call */
			p->callwaitrings = 0;
			if (ast->cid.cid_num)
				ast_copy_string(p->callwait_num, ast->cid.cid_num, sizeof(p->callwait_num));
			else
				p->callwait_num[0] = '\0';
			if (ast->cid.cid_name)
				ast_copy_string(p->callwait_name, ast->cid.cid_name, sizeof(p->callwait_name));
			else
				p->callwait_name[0] = '\0';
			/* Call waiting tone instead */
			if (zt_callwait(ast)) {
				ast_mutex_unlock(&p->lock);
				return -1;
			}
			/* Make ring-back */
			if (tone_zone_play_tone(p->subs[SUB_CALLWAIT].zfd, ZT_TONE_RINGTONE))
				ast_log(LOG_WARNING, "Unable to generate call-wait ring-back on channel %s\n", ast->name);
				
		}
		n = ast->cid.cid_name;
		l = ast->cid.cid_num;
		if (l)
			ast_copy_string(p->lastcid_num, l, sizeof(p->lastcid_num));
		else
			p->lastcid_num[0] = '\0';
		if (n)
			ast_copy_string(p->lastcid_name, n, sizeof(p->lastcid_name));
		else
			p->lastcid_name[0] = '\0';
		ast_setstate(ast, AST_STATE_RINGING);
		index = zt_get_index(ast, p, 0);
		if (index > -1) {
			p->subs[index].needringing = 1;
		}
		break;
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
	case SIG_EMWINK:
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FEATB:
	case SIG_SFWINK:
	case SIG_SF:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_FEATDMF_TA:
	case SIG_SF_FEATB:
		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = "";
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
#ifdef ZAPATA_PRI
		/* Start the trunk, if not GR-303 */
		if (!p->pri) {
#endif
			x = ZT_START;
			res = ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
			if (res < 0) {
				if (errno != EINPROGRESS) {
					ast_log(LOG_WARNING, "Unable to start channel: %s\n", strerror(errno));
					ast_mutex_unlock(&p->lock);
					return -1;
				}
			}
#ifdef ZAPATA_PRI
		}
#endif
		ast_log(LOG_DEBUG, "Dialing '%s'\n", c);
		p->dop.op = ZT_DIAL_OP_REPLACE;

		c += p->stripmsd;

		switch (p->sig) {
		case SIG_FEATD:
			l = ast->cid.cid_num;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T*%s*%s*", l, c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T**%s*", c);
			break;
		case SIG_FEATDMF:
			l = ast->cid.cid_num;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*00%s#*%s#", l, c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*02#*%s#", c);
			break;
		case SIG_FEATDMF_TA:
		{
			char *cic = NULL, *ozz = NULL;

			/* If you have to go through a Tandem Access point you need to use this */
			ozz = pbx_builtin_getvar_helper(p->owner, "FEATDMF_OZZ");
			if (!ozz)
				ozz = defaultozz;
			cic = pbx_builtin_getvar_helper(p->owner, "FEATDMF_CIC");
			if (!cic)
				cic = defaultcic;
			if (!ozz || !cic) {
				ast_log(LOG_WARNING, "Unable to dial channel of type feature group D MF tandem access without CIC or OZZ set\n");
				ast_mutex_unlock(&p->lock);
				return -1;
			}
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s%s#", ozz, cic);
			snprintf(p->finaldial, sizeof(p->finaldial), "M*%s#", c);
			p->whichwink = 0;
		}
			break;
		case SIG_E911:
			ast_copy_string(p->dop.dialstr, "M*911#", sizeof(p->dop.dialstr));
			break;
		case SIG_FEATB:
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s#", c);
			break;
		default:
			if (p->pulse)
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "P%sw", c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%sw", c);
			break;
		}

		if (p->echotraining && (strlen(p->dop.dialstr) > 4)) {
			memset(p->echorest, 'w', sizeof(p->echorest) - 1);
			strcpy(p->echorest + (p->echotraining / 400) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
			p->echorest[sizeof(p->echorest) - 1] = '\0';
			p->echobreak = 1;
			p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
		} else
			p->echobreak = 0;
		if (!res) {
			if (ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop)) {
				x = ZT_ONHOOK;
				ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
				ast_mutex_unlock(&p->lock);
				return -1;
			}
		} else
			ast_log(LOG_DEBUG, "Deferring dialing...\n");
		p->dialing = 1;
		if (ast_strlen_zero(c))
			p->dialednone = 1;
		ast_setstate(ast, AST_STATE_DIALING);
		break;
	case 0:
		/* Special pseudo -- automatically up*/
		ast_setstate(ast, AST_STATE_UP);
		break;		
	case SIG_PRI:
		/* We'll get it in a moment -- but use dialdest to store pre-setup_ack digits */
		p->dialdest[0] = '\0';
		break;
	default:
		ast_log(LOG_DEBUG, "not yet implemented\n");
		ast_mutex_unlock(&p->lock);
		return -1;
	}
#ifdef ZAPATA_PRI
	if (p->pri) {
		struct pri_sr *sr;
		int pridialplan;
		int dp_strip;
		int prilocaldialplan;
		int ldp_strip;
		int exclusive;

		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = dest;
		if (!p->hidecallerid) {
			l = ast->cid.cid_num;
			n = ast->cid.cid_name;
		} else {
			l = NULL;
			n = NULL;
		}
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		if (p->sig != SIG_FXSKS) {
			p->dop.op = ZT_DIAL_OP_REPLACE;
			s = strchr(c + p->stripmsd, 'w');
			if (s) {
				if (strlen(s) > 1)
					snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%s", s);
				else
					p->dop.dialstr[0] = '\0';
				*s = '\0';
			} else {
				p->dop.dialstr[0] = '\0';
			}
		}
		if (pri_grab(p, p->pri)) {
			ast_log(LOG_WARNING, "Failed to grab PRI!\n");
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		if (!(p->call = pri_new_call(p->pri->pri))) {
			ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
			pri_rel(p->pri);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		if (!(sr = pri_sr_new())) {
			ast_log(LOG_WARNING, "Failed to allocate setup request channel %d\n", p->channel);
			pri_rel(p->pri);
			ast_mutex_unlock(&p->lock);
		}
		if (p->bearer || (p->sig == SIG_FXSKS)) {
			if (p->bearer) {
				ast_log(LOG_DEBUG, "Oooh, I have a bearer on %d (%d:%d)\n", PVT_TO_CHANNEL(p->bearer), p->bearer->logicalspan, p->bearer->channel);
				p->bearer->call = p->call;
			} else
				ast_log(LOG_DEBUG, "I'm being setup with no bearer right now...\n");
			pri_set_crv(p->pri->pri, p->call, p->channel, 0);
		}
		p->digital = IS_DIGITAL(ast->transfercapability);
		/* Add support for exclusive override */
		if (p->priexclusive)
			exclusive = 1;
		else {
		/* otherwise, traditional behavior */
			if (p->pri->nodetype == PRI_NETWORK)
				exclusive = 0;
			else
				exclusive = 1;
		}
		
		pri_sr_set_channel(sr, p->bearer ? PVT_TO_CHANNEL(p->bearer) : PVT_TO_CHANNEL(p), exclusive, 1);
		pri_sr_set_bearer(sr, p->digital ? PRI_TRANS_CAP_DIGITAL : ast->transfercapability, 
					(p->digital ? -1 : 
						((p->law == ZT_LAW_ALAW) ? PRI_LAYER_1_ALAW : PRI_LAYER_1_ULAW)));
		if (p->pri->facilityenable)
			pri_facility_enable(p->pri->pri);

		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Requested transfer capability: 0x%.2x - %s\n", ast->transfercapability, ast_transfercapability2str(ast->transfercapability));
		dp_strip = 0;
 		pridialplan = p->pri->dialplan - 1;
 		if (pridialplan == -2) { /* compute dynamically */
 			if (strncmp(c + p->stripmsd, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
 				dp_strip = strlen(p->pri->internationalprefix);
 				pridialplan = PRI_INTERNATIONAL_ISDN;
 			} else if (strncmp(c + p->stripmsd, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
 				dp_strip = strlen(p->pri->nationalprefix);
 				pridialplan = PRI_NATIONAL_ISDN;
 			} else {
				pridialplan = PRI_LOCAL_ISDN;
 			}
 		}
 		pri_sr_set_called(sr, c + p->stripmsd + dp_strip, pridialplan,  s ? 1 : 0);

		ldp_strip = 0;
		prilocaldialplan = p->pri->localdialplan - 1;
		if ((l != NULL) && (prilocaldialplan == -2)) { /* compute dynamically */
			if (strncmp(l, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
				ldp_strip = strlen(p->pri->internationalprefix);
				prilocaldialplan = PRI_INTERNATIONAL_ISDN;
			} else if (strncmp(l, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
				ldp_strip = strlen(p->pri->nationalprefix);
				prilocaldialplan = PRI_NATIONAL_ISDN;
			} else {
				prilocaldialplan = PRI_LOCAL_ISDN;
			}
		}
		pri_sr_set_caller(sr, l ? (l + ldp_strip) : NULL, n, prilocaldialplan, 
					l ? (p->use_callingpres ? ast->cid.cid_pres : PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN) : 
						 PRES_NUMBER_NOT_AVAILABLE);
		pri_sr_set_redirecting(sr, ast->cid.cid_rdnis, p->pri->localdialplan - 1, PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, PRI_REDIR_UNCONDITIONAL);
		if (pri_setup(p->pri->pri, p->call,  sr)) {
 			ast_log(LOG_WARNING, "Unable to setup call to %s (using %s)\n", 
 						c + p->stripmsd + dp_strip, dialplan2str(p->pri->dialplan));
			pri_rel(p->pri);
			ast_mutex_unlock(&p->lock);
			pri_sr_free(sr);
			return -1;
		}
		pri_sr_free(sr);
		ast_setstate(ast, AST_STATE_DIALING);
		pri_rel(p->pri);
	}
#endif		
	ast_mutex_unlock(&p->lock);
	return 0;
}

static void destroy_zt_pvt(struct zt_pvt **pvt)
{
	struct zt_pvt *p = *pvt;
	/* Remove channel from the list */
	if(p->prev)
		p->prev->next = p->next;
	if(p->next)
		p->next->prev = p->prev;
	ast_mutex_destroy(&p->lock);
	free(p);
	*pvt = NULL;
}

static int destroy_channel(struct zt_pvt *prev, struct zt_pvt *cur, int now)
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
			if (cur->subs[SUB_REAL].zfd > -1) {
				zt_close(cur->subs[SUB_REAL].zfd);
			}
			destroy_zt_pvt(&cur);
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
		if (cur->subs[SUB_REAL].zfd > -1) {
			zt_close(cur->subs[SUB_REAL].zfd);
		}
		destroy_zt_pvt(&cur);
	}
	return 0;
}

#ifdef ZAPATA_PRI
int pri_is_up(struct zt_pri *pri)
{
	int x;
	for (x=0;x<NUM_DCHANS;x++) {
		if (pri->dchanavail[x] == DCHAN_AVAILABLE)
			return 1;
	}
	return 0;
}

int pri_assign_bearer(struct zt_pvt *crv, struct zt_pri *pri, struct zt_pvt *bearer)
{
	bearer->owner = &inuse;
	bearer->realcall = crv;
	crv->subs[SUB_REAL].zfd = bearer->subs[SUB_REAL].zfd;
	if (crv->subs[SUB_REAL].owner)
		crv->subs[SUB_REAL].owner->fds[0] = crv->subs[SUB_REAL].zfd;
	crv->bearer = bearer;
	crv->call = bearer->call;
	crv->pri = pri;
	return 0;
}

static char *pri_order(int level)
{
	switch(level) {
	case 0:
		return "Primary";
	case 1:
		return "Secondary";
	case 2:
		return "Tertiary";
	case 3:
		return "Quaternary";
	default:
		return "<Unknown>";
	}		
}

/* Returns fd of the active dchan */
int pri_active_dchan_fd(struct zt_pri *pri)
{
	int x = -1;

	for (x = 0; x < NUM_DCHANS; x++) {
		if ((pri->dchans[x] == pri->pri))
			break;
	}

	return pri->fds[x];
}

int pri_find_dchan(struct zt_pri *pri)
{
	int oldslot = -1;
	struct pri *old;
	int newslot = -1;
	int x;
	old = pri->pri;
	for(x=0;x<NUM_DCHANS;x++) {
		if ((pri->dchanavail[x] == DCHAN_AVAILABLE) && (newslot < 0))
			newslot = x;
		if (pri->dchans[x] == old) {
			oldslot = x;
		}
	}
	if (newslot < 0) {
		newslot = 0;
		ast_log(LOG_WARNING, "No D-channels available!  Using Primary channel %d as D-channel anyway!\n",
			pri->dchannels[newslot]);
	}
	if (old && (oldslot != newslot))
		ast_log(LOG_NOTICE, "Switching from from d-channel %d to channel %d!\n",
			pri->dchannels[oldslot], pri->dchannels[newslot]);
	pri->pri = pri->dchans[newslot];
	return 0;
}
#endif

static int zt_hangup(struct ast_channel *ast)
{
	int res;
	int index,x, law;
	/*static int restore_gains(struct zt_pvt *p);*/
	struct zt_pvt *p = ast->tech_pvt;
	struct zt_pvt *tmp = NULL;
	struct zt_pvt *prev = NULL;
	ZT_PARAMS par;

	if (option_debug)
		ast_log(LOG_DEBUG, "zt_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	
	ast_mutex_lock(&p->lock);
	
	index = zt_get_index(ast, p, 1);

	if (p->sig == SIG_PRI) {
		x = 1;
		ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
	}

	x = 0;
	zt_confmute(p, 0);
	restore_gains(p);
	if (p->origcid_num) {
		ast_copy_string(p->cid_num, p->origcid_num, sizeof(p->cid_num));
		free(p->origcid_num);
		p->origcid_num = NULL;
	}	
	if (p->origcid_name) {
		ast_copy_string(p->cid_name, p->origcid_name, sizeof(p->cid_name));
		free(p->origcid_name);
		p->origcid_name = NULL;
	}	
	if (p->dsp)
		ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);
	if (p->exten)
		p->exten[0] = '\0';

	ast_log(LOG_DEBUG, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, index, p->subs[SUB_REAL].zfd, p->subs[SUB_CALLWAIT].zfd, p->subs[SUB_THREEWAY].zfd);
	p->ignoredtmf = 0;
	
	if (index > -1) {
		/* Real channel, do some fixup */
		p->subs[index].owner = NULL;
		p->subs[index].needanswer = 0;
		p->subs[index].needflash = 0;
		p->subs[index].needringing = 0;
		p->subs[index].needbusy = 0;
		p->subs[index].needcongestion = 0;
		p->subs[index].linear = 0;
		p->subs[index].needcallerid = 0;
		p->polarity = POLARITY_IDLE;
		zt_setlinear(p->subs[index].zfd, 0);
		if (index == SUB_REAL) {
			if ((p->subs[SUB_CALLWAIT].zfd > -1) && (p->subs[SUB_THREEWAY].zfd > -1)) {
				ast_log(LOG_DEBUG, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[SUB_CALLWAIT].inthreeway) {
					/* We had flipped over to answer a callwait and now it's gone */
					ast_log(LOG_DEBUG, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					unalloc_sub(p, SUB_CALLWAIT);
					p->owner = NULL;
				} else {
					/* The three way hung up, but we still have a call wait */
					ast_log(LOG_DEBUG, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					swap_subs(p, SUB_THREEWAY, SUB_REAL);
					unalloc_sub(p, SUB_THREEWAY);
					if (p->subs[SUB_REAL].inthreeway) {
						/* This was part of a three way call.  Immediately make way for
						   another call */
						ast_log(LOG_DEBUG, "Call was complete, setting owner to former third call\n");
						p->owner = p->subs[SUB_REAL].owner;
					} else {
						/* This call hasn't been completed yet...  Set owner to NULL */
						ast_log(LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
						p->owner = NULL;
					}
					p->subs[SUB_REAL].inthreeway = 0;
				}
			} else if (p->subs[SUB_CALLWAIT].zfd > -1) {
				/* Move to the call-wait and switch back to them. */
				swap_subs(p, SUB_CALLWAIT, SUB_REAL);
				unalloc_sub(p, SUB_CALLWAIT);
				p->owner = p->subs[SUB_REAL].owner;
				if (p->owner->_state != AST_STATE_UP)
					p->subs[SUB_REAL].needanswer = 1;
				if (ast_bridged_channel(p->subs[SUB_REAL].owner))
					ast_moh_stop(ast_bridged_channel(p->subs[SUB_REAL].owner));
			} else if (p->subs[SUB_THREEWAY].zfd > -1) {
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				unalloc_sub(p, SUB_THREEWAY);
				if (p->subs[SUB_REAL].inthreeway) {
					/* This was part of a three way call.  Immediately make way for
					   another call */
					ast_log(LOG_DEBUG, "Call was complete, setting owner to former third call\n");
					p->owner = p->subs[SUB_REAL].owner;
				} else {
					/* This call hasn't been completed yet...  Set owner to NULL */
					ast_log(LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
					p->owner = NULL;
				}
				p->subs[SUB_REAL].inthreeway = 0;
			}
		} else if (index == SUB_CALLWAIT) {
			/* Ditch the holding callwait call, and immediately make it availabe */
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* This is actually part of a three way, placed on hold.  Place the third part
				   on music on hold now */
				if (p->subs[SUB_THREEWAY].owner && ast_bridged_channel(p->subs[SUB_THREEWAY].owner))
					ast_moh_start(ast_bridged_channel(p->subs[SUB_THREEWAY].owner), NULL);
				p->subs[SUB_THREEWAY].inthreeway = 0;
				/* Make it the call wait now */
				swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
				unalloc_sub(p, SUB_THREEWAY);
			} else
				unalloc_sub(p, SUB_CALLWAIT);
		} else if (index == SUB_THREEWAY) {
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				if (p->subs[SUB_CALLWAIT].owner && ast_bridged_channel(p->subs[SUB_CALLWAIT].owner))
					ast_moh_start(ast_bridged_channel(p->subs[SUB_CALLWAIT].owner), NULL);
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
#ifdef ZAPATA_PRI
		p->proceeding = 0;
		p->alerting = 0;
		p->setup_ack = 0;
#endif		
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}

		law = ZT_LAW_DEFAULT;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_SETLAW, &law);
		if (res < 0) 
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default\n", p->channel);
		/* Perform low level hangup if no owner left */
#ifdef ZAPATA_PRI
		if (p->pri) {
			/* Make sure we have a call (or REALLY have a call in the case of a PRI) */
			if (p->call && (!p->bearer || (p->bearer->call == p->call))) {
				if (!pri_grab(p, p->pri)) {
					if (p->alreadyhungup) {
						ast_log(LOG_DEBUG, "Already hungup...  Calling hangup once, and clearing call\n");
						pri_hangup(p->pri->pri, p->call, -1);
						p->call = NULL;
						if (p->bearer) 
							p->bearer->call = NULL;
					} else {
						char *cause = pbx_builtin_getvar_helper(ast,"PRI_CAUSE");
						int icause = ast->hangupcause ? ast->hangupcause : -1;
						ast_log(LOG_DEBUG, "Not yet hungup...  Calling hangup once with icause, and clearing call\n");
						p->alreadyhungup = 1;
						if (p->bearer)
							p->bearer->alreadyhungup = 1;
						if (cause) {
							if (atoi(cause))
								icause = atoi(cause);
						}
						pri_hangup(p->pri->pri, p->call, icause);
					}
					if (res < 0) 
						ast_log(LOG_WARNING, "pri_disconnect failed\n");
					pri_rel(p->pri);			
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
					res = -1;
				}
			} else {
				if (p->bearer)
					ast_log(LOG_DEBUG, "Bearer call is %p, while ours is still %p\n", p->bearer->call, p->call);
				p->call = NULL;
				res = 0;
			}
		}
#endif
#ifdef ZAPATA_R2
		if (p->sig == SIG_R2) {
			if (p->hasr2call) {
				mfcr2_DropCall(p->r2, NULL, UC_NORMAL_CLEARING);
				p->hasr2call = 0;
				res = 0;
			} else
				res = 0;

		}
#endif
		if (p->sig && (p->sig != SIG_PRI) && (p->sig != SIG_R2))
			res = zt_set_hook(p->subs[SUB_REAL].zfd, ZT_ONHOOK);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
		}
		switch(p->sig) {
		case SIG_FXOGS:
		case SIG_FXOLS:
		case SIG_FXOKS:
			res = ioctl(p->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &par);
			if (!res) {
#if 0
				ast_log(LOG_DEBUG, "Hanging up channel %d, offhook = %d\n", p->channel, par.rxisoffhook);
#endif
				/* If they're off hook, try playing congestion */
				if ((par.rxisoffhook) && (!p->radio))
					tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
				else
					tone_zone_play_tone(p->subs[SUB_REAL].zfd, -1);
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
			tone_zone_play_tone(p->subs[SUB_REAL].zfd, -1);
		}
		if (p->cidspill)
			free(p->cidspill);
		if (p->sig)
			zt_disable_ec(p);
		x = 0;
		ast_channel_setoption(ast,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		ast_channel_setoption(ast,AST_OPTION_TDD,&x,sizeof(char),0);
		p->didtdd = 0;
		p->cidspill = NULL;
		p->callwaitcas = 0;
		p->callwaiting = p->permcallwaiting;
		p->hidecallerid = p->permhidecallerid;
		p->dialing = 0;
		p->rdnis[0] = '\0';
		update_conf(p);
		reset_conf(p);
		/* Restore data mode */
		if (p->sig == SIG_PRI) {
			x = 0;
			ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
		}
#ifdef ZAPATA_PRI
		if (p->bearer) {
			ast_log(LOG_DEBUG, "Freeing up bearer channel %d\n", p->bearer->channel);
			/* Free up the bearer channel as well, and
			   don't use its file descriptor anymore */
			update_conf(p->bearer);
			reset_conf(p->bearer);
			p->bearer->owner = NULL;
			p->bearer->realcall = NULL;
			p->bearer = NULL;
			p->subs[SUB_REAL].zfd = -1;
			p->pri = NULL;
		}
#endif
		restart_monitor();
	}


	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	ast->tech_pvt = NULL;
	ast_mutex_unlock(&p->lock);
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);

	ast_mutex_lock(&iflock);
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

static int zt_answer(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->tech_pvt;
	int res=0;
	int index;
	int oldstate = ast->_state;
	ast_setstate(ast, AST_STATE_UP);
	ast_mutex_lock(&p->lock);
	index = zt_get_index(ast, p, 0);
	if (index < 0)
		index = SUB_REAL;
	/* nothing to do if a radio channel */
	if (p->radio) {
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	switch(p->sig) {
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
		p->ringt = 0;
		/* Fall through */
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_EMWINK:
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FEATB:
	case SIG_SF:
	case SIG_SFWINK:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_SF_FEATB:
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Pick up the line */
		ast_log(LOG_DEBUG, "Took %s off hook\n", ast->name);
		if(p->hanguponpolarityswitch) {
			gettimeofday(&p->polaritydelaytv, NULL);
		}
		res =  zt_set_hook(p->subs[SUB_REAL].zfd, ZT_OFFHOOK);
		tone_zone_play_tone(p->subs[index].zfd, -1);
		p->dialing = 0;
		if ((index == SUB_REAL) && p->subs[SUB_THREEWAY].inthreeway) {
			if (oldstate == AST_STATE_RINGING) {
				ast_log(LOG_DEBUG, "Finally swapping real and threeway\n");
				tone_zone_play_tone(p->subs[SUB_THREEWAY].zfd, -1);
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				p->owner = p->subs[SUB_REAL].owner;
			}
		}
		if (p->sig & __ZT_SIG_FXS) {
			zt_enable_ec(p);
			zt_train_ec(p);
		}
		break;
#ifdef ZAPATA_PRI
	case SIG_PRI:
		/* Send a pri acknowledge */
		if (!pri_grab(p, p->pri)) {
			p->proceeding = 2;
			res = pri_answer(p->pri->pri, p->call, 0, !p->digital);
			pri_rel(p->pri);
		} else {
			ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
			res= -1;
		}
		break;
#endif
#ifdef ZAPATA_R2
	case SIG_R2:
		res = mfcr2_AnswerCall(p->r2, NULL);
		if (res)
			ast_log(LOG_WARNING, "R2 Answer call failed :( on %s\n", ast->name);
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

static int zt_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	signed char *scp;
	int x;
	int index;
	struct zt_pvt *p = chan->tech_pvt;

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch(option) {
	case AST_OPTION_TXGAIN:
		scp = (signed char *) data;
		index = zt_get_index(chan, p, 0);
		if (index < 0) {
			ast_log(LOG_WARNING, "No index in TXGAIN?\n");
			return -1;
		}
		ast_log(LOG_DEBUG, "Setting actual tx gain on %s to %f\n", chan->name, p->txgain + (float) *scp);
		return set_actual_txgain(p->subs[index].zfd, 0, p->txgain + (float) *scp, p->law);
	case AST_OPTION_RXGAIN:
		scp = (signed char *) data;
		index = zt_get_index(chan, p, 0);
		if (index < 0) {
			ast_log(LOG_WARNING, "No index in RXGAIN?\n");
			return -1;
		}
		ast_log(LOG_DEBUG, "Setting actual rx gain on %s to %f\n", chan->name, p->rxgain + (float) *scp);
		return set_actual_rxgain(p->subs[index].zfd, 0, p->rxgain + (float) *scp, p->law);
	case AST_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",chan->name);
			ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
			break;
		default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		}
		break;
	case AST_OPTION_TDD:
		/* turn on or off TDD */
		cp = (char *) data;
		p->mate = 0;
		if (!*cp) { /* turn it off */
			ast_log(LOG_DEBUG, "Set option TDD MODE, value: OFF(0) on %s\n",chan->name);
			if (p->tdd) tdd_free(p->tdd);
			p->tdd = 0;
			break;
		}
		ast_log(LOG_DEBUG, "Set option TDD MODE, value: %s(%d) on %s\n",
			(*cp == 2) ? "MATE" : "ON", (int) *cp, chan->name);
		zt_disable_ec(p);
		/* otherwise, turn it on */
		if (!p->didtdd) { /* if havent done it yet */
			unsigned char mybuf[41000],*buf;
			int size,res,fd,len;
			struct pollfd fds[1];

			buf = mybuf;
			memset(buf, 0x7f, sizeof(mybuf)); /* set to silence */
			ast_tdd_gen_ecdisa(buf + 16000, 16000);  /* put in tone */
			len = 40000;
			index = zt_get_index(chan, p, 0);
			if (index < 0) {
				ast_log(LOG_WARNING, "No index in TDD?\n");
				return -1;
			}
			fd = p->subs[index].zfd;
			while(len) {
				if (ast_check_hangup(chan)) return -1;
				size = len;
				if (size > READ_SIZE)
					size = READ_SIZE;
				fds[0].fd = fd;
				fds[0].events = POLLPRI | POLLOUT;
				fds[0].revents = 0;
				res = poll(fds, 1, -1);
				if (!res) {
					ast_log(LOG_DEBUG, "poll (for write) ret. 0 on channel %d\n", p->channel);
					continue;
				}
				/* if got exception */
				if (fds[0].revents & POLLPRI) return -1;
				if (!(fds[0].revents & POLLOUT)) {
					ast_log(LOG_DEBUG, "write fd not ready on channel %d\n", p->channel);
					continue;
				}
				res = write(fd, buf, size);
				if (res != size) {
					if (res == -1) return -1;
					ast_log(LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
					break;
				}
				len -= size;
				buf += size;
			}
			p->didtdd = 1; /* set to have done it now */		
		}
		if (*cp == 2) { /* Mate mode */
			if (p->tdd) tdd_free(p->tdd);
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
		ast_log(LOG_DEBUG, "Set option RELAX DTMF, value: %s(%d) on %s\n",
			*cp ? "ON" : "OFF", (int) *cp, chan->name);
		ast_dsp_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
		break;
	case AST_OPTION_AUDIO_MODE:  /* Set AUDIO mode (or not) */
		cp = (char *) data;
		if (!*cp) {		
			ast_log(LOG_DEBUG, "Set option AUDIO MODE, value: OFF(0) on %s\n", chan->name);
			x = 0;
			zt_disable_ec(p);
		} else {		
			ast_log(LOG_DEBUG, "Set option AUDIO MODE, value: ON(1) on %s\n", chan->name);
			x = 1;
		}
		if (ioctl(p->subs[SUB_REAL].zfd, ZT_AUDIOMODE, &x) == -1)
			ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d\n", p->channel, x);
		break;
	}
	errno = 0;

	return 0;
}

static void zt_unlink(struct zt_pvt *slave, struct zt_pvt *master, int needlock)
{
	/* Unlink a specific slave or all slaves/masters from a given master */
	int x;
	int hasslaves;
	if (!master)
		return;
	if (needlock) {
		ast_mutex_lock(&master->lock);
		if (slave) {
			while(ast_mutex_trylock(&slave->lock)) {
				ast_mutex_unlock(&master->lock);
				usleep(1);
				ast_mutex_lock(&master->lock);
			}
		}
	}
	hasslaves = 0;
	for (x=0;x<MAX_SLAVES;x++) {
		if (master->slaves[x]) {
			if (!slave || (master->slaves[x] == slave)) {
				/* Take slave out of the conference */
				ast_log(LOG_DEBUG, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
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
			for (x=0;x<MAX_SLAVES;x++) {
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

static void zt_link(struct zt_pvt *slave, struct zt_pvt *master) {
	int x;
	if (!slave || !master) {
		ast_log(LOG_WARNING, "Tried to link to/from NULL??\n");
		return;
	}
	for (x=0;x<MAX_SLAVES;x++) {
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
	
	ast_log(LOG_DEBUG, "Making %d slave to master %d at %d\n", slave->channel, master->channel, x);
}

static void disable_dtmf_detect(struct zt_pvt *p)
{
	int val;

	p->ignoredtmf = 1;

#ifdef ZT_TONEDETECT
	val = 0;
	ioctl(p->subs[SUB_REAL].zfd, ZT_TONEDETECT, &val);
#endif		
	
}

static void enable_dtmf_detect(struct zt_pvt *p)
{
	int val;

	p->ignoredtmf = 0;

#ifdef ZT_TONEDETECT
	val = ZT_TONEDETECT_ON | ZT_TONEDETECT_MUTE;
	ioctl(p->subs[SUB_REAL].zfd, ZT_TONEDETECT, &val);
#endif		
}

static enum ast_bridge_result zt_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct ast_channel *who;
	struct zt_pvt *p0, *p1, *op0, *op1;
	struct zt_pvt *master = NULL, *slave = NULL;
	struct ast_frame *f;
	int to;
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

	ast_mutex_lock(&c0->lock);
	ast_mutex_lock(&c1->lock);

	p0 = c0->tech_pvt;
	p1 = c1->tech_pvt;
	/* cant do pseudo-channels here */
	if (!p0 || (!p0->sig) || !p1 || (!p1->sig)) {
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);
		return AST_BRIDGE_FAILED_NOWARN;
	}

	oi0 = zt_get_index(c0, p0, 0);
	oi1 = zt_get_index(c1, p1, 0);
	if ((oi0 < 0) || (oi1 < 0)) {
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);
		return AST_BRIDGE_FAILED;
	}

	op0 = p0 = c0->tech_pvt;
	op1 = p1 = c1->tech_pvt;
	ofd0 = c0->fds[0];
	ofd1 = c1->fds[0];
	oc0 = p0->owner;
	oc1 = p1->owner;

	ast_mutex_lock(&p0->lock);
	if (ast_mutex_trylock(&p1->lock)) {
		/* Don't block, due to potential for deadlock */
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);
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
					oi0, (p0->subs[SUB_CALLWAIT].zfd > -1) ? 1 : 0,
					p0->subs[SUB_REAL].inthreeway, p0->channel,
					oi0, (p1->subs[SUB_CALLWAIT].zfd > -1) ? 1 : 0,
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
	ast_log(LOG_DEBUG, "master: %d, slave: %d, nothingok: %d\n",
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
			ast_log(LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c0->name, c1->name);
			tone_zone_play_tone(p0->subs[oi0].zfd, ZT_TONE_RINGTONE);
			os1 = p1->subs[SUB_REAL].owner->_state;
		} else {
			ast_log(LOG_DEBUG, "Stopping tones on %d/%d talking to %d/%d\n", p0->channel, oi0, p1->channel, oi1);
			tone_zone_play_tone(p0->subs[oi0].zfd, -1);
		}
		if ((oi0 == SUB_THREEWAY) && 
		    p0->subs[SUB_THREEWAY].inthreeway && 
		    p0->subs[SUB_REAL].owner && 
		    p0->subs[SUB_REAL].inthreeway && 
		    (p0->subs[SUB_REAL].owner->_state == AST_STATE_RINGING)) {
			ast_log(LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c1->name, c0->name);
			tone_zone_play_tone(p1->subs[oi1].zfd, ZT_TONE_RINGTONE);
			os0 = p0->subs[SUB_REAL].owner->_state;
		} else {
			ast_log(LOG_DEBUG, "Stopping tones on %d/%d talking to %d/%d\n", p1->channel, oi1, p0->channel, oi0);
			tone_zone_play_tone(p1->subs[oi0].zfd, -1);
		}
		if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL)) {
			if (!p0->echocanbridged || !p1->echocanbridged) {
				/* Disable echo cancellation if appropriate */
				zt_disable_ec(p0);
				zt_disable_ec(p1);
			}
		}
		zt_link(slave, master);
		master->inconference = inconf;
	} else if (!nothingok)
		ast_log(LOG_WARNING, "Can't link %d/%s with %d/%s\n", p0->channel, subnames[oi0], p1->channel, subnames[oi1]);

	update_conf(p0);
	update_conf(p1);
	t0 = p0->subs[SUB_REAL].inthreeway;
	t1 = p1->subs[SUB_REAL].inthreeway;

	ast_mutex_unlock(&p0->lock);
	ast_mutex_unlock(&p1->lock);

	ast_mutex_unlock(&c0->lock);
	ast_mutex_unlock(&c1->lock);

	/* Native bridge failed */
	if ((!master || !slave) && !nothingok) {
		zt_enable_ec(p0);
		zt_enable_ec(p1);
		return AST_BRIDGE_FAILED;
	}
	
	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0))
		disable_dtmf_detect(op0);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1))
		disable_dtmf_detect(op1);

	for (;;) {
		struct ast_channel *c0_priority[2] = {c0, c1};
		struct ast_channel *c1_priority[2] = {c1, c0};

		/* Here's our main loop...  Start by locking things, looking for private parts, 
		   and then balking if anything is wrong */
		ast_mutex_lock(&c0->lock);
		ast_mutex_lock(&c1->lock);
		p0 = c0->tech_pvt;
		p1 = c1->tech_pvt;

		if (op0 == p0)
			i0 = zt_get_index(c0, p0, 1);
		if (op1 == p1)
			i1 = zt_get_index(c1, p1, 1);
		ast_mutex_unlock(&c0->lock);
		ast_mutex_unlock(&c1->lock);

		if ((op0 != p0) ||
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
		    (oi1 != i0)) {
			ast_log(LOG_DEBUG, "Something changed out on %d/%d to %d/%d, returning -3 to restart\n",
				op0->channel, oi0, op1->channel, oi1);
			res = AST_BRIDGE_RETRY;
			goto return_from_bridge;
		}

#ifdef PRI_2BCT
		q931c0 = p0->call;
		q931c1 = p1->call;
		if (p0->transfer && p1->transfer 
		    && q931c0 && q931c1 
		    && !triedtopribridge) {
			pri_channel_bridge(q931c0, q931c1);
			triedtopribridge = 1;
		}
#endif

		to = -1;
		who = ast_waitfor_n(priority ? c0_priority : c1_priority, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Ooh, empty read...\n");
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
			} else if (p1->pulsedial) {
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
		zt_enable_ec(p0);

	if (op1 == p1)
		zt_enable_ec(p1);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0))
		enable_dtmf_detect(op0);

	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1))
		enable_dtmf_detect(op1);

	zt_unlink(slave, master, 1);

	return res;
}

static int zt_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct zt_pvt *p = newchan->tech_pvt;
	int x;
	ast_mutex_lock(&p->lock);
	ast_log(LOG_DEBUG, "New owner for channel %d is %s\n", p->channel, newchan->name);
	if (p->owner == oldchan) {
		p->owner = newchan;
	}
	for (x=0;x<3;x++)
		if (p->subs[x].owner == oldchan) {
			if (!x)
				zt_unlink(NULL, p, 0);
			p->subs[x].owner = newchan;
		}
	if (newchan->_state == AST_STATE_RINGING) 
		zt_indicate(newchan, AST_CONTROL_RINGING);
	update_conf(p);
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int zt_ring_phone(struct zt_pvt *p)
{
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = ZT_ONHOOK;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
	do {
		x = ZT_RING;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
#if 0
		printf("Res: %d, error: %s\n", res, strerror(errno));
#endif						
		if (res) {
			switch(errno) {
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

static void *ss_thread(void *data);

static struct ast_channel *zt_new(struct zt_pvt *, int, int, int, int, int);

static int attempt_transfer(struct zt_pvt *p)
{
	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if (ast_bridged_channel(p->subs[SUB_REAL].owner)) {
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner))
			ast_moh_stop(ast_bridged_channel(p->subs[SUB_THREEWAY].owner));
		if (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[SUB_REAL].owner), AST_CONTROL_RINGING);
		}
		if (p->subs[SUB_REAL].owner->cdr) {
			/* Move CDR from second channel to current one */
			p->subs[SUB_THREEWAY].owner->cdr =
				ast_cdr_append(p->subs[SUB_THREEWAY].owner->cdr, p->subs[SUB_REAL].owner->cdr);
			p->subs[SUB_REAL].owner->cdr = NULL;
		}
		if (ast_bridged_channel(p->subs[SUB_REAL].owner)->cdr) {
			/* Move CDR from second channel's bridge to current one */
			p->subs[SUB_THREEWAY].owner->cdr =
				ast_cdr_append(p->subs[SUB_THREEWAY].owner->cdr, ast_bridged_channel(p->subs[SUB_REAL].owner)->cdr);
			ast_bridged_channel(p->subs[SUB_REAL].owner)->cdr = NULL;
		}
		 if (ast_channel_masquerade(p->subs[SUB_THREEWAY].owner, ast_bridged_channel(p->subs[SUB_REAL].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(p->subs[SUB_REAL].owner)->name, p->subs[SUB_THREEWAY].owner->name);
			return -1;
		}
		/* Orphan the channel after releasing the lock */
		ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
		unalloc_sub(p, SUB_THREEWAY);
	} else if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
		if (p->subs[SUB_REAL].owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[SUB_THREEWAY].owner), AST_CONTROL_RINGING);
		}
		ast_moh_stop(ast_bridged_channel(p->subs[SUB_THREEWAY].owner));
		if (p->subs[SUB_THREEWAY].owner->cdr) {
			/* Move CDR from second channel to current one */
			p->subs[SUB_REAL].owner->cdr = 
				ast_cdr_append(p->subs[SUB_REAL].owner->cdr, p->subs[SUB_THREEWAY].owner->cdr);
			p->subs[SUB_THREEWAY].owner->cdr = NULL;
		}
		if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner)->cdr) {
			/* Move CDR from second channel's bridge to current one */
			p->subs[SUB_REAL].owner->cdr = 
				ast_cdr_append(p->subs[SUB_REAL].owner->cdr, ast_bridged_channel(p->subs[SUB_THREEWAY].owner)->cdr);
			ast_bridged_channel(p->subs[SUB_THREEWAY].owner)->cdr = NULL;
		}
		if (ast_channel_masquerade(p->subs[SUB_REAL].owner, ast_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(p->subs[SUB_THREEWAY].owner)->name, p->subs[SUB_REAL].owner->name);
			return -1;
		}
		/* Three-way is now the REAL */
		swap_subs(p, SUB_THREEWAY, SUB_REAL);
		ast_mutex_unlock(&p->subs[SUB_REAL].owner->lock);
		unalloc_sub(p, SUB_THREEWAY);
		/* Tell the caller not to hangup */
		return 1;
	} else {
		ast_log(LOG_DEBUG, "Neither %s nor %s are in a bridge, nothing to transfer\n",
					p->subs[SUB_REAL].owner->name, p->subs[SUB_THREEWAY].owner->name);
		p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
		return -1;
	}
	return 0;
}

#ifdef ZAPATA_R2
static struct ast_frame *handle_r2_event(struct zt_pvt *p, mfcr2_event_t *e, int index)
{
	struct ast_frame *f;
	f = &p->subs[index].f;
	if (!p->r2) {
		ast_log(LOG_WARNING, "Huh?  No R2 structure :(\n");
		return NULL;
	}
	switch(e->e) {
	case MFCR2_EVENT_BLOCKED:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Channel %d blocked\n", p->channel);
		break;
	case MFCR2_EVENT_UNBLOCKED:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Channel %d unblocked\n", p->channel);
		break;
	case MFCR2_EVENT_CONFIG_ERR:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Config error on channel %d\n", p->channel);
		break;
	case MFCR2_EVENT_RING:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Ring on channel %d\n", p->channel);
		break;
	case MFCR2_EVENT_HANGUP:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Hangup on channel %d\n", p->channel);
		break;
	case MFCR2_EVENT_RINGING:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Ringing on channel %d\n", p->channel);
		break;
	case MFCR2_EVENT_ANSWER:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Answer on channel %d\n", p->channel);
		break;
	case MFCR2_EVENT_HANGUP_ACK:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Hangup ACK on channel %d\n", p->channel);
		break;
	case MFCR2_EVENT_IDLE:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Idle on channel %d\n", p->channel);
		break;
	default:
		ast_log(LOG_WARNING, "Unknown MFC/R2 event %d\n", e->e);
		break;
	}
	return f;
}

static mfcr2_event_t *r2_get_event_bits(struct zt_pvt *p)
{
	int x;
	int res;
	mfcr2_event_t *e;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_GETRXBITS, &x);
	if (res) {
		ast_log(LOG_WARNING, "Unable to check received bits\n");
		return NULL;
	}
	if (!p->r2) {
		ast_log(LOG_WARNING, "Odd, no R2 structure on channel %d\n", p->channel);
		return NULL;
	}
	e = mfcr2_cas_signaling_event(p->r2, x);
	return e;
}
#endif

static int check_for_conference(struct zt_pvt *p)
{
	ZT_CONFINFO ci;
	/* Fine if we already have a master, etc */
	if (p->master || (p->confno > -1))
		return 0;
	memset(&ci, 0, sizeof(ci));
	if (ioctl(p->subs[SUB_REAL].zfd, ZT_GETCONF, &ci)) {
		ast_log(LOG_WARNING, "Failed to get conference info on channel %d\n", p->channel);
		return 0;
	}
	/* If we have no master and don't have a confno, then 
	   if we're in a conference, it's probably a MeetMe room or
	   some such, so don't let us 3-way out! */
	if ((p->subs[SUB_REAL].curconf.confno != ci.confno) || (p->subs[SUB_REAL].curconf.confmode != ci.confmode)) {
		if (option_verbose > 2)	
			ast_verbose(VERBOSE_PREFIX_3 "Avoiding 3-way call when in an external conference\n");
		return 1;
	}
	return 0;
}

static int get_alarms(struct zt_pvt *p)
{
	int res;
	ZT_SPANINFO zi;
	memset(&zi, 0, sizeof(zi));
	zi.spanno = p->span;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_SPANSTAT, &zi);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to determine alarm on channel %d\n", p->channel);
		return 0;
	}
	return zi.alarms;
}
			
static struct ast_frame *zt_handle_event(struct ast_channel *ast)
{
	int res,x;
	int index;
	char *c;
	struct zt_pvt *p = ast->tech_pvt;
	pthread_t threadid;
	pthread_attr_t attr;
	struct ast_channel *chan;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	index = zt_get_index(ast, p, 0);
	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.src = "zt_handle_event";
	p->subs[index].f.data = NULL;
	if (index < 0)
		return &p->subs[index].f;
	if (p->fake_event) {
		res = p->fake_event;
		p->fake_event = 0;
	} else
		res = zt_get_event(p->subs[index].zfd);

	ast_log(LOG_DEBUG, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, index);

	if (res & (ZT_EVENT_PULSEDIGIT | ZT_EVENT_DTMFUP)) {
		if (res & ZT_EVENT_PULSEDIGIT)
			p->pulsedial = 1;
		else
			p->pulsedial = 0;
		ast_log(LOG_DEBUG, "Detected %sdigit '%c'\n", p->pulsedial ? "pulse ": "", res & 0xff);
#ifdef ZAPATA_PRI
		if ((p->proceeding < 2) && p->sig == SIG_PRI && p->pri && p->pri->overlapdial) {
			p->subs[index].f.frametype = AST_FRAME_NULL;
			p->subs[index].f.subclass = 0;
		} else {
#endif
			p->subs[index].f.frametype = AST_FRAME_DTMF;
			p->subs[index].f.subclass = res & 0xff;
#ifdef ZAPATA_PRI
		}
#endif
		/* Unmute conference, return the captured digit */
		zt_confmute(p, 0);
		return &p->subs[index].f;
	}

	if (res & ZT_EVENT_DTMFDOWN) {
		ast_log(LOG_DEBUG, "DTMF Down '%c'\n", res & 0xff);
		p->subs[index].f.frametype = AST_FRAME_NULL;
		p->subs[index].f.subclass = 0;
		zt_confmute(p, 1);
		/* Mute conference, return null frame */
		return &p->subs[index].f;
	}

	switch(res) {
		case ZT_EVENT_BITSCHANGED:
			if (p->sig == SIG_R2) {
#ifdef ZAPATA_R2
				struct ast_frame  *f = &p->subs[index].f;
				mfcr2_event_t *e;
				e = r2_get_event_bits(p);
				if (e)
					f = handle_r2_event(p, e, index);
				return f;
#else				
				break;
#endif
			}
			ast_log(LOG_WARNING, "Recieved bits changed on %s signalling?\n", sig2str(p->sig));
		case ZT_EVENT_PULSE_START:
			/* Stop tone if there's a pulse start and the PBX isn't started */
			if (!ast->pbx)
				tone_zone_play_tone(p->subs[index].zfd, -1);
			break;	
		case ZT_EVENT_DIALCOMPLETE:
			if (p->inalarm) break;
			if (p->radio) break;
			if (ioctl(p->subs[index].zfd,ZT_DIALING,&x) == -1) {
				ast_log(LOG_DEBUG, "ZT_DIALING ioctl failed on %s\n",ast->name);
				return NULL;
			}
			if (!x) { /* if not still dialing in driver */
				zt_enable_ec(p);
				if (p->echobreak) {
					zt_train_ec(p);
					ast_copy_string(p->dop.dialstr, p->echorest, sizeof(p->dop.dialstr));
					p->dop.op = ZT_DIAL_OP_REPLACE;
					res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop);
					p->echobreak = 0;
				} else {
					p->dialing = 0;
					if (p->sig == SIG_E911) {
						/* if thru with dialing after offhook */
						if (ast->_state == AST_STATE_DIALING_OFFHOOK) {
							ast_setstate(ast, AST_STATE_UP);
							p->subs[index].f.frametype = AST_FRAME_CONTROL;
							p->subs[index].f.subclass = AST_CONTROL_ANSWER;
							break;
						} else { /* if to state wait for offhook to dial rest */
							/* we now wait for off hook */
							ast_setstate(ast,AST_STATE_DIALING_OFFHOOK);
						}
					}
					if (ast->_state == AST_STATE_DIALING) {
						if ((p->callprogress & 1) && CANPROGRESSDETECT(p) && p->dsp && p->outgoing) {
							ast_log(LOG_DEBUG, "Done dialing, but waiting for progress detection before doing more...\n");
						} else if (p->confirmanswer || (!p->dialednone && ((p->sig == SIG_EM) || (p->sig == SIG_EM_E1) ||  (p->sig == SIG_EMWINK) || (p->sig == SIG_FEATD) || (p->sig == SIG_FEATDMF) || (p->sig == SIG_E911) || (p->sig == SIG_FEATB) || (p->sig == SIG_SF) || (p->sig == SIG_SFWINK) || (p->sig == SIG_SF_FEATD) || (p->sig == SIG_SF_FEATDMF) || (p->sig == SIG_SF_FEATB)))) {
							ast_setstate(ast, AST_STATE_RINGING);
						} else if (!p->answeronpolarityswitch) {
							ast_setstate(ast, AST_STATE_UP);
							p->subs[index].f.frametype = AST_FRAME_CONTROL;
							p->subs[index].f.subclass = AST_CONTROL_ANSWER;
						}
					}
				}
			}
			break;
		case ZT_EVENT_ALARM:
#ifdef ZAPATA_PRI
			if (p->call) {
				if (p->pri && p->pri->pri) {
					if (!pri_grab(p, p->pri)) {
						pri_hangup(p->pri->pri, p->call, -1);
						pri_destroycall(p->pri->pri, p->call);
						p->call = NULL;
						pri_rel(p->pri);
					} else
						ast_log(LOG_WARNING, "Failed to grab PRI!\n");
				} else
					ast_log(LOG_WARNING, "The PRI Call have not been destroyed\n");
			}
			if (p->owner)
				p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
			if (p->bearer)
				p->bearer->inalarm = 1;
			else
#endif
			p->inalarm = 1;
			res = get_alarms(p);
			ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm2str(res));
			manager_event(EVENT_FLAG_SYSTEM, "Alarm",
								"Alarm: %s\r\n"
								"Channel: %d\r\n",
								alarm2str(res), p->channel);
			/* fall through intentionally */
		case ZT_EVENT_ONHOOK:
			if (p->radio)
			{
				p->subs[index].f.frametype = AST_FRAME_CONTROL;
				p->subs[index].f.subclass = AST_CONTROL_RADIO_UNKEY;
				break;
			}
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				p->onhooktime = time(NULL);
				p->msgstate = -1;
				/* Check for some special conditions regarding call waiting */
				if (index == SUB_REAL) {
					/* The normal line was hung up */
					if (p->subs[SUB_CALLWAIT].owner) {
						/* There's a call waiting call, so ring the phone, but make it unowned in the mean time */
						swap_subs(p, SUB_CALLWAIT, SUB_REAL);
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Channel %d still has (callwait) call, ringing phone\n", p->channel);
						unalloc_sub(p, SUB_CALLWAIT);	
#if 0
						p->subs[index].needanswer = 0;
						p->subs[index].needringing = 0;
#endif						
						p->callwaitingrepeat = 0;
						p->cidcwexpire = 0;
						p->owner = NULL;
						/* Don't start streaming audio yet if the incoming call isn't up yet */
						if (p->subs[SUB_REAL].owner->_state != AST_STATE_UP)
							p->dialing = 1;
						zt_ring_phone(p);
					} else if (p->subs[SUB_THREEWAY].owner) {
						unsigned int mssinceflash;
						/* Here we have to retain the lock on both the main channel, the 3-way channel, and
						   the private structure -- not especially easy or clean */
						while(p->subs[SUB_THREEWAY].owner && ast_mutex_trylock(&p->subs[SUB_THREEWAY].owner->lock)) {
							/* Yuck, didn't get the lock on the 3-way, gotta release everything and re-grab! */
							ast_mutex_unlock(&p->lock);
							ast_mutex_unlock(&ast->lock);
							usleep(1);
							/* We can grab ast and p in that order, without worry.  We should make sure
							   nothing seriously bad has happened though like some sort of bizarre double
							   masquerade! */
							ast_mutex_lock(&ast->lock);
							ast_mutex_lock(&p->lock);
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
						ast_log(LOG_DEBUG, "Last flash was %d ms ago\n", mssinceflash);
						if (mssinceflash < MIN_MS_SINCE_FLASH) {
							/* It hasn't been long enough since the last flashook.  This is probably a bounce on 
							   hanging up.  Hangup both channels now */
							if (p->subs[SUB_THREEWAY].owner)
								ast_queue_hangup(p->subs[SUB_THREEWAY].owner);
							p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
							ast_log(LOG_DEBUG, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
							ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
						} else if ((ast->pbx) || (ast->_state == AST_STATE_UP)) {
							if (p->transfer) {
								/* In any case this isn't a threeway call anymore */
								p->subs[SUB_REAL].inthreeway = 0;
								p->subs[SUB_THREEWAY].inthreeway = 0;
								/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
								if (!p->transfertobusy && ast->_state == AST_STATE_BUSY) {
									ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
									/* Swap subs and dis-own channel */
									swap_subs(p, SUB_THREEWAY, SUB_REAL);
									p->owner = NULL;
									/* Ring the phone */
									zt_ring_phone(p);
								} else {
									if ((res = attempt_transfer(p)) < 0) {
										p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
										if (p->subs[SUB_THREEWAY].owner)
											ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
									} else if (res) {
										/* Don't actually hang up at this point */
										if (p->subs[SUB_THREEWAY].owner)
											ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
										break;
									}
								}
							} else {
								p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
								if (p->subs[SUB_THREEWAY].owner)
									ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
							}
						} else {
							ast_mutex_unlock(&p->subs[SUB_THREEWAY].owner->lock);
							/* Swap subs and dis-own channel */
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							p->owner = NULL;
							/* Ring the phone */
							zt_ring_phone(p);
						}
					}
				} else {
					ast_log(LOG_WARNING, "Got a hangup and my index is %d?\n", index);
				}
				/* Fall through */
			default:
				zt_disable_ec(p);
				return NULL;
			}
			break;
		case ZT_EVENT_RINGOFFHOOK:
			if (p->inalarm) break;
			if (p->radio)
			{
				p->subs[index].f.frametype = AST_FRAME_CONTROL;
				p->subs[index].f.subclass = AST_CONTROL_RADIO_KEY;
				break;
			}
			/* for E911, its supposed to wait for offhook then dial
			   the second half of the dial string */
			if ((p->sig == SIG_E911) && (ast->_state == AST_STATE_DIALING_OFFHOOK)) {
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
				if (ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop)) {
					x = ZT_ONHOOK;
					ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
					ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
					return NULL;
					}
				p->dialing = 1;
				return &p->subs[index].f;
			}
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				switch(ast->_state) {
				case AST_STATE_RINGING:
					zt_enable_ec(p);
					zt_train_ec(p);
					p->subs[index].f.frametype = AST_FRAME_CONTROL;
					p->subs[index].f.subclass = AST_CONTROL_ANSWER;
					/* Make sure it stops ringing */
					zt_set_hook(p->subs[index].zfd, ZT_OFFHOOK);
					ast_log(LOG_DEBUG, "channel %d answered\n", p->channel);
					if (p->cidspill) {
						/* Cancel any running CallerID spill */
						free(p->cidspill);
						p->cidspill = NULL;
					}
					p->dialing = 0;
					p->callwaitcas = 0;
					if (p->confirmanswer) {
						/* Ignore answer if "confirm answer" is enabled */
						p->subs[index].f.frametype = AST_FRAME_NULL;
						p->subs[index].f.subclass = 0;
					} else if (!ast_strlen_zero(p->dop.dialstr)) {
						/* nick@dccinc.com 4/3/03 - fxo should be able to do deferred dialing */
						res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop);
						if (res < 0) {
							ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
							p->dop.dialstr[0] = '\0';
							return NULL;
						} else {
							ast_log(LOG_DEBUG, "Sent FXO deferred digit string: %s\n", p->dop.dialstr);
							p->subs[index].f.frametype = AST_FRAME_NULL;
							p->subs[index].f.subclass = 0;
							p->dialing = 1;
						}
						p->dop.dialstr[0] = '\0';
						ast_setstate(ast, AST_STATE_DIALING);
					} else
						ast_setstate(ast, AST_STATE_UP);
					return &p->subs[index].f;
				case AST_STATE_DOWN:
					ast_setstate(ast, AST_STATE_RING);
					ast->rings = 1;
					p->subs[index].f.frametype = AST_FRAME_CONTROL;
					p->subs[index].f.subclass = AST_CONTROL_OFFHOOK;
					ast_log(LOG_DEBUG, "channel %d picked up\n", p->channel);
					return &p->subs[index].f;
				case AST_STATE_UP:
					/* Make sure it stops ringing */
					zt_set_hook(p->subs[index].zfd, ZT_OFFHOOK);
					/* Okay -- probably call waiting*/
					if (ast_bridged_channel(p->owner))
							ast_moh_stop(ast_bridged_channel(p->owner));
					break;
				case AST_STATE_RESERVED:
					/* Start up dialtone */
					if (has_voicemail(p))
						res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_STUTTER);
					else
						res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_DIALTONE);
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
				ast_log(LOG_DEBUG, "Setting IDLE polarity due "
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
			case SIG_FEATB:
			case SIG_SF:
			case SIG_SFWINK:
			case SIG_SF_FEATD:
			case SIG_SF_FEATDMF:
			case SIG_SF_FEATB:
				if (ast->_state == AST_STATE_PRERING)
					ast_setstate(ast, AST_STATE_RING);
				if ((ast->_state == AST_STATE_DOWN) || (ast->_state == AST_STATE_RING)) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Ring detected\n");
					p->subs[index].f.frametype = AST_FRAME_CONTROL;
					p->subs[index].f.subclass = AST_CONTROL_RING;
				} else if (p->outgoing && ((ast->_state == AST_STATE_RINGING) || (ast->_state == AST_STATE_DIALING))) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Line answered\n");
					if (p->confirmanswer) {
						p->subs[index].f.frametype = AST_FRAME_NULL;
						p->subs[index].f.subclass = 0;
					} else {
						p->subs[index].f.frametype = AST_FRAME_CONTROL;
						p->subs[index].f.subclass = AST_CONTROL_ANSWER;
						ast_setstate(ast, AST_STATE_UP);
					}
				} else if (ast->_state != AST_STATE_RING)
					ast_log(LOG_WARNING, "Ring/Off-hook in strange state %d on channel %d\n", ast->_state, p->channel);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
			}
			break;
#ifdef ZT_EVENT_RINGBEGIN
		case ZT_EVENT_RINGBEGIN:
			switch(p->sig) {
			case SIG_FXSLS:
			case SIG_FXSGS:
			case SIG_FXSKS:
				if (ast->_state == AST_STATE_RING) {
					p->ringt = p->ringt_base;
				}
				break;
			}
			break;
#endif			
		case ZT_EVENT_RINGEROFF:
			if (p->inalarm) break;
			if (p->radio) break;
			ast->rings++;
			if ((ast->rings > p->cidrings) && (p->cidspill)) {
				ast_log(LOG_WARNING, "Didn't finish Caller-ID spill.  Cancelling.\n");
				free(p->cidspill);
				p->cidspill = NULL;
				p->callwaitcas = 0;
			}
			p->subs[index].f.frametype = AST_FRAME_CONTROL;
			p->subs[index].f.subclass = AST_CONTROL_RINGING;
			break;
		case ZT_EVENT_RINGERON:
			break;
		case ZT_EVENT_NOALARM:
			p->inalarm = 0;
#ifdef ZAPATA_PRI
			/* Extremely unlikely but just in case */
			if (p->bearer)
				p->bearer->inalarm = 0;
#endif				
			ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
			manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
								"Channel: %d\r\n", p->channel);
			break;
		case ZT_EVENT_WINKFLASH:
			if (p->inalarm) break;
			if (p->radio) break;
			/* Remember last time we got a flash-hook */
			gettimeofday(&p->flashtime, NULL);
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				ast_log(LOG_DEBUG, "Winkflash, index: %d, normal: %d, callwait: %d, thirdcall: %d\n",
					index, p->subs[SUB_REAL].zfd, p->subs[SUB_CALLWAIT].zfd, p->subs[SUB_THREEWAY].zfd);
				p->callwaitcas = 0;

				if (index != SUB_REAL) {
					ast_log(LOG_WARNING, "Got flash hook with index %d on channel %d?!?\n", index, p->channel);
					goto winkflashdone;
				}
				
				if (p->subs[SUB_CALLWAIT].owner) {
					/* Swap to call-wait */
					swap_subs(p, SUB_REAL, SUB_CALLWAIT);
					tone_zone_play_tone(p->subs[SUB_REAL].zfd, -1);
					p->owner = p->subs[SUB_REAL].owner;
					ast_log(LOG_DEBUG, "Making %s the new owner\n", p->owner->name);
					if (p->owner->_state == AST_STATE_RINGING) {
						ast_setstate(p->owner, AST_STATE_UP);
						p->subs[SUB_REAL].needanswer = 1;
					}
					p->callwaitingrepeat = 0;
					p->cidcwexpire = 0;
					/* Start music on hold if appropriate */
					if (!p->subs[SUB_CALLWAIT].inthreeway && ast_bridged_channel(p->subs[SUB_CALLWAIT].owner))
						ast_moh_start(ast_bridged_channel(p->subs[SUB_CALLWAIT].owner), NULL);
					if (ast_bridged_channel(p->subs[SUB_REAL].owner))
						ast_moh_stop(ast_bridged_channel(p->subs[SUB_REAL].owner));
				} else if (!p->subs[SUB_THREEWAY].owner) {
					char cid_num[256];
					char cid_name[256];

					if (!p->threewaycalling) {
						/* Just send a flash if no 3-way calling */
						p->subs[SUB_REAL].needflash = 1;
						goto winkflashdone;
					} else if (!check_for_conference(p)) {
						if (p->zaptrcallerid && p->owner) {
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
							ast_log(LOG_DEBUG, "Flash when call not up or ringing\n");
								goto winkflashdone;
						}
						if (alloc_sub(p, SUB_THREEWAY)) {
							ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
							goto winkflashdone;
						}
						/* Make new channel */
						chan = zt_new(p, AST_STATE_RESERVED, 0, SUB_THREEWAY, 0, 0);
						if (p->zaptrcallerid) {
							if (!p->origcid_num)
								p->origcid_num = strdup(p->cid_num);
							if (!p->origcid_name)
								p->origcid_name = strdup(p->cid_name);
							ast_copy_string(p->cid_num, cid_num, sizeof(p->cid_num));
							ast_copy_string(p->cid_name, cid_name, sizeof(p->cid_name));
						}
						/* Swap things around between the three-way and real call */
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						/* Disable echo canceller for better dialing */
						zt_disable_ec(p);
						res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_DIALRECALL);
						if (res)
							ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
						p->owner = chan;
						if (!chan) {
							ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", p->channel);
						} else if (ast_pthread_create(&threadid, &attr, ss_thread, chan)) {
							ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
							res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
							zt_enable_ec(p);
							ast_hangup(chan);
						} else {
							if (option_verbose > 2)	
								ast_verbose(VERBOSE_PREFIX_3 "Started three way call on channel %d\n", p->channel);
							/* Start music on hold if appropriate */
							if (ast_bridged_channel(p->subs[SUB_THREEWAY].owner))
								ast_moh_start(ast_bridged_channel(p->subs[SUB_THREEWAY].owner), NULL);
						}		
					}
				} else {
					/* Already have a 3 way call */
					if (p->subs[SUB_THREEWAY].inthreeway) {
						/* Call is already up, drop the last person */
						if (option_debug)
							ast_log(LOG_DEBUG, "Got flash with three way call up, dropping last call on %d\n", p->channel);
						/* If the primary call isn't answered yet, use it */
						if ((p->subs[SUB_REAL].owner->_state != AST_STATE_UP) && (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_UP)) {
							/* Swap back -- we're dropping the real 3-way that isn't finished yet*/
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							p->owner = p->subs[SUB_REAL].owner;
						}
						/* Drop the last call and stop the conference */
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Dropping three-way call on %s\n", p->subs[SUB_THREEWAY].owner->name);
						p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
						p->subs[SUB_REAL].inthreeway = 0;
						p->subs[SUB_THREEWAY].inthreeway = 0;
					} else {
						/* Lets see what we're up to */
						if (((ast->pbx) || (ast->_state == AST_STATE_UP)) && 
						    (p->transfertobusy || (ast->_state != AST_STATE_BUSY))) {
							int otherindex = SUB_THREEWAY;

							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Building conference on call on %s and %s\n", p->subs[SUB_THREEWAY].owner->name, p->subs[SUB_REAL].owner->name);
							/* Put them in the threeway, and flip */
							p->subs[SUB_THREEWAY].inthreeway = 1;
							p->subs[SUB_REAL].inthreeway = 1;
							if (ast->_state == AST_STATE_UP) {
								swap_subs(p, SUB_THREEWAY, SUB_REAL);
								otherindex = SUB_REAL;
							}
							if (p->subs[otherindex].owner && ast_bridged_channel(p->subs[otherindex].owner))
								ast_moh_stop(ast_bridged_channel(p->subs[otherindex].owner));
							p->owner = p->subs[SUB_REAL].owner;
							if (ast->_state == AST_STATE_RINGING) {
								ast_log(LOG_DEBUG, "Enabling ringtone on real and threeway\n");
								res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_RINGTONE);
								res = tone_zone_play_tone(p->subs[SUB_THREEWAY].zfd, ZT_TONE_RINGTONE);
							}
						} else {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Dumping incomplete call on on %s\n", p->subs[SUB_THREEWAY].owner->name);
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
							p->owner = p->subs[SUB_REAL].owner;
							if (p->subs[SUB_REAL].owner && ast_bridged_channel(p->subs[SUB_REAL].owner))
								ast_moh_stop(ast_bridged_channel(p->subs[SUB_REAL].owner));
							zt_enable_ec(p);
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
					ast_log(LOG_DEBUG, "Ignoring wink on channel %d\n", p->channel);
				else
					ast_log(LOG_DEBUG, "Got wink in weird state %d on channel %d\n", ast->_state, p->channel);
				break;
			case SIG_FEATDMF_TA:
				switch (p->whichwink) {
				case 0:
					ast_log(LOG_DEBUG, "ANI2 set to '%d' and ANI is '%s'\n", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
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
			case SIG_FEATB:
			case SIG_SF_FEATDMF:
			case SIG_SF_FEATB:
				/* FGD MF *Must* wait for wink */
				if (!ast_strlen_zero(p->dop.dialstr))
					res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop);
				else if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else 
					ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
				p->dop.dialstr[0] = '\0';
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
			}
			break;
		case ZT_EVENT_HOOKCOMPLETE:
			if (p->inalarm) break;
			if (p->radio) break;
			switch(p->sig) {
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
				if (!ast_strlen_zero(p->dop.dialstr)) 
					res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop);
				else if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else 
					ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
				p->dop.dialstr[0] = '\0';
				p->dop.op = ZT_DIAL_OP_REPLACE;
				break;
			case SIG_FEATDMF:
			case SIG_E911:
			case SIG_FEATB:
			case SIG_SF_FEATDMF:
			case SIG_SF_FEATB:
				ast_log(LOG_DEBUG, "Got hook complete in MF FGD, waiting for wink now on channel %d\n",p->channel);
				break;
			default:
				break;
			}
			break;
		case ZT_EVENT_POLARITY:
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
                                        ast_log(LOG_DEBUG, "Answering on polarity switch!\n");
                                        ast_setstate(p->owner, AST_STATE_UP);
                                } else
                                        ast_log(LOG_DEBUG, "Ignore switch to REVERSED Polarity on channel %d, state %d\n", p->channel, ast->_state);
			} 
			/* Removed else statement from here as it was preventing hangups from ever happening*/
			/* Added AST_STATE_RING in if statement below to deal with calling party hangups that take place when ringing */
			if(p->hanguponpolarityswitch &&
				(p->polarityonanswerdelay > 0) &&
			       (p->polarity == POLARITY_REV) &&
				((ast->_state == AST_STATE_UP) || (ast->_state == AST_STATE_RING)) ) {
                                /* Added log_debug information below to provide a better indication of what is going on */
				ast_log(LOG_DEBUG, "Polarity Reversal event occured - DEBUG 1: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %d\n", p->channel, ast->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
			
				if(ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay) {
					ast_log(LOG_DEBUG, "Polarity Reversal detected and now Hanging up on channel %d\n", p->channel);
					ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
					p->polarity = POLARITY_IDLE;
				} else {
					ast_log(LOG_DEBUG, "Polarity Reversal detected but NOT hanging up (too close to answer event) on channel %d, state %d\n", p->channel, ast->_state);
				}
			} else {
				p->polarity = POLARITY_IDLE;
				ast_log(LOG_DEBUG, "Ignoring Polarity switch to IDLE on channel %d, state %d\n", p->channel, ast->_state);
			}
                     	/* Added more log_debug information below to provide a better indication of what is going on */
			ast_log(LOG_DEBUG, "Polarity Reversal event occured - DEBUG 2: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %d\n", p->channel, ast->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
			break;
		default:
			ast_log(LOG_DEBUG, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[index].f;
}

static struct ast_frame *__zt_exception(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->tech_pvt;
	int res;
	int usedindex=-1;
	int index;
	struct ast_frame *f;


	index = zt_get_index(ast, p, 1);
	
	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.subclass = 0;
	p->subs[index].f.delivery = ast_tv(0,0);
	p->subs[index].f.src = "zt_exception";
	p->subs[index].f.data = NULL;
	
	
	if ((!p->owner) && (!p->radio)) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		if (p->fake_event) {
			res = p->fake_event;
			p->fake_event = 0;
		} else
			res = zt_get_event(p->subs[SUB_REAL].zfd);
		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != ZT_EVENT_RINGEROFF) && (res != ZT_EVENT_RINGERON) &&
			(res != ZT_EVENT_HOOKCOMPLETE)) {
			ast_log(LOG_DEBUG, "Restoring owner of channel %d on event %d\n", p->channel, res);
			p->owner = p->subs[SUB_REAL].owner;
			if (p->owner && ast_bridged_channel(p->owner))
				ast_moh_stop(ast_bridged_channel(p->owner));
		}
		switch(res) {
		case ZT_EVENT_ONHOOK:
			zt_disable_ec(p);
			if (p->owner) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has call, ringing phone\n", p->owner->name);
				zt_ring_phone(p);
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		case ZT_EVENT_RINGOFFHOOK:
			zt_set_hook(p->subs[SUB_REAL].zfd, ZT_OFFHOOK);
			if (p->owner && (p->owner->_state == AST_STATE_RINGING)) {
				p->subs[SUB_REAL].needanswer = 1;
				p->dialing = 0;
			}
			break;
		case ZT_EVENT_HOOKCOMPLETE:
		case ZT_EVENT_RINGERON:
		case ZT_EVENT_RINGEROFF:
			/* Do nothing */
			break;
		case ZT_EVENT_WINKFLASH:
			gettimeofday(&p->flashtime, NULL);
			if (p->owner) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Channel %d flashed to other channel %s\n", p->channel, p->owner->name);
				if (p->owner->_state != AST_STATE_UP) {
					/* Answer if necessary */
					usedindex = zt_get_index(p->owner, p, 0);
					if (usedindex > -1) {
						p->subs[usedindex].needanswer = 1;
					}
					ast_setstate(p->owner, AST_STATE_UP);
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				if (ast_bridged_channel(p->owner))
					ast_moh_stop(ast_bridged_channel(p->owner));
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		f = &p->subs[index].f;
		return f;
	}
	if (!p->radio) ast_log(LOG_DEBUG, "Exception on %d, channel %d\n", ast->fds[0],p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast->name, p->owner->name);
		f = &p->subs[index].f;
		return f;
	}
	f = zt_handle_event(ast);
	return f;
}

struct ast_frame *zt_exception(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->tech_pvt;
	struct ast_frame *f;
	ast_mutex_lock(&p->lock);
	f = __zt_exception(ast);
	ast_mutex_unlock(&p->lock);
	return f;
}

struct ast_frame  *zt_read(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->tech_pvt;
	int res;
	int index;
	void *readbuf;
	struct ast_frame *f;
	

	ast_mutex_lock(&p->lock);
	
	index = zt_get_index(ast, p, 0);
	
	/* Hang up if we don't really exist */
	if (index < 0)	{
		ast_log(LOG_WARNING, "We dont exist?\n");
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	
	if (p->radio && p->inalarm) return NULL;

	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.subclass = 0;
	p->subs[index].f.delivery = ast_tv(0,0);
	p->subs[index].f.src = "zt_read";
	p->subs[index].f.data = NULL;
	
	/* make sure it sends initial key state as first frame */
	if (p->radio && (!p->firstradio))
	{
		ZT_PARAMS ps;

		ps.channo = p->channel;
		if (ioctl(p->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &ps) < 0) {
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		p->firstradio = 1;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		if (ps.rxisoffhook)
		{
			p->subs[index].f.subclass = AST_CONTROL_RADIO_KEY;
		}
		else
		{
			p->subs[index].f.subclass = AST_CONTROL_RADIO_UNKEY;
		}
		ast_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}
	if (p->ringt == 1) {
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	else if (p->ringt > 0) 
		p->ringt--;

	if (p->subs[index].needringing) {
		/* Send ringing frame if requested */
		p->subs[index].needringing = 0;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_RINGING;
		ast_setstate(ast, AST_STATE_RINGING);
		ast_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}

	if (p->subs[index].needbusy) {
		/* Send busy frame if requested */
		p->subs[index].needbusy = 0;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_BUSY;
		ast_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}

	if (p->subs[index].needcongestion) {
		/* Send congestion frame if requested */
		p->subs[index].needcongestion = 0;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_CONGESTION;
		ast_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}

	if (p->subs[index].needcallerid) {
		ast_set_callerid(ast, !ast_strlen_zero(p->lastcid_num) ? p->lastcid_num : NULL, 
							!ast_strlen_zero(p->lastcid_name) ? p->lastcid_name : NULL,
							!ast_strlen_zero(p->lastcid_num) ? p->lastcid_num : NULL
							);
		p->subs[index].needcallerid = 0;
	}
	
	if (p->subs[index].needanswer) {
		/* Send answer frame if requested */
		p->subs[index].needanswer = 0;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_ANSWER;
		ast_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}	
	
	if (p->subs[index].needflash) {
		/* Send answer frame if requested */
		p->subs[index].needflash = 0;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_FLASH;
		ast_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}	
	
	if (ast->rawreadformat == AST_FORMAT_SLINEAR) {
		if (!p->subs[index].linear) {
			p->subs[index].linear = 1;
			res = zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
			if (res) 
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, index);
		}
	} else if ((ast->rawreadformat == AST_FORMAT_ULAW) ||
		   (ast->rawreadformat == AST_FORMAT_ALAW)) {
		if (p->subs[index].linear) {
			p->subs[index].linear = 0;
			res = zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
			if (res) 
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to companded mode.\n", p->channel, index);
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_getformatname(ast->rawreadformat));
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->subs[index].buffer) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	res = read(p->subs[index].zfd, readbuf, p->subs[index].linear ? READ_SIZE * 2 : READ_SIZE);
	ast_clear_flag(ast, AST_FLAG_BLOCKING);
	/* Check for hangup */
	if (res < 0) {
		f = NULL;
		if (res == -1)  {
			if (errno == EAGAIN) {
				/* Return "NULL" frame if there is nobody there */
				ast_mutex_unlock(&p->lock);
				return &p->subs[index].f;
			} else if (errno == ELAST) {
				f = __zt_exception(ast);
			} else
				ast_log(LOG_WARNING, "zt_rec: %s\n", strerror(errno));
		}
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (res != (p->subs[index].linear ? READ_SIZE * 2 : READ_SIZE)) {
		ast_log(LOG_DEBUG, "Short read (%d/%d), must be an event...\n", res, p->subs[index].linear ? READ_SIZE * 2 : READ_SIZE);
		f = __zt_exception(ast);
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (p->tdd) { /* if in TDD mode, see if we receive that */
		int c;

		c = tdd_feed(p->tdd,readbuf,READ_SIZE);
		if (c < 0) {
			ast_log(LOG_DEBUG,"tdd_feed failed\n");
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		if (c) { /* if a char to return */
			p->subs[index].f.subclass = 0;
			p->subs[index].f.frametype = AST_FRAME_TEXT;
			p->subs[index].f.mallocd = 0;
			p->subs[index].f.offset = AST_FRIENDLY_OFFSET;
			p->subs[index].f.data = p->subs[index].buffer + AST_FRIENDLY_OFFSET;
			p->subs[index].f.datalen = 1;
			*((char *) p->subs[index].f.data) = c;
			ast_mutex_unlock(&p->lock);
			return &p->subs[index].f;
		}
	}
	if (p->callwaitingrepeat)
		p->callwaitingrepeat--;
	if (p->cidcwexpire)
		p->cidcwexpire--;
	/* Repeat callwaiting */
	if (p->callwaitingrepeat == 1) {
		p->callwaitrings++;
		zt_callwait(ast);
	}
	/* Expire CID/CW */
	if (p->cidcwexpire == 1) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "CPE does not support Call Waiting Caller*ID.\n");
		restore_conference(p);
	}
	if (p->subs[index].linear) {
		p->subs[index].f.datalen = READ_SIZE * 2;
	} else 
		p->subs[index].f.datalen = READ_SIZE;

	/* Handle CallerID Transmission */
	if ((p->owner == ast) && p->cidspill &&((ast->_state == AST_STATE_UP) || (ast->rings == p->cidrings))) {
		send_callerid(p);
	}

	p->subs[index].f.frametype = AST_FRAME_VOICE;
	p->subs[index].f.subclass = ast->rawreadformat;
	p->subs[index].f.samples = READ_SIZE;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = AST_FRIENDLY_OFFSET;
	p->subs[index].f.data = p->subs[index].buffer + AST_FRIENDLY_OFFSET/2;
#if 0
	ast_log(LOG_DEBUG, "Read %d of voice on %s\n", p->subs[index].f.datalen, ast->name);
#endif	
	if (p->dialing || /* Transmitting something */
	   (index && (ast->_state != AST_STATE_UP)) || /* Three-way or callwait that isn't up */
	   ((index == SUB_CALLWAIT) && !p->subs[SUB_CALLWAIT].inthreeway) /* Inactive and non-confed call-wait */
	   ) {
		/* Whoops, we're still dialing, or in a state where we shouldn't transmit....
		   don't send anything */
		p->subs[index].f.frametype = AST_FRAME_NULL;
		p->subs[index].f.subclass = 0;
		p->subs[index].f.samples = 0;
		p->subs[index].f.mallocd = 0;
		p->subs[index].f.offset = 0;
		p->subs[index].f.data = NULL;
		p->subs[index].f.datalen= 0;
	}
	if (p->dsp && (!p->ignoredtmf || p->callwaitcas || p->busydetect  || p->callprogress) && !index) {
		/* Perform busy detection. etc on the zap line */
		f = ast_dsp_process(ast, p->dsp, &p->subs[index].f);
		if (f) {
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_BUSY)) {
				if ((ast->_state == AST_STATE_UP) && !p->outgoing) {
					/* Treat this as a "hangup" instead of a "busy" on the assumption that
					   a busy  */
					f = NULL;
				}
			} else if (f->frametype == AST_FRAME_DTMF) {
#ifdef ZAPATA_PRI
				if ((p->proceeding < 2) && p->sig==SIG_PRI && p->pri && p->pri->overlapdial) {
					/* Don't accept in-band DTMF when in overlap dial mode */
					f->frametype = AST_FRAME_NULL;
					f->subclass = 0;
				}
#endif				
				/* DSP clears us of being pulse */
				p->pulsedial = 0;
			}
		}
	} else 
		f = &p->subs[index].f; 
	if (f && (f->frametype == AST_FRAME_DTMF)) {
		ast_log(LOG_DEBUG, "DTMF digit: %c on %s\n", f->subclass, ast->name);
		if (p->confirmanswer) {
			ast_log(LOG_DEBUG, "Confirm answer on %s!\n", ast->name);
			/* Upon receiving a DTMF digit, consider this an answer confirmation instead
			   of a DTMF digit */
			p->subs[index].f.frametype = AST_FRAME_CONTROL;
			p->subs[index].f.subclass = AST_CONTROL_ANSWER;
			f = &p->subs[index].f;
			/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
			p->confirmanswer = 0;
		} else if (p->callwaitcas) {
			if ((f->subclass == 'A') || (f->subclass == 'D')) {
				ast_log(LOG_DEBUG, "Got some DTMF, but it's for the CAS\n");
				if (p->cidspill)
					free(p->cidspill);
				send_cwcidspill(p);
			}
			if ((f->subclass != 'm') && (f->subclass != 'u')) 
				p->callwaitcas = 0;
			p->subs[index].f.frametype = AST_FRAME_NULL;
			p->subs[index].f.subclass = 0;
			f = &p->subs[index].f;
		} else if (f->subclass == 'f') {
			/* Fax tone -- Handle and return NULL */
			if (!p->faxhandled) {
				p->faxhandled++;
				if (strcmp(ast->exten, "fax")) {
					const char *target_context = ast_strlen_zero(ast->macrocontext) ? ast->context : ast->macrocontext;

					if (ast_exists_extension(ast, target_context, "fax", 1, ast->cid.cid_num)) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension\n", ast->name);
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast->exten);
						if (ast_async_goto(ast, target_context, "fax", 1))
							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, target_context);
					} else
						ast_log(LOG_NOTICE, "Fax detected, but no fax extension\n");
				} else
					ast_log(LOG_DEBUG, "Already in a fax extension, not redirecting\n");
			} else
					ast_log(LOG_DEBUG, "Fax already handled\n");
			zt_confmute(p, 0);
			p->subs[index].f.frametype = AST_FRAME_NULL;
			p->subs[index].f.subclass = 0;
			f = &p->subs[index].f;
		} else if (f->subclass == 'm') {
			/* Confmute request */
			zt_confmute(p, 1);
			p->subs[index].f.frametype = AST_FRAME_NULL;
			p->subs[index].f.subclass = 0;
			f = &p->subs[index].f;		
		} else if (f->subclass == 'u') {
			/* Unmute */
			zt_confmute(p, 0);
			p->subs[index].f.frametype = AST_FRAME_NULL;
			p->subs[index].f.subclass = 0;
			f = &p->subs[index].f;		
		} else
			zt_confmute(p, 0);
	}

	/* If we have a fake_event, trigger exception to handle it */
	if (p->fake_event)
		ast_set_flag(ast, AST_FLAG_EXCEPTION);

	ast_mutex_unlock(&p->lock);
	return f;
}

static int my_zt_write(struct zt_pvt *p, unsigned char *buf, int len, int index, int linear)
{
	int sent=0;
	int size;
	int res;
	int fd;
	fd = p->subs[index].zfd;
	while(len) {
		size = len;
		if (size > (linear ? READ_SIZE * 2 : READ_SIZE))
			size = (linear ? READ_SIZE * 2 : READ_SIZE);
		res = write(fd, buf, size);
		if (res != size) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			return sent;
		}
		len -= size;
		buf += size;
	}
	return sent;
}

static int zt_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct zt_pvt *p = ast->tech_pvt;
	int res;
	unsigned char outbuf[4096];
	int index;
	index = zt_get_index(ast, p, 0);
	if (index < 0) {
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast->name);
		return -1;
	}

#if 0
#ifdef ZAPATA_PRI
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
	if (p->dialing) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Dropping frame since I'm still dialing on %s...\n",ast->name);
		return 0;
	}
	if (!p->owner) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Dropping frame since there is no active owner on %s...\n",ast->name);
		return 0;
	}
	if (p->cidspill) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Dropping frame since I've still got a callerid spill\n");
		return 0;
	}
	/* Return if it's not valid data */
	if (!frame->data || !frame->datalen)
		return 0;
	if (frame->datalen > sizeof(outbuf) * 2) {
		ast_log(LOG_WARNING, "Frame too large\n");
		return 0;
	}

	if (frame->subclass == AST_FORMAT_SLINEAR) {
		if (!p->subs[index].linear) {
			p->subs[index].linear = 1;
			res = zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
		res = my_zt_write(p, (unsigned char *)frame->data, frame->datalen, index, 1);
	} else {
		/* x-law already */
		if (p->subs[index].linear) {
			p->subs[index].linear = 0;
			res = zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set companded mode on channel %d\n", p->channel);
		}
		res = my_zt_write(p, (unsigned char *)frame->data, frame->datalen, index, 0);
	}
	if (res < 0) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	} 
	return 0;
}

static int zt_indicate(struct ast_channel *chan, int condition)
{
	struct zt_pvt *p = chan->tech_pvt;
	int res=-1;
	int index;
	int func = ZT_FLASH;
	ast_mutex_lock(&p->lock);
	index = zt_get_index(chan, p, 0);
	ast_log(LOG_DEBUG, "Requested indication %d on channel %s\n", condition, chan->name);
	if (index == SUB_REAL) {
		switch(condition) {
		case AST_CONTROL_BUSY:
#ifdef ZAPATA_PRI
			if (p->priindication_oob && p->sig == SIG_PRI) {
				chan->hangupcause = AST_CAUSE_USER_BUSY;
				chan->_softhangup |= AST_SOFTHANGUP_DEV;
				res = 0;
			} else if (!p->proceeding && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
						pri_rel(p->pri);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->proceeding=1;
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_BUSY);
			} else
#endif
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_BUSY);
			break;
		case AST_CONTROL_RINGING:
#ifdef ZAPATA_PRI
			if ((!p->alerting) && p->sig==SIG_PRI && p->pri && !p->outgoing && (chan->_state != AST_STATE_UP)) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_acknowledge(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
						pri_rel(p->pri);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->alerting=1;
			}
#endif
			res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_RINGTONE);
			if (chan->_state != AST_STATE_UP) {
				if ((chan->_state != AST_STATE_RING) ||
					((p->sig != SIG_FXSKS) &&
					 (p->sig != SIG_FXSLS) &&
					 (p->sig != SIG_FXSGS)))
					ast_setstate(chan, AST_STATE_RINGING);
			}
            break;
		case AST_CONTROL_PROCEEDING:
			ast_log(LOG_DEBUG,"Received AST_CONTROL_PROCEEDING on %s\n",chan->name);
#ifdef ZAPATA_PRI
			if ((p->proceeding < 2) && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_proceeding(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
						pri_rel(p->pri);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->proceeding=2;
			}
#endif
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_PROGRESS:
			ast_log(LOG_DEBUG,"Received AST_CONTROL_PROGRESS on %s\n",chan->name);
#ifdef ZAPATA_PRI
			p->digital = 0;	/* Digital-only calls isn't allows any inband progress messages */
			if ((p->proceeding < 2) && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
						pri_rel(p->pri);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->proceeding=1;
			}
#endif
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_CONGESTION:
			chan->hangupcause = AST_CAUSE_CONGESTION;
#ifdef ZAPATA_PRI
			if (p->priindication_oob && p->sig == SIG_PRI) {
				chan->hangupcause = AST_CAUSE_SWITCH_CONGESTION;
				chan->_softhangup |= AST_SOFTHANGUP_DEV;
				res = 0;
			} else if (!p->proceeding && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
						pri_rel(p->pri);
					} else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->proceeding=1;
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
			} else
#endif
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
			break;
#ifdef ZAPATA_PRI
		case AST_CONTROL_HOLD:
			if (p->pri) {
				if (!pri_grab(p, p->pri)) {
					res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_HOLD);
					pri_rel(p->pri);
				} else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);			
			}
			break;
		case AST_CONTROL_UNHOLD:
			if (p->pri) {
				if (!pri_grab(p, p->pri)) {
					res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_RETRIEVAL);
					pri_rel(p->pri);
				} else
						ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);			
			}
			break;
#endif
		case AST_CONTROL_RADIO_KEY:
			if (p->radio) 
			    res =  zt_set_hook(p->subs[index].zfd, ZT_OFFHOOK);
			res = 0;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			if (p->radio)
			    res =  zt_set_hook(p->subs[index].zfd, ZT_RINGOFF);
			res = 0;
			break;
		case AST_CONTROL_FLASH:
			/* flash hookswitch */
			if (ISTRUNK(p) && (p->sig != SIG_PRI)) {
				/* Clear out the dial buffer */
				p->dop.dialstr[0] = '\0';
				if ((ioctl(p->subs[SUB_REAL].zfd,ZT_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
					ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n", 
						chan->name, strerror(errno));
				} else
					res = 0;
			} else
				res = 0;
			break;
		case -1:
			res = tone_zone_play_tone(p->subs[index].zfd, -1);
			break;
		}
	} else
		res = 0;
	ast_mutex_unlock(&p->lock);
	return res;
}

static struct ast_channel *zt_new(struct zt_pvt *i, int state, int startpbx, int index, int law, int transfercapability)
{
	struct ast_channel *tmp;
	int deflaw;
	int res;
	int x,y;
	int features;
	ZT_PARAMS ps;
	if (i->subs[index].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[index]);
		return NULL;
	}
	tmp = ast_channel_alloc(0);
	if (tmp) {
		tmp->tech = &zap_tech;
		ps.channo = i->channel;
		res = ioctl(i->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &ps);
		if (res) {
			ast_log(LOG_WARNING, "Unable to get parameters, assuming MULAW\n");
			ps.curlaw = ZT_LAW_MULAW;
		}
		if (ps.curlaw == ZT_LAW_ALAW)
			deflaw = AST_FORMAT_ALAW;
		else
			deflaw = AST_FORMAT_ULAW;
		if (law) {
			if (law == ZT_LAW_ALAW)
				deflaw = AST_FORMAT_ALAW;
			else
				deflaw = AST_FORMAT_ULAW;
		}
		y = 1;
		do {
#ifdef ZAPATA_PRI
			if (i->bearer || (i->pri && (i->sig == SIG_FXSKS)))
				snprintf(tmp->name, sizeof(tmp->name), "Zap/%d:%d-%d", i->pri->trunkgroup, i->channel, y);
			else
#endif
			if (i->channel == CHAN_PSEUDO)
				snprintf(tmp->name, sizeof(tmp->name), "Zap/pseudo-%d", rand());
			else	
				snprintf(tmp->name, sizeof(tmp->name), "Zap/%d-%d", i->channel, y);
			for (x=0;x<3;x++) {
				if ((index != x) && i->subs[x].owner && !strcasecmp(tmp->name, i->subs[x].owner->name))
					break;
			}
			y++;
		} while (x < 3);
		tmp->type = type;
		tmp->fds[0] = i->subs[index].zfd;
		tmp->nativeformats = AST_FORMAT_SLINEAR | deflaw;
		/* Start out assuming ulaw since it's smaller :) */
		tmp->rawreadformat = deflaw;
		tmp->readformat = deflaw;
		tmp->rawwriteformat = deflaw;
		tmp->writeformat = deflaw;
		i->subs[index].linear = 0;
		zt_setlinear(i->subs[index].zfd, i->subs[index].linear);
		features = 0;
		if (i->busydetect && CANBUSYDETECT(i)) {
			features |= DSP_FEATURE_BUSY_DETECT;
		}
		if ((i->callprogress & 1) && CANPROGRESSDETECT(i)) {
			features |= DSP_FEATURE_CALL_PROGRESS;
		}
		if ((!i->outgoing && (i->callprogress & 4)) || 
		    (i->outgoing && (i->callprogress & 2))) {
			features |= DSP_FEATURE_FAX_DETECT;
		}
#ifdef ZT_TONEDETECT
		x = ZT_TONEDETECT_ON | ZT_TONEDETECT_MUTE;
		if (ioctl(i->subs[index].zfd, ZT_TONEDETECT, &x)) {
#endif		
			i->hardwaredtmf = 0;
			features |= DSP_FEATURE_DTMF_DETECT;
#ifdef ZT_TONEDETECT
		} else if (NEED_MFDETECT(i)) {
			i->hardwaredtmf = 1;
			features |= DSP_FEATURE_DTMF_DETECT;
		}
#endif
		if (features) {
			if (i->dsp) {
				ast_log(LOG_DEBUG, "Already have a dsp on %s?\n", tmp->name);
			} else {
				i->dsp = ast_dsp_new();
				if (i->dsp) {
#ifdef ZAPATA_PRI
					/* We cannot do progress detection until receives PROGRESS message */
					if (i->outgoing && (i->sig == SIG_PRI)) {
						/* Remember requested DSP features, don't treat
						   talking as ANSWER */
						i->dsp_features = features & ~DSP_PROGRESS_TALK;
						features = 0;
					}
#endif
					ast_dsp_set_features(i->dsp, features);
					ast_dsp_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
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
		if (!ast_strlen_zero(i->language))
			ast_copy_string(tmp->language, i->language, sizeof(tmp->language));
		if (!ast_strlen_zero(i->musicclass))
			ast_copy_string(tmp->musicclass, i->musicclass, sizeof(tmp->musicclass));
		if (!i->owner)
			i->owner = tmp;
		if (!ast_strlen_zero(i->accountcode))
			ast_copy_string(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode));
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		i->subs[index].owner = tmp;
		ast_copy_string(tmp->context, i->context, sizeof(tmp->context));
		/* Copy call forward info */
		ast_copy_string(tmp->call_forward, i->call_forward, sizeof(tmp->call_forward));
		/* If we've been told "no ADSI" then enforce it */
		if (!i->adsi)
			tmp->adsicpe = AST_ADSI_UNAVAILABLE;
		if (!ast_strlen_zero(i->exten))
			ast_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));
		if (!ast_strlen_zero(i->rdnis))
			tmp->cid.cid_rdnis = strdup(i->rdnis);
		if (!ast_strlen_zero(i->dnid))
			tmp->cid.cid_dnid = strdup(i->dnid);

#ifdef PRI_ANI
		ast_set_callerid(tmp, i->cid_num, i->cid_name, ast_strlen_zero(i->cid_ani) ? i->cid_num : i->cid_ani);
#else
		ast_set_callerid(tmp, i->cid_num, i->cid_name, i->cid_num);
#endif
		tmp->cid.cid_pres = i->callingpres;
		tmp->cid.cid_ton = i->cid_ton;
#ifdef ZAPATA_PRI
		tmp->transfercapability = transfercapability;
		pbx_builtin_setvar_helper(tmp, "TRANSFERCAPABILITY", ast_transfercapability2str(transfercapability));
		if (transfercapability & PRI_TRANS_CAP_DIGITAL) {
			i->digital = 1;
		}
		/* Assume calls are not idle calls unless we're told differently */
		i->isidlecall = 0;
		i->alreadyhungup = 0;
#endif
		/* clear the fake event in case we posted one before we had ast_channel */
		i->fake_event = 0;
		/* Assure there is no confmute on this channel */
		zt_confmute(i, 0);
		ast_setstate(tmp, state);
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (startpbx) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
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

static int zt_wink(struct zt_pvt *p, int index)
{
	int j;
	zt_set_hook(p->subs[index].zfd, ZT_WINK);
	for(;;)
	{
		   /* set bits of interest */
		j = ZT_IOMUX_SIGEVENT;
		    /* wait for some happening */
		if (ioctl(p->subs[index].zfd,ZT_IOMUX,&j) == -1) return(-1);
		   /* exit loop if we have it */
		if (j & ZT_IOMUX_SIGEVENT) break;
	}
	  /* get the event info */
	if (ioctl(p->subs[index].zfd,ZT_GETEVENT,&j) == -1) return(-1);
	return 0;
}

static void *ss_thread(void *data)
{
	struct ast_channel *chan = data;
	struct zt_pvt *p = chan->tech_pvt;
	char exten[AST_MAX_EXTENSION]="";
	char exten2[AST_MAX_EXTENSION]="";
	unsigned char buf[256];
	char dtmfcid[300];
	char dtmfbuf[300];
	struct callerid_state *cs;
	char *name=NULL, *number=NULL;
	int distMatches;
	int curRingData[3];
	int receivedRingT;
	int counter1;
	int counter;
	int samples = 0;

	int flags;
	int i;
	int timeout;
	int getforward=0;
	char *s1, *s2;
	int len = 0;
	int res;
	int index;
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Starting simple switch on '%s'\n", chan->name);
	index = zt_get_index(chan, p, 1);
	if (index < 0) {
		ast_log(LOG_WARNING, "Huh?\n");
		ast_hangup(chan);
		return NULL;
	}
	if (p->dsp)
		ast_dsp_digitreset(p->dsp);
	switch(p->sig) {
#ifdef ZAPATA_PRI
	case SIG_PRI:
		/* Now loop looking for an extension */
		ast_copy_string(exten, p->exten, sizeof(exten));
		len = strlen(exten);
		res = 0;
		while((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
			if (len && !ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[index].zfd, -1);
			else
				tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALTONE);
			if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num))
				timeout = matchdigittimeout;
			else
				timeout = gendigittimeout;
			res = ast_waitfordigit(chan, timeout);
			if (res < 0) {
				ast_log(LOG_DEBUG, "waitfordigit returned < 0...\n");
				ast_hangup(chan);
				return NULL;
			} else if (res) {
				exten[len++] = res;
			} else
				break;
		}
		/* if no extension was received ('unspecified') on overlap call, use the 's' extension */
		if (ast_strlen_zero(exten)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Going to extension s|1 because of empty extension received on overlap call\n");
			exten[0] = 's';
			exten[1] = '\0';
		}
		tone_zone_play_tone(p->subs[index].zfd, -1);
		if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num)) {
			/* Start the real PBX */
			ast_copy_string(chan->exten, exten, sizeof(chan->exten));
			if (p->dsp) ast_dsp_digitreset(p->dsp);
			zt_enable_ec(p);
			ast_setstate(chan, AST_STATE_RING);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero!\n");
			}
		} else {
			ast_log(LOG_DEBUG, "No such possible extension '%s' in context '%s'\n", exten, chan->context);
			chan->hangupcause = AST_CAUSE_UNALLOCATED;
			ast_hangup(chan);
			p->exten[0] = '\0';
			/* Since we send release complete here, we won't get one */
			p->call = NULL;
		}
		return NULL;
		break;
#endif
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FEATB:
	case SIG_EMWINK:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_SF_FEATB:
	case SIG_SFWINK:
		if (zt_wink(p, index))	
			return NULL;
		/* Fall through */
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_SF:
		res = tone_zone_play_tone(p->subs[index].zfd, -1);
		if (p->dsp)
			ast_dsp_digitreset(p->dsp);
		/* set digit mode appropriately */
		if (p->dsp) {
			if (NEED_MFDETECT(p))
				ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_MF | p->dtmfrelax); 
			else 
				ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);
		}
		dtmfbuf[0] = 0;
		/* Wait for the first digit only if immediate=no */
		if (!p->immediate)
			/* Wait for the first digit (up to 5 seconds). */
			res = ast_waitfordigit(chan, 5000);
		else res = 0;
		if (res > 0) {
			/* save first char */
			dtmfbuf[0] = res;
			switch(p->sig) {
			case SIG_FEATD:
			case SIG_SF_FEATD:
				res = my_getsigstr(chan,dtmfbuf + 1, "*", 3000);
				if (res > 0)
					res = my_getsigstr(chan,dtmfbuf + strlen(dtmfbuf), "*", 3000);
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				break;
			case SIG_FEATDMF:
			case SIG_E911:
			case SIG_SF_FEATDMF:
				res = my_getsigstr(chan,dtmfbuf + 1, "#", 3000);
				if (res > 0) {
					/* if E911, take off hook */
					if (p->sig == SIG_E911) {
						zt_set_hook(p->subs[SUB_REAL].zfd, ZT_OFFHOOK);
					}
					res = my_getsigstr(chan,dtmfbuf + strlen(dtmfbuf), "#", 3000);
				}
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				break;
			case SIG_FEATB:
			case SIG_SF_FEATB:
				res = my_getsigstr(chan,dtmfbuf + 1, "#", 3000);
				if ((res < 1) && (p->dsp)) ast_dsp_digitreset(p->dsp);
				break;
			default:
				/* If we got the first digit, get the rest */
				len = 1;
				while((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
					if (ast_exists_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
						timeout = matchdigittimeout;
					} else {
						timeout = gendigittimeout;
					}
					res = ast_waitfordigit(chan, timeout);
					if (res < 0) {
						ast_log(LOG_DEBUG, "waitfordigit returned < 0...\n");
						ast_hangup(chan);
						return NULL;
					} else if (res) {
						dtmfbuf[len++] = res;
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
			return NULL;
		} else if (res < 0) {
			ast_log(LOG_DEBUG, "Got hung up before digits finished\n");
			ast_hangup(chan);
			return NULL;
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
		if (p->sig == SIG_FEATDMF) {
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
						if(*(s1 + 2))
							ast_set_callerid(chan, s1 + 2, NULL, s1 + 2);
					ast_copy_string(exten, s2 + 1, sizeof(exten));
				} else
					ast_copy_string(exten, s1 + 2, sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_E911) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2 && (*(s2 + 1) == '0')) {
					if(*(s2 + 2))
						ast_set_callerid(chan, s2 + 2, NULL, s2 + 2);
				}
				if (s1)	ast_copy_string(exten, s1, sizeof(exten));
				else ast_copy_string(exten, "911", sizeof(exten));
				printf("E911: exten: %s, ANI: %s\n",exten, chan->cid.cid_ani);
			} else
				ast_log(LOG_WARNING, "Got a non-E911 input on channel %d.  Assuming E&M Wink instead\n", p->channel);
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
		if (p->sig == SIG_FEATDMF) {
			zt_wink(p, index);
		}
		zt_enable_ec(p);
		if (NEED_MFDETECT(p)) {
			if (p->dsp) {
				if (!p->hardwaredtmf)
					ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax); 
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
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
			}
			return NULL;
		} else {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_2 "Unknown extension '%s' in context '%s' requested\n", exten, chan->context);
			sleep(2);
			res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_INFO);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = ast_streamfile(chan, "ss-noservice", chan->language);
			if (res >= 0)
				ast_waitstream(chan, "");
			res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
			ast_hangup(chan);
			return NULL;
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
		while(len < AST_MAX_EXTENSION-1) {
			/* Read digit unless it's supposed to be immediate, in which case the
			   only answer is 's' */
			if (p->immediate) 
				res = 's';
			else
				res = ast_waitfordigit(chan, timeout);
			timeout = 0;
			if (res < 0) {
				ast_log(LOG_DEBUG, "waitfordigit returned < 0...\n");
				res = tone_zone_play_tone(p->subs[index].zfd, -1);
				ast_hangup(chan);
				return NULL;
			} else if (res)  {
				exten[len++]=res;
				exten[len] = '\0';
			}
			if (!ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[index].zfd, -1);
			else
				tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALTONE);
			if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num) && strcmp(exten, ast_parking_ext())) {
				if (!res || !ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
					if (getforward) {
						/* Record this as the forwarding extension */
						ast_copy_string(p->call_forward, exten, sizeof(p->call_forward)); 
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Setting call forward to '%s' on channel %d\n", p->call_forward, p->channel);
						res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
						if (res)
							break;
						usleep(500000);
						res = tone_zone_play_tone(p->subs[index].zfd, -1);
						sleep(1);
						memset(exten, 0, sizeof(exten));
						res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALTONE);
						len = 0;
						getforward = 0;
					} else  {
						res = tone_zone_play_tone(p->subs[index].zfd, -1);
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
						zt_enable_ec(p);
						res = ast_pbx_run(chan);
						if (res) {
							ast_log(LOG_WARNING, "PBX exited non-zero\n");
							res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
						}
						return NULL;
					}
				} else {
					/* It's a match, but they just typed a digit, and there is an ambiguous match,
					   so just set the timeout to matchdigittimeout and wait some more */
					timeout = matchdigittimeout;
				}
			} else if (res == 0) {
				ast_log(LOG_DEBUG, "not enough digits (and no ambiguous match)...\n");
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
				zt_wait_event(p->subs[index].zfd);
				ast_hangup(chan);
				return NULL;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Disabling call waiting on %s\n", chan->name);
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				ioctl(p->subs[index].zfd,ZT_CONFDIAG,&len);
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
					
			} else if (!strcmp(exten,ast_pickup_ext())) {
				/* Scan all channels and see if any there
				 * ringing channqels with that have call groups
				 * that equal this channels pickup group  
				 */
			  	if (index == SUB_REAL) {
					/* Switch us from Third call to Call Wait */
				  	if (p->subs[SUB_THREEWAY].owner) {
						/* If you make a threeway call and the *8# a call, it should actually 
						   look like a callwait */
						alloc_sub(p, SUB_CALLWAIT);	
					  	swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
						unalloc_sub(p, SUB_THREEWAY);
					}
					zt_enable_ec(p);
					if (ast_pickup_call(chan)) {
						ast_log(LOG_DEBUG, "No call pickup possible...\n");
						res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
						zt_wait_event(p->subs[index].zfd);
					}
					ast_hangup(chan);
					return NULL;
				} else {
					ast_log(LOG_WARNING, "Huh?  Got *8# on call not on real\n");
					ast_hangup(chan);
					return NULL;
				}
				
			} else if (!p->hidecallerid && !strcmp(exten, "*67")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Disabling Caller*ID on %s\n", chan->name);
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				if (chan->cid.cid_num)
					free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				if (chan->cid.cid_name)
					free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
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
					res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				break;
			} else if (!strcmp(exten, "*78")) {
				/* Do not disturb */
				if (option_verbose > 2) {
					ast_verbose(VERBOSE_PREFIX_3 "Enabled DND on channel %d\n", p->channel);
					manager_event(EVENT_FLAG_SYSTEM, "DNDState",
								"Channel: Zap/%d\r\n"
								"Status: enabled\r\n", p->channel);
				}
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				p->dnd = 1;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (!strcmp(exten, "*79")) {
				/* Do not disturb */
				if (option_verbose > 2) {
					ast_verbose(VERBOSE_PREFIX_3 "Disabled DND on channel %d\n", p->channel);
					manager_event(EVENT_FLAG_SYSTEM, "DNDState",
								"Channel: Zap/%d\r\n"
								"Status: disabled\r\n", p->channel);
				}
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				p->dnd = 0;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*72")) {
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				getforward = 1;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*73")) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Cancelling call forwarding on channel %d\n", p->channel);
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
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
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Parking call to '%s'\n", chan->name);
				break;
			} else if (!ast_strlen_zero(p->lastcid_num) && !strcmp(exten, "*60")) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Blacklisting number %s\n", p->lastcid_num);
				res = ast_db_put("blacklist", p->lastcid_num, "1");
				if (!res) {
					res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
					memset(exten, 0, sizeof(exten));
					len = 0;
				}
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Enabling Caller*ID on %s\n", chan->name);
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				if (chan->cid.cid_num)
					free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				if (chan->cid.cid_name)
					free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
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
				struct zt_pvt *pbridge = NULL;
				  /* set up the private struct of the bridged one, if any */
				if (nbridge && ast_bridged_channel(nbridge)) 
					pbridge = ast_bridged_channel(nbridge)->tech_pvt;
				if (nbridge && pbridge && 
				    (!strcmp(nbridge->type,"Zap")) && 
					(!strcmp(ast_bridged_channel(nbridge)->type, "Zap")) &&
				    ISTRUNK(pbridge)) {
					int func = ZT_FLASH;
					/* Clear out the dial buffer */
					p->dop.dialstr[0] = '\0';
					/* flash hookswitch */
					if ((ioctl(pbridge->subs[SUB_REAL].zfd,ZT_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
						ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n", 
							nbridge->name, strerror(errno));
					}
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
					if (ast_bridged_channel(p->subs[SUB_REAL].owner))
						ast_moh_stop(ast_bridged_channel(p->subs[SUB_REAL].owner));
					ast_hangup(chan);
					return NULL;
				} else {
					tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
					zt_wait_event(p->subs[index].zfd);
					tone_zone_play_tone(p->subs[index].zfd, -1);
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
					ast_hangup(chan);
					return NULL;
				}					
			} else if (!ast_canmatch_extension(chan, chan->context, exten, 1, chan->cid.cid_num) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Can't match %s from '%s' in context %s\n", exten, chan->cid.cid_num ? chan->cid.cid_num : "<Unknown Caller>", chan->context);
				break;
			}
			if (!timeout)
				timeout = gendigittimeout;
			if (len && !ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[index].zfd, -1);
		}
		break;
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
#ifdef ZAPATA_PRI
		if (p->pri) {
			/* This is a GR-303 trunk actually.  Wait for the first ring... */
			struct ast_frame *f;
			int res;
			time_t start;

			time(&start);
			ast_setstate(chan, AST_STATE_RING);
			while(time(NULL) < start + 3) {
				res = ast_waitfor(chan, 1000);
				if (res) {
					f = ast_read(chan);
					if (!f) {
						ast_log(LOG_WARNING, "Whoa, hangup while waiting for first ring!\n");
						ast_hangup(chan);
						return NULL;
					} else if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_RING)) {
						res = 1;
					} else
						res = 0;
					ast_frfree(f);
					if (res) {
						ast_log(LOG_DEBUG, "Got ring!\n");
						res = 0;
						break;
					}
				}
			}
		}
#endif
		/* If we want caller id, we're in a prering state due to a polarity reversal
		 * and we're set to use a polarity reversal to trigger the start of caller id,
		 * grab the caller id and wait for ringing to start... */
		if (p->use_callerid && (chan->_state == AST_STATE_PRERING && p->cid_start == CID_START_POLARITY)) {
			/* If set to use DTMF CID signalling, listen for DTMF */
			if (p->cid_signalling == CID_SIG_DTMF) {
				int i = 0;
				cs = NULL;
				ast_log(LOG_DEBUG, "Receiving DTMF cid on "
					"channel %s\n", chan->name);
				zt_setlinear(p->subs[index].zfd, 0);
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
						dtmfbuf[i++] = f->subclass;
						ast_log(LOG_DEBUG, "CID got digit '%c'\n", f->subclass);
						res = 2000;
					}
					ast_frfree(f);
					if (chan->_state == AST_STATE_RING ||
					    chan->_state == AST_STATE_RINGING) 
						break; /* Got ring */
				}
				dtmfbuf[i] = 0;
				zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
				/* Got cid and ring. */
				ast_log(LOG_DEBUG, "CID got string '%s'\n", dtmfbuf);
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_log(LOG_DEBUG, "CID is '%s', flags %d\n", 
					dtmfcid, flags);
				/* If first byte is NULL, we have no cid */
				if (dtmfcid[0]) 
					number = dtmfcid;
				else
					number = 0;
			/* If set to use V23 Signalling, launch our FSK gubbins and listen for it */
			} else if (p->cid_signalling == CID_SIG_V23) {
				cs = callerid_new(cid_signalling);
				if (cs) {
					samples = 0;
#if 1
					bump_gains(p);
#endif				
					/* Take out of linear mode for Caller*ID processing */
					zt_setlinear(p->subs[index].zfd, 0);
					
					/* First we wait and listen for the Caller*ID */
					for(;;) {	
						i = ZT_IOMUX_READ | ZT_IOMUX_SIGEVENT;
						if ((res = ioctl(p->subs[index].zfd, ZT_IOMUX, &i)))	{
							ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
							callerid_free(cs);
							ast_hangup(chan);
							return NULL;
						}
						if (i & ZT_IOMUX_SIGEVENT) {
							res = zt_get_event(p->subs[index].zfd);
							ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
							res = 0;
							break;
						} else if (i & ZT_IOMUX_READ) {
							res = read(p->subs[index].zfd, buf, sizeof(buf));
							if (res < 0) {
								if (errno != ELAST) {
									ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
									callerid_free(cs);
									ast_hangup(chan);
									return NULL;
								}
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
						if (option_debug)
							ast_log(LOG_DEBUG, "CallerID number: %s, name: %s, flags=%d\n", number, name, flags);
					}
					if (res < 0) {
						ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", chan->name);
					}

					/* Finished with Caller*ID, now wait for a ring to make sure there really is a call coming */ 
					res = 2000;
					for (;;) {
						struct ast_frame *f;
						res = ast_waitfor(chan, res);
						if (res <= 0) {
							ast_log(LOG_WARNING, "CID timed out waiting for ring. "
								"Exiting simple switch\n");
							ast_hangup(chan);
							return NULL;
						} 
						f = ast_read(chan);
						ast_frfree(f);
						if (chan->_state == AST_STATE_RING ||
						    chan->_state == AST_STATE_RINGING) 
							break; /* Got ring */
					}
	
					/* We must have a ring by now, so, if configured, lets try to listen for
					 * distinctive ringing */ 
					if (p->usedistinctiveringdetection == 1) {
						len = 0;
						distMatches = 0;
						/* Clear the current ring data array so we dont have old data in it. */
						for (receivedRingT=0; receivedRingT < 3; receivedRingT++) {
							curRingData[receivedRingT] = 0;
						}
						receivedRingT = 0;
						counter = 0;
						counter1 = 0;
						/* Check to see if context is what it should be, if not set to be. */
						if (strcmp(p->context,p->defcontext) != 0) {
							ast_copy_string(p->context, p->defcontext, sizeof(p->context));
							ast_copy_string(chan->context,p->defcontext,sizeof(chan->context));
						}
		
						for(;;) {	
							i = ZT_IOMUX_READ | ZT_IOMUX_SIGEVENT;
							if ((res = ioctl(p->subs[index].zfd, ZT_IOMUX, &i)))	{
								ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
								callerid_free(cs);
								ast_hangup(chan);
								return NULL;
							}
							if (i & ZT_IOMUX_SIGEVENT) {
								res = zt_get_event(p->subs[index].zfd);
								ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
								res = 0;
								/* Let us detect distinctive ring */
		
								curRingData[receivedRingT] = p->ringt;
		
								if (p->ringt < p->ringt_base/2)
									break;
								++receivedRingT; /* Increment the ringT counter so we can match it against
										values in zapata.conf for distinctive ring */
							} else if (i & ZT_IOMUX_READ) {
								res = read(p->subs[index].zfd, buf, sizeof(buf));
								if (res < 0) {
									if (errno != ELAST) {
										ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
										callerid_free(cs);
										ast_hangup(chan);
										return NULL;
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
						if(option_verbose > 2)
							/* this only shows up if you have n of the dring patterns filled in */
							ast_verbose( VERBOSE_PREFIX_3 "Detected ring pattern: %d,%d,%d\n",curRingData[0],curRingData[1],curRingData[2]);
	
						for (counter=0; counter < 3; counter++) {
							/* Check to see if the rings we received match any of the ones in zapata.conf for this
							channel */
							distMatches = 0;
							for (counter1=0; counter1 < 3; counter1++) {
								if (curRingData[counter1] <= (p->drings.ringnum[counter].ring[counter1]+10) && curRingData[counter1] >=
								(p->drings.ringnum[counter].ring[counter1]-10)) {
									distMatches++;
								}
							}
							if (distMatches == 3) {
								/* The ring matches, set the context to whatever is for distinctive ring.. */
								ast_copy_string(p->context, p->drings.ringContext[counter].contextData, sizeof(p->context));
								ast_copy_string(chan->context, p->drings.ringContext[counter].contextData, sizeof(chan->context));
								if(option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "Distinctive Ring matched context %s\n",p->context);
								break;
							}
						}
					}
					/* Restore linear mode (if appropriate) for Caller*ID processing */
					zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
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
				return NULL;
			}
		} else if (p->use_callerid && p->cid_start == CID_START_RING) {
			/* FSK Bell202 callerID */
			cs = callerid_new(cid_signalling);
			if (cs) {
#if 1
				bump_gains(p);
#endif				
				samples = 0;
				len = 0;
				distMatches = 0;
				/* Clear the current ring data array so we dont have old data in it. */
				for (receivedRingT=0; receivedRingT < 3; receivedRingT++) {
					curRingData[receivedRingT] = 0;
				}
				receivedRingT = 0;
				counter = 0;
				counter1 = 0;
				/* Check to see if context is what it should be, if not set to be. */
				if (strcmp(p->context,p->defcontext) != 0) {
					ast_copy_string(p->context, p->defcontext, sizeof(p->context));
					ast_copy_string(chan->context,p->defcontext,sizeof(chan->context));
				}

				/* Take out of linear mode for Caller*ID processing */
				zt_setlinear(p->subs[index].zfd, 0);
				for(;;) {	
					i = ZT_IOMUX_READ | ZT_IOMUX_SIGEVENT;
					if ((res = ioctl(p->subs[index].zfd, ZT_IOMUX, &i)))	{
						ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
						callerid_free(cs);
						ast_hangup(chan);
						return NULL;
					}
					if (i & ZT_IOMUX_SIGEVENT) {
						res = zt_get_event(p->subs[index].zfd);
						ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
						res = 0;
						/* Let us detect callerid when the telco uses distinctive ring */

						curRingData[receivedRingT] = p->ringt;

						if (p->ringt < p->ringt_base/2)
							break;
						++receivedRingT; /* Increment the ringT counter so we can match it against
								values in zapata.conf for distinctive ring */
					} else if (i & ZT_IOMUX_READ) {
						res = read(p->subs[index].zfd, buf, sizeof(buf));
						if (res < 0) {
							if (errno != ELAST) {
								ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
								callerid_free(cs);
								ast_hangup(chan);
								return NULL;
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
				if (p->usedistinctiveringdetection == 1) {
					if(option_verbose > 2)
						/* this only shows up if you have n of the dring patterns filled in */
						ast_verbose( VERBOSE_PREFIX_3 "Detected ring pattern: %d,%d,%d\n",curRingData[0],curRingData[1],curRingData[2]);

					for (counter=0; counter < 3; counter++) {
						/* Check to see if the rings we received match any of the ones in zapata.conf for this
						channel */
						distMatches = 0;
						for (counter1=0; counter1 < 3; counter1++) {
							if (curRingData[counter1] <= (p->drings.ringnum[counter].ring[counter1]+10) && curRingData[counter1] >=
							(p->drings.ringnum[counter].ring[counter1]-10)) {
								distMatches++;
							}
						}
						if (distMatches == 3) {
							/* The ring matches, set the context to whatever is for distinctive ring.. */
							ast_copy_string(p->context, p->drings.ringContext[counter].contextData, sizeof(p->context));
							ast_copy_string(chan->context, p->drings.ringContext[counter].contextData, sizeof(chan->context));
							if(option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "Distinctive Ring matched context %s\n",p->context);
							break;
						}
					}
				}
				if (res == 1) {
					callerid_get(cs, &name, &number, &flags);
					if (option_debug)
						ast_log(LOG_DEBUG, "CallerID number: %s, name: %s, flags=%d\n", number, name, flags);
				}
				/* Restore linear mode (if appropriate) for Caller*ID processing */
				zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
#if 1
				restore_gains(p);
#endif				
				if (res < 0) {
					ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", chan->name);
				}
			} else
				ast_log(LOG_WARNING, "Unable to get caller ID space\n");
		}
		else
			cs = NULL;
		if (number || name) {
		    if (chan->cid.cid_num) {
			free(chan->cid.cid_num);
			chan->cid.cid_num = NULL;
		    }
		    if (chan->cid.cid_name) {
			free(chan->cid.cid_name);
			chan->cid.cid_name = NULL;
		    }
		}
		if (number)
			ast_shrink_phone_number(number);

		ast_set_callerid(chan, number, name, number);

		if (cs)
			callerid_free(cs);
		ast_setstate(chan, AST_STATE_RING);
		chan->rings = 1;
		p->ringt = p->ringt_base;
		res = ast_pbx_run(chan);
		if (res) {
			ast_hangup(chan);
			ast_log(LOG_WARNING, "PBX exited non-zero\n");
		}
		return NULL;
	default:
		ast_log(LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", sig2str(p->sig), p->channel);
		res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
		if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
	if (res < 0)
			ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
	return NULL;
}

#ifdef ZAPATA_R2
static int handle_init_r2_event(struct zt_pvt *i, mfcr2_event_t *e)
{
	struct ast_channel *chan;
	
	switch(e->e) {
	case MFCR2_EVENT_UNBLOCKED:
		i->r2blocked = 0;
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "R2 Channel %d unblocked\n", i->channel);
		break;
	case MFCR2_EVENT_BLOCKED:
		i->r2blocked = 1;
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "R2 Channel %d unblocked\n", i->channel);
		break;
	case MFCR2_EVENT_IDLE:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "R2 Channel %d idle\n", i->channel);
		break;
	case MFCR2_EVENT_RINGING:
			/* This is what Asterisk refers to as a "RING" event. For some reason they're reversed in
			   Steve's code */
			/* Check for callerid, digits, etc */
			i->hasr2call = 1;
			chan = zt_new(i, AST_STATE_RING, 0, SUB_REAL, 0, 0);
			if (!chan) {
				ast_log(LOG_WARNING, "Unable to create channel for channel %d\n", i->channel);
				mfcr2_DropCall(i->r2, NULL, UC_NETWORK_CONGESTION);
				i->hasr2call = 0;
			}
			if (ast_pbx_start(chan)) {
				ast_log(LOG_WARNING, "Unable to start PBX on channel %s\n", chan->name);
				ast_hangup(chan);
			}
			break;
	default:
		ast_log(LOG_WARNING, "Don't know how to handle initial R2 event %s on channel %d\n", mfcr2_event2str(e->e), i->channel);	
		return -1;
	}
	return 0;
}
#endif

static int handle_init_event(struct zt_pvt *i, int event)
{
	int res;
	pthread_t threadid;
	pthread_attr_t attr;
	struct ast_channel *chan;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	/* Handle an event on a given channel for the monitor thread. */
	switch(event) {
	case ZT_EVENT_NONE:
	case ZT_EVENT_BITSCHANGED:
		if (i->radio) break;
#ifdef ZAPATA_R2
		if (i->r2) {
			mfcr2_event_t *e;
			e = r2_get_event_bits(i);
			i->sigchecked = 1;
			if (e)
				handle_init_r2_event(i, e);
		}
#endif		
		break;
	case ZT_EVENT_WINKFLASH:
	case ZT_EVENT_RINGOFFHOOK:
		if (i->inalarm) break;
		if (i->radio) break;
		/* Got a ring/answer.  What kind of channel are we? */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
		        zt_set_hook(i->subs[SUB_REAL].zfd, ZT_OFFHOOK);
			if (i->cidspill) {
				/* Cancel VMWI spill */
				free(i->cidspill);
				i->cidspill = NULL;
			}
			if (i->immediate) {
				zt_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_RINGTONE);
				chan = zt_new(i, AST_STATE_RING, 1, SUB_REAL, 0, 0);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = zt_new(i, AST_STATE_RESERVED, 0, SUB_REAL, 0, 0);
				if (chan) {
					if (has_voicemail(i))
						res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_STUTTER);
					else
						res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_DIALTONE);
					if (res < 0) 
						ast_log(LOG_WARNING, "Unable to play dialtone on channel %d\n", i->channel);
					if (ast_pthread_create(&threadid, &attr, ss_thread, chan)) {
						ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
						res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
						ast_hangup(chan);
					}
				} else
					ast_log(LOG_WARNING, "Unable to create channel\n");
#if 0
				printf("Created thread %ld detached in switch\n", threadid);
#endif
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
		case SIG_E911:
		case SIG_FEATB:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
		case SIG_SF:
				/* Check for callerid, digits, etc */
				chan = zt_new(i, AST_STATE_RING, 0, SUB_REAL, 0, 0);
				if (chan && ast_pthread_create(&threadid, &attr, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					ast_hangup(chan);
				} else if (!chan) {
					ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				}
#if 0
				printf("Created thread %ld detached in switch(2)\n", threadid);
#endif
				break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/answer with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
			if (res < 0)
					ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			return -1;
		}
		break;
	case ZT_EVENT_NOALARM:
		i->inalarm = 0;
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", i->channel);
		break;
	case ZT_EVENT_ALARM:
		i->inalarm = 1;
		res = get_alarms(i);
		ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", i->channel, alarm2str(res));
		/* fall thru intentionally */
	case ZT_EVENT_ONHOOK:
		if (i->radio) break;
		/* Back on hook.  Hang up. */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_E911:
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
		case SIG_GR303FXSKS:
			zt_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, -1);
			zt_set_hook(i->subs[SUB_REAL].zfd, ZT_ONHOOK);
			break;
		case SIG_GR303FXOKS:
		case SIG_FXOKS:
			zt_disable_ec(i);
			/* Diddle the battery for the zhone */
#ifdef ZHONE_HACK
			zt_set_hook(i->subs[SUB_REAL].zfd, ZT_OFFHOOK);
			usleep(1);
#endif			
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, -1);
			zt_set_hook(i->subs[SUB_REAL].zfd, ZT_ONHOOK);
			break;
		case SIG_PRI:
			zt_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, -1);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle on hook with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, -1);
			return -1;
		}
		break;
	case ZT_EVENT_POLARITY:
		switch(i->sig) {
		case SIG_FXSLS:
		case SIG_FXSKS:
		case SIG_FXSGS:
			if (i->cid_start == CID_START_POLARITY) {
				i->polarity = POLARITY_REV;
				ast_verbose(VERBOSE_PREFIX_2 "Starting post polarity "
					    "CID detection on channel %d\n",
					    i->channel);
				chan = zt_new(i, AST_STATE_PRERING, 0, SUB_REAL, 0, 0);
				if (chan && ast_pthread_create(&threadid, &attr, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
				}
			}
			break;
		default:
			ast_log(LOG_WARNING, "handle_init_event detected "
				"polarity reversal on non-FXO (SIG_FXS) "
				"interface %d\n", i->channel);
		}
	}
	return 0;
}

static void *do_monitor(void *data)
{
	int count, res, res2, spoint, pollres=0;
	struct zt_pvt *i;
	struct zt_pvt *last = NULL;
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
	ast_log(LOG_DEBUG, "Monitor starting...\n");
#endif
	for(;;) {
		/* Lock the interface list */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_ERROR, "Unable to grab interface lock\n");
			return NULL;
		}
		if (!pfds || (lastalloc != ifcount)) {
			if (pfds)
				free(pfds);
			if (ifcount) {
				pfds = malloc(ifcount * sizeof(struct pollfd));
				if (!pfds) {
					ast_log(LOG_WARNING, "Critical memory error.  Zap dies.\n");
					ast_mutex_unlock(&iflock);
					return NULL;
				}
			}
			lastalloc = ifcount;
		}
		/* Build the stuff we're going to poll on, that is the socket of every
		   zt_pvt that does not have an associated owner channel */
		count = 0;
		i = iflist;
		while(i) {
			if ((i->subs[SUB_REAL].zfd > -1) && i->sig && (!i->radio)) {
				if (!i->owner && !i->subs[SUB_REAL].owner) {
					/* This needs to be watched, as it lacks an owner */
					pfds[count].fd = i->subs[SUB_REAL].zfd;
					pfds[count].events = POLLPRI;
					pfds[count].revents = 0;
					/* Message waiting or r2 channels also get watched for reading */
#ifdef ZAPATA_R2
					if (i->cidspill || i->r2)
#else					
					if (i->cidspill)
#endif					
						pfds[count].events |= POLLIN;
					count++;
				}
			}
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_mutex_unlock(&iflock);
		
		pthread_testcancel();
		/* Wait at least a second for something to happen */
		res = poll(pfds, count, 1000);
		pthread_testcancel();
		/* Okay, poll has finished.  Let's see what happened.  */
		if (res < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "poll return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}
		found = 0;
		spoint = 0;
		lastpass = thispass;
		thispass = time(NULL);
		i = iflist;
		while(i) {
			if (thispass != lastpass) {
				if (!found && ((i == last) || ((i == iflist) && !last))) {
					last = i;
					if (last) {
#if 0
						printf("Checking channel %d\n", last->channel);
#endif						
						if (!last->cidspill && !last->owner && !ast_strlen_zero(last->mailbox) && (thispass - last->onhooktime > 3) &&
							(last->sig & __ZT_SIG_FXO)) {
#if 0
							printf("Channel %d has mailbox %s\n", last->channel, last->mailbox);
#endif							
							res = ast_app_has_voicemail(last->mailbox, NULL);
							if (last->msgstate != res) {
								int x;
								ast_log(LOG_DEBUG, "Message status for %s changed from %d to %d on %d\n", last->mailbox, last->msgstate, res, last->channel);
								x = ZT_FLUSH_BOTH;
								res2 = ioctl(last->subs[SUB_REAL].zfd, ZT_FLUSH, &x);
								if (res2)
									ast_log(LOG_WARNING, "Unable to flush input on channel %d\n", last->channel);
								last->cidspill = malloc(MAX_CALLERID_SIZE);
								if (last->cidspill) {
									/* Turn on on hook transfer for 4 seconds */
									x = 4000;
									ioctl(last->subs[SUB_REAL].zfd, ZT_ONHOOKTRANSFER, &x);
									last->cidlen = vmwi_generate(last->cidspill, res, 1, AST_LAW(last));
									last->cidpos = 0;
#if 0
									printf("Made %d bytes of message waiting for %d\n", last->cidlen, res);
#endif									
									last->msgstate = res;
									last->onhooktime = thispass;
								}
								found ++;
							}
						}
						last = last->next;
					}
				}
			}
			if ((i->subs[SUB_REAL].zfd > -1) && i->sig) {
				if (i->radio && !i->owner)
				{
					res = zt_get_event(i->subs[SUB_REAL].zfd);
					if (res)
					{
						if (option_debug)
							ast_log(LOG_DEBUG, "Monitor doohicky got event %s on radio channel %d\n", event2str(res), i->channel);
						/* Don't hold iflock while handling init events */
						ast_mutex_unlock(&iflock);
						handle_init_event(i, res);
						ast_mutex_lock(&iflock);	
					}
					i = i->next;
					continue;
				}					
				pollres = ast_fdisset(pfds, i->subs[SUB_REAL].zfd, count, &spoint);
				if (pollres & POLLIN) {
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef ZAPATA_PRI
						if (!i->pri)
#endif						
							ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d) in read...\n", i->subs[SUB_REAL].zfd);
						i = i->next;
						continue;
					}
#ifdef ZAPATA_R2
					if (i->r2) {
						/* If it's R2 signalled, we always have to check for events */
						mfcr2_event_t *e;
						e = mfcr2_check_event(i->r2);
						if (e)
							handle_init_r2_event(i, e);
						else {
							e = mfcr2_schedule_run(i->r2);
							if (e)
								handle_init_r2_event(i, e);
						}
						i = i->next;
						continue;
					}
#endif
					if (!i->cidspill) {
						ast_log(LOG_WARNING, "Whoa....  I'm reading but have no cidspill (%d)...\n", i->subs[SUB_REAL].zfd);
						i = i->next;
						continue;
					}
					res = read(i->subs[SUB_REAL].zfd, buf, sizeof(buf));
					if (res > 0) {
						/* We read some number of bytes.  Write an equal amount of data */
						if (res > i->cidlen - i->cidpos) 
							res = i->cidlen - i->cidpos;
						res2 = write(i->subs[SUB_REAL].zfd, i->cidspill + i->cidpos, res);
						if (res2 > 0) {
							i->cidpos += res2;
							if (i->cidpos >= i->cidlen) {
								free(i->cidspill);
								i->cidspill = 0;
								i->cidpos = 0;
								i->cidlen = 0;
							}
						} else {
							ast_log(LOG_WARNING, "Write failed: %s\n", strerror(errno));
							i->msgstate = -1;
						}
					} else {
						ast_log(LOG_WARNING, "Read failed with %d: %s\n", res, strerror(errno));
					}
					if (option_debug)
						ast_log(LOG_DEBUG, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
					/* Don't hold iflock while handling init events -- race with chlock */
					ast_mutex_unlock(&iflock);
					handle_init_event(i, res);
					ast_mutex_lock(&iflock);	
				}
#ifdef ZAPATA_R2
				if ((pollres & POLLPRI) || (i->r2 && !i->sigchecked)) 
#else				
				if (pollres & POLLPRI) 
#endif				
				{
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef ZAPATA_PRI
						if (!i->pri)
#endif						
							ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d)...\n", i->subs[SUB_REAL].zfd);
						i = i->next;
						continue;
					}
					res = zt_get_event(i->subs[SUB_REAL].zfd);
					if (option_debug)
						ast_log(LOG_DEBUG, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
					/* Don't hold iflock while handling init events */
					ast_mutex_unlock(&iflock);
					handle_init_event(i, res);
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
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	if (ast_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread != AST_PTHREADT_NULL) {
		/* Just signal it to be sure it wakes up */
#if 0
		pthread_cancel(monitor_thread);
#endif
		pthread_kill(monitor_thread, SIGURG);
#if 0
		pthread_join(monitor_thread, NULL);
#endif
	} else {
		/* Start a new monitor */
		if (ast_pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
#if 0
	printf("Created thread %ld detached in restart monitor\n", monitor_thread);
#endif
	ast_mutex_unlock(&monlock);
	return 0;
}

#ifdef ZAPATA_PRI
static int pri_resolve_span(int *span, int channel, int offset, struct zt_spaninfo *si)
{
	int x;
	int trunkgroup;
	/* Get appropriate trunk group if there is one */
	trunkgroup = pris[*span].mastertrunkgroup;
	if (trunkgroup) {
		/* Select a specific trunk group */
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].trunkgroup == trunkgroup) {
				*span = x;
				return 0;
			}
		}
		ast_log(LOG_WARNING, "Channel %d on span %d configured to use nonexistent trunk group %d\n", channel, *span, trunkgroup);
		*span = -1;
	} else {
		if (pris[*span].trunkgroup) {
			ast_log(LOG_WARNING, "Unable to use span %d implicitly since it is trunk group %d (please use spanmap)\n", *span, pris[*span].trunkgroup);
			*span = -1;
		} else if (pris[*span].mastertrunkgroup) {
			ast_log(LOG_WARNING, "Unable to use span %d implicitly since it is already part of trunk group %d\n", *span, pris[*span].mastertrunkgroup);
			*span = -1;
		} else {
			if (si->totalchans == 31) { /* if it's an E1 */
				pris[*span].dchannels[0] = 16 + offset;
			} else {
				pris[*span].dchannels[0] = 24 + offset;
			}
			pris[*span].dchanavail[0] |= DCHAN_PROVISIONED;
			pris[*span].offset = offset;
			pris[*span].span = *span + 1;
		}
	}
	return 0;
}

static int pri_create_trunkgroup(int trunkgroup, int *channels)
{
	struct zt_spaninfo si;
	ZT_PARAMS p;
	int fd;
	int span;
	int ospan=0;
	int x,y;
	for (x=0;x<NUM_SPANS;x++) {
		if (pris[x].trunkgroup == trunkgroup) {
			ast_log(LOG_WARNING, "Trunk group %d already exists on span %d, Primary d-channel %d\n", trunkgroup, x + 1, pris[x].dchannels[0]);
			return -1;
		}
	}
	for (y=0;y<NUM_DCHANS;y++) {
		if (!channels[y])	
			break;
		memset(&si, 0, sizeof(si));
		memset(&p, 0, sizeof(p));
		fd = open("/dev/zap/channel", O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Failed to open channel: %s\n", strerror(errno));
			return -1;
		}
		x = channels[y];
		if (ioctl(fd, ZT_SPECIFY, &x)) {
			ast_log(LOG_WARNING, "Failed to specify channel %d: %s\n", channels[y], strerror(errno));
			zt_close(fd);
			return -1;
		}
		if (ioctl(fd, ZT_GET_PARAMS, &p)) {
			ast_log(LOG_WARNING, "Failed to get channel parameters for channel %d: %s\n", channels[y], strerror(errno));
			return -1;
		}
		if (ioctl(fd, ZT_SPANSTAT, &si)) {
			ast_log(LOG_WARNING, "Failed go get span information on channel %d (span %d)\n", channels[y], p.spanno);
			zt_close(fd);
			return -1;
		}
		span = p.spanno - 1;
		if (pris[span].trunkgroup) {
			ast_log(LOG_WARNING, "Span %d is already provisioned for trunk group %d\n", span + 1, pris[span].trunkgroup);
			zt_close(fd);
			return -1;
		}
		if (pris[span].pvts[0]) {
			ast_log(LOG_WARNING, "Span %d is already provisioned with channels (implicit PRI maybe?)\n", span + 1);
			zt_close(fd);
			return -1;
		}
		if (!y) {
			pris[span].trunkgroup = trunkgroup;
			pris[span].offset = channels[y] - p.chanpos;
			ospan = span;
		}
		pris[ospan].dchannels[y] = channels[y];
		pris[ospan].dchanavail[y] |= DCHAN_PROVISIONED;
		pris[span].span = span + 1;
		zt_close(fd);
	}
	return 0;	
}

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

#endif

static struct zt_pvt *mkintf(int channel, int signalling, int radio, struct zt_pri *pri, int reloading)
{
	/* Make a zt_pvt structure for this interface (or CRV if "pri" is specified) */
	struct zt_pvt *tmp = NULL, *tmp2,  *prev = NULL;
	char fn[80];
#if 1
	struct zt_bufferinfo bi;
#endif
	struct zt_spaninfo si;
	int res;
	int span=0;
	int here = 0;
	int x;
	struct zt_pvt **wlist;
	struct zt_pvt **wend;
	ZT_PARAMS p;

	wlist = &iflist;
	wend = &ifend;

#ifdef ZAPATA_PRI
	if (pri) {
		wlist = &pri->crvs;
		wend = &pri->crvend;
	}
#endif

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

	if (!here && !reloading) {
		tmp = (struct zt_pvt*)malloc(sizeof(struct zt_pvt));
		if (!tmp) {
			ast_log(LOG_ERROR, "MALLOC FAILED\n");
			destroy_zt_pvt(&tmp);
			return NULL;
		}
		memset(tmp, 0, sizeof(struct zt_pvt));
		ast_mutex_init(&tmp->lock);
		ifcount++;
		for (x=0;x<3;x++)
			tmp->subs[x].zfd = -1;
		tmp->channel = channel;
	}

	if (tmp) {
		if (!here) {
			if ((channel != CHAN_PSEUDO) && !pri) {
				snprintf(fn, sizeof(fn), "%d", channel);
				/* Open non-blocking */
				if (!here)
					tmp->subs[SUB_REAL].zfd = zt_open(fn);
				/* Allocate a zapata structure */
				if (tmp->subs[SUB_REAL].zfd < 0) {
					ast_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
					destroy_zt_pvt(&tmp);
					return NULL;
				}
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &p);
				if (res < 0) {
					ast_log(LOG_ERROR, "Unable to get parameters\n");
					destroy_zt_pvt(&tmp);
					return NULL;
				}
				if (p.sigtype != (signalling & 0x3ffff)) {
					ast_log(LOG_ERROR, "Signalling requested on channel %d is %s but line is in %s signalling\n", channel, sig2str(signalling), sig2str(p.sigtype));
					destroy_zt_pvt(&tmp);
					return tmp;
				}
				tmp->law = p.curlaw;
				tmp->span = p.spanno;
				span = p.spanno - 1;
			} else {
				if (channel == CHAN_PSEUDO)
					signalling = 0;
				else if ((signalling != SIG_FXOKS) && (signalling != SIG_FXSKS)) {
					ast_log(LOG_ERROR, "CRV's must use FXO/FXS Kewl Start (fxo_ks/fxs_ks) signalling only.\n");
					return NULL;
				}
			}
#ifdef ZAPATA_PRI
			if ((signalling == SIG_PRI) || (signalling == SIG_GR303FXOKS) || (signalling == SIG_GR303FXSKS)) {
				int offset;
				int myswitchtype;
				int matchesdchan;
				int x,y;
				offset = 0;
				if ((signalling == SIG_PRI) && ioctl(tmp->subs[SUB_REAL].zfd, ZT_AUDIOMODE, &offset)) {
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_zt_pvt(&tmp);
					return NULL;
				}
				if (span >= NUM_SPANS) {
					ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
					destroy_zt_pvt(&tmp);
					return NULL;
				} else {
					si.spanno = 0;
					if (ioctl(tmp->subs[SUB_REAL].zfd,ZT_SPANSTAT,&si) == -1) {
						ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
						destroy_zt_pvt(&tmp);
						return NULL;
					}
					/* Store the logical span first based upon the real span */
					tmp->logicalspan = pris[span].prilogicalspan;
					pri_resolve_span(&span, channel, (channel - p.chanpos), &si);
					if (span < 0) {
						ast_log(LOG_WARNING, "Channel %d: Unable to find locate channel/trunk group!\n", channel);
						destroy_zt_pvt(&tmp);
						return NULL;
					}
					if (signalling == SIG_PRI)
						myswitchtype = switchtype;
					else
						myswitchtype = PRI_SWITCH_GR303_TMC;
					/* Make sure this isn't a d-channel */
					matchesdchan=0;
					for (x=0;x<NUM_SPANS;x++) {
						for (y=0;y<NUM_DCHANS;y++) {
							if (pris[x].dchannels[y] == tmp->channel) {
								matchesdchan = 1;
								break;
							}
						}
					}
					offset = p.chanpos;
					if (!matchesdchan) {
						if (pris[span].nodetype && (pris[span].nodetype != pritype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s node\n", span + 1, pri_node2str(pris[span].nodetype));
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if (pris[span].switchtype && (pris[span].switchtype != myswitchtype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, pri_switch2str(pris[span].switchtype));
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if ((pris[span].dialplan) && (pris[span].dialplan != dialplan)) {
							ast_log(LOG_ERROR, "Span %d is already a %s dialing plan\n", span + 1, dialplan2str(pris[span].dialplan));
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if (!ast_strlen_zero(pris[span].idledial) && strcmp(pris[span].idledial, idledial)) {
							ast_log(LOG_ERROR, "Span %d already has idledial '%s'.\n", span + 1, idledial);
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if (!ast_strlen_zero(pris[span].idleext) && strcmp(pris[span].idleext, idleext)) {
							ast_log(LOG_ERROR, "Span %d already has idleext '%s'.\n", span + 1, idleext);
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if (pris[span].minunused && (pris[span].minunused != minunused)) {
							ast_log(LOG_ERROR, "Span %d already has minunused of %d.\n", span + 1, minunused);
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if (pris[span].minidle && (pris[span].minidle != minidle)) {
							ast_log(LOG_ERROR, "Span %d already has minidle of %d.\n", span + 1, minidle);
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						if (pris[span].numchans >= MAX_CHANNELS) {
							ast_log(LOG_ERROR, "Unable to add channel %d: Too many channels in trunk group %d!\n", channel,
								pris[span].trunkgroup);
							destroy_zt_pvt(&tmp);
							return NULL;
						}
						pris[span].nodetype = pritype;
						pris[span].switchtype = myswitchtype;
						pris[span].nsf = nsf;
						pris[span].dialplan = dialplan;
						pris[span].localdialplan = localdialplan;
						pris[span].pvts[pris[span].numchans++] = tmp;
						pris[span].minunused = minunused;
						pris[span].minidle = minidle;
						pris[span].overlapdial = overlapdial;
						pris[span].facilityenable = facilityenable;
						ast_copy_string(pris[span].idledial, idledial, sizeof(pris[span].idledial));
						ast_copy_string(pris[span].idleext, idleext, sizeof(pris[span].idleext));
						ast_copy_string(pris[span].internationalprefix, internationalprefix, sizeof(pris[span].internationalprefix));
						ast_copy_string(pris[span].nationalprefix, nationalprefix, sizeof(pris[span].nationalprefix));
						ast_copy_string(pris[span].localprefix, localprefix, sizeof(pris[span].localprefix));
						ast_copy_string(pris[span].privateprefix, privateprefix, sizeof(pris[span].privateprefix));
						ast_copy_string(pris[span].unknownprefix, unknownprefix, sizeof(pris[span].unknownprefix));
						pris[span].resetinterval = resetinterval;
						
						tmp->pri = &pris[span];
						tmp->prioffset = offset;
						tmp->call = NULL;
					} else {
						ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", offset);
						destroy_zt_pvt(&tmp);
						return NULL;
					}
				}
			} else {
				tmp->prioffset = 0;
			}
#endif
#ifdef ZAPATA_R2
			if (signalling == SIG_R2) {
				if (r2prot < 0) {
					ast_log(LOG_WARNING, "R2 Country not specified for channel %d -- Assuming China\n", tmp->channel);
					tmp->r2prot = MFCR2_PROT_CHINA;
				} else
					tmp->r2prot = r2prot;
				tmp->r2 = mfcr2_new(tmp->subs[SUB_REAL].zfd, tmp->r2prot, 1);
				if (!tmp->r2) {
					ast_log(LOG_WARNING, "Unable to create r2 call :(\n");
					zt_close(tmp->subs[SUB_REAL].zfd);
					destroy_zt_pvt(&tmp);
					return NULL;
				}
			} else {
				if (tmp->r2) 
					mfcr2_free(tmp->r2);
				tmp->r2 = NULL;
			}
#endif
		} else {
			signalling = tmp->sig;
			radio = tmp->radio;
			memset(&p, 0, sizeof(p));
			if (tmp->subs[SUB_REAL].zfd > -1)
				res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &p);
		}
		/* Adjust starttime on loopstart and kewlstart trunks to reasonable values */
		if ((signalling == SIG_FXSKS) || (signalling == SIG_FXSLS) ||
		    (signalling == SIG_EM) || (signalling == SIG_EM_E1) ||  (signalling == SIG_EMWINK) ||
			(signalling == SIG_FEATD) || (signalling == SIG_FEATDMF) || (signalling == SIG_FEATDMF_TA) ||
			  (signalling == SIG_FEATB) || (signalling == SIG_E911) ||
		    (signalling == SIG_SF) || (signalling == SIG_SFWINK) ||
			(signalling == SIG_SF_FEATD) || (signalling == SIG_SF_FEATDMF) ||
			  (signalling == SIG_SF_FEATB)) {
			p.starttime = 250;
		}
		if (radio) {
			/* XXX Waiting to hear back from Jim if these should be adjustable XXX */
			p.channo = channel;
			p.rxwinktime = 1;
			p.rxflashtime = 1;
			p.starttime = 1;
			p.debouncetime = 5;
		}
		if (!radio) {
			p.channo = channel;
			/* Override timing settings based on config file */
			if (cur_prewink >= 0)
				p.prewinktime = cur_prewink;
			if (cur_preflash >= 0)
				p.preflashtime = cur_preflash;
			if (cur_wink >= 0)
				p.winktime = cur_wink;
			if (cur_flash >= 0)
				p.flashtime = cur_flash;
			if (cur_start >= 0)
				p.starttime = cur_start;
			if (cur_rxwink >= 0)
				p.rxwinktime = cur_rxwink;
			if (cur_rxflash >= 0)
				p.rxflashtime = cur_rxflash;
			if (cur_debounce >= 0)
				p.debouncetime = cur_debounce;
		}
		
		/* dont set parms on a pseudo-channel (or CRV) */
		if (tmp->subs[SUB_REAL].zfd >= 0)
		{
			res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters\n");
				destroy_zt_pvt(&tmp);
				return NULL;
			}
		}
#if 1
		if (!here && (tmp->subs[SUB_REAL].zfd > -1)) {
			memset(&bi, 0, sizeof(bi));
			res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
				bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
				bi.numbufs = numbufs;
				res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", channel);
				}
			} else
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", channel);
		}
#endif
		tmp->immediate = immediate;
		tmp->transfertobusy = transfertobusy;
		tmp->sig = signalling;
		tmp->radio = radio;
		tmp->ringt_base = ringt_base;
		tmp->firstradio = 0;
		if ((signalling == SIG_FXOKS) || (signalling == SIG_FXOLS) || (signalling == SIG_FXOGS))
			tmp->permcallwaiting = callwaiting;
		else
			tmp->permcallwaiting = 0;
		/* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
		tmp->destroy = 0;
		tmp->drings = drings;
		tmp->usedistinctiveringdetection = usedistinctiveringdetection;
		tmp->callwaitingcallerid = callwaitingcallerid;
		tmp->threewaycalling = threewaycalling;
		tmp->adsi = adsi;
		tmp->permhidecallerid = hidecallerid;
		tmp->callreturn = callreturn;
		tmp->echocancel = echocancel;
		tmp->echotraining = echotraining;
		tmp->pulse = pulse;
		tmp->echocanbridged = echocanbridged;
		tmp->busydetect = busydetect;
		tmp->busycount = busycount;
		tmp->busy_tonelength = busy_tonelength;
		tmp->busy_quietlength = busy_quietlength;
		tmp->callprogress = callprogress;
		tmp->cancallforward = cancallforward;
		tmp->dtmfrelax = relaxdtmf;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->hidecallerid = tmp->permhidecallerid;
		tmp->channel = channel;
		tmp->stripmsd = stripmsd;
		tmp->use_callerid = use_callerid;
		tmp->cid_signalling = cid_signalling;
		tmp->cid_start = cid_start;
		tmp->zaptrcallerid = zaptrcallerid;
		tmp->restrictcid = restrictcid;
		tmp->use_callingpres = use_callingpres;
		tmp->priindication_oob = priindication_oob;
		tmp->priexclusive = cur_priexclusive;
		if (tmp->usedistinctiveringdetection) {
			if (!tmp->use_callerid) {
				ast_log(LOG_NOTICE, "Distinctive Ring detect requires 'usecallerid' be on\n");
				tmp->use_callerid = 1;
			}
		}

		ast_copy_string(tmp->accountcode, accountcode, sizeof(tmp->accountcode));
		tmp->amaflags = amaflags;
		if (!here) {
			tmp->confno = -1;
			tmp->propconfno = -1;
		}
		tmp->canpark = canpark;
		tmp->transfer = transfer;
		ast_copy_string(tmp->defcontext,context,sizeof(tmp->defcontext));
		ast_copy_string(tmp->language, language, sizeof(tmp->language));
		ast_copy_string(tmp->musicclass, musicclass, sizeof(tmp->musicclass));
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
		ast_copy_string(tmp->cid_num, cid_num, sizeof(tmp->cid_num));
		tmp->cid_ton = 0;
		ast_copy_string(tmp->cid_name, cid_name, sizeof(tmp->cid_name));
		ast_copy_string(tmp->mailbox, mailbox, sizeof(tmp->mailbox));
		tmp->msgstate = -1;
		tmp->group = cur_group;
		tmp->callgroup=cur_callergroup;
		tmp->pickupgroup=cur_pickupgroup;
		tmp->rxgain = rxgain;
		tmp->txgain = txgain;
		tmp->tonezone = tonezone;
		tmp->onhooktime = time(NULL);
		if (tmp->subs[SUB_REAL].zfd > -1) {
			set_actual_gain(tmp->subs[SUB_REAL].zfd, 0, tmp->rxgain, tmp->txgain, tmp->law);
			if (tmp->dsp)
				ast_dsp_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
			update_conf(tmp);
			if (!here) {
				if ((signalling != SIG_PRI) && (signalling != SIG_R2))
					/* Hang it up to be sure it's good */
					zt_set_hook(tmp->subs[SUB_REAL].zfd, ZT_ONHOOK);
			}
			ioctl(tmp->subs[SUB_REAL].zfd,ZT_SETTONEZONE,&tmp->tonezone);
#ifdef ZAPATA_PRI
			/* the dchannel is down so put the channel in alarm */
			if (tmp->pri && !pri_is_up(tmp->pri))
				tmp->inalarm = 1;
			else
				tmp->inalarm = 0;
#endif				
			memset(&si, 0, sizeof(si));
			if (ioctl(tmp->subs[SUB_REAL].zfd,ZT_SPANSTAT,&si) == -1) {
				ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
				destroy_zt_pvt(&tmp);
				return NULL;
			}
			if (si.alarms) tmp->inalarm = 1;
		}

		tmp->polarityonanswerdelay = polarityonanswerdelay;
		tmp->answeronpolarityswitch = answeronpolarityswitch;
		tmp->hanguponpolarityswitch = hanguponpolarityswitch;
		tmp->sendcalleridafter = sendcalleridafter;

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
			struct zt_pvt *working = *wlist;

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

static inline int available(struct zt_pvt *p, int channelmatch, int groupmatch, int *busy)
{
	int res;
	ZT_PARAMS par;
	/* First, check group matching */
	if ((p->group & groupmatch) != groupmatch)
		return 0;
	/* Check to see if we have a channel match */
	if ((channelmatch > 0) && (p->channel != channelmatch))
		return 0;
	/* We're at least busy at this point */
	if (busy) {
		if ((p->sig == SIG_FXOKS) || (p->sig == SIG_FXOLS) || (p->sig == SIG_FXOGS))
			*busy = 1;
	}
	/* If do not disturb, definitely not */
	if (p->dnd)
		return 0;
	/* If guard time, definitely not */
	if (p->guardtime && (time(NULL) < p->guardtime)) 
		return 0;
		
	/* If no owner definitely available */
	if (!p->owner) {
#ifdef ZAPATA_PRI
		/* Trust PRI */
		if (p->pri) {
			if (p->resetting || p->call)
				return 0;
			else
				return 1;
		}
#endif
#ifdef ZAPATA_R2
		/* Trust R2 as well */
		if (p->r2) {
			if (p->hasr2call || p->r2blocked)
				return 0;
			else
				return 1;
		}
#endif
		if (!p->radio)
		{
			if (!p->sig || (p->sig == SIG_FXSLS))
				return 1;
			/* Check hook state */
			if (p->subs[SUB_REAL].zfd > -1)
				res = ioctl(p->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &par);
			else {
				/* Assume not off hook on CVRS */
				res = 0;
				par.rxisoffhook = 0;
			}
			if (res) {
				ast_log(LOG_WARNING, "Unable to check hook state on channel %d\n", p->channel);
			} else if ((p->sig == SIG_FXSKS) || (p->sig == SIG_FXSGS)) {
				/* When "onhook" that means no battery on the line, and thus
				  it is out of service..., if it's on a TDM card... If it's a channel
				  bank, there is no telling... */
				if (par.rxbits > -1)
					return 1;
				if (par.rxisoffhook)
					return 1;
				else
#ifdef ZAP_CHECK_HOOKSTATE
					return 0;
#else
					return 1;
#endif
			} else if (par.rxisoffhook) {
				ast_log(LOG_DEBUG, "Channel %d off hook, can't use\n", p->channel);
				/* Not available when the other end is off hook */
				return 0;
			}
		}
		return 1;
	}

	/* If it's not an FXO, forget about call wait */
	if ((p->sig != SIG_FXOKS) && (p->sig != SIG_FXOLS) && (p->sig != SIG_FXOGS)) 
		return 0;

	if (!p->callwaiting) {
		/* If they don't have call waiting enabled, then for sure they're unavailable at this point */
		return 0;
	}

	if (p->subs[SUB_CALLWAIT].zfd > -1) {
		/* If there is already a call waiting call, then we can't take a second one */
		return 0;
	}
	
	if ((p->owner->_state != AST_STATE_UP) &&
		((p->owner->_state != AST_STATE_RINGING) || p->outgoing)) {
		/* If the current call is not up, then don't allow the call */
		return 0;
	}
	if ((p->subs[SUB_THREEWAY].owner) && (!p->subs[SUB_THREEWAY].inthreeway)) {
		/* Can't take a call wait when the three way calling hasn't been merged yet. */
		return 0;
	}
	/* We're cool */
	return 1;
}

static struct zt_pvt *chandup(struct zt_pvt *src)
{
	struct zt_pvt *p;
	ZT_BUFFERINFO bi;
	int res;
	p = malloc(sizeof(struct zt_pvt));
	if (p) {
		memcpy(p, src, sizeof(struct zt_pvt));
		ast_mutex_init(&p->lock);
		p->subs[SUB_REAL].zfd = zt_open("/dev/zap/pseudo");
		/* Allocate a zapata structure */
		if (p->subs[SUB_REAL].zfd < 0) {
			ast_log(LOG_ERROR, "Unable to dup channel: %s\n",  strerror(errno));
			destroy_zt_pvt(&p);
			return NULL;
		}
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_GET_BUFINFO, &bi);
		if (!res) {
			bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
			bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
			bi.numbufs = numbufs;
			res = ioctl(p->subs[SUB_REAL].zfd, ZT_SET_BUFINFO, &bi);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to set buffer policy on dup channel\n");
			}
		} else
			ast_log(LOG_WARNING, "Unable to check buffer policy on dup channel\n");
	}
	p->destroy = 1;
	p->next = iflist;
	iflist = p;
	return p;
}
	

#ifdef ZAPATA_PRI
static int pri_find_empty_chan(struct zt_pri *pri, int backwards)
{
	int x;
	if (backwards)
		x = pri->numchans;
	else
		x = 0;
	for (;;) {
		if (backwards && (x < 0))
			break;
		if (!backwards && (x >= pri->numchans))
			break;
		if (pri->pvts[x] && !pri->pvts[x]->inalarm && !pri->pvts[x]->owner) {
			ast_log(LOG_DEBUG, "Found empty available channel %d/%d\n", 
				pri->pvts[x]->logicalspan, pri->pvts[x]->prioffset);
			return x;
		}
		if (backwards)
			x--;
		else
			x++;
	}
	return -1;
}
#endif

static struct ast_channel *zt_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	int groupmatch = 0;
	int channelmatch = -1;
	int roundrobin = 0;
	int callwait = 0;
	int busy = 0;
	struct zt_pvt *p;
	struct ast_channel *tmp = NULL;
	char *dest=NULL;
	int x;
	char *s;
	char opt=0;
	int res=0, y=0;
	int backwards = 0;
#ifdef ZAPATA_PRI
	int crv;
	int bearer = -1;
	int trunkgroup;
	struct zt_pri *pri=NULL;
#endif	
	struct zt_pvt *exit, *start, *end;
	ast_mutex_t *lock;
	
	/* Assume we're locking the iflock */
	lock = &iflock;
	start = iflist;
	end = ifend;
	/* We do signed linear */
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR | AST_FORMAT_ULAW);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	if (data) {
		dest = ast_strdupa((char *)data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	if (toupper(dest[0]) == 'G' || toupper(dest[0])=='R') {
		/* Retrieve the group number */
		char *stringp=NULL;
		stringp=dest + 1;
		s = strsep(&stringp, "/");
		if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", (char *)data);
			return NULL;
		}
		groupmatch = 1 << x;
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
		char *stringp=NULL;
		stringp=dest;
		s = strsep(&stringp, "/");
		p = iflist;
		if (!strcasecmp(s, "pseudo")) {
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
			channelmatch = x;
		} 
#ifdef ZAPATA_PRI
		else if ((res = sscanf(s, "%d:%d%c%d", &trunkgroup, &crv, &opt, &y)) > 1) {
			if ((trunkgroup < 1) || (crv < 1)) {
				ast_log(LOG_WARNING, "Unable to determine trunk group and CRV for data %s\n", (char *)data);
				return NULL;
			}
			res--;
			for (x=0;x<NUM_SPANS;x++) {
				if (pris[x].trunkgroup == trunkgroup) {
					pri = pris + x;
					lock = &pri->lock;
					start = pri->crvs;
					end = pri->crvend;
					break;
				}
			}
			if (!pri) {
				ast_log(LOG_WARNING, "Unable to find trunk group %d\n", trunkgroup);
				return NULL;
			}
			channelmatch = crv;
			p = pris[x].crvs;
		}
#endif	
		else if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			ast_log(LOG_WARNING, "Unable to determine channel for data %s\n", (char *)data);
			return NULL;
		} else {
			channelmatch = x;
		}
	}
	/* Search for an unowned channel */
	if (ast_mutex_lock(lock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	exit = p;
	while(p && !tmp) {
		if (roundrobin)
			round_robin[x] = p;
#if 0
		ast_verbose("name = %s, %d, %d, %d\n",p->owner ? p->owner->name : "<none>", p->channel, channelmatch, groupmatch);
#endif
		if (p && available(p, channelmatch, groupmatch, &busy)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Using channel %d\n", p->channel);
				if (p->inalarm) 
					goto next;

			callwait = (p->owner != NULL);
#ifdef ZAPATA_PRI
			if (pri && (p->subs[SUB_REAL].zfd < 0)) {
				if (p->sig != SIG_FXSKS) {
					/* Gotta find an actual channel to use for this
					   CRV if this isn't a callwait */
					bearer = pri_find_empty_chan(pri, 0);
					if (bearer < 0) {
						ast_log(LOG_NOTICE, "Out of bearer channels on span %d for call to CRV %d:%d\n", pri->span, trunkgroup, crv);
						p = NULL;
						break;
					}
					pri_assign_bearer(p, pri, pri->pvts[bearer]);
				} else {
					if (alloc_sub(p, 0)) {
						ast_log(LOG_NOTICE, "Failed to allocate place holder pseudo channel!\n");
						p = NULL;
						break;
					} else
						ast_log(LOG_DEBUG, "Allocated placeholder pseudo channel\n");
					p->pri = pri;
				}
			}
#endif			
			if (p->channel == CHAN_PSEUDO) {
				p = chandup(p);
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
			tmp = zt_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, 0);
#ifdef ZAPATA_PRI
			if (p->bearer) {
				/* Log owner to bearer channel, too */
				p->bearer->owner = tmp;
			}
#endif			
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
next:
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
		if (p == exit)
			break;
	}
	ast_mutex_unlock(lock);
	restart_monitor();
	if (callwait || (!tmp && busy))
		*cause = AST_CAUSE_BUSY;
	return tmp;
}


#ifdef ZAPATA_PRI
static struct zt_pvt *pri_find_crv(struct zt_pri *pri, int crv)
{
	struct zt_pvt *p;
	p = pri->crvs;
	while(p) {
		if (p->channel == crv)
			return p;
		p = p->next;
	}
	return NULL;
}


static int pri_find_principle(struct zt_pri *pri, int channel)
{
	int x;
	int span = PRI_SPAN(channel);
	int spanfd;
	ZT_PARAMS param;
	int principle = -1;
	int explicit = PRI_EXPLICIT(channel);
	span = PRI_SPAN(channel);
	channel = PRI_CHANNEL(channel);

	if (!explicit) {
		spanfd = pri_active_dchan_fd(pri);
		if (ioctl(spanfd, ZT_GET_PARAMS, &param))
			return -1;
		span = pris[param.spanno - 1].prilogicalspan;
	}

	for (x=0;x<pri->numchans;x++) {
		if (pri->pvts[x] && (pri->pvts[x]->prioffset == channel) && (pri->pvts[x]->logicalspan == span)) {
			principle = x;
			break;
		}
	}
	
	return principle;
}

static int pri_fixup_principle(struct zt_pri *pri, int principle, q931_call *c)
{
	int x;
	struct zt_pvt *crv;
	if (!c) {
		if (principle < 0)
			return -1;
		return principle;
	}
	if ((principle > -1) && 
		(principle < pri->numchans) && 
		(pri->pvts[principle]) && 
		(pri->pvts[principle]->call == c))
		return principle;
	/* First, check for other bearers */
	for (x=0;x<pri->numchans;x++) {
		if (!pri->pvts[x]) continue;
		if (pri->pvts[x]->call == c) {
			/* Found our call */
			if (principle != x) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Moving call from channel %d to channel %d\n",
						pri->pvts[x]->channel, pri->pvts[principle]->channel);
				if (pri->pvts[principle]->owner) {
					ast_log(LOG_WARNING, "Can't fix up channel from %d to %d because %d is already in use\n",
						pri->pvts[x]->channel, pri->pvts[principle]->channel, pri->pvts[principle]->channel);
					return -1;
				}
				/* Fix it all up now */
				pri->pvts[principle]->owner = pri->pvts[x]->owner;
				if (pri->pvts[principle]->owner) {
					snprintf(pri->pvts[principle]->owner->name, sizeof(pri->pvts[principle]->owner->name), 
						"Zap/%d:%d-%d", pri->trunkgroup, pri->pvts[principle]->channel, 1);
					pri->pvts[principle]->owner->tech_pvt = pri->pvts[principle];
					pri->pvts[principle]->owner->fds[0] = pri->pvts[principle]->subs[SUB_REAL].zfd;
					pri->pvts[principle]->subs[SUB_REAL].owner = pri->pvts[x]->subs[SUB_REAL].owner;
				} else
					ast_log(LOG_WARNING, "Whoa, there's no  owner, and we're having to fix up channel %d to channel %d\n", pri->pvts[x]->channel, pri->pvts[principle]->channel);
				pri->pvts[principle]->call = pri->pvts[x]->call;
				/* Free up the old channel, now not in use */
				pri->pvts[x]->subs[SUB_REAL].owner = NULL;
				pri->pvts[x]->owner = NULL;
				pri->pvts[x]->call = NULL;
			}
			return principle;
		}
	}
	/* Now check for a CRV with no bearer */
	crv = pri->crvs;
	while(crv) {
		if (crv->call == c) {
			/* This is our match...  Perform some basic checks */
			if (crv->bearer)
				ast_log(LOG_WARNING, "Trying to fix up call which already has a bearer which isn't the one we think it is\n");
			else if (pri->pvts[principle]->owner) 
				ast_log(LOG_WARNING, "Tring to fix up a call to a bearer which already has an owner!\n");
			else {
				/* Looks good.  Drop the pseudo channel now, clear up the assignment, and
				   wakeup the potential sleeper */
				zt_close(crv->subs[SUB_REAL].zfd);
				pri->pvts[principle]->call = crv->call;
				pri_assign_bearer(crv, pri, pri->pvts[principle]);
				ast_log(LOG_DEBUG, "Assigning bearer %d/%d to CRV %d:%d\n",
									pri->pvts[principle]->logicalspan, pri->pvts[principle]->prioffset,
									pri->trunkgroup, crv->channel);
				wakeup_sub(crv, SUB_REAL, pri);
			}
			return principle;
		}
		crv = crv->next;
	}
	ast_log(LOG_WARNING, "Call specified, but not found?\n");
	return -1;
}

static void *do_idle_thread(void *vchan)
{
	struct ast_channel *chan = vchan;
	struct zt_pvt *pvt = chan->tech_pvt;
	struct ast_frame *f;
	char ex[80];
	/* Wait up to 30 seconds for an answer */
	int newms, ms = 30000;
	if (option_verbose > 2) 
		ast_verbose(VERBOSE_PREFIX_3 "Initiating idle call on channel %s\n", chan->name);
	snprintf(ex, sizeof(ex), "%d/%s", pvt->channel, pvt->pri->idledial);
	if (ast_call(chan, ex, 0)) {
		ast_log(LOG_WARNING, "Idle dial failed on '%s' to '%s'\n", chan->name, ex);
		ast_hangup(chan);
		return NULL;
	}
	while((newms = ast_waitfor(chan, ms)) > 0) {
		f = ast_read(chan);
		if (!f) {
			/* Got hangup */
			break;
		}
		if (f->frametype == AST_FRAME_CONTROL) {
			switch(f->subclass) {
			case AST_CONTROL_ANSWER:
				/* Launch the PBX */
				ast_copy_string(chan->exten, pvt->pri->idleext, sizeof(chan->exten));
				ast_copy_string(chan->context, pvt->pri->idlecontext, sizeof(chan->context));
				chan->priority = 1;
				if (option_verbose > 3) 
					ast_verbose(VERBOSE_PREFIX_3 "Idle channel '%s' answered, sending to %s@%s\n", chan->name, chan->exten, chan->context);
				ast_pbx_run(chan);
				/* It's already hungup, return immediately */
				return NULL;
			case AST_CONTROL_BUSY:
				if (option_verbose > 3) 
					ast_verbose(VERBOSE_PREFIX_3 "Idle channel '%s' busy, waiting...\n", chan->name);
				break;
			case AST_CONTROL_CONGESTION:
				if (option_verbose > 3) 
					ast_verbose(VERBOSE_PREFIX_3 "Idle channel '%s' congested, waiting...\n", chan->name);
				break;
			};
		}
		ast_frfree(f);
		ms = newms;
	}
#if 0
	printf("Hanging up '%s'\n", chan->name);
#endif
	/* Hangup the channel since nothing happend */
	ast_hangup(chan);
	return NULL;
}

#ifndef PRI_RESTART
#error "Upgrade your libpri"
#endif
static void zt_pri_message(struct pri *pri, char *s)
{
	int x, y;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < NUM_DCHANS; y++) {
				if (pris[x].dchans[y])
					dchancount++;

				if (pris[x].dchans[y] == pri)
					dchan = y;
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if ((dchan >= 0) && (span >= 0)) {
			if (dchancount > 1)
				ast_verbose("[Span %d D-Channel %d]%s", span, dchan, s);
			else
				ast_verbose("%s", s);
		} else
			ast_verbose("PRI debug error: could not find pri associated it with debug message output\n");
	} else
		ast_verbose("%s", s);

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0)
		write(pridebugfd, s, strlen(s));

	ast_mutex_unlock(&pridebugfdlock);
}

static void zt_pri_error(struct pri *pri, char *s)
{
	int x, y;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < NUM_DCHANS; y++) {
				if (pris[x].dchans[y])
					dchancount++;

				if (pris[x].dchans[y] == pri)
					dchan = y;
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if ((dchan >= 0) && (span >= 0)) {
			if (dchancount > 1)
				ast_log(LOG_WARNING, "[Span %d D-Channel %d] PRI: %s", span, dchan, s);
			else
				ast_verbose("%s", s);
		} else
			ast_verbose("PRI debug error: could not find pri associated it with debug message output\n");
	} else
		ast_log(LOG_WARNING, "%s", s);

	ast_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0)
		write(pridebugfd, s, strlen(s));

	ast_mutex_unlock(&pridebugfdlock);
}

static int pri_check_restart(struct zt_pri *pri)
{
	do {
		pri->resetpos++;
	} while((pri->resetpos < pri->numchans) &&
		 (!pri->pvts[pri->resetpos] ||
		  pri->pvts[pri->resetpos]->call ||
		  pri->pvts[pri->resetpos]->resetting));
	if (pri->resetpos < pri->numchans) {
		/* Mark the channel as resetting and restart it */
		pri->pvts[pri->resetpos]->resetting = 1;
		pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[pri->resetpos]));
	} else {
		pri->resetting = 0;
		time(&pri->lastreset);
	}
	return 0;
}

static int pri_hangup_all(struct zt_pvt *p, struct zt_pri *pri)
{
	int x;
	int redo;
	ast_mutex_unlock(&pri->lock);
	ast_mutex_lock(&p->lock);
	do {
		redo = 0;
		for (x=0;x<3;x++) {
			while(p->subs[x].owner && ast_mutex_trylock(&p->subs[x].owner->lock)) {
				redo++;
				ast_mutex_unlock(&p->lock);
				usleep(1);
				ast_mutex_lock(&p->lock);
			}
			if (p->subs[x].owner) {
				ast_queue_hangup(p->subs[x].owner);
				ast_mutex_unlock(&p->subs[x].owner->lock);
			}
		}
	} while (redo);
	ast_mutex_unlock(&p->lock);
	ast_mutex_lock(&pri->lock);
	return 0;
}
char * redirectingreason2str(int redirectingreason)
{
	switch (redirectingreason) {
	case 0:
		return "UNKNOWN";
	case 1:
		return "BUSY";
	case 2:
		return "NO_REPLY";
	case 0xF:
		return "UNCONDITIONAL";
	default:
		return "NOREDIRECT";
	}
}

static void apply_plan_to_number(char *buf, size_t size, const struct zt_pri *pri, const char *number, const int plan)
{
	switch (plan) {
	case PRI_INTERNATIONAL_ISDN:		/* Q.931 dialplan == 0x11 international dialplan => prepend international prefix digits */
		snprintf(buf, size, "%s%s", pri->internationalprefix, number);
		break;
	case PRI_NATIONAL_ISDN:			/* Q.931 dialplan == 0x21 national dialplan => prepend national prefix digits */
		snprintf(buf, size, "%s%s", pri->nationalprefix, number);
		break;
	case PRI_LOCAL_ISDN:			/* Q.931 dialplan == 0x41 local dialplan => prepend local prefix digits */
		snprintf(buf, size, "%s%s", pri->localprefix, number);
		break;
	case PRI_PRIVATE:			/* Q.931 dialplan == 0x49 private dialplan => prepend private prefix digits */
		snprintf(buf, size, "%s%s", pri->privateprefix, number);
		break;
	case PRI_UNKNOWN:			/* Q.931 dialplan == 0x00 unknown dialplan => prepend unknown prefix digits */
		snprintf(buf, size, "%s%s", pri->unknownprefix, number);
		break;
	default:				/* other Q.931 dialplan => don't twiddle with callingnum */
		snprintf(buf, size, "%s", number);
		break;
	}
}

static void *pri_dchannel(void *vpri)
{
	struct zt_pri *pri = vpri;
	pri_event *e;
	struct pollfd fds[NUM_DCHANS];
	int res;
	int chanpos = 0;
	int x;
	int haveidles;
	int activeidles;
	int nextidle = -1;
	struct ast_channel *c;
	struct timeval tv, lowest, *next;
	struct timeval lastidle = { 0, 0 };
	int doidling=0;
	char *cc;
	char idlen[80];
	struct ast_channel *idle;
	pthread_t p;
	time_t t;
	int i, which=-1;
	int numdchans;
	int cause=0;
	struct zt_pvt *crv;
	pthread_t threadid;
	pthread_attr_t attr;
	char ani2str[6];
	char plancallingnum[256];
	char plancallingani[256];
	char calledtonstr[10];
	
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	gettimeofday(&lastidle, NULL);
	if (!ast_strlen_zero(pri->idledial) && !ast_strlen_zero(pri->idleext)) {
		/* Need to do idle dialing, check to be sure though */
		cc = strchr(pri->idleext, '@');
		if (cc) {
			*cc = '\0';
			cc++;
			ast_copy_string(pri->idlecontext, cc, sizeof(pri->idlecontext));
#if 0
			/* Extensions may not be loaded yet */
			if (!ast_exists_extension(NULL, pri->idlecontext, pri->idleext, 1, NULL))
				ast_log(LOG_WARNING, "Extension '%s @ %s' does not exist\n", pri->idleext, pri->idlecontext);
			else
#endif
				doidling = 1;
		} else
			ast_log(LOG_WARNING, "Idle dial string '%s' lacks '@context'\n", pri->idleext);
	}
	for(;;) {
		for (i=0;i<NUM_DCHANS;i++) {
			if (!pri->dchannels[i])
				break;
			fds[i].fd = pri->fds[i];
			fds[i].events = POLLIN | POLLPRI;
			fds[i].revents = 0;
		}
		numdchans = i;
		time(&t);
		ast_mutex_lock(&pri->lock);
		if (pri->switchtype != PRI_SWITCH_GR303_TMC && (pri->resetinterval > 0)) {
			if (pri->resetting && pri_is_up(pri)) {
				if (pri->resetpos < 0)
					pri_check_restart(pri);
			} else {
				if (!pri->resetting	&& (t - pri->lastreset) >= pri->resetinterval) {
					pri->resetting = 1;
					pri->resetpos = -1;
				}
			}
		}
		/* Look for any idle channels if appropriate */
		if (doidling && pri_is_up(pri)) {
			nextidle = -1;
			haveidles = 0;
			activeidles = 0;
			for (x=pri->numchans;x>=0;x--) {
				if (pri->pvts[x] && !pri->pvts[x]->owner && 
				    !pri->pvts[x]->call) {
					if (haveidles < pri->minunused) {
						haveidles++;
					} else if (!pri->pvts[x]->resetting) {
						nextidle = x;
						break;
					}
				} else if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall)
					activeidles++;
			}
#if 0
			printf("nextidle: %d, haveidles: %d, minunsed: %d\n",
				nextidle, haveidles, minunused);
			printf("nextidle: %d, haveidles: %d, ms: %ld, minunsed: %d\n",
				nextidle, haveidles, ast_tvdiff_ms(ast_tvnow(), lastidle), minunused);
#endif
			if (nextidle > -1) {
				if (ast_tvdiff_ms(ast_tvnow(), lastidle) > 1000) {
					/* Don't create a new idle call more than once per second */
					snprintf(idlen, sizeof(idlen), "%d/%s", pri->pvts[nextidle]->channel, pri->idledial);
					idle = zt_request("Zap", AST_FORMAT_ULAW, idlen, &cause);
					if (idle) {
						pri->pvts[nextidle]->isidlecall = 1;
						if (ast_pthread_create(&p, NULL, do_idle_thread, idle)) {
							ast_log(LOG_WARNING, "Unable to start new thread for idle channel '%s'\n", idle->name);
							zt_hangup(idle);
						}
					} else
						ast_log(LOG_WARNING, "Unable to request channel 'Zap/%s' for idle call\n", idlen);
					gettimeofday(&lastidle, NULL);
				}
			} else if ((haveidles < pri->minunused) &&
				   (activeidles > pri->minidle)) {
				/* Mark something for hangup if there is something 
				   that can be hungup */
				for (x=pri->numchans;x>=0;x--) {
					/* find a candidate channel */
					if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall) {
						pri->pvts[x]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
						haveidles++;
						/* Stop if we have enough idle channels or
						  can't spare any more active idle ones */
						if ((haveidles >= pri->minunused) ||
						    (activeidles <= pri->minidle))
							break;
					} 
				}
			}
		}
		/* Start with reasonable max */
		lowest = ast_tv(60, 0);
		for (i=0; i<NUM_DCHANS; i++) {
			/* Find lowest available d-channel */
			if (!pri->dchannels[i])
				break;
			if ((next = pri_schedule_next(pri->dchans[i]))) {
				/* We need relative time here */
				tv = ast_tvsub(*next, ast_tvnow());
				if (tv.tv_sec < 0) {
					tv = ast_tv(0,0);
				}
				if (doidling || pri->resetting) {
					if (tv.tv_sec > 1) {
						tv = ast_tv(1, 0);
					}
				} else {
					if (tv.tv_sec > 60) {
						tv = ast_tv(60, 0);
					}
				}
			} else if (doidling || pri->resetting) {
				/* Make sure we stop at least once per second if we're
				   monitoring idle channels */
				tv = ast_tv(1,0);
			} else {
				/* Don't poll for more than 60 seconds */
				tv = ast_tv(60, 0);
			}
			if (!i || ast_tvcmp(tv, lowest) < 0) {
				lowest = tv;
			}
		}
		ast_mutex_unlock(&pri->lock);

		e = NULL;
		res = poll(fds, numdchans, lowest.tv_sec * 1000 + lowest.tv_usec / 1000);

		ast_mutex_lock(&pri->lock);
		if (!res) {
			for (which=0;which<NUM_DCHANS;which++) {
				if (!pri->dchans[which])
					break;
				/* Just a timeout, run the scheduler */
				e = pri_schedule_run(pri->dchans[which]);
				if (e)
					break;
			}
		} else if (res > -1) {
			for (which=0;which<NUM_DCHANS;which++) {
				if (!pri->dchans[which])
					break;
				if (fds[which].revents & POLLPRI) {
					/* Check for an event */
					x = 0;
					res = ioctl(pri->fds[which], ZT_GETEVENT, &x);
					if (x) 
						ast_log(LOG_NOTICE, "PRI got event: %s (%d) on %s D-channel of span %d\n", event2str(x), x, pri_order(which), pri->span);
					/* Keep track of alarm state */	
					if (x == ZT_EVENT_ALARM) {
						pri->dchanavail[which] &= ~(DCHAN_NOTINALARM | DCHAN_UP);
						pri_find_dchan(pri);
					} else if (x == ZT_EVENT_NOALARM) {
						pri->dchanavail[which] |= DCHAN_NOTINALARM;
						pri_restart(pri->dchans[which]);
					}
				
					if (option_debug)
						ast_log(LOG_DEBUG, "Got event %s (%d) on D-channel for span %d\n", event2str(x), x, pri->span);
				} else if (fds[which].revents & POLLIN) {
					e = pri_check_event(pri->dchans[which]);
				}
				if (e)
					break;
			}
		} else if (errno != EINTR)
			ast_log(LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));

		if (e) {
			if (pri->debug)
				pri_dump_event(pri->dchans[which], e);
			if (e->e != PRI_EVENT_DCHAN_DOWN)
				pri->dchanavail[which] |= DCHAN_UP;
			switch(e->e) {
			case PRI_EVENT_DCHAN_UP:
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "%s D-Channel on span %d up\n", pri_order(which), pri->span);
				pri->dchanavail[which] |= DCHAN_UP;
				if (!pri->pri) pri_find_dchan(pri);

				/* Note presense of D-channel */
				time(&pri->lastreset);

				/* Restart in 5 seconds */
				if (pri->resetinterval > -1) {
					pri->lastreset -= pri->resetinterval;
					pri->lastreset += 5;
				}
				pri->resetting = 0;
				/* Take the channels from inalarm condition */
				for (i=0; i<pri->numchans; i++)
					if (pri->pvts[i]) {
						pri->pvts[i]->inalarm = 0;
					}
				break;
			case PRI_EVENT_DCHAN_DOWN:
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "%s D-Channel on span %d down\n", pri_order(which), pri->span);
				pri->dchanavail[which] &= ~DCHAN_UP;
				pri_find_dchan(pri);
				if (!pri_is_up(pri)) {
					pri->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					for (i=0; i<pri->numchans; i++) {
						struct zt_pvt *p = pri->pvts[i];
						if (p) {
							if (p->call) {
								if (p->pri && p->pri->pri) {
									pri_hangup(p->pri->pri, p->call, -1);
									pri_destroycall(p->pri->pri, p->call);
									p->call = NULL;
								} else
									ast_log(LOG_WARNING, "The PRI Call have not been destroyed\n");
							}
							if (p->realcall) {
								pri_hangup_all(p->realcall, pri);
							} else if (p->owner)
								p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							p->inalarm = 1;
						}
					}
				}
				break;
			case PRI_EVENT_RESTART:
				if (e->restart.channel > -1) {
					chanpos = pri_find_principle(pri, e->restart.channel);
					if (chanpos < 0)
						ast_log(LOG_WARNING, "Restart requested on odd/unavailable channel number %d/%d on span %d\n", 
							PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
					else {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "B-channel %d/%d restarted on span %d\n", 
								PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						if (pri->pvts[chanpos]->call) {
							pri_destroycall(pri->pri, pri->pvts[chanpos]->call);
							pri->pvts[chanpos]->call = NULL;
						}
						/* Force soft hangup if appropriate */
						if (pri->pvts[chanpos]->realcall) 
							pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
						else if (pri->pvts[chanpos]->owner)
							pri->pvts[chanpos]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				} else {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_2 "Restart on requested on entire span %d\n", pri->span);
					for (x=0;x < pri->numchans;x++)
						if (pri->pvts[x]) {
							ast_mutex_lock(&pri->pvts[x]->lock);
							if (pri->pvts[x]->call) {
								pri_destroycall(pri->pri, pri->pvts[x]->call);
								pri->pvts[x]->call = NULL;
							}
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
 							else if (pri->pvts[x]->owner)
								pri->pvts[x]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							ast_mutex_unlock(&pri->pvts[x]->lock);
						}
				}
				break;
			case PRI_EVENT_KEYPAD_DIGIT:
				chanpos = pri_find_principle(pri, e->digit.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "KEYPAD_DIGITs received on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->digit.channel), PRI_CHANNEL(e->digit.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->digit.call);
					if (chanpos > -1) {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding KEYPAD_DIGITs further on */
						if (pri->overlapdial && pri->pvts[chanpos]->call==e->digit.call && pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->digit.digits);
							char digit;
							int i;					
							for (i=0; i<digitlen; i++) {	
								digit = e->digit.digits[i];
								{
									struct ast_frame f = { AST_FRAME_DTMF, digit, };
									zap_queue_frame(pri->pvts[chanpos], &f, pri);
								}
							}
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
				
			case PRI_EVENT_INFO_RECEIVED:
				chanpos = pri_find_principle(pri, e->ring.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "INFO received on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->ring.call);
					if (chanpos > -1) {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding INFORMATION further on */
						if (pri->overlapdial && pri->pvts[chanpos]->call==e->ring.call && pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->ring.callednum);
							char digit;
							int i;					
							for (i=0; i<digitlen; i++) {	
								digit = e->ring.callednum[i];
								{
									struct ast_frame f = { AST_FRAME_DTMF, digit, };
									zap_queue_frame(pri->pvts[chanpos], &f, pri);
								}
							}
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_RING:
				crv = NULL;
				if (e->ring.channel == -1)
					chanpos = pri_find_empty_chan(pri, 1);
				else
					chanpos = pri_find_principle(pri, e->ring.channel);
				/* if no channel specified find one empty */
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ring requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					ast_mutex_lock(&pri->pvts[chanpos]->lock);
					if (pri->pvts[chanpos]->owner) {
						if (pri->pvts[chanpos]->call == e->ring.call) {
							ast_log(LOG_WARNING, "Duplicate setup requested on channel %d/%d already in use on span %d\n", 
								PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							break;
						} else {
							ast_log(LOG_WARNING, "Ring requested on channel %d/%d already in use on span %d.  Hanging up owner.\n", 
							PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
							else
								pri->pvts[chanpos]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							ast_mutex_unlock(&pri->pvts[chanpos]->lock);
							chanpos = -1;
						}
					}
					if (chanpos > -1)
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
				}
				if ((chanpos < 0) && (e->ring.flexible))
					chanpos = pri_find_empty_chan(pri, 1);
				if (chanpos > -1) {
					ast_mutex_lock(&pri->pvts[chanpos]->lock);
					if (pri->switchtype == PRI_SWITCH_GR303_TMC) {
						/* Should be safe to lock CRV AFAIK while bearer is still locked */
						crv = pri_find_crv(pri, pri_get_crv(pri->pri, e->ring.call, NULL));
						if (crv)
							ast_mutex_lock(&crv->lock);
						if (!crv || crv->owner) {
							pri->pvts[chanpos]->call = NULL;
							if (crv) {
								if (crv->owner)
									crv->owner->_softhangup |= AST_SOFTHANGUP_DEV;
								ast_log(LOG_WARNING, "Call received for busy CRV %d on span %d\n", pri_get_crv(pri->pri, e->ring.call, NULL), pri->span);
							} else
								ast_log(LOG_NOTICE, "Call received for unconfigured CRV %d on span %d\n", pri_get_crv(pri->pri, e->ring.call, NULL), pri->span);
							pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_INVALID_CALL_REFERENCE);
							if (crv)
								ast_mutex_unlock(&crv->lock);
							ast_mutex_unlock(&pri->pvts[chanpos]->lock);
							break;
						}
					}
					pri->pvts[chanpos]->call = e->ring.call;
					apply_plan_to_number(plancallingnum, sizeof(plancallingnum), pri, e->ring.callingnum, e->ring.callingplan);
					if (pri->pvts[chanpos]->use_callerid) {
						ast_shrink_phone_number(plancallingnum);
						ast_copy_string(pri->pvts[chanpos]->cid_num, plancallingnum, sizeof(pri->pvts[chanpos]->cid_num));
#ifdef PRI_ANI
						if (!ast_strlen_zero(e->ring.callingani)) {
							apply_plan_to_number(plancallingani, sizeof(plancallingani), pri, e->ring.callingani, e->ring.callingplanani);
							ast_shrink_phone_number(plancallingani);
							ast_copy_string(pri->pvts[chanpos]->cid_ani, plancallingani, sizeof(pri->pvts[chanpos]->cid_ani));
						} else {
							pri->pvts[chanpos]->cid_ani[0] = '\0';
						}
#endif
						ast_copy_string(pri->pvts[chanpos]->cid_name, e->ring.callingname, sizeof(pri->pvts[chanpos]->cid_name));
						pri->pvts[chanpos]->cid_ton = e->ring.callingplan; /* this is the callingplan (TON/NPI), e->ring.callingplan>>4 would be the TON */
					} else {
						pri->pvts[chanpos]->cid_num[0] = '\0';
						pri->pvts[chanpos]->cid_ani[0] = '\0';
						pri->pvts[chanpos]->cid_name[0] = '\0';
						pri->pvts[chanpos]->cid_ton = 0;
					}
					apply_plan_to_number(pri->pvts[chanpos]->rdnis, sizeof(pri->pvts[chanpos]->rdnis), pri,
							     e->ring.redirectingnum, e->ring.callingplanrdnis);
					/* If immediate=yes go to s|1 */
					if (pri->pvts[chanpos]->immediate) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Going to extension s|1 because of immediate=yes\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* Get called number */
					else if (!ast_strlen_zero(e->ring.callednum)) {
						ast_copy_string(pri->pvts[chanpos]->exten, e->ring.callednum, sizeof(pri->pvts[chanpos]->exten));
						ast_copy_string(pri->pvts[chanpos]->dnid, e->ring.callednum, sizeof(pri->pvts[chanpos]->dnid));
					} else
						pri->pvts[chanpos]->exten[0] = '\0';
					/* Set DNID on all incoming calls -- even immediate */
					if (!ast_strlen_zero(e->ring.callednum))
						ast_copy_string(pri->pvts[chanpos]->dnid, e->ring.callednum, sizeof(pri->pvts[chanpos]->dnid));
					/* No number yet, but received "sending complete"? */
					if (e->ring.complete && (ast_strlen_zero(e->ring.callednum))) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Going to extension s|1 because of Complete received\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* Make sure extension exists (or in overlap dial mode, can exist) */
					if ((pri->overlapdial && ast_canmatch_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) ||
						ast_exists_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
						/* Setup law */
						int law;
						if (pri->switchtype != PRI_SWITCH_GR303_TMC) {
							/* Set to audio mode at this point */
							law = 1;
							if (ioctl(pri->pvts[chanpos]->subs[SUB_REAL].zfd, ZT_AUDIOMODE, &law) == -1)
								ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d\n", pri->pvts[chanpos]->channel, law);
						}
						if (e->ring.layer1 == PRI_LAYER_1_ALAW)
							law = ZT_LAW_ALAW;
						else
							law = ZT_LAW_MULAW;
						res = zt_setlaw(pri->pvts[chanpos]->subs[SUB_REAL].zfd, law);
						if (res < 0) 
							ast_log(LOG_WARNING, "Unable to set law on channel %d\n", pri->pvts[chanpos]->channel);
						res = set_actual_gain(pri->pvts[chanpos]->subs[SUB_REAL].zfd, 0, pri->pvts[chanpos]->rxgain, pri->pvts[chanpos]->txgain, law);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to set gains on channel %d\n", pri->pvts[chanpos]->channel);
						if (e->ring.complete || !pri->overlapdial)
							/* Just announce proceeding */
							pri_proceeding(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 0);
						else  {
							if (pri->switchtype != PRI_SWITCH_GR303_TMC) 
								pri_need_more_info(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
							else
								pri_answer(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
						}
						/* Get the use_callingpres state */
						pri->pvts[chanpos]->callingpres = e->ring.callingpres;
					
						/* Start PBX */
						if (pri->overlapdial && ast_matchmore_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
							/* Release the PRI lock while we create the channel */
							ast_mutex_unlock(&pri->lock);
							if (crv) {
								/* Set bearer and such */
								pri_assign_bearer(crv, pri, pri->pvts[chanpos]);
								c = zt_new(crv, AST_STATE_RESERVED, 0, SUB_REAL, law, e->ring.ctype);
								pri->pvts[chanpos]->owner = &inuse;
								ast_log(LOG_DEBUG, "Started up crv %d:%d on bearer channel %d\n", pri->trunkgroup, crv->channel, crv->bearer->channel);
							} else {
								c = zt_new(pri->pvts[chanpos], AST_STATE_RESERVED, 0, SUB_REAL, law, e->ring.ctype);
							}
							if (!ast_strlen_zero(e->ring.callingsubaddr)) {
								pbx_builtin_setvar_helper(c, "CALLINGSUBADDR", e->ring.callingsubaddr);
							}
							if(e->ring.ani2 >= 0) {
								snprintf(ani2str, 5, "%.2d", e->ring.ani2);
								pbx_builtin_setvar_helper(c, "ANI2", ani2str);
							}
							if (!ast_strlen_zero(e->ring.useruserinfo)) {
								pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
							}
							snprintf(calledtonstr, sizeof(calledtonstr)-1, "%d", e->ring.calledplan);
							pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
							if (e->ring.redirectingreason >= 0)
								pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
							
							ast_mutex_lock(&pri->lock);
							if (c && !ast_pthread_create(&threadid, &attr, ss_thread, c)) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Accepting overlap call from '%s' to '%s' on channel %d/%d, span %d\n",
										plancallingnum, !ast_strlen_zero(pri->pvts[chanpos]->exten) ? pri->pvts[chanpos]->exten : "<unspecified>", 
										pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n", 
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								if (c)
									ast_hangup(c);
								else {
									pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
									pri->pvts[chanpos]->call = NULL;
								}
							}
						} else  {
							ast_mutex_unlock(&pri->lock);
							/* Release PRI lock while we create the channel */
							c = zt_new(pri->pvts[chanpos], AST_STATE_RING, 1, SUB_REAL, law, e->ring.ctype);
							ast_mutex_lock(&pri->lock);
							if (c) {
								char calledtonstr[10];
								if(e->ring.ani2 >= 0) {
									snprintf(ani2str, 5, "%d", e->ring.ani2);
									pbx_builtin_setvar_helper(c, "ANI2", ani2str);
								}
								if (!ast_strlen_zero(e->ring.useruserinfo)) {
									pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
								}
								if (e->ring.redirectingreason >= 0)
									pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
							
								snprintf(calledtonstr, sizeof(calledtonstr)-1, "%d", e->ring.calledplan);
								pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Accepting call from '%s' to '%s' on channel %d/%d, span %d\n",
										plancallingnum, pri->pvts[chanpos]->exten, 
											pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								zt_enable_ec(pri->pvts[chanpos]);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n", 
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
								pri->pvts[chanpos]->call = NULL;
							}
						}
					} else {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Extension '%s' in context '%s' from '%s' does not exist.  Rejecting call on channel %d/%d, span %d\n",
								pri->pvts[chanpos]->exten, pri->pvts[chanpos]->context, pri->pvts[chanpos]->cid_num, pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->exten[0] = '\0';
					}
					if (crv)
						ast_mutex_unlock(&crv->lock);
					ast_mutex_unlock(&pri->pvts[chanpos]->lock);
				} else 
					pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
				break;
			case PRI_EVENT_RINGING:
				chanpos = pri_find_principle(pri, e->ringing.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ringing requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->ringing.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Ringing requested on channel %d/%d not in use on span %d\n", 
							PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
						chanpos = -1;
					} else {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						if (ast_strlen_zero(pri->pvts[chanpos]->dop.dialstr)) {
							zt_enable_ec(pri->pvts[chanpos]);
							pri->pvts[chanpos]->subs[SUB_REAL].needringing =1;
							pri->pvts[chanpos]->proceeding=2;
						} else
							ast_log(LOG_DEBUG, "Deferring ringing notification because of extra digits to dial...\n");
#ifdef PRI_PROGRESS_MASK
						if (e->ringing.progressmask & PRI_PROG_INBAND_AVAILABLE) {
#else
						if (e->ringing.progress == 8) {
#endif
							/* Now we can do call progress detection */
							if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
								/* RINGING detection isn't required because we got ALERTING signal */
								ast_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features & ~DSP_PROGRESS_RINGING);
								pri->pvts[chanpos]->dsp_features = 0;
							}
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_PROGRESS:
				/* Get chan value if e->e is not PRI_EVNT_RINGING */
				chanpos = pri_find_principle(pri, e->proceeding.channel);
				if (chanpos > -1) {
#ifdef PRI_PROGRESS_MASK
					if ((!pri->pvts[chanpos]->proceeding) || (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE)) {
#else
					if ((!pri->pvts[chanpos]->proceeding) || (e->proceeding.progress == 8)) {
#endif
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROGRESS, };

						if (e->proceeding.cause > -1) {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "PROGRESS with cause code %d received\n", e->proceeding.cause);

							/* Work around broken, out of spec USER_BUSY cause in a progress message */
							if (e->proceeding.cause == AST_CAUSE_USER_BUSY) {
								if (pri->pvts[chanpos]->owner) {
									if (option_verbose > 2)
										ast_verbose(VERBOSE_PREFIX_3 "PROGRESS with 'user busy' received, signaling AST_CONTROL_BUSY instead of AST_CONTROL_PROGRESS\n");

									pri->pvts[chanpos]->owner->hangupcause = e->proceeding.cause;
									f.subclass = AST_CONTROL_BUSY;
								}
							}
						}
						
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						ast_log(LOG_DEBUG, "Queuing frame from PRI_EVENT_PROGRESS on channel %d/%d span %d\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						zap_queue_frame(pri->pvts[chanpos], &f, pri);
#ifdef PRI_PROGRESS_MASK
						if (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE) {
#else
						if (e->proceeding.progress == 8) {
#endif
							/* Now we can do call progress detection */
							if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
								ast_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features);
								pri->pvts[chanpos]->dsp_features = 0;
							}
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_PROCEEDING:
				chanpos = pri_find_principle(pri, e->proceeding.channel);
				if (chanpos > -1) {
					if (!pri->pvts[chanpos]->proceeding) {
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROCEEDING, };
						
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						ast_log(LOG_DEBUG, "Queuing frame from PRI_EVENT_PROCEEDING on channel %d/%d span %d\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						zap_queue_frame(pri->pvts[chanpos], &f, pri);
#ifdef PRI_PROGRESS_MASK
						if (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE) {
#else
						if (e->proceeding.progress == 8) {
#endif
							/* Now we can do call progress detection */
							if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
								ast_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features);
								pri->pvts[chanpos]->dsp_features = 0;
							}
							/* Bring voice path up */
							f.subclass = AST_CONTROL_PROGRESS;
							zap_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_FACNAME:
				chanpos = pri_find_principle(pri, e->facname.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Facility Name requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->facname.channel), PRI_CHANNEL(e->facname.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->facname.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Facility Name requested on channel %d/%d not in use on span %d\n", 
							PRI_SPAN(e->facname.channel), PRI_CHANNEL(e->facname.channel), pri->span);
						chanpos = -1;
					} else {
						/* Re-use *69 field for PRI */
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						ast_copy_string(pri->pvts[chanpos]->lastcid_num, e->facname.callingnum, sizeof(pri->pvts[chanpos]->lastcid_num));
						ast_copy_string(pri->pvts[chanpos]->lastcid_name, e->facname.callingname, sizeof(pri->pvts[chanpos]->lastcid_name));
						pri->pvts[chanpos]->subs[SUB_REAL].needcallerid =1;
						zt_enable_ec(pri->pvts[chanpos]);
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;				
			case PRI_EVENT_ANSWER:
				chanpos = pri_find_principle(pri, e->answer.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Answer on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->answer.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Answer requested on channel %d/%d not in use on span %d\n", 
							PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
						chanpos = -1;
					} else {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						/* Now we can do call progress detection */

						/* We changed this so it turns on the DSP no matter what... progress or no progress.
						 * By this time, we need DTMF detection and other features that were previously disabled
						 * -- Matt F */
						if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
							ast_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features);
							pri->pvts[chanpos]->dsp_features = 0;
						}
						if (pri->pvts[chanpos]->realcall && (pri->pvts[chanpos]->realcall->sig == SIG_FXSKS)) {
							ast_log(LOG_DEBUG, "Starting up GR-303 trunk now that we got CONNECT...\n");
							x = ZT_START;
							res = ioctl(pri->pvts[chanpos]->subs[SUB_REAL].zfd, ZT_HOOK, &x);
							if (res < 0) {
								if (errno != EINPROGRESS) {
									ast_log(LOG_WARNING, "Unable to start channel: %s\n", strerror(errno));
								}
							}
						} else if (!ast_strlen_zero(pri->pvts[chanpos]->dop.dialstr)) {
							pri->pvts[chanpos]->dialing = 1;
							/* Send any "w" waited stuff */
							res = ioctl(pri->pvts[chanpos]->subs[SUB_REAL].zfd, ZT_DIAL, &pri->pvts[chanpos]->dop);
							if (res < 0) {
								ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", pri->pvts[chanpos]->channel);
								pri->pvts[chanpos]->dop.dialstr[0] = '\0';
							} else 
								ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", pri->pvts[chanpos]->dop.dialstr);
							pri->pvts[chanpos]->dop.dialstr[0] = '\0';
						} else if (pri->pvts[chanpos]->confirmanswer) {
							ast_log(LOG_DEBUG, "Waiting on answer confirmation on channel %d!\n", pri->pvts[chanpos]->channel);
						} else {
							pri->pvts[chanpos]->subs[SUB_REAL].needanswer =1;
							/* Enable echo cancellation if it's not on already */
							zt_enable_ec(pri->pvts[chanpos]);
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;				
			case PRI_EVENT_HANGUP:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						if (!pri->pvts[chanpos]->alreadyhungup) {
							/* we're calling here zt_hangup so once we get there we need to clear p->call after calling pri_hangup */
							pri->pvts[chanpos]->alreadyhungup = 1;
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
							else if (pri->pvts[chanpos]->owner) {
								/* Queue a BUSY instead of a hangup if our cause is appropriate */
								pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
								switch(e->hangup.cause) {
								case PRI_CAUSE_USER_BUSY:
									pri->pvts[chanpos]->subs[SUB_REAL].needbusy =1;
									break;
								case PRI_CAUSE_CALL_REJECTED:
								case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
								case PRI_CAUSE_SWITCH_CONGESTION:
								case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
									pri->pvts[chanpos]->subs[SUB_REAL].needcongestion =1;
									break;
								default:
									pri->pvts[chanpos]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
								}
							}
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d got hangup\n", 
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Forcing restart of channel %d/%d on span %d since channel reported in use\n", 
									PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
						}
						if (e->hangup.aoc_units > -1)
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					} else {
						ast_log(LOG_WARNING, "Hangup on bad channel %d/%d on span %d\n", 
							PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				} 
				break;
#ifndef PRI_EVENT_HANGUP_REQ
#error please update libpri
#endif
			case PRI_EVENT_HANGUP_REQ:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup REQ requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						if (pri->pvts[chanpos]->realcall) 
							pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
						else if (pri->pvts[chanpos]->owner) {
							pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
							switch(e->hangup.cause) {
							case PRI_CAUSE_USER_BUSY:
								pri->pvts[chanpos]->subs[SUB_REAL].needbusy =1;
								break;
							case PRI_CAUSE_CALL_REJECTED:
							case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
							case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
							case PRI_CAUSE_SWITCH_CONGESTION:
							case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
							case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
								pri->pvts[chanpos]->subs[SUB_REAL].needcongestion =1;
								break;
							default:
								pri->pvts[chanpos]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							}
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d got hangup request\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							if (e->hangup.aoc_units > -1)
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
										pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Forcing restart of channel %d/%d span %d since channel reported in use\n", 
									PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					} else {
						ast_log(LOG_WARNING, "Hangup REQ on bad channel %d/%d on span %d\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				} 
				break;
			case PRI_EVENT_HANGUP_ACK:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup ACK requested on unconfigured channel number %d/%d span %d\n", 
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->resetting = 0;
						if (pri->pvts[chanpos]->owner) {
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d got hangup ACK\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
						}
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_CONFIG_ERR:
				ast_log(LOG_WARNING, "PRI Error: %s\n", e->err.err);
				break;
			case PRI_EVENT_RESTART_ACK:
				chanpos = pri_find_principle(pri, e->restartack.channel);
				if (chanpos < 0) {
					/* Sometime switches (e.g. I421 / British Telecom) don't give us the
					   channel number, so we have to figure it out...  This must be why
					   everybody resets exactly a channel at a time. */
					for (x=0;x<pri->numchans;x++) {
						if (pri->pvts[x] && pri->pvts[x]->resetting) {
							chanpos = x;
							ast_mutex_lock(&pri->pvts[chanpos]->lock);
							ast_log(LOG_DEBUG, "Assuming restart ack is really for channel %d/%d span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
							else if (pri->pvts[chanpos]->owner) {
								ast_log(LOG_WARNING, "Got restart ack on channel %d/%d with owner on span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
								pri->pvts[chanpos]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							}
							pri->pvts[chanpos]->resetting = 0;
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
							ast_mutex_unlock(&pri->pvts[chanpos]->lock);
							if (pri->resetting)
								pri_check_restart(pri);
							break;
						}
					}
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Restart ACK requested on strange channel %d/%d span %d\n", 
							PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
					}
					chanpos = -1;
				}
				if (chanpos > -1) {
					if (pri->pvts[chanpos]) {
						ast_mutex_lock(&pri->pvts[chanpos]->lock);
						if (pri->pvts[chanpos]->realcall) 
							pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
						else if (pri->pvts[chanpos]->owner) {
							ast_log(LOG_WARNING, "Got restart ack on channel %d/%d span %d with owner\n",
								PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
							pri->pvts[chanpos]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
						}
						pri->pvts[chanpos]->resetting = 0;
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
						ast_mutex_unlock(&pri->pvts[chanpos]->lock);
						if (pri->resetting)
							pri_check_restart(pri);
					}
				}
				break;
			case PRI_EVENT_SETUP_ACK:
				chanpos = pri_find_principle(pri, e->setup_ack.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received SETUP_ACKNOWLEDGE on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->setup_ack.channel), PRI_CHANNEL(e->setup_ack.channel), pri->span);
				} else {
					ast_mutex_lock(&pri->pvts[chanpos]->lock);
					pri->pvts[chanpos]->setup_ack = 1;
					/* Send any queued digits */
					for (x=0;x<strlen(pri->pvts[chanpos]->dialdest);x++) {
						ast_log(LOG_DEBUG, "Sending pending digit '%c'\n", pri->pvts[chanpos]->dialdest[x]);
						pri_information(pri->pri, pri->pvts[chanpos]->call, 
							pri->pvts[chanpos]->dialdest[x]);
					}
					ast_mutex_unlock(&pri->pvts[chanpos]->lock);
				}
				break;
			case PRI_EVENT_NOTIFY:
				chanpos = pri_find_principle(pri, e->notify.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received NOTIFY on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->notify.channel), PRI_CHANNEL(e->notify.channel), pri->span);
				} else {
					struct ast_frame f = { AST_FRAME_CONTROL, };
					ast_mutex_lock(&pri->pvts[chanpos]->lock);
					switch(e->notify.info) {
					case PRI_NOTIFY_REMOTE_HOLD:
						f.subclass = AST_CONTROL_HOLD;
						zap_queue_frame(pri->pvts[chanpos], &f, pri);
						break;
					case PRI_NOTIFY_REMOTE_RETRIEVAL:
						f.subclass = AST_CONTROL_UNHOLD;
						zap_queue_frame(pri->pvts[chanpos], &f, pri);
						break;
					}
					ast_mutex_unlock(&pri->pvts[chanpos]->lock);
				}
				break;
			default:
				ast_log(LOG_DEBUG, "Event: %d\n", e->e);
			}
		}	
		ast_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

static int start_pri(struct zt_pri *pri)
{
	int res, x;
	ZT_PARAMS p;
	ZT_BUFFERINFO bi;
	struct zt_spaninfo si;
	int i;
	
	for (i=0;i<NUM_DCHANS;i++) {
		if (!pri->dchannels[i])
			break;
		pri->fds[i] = open("/dev/zap/channel", O_RDWR, 0600);
		x = pri->dchannels[i];
		if ((pri->fds[i] < 0) || (ioctl(pri->fds[i],ZT_SPECIFY,&x) == -1)) {
			ast_log(LOG_ERROR, "Unable to open D-channel %d (%s)\n", x, strerror(errno));
			return -1;
		}
		res = ioctl(pri->fds[i], ZT_GET_PARAMS, &p);
		if (res) {
			zt_close(pri->fds[i]);
			pri->fds[i] = -1;
			ast_log(LOG_ERROR, "Unable to get parameters for D-channel %d (%s)\n", x, strerror(errno));
			return -1;
		}
		if (p.sigtype != ZT_SIG_HDLCFCS) {
			zt_close(pri->fds[i]);
			pri->fds[i] = -1;
			ast_log(LOG_ERROR, "D-channel %d is not in HDLC/FCS mode.  See /etc/zaptel.conf\n", x);
			return -1;
		}
		memset(&si, 0, sizeof(si));
		res = ioctl(pri->fds[i], ZT_SPANSTAT, &si);
		if (res) {
			zt_close(pri->fds[i]);
			pri->fds[i] = -1;
			ast_log(LOG_ERROR, "Unable to get span state for D-channel %d (%s)\n", x, strerror(errno));
		}
		if (!si.alarms)
			pri->dchanavail[i] |= DCHAN_NOTINALARM;
		else
			pri->dchanavail[i] &= ~DCHAN_NOTINALARM;
		bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.numbufs = 32;
		bi.bufsize = 1024;
		if (ioctl(pri->fds[i], ZT_SET_BUFINFO, &bi)) {
			ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d\n", x);
			zt_close(pri->fds[i]);
			pri->fds[i] = -1;
			return -1;
		}
		pri->dchans[i] = pri_new(pri->fds[i], pri->nodetype, pri->switchtype);
		/* Force overlap dial if we're doing GR-303! */
		if (pri->switchtype == PRI_SWITCH_GR303_TMC)
			pri->overlapdial = 1;
		pri_set_overlapdial(pri->dchans[i],pri->overlapdial);
		/* Enslave to master if appropriate */
		if (i)
			pri_enslave(pri->dchans[0], pri->dchans[i]);
		if (!pri->dchans[i]) {
			zt_close(pri->fds[i]);
			pri->fds[i] = -1;
			ast_log(LOG_ERROR, "Unable to create PRI structure\n");
			return -1;
		}
		pri_set_debug(pri->dchans[i], DEFAULT_PRI_DEBUG);
		pri_set_nsf(pri->dchans[i], pri->nsf);
#ifdef PRI_GETSET_TIMERS
		for (x = 0; x < PRI_MAX_TIMERS; x++) {
			if (pritimers[x] != 0)
				pri_set_timer(pri->dchans[i], x, pritimers[x]);
		}
#endif
	}
	/* Assume primary is the one we use */
	pri->pri = pri->dchans[0];
	pri->resetpos = -1;
	if (ast_pthread_create(&pri->master, NULL, pri_dchannel, pri)) {
		for (i=0;i<NUM_DCHANS;i++) {
			if (!pri->dchannels[i])
				break;
			zt_close(pri->fds[i]);
			pri->fds[i] = -1;
		}
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static char *complete_span_helper(char *line, char *word, int pos, int state, int rpos)
{
	int span=1;
	char tmp[50];
	if (pos != rpos)
		return 0;
	while(span <= NUM_SPANS) {
		if (span > state && pris[span-1].pri)
			break;
		span++;
	}
	if (span <= NUM_SPANS) {
		snprintf(tmp, sizeof(tmp), "%d", span);
		return strdup(tmp);
	} else
		return NULL;
}

static char *complete_span_4(char *line, char *word, int pos, int state)
{
	return complete_span_helper(line,word,pos,state,3);
}

static char *complete_span_5(char *line, char *word, int pos, int state)
{
	return complete_span_helper(line,word,pos,state,4);
}

static int handle_pri_set_debug_file(int fd, int argc, char **argv)
{
	int myfd;

	if (!strncasecmp(argv[1], "set", 3)) {
		if (argc < 5) 
			return RESULT_SHOWUSAGE;

		if (!argv[4] || ast_strlen_zero(argv[4]))
			return RESULT_SHOWUSAGE;

		myfd = open(argv[4], O_CREAT|O_WRONLY);
		if (myfd < 0) {
			ast_cli(fd, "Unable to open '%s' for writing\n", argv[4]);
			return RESULT_SUCCESS;
		}

		ast_mutex_lock(&pridebugfdlock);

		if (pridebugfd >= 0)
			close(pridebugfd);

		pridebugfd = myfd;
		ast_copy_string(pridebugfilename,argv[4],sizeof(pridebugfilename));
		
		ast_mutex_unlock(&pridebugfdlock);

		ast_cli(fd, "PRI debug output will be sent to '%s'\n", argv[4]);
	} else {
		/* Assume it is unset */
		ast_mutex_lock(&pridebugfdlock);
		close(pridebugfd);
		pridebugfd = -1;
		ast_cli(fd, "PRI debug output to file disabled\n");
		ast_mutex_unlock(&pridebugfdlock);
	}

	return RESULT_SUCCESS;
}

static int handle_pri_debug(int fd, int argc, char *argv[])
{
	int span;
	int x;
	if (argc < 4) {
		return RESULT_SHOWUSAGE;
	}
	span = atoi(argv[3]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(fd, "Invalid span %s.  Should be a number %d to %d\n", argv[3], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		ast_cli(fd, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for (x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchans[x])
			pri_set_debug(pris[span-1].dchans[x], PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE);
	}
	ast_cli(fd, "Enabled debugging on span %d\n", span);
	return RESULT_SUCCESS;
}



static int handle_pri_no_debug(int fd, int argc, char *argv[])
{
	int span;
	int x;
	if (argc < 5)
		return RESULT_SHOWUSAGE;
	span = atoi(argv[4]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(fd, "Invalid span %s.  Should be a number %d to %d\n", argv[4], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		ast_cli(fd, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for (x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchans[x])
			pri_set_debug(pris[span-1].dchans[x], 0);
	}
	ast_cli(fd, "Disabled debugging on span %d\n", span);
	return RESULT_SUCCESS;
}

static int handle_pri_really_debug(int fd, int argc, char *argv[])
{
	int span;
	int x;
	if (argc < 5)
		return RESULT_SHOWUSAGE;
	span = atoi(argv[4]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(fd, "Invalid span %s.  Should be a number %d to %d\n", argv[4], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		ast_cli(fd, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for (x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchans[x])
			pri_set_debug(pris[span-1].dchans[x], (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE));
	}
	ast_cli(fd, "Enabled EXTENSIVE debugging on span %d\n", span);
	return RESULT_SUCCESS;
}

static void build_status(char *s, size_t len, int status, int active)
{
	if (!s || len < 1) {
		return;
	}
	s[0] = '\0';
	if (status & DCHAN_PROVISIONED)
		strncat(s, "Provisioned, ", len - strlen(s) - 1);
	if (!(status & DCHAN_NOTINALARM))
		strncat(s, "In Alarm, ", len - strlen(s) - 1);
	if (status & DCHAN_UP)
		strncat(s, "Up", len - strlen(s) - 1);
	else
		strncat(s, "Down", len - strlen(s) - 1);
	if (active)
		strncat(s, ", Active", len - strlen(s) - 1);
	else
		strncat(s, ", Standby", len - strlen(s) - 1);
	s[len - 1] = '\0';
}

static int handle_pri_show_span(int fd, int argc, char *argv[])
{
	int span;
	int x;
	char status[256];
	if (argc < 4)
		return RESULT_SHOWUSAGE;
	span = atoi(argv[3]);
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(fd, "Invalid span %s.  Should be a number %d to %d\n", argv[4], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		ast_cli(fd, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for(x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchannels[x]) {
#ifdef PRI_DUMP_INFO_STR
			char *info_str = NULL;
#endif
			ast_cli(fd, "%s D-channel: %d\n", pri_order(x), pris[span-1].dchannels[x]);
			build_status(status, sizeof(status), pris[span-1].dchanavail[x], pris[span-1].dchans[x] == pris[span-1].pri);
			ast_cli(fd, "Status: %s\n", status);
#ifdef PRI_DUMP_INFO_STR
			info_str = pri_dump_info_str(pris[span-1].pri);
			if (info_str) {
				ast_cli(fd, "%s", info_str);
				free(info_str);
			}
#else
			pri_dump_info(pris[span-1].pri);
#endif
			ast_cli(fd, "\n");
		}
	}
	return RESULT_SUCCESS;
}

static int handle_pri_show_debug(int fd, int argc, char *argv[])
{
	int x;
	int span;
	int count=0;
	int debug=0;

	for(span=0;span<NUM_SPANS;span++) {
	        if (pris[span].pri) {
			for(x=0;x<NUM_DCHANS;x++) {
				debug=0;
	        		if (pris[span].dchans[x]) {
	        			debug = pri_get_debug(pris[span].dchans[x]);
					ast_cli(fd, "Span %d: Debug: %s\tIntense: %s\n", span+1, (debug&PRI_DEBUG_Q931_STATE)? "Yes" : "No" ,(debug&PRI_DEBUG_Q921_RAW)? "Yes" : "No" );
					count++;
				}
			}
		}

	}
	ast_mutex_lock(&pridebugfdlock);
	if (pridebugfd >= 0) 
		ast_cli(fd, "Logging PRI debug to file %s\n", pridebugfilename);
	ast_mutex_unlock(&pridebugfdlock);
	    
	if (!count) 
		ast_cli(fd, "No debug set or no PRI running\n");
	return RESULT_SUCCESS;
}

static char pri_debug_help[] = 
	"Usage: pri debug span <span>\n"
	"       Enables debugging on a given PRI span\n";
	
static char pri_no_debug_help[] = 
	"Usage: pri no debug span <span>\n"
	"       Disables debugging on a given PRI span\n";

static char pri_really_debug_help[] = 
	"Usage: pri intensive debug span <span>\n"
	"       Enables debugging down to the Q.921 level\n";

static char pri_show_span_help[] = 
	"Usage: pri show span <span>\n"
	"       Displays PRI Information\n";

static struct ast_cli_entry zap_pri_cli[] = {
	{ { "pri", "debug", "span", NULL }, handle_pri_debug,
	  "Enables PRI debugging on a span", pri_debug_help, complete_span_4 },
	{ { "pri", "no", "debug", "span", NULL }, handle_pri_no_debug,
	  "Disables PRI debugging on a span", pri_no_debug_help, complete_span_5 },
	{ { "pri", "intense", "debug", "span", NULL }, handle_pri_really_debug,
	  "Enables REALLY INTENSE PRI debugging", pri_really_debug_help, complete_span_5 },
	{ { "pri", "show", "span", NULL }, handle_pri_show_span,
	  "Displays PRI Information", pri_show_span_help, complete_span_4 },
	{ { "pri", "show", "debug", NULL }, handle_pri_show_debug,
	  "Displays current PRI debug settings" },
	{ { "pri", "set", "debug", "file", NULL }, handle_pri_set_debug_file,
	  "Sends PRI debug output to the specified file" },
	{ { "pri", "unset", "debug", "file", NULL }, handle_pri_set_debug_file,
	  "Ends PRI debug output to file" },
};

#endif /* ZAPATA_PRI */


#ifdef ZAPATA_R2
static int handle_r2_no_debug(int fd, int argc, char *argv[])
{
	int chan;
	struct zt_pvt *tmp = NULL;;
	if (argc < 5)
		return RESULT_SHOWUSAGE;
	chan = atoi(argv[4]);
	if ((chan < 1) || (chan > NUM_SPANS)) {
		ast_cli(fd, "Invalid channel %s.  Should be a number greater than 0\n", argv[4]);
		return RESULT_SUCCESS;
	}
	tmp = iflist;
	while(tmp) {
		if (tmp->channel == chan) {
			if (tmp->r2) {
				mfcr2_set_debug(tmp->r2, 0);
				ast_cli(fd, "Disabled R2 debugging on channel %d\n", chan);
				return RESULT_SUCCESS;
			}
			break;
		}
		tmp = tmp->next;
	}
	if (tmp) 
		ast_cli(fd, "No R2 running on channel %d\n", chan);
	else
		ast_cli(fd, "No such zap channel %d\n", chan);
	return RESULT_SUCCESS;
}

static int handle_r2_debug(int fd, int argc, char *argv[])
{
	int chan;
	struct zt_pvt *tmp = NULL;;
	if (argc < 4) {
		return RESULT_SHOWUSAGE;
	}
	chan = atoi(argv[3]);
	if ((chan < 1) || (chan > NUM_SPANS)) {
		ast_cli(fd, "Invalid channel %s.  Should be a number greater than 0\n", argv[3]);
		return RESULT_SUCCESS;
	}
	tmp = iflist;
	while(tmp) {
		if (tmp->channel == chan) {
			if (tmp->r2) {
				mfcr2_set_debug(tmp->r2, 0xFFFFFFFF);
				ast_cli(fd, "Enabled R2 debugging on channel %d\n", chan);
				return RESULT_SUCCESS;
			}
			break;
		}
		tmp = tmp->next;
	}
	if (tmp) 
		ast_cli(fd, "No R2 running on channel %d\n", chan);
	else
		ast_cli(fd, "No such zap channel %d\n", chan);
	return RESULT_SUCCESS;
}
static char r2_debug_help[] = 
	"Usage: r2 debug channel <channel>\n"
	"       Enables R2 protocol level debugging on a given channel\n";
	
static char r2_no_debug_help[] = 
	"Usage: r2 no debug channel <channel>\n"
	"       Enables R2 protocol level debugging on a given channel\n";

static struct ast_cli_entry zap_r2_cli[] = {
	{ { "r2", "debug", "channel", NULL }, handle_r2_debug,
	  "Enables R2 debugging on a channel", r2_debug_help },
	{ { "r2", "no", "debug", "channel", NULL }, handle_r2_no_debug,
	  "Disables R2 debugging on a channel", r2_no_debug_help },
};

#endif

static int zap_destroy_channel(int fd, int argc, char **argv)
{
	int channel = 0;
	struct zt_pvt *tmp = NULL;
	struct zt_pvt *prev = NULL;
	
	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}
	channel = atoi(argv[3]);

	tmp = iflist;
	while (tmp) {
		if (tmp->channel == channel) {
			destroy_channel(prev, tmp, 1);
			return RESULT_SUCCESS;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	return RESULT_FAILURE;
}

static int zap_show_channels(int fd, int argc, char **argv)
{
#define FORMAT "%7s %-10.10s %-15.15s %-10.10s %-20.20s\n"
#define FORMAT2 "%7s %-10.10s %-15.15s %-10.10s %-20.20s\n"
	struct zt_pvt *tmp = NULL;
	char tmps[20] = "";
	ast_mutex_t *lock;
	struct zt_pvt *start;
#ifdef ZAPATA_PRI
	int trunkgroup;
	struct zt_pri *pri=NULL;
	int x;
#endif

	lock = &iflock;
	start = iflist;

#ifdef ZAPATA_PRI
	if (argc == 4) {
		if ((trunkgroup = atoi(argv[3])) < 1)
			return RESULT_SHOWUSAGE;
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].trunkgroup == trunkgroup) {
				pri = pris + x;
				break;
			}
		}
		if (pri) {
			start = pri->crvs;
			lock = &pri->lock;
		} else {
			ast_cli(fd, "No such trunk group %d\n", trunkgroup);
			return RESULT_FAILURE;
		}
	} else
#endif
	if (argc != 3)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(lock);
#ifdef ZAPATA_PRI
	ast_cli(fd, FORMAT2, pri ? "CRV" : "Chan", "Extension", "Context", "Language", "MusicOnHold");
#else
	ast_cli(fd, FORMAT2, "Chan", "Extension", "Context", "Language", "MusicOnHold");
#endif	
	
	tmp = start;
	while (tmp) {
		if (tmp->channel > 0) {
			snprintf(tmps, sizeof(tmps), "%d", tmp->channel);
		} else
			ast_copy_string(tmps, "pseudo", sizeof(tmps));
		ast_cli(fd, FORMAT, tmps, tmp->exten, tmp->context, tmp->language, tmp->musicclass);
		tmp = tmp->next;
	}
	ast_mutex_unlock(lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int zap_show_channel(int fd, int argc, char **argv)
{
	int channel;
	struct zt_pvt *tmp = NULL;
	ZT_CONFINFO ci;
	ZT_PARAMS ps;
	int x;
	ast_mutex_t *lock;
	struct zt_pvt *start;
#ifdef ZAPATA_PRI
	char *c;
	int trunkgroup;
	struct zt_pri *pri=NULL;
#endif

	lock = &iflock;
	start = iflist;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
#ifdef ZAPATA_PRI
	if ((c = strchr(argv[3], ':'))) {
		if (sscanf(argv[3], "%d:%d", &trunkgroup, &channel) != 2)
			return RESULT_SHOWUSAGE;
		if ((trunkgroup < 1) || (channel < 1))
			return RESULT_SHOWUSAGE;
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].trunkgroup == trunkgroup) {
				pri = pris + x;
				break;
			}
		}
		if (pri) {
			start = pri->crvs;
			lock = &pri->lock;
		} else {
			ast_cli(fd, "No such trunk group %d\n", trunkgroup);
			return RESULT_FAILURE;
		}
	} else
#endif
		channel = atoi(argv[3]);

	ast_mutex_lock(lock);
	tmp = start;
	while (tmp) {
		if (tmp->channel == channel) {
#ifdef ZAPATA_PRI
			if (pri) 
				ast_cli(fd, "Trunk/CRV: %d/%d\n", trunkgroup, tmp->channel);
			else
#endif			
			ast_cli(fd, "Channel: %d\n", tmp->channel);
			ast_cli(fd, "File Descriptor: %d\n", tmp->subs[SUB_REAL].zfd);
			ast_cli(fd, "Span: %d\n", tmp->span);
			ast_cli(fd, "Extension: %s\n", tmp->exten);
			ast_cli(fd, "Dialing: %s\n", tmp->dialing ? "yes" : "no");
			ast_cli(fd, "Context: %s\n", tmp->context);
			ast_cli(fd, "Caller ID: %s\n", tmp->cid_num);
			ast_cli(fd, "Calling TON: %d\n", tmp->cid_ton);
			ast_cli(fd, "Caller ID name: %s\n", tmp->cid_name);
			ast_cli(fd, "Destroy: %d\n", tmp->destroy);
			ast_cli(fd, "InAlarm: %d\n", tmp->inalarm);
			ast_cli(fd, "Signalling Type: %s\n", sig2str(tmp->sig));
			ast_cli(fd, "Radio: %d\n", tmp->radio);
			ast_cli(fd, "Owner: %s\n", tmp->owner ? tmp->owner->name : "<None>");
			ast_cli(fd, "Real: %s%s%s\n", tmp->subs[SUB_REAL].owner ? tmp->subs[SUB_REAL].owner->name : "<None>", tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : "", tmp->subs[SUB_REAL].linear ? " (Linear)" : "");
			ast_cli(fd, "Callwait: %s%s%s\n", tmp->subs[SUB_CALLWAIT].owner ? tmp->subs[SUB_CALLWAIT].owner->name : "<None>", tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : "", tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "");
			ast_cli(fd, "Threeway: %s%s%s\n", tmp->subs[SUB_THREEWAY].owner ? tmp->subs[SUB_THREEWAY].owner->name : "<None>", tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : "", tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "");
			ast_cli(fd, "Confno: %d\n", tmp->confno);
			ast_cli(fd, "Propagated Conference: %d\n", tmp->propconfno);
			ast_cli(fd, "Real in conference: %d\n", tmp->inconference);
			ast_cli(fd, "DSP: %s\n", tmp->dsp ? "yes" : "no");
			ast_cli(fd, "Relax DTMF: %s\n", tmp->dtmfrelax ? "yes" : "no");
			ast_cli(fd, "Dialing/CallwaitCAS: %d/%d\n", tmp->dialing, tmp->callwaitcas);
			ast_cli(fd, "Default law: %s\n", tmp->law == ZT_LAW_MULAW ? "ulaw" : tmp->law == ZT_LAW_ALAW ? "alaw" : "unknown");
			ast_cli(fd, "Fax Handled: %s\n", tmp->faxhandled ? "yes" : "no");
			ast_cli(fd, "Pulse phone: %s\n", tmp->pulsedial ? "yes" : "no");
			ast_cli(fd, "Echo Cancellation: %d taps%s, currently %s\n", tmp->echocancel, tmp->echocanbridged ? "" : " unless TDM bridged", tmp->echocanon ? "ON" : "OFF");
			if (tmp->master)
				ast_cli(fd, "Master Channel: %d\n", tmp->master->channel);
			for (x=0;x<MAX_SLAVES;x++) {
				if (tmp->slaves[x])
					ast_cli(fd, "Slave Channel: %d\n", tmp->slaves[x]->channel);
			}
#ifdef ZAPATA_PRI
			if (tmp->pri) {
				ast_cli(fd, "PRI Flags: ");
				if (tmp->resetting)
					ast_cli(fd, "Resetting ");
				if (tmp->call)
					ast_cli(fd, "Call ");
				if (tmp->bearer)
					ast_cli(fd, "Bearer ");
				ast_cli(fd, "\n");
				if (tmp->logicalspan) 
					ast_cli(fd, "PRI Logical Span: %d\n", tmp->logicalspan);
				else
					ast_cli(fd, "PRI Logical Span: Implicit\n");
			}
				
#endif
#ifdef ZAPATA_R2
			if (tmp->r2) {
				ast_cli(fd, "R2 Flags: ");
				if (tmp->r2blocked)
					ast_cli(fd, "Blocked ");
				if (tmp->hasr2call)
					ast_cli(fd, "Call ");
				ast_cli(fd, "\n");
			}
#endif
			memset(&ci, 0, sizeof(ci));
			ps.channo = tmp->channel;
			if (tmp->subs[SUB_REAL].zfd > -1) {
				if (!ioctl(tmp->subs[SUB_REAL].zfd, ZT_GETCONF, &ci)) {
					ast_cli(fd, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, ci.confmode);
				}
#ifdef ZT_GETCONFMUTE
				if (!ioctl(tmp->subs[SUB_REAL].zfd, ZT_GETCONFMUTE, &x)) {
					ast_cli(fd, "Actual Confmute: %s\n", x ? "Yes" : "No");
				}
#endif
				if (ioctl(tmp->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &ps) < 0) {
					ast_log(LOG_WARNING, "Failed to get parameters on channel %d\n", tmp->channel);
				} else {
					ast_cli(fd, "Hookstate (FXS only): %s\n", ps.rxisoffhook ? "Offhook" : "Onhook");
				}
			}
			ast_mutex_unlock(lock);
			return RESULT_SUCCESS;
		}
		tmp = tmp->next;
	}
	
	ast_cli(fd, "Unable to find given channel %d\n", channel);
	ast_mutex_unlock(lock);
	return RESULT_FAILURE;
}

static char zap_show_cadences_help[] =
"Usage: zap show cadences\n"
"       Shows all cadences currently defined\n";

static int handle_zap_show_cadences(int fd, int argc, char *argv[])
{
	int i, j;
	for (i=0;i<num_cadence;i++) {
		char output[1024];
		char tmp[16], tmp2[64];
		snprintf(tmp, sizeof(tmp), "r%d: ", i + 1);
		term_color(output, tmp, COLOR_GREEN, COLOR_BLACK, sizeof(output));

		for (j=0;j<16;j++) {
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
		ast_cli(fd,"%s\n",output);
	}
	return 0;
}

/* Based on irqmiss.c */
static int zap_show_status(int fd, int argc, char *argv[]) {
	#define FORMAT "%-40.40s %-10.10s %10d %10d %10d\n"
	#define FORMAT2 "%-40.40s %-10.10s %-10.10s %-10.10s %-10.10s\n"

	int span;
	int res;
	char alarms[50];

	int ctl;
	ZT_SPANINFO s;

	ctl = open("/dev/zap/ctl", O_RDWR);
	if (ctl < 0) {
		fprintf(stderr, "Unable to open /dev/zap/ctl: %s\n", strerror(errno));
		ast_cli(fd, "No Zaptel interface found.\n");
		return RESULT_FAILURE;
	}
	ast_cli(fd,FORMAT2, "Description", "Alarms","IRQ","bpviol","CRC4");

	for (span=1;span < ZT_MAX_SPANS;++span) {
		s.spanno = span;
		res = ioctl(ctl, ZT_SPANSTAT, &s);
		if (res) {
			continue;
		}
		alarms[0] = '\0';
		if (s.alarms > 0) {
			if (s.alarms & ZT_ALARM_BLUE)
				strcat(alarms,"BLU/");
			if (s.alarms & ZT_ALARM_YELLOW)
				strcat(alarms, "YEL/");
			if (s.alarms & ZT_ALARM_RED)
				strcat(alarms, "RED/");
			if (s.alarms & ZT_ALARM_LOOPBACK)
				strcat(alarms,"LB/");
			if (s.alarms & ZT_ALARM_RECOVER)
				strcat(alarms,"REC/");
			if (s.alarms & ZT_ALARM_NOTOPEN)
				strcat(alarms, "NOP/");
			if (!strlen(alarms))
				strcat(alarms, "UUU/");
			if (strlen(alarms)) {
				/* Strip trailing / */
				alarms[strlen(alarms)-1]='\0';
			}
		} else {
			if (s.numchans)
				strcpy(alarms, "OK");
			else
				strcpy(alarms, "UNCONFIGURED");
		}

		ast_cli(fd, FORMAT, s.desc, alarms, s.irqmisses, s.bpvcount, s.crc4count);
	}
	close(ctl);

	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char show_channels_usage[] =
	"Usage: zap show channels\n"
	"	Shows a list of available channels\n";

static char show_channel_usage[] =
	"Usage: zap show channel <chan num>\n"
	"	Detailed information about a given channel\n";

static char zap_show_status_usage[] =
	"Usage: zap show status\n"
	"       Shows a list of Zaptel cards with status\n";

static char destroy_channel_usage[] =
	"Usage: zap destroy channel <chan num>\n"
	"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";

static struct ast_cli_entry zap_cli[] = {
	{ { "zap", "show", "cadences", NULL }, handle_zap_show_cadences,
	  "List cadences", zap_show_cadences_help },
	{ {"zap", "show", "channels", NULL}, zap_show_channels,
	  "Show active zapata channels", show_channels_usage },
	{ {"zap", "show", "channel", NULL}, zap_show_channel,
	  "Show information on a channel", show_channel_usage },
	{ {"zap", "destroy", "channel", NULL}, zap_destroy_channel,
	  "Destroy a channel", destroy_channel_usage },
	{ {"zap", "show", "status", NULL}, zap_show_status,
	  "Show all Zaptel cards status", zap_show_status_usage },
};

#define TRANSFER	0
#define HANGUP		1

static int zap_fake_event(struct zt_pvt *p, int mode)
{
	if (p) {
		switch(mode) {
			case TRANSFER:
				p->fake_event = ZT_EVENT_WINKFLASH;
				break;
			case HANGUP:
				p->fake_event = ZT_EVENT_ONHOOK;
				break;
			default:
				ast_log(LOG_WARNING, "I don't know how to handle transfer event with this: %d on channel %s\n",mode, p->owner->name);	
		}
	}
	return 0;
}
static struct zt_pvt *find_channel(int channel)
{
	struct zt_pvt *p = iflist;
	while(p) {
		if (p->channel == channel) {
			break;
		}
		p = p->next;
	}
	return p;
}

static int action_zapdndon(struct mansession *s, struct message *m)
{
    struct zt_pvt *p = NULL;
    char *channel = astman_get_header(m, "ZapChannel");
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

static int action_zapdndoff(struct mansession *s, struct message *m)
{
    struct zt_pvt *p = NULL;
    char *channel = astman_get_header(m, "ZapChannel");
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

static int action_transfer(struct mansession *s, struct message *m)
{
	struct zt_pvt *p = NULL;
	char *channel = astman_get_header(m, "ZapChannel");
	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	zap_fake_event(p,TRANSFER);
	astman_send_ack(s, m, "ZapTransfer");
	return 0;
}

static int action_transferhangup(struct mansession *s, struct message *m)
{
	struct zt_pvt *p = NULL;
	char *channel = astman_get_header(m, "ZapChannel");
	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	p = find_channel(atoi(channel));
	if (!p) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	zap_fake_event(p,HANGUP);
	astman_send_ack(s, m, "ZapHangup");
	return 0;
}

static int action_zapdialoffhook(struct mansession *s, struct message *m)
{
	struct zt_pvt *p = NULL;
	char *channel = astman_get_header(m, "ZapChannel");
	char *number = astman_get_header(m, "Number");
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
	for (i=0; i<strlen(number); i++) {
		struct ast_frame f = { AST_FRAME_DTMF, number[i] };
		zap_queue_frame(p, &f, NULL); 
	}
	astman_send_ack(s, m, "ZapDialOffhook");
	return 0;
}

static int action_zapshowchannels(struct mansession *s, struct message *m)
{
	struct zt_pvt *tmp = NULL;
	char *id = astman_get_header(m, "ActionID");
	char idText[256] = "";

	astman_send_ack(s, m, "Zapata channel status will follow");
	if (id && !ast_strlen_zero(id))
		snprintf(idText, sizeof(idText) - 1, "ActionID: %s\r\n", id);

	ast_mutex_lock(&iflock);
	
	tmp = iflist;
	while (tmp) {
		if (tmp->channel > 0) {
			int alarm = get_alarms(tmp);
			ast_mutex_lock(&s->lock);		
			ast_cli(s->fd,
				"Event: ZapShowChannels\r\n"
				"Channel: %d\r\n"
				"Signalling: %s\r\n"
				"Context: %s\r\n"
				"DND: %s\r\n"
				"Alarm: %s\r\n"
				"%s"
				"\r\n",
				tmp->channel, sig2str(tmp->sig), tmp->context, 
				tmp->dnd ? "Enabled" : "Disabled",
				alarm2str(alarm), idText);
			ast_mutex_unlock(&s->lock);		
		} 

		tmp = tmp->next;
	}

	ast_mutex_unlock(&iflock);
	
	ast_mutex_lock(&s->lock);		
	ast_cli(s->fd, 
		"Event: ZapShowChannelsComplete\r\n"
		"%s"
		"\r\n", 
		idText);
	ast_mutex_unlock(&s->lock);		
	return 0;
}

static int __unload_module(void)
{
	int x = 0;
	struct zt_pvt *p, *pl;
#ifdef ZAPATA_PRI
	int i;
	for(i=0;i<NUM_SPANS;i++) {
		if (pris[i].master != AST_PTHREADT_NULL) 
			pthread_cancel(pris[i].master);
	}
	ast_cli_unregister_multiple(zap_pri_cli, sizeof(zap_pri_cli) / sizeof(zap_pri_cli[0]));
#endif
#ifdef ZAPATA_R2
	ast_cli_unregister_multiple(zap_r2_cli, sizeof(zap_r2_cli) / sizeof(zap_r2_cli[0]));
#endif
	ast_cli_unregister_multiple(zap_cli, sizeof(zap_cli) / sizeof(zap_cli[0]));
	ast_manager_unregister( "ZapDialOffhook" );
	ast_manager_unregister( "ZapHangup" );
	ast_manager_unregister( "ZapTransfer" );
	ast_manager_unregister( "ZapDNDoff" );
	ast_manager_unregister( "ZapDNDon" );
	ast_manager_unregister("ZapShowChannels");
	ast_channel_unregister(&zap_tech);
	if (!ast_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL)) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = AST_PTHREADT_STOP;
		ast_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!ast_mutex_lock(&iflock)) {
		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			/* Free any callerid */
			if (p->cidspill)
				free(p->cidspill);
			/* Close the zapata thingy */
			if (p->subs[SUB_REAL].zfd > -1)
				zt_close(p->subs[SUB_REAL].zfd);
			pl = p;
			p = p->next;
			x++;
			/* Free associated memory */
			if(pl)
				destroy_zt_pvt(&pl);
			ast_verbose(VERBOSE_PREFIX_3 "Unregistered channel %d\n", x);
		}
		iflist = NULL;
		ifcount = 0;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
#ifdef ZAPATA_PRI		
	for(i=0;i<NUM_SPANS;i++) {
		if (pris[i].master && (pris[i].master != AST_PTHREADT_NULL))
			pthread_join(pris[i].master, NULL);
		zt_close(pris[i].fds[i]);
	}
#endif
	return 0;
}

int unload_module()
{
#ifdef ZAPATA_PRI		
	int y;
	for (y=0;y<NUM_SPANS;y++)
		ast_mutex_destroy(&pris[y].lock);
#endif
	return __unload_module();
}
		
static int setup_zap(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct zt_pvt *tmp;
	char *chan;
	char *c;
	char *ringc;
	int start, finish,x;
	int y;
	int found_pseudo = 0;
	int cur_radio = 0;
#ifdef ZAPATA_PRI
	int spanno;
	int i;
	int logicalspan;
	int trunkgroup;
	int dchannels[NUM_DCHANS];
	struct zt_pri *pri;
#endif

	cfg = ast_config_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}
	

	if (ast_mutex_lock(&iflock)) {
		/* It's a little silly to lock it, but we mind as well just to be sure */
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return -1;
	}
#ifdef ZAPATA_PRI
	if (!reload) {
		/* Process trunkgroups first */
		v = ast_variable_browse(cfg, "trunkgroups");
		while(v) {
			if (!strcasecmp(v->name, "trunkgroup")) {
				trunkgroup = atoi(v->value);
				if (trunkgroup > 0) {
					if ((c = strchr(v->value, ','))) {
						i = 0;
						memset(dchannels, 0, sizeof(dchannels));
						while(c && (i < NUM_DCHANS)) {
							dchannels[i] = atoi(c + 1);
							if (dchannels[i] < 0) {
								ast_log(LOG_WARNING, "D-channel for trunk group %d must be a postiive number at line %d of zapata.conf\n", trunkgroup, v->lineno);
							} else
								i++;
							c = strchr(c + 1, ',');
						}
						if (i) {
							if (pri_create_trunkgroup(trunkgroup, dchannels)) {
								ast_log(LOG_WARNING, "Unable to create trunk group %d with Primary D-channel %d at line %d of zapata.conf\n", trunkgroup, dchannels[0], v->lineno);
							} else if (option_verbose > 1)
								ast_verbose(VERBOSE_PREFIX_2 "Created trunk group %d with Primary D-channel %d and %d backup%s\n", trunkgroup, dchannels[0], i - 1, (i == 1) ? "" : "s");
						} else
							ast_log(LOG_WARNING, "Trunk group %d lacks any valid D-channels at line %d of zapata.conf\n", trunkgroup, v->lineno);
					} else
						ast_log(LOG_WARNING, "Trunk group %d lacks a primary D-channel at line %d of zapata.conf\n", trunkgroup, v->lineno);
				} else
					ast_log(LOG_WARNING, "Trunk group identifier must be a positive integer at line %d of zapata.conf\n", v->lineno);
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
								} else if (option_verbose > 1) 
									ast_verbose(VERBOSE_PREFIX_2 "Mapped span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
							} else
								ast_log(LOG_WARNING, "Logical span must be a postive number, or '0' (for unspecified) at line %d of zapata.conf\n", v->lineno);
						} else
							ast_log(LOG_WARNING, "Trunk group must be a postive number at line %d of zapata.conf\n", v->lineno);
					} else
						ast_log(LOG_WARNING, "Missing trunk group for span map at line %d of zapata.conf\n", v->lineno);
				} else
					ast_log(LOG_WARNING, "Span number must be a postive integer at line %d of zapata.conf\n", v->lineno);
			} else {
				ast_log(LOG_NOTICE, "Ignoring unknown keyword '%s' in trunkgroups\n", v->name);
			}
			v = v->next;
		}
	}
#endif
	v = ast_variable_browse(cfg, "channels");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "channel")
#ifdef ZAPATA_PRI
			|| !strcasecmp(v->name, "crv")
#endif			
					) {
			if (reload == 0) {
				if (cur_signalling < 0) {
					ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					return -1;
				}
			}
			c = v->value;

#ifdef ZAPATA_PRI
			pri = NULL;
			if (!strcasecmp(v->name, "crv")) {
				if (sscanf(c, "%d:%n", &trunkgroup, &y) != 1) {
					ast_log(LOG_WARNING, "CRV must begin with trunkgroup followed by a colon at line %d\n", v->lineno);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					return -1;
				}
				if (trunkgroup < 1) {
					ast_log(LOG_WARNING, "CRV trunk group must be a postive number at line %d\n", v->lineno);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					return -1;
				}
				c+=y;
				for (y=0;y<NUM_SPANS;y++) {
					if (pris[y].trunkgroup == trunkgroup) {
						pri = pris + y;
						break;
					}
				}
				if (!pri) {
					ast_log(LOG_WARNING, "No such trunk group %d at CRV declaration at line %d\n", trunkgroup, v->lineno);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					return -1;
				}
			}
#endif			
			chan = strsep(&c, ",");
			while(chan) {
				if (sscanf(chan, "%d-%d", &start, &finish) == 2) {
					/* Range */
				} else if (sscanf(chan, "%d", &start)) {
					/* Just one */
					finish = start;
				} else if (!strcasecmp(chan, "pseudo")) {
					finish = start = CHAN_PSEUDO;
					found_pseudo = 1;
				} else {
					ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", v->value, chan);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					return -1;
				}
				if (finish < start) {
					ast_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
					x = finish;
					finish = start;
					start = x;
				}
				for (x=start;x<=finish;x++) {
#ifdef ZAPATA_PRI
					tmp = mkintf(x, cur_signalling, cur_radio, pri, reload);
#else					
					tmp = mkintf(x, cur_signalling, cur_radio, NULL, reload);
#endif					

					if (tmp) {
						if (option_verbose > 2) {
#ifdef ZAPATA_PRI
							if (pri)
								ast_verbose(VERBOSE_PREFIX_3 "%s CRV %d:%d, %s signalling\n", reload ? "Reconfigured" : "Registered", trunkgroup,x, sig2str(tmp->sig));
							else
#endif
								ast_verbose(VERBOSE_PREFIX_3 "%s channel %d, %s signalling\n", reload ? "Reconfigured" : "Registered", x, sig2str(tmp->sig));
						}
					} else {
						if (reload == 1)
							ast_log(LOG_ERROR, "Unable to reconfigure channel '%s'\n", v->value);
						else
							ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
						ast_config_destroy(cfg);
						ast_mutex_unlock(&iflock);
						return -1;
					}
				}
				chan = strsep(&c, ",");
			}
		} else if (!strcasecmp(v->name, "usedistinctiveringdetection")) {
			if (ast_true(v->value))
				usedistinctiveringdetection = 1;
		} else if (!strcasecmp(v->name, "dring1context")) {
			ast_copy_string(drings.ringContext[0].contextData,v->value,sizeof(drings.ringContext[0].contextData));
		} else if (!strcasecmp(v->name, "dring2context")) {
			ast_copy_string(drings.ringContext[1].contextData,v->value,sizeof(drings.ringContext[1].contextData));
		} else if (!strcasecmp(v->name, "dring3context")) {
			ast_copy_string(drings.ringContext[2].contextData,v->value,sizeof(drings.ringContext[2].contextData));
		} else if (!strcasecmp(v->name, "dring1")) {
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[0].ring[0], &drings.ringnum[0].ring[1], &drings.ringnum[0].ring[2]);
		} else if (!strcasecmp(v->name, "dring2")) {
			ringc = v->value;
			sscanf(ringc,"%d,%d,%d", &drings.ringnum[1].ring[0], &drings.ringnum[1].ring[1], &drings.ringnum[1].ring[2]);
		} else if (!strcasecmp(v->name, "dring3")) {
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[2].ring[0], &drings.ringnum[2].ring[1], &drings.ringnum[2].ring[2]);
		} else if (!strcasecmp(v->name, "usecallerid")) {
			use_callerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "cidsignalling")) {
			if (!strcasecmp(v->value, "bell"))
				cid_signalling = CID_SIG_BELL;
			else if (!strcasecmp(v->value, "v23"))
				cid_signalling = CID_SIG_V23;
			else if (!strcasecmp(v->value, "dtmf"))
				cid_signalling = CID_SIG_DTMF;
			else if (ast_true(v->value))
				cid_signalling = CID_SIG_BELL;
		} else if (!strcasecmp(v->name, "cidstart")) {
			if (!strcasecmp(v->value, "ring"))
				cid_start = CID_START_RING;
			else if (!strcasecmp(v->value, "polarity"))
				cid_start = CID_START_POLARITY;
			else if (ast_true(v->value))
				cid_start = CID_START_RING;
		} else if (!strcasecmp(v->name, "threewaycalling")) {
			threewaycalling = ast_true(v->value);
		} else if (!strcasecmp(v->name, "cancallforward")) {
			cancallforward = ast_true(v->value);
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			if (ast_true(v->value)) 
				relaxdtmf = DSP_DIGITMODE_RELAXDTMF;
			else
				relaxdtmf = 0;
		} else if (!strcasecmp(v->name, "mailbox")) {
			ast_copy_string(mailbox, v->value, sizeof(mailbox));
		} else if (!strcasecmp(v->name, "adsi")) {
			adsi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfer")) {
			transfer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "canpark")) {
			canpark = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancelwhenbridged")) {
			echocanbridged = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busydetect")) {
			busydetect = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busycount")) {
			busycount = atoi(v->value);
		} else if (!strcasecmp(v->name, "busypattern")) {
			if (sscanf(v->value, "%d,%d", &busy_tonelength, &busy_quietlength) != 2) {
				ast_log(LOG_ERROR, "busypattern= expects busypattern=tonelength,quietlength\n");
			}
		} else if (!strcasecmp(v->name, "callprogress")) {
			if (ast_true(v->value))
				callprogress |= 1;
			else
				callprogress &= ~1;
		} else if (!strcasecmp(v->name, "faxdetect")) {
			if (!strcasecmp(v->value, "incoming")) {
				callprogress |= 4;
				callprogress &= ~2;
			} else if (!strcasecmp(v->value, "outgoing")) {
				callprogress &= ~4;
				callprogress |= 2;
			} else if (!strcasecmp(v->value, "both") || ast_true(v->value))
				callprogress |= 6;
			else
				callprogress &= ~6;
		} else if (!strcasecmp(v->name, "echocancel")) {
			if (v->value && !ast_strlen_zero(v->value)) {
				y = atoi(v->value);
			} else
				y = 0;
			if ((y == 32) || (y == 64) || (y == 128) || (y == 256))
				echocancel = y;
			else {
				echocancel = ast_true(v->value);
				if (echocancel)
					echocancel=128;
			}
		} else if (!strcasecmp(v->name, "echotraining")) {
			if (sscanf(v->value, "%d", &y) == 1) {
				if ((y < 10) || (y > 4000)) {
					ast_log(LOG_WARNING, "Echo training time must be within the range of 10 to 2000 ms at line %d\n", v->lineno);					
				} else {
					echotraining = y;
				}
			} else if (ast_true(v->value)) {
				echotraining = 400;
			} else
				echotraining = 0;
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			hidecallerid = ast_true(v->value);
 		} else if (!strcasecmp(v->name, "pulsedial")) {
 			pulse = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callreturn")) {
			callreturn = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			callwaiting = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			callwaitingcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(context, v->value, sizeof(context));
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(language, v->value, sizeof(language));
		} else if (!strcasecmp(v->name, "progzone")) {
			ast_copy_string(progzone, v->value, sizeof(progzone));
		} else if (!strcasecmp(v->name, "musiconhold")) {
			ast_copy_string(musicclass, v->value, sizeof(musicclass));
		} else if (!strcasecmp(v->name, "stripmsd")) {
			stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "jitterbuffers")) {
			numbufs = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			cur_group = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "callgroup")) {
			cur_callergroup = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "pickupgroup")) {
			cur_pickupgroup = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "immediate")) {
			immediate = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfertobusy")) {
			transfertobusy = ast_true(v->value);
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%f", &rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid rxgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%f", &txgain) != 1) {
				ast_log(LOG_WARNING, "Invalid txgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "tonezone")) {
			if (sscanf(v->value, "%d", &tonezone) != 1) {
				ast_log(LOG_WARNING, "Invalid tonezone: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived")) {
				cid_num[0] = '\0';
				cid_name[0] = '\0';
			} else {
				ast_callerid_split(v->value, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
			}
		} else if (!strcasecmp(v->name, "useincomingcalleridonzaptransfer")) {
			zaptrcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "restrictcid")) {
			restrictcid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "usecallingpres")) {
			use_callingpres = ast_true(v->value);
		} else if (!strcasecmp(v->name, "accountcode")) {
			ast_copy_string(accountcode, v->value, sizeof(accountcode));
		} else if (!strcasecmp(v->name, "amaflags")) {
			y = ast_cdr_amaflags2int(v->value);
			if (y < 0) 
				ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
			else
				amaflags = y;
		} else if(!reload){ 
			 if (!strcasecmp(v->name, "signalling")) {
				if (!strcasecmp(v->value, "em")) {
					cur_signalling = SIG_EM;
				} else if (!strcasecmp(v->value, "em_e1")) {
					cur_signalling = SIG_EM_E1;
				} else if (!strcasecmp(v->value, "em_w")) {
					cur_signalling = SIG_EMWINK;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_ls")) {
					cur_signalling = SIG_FXSLS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_gs")) {
					cur_signalling = SIG_FXSGS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_ks")) {
					cur_signalling = SIG_FXSKS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxo_ls")) {
					cur_signalling = SIG_FXOLS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxo_gs")) {
					cur_signalling = SIG_FXOGS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxo_ks")) {
					cur_signalling = SIG_FXOKS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_rx")) {
					cur_signalling = SIG_FXSKS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "fxo_rx")) {
					cur_signalling = SIG_FXOLS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "fxs_tx")) {
					cur_signalling = SIG_FXSLS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "fxo_tx")) {
					cur_signalling = SIG_FXOGS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "em_rx")) {
					cur_signalling = SIG_EM;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "em_tx")) {
					cur_signalling = SIG_EM;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "em_rxtx")) {
					cur_signalling = SIG_EM;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "em_txrx")) {
					cur_signalling = SIG_EM;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "sf")) {
					cur_signalling = SIG_SF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_w")) {
					cur_signalling = SIG_SFWINK;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_featd")) {
					cur_signalling = SIG_FEATD;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_featdmf")) {
					cur_signalling = SIG_FEATDMF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_featb")) {
					cur_signalling = SIG_SF_FEATB;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf")) {
					cur_signalling = SIG_SF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_rx")) {
					cur_signalling = SIG_SF;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "sf_tx")) {
					cur_signalling = SIG_SF;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "sf_rxtx")) {
					cur_signalling = SIG_SF;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "sf_txrx")) {
					cur_signalling = SIG_SF;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "featd")) {
					cur_signalling = SIG_FEATD;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "featdmf")) {
					cur_signalling = SIG_FEATDMF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "featdmf_ta")) {
					cur_signalling = SIG_FEATDMF_TA;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "e911")) {
					cur_signalling = SIG_E911;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "featb")) {
					cur_signalling = SIG_FEATB;
					cur_radio = 0;
#ifdef ZAPATA_PRI
				} else if (!strcasecmp(v->value, "pri_net")) {
					cur_radio = 0;
					cur_signalling = SIG_PRI;
					pritype = PRI_NETWORK;
				} else if (!strcasecmp(v->value, "pri_cpe")) {
					cur_signalling = SIG_PRI;
					cur_radio = 0;
					pritype = PRI_CPE;
				} else if (!strcasecmp(v->value, "gr303fxoks_net")) {
					cur_signalling = SIG_GR303FXOKS;
					cur_radio = 0;
					pritype = PRI_NETWORK;
				} else if (!strcasecmp(v->value, "gr303fxsks_cpe")) {
					cur_signalling = SIG_GR303FXSKS;
					cur_radio = 0;
					pritype = PRI_CPE;
#endif
#ifdef ZAPATA_R2
				} else if (!strcasecmp(v->value, "r2")) {
					cur_signalling = SIG_R2;
					cur_radio = 0;
#endif			
				} else {
					ast_log(LOG_ERROR, "Unknown signalling method '%s'\n", v->value);
				}
#ifdef ZAPATA_R2
			} else if (!strcasecmp(v->name, "r2country")) {
				r2prot = str2r2prot(v->value);
				if (r2prot < 0) {
					ast_log(LOG_WARNING, "Unknown R2 Country '%s' at line %d.\n", v->value, v->lineno);
				}
#endif
#ifdef ZAPATA_PRI
			} else if (!strcasecmp(v->name, "pridialplan")) {
				if (!strcasecmp(v->value, "national")) {
					dialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					dialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					dialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					dialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					dialplan = PRI_LOCAL_ISDN + 1;
	 			} else if (!strcasecmp(v->value, "dynamic")) {
 					dialplan = -1;
				} else {
					ast_log(LOG_WARNING, "Unknown PRI dialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "prilocaldialplan")) {
				if (!strcasecmp(v->value, "national")) {
					localdialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					localdialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					localdialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					localdialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					localdialplan = PRI_LOCAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "dynamic")) {
					localdialplan = -1;
				} else {
					ast_log(LOG_WARNING, "Unknown PRI dialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "switchtype")) {
				if (!strcasecmp(v->value, "national")) 
					switchtype = PRI_SWITCH_NI2;
				else if (!strcasecmp(v->value, "ni1"))
					switchtype = PRI_SWITCH_NI1;
				else if (!strcasecmp(v->value, "dms100"))
					switchtype = PRI_SWITCH_DMS100;
				else if (!strcasecmp(v->value, "4ess"))
					switchtype = PRI_SWITCH_ATT4ESS;
				else if (!strcasecmp(v->value, "5ess"))
					switchtype = PRI_SWITCH_LUCENT5E;
				else if (!strcasecmp(v->value, "euroisdn"))
					switchtype = PRI_SWITCH_EUROISDN_E1;
				else if (!strcasecmp(v->value, "qsig"))
					switchtype = PRI_SWITCH_QSIG;
				else {
					ast_log(LOG_ERROR, "Unknown switchtype '%s'\n", v->value);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					return -1;
				}
			} else if (!strcasecmp(v->name, "nsf")) {
				if (!strcasecmp(v->value, "sdn"))
					nsf = PRI_NSF_SDN;
				else if (!strcasecmp(v->value, "megacom"))
					nsf = PRI_NSF_MEGACOM;
				else if (!strcasecmp(v->value, "accunet"))
					nsf = PRI_NSF_ACCUNET;
				else if (!strcasecmp(v->value, "none"))
					nsf = PRI_NSF_NONE;
				else {
					ast_log(LOG_WARNING, "Unknown network-specific facility '%s'\n", v->value);
					nsf = PRI_NSF_NONE;
				}
			} else if (!strcasecmp(v->name, "priindication")) {
				if (!strcasecmp(v->value, "outofband"))
					priindication_oob = 1;
				else if (!strcasecmp(v->value, "inband"))
					priindication_oob = 0;
				else
					ast_log(LOG_WARNING, "'%s' is not a valid pri indication value, should be 'inband' or 'outofband' at line %d\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "priexclusive")) {
				cur_priexclusive = ast_true(v->value);
			} else if (!strcasecmp(v->name, "internationalprefix")) {
				ast_copy_string(internationalprefix, v->value, sizeof(internationalprefix));
			} else if (!strcasecmp(v->name, "nationalprefix")) {
				ast_copy_string(nationalprefix, v->value, sizeof(nationalprefix));
			} else if (!strcasecmp(v->name, "localprefix")) {
				ast_copy_string(localprefix, v->value, sizeof(localprefix));
			} else if (!strcasecmp(v->name, "privateprefix")) {
				ast_copy_string(privateprefix, v->value, sizeof(privateprefix));
			} else if (!strcasecmp(v->name, "unknownprefix")) {
				ast_copy_string(unknownprefix, v->value, sizeof(unknownprefix));
			} else if (!strcasecmp(v->name, "resetinterval")) {
				if (!strcasecmp(v->value, "never"))
					resetinterval = -1;
				else if( atoi(v->value) >= 60 )
					resetinterval = atoi(v->value);
				else
					ast_log(LOG_WARNING, "'%s' is not a valid reset interval, should be >= 60 seconds or 'never' at line %d\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "minunused")) {
				minunused = atoi(v->value);
			} else if (!strcasecmp(v->name, "idleext")) {
				ast_copy_string(idleext, v->value, sizeof(idleext));
			} else if (!strcasecmp(v->name, "idledial")) {
				ast_copy_string(idledial, v->value, sizeof(idledial));
			} else if (!strcasecmp(v->name, "overlapdial")) {
				overlapdial = ast_true(v->value);
			} else if (!strcasecmp(v->name, "pritimer")) {
#ifdef PRI_GETSET_TIMERS
				char *timerc;
				int timer, timeridx;
				c = v->value;
				timerc = strsep(&c, ",");
				if (timerc) {
					timer = atoi(c);
					if (!timer)
						ast_log(LOG_WARNING, "'%s' is not a valid value for an ISDN timer\n", timerc);
					else {
						if ((timeridx = pri_timer2idx(timerc)) >= 0)
							pritimers[timeridx] = timer;
						else
							ast_log(LOG_WARNING, "'%s' is not a valid ISDN timer\n", timerc);
					}
				} else
					ast_log(LOG_WARNING, "'%s' is not a valid ISDN timer configuration string\n", v->value);

			} else if (!strcasecmp(v->name, "facilityenable")) {
				facilityenable = ast_true(v->value);
#endif /* PRI_GETSET_TIMERS */
#endif /* ZAPATA_PRI */
			} else if (!strcasecmp(v->name, "cadence")) {
				/* setup to scan our argument */
				int element_count, c[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
				int i;
				struct zt_ring_cadence new_cadence;
				int cid_location = -1;
                		int firstcadencepos = 0;
				char original_args[80];
				int cadence_is_ok = 1;

				ast_copy_string(original_args, v->value, sizeof(original_args));
				/* 16 cadences allowed (8 pairs) */
				element_count = sscanf(v->value, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8], &c[9], &c[10], &c[11], &c[12], &c[13], &c[14], &c[15]);
	
				/* Cadence must be even (on/off) */
				if (element_count % 2 == 1) {
					ast_log(LOG_ERROR, "Must be a silence duration for each ring duration: %s\n",original_args);
					cadence_is_ok = 0;
				}
	
				/* Ring cadences cannot be negative */
				for (i=0;i<element_count;i++) {
				        if (c[i] == 0) {
					        ast_log(LOG_ERROR, "Ring or silence duration cannot be zero: %s\n", original_args);
						cadence_is_ok = 0;
						break;
					} else if (c[i] < 0) {
						if (i % 2 == 1) {
						        /* Silence duration, negative possibly okay */
							if (cid_location == -1) {
							        cid_location = i;
								c[i] *= -1;
							} else {
							        ast_log(LOG_ERROR, "CID location specified twice: %s\n",original_args);
								cadence_is_ok = 0;
								break;
							}
						} else {
							if (firstcadencepos == 0) {
							        firstcadencepos = i; /* only recorded to avoid duplicate specification */
								                     /* duration will be passed negative to the zaptel driver */
							} else {
							        ast_log(LOG_ERROR, "First cadence position specified twice: %s\n",original_args);
								cadence_is_ok = 0;
								break;
							}
						}
					}
				}
	
				/* Substitute our scanned cadence */
				for (i=0;i<16;i++) {
					new_cadence.ringcadence[i] = c[i];
				}
	
				if (cadence_is_ok) {
					/* ---we scanned it without getting annoyed; now some sanity checks--- */
					if (element_count < 2) {
						ast_log(LOG_ERROR, "Minimum cadence is ring,pause: %s\n", original_args);
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
							ast_log(LOG_ERROR, "Already %d cadences; can't add another: %s\n", NUM_CADENCE_MAX, original_args);
						else {
							cadences[num_cadence] = new_cadence;
							cidrings[num_cadence++] = cid_location;
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "cadence 'r%d' added: %s\n",num_cadence,original_args);
						}
					}
				}
			} else if (!strcasecmp(v->name, "ringtimeout")) {
				ringt_base = (atoi(v->value) * 8) / READ_SIZE;
			} else if (!strcasecmp(v->name, "prewink")) {
				cur_prewink = atoi(v->value);
			} else if (!strcasecmp(v->name, "preflash")) {
				cur_preflash = atoi(v->value);
			} else if (!strcasecmp(v->name, "wink")) {
				cur_wink = atoi(v->value);
			} else if (!strcasecmp(v->name, "flash")) {
				cur_flash = atoi(v->value);
			} else if (!strcasecmp(v->name, "start")) {
				cur_start = atoi(v->value);
			} else if (!strcasecmp(v->name, "rxwink")) {
				cur_rxwink = atoi(v->value);
			} else if (!strcasecmp(v->name, "rxflash")) {
				cur_rxflash = atoi(v->value);
			} else if (!strcasecmp(v->name, "debounce")) {
				cur_debounce = atoi(v->value);
			} else if (!strcasecmp(v->name, "toneduration")) {
				int toneduration;
				int ctlfd;
				int res;
				struct zt_dialparams dps;

				ctlfd = open("/dev/zap/ctl", O_RDWR);
				if (ctlfd == -1) {
					ast_log(LOG_ERROR, "Unable to open /dev/zap/ctl to set toneduration\n");
					return -1;
				}

				toneduration = atoi(v->value);
				if (toneduration > -1) {
					dps.dtmf_tonelen = dps.mfv1_tonelen = toneduration;
					res = ioctl(ctlfd, ZT_SET_DIALPARAMS, &dps);
					if (res < 0) {
						ast_log(LOG_ERROR, "Invalid tone duration: %d ms\n", toneduration);
						return -1;
					}
				}
				close(ctlfd);
			} else if (!strcasecmp(v->name, "polarityonanswerdelay")) {
				polarityonanswerdelay = atoi(v->value);
			} else if (!strcasecmp(v->name, "answeronpolarityswitch")) {
				answeronpolarityswitch = ast_true(v->value);
			} else if (!strcasecmp(v->name, "hanguponpolarityswitch")) {
				hanguponpolarityswitch = ast_true(v->value);
			} else if (!strcasecmp(v->name, "sendcalleridafter")) {
				sendcalleridafter = atoi(v->value);
			} else if (!strcasecmp(v->name, "defaultcic")) {
				ast_copy_string(defaultcic, v->value, sizeof(defaultcic));
			} else if (!strcasecmp(v->name, "defaultozz")) {
				ast_copy_string(defaultozz, v->value, sizeof(defaultozz));
			} 
		} else 
			ast_log(LOG_WARNING, "Ignoring %s\n", v->name);
		v = v->next;
	}
	if (!found_pseudo && reload == 0) {
	
		/* Make sure pseudo isn't a member of any groups if
		   we're automatically making it. */	
		cur_group = 0;
		cur_callergroup = 0;
		cur_pickupgroup = 0;
	
		tmp = mkintf(CHAN_PSEUDO, cur_signalling, cur_radio, NULL, reload);

		if (tmp) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Automatically generated pseudo channel\n");
		} else {
			ast_log(LOG_WARNING, "Unable to register pseudo channel!\n");
		}
	}
	ast_mutex_unlock(&iflock);
	ast_config_destroy(cfg);
#ifdef ZAPATA_PRI
	if (!reload) {
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].pvts[0]) {
				if (start_pri(pris + x)) {
					ast_log(LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
				} else if (option_verbose > 1)
					ast_verbose(VERBOSE_PREFIX_2 "Starting D-Channel on span %d\n", x + 1);
			}
		}
	}
#endif
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int load_module(void)
{
	int res;

#ifdef ZAPATA_PRI
	int y,i;
	memset(pris, 0, sizeof(pris));
	for (y=0;y<NUM_SPANS;y++) {
		ast_mutex_init(&pris[y].lock);
		pris[y].offset = -1;
		pris[y].master = AST_PTHREADT_NULL;
		for (i=0;i<NUM_DCHANS;i++)
			pris[y].fds[i] = -1;
	}
	pri_set_error(zt_pri_error);
	pri_set_message(zt_pri_message);
#endif
	res = setup_zap(0);
	/* Make sure we can register our Zap channel type */
	if(res) {
	  return -1;
	}
	if (ast_channel_register(&zap_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		__unload_module();
		return -1;
	}
#ifdef ZAPATA_PRI
	ast_cli_register_multiple(zap_pri_cli, sizeof(zap_pri_cli) / sizeof(zap_pri_cli[0]));
#endif	
#ifdef ZAPATA_R2
	ast_cli_register_multiple(zap_r2_cli, sizeof(zap_r2_cli) / sizeof(zap_r2_cli[0]));
#endif	
	ast_cli_register_multiple(zap_cli, sizeof(zap_cli) / sizeof(zap_cli[0]));
	
	memset(round_robin, 0, sizeof(round_robin));
	ast_manager_register( "ZapTransfer", 0, action_transfer, "Transfer Zap Channel" );
	ast_manager_register( "ZapHangup", 0, action_transferhangup, "Hangup Zap Channel" );
	ast_manager_register( "ZapDialOffhook", 0, action_zapdialoffhook, "Dial over Zap channel while offhook" );
	ast_manager_register( "ZapDNDon", 0, action_zapdndon, "Toggle Zap channel Do Not Disturb status ON" );
	ast_manager_register( "ZapDNDoff", 0, action_zapdndoff, "Toggle Zap channel Do Not Disturb status OFF" );
	ast_manager_register("ZapShowChannels", 0, action_zapshowchannels, "Show status zapata channels");

	return res;
}

static int zt_sendtext(struct ast_channel *c, const char *text)
{
#define	END_SILENCE_LEN 400
#define	HEADER_MS 50
#define	TRAILER_MS 5
#define	HEADER_LEN ((HEADER_MS + TRAILER_MS) * 8)
#define	ASCII_BYTES_PER_CHAR 80

	unsigned char *buf,*mybuf;
	struct zt_pvt *p = c->tech_pvt;
	struct pollfd fds[1];
	int size,res,fd,len,x;
	int bytes=0;
	/* Initial carrier (imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	int index;

	index = zt_get_index(c, p, 0);
	if (index < 0) {
		ast_log(LOG_WARNING, "Huh?  I don't exist?\n");
		return -1;
	}
	if (!text[0]) return(0); /* if nothing to send, dont */
	if ((!p->tdd) && (!p->mate)) return(0);  /* if not in TDD mode, just return */
	if (p->mate) 
		buf = malloc(((strlen(text) + 1) * ASCII_BYTES_PER_CHAR) + END_SILENCE_LEN + HEADER_LEN);
	else
		buf = malloc(((strlen(text) + 1) * TDD_BYTES_PER_CHAR) + END_SILENCE_LEN);
	if (!buf) {
		ast_log(LOG_ERROR, "MALLOC FAILED\n");
		return -1;
	}
	mybuf = buf;
	if (p->mate) {
		int codec = AST_LAW(p);
		for (x=0;x<HEADER_MS;x++) {	/* 50 ms of Mark */
			PUT_CLID_MARKMS;
			}
		/* Put actual message */
		for (x=0;text[x];x++)  {
			PUT_CLID(text[x]);
			}
		for (x=0;x<TRAILER_MS;x++) {	/* 5 ms of Mark */
			PUT_CLID_MARKMS;
			}
		len = bytes;
		buf = mybuf;
	}
	else {
		len = tdd_generate(p->tdd,buf,text);
		if (len < 1) {
			ast_log(LOG_ERROR, "TDD generate (len %d) failed!!\n",(int)strlen(text));
			free(mybuf);
			return -1;
		}
	}
	memset(buf + len,0x7f,END_SILENCE_LEN);
	len += END_SILENCE_LEN;
	fd = p->subs[index].zfd;
	while(len) {
		if (ast_check_hangup(c)) {
			free(mybuf);
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
			ast_log(LOG_DEBUG, "poll (for write) ret. 0 on channel %d\n", p->channel);
			continue;
		}
		  /* if got exception */
		if (fds[0].revents & POLLPRI) return -1;
		if (!(fds[0].revents & POLLOUT)) {
			ast_log(LOG_DEBUG, "write fd not ready on channel %d\n", p->channel);
			continue;
		}
		res = write(fd, buf, size);
		if (res != size) {
			if (res == -1) {
				free(mybuf);
				return -1;
			}
			if (option_debug)
				ast_log(LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			break;
		}
		len -= size;
		buf += size;
	}
	free(mybuf);
	return(0);
}


int reload(void)
{
	int res = 0;

	res = setup_zap(1);
	if (res) {
		ast_log(LOG_WARNING, "Reload of chan_zap.so is unsuccessful!\n");
		return -1;
	}
	return 0;
}

int usecount()
{
	return usecnt;
}

char *description()
{
	return (char *) desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

