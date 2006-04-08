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

	/* convert and copy in outbuf */
	for (i=0;  i<f->samples; i++)
		dst[pvt->samples + i] = AST_MULAW(src[i]);

	pvt->samples += f->samples;
	pvt->datalen += 2 * f->samples;
	return 0;
}

/*! \brief convert and store samples in outbuf */
static int lintoulaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int i;
	int16_t *src = f->data;

	for (i=0 ; i < f->samples; i++) 
		pvt->outbuf[pvt->samples + i] = AST_LIN2MU(src[i]);
	pvt->samples += f->samples;
	pvt->datalen += f->samples;	/* 1 byte/sample */
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

static struct ast_module_lock me = { .usecnt = -1 };

static struct ast_translator ulawtolin = {
	.name = "ulawtolin",
	.srcfmt = AST_FORMAT_ULAW,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = ulawtolin_framein,
	.sample = ulawtolin_sample,
	.lockp = &me,
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
	.lockp = &me,
	.buf_size = BUFFER_SAMPLES,
	.buffer_samples = BUFFER_SAMPLES,
};

static void parse_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg = ast_config_load("codecs.conf");
	if (cfg == NULL)
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

int reload(void)
{
	parse_config();
	return 0;
}

int unload_module(void)
{
	int res;
	ast_mutex_lock(&me.lock);
	res = ast_unregister_translator(&lintoulaw);
	if (!res)
		res = ast_unregister_translator(&ulawtolin);
	if (me.usecnt)
		res = -1;
	ast_mutex_unlock(&me.lock);
	return res;
}

int load_module(void)
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

/*
 * Return a description of this module.
 */

const char *description(void)
{
	return "Mu-law Coder/Decoder";
}

int usecount(void)
{
	return me.usecnt;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
