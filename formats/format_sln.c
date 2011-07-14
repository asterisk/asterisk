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
 * \brief RAW SLINEAR Formats
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

static struct ast_frame *generic_read(struct ast_filestream *s, int *whennext, unsigned int buf_size, enum ast_format_id id)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&s->fr.subclass.format, id, 0);
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, buf_size);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = res/2;
	s->fr.datalen = res;
	return &s->fr;
}

static int generic_write(struct ast_filestream *fs, struct ast_frame *f, enum ast_format_id id)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.format.id != id) {
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

static int slinear_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR);}
static struct ast_frame *slinear_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 320, AST_FORMAT_SLINEAR);}
static struct ast_format_def slin_f = {
	.name = "sln",
	.exts = "sln|raw",
	.write = slinear_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear_read,
	.buf_size = 320 + AST_FRIENDLY_OFFSET,
};

static int slinear12_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR12);}
static struct ast_frame *slinear12_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 480, AST_FORMAT_SLINEAR12);}
static struct ast_format_def slin12_f = {
	.name = "sln12",
	.exts = "sln12",
	.write = slinear12_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear12_read,
	.buf_size = 480 + AST_FRIENDLY_OFFSET,
};

static int slinear16_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR16);}
static struct ast_frame *slinear16_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 640, AST_FORMAT_SLINEAR16);}
static struct ast_format_def slin16_f = {
	.name = "sln16",
	.exts = "sln16",
	.write = slinear16_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear16_read,
	.buf_size = 640 + AST_FRIENDLY_OFFSET,
};

static int slinear24_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR24);}
static struct ast_frame *slinear24_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 960, AST_FORMAT_SLINEAR24);}
static struct ast_format_def slin24_f = {
	.name = "sln24",
	.exts = "sln24",
	.write = slinear24_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear24_read,
	.buf_size = 960 + AST_FRIENDLY_OFFSET,
};

static int slinear32_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR32);}
static struct ast_frame *slinear32_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 1280, AST_FORMAT_SLINEAR32);}
static struct ast_format_def slin32_f = {
	.name = "sln32",
	.exts = "sln32",
	.write = slinear32_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear32_read,
	.buf_size = 1280 + AST_FRIENDLY_OFFSET,
};

static int slinear44_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR44);}
static struct ast_frame *slinear44_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 1764, AST_FORMAT_SLINEAR44);}
static struct ast_format_def slin44_f = {
	.name = "sln44",
	.exts = "sln44",
	.write = slinear44_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear44_read,
	.buf_size = 1764 + AST_FRIENDLY_OFFSET,
};

static int slinear48_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR48);}
static struct ast_frame *slinear48_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 1920, AST_FORMAT_SLINEAR48);}
static struct ast_format_def slin48_f = {
	.name = "sln48",
	.exts = "sln48",
	.write = slinear48_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear48_read,
	.buf_size = 1920 + AST_FRIENDLY_OFFSET,
};

static int slinear96_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR96);}
static struct ast_frame *slinear96_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 3840, AST_FORMAT_SLINEAR96);}
static struct ast_format_def slin96_f = {
	.name = "sln96",
	.exts = "sln96",
	.write = slinear96_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear96_read,
	.buf_size = 3840 + AST_FRIENDLY_OFFSET,
};

static int slinear192_write(struct ast_filestream *fs, struct ast_frame *f){return generic_write(fs, f, AST_FORMAT_SLINEAR192);}
static struct ast_frame *slinear192_read(struct ast_filestream *s, int *whennext){return generic_read(s, whennext, 7680, AST_FORMAT_SLINEAR192);}
static struct ast_format_def slin192_f = {
	.name = "sln192",
	.exts = "sln192",
	.write = slinear192_write,
	.seek = slinear_seek,
	.trunc = slinear_trunc,
	.tell = slinear_tell,
	.read = slinear192_read,
	.buf_size = 7680 + AST_FRIENDLY_OFFSET,
};

static struct ast_format_def *slin_list[] = {
	&slin_f,
	&slin12_f,
	&slin16_f,
	&slin24_f,
	&slin32_f,
	&slin44_f,
	&slin48_f,
	&slin96_f,
	&slin192_f,
};

static int load_module(void)
{
	int i;
	ast_format_set(&slin_f.format, AST_FORMAT_SLINEAR, 0);
	ast_format_set(&slin12_f.format, AST_FORMAT_SLINEAR12, 0);
	ast_format_set(&slin16_f.format, AST_FORMAT_SLINEAR16, 0);
	ast_format_set(&slin24_f.format, AST_FORMAT_SLINEAR24, 0);
	ast_format_set(&slin32_f.format, AST_FORMAT_SLINEAR32, 0);
	ast_format_set(&slin44_f.format, AST_FORMAT_SLINEAR44, 0);
	ast_format_set(&slin48_f.format, AST_FORMAT_SLINEAR48, 0);
	ast_format_set(&slin96_f.format, AST_FORMAT_SLINEAR96, 0);
	ast_format_set(&slin192_f.format, AST_FORMAT_SLINEAR192, 0);

	for (i = 0; i < ARRAY_LEN(slin_list); i++) {
		if (ast_format_def_register(slin_list[i])) {
			return AST_MODULE_LOAD_FAILURE;
		}
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res = 0;
	int i = 0;

	for (i = 0; i < ARRAY_LEN(slin_list); i++) {
		if (ast_format_def_unregister(slin_list[i]->name)) {
			res |= AST_MODULE_LOAD_FAILURE;
		}
	}
	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw Signed Linear Audio support (SLN) 8khz-192khz",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
