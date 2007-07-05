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

/* This file is documented using Doxygen */

/**
 * @file mm_contenttype.c 
 *
 * This module contains functions for manipulating Content-Type objects.
 */

/** @defgroup contenttype Accessing and manipulating Content-Type objects */

struct mm_encoding_mappings {
	const char *idstring;
	int type;
};

static struct mm_encoding_mappings mm_content_enctypes[] = {
	{ "Base64", MM_ENCODING_BASE64 },
	{ "Quoted-Printable", MM_ENCODING_QUOTEDPRINTABLE },
	{ NULL, - 1},
};

static const char *mm_composite_maintypes[] = {
	"multipart",
	"message",
	NULL,
};

static const char *mm_composite_encodings[] = {
	"7bit",
	"8bit",
	"binary",
	NULL,
};		

/** @{
 * @name Functions for manipulating Content objects
 */

/**
 * Creates a new object to hold a Content representation.
 * The allocated memory must later be freed using mm_content_free()
 *
 * @return An object representing a MIME Content-Type
 * @see mm_content_free
 * @ingroup contenttype
 */
struct mm_content *
mm_content_new(void)
{
	struct mm_content *ct;

	ct = (struct mm_content *)xmalloc(sizeof(struct mm_content));

	ct->maintype = NULL;
	ct->subtype = NULL;

	TAILQ_INIT(&ct->type_params);
	TAILQ_INIT(&ct->disposition_params);

	ct->encoding = MM_ENCODING_NONE;
	ct->encstring = NULL;

	return ct;
}

/**
 * Releases all memory associated with an Content object
 *
 * @param ct A Content-Type object
 * @return Nothing
 * @ingroup contenttype
 */
void
mm_content_free(struct mm_content *ct)
{
	struct mm_param *param;

	assert(ct != NULL);

	if (ct->maintype != NULL) {
		xfree(ct->maintype);
		ct->maintype = NULL;
	}
	if (ct->subtype != NULL) {
		xfree(ct->subtype);
		ct->subtype = NULL;
	}
	if (ct->encstring != NULL) {
		xfree(ct->encstring);
		ct->encstring = NULL;
	}

	TAILQ_FOREACH(param, &ct->type_params, next) {
		TAILQ_REMOVE(&ct->type_params, param, next);
		mm_param_free(param);
	}	
	TAILQ_FOREACH(param, &ct->disposition_params, next) {
		TAILQ_REMOVE(&ct->disposition_params, param, next);
		mm_param_free(param);
	}	

	xfree(ct);
}

/**
 * Attaches a content-type parameter to a Content object
 *
 * @param ct The target Content object
 * @param param The Content-Type parameter which to attach
 * @return 0 on success and -1 on failure
 * @ingroup contenttype
 */
int
mm_content_attachtypeparam(struct mm_content *ct, struct mm_param *param)
{
	assert(ct != NULL);
	assert(param != NULL);

	if (TAILQ_EMPTY(&ct->type_params)) {
		TAILQ_INSERT_HEAD(&ct->type_params, param, next);
	} else {
		TAILQ_INSERT_TAIL(&ct->type_params, param, next);
	}

	return 0;
}		


/**
 * Attaches a content-disposition parameter to a Content-Disposition object
 *
 * @param ct The target Content object
 * @param param The Content-Type parameter which to attach
 * @return 0 on success and -1 on failure
 * @ingroup contenttype
 */
int
mm_content_attachdispositionparam(struct mm_content *ct, struct mm_param *param)
{
	assert(ct != NULL);
	assert(param != NULL);

	if (TAILQ_EMPTY(&ct->disposition_params)) {
		TAILQ_INSERT_HEAD(&ct->disposition_params, param, next);
	} else {
		TAILQ_INSERT_TAIL(&ct->disposition_params, param, next);
	}

	return 0;
}		


/**
 * Gets a Content-Type parameter value from a Content object.
 *
 * @param ct the Content object
 * @param name the name of the parameter to retrieve
 * @return The value of the parameter on success or a NULL pointer on failure
 * @ingroup contenttype
 */
char *
mm_content_gettypeparambyname(struct mm_content *ct, const char *name)
{
	struct mm_param *param;

	assert(ct != NULL);
	
	TAILQ_FOREACH(param, &ct->type_params, next) {
		if (!strcasecmp(param->name, name)) {
			return param->value;
		}
	}

	return NULL;
}

