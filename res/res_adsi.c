/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ADSI support 
 * 
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <asterisk/ulaw.h>
#include <asterisk/alaw.h>
#include <asterisk/callerid.h>
#include <asterisk/logger.h>
#include <asterisk/fskmodem.h>
#include <asterisk/channel.h>
#include <asterisk/adsi.h>
#include <asterisk/module.h>
#include <asterisk/config.h>
#include <asterisk/file.h>

#define DEFAULT_ADSI_MAX_RETRIES 3

#define ADSI_MAX_INTRO 20
#define ADSI_MAX_SPEED_DIAL 6

#define ADSI_FLAG_DATAMODE	(1 << 8)

static int maxretries = DEFAULT_ADSI_MAX_RETRIES;

/* Asterisk ADSI button definitions */
#define ADSI_SPEED_DIAL		10	/* 10-15 are reserved for speed dial */

static char intro[ADSI_MAX_INTRO][20];
static int aligns[ADSI_MAX_INTRO];

static char speeddial[ADSI_MAX_SPEED_DIAL][3][20];

static int alignment = 0;

static int adsi_generate(unsigned char *buf, int msgtype, char *msg, int msglen, int msgnum, int last, int codec)
{
	int sum;
	int x;	
	int bytes=0;
	/* Initial carrier (imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;

	if (msglen > 255)
		msglen = 255;

	/* If first message, Send 150ms of MARK's */
	if (msgnum == 1) {
		for (x=0;x<150;x++)	/* was 150 */
			PUT_CLID_MARKMS;
	}
	/* Put message type */
	PUT_CLID(msgtype);
	sum = msgtype;

	/* Put message length (plus one  for the message number) */
	PUT_CLID(msglen + 1);
	sum += msglen + 1;

	/* Put message number */
	PUT_CLID(msgnum);
	sum += msgnum;

	/* Put actual message */
	for (x=0;x<msglen;x++) {
		PUT_CLID(msg[x]);
		sum += msg[x];
	}

	/* Put 2's compliment of sum */
	PUT_CLID(256-(sum & 0xff));

#if 0
	if (last) {
		/* Put trailing marks */
		for (x=0;x<50;x++)
			PUT_CLID_MARKMS;
	}
#endif
	return bytes;

}

static int adsi_careful_send(struct ast_channel *chan, unsigned char *buf, int len, int *remainder)
{
	/* Sends carefully on a full duplex channel by using reading for
	   timing */
	struct ast_frame *inf, outf;
	int amt;

	/* Zero out our outgoing frame */
	memset(&outf, 0, sizeof(outf));

	if (remainder && *remainder) {
		amt = len;

		/* Send remainder if provided */
		if (amt > *remainder)
			amt = *remainder;
		else
			*remainder = *remainder - amt;
		outf.frametype = AST_FRAME_VOICE;
		outf.subclass = AST_FORMAT_ULAW;
		outf.data = buf;
		outf.datalen = amt;
		outf.samples = amt;
		if (ast_write(chan, &outf)) {
			ast_log(LOG_WARNING, "Failed to carefully write frame\n");
			return -1;
		}
		/* Update pointers and lengths */
		buf += amt;
		len -= amt;
	}

	while(len) {
		amt = len;
		/* If we don't get anything at all back in a second, forget
		   about it */
		if (ast_waitfor(chan, 1000) < 1)
			return -1;
		inf = ast_read(chan);
		/* Detect hangup */
		if (!inf)
			return -1;
		if (inf->frametype == AST_FRAME_VOICE) {
			/* Read a voice frame */
			if (inf->subclass != AST_FORMAT_ULAW) {
				ast_log(LOG_WARNING, "Channel not in ulaw?\n");
				return -1;
			}
			/* Send no more than they sent us */
			if (amt > inf->datalen)
				amt = inf->datalen;
			else if (remainder)
				*remainder = inf->datalen - amt;
			outf.frametype = AST_FRAME_VOICE;
			outf.subclass = AST_FORMAT_ULAW;
			outf.data = buf;
			outf.datalen = amt;
			outf.samples = amt;
			if (ast_write(chan, &outf)) {
				ast_log(LOG_WARNING, "Failed to carefully write frame\n");
				return -1;
			}
			/* Update pointers and lengths */
			buf += amt;
			len -= amt;
		}
		ast_frfree(inf);
	}
	return 0;
}

