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
 * \brief String manipulation functions
 */

#ifndef _ASTERISK_STRINGS_H
#define _ASTERISK_STRINGS_H

#include <string.h>
#include <stdarg.h>

#include "asterisk/inline_api.h"
#include "asterisk/compiler.h"
#include "asterisk/compat.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"

static force_inline int ast_strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}

/*! \brief returns the equivalent of logic or for strings:
 * first one if not empty, otherwise second one.
 */
#define S_OR(a, b)	(!ast_strlen_zero(a) ? (a) : (b))

/*!
  \brief Gets a pointer to the first non-whitespace character in a string.
  \param ast_skip_blanks function being used
  \param str the input string
  \return a pointer to the first non-whitespace character
 */
AST_INLINE_API(
char *ast_skip_blanks(const char *str),
{
	while (*str && *str < 33)
		str++;
	return (char *)str;
}
)

/*!
  \brief Trims trailing whitespace characters from a string.
  \param ast_trim_blanks function being used
  \param str the input string
  \return a pointer to the modified string
 */
AST_INLINE_API(
char *ast_trim_blanks(char *str),
{
	char *work = str;

	if (work) {
		work += strlen(work) - 1;
		/* It's tempting to only want to erase after we exit this loop, 
		   but since ast_trim_blanks *could* receive a constant string
		   (which we presumably wouldn't have to touch), we shouldn't
		   actually set anything unless we must, and it's easier just
		   to set each position to \0 than to keep track of a variable
		   for it */
		while ((work >= str) && *work < 33)
			*(work--) = '\0';
	}
	return str;
}
)

/*!
  \brief Gets a pointer to first whitespace character in a string.
  \param ast_skip_noblanks function being used
  \param str the input string
  \return a pointer to the first whitespace character
 */
AST_INLINE_API(
char *ast_skip_nonblanks(char *str),
{
	while (*str && *str > 32)
		str++;
	return str;
}
)
  
/*!
  \brief Strip leading/trailing whitespace from a string.
  \param s The string to be stripped (will be modified).
  \return The stripped string.

  This functions strips all leading and trailing whitespace
  characters from the input string, and returns a pointer to
  the resulting string. The string is modified in place.
*/
AST_INLINE_API(
char *ast_strip(char *s),
{
	s = ast_skip_blanks(s);
	if (s)
		ast_trim_blanks(s);
	return s;
} 
)

/*!
  \brief Strip leading/trailing whitespace and quotes from a string.
  \param s The string to be stripped (will be modified).
  \param beg_quotes The list of possible beginning quote characters.
  \param end_quotes The list of matching ending quote characters.
  \return The stripped string.

  This functions strips all leading and trailing whitespace
  characters from the input string, and returns a pointer to
  the resulting string. The string is modified in place.

  It can also remove beginning and ending quote (or quote-like)
  characters, in matching pairs. If the first character of the
  string matches any character in beg_quotes, and the last
  character of the string is the matching character in
  end_quotes, then they are removed from the string.

  Examples:
  \code
  ast_strip_quoted(buf, "\"", "\"");
  ast_strip_quoted(buf, "'", "'");
  ast_strip_quoted(buf, "[{(", "]})");
  \endcode
 */
char *ast_strip_quoted(char *s, const char *beg_quotes, const char *end_quotes);

/*!
  \brief Size-limited null-terminating string copy.
  \param ast_copy_string function being used
  \param dst The destination buffer.
  \param src The source string
  \param size The size of the destination buffer
  \return Nothing.

  This is similar to \a strncpy, with two important differences:
    - the destination buffer will \b always be null-terminated
    - the destination buffer is not filled with zeros past the copied string length
  These differences make it slightly more efficient, and safer to use since it will
  not leave the destination buffer unterminated. There is no need to pass an artificially
  reduced buffer size to this function (unlike \a strncpy), and the buffer does not need
  to be initialized to zeroes prior to calling this function.
*/
AST_INLINE_API(
void ast_copy_string(char *dst, const char *src, size_t size),
{
	while (*src && size) {
		*dst++ = *src++;
		size--;
	}
	if (__builtin_expect(!size, 0))
		dst--;
	*dst = '\0';
}
)


