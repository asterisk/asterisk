/*
 * Asterisk -- An open source telephony toolkit.
 *
 * The G.723.1 code is not included in the Asterisk distribution because
 * it is covered with patents, and in spite of statements to the contrary,
 * the "technology" is extremely expensive to license.
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
 * \brief Translate between signed linear and G.723.1
 *
 * \ingroup codecs
 */

#define TYPE_HIGH	 0x0
#define TYPE_LOW	 0x1
#define TYPE_SILENCE	 0x2
#define TYPE_DONTSEND	 0x3
#define TYPE_MASK	 0x3

#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"

#ifdef ANNEX_B
#include "g723.1b/typedef2.h"
#include "g723.1b/cst2.h"
#include "g723.1b/coder2.h"
#include "g723.1b/decod2.h"
#include "g723.1b/deccng2.h"
#include "g723.1b/codcng2.h"
#include "g723.1b/vad2.h"
#else
#include "g723.1/typedef.h"
#include "g723.1/cst_lbc.h"
#include "g723.1/coder.h"
#include "g723.1/decod.h"
#include "g723.1/dec_cng.h"
#include "g723.1/cod_cng.h"
#include "g723.1/vad.h"
#endif

/* Sample frame data */
#include "slin_g723_ex.h"
#include "g723_slin_ex.h"

/* g723_1 has 240 samples per buffer.
 * We want a buffer which is a multiple...
 */
#define	G723_SAMPLES	240
#define	BUFFER_SAMPLES	8160	/* 240 * 34 */

/* Globals */
Flag UsePf = True;
Flag UseHp = True;
Flag UseVx = True;

enum Crate WrkRate = Rate63;

struct g723_encoder_pvt {
	struct cod_state cod;
	int16_t buf[BUFFER_SAMPLES];	/* input buffer */
};

struct g723_decoder_pvt {
	struct dec_state dec;
};

static struct ast_trans_pvt *g723tolin_new(struct ast *pvt)
{
	struct g723_decoder_pvt *tmp = pvt;

	Init_Decod(&tmp->dec);
	Init_Dec_Cng(&tmp->dec);
	return tmp;
}

static struct ast_frame *lintog723_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_g723_ex);
	f.samples = sizeof(slin_g723_ex)/2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_g723_ex;
	return &f;
}

static struct ast_frame *g723tolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_G723_1;
	f.datalen = sizeof(g723_slin_ex);
	/* All frames are 30 ms long */
	f.samples = 240;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = g723_slin_ex;
	return &f;
}

static void *lintog723_new(void *pvt)
{
	struct g723_encoder_pvt *tmp = pvt;
	Init_Coder(&tmp->cod);
	/* Init Comfort Noise Functions */
	if( UseVx ) {
		Init_Vad(&tmp->cod);
		Init_Cod_Cng(&tmp->cod);
	}
	return tmp;
}

static int g723_len(unsigned char buf)
{
	switch(buf & TYPE_MASK) {
	case TYPE_DONTSEND:
		return 0;
		break;
	case TYPE_SILENCE:
		return 4;
		break;
	case TYPE_HIGH:
		return 24;
		break;
	case TYPE_LOW:
		return 20;
		break;
	default:
		ast_log(LOG_WARNING, "Badly encoded frame (%d)\n", buf & TYPE_MASK);
	}
	return -1;
}

static int g723tolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct g723_decoder_pvt *tmp = pvt->pvt;
	int len = 0;
	int res;
	int16_t *dst = pvt->outbuf;
#ifdef  ANNEX_B
	FLOAT tmpdata[Frame];
	int x;
