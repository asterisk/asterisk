/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Everybody's favorite format: MP3 Files!  Yay!
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/sched.h>
#include <asterisk/module.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include "../channels/adtranvofr.h"

#define MAX_FRAME_SIZE 1441

struct ast_filestream {
	/* First entry MUST be reserved for the channel type */
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_frame fr;	/* Frame representation of buf */
	char offset[AST_FRIENDLY_OFFSET];
	unsigned char buf[MAX_FRAME_SIZE * 2];
	int pos;
	struct timeval last;
};

#if 0
static struct ast_filestream *glist = NULL;
#endif
static ast_mutex_t mp3_lock = AST_MUTEX_INITIALIZER;
static int glistcnt = 0;

static char *name = "mp3";
static char *desc = "MPEG-1,2 Layer 3 File Format Support";
static char *exts = "mp3|mpeg3";

#include "../codecs/mp3anal.h"

static struct ast_filestream *mp3_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		if (ast_mutex_lock(&mp3_lock)) {
			ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->last.tv_usec = 0;
		tmp->last.tv_sec = 0;
		glistcnt++;
		ast_mutex_unlock(&mp3_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *mp3_rewrite(int fd, char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		if (ast_mutex_lock(&mp3_lock)) {
			ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		ast_mutex_unlock(&mp3_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void mp3_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&mp3_lock)) {
		ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&mp3_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
}

static struct ast_frame *mp3_read(struct ast_filestream *s, int *whennext)
{
	/* XXX Don't assume frames are this size XXX */
	u_int32_t delay = -1;
	int res;
	int size;
	if ((res = read(s->fd, s->buf , 4)) != 4) {
		ast_log(LOG_WARNING, "Short read (%d of 4 bytes) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	if (mp3_badheader(s->buf)) {
		ast_log(LOG_WARNING, "Bad mp3 header\n");
		return NULL;
	}
	if ((size = mp3_framelen(s->buf)) < 0) {
		ast_log(LOG_WARNING, "Unable to calculate frame size\n");
		return NULL;
	}
	if ((res = read(s->fd, s->buf + 4 , size - 4)) != size - 4) {
		ast_log(LOG_WARNING, "Short read (%d of %d bytes) (%s)!\n", res, size - 4, strerror(errno));
		return NULL;
	}
	/* Send a frame from the file to the appropriate channel */
	/* Read the data into the buffer */
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_MP3;
	s->fr.mallocd = 0;
	s->fr.src = name;
	s->fr.datalen = size;
	s->fr.data = s->buf;
	delay = mp3_samples(s->buf) * 1000 / mp3_samplerate(s->buf);
	s->fr.samples = delay * 8;
#if 0
	ast_log(LOG_DEBUG, "delay is %d, adjusting by %d, as last was %d\n", delay, s->adj, ms);
#endif
	delay *= 8;
	if (delay < 1)
		delay = 1;
	*whennext = delay;
	return &s->fr;
}

static int mp3_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_MP3) {
		ast_log(LOG_WARNING, "Asked to write non-mp3 frame!\n");
		return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
		ast_log(LOG_WARNING, "Unable to write frame: res=%d (%s)\n", res, strerror(errno));
		return -1;
	}	
	return 0;
}

static int mp3_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	return -1;
}

static int mp3_trunc(struct ast_filestream *fs)
{
	return -1;
}

static long mp3_tell(struct ast_filestream *fs)
{
	return -1;
}

static char *mp3_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_MP3,
								mp3_open,
								mp3_rewrite,
								mp3_write,
								mp3_seek,
								mp3_trunc,
								mp3_tell,
								mp3_read,
								mp3_close,
								mp3_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_mutex_lock(&mp3_lock)) {
		ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
		return -1;
	}
	res = glistcnt;
	ast_mutex_unlock(&mp3_lock);
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
