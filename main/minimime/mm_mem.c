/*
 * $Id$
 *
 * MiniMIME - a library for handling MIME messages
 *
 * Copyright (C) 2003 Jann Fischer <rezine@mistrust.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JANN FISCHER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JANN FISCHER OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "mm_internal.h"

#ifdef __HAVE_LEAK_DETECTION
#	include "mm_mem.h"

static struct MM_chunks chunks;

void *
MM_malloc(size_t size, char *filename, int line)
{
	struct MM_mem_chunk *chunk;
	void *pointer;

	pointer = malloc(size);
	if (pointer == NULL)
		fdprintf(stderr, "INFO: malloc");

	chunk = (struct MM_mem_chunk *)malloc(sizeof(struct MM_mem_chunk));
	if (chunk == NULL)
		fdprintf(stderr, "INFO: malloc");

	chunk->address = pointer;
	chunk->size = size;
	chunk->filename = filename;
	chunk->line = line;

	TAILQ_INSERT_TAIL(&chunks, chunk, next);

	return pointer;
}

char *
MM_strdup(const char *s, char *filename, int line)
{
	char *r;

	r = (char *)MM_malloc(strlen(s)+1, filename, line);
	strlcpy(r, s, strlen(s) + 1);
	if (strlen(r) != strlen(s)) {
		debugp("%d:%d", strlen(s), strlen(r));
	}
	return r;

}

void *
MM_realloc(void *p, size_t new_size, char *filename, int line)
{
	void *r;
	void *a;
	struct MM_mem_chunk *chunk;
	struct MM_mem_chunk *last;
	
	a = p;
	chunk = NULL;
	last = NULL;

	assert(new_size > 0);

	TAILQ_FOREACH(chunk, &chunks, next) {
		if (chunk->address == p) {
			last = chunk;
		}
	}

	if (last == NULL) {
		debugp("MM_realloc: did not find chunk at %p (%s:%d) "
		    ", creating new", p, filename, line);
		return MM_malloc(new_size, filename, line);
	}

	r = realloc(p, new_size);
	if (r == NULL)
		return NULL;

	last->address = r;
	last->size = new_size;
	last->filename = filename;
	last->line = line;

	return r;
}

void
MM_free(void *pointer, char *filename, int line, char *name)
{
	struct MM_mem_chunk *chunk, *nxt;

	for (chunk = TAILQ_FIRST(&chunks); chunk != TAILQ_END(&chunks);
	    chunk = nxt) {
		nxt = TAILQ_NEXT(&chunks, next);
		if (chunk->address == pointer) {
			TAILQ_REMOVE(&chunks, chunk, next);
			free(chunk->address);
			free(chunk);
			return;
		}
	}

	debugp("FREE: did not find storage %s (at %p), %s:%d", name, pointer,
	    filename, line);
}

void
MM_leakd_flush(void)
{
	debugp("flushing memory informations");
	while (!TAILQ_EMPTY(&chunks))
		SLIST_REMOVE_HEAD(&chunks, next);
}

void
MM_leakd_printallocated(void)
{
	struct MM_mem_chunk *chunk;
	debugp("printing dynamic memory allocations");
	TAILQ_FOREACH(chunk, &chunks, next) {
		debugp(" chunk: %p (alloc'ed at %s:%d, size %d)\n", 
		    chunk->address, chunk->filename, chunk->line, chunk->size);
	}
}

void
MM_leakd_init(void)
{
	TAILQ_INIT(&chunks);
}

#endif /* !__HAVE_LEAK_DETECTOR */
