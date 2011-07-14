/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Includes code and algorithms from the Zapata library.
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
 * \brief ADSI support
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note this module is required by app_voicemail and app_getcpeid
 * \todo Move app_getcpeid into this module
 * \todo Create a core layer so that app_voicemail does not require
 * 	res_adsi to load
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>
#include <math.h>

#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"
#include "asterisk/fskmodem.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/file.h"

#define AST_API_MODULE
#include "asterisk/adsi.h"

#define DEFAULT_ADSI_MAX_RETRIES 3

#define ADSI_MAX_INTRO 20
#define ADSI_MAX_SPEED_DIAL 6

#define ADSI_FLAG_DATAMODE	(1 << 8)

static int maxretries = DEFAULT_ADSI_MAX_RETRIES;

/* Asterisk ADSI button definitions */
#define ADSI_SPEED_DIAL		10	/* 10-15 are reserved for speed dial */

static char intro[ADSI_MAX_INTRO][20];
static int aligns[ADSI_MAX_INTRO];

#define	SPEEDDIAL_MAX_LEN	20
static char speeddial[ADSI_MAX_SPEED_DIAL][3][SPEEDDIAL_MAX_LEN];

static int alignment = 0;

static int adsi_generate(unsigned char *buf, int msgtype, unsigned char *msg, int msglen, int msgnum, int last, format_t codec)
{
	int sum, x, bytes = 0;
	/* Initial carrier (imaginary) */
	float cr = 1.0, ci = 0.0, scont = 0.0;

	if (msglen > 255) {
		msglen = 255;
	}

	/* If first message, Send 150ms of MARK's */
	if (msgnum == 1) {
		for (x = 0; x < 150; x++) { /* was 150 */
			PUT_CLID_MARKMS;
		}
	}

	/* Put message type */
	PUT_CLID(msgtype);
	sum = msgtype;

	/* Put message length (plus one for the message number) */
	PUT_CLID(msglen + 1);
	sum += msglen + 1;

	/* Put message number */
	PUT_CLID(msgnum);
	sum += msgnum;

	/* Put actual message */
	for (x = 0; x < msglen; x++) {
		PUT_CLID(msg[x]);
		sum += msg[x];
	}

	/* Put 2's compliment of sum */
	PUT_CLID(256-(sum & 0xff));

#if 0
	if (last) {
		/* Put trailing marks */
		for (x = 0; x < 50; x++) {
			PUT_CLID_MARKMS;
		}
	}
#endif
	return bytes;

}

