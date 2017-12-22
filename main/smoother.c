/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Frame smoother manipulation routines
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/frame.h"
#include "asterisk/astobj2.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"
#include "asterisk/format.h"
#include "asterisk/codec.h"
#include "asterisk/smoother.h"

#define SMOOTHER_SIZE 8000

struct ast_smoother {
	int size;
	struct ast_format *format;
	int flags;
	float samplesperbyte;
	unsigned int opt_needs_swap:1;
	struct ast_frame f;
	struct timeval delivery;
	char data[SMOOTHER_SIZE];
	char framedata[SMOOTHER_SIZE + AST_FRIENDLY_OFFSET];
	struct ast_frame *opt;
	int len;
};

static int smoother_frame_feed(struct ast_smoother *s, struct ast_frame *f, int swap)
{
	if (s->flags & AST_SMOOTHER_FLAG_G729) {
		if (s->len % 10) {
			ast_log(LOG_NOTICE, "Dropping extra frame of G.729 since we already have a VAD frame at the end\n");
			return 0;
		}
	}
	if (swap) {
		ast_swapcopy_samples(s->data + s->len, f->data.ptr, f->samples);
	} else {
		memcpy(s->data + s->len, f->data.ptr, f->datalen);
	}
	/* If either side is empty, reset the delivery time */
	if (!s->len || ast_tvzero(f->delivery) || ast_tvzero(s->delivery)) {	/* XXX really ? */
		s->delivery = f->delivery;
	}
	s->len += f->datalen;

	return 0;
}

void ast_smoother_reset(struct ast_smoother *s, int bytes)
{
	ao2_cleanup(s->format);
	memset(s, 0, sizeof(*s));
	s->size = bytes;
}

void ast_smoother_reconfigure(struct ast_smoother *s, int bytes)
{
	/* if there is no change, then nothing to do */
	if (s->size == bytes) {
		return;
	}
	/* set the new desired output size */
	s->size = bytes;
	/* if there is no 'optimized' frame in the smoother,
	 *   then there is nothing left to do
	 */
	if (!s->opt) {
		return;
	}
	/* there is an 'optimized' frame here at the old size,
	 * but it must now be put into the buffer so the data
	 * can be extracted at the new size
	 */
	smoother_frame_feed(s, s->opt, s->opt_needs_swap);
	s->opt = NULL;
}

struct ast_smoother *ast_smoother_new(int size)
{
	struct ast_smoother *s;
	if (size < 1)
		return NULL;
	if ((s = ast_calloc(1, sizeof(*s))))
		ast_smoother_reset(s, size);
	return s;
}

int ast_smoother_get_flags(struct ast_smoother *s)
{
	return s->flags;
}

void ast_smoother_set_flags(struct ast_smoother *s, int flags)
{
	s->flags = flags;
}

int ast_smoother_test_flag(struct ast_smoother *s, int flag)
{
	return (s->flags & flag);
}

int __ast_smoother_feed(struct ast_smoother *s, struct ast_frame *f, int swap)
{
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Huh?  Can't smooth a non-voice frame!\n");
		return -1;
	}
	if (!s->format) {
		s->format = ao2_bump(f->subclass.format);
		s->samplesperbyte = (float)f->samples / (float)f->datalen;
	} else if (ast_format_cmp(s->format, f->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_log(LOG_WARNING, "Smoother was working on %s format frames, now trying to feed %s?\n",
			ast_format_get_name(s->format), ast_format_get_name(f->subclass.format));
		return -1;
	}
	if (s->len + f->datalen > SMOOTHER_SIZE) {
		ast_log(LOG_WARNING, "Out of smoother space\n");
		return -1;
	}
	if (((f->datalen == s->size) ||
	     ((f->datalen < 10) && (s->flags & AST_SMOOTHER_FLAG_G729))) &&
	    !s->opt &&
	    !s->len &&
	    (f->offset >= AST_MIN_OFFSET)) {
		/* Optimize by sending the frame we just got
		   on the next read, thus eliminating the douple
		   copy */
		if (swap)
			ast_swapcopy_samples(f->data.ptr, f->data.ptr, f->samples);
		s->opt = f;
		s->opt_needs_swap = swap ? 1 : 0;
		return 0;
	}

	return smoother_frame_feed(s, f, swap);
}

struct ast_frame *ast_smoother_read(struct ast_smoother *s)
{
	struct ast_frame *opt;
	int len;

	/* IF we have an optimization frame, send it */
	if (s->opt) {
		if (s->opt->offset < AST_FRIENDLY_OFFSET)
			ast_log(LOG_WARNING, "Returning a frame of inappropriate offset (%d).\n",
							s->opt->offset);
		opt = s->opt;
		s->opt = NULL;
		return opt;
	}

	/* Make sure we have enough data */
	if (s->len < s->size) {
		/* Or, if this is a G.729 frame with VAD on it, send it immediately anyway */
		if (!((s->flags & AST_SMOOTHER_FLAG_G729) && (s->len % 10)))
			return NULL;
	}
	len = s->size;
	if (len > s->len)
		len = s->len;
	/* Make frame */
	s->f.frametype = AST_FRAME_VOICE;
	s->f.subclass.format = s->format;
	s->f.data.ptr = s->framedata + AST_FRIENDLY_OFFSET;
	s->f.offset = AST_FRIENDLY_OFFSET;
	s->f.datalen = len;
	/* Samples will be improper given VAD, but with VAD the concept really doesn't even exist */
	s->f.samples = len * s->samplesperbyte;	/* XXX rounding */
	s->f.delivery = s->delivery;
	/* Fill Data */
	memcpy(s->f.data.ptr, s->data, len);
	s->len -= len;
	/* Move remaining data to the front if applicable */
	if (s->len) {
		/* In principle this should all be fine because if we are sending
		   G.729 VAD, the next timestamp will take over anyawy */
		memmove(s->data, s->data + len, s->len);
		if (!ast_tvzero(s->delivery)) {
			/* If we have delivery time, increment it, otherwise, leave it at 0 */
			s->delivery = ast_tvadd(s->delivery, ast_samp2tv(s->f.samples,
				ast_format_get_sample_rate(s->format)));
		}
	}
	/* Return frame */
	return &s->f;
}

void ast_smoother_free(struct ast_smoother *s)
{
	ao2_cleanup(s->format);
	ast_free(s);
}
