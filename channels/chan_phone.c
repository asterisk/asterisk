/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Generic Linux Telephony Interface driver
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/causes.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/telephony.h>
/* Still use some IXJ specific stuff */
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
# include <linux/compiler.h>
#endif
#include <linux/ixjuser.h>
#include "DialTone.h"

#ifdef QTI_PHONEJACK_TJ_PCI	/* check for the newer quicknet driver v.3.1.0 which has this symbol */
#define QNDRV_VER 310
#else
#define QNDRV_VER 100
#endif

#if QNDRV_VER > 100
#ifdef __linux__
#define IXJ_PHONE_RING_START(x)	ioctl(p->fd, PHONE_RING_START, &x);
#else /* FreeBSD and others */
#define IXJ_PHONE_RING_START(x)	ioctl(p->fd, PHONE_RING_START, x);
#endif /* __linux__ */
#else	/* older driver */
#define IXJ_PHONE_RING_START(x)	ioctl(p->fd, PHONE_RING_START, &x);
#endif

#define DEFAULT_CALLER_ID "Unknown"
#define PHONE_MAX_BUF 480
#define DEFAULT_GAIN 0x100

static const char desc[] = "Linux Telephony API Support";
static const char type[] = "Phone";
static const char tdesc[] = "Standard Linux Telephony API Driver";
static const char config[] = "phone.conf";

/* Default context for dialtone mode */
static char context[AST_MAX_EXTENSION] = "default";

/* Default language */
static char language[MAX_LANGUAGE] = "";
static int usecnt =0;

static int echocancel = AEC_OFF;

static int silencesupression = 0;

static int prefformat = AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR | AST_FORMAT_ULAW;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of phone_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);
   
/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

/* The private structures of the Phone Jack channels are linked for
   selecting outgoing channels */
   
#define MODE_DIALTONE 	1
#define MODE_IMMEDIATE	2
#define MODE_FXO		3
#define MODE_FXS        4

static struct phone_pvt {
	int fd;							/* Raw file descriptor for this device */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	int mode;						/* Is this in the  */
	int lastformat;					/* Last output format */
	int lastinput;					/* Last input format */
	int ministate;					/* Miniature state, for dialtone mode */
	char dev[256];					/* Device name */
	struct phone_pvt *next;			/* Next channel in list */
	struct ast_frame fr;			/* Frame */
	char offset[AST_FRIENDLY_OFFSET];
	char buf[PHONE_MAX_BUF];					/* Static buffer for reading frames */
	int obuflen;
	int dialtone;
	int txgain, rxgain;             /* gain control for playing, recording  */
									/* 0x100 - 1.0, 0x200 - 2.0, 0x80 - 0.5 */
	int cpt;						/* Call Progress Tone playing? */
	int silencesupression;
	char context[AST_MAX_EXTENSION];
	char obuf[PHONE_MAX_BUF * 2];
	char ext[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char cid_num[AST_MAX_EXTENSION];
	char cid_name[AST_MAX_EXTENSION];
} *iflist = NULL;

static char cid_num[AST_MAX_EXTENSION];
static char cid_name[AST_MAX_EXTENSION];

static struct ast_channel *phone_request(const char *type, int format, void *data, int *cause);
static int phone_digit(struct ast_channel *ast, char digit);
static int phone_call(struct ast_channel *ast, char *dest, int timeout);
static int phone_hangup(struct ast_channel *ast);
static int phone_answer(struct ast_channel *ast);
static struct ast_frame *phone_read(struct ast_channel *ast);
static int phone_write(struct ast_channel *ast, struct ast_frame *frame);
static struct ast_frame *phone_exception(struct ast_channel *ast);
static int phone_send_text(struct ast_channel *ast, const char *text);
static int phone_fixup(struct ast_channel *old, struct ast_channel *new);

static const struct ast_channel_tech phone_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR | AST_FORMAT_ULAW,
	.requester = phone_request,
	.send_digit = phone_digit,
	.call = phone_call,
	.hangup = phone_hangup,
	.answer = phone_answer,
	.read = phone_read,
	.write = phone_write,
	.exception = phone_exception,
	.fixup = phone_fixup
};

static struct ast_channel_tech phone_tech_fxs = {
	.type = type,
	.description = tdesc,
	.requester = phone_request,
	.send_digit = phone_digit,
	.call = phone_call,
	.hangup = phone_hangup,
	.answer = phone_answer,
	.read = phone_read,
	.write = phone_write,
	.exception = phone_exception,
	.write_video = phone_write,
	.send_text = phone_send_text,
	.fixup = phone_fixup
};

static struct ast_channel_tech *cur_tech;

static int phone_fixup(struct ast_channel *old, struct ast_channel *new)
{
	struct phone_pvt *pvt = old->tech_pvt;
	if (pvt && pvt->owner == old)
		pvt->owner = new;
	return 0;
}