static int __adsi_transmit_messages(struct ast_channel *chan, unsigned char **msg, int *msglen, int *msgtype)
{
	/* msglen must be no more than 256 bits, each */
	unsigned char buf[24000 * 5];
	int pos = 0, res;
	int x;
	int start=0;
	int retries = 0;

	char ack[3];

	/* Wait up to 500 ms for initial ACK */
	int waittime;
	struct ast_frame *f;
	int rem = 0;
	int def;

	if (chan->adsicpe == AST_ADSI_UNAVAILABLE) {
		/* Don't bother if we know they don't support ADSI */
		errno = ENOSYS;
		return -1;
	}

	while(retries < maxretries) {
		if (!(chan->adsicpe & ADSI_FLAG_DATAMODE)) {
			/* Generate CAS (no SAS) */
			ast_gen_cas(buf, 0, 680, AST_FORMAT_ULAW);
		
			/* Send CAS */
			if (adsi_careful_send(chan, buf, 680, NULL)) {
				ast_log(LOG_WARNING, "Unable to send CAS\n");
			}
			/* Wait For DTMF result */
			waittime = 500;
			for(;;) {
				if (((res = ast_waitfor(chan, waittime)) < 1)) {
					/* Didn't get back DTMF A in time */
					ast_log(LOG_DEBUG, "No ADSI CPE detected (%d)\n", res);
					if (!chan->adsicpe)
						chan->adsicpe = AST_ADSI_UNAVAILABLE;
					errno = ENOSYS;
					return -1;
				}
				waittime = res;
				f = ast_read(chan);
				if (!f) {
					ast_log(LOG_DEBUG, "Hangup in ADSI\n");
					return -1;
				}
				if (f->frametype == AST_FRAME_DTMF) {
					if (f->subclass == 'A') {
						/* Okay, this is an ADSI CPE.  Note this for future reference, too */
						if (!chan->adsicpe)
							chan->adsicpe = AST_ADSI_AVAILABLE;
						break;
					} else {
						if (f->subclass == 'D')  {
							ast_log(LOG_DEBUG, "Off-hook capable CPE only, not ADSI\n");
						} else
							ast_log(LOG_WARNING, "Unknown ADSI response '%c'\n", f->subclass);
						if (!chan->adsicpe)
							chan->adsicpe = AST_ADSI_UNAVAILABLE;
						errno =	ENOSYS;
						return -1;
					}
				}
				ast_frfree(f);
			}

			ast_log(LOG_DEBUG, "ADSI Compatible CPE Detected\n");
		} else
			ast_log(LOG_DEBUG, "Already in data mode\n");

		x = 0;
		pos = 0;
#if 1
		def= ast_channel_defer_dtmf(chan);
#endif
		while((x < 6) && msg[x]) {
			res = adsi_generate(buf + pos, msgtype[x], msg[x], msglen[x], x+1 - start, (x == 5) || !msg[x+1], AST_FORMAT_ULAW);
			if (res < 0) {
				ast_log(LOG_WARNING, "Failed to generate ADSI message %d on channel %s\n", x + 1, chan->name);
				return -1;
			}
			ast_log(LOG_DEBUG, "Message %d, of %d input bytes, %d output bytes\n", 
					x + 1, msglen[x], res);
			pos += res; 
			x++;
		}


		rem = 0;
		res = adsi_careful_send(chan, buf, pos, &rem); 
		if (!def)
			ast_channel_undefer_dtmf(chan);
		if (res)
			return -1;

		ast_log(LOG_DEBUG, "Sent total spill of %d bytes\n", pos);

		memset(ack, 0, sizeof(ack));
		/* Get real result */
		res = ast_readstring(chan, ack, 2, 1000, 1000, "");
		/* Check for hangup */
		if (res < 0)
			return -1;
		if (ack[0] == 'D') {
			ast_log(LOG_DEBUG, "Acked up to message %d\n", atoi(ack + 1));
			start += atoi(ack + 1);
			if (start >= x)
				break;
			else {
				retries++;
				ast_log(LOG_DEBUG, "Retransmitting (%d), from %d\n", retries, start + 1);
			}
		} else {
			retries++;
			ast_log(LOG_WARNING, "Unexpected response to ack: %s (retry %d)\n", ack, retries);
		} 
	}
	if (retries >= maxretries) {
		ast_log(LOG_WARNING, "Maximum ADSI Retries (%d) exceeded\n", maxretries);
		errno = ETIMEDOUT;
		return -1;
	}
	return 0;
	
}

