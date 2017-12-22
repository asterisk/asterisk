/*
 * Asterisk -- An open source telephony toolkit.
 *
 * The GSM code is from TOAST.  Copyright information for that package is available
 * in the GSM directory.
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
 * \brief Translate between signed linear and Global System for Mobile Communications (GSM)
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<depend>gsm</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/translate.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"

#ifdef HAVE_GSM_HEADER
#include "gsm.h"
#elif defined(HAVE_GSM_GSM_HEADER)
#include <gsm/gsm.h>
#endif

#include "../formats/msgsm.h"

#define BUFFER_SAMPLES	8000
#define GSM_SAMPLES	160
#define	GSM_FRAME_LEN	33
#define	MSGSM_FRAME_LEN	65

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_gsm.h"

struct gsm_translator_pvt {	/* both gsm2lin and lin2gsm */
	gsm gsm;
	int16_t buf[BUFFER_SAMPLES];	/* lin2gsm, temporary storage */
};

static int gsm_new(struct ast_trans_pvt *pvt)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;

	return (tmp->gsm = gsm_create()) ? 0 : -1;
}

/*! \brief decode and store in outbuf. */
static int gsmtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	int x;
	int16_t *dst = pvt->outbuf.i16;
	/* guess format from frame len. 65 for MSGSM, 33 for regular GSM */
	int flen = (f->datalen % MSGSM_FRAME_LEN == 0) ?
		MSGSM_FRAME_LEN : GSM_FRAME_LEN;

	for (x=0; x < f->datalen; x += flen) {
		unsigned char data[2 * GSM_FRAME_LEN];
		unsigned char *src;
		int len;
		if (flen == MSGSM_FRAME_LEN) {
			len = 2*GSM_SAMPLES;
			src = data;
			/* Translate MSGSM format to Real GSM format before feeding in */
			/* XXX what's the point here! we should just work
			 * on the full format.
			 */
			conv65(f->data.ptr + x, data);
		} else {
			len = GSM_SAMPLES;
			src = f->data.ptr + x;
		}
		/* XXX maybe we don't need to check */
		if (pvt->samples + len > BUFFER_SAMPLES) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		if (gsm_decode(tmp->gsm, src, dst + pvt->samples)) {
			ast_log(LOG_WARNING, "Invalid GSM data (1)\n");
			return -1;
		}
		pvt->samples += GSM_SAMPLES;
		pvt->datalen += 2 * GSM_SAMPLES;
		if (flen == MSGSM_FRAME_LEN) {
			if (gsm_decode(tmp->gsm, data + GSM_FRAME_LEN, dst + pvt->samples)) {
				ast_log(LOG_WARNING, "Invalid GSM data (2)\n");
				return -1;
			}
			pvt->samples += GSM_SAMPLES;
			pvt->datalen += 2 * GSM_SAMPLES;
		}
	}
	return 0;
}

/*! \brief store samples into working buffer for later decode */
static int lintogsm_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;

	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	if (pvt->samples + f->samples > BUFFER_SAMPLES) {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	memcpy(tmp->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief encode and produce a frame */
static struct ast_frame *lintogsm_frameout(struct ast_trans_pvt *pvt)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int samples = 0; /* output samples */

	while (pvt->samples >= GSM_SAMPLES) {
		struct ast_frame *current;

		/* Encode a frame of data */
		gsm_encode(tmp->gsm, tmp->buf + samples, (gsm_byte *) pvt->outbuf.c);
		samples += GSM_SAMPLES;
		pvt->samples -= GSM_SAMPLES;

		current = ast_trans_frameout(pvt, GSM_FRAME_LEN, GSM_SAMPLES);
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

static void gsm_destroy_stuff(struct ast_trans_pvt *pvt)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	if (tmp->gsm)
		gsm_destroy(tmp->gsm);
}

static struct ast_translator gsmtolin = {
	.name = "gsmtolin",
	.src_codec = {
		.name = "gsm",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = gsm_new,
	.framein = gsmtolin_framein,
	.destroy = gsm_destroy_stuff,
	.sample = gsm_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
	.desc_size = sizeof (struct gsm_translator_pvt ),
};

static struct ast_translator lintogsm = {
	.name = "lintogsm",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "gsm",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "gsm",
	.newpvt = gsm_new,
	.framein = lintogsm_framein,
	.frameout = lintogsm_frameout,
	.destroy = gsm_destroy_stuff,
	.sample = slin8_sample,
	.desc_size = sizeof (struct gsm_translator_pvt ),
	.buf_size = (BUFFER_SAMPLES * GSM_FRAME_LEN + GSM_SAMPLES - 1)/GSM_SAMPLES,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintogsm);
	res |= ast_unregister_translator(&gsmtolin);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&gsmtolin);
	res |= ast_register_translator(&lintogsm);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "GSM Coder/Decoder",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
