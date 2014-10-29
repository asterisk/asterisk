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

/*! \file
 *
 * \brief Generic Linux Telephony Interface driver
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup channel_drivers
 */

/*! \li \ref chan_phone.c uses the configuration file \ref phone.conf
 * \addtogroup configuration_file
 */

/*! \page phone.conf phone.conf
 * \verbinclude phone.conf.sample
 */

/*** MODULEINFO
	<depend>ixjuser</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
#include <linux/telephony.h>
/* Still use some IXJ specific stuff */
#include <linux/version.h>
#include <linux/ixjuser.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/callerid.h"
#include "asterisk/causes.h"
#include "asterisk/stringfields.h"
#include "asterisk/musiconhold.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_compatibility.h"

#include "chan_phone.h"

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

static const char tdesc[] = "Standard Linux Telephony API Driver";
static const char config[] = "phone.conf";

/* Default context for dialtone mode */
static char context[AST_MAX_EXTENSION] = "default";

/* Default language */
static char language[MAX_LANGUAGE] = "";

static int echocancel = AEC_OFF;

static int silencesupression = 0;

static struct ast_format_cap *prefcap;

/* Protect the interface list (of phone_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* Boolean value whether the monitoring thread shall continue. */
static unsigned int monitor;
   
/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

/* The private structures of the Phone Jack channels are linked for
   selecting outgoing channels */
   
#define MODE_DIALTONE 	1
#define MODE_IMMEDIATE	2
#define MODE_FXO	3
#define MODE_FXS        4
#define MODE_SIGMA      5

static struct phone_pvt {
	int fd;							/* Raw file descriptor for this device */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	int mode;						/* Is this in the  */
	struct ast_format *lastformat;            /* Last output format */
	struct ast_format *lastinput;             /* Last input format */
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

static struct ast_channel *phone_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int phone_digit_begin(struct ast_channel *ast, char digit);
static int phone_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int phone_call(struct ast_channel *ast, const char *dest, int timeout);
static int phone_hangup(struct ast_channel *ast);
static int phone_answer(struct ast_channel *ast);
static struct ast_frame *phone_read(struct ast_channel *ast);
static int phone_write(struct ast_channel *ast, struct ast_frame *frame);
static struct ast_frame *phone_exception(struct ast_channel *ast);
static int phone_send_text(struct ast_channel *ast, const char *text);
static int phone_fixup(struct ast_channel *old, struct ast_channel *new);
static int phone_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen);

static struct ast_channel_tech phone_tech = {
	.type = "Phone",
	.description = tdesc,
	.requester = phone_request,
	.send_digit_begin = phone_digit_begin,
	.send_digit_end = phone_digit_end,
	.call = phone_call,
	.hangup = phone_hangup,
	.answer = phone_answer,
	.read = phone_read,
	.write = phone_write,
	.exception = phone_exception,
	.indicate = phone_indicate,
	.fixup = phone_fixup
};

static struct ast_channel_tech phone_tech_fxs = {
	.type = "Phone",
	.description = tdesc,
	.requester = phone_request,
	.send_digit_begin = phone_digit_begin,
	.send_digit_end = phone_digit_end,
	.call = phone_call,
	.hangup = phone_hangup,
	.answer = phone_answer,
	.read = phone_read,
	.write = phone_write,
	.exception = phone_exception,
	.write_video = phone_write,
	.send_text = phone_send_text,
	.indicate = phone_indicate,
	.fixup = phone_fixup
};

static struct ast_channel_tech *cur_tech;

static int phone_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	struct phone_pvt *p = ast_channel_tech_pvt(chan);
	int res=-1;
	ast_debug(1, "Requested indication %d on channel %s\n", condition, ast_channel_name(chan));
	switch(condition) {
	case AST_CONTROL_FLASH:
		ioctl(p->fd, IXJCTL_PSTN_SET_STATE, PSTN_ON_HOOK);
		usleep(320000);
		ioctl(p->fd, IXJCTL_PSTN_SET_STATE, PSTN_OFF_HOOK);
		ao2_cleanup(p->lastformat);
		p->lastformat = NULL;
		res = 0;
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(chan, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(chan);
		break;
	case AST_CONTROL_SRCUPDATE:
		res = 0;
		break;
	case AST_CONTROL_PVT_CAUSE_CODE:
		break;
	default:
		ast_log(LOG_WARNING, "Condition %d is not supported on channel %s\n", condition, ast_channel_name(chan));
	}
	return res;
}

static int phone_fixup(struct ast_channel *old, struct ast_channel *new)
{
	struct phone_pvt *pvt = ast_channel_tech_pvt(old);
	if (pvt && pvt->owner == old)
		pvt->owner = new;
	return 0;
}

