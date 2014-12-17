/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005, Adrian Kennard, rights assigned to Digium
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
 * \brief SMS application - ETSI ES 201 912 protocol 1 implementation
 * 
 * \par Development notes
 * \note The ETSI standards are available free of charge from ETSI at
 *	http://pda.etsi.org/pda/queryform.asp
 * 	Among the relevant documents here we have:
 *
 *	ES 201 912	SMS for PSTN/ISDN
 *	TS 123 040	Technical realization of SMS
 *
 * 
 * \ingroup applications
 *
 * \author Adrian Kennard (for the original protocol 1 code)
 * \author Filippo Grassilli (Hyppo) - protocol 2 support
 *		   Not fully tested, under development
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>

#include "asterisk/paths.h"  /* use ast_config_AST_SPOOL_DIR and LOG_DIR */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="SMS" language="en_US">
		<synopsis>
			Communicates with SMS service centres and SMS capable analogue phones.
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The name of the queue used in <filename>/var/spool/asterisk/sms</filename></para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Answer, i.e. send initial FSK packet.</para>
					</option>
					<option name="s">
						<para>Act as service centre talking to a phone.</para>
					</option>
					<option name="t">
						<para>Use protocol 2 (default used is protocol 1).</para>
					</option>
					<option name="p">
						<para>Set the initial delay to N ms (default is <literal>300</literal>).
						addr and body are a deprecated format to send messages out.</para>
					</option>
					<option name="r">
						<para>Set the Status Report Request (SRR) bit.</para>
					</option>
					<option name="o">
						<para>The body should be coded as octets not 7-bit symbols.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="addr" />
			<parameter name="body" />
		</syntax>
		<description>
			<para>SMS handles exchange of SMS data with a call to/from SMS capable phone or SMS PSTN service center.
			Can send and/or receive SMS messages. Works to ETSI ES 201 912; compatible with BT SMS PSTN service in
			UK and Telecom Italia in Italy.</para>
			<para>Typical usage is to use to handle calls from the SMS service centre CLI, or to set up a call using
			<literal>outgoing</literal> or manager interface to connect service centre to SMS().</para>
			<para>"Messages are processed as per text file message queues. smsq (a separate software) is a command to
			generate message queues and send messages.</para>
			<note><para>The protocol has tight delay bounds. Please use short frames and disable/keep short the
			jitter buffer on the ATA to make sure that respones (ACK etc.) are received in time.</para></note>
		</description>
	</application>
 ***/

/* #define OUTALAW */        /* enable this to output Alaw rather than linear */

/* ToDo */
/* Add full VP support */
/* Handle status report messages (generation and reception) */
/* Time zones on time stamps */
/* user ref field */

static volatile unsigned char message_ref;  /* arbitary message ref */
static volatile unsigned int seq;           /* arbitrary message sequence number for unqiue files */

static char log_file[255];

static char *app = "SMS";

/*
 * 80 samples of a single period of the wave. At 8000 Hz, it means these
 * are the samples of a 100 Hz signal.
 * To pick the two carriers (1300Hz for '1' and 2100 Hz for '0') used by
 * the modulation, we should take one every 13 and 21 samples respectively.
 */
static const signed short wave[] = {
	0, 392, 782, 1167, 1545, 1913, 2270, 2612, 2939, 3247, 3536, 3802, 4045, 4263, 4455, 4619, 4755, 4862, 4938, 4985,
	5000, 4985, 4938, 4862, 4755, 4619, 4455, 4263, 4045, 3802, 3536, 3247, 2939, 2612, 2270, 1913, 1545, 1167, 782, 392,
	0, -392, -782, -1167,
	 -1545, -1913, -2270, -2612, -2939, -3247, -3536, -3802, -4045, -4263, -4455, -4619, -4755, -4862, -4938, -4985, -5000,
	-4985, -4938, -4862,
	-4755, -4619, -4455, -4263, -4045, -3802, -3536, -3247, -2939, -2612, -2270, -1913, -1545, -1167, -782, -392
};

#ifdef OUTALAW
static unsigned char wavea[80];
typedef unsigned char output_t;
static const output_t *wave_out = wavea;    /* outgoing samples */
#define __OUT_FMT AST_FORMAT_ALAW;
#else
typedef signed short output_t;
static const output_t *wave_out = wave;     /* outgoing samples */
#define __OUT_FMT AST_FORMAT_SLINEAR
#endif

#define OSYNC_BITS	80                      /* initial sync bits */

/*!
 * The SMS spec ETSI ES 201 912 defines two protocols with different message types.
 * Also note that the high bit is used to indicate whether the message
 * is complete or not, but in two opposite ways:
 * for Protocol 1, 0x80 means that the message is complete;
 * for Protocol 2, 0x00 means that the message is complete;
 */
enum message_types {
	DLL_SMS_MASK        = 0x7f,             /* mask for the valid bits */

	/* Protocol 1 values */
	DLL1_SMS_DATA       = 0x11,             /* data packet */
	DLL1_SMS_ERROR      = 0x12,
	DLL1_SMS_EST        = 0x13,             /* start the connection */
	DLL1_SMS_REL        = 0x14,             /* end the connection */
	DLL1_SMS_ACK        = 0x15,
	DLL1_SMS_NACK       = 0x16,

	DLL1_SMS_COMPLETE   = 0x80,             /* packet is complete */
	DLL1_SMS_MORE       = 0x00,             /* more data to follow */

	/* Protocol 2 values */
	DLL2_SMS_EST        = 0x7f,             /* magic number. No message body */
	DLL2_SMS_INFO_MO    = 0x10,
	DLL2_SMS_INFO_MT    = 0x11,
	DLL2_SMS_INFO_STA   = 0x12,
	DLL2_SMS_NACK       = 0x13,
	DLL2_SMS_ACK0       = 0x14,             /* ack even-numbered frame */
	DLL2_SMS_ACK1       = 0x15,             /* ack odd-numbered frame */
	DLL2_SMS_ENQ        = 0x16,
	DLL2_SMS_REL        = 0x17,             /* end the connection */

	DLL2_SMS_COMPLETE   = 0x00,             /* packet is complete */
	DLL2_SMS_MORE       = 0x80,             /* more data to follow */
};

/* SMS 7 bit character mapping to UCS-2 */
static const unsigned short defaultalphabet[] = {
	0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC,
	0x00F2, 0x00E7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5,
	0x0394, 0x005F, 0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8,
	0x03A3, 0x0398, 0x039E, 0x00A0, 0x00C6, 0x00E6, 0x00DF, 0x00C9,
	' ', '!', '"', '#', 164, '%', '&', 39, '(', ')', '*', '+', ',', '-', '.', '/',
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<', '=', '>', '?',
	161, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 196, 214, 209, 220, 167,
	191, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
	'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 228, 246, 241, 252, 224,
};

static const unsigned short escapes[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x000C, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0x005E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0x007B, 0x007D, 0, 0, 0, 0, 0, 0x005C,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x005B, 0x007E, 0x005D, 0,
	0x007C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0x20AC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define SMSLEN      160          /*!< max SMS length */
#define SMSLEN_8    140          /*!< max SMS length for 8-bit char */

typedef struct sms_s {
	unsigned char hangup;        /*!< we are done... */
	unsigned char err;           /*!< set for any errors */
	unsigned char sent_rel:1;     /*!< have sent REL message... */
	unsigned char smsc:1;        /*!< we are SMSC */
	unsigned char rx:1;          /*!< this is a received message */
	char queue[30];              /*!< queue name */
	char oa[20];                 /*!< originating address */
	char da[20];                 /*!< destination address */
	struct timeval scts;         /*!< time stamp, UTC */
	unsigned char pid;           /*!< protocol ID */
	unsigned char dcs;           /*!< data coding scheme */
	short mr;                    /*!< message reference - actually a byte, but use -1 for not set */
	int udl;                     /*!< user data length */
	int udhl;                    /*!< user data header length */
	unsigned char srr:1;         /*!< Status Report request */
	unsigned char udhi:1;        /*!< User Data Header required, even if length 0 */
	unsigned char rp:1;          /*!< Reply Path */
	unsigned int vp;             /*!< validity period in minutes, 0 for not set */
	unsigned short ud[SMSLEN];   /*!< user data (message), UCS-2 coded */
	unsigned char udh[SMSLEN];   /*!< user data header */
	char cli[20];                /*!< caller ID */
	unsigned char ophase;        /*!< phase (0-79) for 0 and 1 frequencies (1300Hz and 2100Hz) */
	unsigned char ophasep;       /*!< phase (0-79) for 1200 bps */
	unsigned char obyte;         /*!< byte being sent */
	unsigned int opause;         /*!< silent pause before sending (in sample periods) */
	unsigned char obitp;         /*!< bit in byte */
	unsigned char osync;         /*!< sync bits to send */
	unsigned char obytep;        /*!< byte in data */
	unsigned char obyten;        /*!< bytes in data */
	unsigned char omsg[256];     /*!< data buffer (out) */
	unsigned char imsg[250];     /*!< data buffer (in) */
	signed long long ims0,
		imc0,
		ims1,
		imc1;                    /*!< magnitude averages sin/cos 0/1 */
	unsigned int idle;
	unsigned short imag;         /*!< signal level */
	unsigned char ips0;          /*!< phase sin for bit 0, start at  0 inc by 21 mod 80 */
	unsigned char ips1;          /*!< phase cos for bit 0, start at 20 inc by 21 mod 80 */
	unsigned char ipc0;          /*!< phase sin for bit 1, start at  0 inc by 13 mod 80 */
	unsigned char ipc1;          /*!< phase cos for bit 1, start at 20 inc by 13 mod 80 */
	unsigned char ibitl;         /*!< last bit */
	unsigned char ibitc;         /*!< bit run length count */
	unsigned char iphasep;       /*!< bit phase (0-79) for 1200 bps */
	unsigned char ibitn;         /*!< bit number in byte being received */
	unsigned char ibytev;        /*!< byte value being received */
	unsigned char ibytep;        /*!< byte pointer in message */
	unsigned char ibytec;        /*!< byte checksum for message */
	unsigned char ierr;          /*!< error flag */
	unsigned char ibith;         /*!< history of last bits */
	unsigned char ibitt;         /*!< total of 1's in last 3 bytes */
	/* more to go here */

	int opause_0;                /*!< initial delay in ms, p() option */
	int protocol;                /*!< ETSI SMS protocol to use (passed at app call) */
	int oseizure;                /*!< protocol 2: channel seizure bits to send */
	int framenumber;             /*!< protocol 2: frame number (for sending ACK0 or ACK1) */
	char udtxt[SMSLEN];          /*!< user data (message), PLAIN text */
} sms_t;

/* different types of encoding */
#define is7bit(dcs)  ( ((dcs) & 0xC0) ? (!((dcs) & 4) ) : (((dcs) & 0xc) == 0) )
#define is8bit(dcs)  ( ((dcs) & 0xC0) ? ( ((dcs) & 4) ) : (((dcs) & 0xc) == 4) )
#define is16bit(dcs) ( ((dcs) & 0xC0) ? 0               : (((dcs) & 0xc) == 8) )

static void sms_messagetx(sms_t *h);

/*! \brief copy number, skipping non digits apart from leading + */
static void numcpy(char *d, char *s)
{
	if (*s == '+') {
		*d++ = *s++;
	}
	while (*s) {
  		if (isdigit(*s)) {
			*d++ = *s;
		}
		s++;
	}
	*d = 0;
}

/*! \brief static, return a date/time in ISO format */
static char *isodate(time_t t, char *buf, int len)
{
	struct ast_tm tm;
	struct timeval local = { t, 0 };
	ast_localtime(&local, &tm, NULL);
	ast_strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm);
	return buf;
}

