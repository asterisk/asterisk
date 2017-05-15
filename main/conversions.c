/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
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
 * \brief Conversion utility functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "asterisk/conversions.h"

static int str_is_negative(const char *str)
{
	/* Ignore any preceding white space */
	while (isspace(*str) && *++str);
	return *str == '-';
}

int ast_str_to_uint(const char *str, unsigned int *res)
{
	unsigned long val;

	if (ast_str_to_ulong(str, &val) || val > UINT_MAX) {
		return -1;
	}

	*res = val;
	return 0;
}

int ast_str_to_ulong(const char *str, unsigned long *res)
{
	char *end;
	unsigned long val;

	if (!str || str_is_negative(str)) {
		return -1;
	}

	errno = 0;
	val = strtoul(str, &end, 0);

	/*
	 * If str equals end then no digits were found. If end is not pointing to
	 * a null character then the string contained some numbers that could be
	 * converted, but some characters that could not, which we'll consider
	 * invalid.
	 */
	if ((str == end || *end != '\0' || (errno == ERANGE && val == ULONG_MAX))) {
		return -1;
	}

	*res = val;
	return 0;

}
