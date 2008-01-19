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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "mm_internal.h"

/**
 * Attaches a warning message to a context
 *
 * @param ctx A valid MiniMIME context object
 * @param type The type of the warning
 * @param fmt The warning message as format string
 */
void
mm_warning_add(MM_CTX *ctx, int type, const char *fmt, ...)
{
	struct mm_warning *warning;
	char buf[1024];
	va_list ap;

	assert(ctx != NULL);

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	warning = (struct mm_warning *)xmalloc(sizeof(struct mm_warning));
	warning->message = xstrdup(buf);
	warning->type = type;

	if (SLIST_EMPTY(&ctx->warnings)) {
		SLIST_INSERT_HEAD(&ctx->warnings, warning, next);
	} else {
		struct mm_warning *last, *after;

		after = NULL;

		SLIST_FOREACH(last, &ctx->warnings, next) {
			if (last != NULL) {
				after = last;
			}
		}
		
		assert(after != NULL);

		SLIST_INSERT_AFTER(after, warning, next);
	}
}

struct mm_warning *
mm_warning_next(MM_CTX *ctx, struct mm_warning **last)
{
	struct mm_warning *warning;

	if (*last == NULL) {
		warning = SLIST_FIRST(&ctx->warnings);
	} else {
		warning = SLIST_NEXT(*last, next);
	}

	*last = warning;
	return warning;
}
