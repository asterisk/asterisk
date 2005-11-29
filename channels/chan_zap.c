/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Tormenta T1 Card (via Zapata library) support 
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/file.h>
#include <asterisk/ulaw.h>
#include <asterisk/alaw.h>
#include <asterisk/callerid.h>
#include <asterisk/adsi.h>
#include <asterisk/cli.h>
#include <asterisk/cdr.h>
#include <asterisk/parking.h>
#include <asterisk/musiconhold.h>
#include <asterisk/say.h>
#include <asterisk/tdd.h>
#include <asterisk/app.h>
#include <asterisk/dsp.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/zaptel.h>
#include <math.h>
#include <tonezone.h>
#include <ctype.h>
#ifdef ZAPATA_PRI
#include <libpri.h>
#endif
#ifdef ZAPATA_R2
#include <libmfcr2.h>
#endif

#include "../asterisk.h"

/* 
   XXX 
   XXX   We definitely need to lock the private structure in zt_read and such 
   XXX  
 */


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

#define CHANNEL_PSEUDO -12

#define AST_LAW(p) (((p)->law == ZT_LAW_ALAW) ? AST_FORMAT_ALAW : AST_FORMAT_ULAW)

static char *desc = "Zapata Telephony"
#ifdef ZAPATA_PRI
               " w/PRI"
#endif
#ifdef ZAPATA_R2
               " w/R2"
#endif
;

static char *tdesc = "Zapata Telephony Driver"
#ifdef ZAPATA_PRI
               " w/PRI"
#endif
#ifdef ZAPATA_R2
               " w/R2"
#endif
;

static char *type = "Zap";
static char *typecompat = "Tor";	/* Retain compatibility with chan_tor */
static char *config = "zapata.conf";

#define SIG_EM		ZT_SIG_EM
#define SIG_EMWINK 	(0x10000 | ZT_SIG_EM)
#define SIG_FEATD	(0x20000 | ZT_SIG_EM)
#define	SIG_FEATDMF	(0x40000 | ZT_SIG_EM)
#define	SIG_FEATB	(0x80000 | ZT_SIG_EM)
#define SIG_FXSLS	ZT_SIG_FXSLS
#define SIG_FXSGS	ZT_SIG_FXSGS
#define SIG_FXSKS	ZT_SIG_FXSKS
#define SIG_FXOLS	ZT_SIG_FXOLS
#define SIG_FXOGS	ZT_SIG_FXOGS
#define SIG_FXOKS	ZT_SIG_FXOKS
#define SIG_PRI		ZT_SIG_CLEAR
#define SIG_R2		ZT_SIG_CAS

#define NUM_SPANS 	32
#define RESET_INTERVAL	3600	/* How often (in seconds) to reset unused channels */

#define CHAN_PSEUDO	-2

static char context[AST_MAX_EXTENSION] = "default";
static char callerid[256] = "";

static char language[MAX_LANGUAGE] = "";
static char musicclass[MAX_LANGUAGE] = "";

static int use_callerid = 1;

static int cur_signalling = -1;

static int cur_group = 0;
static int cur_callergroup = 0;
static int cur_pickupgroup = 0;
static int relaxdtmf = 0;

static int immediate = 0;

static int stripmsd = 0;

static int callwaiting = 0;

static int callwaitingcallerid = 0;

static int hidecallerid = 0;

static int callreturn = 0;

static int threewaycalling = 0;

static int transfer = 0;

static int cancallforward = 0;

static float rxgain = 0.0;

static float txgain = 0.0;

static int echocancel;

static int echocanbridged = 0;

static int busydetect = 0;

static int callprogress = 0;

static char accountcode[20] = "";

static char mailbox[AST_MAX_EXTENSION];

static int amaflags = 0;

static int adsi = 0;

#ifdef ZAPATA_PRI
static int minunused = 2;
static int minidle = 0;
static char idleext[AST_MAX_EXTENSION];
static char idledial[AST_MAX_EXTENSION];
#endif

/* Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/* How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

/* How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

static int usecnt =0;
static pthread_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

/* Protect the interface list (of zt_pvt's) */
static pthread_mutex_t iflock = AST_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static pthread_mutex_t monlock = AST_MUTEX_INITIALIZER;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = 0;

static int restart_monitor(void);

static int zt_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc);

static int zt_sendtext(struct ast_channel *c, char *text);

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
#define RINGT 						( (8000 * 8) / READ_SIZE)

struct zt_pvt;


#ifdef ZAPATA_R2
static int r2prot = -1;
#endif


#ifdef ZAPATA_PRI
struct zt_pri {
	pthread_t master;			/* Thread of master */
	pthread_mutex_t lock;		/* Mutex */
	char idleext[AST_MAX_EXTENSION];		/* Where to idle extra calls */
	char idlecontext[AST_MAX_EXTENSION];		/* What context to use for idle */
	char idledial[AST_MAX_EXTENSION];		/* What to dial before dumping */
	int minunused;				/* Min # of channels to keep empty */
	int minidle;				/* Min # of "idling" calls to keep active */
	int nodetype;				/* Node type */
	int switchtype;				/* Type of switch to emulate */
	int dialplan;			/* Dialing plan */
	int dchannel;			/* What channel the dchannel is on */
	int channels;			/* Num of chans in span (31 or 24) */
	struct pri *pri;
	int debug;
	int fd;
	int up;
	int offset;
	int span;
	int chanmask[32];			/* Channel status */
	int resetting;
	int resetchannel;
	time_t lastreset;
	struct zt_pvt *pvt[32];	/* Member channel pvt structs */
	struct zt_channel *chan[32];	/* Channels on each line */
};


static struct zt_pri pris[NUM_SPANS];

static int pritype = PRI_CPE;

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
#endif

static inline int pri_grab(struct zt_pri *pri)
{
	int res;
	/* Grab the lock first */
    res = ast_pthread_mutex_lock(&pri->lock);
	if (res)
		return res;
	/* Then break the select */
	pthread_kill(pri->master, SIGURG);
	return 0;
}

static inline void pri_rel(struct zt_pri *pri)
{
	ast_pthread_mutex_unlock(&pri->lock);
}

static int switchtype = PRI_SWITCH_NI2;
static int dialplan = PRI_NATIONAL_ISDN + 1;

#endif

#define SUB_REAL		0			/* Active call */
#define SUB_CALLWAIT	1			/* Call-Waiting call on hold */
#define SUB_THREEWAY	2			/* Three-way call */

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
	int needringing;
	int needcallerid;
	int needanswer;
	int linear;
	int inthreeway;
	int curconfno;			/* What conference we're currently in */
};

#define CONF_USER_REAL		(1 << 0)
#define CONF_USER_THIRDCALL	(1 << 1)

#define MAX_SLAVES	4

static struct zt_pvt {
	pthread_mutex_t lock;
	struct ast_channel *owner;	/* Our current active owner (if applicable) */
		/* Up to three channels can be associated with this call */
		
	struct zt_subchannel sub_unused;	/* Just a safety precaution */
	struct zt_subchannel subs[3];	/* Sub-channels */
	struct zt_confinfo saveconf;	/* Saved conference info */

	struct zt_pvt *slaves[MAX_SLAVES];	/* Slave to us (follows our conferencing) */
	struct zt_pvt *master;	/* Master to us (we follow their conferencing) */
	int inconference;		/* If our real should be in the conference */
	
	int sig;					/* Signalling style */
	int radio;				/* radio type */
	int firstradio;				/* first radio flag */
	float rxgain;
	float txgain;
	struct zt_pvt *next;			/* Next channel in list */
	char context[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char musicclass[MAX_LANGUAGE];
	char callerid[AST_MAX_EXTENSION];
	char lastcallerid[AST_MAX_EXTENSION];
	char callwaitcid[AST_MAX_EXTENSION];
	char rdnis[AST_MAX_EXTENSION];
	int group;
	int law;
	int confno;					/* Our conference */
	int confusers;				/* Who is using our conference */
	int propconfno;				/* Propagated conference number */
	int callgroup;
	int pickupgroup;
	int immediate;				/* Answer before getting digits? */
	int channel;				/* Channel Number */
	int span;					/* Span number */
	int dialing;
	int dialednone;
	int use_callerid;			/* Whether or not to use caller id on this channel */
	int hidecallerid;
	int callreturn;
	int permhidecallerid;		/* Whether to hide our outgoing caller ID or not */
	int callwaitingrepeat;		/* How many samples to wait before repeating call waiting */
	int cidcwexpire;			/* When to expire our muting for CID/CW */
	unsigned char *cidspill;
	int cidpos;
	int cidlen;
	int ringt;
	int stripmsd;
	int callwaiting;
	int callwaitcas;
	int callwaitrings;
	int echocancel;
	int echocanbridged;
	int echocanon;
	int permcallwaiting;
	int callwaitingcallerid;
	int threewaycalling;
	int transfer;
	int digital;
	int outgoing;
	int dnd;
	int busydetect;
	int callprogress;
	struct ast_dsp *dsp;
	int cref;					/* Call reference number */
	ZT_DIAL_OPERATION dop;
	int destroy;
	int ignoredtmf;				
	int inalarm;
	char accountcode[20];		/* Account code */
	int amaflags;				/* AMA Flags */
	char didtdd;			/* flag to say its done it once */
	struct tdd_state *tdd;		/* TDD flag */
	int adsi;
	int cancallforward;
	char call_forward[AST_MAX_EXTENSION];
	char mailbox[AST_MAX_EXTENSION];
	int onhooktime;
	int msgstate;
	
	int confirmanswer;		/* Wait for '#' to confirm answer */
	int distinctivering;	/* Which distinctivering to use */
	int cidrings;			/* Which ring to deliver CID on */
	
	int faxhandled;			/* Has a fax tone already been handled? */
	
	char mate;			/* flag to say its in MATE mode */
	int pulsedial;		/* whether a pulse dial phone is detected */
	int dtmfrelax;		/* whether to run in relaxed DTMF mode */
#ifdef ZAPATA_PRI
	struct zt_pri *pri;
	q931_call *call;
	int isidlecall;
	int resetting;
	int prioffset;
	int alreadyhungup;
#endif	
#ifdef ZAPATA_R2
	int r2prot;
	mfcr2_t *r2;
	int hasr2call;
	int r2blocked;
	int sigchecked;
#endif	
} *iflist = NULL;

static struct zt_ring_cadence cadences[] = {
	{ { 125, 125, 2000, 4000 } },			/* Quick chirp followed by normal ring */
	{ { 250, 250, 500, 1000, 250, 250, 500, 4000 } }, /* British style ring */
	{ { 125, 125, 125, 125, 125, 4000 } },	/* Three short bursts */
	{ { 1000, 500, 2500, 5000 } },	/* Long ring */
};

static int cidrings[] = {
	2,										/* Right after first long ring */
	4,										/* Right after long part */
	3,										/* After third chirp */
	2,										/* Second spell */
};

#define NUM_CADENCE (sizeof(cadences) / sizeof(cadences[0]))

#define ISTRUNK(p) ((p->sig == SIG_FXSLS) || (p->sig == SIG_FXSKS) || \
			(p->sig == SIG_FXSGS) || (p->sig == SIG_PRI))

#define CANBUSYDETECT(p) (ISTRUNK(p) || (p->sig & SIG_EM) /* || (p->sig & __ZT_SIG_FXO) */)
#define CANPROGRESSDETECT(p) (ISTRUNK(p) || (p->sig & SIG_EM) /* || (p->sig & __ZT_SIG_FXO) */)

#if 0
/* return non-zero if clear dtmf is appropriate */
static int CLEARDTMF(struct ast_channel *chan) {
struct zt_pvt *p = chan->pvt->pvt,*themp;
struct ast_channel *them;
	if (!p)
		return 0;
	  /* if not in a 3 way, we should be okay */
	if (p->thirdcallindex == -1) return 1;
	  /* get the other side of the call's channel pointer */
	if (p->owners[p->normalindex] == chan)
		them = p->owners[p->thirdcallindex];
	else
		them = p->owners[p->normalindex];
	if (!them)
		return 0;
	if (!them->bridge) return 1;
	  /* get their private structure, too */
	themp = them->pvt->pvt;
	  /* if does not use zt bridge code, return 0 */
	if (them->pvt->bridge != zt_bridge) return 0;
	if (them->bridge->pvt->bridge != zt_bridge) return 0;
	return 1; /* okay, I guess we are okay to be clear */
}
#endif

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
				bi.numbufs = 4;
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
	p->subs[x].curconfno = -1;
	return 0;
}

static int zt_digit(struct ast_channel *ast, char digit)
{
	ZT_DIAL_OPERATION zo;
	struct zt_pvt *p;
	int res = 0;
	int index;
	p = ast->pvt->pvt;

	index = zt_get_index(ast, p, 0);
	if (index == SUB_REAL) {
		zo.op = ZT_DIAL_OP_APPEND;
		zo.dialstr[0] = 'T';
		zo.dialstr[1] = digit;
		zo.dialstr[2] = 0;
		if ((res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &zo)))
			ast_log(LOG_WARNING, "Couldn't dial digit %c\n", digit);
		else
			p->dialing = 1;
	}
	
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
		"Pulse Start"
};
 
static char *event2str(int event)
{
        static char buf[256];
        if ((event < 15) && (event > -1))
                return events[event];
        sprintf(buf, "Event %d", event);
        return buf;
}

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

static char *sig2str(int sig)
{
	static char buf[256];
	switch(sig) {
	case SIG_EM:
		return "E & M Immediate";
	case SIG_EMWINK:
		return "E & M Wink";
	case SIG_FEATD:
		return "Feature Group D (DTMF)";
	case SIG_FEATDMF:
		return "Feature Group D (MF)";
	case SIG_FEATB:
		return "Feature Group B (MF)";
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
	case 0:
		return "Pseudo Signalling";
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d", sig);
		return buf;
	}
}

static int conf_add(int *confno, struct zt_subchannel *c, int index)
{
	/* If the conference already exists, and we're already in it
	   don't bother doing anything */
	ZT_CONFINFO zi;
	if ((*confno > 0) && (c->curconfno == *confno))
		return 0; 
	if (c->curconfno > 0) {
		ast_log(LOG_WARNING, "Subchannel %d is already in conference %d, moving to %d\n", c->zfd, c->curconfno, *confno);
	}
	if (c->zfd < 0)
		return 0;
	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;
	zi.confno = *confno;
	if (!index) {
		/* Real-side and pseudo-side both participate in conference */
		zi.confmode = ZT_CONF_REALANDPSEUDO | ZT_CONF_TALKER | ZT_CONF_LISTENER |
							ZT_CONF_PSEUDO_TALKER | ZT_CONF_PSEUDO_LISTENER;
	} else
		zi.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
	if (ioctl(c->zfd, ZT_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to add %d to conference %d\n", c->zfd, *confno);
		return -1;
	}
	c->curconfno = zi.confno;
	*confno = zi.confno;
	ast_log(LOG_DEBUG, "Added %d to conference %d\n", c->zfd, *confno);
	return 0;
}

static int conf_del(int *confno, struct zt_subchannel *c, int index)
{
	ZT_CONFINFO zi;
		/* Can't delete from this conference if it's not 0 */
	if ((*confno < 1) ||
		/* Can't delete if there's no zfd */
		(c->zfd < 0) ||
		/* Don't delete from the conference if it's not our conference */
		(*confno != c->curconfno) 
		/* Don't delete if we don't think it's conferenced at all (implied) */
		) return 0;
	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;
	zi.confno = 0;
	zi.confmode = 0;
	if (ioctl(c->zfd, ZT_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to drop %d from conference %d\n", c->zfd, *confno);
		return -1;
	}
	c->curconfno = -1;
	ast_log(LOG_DEBUG, "Removed %d from conference %d\n", c->zfd, *confno);
	return 0;
}

static int update_conf(struct zt_pvt *p)
{
	int needconf = 0;
	int x;
	/* Update conference state in a stateless fashion */
	/* Start with the obvious, general stuff */
	for (x=0;x<3;x++) {
		if ((p->subs[x].zfd > -1) && p->subs[x].inthreeway) {
			conf_add(&p->confno, &p->subs[x], x);
			needconf++;
		} else {
			conf_del(&p->confno, &p->subs[x], x);
		}
	}
	/* If we have a slave, add him to our conference now */
	for (x=0;x<MAX_SLAVES;x++) {
		if (p->slaves[x]) {
			conf_add(&p->confno, &p->slaves[x]->subs[SUB_REAL], SUB_REAL);
			needconf++;
		}
	}
	/* If we're supposed to be in there, do so now */
	if (p->inconference && !p->subs[SUB_REAL].inthreeway) {
		conf_add(&p->confno, &p->subs[SUB_REAL], SUB_REAL);
		needconf++;
	}
	/* If we have a master, add ourselves to his conference */
	if (p->master) 
		conf_add(&p->master->confno, &p->subs[SUB_REAL], SUB_REAL);
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
	if (p->echocanon) {
		ast_log(LOG_DEBUG, "Echo cancellation already on\n");
		return;
	}
	if (p && p->echocancel) {
		x = p->echocancel;
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		else {
			p->echocanon = 1;
			ast_log(LOG_DEBUG, "Enabled echo cancellation on channel %d\n", p->channel);
		}
	} else
		ast_log(LOG_DEBUG, "No echocancellation requested\n");
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

int set_actual_gain(int fd, int chan, float rxgain, float txgain, int law)
{
	struct	zt_gains g;
	float ltxgain;
	float lrxgain;
	int j,k;
	g.chan = chan;
	  /* caluculate linear value of tx gain */
	ltxgain = pow(10.0,txgain / 20.0);
	  /* caluculate linear value of rx gain */
	lrxgain = pow(10.0,rxgain / 20.0);
	if (law == ZT_LAW_ALAW) {
		for (j=0;j<256;j++) {
			k = (int)(((float)AST_ALAW(j)) * lrxgain);
			if (k > 32767) k = 32767;
			if (k < -32767) k = -32767;
			g.rxgain[j] = AST_LIN2A(k);
			k = (int)(((float)AST_ALAW(j)) * ltxgain);
			if (k > 32767) k = 32767;
			if (k < -32767) k = -32767;
			g.txgain[j] = AST_LIN2A(k);
		}
	} else {
		for (j=0;j<256;j++) {
			k = (int)(((float)AST_MULAW(j)) * lrxgain);
			if (k > 32767) k = 32767;
			if (k < -32767) k = -32767;
			g.rxgain[j] = AST_LIN2MU(k);
			k = (int)(((float)AST_MULAW(j)) * ltxgain);
			if (k > 32767) k = 32767;
			if (k < -32767) k = -32767;
			g.txgain[j] = AST_LIN2MU(k);
		}
	}
	  /* set 'em */
	return(ioctl(fd,ZT_SETGAINS,&g));
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
	int x, res;
	x = muted;
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
		p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, p->callwaitcid, AST_LAW(p));
		/* Make sure we account for the end */
		p->cidlen += READ_SIZE * 4;
		p->cidpos = 0;
		send_callerid(p);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "CPE supports Call Waiting Caller*ID.  Sending '%s'\n", p->callwaitcid);
	} else return -1;
	return 0;
}