static int phone_digit(struct ast_channel *ast, char digit)
{
	struct phone_pvt *p;
	int outdigit;
	p = ast->tech_pvt;
	ast_log(LOG_NOTICE, "Dialed %c\n", digit);
	switch(digit) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		outdigit = digit - '0';
		break;
	case '*':
		outdigit = 11;
		break;
	case '#':
		outdigit = 12;
		break;
	case 'f':	/*flash*/
	case 'F':
		ioctl(p->fd, IXJCTL_PSTN_SET_STATE, PSTN_ON_HOOK);
		usleep(320000);
		ioctl(p->fd, IXJCTL_PSTN_SET_STATE, PSTN_OFF_HOOK);
		p->lastformat = -1;
		return 0;
	default:
		ast_log(LOG_WARNING, "Unknown digit '%c'\n", digit);
		return -1;
	}
	ast_log(LOG_NOTICE, "Dialed %i\n", outdigit);
	ioctl(p->fd, PHONE_PLAY_TONE, outdigit);
	p->lastformat = -1;
	return 0;
}

static int phone_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct phone_pvt *p;

	PHONE_CID cid;
	time_t UtcTime;
	struct tm tm;
	int start;

	time(&UtcTime);
	localtime_r(&UtcTime,&tm);

	memset(&cid, 0, sizeof(PHONE_CID));
	if(&tm != NULL) {
		snprintf(cid.month, sizeof(cid.month), "%02d",(tm.tm_mon + 1));
		snprintf(cid.day, sizeof(cid.day),     "%02d", tm.tm_mday);
		snprintf(cid.hour, sizeof(cid.hour),   "%02d", tm.tm_hour);
		snprintf(cid.min, sizeof(cid.min),     "%02d", tm.tm_min);
	}
	/* the standard format of ast->callerid is:  "name" <number>, but not always complete */
	if (!ast->cid.cid_name || ast_strlen_zero(ast->cid.cid_name))
		strncpy(cid.name, DEFAULT_CALLER_ID, sizeof(cid.name) - 1);
	else
		strncpy(cid.name, ast->cid.cid_name, sizeof(cid.name) - 1);

	if (ast->cid.cid_num) 
		strncpy(cid.number, ast->cid.cid_num, sizeof(cid.number) - 1);

	p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "phone_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Ringing %s on %s (%d)\n", dest, ast->name, ast->fds[0]);

	start = IXJ_PHONE_RING_START(cid);
	if (start == -1)
		return -1;
	
	if (p->mode == MODE_FXS) {
		char *digit = strchr(dest, '/');
		if (digit)
		{
		  digit++;
		  while (*digit)
		    phone_digit(ast, *digit++);
		}
	}
 
  	ast_setstate(ast, AST_STATE_RINGING);
	ast_queue_control(ast, AST_CONTROL_RINGING);
	return 0;
}

static int phone_hangup(struct ast_channel *ast)
{
	struct phone_pvt *p;
	p = ast->tech_pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "phone_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	/* XXX Is there anything we can do to really hang up except stop recording? */
	ast_setstate(ast, AST_STATE_DOWN);
	if (ioctl(p->fd, PHONE_REC_STOP))
		ast_log(LOG_WARNING, "Failed to stop recording\n");
	if (ioctl(p->fd, PHONE_PLAY_STOP))
		ast_log(LOG_WARNING, "Failed to stop playing\n");
	if (ioctl(p->fd, PHONE_RING_STOP))
		ast_log(LOG_WARNING, "Failed to stop ringing\n");
	if (ioctl(p->fd, PHONE_CPT_STOP))
		ast_log(LOG_WARNING, "Failed to stop sounds\n");

	/* If it's an FXO, hang them up */
	if (p->mode == MODE_FXO) {
		if (ioctl(p->fd, PHONE_PSTN_SET_STATE, PSTN_ON_HOOK)) 
			ast_log(LOG_DEBUG, "ioctl(PHONE_PSTN_SET_STATE) failed on %s (%s)\n",ast->name, strerror(errno));
	}

	/* If they're off hook, give a busy signal */
	if (ioctl(p->fd, PHONE_HOOKSTATE)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Got hunghup, giving busy signal\n");
		ioctl(p->fd, PHONE_BUSY);
		p->cpt = 1;
	}
	p->lastformat = -1;
	p->lastinput = -1;
	p->ministate = 0;
	p->obuflen = 0;
	p->dialtone = 0;
	memset(p->ext, 0, sizeof(p->ext));
	((struct phone_pvt *)(ast->tech_pvt))->owner = NULL;
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	restart_monitor();
	return 0;
}

