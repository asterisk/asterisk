/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Lorenzo Miniero
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
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
 * \brief Save to raw, headerless VP8 data.
 *
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 *
 * \note Basically a "clone" of the H.264 passthrough format
 *
 * \arg File name extension: VP8
 * \ingroup formats
 * \arg See \ref AstVideo
 */

/*** MODULEINFO
	 <support_level>core</support_level>
***/

#include "asterisk.h"

#if defined(ASTERISK_REGISTER_FILE)
ASTERISK_REGISTER_FILE()
#elif defined(ASTERISK_FILE_VERSION)
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")
#endif

#include <netinet/in.h>             /* for htonl, htons, ntohl, ntohs */
#include <sys/time.h>               /* for timeval = ast_filestream->ast_frame.delivery */

#include "asterisk/format_cache.h"  /* for ast_format_vp8 */
#include "asterisk/frame.h"         /* for ast_frame, AST_FRIENDLY_OFFSET */
#include "asterisk/logger.h"        /* for ast_log, LOG_WARNING */
#include "asterisk/mod_format.h"    /* for ast_filestream, ast_format_def */
#include "asterisk/module.h"

/* VP8 passthrough */
#define FRAME_ENDED	0x8000

#define BUF_SIZE	4096
struct vp8_desc {
	unsigned int lastts;
};

static int vp8_open(struct ast_filestream *s)
{
	unsigned int ts;

	if (fread(&ts, 1, sizeof(ts), s->f) < sizeof(ts)) {
		ast_log(LOG_WARNING, "Empty file!\n");
		return -1;
	}

	return 0;
}

static struct ast_frame *vp8_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int mark = 0;
	unsigned short len;
	unsigned int ts;
	struct vp8_desc *fs = (struct vp8_desc *) s->_private;

	/* Send a frame from the file to the appropriate channel */
	if ((res = fread(&len, 1, sizeof(len), s->f)) < 1) {
		return NULL;
	}

	len = ntohs(len);
	mark = (len & FRAME_ENDED) ? 1 : 0;
	len &= 0x7fff;
	if (len > BUF_SIZE) {
		ast_log(LOG_WARNING, "Length %d is too long\n", len);
		len = BUF_SIZE;	/* XXX truncate */
	}
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, len)
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res) {
			ast_log(LOG_WARNING, "Short read (%d of %d) (%s)!\n", res, len, strerror(errno));
		}
		return NULL;
	}
	s->fr.samples = fs->lastts;
	s->fr.datalen = len;
	s->fr.subclass.frame_ending = mark;
	s->fr.delivery.tv_sec = 0;
	s->fr.delivery.tv_usec = 0;
	if ((res = fread(&ts, 1, sizeof(ts), s->f)) == sizeof(ts)) {
		fs->lastts = ntohl(ts);
		*whennext = fs->lastts * 4 / 45;
	} else {
		*whennext = 0;
	}
	return &s->fr;
}

static int vp8_write(struct ast_filestream *s, struct ast_frame *f)
{
	int res;
	unsigned int ts;
	unsigned short len;
	int mark;

	if (f->frametype != AST_FRAME_VIDEO) {
		ast_log(LOG_WARNING, "Asked to write non-video frame!\n");
		return -1;
	}

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

static int vp8_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	/* No way Jose */
	return -1;
}

static int vp8_trunc(struct ast_filestream *fs)
{
	int fd;
	off_t cur;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(LOG_WARNING, "Unable to determine file descriptor for VP8 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if ((cur = ftello(fs->f)) < 0) {
		ast_log(LOG_WARNING, "Unable to determine current position in VP8 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	/* Truncate file to current length */
	return ftruncate(fd, cur);
}

static off_t vp8_tell(struct ast_filestream *fs)
{
	off_t offset = ftell(fs->f);
	return offset; /* XXX totally bogus, needs fixing */
}

static struct ast_format_def vp8_f = {
	.name = "VP8",
	.exts = "vp8",
	.open = vp8_open,
	.write = vp8_write,
	.seek = vp8_seek,
	.trunc = vp8_trunc,
	.tell = vp8_tell,
	.read = vp8_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct vp8_desc),
};

static int load_module(void)
{
	vp8_f.format = ast_format_vp8;
	if (ast_format_def_register(&vp8_f)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(vp8_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw VP8 data",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
	);