static int adsi_careful_send(struct ast_channel *chan, unsigned char *buf, int len, int *remain)
{
	/* Sends carefully on a full duplex channel by using reading for
	   timing */
	struct ast_frame *inf, outf;
	int amt;

	/* Zero out our outgoing frame */
	memset(&outf, 0, sizeof(outf));

	if (remain && *remain) {
		amt = len;

		/* Send remainder if provided */
		if (amt > *remain) {
			amt = *remain;
		} else {
			*remain = *remain - amt;
		}
		outf.frametype = AST_FRAME_VOICE;
		outf.subclass.codec = AST_FORMAT_ULAW;
		outf.data.ptr = buf;
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

	while (len) {
		amt = len;
		/* If we don't get anything at all back in a second, forget
		   about it */
		if (ast_waitfor(chan, 1000) < 1) {
			return -1;
		}
		/* Detect hangup */
		if (!(inf = ast_read(chan))) {
			return -1;
		}

		/* Drop any frames that are not voice */
		if (inf->frametype != AST_FRAME_VOICE) {
			ast_frfree(inf);
			continue;
		}

		if (inf->subclass.codec != AST_FORMAT_ULAW) {
			ast_log(LOG_WARNING, "Channel not in ulaw?\n");
			ast_frfree(inf);
			return -1;
		}
		/* Send no more than they sent us */
		if (amt > inf->datalen) {
			amt = inf->datalen;
		} else if (remain) {
			*remain = inf->datalen - amt;
		}
		outf.frametype = AST_FRAME_VOICE;
		outf.subclass.codec = AST_FORMAT_ULAW;
		outf.data.ptr = buf;
		outf.datalen = amt;
		outf.samples = amt;
		if (ast_write(chan, &outf)) {
			ast_log(LOG_WARNING, "Failed to carefully write frame\n");
			ast_frfree(inf);
			return -1;
		}
		/* Update pointers and lengths */
		buf += amt;
		len -= amt;
		ast_frfree(inf);
	}
	return 0;
}

static int __adsi_transmit_messages(struct ast_channel *chan, unsigned char **msg, int *msglen, int *msgtype)
{
	/* msglen must be no more than 256 bits, each */
	unsigned char buf[24000 * 5];
	int pos = 0, res, x, start = 0, retries = 0, waittime, rem = 0, def;
	char ack[3];
	struct ast_frame *f;

	if (chan->adsicpe == AST_ADSI_UNAVAILABLE) {
		/* Don't bother if we know they don't support ADSI */
		errno = ENOSYS;
		return -1;
	}

	while (retries < maxretries) {
		if (!(chan->adsicpe & ADSI_FLAG_DATAMODE)) {
			/* Generate CAS (no SAS) */
			ast_gen_cas(buf, 0, 680, AST_FORMAT_ULAW);

			/* Send CAS */
			if (adsi_careful_send(chan, buf, 680, NULL)) {
				ast_log(LOG_WARNING, "Unable to send CAS\n");
			}

			/* Wait For DTMF result */
			waittime = 500;
			for (;;) {
				if (((res = ast_waitfor(chan, waittime)) < 1)) {
					/* Didn't get back DTMF A in time */
					ast_debug(1, "No ADSI CPE detected (%d)\n", res);
					if (!chan->adsicpe) {
						chan->adsicpe = AST_ADSI_UNAVAILABLE;
					}
					errno = ENOSYS;
					return -1;
				}
				waittime = res;
				if (!(f = ast_read(chan))) {
					ast_debug(1, "Hangup in ADSI\n");
					return -1;
				}
				if (f->frametype == AST_FRAME_DTMF) {
					if (f->subclass.integer == 'A') {
						/* Okay, this is an ADSI CPE.  Note this for future reference, too */
						if (!chan->adsicpe) {
							chan->adsicpe = AST_ADSI_AVAILABLE;
						}
						break;
					} else {
						if (f->subclass.integer == 'D') {
							ast_debug(1, "Off-hook capable CPE only, not ADSI\n");
						} else {
							ast_log(LOG_WARNING, "Unknown ADSI response '%c'\n", f->subclass.integer);
						}
						if (!chan->adsicpe) {
							chan->adsicpe = AST_ADSI_UNAVAILABLE;
						}
						errno =	ENOSYS;
						ast_frfree(f);
						return -1;
					}
				}
				ast_frfree(f);
			}

			ast_debug(1, "ADSI Compatible CPE Detected\n");
		} else {
			ast_debug(1, "Already in data mode\n");
		}

		x = 0;
		pos = 0;
#if 1
		def= ast_channel_defer_dtmf(chan);
#endif
		while ((x < 6) && msg[x]) {
			if ((res = adsi_generate(buf + pos, msgtype[x], msg[x], msglen[x], x+1 - start, (x == 5) || !msg[x+1], AST_FORMAT_ULAW)) < 0) {
				ast_log(LOG_WARNING, "Failed to generate ADSI message %d on channel %s\n", x + 1, chan->name);
				return -1;
			}
			ast_debug(1, "Message %d, of %d input bytes, %d output bytes\n", x + 1, msglen[x], res);
			pos += res;
			x++;
		}


		rem = 0;
		res = adsi_careful_send(chan, buf, pos, &rem);
		if (!def) {
			ast_channel_undefer_dtmf(chan);
		}
		if (res) {
			return -1;
		}

		ast_debug(1, "Sent total spill of %d bytes\n", pos);

		memset(ack, 0, sizeof(ack));
		/* Get real result and check for hangup */
		if ((res = ast_readstring(chan, ack, 2, 1000, 1000, "")) < 0) {
			return -1;
		}
		if (ack[0] == 'D') {
			ast_debug(1, "Acked up to message %d\n", atoi(ack + 1)); start += atoi(ack + 1);
			if (start >= x) {
				break;
			} else {
				retries++;
				ast_debug(1, "Retransmitting (%d), from %d\n", retries, start + 1);
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

int AST_OPTIONAL_API_NAME(ast_adsi_begin_download)(struct ast_channel *chan, char *service, unsigned char *fdn, unsigned char *sec, int version)
{
	int bytes = 0;
	unsigned char buf[256];
	char ack[2];

	/* Setup the resident soft key stuff, a piece at a time */
	/* Upload what scripts we can for voicemail ahead of time */
	bytes += ast_adsi_download_connect(buf + bytes, service, fdn, sec, version);
	if (ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DOWNLOAD, 0)) {
		return -1;
	}
	if (ast_readstring(chan, ack, 1, 10000, 10000, "")) {
		return -1;
	}
	if (ack[0] == 'B') {
		return 0;
	}
	ast_debug(1, "Download was denied by CPE\n");
	return -1;
}

int AST_OPTIONAL_API_NAME(ast_adsi_end_download)(struct ast_channel *chan)
{
	int bytes = 0;
	unsigned char buf[256];

	/* Setup the resident soft key stuff, a piece at a time */
	/* Upload what scripts we can for voicemail ahead of time */
	bytes += ast_adsi_download_disconnect(buf + bytes);
	if (ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DOWNLOAD, 0)) {
		return -1;
	}
	return 0;
}

int AST_OPTIONAL_API_NAME(ast_adsi_transmit_message_full)(struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype, int dowait)
{
	unsigned char *msgs[5] = { NULL, NULL, NULL, NULL, NULL };
	int msglens[5], msgtypes[5], newdatamode = (chan->adsicpe & ADSI_FLAG_DATAMODE), res, x, writeformat = chan->writeformat, readformat = chan->readformat, waitforswitch = 0;

	for (x = 0; x < msglen; x += (msg[x+1]+2)) {
		if (msg[x] == ADSI_SWITCH_TO_DATA) {
			ast_debug(1, "Switch to data is sent!\n");
			waitforswitch++;
			newdatamode = ADSI_FLAG_DATAMODE;
		}

		if (msg[x] == ADSI_SWITCH_TO_VOICE) {
			ast_debug(1, "Switch to voice is sent!\n");
			waitforswitch++;
			newdatamode = 0;
		}
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
			if (ast_set_write_format(chan, writeformat)) {
				ast_log(LOG_WARNING, "Unable to restore write format to %d\n", writeformat);
			}
		}
		return -1;
	}
	res = __adsi_transmit_messages(chan, msgs, msglens, msgtypes);

	if (dowait) {
		ast_debug(1, "Wait for switch is '%d'\n", waitforswitch);
		while (waitforswitch-- && ((res = ast_waitfordigit(chan, 1000)) > 0)) {
			res = 0;
			ast_debug(1, "Waiting for 'B'...\n");
		}
	}

	if (!res) {
		chan->adsicpe = (chan->adsicpe & ~ADSI_FLAG_DATAMODE) | newdatamode;
	}

	if (writeformat) {
		ast_set_write_format(chan, writeformat);
	}
	if (readformat) {
		ast_set_read_format(chan, readformat);
	}

	if (!res) {
		res = ast_safe_sleep(chan, 100 );
	}
	return res;
}

int AST_OPTIONAL_API_NAME(ast_adsi_transmit_message)(struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype)
{
	return ast_adsi_transmit_message_full(chan, msg, msglen, msgtype, 1);
}

static inline int ccopy(unsigned char *dst, const unsigned char *src, int max)
{
	int x = 0;
	/* Carefully copy the requested data */
	while ((x < max) && src[x] && (src[x] != 0xff)) {
		dst[x] = src[x];
		x++;
	}
	return x;
}

int AST_OPTIONAL_API_NAME(ast_adsi_load_soft_key)(unsigned char *buf, int key, const char *llabel, const char *slabel, char *ret, int data)
{
	int bytes = 0;

	/* Abort if invalid key specified */
	if ((key < 2) || (key > 33)) {
		return -1;
	}

	buf[bytes++] = ADSI_LOAD_SOFTKEY;
	/* Reserve for length */
	bytes++;
	/* Which key */
	buf[bytes++] = key;

	/* Carefully copy long label */
	bytes += ccopy(buf + bytes, (const unsigned char *)llabel, 18);

	/* Place delimiter */
	buf[bytes++] = 0xff;

	/* Short label */
	bytes += ccopy(buf + bytes, (const unsigned char *)slabel, 7);


	/* If specified, copy return string */
	if (ret) {
		/* Place delimiter */
		buf[bytes++] = 0xff;
		if (data) {
			buf[bytes++] = ADSI_SWITCH_TO_DATA2;
		}
		/* Carefully copy return string */
		bytes += ccopy(buf + bytes, (const unsigned char *)ret, 20);

	}
	/* Replace parameter length */
	buf[1] = bytes - 2;
	return bytes;
}

int AST_OPTIONAL_API_NAME(ast_adsi_connect_session)(unsigned char *buf, unsigned char *fdn, int ver)
{
	int bytes = 0, x;

	/* Message type */
	buf[bytes++] = ADSI_CONNECT_SESSION;

	/* Reserve space for length */
	bytes++;

	if (fdn) {
		for (x = 0; x < 4; x++) {
			buf[bytes++] = fdn[x];
		}
		if (ver > -1) {
			buf[bytes++] = ver & 0xff;
		}
	}

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_download_connect)(unsigned char *buf, char *service,  unsigned char *fdn, unsigned char *sec, int ver)
{
	int bytes = 0, x;

	/* Message type */
	buf[bytes++] = ADSI_DOWNLOAD_CONNECT;

	/* Reserve space for length */
	bytes++;

	/* Primary column */
	bytes+= ccopy(buf + bytes, (unsigned char *)service, 18);

	/* Delimiter */
	buf[bytes++] = 0xff;

	for (x = 0; x < 4; x++) {
		buf[bytes++] = fdn[x];
	}

	for (x = 0; x < 4; x++) {
		buf[bytes++] = sec[x];
	}

	buf[bytes++] = ver & 0xff;

	buf[1] = bytes - 2;

	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_disconnect_session)(unsigned char *buf)
{
	int bytes = 0;

	/* Message type */
	buf[bytes++] = ADSI_DISC_SESSION;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_query_cpeid)(unsigned char *buf)
{
	int bytes = 0;
	buf[bytes++] = ADSI_QUERY_CPEID;
	/* Reserve space for length */
	bytes++;
	buf[1] = bytes - 2;
	return bytes;
}

int AST_OPTIONAL_API_NAME(ast_adsi_query_cpeinfo)(unsigned char *buf)
{
	int bytes = 0;
	buf[bytes++] = ADSI_QUERY_CONFIG;
	/* Reserve space for length */
	bytes++;
	buf[1] = bytes - 2;
	return bytes;
}

int AST_OPTIONAL_API_NAME(ast_adsi_read_encoded_dtmf)(struct ast_channel *chan, unsigned char *buf, int maxlen)
{
	int bytes = 0, res, gotstar = 0, pos = 0;
	unsigned char current = 0;

	memset(buf, 0, sizeof(buf));

	while (bytes <= maxlen) {
		/* Wait up to a second for a digit */
		if (!(res = ast_waitfordigit(chan, 1000))) {
			break;
		}
		if (res == '*') {
			gotstar = 1;
			continue;
		}
		/* Ignore anything other than a digit */
		if ((res < '0') || (res > '9')) {
			continue;
		}
		res -= '0';
		if (gotstar) {
			res += 9;
		}
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

int AST_OPTIONAL_API_NAME(ast_adsi_get_cpeid)(struct ast_channel *chan, unsigned char *cpeid, int voice)
{
	unsigned char buf[256] = "";
	int bytes = 0, res;

	bytes += ast_adsi_data_mode(buf);
	ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);

	bytes = 0;
	bytes += ast_adsi_query_cpeid(buf);
	ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);

	/* Get response */
	res = ast_adsi_read_encoded_dtmf(chan, cpeid, 4);
	if (res != 4) {
		ast_log(LOG_WARNING, "Got %d bytes back of encoded DTMF, expecting 4\n", res);
		res = 0;
	} else {
		res = 1;
	}

	if (voice) {
		bytes = 0;
		bytes += ast_adsi_voice_mode(buf, 0);
		ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);
		/* Ignore the resulting DTMF B announcing it's in voice mode */
		ast_waitfordigit(chan, 1000);
	}
	return res;
}

int AST_OPTIONAL_API_NAME(ast_adsi_get_cpeinfo)(struct ast_channel *chan, int *width, int *height, int *buttons, int voice)
{
	unsigned char buf[256] = "";
	int bytes = 0, res;

	bytes += ast_adsi_data_mode(buf);
	ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);

	bytes = 0;
	bytes += ast_adsi_query_cpeinfo(buf);
	ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);

	/* Get width */
	if ((res = ast_readstring(chan, (char *) buf, 2, 1000, 500, "")) < 0) {
		return res;
	}
	if (strlen((char *) buf) != 2) {
		ast_log(LOG_WARNING, "Got %d bytes of width, expecting 2\n", res);
		res = 0;
	} else {
		res = 1;
	}
	if (width) {
		*width = atoi((char *) buf);
	}
	/* Get height */
	memset(buf, 0, sizeof(buf));
	if (res) {
		if ((res = ast_readstring(chan, (char *) buf, 2, 1000, 500, "")) < 0) {
			return res;
		}
		if (strlen((char *) buf) != 2) {
			ast_log(LOG_WARNING, "Got %d bytes of height, expecting 2\n", res);
			res = 0;
		} else {
			res = 1;
		}
		if (height) {
			*height = atoi((char *) buf);
		}
	}
	/* Get buttons */
	memset(buf, 0, sizeof(buf));
	if (res) {
		if ((res = ast_readstring(chan, (char *) buf, 1, 1000, 500, "")) < 0) {
			return res;
		}
		if (strlen((char *) buf) != 1) {
			ast_log(LOG_WARNING, "Got %d bytes of buttons, expecting 1\n", res);
			res = 0;
		} else {
			res = 1;
		}
		if (buttons) {
			*buttons = atoi((char *) buf);
		}
	}
	if (voice) {
		bytes = 0;
		bytes += ast_adsi_voice_mode(buf, 0);
		ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);
		/* Ignore the resulting DTMF B announcing it's in voice mode */
		ast_waitfordigit(chan, 1000);
	}
	return res;
}

