/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Flat, binary, ADPCM vox file format.
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
#include <asterisk/endian.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define BUF_SIZE 80		/* 160 samples */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE];	/* Output Buffer */
	int lasttimeout;
	struct timeval last;
	short signal;						/* Signal level (file side) */
	short ssindex;						/* Signal ssindex (file side) */
	unsigned char zero_count;				/* counter of consecutive zero samples */
	unsigned char next_flag;
};


AST_MUTEX_DEFINE_STATIC(vox_lock);
static int glistcnt = 0;

static char *name = "vox";
static char *desc = "Dialogic VOX (ADPCM) File Format";
static char *exts = "vox";

static struct ast_filestream *vox_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&vox_lock)) {
			ast_log(LOG_WARNING, "Unable to lock vox list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->buf;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_ADPCM;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->lasttimeout = -1;
		glistcnt++;
		ast_mutex_unlock(&vox_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *vox_rewrite(int fd, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&vox_lock)) {
			ast_log(LOG_WARNING, "Unable to lock vox list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		ast_mutex_unlock(&vox_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void vox_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&vox_lock)) {
		ast_log(LOG_WARNING, "Unable to lock vox list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&vox_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct ast_frame *vox_read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ADPCM;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->buf;
	if ((res = read(s->fd, s->buf, BUF_SIZE)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = res * 2;
	s->fr.datalen = res;
	*whennext = s->fr.samples;
	return &s->fr;
}

static int vox_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ADPCM) {
		ast_log(LOG_WARNING, "Asked to write non-ADPCM frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static char *vox_getcomment(struct ast_filestream *s)
{
	return NULL;
}

static int vox_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
     off_t offset=0,min,cur,max,distance;
	
     min = 0;
     cur = lseek(fs->fd, 0, SEEK_CUR);
     max = lseek(fs->fd, 0, SEEK_END);
     /* have to fudge to frame here, so not fully to sample */
     distance = sample_offset/2;
     if(whence == SEEK_SET)
	  offset = distance;
     else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
	  offset = distance + cur;
     else if(whence == SEEK_END)
	  offset = max - distance;
     if (whence != SEEK_FORCECUR) {
	  offset = (offset > max)?max:offset;
	  offset = (offset < min)?min:offset;
     }
     return lseek(fs->fd, offset, SEEK_SET);
}

static int vox_trunc(struct ast_filestream *fs)
{
     return ftruncate(fs->fd, lseek(fs->fd,0,SEEK_CUR));
}

static long vox_tell(struct ast_filestream *fs)
{
     off_t offset;
     offset = lseek(fs->fd, 0, SEEK_CUR);
     return offset; 
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_ADPCM,
								vox_open,
								vox_rewrite,
								vox_write,
								vox_seek,
								vox_trunc,
								vox_tell,
								vox_read,
								vox_close,
								vox_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_mutex_lock(&vox_lock)) {
		ast_log(LOG_WARNING, "Unable to lock vox list\n");
		return -1;
	}
	res = glistcnt;
	ast_mutex_unlock(&vox_lock);
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
