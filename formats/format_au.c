/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Work with Sun Microsystems AU format.
 * 
 * Copyright (C) 2005, Andriy Pylypenko
 * Code based on format_wav.c by Mark Spencer
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
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
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"

#define BUF_SIZE		160

#define AU_HEADER_SIZE		24
#define AU_HEADER(var)		u_int32_t var[6]

#define AU_HDR_MAGIC_OFF	0
#define AU_HDR_HDR_SIZE_OFF	1
#define AU_HDR_DATA_SIZE_OFF	2
#define AU_HDR_ENCODING_OFF	3
#define AU_HDR_SAMPLE_RATE_OFF	4
#define AU_HDR_CHANNELS_OFF	5

#define AU_ENC_8BIT_ULAW	1

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; 				/* Descriptor */
	struct ast_channel *owner;
	struct ast_frame fr;			/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;				/* Empty character */
	short buf[BUF_SIZE];
};


AST_MUTEX_DEFINE_STATIC(au_lock);
static int localusecnt = 0;

static char *name = "au";
static char *desc = "Sun Microsystems AU format (signed linear)";
static char *exts = "au";


#define AU_MAGIC 0x2e736e64
#if __BYTE_ORDER == __BIG_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htoll(b)  \
          (((((b)      ) & 0xFF) << 24) | \
	       ((((b) >>  8) & 0xFF) << 16) | \
		   ((((b) >> 16) & 0xFF) <<  8) | \
		   ((((b) >> 24) & 0xFF)      ))
#define htols(b) \
          (((((b)      ) & 0xFF) << 8) | \
		   ((((b) >> 8) & 0xFF)      ))
#define ltohl(b) htoll(b)
#define ltohs(b) htols(b)
#else
#error "Endianess not defined"
#endif
#endif


static int check_header(int fd)
{
	AU_HEADER(header);
	u_int32_t magic;
	u_int32_t hdr_size;
	u_int32_t data_size;
	u_int32_t encoding;
	u_int32_t sample_rate;
	u_int32_t channels;

	if (read(fd, header, AU_HEADER_SIZE) != AU_HEADER_SIZE) {
		ast_log(LOG_WARNING, "Read failed (header)\n");
		return -1;
	}
	magic = ltohl(header[AU_HDR_MAGIC_OFF]);
	if (magic != (u_int32_t) AU_MAGIC) {
		ast_log(LOG_WARNING, "Bad magic: 0x%x\n", magic);
	}
/*	hdr_size = ltohl(header[AU_HDR_HDR_SIZE_OFF]);
	if (hdr_size < AU_HEADER_SIZE)*/
	hdr_size = AU_HEADER_SIZE;
/*	data_size = ltohl(header[AU_HDR_DATA_SIZE_OFF]); */
	encoding = ltohl(header[AU_HDR_ENCODING_OFF]);
	if (encoding != AU_ENC_8BIT_ULAW) {
		ast_log(LOG_WARNING, "Unexpected format: %d. Only 8bit ULAW allowed (%d)\n", encoding, AU_ENC_8BIT_ULAW);
		return -1;
	}
	sample_rate = ltohl(header[AU_HDR_SAMPLE_RATE_OFF]);
	if (sample_rate != 8000) {
		ast_log(LOG_WARNING, "Sample rate can only be 8000 not %d\n", sample_rate);
		return -1;
	}
	channels = ltohl(header[AU_HDR_CHANNELS_OFF]);
	if (channels != 1) {
		ast_log(LOG_WARNING, "Not in mono: channels=%d\n", channels);
		return -1;
	}
	/* Skip to data */
	data_size = lseek(fd, 0, SEEK_END) - hdr_size;
	if (lseek(fd, hdr_size, SEEK_SET) == -1 ) {
		ast_log(LOG_WARNING, "Failed to skip to data: %d\n", hdr_size);
		return -1;
	}
	return data_size;
}