int AST_OPTIONAL_API_NAME(ast_adsi_data_mode)(unsigned char *buf)
{
	int bytes = 0;

	/* Message type */
	buf[bytes++] = ADSI_SWITCH_TO_DATA;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_clear_soft_keys)(unsigned char *buf)
{
	int bytes = 0;

	/* Message type */
	buf[bytes++] = ADSI_CLEAR_SOFTKEY;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_clear_screen)(unsigned char *buf)
{
	int bytes = 0;

	/* Message type */
	buf[bytes++] = ADSI_CLEAR_SCREEN;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_voice_mode)(unsigned char *buf, int when)
{
	int bytes = 0;

	/* Message type */
	buf[bytes++] = ADSI_SWITCH_TO_VOICE;

	/* Reserve space for length */
	bytes++;

	buf[bytes++] = when & 0x7f;

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_available)(struct ast_channel *chan)
{
	int cpe = chan->adsicpe & 0xff;
	if ((cpe == AST_ADSI_AVAILABLE) ||
	    (cpe == AST_ADSI_UNKNOWN)) {
		return 1;
	}
	return 0;
}

int AST_OPTIONAL_API_NAME(ast_adsi_download_disconnect)(unsigned char *buf)
{
	int bytes = 0;

	/* Message type */
	buf[bytes++] = ADSI_DOWNLOAD_DISC;

	/* Reserve space for length */
	bytes++;

	buf[1] = bytes - 2;
	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_display)(unsigned char *buf, int page, int line, int just, int wrap,
		 char *col1, char *col2)
{
	int bytes = 0;

	/* Sanity check line number */

	if (page) {
		if (line > 4) return -1;
	} else {
		if (line > 33) return -1;
	}

	if (line < 1) {
		return -1;
	}
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
	bytes+= ccopy(buf + bytes, (unsigned char *)col1, 20);

	/* Delimiter */
	buf[bytes++] = 0xff;

	/* Secondary column */
	bytes += ccopy(buf + bytes, (unsigned char *)col2, 20);

	/* Update length */
	buf[1] = bytes - 2;

	return bytes;

}