/*! \brief Reads next UCS character from NUL terminated UTF-8 string and advance pointer */
/* for non valid UTF-8 sequences, returns character as is */
/* Does not advance pointer for null termination */
static long utf8decode(unsigned char **pp)
{
	unsigned char *p = *pp;
	if (!*p) {
		return 0;                           /* null termination of string */
	}
	(*pp)++;
	if (*p < 0xC0) {
		return *p;                          /* ascii or continuation character */
	}
	if (*p < 0xE0) {
		if (*p < 0xC2 || (p[1] & 0xC0) != 0x80) {
			return *p;                      /* not valid UTF-8 */
		}
		(*pp)++;
		return ((*p & 0x1F) << 6) + (p[1] & 0x3F);
   	}
	if (*p < 0xF0) {
		if ((*p == 0xE0 && p[1] < 0xA0) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) {
			return *p;                      /* not valid UTF-8 */
		}
		(*pp) += 2;
		return ((*p & 0x0F) << 12) + ((p[1] & 0x3F) << 6) + (p[2] & 0x3F);
	}
	if (*p < 0xF8) {
		if ((*p == 0xF0 && p[1] < 0x90) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) {
			return *p;                      /* not valid UTF-8 */
		}
		(*pp) += 3;
		return ((*p & 0x07) << 18) + ((p[1] & 0x3F) << 12) + ((p[2] & 0x3F) << 6) + (p[3] & 0x3F);
	}
	if (*p < 0xFC) {
		if ((*p == 0xF8 && p[1] < 0x88) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80
			|| (p[4] & 0xC0) != 0x80) {
			return *p;                      /* not valid UTF-8 */
		}
		(*pp) += 4;
		return ((*p & 0x03) << 24) + ((p[1] & 0x3F) << 18) + ((p[2] & 0x3F) << 12) + ((p[3] & 0x3F) << 6) + (p[4] & 0x3F);
	}
	if (*p < 0xFE) {
		if ((*p == 0xFC && p[1] < 0x84) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80
			|| (p[4] & 0xC0) != 0x80 || (p[5] & 0xC0) != 0x80) {
			return *p;                      /* not valid UTF-8 */
		}
		(*pp) += 5;
		return ((*p & 0x01) << 30) + ((p[1] & 0x3F) << 24) + ((p[2] & 0x3F) << 18) + ((p[3] & 0x3F) << 12) + ((p[4] & 0x3F) << 6) + (p[5] & 0x3F);
	}
	return *p;                              /* not sensible */
}

/*! \brief takes a binary header (udhl bytes at udh) and UCS-2 message (udl characters at ud) and packs in to o using SMS 7 bit character codes */
/* The return value is the number of septets packed in to o, which is internally limited to SMSLEN */
/* o can be null, in which case this is used to validate or count only */
/* if the input contains invalid characters then the return value is -1 */
static int packsms7(unsigned char *o, int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char p = 0;                    /* output pointer (bytes) */
	unsigned char b = 0;                    /* bit position */
	unsigned char n = 0;                    /* output character count */
	unsigned char dummy[SMSLEN];

	if (o == NULL) {                        /* output to a dummy buffer if o not set */
		o = dummy;
	}

	if (udhl) {                             /* header */
		o[p++] = udhl;
		b = 1;
		n = 1;
		while (udhl--) {
			o[p++] = *udh++;
			b += 8;
			while (b >= 7) {
				b -= 7;
				n++;
			}
			if (n >= SMSLEN)
				return n;
		}
		if (b) {
			b = 7 - b;
			if (++n >= SMSLEN)
				return n;
		}                                   /* filling to septet boundary */
	}
	o[p] = 0;
	/* message */
	while (udl--) {
		long u;
		unsigned char v;
		u = *ud++;
		/* XXX 0 is invalid ? */
		/* look up in defaultalphabet[]. If found, v is the 7-bit code */
		for (v = 0; v < 128 && defaultalphabet[v] != u; v++);
		if (v == 128 /* not found */ && u && n + 1 < SMSLEN) {
			/* if not found, look in the escapes table (we need 2 bytes) */
			for (v = 0; v < 128 && escapes[v] != u; v++);
			if (v < 128) {	/* escaped sequence, esc + v */
				/* store the low (8-b) bits in o[p], the remaining bits in o[p+1] */
				o[p] |= (27 << b);          /* the low bits go into o[p] */ 
				b += 7;
				if (b >= 8) {
					b -= 8;
					p++;
					o[p] = (27 >> (7 - b));
				}
				n++;
			}
		}
		if (v == 128)
			return -1;                      /* invalid character */
		/* store, same as above */
		o[p] |= (v << b);
		b += 7;
		if (b >= 8) {
			b -= 8;
			p++;
			o[p] = (v >> (7 - b));
		}
		if (++n >= SMSLEN)
			return n;
	}
	return n;
}

/*! \brief takes a binary header (udhl bytes at udh) and UCS-2 message (udl characters at ud)
 * and packs in to o using 8 bit character codes.
 * The return value is the number of bytes packed in to o, which is internally limited to 140.
 * o can be null, in which case this is used to validate or count only.
 * if the input contains invalid characters then the return value is -1
 */
static int packsms8(unsigned char *o, int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char p = 0;
	unsigned char dummy[SMSLEN_8];

	if (o == NULL)
		o = dummy;
	/* header - no encoding */
	if (udhl) {
		o[p++] = udhl;
		while (udhl--) {
			o[p++] = *udh++;
			if (p >= SMSLEN_8) {
				return p;
			}
		}
	}
	while (udl--) {
		long u;
		u = *ud++;
		if (u < 0 || u > 0xFF) {
			return -1;                      /* not valid */
		}
		o[p++] = u;
		if (p >= SMSLEN_8) {
			return p;
		}
	}
	return p;
}

/*! \brief takes a binary header (udhl bytes at udh) and UCS-2 
	message (udl characters at ud) and packs in to o using 16 bit 
	UCS-2 character codes 
	The return value is the number of bytes packed in to o, which is 
	internally limited to 140 
	o can be null, in which case this is used to validate or count 
	only if the input contains invalid characters then 
	the return value is -1 */
static int packsms16(unsigned char *o, int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char p = 0;
	unsigned char dummy[SMSLEN_8];

	if (o == NULL) {
		o = dummy;
	}
	/* header - no encoding */
	if (udhl) {
		o[p++] = udhl;
		while (udhl--) {
			o[p++] = *udh++;
			if (p >= SMSLEN_8) {
				return p;
			}
		}
	}
	while (udl--) {
		long u;
		u = *ud++;
		o[p++] = (u >> 8);
		if (p >= SMSLEN_8) {
			return p - 1;                   /* could not fit last character */
		}
		o[p++] = u;
		if (p >= SMSLEN_8) {
			return p;
		}
	}
	return p;
}

/*! \brief general pack, with length and data, 
	returns number of bytes of target used */
static int packsms(unsigned char dcs, unsigned char *base, unsigned int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char *p = base;
	if (udl == 0) {
		*p++ = 0;                           /* no user data */
	} else {
		
		int l = 0;
		if (is7bit(dcs)) {                  /* 7 bit */
			if ((l = packsms7(p + 1, udhl, udh, udl, ud)) < 0) {
				l = 0;
			}
			*p++ = l;
			p += (l * 7 + 7) / 8;
		} else if (is8bit(dcs)) {           /* 8 bit */
			if ((l = packsms8(p + 1, udhl, udh, udl, ud)) < 0) {
				l = 0;
			}
			*p++ = l;
			p += l;
		} else {                            /* UCS-2 */
			if ((l = packsms16(p + 1, udhl, udh, udl, ud)) < 0) {
				l = 0;
			}
			*p++ = l;
			p += l;
		}
	}
	return p - base;
}


