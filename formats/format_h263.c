/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Save to raw, headerless h263 data.
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
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#ifdef __linux__
#include <endian.h>
#else
#include <machine/endian.h>
#endif

/* Some Ideas for this code came from makeh263e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	unsigned int lastts;
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char h263[4096];				/* Two Real h263 Frames */
};


AST_MUTEX_DEFINE_STATIC(h263_lock);
static int glistcnt = 0;

static char *name = "h263";
static char *desc = "Raw h263 data";
static char *exts = "h263";

static struct ast_filestream *h263_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	unsigned int ts;
	int res;
	if ((res = read(fd, &ts, sizeof(ts))) < sizeof(ts)) {
		ast_log(LOG_WARNING, "Empty file!\n");
		return NULL;
	}
		
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&h263_lock)) {
			ast_log(LOG_WARNING, "Unable to lock h263 list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->h263;
		tmp->fr.frametype = AST_FRAME_VIDEO;
		tmp->fr.subclass = AST_FORMAT_H263;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&h263_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *h263_rewrite(int fd, char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&h263_lock)) {
			ast_log(LOG_WARNING, "Unable to lock h263 list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		ast_mutex_unlock(&h263_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void h263_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&h263_lock)) {
		ast_log(LOG_WARNING, "Unable to lock h263 list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&h263_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct ast_frame *h263_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int mark=0;
	unsigned short len;
	unsigned int ts;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VIDEO;
	s->fr.subclass = AST_FORMAT_H263;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->h263;
	if ((res = read(s->fd, &len, sizeof(len))) < 1) {
		return NULL;
	}
	len = ntohs(len);
	if (len & 0x8000) {
		mark = 1;
	}
	len &= 0x7fff;
	if (len > sizeof(s->h263)) {
		ast_log(LOG_WARNING, "Length %d is too long\n", len);
	}
	if ((res = read(s->fd, s->h263, len)) != len) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = s->lastts;
	s->fr.datalen = len;
	s->fr.subclass |= mark;
	if ((res = read(s->fd, &ts, sizeof(ts))) == sizeof(ts)) {
		s->lastts = *whennext = ntohl(ts) * 4/45;
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
	if ((res = write(fs->fd, &ts, sizeof(ts))) != sizeof(ts)) {
			ast_log(LOG_WARNING, "Bad write (%d/4): %s\n", res, strerror(errno));
			return -1;
	}
	len = htons(f->datalen | mark);
	if ((res = write(fs->fd, &len, sizeof(len))) != sizeof(len)) {
			ast_log(LOG_WARNING, "Bad write (%d/2): %s\n", res, strerror(errno));
			return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static char *h263_getcomment(struct ast_filestream *s)
{
	return NULL;
}

static int h263_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	/* No way Jose */
	return -1;
}

static int h263_trunc(struct ast_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fs->fd, lseek(fs->fd, 0, SEEK_CUR)) < 0)
		return -1;
	return 0;
}

static long h263_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = lseek(fs->fd, 0, SEEK_CUR);
	return (offset/20)*160;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_H263,
								h263_open,
								h263_rewrite,
								h263_write,
								h263_seek,
								h263_trunc,
								h263_tell,
								h263_read,
								h263_close,
								h263_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_mutex_lock(&h263_lock)) {
		ast_log(LOG_WARNING, "Unable to lock h263 list\n");
		return -1;
	}
	res = glistcnt;
	ast_mutex_unlock(&h263_lock);
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
