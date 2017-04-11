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
 * \brief codec_a_mu.c - translate between alaw and ulaw directly
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/alaw.h"
#include "asterisk/ulaw.h"
#include "asterisk/utils.h"

#define BUFFER_SAMPLES   8000	/* size for the translation buffers */

static unsigned char mu2a[256];
static unsigned char a2mu[256];

/* Sample frame data */
#include "ex_ulaw.h"
#include "ex_alaw.h"

/*! \brief convert frame data and store into the buffer */
static int alawtoulaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int x = f->samples;
	unsigned char *src = f->data.ptr;
	unsigned char *dst = pvt->outbuf.uc + pvt->samples;

	pvt->samples += x;
	pvt->datalen += x;

	while (x--)
		*dst++ = a2mu[*src++];

	return 0;
}

/*! \brief convert frame data and store into the buffer */
static int ulawtoalaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int x = f->samples;
	unsigned char *src = f->data.ptr;
	unsigned char *dst = pvt->outbuf.uc + pvt->samples;

	pvt->samples += x;
	pvt->datalen += x;

	while (x--)
		*dst++ = mu2a[*src++];

	return 0;
}

static struct ast_translator alawtoulaw = {
	.name = "alawtoulaw",
	.src_codec = {
		.name = "alaw",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "ulaw",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "ulaw",
	.framein = alawtoulaw_framein,
	.sample = alaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES,
};

static struct ast_translator ulawtoalaw = {
	.name = "ulawtoalaw",
	.src_codec = {
		.name = "ulaw",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "alaw",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "alaw",
	.framein = ulawtoalaw_framein,
	.sample = ulaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES,
};

/*! \brief standard module glue */

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&ulawtoalaw);
	res |= ast_unregister_translator(&alawtoulaw);

	return res;
}

static int load_module(void)
{
	int res;
	int x;

	for (x=0;x<256;x++) {
		mu2a[x] = AST_LIN2A(AST_MULAW(x));
		a2mu[x] = AST_LIN2MU(AST_ALAW(x));
	}

	res = ast_register_translator(&alawtoulaw);
	res |= ast_register_translator(&ulawtoalaw);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "A-law and Mulaw direct Coder/Decoder");
