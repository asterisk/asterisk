/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Compatibility functions for strsep and strtoq missing on Solaris 
 */

#include "autoconfig.h"

#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#ifndef HAVE_STRSEP
char *strsep(char **str, const char *delims)
{
    char *token;

    if (!*str) {
        /* No more tokens */
        return NULL;
    }

    token = *str;
    while (**str != '\0') {
        if (strchr(delims, **str)) {
            **str = '\0';
            (*str)++;
            return token;
        }
        (*str)++;
    }

    /* There is no other token */
    *str = NULL;

    return token;
}
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite)
{
	unsigned char *buf;
	int buflen;

	buflen = strlen(name) + strlen(value) + 2;
	buf = alloca(buflen);

	if (!overwrite && getenv(name))
		return 0;

	snprintf(buf, buflen, "%s=%s", name, value);

	return putenv(buf);
}
#endif

#ifndef HAVE_UNSETENV
int unsetenv(const char *name)
{
	return setenv(name, "", 0);
}
#endif

#ifndef HAVE_STRCASESTR
static char *upper(const char *orig, char *buf, int bufsize)
{
	int i = 0;

	while (i < (bufsize - 1) && orig[i]) {
		buf[i] = toupper(orig[i]);
		i++;
	}

	buf[i] = '\0';

	return buf;
}

char *strcasestr(const char *haystack, const char *needle)
{
	char *u1, *u2;
	int u1len = strlen(haystack) + 1, u2len = strlen(needle) + 1;

	u1 = alloca(u1len);
	u2 = alloca(u2len);
	if (u1 && u2) {
		char *offset;
		if (u2len > u1len) {
			/* Needle bigger than haystack */
			return NULL;
		}
		offset = strstr(upper(haystack, u1, u1len), upper(needle, u2, u2len));
		if (offset) {
			/* Return the offset into the original string */
			return ((char *)((unsigned long)haystack + (unsigned long)(offset - u1)));
		} else {
			return NULL;
		}
	} else {
		return NULL;
	}
}
#endif /* !HAVE_STRCASESTR */

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t n)
{
	size_t len;

	for (len = 0; len < n; len++)
		if (s[len] == '\0')
			break;

	return len;
}
#endif /* !HAVE_STRNLEN */

#if !defined(HAVE_STRNDUP) && !defined(__AST_DEBUG_MALLOC)
char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *new = malloc(len + 1);

	if (!new)
		return NULL;

	new[len] = '\0';
	return memcpy(new, s, len);
}
#endif /* !defined(HAVE_STRNDUP) && !defined(__AST_DEBUG_MALLOC) */

#if !defined(HAVE_VASPRINTF) && !defined(__AST_DEBUG_MALLOC)
int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int size;
	va_list ap2;
	char s;

	*strp = NULL;
	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	*strp = malloc(size + 1);
	if (!*strp)
		return -1;
	vsnprintf(*strp, size + 1, fmt, ap);

	return size;
}
#endif /* !defined(HAVE_VASPRINTF) && !defined(__AST_DEBUG_MALLOC) */

/*
 * Based on Code from bsd-asprintf from OpenSSH
 * Copyright (c) 2004 Darren Tucker.
 *
 * Based originally on asprintf.c from OpenBSD:
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#if !defined(HAVE_ASPRINTF) && !defined(__AST_DEBUG_MALLOC) 
int asprintf(char **str, const char *fmt, ...)
{
        va_list ap;
        int ret;

        *str = NULL;
        va_start(ap, fmt);
        ret = vasprintf(str, fmt, ap);
        va_end(ap);

        return ret;
}
#endif /* !defined(HAVE_ASPRINTF) && !defined(__AST_DEBUG_MALLOC) */

#ifndef HAVE_GETLOADAVG
#ifdef linux
/*! \brief Alternative method of getting load avg on Linux only */
int getloadavg(double *list, int nelem)
{
	FILE *LOADAVG;
	double avg[3] = { 0.0, 0.0, 0.0 };
	int i, res = -1;

	if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
		fscanf(LOADAVG, "%lf %lf %lf", &avg[0], &avg[1], &avg[2]);
		res = 0;
		fclose(LOADAVG);
	}

	for (i = 0; (i < nelem) && (i < 3); i++) {
		list[i] = avg[i];
	}

	return res;
}
#else /* !linux */
/*! \brief Return something that won't cancel the call, but still return -1, in case
 * we correct the implementation to check return value */
int getloadavg(double *list, int nelem)
{
	int i;

	for (i = 0; i < nelem; i++) {
		list[i] = 0.1;
	}
	return -1;
}
#endif /* linux */
#endif /* !HAVE_GETLOADAVG */
