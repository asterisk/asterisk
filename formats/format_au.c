/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Andriy Pylypenko
 * Code based on format_wav.c by Mark Spencer
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

/*! 
 * \file
 *
 * \brief Work with Sun Microsystems AU format.
 *
 * signed linear
 * \arg File extension: au
 * \ingroup formats
 */
 
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#define BUF_SIZE		160	/* samples and 1 byte per sample */

#define AU_HEADER_SIZE		24
#define AU_HEADER(var)		u_int32_t var[6]

#define AU_HDR_MAGIC_OFF	0
#define AU_HDR_HDR_SIZE_OFF	1
#define AU_HDR_DATA_SIZE_OFF	2
#define AU_HDR_ENCODING_OFF	3
#define AU_HDR_SAMPLE_RATE_OFF	4
#define AU_HDR_CHANNELS_OFF	5

#define AU_ENC_8BIT_ULAW	1

#define AU_MAGIC 0x2e736e64
#if __BYTE_ORDER == __BIG_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htoll(b)  \
          (((((b)      ) & 0xFF) << 24) | \
	       ((((b) >>  8) & 0xFF) << 16) | \
		   ((((b) >> 16) & 0xFF) <<  8) | \
		   ((((b) >> 24) & 0xFF)      ))
#define htols(b) \
          (((((b)      ) & 0xFF) << 8) | \
		   ((((b) >> 8) & 0xFF)      ))
#define ltohl(b) htoll(b)
#define ltohs(b) htols(b)
#else
#error "Endianess not defined"
#endif
#endif


static int check_header(FILE *f)
{
	AU_HEADER(header);
	u_int32_t magic;
	u_int32_t hdr_size;
	off_t data_size;
	u_int32_t encoding;
	u_int32_t sample_rate;
	u_int32_t channels;

	if (fread(header, 1, AU_HEADER_SIZE, f) != AU_HEADER_SIZE) {
		ast_log(LOG_WARNING, "Read failed (header)\n");
		return -1;
	}
	magic = ltohl(header[AU_HDR_MAGIC_OFF]);
	if (magic != (u_int32_t) AU_MAGIC) {
		ast_log(LOG_WARNING, "Bad magic: 0x%x\n", magic);
	}
/*	hdr_size = ltohl(header[AU_HDR_HDR_SIZE_OFF]);
	if (hdr_size < AU_HEADER_SIZE)*/
	hdr_size = AU_HEADER_SIZE;
/*	data_size = ltohl(header[AU_HDR_DATA_SIZE_OFF]); */
	encoding = ltohl(header[AU_HDR_ENCODING_OFF]);
	if (encoding != AU_ENC_8BIT_ULAW) {
		ast_log(LOG_WARNING, "Unexpected format: %d. Only 8bit ULAW allowed (%d)\n", encoding, AU_ENC_8BIT_ULAW);
		return -1;
	}
	sample_rate = ltohl(header[AU_HDR_SAMPLE_RATE_OFF]);
	if (sample_rate != DEFAULT_SAMPLE_RATE) {
		ast_log(LOG_WARNING, "Sample rate can only be 8000 not %d\n", sample_rate);
		return -1;
	}
	channels = ltohl(header[AU_HDR_CHANNELS_OFF]);
	if (channels != 1) {
		ast_log(LOG_WARNING, "Not in mono: channels=%d\n", channels);
		return -1;
	}
	/* Skip to data */
	fseek(f, 0, SEEK_END);
	data_size = ftello(f) - hdr_size;
	if (fseek(f, hdr_size, SEEK_SET) == -1 ) {
		ast_log(LOG_WARNING, "Failed to skip to data: %d\n", hdr_size);
		return -1;
	}
	return data_size;
}

static int update_header(FILE *f)
{
	off_t cur, end;
	u_int32_t datalen;
	int bytes;

	cur = ftello(f);
	fseek(f, 0, SEEK_END);
	end = ftello(f);
	/* data starts 24 bytes in */
	bytes = end - AU_HEADER_SIZE;
	datalen = htoll(bytes);

	if (cur < 0) {
		ast_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (fseek(f, AU_HDR_DATA_SIZE_OFF * sizeof(u_int32_t), SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&datalen, 1, sizeof(datalen), f) != sizeof(datalen)) {
		ast_log(LOG_WARNING, "Unable to set write file size\n");
		return -1;
	}
	if (fseek(f, cur, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to return to position\n");
		return -1;
	}
	return 0;
}

static int write_header(FILE *f)
{
	AU_HEADER(header);

	header[AU_HDR_MAGIC_OFF] = htoll((u_int32_t) AU_MAGIC);
	header[AU_HDR_HDR_SIZE_OFF] = htoll(AU_HEADER_SIZE);
	header[AU_HDR_DATA_SIZE_OFF] = 0;
	header[AU_HDR_ENCODING_OFF] = htoll(AU_ENC_8BIT_ULAW);
	header[AU_HDR_SAMPLE_RATE_OFF] = htoll(DEFAULT_SAMPLE_RATE);
	header[AU_HDR_CHANNELS_OFF] = htoll(1);

	/* Write an au header, ignoring sizes which will be filled in later */
	fseek(f, 0, SEEK_SET);
	if (fwrite(header, 1, AU_HEADER_SIZE, f) != AU_HEADER_SIZE) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	return 0;
}

static int au_open(struct ast_filestream *s)
{
	if (check_header(s->f) < 0)
		return -1;
	return 0;
}

static int au_rewrite(struct ast_filestream *s, const char *comment)
{
	if (write_header(s->f))
		return -1;
	return 0;
}

static struct ast_frame *au_read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ULAW;
	s->fr.mallocd = 0;
	FR_SET_BUF(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data, 1, s->fr.datalen, s->f)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = res;
	s->fr.datalen = res;
	return &s->fr;
}

static int au_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ULAW) {
		ast_log(LOG_WARNING, "Asked to write non-ulaw frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
		return -1;
	}
	update_header(fs->f);
	return 0;
}

static int au_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t min, max, cur;
	unsigned long offset = 0, samples;
	
	samples = sample_offset;
	min = AU_HEADER_SIZE;
	cur = ftello(fs->f);
	fseek(fs->f, 0, SEEK_END);
	max = ftello(fs->f);
	if (whence == SEEK_SET)
		offset = samples + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = samples + cur;
	else if (whence == SEEK_END)
		offset = max - samples;
        if (whence != SEEK_FORCECUR) {
		offset = (offset > max) ? max : offset;
	}
	/* always protect the header space. */
	offset = (offset < min) ? min : offset;
	return fseek(fs->f, offset, SEEK_SET);
}

static int au_trunc(struct ast_filestream *fs)
{
	if (ftruncate(fileno(fs->f), ftello(fs->f)))
		return -1;
	return update_header(fs->f);
}

static off_t au_tell(struct ast_filestream *fs)
{
	off_t offset = ftello(fs->f);
	return offset - AU_HEADER_SIZE;
}

static struct ast_format_lock me = { .usecnt = -1 };

static const struct ast_format au_f = {
	.name = "au",
	.exts = "au",
	.format = AST_FORMAT_ULAW,
	.open = au_open,
	.rewrite = au_rewrite,
	.write = au_write,
	.seek = au_seek,
	.trunc = au_trunc,
	.tell = au_tell,
	.read = au_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,	/* this many shorts */
	.lockp = &me,
};

int load_module()
{
	return ast_format_register(&au_f);
}

int unload_module()
{
	return ast_format_unregister(au_f.name);
}

int usecount()
{
	return me.usecnt;
}

char *description()
{
	return "Sun Microsystems AU format (signed linear)";
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