static int phone_digit_begin(struct ast_channel *chan, char digit)
{
	/* XXX Modify this callback to let Asterisk support controlling the length of DTMF */
	return 0;
}

static int phone_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct phone_pvt *p;
	int outdigit;
	p = ast_channel_tech_pvt(ast);
	ast_debug(1, "Dialed %c\n", digit);
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
		ao2_cleanup(p->lastformat);
		p->lastformat = NULL;
		return 0;
	default:
		ast_log(LOG_WARNING, "Unknown digit '%c'\n", digit);
		return -1;
	}
	ast_debug(1, "Dialed %d\n", outdigit);
	ioctl(p->fd, PHONE_PLAY_TONE, outdigit);
	ao2_cleanup(p->lastformat);
	p->lastformat = NULL;
	return 0;
}

static int phone_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct phone_pvt *p;

	PHONE_CID cid;
	struct timeval UtcTime = ast_tvnow();
	struct ast_tm tm;
	int start;

	ast_localtime(&UtcTime, &tm, NULL);

	memset(&cid, 0, sizeof(PHONE_CID));
    snprintf(cid.month, sizeof(cid.month), "%02d",(tm.tm_mon + 1));
    snprintf(cid.day, sizeof(cid.day),     "%02d", tm.tm_mday);
    snprintf(cid.hour, sizeof(cid.hour),   "%02d", tm.tm_hour);
    snprintf(cid.min, sizeof(cid.min),     "%02d", tm.tm_min);
	/* the standard format of ast->callerid is:  "name" <number>, but not always complete */
	if (!ast_channel_connected(ast)->id.name.valid
		|| ast_strlen_zero(ast_channel_connected(ast)->id.name.str)) {
		strcpy(cid.name, DEFAULT_CALLER_ID);
	} else {
		ast_copy_string(cid.name, ast_channel_connected(ast)->id.name.str, sizeof(cid.name));
	}

	if (ast_channel_connected(ast)->id.number.valid && ast_channel_connected(ast)->id.number.str) {
		ast_copy_string(cid.number, ast_channel_connected(ast)->id.number.str, sizeof(cid.number));
	}

	p = ast_channel_tech_pvt(ast);

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "phone_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}
	ast_debug(1, "Ringing %s on %s (%d)\n", dest, ast_channel_name(ast), ast_channel_fd(ast, 0));

	start = IXJ_PHONE_RING_START(cid);
	if (start == -1)
		return -1;
	
	if (p->mode == MODE_FXS) {
		const char *digit = strchr(dest, '/');
		if (digit)
		{
		  digit++;
		  while (*digit)
		    phone_digit_end(ast, *digit++, 0);
		}
	}
 
  	ast_setstate(ast, AST_STATE_RINGING);
	ast_queue_control(ast, AST_CONTROL_RINGING);
	return 0;
}

static int phone_hangup(struct ast_channel *ast)
{
	struct phone_pvt *p;
	p = ast_channel_tech_pvt(ast);
	ast_debug(1, "phone_hangup(%s)\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast)) {
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
			ast_debug(1, "ioctl(PHONE_PSTN_SET_STATE) failed on %s (%s)\n",ast_channel_name(ast), strerror(errno));
	}

	/* If they're off hook, give a busy signal */
	if (ioctl(p->fd, PHONE_HOOKSTATE)) {
		ast_debug(1, "Got hunghup, giving busy signal\n");
		ioctl(p->fd, PHONE_BUSY);
		p->cpt = 1;
	}
	ao2_cleanup(p->lastformat);
	p->lastformat = NULL;
	ao2_cleanup(p->lastinput);
	p->lastinput = NULL;
	p->ministate = 0;
	p->obuflen = 0;
	p->dialtone = 0;
	memset(p->ext, 0, sizeof(p->ext));
	((struct phone_pvt *)(ast_channel_tech_pvt(ast)))->owner = NULL;
	ast_module_unref(ast_module_info->self);
	ast_verb(3, "Hungup '%s'\n", ast_channel_name(ast));
	ast_channel_tech_pvt_set(ast, NULL);
	ast_setstate(ast, AST_STATE_DOWN);
	restart_monitor();
	return 0;
}

