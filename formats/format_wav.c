/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Microsoft WAV File Format using libaudiofile 
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
#include <audiofile.h>


/* Read 320 samples at a time, max */ 
#define WAV_MAX_SIZE 320

/* Fudge in milliseconds */
#define WAV_FUDGE 2

struct ast_filestream {
	/* First entry MUST be reserved for the channel type */
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	/* Audio File */
	AFfilesetup afs;
	AFfilehandle af;
	int lasttimeout;
	struct ast_channel *owner;
	struct ast_filestream *next;
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	short samples[WAV_MAX_SIZE];
};


static struct ast_filestream *glist = NULL;
static pthread_mutex_t wav_lock = PTHREAD_MUTEX_INITIALIZER;
static int glistcnt = 0;

static char *name = "wav";
static char *desc = "Microsoft WAV format (PCM/16, 8000Hz mono)";
static char *exts = "wav";

static struct ast_filestream *wav_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	int notok = 0;
	int fmt, width;
	double rate;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		tmp->afs = afNewFileSetup();
		if (!tmp->afs) {
			ast_log(LOG_WARNING, "Unable to create file setup\n");
			free(tmp);
			return NULL;
		}
		afInitFileFormat(tmp->afs, AF_FILE_WAVE);
		tmp->af = afOpenFD(fd, "r", tmp->afs);
		if (!tmp->af) {
			afFreeFileSetup(tmp->afs);
			ast_log(LOG_WARNING, "Unable to open file descriptor\n");
			free(tmp);
			return NULL;
		}
#if 0
		afGetFileFormat(tmp->af, &version);
		if (version != AF_FILE_WAVE) {
			ast_log(LOG_WARNING, "This is not a wave file (%d)\n", version);
			notok++;
		}
#endif
		/* Read the format and make sure it's exactly what we seek. */
		if (afGetChannels(tmp->af, AF_DEFAULT_TRACK) != 1) {
			ast_log(LOG_WARNING, "Invalid number of channels %d.  Should be mono (1)\n", afGetChannels(tmp->af, AF_DEFAULT_TRACK));
			notok++;
		}
		afGetSampleFormat(tmp->af, AF_DEFAULT_TRACK, &fmt, &width);
		if (fmt != AF_SAMPFMT_TWOSCOMP) {
			ast_log(LOG_WARNING, "Input file is not signed\n");
			notok++;
		}
		rate = afGetRate(tmp->af, AF_DEFAULT_TRACK);
		if ((rate < 7900) || (rate > 8100)) {
			ast_log(LOG_WARNING, "Rate %f is not close enough to 8000 Hz\n", rate);
			notok++;
		}
		if (width != 16) {
			ast_log(LOG_WARNING, "Input file is not 16-bit\n");
			notok++;
		}
		if (notok) {
			afCloseFile(tmp->af);
			afFreeFileSetup(tmp->afs);
			free(tmp);
			return NULL;
		}
		if (pthread_mutex_lock(&wav_lock)) {
			afCloseFile(tmp->af);
			afFreeFileSetup(tmp->afs);
			ast_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->next = glist;
		glist = tmp;
		tmp->fd = fd;
		tmp->owner = NULL;
		tmp->fr.data = tmp->samples;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_SLINEAR;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
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
		tmp->afs = afNewFileSetup();
		if (!tmp->afs) {
			ast_log(LOG_WARNING, "Unable to create file setup\n");
			free(tmp);
			return NULL;
		}
		/* WAV format */
		afInitFileFormat(tmp->afs, AF_FILE_WAVE);
		/* Mono */
		afInitChannels(tmp->afs, AF_DEFAULT_TRACK, 1);
		/* Signed linear, 16-bit */
		afInitSampleFormat(tmp->afs, AF_DEFAULT_TRACK, AF_SAMPFMT_TWOSCOMP, 16);
		/* 8000 Hz */
		afInitRate(tmp->afs, AF_DEFAULT_TRACK, (double)8000.0);
		tmp->af = afOpenFD(fd, "w", tmp->afs);
		if (!tmp->af) {
			afFreeFileSetup(tmp->afs);
			ast_log(LOG_WARNING, "Unable to open file descriptor\n");
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
	afCloseFile(tmp->af);
	afFreeFileSetup(tmp->afs);
	close(s->fd);
	free(s);
}

static int ast_read_callback(void *data)
{
	u_int32_t delay = -1;
	int retval = 0;
	int res;
	struct ast_filestream *s = data;
	/* Send a frame from the file to the appropriate channel */

	if ((res = afReadFrames(s->af, AF_DEFAULT_TRACK, s->samples, sizeof(s->samples)/2)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		s->owner->streamid = -1;
		return 0;
	}
	/* Per 8 samples, one milisecond */
	delay = res / 8;
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_SLINEAR;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.datalen = res * 2;
	s->fr.data = s->samples;
	s->fr.mallocd = 0;
	s->fr.timelen = delay;
	/* Unless there is no delay, we're going to exit out as soon as we
	   have processed the current frame. */
	/* If there is a delay, lets schedule the next event */
	if (delay != s->lasttimeout) {
		/* We'll install the next timeout now. */
		s->owner->streamid = ast_sched_add(s->owner->sched, 
											  delay, 
											  ast_read_callback, s);
		
		s->lasttimeout = delay;
	} else {
		/* Just come back again at the same time */
		retval = -1;
	}
	/* Lastly, process the frame */
	if (ast_write(s->owner, &s->fr)) {
		ast_log(LOG_WARNING, "Failed to write frame\n");
		s->owner->streamid = -1;
		return 0;
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
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Asked to write non-signed linear frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = afWriteFrames(fs->af, AF_DEFAULT_TRACK, f->data, f->datalen/2)) != f->datalen/2) {
		ast_log(LOG_WARNING, "Unable to write frame: res=%d (%s)\n", res, strerror(errno));
		return -1;
	}	
	return 0;
}

char *wav_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_SLINEAR,
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