#endif
	unsigned char *src = f->data;

	while(len < f->datalen) {
		/* Assuming there's space left, decode into the current buffer at
		   the tail location */
		res = g723_len(src[len]);
		if (res < 0) {
			ast_log(LOG_WARNING, "Invalid data\n");
			return -1;
		}
		if (res + len > f->datalen) {
			ast_log(LOG_WARNING, "Measured length exceeds frame length\n");
			return -1;
		}
		if (pvt->samples + Frame > BUFFER_SAMPLES) {	
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
#ifdef ANNEX_B
		Decod(&tmp->dec, tmpdata, f->data + len, 0);
		for (x=0;x<Frame;x++)
			dst[pvt->samples + x] = (int16_t)(tmpdata[x]); 
#else
		Decod(&tmp->dec, dst + pvt->samples, f->data + len, 0);
#endif
		pvt->samples += Frame;
		pvt->datalen += 2*Frame; /* 2 bytes/sample */
		len += res;
	}
	return 0;
}

static int lintog723_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	/* Just add the frames to our stream */
	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	struct g723_encoder_pvt *tmp = pvt->pvt;

	if (tmp->samples + f->samples > BUFFER_SAMPLES) {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	memcpy(&tmp->buf[pvt->samples], f->data, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

static struct ast_frame *lintog723_frameout(void *pvt)
{
	struct g723_encoder_pvt *tmp = (struct g723_encoder_pvt *)pvt;
	int samples = 0;	/* how many samples in buffer */
#ifdef ANNEX_B
	int x;
	FLOAT tmpdata[Frame];
#endif
	int cnt = 0;	/* how many bytes so far */

	/* We can't work on anything less than a frame in size */
	if (pvt->samples < Frame)
		return NULL;
	while (pvt->samples >= Frame) {
		/* Encode a frame of data */
		/* at most 24 bytes/frame... */
		if (cnt + 24 > pvt->buf_size) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return NULL;
		}
#ifdef ANNEX_B
		for ( x = 0; x < Frame ; x++)
			tmpdata[x] = tmp->buf[x];
		Coder(&tmp->cod, tmpdata, pvt->outbuf + cnt);
#else
		Coder(&tmp->cod, tmp->buf, pvt->outbuf + cnt);
#endif
		/* Assume 8000 Hz */
		samples += G723_SAMPLES;
		cnt += g723_len(tmp->outbuf[cnt]);
		pvt->samples -= Frame;
		/* Move the data at the end of the buffer to the front */
		/* XXX inefficient... */
		if (pvt->samples)
			memmove(tmp->buf, tmp->buf + Frame, pvt->samples * 2);
	}
	return ast_trans_frameout(pvt, cnt, samples);
}

static struct ast_translator g723tolin = {
	.name =
#ifdef ANNEX_B
	"g723btolin", 
#else
	"g723tolin", 
#endif
	.srcfmt = AST_FORMAT_G723_1,
	.dstfmt =  AST_FORMAT_SLINEAR,
	.newpvt = g723tolin_new,
	.framein = g723tolin_framein,
	.sample = g723tolin_sample,
	.desc_size = sizeof(struct ...),
};

static struct ast_translator lintog723 = {
	.name =
#ifdef ANNEX_B
	"lintog723b", 
#else
	"lintog723", 
#endif
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt =  AST_FORMAT_G723_1,
	.new = lintog723_new,
	.framein = lintog723_framein,
	.frameout = lintog723_frameout,
	.destroy = g723_destroy,
	.sample = lintog723_sample,
	.desc_size = sizeof(struct ...),
};

/*! \brief standard module glue */

static int unload_module(void *mod)
{
	int res;
	res = ast_unregister_translator(&lintog723);
	res |= ast_unregister_translator(&g723tolin);
	return res;
}

static int load_module(void *mod)
{
	int res;
	res=ast_register_translator(&g723tolin, mod);
	if (!res) 
		res=ast_register_translator(&lintog723, mod);
	else
		ast_unregister_translator(&g723tolin);
	return res;
}

static const char *description(void)
{
#ifdef ANNEX_B
	return "Annex B (floating point) G.723.1/PCM16 Codec Translator";
#else
	return "Annex A (fixed point) G.723.1/PCM16 Codec Translator";
#endif

}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD(MOD_1, reload, NULL, NULL);
