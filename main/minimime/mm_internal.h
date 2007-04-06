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

/** @file mm_internal.h
 * Data definitions for MiniMIME
 */
#ifndef _MM_INTERNAL_H_INCLUDED
#define _MM_INTERNAL_H_INCLUDED

#include "mm.h"

#define debugp(m, ...) do { \
	fprintf(stderr, "%s:%d:: ", __FILE__, __LINE__); \
	fprintf(stderr, m, ## __VA_ARGS__); \
	fprintf(stderr, "\n"); \
	fflush(stderr); \
} while (0); 

/**
 * @{ 
 * @name Utility functions
 */
#ifndef __HAVE_LEAK_DETECTION
void *xmalloc(size_t);
void *xrealloc(void *, size_t);
void xfree(void *);
char *xstrdup(const char *);
#endif

char *xstrsep(char **, const char *);

/* THIS FILE IS INTENTIONALLY LEFT BLANK */

#endif /* ! _MM_INTERNAL_H_INCLUDED */
