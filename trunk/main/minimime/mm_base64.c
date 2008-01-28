/*
 * Copyright (C) 2003 Jann Fischer <jfi@openbsd.de>
 * All rights reserved.
 *
 * XXX: This piece of software is not nearly MIME compatible as it should be.
 *
 * This is based on third-party code, see the copyright notice below.
 *
 */

/* $Id$ */

/***********************************************************
        Copyright 1998 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Carnegie Mellon
University not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE FOR
ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mm_internal.h"

#define XX 127

static int _mm_base64_decode(char *);
static char *_mm_base64_encode(char *, uint32_t);

/*
 * Tables for encoding/decoding base64
 */
static const char basis_64[] =
   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char index_64[256] = {
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,62, XX,XX,XX,63,
	52,53,54,55, 56,57,58,59, 60,61,XX,XX, XX,XX,XX,XX,
	XX, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
	15,16,17,18, 19,20,21,22, 23,24,25,XX, XX,XX,XX,XX,
	XX,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
	41,42,43,44, 45,46,47,48, 49,50,51,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
	XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
};
#define CHAR64(c)  (index_64[(unsigned char)(c)])

/*
 * mm_base64_decode()
 *
 * Decodes the data pointed to by 'data' from the BASE64 encoding to the data
 * format it was encoded from. Returns a pointer to a string on success or
 * NULL on error. The string returned needs to be freed by the caller at some
 * later point.
 *
 */
char *
mm_base64_decode(char *data)
{
	char *buf;

	assert(data != NULL);

	buf = mm_stripchars(data, "\r\n");
	assert(buf != NULL);

	_mm_base64_decode(buf);
	assert(buf != NULL);
	return(buf);
}

/*
 * mm_base64_encode()
 *
 * Encodes the data pointed to by 'data', which is of the length specified in
 * 'len' to the BASE64 format. Returns a pointer to a string containing the
 * BASE64 encoding, whose lines are broken at the MIME recommended linelength
 * of 76 characters. If an error occured, returns NULL. The string returned
 * needs to be freed by the caller at some later point.
 *
 */
char *
mm_base64_encode(char *data, uint32_t len) {
	char *buf;
	char *ret;

	assert(data != NULL);

	buf = _mm_base64_encode(data, len);
	assert(buf != NULL);

	ret = mm_addchars(buf, "\r\n", MM_BASE64_LINELEN);
	xfree(buf);
	assert(ret != NULL);
	return ret;
}

/*
 * Decode in-place the base64 data in 'input'.  Returns the length
 * of the decoded data, or -1 if there was an error.
 */
static int
_mm_base64_decode(char *input)
{
	uint32_t len = 0;
	unsigned char *output = (unsigned char *)input;
	int c1, c2, c3, c4;

	while (*input) {
		c1 = *input++;
		if (CHAR64(c1) == XX) return -1;
		c2 = *input++;
		if (CHAR64(c2) == XX) return -1;
		c3 = *input++;
		if (c3 != '=' && CHAR64(c3) == XX) return -1; 
		c4 = *input++;
		if (c4 != '=' && CHAR64(c4) == XX) return -1;
		*output++ = (CHAR64(c1) << 2) | (CHAR64(c2) >> 4);
		++len;
		if (c3 == '=') break;
		*output++ = ((CHAR64(c2) << 4) & 0xf0) | (CHAR64(c3) >> 2);
		++len;
		if (c4 == '=') break;
		*output++ = ((CHAR64(c3) << 6) & 0xc0) | CHAR64(c4);
		++len;
	}
	*output = 0;

	return len;
}

/*
 * Encode the given binary string of length 'len' and return Base64
 * in a char buffer.  It allocates the space for buffer.
 * caller must free the space.
 */
static char *
_mm_base64_encode(char *data, uint32_t len)
{
	char *buf;
	uint32_t buflen;
	int c1;
	int c2;
	int c3;
	uint32_t maxbuf;

	buflen = 0;

#ifdef RUBBISH
	maxbuf = len*4/3 + 1;  /* size after expantion */
#endif
	maxbuf = len*2 + 20;  /* size after expantion */

	buf = (char *)xmalloc(maxbuf);

	while (len && buflen < (maxbuf - 6)) {

		c1 = (unsigned char)*data++;
		buf[buflen++] = basis_64[c1>>2];

		if (--len == 0) c2 = 0;
		else c2 = (unsigned char)*data++;
		buf[buflen++] = basis_64[((c1 & 0x3)<< 4) | ((c2 & 0xF0) >> 4)];

		if (len == 0) {
			buf[buflen++] = '=';
			buf[buflen++] = '=';
			break;
		}

		if (--len == 0) c3 = 0;
		else c3 = (unsigned char)*data++;

		buf[buflen++] = basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)];
		if (len == 0) {
			buf[buflen++] = '=';

			break;
		}

		--len;
		buf[buflen++] = basis_64[c3 & 0x3F];
	}

	buf[buflen]=0;
	return buf;
}
