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
 * ISDN4Linux TTY Driver
 * 
 */

#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/vmodem.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/dsp.h"
#include "asterisk/callerid.h"
#include "asterisk/ulaw.h"
#include "asterisk/pbx.h"

#define STATE_COMMAND 	0
#define STATE_VOICE 	1

static char *breakcmd = "\0x10\0x14\0x10\0x3";

static char *desc = "ISDN4Linux Emulated Modem Driver";

static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char *i4l_idents[] = {
	/* Identify ISDN4Linux Driver */
	"Linux ISDN",
	NULL
};

static int i4l_setdev(struct ast_modem_pvt *p, int dev)
{
	char cmd[80];
	if ((dev != MODEM_DEV_TELCO) && (dev != MODEM_DEV_TELCO_SPK)) {
		ast_log(LOG_WARNING, "ISDN4Linux only supports telco device, not %d.\n", dev);
		return -1;
	} else	/* Convert DEV to our understanding of it */
		dev = 2;
	if (ast_modem_send(p, "AT+VLS?", 0)) {
		ast_log(LOG_WARNING, "Unable to select current mode %d\n", dev);
		return -1;
	}
	if (ast_modem_read_response(p, 5)) {
		ast_log(LOG_WARNING, "Unable to select device %d\n", dev);
		return -1;
	}
	ast_modem_trim(p->response);
	strncpy(cmd, p->response, sizeof(cmd)-1);
	if (ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Modem did not respond properly\n");
		return -1;
	}
	if (dev == atoi(cmd)) {
		/* We're already in the right mode, don't bother changing for fear of
		   hanging up */
		return 0;
	}
	snprintf(cmd, sizeof(cmd), "AT+VLS=%d", dev);
	if (ast_modem_send(p, cmd, 0))  {
		ast_log(LOG_WARNING, "Unable to select device %d\n", dev);
		return -1;
	}
	if (ast_modem_read_response(p, 5)) {
		ast_log(LOG_WARNING, "Unable to select device %d\n", dev);
		return -1;
	}
	ast_modem_trim(p->response);
	if (strcasecmp(p->response, "VCON") && strcasecmp(p->response, "OK")) {
		ast_log(LOG_WARNING, "Unexpected reply: %s\n", p->response);
		return -1;
	}
	return 0;
}

static int i4l_startrec(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, "AT+VRX+VTX", 0) ||
	     ast_modem_expect(p, "CONNECT", 5)) {
		ast_log(LOG_WARNING, "Unable to start recording\n");
		return -1;
	}
	p->ministate = STATE_VOICE;
	
	/*  let ast dsp detect dtmf */
	if (p->dtmfmode & MODEM_DTMF_AST) {
		if (p->dsp) {
			ast_log(LOG_DEBUG, "Already have a dsp on %s?\n", p->dev);
		} else {
			p->dsp = ast_dsp_new();
			if (p->dsp) {
				ast_log(LOG_DEBUG, "Detecting DTMF inband with sw DSP on %s\n",p->dev);
				ast_dsp_set_features(p->dsp, DSP_FEATURE_DTMF_DETECT|DSP_FEATURE_FAX_DETECT);
				ast_dsp_digitmode(p->dsp, DSP_DIGITMODE_DTMF | 0);
			}
		}
	}

	return 0;
}

static int i4l_break(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, breakcmd, 2)) {
		ast_log(LOG_WARNING, "Failed to break\n");
		return -1;
	}
	if (ast_modem_send(p, "\r\n", 2)) {
		ast_log(LOG_WARNING, "Failed to send enter?\n");
		return -1;
	}
#if 0
	/* Read any outstanding junk */
	while(!ast_modem_read_response(p, 1));
#endif
	if (ast_modem_send(p, "AT", 0)) {
		/* Modem might be stuck in some weird mode, try to get it out */
		ast_modem_send(p, "+++", 3);
		if (ast_modem_expect(p, "OK", 10)) {
			ast_log(LOG_WARNING, "Modem is not responding\n");
			return -1;
		}
		if (ast_modem_send(p, "AT", 0)) {
			ast_log(LOG_WARNING, "Modem is not responding\n");
			return -1;
		}
	}
	if (ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Modem did not respond properly\n");
		return -1;
	}
	return 0;
}

