/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Voice over Frame Relay, Adtran Style
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
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#ifndef OLD_SANGOMA_API
#include <linux/if_wanpipe.h>
#include <linux/wanpipe.h>
#endif
#include <sys/signal.h>
#include "adtranvofr.h"

/* #define VOFRDUMPER */

#define G723_MAX_BUF 2048

#define FR_API_MESS 16

static char *desc = "Adtran Voice over Frame Relay";
static char *type = "AdtranVoFR";
static char *tdesc = "Voice over Frame Relay/Adtran style";
static char *config = "adtranvofr.conf";

static char context[AST_MAX_EXTENSION] = "default";

static char language[MAX_LANGUAGE] = "";

static int usecnt =0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of vofr_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

/* The private structures of the Adtran VoFR channels are linked for
   selecting outgoing channels */
   
static struct vofr_pvt {
	int s;							/* Raw socket for this DLCI */
#ifdef OLD_SANGOMA_API
	struct sockaddr_pkt sa;			/* Sockaddr needed for sending, also has iface name */
#else
	struct wan_sockaddr_ll sa;		/* Wanpipe sockaddr */
#endif
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	int outgoing;					/* Does this channel support outgoing calls? */
	struct vofr_pvt *next;			/* Next channel in list */
	struct vofr_hdr *hdr;				/* VOFR header version of buf */
	struct vofr_hdr *ohdr;
	u_int8_t dlcih;					/* High two bits of DLCI */
	u_int8_t dlcil;					/* Bottom two bits of DLCI */
	u_int8_t cid;					/* Call ID */
	char buf[G723_MAX_BUF];					/* Static buffer for reading frames */
	char obuf[G723_MAX_BUF];				/* Output buffer */
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	int ringgothangup;				/* Have we received exactly one hangup after a ring */
} *iflist = NULL;

#ifdef VOFRDUMPER

/* Some useful debugging routines */

static char *set(int val)
{
	return (val ? "Set  " : "Unset");
}

static char *controlstr(int control)
{
	switch(control) {
	case VOFR_CONTROL_ADTRAN:
		return "Adtran Proprietary";
	case VOFR_CONTROL_VOICE:
		return "Voice";
	case VOFR_CONTROL_RFC1490:
		return "RFC 1490";
	}
	return "Unknown";
}

static char *dtypestr(int control)
{
	switch(control) {
	case VOFR_TYPE_SIGNAL:
		return "Signal Frame";
	case VOFR_TYPE_VOICE:
		return "Voice Frame";
	case VOFR_TYPE_ANSWER:
		return "Answer Tone";
	case VOFR_TYPE_FAX:	
		return "FAX";
	case VOFR_TYPE_DTMF:
		return "DTMF Digit";
	}
	return "Unknown";
}

static char *vflagsstr(int flags)
{
	static char buf[80] = "";
	buf[0] = '\0';
	if (!flags)
		return "(None)";
	if (flags & VOFR_ROUTE_LOCAL)
		strncat(buf, "Local ", sizeof(buf) - strlen(buf) - 1);
	if (flags & VOFR_ROUTE_VOICE)
		strncat(buf, "Voice ", sizeof(buf) - strlen(buf) - 1);
	if (flags & VOFR_ROUTE_DTE)
		strncat(buf, "DTE ", sizeof(buf) - strlen(buf) - 1);
	else if (flags & VOFR_ROUTE_DTE1)
		strncat(buf, "DTE1 ", sizeof(buf) - strlen(buf) - 1);
	else if (flags & VOFR_ROUTE_DTE2)	
		strncat(buf, "DTE2 ", sizeof(buf) - strlen(buf) - 1);
	return buf;
}

static char *remidstr(int remid)
{
	switch(remid) {
	case VOFR_CARD_TYPE_UNSPEC:
		return "Unspecified";
	case VOFR_CARD_TYPE_FXS:
		return "FXS";
	case VOFR_CARD_TYPE_FXO:
		return "FXO";
	case VOFR_CARD_TYPE_ENM:	
		return "E&M";
	case VOFR_CARD_TYPE_VCOM:	
		return "Atlas/VCOM";
	}
	return "Unknown";
}

static char *modulationstr(int modulation)
{
	switch(modulation) {
	case VOFR_MODULATION_SINGLE:
		return "Single Frequency";
	case VOFR_MODULATION_V21:
		return "V.21";
	case VOFR_MODULATION_V27ter_2:
		return "V.27 (2400bps)";
	case VOFR_MODULATION_V27ter_4:
		return "V.27 (4800bps)";
	case VOFR_MODULATION_V29_7:
		return "V.29 (7200bps)";
	case VOFR_MODULATION_V29_9:
		return "V.29 (9600bps)";
	case VOFR_MODULATION_V33_12:
		return "V.33 (12000bps)";
	case VOFR_MODULATION_V33_14:
		return "V.33 (14400BPS)";
	}
	return "Unknown";
}

