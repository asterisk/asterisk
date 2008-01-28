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
#include <assert.h>

#include "mm_internal.h"

/** @file mm_context.c
 *
 * Modules for manipulating MiniMIME contexts
 */

/** @defgroup context Accessing and manipulating MIME contexts 
 *
 * Each message in MiniMIME is represented by a so called ``context''. A
 * context holds all necessary information given about a MIME message, such
 * as the envelope, all MIME parts etc.
 */

/** @{
 * @name Manipulating MiniMIME contexts
 */

/**
 * Creates a new MiniMIME context object. 
 *
 * @return a new MiniMIME context object
 * @see mm_context_free
 *
 * This function creates a new MiniMIME context, which will hold a message.
 * The memory needed is allocated dynamically and should later be free'd
 * using mm_context_free().
 *
 * Before a context can be created, the MiniMIME library needs to be
 * initialized properly using mm_library_init().
 *
 */
MM_CTX *
mm_context_new(void)
{
	MM_CTX *ctx;

	MM_ISINIT();

	ctx = (MM_CTX *)xmalloc(sizeof(MM_CTX));
	ctx->messagetype = MM_MSGTYPE_FLAT; /* This is the default */
	ctx->boundary = NULL;
	ctx->preamble = xstrdup("This is a message in MIME format, generated "
	    "by MiniMIME 0.1");

	TAILQ_INIT(&ctx->parts);
	SLIST_INIT(&ctx->warnings);

	return ctx;
}

/**
 * Releases a MiniMIME context object
 *
 * @param ctx A valid MiniMIME context
 * @see mm_context_new
 *
 * This function releases all memory associated with MiniMIME context object
 * that was created using mm_context_new(). It will also release all memory
 * used for the MIME parts attached, and their specific properties (such as
 * Content-Type information, headers, and the body data).
 */
void
mm_context_free(MM_CTX *ctx)
{
	struct mm_mimepart *part;
	struct mm_warning *warning, *nxt;
	
	assert(ctx != NULL);

	TAILQ_FOREACH(part, &ctx->parts, next) {
		TAILQ_REMOVE(&ctx->parts, part, next);
		mm_mimepart_free(part);
	}

	if (ctx->boundary != NULL) {
		xfree(ctx->boundary);
		ctx->boundary = NULL;
	}

	if (ctx->preamble != NULL) {
		xfree(ctx->preamble);
		ctx->preamble = NULL;
	}

	for (warning = SLIST_FIRST(&ctx->warnings); 
	    warning != SLIST_END(&ctx->warnings);
	    warning = nxt) {
		nxt = SLIST_NEXT(warning, next);
		SLIST_REMOVE(&ctx->warnings, warning, mm_warning, next);
		xfree(warning);
		warning = NULL;
	}

	xfree(ctx);
	ctx = NULL;
}

/**
 * Attaches a MIME part object to a MiniMIME context.
 *
 * @param ctx the MiniMIME context
 * @param part the MIME part object to attach
 * @return 0 on success or -1 on failure. Sets mm_errno on failure.
 *
 * This function attaches a MIME part to a context, appending it to the end
 * of the message. 
 *
 * The MIME part should be initialized before attaching it using 
 * mm_mimepart_new().
 */
int
mm_context_attachpart(MM_CTX *ctx, struct mm_mimepart *part)
{
	assert(ctx != NULL);
	assert(part != NULL);
	
	if (TAILQ_EMPTY(&ctx->parts)) {
		TAILQ_INSERT_HEAD(&ctx->parts, part, next);
	} else {
		TAILQ_INSERT_TAIL(&ctx->parts, part, next);
	}

	return 0;
}

