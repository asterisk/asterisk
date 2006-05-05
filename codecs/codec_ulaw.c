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

#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/translate.h"
#include "asterisk/channel.h"
#include "asterisk/ulaw.h"
#include "asterisk/utils.h"

#define BUFFER_SAMPLES   8096	/* size for the translation buffers */

/* Sample frame data */

#include "slin_ulaw_ex.h"
#include "ulaw_slin_ex.h"

/*! \brief convert and store samples in outbuf */
static int ulawtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i;
	unsigned char *src = f->data;
	int16_t *dst = (int16_t *)pvt->outbuf;
	int in_samples = f->samples;
	int out_samples = pvt->samples;

	/* convert and copy in outbuf */
	for (i = 0; i < in_samples; i++)
		dst[out_samples++] = AST_MULAW(src[i]);

	pvt->samples = out_samples;
	pvt->datalen += in_samples * 2;	/* 2 bytes/sample */
	return 0;
}

/*! \brief convert and store samples in outbuf */
static int lintoulaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i;
	char *dst = pvt->outbuf + pvt->samples;
	int16_t *src = f->data;
	int in_samples = f->samples;

	for (i = 0; i < in_samples; i++) 
		*dst++ = AST_LIN2MU(src[i]);
	pvt->samples += in_samples;
	pvt->datalen += in_samples;	/* 1 byte/sample */
	return 0;
}

/*!  * \brief ulawToLin_Sample */
static struct ast_frame *ulawtolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ULAW;
	f.datalen = sizeof(ulaw_slin_ex);
	f.samples = sizeof(ulaw_slin_ex);
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = ulaw_slin_ex;
	return &f;
}

/*!
 * \brief LinToulaw_Sample
 */

static struct ast_frame *lintoulaw_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_ulaw_ex);
	/* Assume 8000 Hz */
	f.samples = sizeof(slin_ulaw_ex) / 2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_ulaw_ex;
	return &f;
}

/*!
 * \brief The complete translator for ulawToLin.
 */

static struct ast_translator ulawtolin = {
	.name = "ulawtolin",
	.srcfmt = AST_FORMAT_ULAW,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = ulawtolin_framein,
	.sample = ulawtolin_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
	.plc_samples = 160,
};

/*!
 * \brief The complete translator for LinToulaw.
 */

static struct ast_translator lintoulaw = {
	.name = "lintoulaw",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ULAW,
	.framein = lintoulaw_framein,
	.sample = lintoulaw_sample,
	.buf_size = BUFFER_SAMPLES,
	.buffer_samples = BUFFER_SAMPLES,
};

static void parse_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg = ast_config_load("codecs.conf");
	if (!cfg)
		return;
	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
		if (!strcasecmp(var->name, "genericplc")) {
			ulawtolin.useplc = ast_true(var->value) ? 1 : 0;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "codec_ulaw: %susing generic PLC\n", ulawtolin.useplc ? "" : "not ");
		}
	}
	ast_config_destroy(cfg);
}

static int reload(void *mod)
{
	parse_config();
	return 0;
}

static int unload_module(void *mod)
{
	int res;
	res = ast_unregister_translator(&lintoulaw);
	res |= ast_unregister_translator(&ulawtolin);
	return res;
}

static int load_module(void *mod)
{
	int res;
	parse_config();
	res = ast_register_translator(&ulawtolin, mod);
	if (!res)
		res = ast_register_translator(&lintoulaw, mod);
	else
		ast_unregister_translator(&ulawtolin);
	return res;
}

/*
 * Return a description of this module.
 */

static const char *description(void)
{
	return "Mu-law Coder/Decoder";
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD(MOD_1, reload, NULL, NULL);
