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

#include "asterisk/inline_api.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"

/* You may see casts in this header that may seem useless but they ensure this file is C++ clean */

#ifdef AST_DEVMODE
#define ast_strlen_zero(foo)	_ast_strlen_zero(foo, __FILE__, __PRETTY_FUNCTION__, __LINE__)
static force_inline int _ast_strlen_zero(const char *s, const char *file, const char *function, int line)
{
	if (!s || (*s == '\0')) {
		return 1;
	}
	if (!strcmp(s, "(null)")) {
		ast_log(__LOG_WARNING, file, line, function, "Possible programming error: \"(null)\" is not NULL!\n");
	}
	return 0;
}

#else
static force_inline int ast_strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}
#endif

/*! \brief returns the equivalent of logic or for strings:
 * first one if not empty, otherwise second one.
 */
#define S_OR(a, b)           (!ast_strlen_zero(a) ? (a) : (b))

/*! \brief returns the equivalent of logic or for strings, with an additional boolean check:
 * second one if not empty and first one is true, otherwise third one.
 * example: S_COR(usewidget, widget, "<no widget>")
 */
#define S_COR(a, b, c)   ((a && !ast_strlen_zero(b)) ? (b) : (c))

/*!
  \brief Gets a pointer to the first non-whitespace character in a string.
  \param str the input string
  \return a pointer to the first non-whitespace character
 */
AST_INLINE_API(
char *ast_skip_blanks(const char *str),
{
	while (*str && ((unsigned char) *str) < 33)
		str++;
	return (char *)str;
}
)

/*!
  \brief Trims trailing whitespace characters from a string.
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
		while ((work >= str) && ((unsigned char) *work) < 33)
			*(work--) = '\0';
	}
	return str;
}
)

/*!
  \brief Gets a pointer to first whitespace character in a string.
  \param str the input string
  \return a pointer to the first whitespace character
 */
