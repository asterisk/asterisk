/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate between signed linear and LPC10 (Linear Predictor Code)
 *
 * The lpc10 code is from a library used by nautilus, modified to be a bit
 * nicer to the compiler.
 *
 * See http://www.arl.wustl.edu/~jaf/ 
 * 
 * Copyright (C) 1999 - 2005  Digium,inc 
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */


#include <asterisk/lock.h>
#include <asterisk/translate.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#include "lpc10/lpc10.h"

/* Sample frame data */
#include "slin_lpc10_ex.h"
#include "lpc10_slin_ex.h"

/* We use a very strange format here...  I have no idea why...  The frames are 180
   samples long, which isn't even an even number of milliseconds...  Not only that
   but we hvae to waste two bits of each frame to keep them ending on a byte boundary
   because the frames are 54 bits long */

#define LPC10_BYTES_IN_COMPRESSED_FRAME (LPC10_BITS_IN_COMPRESSED_FRAME + 7)/8

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt=0;

static char *tdesc = "LPC10 2.4kbps (signed linear) Voice Coder";

static int useplc = 0;

struct ast_translator_pvt {
	union {
		struct lpc10_encoder_state *enc;
		struct lpc10_decoder_state *dec;
	} lpc10;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Buffer for our outgoing frame */
	short outbuf[8000];
	/* Enough to store a full second */
	short buf[8000];
	int tail;
	int longer;
	plc_state_t plc; /* god only knows why I bothered to implement PLC for LPC10 :) */
};

#define lpc10_coder_pvt ast_translator_pvt

static struct ast_translator_pvt *lpc10_enc_new(void)
{
	struct lpc10_coder_pvt *tmp;
	tmp = malloc(sizeof(struct lpc10_coder_pvt));
	if (tmp) {
		if (!(tmp->lpc10.enc = create_lpc10_encoder_state())) {
			free(tmp);
			tmp = NULL;
		}
		tmp->tail = 0;
		tmp->longer = 0;
		localusecnt++;
	}
	return tmp;
}

static struct ast_translator_pvt *lpc10_dec_new(void)
{
	struct lpc10_coder_pvt *tmp;
	tmp = malloc(sizeof(struct lpc10_coder_pvt));
	if (tmp) {
		if (!(tmp->lpc10.dec = create_lpc10_decoder_state())) {
			free(tmp);
			tmp = NULL;
		}
		tmp->tail = 0;
		tmp->longer = 0;
		plc_init(&tmp->plc);
		localusecnt++;
	}
	return tmp;
}
static struct ast_frame *lintolpc10_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_lpc10_ex);
	/* Assume 8000 Hz */
	f.samples = LPC10_SAMPLES_PER_FRAME;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_lpc10_ex;
	return &f;
}

static struct ast_frame *lpc10tolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_LPC10;
	f.datalen = sizeof(lpc10_slin_ex);
	/* All frames are 22 ms long (maybe a little more -- why did he choose
	   LPC10_SAMPLES_PER_FRAME sample frames anyway?? */
	f.samples = LPC10_SAMPLES_PER_FRAME;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = lpc10_slin_ex;
	return &f;
}

static struct ast_frame *lpc10tolin_frameout(struct ast_translator_pvt *tmp)
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

#if 0
	/* Save a sample frame */
	{ static int samplefr = 0;
	if (samplefr == 80) {
		int fd;
		fd = open("lpc10.example", O_WRONLY | O_CREAT, 0644);
		write(fd, tmp->f.data, tmp->f.datalen);
		close(fd);
	} 		
	samplefr++;
	}
#endif
	return &tmp->f;	
}

static void extract_bits(INT32 *bits, unsigned char *c)
{
	int x;
	for (x=0;x<LPC10_BITS_IN_COMPRESSED_FRAME;x++) {
		if (*c & (0x80 >> (x & 7)))
			bits[x] = 1;
		else
			bits[x] = 0;
		if ((x & 7) == 7)
			c++;
	}
}

static void build_bits(unsigned char *c, INT32 *bits)
{
	unsigned char mask=0x80;
	int x;
	*c = 0;
	for (x=0;x<LPC10_BITS_IN_COMPRESSED_FRAME;x++) {
		if (bits[x])
			*c |= mask;
		mask = mask >> 1;
		if ((x % 8)==7) {
			c++;
			*c = 0;
			mask = 0x80;
		}
	}
}

