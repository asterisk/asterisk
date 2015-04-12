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
 * \brief Save to raw, headerless h264 data.
 * \arg File name extension: h264
 * \ingroup formats
 * \arg See \ref AstVideo
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"
#include "asterisk/format_cache.h"

/* Some Ideas for this code came from makeh264e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */
/*! \todo Check this buf size estimate, it may be totally wrong for large frame video */

#define FRAME_ENDED	0x8000

#define BUF_SIZE	4096	/* Two Real h264 Frames */
struct h264_desc {
	unsigned int lastts;
};

static int h264_open(struct ast_filestream *s)
{
	unsigned int ts;
	if (fread(&ts, 1, sizeof(ts), s->f) < sizeof(ts)) {
		ast_log(LOG_WARNING, "Empty file!\n");
		return -1;
	}
	return 0;
}

static struct ast_frame *h264_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int mark = 0;
	unsigned short len;
	unsigned int ts;
	struct h264_desc *fs = (struct h264_desc *)s->_private;

	/* Send a frame from the file to the appropriate channel */
	if ((res = fread(&len, 1, sizeof(len), s->f)) < 1)
		return NULL;
	len = ntohs(len);
	mark = (len & FRAME_ENDED) ? 1 : 0;
	len &= 0x7fff;
	if (len > BUF_SIZE) {
		ast_log(LOG_WARNING, "Length %d is too long\n", len);
		len = BUF_SIZE;	/* XXX truncate */
	}
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, len);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d of %d) (%s)!\n", res, len, strerror(errno));
		return NULL;
	}
	s->fr.samples = fs->lastts;
	s->fr.datalen = len;
	s->fr.subclass.frame_ending = mark;
	if ((res = fread(&ts, 1, sizeof(ts), s->f)) == sizeof(ts)) {
		fs->lastts = ntohl(ts);
		*whennext = fs->lastts * 4/45;
	} else
		*whennext = 0;
	return &s->fr;
}

static int h264_write(struct ast_filestream *s, struct ast_frame *f)
{
	int res;
	unsigned int ts;
	unsigned short len;
	int mark;

	mark = f->subclass.frame_ending ? FRAME_ENDED : 0;
	ts = htonl(f->samples);
	if ((res = fwrite(&ts, 1, sizeof(ts), s->f)) != sizeof(ts)) {
		ast_log(LOG_WARNING, "Bad write (%d/4): %s\n", res, strerror(errno));
		return -1;
	}
	len = htons(f->datalen | mark);
	if ((res = fwrite(&len, 1, sizeof(len), s->f)) != sizeof(len)) {
		ast_log(LOG_WARNING, "Bad write (%d/2): %s\n", res, strerror(errno));
		return -1;
	}
	if ((res = fwrite(f->data.ptr, 1, f->datalen, s->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
		return -1;
	}
	return 0;
}

static int h264_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	/* No way Jose */
	return -1;
}

static int h264_trunc(struct ast_filestream *fs)
{
	int fd;
	off_t cur;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for h264 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in h264 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	/* Truncate file to current length */
	return ftruncate(fd, cur);
}

static off_t h264_tell(struct ast_filestream *fs)
{
	off_t offset = ftell(fs->f);
	return offset; /* XXX totally bogus, needs fixing */
}

static struct ast_format_def h264_f = {
	.name = "h264",
	.exts = "h264",
	.open = h264_open,
	.write = h264_write,
	.seek = h264_seek,
	.trunc = h264_trunc,
	.tell = h264_tell,
	.read = h264_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct h264_desc),
};

static int load_module(void)
{
	h264_f.format = ast_format_h264;
	if (ast_format_def_register(&h264_f))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(h264_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw H.264 data",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
