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
#include <asterisk/cli.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/tor.h>
#include <zap.h>
#include <math.h>
#include <tonezone.h>
#ifdef TORMENTA_PRI
#include <libpri.h>
#endif

/* 
   XXX 
   XXX   We definitely need to lock the private structure in tor_read and such 
   XXX  
 */

#ifdef TORMENTA_PRI
static char *desc = "Tormenta (Zapata) Channelized T1/PRI Driver";
static char *tdesc = "Tormenta T1//PRI Driver";
#else
static char *desc = "Tormenta (Zapata) Channelized T1 Driver";
static char *tdesc = "Tormenta T1 Driver";
#endif
static char *type = "Tor";
static char *config = "tormenta.conf";

#define SIG_EM		0x1
#define SIG_EMWINK 	0x11
#define SIG_FEATD	0X21
#define SIG_FXSLS	0x2
#define SIG_FXSGS	0x3
#define SIG_FXSKS	0x4
#define SIG_FXOLS	0x5
#define SIG_FXOGS	0x6
#define SIG_FXOKS	0x7
#define SIG_PRI		0x8

#define NUM_SPANS 	2

static char context[AST_MAX_EXTENSION] = "default";
static char callerid[256] = "";

/* Keep certain dial patterns from turning off dialtone */
#define AST_MAX_DIAL_PAT 32

static char keepdialpat[AST_MAX_DIAL_PAT][10];
static int dialpats = 0;

static char language[MAX_LANGUAGE] = "";

static int use_callerid = 1;

static int cur_signalling = -1;

static int cur_group = 0;

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

/* Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/* How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

static int usecnt =0;
static pthread_mutex_t usecnt_lock = PTHREAD_MUTEX_INITIALIZER;

/* Protect the interface list (of tor_pvt's) */
static pthread_mutex_t iflock = PTHREAD_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static pthread_mutex_t monlock = PTHREAD_MUTEX_INITIALIZER;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = 0;

static int restart_monitor(void);

static inline int tor_get_event(int fd)
{
	/* Avoid the silly tor_getevent which ignores a bunch of events */
	int j;
	if (ioctl(fd, TOR_GETEVENT, &j) == -1) return -1;
	return j;
}

static inline int tor_wait_event(int fd)
{
	/* Avoid the silly tor_waitevent which ignores a bunch of events */
	int i,j=0;
	i = TOR_IOMUX_SIGEVENT;
	if (ioctl(fd, TOR_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, TOR_GETEVENT, &j) == -1) return -1;
	return j;
}

/* Chunk size to read -- we use the same size as the chunks that the zapata library uses.  */   
#define READ_SIZE 204

#define MASK_AVAIL		(1 << 0)		/* Channel available for PRI use */
#define MASK_INUSE		(1 << 1)		/* Channel currently in use */

#define CALLWAITING_SILENT_SAMPLES	( (300 * 8) / READ_SIZE) /* 300 ms */
#define CALLWAITING_REPEAT_SAMPLES	( (10000 * 8) / READ_SIZE) /* 300 ms */

struct tor_pvt;


#ifdef TORMENTA_PRI
struct tor_pri {
	pthread_t master;			/* Thread of master */
	pthread_mutex_t lock;		/* Mutex */
	int nodetype;				/* Node type */
	int switchtype;				/* Type of switch to emulate */
	struct pri *pri;
	int debug;
	int fd;
	int up;
	int offset;
	int span;
	int chanmask[24];			/* Channel status */
	struct tor_pvt *pvt[24];	/* Member channel pvt structs */
	struct tor_channel *chan[24];	/* Channels on each line */
};

static struct tor_pri pris[NUM_SPANS];

static int pritype = PRI_CPE;

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
/*
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW)
*/
#endif

static inline int pri_grab(struct tor_pri *pri)
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

static inline void pri_rel(struct tor_pri *pri)
{
	ast_pthread_mutex_unlock(&pri->lock);
}

static int switchtype = PRI_SWITCH_NI2;

#endif

static struct tor_pvt {
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
	struct tor_pvt *next;			/* Next channel in list */
	char context[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char callerid[AST_MAX_EXTENSION];
	char callwaitcid[AST_MAX_EXTENSION];
	char dtmfq[AST_MAX_EXTENSION];
	struct ast_frame f;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	int group;
	int immediate;				/* Answer before getting digits? */
	int channel;				/* Channel Number */
	int span;					/* Span number */
	int dialing;
	int use_callerid;			/* Whether or not to use caller id on this channel */
	int hidecallerid;
	int permhidecallerid;		/* Whether to hide our outgoing caller ID or not */
	int callwaitingrepeat;		/* How many samples to wait before repeating call waiting */
	unsigned char *cidspill;
	int cidpos;
	int cidlen;
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
	DIAL_OPERATION dop;
	struct tor_confinfo conf;	/* Saved state of conference */
	struct tor_confinfo conf2;	/* Saved state of alternate conference */
	int confno;					/* Conference number */
	ZAP *pseudo;				/* Pseudo channel FD */
	int pseudochan;				/* Pseudo channel */
#ifdef TORMENTA_PRI
	struct tor_pri *pri;
	q931_call *call;
#endif	
} *iflist = NULL;

#define FIRST_PSEUDO 49

#define INTHREEWAY(p) ((p->normalindex > -1) && (p->thirdcallindex > -1) && \
		(p->owner == p->owners[p->normalindex]))

static int alloc_pseudo(struct tor_pvt *p)
{
	int x;
	ZAP *z;
	int res;
	BUFFER_INFO bi;
	char fn[256];
	if (p->pseudo || p->pseudochan){
		ast_log(LOG_WARNING, "Already have a pseudo fd: %d, chan: %d\n",
			zap_fd(p->pseudo), p->pseudochan);
		return -1;
	}
	for (x=FIRST_PSEUDO;;x++) {
		snprintf(fn, sizeof(fn), "/dev/tor/%d", x);
		z = zap_open(fn, 1);
		if (!z) {
			if (errno != EBUSY) {
				ast_log(LOG_WARNING, "Unable to open %s: %s\n", fn, strerror(errno));
				return -1;
			}
		} else {
			res = ioctl(zap_fd(z), TOR_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = POLICY_IMMEDIATE;
				bi.rxbufpolicy = POLICY_IMMEDIATE;
				bi.numbufs = 4;
				res = ioctl(zap_fd(z), TOR_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", x);
				}
			} else
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", x);
			p->pseudo = z;
			p->pseudochan = x;
			if (option_debug)
				ast_log(LOG_DEBUG, "Allocated pseudo channel %d on FD %d\n", p->pseudochan, zap_fd(p->pseudo));
			return 0;
		}
	}
	/* Never reached */
	return 0;
}

static int unalloc_pseudo(struct tor_pvt *p)
{
	if (p->pseudo)
		zap_close(p->pseudo);
	if (option_debug)
		ast_log(LOG_DEBUG, "Released pseudo channel %d\n", p->pseudochan);
	p->pseudo = NULL;
	p->pseudochan = 0;
	return 0;
}

