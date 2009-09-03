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
 * \brief Utility functions
 */

#ifndef _ASTERISK_UTILS_H
#define _ASTERISK_UTILS_H

#include "asterisk/compat.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <arpa/inet.h>	/* we want to override inet_ntoa */
#include <netdb.h>
#include <limits.h>
#include <time.h>	/* we want to override localtime_r */
#include <unistd.h>

#include "asterisk/lock.h"
#include "asterisk/time.h"
#include "asterisk/strings.h"
#include "asterisk/logger.h"
#include "asterisk/compiler.h"
#include "asterisk/localtime.h"

/*! \note
 \verbatim
   Note:
   It is very important to use only unsigned variables to hold
   bit flags, as otherwise you can fall prey to the compiler's
   sign-extension antics if you try to use the top two bits in
   your variable.

   The flag macros below use a set of compiler tricks to verify
   that the caller is using an "unsigned int" variable to hold
   the flags, and nothing else. If the caller uses any other
   type of variable, a warning message similar to this:

   warning: comparison of distinct pointer types lacks cast
   will be generated.

   The "dummy" variable below is used to make these comparisons.

   Also note that at -O2 or above, this type-safety checking
   does _not_ produce any additional object code at all.
 \endverbatim
*/

extern unsigned int __unsigned_int_flags_dummy;

#define ast_test_flag(p,flag) 		({ \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags & (flag)); \
					})

#define ast_set_flag(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags |= (flag)); \
					} while(0)

#define ast_clear_flag(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags &= ~(flag)); \
					} while(0)

#define ast_copy_flags(dest,src,flagz)	do { \
					typeof ((dest)->flags) __d = (dest)->flags; \
					typeof ((src)->flags) __s = (src)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__d == &__x); \
					(void) (&__s == &__x); \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define ast_set2_flag(p,value,flag)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

#define ast_set_flags_to(p,flag,value)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					(p)->flags &= ~(flag); \
					(p)->flags |= (value); \
					} while (0)

/* Non-type checking variations for non-unsigned int flags.  You
   should only use non-unsigned int flags where required by 
   protocol etc and if you know what you're doing :)  */
#define ast_test_flag_nonstd(p,flag) \
					((p)->flags & (flag))

#define ast_set_flag_nonstd(p,flag) 		do { \
					((p)->flags |= (flag)); \
					} while(0)

#define ast_clear_flag_nonstd(p,flag) 		do { \
					((p)->flags &= ~(flag)); \
					} while(0)

#define ast_copy_flags_nonstd(dest,src,flagz)	do { \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define ast_set2_flag_nonstd(p,value,flag)	do { \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

#define AST_FLAGS_ALL UINT_MAX

struct ast_flags {
	unsigned int flags;
};

struct ast_hostent {
	struct hostent hp;
	char buf[1024];
};

struct hostent *ast_gethostbyname(const char *host, struct ast_hostent *hp);

/* ast_md5_hash 
	\brief Produces MD5 hash based on input string */
void ast_md5_hash(char *output, char *input);
/* ast_sha1_hash
	\brief Produces SHA1 hash based on input string */
void ast_sha1_hash(char *output, char *input);

int ast_base64encode_full(char *dst, const unsigned char *src, int srclen, int max, int linebreaks);
int ast_base64encode(char *dst, const unsigned char *src, int srclen, int max);
int ast_base64decode(unsigned char *dst, const char *src, int max);

/*! ast_uri_encode
	\brief Turn text string to URI-encoded %XX version 
 	At this point, we're converting from ISO-8859-x (8-bit), not UTF8
	as in the SIP protocol spec 
	If doreserved == 1 we will convert reserved characters also.
	RFC 2396, section 2.4
	outbuf needs to have more memory allocated than the instring
	to have room for the expansion. Every char that is converted
	is replaced by three ASCII characters.
	\param string	String to be converted
	\param outbuf	Resulting encoded string
	\param buflen	Size of output buffer
	\param doreserved	Convert reserved characters
*/

char *ast_uri_encode(const char *string, char *outbuf, int buflen, int doreserved);

/*!	\brief Decode URI, URN, URL (overwrite string)
	\param s	String to be decoded 
 */
void ast_uri_decode(char *s);

static force_inline void ast_slinear_saturated_add(short *input, short *value)
{
	int res;

	res = (int) *input + *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32767)
		*input = -32767;
	else
		*input = (short) res;
}
	
