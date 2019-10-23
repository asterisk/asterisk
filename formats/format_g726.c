/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, inAccess Networks
 *
 * Michael Manousos <manousos@inaccessnetworks.com>
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

/*!\file
 *
 * \brief Headerless G.726 (16/24/32/40kbps) data format for Asterisk.
 *
 * File name extensions:
 * \arg 40 kbps: g726-40
 * \arg 32 kbps: g726-32
 * \arg 24 kbps: g726-24
 * \arg 16 kbps: g726-16
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

#define	RATE_40		0
#define	RATE_32		1
#define	RATE_24		2
#define	RATE_16		3

/* We can only read/write chunks of FRAME_TIME ms G.726 data */
#define	FRAME_TIME	10	/* 10 ms size */

#define	BUF_SIZE	(5*FRAME_TIME)	/* max frame size in bytes ? */
/* Frame sizes in bytes */
static int frame_size[4] = {
		FRAME_TIME * 5,
		FRAME_TIME * 4,
		FRAME_TIME * 3,
		FRAME_TIME * 2
};

struct g726_desc  {
	int rate;	/* RATE_* defines */
};

/*
 * Rate dependant format functions (open, rewrite)
 */
static int g726_open(struct ast_filestream *tmp, int rate)
{
	struct g726_desc *s = (struct g726_desc *)tmp->_private;
	s->rate = rate;
	ast_debug(1, "Created filestream G.726-%dk.\n", 40 - s->rate * 8);
	return 0;
}

static int g726_40_open(struct ast_filestream *s)
{
	return g726_open(s, RATE_40);
}

static int g726_32_open(struct ast_filestream *s)
{
	return g726_open(s, RATE_32);
}

static int g726_24_open(struct ast_filestream *s)
{
	return g726_open(s, RATE_24);
}

static int g726_16_open(struct ast_filestream *s)
{
	return g726_open(s, RATE_16);
}

static int g726_40_rewrite(struct ast_filestream *s, const char *comment)
{
	return g726_open(s, RATE_40);
}

static int g726_32_rewrite(struct ast_filestream *s, const char *comment)
{
	return g726_open(s, RATE_32);
}

static int g726_24_rewrite(struct ast_filestream *s, const char *comment)
{
	return g726_open(s, RATE_24);
}

static int g726_16_rewrite(struct ast_filestream *s, const char *comment)
{
	return g726_open(s, RATE_16);
}

/*
 * Rate independent format functions (read, write)
 */

static struct ast_frame *g726_read(struct ast_filestream *s, int *whennext)
{
	size_t res;
	struct g726_desc *fs = (struct g726_desc *)s->_private;

	/* Send a frame from the file to the appropriate channel */
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, frame_size[fs->rate]);
	s->fr.samples = 8 * FRAME_TIME;
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res) {
			ast_log(LOG_WARNING, "Short read of %s data (expected %d bytes, read %zu): %s\n",
					ast_format_get_name(s->fr.subclass.format), s->fr.datalen, res,
					strerror(errno));
		}
		return NULL;
	}
	*whennext = s->fr.samples;
	return &s->fr;
}

static int g726_write(struct ast_filestream *s, struct ast_frame *f)
{
	int res;
	struct g726_desc *fs = (struct g726_desc *)s->_private;

	if (f->datalen % frame_size[fs->rate]) {
		ast_log(LOG_WARNING, "Invalid data length %d, should be multiple of %d\n",
						f->datalen, frame_size[fs->rate]);
		return -1;
	}
	if ((res = fwrite(f->data.ptr, 1, f->datalen, s->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n",
				res, frame_size[fs->rate], strerror(errno));
			return -1;
	}
	return 0;
}

static int g726_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t offset = 0, min = 0, cur, max, distance;

	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in g726 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if (fseeko(fs->f, 0, SEEK_END) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to seek to end of g726 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if ((max = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine max position in g726 filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	/* have to fudge to frame here, so not fully to sample */
	distance = sample_offset / 2;
	if (whence == SEEK_SET) {
		offset = distance;
	} else if (whence == SEEK_CUR || whence == SEEK_FORCECUR) {
		offset = distance + cur;
	} else if (whence == SEEK_END) {
		offset = max - distance;
	}

	if (whence != SEEK_FORCECUR) {
		offset = offset > max ? max : offset;
		offset = offset < min ? min : offset;
	}
	return fseeko(fs->f, offset, SEEK_SET);
}

static int g726_trunc(struct ast_filestream *fs)
{
	return -1;
}

static off_t g726_tell(struct ast_filestream *fs)
{
	return ftello(fs->f) << 1;
}

static struct ast_format_def f_def[] = {
	{
		.name = "g726-40",
		.exts = "g726-40",
		.open = g726_40_open,
		.rewrite = g726_40_rewrite,
		.write = g726_write,
		.seek = g726_seek,
		.trunc = g726_trunc,
		.tell = g726_tell,
		.read = g726_read,
		.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
		.desc_size = sizeof(struct g726_desc),
	},
	{
		.name = "g726-32",
		.exts = "g726-32",
		.open = g726_32_open,
		.rewrite = g726_32_rewrite,
		.write = g726_write,
		.seek = g726_seek,
		.trunc = g726_trunc,
		.tell = g726_tell,
		.read = g726_read,
		.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
		.desc_size = sizeof(struct g726_desc),
	},
	{
		.name = "g726-24",
		.exts = "g726-24",
		.open = g726_24_open,
		.rewrite = g726_24_rewrite,
		.write = g726_write,
		.seek = g726_seek,
		.trunc = g726_trunc,
		.tell = g726_tell,
		.read = g726_read,
		.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
		.desc_size = sizeof(struct g726_desc),
	},
	{
		.name = "g726-16",
		.exts = "g726-16",
		.open = g726_16_open,
		.rewrite = g726_16_rewrite,
		.write = g726_write,
		.seek = g726_seek,
		.trunc = g726_trunc,
		.tell = g726_tell,
		.read = g726_read,
		.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
		.desc_size = sizeof(struct g726_desc),
	},
	{	.desc_size = 0 }	/* terminator */
};

static int unload_module(void)
{
	int i;

	for (i = 0; f_def[i].desc_size ; i++) {
		if (ast_format_def_unregister(f_def[i].name))
			ast_log(LOG_WARNING, "Failed to unregister format %s.\n", f_def[i].name);
	}
	return(0);
}

static int load_module(void)
{
	int i;

	for (i = 0; f_def[i].desc_size ; i++) {
		f_def[i].format = ast_format_g726;
		if (ast_format_def_register(&f_def[i])) {	/* errors are fatal */
			ast_log(LOG_WARNING, "Failed to register format %s.\n", f_def[i].name);
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw G.726 (16/24/32/40kbps) data",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
