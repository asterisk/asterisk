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

#include <netdb.h>

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

#endif