static int tor_digit(struct ast_channel *ast, char digit)
{
	DIAL_OPERATION zo;
	struct tor_pvt *p;
	int res;
	zo.op = TOR_DIAL_OP_APPEND;
	zo.dialstr[0] = 'T';
	zo.dialstr[1] = digit;
	zo.dialstr[2] = 0;
	p = ast->pvt->pvt;
	if ((res = ioctl(zap_fd(p->z), TOR_DIAL, &zo)))
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

static int conf_set(struct tor_pvt *p, int req, int force)
{
	/* Set channel to given conference, -1 to allocate one */
	TOR_CONFINFO ci;
	TOR_CONFINFO cip;
	int res;
	if ((p->confno > -1) && (p->confno != req) && (!force)) {
		ast_log(LOG_WARNING, "Channel %d already has conference %d allocated\n", p->channel, p->confno);
		return -1;
	}
	ci.chan = 0;
	ci.confno = 0;
	/* Check current conference stuff */
	res = ioctl(zap_fd(p->z), TOR_GETCONF, &ci);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to get conference info on channel %d: %s\n",
			p->channel, strerror(errno));
		return -1;
	}
	if (!force && ci.confmode && (ci.confno != p->confno)) {
		ast_log(LOG_WARNING, "Channel %d is already in a conference (%d, %d) we didn't create (req = %d)\n", p->channel, ci.confno, ci.confmode, req);
		return -1;
	}
	ci.chan = 0;
	ci.confno = req;
	ci.confmode = TOR_CONF_REALANDPSEUDO | TOR_CONF_TALKER | TOR_CONF_LISTENER | TOR_CONF_PSEUDO_LISTENER | TOR_CONF_PSEUDO_TALKER;
	res = ioctl(zap_fd(p->z), TOR_SETCONF, &ci);
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
		cip.confmode = TOR_CONF_CONF | TOR_CONF_TALKER | TOR_CONF_LISTENER;
		
		res = ioctl(zap_fd(p->pseudo), TOR_SETCONF, &cip);
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
			cip.confmode = TOR_CONF_NORMAL;
			res = ioctl(zap_fd(p->pseudo), TOR_SETCONF, &cip);
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

static int three_way(struct tor_pvt *p)
{
	ast_log(LOG_DEBUG, "Setting up three way call\n");
	return conf_set(p, p->confno, 0);
}

static int conf_clear(struct tor_pvt *p)
{
	TOR_CONFINFO ci;
	int res;
	ci.confmode = TOR_CONF_NORMAL;
	ci.chan = 0;
	ci.confno = 0;
	res = ioctl(zap_fd(p->z), TOR_SETCONF, &ci);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to clear conference info on channel %d: %s\n",
			p->channel, strerror(errno));
		return -1;
	}
	p->confno = -1;
	return 0;
}

static void tor_enable_ec(struct tor_pvt *p)
{
	int x;
	int res;
	if (p->echocancel) {
		x = 1;
		res = ioctl(zap_fd(p->z), TOR_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		else
			ast_log(LOG_DEBUG, "Enabled echo cancellation on channel %d\n", p->channel);
	}
}

static void tor_disable_ec(struct tor_pvt *p)
{
	int x;
	int res;
	if (p->echocancel) {
		x = 0;
		res = ioctl(zap_fd(p->z), TOR_ECHOCANCEL, &x);
		if (res) 
			ast_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d\n", p->channel);
		else
			ast_log(LOG_DEBUG, "disabled echo cancellation on channel %d\n", p->channel);
	}
}

static int tor_get_index(struct ast_channel *ast, struct tor_pvt *p, int nullok)
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
	struct	tor_gains g;
	float ltxgain;
	float lrxgain;
	int j,k;
	g.chan = chan;
	  /* caluculate linear value of tx gain */
	ltxgain = pow(10.0,txgain / 20.0);
	  /* caluculate linear value of rx gain */
	lrxgain = pow(10.0,rxgain / 20.0);
	for (j=0;j<256;j++) {
		k = (int)(((float)ast_mulaw[j]) * lrxgain);
		if (k > 32767) k = 32767;
		if (k < -32767) k = -32767;
		g.rxgain[j] = ast_lin2mu[k + 32768];
		k = (int)(((float)ast_mulaw[j]) * ltxgain);
		if (k > 32767) k = 32767;
		if (k < -32767) k = -32767;
		g.txgain[j] = ast_lin2mu[k + 32768];
	}
		
	  /* set 'em */
	return(ioctl(fd,TOR_SETGAINS,&g));
}

static inline int tor_set_hook(int fd, int hs)
{
	int x, res;
	x = hs;
	res = ioctl(fd, TOR_HOOK, &x);
	if (res < 0) 
		ast_log(LOG_WARNING, "tor hook failed: %s\n", strerror(errno));
	return res;
}

