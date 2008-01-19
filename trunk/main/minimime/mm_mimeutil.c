/*
 * $Id$
 *
 * MiniMIME - a library for handling MIME messages
 *
 * Copyright (C) 2004 Jann Fischer <rezine@mistrust.net>
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
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "mm_internal.h"

#define MM_DATE_LENGTH 50

static const char boundary_charset[] = 
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.=";

/** @file mm_mimeutil.c
 *
 * This module contains various MIME related utility functions.
 */

/** @defgroup mimeutil MIME related utility functions */

/**
 * Generates an RFC 2822 conform date string
 *
 * @param timezone Whether to include timezone information
 * @returns A pointer to the actual date string
 * @note The pointer returned must be freed some time
 *
 * This function generates an RFC 2822 conform date string to use in message
 * headers. It allocates memory to hold the string and returns a pointer to
 * it. The generated date is in the format (example):
 *
 *	Thu, 25 December 2003 16:35:22 +0100 (CET)
 *
 * This function dynamically allocates memory and returns a pointer to it.
 * This memory should be released with free() once not needed anymore.
 */
#if 0
int
mm_mimeutil_gendate(char **result)
{
	time_t curtime;
	struct tm *curtm;
	
	if (result != NULL) {
		curtime = time(NULL);
		curtm = localtime(&curtime);
		if ((*result = (char *) malloc(MM_DATE_LENGTH)) == NULL) {
			return(-1);
		}	
		return(strftime(*result, MM_DATE_LENGTH, 
		    "%a, %d %b %G %T %z (%Z)", curtm));
	} else {
		return(-1);
	}	
}
#endif

int
mm_mimeutil_genboundary(char *prefix, size_t length, char **result)
{
	size_t total;
	size_t preflen;
	struct timeval curtm;
	int i;
	int pos;

	total = 0;
	preflen = 0;

	if (result == NULL) {
		return(-1);
	}	
	*result = NULL;

	gettimeofday(&curtm, NULL);
	srandom(curtm.tv_usec);
	
	if (prefix != NULL) {
		total = strlen(prefix);
		preflen = total;
	}

	total += length;

	if ((*result = (char *) xmalloc(total + 1)) == NULL) {
		mm_errno = MM_ERROR_ERRNO;
		return(-1);
	}	

	*result = '\0';

	if (prefix != NULL) {
		strlcat(*result, prefix, total);
	}

	for (i = 0; i < length - 1; i++) {
		pos = random() % strlen(boundary_charset);
		*result[i + preflen] = boundary_charset[pos];
	}
	*result[total] = '\0';

	return (0);
}
