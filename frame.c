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
#include <stdlib.h>
#include <string.h>

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
		out->data = malloc(fr->datalen + fr->offset);
		out->data += fr->offset;
		out->offset = fr->offset;
		out->datalen = fr->datalen;
		memcpy(out->data, fr->data, fr->datalen);
		if (!out->data) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return NULL;
		}
	}
	out->mallocd = AST_MALLOCD_HDR | AST_MALLOCD_SRC | AST_MALLOCD_DATA;
	return out;
}
