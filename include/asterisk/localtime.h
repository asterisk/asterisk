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

struct ast_tm *ast_localtime(const struct timeval *timep, struct ast_tm *p_tm, const char *zone);
void ast_get_dst_info(const time_t * const timep, int *dst_enabled, time_t *dst_start, time_t *dst_end, int *gmt_off, const char * const zone);
struct timeval ast_mktime(struct ast_tm * const tmp, const char *zone);
int ast_strftime(char *buf, size_t len, const char *format, const struct ast_tm *tm);

#endif /* _ASTERISK_LOCALTIME_H */
