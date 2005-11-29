/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MP3 Decoder
 *
 * The MP3 code is from freeamp, which in turn is from xingmp3's release
 * which I can't seem to find anywhere
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/translate.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#include "mp3/include/L3.h"
#include "mp3/include/mhead.h"

#include "mp3anal.h"

/* Sample frame data */
#include "mp3_slin_ex.h"

#define MAX_OUT_FRAME 320

#define MAX_FRAME_SIZE 1441
#define MAX_OUTPUT_LEN 2304

static pthread_mutex_t localuser_lock = PTHREAD_MUTEX_INITIALIZER;
static int localusecnt=0;

static char *tdesc = "MP3/PCM16 (signed linear) Translator (Decoder only)";

struct ast_translator_pvt {
	MPEG m;
	MPEG_HEAD head;
	DEC_INFO info;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Mini buffer */
	char outbuf[MAX_OUT_FRAME];
	/* Enough to store a full second */
	short buf[32000];
	/* Tail of signed linear stuff */
	int tail;
	/* Current bitrate */
	int bitrate;
	/* XXX What's forward? XXX */
	int forward;
	/* Have we called head info yet? */
	int init;
	int copy;
};

#define mp3_coder_pvt ast_translator_pvt

static struct ast_translator_pvt *mp3_new()
{
	struct mp3_coder_pvt *tmp;
	tmp = malloc(sizeof(struct mp3_coder_pvt));
	if (tmp) {
		tmp->init = 0;
		tmp->tail = 0;
		tmp->copy = -1;
		mpeg_init(&tmp->m);
	}
	return tmp;
}

static struct ast_frame *mp3tolin_sample()
{
	static struct ast_frame f;
	int size;
	if (mp3_badheader(mp3_slin_ex)) {
		ast_log(LOG_WARNING, "Bad MP3 sample??\n");
		return NULL;
	}
	size = mp3_framelen(mp3_slin_ex);
	if (size < 1) {
		ast_log(LOG_WARNING, "Failed to size??\n");
		return NULL;
	}
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_MP3;
	f.data = mp3_slin_ex;
	f.datalen = sizeof(mp3_slin_ex);
	/* Dunno how long an mp3 frame is -- kinda irrelevant anyway */
	f.timelen = 30;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	return &f;
}

static struct ast_frame *mp3tolin_frameout(struct ast_translator_pvt *tmp)
{
	int sent;
	if (!tmp->tail)
		return NULL;
	sent = tmp->tail;
	if (sent > MAX_OUT_FRAME/2)
		sent = MAX_OUT_FRAME/2;
	/* Signed linear is no particular frame size, so just send whatever
	   we have in the buffer in one lump sum */
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_SLINEAR;
	tmp->f.datalen = sent * 2;
	/* Assume 8000 Hz */
	tmp->f.timelen = sent / 8;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	memcpy(tmp->outbuf, tmp->buf, tmp->tail * 2);
	tmp->f.data = tmp->outbuf;
	/* Reset tail pointer */
	tmp->tail -= sent;
	if (tmp->tail) 
		memmove(tmp->buf, tmp->buf + sent, tmp->tail * 2);

#if 0
	/* Save a sample frame */
	{ static int samplefr = 0;
	if (samplefr == 80) {
		int fd;
		fd = open("mp3.example", O_WRONLY | O_CREAT, 0644);
		write(fd, tmp->f.data, tmp->f.datalen);
		close(fd);
	} 		
	samplefr++;
	}
#endif
	return &tmp->f;	
}

