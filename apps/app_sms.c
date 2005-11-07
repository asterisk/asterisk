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
 * \brief SMS application - ETSI ES 201 912 protocol 1 implimentation
 * \ingroup applications
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"

/* output using Alaw rather than linear */
/* #define OUTALAW */

/* ToDo */
/* Add full VP support */
/* Handle status report messages (generation and reception) */
/* Time zones on time stamps */
/* user ref field */

static volatile unsigned char message_ref;      /* arbitary message ref */
static volatile unsigned int seq;       /* arbitrary message sequence number for unqiue files */

static char log_file[255];
static char spool_dir[255];

static char *tdesc = "SMS/PSTN handler";

static char *app = "SMS";

static char *synopsis = "Communicates with SMS service centres and SMS capable analogue phones";

static char *descrip =
	"  SMS(name|[a][s]):  SMS handles exchange of SMS data with a call to/from SMS capabale\n"
	"phone or SMS PSTN service center. Can send and/or receive SMS messages.\n"
	"Works to ETSI ES 201 912 compatible with BT SMS PSTN service in UK\n"
	"Typical usage is to use to handle called from the SMS service centre CLI,\n"
	"or to set up a call using 'outgoing' or manager interface to connect\n"
	"service centre to SMS()\n"
	"name is the name of the queue used in /var/spool/asterisk/sms\n"
	"Arguments:\n"
	" a: answer, i.e. send initial FSK packet.\n"
	" s: act as service centre talking to a phone.\n"
	"Messages are processed as per text file message queues.\n" 
	"smsq (a separate software) is a command to generate message\n"
	"queues and send messages.\n";

static signed short wave[] = {
	0, 392, 782, 1167, 1545, 1913, 2270, 2612, 2939, 3247, 3536, 3802, 4045, 4263, 4455, 4619, 4755, 4862, 4938, 4985,
	5000, 4985, 4938, 4862, 4755, 4619, 4455, 4263, 4045, 3802, 3536, 3247, 2939, 2612, 2270, 1913, 1545, 1167, 782, 392,
	0, -392, -782, -1167,
	 -1545, -1913, -2270, -2612, -2939, -3247, -3536, -3802, -4045, -4263, -4455, -4619, -4755, -4862, -4938, -4985, -5000,
	-4985, -4938, -4862,
	-4755, -4619, -4455, -4263, -4045, -3802, -3536, -3247, -2939, -2612, -2270, -1913, -1545, -1167, -782, -392
};

#ifdef OUTALAW
static unsigned char wavea[80];
#endif

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

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

#define SMSLEN 160              /* max SMS length */

typedef struct sms_s
{
	unsigned char hangup;        /* we are done... */
	unsigned char err;           /* set for any errors */
	unsigned char smsc:1;        /* we are SMSC */
	unsigned char rx:1;          /* this is a received message */
	char queue[30];              /* queue name */
	char oa[20];                 /* originating address */
	char da[20];                 /* destination address */
	time_t scts;                 /* time stamp, UTC */
	unsigned char pid;           /* protocol ID */
	unsigned char dcs;           /* data coding scheme */
	short mr;                    /* message reference - actually a byte, but usde -1 for not set */
	int udl;                     /* user data length */
	int udhl;                    /* user data header length */
	unsigned char srr:1;         /* Status Report request */
	unsigned char udhi:1;        /* User Data Header required, even if length 0 */
	unsigned char rp:1;          /* Reply Path */
	unsigned int vp;             /* validity period in minutes, 0 for not set */
	unsigned short ud[SMSLEN];   /* user data (message), UCS-2 coded */
	unsigned char udh[SMSLEN];   /* user data header */
	char cli[20];                /* caller ID */
	unsigned char ophase;        /* phase (0-79) for 0 and 1 frequencies (1300Hz and 2100Hz) */
	unsigned char ophasep;       /* phase (0-79) for 1200 bps */
	unsigned char obyte;         /* byte being sent */
	unsigned int opause;         /* silent pause before sending (in sample periods) */
	unsigned char obitp;         /* bit in byte */
	unsigned char osync;         /* sync bits to send */
	unsigned char obytep;        /* byte in data */
	unsigned char obyten;        /* bytes in data */
	unsigned char omsg[256];     /* data buffer (out) */
	unsigned char imsg[200];     /* data buffer (in) */
	signed long long ims0,
		imc0,
		ims1,
		imc1;                      /* magnitude averages sin/cos 0/1 */
	unsigned int idle;
	unsigned short imag;         /* signal level */
	unsigned char ips0,
		ips1,
		ipc0,
		ipc1;                      /* phase sin/cos 0/1 */
	unsigned char ibitl;         /* last bit */
	unsigned char ibitc;         /* bit run length count */
	unsigned char iphasep;       /* bit phase (0-79) for 1200 bps */
	unsigned char ibitn;         /* bit number in byte being received */
	unsigned char ibytev;        /* byte value being received */
	unsigned char ibytep;        /* byte pointer in messafe */
	unsigned char ibytec;        /* byte checksum for message */
	unsigned char ierr;          /* error flag */
	unsigned char ibith;         /* history of last bits */
	unsigned char ibitt;         /* total of 1's in last 3 bites */
	/* more to go here */
} sms_t;

/* different types of encoding */
#define is7bit(dcs) (((dcs)&0xC0)?(!((dcs)&4)):(!((dcs)&12)))
#define is8bit(dcs) (((dcs)&0xC0)?(((dcs)&4)):(((dcs)&12)==4))
#define is16bit(dcs) (((dcs)&0xC0)?0:(((dcs)&12)==8))

static void *sms_alloc (struct ast_channel *chan, void *params)
{
	return params;
}

static void sms_release (struct ast_channel *chan, void *data)
{
	return;
}

static void sms_messagetx (sms_t * h);

/*--- numcpy: copy number, skipping non digits apart from leading + */
static void numcpy (char *d, char *s)
{
	if (*s == '+')
		*d++ = *s++;
	while (*s) {
  		if (isdigit (*s))
     			*d++ = *s;
		s++;
	}
	*d = 0;
}

/*--- isodate: static, return a date/time in ISO format */
static char * isodate (time_t t)
{
	static char date[20];
	strftime (date, sizeof (date), "%Y-%m-%dT%H:%M:%S", localtime (&t));
	return date;
}