/**
 * Attaches a MIME part object to a MiniMIME context at a given position
 *
 * @param ctx A valid MiniMIME context
 * @param part The MIME part object to attach
 * @param pos After which part to attach the object
 * @return 0 on success or -1 if the given position is invalid
 * @see mm_context_attachpart
 *
 * This function attaches a MIME part object after a given position in the
 * specified context. If the position is invalid (out of range), the part
 * will not get attached to the message and the function returns -1. If
 * the index was in range, the MIME part will get attached after the MIME
 * part at the given position, moving any possible following MIME parts one
 * down the hierarchy.
 */
#if 0
int
mm_context_attachpart_after(MM_CTX *ctx, struct mm_mimepart *part, int pos)
{
	struct mm_mimepart *p;
	int where;

	where = 0;
	p = NULL;

	TAILQ_FOREACH(part, &ctx->parts, next) {
		if (where == pos) {
			p = part;
		}	
	}

	if (p == NULL) {
		return(-1);
	}

	TAILQ_INSERT_AFTER(&ctx->parts, p, part, next);

	return(0);
}
#endif

/**
 * Deletes a MIME part object from a MiniMIME context
 *
 * @param ctx A valid MiniMIME context object
 * @param which The number of the MIME part object to delete
 * @param freemem Whether to free the memory associated with the MIME part
 *        object
 * @return 0 on success or -1 on failure. Sets mm_errno on failure.
 *
 * This function deletes a MIME part from a given context. The MIME part to
 * delete is specified as numerical index by the parameter ``which''. If the
 * parameter ``freemem'' is set to anything greater than 0, the memory that
 * is associated will be free'd by using mm_mimepart_free(), otherwise the
 * memory is left untouched (if you still have a pointer to the MIME part
 * around).
 */
int
mm_context_deletepart(MM_CTX *ctx, int which, int freemem)
{
	struct mm_mimepart *part;
	int cur;

	assert(ctx != NULL);
	assert(which >= 0);

	cur = 0;

	TAILQ_FOREACH(part, &ctx->parts, next) {
		if (cur == which) {
			TAILQ_REMOVE(&ctx->parts, part, next);
			if (freemem)
				mm_mimepart_free(part);
			return 0;
		}
		cur++;
	}

	return -1;
}

/**
 * Counts the number of attached MIME part objects in a given MiniMIME context
 *
 * @param ctx The MiniMIME context
 * @returns The number of attached MIME part objects
 */
int
mm_context_countparts(MM_CTX *ctx)
{
	int count;
	struct mm_mimepart *part;
	
	assert(ctx != NULL);

	count = 0;

	if (TAILQ_EMPTY(&ctx->parts)) {
		return 0;
	} else {
		TAILQ_FOREACH(part, &ctx->parts, next) {
			count++;
		}
	}

	assert(count > -1);

	return count;
}

/**
 * Gets a specified MIME part object from a MimeMIME context
 *
 * @param ctx The MiniMIME context
 * @param which The number of the MIME part object to retrieve
 * @returns The requested MIME part object on success or a NULL pointer if
 *          there is no such part.
 */
struct mm_mimepart *
mm_context_getpart(MM_CTX *ctx, int which)
{
	struct mm_mimepart *part;
	int cur;
	
	assert(ctx != NULL);

	cur = 0;
	
	TAILQ_FOREACH(part, &ctx->parts, next) {
		if (cur == which) {
			return part;
		}
		cur++;
	}

	return NULL;
}

/**
 * Checks whether a given context represents a composite (multipart) message
 *
 * @param ctx A valid MiniMIME context object
 * @return 1 if the context is a composite message or 0 if it's flat
 *
 */
int
mm_context_iscomposite(MM_CTX *ctx)
{
	if (ctx->messagetype == MM_MSGTYPE_MULTIPART) {
		return 1;
	} else {
		return 0;
	}
}

/**
 * Checks whether there are any warnings associated with a given context
 *
 * @param ctx A valid MiniMIME context
 * @return 1 if there are warnings associated with the context, otherwise 0
 */