static int phone_setup(struct ast_channel *ast)
{
	struct phone_pvt *p;
	p = ast_channel_tech_pvt(ast);
	ioctl(p->fd, PHONE_CPT_STOP);
	/* Nothing to answering really, just start recording */
	if (ast_format_cmp(ast_channel_rawreadformat(ast), ast_format_g729) == AST_FORMAT_CMP_EQUAL) {
		/* Prefer g729 */
		ioctl(p->fd, PHONE_REC_STOP);
		if (!p->lastinput || (ast_format_cmp(p->lastinput, ast_format_g729) != AST_FORMAT_CMP_EQUAL)) {
			ao2_replace(p->lastinput, ast_format_g729);
			if (ioctl(p->fd, PHONE_REC_CODEC, G729)) {
				ast_log(LOG_WARNING, "Failed to set codec to g729\n");
				return -1;
			}
		}
	} else if (ast_format_cmp(ast_channel_rawreadformat(ast), ast_format_g723) == AST_FORMAT_CMP_EQUAL) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (!p->lastinput || (ast_format_cmp(p->lastinput, ast_format_g723) != AST_FORMAT_CMP_EQUAL)) {
			ao2_replace(p->lastinput, ast_format_g723);
			if (ioctl(p->fd, PHONE_REC_CODEC, G723_63)) {
				ast_log(LOG_WARNING, "Failed to set codec to g723.1\n");
				return -1;
			}
		}
	} else if (ast_format_cmp(ast_channel_rawreadformat(ast), ast_format_slin) == AST_FORMAT_CMP_EQUAL) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (!p->lastinput || (ast_format_cmp(p->lastinput, ast_format_slin) != AST_FORMAT_CMP_EQUAL)) {
			ao2_replace(p->lastinput, ast_format_slin);
			if (ioctl(p->fd, PHONE_REC_CODEC, LINEAR16)) {
				ast_log(LOG_WARNING, "Failed to set codec to signed linear 16\n");
				return -1;
			}
		}
	} else if (ast_format_cmp(ast_channel_rawreadformat(ast), ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (!p->lastinput || (ast_format_cmp(p->lastinput, ast_format_ulaw) != AST_FORMAT_CMP_EQUAL)) {
			ao2_replace(p->lastinput, ast_format_ulaw);
			if (ioctl(p->fd, PHONE_REC_CODEC, ULAW)) {
				ast_log(LOG_WARNING, "Failed to set codec to uLaw\n");
				return -1;
			}
		}
	} else if (p->mode == MODE_FXS) {
		ioctl(p->fd, PHONE_REC_STOP);
		if (!p->lastinput || (ast_format_cmp(p->lastinput, ast_channel_rawreadformat(ast)) == AST_FORMAT_CMP_NOT_EQUAL)) {
			ao2_replace(p->lastinput, ast_channel_rawreadformat(ast));
			if (ioctl(p->fd, PHONE_REC_CODEC, ast_channel_rawreadformat(ast))) {
				ast_log(LOG_WARNING, "Failed to set codec to %s\n", 
					ast_format_get_name(ast_channel_rawreadformat(ast)));
				return -1;
			}
		}
	} else {
		ast_log(LOG_WARNING, "Can't do format %s\n", ast_format_get_name(ast_channel_rawreadformat(ast)));
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
	p = ast_channel_tech_pvt(ast);
	/* In case it's a LineJack, take it off hook */
	if (p->mode == MODE_FXO) {
		if (ioctl(p->fd, PHONE_PSTN_SET_STATE, PSTN_OFF_HOOK))
			ast_debug(1, "ioctl(PHONE_PSTN_SET_STATE) failed on %s (%s)\n", ast_channel_name(ast), strerror(errno));
		else
			ast_debug(1, "Took linejack off hook\n");
	}
	phone_setup(ast);
	ast_debug(1, "phone_answer(%s)\n", ast_channel_name(ast));
	ast_channel_rings_set(ast, 0);
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
	struct phone_pvt *p = ast_channel_tech_pvt(ast);
	char digit;

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data.ptr =  NULL;
	p->fr.src = "Phone";
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery = ast_tv(0,0);
	
	phonee.bytes = ioctl(p->fd, PHONE_EXCEPTION);
	if (phonee.bits.dtmf_ready)  {
		ast_debug(1, "phone_exception(): DTMF\n");
	
		/* We've got a digit -- Just handle this nicely and easily */
		digit =  ioctl(p->fd, PHONE_GET_DTMF_ASCII);
		p->fr.subclass.integer = digit;
		p->fr.frametype = AST_FRAME_DTMF;
		return &p->fr;
	}
	if (phonee.bits.hookstate) {
		ast_debug(1, "Hookstate changed\n");
		res = ioctl(p->fd, PHONE_HOOKSTATE);
		/* See if we've gone on hook, if so, notify by returning NULL */
		ast_debug(1, "New hookstate: %d\n", res);
		if (!res && (p->mode != MODE_FXO))
			return NULL;
		else {
			if (ast_channel_state(ast) == AST_STATE_RINGING) {
				/* They've picked up the phone */
				p->fr.frametype = AST_FRAME_CONTROL;
				p->fr.subclass.integer = AST_CONTROL_ANSWER;
				phone_setup(ast);
				ast_setstate(ast, AST_STATE_UP);
				return &p->fr;
			}  else 
				ast_log(LOG_WARNING, "Got off hook in weird state %u\n", ast_channel_state(ast));
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
	p->fr.subclass.integer = 0;
	return &p->fr;
}

static struct ast_frame  *phone_read(struct ast_channel *ast)
{
	int res;
	struct phone_pvt *p = ast_channel_tech_pvt(ast);
	

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data.ptr =  NULL;
	p->fr.src = "Phone";
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery = ast_tv(0,0);

	/* Try to read some data... */
	CHECK_BLOCKING(ast);
	res = read(p->fd, p->buf, PHONE_MAX_BUF);
	ast_clear_flag(ast_channel_flags(ast), AST_FLAG_BLOCKING);
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
	p->fr.data.ptr = p->buf;
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
	p->fr.frametype = ast_format_get_type(p->lastinput) == AST_MEDIA_TYPE_AUDIO ?
		AST_FRAME_VOICE : ast_format_get_type(p->lastinput) == AST_MEDIA_TYPE_IMAGE ?
		AST_FRAME_IMAGE : AST_FRAME_VIDEO;
	p->fr.subclass.format = p->lastinput;
	p->fr.offset = AST_FRIENDLY_OFFSET;
	/* Byteswap from little-endian to native-endian */
	if (ast_format_cmp(p->fr.subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)
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
		ast_swapcopy_samples(p->obuf+p->obuflen, buf, len/2);
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
    return phone_write_buf(ast_channel_tech_pvt(ast), text, length, length, 0) == 
           length ? 0 : -1;
}

static int phone_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct phone_pvt *p = ast_channel_tech_pvt(ast);
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
			ast_log(LOG_WARNING, "Don't know what to do with  frame type '%u'\n", frame->frametype);
		return 0;
	}
#if 0
	/* If we're not in up mode, go into up mode now */
	if (ast->_state != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
		phone_setup(ast);
	}
#else
	if (ast_channel_state(ast) != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
#endif	
	if (ast_format_cmp(frame->subclass.format, ast_format_g729) == AST_FORMAT_CMP_EQUAL) {
		if (!p->lastformat || (ast_format_cmp(p->lastformat, ast_format_g729) != AST_FORMAT_CMP_EQUAL)) {
			ioctl(p->fd, PHONE_PLAY_STOP);
			ioctl(p->fd, PHONE_REC_STOP);
			if (ioctl(p->fd, PHONE_PLAY_CODEC, G729)) {
				ast_log(LOG_WARNING, "Unable to set G729 mode\n");
				return -1;
			}
			if (ioctl(p->fd, PHONE_REC_CODEC, G729)) {
				ast_log(LOG_WARNING, "Unable to set G729 mode\n");
				return -1;
			}
			ao2_replace(p->lastformat, ast_format_g729);
			ao2_replace(p->lastinput, ast_format_g729);
			/* Reset output buffer */
			p->obuflen = 0;
			codecset = 1;
		}
		if (frame->datalen > 80) {
			ast_log(LOG_WARNING, "Frame size too large for G.729 (%d bytes)\n", frame->datalen);
			return -1;
		}
		maxfr = 80;
    } else if (ast_format_cmp(frame->subclass.format, ast_format_g723) == AST_FORMAT_CMP_EQUAL) {
		if (!p->lastformat || (ast_format_cmp(p->lastformat, ast_format_g723) != AST_FORMAT_CMP_EQUAL)) {
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
			ao2_replace(p->lastformat, ast_format_g723);
			ao2_replace(p->lastinput, ast_format_g723);
			/* Reset output buffer */
			p->obuflen = 0;
			codecset = 1;
		}
		if (frame->datalen > 24) {
			ast_log(LOG_WARNING, "Frame size too large for G.723.1 (%d bytes)\n", frame->datalen);
			return -1;
		}
		maxfr = 24;
	} else if (ast_format_cmp(frame->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL) {
		if (!p->lastformat || (ast_format_cmp(p->lastformat, ast_format_slin) != AST_FORMAT_CMP_EQUAL)) {
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
			ao2_replace(p->lastformat, ast_format_slin);
			ao2_replace(p->lastinput, ast_format_slin);
			codecset = 1;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		maxfr = 480;
	} else if (ast_format_cmp(frame->subclass.format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		if (!p->lastformat || (ast_format_cmp(p->lastformat, ast_format_ulaw) != AST_FORMAT_CMP_EQUAL)) {
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
			ao2_replace(p->lastformat, ast_format_ulaw);
			ao2_replace(p->lastinput, ast_format_ulaw);
			codecset = 1;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		maxfr = 240;
	} else {
		if (!p->lastformat || (ast_format_cmp(p->lastformat, frame->subclass.format) != AST_FORMAT_CMP_EQUAL)) {
			ioctl(p->fd, PHONE_PLAY_STOP);
			ioctl(p->fd, PHONE_REC_STOP);
			if (ioctl(p->fd, PHONE_PLAY_CODEC, ast_format_compatibility_format2bitfield(frame->subclass.format))) {
				ast_log(LOG_WARNING, "Unable to set %s mode\n",
					ast_format_get_name(frame->subclass.format));
				return -1;
			}
			if (ioctl(p->fd, PHONE_REC_CODEC, ast_format_compatibility_format2bitfield(frame->subclass.format))) {
				ast_log(LOG_WARNING, "Unable to set %s mode\n",
					ast_format_get_name(frame->subclass.format));
				return -1;
			}
			ao2_replace(p->lastformat, frame->subclass.format);
			ao2_replace(p->lastinput, frame->subclass.format);
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
	pos = frame->data.ptr;
	while(sofar < frame->datalen) {
		/* Write in no more than maxfr sized frames */
		expected = frame->datalen - sofar;
		if (maxfr < expected)
			expected = maxfr;
		/* XXX Internet Phone Jack does not handle the 4-byte VAD frame properly! XXX 
		   we have to pad it to 24 bytes still.  */
		if (frame->datalen == 4) {
			if (p->silencesupression) {
				memcpy(tmpbuf, frame->data.ptr, 4);
				expected = 24;
				res = phone_write_buf(p, tmpbuf, expected, maxfr, 0);
			}
			res = 4;
			expected=4;
		} else {
			int swap = 0;
#if __BYTE_ORDER == __BIG_ENDIAN
			if (ast_format_cmp(frame->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)
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

static struct ast_channel *phone_new(struct phone_pvt *i, int state, char *cntx, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_format_cap *caps = NULL;
	struct ast_channel *tmp;
	struct phone_codec_data queried_codec;
	struct ast_format *tmpfmt;
	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	tmp = ast_channel_alloc(1, state, i->cid_num, i->cid_name, "", i->ext, i->context, assignedids, requestor, 0, "Phone/%s", i->dev + 5);
	if (tmp && caps) {
		ast_channel_lock(tmp);
		ast_channel_tech_set(tmp, cur_tech);
		ast_channel_set_fd(tmp, 0, i->fd);
		/* XXX Switching formats silently causes kernel panics XXX */
		if (i->mode == MODE_FXS &&
		    ioctl(i->fd, PHONE_QUERY_CODEC, &queried_codec) == 0) {
			if (queried_codec.type == LINEAR16) {
				ast_format_cap_append(caps, ast_format_slin, 0);
			} else {
				ast_format_cap_remove(prefcap, ast_format_slin);
				ast_format_cap_append_from_cap(caps, prefcap, AST_MEDIA_TYPE_UNKNOWN);
			}
		} else {
			ast_format_cap_append_from_cap(caps, prefcap, AST_MEDIA_TYPE_UNKNOWN);
		}
		tmpfmt = ast_format_cap_get_format(caps, 0);
		ast_channel_nativeformats_set(tmp, caps);
		ao2_ref(caps, -1);
		ast_channel_set_rawreadformat(tmp, tmpfmt);
		ast_channel_set_rawwriteformat(tmp, tmpfmt);
		ao2_ref(tmpfmt, -1);
		/* no need to call ast_setstate: the channel_alloc already did its job */
		if (state == AST_STATE_RING)
			ast_channel_rings_set(tmp, 1);
		ast_channel_tech_pvt_set(tmp, i);
		ast_channel_context_set(tmp, cntx);
		if (!ast_strlen_zero(i->ext))
			ast_channel_exten_set(tmp, i->ext);
		else
			ast_channel_exten_set(tmp, "s");
		if (!ast_strlen_zero(i->language))
			ast_channel_language_set(tmp, i->language);

		/* Don't use ast_set_callerid() here because it will
		 * generate a NewCallerID event before the NewChannel event */
		if (!ast_strlen_zero(i->cid_num)) {
			ast_channel_caller(tmp)->ani.number.valid = 1;
			ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
		}

		i->owner = tmp;
		ast_module_ref(ast_module_info->self);
		ast_channel_unlock(tmp);
		if (state != AST_STATE_DOWN) {
			if (state == AST_STATE_RING) {
				ioctl(ast_channel_fd(tmp, 0), PHONE_RINGBACK);
				i->cpt = 1;
			}
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
				ast_hangup(tmp);
			}
		}
	} else {
		ao2_cleanup(caps);
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	}
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
	ast_debug(1, "Exception!\n");
#endif
	phonee.bytes = ioctl(i->fd, PHONE_EXCEPTION);
	if (phonee.bits.dtmf_ready)  {
		digit[0] = ioctl(i->fd, PHONE_GET_DTMF_ASCII);
		if (i->mode == MODE_DIALTONE || i->mode == MODE_FXS || i->mode == MODE_SIGMA) {
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
				phone_new(i, AST_STATE_RING, i->context, NULL, NULL);
				/* No need to restart monitor, we are the monitor */
			} else if (!ast_canmatch_extension(NULL, i->context, i->ext, 1, i->cid_num)) {
				/* There is nothing in the specified extension that can match anymore.
				   Try the default */
				if (ast_exists_extension(NULL, "default", i->ext, 1, i->cid_num)) {
					/* Check the default, too... */
					phone_new(i, AST_STATE_RING, "default", NULL, NULL);
					/* XXX This should probably be justified better XXX */
				}  else if (!ast_canmatch_extension(NULL, "default", i->ext, 1, i->cid_num)) {
					/* It's not a valid extension, give a busy signal */
					ast_debug(1, "%s can't match anything in %s or default\n", i->ext, i->context);
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
				phone_new(i, AST_STATE_RING, i->context, NULL, NULL);
			} else if (i->mode == MODE_DIALTONE) {
				ast_module_ref(ast_module_info->self);
				/* Reset the extension */
				i->ext[0] = '\0';
				/* Play the dialtone */
				i->dialtone++;
				ioctl(i->fd, PHONE_PLAY_STOP);
				ioctl(i->fd, PHONE_PLAY_CODEC, ULAW);
				ioctl(i->fd, PHONE_PLAY_START);
				ao2_cleanup(i->lastformat);
				i->lastformat = NULL;
			} else if (i->mode == MODE_SIGMA) {
				ast_module_ref(ast_module_info->self);
				/* Reset the extension */
				i->ext[0] = '\0';
				/* Play the dialtone */
				i->dialtone++;
				ioctl(i->fd, PHONE_DIALTONE);
			}
		} else {
			if (i->dialtone)
				ast_module_unref(ast_module_info->self);
			memset(i->ext, 0, sizeof(i->ext));
			if (i->cpt)
			{
				ioctl(i->fd, PHONE_CPT_STOP);
				i->cpt = 0;
			}
			ioctl(i->fd, PHONE_PLAY_STOP);
			ioctl(i->fd, PHONE_REC_STOP);
			i->dialtone = 0;
			ao2_cleanup(i->lastformat);
			i->lastformat = NULL;
		}
	}
	if (phonee.bits.pstn_ring) {
		ast_verbose("Unit is ringing\n");
		phone_new(i, AST_STATE_RING, i->context, NULL, NULL);
	}
	if (phonee.bits.caller_id)
		ast_verbose("We have caller ID\n");
	
	
}

static void *do_monitor(void *data)
{
	struct pollfd *fds = NULL;
	int nfds = 0, inuse_fds = 0, res;
	struct phone_pvt *i;
	int tonepos = 0;
	/* The tone we're playing this round */
	struct timeval to = { 0, 0 };
	int dotone;
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	while (monitor) {
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		/* Lock the interface list */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_ERROR, "Unable to grab interface lock\n");
			return NULL;
		}
		/* Build the stuff we're going to select on, that is the socket of every
		   phone_pvt that does not have an associated owner channel */
		i = iflist;
		dotone = 0;
		inuse_fds = 0;
		for (i = iflist; i; i = i->next) {
			if (!i->owner) {
				/* This needs to be watched, as it lacks an owner */
				if (inuse_fds == nfds) {
					void *tmp = ast_realloc(fds, (nfds + 1) * sizeof(*fds));
					if (!tmp) {
						/* Avoid leaking */
						continue;
					}
					fds = tmp;
					nfds++;
				}
				fds[inuse_fds].fd = i->fd;
				fds[inuse_fds].events = POLLIN | POLLERR;
				fds[inuse_fds].revents = 0;
				inuse_fds++;

				if (i->dialtone && i->mode != MODE_SIGMA) {
					/* Remember we're going to have to come back and play
					   more dialtones */
					if (ast_tvzero(to)) {
						/* If we're due for a dialtone, play one */
						if (write(i->fd, DialTone + tonepos, 240) != 240) {
							ast_log(LOG_WARNING, "Dial tone write error\n");
						}
					}
					dotone++;
				}
			}
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_mutex_unlock(&iflock);

		/* Wait indefinitely for something to happen */
		if (dotone && i && i->mode != MODE_SIGMA) {
			/* If we're ready to recycle the time, set it to 30 ms */
			tonepos += 240;
			if (tonepos >= sizeof(DialTone)) {
				tonepos = 0;
			}
			if (ast_tvzero(to)) {
				to = ast_tv(0, 30000);
			}
			res = ast_poll2(fds, inuse_fds, &to);
		} else {
			res = ast_poll(fds, inuse_fds, -1);
			to = ast_tv(0, 0);
			tonepos = 0;
		}
		/* Okay, select has finished.  Let's see what happened.  */
		if (res < 0) {
			ast_debug(1, "poll returned %d: %s\n", res, strerror(errno));
			continue;
		}
		/* If there are no fd's changed, just continue, it's probably time
		   to play some more dialtones */
		if (!res) {
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}

		for (i = iflist; i; i = i->next) {
			int j;
			/* Find the record */
			for (j = 0; j < inuse_fds; j++) {
				if (fds[j].fd == i->fd) {
					break;
				}
			}

			/* Not found? */
			if (j == inuse_fds) {
				continue;
			}

			if (fds[j].revents & POLLIN) {
				if (i->owner) {
					continue;
				}
				phone_mini_packet(i);
			}
			if (fds[j].revents & POLLERR) {
				if (i->owner) {
					continue;
				}
				phone_check_exception(i);
			}
		}
		ast_mutex_unlock(&iflock);
	}
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
		monitor = 0;
		while (pthread_kill(monitor_thread, SIGURG) == 0)
			sched_yield();
		pthread_join(monitor_thread, NULL);
		ast_mutex_unlock(&iflock);
	}
	monitor = 1;
	/* Start a new monitor */
	if (ast_pthread_create_background(&monitor_thread, NULL, do_monitor, NULL) < 0) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
		return -1;
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static struct phone_pvt *mkif(const char *iface, int mode, int txgain, int rxgain)
{
	/* Make a phone_pvt structure for this interface */
	struct phone_pvt *tmp;
	int flags;	
	
	tmp = ast_calloc(1, sizeof(*tmp));
	if (tmp) {
		tmp->fd = open(iface, O_RDWR);
		if (tmp->fd < 0) {
			ast_log(LOG_WARNING, "Unable to open '%s'\n", iface);
			ast_free(tmp);
			return NULL;
		}
		if (mode == MODE_FXO) {
			if (ioctl(tmp->fd, IXJCTL_PORT, PORT_PSTN)) {
				ast_debug(1, "Unable to set port to PSTN\n");
			}
		} else {
			if (ioctl(tmp->fd, IXJCTL_PORT, PORT_POTS)) 
				 if (mode != MODE_FXS)
				      ast_debug(1, "Unable to set port to POTS\n");
		}
		ioctl(tmp->fd, PHONE_PLAY_STOP);
		ioctl(tmp->fd, PHONE_REC_STOP);
		ioctl(tmp->fd, PHONE_RING_STOP);
		ioctl(tmp->fd, PHONE_CPT_STOP);
		if (ioctl(tmp->fd, PHONE_PSTN_SET_STATE, PSTN_ON_HOOK))
			ast_debug(1, "ioctl(PHONE_PSTN_SET_STATE) failed on %s (%s)\n",iface, strerror(errno));
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
		ao2_cleanup(tmp->lastformat);
		tmp->lastformat = NULL;
		ao2_cleanup(tmp->lastinput);
		tmp->lastinput = NULL;
		tmp->ministate = 0;
		memset(tmp->ext, 0, sizeof(tmp->ext));
		ast_copy_string(tmp->language, language, sizeof(tmp->language));
		ast_copy_string(tmp->dev, iface, sizeof(tmp->dev));
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
		tmp->next = NULL;
		tmp->obuflen = 0;
		tmp->dialtone = 0;
		tmp->cpt = 0;
		ast_copy_string(tmp->cid_num, cid_num, sizeof(tmp->cid_num));
		ast_copy_string(tmp->cid_name, cid_name, sizeof(tmp->cid_name));
		tmp->txgain = txgain;
		ioctl(tmp->fd, PHONE_PLAY_VOLUME, tmp->txgain);
		tmp->rxgain = rxgain;
		ioctl(tmp->fd, PHONE_REC_VOLUME, tmp->rxgain);
	}
	return tmp;
}

static struct ast_channel *phone_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct phone_pvt *p;
	struct ast_channel *tmp = NULL;
	const char *name = data;

	/* Search for an unowned channel */
	if (ast_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p) {
		if (p->mode == MODE_FXS || (ast_format_cap_iscompatible(cap, phone_tech.capabilities))) {
			size_t length = strlen(p->dev + 5);
    		if (strncmp(name, p->dev + 5, length) == 0 &&
    		    !isalnum(name[length])) {
    		    if (!p->owner) {
                     tmp = phone_new(p, AST_STATE_DOWN, p->context, assignedids, requestor);
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
		if (!(ast_format_cap_iscompatible(cap, phone_tech.capabilities))) {
			struct ast_str *codec_buf = ast_str_alloca(64);
			ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n",
				ast_format_cap_get_names(cap, &codec_buf));
			return NULL;
		}
	}
	return tmp;
}

/* parse gain value from config file */
static int parse_gain_value(const char *gain_type, const char *value)
{
	float gain;

	/* try to scan number */
	if (sscanf(value, "%30f", &gain) != 1)
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
	if (cur_tech)
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
			monitor = 0;
			while (pthread_kill(monitor_thread, SIGURG) == 0)
				sched_yield();
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
			ast_free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	ao2_ref(phone_tech.capabilities, -1);
	ao2_ref(phone_tech_fxs.capabilities, -1);
	ao2_ref(prefcap, -1);

	return 0;
}

static int unload_module(void)
{
	return __unload_module();
}

static int load_module(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct phone_pvt *tmp;
	int mode = MODE_IMMEDIATE;
	int txgain = DEFAULT_GAIN, rxgain = DEFAULT_GAIN; /* default gain 1.0 */
	struct ast_flags config_flags = { 0 };

	if (!(phone_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_append(phone_tech.capabilities, ast_format_g723, 0);
	ast_format_cap_append(phone_tech.capabilities, ast_format_slin, 0);
	ast_format_cap_append(phone_tech.capabilities, ast_format_ulaw, 0);
	ast_format_cap_append(phone_tech.capabilities, ast_format_g729, 0);

	if (!(prefcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append_from_cap(prefcap, phone_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);
	if (!(phone_tech_fxs.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if ((cfg = ast_config_load(config, config_flags)) == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_mutex_lock(&iflock)) {
		/* It's a little silly to lock it, but we mind as well just to be sure */
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return AST_MODULE_LOAD_FAILURE;
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
					return AST_MODULE_LOAD_FAILURE;
				}
		} else if (!strcasecmp(v->name, "silencesupression")) {
			silencesupression = ast_true(v->value);
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(language, v->value, sizeof(language));
		} else if (!strcasecmp(v->name, "callerid")) {
			ast_callerid_split(v->value, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
		} else if (!strcasecmp(v->name, "mode")) {
			if (!strncasecmp(v->value, "di", 2)) 
				mode = MODE_DIALTONE;
			else if (!strncasecmp(v->value, "sig", 3))
				mode = MODE_SIGMA;
			else if (!strncasecmp(v->value, "im", 2))
				mode = MODE_IMMEDIATE;
			else if (!strncasecmp(v->value, "fxs", 3)) {
				mode = MODE_FXS;
				ast_format_cap_remove_by_type(prefcap, AST_MEDIA_TYPE_AUDIO); /* All non-voice */
			}
			else if (!strncasecmp(v->value, "fx", 2))
				mode = MODE_FXO;
			else
				ast_log(LOG_WARNING, "Unknown mode: %s\n", v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(context, v->value, sizeof(context));
		} else if (!strcasecmp(v->name, "format")) {
			if (!strcasecmp(v->value, "g729")) {
				ast_format_cap_remove_by_type(prefcap, AST_MEDIA_TYPE_UNKNOWN);
				ast_format_cap_append(prefcap, ast_format_g729, 0);
			} else if (!strcasecmp(v->value, "g723.1")) {
				ast_format_cap_remove_by_type(prefcap, AST_MEDIA_TYPE_UNKNOWN);
				ast_format_cap_append(prefcap, ast_format_g723, 0);
			} else if (!strcasecmp(v->value, "slinear")) {
				if (mode == MODE_FXS) {
					ast_format_cap_append(prefcap, ast_format_slin, 0);
				} else {
					ast_format_cap_remove_by_type(prefcap, AST_MEDIA_TYPE_UNKNOWN);
					ast_format_cap_append(prefcap, ast_format_slin, 0);
				}
			} else if (!strcasecmp(v->value, "ulaw")) {
				ast_format_cap_remove_by_type(prefcap, AST_MEDIA_TYPE_UNKNOWN);
				ast_format_cap_append(prefcap, ast_format_ulaw, 0);
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
		ast_format_cap_append_from_cap(phone_tech_fxs.capabilities, prefcap, AST_MEDIA_TYPE_UNKNOWN);
		cur_tech = &phone_tech_fxs;
	} else
		cur_tech = (struct ast_channel_tech *) &phone_tech;

	/* Make sure we can register our Adtranphone channel type */

	if (ast_channel_register(cur_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Phone'\n");
		ast_config_destroy(cfg);
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_config_destroy(cfg);
	/* And start the monitor for the first time */
	restart_monitor();
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Linux Telephony API Support");