/*! \brief pack a date and return */
static void packdate(unsigned char *o, time_t w)
{
	struct ast_tm t;
	struct timeval topack = { w, 0 };
	int z;

	ast_localtime(&topack, &t, NULL);
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined( __NetBSD__ ) || defined(__APPLE__) || defined(__CYGWIN__)
	z = -t.tm_gmtoff / 60 / 15;
#else
	z = timezone / 60 / 15;
#endif
	*o++ = ((t.tm_year % 10) << 4) + (t.tm_year % 100) / 10;
	*o++ = (((t.tm_mon + 1) % 10) << 4) + (t.tm_mon + 1) / 10;
	*o++ = ((t.tm_mday % 10) << 4) + t.tm_mday / 10;
	*o++ = ((t.tm_hour % 10) << 4) + t.tm_hour / 10;
	*o++ = ((t.tm_min % 10) << 4) + t.tm_min / 10;
	*o++ = ((t.tm_sec % 10) << 4) + t.tm_sec / 10;
	if (z < 0) {
		*o++ = (((-z) % 10) << 4) + (-z) / 10 + 0x08;
	} else {
		*o++ = ((z % 10) << 4) + z / 10;
	}
}

/*! \brief unpack a date and return */
static struct timeval unpackdate(unsigned char *i)
{
	struct ast_tm t;

	t.tm_year = 100 + (i[0] & 0xF) * 10 + (i[0] >> 4);
	t.tm_mon = (i[1] & 0xF) * 10 + (i[1] >> 4) - 1;
	t.tm_mday = (i[2] & 0xF) * 10 + (i[2] >> 4);
	t.tm_hour = (i[3] & 0xF) * 10 + (i[3] >> 4);
	t.tm_min = (i[4] & 0xF) * 10 + (i[4] >> 4);
	t.tm_sec = (i[5] & 0xF) * 10 + (i[5] >> 4);
	t.tm_isdst = 0;
	if (i[6] & 0x08) {
		t.tm_min += 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
	} else {
		t.tm_min -= 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
	}

	return ast_mktime(&t, NULL);
}

/*! \brief unpacks bytes (7 bit encoding) at i, len l septets, 
	and places in udh and ud setting udhl and udl. udh not used 
	if udhi not set */
