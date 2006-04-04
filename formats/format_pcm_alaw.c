/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Flat, binary, alaw PCM file format.
 * \arg File name extensions: alaw, al
 * \ingroup formats
 */
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"
#include "asterisk/alaw.h"

#define BUF_SIZE 160		/* 160 bytes, and same number of samples */

/* #define REALTIME_WRITE */	/* XXX does it work at all ? */

struct pcma_desc {
#ifdef REALTIME_WRITE
	unsigned long start_time;
#endif
};

static char alaw_silence[BUF_SIZE];

#if 0
/* Returns time in msec since system boot. */
static unsigned long get_time(struct ast_filestream *s)
{
	struct tms buf;
	clock_t cur;
	unsigned long *res;

	cur = times( &buf );
	if( cur < 0 )
	{
		ast_log( LOG_WARNING, "Cannot get current time\n" );
		return 0;
	}
	res = cur * 1000 / sysconf( _SC_CLK_TCK );
	if (s) {
		struct pcma_desc *d = (struct pcma_filestream *)s->private;
		d->start_time = res;
	}
	return res;
}
#endif

static int pcm_open(struct ast_filestream *s)
{
#ifdef REALTIME_WRITE
	get_time(s);
#endif
	return 0;
}

static int pcm_rewrite(struct ast_filestream *s, const char *comment)
{
	return pcm_open(s);
}

static struct ast_frame *pcm_read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ALAW;
	s->fr.mallocd = 0;
	FR_SET_BUF(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data, 1, s->fr.datalen, s->f)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.datalen = res;
	*whennext = s->fr.samples = res;
	return &s->fr;
}

static int pcm_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
#ifdef REALTIME_WRITE
	unsigned long cur_time;
	unsigned long fpos;
	struct stat stat_buf;
	struct pcma_filestream *s = (struct pcma_filestream *)fs->private;
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
	fpos = ( cur_time - s->start_time ) * 8;	/* 8 bytes per msec */
	/* Check if we have written to this position yet. If we have, then increment pos by one frame
	*  for some degree of protection against receiving packets in the same clock tick.
	*/
	
	fstat(fileno(fs->f), &stat_buf );
	if (stat_buf.st_size > fpos ) {
		fpos += f->datalen;	/* Incrementing with the size of this current frame */
	}

	if (stat_buf.st_size < fpos) {
		/* fill the gap with 0x55 rather than 0. */
		char buf[ 512 ];
		unsigned long cur, to_write;

		cur = stat_buf.st_size;
		if (fseeko(fs->f, cur, SEEK_SET) < 0) {
			ast_log( LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
			return -1;
		}
		memset(buf, 0x55, 512);
		while (cur < fpos) {
			to_write = fpos - cur;
			if (to_write > 512) {
				to_write = 512;
			}
			fwrite(buf, 1, to_write, fs->f);
			cur += to_write;
		}
	}


	if (fseeko(s->f, fpos, SEEK_SET) < 0) {
		ast_log( LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
		return -1;
	}
#endif	/* REALTIME_WRITE */
	
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int pcm_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t cur, max, offset = 0;
 	int ret = -1; /* assume error */

	cur = ftello(fs->f);
	fseeko(fs->f, 0, SEEK_END);
	max = ftello(fs->f);

	switch (whence) {
	case SEEK_SET:
		offset = sample_offset;
		break;
	case SEEK_END:
		offset = max - sample_offset;
		break;
	case SEEK_CUR:
	case SEEK_FORCECUR:
		offset = cur + sample_offset;
		break;
	default:
		ast_log(LOG_WARNING, "invalid whence %d, assuming SEEK_SET\n", whence);
		offset = sample_offset;
	}

	if (offset < 0) {
		offset = 0;
		ast_log(LOG_WARNING, "negative offset %ld, resetting to 0\n", (long) offset);
	}
	if (whence == SEEK_FORCECUR && offset > max) {
		size_t left = offset - max;

		while (left) {
			size_t written = fwrite(alaw_silence, sizeof(alaw_silence[0]),
				     (left > BUF_SIZE) ? BUF_SIZE : left, fs->f);
			if (written == -1)
				break; /* error */
			left -= written * sizeof(alaw_silence[0]);
		}
		ret = 0; /* success */
	} else {
		if (offset > max) {
			ast_log(LOG_WARNING, "offset too large %ld, truncating to %ld\n", (long) offset, (long) max);
			offset = max;
		}
		ret = fseeko(fs->f, offset, SEEK_SET);
	}
	return ret;
}

static int pcm_trunc(struct ast_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftello(fs->f));
}

static off_t pcm_tell(struct ast_filestream *fs)
{
	return ftello(fs->f);
}

static struct ast_format_lock me = { .usecnt = -1 };

static const struct ast_format alaw_f = {
	.name = "alaw",
	.exts = "alaw|al",
	.format = AST_FORMAT_ALAW,
	.open = pcm_open,
	.rewrite = pcm_rewrite,
	.write = pcm_write,
	.seek = pcm_seek,
	.trunc = pcm_trunc,
	.tell = pcm_tell,
	.read = pcm_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.lockp = &me,
#ifdef	REALTIME_WRITE
	.desc_size = sizeof(struct pcma_desc),
#endif
};

int load_module()
{
	int index;

	for (index = 0; index < (sizeof(alaw_silence) / sizeof(alaw_silence[0])); index++)
		alaw_silence[index] = AST_LIN2A(0);

	return ast_format_register(&alaw_f);
}

int unload_module()
{
	return ast_format_unregister(alaw_f.name);
}	

int usecount()
{
	return me.usecnt;
}

char *description()
{
	return "Raw aLaw 8khz PCM Audio support";
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
