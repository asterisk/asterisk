/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
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
 * \brief Translate between signed linear and Speex (Open Codec)
 *
 * http://www.speex.org
 * \note This work was motivated by Jeremy McNamara 
 * hacked to be configurable by anthm and bkw 9/28/2004
 */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <speex.h>

/* We require a post 1.1.8 version of Speex to enable preprocessing
   and better type handling */   
#ifdef _SPEEX_TYPES_H
#include <speex/speex_preprocess.h>
#endif

static int quality = 3;
static int complexity = 2;
static int enhancement = 0;
static int vad = 0;
static int vbr = 0;
static float vbr_quality = 4;
static int abr = 0;
static int dtx = 0;

static int preproc = 0;
static int pp_vad = 0;
static int pp_agc = 0;
static float pp_agc_level = 8000;
static int pp_denoise = 0;
static int pp_dereverb = 0;
static float pp_dereverb_decay = 0.4;
static float pp_dereverb_level = 0.3;

#define TYPE_SILENCE	 0x2
#define TYPE_HIGH	 0x0
#define TYPE_LOW	 0x1
#define TYPE_MASK	 0x3

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

/* Sample frame data */
#include "slin_speex_ex.h"
#include "speex_slin_ex.h"

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt=0;

static char *tdesc = "Speex/PCM16 (signed linear) Codec Translator";

struct ast_translator_pvt {
	void *speex;
	struct ast_frame f;
	SpeexBits bits;
	int framesize;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
#ifdef _SPEEX_TYPES_H
	SpeexPreprocessState *pp;
	/* Buffer for our outgoing frame */
	spx_int16_t outbuf[8000];
	/* Enough to store a full second */
	spx_int16_t buf[8000];
#else
	short outbuf[8000];
	short buf[8000];
#endif

	int tail;
	int silent_state;
};

#define speex_coder_pvt ast_translator_pvt

static struct ast_translator_pvt *lintospeex_new(void)
{
	struct speex_coder_pvt *tmp;
	tmp = malloc(sizeof(struct speex_coder_pvt));
	if (tmp) {
		if (!(tmp->speex = speex_encoder_init(&speex_nb_mode))) {
			free(tmp);
			tmp = NULL;
		} else {
			speex_bits_init(&tmp->bits);
			speex_bits_reset(&tmp->bits);
			speex_encoder_ctl(tmp->speex, SPEEX_GET_FRAME_SIZE, &tmp->framesize);
			speex_encoder_ctl(tmp->speex, SPEEX_SET_COMPLEXITY, &complexity);
#ifdef _SPEEX_TYPES_H
			if (preproc) {
				tmp->pp = speex_preprocess_state_init(tmp->framesize, 8000);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_VAD, &pp_vad);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_AGC, &pp_agc);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_AGC_LEVEL, &pp_agc_level);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DENOISE, &pp_denoise);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DEREVERB, &pp_dereverb);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DEREVERB_DECAY, &pp_dereverb_decay);
				speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DEREVERB_LEVEL, &pp_dereverb_level);
			}
#endif
			if (!abr && !vbr) {
				speex_encoder_ctl(tmp->speex, SPEEX_SET_QUALITY, &quality);
				if (vad)
					speex_encoder_ctl(tmp->speex, SPEEX_SET_VAD, &vad);
			}
			if (vbr) {
				speex_encoder_ctl(tmp->speex, SPEEX_SET_VBR, &vbr);
				speex_encoder_ctl(tmp->speex, SPEEX_SET_VBR_QUALITY, &vbr_quality);
			}
			if (abr) {
				speex_encoder_ctl(tmp->speex, SPEEX_SET_ABR, &abr);
			}
			if (dtx)
				speex_encoder_ctl(tmp->speex, SPEEX_SET_DTX, &dtx); 
			tmp->tail = 0;
			tmp->silent_state = 0;
		}
		localusecnt++;
	}
	return tmp;
}

