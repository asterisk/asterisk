/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Tilghman Lesher <tlesher@vcch.com>
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
 * \brief Custom localtime functions for multiple timezones
 */

#ifndef _ASTERISK_LOCALTIME_H
#define _ASTERISK_LOCALTIME_H

struct ast_tm {
	int tm_sec;             /*!< Seconds. [0-60] (1 leap second) */
	int tm_min;             /*!< Minutes. [0-59] */
	int tm_hour;            /*!< Hours.   [0-23] */
	int tm_mday;            /*!< Day.     [1-31] */
	int tm_mon;             /*!< Month.   [0-11] */
	int tm_year;            /*!< Year - 1900.  */
	int tm_wday;            /*!< Day of week. [0-6] */
	int tm_yday;            /*!< Days in year.[0-365]	*/
	int tm_isdst;           /*!< DST.		[-1/0/1]*/
	long int tm_gmtoff;     /*!< Seconds east of UTC.  */
	char *tm_zone;          /*!< Timezone abbreviation.  */
	/* NOTE: do NOT reorder this final item.  The order needs to remain compatible with struct tm */
	int tm_usec;        /*!< microseconds */
};

/*!\brief Timezone-independent version of localtime_r(3).
 * \param timep Current time, including microseconds
 * \param p_tm Pointer to memory where the broken-out time will be stored
 * \param zone Text string of a standard system zoneinfo file.  If NULL, the system localtime will be used.
 * \retval p_tm is returned for convenience
 */
struct ast_tm *ast_localtime(const struct timeval *timep, struct ast_tm *p_tm, const char *zone);

void ast_get_dst_info(const time_t * const timep, int *dst_enabled, time_t *dst_start, time_t *dst_end, int *gmt_off, const char * const zone);

/*!\brief Timezone-independent version of mktime(3).
 * \param tmp Current broken-out time, including microseconds
 * \param zone Text string of a standard system zoneinfo file.  If NULL, the system localtime will be used.
 * \retval A structure containing both seconds and fractional thereof since January 1st, 1970 UTC
 */
struct timeval ast_mktime(struct ast_tm * const tmp, const char *zone);

/*!\brief Special version of strftime(3) that handles fractions of a second.
 * Takes the same arguments as strftime(3), with the addition of %q, which
 * specifies microseconds.
 * \param buf Address in memory where the resulting string will be stored.
 * \param len Size of the chunk of memory buf.
 * \param format A string specifying the format of time to be placed into buf.
 * \param tm Pointer to the broken out time to be used for the format.
 * \retval An integer value specifying the number of bytes placed into buf or -1 on error.
 */
int ast_strftime(char *buf, size_t len, const char *format, const struct ast_tm *tm);

/*!\brief Special version of strptime(3) which places the answer in the common
 * structure ast_tm.  Also, unlike strptime(3), ast_strptime() initializes its
 * memory prior to use.
 * \param s A string specifying some portion of a date and time.
 * \param format The format in which the string, s, is expected.
 * \param tm The broken-out time structure into which the parsed data is expected.
 * \retval A pointer to the first character within s not used to parse the date and time.
 */
char *ast_strptime(const char *s, const char *format, struct ast_tm *tm);

#endif /* _ASTERISK_LOCALTIME_H */