static char *signalstr(int signal)
{
	switch(signal) {
	case VOFR_SIGNAL_ON_HOOK:
		return "On Hook";
	case VOFR_SIGNAL_OFF_HOOK:
		return "Off Hook";
	case VOFR_SIGNAL_RING:
		return "Ring";
	case VOFR_SIGNAL_SWITCHED_DIAL:
		return "Switched Dial";
	case VOFR_SIGNAL_BUSY:
		return "Busy";
	case VOFR_SIGNAL_TRUNK_BUSY:
		return "Trunk Busy";
	}
	return "Unknown";
}

static char *vofr_digitstr(int val)
{
	static char num[5];
	if (val < 10) {
		snprintf(num, sizeof(num), "%d", val);
		return num;
	}
	switch(val) {
	case 10:
		return "*";
	case 11:
		return "#";
	}
	return "Unknown";
}


static void vofr_dump_packet(struct vofr_hdr *vh, int len)
{
	printf("VoFR Packet Dump\n");
	printf("================\n");
	printf("EI: %s ", set(vh->control & VOFR_MASK_EI));
	printf("LI: %s\n", set(vh->control & VOFR_MASK_LI));
	printf("Control: %s (0x%02x)\n", 
		controlstr(vh->control & VOFR_MASK_CONTROL), vh->control & VOFR_MASK_CONTROL);
	printf("Data Type: %s (0x%02x)\n", dtypestr(vh->dtype), vh->dtype);
	if (vh->dtype == VOFR_TYPE_SIGNAL) {
		printf(" \\--Signal: %s (0x%02x)\n", signalstr(vh->data[0]), vh->data[0]);
	}
	if (vh->dtype == VOFR_TYPE_DTMF) {
		printf(" \\--Digit: %s (0x%02x)\n", vofr_digitstr(vh->data[0]), vh->data[0]);
	}
	printf("Connect Tag: 0x%02x\n", vh->ctag);
	printf("Voice Rt Flags: %s\n", vflagsstr(vh->vflags));
	printf("DLCI X-Ref: %d\n", (vh->dlcih << 8) | (vh->dlcil));
	printf("Channel ID: %d\n", vh->cid);
	printf("Remote ID: %s (0x%02x)\n", remidstr(vh->remid), vh->remid);
	printf("Modulation: %s (0x%02x)\n", modulationstr(vh->mod), vh->mod);
	printf("\n");
	fflush(stdout);
}

#endif

static struct ast_frame  *vofr_read(struct ast_channel *ast);

static int vofr_xmit(struct vofr_pvt *p, char *data, int len)
{
	int res;
#ifdef OLD_SANGOMA_API
    res=sendto(p->s, data, len, 0, (struct sockaddr *)&p->sa, sizeof(struct sockaddr_pkt));
#else
    res=sendto(p->s, data, len, 0, (struct sockaddr *)&p->sa, sizeof(struct wan_sockaddr_ll));
#endif
	if (res != len) {
		ast_log(LOG_WARNING, "vofr_xmit returned %d\n", res);
	}
	return res;
}

static int vofr_digit(struct ast_channel *ast, char digit)
{
	/*
	 * T H I S   I S   T O T A L L Y   U N D O C U M E N T E D
	 *     A N D   D O E S   N O T   S O U N D   R I G H T
	 *   XXX Figure out how to really send a decent digit XXX
	 */
	struct vofr_pvt *p;
	struct vofr_hdr *vh;
	p = ast->pvt->pvt;
	vh = p->ohdr;
	vh->control = VOFR_CONTROL_VOICE;
	vh->dtype = VOFR_TYPE_DTMF;
	vh->vflags = VOFR_ROUTE_NONE;
	vh->dlcih = p->dlcih;
	vh->dlcil = p->dlcil;
	vh->cid = p->cid;
	vh->remid = VOFR_CARD_TYPE_ASTERISK;
	vh->mod = VOFR_MODULATION_SINGLE;
	if ((digit >= '0') && (digit <= '9'))
                vh->data[0] = digit - '0';
        else if (digit == '*')
                vh->data[0] = 10;
        else if (digit == '#')
                vh->data[0] = 11;
        else {
                ast_log(LOG_WARNING, "%s: tried to dial a non digit '%c'\n", ast->name, digit);
                return -1;
        }
        vh->data[1] = 0x14;
        vh->data[2] = 0x1f;
        vh->data[3] = 0x70;
		/* We sorta start the digit */
        vofr_xmit(p, p->obuf, VOFR_HDR_SIZE + 4 + FR_API_MESS);
        usleep(30000);
		/* And terminate with an empty voice frame */
        vh->control = VOFR_CONTROL_VOICE;
        vh->dtype = VOFR_TYPE_VOICE;
        vh->vflags = VOFR_ROUTE_NONE;
        vh->dlcih = p->dlcih;
        vh->dlcil = p->dlcil;
        vh->cid = p->cid;
        vh->remid = VOFR_CARD_TYPE_ASTERISK;
        vh->mod = VOFR_MODULATION_SINGLE;
        vofr_xmit(p, p->obuf, VOFR_HDR_SIZE + FR_API_MESS);
	return 0;
}

