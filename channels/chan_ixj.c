/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * QuickNet Internet Phone Jack Channel
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
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
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include "ixjuser.h"
#include "DialTone.h"

#define IXJ_MAX_BUF 480

static char *desc = "QuickNet Internet Phone Jack";
static char *type = "PhoneJack";
static char *tdesc = "QuickNet Internet Phone Jack";
static char *config = "ixj.conf";

/* Default context for dialtone mode */
static char context[AST_MAX_EXTENSION] = "default";

char *ignore_rcs_id_for_chan_ixj = ixjuser_h_rcsid;

static int usecnt =0;
static pthread_mutex_t usecnt_lock = PTHREAD_MUTEX_INITIALIZER;

/* Protect the interface list (of ixj_pvt's) */
static pthread_mutex_t iflock = PTHREAD_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static pthread_mutex_t monlock = PTHREAD_MUTEX_INITIALIZER;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = -1;

static int restart_monitor();

/* The private structures of the Phone Jack channels are linked for
   selecting outgoing channels */
   
#define MODE_DIALTONE 	1
#define MODE_IMMEDIATE	2
   
static struct ixj_pvt {
	int fd;							/* Raw file descriptor for this device */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	int mode;						/* Is this in the  */
	int lastformat;					/* Last output format */
	int lastinput;					/* Last input format */
	int ministate;					/* Miniature state, for dialtone mode */
	char dev[256];					/* Device name */
	struct ixj_pvt *next;			/* Next channel in list */
	struct ast_frame fr;			/* Frame */
	char offset[AST_FRIENDLY_OFFSET];
	char buf[IXJ_MAX_BUF];					/* Static buffer for reading frames */
	int obuflen;
	int dialtone;
	char context[AST_MAX_EXTENSION];
	char obuf[IXJ_MAX_BUF * 2];
	char ext[AST_MAX_EXTENSION];
} *iflist = NULL;

static int ixj_digit(struct ast_channel *ast, char digit)
{
	struct ixj_pvt *p;
	int outdigit;
	p = ast->pvt->pvt;
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
		outdigit = digit - '0' + 1;
		break;
	case '*':
		outdigit = 11;
		break;
	case '#':
		outdigit = 12;
		break;
	default:
		ast_log(LOG_WARNING, "Unknown digit '%c'\n", digit);
		return -1;
	}
	ioctl(p->fd, IXJCTL_PLAY_TONE, digit);
	return 0;
}

