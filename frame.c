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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

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
		free(fr);
	}
}

void ast_frchain(struct ast_frame_chain *fc)
{
	struct ast_frame_chain *last;
	while(fc) {
		last = fc;
		fc = fc->next;
		if (last->fr)
			ast_frfree(last->fr);
		free(last);
	}
}

struct ast_frame *ast_frisolate(struct ast_frame *fr)
{
	struct ast_frame *out;
	if (!(fr->mallocd & AST_MALLOCD_HDR)) {
		/* Allocate a new header if needed */
		out = malloc(sizeof(struct ast_frame));
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
	char buf[4096];
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