static int vofr_xmit_signal(struct vofr_pvt *p, int signal, int pad)
{
        /* Prepare and transmit outgoing buffer with given signal and
           pad the end with *pad* bytes of data presumed to already
           be in the buffer (like DTMF tones, etc) */
        struct vofr_hdr *vh = p->ohdr;
	int res;
    vh->control = VOFR_CONTROL_VOICE;
    vh->dtype = VOFR_TYPE_SIGNAL;
    vh->vflags = VOFR_ROUTE_NONE;
    vh->dlcih = p->dlcih;
    vh->dlcil = p->dlcil;
    vh->cid = p->cid;
    vh->remid = VOFR_CARD_TYPE_ASTERISK;
    vh->mod = VOFR_MODULATION_SINGLE;
    vh->data[0] = signal;
	if (FR_API_MESS)
		memset(p->obuf, 0, FR_API_MESS);
	res = vofr_xmit(p, p->obuf,  VOFR_HDR_SIZE + pad + 1 + FR_API_MESS);
	return res;

}

static int vofr_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res;
	int otimeout;
	struct ast_frame *f;
	struct vofr_pvt *p;
	p = ast->pvt->pvt;
	if ((ast->state != AST_STATE_DOWN) && (ast->state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "vofr_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* Take the system off hook */
	vofr_xmit_signal(p, VOFR_SIGNAL_OFFHOOK, 0);
	/* Wait for an acknowledgement */
	otimeout = 1000;
	while(otimeout) {
		otimeout = ast_waitfor(ast, 1000);
		if (otimeout < 1) {
			ast_log(LOG_WARNING, "Unable to take line '%s' off hook\n", ast->name);
			/* Musta gotten hung up, or no ack on off hook */
			return -1;	
		}
		f = vofr_read(ast);
		if (!f)
			return -1;
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_OFFHOOK)) 
			/* Off hook */
				break;
	}
	if (!otimeout) {
		ast_log(LOG_WARNING, "Unable to take line off hook\n");
		return -1;
	}
	/* Send the digits */
	while(*dest) {
		ast->state = AST_STATE_DIALING;
		vofr_digit(ast, *dest);
		/* Wait .1 seconds before dialing next digit */
		usleep(100000);
		dest++;
	}
	if (timeout) {
		/* Wait for the ack that it's ringing */
		otimeout = 1000;
		while(otimeout) {
			otimeout = ast_waitfor(ast, 1000);
			if (otimeout < 1) {
				ast_log(LOG_WARNING, "No acknowledgement for ringing\n");
				/* Musta gotten hung up, or no ack on off hook */
				return -1;	
			}
			f = vofr_read(ast);
			if (!f) 
				return -1;
			
			if (f->frametype == AST_FRAME_CONTROL) {
			    if (f->subclass == AST_CONTROL_RINGING) {
					ast->state = AST_STATE_RINGING;
					/* We're ringing -- good enough */
					break;
				}
				if (f->subclass == AST_CONTROL_BUSY)
				/* It's busy */
					return -1;
			}
			ast_frfree(f);
		}		
	}
	otimeout = timeout;
	while(timeout) {
		/* Wait for an answer, up to timeout... */
		res = ast_waitfor(ast, timeout);
		if (res < 0)
			/* Musta gotten hung up */
			return -1;
		else
			timeout = res;
		if (res) {
			/* Ooh, read what's there. */
			f = vofr_read(ast);
			if (!f)
				return -1;
			if ((f->frametype == AST_FRAME_CONTROL) && 
			    (f->subclass == AST_CONTROL_ANSWER)) 
				/* Got an answer -- return the # of ms it took */
				return otimeout - res;
				
		}
	}
	return 0;
}

static int send_hangup(struct vofr_pvt *p)
{
	/* Just send the hangup sequence */
	return vofr_xmit_signal(p, 0x80, 0);
}

