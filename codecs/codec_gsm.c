/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate between signed linear and Global System for Mobile Communications (GSM)
 *
 * The GSM code is from TOAST.  Copyright information for that package is available
 * in  the GSM directory.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#define TYPE_SILENCE	 0x2
#define TYPE_HIGH	 0x0
#define TYPE_LOW	 0x1
#define TYPE_MASK	 0x3

#include <asterisk/translate.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#include "gsm/inc/gsm.h"

/* Sample frame data */
#include "slin_gsm_ex.h"
#include "gsm_slin_ex.h"

static pthread_mutex_t localuser_lock = PTHREAD_MUTEX_INITIALIZER;
static int localusecnt=0;

static char *tdesc = "GSM/PCM16 (signed linear) Codec Translator";

struct ast_translator_pvt {
	gsm gsm;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Buffer for our outgoing frame */
	gsm_frame outbuf;
	/* Enough to store a full second */
	short buf[8000];
	int tail;
};

#define gsm_coder_pvt ast_translator_pvt

static struct ast_translator_pvt *gsm_new()
{
	struct gsm_coder_pvt *tmp;
	tmp = malloc(sizeof(struct gsm_coder_pvt));
	if (tmp) {
		if (!(tmp->gsm = gsm_create())) {
			free(tmp);
			tmp = NULL;
		}
		tmp->tail = 0;
		localusecnt++;
	}
	return tmp;
}

static struct ast_frame *lintogsm_sample()
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_gsm_ex);
	/* Assume 8000 Hz */
	f.timelen = sizeof(slin_gsm_ex)/16;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_gsm_ex;
	return &f;
}

static struct ast_frame *gsmtolin_sample()
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_GSM;
	f.datalen = sizeof(gsm_slin_ex);
	/* All frames are 20 ms long */
	f.timelen = 20;
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
	tmp->f.timelen = tmp->tail / 8;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->buf;
	/* Reset tail pointer */
	tmp->tail = 0;

#if 0
	/* Save a sample frame */
	{ static int samplefr = 0;
	if (samplefr == 80) {
		int fd;
		fd = open("gsm.example", O_WRONLY | O_CREAT, 0644);
		write(fd, tmp->f.data, tmp->f.datalen);
		close(fd);
	} 		
	samplefr++;
	}
#endif
	return &tmp->f;	
}

static int gsmtolin_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Assuming there's space left, decode into the current buffer at
	   the tail location */
	if (tmp->tail + 160 < sizeof(tmp->buf)/2) {	
		if (gsm_decode(tmp->gsm, f->data, tmp->buf + tmp->tail)) {
			ast_log(LOG_WARNING, "Invalid GSM data\n");
			return -1;
		}
		tmp->tail+=160;
	} else {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	return 0;
}

static int lintogsm_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Just add the frames to our stream */
	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	if (tmp->tail + f->datalen < sizeof(tmp->buf) / 2) {
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
	/* We can't work on anything less than a frame in size */
	if (tmp->tail < 160)
		return NULL;
	/* Encode a frame of data */
	gsm_encode(tmp->gsm, tmp->buf, tmp->outbuf);
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_GSM;
	tmp->f.datalen = 33;
	/* Assume 8000 Hz -- 20 ms */
	tmp->f.timelen = 20;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->outbuf;
	tmp->tail -= 160;
	/* Move the data at the end of the buffer to the front */
	if (tmp->tail)
		memmove(tmp->buf, tmp->buf + 160, tmp->tail * 2);
#if 0
	/* Save a sample frame */
	{ static int samplefr = 0;
	if (samplefr == 0) {
		int fd;
		fd = open("gsm.example", O_WRONLY | O_CREAT, 0644);
		write(fd, tmp->f.data, tmp->f.datalen);
		close(fd);
	} 		
	samplefr++;
	}
#endif
	return &tmp->f;	
}

static void gsm_destroy_stuff(struct ast_translator_pvt *pvt)
{
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

int unload_module(void)
{
	int res;
	pthread_mutex_lock(&localuser_lock);
	res = ast_unregister_translator(&lintogsm);
	if (!res)
		res = ast_unregister_translator(&gsmtolin);
	if (localusecnt)
		res = -1;
	pthread_mutex_unlock(&localuser_lock);
	return res;
}

int load_module(void)
{
	int res;
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
