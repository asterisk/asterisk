/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * TTY/TDD Generation support 
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
#include <ctype.h>

#include "asterisk/ulaw.h"
#include "asterisk/tdd.h"
#include "asterisk/logger.h"
#include "asterisk/fskmodem.h"
#include "ecdisa.h"

struct tdd_state {
	fsk_data fskd;
	char rawdata[256];
	short oldstuff[4096];
	int oldlen;
	int pos;
	int modo;
	int mode;
};

static float dr[4], di[4];
static float tddsb = 176.0;  /* 45.5 baud */

#define TDD_SPACE	1800.0		/* 1800 hz for "0" */
#define TDD_MARK	1400.0		/* 1400 hz for "1" */

static int tdd_decode_baudot(struct tdd_state *tdd,unsigned char data)	/* covert baudot into ASCII */
{
	static char ltrs[32]={'<','E','\n','A',' ','S','I','U',
				'\n','D','R','J','N','F','C','K',
				'T','Z','L','W','H','Y','P','Q',
				'O','B','G','^','M','X','V','^'};
	static char figs[32]={'<','3','\n','-',' ',',','8','7',
				'\n','$','4','\'',',','·',':','(',
				'5','+',')','2','·','6','0','1',
				'9','7','·','^','.','/','=','^'};
	int d;
	d=0;  /* return 0 if not decodeable */
	switch (data) {
	case 0x1f :	tdd->modo=0; break;
	case 0x1b : tdd->modo=1; break;
	default:	if (tdd->modo==0) d=ltrs[data]; else d=figs[data]; break;
	}
	return d;
}

void tdd_init(void)
{
	/* Initialize stuff for inverse FFT */
	dr[0] = cos(TDD_SPACE * 2.0 * M_PI / 8000.0);
	di[0] = sin(TDD_SPACE * 2.0 * M_PI / 8000.0);
	dr[1] = cos(TDD_MARK * 2.0 * M_PI / 8000.0);
	di[1] = sin(TDD_MARK * 2.0 * M_PI / 8000.0);
}

struct tdd_state *tdd_new(void)
{
	struct tdd_state *tdd;
	tdd = malloc(sizeof(struct tdd_state));
	if (tdd) {
		memset(tdd, 0, sizeof(struct tdd_state));
		tdd->fskd.spb = 176;		/* 45.5 baud */
		tdd->fskd.hdlc = 0;		/* Async */
		tdd->fskd.nbit = 5;		/* 5 bits */
		tdd->fskd.nstop = 1.5;	/* 1.5 stop bits */
		tdd->fskd.paridad = 0;	/* No parity */
		tdd->fskd.bw=0;			/* Filter 75 Hz */
		tdd->fskd.f_mark_idx =  0;	/* 1400 Hz */
		tdd->fskd.f_space_idx = 1;	/* 1800 Hz */
		tdd->fskd.pcola = 0;		/* No clue */
		tdd->fskd.cont = 0;			/* Digital PLL reset */
		tdd->fskd.x0 = 0.0;
		tdd->fskd.state = 0;
		tdd->pos = 0;
		tdd->mode = 2;
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tdd;
}

int ast_tdd_gen_ecdisa(unsigned char *outbuf, int len)
{
	int pos = 0;
	int cnt;
	while(len) {
		cnt = len;
		if (cnt > sizeof(ecdisa))
			cnt = sizeof(ecdisa);
		memcpy(outbuf + pos, ecdisa, cnt);
		pos += cnt;
		len -= cnt;
	}
	return 0;
}

int tdd_feed(struct tdd_state *tdd, unsigned char *ubuf, int len)
{
	int mylen = len;
	int olen;
	int b = 'X';
	int res;
	int c,x;
	short *buf = malloc(2 * len + tdd->oldlen);
	short *obuf = buf;
	if (!buf) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(buf, 0, 2 * len + tdd->oldlen);
	memcpy(buf, tdd->oldstuff, tdd->oldlen);
	mylen += tdd->oldlen/2;
	for (x=0;x<len;x++) 
		buf[x+tdd->oldlen/2] = AST_MULAW(ubuf[x]);
	c = res = 0;
	while(mylen >= 1320) { /* has to have enough to work on */
		olen = mylen;
		res = fsk_serie(&tdd->fskd, buf, &mylen, &b);
		if (mylen < 0) {
			ast_log(LOG_ERROR, "fsk_serie made mylen < 0 (%d) (olen was %d)\n", mylen,olen);
			free(obuf);
			return -1;
		}
		buf += (olen - mylen);
		if (res < 0) {
			ast_log(LOG_NOTICE, "fsk_serie failed\n");
			free(obuf);
			return -1;
		}
		if (res == 1) {
			/* Ignore invalid bytes */
			if (b > 0x7f)
				continue;
			c = tdd_decode_baudot(tdd,b);
			if ((c < 1) || (c > 126)) continue; /* if not valid */
			break;
		}
	}
	if (mylen) {
		memcpy(tdd->oldstuff, buf, mylen * 2);
		tdd->oldlen = mylen * 2;
	} else
		tdd->oldlen = 0;
	free(obuf);
	if (res)  {
		tdd->mode = 2; /* put it in mode where it
			reliably puts teleprinter in correct shift mode */
		return(c);
	}
	return 0;
}

void tdd_free(struct tdd_state *tdd)
{
	free(tdd);
}

static inline float tdd_getcarrier(float *cr, float *ci, int bit)
{
	/* Move along.  There's nothing to see here... */
	float t;
	t = *cr * dr[bit] - *ci * di[bit];
	*ci = *cr * di[bit] + *ci * dr[bit];
	*cr = t;
	
	t = 2.0 - (*cr * *cr + *ci * *ci);
	*cr *= t;
	*ci *= t;
	return *cr;
}	

#define PUT_BYTE(a) do { \
	*(buf++) = (a); \
	bytes++; \
} while(0)