int AST_OPTIONAL_API_NAME(ast_adsi_input_control)(unsigned char *buf, int page, int line, int display, int format, int just)
{
	int bytes = 0;

	if (page) {
		if (line > 4) return -1;
	} else {
		if (line > 33) return -1;
	}

	if (line < 1) {
		return -1;
	}

	buf[bytes++] = ADSI_INPUT_CONTROL;
	bytes++;
	buf[bytes++] = ((page & 1) << 7) | (line & 0x3f);
	buf[bytes++] = ((display & 1) << 7) | ((just & 0x3) << 4) | (format & 0x7);

	buf[1] = bytes - 2;
	return bytes;
}

int AST_OPTIONAL_API_NAME(ast_adsi_input_format)(unsigned char *buf, int num, int dir, int wrap, char *format1, char *format2)
{
	int bytes = 0;

	if (ast_strlen_zero((char *) format1)) {
		return -1;
	}

	buf[bytes++] = ADSI_INPUT_FORMAT;
	bytes++;
	buf[bytes++] = ((dir & 1) << 7) | ((wrap & 1) << 6) | (num & 0x7);
	bytes += ccopy(buf + bytes, (unsigned char *) format1, 20);
	buf[bytes++] = 0xff;
	if (!ast_strlen_zero(format2)) {
		bytes += ccopy(buf + bytes, (unsigned char *) format2, 20);
	}
	buf[1] = bytes - 2;
	return bytes;
}

