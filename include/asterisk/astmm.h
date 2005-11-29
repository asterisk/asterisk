/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk debugging
 * 
 * Copyright (C) 2002, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef NO_AST_MM
#ifndef _ASTMM_H
#define _ASTMM_H

#define __AST_DEBUG_MALLOC

/* Include these now to prevent them from being needed later */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Undefine any macros */
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef strndup
#undef vasprintf

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func);
void *__ast_malloc(size_t size, const char *file, int lineno, const char *func);
void __ast_free(void *ptr, const char *file, int lineno, const char *func);
void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func);
char *__ast_strdup(const char *s, const char *file, int lineno, const char *func);
char *__ast_strndup(const char *s, size_t n, const char *file, int lineno, const char *func);
int __ast_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func);

void __ast_mm_init(void);


/* Provide our own definitions */
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

#define vasprintf(a,b,c) \
	__ast_vasprintf(a,b,c,__FILE__, __LINE__, __PRETTY_FUNCTION__)

#else
#error "NEVER INCLUDE astmm.h DIRECTLY!!"
#endif
#endif
