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

/* Some Ideas for this code came from makeh264e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	unsigned int lastts;
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char h264[4096];				/* Two Real h264 Frames */
};


AST_MUTEX_DEFINE_STATIC(h264_lock);
static int glistcnt = 0;

static char *name = "h264";
static char *desc = "Raw h264 data";
static char *exts = "h264";

static struct ast_filestream *h264_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	unsigned int ts;
	int res;
	if ((res = fread(&ts, 1, sizeof(ts), f)) < sizeof(ts)) {
		ast_log(LOG_WARNING, "Empty file!\n");
		return NULL;
	}
		
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&h264_lock)) {
			ast_log(LOG_WARNING, "Unable to lock h264 list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		tmp->fr.data = tmp->h264;
		tmp->fr.frametype = AST_FRAME_VIDEO;
		tmp->fr.subclass = AST_FORMAT_H264;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&h264_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *h264_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&h264_lock)) {
			ast_log(LOG_WARNING, "Unable to lock h264 list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		glistcnt++;
		ast_mutex_unlock(&h264_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void h264_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&h264_lock)) {
		ast_log(LOG_WARNING, "Unable to lock h264 list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&h264_lock);
	ast_update_use_count();
	fclose(s->f);
	free(s);
	s = NULL;
}

static struct ast_frame *h264_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int mark=0;
	unsigned short len;
	unsigned int ts;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VIDEO;
	s->fr.subclass = AST_FORMAT_H264;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->h264;
	if ((res = fread(&len, 1, sizeof(len), s->f)) < 1) {
		return NULL;
	}
	len = ntohs(len);
	if (len & 0x8000) {
		mark = 1;
	}
	len &= 0x7fff;
	if (len > sizeof(s->h264)) {
		ast_log(LOG_WARNING, "Length %d is too long\n", len);
	}
	if ((res = fread(s->h264, 1, len, s->f)) != len) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d of %d) (%s)!\n", res, len, strerror(errno));
		return NULL;
	}
	s->fr.samples = s->lastts;
	s->fr.datalen = len;
	s->fr.subclass |= mark;
	s->fr.delivery.tv_sec = 0;
	s->fr.delivery.tv_usec = 0;
	if ((res = fread(&ts, 1, sizeof(ts), s->f)) == sizeof(ts)) {
		s->lastts = ntohl(ts);
		*whennext = s->lastts * 4/45;
	} else
		*whennext = 0;
	return &s->fr;
}

static int h264_write(struct ast_filestream *fs, struct ast_frame *f)
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
	if (subclass != AST_FORMAT_H264) {
		ast_log(LOG_WARNING, "Asked to write non-h264 frame (%d)!\n", f->subclass);
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

static char *h264_getcomment(struct ast_filestream *s)
{
	return NULL;
}

static int h264_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	/* No way Jose */
	return -1;
}

static int h264_trunc(struct ast_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
		return -1;
	return 0;
}

static long h264_tell(struct ast_filestream *fs)
{
	/* XXX This is totally bogus XXX */
	off_t offset;
	offset = ftell(fs->f);
	return (offset/20)*160;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_H264,
								h264_open,
								h264_rewrite,
								h264_write,
								h264_seek,
								h264_trunc,
								h264_tell,
								h264_read,
								h264_close,
								h264_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	return glistcnt;
}

char *description()
{
	return desc;
}


char *key()
{
	return ASTERISK_GPL_KEY;
}
