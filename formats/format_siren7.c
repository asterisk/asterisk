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
 * \brief ITU G.722.1 (Siren7, licensed from Polycom) format, 32kbps bitrate only
 * \arg File name extensions: siren7
 * \ingroup formats
 */

/*** MODULEINFO
	<load_priority>app_depend</load_priority>
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"
#include "asterisk/format_cache.h"

#define BUF_SIZE	80		/* 20 milliseconds == 80 bytes, 320 samples */
#define SAMPLES_TO_BYTES(x)	x / (320 / 80)
#define BYTES_TO_SAMPLES(x)	x * (320 / 80)

static struct ast_frame *siren7read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = BYTES_TO_SAMPLES(res);
	return &s->fr;
}

static int siren7write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;

	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
		return -1;
	}
	return 0;
}

static int siren7seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t offset = 0, min = 0, cur, max;

	sample_offset = SAMPLES_TO_BYTES(sample_offset);

	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in siren7 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if (fseeko(fs->f, 0, SEEK_END) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to seek to end of siren7 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if ((max = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine max position in siren7 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

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

static int siren7trunc(struct ast_filestream *fs)
{
	int fd;
	off_t cur;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for siren7 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in siren7 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	/* Truncate file to current length */
	return ftruncate(fd, cur);
}

static off_t siren7tell(struct ast_filestream *fs)
{
	return BYTES_TO_SAMPLES(ftello(fs->f));
}

static struct ast_format_def siren7_f = {
	.name = "siren7",
	.exts = "siren7",
	.write = siren7write,
	.seek = siren7seek,
	.trunc = siren7trunc,
	.tell = siren7tell,
	.read = siren7read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	siren7_f.format = ast_format_siren7;
	if (ast_format_def_register(&siren7_f))
		return AST_MODULE_LOAD_DECLINE;

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "ITU G.722.1 (Siren7, licensed from Polycom)");
