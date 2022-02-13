/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
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
 * \brief Date/Time utility functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "asterisk/time.h"

const char *nanosecond_labels[] = {"ns", "nsec", "nanosecond"};
const char *microsecond_labels[] = {"us", "usec", "microsecond"};
const char *millisecond_labels[] = {"ms", "msec", "millisecond"};
const char *second_labels[] = {"s", "sec", "second"};
const char *minute_labels[] = {"m", "min", "minute"};
const char *hour_labels[] = {"h", "hr", "hour"};
const char *day_labels[] = {"d", "", "day"};
const char *week_labels[] = {"w", "wk", "week"};
const char *month_labels[] = {"mo", "mth", "month"};
const char *year_labels[] = {"y", "yr", "year"};

#define MAX_UNIT_LABELS 3

struct time_unit_labels {
	enum TIME_UNIT unit;
	const char **values;
};

static struct time_unit_labels unit_labels[] = {
	{ TIME_UNIT_NANOSECOND, nanosecond_labels },
	{ TIME_UNIT_MICROSECOND, microsecond_labels },
	{ TIME_UNIT_MILLISECOND, millisecond_labels },
	{ TIME_UNIT_MONTH, month_labels }, /* Here so "mo" matches before "m" */
	{ TIME_UNIT_SECOND, second_labels },
	{ TIME_UNIT_MINUTE, minute_labels },
	{ TIME_UNIT_HOUR, hour_labels },
	{ TIME_UNIT_DAY, day_labels },
	{ TIME_UNIT_WEEK, week_labels },
	{ TIME_UNIT_YEAR, year_labels },
};

const unsigned int unit_labels_size = sizeof(unit_labels) / sizeof(0[unit_labels]);

enum TIME_UNIT ast_time_str_to_unit(const char *unit)
{
	size_t i, j;

	if (!unit) {
		return TIME_UNIT_ERROR;
	}

	for (i = 0; i < unit_labels_size; ++i) {
		for (j = 0; j < MAX_UNIT_LABELS; ++j) {
			/*
			 * A lazy pluralization check. If the given unit string at least starts
			 * with a label assume a match.
			 */
			if (*unit_labels[i].values[j] && !strncasecmp(unit, unit_labels[i].values[j],
					strlen(unit_labels[i].values[j]))) {
				return unit_labels[i].unit;
			}
		}
	}

	return TIME_UNIT_ERROR;
}

ast_suseconds_t ast_time_tv_to_usec(const struct timeval *tv)
{
	return tv->tv_sec * 1000000 + tv->tv_usec;
}

struct timeval ast_time_create(ast_time_t sec, ast_suseconds_t usec)
{
	return ast_tv(sec, usec);
}

/*!
 * \brief Create a timeval first converting the given microsecond value
 *        into seconds and microseconds
 *
 * \param usec microsecond value
 *
 * \return A timeval structure
 */
static struct timeval normalize_and_create(unsigned long usec)
{
	return ast_time_create(usec / 1000000, usec % 1000000);
}

struct timeval ast_time_create_by_unit(unsigned long val, enum TIME_UNIT unit)
{
	switch (unit) {
	case TIME_UNIT_NANOSECOND:
		return normalize_and_create(val / 1000);
	case TIME_UNIT_MICROSECOND:
		return normalize_and_create(val);
	case TIME_UNIT_MILLISECOND:
		return normalize_and_create(val * 1000);
	case TIME_UNIT_SECOND:
		return ast_time_create(val, 0);
	case TIME_UNIT_MINUTE:
		return ast_time_create(val * 60, 0);
	case TIME_UNIT_HOUR:
		return ast_time_create(val * 3600, 0);
	case TIME_UNIT_DAY:
		return ast_time_create(val * 86400, 0);
	case TIME_UNIT_WEEK:
		return ast_time_create(val * 604800, 0);
	case TIME_UNIT_MONTH:
		/* Using Gregorian mean month - 30.436875 * 86400 */
		return ast_time_create(val * 2629746, 0);
	case TIME_UNIT_YEAR:
		/* Using Gregorian year - 365.2425 * 86400 */
		return ast_time_create(val * 31556952, 0);
	default:
		return ast_time_create(0, 0);
	}
}

struct timeval ast_time_create_by_unit_str(unsigned long val, const char *unit)
{
	return ast_time_create_by_unit(val, ast_time_str_to_unit(unit));
}

/*!
 * \brief Returns a string representation of a time_t as decimal seconds
 * since the epoch.
 */
int ast_time_t_to_string(time_t time, char *buf, size_t length)
{
	struct tm tm;

	localtime_r(&time, &tm);
	return (strftime(buf, length, "%s", &tm) == 0) ? -1 : 0;
}

/*!
 * \brief Returns a time_t from a string containing seconds since the epoch.
 */
time_t ast_string_to_time_t(const char *str)
{
	struct tm tm = { 0, };

	/* handle leading spaces */
	if (strptime(str, " %s", &tm) == NULL) {
		return (time_t)-1;
	}
	tm.tm_isdst = -1;
	return mktime(&tm);
}

