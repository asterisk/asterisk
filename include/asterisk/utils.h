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

static inline int ast_strlen_zero(const char *s)
{
	return (*s == '\0');
}

#endif
