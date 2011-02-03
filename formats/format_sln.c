/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale
 * Anthony Minessale (anthmct@yahoo.com)
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
 * \brief RAW SLINEAR Format
 * \arg File name extensions: sln, raw
 * \ingroup formats
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#define BUF_SIZE	320		/* 320 bytes, 160 samples */
#define	SLIN_SAMPLES	160

static struct ast_frame *slinear_read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&s->fr.subclass.format, AST_FORMAT_SLINEAR, 0);
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = res/2;
	s->fr.datalen = res;
	return &s->fr;
}

static int slinear_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.format.id != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Asked to write non-slinear frame (%s)!\n", ast_getformatname(&f->subclass.format));
		return -1;
	}
	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int slinear_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t offset=0,min,cur,max;

	min = 0;
	sample_offset <<= 1;
	cur = ftello(fs->f);
	fseeko(fs->f, 0, SEEK_END);
	max = ftello(fs->f);
	if (whence == SEEK_SET)
		offset = sample_offset;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = sample_offset + cur;
	else if (whence == SEEK_END)
		offset = max - sample_offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* always protect against seeking past begining. */
	offset = (offset < min)?min:offset;
	return fseeko(fs->f, offset, SEEK_SET);
}

static int slinear_trunc(struct ast_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftello(fs->f));
}

static off_t slinear_tell(struct ast_filestream *fs)
{
	return ftello(fs->f) / 2;
}

static struct ast_format_def slin_f = {
	.name = "sln",
	.exts = "sln|raw",
	.write = slinear_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	ast_format_set(&slin_f.format, AST_FORMAT_SLINEAR, 0);
	if (ast_format_def_register(&slin_f))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(slin_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw Signed Linear Audio support (SLN)",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