static struct ast_translator_pvt *speextolin_new(void)
{
	struct speex_coder_pvt *tmp;
	tmp = malloc(sizeof(struct speex_coder_pvt));
	if (tmp) {
		if (!(tmp->speex = speex_decoder_init(&speex_nb_mode))) {
			free(tmp);
			tmp = NULL;
		} else {
			speex_bits_init(&tmp->bits);
			speex_decoder_ctl(tmp->speex, SPEEX_GET_FRAME_SIZE, &tmp->framesize);
			if (enhancement)
				speex_decoder_ctl(tmp->speex, SPEEX_SET_ENH, &enhancement);
			tmp->tail = 0;
		}
		localusecnt++;
	}
	return tmp;
}

static struct ast_frame *lintospeex_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_speex_ex);
	/* Assume 8000 Hz */
	f.samples = sizeof(slin_speex_ex)/2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_speex_ex;
	return &f;
}

static struct ast_frame *speextolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SPEEX;
	f.datalen = sizeof(speex_slin_ex);
	/* All frames are 20 ms long */
	f.samples = 160;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = speex_slin_ex;
	return &f;
}

static struct ast_frame *speextolin_frameout(struct ast_translator_pvt *tmp)
{
	if (!tmp->tail)
		return NULL;
	/* Signed linear is no particular frame size, so just send whatever
	   we have in the buffer in one lump sum */
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_SLINEAR;
	tmp->f.datalen = tmp->tail * 2;
	/* Assume 8000 Hz */
	tmp->f.samples = tmp->tail;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->buf;
	/* Reset tail pointer */
	tmp->tail = 0;
	return &tmp->f;	
}

static int speextolin_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Assuming there's space left, decode into the current buffer at
	   the tail location.  Read in as many frames as there are */
	int x;
	int res;
#ifdef _SPEEX_TYPES_H
	spx_int16_t out[1024];
#else
	float fout[1024];
#endif

	if (f->datalen == 0) {  /* Native PLC interpolation */
		if (tmp->tail + tmp->framesize > sizeof(tmp->buf) / 2) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
#ifdef _SPEEX_TYPES_H
		speex_decode_int(tmp->speex, NULL, tmp->buf + tmp->tail);
#else
		speex_decode(tmp->speex, NULL, fout);
		for (x=0;x<tmp->framesize;x++) {
			tmp->buf[tmp->tail + x] = fout[x];
		}
#endif
		tmp->tail += tmp->framesize;
		return 0;
	}

	/* Read in bits */
	speex_bits_read_from(&tmp->bits, f->data, f->datalen);
	for(;;) {
#ifdef _SPEEX_TYPES_H
		res = speex_decode_int(tmp->speex, &tmp->bits, out);
#else
		res = speex_decode(tmp->speex, &tmp->bits, fout);
#endif
		if (res < 0)
			break;
		if (tmp->tail + tmp->framesize < sizeof(tmp->buf) / 2) {
			for (x=0;x<tmp->framesize;x++) {
#ifdef _SPEEX_TYPES_H
				tmp->buf[tmp->tail + x] = out[x];
#else
				tmp->buf[tmp->tail + x] = fout[x];
#endif
			}
			tmp->tail += tmp->framesize;
		} else {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		
	}
	return 0;
}

static int lintospeex_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Just add the frames to our stream */
	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	if (tmp->tail + f->datalen/2 < sizeof(tmp->buf) / 2) {
		memcpy((tmp->buf + tmp->tail), f->data, f->datalen);
		tmp->tail += f->datalen/2;
	} else {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	return 0;
}

static struct ast_frame *lintospeex_frameout(struct ast_translator_pvt *tmp)
{
#ifndef _SPEEX_TYPES_H
	float fbuf[1024];
	int x;
#endif
	int len;
	int y=0;
	int is_speech=1;
	/* We can't work on anything less than a frame in size */
	if (tmp->tail < tmp->framesize)
		return NULL;
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_SPEEX;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->outbuf;
	speex_bits_reset(&tmp->bits);
	while(tmp->tail >= tmp->framesize) {
#ifdef _SPEEX_TYPES_H
		/* Preprocess audio */
		if(preproc)
			is_speech = speex_preprocess(tmp->pp, tmp->buf, NULL);
		/* Encode a frame of data */
		if (is_speech) {
			/* If DTX enabled speex_encode returns 0 during silence */
			is_speech = speex_encode_int(tmp->speex, tmp->buf, &tmp->bits) || !dtx;
		} else {
			/* 5 zeros interpreted by Speex as silence (submode 0) */
			speex_bits_pack(&tmp->bits, 0, 5);
		}
#else
		/* Convert to floating point */
		for (x=0;x<tmp->framesize;x++)
			fbuf[x] = tmp->buf[x];
		/* Encode a frame of data */
		is_speech = speex_encode(tmp->speex, fbuf, &tmp->bits) || !dtx;
#endif
		/* Assume 8000 Hz -- 20 ms */
		tmp->tail -= tmp->framesize;
		/* Move the data at the end of the buffer to the front */
		if (tmp->tail)
			memmove(tmp->buf, tmp->buf + tmp->framesize, tmp->tail * 2);
		y++;
	}

