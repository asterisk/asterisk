/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * A/Open ITU-56/2 Voice Modem Driver (Rockwell, IS-101, and others)
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
#include <stdlib.h>
#include <errno.h>
#include <asterisk/lock.h>
#include <asterisk/vmodem.h>
#include <asterisk/module.h>
#include <asterisk/frame.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>

#define STATE_COMMAND 	0
#define STATE_VOICE 	1

#define VRA "40"			/* Number of 100ms of non-ring after a ring cadence after which we consider the lien to be answered */
#define VRN "100"			/* Number of 100ms of non-ring with no cadence after which we assume an answer */

static char *breakcmd = "\0x10\0x03";

static char *desc = "A/Open (Rockwell Chipset) ITU-2 VoiceModem Driver";

static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char *aopen_idents[] = {
	/* Identify A/Open Modem */
	"V2.210-V90_2M_DLP",
	NULL
};

static int aopen_setdev(struct ast_modem_pvt *p, int dev)
{
	char cmd[80];
	if (ast_modem_send(p, "AT#VLS?", 0)) {
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
	snprintf(cmd, sizeof(cmd), "AT#VLS=%d", dev);
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

static int aopen_startrec(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, "AT#VRX", 0) ||
	     ast_modem_expect(p, "CONNECT", 5)) {
		ast_log(LOG_WARNING, "Unable to start recording\n");
		return -1;
	}
	p->ministate = STATE_VOICE;
	return 0;
}

static int aopen_break(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, "\r\n", 2)) {
		ast_log(LOG_WARNING, "Failed to send enter?\n");
		return -1;
	}
	if (ast_modem_send(p, breakcmd, 2)) {
		ast_log(LOG_WARNING, "Failed to break\n");
		return -1;
	}
	if (ast_modem_send(p, "\r\n", 2)) {
		ast_log(LOG_WARNING, "Failed to send enter?\n");
		return -1;
	}
	/* Read any outstanding junk */
	while(!ast_modem_read_response(p, 1));
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

static int aopen_init(struct ast_modem_pvt *p)
{
	if (option_debug)
		ast_log(LOG_DEBUG, "aopen_init()\n");
	if (aopen_break(p))
		return -1;
	/* Force into command mode */
	p->ministate = STATE_COMMAND;
	if (ast_modem_send(p, "AT#BDR=0", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to auto-baud\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#CLS=8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to voice mode\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#VBS=8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 8-bit mode\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#VSR=8000", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 8000 Hz sampling\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#VLS=0", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to telco interface\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#VRA=" VRA, 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 'ringback goes away' timer\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#VRN=" VRN, 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 'ringback never came timer'\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#VTD=3F,3F,3F", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to tone detection\n");
		return -1;
	}
	
	return 0;
}

static struct ast_frame *aopen_handle_escape(struct ast_modem_pvt *p, char esc)
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
	if (esc)
		ast_log(LOG_DEBUG, "Escaped character '%c'\n", esc);
	
	switch(esc) {
	case 'R': /* Pseudo ring */
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass = AST_CONTROL_RING;
		return &p->fr;
	case 'X': /* Pseudo connect */
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass = AST_CONTROL_RING;
		if (p->owner)
			ast_setstate(p->owner, AST_STATE_UP);
		if (aopen_startrec(p))
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
	case 'u': /* Underrun */
		ast_log(LOG_WARNING, "Data underrun\n");
		/* Fall Through */
	case CHAR_ETX: /* End Transmission */
	case 'd': /* Dialtone */
	case 'c': /* Calling Tone */
	case 'e': /* European version */
	case 'a': /* Answer Tone */
	case 'f': /* Bell Answer Tone */
	case 'T': /* Timing mark */
	case 't': /* Handset off hook */
	case 'h': /* Handset hungup */
	case 0: /* Pseudo signal */
		/* Ignore */
		return &p->fr;	
	default:
		ast_log(LOG_DEBUG, "Unknown Escaped character '%c' (%d)\n", esc, esc);
	}
	return &p->fr;
}

