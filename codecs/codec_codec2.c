/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Alexander Traud
 *
 * Alexander Traud <pabstraud@compuserve.com>
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
 * \brief Translate between signed linear and Codec 2
 *
 * \author Alexander Traud <pabstraud@compuserve.com>
 *
 * \note http://www.rowetel.com/codec2.html
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<depend>codec2</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/codec.h"             /* for AST_MEDIA_TYPE_AUDIO       */
#include "asterisk/frame.h"             /* for ast_frame                  */
#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT, etc         */
#include "asterisk/logger.h"            /* for ast_log, etc               */
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"        /* ast_rtp_engine_(un)load_format */
#include "asterisk/translate.h"         /* for ast_trans_pvt, etc         */

#include <codec2/codec2.h>

#define BUFFER_SAMPLES    8000
#define CODEC2_SAMPLES    160  /* consider codec2_samples_per_frame(.) */
#define CODEC2_FRAME_LEN  6    /* consider codec2_bits_per_frame(.)    */

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_codec2.h"

struct codec2_translator_pvt {
	struct CODEC2 *state; /* May be encoder or decoder */
	int16_t buf[BUFFER_SAMPLES];
};

static int codec2_new(struct ast_trans_pvt *pvt)
{
	struct codec2_translator_pvt *tmp = pvt->pvt;

	tmp->state = codec2_create(CODEC2_MODE_2400);

	if (!tmp->state) {
		ast_log(LOG_ERROR, "Error creating Codec 2 conversion\n");
		return -1;
	}

	return 0;
}

/*! \brief decode and store in outbuf. */
static int codec2tolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec2_translator_pvt *tmp = pvt->pvt;
	int x;

	for (x = 0; x < f->datalen; x += CODEC2_FRAME_LEN) {
		unsigned char *src = f->data.ptr + x;
		int16_t *dst = pvt->outbuf.i16 + pvt->samples;

		codec2_decode(tmp->state, dst, src);

		pvt->samples += CODEC2_SAMPLES;
		pvt->datalen += CODEC2_SAMPLES * 2;
	}

	return 0;
}

/*! \brief store samples into working buffer for later decode */
static int lintocodec2_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec2_translator_pvt *tmp = pvt->pvt;

	memcpy(tmp->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;

	return 0;
}

/*! \brief encode and produce a frame */
static struct ast_frame *lintocodec2_frameout(struct ast_trans_pvt *pvt)
{
	struct codec2_translator_pvt *tmp = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int samples = 0; /* output samples */

	while (pvt->samples >= CODEC2_SAMPLES) {
		struct ast_frame *current;

		/* Encode a frame of data */
		codec2_encode(tmp->state, pvt->outbuf.uc, tmp->buf + samples);

		samples += CODEC2_SAMPLES;
		pvt->samples -= CODEC2_SAMPLES;

		current = ast_trans_frameout(pvt, CODEC2_FRAME_LEN, CODEC2_SAMPLES);

		if (!current) {
			continue;
		} else if (last) {
			AST_LIST_NEXT(last, frame_list) = current;
		} else {
			result = current;
		}
		last = current;
	}

	/* Move the data at the end of the buffer to the front */
	if (samples) {
		memmove(tmp->buf, tmp->buf + samples, pvt->samples * 2);
	}

	return result;
}

static void codec2_destroy_stuff(struct ast_trans_pvt *pvt)
{
	struct codec2_translator_pvt *tmp = pvt->pvt;

	if (tmp->state) {
		codec2_destroy(tmp->state);
	}
}

static struct ast_translator codec2tolin = {
	.name = "codec2tolin",
	.src_codec = {
		.name = "codec2",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = codec2_new,
	.framein = codec2tolin_framein,
	.destroy = codec2_destroy_stuff,
	.sample = codec2_sample,
	.desc_size = sizeof(struct codec2_translator_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintocodec2 = {
	.name = "lintocodec2",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "codec2",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "codec2",
	.newpvt = codec2_new,
	.framein = lintocodec2_framein,
	.frameout = lintocodec2_frameout,
	.destroy = codec2_destroy_stuff,
	.sample = slin8_sample,
	.desc_size = sizeof(struct codec2_translator_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = (BUFFER_SAMPLES * CODEC2_FRAME_LEN + CODEC2_SAMPLES - 1) / CODEC2_SAMPLES,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_rtp_engine_unload_format(ast_format_codec2);
	res |= ast_unregister_translator(&lintocodec2);
	res |= ast_unregister_translator(&codec2tolin);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_translator(&codec2tolin);
	res |= ast_register_translator(&lintocodec2);
	res |= ast_rtp_engine_load_format(ast_format_codec2);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Codec 2 Coder/Decoder");
