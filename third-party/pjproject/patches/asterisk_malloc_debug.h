/*
 * Copyright (C) 2016 George Joseph <gjoseph@digium.com>
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

#ifndef ASTERISK_MALLOC_DEBUG_H_
#define ASTERISK_MALLOC_DEBUG_H_

/* Include these now to prevent them from messing up MALLOC_DEBUG */
#include <sys/types.h>
#include <pj/compat/string.h>
#include <pj/compat/stdarg.h>
#include <pj/compat/malloc.h>

#ifdef __cplusplus
extern "C" {
#endif

int __ast_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
	__attribute__((format(printf, 5, 6)));
void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func);
void __ast_free(void *ptr, const char *file, int lineno, const char *func);
void *__ast_malloc(size_t size, const char *file, int lineno, const char *func);
void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func);
char *__ast_strdup(const char *s, const char *file, int lineno, const char *func);
char *__ast_strndup(const char *s, size_t n, const char *file, int lineno, const char *func);
int __ast_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
	__attribute__((format(printf, 2, 0)));

/* Undefine any macros */
#undef asprintf
#undef calloc
#undef free
#undef malloc
#undef realloc
#undef strdup
#undef strndup
#undef vasprintf

 /* Provide our own definitions */
#define asprintf(a, b, c...) \
	__ast_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, a, b, c)

#define calloc(a,b) \
	__ast_calloc(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define free(a) \
	__ast_free(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define malloc(a) \
	__ast_malloc(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define realloc(a,b) \
	__ast_realloc(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define strdup(a) \
	__ast_strdup(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define strndup(a,b) \
	__ast_strndup(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define vasprintf(a,b,c) \
	__ast_vasprintf(a,b,c,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#ifdef __cplusplus
}
#endif

#endif /* ASTERISK_MALLOC_DEBUG_H_ */
