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

/*!
 * \brief Computes the difference (in milliseconds) between two \c struct \c timeval instances.
 * \param end the beginning of the time period
 * \param start the end of the time period
 * \return the difference in milliseconds
 */
int ast_tvdiff_ms(const struct timeval *end, const struct timeval *start);
#if !defined(LOW_MEMORY) && !defined(AST_API_MODULE)
extern inline
#endif
#if !defined(LOW_MEMORY) || defined(AST_API_MODULE)
int ast_tvdiff_ms(const struct timeval *end, const struct timeval *start)
{
	return ((end->tv_sec - start->tv_sec) * 1000) + ((end->tv_usec - start->tv_usec) / 1000);
}
#endif

#undef AST_API_MODULE
#endif /* _ASTERISK_TIME_H */