static int vofr_hangup(struct ast_channel *ast)
{
	int res;
	if (option_debug)
		ast_log(LOG_DEBUG, "vofr_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	res = send_hangup(ast->pvt->pvt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
		return -1;
	}
	ast->state = AST_STATE_DOWN;
	((struct vofr_pvt *)(ast->pvt->pvt))->owner = NULL;
	((struct vofr_pvt *)(ast->pvt->pvt))->ringgothangup = 0;
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);
	ast->pvt->pvt = NULL;
	ast->state = AST_STATE_DOWN;
	restart_monitor();
	return 0;
}

static int vofr_answer(struct ast_channel *ast)
{
	int res;
	int cnt = 1000;
	char buf[2048];
	struct vofr_hdr *vh;
	ast->rings = 0;
	if (option_debug)
		ast_log(LOG_DEBUG, "vofr_answer(%s)\n", ast->name);
	res = vofr_xmit_signal(ast->pvt->pvt, VOFR_SIGNAL_OFFHOOK, 0);
	if (res < 0)
		ast_log(LOG_WARNING, "Unable to anaswer line %s\n", ast->name);
	ast->state = AST_STATE_UP;
	while(cnt > 0) {
		cnt = ast_waitfor(ast, cnt);
		if (cnt > 0) {
			res = read(ast->fds[0], buf, sizeof(buf));
#ifdef VOFRDUMPER
				vofr_dump_packet((void *)(buf +FR_API_MESS), res - FR_API_MESS);
#endif
			res -= FR_API_MESS;
			if (res < 0)
				ast_log(LOG_WARNING, "Warning:  read failed (%s) on %s\n", strerror(errno), ast->name);
			else {
				/* We're looking for an answer */
				vh = (struct vofr_hdr *)(buf + FR_API_MESS);
				switch(vh->dtype) {
				case VOFR_TYPE_SIGNAL:
					switch(vh->data[0]) {
					case VOFR_SIGNAL_UNKNOWN:
						switch(vh->data[1]) {
						case 0x1:
							if (option_debug) 
								ast_log(LOG_DEBUG, "Answered '%s'\n", ast->name);
							else if (option_verbose > 2) 
								ast_verbose( VERBOSE_PREFIX_3 "Answered '%s'\n", ast->name);
							ast->state = AST_STATE_UP;
							return 0;
							break;
						default:
							ast_log(LOG_WARNING, "Unexpected 'unknown' frame type %d\n", vh->data[1]);
						}
						break;
					case VOFR_SIGNAL_ON_HOOK:
						/* Ignore onhooks.  */
						break;
					default:
						ast_log(LOG_WARNING, "Unexpected signal type %d\n", vh->data[0]);
					}
					break;
				default:
					ast_log(LOG_WARNING, "Unexpected data type %d\n", vh->dtype);
				}
			}
		}
	}
	ast_log(LOG_WARNING, "Did not get acknowledged answer\n");
	return -1;
}

static char vofr_2digit(char c)
{
	if (c == 11)
		return '#';
	else if (c == 10)
		return '*';
	else if ((c < 10) && (c >= 0))
		return '0' + c;
	else
		return '?';
}

static struct ast_frame  *vofr_read(struct ast_channel *ast)
{
	int res;
	char tone;
	int timeout,x;
	struct vofr_pvt *p = ast->pvt->pvt;
	short *swapping;
	struct ast_frame *fr = (struct ast_frame *)(p->buf);
	struct vofr_hdr *vh = (struct vofr_hdr *)(p->buf + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET - sizeof(struct vofr_hdr));
	/* Read into the right place in the buffer, in case we send this
	   as a voice frame. */
	CHECK_BLOCKING(ast);
retry:
	res = read(p->s, ((char *)vh)  - FR_API_MESS, 
				G723_MAX_BUF - AST_FRIENDLY_OFFSET - sizeof(struct ast_frame) + sizeof(struct vofr_hdr) + FR_API_MESS);
	if (res < 0) {
		/*  XXX HUGE BUG IN SANGOMA'S STACK: IT IGNORES O_NONBLOCK XXX */
		if (errno == EAGAIN) {
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(p->s, &fds);
			ast_select(p->s + 1, &fds, NULL, NULL, NULL);
			goto retry;
		}
		ast->blocking = 0;
		ast_log(LOG_WARNING, "Read error on %s: %s (%d)\n", ast->name, strerror(errno));
		return NULL;
	}
	ast->blocking = 0;
		
#ifdef VOFRDUMPER
	vofr_dump_packet((void *)(vh), res);
#endif
	res -= FR_API_MESS;		
	if (res < sizeof(struct vofr_hdr)) {
		ast_log(LOG_WARNING, "Nonsense frame on %s\n", ast->name);
		return NULL;
	}
	/* Some nice norms */
	fr->datalen = 0;
	fr->timelen = 0;
	fr->data =  NULL;
	fr->src = type;
	fr->offset = 0;
	fr->mallocd=0;
	fr->delivery.tv_sec = 0;
	fr->delivery.tv_usec = 0;
	
