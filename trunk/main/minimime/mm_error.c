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
#include <errno.h>

#include "mm_internal.h"
#include "mm_util.h"

/** @file mm_error.c
 *
 * This module contains functions for MiniMIME error information/manipulation
 */

/** @defgroup error MiniMIME error functions */

/**
 * Initializes the global error object 
 *
 * @ingroup error
 *
 * This function initializes the global error object mm_error. This must be
 * done when the library is initialized, and is automatically called from
 * mm_init_library().
 */
void
mm_error_init(void)
{
	mm_error.error_id = 0;
	mm_error.error_where = 0;
	mm_error.lineno = 0;
	memset(&mm_error.error_msg, '\0', sizeof(mm_error.error_msg));
}

/**
 * Sets a descriptive error message
 *
 * @param fmt The error message as format string
 * @ingroup error
 *
 * This function is called from the various MiniMIME modules in case an
 * error occured. Should never be called by the user.
 */
void
mm_error_setmsg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(mm_error.error_msg, sizeof(mm_error.error_msg), fmt, ap);
	va_end(ap);

}

void
mm_error_setlineno(int lineno)
{
	mm_error.lineno = lineno;
}

/**
 * Retrieves the current error message
 *
 * @return The currently set error message
 * @ingroup error
 *
 * This function can be used to retrieve a descriptive error message for the
 * current error, much like strerror() function of libc. When this function
 * is called without an error being set, it returns the string "No error".
 * The string returned does not need to be freed, since it is not dynamically
 * allocated by the library.
 */
char *
mm_error_string(void)
{
	if (mm_errno != MM_ERROR_ERRNO && mm_error.error_msg[0] == '\0') {
		return "No error";
	} else if (mm_errno == MM_ERROR_ERRNO) {
		return strerror(errno);
	} else {
		return mm_error.error_msg;
	}
}

int
mm_error_lineno(void)
{
	return mm_error.lineno;
}
