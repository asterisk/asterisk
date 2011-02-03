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
 * \brief Save to raw, headerless GSM data.
 * \arg File name extension: gsm
 * \ingroup formats
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#include "msgsm.h"

/* Some Ideas for this code came from makegsme.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define	GSM_FRAME_SIZE	33
#define	GSM_SAMPLES	160

/* silent gsm frame */
/* begin binary data: */
static const char gsm_silence[] = /* 33 */
{0xD8,0x20,0xA2,0xE1,0x5A,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49
,0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24
,0x92,0x49,0x24};
/* end binary data. size = 33 bytes */

static struct ast_frame *gsm_read(struct ast_filestream *s, int *whennext)
{
	int res;

	s->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&s->fr.subclass.format, AST_FORMAT_GSM, 0);
	AST_FRAME_SET_BUFFER(&(s->fr), s->buf, AST_FRIENDLY_OFFSET, GSM_FRAME_SIZE)
	s->fr.mallocd = 0;
	if ((res = fread(s->fr.data.ptr, 1, GSM_FRAME_SIZE, s->f)) != GSM_FRAME_SIZE) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = GSM_SAMPLES;
	return &s->fr;
}

static int gsm_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	unsigned char gsm[2*GSM_FRAME_SIZE];

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.format.id != AST_FORMAT_GSM) {
		ast_log(LOG_WARNING, "Asked to write non-GSM frame (%s)!\n", ast_getformatname(&f->subclass.format));
		return -1;
	}
	if (!(f->datalen % 65)) {
		/* This is in MSGSM format, need to be converted */
		int len=0;
		while(len < f->datalen) {
			conv65(f->data.ptr + len, gsm);
			if ((res = fwrite(gsm, 1, 2*GSM_FRAME_SIZE, fs->f)) != 2*GSM_FRAME_SIZE) {
				ast_log(LOG_WARNING, "Bad write (%d/66): %s\n", res, strerror(errno));
				return -1;
			}
			len += 65;
		}
	} else {
		if (f->datalen % GSM_FRAME_SIZE) {
			ast_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 33\n", f->datalen);
			return -1;
		}
		if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
				ast_log(LOG_WARNING, "Bad write (%d/33): %s\n", res, strerror(errno));
				return -1;
		}
	}
	return 0;
}

static int gsm_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t offset=0,min,cur,max,distance;
	
	min = 0;
	cur = ftello(fs->f);
	fseeko(fs->f, 0, SEEK_END);
	max = ftello(fs->f);
	/* have to fudge to frame here, so not fully to sample */
	distance = (sample_offset/GSM_SAMPLES) * GSM_FRAME_SIZE;
	if(whence == SEEK_SET)
		offset = distance;
	else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = distance + cur;
	else if(whence == SEEK_END)
		offset = max - distance;
	/* Always protect against seeking past the begining. */
	offset = (offset < min)?min:offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	} else if (offset > max) {
		int i;
		fseeko(fs->f, 0, SEEK_END);
		for (i=0; i< (offset - max) / GSM_FRAME_SIZE; i++) {
			if (!fwrite(gsm_silence, 1, GSM_FRAME_SIZE, fs->f)) {
				ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
			}
		}
	}
	return fseeko(fs->f, offset, SEEK_SET);
}

static int gsm_trunc(struct ast_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftello(fs->f));
}

static off_t gsm_tell(struct ast_filestream *fs)
{
	off_t offset = ftello(fs->f);
	return (offset/GSM_FRAME_SIZE)*GSM_SAMPLES;
}

static struct ast_format_def gsm_f = {
	.name = "gsm",
	.exts = "gsm",
	.write = gsm_write,
	.seek =	gsm_seek,
	.trunc = gsm_trunc,
	.tell =	gsm_tell,
	.read =	gsm_read,
	.buf_size = 2*GSM_FRAME_SIZE + AST_FRIENDLY_OFFSET,	/* 2 gsm frames */
};

static int load_module(void)
{
	ast_format_set(&gsm_f.format, AST_FORMAT_GSM, 0);
	if (ast_format_def_register(&gsm_f))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(gsm_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw GSM data",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