/*!
  \brief Build a string in a buffer, designed to be called repeatedly
  
  This is a wrapper for snprintf, that properly handles the buffer pointer
  and buffer space available.

  \param buffer current position in buffer to place string into (will be updated on return)
  \param space remaining space in buffer (will be updated on return)
  \param fmt printf-style format string
  \return 0 on success, non-zero on failure.
*/
int ast_build_string(char **buffer, size_t *space, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

/*!
  \brief Build a string in a buffer, designed to be called repeatedly
  
  This is a wrapper for snprintf, that properly handles the buffer pointer
  and buffer space available.

  \return 0 on success, non-zero on failure.
  \param buffer current position in buffer to place string into (will be updated on return)
  \param space remaining space in buffer (will be updated on return)
  \param fmt printf-style format string
  \param ap varargs list of arguments for format
*/
int ast_build_string_va(char **buffer, size_t *space, const char *fmt, va_list ap);

/*! Make sure something is true */
/*!
 * Determine if a string containing a boolean value is "true".
 * This function checks to see whether a string passed to it is an indication of an "true" value.  It checks to see if the string is "yes", "true", "y", "t", "on" or "1".  
 *
 * Returns 0 if val is a NULL pointer, -1 if "true", and 0 otherwise.
 */
int ast_true(const char *val);

/*! Make sure something is false */
/*!
 * Determine if a string containing a boolean value is "false".
 * This function checks to see whether a string passed to it is an indication of an "false" value.  It checks to see if the string is "no", "false", "n", "f", "off" or "0".  
 *
 * Returns 0 if val is a NULL pointer, -1 if "false", and 0 otherwise.
 */
int ast_false(const char *val);

/*
  \brief Join an array of strings into a single string.
  \param s the resulting string buffer
  \param len the length of the result buffer, s
  \param w an array of strings to join

  This function will join all of the strings in the array 'w' into a single
  string.  It will also place a space in the result buffer in between each
  string from 'w'.
*/
void ast_join(char *s, size_t len, char * const w[]);

/*
  \brief Parse a time (integer) string.
  \param src String to parse
  \param dst Destination
  \param _default Value to use if the string does not contain a valid time
  \param consumed The number of characters 'consumed' in the string by the parse (see 'man sscanf' for details)
  \return zero on success, non-zero on failure
*/
int ast_get_time_t(const char *src, time_t *dst, time_t _default, int *consumed);

/* The realloca lets us ast_restrdupa(), but you can't mix any other ast_strdup calls! */

struct ast_realloca {
	char *ptr;
	int alloclen;
};

#define ast_restrdupa(ra, s) \
	({ \
		if ((ra)->ptr && strlen(s) + 1 < (ra)->alloclen) { \
			strcpy((ra)->ptr, s); \
		} else { \
			(ra)->ptr = alloca(strlen(s) + 1 - (ra)->alloclen); \
			if ((ra)->ptr) (ra)->alloclen = strlen(s) + 1; \
		} \
		(ra)->ptr; \
	})

/*!
 * Support for dynamic strings.
 *
 * A dynamic string is just a C string prefixed by a few control fields
 * that help setting/appending/extending it using a printf-like syntax.
 *
 * One should never declare a variable with this type, but only a pointer
 * to it, e.g.
 *
 *	struct ast_dynamic_str *ds;
 *
 * The pointer can be initialized with the following:
 *
 *	ds = ast_dynamic_str_create(init_len);
 *		creates a malloc()'ed dynamic string;
 *
 *	ds = ast_dynamic_str_alloca(init_len);
 *		creates a string on the stack (not very dynamic!).
 *
 *	ds = ast_dynamic_str_thread_get(ts, init_len)
 *		creates a malloc()'ed dynamic string associated to
 *		the thread-local storage key ts
 *
 * Finally, the string can be manipulated with the following:
 *
 *	ast_dynamic_str_set(&buf, max_len, ts, fmt, ...)
 *	ast_dynamic_str_append(&buf, max_len, ts, fmt, ...)
 *	ast_dynamic_str_thread_set(&buf, max_len, ts, fmt, ...)
 *	ast_dynamic_str_thread_append(&buf, max_len, ts, fmt, ...)
 *
 * and their varargs format.
 *
 * \arg max_len The maximum allowed length, reallocating if needed.
 * 	0 means unlimited, -1 means "at most the available space"
 *
 * XXX the [_thread] variants can be removed if we save the ts in the
 * string descriptor.
 */

/*! \brief type of storage used for dynamic string */
enum dynstr_type {
	DS_MALLOC = 1,
	DS_ALLOCA = 2,
	DS_STATIC = 3,	/* XXX not supported yet */
};

/*! \brief The descriptor of a dynamic string
 *  XXX storage will be optimized later if needed
 */
struct ast_dynamic_str {
	size_t len;	/*!< The current maximum length of the string */
	size_t used;	/*!< Amount of space used */
	enum dynstr_type type;	/*!< What kind of storage is this ? */
	char str[0];	/*!< The string buffer */
};

/*!
 * \brief Create a malloc'ed dynamic length string
 *
 * \arg init_len This is the initial length of the string buffer
 *
 * \return This function returns a pointer to the dynamic string length.  The
 *         result will be NULL in the case of a memory allocation error.
 *
 * \note The result of this function is dynamically allocated memory, and must
 *       be free()'d after it is no longer needed.
 */
AST_INLINE_API(
struct ast_dynamic_str * attribute_malloc ast_dynamic_str_create(size_t init_len),
{
	struct ast_dynamic_str *buf;

	if (!(buf = ast_calloc(1, sizeof(*buf) + init_len)))
		return NULL;
	
	buf->len = init_len;
	buf->used = 0;
	buf->type = DS_MALLOC;

	return buf;
}
)

#define ast_dynamic_str_alloca(init_len)		\
	({						\
		struct ast_dynamic_str *buf;		\
		buf = alloca(sizeof(*buf) + init_len);	\
		buf->len = init_len;			\
		buf->used = 0;				\
		buf->type = DS_ALLOCA;			\
		buf->str[0] = '\0';			\
		(buf);					\
	})


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
 * \return This function will return the thread locally stored dynamic string
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
AST_INLINE_API(
struct ast_dynamic_str *ast_dynamic_str_thread_get(struct ast_threadstorage *ts,
	size_t init_len),
{
	struct ast_dynamic_str *buf;

	if (!(buf = ast_threadstorage_get(ts, sizeof(*buf) + init_len)))
		return NULL;
	
	if (!buf->len) {
		buf->len = init_len;
		buf->used = 0;
		buf->type = DS_MALLOC;
	}

	return buf;
}
)

/*!
 * \brief Error codes from __ast_dyn_str_helper()
 * The undelying processing to manipulate dynamic string is done
 * by __ast_dyn_str_helper(), which can return a success, a
 * permanent failure (e.g. no memory), or a temporary one (when
 * the string needs to be reallocated, and we must run va_start()
 * again; XXX this convoluted interface is only here because
 * FreeBSD 4 lacks va_copy, but this will be fixed and the
 * interface simplified).
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
 *      accommodate a longer string than what it currently has space for.
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
 *
 * \note: the following two functions must be implemented as macros
 *	because we must do va_end()/va_start() on the original arguments.
 */
#define ast_dynamic_str_thread_set_va(buf, max_len, ts, fmt, ap)	\
	({								\
		int __res;						\
		while ((__res = __ast_dyn_str_helper(buf, max_len,	\
			ts, 0, fmt, ap)) == AST_DYNSTR_BUILD_RETRY) {	\
			va_end(ap);					\
			va_start(ap, fmt);				\
		}							\
		(__res);						\
	})

/*!
 * \brief Append to a thread local dynamic string using a va_list
 *
 * The arguments, return values, and usage of this are the same as those for
 * ast_dynamic_str_thread_set_va().  However, instead of setting a new value
 * for the string, this will append to the current value.
 */
#define ast_dynamic_str_thread_append_va(buf, max_len, ts, fmt, ap)	\
	({								\
		int __res;						\
		while ((__res = __ast_dyn_str_helper(buf, max_len,	\
			ts, 1, fmt, ap)) == AST_DYNSTR_BUILD_RETRY) {	\
			va_end(ap);					\
			va_start(ap, fmt);				\
		}							\
		(__res);						\
	})

/*!
 * \brief Core functionality of ast_dynamic_str_[thread_](set|append)_va
 *
 * The arguments to this function are the same as those described for
 * ast_dynamic_str_thread_set_va except for an addition argument, append.
 * If append is non-zero, this will append to the current string instead of
 * writing over it.
 *
 * In the case that this function is called and the buffer was not large enough
 * to hold the result, the partial write will be truncated, and the result
 * AST_DYNSTR_BUILD_RETRY will be returned to indicate that the buffer size
 * was increased, and the function should be called a second time.
 *
 * A return of AST_DYNSTR_BUILD_FAILED indicates a memory allocation error.
 *
 * A return value greater than or equal to zero indicates the number of
 * characters that have been written, not including the terminating '\0'.
 * In the append case, this only includes the number of characters appended.
 *
 * \note This function should never need to be called directly.  It should
 *       through calling one of the other functions or macros defined in this
 *       file.
 */
int __ast_dyn_str_helper(struct ast_dynamic_str **buf, size_t max_len,
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
 *      accommodate a longer string than what it currently has space for.
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
 * \brief Append to a dynamic string
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

#endif /* _ASTERISK_STRINGS_H */