/**
 * Gets a Content-Disposition parameter value from a Content object.
 *
 * @param ct the Content object
 * @param name the name of the parameter to retrieve
 * @return The value of the parameter on success or a NULL pointer on failure
 * @ingroup contenttype
 */
char *
mm_content_getdispositionparambyname(struct mm_content *ct, const char *name)
{
	struct mm_param *param;

	assert(ct != NULL);
	
	TAILQ_FOREACH(param, &ct->disposition_params, next) {
		if (!strcasecmp(param->name, name)) {
			return param->value;
		}
	}

	return NULL;
}

struct mm_param *
mm_content_gettypeparamobjbyname(struct mm_content *ct, const char *name)
{
	struct mm_param *param;

	assert(ct != NULL);
	
	TAILQ_FOREACH(param, &ct->type_params, next) {
		if (!strcasecmp(param->name, name)) {
			return param;
		}
	}

	return NULL;
}

struct mm_param *
mm_content_getdispositionparamobjbyname(struct mm_content *ct, const char *name)
{
	struct mm_param *param;

	assert(ct != NULL);
	
	TAILQ_FOREACH(param, &ct->disposition_params, next) {
		if (!strcasecmp(param->name, name)) {
			return param;
		}
	}

	return NULL;
}

/**
 * Sets the MIME main Content-Type for a MIME Content object
 *
 * @param ct The MIME Content object
 * @param value The value which to set the main type to
 * @param copy Whether to make a copy of the value (original value must be
 *        freed afterwards to prevent memory leaks).
 */
int
mm_content_setmaintype(struct mm_content *ct, char *value, int copy)
{
	assert(ct != NULL);
	assert(value != NULL);

	if (copy) {
		/**
		 * @bug The xfree() call could lead to undesirable results. 
 		 * Do we really need it?
		 */
		if (ct->maintype != NULL) {
			xfree(ct->maintype);
		}
		ct->maintype = xstrdup(value);
	} else {
		ct->maintype = value;
	}

	return 0;
}

/**
 * Retrieves the main MIME Content-Type stored in a Content object
 *
 * @param ct A valid Content object
 * @returns A pointer to the string representing the main type
 * @ingroup contenttype
 */
char *
mm_content_getmaintype(struct mm_content *ct)
{
	assert(ct != NULL);
	assert(ct->maintype != NULL);

	return ct->maintype;
}

/**
 * Sets the MIME Content-Disposition type for a MIME Content object
 *
 * @param ct The MIME Content object
 * @param value The value which to set the main type to
 * @param copy Whether to make a copy of the value (original value must be
 *        freed afterwards to prevent memory leaks).
 */
int
mm_content_setdispositiontype(struct mm_content *ct, char *value, int copy)
{
	assert(ct != NULL);
	assert(value != NULL);

	if (copy) {
		/**
		 * @bug The xfree() call could lead to undesirable results. 
 		 * Do we really need it?
		 */
		if (ct->disposition_type != NULL) {
			xfree(ct->disposition_type);
		}
		ct->disposition_type = xstrdup(value);
	} else {
		ct->disposition_type = value;
	}

	return 0;
}

/**
 * Retrieves the Content-Disposition MIME type stored in a Content object
 *
 * @param ct A valid Content-Type object
 * @returns A pointer to the string representing the main type
 * @ingroup contenttype
 */
char *
mm_content_getdispositiontype(struct mm_content *ct)
{
	assert(ct != NULL);
	assert(ct->disposition_type != NULL);

	return ct->disposition_type;
}

/**
 * Retrieves the sub MIME Content-Type stored in a Content object
 *
 * @param ct A valid Content-Type object
 * @return A pointer to the string holding the current sub MIME type
 * @ingroup contenttype
 */
char *
mm_content_getsubtype(struct mm_content *ct)
{
	assert(ct != NULL);
	assert(ct->subtype != NULL);

	return ct->subtype;
}

/**
 * Sets the MIME sub Content-Type for a MIME Content object
 *
 * @param ct The MIME Content-Type object
 * @param value The value which to set the sub type to
 * @param copy Whether to make a copy of the value (original value must be
 *        freed afterwards to prevent memory leaks).
 */
int
mm_content_setsubtype(struct mm_content *ct, char *value, int copy)
{
	assert(ct != NULL);
	assert(value != NULL);

	if (copy) {
		/**
		 * @bug The xfree() call could lead to undesirable results. 
 		 * Do we really need it?
		 */
		if (ct->subtype != NULL) {
			xfree(ct->subtype);
		}
		ct->subtype = xstrdup(value);
	} else {
		ct->subtype = value;
	}

	return 0;
}