int adsi_begin_download(struct ast_channel *chan, char *service, char *fdn, char *sec, int version)
{
	int bytes;
	unsigned char buf[256];
	char ack[2];
	bytes = 0;
	/* Setup the resident soft key stuff, a piece at a time */
	/* Upload what scripts we can for voicemail ahead of time */
	bytes += adsi_download_connect(buf + bytes, service, fdn, sec, version);
	if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD))
		return -1;
	if (ast_readstring(chan, ack, 1, 10000, 10000, ""))
		return -1;
	if (ack[0] == 'B')
		return 0;
	ast_log(LOG_DEBUG, "Download was denied by CPE\n");
	return -1;
}

int adsi_end_download(struct ast_channel *chan)
{
	int bytes;
	unsigned char buf[256];
        bytes = 0;
        /* Setup the resident soft key stuff, a piece at a time */
        /* Upload what scripts we can for voicemail ahead of time */
        bytes += adsi_download_disconnect(buf + bytes);
	if (adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD))
		return -1;
	return 0;
}

int adsi_transmit_message(struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype)
{
	unsigned char *msgs[5] = { NULL, NULL, NULL, NULL, NULL };
	int msglens[5];
	int msgtypes[5];
	int newdatamode;
	int res;
	int x;
	int writeformat, readformat;

	writeformat = chan->writeformat;
	readformat = chan->readformat;

	newdatamode = chan->adsicpe & ADSI_FLAG_DATAMODE;

	for (x=0;x<msglen;x+=(msg[x+1]+2)) {
		if (msg[x] == ADSI_SWITCH_TO_DATA) 
			newdatamode = ADSI_FLAG_DATAMODE;
		
		if (msg[x] == ADSI_SWITCH_TO_VOICE)
			newdatamode = 0;
	}
	msgs[0] = msg;

	msglens[0] = msglen;
	msgtypes[0] = msgtype;

	if (msglen > 253) {
		ast_log(LOG_WARNING, "Can't send ADSI message of %d bytes, too large\n", msglen);
		return -1;
	}

	ast_stopstream(chan);

	if (ast_set_write_format(chan, AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "Unable to set write format to ULAW\n");
		return -1;
	}

	if (ast_set_read_format(chan, AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "Unable to set read format to ULAW\n");
		if (writeformat) {
			if (ast_set_write_format(chan, writeformat)) 
				ast_log(LOG_WARNING, "Unable to restore write format to %d\n", writeformat);
		}
		return -1;
	}
	res = __adsi_transmit_messages(chan, msgs, msglens, msgtypes);
	if (!res)
		chan->adsicpe = (chan->adsicpe & ~ADSI_FLAG_DATAMODE) | newdatamode;

	if (writeformat)
		ast_set_write_format(chan, writeformat);
	if (readformat)
		ast_set_read_format(chan, readformat);

	return res;
}

static inline int ccopy(unsigned char *dst, unsigned char *src, int max)
{
	int x=0;
	/* Carefully copy the requested data */
	while ((x < max) && src[x] && (src[x] != 0xff)) {
		dst[x] = src[x];
		x++;
	}
	return x;
}

int adsi_load_soft_key(unsigned char *buf, int key, unsigned char *llabel, unsigned char *slabel, unsigned char *ret, int data)
{
	int bytes=0;

	/* Abort if invalid key specified */
	if ((key < 2) || (key > 33))
		return -1;
	buf[bytes++] = ADSI_LOAD_SOFTKEY;
	/* Reserve for length */
	bytes++;
	/* Which key */
	buf[bytes++] = key;

	/* Carefully copy long label */
	bytes += ccopy(buf + bytes, llabel, 18);

	/* Place delimiter */
	buf[bytes++] = 0xff;

	/* Short label */
	bytes += ccopy(buf + bytes, slabel, 7);


	/* If specified, copy return string */
	if (ret) {
		/* Place delimiter */
		buf[bytes++] = 0xff;
		if (data)
			buf[bytes++] = ADSI_SWITCH_TO_DATA2;
		/* Carefully copy return string */
		bytes += ccopy(buf + bytes, ret, 20);

	}
	/* Replace parameter length */
	buf[1] = bytes - 2;
	return bytes;
	
}