static int phone_setup(struct ast_channel *ast)
{
	struct phone_pvt *p;
	p = ast->tech_pvt;
	ioctl(p->fd, PHONE_CPT_STOP);
	/* Nothing to answering really, just start recording */
	if (ast->rawreadformat == AST_FORMAT_G723_1) {
		/* Prefer g723 */
		ioctl(p->fd, PHONE_REC_STOP);
		if (p->lastinput != AST_FORMAT_G723_1) {
			p->lastinput = AST_FORMAT_G723_1;
			if (ioctl(p->fd, PHONE_REC_CODEC, G723_63)) {
				ast_log(LOG_WARNING, "Failed to set codec to g723.1\n");
				return -1;
			}
		}
	} else if (ast->rawreadformat == AST_FORMAT_SLINEAR) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (p->lastinput != AST_FORMAT_SLINEAR) {
			p->lastinput = AST_FORMAT_SLINEAR;
			if (ioctl(p->fd, PHONE_REC_CODEC, LINEAR16)) {
				ast_log(LOG_WARNING, "Failed to set codec to signed linear 16\n");
				return -1;
			}
		}
	} else if (ast->rawreadformat == AST_FORMAT_ULAW) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (p->lastinput != AST_FORMAT_ULAW) {
			p->lastinput = AST_FORMAT_ULAW;
			if (ioctl(p->fd, PHONE_REC_CODEC, ULAW)) {
				ast_log(LOG_WARNING, "Failed to set codec to uLaw\n");
				return -1;
			}
		}
	} else if (p->mode == MODE_FXS) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (p->lastinput != ast->rawreadformat) {
			p->lastinput = ast->rawreadformat;
			if (ioctl(p->fd, PHONE_REC_CODEC, ast->rawreadformat)) {
				ast_log(LOG_WARNING, "Failed to set codec to %d\n", 
					ast->rawreadformat);
				return -1;
			}
		}
	} else {
		ast_log(LOG_WARNING, "Can't do format %s\n", ast_getformatname(ast->rawreadformat));
		return -1;
	}
	if (ioctl(p->fd, PHONE_REC_START)) {
		ast_log(LOG_WARNING, "Failed to start recording\n");
		return -1;
	}
	/* set the DTMF times (the default is too short) */
	ioctl(p->fd, PHONE_SET_TONE_ON_TIME, 300);
	ioctl(p->fd, PHONE_SET_TONE_OFF_TIME, 200);
	return 0;
}

static int phone_answer(struct ast_channel *ast)
{
	struct phone_pvt *p;
	p = ast->tech_pvt;
	/* In case it's a LineJack, take it off hook */
	if (p->mode == MODE_FXO) {
		if (ioctl(p->fd, PHONE_PSTN_SET_STATE, PSTN_OFF_HOOK)) 
			ast_log(LOG_DEBUG, "ioctl(PHONE_PSTN_SET_STATE) failed on %s (%s)\n", ast->name, strerror(errno));
		else
			ast_log(LOG_DEBUG, "Took linejack off hook\n");
	}
	phone_setup(ast);
	if (option_debug)
		ast_log(LOG_DEBUG, "phone_answer(%s)\n", ast->name);
	ast->rings = 0;
	ast_setstate(ast, AST_STATE_UP);
	return 0;
}

#if 0
static char phone_2digit(char c)
{
	if (c == 12)
		return '#';
	else if (c == 11)
		return '*';
	else if ((c < 10) && (c >= 0))
		return '0' + c - 1;
	else
		return '?';
}
#endif

static struct ast_frame  *phone_exception(struct ast_channel *ast)
{
	int res;
	union telephony_exception phonee;
	struct phone_pvt *p = ast->tech_pvt;
	char digit;

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data =  NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;
	
	phonee.bytes = ioctl(p->fd, PHONE_EXCEPTION);
	if (phonee.bits.dtmf_ready)  {
		if (option_debug)
			ast_log(LOG_DEBUG, "phone_exception(): DTMF\n");
	
		/* We've got a digit -- Just handle this nicely and easily */
		digit =  ioctl(p->fd, PHONE_GET_DTMF_ASCII);
		p->fr.subclass = digit;
		p->fr.frametype = AST_FRAME_DTMF;
		return &p->fr;
	}
	if (phonee.bits.hookstate) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Hookstate changed\n");
		res = ioctl(p->fd, PHONE_HOOKSTATE);
		/* See if we've gone on hook, if so, notify by returning NULL */
		if (option_debug)
			ast_log(LOG_DEBUG, "New hookstate: %d\n", res);
		if (!res && (p->mode != MODE_FXO))
			return NULL;
		else {
			if (ast->_state == AST_STATE_RINGING) {
				/* They've picked up the phone */
				p->fr.frametype = AST_FRAME_CONTROL;
				p->fr.subclass = AST_CONTROL_ANSWER;
				phone_setup(ast);
				ast_setstate(ast, AST_STATE_UP);
				return &p->fr;
			}  else 
				ast_log(LOG_WARNING, "Got off hook in weird state %d\n", ast->_state);
		}
	}
#if 1
	if (phonee.bits.pstn_ring)
		ast_verbose("Unit is ringing\n");
	if (phonee.bits.caller_id) {
		ast_verbose("We have caller ID\n");
	}
	if (phonee.bits.pstn_wink)
		ast_verbose("Detected Wink\n");
#endif
	/* Strange -- nothing there.. */
	p->fr.frametype = AST_FRAME_NULL;
	p->fr.subclass = 0;
	return &p->fr;
}

static struct ast_frame  *phone_read(struct ast_channel *ast)
{
	int res;
	struct phone_pvt *p = ast->tech_pvt;
	

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data =  NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;

