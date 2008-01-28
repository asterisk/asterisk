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
#include "mm_util.h"

/** @file mm_envelope.c
 *
 * This module contains functions for accessing a message's envelope. This
 * are mainly wrapper functions for easy access.
 */

/** @defgroup envelope Accessing and manipulating a message's envelope
 */

/** @{
 * @name Accessing and manipulating a message's envelope
 */

/**
 * Gets an ASCII representation of all envelope headers
 *
 * @param ctx A valid MiniMIME context
 * @param result Where to store the resulting ASCII headers
 * @param length Where to store the length of the result
 * @returns 0 on success or -1 on failure.
 * @note Sets mm_errno on failure
 *
 * This is mainly a convinience function. It constructs an ASCII representation
 * from all of the message's envelope headers and stores the result in headers.
 * Memory is allocated dynamically, and the total length of the result is
 * stored in length. This function takes care that the output is MIME conform,
 * and folds long lines according to the MIME standard at position 78 of the
 * string. It also nicely formats all MIME related header fields, such as
 * the Content-Type header.
 *
 * Since the memory needed to store the result is allocated dynamically, one
 * should take care of freeing it again when it's not needed anymore. If an
 * error occurs, *result will be set to NULL, *length will be set to zero
 * and mm_errno will be set to a reasonable value.
 *
 */
int
mm_envelope_getheaders(MM_CTX *ctx, char **result, size_t *length)
{
	struct mm_mimepart *part;
	struct mm_mimeheader *hdr;
	char *buf, *hdrbuf;
	size_t headers_length, tmp_length;

	headers_length = 1;
	buf = NULL;

	part = mm_context_getpart(ctx, 0);
	if (part == NULL) {
		return -1;
	}	

	/* Initialize our buffer */
	if ((buf = (char *)xmalloc(headers_length)) == NULL) {
		mm_errno = MM_ERROR_ERRNO;
		goto cleanup;
	}	
	*buf = '\0';

	/* Store each envelope header */
	TAILQ_FOREACH(hdr, &part->headers, next) {
		tmp_length = strlen(hdr->name) + strlen(hdr->value) 
		    + strlen(": \r\n");
		hdrbuf = (char *) xrealloc(buf, headers_length + tmp_length);
		if (hdrbuf == NULL) {
			mm_errno = MM_ERROR_ERRNO;
			goto cleanup;
		}

		headers_length += tmp_length;
		buf = hdrbuf;

		strlcat(buf, hdr->name, headers_length);
		strlcat(buf, ": ", headers_length);
		strlcat(buf, hdr->value, headers_length);
		strlcat(buf, "\r\n", headers_length);
	}

	/* Construct and store MIME headers */
	if (part->type != NULL) {
		char *typebuf;
		typebuf = mm_content_tostring(part->type);
		if (typebuf == NULL) {
			goto cleanup;
		}
		tmp_length = strlen(typebuf) + strlen("\r\n");

		hdrbuf = (char *) xrealloc(buf, headers_length + tmp_length);
		if (hdrbuf == NULL) {
			mm_errno = MM_ERROR_ERRNO;
			goto cleanup;
		}

		headers_length += tmp_length;
		buf = hdrbuf;
		
		strlcat(buf, typebuf, headers_length);
		strlcat(buf, "\r\n", headers_length);
	}

	*result = buf;
	*length = headers_length;

	return 0;

cleanup:
	if (buf != NULL) {
		xfree(buf);
		buf = NULL;
	}
	*result = NULL;
	*length = 0;
	return -1;
}

/**
 * Sets a header field in the envelope
 *
 * @param ctx A valid MiniMIME context
 * @param name The name of the header field to set
 * @param fmt A format string specifying the value of the header field
 * @return 0 on success or -1 on failure
 *
 * This function generates a new MIME header and attaches it to the first
 * MIME part (the envelope) found in the given context. If no part is
 * attached already, the function will return an error. The function will
 * store a copy of ``name'' as the header's name field, and dynamically
 * allocate the memory needed to build the format string.
 */
int
mm_envelope_setheader(MM_CTX *ctx, const char *name, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	struct mm_mimeheader *hdr;
	struct mm_mimepart *part;

	part = mm_context_getpart(ctx, 0);
	if (part == NULL) {
		return(-1);
	}	

	hdr = mm_mimeheader_new();
	if (hdr == NULL) {
		return(-1);
	}

	hdr->name = xstrdup(name);

	va_start(ap, fmt);
	if (vasprintf(&buf, fmt, ap) == -1) {
		goto cleanup;
	}	
	va_end(ap);

	hdr->value = buf;

	if (mm_mimepart_attachheader(part, hdr) == -1) {
		goto cleanup;
	}	

	return(0);

cleanup:
	if (hdr != NULL) {
		if (hdr->name != NULL) {
			xfree(hdr->name);
			hdr->name = NULL;
		}
		if (hdr->value != NULL) {
			xfree(hdr->value);
			hdr->value = NULL;
		}
	}	
	return(-1);
}

/**
 * Gets the list of recipients for a MIME message
 *
 * @param ctx A valid MiniMIME context
 * @param result Where to store the result
 * @param length Where to store the length of the result
 * @returns 0 on success or -1 on error
 * @note Sets mm_errno on error
 *
 * This functions gets the list of recipients for a given MIME message. It
 * does so by concatenating the "From" and "Cc" header fields, and storing
 * the results in recipients. The memory needed to store the result is
 * allocated dynamically, and the total length of the result is stored in
 * length.
 *
 * One should take care to free() the result once it's not needed anymore.
 */
#if 0
int
mm_envelope_getrecipients(MM_CTX *ctx, char **result, size_t *length)
{
	struct mm_mimepart *part;
	struct mm_mimeheader *to, *cc;
	size_t recipients_length = 0;

	part = mm_context_getpart(ctx, 0);
	if (part == NULL) {
		return -1;
	}

	to = mm_mimepart_getheaderbyname(part, "From", 0);
	cc = mm_mimepart_getheaderbyname(part, "Cc", 0);

	if (to == NULL || cc == NULL) {
		*result = NULL;
		*length = 0;
		return -1;
	}

	if (to != NULL) {
		recipients_length += strlen(to->value);
	}	
	if (cc != NULL) {
		recipients_length += strlen(cc->value);
	}	
	
	return 0;
}
#endif

/** @} */
