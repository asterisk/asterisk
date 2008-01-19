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
#include <string.h>
#include <time.h>
#include <assert.h>

#include "mm_internal.h"

/** @file mm_util.c
 *
 * This module contains utility functions for the MiniMIME library
 */

/** @defgroup util General purpose utility functions */

#ifndef __HAVE_LEAK_DETECTION
/**
 * Allocates a block of memory
 *
 * @param size The size of the memory region to allocate
 * @return A pointer to the allocated memory region
 * @ingroup util
 *
 * xmalloc() calls abort() if either the size argument is negative or the
 * requested memory amount could not be allocated via an assert() call.
 */
void *
xmalloc(size_t size)
{
	void *p;

	assert(size > 0);
	p = malloc(size);
	assert(p != NULL);

	return p;
}

/**
 * realloc() wrapper
 *
 * @param p Pointer to a memory region which should be reallocated
 * @param size The new size of the memory region
 * @return A pointer to the reallocated memory region
 * @ingroup util
 *
 * xrealloc() is a wrapper around realloc() which calls abort() if either the
 * size argument is negative or the requested memory amount could not be
 * allocated.
 */
void *
xrealloc(void *p, size_t size)
{
	void *n;
	
	assert(size > 0);
	n = realloc(p, size);
	assert(n != NULL);

	return n;
}

char *
xstrdup(const char *str)
{
	char *p;

	assert(str != NULL);
	p = strdup(str);
	assert(p != NULL);
	
	return p;
}

void
xfree(void *p)
{
	assert(p != NULL);
	free(p);
	p = NULL;
	assert(p == NULL);
}
#endif /* ! __HAVE_LEAK_DETECTION */

/**
 * Unquotes a string
 *
 * @param string The quoted string to unquote
 * @return A pointer to the unquoted string
 * @ingroup util
 *
 * This function unquotes a string. That is, it returns a pointer to a newly
 * allocated memory region in which the unquoted string is stored. Only
 * leading and trailing double-qoutes are removed. The string needs to be
 * freed when it is not needed anymore.
 */
char *
mm_unquote(const char *string)
{
	char *ret;

	if (string[0] != '\"' || string[strlen(string)-1] != '\"')
		return xstrdup(string);

	ret = xstrdup(string + 1);
	ret[strlen(ret)-1] = '\0';

	return ret;
}


/**
 * Removes MIME comments from a string
 *
 * @param string The string to uncomment
 * @return A pointer to the uncommented string or NULL on error. Sets mm_errno.
 * @ingroup util
 *
 * This function removes MIME comments from a string (included in parantheses).
 * It returns a pointer to a newly allocated memory region in which the
 * uncommented string is stored. The returned string needs to be freed when
 * it's not used anymore.
 */
char *
mm_uncomment(const char *string)
{
	char *buf, *new, *orig, *token;
	size_t new_size;
	int found;
	int open;

	assert(string != NULL);
	
	new_size = strlen(string) + 1;
	new = NULL;
	buf = NULL;
	orig = NULL;
	found = 0;
	open = 0;
	mm_errno = MM_ERROR_NONE;

	buf = xstrdup(string);
	orig = buf;

	while (*buf != '\0') {
		if (*buf == '(') {
			open++;
			new_size--;
			found++;
		} else if (*buf == ')') {
			open--;
			new_size--;
		} else {
			if (open)
				new_size--;
		}
		buf++;
	}

	if (open != 0) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("Uncommenting: parantheses are unbalanced");
		goto cleanup;
	}

	if (!found) {
		new = orig;
		return orig;
	}

	new = xmalloc(new_size + 1);
	*new = '\0';
	buf = orig;
	token = buf;

	/* Tokenize our string by parentheses, and copy the portions which are
	 * not commented to our destination.
	 */
	open = 0;
	while (*buf != '\0') {
		if (*buf == '(') {
			if (!open) {
				*buf = '\0';
				strlcat(new, token, new_size);
				token = buf+1;
			}
			open++;
		}
		if (*buf == ')') {
			open--;
			token = buf + 1;
		}
		buf++;
	}

	strlcat(new, token, new_size);
	
