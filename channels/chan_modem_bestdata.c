/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * BestData 56SX-92 Voice Modem Driver (Conexant)
 * 
 * Copyright (C) 1999, Mark Spencer and 2001 Jim Dixon
 *
 * Mark Spencer <markster@linux-support.net>
 * Jim Dixon <jim@lambdatel.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <asterisk/lock.h>
#include <asterisk/vmodem.h>
#include <asterisk/module.h>
#include <asterisk/frame.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>

#define STATE_COMMAND 	0
#define STATE_VOICE 	1
#define	STATE_VOICEPLAY	2

#define VRA "40"			/* Number of 100ms of non-ring after a ring cadence after which we consider the lien to be answered */
#define VRN "25"			/* Number of 100ms of non-ring with no cadence after which we assume an answer */

#define	RINGT	7000

static char *breakcmd = "\020!";

static char *desc = "BestData (Conexant V.90 Chipset) VoiceModem Driver";

static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char *bestdata_idents[] = {
	/* Identify BestData Modem */
	"ACF3_V1.010-V90_P21_FSH",
	NULL
};

static int bestdata_startrec(struct ast_modem_pvt *p)
{
static int bestdata_break(struct ast_modem_pvt *p);

	if (p->ministate != STATE_COMMAND) bestdata_break(p);
	if (ast_modem_send(p, "AT+VRX", 0) ||
	     ast_modem_expect(p, "CONNECT", 5)) {
		ast_log(LOG_WARNING, "Unable to start recording\n");
		return -1;
	}
	p->ministate = STATE_VOICE;
	return 0;
}

static int bestdata_startplay(struct ast_modem_pvt *p)
{
static int bestdata_break(struct ast_modem_pvt *p);

	if (p->ministate != STATE_COMMAND) bestdata_break(p);
	if (ast_modem_send(p, "AT+VTX", 0) ||
	     ast_modem_expect(p, "CONNECT", 5)) {
		ast_log(LOG_WARNING, "Unable to start recording\n");
		return -1;
	}
	p->ministate = STATE_VOICEPLAY;
	return 0;
}

