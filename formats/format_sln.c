/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * RAW SLINEAR Format
 * Anthony Minessale (anthmct@yahoo.com)
 * Derived from format_pcm.c in the asterisk distro
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

#define BUF_SIZE 320		/* 320 samples */

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


AST_MUTEX_DEFINE_STATIC(slinear_lock);
static int glistcnt = 0;

static char *name = "sln";
static char *desc = "Raw Signed Linear Audio support (SLN)";
static char *exts = "sln|raw";

static struct ast_filestream *slinear_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&slinear_lock)) {
			ast_log(LOG_WARNING, "Unable to lock slinear list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->buf;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_SLINEAR;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&slinear_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *slinear_rewrite(int fd, char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&slinear_lock)) {
			ast_log(LOG_WARNING, "Unable to lock slinear list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		ast_mutex_unlock(&slinear_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void slinear_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&slinear_lock)) {
		ast_log(LOG_WARNING, "Unable to lock slinear list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&slinear_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct ast_frame *slinear_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int delay;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_SLINEAR;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->buf;
	if ((res = read(s->fd, s->buf, BUF_SIZE)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = res/2;
	s->fr.datalen = res;
	delay = s->fr.samples;
	*whennext = delay;
	return &s->fr;
}

static int slinear_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Asked to write non-slinear frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int slinear_seek(struct ast_filestream *fs, long sample_offset, int whence)
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
	// always protect against seeking past begining.
	offset = (offset < min)?min:offset;
	return lseek(fs->fd, offset, SEEK_SET);
}

static int slinear_trunc(struct ast_filestream *fs)
{
	return ftruncate(fs->fd, lseek(fs->fd,0,SEEK_CUR));
}

static long slinear_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = lseek(fs->fd, 0, SEEK_CUR);
	return offset;
}

static char *slinear_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_SLINEAR,
								slinear_open,
								slinear_rewrite,
								slinear_write,
								slinear_seek,
								slinear_trunc,
								slinear_tell,
								slinear_read,
								slinear_close,
								slinear_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_mutex_lock(&slinear_lock)) {
		ast_log(LOG_WARNING, "Unable to lock slinear list\n");
		return -1;
	}
	res = glistcnt;
	ast_mutex_unlock(&slinear_lock);
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
