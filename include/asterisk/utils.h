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

#include "asterisk/network.h"

#include <time.h>	/* we want to override localtime_r */
#include <unistd.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/time.h"
#include "asterisk/logger.h"
#include "asterisk/localtime.h"
#include "asterisk/stringfields.h"

/*!
\note \verbatim
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


/* The following 64-bit flag code can most likely be erased after app_dial
   is reorganized to either reduce the large number of options, or handle
   them in some other way. At the time of this writing, app_dial would be
   the only user of 64-bit option flags */

extern uint64_t __unsigned_int_flags_dummy64;

#define ast_test_flag64(p,flag) 		({ \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy64) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags & (flag)); \
					})

#define ast_set_flag64(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy64) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags |= (flag)); \
					} while(0)

#define ast_clear_flag64(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy64) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags &= ~(flag)); \
					} while(0)

#define ast_copy_flags64(dest,src,flagz)	do { \
					typeof ((dest)->flags) __d = (dest)->flags; \
					typeof ((src)->flags) __s = (src)->flags; \
					typeof (__unsigned_int_flags_dummy64) __x = 0; \
					(void) (&__d == &__x); \
					(void) (&__s == &__x); \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define ast_set2_flag64(p,value,flag)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy64) __x = 0; \
					(void) (&__p == &__x); \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

#define ast_set_flags_to64(p,flag,value)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy64) __x = 0; \
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

/*! \brief Structure used to handle boolean flags */
struct ast_flags {
	unsigned int flags;
};

/*! \brief Structure used to handle a large number of boolean flags == used only in app_dial? */
struct ast_flags64 {
	uint64_t flags;
};

struct ast_hostent {
	struct hostent hp;
	char buf[1024];
};

/*! \brief Thread-safe gethostbyname function to use in Asterisk */
struct hostent *ast_gethostbyname(const char *host, struct ast_hostent *hp);

/*! \brief Produces MD5 hash based on input string */
void ast_md5_hash(char *output, const char *input);
/*! \brief Produces SHA1 hash based on input string */
void ast_sha1_hash(char *output, const char *input);
/*! \brief Produces SHA1 hash based on input string, stored in uint8_t array */
void ast_sha1_hash_uint(uint8_t *digest, const char *input);

int ast_base64encode_full(char *dst, const unsigned char *src, int srclen, int max, int linebreaks);

#undef MIN
#define MIN(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); ((__a > __b) ? __b : __a);})
#undef MAX
#define MAX(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); ((__a < __b) ? __b : __a);})

#define SWAP(a,b) do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

/*!
 * \brief Encode data in base64
 * \param dst the destination buffer
 * \param src the source data to be encoded
 * \param srclen the number of bytes present in the source buffer
 * \param max the maximum number of bytes to write into the destination
 *        buffer, *including* the terminating NULL character.
 */
int ast_base64encode(char *dst, const unsigned char *src, int srclen, int max);

/*!
 * \brief Decode data from base64
 * \param dst the destination buffer
 * \param src the source buffer
 * \param max The maximum number of bytes to write into the destination
 *            buffer.  Note that this function will not ensure that the
 *            destination buffer is NULL terminated.  So, in general,
 *            this parameter should be sizeof(dst) - 1.
 */
int ast_base64decode(unsigned char *dst, const char *src, int max);

#define AST_URI_ALPHANUM     (1 << 0)
#define AST_URI_MARK         (1 << 1)
#define AST_URI_UNRESERVED   (AST_URI_ALPHANUM | AST_URI_MARK)
#define AST_URI_LEGACY_SPACE (1 << 2)

#define AST_URI_SIP_USER_UNRESERVED (1 << 20)

extern const struct ast_flags ast_uri_http;
extern const struct ast_flags ast_uri_http_legacy;
extern const struct ast_flags ast_uri_sip_user;

