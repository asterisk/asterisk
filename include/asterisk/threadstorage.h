/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file threadstorage.h
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Definitions to aid in the use of thread local storage
*/

#ifndef ASTERISK_THREADSTORAGE_H
#define ASTERISK_THREADSTORAGE_H

#include <pthread.h>

#include "asterisk/utils.h"
#include "asterisk/inline_api.h"

/*!
 * \brief data for a thread locally stored variable
 */
struct ast_threadstorage {
	/*! Ensure that the key is only initialized by one thread */
	pthread_once_t once;
	/*! The key used to retrieve this thread's data */
	pthread_key_t key;
	/*! The function that initializes the key */
	void (*key_init)(void);
};

#ifdef SOLARIS
#define THREADSTORAGE_ONCE_INIT {PTHREAD_ONCE_INIT}
#else
#define THREADSTORAGE_ONCE_INIT PTHREAD_ONCE_INIT
#endif

#if defined(DEBUG_THREADLOCALS)
void __ast_threadstorage_object_add(void *key, size_t len, const char *file, const char *function, unsigned int line);
void __ast_threadstorage_object_remove(void *key);
void __ast_threadstorage_object_replace(void *key_old, void *key_new, size_t len);
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief Define a thread storage variable
 *
 * \arg name The name of the thread storage
 * \arg name_init This is a name used to create the function that gets called
 *      to initialize this thread storage. It can be anything since it will not
 *      be referred to anywhere else
 *
 * This macro would be used to declare an instance of thread storage in a file.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_buf, my_buf_init);
 * \endcode
 */
#define AST_THREADSTORAGE(name, name_init) \
	AST_THREADSTORAGE_CUSTOM(name, name_init, ast_free) 

#if !defined(DEBUG_THREADLOCALS)
#define AST_THREADSTORAGE_CUSTOM(name, name_init, cleanup)  \
static void name_init(void);                                \
static struct ast_threadstorage name = {                    \
	.once = THREADSTORAGE_ONCE_INIT,                    \
	.key_init = name_init,                              \
};                                                          \
static void name_init(void)                                 \
{                                                           \
	pthread_key_create(&(name).key, cleanup);           \
}
#else /* defined(DEBUG_THREADLOCALS) */
#define AST_THREADSTORAGE_CUSTOM(name, name_init, cleanup)  \
static void name_init(void);                                \
static struct ast_threadstorage name = {                    \
	.once = THREADSTORAGE_ONCE_INIT,                    \
	.key_init = name_init,                              \
};                                                          \
static void __cleanup_##name(void *data)		    \
{							    \
	__ast_threadstorage_object_remove(data);	    \
	cleanup(data);					    \
}							    \
static void name_init(void)                                 \
{                                                           \
	pthread_key_create(&(name).key, __cleanup_##name);  \
}
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief Retrieve thread storage
 *
 * \arg ts This is a pointer to the thread storage structure declared by using
 *      the AST_THREADSTORAGE macro.  If declared with 
 *      AST_THREADSTORAGE(my_buf, my_buf_init), then this argument would be 
 *      (&my_buf).
 * \arg init_size This is the amount of space to be allocated the first time
 *      this thread requests its data. Thus, this should be the size that the
 *      code accessing this thread storage is assuming the size to be.
 *
 * \return This function will return the thread local storage associated with
 *         the thread storage management variable passed as the first argument.
 *         The result will be NULL in the case of a memory allocation error.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_buf, my_buf_init);
 * #define MY_BUF_SIZE   128
 * ...
 * void my_func(const char *fmt, ...)
 * {
 *      void *buf;
 *
 *      if (!(buf = ast_threadstorage_get(&my_buf, MY_BUF_SIZE)))
 *           return;
 *      ...
 * }
 * \endcode
 */
