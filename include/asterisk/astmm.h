/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Asterisk memory usage debugging
 * This file provides headers for MALLOC_DEBUG, a define used for tracking down
 * memory leaks.  It should never be \#included directly; always use the
 * MALLOC_DEBUG definition in menuselect to activate those functions.
 */


#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASTERISK_ASTMM_H
#define _ASTERISK_ASTMM_H

#ifndef STANDALONE

#define __AST_DEBUG_MALLOC

#include "asterisk.h"

/* Include these now to prevent them from being needed later */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Undefine any macros */
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef strndup
#undef asprintf
#undef vasprintf
#undef free

void *ast_std_malloc(size_t size);
void *ast_std_calloc(size_t nmemb, size_t size);
void *ast_std_realloc(void *ptr, size_t size);
void ast_std_free(void *ptr);
void ast_free_ptr(void *ptr);

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func);
void *__ast_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func);
void *__ast_malloc(size_t size, const char *file, int lineno, const char *func);
void __ast_free(void *ptr, const char *file, int lineno, const char *func);
void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func);
char *__ast_strdup(const char *s, const char *file, int lineno, const char *func);
char *__ast_strndup(const char *s, size_t n, const char *file, int lineno, const char *func);
int __ast_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
	__attribute__((format(printf, 5, 6)));
int __ast_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
	__attribute__((format(printf, 2, 0)));
void __ast_mm_init_phase_1(void);
void __ast_mm_init_phase_2(void);

/* Redefine libc malloc to our own versions */

#ifdef WRAP_LIBC_MALLOC
#define calloc(a,b) \
	__ast_calloc(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define malloc(a) \
	__ast_malloc(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define free(a) \
	__ast_free(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define realloc(a,b) \
	__ast_realloc(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define strdup(a) \
	__ast_strdup(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define strndup(a,b) \
	__ast_strndup(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define asprintf(a, b, c...) \
	__ast_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, a, b, c)
#define vasprintf(a,b,c) \
	__ast_vasprintf(a,b,c,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#else
#define calloc(a,b) \
	Do_not_use_calloc__use_ast_calloc(a,b)
#define malloc(a) \
	Do_not_use_malloc__use_ast_malloc(a)
#define free(a) \
	Do_not_use_free__use_ast_free_or_ast_std_free_for_remotely_allocated_memory(a)
#define realloc(a,b) \
	Do_not_use_realloc__use_ast_realloc(a,b)
#define strdup(a) \
	Do_not_use_strdup__use_ast_strdup(a)
#define strndup(a,b) \
	Do_not_use_strndup__use_ast_strndup(a,b)
#define asprintf(a, b, c...) \
	Do_not_use_asprintf__use_ast_asprintf(a,b,c)
#define vasprintf(a,b,c) \
	Do_not_use_vasprintf__use_ast_vasprintf(a,b,c)
#endif

/* Provide our own definitions */

#define ast_calloc(a,b) \
	__ast_calloc(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_calloc_cache(a,b) \
	__ast_calloc_cache(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_malloc(a) \
	__ast_malloc(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_free(a) \
	__ast_free(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_realloc(a,b) \
	__ast_realloc(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_strdup(a) \
	__ast_strdup(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_strndup(a,b) \
	__ast_strndup(a,b,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_asprintf(a, b, c...) \
	__ast_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, a, b, c)

#define ast_vasprintf(a,b,c) \
	__ast_vasprintf(a,b,c,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#endif /* !STANDALONE */

#else
#error "NEVER INCLUDE astmm.h DIRECTLY!!"
#endif /* _ASTERISK_ASTMM_H */

#ifdef __cplusplus
}
#endif