/*!
 * \brief Turn text string to URI-encoded %XX version
 *
 * This function encodes characters according to the rules presented in RFC
 * 2396 and/or RFC 3261 section 19.1.2 and section 25.1.
 *
 * Outbuf needs to have more memory allocated than the instring to have room
 * for the expansion. Every byte that is converted is replaced by three ASCII
 * characters.
 *
 * \param string string to be converted
 * \param outbuf resulting encoded string
 * \param buflen size of output buffer
 * \param spec flags describing how the encoding should be performed
 * \return a pointer to the uri encoded string
 */
char *ast_uri_encode(const char *string, char *outbuf, int buflen, struct ast_flags spec);

/*!
 * \brief Decode URI, URN, URL (overwrite string)
 *
 * \note The ast_uri_http_legacy decode spec flag will cause this function to
 * decode '+' as ' '.
 *
 * \param s string to be decoded
 * \param spec flags describing how the decoding should be performed
 */
void ast_uri_decode(char *s, struct ast_flags spec);

/*! ast_xml_escape
	\brief Escape reserved characters for use in XML.

	If \a outbuf is too short, the output string will be truncated.
	Regardless, the output will always be null terminated.

	\param string String to be converted
	\param outbuf Resulting encoded string
	\param buflen Size of output buffer
	\return 0 for success
	\return -1 if buflen is too short.
 */
int ast_xml_escape(const char *string, char *outbuf, size_t buflen);

/*!
 * \brief Escape characters found in a quoted string.
 *
 * \note This function escapes quoted characters based on the 'qdtext' set of
 * allowed characters from RFC 3261 section 25.1.
 *
 * \param string string to be escaped
 * \param outbuf resulting escaped string
 * \param buflen size of output buffer
 * \return a pointer to the escaped string
 */
char *ast_escape_quoted(const char *string, char *outbuf, int buflen);

/*!
 * \brief Escape semicolons found in a string.
 *
 * \param string string to be escaped
 * \param outbuf resulting escaped string
 * \param buflen size of output buffer
 * \return a pointer to the escaped string
 */
char *ast_escape_semicolons(const char *string, char *outbuf, int buflen);

/*!
 * \brief Unescape quotes in a string
 *
 * \param quote_str The string with quotes to be unescaped
 *
 * \note This function mutates the passed-in string.
 */
void ast_unescape_quoted(char *quote_str);

static force_inline void ast_slinear_saturated_add(short *input, short *value)
{
	int res;

	res = (int) *input + *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32768)
		*input = -32768;
	else
		*input = (short) res;
}

static force_inline void ast_slinear_saturated_subtract(short *input, short *value)
{
	int res;

	res = (int) *input - *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32768)
		*input = -32768;
	else
		*input = (short) res;
}

static force_inline void ast_slinear_saturated_multiply(short *input, short *value)
{
	int res;

	res = (int) *input * *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32768)
		*input = -32768;
	else
		*input = (short) res;
}

static force_inline void ast_slinear_saturated_multiply_float(short *input, float *value)
{
	float res;

	res = (float) *input * *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32768)
		*input = -32768;
	else
		*input = (short) res;
}

static force_inline void ast_slinear_saturated_divide(short *input, short *value)
{
	*input /= *value;
}

static force_inline void ast_slinear_saturated_divide_float(short *input, float *value)
{
	float res = (float) *input / *value;
	if (res > 32767)
		*input = 32767;
	else if (res < -32768)
		*input = -32768;
	else
		*input = (short) res;
}

#ifdef localtime_r
#undef localtime_r
#endif
#define localtime_r __dont_use_localtime_r_use_ast_localtime_instead__

int ast_utils_init(void);
int ast_wait_for_input(int fd, int ms);
int ast_wait_for_output(int fd, int ms);