static int lpc10tolin_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Assuming there's space left, decode into the current buffer at
	   the tail location */
	int x;
	int len=0;
	float tmpbuf[LPC10_SAMPLES_PER_FRAME];
	short *sd;
	INT32 bits[LPC10_BITS_IN_COMPRESSED_FRAME];

	if(f->datalen == 0) { /* perform PLC with nominal framesize of LPC10_SAMPLES_PER_FRAME */
	      if((tmp->tail + LPC10_SAMPLES_PER_FRAME) > sizeof(tmp->buf)/2) {
		  ast_log(LOG_WARNING, "Out of buffer space\n");
		  return -1;
	      }
	      if(useplc) {
		  plc_fillin(&tmp->plc, tmp->buf+tmp->tail, LPC10_SAMPLES_PER_FRAME);
		  tmp->tail += LPC10_SAMPLES_PER_FRAME;
	      }
	      return 0;
	}

	while(len + LPC10_BYTES_IN_COMPRESSED_FRAME <= f->datalen) {
		if (tmp->tail + LPC10_SAMPLES_PER_FRAME < sizeof(tmp->buf)/2) {
			sd = tmp->buf + tmp->tail;
			extract_bits(bits, f->data + len);
			if (lpc10_decode(bits, tmpbuf, tmp->lpc10.dec)) {
				ast_log(LOG_WARNING, "Invalid lpc10 data\n");
				return -1;
			}
			for (x=0;x<LPC10_SAMPLES_PER_FRAME;x++) {
				/* Convert to a real between -1.0 and 1.0 */
				sd[x] = 32768.0 * tmpbuf[x];
			}

			if(useplc) plc_rx(&tmp->plc, tmp->buf + tmp->tail, LPC10_SAMPLES_PER_FRAME);
			
			tmp->tail+=LPC10_SAMPLES_PER_FRAME;
		} else {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		len += LPC10_BYTES_IN_COMPRESSED_FRAME;
	}
	if (len != f->datalen) 
		printf("Decoded %d, expected %d\n", len, f->datalen);
	return 0;
}

static int lintolpc10_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
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

static struct ast_frame *lintolpc10_frameout(struct ast_translator_pvt *tmp)
{
	int x;
	int consumed = 0;
	float tmpbuf[LPC10_SAMPLES_PER_FRAME];
	INT32 bits[LPC10_BITS_IN_COMPRESSED_FRAME];
	/* We can't work on anything less than a frame in size */
	if (tmp->tail < LPC10_SAMPLES_PER_FRAME)
		return NULL;
	/* Start with an empty frame */
	tmp->f.samples = 0;
	tmp->f.datalen = 0;
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_LPC10;
	while(tmp->tail >=  LPC10_SAMPLES_PER_FRAME) {
		if (tmp->f.datalen + LPC10_BYTES_IN_COMPRESSED_FRAME > sizeof(tmp->outbuf)) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return NULL;
		}
		/* Encode a frame of data */
		for (x=0;x<LPC10_SAMPLES_PER_FRAME;x++) {
			tmpbuf[x] = (float)tmp->buf[x+consumed] / 32768.0;
		}
		lpc10_encode(tmpbuf, bits, tmp->lpc10.enc);
		build_bits(((unsigned char *)tmp->outbuf) + tmp->f.datalen, bits);
		tmp->f.datalen += LPC10_BYTES_IN_COMPRESSED_FRAME;
		tmp->f.samples += LPC10_SAMPLES_PER_FRAME;
		/* Use one of the two left over bits to record if this is a 22 or 23 ms frame...
		   important for IAX use */
		tmp->longer = 1 - tmp->longer;
#if 0	/* what the heck was this for? */
		((char *)(tmp->f.data))[consumed - 1] |= tmp->longer;
#endif		
		tmp->tail -= LPC10_SAMPLES_PER_FRAME;
		consumed += LPC10_SAMPLES_PER_FRAME;
	}
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->outbuf;
	/* Move the data at the end of the buffer to the front */
	if (tmp->tail)
		memmove(tmp->buf, tmp->buf + consumed, tmp->tail * 2);
#if 0
	/* Save a sample frame */
	{ static int samplefr = 0;
	if (samplefr == 0) {
		int fd;
		fd = open("lpc10.example", O_WRONLY | O_CREAT, 0644);
		write(fd, tmp->f.data, tmp->f.datalen);
		close(fd);
	} 		
	samplefr++;
	}
#endif
	return &tmp->f;	
}

static void lpc10_destroy(struct ast_translator_pvt *pvt)
{
	/* Enc and DEC are both just allocated, so they can be freed */
	free(pvt->lpc10.enc);
	free(pvt);
	localusecnt--;
}

static struct ast_translator lpc10tolin =
	{ "lpc10tolin", 
	   AST_FORMAT_LPC10, AST_FORMAT_SLINEAR,
	   lpc10_dec_new,
	   lpc10tolin_framein,
	   lpc10tolin_frameout,
	   lpc10_destroy,
	   lpc10tolin_sample
	   };

static struct ast_translator lintolpc10 =
	{ "lintolpc10", 
	   AST_FORMAT_SLINEAR, AST_FORMAT_LPC10,
	   lpc10_enc_new,
	   lintolpc10_framein,
	   lintolpc10_frameout,
	   lpc10_destroy,
	   lintolpc10_sample
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
                                               ast_verbose(VERBOSE_PREFIX_3 "CODEC ULAW: %susing generic PLC\n", useplc ? "" : "not ");
                               }
                               var = var->next;
                        }
                }
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
	res = ast_unregister_translator(&lintolpc10);
	if (!res)
		res = ast_unregister_translator(&lpc10tolin);
	if (localusecnt)
		res = -1;
	ast_mutex_unlock(&localuser_lock);
	return res;
}

int load_module(void)
{
	int res;
	parse_config();
	res=ast_register_translator(&lpc10tolin);
	if (!res) 
		res=ast_register_translator(&lintolpc10);
	else
		ast_unregister_translator(&lpc10tolin);
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
