/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate between signed linear and G.723.1
 *
 * The G.723.1 code is not included in the Asterisk distribution because
 * it is covered with patents, and in spite of statements to the contrary,
 * the "technology" is extremely expensive to license.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#define TYPE_HIGH	 0x0
#define TYPE_LOW	 0x1
#define TYPE_SILENCE	 0x2
#define TYPE_DONTSEND	 0x3
#define TYPE_MASK	 0x3

#include <sys/types.h>
#include <asterisk/lock.h>
#include <asterisk/translate.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

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

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt=0;

#ifdef ANNEX_B
static char *tdesc = "Annex B (floating point) G.723.1/PCM16 Codec Translator";
#else
static char *tdesc = "Annex A (fixed point) G.723.1/PCM16 Codec Translator";
#endif

/* Globals */
Flag UsePf = True;
Flag UseHp = True;
Flag UseVx = True;

enum Crate WrkRate = Rate63;

struct g723_encoder_pvt {
	struct cod_state cod;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Buffer for our outgoing frame */
	char outbuf[8000];
	/* Enough to store a full second */
	short buf[8000];
	int tail;
};

struct g723_decoder_pvt {
	struct dec_state dec;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Enough to store a full second */
	short buf[8000];
	int tail;
};

static struct ast_translator_pvt *g723tolin_new(void)
{
	struct g723_decoder_pvt *tmp;
	tmp = malloc(sizeof(struct g723_decoder_pvt));
	if (tmp) {
		Init_Decod(&tmp->dec);
	    Init_Dec_Cng(&tmp->dec);
		tmp->tail = 0;
		localusecnt++;
		ast_update_use_count();
	}
	return (struct ast_translator_pvt *)tmp;
}

static struct ast_frame *lintog723_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_g723_ex);
	/* Assume 8000 Hz */
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

static struct ast_translator_pvt *lintog723_new(void)
{
	struct g723_encoder_pvt *tmp;
	tmp = malloc(sizeof(struct g723_encoder_pvt));
	if (tmp) {
		Init_Coder(&tmp->cod);
	    /* Init Comfort Noise Functions */
   		 if( UseVx ) {
   	   		Init_Vad(&tmp->cod);
        	Init_Cod_Cng(&tmp->cod);
    	 }
		localusecnt++;
		ast_update_use_count();
		tmp->tail = 0;
	}
	return (struct ast_translator_pvt *)tmp;
}