#define PUT_AUDIO_SAMPLE(y) do { \
	int index = (short)(rint(8192.0 * (y))); \
	*(buf++) = AST_LIN2MU(index); \
	bytes++; \
} while(0)
	
#define PUT_TDD_MARKMS do { \
	int x; \
	for (x=0;x<8;x++) \
		PUT_AUDIO_SAMPLE(tdd_getcarrier(&cr, &ci, 1)); \
} while(0)

#define PUT_TDD_BAUD(bit) do { \
	while(scont < tddsb) { \
		PUT_AUDIO_SAMPLE(tdd_getcarrier(&cr, &ci, bit)); \
		scont += 1.0; \
	} \
	scont -= tddsb; \
} while(0)

#define PUT_TDD_STOP do { \
	while(scont < (tddsb * 1.5)) { \
		PUT_AUDIO_SAMPLE(tdd_getcarrier(&cr, &ci, 1)); \
		scont += 1.0; \
	} \
	scont -= (tddsb * 1.5); \
} while(0)


#define PUT_TDD(byte) do { \
	int z; \
	unsigned char b = (byte); \
	PUT_TDD_BAUD(0); 	/* Start bit */ \
	for (z=0;z<5;z++) { \
		PUT_TDD_BAUD(b & 1); \
		b >>= 1; \
	} \
	PUT_TDD_STOP;	/* Stop bit */ \
} while(0);	

int tdd_generate(struct tdd_state *tdd, unsigned char *buf, const char *str)
{
	int bytes=0;
	int i,x;
	char	c;
	static unsigned char lstr[31] = "\000E\nA SIU\rDRJNFCKTZLWHYPQOBG\000MXV";
	static unsigned char fstr[31] = "\0003\n- \00787\r$4',!:(5\")2\0006019?&\000./;";
	/* Initial carriers (real/imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;

	for(x = 0; str[x]; x++) {
		c = toupper(str[x]);
#if	0
		printf("%c",c); fflush(stdout);
#endif
		if (c == 0) /* send null */
		   {
			PUT_TDD(0);
			continue;
		   }
		if (c == '\r') /* send c/r */
		   {
			PUT_TDD(8);
			continue;
		   }
		if (c == '\n') /* send c/r and l/f */
		   {
			PUT_TDD(8);
			PUT_TDD(2);
			continue;
		   }
		if (c == ' ') /* send space */
		   {
			PUT_TDD(4);
			continue;
		   }
		for(i = 0; i < 31; i++)
		   {
			if (lstr[i] == c) break;
		   }
		if (i < 31) /* if we found it */
		   {
			if (tdd->mode)  /* if in figs mode, change it */
			   { 
				PUT_TDD(31); /* Send LTRS */
				tdd->mode = 0;
			   }
			PUT_TDD(i);
			continue;
		   }
		for(i = 0; i < 31; i++)
		   {
			if (fstr[i] == c) break;
		   }
		if (i < 31) /* if we found it */
		   {
			if (tdd->mode != 1)  /* if in ltrs mode, change it */
			   {
				PUT_TDD(27); /* send FIGS */
				tdd->mode = 1;
			   }
			PUT_TDD(i);  /* send byte */
			continue;
		   }
	   }
	return bytes;
}