	/* Now, what we do depends on what we read */
	switch(vh->dtype) {
	case VOFR_TYPE_SIGNAL:
		switch(vh->data[0]) {
		case VOFR_SIGNAL_ON_HOOK:
			/* Hang up this line */
			if ((ast->state == AST_STATE_UP) || (p->ringgothangup)) {
				return NULL;
			} else {
				fr->frametype = AST_FRAME_NULL;
				fr->subclass = 0;
				p->ringgothangup=1;
			}
			break;
		case VOFR_SIGNAL_RING:
			ast->rings++;
			p->ringgothangup = 0;
			break;
		case VOFR_SIGNAL_UNKNOWN:
			switch(vh->data[1]) {
			case 0x1:
				/* This is a little tricky, because it depends
				   on the context of what state we're in */
				switch(ast->state) {
				case AST_STATE_RINGING:
					fr->frametype = AST_FRAME_CONTROL;
					fr->subclass = AST_CONTROL_ANSWER;
					ast->state = AST_STATE_UP;
					break;
				case AST_STATE_DOWN:
				case AST_STATE_UP:
					fr->frametype = AST_FRAME_NULL;
					fr->subclass = 0;
					break;
				}
				break;
			case 0x2:
				/* Remote acknowledged off hook */
				fr->frametype = AST_FRAME_CONTROL;
				fr->subclass = AST_CONTROL_OFFHOOK;
				ast->state = AST_STATE_OFFHOOK;
				break;
			case 0x3:
				/* Busy signal */
				fr->frametype = AST_FRAME_CONTROL;
				fr->subclass = AST_CONTROL_BUSY;
				ast->state = AST_STATE_BUSY;
				break;
			case 0x5:
				/* Ringing -- acknowledged */
				fr->frametype = AST_FRAME_CONTROL;
				fr->subclass = AST_CONTROL_RINGING;
				ast->state = AST_STATE_RINGING;
				break;
			case 0x6:
				/* Hang up detected.  Return NULL */
				return NULL;
			default:
				ast_log(LOG_WARNING, "Don't know what to do with 'unknown' signal '%d'\n", vh->data[1]);
				fr->frametype = AST_FRAME_NULL;
				fr->subclass = 0;
			}
			return fr;
			break;
		default:
			ast_log(LOG_WARNING, "Don't know what to do with signal '%d'\n", vh->data[0]);
		}
		break;
	case VOFR_TYPE_DTMF:
		/* If it's a DTMF tone, then we want to wait until we don't get any more dtmf tones or
		   the DTMF tone changes.  
		       XXX Note: We will drop at least one frame here though XXX */
		
		tone = vofr_2digit(vh->data[0]);
		timeout = 50;
		do {
			if ((timeout = ast_waitfor(ast, timeout)) < 1)
				break;
			CHECK_BLOCKING(ast);
			res = read(p->s, ((char *)vh)  - FR_API_MESS, 
					G723_MAX_BUF - AST_FRIENDLY_OFFSET - sizeof(struct ast_frame) + sizeof(struct vofr_hdr) + FR_API_MESS);
			ast->blocking = 0;
			res -= FR_API_MESS;		
			if (res < sizeof(struct vofr_hdr *)) {
				ast_log(LOG_WARNING, "Nonsense frame on %s\n", ast->name);
				return NULL;
			}
			if (vh->dtype == VOFR_TYPE_DTMF) {
				/* Reset the timeout */
				timeout = 50;
				if ((tone != vofr_2digit(vh->data[0])) )
					/* Or not...  Something else now.. Just send our first frame */
					break;
			}
			
		} while (timeout);
		fr->frametype = AST_FRAME_DTMF;
		fr->subclass = tone;
		fr->datalen = 0;
		fr->data = NULL;
		fr->offset = 0;
		return fr;
	case VOFR_TYPE_VOICE:
		/* XXX Bug in the Adtran: Sometimes we don't know when calls are picked up, so if we
		       get voice frames, go ahead and consider it answered even though it probably has
			   not been answered XXX */
		if ((ast->state == AST_STATE_RINGING) || (ast->state == AST_STATE_DIALING))  {
			ast_log(LOG_DEBUG, "Adtran bug! (state = %d)\n", ast->state);
			fr->frametype = AST_FRAME_CONTROL;
			fr->subclass = AST_CONTROL_ANSWER;
			ast->state = AST_STATE_UP;
			return fr;
		} else if (ast->state !=  AST_STATE_UP) {
			ast_log(LOG_WARNING, "%s: Voice in weird state %d\n", ast->name, ast->state);
		}
		fr->frametype = AST_FRAME_VOICE;
		fr->subclass = AST_FORMAT_G723_1;
		fr->datalen = res - sizeof(struct vofr_hdr);
		fr->data = ((char *)vh) + sizeof(struct vofr_hdr);
		fr->src = type;
		/* XXX Byte swapping is a bug XXX */
		swapping = fr->data;
		for (x=0;x<fr->datalen/2;x++)
			swapping[x] = ntohs(swapping[x]);
		fr->offset = AST_FRIENDLY_OFFSET;
		/* Thirty ms of sound per frame */
		fr->timelen = 30;
		return fr;
	default:
		ast_log(LOG_WARNING, "Don't know what to do with data type %d frames\n", vh->dtype);
	}
	/* If we don't know what it is, send a NULL frame */
	fr->frametype = AST_FRAME_NULL;
	fr->subclass = 0;
	return fr;
}

