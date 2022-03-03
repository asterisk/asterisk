/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Time-related functions and macros
 */

#ifndef _ASTERISK_TIME_H
#define _ASTERISK_TIME_H

#include "asterisk/autoconfig.h"

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "asterisk/inline_api.h"

/* We have to let the compiler learn what types to use for the elements of a
   struct timeval since on linux, it's time_t and suseconds_t, but on *BSD,
   they are just a long.
   note:dummy_tv_var_for_types never actually gets exported, only used as
   local place holder. */
extern struct timeval dummy_tv_var_for_types;
typedef typeof(dummy_tv_var_for_types.tv_sec) ast_time_t;
typedef typeof(dummy_tv_var_for_types.tv_usec) ast_suseconds_t;

/*!
 * \brief Computes the difference (in seconds) between two \c struct \c timeval instances.
 * \param end the end of the time period
 * \param start the beginning of the time period
 * \return the difference in seconds
 */
AST_INLINE_API(
int64_t ast_tvdiff_sec(struct timeval end, struct timeval start),
{
	int64_t result = end.tv_sec - start.tv_sec;
	if (result > 0 && end.tv_usec < start.tv_usec)
		result--;
	else if (result < 0 && end.tv_usec > start.tv_usec)
		result++;

	return result;
}
)

/*!
 * \brief Computes the difference (in microseconds) between two \c struct \c timeval instances.
 * \param end the end of the time period
 * \param start the beginning of the time period
 * \return the difference in microseconds
 */
AST_INLINE_API(
int64_t ast_tvdiff_us(struct timeval end, struct timeval start),
{
	return (end.tv_sec - start.tv_sec) * (int64_t) 1000000 +
		end.tv_usec - start.tv_usec;
}
)

/*!
 * \brief Computes the difference (in milliseconds) between two \c struct \c timeval instances.
 * \param end end of the time period
 * \param start beginning of the time period
 * \return the difference in milliseconds
 */
AST_INLINE_API(
int64_t ast_tvdiff_ms(struct timeval end, struct timeval start),
{
	/* the offset by 1,000,000 below is intentional...
	   it avoids differences in the way that division
	   is handled for positive and negative numbers, by ensuring
	   that the divisor is always positive
	*/
	int64_t sec_dif = (int64_t)(end.tv_sec - start.tv_sec) * 1000;
	int64_t usec_dif = (1000000 + end.tv_usec - start.tv_usec) / 1000 - 1000;
	return  sec_dif + usec_dif;
}
)

/*!
 * \brief Returns true if the argument is 0,0
 */
AST_INLINE_API(
int ast_tvzero(const struct timeval t),
{
	return (t.tv_sec == 0 && t.tv_usec == 0);
}
)

/*!
 * \brief Compress two \c struct \c timeval instances returning
 * -1, 0, 1 if the first arg is smaller, equal or greater to the second.
 */
AST_INLINE_API(
int ast_tvcmp(struct timeval _a, struct timeval _b),
{
	if (_a.tv_sec < _b.tv_sec)
		return -1;
	if (_a.tv_sec > _b.tv_sec)
		return 1;
	/* now seconds are equal */
	if (_a.tv_usec < _b.tv_usec)
		return -1;
	if (_a.tv_usec > _b.tv_usec)
		return 1;
	return 0;
}
)

/*!
 * \brief Returns true if the two \c struct \c timeval arguments are equal.
 */
AST_INLINE_API(
int ast_tveq(struct timeval _a, struct timeval _b),
{
	return (_a.tv_sec == _b.tv_sec && _a.tv_usec == _b.tv_usec);
}
)

/*!
 * \brief Returns current timeval. Meant to replace calls to gettimeofday().
 */
AST_INLINE_API(
struct timeval ast_tvnow(void),
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return t;
}
)

/*!
 * \brief Returns current timespec. Meant to avoid calling ast_tvnow() just to
 * create a timespec from the timeval it returns.
 */
#if defined _POSIX_TIMERS && _POSIX_TIMERS > 0
AST_INLINE_API(
struct timespec ast_tsnow(void),
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts;
}
)
#else
AST_INLINE_API(
struct timespec ast_tsnow(void),
{
	struct timeval tv = ast_tvnow();
	struct timespec ts;
	/* Can't use designated initializer, because it does odd things with
	 * the AST_INLINE_API macro. Go figure. */
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;
	return ts;
}
)
#endif

