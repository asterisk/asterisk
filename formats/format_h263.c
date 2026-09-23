/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Save to raw, headerless h263 data.
 * \arg File name extension: h263
 * \ingroup formats
 * \arg See \ref AstVideo
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"
#include "asterisk/format_cache.h"

/* Some Ideas for this code came from makeh263e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

/* According to:
 * http://lists.mpegif.org/pipermail/mp4-tech/2005-July/005741.html
 * the maximum actual frame size is not 2048, but 8192.  Since the maximum
 * theoretical limit is not much larger (32k = 15bits), we'll go for that
 * size to ensure we don't corrupt frames sent to us (unless they're
 * ridiculously large). */
#define BUF_SIZE	32768	/* Four real h.263 Frames */
#define FRAME_ENDED	0x8000

struct h263_desc {
	unsigned int lastts;
};

static int h263_open(struct ast_filestream *fs)
{
	unsigned int ts;
	struct h263_desc *desc = (struct h263_desc *) fs->_private;

	if (fread(&ts, 1, sizeof(ts), fs->f) != sizeof(ts)) {
		ast_log(LOG_WARNING, "Empty file!\n");
		return -1;
	}

	desc->lastts = ntohl(ts);
	return 0;
}

static struct ast_frame *h263_read(struct ast_filestream *fs, int *whennext)
{
	size_t res;
	unsigned int ts;
	unsigned short mark;
	unsigned short len;
	struct h263_desc *desc = (struct h263_desc *) fs->_private;

	/* Send a frame from the file to the appropriate channel */
	if (fread(&len, 1, sizeof(len), fs->f) != sizeof(len)) {
		return NULL;
	}

	len = ntohs(len);
	mark = len & FRAME_ENDED;
	len &= ~FRAME_ENDED;

	if (len > BUF_SIZE) {
		ast_log(LOG_WARNING, "Length %d is too long\n", len);
		return NULL;
	}

	fs->fr.datalen = len;
	fs->fr.subclass.frame_ending = mark ? 1 : 0;
	AST_FRAME_SET_BUFFER(&fs->fr, fs->buf, AST_FRIENDLY_OFFSET, len);

	if ((res = fread(fs->fr.data.ptr, 1, fs->fr.datalen, fs->f)) != fs->fr.datalen) {
		ast_log(LOG_WARNING, "Short read of %s data (expected %d bytes, read %zu): %s\n",
			ast_format_get_name(fs->fr.subclass.format), fs->fr.datalen, res,
			strerror(errno));
		return NULL;
	}

	fs->fr.ts = desc->lastts;
	ast_set_flag(&fs->fr, AST_FRFLAG_HAS_TIMING_INFO);

	if (fread(&ts, 1, sizeof(ts), fs->f) == sizeof(ts)) {
		ts = ntohl(ts);

		if (ts != desc->lastts) {
			*whennext = ts - desc->lastts;

			/* The timestamp probably won't exactly match the expected sample rate
			 * increment due to scheduling jitter inside the phone and the use of
			 * wall-clock time. If it looks roughly like 30fps adjust it to that. */
			if (*whennext >= 27 && *whennext <= 40) {
				*whennext = 33;
			}

			/* We want to be scheduled closest to just before the actual timeout */
			*whennext -= 1;
			desc->lastts = ts;
		}
	}

	return &fs->fr;
}

static int h263_write(struct ast_filestream *fs, struct ast_frame *fr)
{
	size_t res;
	unsigned int ts;
	unsigned short mark;
	unsigned short len;

	if (fr->len > BUF_SIZE) {
		ast_log(LOG_WARNING, "Length %ld is too long\n", fr->len);
		return -1;
	}

	ts = htonl(fr->ts);

	if ((res = fwrite(&ts, 1, sizeof(ts), fs->f)) != sizeof(ts)) {
		ast_log(LOG_WARNING, "Bad write (%zd/4): %s\n", res, strerror(errno));
		return -1;
	}

	mark = fr->subclass.frame_ending ? FRAME_ENDED : 0;
	len = htons(fr->datalen | mark);

	if ((res = fwrite(&len, 1, sizeof(len), fs->f)) != sizeof(len)) {
		ast_log(LOG_WARNING, "Bad write (%zd/2): %s\n", res, strerror(errno));
		return -1;
	}

	if ((res = fwrite(fr->data.ptr, 1, fr->datalen, fs->f)) != fr->datalen) {
		ast_log(LOG_WARNING, "Bad write (%zd/%d): %s\n", res, fr->datalen, strerror(errno));
		return -1;
	}

	return 0;
}

static int h263_seek(struct ast_filestream *fs, off_t off, int whence)
{
	/* No way Jose */
	return -1;
}

static int h263_trunc(struct ast_filestream *fs)
{
	int fd;
	off_t cur;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for h263 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in h263 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	/* Truncate file to current length */
	return ftruncate(fd, cur);
}

static off_t h263_tell(struct ast_filestream *fs)
{
	off_t off = ftello(fs->f);

	return off; /* XXX totally bogus, needs fixing */
}

static struct ast_format_def h263_f = {
	.name = "h263",
	.exts = "h263",
	.open = h263_open,
	.write = h263_write,
	.seek = h263_seek,
	.trunc = h263_trunc,
	.tell = h263_tell,
	.read = h263_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct h263_desc),
};

static int load_module(void)
{
	h263_f.format = ast_format_h263;

	if (ast_format_def_register(&h263_f)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(h263_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw H.263 data",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