static int vofr_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct vofr_hdr *vh;
	struct vofr_pvt *p = ast->pvt->pvt;
	short *swapping;
    int x;
	char *start;
	int res;
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Don't know what to do with  frame type '%d'\n", frame->frametype);
		return -1;
	}
	if (frame->subclass != AST_FORMAT_G723_1) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return -1;
	}
	/* If we get here, we have a voice frame of G.723.1 data.  First check to be
	   sure we have enough headroom for the vofr header.  If there isn't enough
	   headroom, we're lazy and just error out rather than copying it into the
	   output buffer, because applications should always leave AST_FRIENDLY_OFFSET
	   bytes just for this reason. */
	if (frame->offset < sizeof(struct vofr_hdr) + FR_API_MESS) {
		ast_log(LOG_WARNING, "Frame source '%s' didn't provide a friendly enough offset\n", (frame->src ? frame->src : "**Unknown**"));
		return -1;
	}
	/* XXX Byte swapping is a bug XXX */
	swapping = frame->data;
	for (x=0;x<frame->datalen/2;x++)
		swapping[x] = ntohs(swapping[x]);
	vh = (struct vofr_hdr *)(frame->data - sizeof(struct vofr_hdr));
	/* Some versions of the API have some header mess that needs to be
	   zero'd out and acounted for..  */
	start = ((void *)vh) - FR_API_MESS;
	if (start)
		memset(start, 0, FR_API_MESS);
	/* Now we fill in the vofr header */
	vh->control = VOFR_CONTROL_VOICE;
	vh->dtype = VOFR_TYPE_VOICE;
	vh->vflags = VOFR_ROUTE_NONE;
	vh->dlcih = p->dlcih;
	vh->dlcil = p->dlcil;
	vh->cid = p->cid;
	vh->remid = VOFR_CARD_TYPE_ASTERISK;
	vh->mod = VOFR_MODULATION_SINGLE;
	res = vofr_xmit(p, start, 
				VOFR_HDR_SIZE + frame->datalen + FR_API_MESS);
	res -= FR_API_MESS;
	/* XXX Byte swapping is a bug, but get it back to the right format XXX */
	swapping = frame->data;
	for (x=0;x<frame->datalen/2;x++)
		swapping[x] = htons(swapping[x]);
	if (res != VOFR_HDR_SIZE + frame->datalen) {
		ast_log(LOG_WARNING, "Unable to write frame correctly\n");
		return -1;
	}
	return 0;
}

static int vofr_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct vofr_pvt *p = newchan->pvt->pvt;
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		return -1;
	}
	p->owner = newchan;
	return 0;
}