static int i4l_init(struct ast_modem_pvt *p)
{
	char cmd[256];
	if (option_debug)
		ast_log(LOG_DEBUG, "i4l_init()\n");
	if (i4l_break(p))
		return -1;
	/* Force into command mode */
	p->ministate = STATE_COMMAND;
	if (ast_modem_send(p, "AT+FCLASS=8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to voice mode\n");
		return -1;
	}
	if (strlen(p->msn)) {
		snprintf(cmd, sizeof(cmd), "AT&E%s", p->msn);
		if (ast_modem_send(p, cmd, 0) ||
		    ast_modem_expect(p, "OK", 5)) {
			ast_log(LOG_WARNING, "Unable to set MSN to %s\n", p->msn);
			return -1;
		}
	}
	if (strlen(p->incomingmsn)) {
		char *q;
		snprintf(cmd, sizeof(cmd), "AT&L%s", p->incomingmsn);
		/* translate , into ; since that is the seperator I4L uses, but can't be directly */
		/* put in the config file because it will interpret the rest of the line as comment. */
		q = cmd+4;
		while (*q) {
			if (*q == ',') *q = ';';
			++q;
		}
		if (ast_modem_send(p, cmd, 0) ||
		    ast_modem_expect(p, "OK", 5)) {
			ast_log(LOG_WARNING, "Unable to set Listen to %s\n", p->msn);
			return -1;
		}
	}
	if (ast_modem_send(p, "AT&D2", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to DTR disconnect mode\n");
		return -1;
	}
	if (ast_modem_send(p, "ATS18=1", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to audio only mode\n");
		return -1;
	}
	if (ast_modem_send(p, "ATS13.6=1", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to RUNG indication\n");
		return -1;
	}
	if (ast_modem_send(p, "ATS14=4", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to transparent mode\n");
		return -1;
	}
	if (ast_modem_send(p, "ATS23=9", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to transparent/ringing mode\n");
		return -1;
	}

	if (ast_modem_send(p, "AT+VSM=6", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to muLAW mode\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VLS=2", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to phone line interface\n");
		return -1;
	}
	p->escape = 0;
	return 0;
}

static struct ast_frame *i4l_handle_escape(struct ast_modem_pvt *p, char esc)
{
	/* Handle escaped characters -- but sometimes we call it directly as 
	   a quick way to cause known responses */
	p->fr.frametype = AST_FRAME_NULL;
	p->fr.subclass = 0;
	p->fr.data = NULL;
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.offset = 0;
	p->fr.mallocd = 0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;
	if (esc && option_debug)
		ast_log(LOG_DEBUG, "Escaped character '%c'\n", esc);
	
	switch(esc) {
	case 'R': /* Pseudo ring */
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass = AST_CONTROL_RING;
		return &p->fr;
	case 'I': /* Pseudo ringing */
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass =  AST_CONTROL_RINGING;
		return &p->fr;
	case 'X': /* Pseudo connect */
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass = AST_CONTROL_ANSWER;
		if (p->owner)
			ast_setstate(p->owner, AST_STATE_UP);
		if (i4l_startrec(p))
			return 	NULL;
		return &p->fr;
	case 'b': /* Busy signal */
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass = AST_CONTROL_BUSY;
		return &p->fr;
	case 'o': /* Overrun */
		ast_log(LOG_WARNING, "Overflow on modem, flushing buffers\n");
		if (ast_modem_send(p, "\0x10E", 2)) 
			ast_log(LOG_WARNING, "Unable to flush buffers\n");
		return &p->fr;	
	case CHAR_ETX: /* End Transmission */
		return NULL;
	case 'u': /* Underrun */
		ast_log(LOG_WARNING, "Data underrun\n");
		/* Fall Through */
	case 'd': /* Dialtone */
	case 'c': /* Calling Tone */
	case 'e': /* European version */
	case 'a': /* Answer Tone */
	case 'f': /* Bell Answer Tone */
	case 'T': /* Timing mark */
	case 't': /* Handset off hook */
	case 'h': /* Handset hungup */
		/* Ignore */
		if (option_debug)
			ast_log(LOG_DEBUG, "Ignoring Escaped character '%c' (%d)\n", esc, esc);
		return &p->fr;
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
	case '*':
	case '#':
		ast_log(LOG_DEBUG, "Detected outband DTMF digit: '%c' (%d)\n", esc, esc);
		p->fr.frametype=AST_FRAME_DTMF;
		p->fr.subclass=esc;
		return &p->fr;
	case 0: /* Pseudo signal */
		return &p->fr;
	default:
		ast_log(LOG_DEBUG, "Unknown Escaped character '%c' (%d)\n", esc, esc);
	}
	return &p->fr;
}

static struct ast_frame *i4l_read(struct ast_modem_pvt *p)
{
	char result[256];
	short *b;
	struct ast_frame *f=NULL;
	int res;
	int x;
	if (p->ministate == STATE_COMMAND) {
		/* Read the first two bytes, first, in case it's a control message */
		res = read(p->fd, result, 2);
		if (res < 2) {
			/* short read, means there was a hangup? */
			/* (or is this also possible without hangup?) */
			/* Anyway, reading from unitialized buffers is a bad idea anytime. */
			if (errno == EAGAIN)
				return i4l_handle_escape(p, 0);
			return NULL;
		}
		if (result[0] == CHAR_DLE) {
			return i4l_handle_escape(p, result[1]);
			
		} else {
			if ((result[0] == '\n') || (result[0] == '\r'))
				return i4l_handle_escape(p, 0);
			/* Read the rest of the line */
			fgets(result + 2, sizeof(result) - 2, p->f);
			ast_modem_trim(result);
			if (!strcasecmp(result, "VCON")) {
				/* If we're in immediate mode, reply now */
/*				if (p->mode == MODEM_MODE_IMMEDIATE) */
					return i4l_handle_escape(p, 'X');
			} else
			if (!strcasecmp(result, "BUSY")) {
				/* Same as a busy signal */
				return i4l_handle_escape(p, 'b');
			} else
			if (!strncasecmp(result, "CALLER NUMBER: ", 15 )) {
				strncpy(p->cid_num, result + 15, sizeof(p->cid_num)-1);
				return i4l_handle_escape(p, 0);
			} else
			if (!strcasecmp(result, "RINGING")) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "%s is ringing...\n", p->dev);
				return i4l_handle_escape(p, 'I');
			} else
			if (!strncasecmp(result, "RUNG", 4)) {
				/* PM2002: the line was hung up before we picked it up, bye bye */
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "%s was hung up on before we answered\n", p->dev);
				return NULL;
			} else
			if (!strncasecmp(result, "RING", 4)) {
				if (result[4]=='/') 
					strncpy(p->dnid, result + 5, sizeof(p->dnid)-1);
				return i4l_handle_escape(p, 'R');
			} else
			if (!strcasecmp(result, "NO CARRIER")) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "%s hung up on\n", p->dev);
				return NULL;
			} else
			if (!strcasecmp(result, "NO DIALTONE")) {
				/* There's no dialtone, so the line isn't working */
				ast_log(LOG_WARNING, "Device '%s' lacking dialtone\n", p->dev);
				return NULL;
			}
			if (option_debug)
				ast_log(LOG_DEBUG, "Modem said '%s'\n", result);
			return i4l_handle_escape(p, 0);
		}
	} else {
		/* We have to be more efficient in voice mode */
		b = (short *)(p->obuf + p->obuflen);
		while (p->obuflen/2 < 240) {
			/* Read ahead the full amount */
			res = read(p->fd, result, 240 - p->obuflen/2);
			if (res < 1) {
				/* If there's nothing there, just continue on */
				if (errno == EAGAIN)
					return i4l_handle_escape(p, 0);
				ast_log(LOG_WARNING, "Read failed: %s\n", strerror(errno));
				return NULL;
			}
			
			for (x=0;x<res;x++) {
				/* Process all the bytes that we've read */
				switch(result[x]) {
				case CHAR_DLE:
#if 0
					ast_log(LOG_DEBUG, "Ooh, an escape at %d...\n", x);
#endif
					if (!p->escape) {
						/* Note that next value is
						   an escape, and continue. */
						p->escape++;
						break;
					} else {
						/* Send as is -- fallthrough */
						p->escape = 0;
					}
				default:
					if (p->escape) {
						ast_log(LOG_DEBUG, "Value of escape is %c (%d)...\n", result[x] < 32 ? '^' : result[x], result[x]);
						p->escape = 0;
						if (f) 
							ast_log(LOG_WARNING, "Warning: Dropped a signal frame\n");
						f = i4l_handle_escape(p, result[x]);
						/* If i4l_handle_escape says NULL, say it now, doesn't matter
						what else is there, the connection is dead. */
						if (!f)
							return NULL;
					} else {
						*(b++) = AST_MULAW((int)result[x]);
						p->obuflen += 2;
					}
				}
			}
			if (f)
				break;
		}
		if (f) {
			if( ! (!(p->dtmfmode & MODEM_DTMF_I4L) && f->frametype == AST_FRAME_DTMF))
			return f;
		}

		/* If we get here, we have a complete voice frame */
		p->fr.frametype = AST_FRAME_VOICE;
		p->fr.subclass = AST_FORMAT_SLINEAR;
		p->fr.samples = 240;
		p->fr.data = p->obuf;
		p->fr.datalen = p->obuflen;
		p->fr.mallocd = 0;
		p->fr.delivery.tv_sec = 0;
		p->fr.delivery.tv_usec = 0;
		p->fr.offset = AST_FRIENDLY_OFFSET;
		p->fr.src = __FUNCTION__;
		p->obuflen = 0;

		/* process with dsp */
		if (p->dsp) {
			f = ast_dsp_process(p->owner, p->dsp, &p->fr);
			if (f && (f->frametype == AST_FRAME_DTMF)) {
				ast_log(LOG_DEBUG, "Detected inband DTMF digit: %c on %s\n", f->subclass, p->dev);
				if (f->subclass == 'f') {
					/* Fax tone -- Handle and return NULL */
					struct ast_channel *ast = p->owner;
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
					p->fr.frametype = AST_FRAME_NULL;
					p->fr.subclass = 0;
					f = &p->fr;
				}
				return f;
			}
		}
		
		return &p->fr;
	}
	return NULL;
}

