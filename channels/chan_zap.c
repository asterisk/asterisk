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
#include <pthread.h>
#include <string.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/file.h>
#include <asterisk/ulaw.h>
#include <asterisk/callerid.h>
#include <asterisk/adsi.h>
#include <asterisk/cli.h>
#include <asterisk/cdr.h>
#include <asterisk/parking.h>
#include <asterisk/tdd.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/zaptel.h>
#include <zap.h>
#include <math.h>
#include <tonezone.h>
#ifdef ZAPATA_PRI
#include <libpri.h>
#endif

/* 
   XXX 
   XXX   We definitely need to lock the private structure in zt_read and such 
   XXX  
 */

#define RINGT 274

#ifdef ZAPATA_PRI
static char *desc = "Zapata Telephony (PRI) Driver";
static char *tdesc = "Zapata Telephony + PRI Interface Driver";
#else
static char *desc = "Zapata Telphony Driver";
static char *tdesc = "Zapata Telephony Interface Driver";
#endif
static char *type = "Zap";
static char *typecompat = "Tor";	/* Retain compatibility with chan_tor */
static char *config = "zapata.conf";

#define SIG_EM		ZT_SIG_EM
#define SIG_EMWINK 	(0x10000 | ZT_SIG_EM)
#define SIG_FEATD	(0x20000 | ZT_SIG_EM)
#define SIG_FXSLS	ZT_SIG_FXSLS
#define SIG_FXSGS	ZT_SIG_FXSGS
#define SIG_FXSKS	ZT_SIG_FXSKS
#define SIG_FXOLS	ZT_SIG_FXOLS
#define SIG_FXOGS	ZT_SIG_FXOGS
#define SIG_FXOKS	ZT_SIG_FXOKS
#define SIG_PRI		ZT_SIG_CLEAR

#define NUM_SPANS 	32

static char context[AST_MAX_EXTENSION] = "default";
static char callerid[256] = "";

static char language[MAX_LANGUAGE] = "";

static int use_callerid = 1;

static int cur_signalling = -1;

static int cur_group = 0;
static int cur_callergroup = 0;
static int cur_pickupgroup = 0;

static int immediate = 0;

static int stripmsd = 0;

static int callwaiting = 0;

static int callwaitingcallerid = 0;

static int hidecallerid = 0;

static int threewaycalling = 0;

static int transfer = 0;

static float rxgain = 0.0;

static float txgain = 0.0;

static int echocancel;

static char accountcode[20] = "";

static int amaflags = 0;

static int adsi = 0;

/* Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/* How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

static int usecnt =0;
static pthread_mutex_t usecnt_lock = PTHREAD_MUTEX_INITIALIZER;

/* Protect the interface list (of zt_pvt's) */
static pthread_mutex_t iflock = PTHREAD_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static pthread_mutex_t monlock = PTHREAD_MUTEX_INITIALIZER;

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

/* Chunk size to read -- we use the same size as the chunks that the zapata library uses.  */   
#define READ_SIZE 204

#define MASK_AVAIL		(1 << 0)		/* Channel available for PRI use */
#define MASK_INUSE		(1 << 1)		/* Channel currently in use */

#define CALLWAITING_SILENT_SAMPLES	( (300 * 8) / READ_SIZE) /* 300 ms */
#define CALLWAITING_REPEAT_SAMPLES	( (10000 * 8) / READ_SIZE) /* 300 ms */

struct zt_pvt;


#ifdef ZAPATA_PRI
struct zt_pri {
	pthread_t master;			/* Thread of master */
	pthread_mutex_t lock;		/* Mutex */
	int nodetype;				/* Node type */
	int switchtype;				/* Type of switch to emulate */
	int dchannel;			/* What channel the dchannel is on */
	int channels;			/* Num of chans in span (31 or 24) */
	struct pri *pri;
	int debug;
	int fd;
	int up;
	int offset;
	int span;
	int chanmask[31];			/* Channel status */
	struct zt_pvt *pvt[30];	/* Member channel pvt structs */
	struct zt_channel *chan[30];	/* Channels on each line */
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

#endif

static struct zt_pvt {
	ZAP *z;
	pthread_mutex_t lock;
	struct ast_channel *owner;	/* Our owner (if applicable) */
	struct ast_channel *owners[3];	
		/* Up to three channels can be associated with this call */
		
	int callwaitindex;			/* Call waiting index into owners */	
	int thirdcallindex;			/* Three-way calling index into owners */
	int normalindex;			/* "Normal" call index into owners */
	
	int sig;					/* Signalling style */
	float rxgain;
	float txgain;
	struct zt_pvt *next;			/* Next channel in list */
	char context[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char callerid[AST_MAX_EXTENSION];
	char callwaitcid[AST_MAX_EXTENSION];
	char dtmfq[AST_MAX_EXTENSION];
	struct ast_frame f_unused;	/* Usually unused, but could in rare cases be needed */
	struct ast_frame f[3];		/* One frame for each channel.  How did this ever work before? */
	short buffer[3][AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	int group;
	int law;
	int callgroup;
	int pickupgroup;
	int immediate;				/* Answer before getting digits? */
	int channel;				/* Channel Number */
	int span;					/* Span number */
	int dialing;
	int dialednone;
	int use_callerid;			/* Whether or not to use caller id on this channel */
	int hidecallerid;
	int permhidecallerid;		/* Whether to hide our outgoing caller ID or not */
	int callwaitingrepeat;		/* How many samples to wait before repeating call waiting */
	unsigned char *cidspill;
	int cidpos;
	int cidlen;
	int ringt;
	int stripmsd;
	int needringing[3];
	int needanswer[3];
	int callwaiting;
	int callwaitcas;
	int callwaitrings;
	int echocancel;
	int permcallwaiting;
	int callwaitingcallerid;
	int threewaycalling;
	int transfer;
	int cref;					/* Call reference number */
	ZT_DIAL_OPERATION dop;
	struct zt_confinfo conf;	/* Saved state of conference */
	struct zt_confinfo conf2;	/* Saved state of alternate conference */
	int confno;					/* Conference number */
	ZAP *pseudo;				/* Pseudo channel FD */
	int pseudochan;				/* Pseudo channel */
	int destroy;
	int ignoredtmf;				
	int inalarm;
	char accountcode[20];		/* Account code */
	int amaflags;				/* AMA Flags */
	char didtdd;			/* flag to say its done it once */
	struct tdd_state *tdd;		/* TDD flag */
	int linear;
	int adsi;
#ifdef ZAPATA_PRI
	struct zt_pri *pri;
	q931_call *call;
#endif	
} *iflist = NULL;

#define INTHREEWAY(p) ((p->normalindex > -1) && (p->thirdcallindex > -1) && \
		(p->owner == p->owners[p->normalindex]))

#define ISTRUNK(p) ((p->sig == SIG_FXSLS) || (p->sig == SIG_FXSKS) || \
			(p->sig == SIG_FXSGS))


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
	if (!them->bridge) return 1;
	  /* get their private structure, too */
	themp = them->pvt->pvt;
	  /* if does not use zt bridge code, return 0 */
	if (them->pvt->bridge != zt_bridge) return 0;
	if (them->bridge->pvt->bridge != zt_bridge) return 0;
	return 1; /* okay, I guess we are okay to be clear */
}

static int alloc_pseudo(struct zt_pvt *p)
{
	int x;
	ZAP *z;
	int res;
	ZT_BUFFERINFO bi;
	if (p->pseudo || p->pseudochan){
		ast_log(LOG_WARNING, "Already have a pseudo fd: %d, chan: %d\n",
			zap_fd(p->pseudo), p->pseudochan);
		return -1;
	}
	z = zap_open("/dev/zap/pseudo", 1);
	if (!z) {
		ast_log(LOG_WARNING, "Unable to open /dev/zap/pseudo: %s\n", strerror(errno));
		return -1;
	} else {
		res = ioctl(zap_fd(z), ZT_GET_BUFINFO, &bi);
		if (!res) {
			bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
			bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
			bi.numbufs = 4;
			res = ioctl(zap_fd(z), ZT_SET_BUFINFO, &bi);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", x);
			}
		} else 
			ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", x);
		p->pseudo = z;
		if (ioctl(zap_fd(z), ZT_CHANNO, &x) == 1) {
			ast_log(LOG_WARNING,"Unable to get channel number for pseudo channel on FD %d\n",zap_fd(z));
			return -1;
		}
		p->pseudochan = x;
		if (option_debug)
			ast_log(LOG_DEBUG, "Allocated pseudo channel %d on FD %d\n", p->pseudochan, zap_fd(p->pseudo));
		return 0;
	}
	/* Never reached */
	return 0;
}

static int unalloc_pseudo(struct zt_pvt *p)
{
	if (p->pseudo)
		zap_close(p->pseudo);
	if (option_debug)
		ast_log(LOG_DEBUG, "Released pseudo channel %d\n", p->pseudochan);
	p->pseudo = NULL;
	p->pseudochan = 0;
	return 0;
}

static int zt_digit(struct ast_channel *ast, char digit)
{
	ZT_DIAL_OPERATION zo;
	struct zt_pvt *p;
	int res;
	zo.op = ZT_DIAL_OP_APPEND;
	zo.dialstr[0] = 'T';
	zo.dialstr[1] = digit;
	zo.dialstr[2] = 0;
	p = ast->pvt->pvt;
	if ((res = ioctl(zap_fd(p->z), ZT_DIAL, &zo)))
		ast_log(LOG_WARNING, "Couldn't dial digit %c\n", digit);
	else
		p->dialing = 1;
	
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
		"Hook Transition Complete"
};
 
static char *event2str(int event)
{
        static char buf[256];
        if ((event < 13) && (event > -1))
                return events[event];
        sprintf(buf, "Event %d", event);
        return buf;
}

static char *sig2str(int sig)
{
	static char buf[256];
	switch(sig) {
	case SIG_EM:
		return "E & M Immediate";
	case SIG_EMWINK:
		return "E & M Wink";
	case SIG_FEATD:
		return "Feature Group D";
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
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d\n", sig);
		return buf;
	}
}

static int conf_set(struct zt_pvt *p, int req, int force)
{

	/* Set channel to given conference, -1 to allocate one */
	ZT_CONFINFO ci;
	ZT_CONFINFO cip;
	int res;
	if ((p->confno > -1) && (p->confno != req) && (!force)) {
		ast_log(LOG_WARNING, "Channel %d already has conference %d allocated\n", p->channel, p->confno);
		return -1;
	}
	ci.chan = 0;
	ci.confno = 0;
	/* Check current conference stuff */
	res = ioctl(zap_fd(p->z), ZT_GETCONF, &ci);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to get conference info on channel %d: %s\n",
			p->channel, strerror(errno));
		return -1;
	}
	if (!force && ci.confmode && (ci.confno != p->confno)) {
		ast_log(LOG_WARNING, "Channel %d is already in a conference (%d, %x) we didn't create (req = %d)\n", p->channel, ci.confno, ci.confmode, req);
		return -1;
	}
	ci.chan = 0;
	ci.confno = req;
	ci.confmode = ZT_CONF_REALANDPSEUDO | ZT_CONF_TALKER | ZT_CONF_LISTENER | ZT_CONF_PSEUDO_LISTENER | ZT_CONF_PSEUDO_TALKER; 
	res = ioctl(zap_fd(p->z), ZT_SETCONF, &ci);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set conference to %d on channel %d: %s\n",
			req, p->channel, strerror(errno));
		return -1;
	}
	if (INTHREEWAY(p)) {
			/* We have a three way call active, be sure the third participant is included in
			   our conference. */
		cip.chan = 0;
		cip.confno = ci.confno;
		cip.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
		
		res = ioctl(zap_fd(p->pseudo), ZT_SETCONF, &cip);
		if (res < 0) {
			ast_log(LOG_WARNING, "Failed to set conference info on pseudo channel %d: %s\n",
				p->pseudochan, strerror(errno));
			return -1;
		}
		ast_log(LOG_DEBUG, "Conferenced in third way call\n");
	} else {
		if (p->pseudo || (p->pseudochan)) {
			ast_log(LOG_DEBUG, "There's a pseudo something on %d (channel %d), but we're not conferencing it in at the moment?\n",
				zap_fd(p->pseudo), p->pseudochan);
			cip.chan = 0;
			cip.confno = ci.confno;
			cip.confmode = ZT_CONF_NORMAL;
			res = ioctl(zap_fd(p->pseudo), ZT_SETCONF, &cip);
			if (res < 0) {
				ast_log(LOG_WARNING, "Failed to set conference info on pseudo channel %d: %s\n",
					p->pseudochan, strerror(errno));
				return -1;
			}
		}
	}
	p->confno = ci.confno;
	return 0;
}