/*!
 * \brief Returns the sum of two timevals a + b
 */
struct timeval ast_tvadd(struct timeval a, struct timeval b);

/*!
 * \brief Returns the difference of two timevals a - b
 */
struct timeval ast_tvsub(struct timeval a, struct timeval b);

/*!
 * \since 12
 * \brief Formats a duration into HH:MM:SS
 *
 * \param duration The time (in seconds) to format
 * \param buf A buffer to hold the formatted string'
 * \param length The size of the buffer
 */
void ast_format_duration_hh_mm_ss(int duration, char *buf, size_t length);


/*!
 * \brief Calculate remaining milliseconds given a starting timestamp
 * and upper bound
 *
 * If the upper bound is negative, then this indicates that there is no
 * upper bound on the amount of time to wait. This will result in a
 * negative return.
 *
 * \param start When timing started being calculated
 * \param max_ms The maximum number of milliseconds to wait from start. May be negative.
 * \return The number of milliseconds left to wait for. May be negative.
 */
int ast_remaining_ms(struct timeval start, int max_ms);

/*!
 * \brief Returns a timeval from sec, usec
 */
AST_INLINE_API(
struct timeval ast_tv(ast_time_t sec, ast_suseconds_t usec),
{
	struct timeval t;
	t.tv_sec = sec;
	t.tv_usec = usec;
	return t;
}
)

/*!
 * \brief Returns a timeval corresponding to the duration of n samples at rate r.
 * Useful to convert samples to timevals, or even milliseconds to timevals
 * in the form ast_samp2tv(milliseconds, 1000)
 */
AST_INLINE_API(
struct timeval ast_samp2tv(unsigned int _nsamp, unsigned int _rate),
{
	return ast_tv(_nsamp / _rate, (_nsamp % _rate) * (1000000 / (float) _rate));
}
)

/*!
 * \brief Time units enumeration.
 */
enum TIME_UNIT {
	TIME_UNIT_ERROR = -1,
	TIME_UNIT_NANOSECOND,
	TIME_UNIT_MICROSECOND,
	TIME_UNIT_MILLISECOND,
	TIME_UNIT_SECOND,
	TIME_UNIT_MINUTE,
	TIME_UNIT_HOUR,
	TIME_UNIT_DAY,
	TIME_UNIT_WEEK,
	TIME_UNIT_MONTH,
	TIME_UNIT_YEAR,
};

/*!
 * \brief Convert a string to a time unit enumeration value.
 *
 * This method attempts to be as flexible, and forgiving as possible when
 * converting. In most cases the algorithm will match on the beginning of
 * up to three strings (short, medium, long form). So that means if the
 * given string at least starts with one of the form values it will match.
 *
 * For example: us, usec, microsecond will all map to TIME_UNIT_MICROSECOND.
 * So will uss, usecs, microseconds, or even microsecondvals
 *
 * Matching is also not case sensitive.
 *
 * \param unit The string to map to an enumeration
 *
 * \return A time unit enumeration
 */
enum TIME_UNIT ast_time_str_to_unit(const char *unit);

/*!
 * \brief Convert a timeval structure to microseconds
 *
 * \param tv The timeval to convert
 *
 * \return The time in microseconds
 */
ast_suseconds_t ast_time_tv_to_usec(const struct timeval *tv);

/*!
 * \brief Create a timeval object initialized to given values.
 *
 * \param sec The timeval seconds value
 * \param usec The timeval microseconds value
 *
 * \return A timeval object
 */
struct timeval ast_time_create(ast_time_t sec, ast_suseconds_t usec);

/*!
 * \brief Convert the given unit value, and create a timeval object from it.
 *
 * \param val The value to convert to a timeval
 * \param unit The time unit type of val
 *
 * \return A timeval object
 */
struct timeval ast_time_create_by_unit(unsigned long val, enum TIME_UNIT unit);

/*!
 * \brief Convert the given unit value, and create a timeval object from it.
 *
 * This will first attempt to convert the unit from a string to a TIME_UNIT
 * enumeration. If that conversion fails then a zeroed out timeval object
 * is returned.
 *
 * \param val The value to convert to a timeval
 * \param unit The time unit type of val
 *
 * \return A timeval object
 */
struct timeval ast_time_create_by_unit_str(unsigned long val, const char *unit);

#endif /* _ASTERISK_TIME_H */
