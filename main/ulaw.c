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
 * \brief u-Law to Signed linear conversion
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/ulaw.h"
#include "asterisk/logger.h"

#if 0
/* ZEROTRAP is the military recommendation to improve the encryption
 * of u-Law traffic. It is irrelevant with modern encryption systems
 * like AES, and will simply degrade the signal quality.
 * ZEROTRAP is not implemented in AST_LIN2MU and so the coding table
 * tests will fail if you use it */
#define ZEROTRAP    /*!< turn on the trap as per the MIL-STD */
#endif

#define BIAS 0x84   /*!< define the add-in bias for 16 bit samples */
#define CLIP 32635

#ifndef G711_NEW_ALGORITHM

unsigned char __ast_lin2mu[16384];
short __ast_mulaw[256];

static unsigned char linear2ulaw(short sample)
{
	static int exp_lut[256] = {
		0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
		4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7 };
	int sign, exponent, mantissa;
	unsigned char ulawbyte;

	/* Get the sample into sign-magnitude. */
	sign = (sample >> 8) & 0x80;          /* set aside the sign */
	if (sign != 0)
		sample = -sample;              /* get magnitude */
	if (sample > CLIP)
		sample = CLIP;             /* clip the magnitude */

	/* Convert from 16 bit linear to ulaw. */
	sample = sample + BIAS;
	exponent = exp_lut[(sample >> 7) & 0xFF];
	mantissa = (sample >> (exponent + 3)) & 0x0F;
	ulawbyte = ~(sign | (exponent << 4) | mantissa);

#ifdef ZEROTRAP
	if (ulawbyte == 0)
		ulawbyte = 0x02;   /* optional CCITT trap */
#endif

	return ulawbyte;
}

#else

unsigned char __ast_lin2mu[AST_ULAW_TAB_SIZE];
short __ast_mulaw[256];

static unsigned char linear2ulaw(short sample, int full_coding)
{
	static const unsigned exp_lut[256] = {
		0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
		4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
		5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7 };
	unsigned sign, exponent, mantissa, mag;
	unsigned char ulawbyte;

	/* Get the sample into sign-magnitude. */
	ast_ulaw_get_sign_mag(sample, &sign, &mag);
	if (mag > CLIP)
		mag = CLIP;                /* clip the magnitude */

	sign = (sample >> 8) & 0x80;          /* set aside the sign */
	if (sign != 0)
		sample = -sample;              /* get magnitude */
	if (sample > CLIP)
		sample = CLIP;             /* clip the magnitude */

	/* Convert from 16 bit linear to ulaw. */
	mag += BIAS;
	exponent = exp_lut[(mag >> 7) & 0xFF];
	mantissa = (mag >> (exponent + 3)) & 0x0F;

	if (full_coding) {
		/* full encoding, with sign and xform */
		ulawbyte = ~(sign | (exponent << 4) | mantissa);
#ifdef ZEROTRAP
		if (ulawbyte == 0)
			ulawbyte = 0x02;   /* optional CCITT trap */
#endif
	} else {
		/* half-cooked coding -- mantissa+exponent only (for lookup tab) */
		ulawbyte = (exponent << 4) | mantissa;
	}

	return ulawbyte;
}

static inline short ulaw2linear(unsigned char ulawbyte)
{
	unsigned exponent, mantissa;
	short sample;
	static const short etab[]={0,132,396,924,1980,4092,8316,16764};

	ulawbyte = ~ulawbyte;
	exponent = (ulawbyte & 0x70) >> 4;
	mantissa = ulawbyte & 0x0f;
	sample = mantissa << (exponent + 3);
	sample += etab[exponent];
	if (ulawbyte & 0x80)
		sample = -sample;
	return sample;
}
#endif

/*!
 * \brief  Set up mu-law conversion table
 */
void ast_ulaw_init(void)
{
	int i;

	/*
	 *  Set up mu-law conversion table
	 */
#ifndef G711_NEW_ALGORITHM
	for (i = 0;i < 256;i++) {
		short mu,e,f,y;
		static const short etab[]={0,132,396,924,1980,4092,8316,16764};

		mu = 255-i;
		e = (mu & 0x70)/16;
		f = mu & 0x0f;
		y = f * (1 << (e + 3));
		y += etab[e];
		if (mu & 0x80) y = -y;
		__ast_mulaw[i] = y;
	}
	/* set up the reverse (mu-law) conversion table */
	for (i = -32768; i < 32768; i++) {
		__ast_lin2mu[((unsigned short)i) >> 2] = linear2ulaw(i);
	}
#else

	for (i = 0; i < 256; i++) {
		__ast_mulaw[i] = ulaw2linear(i);
	}
	/* set up the reverse (mu-law) conversion table */
	for (i = 0; i <= 32768; i += AST_ULAW_STEP) {
		AST_LIN2MU_LOOKUP(i) = linear2ulaw(i, 0 /* half-cooked */);
	}
#endif

#ifdef TEST_CODING_TABLES
	for (i = -32768; i < 32768; ++i) {
#ifndef G711_NEW_ALGORITHM
		unsigned char e1 = linear2ulaw(i);
#else
		unsigned char e1 = linear2ulaw(i, 1);
#endif
		short d1 = ulaw2linear(e1);
		unsigned char e2 = AST_LIN2MU(i);
		short d2 = ulaw2linear(e2);
		short d3 = AST_MULAW(e1);

		if (e1 != e2 || d1 != d3 || d2 != d3) {
			ast_log(LOG_WARNING, "u-Law coding tables test failed on %d: e1=%u, e2=%u, d1=%d, d2=%d\n",
					i, (unsigned)e1, (unsigned)e2, (int)d1, (int)d2);
		}
	}
	ast_log(LOG_NOTICE, "u-Law coding table test complete.\n");
#endif /* TEST_CODING_TABLES */

#ifdef TEST_TANDEM_TRANSCODING
	/* tandem transcoding test */
	for (i = -32768; i < 32768; ++i) {
		unsigned char e1 = AST_LIN2MU(i);
		short d1 = AST_MULAW(e1);
		unsigned char e2 = AST_LIN2MU(d1);
		short d2 = AST_MULAW(e2);
		unsigned char e3 = AST_LIN2MU(d2);
		short d3 = AST_MULAW(e3);

		if (i < 0 && e1 == 0x7f && e2 == 0xff && e3 == 0xff)
			continue; /* known and normal negative 0 case */

		if (e1 != e2 || e2 != e3 || d1 != d2 || d2 != d3) {
			ast_log(LOG_WARNING, "u-Law tandem transcoding test failed on %d: e1=%u, e2=%u, d1=%d, d2=%d, d3=%d\n",
					i, (unsigned)e1, (unsigned)e2, (int)d1, (int)d2, (int)d3);
		}
	}
	ast_log(LOG_NOTICE, "u-Law tandem transcoding test complete.\n");
#endif /* TEST_TANDEM_TRANSCODING */
}
