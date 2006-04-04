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
 * \brief Save to raw, headerless h263 data.
 * \arg File name extension: h263
 * \ingroup formats
 */
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
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

/* Some Ideas for this code came from makeh263e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define	BUF_SIZE	4096	/* Two Real h263 Frames */

struct h263_desc {
	unsigned int lastts;
};


static int h263_open(struct ast_filestream *s)
{
	unsigned int ts;
	int res;

	if ((res = fread(&ts, 1, sizeof(ts), s->f)) < sizeof(ts)) {
		ast_log(LOG_WARNING, "Empty file!\n");
		return -1;
	}
	return 0;
}

static struct ast_frame *h263_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int mark;
	unsigned short len;
	unsigned int ts;
	struct h263_desc *fs = (struct h263_desc *)s->private;

	/* Send a frame from the file to the appropriate channel */
	if ((res = fread(&len, 1, sizeof(len), s->f)) < 1)
		return NULL;
	len = ntohs(len);
	mark = (len & 0x8000) ? 1 : 0;
	len &= 0x7fff;
	if (len > BUF_SIZE) {
		ast_log(LOG_WARNING, "Length %d is too long\n", len);
		len = BUF_SIZE;	/* XXX truncate ? */
	}
	s->fr.frametype = AST_FRAME_VIDEO;
	s->fr.subclass = AST_FORMAT_H263;
	s->fr.mallocd = 0;
	FR_SET_BUF(&s->fr, s->buf, AST_FRIENDLY_OFFSET, len);
	if ((res = fread(s->fr.data, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = fs->lastts;	/* XXX what ? */
	s->fr.datalen = len;
	s->fr.subclass |= mark;
	s->fr.delivery.tv_sec = 0;
	s->fr.delivery.tv_usec = 0;
	if ((res = fread(&ts, 1, sizeof(ts), s->f)) == sizeof(ts)) {
		fs->lastts = ntohl(ts);
		*whennext = fs->lastts * 4/45;
	} else
		*whennext = 0;
	return &s->fr;
}

static int h263_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	unsigned int ts;
	unsigned short len;
	int subclass;
	int mark=0;
	if (f->frametype != AST_FRAME_VIDEO) {
		ast_log(LOG_WARNING, "Asked to write non-video frame!\n");
		return -1;
	}
	subclass = f->subclass;
	if (subclass & 0x1)
		mark=0x8000;
	subclass &= ~0x1;
	if (subclass != AST_FORMAT_H263) {
		ast_log(LOG_WARNING, "Asked to write non-h263 frame (%d)!\n", f->subclass);
		return -1;
	}
	ts = htonl(f->samples);
	if ((res = fwrite(&ts, 1, sizeof(ts), fs->f)) != sizeof(ts)) {
			ast_log(LOG_WARNING, "Bad write (%d/4): %s\n", res, strerror(errno));
			return -1;
	}
	len = htons(f->datalen | mark);
	if ((res = fwrite(&len, 1, sizeof(len), fs->f)) != sizeof(len)) {
			ast_log(LOG_WARNING, "Bad write (%d/2): %s\n", res, strerror(errno));
			return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int h263_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	/* No way Jose */
	return -1;
}

static int h263_trunc(struct ast_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fileno(fs->f), ftello(fs->f)) < 0)
		return -1;
	return 0;
}

static off_t h263_tell(struct ast_filestream *fs)
{
	off_t offset = ftello(fs->f);
	return offset;	/* XXX totally bogus, needs fixing */
}

static struct ast_format_lock me = { .usecnt = -1 };

static const struct ast_format h263_f = {
	.name = "h263",
	.exts = "h264",
	.format = AST_FORMAT_H263,
	.open = h263_open,
	.write = h263_write,
	.seek = h263_seek,
	.trunc = h263_trunc,
	.tell = h263_tell,
	.read = h263_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct h263_desc),
	.lockp = &me,
};

int load_module()
{
	return ast_format_register(&h263_f);
}

int unload_module()
{
	return ast_format_unregister(h263_f.name);
}	

int usecount()
{
	return me.usecnt;
}

char *description()
{
	return "Raw h263 data";
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