int
mm_content_settype(struct mm_content *ct, const char *fmt, ...)
{
	char *maint, *subt;
	char buf[512], *parse;
	va_list ap;
	
	mm_errno = MM_ERROR_NONE;
	
	va_start(ap, fmt);
	/* Make sure no truncation occurs */
	if (vsnprintf(buf, sizeof buf, fmt, ap) > sizeof buf) {
		mm_errno = MM_ERROR_ERRNO;
		mm_error_setmsg("Input string too long");
		return -1;
	}
	va_end(ap);

	parse = buf;
	maint = strsep(&parse, "/");
	if (maint == NULL) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("Invalid type specifier: %s", buf);
		return -1;
	}
	ct->maintype = xstrdup(maint);

	subt = strsep(&parse, "");
	if (subt == NULL) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("Invalid type specifier: %s", buf);
		return -1;
	}
	ct->subtype = xstrdup(subt);
	
	return 0;
}

/**
 * Checks whether the Content-Type represents a composite message or not
 *
 * @param ct A valid Content-Type object
 * @returns 1 if the Content-Type object represents a composite message or
 *          0 if not.
 */
int
mm_content_iscomposite(struct mm_content *ct)
{
	int i;

	for (i = 0; mm_composite_maintypes[i] != NULL; i++) {
		if (!strcasecmp(ct->maintype, mm_composite_maintypes[i])) {
			return 1;
		}
	}

	/* Not found */
	return 0;
}

/**
 * Verifies whether a string represents a valid encoding or not.
 *
 * @param encoding The string to verify
 * @return 1 if the encoding string is valid or 0 if not
 *
 */
int
mm_content_isvalidencoding(const char *encoding)
{
	int i;
	
	for (i = 0; mm_composite_encodings[i] != NULL; i++) {
		if (!strcasecmp(encoding, mm_composite_encodings[i])) {
			return 1;
		}
	}

	/* Not found */
	return 0;
}

/**
 * Set the encoding of a MIME entitity according to a mapping table
 *
 * @param ct A valid content type object
 * @param encoding A string representing the content encoding
 * @return 0 if successfull or -1 if not (i.e. unknown content encoding)
 */
int
mm_content_setencoding(struct mm_content *ct, const char *encoding)
{
	int i;

	assert(ct != NULL);
	assert(encoding != NULL);

	for (i = 0; mm_content_enctypes[i].idstring != NULL; i++) {
		if (!strcasecmp(mm_content_enctypes[i].idstring, encoding)) {
			ct->encoding = mm_content_enctypes[i].type;
			ct->encstring = xstrdup(encoding);
			return 0;
		}
	}

	/* If we didn't find a mapping, set the encoding to unknown */
	ct->encoding = MM_ENCODING_UNKNOWN;
	ct->encstring = NULL;
	return 1;
}

/**
 * Gets the numerical ID of a content encoding identifier
 *
 * @param ct A valid Content Type object
 * @param encoding A string representing the content encoding identifier
 * @return The numerical ID of the content encoding
 */ 
#if 0
int
mm_content_getencoding(struct mm_content *ct, const char *encoding)
{
	int i;

	assert(ct != NULL);

	for (i = 0; mm_content_enctypes[i].idstring != NULL; i++) {
		if (!strcasecmp(mm_content_enctypes[i].idstring, encoding)) {
			return mm_content_enctypes[i].type;
		}
	}

	/* Not found */
	return MM_ENCODING_UNKNOWN;
}
#endif

/**
 * Constructs a MIME conform string of Content-Type parameters.
 *
 * @param ct A valid Content Type object
 * @return A pointer to a string representing the Content-Type parameters
 *         in MIME terminology, or NULL if either the Content-Type object
 *         is invalid, has no parameters or no memory could be allocated.
 *
 * This function constructs a MIME conform string including all the parameters
 * associated with the given Content-Type object. It should NOT be used if
 * you need an opaque copy of the current MIME part (e.g. for PGP purposes).
 */
