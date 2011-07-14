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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

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
	int res;
	struct g726_desc *fs = (struct g726_desc *)s->_private;

	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&s->fr.subclass.format, AST_FORMAT_G726, 0);
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, frame_size[fs->rate]);
	s->fr.samples = 8 * FRAME_TIME;
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples;
	return &s->fr;
}

static int g726_write(struct ast_filestream *s, struct ast_frame *f)
{
	int res;
	struct g726_desc *fs = (struct g726_desc *)s->_private;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.format.id != AST_FORMAT_G726) {
		ast_log(LOG_WARNING, "Asked to write non-G726 frame (%s)!\n", 
						ast_getformatname(&f->subclass.format));
		return -1;
	}
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
	return -1;
}

static int g726_trunc(struct ast_filestream *fs)
{
	return -1;
}

static off_t g726_tell(struct ast_filestream *fs)
{
	return -1;
}

static struct ast_format_def f[] = {
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

static int load_module(void)
{
	int i;

	for (i = 0; f[i].desc_size ; i++) {
		ast_format_set(&f[i].format, AST_FORMAT_G726, 0);
		if (ast_format_def_register(&f[i])) {	/* errors are fatal */
			ast_log(LOG_WARNING, "Failed to register format %s.\n", f[i].name);
			return AST_MODULE_LOAD_FAILURE;
		}
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int i;

	for (i = 0; f[i].desc_size ; i++) {
		if (ast_format_def_unregister(f[i].name))
			ast_log(LOG_WARNING, "Failed to unregister format %s.\n", f[i].name);
	}
	return(0);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw G.726 (16/24/32/40kbps) data",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
