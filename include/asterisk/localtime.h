/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Custom localtime functions for multiple timezones
 * 
 * Copyright (C) 2003, Mark Spencer
 *
 * Tilghman Lesher <tlesher@vcch.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_LOCALTIME_H
#define _ASTERISK_LOCALTIME_H

extern int ast_tzsetwall(void);
extern void ast_tzset(const char *name);
extern struct tm *ast_localtime(const time_t *timep, struct tm *p_tm, const char *zone);
extern time_t ast_mktime(struct tm * const tmp, const char *zone);
extern char *ast_ctime(const time_t * const timep);
extern char *ast_ctime_r(const time_t * const timep, char *buf);

#endif
