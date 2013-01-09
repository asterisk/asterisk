/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Based on frompcm.c and topcm.c from the Emiliano MIPL browser/
 * interpreter.  See http://www.bsdtelephony.com.mx
 *
 * Copyright (c) 2001 - 2005 Digium, Inc.
 * All rights reserved.
 *
 * Karl Sackett <krs@linux-support.net>, 2001-03-21
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
 * \brief codec_adpcm.c - translate between signed linear and Dialogic ADPCM
 * 
 * \ingroup codecs
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"

/* define NOT_BLI to use a faster but not bit-level identical version */
/* #define NOT_BLI */

#define BUFFER_SAMPLES   8096	/* size for the translation buffers */

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_adpcm.h"

/*
 * Step size index shift table 
 */

static int indsft[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/*
 * Step size table, where stpsz[i]=floor[16*(11/10)^i]
 */

static int stpsz[49] = {
  16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
  80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
  307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552
};

/*
 * Decoder/Encoder state
 *   States for both encoder and decoder are synchronized
 */
struct adpcm_state {
	int ssindex;
	int signal;
	int zero_count;
	int next_flag;
};

/*
 * Decode(encoded)
 *  Decodes the encoded nibble from the adpcm file.
 *
 * Results:
 *  Returns the encoded difference.
 *
 * Side effects:
 *  Sets the index to the step size table for the next encode.
 */

static inline short decode(int encoded, struct adpcm_state *state)
{
	int diff;
	int step;
	int sign;

	step = stpsz[state->ssindex];

	sign = encoded & 0x08;
	encoded &= 0x07;
#ifdef NOT_BLI
	diff = (((encoded << 1) + 1) * step) >> 3;
#else /* BLI code */
	diff = step >> 3;
	if (encoded & 4)
		diff += step;
	if (encoded & 2)
		diff += step >> 1;
	if (encoded & 1)
		diff += step >> 2;
	if ((encoded >> 1) & step & 0x1)
		diff++;
#endif
	if (sign)
		diff = -diff;

	if (state->next_flag & 0x1)
		state->signal -= 8;
	else if (state->next_flag & 0x2)
		state->signal += 8;

	state->signal += diff;

	if (state->signal > 2047)
		state->signal = 2047;
	else if (state->signal < -2047)
		state->signal = -2047;

	state->next_flag = 0;

#ifdef AUTO_RETURN
	if (encoded)
		state->zero_count = 0;
	else if (++(state->zero_count) == 24) {
		state->zero_count = 0;
		if (state->signal > 0)
			state->next_flag = 0x1;
		else if (state->signal < 0)
			state->next_flag = 0x2;
	}
#endif

	state->ssindex += indsft[encoded];
	if (state->ssindex < 0)
		state->ssindex = 0;
	else if (state->ssindex > 48)
		state->ssindex = 48;

	return state->signal << 4;
}

/*
 * Adpcm
 *  Takes a signed linear signal and encodes it as ADPCM
 *  For more information see http://support.dialogic.com/appnotes/adpcm.pdf
 *
 * Results:
 *  Foo.
 *
 * Side effects:
 *  signal gets updated with each pass.
 */

static inline int adpcm(short csig, struct adpcm_state *state)
{
	int diff;
	int step;
	int encoded;

	/* 
	 * Clip csig if too large or too small
	 */
	csig >>= 4;

	step = stpsz[state->ssindex];
	diff = csig - state->signal;

#ifdef NOT_BLI
	if (diff < 0) {
		encoded = (-diff << 2) / step;
		if (encoded > 7)
			encoded = 7;
		encoded |= 0x08;
	} else {
		encoded = (diff << 2) / step;
		if (encoded > 7)
			encoded = 7;
	}
#else /* BLI code */
	if (diff < 0) {
		encoded = 8;
		diff = -diff;
	} else
		encoded = 0;
	if (diff >= step) {
		encoded |= 4;
		diff -= step;
	}
	step >>= 1;
	if (diff >= step) {
		encoded |= 2;
		diff -= step;
	}
	step >>= 1;
	if (diff >= step)
		encoded |= 1;
#endif /* NOT_BLI */

	/* feedback to state */
	decode(encoded, state);
	
	return encoded;
}

/*----------------- Asterisk-codec glue ------------*/

/*! \brief Workspace for translating signed linear signals to ADPCM. */
struct adpcm_encoder_pvt {
	struct adpcm_state state;
	int16_t inbuf[BUFFER_SAMPLES];	/* Unencoded signed linear values */
};

/*! \brief Workspace for translating ADPCM signals to signed linear. */
struct adpcm_decoder_pvt {
	struct adpcm_state state;
};

/*! \brief decode 4-bit adpcm frame data and store in output buffer */
static int adpcmtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_decoder_pvt *tmp = pvt->pvt;
	int x = f->datalen;
	unsigned char *src = f->data.ptr;
	int16_t *dst = pvt->outbuf.i16 + pvt->samples;

	while (x--) {
		*dst++ = decode((*src >> 4) & 0xf, &tmp->state);
		*dst++ = decode(*src++ & 0x0f, &tmp->state);
	}
	pvt->samples += f->samples;
	pvt->datalen += 2*f->samples;
	return 0;
}

/*! \brief fill input buffer with 16-bit signed linear PCM values. */
static int lintoadpcm_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;

	memcpy(&tmp->inbuf[pvt->samples], f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief convert inbuf and store into frame */
static struct ast_frame *lintoadpcm_frameout(struct ast_trans_pvt *pvt)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;
	struct ast_frame *f;
	int i;
	int samples = pvt->samples;	/* save original number */
  
	if (samples < 2)
		return NULL;

	pvt->samples &= ~1; /* atomic size is 2 samples */

	for (i = 0; i < pvt->samples; i += 2) {
		pvt->outbuf.c[i/2] =
			(adpcm(tmp->inbuf[i  ], &tmp->state) << 4) |
			(adpcm(tmp->inbuf[i+1], &tmp->state)     );
	};

	f = ast_trans_frameout(pvt, pvt->samples/2, 0);

	/*
	 * If there is a left over sample, move it to the beginning
	 * of the input buffer.
	 */

	if (samples & 1) {	/* move the leftover sample at beginning */
		tmp->inbuf[0] = tmp->inbuf[samples - 1];
		pvt->samples = 1;
	}
	return f;
}


static struct ast_translator adpcmtolin = {
	.name = "adpcmtolin",
	.srcfmt = AST_FORMAT_ADPCM,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = adpcmtolin_framein,
	.sample = adpcm_sample,
	.desc_size = sizeof(struct adpcm_decoder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintoadpcm = {
	.name = "lintoadpcm",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ADPCM,
	.framein = lintoadpcm_framein,
	.frameout = lintoadpcm_frameout,
	.sample = slin8_sample,
	.desc_size = sizeof (struct adpcm_encoder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES/ 2,	/* 2 samples per byte */
};

/*! \brief standard module glue */
static int reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintoadpcm);
	res |= ast_unregister_translator(&adpcmtolin);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&adpcmtolin);
	if (!res)
		res = ast_register_translator(&lintoadpcm);
	else
		ast_unregister_translator(&adpcmtolin);
	if (res)
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Adaptive Differential PCM Coder/Decoder",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
