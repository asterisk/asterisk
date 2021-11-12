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
 * \brief Asterisk memory management routines
 *
 * This file should never be \#included directly, it is included
 * by asterisk.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASTERISK_ASTMM_H
#define _ASTERISK_ASTMM_H
/* IWYU pragma: private, include "asterisk.h" */

void *ast_std_malloc(size_t size) attribute_malloc;
void *ast_std_calloc(size_t nmemb, size_t size) attribute_malloc;
void *ast_std_realloc(void *ptr, size_t size);
void ast_std_free(void *ptr);

/*!
 * \brief free() wrapper
 *
 * ast_free_ptr should be used when a function pointer for free() needs to be passed
 * as the argument to a function. Otherwise, astmm will cause seg faults.
 */
void ast_free_ptr(void *ptr);

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void *__ast_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void *__ast_malloc(size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void __ast_free(void *ptr, const char *file, int lineno, const char *func);
void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func);
char *__ast_strdup(const char *s, const char *file, int lineno, const char *func) attribute_malloc;
char *__ast_strndup(const char *s, size_t n, const char *file, int lineno, const char *func) attribute_malloc;
int __ast_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
	__attribute__((format(printf, 5, 6)));
int __ast_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
	__attribute__((format(printf, 2, 0)));

/* The __ast_repl functions should not used from Asterisk sources, they are exposed
 * for use by ASTMM_REDIRECT and bundled pjproject. */
void *__ast_repl_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void *__ast_repl_malloc(size_t size, const char *file, int lineno, const char *func) attribute_malloc;
void *__ast_repl_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func);
char *__ast_repl_strdup(const char *s, const char *file, int lineno, const char *func) attribute_malloc;
char *__ast_repl_strndup(const char *s, size_t n, const char *file, int lineno, const char *func) attribute_malloc;
int __ast_repl_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
	__attribute__((format(printf, 5, 6)));
int __ast_repl_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
	__attribute__((format(printf, 2, 0)));

/*!
 * \brief ASTMM_LIBC can be defined to control the meaning of standard allocators.
 *
 * \note The standard allocators effected by this compiler define are:
 *    malloc, calloc, realloc, strdup, strndup, asprintf, vasprintf and free.
 *
 * @{
 */

/*!
 * \brief Produce compiler errors if standard allocators are used.
 *
 * \note This is the default option, and in most cases the correct option.
 * Any use of standard allocators will cause an error, even if those uses
 * are in unused static inline header functions.
 */
#define ASTMM_BLOCK    0

/*!
 * \brief Redirect standard allocators to use Asterisk functions.
 *
 * \note This option is used in some cases instead of changing the
 * existing source to use Asterisk functions.  New code should
 * generally avoid this option, except where it's needed to work
 * with situations where switching the code is unreasonable, such
 * as output from code generators that are hard coded to use
 * standard functions.
 */
#define ASTMM_REDIRECT 1

/*!
 * \brief Standard allocators are used directly.
 *
 * \note This option is needed when including 3rd party headers with calls
 * to standard allocators from inline functions.  Using ASTMM_REDIRECT in
 * this situation could result in an object being allocated by malloc and
 * freed by ast_free, or the reverse.
 */
#define ASTMM_IGNORE   2

/*! @} */

#if !defined(ASTMM_LIBC)
/* BLOCK libc allocators by default. */
#define ASTMM_LIBC ASTMM_BLOCK
#endif

#if ASTMM_LIBC == ASTMM_IGNORE
/* Don't touch the libc functions. */
#else

/* Undefine any macros */
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef strndup
#undef asprintf
#undef vasprintf
#undef free

#if ASTMM_LIBC == ASTMM_REDIRECT