static int has_voicemail(struct zt_pvt *p)
{

	return ast_app_has_voicemail(p->mailbox);
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
	struct zt_pvt *p = ast->pvt->pvt;
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
	struct zt_pvt *p = ast->pvt->pvt;
	int x, res, index;
	char *c, *n, *l;
	char *s;
	char callerid[256];
	char dest[256];
	strncpy(dest, rdest, sizeof(dest) - 1);
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "zt_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	p->dialednone = 0;
	if (p->radio)  /* if a radio channel, up immediately */
	{
		/* Special pseudo -- automatically up */
		ast_setstate(ast, AST_STATE_UP); 
		return 0;
	}
	x = ZT_FLUSH_READ | ZT_FLUSH_WRITE;
	res = ioctl(p->subs[SUB_REAL].zfd, ZT_FLUSH, &x);
	if (res)
		ast_log(LOG_WARNING, "Unable to flush input on channel %d\n", p->channel);
	p->outgoing = 1;

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
					p->cidlen = ast_callerid_generate(p->cidspill, ast->callerid, AST_LAW(p));
					p->cidpos = 0;
					send_callerid(p);
				} else
					ast_log(LOG_WARNING, "Unable to generate CallerID spill\n");
			}
			/* Select proper cadence */
			if ((p->distinctivering > 0) && (p->distinctivering <= NUM_CADENCE)) {
				if (ioctl(p->subs[SUB_REAL].zfd, ZT_SETCADENCE, &cadences[p->distinctivering-1]))
					ast_log(LOG_WARNING, "Unable to set distinctive ring cadence %d on '%s'\n", p->distinctivering, ast->name);
				p->cidrings = cidrings[p->distinctivering - 1];
			} else {
				if (ioctl(p->subs[SUB_REAL].zfd, ZT_SETCADENCE, NULL))
					ast_log(LOG_WARNING, "Unable to reset default ring on '%s'\n", ast->name);
				p->cidrings = 1;
			}
			x = ZT_RING;
			if (ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x) && (errno != EINPROGRESS)) {
				ast_log(LOG_WARNING, "Unable to ring phone: %s\n", strerror(errno));
				return -1;
			}
			p->dialing = 1;
		} else {
			/* Call waiting call */
			p->callwaitrings = 0;
			if (ast->callerid)
				strncpy(p->callwaitcid, ast->callerid, sizeof(p->callwaitcid)-1);
			else
				strcpy(p->callwaitcid, "");
			/* Call waiting tone instead */
			if (zt_callwait(ast))
				return -1;
			/* Make ring-back */
			if (tone_zone_play_tone(p->subs[SUB_CALLWAIT].zfd, ZT_TONE_RINGTONE))
				ast_log(LOG_WARNING, "Unable to generate call-wait ring-back on channel %s\n", ast->name);
				
		}
		if (ast->callerid) 
			strncpy(callerid, ast->callerid, sizeof(callerid)-1);
		else
			strcpy(callerid, "");
		ast_callerid_parse(callerid, &n, &l);
		if (l) {
			ast_shrink_phone_number(l);
			if (!ast_isphonenumber(l))
				l = NULL;
		}
		if (l)
			strcpy(p->lastcallerid, l);
		else
			strcpy(p->lastcallerid, "");
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
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_FEATB:
		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = dest;
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			return -1;
		}
		x = ZT_START;
		/* Start the trunk */
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
		if (res < 0) {
			if (errno != EINPROGRESS) {
				ast_log(LOG_WARNING, "Unable to start channel: %s\n", strerror(errno));
				return -1;
			}
		}
		ast_log(LOG_DEBUG, "Dialing '%s'\n", c);
		p->dop.op = ZT_DIAL_OP_REPLACE;
		if (p->sig == SIG_FEATD) {
			if (ast->callerid) {
				strncpy(callerid, ast->callerid, sizeof(callerid)-1);
				ast_callerid_parse(callerid, &n, &l);
				if (l) {
					ast_shrink_phone_number(l);
					if (!ast_isphonenumber(l))
						l = NULL;
				}
			} else
				l = NULL;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T*%s*%s*", l, c + p->stripmsd);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T**%s*", c + p->stripmsd);
		} else 
		if (p->sig == SIG_FEATDMF) {
			if (ast->callerid) {
				strncpy(callerid, ast->callerid, sizeof(callerid)-1);
				ast_callerid_parse(callerid, &n, &l);
				if (l) {
					ast_shrink_phone_number(l);
					if (!ast_isphonenumber(l))
						l = NULL;
				}
			} else
				l = NULL;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*00%s#*%s#", l, c + p->stripmsd);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*02#*%s#", c + p->stripmsd);
		} else 
		if (p->sig == SIG_FEATB) {
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s#", c + p->stripmsd);
		} else 
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%s", c + p->stripmsd);
		if (!res) {
			if (ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop)) {
				x = ZT_ONHOOK;
				ioctl(p->subs[SUB_REAL].zfd, ZT_HOOK, &x);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
				return -1;
			}
		} else
			ast_log(LOG_DEBUG, "Deferring dialing...\n");
		p->dialing = 1;
		if (strlen(c + p->stripmsd) < 1) p->dialednone = 1;
		ast_setstate(ast, AST_STATE_DIALING);
		break;
#ifdef ZAPATA_PRI
	case SIG_PRI:
		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = dest;
		if (ast->callerid) {
			strncpy(callerid, ast->callerid, sizeof(callerid)-1);
			ast_callerid_parse(callerid, &n, &l);
			if (l) {
				ast_shrink_phone_number(l);
				if (!ast_isphonenumber(l))
					l = NULL;
			}
		} else
			l = NULL;
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			return -1;
		}
		p->dop.op = ZT_DIAL_OP_REPLACE;
		s = strchr(c + p->stripmsd, 'w');
		if (s) {
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%s", s);
			*s = '\0';
		} else {
			strcpy(p->dop.dialstr, "");
		}
		if (pri_call(p->pri->pri, p->call, p->digital ? PRI_TRANS_CAP_DIGITAL : PRI_TRANS_CAP_SPEECH, 
			p->prioffset, p->pri->nodetype == PRI_NETWORK ? 0 : 1, 1, l, p->pri->dialplan - 1, n,
			l ? PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN : PRES_NUMBER_NOT_AVAILABLE,
			c + p->stripmsd, p->pri->dialplan - 1, 
			((p->law == ZT_LAW_ALAW) ? PRI_LAYER_1_ALAW : PRI_LAYER_1_ULAW))) {
			ast_log(LOG_WARNING, "Unable to setup call to %s\n", c + p->stripmsd);
			return -1;
		}
		ast_setstate(ast, AST_STATE_DIALING);
		break;
#endif		
	case 0:
		/* Special pseudo -- automatically up*/
		ast_setstate(ast, AST_STATE_UP);
		break;		
	default:
		ast_log(LOG_DEBUG, "not yet implemented\n");
		return -1;
	}
	return 0;
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
			} else {
				iflist = cur->next;
			}
			if (cur->subs[SUB_REAL].zfd > -1) {
				zt_close(cur->subs[SUB_REAL].zfd);
			}
			free(cur);
		}
	} else {
		if (prev) {
			prev->next = cur->next;
		} else {
			iflist = cur->next;
		}
		if (cur->subs[SUB_REAL].zfd > -1) {
			zt_close(cur->subs[SUB_REAL].zfd);
		}
		free(cur);
	}
	return 0;
}


static int zt_hangup(struct ast_channel *ast)
{
	int res;
	int index,x, law;
	static int restore_gains(struct zt_pvt *p);
	struct zt_pvt *p = ast->pvt->pvt;
	struct zt_pvt *tmp = NULL;
	struct zt_pvt *prev = NULL;
	ZT_PARAMS par;

	if (option_debug)
		ast_log(LOG_DEBUG, "zt_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	index = zt_get_index(ast, p, 1);

	restore_gains(p);
	
	if (p->dsp)
		ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);

	x = 0;
	zt_confmute(p, 0);

	ast_log(LOG_DEBUG, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, index, p->subs[SUB_REAL].zfd, p->subs[SUB_CALLWAIT].zfd, p->subs[SUB_THREEWAY].zfd);
	p->ignoredtmf = 0;
	
	if (index > -1) {
		/* Real channel, do some fixup */
		p->subs[index].owner = NULL;
		p->subs[index].needanswer = 0;
		p->subs[index].needringing = 0;
		p->subs[index].linear = 0;
		p->subs[index].needcallerid = 0;
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
				if (p->subs[SUB_REAL].owner->bridge)
					ast_moh_stop(p->subs[SUB_REAL].owner->bridge);
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
				if (p->subs[SUB_THREEWAY].owner && p->subs[SUB_THREEWAY].owner->bridge)
					ast_moh_start(p->subs[SUB_THREEWAY].owner->bridge, NULL);
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
				if (p->subs[SUB_CALLWAIT].owner && p->subs[SUB_CALLWAIT].owner->bridge)
					ast_moh_start(p->subs[SUB_CALLWAIT].owner->bridge, NULL);
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
		if (p->sig == SIG_PRI) {
			if (p->call) {
				if (!pri_grab(p->pri)) {
					res = pri_disconnect(p->pri->pri, p->call, PRI_CAUSE_NORMAL_CLEARING);
					if (p->alreadyhungup) {
						p->call = NULL;
						p->alreadyhungup = 0;
					}
					if (res < 0) 
						ast_log(LOG_WARNING, "pri_disconnect failed\n");
					pri_rel(p->pri);			
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
					res = -1;
				}
			} else
				res = 0;

		} else 
#endif
#ifdef ZAPATA_R2
		if (p->sig == SIG_R2) {
			if (p->hasr2call) {
				mfcr2_DropCall(p->r2, NULL, UC_NORMAL_CLEARING);
				p->hasr2call = 0;
				res = 0;
			} else
				res = 0;

		} else 
#endif
		if (p->sig)
			res = zt_set_hook(p->subs[SUB_REAL].zfd, ZT_ONHOOK);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
			return -1;
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
				if (par.rxisoffhook)
					tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
				else
					tone_zone_play_tone(p->subs[SUB_REAL].zfd, -1);
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
		strcpy(p->rdnis, "");
		update_conf(p);
		restart_monitor();
	}


	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	ast->pvt->pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	ast_pthread_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	ast_pthread_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);

	ast_pthread_mutex_lock(&iflock);
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
	ast_pthread_mutex_unlock(&iflock);
	return 0;
}

