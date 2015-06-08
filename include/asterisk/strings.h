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

/* #define DEBUG_OPAQUE */

#include <ctype.h>

#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"
#include "asterisk/astobj2.h"

#if defined(DEBUG_OPAQUE)
#define __AST_STR_USED used2
#define __AST_STR_LEN len2
#define __AST_STR_STR str2
#define __AST_STR_TS ts2
#else
#define __AST_STR_USED used
#define __AST_STR_LEN len
#define __AST_STR_STR str
#define __AST_STR_TS ts
#endif

/* You may see casts in this header that may seem useless but they ensure this file is C++ clean */

#define AS_OR(a,b)	(a && ast_str_strlen(a)) ? ast_str_buffer(a) : (b)

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
static force_inline int attribute_pure ast_strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}
#endif

#ifdef SENSE_OF_HUMOR
#define ast_strlen_real(a)	(a) ? strlen(a) : 0
#define ast_strlen_imaginary(a)	ast_random()
#endif

/*! \brief returns the equivalent of logic or for strings:
 * first one if not empty, otherwise second one.
 */
#define S_OR(a, b) ({typeof(&((a)[0])) __x = (a); ast_strlen_zero(__x) ? (b) : __x;})

/*! \brief returns the equivalent of logic or for strings, with an additional boolean check:
 * second one if not empty and first one is true, otherwise third one.
 * example: S_COR(usewidget, widget, "<no widget>")
 */
#define S_COR(a, b, c) ({typeof(&((b)[0])) __x = (b); (a) && !ast_strlen_zero(__x) ? (__x) : (c);})

/*
  \brief Checks whether a string begins with another.
  \since 12.0.0
  \param str String to check.
  \param prefix Prefix to look for.
  \param 1 if \a str begins with \a prefix, 0 otherwise.
 */
static int force_inline attribute_pure ast_begins_with(const char *str, const char *prefix)
{
	ast_assert(str != NULL);
	ast_assert(prefix != NULL);
	while (*str == *prefix && *prefix != '\0') {
		++str;
		++prefix;
	}
	return *prefix == '\0';
}

/*
  \brief Checks whether a string ends with another.
  \since 12.0.0
  \param str String to check.
  \param suffix Suffix to look for.
  \param 1 if \a str ends with \a suffix, 0 otherwise.
 */