#if !defined(DEBUG_THREADLOCALS)
AST_INLINE_API(
void *ast_threadstorage_get(struct ast_threadstorage *ts, size_t init_size),
{
	void *buf;

	pthread_once(&ts->once, ts->key_init);
	if (!(buf = pthread_getspecific(ts->key))) {
		if (!(buf = ast_calloc(1, init_size)))
			return NULL;
		pthread_setspecific(ts->key, buf);
	}

	return buf;
}
)
#else /* defined(DEBUG_THREADLOCALS) */
AST_INLINE_API(
void *__ast_threadstorage_get(struct ast_threadstorage *ts, size_t init_size, const char *file, const char *function, unsigned int line),
{
	void *buf;

	pthread_once(&ts->once, ts->key_init);
	if (!(buf = pthread_getspecific(ts->key))) {
		if (!(buf = ast_calloc(1, init_size)))
			return NULL;
		pthread_setspecific(ts->key, buf);
		__ast_threadstorage_object_add(&ts->key, init_size, file, function, line);
	}

	return buf;
}
)

#define ast_threadstorage_get(ts, init_size) __ast_threadstorage_get(ts, init_size, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief A dynamic length string
 */
struct ast_dynamic_str {
	/* The current maximum length of the string */
	size_t len;
	/* The string buffer */
	char str[0];
};

/*!
 * \brief Create a dynamic length string
 *
 * \arg init_len This is the initial length of the string buffer
 *
 * \return This function returns a pointer to the dynamic string length.  The
 *         result will be NULL in the case of a memory allocation error.
 *
 * /note The result of this function is dynamically allocated memory, and must
 *       be free()'d after it is no longer needed.
 */
AST_INLINE_API(
struct ast_dynamic_str * attribute_malloc ast_dynamic_str_create(size_t init_len),
{
	struct ast_dynamic_str *buf;

	if (!(buf = ast_calloc(1, sizeof(*buf) + init_len)))
		return NULL;
	
	buf->len = init_len;

	return buf;
}
)

/*!
 * \brief Retrieve a thread locally stored dynamic string
 *
 * \arg ts This is a pointer to the thread storage structure declared by using
 *      the AST_THREADSTORAGE macro.  If declared with 
 *      AST_THREADSTORAGE(my_buf, my_buf_init), then this argument would be 
 *      (&my_buf).
 * \arg init_len This is the initial length of the thread's dynamic string. The
 *      current length may be bigger if previous operations in this thread have
 *      caused it to increase.
 *
 * \return This function will return the thread locally storaged dynamic string
 *         associated with the thread storage management variable passed as the
 *         first argument.
 *         The result will be NULL in the case of a memory allocation error.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_str, my_str_init);
 * #define MY_STR_INIT_SIZE   128
 * ...
 * void my_func(const char *fmt, ...)
 * {
 *      struct ast_dynamic_str *buf;
 *
 *      if (!(buf = ast_dynamic_str_thread_get(&my_str, MY_STR_INIT_SIZE)))
 *           return;
 *      ...
 * }
 * \endcode
 */
#if !defined(DEBUG_THREADLOCALS)
AST_INLINE_API(
struct ast_dynamic_str *ast_dynamic_str_thread_get(struct ast_threadstorage *ts,
	size_t init_len),
{
	struct ast_dynamic_str *buf;

	if (!(buf = ast_threadstorage_get(ts, sizeof(*buf) + init_len)))
		return NULL;
	
	if (!buf->len)
		buf->len = init_len;

	return buf;
}
)
#else /* defined(DEBUG_THREADLOCALS) */
AST_INLINE_API(
struct ast_dynamic_str *__ast_dynamic_str_thread_get(struct ast_threadstorage *ts,
	size_t init_len, const char *file, const char *function, unsigned int line),
{
	struct ast_dynamic_str *buf;

	if (!(buf = __ast_threadstorage_get(ts, sizeof(*buf) + init_len, file, function, line)))
		return NULL;
	
	if (!buf->len)
		buf->len = init_len;

	return buf;
}
)

#define ast_dynamic_str_thread_get(ts, init_len) __ast_dynamic_str_thread_get(ts, init_len, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif /* defined(DEBUG_THREADLOCALS) */ 