int
mm_context_haswarnings(MM_CTX *ctx)
{
	if (SLIST_EMPTY(&ctx->warnings)) {
		return 0;
	} else {
		return 1;
	}
}

/**
 * Generates a generic boundary string for a given context
 *
 * @param ctx A valid MiniMIME context
 * @return 0 on success or -1 on failure
 *
 * This function generates a default boundary string for the given context.
 * If there is already a boundary for the context, the memory will be free()'d.
 */
#if 0
int
mm_context_generateboundary(MM_CTX *ctx)
{
	char *boundary;
	struct mm_mimepart *part;
	struct mm_param *param;
	
	if (mm_mimeutil_genboundary("++MiniMIME++", 20, &boundary) == -1) {
		return(-1);
	}	

	if (ctx->boundary != NULL) {
		xfree(ctx->boundary);
		ctx->boundary = NULL;
	}
	
	/* If we already have an envelope, make sure that we also justify the
	 * "boundary" parameter of the envelope.
	 */
	part = mm_context_getpart(ctx, 0);
	if (part == NULL) {
		return(0);
	}
	if (part->type != NULL) {
		param = mm_content_gettypeparamobjbyname(part->type, "boundary");
		if (param == NULL) {
			param = mm_param_new();
			param->name = xstrdup("boundary");
			param->value = xstrdup(boundary);
			mm_content_attachtypeparam(part->type, param);
		} else {
			if (param->value != NULL) {
				xfree(param->value);
				param->value = NULL;
			}
			param->value = xstrdup(boundary);
		}	
	}

	ctx->boundary = boundary;
	return(0);
}
#endif

/**
 * Sets a preamble for the given MiniMIME context
 *
 * @param ctx A valid MiniMIME context
 * @param preamble The preamble to set
 * @return 0 on success or -1 on failure
 *
 * This function sets the MIME preamble (the text between the end of envelope
 * headers and the beginning of the first MIME part) for a given context
 * object. If preamble is a NULL-pointer then the preamble will be deleted,
 * and the currently associated memory will be free automagically.
 */
#if 0
int
mm_context_setpreamble(MM_CTX *ctx, char *preamble)
{
	if (ctx == NULL)
		return(-1);

	if (preamble == NULL) {
		if (ctx->preamble != NULL) {
			xfree(ctx->preamble);
		}
		ctx->preamble = NULL;
	} else {	
		ctx->preamble = xstrdup(preamble);
	}	
	return(0);
}
#endif

#if 0
char *
mm_context_getpreamble(MM_CTX *ctx)
{
	if (ctx == NULL)
		return(NULL);

	return(ctx->preamble);	
}
#endif

/**
 * Creates an ASCII message of the specified context
 *
 * @param ctx A valid MiniMIME context object
 * @param flat Where to store the message
 * @param flags Flags that affect the flattening process
 *
 * This function ``flattens'' a MiniMIME context, that is, it creates an ASCII
 * represantation of the message the context contains. The flags can be a
 * bitwise combination of the following constants:
 *
 * - MM_FLATTEN_OPAQUE : use opaque MIME parts when flattening
 * - MM_FLATTEN_SKIPENVELOPE : do not flatten the envelope part
 *
 * Great care is taken to not produce invalid MIME output.
 */