static struct ast_frame *aopen_read(struct ast_modem_pvt *p)
{
	char result[256];
	short *b;
	struct ast_frame *f=NULL;
	int res;
	int x;
	if (p->ministate == STATE_COMMAND) {
		/* Read the first two bytes, first, in case it's a control message */
		fread(result, 1, 2, p->f);
		if (result[0] == CHAR_DLE) {
			return aopen_handle_escape(p, result[1]);
			
		} else {
			if ((result[0] == '\n') || (result[0] == '\r'))
				return aopen_handle_escape(p, 0);
			/* Read the rest of the line */
			fgets(result + 2, sizeof(result) - 2, p->f);
			ast_modem_trim(result);
			if (!strcasecmp(result, "VCON")) {
				/* If we're in immediate mode, reply now */
				if (p->mode == MODEM_MODE_IMMEDIATE)
					return aopen_handle_escape(p, 'X');
			} else
			if (!strcasecmp(result, "BUSY")) {
				/* Same as a busy signal */
				return aopen_handle_escape(p, 'b');
			} else
			if (!strcasecmp(result, "RING")) {
				return aopen_handle_escape(p, 'R');
			} else
			if (!strcasecmp(result, "NO DIALTONE")) {
				/* There's no dialtone, so the line isn't working */
				ast_log(LOG_WARNING, "Device '%s' lacking dialtone\n", p->dev);
				return NULL;
			}
			ast_log(LOG_DEBUG, "Modem said '%s'\n", result);
			return aopen_handle_escape(p, 0);
		}
	} else {
		/* We have to be more efficient in voice mode */
		b = (short *)(p->obuf + p->obuflen);
		while (p->obuflen/2 < 240) {
			/* Read ahead the full amount */
			res = fread(result, 1, 240 - p->obuflen/2, p->f);
			if (res < 1) {
				/* If there's nothing there, just continue on */
				if (errno == EAGAIN)
					return aopen_handle_escape(p, 0);
				ast_log(LOG_WARNING, "Read failed: %s\n", strerror(errno));
			}
			for (x=0;x<res;x++) {
				/* Process all the bytes that we've read */
				if (result[x] == CHAR_DLE) {
					/* We assume there is no more than one signal frame among our
					   data.  */
					if (f) 
						ast_log(LOG_WARNING, "Warning: Dropped a signal frame\n");
					f = aopen_handle_escape(p, result[x+1]);
					/* If aopen_handle_escape says NULL, say it now, doesn't matter
					   what else is there, the connection is dead. */
					if (!f)
						return NULL;
				} else {
				/* Generate a 16-bit signed linear value from our 
				   unsigned 8-bit value */
					*(b++) = (((short)result[x]) - 127) * 0xff;
					p->obuflen += 2;
				}
			}
			if (f)
				break;
		}
		/* If we have a control frame, return it now */
		if (f)
			return f;
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
		if (option_debug)
			ast_log(LOG_DEBUG, "aopen_read(voice frame)\n");
		p->obuflen = 0;
		return &p->fr;
	}
	return NULL;
}

static int aopen_write(struct ast_modem_pvt *p, struct ast_frame *f)
{
	if (option_debug)
		ast_log(LOG_DEBUG, "aopen_write()\n");
	return 0;
}

static char *aopen_identify(struct ast_modem_pvt *p)
{
	char identity[256];
	char mfr[80];
	char mdl[80];
	char rev[80];
	ast_modem_send(p, "AT#MDL?", 0);
	ast_modem_read_response(p, 5);
	strncpy(mdl, p->response, sizeof(mdl)-1);
	ast_modem_trim(mdl);
	ast_modem_expect(p, "OK", 5);
	ast_modem_send(p, "AT#MFR?", 0);
	ast_modem_read_response(p, 5);
	strncpy(mfr, p->response, sizeof(mfr)-1);
	ast_modem_trim(mfr);
	ast_modem_expect(p, "OK", 5);
	ast_modem_send(p, "AT#REV?", 0);
	ast_modem_read_response(p, 5);
	strncpy(rev, p->response, sizeof(rev)-1);
	ast_modem_trim(rev);
	ast_modem_expect(p, "OK", 5);
	snprintf(identity, sizeof(identity), "%s Model %s Revision %s", mfr, mdl, rev);
	return strdup(identity);
}

static void aopen_incusecnt(void)
{
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
}

static void aopen_decusecnt(void)
{
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
}

static int aopen_answer(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, "ATA", 0) ||
	     ast_modem_expect(p, "VCON", 10)) {
		ast_log(LOG_WARNING, "Unable to answer: %s", p->response);
		return -1;
	}
	return 0;
}

static int aopen_dialdigit(struct ast_modem_pvt *p, char digit)
{
	char cmd[80];
	snprintf(cmd, sizeof(cmd), "AT#VTS=%c", digit);
	if (ast_modem_send(p, cmd, 0) ||
	     ast_modem_expect(p, "VCON", 10)) {
		ast_log(LOG_WARNING, "Unable to answer: %s", p->response);
		return -1;
	}
	return 0;
}

static int aopen_dial(struct ast_modem_pvt *p, char *stuff)
{
	char cmd[80];
	snprintf(cmd, sizeof(cmd), "ATD%c %s", p->dialtype,stuff);
	if (ast_modem_send(p, cmd, 0)) {
		ast_log(LOG_WARNING, "Unable to dial\n");
		return -1;
	}
	return 0;
}

static int aopen_hangup(struct ast_modem_pvt *p)
{
	if (aopen_break(p))
		return -1;
	/* Hangup by switching to data, then back to voice */
	if (ast_modem_send(p, "ATH", 0) ||
	     ast_modem_expect(p, "OK", 8)) {
		ast_log(LOG_WARNING, "Unable to set to data mode\n");
		return -1;
	}
	if (ast_modem_send(p, "AT#CLS=8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to voice mode\n");
		return -1;
	}
	return 0;
}

static struct ast_modem_driver aopen_driver =
{
	"AOpen",
	aopen_idents,
	AST_FORMAT_SLINEAR,
	0,		/* Not full duplex */
	aopen_incusecnt,	/* incusecnt */
	aopen_decusecnt,	/* decusecnt */
	aopen_identify,	/* identify */
	aopen_init,	/* init */
	aopen_setdev,	/* setdev */
	aopen_read,
	aopen_write,
	aopen_dial,	/* dial */
	aopen_answer,	/* answer */
	aopen_hangup,	/* hangup */
	aopen_startrec,	/* start record */
	NULL,	/* stop record */
	NULL,	/* start playback */
	NULL,	/* stop playback */
	NULL,	/* set silence supression */
	aopen_dialdigit,	/* dialdigit */
};



int usecount(void)
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

int load_module(void)
{
	return ast_register_modem_driver(&aopen_driver);
}

int unload_module(void)
{
	return ast_unregister_modem_driver(&aopen_driver);
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