/* Redefine libc functions to our own versions */
#define calloc(nmemb, size) \
	__ast_repl_calloc(nmemb, size, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define malloc(size) \
	__ast_repl_malloc(size, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define free(ptr) \
	__ast_free(ptr, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define realloc(ptr, size) \
	__ast_repl_realloc(ptr, size, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define strdup(s) \
	__ast_repl_strdup(s, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define strndup(s, n) \
	__ast_repl_strndup(s, n, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define asprintf(strp, format, args...) \
	__ast_repl_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, strp, format, args)
#define vasprintf(strp, format, ap) \
	__ast_repl_vasprintf(strp, format, ap, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#elif ASTMM_LIBC == ASTMM_BLOCK

/* Redefine libc functions to cause compile errors */
#define calloc(a, b) \
	Do_not_use_calloc__use_ast_calloc->fail(a, b)
#define malloc(a) \
	Do_not_use_malloc__use_ast_malloc->fail(a)
#define free(a) \
	Do_not_use_free__use_ast_free_or_ast_std_free_for_remotely_allocated_memory->fail(a)
#define realloc(a, b) \
	Do_not_use_realloc__use_ast_realloc->fail(a, b)
#define strdup(a) \
	Do_not_use_strdup__use_ast_strdup->fail(a)
#define strndup(a, b) \
	Do_not_use_strndup__use_ast_strndup->fail(a, b)
#define asprintf(a, b, c...) \
	Do_not_use_asprintf__use_ast_asprintf->fail(a, b, c)
#define vasprintf(a, b, c) \
	Do_not_use_vasprintf__use_ast_vasprintf->fail(a, b, c)

#else
#error "Unacceptable value for the macro ASTMM_LIBC"
#endif

#endif

/* Provide our own definition for ast_free */

#define ast_free(a) \
	__ast_free(a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for malloc()
 *
 * ast_malloc() is a wrapper for malloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The argument and return value are the same as malloc()
 */
#define ast_malloc(len) \
	__ast_malloc((len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for calloc()
 *
 * ast_calloc() is a wrapper for calloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as calloc()
 */
#define ast_calloc(num, len) \
	__ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for calloc() for use in cache pools
 *
 * ast_calloc_cache() is a wrapper for calloc() that will generate an Asterisk log
 * message in the case that the allocation fails. When memory debugging is in use,
 * the memory allocated by this function will be marked as 'cache' so it can be
 * distinguished from normal memory allocations.
 *
 * The arguments and return value are the same as calloc()
 */
#define ast_calloc_cache(num, len) \
	__ast_calloc_cache((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for realloc()
 *
 * ast_realloc() is a wrapper for realloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as realloc()
 */
#define ast_realloc(p, len) \
	__ast_realloc((p), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for strdup()
 *
 * ast_strdup() is a wrapper for strdup() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * ast_strdup(), unlike strdup(), can safely accept a NULL argument. If a NULL
 * argument is provided, ast_strdup will return NULL without generating any
 * kind of error log message.
 *
 * The argument and return value are the same as strdup()
 */
#define ast_strdup(str) \
	__ast_strdup((str), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for strndup()
 *
 * ast_strndup() is a wrapper for strndup() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * ast_strndup(), unlike strndup(), can safely accept a NULL argument for the
 * string to duplicate. If a NULL argument is provided, ast_strdup will return
 * NULL without generating any kind of error log message.
 *
 * The arguments and return value are the same as strndup()
 */
#define ast_strndup(str, len) \
	__ast_strndup((str), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for asprintf()
 *
 * ast_asprintf() is a wrapper for asprintf() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as asprintf()
 */
#define ast_asprintf(ret, fmt, ...) \
	__ast_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, (ret), (fmt), __VA_ARGS__)

/*!
 * \brief A wrapper for vasprintf()
 *
 * ast_vasprintf() is a wrapper for vasprintf() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as vasprintf()
 */
#define ast_vasprintf(ret, fmt, ap) \
	__ast_vasprintf((ret), (fmt), (ap), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
  \brief call __builtin_alloca to ensure we get gcc builtin semantics
  \param size The size of the buffer we want allocated

  This macro will attempt to allocate memory from the stack.  If it fails
  you won't get a NULL returned, but a SEGFAULT if you're lucky.
*/
#define ast_alloca(size) __builtin_alloca(size)

#if !defined(ast_strdupa) && defined(__GNUC__)
/*!
 * \brief duplicate a string in memory from the stack
 * \param s The string to duplicate
 *
 * This macro will duplicate the given string.  It returns a pointer to the stack
 * allocated memory for the new string.
 */
#define ast_strdupa(s)                                                    \
	(__extension__                                                    \
	({                                                                \
		const char *__old = (s);                                  \
		size_t __len = strlen(__old) + 1;                         \
		char *__new = __builtin_alloca(__len);                    \
		memcpy (__new, __old, __len);                             \
		__new;                                                    \
	}))
#endif

#else
#error "NEVER INCLUDE astmm.h DIRECTLY!!"
#endif /* _ASTERISK_ASTMM_H */

#ifdef __cplusplus
}
#endif
