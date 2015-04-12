/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
 * David Vossel <dvossel@digium.com>
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

/*! 
 * \file
 *
 * \brief Resample slinear audio
 * 
 * \ingroup codecs
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "speex/speex_resampler.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/slin.h"

#define OUTBUF_SIZE   8096

static struct ast_translator *translators;
static int trans_size;
static struct ast_codec codec_list[] = {
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 32000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 44100,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 96000,
	},
	{
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 192000,
	},
};

static int resamp_new(struct ast_trans_pvt *pvt)
{
	int err;

	if (!(pvt->pvt = speex_resampler_init(1, pvt->t->src_codec.sample_rate, pvt->t->dst_codec.sample_rate, 5, &err))) {
		return -1;
	}

	ast_assert(pvt->f.subclass.format == NULL);
	pvt->f.subclass.format = ao2_bump(ast_format_cache_get_slin_by_rate(pvt->t->dst_codec.sample_rate));

	return 0;
}

static void resamp_destroy(struct ast_trans_pvt *pvt)
{
	SpeexResamplerState *resamp_pvt = pvt->pvt;

	speex_resampler_destroy(resamp_pvt);
}

static int resamp_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	SpeexResamplerState *resamp_pvt = pvt->pvt;
	unsigned int out_samples = (OUTBUF_SIZE / sizeof(int16_t)) - pvt->samples;
	unsigned int in_samples;

	if (!f->datalen) {
		return -1;
	}
	in_samples = f->datalen / 2;

	speex_resampler_process_int(resamp_pvt,
		0,
		f->data.ptr,
		&in_samples,
		pvt->outbuf.i16 + pvt->samples,
		&out_samples);

	pvt->samples += out_samples;
	pvt->datalen += out_samples * 2;

	return 0;
}

static int unload_module(void)
{
	int res = 0;
	int idx;

	for (idx = 0; idx < trans_size; idx++) {
		res |= ast_unregister_translator(&translators[idx]);
	}
	ast_free(translators);

	return res;
}

static int load_module(void)
{
	int res = 0;
	int x, y, idx = 0;

	trans_size = ARRAY_LEN(codec_list) * (ARRAY_LEN(codec_list) - 1);
	if (!(translators = ast_calloc(1, sizeof(struct ast_translator) * trans_size))) {
		return AST_MODULE_LOAD_FAILURE;
	}

	for (x = 0; x < ARRAY_LEN(codec_list); x++) {
		for (y = 0; y < ARRAY_LEN(codec_list); y++) {
			if (x == y) {
				continue;
			}
			translators[idx].newpvt = resamp_new;
			translators[idx].destroy = resamp_destroy;
			translators[idx].framein = resamp_framein;
			translators[idx].desc_size = 0;
			translators[idx].buffer_samples = (OUTBUF_SIZE / sizeof(int16_t));
			translators[idx].buf_size = OUTBUF_SIZE;
			memcpy(&translators[idx].src_codec, &codec_list[x], sizeof(struct ast_codec));
			memcpy(&translators[idx].dst_codec, &codec_list[y], sizeof(struct ast_codec));
			snprintf(translators[idx].name, sizeof(translators[idx].name), "slin %ukhz -> %ukhz",
				translators[idx].src_codec.sample_rate, translators[idx].dst_codec.sample_rate);
			res |= ast_register_translator(&translators[idx]);
			idx++;
		}

	}
	/* in case ast_register_translator() failed, we call unload_module() and
	ast_unregister_translator won't fail.*/
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SLIN Resampling Codec");