#if 0
int
mm_context_flatten(MM_CTX *ctx, char **flat, size_t *length, int flags)
{
	struct mm_mimepart *part;
	char *message;
	char *flatpart;
	char *buf;
	char *envelope_headers;
	size_t message_size;
	size_t tmp_size;
	char envelope;

	mm_errno = MM_ERROR_NONE;
	envelope = 1;

	message = NULL;
	message_size = 0;

	if (ctx->boundary == NULL) {
		if (mm_context_iscomposite(ctx)) {
			mm_context_generateboundary(ctx);
		}
	}

	TAILQ_FOREACH(part, &ctx->parts, next) {
		if (envelope) {
			if (flags & MM_FLATTEN_SKIPENVELOPE) {
				envelope = 0;
				if ((message = (char *) malloc(1)) == NULL) {
					mm_errno = MM_ERROR_ERRNO;
					goto cleanup;
				}
				*message = '\0';
				continue;
			}
	
			if (part->type == NULL && mm_context_countparts(ctx) > 1) {
				if (mm_mimepart_setdefaultcontenttype(part, 1) 
				    == -1) {
					goto cleanup;
				}	
				if (mm_context_generateboundary(ctx) == -1) {
					goto cleanup;
				}	
				ctx->messagetype = MM_MSGTYPE_MULTIPART;
			}
			
			if (mm_envelope_getheaders(ctx, &envelope_headers,
			    &tmp_size) == -1) {
			    	return -1;
			}
			
			message = envelope_headers;
			message_size = tmp_size;
			envelope = 0;

			if (ctx->preamble != NULL 
			    && mm_context_iscomposite(ctx) 
			    && !(flags & MM_FLATTEN_NOPREAMBLE)) {
				tmp_size += strlen(ctx->preamble) 
				    + (strlen("\r\n") * 2);
				buf = (char *)xrealloc(message, tmp_size);
				if (buf == NULL) {
					goto cleanup;
				}
				message_size += tmp_size;
				message = buf;
				strlcat(message, "\r\n", message_size);
				strlcat(message, ctx->preamble, message_size);
				strlcat(message, "\r\n", message_size);
			}
		} else {
			/* Enforce Content-Type if none exist */
			if (part->type == NULL) {
				if (mm_mimepart_setdefaultcontenttype(part, 0) 
				    == -1) {
					goto cleanup;
				}	
			}

			/* Append a boundary if necessary */
			if (ctx->boundary != NULL) {
				tmp_size = strlen(ctx->boundary) + 
				    (strlen("\r\n") * 2) + strlen("--");

				if (tmp_size < 1) {
					return(-1);
				}	
				if (message_size + tmp_size < 1) {
					return(-1);
				}

				buf = (char *)xrealloc(message, message_size
				    + tmp_size);
				if (buf == NULL) {
					goto cleanup;
				}
				message_size += tmp_size;
				message = buf;
				strlcat(message, "\r\n", message_size);
				strlcat(message, "--", message_size);
				strlcat(message, ctx->boundary, message_size);
				strlcat(message, "\r\n", message_size);
			}

			if (mm_mimepart_flatten(part, &flatpart, &tmp_size, 
			    (flags & MM_FLATTEN_OPAQUE)) == -1) {
				goto cleanup;
			}
			
			if (tmp_size < 1) {
				goto cleanup;
			}
			
			buf = (char *) xrealloc(message, message_size 
			    + tmp_size);
			if (buf == NULL) {
				goto cleanup;
			}
			
			message_size += tmp_size;
			message = buf;
			
			strlcat(message, flatpart, message_size);
			xfree(flatpart);
			flatpart = NULL;
		}	
	}
	
	/* Append end boundary */
	if (ctx->boundary != NULL && mm_context_iscomposite(ctx)) {
		tmp_size = strlen(ctx->boundary) + (strlen("\r\n") * 2) 
		    + (strlen("--") * 2);
		buf = (char *)xrealloc(message, message_size + tmp_size);
		if (buf == NULL) {
			goto cleanup;
		}
		
		message_size += tmp_size;
		message = buf;
		if (message[strlen(message)-1] != 13)
			strlcat(message, "\r", message_size);
		strlcat(message, "\n", message_size);
		strlcat(message, "--", message_size);
		strlcat(message, ctx->boundary, message_size);
		strlcat(message, "--", message_size);
		strlcat(message, "\r\n", message_size);
	}

	*flat = message;
	*length = message_size;

	return 0;

cleanup:
	if (message != NULL) {
		xfree(message);
		message = NULL;
	}	
	return -1;
}
#endif

/** @} */