int AST_OPTIONAL_API_NAME(ast_adsi_set_keys)(unsigned char *buf, unsigned char *keys)
{
	int bytes = 0, x;

	/* Message type */
	buf[bytes++] = ADSI_INIT_SOFTKEY_LINE;
	/* Space for size */
	bytes++;
	/* Key definitions */
	for (x = 0; x < 6; x++) {
		buf[bytes++] = (keys[x] & 0x3f) ? keys[x] : (keys[x] | 0x1);
	}
	buf[1] = bytes - 2;
	return bytes;
}

int AST_OPTIONAL_API_NAME(ast_adsi_set_line)(unsigned char *buf, int page, int line)
{
	int bytes = 0;

	/* Sanity check line number */

	if (page) {
		if (line > 4) return -1;
	} else {
		if (line > 33) return -1;
	}

	if (line < 1) {
		return -1;
	}
	/* Parameter type */
	buf[bytes++] = ADSI_LINE_CONTROL;

	/* Reserve space for size */
	bytes++;

	/* Page and line */
	buf[bytes++] = ((page & 0x1) << 7) | (line & 0x3f);

	buf[1] = bytes - 2;
	return bytes;
}

static int total = 0;
static int speeds = 0;

int AST_OPTIONAL_API_NAME(ast_adsi_channel_restore)(struct ast_channel *chan)
{
	unsigned char dsp[256] = "", keyd[6] = "";
	int bytes, x;

	/* Start with initial display setup */
	bytes = 0;
	bytes += ast_adsi_set_line(dsp + bytes, ADSI_INFO_PAGE, 1);

	/* Prepare key setup messages */

	if (speeds) {
		for (x = 0; x < speeds; x++) {
			keyd[x] = ADSI_SPEED_DIAL + x;
		}
		bytes += ast_adsi_set_keys(dsp + bytes, keyd);
	}
	ast_adsi_transmit_message_full(chan, dsp, bytes, ADSI_MSG_DISPLAY, 0);
	return 0;

}

