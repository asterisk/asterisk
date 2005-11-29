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
 
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/sched.h>
#include <asterisk/module.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <endian.h>

#define BUF_SIZE 160		/* 160 samples */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_channel *owner;
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE];				/* Output Buffer */
	int lasttimeout;
	struct timeval last;
	int adj;
	struct ast_filestream *next;
};


static struct ast_filestream *glist = NULL;
static pthread_mutex_t pcm_lock = AST_MUTEX_INITIALIZER;
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
		if (pthread_mutex_lock(&pcm_lock)) {
			ast_log(LOG_WARNING, "Unable to lock pcm list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->fr.data = tmp->buf;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_ULAW;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->lasttimeout = -1;
		glistcnt++;
		pthread_mutex_unlock(&pcm_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *pcm_rewrite(int fd, char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (pthread_mutex_lock(&pcm_lock)) {
			ast_log(LOG_WARNING, "Unable to lock pcm list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->lasttimeout = -1;
		glistcnt++;
		pthread_mutex_unlock(&pcm_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static struct ast_frame *pcm_read(struct ast_filestream *s)
{
	return NULL;
}

static void pcm_close(struct ast_filestream *s)
{
	struct ast_filestream *tmp, *tmpl = NULL;
	if (pthread_mutex_lock(&pcm_lock)) {
		ast_log(LOG_WARNING, "Unable to lock pcm list\n");
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
	pthread_mutex_unlock(&pcm_lock);
	ast_update_use_count();
	if (!tmp) 
		ast_log(LOG_WARNING, "Freeing a filestream we don't seem to own\n");
	close(s->fd);
	free(s);
	s = NULL;
}

static int ast_read_callback(void *data)
{
	int retval = 0;
	int res;
	int delay;
	struct ast_filestream *s = data;
	struct timeval tv;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ULAW;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->buf;
	if ((res = read(s->fd, s->buf, BUF_SIZE)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		s->owner->streamid = -1;
		return 0;
	}
	s->fr.samples = res;
	s->fr.datalen = res;
	delay = s->fr.samples/8;
	/* Lastly, process the frame */
	if (ast_write(s->owner, &s->fr)) {
		ast_log(LOG_WARNING, "Failed to write frame\n");
		s->owner->streamid = -1;
		return 0;
	}
	if (s->last.tv_usec || s->last.tv_usec) {
		int ms;
		gettimeofday(&tv, NULL);
		ms = 1000 * (tv.tv_sec - s->last.tv_sec) + 
			(tv.tv_usec - s->last.tv_usec) / 1000;
		s->last.tv_sec = tv.tv_sec;
		s->last.tv_usec = tv.tv_usec;
		if ((ms - delay) * (ms - delay) > 4) {
			/* Compensate if we're more than 2 ms off */
			s->adj -= (ms - delay);
		}
#if 0
		fprintf(stdout, "Delay is %d, adjustment is %d, last was %d\n", delay, s->adj, ms);
#endif
		delay += s->adj;
		if (delay < 1)
			delay = 1;
	} else
		gettimeofday(&s->last, NULL);
	if (s->lasttimeout != delay) {
		/* We'll install the next timeout now. */
		s->owner->streamid = ast_sched_add(s->owner->sched,
				delay, ast_read_callback, s); 
		s->lasttimeout = delay;
	} else {
		/* Just come back again at the same time */
		retval = -1;
	}
	return retval;
}

static int pcm_apply(struct ast_channel *c, struct ast_filestream *s)
{
	/* Select our owner for this stream, and get the ball rolling. */
	s->owner = c;
	return 0;
}

static int pcm_play(struct ast_filestream *s)
{
	ast_read_callback(s);
	return 0;
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
	off_t offset,min,cur,max;

	min = 0;
	cur = lseek(fs->fd, 0, SEEK_CUR);
	max = lseek(fs->fd, 0, SEEK_END);
	if(whence == SEEK_SET)
		offset = sample_offset;
	if(whence == SEEK_CUR)
		offset = sample_offset + cur;
	if(whence == SEEK_END)
		offset = max - sample_offset;
	offset = (offset > max)?max:offset;
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
								pcm_apply,
								pcm_play,
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
	struct ast_filestream *tmp, *tmpl;
	if (pthread_mutex_lock(&pcm_lock)) {
		ast_log(LOG_WARNING, "Unable to lock pcm list\n");
		return -1;
	}
	tmp = glist;
	while(tmp) {
		if (tmp->owner)
			ast_softhangup(tmp->owner, AST_SOFTHANGUP_APPUNLOAD);
		tmpl = tmp;
		tmp = tmp->next;
		free(tmpl);
	}
	pthread_mutex_unlock(&pcm_lock);
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (pthread_mutex_lock(&pcm_lock)) {
		ast_log(LOG_WARNING, "Unable to lock pcm list\n");
		return -1;
	}
	res = glistcnt;
	pthread_mutex_unlock(&pcm_lock);
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