static int i4l_write(struct ast_modem_pvt *p, struct ast_frame *f)
{
#define MAX_WRITE_SIZE 2048
	unsigned char result[MAX_WRITE_SIZE << 1];
	unsigned char b;
	int bpos=0, x;
	int res;
	if (f->datalen > MAX_WRITE_SIZE) {
		ast_log(LOG_WARNING, "Discarding too big frame of size %d\n", f->datalen);
		return -1;
	}
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Don't know how to handle %d type frames\n", f->frametype);
		return -1;
	}
	if (f->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Don't know how to handle anything but signed linear frames\n");
		return -1;
	}
	for (x=0;x<f->datalen/2;x++) {
		b = AST_LIN2MU(((short *)f->data)[x]);
		result[bpos++] = b;
		if (b == CHAR_DLE)
			result[bpos++]=b;
	}
#if 0
	res = fwrite(result, bpos, 1, p->f);
	res *= bpos;
#else
	res = write(p->fd, result, bpos);
#endif
	if (res < 1) {
		if (errno != EAGAIN) {
			ast_log(LOG_WARNING, "Failed to write buffer\n");
			return -1;
		}
	}
#if 0
	printf("Result of write is %d\n", res);
#endif
	return 0;
}

static char *i4l_identify(struct ast_modem_pvt *p)
{
	return strdup("Linux ISDN");
}