static int mp3_init(struct ast_translator_pvt *tmp, int len)
{	
	if (!audio_decode_init(&tmp->m, &tmp->head, len,0,0,1 /* Convert to mono */,24000)) {
		ast_log(LOG_WARNING, "audio_decode_init() failed\n");
		return -1;
	}
	audio_decode_info(&tmp->m, &tmp->info);
#if 0
	ast_verbose(
"Channels: %d\nOutValues: %d\nSample Rate: %d\nBits: %d\nFramebytes: %d\nType: %d\n",
	tmp->info.channels, tmp->info.outvalues, tmp->info.samprate, tmp->info.bits,tmp->info.framebytes,tmp->info.type);
#endif
	return 0;
}

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#if 1
static int add_to_buf(short *dst, int maxdst, short *src, int srclen, int samprate)
{
	float inc, cur, sum=0;
	int cnt=0, pos, ptr, lastpos = -1;
	/* Resample source to destination converting from its sampling rate to 8000 Hz */
	if (samprate == 8000) {
		/* Quickly, all we have to do is copy */
		memcpy(dst, src, 2 * MIN(maxdst, srclen));
		return MIN(maxdst, srclen);
	}
	if (samprate < 8000) {
		ast_log(LOG_WARNING, "Don't know how to resample a source less than 8000 Hz!\n");
		/* XXX Wrong thing to do XXX */
		memcpy(dst, src, 2 * MIN(maxdst, srclen));
		return MIN(maxdst, srclen);
	}
	/* Ugh, we actually *have* to resample */
	inc = 8000.0 / (float)samprate;
	cur = 0;
	ptr = 0;
	pos = 0;
#if 0
	ast_verbose("Incrementing by %f, in = %d bytes, out = %d bytes\n", inc, srclen, maxdst);
#endif
	while((pos < maxdst) && (ptr < srclen)) {
		if (pos != lastpos) {
			if (lastpos > -1) {
				sum = sum / (float)cnt;
				dst[pos - 1] = (int) sum;
#if 0
				ast_verbose("dst[%d] = %d\n", pos - 1, dst[pos - 1]);
#endif
			}
			/* Each time we have a first pass */
			sum = 0;
			cnt = 0;
		} else {
			sum += src[ptr];
		}
		ptr++;
		cur += inc;
		cnt++;
		lastpos = pos;
		pos = (int)cur;
	}
	return pos;
}
#endif

static int mp3tolin_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Assuming there's space left, decode into the current buffer at
	   the tail location */
	int framelen;
	short tmpbuf[8000];
	IN_OUT x;
#if 0
	if (tmp->copy < 0) {
		tmp->copy = open("sample.out", O_WRONLY | O_CREAT | O_TRUNC, 0700);
	}
	if (tmp->copy > -1)
		write(tmp->copy, f->data, f->datalen);
#endif
	/* Check if it's a valid frame */
	if (mp3_badheader((unsigned char *)f->data)) {
		ast_log(LOG_WARNING, "Invalid MP3 header\n");
		return -1;
	}
	if ((framelen = mp3_framelen((unsigned char *)f->data) != f->datalen)) {
		ast_log(LOG_WARNING, "Calculated length %d does not match real length %d\n", framelen, f->datalen);
		return -1;
	}
	/* Start by putting this in the mp3 buffer */
	if((framelen = head_info3(f->data, 
			f->datalen, &tmp->head, &tmp->bitrate, &tmp->forward)) > 0) {
		if (!tmp->init) {
			if (mp3_init(tmp, framelen))
				return -1;
			else
				tmp->init++;
		}
		if (tmp->tail + MAX_OUTPUT_LEN/2  < sizeof(tmp->buf)/2) {	
			x = audio_decode(&tmp->m, f->data, tmpbuf);
			audio_decode_info(&tmp->m, &tmp->info);
			if (!x.in_bytes) {
				ast_log(LOG_WARNING, "Invalid MP3 data\n");
			} else {
#if 1
				/* Resample to 8000 Hz */
				tmp->tail += add_to_buf(tmp->buf + tmp->tail, 
			           sizeof(tmp->buf) / 2 - tmp->tail, 
					   tmpbuf,
					   x.out_bytes/2,
					   tmp->info.samprate);
#else
				memcpy(tmp->buf + tmp->tail, tmpbuf, x.out_bytes);
				/* Signed linear output */
				tmp->tail+=x.out_bytes/2;
#endif
			}
		} else {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Not a valid MP3 frame\n");
	}
	return 0;
}

static void mp3_destroy_stuff(struct ast_translator_pvt *pvt)
{
	close(pvt->copy);
	free(pvt);
}

static struct ast_translator mp3tolin =
	{ "mp3tolin", 
	   AST_FORMAT_MP3, AST_FORMAT_SLINEAR,
	   mp3_new,
	   mp3tolin_framein,
	   mp3tolin_frameout,
	   mp3_destroy_stuff,
	   mp3tolin_sample
	   };

int unload_module(void)
{
	int res;
	pthread_mutex_lock(&localuser_lock);
	res = ast_unregister_translator(&mp3tolin);
	if (localusecnt)
		res = -1;
	pthread_mutex_unlock(&localuser_lock);
	return res;
}

int load_module(void)
{
	int res;
	res=ast_register_translator(&mp3tolin);
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