/*!
 * \brief Try to write string, but wait no more than ms milliseconds
 * before timing out.
 *
 * \note If you are calling ast_carefulwrite, it is assumed that you are calling
 * it on a file descriptor that _DOES_ have NONBLOCK set.  This way,
 * there is only one system call made to do a write, unless we actually
 * have a need to wait.  This way, we get better performance.
 */
int ast_carefulwrite(int fd, char *s, int len, int timeoutms);

/*!
 * \brief Write data to a file stream with a timeout
 *
 * \param f the file stream to write to
 * \param fd the file description to poll on to know when the file stream can
 *        be written to without blocking.
 * \param s the buffer to write from
 * \param len the number of bytes to write
 * \param timeoutms The maximum amount of time to block in this function trying
 *        to write, specified in milliseconds.
 *
 * \note This function assumes that the associated file stream has been set up
 *       as non-blocking.
 *
 * \retval 0 success
 * \retval -1 error
 */
int ast_careful_fwrite(FILE *f, int fd, const char *s, size_t len, int timeoutms);

/*
 * Thread management support (should be moved to lock.h or a different header)
 */

#if defined(PTHREAD_STACK_MIN)
# define AST_STACKSIZE     MAX((((sizeof(void *) * 8 * 8) - 16) * 1024), PTHREAD_STACK_MIN)
# define AST_STACKSIZE_LOW MAX((((sizeof(void *) * 8 * 2) - 16) * 1024), PTHREAD_STACK_MIN)
#else
# define AST_STACKSIZE     (((sizeof(void *) * 8 * 8) - 16) * 1024)
# define AST_STACKSIZE_LOW (((sizeof(void *) * 8 * 2) - 16) * 1024)
#endif

int ast_background_stacksize(void);

#define AST_BACKGROUND_STACKSIZE ast_background_stacksize()

void ast_register_thread(char *name);
void ast_unregister_thread(void *id);

int ast_pthread_create_stack(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *),
			     void *data, size_t stacksize, const char *file, const char *caller,
			     int line, const char *start_fn);

int ast_pthread_create_detached_stack(pthread_t *thread, pthread_attr_t *attr, void*(*start_routine)(void *),
				 void *data, size_t stacksize, const char *file, const char *caller,
				 int line, const char *start_fn);

