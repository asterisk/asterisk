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

/*! \file
 *
 * \brief CallerID Generation support 
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>
#include <math.h>
#include <ctype.h>

#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/fskmodem.h"
#include "asterisk/utils.h"

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

	int skipflag; 
	unsigned short crc;
};


float cid_dr[4], cid_di[4];
float clidsb = 8000.0 / 1200.0;
float sasdr, sasdi;
float casdr1, casdi1, casdr2, casdi2;

#define CALLERID_SPACE	2200.0		/*!< 2200 hz for "0" */
#define CALLERID_MARK	1200.0		/*!< 1200 hz for "1" */
#define SAS_FREQ		 440.0
#define CAS_FREQ1		2130.0
#define CAS_FREQ2		2750.0

#define AST_CALLERID_UNKNOWN	"<unknown>"

static inline void gen_tones(unsigned char *buf, int len, int codec, float ddr1, float ddi1, float ddr2, float ddi2, float *cr1, float *ci1, float *cr2, float *ci2)
{
	int x;
	float t;
	for (x = 0; x < len; x++) {
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
	for (x = 0; x < len; x++) {
		t = *cr1 * ddr1 - *ci1 * ddi1;
		*ci1 = *cr1 * ddi1 + *ci1 * ddr1;
		*cr1 = t;
		t = 2.0 - (*cr1 * *cr1 + *ci1 * *ci1);
		*cr1 *= t;
		*ci1 *= t; 	
		buf[x] = AST_LIN2X(*cr1 * 8192.0);
	}
}

/*! \brief Initialize stuff for inverse FFT */
void callerid_init(void)
{
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

	if ((cid = ast_calloc(1, sizeof(*cid)))) {
#ifdef INTEGER_CALLERID
		cid->fskd.ispb = 7;          	/* 1200 baud */	
		/* Set up for 1200 / 8000 freq *32 to allow ints */
		cid->fskd.pllispb  = (int)(8000 * 32  / 1200);
		cid->fskd.pllids   = cid->fskd.pllispb/32;
		cid->fskd.pllispb2 = cid->fskd.pllispb/2;
		
		cid->fskd.icont = 0;           /* PLL REset */
		/* cid->fskd.hdlc = 0; */     	/* Async */
		cid->fskd.nbit = 8;           	/* 8 bits */
		cid->fskd.instop = 1;        	/* 1 stop bit */
		/* cid->fskd.paridad = 0; */  	/* No parity */
		cid->fskd.bw = 1;             	/* Filter 800 Hz */
		if (cid_signalling == 2) {    	/* v23 signalling */
			cid->fskd.f_mark_idx  = 4;	/* 1300 Hz */
			cid->fskd.f_space_idx = 5;	/* 2100 Hz */
		} else {                      	/* Bell 202 signalling as default */
			cid->fskd.f_mark_idx  = 2;	/* 1200 Hz */
			cid->fskd.f_space_idx = 3;	/* 2200 Hz */
		}
		/* cid->fskd.pcola = 0; */    	/* No clue */
		/* cid->fskd.cont = 0.0; */   	/* Digital PLL reset */
		/* cid->fskd.x0 = 0.0; */
		/* cid->fskd.state = 0; */
		cid->flags = CID_UNKNOWN_NAME | CID_UNKNOWN_NUMBER;
		/* cid->pos = 0; */

		fskmodem_init(&cid->fskd);
#else
		cid->fskd.spb = 7.0;          	/* 1200 baud */
		/* cid->fskd.hdlc = 0; */     	/* Async */
		cid->fskd.nbit = 8;           	/* 8 bits */
		cid->fskd.nstop = 1.0;        	/* 1 stop bit */
		/* cid->fskd.paridad = 0; */  	/* No parity */
		cid->fskd.bw = 1;             	/* Filter 800 Hz */
		if (cid_signalling == 2) {    	/* v23 signalling */
			cid->fskd.f_mark_idx =  4;	/* 1300 Hz */
			cid->fskd.f_space_idx = 5;	/* 2100 Hz */
		} else {                      	/* Bell 202 signalling as default */
			cid->fskd.f_mark_idx =  2;	/* 1200 Hz */
			cid->fskd.f_space_idx = 3;	/* 2200 Hz */
		}
		/* cid->fskd.pcola = 0; */    	/* No clue */
		/* cid->fskd.cont = 0.0; */   	/* Digital PLL reset */
		/* cid->fskd.x0 = 0.0; */
		/* cid->fskd.state = 0; */
		cid->flags = CID_UNKNOWN_NAME | CID_UNKNOWN_NUMBER;
		/* cid->pos = 0; */
#endif
	}

	return cid;
}

void callerid_get(struct callerid_state *cid, char **name, char **number, int *flags)
{
	*flags = cid->flags;
	if (cid->flags & (CID_UNKNOWN_NAME | CID_PRIVATE_NAME))
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
		ast_debug(1, "No cid detected\n");
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
			ast_debug(1, "Unknown DTMF code %d\n", code);
	} else if (cidstring[0] == 'D' && cidstring[2] == '#') {
		/* .DK special code */
		if (cidstring[1] == '1')
			*flags = CID_PRIVATE_NUMBER;
		if (cidstring[1] == '2' || cidstring[1] == '3')
			*flags = CID_UNKNOWN_NUMBER;
	} else if (cidstring[0] == 'D' || cidstring[0] == 'A') {
		/* "Standard" callerid */
		for (i = 1; i < strlen(cidstring); i++) {
			if (cidstring[i] == 'C' || cidstring[i] == '#')
				break;
			if (isdigit(cidstring[i]))
				number[i-1] = cidstring[i];
			else
				ast_debug(1, "Unknown CID digit '%c'\n",
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
		ast_debug(1, "Unknown CID protocol, start digit '%c'\n", cidstring[0]);
		*flags = CID_UNKNOWN_NUMBER;
	}
}

int ast_gen_cas(unsigned char *outbuf, int sendsas, int len, int codec)
{
	int pos = 0;
	int saslen = 2400;
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

static unsigned short calc_crc(unsigned short crc, unsigned char data)
{
	unsigned int i, j, org, dst;
	org = data;
	dst = 0;

	for (i = 0; i < CHAR_BIT; i++) {
		org <<= 1;
		dst >>= 1;
		if (org & 0x100) 
			dst |= 0x80;
	}
	data = (unsigned char) dst;
	crc ^= (unsigned int) data << (16 - CHAR_BIT);
	for (j = 0; j < CHAR_BIT; j++) {
		if (crc & 0x8000U)
			crc = (crc << 1) ^ 0x1021U ;
		else
			crc <<= 1 ;
	}
   	return crc;
}

int callerid_feed_jp(struct callerid_state *cid, unsigned char *ubuf, int len, int codec)
{
	int mylen = len;
	int olen;
	int b = 'X';
	int b2;
	int res;
	int x;
	short *buf;

	buf = alloca(2 * len + cid->oldlen);

	memcpy(buf, cid->oldstuff, cid->oldlen);
	mylen += cid->oldlen / 2;

	for (x = 0; x < len; x++) 
		buf[x+cid->oldlen/2] = AST_XLAW(ubuf[x]);

	while (mylen >= 160) {
		b = b2 = 0;
		olen = mylen;
		res = fsk_serial(&cid->fskd, buf, &mylen, &b);

		if (mylen < 0) {
			ast_log(LOG_ERROR, "No start bit found in fsk data.\n");
			return -1;
		}

		buf += (olen - mylen);

		if (res < 0) {
			ast_log(LOG_NOTICE, "fsk_serial failed\n");
			return -1;
		}

		if (res == 1) {
			b2 = b;
			b  &= 0x7f;

			/* crc checksum calculation */
			if (cid->sawflag > 1)
				cid->crc = calc_crc(cid->crc, (unsigned char) b2);

			/* Ignore invalid bytes */
			if (b > 0xff)
				continue;

			/* skip DLE if needed */
			if (cid->sawflag > 0) {
				if (cid->sawflag != 5 && cid->skipflag == 0 && b == 0x10) {
					cid->skipflag = 1 ;
					continue ;
				}
			}
			if (cid->skipflag == 1)
				cid->skipflag = 0 ;

			/* caller id retrieval */
			switch (cid->sawflag) {
			case 0: /* DLE */
				if (b == 0x10) {
					cid->sawflag = 1;
					cid->skipflag = 0;
					cid->crc = 0;
				}
				break;
			case 1: /* SOH */
				if (b == 0x01) 
					cid->sawflag = 2;
				break ;
			case 2: /* HEADER */
				if (b == 0x07) 
					cid->sawflag = 3;
				break;
			case 3: /* STX */
				if (b == 0x02) 
					cid->sawflag = 4;
				break;
			case 4: /* SERVICE TYPE */
				if (b == 0x40) 
					cid->sawflag = 5;
				break;
			case 5: /* Frame Length */
				cid->sawflag = 6;
				break;	
			case 6: /* NUMBER TYPE */
				cid->sawflag = 7;
				cid->pos = 0;
				cid->rawdata[cid->pos++] = b;
				break;
			case 7:	/* NUMBER LENGTH */
				cid->sawflag = 8;
				cid->len = b;
				if ((cid->len+2) >= sizeof(cid->rawdata)) {
					ast_log(LOG_WARNING, "too long caller id string\n") ;
					return -1;
				}
				cid->rawdata[cid->pos++] = b;
				break;
			case 8:	/* Retrieve message */
				cid->rawdata[cid->pos++] = b;
				cid->len--;
				if (cid->len<=0) {
					cid->rawdata[cid->pos] = '\0';
					cid->sawflag = 9;
				}
				break;
			case 9:	/* ETX */
				cid->sawflag = 10;
				break;
			case 10: /* CRC Checksum 1 */
				cid->sawflag = 11;
				break;
			case 11: /* CRC Checksum 2 */
				cid->sawflag = 12;
				if (cid->crc != 0) {
					ast_log(LOG_WARNING, "crc checksum error\n") ;
					return -1;
				} 
				/* extract caller id data */
				for (x = 0; x < cid->pos;) {
					switch (cid->rawdata[x++]) {
					case 0x02: /* caller id  number */
						cid->number[0] = '\0';
						cid->name[0] = '\0';
						cid->flags = 0;
						res = cid->rawdata[x++];
						ast_copy_string(cid->number, &cid->rawdata[x], res+1);
						x += res;
						break;
					case 0x21: /* additional information */
						/* length */
						x++; 
						/* number type */
						switch (cid->rawdata[x]) { 
						case 0x00: /* unknown */
						case 0x01: /* international number */
						case 0x02: /* domestic number */
						case 0x03: /* network */
						case 0x04: /* local call */
						case 0x06: /* short dial number */
						case 0x07: /* reserved */
						default:   /* reserved */
							ast_debug(2, "cid info:#1=%X\n", cid->rawdata[x]);
							break ;
						}
						x++; 
						/* numbering plan octed 4 */
						x++; 
						/* numbering plan octed 5 */
						switch (cid->rawdata[x]) { 
						case 0x00: /* unknown */
						case 0x01: /* recommendation E.164 ISDN */
						case 0x03: /* recommendation X.121 */
						case 0x04: /* telex dial plan */
						case 0x08: /* domestic dial plan */
						case 0x09: /* private dial plan */
						case 0x05: /* reserved */
						default:   /* reserved */
							ast_debug(2, "cid info:#2=%X\n", cid->rawdata[x]);
							break ;
						}
						x++; 
						break ;
					case 0x04: /* no callerid reason */
						/* length */
						x++; 
						/* no callerid reason code */
						switch (cid->rawdata[x]) {
						case 'P': /* caller id denied by user */
						case 'O': /* service not available */
						case 'C': /* pay phone */
						case 'S': /* service congested */
							cid->flags |= CID_UNKNOWN_NUMBER;
							ast_debug(2, "no cid reason:%c\n", cid->rawdata[x]);
							break ;
						}
						x++; 
						break ;
					case 0x09: /* dialed number */
						/* length */
						res = cid->rawdata[x++];
						/* dialed number */
						x += res;
						break ;
					case 0x22: /* dialed number additional information */
						/* length */
						x++;
						/* number type */
						switch (cid->rawdata[x]) {
						case 0x00: /* unknown */
						case 0x01: /* international number */
						case 0x02: /* domestic number */
						case 0x03: /* network */
						case 0x04: /* local call */
						case 0x06: /* short dial number */
						case 0x07: /* reserved */
						default:   /* reserved */
							if (option_debug > 1)
								ast_log(LOG_NOTICE, "did info:#1=%X\n", cid->rawdata[x]);
							break ;
						}
						x++;
						/* numbering plan octed 4 */
						x++;
						/* numbering plan octed 5 */
						switch (cid->rawdata[x]) {
						case 0x00: /* unknown */
						case 0x01: /* recommendation E.164 ISDN */
						case 0x03: /* recommendation X.121 */
						case 0x04: /* telex dial plan */
						case 0x08: /* domestic dial plan */
						case 0x09: /* private dial plan */
						case 0x05: /* reserved */
						default:   /* reserved */
							ast_debug(2, "did info:#2=%X\n", cid->rawdata[x]);
							break ;
						}
						x++;
						break ;
					}
				}
				return 1;
				break;
			default:
				ast_log(LOG_ERROR, "invalid value in sawflag %d\n", cid->sawflag);
			}
		}
	}
	if (mylen) {
		memcpy(cid->oldstuff, buf, mylen * 2);
		cid->oldlen = mylen * 2;
	} else
		cid->oldlen = 0;
	
	return 0;
}


int callerid_feed(struct callerid_state *cid, unsigned char *ubuf, int len, int codec)
{
	int mylen = len;
	int olen;
	int b = 'X';
	int res;
	int x;
	short *buf;

	buf = alloca(2 * len + cid->oldlen);

	memcpy(buf, cid->oldstuff, cid->oldlen);
	mylen += cid->oldlen/2;

	for (x = 0; x < len; x++) 
		buf[x+cid->oldlen/2] = AST_XLAW(ubuf[x]);
	while (mylen >= 160) {
		olen = mylen;
		res = fsk_serial(&cid->fskd, buf, &mylen, &b);
		if (mylen < 0) {
			ast_log(LOG_ERROR, "No start bit found in fsk data.\n");
			return -1;
		}
		buf += (olen - mylen);
		if (res < 0) {
			ast_log(LOG_NOTICE, "fsk_serial failed\n");
			return -1;
		}
		if (res == 1) {
			if (b > 0xff) {
				if (cid->sawflag != 5) {
					/* Ignore invalid bytes */
					continue;
				}
				/*
				 * We can tollerate an error on the checksum character since the
				 * checksum character is the last character in the message and
				 * it validates the message.
				 *
				 * Remove character error flags.
				 * Bit 8 : Parity error
				 * Bit 9 : Framing error
				 */
				b &= 0xff;
			}
			switch (cid->sawflag) {
			case 0: /* Look for flag */
				if (b == 'U')
					cid->sawflag = 2;
				break;
			case 2: /* Get lead-in */
				if ((b == 0x04) || (b == 0x80) || (b == 0x06) || (b == 0x82)) {
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
				/* Update flags */
				cid->flags = 0;
				/* If we get this far we're fine.  */
				if ((cid->type == 0x80) || (cid->type == 0x82)) {
					/* MDMF */
					/* Go through each element and process */
					for (x = 0; x < cid->pos;) {
						switch (cid->rawdata[x++]) {
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
						case 11: /* Message Waiting */
							res = cid->rawdata[x + 1];
							if (res)
								cid->flags |= CID_MSGWAITING;
							else
								cid->flags |= CID_NOMSGWAITING;
							break;
						case 17: /* UK: Call type, 1=Voice Call, 2=Ringback when free, 129=Message waiting  */
						case 19: /* UK: Network message system status (Number of messages waiting) */
						case 22: /* Something French */
							break;
						default:
							ast_log(LOG_NOTICE, "Unknown IE %d\n", cid->rawdata[x - 1]);
						}
						res = cid->rawdata[x];
						if (0 > res){	/* Negative offset in the CID Spill */
							ast_log(LOG_NOTICE, "IE %d has bad field length of %d at offset %d\n", cid->rawdata[x-1], cid->rawdata[x], x);
							/* Try again */
							cid->sawflag = 0;
							break; 	/* Exit the loop */
						}
						x += cid->rawdata[x];
						x++;
					}
				} else if (cid->type == 0x6) {
					/* VMWI SDMF */
					if (cid->rawdata[2] == 0x42) {
						cid->flags |= CID_MSGWAITING;
					} else if (cid->rawdata[2] == 0x6f) {
						cid->flags |= CID_NOMSGWAITING;
					}
				} else {
					/* SDMF */
					ast_copy_string(cid->number, cid->rawdata + 8, sizeof(cid->number));
				}
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

	return 0;
}

void callerid_free(struct callerid_state *cid)
{
	ast_free(cid);
}

static int callerid_genmsg(char *msg, int size, const char *number, const char *name, int flags)
{
	struct timeval now = ast_tvnow();
	struct ast_tm tm;
	char *ptr;
	int res;
	int i, x;

	/* Get the time */
	ast_localtime(&now, &tm, NULL);
	
	ptr = msg;
	
	/* Format time and message header */
	res = snprintf(ptr, size, "\001\010%02d%02d%02d%02d", tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour, tm.tm_min);
	size -= res;
	ptr += res;
	if (ast_strlen_zero(number) || (flags & CID_UNKNOWN_NUMBER)) {
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
		if (i > 16)
			i = 16;
		res = snprintf(ptr, size, "\002%c", i);
		size -= res;
		ptr += res;
		for (x = 0; x < i; x++)
			ptr[x] = number[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}

	if (ast_strlen_zero(name) || (flags & CID_UNKNOWN_NAME)) {
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
		if (i > 16)
			i = 16;
		res = snprintf(ptr, size, "\007%c", i);
		size -= res;
		ptr += res;
		for (x = 0; x < i; x++)
			ptr[x] = name[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}
	return (ptr - msg);
	
}

int vmwi_generate(unsigned char *buf, int active, int type, int codec,
				  const char* name, const char* number, int flags)
{
	char msg[256];
	int len = 0;
	int sum;
	int x;
	int bytes = 0;
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	
	if (type == CID_MWI_TYPE_MDMF_FULL) {
		/* MDMF Message waiting with date, number, name and MWI parameter */
		msg[0] = 0x82;

		/* put date, number info at the right place */
		len = callerid_genmsg(msg+2, sizeof(msg)-2, number, name, flags); 
		
		/* length of MDMF CLI plus Message Waiting Structure */
		msg[1] = len+3;
		
		/* Go to the position to write to */
		len = len+2;
		
		/* "Message Waiting Parameter" */
		msg[len++] = 0x0b;
		/* Length of IE is one */
		msg[len++] = 1;
		/* Active or not */
		if (active)
			msg[len++] = 0xff;
		else
			msg[len++] = 0x00;
		
	} else if (type == CID_MWI_TYPE_MDMF) {
		/* MDMF Message waiting only */
		/* same as above except that the we only put MWI parameter */
		msg[len++] = 0x82;
		/* Length is 3 */
		msg[len++] = 3;
		/* IE is "Message Waiting Parameter" */
		msg[len++] = 0x0b;
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
	for (x = 0; x < len; x++)
		sum += msg[x];
	sum = (256 - (sum & 255));
	msg[len++] = sum;
	/* Wait a half a second */
	for (x = 0; x < 4000; x++)
		PUT_BYTE(0x7f);
	/* Transmit 30 0x55's (looks like a square wave) for channel seizure */
	for (x = 0; x < 30; x++)
		PUT_CLID(0x55);
	/* Send 170ms of callerid marks */
	for (x = 0; x < 170; x++)
		PUT_CLID_MARKMS;
	for (x = 0; x < len; x++) {
		PUT_CLID(msg[x]);
	}
	/* Send 50 more ms of marks */
	for (x = 0; x < 50; x++)
		PUT_CLID_MARKMS;
	return bytes;
}

int callerid_generate(unsigned char *buf, const char *number, const char *name, int flags, int callwaiting, int codec)
{
	int bytes = 0;
	int x, sum;
	int len;

	/* Initial carriers (real/imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	char msg[256];
	len = callerid_genmsg(msg, sizeof(msg), number, name, flags);
	if (!callwaiting) {
		/* Wait a half a second */
		for (x = 0; x < 4000; x++)
			PUT_BYTE(0x7f);
		/* Transmit 30 0x55's (looks like a square wave) for channel seizure */
		for (x = 0; x < 30; x++)
			PUT_CLID(0x55);
	}
	/* Send 150ms of callerid marks */
	for (x = 0; x < 150; x++)
		PUT_CLID_MARKMS;
	/* Send 0x80 indicating MDMF format */
	PUT_CLID(0x80);
	/* Put length of whole message */
	PUT_CLID(len);
	sum = 0x80 + strlen(msg);
	/* Put each character of message and update checksum */
	for (x = 0; x < len; x++) {
		PUT_CLID(msg[x]);
		sum += msg[x];
	}
	/* Send 2's compliment of sum */
	PUT_CLID(256 - (sum & 255));

	/* Send 50 more ms of marks */
	for (x = 0; x < 50; x++)
		PUT_CLID_MARKMS;
	
	return bytes;
}

/*! \brief Clean up phone string
 * remove '(', ' ', ')', non-trailing '.', and '-' not in square brackets.
 * Basically, remove anything that could be invalid in a pattern.
 */
void ast_shrink_phone_number(char *n)
{
	int x, y = 0;
	int bracketed = 0;

	for (x = 0; n[x]; x++) {
		switch (n[x]) {
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
			/* ignore parenthesis and whitespace */
			if (!strchr("( )", n[x]))
				n[y++] = n[x];
		}
	}
	n[y] = '\0';
}

/*! \brief Checks if phone number consists of valid characters 
	\param exten	String that needs to be checked
	\param valid	Valid characters in string
	\return 1 if valid string, 0 if string contains invalid characters
*/
static int ast_is_valid_string(const char *exten, const char *valid)
{
	int x;

	if (ast_strlen_zero(exten))
		return 0;
	for (x = 0; exten[x]; x++)
		if (!strchr(valid, exten[x]))
			return 0;
	return 1;
}

/*! \brief checks if string consists only of digits and * \# and + 
	\return 1 if string is valid AST phone number
	\return 0 if not
*/
int ast_isphonenumber(const char *n)
{
	return ast_is_valid_string(n, "0123456789*#+");
}

/*! \brief checks if string consists only of digits and ( ) - * \# and + 
	Pre-qualifies the string for ast_shrink_phone_number()
	\return 1 if string is valid AST shrinkable phone number
	\return 0 if not
*/
int ast_is_shrinkable_phonenumber(const char *exten)
{
	return ast_is_valid_string(exten, "0123456789*#+()-.");
}

/*!
 * \brief Destructively parse instr for caller id information 
 * \return always returns 0, as the code always returns something.
 * \note XXX 'name' is not parsed consistently e.g. we have
 * input                   location        name
 * " foo bar " <123>       123             ' foo bar ' (with spaces around)
 * " foo bar "             NULL            'foo bar' (without spaces around)
 * The parsing of leading and trailing space/quotes should be more consistent.
 */
int ast_callerid_parse(char *instr, char **name, char **location)
{
	char *ns, *ne, *ls, *le;

	/* Try "name" <location> format or name <location> format */
	if ((ls = strrchr(instr, '<')) && (le = strrchr(ls, '>'))) {
		*ls = *le = '\0';	/* location found, trim off the brackets */
		*location = ls + 1;	/* and this is the result */
		if ((ns = strchr(instr, '"')) && (ne = strchr(ns + 1, '"'))) {
			*ns = *ne = '\0';	/* trim off the quotes */
			*name = ns + 1;		/* and this is the name */
		} else if (ns) {
			/* An opening quote was found but no closing quote was. The closing
			 * quote may actually be after the end of the bracketed number
			 */
			if (strchr(le + 1, '\"')) {
				*ns = '\0';
				*name = ns + 1;
				ast_trim_blanks(*name);
			} else {
				*name = NULL;
			}
		} else { /* no quotes, trim off leading and trailing spaces */
			*name = ast_skip_blanks(instr);
			ast_trim_blanks(*name);
		}
	} else {	/* no valid brackets */
		char tmp[256];

		ast_copy_string(tmp, instr, sizeof(tmp));
		ast_shrink_phone_number(tmp);
		if (ast_isphonenumber(tmp)) {	/* Assume it's just a location */
			*name = NULL;
			strcpy(instr, tmp); /* safe, because tmp will always be the same size or smaller than instr */
			*location = instr;
		} else { /* Assume it's just a name. */
			*location = NULL;
			if ((ns = strchr(instr, '"')) && (ne = strchr(ns + 1, '"'))) {
				*ns = *ne = '\0';	/* trim off the quotes */
				*name = ns + 1;		/* and this is the name */
			} else { /* no quotes, trim off leading and trailing spaces */
				*name = ast_skip_blanks(instr);
				ast_trim_blanks(*name);
			}
		}
	}
	return 0;
}

static int __ast_callerid_generate(unsigned char *buf, const char *name, const char *number, int callwaiting, int codec)
{
	if (ast_strlen_zero(name))
		name = NULL;
	if (ast_strlen_zero(number))
		number = NULL;
	return callerid_generate(buf, number, name, 0, callwaiting, codec);
}

int ast_callerid_generate(unsigned char *buf, const char *name, const char *number, int codec)
{
	return __ast_callerid_generate(buf, name, number, 0, codec);
}

int ast_callerid_callwaiting_generate(unsigned char *buf, const char *name, const char *number, int codec)
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
		ast_copy_string(buf, name, bufsiz);
	else if (num)
		ast_copy_string(buf, num, bufsiz);
	else
		ast_copy_string(buf, unknown, bufsiz);
	return buf;
}

int ast_callerid_split(const char *buf, char *name, int namelen, char *num, int numlen)
{
	char *tmp;
	char *l = NULL, *n = NULL;

	tmp = ast_strdupa(buf);
	ast_callerid_parse(tmp, &n, &l);
	if (n)
		ast_copy_string(name, n, namelen);
	else
		name[0] = '\0';
	if (l) {
		ast_shrink_phone_number(l);
		ast_copy_string(num, l, numlen);
	} else
		num[0] = '\0';
	return 0;
}

/*! \brief Translation table for Caller ID Presentation settings */
static struct {
	int val;
	const char *name;
	const char *description;
} pres_types[] = {
	{  AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED, "allowed_not_screened", "Presentation Allowed, Not Screened"},
	{  AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, "allowed_passed_screen", "Presentation Allowed, Passed Screen"},
	{  AST_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN, "allowed_failed_screen", "Presentation Allowed, Failed Screen"},
	{  AST_PRES_ALLOWED_NETWORK_NUMBER, "allowed", "Presentation Allowed, Network Number"},
	{  AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED, "prohib_not_screened", "Presentation Prohibited, Not Screened"},
	{  AST_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN, "prohib_passed_screen", "Presentation Prohibited, Passed Screen"},
	{  AST_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN, "prohib_failed_screen", "Presentation Prohibited, Failed Screen"},
	{  AST_PRES_PROHIB_NETWORK_NUMBER, "prohib", "Presentation Prohibited, Network Number"},
	{  AST_PRES_NUMBER_NOT_AVAILABLE, "unavailable", "Number Unavailable"},
};

/*! \brief Convert caller ID text code to value 
	used in config file parsing
	\param data text string
	\return value AST_PRES_ from callerid.h 
*/
int ast_parse_caller_presentation(const char *data)
{
	int i;
	if (!data) {
		return -1;
	}

	for (i = 0; i < ARRAY_LEN(pres_types); i++) {
		if (!strcasecmp(pres_types[i].name, data))
			return pres_types[i].val;
	}

	return -1;
}

/*! \brief Convert caller ID pres value to explanatory string 
	\param data value (see callerid.h AST_PRES_ ) 
	\return string for human presentation
*/
const char *ast_describe_caller_presentation(int data)
{
	int i;

	for (i = 0; i < ARRAY_LEN(pres_types); i++) {
		if (pres_types[i].val == data)
			return pres_types[i].description;
	}

	return "unknown";
}

/*! \brief Convert caller ID pres value to text code
	\param data text string
	\return string for config file
*/
const char *ast_named_caller_presentation(int data)
{
	int i;

	for (i = 0; i < ARRAY_LEN(pres_types); i++) {
		if (pres_types[i].val == data)
			return pres_types[i].name;
	}

	return "unknown";
}