int adsi_connect_session(unsigned char *buf, unsigned char *fdn, int ver)
{
	int bytes=0;
	int x;

	/* Message type */
	buf[bytes++] = ADSI_CONNECT_SESSION;

	/* Reserve space for length */
	bytes++;

	if (fdn) {
		for (x=0;x<4;x++)
			buf[bytes++] = fdn[x];
		if (ver > -1)
			buf[bytes++] = ver & 0xff;
	}

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_download_connect(unsigned char *buf, unsigned char *service,  unsigned char *fdn, unsigned char *sec, int ver)
{
	int bytes=0;
	int x;

	/* Message type */
	buf[bytes++] = ADSI_DOWNLOAD_CONNECT;

	/* Reserve space for length */
	bytes++;

	/* Primary column */
	bytes+= ccopy(buf + bytes, service, 18);

	/* Delimiter */
	buf[bytes++] = 0xff;
	
	for (x=0;x<4;x++) {
		buf[bytes++] = fdn[x];
	}
	for (x=0;x<4;x++)
		buf[bytes++] = sec[x];
	buf[bytes++] = ver & 0xff;

	buf[1] = bytes - 2;

	return bytes;

}

int adsi_disconnect_session(unsigned char *buf)
{
	int bytes=0;

	/* Message type */
	buf[bytes++] = ADSI_DISC_SESSION;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_query_cpeid(unsigned char *buf)
{
	int bytes = 0;
	buf[bytes++] = ADSI_QUERY_CPEID;
	/* Reserve space for length */
	bytes++;
	buf[1] = bytes - 2;
	return bytes;
}

int adsi_query_cpeinfo(unsigned char *buf)
{
	int bytes = 0;
	buf[bytes++] = ADSI_QUERY_CONFIG;
	/* Reserve space for length */
	bytes++;
	buf[1] = bytes - 2;
	return bytes;
}

int adsi_read_encoded_dtmf(struct ast_channel *chan, unsigned char *buf, int maxlen)
{
	int bytes = 0;
	int res;
	unsigned char current = 0;
	int gotstar = 0;
	int pos = 0;
	memset(buf, 0, sizeof(buf));
	while(bytes <= maxlen) {
		/* Wait up to a second for a digit */
		res = ast_waitfordigit(chan, 1000);
		if (!res)
			break;
		if (res == '*') {
			gotstar = 1;	
			continue;
		}
		/* Ignore anything other than a digit */
		if ((res < '0') || (res > '9'))
			continue;
		res -= '0';
		if (gotstar)
			res += 9;
		if (pos)  {
			pos = 0;
			buf[bytes++] = (res << 4) | current;
		} else {
			pos = 1;
			current = res;
		}
		gotstar = 0;
	}
	return bytes;
}

int adsi_get_cpeid(struct ast_channel *chan, unsigned char *cpeid, int voice)
{
	char buf[256];
	int bytes = 0;
	int res;
	bytes += adsi_data_mode(buf);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	bytes = 0;
	bytes += adsi_query_cpeid(buf);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	/* Get response */
	memset(buf, 0, sizeof(buf));
	res = adsi_read_encoded_dtmf(chan, cpeid, 4);
	if (res != 4) {
		ast_log(LOG_WARNING, "Got %d bytes back of encoded DTMF, expecting 4\n", res);
		res = 0;
	} else {
		res = 1;
	}

	if (voice) {
		bytes = 0;
		bytes += adsi_voice_mode(buf, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
		/* Ignore the resulting DTMF B announcing it's in voice mode */
		ast_waitfordigit(chan, 1000);
	}
	return res;
}

int adsi_get_cpeinfo(struct ast_channel *chan, int *width, int *height, int *buttons, int voice)
{
	char buf[256];
	int bytes = 0;
	int res;
	bytes += adsi_data_mode(buf);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	bytes = 0;
	bytes += adsi_query_cpeinfo(buf);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	/* Get width */
	memset(buf, 0, sizeof(buf));
	res = ast_readstring(chan, buf, 2, 1000, 500, "");
	if (res < 0)
		return res;
	if (strlen(buf) != 2) {
		ast_log(LOG_WARNING, "Got %d bytes of width, expecting 2\n", res);
		res = 0;
	} else {
		res = 1;
	}
	if (width)
		*width = atoi(buf);
	/* Get height */
	memset(buf, 0, sizeof(buf));
	if (res) {
		res = ast_readstring(chan, buf, 2, 1000, 500, "");
		if (res < 0)
			return res;
		if (strlen(buf) != 2) {
			ast_log(LOG_WARNING, "Got %d bytes of height, expecting 2\n", res);
			res = 0;
		} else {
			res = 1;
		}	
		if (height)
			*height= atoi(buf);
	}
	/* Get buttons */
	memset(buf, 0, sizeof(buf));
	if (res) {
		res = ast_readstring(chan, buf, 1, 1000, 500, "");
		if (res < 0)
			return res;
		if (strlen(buf) != 1) {
			ast_log(LOG_WARNING, "Got %d bytes of buttons, expecting 1\n", res);
			res = 0;
		} else {
			res = 1;
		}	
		if (buttons)
			*buttons = atoi(buf);
	}
	if (voice) {
		bytes = 0;
		bytes += adsi_voice_mode(buf, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
		/* Ignore the resulting DTMF B announcing it's in voice mode */
		ast_waitfordigit(chan, 1000);
	}
	return res;
}

int adsi_data_mode(unsigned char *buf)
{
	int bytes=0;

	/* Message type */
	buf[bytes++] = ADSI_SWITCH_TO_DATA;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_clear_soft_keys(unsigned char *buf)
{
	int bytes=0;

	/* Message type */
	buf[bytes++] = ADSI_CLEAR_SOFTKEY;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_clear_screen(unsigned char *buf)
{
	int bytes=0;

	/* Message type */
	buf[bytes++] = ADSI_CLEAR_SCREEN;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_voice_mode(unsigned char *buf, int when)
{
	int bytes=0;

	/* Message type */
	buf[bytes++] = ADSI_SWITCH_TO_VOICE;

	/* Reserve space for length */
	bytes++;

	buf[bytes++] = when & 0x7f;

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_available(struct ast_channel *chan)
{
	int cpe = chan->adsicpe & 0xff;
	if ((cpe == AST_ADSI_AVAILABLE) ||
	    (cpe == AST_ADSI_UNKNOWN))
		return 1;
	return 0;
}

int adsi_download_disconnect(unsigned char *buf)
{
	int bytes=0;

	/* Message type */
	buf[bytes++] = ADSI_DOWNLOAD_DISC;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int adsi_display(unsigned char *buf, int page, int line, int just, int wrap, 
		 unsigned char *col1, unsigned char *col2)
{
	int bytes=0;

	/* Sanity check line number */

	if (page) {
		if (line > 4) return -1;
	} else {
		if (line > 33) return -1;
	}

	if (line < 1)
		return -1;
	/* Parameter type */
	buf[bytes++] = ADSI_LOAD_VIRTUAL_DISP;
	
	/* Reserve space for size */
	bytes++;

	/* Page and wrap indicator */
	buf[bytes++] = ((page & 0x1) << 7) | ((wrap & 0x1) << 6) | (line & 0x3f);

	/* Justification */
	buf[bytes++] = (just & 0x3) << 5;

	/* Omit highlight mode definition */
	buf[bytes++] = 0xff;

	/* Primary column */
	bytes+= ccopy(buf + bytes, col1, 20);

	/* Delimiter */
	buf[bytes++] = 0xff;
	
	/* Secondary column */
	bytes += ccopy(buf + bytes, col2, 20);

	/* Update length */
	buf[1] = bytes - 2;
	
	return bytes;

}

int adsi_input_control(unsigned char *buf, int page, int line, int display, int format, int just)
{
	int bytes=0;

	if (page) {
		if (line > 4) return -1;
	} else {
		if (line > 33) return -1;
	}

	if (line < 1)
		return -1;

	buf[bytes++] = ADSI_INPUT_CONTROL;
	bytes++;
	buf[bytes++] = ((page & 1) << 7) | (line & 0x3f);
	buf[bytes++] = ((display & 1) << 7) | ((just & 0x3) << 4) | (format & 0x7);
	
	buf[1] = bytes - 2;
	return bytes;

}

int adsi_input_format(unsigned char *buf, int num, int dir, int wrap, unsigned char *format1, unsigned char *format2)
{
	int bytes = 0;

	if (!strlen(format1))
		return -1;

	buf[bytes++] = ADSI_INPUT_FORMAT;
	bytes++;
	buf[bytes++] = ((dir & 1) << 7) | ((wrap & 1) << 6) | (num & 0x7);
	bytes += ccopy(buf + bytes, format1, 20);
	buf[bytes++] = 0xff;
	if (format2 && strlen(format2)) {
		bytes += ccopy(buf + bytes, format2, 20);
	}
	buf[1] = bytes - 2;
	return bytes;
}

int adsi_set_keys(unsigned char *buf, unsigned char *keys)
{
	int bytes=0;
	int x;
	/* Message type */
	buf[bytes++] = ADSI_INIT_SOFTKEY_LINE;
	/* Space for size */
	bytes++;
	/* Key definitions */
	for (x=0;x<6;x++)
		buf[bytes++] = (keys[x] & 0x3f) ? keys[x] : (keys[x] | 0x1);
	buf[1] = bytes - 2;
	return bytes;
}

int adsi_set_line(unsigned char *buf, int page, int line)
{
	int bytes=0;

	/* Sanity check line number */

	if (page) {
		if (line > 4) return -1;
	} else {
		if (line > 33) return -1;
	}

	if (line < 1)
		return -1;
	/* Parameter type */
	buf[bytes++] = ADSI_LINE_CONTROL;
	
	/* Reserve space for size */
	bytes++;

	/* Page and line */
	buf[bytes++] = ((page & 0x1) << 7) | (line & 0x3f);

	buf[1] = bytes - 2;
	return bytes;

};

static int total = 0;
static int speeds = 0;

int adsi_channel_restore(struct ast_channel *chan)
{
	char dsp[256];
	int bytes;
	int x;
	unsigned char keyd[6];

	memset(dsp, 0, sizeof(dsp));

	/* Start with initial display setup */
	bytes = 0;
	bytes += adsi_set_line(dsp + bytes, ADSI_INFO_PAGE, 1);

	/* Prepare key setup messages */

	if (speeds) {
		memset(keyd, 0, sizeof(keyd));
		for (x=0;x<speeds;x++) {
			keyd[x] = ADSI_SPEED_DIAL + x;
		}
		bytes += adsi_set_keys(dsp + bytes, keyd);
	}
	adsi_transmit_message(chan, dsp, bytes, ADSI_MSG_DISPLAY);
	return 0;

}

int adsi_print(struct ast_channel *chan, char **lines, int *aligns, int voice)
{
	char buf[4096];
	int bytes=0;
	int res;
	int x;
	for(x=0;lines[x];x++) 
		bytes += adsi_display(buf + bytes, ADSI_INFO_PAGE, x+1, aligns[x],0, lines[x], "");
	bytes += adsi_set_line(buf + bytes, ADSI_INFO_PAGE, 1);
	if (voice) {
		bytes += adsi_voice_mode(buf + bytes, 0);
	}
	res = adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	if (voice) {
		/* Ignore the resulting DTMF B announcing it's in voice mode */
		ast_waitfordigit(chan, 1000);
	}
	return res;
}

int adsi_load_session(struct ast_channel *chan, unsigned char *app, int ver, int data)
{
	char dsp[256];
	int bytes;
	int res;
	char resp[2];

	memset(dsp, 0, sizeof(dsp));

	/* Connect to session */
	bytes = 0;
	bytes += adsi_connect_session(dsp + bytes, app,ver);

	if (data)
		bytes += adsi_data_mode(dsp + bytes);

	/* Prepare key setup messages */
	if (adsi_transmit_message(chan, dsp, bytes, ADSI_MSG_DISPLAY))
		return -1;
	if (app) {
		res = ast_readstring(chan, resp, 1, 1200, 1200, "");
		if (res < 0)
			return -1;
		if (res) {
			ast_log(LOG_DEBUG, "No response from CPE about version.  Assuming not there.\n");
			return 0;
		}
		if (!strcmp(resp, "B")) {
			ast_log(LOG_DEBUG, "CPE has script '%s' version %d already loaded\n", app, ver);
			return 1;
		} else if (!strcmp(resp, "A")) {
			ast_log(LOG_DEBUG, "CPE hasn't script '%s' version %d already loaded\n", app, ver);
		} else {
			ast_log(LOG_WARNING, "Unexpected CPE response to script query: %s\n", resp);
		}
	} else
		return 1;
	return 0;

}

int adsi_unload_session(struct ast_channel *chan)
{
	char dsp[256];
	int bytes;

	memset(dsp, 0, sizeof(dsp));

	/* Connect to session */
	bytes = 0;
	bytes += adsi_disconnect_session(dsp + bytes);
	bytes += adsi_voice_mode(dsp + bytes, 0);

	/* Prepare key setup messages */
	if (adsi_transmit_message(chan, dsp, bytes, ADSI_MSG_DISPLAY))
		return -1;
	return 0;
}

static int str2align(char *s)
{
	if (!strncasecmp(s, "l", 1))
		return ADSI_JUST_LEFT;
	else if (!strncasecmp(s, "r", 1))
		return ADSI_JUST_RIGHT;
	else if (!strncasecmp(s, "i", 1))
		return ADSI_JUST_IND;
	else
		return ADSI_JUST_CENT;
}

static void init_state(void)
{
	int x;

	for (x=0;x<ADSI_MAX_INTRO;x++)
		aligns[x] = ADSI_JUST_CENT;
	strcpy(intro[0], "Welcome to the");
	strcpy(intro[1], "Asterisk");
	strcpy(intro[2], "Open Source PBX");
	total = 3;
	speeds = 0;
	for (x=3;x<ADSI_MAX_INTRO;x++)
		strcpy(intro[x], "");
	memset(speeddial, 0, sizeof(speeddial));
	alignment = ADSI_JUST_CENT;
}

static void adsi_load(void)
{
	int x;
	struct ast_config *conf;
	struct ast_variable *v;
	char *name, *sname;
	init_state();
	conf = ast_load("adsi.conf");
	if (conf) {
		x=0;
		v = ast_variable_browse(conf, "intro");
		while(v) {
			if (!strcasecmp(v->name, "alignment"))
				alignment = str2align(v->value);
			else if (!strcasecmp(v->name, "greeting")) {
				if (x < ADSI_MAX_INTRO) {
					aligns[x] = alignment;
					strncpy(intro[x], v->value, 20);
					x++;
				}
			} else if (!strcasecmp(v->name, "maxretries")) {
				if (atoi(v->value) > 0)
					maxretries = atoi(v->value);
			}
			v = v->next;
		}
		v = ast_variable_browse(conf, "speeddial");
		if (x)
			total = x;
		x = 0;
		while(v) {
			char *stringp=NULL;
			stringp=v->value;
			name = strsep(&stringp, ",");
			sname = strsep(&stringp, ",");
			if (!sname) 
				sname = name;
			if (x < ADSI_MAX_SPEED_DIAL) {
				/* Up to 20 digits */
				strncpy(speeddial[x][0], v->name, 20);
				strncpy(speeddial[x][1], name, 18);
				strncpy(speeddial[x][2], sname, 7);
				x++;
			}
			v = v->next;
				
		}
		if (x)
			speeds = x;
		ast_destroy(conf);
	}
}

int reload(void)
{
	adsi_load();
	return 0;
}

int load_module(void)
{
	adsi_load();
	return 0;
}

int unload_module(void)
{
	/* Can't unload this once we're loaded */
	return -1;
}

char *description(void)
{
	return "ADSI Resource";
}

int usecount(void)
{
	/* We should never be unloaded */
	return 1;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
