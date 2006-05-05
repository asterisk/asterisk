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
 * \brief codec_alaw.c - translate between signed linear and alaw
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
#include "asterisk/alaw.h"
#include "asterisk/utils.h"

#define BUFFER_SAMPLES   8096	/* size for the translation buffers */

/* Sample frame data (Mu data is okay) */

#include "slin_ulaw_ex.h"
#include "ulaw_slin_ex.h"

/*! \brief decode frame into lin and fill output buffer. */
static int alawtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i;
	unsigned char *src = f->data;
	int16_t *dst = (int16_t *)pvt->outbuf;
	int in_samples = f->samples;
	int out_samples = pvt->samples;
	
	for (i = 0; i < in_samples; i++)
		dst[out_samples++] = AST_ALAW(src[i]);

	pvt->samples = out_samples;
	pvt->datalen += in_samples * 2;	/* 2 bytes/sample */
	return 0;
}

/*! \brief convert and store input samples in output buffer */
static int lintoalaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i;
	char *dst = pvt->outbuf + pvt->samples;
	int16_t *src = f->data;
	int in_samples = f->samples;

	for (i = 0; i < in_samples; i++) 
		*dst++ = AST_LIN2A(src[i]);
	pvt->samples += in_samples;
	pvt->datalen += in_samples;	/* 1 byte/sample */
	return 0;
}

/*! \brief alawToLin_Sample */
static struct ast_frame *alawtolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.datalen = sizeof(ulaw_slin_ex);
	f.samples = sizeof(ulaw_slin_ex);
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = ulaw_slin_ex;
	return &f;
}

/*! \brief LinToalaw_Sample */
static struct ast_frame *lintoalaw_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_ulaw_ex);
	f.samples = sizeof(slin_ulaw_ex) / 2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_ulaw_ex;
	return &f;
}

static struct ast_translator alawtolin = {
	.name = "alawtolin",
	.srcfmt = AST_FORMAT_ALAW,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = alawtolin_framein,
	.sample = alawtolin_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
	.plc_samples = 160,
};

static struct ast_translator lintoalaw = {
	"lintoalaw",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ALAW,
	.framein = lintoalaw_framein,
	.sample = lintoalaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES,
};

static void parse_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg = ast_config_load("codecs.conf");
	if (!cfg)
		return;
	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
		if (!strcasecmp(var->name, "genericplc")) {
			alawtolin.useplc = ast_true(var->value) ? 1 : 0;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "codec_alaw: %susing generic PLC\n", alawtolin.useplc ? "" : "not ");
		}
	}
	ast_config_destroy(cfg);
}

/*! \brief standard module stuff */

static int reload(void *mod)
{
	parse_config();
	return 0;
}

static int unload_module(void *mod)
{
	int res;
	res = ast_unregister_translator(&lintoalaw);
	res |= ast_unregister_translator(&alawtolin);
	return res;
}

static int load_module(void *mod)
{
	int res;
	parse_config();
	res = ast_register_translator(&alawtolin, mod);
	if (!res)
		res = ast_register_translator(&lintoalaw, mod);
	else
		ast_unregister_translator(&alawtolin);
	return res;
}

static const char *description(void)
{
	return "A-law Coder/Decoder";
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD(MOD_1, reload, NULL, NULL);
