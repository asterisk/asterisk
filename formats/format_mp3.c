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

#define MAX_FRAME_SIZE 1441

struct ast_filestream {
	/* First entry MUST be reserved for the channel type */
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_channel *owner;
	struct ast_filestream *next;
	struct ast_frame fr;	/* Frame representation of buf */
	char offset[AST_FRIENDLY_OFFSET];
	unsigned char buf[MAX_FRAME_SIZE * 2];
	int lasttimeout;
	int pos;
	int adj;
	struct timeval last;
};


static struct ast_filestream *glist = NULL;
static pthread_mutex_t mp3_lock = PTHREAD_MUTEX_INITIALIZER;
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
		if (ast_pthread_mutex_lock(&mp3_lock)) {
			ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->lasttimeout = -1;
		tmp->last.tv_usec = 0;
		tmp->last.tv_sec = 0;
		tmp->adj = 0;
		glistcnt++;
		ast_pthread_mutex_unlock(&mp3_lock);
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
		if (ast_pthread_mutex_lock(&mp3_lock)) {
			ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		glistcnt++;
		ast_pthread_mutex_unlock(&mp3_lock);
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
	if (ast_pthread_mutex_lock(&mp3_lock)) {
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
	ast_pthread_mutex_unlock(&mp3_lock);
	ast_update_use_count();
	if (!tmp) 
		ast_log(LOG_WARNING, "Freeing a filestream we don't seem to own\n");
	close(s->fd);
	free(s);
}

static int ast_read_callback(void *data)
{
	/* XXX Don't assume frames are this size XXX */
	u_int32_t delay = -1;
	int res;
	struct ast_filestream *s = data;
	int size;
	int ms=0;
	struct timeval tv;
	if ((res = read(s->fd, s->buf , 4)) != 4) {
		ast_log(LOG_WARNING, "Short read (%d of 4 bytes) (%s)!\n", res, strerror(errno));
		s->owner->streamid = -1;
		return 0;
	}
	if (mp3_badheader(s->buf)) {
		ast_log(LOG_WARNING, "Bad mp3 header\n");
		return 0;
	}
	if ((size = mp3_framelen(s->buf)) < 0) {
		ast_log(LOG_WARNING, "Unable to calculate frame size\n");
		return 0;
	}
	if ((res = read(s->fd, s->buf + 4 , size - 4)) != size - 4) {
		ast_log(LOG_WARNING, "Short read (%d of %d bytes) (%s)!\n", res, size - 4, strerror(errno));
		s->owner->streamid = -1;
		return 0;
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
	if (s->last.tv_sec || s->last.tv_usec) {
		/* To keep things running smoothly, we watch how close we're coming */
		gettimeofday(&tv, NULL);
		ms = ((tv.tv_usec - s->last.tv_usec) / 1000 + (tv.tv_sec - s->last.tv_sec) * 1000);
		/* If we're within 2 milliseconds, that's close enough */
		if ((ms - delay) > 0 )
			s->adj -= (ms - delay);
		s->adj -= 2;
	}
	s->fr.timelen = delay;
#if 0
	ast_log(LOG_DEBUG, "delay is %d, adjusting by %d, as last was %d\n", delay, s->adj, ms);
#endif
	delay += s->adj;
	if (delay < 1)
		delay = 1;
	/* Lastly, process the frame */
	if (ast_write(s->owner, &s->fr)) {
		ast_log(LOG_WARNING, "Failed to write frame\n");
		s->owner->streamid = -1;
		return 0;
	}
	gettimeofday(&s->last, NULL);
	if (s->lasttimeout != delay) {
		s->owner->streamid = ast_sched_add(s->owner->sched, delay, ast_read_callback, s);
		s->lasttimeout = delay;
		return 0;
	}
	return -1;
}

static int mp3_apply(struct ast_channel *c, struct ast_filestream *s)
{
	/* Select our owner for this stream, and get the ball rolling. */
	s->owner = c;
	ast_read_callback(s);
	return 0;
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

static char *mp3_getcomment(struct ast_filestream *s)
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
	if (ast_pthread_mutex_lock(&mp3_lock)) {
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
	ast_pthread_mutex_unlock(&mp3_lock);
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_pthread_mutex_lock(&mp3_lock)) {
		ast_log(LOG_WARNING, "Unable to lock mp3 list\n");
		return -1;
	}
	res = glistcnt;
	ast_pthread_mutex_unlock(&mp3_lock);
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
