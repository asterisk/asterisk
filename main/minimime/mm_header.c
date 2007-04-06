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

/** @file mm_header.c
 *
 * This module contains functions for manipulating MIME headers
 */

/**
 * Creates a new MIME header object
 *
 * @return A new and initialized MIME header object
 * @see mm_mimeheader_free
 *
 * This function creates and initializes a new MIME header object, which must
 * later be freed using mm_mimeheader_free()
 */
struct mm_mimeheader *
mm_mimeheader_new(void)
{
	struct mm_mimeheader *header;

	header = (struct mm_mimeheader *)xmalloc(sizeof(struct mm_mimeheader));
	
	header->name = NULL;
	header->value = NULL;
	TAILQ_INIT(&header->params);

	return header;
}

/**
 * Frees a MIME header object
 *
 * @param header The MIME header object which to free
 */
void
mm_mimeheader_free(struct mm_mimeheader *header)
{
	struct mm_param *param;
	assert(header != NULL);

	if (header->name != NULL) {
		xfree(header->name);
		header->name = NULL;
	}
	if (header->value != NULL) {
		xfree(header->value);
		header->value = NULL;
	}

	TAILQ_FOREACH(param, &header->params, next) {
		TAILQ_REMOVE(&header->params, param, next);
		mm_param_free(param);
	}	
xfree(header);
	header = NULL;
}

/**
 * Creates a new MIME header, but does no checks whatsoever (create as-is)
 */
struct mm_mimeheader *
mm_mimeheader_generate(const char *name, const char *value)
{
	struct mm_mimeheader *header;

	header = mm_mimeheader_new();

	header->name = xstrdup(name);
	header->value = xstrdup(value);

	return header;
}

/**
 * Attaches a parameter to a MimeHeader object
 *
 * @param hdr The target MimeHeader object
 * @param param The parameter to attach
 * @return 0 on success and -1 on failure
 * @ingroup mimeheader
 */
int
mm_mimeheader_attachparam(struct mm_mimeheader *hdr, struct mm_param *param)
{
	assert(hdr != NULL);
	assert(param != NULL);

	if (TAILQ_EMPTY(&hdr->params)) {
		TAILQ_INSERT_HEAD(&hdr->params, param, next);
	} else {
		TAILQ_INSERT_TAIL(&hdr->params, param, next);
	}

	return 0;
}		


/**
 * Gets a parameter value from a MimeHeader object.
 *
 * @param hdr the MimeHeader object
 * @param name the name of the parameter to retrieve
 * @return The value of the parameter on success or a NULL pointer on failure
 * @ingroup mimeheader
 */
char *
mm_mimeheader_getparambyname(struct mm_mimeheader *hdr, const char *name)
{
	struct mm_param *param;

	assert(hdr != NULL);
	
	TAILQ_FOREACH(param, &hdr->params, next) {
		if (!strcasecmp(param->name, name)) {
			return param->value;
		}
	}

	return NULL;
}

int
mm_mimeheader_uncomment(struct mm_mimeheader *header)
{
	char *new;

	assert(header != NULL);
	assert(header->name != NULL);
	assert(header->value != NULL);

	new = mm_uncomment(header->value);
	if (new == NULL)
		return -1;

	xfree(header->value);
	header->value = new;

	return 0;
}

int
mm_mimeheader_uncommentbyname(struct mm_mimepart *part, const char *name)
{
	struct mm_mimeheader *header;

	TAILQ_FOREACH(header, &part->headers, next) {
		if (!strcasecmp(header->name, name)) {
			return mm_mimeheader_uncomment(header);
		}
	}

	/* Not found */
	return -1;
}

int
mm_mimeheader_uncommentall(struct mm_mimepart *part)
{
	struct mm_mimeheader *header;
	int ret, r;

	ret = 0;

	TAILQ_FOREACH(header, &part->headers, next) {
		if ((r = mm_mimeheader_uncomment(header)) == -1) {
			ret = -1;
		}
	}

	return ret;
}
