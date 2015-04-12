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
 * \brief a-Law to Signed linear conversion
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/alaw.h"
#include "asterisk/logger.h"

#ifndef G711_NEW_ALGORITHM
#define AMI_MASK 0x55

static inline unsigned char linear2alaw(short int linear)
{
	int mask;
	int seg;
	int pcm_val;
	static int seg_end[8] =
		{
			0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF
		};

	pcm_val = linear;
	if (pcm_val >= 0) {
		/* Sign (7th) bit = 1 */
		mask = AMI_MASK | 0x80;
	} else {
		/* Sign bit = 0 */
		mask = AMI_MASK;
		pcm_val = -pcm_val;
	}

	/* Convert the scaled magnitude to segment number. */
	for (seg = 0; seg < 8; seg++) {
		if (pcm_val <= seg_end[seg]) {
			break;
		}
	}
	/* Combine the sign, segment, and quantization bits. */
	return ((seg << 4) | ((pcm_val >> ((seg) ? (seg + 3) : 4)) & 0x0F)) ^ mask;
}
#else
static unsigned char linear2alaw(short sample, int full_coding)
{
	static const unsigned exp_lut[128] = {
		1,1,2,2,3,3,3,3,
		4,4,4,4,4,4,4,4,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,
		6,6,6,6,6,6,6,6,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7,
		7,7,7,7,7,7,7,7 };
	unsigned sign, exponent, mantissa, mag;
	unsigned char alawbyte;

	ast_alaw_get_sign_mag(sample, &sign, &mag);
	if (mag > 32767)
		mag = 32767;        /* clip the magnitude for -32768 */

	exponent = exp_lut[(mag >> 8) & 0x7f];
	mantissa = (mag >> (exponent + 3)) & 0x0f;
	if (mag < 0x100)
		exponent = 0;

	if (full_coding) {
		/* full encoding, with sign and xform */
		alawbyte = (unsigned char)(sign | (exponent << 4) | mantissa);
		alawbyte ^= AST_ALAW_AMI_MASK;
	} else {
		/* half-cooked coding -- mantissa+exponent only (for lookup tab) */
		alawbyte = (exponent << 4) | mantissa;
	}
	return alawbyte;
}
#endif

#ifndef G711_NEW_ALGORITHM
static inline short int alaw2linear (unsigned char alaw)
{
	int i;
	int seg;

	alaw ^= AMI_MASK;
	i = ((alaw & 0x0F) << 4) + 8 /* rounding error */;
	seg = (((int) alaw & 0x70) >> 4);
	if (seg) {
		i = (i + 0x100) << (seg - 1);
	}
	return (short int) ((alaw & 0x80) ? i : -i);
}
#else
static inline short alaw2linear(unsigned char alawbyte)
{
	unsigned exponent, mantissa;
	short sample;

	alawbyte ^= AST_ALAW_AMI_MASK;
	exponent = (alawbyte & 0x70) >> 4;
	mantissa = alawbyte & 0x0f;
	sample = (mantissa << 4) + 8 /* rounding error */;
	if (exponent)
		sample = (sample + 0x100) << (exponent - 1);
	if (!(alawbyte & 0x80))
		sample = -sample;
	return sample;
}
#endif



#ifndef G711_NEW_ALGORITHM
unsigned char __ast_lin2a[8192];
#else
unsigned char __ast_lin2a[AST_ALAW_TAB_SIZE];
#endif
short __ast_alaw[256];

void ast_alaw_init(void)
{
	int i;
	/*
	 *  Set up mu-law conversion table
	 */
#ifndef G711_NEW_ALGORITHM
	for (i = 0; i < 256; i++) {
		__ast_alaw[i] = alaw2linear(i);
	}
	/* set up the reverse (mu-law) conversion table */
	for (i = -32768; i < 32768; i++) {
		__ast_lin2a[((unsigned short)i) >> 3] = linear2alaw(i);
	}
#else
	for (i = 0; i < 256; i++) {
		__ast_alaw[i] = alaw2linear(i);
	}
	/* set up the reverse (a-law) conversion table */
	for (i = 0; i <= 32768; i += AST_ALAW_STEP) {
		AST_LIN2A_LOOKUP(i) = linear2alaw(i, 0 /* half-cooked */);
	}
#endif

#ifdef TEST_CODING_TABLES
	for (i = -32768; i < 32768; ++i) {
#ifndef G711_NEW_ALGORITHM
		unsigned char e1 = linear2alaw(i);
#else
		unsigned char e1 = linear2alaw(i, 1);
#endif
		short d1 = alaw2linear(e1);
		unsigned char e2 = AST_LIN2A(i);
		short d2 = alaw2linear(e2);
		short d3 = AST_ALAW(e1);

		if (e1 != e2 || d1 != d3 || d2 != d3) {
			ast_log(LOG_WARNING, "a-Law coding tables test failed on %d: e1=%u, e2=%u, d1=%d, d2=%d\n",
					i, (unsigned)e1, (unsigned)e2, (int)d1, (int)d2);
		}
	}
	ast_log(LOG_NOTICE, "a-Law coding tables test complete.\n");
#endif /* TEST_CODING_TABLES */

#ifdef TEST_TANDEM_TRANSCODING
	/* tandem transcoding test */
	for (i = -32768; i < 32768; ++i) {
		unsigned char e1 = AST_LIN2A(i);
		short d1 = AST_ALAW(e1);
		unsigned char e2 = AST_LIN2A(d1);
		short d2 = AST_ALAW(e2);
		unsigned char e3 = AST_LIN2A(d2);
		short d3 = AST_ALAW(e3);

		if (e1 != e2 || e2 != e3 || d1 != d2 || d2 != d3) {
			ast_log(LOG_WARNING, "a-Law tandem transcoding test failed on %d: e1=%u, e2=%u, d1=%d, d2=%d, d3=%d\n",
					i, (unsigned)e1, (unsigned)e2, (int)d1, (int)d2, (int)d3);
		}
	}
	ast_log(LOG_NOTICE, "a-Law tandem transcoding test complete.\n");
#endif /* TEST_TANDEM_TRANSCODING */

}

