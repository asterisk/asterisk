/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Everybody's favorite format: MP3 Files!  Yay!
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/sched.h>
#include <asterisk/module.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include "../channels/adtranvofr.h"


#define MP3_MAX_SIZE 1400

struct ast_filestream {
	/* First entry MUST be reserved for the channel type */
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_channel *owner;
	struct ast_filestream *next;
	struct ast_frame *fr;	/* Frame representation of buf */
	char buf[sizeof(struct ast_frame) + MP3_MAX_SIZE + AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	int pos;
};


static struct ast_filestream *glist = NULL;
static pthread_mutex_t mp3_lock = PTHREAD_MUTEX_INITIALIZER;
static int glistcnt = 0;

static char *name = "mp3";
static char *desc = "MPEG-2 Layer 3 File Format Support";
static char *exts = "mp3|mpeg3";

#if 0
#define MP3_FRAMELEN 417
#else
#define MP3_FRAMELEN 400
#endif
#define MP3_OUTPUTLEN 2304	/* Bytes */
#define MP3_TIMELEN ((MP3_OUTPUTLEN * 1000 / 16000) )

static struct ast_filestream *mp3_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		if (pthread_mutex_lock(&mp3_lock)) {
			ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->fr = (struct ast_frame *)tmp->buf;
		tmp->fr->data = tmp->buf + sizeof(struct ast_frame);
		tmp->fr->frametype = AST_FRAME_VOICE;
		tmp->fr->subclass = AST_FORMAT_MP3;
		/* datalen will vary for each frame */
		tmp->fr->src = name;
		tmp->fr->mallocd = 0;
		glistcnt++;
		pthread_mutex_unlock(&mp3_lock);
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
		if (pthread_mutex_lock(&mp3_lock)) {
			ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->fr = NULL;
		glistcnt++;
		pthread_mutex_unlock(&mp3_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static struct ast_frame *mp3_read(struct ast_filestream *s)
{
	return NULL;
}

static void mp3_close(struct ast_filestream *s)
{
	struct ast_filestream *tmp, *tmpl = NULL;
	if (pthread_mutex_lock(&mp3_lock)) {
		ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
		return;
	}
	tmp = glist;
	while(tmp) {
		if (tmp == s) {
			if (tmpl)
				tmpl->next = tmp->next;
			else
				glist = tmp->next;
			break;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	glistcnt--;
	if (s->owner) {
		s->owner->stream = NULL;
		if (s->owner->streamid > -1)
			ast_sched_del(s->owner->sched, s->owner->streamid);
		s->owner->streamid = -1;
	}
	pthread_mutex_unlock(&mp3_lock);
	ast_update_use_count();
	if (!tmp) 
		ast_log(LOG_WARNING, "Freeing a filestream we don't seem to own\n");
	close(s->fd);
	free(s);
}

static int ast_read_callback(void *data)
{
	/* XXX Don't assume frames are this size XXX */
	u_int16_t size=MP3_FRAMELEN;
	u_int32_t delay = -1;
	int res;
	struct ast_filestream *s = data;
	/* Send a frame from the file to the appropriate channel */
	/* Read the data into the buffer */
	s->fr->offset = AST_FRIENDLY_OFFSET;
	s->fr->datalen = size;
	s->fr->data = s->buf + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET;
	if ((res = read(s->fd, s->fr->data , size)) != size) {
		ast_log(LOG_WARNING, "Short read (%d of %d bytes) (%s)!\n", res, size, strerror(errno));
		s->owner->streamid = -1;
		return 0;
	}
	delay = MP3_TIMELEN;
	s->fr->timelen = delay;
	/* Lastly, process the frame */
	if (ast_write(s->owner, s->fr)) {
		ast_log(LOG_WARNING, "Failed to write frame\n");
		s->owner->streamid = -1;
		return 0;
	}
	return -1;
}

static int mp3_apply(struct ast_channel *c, struct ast_filestream *s)
{
	/* Select our owner for this stream, and get the ball rolling. */
	s->owner = c;
	s->owner->streamid = ast_sched_add(s->owner->sched, MP3_TIMELEN, ast_read_callback, s);
	ast_read_callback(s);
	return 0;
}

static int mp3_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (fs->fr) {
		ast_log(LOG_WARNING, "Asked to write on a read stream??\n");
		return -1;
	}
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

char *mp3_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_MP3,
								mp3_open,
								mp3_rewrite,
								mp3_apply,
								mp3_write,
								mp3_read,
								mp3_close,
								mp3_getcomment);
								
								
}

int unload_module()
{
	struct ast_filestream *tmp, *tmpl;
	if (pthread_mutex_lock(&mp3_lock)) {
		ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
		return -1;
	}
	tmp = glist;
	while(tmp) {
		if (tmp->owner)
			ast_softhangup(tmp->owner);
		tmpl = tmp;
		tmp = tmp->next;
		free(tmpl);
	}
	pthread_mutex_unlock(&mp3_lock);
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (pthread_mutex_lock(&mp3_lock)) {
		ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
		return -1;
	}
	res = glistcnt;
	pthread_mutex_unlock(&mp3_lock);
	return res;
}

char *description()
{
	return desc;
}

