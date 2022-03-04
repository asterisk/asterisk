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
#include <inttypes.h>
#include <stdio.h>

#include "asterisk/conversions.h"

static int str_is_negative(const char **str)
{
	/*
	 * Ignore any preceding white space. It's okay to move the pointer here
	 * since the converting function would do the same, i.e. skip white space.
	 */
	while (isspace(**str)) ++*str;
	return **str == '-';
}

int ast_str_to_int(const char *str, int *res)
{
	intmax_t val;

	if (ast_str_to_imax(str, &val) || val < INT_MIN || val > INT_MAX) {
		return -1;
	}

	*res = val;
	return 0;
}

int ast_str_to_uint(const char *str, unsigned int *res)
{
	uintmax_t val;

	if (ast_str_to_umax(str, &val) || val > UINT_MAX) {
		return -1;
	}

	*res = val;
	return 0;
}

int ast_str_to_long(const char *str, long *res)
{
	intmax_t val;

	if (ast_str_to_imax(str, &val) || val < LONG_MIN || val > LONG_MAX) {
		return -1;
	}

	*res = val;
	return 0;
}

int ast_str_to_ulong(const char *str, unsigned long *res)
{
	uintmax_t val;

	if (ast_str_to_umax(str, &val) || val > ULONG_MAX) {
		return -1;
	}

	*res = val;
	return 0;
}

int ast_str_to_imax(const char *str, intmax_t *res)
{
	char *end;
	intmax_t val;

	if (!str) {
		return -1;
	}

	errno = 0;
	val = strtoimax(str, &end, 10);

	/*
	 * If str equals end then no digits were found. If end is not pointing to
	 * a null character then the string contained some numbers that could be
	 * converted, but some characters that could not, which we'll consider
	 * invalid.
	 */
	if (str == end || *end != '\0' || (errno == ERANGE &&
			(val == INTMAX_MIN || val == INTMAX_MAX))) {
		return -1;
	}

	*res = val;
	return 0;
}

int ast_str_to_umax(const char *str, uintmax_t *res)
{
	char *end;
	uintmax_t val;

	if (!str || str_is_negative(&str)) {
		return -1;
	}

	errno = 0;
	val = strtoumax(str, &end, 10);

	/*
	 * If str equals end then no digits were found. If end is not pointing to
	 * a null character then the string contained some numbers that could be
	 * converted, but some characters that could not, which we'll consider
	 * invalid.
	 */
	if ((str == end || *end != '\0' || (errno == ERANGE && val == UINTMAX_MAX))) {
		return -1;
	}

	*res = val;
	return 0;
}
