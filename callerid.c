/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * CallerID Generation support 
 * 
 * Copyright (C) 2001 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
#include <ctype.h>
#include <asterisk/ulaw.h>
#include <asterisk/alaw.h>
#include <asterisk/frame.h>
#include <asterisk/channel.h>
#include <asterisk/callerid.h>
#include <asterisk/logger.h>
#include <asterisk/fskmodem.h>
#include <asterisk/utils.h>

struct callerid_state {
	fsk_data fskd;
	char rawdata[256];
	short oldstuff[160];
	int oldlen;
	int pos;
	int type;
	int cksum;
	char name[64];
	char number[64];
	int flags;
	int sawflag;
	int len;
};


float cid_dr[4], cid_di[4];
float clidsb = 8000.0 / 1200.0;
float sasdr, sasdi;
float casdr1, casdi1, casdr2, casdi2;

#define CALLERID_SPACE	2200.0		/* 2200 hz for "0" */
#define CALLERID_MARK	1200.0		/* 1200 hz for "1" */
#define SAS_FREQ		 440.0
#define CAS_FREQ1		2130.0
#define CAS_FREQ2		2750.0

static inline void gen_tones(unsigned char *buf, int len, int codec, float ddr1, float ddi1, float ddr2, float ddi2, float *cr1, float *ci1, float *cr2, float *ci2)
{
	int x;
	float t;
	for (x=0;x<len;x++) {
		t = *cr1 * ddr1 - *ci1 * ddi1;
		*ci1 = *cr1 * ddi1 + *ci1 * ddr1;
		*cr1 = t;
		t = 2.0 - (*cr1 * *cr1 + *ci1 * *ci1);
		*cr1 *= t;
		*ci1 *= t; 	

		t = *cr2 * ddr2 - *ci2 * ddi2;
		*ci2 = *cr2 * ddi2 + *ci2 * ddr2;
		*cr2 = t;
		t = 2.0 - (*cr2 * *cr2 + *ci2 * *ci2);
		*cr2 *= t;
		*ci2 *= t; 	
		buf[x] = AST_LIN2X((*cr1 + *cr2) * 2048.0);
	}
}

static inline void gen_tone(unsigned char *buf, int len, int codec, float ddr1, float ddi1, float *cr1, float *ci1)
{
	int x;
	float t;
	for (x=0;x<len;x++) {
		t = *cr1 * ddr1 - *ci1 * ddi1;
		*ci1 = *cr1 * ddi1 + *ci1 * ddr1;
		*cr1 = t;
		t = 2.0 - (*cr1 * *cr1 + *ci1 * *ci1);
		*cr1 *= t;
		*ci1 *= t; 	
		buf[x] = AST_LIN2X(*cr1 * 8192.0);
	}
}

void callerid_init(void)
{
	/* Initialize stuff for inverse FFT */
	cid_dr[0] = cos(CALLERID_SPACE * 2.0 * M_PI / 8000.0);
	cid_di[0] = sin(CALLERID_SPACE * 2.0 * M_PI / 8000.0);
	cid_dr[1] = cos(CALLERID_MARK * 2.0 * M_PI / 8000.0);
	cid_di[1] = sin(CALLERID_MARK * 2.0 * M_PI / 8000.0);
	sasdr = cos(SAS_FREQ * 2.0 * M_PI / 8000.0);
	sasdi = sin(SAS_FREQ * 2.0 * M_PI / 8000.0);
	casdr1 = cos(CAS_FREQ1 * 2.0 * M_PI / 8000.0);
	casdi1 = sin(CAS_FREQ1 * 2.0 * M_PI / 8000.0);
	casdr2 = cos(CAS_FREQ2 * 2.0 * M_PI / 8000.0);
	casdi2 = sin(CAS_FREQ2 * 2.0 * M_PI / 8000.0);
}