char *
mm_content_typeparamstostring(struct mm_content *ct)
{
	size_t size, new_size;
	struct mm_param *param;
	char *param_string, *cur_param;
	char *buf;

	size = 1;
	param_string = NULL;
	cur_param = NULL;

	param_string = (char *) xmalloc(size);
	*param_string = '\0';

	/* Concatenate all Content-Type parameters attached to the current
	 * Content-Type object to a single string.
	 */
	TAILQ_FOREACH(param, &ct->type_params, next) {
		if (asprintf(&cur_param, "; %s=\"%s\"", param->name, 
		    param->value) == -1) {
			goto cleanup;
		}

		new_size = size + strlen(cur_param) + 1;
		
		if (new_size < 0 || new_size > 1000) {
			size = 0;
			goto cleanup;
		}	

		buf = (char *) xrealloc(param_string, new_size);
		if (buf == NULL) {
			size = 0;
			goto cleanup;
		}

		param_string = buf;
		size = new_size;
		strlcat(param_string, cur_param, size);
		
		xfree(cur_param);
		cur_param = NULL;
	}

	return param_string;

cleanup:
	if (param_string != NULL) {
		xfree(param_string);
		param_string = NULL;
	}
	if (cur_param != NULL) {
		xfree(cur_param);
		cur_param = NULL;
	}	
	return NULL;
}

/**
 * Constructs a MIME conformant string of Content-Disposition parameters.
 *
 * @param ct A valid Content object
 * @return A pointer to a string representing the Content-Disposition parameters
 *         in MIME terminology, or NULL if either the Content object
 *         is invalid, has no Disposition parameters or no memory could be allocated.
 *
 * This function constructs a MIME conforming string including all the parameters
 * associated with the given Content-Disposition object. It should NOT be used if
 * you need an opaque copy of the current MIME part (e.g. for PGP purposes).
 */
char *
mm_content_dispositionparamstostring(struct mm_content *ct)
{
	size_t size, new_size;
	struct mm_param *param;
	char *param_string, *cur_param;
	char *buf;

	size = 1;
	param_string = NULL;
	cur_param = NULL;

	param_string = (char *) xmalloc(size);
	*param_string = '\0';

	/* Concatenate all Content-Disposition parameters attached to the current
	 * Content object to a single string.
	 */
	TAILQ_FOREACH(param, &ct->disposition_params, next) {
		if (asprintf(&cur_param, "; %s=\"%s\"", param->name, 
		    param->value) == -1) {
			goto cleanup;
		}

		new_size = size + strlen(cur_param) + 1;
		
		if (new_size < 0 || new_size > 1000) {
			size = 0;
			goto cleanup;
		}	

		buf = (char *) xrealloc(param_string, new_size);
		if (buf == NULL) {
			size = 0;
			goto cleanup;
		}

		param_string = buf;
		size = new_size;
		strlcat(param_string, cur_param, size);
		
		xfree(cur_param);
		cur_param = NULL;
	}

	return param_string;

cleanup:
	if (param_string != NULL) {
		xfree(param_string);
		param_string = NULL;
	}
	if (cur_param != NULL) {
		xfree(cur_param);
		cur_param = NULL;
	}	
	return NULL;
}

/**
 * Creates a Content-Type header according to the object given
 *
 * @param ct A valid Content-Type object
 *
 */
char *
mm_content_tostring(struct mm_content *ct)
{
	char *paramstring;
	char *buf;
	char *headerstring;
	size_t size;

	paramstring = NULL;
	headerstring = NULL;
	buf = NULL;

	if (ct == NULL) {
		return NULL;
	}	
	if (ct->maintype == NULL || ct->subtype == NULL) {
		return NULL;
	}	

	size = strlen(ct->maintype) + strlen(ct->subtype) + 2;
	headerstring = (char *)xmalloc(size);
	snprintf(headerstring, size, "%s/%s", ct->maintype, ct->subtype);

	paramstring = mm_content_typeparamstostring(ct);
	if (paramstring == NULL) {
		goto cleanup;
	}

	size += strlen(paramstring) + strlen("Content-Type: ") + 1;
	buf = (char *)malloc(size);
	if (buf == NULL) {
		goto cleanup;
	}

	snprintf(buf, size, "Content-Type: %s%s", headerstring, paramstring);

	xfree(headerstring);
	xfree(paramstring);

	headerstring = NULL;
	paramstring = NULL;

	return buf;

cleanup:
	if (paramstring != NULL) {
		xfree(paramstring);
		paramstring = NULL;
	}
	if (headerstring != NULL) {
		xfree(headerstring);
		headerstring = NULL;
	}	
	if (buf != NULL) {
		xfree(buf);
		buf = NULL;
	}	
	return NULL;
}

/** @} */
