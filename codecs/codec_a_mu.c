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
#include "asterisk/translate.h"
#include "asterisk/channel.h"
#include "asterisk/alaw.h"
#include "asterisk/ulaw.h"
#include "asterisk/utils.h"

#define BUFFER_SAMPLES   8000	/* size for the translation buffers */

static unsigned char mu2a[256];
static unsigned char a2mu[256];

/* Sample frame data (Mu data is okay) */

#include "ulaw_slin_ex.h"

/*! \brief convert frame data and store into the buffer */
static int alawtoulaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int x;
	unsigned char *src = f->data;
	unsigned char *dst = (unsigned char *)pvt->outbuf + pvt->samples;

	for ( x = 0 ; x < f->samples; x++)
		dst[x] = a2mu[src[x]];
	pvt->samples += f->samples;
	pvt->datalen += f->datalen;
	return 0;
}

/*! \brief convert frame data and store into the buffer */
static int ulawtoalaw_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int x;
	unsigned char *src = f->data;
	unsigned char *dst = (unsigned char *)pvt->outbuf + pvt->samples;

	for ( x = 0 ; x < f->samples; x++)
		dst[x] = mu2a[src[x]];
	pvt->samples += f->samples;
	pvt->datalen += f->datalen;
	return 0;
}

/*
 * alawToLin_Sample. Just random data, somehow...
 */
static struct ast_frame *alawtoulaw_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.datalen = sizeof(ulaw_slin_ex);
	f.samples = sizeof(ulaw_slin_ex);
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = ulaw_slin_ex; /* XXX what ? */
	return &f;
}

static struct ast_frame *ulawtoalaw_sample(void)
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

static struct ast_module_lock me = { .usecnt = -1 };

static struct ast_translator alawtoulaw = {
	.name = "alawtoulaw",
	.srcfmt = AST_FORMAT_ALAW,
	.dstfmt = AST_FORMAT_ULAW,
	.framein = alawtoulaw_framein,
	.sample = alawtoulaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES,
	.lockp = &me,
};

static struct ast_translator ulawtoalaw = {
	.name = "ulawtoalaw",
	.srcfmt = AST_FORMAT_ULAW,
	.dstfmt = AST_FORMAT_ALAW,
	.framein = ulawtoalaw_framein,
	.sample = ulawtoalaw_sample,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES,
	.lockp = &me,
};

/*! \brief standard module glue */

int unload_module(void)
{
	int res;
	ast_mutex_lock(&me.lock);
	res = ast_unregister_translator(&ulawtoalaw);
	res |= ast_unregister_translator(&alawtoulaw);
	if (me.usecnt)
		res = -1;
	ast_mutex_unlock(&me.lock);
	return res;
}

int load_module(void)
{
	int res;
	int x;
	for (x=0;x<256;x++) {
		mu2a[x] = AST_LIN2A(AST_MULAW(x));
		a2mu[x] = AST_LIN2MU(AST_ALAW(x));
	}
	res = ast_register_translator(&alawtoulaw);
	if (!res)
		res = ast_register_translator(&ulawtoalaw);
	else
		ast_unregister_translator(&alawtoulaw);
	return res;
}

const char *description(void)
{
	return "A-law and Mulaw direct Coder/Decoder";
}

int usecount(void)
{
	return me.usecnt;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