int AST_OPTIONAL_API_NAME(ast_adsi_print)(struct ast_channel *chan, char **lines, int *alignments, int voice)
{
	unsigned char buf[4096];
	int bytes = 0, res, x;

	for (x = 0; lines[x]; x++) {
		bytes += ast_adsi_display(buf + bytes, ADSI_INFO_PAGE, x+1, alignments[x], 0, lines[x], "");
	}
	bytes += ast_adsi_set_line(buf + bytes, ADSI_INFO_PAGE, 1);
	if (voice) {
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
	}
	res = ast_adsi_transmit_message_full(chan, buf, bytes, ADSI_MSG_DISPLAY, 0);
	if (voice) {
		/* Ignore the resulting DTMF B announcing it's in voice mode */
		ast_waitfordigit(chan, 1000);
	}
	return res;
}

int AST_OPTIONAL_API_NAME(ast_adsi_load_session)(struct ast_channel *chan, unsigned char *app, int ver, int data)
{
	unsigned char dsp[256] = "";
	int bytes = 0, res;
	char resp[2];

	/* Connect to session */
	bytes += ast_adsi_connect_session(dsp + bytes, app, ver);

	if (data) {
		bytes += ast_adsi_data_mode(dsp + bytes);
	}

	/* Prepare key setup messages */
	if (ast_adsi_transmit_message_full(chan, dsp, bytes, ADSI_MSG_DISPLAY, 0)) {
		return -1;
	}
	if (app) {
		if ((res = ast_readstring(chan, resp, 1, 1200, 1200, "")) < 0) {
			return -1;
		}
		if (res) {
			ast_debug(1, "No response from CPE about version.  Assuming not there.\n");
			return 0;
		}
		if (!strcmp(resp, "B")) {
			ast_debug(1, "CPE has script '%s' version %d already loaded\n", app, ver);
			return 1;
		} else if (!strcmp(resp, "A")) {
			ast_debug(1, "CPE hasn't script '%s' version %d already loaded\n", app, ver);
		} else {
			ast_log(LOG_WARNING, "Unexpected CPE response to script query: %s\n", resp);
		}
	} else
		return 1;
	return 0;

}