	/* Use AST_FRAME_CNG to signify the start of any silence period */
	if (!is_speech) {
		if (tmp->silent_state) {
			return NULL;
		} else {
			tmp->silent_state = 1;
			speex_bits_reset(&tmp->bits);
			tmp->f.frametype = AST_FRAME_CNG;
		}
	} else {
		tmp->silent_state = 0;
	}

	/* Terminate bit stream */
	speex_bits_pack(&tmp->bits, 15, 5);
	len = speex_bits_write(&tmp->bits, (char *)tmp->outbuf, sizeof(tmp->outbuf));
	tmp->f.datalen = len;
	tmp->f.samples = y * 160;
#if 0
	{
		static int fd = -1;
		if (fd < 0) {
			fd = open("speex.raw", O_WRONLY|O_TRUNC|O_CREAT);
			if (fd > -1) {
				write(fd, tmp->f.data, tmp->f.datalen);
				close(fd);
			}
		}
	}
#endif
	return &tmp->f;	
}

static void speextolin_destroy(struct ast_translator_pvt *pvt)
{
	speex_decoder_destroy(pvt->speex);
	speex_bits_destroy(&pvt->bits);
	free(pvt);
	localusecnt--;
}

static void lintospeex_destroy(struct ast_translator_pvt *pvt)
{
#ifdef _SPEEX_TYPES_H
	if (preproc)
		speex_preprocess_state_destroy(pvt->pp);
#endif
	speex_encoder_destroy(pvt->speex);
	speex_bits_destroy(&pvt->bits);
	free(pvt);
	localusecnt--;
}

static struct ast_translator speextolin =
	{ "speextolin", 
	   AST_FORMAT_SPEEX, AST_FORMAT_SLINEAR,
	   speextolin_new,
	   speextolin_framein,
	   speextolin_frameout,
	   speextolin_destroy,
	   speextolin_sample
	   };

static struct ast_translator lintospeex =
	{ "lintospeex", 
	   AST_FORMAT_SLINEAR, AST_FORMAT_SPEEX,
	   lintospeex_new,
	   lintospeex_framein,
	   lintospeex_frameout,
	   lintospeex_destroy,
	   lintospeex_sample
	};


