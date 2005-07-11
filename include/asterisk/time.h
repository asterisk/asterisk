/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Time-related functions and macros
 *
 * Copyright (C) 2004 - 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_TIME_H
#define _ASTERISK_TIME_H

#include <sys/time.h>

#include "asterisk/inline_api.h"

/*!
 * \brief Computes the difference (in milliseconds) between two \c struct \c timeval instances.
 * \param end the beginning of the time period
 * \param start the end of the time period
 * \return the difference in milliseconds
 */
AST_INLINE_API(
int ast_tvdiff_ms(const struct timeval *end, const struct timeval *start),
{
	return ((end->tv_sec - start->tv_sec) * 1000) + ((end->tv_usec - start->tv_usec) / 1000);
}
)

#endif /* _ASTERISK_TIME_H */
