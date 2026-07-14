/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc
 *
 * George Joseph <gjoseph@digium.com>
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

int __ast_repl_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
{
	va_list ap;
	int rc = 0;

	va_start(ap, format);
	rc = vasprintf(strp, format, ap);
	va_end(ap);

	return rc;
}

void *__ast_repl_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	return calloc(nmemb, size);
}

void __ast_free(void *ptr, const char *file, int lineno, const char *func)
{
	free(ptr);
}

void *__ast_repl_malloc(size_t size, const char *file, int lineno, const char *func)
{
	return malloc(size);
}

void *__ast_repl_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func)
{
	return realloc(ptr, size);
}

char *__ast_repl_strdup(const char *s, const char *file, int lineno, const char *func)
{
	return strdup(s);
}

char *__ast_repl_strndup(const char *s, size_t n, const char *file, int lineno, const char *func)
{
	return strndup(s, n);
}

int __ast_repl_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
{
	return vasprintf(strp, format, ap);
}