static int update_header(int fd)
{
	off_t cur, end;
	u_int32_t datalen;
	int bytes;

	cur = lseek(fd, 0, SEEK_CUR);
	end = lseek(fd, 0, SEEK_END);
	/* data starts 24 bytes in */
	bytes = end - AU_HEADER_SIZE;
	datalen = htoll(bytes);

	if (cur < 0) {
		ast_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (lseek(fd, AU_HDR_DATA_SIZE_OFF * sizeof(u_int32_t), SEEK_SET) != (AU_HDR_DATA_SIZE_OFF * sizeof(u_int32_t))) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (write(fd, &datalen, sizeof(datalen)) != sizeof(datalen)) {
		ast_log(LOG_WARNING, "Unable to set write file size\n");
		return -1;
	}
	if (lseek(fd, cur, SEEK_SET) != cur) {
		ast_log(LOG_WARNING, "Unable to return to position\n");
		return -1;
	}
	return 0;
}

static int write_header(int fd)
{
	AU_HEADER(header);

	header[AU_HDR_MAGIC_OFF] = htoll((u_int32_t) AU_MAGIC);
	header[AU_HDR_HDR_SIZE_OFF] = htoll(AU_HEADER_SIZE);
	header[AU_HDR_DATA_SIZE_OFF] = 0;
	header[AU_HDR_ENCODING_OFF] = htoll(AU_ENC_8BIT_ULAW);
	header[AU_HDR_SAMPLE_RATE_OFF] = htoll(8000);
	header[AU_HDR_CHANNELS_OFF] = htoll(1);

	/* Write an au header, ignoring sizes which will be filled in later */
	lseek(fd, 0, SEEK_SET);
	if (write(fd, header, AU_HEADER_SIZE) != AU_HEADER_SIZE) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	return 0;
}

static struct ast_filestream *au_open(int fd)
{
	struct ast_filestream *tmp;

	if (!(tmp = malloc(sizeof(struct ast_filestream)))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return NULL;
	}

	memset(tmp, 0, sizeof(struct ast_filestream));
	if (check_header(fd) < 0) {
		free(tmp);
		return NULL;
	}
	if (ast_mutex_lock(&au_lock)) {
		ast_log(LOG_WARNING, "Unable to lock au count\n");
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
	localusecnt++;
	ast_mutex_unlock(&au_lock);
	ast_update_use_count();
	return tmp;
}

static struct ast_filestream *au_rewrite(int fd, const char *comment)
{
	struct ast_filestream *tmp;

	if ((tmp = malloc(sizeof(struct ast_filestream))) == NULL) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return NULL;
	}

	memset(tmp, 0, sizeof(struct ast_filestream));
	if (write_header(fd)) {
		free(tmp);
		return NULL;
	}
	if (ast_mutex_lock(&au_lock)) {
		ast_log(LOG_WARNING, "Unable to lock au count\n");
		free(tmp);
		return NULL;
	}
	tmp->fd = fd;
	localusecnt++;
	ast_mutex_unlock(&au_lock);
	ast_update_use_count();
	return tmp;
}

static void au_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&au_lock)) {
		ast_log(LOG_WARNING, "Unable to lock au count\n");
		return;
	}
	localusecnt--;
	ast_mutex_unlock(&au_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
}

static struct ast_frame *au_read(struct ast_filestream *s, int *whennext)
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

static int au_write(struct ast_filestream *fs, struct ast_frame *f)
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
	update_header(fs->fd);
	return 0;
}

static int au_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	off_t min, max, cur;
	long offset = 0, samples;
	
	samples = sample_offset;
	min = AU_HEADER_SIZE;
	cur = lseek(fs->fd, 0, SEEK_CUR);
	max = lseek(fs->fd, 0, SEEK_END);
	if (whence == SEEK_SET)
		offset = samples + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = samples + cur;
	else if (whence == SEEK_END)
		offset = max - samples;
        if (whence != SEEK_FORCECUR) {
		offset = (offset > max) ? max : offset;
	}
	/* always protect the header space. */
	offset = (offset < min) ? min : offset;
	return lseek(fs->fd, offset, SEEK_SET);
}

static int au_trunc(struct ast_filestream *fs)
{
	if(ftruncate(fs->fd, lseek(fs->fd, 0, SEEK_CUR)))
		return -1;
	return update_header(fs->fd);
}

static long au_tell(struct ast_filestream *fs)
{
	off_t offset;

	offset = lseek(fs->fd, 0, SEEK_CUR);
	return offset - AU_HEADER_SIZE;
}

static char *au_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_ULAW,
				   au_open,
				   au_rewrite,
				   au_write,
				   au_seek,
				   au_trunc,
				   au_tell,
				   au_read,
				   au_close,
				   au_getcomment);
}

int unload_module()
{
	return ast_format_unregister(name);
}

int usecount()
{
	return localusecnt;
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
