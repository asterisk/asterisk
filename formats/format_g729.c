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
 * 
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

/* Some Ideas for this code came from makeg729e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char g729[20];				/* Two Real G729 Frames */
};


AST_MUTEX_DEFINE_STATIC(g729_lock);
static int glistcnt = 0;

static char *name = "g729";
static char *desc = "Raw G729 data";
static char *exts = "g729";

static struct ast_filestream *g729_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&g729_lock)) {
			ast_log(LOG_WARNING, "Unable to lock g729 list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		tmp->fr.data = tmp->g729;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_G729A;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&g729_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *g729_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&g729_lock)) {
			ast_log(LOG_WARNING, "Unable to lock g729 list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		glistcnt++;
		ast_mutex_unlock(&g729_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void g729_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&g729_lock)) {
		ast_log(LOG_WARNING, "Unable to lock g729 list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&g729_lock);
	ast_update_use_count();
	fclose(s->f);
	free(s);
	s = NULL;
}

static struct ast_frame *g729_read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_G729A;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.samples = 160;
	s->fr.datalen = 20;
	s->fr.mallocd = 0;
	s->fr.data = s->g729;
	if ((res = fread(s->g729, 1, 20, s->f)) != 20) {
		if (res && (res != 10))
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples;
	return &s->fr;
}

static int g729_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_G729A) {
		ast_log(LOG_WARNING, "Asked to write non-G729 frame (%d)!\n", f->subclass);
		return -1;
	}
	if (f->datalen % 10) {
		ast_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 10\n", f->datalen);
		return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/10): %s\n", res, strerror(errno));
			return -1;
	}
	return 0;
}

static char *g729_getcomment(struct ast_filestream *s)
{
	return NULL;
}

static int g729_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	long bytes;
	off_t min,cur,max,offset=0;
	min = 0;
	cur = ftell(fs->f);
	fseek(fs->f, 0, SEEK_END);
	max = ftell(fs->f);
	
	bytes = 20 * (sample_offset / 160);
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
	if (fseek(fs->f, offset, SEEK_SET) < 0)
		return -1;
	return 0;
}

static int g729_trunc(struct ast_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
		return -1;
	return 0;
}

static long g729_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = ftell(fs->f);
	return (offset/20)*160;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_G729A,
								g729_open,
								g729_rewrite,
								g729_write,
								g729_seek,
								g729_trunc,
								g729_tell,
								g729_read,
								g729_close,
								g729_getcomment);
								
								
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
