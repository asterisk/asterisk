/*
 * Asterisk -- An open source telephony toolkit.
 *
 * The iLBC code is from The IETF code base and is copyright The Internet Society (2004)
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
 * \brief Translate between signed linear and Internet Low Bitrate Codec
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
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "ilbc/iLBC_encode.h"
#include "ilbc/iLBC_decode.h"

/* Sample frame data */
#include "slin_ilbc_ex.h"
#include "ilbc_slin_ex.h"

#define USE_ILBC_ENHANCER	0
#define ILBC_MS 			30
/* #define ILBC_MS			20 */

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt=0;

static char *tdesc = "iLBC/PCM16 (signed linear) Codec Translator";

struct ast_translator_pvt {
	iLBC_Enc_Inst_t enc;
	iLBC_Dec_Inst_t dec;
	struct ast_frame f;
	/* Space to build offset */
	char offset[AST_FRIENDLY_OFFSET];
	/* Buffer for our outgoing frame */
	short outbuf[8000];
	/* Enough to store a full second */
	short buf[8000];
	int tail;
};

#define ilbc_coder_pvt ast_translator_pvt

static struct ast_translator_pvt *lintoilbc_new(void)
{
	struct ilbc_coder_pvt *tmp;
	tmp = malloc(sizeof(struct ilbc_coder_pvt));
	if (tmp) {
		/* Shut valgrind up */
		memset(&tmp->enc, 0, sizeof(tmp->enc));
		initEncode(&tmp->enc, ILBC_MS);
		tmp->tail = 0;
		localusecnt++;
	}
	return tmp;
}

static struct ast_translator_pvt *ilbctolin_new(void)
{
	struct ilbc_coder_pvt *tmp;
	tmp = malloc(sizeof(struct ilbc_coder_pvt));
	if (tmp) {
		/* Shut valgrind up */
		memset(&tmp->dec, 0, sizeof(tmp->dec));
		initDecode(&tmp->dec, ILBC_MS, USE_ILBC_ENHANCER);
		tmp->tail = 0;
		localusecnt++;
	}
	return tmp;
}

static struct ast_frame *lintoilbc_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_ilbc_ex);
	/* Assume 8000 Hz */
	f.samples = sizeof(slin_ilbc_ex)/2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_ilbc_ex;
	return &f;
}

static struct ast_frame *ilbctolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ILBC;
	f.datalen = sizeof(ilbc_slin_ex);
	/* All frames are 30 ms long */
	f.samples = 240;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = ilbc_slin_ex;
	return &f;
}

static struct ast_frame *ilbctolin_frameout(struct ast_translator_pvt *tmp)
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

static int ilbctolin_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
{
	/* Assuming there's space left, decode into the current buffer at
	   the tail location.  Read in as many frames as there are */
	int x,i;
	float tmpf[240];

	if (f->datalen == 0) { /* native PLC */
		if (tmp->tail + 240 < sizeof(tmp->buf)/2) {	
			iLBC_decode(tmpf, NULL, &tmp->dec, 0);
			for (i=0;i<240;i++)
				tmp->buf[tmp->tail + i] = tmpf[i];
			tmp->tail+=240;
		} else {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}		
	}

	if (f->datalen % 50) {
		ast_log(LOG_WARNING, "Huh?  An ilbc frame that isn't a multiple of 50 bytes long from %s (%d)?\n", f->src, f->datalen);
		return -1;
	}
	
	for (x=0;x<f->datalen;x+=50) {
		if (tmp->tail + 240 < sizeof(tmp->buf)/2) {	
			iLBC_decode(tmpf, f->data + x, &tmp->dec, 1);
			for (i=0;i<240;i++)
				tmp->buf[tmp->tail + i] = tmpf[i];
			tmp->tail+=240;
		} else {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}		
	}
	return 0;
}

static int lintoilbc_framein(struct ast_translator_pvt *tmp, struct ast_frame *f)
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

static struct ast_frame *lintoilbc_frameout(struct ast_translator_pvt *tmp)
{
	int x=0,i;
	float tmpf[240];
	/* We can't work on anything less than a frame in size */
	if (tmp->tail < 240)
		return NULL;
	tmp->f.frametype = AST_FRAME_VOICE;
	tmp->f.subclass = AST_FORMAT_ILBC;
	tmp->f.mallocd = 0;
	tmp->f.offset = AST_FRIENDLY_OFFSET;
	tmp->f.src = __PRETTY_FUNCTION__;
	tmp->f.data = tmp->outbuf;
	while(tmp->tail >= 240) {
		if ((x+1) * 50 >= sizeof(tmp->outbuf)) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			break;
		}
		for (i=0;i<240;i++)
			tmpf[i] = tmp->buf[i];
		/* Encode a frame of data */
		iLBC_encode(((unsigned char *)(tmp->outbuf)) + (x * 50), tmpf, &tmp->enc);
		/* Assume 8000 Hz -- 20 ms */
		tmp->tail -= 240;
		/* Move the data at the end of the buffer to the front */
		if (tmp->tail)
			memmove(tmp->buf, tmp->buf + 240, tmp->tail * 2);
		x++;
	}
	tmp->f.datalen = x * 50;
	tmp->f.samples = x * 240;
#if 0
	{
		static int fd = -1;
		if (fd == -1) {
			fd = open("ilbc.out", O_CREAT|O_TRUNC|O_WRONLY, 0666);
			write(fd, tmp->f.data, tmp->f.datalen);
			close(fd);
		}
	}
#endif	
	return &tmp->f;	
}

static void ilbc_destroy_stuff(struct ast_translator_pvt *pvt)
{
	free(pvt);
	localusecnt--;
}

static struct ast_translator ilbctolin =
	{ "ilbctolin", 
	   AST_FORMAT_ILBC, AST_FORMAT_SLINEAR,
	   ilbctolin_new,
	   ilbctolin_framein,
	   ilbctolin_frameout,
	   ilbc_destroy_stuff,
	   ilbctolin_sample
	   };

static struct ast_translator lintoilbc =
	{ "lintoilbc", 
	   AST_FORMAT_SLINEAR, AST_FORMAT_ILBC,
	   lintoilbc_new,
	   lintoilbc_framein,
	   lintoilbc_frameout,
	   ilbc_destroy_stuff,
	   lintoilbc_sample
	   };

int unload_module(void)
{
	int res;
	ast_mutex_lock(&localuser_lock);
	res = ast_unregister_translator(&lintoilbc);
	if (!res)
		res = ast_unregister_translator(&ilbctolin);
	if (localusecnt)
		res = -1;
	ast_mutex_unlock(&localuser_lock);
	return res;
}

int load_module(void)
{
	int res;
	res=ast_register_translator(&ilbctolin);
	if (!res) 
		res=ast_register_translator(&lintoilbc);
	else
		ast_unregister_translator(&ilbctolin);
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