static void unpacksms7(unsigned char *i, unsigned char l, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
{
	unsigned char b = 0, p = 0;
	unsigned short *o = ud;
	*udhl = 0;
	if (udhi && l) {                        /* header */
		int h = i[p];
		*udhl = h;
		if (h) {
			b = 1;
			p++;
			l--;
			while (h-- && l) {
				*udh++ = i[p++];
				b += 8;
				while (b >= 7) {
					b -= 7;
					l--;
					if (!l) {
						break;
					}
				}
			}
			/* adjust for fill, septets */
			if (b) {
				b = 7 - b;
				l--;
			}
		}
	}
	while (l--) {
		unsigned char v;
		if (b < 2) {
			v = ((i[p] >> b) & 0x7F);       /* everything in one byte */
		} else {
			v = ((((i[p] >> b) + (i[p + 1] << (8 - b)))) & 0x7F);
		}
		b += 7;
		if (b >= 8) {
			b -= 8;
			p++;
		}
		/* 0x00A0 is the encoding of ESC (27) in defaultalphabet */
		if (o > ud && o[-1] == 0x00A0 && escapes[v]) {
			o[-1] = escapes[v];
		} else {
			*o++ = defaultalphabet[v];
		}
	}
	*udl = (o - ud);
}

/*! \brief unpacks bytes (8 bit encoding) at i, len l septets, 
 *  and places in udh and ud setting udhl and udl. udh not used 
 *  if udhi not set.
 */
static void unpacksms8(unsigned char *i, unsigned char l, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
{
	unsigned short *o = ud;
	*udhl = 0;
	if (udhi) {
		int n = *i;
		*udhl = n;
		if (n) {
			i++;
			l--;
			while (l && n) {
				l--;
				n--;
				*udh++ = *i++;
			}
		}
	}
	while (l--) {
		*o++ = *i++;                        /* not to UTF-8 as explicitly 8 bit coding in DCS */
	}
	*udl = (o - ud);
}

/*! \brief unpacks bytes (16 bit encoding) at i, len l septets,
	 and places in udh and ud setting udhl and udl. 
	udh not used if udhi not set */
static void unpacksms16(unsigned char *i, unsigned char l, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
{
	unsigned short *o = ud;
	*udhl = 0;
	if (udhi) {
		int n = *i;
		*udhl = n;
		if (n) {
			i++;
			l--;
			while (l && n) {
				l--;
				n--;
				*udh++ = *i++;
			}
		}
	}
	while (l--) {
		int v = *i++;
		if (l && l--) {
			v = (v << 8) + *i++;
		}
		*o++ = v;
	}
	*udl = (o - ud);
}

/*! \brief general unpack - starts with length byte (octet or septet) and returns number of bytes used, inc length */
static int unpacksms(unsigned char dcs, unsigned char *i, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
{
	int l = *i++;
	if (is7bit(dcs)) {
		unpacksms7(i, l, udh, udhl, ud, udl, udhi);
		l = (l * 7 + 7) / 8;                /* adjust length to return */
	} else if (is8bit(dcs)) {
		unpacksms8(i, l, udh, udhl, ud, udl, udhi);
	} else {
		l += l % 2;
		unpacksms16(i, l, udh, udhl, ud, udl, udhi);
	}
	return l + 1;
}

/*! \brief unpack an address from i, return byte length, unpack to o */
static unsigned char unpackaddress(char *o, unsigned char *i)
{
	unsigned char l = i[0], p;
	if (i[1] == 0x91) {
		*o++ = '+';
	}
	for (p = 0; p < l; p++) {
		if (p & 1) {
			*o++ = (i[2 + p / 2] >> 4) + '0';
		} else {
			*o++ = (i[2 + p / 2] & 0xF) + '0';
		}
	}
	*o = 0;
	return (l + 5) / 2;
}

/*! \brief store an address at o, and return number of bytes used */
static unsigned char packaddress(unsigned char *o, char *i)
{
	unsigned char p = 2;
	o[0] = 0;                               /* number of bytes */
	if (*i == '+') {                        /* record as bit 0 in byte 1 */
		i++;
		o[1] = 0x91;
	} else {
		o[1] = 0x81;
	}
	for ( ; *i ; i++) {
		if (!isdigit(*i)) {                 /* ignore non-digits */
			continue;
		}
		if (o[0] & 1) {
			o[p++] |= ((*i & 0xF) << 4);
		} else {
			o[p] = (*i & 0xF);
		}
		o[0]++;
	}
	if (o[0] & 1) {
		o[p++] |= 0xF0;                     /* pad */
	}
	return p;
}

/*! \brief Log the output, and remove file */
static void sms_log(sms_t * h, char status)
{
	int o;

	if (*h->oa == '\0' && *h->da == '\0') {
		return;
	}
	o = open(log_file, O_CREAT | O_APPEND | O_WRONLY, AST_FILE_MODE);
	if (o >= 0) {
		char line[1000], mrs[3] = "", *p;
		char buf[30];
		unsigned char n;

		if (h->mr >= 0) {
			snprintf(mrs, sizeof(mrs), "%02hhX", (unsigned char)h->mr);
		}
		snprintf(line, sizeof(line), "%s %c%c%c%s %s %s %s ",
			isodate(time(NULL), buf, sizeof(buf)),
			status, h->rx ? 'I' : 'O', h->smsc ? 'S' : 'M', mrs, h->queue,
			S_OR(h->oa, "-"), S_OR(h->da, "-") );
		p = line + strlen(line);
		for (n = 0; n < h->udl; n++) {
			if (h->ud[n] == '\\') {
				*p++ = '\\';
				*p++ = '\\';
			} else if (h->ud[n] == '\n') {
				*p++ = '\\';
				*p++ = 'n';
			} else if (h->ud[n] == '\r') {
				*p++ = '\\';
				*p++ = 'r';
			} else if (h->ud[n] < 32 || h->ud[n] == 127) {
				*p++ = 191;
			} else {
				*p++ = h->ud[n];
			}
		}
		*p++ = '\n';
		*p = 0;
		if (write(o, line, strlen(line)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
		close(o);
	}
	*h->oa = *h->da = h->udl = 0;
}

/*! \brief parse and delete a file */
static void sms_readfile(sms_t * h, char *fn)
{
	char line[1000];
	FILE *s;
	char dcsset = 0;                        /* if DSC set */
	ast_log(LOG_NOTICE, "Sending %s\n", fn);
	h->rx = h->udl = *h->oa = *h->da = h->pid = h->srr = h->udhi = h->rp = h->vp = h->udhl = 0;
	h->mr = -1;
	h->dcs = 0xF1;                          /* normal messages class 1 */
	h->scts = ast_tvnow();
	s = fopen(fn, "r");
	if (s) {
		if (unlink(fn)) {                   /* concurrent access, we lost */
			fclose(s);
			return;
		}
		while (fgets (line, sizeof(line), s)) {	/* process line in file */
			char *p;
			void *pp = &p;
			for (p = line; *p && *p != '\n' && *p != '\r'; p++);
			*p = 0;                         /* strip eoln */
			p = line;
			if (!*p || *p == ';') {
				continue;                   /* blank line or comment, ignore */
			}
			while (isalnum(*p)) {
				*p = tolower (*p);
				p++;
			}
			while (isspace (*p)) {
				*p++ = 0;
			}
			if (*p == '=') {
				*p++ = 0;
				if (!strcmp(line, "ud")) {  /* parse message (UTF-8) */
					unsigned char o = 0;
					memcpy(h->udtxt, p, SMSLEN); /* for protocol 2 */
					while (*p && o < SMSLEN) {
						h->ud[o++] = utf8decode(pp);
					}
					h->udl = o;
					if (*p) {
						ast_log(LOG_WARNING, "UD too long in %s\n", fn);
					}
				} else {
					while (isspace (*p)) {
						p++;
					}
					if (!strcmp(line, "oa") && strlen(p) < sizeof(h->oa)) {
						numcpy (h->oa, p);
					} else if (!strcmp(line, "da") && strlen(p) < sizeof(h->oa)) {
						numcpy (h->da, p);
					} else if (!strcmp(line, "pid")) {
						h->pid = atoi(p);
					} else if (!strcmp(line, "dcs")) {
						h->dcs = atoi(p);
						dcsset = 1;
					} else if (!strcmp(line, "mr")) {
						h->mr = atoi(p);
					} else if (!strcmp(line, "srr")) {
						h->srr = (atoi(p) ? 1 : 0);
					} else if (!strcmp(line, "vp")) {
						h->vp = atoi(p);
					} else if (!strcmp(line, "rp")) {
						h->rp = (atoi(p) ? 1 : 0);
					} else if (!strcmp(line, "scts")) {    /* get date/time */
						int Y, m, d, H, M, S;
						/* XXX Why aren't we using ast_strptime here? */
						if (sscanf(p, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &m, &d, &H, &M, &S) == 6) {
							struct ast_tm t = { 0, };
							t.tm_year = Y - 1900;
							t.tm_mon = m - 1;
							t.tm_mday = d;
							t.tm_hour = H;
							t.tm_min = M;
							t.tm_sec = S;
							t.tm_isdst = -1;
							h->scts = ast_mktime(&t, NULL);
							if (h->scts.tv_sec == 0) {
								ast_log(LOG_WARNING, "Bad date/timein %s: %s", fn, p);
							}
						}
					} else {
						ast_log(LOG_WARNING, "Cannot parse in %s: %s=%si\n", fn, line, p);
					}
				}
			} else if (*p == '#') {                   /* raw hex format */
				*p++ = 0;
				if (*p == '#') {
					p++;
					if (!strcmp(line, "ud")) {        /* user data */
						int o = 0;
						while (*p && o < SMSLEN) {
							if (isxdigit(*p) && isxdigit(p[1]) && isxdigit(p[2]) && isxdigit(p[3])) {
								h->ud[o++] =
									(((isalpha(*p) ? 9 : 0) + (*p & 0xF)) << 12) +
									(((isalpha(p[1]) ? 9 : 0) + (p[1] & 0xF)) << 8) +
									(((isalpha(p[2]) ? 9 : 0) + (p[2] & 0xF)) << 4) + ((isalpha(p[3]) ? 9 : 0) + (p[3] & 0xF));
								p += 4;
							} else
								break;
						}
						h->udl = o;
						if (*p)
							ast_log(LOG_WARNING, "UD too long / invalid UCS-2 hex in %s\n", fn);
					} else
						ast_log(LOG_WARNING, "Only ud can use ## format, %s\n", fn);
				} else if (!strcmp(line, "ud")) {       /* user data */
					int o = 0;
					while (*p && o < SMSLEN) {
						if (isxdigit(*p) && isxdigit(p[1])) {
							h->ud[o++] = (((isalpha(*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha(p[1]) ? 9 : 0) + (p[1] & 0xF));
							p += 2;
						} else {
							break;
						}
					}
					h->udl = o;
					if (*p) {
						ast_log(LOG_WARNING, "UD too long / invalid UCS-1 hex in %s\n", fn);
					}
				} else if (!strcmp(line, "udh")) {      /* user data header */
					unsigned char o = 0;
					h->udhi = 1;
					while (*p && o < SMSLEN) {
						if (isxdigit(*p) && isxdigit(p[1])) {
							h->udh[o] = (((isalpha(*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha(p[1]) ? 9 : 0) + (p[1] & 0xF));
							o++;
							p += 2;
						} else {
							break;
						}
					}
					h->udhl = o;
					if (*p) {
						ast_log(LOG_WARNING, "UDH too long / invalid hex in %s\n", fn);
					}
				} else {
					ast_log(LOG_WARNING, "Only ud and udh can use # format, %s\n", fn);
				}
			} else {
				ast_log(LOG_WARNING, "Cannot parse in %s: %s\n", fn, line);
			}
		}
		fclose(s);
		if (!dcsset && packsms7(0, h->udhl, h->udh, h->udl, h->ud) < 0) {
			if (packsms8(0, h->udhl, h->udh, h->udl, h->ud) < 0) {
				if (packsms16(0, h->udhl, h->udh, h->udl, h->ud) < 0) {
					ast_log(LOG_WARNING, "Invalid UTF-8 message even for UCS-2 (%s)\n", fn);
				} else {
					h->dcs = 0x08;          /* default to 16 bit */
					ast_log(LOG_WARNING, "Sending in 16 bit format(%s)\n", fn);
				}
			} else {
				h->dcs = 0xF5;              /* default to 8 bit */
				ast_log(LOG_WARNING, "Sending in 8 bit format(%s)\n", fn);
			}
		}
		if (is7bit(h->dcs) && packsms7(0, h->udhl, h->udh, h->udl, h->ud) < 0) {
			ast_log(LOG_WARNING, "Invalid 7 bit GSM data %s\n", fn);
		}
		if (is8bit(h->dcs) && packsms8(0, h->udhl, h->udh, h->udl, h->ud) < 0) {
			ast_log(LOG_WARNING, "Invalid 8 bit data %s\n", fn);
		}
		if (is16bit(h->dcs) && packsms16(0, h->udhl, h->udh, h->udl, h->ud) < 0) {
			ast_log(LOG_WARNING, "Invalid 16 bit data %s\n", fn);
		}
	}
}

/*! \brief white a received text message to a file */
static void sms_writefile(sms_t * h)
{
	char fn[200] = "", fn2[200] = "";
	char buf[30];
	FILE *o;

	if (ast_tvzero(h->scts)) {
		h->scts = ast_tvnow();
	}
	snprintf(fn, sizeof(fn), "%s/sms/%s", ast_config_AST_SPOOL_DIR, h->smsc ? h->rx ? "morx" : "mttx" : h->rx ? "mtrx" : "motx");
	ast_mkdir(fn, 0777);                    /* ensure it exists */
	ast_copy_string(fn2, fn, sizeof(fn2));
	snprintf(fn2 + strlen(fn2), sizeof(fn2) - strlen(fn2), "/%s.%s-%u", h->queue, isodate(h->scts.tv_sec, buf, sizeof(buf)), seq++);
	snprintf(fn + strlen(fn), sizeof(fn) - strlen(fn), "/.%s", fn2 + strlen(fn) + 1);
	if ((o = fopen(fn, "w")) == NULL) {
		return;
	}

	if (*h->oa) {
		fprintf(o, "oa=%s\n", h->oa);
	}
	if (*h->da) {
		fprintf(o, "da=%s\n", h->da);
	}
	if (h->udhi) {
		unsigned int p;
		fprintf(o, "udh#");
		for (p = 0; p < h->udhl; p++) {
			fprintf(o, "%02hhX", (unsigned char)h->udh[p]);
		}
		fprintf(o, "\n");
	}
	if (h->udl) {
		unsigned int p;
		for (p = 0; p < h->udl && h->ud[p] >= ' '; p++);
		if (p < h->udl) {
			fputc(';', o);                  /* cannot use ud=, but include as a comment for human readable */
		}
		fprintf(o, "ud=");
		for (p = 0; p < h->udl; p++) {
			unsigned short v = h->ud[p];
			if (v < 32) {
				fputc(191, o);
			} else if (v < 0x80) {
				fputc(v, o);
			} else if (v < 0x800) {
				fputc(0xC0 + (v >> 6), o);
				fputc(0x80 + (v & 0x3F), o);
			} else {
				fputc(0xE0 + (v >> 12), o);
				fputc(0x80 + ((v >> 6) & 0x3F), o);
				fputc(0x80 + (v & 0x3F), o);
			}
		}
		fprintf(o, "\n");
		for (p = 0; p < h->udl && h->ud[p] >= ' '; p++);
		if (p < h->udl) {
			for (p = 0; p < h->udl && h->ud[p] < 0x100; p++);
			if (p == h->udl) {              /* can write in ucs-1 hex */
				fprintf(o, "ud#");
				for (p = 0; p < h->udl; p++) {
					fprintf(o, "%02hhX", (unsigned char)h->ud[p]);
				}
				fprintf(o, "\n");
			} else {                        /* write in UCS-2 */
				fprintf(o, "ud##");
				for (p = 0; p < h->udl; p++) {
					fprintf(o, "%04X", (unsigned)h->ud[p]);
				}
				fprintf(o, "\n");
			}
		}
	}
	if (h->scts.tv_sec) {
		char datebuf[30];
		fprintf(o, "scts=%s\n", isodate(h->scts.tv_sec, datebuf, sizeof(datebuf)));
	}
	if (h->pid) {
		fprintf(o, "pid=%d\n", h->pid);
	}
	if (h->dcs != 0xF1) {
		fprintf(o, "dcs=%d\n", h->dcs);
	}
	if (h->vp) {
		fprintf(o, "vp=%u\n", h->vp);
	}
	if (h->srr) {
		fprintf(o, "srr=1\n");
	}
	if (h->mr >= 0) {
		fprintf(o, "mr=%d\n", h->mr);
	}
	if (h->rp) {
		fprintf(o, "rp=1\n");
	}
	fclose(o);
	if (rename(fn, fn2)) {
		unlink(fn);
	} else {
		ast_log(LOG_NOTICE, "Received to %s\n", fn2);
	}
}

/*! \brief read dir skipping dot files... */
static struct dirent *readdirqueue(DIR *d, char *queue)
{
	struct dirent *f;
	do {
		f = readdir(d);
	} while (f && (*f->d_name == '.' || strncmp(f->d_name, queue, strlen(queue)) || f->d_name[strlen(queue)] != '.'));
	return f;
}

/*! \brief handle the incoming message */
static unsigned char sms_handleincoming (sms_t * h)
{
	unsigned char p = 3;
	if (h->smsc) {                          /* SMSC */
		if ((h->imsg[2] & 3) == 1) {        /* SMS-SUBMIT */
			h->udhl = h->udl = 0;
			h->vp = 0;
			h->srr = ((h->imsg[2] & 0x20) ? 1 : 0);
			h->udhi = ((h->imsg[2] & 0x40) ? 1 : 0);
			h->rp = ((h->imsg[2] & 0x80) ? 1 : 0);
			ast_copy_string(h->oa, h->cli, sizeof(h->oa));
			h->scts = ast_tvnow();
			h->mr = h->imsg[p++];
			p += unpackaddress(h->da, h->imsg + p);
			h->pid = h->imsg[p++];
			h->dcs = h->imsg[p++];
			if ((h->imsg[2] & 0x18) == 0x10) {       /* relative VP */
				if (h->imsg[p] < 144) {
					h->vp = (h->imsg[p] + 1) * 5;
				} else if (h->imsg[p] < 168) {
					h->vp = 720 + (h->imsg[p] - 143) * 30;
				} else if (h->imsg[p] < 197) {
					h->vp = (h->imsg[p] - 166) * 1440;
				} else {
					h->vp = (h->imsg[p] - 192) * 10080;
				}
				p++;
			} else if (h->imsg[2] & 0x18) {
				p += 7;                     /* ignore enhanced / absolute VP */
			}
			p += unpacksms(h->dcs, h->imsg + p, h->udh, &h->udhl, h->ud, &h->udl, h->udhi);
			h->rx = 1;                      /* received message */
			sms_writefile(h);               /* write the file */
			if (p != h->imsg[1] + 2) {
				ast_log(LOG_WARNING, "Mismatch receive unpacking %d/%d\n", p, h->imsg[1] + 2);
				return 0xFF;		  /* duh! */
			}
		} else {
			ast_log(LOG_WARNING, "Unknown message type %02hhX\n", h->imsg[2]);
			return 0xFF;
		}
	} else {                                /* client */
		if (!(h->imsg[2] & 3)) {            /* SMS-DELIVER */
			*h->da = h->srr = h->rp = h->vp = h->udhi = h->udhl = h->udl = 0;
			h->srr = ((h->imsg[2] & 0x20) ? 1 : 0);
			h->udhi = ((h->imsg[2] & 0x40) ? 1 : 0);
			h->rp = ((h->imsg[2] & 0x80) ? 1 : 0);
			h->mr = -1;
			p += unpackaddress(h->oa, h->imsg + p);
			h->pid = h->imsg[p++];
			h->dcs = h->imsg[p++];
			h->scts = unpackdate(h->imsg + p);
			p += 7;
			p += unpacksms(h->dcs, h->imsg + p, h->udh, &h->udhl, h->ud, &h->udl, h->udhi);
			h->rx = 1;                      /* received message */
			sms_writefile(h);               /* write the file */
			if (p != h->imsg[1] + 2) {
				ast_log(LOG_WARNING, "Mismatch receive unpacking %d/%d\n", p, h->imsg[1] + 2);
				return 0xFF;                /* duh! */
			}
		} else {
			ast_log(LOG_WARNING, "Unknown message type %02hhX\n", h->imsg[2]);
			return 0xFF;
		}
	}
	return 0;                               /* no error */
}

#ifdef SOLARIS
#define NAME_MAX 1024
#endif

/*!
 * Add data to a protocol 2 message.
 * Use the length field (h->omsg[1]) as a pointer to the next free position.
 */
static void adddata_proto2(sms_t *h, unsigned char msg, char *data, int size)
{
	int x = h->omsg[1] + 2;                 /* Get current position */
	if (x == 2) {
		x += 2;                             /* First: skip Payload length (set later) */
	}
	h->omsg[x++] = msg;                     /* Message code */
	h->omsg[x++] = (unsigned char)size;     /* Data size Low */
	h->omsg[x++] = 0;                       /* Data size Hi */
	for (; size > 0 ; size--) {
		h->omsg[x++] = *data++;
	}
	h->omsg[1] = x - 2;                     /* Frame size */
	h->omsg[2] = x - 4;                     /* Payload length (Lo) */
	h->omsg[3] = 0;                         /* Payload length (Hi) */
}

static void putdummydata_proto2(sms_t *h)
{
	adddata_proto2(h, 0x10, "\0", 1);           /* Media Identifier > SMS */
	adddata_proto2(h, 0x11, "\0\0\0\0\0\0", 6); /* Firmware version */
	adddata_proto2(h, 0x12, "\2\0\4", 3);       /* SMS provider ID */
	adddata_proto2(h, 0x13, h->udtxt, h->udl);  /* Body */
}

static void sms_compose2(sms_t *h, int more)
{
	struct ast_tm tm;
	struct timeval now = h->scts;
	char stm[9];

	h->omsg[0] = 0x00;                      /* set later... */
	h->omsg[1] = 0;
	putdummydata_proto2(h);
	if (h->smsc) {                          /* deliver */
		h->omsg[0] = 0x11;                  /* SMS_DELIVERY */
		/* Required: 10 11 12 13 14 15 17 (seems they must be ordered!) */
		ast_localtime(&now, &tm, NULL);
		sprintf(stm, "%02d%02d%02d%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);  /* Date mmddHHMM */
		adddata_proto2(h, 0x14, stm, 8);    /* Date */
		if (*h->oa == 0) {
			strcpy(h->oa, "00000000");
		}
		adddata_proto2(h, 0x15, h->oa, strlen(h->oa)); /* Originator */
		adddata_proto2(h, 0x17, "\1", 1);   /* Calling Terminal ID */
	} else {                                /* submit */
		h->omsg[0] = 0x10;                  /* SMS_SUBMIT */
		/* Required: 10 11 12 13 17 18 1B 1C (seems they must be ordered!) */
		adddata_proto2(h, 0x17, "\1", 1);   /* Calling Terminal ID */
		if (*h->da == 0) {
			strcpy(h->da, "00000000");
		}
		adddata_proto2(h, 0x18, h->da, strlen(h->da)); /* Originator */
		adddata_proto2(h, 0x1B, "\1", 1);	      /* Called Terminal ID */
		adddata_proto2(h, 0x1C, "\0\0\0", 3);	  /* Notification */
	}
}

static void putdummydata_proto2(sms_t *h);

#define MAX_DEBUG_LEN	300
static char *sms_hexdump(unsigned char buf[], int size, char *s /* destination */)
{
	char *p;
	int f;

	for (p = s, f = 0; f < size && f < MAX_DEBUG_LEN; f++, p += 3) {
		sprintf(p, "%02hhX ", (unsigned char)buf[f]);
	}
	return(s);
}


/*! \brief sms_handleincoming_proto2: handle the incoming message */
static int sms_handleincoming_proto2(sms_t *h)
{
	int f, i, sz = 0;
	int msg, msgsz;
	struct ast_tm tm;
	struct timeval now = { 0, 0 };
	char debug_buf[MAX_DEBUG_LEN * 3 + 1];

	sz = h->imsg[1] + 2;
	/* ast_verb(3, "SMS-P2 Frame: %s\n", sms_hexdump(h->imsg, sz, debug_buf)); */

	/* Parse message body (called payload) */
	now = h->scts = ast_tvnow();
	for (f = 4; f < sz; ) {
		msg = h->imsg[f++];
		msgsz = h->imsg[f++];
		msgsz += (h->imsg[f++] * 256);
		switch (msg) {
		case 0x13:                          /* Body */
			ast_verb(3, "SMS-P2 Body#%02X=[%.*s]\n", (unsigned)msg, msgsz, &h->imsg[f]);
			if (msgsz >= sizeof(h->ud)) {
				msgsz = sizeof(h->ud) - 1;
			}
			for (i = 0; i < msgsz; i++) {
				h->ud[i] = h->imsg[f + i];
			}
			h->udl = msgsz;
			break;
		case 0x14:                          /* Date SCTS */
			now = h->scts = ast_tvnow();
			ast_localtime(&now, &tm, NULL);
			tm.tm_mon = ( (h->imsg[f] * 10) + h->imsg[f + 1] ) - 1;
			tm.tm_mday = ( (h->imsg[f + 2] * 10) + h->imsg[f + 3] );
			tm.tm_hour = ( (h->imsg[f + 4] * 10) + h->imsg[f + 5] );
			tm.tm_min = ( (h->imsg[f + 6] * 10) + h->imsg[f + 7] );
			tm.tm_sec = 0;
			h->scts = ast_mktime(&tm, NULL);
			ast_verb(3, "SMS-P2 Date#%02X=%02d/%02d %02d:%02d\n", (unsigned)msg, tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
			break;
		case 0x15:                          /* Calling line (from SMSC) */
			if (msgsz >= 20) {
				msgsz = 20 - 1;
			}
			ast_verb(3, "SMS-P2 Origin#%02X=[%.*s]\n", (unsigned)msg, msgsz, &h->imsg[f]);
			ast_copy_string(h->oa, (char *)(&h->imsg[f]), msgsz + 1);
			break;
		case 0x18:                          /* Destination(from TE/phone) */
			if (msgsz >= 20) {
				msgsz = 20 - 1;
			}
			ast_verb(3, "SMS-P2 Destination#%02X=[%.*s]\n", (unsigned)msg, msgsz, &h->imsg[f]);
			ast_copy_string(h->da, (char *)(&h->imsg[f]), msgsz + 1);
			break;
		case 0x1C:                          /* Notify */
			ast_verb(3, "SMS-P2 Notify#%02X=%s\n", (unsigned)msg, sms_hexdump(&h->imsg[f], 3, debug_buf));
			break;
		default:
			ast_verb(3, "SMS-P2 Par#%02X [%d]: %s\n", (unsigned)msg, msgsz, sms_hexdump(&h->imsg[f], msgsz, debug_buf));
			break;
		}
		f+=msgsz;                           /* Skip to next */
	}
	h->rx = 1;                              /* received message */
	sms_writefile(h);                       /* write the file */
	return 0;                               /* no error */
}

#if 0
static void smssend(sms_t *h, char *c)
{
	int f, x;
	for (f = 0; f < strlen(c); f++) {
		sscanf(&c[f*3], "%x", &x);
		h->omsg[f] = x;
	}
	sms_messagetx(h);
}
#endif

static void sms_nextoutgoing (sms_t *h);

static void sms_messagerx2(sms_t * h)
{
	int p = h->imsg[0] & DLL_SMS_MASK ; /* mask the high bit */
	int cause;

#define DLL2_ACK(h) ((h->framenumber & 1) ? DLL2_SMS_ACK1: DLL2_SMS_ACK1)
	switch (p) {
	case DLL2_SMS_EST:                      /* Protocol 2: Connection ready (fake): send message  */
		sms_nextoutgoing (h);
		/* smssend(h,"11 29 27 00 10 01 00 00 11 06 00 00 00 00 00 00 00 12 03 00 02 00 04 13 01 00 41 14 08 00 30 39 31 35 30 02 30 02 15 02 00 39 30 "); */
		break;

	case DLL2_SMS_INFO_MO:                  /* transport SMS_SUBMIT */
	case DLL2_SMS_INFO_MT:                  /* transport SMS_DELIVERY */
		cause = sms_handleincoming_proto2(h);
		if (!cause)	{                       /* ACK */
			sms_log(h, 'Y');
		}
		h->omsg[0] = DLL2_ACK(h);
		h->omsg[1] = 0x06;                  /* msg len */
		h->omsg[2] = 0x04;                  /* payload len */
		h->omsg[3] = 0x00;                  /* payload len */
		h->omsg[4] = 0x1f;                  /* Response type */
		h->omsg[5] = 0x01;                  /* parameter len */
		h->omsg[6] = 0x00;                  /* parameter len */
		h->omsg[7] = cause;                 /* CONFIRM or error */
		sms_messagetx(h);
		break;

	case DLL2_SMS_NACK:                     /* Protocol 2: SMS_NAK */
		h->omsg[0] = DLL2_SMS_REL;          /* SMS_REL */
		h->omsg[1] = 0x00;                  /* msg len */
		sms_messagetx(h);
		break;

	case DLL2_SMS_ACK0:
	case DLL2_SMS_ACK1:
		/* SMS_ACK also transport SMS_SUBMIT or SMS_DELIVERY */
		if ( (h->omsg[0] & DLL_SMS_MASK) == DLL2_SMS_REL) {
			/* a response to our Release, just hangup */
			h->hangup = 1;                  /* hangup */
		} else {
			/* XXX depending on what we are.. */
			ast_log(LOG_NOTICE, "SMS_SUBMIT or SMS_DELIVERY\n");
			sms_nextoutgoing (h);
		}
		break;

	case DLL2_SMS_REL:                      /* Protocol 2: SMS_REL (hangup req) */
		h->omsg[0] = DLL2_ACK(h);
		h->omsg[1] = 0;
		sms_messagetx(h);
		break;
	}
}

/*! \brief compose a message for protocol 1 */
static void sms_compose1(sms_t *h, int more)
{
	unsigned int p = 2;                     /* next byte to write. Skip type and len */

	h->omsg[0] = 0x91;                      /* SMS_DATA */
	if (h->smsc) {                          /* deliver */
		h->omsg[p++] = (more ? 4 : 0) + ((h->udhl > 0) ? 0x40 : 0);
		p += packaddress(h->omsg + p, h->oa);
		h->omsg[p++] = h->pid;
		h->omsg[p++] = h->dcs;
		packdate(h->omsg + p, h->scts.tv_sec);
		p += 7;
		p += packsms(h->dcs, h->omsg + p, h->udhl, h->udh, h->udl, h->ud);
	} else {                                /* submit */
		h->omsg[p++] =
			0x01 + (more ? 4 : 0) + (h->srr ? 0x20 : 0) + (h->rp ? 0x80 : 0) + (h->vp ? 0x10 : 0) + (h->udhi ? 0x40 : 0);
		if (h->mr < 0) {
			h->mr = message_ref++;
		}
		h->omsg[p++] = h->mr;
		p += packaddress(h->omsg + p, h->da);
		h->omsg[p++] = h->pid;
		h->omsg[p++] = h->dcs;
		if (h->vp) {                        /* relative VP */
			if (h->vp < 720) {
				h->omsg[p++] = (h->vp + 4) / 5 - 1;
			} else if (h->vp < 1440) {
				h->omsg[p++] = (h->vp - 720 + 29) / 30 + 143;
			} else if (h->vp < 43200) {
				h->omsg[p++] = (h->vp + 1439) / 1440 + 166;
			} else if (h->vp < 635040) {
				h->omsg[p++] = (h->vp + 10079) / 10080 + 192;
			} else {
				h->omsg[p++] = 255;         /* max */
			}
		}
		p += packsms(h->dcs, h->omsg + p, h->udhl, h->udh, h->udl, h->ud);
	}
	h->omsg[1] = p - 2;
}

/*! \brief find and fill in next message, or send a REL if none waiting */
static void sms_nextoutgoing (sms_t * h)
{	  
	char fn[100 + NAME_MAX] = "";
	DIR *d;
	char more = 0;

	*h->da = *h->oa = '\0';                 /* clear destinations */
	h->rx = 0;                              /* outgoing message */
	snprintf(fn, sizeof(fn), "%s/sms/%s", ast_config_AST_SPOOL_DIR, h->smsc ? "mttx" : "motx");
	ast_mkdir(fn, 0777);                    /* ensure it exists */
	d = opendir(fn);
	if (d) {
		struct dirent *f = readdirqueue(d, h->queue);
		if (f) {
			snprintf(fn + strlen(fn), sizeof(fn) - strlen(fn), "/%s", f->d_name);
			sms_readfile(h, fn);
			if (readdirqueue(d, h->queue)) {
				more = 1;                   /* more to send */
			}
		}
		closedir(d);
	}
	if (*h->da || *h->oa) {                 /* message to send */
		if (h->protocol == 2) {
			sms_compose2(h, more);
		} else {
			sms_compose1(h, more);
		}
	} else {                                /* no message */
		if (h->protocol == 2) {
			h->omsg[0] = 0x17;              /* SMS_REL */
			h->omsg[1] = 0;
		} else {
			h->omsg[0] = 0x94;              /* SMS_REL */
			h->omsg[1] = 0;
			h->sent_rel = 1;
		}
	}
	sms_messagetx(h);
}

#define DIR_RX 1
#define DIR_TX 2
static void sms_debug (int dir, sms_t *h)
{
	char txt[259 * 3 + 1];
	char *p = txt;                          /* always long enough */
	unsigned char *msg = (dir == DIR_RX) ? h->imsg : h->omsg;
	int n = (dir == DIR_RX) ? h->ibytep : msg[1] + 2;
	int q = 0;
	while (q < n && q < 30) {
		sprintf(p, " %02hhX", msg[q++]);
		p += 3;
	}
	if (q < n) {
		sprintf(p, "...");
	}
	ast_verb(3, "SMS %s%s\n", dir == DIR_RX ? "RX" : "TX", txt);
}


static void sms_messagerx(sms_t * h)
{
	int cause;

	sms_debug (DIR_RX, h);
	if (h->protocol == 2) {
		sms_messagerx2(h);
		return;
	}
	/* parse incoming message for Protocol 1 */
	switch (h->imsg[0]) {
	case 0x91:                              /* SMS_DATA */
		cause = sms_handleincoming (h);
		if (!cause) {
			sms_log(h, 'Y');
			h->omsg[0] = 0x95;              /* SMS_ACK */
			h->omsg[1] = 0x02;
			h->omsg[2] = 0x00;              /* deliver report */
			h->omsg[3] = 0x00;              /* no parameters */
		} else {                            /* NACK */
			sms_log(h, 'N');
			h->omsg[0] = 0x96;              /* SMS_NACK */
			h->omsg[1] = 3;
			h->omsg[2] = 0;                 /* delivery report */
			h->omsg[3] = cause;             /* cause */
			h->omsg[4] = 0;                 /* no parameters */
		}
		sms_messagetx(h);
		break;

	case 0x92:                              /* SMS_ERROR */
		h->err = 1;
		sms_messagetx(h);                   /* send whatever we sent again */
		break;
	case 0x93:                              /* SMS_EST */
		sms_nextoutgoing (h);
		break;
	case 0x94:                              /* SMS_REL */
		h->hangup = 1;                      /* hangup */
		break;
	case 0x95:                              /* SMS_ACK */
		sms_log(h, 'Y');
		sms_nextoutgoing (h);
		break;
	case 0x96:                              /* SMS_NACK */
		h->err = 1;
		sms_log(h, 'N');
		sms_nextoutgoing (h);
		break;
	default:                                /* Unknown */
		h->omsg[0] = 0x92;                  /* SMS_ERROR */
		h->omsg[1] = 1;
		h->omsg[2] = 3;                     /* unknown message type */
		sms_messagetx(h);
		break;
	}
}

static void sms_messagetx(sms_t * h)
{
	unsigned char c = 0, p;
	int len = h->omsg[1] + 2;               /* total message length excluding checksum */

	for (p = 0; p < len; p++) {             /* compute checksum */
		c += h->omsg[p];
	}
	h->omsg[len] = 0 - c;                   /* actually, (256 - (c & 0fxx)) & 0xff) */
	sms_debug(DIR_TX, h);
	h->framenumber++;                       /* Proto 2 */
	h->obytep = 0;
	h->obitp = 0;
	if (h->protocol == 2) {                 /* Proto 2: */
		h->oseizure = 300;                  /* 300bits (or more ?) */
		h->obyte = 0;                       /* Seizure starts with  space (0) */
		if (h->omsg[0] == 0x7F) {
			h->opause = 8 * h->opause_0;    /* initial message delay */
		} else {
			h->opause = 400;
		}
	} else {                                /* Proto 1: */
		h->oseizure = 0;                    /* No seizure */
		h->obyte = 1;                       /* send mark ('1') at the beginning */
		/* Change the initial message delay. BT requires 300ms,
		 * but for others this might be way too much and the phone
		 * could time out. XXX make it configurable.
	 	*/
		if (h->omsg[0] == 0x93) {
			h->opause = 8 * h->opause_0;    /* initial message delay */
		} else {
			h->opause = 200;
		}
	}
	/* Note - setting osync triggers the generator */
	h->osync = OSYNC_BITS;                  /* 80 sync bits */
	h->obyten = len + 1;                    /* bytes to send (including checksum) */
}

/*!
 * outgoing data are produced by this generator function, that reads from
 * the descriptor whether it has data to send and which ones.
 */
static int sms_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct ast_frame f = { 0 };
#define MAXSAMPLES (800)
	output_t *buf;
	sms_t *h = data;
	int i;

	if (samples > MAXSAMPLES) {
		ast_log(LOG_WARNING, "Only doing %d samples (%d requested)\n",
			 MAXSAMPLES, samples);
		samples = MAXSAMPLES;
	}
	len = samples * sizeof(*buf) + AST_FRIENDLY_OFFSET;
	buf = ast_alloca(len);

	f.frametype = AST_FRAME_VOICE;
	ast_format_set(&f.subclass.format, __OUT_FMT, 0);
	f.datalen = samples * sizeof(*buf);
	f.offset = AST_FRIENDLY_OFFSET;
	f.mallocd = 0;
	f.data.ptr = buf;
	f.samples = samples;
	f.src = "app_sms";
	/* create a buffer containing the digital sms pattern */
	for (i = 0; i < samples; i++) {
		buf[i] = wave_out[0];               /* default is silence */

		if (h->opause) {
			h->opause--;
		} else if (h->obyten || h->osync) { /* sending data */
			buf[i] = wave_out[h->ophase];
			h->ophase += (h->obyte & 1) ? 13 : 21; /* compute next phase */
			if (h->ophase >= 80)
				h->ophase -= 80;
			if ((h->ophasep += 12) >= 80) { /* time to send the next bit */
				h->ophasep -= 80;
				if (h->oseizure > 0) {      /* sending channel seizure (proto 2) */
					h->oseizure--;
					h->obyte ^= 1;          /* toggle low bit */
				} else if (h->osync) {
					h->obyte = 1;           /* send mark as sync bit */
					h->osync--;             /* sending sync bits */
					if (h->osync == 0 && h->protocol == 2 && h->omsg[0] == DLL2_SMS_EST) {
						h->obytep = h->obyten = 0; /* we are done */
					}
				} else {
					h->obitp++;
					if (h->obitp == 1) {
						h->obyte = 0;       /* start bit; */
					} else if (h->obitp == 2) {
						h->obyte = h->omsg[h->obytep];
					} else if (h->obitp == 10) {
						h->obyte = 1; /* stop bit */
						h->obitp = 0;
						h->obytep++;
						if (h->obytep == h->obyten) {
							h->obytep = h->obyten = 0; /* sent */
							h->osync = 10;	  /* trailing marks */
						}
					} else {
						h->obyte >>= 1;
					}
				}
			}
		}
	}
	if (ast_write(chan, &f) < 0) {
		ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
#undef MAXSAMPLES
}

/*!
 * Just return the pointer to the descriptor that we received.
 */
static void *sms_alloc(struct ast_channel *chan, void *sms_t_ptr)
{
	return sms_t_ptr;
}

static void sms_release(struct ast_channel *chan, void *data)
{
	return;	/* nothing to do here. */
}

static struct ast_generator smsgen = {
	.alloc = sms_alloc,
	.release = sms_release,
	.generate = sms_generate,
};

/*!
 * Process an incoming frame, trying to detect the carrier and
 * decode the message. The two frequencies are 1300 and 2100 Hz.
 * The decoder detects the amplitude of the signal over the last
 * few samples, filtering the absolute values with a lowpass filter.
 * If the magnitude (h->imag) is large enough, multiply the signal
 * by the two carriers, and compute the amplitudes m0 and m1.
 * Record the current sample as '0' or '1' depending on which one is greater.
 * The last 3 bits are stored in h->ibith, with the count of '1'
 * bits in h->ibitt.
 * XXX the rest is to be determined.
 */
static void sms_process(sms_t * h, int samples, signed short *data)
{
	int bit;

	/*
	 * Ignore incoming audio while a packet is being transmitted,
	 * the protocol is half-duplex.
	 * Unfortunately this means that if the outbound and incoming
	 * transmission overlap (which is an error condition anyways),
	 * we may miss some data and this makes debugging harder.
	 */
	if (h->obyten || h->osync) {
		return;
	}
	for ( ; samples-- ; data++) {
		unsigned long long m0, m1;
		if (abs(*data) > h->imag) {
			h->imag = abs(*data);
		} else {
			h->imag = h->imag * 7 / 8;
		}
		if (h->imag <= 500) {               /* below [arbitrary] threahold: lost carrier */
			if (h->idle++ == 80000) {       /* nothing happening */
				ast_log(LOG_NOTICE, "No data, hanging up\n");
				h->hangup = 1;
				h->err = 1;
			}
			if (h->ierr) {                  /* error */
				ast_log(LOG_NOTICE, "Error %d, hanging up\n", h->ierr);
				/* Protocol 1 */
				h->err = 1;
				h->omsg[0] = 0x92;          /* error */
				h->omsg[1] = 1;
				h->omsg[2] = h->ierr;
				sms_messagetx(h);           /* send error */
			}
			h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
			continue;
		}
		h->idle = 0;

		/* multiply signal by the two carriers. */
		h->ims0 = (h->ims0 * 6 + *data * wave[h->ips0]) / 7;
		h->imc0 = (h->imc0 * 6 + *data * wave[h->ipc0]) / 7;
		h->ims1 = (h->ims1 * 6 + *data * wave[h->ips1]) / 7;
		h->imc1 = (h->imc1 * 6 + *data * wave[h->ipc1]) / 7;
		/* compute the amplitudes */
		m0 = h->ims0 * h->ims0 + h->imc0 * h->imc0;
		m1 = h->ims1 * h->ims1 + h->imc1 * h->imc1;

		/* advance the sin/cos pointers */
		if ((h->ips0 += 21) >= 80) {
			h->ips0 -= 80;
		}
		if ((h->ipc0 += 21) >= 80) {
			h->ipc0 -= 80;
		}
		if ((h->ips1 += 13) >= 80) {
			h->ips1 -= 80;
		}
		if ((h->ipc1 += 13) >= 80) {
			h->ipc1 -= 80;
		}

		/* set new bit to 1 or 0 depending on which value is stronger */
		h->ibith <<= 1;
		if (m1 > m0) {
			h->ibith |= 1;
		}
		if (h->ibith & 8) {
			h->ibitt--;
		}
		if (h->ibith & 1) {
			h->ibitt++;
		}
		bit = ((h->ibitt > 1) ? 1 : 0);
		if (bit != h->ibitl) {
			h->ibitc = 1;
		} else {
			h->ibitc++;
		}
		h->ibitl = bit;
		if (!h->ibitn && h->ibitc == 4 && !bit) {
			h->ibitn = 1;
			h->iphasep = 0;
		}
		if (bit && h->ibitc == 200) {       /* sync, restart message */
			/* Protocol 2: empty connection ready (I am master) */
			if (h->framenumber < 0 && h->ibytec >= 160 && !memcmp(h->imsg, "UUUUUUUUUUUUUUUUUUUU", 20)) {
				h->framenumber = 1;
				ast_verb(3, "SMS protocol 2 detected\n");
				h->protocol = 2;
				h->imsg[0] = 0xff;          /* special message (fake) */
				h->imsg[1] = h->imsg[2] = 0x00;
				h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
				sms_messagerx(h);
			}
			h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
		}
		if (h->ibitn) {
			h->iphasep += 12;
			if (h->iphasep >= 80) {         /* next bit */
				h->iphasep -= 80;
				if (h->ibitn++ == 9) {      /* end of byte */
					if (!bit) {             /* bad stop bit */
						if (h->sent_rel) {
							h->hangup = 1;
						} else {
							ast_log(LOG_NOTICE, "Bad stop bit\n");
							h->ierr = 0xFF;     /* unknown error */
						}
					} else {
						if (h->ibytep < sizeof(h->imsg)) {
							h->imsg[h->ibytep] = h->ibytev;
							h->ibytec += h->ibytev;
							h->ibytep++;
						} else if (h->ibytep == sizeof(h->imsg)) {
							ast_log(LOG_NOTICE, "msg too large\n");
							h->ierr = 2;    /* bad message length */
						}
						if (h->ibytep > 1 && h->ibytep == 3 + h->imsg[1] && !h->ierr) {
							if (!h->ibytec) {
								sms_messagerx(h);
							} else {
								ast_log(LOG_NOTICE, "bad checksum\n");
								h->ierr = 1; /* bad checksum */
							}
						}
					}
					h->ibitn = 0;
				}
				h->ibytev = (h->ibytev >> 1) + (bit ? 0x80 : 0);
			}
		}
	}
}

/*
 * Standard argument parsing:
 *	- one enum for the flags we recognise,
 *	- one enum for argument indexes
 *	- AST_APP_OPTIONS() to drive the parsing routine
 *	- in the function, AST_DECLARE_APP_ARGS(...) for the arguments.
 */
enum sms_flags {
	OPTION_BE_SMSC	= (1 << 0),             /* act as sms center */
	OPTION_ANSWER	= (1 << 1),             /* answer on incoming calls */
	OPTION_TWO	= (1 << 2),                 /* Use Protocol Two */
	OPTION_PAUSE	= (1 << 3),             /* pause before sending data, in ms */
	OPTION_SRR	= (1 << 4),                 /* set srr */
	OPTION_DCS	= (1 << 5),                 /* set dcs */
};

enum sms_opt_args {
	OPTION_ARG_PAUSE = 0,
	OPTION_ARG_ARRAY_SIZE
};

AST_APP_OPTIONS(sms_options, {
	AST_APP_OPTION('s', OPTION_BE_SMSC),
	AST_APP_OPTION('a', OPTION_ANSWER),
	AST_APP_OPTION('t', OPTION_TWO),
	AST_APP_OPTION('r', OPTION_SRR),
	AST_APP_OPTION('o', OPTION_DCS),
	AST_APP_OPTION_ARG('p', OPTION_PAUSE, OPTION_ARG_PAUSE),
	} );

static int sms_exec(struct ast_channel *chan, const char *data)
{
	int res = -1;
	sms_t h = { 0 };
	/* argument parsing support */
	struct ast_flags flags = { 0 };
	char *parse, *sms_opts[OPTION_ARG_ARRAY_SIZE] = { 0, };
	char *p;
	AST_DECLARE_APP_ARGS(sms_args,
		AST_APP_ARG(queue);
		AST_APP_ARG(options);
		AST_APP_ARG(addr);
		AST_APP_ARG(body);
	);

	if (!data) {
		ast_log(LOG_ERROR, "Requires queue name at least\n");
		return -1;
	}

	parse = ast_strdupa(data);              /* create a local copy */
	AST_STANDARD_APP_ARGS(sms_args, parse);
	if (sms_args.argc > 1) {
		ast_app_parse_options(sms_options, &flags, sms_opts, sms_args.options);
	}

	ast_verb(1, "sms argc %u queue <%s> opts <%s> addr <%s> body <%s>\n",
		sms_args.argc, S_OR(sms_args.queue, ""),
		S_OR(sms_args.options, ""),
		S_OR(sms_args.addr, ""),
		S_OR(sms_args.body, "") );

	h.ipc0 = h.ipc1 = 20;                   /* phase for cosine */
	h.dcs = 0xF1;                           /* default */

	ast_copy_string(h.cli,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""),
		sizeof(h.cli));

	if (ast_strlen_zero(sms_args.queue)) {
		ast_log(LOG_ERROR, "Requires queue name\n");
		goto done;
	}
	if (strlen(sms_args.queue) >= sizeof(h.queue)) {
		ast_log(LOG_ERROR, "Queue name too long\n");
		goto done;
	}
	ast_copy_string(h.queue, sms_args.queue, sizeof(h.queue));

	for (p = h.queue; *p; p++) {
		if (!isalnum(*p)) {
			*p = '-';                       /* make very safe for filenames */
		}
	}

	h.smsc = ast_test_flag(&flags, OPTION_BE_SMSC);
	h.protocol = ast_test_flag(&flags, OPTION_TWO) ? 2 : 1;
	if (!ast_strlen_zero(sms_opts[OPTION_ARG_PAUSE])) {
		h.opause_0 = atoi(sms_opts[OPTION_ARG_PAUSE]);
	}
	if (h.opause_0 < 25 || h.opause_0 > 2000) {
		h.opause_0 = 300;                   /* default 300ms */
	}
	ast_verb(1, "initial delay %dms\n", h.opause_0);


	/* the following apply if there is an arg3/4 and apply to the created message file */
	if (ast_test_flag(&flags, OPTION_SRR)) {
		h.srr = 1;
	}
	if (ast_test_flag(&flags, OPTION_DCS)) {
		h.dcs = 1;
	}
#if 0	
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':                           /* set the pid for saved local message */
			h.pid = 0x40 + (*d & 0xF);
			break;
		}
#endif
	if (sms_args.argc > 2) {
		unsigned char *up;

		/* submitting a message, not taking call. */
		/* deprecated, use smsq instead */
		h.scts = ast_tvnow();
		if (ast_strlen_zero(sms_args.addr) || strlen(sms_args.addr) >= sizeof(h.oa)) {
			ast_log(LOG_ERROR, "Address too long %s\n", sms_args.addr);
			goto done;
		}
		if (h.smsc) {
			ast_copy_string(h.oa, sms_args.addr, sizeof(h.oa));
		} else {
			ast_copy_string(h.da, sms_args.addr, sizeof(h.da));
			ast_copy_string(h.oa, h.cli, sizeof(h.oa));
		}
		h.udl = 0;
		if (ast_strlen_zero(sms_args.body)) {
			ast_log(LOG_ERROR, "Missing body for %s\n", sms_args.addr);
			goto done;
		}
		up = (unsigned char *)sms_args.body;
		while (*up && h.udl < SMSLEN) {
			h.ud[h.udl++] = utf8decode(&up);
		}
		if (is7bit(h.dcs) && packsms7(0, h.udhl, h.udh, h.udl, h.ud) < 0) {
			ast_log(LOG_WARNING, "Invalid 7 bit GSM data\n");
			goto done;
		}
		if (is8bit(h.dcs) && packsms8(0, h.udhl, h.udh, h.udl, h.ud) < 0) {
			ast_log(LOG_WARNING, "Invalid 8 bit data\n");
			goto done;
		}
		if (is16bit(h.dcs) && packsms16(0, h.udhl, h.udh, h.udl, h.ud) < 0) {
			ast_log(LOG_WARNING, "Invalid 16 bit data\n");
			goto done;
		}
		h.rx = 0;                           /* sent message */
		h.mr = -1;
		sms_writefile(&h);
		res = h.err;
		goto done;
	}
	
	if (ast_channel_state(chan) != AST_STATE_UP) {		/* make sure channel is answered before any TX */
		ast_answer(chan);
	}

	if (ast_test_flag(&flags, OPTION_ANSWER)) {
		h.framenumber = 1;                  /* Proto 2 */
		/* set up SMS_EST initial message */
		if (h.protocol == 2) {
			h.omsg[0] = DLL2_SMS_EST;
			h.omsg[1] = 0;
		} else {
			h.omsg[0] = DLL1_SMS_EST | DLL1_SMS_COMPLETE;
			h.omsg[1] = 0;
		}
		sms_messagetx(&h);
	}

	res = ast_set_write_format_by_id(chan, __OUT_FMT);
	if (res >= 0) {
		res = ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR);
	}
	if (res < 0) {
		ast_log(LOG_ERROR, "Unable to set to linear mode, giving up\n");
		goto done;
	}

	if ( (res = ast_activate_generator(chan, &smsgen, &h)) < 0) {
		ast_log(LOG_ERROR, "Failed to activate generator on '%s'\n", ast_channel_name(chan));
		goto done;
	}

	/* Do our thing here */
	for (;;) {
		struct ast_frame *f;
		int i = ast_waitfor(chan, -1);
		if (i < 0) {
			ast_log(LOG_NOTICE, "waitfor failed\n");
			break;
		}
		if (h.hangup) {
			ast_log(LOG_NOTICE, "channel hangup\n");
			break;
		}
		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_NOTICE, "ast_read failed\n");
			break;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			sms_process(&h, f->samples, f->data.ptr);
		}

		ast_frfree(f);
	}
	res = h.err;                            /* XXX */

	/* 
	 * The SMS generator data is on the stack.  We _MUST_ make sure the generator
	 * is stopped before returning from this function.
	 */
	ast_deactivate_generator(chan);

	sms_log(&h, '?');                       /* log incomplete message */
done:
	return (res);
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
#ifdef OUTALAW
	int p;
	for (p = 0; p < 80; p++) {
		wavea[p] = AST_LIN2A(wave[p]);
	}
#endif
	snprintf(log_file, sizeof(log_file), "%s/sms", ast_config_AST_LOG_DIR);
	return ast_register_application_xml(app, sms_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SMS/PSTN handler");
