/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Frame manipulation routines
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/frame.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "asterisk.h"

#ifdef TRACE_FRAMES
static int headers = 0;
static struct ast_frame *headerlist = NULL;
static pthread_mutex_t framelock = AST_MUTEX_INITIALIZER;
#endif

#define SMOOTHER_SIZE 8000

struct ast_smoother {
	int size;
	int format;
	int readdata;
	float timeperbyte;
	struct ast_frame f;
	char data[SMOOTHER_SIZE];
	char framedata[SMOOTHER_SIZE + AST_FRIENDLY_OFFSET];
	int len;
};

struct ast_smoother *ast_smoother_new(int size)
{
	struct ast_smoother *s;
	s = malloc(sizeof(struct ast_smoother));
	if (s) {
		memset(s, 0, sizeof(s));
		s->size = size;
	}
	return s;
}

int ast_smoother_feed(struct ast_smoother *s, struct ast_frame *f)
{
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Huh?  Can't smooth a non-voice frame!\n");
		return -1;
	}
	if (!s->format) {
		s->format = f->subclass;
		s->timeperbyte = (float)f->timelen / (float)f->datalen;
	} else if (s->format != f->subclass) {
		ast_log(LOG_WARNING, "Smoother was working on %d format frames, now trying to feed %d?\n", s->format, f->subclass);
		return -1;
	}
	if (s->len + f->datalen > SMOOTHER_SIZE) {
		ast_log(LOG_WARNING, "Out of smoother space\n");
		return -1;
	}
	memcpy(s->data + s->len, f->data, f->datalen);
	s->len += f->datalen;
	return 0;
}

struct ast_frame *ast_smoother_read(struct ast_smoother *s)
{
	/* Make sure we have enough data */
	if (s->len < s->size) 
		return NULL;
	/* Make frame */
	s->f.frametype = AST_FRAME_VOICE;
	s->f.subclass = s->format;
	s->f.data = s->framedata;
	s->f.offset = AST_FRIENDLY_OFFSET;
	s->f.datalen = s->size;
	s->f.timelen = s->size * s->timeperbyte;
	/* Fill Data */
	memcpy(s->f.data  + AST_FRIENDLY_OFFSET, s->f.data, s->size);
	s->len -= s->size;
	/* Move remaining data to the front if applicable */
	if (s->len) 
		memmove(s->f.data, s->f.data + s->size, s->len);
	/* Return frame */
	return &s->f;
}

void ast_smoother_free(struct ast_smoother *s)
{
	free(s);
}

static struct ast_frame *ast_frame_header_new(void)
{
	struct ast_frame *f;
	f = malloc(sizeof(struct ast_frame));
	if (f)
		memset(f, 0, sizeof(struct ast_frame));
#ifdef TRACE_FRAMES
	if (f) {
		headers++;
		f->prev = NULL;
		ast_pthread_mutex_lock(&framelock);
		f->next = headerlist;
		if (headerlist)
			headerlist->prev = f;
		headerlist = f;
		pthread_mutex_unlock(&framelock);
	}
#endif	
	return f;
}

/*
 * Important: I should be made more efficient.  Frame headers should
 * most definitely be cached
 */

void ast_frfree(struct ast_frame *fr)
{
	if (fr->mallocd & AST_MALLOCD_DATA) {
		if (fr->data) 
			free(fr->data - fr->offset);
	}
	if (fr->mallocd & AST_MALLOCD_SRC) {
		if (fr->src)
			free(fr->src);
	}
	if (fr->mallocd & AST_MALLOCD_HDR) {
#ifdef TRACE_FRAMES
		headers--;
		ast_pthread_mutex_lock(&framelock);
		if (fr->next)
			fr->next->prev = fr->prev;
		if (fr->prev)
			fr->prev->next = fr->next;
		else
			headerlist = fr->next;
		pthread_mutex_unlock(&framelock);
#endif			
		free(fr);
	}
}

struct ast_frame *ast_frisolate(struct ast_frame *fr)
{
	struct ast_frame *out;
	if (!(fr->mallocd & AST_MALLOCD_HDR)) {
		/* Allocate a new header if needed */
		out = ast_frame_header_new();
		if (!out) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return NULL;
		}
		out->frametype = fr->frametype;
		out->subclass = fr->subclass;
		out->datalen = 0;
		out->timelen = fr->timelen;
		out->offset = 0;
		out->src = NULL;
		out->data = NULL;
	} else {
		out = fr;
	}
	if (!(fr->mallocd & AST_MALLOCD_SRC)) {
		if (fr->src)
			out->src = strdup(fr->src);
	} else
		out->src = fr->src;
	if (!(fr->mallocd & AST_MALLOCD_DATA))  {
		out->data = malloc(fr->datalen + AST_FRIENDLY_OFFSET);
		if (!out->data) {
			free(out);
			ast_log(LOG_WARNING, "Out of memory\n");
			return NULL;
		}
		out->data += AST_FRIENDLY_OFFSET;
		out->offset = AST_FRIENDLY_OFFSET;
		out->datalen = fr->datalen;
		memcpy(out->data, fr->data, fr->datalen);
	}
	out->mallocd = AST_MALLOCD_HDR | AST_MALLOCD_SRC | AST_MALLOCD_DATA;
	return out;
}