static force_inline void ast_slinear_saturated_multiply(short *input, short *value)
{
	int res;

	res = (int) *input * *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32767)
		*input = -32767;
	else
		*input = (short) res;
}

static force_inline void ast_slinear_saturated_divide(short *input, short *value)
{
	*input /= *value;
}

/*!
 * \brief thread-safe replacement for inet_ntoa().
 *
 * \note It is very important to note that even though this is a thread-safe
 *       replacement for inet_ntoa(), it is *not* reentrant.  In a single
 *       thread, the result from a previous call to this function is no longer
 *       valid once it is called again.  If the result from multiple calls to
 *       this function need to be kept or used at once, then the result must be
 *       copied to a local buffer before calling this function again.
 */
const char *ast_inet_ntoa(struct in_addr ia);

#ifdef inet_ntoa
#undef inet_ntoa
#endif
#define inet_ntoa __dont__use__inet_ntoa__use__ast_inet_ntoa__instead__

#ifdef localtime_r
#undef localtime_r
#endif
#define localtime_r __dont_use_localtime_r_use_ast_localtime_instead__

int ast_utils_init(void);
int ast_wait_for_input(int fd, int ms);

/*! ast_carefulwrite
	\brief Try to write string, but wait no more than ms milliseconds
	before timing out.

	\note If you are calling ast_carefulwrite, it is assumed that you are calling
	it on a file descriptor that _DOES_ have NONBLOCK set.  This way,
	there is only one system call made to do a write, unless we actually
	have a need to wait.  This way, we get better performance.
*/
int ast_carefulwrite(int fd, char *s, int len, int timeoutms);

/*! Compares the source address and port of two sockaddr_in */
static force_inline int inaddrcmp(const struct sockaddr_in *sin1, const struct sockaddr_in *sin2)
{
	return ((sin1->sin_addr.s_addr != sin2->sin_addr.s_addr) 
		|| (sin1->sin_port != sin2->sin_port));
}

#define AST_STACKSIZE (((sizeof(void *) * 8 * 8) - 16) * 1024)

#if defined(LOW_MEMORY)
#define AST_BACKGROUND_STACKSIZE (((sizeof(void *) * 8 * 2) - 16) * 1024)
#else
#define AST_BACKGROUND_STACKSIZE AST_STACKSIZE
#endif

void ast_register_thread(char *name);
void ast_unregister_thread(void *id);

int ast_pthread_create_stack(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *),
			     void *data, size_t stacksize, const char *file, const char *caller,
			     int line, const char *start_fn);

#define ast_pthread_create(a, b, c, d) ast_pthread_create_stack(a, b, c, d,			\
							        0,				\
	 						        __FILE__, __FUNCTION__,		\
 							        __LINE__, #c)

#define ast_pthread_create_background(a, b, c, d) ast_pthread_create_stack(a, b, c, d,			\
									   AST_BACKGROUND_STACKSIZE,	\
									   __FILE__, __FUNCTION__,	\
									   __LINE__, #c)

/*!
	\brief Process a string to find and replace characters
	\param start The string to analyze
	\param find The character to find
	\param replace_with The character that will replace the one we are looking for
*/
char *ast_process_quotes_and_slashes(char *start, char find, char replace_with);

#ifdef linux
#define ast_random random
#else
long int ast_random(void);
#endif

/*! 
 * \brief free() wrapper
 *
 * ast_free_ptr should be used when a function pointer for free() needs to be passed
 * as the argument to a function. Otherwise, astmm will cause seg faults.
 */
#ifdef __AST_DEBUG_MALLOC
static void ast_free_ptr(void *ptr) attribute_unused;
static void ast_free_ptr(void *ptr)
{
	free(ptr);
}
#else
#define ast_free free
#define ast_free_ptr ast_free
#endif

#ifndef __AST_DEBUG_MALLOC

#define MALLOC_FAILURE_MSG \
	ast_log(LOG_ERROR, "Memory Allocation Failure in function %s at line %d of %s\n", func, lineno, file);
/*!
 * \brief A wrapper for malloc()
 *
 * ast_malloc() is a wrapper for malloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The argument and return value are the same as malloc()
 */