static int three_way(struct zt_pvt *p)
{
	ast_log(LOG_DEBUG, "Setting up three way call\n");
	return conf_set(p, p->confno, 0);
}

static int conf_clear(struct zt_pvt *p)
{
	ZT_CONFINFO ci;
	int res;
	ci.confmode = ZT_CONF_NORMAL;
	ci.chan = 0;
	ci.confno = 0;
	res = ioctl(zap_fd(p->z), ZT_SETCONF, &ci);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to clear conference info on channel %d: %s\n",
			p->channel, strerror(errno));
		return -1;
	}
	p->confno = -1;
	return 0;
}

static void zt_enable_ec(struct zt_pvt *p)
{
	int x;
	int res;
	if (p && p->echocancel) {
		x = 1;
		res = ioctl(zap_fd(p->z), ZT_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		else
			ast_log(LOG_DEBUG, "Enabled echo cancellation on channel %d\n", p->channel);
	}
}

static void zt_disable_ec(struct zt_pvt *p)
{
	int x;
	int res;
	if (p->echocancel) {
		x = 0;
		res = ioctl(zap_fd(p->z), ZT_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d\n", p->channel);
		else
			ast_log(LOG_DEBUG, "disabled echo cancellation on channel %d\n", p->channel);
	}
}

static int zt_get_index(struct ast_channel *ast, struct zt_pvt *p, int nullok)
{
	int res;
	if (p->owners[0] == ast)
		res = 0;
	else if (p->owners[1] == ast)
		res = 1;
	else if (p->owners[2] == ast)
		res = 2;
	else {
		res = -1;
		if (!nullok)
			ast_log(LOG_WARNING, "Unable to get index, and nullok is not asserted\n");
	}
	return res;
}

static int set_actual_gain(int fd, int chan, float rxgain, float txgain)
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
	for (j=0;j<256;j++) {
		/* XXX Fix for A-law XXX */
		k = (int)(((float)AST_MULAW(j)) * lrxgain);
		if (k > 32767) k = 32767;
		if (k < -32767) k = -32767;
		g.rxgain[j] = AST_LIN2MU(k);
		k = (int)(((float)AST_MULAW(j)) * ltxgain);
		if (k > 32767) k = 32767;
		if (k < -32767) k = -32767;
		g.txgain[j] = AST_LIN2MU(k);
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
		ast_log(LOG_WARNING, "zt hook failed: %s\n", strerror(errno));
	return res;
}

static int save_conference(struct zt_pvt *p)
{
	struct zt_confinfo c;
	int res;
	if (p->conf.confmode) {
		ast_log(LOG_WARNING, "Can't save conference -- already in use\n");
		return -1;
	}
	p->conf.chan = 0;
	res = ioctl(zap_fd(p->z), ZT_GETCONF, &p->conf);
	if (res) {
		ast_log(LOG_WARNING, "Unable to get conference info: %s\n", strerror(errno));
		p->conf.confmode = 0;
		return -1;
	}
	c.chan = 0;
	c.confno = 0;
	c.confmode = ZT_CONF_NORMAL;
	res = ioctl(zap_fd(p->z), ZT_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "Unable to set conference info: %s\n", strerror(errno));
		return -1;
	}
	switch(p->conf.confmode) {
	case ZT_CONF_NORMAL:
		p->conf2.confmode = 0;
		break;
	case ZT_CONF_MONITOR:
		/* Get the other size */
		p->conf2.chan = p->conf.confno;
		res = ioctl(zap_fd(p->z), ZT_GETCONF, &p->conf2);
		if (res) {
			ast_log(LOG_WARNING, "Unable to get secondaryconference info: %s\n", strerror(errno));
			p->conf2.confmode = 0;
			return -1;
		}
		c.chan = p->conf.confno;
		c.confno = 0;
		c.confmode = ZT_CONF_NORMAL;
		res = ioctl(zap_fd(p->z), ZT_SETCONF, &c);
		if (res) {
			ast_log(LOG_WARNING, "Unable to set secondaryconference info: %s\n", strerror(errno));
			p->conf2.confmode = 0;
			return -1;
		}
		break;
	case ZT_CONF_CONF | ZT_CONF_LISTENER | ZT_CONF_TALKER:
		p->conf2.confmode = 0;
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to save conference state for conf mode %d\n", p->conf.confmode);
		return -1;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Disabled conferencing\n");
	return 0;
}

static int restore_conference(struct zt_pvt *p)
{
	int res;
	if (p->conf.confmode) {
		res = ioctl(zap_fd(p->z), ZT_SETCONF, &p->conf);
		p->conf.confmode = 0;
		if (res) {
			ast_log(LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
			return -1;
		}
		if (p->conf2.confmode) {
			res = ioctl(zap_fd(p->z), ZT_SETCONF, &p->conf2);
			p->conf2.confmode = 0;
			if (res) {
				ast_log(LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
				return -1;
			}
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
	p->cidspill = malloc(MAX_CALLERID_SIZE);
	if (p->cidspill) {
		memset(p->cidspill, 0x7f, MAX_CALLERID_SIZE);
		p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, p->callwaitcid);
		/* Make sure we account for the end */
		p->cidlen += READ_SIZE * 4;
		p->cidpos = 0;
		send_callerid(p);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "CPE supports Call Waiting Caller*ID.  Sending '%s'\n", p->callwaitcid);
	} else return -1;
	return 0;
}

static int send_callerid(struct zt_pvt *p)
{
	/* Assumes spill in p->cidspill, p->cidlen in length and we're p->cidpos into it */
	int res;
	while(p->cidpos < p->cidlen) {
		res = write(zap_fd(p->z), p->cidspill + p->cidpos, p->cidlen - p->cidpos);
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
	p->cidspill = 0;
	if (p->callwaitcas) {
		zap_clrdtmfn(p->z);
		/* Check for a the ack on the CAS (up to 500 ms) */
		res = zap_getdtmf(p->z, 1, NULL, 0, 500, 500, ZAP_HOOKEXIT | ZAP_TIMEOUTOK);
		if (res > 0) {
			char tmp[2];
			strncpy(tmp, zap_dtmfbuf(p->z), sizeof(tmp)-1);
			zap_clrdtmfn(p->z);
			if ((tmp[0] == 'A') || (tmp[0] == 'D')) {
				send_cwcidspill(p);
			}
		} else {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "CPE does not support Call Waiting Caller*ID.\n");
			restore_conference(p);
		}
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
			ast_gen_cas(p->cidspill, 1, 2400 + 680);
			p->callwaitcas = 1;
			p->cidlen = 2400 + 680 + READ_SIZE * 4;
		} else {
			ast_gen_cas(p->cidspill, 1, 2400);
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

static int zt_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int x, res, index;
	char *c, *n, *l;
	char callerid[256];
	if ((ast->state != AST_STATE_DOWN) && (ast->state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "zt_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	p->dialednone = 0;
	switch(p->sig) {
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		if (p->owner == ast) {
			/* Normal ring, on hook */
			if (p->use_callerid) {
				/* Generate the Caller-ID spill if desired */
				if (p->cidspill) {
					ast_log(LOG_WARNING, "cidspill already exists??\n");
					free(p->cidspill);
				}
				p->cidspill = malloc(MAX_CALLERID_SIZE);
				p->callwaitcas = 0;
				if (p->cidspill) {
					p->cidlen = ast_callerid_generate(p->cidspill, ast->callerid);
					p->cidpos = 0;
					send_callerid(p);
				} else
					ast_log(LOG_WARNING, "Unable to generate CallerID spill\n");
			}
			x = ZT_RING;
			if (ioctl(zap_fd(p->z), ZT_HOOK, &x) && (errno != EINPROGRESS)) {
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
				
		}
		ast->state = AST_STATE_RINGING;
		index = zt_get_index(ast, p, 0);
		if (index > -1) {
			p->needringing[index] = 1;
		}
		break;
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
	case SIG_EMWINK:
	case SIG_EM:
	case SIG_FEATD:
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
		res = ioctl(zap_fd(p->z), ZT_HOOK, &x);
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
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%s", c + p->stripmsd);
		if (!res) {
			if (ioctl(zap_fd(p->z), ZT_DIAL, &p->dop)) {
				x = ZT_ONHOOK;
				ioctl(zap_fd(p->z), ZT_HOOK, &x);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
				return -1;
			}
		} else
			ast_log(LOG_DEBUG, "Deferring dialing...\n");
		p->dialing = 1;
		if (strlen(c + p->stripmsd) < 1) p->dialednone = 1;
		ast->state = AST_STATE_DIALING;
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
		if (pri_call(p->pri->pri, p->call, PRI_TRANS_CAP_SPEECH, 
			((p->channel - 1) % p->pri->channels) + 1, p->pri->nodetype == PRI_NETWORK ? 0 : 1, 1, l, PRI_NATIONAL_ISDN, 
			l ? PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN : PRES_NUMBER_NOT_AVAILABLE,
			c + p->stripmsd, PRI_NATIONAL_ISDN, 
			((p->law == ZT_LAW_ALAW) ? PRI_LAYER_1_ALAW : PRI_LAYER_1_ULAW))) {
			ast_log(LOG_WARNING, "Unable to setup call to %s\n", c + p->stripmsd);
			return -1;
		}
		break;
#endif				
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
	int res = 0;

	if (!now) {
		if (cur->owner) {
			owned = 1;
		}

		for (i = 0; i < 3; i++) {
			if (cur->owners[i]) {
				owned = 1;
			}
		}
		if (!owned) {
			if (prev) {
				prev->next = cur->next;
			} else {
				iflist = cur->next;
			}
			res = zap_close(cur->z);
			if (res) {
				ast_log(LOG_ERROR, "Unable to close device on channel %d\n", cur->channel);
				free(cur);
				return -1;
			}
			free(cur);
		}
	} else {
		if (prev) {
			prev->next = cur->next;
		} else {
			iflist = cur->next;
		}
		res = zap_close(cur->z);
		if (res) {
			ast_log(LOG_ERROR, "Unable to close device on channel %d\n", cur->channel);
			free(cur);
			return -1;
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
	zap_digitmode(p->z,0);
	ast->state = AST_STATE_DOWN;
	ast_log(LOG_DEBUG, "Hangup: index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		index, p->normalindex, p->callwaitindex, p->thirdcallindex);
	p->ignoredtmf = 0;
	
	if (index > -1) {
		/* Real channel, do some fixup */
		p->owners[index] = NULL;
		p->needanswer[index] = 0;
		p->needringing[index] = 0;
		if (index == p->normalindex) {
			p->normalindex = -1;
			if ((p->callwaitindex > -1) && (p->thirdcallindex > -1)) 
				ast_log(LOG_WARNING, "Normal call hung up with both three way call and a call waiting call in place?\n");
			if (p->callwaitindex > -1) {
				/* If we hung up the normal call, make the call wait call
				   be the normal call if there was one */
				p->normalindex = p->callwaitindex;
				p->callwaitindex = -1;
			} else if (p->thirdcallindex > -1) {
				/* This was part of a three way call */
				p->normalindex = p->thirdcallindex;
				p->owners[p->normalindex]->fds[0] = zap_fd(p->z);
				p->owner = p->owners[p->normalindex];
				p->thirdcallindex = -1;
				unalloc_pseudo(p);
			}
		} else if (index == p->callwaitindex) {
			/* If this was a call waiting call, mark the call wait
			   index as -1, so we know iavailable again */
			p->callwaitindex = -1;
		} else if (index == p->thirdcallindex) {
			/* If this was part of a three way call index, let us make
			   another three way call */
			p->thirdcallindex = -1;
			unalloc_pseudo(p);
		} else {
			/* This wasn't any sort of call, but how are we an index? */
			ast_log(LOG_WARNING, "Index found but not any type of call?\n");
		}
	}

	if (!p->owners[0] && !p->owners[1] && !p->owners[2]) {
		p->owner = NULL;
		p->ringt = 0;
		law = ZT_LAW_DEFAULT;
		res = ioctl(zap_fd(p->z), ZT_SETLAW, &law);
		p->linear = 0;
		zap_setlinear(p->z, 0);
		if (res < 0) 
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default\n", p->channel);
		/* Perform low level hangup if no owner left */
#ifdef ZAPATA_PRI
		if (p->sig == SIG_PRI) {
			if (p->call) {
				if (!pri_grab(p->pri)) {
					res = pri_disconnect(p->pri->pri, p->call, PRI_CAUSE_NORMAL_CLEARING);
					p->call = NULL;
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
			res = zt_set_hook(zap_fd(p->z), ZT_ONHOOK);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
			return -1;
		}
		switch(p->sig) {
		case SIG_FXOGS:
		case SIG_FXOLS:
		case SIG_FXOKS:
			res = ioctl(zap_fd(p->z), ZT_GET_PARAMS, &par);
			if (!res) {
#if 0
				ast_log(LOG_DEBUG, "Hanging up channel %d, offhook = %d\n", p->channel, par.rxisoffhook);
#endif
				/* If they're off hook, try playing congestion */
				if (par.rxisoffhook)
					tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
				else
					tone_zone_play_tone(zap_fd(p->z), -1);
			}
			break;
		default:
			tone_zone_play_tone(zap_fd(p->z), -1);
		}
		if (index > -1) {
			p->needringing[index] = 0;
			p->needanswer[index] = 0;
		}
		if (p->cidspill)
			free(p->cidspill);
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
		conf_clear(p);
		unalloc_pseudo(p);
		restart_monitor();
	}
	p->callwaitingrepeat = 0;
	ast->pvt->pvt = NULL;
	ast->state = AST_STATE_DOWN;
	ast_pthread_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	ast_pthread_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);

	tmp = iflist;
	prev = NULL;
	if (p->destroy) {
		while (tmp) {
			if (tmp->channel == p->channel) {
				destroy_channel(prev, tmp, 0);
				break;
			} else {
				prev = tmp;
				tmp = tmp->next;
			}
		}
	}

	return 0;
}

static int zt_answer(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int res=0;
	ast->state = AST_STATE_UP;
	switch(p->sig) {
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
		p->ringt = 0;
		/* Fall through */
	case SIG_EM:
	case SIG_EMWINK:
	case SIG_FEATD:
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Pick up the line */
		ast_log(LOG_DEBUG, "Took %s off hook\n", ast->name);
		res =  zt_set_hook(zap_fd(p->z), ZT_OFFHOOK);
		tone_zone_play_tone(zap_fd(p->z), -1);
		if (INTHREEWAY(p))
			tone_zone_play_tone(zap_fd(p->pseudo), -1);
		p->dialing = 0;
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
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		return -1;
	}
	return res;
}

static inline int bridge_cleanup(struct zt_pvt *p0, struct zt_pvt *p1)
{
	int res = 0;
	if (p0)
		res = conf_clear(p0);
	if (p1)
		res |= conf_clear(p1);
	return res;
}

static int zt_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
char	*cp;

	struct zt_pvt *p = chan->pvt->pvt;

	
	if ((option != AST_OPTION_TONE_VERIFY) &&
		(option != AST_OPTION_TDD))
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
		switch(*cp) {
		    case 1:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",chan->name);
			zap_digitmode(p->z,ZAP_MUTECONF);  /* set mute mode if desired */
			break;
		    case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			zap_digitmode(p->z,ZAP_MUTECONF | ZAP_MUTEMAX);  /* set mute mode if desired */
			break;
		    default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			zap_digitmode(p->z,0);  /* set mute mode if desired */
			break;
		}
		break;
	    case AST_OPTION_TDD:  /* turn on or off TDD */
		if (!*cp) { /* turn it off */
			ast_log(LOG_DEBUG, "Set option TDD MODE, value: OFF(0) on %s\n",chan->name);
			if (p->tdd) tdd_free(p->tdd);
			p->tdd = 0;
			break;
		}
		ast_log(LOG_DEBUG, "Set option TDD MODE, value: ON(1) on %s\n",chan->name);
		/* otherwise, turn it on */
		if (!p->didtdd) { /* if havent done it yet */
			unsigned char mybuf[41000],*buf;
			int size,res,fd,len;
			fd_set wfds,efds;
			buf = mybuf;
			memset(buf,0x7f,sizeof(mybuf)); /* set to silence */
			ast_tdd_gen_ecdisa(buf + 16000,16000);  /* put in tone */
			len = 40000;
			if (chan != p->owner)   /* if in three-way */
				fd = zap_fd(p->pseudo);
			else
				fd = zap_fd(p->z);
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
		if (!p->tdd) { /* if we dont have one yet */
			p->tdd = tdd_new(); /* allocate one */
		}		
		break;
	}
	errno = 0;
	return 0;
}

static int zt_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	/* Do a quickie conference between the two channels and wait for something to happen */
	struct zt_pvt *p0 = c0->pvt->pvt;
	struct zt_pvt *p1 = c1->pvt->pvt;
	struct ast_channel *who = NULL, *cs[3];
	struct ast_frame *f;
	int to = -1;
	
	int confno = -1;
	
	  /* if need DTMF, cant native bridge */
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return -2;
	  /* if cant run clear DTMF, cant native bridge */
	pthread_mutex_lock(&c0->lock);
	pthread_mutex_lock(&c1->lock);
	if (!CLEARDTMF(c0) || !CLEARDTMF(c1)) {
		pthread_mutex_unlock(&c1->lock);
		pthread_mutex_unlock(&c0->lock);
		return -3;
	}
	p0 = c0->pvt->pvt;
	p1 = c1->pvt->pvt;
	if (c0->type == type)
		/* Stop any playing tones */
		tone_zone_play_tone(zap_fd(p0->z), 	-1);
	if (c1->type == type)
		tone_zone_play_tone(zap_fd(p1->z), 	-1);
	pthread_mutex_unlock(&c1->lock);
	pthread_mutex_unlock(&c0->lock);

	cs[0] = c0;
	cs[1] = c1;
	for (;;) {
		pthread_mutex_lock(&c0->lock);
		pthread_mutex_lock(&c1->lock);
		p0 = c0->pvt->pvt;
		p1 = c1->pvt->pvt;

		/* Stop if we're a zombie or need a soft hangup */
		if (c0->zombie || ast_check_hangup(c0) || c1->zombie || ast_check_hangup(c1)) {
			*fo = NULL;
			if (who) *rc = who;
			bridge_cleanup(p0, p1);
			pthread_mutex_unlock(&c0->lock);
			pthread_mutex_unlock(&c1->lock);
			return 0;
		}
		if (!p0 || !p1 || (c0->type != type) || (c1->type != type)) {
			pthread_mutex_unlock(&c0->lock);
			pthread_mutex_unlock(&c1->lock);
			return -2;
		}

		if (INTHREEWAY(p0) && (c0 == p0->owners[p0->thirdcallindex]))
			tone_zone_play_tone(zap_fd(p0->pseudo), -1);
		if (INTHREEWAY(p1) && (c1 == p1->owners[p1->thirdcallindex]))
			tone_zone_play_tone(zap_fd(p1->pseudo), -1);
		if (INTHREEWAY(p0) && (INTHREEWAY(p1))) {
			ast_log(LOG_WARNING, "Too weird, can't bridge multiple three way calls\n");
			pthread_mutex_unlock(&c0->lock);
			pthread_mutex_unlock(&c1->lock);
			return -1;
		}
		if ((p0->owner == c0) && (p1->owner == c1)) {
			/* Okay, this call should actually be connected */
			if ((p0->confno > -1) && (p1->confno > -1) && (p0->confno != p1->confno)) {
				/* We have a conflict here.  Try to resolve it. */
				if ((INTHREEWAY(p0) && (c0 == p0->owners[p0->normalindex]))) {
					ast_log(LOG_DEBUG, "Channel %s is in a three way call with us, moving to our conference %d\n",
						c1->name, p0->confno);
					conf_set(p1, p0->confno, 1);
				} else if (INTHREEWAY(p1) && (c1 == p1->owners[p1->normalindex])) {
						ast_log(LOG_DEBUG, "Channel %s is in a three way call with us, moving to our conference %d\n",
							c0->name, p1->confno);
						conf_set(p0, p1->confno, 1);
				} else {
					ast_log(LOG_WARNING, "Can't bridge since %s is on conf %d and %s is on conf %d\n",
						c0->name, p0->confno, c1->name, p1->confno);
					pthread_mutex_unlock(&c0->lock);
					pthread_mutex_unlock(&c1->lock);
					return -1;
				}
			}
			if (p0->confno > -1)
				confno = p0->confno;
			else
				confno = p1->confno;
			if (confno < 0) {
				conf_set(p0, -1, 0);
				confno = p0->confno;
				ast_log(LOG_DEBUG, "Creating new conference %d for %s\n", confno, c0->name);
			}
			if (p0->confno != confno) {
				ast_log(LOG_DEBUG, "Placing %s in conference %d\n", c0->name, confno);
				conf_set(p0, confno, 0);
			}
			if (p1->confno != confno) {
				ast_log(LOG_DEBUG, "Placing %s in conference %d\n", c1->name, confno);
				conf_set(p1, confno, 0);
			}
		} else if (INTHREEWAY(p0) && (c0 == p0->owners[p0->thirdcallindex])) {
			/* p0 is in a three way call and we're the third leg.  Join their
			   conference, already in progress if there is one */
			if ((p0->confno > -1) && (p1->confno != p0->confno)) {
				confno = p0->confno;
				ast_log(LOG_DEBUG, "Placing %s in conference %d\n", c1->name, confno);
				conf_set(p1, confno, 0);
			}
		} else if (INTHREEWAY(p1) && (c1 == p1->owners[p1->thirdcallindex])) {
			/* p0 is in a three way call and we're the third leg.  Join their
			   conference, already in progress if there is one */
			if ((p1->confno > -1) && (p1->confno != p0->confno)) {
				confno = p0->confno;
				ast_log(LOG_DEBUG, "Placing %s in conference %d\n", c0->name, confno);
				conf_set(p0, confno, 0);
			}
		}
		pthread_mutex_unlock(&c0->lock);
		pthread_mutex_unlock(&c1->lock);
		
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_WARNING, "Nobody there??\n");
			continue;
		}
		  /* if gone out of CLEAR DTMF mode */
		if (!CLEARDTMF(c0) || !CLEARDTMF(c1)) {
			*fo = NULL;
			*rc = who;
			bridge_cleanup(p0, p1);
			return -3;
		}
		if (who == c0)
			p0->ignoredtmf = 1;
		else
			p1->ignoredtmf = 1;
		f = ast_read(who);
		if (who == c0)
			p0->ignoredtmf = 0;
		else
			p1->ignoredtmf = 0;
		if (!f) {
			*fo = NULL;
			*rc = who;
			bridge_cleanup(p0, p1);
			return 0;
		}
		if ((f->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			*fo = f;
			*rc = who;
			bridge_cleanup(p0, p1);
			return 0;
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
			(f->frametype == AST_FRAME_TEXT) ||
			(f->frametype == AST_FRAME_VIDEO) || 
			(f->frametype == AST_FRAME_IMAGE) ||
			(f->frametype == AST_FRAME_DTMF)) {
			if ((f->frametype == AST_FRAME_DTMF) && (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))) {
				if ((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
					*rc = c0;
					*fo = f;
					bridge_cleanup(p0, p1);
					return 0;
				} else
				if ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1)) {
					*rc = c1;
					*fo = f;
					bridge_cleanup(p0, p1);
					return 0;
				}
			}
			ast_frfree(f);
		} else
			ast_frfree(f);
		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	return 0;
}

static int zt_indicate(struct ast_channel *chan, int condition);

static int zt_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct zt_pvt *p = newchan->pvt->pvt;
	int x;
	ast_log(LOG_DEBUG, "New owner for channel %d is %s\n", p->channel, newchan->name);
	p->owner = newchan;
	for (x=0;x<3;x++)
		if (p->owners[x] == oldchan)
			p->owners[x] = newchan;
	if (newchan->state == AST_STATE_RINGING) 
		zt_indicate(newchan, AST_CONTROL_RINGING);
	return 0;
}

static int zt_ring_phone(struct zt_pvt *p)
{
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = ZT_ONHOOK;
	res = ioctl(zap_fd(p->z), ZT_HOOK, &x);
	do {
		x = ZT_RING;
		res = ioctl(zap_fd(p->z), ZT_HOOK, &x);
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
	if (p->owners[p->normalindex]->bridge) {
		if (ast_channel_masquerade(p->owners[p->thirdcallindex], p->owners[p->normalindex]->bridge)) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					p->owners[p->normalindex]->bridge->name, p->owners[p->thirdcallindex]->name);
			return -1;
		}
		/* Orphan the channel */
		p->owners[p->thirdcallindex] = NULL;
		p->thirdcallindex = -1;
	} else if (p->owners[p->thirdcallindex]->bridge) {
		if (ast_channel_masquerade(p->owners[p->normalindex], p->owners[p->thirdcallindex]->bridge)) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
				p->owners[p->thirdcallindex]->bridge->name, p->owners[p->normalindex]->name);
			return -1;
		}
		/* Orphan the normal channel */
		p->owners[p->normalindex] = NULL;
		p->normalindex = p->thirdcallindex;
		p->thirdcallindex = -1;
	} else {
		ast_log(LOG_DEBUG, "Neither %s nor %s are in a bridge, nothing to transfer\n",
					p->owners[p->normalindex]->name, p->owners[p->thirdcallindex]->name);
		p->owners[p->thirdcallindex]->softhangup=1;
	}
	return 0;
}

struct ast_frame *zt_handle_event(struct ast_channel *ast)
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
	p->f[index].frametype = AST_FRAME_NULL;
	p->f[index].datalen = 0;
	p->f[index].timelen = 0;
	p->f[index].mallocd = 0;
	p->f[index].offset = 0;
	p->f[index].src = "zt_handle_event";
	p->f[index].data = NULL;
	if (index < 0)
		return &p->f[index];
	res = zt_get_event(zap_fd(p->z));
	ast_log(LOG_DEBUG, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, index);
	switch(res) {
		case ZT_EVENT_DIALCOMPLETE:
			if (p->inalarm) break;
			if (ioctl(zap_fd(p->z),ZT_DIALING,&x) == -1) {
				ast_log(LOG_DEBUG, "ZT_DIALING ioctl failed on %s\n",ast->name);
				return NULL;
			}
			if (!x) { /* if not still dialing in driver */
				zt_enable_ec(p);
				p->dialing = 0;
				if (ast->state == AST_STATE_DIALING) {
					if (!p->dialednone && ((p->sig == SIG_EM) || (p->sig == SIG_EMWINK) || (p->sig == SIG_FEATD))) {
						ast->state = AST_STATE_RINGING;
					} else {
					ast->state = AST_STATE_UP;
					p->f[index].frametype = AST_FRAME_CONTROL;
					p->f[index].subclass = AST_CONTROL_ANSWER;
					}
				}
			}
			break;
		case ZT_EVENT_ALARM:
			p->inalarm = 1;
			/* fall through intentionally */
		case ZT_EVENT_ONHOOK:
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				/* Check for some special conditions regarding call waiting */
				if (index == p->normalindex) {
					/* The normal line was hung up */
					if (p->callwaitindex > -1) {
						bridge_cleanup(p->owners[p->normalindex]->pvt->pvt,
						    p->owners[p->callwaitindex]->pvt->pvt);
						/* There's a call waiting call, so ring the phone */
						p->owner = p->owners[p->callwaitindex];
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has (callwait) call, ringing phone\n", p->owner);
						p->needanswer[index] = 0;
						p->needringing[index] = 0;
						p->callwaitingrepeat = 0;
						zt_ring_phone(p);
					} else if (p->thirdcallindex > -1) {
						if (p->transfer) {
							if (attempt_transfer(p))
								p->owners[p->thirdcallindex]->softhangup = 1;
						} else
							p->owners[p->thirdcallindex]->softhangup=1;
					}
				} else if (index == p->callwaitindex) {
					/* Check to see if there is a normal call */
					if (p->normalindex > -1) {
						bridge_cleanup(p->owners[p->normalindex]->pvt->pvt,
						    p->owners[p->callwaitindex]->pvt->pvt);
						/* There's a call waiting call, so ring the phone */
						p->owner = p->owners[p->normalindex];
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has (normal) call, ringing phone\n", p->owner);
						p->needanswer[index] = 0;
						p->needringing[index] = 0;
						p->callwaitingrepeat = 0;
						zt_ring_phone(p);
					}
				} else if (index == p->thirdcallindex) {
					if ((ast->state != AST_STATE_UP) && (ast->state != AST_STATE_RINGING) &&
							(ast->state != AST_STATE_RING)) {
						/* According to the LSSGR, we should kill everything now, and we 
						   do, instead of ringing the phone */
						if (p->normalindex > -1) 
							p->owners[p->normalindex]->softhangup=1;
						if (p->callwaitindex > -1) {
							ast_log(LOG_WARNING, "Somehow there was a call wait\n");
							p->owners[p->callwaitindex]->softhangup = 1;
						}
						
					} else {
						if (p->transfer) {
							if (attempt_transfer(p))
								p->owners[p->normalindex]->softhangup = 1;
							else {
								/* Don't actually hangup.  We're going to get transferred */
								zt_disable_ec(p);
								break;
							}
						} else 
							p->owners[p->normalindex]->softhangup = 1;
					}
				}
				/* Fall through */
			default:
				zt_disable_ec(p);
				return NULL;
			}
			break;
		case ZT_EVENT_RINGOFFHOOK:
			if (p->inalarm) break;
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				switch(ast->state) {
				case AST_STATE_RINGING:
					zt_enable_ec(p);
					ast->state = AST_STATE_UP;
					p->f[index].frametype = AST_FRAME_CONTROL;
					p->f[index].subclass = AST_CONTROL_ANSWER;
					/* Make sure it stops ringing */
					zt_set_hook(zap_fd(p->z), ZT_OFFHOOK);
					ast_log(LOG_DEBUG, "channel %d answered\n", p->channel);
					if (p->cidspill) {
						/* Cancel any running CallerID spill */
						free(p->cidspill);
						p->cidspill = NULL;
					}
					p->dialing = 0;
					return &p->f[index];
				case AST_STATE_DOWN:
					ast->state = AST_STATE_RING;
					ast->rings = 1;
					p->f[index].frametype = AST_FRAME_CONTROL;
					p->f[index].subclass = AST_CONTROL_OFFHOOK;
					ast_log(LOG_DEBUG, "channel %d picked up\n", p->channel);
					return &p->f[index];
				case AST_STATE_UP:
					/* Make sure it stops ringing */
					zt_set_hook(zap_fd(p->z), ZT_OFFHOOK);
					/* Okay -- probably call waiting*/
					break;
				default:
					ast_log(LOG_WARNING, "FXO phone off hook in weird state %d??\n", ast->state);
				}
				break;
			case SIG_FXSLS:
			case SIG_FXSGS:
			case SIG_FXSKS:
				if (ast->state == AST_STATE_RING) {
					p->ringt = RINGT;
				}
				/* Fall through */
			case SIG_EM:
			case SIG_EMWINK:
			case SIG_FEATD:
				if (ast->state == AST_STATE_DOWN) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Ring detected\n");
					p->f[index].frametype = AST_FRAME_CONTROL;
					p->f[index].subclass = AST_CONTROL_RING;
				} else if (ast->state == AST_STATE_RINGING) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Line answered\n");
					p->f[index].frametype = AST_FRAME_CONTROL;
					p->f[index].subclass = AST_CONTROL_ANSWER;
					ast->state = AST_STATE_UP;
				} else if (ast->state != AST_STATE_RING)
					ast_log(LOG_WARNING, "Ring/Off-hook in strange state %d on channel %d\n", ast->state, p->channel);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
			}
			break;
		case ZT_EVENT_RINGEROFF:
			if (p->inalarm) break;
			ast->rings++;
			if ((ast->rings > 1) && (p->cidspill)) {
				ast_log(LOG_WARNING, "Didn't finish Caller-ID spill.  Cancelling.\n");
				free(p->cidspill);
				p->cidspill = NULL;
				p->callwaitcas = 0;
			}
			p->f[index].frametype = AST_FRAME_CONTROL;
			p->f[index].subclass = AST_CONTROL_RINGING;
			break;
		case ZT_EVENT_RINGERON:
			break;
		case ZT_EVENT_NOALARM:
			p->inalarm = 0;
			break;
		case ZT_EVENT_WINKFLASH:
			if (p->inalarm) break;
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				ast_log(LOG_DEBUG, "Winkflash, index: %d, normal: %d, callwait: %d, thirdcall: %d\n",
					index, p->normalindex, p->callwaitindex, p->thirdcallindex);
				if (index == p->normalindex) {
					if (p->callwaitindex > -1) {
						tone_zone_play_tone(zap_fd(p->z), -1);
						p->owner = p->owners[p->callwaitindex];
						if (p->owner->state == AST_STATE_RINGING) {
							p->owner->state = AST_STATE_UP;
							p->needanswer[p->callwaitindex] = 1;
						}
						p->callwaitingrepeat = 0;
						conf_clear(p);
					} else if (p->thirdcallindex == -1) {
						if (p->threewaycalling) {
							if ((ast->state == AST_STATE_RINGING) ||
									(ast->state == AST_STATE_UP) ||
									(ast->state == AST_STATE_RING)) {
								if (!alloc_pseudo(p)) {
									/* Start three way call */
									res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_DIALRECALL);
									if (res)
										ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
									chan = zt_new(p, AST_STATE_RESERVED,0,0,1);
									p->owner = chan;
									if (pthread_create(&threadid, &attr, ss_thread, chan)) {
										ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
										res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
										ast_hangup(chan);
									} else {
										if (option_verbose > 2)	
											ast_verbose(VERBOSE_PREFIX_3 "Started three way call on channel %d (index %d)\n", p->channel, p->thirdcallindex);
										conf_clear(p);
									}		
								} else
									ast_log(LOG_WARNING, "Unable to allocate pseudo channel\n");
							} else 
								ast_log(LOG_DEBUG, "Flash when call not up or ringing\n");
						}
					} else {
						if (option_debug)
							ast_log(LOG_DEBUG, "Got flash with three way call up, dropping last call %d\n",
								p->thirdcallindex);
						/* Drop the last call and stop the conference */
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Dropping three-way call on %s\n", p->owners[p->thirdcallindex]->name);
						p->owners[p->thirdcallindex]->softhangup=1;
						conf_clear(p);
					}
				} else if (index == p->callwaitindex) {
					if (p->normalindex > -1) {
						p->owner = p->owners[p->normalindex];
						p->callwaitingrepeat = 0;
						conf_clear(p);
					} else
						ast_log(LOG_WARNING, "Wink/Flash on call wait, with no normal channel to flash to on channel %d?\n", p->channel);
				} else if (index == p->thirdcallindex) {
					if (p->normalindex > -1) {
						if ((ast->state != AST_STATE_RINGING) && (ast->state != AST_STATE_UP) && (ast->state != AST_STATE_RING)) {
							tone_zone_play_tone(zap_fd(p->z), -1);
							p->owner = p->owners[p->normalindex];
							ast_log(LOG_DEBUG, "Dumping incomplete three way call in state %d\n", ast->state);
							return NULL;
						}
						p->owner = p->owners[p->normalindex];
						p->owners[p->thirdcallindex]->fds[0] = zap_fd(p->pseudo);
						p->callwaitingrepeat = 0;
						if (p->owners[p->thirdcallindex]->state == AST_STATE_RINGING) {
							/* If we were ringing, stop the ringing on the main line and start it on
							   the pseudo */
							tone_zone_play_tone(zap_fd(p->z), -1);
							tone_zone_play_tone(zap_fd(p->pseudo), ZT_TONE_RINGTONE);
						}
						three_way(p);
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Established 3-way conference between %s and %s\n", 
										p->owners[p->normalindex]->name, p->owners[p->thirdcallindex]->name);
					} else {
						ast_log(LOG_WARNING, "Wink/Flash on threeway call, with no normal channel to flash to on channel %d?\n", p->channel);
						return NULL;
					}
				}
				break;
			case SIG_EM:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_FXSLS:
			case SIG_FXSGS:
				if (p->dialing)
					ast_log(LOG_DEBUG, "Ignoring wink on channel %d\n", p->channel);
				else
					ast_log(LOG_DEBUG, "Got wink in weird state %d on channel %d\n", ast->state, p->channel);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
			}
			break;
		case ZT_EVENT_HOOKCOMPLETE:
			if (p->inalarm) break;
			switch(p->sig) {
			case SIG_FXSLS:  /* only interesting for FXS */
			case SIG_FXSGS:
			case SIG_FXSKS:
			case SIG_EM:
			case SIG_EMWINK:
			case SIG_FEATD:
				res = ioctl(zap_fd(p->z), ZT_DIAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else 
					ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
				p->dop.dialstr[0] = '\0';
				break;
			default:
				break;
			}
			break;
		default:
			ast_log(LOG_DEBUG, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->f[index];
 }

struct ast_frame *zt_exception(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int res;
	int usedindex=-1;
	int index;
	
	index = zt_get_index(ast, p, 1);
	
	p->f[index].frametype = AST_FRAME_NULL;
	p->f[index].datalen = 0;
	p->f[index].timelen = 0;
	p->f[index].mallocd = 0;
	p->f[index].offset = 0;
	p->f[index].subclass = 0;
	p->f[index].src = "zt_exception";
	p->f[index].data = NULL;
		
	if ((p->owner != p->owners[0]) && 
	    (p->owner != p->owners[1]) &&
		(p->owner != p->owners[2])) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		res = zt_get_event(zap_fd(p->z));
		if ((p->callwaitindex > -1) && (p->normalindex > -1)) 
			ast_log(LOG_WARNING, "Absorbing exception on unowned channel, but there is both a normal and call waiting call still here?\n");
		if (p->callwaitindex > -1) {
			tone_zone_play_tone(zap_fd(p->z), -1);
			p->owner = p->owners[p->callwaitindex];
			usedindex = p->callwaitindex;
		} else if (p->normalindex > -1) {
			tone_zone_play_tone(zap_fd(p->z), -1);
			p->owner = p->owners[p->normalindex];
			usedindex = p->normalindex;
		} else {
			ast_log(LOG_WARNING, "No call wait call, no normal call, what do I do?\n");
			return NULL;
		}
		switch(res) {
		case ZT_EVENT_ONHOOK:
			zt_disable_ec(p);
			if (p->owner) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has call, ringing phone\n", p->owner->name);
				zt_ring_phone(p);
				p->callwaitingrepeat = 0;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			break;
		case ZT_EVENT_WINKFLASH:
			if (p->owner) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Channel %d flashed to other channel %s\n", p->channel, p->owner->name);
				if ((usedindex == p->callwaitindex) && (p->owner->state == AST_STATE_RINGING)) {
					/* Answer the call wait if necessary */
					p->needanswer[usedindex] = 1;
					p->owner->state = AST_STATE_UP;
				}
				p->callwaitingrepeat = 0;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		return &p->f[index];
	}
	/* If it's not us, return NULL immediately */
	if (ast != p->owner)
		return &p->f[index];

	return zt_handle_event(ast);
}

struct ast_frame  *zt_read(struct ast_channel *ast)
{
	struct zt_pvt *p = ast->pvt->pvt;
	int res;
	int index;
	ZAP *z = NULL;
	void *readbuf;
	
	pthread_mutex_lock(&p->lock);
	
	index = zt_get_index(ast, p, 0);
	
	p->f[index].frametype = AST_FRAME_NULL;
	p->f[index].datalen = 0;
	p->f[index].timelen = 0;
	p->f[index].mallocd = 0;
	p->f[index].offset = 0;
	p->f[index].subclass = 0;
	p->f[index].src = "zt_read";
	p->f[index].data = NULL;
	
	/* Hang up if we don't really exist */
	if (index < 0)	{
		ast_log(LOG_WARNING, "We dont exist?\n");
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	
	if (p->ringt == 1) {
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	else if (p->ringt > 0) 
		p->ringt--;

	if (p->needringing[index]) {
		/* Send ringing frame if requested */
		p->needringing[index] = 0;
		p->f[index].frametype = AST_FRAME_CONTROL;
		p->f[index].subclass = AST_CONTROL_RINGING;
		ast->state = AST_STATE_RINGING;
		pthread_mutex_unlock(&p->lock);
		return &p->f[index];
	}	
	
	if (p->needanswer[index]) {
		/* Send ringing frame if requested */
		p->needanswer[index] = 0;
		p->f[index].frametype = AST_FRAME_CONTROL;
		p->f[index].subclass = AST_CONTROL_ANSWER;
		ast->state = AST_STATE_UP;
		pthread_mutex_unlock(&p->lock);
		return &p->f[index];
	}	
	
	if (ast != p->owner) {
		/* If it's not us.  If this isn't a three way call, return immediately */
		if (!INTHREEWAY(p)) {
			pthread_mutex_unlock(&p->lock);
			return &p->f[index];
		}
		/* If it's not the third call, return immediately */
		if (ast != p->owners[p->thirdcallindex]) {
			pthread_mutex_unlock(&p->lock);
			return &p->f[index];
		}
		if (!p->pseudo) 
			ast_log(LOG_ERROR, "No pseudo channel\n");
		z = p->pseudo;		
	} else
		z = p->z;

	if (!z) {
		ast_log(LOG_WARNING, "No zap structure?!?\n");
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	
	/* Check first for any outstanding DTMF characters */
	if (strlen(p->dtmfq)) {
		p->f[index].subclass = p->dtmfq[0];
		memmove(p->dtmfq, p->dtmfq + 1, sizeof(p->dtmfq) - 1);
		p->f[index].frametype = AST_FRAME_DTMF;
		pthread_mutex_unlock(&p->lock);
		return &p->f[index];
	}
	
	if (ast->pvt->rawreadformat == AST_FORMAT_SLINEAR) {
		if (!p->linear) {
			p->linear = 1;
			res = zap_setlinear(p->z, p->linear);
			if (res) 
				ast_log(LOG_WARNING, "Unable to set channel %d to linear mode.\n", p->channel);
		}
	} else if ((ast->pvt->rawreadformat == AST_FORMAT_ULAW) ||
		   (ast->pvt->rawreadformat == AST_FORMAT_ALAW)) {
		if (p->linear) {
			p->linear = 0;
			res = zap_setlinear(p->z, p->linear);
			if (res) 
				ast_log(LOG_WARNING, "Unable to set channel %d to linear mode.\n", p->channel);
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %d\n", ast->pvt->rawreadformat);
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->buffer[index]) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	if ((z != p->z) && (z != p->pseudo)) {
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	res = zap_recchunk(z, readbuf, READ_SIZE, ((p->ignoredtmf) ? 0 : ZAP_DTMFINT));
	ast->blocking = 0;
	/* Check for hangup */
	if (res < 0) {
		if (res == -1) 
			ast_log(LOG_WARNING, "zt_rec: %s\n", strerror(errno));
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	if (res != READ_SIZE) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Short read, must be DTMF or something...\n");
		/* XXX UGLY!!  Zapata's DTMF handling is a bit ugly XXX */
		if (zap_dtmfwaiting(z) && !strlen(zap_dtmfbuf(z))) {
			zap_getdtmf(z, 1, NULL, 0, 1, 1, 0);
		}
		if (strlen(zap_dtmfbuf(z))) {
			ast_log(LOG_DEBUG, "Got some dtmf ('%s')... on channel %s\n", zap_dtmfbuf(z), ast->name);
			/* DTMF tone detected.  Queue and erturn */
			if (p->callwaitcas) {
				if (!strcmp(zap_dtmfbuf(z), "A") || !strcmp(zap_dtmfbuf(z), "D")) {
					ast_log(LOG_DEBUG, "Got some DTMF, but it's for the CAS\n");
					if (p->cidspill)
						free(p->cidspill);
					send_cwcidspill(p);
				}
				/* Return NULL */
				pthread_mutex_unlock(&p->lock);
				return &p->f[index];
			} else {
				strncpy(p->dtmfq + strlen(p->dtmfq), zap_dtmfbuf(z), sizeof(p->dtmfq) - strlen(p->dtmfq)-1);
				zap_clrdtmfn(z);
			}
		} else {
			pthread_mutex_unlock(&p->lock);
			return zt_handle_event(ast);
		}
		if (strlen(p->dtmfq)) {
			p->f[index].subclass = p->dtmfq[0];
			memmove(p->dtmfq, p->dtmfq + 1, sizeof(p->dtmfq) - 1);
			p->f[index].frametype = AST_FRAME_DTMF;
		}
		pthread_mutex_unlock(&p->lock);
		return &p->f[index];
	}
	if (p->tdd) { /* if in TDD mode, see if we receive that */
		int c;

		c = tdd_feed(p->tdd,readbuf,READ_SIZE);
		if (c < 0) {
			ast_log(LOG_DEBUG,"tdd_feed failed\n");
			return NULL;
		}
		if (c) { /* if a char to return */
			p->f[index].subclass = 0;
			p->f[index].frametype = AST_FRAME_TEXT;
			p->f[index].mallocd = 0;
			p->f[index].offset = AST_FRIENDLY_OFFSET;
			p->f[index].data = p->buffer[index] + AST_FRIENDLY_OFFSET;
			p->f[index].datalen = 1;
			*((char *) p->f[index].data) = c;
			pthread_mutex_unlock(&p->lock);
			return &p->f[index];
		}
	}
	if (p->callwaitingrepeat)
		p->callwaitingrepeat--;
	/* Repeat callwaiting */
	if (p->callwaitingrepeat == 1) {
		p->callwaitrings++;
		zt_callwait(ast);
	}
	if (ast->pvt->rawreadformat == AST_FORMAT_SLINEAR) {
		p->f[index].datalen = READ_SIZE * 2;
	} else 
		p->f[index].datalen = READ_SIZE;

	/* Handle CallerID Transmission */
	if (p->cidspill &&((ast->state == AST_STATE_UP) || (ast->rings == 1)))
		send_callerid(p);

	p->f[index].frametype = AST_FRAME_VOICE;
	p->f[index].subclass = ast->pvt->rawreadformat;
	p->f[index].timelen = READ_SIZE/8;
	p->f[index].mallocd = 0;
	p->f[index].offset = AST_FRIENDLY_OFFSET;
	p->f[index].data = p->buffer[index] + AST_FRIENDLY_OFFSET/2;
#if 0
	ast_log(LOG_DEBUG, "Read %d of voice on %s\n", p->f[index].datalen, ast->name);
#endif	
	if (p->dialing) {
		/* Whoops, we're still dialing, don't send anything */
		p->f[index].frametype = AST_FRAME_NULL;
		p->f[index].subclass = 0;
		p->f[index].timelen = 0;
		p->f[index].mallocd = 0;
		p->f[index].offset = 0;
		p->f[index].data = NULL;
		p->f[index].datalen= 0;
	}
	pthread_mutex_unlock(&p->lock);
	return &p->f[index];
}

static int my_zt_write(struct zt_pvt *p, unsigned char *buf, int len, int threeway)
{
	int sent=0;
	int size;
	int res;
	int fd;
	if (threeway) 
		fd = zap_fd(p->pseudo);
	else
		fd = zap_fd(p->z);
	while(len) {
		size = len;
		if (size > READ_SIZE)
			size = READ_SIZE;
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
	
	if (ast != p->owner) {
		/* If it's not us.  If this isn't a three way call, return immediately */
		if (!INTHREEWAY(p)) {
			return 0;
		}
		/* If it's not the third call, return immediately */
		if (ast != p->owners[p->thirdcallindex]) {
			return 0;
		}
	}
	
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Don't know what to do with frame type '%d'\n", frame->frametype);
		return -1;
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
		if (!p->linear) {
			p->linear = 1;
			res = zap_setlinear(p->z, p->linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
		res = my_zt_write(p, (unsigned char *)frame->data, frame->datalen, (ast != p->owner));
	} else {
		/* x-law already */
		if (p->linear) {
			p->linear = 0;
			res = zap_setlinear(p->z, p->linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
		res = my_zt_write(p, (unsigned char *)frame->data, frame->datalen, (ast != p->owner));
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
	switch(condition) {
	case AST_CONTROL_BUSY:
		res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_BUSY);
		break;
	case AST_CONTROL_RINGING:
		res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_RINGTONE);
		if (chan->state != AST_STATE_UP) {
			if ((chan->state != AST_STATE_RING) ||
				((p->sig != SIG_FXSKS) &&
				 (p->sig != SIG_FXSLS) &&
				 (p->sig != SIG_FXSGS)))
				chan->state = AST_STATE_RINGING;
		}
		break;
	case AST_CONTROL_CONGESTION:
		res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to set condition %d on channel %s\n", condition, chan->name);
	}
	return res;
}

static struct ast_channel *zt_new(struct zt_pvt *i, int state, int startpbx, int callwaiting, int thirdcall)
{
	struct ast_channel *tmp;
	int x;
	int deflaw;
	int res;
	ZT_PARAMS ps;
	for (x=0;x<3;x++)
		if (!i->owners[x])
			break;
	if (x > 2) {
		ast_log(LOG_WARNING, "No available owner slots\n");
		return NULL;
	}
	tmp = ast_channel_alloc();
	if (tmp) {
		ps.channo = i->channel;
		res = ioctl(zap_fd(i->z), ZT_GET_PARAMS, &ps);
		if (res) {
			ast_log(LOG_WARNING, "Unable to get parameters, assuming MULAW\n");
			ps.curlaw = ZT_LAW_MULAW;
		}
		if (ps.curlaw == ZT_LAW_ALAW)
			deflaw = AST_FORMAT_ALAW;
		else
			deflaw = AST_FORMAT_ULAW;
		snprintf(tmp->name, sizeof(tmp->name), "Zap/%d-%d", i->channel, x + 1);
		tmp->type = type;
		tmp->fds[0] = zap_fd(i->z);
		
		tmp->nativeformats = AST_FORMAT_SLINEAR | deflaw;
		
		/* Start out assuming ulaw since it's smaller :) */
		tmp->pvt->rawreadformat = deflaw;
		tmp->readformat = deflaw;
		tmp->pvt->rawwriteformat = deflaw;
		tmp->writeformat = deflaw;
		
		tmp->state = state;
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
		/* Keep track of who owns it */
		i->owners[x] = tmp;
		if (!i->owner)
			i->owner = tmp;
		if (strlen(i->accountcode))
			strncpy(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode)-1);
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		if (callwaiting) {
			if (i->callwaitindex > -1)
				ast_log(LOG_WARNING, "channel %d already has a call wait call\n", i->channel);
			i->callwaitindex = x;
		} else if (thirdcall) {
			if (i->thirdcallindex > -1)
				ast_log(LOG_WARNING, "channel %d already has a third call\n", i->channel);
			i->thirdcallindex = x;
		} else {
			if (i->normalindex > -1) 
				ast_log(LOG_WARNING, "channel %d already has a normal call\n", i->channel);
			i->normalindex = x;
		}
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		/* If we've been told "no ADSI" then enforce it */
		if (!i->adsi)
			tmp->adsicpe = AST_ADSI_UNAVAILABLE;
		if (strlen(i->exten))
			strncpy(tmp->exten, i->exten, sizeof(tmp->exten)-1);
		if (startpbx) {
			if (strlen(i->callerid)) {
				tmp->callerid = strdup(i->callerid);
				tmp->hidden_callerid = strdup(i->callerid);
			}
			if (i->adsi) {
				/* Initialize ADSI here */
				adsi_channel_init(tmp);
			}
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
	res = set_actual_gain(zap_fd(p->z), 0, p->rxgain + 5.0, p->txgain);
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
	res = set_actual_gain(zap_fd(p->z), 0, p->rxgain, p->txgain);
	if (res) {
		ast_log(LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static void *ss_thread(void *data)
{
	struct ast_channel *chan = data;
	struct zt_pvt *p = chan->pvt->pvt;
	char exten[AST_MAX_EXTENSION];
	char exten2[AST_MAX_EXTENSION];
	unsigned char buf[256];
	char cid[256];
	struct callerid_state *cs;
	char *name=NULL, *number=NULL;
	int flags;
	int i;
	int timeout;
	char *s1, *s2;
	int len = 0;
	int res;
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Starting simple switch on '%s'\n", chan->name);
	zap_clrdtmf(p->z);
	switch(p->sig) {
	case SIG_FEATD:
	case SIG_EMWINK:
		zap_wink(p->z);
		/* Fall through */
	case SIG_EM:
		res = tone_zone_play_tone(zap_fd(p->z), -1);
		zap_clrdtmf(p->z);
		/* Wait for the first digit (up to 1 second). */
		res = zap_getdtmf(p->z, 1, NULL, 0, 1000, 1000, ZAP_TIMEOUTOK | ZAP_HOOKEXIT);

		if (res == 1) {
			/* If we got it, get the rest */
			res = zap_getdtmf(p->z, 50, NULL, 0, 250, 15000, ZAP_TIMEOUTOK | ZAP_HOOKEXIT);
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
		strncpy(exten, zap_dtmfbuf(p->z), sizeof(exten)-1);
		if (!strlen(exten))
			strncpy(exten, "s", sizeof(exten)-1);
		if (p->sig == SIG_FEATD) {
			if (exten[0] == '*') {
				strncpy(exten2, exten, sizeof(exten2)-1);
				/* Parse out extension and callerid */
				s1 = strtok(exten2 + 1, "*");
				s2 = strtok(NULL, "*");
				if (s2) {
					if (strlen(p->callerid))
						chan->callerid = strdup(p->callerid);
					else
						chan->callerid = strdup(s1);
					if (chan->callerid)
						chan->hidden_callerid = strdup(chan->callerid);
					strncpy(exten, s2, sizeof(exten)-1);
				} else
					strncpy(exten, s1, sizeof(exten)-1);
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		zt_enable_ec(p);
		if (ast_exists_extension(chan, chan->context, exten, 1, chan->callerid)) {
			strncpy(chan->exten, exten, sizeof(chan->exten)-1);
			zap_clrdtmf(p->z);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
			}
			return NULL;
		} else {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_2 "Unknown extension '%s' in context '%s' requested\n", exten, chan->context);
			sleep(2);
			res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_INFO);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = ast_streamfile(chan, "ss-noservice", chan->language);
			if (res >= 0)
				ast_waitstream(chan, "");
			res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
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
			if (res < 0) {
				ast_log(LOG_DEBUG, "waitfordigit returned < 0...\n");
				res = tone_zone_play_tone(zap_fd(p->z), -1);
				ast_hangup(chan);
				return NULL;
			} else if (res == 0) {
				ast_log(LOG_DEBUG, "not enough digits...\n");
				res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
				zt_wait_event(zap_fd(p->z));
				ast_hangup(chan);
				return NULL;
			} else {
				exten[len++]=res;
            			exten[len] = '\0';
			}
			if (!ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(zap_fd(p->z), -1);
			if (ast_exists_extension(chan, chan->context, exten, 1, p->callerid)) {
				res = tone_zone_play_tone(zap_fd(p->z), -1);
				strncpy(chan->exten, exten, sizeof(chan->exten)-1);
				if (strlen(p->callerid)) {
					if (!p->hidecallerid)
						chan->callerid = strdup(p->callerid);
					chan->hidden_callerid = strdup(p->callerid);
				}
				chan->state = AST_STATE_RING;
				zt_enable_ec(p);
				res = ast_pbx_run(chan);
				if (res) {
					ast_log(LOG_WARNING, "PBX exited non-zero\n");
					res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
				}						
				return NULL;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Disabling call waiting on %s\n", chan->name);
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				ioctl(zap_fd(p->z),ZT_CONFDIAG,&len);
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
					
			} else if (!strcmp(exten,"*8#")){
				/* Scan all channels and see if any there
				 * ringing channqels with that have call groups
				 * that equal this channels pickup group  
				 */
				struct zt_pvt *chan_pvt=iflist;
				while(chan_pvt!=NULL){
					if((p!=chan_pvt)&&
					  ((p->pickupgroup & chan_pvt->callgroup)==chan_pvt->callgroup)&&
					  (chan_pvt->owner && (chan_pvt->owner->state==AST_STATE_RING || chan_pvt->owner->state == AST_STATE_RINGING))
					  ){
					  	/* Switch us from Third call to Call Wait */
						p->callwaitindex = p->thirdcallindex;
						p->thirdcallindex = -1;
						ast_log(LOG_DEBUG, "Call pickup on chan %s\n",chan_pvt->owner->name);
						p->needanswer[zt_get_index(chan, p, 1)]=1;
						zt_enable_ec(p);
						if(ast_channel_masquerade(chan_pvt->owner,p->owner))
							printf("Error Masquerade failed on call-pickup\n");
						/* Do not hang up masqueraded channel */
						return NULL;
					}
					chan_pvt=chan_pvt->next;
				}
				ast_log(LOG_DEBUG, "No call pickup possible...\n");
				res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
				zt_wait_event(zap_fd(p->z));
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
				res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (p->transfer && !strcmp(exten, ast_parking_ext()) && 
						(zt_get_index(chan, p, 1) == p->thirdcallindex) &&
						p->owners[p->normalindex]->bridge) {
				/* This is a three way call, the main call being a real channel, 
					and we're parking the first call. */
				ast_masq_park_call(p->owners[p->normalindex]->bridge, chan);
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
				res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (!strcmp(exten, "*0")) {
				int index = zt_get_index(chan, p, 0);
				struct ast_channel *nbridge = 
					p->owners[p->normalindex]->bridge;
				struct zt_pvt *pbridge = NULL;
				  /* set up the private struct of the bridged one, if any */
				if (nbridge) pbridge = nbridge->pvt->pvt;
				if ((p->thirdcallindex > -1) &&
				    (index == p->thirdcallindex) &&
				    nbridge && 
				    (!strcmp(nbridge->type,"Zap")) &&
				    ISTRUNK(pbridge)) {
					int func = ZT_FLASH;
					/* flash hookswitch */
					if (ioctl(zap_fd(pbridge->z),ZT_HOOK,&func) == -1) {
						ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n", 
							nbridge->name, strerror(errno));
					}				
					p->owner = p->owners[p->normalindex];
					ast_hangup(chan);
					return NULL;
				} else {
					tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
					zt_wait_event(zap_fd(p->z));
					tone_zone_play_tone(zap_fd(p->z), -1);
					p->owner = p->owners[p->normalindex];
					ast_hangup(chan);
					return NULL;
				}					
			} else if (!ast_canmatch_extension(chan, chan->context, exten, 1, chan->callerid) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Can't match %s from '%s' in context %s\n", exten, chan->callerid ? chan->callerid : "<Unknown Caller>", chan->context);
				break;
			}
			timeout = gendigittimeout;
			if (len && !ast_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(zap_fd(p->z), -1);
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
				for(;;) {	
					i = ZT_IOMUX_READ | ZT_IOMUX_SIGEVENT;
					if ((res = ioctl(zap_fd(p->z), ZT_IOMUX, &i)))	{
						ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
						callerid_free(cs);
						ast_hangup(chan);
						return NULL;
					}
					if (i & ZT_IOMUX_SIGEVENT) {
						res = zt_get_event(zap_fd(p->z));
						ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
						res = 0;
						break;
					} else if (i & ZT_IOMUX_READ) {
						res = read(zap_fd(p->z), buf, sizeof(buf));
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
						res = callerid_feed(cs, buf, res);
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
			chan->hidden_callerid = strdup(cid);
		}
		chan->state = AST_STATE_RING;
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
		res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
		if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = tone_zone_play_tone(zap_fd(p->z), ZT_TONE_CONGESTION);
	if (res < 0)
			ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
	return NULL;
}

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
	case ZT_EVENT_RINGOFFHOOK:
		if (i->inalarm) break;
		/* Got a ring/answer.  What kind of channel are we? */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			if (i->immediate) {
				zt_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(zap_fd(i->z), ZT_TONE_RINGTONE);
				chan = zt_new(i, AST_STATE_RING, 1, 0, 0);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), ZT_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = zt_new(i, AST_STATE_DOWN, 0, 0, 0);
				if (chan) {
					if (i->adsi) {
						/* Wait 700 ms */
						usleep(700 * 1000);
						/* Clear anything waiting */
						while(read(zap_fd(i->z), &res, sizeof(res)) > 0);
						adsi_channel_init(chan);
					}
					res = tone_zone_play_tone(zap_fd(i->z), ZT_TONE_DIALTONE);
					if (res < 0) 
						ast_log(LOG_WARNING, "Unable to play dialtone on channel %d\n", i->channel);
					if (pthread_create(&threadid, &attr, ss_thread, chan)) {
						ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
						res = tone_zone_play_tone(zap_fd(i->z), ZT_TONE_CONGESTION);
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
		case SIG_EM:
				/* Check for callerid, digits, etc */
				chan = zt_new(i, AST_STATE_RING, 0, 0, 0);
				if (pthread_create(&threadid, &attr, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), ZT_TONE_CONGESTION);
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
			res = tone_zone_play_tone(zap_fd(i->z), ZT_TONE_CONGESTION);
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
	case ZT_EVENT_WINKFLASH:
	case ZT_EVENT_ONHOOK:
		/* Back on hook.  Hang up. */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
		case SIG_FEATD:
		case SIG_EM:
		case SIG_EMWINK:
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
			zt_disable_ec(i);
			res = tone_zone_play_tone(zap_fd(i->z), -1);
			zt_set_hook(zap_fd(i->z), ZT_ONHOOK);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know hwo to handle on hook with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(zap_fd(i->z), -1);
			return -1;
		}
		break;
	}
	return 0;
}

static void *do_monitor(void *data)
{
	fd_set efds;
	int n, res;
	struct zt_pvt *i;
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
		i = iflist;
		while(i) {
			if (FD_ISSET(zap_fd(i->z), &efds)) 
				ast_log(LOG_WARNING, "Descriptor %d appears twice?\n", zap_fd(i->z));
			if (!i->owner) {
				/* This needs to be watched, as it lacks an owner */
				FD_SET(zap_fd(i->z), &efds);
				if (zap_fd(i->z) > n)
					n = zap_fd(i->z);
			}
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_pthread_mutex_unlock(&iflock);
		
		pthread_testcancel();
		/* Wait indefinitely for something to happen */
		res = select(n + 1, NULL, NULL, &efds, NULL);
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
		i = iflist;
		while(i) {
			if (FD_ISSET(zap_fd(i->z), &efds)) {
				if (i->owner) {
					ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d)...\n", zap_fd(i->z));
					i = i->next;
					continue;
				}
				res = zt_get_event(zap_fd(i->z));
				if (option_debug)
					ast_log(LOG_DEBUG, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
				handle_init_event(i, res);
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
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}
	/* Start a new monitor */
	if (pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0) {
		ast_pthread_mutex_unlock(&monlock);
		ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
		return -1;
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
		p->owner->softhangup = 1;
	}
	for (i = 0; i < 3; i++) {
		if (p->owners[i]) {
			ioctlflag = 0;
			p->owners[i]->softhangup = 1;
		}
	}
	if (ioctlflag) {
		res = zt_set_hook(zap_fd(p->z), ZT_ONHOOK);
		if (res < 0) {
			ast_log(LOG_ERROR, "Unable to hangup chan_zap channel %d (ioctl)\n", p->channel);
			return -1;
		}
	}

	return 0;
}


static struct zt_pvt *mkintf(int channel, int signalling)
{
	/* Make a zt_pvt structure for this interface */
	struct zt_pvt *tmp = NULL, *tmp2,  *prev = NULL;
	char fn[80];
#if 1
	struct zt_bufferinfo bi;
#endif	
	struct zt_spaninfo si;
	int res;
	int span;
	int here = 0;
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
		tmp->next = tmp2;
		if (!prev) {
			iflist = tmp;
		} else {
			prev->next = tmp;
		}
	}

	if (tmp) {
		snprintf(fn, sizeof(fn), "%d", channel);
		/* Open non-blocking */
		if (!here)
			tmp->z = zap_open(fn, 1);
		/* Allocate a zapata structure */
		if (!tmp->z) {
			ast_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
			free(tmp);
			return NULL;
		}
		res = ioctl(zap_fd(tmp->z), ZT_GET_PARAMS, &p);
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
#ifdef ZAPATA_PRI
		if (signalling == SIG_PRI) {
			int offset;
			int numchans;
			int dchannel;
			offset = 1;
			if (ioctl(zap_fd(tmp->z), ZT_AUDIOMODE, &offset)) {
				ast_log(LOG_ERROR, "Unable to set audio mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
				return NULL;
			}
			if (span >= NUM_SPANS) {
				ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
				free(tmp);
				return NULL;
			} else {
				si.spanno = 0;
				if (ioctl(zap_fd(tmp->z),ZT_SPANSTAT,&si) == -1) {
					ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
					free(tmp);
					return NULL;
				} 
				if (si.totalchans == 31) { /* if it's an E1 */
					dchannel = 16;
					numchans = 31;
				} else {
					dchannel = 24;
					numchans = 24;
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
					pris[span].nodetype = pritype;
					pris[span].switchtype = switchtype;
					pris[span].chanmask[offset] |= MASK_AVAIL;
					pris[span].pvt[offset] = tmp;
					pris[span].channels = numchans;
					pris[span].dchannel = dchannel;
					
					tmp->pri = &pris[span];
					tmp->call = NULL;
				} else {
					ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", offset);
					free(tmp);
					return NULL;
				}
			}
		}
#endif		
		/* Adjust starttime on loopstart and kewlstart trunks to reasonable values */
		if ((signalling == SIG_FXSKS) || (signalling == SIG_FXSLS) ||
		    (signalling == SIG_EM) || (signalling == SIG_EMWINK) ||
			(signalling == SIG_FEATD)) {
			p.starttime = 250;
			res = ioctl(zap_fd(tmp->z), ZT_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters\n");
				free(tmp);
				return NULL;
			}
		}
#if 0
		res = fcntl(zap_fd(tmp->z), F_GETFL);
		if (res >= 0) {
			res |= O_NONBLOCK;
			if (fcntl(zap_fd(tmp->z), F_SETFL, res))
				ast_log(LOG_WARNING, "Unable to set non-blocking mode on channel %d\n", channel);
		} else
			ast_log(LOG_WARNING, "Unable to read flags on channel %d\n", channel);
#endif			
#if 1
		if (!here) {
			res = ioctl(zap_fd(tmp->z), ZT_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
				bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
				bi.numbufs = 4;
				res = ioctl(zap_fd(tmp->z), ZT_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", channel);
				}
			} else
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", channel);
		}
#endif
		tmp->immediate = immediate;
		tmp->sig = signalling;
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
		tmp->echocancel = echocancel;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->hidecallerid = tmp->permhidecallerid;
		tmp->channel = channel;
		tmp->stripmsd = stripmsd;
		tmp->use_callerid = use_callerid;
		strncpy(tmp->accountcode, accountcode, sizeof(tmp->accountcode)-1);
		tmp->amaflags = amaflags;
		if (!here) {
			tmp->callwaitindex = -1;
			tmp->normalindex = -1;
			tmp->thirdcallindex = -1;
			tmp->normalindex = -1;
			tmp->confno = -1;
			tmp->pseudo = NULL;
			tmp->pseudochan = 0;
		}
		tmp->transfer = transfer;
		pthread_mutex_init(&tmp->lock, NULL);
		strncpy(tmp->language, language, sizeof(tmp->language)-1);
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		strncpy(tmp->callerid, callerid, sizeof(tmp->callerid)-1);
		tmp->group = cur_group;
		tmp->callgroup=cur_callergroup;
		tmp->pickupgroup=cur_pickupgroup;
		tmp->rxgain = rxgain;
		tmp->txgain = txgain;
		set_actual_gain(zap_fd(tmp->z), 0, tmp->rxgain, tmp->txgain);
		zap_digitmode(tmp->z, ZAP_DTMF /* | ZAP_MUTECONF */);
		conf_clear(tmp);
		if (!here) {
			if (signalling != SIG_PRI) 
				/* Hang it up to be sure it's good */
				zt_set_hook(zap_fd(tmp->z), ZT_ONHOOK);
		}
		tmp->inalarm = 0;
		si.spanno = 0;
		if (ioctl(zap_fd(tmp->z),ZT_SPANSTAT,&si) == -1) {
			ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
			free(tmp);
			return NULL;
		}
		if (si.alarms) tmp->inalarm = 1;
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
		
	/* If no owner definitely available */
	if (!p->owner) {
		/* Trust PRI */
#ifdef ZAPATA_PRI
		if (p->pri)
			return 1;
#endif
		if ((p->sig == SIG_FXSKS) || (p->sig == SIG_FXSLS) ||
			(p->sig == SIG_FXSGS))
			return 1;
		/* Check hook state */
		res = ioctl(zap_fd(p->z), ZT_GET_PARAMS, &par);
		if (res) {
			ast_log(LOG_WARNING, "Unable to check hook state on channel %d\n", p->channel);
		} else if (par.rxisoffhook) {
			ast_log(LOG_DEBUG, "Channel %d off hook, can't use\n", p->channel);
			/* Not available when the other end is off hook */
			return 0;
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

	if (p->callwaitindex > -1) {
		/* If there is already a call waiting call, then we can't take a second one */
		return 0;
	}
	
	if ((p->owner->state != AST_STATE_UP) &&
		(p->owner->state != AST_STATE_RINGING)) {
		/* If the current call is not up, then don't allow the call */
		return 0;
	}
	if ((p->thirdcallindex > -1) && (p->owner == p->owners[p->thirdcallindex])) {
		/* Can't take a call wait when the three way calling hasn't been merged yet. */
		return 0;
	}
	/* We're cool */
	return 1;
}

static struct ast_channel *zt_request(char *type, int format, void *data)
{
	int oldformat;
	int groupmatch = 0;
	int channelmatch = -1;
	struct zt_pvt *p;
	struct ast_channel *tmp = NULL;
	char *dest=NULL;
	int x;
	char *s;
	int callwait;
	
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
		s = strtok(dest  + 1, "/");
		if (sscanf(s, "%d", &x) != 1) {
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", (char *)data);
			free(dest);
			return NULL;
		}
		groupmatch = 1 << x;
	} else {
		s = strtok(dest, "/");
		if (sscanf(s, "%d", &x) != 1) {
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
			tmp = zt_new(p, AST_STATE_RESERVED, 0, p->owner ? 1 : 0, 0);
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
	int start, finish,x;
	int group = 0;
	copy = strdup(s);
	if (!copy) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return 0;
	}
	piece = strtok(copy, ",");
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
		piece = strtok(NULL, ",");
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
	for (x=pri->channels-1;x>0;x--) {
		if (pri->pvt[x] && !pri->pvt[x]->owner)
			return x;
	}
	return 0;
}

static int pri_fixup(struct zt_pri *pri, int channel, q931_call *c)
{
	int x;
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
				pri->pvt[channel]->owner->pvt->pvt = pri->pvt[channel];
				pri->pvt[channel]->owner->fds[0] = zap_fd(pri->pvt[channel]->z);
				pri->pvt[channel]->call = pri->pvt[x]->call;
				
				/* Free up the old channel, now not in use */
				pri->pvt[x]->owner = NULL;
				pri->pvt[x]->call = NULL;
			}
			return channel;
		}
	}
	return 0;
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
	struct ast_channel *c;
	struct timeval tv, *next;
	for(;;) {
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		FD_SET(pri->fd, &rfds);
		FD_SET(pri->fd, &efds);
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
		}
		res = select(pri->fd + 1, &rfds, NULL, &efds, next ? &tv : NULL);
		pthread_mutex_lock(&pri->lock);
		if (!res) {
			/* Just a timeout, run the scheduler */
			pri_schedule_run(pri->pri);
		} else if (res > -1) {
			e = pri_check_event(pri->pri);
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
								pri->pvt[chan]->owner->softhangup = 1;
						}
					} else {
						if (option_verbose > 2)
							ast_verbose("Restart on requested on entire span %d\n", pri->span);
						for (x=1;x <= pri->channels;x++)
							if ((x != pri->dchannel) && (pri->pvt[x]->owner))
								pri->pvt[x]->owner->softhangup = 1;
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
							pri->pvt[chan]->owner->softhangup = 1;
							chan = 0;
						}
					}
					if (!chan && (e->ring.flexible))
						chan = pri_find_empty_chan(pri);
					if (chan) {
						/* Get caller ID */
						if (pri->pvt[chan]->use_callerid) 
							strncpy(pri->pvt[chan]->callerid, e->ring.callingnum, sizeof(pri->pvt[chan]->callerid)-1);
						else
							strcpy(pri->pvt[chan]->callerid, "");
						/* Get called number */
						if (strlen(e->ring.callednum)) {
							strncpy(pri->pvt[chan]->exten, e->ring.callednum, sizeof(pri->pvt[chan]->exten)-1);
						} else
							strcpy(pri->pvt[chan]->exten, "s");
						/* Make sure extension exists */
						if (ast_exists_extension(NULL, pri->pvt[chan]->context, pri->pvt[chan]->exten, 1, pri->pvt[chan]->callerid)) {
							/* Setup law */
							int law;
							if (e->ring.layer1 == PRI_LAYER_1_ALAW)
								law = ZT_LAW_ALAW;
							else
								law = ZT_LAW_MULAW;
							res = ioctl(zap_fd(pri->pvt[chan]->z), ZT_SETLAW, &law);
							if (res < 0) 
								ast_log(LOG_WARNING, "Unable to set law on channel %d\n", pri->pvt[chan]->channel);
							/* Start PBX */
							pri->pvt[chan]->call = e->ring.call;
							c = zt_new(pri->pvt[chan], AST_STATE_RING, 1, 0, 0);
							if (c) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Accepting call from '%s' to '%s' on channel %d, span %d\n",
										e->ring.callingnum, pri->pvt[chan]->exten, chan, pri->span);
								pri_acknowledge(pri->pri, e->ring.call, chan, 0);
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
						} else
							pri->pvt[chan]->needringing[0] =1;
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
						chan = pri_fixup(pri, chan, e->ringing.call);
						if (!chan) {
							ast_log(LOG_WARNING, "Ring requested on channel %d not in use on span %d\n", chan, pri->span);
							chan = 0;
						} else
							pri->pvt[chan]->needanswer[0] =1;
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
								if (option_verbose > 3) 
									ast_verbose(VERBOSE_PREFIX_3, "Channel %d, span %d got hangup\n", chan, pri->span);
								pri->pvt[chan]->owner->softhangup = 1;
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
						ast_log(LOG_WARNING, "Hanngup ACK requested on unconfigured channel %d span %d\n", chan, pri->span);
						chan = 0;
					}
					if (chan) {
						chan = pri_fixup(pri, chan, e->hangup.call);
						if (chan) {
							if (pri->pvt[chan]->owner) {
								if (option_verbose > 3) 
									ast_verbose(VERBOSE_PREFIX_3, "Channel %d, span %d got hangup ACK\n", chan, pri->span);
								pri->pvt[chan]->call = NULL;
							}
						}
					}
					break;
				case PRI_EVENT_CONFIG_ERR:
					ast_log(LOG_WARNING, "PRI Error: %s\n", e->err.err);
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
		} else {
			if (errno != EINTR) 
				ast_log(LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));
		}
		pthread_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

static int start_pri(struct zt_pri *pri)
{
	char filename[80];
	int res;
	ZT_PARAMS p;
	ZT_BUFFERINFO bi;
	snprintf(filename, sizeof(filename), "/dev/zap/%d", pri->offset + pri->dchannel);
	pri->fd = open(filename, O_RDWR, 0600);
	if (pri->fd < 0) {
		ast_log(LOG_ERROR, "Unable to open D-channel %s (%s)\n", filename, strerror(errno));
		return -1;
	}
	res = ioctl(pri->fd, ZT_GET_PARAMS, &p);
	if (res) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "Unable to get parameters for D-channel %s (%s)\n", filename, strerror(errno));
		return -1;
	}
	if (p.sigtype != ZT_SIG_HDLCFCS) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "D-channel %s is not in HDLC/FCS mode.  See /etc/tormenta.conf\n", filename);
		return -1;
	}
	bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.numbufs = 8;
	bi.bufsize = 1024;
	if (ioctl(pri->fd, ZT_SET_BUFINFO, &bi)) {
		ast_log(LOG_ERROR, "Unable to set appropriate buffering on %s\n", filename);
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

static int handle_pri_no_debug(int fd, int argc, char *argv[])
{
	int span;
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

#if 0
static struct ast_cli_entry cli_show_channel = {
	{"zap", "show", "channel", NULL}, zap_show_channel, "Show the detailed status of a single zapata channel", show_channel_usage
};
#endif

static struct ast_cli_entry pri_debug = {
	{ "pri", "debug", "span", NULL }, handle_pri_debug, "Enables PRI debugging on a span", pri_debug_help, complete_span 
};

static struct ast_cli_entry pri_no_debug = {
	{ "pri", "no", "debug", "span", NULL }, handle_pri_no_debug, "Enables PRI debugging on a span", pri_no_debug_help, complete_span };

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
#define FORMAT "%3d %-10.10s %-10.10s %-10.10s\n"
#define FORMAT2 "%3s %-10.10s %-10.10s %-10.10s\n"
	struct zt_pvt *tmp = NULL;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	ast_pthread_mutex_lock(&iflock);
	ast_cli(fd, FORMAT2, "Chan. Num.", "Extension", "Context", "Language");
	
	tmp = iflist;
	while (tmp) {
		ast_cli(fd, FORMAT, tmp->channel, tmp->exten, tmp->context, tmp->language);
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

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	channel = atoi(argv[3]);

	ast_pthread_mutex_lock(&iflock);
	tmp = iflist;
	while (tmp) {
		if (tmp->channel == channel) {
			ast_cli(fd, "Channel: %d\n", tmp->channel);
			ast_cli(fd, "Span: %d\n", tmp->span);
			ast_cli(fd, "Extension: %s\n", tmp->exten);
			ast_cli(fd, "Context: %s\n", tmp->context);
			ast_cli(fd, "Caller ID string: %s\n", tmp->callerid);
			ast_cli(fd, "Destroy: %d\n", tmp->destroy);
			ast_cli(fd, "Signalling Type: %s\n", sig2str(tmp->sig));
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
	int start, finish,x;
	int y;

#ifdef ZAPATA_PRI
	int offset;

	memset(pris, 0, sizeof(pris));
	for (y=0;y<NUM_SPANS;y++) {
		pris[y].offset = -1;
		pris[y].fd = -1;
	}
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
			chan = strtok(v->value, ",");
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
					tmp = mkintf(x, cur_signalling);
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
				chan = strtok(NULL, ",");
			}
		} else if (!strcasecmp(v->name, "usecallerid")) {
			use_callerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "threewaycalling")) {
			threewaycalling = ast_true(v->value);
		} else if (!strcasecmp(v->name, "adsi")) {
			adsi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfer")) {
			transfer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancel")) {
			echocancel = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			hidecallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			callwaiting = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			callwaitingcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
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
			else if (!strcasecmp(v->value, "euroisdn"))
				switchtype = PRI_SWITCH_EUROISDN_E1;
			else {
				ast_log(LOG_ERROR, "Unknown switchtype '%s'\n", v->value);
				ast_destroy(cfg);
				ast_pthread_mutex_unlock(&iflock);
				unload_module();
				return -1;
			}
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
				ast_softhangup(p->owner);
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
			if (p->z)
				zap_close(p->z);
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

static int reload_zt(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct zt_pvt *tmp;
	struct zt_pvt *prev = NULL;
	char *chan;
	int start, finish,x;

	/* Some crap that needs to be reinitialized on the reload */
	strcpy(context, "default");
	language[0] = '\0'; 
	use_callerid = 1;
	cur_signalling = -1;
	cur_group = 0;
	cur_callergroup = 0;
	cur_pickupgroup = 0;
	immediate = 0;
	stripmsd = 0;
	callwaiting = 0;
	callwaitingcallerid = 0;
	hidecallerid = 0;
	threewaycalling = 0;
	transfer = 0;
	rxgain = 0.0;
	txgain = 0.0;
	firstdigittimeout = 16000;
	gendigittimeout = 8000;
	amaflags = 0;
	adsi = 0;
	strncpy(accountcode, "", sizeof(accountcode)-1);
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
			chan = strtok(v->value, ",");
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
				chan = strtok(NULL, ",");
			}
		} else if (!strcasecmp(v->name, "usecallerid")) {
			use_callerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "threewaycalling")) {
			threewaycalling = ast_true(v->value);
		} else if (!strcasecmp(v->name, "transfer")) {
			transfer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancel")) {
			echocancel = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			hidecallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			callwaiting = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			callwaitingcallerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
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

static int zt_sendtext(struct ast_channel *c, char *text)
{
#define	END_SILENCE_LEN 400

	unsigned char *buf,*mybuf;
	struct zt_pvt *p = c->pvt->pvt;
	fd_set wfds,efds;
	int size,res,fd,len;

	if (!p->tdd) return(0);  /* if not in TDD mode, just return */
	buf = malloc(((strlen(text) + 1) * TDD_BYTES_PER_CHAR) + END_SILENCE_LEN);
	if (!buf) {
		ast_log(LOG_ERROR, "MALLOC FAILED\n");
		return -1;
	}
	mybuf = buf;
	len = tdd_generate(p->tdd,buf,text);
	if (len < 1) {
		ast_log(LOG_ERROR, "TDD generate (len %d) failed!!\n",strlen(text));
		free(mybuf);
		return -1;
	}
	memset(buf + len,0x7f,END_SILENCE_LEN);
	len += END_SILENCE_LEN;
	if (c != p->owner)   /* if in three-way */
		fd = zap_fd(p->pseudo);
	else
		fd = zap_fd(p->z);
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
