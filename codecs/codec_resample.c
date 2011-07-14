/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \note To install libresample, check it out of the following repository:
 * <code>$ svn co http://svn.digium.com/svn/thirdparty/libresample/trunk</code>
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<depend>resample</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

/* These are for SHRT_MAX and FLT_MAX -- { */
#if defined(__Darwin__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__CYGWIN__)
#include <float.h>
#else
#include <values.h>
#endif
#include <limits.h>
/* } */

#include <libresample.h>

#include "asterisk/module.h"
#include "asterisk/translate.h"

#include "asterisk/slin.h"

#define RESAMPLER_QUALITY 1

#define OUTBUF_SIZE   8096

struct slin16_to_slin8_pvt {
	void *resampler;
	float resample_factor;
};

struct slin8_to_slin16_pvt {
	void *resampler;
	float resample_factor;
};

static int slin16_to_slin8_new(struct ast_trans_pvt *pvt)
{
	struct slin16_to_slin8_pvt *resamp_pvt = pvt->pvt;

	resamp_pvt->resample_factor = 8000.0 / 16000.0;

	if (!(resamp_pvt->resampler = resample_open(RESAMPLER_QUALITY, resamp_pvt->resample_factor, resamp_pvt->resample_factor)))
		return -1;

	return 0;
}

static int slin8_to_slin16_new(struct ast_trans_pvt *pvt)
{
	struct slin8_to_slin16_pvt *resamp_pvt = pvt->pvt;

	resamp_pvt->resample_factor = 16000.0 / 8000.0;

	if (!(resamp_pvt->resampler = resample_open(RESAMPLER_QUALITY, resamp_pvt->resample_factor, resamp_pvt->resample_factor)))
		return -1;

	return 0;
}

static void slin16_to_slin8_destroy(struct ast_trans_pvt *pvt)
{
	struct slin16_to_slin8_pvt *resamp_pvt = pvt->pvt;

	if (resamp_pvt->resampler)
		resample_close(resamp_pvt->resampler);
}

static void slin8_to_slin16_destroy(struct ast_trans_pvt *pvt)
{
	struct slin8_to_slin16_pvt *resamp_pvt = pvt->pvt;

	if (resamp_pvt->resampler)
		resample_close(resamp_pvt->resampler);
}

static int resample_frame(struct ast_trans_pvt *pvt,
	void *resampler, float resample_factor, struct ast_frame *f)
{
	int total_in_buf_used = 0;
	int total_out_buf_used = 0;
	int16_t *in_buf = (int16_t *) f->data.ptr;
	int16_t *out_buf = pvt->outbuf.i16 + pvt->samples;
	float in_buf_f[f->samples];
	float out_buf_f[2048];
	int res = 0;
	int i;

	for (i = 0; i < f->samples; i++)
		in_buf_f[i] = in_buf[i] * (FLT_MAX / SHRT_MAX);

	while (total_in_buf_used < f->samples) {
		int in_buf_used, out_buf_used;

		out_buf_used = resample_process(resampler, resample_factor,
			&in_buf_f[total_in_buf_used], f->samples - total_in_buf_used,
			0, &in_buf_used,
			&out_buf_f[total_out_buf_used], ARRAY_LEN(out_buf_f) - total_out_buf_used);

		if (out_buf_used < 0)
			break;

		total_out_buf_used += out_buf_used;
		total_in_buf_used += in_buf_used;

		if (total_out_buf_used == ARRAY_LEN(out_buf_f)) {
			ast_log(LOG_ERROR, "Output buffer filled ... need to increase its size\n");
			res = -1;
			break;
		}
	}

	for (i = 0; i < total_out_buf_used; i++)
		out_buf[i] = out_buf_f[i] * (SHRT_MAX / FLT_MAX);	

	pvt->samples += total_out_buf_used;
	pvt->datalen += (total_out_buf_used * sizeof(int16_t));

	return res;
}

static int slin16_to_slin8_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct slin16_to_slin8_pvt *resamp_pvt = pvt->pvt;
	void *resampler = resamp_pvt->resampler;
	float resample_factor = resamp_pvt->resample_factor;

	return resample_frame(pvt, resampler, resample_factor, f);
}

static int slin8_to_slin16_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct slin8_to_slin16_pvt *resamp_pvt = pvt->pvt;
	void *resampler = resamp_pvt->resampler;
	float resample_factor = resamp_pvt->resample_factor;

	return resample_frame(pvt, resampler, resample_factor, f);
}

static struct ast_translator slin16_to_slin8 = {
	.name = "slin16_to_slin8",
	.srcfmt = AST_FORMAT_SLINEAR16,
	.dstfmt = AST_FORMAT_SLINEAR,
	.newpvt = slin16_to_slin8_new,
	.destroy = slin16_to_slin8_destroy,
	.framein = slin16_to_slin8_framein,
	.sample = slin16_sample,
	.desc_size = sizeof(struct slin16_to_slin8_pvt),
	.buffer_samples = (OUTBUF_SIZE / sizeof(int16_t)),
	.buf_size = OUTBUF_SIZE,
};

static struct ast_translator slin8_to_slin16 = {
	.name = "slin8_to_slin16",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_SLINEAR16,
	.newpvt = slin8_to_slin16_new,
	.destroy = slin8_to_slin16_destroy,
	.framein = slin8_to_slin16_framein,
	.sample = slin8_sample,
	.desc_size = sizeof(struct slin8_to_slin16_pvt),
	.buffer_samples = (OUTBUF_SIZE / sizeof(int16_t)),
	.buf_size = OUTBUF_SIZE,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_unregister_translator(&slin16_to_slin8);
	res |= ast_unregister_translator(&slin8_to_slin16);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_translator(&slin16_to_slin8);
	res |= ast_register_translator(&slin8_to_slin16);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SLIN Resampling Codec");