struct callerid_state *callerid_new(int cid_signalling)
{
	struct callerid_state *cid;
	cid = malloc(sizeof(struct callerid_state));
	if (cid) {
		memset(cid, 0, sizeof(struct callerid_state));
		cid->fskd.spb = 7;		/* 1200 baud */
		cid->fskd.hdlc = 0;		/* Async */
		cid->fskd.nbit = 8;		/* 8 bits */
		cid->fskd.nstop = 1;	/* 1 stop bit */
		cid->fskd.paridad = 0;	/* No parity */
		cid->fskd.bw=1;			/* Filter 800 Hz */
		if (cid_signalling == 2) { /* v23 signalling */
			cid->fskd.f_mark_idx =  4;	/* 1300 Hz */
			cid->fskd.f_space_idx = 5;	/* 2100 Hz */
		} else { /* Bell 202 signalling as default */ 
			cid->fskd.f_mark_idx =  2;	/* 1200 Hz */
			cid->fskd.f_space_idx = 3;	/* 2200 Hz */
		}
		cid->fskd.pcola = 0;		/* No clue */
		cid->fskd.cont = 0;			/* Digital PLL reset */
		cid->fskd.x0 = 0.0;
		cid->fskd.state = 0;
		memset(cid->name, 0, sizeof(cid->name));
		memset(cid->number, 0, sizeof(cid->number));
		cid->flags = CID_UNKNOWN_NAME | CID_UNKNOWN_NUMBER;
		cid->pos = 0;
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return cid;
}

void callerid_get(struct callerid_state *cid, char **name, char **number, int *flags)
{
	*flags = cid->flags;
	if (cid->flags & (CID_UNKNOWN_NAME | CID_PRIVATE_NUMBER))
		*name = NULL;
	else
		*name = cid->name;
	if (cid->flags & (CID_UNKNOWN_NUMBER | CID_PRIVATE_NUMBER))
		*number = NULL;
	else
		*number = cid->number;
}

void callerid_get_dtmf(char *cidstring, char *number, int *flags)
{
	int i;
	int code;

	/* "Clear" the number-buffer. */
	number[0] = 0;

	if (strlen(cidstring) < 2) {
		ast_log(LOG_DEBUG, "No cid detected\n");
		*flags = CID_UNKNOWN_NUMBER;
		return;
	}
	
	/* Detect protocol and special types */
	if (cidstring[0] == 'B') {
		/* Handle special codes */
		code = atoi(&cidstring[1]);
		if (code == 0)
			*flags = CID_UNKNOWN_NUMBER;
		else if (code == 10) 
			*flags = CID_PRIVATE_NUMBER;
		else
			ast_log(LOG_DEBUG, "Unknown DTMF code %d\n", code);
	} else if (cidstring[0] == 'D' && cidstring[2] == '#') {
		/* .DK special code */
		if (cidstring[1] == '1')
			*flags = CID_PRIVATE_NUMBER;
		if (cidstring[1] == '2' || cidstring[1] == '3')
			*flags = CID_UNKNOWN_NUMBER;
	} else if (cidstring[0] == 'D' || cidstring[0] == 'A') {
		/* "Standard" callerid */
		for (i = 1; i < strlen(cidstring); i++ ) {
			if (cidstring[i] == 'C' || cidstring[i] == '#')
				break;
			if (isdigit(cidstring[i]))
				number[i-1] = cidstring[i];
			else
				ast_log(LOG_DEBUG, "Unknown CID digit '%c'\n",
					cidstring[i]);
		}
		number[i-1] = 0;
	} else if (isdigit(cidstring[0])) {
		/* It begins with a digit, so we parse it as a number and hope
		 * for the best */
		ast_log(LOG_WARNING, "Couldn't detect start-character. CID "
			"parsing might be unreliable\n");
		for (i = 0; i < strlen(cidstring); i++) {
			if (isdigit(cidstring[i]))
                                number[i] = cidstring[i];
			else
				break;
		}
		number[i] = 0;
	} else {
		ast_log(LOG_DEBUG, "Unknown CID protocol, start digit '%c'\n", 
			cidstring[0]);
		*flags = CID_UNKNOWN_NUMBER;
	}
}

int ast_gen_cas(unsigned char *outbuf, int sendsas, int len, int codec)
{
	int pos = 0;
	int saslen=2400;
	float cr1 = 1.0;
	float ci1 = 0.0;
	float cr2 = 1.0;
	float ci2 = 0.0;
	if (sendsas) {
		if (len < saslen)
			return -1;
		gen_tone(outbuf, saslen, codec, sasdr, sasdi, &cr1, &ci1);
		len -= saslen;
		pos += saslen;
		cr2 = cr1;
		ci2 = ci1;
	}
	gen_tones(outbuf + pos, len, codec, casdr1, casdi1, casdr2, casdi2, &cr1, &ci1, &cr2, &ci2);
	return 0;
}

int callerid_feed(struct callerid_state *cid, unsigned char *ubuf, int len, int codec)
{
	int mylen = len;
	int olen;
	int b = 'X';
	int res;
	int x;
	short *buf = malloc(2 * len + cid->oldlen);
	short *obuf = buf;
	if (!buf) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(buf, 0, 2 * len + cid->oldlen);
	memcpy(buf, cid->oldstuff, cid->oldlen);
	mylen += cid->oldlen/2;
	for (x=0;x<len;x++) 
		buf[x+cid->oldlen/2] = AST_XLAW(ubuf[x]);
	while(mylen >= 160) {
		olen = mylen;
		res = fsk_serie(&cid->fskd, buf, &mylen, &b);
		if (mylen < 0) {
			ast_log(LOG_ERROR, "fsk_serie made mylen < 0 (%d)\n", mylen);
			return -1;
		}
		buf += (olen - mylen);
		if (res < 0) {
			ast_log(LOG_NOTICE, "fsk_serie failed\n");
			return -1;
		}
		if (res == 1) {
			/* Ignore invalid bytes */
			if (b > 0xff)
				continue;
			switch(cid->sawflag) {
			case 0: /* Look for flag */
				if (b == 'U')
					cid->sawflag = 2;
				break;
			case 2: /* Get lead-in */
				if ((b == 0x04) || (b == 0x80)) {
					cid->type = b;
					cid->sawflag = 3;
					cid->cksum = b;
				}
				break;
			case 3:	/* Get length */
				/* Not a lead in.  We're ready  */
				cid->sawflag = 4;
				cid->len = b;
				cid->pos = 0;
				cid->cksum += b;
				break;
			case 4: /* Retrieve message */
				if (cid->pos >= 128) {
					ast_log(LOG_WARNING, "Caller ID too long???\n");
					return -1;
				}
				cid->rawdata[cid->pos++] = b;
				cid->len--;
				cid->cksum += b;
				if (!cid->len) {
					cid->rawdata[cid->pos] = '\0';
					cid->sawflag = 5;
				}
				break;
			case 5: /* Check checksum */
				if (b != (256 - (cid->cksum & 0xff))) {
					ast_log(LOG_NOTICE, "Caller*ID failed checksum\n");
					/* Try again */
					cid->sawflag = 0;
					break;
				}
		
				cid->number[0] = '\0';
				cid->name[0] = '\0';
				/* If we get this far we're fine.  */
				if (cid->type == 0x80) {
					/* MDMF */
					/* Go through each element and process */
					for (x=0;x< cid->pos;) {
						switch(cid->rawdata[x++]) {
						case 1:
							/* Date */
							break;
						case 2: /* Number */
						case 3: /* Number (for Zebble) */
						case 4: /* Number */
							res = cid->rawdata[x];
							if (res > 32) {
								ast_log(LOG_NOTICE, "Truncating long caller ID number from %d bytes to 32\n", cid->rawdata[x]);
								res = 32; 
							}
							if (ast_strlen_zero(cid->number)) {
								memcpy(cid->number, cid->rawdata + x + 1, res);
								/* Null terminate */
								cid->number[res] = '\0';
							}
							break;
						case 6: /* Stentor Call Qualifier (ie. Long Distance call) */
							break;
						case 7: /* Name */
						case 8: /* Name */
							res = cid->rawdata[x];
							if (res > 32) {
								ast_log(LOG_NOTICE, "Truncating long caller ID name from %d bytes to 32\n", cid->rawdata[x]);
								res = 32; 
							}
							memcpy(cid->name, cid->rawdata + x + 1, res);
							cid->name[res] = '\0';
							break;
						case 17: /* UK: Call type, 1=Voice Call, 2=Ringback when free, 129=Message waiting  */
						case 19: /* UK: Network message system status (Number of messages waiting) */
						case 22: /* Something French */
							break;
						default:
							ast_log(LOG_NOTICE, "Unknown IE %d\n", cid->rawdata[x-1]);
						}
						x += cid->rawdata[x];
						x++;
					}
				} else {
					/* SDMF */
					strncpy(cid->number, cid->rawdata + 8, sizeof(cid->number)-1);
				}
				/* Update flags */
				cid->flags = 0;
				if (!strcmp(cid->number, "P")) {
					strcpy(cid->number, "");
					cid->flags |= CID_PRIVATE_NUMBER;
				} else if (!strcmp(cid->number, "O") || ast_strlen_zero(cid->number)) {
					strcpy(cid->number, "");
					cid->flags |= CID_UNKNOWN_NUMBER;
				}
				if (!strcmp(cid->name, "P")) {
					strcpy(cid->name, "");
					cid->flags |= CID_PRIVATE_NAME;
				} else if (!strcmp(cid->name, "O") || ast_strlen_zero(cid->name)) {
					strcpy(cid->name, "");
					cid->flags |= CID_UNKNOWN_NAME;
				}
				return 1;
				break;
			default:
				ast_log(LOG_ERROR, "Dunno what to do with a digit in sawflag %d\n", cid->sawflag);
			}
		}
	}
	if (mylen) {
		memcpy(cid->oldstuff, buf, mylen * 2);
		cid->oldlen = mylen * 2;
	} else
		cid->oldlen = 0;
	free(obuf);
	return 0;
}

void callerid_free(struct callerid_state *cid)
{
	free(cid);
}

static int callerid_genmsg(char *msg, int size, char *number, char *name, int flags)
{
	time_t t;
	struct tm tm;
	char *ptr;
	int res;
	int i,x;
	/* Get the time */
	time(&t);
	localtime_r(&t,&tm);
	
	ptr = msg;
	
	/* Format time and message header */
	res = snprintf(ptr, size, "\001\010%02d%02d%02d%02d", tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour, tm.tm_min);
	size -= res;
	ptr += res;
	if (!number || ast_strlen_zero(number) || (flags & CID_UNKNOWN_NUMBER)) {
		/* Indicate number not known */
		res = snprintf(ptr, size, "\004\001O");
		size -= res;
		ptr += res;
	} else if (flags & CID_PRIVATE_NUMBER) {
		/* Indicate number is private */
		res = snprintf(ptr, size, "\004\001P");
		size -= res;
		ptr += res;
	} else {
		/* Send up to 16 digits of number MAX */
		i = strlen(number);
		if (i > 16) i = 16;
		res = snprintf(ptr, size, "\002%c", i);
		size -= res;
		ptr += res;
		for (x=0;x<i;x++)
			ptr[x] = number[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}

	if (!name || ast_strlen_zero(name) || (flags & CID_UNKNOWN_NAME)) {
		/* Indicate name not known */
		res = snprintf(ptr, size, "\010\001O");
		size -= res;
		ptr += res;
	} else if (flags & CID_PRIVATE_NAME) {
		/* Indicate name is private */
		res = snprintf(ptr, size, "\010\001P");
		size -= res;
		ptr += res;
	} else {
		/* Send up to 16 digits of name MAX */
		i = strlen(name);
		if (i > 16) i = 16;
		res = snprintf(ptr, size, "\007%c", i);
		size -= res;
		ptr += res;
		for (x=0;x<i;x++)
			ptr[x] = name[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}
	return (ptr - msg);
	
}

int vmwi_generate(unsigned char *buf, int active, int mdmf, int codec)
{
	unsigned char msg[256];
	int len=0;
	int sum;
	int x;
	int bytes = 0;
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	if (mdmf) {
		/* MDMF Message waiting */
		msg[len++] = 0x82;
		/* Length is 3 */
		msg[len++] = 3;
		/* IE is "Message Waiting Parameter" */
		msg[len++] = 0xb;
		/* Length of IE is one */
		msg[len++] = 1;
		/* Active or not */
		if (active)
			msg[len++] = 0xff;
		else
			msg[len++] = 0x00;
	} else {
		/* SDMF Message waiting */
		msg[len++] = 0x6;
		/* Length is 3 */
		msg[len++] = 3;
		if (active) {
			msg[len++] = 0x42;
			msg[len++] = 0x42;
			msg[len++] = 0x42;
		} else {
			msg[len++] = 0x6f;
			msg[len++] = 0x6f;
			msg[len++] = 0x6f;
		}
	}
	sum = 0;
	for (x=0;x<len;x++)
		sum += msg[x];
	sum = (256 - (sum & 255));
	msg[len++] = sum;
	/* Wait a half a second */
	for (x=0;x<4000;x++)
		PUT_BYTE(0x7f);
	/* Transmit 30 0x55's (looks like a square wave) for channel seizure */
	for (x=0;x<30;x++)
		PUT_CLID(0x55);
	/* Send 170ms of callerid marks */
	for (x=0;x<170;x++)
		PUT_CLID_MARKMS;
	for (x=0;x<len;x++) {
		PUT_CLID(msg[x]);
	}
	/* Send 50 more ms of marks */
	for (x=0;x<50;x++)
		PUT_CLID_MARKMS;
	return bytes;
}

int callerid_generate(unsigned char *buf, char *number, char *name, int flags, int callwaiting, int codec)
{
	int bytes=0;
	int x, sum;
	int len;
	/* Initial carriers (real/imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	unsigned char msg[256];
	len = callerid_genmsg(msg, sizeof(msg), number, name, flags);
	if (!callwaiting) {
		/* Wait a half a second */
		for (x=0;x<4000;x++)
			PUT_BYTE(0x7f);
		/* Transmit 30 0x55's (looks like a square wave) for channel seizure */
		for (x=0;x<30;x++)
			PUT_CLID(0x55);
	}
	/* Send 150ms of callerid marks */
	for (x=0;x<150;x++)
		PUT_CLID_MARKMS;
	/* Send 0x80 indicating MDMF format */
	PUT_CLID(0x80);
	/* Put length of whole message */
	PUT_CLID(len);
	sum = 0x80 + strlen(msg);
	/* Put each character of message and update checksum */
	for (x=0;x<len; x++) {
		PUT_CLID(msg[x]);
		sum += msg[x];
	}
	/* Send 2's compliment of sum */
	PUT_CLID(256 - (sum & 255));

	/* Send 50 more ms of marks */
	for (x=0;x<50;x++)
		PUT_CLID_MARKMS;
	
	return bytes;
}

void ast_shrink_phone_number(char *n)
{
	int x,y=0;
	int bracketed=0;
	for (x=0;n[x];x++) {
		switch(n[x]) {
		case '[':
			bracketed++;
			n[y++] = n[x];
			break;
		case ']':
			bracketed--;
			n[y++] = n[x];
			break;
		case '-':
			if (bracketed)
				n[y++] = n[x];
			break;
		case '.':
			if (!n[x+1])
				n[y++] = n[x];
			break;
		default:
			if (!strchr("( )", n[x]))
				n[y++] = n[x];
		}
	}
	n[y] = '\0';
}

int ast_isphonenumber(char *n)
{
	int x;
	if (!n || ast_strlen_zero(n))
		return 0;
	for (x=0;n[x];x++)
		if (!strchr("0123456789*#+", n[x]))
			return 0;
	return 1;
}

int ast_callerid_parse(char *instr, char **name, char **location)
{
	char *ns, *ne;
	char *ls, *le;
	char tmp[256];
	/* Try for "name" <location> format or 
	   name <location> format */
	if ((ls = strchr(instr, '<')) && (le = strchr(ls, '>'))) {
		/* Found the location */
		*le = '\0';
		*ls = '\0';
		*location = ls + 1;
		if ((ns = strchr(instr, '\"')) && (ne = strchr(ns + 1, '\"'))) {
			/* Get name out of quotes */
			*ns = '\0';
			*ne = '\0';
			*name = ns + 1;
			return 0;
		} else {
			/* Just trim off any trailing spaces */
			*name = instr;
			while(!ast_strlen_zero(instr) && (instr[strlen(instr) - 1] < 33))
				instr[strlen(instr) - 1] = '\0';
			/* And leading spaces */
			while(**name && (**name < 33))
				(*name)++;
			return 0;
		}
	} else {
		strncpy(tmp, instr, sizeof(tmp)-1);
		ast_shrink_phone_number(tmp);
		if (ast_isphonenumber(tmp)) {
			/* Assume it's just a location */
			*name = NULL;
			*location = instr;
		} else {
			/* Assume it's just a name.  Make sure it's not quoted though */
			*name = instr;
			while(*(*name) && ((*(*name) < 33) || (*(*name) == '\"'))) (*name)++;
			ne = *name + strlen(*name) - 1;
			while((ne > *name) && ((*ne < 33) || (*ne == '\"'))) { *ne = '\0'; ne--; }
			*location = NULL;
		}
		return 0;
	}
	return -1;
}

static int __ast_callerid_generate(unsigned char *buf, char *name, char *number, int callwaiting, int codec)
{
	if (name && ast_strlen_zero(name))
		name = NULL;
	if (number && ast_strlen_zero(number))
		number = NULL;
	return callerid_generate(buf, number, name, 0, callwaiting, codec);
}

int ast_callerid_generate(unsigned char *buf, char *name, char *number, int codec)
{
	return __ast_callerid_generate(buf, name, number, 0, codec);
}

int ast_callerid_callwaiting_generate(unsigned char *buf, char *name, char *number, int codec)
{
	return __ast_callerid_generate(buf, name, number, 1, codec);
}

char *ast_callerid_merge(char *buf, int bufsiz, const char *name, const char *num, const char *unknown)
{
	if (!unknown)
		unknown = "<unknown>";
	if (name && num)
		snprintf(buf, bufsiz, "\"%s\" <%s>", name, num);
	else if (name) 
		strncpy(buf, name, bufsiz - 1);
	else if (num)
		strncpy(buf, num, bufsiz - 1);
	else
		strncpy(buf, unknown, bufsiz - 1);
	return buf;
}
int ast_callerid_split(const char *buf, char *name, int namelen, char *num, int numlen)
{
	char *tmp;
	char *l = NULL, *n = NULL;
	tmp = ast_strdupa(buf);
	if (!tmp) {
		name[0] = '\0';
		num[0] = '\0';
		return -1;
	}
	ast_callerid_parse(tmp, &n, &l);
	if (n)
		strncpy(name, n, namelen - 1);
	if (l) {
		ast_shrink_phone_number(l);
		strncpy(num, l, numlen - 1);
	}
	return 0;
}