/*--- utf8decode: reads next UCS character from null terminated UTF-8 string and advanced pointer */
/* for non valid UTF-8 sequences, returns character as is */
/* Does not advance pointer for null termination */
static long utf8decode (unsigned char **pp)
{
	unsigned char *p = *pp;
	if (!*p)
		return 0;                 /* null termination of string */
	(*pp)++;
	if (*p < 0xC0)
		return *p;                /* ascii or continuation character */
	if (*p < 0xE0) {
		if (*p < 0xC2 || (p[1] & 0xC0) != 0x80)
			return *p;             /* not valid UTF-8 */
		(*pp)++;
		return ((*p & 0x1F) << 6) + (p[1] & 0x3F);
   	}
	if (*p < 0xF0) {
		if ((*p == 0xE0 && p[1] < 0xA0) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
			 return *p;             /* not valid UTF-8 */
		(*pp) += 2;
		return ((*p & 0x0F) << 12) + ((p[1] & 0x3F) << 6) + (p[2] & 0x3F);
	}
	if (*p < 0xF8) {
		if ((*p == 0xF0 && p[1] < 0x90) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
			return *p;             /* not valid UTF-8 */
		(*pp) += 3;
		return ((*p & 0x07) << 18) + ((p[1] & 0x3F) << 12) + ((p[2] & 0x3F) << 6) + (p[3] & 0x3F);
	}
	if (*p < 0xFC) {
		if ((*p == 0xF8 && p[1] < 0x88) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80
			|| (p[4] & 0xC0) != 0x80)
			return *p;             /* not valid UTF-8 */
		(*pp) += 4;
		return ((*p & 0x03) << 24) + ((p[1] & 0x3F) << 18) + ((p[2] & 0x3F) << 12) + ((p[3] & 0x3F) << 6) + (p[4] & 0x3F);
	}
	if (*p < 0xFE) {
		if ((*p == 0xFC && p[1] < 0x84) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80
			|| (p[4] & 0xC0) != 0x80 || (p[5] & 0xC0) != 0x80)
			return *p;             /* not valid UTF-8 */
		(*pp) += 5;
		return ((*p & 0x01) << 30) + ((p[1] & 0x3F) << 24) + ((p[2] & 0x3F) << 18) + ((p[3] & 0x3F) << 12) + ((p[4] & 0x3F) << 6) + (p[5] & 0x3F);
	}
	return *p;                   /* not sensible */
}

/*--- packsms7: takes a binary header (udhl bytes at udh) and UCS-2 message (udl characters at ud) and packs in to o using SMS 7 bit character codes */
/* The return value is the number of septets packed in to o, which is internally limited to SMSLEN */
/* o can be null, in which case this is used to validate or count only */
/* if the input contains invalid characters then the return value is -1 */
static int packsms7 (unsigned char *o, int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	 unsigned char p = 0, b = 0, n = 0;

	if (udhl) {                            /* header */
		if (o)
			o[p++] = udhl;
		b = 1;
		n = 1;
		while (udhl--) {
			if (o)
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
			};	/* filling to septet boundary */
		}
		if (o)
			o[p] = 0;
		/* message */
		while (udl--) {
			long u;
			unsigned char v;
			u = *ud++;
			for (v = 0; v < 128 && defaultalphabet[v] != u; v++);
			if (v == 128 && u && n + 1 < SMSLEN) {
				for (v = 0; v < 128 && escapes[v] != u; v++);
				if (v < 128) {	/* escaped sequence */
				if (o)
					o[p] |= (27 << b);
				b += 7;
				if (b >= 8) {
					b -= 8;
					p++;
					if (o)
						o[p] = (27 >> (7 - b));
				}
				n++;
			}
		}
		if (v == 128)
			return -1;             /* invalid character */
		if (o)
			o[p] |= (v << b);
		b += 7;
		if (b >= 8) {
			b -= 8;
			p++;
			if (o)
				o[p] = (v >> (7 - b));
		}
		if (++n >= SMSLEN)
			return n;
	}
	return n;
}

/*--- packsms8: takes a binary header (udhl bytes at udh) and UCS-2 message (udl characters at ud) and packs in to o using 8 bit character codes */
/* The return value is the number of bytes packed in to o, which is internally limited to 140 */
/* o can be null, in which case this is used to validate or count only */
/* if the input contains invalid characters then the return value is -1 */
static int packsms8 (unsigned char *o, int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char p = 0;

	/* header - no encoding */
	if (udhl) {
		if (o)
			o[p++] = udhl;
		while (udhl--) {
			if (o)
				o[p++] = *udh++;
			if (p >= 140)
				return p;
		}
	}
	while (udl--) {
		long u;
		u = *ud++;
		if (u < 0 || u > 0xFF)
			return -1;             /* not valid */
		if (o)
			o[p++] = u;
		if (p >= 140)
			return p;
	}
	return p;
}

/*--- packsms16: takes a binary header (udhl bytes at udh) and UCS-2 
	message (udl characters at ud) and packs in to o using 16 bit 
	UCS-2 character codes 
	The return value is the number of bytes packed in to o, which is 
	internally limited to 140 
	o can be null, in which case this is used to validate or count 
	only if the input contains invalid characters then 
	the return value is -1 */
static int packsms16 (unsigned char *o, int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char p = 0;
	/* header - no encoding */
	if (udhl) {
		if (o)
			o[p++] = udhl;
		while (udhl--) {
			if (o)
				o[p++] = *udh++;
			if (p >= 140)
				return p;
		}
	}
	while (udl--) {
		long u;
		u = *ud++;
		if (o)
			o[p++] = (u >> 8);
		if (p >= 140)
			return p - 1;          /* could not fit last character */
		if (o)
			o[p++] = u;
		if (p >= 140)
			return p;
	}
	return p;
}

/*--- packsms: general pack, with length and data, 
	returns number of bytes of target used */
static int packsms (unsigned char dcs, unsigned char *base, unsigned int udhl, unsigned char *udh, int udl, unsigned short *ud)
{
	unsigned char *p = base;
	if (udl) {
		int l = 0;
		if (is7bit (dcs)) {		 /* 7 bit */
			l = packsms7 (p + 1, udhl, udh, udl, ud);
			if (l < 0)
				l = 0;
			*p++ = l;
			p += (l * 7 + 7) / 8;
		} else if (is8bit (dcs)) {								 /* 8 bit */
			l = packsms8 (p + 1, udhl, udh, udl, ud);
			if (l < 0)
				l = 0;
			*p++ = l;
			p += l;
		} else {			 /* UCS-2 */
			l = packsms16 (p + 1, udhl, udh, udl, ud);
			if (l < 0)
				l = 0;
			*p++ = l;
			p += l;
		}
	} else
		*p++ = 0;			  /* no user data */
	return p - base;
}


/*--- packdate: pack a date and return */
static void packdate (unsigned char *o, time_t w)
{
	struct tm *t = localtime (&w);
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined( __NetBSD__ ) || defined(__APPLE__)
	int z = -t->tm_gmtoff / 60 / 15;
#else
	int z = timezone / 60 / 15;
#endif
	*o++ = ((t->tm_year % 10) << 4) + (t->tm_year % 100) / 10;
	*o++ = (((t->tm_mon + 1) % 10) << 4) + (t->tm_mon + 1) / 10;
	*o++ = ((t->tm_mday % 10) << 4) + t->tm_mday / 10;
	*o++ = ((t->tm_hour % 10) << 4) + t->tm_hour / 10;
	*o++ = ((t->tm_min % 10) << 4) + t->tm_min / 10;
	*o++ = ((t->tm_sec % 10) << 4) + t->tm_sec / 10;
	if (z < 0)
		*o++ = (((-z) % 10) << 4) + (-z) / 10 + 0x08;
	else
		*o++ = ((z % 10) << 4) + z / 10;
}

/*--- unpackdate: unpack a date and return */
static time_t unpackdate (unsigned char *i)
{
	struct tm t;
	t.tm_year = 100 + (i[0] & 0xF) * 10 + (i[0] >> 4);
	t.tm_mon = (i[1] & 0xF) * 10 + (i[1] >> 4) - 1;
	t.tm_mday = (i[2] & 0xF) * 10 + (i[2] >> 4);
	t.tm_hour = (i[3] & 0xF) * 10 + (i[3] >> 4);
	t.tm_min = (i[4] & 0xF) * 10 + (i[4] >> 4);
	t.tm_sec = (i[5] & 0xF) * 10 + (i[5] >> 4);
	t.tm_isdst = 0;
	if (i[6] & 0x08)
		t.tm_min += 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
	else
		t.tm_min -= 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
	return mktime (&t);
}

/*--- unpacksms7: unpacks bytes (7 bit encoding) at i, len l septets, 
	and places in udh and ud setting udhl and udl. udh not used 
	if udhi not set */
static void unpacksms7 (unsigned char *i, unsigned char l, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
{
	unsigned char b = 0, p = 0;
	unsigned short *o = ud;
	*udhl = 0;
	if (udhi && l) {		 /* header */
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
					if (!l)
						break;
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
		if (b < 2)
			v = ((i[p] >> b) & 0x7F);
		else
			v = ((((i[p] >> b) + (i[p + 1] << (8 - b)))) & 0x7F);
		b += 7;
		if (b >= 8) {
			b -= 8;
			p++;
		}
		if (o > ud && o[-1] == 0x00A0 && escapes[v])
			o[-1] = escapes[v];
		else
			*o++ = defaultalphabet[v];
	}
	*udl = (o - ud);
}

/*--- unpacksms8: unpacks bytes (8 bit encoding) at i, len l septets, 
      and places in udh and ud setting udhl and udl. udh not used 
      if udhi not set */
static void unpacksms8 (unsigned char *i, unsigned char l, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
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
	while (l--)
		*o++ = *i++;	  /* not to UTF-8 as explicitely 8 bit coding in DCS */
	*udl = (o - ud);
}

/*--- unpacksms16: unpacks bytes (16 bit encoding) at i, len l septets,
	 and places in udh and ud setting udhl and udl. 
	udh not used if udhi not set */
static void unpacksms16 (unsigned char *i, unsigned char l, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
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
		if (l--)
			v = (v << 8) + *i++;
		*o++ = v;
	}
	*udl = (o - ud);
}

/*--- unpacksms: general unpack - starts with length byte (octet or septet) and returns number of bytes used, inc length */
static int unpacksms (unsigned char dcs, unsigned char *i, unsigned char *udh, int *udhl, unsigned short *ud, int *udl, char udhi)
{
	int l = *i++;
	if (is7bit (dcs)) {
		unpacksms7 (i, l, udh, udhl, ud, udl, udhi);
		l = (l * 7 + 7) / 8;		/* adjust length to return */
	} else if (is8bit (dcs))
		unpacksms8 (i, l, udh, udhl, ud, udl, udhi);
	else
		unpacksms16 (i, l, udh, udhl, ud, udl, udhi);
	return l + 1;
}

/*--- unpackaddress: unpack an address from i, return byte length, unpack to o */
static unsigned char unpackaddress (char *o, unsigned char *i)
{
	unsigned char l = i[0],
		p;
	if (i[1] == 0x91)
		*o++ = '+';
	for (p = 0; p < l; p++) {
		if (p & 1)
			*o++ = (i[2 + p / 2] >> 4) + '0';
		else
			*o++ = (i[2 + p / 2] & 0xF) + '0';
	}
	*o = 0;
	return (l + 5) / 2;
}

/*--- packaddress: store an address at o, and return number of bytes used */
static unsigned char packaddress (unsigned char *o, char *i)
{
	unsigned char p = 2;
	o[0] = 0;
	if (*i == '+') {
		i++;
		o[1] = 0x91;
	} else
		o[1] = 0x81;
	while (*i)
		if (isdigit (*i)) {
			if (o[0] & 1)
				o[p++] |= ((*i & 0xF) << 4);
			else
				o[p] = (*i & 0xF);
			o[0]++;
			i++;
		} else
			i++;
	if (o[0] & 1)
		o[p++] |= 0xF0;			  /* pad */
	return p;
}

/*--- sms_log: Log the output, and remove file */
static void sms_log (sms_t * h, char status)
{
	if (*h->oa || *h->da) {
		int o = open (log_file, O_CREAT | O_APPEND | O_WRONLY, 0666);
		if (o >= 0) {
			char line[1000], mrs[3] = "", *p;
			unsigned char n;

			if (h->mr >= 0)
				snprintf (mrs, sizeof (mrs), "%02X", h->mr);
			snprintf (line, sizeof (line), "%s %c%c%c%s %s %s %s ",
				 isodate (time (0)), status, h->rx ? 'I' : 'O', h->smsc ? 'S' : 'M', mrs, h->queue, *h->oa ? h->oa : "-",
				 *h->da ? h->da : "-");
			p = line + strlen (line);
			for (n = 0; n < h->udl; n++)
				if (h->ud[n] == '\\') {
					*p++ = '\\';
					*p++ = '\\';
				} else if (h->ud[n] == '\n') {
					*p++ = '\\';
					*p++ = 'n';
				} else if (h->ud[n] == '\r') {
					*p++ = '\\';
					*p++ = 'r';
				} else if (h->ud[n] < 32 || h->ud[n] == 127)
					*p++ = 191;
				else
					*p++ = h->ud[n];
			*p++ = '\n';
			*p = 0;
			write (o, line, strlen (line));
			close (o);
		}
		*h->oa = *h->da = h->udl = 0;
	}
}

/*--- sms_readfile: parse and delete a file */
static void sms_readfile (sms_t * h, char *fn)
{
	char line[1000];
	FILE *s;
	char dcsset = 0;				 /* if DSC set */
	ast_log (LOG_EVENT, "Sending %s\n", fn);
	h->rx = h->udl = *h->oa = *h->da = h->pid = h->srr = h->udhi = h->rp = h->vp = h->udhl = 0;
	h->mr = -1;
	h->dcs = 0xF1;					/* normal messages class 1 */
	h->scts = time (0);
	s = fopen (fn, "r");
	if (s)
	{
		if (unlink (fn))
		{								 /* concurrent access, we lost */
			fclose (s);
			return;
		}
		while (fgets (line, sizeof (line), s))
		{								 /* process line in file */
			char *p;
			for (p = line; *p && *p != '\n' && *p != '\r'; p++);
			*p = 0;					 /* strip eoln */
			p = line;
			if (!*p || *p == ';')
				continue;			  /* blank line or comment, ignore */
			while (isalnum (*p))
			{
				*p = tolower (*p);
				p++;
			}
			while (isspace (*p))
				*p++ = 0;
			if (*p == '=')
			{
				*p++ = 0;
				if (!strcmp (line, "ud"))
				{						 /* parse message (UTF-8) */
					unsigned char o = 0;
					while (*p && o < SMSLEN)
						h->ud[o++] = utf8decode((unsigned char **)&p);
					h->udl = o;
					if (*p)
						ast_log (LOG_WARNING, "UD too long in %s\n", fn);
				} else
				{
					while (isspace (*p))
						p++;
					if (!strcmp (line, "oa") && strlen (p) < sizeof (h->oa))
						numcpy (h->oa, p);
					else if (!strcmp (line, "da") && strlen (p) < sizeof (h->oa))
						numcpy (h->da, p);
					else if (!strcmp (line, "pid"))
						h->pid = atoi (p);
					else if (!strcmp (line, "dcs"))
					{
						h->dcs = atoi (p);
						dcsset = 1;
					} else if (!strcmp (line, "mr"))
						h->mr = atoi (p);
					else if (!strcmp (line, "srr"))
						h->srr = (atoi (p) ? 1 : 0);
					else if (!strcmp (line, "vp"))
						h->vp = atoi (p);
					else if (!strcmp (line, "rp"))
						h->rp = (atoi (p) ? 1 : 0);
					else if (!strcmp (line, "scts"))
					{					 /* get date/time */
						int Y,
						  m,
						  d,
						  H,
						  M,
						  S;
						if (sscanf (p, "%d-%d-%dT%d:%d:%d", &Y, &m, &d, &H, &M, &S) == 6)
						{
							struct tm t;
							t.tm_year = Y - 1900;
							t.tm_mon = m - 1;
							t.tm_mday = d;
							t.tm_hour = H;
							t.tm_min = M;
							t.tm_sec = S;
							t.tm_isdst = -1;
							h->scts = mktime (&t);
							if (h->scts == (time_t) - 1)
								ast_log (LOG_WARNING, "Bad date/timein %s: %s", fn, p);
						}
					} else
						ast_log (LOG_WARNING, "Cannot parse in %s: %s=%si\n", fn, line, p);
				}
			} else if (*p == '#')
			{							 /* raw hex format */
				*p++ = 0;
				if (*p == '#')
				{
					p++;
					if (!strcmp (line, "ud"))
					{					 /* user data */
						int o = 0;
						while (*p && o < SMSLEN)
						{
							if (isxdigit (*p) && isxdigit (p[1]) && isxdigit (p[2]) && isxdigit (p[3]))
							{
								h->ud[o++] =
									(((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 12) +
									(((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF)) << 8) +
									(((isalpha (p[2]) ? 9 : 0) + (p[2] & 0xF)) << 4) + ((isalpha (p[3]) ? 9 : 0) + (p[3] & 0xF));
								p += 4;
							} else
								break;
						}
						h->udl = o;
						if (*p)
							ast_log (LOG_WARNING, "UD too long / invalid UCS-2 hex in %s\n", fn);
					} else
						ast_log (LOG_WARNING, "Only ud can use ## format, %s\n", fn);
				} else if (!strcmp (line, "ud"))
				{						 /* user data */
					int o = 0;
					while (*p && o < SMSLEN)
					{
						if (isxdigit (*p) && isxdigit (p[1]))
						{
							h->ud[o++] = (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF));
							p += 2;
						} else
							break;
					}
					h->udl = o;
					if (*p)
						ast_log (LOG_WARNING, "UD too long / invalid UCS-1 hex in %s\n", fn);
				} else if (!strcmp (line, "udh"))
				{						 /* user data header */
					unsigned char o = 0;
					h->udhi = 1;
					while (*p && o < SMSLEN)
					{
						if (isxdigit (*p) && isxdigit (p[1]))
						{
							h->udh[o] = (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF));
							o++;
							p += 2;
						} else
							break;
					}
					h->udhl = o;
					if (*p)
						ast_log (LOG_WARNING, "UDH too long / invalid hex in %s\n", fn);
				} else
					ast_log (LOG_WARNING, "Only ud and udh can use # format, %s\n", fn);
			} else
				ast_log (LOG_WARNING, "Cannot parse in %s: %s\n", fn, line);
		}
		fclose (s);
		if (!dcsset && packsms7 (0, h->udhl, h->udh, h->udl, h->ud) < 0)
		{
			if (packsms8 (0, h->udhl, h->udh, h->udl, h->ud) < 0)
			{
				if (packsms16 (0, h->udhl, h->udh, h->udl, h->ud) < 0)
					ast_log (LOG_WARNING, "Invalid UTF-8 message even for UCS-2 (%s)\n", fn);
				else
				{
					h->dcs = 0x08;	/* default to 16 bit */
					ast_log (LOG_WARNING, "Sending in 16 bit format (%s)\n", fn);
				}
			} else
			{
				h->dcs = 0xF5;		/* default to 8 bit */
				ast_log (LOG_WARNING, "Sending in 8 bit format (%s)\n", fn);
			}
		}
		if (is7bit (h->dcs) && packsms7 (0, h->udhl, h->udh, h->udl, h->ud) < 0)
			ast_log (LOG_WARNING, "Invalid 7 bit GSM data %s\n", fn);
		if (is8bit (h->dcs) && packsms8 (0, h->udhl, h->udh, h->udl, h->ud) < 0)
			ast_log (LOG_WARNING, "Invalid 8 bit data %s\n", fn);
		if (is16bit (h->dcs) && packsms16 (0, h->udhl, h->udh, h->udl, h->ud) < 0)
			ast_log (LOG_WARNING, "Invalid 16 bit data %s\n", fn);
	}
}

/*--- sms_writefile: white a received text message to a file */
static void sms_writefile (sms_t * h)
{
	char fn[200] = "", fn2[200] = "";
	FILE *o;
	ast_copy_string (fn, spool_dir, sizeof (fn));
	mkdir (fn, 0777);			/* ensure it exists */
	snprintf (fn + strlen (fn), sizeof (fn) - strlen (fn), "/%s", h->smsc ? h->rx ? "morx" : "mttx" : h->rx ? "mtrx" : "motx");
	mkdir (fn, 0777);			/* ensure it exists */
	ast_copy_string (fn2, fn, sizeof (fn2));
	snprintf (fn2 + strlen (fn2), sizeof (fn2) - strlen (fn2), "/%s.%s-%d", h->queue, isodate (h->scts), seq++);
	snprintf (fn + strlen (fn), sizeof (fn) - strlen (fn), "/.%s", fn2 + strlen (fn) + 1);
	o = fopen (fn, "w");
	if (o) {
		if (*h->oa)
			fprintf (o, "oa=%s\n", h->oa);
		if (*h->da)
			fprintf (o, "da=%s\n", h->da);
		if (h->udhi) {
			unsigned int p;
			fprintf (o, "udh#");
			for (p = 0; p < h->udhl; p++)
				fprintf (o, "%02X", h->udh[p]);
			fprintf (o, "\n");
		}
		if (h->udl) {
			unsigned int p;
			for (p = 0; p < h->udl && h->ud[p] >= ' '; p++);
			if (p < h->udl)
				fputc (';', o);	  /* cannot use ud=, but include as a comment for human readable */
			fprintf (o, "ud=");
			for (p = 0; p < h->udl; p++) {
				unsigned short v = h->ud[p];
				if (v < 32)
					fputc (191, o);
				else if (v < 0x80)
					fputc (v, o);
				else if (v < 0x800)
				{
					fputc (0xC0 + (v >> 6), o);
					fputc (0x80 + (v & 0x3F), o);
				} else
				{
					fputc (0xE0 + (v >> 12), o);
					fputc (0x80 + ((v >> 6) & 0x3F), o);
					fputc (0x80 + (v & 0x3F), o);
				}
			}
			fprintf (o, "\n");
			for (p = 0; p < h->udl && h->ud[p] >= ' '; p++);
			if (p < h->udl) {
				for (p = 0; p < h->udl && h->ud[p] < 0x100; p++);
				if (p == h->udl) {						 /* can write in ucs-1 hex */
					fprintf (o, "ud#");
					for (p = 0; p < h->udl; p++)
						fprintf (o, "%02X", h->ud[p]);
					fprintf (o, "\n");
				} else {						 /* write in UCS-2 */
					fprintf (o, "ud##");
					for (p = 0; p < h->udl; p++)
						fprintf (o, "%04X", h->ud[p]);
					fprintf (o, "\n");
				}
			}
		}
		if (h->scts)
			fprintf (o, "scts=%s\n", isodate (h->scts));
		if (h->pid)
			fprintf (o, "pid=%d\n", h->pid);
		if (h->dcs != 0xF1)
			fprintf (o, "dcs=%d\n", h->dcs);
		if (h->vp)
			fprintf (o, "vp=%d\n", h->vp);
		if (h->srr)
			fprintf (o, "srr=1\n");
		if (h->mr >= 0)
			fprintf (o, "mr=%d\n", h->mr);
		if (h->rp)
			fprintf (o, "rp=1\n");
		fclose (o);
		if (rename (fn, fn2))
			unlink (fn);
		else
			ast_log (LOG_EVENT, "Received to %s\n", fn2);
	}
}

/*--- readdirqueue: read dir skipping dot files... */
static struct dirent *readdirqueue (DIR * d, char *queue)
{
   struct dirent *f;
   do {
      f = readdir (d);
   } while (f && (*f->d_name == '.' || strncmp (f->d_name, queue, strlen (queue)) || f->d_name[strlen (queue)] != '.'));
   return f;
}

/*--- sms_handleincoming: handle the incoming message */
static unsigned char sms_handleincoming (sms_t * h)
{
	unsigned char p = 3;
	if (h->smsc) {									 /* SMSC */
		if ((h->imsg[2] & 3) == 1) {				/* SMS-SUBMIT */
			h->udhl = h->udl = 0;
			h->vp = 0;
			h->srr = ((h->imsg[2] & 0x20) ? 1 : 0);
			h->udhi = ((h->imsg[2] & 0x40) ? 1 : 0);
			h->rp = ((h->imsg[2] & 0x80) ? 1 : 0);
			ast_copy_string (h->oa, h->cli, sizeof (h->oa));
			h->scts = time (0);
			h->mr = h->imsg[p++];
			p += unpackaddress (h->da, h->imsg + p);
			h->pid = h->imsg[p++];
			h->dcs = h->imsg[p++];
			if ((h->imsg[2] & 0x18) == 0x10) {							 /* relative VP */
				if (h->imsg[p] < 144)
					h->vp = (h->imsg[p] + 1) * 5;
				else if (h->imsg[p] < 168)
					h->vp = 720 + (h->imsg[p] - 143) * 30;
				else if (h->imsg[p] < 197)
					h->vp = (h->imsg[p] - 166) * 1440;
				else
					h->vp = (h->imsg[p] - 192) * 10080;
				p++;
			} else if (h->imsg[2] & 0x18)
				p += 7;				 /* ignore enhanced / absolute VP */
			p += unpacksms (h->dcs, h->imsg + p, h->udh, &h->udhl, h->ud, &h->udl, h->udhi);
			h->rx = 1;				 /* received message */
			sms_writefile (h);	  /* write the file */
			if (p != h->imsg[1] + 2) {
				ast_log (LOG_WARNING, "Mismatch receive unpacking %d/%d\n", p, h->imsg[1] + 2);
				return 0xFF;		  /* duh! */
			}
		} else {
			ast_log (LOG_WARNING, "Unknown message type %02X\n", h->imsg[2]);
			return 0xFF;
		}
	} else {									 /* client */
		if (!(h->imsg[2] & 3)) {								 /* SMS-DELIVER */
			*h->da = h->srr = h->rp = h->vp = h->udhi = h->udhl = h->udl = 0;
			h->srr = ((h->imsg[2] & 0x20) ? 1 : 0);
			h->udhi = ((h->imsg[2] & 0x40) ? 1 : 0);
			h->rp = ((h->imsg[2] & 0x80) ? 1 : 0);
			h->mr = -1;
			p += unpackaddress (h->oa, h->imsg + p);
			h->pid = h->imsg[p++];
			h->dcs = h->imsg[p++];
			h->scts = unpackdate (h->imsg + p);
			p += 7;
			p += unpacksms (h->dcs, h->imsg + p, h->udh, &h->udhl, h->ud, &h->udl, h->udhi);
			h->rx = 1;				 /* received message */
			sms_writefile (h);	  /* write the file */
			if (p != h->imsg[1] + 2) {
				ast_log (LOG_WARNING, "Mismatch receive unpacking %d/%d\n", p, h->imsg[1] + 2);
				return 0xFF;		  /* duh! */
			}
		} else {
			ast_log (LOG_WARNING, "Unknown message type %02X\n", h->imsg[2]);
			return 0xFF;
		}
	}
	return 0;						  /* no error */
}

#ifdef SOLARIS
#define NAME_MAX 1024
#endif

/*--- sms_nextoutgoing: find and fill in next message, 
	or send a REL if none waiting */
static void sms_nextoutgoing (sms_t * h)
{          
	char fn[100 + NAME_MAX] = "";
	DIR *d;
	char more = 0;
	ast_copy_string (fn, spool_dir, sizeof (fn));
	mkdir (fn, 0777);				/* ensure it exists */
	h->rx = 0;						 /* outgoing message */
	snprintf (fn + strlen (fn), sizeof (fn) - strlen (fn), "/%s", h->smsc ? "mttx" : "motx");
	mkdir (fn, 0777);				/* ensure it exists */
	d = opendir (fn);
	if (d) {
		struct dirent *f = readdirqueue (d, h->queue);
		if (f) {
			snprintf (fn + strlen (fn), sizeof (fn) - strlen (fn), "/%s", f->d_name);
			sms_readfile (h, fn);
			if (readdirqueue (d, h->queue))
				more = 1;			  /* more to send */
		}
		closedir (d);
	}
	if (*h->da || *h->oa) {									 /* message to send */
		unsigned char p = 2;
		h->omsg[0] = 0x91;		  /* SMS_DATA */
		if (h->smsc) {			 /* deliver */
			h->omsg[p++] = (more ? 4 : 0);
			p += packaddress (h->omsg + p, h->oa);
			h->omsg[p++] = h->pid;
			h->omsg[p++] = h->dcs;
			packdate (h->omsg + p, h->scts);
			p += 7;
			p += packsms (h->dcs, h->omsg + p, h->udhl, h->udh, h->udl, h->ud);
		} else {			 /* submit */
			h->omsg[p++] =
				0x01 + (more ? 4 : 0) + (h->srr ? 0x20 : 0) + (h->rp ? 0x80 : 0) + (h->vp ? 0x10 : 0) + (h->udhi ? 0x40 : 0);
			if (h->mr < 0)
				h->mr = message_ref++;
			h->omsg[p++] = h->mr;
			p += packaddress (h->omsg + p, h->da);
			h->omsg[p++] = h->pid;
			h->omsg[p++] = h->dcs;
			if (h->vp) {		 /* relative VP */
				if (h->vp < 720)
					h->omsg[p++] = (h->vp + 4) / 5 - 1;
				else if (h->vp < 1440)
					h->omsg[p++] = (h->vp - 720 + 29) / 30 + 143;
				else if (h->vp < 43200)
					h->omsg[p++] = (h->vp + 1439) / 1440 + 166;
				else if (h->vp < 635040)
					h->omsg[p++] = (h->vp + 10079) / 10080 + 192;
				else
					h->omsg[p++] = 255;		/* max */
			}
			p += packsms (h->dcs, h->omsg + p, h->udhl, h->udh, h->udl, h->ud);
		}
		h->omsg[1] = p - 2;
		sms_messagetx (h);
	} else {				 /* no message */
		h->omsg[0] = 0x94;		  /* SMS_REL */
		h->omsg[1] = 0;
		sms_messagetx (h);
	}
}

static void sms_debug (char *dir, unsigned char *msg)
{
	char txt[259 * 3 + 1],
	 *p = txt;						 /* always long enough */
	int n = msg[1] + 3,
		q = 0;
	while (q < n && q < 30) {
		sprintf (p, " %02X", msg[q++]);
		p += 3;
	}
	if (q < n)
		sprintf (p, "...");
	if (option_verbose > 2)
		ast_verbose (VERBOSE_PREFIX_3 "SMS %s%s\n", dir, txt);
}

static void sms_messagerx(sms_t * h)
{
	sms_debug ("RX", h->imsg);
	/* testing */
	switch (h->imsg[0]) {
	case 0x91:						/* SMS_DATA */
		{
			unsigned char cause = sms_handleincoming (h);
			if (!cause) {
				sms_log (h, 'Y');
				h->omsg[0] = 0x95;  /* SMS_ACK */
				h->omsg[1] = 0x02;
				h->omsg[2] = 0x00;  /* deliver report */
				h->omsg[3] = 0x00;  /* no parameters */
			} else {							 /* NACK */
				sms_log (h, 'N');
				h->omsg[0] = 0x96;  /* SMS_NACK */
				h->omsg[1] = 3;
				h->omsg[2] = 0;	  /* delivery report */
				h->omsg[3] = cause; /* cause */
				h->omsg[4] = 0;	  /* no parameters */
			}
			sms_messagetx (h);
		}
		break;
	case 0x92:						/* SMS_ERROR */
		h->err = 1;
		sms_messagetx (h);		  /* send whatever we sent again */
		break;
	case 0x93:						/* SMS_EST */
		sms_nextoutgoing (h);
		break;
	case 0x94:						/* SMS_REL */
		h->hangup = 1;				/* hangup */
		break;
	case 0x95:						/* SMS_ACK */
		sms_log (h, 'Y');
		sms_nextoutgoing (h);
		break;
	case 0x96:						/* SMS_NACK */
		h->err = 1;
		sms_log (h, 'N');
		sms_nextoutgoing (h);
		break;
	default:						  /* Unknown */
		h->omsg[0] = 0x92;		  /* SMS_ERROR */
		h->omsg[1] = 1;
		h->omsg[2] = 3;			  /* unknown message type; */
		sms_messagetx (h);
		break;
	}
}

static void sms_messagetx(sms_t * h)
{
	unsigned char c = 0, p;
	for (p = 0; p < h->omsg[1] + 2; p++)
		c += h->omsg[p];
	h->omsg[h->omsg[1] + 2] = 0 - c;
	sms_debug ("TX", h->omsg);
	h->obyte = 1;
	h->opause = 200;
	if (h->omsg[0] == 0x93)
		h->opause = 2400;			/* initial message delay 300ms (for BT) */
	h->obytep = 0;
	h->obitp = 0;
	h->osync = 80;
	h->obyten = h->omsg[1] + 3;
}

static int sms_generate (struct ast_channel *chan, void *data, int len, int samples)
{
	struct ast_frame f = { 0 };
	unsigned char waste[AST_FRIENDLY_OFFSET];
#ifdef OUTALAW
	unsigned char buf[800];
#else
	signed short buf[800];
#endif
	sms_t *h = data;
	int i;

	if (len > sizeof (buf)) {
		ast_log (LOG_WARNING, "Only doing %d bytes (%d bytes requested)\n", (int)(sizeof (buf) / sizeof (signed short)), len);
		len = sizeof (buf);
#ifdef OUTALAW
		samples = len;
#else
		samples = len / 2;
#endif
	}
	waste[0] = 0;					 /* make compiler happy */
	f.frametype = AST_FRAME_VOICE;
#ifdef OUTALAW
	f.subclass = AST_FORMAT_ALAW;
	f.datalen = samples;
#else
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = samples * 2;
#endif
	f.offset = AST_FRIENDLY_OFFSET;
	f.mallocd = 0;
	f.data = buf;
	f.samples = samples;
	f.src = "app_sms";
	/* create a buffer containing the digital sms pattern */
	for (i = 0; i < samples; i++) {
#ifdef OUTALAW
		buf[i] = wavea[0];
#else
		buf[i] = wave[0];
#endif
		if (h->opause)
			h->opause--;
		else if (h->obyten || h->osync) {								 /* sending data */
#ifdef OUTALAW
			buf[i] = wavea[h->ophase];
#else
			buf[i] = wave[h->ophase];
#endif
			if ((h->ophase += ((h->obyte & 1) ? 13 : 21)) >= 80)
				h->ophase -= 80;
			if ((h->ophasep += 12) >= 80) {							 /* next bit */
				h->ophasep -= 80;
				if (h->osync)
					h->osync--;		/* sending sync bits */
				else {
					h->obyte >>= 1;
					h->obitp++;
					if (h->obitp == 1)
						h->obyte = 0; /* start bit; */
					else if (h->obitp == 2)
						h->obyte = h->omsg[h->obytep];
					else if (h->obitp == 10) {
						h->obyte = 1; /* stop bit */
						h->obitp = 0;
						h->obytep++;
						if (h->obytep == h->obyten) {
							h->obytep = h->obyten = 0; /* sent */
							h->osync = 10;	  /* trailing marks */
						}
					}
				}
			}
		}
	}
	if (ast_write (chan, &f) < 0) {
		ast_log (LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror (errno));
		return -1;
	}
	return 0;
}

static void sms_process (sms_t * h, int samples, signed short *data)
{
	if (h->obyten || h->osync)
		return;						 /* sending */
	while (samples--) {
		unsigned long long m0, m1;
		if (abs (*data) > h->imag)
			h->imag = abs (*data);
		else
			h->imag = h->imag * 7 / 8;
		if (h->imag > 500) {
			h->idle = 0;
			h->ims0 = (h->ims0 * 6 + *data * wave[h->ips0]) / 7;
			h->imc0 = (h->imc0 * 6 + *data * wave[h->ipc0]) / 7;
			h->ims1 = (h->ims1 * 6 + *data * wave[h->ips1]) / 7;
			h->imc1 = (h->imc1 * 6 + *data * wave[h->ipc1]) / 7;
			m0 = h->ims0 * h->ims0 + h->imc0 * h->imc0;
			m1 = h->ims1 * h->ims1 + h->imc1 * h->imc1;
			if ((h->ips0 += 21) >= 80)
				h->ips0 -= 80;
			if ((h->ipc0 += 21) >= 80)
				h->ipc0 -= 80;
			if ((h->ips1 += 13) >= 80)
				h->ips1 -= 80;
			if ((h->ipc1 += 13) >= 80)
				h->ipc1 -= 80;
			{
				char bit;
				h->ibith <<= 1;
				if (m1 > m0)
					h->ibith |= 1;
				if (h->ibith & 8)
					h->ibitt--;
				if (h->ibith & 1)
					h->ibitt++;
				bit = ((h->ibitt > 1) ? 1 : 0);
				if (bit != h->ibitl)
					h->ibitc = 1;
				else
					h->ibitc++;
				h->ibitl = bit;
				if (!h->ibitn && h->ibitc == 4 && !bit) {
					h->ibitn = 1;
					h->iphasep = 0;
				}
				if (bit && h->ibitc == 200) {						 /* sync, restart message */
					h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
				}
				if (h->ibitn) {
					h->iphasep += 12;
					if (h->iphasep >= 80) {					 /* next bit */
						h->iphasep -= 80;
						if (h->ibitn++ == 9) {				 /* end of byte */
							if (!bit)  /* bad stop bit */
								h->ierr = 0xFF; /* unknown error */
							else {
								if (h->ibytep < sizeof (h->imsg)) {
									h->imsg[h->ibytep] = h->ibytev;
									h->ibytec += h->ibytev;
									h->ibytep++;
								} else if (h->ibytep == sizeof (h->imsg))
									h->ierr = 2; /* bad message length */
								if (h->ibytep > 1 && h->ibytep == 3 + h->imsg[1] && !h->ierr) {
									if (!h->ibytec)
										sms_messagerx (h);
									else
										h->ierr = 1;		/* bad checksum */
								}
							}
							h->ibitn = 0;
						}
						h->ibytev = (h->ibytev >> 1) + (bit ? 0x80 : 0);
					}
				}
			}
		} else {			 /* lost carrier */
			if (h->idle++ == 80000) {		 /* nothing happening */
				ast_log (LOG_EVENT, "No data, hanging up\n");
				h->hangup = 1;
				h->err = 1;
			}
			if (h->ierr) {							 /* error */
				h->err = 1;
				h->omsg[0] = 0x92;  /* error */
				h->omsg[1] = 1;
				h->omsg[2] = h->ierr;
				sms_messagetx (h);  /* send error */
			}
			h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
		}
		data++;
	}
}

static struct ast_generator smsgen = {
	alloc:sms_alloc,
	release:sms_release,
	generate:sms_generate,
};

static int sms_exec (struct ast_channel *chan, void *data)
{
	int res = -1;
	struct localuser *u;
	struct ast_frame *f;
	sms_t h = { 0 };
	
	LOCAL_USER_ADD(u);

	h.ipc0 = h.ipc1 = 20;		  /* phase for cosine */
	h.dcs = 0xF1;					 /* default */
	if (!data) {
		ast_log (LOG_ERROR, "Requires queue name at least\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (chan->cid.cid_num)
		ast_copy_string (h.cli, chan->cid.cid_num, sizeof (h.cli));

	{
		char *d = data,
			*p,
			answer = 0;
		if (!*d || *d == '|') {
			ast_log (LOG_ERROR, "Requires queue name\n");
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		for (p = d; *p && *p != '|'; p++);
		if (p - d >= sizeof (h.queue)) {
			ast_log (LOG_ERROR, "Queue name too long\n");
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		strncpy (h.queue, d, p - d);
		if (*p == '|')
			p++;
		d = p;
		for (p = h.queue; *p; p++)
			if (!isalnum (*p))
				*p = '-';			  /* make very safe for filenames */
		while (*d && *d != '|') {
			switch (*d) {
			case 'a':				 /* we have to send the initial FSK sequence */
				answer = 1;
				break;
			case 's':				 /* we are acting as a service centre talking to a phone */
				h.smsc = 1;
				break;
				/* the following apply if there is an arg3/4 and apply to the created message file */
			case 'r':
				h.srr = 1;
				break;
			case 'o':
				h.dcs |= 4;			/* octets */
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':				 /* set the pid for saved local message */
				h.pid = 0x40 + (*d & 0xF);
				break;
			}
			d++;
		}
		if (*d == '|') {
			/* submitting a message, not taking call. */
			/* depricated, use smsq instead */
			d++;
			h.scts = time (0);
			for (p = d; *p && *p != '|'; p++);
			if (*p)
				*p++ = 0;
			if (strlen (d) >= sizeof (h.oa)) {
				ast_log (LOG_ERROR, "Address too long %s\n", d);
				return 0;
			}
			if (h.smsc) {
				ast_copy_string (h.oa, d, sizeof (h.oa));
			} else {
				ast_copy_string (h.da, d, sizeof (h.da));
			}
			if (!h.smsc)
				ast_copy_string (h.oa, h.cli, sizeof (h.oa));
			d = p;
			h.udl = 0;
			while (*p && h.udl < SMSLEN)
				h.ud[h.udl++] = utf8decode((unsigned char **)&p);
			if (is7bit (h.dcs) && packsms7 (0, h.udhl, h.udh, h.udl, h.ud) < 0)
				ast_log (LOG_WARNING, "Invalid 7 bit GSM data\n");
			if (is8bit (h.dcs) && packsms8 (0, h.udhl, h.udh, h.udl, h.ud) < 0)
				ast_log (LOG_WARNING, "Invalid 8 bit data\n");
			if (is16bit (h.dcs) && packsms16 (0, h.udhl, h.udh, h.udl, h.ud) < 0)
				ast_log (LOG_WARNING, "Invalid 16 bit data\n");
			h.rx = 0;				  /* sent message */
			h.mr = -1;
			sms_writefile (&h);
			LOCAL_USER_REMOVE(u);
			return 0;
		}

		if (answer) {
			/* set up SMS_EST initial message */
			h.omsg[0] = 0x93;
			h.omsg[1] = 0;
			sms_messagetx (&h);
		}
	}

	if (chan->_state != AST_STATE_UP)
		ast_answer (chan);

#ifdef OUTALAW
	res = ast_set_write_format (chan, AST_FORMAT_ALAW);
#else
	res = ast_set_write_format (chan, AST_FORMAT_SLINEAR);
#endif
	if (res >= 0)
		res = ast_set_read_format (chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log (LOG_ERROR, "Unable to set to linear mode, giving up\n");
		LOCAL_USER_REMOVE (u);
		return -1;
	}

	if (ast_activate_generator (chan, &smsgen, &h) < 0) {
		ast_log (LOG_ERROR, "Failed to activate generator on '%s'\n", chan->name);
		LOCAL_USER_REMOVE (u);
		return -1;
	}

	/* Do our thing here */
	while (ast_waitfor (chan, -1) > -1 && !h.hangup)
	{
		f = ast_read (chan);
		if (!f)
			break;
		if (f->frametype == AST_FRAME_VOICE) {
			sms_process (&h, f->samples, f->data);
		}

		ast_frfree (f);
	}

	sms_log (&h, '?');			  /* log incomplete message */

	LOCAL_USER_REMOVE (u);
	return (h.err);
}

int unload_module (void)
{
	int res;

	res = ast_unregister_application (app);
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

int load_module (void)
{
#ifdef OUTALAW
	{
		int p;
		for (p = 0; p < 80; p++)
			wavea[p] = AST_LIN2A (wave[p]);
	}
#endif
	snprintf (log_file, sizeof (log_file), "%s/sms", ast_config_AST_LOG_DIR);
	snprintf (spool_dir, sizeof (spool_dir), "%s/sms", ast_config_AST_SPOOL_DIR);
	return ast_register_application (app, sms_exec, synopsis, descrip);
}

char *description (void)
{
	return tdesc;
}

int usecount (void)
{
	int res;
	STANDARD_USECOUNT (res);
	return res;
}

char *key ()
{
	return ASTERISK_GPL_KEY;
}
