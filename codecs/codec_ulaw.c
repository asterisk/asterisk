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
 * \brief codec_ulaw.c - translate between signed linear and ulaw
 * 
 * \ingroup codecs
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/translate.h"
#include "asterisk/ulaw.h"
#include "asterisk/utils.h"

#define BUFFER_SAMPLES   8096	/* size for the translation buffers */

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_ulaw.h"

/*! \brief convert and store samples in outbuf */
static int ulawtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i = f->samples;
	unsigned char *src = f->data.ptr;
	int16_t *dst = pvt->outbuf.i16 + pvt->samples;

	pvt->samples += i;
	pvt->datalen += i * 2;	/* 2 bytes/sample */

	/* convert and copy in outbuf */
	while (i--)
		*dst++ = AST_MULAW(*src++);

	return 0;
}

/*! \brief convert and store samples in outbuf */
static int lintoulaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i = f->samples;
	char *dst = pvt->outbuf.c + pvt->samples;
	int16_t *src = f->data.ptr;

	pvt->samples += i;
	pvt->datalen += i;	/* 1 byte/sample */

	while (i--)
		*dst++ = AST_LIN2MU(*src++);

	return 0;
}

/*!
 * \brief The complete translator for ulawToLin.
 */

static struct ast_translator ulawtolin = {
	.name = "ulawtolin",
	.framein = ulawtolin_framein,
	.sample = ulaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator testlawtolin = {
	.name = "testlawtolin",
	.framein = ulawtolin_framein,
	.sample = ulaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

/*!
 * \brief The complete translator for LinToulaw.
 */

static struct ast_translator lintoulaw = {
	.name = "lintoulaw",
	.framein = lintoulaw_framein,
	.sample = slin8_sample,
	.buf_size = BUFFER_SAMPLES,
	.buffer_samples = BUFFER_SAMPLES,
};

static struct ast_translator lintotestlaw = {
	.name = "lintotestlaw",
	.framein = lintoulaw_framein,
	.sample = slin8_sample,
	.buf_size = BUFFER_SAMPLES,
	.buffer_samples = BUFFER_SAMPLES,
};

static int reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintoulaw);
	res |= ast_unregister_translator(&ulawtolin);
	res |= ast_unregister_translator(&testlawtolin);
	res |= ast_unregister_translator(&lintotestlaw);

	return res;
}

static int load_module(void)
{
	int res;

	ast_format_set(&lintoulaw.src_format, AST_FORMAT_SLINEAR, 0);
	ast_format_set(&lintoulaw.dst_format, AST_FORMAT_ULAW, 0);

	ast_format_set(&lintotestlaw.src_format, AST_FORMAT_SLINEAR, 0);
	ast_format_set(&lintotestlaw.dst_format, AST_FORMAT_TESTLAW, 0);

	ast_format_set(&ulawtolin.src_format, AST_FORMAT_ULAW, 0);
	ast_format_set(&ulawtolin.dst_format, AST_FORMAT_SLINEAR, 0);

	ast_format_set(&testlawtolin.src_format, AST_FORMAT_TESTLAW, 0);
	ast_format_set(&testlawtolin.dst_format, AST_FORMAT_SLINEAR, 0);

	res = ast_register_translator(&ulawtolin);
	if (!res) {
		res = ast_register_translator(&lintoulaw);
		res |= ast_register_translator(&lintotestlaw);
		res |= ast_register_translator(&testlawtolin);
	} else
		ast_unregister_translator(&ulawtolin);
	if (res)
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "mu-Law Coder/Decoder",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