/*!
 * \brief Error codes from ast_dynamic_str_thread_build_va()
 */
enum {
	/*! An error has occured and the contents of the dynamic string
	 *  are undefined */
	AST_DYNSTR_BUILD_FAILED = -1,
	/*! The buffer size for the dynamic string had to be increased, and
	 *  ast_dynamic_str_thread_build_va() needs to be called again after
	 *  a va_end() and va_start().
	 */
	AST_DYNSTR_BUILD_RETRY = -2
};

/*!
 * \brief Set a thread locally stored dynamic string from a va_list
 *
 * \arg buf This is the address of a pointer to an ast_dynamic_str which should
 *      have been retrieved using ast_dynamic_str_thread_get.  It will need to
 *      be updated in the case that the buffer has to be reallocated to
 *      accomodate a longer string than what it currently has space for.
 * \arg max_len This is the maximum length to allow the string buffer to grow
 *      to.  If this is set to 0, then there is no maximum length.
 * \arg ts This is a pointer to the thread storage structure declared by using
 *      the AST_THREADSTORAGE macro.  If declared with 
 *      AST_THREADSTORAGE(my_buf, my_buf_init), then this argument would be 
 *      (&my_buf).
 * \arg fmt This is the format string (printf style)
 * \arg ap This is the va_list
 *
 * \return The return value of this function is the same as that of the printf
 *         family of functions.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_str, my_str_init);
 * #define MY_STR_INIT_SIZE   128
 * ...
 * void my_func(const char *fmt, ...)
 * {
 *      struct ast_dynamic_str *buf;
 *      va_list ap;
 *
 *      if (!(buf = ast_dynamic_str_thread_get(&my_str, MY_STR_INIT_SIZE)))
 *           return;
 *      ...
 *      va_start(fmt, ap);
 *      ast_dynamic_str_thread_set_va(&buf, 0, &my_str, fmt, ap);
 *      va_end(ap);
 * 
 *      printf("This is the string we just built: %s\n", buf->str);
 *      ...
 * }
 * \endcode
 */
#define ast_dynamic_str_thread_set_va(buf, max_len, ts, fmt, ap)                 \
	({                                                                       \
		int __res;                                                       \
		while ((__res = ast_dynamic_str_thread_build_va(buf, max_len,    \
			ts, 0, fmt, ap)) == AST_DYNSTR_BUILD_RETRY) {            \
			va_end(ap);                                              \
			va_start(ap, fmt);                                       \
		}                                                                \
		(__res);                                                         \
	})

/*!
 * \brief Append to a thread local dynamic string using a va_list
 *
 * The arguments, return values, and usage of this are the same as those for
 * ast_dynamic_str_thread_set_va().  However, instead of setting a new value
 * for the string, this will append to the current value.
 */
#define ast_dynamic_str_thread_append_va(buf, max_len, ts, fmt, ap)              \
	({                                                                       \
		int __res;                                                       \
		while ((__res = ast_dynamic_str_thread_build_va(buf, max_len,    \
			ts, 1, fmt, ap)) == AST_DYNSTR_BUILD_RETRY) {            \
			va_end(ap);                                              \
			va_start(ap, fmt);                                       \
		}                                                                \
		(__res);                                                         \
	})

/*!
 * \brief Core functionality of ast_dynamic_str_thread_(set|append)_va
 *
 * The arguments to this function are the same as those described for
 * ast_dynamic_str_thread_set_va except for an addition argument, append.
 * If append is non-zero, this will append to the current string instead of
 * writing over it.
 */
int ast_dynamic_str_thread_build_va(struct ast_dynamic_str **buf, size_t max_len,
	struct ast_threadstorage *ts, int append, const char *fmt, va_list ap);