static int ixj_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct ixj_pvt *p;
	p = ast->pvt->pvt;
	if ((ast->state != AST_STATE_DOWN) && (ast->state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "ixj_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	ast_log(LOG_DEBUG, "Ringing %s on %s (%d)\n", dest, ast->name, ast->fd);
	ioctl(p->fd, IXJCTL_RING_START);
	ast->state = AST_STATE_RINGING;
	return 0;
}

static int ixj_hangup(struct ast_channel *ast)
{
	struct ixj_pvt *p;
	p = ast->pvt->pvt;
	ast_log(LOG_DEBUG, "ixj_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	/* XXX Is there anything we can do to really hang up except stop recording? */
	ast->state = AST_STATE_DOWN;
	if (ioctl(p->fd, IXJCTL_REC_STOP))
		ast_log(LOG_WARNING, "Failed to stop recording\n");
	if (ioctl(p->fd, IXJCTL_PLAY_STOP))
		ast_log(LOG_WARNING, "Failed to stop playing\n");
	if (ioctl(p->fd, IXJCTL_RING_STOP))
		ast_log(LOG_WARNING, "Failed to stop ringing\n");
	if (ioctl(p->fd, IXJCTL_CPT_STOP))
		ast_log(LOG_WARNING, "Failed to stop sounds\n");
	/* If they're off hook, give a busy signal */
	if (ioctl(p->fd, IXJCTL_HOOKSTATE))
		ioctl(p->fd, IXJCTL_BUSY);
	p->lastformat = -1;
	p->lastinput = -1;
	p->ministate = 0;
	p->obuflen = 0;
	p->dialtone = 0;
	memset(p->ext, 0, sizeof(p->ext));
	((struct ixj_pvt *)(ast->pvt->pvt))->owner = NULL;
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

static int ixj_setup(struct ast_channel *ast)
{
	struct ixj_pvt *p;
	p = ast->pvt->pvt;
	ioctl(p->fd, IXJCTL_CPT_STOP);
	/* Nothing to answering really, just start recording */
	if (ast->format & AST_FORMAT_G723_1) {
		/* Prefer g723 */
		ioctl(p->fd, IXJCTL_REC_STOP);
		if (p->lastinput != AST_FORMAT_G723_1) {
			p->lastinput = AST_FORMAT_G723_1;
			if (ioctl(p->fd, IXJCTL_REC_CODEC, G723_63)) {
				ast_log(LOG_WARNING, "Failed to set codec to g723.1\n");
				return -1;
			}
		}
	} else if (ast->format & AST_FORMAT_SLINEAR) {
		ioctl(p->fd, IXJCTL_REC_STOP);
		if (p->lastinput != AST_FORMAT_SLINEAR) {
			p->lastinput = AST_FORMAT_SLINEAR;
			if (ioctl(p->fd, IXJCTL_REC_CODEC, LINEAR16)) {
				ast_log(LOG_WARNING, "Failed to set codec to signed linear 16\n");
				return -1;
			}
		}
	} else {
		ast_log(LOG_WARNING, "Can't do format %d\n", ast->format);
		return -1;
	}
	if (ioctl(p->fd, IXJCTL_REC_START)) {
		ast_log(LOG_WARNING, "Failed to start recording\n");
		return -1;
	}
	return 0;
}

static int ixj_answer(struct ast_channel *ast)
{
	ixj_setup(ast);
	ast_log(LOG_DEBUG, "ixj_answer(%s)\n", ast->name);
	ast->rings = 0;
	ast->state = AST_STATE_UP;
	return 0;
}

static char ixj_2digit(char c)
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

static struct ast_frame  *ixj_read(struct ast_channel *ast)
{
	int res;
	IXJ_EXCEPTION ixje;
	struct ixj_pvt *p = ast->pvt->pvt;
	char digit;

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.timelen = 0;
	p->fr.data =  NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd=0;

	ixje.bytes = ioctl(p->fd, IXJCTL_EXCEPTION);
	if (ixje.bits.dtmf_ready)  {
		/* We've got a digit -- Just handle this nicely and easily */
		digit =  ioctl(p->fd, IXJCTL_GET_DTMF_ASCII);
		p->fr.subclass = digit;
		p->fr.frametype = AST_FRAME_DTMF;
		return &p->fr;
	}
	if (ixje.bits.hookstate) {
		res = ioctl(p->fd, IXJCTL_HOOKSTATE);
		/* See if we've gone on hook, if so, notify by returning NULL */
		if (!res)
			return NULL;
		else {
			if (ast->state == AST_STATE_RINGING) {
				/* They've picked up the phone */
				p->fr.frametype = AST_FRAME_CONTROL;
				p->fr.subclass = AST_CONTROL_ANSWER;
				ixj_setup(ast);
				ast->state = AST_STATE_UP;
				return &p->fr;
			}  else 
				ast_log(LOG_WARNING, "Got off hook in weird state\n");
		}
	}
#if 0
	if (ixje.bits.pstn_ring)
		ast_verbose("Unit is ringing\n");
	if (ixje.bits.caller_id) {
		ast_verbose("We have caller ID: %s\n");
	}
#endif
	/* Try to read some data... */
	CHECK_BLOCKING(ast);
	res = read(p->fd, p->buf, IXJ_MAX_BUF);
	ast->blocking = 0;
	if (res < 0) {
		ast_log(LOG_WARNING, "Error reading: %s\n", strerror(errno));
		return NULL;
	}
	p->fr.data = p->buf;
	p->fr.datalen = res;
	p->fr.frametype = AST_FRAME_VOICE;
	p->fr.subclass = p->lastinput;
	p->fr.offset = AST_FRIENDLY_OFFSET;
	return &p->fr;
}

static int ixj_write_buf(struct ixj_pvt *p, char *buf, int len, int frlen)
{
	int res;
	/* Store as much of the buffer as we can, then write fixed frames */
	int space = sizeof(p->obuf) - p->obuflen;
	/* Make sure we have enough buffer space to store the frame */
	if (space < len)
		len = space;
	memcpy(p->obuf + p->obuflen, buf, len);
	p->obuflen += len;
	while(p->obuflen > frlen) {
#if 0
		res = frlen;
		ast_log(LOG_DEBUG, "Wrote %d bytes\n", res);
#else
		res = write(p->fd, p->obuf, frlen);
#endif
		if (res != frlen) {
			if (res < 1) {
				ast_log(LOG_WARNING, "Write failed: %s\n", strerror(errno));
				return -1;
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

static int ixj_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct ixj_pvt *p = ast->pvt->pvt;
	int res;
	int maxfr=0;
	char *pos;
	int sofar;
	int expected;
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Don't know what to do with  frame type '%d'\n", frame->frametype);
		ast_frfree(frame);
		return -1;
	}
	if (!(frame->subclass & (AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		ast_frfree(frame);
		return -1;
	}
	if (frame->subclass == AST_FORMAT_G723_1) {
		if (p->lastformat != AST_FORMAT_G723_1) {
			ioctl(p->fd, IXJCTL_PLAY_STOP);
			if (ioctl(p->fd, IXJCTL_PLAY_CODEC, G723_63)) {
				ast_log(LOG_WARNING, "Unable to set G723.1 mode\n");
				return -1;
			}
			p->lastformat = AST_FORMAT_G723_1;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		if (frame->datalen > 24) {
			ast_log(LOG_WARNING, "Frame size too large for G.723.1 (%d bytes)\n", frame->datalen);
			return -1;
		}
		maxfr = 24;
	} else if (frame->subclass == AST_FORMAT_SLINEAR) {
		if (p->lastformat != AST_FORMAT_SLINEAR) {
			ioctl(p->fd, IXJCTL_PLAY_STOP);
			if (ioctl(p->fd, IXJCTL_PLAY_CODEC, LINEAR16)) {
				ast_log(LOG_WARNING, "Unable to set 16-bit linear mode\n");
				return -1;
			}
			p->lastformat = AST_FORMAT_SLINEAR;
			/* Reset output buffer */
			p->obuflen = 0;
		}
		maxfr = 480;
	}
	if (ioctl(p->fd, IXJCTL_PLAY_START)) {
		ast_log(LOG_WARNING, "Failed to start recording\n");
		return -1;
	}
	/* If we get here, we have a voice frame of Appropriate data */
	sofar = 0;
	pos = frame->data;
	while(sofar < frame->datalen) {
		/* Write in no more than maxfr sized frames */
		expected = frame->datalen - sofar;
		if (maxfr < expected)
			expected = maxfr;
		/* XXX Internet Phone Jack does not handle the 4-byte VAD frame properly! XXX */
		if (frame->datalen != 4) {
			res = ixj_write_buf(p, pos, expected, maxfr);
			if (res != expected) {
				if (res < 0) 
					ast_log(LOG_WARNING, "Write returned error (%s)\n", strerror(errno));
				else
					ast_log(LOG_WARNING, "Only wrote %d of %d bytes\n", res, frame->datalen);
				return -1;
			}
			sofar += res;
			pos += res;
		} else
			sofar += 4;
	}
	return 0;
}

static struct ast_channel *ixj_new(struct ixj_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc();
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "PhoneJack/%s", i->dev + 5);
		tmp->type = type;
		tmp->fd = i->fd;
		/* XXX Switching formats silently causes kernel panics XXX */
		tmp->format = AST_FORMAT_G723_1 /* | AST_FORMAT_SLINEAR */;
		tmp->state = state;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = ixj_digit;
		tmp->pvt->call = ixj_call;
		tmp->pvt->hangup = ixj_hangup;
		tmp->pvt->answer = ixj_answer;
		tmp->pvt->read = ixj_read;
		tmp->pvt->write = ixj_write;
		strncpy(tmp->context, i->context, sizeof(tmp->context));
		if (strlen(i->ext))
			strncpy(tmp->exten, i->ext, sizeof(tmp->exten));
		i->owner = tmp;
		pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (state == AST_STATE_RING)
				ioctl(tmp->fd, IXJCTL_RINGBACK);
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

static void ixj_mini_packet(struct ixj_pvt *i)
{
	int res;
	char buf[1024];
	/* Ignore stuff we read... */
	res = read(i->fd, buf, sizeof(buf));
	if (res < 1) {
		ast_log(LOG_WARNING, "Read returned %d\n", res);
		return;
	}
}

static void ixj_check_exception(struct ixj_pvt *i)
{
	int offhook=0;
	char digit[2] = {0 , 0};
	IXJ_EXCEPTION ixje;
	/* XXX Do something XXX */
#if 0
	ast_log(LOG_DEBUG, "Exception!\n");
#endif
	ixje.bytes = ioctl(i->fd, IXJCTL_EXCEPTION);
	if (ixje.bits.dtmf_ready)  {
		digit[0] = ioctl(i->fd, IXJCTL_GET_DTMF_ASCII);
		if (i->mode == MODE_DIALTONE) {
			ioctl(i->fd, IXJCTL_PLAY_STOP);
			ioctl(i->fd, IXJCTL_REC_STOP);
			ioctl(i->fd, IXJCTL_CPT_STOP);
			i->dialtone = 0;
			if (strlen(i->ext) < AST_MAX_EXTENSION - 1)
				strcat(i->ext, digit);
			if (ast_exists_extension(NULL, i->context, i->ext, 1)) {
				/* It's a valid extension in its context, get moving! */
				ixj_new(i, AST_STATE_UP);
				/* No need to restart monitor, we are the monitor */
				if (i->owner) {
					ixj_setup(i->owner);
				}
			} else if (ast_exists_extension(NULL, "default", i->ext, 1)) {
				/* Check the default, too... */
				/* XXX This should probably be justified better XXX */
				strncpy(i->context, "default", sizeof(i->context));
				ixj_new(i, AST_STATE_UP);
				if (i->owner) {
					ixj_setup(i->owner);
				}
			} else if ((strlen(i->ext) >= ast_pbx_longest_extension(i->context)) &&
					   (strlen(i->ext) >= ast_pbx_longest_extension("default"))) {
				/* It's not a valid extension, give a busy signal */
				ioctl(i->fd, IXJCTL_BUSY);
			}
#if 0
			ast_verbose("Extension is %s\n", i->ext);
#endif
		}
	}
	if (ixje.bits.hookstate) {
		offhook = ioctl(i->fd, IXJCTL_HOOKSTATE);
		if (offhook) {
			if (i->mode == MODE_IMMEDIATE) {
				ixj_new(i, AST_STATE_RING);
			} else if (i->mode == MODE_DIALTONE) {
#if 0
				/* XXX Bug in the Phone jack, you can't detect DTMF when playing a tone XXX */
				ioctl(i->fd, IXJCTL_DIALTONE);
#else
				/* Play the dialtone */
				i->dialtone++;
				ioctl(i->fd, IXJCTL_PLAY_STOP);
				ioctl(i->fd, IXJCTL_PLAY_CODEC, ULAW);
				ioctl(i->fd, IXJCTL_PLAY_START);
#endif
			}
		} else {
			memset(i->ext, 0, sizeof(i->ext));
			ioctl(i->fd, IXJCTL_CPT_STOP);
			ioctl(i->fd, IXJCTL_PLAY_STOP);
			ioctl(i->fd, IXJCTL_REC_STOP);
			i->dialtone = 0;
		}
	}
	if (ixje.bits.pstn_ring)
		ast_verbose("Unit is ringing\n");
	if (ixje.bits.caller_id)
		ast_verbose("We have caller ID\n");
	
	
}

static void *do_monitor(void *data)
{
	fd_set rfds, efds;
	int n, res;
	struct ixj_pvt *i;
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
		   ixj_pvt that does not have an associated owner channel */
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
		pthread_mutex_unlock(&iflock);
		
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		pthread_mutex_unlock(&monlock);
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
			res = select(n + 1, &rfds, NULL, &efds, &tv);
		} else {
			res = select(n + 1, &rfds, NULL, &efds, NULL);
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
		if (pthread_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}
		i = iflist;
		while(i) {
			if (FD_ISSET(i->fd, &rfds)) {
				if (i->owner) {
					ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d, %s)...\n", i->fd, i->dev);
					continue;
				}
				ixj_mini_packet(i);
			}
			if (FD_ISSET(i->fd, &efds)) {
				if (i->owner) {
					ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d, %s)...\n", i->fd, i->dev);
					continue;
				}
				ixj_check_exception(i);
			}
			i=i->next;
		}
		pthread_mutex_unlock(&iflock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor()
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
	if (monitor_thread != -1) {
		pthread_cancel(monitor_thread);
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

struct ixj_pvt *mkif(char *iface, int mode)
{
	/* Make a ixj_pvt structure for this interface */
	struct ixj_pvt *tmp;
	int flags;	
	
	tmp = malloc(sizeof(struct ixj_pvt));
	if (tmp) {
		tmp->fd = open(iface, O_RDWR);
		if (tmp->fd < 0) {
			ast_log(LOG_WARNING, "Unable to open '%s'\n", iface);
			free(tmp);
			return NULL;
		}
		ioctl(tmp->fd, IXJCTL_PLAY_STOP);
		ioctl(tmp->fd, IXJCTL_REC_STOP);
		ioctl(tmp->fd, IXJCTL_RING_STOP);
		ioctl(tmp->fd, IXJCTL_CPT_STOP);
		tmp->mode = mode;
		flags = fcntl(tmp->fd, F_GETFL);
		fcntl(tmp->fd, F_SETFL, flags | O_NONBLOCK);
		tmp->owner = NULL;
		tmp->lastformat = -1;
		tmp->lastinput = -1;
		tmp->ministate = 0;
		memset(tmp->ext, 0, sizeof(tmp->ext));
		strncpy(tmp->dev, iface, sizeof(tmp->dev));
		strncpy(tmp->context, context, sizeof(tmp->context));
		tmp->next = NULL;
		tmp->obuflen = 0;
		tmp->dialtone = 0;
	}
	return tmp;
}

static struct ast_channel *ixj_request(char *type, int format, void *data)
{
	int oldformat;
	struct ixj_pvt *p;
	struct ast_channel *tmp = NULL;
	char *name = data;
	
	oldformat = format;
	format &= (AST_FORMAT_G723_1 | AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	/* Search for an unowned channel */
	if (pthread_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p) {
		if (!strcmp(name, p->dev + 5)) {
			if (!p->owner) {
				tmp = ixj_new(p, AST_STATE_DOWN);
				break;
			}
		}
		p = p->next;
	}
	pthread_mutex_unlock(&iflock);
	restart_monitor();
	return tmp;
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ixj_pvt *tmp;
	int mode = MODE_IMMEDIATE;
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
	v = ast_variable_browse(cfg, "interfaces");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "device")) {
				tmp = mkif(v->value, mode);
				if (tmp) {
					tmp->next = iflist;
					iflist = tmp;
					
				} else {
					ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
					ast_destroy(cfg);
					pthread_mutex_unlock(&iflock);
					unload_module();
					return -1;
				}
		} else if (!strcasecmp(v->name, "mode")) {
			if (!strncasecmp(v->value, "di", 2)) 
				mode = MODE_DIALTONE;
			else if (!strncasecmp(v->value, "im", 2))
				mode = MODE_IMMEDIATE;
			else
				ast_log(LOG_WARNING, "Unknown mode: %s\n", v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context));
		}
		v = v->next;
	}
	pthread_mutex_unlock(&iflock);
	/* Make sure we can register our Adtranixj channel type */
	if (ast_channel_register(type, tdesc, AST_FORMAT_G723_1, ixj_request)) {
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
	struct ixj_pvt *p, *pl;
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
		if (monitor_thread > -1) {
			pthread_cancel(monitor_thread);
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
			/* Close the socket, assuming it's real */
			if (p->fd > -1)
				close(p->fd);
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

