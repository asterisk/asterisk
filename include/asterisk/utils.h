/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Utility functions
 *
 * Copyright (C) 2004, Digium
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_UTIL_H
#define _ASTERISK_UTIL_H

#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

static inline int ast_strlen_zero(const char *s)
{
	return (*s == '\0');
}

struct ast_hostent {
	struct hostent hp;
	char buf[1024];
};

extern struct hostent *ast_gethostbyname(const char *host, struct ast_hostent *hp);
extern int ast_base64encode(char *dst, unsigned char *src, int srclen, int max);
extern int ast_base64decode(unsigned char *dst, char *src, int max);

extern int test_for_thread_safety(void);
extern const char *ast_inet_ntoa(char *buf, int bufsiz, struct in_addr ia);
extern int ast_utils_init(void);

#ifdef inet_ntoa
#undef inet_ntoa
#endif
#define inet_ntoa __dont__use__inet_ntoa__use__ast_inet_ntoa__instead__

#ifdef LINUX
#define ast_pthread_create pthread_create
#define ast_strcasestr strcasestr
#else
/* Linux threads have a default 2MB stack size. */
#ifndef PTHREAD_ATTR_STACKSIZE
#define	PTHREAD_ATTR_STACKSIZE		2097152
#endif /* PTHREAD_ATTR_STACKSIZE */
extern int ast_pthread_create(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), void *data);
#endif /* LINUX */

extern char *ast_strcasestr(const char *, const char *);

#endif