static int save_conference(struct tor_pvt *p)
{
	struct tor_confinfo c;
	int res;
	if (p->conf.confmode) {
		ast_log(LOG_WARNING, "Can't save conference -- already in use\n");
		return -1;
	}
	p->conf.chan = 0;
	res = ioctl(zap_fd(p->z), TOR_GETCONF, &p->conf);
	if (res) {
		ast_log(LOG_WARNING, "Unable to get conference info: %s\n", strerror(errno));
		p->conf.confmode = 0;
		return -1;
	}
	c.chan = 0;
	c.confno = 0;
	c.confmode = TOR_CONF_NORMAL;
	res = ioctl(zap_fd(p->z), TOR_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "Unable to set conference info: %s\n", strerror(errno));
		return -1;
	}
	switch(p->conf.confmode) {
	case TOR_CONF_NORMAL:
		p->conf2.confmode = 0;
		break;
	case TOR_CONF_MONITOR:
		/* Get the other size */
		p->conf2.chan = p->conf.confno;
		res = ioctl(zap_fd(p->z), TOR_GETCONF, &p->conf2);
		if (res) {
			ast_log(LOG_WARNING, "Unable to get secondaryconference info: %s\n", strerror(errno));
			p->conf2.confmode = 0;
			return -1;
		}
		c.chan = p->conf.confno;
		c.confno = 0;
		c.confmode = TOR_CONF_NORMAL;
		res = ioctl(zap_fd(p->z), TOR_SETCONF, &c);
		if (res) {
			ast_log(LOG_WARNING, "Unable to set secondaryconference info: %s\n", strerror(errno));
			p->conf2.confmode = 0;
			return -1;
		}
		break;
	case TOR_CONF_CONF | TOR_CONF_LISTENER | TOR_CONF_TALKER:
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

static int restore_conference(struct tor_pvt *p)
{
	int res;
	if (p->conf.confmode) {
		res = ioctl(zap_fd(p->z), TOR_SETCONF, &p->conf);
		p->conf.confmode = 0;
		if (res) {
			ast_log(LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
			return -1;
		}
		if (p->conf2.confmode) {
			res = ioctl(zap_fd(p->z), TOR_SETCONF, &p->conf2);
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

static int send_callerid(struct tor_pvt *p);

int send_cwcidspill(struct tor_pvt *p)
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

static int send_callerid(struct tor_pvt *p)
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
		/* Check for a the ack on the CAS */
		res = zap_getdtmf(p->z, 1, NULL, 0, 250, 250, ZAP_HOOKEXIT | ZAP_TIMEOUTOK);
		if (res > 0) {
			char tmp[2];
			strncpy(tmp, zap_dtmfbuf(p->z), sizeof(tmp));
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

static int tor_callwait(struct ast_channel *ast)
{
	struct tor_pvt *p = ast->pvt->pvt;
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
			ast_callerid_gen_cas(p->cidspill, 2400 + 680);
			p->callwaitcas = 1;
			p->cidlen = 2400 + 680 + READ_SIZE * 4;
		} else {
			ast_callerid_gen_cas(p->cidspill, 2400);
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

static int tor_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int x, res, index;
	char *c, *n, *l;
	char callerid[256];
	if ((ast->state != AST_STATE_DOWN) && (ast->state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "tor_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
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
			x = TOR_RING;
			if (ioctl(zap_fd(p->z), TOR_HOOK, &x) && (errno != EINPROGRESS)) {
				ast_log(LOG_WARNING, "Unable to ring phone: %s\n", strerror(errno));
				return -1;
			}
			p->dialing = 1;
		} else {
			/* Call waiting call */
			p->callwaitrings = 0;
			if (ast->callerid)
				strncpy(p->callwaitcid, ast->callerid, sizeof(p->callwaitcid));
			else
				strcpy(p->callwaitcid, "");
			/* Call waiting tone instead */
			if (tor_callwait(ast))
				return -1;
				
		}
		ast->state = AST_STATE_RINGING;
		index = tor_get_index(ast, p, 0);
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
		x = TOR_START;
		/* Start the trunk */
		res = ioctl(zap_fd(p->z), TOR_HOOK, &x);
		if (res < 0) {
			if (errno != EINPROGRESS) {
				ast_log(LOG_WARNING, "Unable to start channel: %s\n", strerror(errno));
				return -1;
			}
		}
		ast_log(LOG_DEBUG, "Dialing '%s'\n", c);
		p->dop.op = TOR_DIAL_OP_REPLACE;
		if (p->sig == SIG_FEATD) {
			if (ast->callerid) {
				strncpy(callerid, ast->callerid, sizeof(callerid));
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
			if (ioctl(zap_fd(p->z), TOR_DIAL, &p->dop)) {
				x = TOR_ONHOOK;
				ioctl(zap_fd(p->z), TOR_HOOK, &x);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
				return -1;
			}
		} else
			ast_log(LOG_DEBUG, "Deferring dialing...\n");
		p->dialing = 1;
		ast->state = AST_STATE_DIALING;
		break;
#ifdef TORMENTA_PRI
	case SIG_PRI:
		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = dest;
		if (ast->callerid) {
			strncpy(callerid, ast->callerid, sizeof(callerid));
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
			((p->channel - 1) % 24) + 1, p->pri->nodetype == PRI_NETWORK ? 0 : 1, 1, l, PRI_NATIONAL_ISDN, 
			l ? PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN : PRES_NUMBER_NOT_AVAILABLE,
			c + p->stripmsd, PRI_NATIONAL_ISDN)) {
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

static int tor_hangup(struct ast_channel *ast)
{
	int res;
	int index;
	struct tor_pvt *p = ast->pvt->pvt;
	TOR_PARAMS par;
	if (option_debug)
		ast_log(LOG_DEBUG, "tor_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	index = tor_get_index(ast, p, 1);

	zap_digitmode(p->z,0);
	ast->state = AST_STATE_DOWN;
	ast_log(LOG_DEBUG, "Hangup: index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		index, p->normalindex, p->callwaitindex, p->thirdcallindex);
	
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
				p->thirdcallindex = -1;
				unalloc_pseudo(p);
			}
		} else if (index == p->callwaitindex) {
			/* If this was a call waiting call, mark the call wait
			   index as -1, so we know it's available again */
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
		/* Perform low level hangup if no owner left */
#ifdef TORMENTA_PRI
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
			res = tor_set_hook(zap_fd(p->z), TOR_ONHOOK);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
			return -1;
		}
		switch(p->sig) {
		case SIG_FXOGS:
		case SIG_FXOLS:
		case SIG_FXOKS:
			res = ioctl(zap_fd(p->z), TOR_GET_PARAMS, &par);
			if (!res) {
#if 0
				ast_log(LOG_DEBUG, "Hanging up channel %d, offhook = %d\n", p->channel, par.rxisoffhook);
#endif
				/* If they're off hook, try playing congestion */
				if (par.rxisoffhook)
					tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
				else
					tone_zone_play_tone(zap_fd(p->z), -1);
			}
			break;
		default:
		}
		if (index > -1) {
			p->needringing[index] = 0;
			p->needanswer[index] = 0;
		}
		if (p->cidspill)
			free(p->cidspill);
		tor_disable_ec(p);
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
	return 0;
}

static int tor_answer(struct ast_channel *ast)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int res=0;
	ast->state = AST_STATE_UP;
	switch(p->sig) {
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
	case SIG_EM:
	case SIG_EMWINK:
	case SIG_FEATD:
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Pick up the line */
		ast_log(LOG_DEBUG, "Took %s off hook\n", ast->name);
		res =  tor_set_hook(zap_fd(p->z), TOR_OFFHOOK);
		tone_zone_play_tone(zap_fd(p->z), -1);
		if (INTHREEWAY(p))
			tone_zone_play_tone(zap_fd(p->pseudo), -1);
		p->dialing = 0;
		break;
#ifdef TORMENTA_PRI
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

static inline int bridge_cleanup(struct tor_pvt *p0, struct tor_pvt *p1)
{
	int res;
	res = conf_clear(p0);
	res |= conf_clear(p1);
	return res;
}

static int tor_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
char	*cp;

	struct tor_pvt *p = chan->pvt->pvt;

	ast_log(LOG_DEBUG, "Set option %d, data %p, len %d\n", option, data, datalen);
	if (option != AST_OPTION_TONE_VERIFY) 
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
	zap_digitmode(p->z,((*cp) ? ZAP_MUTECONF : 0));  /* set mute mode if desired */
	errno = 0;
	return 0;
}

static int tor_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	/* Do a quickie conference between the two channels and wait for something to happen */
	struct tor_pvt *p0 = c0->pvt->pvt;
	struct tor_pvt *p1 = c1->pvt->pvt;
	struct ast_channel *who, *cs[3];
	struct ast_frame *f;
	int to = -1;
	
	int confno = -1;
	
	/* Stop any playing tones */
	tone_zone_play_tone(zap_fd(p0->z), 	-1);

	tone_zone_play_tone(zap_fd(p1->z), 	-1);

	cs[0] = c0;
	cs[1] = c1;
	for (;;) {
		pthread_mutex_lock(&c0->lock);
		pthread_mutex_lock(&c1->lock);
		p0 = c0->pvt->pvt;
		p1 = c1->pvt->pvt;

		if (!p0 || !p1) {
			pthread_mutex_unlock(&c0->lock);
			pthread_mutex_unlock(&c1->lock);
			return -1;
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
		f = ast_read(who);
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

static int tor_indicate(struct ast_channel *chan, int condition);

static int tor_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct tor_pvt *p = newchan->pvt->pvt;
	int x;
	ast_log(LOG_DEBUG, "New owner for channel %d is %s\n", p->channel, newchan->name);
	p->owner = newchan;
	for (x=0;x<3;x++)
		if (p->owners[x] == oldchan)
			p->owners[x] = newchan;
	if (newchan->state == AST_STATE_RINGING) 
		tor_indicate(newchan, AST_CONTROL_RINGING);
	return 0;
}

static int tor_ring_phone(struct tor_pvt *p)
{
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = TOR_ONHOOK;
	res = ioctl(zap_fd(p->z), TOR_HOOK, &x);
	do {
		x = TOR_RING;
		res = ioctl(zap_fd(p->z), TOR_HOOK, &x);
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

static struct ast_channel *tor_new(struct tor_pvt *, int, int, int, int);

static int attempt_transfer(struct tor_pvt *p)
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

struct ast_frame *tor_handle_event(struct ast_channel *ast)
{
	int res;
	int index;
	struct tor_pvt *p = ast->pvt->pvt;
	pthread_t threadid;
	pthread_attr_t attr;
	struct ast_channel *chan;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	index = tor_get_index(ast, p, 0);
	p->f.frametype = AST_FRAME_NULL;
	p->f.datalen = 0;
	p->f.timelen = 0;
	p->f.mallocd = 0;
	p->f.offset = 0;
	p->f.src = "tor_handle_event";
	p->f.data = NULL;
	if (index < 0)
		return &p->f;
	res = tor_get_event(zap_fd(p->z));
	ast_log(LOG_DEBUG, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, index);
	switch(res) {
		case TOR_EVENT_DIALCOMPLETE:
			tor_enable_ec(p);
			p->dialing = 0;
			if (ast->state == AST_STATE_DIALING) {
#if 0
				ast->state = AST_STATE_RINGING;
#else
				ast->state = AST_STATE_UP;
				p->f.frametype = AST_FRAME_CONTROL;
				p->f.subclass = AST_CONTROL_ANSWER;
#endif				
			}
			break;
		case TOR_EVENT_ONHOOK:
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				/* Check for some special conditions regarding call waiting */
				if (index == p->normalindex) {
					/* The normal line was hung up */
					if (p->callwaitindex > -1) {
						/* There's a call waiting call, so ring the phone */
						p->owner = p->owners[p->callwaitindex];
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has (callwait) call, ringing phone\n", p->owner);
						p->needanswer[index] = 0;
						p->needringing[index] = 0;
						p->callwaitingrepeat = 0;
						tor_ring_phone(p);
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
						/* There's a call waiting call, so ring the phone */
						p->owner = p->owners[p->normalindex];
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has (normal) call, ringing phone\n", p->owner);
						p->needanswer[index] = 0;
						p->needringing[index] = 0;
						p->callwaitingrepeat = 0;
						tor_ring_phone(p);
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
								tor_disable_ec(p);
								break;
							}
						} else 
							p->owners[p->normalindex]->softhangup = 1;
					}
				}
				/* Fall through */
			default:
				tor_disable_ec(p);
				return NULL;
			}
			break;
		case TOR_EVENT_RINGOFFHOOK:
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				switch(ast->state) {
				case AST_STATE_RINGING:
					tor_enable_ec(p);
					ast->state = AST_STATE_UP;
					p->f.frametype = AST_FRAME_CONTROL;
					p->f.subclass = AST_CONTROL_ANSWER;
					/* Make sure it stops ringing */
					tor_set_hook(zap_fd(p->z), TOR_OFFHOOK);
					ast_log(LOG_DEBUG, "channel %d answered\n", p->channel);
					if (p->cidspill) {
						/* Cancel any running CallerID spill */
						free(p->cidspill);
						p->cidspill = NULL;
					}
					p->dialing = 0;
					return &p->f;
				case AST_STATE_DOWN:
					ast->state = AST_STATE_RING;
					ast->rings = 1;
					p->f.frametype = AST_FRAME_CONTROL;
					p->f.subclass = AST_CONTROL_OFFHOOK;
					ast_log(LOG_DEBUG, "channel %d picked up\n", p->channel);
					return &p->f;
				case AST_STATE_UP:
					/* Okay -- probably call waiting*/
					break;
				default:
					ast_log(LOG_WARNING, "FXO phone off hook in weird state %d??\n", ast->state);
				}
				break;
			case SIG_EM:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_FXSLS:
			case SIG_FXSGS:
			case SIG_FXSKS:
				if (ast->state == AST_STATE_DOWN) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Ring detected\n");
					p->f.frametype = AST_FRAME_CONTROL;
					p->f.subclass = AST_CONTROL_RING;
				} else if (ast->state == AST_STATE_RINGING) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Line answered\n");
					p->f.frametype = AST_FRAME_CONTROL;
					p->f.subclass = AST_CONTROL_ANSWER;
					ast->state = AST_STATE_UP;
				} else 
					ast_log(LOG_WARNING, "Ring/Off-hook in strange state %d on channel %d\n", ast->state, p->channel);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
			}
			break;
		case TOR_EVENT_RINGEROFF:
			ast->rings++;
			if ((ast->rings > 1) && (p->cidspill)) {
				ast_log(LOG_WARNING, "Didn't finish Caller-ID spill.  Cancelling.\n");
				free(p->cidspill);
				p->cidspill = NULL;
				p->callwaitcas = 0;
			}
			p->f.frametype = AST_FRAME_CONTROL;
			p->f.subclass = AST_CONTROL_RINGING;
			break;
		case TOR_EVENT_RINGERON:
		case TOR_EVENT_NOALARM:
			break;
		case TOR_EVENT_WINKFLASH:
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
									res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_DIALRECALL);
									if (res)
										ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
									chan = tor_new(p, AST_STATE_RESERVED,0,0,1);
									p->owner = chan;
									if (pthread_create(&threadid, &attr, ss_thread, chan)) {
										ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
										res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
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
							tone_zone_play_tone(zap_fd(p->pseudo), TOR_TONE_RINGTONE);
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
		case TOR_EVENT_HOOKCOMPLETE:
			res = ioctl(zap_fd(p->z), TOR_DIAL, &p->dop);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
				p->dop.dialstr[0] = '\0';
				return NULL;
			} else 
				ast_log(LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
			p->dop.dialstr[0] = '\0';
			break;
		default:
			ast_log(LOG_DEBUG, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->f;
 }

struct ast_frame *tor_exception(struct ast_channel *ast)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int res;
	int usedindex=-1;
	p->f.frametype = AST_FRAME_NULL;
	p->f.datalen = 0;
	p->f.timelen = 0;
	p->f.mallocd = 0;
	p->f.offset = 0;
	p->f.subclass = 0;
	p->f.src = "tor_exception";
	p->f.data = NULL;
	if ((p->owner != p->owners[0]) && 
	    (p->owner != p->owners[1]) &&
		(p->owner != p->owners[2])) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		res = tor_get_event(zap_fd(p->z));
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
		case TOR_EVENT_ONHOOK:
			tor_disable_ec(p);
			if (p->owner) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Channel %s still has call, ringing phone\n", p->owner->name);
				tor_ring_phone(p);
				p->callwaitingrepeat = 0;
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			break;
		case TOR_EVENT_WINKFLASH:
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
		return &p->f;
	}
	/* If it's not us, return NULL immediately */
	if (ast != p->owner)
		return &p->f;
		
	return tor_handle_event(ast);
}

struct ast_frame  *tor_read(struct ast_channel *ast)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int res,x;
	int index;
	unsigned char ireadbuf[READ_SIZE];
	unsigned char *readbuf;
	ZAP *z = NULL;
	
	pthread_mutex_lock(&p->lock);
	
	p->f.frametype = AST_FRAME_NULL;
	p->f.datalen = 0;
	p->f.timelen = 0;
	p->f.mallocd = 0;
	p->f.offset = 0;
	p->f.subclass = 0;
	p->f.src = "tor_read";
	p->f.data = NULL;
	
	index = tor_get_index(ast, p, 0);
	
	/* Hang up if we don't really exist */
	if (index < 0)	{
		ast_log(LOG_WARNING, "We dont exist?\n");
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	
	if (p->needringing[index]) {
		/* Send ringing frame if requested */
		p->needringing[index] = 0;
		p->f.frametype = AST_FRAME_CONTROL;
		p->f.subclass = AST_CONTROL_RINGING;
		pthread_mutex_unlock(&p->lock);
		return &p->f;
	}	
	
	if (p->needanswer[index]) {
		/* Send ringing frame if requested */
		p->needanswer[index] = 0;
		p->f.frametype = AST_FRAME_CONTROL;
		p->f.subclass = AST_CONTROL_ANSWER;
		pthread_mutex_unlock(&p->lock);
		return &p->f;
	}	
	
	if (ast != p->owner) {
		/* If it's not us.  If this isn't a three way call, return immediately */
		if (!INTHREEWAY(p)) {
			pthread_mutex_unlock(&p->lock);
			return &p->f;
		}
		/* If it's not the third call, return immediately */
		if (ast != p->owners[p->thirdcallindex]) {
			pthread_mutex_unlock(&p->lock);
			return &p->f;
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
		p->f.subclass = p->dtmfq[0];
		memmove(p->dtmfq, p->dtmfq + 1, sizeof(p->dtmfq) - 1);
		p->f.frametype = AST_FRAME_DTMF;
		pthread_mutex_unlock(&p->lock);
		return &p->f;
	}
	
	if (ast->pvt->rawreadformat == AST_FORMAT_SLINEAR) {
		/* Read into temporary buffer */
		readbuf = ireadbuf;
	} else if (ast->pvt->rawreadformat == AST_FORMAT_ULAW) {
		/* Read ulaw directly into frame */
		readbuf = ((unsigned char *)p->buffer) + AST_FRIENDLY_OFFSET;
	} else {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %d\n", ast->pvt->rawreadformat);
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	CHECK_BLOCKING(ast);
	if ((z != p->z) && (z != p->pseudo)) {
		pthread_mutex_unlock(&p->lock);
		return NULL;
	}
	res = zap_recchunk(z, readbuf, READ_SIZE, ZAP_DTMFINT);
	ast->blocking = 0;
	/* Check for hangup */
	if (res < 0) {
		if (res == -1) 
			ast_log(LOG_WARNING, "tor_rec: %s\n", strerror(errno));
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
				return &p->f;
			} else {
				strncpy(p->dtmfq + strlen(p->dtmfq), zap_dtmfbuf(z), sizeof(p->dtmfq) - strlen(p->dtmfq));
				zap_clrdtmfn(z);
			}
		} else {
			pthread_mutex_unlock(&p->lock);
			return tor_handle_event(ast);
		}
		if (strlen(p->dtmfq)) {
			p->f.subclass = p->dtmfq[0];
			memmove(p->dtmfq, p->dtmfq + 1, sizeof(p->dtmfq) - 1);
			p->f.frametype = AST_FRAME_DTMF;
		}
		pthread_mutex_unlock(&p->lock);
		return &p->f;
	}
	if (p->callwaitingrepeat)
		p->callwaitingrepeat--;
	/* Repeat callwaiting */
	if (p->callwaitingrepeat == 1) {
		p->callwaitrings++;
		tor_callwait(ast);
	}
	if (ast->pvt->rawreadformat == AST_FORMAT_SLINEAR) {
		for (x=0;x<READ_SIZE;x++) {
			p->buffer[x + AST_FRIENDLY_OFFSET/2] = ast_mulaw[readbuf[x]];
		}
		p->f.datalen = READ_SIZE * 2;
	} else 
		p->f.datalen = READ_SIZE;

	/* Handle CallerID Transmission */
	if (p->cidspill &&((ast->state == AST_STATE_UP) || (ast->rings == 1)))
		send_callerid(p);

	p->f.frametype = AST_FRAME_VOICE;
	p->f.subclass = ast->pvt->rawreadformat;
	p->f.timelen = READ_SIZE/8;
	p->f.mallocd = 0;
	p->f.offset = AST_FRIENDLY_OFFSET;
	p->f.data = p->buffer + AST_FRIENDLY_OFFSET/2;
#if 0
	ast_log(LOG_DEBUG, "Read %d of voice on %s\n", p->f.datalen, ast->name);
#endif	
	if (p->dialing) {
		/* Whoops, we're still dialing, don't send anything */
		p->f.frametype = AST_FRAME_NULL;
		p->f.subclass = 0;
		p->f.timelen = 0;
		p->f.mallocd = 0;
		p->f.offset = 0;
		p->f.data = NULL;
		p->f.datalen= 0;
	}
	pthread_mutex_unlock(&p->lock);
	return &p->f;
}

static int my_tor_write(struct tor_pvt *p, unsigned char *buf, int len, int threeway)
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
			ast_log(LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			return sent;
		}
		len -= size;
		buf += size;
	}
	return sent;
}

static int tor_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int x;
	int res;
	unsigned char outbuf[4096];
	short *inbuf;
	
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
	if ((frame->subclass != AST_FORMAT_SLINEAR) && (frame->subclass != AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return -1;
	}
	if (p->dialing) {
#if 0
		if (option_debug)
#endif		
			ast_log(LOG_DEBUG, "Dropping frame since I'm still dialing...\n");
		return 0;
	}
	if (p->cidspill) {
#if 0
		if (option_debug)
#endif		
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
		inbuf = frame->data;
		for (x=0;x<frame->datalen/2;x++)
			outbuf[x] = ast_lin2mu[inbuf[x]+32768];
		res = my_tor_write(p, outbuf, frame->datalen/2, (ast != p->owner));
	} else {
		/* uLaw already */
		res = my_tor_write(p, (unsigned char *)frame->data, frame->datalen, (ast != p->owner));
	}
	if (res < 0) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	} else if (res != frame->datalen/2) {
		/* Some sort of an event */
		return 0;
	}
	return 0;
}

static int tor_indicate(struct ast_channel *chan, int condition)
{
	struct tor_pvt *p = chan->pvt->pvt;
	int res=-1;
	switch(condition) {
	case AST_CONTROL_BUSY:
		res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_BUSY);
		break;
	case AST_CONTROL_RINGING:
		res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_RINGTONE);
		if (chan->state != AST_STATE_UP)
			chan->state = AST_STATE_RINGING;
		break;
	case AST_CONTROL_CONGESTION:
		res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to set condition %d on channel %s\n", condition, chan->name);
	}
	return res;
}

static struct ast_channel *tor_new(struct tor_pvt *i, int state, int startpbx, int callwaiting, int thirdcall)
{
	struct ast_channel *tmp;
	int x;
	for (x=0;x<3;x++)
		if (!i->owners[x])
			break;
	if (x > 2) {
		ast_log(LOG_WARNING, "No available owner slots\n");
		return NULL;
	}
	tmp = ast_channel_alloc();
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "Tor/%d-%d", i->channel, x + 1);
		tmp->type = type;
		tmp->fds[0] = zap_fd(i->z);
		tmp->nativeformats = AST_FORMAT_SLINEAR | AST_FORMAT_ULAW;
		/* Start out assuming ulaw since it's smaller :) */
		tmp->pvt->rawreadformat = AST_FORMAT_ULAW;
		tmp->readformat = AST_FORMAT_ULAW;
		tmp->pvt->rawwriteformat = AST_FORMAT_ULAW;
		tmp->writeformat = AST_FORMAT_ULAW;
		
		tmp->state = state;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = tor_digit;
		tmp->pvt->call = tor_call;
		tmp->pvt->hangup = tor_hangup;
		tmp->pvt->answer = tor_answer;
		tmp->pvt->read = tor_read;
		tmp->pvt->write = tor_write;
		tmp->pvt->bridge = tor_bridge;
		tmp->pvt->exception = tor_exception;
		tmp->pvt->indicate = tor_indicate;
		tmp->pvt->fixup = tor_fixup;
		tmp->pvt->setoption = tor_setoption;
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language));
		/* Keep track of who owns it */
		i->owners[x] = tmp;
		if (!i->owner)
			i->owner = tmp;
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
		strncpy(tmp->context, i->context, sizeof(tmp->context));
		if (strlen(i->exten))
			strncpy(tmp->exten, i->exten, sizeof(tmp->exten));
		if (startpbx) {
			if (strlen(i->callerid))
				tmp->callerid = strdup(i->callerid);
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


static int ignore_pat(char *s)
{
	int x;
	for (x=0;x<dialpats;x++)
		if (ast_extension_match(keepdialpat[x], s))
			return 1;
	return 0;
}

static int bump_gains(struct tor_pvt *p)
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

static int restore_gains(struct tor_pvt *p)
{
	int res;
	/* Bump receive gain by 9.0db */
	res = set_actual_gain(zap_fd(p->z), 0, p->rxgain, p->txgain);
	if (res) {
		ast_log(LOG_WARNING, "Unable to restore gain\n");
		return -1;
	}
	return 0;
}

static void *ss_thread(void *data)
{
	struct ast_channel *chan = data;
	struct tor_pvt *p = chan->pvt->pvt;
	char exten[AST_MAX_EXTENSION];
	char exten2[AST_MAX_EXTENSION];
	unsigned char buf[256];
	char cid[256];
	struct callerid_state *cs;
	char *name, *number;
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
		strncpy(exten, zap_dtmfbuf(p->z), sizeof(exten));
		if (!strlen(exten))
			strncpy(exten, "s", sizeof(exten));
		if (p->sig == SIG_FEATD) {
			if (exten[0] == '*') {
				strncpy(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				s1 = strtok(exten2 + 1, "*");
				s2 = strtok(NULL, "*");
				if (s2) {
					if (strlen(p->callerid))
						chan->callerid = strdup(p->callerid);
					else
						chan->callerid = strdup(s1);
					strncpy(exten, s2, sizeof(exten));
				} else
					strncpy(exten, s1, sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		tor_enable_ec(p);
		if (ast_exists_extension(chan, chan->context, exten, 1)) {
			strncpy(chan->exten, exten, sizeof(chan->exten));
			zap_clrdtmf(p->z);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
			}
			return NULL;
		} else {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_2 "Unknown extension '%s' in context '%s' requested\n", exten, chan->context);
			sleep(2);
			res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_INFO);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = ast_streamfile(chan, "ss-noservice", chan->language);
			if (res >= 0)
				ast_waitstream(chan, "");
			res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
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
				res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
				tor_wait_event(zap_fd(p->z));
				ast_hangup(chan);
				return NULL;
			} else {
				exten[len++]=res;
            exten[len] = '\0';
			}
			if (!ignore_pat(exten))
				tone_zone_play_tone(zap_fd(p->z), -1);
			if (ast_exists_extension(chan, chan->context, exten, 1)) {
				res = tone_zone_play_tone(zap_fd(p->z), -1);
				strncpy(chan->exten, exten, sizeof(chan->exten));
				if (strlen(p->callerid) && !p->hidecallerid)
					chan->callerid = strdup(p->callerid);
				chan->state = AST_STATE_RING;
				tor_enable_ec(p);
				res = ast_pbx_run(chan);
				if (res) {
					ast_log(LOG_WARNING, "PBX exited non-zero\n");
					res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
				}						
				return NULL;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Disabling call waiting on %s\n", chan->name);
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
					
			} else if (!p->hidecallerid && !strcmp(exten, "*67")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Disabling Caller*ID on %s\n", chan->name);
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				if (chan->callerid)
					free(chan->callerid);
				chan->callerid = NULL;
				res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
					
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Enabling Caller*ID on %s\n", chan->name);
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				if (chan->callerid)
					free(chan->callerid);
				chan->callerid = NULL;
				res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
					
			} else if (!ast_canmatch_extension(chan, chan->context, exten, 1) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Can't match %s from '%s' in context %s\n", exten, chan->callerid ? chan->callerid : "<Unknown Caller>", chan->context);
				break;
			}
			timeout = gendigittimeout;
			if (len && !ignore_pat(exten))
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
					i = TOR_IOMUX_READ | TOR_IOMUX_SIGEVENT;
					if ((res = ioctl(zap_fd(p->z), TOR_IOMUX, &i)))	{
						ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
						callerid_free(cs);
						ast_hangup(chan);
						return NULL;
					}
					if (i & TOR_IOMUX_SIGEVENT) {
						res = tor_get_event(zap_fd(p->z));
						ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
						res = 0;
						break;
					} else if (i & TOR_IOMUX_READ) {
						res = read(zap_fd(p->z), buf + len, sizeof(buf) - len);
						if (res < 0) {
							if (errno != ELAST) {
								ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
								callerid_free(cs);
								ast_hangup(chan);
								return NULL;
							}
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
					callerid_get(cs, &number, &name, &flags);
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
		if (strlen(cid))
			chan->callerid = strdup(cid);
		chan->state = AST_STATE_RING;
		chan->rings = 1;
		tor_enable_ec(p);
		res = ast_pbx_run(chan);
		if (res) {
			ast_hangup(chan);
			ast_log(LOG_WARNING, "PBX exited non-zero\n");
		}
		return NULL;
	default:
		ast_log(LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", sig2str(p->sig), p->channel);
		res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
		if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
	if (res < 0)
			ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
	return NULL;
}

static int handle_init_event(struct tor_pvt *i, int event)
{
	int res;
	pthread_t threadid;
	pthread_attr_t attr;
	struct ast_channel *chan;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	/* Handle an event on a given channel for the monitor thread. */
	switch(event) {
	case TOR_EVENT_RINGOFFHOOK:
		/* Got a ring/answer.  What kind of channel are we? */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			if (i->immediate) {
				tor_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_RINGTONE);
				chan = tor_new(i, AST_STATE_RING, 1, 0, 0);
				if (!chan)  {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_DIALTONE);
				if (res < 0) 
					ast_log(LOG_WARNING, "Unable to play dialtone on channel %d\n", i->channel);
				/* Check for callerid, digits, etc */
				chan = tor_new(i, AST_STATE_DOWN, 0, 0, 0);
				if (pthread_create(&threadid, &attr, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					ast_hangup(chan);
				}
#if 0
				printf("Created thread %ld detached in switch\n", threadid);
#endif
			}
			break;
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_EM:
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
				/* Check for callerid, digits, etc */
				chan = tor_new(i, AST_STATE_RING, 0, 0, 0);
				if (pthread_create(&threadid, &attr, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_CONGESTION);
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
			res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_CONGESTION);
			if (res < 0)
					ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			return -1;
		}
		break;
	case TOR_EVENT_WINKFLASH:
	case TOR_EVENT_ONHOOK:
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
			tor_disable_ec(i);
			res = tone_zone_play_tone(zap_fd(i->z), -1);
			tor_set_hook(zap_fd(i->z), TOR_ONHOOK);
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
	struct tor_pvt *i;
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
		   tor_pvt that does not have an associated owner channel */
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
				res = tor_get_event(zap_fd(i->z));
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

static struct tor_pvt *mkif(int channel, int signalling)
{
	/* Make a tor_pvt structure for this interface */
	struct tor_pvt *tmp;
	char fn[80];
#if 1
	struct tor_bufferinfo bi;
#endif	
	int res;
	int span;
	TOR_PARAMS p;

	tmp = malloc(sizeof(struct tor_pvt));
	if (tmp) {
		memset(tmp, 0, sizeof(struct tor_pvt));
		snprintf(fn, sizeof(fn), "/dev/tor/%d", channel);
		/* Open non-blocking */
		tmp->z = zap_open(fn, 1);
		/* Allocate a zapata structure */
		if (!tmp->z) {
			ast_log(LOG_ERROR, "Unable to open channel %d: %s\n", channel, strerror(errno));
			free(tmp);
			return NULL;
		}
		res = ioctl(zap_fd(tmp->z), TOR_GET_PARAMS, &p);
		if (res < 0) {
			ast_log(LOG_ERROR, "Unable to get parameters\n");
			free(tmp);
			return NULL;
		}
		if (p.sigtype != (signalling & 0xf)) {
			ast_log(LOG_ERROR, "Signalling requested is %s but line is in %s signalling\n", sig2str(signalling), sig2str(p.sigtype));
			free(tmp);
			return NULL;
		}
		span = (channel - 1)/24;
		tmp->span = span + 1;
#ifdef TORMENTA_PRI
		if (signalling == SIG_PRI) {
			int offset;
			offset = 1;
			if (ioctl(zap_fd(tmp->z), TOR_AUDIOMODE, &offset)) {
				ast_log(LOG_ERROR, "Unable to set audio mode on clear channel %d of span %d: %s\n", channel, span, strerror(errno));
				return NULL;
			}
			if (span >= NUM_SPANS) {
				ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
				free(tmp);
				return NULL;
			} else {
				offset = (channel -1) % 24 + 1;
				if (offset < 24) {
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
					tmp->pri = &pris[span];
					tmp->call = NULL;
				} else {
					ast_log(LOG_ERROR, "Channel 24 is reserved for D-channel.\n");
					free(tmp);
					return NULL;
				}
			}
		}
#endif		
		/* Adjust starttime on loopstart and kewlstart trunks to reasonable values */
		if ((signalling == SIG_FXSKS) || (signalling == SIG_FXSLS)) {
			p.starttime = 250;
			res = ioctl(zap_fd(tmp->z), TOR_SET_PARAMS, &p);
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
		res = ioctl(zap_fd(tmp->z), TOR_GET_BUFINFO, &bi);
		if (!res) {
			bi.txbufpolicy = POLICY_IMMEDIATE;
			bi.rxbufpolicy = POLICY_IMMEDIATE;
			bi.numbufs = 4;
			res = ioctl(zap_fd(tmp->z), TOR_SET_BUFINFO, &bi);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d\n", channel);
			}
		} else
			ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d\n", channel);
#endif
		tmp->immediate = immediate;
		tmp->sig = signalling;
		if ((signalling == SIG_FXOKS) || (signalling == SIG_FXOLS) || (signalling == SIG_FXOGS))
			tmp->permcallwaiting = callwaiting;
		else
			tmp->permcallwaiting = 0;
		tmp->callwaitingcallerid = callwaitingcallerid;
		tmp->threewaycalling = threewaycalling;
		tmp->permhidecallerid = hidecallerid;
		tmp->echocancel = echocancel;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->hidecallerid = tmp->permhidecallerid;
		tmp->channel = channel;
		tmp->stripmsd = stripmsd;
		tmp->use_callerid = use_callerid;
		tmp->callwaitindex = -1;
		tmp->normalindex = -1;
		tmp->thirdcallindex = -1;
		tmp->normalindex = -1;
		tmp->confno = -1;
		tmp->pseudo = NULL;
		tmp->pseudochan = 0;
		tmp->transfer = transfer;
		pthread_mutex_init(&tmp->lock, NULL);
		strncpy(tmp->language, language, sizeof(tmp->language));
		strncpy(tmp->context, context, sizeof(tmp->context));
		strncpy(tmp->callerid, callerid, sizeof(tmp->callerid));
		tmp->group = cur_group;
		tmp->next = NULL;
		tmp->rxgain = rxgain;
		tmp->txgain = txgain;
		set_actual_gain(zap_fd(tmp->z), 0, tmp->rxgain, tmp->txgain);
		zap_digitmode(tmp->z, ZAP_DTMF /* | ZAP_MUTECONF */);
		conf_clear(tmp);
		if (signalling != SIG_PRI) 
			/* Hang it up to be sure it's good */
			tor_set_hook(zap_fd(tmp->z), TOR_ONHOOK);
	}
	return tmp;
}

static inline int available(struct tor_pvt *p, int channelmatch, int groupmatch)
{
	/* First, check group matching */
	if ((p->group & groupmatch) != groupmatch)
		return 0;
	/* Check to see if we have a channel match */
	if ((channelmatch > 0) && (p->channel != channelmatch))
		return 0;
		
	/* If no owner definitely available */
	if (!p->owner)
		return 1;

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

static struct ast_channel *tor_request(char *type, int format, void *data)
{
	int oldformat;
	int groupmatch = 0;
	int channelmatch = -1;
	struct tor_pvt *p;
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
#ifdef TORMENTA_PRI
       	if (p->pri) 
				if (!(p->call = pri_new_call(p->pri->pri))) {
					ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
					break;
				}
#endif
			callwait = (p->owner != NULL);
			tmp = tor_new(p, AST_STATE_RESERVED, 0, p->owner ? 1 : 0, 0);
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

#ifdef TORMENTA_PRI

static int pri_find_empty_chan(struct tor_pri *pri)
{
	int x;
	for (x=23;x>0;x--) {
		if (pri->pvt[x] && !pri->pvt[x]->owner)
			return x;
	}
	return 0;
}

static int pri_fixup(struct tor_pri *pri, int channel, q931_call *c)
{
	int x;
	for (x=1;x<24;x++) {
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
	struct tor_pri *pri = vpri;
	pri_event *e;
	fd_set efds;
	fd_set rfds;
	int res;
	int chan;
	int x;
	struct ast_channel *c;
	for(;;) {
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		FD_SET(pri->fd, &rfds);
		FD_SET(pri->fd, &efds);
		res = select(pri->fd + 1, &rfds, NULL, &efds, pri_schedule_next(pri->pri));
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
						if ((chan < 1) || (chan > 23) )
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
						for (x=1;x<24;x++)
							if (pri->pvt[chan]->owner)
								pri->pvt[chan]->owner->softhangup = 1;
					}
					break;
				case PRI_EVENT_RING:
					chan = e->ring.channel;
					if ((chan < 1) || (chan > 23)) {
						ast_log(LOG_WARNING, "Ring requested on odd channel number %d span %d\n", chan, pri->span);
						chan = 0;
					} else if (!pri->pvt[chan]) {
						ast_log(LOG_WARNING, "Ring requested on unconfigured channel %d span %d\n", chan, pri->span);
						chan = 0;
					} else if (pri->pvt[chan]->owner) {
						ast_log(LOG_WARNING, "Ring requested on channel %d already in use on span %d\n", chan, pri->span);
						chan = 0;
					}
					if (!chan && (e->ring.flexible))
						chan = pri_find_empty_chan(pri);
					if (chan) {
						/* Get caller ID */
						if (pri->pvt[chan]->use_callerid) 
							strncpy(pri->pvt[chan]->callerid, e->ring.callingnum, sizeof(pri->pvt[chan]->callerid));
						else
							strcpy(pri->pvt[chan]->callerid, "");
						/* Get called number */
						if (strlen(e->ring.callednum)) {
							strncpy(pri->pvt[chan]->exten, e->ring.callednum, sizeof(pri->pvt[chan]->exten));
						} else
							strcpy(pri->pvt[chan]->exten, "s");
						/* Make sure extension exists */
						if (ast_exists_extension(NULL, pri->pvt[chan]->context, pri->pvt[chan]->exten, 1)) {
							/* Start PBX */
							pri->pvt[chan]->call = e->ring.call;
							c = tor_new(pri->pvt[chan], AST_STATE_RING, 1, 0, 0);
							if (c) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Accepting call from '%s' to '%s' on channel %d, span %d\n",
										e->ring.callingnum, pri->pvt[chan]->exten, chan, pri->span);
								pri_acknowledge(pri->pri, e->ring.call, chan, 0);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d, span %d\n", chan, pri->span);
								pri_release(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
								pri->pvt[chan]->call = NULL;
							}
						} else {
							if (option_verbose > 2) 
								ast_verbose(VERBOSE_PREFIX_3 "Extension '%s' in context '%s' does not exist.  Rejecting call on channel %d, span %d\n", 
									pri->pvt[chan]->exten, pri->pvt[chan]->context, chan, pri->span);
							pri_release(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
						}
					} else 
						pri_release(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
					break;
				case PRI_EVENT_RINGING:
					chan = e->ringing.channel;
					if ((chan < 1) || (chan > 23)) {
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
					break;				
				case PRI_EVENT_ANSWER:
					chan = e->answer.channel;
					if ((chan < 1) || (chan > 23)) {
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
					if ((chan < 1) || (chan > 23)) {
						ast_log(LOG_WARNING, "Hangup requested on odd channel number %d span %d\n", chan, pri->span);
						chan = 0;
					} else if (!pri->pvt[chan]) {
						ast_log(LOG_WARNING, "Hanngup requested on unconfigured channel %d span %d\n", chan, pri->span);
						chan = 0;
					}
					if (chan) {
						chan = pri_fixup(pri, chan, e->hangup.call);
						if (chan) {
							if (pri->pvt[chan]->owner) {
								if (option_verbose > 3) 
									ast_verbose(VERBOSE_PREFIX_3, "Channel %d, span %d got hangup\n", chan, pri->span);
								pri->pvt[chan]->owner->softhangup = 1;
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
				res = ioctl(pri->fd, TOR_GETEVENT, &x);
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

static int start_pri(struct tor_pri *pri)
{
	char filename[80];
	int res;
	TOR_PARAMS p;
	BUFFER_INFO bi;
	snprintf(filename, sizeof(filename), "/dev/tor/%d", pri->offset + 24);
	pri->fd = open(filename, O_RDWR, 0600);
	if (pri->fd < 0) {
		ast_log(LOG_ERROR, "Unable to open D-channel %s (%s)\n", filename, strerror(errno));
		return -1;
	}
	res = ioctl(pri->fd, TOR_GET_PARAMS, &p);
	if (res) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "Unable to get parameters for D-channel %s (%s)\n", filename, strerror(errno));
		return -1;
	}
	if (p.sigtype != TOR_HDLCFCS) {
		close(pri->fd);
		pri->fd = -1;
		ast_log(LOG_ERROR, "D-channel %s is not in HDLC/FCS mode.  See /etc/tormenta.conf\n", filename);
		return -1;
	}
	bi.txbufpolicy = POLICY_IMMEDIATE;
	bi.rxbufpolicy = POLICY_IMMEDIATE;
	bi.numbufs = 4;
	bi.bufsize = 1024;
	if (ioctl(pri->fd, TOR_SET_BUFINFO, &bi)) {
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


static char pri_debug_help[] = 
	"Usage: pri debug span <span>\n"
	"       Enables debugging on a given PRI span\n";
	
static char pri_no_debug_help[] = 
	"Usage: pri no debug span <span>\n"
	"       Disables debugging on a given PRI span\n";

static struct ast_cli_entry pri_debug = {
	{ "pri", "debug", "span", NULL }, handle_pri_debug, "Enables PRI debugging on a span", pri_debug_help, complete_span 
};

static struct ast_cli_entry pri_no_debug = {
	{ "pri", "no", "debug", "span", NULL }, handle_pri_no_debug, "Enables PRI debugging on a span", pri_no_debug_help, complete_span };

#endif

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct tor_pvt *tmp;
	char *chan;
	int start, finish,x;
#ifdef TORMENTA_PRI
	int y;
#endif

	
#ifdef TORMENTA_PRI
	memset(pris, 0, sizeof(pris));
	for (y=0;y<NUM_SPANS;y++)
		pris[y].fd = -1;
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
					tmp = mkif(x, cur_signalling);
					if (tmp) {
						tmp->next = iflist;
						iflist = tmp;
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
			strncpy(context, v->value, sizeof(context));
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language));
		} else if (!strcasecmp(v->name, "stripmsd")) {
			stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			cur_group = get_group(v->value);
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
				strncpy(callerid, v->value, sizeof(callerid));
		} else if (!strcasecmp(v->name, "ignorepat")) {
			if (dialpats < AST_MAX_DIAL_PAT - 1) {
				strncpy(keepdialpat[dialpats], v->value, sizeof(keepdialpat[dialpats]));
				dialpats++;
			} else
				ast_log(LOG_WARNING, "Too many dial patterns, ignoring '%s'\n", v->value);
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
#ifdef TORMENTA_PRI
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
#ifdef TORMENTA_PRI
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
				unload_module();
				return -1;
			}
#endif		
		} else
			ast_log(LOG_DEBUG, "Ignoring %s\n", v->name);
		v = v->next;
	}
	ast_pthread_mutex_unlock(&iflock);
	/* Make sure we can register our Tor channel type */
	if (ast_channel_register(type, tdesc, AST_FORMAT_SLINEAR |  AST_FORMAT_ULAW, tor_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		unload_module();
		return -1;
	}
	ast_destroy(cfg);
#ifdef TORMENTA_PRI
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
	ast_cli_register(&pri_debug);
	ast_cli_register(&pri_no_debug);
#endif	
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int unload_module()
{
	struct tor_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
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
