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

#endif
