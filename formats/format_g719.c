/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Anthony Minessale and Digium, Inc.
 * Anthony Minessale (anthmct@yahoo.com)
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief ITU G.719 , 64kbps bitrate only
 * \arg File name extensions: g719
 * \ingroup formats
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#define BUF_SIZE	160		/* 20 milliseconds == 160 bytes, 960 samples */
#define SAMPLES_TO_BYTES(x)	((typeof(x)) x / ((float) 960 / 160))
#define BYTES_TO_SAMPLES(x)	((typeof(x)) x * ((float) 960 / 160))

static struct ast_frame *g719read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass.codec = AST_FORMAT_G719;
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = BYTES_TO_SAMPLES(res);
	return &s->fr;
}

static int g719write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.codec != AST_FORMAT_G719) {
		ast_log(LOG_WARNING, "Asked to write non-G.719 frame (%s)!\n", ast_getformatname(f->subclass.codec));
		return -1;
	}
	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
		return -1;
	}
	return 0;
}

static int g719seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t offset = 0, min = 0, cur, max;

	sample_offset = SAMPLES_TO_BYTES(sample_offset);

	cur = ftello(fs->f);

	fseeko(fs->f, 0, SEEK_END);

	max = ftello(fs->f);

	if (whence == SEEK_SET)
		offset = sample_offset;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = sample_offset + cur;
	else if (whence == SEEK_END)
		offset = max - sample_offset;

	if (whence != SEEK_FORCECUR)
		offset = (offset > max) ? max : offset;

	/* always protect against seeking past begining. */
	offset = (offset < min) ? min : offset;

	return fseeko(fs->f, offset, SEEK_SET);
}

static int g719trunc(struct ast_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftello(fs->f));
}

static off_t g719tell(struct ast_filestream *fs)
{
	return BYTES_TO_SAMPLES(ftello(fs->f));
}

static const struct ast_format g719_f = {
	.name = "g719",
	.exts = "g719",
	.format = AST_FORMAT_G719,
	.write = g719write,
	.seek = g719seek,
	.trunc = g719trunc,
	.tell = g719tell,
	.read = g719read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	if (ast_format_register(&g719_f))
		return AST_MODULE_LOAD_DECLINE;

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_unregister(g719_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "ITU G.719",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);

