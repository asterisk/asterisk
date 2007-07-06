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
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "mm_internal.h"
#include "mm_util.h"

#include "mimeparser.h"
#include "mimeparser.tab.h"

/** @file mm_parse.c
 *
 * Functions to parse MIME messages
 */

/**
 * Parses a NUL-terminated string into a MiniMIME context
 *
 * @param ctx A valid MiniMIME context object
 * @param text The NUL-terminated string to parse
 * @param parsemode The parsemode
 * @param flags The flags to pass to the parser
 * @returns 0 on success or -1 on failure
 * @note Sets mm_errno if an error occurs
 *
 * This function parses a MIME message, stored in the memory region pointed to
 * by text (must be NUL-terminated) according to the parseflags and stores the
 * results in the MiniMIME context specified by ctx.
 *
 * The following modes can be used to specify how the message should be
 * parsed:
 *
 *	- MM_PARSE_STRICT: Do not tolerate MIME violations
 *	- MM_PARSE_LOOSE: Tolerate as much MIME violations as possible
 *
 * The context needs to be initialized before using mm_context_new() and may
 * be freed using mm_context_free().
 */
int
mm_parse_mem(MM_CTX *ctx, const char *text, int parsemode, int flags)
{
	void *yyscanner;
	int res;
	struct parser_state pstate;
	
	pstate.ctx = ctx;
	pstate.parsemode = parsemode;

	mimeparser_yylex_init(&yyscanner);
	PARSER_initialize(&pstate, yyscanner);
	
	PARSER_setbuffer(text, yyscanner);
	PARSER_setfp(NULL, yyscanner);
	
	res =  mimeparser_yyparse(&pstate,yyscanner);
	mimeparser_yylex_destroy(yyscanner);
	return res;
}

/**
 * Parses a file into a MiniMIME context
 *
 * @param ctx A valid MiniMIME context object
 * @param filename The name of the file to parse
 * @param parsemode The parsemode
 * @param flags The flags to pass to the parser
 * @returns 0 on success or -1 on failure
 * @note Sets mm_errno if an error occurs
 *
 * This function parses a MIME message, stored in the filesystem according to
 * the parseflags and stores the results in the MiniMIME context specified by 
 * ctx.
 *
 * The following modes can be used to specify how the message should be
 * parsed:
 *
 *	- MM_PARSE_STRICT: Do not tolerate MIME violations
 *	- MM_PARSE_LOOSE: Tolerate as much MIME violations as possible
 *
 * The context needs to be initialized before using mm_context_new() and may
 * be freed using mm_context_free().
 */
int
mm_parse_file(MM_CTX *ctx, const char *filename, int parsemode, int flags)
{
	FILE *fp;
	int res;
	void *yyscanner;
	struct parser_state pstate;

	mimeparser_yylex_init(&yyscanner);

	if ((fp = fopen(filename, "r")) == NULL) {
		mm_errno = MM_ERROR_ERRNO;
		return -1;
	}

	PARSER_setfp(fp,yyscanner);
	PARSER_initialize(&pstate, yyscanner);

	pstate.ctx = ctx;
	pstate.parsemode = parsemode;

	res = mimeparser_yyparse(&pstate,yyscanner);
	mimeparser_yylex_destroy(yyscanner);
	fclose(fp);
	return res;
}

int
mm_parse_fileptr(MM_CTX *ctx, FILE *f, int parsemode, int flags)
{
	int res;
	void *yyscanner;
	struct parser_state pstate;

	mimeparser_yylex_init(&yyscanner);

	PARSER_setfp(f, yyscanner);
	PARSER_initialize(&pstate, yyscanner);

	pstate.ctx = ctx;
	pstate.parsemode = parsemode;

	res = mimeparser_yyparse(&pstate,yyscanner);
	mimeparser_yylex_destroy(yyscanner);

	return res;
}
