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

static char *desc = "Tormenta (Zapata) Channelized T1 Driver";
static char *type = "Tor";
static char *tdesc = "Tormenta T1 Driver";
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

#define tor_STATE_DOWN 0

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

static struct tor_pvt {
	ZAP *z;
	struct ast_channel *owner;	/* Our owner (if applicable) */
	int sig;					/* Signalling style */
	struct tor_pvt *next;			/* Next channel in list */
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char callerid[AST_MAX_EXTENSION];
	char dtmfq[AST_MAX_EXTENSION];
	struct ast_frame f;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	int group;
	int state;						/* Perhaps useful state info */
	int immediate;				/* Answer before getting digits? */
	int channel;				/* Channel Number */
	int ringgothangup;				/* Have we received exactly one hangup after a ring */
	int dialing;
	int use_callerid;			/* Whether or not to use caller id on this channel */
	unsigned char *cidspill;
	int cidpos;
	int cidlen;
	int stripmsd;
	DIAL_OPERATION dop;
} *iflist = NULL;

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
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d\n", sig);
		return buf;
	}
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
	return 0;
}
                                                                                
static int tor_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int x, res;
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
		if (p->use_callerid) {
			/* Generate the Caller-ID spill if desired */
			if (p->cidspill) {
				ast_log(LOG_WARNING, "cidspill already exists??\n");
				free(p->cidspill);
			}
			p->cidspill = malloc(MAX_CALLERID_SIZE);
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
		ast->state = AST_STATE_RINGING;
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
				printf("Name: %s, number: %s\n", n, l);
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
	default:
		ast_log(LOG_DEBUG, "not yet implemented\n");
		return -1;
	}
	return 0;
}

static int tor_hangup(struct ast_channel *ast)
{
	int res;
	struct tor_pvt *p = ast->pvt->pvt;
	TOR_PARAMS par;
	if (option_debug)
		ast_log(LOG_DEBUG, "tor_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
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
			/* If they're off hook, try playing congestion */
			if (par.rxisoffhook)
				tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
		}
		break;
	default:
	}
	ast->state = AST_STATE_DOWN;
	p->owner = NULL;
	p->ringgothangup = 0;
	if (p->cidspill)
		free(p->cidspill);
	p->cidspill = NULL;
	pthread_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	pthread_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);
	ast->pvt->pvt = NULL;
	ast->state = AST_STATE_DOWN;
	restart_monitor();
	return 0;
}

static int tor_answer(struct ast_channel *ast)
{
	struct tor_pvt *p = ast->pvt->pvt;
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
		return tor_set_hook(zap_fd(p->z), TOR_OFFHOOK);
		break;
		/* Nothing */
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		return -1;
	}
	return 0;
}

static int bridge_cleanup(struct tor_pvt *p0, struct tor_pvt *p1)
{
	struct tor_confinfo c;
	int res;
	c.chan = 0;
	c.confno = 0;
	c.confmode = TOR_CONF_NORMAL;
	res = ioctl(zap_fd(p0->z), TOR_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "ioctl(TOR_SETCONF) failed on channel %d: %s\n", p0->channel, strerror(errno));
		return -1;
	}
	c.chan = 0;
	c.confno = 0;
	c.confmode = TOR_CONF_NORMAL;
	res = ioctl(zap_fd(p1->z), TOR_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "ioctl(TOR_SETCONF) failed on channel %d: %s\n", p1->channel, strerror(errno));
		return -1;
	}
	return 0;
}


