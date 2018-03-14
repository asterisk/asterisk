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

#ifndef attribute_malloc
#ifdef HAVE_ATTRIBUTE_malloc
/* HAVE_ATTRIBUTE_malloc is never defined from pjproject.  This is here as a placeholder
 * hopefully we can just use __attribute__((malloc)) unconditionally. */
#define attribute_malloc __attribute__((malloc))
#else
#define attribute_malloc
#endif
#endif

int __ast_repl_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
	__attribute__((format(printf, 5, 6)));
void *__ast_repl_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void __ast_free(void *ptr, const char *file, int lineno, const char *func);
void *__ast_repl_malloc(size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void *__ast_repl_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func);
char *__ast_repl_strdup(const char *s, const char *file, int lineno, const char *func) attribute_malloc;
char *__ast_repl_strndup(const char *s, size_t n, const char *file, int lineno, const char *func) attribute_malloc;
int __ast_repl_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
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
#define asprintf(strp, format, args...) \
	__ast_repl_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, strp, format, args)

#define calloc(nmemb, size) \
	__ast_repl_calloc(nmemb, size, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define free(ptr) \
	__ast_free(ptr, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define malloc(size) \
	__ast_repl_malloc(size, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define realloc(ptr, size) \
	__ast_repl_realloc(ptr, size, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define strdup(s) \
	__ast_repl_strdup(s, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define strndup(s, n) \
	__ast_repl_strndup(s, n, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define vasprintf(strp, format, ap) \
	__ast_repl_vasprintf(strp, format, ap, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#ifdef __cplusplus
}
#endif

#endif /* ASTERISK_MALLOC_DEBUG_H_ */