	/* Try to read some data... */
	CHECK_BLOCKING(ast);
	res = read(p->fd, p->buf, PHONE_MAX_BUF);
	ast_clear_flag(ast, AST_FLAG_BLOCKING);
	if (res < 0) {
#if 0
		if (errno == EAGAIN) {
			ast_log(LOG_WARNING, "Null frame received\n");
			p->fr.frametype = AST_FRAME_NULL;
			p->fr.subclass = 0;
			return &p->fr;
		}
#endif
		ast_log(LOG_WARNING, "Error reading: %s\n", strerror(errno));
		return NULL;
	}
	p->fr.data = p->buf;
	if (p->mode != MODE_FXS)
	switch(p->buf[0] & 0x3) {
	case '0':
	case '1':
		/* Normal */
		break;
	case '2':
	case '3':
		/* VAD/CNG, only send two words */
		res = 4;
		break;
	}
	p->fr.samples = 240;
	p->fr.datalen = res;
	p->fr.frametype = p->lastinput <= AST_FORMAT_MAX_AUDIO ?
                          AST_FRAME_VOICE : 
			  p->lastinput <= AST_FORMAT_PNG ? AST_FRAME_IMAGE 
			  : AST_FRAME_VIDEO;
	p->fr.subclass = p->lastinput;
	p->fr.offset = AST_FRIENDLY_OFFSET;
	/* Byteswap from little-endian to native-endian */
	if (p->fr.subclass == AST_FORMAT_SLINEAR)
		ast_frame_byteswap_le(&p->fr);
	return &p->fr;
}

static int phone_write_buf(struct phone_pvt *p, const char *buf, int len, int frlen, int swap)
{
	int res;
	/* Store as much of the buffer as we can, then write fixed frames */
	int space = sizeof(p->obuf) - p->obuflen;
	/* Make sure we have enough buffer space to store the frame */
	if (space < len)
		len = space;
	if (swap)
		ast_memcpy_byteswap(p->obuf+p->obuflen, buf, len/2);
	else
		memcpy(p->obuf + p->obuflen, buf, len);
	p->obuflen += len;
	while(p->obuflen > frlen) {
		res = write(p->fd, p->obuf, frlen);
		if (res != frlen) {
			if (res < 1) {
/*
 * Card is in non-blocking mode now and it works well now, but there are
 * lot of messages like this. So, this message is temporarily disabled.
 */
				return 0;
			} else {
				ast_log(LOG_WARNING, "Only wrote %d of %d bytes\n", res, frlen);
			}
		}
		p->obuflen -= frlen;
		/* Move memory if necessary */
		if (p->obuflen) 
			memmove(p->obuf, p->obuf + frlen, p->obuflen);
	}
	return len;
}

static int phone_send_text(struct ast_channel *ast, const char *text)
{
    int length = strlen(text);
    return phone_write_buf(ast->tech_pvt, text, length, length, 0) == 
           length ? 0 : -1;
}

static int phone_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct phone_pvt *p = ast->tech_pvt;
	int res;
	int maxfr=0;
	char *pos;
	int sofar;
	int expected;
	int codecset = 0;
	char tmpbuf[4];
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE && p->mode != MODE_FXS) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING, "Don't know what to do with  frame type '%d'\n", frame->frametype);
		return 0;
	}
	if (!(frame->subclass &
		(AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR | AST_FORMAT_ULAW)) && 
	    p->mode != MODE_FXS) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return -1;
	}
#if 0
	/* If we're not in up mode, go into up mode now */
	if (ast->_state != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
		phone_setup(ast);
	}