/*!
 * \brief Set a thread locally stored dynamic string using variable arguments
 *
 * \arg buf This is the address of a pointer to an ast_dynamic_str which should
 *      have been retrieved using ast_dynamic_str_thread_get.  It will need to
 *      be updated in the case that the buffer has to be reallocated to
 *      accomodate a longer string than what it currently has space for.
 * \arg max_len This is the maximum length to allow the string buffer to grow
 *      to.  If this is set to 0, then there is no maximum length.
 * \arg ts This is a pointer to the thread storage structure declared by using
 *      the AST_THREADSTORAGE macro.  If declared with 
 *      AST_THREADSTORAGE(my_buf, my_buf_init), then this argument would be 
 *      (&my_buf).
 * \arg fmt This is the format string (printf style)
 *
 * \return The return value of this function is the same as that of the printf
 *         family of functions.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_str, my_str_init);
 * #define MY_STR_INIT_SIZE   128
 * ...
 * void my_func(int arg1, int arg2)
 * {
 *      struct ast_dynamic_str *buf;
 *      va_list ap;
 *
 *      if (!(buf = ast_dynamic_str_thread_get(&my_str, MY_STR_INIT_SIZE)))
 *           return;
 *      ...
 *      ast_dynamic_str_thread_set(&buf, 0, &my_str, "arg1: %d  arg2: %d\n",
 *           arg1, arg2);
 * 
 *      printf("This is the string we just built: %s\n", buf->str);
 *      ...
 * }
 * \endcode
 */
AST_INLINE_API(
int __attribute__ ((format (printf, 4, 5))) ast_dynamic_str_thread_set(
	struct ast_dynamic_str **buf, size_t max_len, 
	struct ast_threadstorage *ts, const char *fmt, ...),
{
	int res;
	va_list ap;

	va_start(ap, fmt);
	res = ast_dynamic_str_thread_set_va(buf, max_len, ts, fmt, ap);
	va_end(ap);

	return res;
}
)

/*!
 * \brief Append to a thread local dynamic string
 *
 * The arguments, return values, and usage of this function are the same as
 * ast_dynamic_str_thread_set().  However, instead of setting a new value for
 * the string, this function appends to the current value.
 */
AST_INLINE_API(
int __attribute__ ((format (printf, 4, 5))) ast_dynamic_str_thread_append(
	struct ast_dynamic_str **buf, size_t max_len, 
	struct ast_threadstorage *ts, const char *fmt, ...),
{
	int res;
	va_list ap;

	va_start(ap, fmt);
	res = ast_dynamic_str_thread_append_va(buf, max_len, ts, fmt, ap);
	va_end(ap);

	return res;
}
)

/*!
 * \brief Set a dynamic string
 *
 * \arg buf This is the address of a pointer to an ast_dynamic_str.  It will
 *      need to be updated in the case that the buffer has to be reallocated to
 *      accomodate a longer string than what it currently has space for.
 * \arg max_len This is the maximum length to allow the string buffer to grow
 *      to.  If this is set to 0, then there is no maximum length.
 *
 * \return The return value of this function is the same as that of the printf
 *         family of functions.
 */
AST_INLINE_API(
int __attribute__ ((format (printf, 3, 4))) ast_dynamic_str_set(
	struct ast_dynamic_str **buf, size_t max_len,
	const char *fmt, ...),
{
	int res;
	va_list ap;
	
	va_start(ap, fmt);
	res = ast_dynamic_str_thread_set_va(buf, max_len, NULL, fmt, ap);
	va_end(ap);

	return res;
}
)

/*!
 * \brief Append to a dynatic string
 *
 * The arguments, return values, and usage of this function are the same as
 * ast_dynamic_str_set().  However, this function appends to the string instead
 * of setting a new value.
 */
AST_INLINE_API(
int __attribute__ ((format (printf, 3, 4))) ast_dynamic_str_append(
	struct ast_dynamic_str **buf, size_t max_len,
	const char *fmt, ...),
{
	int res;
	va_list ap;
	
	va_start(ap, fmt);
	res = ast_dynamic_str_thread_append_va(buf, max_len, NULL, fmt, ap);
	va_end(ap);

	return res;
}
)

#endif /* ASTERISK_THREADSTORAGE_H */
