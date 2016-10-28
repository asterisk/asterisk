/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, CFWare, LLC
 *
 * Corey Farrell <git@cfware.com>
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
 * \brief PBX timing routines.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/localtime.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

/*! \brief Helper for get_range.
 * return the index of the matching entry, starting from 1.
 * If names is not supplied, try numeric values.
 */
static int lookup_name(const char *s, const char * const names[], int max)
{
	int i;

	if (names && *s > '9') {
		for (i = 0; names[i]; i++) {
			if (!strcasecmp(s, names[i])) {
				return i;
			}
		}
	}

	/* Allow months and weekdays to be specified as numbers, as well */
	if (sscanf(s, "%2d", &i) == 1 && i >= 1 && i <= max) {
		/* What the array offset would have been: "1" would be at offset 0 */
		return i - 1;
	}
	return -1; /* error return */
}

/*! \brief helper function to return a range up to max (7, 12, 31 respectively).
 * names, if supplied, is an array of names that should be mapped to numbers.
 */
static unsigned get_range(char *src, int max, const char * const names[], const char *msg)
{
	int start, end; /* start and ending position */
	unsigned int mask = 0;
	char *part;

	/* Check for whole range */
	if (ast_strlen_zero(src) || !strcmp(src, "*")) {
		return (1 << max) - 1;
	}

	while ((part = strsep(&src, "&"))) {
		/* Get start and ending position */
		char *endpart = strchr(part, '-');
		if (endpart) {
			*endpart++ = '\0';
		}
		/* Find the start */
		if ((start = lookup_name(part, names, max)) < 0) {
			ast_log(LOG_WARNING, "Invalid %s '%s', skipping element\n", msg, part);
			continue;
		}
		if (endpart) { /* find end of range */
			if ((end = lookup_name(endpart, names, max)) < 0) {
				ast_log(LOG_WARNING, "Invalid end %s '%s', skipping element\n", msg, endpart);
				continue;
			}
		} else {
			end = start;
		}
		/* Fill the mask. Remember that ranges are cyclic */
		mask |= (1 << end);   /* initialize with last element */
		while (start != end) {
			mask |= (1 << start);
			if (++start >= max) {
				start = 0;
			}
		}
	}
	return mask;
}

/*! \brief store a bitmask of valid times, one bit each 1 minute */
static void get_timerange(struct ast_timing *i, char *times)
{
	char *endpart, *part;
	int x;
	int st_h, st_m;
	int endh, endm;
	int minute_start, minute_end;

	/* start disabling all times, fill the fields with 0's, as they may contain garbage */
	memset(i->minmask, 0, sizeof(i->minmask));

	/* 1-minute per bit */
	/* Star is all times */
	if (ast_strlen_zero(times) || !strcmp(times, "*")) {
		/* 48, because each hour takes 2 integers; 30 bits each */
		for (x = 0; x < 48; x++) {
			i->minmask[x] = 0x3fffffff; /* 30 bits */
		}
		return;
	}
	/* Otherwise expect a range */
	while ((part = strsep(&times, "&"))) {
		if (!(endpart = strchr(part, '-'))) {
			if (sscanf(part, "%2d:%2d", &st_h, &st_m) != 2 || st_h < 0 || st_h > 23 || st_m < 0 || st_m > 59) {
				ast_log(LOG_WARNING, "%s isn't a valid time.\n", part);
				continue;
			}
			i->minmask[st_h * 2 + (st_m >= 30 ? 1 : 0)] |= (1 << (st_m % 30));
			continue;
		}
		*endpart++ = '\0';
		/* why skip non digits? Mostly to skip spaces */
		while (*endpart && !isdigit(*endpart)) {
			endpart++;
		}
		if (!*endpart) {
			ast_log(LOG_WARNING, "Invalid time range starting with '%s-'.\n", part);
			continue;
		}
		if (sscanf(part, "%2d:%2d", &st_h, &st_m) != 2 || st_h < 0 || st_h > 23 || st_m < 0 || st_m > 59) {
			ast_log(LOG_WARNING, "'%s' isn't a valid start time.\n", part);
			continue;
		}
		if (sscanf(endpart, "%2d:%2d", &endh, &endm) != 2 || endh < 0 || endh > 23 || endm < 0 || endm > 59) {
			ast_log(LOG_WARNING, "'%s' isn't a valid end time.\n", endpart);
			continue;
		}
		minute_start = st_h * 60 + st_m;
		minute_end = endh * 60 + endm;
		/* Go through the time and enable each appropriate bit */
		for (x = minute_start; x != minute_end; x = (x + 1) % (24 * 60)) {
			i->minmask[x / 30] |= (1 << (x % 30));
		}
		/* Do the last one */
		i->minmask[x / 30] |= (1 << (x % 30));
	}
	/* All done */
	return;
}

