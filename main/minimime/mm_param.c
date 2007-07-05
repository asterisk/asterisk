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

/**
 * @file mm_param.c
 *
 * Functions to manipulate MIME parameters
 */

/** @defgroup param Accessing and manipulating MIME parameters */

/** @{ 
 *
 * @name Functions for manipulating MIME parameters
 *
 * MIME parameters are properties attached to certain MIME headers, such as
 * Content-Type and Content-Disposition. MIME parameters have a textual
 * representations as in <i>name=value</i>. They contain important information
 * about the MIME structure of a message, such as the boundary string used,
 * which charset was used to encode the message and so on. This module
 * provides simple to use functions to query or set MIME parameters.
 *
 * Each MIME header may hold an arbitrary amount of such parameters, which
 * are delimeted by each other with a semicolon.
 */

/**
 * Creates a new object to hold a MIME parameter. 
 *
 * @return An object representing a MIME parameter
 * @see mm_param_free
 * @note The allocated memory must later be freed using mm_param_free()
 */
struct mm_param *
mm_param_new(void)
{
	struct mm_param *param;

	param = (struct mm_param *)xmalloc(sizeof(struct mm_param));
	
	param->name = NULL;
	param->value = NULL;

	return param;
}

/**
 * Releases all memory associated with a MIME parameter object.
 *
 * @param param A valid MIME parameter object to be freed
 * @return Nothing
 * @see mm_param_new
 */
void
mm_param_free(struct mm_param *param)
{
	assert(param != NULL);

	if (param->name != NULL) {
		xfree(param->name);
		param->name = NULL;
	}
	if (param->value != NULL) {
		xfree(param->value);
		param->value = NULL;
	}
	xfree(param);
}

/**
 * Generates a new Content-Type parameter with the given name and value
 *
 * @param name The name of the MIME parameter
 * @param value The value of the MIME parameter
 * @returns A new MIME parameter object
 * @see mm_param_free
 * @see mm_param_new
 *
 * This function generates a new MIME parameter, with the name
 * and value given as the arguments. The needed memory for the operation
 * is allocated dynamically. It stores a copy of name and value in the
 * actual object, so the memory holding the arguments can safely be
 * freed after successfull return of this function.
 */
#if 0
struct mm_param *
mm_param_generate(const char *name, const char *value)
{
	struct mm_param *param;

	param = mm_param_new();

	param->name = xstrdup(name);
	param->value = xstrdup(value);
	
	return param;
}
#endif

/**
 * Sets the name of the given MIME parameter
 *
 * @param param A valid MIME parameter object
 * @param name The new name of the parameter
 * @param copy If set to > 0, copy the value stored in name
 * @returns The address of the previous name for passing to free()
 */
#if 0
char *
mm_param_setname(struct mm_param *param, const char *name, int copy)
{
	char *retadr;
	assert(param != NULL);

	retadr = param->name;

	if (copy)
		param->name = xstrdup(name);
	else
		param->name = (char *)name;

	return retadr;	
}
#endif

/**
 * Sets the value of the given MIME parameter
 *
 * @param param A valid MIME parameter object
 * @param name The new value for the parameter
 * @param copy If set to > 0, copy the value stored in value
 * @returns The address of the previous value for passing to free()
 */
#if 0
char *
mm_param_setvalue(struct mm_param *param, const char *value, int copy)
{
	char *retadr;
	assert(param != NULL);

	retadr = param->value;

	if (copy)
		param->value = xstrdup(value);
	else
		param->value = (char *)value;

	return retadr;	
}
#endif

/**
 * Gets the name of a MIME parameter object
 *
 * @param param A valid MIME parameter object
 * @returns The name of the MIME parameter
 */
#if 0
const char *
mm_param_getname(struct mm_param *param)
{
	assert(param != NULL);
	return param->name;
}
#endif

/**
 * Gets the value of a MIME parameter object
 *
 * @param param A valid MIME parameter object
 * @returns The value of the MIME parameter
 */
#if 0
const char *
mm_param_getvalue(struct mm_param *param)
{
	assert(param != NULL);
	return param->value;
}
#endif

/** @} */
