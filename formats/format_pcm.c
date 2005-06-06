/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Flat, binary, ulaw PCM file format.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
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

#define BUF_SIZE 160		/* 160 samples */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_channel *owner;
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE];				/* Output Buffer */
	struct timeval last;
};


AST_MUTEX_DEFINE_STATIC(pcm_lock);
static int glistcnt = 0;

static char *name = "pcm";
static char *desc = "Raw uLaw 8khz Audio support (PCM)";
static char *exts = "pcm|ulaw|ul|mu";

static struct ast_filestream *pcm_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&pcm_lock)) {
			ast_log(LOG_WARNING, "Unable to lock pcm list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->buf;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_ULAW;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&pcm_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *pcm_rewrite(int fd, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&pcm_lock)) {
			ast_log(LOG_WARNING, "Unable to lock pcm list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		ast_mutex_unlock(&pcm_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void pcm_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&pcm_lock)) {
		ast_log(LOG_WARNING, "Unable to lock pcm list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&pcm_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct ast_frame *pcm_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int delay;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ULAW;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->buf;
	if ((res = read(s->fd, s->buf, BUF_SIZE)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = res;
	s->fr.datalen = res;
	delay = s->fr.samples;
	*whennext = delay;
	return &s->fr;
}

static int pcm_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ULAW) {
		ast_log(LOG_WARNING, "Asked to write non-ulaw frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int pcm_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	off_t offset=0,min,cur,max;

	min = 0;
	cur = lseek(fs->fd, 0, SEEK_CUR);
	max = lseek(fs->fd, 0, SEEK_END);
	if (whence == SEEK_SET)
		offset = sample_offset;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = sample_offset + cur;
	else if (whence == SEEK_END)
		offset = max - sample_offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* always protect against seeking past begining. */
	offset = (offset < min)?min:offset;
	return lseek(fs->fd, offset, SEEK_SET);
}

static int pcm_trunc(struct ast_filestream *fs)
{
	return ftruncate(fs->fd, lseek(fs->fd,0,SEEK_CUR));
}

static long pcm_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = lseek(fs->fd, 0, SEEK_CUR);
	return offset;
}

static char *pcm_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_ULAW,
								pcm_open,
								pcm_rewrite,
								pcm_write,
								pcm_seek,
								pcm_trunc,
								pcm_tell,
								pcm_read,
								pcm_close,
								pcm_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_mutex_lock(&pcm_lock)) {
		ast_log(LOG_WARNING, "Unable to lock pcm list\n");
		return -1;
	}
	res = glistcnt;
	ast_mutex_unlock(&pcm_lock);
	return res;
}

char *description()
{
	return desc;
}


char *key()
{
	return ASTERISK_GPL_KEY;
}