static int zt_answer(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int res=0;
	int index;
	int oldstate = ast->_state;
	ast_setstate(ast, AST_STATE_UP);
	index = zt_get_index(ast, p, 0);
	if (index < 0)
		index = SUB_REAL;
	/* nothing to do if a radio channel */
	if (p->radio)
		return 0;
	switch(p->sig) {
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
		p->ringt = 0;
		/* Fall through */
	case SIG_EM:
	case SIG_EMWINK:
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_FEATB:
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Pick up the line */
		ast_log(LOG_DEBUG, "Took %s off hook\n", ast->name);
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
		break;
#ifdef ZAPATA_PRI
	case SIG_PRI:
		/* Send a pri acknowledge */
		if (!pri_grab(p->pri)) {
			res = pri_answer(p->pri->pri, p->call, 0, 1);
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
		return 0;
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		return -1;
	}
	return res;
}

static int zt_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
char	*cp;
int	x;

	struct zt_pvt *p = chan->pvt->pvt;

	
	if ((option != AST_OPTION_TONE_VERIFY) &&
		(option != AST_OPTION_TDD) && (option != AST_OPTION_RELAXDTMF))
	   {
		errno = ENOSYS;
		return -1;
	   }
	cp = (char *)data;
	if ((!cp) || (datalen < 1))
	   {
		errno = EINVAL;
		return -1;
	   }
	switch(option) {
	    case AST_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		switch(*cp) {
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
	    case AST_OPTION_TDD:  /* turn on or off TDD */
		if (!*cp) { /* turn it off */
			ast_log(LOG_DEBUG, "Set option TDD MODE, value: OFF(0) on %s\n",chan->name);
			if (p->tdd) tdd_free(p->tdd);
			p->tdd = 0;
			p->mate = 0;
			break;
		}
		if (*cp == 2)
			ast_log(LOG_DEBUG, "Set option TDD MODE, value: MATE(2) on %s\n",chan->name);
		else ast_log(LOG_DEBUG, "Set option TDD MODE, value: ON(1) on %s\n",chan->name);
		p->mate = 0;
		zt_disable_ec(p);
		/* otherwise, turn it on */
		if (!p->didtdd) { /* if havent done it yet */
			unsigned char mybuf[41000],*buf;
			int size,res,fd,len;
			int index;
			fd_set wfds,efds;
			buf = mybuf;
			memset(buf,0x7f,sizeof(mybuf)); /* set to silence */
			ast_tdd_gen_ecdisa(buf + 16000,16000);  /* put in tone */
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
				FD_ZERO(&wfds);
				FD_ZERO(&efds);
				FD_SET(fd,&wfds);
				FD_SET(fd,&efds);			
				res = select(fd + 1,NULL,&wfds,&efds,NULL);
				if (!res) {
					ast_log(LOG_DEBUG, "select (for write) ret. 0 on channel %d\n", p->channel);
					continue;
				}
				  /* if got exception */
				if (FD_ISSET(fd,&efds)) return -1;
				if (!FD_ISSET(fd,&wfds)) {
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
		if (!*cp)
		{		
			ast_log(LOG_DEBUG, "Set option RELAX DTMF, value: OFF(0) on %s\n",chan->name);
			x = 0;
		}
		else
		{		
			ast_log(LOG_DEBUG, "Set option RELAX DTMF, value: ON(1) on %s\n",chan->name);
			x = 1;
		}
		ast_dsp_digitmode(p->dsp,x ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF | p->dtmfrelax);
		break;
	}
	errno = 0;
	return 0;
}

static void zt_unlink(struct zt_pvt *slave, struct zt_pvt *master)
{
	/* Unlink a specific slave or all slaves/masters from a given master */
	int x;
	int hasslaves;
	if (!master)
		return;
	hasslaves = 0;
	for (x=0;x<MAX_SLAVES;x++) {
		if (master->slaves[x]) {
			if (!slave || (master->slaves[x] == slave)) {
				/* Take slave out of the conference */
				ast_log(LOG_DEBUG, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
				conf_del(&master->confno, &master->slaves[x]->subs[SUB_REAL], SUB_REAL);
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
			conf_del(&master->master->confno, &master->subs[SUB_REAL], SUB_REAL);
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

static int zt_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct ast_channel *who = NULL, *cs[3];
	struct zt_pvt *p0, *p1, *op0, *op1;
	struct zt_pvt *master=NULL, *slave=NULL;
	struct ast_frame *f;
	int to;
	int inconf = 0;
	int nothingok = 0;
	int ofd1, ofd2;
	int oi1, oi2, i1 = -1, i2 = -1, t1, t2;
	int os1 = -1, os2 = -1;
	struct ast_channel *oc1, *oc2;

	/* if need DTMF, cant native bridge */
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return -2;
	p0 = c0->pvt->pvt;
	p1 = c1->pvt->pvt;
	/* cant do pseudo-channels here */
	if ((!p0->sig) || (!p1->sig)) return -2;

	ast_pthread_mutex_lock(&c0->lock);
	ast_pthread_mutex_lock(&c1->lock);
	op0 = p0 = c0->pvt->pvt;
	op1 = p1 = c1->pvt->pvt;
	ofd1 = c0->fds[0];
	ofd2 = c1->fds[0];
	oi1 = zt_get_index(c0, p0, 0);
	oi2 = zt_get_index(c1, p1, 0);
	oc1 = p0->owner;
	oc2 = p1->owner;
	if ((oi1 < 0) || (oi2 < 0))
		return -1;



	ast_pthread_mutex_lock(&p0->lock);
	if (pthread_mutex_trylock(&p1->lock)) {
		/* Don't block, due to potential for deadlock */
		ast_pthread_mutex_unlock(&p0->lock);
		ast_pthread_mutex_unlock(&c0->lock);
		ast_pthread_mutex_unlock(&c1->lock);
		ast_log(LOG_NOTICE, "Avoiding deadlock...\n");
		return -3;
	}
	if ((oi1 == SUB_REAL) && (oi2 == SUB_REAL)) {
		if (!p0->owner || !p1->owner) {
			/* Currently unowned -- Do nothing.  */
			nothingok = 1;
		} else {
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
				ast_log(LOG_WARNING, "p0: chan %d/%d/CW%d/3W%d, p1: chan %d/%d/CW%d/3W%d\n", p0->channel, oi1, (p0->subs[SUB_CALLWAIT].zfd > -1) ? 1 : 0, p0->subs[SUB_REAL].inthreeway,
						p0->channel, oi1, (p1->subs[SUB_CALLWAIT].zfd > -1) ? 1 : 0, p1->subs[SUB_REAL].inthreeway);
			}
		}
	} else if ((oi1 == SUB_REAL) && (oi2 == SUB_THREEWAY)) {
		if (p1->subs[SUB_THREEWAY].inthreeway) {
			master = p1;
			slave = p0;
		} else {
			nothingok = 1;
		}
	} else if ((oi1 == SUB_THREEWAY) && (oi2 == SUB_REAL)) {
		if (p0->subs[SUB_THREEWAY].inthreeway) {
			master = p0;
			slave = p1;
		} else {
			nothingok  = 1;
		}
	} else if ((oi1 == SUB_REAL) && (oi2 == SUB_CALLWAIT)) {
		/* We have a real and a call wait.  If we're in a three way call, put us in it, otherwise, 
		   don't put us in anything */
		if (p1->subs[SUB_CALLWAIT].inthreeway) {
			master = p1;
			slave = p0;
		} else {
			nothingok = 1;
		}
	} else if ((oi1 == SUB_CALLWAIT) && (oi2 == SUB_REAL)) {
		/* Same as previous */
		if (p0->subs[SUB_CALLWAIT].inthreeway) {
			master = p0;
			slave = p1;
		} else {
			nothingok = 1;
		}
	}
	ast_log(LOG_DEBUG, "master: %d, slave: %d, nothingok: %d\n",
		master ? master->channel : 0, slave ? slave->channel : 0, nothingok);
	if (master && slave) {
		/* Stop any tones, or play ringtone as appropriate.  If they're bridged
		   in an active threeway call with a channel that is ringing, we should
		   indicate ringing. */
		if ((oi2 == SUB_THREEWAY) && 
			p1->subs[SUB_THREEWAY].inthreeway && 
			p1->subs[SUB_REAL].owner && 
			p1->subs[SUB_REAL].inthreeway && 
			(p1->subs[SUB_REAL].owner->_state == AST_STATE_RINGING)) {
				ast_log(LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c0->name, c1->name);
				tone_zone_play_tone(p0->subs[oi1].zfd, ZT_TONE_RINGTONE);
				os2 = p1->subs[SUB_REAL].owner->_state;
		} else {
				ast_log(LOG_DEBUG, "Stoping tones on %d/%d talking to %d/%d\n", p0->channel, oi1, p1->channel, oi2);
				tone_zone_play_tone(p0->subs[oi1].zfd, -1);
		}
		if ((oi1 == SUB_THREEWAY) && 
			p0->subs[SUB_THREEWAY].inthreeway && 
			p0->subs[SUB_REAL].owner && 
			p0->subs[SUB_REAL].inthreeway && 
			(p0->subs[SUB_REAL].owner->_state == AST_STATE_RINGING)) {
				ast_log(LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c1->name, c0->name);
				tone_zone_play_tone(p1->subs[oi2].zfd, ZT_TONE_RINGTONE);
				os1 = p0->subs[SUB_REAL].owner->_state;
		} else {
				ast_log(LOG_DEBUG, "Stoping tones on %d/%d talking to %d/%d\n", p1->channel, oi2, p0->channel, oi1);
				tone_zone_play_tone(p1->subs[oi1].zfd, -1);
		}
		if ((oi1 == SUB_REAL) && (oi2 == SUB_REAL)) {
			if (!p0->echocanbridged || !p1->echocanbridged) {
				/* Disable echo cancellation if appropriate */
				zt_disable_ec(p0);
				zt_disable_ec(p1);
			}
		}
		zt_link(slave, master);
		master->inconference = inconf;
	} else if (!nothingok)
		ast_log(LOG_WARNING, "Can't link %d/%s with %d/%s\n", p0->channel, subnames[oi1], p1->channel, subnames[oi2]);

	update_conf(p0);
	update_conf(p1);
	t1 = p0->subs[SUB_REAL].inthreeway;
	t2 = p1->subs[SUB_REAL].inthreeway;

	ast_pthread_mutex_unlock(&p0->lock);
	ast_pthread_mutex_unlock(&p1->lock);

	ast_pthread_mutex_unlock(&c0->lock);
	ast_pthread_mutex_unlock(&c1->lock);

	/* Native bridge failed */
	if ((!master || !slave) && !nothingok) {
		if (op0 == p0)
			zt_enable_ec(p0);
		if (op1 == p1)
			zt_enable_ec(p1);
		return -1;
	}
	
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		/* Here's our main loop...  Start by locking things, looking for private parts, 
		   and then balking if anything is wrong */
		ast_pthread_mutex_lock(&c0->lock);
		ast_pthread_mutex_lock(&c1->lock);
		p0 = c0->pvt->pvt;
		p1 = c1->pvt->pvt;
		if (op0 == p0)
			i1 = zt_get_index(c0, p0, 1);
		if (op1 == p1)
			i2 = zt_get_index(c1, p1, 1);
		ast_pthread_mutex_unlock(&c0->lock);
		ast_pthread_mutex_unlock(&c1->lock);
		if ((op0 != p0) || (op1 != p1) || 
		    (ofd1 != c0->fds[0]) || 
			(ofd2 != c1->fds[0]) ||
		    (p0->subs[SUB_REAL].owner && (os1 > -1) && (os1 != p0->subs[SUB_REAL].owner->_state)) || 
		    (p1->subs[SUB_REAL].owner && (os2 > -1) && (os2 != p1->subs[SUB_REAL].owner->_state)) || 
		    (oc1 != p0->owner) || 
			(oc2 != p1->owner) ||
			(t1 != p0->subs[SUB_REAL].inthreeway) ||
			(t2 != p1->subs[SUB_REAL].inthreeway) ||
			(oi1 != i1) ||
			(oi2 != i2)) {
			if (slave && master)
				zt_unlink(slave, master);
			ast_log(LOG_DEBUG, "Something changed out on %d/%d to %d/%d, returning -3 to restart\n",
									op0->channel, oi1, op1->channel, oi2);
			if (op0 == p0)
				zt_enable_ec(p0);
			if (op1 == p1)
				zt_enable_ec(p1);
			return -3;
		}
		to = -1;
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			continue;
		}
		if (who->pvt->pvt == op0) 
			op0->ignoredtmf = 1;
		else if (who->pvt->pvt == op1)
			op1->ignoredtmf = 1;
		f = ast_read(who);
		if (who->pvt->pvt == op0) 
			op0->ignoredtmf = 0;
		else if (who->pvt->pvt == op1)
			op1->ignoredtmf = 0;
		if (!f) {
			*fo = NULL;
			*rc = who;
			if (slave && master)
				zt_unlink(slave, master);
			if (op0 == p0)
				zt_enable_ec(p0);
			if (op1 == p1)
				zt_enable_ec(p1);
			return 0;
		}
		if (f->frametype == AST_FRAME_DTMF) {
			if (((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) || 
			    ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1))) {
				*fo = f;
				*rc = who;
				if (slave && master)
					zt_unlink(slave, master);
				return 0;
			} else if ((who == c0) && p0->pulsedial) {
				ast_write(c1, f);
			} else if ((who == c1) && p1->pulsedial) {
				ast_write(c0, f);
			}
		}
		ast_frfree(f);

		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
}

static int zt_indicate(struct ast_channel *chan, int condition);

static int zt_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct zt_pvt *p = newchan->pvt->pvt;
	int x;
	ast_log(LOG_DEBUG, "New owner for channel %d is %s\n", p->channel, newchan->name);
	if (p->owner == oldchan)
		p->owner = newchan;
	for (x=0;x<3;x++)
		if (p->subs[x].owner == oldchan) {
			if (!x)
				zt_unlink(NULL, p);
			p->subs[x].owner = newchan;
		}
	if (newchan->_state == AST_STATE_RINGING) 
		zt_indicate(newchan, AST_CONTROL_RINGING);
	update_conf(p);
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

static struct ast_channel *zt_new(struct zt_pvt *, int, int, int, int);

static int attempt_transfer(struct zt_pvt *p)
{
	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if (p->subs[SUB_REAL].owner->bridge) {
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if (p->subs[SUB_THREEWAY].owner->bridge)
			ast_moh_stop(p->subs[SUB_THREEWAY].owner->bridge);
		if (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_RINGING) {
			ast_indicate(p->subs[SUB_REAL].owner->bridge, AST_CONTROL_RINGING);
		}
		if (ast_channel_masquerade(p->subs[SUB_THREEWAY].owner, p->subs[SUB_REAL].owner->bridge)) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					p->subs[SUB_REAL].owner->bridge->name, p->subs[SUB_THREEWAY].owner->name);
			return -1;
		}
		/* Orphan the channel */
		unalloc_sub(p, SUB_THREEWAY);
	} else if (p->subs[SUB_THREEWAY].owner->bridge) {
		if (p->subs[SUB_REAL].owner->_state == AST_STATE_RINGING) {
			ast_indicate(p->subs[SUB_THREEWAY].owner->bridge, AST_CONTROL_RINGING);
		}
		ast_moh_stop(p->subs[SUB_THREEWAY].owner->bridge);
		if (ast_channel_masquerade(p->subs[SUB_REAL].owner, p->subs[SUB_THREEWAY].owner->bridge)) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					p->subs[SUB_THREEWAY].owner->bridge->name, p->subs[SUB_REAL].owner->name);
			return -1;
		}
		swap_subs(p, SUB_THREEWAY, SUB_REAL);
		unalloc_sub(p, SUB_THREEWAY);
		/* Tell the caller not to hangup */
		return 1;
	} else {
		ast_log(LOG_DEBUG, "Neither %s nor %s are in a bridge, nothing to transfer\n",
					p->subs[SUB_REAL].owner->name, p->subs[SUB_THREEWAY].owner->name);
		p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
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

static struct ast_frame *zt_handle_event(struct ast_channel *ast)
{
	int res,x;
	int index;
	struct zt_pvt *p = ast->pvt->pvt;
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
	res = zt_get_event(p->subs[index].zfd);
	ast_log(LOG_DEBUG, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, index);
	if (res & (ZT_EVENT_PULSEDIGIT | ZT_EVENT_DTMFDIGIT)) {
		if (res & ZT_EVENT_PULSEDIGIT)
			p->pulsedial = 1;
		else
			p->pulsedial = 0;
		ast_log(LOG_DEBUG, "Pulse dial '%c'\n", res & 0xff);
		p->subs[index].f.frametype = AST_FRAME_DTMF;
		p->subs[index].f.subclass = res & 0xff;
		/* Return the captured digit */
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
				p->dialing = 0;
				if (ast->_state == AST_STATE_DIALING) {
					if (p->callprogress && CANPROGRESSDETECT(p) && p->dsp) {
						ast_log(LOG_DEBUG, "Done dialing, but waiting for progress detection before doing more...\n");
					} else if (p->confirmanswer || (!p->dialednone && ((p->sig == SIG_EM) || (p->sig == SIG_EMWINK) || (p->sig == SIG_FEATD) || (p->sig == SIG_FEATDMF) || (p->sig == SIG_FEATB)))) {
						ast_setstate(ast, AST_STATE_RINGING);
					} else {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[index].f.frametype = AST_FRAME_CONTROL;
						p->subs[index].f.subclass = AST_CONTROL_ANSWER;
					}
				}
			}
			break;
		case ZT_EVENT_ALARM:
			p->inalarm = 1;
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
						zt_ring_phone(p);
					} else if (p->subs[SUB_THREEWAY].owner) {
						if ((ast->pbx) ||
							(ast->_state == AST_STATE_UP)) {
							if (p->transfer) {
								/* In any case this isn't a threeway call anymore */
								p->subs[SUB_REAL].inthreeway = 0;
								p->subs[SUB_THREEWAY].inthreeway = 0;
								if ((res = attempt_transfer(p)) < 0)
									p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
								else if (res) {
									/* Don't actually hang up at this point */
									break;
								}
							} else
								p->subs[SUB_THREEWAY].owner->_softhangup |= AST_SOFTHANGUP_DEV;
						} else {
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
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				switch(ast->_state) {
				case AST_STATE_RINGING:
					zt_enable_ec(p);
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
						/* Ignore answer if "confirm answer" is selected */
						p->subs[index].f.frametype = AST_FRAME_NULL;
						p->subs[index].f.subclass = 0;
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
					if (p->owner->bridge)
							ast_moh_stop(p->owner->bridge);
					break;
				default:
					ast_log(LOG_WARNING, "FXO phone off hook in weird state %d??\n", ast->_state);
				}
				break;
			case SIG_FXSLS:
			case SIG_FXSGS:
			case SIG_FXSKS:
				if (ast->_state == AST_STATE_RING) {
					p->ringt = RINGT;
				}
				/* Fall through */
			case SIG_EM:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_FEATDMF:
			case SIG_FEATB:
				if (ast->_state == AST_STATE_DOWN) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Ring detected\n");
					p->subs[index].f.frametype = AST_FRAME_CONTROL;
					p->subs[index].f.subclass = AST_CONTROL_RING;
				} else if (ast->_state == AST_STATE_RINGING) {
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
				ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
			}
			break;
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
			break;
		case ZT_EVENT_WINKFLASH:
			if (p->inalarm) break;
			if (p->radio) break;
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				ast_log(LOG_DEBUG, "Winkflash, index: %d, normal: %d, callwait: %d, thirdcall: %d\n",
					index, p->subs[SUB_REAL].zfd, p->subs[SUB_CALLWAIT].zfd, p->subs[SUB_THREEWAY].zfd);
				p->callwaitcas = 0;
				if (index == SUB_REAL) {
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
						if (!p->subs[SUB_CALLWAIT].inthreeway && p->subs[SUB_CALLWAIT].owner->bridge)
								ast_moh_start(p->subs[SUB_CALLWAIT].owner->bridge, NULL);
						if (p->subs[SUB_REAL].owner->bridge)
								ast_moh_stop(p->subs[SUB_REAL].owner->bridge);
					} else if (!p->subs[SUB_THREEWAY].owner) {
						if (p->threewaycalling) {
							/* XXX This section needs much more error checking!!! XXX */
							/* Start a 3-way call if feasible */
							if ((ast->pbx) ||
									(ast->_state == AST_STATE_UP) ||
									(ast->_state == AST_STATE_RING)) {
								if (!alloc_sub(p, SUB_THREEWAY)) {
									/* Make new channel */
									chan = zt_new(p, AST_STATE_RESERVED, 0, SUB_THREEWAY, 0);
									/* Swap things around between the three-way and real call */
									swap_subs(p, SUB_THREEWAY, SUB_REAL);
									/* Disable echo canceller for better dialing */
									zt_disable_ec(p);
									res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_DIALRECALL);
									if (res)
										ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
									p->owner = chan;
									if (pthread_create(&threadid, &attr, ss_thread, chan)) {
										ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
										res = tone_zone_play_tone(p->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
										zt_enable_ec(p);
										ast_hangup(chan);
									} else {
										if (option_verbose > 2)	
											ast_verbose(VERBOSE_PREFIX_3 "Started three way call on channel %d\n", p->channel);
										/* Start music on hold if appropriate */
										if (p->subs[SUB_THREEWAY].owner->bridge)
											ast_moh_start(p->subs[SUB_THREEWAY].owner->bridge, NULL);
									}		
								} else
									ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
							} else 
								ast_log(LOG_DEBUG, "Flash when call not up or ringing\n");
						}
					} else {
						/* Already have a 3 way call */
						if (p->subs[SUB_THREEWAY].inthreeway) {
							/* Call is already up, drop the last person */
							if (option_debug)
								ast_log(LOG_DEBUG, "Got flash with three way call up, dropping last call on %d\n", p->channel);
							/* If the primary call isn't answered yet, use it */
							if ((p->subs[SUB_REAL].owner->_state != AST_STATE_UP) && (p->subs[SUB_THREEWAY].owner->_state == AST_STATE_UP)) {
								/* Swap back -- we're droppign the real 3-way that isn't finished yet*/
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
							if ((ast->pbx) ||
									(ast->_state == AST_STATE_UP)) {
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
								if (p->subs[otherindex].owner && p->subs[otherindex].owner->bridge)
									ast_moh_stop(p->subs[otherindex].owner->bridge);
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
								if (p->subs[SUB_REAL].owner && p->subs[SUB_REAL].owner->bridge)
									ast_moh_stop(p->subs[SUB_REAL].owner->bridge);
							}
							
						}
					}
				} else {
					ast_log(LOG_WARNING, "Got flash hook with index %d on channel %d?!?\n", index, p->channel);
				}
				update_conf(p);
				break;
			case SIG_EM:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_FXSLS:
			case SIG_FXSGS:
				if (p->dialing)
					ast_log(LOG_DEBUG, "Ignoring wink on channel %d\n", p->channel);
				else
					ast_log(LOG_DEBUG, "Got wink in weird state %d on channel %d\n", ast->_state, p->channel);
				break;
			case SIG_FEATDMF:
			case SIG_FEATB:
				/* FGD MF *Must* wait for wink */
				res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop);
				if (res < 0) {
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
			case SIG_EMWINK:
			case SIG_FEATD:
				res = ioctl(p->subs[SUB_REAL].zfd, ZT_DIAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else 
					ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
				p->dop.dialstr[0] = '\0';
				break;
			case SIG_FEATDMF:
			case SIG_FEATB:
				ast_log(LOG_DEBUG, "Got hook complete in MF FGD, waiting for wink now on channel %d\n",p->channel);
				break;
			default:
				break;
			}
			break;
		default:
			ast_log(LOG_DEBUG, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[index].f;
 }

struct ast_frame *zt_exception(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int res;
	int usedindex=-1;
	int index;

	index = zt_get_index(ast, p, 1);
	
	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.subclass = 0;
	p->subs[index].f.src = "zt_exception";
	p->subs[index].f.data = NULL;
	
	
	if ((!p->owner) && (!p->radio)) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		res = zt_get_event(p->subs[SUB_REAL].zfd);
		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != ZT_EVENT_RINGEROFF) && (res != ZT_EVENT_RINGERON) &&
			(res != ZT_EVENT_HOOKCOMPLETE)) {
			ast_log(LOG_DEBUG, "Restoring owner of channel %d on event %d\n", p->channel, res);
			p->owner = p->subs[SUB_REAL].owner;
			if (p->owner && p->owner->bridge)
				ast_moh_stop(p->owner->bridge);
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
			}
			break;
		case ZT_EVENT_HOOKCOMPLETE:
		case ZT_EVENT_RINGERON:
		case ZT_EVENT_RINGEROFF:
			/* Do nothing */
			break;
		case ZT_EVENT_WINKFLASH:
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
				if (p->owner->bridge)
					ast_moh_stop(p->owner->bridge);
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		return &p->subs[index].f;
	}
	if (!p->radio) ast_log(LOG_DEBUG, "Exception on %d, channel %d\n", ast->fds[0],p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast->name, p->owner->name);
		return &p->subs[index].f;
	}
	return zt_handle_event(ast);
}