static struct ast_frame *g723tolin_frameout(struct ast_translator_pvt *pvt)
{
	struct g723_decoder_pvt *tmp = (struct g723_decoder_pvt *)pvt;
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

#if 0
	/* Save the frames */
	{ 
		static int fd2 = -1;
		if (fd2 == -1) {
			fd2 = open("g723.example", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}
		write(fd2, tmp->f.data, tmp->f.datalen);
	} 		
#endif
	return &tmp->f;	
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

static int g723tolin_framein(struct ast_translator_pvt *pvt, struct ast_frame *f)
{
	struct g723_decoder_pvt *tmp = (struct g723_decoder_pvt *)pvt;
	int len = 0;
	int res;
#ifdef  ANNEX_B
	FLOAT tmpdata[Frame];
	int x;
#endif
	while(len < f->datalen) {
		/* Assuming there's space left, decode into the current buffer at
		   the tail location */
		res = g723_len(((unsigned char *)f->data + len)[0]);
		if (res < 0) {
			ast_log(LOG_WARNING, "Invalid data\n");
			return -1;
		}
		if (res + len > f->datalen) {
			ast_log(LOG_WARNING, "Measured length exceeds frame length\n");
			return -1;
		}
		if (tmp->tail + Frame < sizeof(tmp->buf)/2) {	
#ifdef ANNEX_B
			Decod(&tmp->dec, tmpdata, f->data + len, 0);
			for (x=0;x<Frame;x++)
				(tmp->buf + tmp->tail)[x] = (short)(tmpdata[x]); 
#else
			Decod(&tmp->dec, tmp->buf + tmp->tail, f->data + len, 0);
#endif
			tmp->tail+=Frame;
		} else {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		len += res;
	}
	return 0;
}

static int lintog723_framein(struct ast_translator_pvt *pvt, struct ast_frame *f)
{
	/* Just add the frames to our stream */
	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	struct g723_encoder_pvt *tmp = (struct g723_encoder_pvt *)pvt;
	if (tmp->tail + f->datalen/2 < sizeof(tmp->buf) / 2) {
		memcpy(&tmp->buf[tmp->tail], f->data, f->datalen);
		tmp->tail += f->datalen/2;
	} else {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	return 0;
}

static struct ast_frame *lintog723_frameout(struct ast_translator_pvt *pvt)
{
	struct g723_encoder_pvt *tmp = (struct g723_encoder_pvt *)pvt;
#ifdef ANNEX_B
	int x;
	FLOAT tmpdata[Frame];
#endif
	int cnt=0;
	/* We can't work on anything less than a frame in size */
	if (tmp->tail < Frame)
		return NULL;
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_G723_1;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.samples = 0;
	tmp->f.mallocd = 0;
	while(tmp->tail >= Frame) {
		/* Encode a frame of data */
		if (cnt + 24 >= sizeof(tmp->outbuf)) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return NULL;
		}
#ifdef ANNEX_B
		for (x=0;x<Frame;x++)
			tmpdata[x] = tmp->buf[x];
		Coder(&tmp->cod, tmpdata, tmp->outbuf + cnt);
#else
		Coder(&tmp->cod, tmp->buf, tmp->outbuf + cnt);
#endif
		/* Assume 8000 Hz */
		tmp->f.samples += 240;
		cnt += g723_len(tmp->outbuf[cnt]);
		tmp->tail -= Frame;
		/* Move the data at the end of the buffer to the front */
		if (tmp->tail)
			memmove(tmp->buf, tmp->buf + Frame, tmp->tail * 2);
	}
	tmp->f.datalen = cnt;
	tmp->f.data = tmp->outbuf;
#if 0
	/* Save to a g723 sample output file... */
	{ 
		static int fd = -1;
		int delay = htonl(30);
		short size;
		if (fd < 0)
			fd = open("trans.g723", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			ast_log(LOG_WARNING, "Unable to create demo\n");
		write(fd, &delay, 4);
		size = htons(tmp->f.datalen);
		write(fd, &size, 2);
		write(fd, tmp->f.data, tmp->f.datalen);
	}
#endif
	return &tmp->f;	
}

static void g723_destroy(struct ast_translator_pvt *pvt)
{
	free(pvt);
	localusecnt--;
	ast_update_use_count();
}

static struct ast_translator g723tolin =
#ifdef ANNEX_B
	{ "g723tolinb", 
#else
	{ "g723tolin", 
#endif
	   AST_FORMAT_G723_1, AST_FORMAT_SLINEAR,
	   g723tolin_new,
	   g723tolin_framein,
	   g723tolin_frameout,
	   g723_destroy,
	   g723tolin_sample
	   };

static struct ast_translator lintog723 =
#ifdef ANNEX_B
	{ "lintog723b", 
#else
	{ "lintog723", 
#endif
	   AST_FORMAT_SLINEAR, AST_FORMAT_G723_1,
	   lintog723_new,
	   lintog723_framein,
	   lintog723_frameout,
	   g723_destroy,
	   lintog723_sample
	   };

int unload_module(void)
{
	int res;
	ast_mutex_lock(&localuser_lock);
	res = ast_unregister_translator(&lintog723);
	if (!res)
		res = ast_unregister_translator(&g723tolin);
	if (localusecnt)
		res = -1;
	ast_mutex_unlock(&localuser_lock);
	return res;
}

int load_module(void)
{
	int res;
	res=ast_register_translator(&g723tolin);
	if (!res) 
		res=ast_register_translator(&lintog723);
	else
		ast_unregister_translator(&g723tolin);
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

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