static int bestdata_break(struct ast_modem_pvt *p)
{
	if (ast_modem_send(p, breakcmd, 2)) {
		ast_log(LOG_WARNING, "Failed to break\n");
		return -1;
	}
	p->ministate = STATE_COMMAND;
	usleep(10000);
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

static int bestdata_init(struct ast_modem_pvt *p)
{
	if (option_debug)
		ast_log(LOG_DEBUG, "bestdata_init()\n");
	if (bestdata_break(p))
		return -1;
	/* Force into command mode */
	p->ministate = STATE_COMMAND;
	if (ast_modem_send(p, "AT+FCLASS=8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to voice mode\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VSM=1,8000,0,0", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 8000 Hz sampling\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VLS=0", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to telco interface\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VRA=" VRA, 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 'ringback goes away' timer\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VRN=" VRN, 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to 'ringback never came timer'\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VTD=63", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to tone detection\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+VCID=1", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to enable Caller*ID\n");
		return -1;
	}
	return 0;
}

static struct ast_frame *bestdata_handle_escape(struct ast_modem_pvt *p, char esc)
{
	char name[30]="",nmbr[30]="";
	time_t	now;

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
		time(&now);
		if (now > (p->lastring + (RINGT / 1000)))
		   { /* if stale, treat as new */
			p->gotclid = 0;
		   }
		if (p->gotclid)
		   {
			p->fr.frametype = AST_FRAME_CONTROL;
			p->fr.subclass = AST_CONTROL_RING;
		   }
		p->ringt = RINGT;
		time(&p->lastring);
		return &p->fr;
	case 'X': /* Caller-ID Spill */
		if (p->gotclid) return &p->fr;
		name[0] = nmbr[0] = 0;
		for(;;)
		   {
			char res[1000]="";

			if (ast_modem_read_response(p, 5)) break;
			strncpy(res, p->response, sizeof(res)-1);
			ast_modem_trim(res);
			if (!strncmp(res,"\020.",2)) break;
			if (!strncmp(res,"NAME",4)) strncpy(name,res + 7, sizeof(name) - 1);
			if (!strncmp(res,"NMBR",4)) strncpy(nmbr,res + 7, sizeof(nmbr) - 1);
		   }
		p->gotclid = 1;
		if ((!strcmp(name,"O")) || (!strcmp(name,"P"))) name[0] = 0;
		if ((!strcmp(nmbr,"O")) || (!strcmp(nmbr,"P"))) nmbr[0] = 0;
		if (name[0])
			strncpy(p->cid_name, name, sizeof(p->cid_name) - 1);
		if (nmbr[0])
			strncpy(p->cid_num, nmbr, sizeof(p->cid_num) - 1);
		if (p->owner) {
			p->owner->cid.cid_num = strdup(p->cid_num);
			p->owner->cid.cid_name = strdup(p->cid_name);
		}
		return &p->fr;
	case '@': /* response from "OK" in command mode */
		if (p->owner)
			ast_setstate(p->owner, AST_STATE_UP);
		if (bestdata_startrec(p)) return NULL;
		p->fr.frametype = AST_FRAME_CONTROL;
		p->fr.subclass = AST_CONTROL_RING;
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
	case '0':  /* All the DTMF characters */
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
	case 'A':
	case 'B':
	case 'C':
	case 'D':
		p->dtmfrx = esc;  /* save this for when its done */
		return &p->fr;	
	case '/': /* Start of DTMF tone shielding */
		p->dtmfrx = ' ';
		return &p->fr;	
	case '~': /* DTMF transition to off */
		if (p->dtmfrx > ' ')
		   {
			p->fr.frametype = AST_FRAME_DTMF;
			p->fr.subclass = p->dtmfrx;
		   }
		p->dtmfrx = 0;
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

static struct ast_frame *bestdata_read(struct ast_modem_pvt *p)
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
			return bestdata_handle_escape(p, result[1]);
		} else {
			if (p->ringt) /* if ring timeout specified */
			   {
				x = fileno(p->f);
				res = ast_waitfor_n_fd(&x, 1, &p->ringt, NULL);
				if (res < 0) {
					return NULL;
				}
			   }
			if ((result[0] == '\n') || (result[0] == '\r'))
				return bestdata_handle_escape(p, 0);
			/* Read the rest of the line */
			fgets(result + 2, sizeof(result) - 2, p->f);
			ast_modem_trim(result);
			if (!strcasecmp(result, "OK")) {
				/* If we're in immediate mode, reply now */
				if (p->mode == MODEM_MODE_IMMEDIATE)
					return bestdata_handle_escape(p, '@');
			} else
			if (!strcasecmp(result, "BUSY")) {
				/* Same as a busy signal */
				return bestdata_handle_escape(p, 'b');
			} else
			if (!strcasecmp(result, "RING")) {
				return bestdata_handle_escape(p, 'R');
			} else
			if (!strcasecmp(result, "NO DIALTONE")) {
				/* There's no dialtone, so the line isn't working */
				ast_log(LOG_WARNING, "Device '%s' lacking dialtone\n", p->dev);
				return NULL;
			}
			ast_log(LOG_DEBUG, "Modem said '%s'\n", result);
			return bestdata_handle_escape(p, 0);
		}
	} else {
		  /* if playing, start recording instead */
		if (p->ministate == STATE_VOICEPLAY)
		   {
			if (bestdata_startrec(p)) return NULL;
		   }
		/* We have to be more efficient in voice mode */
		b = (short *)(p->obuf + p->obuflen);
		while (p->obuflen/2 < 240) {
			/* Read ahead the full amount */
			res = fread(result, 1, 240 - p->obuflen/2, p->f);
			if (res < 1) {
				/* If there's nothing there, just continue on */
				if (errno == EAGAIN)
					return bestdata_handle_escape(p, 0);
				ast_log(LOG_WARNING, "Read failed: %s\n", strerror(errno));
			}
			for (x=0;x<res;x++) {
				/* Process all the bytes that we've read */
				if (result[x] == CHAR_DLE) {
					/* We assume there is no more than one signal frame among our
					   data.  */
					if (f) ast_log(LOG_WARNING, "Warning: Dropped a signal frame\n");
					  /* if not a DLE in the data */
					if (result[++x] != CHAR_DLE)
					   {
						/* If bestdata_handle_escape says NULL, say it now, doesn't matter
						   what else is there, the connection is dead. */
						f = bestdata_handle_escape(p, result[x]);
						if (p->dtmfrx) continue;
						return(f);
					   }
				}
				/* Generate a 16-bit signed linear value from our 
				   unsigned 8-bit value */
				*(b++) = (((short)result[x]) - 127) * 0xff;
				p->obuflen += 2;
			}
			if (f) break;
		}
		/* If we have a control frame, return it now */
		if (f) return f;
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
			ast_log(LOG_DEBUG, "bestdata_read(voice frame)\n");
		p->obuflen = 0;
		return &p->fr;
	}
	return NULL;
}

static int bestdata_write(struct ast_modem_pvt *p, struct ast_frame *f)
{
unsigned char	c,buf[32768]; /* I hope we dont have frames larger then 16K */
int	i,j;
short	*sp;
unsigned long u;
#define	DLE	16

	if (p->owner && (p->owner->_state == AST_STATE_UP) && 
		(p->ministate != STATE_VOICEPLAY) && bestdata_startplay(p)) return -1;
	sp = (short *) f->data;
	  /* stick DLE's in ahead of anything else */
	for(i = 0,j = 0; i < f->datalen / 2; i++)
	   {
		*sp *= 3;
		u = *sp++ + 32768;
		c = u >> 8;
		if (c == DLE) buf[j++] = DLE;
		buf[j++] = c;
	   }
	do i = fwrite(buf,1,j,p->f);
	while ((i == -1) && (errno == EWOULDBLOCK));
	if (i != j)
	   {
		ast_log(LOG_WARNING,"modem short write!!\n");
		return -1;
	   }
	fflush(p->f);
	if (option_debug)
		ast_log(LOG_DEBUG, "bestdata_write()\n");
	return 0;
}