#else
	if (ast->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
#endif	
	if (frame->subclass == AST_FORMAT_G723_1) {
		if (p->lastformat != AST_FORMAT_G723_1) {
			ioctl(p->fd, PHONE_PLAY_STOP);
			ioctl(p->fd, PHONE_REC_STOP);
			if (ioctl(p->fd, PHONE_PLAY_CODEC, G723_63)) {
				ast_log(LOG_WARNING, "Unable to set G723.1 mode\n");
				return -1;
			}
			if (ioctl(p->fd, PHONE_REC_CODEC, G723_63)) {
				ast_log(LOG_WARNING, "Unable to set G723.1 mode\n");
				return -1;
			}
			p->lastformat = AST_FORMAT_G723_1;
			p->lastinput = AST_FORMAT_G723_1;
			/* Reset output buffer */
			p->obuflen = 0;
			codecset = 1;
		}
		if (frame->datalen > 24) {
			ast_log(LOG_WARNING, "Frame size too large for G.723.1 (%d bytes)\n", frame->datalen);
			return -1;
		}
		maxfr = 24;
	} else if (frame->subclass == AST_FORMAT_SLINEAR) {
		if (p->lastformat != AST_FORMAT_SLINEAR) {
			ioctl(p->fd, PHONE_PLAY_STOP);
			ioctl(p->fd, PHONE_REC_STOP);
			if (ioctl(p->fd, PHONE_PLAY_CODEC, LINEAR16)) {
				ast_log(LOG_WARNING, "Unable to set 16-bit linear mode\n");
				return -1;
			}
			if (ioctl(p->fd, PHONE_REC_CODEC, LINEAR16)) {
				ast_log(LOG_WARNING, "Unable to set 16-bit linear mode\n");
				return -1;
			}
			p->lastformat = AST_FORMAT_SLINEAR;
			p->lastinput = AST_FORMAT_SLINEAR;
			codecset = 1;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		maxfr = 480;
	} else if (frame->subclass == AST_FORMAT_ULAW) {
		if (p->lastformat != AST_FORMAT_ULAW) {
			ioctl(p->fd, PHONE_PLAY_STOP);
			ioctl(p->fd, PHONE_REC_STOP);
			if (ioctl(p->fd, PHONE_PLAY_CODEC, ULAW)) {
				ast_log(LOG_WARNING, "Unable to set uLaw mode\n");
				return -1;
			}
			if (ioctl(p->fd, PHONE_REC_CODEC, ULAW)) {
				ast_log(LOG_WARNING, "Unable to set uLaw mode\n");
				return -1;
			}
			p->lastformat = AST_FORMAT_ULAW;
			p->lastinput = AST_FORMAT_ULAW;
			codecset = 1;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		maxfr = 240;
	} else {
		if (p->lastformat != frame->subclass) {
			ioctl(p->fd, PHONE_PLAY_STOP);
			ioctl(p->fd, PHONE_REC_STOP);
			if (ioctl(p->fd, PHONE_PLAY_CODEC, frame->subclass)) {
				ast_log(LOG_WARNING, "Unable to set %d mode\n",
					frame->subclass);
				return -1;
			}
			if (ioctl(p->fd, PHONE_REC_CODEC, frame->subclass)) {
				ast_log(LOG_WARNING, "Unable to set %d mode\n",
					frame->subclass);
				return -1;
			}
			p->lastformat = frame->subclass;
			p->lastinput = frame->subclass;
			codecset = 1;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		maxfr = 480;
	}
 	if (codecset) {
		ioctl(p->fd, PHONE_REC_DEPTH, 3);
		ioctl(p->fd, PHONE_PLAY_DEPTH, 3);
		if (ioctl(p->fd, PHONE_PLAY_START)) {
			ast_log(LOG_WARNING, "Failed to start playback\n");
			return -1;
		}
		if (ioctl(p->fd, PHONE_REC_START)) {
			ast_log(LOG_WARNING, "Failed to start recording\n");
			return -1;
		}
	}
	/* If we get here, we have a frame of Appropriate data */
	sofar = 0;
	pos = frame->data;
	while(sofar < frame->datalen) {
		/* Write in no more than maxfr sized frames */
		expected = frame->datalen - sofar;
		if (maxfr < expected)
			expected = maxfr;
		/* XXX Internet Phone Jack does not handle the 4-byte VAD frame properly! XXX 
		   we have to pad it to 24 bytes still.  */
		if (frame->datalen == 4) {
			if (p->silencesupression) {
				memset(tmpbuf + 4, 0, sizeof(tmpbuf) - 4);
				memcpy(tmpbuf, frame->data, 4);
				expected = 24;
				res = phone_write_buf(p, tmpbuf, expected, maxfr, 0);
			}
			res = 4;
			expected=4;
		} else {
			int swap = 0;
#if __BYTE_ORDER == __BIG_ENDIAN
			if (frame->subclass == AST_FORMAT_SLINEAR)
				swap = 1; /* Swap big-endian samples to little-endian as we copy */
#endif
			res = phone_write_buf(p, pos, expected, maxfr, swap);
		}
		if (res != expected) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				if (res < 0) 
					ast_log(LOG_WARNING, "Write returned error (%s)\n", strerror(errno));
	/*
	 * Card is in non-blocking mode now and it works well now, but there are
	 * lot of messages like this. So, this message is temporarily disabled.
	 */
#if 0
				else
					ast_log(LOG_WARNING, "Only wrote %d of %d bytes\n", res, frame->datalen);
#endif
				return -1;
			} else /* Pretend it worked */
				res = expected;
		}
		sofar += res;
		pos += res;
	}
	return 0;
}

static struct ast_channel *phone_new(struct phone_pvt *i, int state, char *context)
{
	struct ast_channel *tmp;
	struct phone_codec_data codec;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		tmp->tech = cur_tech;
		snprintf(tmp->name, sizeof(tmp->name), "Phone/%s", i->dev + 5);
		tmp->type = type;
		tmp->fds[0] = i->fd;
		/* XXX Switching formats silently causes kernel panics XXX */
		if (i->mode == MODE_FXS &&
		    ioctl(i->fd, PHONE_QUERY_CODEC, &codec) == 0) {
			if (codec.type == LINEAR16)
				tmp->nativeformats =
				tmp->rawreadformat =
				tmp->rawwriteformat =
				AST_FORMAT_SLINEAR;
			else {
				tmp->nativeformats =
				tmp->rawreadformat =
				tmp->rawwriteformat =
				prefformat & ~AST_FORMAT_SLINEAR;
			}
		}
		else {
			tmp->nativeformats = prefformat;
			tmp->rawreadformat = prefformat;
			tmp->rawwriteformat = prefformat;
		}
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		if (strlen(i->ext))
			strncpy(tmp->exten, i->ext, sizeof(tmp->exten)-1);
		else
			strncpy(tmp->exten, "s",  sizeof(tmp->exten) - 1);
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);
		if (!ast_strlen_zero(i->cid_num))
			tmp->cid.cid_num = strdup(i->cid_num);
		if (!ast_strlen_zero(i->cid_name))
			tmp->cid.cid_name = strdup(i->cid_name);
		i->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (state == AST_STATE_RING) {
				ioctl(tmp->fds[0], PHONE_RINGBACK);
				i->cpt = 1;
			}
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

static void phone_mini_packet(struct phone_pvt *i)
{
	int res;
	char buf[1024];
	/* Ignore stuff we read... */
	res = read(i->fd, buf, sizeof(buf));
	if (res < 1) {
		ast_log(LOG_WARNING, "Read returned %d: %s\n", res, strerror(errno));
		return;
	}
}

static void phone_check_exception(struct phone_pvt *i)
{
	int offhook=0;
	char digit[2] = {0 , 0};
	union telephony_exception phonee;
	/* XXX Do something XXX */
#if 0
	ast_log(LOG_DEBUG, "Exception!\n");
#endif
	phonee.bytes = ioctl(i->fd, PHONE_EXCEPTION);
	if (phonee.bits.dtmf_ready)  {
		digit[0] = ioctl(i->fd, PHONE_GET_DTMF_ASCII);
		if (i->mode == MODE_DIALTONE || i->mode == MODE_FXS) {
			ioctl(i->fd, PHONE_PLAY_STOP);
			ioctl(i->fd, PHONE_REC_STOP);
			ioctl(i->fd, PHONE_CPT_STOP);
			i->dialtone = 0;
			if (strlen(i->ext) < AST_MAX_EXTENSION - 1)
				strncat(i->ext, digit, sizeof(i->ext) - strlen(i->ext) - 1);
			if ((i->mode != MODE_FXS ||
			     !(phonee.bytes = ioctl(i->fd, PHONE_EXCEPTION)) ||
			     !phonee.bits.dtmf_ready) &&
			    ast_exists_extension(NULL, i->context, i->ext, 1, i->cid_num)) {
				/* It's a valid extension in its context, get moving! */
				phone_new(i, AST_STATE_RING, i->context);
				/* No need to restart monitor, we are the monitor */
			} else if (!ast_canmatch_extension(NULL, i->context, i->ext, 1, i->cid_num)) {
				/* There is nothing in the specified extension that can match anymore.
				   Try the default */
				if (ast_exists_extension(NULL, "default", i->ext, 1, i->cid_num)) {
					/* Check the default, too... */
					phone_new(i, AST_STATE_RING, "default");
					/* XXX This should probably be justified better XXX */
				}  else if (!ast_canmatch_extension(NULL, "default", i->ext, 1, i->cid_num)) {
					/* It's not a valid extension, give a busy signal */
					if (option_debug)
						ast_log(LOG_DEBUG, "%s can't match anything in %s or default\n", i->ext, i->context);
					ioctl(i->fd, PHONE_BUSY);
					i->cpt = 1;
				}
			}
#if 0
			ast_verbose("Extension is %s\n", i->ext);
#endif
		}
	}
	if (phonee.bits.hookstate) {
		offhook = ioctl(i->fd, PHONE_HOOKSTATE);
		if (offhook) {
			if (i->mode == MODE_IMMEDIATE) {
				phone_new(i, AST_STATE_RING, i->context);
			} else if (i->mode == MODE_DIALTONE) {
				ast_mutex_lock(&usecnt_lock);
				usecnt++;
				ast_mutex_unlock(&usecnt_lock);
				ast_update_use_count();
				/* Reset the extension */
				i->ext[0] = '\0';
				/* Play the dialtone */
				i->dialtone++;
				ioctl(i->fd, PHONE_PLAY_STOP);
				ioctl(i->fd, PHONE_PLAY_CODEC, ULAW);
				ioctl(i->fd, PHONE_PLAY_START);
				i->lastformat = -1;
			}
		} else {
			if (i->dialtone) {
				ast_mutex_lock(&usecnt_lock);
				usecnt--;
				ast_mutex_unlock(&usecnt_lock);
				ast_update_use_count();
			}
			memset(i->ext, 0, sizeof(i->ext));
			if (i->cpt)
			{
				ioctl(i->fd, PHONE_CPT_STOP);
				i->cpt = 0;
			}
			ioctl(i->fd, PHONE_PLAY_STOP);
			ioctl(i->fd, PHONE_REC_STOP);
			i->dialtone = 0;
			i->lastformat = -1;
		}
	}
	if (phonee.bits.pstn_ring) {
		ast_verbose("Unit is ringing\n");
		phone_new(i, AST_STATE_RING, i->context);
	}
	if (phonee.bits.caller_id)
		ast_verbose("We have caller ID\n");
	
	
}

static void *do_monitor(void *data)
{
	fd_set rfds, efds;
	int n, res;
	struct phone_pvt *i;
	int tonepos = 0;
	/* The tone we're playing this round */
	struct timeval tv = {0,0};
	int dotone;
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL)) {
		ast_log(LOG_WARNING, "Unable to set cancel type to asynchronous\n");
		return NULL;
	}
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
		   phone_pvt that does not have an associated owner channel */
		n = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		i = iflist;
		dotone = 0;
		while(i) {
			if (FD_ISSET(i->fd, &rfds)) 
				ast_log(LOG_WARNING, "Descriptor %d appears twice (%s)?\n", i->fd, i->dev);
			if (!i->owner) {
				/* This needs to be watched, as it lacks an owner */
				FD_SET(i->fd, &rfds);
				FD_SET(i->fd, &efds);
				if (i->fd > n)
					n = i->fd;
				if (i->dialtone) {
					/* Remember we're going to have to come back and play
					   more dialtones */
					if (!tv.tv_usec && !tv.tv_sec) {
						/* If we're due for a dialtone, play one */
						if (write(i->fd, DialTone + tonepos, 240) != 240)
							ast_log(LOG_WARNING, "Dial tone write error\n");
					}
					dotone++;
				}
			}
			
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_mutex_unlock(&iflock);

		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_mutex_unlock(&monlock);

		/* Wait indefinitely for something to happen */
		if (dotone) {
			/* If we're ready to recycle the time, set it to 30 ms */
			tonepos += 240;
			if (tonepos >= sizeof(DialTone))
					tonepos = 0;
			if (!tv.tv_usec && !tv.tv_sec) {
				tv.tv_usec = 30000;
				tv.tv_sec = 0;
			}
			res = ast_select(n + 1, &rfds, NULL, &efds, &tv);
		} else {
			res = ast_select(n + 1, &rfds, NULL, &efds, NULL);
			tv.tv_usec = 0;
			tv.tv_sec = 0;
			tonepos = 0;
		}
		/* Okay, select has finished.  Let's see what happened.  */
		if (res < 0) {
			ast_log(LOG_WARNING, "select return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* If there are no fd's changed, just continue, it's probably time
		   to play some more dialtones */
		if (!res)
			continue;
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}

		i = iflist;
		for(; i; i=i->next) {
			if (FD_ISSET(i->fd, &rfds)) {
				if (i->owner) {
					continue;
				}
				phone_mini_packet(i);
			}
			if (FD_ISSET(i->fd, &efds)) {
				if (i->owner) {
					continue;
				}
				phone_check_exception(i);
			}
		}
		ast_mutex_unlock(&iflock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor()
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
		if (ast_mutex_lock(&iflock)) {
		  ast_mutex_unlock(&monlock);
		  ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		  return -1;
		}
		pthread_cancel(monitor_thread);
#if 0
		pthread_join(monitor_thread, NULL);
#endif
		ast_mutex_unlock(&iflock);
	}
	/* Start a new monitor */
	if (ast_pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
		return -1;
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static struct phone_pvt *mkif(char *iface, int mode, int txgain, int rxgain)
{
	/* Make a phone_pvt structure for this interface */
	struct phone_pvt *tmp;
	int flags;	
	
	tmp = malloc(sizeof(struct phone_pvt));
	if (tmp) {
		tmp->fd = open(iface, O_RDWR);
		if (tmp->fd < 0) {
			ast_log(LOG_WARNING, "Unable to open '%s'\n", iface);
			free(tmp);
			return NULL;
		}
		if (mode == MODE_FXO) {
			if (ioctl(tmp->fd, IXJCTL_PORT, PORT_PSTN)) 
				ast_log(LOG_DEBUG, "Unable to set port to PSTN\n");
		} else {
			if (ioctl(tmp->fd, IXJCTL_PORT, PORT_POTS)) 
				 if (mode != MODE_FXS)
				      ast_log(LOG_DEBUG, "Unable to set port to POTS\n");
		}
		ioctl(tmp->fd, PHONE_PLAY_STOP);
		ioctl(tmp->fd, PHONE_REC_STOP);
		ioctl(tmp->fd, PHONE_RING_STOP);
		ioctl(tmp->fd, PHONE_CPT_STOP);
		if (ioctl(tmp->fd, PHONE_PSTN_SET_STATE, PSTN_ON_HOOK)) 
			ast_log(LOG_DEBUG, "ioctl(PHONE_PSTN_SET_STATE) failed on %s (%s)\n",iface, strerror(errno));
		if (echocancel != AEC_OFF)
			ioctl(tmp->fd, IXJCTL_AEC_START, echocancel);
		if (silencesupression) 
			tmp->silencesupression = 1;
#ifdef PHONE_VAD
		ioctl(tmp->fd, PHONE_VAD, tmp->silencesupression);
#endif
		tmp->mode = mode;
		flags = fcntl(tmp->fd, F_GETFL);
		fcntl(tmp->fd, F_SETFL, flags | O_NONBLOCK);
		tmp->owner = NULL;
		tmp->lastformat = -1;
		tmp->lastinput = -1;
		tmp->ministate = 0;
		memset(tmp->ext, 0, sizeof(tmp->ext));
		strncpy(tmp->language, language, sizeof(tmp->language)-1);
		strncpy(tmp->dev, iface, sizeof(tmp->dev)-1);
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		tmp->next = NULL;
		tmp->obuflen = 0;
		tmp->dialtone = 0;
		tmp->cpt = 0;
		strncpy(tmp->cid_num, cid_num, sizeof(tmp->cid_num)-1);
		strncpy(tmp->cid_name, cid_name, sizeof(tmp->cid_name)-1);
		tmp->txgain = txgain;
		ioctl(tmp->fd, PHONE_PLAY_VOLUME, tmp->txgain);
		tmp->rxgain = rxgain;
		ioctl(tmp->fd, PHONE_REC_VOLUME, tmp->rxgain);
	}
	return tmp;
}

static struct ast_channel *phone_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct phone_pvt *p;
	struct ast_channel *tmp = NULL;
	char *name = data;

	/* Search for an unowned channel */
	if (ast_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p) {
		if (p->mode == MODE_FXS ||
		    format & (AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR | AST_FORMAT_ULAW)) {
		    size_t length = strlen(p->dev + 5);
    		if (strncmp(name, p->dev + 5, length) == 0 &&
    		    !isalnum(name[length])) {
    		    if (!p->owner) {
                     tmp = phone_new(p, AST_STATE_DOWN, p->context);
                     break;
                } else
                     *cause = AST_CAUSE_BUSY;
            }
		}
		p = p->next;
	}
	ast_mutex_unlock(&iflock);
	restart_monitor();
	if (tmp == NULL) {
		oldformat = format;
		format &= (AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR | AST_FORMAT_ULAW);
		if (!format) {
			ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
			return NULL;
		}
	}
	return tmp;
}

/* parse gain value from config file */
static int parse_gain_value(char *gain_type, char *value)
{
	float gain;

	/* try to scan number */
	if (sscanf(value, "%f", &gain) != 1)
	{
		ast_log(LOG_ERROR, "Invalid %s value '%s' in '%s' config\n",
			value, gain_type, config);
		return DEFAULT_GAIN;
	}

	/* multiplicate gain by 1.0 gain value */ 
	gain = gain * (float)DEFAULT_GAIN;

	/* percentage? */
	if (value[strlen(value) - 1] == '%')
		return (int)(gain / (float)100);

	return (int)gain;
}

static int __unload_module(void)
{
	struct phone_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(cur_tech);
	if (!ast_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if (monitor_thread > AST_PTHREADT_NULL) {
			pthread_cancel(monitor_thread);
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
			if (p->fd > -1)
				close(p->fd);
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

int unload_module(void)
{
	return __unload_module();
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct phone_pvt *tmp;
	int mode = MODE_IMMEDIATE;
	int txgain = DEFAULT_GAIN, rxgain = DEFAULT_GAIN; /* default gain 1.0 */
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
	v = ast_variable_browse(cfg, "interfaces");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "device")) {
				tmp = mkif(v->value, mode, txgain, rxgain);
				if (tmp) {
					tmp->next = iflist;
					iflist = tmp;
					
				} else {
					ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&iflock);
					__unload_module();
					return -1;
				}
		} else if (!strcasecmp(v->name, "silencesupression")) {
			silencesupression = ast_true(v->value);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		} else if (!strcasecmp(v->name, "callerid")) {
			ast_callerid_split(v->value, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
		} else if (!strcasecmp(v->name, "mode")) {
			if (!strncasecmp(v->value, "di", 2)) 
				mode = MODE_DIALTONE;
			else if (!strncasecmp(v->value, "im", 2))
				mode = MODE_IMMEDIATE;
			else if (!strncasecmp(v->value, "fxs", 3)) {
				mode = MODE_FXS;
				prefformat = 0x01ff0000; /* All non-voice */
			}
			else if (!strncasecmp(v->value, "fx", 2))
				mode = MODE_FXO;
			else
				ast_log(LOG_WARNING, "Unknown mode: %s\n", v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "format")) {
			if (!strcasecmp(v->value, "g723.1")) {
				prefformat = AST_FORMAT_G723_1;
			} else if (!strcasecmp(v->value, "slinear")) {
				if (mode == MODE_FXS)
				    prefformat |= AST_FORMAT_SLINEAR;
				else prefformat = AST_FORMAT_SLINEAR;
			} else if (!strcasecmp(v->value, "ulaw")) {
				prefformat = AST_FORMAT_ULAW;
			} else
				ast_log(LOG_WARNING, "Unknown format '%s'\n", v->value);
		} else if (!strcasecmp(v->name, "echocancel")) {
			if (!strcasecmp(v->value, "off")) {
				echocancel = AEC_OFF;
			} else if (!strcasecmp(v->value, "low")) {
				echocancel = AEC_LOW;
			} else if (!strcasecmp(v->value, "medium")) {
				echocancel = AEC_MED;
			} else if (!strcasecmp(v->value, "high")) {
				echocancel = AEC_HIGH;
			} else 
				ast_log(LOG_WARNING, "Unknown echo cancellation '%s'\n", v->value);
		} else if (!strcasecmp(v->name, "txgain")) {
			txgain = parse_gain_value(v->name, v->value);
		} else if (!strcasecmp(v->name, "rxgain")) {
			rxgain = parse_gain_value(v->name, v->value);
		}	
		v = v->next;
	}
	ast_mutex_unlock(&iflock);

	if (mode == MODE_FXS) {
		phone_tech_fxs.capabilities = prefformat;
		cur_tech = &phone_tech_fxs;
	} else
		cur_tech = (struct ast_channel_tech *) &phone_tech;

	/* Make sure we can register our Adtranphone channel type */

	if (ast_channel_register(cur_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_config_destroy(cfg);
		__unload_module();
		return -1;
	}
	ast_config_destroy(cfg);
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

char *description()
{
	return (char *) desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
