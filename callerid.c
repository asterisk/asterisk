/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * CallerID Generation support 
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
#include <asterisk/ulaw.h>
#include <asterisk/callerid.h>
#include <asterisk/logger.h>
#include <asterisk/fskmodem.h>
#include "sas.h"
#include "cas.h"


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

float dr[4], di[4];
float clidsb = 8000.0 / 1200.0;

#define CALLERID_SPACE	2200.0		/* 2200 hz for "0" */
#define CALLERID_MARK	1200.0		/* 1200 hz for "1" */

void callerid_init(void)
{
	/* Initialize stuff for inverse FFT */
	dr[0] = cos(CALLERID_SPACE * 2.0 * M_PI / 8000.0);
	di[0] = sin(CALLERID_SPACE * 2.0 * M_PI / 8000.0);
	dr[1] = cos(CALLERID_MARK * 2.0 * M_PI / 8000.0);
	di[1] = sin(CALLERID_MARK * 2.0 * M_PI / 8000.0);
}

struct callerid_state *callerid_new(void)
{
	struct callerid_state *cid;
	cid = malloc(sizeof(struct callerid_state));
	memset(cid, 0, sizeof(struct callerid_state));
	if (cid) {
		cid->fskd.spb = 7;		/* 1200 baud */
		cid->fskd.hdlc = 0;		/* Async */
		cid->fskd.nbit = 8;		/* 8 bits */
		cid->fskd.nstop = 1;	/* 1 stop bit */
		cid->fskd.paridad = 0;	/* No parity */
		cid->fskd.bw=1;			/* Filter 800 Hz */
		cid->fskd.f_mark_idx =  2;	/* 1200 Hz */
		cid->fskd.f_space_idx = 3;	/* 2200 Hz */
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

int ast_gen_cas(unsigned char *outbuf, int sendsas, int len)
{
	int pos = 0;
	int cnt;
	int saslen=2400;
	if (sendsas) {
		if (len < saslen)
			return -1;
		while(saslen) {
			cnt = saslen;
			if (cnt > sizeof(sas))
				cnt = sizeof(sas);
			memcpy(outbuf + pos, sas, cnt);
			pos += cnt;
			len -= cnt;
			saslen -= cnt;
		}
	}
	while(len) {
		cnt = len;
		if (cnt > sizeof(cas))
			cnt = sizeof(cas);
		memcpy(outbuf + pos, cas, cnt);
		pos += cnt;
		len -= cnt;
	}
	return 0;
}

int callerid_feed(struct callerid_state *cid, unsigned char *ubuf, int len)
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
		buf[x+cid->oldlen/2] = AST_MULAW(ubuf[x]);
	while(mylen >= 80) {
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
		
				strcpy(cid->number, "");
				strcpy(cid->name, "");
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
						case 4: /* Number */
							res = cid->rawdata[x];
							if (res > 32) {
								ast_log(LOG_NOTICE, "Truncating long caller ID number from %d bytes to 32\n", cid->rawdata[x]);
								res = 32; 
							}
							memcpy(cid->number, cid->rawdata + x + 1, res);
							/* Null terminate */
							cid->number[res] = '\0';
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
				} else if (!strcmp(cid->number, "O") || !strlen(cid->number)) {
					strcpy(cid->number, "");
					cid->flags |= CID_UNKNOWN_NUMBER;
				}
				if (!strcmp(cid->name, "P")) {
					strcpy(cid->name, "");
					cid->flags |= CID_PRIVATE_NAME;
				} else if (!strcmp(cid->name, "O") || !strlen(cid->name)) {
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

static void callerid_genmsg(char *msg, int size, char *number, char *name, int flags)
{
	time_t t;
	struct tm *tm;
	char *ptr;
	int res;
	int i,x;
	/* Get the time */
	time(&t);
	tm = localtime(&t);
	
	ptr = msg;
	
	/* Format time and message header */
	res = snprintf(ptr, size, "\001\010%02d%02d%02d%02d", tm->tm_mon + 1,
				tm->tm_mday, tm->tm_hour, tm->tm_min);
	size -= res;
	ptr += res;
	if (!number || !strlen(number) || (flags & CID_UNKNOWN_NUMBER)) {
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
		/* Send up to 10 digits of number MAX */
		i = strlen(number);
		if (i > 10) i = 10;
		res = snprintf(ptr, size, "\002%c", i);
		size -= res;
		ptr += res;
		for (x=0;x<i;x++)
			ptr[x] = number[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}

	if (!name || !strlen(name) || (flags & CID_UNKNOWN_NAME)) {
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
		/* Send up to 10 digits of number MAX */
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
	
}

int callerid_generate(unsigned char *buf, char *number, char *name, int flags, int callwaiting)
{
	int bytes=0;
	int x, sum;
	/* Initial carriers (real/imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	char msg[256];
	callerid_genmsg(msg, sizeof(msg), number, name, flags);
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
	PUT_CLID(strlen(msg));
	sum = 0x80 + strlen(msg);
	/* Put each character of message and update checksum */
	for (x=0;x<strlen(msg); x++) {
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
	for (x=0;n[x];x++)
		if (!strchr("( )-.", n[x]))
			n[y++] = n[x];
	n[y] = '\0';
}

int ast_isphonenumber(char *n)
{
	int x;
	if (!n || !strlen(n))
		return 0;
	for (x=0;n[x];x++)
		if (!strchr("0123456789", n[x]))
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
			while(strlen(instr) && (instr[strlen(instr) - 1] < 33))
				instr[strlen(instr) - 1] = '\0';
			/* And leading spaces */
			while(**name && (**name < 33))
				name++;
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
			/* Assume it's just a name */
			*name = instr;
			*location = NULL;
		}
		return 0;
	}
	return -1;
}

static int __ast_callerid_generate(unsigned char *buf, char *callerid, int callwaiting)
{
	char tmp[256];
	char *n, *l;
	if (!callerid)
		return callerid_generate(buf, NULL, NULL, 0, callwaiting);
	strncpy(tmp, callerid, sizeof(tmp)-1);
	if (ast_callerid_parse(tmp, &n, &l)) {
		ast_log(LOG_WARNING, "Unable to parse '%s' into CallerID name & number\n", callerid);
		return callerid_generate(buf, NULL, NULL, 0, callwaiting);
	}
	if (l)
		ast_shrink_phone_number(l);
	if (!ast_isphonenumber(l))
		return callerid_generate(buf, NULL, n, 0, callwaiting);
	return callerid_generate(buf, l, n, 0, callwaiting);
}

int ast_callerid_generate(unsigned char *buf, char *callerid)
{
	return __ast_callerid_generate(buf, callerid, 0);
}

int ast_callerid_callwaiting_generate(unsigned char *buf, char *callerid)
{
	return __ast_callerid_generate(buf, callerid, 1);
}