#define ast_pthread_create(a, b, c, d) 				\
	ast_pthread_create_stack(a, b, c, d,			\
		0, __FILE__, __FUNCTION__, __LINE__, #c)

#define ast_pthread_create_detached(a, b, c, d)			\
	ast_pthread_create_detached_stack(a, b, c, d,		\
		0, __FILE__, __FUNCTION__, __LINE__, #c)

#define ast_pthread_create_background(a, b, c, d)		\
	ast_pthread_create_stack(a, b, c, d,			\
		AST_BACKGROUND_STACKSIZE,			\
		__FILE__, __FUNCTION__, __LINE__, #c)

#define ast_pthread_create_detached_background(a, b, c, d)	\
	ast_pthread_create_detached_stack(a, b, c, d,		\
		AST_BACKGROUND_STACKSIZE,			\
		__FILE__, __FUNCTION__, __LINE__, #c)

/* End of thread management support */

/*!
 * \brief Replace '^' in a string with ','
 * \param s String within which to replace characters
 */
void ast_replace_subargument_delimiter(char *s);

/*!
 * \brief Process a string to find and replace characters
 * \param start The string to analyze
 * \param find The character to find
 * \param replace_with The character that will replace the one we are looking for
 */
char *ast_process_quotes_and_slashes(char *start, char find, char replace_with);

long int ast_random(void);

/*!
 * \brief Returns a random number between 0.0 and 1.0, inclusive.
 * \since 12
 */
#define ast_random_double() (((double)ast_random()) / RAND_MAX)

/*!
 * \brief Disable PMTU discovery on a socket
 * \param sock The socket to manipulate
 * \return Nothing
 *
 * On Linux, UDP sockets default to sending packets with the Dont Fragment (DF)
 * bit set. This is supposedly done to allow the application to do PMTU
 * discovery, but Asterisk does not do this.
 *
 * Because of this, UDP packets sent by Asterisk that are larger than the MTU
 * of any hop in the path will be lost. This function can be called on a socket
 * to ensure that the DF bit will not be set.
 */
void ast_enable_packet_fragmentation(int sock);

/*!
 * \brief Recursively create directory path
 * \param path The directory path to create
 * \param mode The permissions with which to try to create the directory
 * \return 0 on success or an error code otherwise
 *
 * Creates a directory path, creating parent directories as needed.
 */
int ast_mkdir(const char *path, int mode);

/*!
 * \brief Recursively create directory path, but only if it resolves within
 * the given \a base_path.
 *
 * If \a base_path does not exist, it will not be created and this function
 * returns \c EPERM.
 *
 * \param path The directory path to create
 * \param mode The permissions with which to try to create the directory
 * \return 0 on success or an error code otherwise
 */
int ast_safe_mkdir(const char *base_path, const char *path, int mode);

#define ARRAY_LEN(a) (size_t) (sizeof(a) / sizeof(0[a]))

/*!
 * \brief Checks to see if value is within the given bounds
 *
 * \param v the value to check
 * \param min minimum lower bound (inclusive)
 * \param max maximum upper bound (inclusive)
 * \return 0 if value out of bounds, otherwise true (non-zero)
 */
#define IN_BOUNDS(v, min, max) ((v) >= (min)) && ((v) <= (max))

/*!
 * \brief Checks to see if value is within the bounds of the given array
 *
 * \param v the value to check
 * \param a the array to bound check
 * \return 0 if value out of bounds, otherwise true (non-zero)
 */
#define ARRAY_IN_BOUNDS(v, a) IN_BOUNDS((int) (v), 0, ARRAY_LEN(a) - 1)

/* Definition for Digest authorization */
struct ast_http_digest {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(username);
		AST_STRING_FIELD(nonce);
		AST_STRING_FIELD(uri);
		AST_STRING_FIELD(realm);
		AST_STRING_FIELD(domain);
		AST_STRING_FIELD(response);
		AST_STRING_FIELD(cnonce);
		AST_STRING_FIELD(opaque);
		AST_STRING_FIELD(nc);
	);
	int qop;		/* Flag set to 1, if we send/recv qop="quth" */
};

/*!
 * \brief Parse digest authorization header.
 * \return Returns -1 if we have no auth or something wrong with digest.
 * \note This function may be used for Digest request and responce header.
 * request arg is set to nonzero, if we parse Digest Request.
 * pedantic arg can be set to nonzero if we need to do addition Digest check.
 */
int ast_parse_digest(const char *digest, struct ast_http_digest *d, int request, int pedantic);

#ifdef DO_CRASH
#define DO_CRASH_NORETURN attribute_noreturn
#else
#define DO_CRASH_NORETURN
#endif

void DO_CRASH_NORETURN __ast_assert_failed(int condition, const char *condition_str,
	const char *file, int line, const char *function);

#ifdef AST_DEVMODE
#define ast_assert(a) _ast_assert(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_assert_return(a, ...) \
({ \
	if (__builtin_expect(!(a), 1)) { \
		_ast_assert(0, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__); \
		return __VA_ARGS__; \
	}\
})
static void force_inline _ast_assert(int condition, const char *condition_str, const char *file, int line, const char *function)
{
	if (__builtin_expect(!condition, 1)) {
		__ast_assert_failed(condition, condition_str, file, line, function);
	}
}
#else
#define ast_assert(a)
#define ast_assert_return(a, ...) \
({ \
	if (__builtin_expect(!(a), 1)) { \
		return __VA_ARGS__; \
	}\
})
#endif

/*!
 * \brief Force a crash if DO_CRASH is defined.
 *
 * \note If DO_CRASH is not defined then the function returns.
 *
 * \return Nothing
 */
void DO_CRASH_NORETURN ast_do_crash(void);

#include "asterisk/strings.h"

/*!
 * \brief Return the number of bytes used in the alignment of type.
 * \param type
 * \return The number of bytes required for alignment.
 *
 * This is really just __alignof__(), but tucked away in this header so we
 * don't have to look at the nasty underscores in the source.
 */
#define ast_alignof(type) __alignof__(type)

/*!
 * \brief Increase offset so it is a multiple of the required alignment of type.
 * \param offset The value that should be increased.
 * \param type The data type that offset should be aligned to.
 * \return The smallest multiple of alignof(type) larger than or equal to offset.
 * \see ast_make_room_for()
 *
 * Many systems prefer integers to be stored on aligned on memory locations.
 * This macro will increase an offset so a value of the supplied type can be
 * safely be stored on such a memory location.
 *
 * Examples:
 * ast_align_for(0x17, int64_t) ==> 0x18
 * ast_align_for(0x18, int64_t) ==> 0x18
 * ast_align_for(0x19, int64_t) ==> 0x20
 *
 * Don't mind the ugliness, the compiler will optimize it.
 */
#define ast_align_for(offset, type) (((offset + __alignof__(type) - 1) / __alignof__(type)) * __alignof__(type))

/*!
 * \brief Increase offset by the required alignment of type and make sure it is
 *        a multiple of said alignment.
 * \param offset The value that should be increased.
 * \param type The data type that room should be reserved for.
 * \return The smallest multiple of alignof(type) larger than or equal to offset
 *         plus alignof(type).
 * \see ast_align_for()
 *
 * A use case for this is when prepending length fields of type int to a buffer.
 * If you keep the offset a multiple of the alignment of the integer type,
 * a next block of length+buffer will have the length field automatically
 * aligned.
 *
 * Examples:
 * ast_make_room_for(0x17, int64_t) ==> 0x20
 * ast_make_room_for(0x18, int64_t) ==> 0x20
 * ast_make_room_for(0x19, int64_t) ==> 0x28
 *
 * Don't mind the ugliness, the compiler will optimize it.
 */
#define ast_make_room_for(offset, type) (((offset + (2 * __alignof__(type) - 1)) / __alignof__(type)) * __alignof__(type))

/*!
 * \brief An Entity ID is essentially a MAC address, brief and unique
 */
struct ast_eid {
	unsigned char eid[6];
} __attribute__((__packed__));

/*!
 * \brief Global EID
 *
 * This is set in asterisk.conf, or determined automatically by taking the mac
 * address of an Ethernet interface on the system.
 */
extern struct ast_eid ast_eid_default;

/*!
 * \brief Fill in an ast_eid with the default eid of this machine
 * \since 1.6.1
 */
void ast_set_default_eid(struct ast_eid *eid);

/*!
 * \brief Convert an EID to a string
 * \since 1.6.1
 */
char *ast_eid_to_str(char *s, int maxlen, struct ast_eid *eid);

/*!
 * \brief Convert a string into an EID
 *
 * This function expects an EID in the format:
 *    00:11:22:33:44:55
 *
 * \return 0 success, non-zero failure
 * \since 1.6.1
 */
int ast_str_to_eid(struct ast_eid *eid, const char *s);

/*!
 * \brief Compare two EIDs
 *
 * \return 0 if the two are the same, non-zero otherwise
 * \since 1.6.1
 */
int ast_eid_cmp(const struct ast_eid *eid1, const struct ast_eid *eid2);

/*!
 * \brief Check if EID is empty
 *
 * \return 1 if the EID is empty, zero otherwise
 * \since 13.12.0
 */
int ast_eid_is_empty(const struct ast_eid *eid);

/*!
 * \brief Get current thread ID
 * \return the ID if platform is supported, else -1
 */
int ast_get_tid(void);

/*!
 * \brief Resolve a binary to a full pathname
 * \param binary Name of the executable to resolve
 * \param fullpath Buffer to hold the complete pathname
 * \param fullpath_size Size of \a fullpath
 * \retval NULL \a binary was not found or the environment variable PATH is not set
 * \return \a fullpath
 */
char *ast_utils_which(const char *binary, char *fullpath, size_t fullpath_size);

/*!
 * \brief Declare a variable that will call a destructor function when it goes out of scope.
 *
 * Resource Allocation Is Initialization (RAII) variable declaration.
 *
 * \since 11.0
 * \param vartype The type of the variable
 * \param varname The name of the variable
 * \param initval The initial value of the variable
 * \param dtor The destructor function of type' void func(vartype *)'
 *
 * \code
 * void mything_cleanup(struct mything *t)
 * {
 *     if (t) {
 *         ast_free(t->stuff);
 *     }
 * }
 *
 * void do_stuff(const char *name)
 * {
 *     RAII_VAR(struct mything *, thing, mything_alloc(name), mything_cleanup);
 *     ...
 * }
 * \endcode
 *
 * \note This macro is especially useful for working with ao2 objects. A common idiom
 * would be a function that needed to look up an ao2 object and might have several error
 * conditions after the allocation that would normally need to unref the ao2 object.
 * With RAII_VAR, it is possible to just return and leave the cleanup to the destructor
 * function. For example:
 *
 * \code
 * void do_stuff(const char *name)
 * {
 *     RAII_VAR(struct mything *, thing, find_mything(name), ao2_cleanup);
 *     if (!thing) {
 *         return;
 *     }
 *     if (error) {
 *         return;
 *     }
 *     do_stuff_with_thing(thing);
 * }
 * \endcode
 */

#if defined(__clang__)
typedef void (^_raii_cleanup_block_t)(void);
static inline void _raii_cleanup_block(_raii_cleanup_block_t *b) { (*b)(); }

#define RAII_VAR(vartype, varname, initval, dtor)                                                              \
    __block vartype varname = initval;                                                                         \
    _raii_cleanup_block_t _raii_cleanup_ ## varname __attribute__((cleanup(_raii_cleanup_block),unused)) =     \
        ^{ {(void)dtor(varname);} };

#elif defined(__GNUC__)

#define RAII_VAR(vartype, varname, initval, dtor)                              \
    auto void _dtor_ ## varname (vartype * v);                                 \
    void _dtor_ ## varname (vartype * v) { dtor(*v); }                         \
    vartype varname __attribute__((cleanup(_dtor_ ## varname))) = (initval)

#else
    #error "Cannot compile Asterisk: unknown and unsupported compiler."
#endif /* #if __GNUC__ */

/*!
 * \brief Asterisk wrapper around crypt(3).
 *
 * The interpretation of the salt (which determines the password hashing
 * algorithm) is system specific. Application code should prefer to use
 * ast_crypt_encrypt() or ast_crypt_validate().
 *
 * The returned string is heap allocated, and should be freed with ast_free().
 *
 * \param key User's password to crypt.
 * \param salt Salt to crypt with.
 * \return Crypted password.
 * \return \c NULL on error.
 */
char *ast_crypt(const char *key, const char *salt);

/*!
 * \brief Asterisk wrapper around crypt(3) for encrypting passwords.
 *
 * This function will generate a random salt and encrypt the given password.
 *
 * The returned string is heap allocated, and should be freed with ast_free().
 *
 * \param key User's password to crypt.
 * \return Crypted password.
 * \return \c NULL on error.
 */
char *ast_crypt_encrypt(const char *key);

/*!
 * \brief Asterisk wrapper around crypt(3) for validating passwords.
 *
 * \param key User's password to validate.
 * \param expected Expected result from crypt.
 * \return True (non-zero) if \a key matches \a expected.
 * \return False (zero) if \a key doesn't match.
 */
int ast_crypt_validate(const char *key, const char *expected);

/*!
 * \brief Test that a file exists and is readable by the effective user.
 * \since 13.7.0
 *
 * \param filename File to test.
 * \return True (non-zero) if the file exists and is readable.
 * \return False (zero) if the file either doesn't exists or is not readable.
 */
int ast_file_is_readable(const char *filename);

/*!
 * \brief Compare 2 major.minor.patch.extra version strings.
 * \since 13.7.0
 *
 * \param version1.
 * \param version2.
 *
 * \return <0 if version 1 < version 2.
 * \return =0 if version 1 = version 2.
 * \return >0 if version 1 > version 2.
 */
int ast_compare_versions(const char *version1, const char *version2);

/*!
 * \brief Test that an OS supports IPv6 Networking.
 * \since 13.14.0
 *
 * \return True (non-zero) if the IPv6 supported.
 * \return False (zero) if the OS doesn't support IPv6.
 */
int ast_check_ipv6(void);

enum ast_fd_flag_operation {
	AST_FD_FLAG_SET,
	AST_FD_FLAG_CLEAR,
};

/*!
 * \brief Set flags on the given file descriptor
 * \since 13.19
 *
 * If getting or setting flags of the given file descriptor fails, logs an
 * error message.
 *
 * \param fd File descriptor to set flags on
 * \param flags The flag(s) to set
 *
 * \return -1 on error
 * \return 0 if successful
 */
#define ast_fd_set_flags(fd, flags) \
	__ast_fd_set_flags((fd), (flags), AST_FD_FLAG_SET, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Clear flags on the given file descriptor
 * \since 13.19
 *
 * If getting or setting flags of the given file descriptor fails, logs an
 * error message.
 *
 * \param fd File descriptor to clear flags on
 * \param flags The flag(s) to clear
 *
 * \return -1 on error
 * \return 0 if successful
 */
#define ast_fd_clear_flags(fd, flags) \
	__ast_fd_set_flags((fd), (flags), AST_FD_FLAG_CLEAR, __FILE__, __LINE__, __PRETTY_FUNCTION__)

int __ast_fd_set_flags(int fd, int flags, enum ast_fd_flag_operation op,
	const char *file, int lineno, const char *function);

/*!
 * \brief Create a non-blocking socket
 * \since 13.25
 *
 * Wrapper around socket(2) that sets the O_NONBLOCK flag on the resulting
 * socket.
 *
 * \details
 * For parameter and return information, see the man page for
 * socket(2).
 */
#ifdef HAVE_SOCK_NONBLOCK
# define ast_socket_nonblock(domain, type, protocol) socket((domain), (type) | SOCK_NONBLOCK, (protocol))
#else
int ast_socket_nonblock(int domain, int type, int protocol);
#endif

/*!
 * \brief Create a non-blocking pipe
 * \since 13.25
 *
 * Wrapper around pipe(2) that sets the O_NONBLOCK flag on the resulting
 * file descriptors.
 *
 * \details
 * For parameter and return information, see the man page for
 * pipe(2).
 */
#ifdef HAVE_PIPE2
# define ast_pipe_nonblock(filedes) pipe2((filedes), O_NONBLOCK)
#else
int ast_pipe_nonblock(int filedes[2]);
#endif

/*!
 * \brief Set the current thread's user interface status.
 *
 * \param is_user_interface Non-zero to mark the thread as a user interface.
 *
 * \return 0 if successfuly marked current thread.
 * \return Non-zero if marking current thread failed.
 */
int ast_thread_user_interface_set(int is_user_interface);

/*!
 * \brief Indicates whether the current thread is a user interface
 *
 * \return True (non-zero) if thread is a user interface.
 * \return False (zero) if thread is not a user interface.
 */
int ast_thread_is_user_interface(void);

#endif /* _ASTERISK_UTILS_H */
