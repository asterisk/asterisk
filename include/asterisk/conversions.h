/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
 * \brief Conversion utility functions
 */

#ifndef _ASTERISK_CONVERSIONS_H
#define _ASTERISK_CONVERSIONS_H

#include <stdint.h>

/*!
 * \brief Convert the given string to an unsigned integer
 *
 * This function will return failure for the following reasons:
 *
 *   The given string to convert is NULL
 *   The given string to convert is empty.
 *   The given string to convert is negative (starts with a '-')
 *   The given string to convert contains non numeric values
 *   Once converted the number is out of range (greater than UINT_MAX)
 *
 * \param str The string to convert
 * \param res [out] The converted value
 *
 * \returns -1 if it fails to convert, 0 on success
 */
int ast_str_to_uint(const char *str, unsigned int *res);

/*!
 * \brief Convert the given string to an unsigned long
 *
 * This function will return failure for the following reasons:
 *
 *   The given string to convert is NULL
 *   The given string to convert is empty.
 *   The given string to convert is negative (starts with a '-')
 *   The given string to convert contains non numeric values
 *   Once converted the number is out of range (greater than ULONG_MAX)
 *
 * \param str The string to convert
 * \param res [out] The converted value
 *
 * \returns -1 if it fails to convert, 0 on success
 */
int ast_str_to_ulong(const char *str, unsigned long *res);

/*!
 * \brief Convert the given string to an unsigned max size integer
 *
 * This function will return failure for the following reasons:
 *
 *   The given string to convert is NULL
 *   The given string to convert is empty.
 *   The given string to convert is negative (starts with a '-')
 *   The given string to convert contains non numeric values
 *   Once converted the number is out of range (greater than UINTMAX_MAX)
 *
 * \param str The string to convert
 * \param res [out] The converted value
 *
 * \returns -1 if it fails to convert, 0 on success
 */
int ast_str_to_umax(const char *str, uintmax_t *res);

#endif /* _ASTERISK_CONVERSIONS_H */
