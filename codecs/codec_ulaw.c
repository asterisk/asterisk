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

#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	int i = f->samples;
	unsigned char *src = f->data;
	int16_t *dst = (int16_t *)pvt->outbuf + pvt->samples;

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
	char *dst = pvt->outbuf + pvt->samples;
	int16_t *src = f->data;

	pvt->samples += i;
	pvt->datalen += i;	/* 1 byte/sample */

	while (i--)
		*dst++ = AST_LIN2MU(*src++);

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

static int reload(void)
{
	parse_config();

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintoulaw);
	res |= ast_unregister_translator(&ulawtolin);

	return res;
}

static int load_module(void)
{
	int res;

	parse_config();
	res = ast_register_translator(&ulawtolin);
	if (!res)
		res = ast_register_translator(&lintoulaw);
	else
		ast_unregister_translator(&ulawtolin);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "mu-Law Coder/Decoder",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
