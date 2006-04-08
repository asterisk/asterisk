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

	for ( i = 0; i < f->samples; i++)
		dst[pvt->samples + i] = AST_ALAW(src[i]);
	pvt->samples += f->samples;
	pvt->datalen += 2*f->samples;	/* 2 bytes/sample */
	return 0;
}

/*! \brief convert and store input samples in output buffer */
static int lintoalaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int x;
	char *dst = pvt->outbuf + pvt->samples;
	int16_t *src = f->data;

	for ( x = 0; x < f->samples; x++) 
		*dst++ = AST_LIN2A(src[x]);
	pvt->samples += f->samples;
	pvt->datalen += f->samples;	/* 1 byte/sample */
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

static struct ast_module_lock me = { .usecnt = -1 };

static struct ast_translator alawtolin = {
	.name = "alawtolin",
	.srcfmt = AST_FORMAT_ALAW,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = alawtolin_framein,
	.sample = alawtolin_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
	.plc_samples = 160,
	.lockp = &me,
};

static struct ast_translator lintoalaw = {
	"lintoalaw",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ALAW,
	.framein = lintoalaw_framein,
	.sample = lintoalaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES,
	.lockp = &me,
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

int reload(void)
{
	parse_config();
	return 0;
}

int unload_module(void)
{
	int res;
	ast_mutex_lock(&me.lock);
	res = ast_unregister_translator(&lintoalaw);
	res |= ast_unregister_translator(&alawtolin);
	if (me.usecnt)
		res = -1;
	ast_mutex_unlock(&me.lock);
	return res;
}

int load_module(void)
{
	int res;
	parse_config();
	res = ast_register_translator(&alawtolin);
	if (!res)
		res = ast_register_translator(&lintoalaw);
	else
		ast_unregister_translator(&alawtolin);
	return res;
}

const char *description(void)
{
	return "A-law Coder/Decoder";
}

int usecount(void)
{
	return me.usecnt;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
