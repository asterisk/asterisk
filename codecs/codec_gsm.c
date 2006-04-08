/*
 * Asterisk -- An open source telephony toolkit.
 *
 * The GSM code is from TOAST.  Copyright information for that package is available
 * in the GSM directory.
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
 * \brief Translate between signed linear and Global System for Mobile Communications (GSM)
 *
 * \ingroup codecs
 */

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
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"

#ifdef USE_EXTERNAL_GSM_LIB
#include <gsm/gsm.h>
#else
#include "gsm/inc/gsm.h"
#endif

#include "../formats/msgsm.h"

/* Sample frame data */
#include "slin_gsm_ex.h"
#include "gsm_slin_ex.h"

#define BUFFER_SAMPLES	8000
#define GSM_SAMPLES	160
#define	GSM_FRAME_LEN	33
#define	MSGSM_FRAME_LEN	65

struct gsm_translator_pvt {	/* both gsm2lin and lin2gsm */
	gsm gsm;
	int16_t buf[BUFFER_SAMPLES];	/* lin2gsm, temporary storage */
};

static void *gsm_new(struct ast_trans_pvt *pvt)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	
	if (!(tmp->gsm = gsm_create()))
		return NULL;
	return tmp;
}

static struct ast_frame *lintogsm_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_gsm_ex);
	/* Assume 8000 Hz */
	f.samples = sizeof(slin_gsm_ex)/2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_gsm_ex;
	return &f;
}

static struct ast_frame *gsmtolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_GSM;
	f.datalen = sizeof(gsm_slin_ex);
	/* All frames are 20 ms long */
	f.samples = GSM_SAMPLES;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = gsm_slin_ex;
	return &f;
}

/*! \brief decode and store in outbuf. */
static int gsmtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	int x;
	int16_t *dst = (int16_t *)pvt->outbuf;
	/* guess format from frame len. 65 for MSGSM, 33 for regular GSM */
	int flen = (f->datalen % MSGSM_FRAME_LEN == 0) ?
		MSGSM_FRAME_LEN : GSM_FRAME_LEN;

	for (x=0; x < f->datalen; x += flen) {
		unsigned char data[2 * GSM_FRAME_LEN];
		char *src;
		int len;
		if (flen == MSGSM_FRAME_LEN) {
			len = 2*GSM_SAMPLES;
			src = data;
			/* Translate MSGSM format to Real GSM format before feeding in */
			/* XXX what's the point here! we should just work
			 * on the full format.
			 */
			conv65(f->data + x, data);
		} else {
			len = GSM_SAMPLES;
			src = f->data + x;
		}
		/* XXX maybe we don't need to check */
		if (pvt->samples + len > BUFFER_SAMPLES) {	
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		if (gsm_decode(tmp->gsm, src, dst + pvt->samples)) {
			ast_log(LOG_WARNING, "Invalid GSM data (1)\n");
			return -1;
		}
		pvt->samples += GSM_SAMPLES;
		pvt->datalen += 2 * GSM_SAMPLES;
		if (flen == MSGSM_FRAME_LEN) {
			if (gsm_decode(tmp->gsm, data + GSM_FRAME_LEN, dst + pvt->samples)) {
				ast_log(LOG_WARNING, "Invalid GSM data (2)\n");
				return -1;
			}
			pvt->samples += GSM_SAMPLES;
			pvt->datalen += 2 * GSM_SAMPLES;
		}
	}
	return 0;
}

/*! \brief store samples into working buffer for later decode */
static int lintogsm_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;

	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	if (pvt->samples + f->samples > BUFFER_SAMPLES) {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	memcpy(tmp->buf + pvt->samples, f->data, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief encode and produce a frame */
static struct ast_frame *lintogsm_frameout(struct ast_trans_pvt *pvt)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	int datalen = 0;
	int samples = 0;

	/* We can't work on anything less than a frame in size */
	if (pvt->samples < GSM_SAMPLES)
		return NULL;
	while (pvt->samples >= GSM_SAMPLES) {
		/* Encode a frame of data */
		gsm_encode(tmp->gsm, tmp->buf, (gsm_byte *)pvt->outbuf + datalen);
		datalen += GSM_FRAME_LEN;
		samples += GSM_SAMPLES;
		pvt->samples -= GSM_SAMPLES;
		/* Move the data at the end of the buffer to the front */
		if (pvt->samples)
			memmove(tmp->buf, tmp->buf + GSM_SAMPLES, pvt->samples * 2);
	}
	return ast_trans_frameout(pvt, datalen, samples);
}

static void gsm_destroy_stuff(struct ast_trans_pvt *pvt)
{
	struct gsm_translator_pvt *tmp = pvt->pvt;
	if (tmp->gsm)
		gsm_destroy(tmp->gsm);
}

static struct ast_module_lock me = { .usecnt = -1 };

static struct ast_translator gsmtolin = {
	.name = "gsmtolin", 
	.srcfmt = AST_FORMAT_GSM,
	.dstfmt = AST_FORMAT_SLINEAR,
	.newpvt = gsm_new,
	.framein = gsmtolin_framein,
	.destroy = gsm_destroy_stuff,
	.sample = gsmtolin_sample,
	.lockp = &me,
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
	.desc_size = sizeof (struct gsm_translator_pvt ),
	.plc_samples = GSM_SAMPLES,
};

static struct ast_translator lintogsm = {
	.name = "lintogsm", 
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_GSM,
	.newpvt = gsm_new,
	.framein = lintogsm_framein,
	.frameout = lintogsm_frameout,
	.destroy = gsm_destroy_stuff,
	.sample = lintogsm_sample,
	.lockp = &me,
	.desc_size = sizeof (struct gsm_translator_pvt ),
	.buf_size = (BUFFER_SAMPLES * GSM_FRAME_LEN + GSM_SAMPLES - 1)/GSM_SAMPLES,
};


static void parse_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg = ast_config_load("codecs.conf");
	if (!cfg)
		return;
	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
	       if (!strcasecmp(var->name, "genericplc")) {
		       gsmtolin.useplc = ast_true(var->value) ? 1 : 0;
		       if (option_verbose > 2)
			       ast_verbose(VERBOSE_PREFIX_3 "codec_gsm: %susing generic PLC\n", gsmtolin.useplc ? "" : "not ");
	       }
	}
	ast_config_destroy(cfg);
}

/*! \brief standard module glue */
int reload(void)
{
	parse_config();
	return 0;
}

int unload_module(void)
{
	int res;
	ast_mutex_lock(&me.lock);
	res = ast_unregister_translator(&lintogsm);
	if (!res)
		res = ast_unregister_translator(&gsmtolin);
	if (me.usecnt)
		res = -1;
	ast_mutex_unlock(&me.lock);
	return res;
}

int load_module(void)
{
	int res;
	parse_config();
	res=ast_register_translator(&gsmtolin);
	if (!res) 
		res=ast_register_translator(&lintogsm);
	else
		ast_unregister_translator(&gsmtolin);
	return res;
}

const char *description(void)
{
	return "GSM/PCM16 (signed linear) Codec Translator";
}

int usecount(void)
{
	return me.usecnt;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
