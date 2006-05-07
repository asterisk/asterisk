/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 *
 * \brief Compatibility functions for strsep and strtoq missing on Solaris 
 */

#include <sys/types.h>
#include <stdio.h>

#include "asterisk.h"

#include "asterisk/compat.h"

#ifndef HAVE_STRSEP
char *strsep(char **str, const char *delims)
{
    char *token;

    if (!*str) {
        /* No more tokens */
        return NULL;
    }

    token = *str;
    while (**str != '\0') {
        if (strchr(delims, **str)) {
            **str = '\0';
            (*str)++;
            return token;
        }
        (*str)++;
    }

    /* There is no other token */
    *str = NULL;

    return token;
}
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite)
{
	unsigned char *buf;
	int buflen;

	buflen = strlen(name) + strlen(value) + 2;
	if (!(buf = alloca(buflen)))
 		return -1;

	if (!overwrite && getenv(name))
		return 0;

	snprintf(buf, buflen, "%s=%s", name, value);

	return putenv(buf);
}
#endif

#ifndef HAVE_UNSETENV
int unsetenv(const char *name)
{
	return setenv(name, "", 0);
}
#endif