int AST_OPTIONAL_API_NAME(ast_adsi_unload_session)(struct ast_channel *chan)
{
	unsigned char dsp[256] = "";
	int bytes = 0;

	/* Connect to session */
	bytes += ast_adsi_disconnect_session(dsp + bytes);
	bytes += ast_adsi_voice_mode(dsp + bytes, 0);

	/* Prepare key setup messages */
	if (ast_adsi_transmit_message_full(chan, dsp, bytes, ADSI_MSG_DISPLAY, 0)) {
		return -1;
	}

	return 0;
}

static int str2align(const char *s)
{
	if (!strncasecmp(s, "l", 1)) {
		return ADSI_JUST_LEFT;
	} else if (!strncasecmp(s, "r", 1)) {
		return ADSI_JUST_RIGHT;
	} else if (!strncasecmp(s, "i", 1)) {
		return ADSI_JUST_IND;
	} else {
		return ADSI_JUST_CENT;
	}
}

static void init_state(void)
{
	int x;

	for (x = 0; x < ADSI_MAX_INTRO; x++) {
		aligns[x] = ADSI_JUST_CENT;
	}
	ast_copy_string(intro[0], "Welcome to the", sizeof(intro[0]));
	ast_copy_string(intro[1], "Asterisk", sizeof(intro[1]));
	ast_copy_string(intro[2], "Open Source PBX", sizeof(intro[2]));
	total = 3;
	speeds = 0;
	for (x = 3; x < ADSI_MAX_INTRO; x++) {
		intro[x][0] = '\0';
	}
	memset(speeddial, 0, sizeof(speeddial));
	alignment = ADSI_JUST_CENT;
}

static void adsi_load(int reload)
{
	int x = 0;
	struct ast_config *conf = NULL;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	char *name, *sname;
	init_state();

	conf = ast_config_load("adsi.conf", config_flags);
	if (conf == CONFIG_STATUS_FILEMISSING || conf == CONFIG_STATUS_FILEUNCHANGED || conf == CONFIG_STATUS_FILEINVALID) {
		return;
	}
	for (v = ast_variable_browse(conf, "intro"); v; v = v->next) {
		if (!strcasecmp(v->name, "alignment")) {
			alignment = str2align(v->value);
		} else if (!strcasecmp(v->name, "greeting")) {
			if (x < ADSI_MAX_INTRO) {
				aligns[x] = alignment;
				ast_copy_string(intro[x], v->value, sizeof(intro[x]));
				x++;
			}
		} else if (!strcasecmp(v->name, "maxretries")) {
			if (atoi(v->value) > 0) {
				maxretries = atoi(v->value);
			}
		}
	}
	if (x) {
		total = x;
	}

	x = 0;
	for (v = ast_variable_browse(conf, "speeddial"); v; v = v->next) {
		char buf[3 * SPEEDDIAL_MAX_LEN];
		char *stringp = buf;
		ast_copy_string(buf, v->value, sizeof(buf));
		name = strsep(&stringp, ",");
		sname = strsep(&stringp, ",");
		if (!sname) {
			sname = name;
		}
		if (x < ADSI_MAX_SPEED_DIAL) {
			ast_copy_string(speeddial[x][0], v->name, sizeof(speeddial[x][0]));
			ast_copy_string(speeddial[x][1], name, 18);
			ast_copy_string(speeddial[x][2], sname, 7);
			x++;
		}
	}
	if (x) {
		speeds = x;
	}
	ast_config_destroy(conf);

	return;
}

static int reload(void)
{
	adsi_load(1);
	return 0;
}

static int load_module(void)
{
	adsi_load(0);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	/* Can't unload this once we're loaded */
	return -1;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "ADSI Resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
