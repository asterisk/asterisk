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
 * \brief Save to raw, headerless G729 data.
 * \note This is not an encoder/decoder. The codec for g729 is only
 * available with a commercial license from Digium, due to patent
 * restrictions. Check http://www.digium.com for information.
 * \arg Extensions: g729
 * \ingroup formats
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"
#include "asterisk/format_cache.h"

/* Some Ideas for this code came from makeg729e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define	BUF_SIZE	20	/* two G729 frames */
#define	G729A_SAMPLES	160

static struct ast_frame *g729_read(struct ast_filestream *s, int *whennext)
{
	size_t res;

	/* Send a frame from the file to the appropriate channel */
	s->fr.samples = G729A_SAMPLES;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res && res != 10) /* XXX what for ? */ {
			ast_log(LOG_WARNING, "Short read of %s data (expected %d bytes, read %zu): %s\n",
					ast_format_get_name(s->fr.subclass.format), s->fr.datalen, res,
					strerror(errno));
		}
		return NULL;
	}
	*whennext = s->fr.samples;
	return &s->fr;
}

static int g729_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;

	if (f->datalen % 10) {
		ast_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 10\n", f->datalen);
		return -1;
	}
	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/10): %s\n", res, strerror(errno));
			return -1;
	}
	return 0;
}

static int g729_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	long bytes;
	off_t min,cur,max,offset=0;
	min = 0;
	cur = ftello(fs->f);
	fseeko(fs->f, 0, SEEK_END);
	max = ftello(fs->f);

	bytes = BUF_SIZE * (sample_offset / G729A_SAMPLES);
	if (whence == SEEK_SET)
		offset = bytes;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = cur + bytes;
	else if (whence == SEEK_END)
		offset = max - bytes;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* protect against seeking beyond begining. */
	offset = (offset < min)?min:offset;
	if (fseeko(fs->f, offset, SEEK_SET) < 0)
		return -1;
	return 0;
}

static int g729_trunc(struct ast_filestream *fs)
{
	int fd;
	off_t cur;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for g729 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in g729 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	/* Truncate file to current length */
	return ftruncate(fd, cur);
}

static off_t g729_tell(struct ast_filestream *fs)
{
	off_t offset = ftello(fs->f);
	return (offset/BUF_SIZE)*G729A_SAMPLES;
}

static struct ast_format_def g729_f = {
	.name = "g729",
	.exts = "g729",
	.write = g729_write,
	.seek = g729_seek,
	.trunc = g729_trunc,
	.tell = g729_tell,
	.read = g729_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	g729_f.format = ast_format_g729;
	if (ast_format_def_register(&g729_f))
		return AST_MODULE_LOAD_DECLINE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(g729_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw G.729 data",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