static void parse_config(void) 
{
	struct ast_config *cfg;
	struct ast_variable *var;
	int res;
	float res_f;

	if ((cfg = ast_config_load("codecs.conf"))) {
		if ((var = ast_variable_browse(cfg, "speex"))) {
			while (var) {
				if (!strcasecmp(var->name, "quality")) {
					res = abs(atoi(var->value));
					if (res > -1 && res < 11) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting Quality to %d\n",res);
						ast_mutex_lock(&localuser_lock);
						quality = res;
						ast_mutex_unlock(&localuser_lock);
					} else 
						ast_log(LOG_ERROR,"Error Quality must be 0-10\n");
				} else if (!strcasecmp(var->name, "complexity")) {
					res = abs(atoi(var->value));
					if (res > -1 && res < 11) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting Complexity to %d\n",res);
						ast_mutex_lock(&localuser_lock);
						complexity = res;
						ast_mutex_unlock(&localuser_lock);
					} else 
						ast_log(LOG_ERROR,"Error! Complexity must be 0-10\n");
				} else if (!strcasecmp(var->name, "vbr_quality")) {
					if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0 && res_f <= 10) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting VBR Quality to %f\n",res_f);
						ast_mutex_lock(&localuser_lock);
						vbr_quality = res_f;
						ast_mutex_unlock(&localuser_lock);
					} else
						ast_log(LOG_ERROR,"Error! VBR Quality must be 0-10\n");
				} else if (!strcasecmp(var->name, "abr_quality")) {
					ast_log(LOG_ERROR,"Error! ABR Quality setting obsolete, set ABR to desired bitrate\n");
				} else if (!strcasecmp(var->name, "enhancement")) {
					ast_mutex_lock(&localuser_lock);
					enhancement = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Perceptual Enhancement Mode. [%s]\n",enhancement ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "vbr")) {
					ast_mutex_lock(&localuser_lock);
					vbr = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: VBR Mode. [%s]\n",vbr ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "abr")) {
					res = abs(atoi(var->value));
					if (res >= 0) {
						if (option_verbose > 2) {
							if (res > 0)
								ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting ABR target bitrate to %d\n",res);
							else
								ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Disabling ABR\n");
						}
						ast_mutex_lock(&localuser_lock);
						abr = res;
						ast_mutex_unlock(&localuser_lock);
					} else 
						ast_log(LOG_ERROR,"Error! ABR target bitrate must be >= 0\n");
				} else if (!strcasecmp(var->name, "vad")) {
					ast_mutex_lock(&localuser_lock);
					vad = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: VAD Mode. [%s]\n",vad ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "dtx")) {
					ast_mutex_lock(&localuser_lock);
					dtx = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: DTX Mode. [%s]\n",dtx ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "preprocess")) {
					ast_mutex_lock(&localuser_lock);
					preproc = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessing. [%s]\n",preproc ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "pp_vad")) {
					ast_mutex_lock(&localuser_lock);
					pp_vad = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor VAD. [%s]\n",pp_vad ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "pp_agc")) {
					ast_mutex_lock(&localuser_lock);
					pp_agc = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor AGC. [%s]\n",pp_agc ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "pp_agc_level")) {
					if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting preprocessor AGC Level to %f\n",res_f);
						ast_mutex_lock(&localuser_lock);
						pp_agc_level = res_f;
						ast_mutex_unlock(&localuser_lock);
					} else
						ast_log(LOG_ERROR,"Error! Preprocessor AGC Level must be >= 0\n");
				} else if (!strcasecmp(var->name, "pp_denoise")) {
					ast_mutex_lock(&localuser_lock);
					pp_denoise = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor Denoise. [%s]\n",pp_denoise ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "pp_dereverb")) {
					ast_mutex_lock(&localuser_lock);
					pp_dereverb = ast_true(var->value) ? 1 : 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor Dereverb. [%s]\n",pp_dereverb ? "on" : "off");
					ast_mutex_unlock(&localuser_lock);
				} else if (!strcasecmp(var->name, "pp_dereverb_decay")) {
					if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting preprocessor Dereverb Decay to %f\n",res_f);
						ast_mutex_lock(&localuser_lock);
						pp_dereverb_decay = res_f;
						ast_mutex_unlock(&localuser_lock);
					} else
						ast_log(LOG_ERROR,"Error! Preprocessor Dereverb Decay must be >= 0\n");
				} else if (!strcasecmp(var->name, "pp_dereverb_level")) {
					if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting preprocessor Dereverb Level to %f\n",res_f);
						ast_mutex_lock(&localuser_lock);
						pp_dereverb_level = res_f;
						ast_mutex_unlock(&localuser_lock);
					} else
						ast_log(LOG_ERROR,"Error! Preprocessor Dereverb Level must be >= 0\n");
				}
				var = var->next;
			}
		}
		ast_config_destroy(cfg);
	}
}

int reload(void) 
{
	parse_config();
	return 0;
}

int unload_module(void)
{
	int res;
	ast_mutex_lock(&localuser_lock);
	res = ast_unregister_translator(&lintospeex);
	if (!res)
		res = ast_unregister_translator(&speextolin);
	if (localusecnt)
		res = -1;
	ast_mutex_unlock(&localuser_lock);
	return res;
}

int load_module(void)
{
	int res;
	parse_config();
	res=ast_register_translator(&speextolin);
	if (!res) 
		res=ast_register_translator(&lintospeex);
	else
		ast_unregister_translator(&speextolin);
	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
