/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Flat, binary, alaw PCM file format.
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
#include <sys/times.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#ifdef __linux__
#include <endian.h>
#else
#include <machine/endian.h>
#endif

#define BUF_SIZE 160		/* 160 samples */

// #define REALTIME_WRITE

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE];				/* Output Buffer */
#ifdef REALTIME_WRITE
	unsigned long start_time;
#endif
};


AST_MUTEX_DEFINE_STATIC(pcm_lock);
static int glistcnt = 0;

static char *name = "alaw";
static char *desc = "Raw aLaw 8khz PCM Audio support";
static char *exts = "alaw|al";


#if 0
/* Returns time in msec since system boot. */
static unsigned long get_time(void)
{
	struct tms buf;
	clock_t cur;

	cur = times( &buf );
	if( cur < 0 )
	{
		ast_log( LOG_WARNING, "Cannot get current time\n" );
		return 0;
	}
	return cur * 1000 / sysconf( _SC_CLK_TCK );
}
#endif

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
		tmp->fr.subclass = AST_FORMAT_ALAW;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
#ifdef REALTIME_WRITE
		tmp->start_time = get_time();
#endif
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
#ifdef REALTIME_WRITE
		tmp->start_time = get_time();
#endif
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
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ALAW;
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
	*whennext = s->fr.samples;
	return &s->fr;
}

static int pcm_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
#ifdef REALTIME_WRITE
	unsigned long cur_time;
	unsigned long fpos;
	struct stat stat_buf;
#endif

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ALAW) {
		ast_log(LOG_WARNING, "Asked to write non-alaw frame (%d)!\n", f->subclass);
		return -1;
	}

#ifdef REALTIME_WRITE
	cur_time = get_time();
	fpos = ( cur_time - fs->start_time ) * 8;	// 8 bytes per msec
	// Check if we have written to this position yet. If we have, then increment pos by one frame
	// for some degree of protection against receiving packets in the same clock tick.
	fstat( fs->fd, &stat_buf );
	if( stat_buf.st_size > fpos )
	{
		fpos += f->datalen;	// Incrementing with the size of this current frame
	}

	if( stat_buf.st_size < fpos )
	{
		// fill the gap with 0x55 rather than 0.
		char buf[ 512 ];
		unsigned long cur, to_write;

		cur = stat_buf.st_size;
		if( lseek( fs->fd, cur, SEEK_SET ) < 0 )
		{
			ast_log( LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
			return -1;
		}
		memset( buf, 0x55, 512 );
		while( cur < fpos )
		{
			to_write = fpos - cur;
			if( to_write > 512 )
			{
				to_write = 512;
			}
			write( fs->fd, buf, to_write );
			cur += to_write;
		}
	}


	if( lseek( fs->fd, fpos, SEEK_SET ) < 0 )
	{
		ast_log( LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
		return -1;
	}
#endif	// REALTIME_WRITE
	
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
	// Always protect against seeking past begining
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
	return ast_format_register(name, exts, AST_FORMAT_ALAW,
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