static void i4l_incusecnt(void)
{
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
}

static void i4l_decusecnt(void)
{
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
}

static int i4l_answer(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, "ATA\r", 4) ||
	     ast_modem_expect(p, "VCON", 10)) {
		ast_log(LOG_WARNING, "Unable to answer: %s", p->response);
		return -1;
	}
#if 1
	if (ast_modem_send(p, "AT+VDD=0,8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to phone line interface\n");
		return -1;
	}
#endif
	if (ast_modem_send(p, "AT+VTX+VRX", 0) ||
	     ast_modem_expect(p, "CONNECT", 10)) {
		ast_log(LOG_WARNING, "Unable to answer: %s", p->response);
		return -1;
	}
	p->ministate = STATE_VOICE;

	/*  let ast dsp detect dtmf */
	if (p->dtmfmode & MODEM_DTMF_AST) {
		if (p->dsp) {
			ast_log(LOG_DEBUG, "Already have a dsp on %s?\n", p->dev);
		} else {
			p->dsp = ast_dsp_new();
			if (p->dsp) {
				ast_log(LOG_DEBUG, "Detecting DTMF inband with sw DSP on %s\n",p->dev);
				ast_dsp_set_features(p->dsp, DSP_FEATURE_DTMF_DETECT|DSP_FEATURE_FAX_DETECT);
				ast_dsp_digitmode(p->dsp, DSP_DIGITMODE_DTMF | 0);
			}
		}
	}

	return 0;
}

