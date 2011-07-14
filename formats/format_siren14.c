/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Anthony Minessale and Digium, Inc.
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
 * \brief ITU G.722.1 Annex C (Siren14, licensed from Polycom) format, 48kbps bitrate only
 * \arg File name extensions: siren14
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

#define BUF_SIZE	120		/* 20 milliseconds == 120 bytes, 640 samples */
#define SAMPLES_TO_BYTES(x)	((typeof(x)) x / ((float) 640 / 120))
#define BYTES_TO_SAMPLES(x)	((typeof(x)) x * ((float) 640 / 120))

static struct ast_frame *siren14read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass.codec = AST_FORMAT_SIREN14;
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

static int siren14write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.codec != AST_FORMAT_SIREN14) {
		ast_log(LOG_WARNING, "Asked to write non-Siren14 frame (%s)!\n", ast_getformatname(f->subclass.codec));
		return -1;
	}
	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
		return -1;
	}
	return 0;
}

static int siren14seek(struct ast_filestream *fs, off_t sample_offset, int whence)
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

static int siren14trunc(struct ast_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftello(fs->f));
}

static off_t siren14tell(struct ast_filestream *fs)
{
	return BYTES_TO_SAMPLES(ftello(fs->f));
}

static const struct ast_format siren14_f = {
	.name = "siren14",
	.exts = "siren14",
	.format = AST_FORMAT_SIREN14,
	.write = siren14write,
	.seek = siren14seek,
	.trunc = siren14trunc,
	.tell = siren14tell,
	.read = siren14read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	if (ast_format_register(&siren14_f))
		return AST_MODULE_LOAD_DECLINE;

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_unregister(siren14_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "ITU G.722.1 Annex C (Siren14, licensed from Polycom)",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