static int tor_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	/* Do a quickie conference between the two channels and wait for something to happen */
	struct tor_pvt *p0 = c0->pvt->pvt;
	struct tor_pvt *p1 = c1->pvt->pvt;
	struct ast_channel *who, *cs[3];
	struct ast_frame *f;
	struct tor_confinfo c;
	int res;
	int to = -1;
	/* Put the first channel in a unique conference */
	c.chan = 0;
	c.confno = p1->channel;
	c.confmode = TOR_CONF_MONITOR;
	
	/* Stop any playing */
	tone_zone_play_tone(zap_fd(p0->z), 	-1);
	res = ioctl(zap_fd(p0->z), TOR_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "ioctl(TOR_SETCONF) failed on channel %s: %s\n", c0->name, strerror(errno));
		bridge_cleanup(p0, p1);
		return -1;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Channel %d got put on conference %d\n", c.chan, c.confno);

	/* Put the other channel on the same conference */
	c.chan = 0;
	c.confno = p0->channel;
	res = ioctl(zap_fd(p1->z), TOR_SETCONF, &c);
	if (res) {
		ast_log(LOG_WARNING, "ioctl(TOR_SETCONF) failed on channel %s: %s\n", c0->name, strerror(errno));
		bridge_cleanup(p0, p1);
		return -1;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Channel %d got put on conference %d\n", c.chan, c.confno);

	for (;;) {
		cs[0] = c0;
		cs[1] = c1;
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

struct ast_frame *tor_handle_event(struct ast_channel *ast)
{
	int res;
	struct tor_pvt *p = ast->pvt->pvt;
	p->f.frametype = AST_FRAME_NULL;
	p->f.datalen = 0;
	p->f.timelen = 0;
	p->f.mallocd = 0;
	p->f.offset = 0;
	p->f.src = "tor_handle_event";
	p->f.data = NULL;
	res = tor_get_event(zap_fd(p->z));
	ast_log(LOG_DEBUG, "Got event %s(%d) on channel %d\n", event2str(res), res, p->channel);
	switch(res) {
		case TOR_EVENT_DIALCOMPLETE:
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
			return NULL;
		case TOR_EVENT_RINGOFFHOOK:
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				switch(ast->state) {
				case AST_STATE_RINGING:
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
					return &p->f;
				case AST_STATE_DOWN:
					ast->state = AST_STATE_RING;
					ast->rings = 1;
					p->f.frametype = AST_FRAME_CONTROL;
					p->f.subclass = AST_CONTROL_OFFHOOK;
					ast_log(LOG_DEBUG, "channel %d picked up\n", p->channel);
					return &p->f;
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
				/* XXX For now, treat as a hang up */
				return NULL;
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
	return tor_handle_event(ast);
}

struct ast_frame  *tor_read(struct ast_channel *ast)
{
	struct tor_pvt *p = ast->pvt->pvt;
	int res,x;
	unsigned char ireadbuf[READ_SIZE];
	unsigned char *readbuf;
	
	p->f.frametype = AST_FRAME_DTMF;
	p->f.datalen = 0;
	p->f.timelen = 0;
	p->f.mallocd = 0;
	p->f.offset = 0;
	p->f.src = "tor_read";
	p->f.data = NULL;
	
	/* Check first for any outstanding DTMF characters */
	if (strlen(p->dtmfq)) {
		p->f.subclass = p->dtmfq[0];
		memmove(p->dtmfq, p->dtmfq + 1, sizeof(p->dtmfq) - 1);
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
		return NULL;
	}
	CHECK_BLOCKING(ast);
	res = zap_recchunk(p->z, readbuf, READ_SIZE, ZAP_DTMFINT);
	ast->blocking = 0;
	/* Check for hangup */
	if (res < 0) {
		if (res == -1) 
			ast_log(LOG_WARNING, "tor_rec: %s\n", strerror(errno));
		return NULL;
	}
	if (res != READ_SIZE) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Short read, must be DTMF or something...\n");
		/* XXX UGLY!!  Zapata's DTMF handling is a bit ugly XXX */
		if (zap_dtmfwaiting(p->z) && !strlen(zap_dtmfbuf(p->z))) {
			zap_getdtmf(p->z, 1, NULL, 0, 1, 1, 0);
		}
		if (strlen(zap_dtmfbuf(p->z))) {
			ast_log(LOG_DEBUG, "Got some dtmf ('%s')... on channel %s\n", zap_dtmfbuf(p->z), ast->name);
			/* DTMF tone detected.  Queue and erturn */
			strncpy(p->dtmfq + strlen(p->dtmfq), zap_dtmfbuf(p->z), sizeof(p->dtmfq) - strlen(p->dtmfq));
			zap_clrdtmfn(p->z);
		} else {
			return tor_handle_event(ast);
		}
		return &p->f;
	}
	if (ast->pvt->rawreadformat == AST_FORMAT_SLINEAR) {
		for (x=0;x<READ_SIZE;x++) {
			p->buffer[x + AST_FRIENDLY_OFFSET/2] = ast_mulaw[readbuf[x]];
		}
		p->f.datalen = READ_SIZE * 2;
	} else 
		p->f.datalen = READ_SIZE;

	/* Handle CallerID Transmission */
	if ((ast->rings == 1) && (p->cidspill))
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
	return &p->f;
}

static int my_tor_write(struct tor_pvt *p, unsigned char *buf, int len)
{
	int sent=0;
	int size;
	int res;
	while(len) {
		size = len;
		if (size > READ_SIZE)
			size = READ_SIZE;
		res = write(zap_fd(p->z), buf, size);
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
		ast_log(LOG_DEBUG, "Dropping frame since I'm still dialing...\n");
		return 0;
	}
	if (p->cidspill) {
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
		res = my_tor_write(p, outbuf, frame->datalen/2);
	} else {
		/* uLaw already */
		res = my_tor_write(p, (unsigned char *)frame->data, frame->datalen);
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

static struct ast_channel *tor_new(struct tor_pvt *i, int state, int startpbx)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc();
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "Tor/%d", i->channel);
		tmp->type = type;
		tmp->fd = zap_fd(i->z);
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
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language));
		i->owner = tmp;
		pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, i->context, sizeof(tmp->context));
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


static int ignore_pat(char *s)
{
	int x;
	for (x=0;x<dialpats;x++)
		if (!strcmp(s, keepdialpat[x]))
			return 1;
	return 0;
}

static int bump_gains(struct tor_pvt *p)
{
	int res;
	/* Bump receive gain by 9.0db */
	res = set_actual_gain(zap_fd(p->z), 0, 9, 0);
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
	res = set_actual_gain(zap_fd(p->z), 0, 0, 0);
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
		res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_RINGTONE);
		if (res < 0)
			ast_log(LOG_WARNING, "Unable to start ringback tone on channel %d\n", p->channel);
		if (ast_exists_extension(chan, chan->context, exten, 1)) {
			strncpy(chan->exten, exten, sizeof(chan->exten));
			zap_clrdtmf(p->z);
			res = ast_pbx_run(chan);
			if (res) 
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
			res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
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
		res = zap_getdtmf(p->z, 1, NULL, 0, firstdigittimeout, firstdigittimeout, ZAP_HOOKEXIT | ZAP_TIMEOUTOK);
		if (res < 0) {
			if (option_debug)
				ast_log(LOG_DEBUG, "getdtmf returned %d (%s)...\n", res, strerror(errno));
			/* Got hung up on apparently.  Stop any playing tones.  We're done */
			res = tone_zone_play_tone(zap_fd(p->z), -1);
			ast_hangup(chan);
			return NULL;
		}
		if (res) {
			strncpy(exten + len, zap_dtmfbuf(p->z), sizeof(exten) - len);
			len++;
			if (ast_exists_extension(chan, chan->context, exten, 1)) {
				if (!ignore_pat(exten)) {
					res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_RINGTONE);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to start ringback tone on channel %d\n", p->channel);
				}
				/* Check for a single digit extension */
				strncpy(chan->exten, exten, sizeof(chan->exten));
				zap_clrdtmf(p->z);
				res = ast_pbx_run(chan);
				if (res) 
					ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
				return NULL;
			}
			
			if (!ignore_pat(exten))
				tone_zone_play_tone(zap_fd(p->z), -1);
			while(len < AST_MAX_EXTENSION-1) {
				zap_clrdtmf(p->z);
				res = zap_getdtmf(p->z, 1, NULL, 0, gendigittimeout, gendigittimeout, ZAP_HOOKEXIT | ZAP_TIMEOUTOK);
				if (res < 0) {
					ast_log(LOG_DEBUG, "getdtmf returned < 0...\n");
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
					strncpy(exten + len, zap_dtmfbuf(p->z), sizeof(exten) - len);
					len++;
					if (!ignore_pat(exten))
						tone_zone_play_tone(zap_fd(p->z), -1);
					if (ast_exists_extension(chan, chan->context, exten, 1)) {
						res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_RINGTONE);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to start ringback tone on channel %d\n", p->channel);
						strncpy(chan->exten, exten, sizeof(chan->exten));
						if (strlen(p->callerid))
							chan->callerid = strdup(p->callerid);
						zap_clrdtmf(p->z);
						res = ast_pbx_run(chan);
						if (res) 
							ast_log(LOG_WARNING, "PBX exited non-zero\n");
						res = tone_zone_play_tone(zap_fd(p->z), TOR_TONE_CONGESTION);
						return NULL;
					} else if (!ast_canmatch_extension(chan, chan->context, exten, 1)) {
						printf("Can't match %s is context %s\n", exten, chan->context);
						break;
					}
				}
			}
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
	struct ast_channel *chan;
	/* Handle an event on a given channel for the monitor thread. */
	switch(event) {
	case TOR_EVENT_RINGOFFHOOK:
		/* Got a ring/answer.  What kind of channel are we? */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
			if (i->immediate) {
				/* The channel is immediately up.  Start right away */
				chan = tor_new(i, AST_STATE_UP, 1);
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
				chan = tor_new(i, AST_STATE_DOWN, 0);
				if (pthread_create(&threadid, NULL, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					ast_hangup(chan);
				}
			}
			break;
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_EM:
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
				/* Check for callerid, digits, etc */
				chan = tor_new(i, AST_STATE_RING, 0);
				if (pthread_create(&threadid, NULL, ss_thread, chan)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(zap_fd(i->z), TOR_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					ast_hangup(chan);
				}
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
	fd_set rfds;
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
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		if (pthread_mutex_lock(&monlock)) {
			ast_log(LOG_ERROR, "Unable to grab monitor lock\n");
			return NULL;
		}
		/* Lock the interface list */
		if (pthread_mutex_lock(&iflock)) {
			ast_log(LOG_ERROR, "Unable to grab interface lock\n");
			pthread_mutex_unlock(&monlock);
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
		pthread_mutex_unlock(&iflock);
		
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		pthread_mutex_unlock(&monlock);
		pthread_testcancel();
		/* Wait indefinitely for something to happen */
		res = select(n + 1, &rfds, NULL, &efds, NULL);
		pthread_testcancel();
		/* Okay, select has finished.  Let's see what happened.  */
		if (res < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "select return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (pthread_mutex_lock(&iflock)) {
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
				ast_log(LOG_DEBUG, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
				handle_init_event(i, res);
			}
			i=i->next;
		}
		pthread_mutex_unlock(&iflock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == -2)
		return 0;
	if (pthread_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		pthread_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread) {
#if 1
		pthread_cancel(monitor_thread);
#endif
		pthread_kill(monitor_thread, SIGURG);
#if 0
		pthread_join(monitor_thread, NULL);
#endif
	}
	/* Start a new monitor */
	if (pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
		pthread_mutex_unlock(&monlock);
		ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
		return -1;
	}
	pthread_mutex_unlock(&monlock);
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
			return NULL;
		}
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
		tmp->state = tor_STATE_DOWN;
		tmp->sig = signalling;
		tmp->use_callerid = use_callerid;
		tmp->channel = channel;
		tmp->stripmsd = stripmsd;
		strncpy(tmp->language, language, sizeof(tmp->language));
		strncpy(tmp->context, context, sizeof(tmp->context));
		strncpy(tmp->callerid, callerid, sizeof(tmp->callerid));
		tmp->group = cur_group;
		tmp->next = NULL;
		tmp->ringgothangup = 0;
		/* Hang it up to be sure it's good */
		tor_set_hook(zap_fd(tmp->z), TOR_ONHOOK);
		
	}
	return tmp;
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
	if (pthread_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p && !tmp) {
		if (!p->owner &&	/* No current owner */
		    ((channelmatch < 0) || (p->channel == channelmatch)) && /* Right channel */
			(((p->group & groupmatch) == groupmatch))				/* Right group */
		) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Using channel %d\n", p->channel);
			tmp = tor_new(p, AST_STATE_RESERVED, 0);
			break;
		}
		p = p->next;
	}
	pthread_mutex_unlock(&iflock);
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

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct tor_pvt *tmp;
	char *chan;
	int start, finish,x;
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}
	

	if (pthread_mutex_lock(&iflock)) {
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
				pthread_mutex_unlock(&iflock);
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
					pthread_mutex_unlock(&iflock);
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
						pthread_mutex_unlock(&iflock);
						unload_module();
						return -1;
					}
				}
				chan = strtok(NULL, ",");
			}
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
			} else {
				ast_log(LOG_ERROR, "Unknown signalling method '%s'\n", v->value);
				ast_destroy(cfg);
				pthread_mutex_unlock(&iflock);
				unload_module();
				return -1;
			}
		} else
			ast_log(LOG_DEBUG, "Ignoring %s\n", v->name);
		v = v->next;
	}
	pthread_mutex_unlock(&iflock);
	/* Make sure we can register our Tor channel type */
	if (ast_channel_register(type, tdesc, AST_FORMAT_SLINEAR |  AST_FORMAT_ULAW, tor_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		unload_module();
		return -1;
	}
	ast_destroy(cfg);
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int unload_module()
{
	struct tor_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	if (!pthread_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner);
			p = p->next;
		}
		iflist = NULL;
		pthread_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!pthread_mutex_lock(&monlock)) {
		if (monitor_thread) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = -2;
		pthread_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!pthread_mutex_lock(&iflock)) {
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
		pthread_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
		
	return 0;
}
int usecount()
{
	int res;
	pthread_mutex_lock(&usecnt_lock);
	res = usecnt;
	pthread_mutex_unlock(&usecnt_lock);
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
