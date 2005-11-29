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

#include "gsm/inc/gsm.h"
#include "../formats/msgsm.h"

/* Sample frame data */
#include "slin_gsm_ex.h"
#include "gsm_slin_ex.h"

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt=0;

static char *tdesc = "GSM/PCM16 (signed linear) Codec Translator";

static int useplc = 0;

struct ast_translator_pvt {
	gsm gsm;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Buffer for our outgoing frame */
	short outbuf[8000];
	/* Enough to store a full second */
	short buf[8000];
	int tail;
	plc_state_t plc;
};

#define gsm_coder_pvt ast_translator_pvt

static struct ast_translator_pvt *gsm_new(void)
{
	struct gsm_coder_pvt *tmp;
	tmp = malloc(sizeof(struct gsm_coder_pvt));
	if (tmp) {
		if (!(tmp->gsm = gsm_create())) {
			free(tmp);
			tmp = NULL;
		}
		tmp->tail = 0;
		plc_init(&tmp->plc);
		localusecnt++;
	}
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
	f.samples = 160;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = gsm_slin_ex;
	return &f;
}

static struct ast_frame *gsmtolin_frameout(struct ast_translator_pvt *tmp)
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

static int gsmtolin_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Assuming there's space left, decode into the current buffer at
	   the tail location.  Read in as many frames as there are */
	int x;
	unsigned char data[66];
	int msgsm=0;
	
	if(f->datalen == 0) { /* perform PLC with nominal framesize of 20ms/160 samples */
	      if((tmp->tail + 160) > sizeof(tmp->buf) / 2) {
		  ast_log(LOG_WARNING, "Out of buffer space\n");
		  return -1;
	      }
	      if(useplc) {
		  plc_fillin(&tmp->plc, tmp->buf+tmp->tail, 160);
		  tmp->tail += 160;
	      }
	      return 0;
	}

	if ((f->datalen % 33) && (f->datalen % 65)) {
		ast_log(LOG_WARNING, "Huh?  A GSM frame that isn't a multiple of 33 or 65 bytes long from %s (%d)?\n", f->src, f->datalen);
		return -1;
	}
	
	if (f->datalen % 65 == 0) 
		msgsm = 1;
		
	for (x=0;x<f->datalen;x+=(msgsm ? 65 : 33)) {
		if (msgsm) {
			/* Translate MSGSM format to Real GSM format before feeding in */
			conv65(f->data + x, data);
			if (tmp->tail + 320 < sizeof(tmp->buf)/2) {	
				if (gsm_decode(tmp->gsm, data, tmp->buf + tmp->tail)) {
					ast_log(LOG_WARNING, "Invalid GSM data (1)\n");
					return -1;
				}
				tmp->tail+=160;
				if (gsm_decode(tmp->gsm, data + 33, tmp->buf + tmp->tail)) {
					ast_log(LOG_WARNING, "Invalid GSM data (2)\n");
					return -1;
				}
				tmp->tail+=160;
			} else {
				ast_log(LOG_WARNING, "Out of (MS) buffer space\n");
				return -1;
			}
		} else {
			if (tmp->tail + 160 < sizeof(tmp->buf)/2) {	
				if (gsm_decode(tmp->gsm, f->data + x, tmp->buf + tmp->tail)) {
					ast_log(LOG_WARNING, "Invalid GSM data\n");
					return -1;
				}
				tmp->tail+=160;
			} else {
				ast_log(LOG_WARNING, "Out of buffer space\n");
				return -1;
			}
		}
	}

	/* just add the last 20ms frame; there must have been at least one */
	if(useplc) plc_rx(&tmp->plc, tmp->buf+tmp->tail-160, 160);

	return 0;
}

static int lintogsm_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
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

static struct ast_frame *lintogsm_frameout(struct ast_translator_pvt *tmp)
{
	int x=0;
	/* We can't work on anything less than a frame in size */
	if (tmp->tail < 160)
		return NULL;
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_GSM;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->outbuf;
	while(tmp->tail >= 160) {
		if ((x+1) * 33 >= sizeof(tmp->outbuf)) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			break;
		}
		/* Encode a frame of data */
		gsm_encode(tmp->gsm, tmp->buf, ((gsm_byte *) tmp->outbuf) + (x * 33));
		/* Assume 8000 Hz -- 20 ms */
		tmp->tail -= 160;
		/* Move the data at the end of the buffer to the front */
		if (tmp->tail)
			memmove(tmp->buf, tmp->buf + 160, tmp->tail * 2);
		x++;
	}
	tmp->f.datalen = x * 33;
	tmp->f.samples = x * 160;
	return &tmp->f;	
}

static void gsm_destroy_stuff(struct ast_translator_pvt *pvt)
{
	if (pvt->gsm)
		gsm_destroy(pvt->gsm);
	free(pvt);
	localusecnt--;
}

static struct ast_translator gsmtolin =
	{ "gsmtolin", 
	   AST_FORMAT_GSM, AST_FORMAT_SLINEAR,
	   gsm_new,
	   gsmtolin_framein,
	   gsmtolin_frameout,
	   gsm_destroy_stuff,
	   gsmtolin_sample
	   };

static struct ast_translator lintogsm =
	{ "lintogsm", 
	   AST_FORMAT_SLINEAR, AST_FORMAT_GSM,
	   gsm_new,
	   lintogsm_framein,
	   lintogsm_frameout,
	   gsm_destroy_stuff,
	   lintogsm_sample
	   };


static void parse_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	if ((cfg = ast_config_load("codecs.conf"))) {
		if ((var = ast_variable_browse(cfg, "plc"))) {
			while (var) {
			       if (!strcasecmp(var->name, "genericplc")) {
				       useplc = ast_true(var->value) ? 1 : 0;
				       if (option_verbose > 2)
					       ast_verbose(VERBOSE_PREFIX_3 "codec_gsm: %susing generic PLC\n", useplc ? "" : "not ");
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
	res = ast_unregister_translator(&lintogsm);
	if (!res)
		res = ast_unregister_translator(&gsmtolin);
	if (localusecnt)
		res = -1;
	ast_mutex_unlock(&localuser_lock);
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