static int i4l_dialdigit(struct ast_modem_pvt *p, char digit)
{
	char c[2];
	if (p->ministate == STATE_VOICE) {
		if (p->dtmfmodegen & MODEM_DTMF_I4L) {
		c[0] = CHAR_DLE;
		c[1] = digit;
		write(p->fd, c, 2);
			ast_log(LOG_DEBUG, "Send ISDN out-of-band DTMF %c\n",digit);
		}
		if(p->dtmfmodegen & MODEM_DTMF_AST) {
			ast_log(LOG_DEBUG, "Generating inband DTMF\n");
			return -1;
		}
	} else
		ast_log(LOG_DEBUG, "Asked to send digit but call not up on %s\n", p->dev);
	return 0;
}

static int i4l_dial(struct ast_modem_pvt *p, char *stuff)
{
	char cmd[80];
	char tmpmsn[255];
	struct ast_channel *c = p->owner;

	/* Find callerid number first, to set the correct A number */
	if (c && c->cid.cid_num && !(c->cid.cid_pres & 0x20)) {
	    snprintf(tmpmsn, sizeof(tmpmsn), ",%s,", c->cid.cid_num);
	    if(strlen(p->outgoingmsn) && strstr(p->outgoingmsn,tmpmsn) != NULL) {
	      /* Tell ISDN4Linux to use this as A number */
	      snprintf(cmd, sizeof(cmd), "AT&E%s\n", c->cid.cid_num);
	      if (ast_modem_send(p, cmd, strlen(cmd))) {
		ast_log(LOG_WARNING, "Unable to set A number to %s\n", c->cid.cid_num);
	      }

	    } else {
	      ast_log(LOG_WARNING, "Outgoing MSN %s not allowed (see outgoingmsn=%s in modem.conf)\n",c->cid.cid_num,p->outgoingmsn);
	    }
	}

	snprintf(cmd, sizeof(cmd), "ATD%c %s\n", p->dialtype,stuff);
	if (ast_modem_send(p, cmd, strlen(cmd))) {
		ast_log(LOG_WARNING, "Unable to dial\n");
		return -1;
	}
	return 0;
}

static int i4l_hangup(struct ast_modem_pvt *p)
{
	char dummy[50];
	int dtr = TIOCM_DTR;

	/* free the memory used by the DSP */
	if (p->dsp) {
		ast_dsp_free(p->dsp);
		p->dsp = NULL;
	}

	/* down DTR to hangup modem */
	ioctl(p->fd, TIOCMBIC, &dtr);
	/* Read anything outstanding */
	while(read(p->fd, dummy, sizeof(dummy)) > 0);

	/* rise DTR to re-enable line */
	ioctl(p->fd, TIOCMBIS, &dtr);
	
	/* Read anything outstanding */
	while(read(p->fd, dummy, sizeof(dummy)) > 0);

	/* basically we're done, just to be sure */
	write(p->fd, "\n\n", 2);
	read(p->fd, dummy, sizeof(dummy));
	if (ast_modem_send(p, "ATH", 0)) {
		ast_log(LOG_WARNING, "Unable to hang up\n");
		return -1;
	}
	if (ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Final 'OK' not received\n");
		return -1;
	}

	return 0;
}

static struct ast_modem_driver i4l_driver =
{
	"i4l",
	i4l_idents,
	AST_FORMAT_SLINEAR,
	0,		/* Not full duplex */
	i4l_incusecnt,	/* incusecnt */
	i4l_decusecnt,	/* decusecnt */
	i4l_identify,	/* identify */
	i4l_init,	/* init */
	i4l_setdev,	/* setdev */
	i4l_read,
	i4l_write,
	i4l_dial,	/* dial */
	i4l_answer,	/* answer */
	i4l_hangup,	/* hangup */
	i4l_startrec,	/* start record */
	NULL,	/* stop record */
	NULL,	/* start playback */
	NULL,	/* stop playback */
	NULL,	/* set silence supression */
	i4l_dialdigit,	/* dialdigit */
};



int usecount(void)
{
	return usecnt;
}

int load_module(void)
{
	return ast_register_modem_driver(&i4l_driver);
}

int unload_module(void)
{
	return ast_unregister_modem_driver(&i4l_driver);
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
