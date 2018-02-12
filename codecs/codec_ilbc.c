/*
 * Asterisk -- An open source telephony toolkit.
 *
 * The iLBC code is from The IETF code base and is copyright The Internet Society (2004)
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
 * \brief Translate between signed linear and Internet Low Bitrate Codec
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<use>ilbc</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/codec.h"             /* for AST_MEDIA_TYPE_AUDIO */
#include "asterisk/format.h"            /* for ast_format_get_attribute_data */
#include "asterisk/frame.h"             /* for ast_frame, etc */
#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT, etc */
#include "asterisk/logger.h"            /* for ast_log, ast_debug, etc */
#include "asterisk/module.h"
#include "asterisk/translate.h"         /* for ast_trans_pvt, etc */

#ifdef ILBC_WEBRTC
#include <ilbc.h>
typedef uint16_t ilbc_bytes;
typedef int16_t  ilbc_block;
#define BUF_TYPE i16
#else
#include "ilbc/iLBC_encode.h"
#include "ilbc/iLBC_decode.h"
typedef unsigned char ilbc_bytes;
typedef float         ilbc_block;
#define BUF_TYPE uc
#endif

#include "asterisk/ilbc.h"

#define USE_ILBC_ENHANCER 0
#define BUFFER_SAMPLES    8000

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_ilbc.h"

struct ilbc_coder_pvt {
	iLBC_Enc_Inst_t enc;
	iLBC_Dec_Inst_t dec;
	/* Enough to store a full second */
	int16_t buf[BUFFER_SAMPLES];
	int16_t inited;
};

static int lintoilbc_new(struct ast_trans_pvt *pvt)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;
	struct ilbc_attr *attr = pvt->explicit_dst ? ast_format_get_attribute_data(pvt->explicit_dst) : NULL;
	const unsigned int mode = attr ? attr->mode : 30;

	initEncode(&tmp->enc, mode);

	return 0;
}

static int ilbctolin_new(struct ast_trans_pvt *pvt)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;

	tmp->inited = 0; /* we do not know the iLBC mode, yet */

	return 0;
}

/*! \brief decode a frame and store in outbuf */
static int ilbctolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;
	struct ilbc_attr *attr = ast_format_get_attribute_data(f->subclass.format);
	const unsigned int mode = attr ? attr->mode : 30;
	const unsigned int sample_rate = pvt->t->dst_codec.sample_rate;
	const unsigned int samples_per_frame = mode * sample_rate / 1000;
	const unsigned int octets_per_frame = (mode == 20) ? 38 : 50;

	int plc_mode = 1; /* 1 = normal data, 0 = plc */
	/* Assuming there's space left, decode into the current buffer at
	   the tail location.  Read in as many frames as there are */
	int x,i;
	int datalen = f->datalen;
	int16_t *dst = pvt->outbuf.i16;
	ilbc_block tmpf[samples_per_frame];

	if (!f->data.ptr && datalen) {
		ast_debug(1, "issue 16070, ILIB ERROR. data = NULL datalen = %d src = %s\n", datalen, f->src ? f->src : "no src set");
		f->datalen = 0;
		datalen = 0;
	}

	if (datalen == 0) { /* native PLC, set fake datalen and clear plc_mode */
		datalen = octets_per_frame;
		f->samples = samples_per_frame;
		plc_mode = 0;	/* do native plc */
		pvt->samples += samples_per_frame;
	}

	if (datalen % octets_per_frame) {
		ast_log(LOG_WARNING, "Huh?  An ilbc frame that isn't a multiple of %u bytes long from %s (%d)?\n", octets_per_frame, f->src, datalen);
		return -1;
	}

	if (!tmp->inited) {
		initDecode(&tmp->dec, mode, USE_ILBC_ENHANCER);
		tmp->inited = 1;
	}

	for (x = 0; x < datalen; x += octets_per_frame) {
		if (pvt->samples + samples_per_frame > BUFFER_SAMPLES) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		iLBC_decode(tmpf, plc_mode ? f->data.ptr + x : NULL, &tmp->dec, plc_mode);
		for (i = 0; i < samples_per_frame; i++)
			dst[pvt->samples + i] = tmpf[i];
		pvt->samples += samples_per_frame;
		pvt->datalen += samples_per_frame * 2;
	}
	return 0;
}

/*! \brief store a frame into a temporary buffer, for later decoding */
static int lintoilbc_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;

	/* Just add the frames to our stream */
	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	memcpy(tmp->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief encode the temporary buffer and generate a frame */
static struct ast_frame *lintoilbc_frameout(struct ast_trans_pvt *pvt)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int samples = 0; /* output samples */

	struct ilbc_attr *attr = ast_format_get_attribute_data(pvt->f.subclass.format);
	const unsigned int mode = attr ? attr->mode : 30;
	const unsigned int sample_rate = pvt->t->dst_codec.sample_rate;
	const unsigned int samples_per_frame = mode * sample_rate / 1000;
	const unsigned int octets_per_frame = (mode == 20) ? 38 : 50;

	while (pvt->samples >= samples_per_frame) {
		struct ast_frame *current;
		ilbc_block tmpf[samples_per_frame];
		int i;

		/* Encode a frame of data */
		for (i = 0; i < samples_per_frame; i++)
			tmpf[i] = tmp->buf[samples + i];
		iLBC_encode((ilbc_bytes *) pvt->outbuf.BUF_TYPE, tmpf, &tmp->enc);

		samples += samples_per_frame;
		pvt->samples -= samples_per_frame;

		current = ast_trans_frameout(pvt, octets_per_frame, samples_per_frame);
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

static struct ast_translator ilbctolin = {
	.name = "ilbctolin",
	.src_codec = {
		.name = "ilbc",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = ilbctolin_new,
	.framein = ilbctolin_framein,
	.sample = ilbc_sample,
	.desc_size = sizeof(struct ilbc_coder_pvt),
	.buf_size = BUFFER_SAMPLES * 2,
	.native_plc = 1,
};

static struct ast_translator lintoilbc = {
	.name = "lintoilbc",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "ilbc",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "ilbc",
	.newpvt = lintoilbc_new,
	.framein = lintoilbc_framein,
	.frameout = lintoilbc_frameout,
	.sample = slin8_sample,
	.desc_size = sizeof(struct ilbc_coder_pvt),
	/* frame len (38 bytes), frame size (160 samples), ceil (+ 160 - 1) */
	.buf_size = (BUFFER_SAMPLES * 38 + 160 - 1) / 160,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintoilbc);
	res |= ast_unregister_translator(&ilbctolin);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&ilbctolin);
	res |= ast_register_translator(&lintoilbc);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "iLBC Coder/Decoder");
