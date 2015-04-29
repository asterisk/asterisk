/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Matthew Fredrickson <creslin@digium.com>
 * Russell Bryant <russell@digium.com>
 *
 * Special thanks to Steve Underwood for the implementation
 * and for doing the 8khz<->g.722 direct translation code.
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
 * \brief codec_g722.c - translate between signed linear and ITU G.722-64kbps
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 * \author Russell Bryant <russell@digium.com>
 *
 * \arg http://soft-switch.org/downloads/non-gpl-bits.tgz
 * \arg http://lists.digium.com/pipermail/asterisk-dev/2006-September/022866.html
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"

#define BUFFER_SAMPLES   8096	/* size for the translation buffers */
#define BUF_SHIFT	5

#include "g722/g722.h"

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_g722.h"

struct g722_encoder_pvt {
	g722_encode_state_t g722;
};

struct g722_decoder_pvt {
	g722_decode_state_t g722;
};

/*! \brief init a new instance of g722_encoder_pvt. */
static int lintog722_new(struct ast_trans_pvt *pvt)
{
	struct g722_encoder_pvt *tmp = pvt->pvt;

	g722_encode_init(&tmp->g722, 64000, G722_SAMPLE_RATE_8000);

	return 0;
}

static int lin16tog722_new(struct ast_trans_pvt *pvt)
{
	struct g722_encoder_pvt *tmp = pvt->pvt;

	g722_encode_init(&tmp->g722, 64000, 0);

	return 0;
}

/*! \brief init a new instance of g722_encoder_pvt. */
static int g722tolin_new(struct ast_trans_pvt *pvt)
{
	struct g722_decoder_pvt *tmp = pvt->pvt;

	g722_decode_init(&tmp->g722, 64000, G722_SAMPLE_RATE_8000);

	return 0;
}

static int g722tolin16_new(struct ast_trans_pvt *pvt)
{
	struct g722_decoder_pvt *tmp = pvt->pvt;

	g722_decode_init(&tmp->g722, 64000, 0);

	return 0;
}

static int g722tolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct g722_decoder_pvt *tmp = pvt->pvt;
	int out_samples;
	int in_samples;

	/* g722_decode expects the samples to be in the invalid samples / 2 format */
	in_samples = f->samples / 2;

	out_samples = g722_decode(&tmp->g722, &pvt->outbuf.i16[pvt->samples * sizeof(int16_t)], 
		(uint8_t *) f->data.ptr, in_samples);

	pvt->samples += out_samples;

	pvt->datalen += (out_samples * sizeof(int16_t));

	return 0;
}

static int lintog722_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct g722_encoder_pvt *tmp = pvt->pvt;
	int outlen;

	outlen = g722_encode(&tmp->g722, (&pvt->outbuf.ui8[pvt->datalen]), 
		(int16_t *) f->data.ptr, f->samples);

	pvt->samples += outlen * 2;

	pvt->datalen += outlen;

	return 0;
}

static struct ast_translator g722tolin = {
	.name = "g722tolin",
	.src_codec = {
		.name = "g722",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = g722tolin_new,	/* same for both directions */
	.framein = g722tolin_framein,
	.sample = g722_sample,
	.desc_size = sizeof(struct g722_decoder_pvt),
	.buffer_samples = BUFFER_SAMPLES / sizeof(int16_t),
	.buf_size = BUFFER_SAMPLES,
};

static struct ast_translator lintog722 = {
	.name = "lintog722",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "g722",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "g722",
	.newpvt = lintog722_new,	/* same for both directions */
	.framein = lintog722_framein,
	.sample = slin8_sample,
	.desc_size = sizeof(struct g722_encoder_pvt),
	.buffer_samples = BUFFER_SAMPLES * 2,
	.buf_size = BUFFER_SAMPLES,
};

static struct ast_translator g722tolin16 = {
	.name = "g722tolin16",
	.src_codec = {
		.name = "g722",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "slin16",
	.newpvt = g722tolin16_new,	/* same for both directions */
	.framein = g722tolin_framein,
	.sample = g722_sample,
	.desc_size = sizeof(struct g722_decoder_pvt),
	.buffer_samples = BUFFER_SAMPLES / sizeof(int16_t),
	.buf_size = BUFFER_SAMPLES,
};

static struct ast_translator lin16tog722 = {
	.name = "lin16tog722",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "g722",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "g722",
	.newpvt = lin16tog722_new,	/* same for both directions */
	.framein = lintog722_framein,
	.sample = slin16_sample,
	.desc_size = sizeof(struct g722_encoder_pvt),
	.buffer_samples = BUFFER_SAMPLES * 2,
	.buf_size = BUFFER_SAMPLES,
};

static int load_module(void)
{
	int res = 0;

	res |= ast_register_translator(&g722tolin);
	res |= ast_register_translator(&lintog722);
	res |= ast_register_translator(&g722tolin16);
	res |= ast_register_translator(&lin16tog722);

	if (res) {
		return AST_MODULE_LOAD_FAILURE;
	}	

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "ITU G.722-64kbps G722 Transcoder");