#define ast_malloc(len) \
	_ast_malloc((len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
void * attribute_malloc _ast_malloc(size_t len, const char *file, int lineno, const char *func),
{
	void *p;

	if (!(p = malloc(len)))
		MALLOC_FAILURE_MSG;

	return p;
}
)

/*!
 * \brief A wrapper for calloc()
 *
 * ast_calloc() is a wrapper for calloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as calloc()
 */
#define ast_calloc(num, len) \
	_ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
void * attribute_malloc _ast_calloc(size_t num, size_t len, const char *file, int lineno, const char *func),
{
	void *p;

	if (!(p = calloc(num, len)))
		MALLOC_FAILURE_MSG;

	return p;
}
)

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
	_ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for realloc()
 *
 * ast_realloc() is a wrapper for realloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as realloc()
 */
#define ast_realloc(p, len) \
	_ast_realloc((p), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
void * attribute_malloc _ast_realloc(void *p, size_t len, const char *file, int lineno, const char *func),
{
	void *newp;

	if (!(newp = realloc(p, len)))
		MALLOC_FAILURE_MSG;

	return newp;
}
)

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
	_ast_strdup((str), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
char * attribute_malloc _ast_strdup(const char *str, const char *file, int lineno, const char *func),
{
	char *newstr = NULL;

	if (str) {
		if (!(newstr = strdup(str)))
			MALLOC_FAILURE_MSG;
	}

	return newstr;
}
)

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
	_ast_strndup((str), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
char * attribute_malloc _ast_strndup(const char *str, size_t len, const char *file, int lineno, const char *func),
{
	char *newstr = NULL;

	if (str) {
		if (!(newstr = strndup(str, len)))
			MALLOC_FAILURE_MSG;
	}

	return newstr;
}
)

/*!
 * \brief A wrapper for asprintf()
 *
 * ast_asprintf() is a wrapper for asprintf() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as asprintf()
 */
#define ast_asprintf(ret, fmt, ...) \
	_ast_asprintf((ret), __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, __VA_ARGS__)

int _ast_asprintf(char **ret, const char *file, int lineno, const char *func, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

/*!
 * \brief A wrapper for vasprintf()
 *
 * ast_vasprintf() is a wrapper for vasprintf() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as vasprintf()
 */
#define ast_vasprintf(ret, fmt, ap) \
	_ast_vasprintf((ret), __FILE__, __LINE__, __PRETTY_FUNCTION__, (fmt), (ap))

AST_INLINE_API(
int __attribute__((format(printf, 5, 0))) _ast_vasprintf(char **ret, const char *file, int lineno, const char *func, const char *fmt, va_list ap),
{
	int res;

	if ((res = vasprintf(ret, fmt, ap)) == -1)
		MALLOC_FAILURE_MSG;

	return res;
}
)

#endif /* AST_DEBUG_MALLOC */

#if !defined(ast_strdupa) && defined(__GNUC__)
/*!
  \brief duplicate a string in memory from the stack
  \param s The string to duplicate

  This macro will duplicate the given string.  It returns a pointer to the stack
  allocatted memory for the new string.
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

/*!
  \brief Disable PMTU discovery on a socket
  \param sock The socket to manipulate
  \return Nothing

  On Linux, UDP sockets default to sending packets with the Dont Fragment (DF)
  bit set. This is supposedly done to allow the application to do PMTU
  discovery, but Asterisk does not do this.

  Because of this, UDP packets sent by Asterisk that are larger than the MTU
  of any hop in the path will be lost. This function can be called on a socket
  to ensure that the DF bit will not be set.
 */
void ast_enable_packet_fragmentation(int sock);

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

#ifdef AST_DEVMODE
#define ast_assert(a) _ast_assert(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
static void force_inline _ast_assert(int condition, const char *condition_str, 
	const char *file, int line, const char *function)
{
	if (__builtin_expect(!condition, 1)) {
		/* Attempt to put it into the logger, but hope that at least someone saw the
		 * message on stderr ... */
		ast_log(LOG_ERROR, "FRACK!, Failed assertion %s (%d) at line %d in %s of %s\n",
			condition_str, condition, line, function, file);
		fprintf(stderr, "FRACK!, Failed assertion %s (%d) at line %d in %s of %s\n",
			condition_str, condition, line, function, file);
		/* Give the logger a chance to get the message out, just in case we abort(), or
		 * Asterisk crashes due to whatever problem just happened after we exit ast_assert(). */
		usleep(1);
#ifdef DO_CRASH
		abort();
		/* Just in case abort() doesn't work or something else super silly,
		 * and for Qwell's amusement. */
		*((int*)0)=0;
#endif
	}
}
#else
#define ast_assert(a)
#endif

#endif /* _ASTERISK_UTILS_H */
