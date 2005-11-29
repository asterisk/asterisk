/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Save GSM in the proprietary Microsoft format.
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
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <endian.h>
#include "msgsm.h"

/* Some Ideas for this code came from makewave.c by Jeffery Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	int bytes;
	struct ast_channel *owner;
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char gsm[66];				/* Two Real GSM Frames */
	int foffset;
	int secondhalf;						/* Are we on the second half */
	int lasttimeout;
	struct timeval last;
	int adj;
	struct ast_filestream *next;
};


static struct ast_filestream *glist = NULL;
static pthread_mutex_t wav_lock = PTHREAD_MUTEX_INITIALIZER;
static int glistcnt = 0;

static char *name = "wav49";
static char *desc = "Microsoft WAV format (Proprietary GSM)";
static char *exts = "WAV";

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __BIG_ENDIAN
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
	int type, size, formtype;
	int fmt, hsize, fact;
	short format, chans;
	int freq;
	int data;
	if (read(fd, &type, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (type)\n");
		return -1;
	}
	if (read(fd, &size, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (size)\n");
		return -1;
	}
	size = ltohl(size);
	if (read(fd, &formtype, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (formtype)\n");
		return -1;
	}
	if (memcmp(&type, "RIFF", 4)) {
		ast_log(LOG_WARNING, "Does not begin with RIFF\n");
		return -1;
	}
	if (memcmp(&formtype, "WAVE", 4)) {
		ast_log(LOG_WARNING, "Does not contain WAVE\n");
		return -1;
	}
	if (read(fd, &fmt, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (fmt)\n");
		return -1;
	}
	if (memcmp(&fmt, "fmt ", 4)) {
		ast_log(LOG_WARNING, "Does not say fmt\n");
		return -1;
	}
	if (read(fd, &hsize, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (formtype)\n");
		return -1;
	}
	if (ltohl(hsize) != 20) {
		ast_log(LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
		return -1;
	}
	if (read(fd, &format, 2) != 2) {
		ast_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(format) != 49) {
		ast_log(LOG_WARNING, "Not a GSM file %d\n", ltohs(format));
		return -1;
	}
	if (read(fd, &chans, 2) != 2) {
		ast_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(chans) != 1) {
		ast_log(LOG_WARNING, "Not in mono %d\n", ltohs(chans));
		return -1;
	}
	if (read(fd, &freq, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (freq)\n");
		return -1;
	}
	if (ltohl(freq) != 8000) {
		ast_log(LOG_WARNING, "Unexpected freqency %d\n", ltohl(freq));
		return -1;
	}
	/* Ignore the byte frequency */
	if (read(fd, &freq, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (X_1)\n");
		return -1;
	}
	/* Ignore the two weird fields */
	if (read(fd, &freq, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (X_2/X_3)\n");
		return -1;
	}
	/* Ignore the byte frequency */
	if (read(fd, &freq, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (Y_1)\n");
		return -1;
	}
	/* Check for the word fact */
	if (read(fd, &fact, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (fact)\n");
		return -1;
	}
	if (memcmp(&fact, "fact", 4)) {
		ast_log(LOG_WARNING, "Does not say fact\n");
		return -1;
	}
	/* Ignore the "fact value" */
	if (read(fd, &fact, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (fact header)\n");
		return -1;
	}
	if (read(fd, &fact, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (fact value)\n");
		return -1;
	}
	/* Check for the word data */
	if (read(fd, &data, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	}
	if (memcmp(&data, "data", 4)) {
		ast_log(LOG_WARNING, "Does not say data\n");
		return -1;
	}
	/* Ignore the data length */
	if (read(fd, &data, 4) != 4) {
		ast_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	}
	return 0;
}

static int update_header(int fd, int bytes)
{
	int cur;
	int datalen = htoll(bytes);
	int filelen = htoll(52 + ((bytes + 1) & ~0x1));
	cur = lseek(fd, 0, SEEK_CUR);
	if (cur < 0) {
		ast_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (lseek(fd, 4, SEEK_SET) != 4) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (write(fd, &filelen, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to set write file size\n");
		return -1;
	}
	if (lseek(fd, 56, SEEK_SET) != 56) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (write(fd, &datalen, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to set write datalen\n");
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
	unsigned int hz=htoll(8000);
	unsigned int bhz = htoll(1625);
	unsigned int hs = htoll(20);
	unsigned short fmt = htols(49);
	unsigned short chans = htols(1);
	unsigned int fhs = htoll(4);
	unsigned int x_1 = htoll(65);
	unsigned short x_2 = htols(2);
	unsigned short x_3 = htols(320);
	unsigned int y_1 = htoll(20160);
	unsigned int size = htoll(0);
	/* Write a GSM header, ignoring sizes which will be filled in later */
	if (write(fd, "RIFF", 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &size, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, "WAVEfmt ", 8) != 8) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &hs, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &fmt, 2) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &chans, 2) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &hz, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &bhz, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &x_1, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &x_2, 2) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &x_3, 2) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, "fact", 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &fhs, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &y_1, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, "data", 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &size, 4) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	return 0;
}

static struct ast_filestream *wav_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (check_header(fd)) {
			free(tmp);
			return NULL;
		}
		if (pthread_mutex_lock(&wav_lock)) {
			ast_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->fr.data = tmp->gsm;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_GSM;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->secondhalf = 0;
		tmp->lasttimeout = -1;
		glistcnt++;
		pthread_mutex_unlock(&wav_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *wav_rewrite(int fd, char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (write_header(fd)) {
			free(tmp);
			return NULL;
		}
		if (pthread_mutex_lock(&wav_lock)) {
			ast_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->lasttimeout = -1;
		glistcnt++;
		pthread_mutex_unlock(&wav_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static struct ast_frame *wav_read(struct ast_filestream *s)
{
	return NULL;
}

static void wav_close(struct ast_filestream *s)
{
	struct ast_filestream *tmp, *tmpl = NULL;
	char zero = 0;
	if (pthread_mutex_lock(&wav_lock)) {
		ast_log(LOG_WARNING, "Unable to lock wav list\n");
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
	pthread_mutex_unlock(&wav_lock);
	ast_update_use_count();
	if (!tmp) 
		ast_log(LOG_WARNING, "Freeing a filestream we don't seem to own\n");
	/* Pad to even length */
	if (s->bytes & 0x1)
		write(s->fd, &zero, 1);
	close(s->fd);
	free(s);
}

static int ast_read_callback(void *data)
{
	int retval = 0;
	int res;
	int delay = 20;
	struct ast_filestream *s = data;
	char msdata[66];
	struct timeval tv;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_GSM;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.timelen = 20;
	s->fr.datalen = 33;
	s->fr.mallocd = 0;
	if (s->secondhalf) {
		/* Just return a frame based on the second GSM frame */
		s->fr.data = s->gsm + 33;
	} else {
		if ((res = read(s->fd, msdata, 65)) != 65) {
			if (res && (res != 1))
				ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
			s->owner->streamid = -1;
			return 0;
		}
		/* Convert from MS format to two real GSM frames */
		conv65(msdata, s->gsm);
		s->fr.data = s->gsm;
	}
	s->secondhalf = !s->secondhalf;
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

static int wav_apply(struct ast_channel *c, struct ast_filestream *s)
{
	/* Select our owner for this stream, and get the ball rolling. */
	s->owner = c;
	ast_read_callback(s);
	return 0;
}

static int wav_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	char msdata[66];
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_GSM) {
		ast_log(LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
		return -1;
	}
	if (fs->secondhalf) {
		memcpy(fs->gsm + 33, f->data, 33);
		conv66(fs->gsm, msdata);
		if ((res = write(fs->fd, msdata, 65)) != 65) {
			ast_log(LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
			return -1;
		}
		fs->bytes += 65;
		update_header(fs->fd, fs->bytes);
	} else {
		/* Copy the data and do nothing */
		memcpy(fs->gsm, f->data, 33);
	}
	fs->secondhalf = !fs->secondhalf;
	return 0;
}

static char *wav_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_GSM,
								wav_open,
								wav_rewrite,
								wav_apply,
								wav_write,
								wav_read,
								wav_close,
								wav_getcomment);
								
								
}

int unload_module()
{
	struct ast_filestream *tmp, *tmpl;
	if (pthread_mutex_lock(&wav_lock)) {
		ast_log(LOG_WARNING, "Unable to lock wav list\n");
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
	pthread_mutex_unlock(&wav_lock);
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (pthread_mutex_lock(&wav_lock)) {
		ast_log(LOG_WARNING, "Unable to lock wav list\n");
		return -1;
	}
	res = glistcnt;
	pthread_mutex_unlock(&wav_lock);
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