static char *bestdata_identify(struct ast_modem_pvt *p)
{
	char identity[256];
	char mfr[80];
	char mdl[80];
	char rev[80];
	ast_modem_send(p, "AT+FMM", 0);
	ast_modem_read_response(p, 5);
	strncpy(mdl, p->response, sizeof(mdl)-1);
	ast_modem_trim(mdl);
	ast_modem_expect(p, "OK", 5);
	ast_modem_send(p, "AT+FMI", 0);
	ast_modem_read_response(p, 5);
	strncpy(mfr, p->response, sizeof(mfr)-1);
	ast_modem_trim(mfr);
	ast_modem_expect(p, "OK", 5);
	ast_modem_send(p, "AT+FMR", 0);
	ast_modem_read_response(p, 5);
	strncpy(rev, p->response, sizeof(rev)-1);
	ast_modem_trim(rev);
	ast_modem_expect(p, "OK", 5);
	snprintf(identity, sizeof(identity), "%s Model %s Revision %s", mfr, mdl, rev);
	return strdup(identity);
}

static void bestdata_incusecnt(void)
{
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
}

static void bestdata_decusecnt(void)
{
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
}

static int bestdata_answer(struct ast_modem_pvt *p)
{
	p->ringt = 0;
	p->lastring = 0;
	if (ast_modem_send(p, "AT+VLS=1", 0) ||
	     ast_modem_expect(p, "OK", 10)) {
		ast_log(LOG_WARNING, "Unable to answer: %s", p->response);
		return -1;
	}
	return 0;
}

static int bestdata_dialdigit(struct ast_modem_pvt *p, char digit)
{
	char cmd[80];

	if (p->ministate != STATE_COMMAND) bestdata_break(p);
	snprintf(cmd, sizeof(cmd), "AT+VTS=%c", digit);
	if (ast_modem_send(p, cmd, 0) ||
	     ast_modem_expect(p, "OK", 10)) {
		ast_log(LOG_WARNING, "Unable to answer: %s", p->response);
		return -1;
	}
	return 0;
}

static int bestdata_dial(struct ast_modem_pvt *p, char *stuff)
{
	char cmd[800] = "",a[20]="";
	int i,j;

	if (p->ministate != STATE_COMMAND)
	   {
		bestdata_break(p);
		strncpy(cmd, "AT+VTS=", sizeof(cmd) - 1);
		j = strlen(cmd);
		for(i = 0; stuff[i]; i++)
		   {
			switch(stuff[i])
			   {
			    case '!' :
				a[0] = stuff[i];
				a[1] = 0;
				break;
			    case ',':
				strncpy(a, "[,,100]", sizeof(a) - 1);
				break;
			    default:
				snprintf(a, sizeof(a), "{%c,7}", stuff[i]);
			   }
			if (stuff[i + 1]) strncat(a, ",", sizeof(a) - strlen(a) - 1);
			strncpy(cmd + j, a, sizeof(cmd) - j - 1);
			j += strlen(a);
		   }
 	   }
	else
	   {
		snprintf(cmd, sizeof(cmd), "ATD%c %s", p->dialtype,stuff);
	   }
	if (ast_modem_send(p, cmd, 0)) {
		ast_log(LOG_WARNING, "Unable to dial\n");
		return -1;
	}
	return 0;
}

static int bestdata_hangup(struct ast_modem_pvt *p)
{
	if (bestdata_break(p))
		return -1;
	/* Hangup by switching to data, then back to voice */
	if (ast_modem_send(p, "ATH", 0) ||
	     ast_modem_expect(p, "OK", 8)) {
		ast_log(LOG_WARNING, "Unable to set to data mode\n");
		return -1;
	}
	if (ast_modem_send(p, "AT+FCLASS=8", 0) ||
	     ast_modem_expect(p, "OK", 5)) {
		ast_log(LOG_WARNING, "Unable to set to voice mode\n");
		return -1;
	}
	p->gotclid = 0;
	p->ringt = 0;
	p->lastring = 0;
	p->dtmfrx = 0;
	return 0;
}

static struct ast_modem_driver bestdata_driver =
{
	"BestData",
	bestdata_idents,
	AST_FORMAT_SLINEAR,
	0,		/* Not full duplex */
	bestdata_incusecnt,	/* incusecnt */
	bestdata_decusecnt,	/* decusecnt */
	bestdata_identify,	/* identify */
	bestdata_init,	/* init */
	NULL,	/* setdev */
	bestdata_read,
	bestdata_write,
	bestdata_dial,	/* dial */
	bestdata_answer,	/* answer */
	bestdata_hangup,	/* hangup */
	bestdata_startrec,	/* start record */
	NULL,	/* stop record */
	bestdata_startplay,	/* start playback */
	NULL,	/* stop playback */
	NULL,	/* set silence supression */
	bestdata_dialdigit,	/* dialdigit */
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
	return ast_register_modem_driver(&bestdata_driver);
}

int unload_module(void)
{
	return ast_unregister_modem_driver(&bestdata_driver);
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