AST_INLINE_API(
char *ast_skip_nonblanks(char *str),
{
	while (*str && ((unsigned char) *str) > 32)
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
  \brief Strip backslash for "escaped" semicolons, 
	the string to be stripped (will be modified).
  \return The stripped string.
 */
char *ast_unescape_semicolon(char *s);

/*!
  \brief Convert some C escape sequences  \verbatim (\b\f\n\r\t) \endverbatim into the
	equivalent characters. The string to be converted (will be modified).
  \return The converted string.
 */
char *ast_unescape_c(char *s);

/*!
  \brief Size-limited null-terminating string copy.
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
  
  \note This method is not recommended. New code should use ast_str_*() instead.

  This is a wrapper for snprintf, that properly handles the buffer pointer
  and buffer space available.

  \param buffer current position in buffer to place string into (will be updated on return)
  \param space remaining space in buffer (will be updated on return)
  \param fmt printf-style format string
  \retval 0 on success
  \retval non-zero on failure.
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
int ast_build_string_va(char **buffer, size_t *space, const char *fmt, va_list ap) __attribute__((format (printf, 3, 0)));

/*! 
 * \brief Make sure something is true.
 * Determine if a string containing a boolean value is "true".
 * This function checks to see whether a string passed to it is an indication of an "true" value.  
 * It checks to see if the string is "yes", "true", "y", "t", "on" or "1".  
 *
 * \retval 0 if val is a NULL pointer.
 * \retval -1 if "true".
 * \retval 0 otherwise.
 */
int ast_true(const char *val);

/*! 
 * \brief Make sure something is false.
 * Determine if a string containing a boolean value is "false".
 * This function checks to see whether a string passed to it is an indication of an "false" value.  
 * It checks to see if the string is "no", "false", "n", "f", "off" or "0".  
 *
 * \retval 0 if val is a NULL pointer.
 * \retval -1 if "true".
 * \retval 0 otherwise.
 */
int ast_false(const char *val);

/*
 *  \brief Join an array of strings into a single string.
 * \param s the resulting string buffer
 * \param len the length of the result buffer, s
 * \param w an array of strings to join.
 *
 * This function will join all of the strings in the array 'w' into a single
 * string.  It will also place a space in the result buffer in between each
 * string from 'w'.
*/
void ast_join(char *s, size_t len, char * const w[]);

/*
  \brief Parse a time (integer) string.
  \param src String to parse
  \param dst Destination
  \param _default Value to use if the string does not contain a valid time
  \param consumed The number of characters 'consumed' in the string by the parse (see 'man sscanf' for details)
  \retval 0 on success
  \retval non-zero on failure.
*/
int ast_get_time_t(const char *src, time_t *dst, time_t _default, int *consumed);

/*
  \brief Parse a time (float) string.
  \param src String to parse
  \param dst Destination
  \param _default Value to use if the string does not contain a valid time
  \param consumed The number of characters 'consumed' in the string by the parse (see 'man sscanf' for details)
  \return zero on success, non-zero on failure
*/
int ast_get_timeval(const char *src, struct timeval *tv, struct timeval _default, int *consumed);

/*!
 * Support for dynamic strings.
 *
 * A dynamic string is just a C string prefixed by a few control fields
 * that help setting/appending/extending it using a printf-like syntax.
 *
 * One should never declare a variable with this type, but only a pointer
 * to it, e.g.
 *
 *	struct ast_str *ds;
 *
 * The pointer can be initialized with the following:
 *
 *	ds = ast_str_create(init_len);
 *		creates a malloc()'ed dynamic string;
 *
 *	ds = ast_str_alloca(init_len);
 *		creates a string on the stack (not very dynamic!).
 *
 *	ds = ast_str_thread_get(ts, init_len)
 *		creates a malloc()'ed dynamic string associated to
 *		the thread-local storage key ts
 *
 * Finally, the string can be manipulated with the following:
 *
 *	ast_str_set(&buf, max_len, fmt, ...)
 *	ast_str_append(&buf, max_len, fmt, ...)
 *
 * and their varargs variant
 *
 *	ast_str_set_va(&buf, max_len, ap)
 *	ast_str_append_va(&buf, max_len, ap)
 *
 * \param max_len The maximum allowed length, reallocating if needed.
 * 	0 means unlimited, -1 means "at most the available space"
 *
 * \return All the functions return <0 in case of error, or the
 *	length of the string added to the buffer otherwise.
 */

/*! \brief The descriptor of a dynamic string
 *  XXX storage will be optimized later if needed
 * We use the ts field to indicate the type of storage.
 * Three special constants indicate malloc, alloca() or static
 * variables, all other values indicate a
 * struct ast_threadstorage pointer.
 */
struct ast_str {
	size_t len;	/*!< The current maximum length of the string */
	size_t used;	/*!< Amount of space used */
	struct ast_threadstorage *ts;	/*!< What kind of storage is this ? */
#define DS_MALLOC	((struct ast_threadstorage *)1)
#define DS_ALLOCA	((struct ast_threadstorage *)2)
#define DS_STATIC	((struct ast_threadstorage *)3)	/* not supported yet */
	char str[0];	/*!< The string buffer */
};

/*!
 * \brief Create a malloc'ed dynamic length string
 *
 * \param init_len This is the initial length of the string buffer
 *
 * \return This function returns a pointer to the dynamic string length.  The
 *         result will be NULL in the case of a memory allocation error.
 *
 * \note The result of this function is dynamically allocated memory, and must
 *       be free()'d after it is no longer needed.
 */
AST_INLINE_API(
struct ast_str * attribute_malloc ast_str_create(size_t init_len),
{
	struct ast_str *buf;

	buf = (struct ast_str *)ast_calloc(1, sizeof(*buf) + init_len);
	if (buf == NULL)
		return NULL;
	
	buf->len = init_len;
	buf->used = 0;
	buf->ts = DS_MALLOC;

	return buf;
}
)

/*! \brief Reset the content of a dynamic string.
 * Useful before a series of ast_str_append.
 */
AST_INLINE_API(
void ast_str_reset(struct ast_str *buf),
{
	if (buf) {
		buf->used = 0;
		if (buf->len)
			buf->str[0] = '\0';
	}
}
)

/*
 * AST_INLINE_API() is a macro that takes a block of code as an argument.
 * Using preprocessor #directives in the argument is not supported by all
 * compilers, and it is a bit of an obfuscation anyways, so avoid it.
 * As a workaround, define a macro that produces either its argument
 * or nothing, and use that instead of #ifdef/#endif within the
 * argument to AST_INLINE_API().
 */
#if defined(DEBUG_THREADLOCALS)
#define	_DB1(x)	x
#else
#define _DB1(x)
#endif

/*!
 * Make space in a new string (e.g. to read in data from a file)
 */
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
AST_INLINE_API(
int _ast_str_make_space(struct ast_str **buf, size_t new_len, const char *file, int lineno, const char *function),
{
	_DB1(struct ast_str *old_buf = *buf;)

	if (new_len <= (*buf)->len) 
		return 0;	/* success */
	if ((*buf)->ts == DS_ALLOCA || (*buf)->ts == DS_STATIC)
		return -1;	/* cannot extend */
	*buf = (struct ast_str *)__ast_realloc(*buf, new_len + sizeof(struct ast_str), file, lineno, function);
	if (*buf == NULL) /* XXX watch out, we leak memory here */
		return -1;
	if ((*buf)->ts != DS_MALLOC) {
		pthread_setspecific((*buf)->ts->key, *buf);
		_DB1(__ast_threadstorage_object_replace(old_buf, *buf, new_len + sizeof(struct ast_str));)
	}

	(*buf)->len = new_len;
	return 0;
}
)
#define ast_str_make_space(a,b)	_ast_str_make_space(a,b,__FILE__,__LINE__,__PRETTY_FUNCTION__)
#else
AST_INLINE_API(
int ast_str_make_space(struct ast_str **buf, size_t new_len),
{
	_DB1(struct ast_str *old_buf = *buf;)

	if (new_len <= (*buf)->len) 
		return 0;	/* success */
	if ((*buf)->ts == DS_ALLOCA || (*buf)->ts == DS_STATIC)
		return -1;	/* cannot extend */
	*buf = (struct ast_str *)ast_realloc(*buf, new_len + sizeof(struct ast_str));
	if (*buf == NULL) /* XXX watch out, we leak memory here */
		return -1;
	if ((*buf)->ts != DS_MALLOC) {
		pthread_setspecific((*buf)->ts->key, *buf);
		_DB1(__ast_threadstorage_object_replace(old_buf, *buf, new_len + sizeof(struct ast_str));)
	}

        (*buf)->len = new_len;
        return 0;
}
)
#endif

#define ast_str_alloca(init_len)			\
	({						\
		struct ast_str *__ast_str_buf;			\
		__ast_str_buf = alloca(sizeof(*__ast_str_buf) + init_len);	\
		__ast_str_buf->len = init_len;			\
		__ast_str_buf->used = 0;				\
		__ast_str_buf->ts = DS_ALLOCA;			\
		__ast_str_buf->str[0] = '\0';			\
		(__ast_str_buf);					\
	})

/*!
 * \brief Retrieve a thread locally stored dynamic string
 *
 * \param ts This is a pointer to the thread storage structure declared by using
 *      the AST_THREADSTORAGE macro.  If declared with 
 *      AST_THREADSTORAGE(my_buf, my_buf_init), then this argument would be 
 *      (&my_buf).
 * \param init_len This is the initial length of the thread's dynamic string. The
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
 *      struct ast_str *buf;
 *
 *      if (!(buf = ast_str_thread_get(&my_str, MY_STR_INIT_SIZE)))
 *           return;
 *      ...
 * }
 * \endcode
 */
#if !defined(DEBUG_THREADLOCALS)
AST_INLINE_API(
struct ast_str *ast_str_thread_get(struct ast_threadstorage *ts,
	size_t init_len),
{
	struct ast_str *buf;

	buf = (struct ast_str *)ast_threadstorage_get(ts, sizeof(*buf) + init_len);
	if (buf == NULL)
		return NULL;
	
	if (!buf->len) {
		buf->len = init_len;
		buf->used = 0;
		buf->ts = ts;
	}

	return buf;
}
)
#else /* defined(DEBUG_THREADLOCALS) */
AST_INLINE_API(
struct ast_str *__ast_str_thread_get(struct ast_threadstorage *ts,
	size_t init_len, const char *file, const char *function, unsigned int line),
{
	struct ast_str *buf;

	buf = (struct ast_str *)__ast_threadstorage_get(ts, sizeof(*buf) + init_len, file, function, line);
	if (buf == NULL)
		return NULL;
	
	if (!buf->len) {
		buf->len = init_len;
		buf->used = 0;
		buf->ts = ts;
	}

	return buf;
}
)

#define ast_str_thread_get(ts, init_len) __ast_str_thread_get(ts, init_len, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief Error codes from __ast_str_helper()
 * The undelying processing to manipulate dynamic string is done
 * by __ast_str_helper(), which can return a success, a
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
	 *  __ast_str_helper() needs to be called again after
	 *  a va_end() and va_start().
	 */
	AST_DYNSTR_BUILD_RETRY = -2
};

/*!
 * \brief Set a dynamic string from a va_list
 *
 * \param buf This is the address of a pointer to a struct ast_str.
 *	If it is retrieved using ast_str_thread_get, the
	struct ast_threadstorage pointer will need to
 *      be updated in the case that the buffer has to be reallocated to
 *      accommodate a longer string than what it currently has space for.
 * \param max_len This is the maximum length to allow the string buffer to grow
 *      to.  If this is set to 0, then there is no maximum length.
 * \param fmt This is the format string (printf style)
 * \param ap This is the va_list
 *
 * \return The return value of this function is the same as that of the printf
 *         family of functions.
 *
 * Example usage (the first part is only for thread-local storage)
 * \code
 * AST_THREADSTORAGE(my_str, my_str_init);
 * #define MY_STR_INIT_SIZE   128
 * ...
 * void my_func(const char *fmt, ...)
 * {
 *      struct ast_str *buf;
 *      va_list ap;
 *
 *      if (!(buf = ast_str_thread_get(&my_str, MY_STR_INIT_SIZE)))
 *           return;
 *      ...
 *      va_start(fmt, ap);
 *      ast_str_set_va(&buf, 0, fmt, ap);
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
#define ast_str_set_va(buf, max_len, fmt, ap)			\
	({								\
		int __res;						\
		while ((__res = __ast_str_helper(buf, max_len,		\
			0, fmt, ap)) == AST_DYNSTR_BUILD_RETRY) {	\
			va_end(ap);					\
			va_start(ap, fmt);				\
		}							\
		(__res);						\
	})

/*!
 * \brief Append to a dynamic string using a va_list
 *
 * Same as ast_str_set_va(), but append to the current content.
 */
#define ast_str_append_va(buf, max_len, fmt, ap)		\
	({								\
		int __res;						\
		while ((__res = __ast_str_helper(buf, max_len,		\
			1, fmt, ap)) == AST_DYNSTR_BUILD_RETRY) {	\
			va_end(ap);					\
			va_start(ap, fmt);				\
		}							\
		(__res);						\
	})

/*!
 * \brief Core functionality of ast_str_(set|append)_va
 *
 * The arguments to this function are the same as those described for
 * ast_str_set_va except for an addition argument, append.
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
int __ast_str_helper(struct ast_str **buf, size_t max_len,
	int append, const char *fmt, va_list ap);

/*!
 * \brief Set a dynamic string using variable arguments
 *
 * \param buf This is the address of a pointer to a struct ast_str which should
 *      have been retrieved using ast_str_thread_get.  It will need to
 *      be updated in the case that the buffer has to be reallocated to
 *      accomodate a longer string than what it currently has space for.
 * \param max_len This is the maximum length to allow the string buffer to grow
 *      to.  If this is set to 0, then there is no maximum length.
 *	If set to -1, we are bound to the current maximum length.
 * \param fmt This is the format string (printf style)
 *
 * \return The return value of this function is the same as that of the printf
 *         family of functions.
 *
 * All the rest is the same as ast_str_set_va()
 */
AST_INLINE_API(
int __attribute__ ((format (printf, 3, 4))) ast_str_set(
	struct ast_str **buf, size_t max_len, const char *fmt, ...),
{
	int res;
	va_list ap;

	va_start(ap, fmt);
	res = ast_str_set_va(buf, max_len, fmt, ap);
	va_end(ap);

	return res;
}
)

/*!
 * \brief Append to a thread local dynamic string
 *
 * The arguments, return values, and usage of this function are the same as
 * ast_str_set(), but the new data is appended to the current value.
 */
AST_INLINE_API(
int __attribute__ ((format (printf, 3, 4))) ast_str_append(
	struct ast_str **buf, size_t max_len, const char *fmt, ...),
{
	int res;
	va_list ap;

	va_start(ap, fmt);
	res = ast_str_append_va(buf, max_len, fmt, ap);
	va_end(ap);

	return res;
}
)

/*!
 * \brief Compute a hash value on a string
 *
 * This famous hash algorithm was written by Dan Bernstein and is
 * commonly used.
 *
 * http://www.cse.yorku.ca/~oz/hash.html
 */
static force_inline int ast_str_hash(const char *str)
{
	int hash = 5381;

	while (*str)
		hash = hash * 33 ^ *str++;

	return abs(hash);
}

#endif /* _ASTERISK_STRINGS_H */