cleanup:
	if (orig != NULL) {
		xfree(orig);
		orig = NULL;
	}

	if (mm_errno != MM_ERROR_NONE) {
		if (new != NULL) {
			xfree(new);
			new = NULL;
		}
		return NULL;
	} else {
		return new;
	}
}

/**
 * separate strings
 *
 * @param stringp A pointer to the string being splitted
 * @param delim The delimeter string
 * @ingroup util
 *
 * This function works similar to strsep(), with the difference that delim is
 * treated as a whole.
 */
char *
xstrsep(char **stringp, const char *delim)
{
	char *p;
	char *s;
	char *r;

	if (*stringp == NULL || *stringp == '\0')
		return NULL;

	p = *stringp;

	if ((s = strstr(p, delim)) == NULL) {
		r = p;
		while (*p != '\0')
			p++;
		*stringp = NULL;
		return r;
	} else {
		r = p;
		p += strlen(p) - strlen(s);
		*p = '\0';
		*stringp = p + strlen(delim);
		return r;
	}
}

/**
 * Strips a given character set from a string
 *
 * @param input The string which to strip
 * @param strip The character set to strip off
 * @return A copy of the original string with all chars stripped
 * @ingroup util
 */
char *
mm_stripchars(char *input, char *strip)
{
	char *output, *orig;
	int i, j, chars;

	assert(input != NULL);
	assert(strip != '\0');
	
	chars = 0;
	orig = input;

	while (*orig != '\0') {
		for (i = 0; i < strlen(strip); i++) {
			if (*orig == strip[i]) {
				chars++;
				break;
			}
		}
		orig++;
	}
	
	/* If we have not found any char in the input, return a dup of the orig
	   string */
	if (chars == 0)
		return(xstrdup(input));

	output = (char *)xmalloc(strlen(input) - chars);
	orig = output;

	for (i = 0; i < strlen(input); i++) {
		int stripc;
		stripc = 0;
		for (j = 0; j < strlen(strip); j++) {
			if (input[i] == strip[j]) {
				stripc = 1;
				break;
			}
		}
		if (stripc == 0) {
			*output = input[i];
			output++;
		}
	}

	*output = '\0';

	return(orig);
}

/**
 * Adds characters to a string at given positions
 *
 * @param input The string to which to add characters
 * @param add The character string to add
 * @param linelength The position where to add the character
 * @return A copy of the string with characters added
 * @ingroup util
 *
 * This function adds the characters add at each linelength positions and
 * returns this new string.
 */
char *
mm_addchars(char *input, char *add, uint16_t linelength)
{
	uint32_t len;
	uint32_t i;
	uint32_t l;
	uint32_t j;
	uint16_t addcrlf;
	char *output;
	char *orig;
	
	len = strlen(input);
	if (len <= linelength)
		return(xstrdup(input));

	addcrlf = len / linelength;

	output = (char *)xmalloc(len + (addcrlf * strlen(add)));
	orig = output;
	
	for (i = 0, l = 0; i < len; i++, l++) {
		if (l == linelength) {
			for (j = 0; j < strlen(add); j++) {
				*output = add[j];
				output++;
			}
			l = 0;
		}
		*output = input[i];
		output++;
	}

	*output = '\0';
	output = orig;

	return(orig);
}

void
mm_striptrailing(char **what, const char *charset)
{
	size_t eos, i, hit; 
	char *str;

	str = *what;
	for (eos = strlen(str)-1; eos >= 0; eos--) { 
		hit = 0;
		for (i = 0; i < strlen(charset); i++) { 
			if (str[eos] == charset[i]) {
				str[eos] = '\0'; 
				hit = 1; 
				break; 
			} 
		}
		if (!hit)
			break;
	}
}