struct ast_frame  *zt_read(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int res;
	int index;
	void *readbuf;
	struct ast_frame *f;
	

	ast_pthread_mutex_lock(&p->lock);
	
	index = zt_get_index(ast, p, 0);
	
	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.subclass = 0;
	p->subs[index].f.src = "zt_read";
	p->subs[index].f.data = NULL;
	
	/* Hang up if we don't really exist */
	if (index < 0)	{
		ast_log(LOG_WARNING, "We dont exist?\n");
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	
	/* make sure it sends initial key state as first frame */
	if (p->radio && (!p->firstradio))
	{
		ZT_PARAMS ps;

		ps.channo = p->channel;
		if (ioctl(p->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &ps) < 0)
			return NULL;
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
		pthread_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}
	if (p->ringt == 1) {
		pthread_mutex_unlock(&p->lock);
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
		pthread_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}

	if (p->subs[index].needcallerid) {
		ast_set_callerid(ast, strlen(p->lastcallerid) ? p->lastcallerid : NULL, 1);
		p->subs[index].needcallerid = 0;
	}
	
	if (p->subs[index].needanswer) {
		/* Send ringing frame if requested */
		p->subs[index].needanswer = 0;
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_ANSWER;
		ast_setstate(ast, AST_STATE_UP);
		pthread_mutex_unlock(&p->lock);
		return &p->subs[index].f;
	}	
	
	if (ast->pvt->rawreadformat == AST_FORMAT_SLINEAR) {
		if (!p->subs[index].linear) {
			p->subs[index].linear = 1;
			res = zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
			if (res) 
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, index);
		}
	} else if ((ast->pvt->rawreadformat == AST_FORMAT_ULAW) ||
		   (ast->pvt->rawreadformat == AST_FORMAT_ALAW)) {
		if (p->subs[index].linear) {
			p->subs[index].linear = 0;
			res = zt_setlinear(p->subs[index].zfd, p->subs[index].linear);
			if (res) 
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to campanded mode.\n", p->channel, index);
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %d\n", ast->pvt->rawreadformat);
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->subs[index].buffer) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	res = read(p->subs[index].zfd, readbuf, p->subs[index].linear ? READ_SIZE * 2 : READ_SIZE);
	ast->blocking = 0;
	/* Check for hangup */
	if (res < 0) {
		if (res == -1)  {
			if (errno == EAGAIN) {
				/* Return "NULL" frame if there is nobody there */
				pthread_mutex_unlock(&p->lock);
				return &p->subs[index].f;
			} else
				ast_log(LOG_WARNING, "zt_rec: %s\n", strerror(errno));
		}
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	if (res != (p->subs[index].linear ? READ_SIZE * 2 : READ_SIZE)) {
		ast_log(LOG_DEBUG, "Short read (%d/%d), must be an event...\n", res, p->subs[index].linear ? READ_SIZE * 2 : READ_SIZE);
		pthread_mutex_unlock(&p->lock);
		return zt_handle_event(ast);
	}
	if (p->tdd) { /* if in TDD mode, see if we receive that */
		int c;

		c = tdd_feed(p->tdd,readbuf,READ_SIZE);
		if (c < 0) {
			ast_log(LOG_DEBUG,"tdd_feed failed\n");
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
			pthread_mutex_unlock(&p->lock);
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
	p->subs[index].f.subclass = ast->pvt->rawreadformat;
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
	if (p->dsp && !p->ignoredtmf && !index) {
		/* Perform busy detection. etc on the zap line */
		f = ast_dsp_process(ast, p->dsp, &p->subs[index].f, 0);
		if (f) {
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_BUSY)) {
				if ((ast->_state == AST_STATE_UP) && !p->outgoing) {
					/* Treat this as a "hangup" instead of a "busy" on the assumption that
					   a busy  */
					f = NULL;
				}
			} else if (f->frametype == AST_FRAME_DTMF) {
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
			ast_setstate(ast, AST_STATE_UP);
			f = &p->subs[index].f;
		} else if (p->callwaitcas) {
			if ((f->subclass == 'A') || (f->subclass == 'D')) {
				ast_log(LOG_DEBUG, "Got some DTMF, but it's for the CAS\n");
				if (p->cidspill)
					free(p->cidspill);
				send_cwcidspill(p);
			}
			p->callwaitcas = 0;
			p->subs[index].f.frametype = AST_FRAME_NULL;
			p->subs[index].f.subclass = 0;
			f = &p->subs[index].f;
		} else if (f->subclass == 'f') {
			/* Fax tone -- Handle and return NULL */
			if (!p->faxhandled) {
				p->faxhandled++;
				if (strcmp(ast->exten, "fax")) {
					if (ast_exists_extension(ast, ast->context, "fax", 1, ast->callerid)) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension\n", ast->name);
						if (ast_async_goto(ast, ast->context, "fax", 1, 0))
							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, ast->context);
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
#if 0
	if (f->frametype == AST_FRAME_VOICE && (ast->_state == AST_STATE_UP)) {
		p->subs[index].f.frametype = AST_FRAME_NULL;
		p->subs[index].f.subclass = 0;
		f = &p->subs[index].f;
	}
#endif	
	pthread_mutex_unlock(&p->lock);
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
	struct zt_pvt *p = ast->pvt->pvt;
	int res;
	unsigned char outbuf[4096];
	int index;
	
	index = zt_get_index(ast, p, 0);
	if (index < 0) {
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast->name);
		return -1;
	}
	
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
	struct zt_pvt *p = chan->pvt->pvt;
	int res=-1;
	int index = zt_get_index(chan, p, 0);
	if (index == SUB_REAL) {
		switch(condition) {
		case AST_CONTROL_BUSY:
			res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_BUSY);
			break;
		case AST_CONTROL_RINGING:
			res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_RINGTONE);
			if (chan->_state != AST_STATE_UP) {
				if ((chan->_state != AST_STATE_RING) ||
					((p->sig != SIG_FXSKS) &&
					 (p->sig != SIG_FXSLS) &&
					 (p->sig != SIG_FXSGS)))
					ast_setstate(chan, AST_STATE_RINGING);
			}
			break;
		case AST_CONTROL_CONGESTION:
			res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
			break;
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
		case -1:
			res = tone_zone_play_tone(p->subs[index].zfd, -1);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to set condition %d on channel %s\n", condition, chan->name);
		}
	} else
		res = 0;
	return res;
}

static struct ast_channel *zt_new(struct zt_pvt *i, int state, int startpbx, int index, int law)
{
	struct ast_channel *tmp;
	int deflaw;
	int res;
	int x,y;
	int features;
	ZT_PARAMS ps;
	tmp = ast_channel_alloc(0);
	if (tmp) {
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
		tmp->pvt->rawreadformat = deflaw;
		tmp->readformat = deflaw;
		tmp->pvt->rawwriteformat = deflaw;
		tmp->writeformat = deflaw;
		i->subs[index].linear = 0;
		zt_setlinear(i->subs[index].zfd, i->subs[index].linear);
		features = 0;
		if (i->busydetect && CANBUSYDETECT(i)) {
			features |= DSP_FEATURE_BUSY_DETECT;
		}
		if (i->callprogress && CANPROGRESSDETECT(i)) {
			features |= DSP_FEATURE_CALL_PROGRESS;
		}
		features |= DSP_FEATURE_DTMF_DETECT;
		if (features) {
			if (i->dsp) {
				ast_log(LOG_DEBUG, "Already have a dsp on %s?\n", tmp->name);
			} else {
				i->dsp = ast_dsp_new();
				if (i->dsp) {
					ast_dsp_set_features(i->dsp, features);
					ast_dsp_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
				}
			}
		}
		
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = zt_digit;
		tmp->pvt->send_text = zt_sendtext;
		tmp->pvt->call = zt_call;
		tmp->pvt->hangup = zt_hangup;
		tmp->pvt->answer = zt_answer;
		tmp->pvt->read = zt_read;
		tmp->pvt->write = zt_write;
		tmp->pvt->bridge = zt_bridge;
		tmp->pvt->exception = zt_exception;
		tmp->pvt->indicate = zt_indicate;
		tmp->pvt->fixup = zt_fixup;
		tmp->pvt->setoption = zt_setoption;
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		if (strlen(i->musicclass))
			strncpy(tmp->musicclass, i->musicclass, sizeof(tmp->musicclass)-1);
		if (!i->owner)
			i->owner = tmp;
		if (strlen(i->accountcode))
			strncpy(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode)-1);
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		if (i->subs[index].owner) {
			ast_log(LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[index]);
		}
		i->subs[index].owner = tmp;
		ast_setstate(tmp, state);
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		/* Copy call forward info */
		strncpy(tmp->call_forward, i->call_forward, sizeof(tmp->call_forward));
		/* If we've been told "no ADSI" then enforce it */
		if (!i->adsi)
			tmp->adsicpe = AST_ADSI_UNAVAILABLE;
		if (strlen(i->exten))
			strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
		if (strlen(i->rdnis))
			tmp->rdnis = strdup(i->rdnis);
		if (strlen(i->callerid)) {
			tmp->callerid = strdup(i->callerid);
			tmp->ani = strdup(i->callerid);
		}
#ifdef ZAPATA_PRI
		/* Assume calls are not idle calls unless we're told differently */
		i->isidlecall = 0;
#endif
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


static int bump_gains(struct zt_pvt *p)
{
	int res;
	/* Bump receive gain by 9.0db */
	res = set_actual_gain(p->subs[SUB_REAL].zfd, 0, p->rxgain + 5.0, p->txgain, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to bump gain\n");
		return -1;
	}
	return 0;
}

static int restore_gains(struct zt_pvt *p)
{
	int res;
	/* Bump receive gain by 9.0db */
	res = set_actual_gain(p->subs[SUB_REAL].zfd, 0, p->rxgain, p->txgain, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int my_getsigstr(struct ast_channel *chan, char *str, char term, int ms)
{
char c;

	*str = 0; /* start with empty output buffer */
	for(;;)
	{
		/* Wait for the first digit (up to specified ms). */
		c = ast_waitfordigit(chan,ms);
		/* if timeout, hangup or error, return as such */
		if (c < 1) return(c);
		*str++ = c;
		*str = 0;
		if (c == term) return(1);
	}
}

static void *ss_thread(void *data)
{
	struct ast_channel *chan = data;
	struct zt_pvt *p = chan->pvt->pvt;
	char exten[AST_MAX_EXTENSION];
	char exten2[AST_MAX_EXTENSION];
	unsigned char buf[256];
	char cid[256];
	char dtmfbuf[300];
	struct callerid_state *cs;
	char *name=NULL, *number=NULL;
	int flags;
	int i,j;
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
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_FEATB:
	case SIG_EMWINK:
		zt_set_hook(p->subs[index].zfd, ZT_WINK);
		for(;;)
		{
			   /* set bits of interest */
			j = ZT_IOMUX_SIGEVENT;
			    /* wait for some happening */
			if (ioctl(p->subs[index].zfd,ZT_IOMUX,&j) == -1) return(NULL);
			   /* exit loop if we have it */
			if (j & ZT_IOMUX_SIGEVENT) break;
		}
		  /* get the event info */
		if (ioctl(p->subs[index].zfd,ZT_GETEVENT,&j) == -1) return(NULL);
		/* Fall through */
	case SIG_EM:
		res = tone_zone_play_tone(p->subs[index].zfd, -1);
		if (p->dsp)
			ast_dsp_digitreset(p->dsp);
		/* set digit mode appropriately */
		if (p->dsp) {
			if ((p->sig == SIG_FEATDMF) || (p->sig == SIG_FEATB)) 
				ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_MF | p->dtmfrelax); 
			else 
				ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);
		}
		/* Wait for the first digit (up to 5 seconds). */
		res = ast_waitfordigit(chan,5000);
		if (res > 0) {
			/* save first char */
			dtmfbuf[0] = res;
			switch(p->sig)
			{
			    case SIG_FEATD:
				res = my_getsigstr(chan,dtmfbuf + 1,'*',3000);
				if (res > 0)
					res = my_getsigstr(chan,dtmfbuf + strlen(dtmfbuf),'*',3000);
				if (res < 1) ast_dsp_digitreset(p->dsp);
				break;
			    case SIG_FEATDMF:
				res = my_getsigstr(chan,dtmfbuf + 1,'#',3000);
				if (res > 0)
					res = my_getsigstr(chan,dtmfbuf + strlen(dtmfbuf),'#',3000);
				if (res < 1) ast_dsp_digitreset(p->dsp);
				break;
			    case SIG_FEATB:
				res = my_getsigstr(chan,dtmfbuf + 1,'#',3000);
				if (res < 1) ast_dsp_digitreset(p->dsp);
				break;
			    default:
				/* If we got it, get the rest */
				res = my_getsigstr(chan,dtmfbuf + 1,' ',250);
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
		strncpy(exten, dtmfbuf, sizeof(exten)-1);
		if (!strlen(exten))
			strncpy(exten, "s", sizeof(exten)-1);
		if (p->sig == SIG_FEATD) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				strncpy(exten2, exten, sizeof(exten2)-1);
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "*");
				s2 = strsep(&stringp, "*");
				if (s2) {
					if (strlen(p->callerid))
						chan->callerid = strdup(p->callerid);
					else
						chan->callerid = strdup(s1);
					if (chan->callerid)
						chan->ani = strdup(chan->callerid);
					strncpy(exten, s2, sizeof(exten)-1);
				} else
					strncpy(exten, s1, sizeof(exten)-1);
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_FEATDMF) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				strncpy(exten2, exten, sizeof(exten2)-1);
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2) {
					if (strlen(p->callerid))
						chan->callerid = strdup(p->callerid);
					else
						if (*(s1 + 2)) chan->callerid = strdup(s1 + 2);
					if (chan->callerid)
						chan->ani = strdup(chan->callerid);
					strncpy(exten, s2 + 1, sizeof(exten)-1);
				} else
					strncpy(exten, s1 + 2, sizeof(exten)-1);
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_FEATB) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				strncpy(exten2, exten, sizeof(exten2)-1);
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				strncpy(exten, exten2 + 1, sizeof(exten)-1);
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group B input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		zt_enable_ec(p);
		if (ast_exists_extension(chan, chan->context, exten, 1, chan->callerid)) {
			strncpy(chan->exten, exten, sizeof(chan->exten)-1);
			ast_dsp_digitreset(p->dsp);
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
		while(len < AST_MAX_EXTENSION-1) {
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
			if (ast_exists_extension(chan, chan->context, exten, 1, p->callerid)) {
				if (!res || !ast_matchmore_extension(chan, chan->context, exten, 1, p->callerid)) {
					if (getforward) {
						/* Record this as the forwarding extension */
						strncpy(p->call_forward, exten, sizeof(p->call_forward)); 
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
						strncpy(chan->exten, exten, sizeof(chan->exten)-1);
						if (strlen(p->callerid)) {
							if (!p->hidecallerid)
								chan->callerid = strdup(p->callerid);
							chan->ani = strdup(p->callerid);
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
					
			} else if (!strcmp(exten,"*8#")){
				/* Scan all channels and see if any there
				 * ringing channqels with that have call groups
				 * that equal this channels pickup group  
				 */
				struct zt_pvt *chan_pvt=iflist;
				while(chan_pvt!=NULL){
					if((p!=chan_pvt) &&
					  (p->pickupgroup & chan_pvt->callgroup) &&
					  (chan_pvt->owner && (chan_pvt->owner->_state==AST_STATE_RING || chan_pvt->owner->_state == AST_STATE_RINGING)) &&
					  chan_pvt->dialing
					  ){
					  	if (index == SUB_REAL) {
						  	if (p->subs[SUB_THREEWAY].owner) {
								/* If you make a threeway call and the *8# a call, it should actually 
								   look like a callwait */
								alloc_sub(p, SUB_CALLWAIT);
							  	swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
								unalloc_sub(p, SUB_THREEWAY);
							}
							/* Switch us from Third call to Call Wait */
							ast_log(LOG_DEBUG, "Call pickup on chan %s\n",chan_pvt->owner->name);
							p->subs[index].needanswer=1;
							zt_enable_ec(p);
							if(ast_channel_masquerade(chan_pvt->owner,p->owner))
								printf("Error Masquerade failed on call-pickup\n");
							ast_hangup(p->owner);
						} else {
							ast_log(LOG_WARNING, "Huh?  Got *8# on call not on real\n");
							ast_hangup(p->owner);
						}
						return NULL;
					}
					chan_pvt=chan_pvt->next;
				}
				ast_log(LOG_DEBUG, "No call pickup possible...\n");
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_CONGESTION);
				zt_wait_event(p->subs[index].zfd);
				ast_hangup(chan);
				return NULL;
			} else if (!p->hidecallerid && !strcmp(exten, "*67")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Disabling Caller*ID on %s\n", chan->name);
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				if (chan->callerid)
					free(chan->callerid);
				chan->callerid = NULL;
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
				if (strlen(p->lastcallerid)) {
					res = ast_say_digit_str(chan, p->lastcallerid, "", chan->language);
				}
				if (!res)
					res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				break;
			} else if (!strcmp(exten, "*78")) {
				/* Do not disturb */
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Enabled DND on channel %d\n", p->channel);
				res = tone_zone_play_tone(p->subs[index].zfd, ZT_TONE_DIALRECALL);
				p->dnd = 1;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (!strcmp(exten, "*79")) {
				/* Do not disturb */
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Disabled DND on channel %d\n", p->channel);
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
			} else if (p->transfer && !strcmp(exten, ast_parking_ext()) && 
						p->subs[SUB_THREEWAY].owner &&
						p->subs[SUB_THREEWAY].owner->bridge) {
				/* This is a three way call, the main call being a real channel, 
					and we're parking the first call. */
				ast_masq_park_call(p->subs[SUB_THREEWAY].owner->bridge, chan, 0, NULL);
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Parking call to '%s'\n", chan->name);
				break;
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Enabling Caller*ID on %s\n", chan->name);
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				if (chan->callerid)
					free(chan->callerid);
				if (strlen(p->callerid))
					chan->callerid = strdup(p->callerid);
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
				if (nbridge && nbridge->bridge) pbridge = nbridge->bridge->pvt->pvt;
				if (nbridge && 
				    (!strcmp(nbridge->type,"Zap")) &&
				    ISTRUNK(pbridge)) {
					int func = ZT_FLASH;
					/* flash hookswitch */
					if ((ioctl(pbridge->subs[SUB_REAL].zfd,ZT_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
						ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n", 
							nbridge->name, strerror(errno));
					}
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
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
			} else if (!ast_canmatch_extension(chan, chan->context, exten, 1, chan->callerid) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Can't match %s from '%s' in context %s\n", exten, chan->callerid ? chan->callerid : "<Unknown Caller>", chan->context);
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
		if (p->use_callerid) {
			cs = callerid_new();
			if (cs) {
#if 1
				bump_gains(p);
#endif				
				len = 0;
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
						if (p->ringt) 
							p->ringt--;
						if (p->ringt == 1) {
							res = -1;
							break;
						}
						res = callerid_feed(cs, buf, res, AST_LAW(p));
						if (res < 0) {
							ast_log(LOG_WARNING, "CallerID feed failed: %s\n", strerror(errno));
							break;
						} else if (res)
							break;
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
		if (name && number) {
			snprintf(cid, sizeof(cid), "\"%s\" <%s>", name, number);
		} else if (name) {
			snprintf(cid, sizeof(cid), "\"%s\"", name);
		} else if (number) {
			snprintf(cid, sizeof(cid), "%s", number);
		} else {
			strcpy(cid, "");
		}
		if (strlen(cid)) {
			chan->callerid = strdup(cid);
			chan->ani = strdup(cid);
		}
		ast_setstate(chan, AST_STATE_RING);
		chan->rings = 1;
		p->ringt = RINGT;
		zt_enable_ec(p);
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
			chan = zt_new(i, AST_STATE_RING, 0, SUB_REAL, 0);
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
	if (i->radio) return 0;
	/* Handle an event on a given channel for the monitor thread. */
	switch(event) {
	case ZT_EVENT_NONE:
	case ZT_EVENT_BITSCHANGED:
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
		/* Got a ring/answer.  What kind of channel are we? */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			if (i->cidspill) {
				/* Cancel VMWI spill */
				free(i->cidspill);
				i->cidspill = NULL;
			}
			if (i->immediate) {
				zt_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_RINGTONE);
				chan = zt_new(i, AST_STATE_RING, 1, SUB_REAL, 0);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = zt_new(i, AST_STATE_DOWN, 0, SUB_REAL, 0);
				if (chan) {
					if (has_voicemail(i))
						res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_DIALRECALL);
					else
						res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_DIALTONE);
					if (res < 0) 
						ast_log(LOG_WARNING, "Unable to play dialtone on channel %d\n", i->channel);
					if (pthread_create(&threadid, &attr, ss_thread, chan)) {
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
				i->ringt = RINGT;
				/* Fall through */
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_FEATB:
		case SIG_EM:
				/* Check for callerid, digits, etc */
				chan = zt_new(i, AST_STATE_RING, 0, SUB_REAL, 0);
				if (pthread_create(&threadid, &attr, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, ZT_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					ast_hangup(chan);
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
		break;
	case ZT_EVENT_ALARM:
		i->inalarm = 1;
		/* fall thru intentionally */
	case ZT_EVENT_ONHOOK:
		/* Back on hook.  Hang up. */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_FEATB:
		case SIG_EM:
		case SIG_EMWINK:
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
			zt_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, -1);
			zt_set_hook(i->subs[SUB_REAL].zfd, ZT_ONHOOK);
			break;
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
		default:
			ast_log(LOG_WARNING, "Don't know how to handle on hook with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].zfd, -1);
			return -1;
		}
		break;
	}
	return 0;
}

static void *do_monitor(void *data)
{
	fd_set efds;
	fd_set rfds;
	int n, res, res2;
	struct zt_pvt *i;
	struct zt_pvt *last = NULL;
	struct timeval tv;
	time_t thispass = 0, lastpass = 0;
	int found;
	char buf[1024];
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
		if (ast_pthread_mutex_lock(&iflock)) {
			ast_log(LOG_ERROR, "Unable to grab interface lock\n");
			return NULL;
		}
		/* Build the stuff we're going to select on, that is the socket of every
		   zt_pvt that does not have an associated owner channel */
		n = -1;
		FD_ZERO(&efds);
		FD_ZERO(&rfds);
		i = iflist;
		while(i) {
			if ((i->subs[SUB_REAL].zfd > -1) && i->sig && (!i->radio)) {
				if (FD_ISSET(i->subs[SUB_REAL].zfd, &efds)) 
					ast_log(LOG_WARNING, "Descriptor %d appears twice?\n", i->subs[SUB_REAL].zfd);
				if (!i->owner && !i->subs[SUB_REAL].owner) {
					/* This needs to be watched, as it lacks an owner */
					FD_SET(i->subs[SUB_REAL].zfd, &efds);
					/* Message waiting or r2 channels also get watched for reading */
#ifdef ZAPATA_R2
					if (i->cidspill || i->r2)
#else					
					if (i->cidspill)
#endif					
						FD_SET(i->subs[SUB_REAL].zfd, &rfds);
					if (i->subs[SUB_REAL].zfd > n)
						n = i->subs[SUB_REAL].zfd;
				}
			}
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_pthread_mutex_unlock(&iflock);
		
		pthread_testcancel();
		/* Wait at least a second for something to happen */
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		res = select(n + 1, &rfds, NULL, &efds, &tv);
		pthread_testcancel();
		/* Okay, select has finished.  Let's see what happened.  */
		if (res < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "select return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (ast_pthread_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}
		found = 0;
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
						if (!last->cidspill && !last->owner && strlen(last->mailbox) && (thispass - last->onhooktime > 3) &&
							(last->sig & __ZT_SIG_FXO)) {
#if 0
							printf("Channel %d has mailbox %s\n", last->channel, last->mailbox);
#endif							
							res = ast_app_has_voicemail(last->mailbox);
							if (last->msgstate != res) {
								int x;
								ast_log(LOG_DEBUG, "Message status for %s changed from %d to %d on %d\n", last->mailbox, last->msgstate, res, last->channel);
								x = ZT_FLUSH_WRITE;
								res2 = ioctl(last->subs[SUB_REAL].zfd, ZT_FLUSH, &x);
								if (res2)
									ast_log(LOG_WARNING, "Unable to flush input on channel %d\n", last->channel);
								last->cidspill = malloc(8192);
								if (last->cidspill) {
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
			if ((i->subs[SUB_REAL].zfd > -1) && i->sig && (!i->radio)) {
				if (FD_ISSET(i->subs[SUB_REAL].zfd, &rfds)) {
					if (i->owner || i->subs[SUB_REAL].owner) {
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
					handle_init_event(i, res);
				}
#ifdef ZAPATA_R2
				if (FD_ISSET(i->subs[SUB_REAL].zfd, &efds) || (i->r2 && !i->sigchecked)) 
#else				
				if (FD_ISSET(i->subs[SUB_REAL].zfd, &efds)) 
#endif				
				{
					if (i->owner || i->subs[SUB_REAL].owner) {
						ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d)...\n", i->subs[SUB_REAL].zfd);
						i = i->next;
						continue;
					}
					res = zt_get_event(i->subs[SUB_REAL].zfd);
					if (option_debug)
						ast_log(LOG_DEBUG, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
					handle_init_event(i, res);
				}
			}
			i=i->next;
		}
		ast_pthread_mutex_unlock(&iflock);
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
	if (monitor_thread == -2)
		return 0;
	if (ast_pthread_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_pthread_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread) {
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
		if (pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0) {
			ast_pthread_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
#if 0
	printf("Created thread %ld detached in restart monitor\n", monitor_thread);
#endif
	ast_pthread_mutex_unlock(&monlock);
	return 0;
}

static int reset_channel(struct zt_pvt *p)
{
	int ioctlflag = 1;
	int res = 0;
	int i = 0;

	ast_log(LOG_DEBUG, "reset_channel()\n");
	if (p->owner) {
		ioctlflag = 0;
		p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
	}
	for (i = 0; i < 3; i++) {
		if (p->subs[i].owner) {
			ioctlflag = 0;
			p->subs[i].owner->_softhangup |= AST_SOFTHANGUP_DEV;
		}
	}
	if (ioctlflag) {
		res = zt_set_hook(p->subs[SUB_REAL].zfd, ZT_ONHOOK);
		if (res < 0) {
			ast_log(LOG_ERROR, "Unable to hangup chan_zap channel %d (ioctl)\n", p->channel);
			return -1;
		}
	}

	return 0;
}


static struct zt_pvt *mkintf(int channel, int signalling, int radio)
{
	/* Make a zt_pvt structure for this interface */
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
	ZT_PARAMS p;

	tmp2 = iflist;
	prev = NULL;

	while (tmp2) {
		if (tmp2->channel == channel) {
			tmp = tmp2;
			here = 1;
			break;
		}
		if (tmp2->channel > channel) {
			break;
		}
		prev = tmp2;
		tmp2 = tmp2->next;
	}

	if (!here) {
		tmp = (struct zt_pvt*)malloc(sizeof(struct zt_pvt));
		if (!tmp) {
			ast_log(LOG_ERROR, "MALLOC FAILED\n");
			return NULL;
		}
		memset(tmp, 0, sizeof(struct zt_pvt));
		for (x=0;x<3;x++)
			tmp->subs[x].zfd = -1;
		tmp->next = tmp2;
		if (!prev) {
			iflist = tmp;
		} else {
			prev->next = tmp;
		}
	}

	if (tmp) {
		if (channel != CHAN_PSEUDO) {
			snprintf(fn, sizeof(fn), "%d", channel);
			/* Open non-blocking */
			if (!here)
				tmp->subs[SUB_REAL].zfd = zt_open(fn);
			/* Allocate a zapata structure */
			if (tmp->subs[SUB_REAL].zfd < 0) {
				ast_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
				free(tmp);
				return NULL;
			}
			memset(&p, 0, sizeof(p));
			res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to get parameters\n");
				free(tmp);
				return NULL;
			}
			if (p.sigtype != (signalling & 0xffff)) {
				ast_log(LOG_ERROR, "Signalling requested is %s but line is in %s signalling\n", sig2str(signalling), sig2str(p.sigtype));
				free(tmp);
				return NULL;
			}
			if (here) {
				if (tmp->sig != signalling) {
					if (reset_channel(tmp)) {
						ast_log(LOG_ERROR, "Failed to reset chan_zap channel %d\n", tmp->channel);
						return NULL;
					}
				}
			}
			tmp->law = p.curlaw;
			tmp->span = p.spanno;
			span = p.spanno - 1;
		} else {
			signalling = 0;
		}
#ifdef ZAPATA_PRI
		if (signalling == SIG_PRI) {
			int offset;
			int numchans;
			int dchannel;
			offset = 1;
			if (ioctl(tmp->subs[SUB_REAL].zfd, ZT_AUDIOMODE, &offset)) {
				ast_log(LOG_ERROR, "Unable to set audio mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
				return NULL;
			}
			if (span >= NUM_SPANS) {
				ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
				free(tmp);
				return NULL;
			} else {
				si.spanno = 0;
				if (ioctl(tmp->subs[SUB_REAL].zfd,ZT_SPANSTAT,&si) == -1) {
					ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
					free(tmp);
					return NULL;
				} 
				if (si.totalchans == 31) { /* if it's an E1 */
					dchannel = 16;
					numchans = 31;
				} else {
					dchannel = 24;
					numchans = 23;
				}
				offset = p.chanpos;
				if (offset != dchannel) {
					if (pris[span].nodetype && (pris[span].nodetype != pritype)) {
						ast_log(LOG_ERROR, "Span %d is already a %s node\n", span + 1, pri_node2str(pris[span].nodetype));
						free(tmp);
						return NULL;
					}
					if (pris[span].switchtype && (pris[span].switchtype != switchtype)) {
						ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, pri_switch2str(pris[span].switchtype));
						free(tmp);
						return NULL;
					}
					if ((pris[span].dialplan) && (pris[span].dialplan != dialplan)) {
						ast_log(LOG_ERROR, "Span %d is already a %s dialing plan\n", span + 1, pri_plan2str(pris[span].dialplan));
						free(tmp);
						return NULL;
					}
					if (strlen(pris[span].idledial) && strcmp(pris[span].idledial, idledial)) {
						ast_log(LOG_ERROR, "Span %d already has idledial '%s'.\n", span + 1, idledial);
						free(tmp);
						return NULL;
					}
					if (strlen(pris[span].idleext) && strcmp(pris[span].idleext, idleext)) {
						ast_log(LOG_ERROR, "Span %d already has idleext '%s'.\n", span + 1, idleext);
						free(tmp);
						return NULL;
					}
					if (pris[span].minunused && (pris[span].minunused != minunused)) {
						ast_log(LOG_ERROR, "Span %d already has minunused of %d.\n", span + 1, minunused);
						free(tmp);
						return NULL;
					}
					if (pris[span].minidle && (pris[span].minidle != minidle)) {
						ast_log(LOG_ERROR, "Span %d already has minidle of %d.\n", span + 1, minidle);
						free(tmp);
						return NULL;
					}
					pris[span].nodetype = pritype;
					pris[span].switchtype = switchtype;
					pris[span].dialplan = dialplan;
					pris[span].chanmask[offset] |= MASK_AVAIL;
					pris[span].pvt[offset] = tmp;
					pris[span].channels = numchans;
					pris[span].dchannel = dchannel;
					pris[span].minunused = minunused;
					pris[span].minidle = minidle;
					strncpy(pris[span].idledial, idledial, sizeof(pris[span].idledial) - 1);
					strncpy(pris[span].idleext, idleext, sizeof(pris[span].idleext) - 1);
					
					tmp->pri = &pris[span];
					tmp->prioffset = offset;
					tmp->call = NULL;
				} else {
					ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", offset);
					free(tmp);
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
				free(tmp);
				return NULL;
			}
		} else {
			if (tmp->r2) 
				mfcr2_free(tmp->r2);
			tmp->r2 = NULL;
		}
#endif
		/* Adjust starttime on loopstart and kewlstart trunks to reasonable values */
		if ((signalling == SIG_FXSKS) || (signalling == SIG_FXSLS) ||
		    (signalling == SIG_EM) || (signalling == SIG_EMWINK) ||
			(signalling == SIG_FEATD) || (signalling == SIG_FEATDMF) ||
			  (signalling == SIG_FEATB)) {
			p.starttime = 250;
			res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters\n");
				free(tmp);
				return NULL;
			}
		}
		if (radio)
		{
			p.channo = channel;
			p.rxwinktime = 1;
			p.rxflashtime = 1;
			p.starttime = 1;
			p.debouncetime = 5;
			res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters\n");
				free(tmp);
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
				bi.numbufs = 4;
				res = ioctl(tmp->subs[SUB_REAL].zfd, ZT_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", channel);
				}
			} else
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", channel);
		}
#endif
		tmp->immediate = immediate;
		tmp->sig = signalling;
		tmp->radio = radio;
		tmp->firstradio = 0;
		if ((signalling == SIG_FXOKS) || (signalling == SIG_FXOLS) || (signalling == SIG_FXOGS))
			tmp->permcallwaiting = callwaiting;
		else
			tmp->permcallwaiting = 0;
		/* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
		tmp->destroy = 0;
		tmp->callwaitingcallerid = callwaitingcallerid;
		tmp->threewaycalling = threewaycalling;
		tmp->adsi = adsi;
		tmp->permhidecallerid = hidecallerid;
		tmp->callreturn = callreturn;
		tmp->echocancel = echocancel;
		tmp->echocanbridged = echocanbridged;
		tmp->busydetect = busydetect;
		tmp->callprogress = callprogress;
		tmp->cancallforward = cancallforward;
		tmp->dtmfrelax = relaxdtmf;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->hidecallerid = tmp->permhidecallerid;
		tmp->channel = channel;
		tmp->stripmsd = stripmsd;
		tmp->use_callerid = use_callerid;
		strncpy(tmp->accountcode, accountcode, sizeof(tmp->accountcode)-1);
		tmp->amaflags = amaflags;
		if (!here) {
			tmp->confno = -1;
			tmp->propconfno = -1;
		}
		tmp->transfer = transfer;
		ast_pthread_mutex_init(&tmp->lock);
		strncpy(tmp->language, language, sizeof(tmp->language)-1);
		strncpy(tmp->musicclass, musicclass, sizeof(tmp->musicclass)-1);
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		strncpy(tmp->callerid, callerid, sizeof(tmp->callerid)-1);
		strncpy(tmp->mailbox, mailbox, sizeof(tmp->mailbox)-1);
		tmp->msgstate = -1;
		tmp->group = cur_group;
		tmp->callgroup=cur_callergroup;
		tmp->pickupgroup=cur_pickupgroup;
		tmp->rxgain = rxgain;
		tmp->txgain = txgain;
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
			tmp->inalarm = 0;
			memset(&si, 0, sizeof(si));
			if (ioctl(tmp->subs[SUB_REAL].zfd,ZT_SPANSTAT,&si) == -1) {
				ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
				free(tmp);
				return NULL;
			}
			if (si.alarms) tmp->inalarm = 1;
		}
	}
	return tmp;
}

static inline int available(struct zt_pvt *p, int channelmatch, int groupmatch)
{
	int res;
	ZT_PARAMS par;
	/* First, check group matching */
	if ((p->group & groupmatch) != groupmatch)
		return 0;
	/* Check to see if we have a channel match */
	if ((channelmatch > 0) && (p->channel != channelmatch))
		return 0;
	/* If do not distrub, definitely not */
	if (p->dnd)
		return 0;	
		
	/* If no owner definitely available */
	if (!p->owner) {
		/* Trust PRI */
#ifdef ZAPATA_PRI
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
		if ((p->sig == SIG_FXSKS) || (p->sig == SIG_FXSLS) ||
			(p->sig == SIG_FXSGS) || !p->sig)
			return 1;
		if (!p->radio)
		{
			/* Check hook state */
			res = ioctl(p->subs[SUB_REAL].zfd, ZT_GET_PARAMS, &par);
			if (res) {
				ast_log(LOG_WARNING, "Unable to check hook state on channel %d\n", p->channel);
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
		(p->owner->_state != AST_STATE_RINGING)) {
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
		p->subs[SUB_REAL].zfd = zt_open("/dev/zap/pseudo");
		/* Allocate a zapata structure */
		if (p->subs[SUB_REAL].zfd < 0) {
			ast_log(LOG_ERROR, "Unable to dup channel: %s\n",  strerror(errno));
			free(p);
			return NULL;
		}
		res = ioctl(p->subs[SUB_REAL].zfd, ZT_GET_BUFINFO, &bi);
		if (!res) {
			bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
			bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
			bi.numbufs = 4;
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
	

static struct ast_channel *zt_request(char *type, int format, void *data)
{
	int oldformat;
	int groupmatch = 0;
	int channelmatch = -1;
	int callwait = 0;
	struct zt_pvt *p;
	struct ast_channel *tmp = NULL;
	char *dest=NULL;
	int x;
	char *s;
	char opt=0;
	int res=0, y=0;
	
	/* We do signed linear */
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR | AST_FORMAT_ULAW);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	if (data) {
		dest = strdup((char *)data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	if (dest[0] == 'g') {
		/* Retrieve the group number */
		char *stringp=NULL;
		stringp=dest + 1;
		s = strsep(&stringp, "/");
		if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", (char *)data);
			free(dest);
			return NULL;
		}
		groupmatch = 1 << x;
	} else {
		char *stringp=NULL;
		stringp=dest;
		s = strsep(&stringp, "/");
		if (!strcasecmp(s, "pseudo")) {
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
		} else if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			ast_log(LOG_WARNING, "Unable to determine channel for data %s\n", (char *)data);
			free(dest);
			return NULL;
		}
		channelmatch = x;
	}
	/* Search for an unowned channel */
	if (ast_pthread_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p && !tmp) {
		if (available(p, channelmatch, groupmatch)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Using channel %d\n", p->channel);
				if (p->inalarm) {
					p = p->next;
					continue;
				}
#ifdef ZAPATA_PRI
			if (p->pri) 
				if (!(p->call = pri_new_call(p->pri->pri))) {
					ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
					break;
				}
#endif
			callwait = (p->owner != NULL);
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
			tmp = zt_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0);
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
				} else {
					ast_log(LOG_WARNING, "Unknown option '%c' in '%s'\n", opt, (char *)data);
				}
			}
			/* Note if the call is a call waiting call */
			if (callwait)
				tmp->cdrflags |= AST_CDR_CALLWAIT;
			break;
		}
		p = p->next;
	}
	ast_pthread_mutex_unlock(&iflock);
	restart_monitor();
	return tmp;
}


static int get_group(char *s)
{
	char *copy;
	char *piece;
	char *c=NULL;
	int start=0, finish=0,x;
	int group = 0;
	copy = strdup(s);
	if (!copy) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return 0;
	}
	c = copy;
	piece = strsep(&c, ",");
	while(piece) {
		if (sscanf(piece, "%d-%d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(piece, "%d", &start)) {
			/* Just one */
			finish = start;
		} else {
			ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'.  Using '0'\n", s,piece);
			return 0;
		}
		piece = strsep(&c, ",");
		for (x=start;x<=finish;x++) {
			if ((x > 31) || (x < 0)) {
				ast_log(LOG_WARNING, "Ignoring invalid group %d\n", x);
			} else
				group |= (1 << x);
		}
	}
	free(copy);
	return group;
}

#ifdef ZAPATA_PRI

static int pri_find_empty_chan(struct zt_pri *pri)
{
	int x;
	for (x=pri->channels;x>0;x--) {
		if (pri->pvt[x] && !pri->pvt[x]->owner)
			return x;
	}
	return 0;
}

static int pri_fixup(struct zt_pri *pri, int channel, q931_call *c)
{
	int x;
	if (!c) {
		if (channel < 1)
			return 0;
		return channel;
	}
	for (x=1;x<=pri->channels;x++) {
		if (!pri->pvt[x]) continue;
		if (pri->pvt[x]->call == c) {
			/* Found our call */
			if (channel != x) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Moving call from channel %d to channel %d\n",
						x, channel);
				if (pri->pvt[channel]->owner) {
					ast_log(LOG_WARNING, "Can't fix up channel from %d to %d because %d is already in use\n",
						x, channel, channel);
					return 0;
				}
				/* Fix it all up now */
				pri->pvt[channel]->owner = pri->pvt[x]->owner;
				if (pri->pvt[channel]->owner) {
					pri->pvt[channel]->owner->pvt->pvt = pri->pvt[channel];
					pri->pvt[channel]->owner->fds[0] = pri->pvt[channel]->subs[SUB_REAL].zfd;
				} else
					ast_log(LOG_WARNING, "Whoa, there's no  owner, and we're having to fix up channel %d to channel %d\n", x, channel);
				pri->pvt[channel]->call = pri->pvt[x]->call;
				/* Free up the old channel, now not in use */
				pri->pvt[x]->owner = NULL;
				pri->pvt[x]->call = NULL;
			}
			return channel;
		}
	}
	ast_log(LOG_WARNING, "Call specified, but not found?\n");
	return 0;
}

static void *do_idle_thread(void *vchan)
{
	struct ast_channel *chan = vchan;
	struct zt_pvt *pvt = chan->pvt->pvt;
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
				strncpy(chan->exten, pvt->pri->idleext, sizeof(chan->exten) - 1);
				strncpy(chan->context, pvt->pri->idlecontext, sizeof(chan->context) - 1);
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

static void zt_pri_message(char *s)
{
	ast_verbose(s);
}

static void zt_pri_error(char *s)
{
	ast_log(LOG_WARNING, "PRI: %s", s);
}

static void *pri_dchannel(void *vpri)
{
	struct zt_pri *pri = vpri;
	pri_event *e;
	fd_set efds;
	fd_set rfds;
	int res;
	int chan;
	int x;
	int haveidles;
	int activeidles;
	int nextidle = -1;
	struct ast_channel *c;
	struct timeval tv, *next;
	struct timeval lastidle = { 0, 0 };
	int doidling=0;
	char *cc;
	char idlen[80];
	struct ast_channel *idle;
	pthread_t p;
	time_t t;
	gettimeofday(&lastidle, NULL);
	if (strlen(pri->idledial) && strlen(pri->idleext)) {
		/* Need to do idle dialing, check to be sure though */
		cc = strchr(pri->idleext, '@');
		if (cc) {
			*cc = '\0';
			cc++;
			strncpy(pri->idlecontext, cc, sizeof(pri->idlecontext) - 1);
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
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		FD_SET(pri->fd, &rfds);
		FD_SET(pri->fd, &efds);
		time(&t);
		ast_pthread_mutex_lock(&pri->lock);
		if (pri->resetting && pri->up) {
			/* Look for a resetable channel and go */
			if ((t - pri->lastreset) > 0) {
				pri->lastreset = t;
				do {
					pri->resetchannel++;
				} while((pri->resetchannel <= pri->channels) &&
					(!pri->pvt[pri->resetchannel] ||
					pri->pvt[pri->resetchannel]->call ||
					 pri->pvt[pri->resetchannel]->resetting));
				if (pri->resetchannel <= pri->channels) {
					/* Mark the channel as resetting and restart it */
					pri->pvt[pri->resetchannel]->resetting = 1;
					pri_reset(pri->pri, pri->resetchannel);
				} else {
					pri->resetting = 0;
				}
			}
		} else {
			if ((t - pri->lastreset) >= RESET_INTERVAL) {
				pri->resetting = 1;
				pri->resetchannel = 0;
			}
		}
		/* Look for any idle channels if appropriate */
		if (doidling && pri->up) {
			nextidle = -1;
			haveidles = 0;
			activeidles = 0;
			for (x=pri->channels;x>=0;x--) {
				if (pri->pvt[x] && !pri->pvt[x]->owner && 
				    !pri->pvt[x]->call) {
					if (haveidles < pri->minunused) {
						haveidles++;
					} else if (!pri->pvt[x]->resetting) {
						nextidle = x;
						break;
					}
				} else if (pri->pvt[x] && pri->pvt[x]->owner && pri->pvt[x]->isidlecall)
					activeidles++;
			}
#if 0
			printf("nextidle: %d, haveidles: %d, minunsed: %d\n",
				nextidle, haveidles, minunused);
			gettimeofday(&tv, NULL);
			printf("nextidle: %d, haveidles: %d, ms: %ld, minunsed: %d\n",
				nextidle, haveidles, (tv.tv_sec - lastidle.tv_sec) * 1000 +
				    (tv.tv_usec - lastidle.tv_usec) / 1000, minunused);
#endif
			if (nextidle > -1) {
				gettimeofday(&tv, NULL);
				if (((tv.tv_sec - lastidle.tv_sec) * 1000 +
				    (tv.tv_usec - lastidle.tv_usec) / 1000) > 1000) {
					/* Don't create a new idle call more than once per second */
					snprintf(idlen, sizeof(idlen), "%d/%s", pri->pvt[nextidle]->channel, pri->idledial);
					idle = zt_request("Zap", AST_FORMAT_ULAW, idlen);
					if (idle) {
						pri->pvt[nextidle]->isidlecall = 1;
						if (pthread_create(&p, NULL, do_idle_thread, idle)) {
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
				for (x=pri->channels;x>0;x--) {
					/* find a candidate channel */
					if (pri->pvt[x] && pri->pvt[x]->owner && pri->pvt[x]->isidlecall) {
						pri->pvt[x]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
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
		if ((next = pri_schedule_next(pri->pri))) {
			/* We need relative time here */
			gettimeofday(&tv, NULL);
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
			if (doidling || pri->resetting) {
				if (tv.tv_sec > 1) {
					tv.tv_sec = 1;
					tv.tv_usec = 0;
				}
			} else {
				if (tv.tv_sec > 60) {
					tv.tv_sec = 60;
					tv.tv_usec = 0;
				}
			}
		} else if (doidling || pri->resetting) {
			/* Make sure we stop at least once per second if we're
			   monitoring idle channels */
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		} else {
			/* Don't poll for more than 60 seconds */
			tv.tv_sec = 60;
			tv.tv_usec = 0;
		}
		pthread_mutex_unlock(&pri->lock);

		e = NULL;
		res = select(pri->fd + 1, &rfds, NULL, &efds, &tv);

		ast_pthread_mutex_lock(&pri->lock);
		if (!res) {
			/* Just a timeout, run the scheduler */
			e = pri_schedule_run(pri->pri);
		} else if (res > -1) {
			e = pri_check_event(pri->pri);
		} else if (errno != EINTR)
			ast_log(LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));

		if (e) {
			if (pri->debug)
				pri_dump_event(pri->pri, e);
			switch(e->e) {
			case PRI_EVENT_DCHAN_UP:
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "D-Channel on span %d up\n", pri->span);
				pri->up = 1;
				break;
			case PRI_EVENT_DCHAN_DOWN:
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "D-Channel on span %d down\n", pri->span);
				pri->up = 0;
				break;
			case PRI_EVENT_RESTART:
				chan = e->restart.channel;
				if (chan > -1) {
					if ((chan < 1) || (chan > pri->channels) )
						ast_log(LOG_WARNING, "Restart requested on odd channel number %d on span %d\n", chan, pri->span);
					else if (!pri->pvt[chan])
						ast_log(LOG_WARNING, "Restart requested on unconfigured channel %d on span %d\n", chan, pri->span);
					else {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "B-channel %d restarted on span %d\n", 
								chan, pri->span);
						/* Force soft hangup if appropriate */
						if (pri->pvt[chan]->owner)
							pri->pvt[chan]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
					}
				} else {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_2 "Restart on requested on entire span %d\n", pri->span);
					for (x=1;x <= pri->channels;x++)
						if ((x != pri->dchannel) && pri->pvt[x] && (pri->pvt[x]->owner))
							pri->pvt[x]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
				}
				break;
			case PRI_EVENT_RING:
				chan = e->ring.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					ast_log(LOG_WARNING, "Ring requested on odd channel number %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Ring requested on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (pri->pvt[chan]->owner) {
					if (pri->pvt[chan]->call == e->ring.call) {
						ast_log(LOG_WARNING, "Duplicate setup requested on channel %d already in use on span %d\n", chan, pri->span);
						break;
					} else {
						ast_log(LOG_WARNING, "Ring requested on channel %d already in use on span %d.  Hanging up owner.\n", chan, pri->span);
						pri->pvt[chan]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
						chan = 0;
					}
				}
				if (!chan && (e->ring.flexible))
					chan = pri_find_empty_chan(pri);
				if (chan) {
					/* Get caller ID */
					if (pri->pvt[chan]->use_callerid) {
						if (strlen(e->ring.callingname)) {
							snprintf(pri->pvt[chan]->callerid, sizeof(pri->pvt[chan]->callerid), "\"%s\" <%s>", e->ring.callingname, e->ring.callingnum);
						} else
							strncpy(pri->pvt[chan]->callerid, e->ring.callingnum, sizeof(pri->pvt[chan]->callerid)-1);
					} else
						strcpy(pri->pvt[chan]->callerid, "");
					/* Get called number */
					if (strlen(e->ring.callednum)) {
						strncpy(pri->pvt[chan]->exten, e->ring.callednum, sizeof(pri->pvt[chan]->exten)-1);
					} else
						strcpy(pri->pvt[chan]->exten, "s");
					strncpy(pri->pvt[chan]->rdnis, e->ring.redirectingnum, sizeof(pri->pvt[chan]->rdnis));
					/* Make sure extension exists */
					if (ast_exists_extension(NULL, pri->pvt[chan]->context, pri->pvt[chan]->exten, 1, pri->pvt[chan]->callerid)) {
						/* Setup law */
						int law;
						if (e->ring.layer1 == PRI_LAYER_1_ALAW)
							law = ZT_LAW_ALAW;
						else
							law = ZT_LAW_MULAW;
						res = zt_setlaw(pri->pvt[chan]->subs[SUB_REAL].zfd, law);
						if (res < 0) 
							ast_log(LOG_WARNING, "Unable to set law on channel %d\n", pri->pvt[chan]->channel);
						res = set_actual_gain(pri->pvt[chan]->subs[SUB_REAL].zfd, 0, pri->pvt[chan]->rxgain, pri->pvt[chan]->txgain, law);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to set gains on channel %d\n", pri->pvt[chan]->channel);
						/* Start PBX */
						pri->pvt[chan]->call = e->ring.call;
						c = zt_new(pri->pvt[chan], AST_STATE_RING, 1, SUB_REAL, law);
						if (c) {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Accepting call from '%s' to '%s' on channel %d, span %d\n",
									e->ring.callingnum, pri->pvt[chan]->exten, chan, pri->span);
							pri_acknowledge(pri->pri, e->ring.call, chan, 1);
							zt_enable_ec(pri->pvt[chan]);
						} else {
							ast_log(LOG_WARNING, "Unable to start PBX on channel %d, span %d\n", chan, pri->span);
							pri_release(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
							pri->pvt[chan]->call = 0;
						}
					} else {
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Extension '%s' in context '%s' from '%s' does not exist.  Rejecting call on channel %d, span %d\n", 
								pri->pvt[chan]->exten, pri->pvt[chan]->context, pri->pvt[chan]->callerid, chan, pri->span);
						pri_release(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
					}
				} else 
					pri_release(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
				break;
			case PRI_EVENT_RINGING:
				chan = e->ringing.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					ast_log(LOG_WARNING, "Ringing requested on odd channel number %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Ringing requested on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				}
				if (chan) {
					chan = pri_fixup(pri, chan, e->ringing.call);
					if (!chan) {
						ast_log(LOG_WARNING, "Ringing requested on channel %d not in use on span %d\n", e->ringing.channel, pri->span);
						chan = 0;
					} else if (!strlen(pri->pvt[chan]->dop.dialstr)) {
						pri->pvt[chan]->subs[SUB_REAL].needringing =1;
					} else
						ast_log(LOG_DEBUG, "Deferring ringing notification because of extra digits to dial...\n");
				}
				zt_enable_ec(pri->pvt[chan]);
				break;				
			case PRI_EVENT_FACNAME:
				chan = e->facname.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					ast_log(LOG_WARNING, "Facilty Name requested on odd channel number %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Facility Name requested on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				}
				if (chan) {
					chan = pri_fixup(pri, chan, e->facname.call);
					if (!chan) {
						ast_log(LOG_WARNING, "Facility Name requested on channel %d not in use on span %d\n", e->ringing.channel, pri->span);
						chan = 0;
					} else {
						/* Re-use *69 field for PRI */
						snprintf(pri->pvt[chan]->lastcallerid, sizeof(pri->pvt[chan]->lastcallerid), "\"%s\" <%s>", e->facname.callingname, e->facname.callingnum);
						pri->pvt[chan]->subs[SUB_REAL].needcallerid =1;
					}
				}
				zt_enable_ec(pri->pvt[chan]);
				break;				
			case PRI_EVENT_ANSWER:
				chan = e->answer.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					ast_log(LOG_WARNING, "Answer on odd channel number %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Answer on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				}
				if (chan) {
					chan = pri_fixup(pri, chan, e->answer.call);
					if (!chan) {
						ast_log(LOG_WARNING, "Answer requested on channel %d not in use on span %d\n", chan, pri->span);
						chan = 0;
					} else {
						if (strlen(pri->pvt[chan]->dop.dialstr)) {
							pri->pvt[chan]->dialing = 1;
							/* Send any "w" waited stuff */
							res = ioctl(pri->pvt[chan]->subs[SUB_REAL].zfd, ZT_DIAL, &pri->pvt[chan]->dop);
							if (res < 0) {
								ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", pri->pvt[chan]->channel);
								pri->pvt[chan]->dop.dialstr[0] = '\0';
								return NULL;
							} else 
								ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", pri->pvt[chan]->dop.dialstr);
							pri->pvt[chan]->dop.dialstr[0] = '\0';
						} else
							pri->pvt[chan]->subs[SUB_REAL].needanswer =1;
						/* Enable echo cancellation if it's not on already */
						zt_enable_ec(pri->pvt[chan]);
					}
				}
				break;				
			case PRI_EVENT_HANGUP:
				chan = e->hangup.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					ast_log(LOG_WARNING, "Hangup requested on odd channel number %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Hangup requested on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				}
				if (chan) {
					chan = pri_fixup(pri, chan, e->hangup.call);
					if (chan) {
						if (pri->pvt[chan]->owner) {
							pri->pvt[chan]->alreadyhungup = 1;
							pri->pvt[chan]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Channel %d, span %d got hangup\n", chan, pri->span);
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Forcing restart of channel %d since channel reported in use\n", chan);
							pri_reset(pri->pri, chan);
							pri->pvt[chan]->resetting = 1;
						}
					} else {
						ast_log(LOG_WARNING, "Hangup on bad channel %d\n", e->hangup.channel);
					}
				} 
				break;
			case PRI_EVENT_HANGUP_ACK:
				chan = e->hangup.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					ast_log(LOG_WARNING, "Hangup ACK requested on odd channel number %d span %d\n", chan, pri->span);
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Hangup ACK requested on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				}
				if (chan) {
					chan = pri_fixup(pri, chan, e->hangup.call);
					if (chan) {
						pri->pvt[chan]->call = NULL;
						pri->pvt[chan]->resetting = 0;
						if (pri->pvt[chan]->owner) {
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Channel %d, span %d got hangup ACK\n", chan, pri->span);
						}
					}
				}
				break;
			case PRI_EVENT_CONFIG_ERR:
				ast_log(LOG_WARNING, "PRI Error: %s\n", e->err.err);
				break;
			case PRI_EVENT_RESTART_ACK:
				chan = e->restartack.channel;
				if ((chan < 1) || (chan > pri->channels)) {
					/* Sometime switches (e.g. I421 / British Telecom) don't give us the
					   channel number, so we have to figure it out...  This must be why
					   everybody resets exactly a channel at a time. */
					for (x=1;x<=pri->channels;x++) {
						if (pri->pvt[x] && pri->pvt[x]->resetting) {
							chan = x;
							ast_log(LOG_DEBUG, "Assuming restart ack is really for channel %d span %d\n", chan, pri->span);
							if (pri->pvt[chan]->owner) {
								ast_log(LOG_WARNING, "Got restart ack on channel with owner\n");
								pri->pvt[chan]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
							}
							pri->pvt[chan]->resetting = 0;
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "B-channel %d successfully restarted on span %d\n", chan, pri->span);
							break;
						}
					}
					if ((chan < 1) || (chan > pri->channels)) {
						ast_log(LOG_WARNING, "Restart ACK requested on strange channel %d span %d\n", chan, pri->span);
					}
					chan = 0;
				} else if (!pri->pvt[chan]) {
					ast_log(LOG_WARNING, "Restart ACK requested on unconfigured channel %d span %d\n", chan, pri->span);
					chan = 0;
				}
				if (chan) {
					if (pri->pvt[chan]) {
						if (pri->pvt[chan]->owner) {
							ast_log(LOG_WARNING, "Got restart ack on channel with owner\n");
							pri->pvt[chan]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
						}
						pri->pvt[chan]->resetting = 0;
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "B-channel %d successfully restarted on span %d\n", chan, pri->span);
					}
				}
				break;
			default:
				ast_log(LOG_DEBUG, "Event: %d\n", e->e);
			}
		} else {
			/* Check for an event */
			x = 0;
			res = ioctl(pri->fd, ZT_GETEVENT, &x);
			if (x) 
				printf("PRI got event: %d\n", x);
			if (option_debug)
				ast_log(LOG_DEBUG, "Got event %s (%d) on D-channel for span %d\n", event2str(x), x, pri->span);
		}
		pthread_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

static int start_pri(struct zt_pri *pri)
{
	int res, x;
	ZT_PARAMS p;
	ZT_BUFFERINFO bi;

	pri->fd = open("/dev/zap/channel", O_RDWR, 0600);
	x = pri->offset + pri->dchannel;
	if ((pri->fd < 0) || (ioctl(pri->fd,ZT_SPECIFY,&x) == -1)) {
		ast_log(LOG_ERROR, "Unable to open D-channel %d (%s)\n", x, strerror(errno));
		return -1;
	}

	res = ioctl(pri->fd, ZT_GET_PARAMS, &p);
	if (res) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "Unable to get parameters for D-channel %d (%s)\n", x, strerror(errno));
		return -1;
	}
	if (p.sigtype != ZT_SIG_HDLCFCS) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "D-channel %x is not in HDLC/FCS mode.  See /etc/tormenta.conf\n", x);
		return -1;
	}
	bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.numbufs = 8;
	bi.bufsize = 1024;
	if (ioctl(pri->fd, ZT_SET_BUFINFO, &bi)) {
		ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d\n", x);
		close(pri->fd);
		pri->fd = -1;
		return -1;
	}
	pri->pri = pri_new(pri->fd, pri->nodetype, pri->switchtype);
	if (!pri->pri) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "Unable to create PRI structure\n");
		return -1;
	}
	pri_set_debug(pri->pri, DEFAULT_PRI_DEBUG);
	if (pthread_create(&pri->master, NULL, pri_dchannel, pri)) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static char *complete_span(char *line, char *word, int pos, int state)
{
	int span=1;
	char tmp[50];
	while(span <= NUM_SPANS) {
		if (span > state)
			break;
		span++;
	}
	if (span <= NUM_SPANS) {
		snprintf(tmp, sizeof(tmp), "%d", span);
		return strdup(tmp);
	} else
		return NULL;
}

static int handle_pri_debug(int fd, int argc, char *argv[])
{
	int span;
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
	pri_set_debug(pris[span-1].pri, PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE);
	ast_cli(fd, "Enabled debugging on span %d\n", span);
	return RESULT_SUCCESS;
}


#ifdef ZAPATA_R2
static int handle_r2_no_debug(int fd, int argc, char *argv[])
{
	int chan;
	struct zt_pvt *tmp = NULL;;
	if (argc < 5)
		return RESULT_SHOWUSAGE;
	chan = atoi(argv[4]);
	if ((chan < 1) || (chan > NUM_SPANS)) {
		ast_cli(fd, "Invalid channel %s.  Should be a number greater than 0\n");
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
		ast_cli(fd, "Invalid channel %s.  Should be a number greater than 0\n");
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

static struct ast_cli_entry r2_debug = {
	{ "r2", "debug", "channel", NULL }, handle_r2_debug, "Enables R2 debugging on a channel", r2_debug_help };

static struct ast_cli_entry r2_no_debug = {
	{ "r2", "no", "debug", "channel", NULL }, handle_r2_no_debug, "Disables R2 debugging on a channel", r2_no_debug_help };

#endif

static int handle_pri_no_debug(int fd, int argc, char *argv[])
{
	int span;
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
	pri_set_debug(pris[span-1].pri, 0);
	ast_cli(fd, "Disabled debugging on span %d\n", span);
	return RESULT_SUCCESS;
}

static int handle_pri_really_debug(int fd, int argc, char *argv[])
{
	int span;
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
	pri_set_debug(pris[span-1].pri, (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE));
	ast_cli(fd, "Enabled EXTENSIVE debugging on span %d\n", span);
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

static struct ast_cli_entry pri_debug = {
	{ "pri", "debug", "span", NULL }, handle_pri_debug, "Enables PRI debugging on a span", pri_debug_help, complete_span 
};

static struct ast_cli_entry pri_no_debug = {
	{ "pri", "no", "debug", "span", NULL }, handle_pri_no_debug, "Disables PRI debugging on a span", pri_no_debug_help, complete_span };

static struct ast_cli_entry pri_really_debug = {
	{ "pri", "intense", "debug", "span", NULL }, handle_pri_really_debug, "Enables REALLY INTENSE PRI debugging", pri_really_debug_help, complete_span };

#endif /* ZAPATA_PRI */


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
#define FORMAT "%3d %-10.10s %-10.10s %-10.10s %-8.8s\n"
#define FORMAT2 "%3s %-10.10s %-10.10s %-10.10s %-8.8s\n"
	struct zt_pvt *tmp = NULL;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	ast_pthread_mutex_lock(&iflock);
	ast_cli(fd, FORMAT2, "Chan. Num.", "Extension", "Context", "Language", "MusicOnHold");
	
	tmp = iflist;
	while (tmp) {
		ast_cli(fd, FORMAT, tmp->channel, tmp->exten, tmp->context, tmp->language, tmp->musicclass);
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&iflock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int zap_show_channel(int fd, int argc, char **argv)
{
	int channel;
	struct zt_pvt *tmp = NULL;
	ZT_CONFINFO ci;
	int x;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	channel = atoi(argv[3]);

	ast_pthread_mutex_lock(&iflock);
	tmp = iflist;
	while (tmp) {
		if (tmp->channel == channel) {
			ast_cli(fd, "Channel: %d\n", tmp->channel);
			ast_cli(fd, "File Descriptor: %d\n", tmp->subs[SUB_REAL].zfd);
			ast_cli(fd, "Span: %d\n", tmp->span);
			ast_cli(fd, "Extension: %s\n", tmp->exten);
			ast_cli(fd, "Context: %s\n", tmp->context);
			ast_cli(fd, "Caller ID string: %s\n", tmp->callerid);
			ast_cli(fd, "Destroy: %d\n", tmp->destroy);
			ast_cli(fd, "Signalling Type: %s\n", sig2str(tmp->sig));
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
				ast_cli(fd, "\n");
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
			if (ioctl(tmp->subs[SUB_REAL].zfd, ZT_GETCONF, &ci)) {
				ast_log(LOG_WARNING, "Failed to get conference info on channel %d\n", tmp->channel);
			} else {
				ast_cli(fd, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, ci.confmode);
			}
			ast_pthread_mutex_unlock(&iflock);
			return RESULT_SUCCESS;
		}
		tmp = tmp->next;
	}
	
	ast_cli(fd, "Unable to find given channel %d\n", channel);
	ast_pthread_mutex_unlock(&iflock);
	return RESULT_FAILURE;
}

			

static char show_channels_usage[] =
	"Usage: zap show channels\n"
	"	Shows a list of available channels\n";

static char show_channel_usage[] =
	"Usage: zap show channel <chan num>\n"
	"	Detailed information about a given channel\n";
static char destroy_channel_usage[] =
	"Usage: zap destroy channel <chan num>\n"
	"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";

static struct ast_cli_entry cli_show_channels = { 
	{"zap", "show", "channels", NULL}, zap_show_channels, "Show active zapata channels", show_channels_usage, NULL };

static struct ast_cli_entry cli_show_channel = { 
	{"zap", "show", "channel", NULL}, zap_show_channel, "Show information on a channel", show_channel_usage, NULL };

static struct ast_cli_entry cli_destroy_channel = { 
	{"zap", "destroy", "channel", NULL}, zap_destroy_channel, "Destroy a channel", destroy_channel_usage, NULL };

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct zt_pvt *tmp;
	char *chan;
	char *c;
	int start, finish,x;
	int y;
	int cur_radio = 0;

#ifdef ZAPATA_PRI
	int offset;

	memset(pris, 0, sizeof(pris));
	for (y=0;y<NUM_SPANS;y++) {
		pris[y].offset = -1;
		pris[y].fd = -1;
	}
	pri_set_error(zt_pri_error);
	pri_set_message(zt_pri_message);
#endif
	
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}
	

	if (ast_pthread_mutex_lock(&iflock)) {
		/* It's a little silly to lock it, but we mind as well just to be sure */
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return -1;
	}
	v = ast_variable_browse(cfg, "channels");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "channel")) {
			if (cur_signalling < 0) {
				ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
				ast_destroy(cfg);
				ast_pthread_mutex_unlock(&iflock);
				unload_module();
				return -1;
			}
			c = v->value;
			chan = strsep(&c, ",");
			while(chan) {
				if (sscanf(chan, "%d-%d", &start, &finish) == 2) {
					/* Range */
				} else if (sscanf(chan, "%d", &start)) {
					/* Just one */
					finish = start;
				} else if (!strcasecmp(chan, "pseudo")) {
					finish = start = CHAN_PSEUDO;
				} else {
					ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", v->value, chan);
					ast_destroy(cfg);
					ast_pthread_mutex_unlock(&iflock);
					unload_module();
					return -1;
				}
				if (finish < start) {
					ast_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
					x = finish;
					finish = start;
					start = x;
				}
				for (x=start;x<=finish;x++) {
					tmp = mkintf(x, cur_signalling, cur_radio);
					if (tmp) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Registered channel %d, %s signalling\n", x, sig2str(tmp->sig));
					} else {
						ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
						ast_destroy(cfg);
						ast_pthread_mutex_unlock(&iflock);
						unload_module();
						return -1;
					}
				}
				chan = strsep(&c, ",");
			}
		} else if (!strcasecmp(v->name, "usecallerid")) {
			use_callerid = ast_true(v->value);
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
			strncpy(mailbox, v->value, sizeof(mailbox) -1);
		} else if (!strcasecmp(v->name, "adsi")) {
			adsi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfer")) {
			transfer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancelwhenbridged")) {
			echocanbridged = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busydetect")) {
			busydetect = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callprogress")) {
			callprogress = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancel")) {
			if (v->value && strlen(v->value))
				y = atoi(v->value);
			else
				y = 0;
			if ((y == 32) || (y == 64) || (y == 128) || (y == 256))
				echocancel = y;
			else
				echocancel = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			hidecallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callreturn")) {
			callreturn = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			callwaiting = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			callwaitingcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		} else if (!strcasecmp(v->name, "musiconhold")) {
			strncpy(musicclass, v->value, sizeof(musicclass)-1);
		} else if (!strcasecmp(v->name, "stripmsd")) {
			stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			cur_group = get_group(v->value);
		} else if (!strcasecmp(v->name, "callgroup")) {
			cur_callergroup = get_group(v->value);
		} else if (!strcasecmp(v->name, "pickupgroup")) {
			cur_pickupgroup = get_group(v->value);
		} else if (!strcasecmp(v->name, "immediate")) {
			immediate = ast_true(v->value);
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%f", &rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid rxgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%f", &txgain) != 1) {
				ast_log(LOG_WARNING, "Invalid txgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived"))
				strcpy(callerid,"");
			else
				strncpy(callerid, v->value, sizeof(callerid)-1);
		} else if (!strcasecmp(v->name, "accountcode")) {
			strncpy(accountcode, v->value, sizeof(accountcode)-1);
		} else if (!strcasecmp(v->name, "amaflags")) {
			y = ast_cdr_amaflags2int(v->value);
			if (y < 0) 
				ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
			else
				amaflags = y;
		} else if (!strcasecmp(v->name, "signalling")) {
			if (!strcasecmp(v->value, "em")) {
				cur_signalling = SIG_EM;
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
			} else if (!strcasecmp(v->value, "featd")) {
				cur_signalling = SIG_FEATD;
				cur_radio = 0;
			} else if (!strcasecmp(v->value, "featdmf")) {
				cur_signalling = SIG_FEATDMF;
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
			} else {
				ast_log(LOG_WARNING, "Unknown PRI dialplan '%s' at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "switchtype")) {
			if (!strcasecmp(v->value, "national")) 
				switchtype = PRI_SWITCH_NI2;
			else if (!strcasecmp(v->value, "dms100"))
				switchtype = PRI_SWITCH_DMS100;
			else if (!strcasecmp(v->value, "4ess"))
				switchtype = PRI_SWITCH_ATT4ESS;
			else if (!strcasecmp(v->value, "5ess"))
				switchtype = PRI_SWITCH_LUCENT5E;
			else if (!strcasecmp(v->value, "euroisdn"))
				switchtype = PRI_SWITCH_EUROISDN_E1;
			else {
				ast_log(LOG_ERROR, "Unknown switchtype '%s'\n", v->value);
				ast_destroy(cfg);
				ast_pthread_mutex_unlock(&iflock);
				unload_module();
				return -1;
			}
		} else if (!strcasecmp(v->name, "minunused")) {
			minunused = atoi(v->value);
		} else if (!strcasecmp(v->name, "idleext")) {
			strncpy(idleext, v->value, sizeof(idleext) - 1);
		} else if (!strcasecmp(v->name, "idledial")) {
			strncpy(idledial, v->value, sizeof(idledial) - 1);
#endif		
		} else
			ast_log(LOG_DEBUG, "Ignoring %s\n", v->name);
		v = v->next;
	}
	ast_pthread_mutex_unlock(&iflock);
	/* Make sure we can register our Zap channel type */
	if (ast_channel_register(type, tdesc, AST_FORMAT_SLINEAR |  AST_FORMAT_ULAW, zt_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		unload_module();
		return -1;
	}
	if (ast_channel_register(typecompat, tdesc, AST_FORMAT_SLINEAR |  AST_FORMAT_ULAW, zt_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", typecompat);
		ast_destroy(cfg);
		unload_module();
		return -1;
	}
	ast_destroy(cfg);
#ifdef ZAPATA_PRI
	for (x=0;x<NUM_SPANS;x++) {
		for (y=1;y<pris[x].channels;y++) {
			if (pris[x].chanmask[y]) {
				offset = pris[x].pvt[y]->channel - y;
				if ((pris[x].offset > -1) && (pris[x].offset != offset)) {
					ast_log(LOG_WARNING, "Huh??  Offset mismatch...\n");
				}
				pris[x].offset = offset;
				pris[x].span = x + 1;
				if (start_pri(pris + x)) {
					ast_log(LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
				} else if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "Starting D-Channel on span %d\n", x + 1);
				break;
			}
		}
	}
	ast_cli_register(&pri_debug);
	ast_cli_register(&pri_no_debug);
	ast_cli_register(&pri_really_debug);
#endif	
#ifdef ZAPATA_R2
	ast_cli_register(&r2_debug);
	ast_cli_register(&r2_no_debug);
#endif	
	ast_cli_register(&cli_show_channels);
	ast_cli_register(&cli_show_channel);
	ast_cli_register(&cli_destroy_channel);
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int unload_module()
{
	struct zt_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	ast_channel_unregister(typecompat);
	ast_cli_unregister(&cli_show_channels);
	ast_cli_unregister(&cli_show_channel);
	ast_cli_unregister(&cli_destroy_channel);
	if (!ast_pthread_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		iflist = NULL;
		ast_pthread_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_pthread_mutex_lock(&monlock)) {
		if (monitor_thread) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = -2;
		ast_pthread_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!ast_pthread_mutex_lock(&iflock)) {
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
			/* Free associated memory */
			free(pl);
		}
		iflist = NULL;
		ast_pthread_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
		
	return 0;
}

#if 0

static int reload_zt(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct zt_pvt *tmp;
	struct zt_pvt *prev = NULL;
	char *chan;
	int start, finish,x;
	char *stringp=NULL;

	/* Some crap that needs to be reinitialized on the reload */
	strcpy(context, "default");
	language[0] = '\0'; 
	musicclass[0] = '\0';
	use_callerid = 1;
	cur_signalling = -1;
	cur_group = 0;
	cur_callergroup = 0;
	cur_pickupgroup = 0;
	immediate = 0;
	stripmsd = 0;
	callwaiting = 0;
	busydetect = 0;
	callprogress = 0;
	callwaitingcallerid = 0;
	hidecallerid = 0;
	callreturn = 0;
	threewaycalling = 0;
	transfer = 0;
	rxgain = 0.0;
	txgain = 0.0;
	firstdigittimeout = 16000;
	gendigittimeout = 8000;
	amaflags = 0;
	adsi = 0;
	strncpy(accountcode, "", sizeof(accountcode)-1);
#ifdef ZAPATA_PRI
	strncpy(idleext, "", sizeof(idleext) - 1);
	strncpy(idledial, "", sizeof(idledial) - 1);
	minunused = 2;
	minidle = 0;
#endif
//	usecnt = 0;

#if 0
#ifdef ZAPATA_PRI
	int y;
#endif
#endif

	
#if 0
#ifdef ZAPATA_PRI
	memset(pris, 0, sizeof(pris));
	for (y=0;y<NUM_SPANS;y++)
		pris[y].fd = -1;
#endif
#endif /* 0 */
	
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}
	

	if (ast_pthread_mutex_lock(&iflock)) {
		/* It's a little silly to lock it, but we mind as well just to be sure */
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return -1;
	}
	
	/* Part of the primary changes for the reload... */
	tmp = iflist;
	
	while (tmp) {
		tmp->destroy = 1;
		tmp = tmp->next;
	}

	v = ast_variable_browse(cfg, "channels");
	
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "channel")) {
			if (cur_signalling < 0) {
				ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
				ast_destroy(cfg);
				ast_pthread_mutex_unlock(&iflock);
				return -1;
			}
			stringp=v->value;
			chan = strsep(&stringp, ",");
			while(chan) {
				if (sscanf(chan, "%d-%d", &start, &finish) == 2) {
					/* Range */
				} else if (sscanf(chan, "%d", &start)) {
					/* Just one */
					finish = start;
				} else {
					ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", v->value, chan);
					ast_destroy(cfg);
					ast_pthread_mutex_unlock(&iflock);
					return -1;
				}
				if (finish < start) {
					ast_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
					x = finish;
					finish = start;
					start = x;
				}
				for (x = start; x <= finish; x++) {
					tmp = mkintf(x, cur_signalling);
					if (tmp) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Registered channel %d, %s signalling\n", x, sig2str(tmp->sig));
					} else {
						ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
						ast_destroy(cfg);
						ast_pthread_mutex_unlock(&iflock);
						return -1;
					}
				}
				chan = strsep(&stringp, ",");
			}
		} else if (!strcasecmp(v->name, "usecallerid")) {
			use_callerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "threewaycalling")) {
			threewaycalling = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfer")) {
			transfer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busydetect")) {
			busydetect = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callprogress")) {
			callprogress = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			hidecallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callreturn")) {
			callreturn = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			callwaiting = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			callwaitingcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		} else if (!strcasecmp(v->name, "musiconhold")) {
			strncpy(musicclass, v->value, sizeof(musicclass)-1);
		} else if (!strcasecmp(v->name, "stripmsd")) {
			stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			cur_group = get_group(v->value);
		} else if (!strcasecmp(v->name, "callgroup")) {
			cur_callergroup = get_group(v->value);
		} else if (!strcasecmp(v->name, "pickupgroup")) {
			cur_pickupgroup = get_group(v->value);
		} else if (!strcasecmp(v->name, "immediate")) {
			immediate = ast_true(v->value);
		} else if (!strcasecmp(v->name, "mailbox")) {
			strncpy(mailbox, v->value, sizeof(mailbox) -1);
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%f", &rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid rxgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%f", &txgain) != 1) {
				ast_log(LOG_WARNING, "Invalid txgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived"))
				strcpy(callerid,"");
			else
				strncpy(callerid, v->value, sizeof(callerid)-1);
		} else if (!strcasecmp(v->name, "signalling")) {
			if (!strcasecmp(v->value, "em")) {
				cur_signalling = SIG_EM;
			} else if (!strcasecmp(v->value, "em_w")) {
				cur_signalling = SIG_EMWINK;
			} else if (!strcasecmp(v->value, "fxs_ls")) {
				cur_signalling = SIG_FXSLS;
			} else if (!strcasecmp(v->value, "fxs_gs")) {
				cur_signalling = SIG_FXSGS;
			} else if (!strcasecmp(v->value, "fxs_ks")) {
				cur_signalling = SIG_FXSKS;
			} else if (!strcasecmp(v->value, "fxo_ls")) {
				cur_signalling = SIG_FXOLS;
			} else if (!strcasecmp(v->value, "fxo_gs")) {
				cur_signalling = SIG_FXOGS;
			} else if (!strcasecmp(v->value, "fxo_ks")) {
				cur_signalling = SIG_FXOKS;
			} else if (!strcasecmp(v->value, "featd")) {
				cur_signalling = SIG_FEATD;
			} else if (!strcasecmp(v->value, "featdmf")) {
				cur_signalling = SIG_FEATDMF;
			} else if (!strcasecmp(v->value, "featb")) {
				cur_signalling = SIG_FEATB;
#ifdef ZAPATA_PRI
			} else if (!strcasecmp(v->value, "pri_net")) {
				cur_signalling = SIG_PRI;
				pritype = PRI_NETWORK;
			} else if (!strcasecmp(v->value, "pri_cpe")) {
				cur_signalling = SIG_PRI;
				pritype = PRI_CPE;
#endif
			} else {
				ast_log(LOG_ERROR, "Unknown signalling method '%s'\n", v->value);
			}
#ifdef ZAPATA_PRI
		} else if (!strcasecmp(v->name, "switchtype")) {
			if (!strcasecmp(v->value, "national")) 
				switchtype = PRI_SWITCH_NI2;
			else if (!strcasecmp(v->value, "dms100"))
				switchtype = PRI_SWITCH_DMS100;
			else if (!strcasecmp(v->value, "4ess"))
				switchtype = PRI_SWITCH_ATT4ESS;
			else if (!strcasecmp(v->value, "5ess"))
				switchtype = PRI_SWITCH_LUCENT5E;
			else {
				ast_log(LOG_ERROR, "Unknown switchtype '%s'\n", v->value);
				ast_destroy(cfg);
				ast_pthread_mutex_unlock(&iflock);
				return -1;
			}
		} else if (!strcasecmp(v->name, "minunused")) {
			minunused = atoi(v->value);
		} else if (!strcasecmp(v->name, "idleext")) {
			strncpy(idleext, v->value, sizeof(idleext) - 1);
		} else if (!strcasecmp(v->name, "idledial")) {
			strncpy(idledial, v->value, sizeof(idledial) - 1);
#endif
		} else
			ast_log(LOG_DEBUG, "Ignoring %s\n", v->name);
		v = v->next;
	}

	tmp = iflist;
	prev = NULL;

	while (tmp) {
		if (tmp->destroy) {
			if (destroy_channel(prev, tmp, 0)) {
				ast_log(LOG_ERROR, "Unable to destroy chan_zap channel %d\n", tmp->channel);
				ast_pthread_mutex_unlock(&iflock);
				return -1;
			}
			tmp = tmp->next;
		} else {
			prev = tmp;
			tmp = tmp->next;
		}
	}

	ast_pthread_mutex_unlock(&iflock);

	ast_destroy(cfg);
#if 0
#ifdef ZAPATA_PRI
	for (x=0;x<NUM_SPANS;x++) {
		for (y=1;y<23;y++) {
			if (pris[x].chanmask[y]) {
				pris[x].offset = x * 24;
				pris[x].span = x + 1;
				if (start_pri(pris + x)) {
					ast_log(LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
				} else if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "Starting D-Channel on span %d\n", x + 1);
				break;
			}
		}
	}
#endif
#endif
	/* And start the monitor for the first time */

	restart_monitor();
	return 0;
}
#endif

static int zt_sendtext(struct ast_channel *c, char *text)
{
#define	END_SILENCE_LEN 400
#define	HEADER_MS 50
#define	TRAILER_MS 5
#define	HEADER_LEN ((HEADER_MS + TRAILER_MS) * 8)
#define	ASCII_BYTES_PER_CHAR 80

	unsigned char *buf,*mybuf;
	struct zt_pvt *p = c->pvt->pvt;
	fd_set wfds,efds;
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
			ast_log(LOG_ERROR, "TDD generate (len %d) failed!!\n",strlen(text));
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
		FD_ZERO(&wfds);
		FD_ZERO(&efds);
		FD_SET(fd,&wfds);
		FD_SET(fd,&efds);			
		res = select(fd + 1,NULL,&wfds,&efds,NULL);
		if (!res) {
			ast_log(LOG_DEBUG, "select (for write) ret. 0 on channel %d\n", p->channel);
			continue;
		}
		  /* if got exception */
		if (FD_ISSET(fd,&efds)) return -1;
		if (!FD_ISSET(fd,&wfds)) {
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

#if 0
/* XXX Very broken on PRI XXX */
int reload(void)
{
	if (reload_zt()) {
		ast_log(LOG_WARNING, "Reload of chan_zap is unsuccessful\n");
		return -1;
	}

	return 0;
}
#endif
int usecount()
{
	int res;
	ast_pthread_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_pthread_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