static const char * const days[] =
{
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",
	NULL,
};

static const char * const months[] =
{
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",
	NULL,
};

/*! /brief Build timing
 *
 * /param i info
 * /param info_in
 *
 */
int ast_build_timing(struct ast_timing *i, const char *info_in)
{
	char *info;
	int j, num_fields, last_sep = -1;

	i->timezone = NULL;

	/* Check for empty just in case */
	if (ast_strlen_zero(info_in)) {
		return 0;
	}

	/* make a copy just in case we were passed a static string */
	info = ast_strdupa(info_in);

	/* count the number of fields in the timespec */
	for (j = 0, num_fields = 1; info[j] != '\0'; j++) {
		if (info[j] == ',') {
			last_sep = j;
			num_fields++;
		}
	}

	/* save the timezone, if it is specified */
	if (num_fields == 5) {
		i->timezone = ast_strdup(info + last_sep + 1);
	}

	/* Assume everything except time */
	i->monthmask = 0xfff;	/* 12 bits */
	i->daymask = 0x7fffffffU; /* 31 bits */
	i->dowmask = 0x7f; /* 7 bits */
	/* on each call, use strsep() to move info to the next argument */
	get_timerange(i, strsep(&info, "|,"));
	if (info)
		i->dowmask = get_range(strsep(&info, "|,"), 7, days, "day of week");
	if (info)
		i->daymask = get_range(strsep(&info, "|,"), 31, NULL, "day");
	if (info)
		i->monthmask = get_range(strsep(&info, "|,"), 12, months, "month");
	return 1;
}

int ast_check_timing(const struct ast_timing *i)
{
	return ast_check_timing2(i, ast_tvnow());
}

int ast_check_timing2(const struct ast_timing *i, const struct timeval tv)
{
	struct ast_tm tm;

	ast_localtime(&tv, &tm, i->timezone);

	/* If it's not the right month, return */
	if (!(i->monthmask & (1 << tm.tm_mon)))
		return 0;

	/* If it's not that time of the month.... */
	/* Warning, tm_mday has range 1..31! */
	if (!(i->daymask & (1 << (tm.tm_mday-1))))
		return 0;

	/* If it's not the right day of the week */
	if (!(i->dowmask & (1 << tm.tm_wday)))
		return 0;

	/* Sanity check the hour just to be safe */
	if ((tm.tm_hour < 0) || (tm.tm_hour > 23)) {
		ast_log(LOG_WARNING, "Insane time...\n");
		return 0;
	}

	/* Now the tough part, we calculate if it fits
	   in the right time based on min/hour */
	if (!(i->minmask[tm.tm_hour * 2 + (tm.tm_min >= 30 ? 1 : 0)] & (1 << (tm.tm_min >= 30 ? tm.tm_min - 30 : tm.tm_min))))
		return 0;

	/* If we got this far, then we're good */
	return 1;
}

int ast_destroy_timing(struct ast_timing *i)
{
	if (i->timezone) {
		ast_free(i->timezone);
		i->timezone = NULL;
	}
	return 0;
}