struct ast_frame *ast_frdup(struct ast_frame *f)
{
	struct ast_frame *ret;
	int p;
	p = f->mallocd;
	f->mallocd = 0;
	/* Make frisolate think this is a 100% static frame, and make a duplicate */
	ret = ast_frisolate(f);
	/* Restore its true malloc status */
	f->mallocd = p;
	return ret;
}

struct ast_frame *ast_fr_fdread(int fd)
{
	char buf[65536];
	int res;
	int ttl = sizeof(struct ast_frame);
	struct ast_frame *f = (struct ast_frame *)buf;
	/* Read a frame directly from there.  They're always in the
	   right format. */
	
	while(ttl) {
		res = read(fd, buf, ttl);
		if (res < 0) {
			ast_log(LOG_WARNING, "Bad read on %d: %s\n", fd, strerror(errno));
			return NULL;
		}
		ttl -= res;
	}
	
	/* read the frame header */
	f->mallocd = 0;
	/* Re-write data position */
	f->data = buf + sizeof(struct ast_frame);
	f->offset = 0;
	/* Forget about being mallocd */
	f->mallocd = 0;
	/* Re-write the source */
	f->src = __FUNCTION__;
	if (f->datalen > sizeof(buf) - sizeof(struct ast_frame)) {
		/* Really bad read */
		ast_log(LOG_WARNING, "Strange read (%d bytes)\n", f->datalen);
		return NULL;
	}
	if (f->datalen) {
		if ((res = read(fd, f->data, f->datalen)) != f->datalen) {
			/* Bad read */
			ast_log(LOG_WARNING, "How very strange, expected %d, got %d\n", f->datalen, res);
			return NULL;
		}
	}
	if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
		return NULL;
	}
	return ast_frisolate(f);
}

/* Some convenient routines for sending frames to/from stream or datagram
   sockets, pipes, etc (maybe even files) */

int ast_fr_fdwrite(int fd, struct ast_frame *frame)
{
	/* Write the frame exactly */
	if (write(fd, frame, sizeof(struct ast_frame)) != sizeof(struct ast_frame)) {
		ast_log(LOG_WARNING, "Write error: %s\n", strerror(errno));
		return -1;
	}
	if (write(fd, frame->data, frame->datalen) != frame->datalen) {
		ast_log(LOG_WARNING, "Write error: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int ast_fr_fdhangup(int fd)
{
	struct ast_frame hangup = {
		AST_FRAME_CONTROL,
		AST_CONTROL_HANGUP
	};
	return ast_fr_fdwrite(fd, &hangup);
}

int ast_getformatbyname(char *name)
{
	if (!strcasecmp(name, "g723.1")) 
		return AST_FORMAT_G723_1;
	else if (!strcasecmp(name, "gsm"))
		return AST_FORMAT_GSM;
	else if (!strcasecmp(name, "ulaw"))
		return AST_FORMAT_ULAW;
	else if (!strcasecmp(name, "alaw"))
		return AST_FORMAT_ALAW;
	else if (!strcasecmp(name, "mp3"))
		return AST_FORMAT_MP3;
	else if (!strcasecmp(name, "slinear"))
		return AST_FORMAT_SLINEAR;
	else if (!strcasecmp(name, "lpc10"))
		return AST_FORMAT_LPC10;
	else if (!strcasecmp(name, "adpcm"))
		return AST_FORMAT_ADPCM;
	else if (!strcasecmp(name, "all"))
		return 0x7FFFFFFF;
	return 0;
}

#ifdef TRACE_FRAMES
static int show_frame_stats(int fd, int argc, char *argv[])
{
	struct ast_frame *f;
	int x=1;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "     Framer Statistics     \n");
	ast_cli(fd, "---------------------------\n");
	ast_cli(fd, "Total allocated headers: %d\n", headers);
	ast_cli(fd, "Queue Dump:\n");
	ast_pthread_mutex_lock(&framelock);
	for (f=headerlist; f; f = f->next) {
		ast_cli(fd, "%d.  Type %d, subclass %d from %s\n", x++, f->frametype, f->subclass, f->src ? f->src : "<Unknown>");
	}
	pthread_mutex_unlock(&framelock);
	return RESULT_SUCCESS;
}

static char frame_stats_usage[] =
"Usage: show frame stats\n"
"       Displays debugging statistics from framer\n";

struct ast_cli_entry cli_frame_stats =
{ { "show", "frame", "stats", NULL }, show_frame_stats, "Shows frame statistics", frame_stats_usage };
#endif

int init_framer(void)
{
#ifdef TRACE_FRAMES
	ast_cli_register(&cli_frame_stats);
#endif
	return 0;	
}
