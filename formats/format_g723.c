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
 * \brief Old-style G.723 frame/timestamp format.
 * 
 */
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"

#include "../channels/adtranvofr.h"


#define G723_MAX_SIZE 1024

struct ast_filestream {
	/* First entry MUST be reserved for the channel type */
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	struct ast_filestream *next;
	struct ast_frame *fr;	/* Frame representation of buf */
	struct timeval orig;	/* Original frame time */
	char buf[G723_MAX_SIZE + AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
};


AST_MUTEX_DEFINE_STATIC(g723_lock);
static int glistcnt = 0;

static char *name = "g723sf";
static char *desc = "G.723.1 Simple Timestamp File Format";
static char *exts = "g723|g723sf";

static struct ast_filestream *g723_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&g723_lock)) {
			ast_log(LOG_WARNING, "Unable to lock g723 list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		tmp->fr = (struct ast_frame *)tmp->buf;
		tmp->fr->data = tmp->buf + sizeof(struct ast_frame);
		tmp->fr->frametype = AST_FRAME_VOICE;
		tmp->fr->subclass = AST_FORMAT_G723_1;
		/* datalen will vary for each frame */
		tmp->fr->src = name;
		tmp->fr->mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&g723_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *g723_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&g723_lock)) {
			ast_log(LOG_WARNING, "Unable to lock g723 list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		glistcnt++;
		ast_mutex_unlock(&g723_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static struct ast_frame *g723_read(struct ast_filestream *s, int *whennext)
{
	unsigned short size;
	int res;
	int delay;
	/* Read the delay for the next packet, and schedule again if necessary */
	if (fread(&delay, 1, 4, s->f) == 4) 
		delay = ntohl(delay);
	else
		delay = -1;
	if (fread(&size, 1, 2, s->f) != 2) {
		/* Out of data, or the file is no longer valid.  In any case
		   go ahead and stop the stream */
		return NULL;
	}
	/* Looks like we have a frame to read from here */
	size = ntohs(size);
	if (size > G723_MAX_SIZE - sizeof(struct ast_frame)) {
		ast_log(LOG_WARNING, "Size %d is invalid\n", size);
		/* The file is apparently no longer any good, as we
		   shouldn't ever get frames even close to this 
		   size.  */
		return NULL;
	}
	/* Read the data into the buffer */
	s->fr->offset = AST_FRIENDLY_OFFSET;
	s->fr->datalen = size;
	s->fr->data = s->buf + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET;
	if ((res = fread(s->fr->data, 1, size, s->f)) != size) {
		ast_log(LOG_WARNING, "Short read (%d of %d bytes) (%s)!\n", res, size, strerror(errno));
		return NULL;
	}
#if 0
		/* Average out frames <= 50 ms */
		if (delay < 50)
			s->fr->timelen = 30;
		else
			s->fr->timelen = delay;
#else
		s->fr->samples = 240;
#endif
	*whennext = s->fr->samples;
	return s->fr;
}

static void g723_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&g723_lock)) {
		ast_log(LOG_WARNING, "Unable to lock g723 list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&g723_lock);
	ast_update_use_count();
	fclose(s->f);
	free(s);
	s = NULL;
}


static int g723_write(struct ast_filestream *fs, struct ast_frame *f)
{
	u_int32_t delay;
	u_int16_t size;
	int res;
	if (fs->fr) {
		ast_log(LOG_WARNING, "Asked to write on a read stream??\n");
		return -1;
	}
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_G723_1) {
		ast_log(LOG_WARNING, "Asked to write non-g723 frame!\n");
		return -1;
	}
	delay = 0;
	if (f->datalen <= 0) {
		ast_log(LOG_WARNING, "Short frame ignored (%d bytes long?)\n", f->datalen);
		return 0;
	}
	if ((res = fwrite(&delay, 1, 4, fs->f)) != 4) {
		ast_log(LOG_WARNING, "Unable to write delay: res=%d (%s)\n", res, strerror(errno));
		return -1;
	}
	size = htons(f->datalen);
	if ((res = fwrite(&size, 1, 2, fs->f)) != 2) {
		ast_log(LOG_WARNING, "Unable to write size: res=%d (%s)\n", res, strerror(errno));
		return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Unable to write frame: res=%d (%s)\n", res, strerror(errno));
		return -1;
	}	
	return 0;
}

static int g723_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	return -1;
}

static int g723_trunc(struct ast_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
		return -1;
	return 0;
}

static long g723_tell(struct ast_filestream *fs)
{
	return -1;
}

static char *g723_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_G723_1,
								g723_open,
								g723_rewrite,
								g723_write,
								g723_seek,
								g723_trunc,
								g723_tell,
								g723_read,
								g723_close,
								g723_getcomment);
								
								
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