static struct ast_channel *vofr_new(struct vofr_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(0);
	if (tmp) {
#ifdef OLD_SANGOMA_API
		snprintf(tmp->name, sizeof(tmp->name), "AdtranVoFR/%s", i->sa.spkt_device);
#else
		snprintf(tmp->name, sizeof(tmp->name), "AdtranVoFR/%s", i->sa.sll_device);
#endif
		tmp->type = type;
		tmp->fds[0] = i->s;
		/* Adtran VoFR supports only G723.1 format data.  G711 (ulaw) would be nice too */
		tmp->nativeformats = AST_FORMAT_G723_1;
		tmp->state = state;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->writeformat = AST_FORMAT_G723_1;
		tmp->readformat = AST_FORMAT_G723_1;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = vofr_digit;
		tmp->pvt->call = vofr_call;
		tmp->pvt->hangup = vofr_hangup;
		tmp->pvt->answer = vofr_answer;
		tmp->pvt->read = vofr_read;
		tmp->pvt->write = vofr_write;
		tmp->pvt->fixup = vofr_fixup;
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		i->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);
		if (state != AST_STATE_DOWN) {
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

static int vofr_mini_packet(struct vofr_pvt *i, struct vofr_hdr *pkt, int len)
{
	/* Here, we're looking for rings or off hooks -- signals that
	   something is about to happen and we need to start the 
	   PBX thread */
	switch(pkt->dtype) {
	case VOFR_TYPE_SIGNAL:
		switch(pkt->data[0]) {
		case VOFR_SIGNAL_RING:
			/* If we get a RING, we definitely want to start a new thread */
			if (!i->owner) {
				i->ringgothangup = 0;
				vofr_new(i, AST_STATE_RING);
			} else
				ast_log(LOG_WARNING, "Got a ring, but there's an owner?\n");
			break;
		case VOFR_SIGNAL_OFF_HOOK:
			/* Network termination, go off hook */
#if 0
			ast_log(LOG_DEBUG, "Off hook\n");
#endif
			vofr_xmit_signal(i, 0x10, 2);
			if (!i->owner)
				vofr_new(i, AST_STATE_UP);
			else
				ast_log(LOG_WARNING, "Got an offhook, but there's an owner?\n");
			break;
		case VOFR_SIGNAL_ON_HOOK:
			break;
		case VOFR_SIGNAL_UNKNOWN:
			switch(pkt->data[1]) {
			case 0x1:
				/* ignore */
				break;
			case 0x6:
				/* A remote hangup request */
				if (option_debug)
					ast_log(LOG_DEBUG, "Sending hangup reply\n");
				send_hangup(i);
				break;
			default:
				ast_log(LOG_WARNING, "Unexected 'unknown' signal '%d'\n", pkt->data[1]);
			}
			break;
		default:
			ast_log(LOG_DEBUG, "Unknown signal type '%d'\n", pkt->data[0]);
		}			
		break;
	case VOFR_TYPE_VOICE:
		break;
	default:
		ast_log(LOG_DEBUG, "Unknown packet type '%d'\n", pkt->dtype);
	}
	return 0;
}

static void *do_monitor(void *data)
{
	fd_set rfds;
	int n, res;
	struct vofr_pvt *i;
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
#if 0
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL)) {
		ast_log(LOG_WARNING, "Unable to set cancel type to asynchronous\n");
		return NULL;
	}
#endif
	for(;;) {
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		if (ast_mutex_lock(&monlock)) {
			ast_log(LOG_ERROR, "Unable to grab monitor lock\n");
			return NULL;
		}
		/* Lock the interface list */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_ERROR, "Unable to grab interface lock\n");
			ast_mutex_unlock(&monlock);
			return NULL;
		}
		/* Build the stuff we're going to select on, that is the socket of every
		   vofr_pvt that does not have an associated owner channel */
		n = -1;
		FD_ZERO(&rfds);
		i = iflist;
		while(i) {
			if (FD_ISSET(i->s, &rfds)) 
#ifdef OLD_SANGOMA_API
				ast_log(LOG_WARNING, "Descriptor %d appears twice (%s)?\n", i->s, i->sa.spkt_device);
#else
				ast_log(LOG_WARNING, "Descriptor %d appears twice (%s)?\n", i->s, i->sa.sll_device);
#endif
			if (!i->owner) {
				/* This needs to be watched, as it lacks an owner */
				FD_SET(i->s, &rfds);
				if (i->s > n)
					n = i->s;
			}
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_mutex_unlock(&iflock);
		
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_mutex_unlock(&monlock);
		pthread_testcancel();
		/* Wait indefinitely for something to happen */
		res = ast_select(n + 1, &rfds, NULL, NULL, NULL);
		pthread_testcancel();
		/* Okay, select has finished.  Let's see what happened.  */
		if (res < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "select return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}
		i = iflist;
		while(i) {
			if (FD_ISSET(i->s, &rfds)) {
				if (i->owner) {
#ifdef OLD_SANGOMA_API
					ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d, %s)...\n", i->s, i->sa.spkt_device);
#else
					ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d, %s)...\n", i->s, i->sa.sll_device);
#endif
					continue;
				}
				res = read(i->s, i->buf, sizeof(i->buf));
				res -= FR_API_MESS;
#ifdef VOFRDUMPER
				vofr_dump_packet(i->hdr, res);
#endif
				vofr_mini_packet(i, i->hdr, res);
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
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static struct vofr_pvt *mkif(char *type, char *iface)
{
	/* Make a vofr_pvt structure for this interface */
	struct vofr_pvt *tmp;
	int sndbuf = 4096;

	tmp = malloc(sizeof(struct vofr_pvt));
	if (tmp) {

		/* Allocate a packet socket */
#ifdef OLD_SANGOMA_API
		tmp->s = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ALL));
#else
		/* Why the HELL does Sangoma change their API every damn time
		   they make a new driver release?!?!?!  Leave it the hell
		   alone this time.  */
		tmp->s = socket(AF_WANPIPE, SOCK_RAW, 0);
#endif		

		if (tmp->s < 0) {
			ast_log(LOG_ERROR, "Unable to create socket: %s\n", strerror(errno));
			free(tmp);
			return NULL;
		}

#ifdef OLD_SANGOMA_API
		/* Prepare sockaddr for binding */
		memset(&tmp->sa, 0, sizeof(tmp->sa));
		strncpy(tmp->sa.spkt_device, iface, sizeof(tmp->sa.spkt_device)-1);
		tmp->sa.spkt_protocol = htons(0x16);
		tmp->sa.spkt_family = AF_PACKET;
		if (bind(tmp->s, (struct sockaddr *)&tmp->sa, sizeof(struct sockaddr))) {
#else
		/* Prepare sockaddr for binding */
		memset(&tmp->sa, 0, sizeof(tmp->sa));
		tmp->sa.sll_family = AF_WANPIPE;
		tmp->sa.sll_protocol = htons(ETH_P_IP);
		strncpy(tmp->sa.sll_device, iface, sizeof(tmp->sa.sll_device)-1);
		strncpy(tmp->sa.sll_card, "wanpipe1", sizeof(tmp->sa.sll_card)-1);
		tmp->sa.sll_ifindex = 0;
		if (bind(tmp->s, (struct sockaddr *)&tmp->sa, sizeof(struct wan_sockaddr_ll))) {
#endif		
		/* Bind socket to specific interface */
#ifdef OLD_SANGOMA_API
			ast_log(LOG_ERROR, "Unable to bind to '%s': %s\n", tmp->sa.spkt_device, 
#else
			ast_log(LOG_ERROR, "Unable to bind to '%s': %s\n", tmp->sa.sll_device, 
#endif
										strerror(errno));
			free(tmp);
			return NULL;
		}
		
		/* Set magic send buffer size */
		if (setsockopt(tmp->s, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf))) {
			ast_log(LOG_ERROR, "Unable to set send buffer size to %d: %s\n", sndbuf, strerror(errno));
			free(tmp);
			return NULL;
		}
		tmp->owner =  NULL;
		tmp->hdr = (struct vofr_hdr *)(tmp->buf + FR_API_MESS);
		tmp->ohdr = (struct vofr_hdr *)(tmp->obuf + FR_API_MESS);
		tmp->dlcil = 0;
		tmp->dlcih = 0;
		tmp->cid = 1;
		tmp->ringgothangup = 0;
		strncpy(tmp->language, language, sizeof(tmp->language)-1);
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		/* User terminations are game for outgoing connections */
		if (!strcasecmp(type, "user")) 
			tmp->outgoing = 1;
		else
			tmp->outgoing = 0;
		tmp->next = NULL;
		/* Hang it up to be sure it's good */
		send_hangup(tmp);
		
	}
	return tmp;
}

static struct ast_channel *vofr_request(char *type, int format, void *data)
{
	int oldformat;
	struct vofr_pvt *p;
	struct ast_channel *tmp = NULL;
	/* We can only support G.723.1 formatted frames, but we should never
	   be asked to support anything else anyway, since we've published
	   our capabilities when we registered. */
	oldformat = format;
	format &= AST_FORMAT_G723_1;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		return NULL;
	}
	/* Search for an unowned channel */
	if (ast_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p) {
		if (!p->owner && p->outgoing) {
			tmp = vofr_new(p, AST_STATE_DOWN);
			break;
		}
		p = p->next;
	}
	ast_mutex_unlock(&iflock);
	restart_monitor();
	return tmp;
}

static int __unload_module(void)
{
	struct vofr_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	if (!ast_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner);
			p = p->next;
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if (monitor_thread) {
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
			/* Close the socket, assuming it's real */
			if (p->s > -1)
				close(p->s);
			pl = p;
			p = p->next;
			/* Free associated memory */
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
		
	return 0;
}

int unload_module()
{
	return __unload_module();
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct vofr_pvt *tmp;
	cfg = ast_load(config);

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
	v = ast_variable_browse(cfg, "interfaces");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "user") ||
			!strcasecmp(v->name, "network")) {
				tmp = mkif(v->name, v->value);
				if (tmp) {
					tmp->next = iflist;
					iflist = tmp;
				} else {
					ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
					ast_destroy(cfg);
					ast_mutex_unlock(&iflock);
					__unload_module();
					return -1;
				}
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		}
		v = v->next;
	}
	ast_mutex_unlock(&iflock);
	/* Make sure we can register our AdtranVoFR channel type */
	if (ast_channel_register(type, tdesc, AST_FORMAT_G723_1, vofr_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		__unload_module();
		return -1;
	}
	ast_destroy(cfg);
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

char *description()
{
	return desc;
}