static int force_inline attribute_pure ast_ends_with(const char *str, const char *suffix)
{
	size_t str_len;
	size_t suffix_len;

	ast_assert(str != NULL);
	ast_assert(suffix != NULL);
	str_len = strlen(str);
	suffix_len = strlen(suffix);

	if (suffix_len > str_len) {
		return 0;
	}

	return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/*!
 * \brief return Yes or No depending on the argument.
 *
 * Note that this macro is used my AMI, where a literal "Yes" and "No" are
 * expected, and translations would cause problems.
 *
 * \param x Boolean value
 * \return "Yes" if x is true (non-zero)
 * \return "No" if x is false (zero)
 */
#define AST_YESNO(x) ((x) ? "Yes" : "No")

/*!
  \brief Gets a pointer to the first non-whitespace character in a string.
  \param str the input string
  \return a pointer to the first non-whitespace character
 */
AST_INLINE_API(
char * attribute_pure ast_skip_blanks(const char *str),
{
	while (*str && ((unsigned char) *str) < 33)
		str++;
	return (char *) str;
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
char * attribute_pure ast_skip_nonblanks(const char *str),
{
	while (*str && ((unsigned char) *str) > 32)
		str++;
	return (char *) str;
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
	if ((s = ast_skip_blanks(s))) {
		ast_trim_blanks(s);
	}
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
  \brief Flags for ast_strsep
 */
enum ast_strsep_flags {
	AST_STRSEP_STRIP =    0x01, /*!< Trim, then strip quotes.  You may want to trim again */
	AST_STRSEP_TRIM =     0x02, /*!< Trim leading and trailing whitespace */
	AST_STRSEP_UNESCAPE = 0x04, /*!< Unescape '\' */
	AST_STRSEP_ALL =      0x07, /*!< Trim, strip, unescape */
};

/*!
  \brief Act like strsep but ignore separators inside quotes.
  \param s Pointer to address of the the string to be processed.
  Will be modified and can't be constant.
  \param sep A single character delimiter.
  \param flags Controls post-processing of the result.
  AST_STRSEP_TRIM trims all leading and trailing whitespace from the result.
  AST_STRSEP_STRIP does a trim then strips the outermost quotes.  You may want
  to trim again after the strip.  Just OR both the TRIM and STRIP flags.
  AST_STRSEP_UNESCAPE unescapes '\' sequences.
  AST_STRSEP_ALL does all of the above processing.
  \return The next token or NULL if done or if there are more than 8 levels of
  nested quotes.

  This function acts like strsep with three exceptions...
  The separator is a single character instead of a string.
  Separators inside quotes are treated literally instead of like separators.
  You can elect to have leading and trailing whitespace and quotes
  stripped from the result and have '\' sequences unescaped.

  Like strsep, ast_strsep maintains no internal state and you can call it
  recursively using different separators on the same storage.

  Also like strsep, for consistent results, consecutive separators are not
  collapsed so you may get an empty string as a valid result.

  Examples:
  \code
	char *mystr = ast_strdupa("abc=def,ghi='zzz=yyy,456',jkl");
	char *token, *token2, *token3;

	while((token = ast_strsep(&mystr, ',', AST_SEP_STRIP))) {
		// 1st token will be aaa=def
		// 2nd token will be ghi='zzz=yyy,456'
		while((token2 = ast_strsep(&token, '=', AST_SEP_STRIP))) {
			// 1st token2 will be ghi
			// 2nd token2 will be zzz=yyy,456
			while((token3 = ast_strsep(&token2, ',', AST_SEP_STRIP))) {
				// 1st token3 will be zzz=yyy
				// 2nd token3 will be 456
				// and so on
			}
		}
		// 3rd token will be jkl
	}

  \endcode
 */
char *ast_strsep(char **s, const char sep, uint32_t flags);

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
 * \brief Escape the 'to_escape' characters in the given string.
 *
 * \note The given output buffer has to have enough memory allocated to store the
 *       original string plus any escaped values.
 *
 * \param dest the escaped string
 * \param s the source string to escape
 * \param num number of characters to be copied from the source
 * \param to_escape an array of characters to escape
 *
 * \return Pointer to the destination.
 */
char* ast_escape(char *dest, const char *s, size_t num, const char *to_escape);

/*!
 * \brief Escape standard 'C' sequences in the given string.
 *
 * \note The given output buffer has to have enough memory allocated to store the
 *       original string plus any escaped values.
 *
 * \param dest the escaped string
 * \param s the source string to escape
 * \param num number of characters to be copied from the source
 * \param to_escape an array of characters to escape
 *
 * \return Pointer to the escaped string.
 */
char* ast_escape_c(char *dest, const char *s, size_t num);

/*!
 * \brief Escape the 'to_escape' characters in the given string.
 *
 * \note Caller is responsible for freeing the returned string
 *
 * \param s the source string to escape
 * \param to_escape an array of characters to escape
 *
 * \return Pointer to the escaped string or NULL.
 */
char *ast_escape_alloc(const char *s, const char *to_escape);

/*!
 * \brief Escape standard 'C' sequences in the given string.
 *
 * \note Caller is responsible for freeing the returned string
 *
 * \param s the source string to escape
 *
 * \return Pointer to the escaped string or NULL.
 */
char *ast_escape_c_alloc(const char *s);

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
int ast_build_string(char **buffer, size_t *space, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

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
int ast_build_string_va(char **buffer, size_t *space, const char *fmt, va_list ap) __attribute__((format(printf, 3, 0)));

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
int attribute_pure ast_true(const char *val);

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
int attribute_pure ast_false(const char *val);

/*
 * \brief Join an array of strings into a single string.
 * \param s the resulting string buffer
 * \param len the length of the result buffer, s
 * \param w an array of strings to join.
 * \param size the number of elements to join
 * \param delim delimiter between elements
 *
 * This function will join all of the strings in the array 'w' into a single
 * string.  It will also place 'delim' in the result buffer in between each
 * string from 'w'.
 * \since 12
*/
void ast_join_delim(char *s, size_t len, const char * const w[],
		    unsigned int size, char delim);

/*
 * \brief Join an array of strings into a single string.
 * \param s the resulting string buffer
 * \param len the length of the result buffer, s
 * \param w an array of strings to join.
 *
 * This function will join all of the strings in the array 'w' into a single
 * string.  It will also place a space in the result buffer in between each
 * string from 'w'.
*/
#define ast_join(s, len, w) ast_join_delim(s, len, w, -1, ' ')

/*
 * \brief Attempts to convert the given string to camel case using
 *        the specified delimiter.
 *
 * note - returned string needs to be freed
 *
 * \param s the string to convert
 * \param delim delimiter to parse out
 *
 * \retval The string converted to "CamelCase"
 * \since 12
*/
char *ast_to_camel_case_delim(const char *s, const char *delim);

/*
 * \brief Attempts to convert the given string to camel case using
 *        an underscore as the specified delimiter.
 *
 * note - returned string needs to be freed
 *
 * \param s the string to convert
 *
 * \retval The string converted to "CamelCase"
*/
#define ast_to_camel_case(s) ast_to_camel_case_delim(s, "_")

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
 * \param max_len The maximum allowed capacity of the ast_str. Note that
 *  if the value of max_len is less than the current capacity of the
 *  ast_str (as returned by ast_str_size), then the parameter is effectively
 *  ignored.
 * 	0 means unlimited, -1 means "at most the available space"
 *
 * \return All the functions return <0 in case of error, or the
 *	length of the string added to the buffer otherwise. Note that
 *	in most cases where an error is returned, characters ARE written
 *	to the ast_str.
 */

/*! \brief The descriptor of a dynamic string
 *  XXX storage will be optimized later if needed
 * We use the ts field to indicate the type of storage.
 * Three special constants indicate malloc, ast_alloca() or static
 * variables, all other values indicate a
 * struct ast_threadstorage pointer.
 */
struct ast_str {
	size_t __AST_STR_LEN;			/*!< The current maximum length of the string */
	size_t __AST_STR_USED;			/*!< Amount of space used */
	struct ast_threadstorage *__AST_STR_TS;	/*!< What kind of storage is this ? */
#define DS_MALLOC	((struct ast_threadstorage *)1)
#define DS_ALLOCA	((struct ast_threadstorage *)2)
#define DS_STATIC	((struct ast_threadstorage *)3)	/* not supported yet */
	char __AST_STR_STR[0];			/*!< The string buffer */
};

/*!
 * \brief Given a string regex_string in the form of "/regex/", convert it into the form of "regex"
 *
 * This function will trim one leading / and one trailing / from a given input string
 * ast_str regex_pattern must be preallocated before calling this function
 *
 * \return 0 on success, non-zero on failure.
 * \return 1 if we only stripped a leading /
 * \return 2 if we only stripped a trailing /
 * \return 3 if we did not strip any / characters
 * \param regex_string  the string containing /regex/
 * \param regex_pattern the destination ast_str which will contain "regex" after execution
 */
int ast_regex_string_to_regex_pattern(const char *regex_string, struct ast_str **regex_pattern);

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
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
#define	ast_str_create(a)	_ast_str_create(a,__FILE__,__LINE__,__PRETTY_FUNCTION__)
AST_INLINE_API(
struct ast_str * attribute_malloc _ast_str_create(size_t init_len,
		const char *file, int lineno, const char *func),
{
	struct ast_str *buf;

	buf = (struct ast_str *)__ast_calloc(1, sizeof(*buf) + init_len, file, lineno, func);
	if (buf == NULL)
		return NULL;

	buf->__AST_STR_LEN = init_len;
	buf->__AST_STR_USED = 0;
	buf->__AST_STR_TS = DS_MALLOC;

	return buf;
}
)
#else
AST_INLINE_API(
struct ast_str * attribute_malloc ast_str_create(size_t init_len),
{
	struct ast_str *buf;

	buf = (struct ast_str *)ast_calloc(1, sizeof(*buf) + init_len);
	if (buf == NULL)
		return NULL;

	buf->__AST_STR_LEN = init_len;
	buf->__AST_STR_USED = 0;
	buf->__AST_STR_TS = DS_MALLOC;

	return buf;
}
)
#endif

/*! \brief Reset the content of a dynamic string.
 * Useful before a series of ast_str_append.
 */
AST_INLINE_API(
void ast_str_reset(struct ast_str *buf),
{
	if (buf) {
		buf->__AST_STR_USED = 0;
		if (buf->__AST_STR_LEN) {
			buf->__AST_STR_STR[0] = '\0';
		}
	}
}
)

/*! \brief Update the length of the buffer, after using ast_str merely as a buffer.
 *  \param buf A pointer to the ast_str string.
 */
AST_INLINE_API(
void ast_str_update(struct ast_str *buf),
{
	buf->__AST_STR_USED = strlen(buf->__AST_STR_STR);
}
)

/*! \brief Trims trailing whitespace characters from an ast_str string.
 *  \param buf A pointer to the ast_str string.
 */
AST_INLINE_API(
void ast_str_trim_blanks(struct ast_str *buf),
{
	if (!buf) {
		return;
	}
	while (buf->__AST_STR_USED && buf->__AST_STR_STR[buf->__AST_STR_USED - 1] < 33) {
		buf->__AST_STR_STR[--(buf->__AST_STR_USED)] = '\0';
	}
}
)

/*!\brief Returns the current length of the string stored within buf.
 * \param buf A pointer to the ast_str structure.
 */
AST_INLINE_API(
size_t attribute_pure ast_str_strlen(const struct ast_str *buf),
{
	return buf->__AST_STR_USED;
}
)

/*!\brief Returns the current maximum length (without reallocation) of the current buffer.
 * \param buf A pointer to the ast_str structure.
 * \retval Current maximum length of the buffer.
 */
AST_INLINE_API(
size_t attribute_pure ast_str_size(const struct ast_str *buf),
{
	return buf->__AST_STR_LEN;
}
)

/*!\brief Returns the string buffer within the ast_str buf.
 * \param buf A pointer to the ast_str structure.
 * \retval A pointer to the enclosed string.
 */
AST_INLINE_API(
char * attribute_pure ast_str_buffer(const struct ast_str *buf),
{
	/* for now, cast away the const qualifier on the pointer
	 * being returned; eventually, it should become truly const
	 * and only be modified via accessor functions
	 */
	return (char *) buf->__AST_STR_STR;
}
)

/*!\brief Truncates the enclosed string to the given length.
 * \param buf A pointer to the ast_str structure.
 * \param len Maximum length of the string. If len is larger than the
 *        current maximum length, things will explode. If it is negative
 *        at most -len characters will be trimmed off the end.
 * \retval A pointer to the resulting string.
 */
AST_INLINE_API(
char *ast_str_truncate(struct ast_str *buf, ssize_t len),
{
	if (len < 0) {
		if ((typeof(buf->__AST_STR_USED)) -len >= buf->__AST_STR_USED) {
			buf->__AST_STR_USED = 0;
		} else {
			buf->__AST_STR_USED += len;
		}
	} else {
		buf->__AST_STR_USED = len;
	}
	buf->__AST_STR_STR[buf->__AST_STR_USED] = '\0';
	return buf->__AST_STR_STR;
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
	struct ast_str *old_buf = *buf;

	if (new_len <= (*buf)->__AST_STR_LEN) 
		return 0;	/* success */
	if ((*buf)->__AST_STR_TS == DS_ALLOCA || (*buf)->__AST_STR_TS == DS_STATIC)
		return -1;	/* cannot extend */
	*buf = (struct ast_str *)__ast_realloc(*buf, new_len + sizeof(struct ast_str), file, lineno, function);
	if (*buf == NULL) {
		*buf = old_buf;
		return -1;
	}
	if ((*buf)->__AST_STR_TS != DS_MALLOC) {
		pthread_setspecific((*buf)->__AST_STR_TS->key, *buf);
		_DB1(__ast_threadstorage_object_replace(old_buf, *buf, new_len + sizeof(struct ast_str));)
	}

	(*buf)->__AST_STR_LEN = new_len;
	return 0;
}
)
#define ast_str_make_space(a,b)	_ast_str_make_space(a,b,__FILE__,__LINE__,__PRETTY_FUNCTION__)
#else
AST_INLINE_API(
int ast_str_make_space(struct ast_str **buf, size_t new_len),
{
	struct ast_str *old_buf = *buf;

	if (new_len <= (*buf)->__AST_STR_LEN) 
		return 0;	/* success */
	if ((*buf)->__AST_STR_TS == DS_ALLOCA || (*buf)->__AST_STR_TS == DS_STATIC)
		return -1;	/* cannot extend */
	*buf = (struct ast_str *)ast_realloc(*buf, new_len + sizeof(struct ast_str));
	if (*buf == NULL) {
		*buf = old_buf;
		return -1;
	}
	if ((*buf)->__AST_STR_TS != DS_MALLOC) {
		pthread_setspecific((*buf)->__AST_STR_TS->key, *buf);
		_DB1(__ast_threadstorage_object_replace(old_buf, *buf, new_len + sizeof(struct ast_str));)
	}

	(*buf)->__AST_STR_LEN = new_len;
	return 0;
}
)
#endif

AST_INLINE_API(
int ast_str_copy_string(struct ast_str **dst, struct ast_str *src),
{

	/* make sure our destination is large enough */
	if (src->__AST_STR_USED + 1 > (*dst)->__AST_STR_LEN) {
		if (ast_str_make_space(dst, src->__AST_STR_USED + 1)) {
			return -1;
		}
	}

	memcpy((*dst)->__AST_STR_STR, src->__AST_STR_STR, src->__AST_STR_USED + 1);
	(*dst)->__AST_STR_USED = src->__AST_STR_USED;
	return 0;
}
)

#define ast_str_alloca(init_len)			\
	({						\
		struct ast_str *__ast_str_buf;			\
		__ast_str_buf = ast_alloca(sizeof(*__ast_str_buf) + init_len);	\
		__ast_str_buf->__AST_STR_LEN = init_len;			\
		__ast_str_buf->__AST_STR_USED = 0;				\
		__ast_str_buf->__AST_STR_TS = DS_ALLOCA;			\
		__ast_str_buf->__AST_STR_STR[0] = '\0';			\
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

	if (!buf->__AST_STR_LEN) {
		buf->__AST_STR_LEN = init_len;
		buf->__AST_STR_USED = 0;
		buf->__AST_STR_TS = ts;
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

	if (!buf->__AST_STR_LEN) {
		buf->__AST_STR_LEN = init_len;
		buf->__AST_STR_USED = 0;
		buf->__AST_STR_TS = ts;
	}

	return buf;
}
)

#define ast_str_thread_get(ts, init_len) __ast_str_thread_get(ts, init_len, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief Error codes from __ast_str_helper()
 * The undelying processing to manipulate dynamic string is done
 * by __ast_str_helper(), which can return a success or a
 * permanent failure (e.g. no memory).
 */
enum {
	/*! An error has occurred and the contents of the dynamic string
	 *  are undefined */
	AST_DYNSTR_BUILD_FAILED = -1,
	/*! The buffer size for the dynamic string had to be increased, and
	 *  __ast_str_helper() needs to be called again after
	 *  a va_end() and va_start().  This return value is legacy and will
	 *  no longer be used.
	 */
	AST_DYNSTR_BUILD_RETRY = -2
};

/*!
 * \brief Core functionality of ast_str_(set|append)_va
 *
 * The arguments to this function are the same as those described for
 * ast_str_set_va except for an addition argument, append.
 * If append is non-zero, this will append to the current string instead of
 * writing over it.
 *
 * AST_DYNSTR_BUILD_RETRY is a legacy define.  It should probably never
 * again be used.
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
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
int __attribute__((format(printf, 4, 0))) __ast_debug_str_helper(struct ast_str **buf, ssize_t max_len,
							   int append, const char *fmt, va_list ap, const char *file, int lineno, const char *func);
#define __ast_str_helper(a,b,c,d,e)	__ast_debug_str_helper(a,b,c,d,e,__FILE__,__LINE__,__PRETTY_FUNCTION__)
#else
int __attribute__((format(printf, 4, 0))) __ast_str_helper(struct ast_str **buf, ssize_t max_len,
							   int append, const char *fmt, va_list ap);
#endif
char *__ast_str_helper2(struct ast_str **buf, ssize_t max_len,
	const char *src, size_t maxsrc, int append, int escapecommas);

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
 * \note Care should be taken when using this function. The function can
 * result in reallocating the ast_str. If a pointer to the ast_str is passed
 * by value to a function that calls ast_str_set_va(), then the original ast_str
 * pointer may be invalidated due to a reallocation.
 *
 */
AST_INLINE_API(int __attribute__((format(printf, 3, 0))) ast_str_set_va(struct ast_str **buf, ssize_t max_len, const char *fmt, va_list ap),
{
	return __ast_str_helper(buf, max_len, 0, fmt, ap);
}
)

/*!
 * \brief Append to a dynamic string using a va_list
 *
 * Same as ast_str_set_va(), but append to the current content.
 *
 * \note Care should be taken when using this function. The function can
 * result in reallocating the ast_str. If a pointer to the ast_str is passed
 * by value to a function that calls ast_str_append_va(), then the original ast_str
 * pointer may be invalidated due to a reallocation.
 *
 * \param buf, max_len, fmt, ap
 */
AST_INLINE_API(int __attribute__((format(printf, 3, 0))) ast_str_append_va(struct ast_str **buf, ssize_t max_len, const char *fmt, va_list ap),
{
	return __ast_str_helper(buf, max_len, 1, fmt, ap);
}
)

/*!\brief Set a dynamic string to a non-NULL terminated substring. */
AST_INLINE_API(char *ast_str_set_substr(struct ast_str **buf, ssize_t maxlen, const char *src, size_t maxsrc),
{
	return __ast_str_helper2(buf, maxlen, src, maxsrc, 0, 0);
}
)

/*!\brief Append a non-NULL terminated substring to the end of a dynamic string. */
AST_INLINE_API(char *ast_str_append_substr(struct ast_str **buf, ssize_t maxlen, const char *src, size_t maxsrc),
{
	return __ast_str_helper2(buf, maxlen, src, maxsrc, 1, 0);
}
)

/*!\brief Set a dynamic string to a non-NULL terminated substring, with escaping of commas. */
AST_INLINE_API(char *ast_str_set_escapecommas(struct ast_str **buf, ssize_t maxlen, const char *src, size_t maxsrc),
{
	return __ast_str_helper2(buf, maxlen, src, maxsrc, 0, 1);
}
)

/*!\brief Append a non-NULL terminated substring to the end of a dynamic string, with escaping of commas. */
AST_INLINE_API(char *ast_str_append_escapecommas(struct ast_str **buf, ssize_t maxlen, const char *src, size_t maxsrc),
{
	return __ast_str_helper2(buf, maxlen, src, maxsrc, 1, 1);
}
)

/*!
 * \brief Set a dynamic string using variable arguments
 *
 * \note Care should be taken when using this function. The function can
 * result in reallocating the ast_str. If a pointer to the ast_str is passed
 * by value to a function that calls ast_str_set(), then the original ast_str
 * pointer may be invalidated due to a reallocation.
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
int __attribute__((format(printf, 3, 4))) ast_str_set(
	struct ast_str **buf, ssize_t max_len, const char *fmt, ...),
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
 * \note Care should be taken when using this function. The function can
 * result in reallocating the ast_str. If a pointer to the ast_str is passed
 * by value to a function that calls ast_str_append(), then the original ast_str
 * pointer may be invalidated due to a reallocation.
 *
 * The arguments, return values, and usage of this function are the same as
 * ast_str_set(), but the new data is appended to the current value.
 */
AST_INLINE_API(
int __attribute__((format(printf, 3, 4))) ast_str_append(
	struct ast_str **buf, ssize_t max_len, const char *fmt, ...),
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
 * \brief Check if a string is only digits
 *
 * \retval 1 The string contains only digits
 * \retval 0 The string contains non-digit characters
 */
AST_INLINE_API(
int ast_check_digits(const char *arg),
{
	while (*arg) {
		if (*arg < '0' || *arg > '9') {
			return 0;
		}
		arg++;
	}
	return 1;
}
)

/*!
 * \brief Convert the tech portion of a device string to upper case
 *
 * \retval dev_str Returns the char* passed in for convenience
 */
AST_INLINE_API(
char *ast_tech_to_upper(char *dev_str),
{
	char *pos;
	if (!dev_str || !strchr(dev_str, '/')) {
		return dev_str;
	}

	for (pos = dev_str; *pos && *pos != '/'; pos++) {
		*pos = toupper(*pos);
	}
	return dev_str;
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
static force_inline int attribute_pure ast_str_hash(const char *str)
{
	int hash = 5381;

	while (*str)
		hash = hash * 33 ^ *str++;

	return abs(hash);
}

/*!
 * \brief Compute a hash value on a string
 *
 * \param[in] str The string to add to the hash
 * \param[in] hash The hash value to add to
 * 
 * \details
 * This version of the function is for when you need to compute a
 * string hash of more than one string.
 *
 * This famous hash algorithm was written by Dan Bernstein and is
 * commonly used.
 *
 * \sa http://www.cse.yorku.ca/~oz/hash.html
 */
static force_inline int ast_str_hash_add(const char *str, int hash)
{
	while (*str)
		hash = hash * 33 ^ *str++;

	return abs(hash);
}

/*!
 * \brief Compute a hash value on a case-insensitive string
 *
 * Uses the same hash algorithm as ast_str_hash, but converts
 * all characters to lowercase prior to computing a hash. This
 * allows for easy case-insensitive lookups in a hash table.
 */
static force_inline int attribute_pure ast_str_case_hash(const char *str)
{
	int hash = 5381;

	while (*str) {
		hash = hash * 33 ^ tolower(*str++);
	}

	return abs(hash);
}

/*!
 * \brief Convert a string to all lower-case
 *
 * \param str The string to be converted to lower case
 *
 * \retval str for convenience
 */
static force_inline char *attribute_pure ast_str_to_lower(char *str)
{
	char *str_orig = str;
	if (!str) {
		return str;
	}

	for (; *str; ++str) {
		*str = tolower(*str);
	}

	return str_orig;
}

/*!
 * \brief Convert a string to all upper-case
 *
 * \param str The string to be converted to upper case
 *
 * \retval str for convenience
 */
static force_inline char *attribute_pure ast_str_to_upper(char *str)
{
	char *str_orig = str;
	if (!str) {
		return str;
	}

	for (; *str; ++str) {
		*str = toupper(*str);
	}

	return str_orig;
}

/*!
 * \since 12
 * \brief Allocates a hash container for bare strings
 *
 * \param buckets The number of buckets to use for the hash container
 *
 * \retval AO2 container for strings
 * \retval NULL if allocation failed
 */
#define ast_str_container_alloc(buckets) ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_MUTEX, buckets)

/*!
 * \since 12
 * \brief Allocates a hash container for bare strings
 *
 * \param opts Options to be provided to the container
 * \param buckets The number of buckets to use for the hash container
 *
 * \retval AO2 container for strings
 * \retval NULL if allocation failed
 */
struct ao2_container *ast_str_container_alloc_options(enum ao2_container_opts opts, int buckets);

/*!
 * \since 12
 * \brief Adds a string to a string container allocated by ast_str_container_alloc
 *
 * \param str_container The container to which to add a string
 * \param add The string to add to the container
 *
 * \retval zero on success
 * \retval non-zero if the operation failed
 */
int ast_str_container_add(struct ao2_container *str_container, const char *add);

/*!
 * \since 12
 * \brief Removes a string from a string container allocated by ast_str_container_alloc
 *
 * \param str_container The container from which to remove a string
 * \param remove The string to remove from the container
 */
void ast_str_container_remove(struct ao2_container *str_container, const char *remove);

/*!
 * \brief Create a pseudo-random string of a fixed length.
 *
 * This function is useful for generating a string whose randomness
 * does not need to be across all time and space, does not need to
 * be cryptographically secure, and needs to fit in a limited space.
 *
 * This function will write a null byte at the final position
 * in the buffer (buf[size - 1]). So if you pass in a size of
 * 10, then this will generate a random 9-character string.
 *
 * \param buf Buffer to write random string into.
 * \param size The size of the buffer.
 * \return A pointer to buf
 */
char *ast_generate_random_string(char *buf, size_t size);
#endif /* _ASTERISK_STRINGS_H */
