/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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

/*!
 * \file
 * \brief Timezone tests
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

#ifndef TZDIR
#ifdef SOLARIS
#define TZDIR	"/usr/share/lib/zoneinfo"
#else
#define TZDIR	"/usr/share/zoneinfo"
#endif /* defined SOLARIS */
#endif /* !defined TZDIR */

AST_TEST_DEFINE(test_timezone_watch)
{
	const char *zones[] = { "America/Chicago", "America/New_York" };
	int type, i, res = AST_TEST_PASS;
	struct timeval tv = ast_tvnow();
	struct ast_tm atm[ARRAY_LEN(zones)];
	char tmpdir[] = "/tmp/timezone.XXXXXX";
	char tzfile[50], syscmd[256];

	switch (cmd) {
	case TEST_INIT:
		info->name = "timezone_watch";
		info->category = "/main/stdtime/";
		info->summary = "Verify deleting timezone file purges cache";
		info->description =
			"Verifies that the caching engine properly destroys a timezone entry when its file is deleted.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!mkdtemp(tmpdir)) {
		ast_test_status_update(test, "Unable to create working directory: %s\n", strerror(errno));
		return AST_TEST_NOT_RUN;
	}
	snprintf(tzfile, sizeof(tzfile), "%s/test", tmpdir);

	for (type = 0; type <
#ifdef SOLARIS
			1 /* Solaris doesn't use symlinks for timezones */
#else
			2
#endif
				; type++) {
		ast_test_status_update(test, "Executing %s test...\n", type == 0 ? "deletion" : "symlink");
		for (i = 0; i < ARRAY_LEN(zones); i++) {
			int system_res;
			snprintf(syscmd, sizeof(syscmd), "%s " TZDIR "/%s %s", type == 0 ? "cp" : "ln -sf", zones[i], tzfile);
			if ((system_res = ast_safe_system(syscmd))) {
				ast_log(LOG_WARNING, "system(%s) returned non-zero: %d\n", syscmd, system_res);
			}
			ast_localtime_wakeup_monitor(test);
			ast_test_status_update(test, "Querying timezone %s\n", tzfile);
			ast_localtime(&tv, &atm[i], tzfile);
			if (i != 0) {
				if (atm[i].tm_hour == atm[i - 1].tm_hour) {
					if (atm[i].tm_isdst == atm[i - 1].tm_isdst) {
						res = AST_TEST_FAIL;
						ast_test_status_update(test, "Failed %s test: %d(%s) = %d(%s)\n", type == 0 ? "deletion" : "symlink", atm[i].tm_hour, zones[i], atm[i-1].tm_hour, zones[i-1]);
					} else {
						ast_log(LOG_WARNING, "DST transition during %s test: %d(%s/%d) != %d(%s/%d)\n", type == 0 ? "deletion" : "symlink", atm[i].tm_hour, zones[i], atm[i].tm_isdst, atm[i-1].tm_hour, zones[i-1], atm[i-1].tm_isdst);
					}
				}
			}

			if (i + 1 != ARRAY_LEN(zones)) {
				/* stat(2) only has resolution to 1 second - must wait, or the mtime is the same */
				usleep(1100000);
			}
		}
	}

	snprintf(syscmd, sizeof(syscmd), "rm -rf %s", tmpdir);
	if (ast_safe_system(syscmd)) {
		ast_log(LOG_WARNING, "system(%s) returned non-zero.\n", syscmd);
	}

	return res;
}

AST_TEST_DEFINE(test_time_str_to_unit)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "time_str_to_unit";
		info->category = "/main/stdtime/";
		info->summary = "Verify string to time unit conversions";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Nominal */
	ast_test_validate(test, ast_time_str_to_unit("ns") == TIME_UNIT_NANOSECOND);
	ast_test_validate(test, ast_time_str_to_unit("us") == TIME_UNIT_MICROSECOND);
	ast_test_validate(test, ast_time_str_to_unit("ms") == TIME_UNIT_MILLISECOND);
	ast_test_validate(test, ast_time_str_to_unit("s") == TIME_UNIT_SECOND);
	ast_test_validate(test, ast_time_str_to_unit("m") == TIME_UNIT_MINUTE);
	ast_test_validate(test, ast_time_str_to_unit("h") == TIME_UNIT_HOUR);
	ast_test_validate(test, ast_time_str_to_unit("d") == TIME_UNIT_DAY);
	ast_test_validate(test, ast_time_str_to_unit("w") == TIME_UNIT_WEEK);
	ast_test_validate(test, ast_time_str_to_unit("mo") == TIME_UNIT_MONTH);
	ast_test_validate(test, ast_time_str_to_unit("y") == TIME_UNIT_YEAR);

	/* Plural */
	ast_test_validate(test, ast_time_str_to_unit("nanoseconds") == TIME_UNIT_NANOSECOND);
	ast_test_validate(test, ast_time_str_to_unit("microseconds") == TIME_UNIT_MICROSECOND);
	ast_test_validate(test, ast_time_str_to_unit("milliseconds") == TIME_UNIT_MILLISECOND);
	ast_test_validate(test, ast_time_str_to_unit("seconds") == TIME_UNIT_SECOND);
	ast_test_validate(test, ast_time_str_to_unit("minutes") == TIME_UNIT_MINUTE);
	ast_test_validate(test, ast_time_str_to_unit("hours") == TIME_UNIT_HOUR);
	ast_test_validate(test, ast_time_str_to_unit("days") == TIME_UNIT_DAY);
	ast_test_validate(test, ast_time_str_to_unit("weeks") == TIME_UNIT_WEEK);
	ast_test_validate(test, ast_time_str_to_unit("months") == TIME_UNIT_MONTH);
	ast_test_validate(test, ast_time_str_to_unit("years") == TIME_UNIT_YEAR);

	/* Case */
	ast_test_validate(test, ast_time_str_to_unit("Nsec") == TIME_UNIT_NANOSECOND);
	ast_test_validate(test, ast_time_str_to_unit("Usec") == TIME_UNIT_MICROSECOND);
	ast_test_validate(test, ast_time_str_to_unit("Msec") == TIME_UNIT_MILLISECOND);
	ast_test_validate(test, ast_time_str_to_unit("Sec") == TIME_UNIT_SECOND);
	ast_test_validate(test, ast_time_str_to_unit("Min") == TIME_UNIT_MINUTE);
	ast_test_validate(test, ast_time_str_to_unit("Hr") == TIME_UNIT_HOUR);
	ast_test_validate(test, ast_time_str_to_unit("Day") == TIME_UNIT_DAY);
	ast_test_validate(test, ast_time_str_to_unit("Wk") == TIME_UNIT_WEEK);
	ast_test_validate(test, ast_time_str_to_unit("Mth") == TIME_UNIT_MONTH);
	ast_test_validate(test, ast_time_str_to_unit("Yr") == TIME_UNIT_YEAR);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_time_create_by_unit)
{
	struct timeval tv;

	switch (cmd) {
	case TEST_INIT:
		info->name = "time_create_by_unit";
		info->category = "/main/stdtime/";
		info->summary = "Verify unit value to timeval conversions";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Nominal */
	ast_test_validate(test, ast_time_create_by_unit(1000, TIME_UNIT_NANOSECOND).tv_usec == 1);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_MICROSECOND).tv_usec == 1);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_MILLISECOND).tv_usec == 1000);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_SECOND).tv_sec == 1);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_MINUTE).tv_sec == 60);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_HOUR).tv_sec == 3600);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_DAY).tv_sec == 86400);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_WEEK).tv_sec == 604800);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_MONTH).tv_sec == 2629746);
	ast_test_validate(test, ast_time_create_by_unit(1, TIME_UNIT_YEAR).tv_sec == 31556952);

	/* timeval normalization */
	tv = ast_time_create_by_unit(1500000000, TIME_UNIT_NANOSECOND);
	ast_test_validate(test, tv.tv_sec == 1 && tv.tv_usec == 500000);

	tv = ast_time_create_by_unit(1500000, TIME_UNIT_MICROSECOND);
	ast_test_validate(test, tv.tv_sec == 1 && tv.tv_usec == 500000);

	tv = ast_time_create_by_unit(1500, TIME_UNIT_MILLISECOND);
	ast_test_validate(test, tv.tv_sec == 1 && tv.tv_usec == 500000);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_time_create_by_unit_str)
{
	struct timeval tv;

	switch (cmd) {
	case TEST_INIT:
		info->name = "time_create_by_unit_str";
		info->category = "/main/stdtime/";
		info->summary = "Verify value with unit as a string to timeval conversions";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Nominal */
	ast_test_validate(test, ast_time_create_by_unit_str(1000, "ns").tv_usec == 1);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "us").tv_usec == 1);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "ms").tv_usec == 1000);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "s").tv_sec == 1);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "m").tv_sec == 60);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "h").tv_sec == 3600);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "d").tv_sec == 86400);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "w").tv_sec == 604800);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "mo").tv_sec == 2629746);
	ast_test_validate(test, ast_time_create_by_unit_str(1, "yr").tv_sec == 31556952);

	/* timeval normalization */
	tv = ast_time_create_by_unit_str(1500000000, "ns");
	ast_test_validate(test, tv.tv_sec == 1 && tv.tv_usec == 500000);

	tv = ast_time_create_by_unit_str(1500000, "us");
	ast_test_validate(test, tv.tv_sec == 1 && tv.tv_usec == 500000);

	tv = ast_time_create_by_unit_str(1500, "ms");
	ast_test_validate(test, tv.tv_sec == 1 && tv.tv_usec == 500000);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_time_tv_to_usec)
{
	struct timeval tv;

	switch (cmd) {
	case TEST_INIT:
		info->name = "time_tv_to_usec";
		info->category = "/main/stdtime/";
		info->summary = "Verify conversion of a timeval structure to microseconds";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tv = ast_time_create(0, 0);
	ast_test_validate(test, ast_time_tv_to_usec(&tv) == 0);

	tv = ast_time_create(0, 1);
	ast_test_validate(test, ast_time_tv_to_usec(&tv) == 1);

	tv = ast_time_create(1, 0);
	ast_test_validate(test, ast_time_tv_to_usec(&tv) == 1000000);

	tv = ast_time_create(1, 1);
	ast_test_validate(test, ast_time_tv_to_usec(&tv) == 1000001);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_time_create_by_unit_str);
	AST_TEST_UNREGISTER(test_time_create_by_unit);
	AST_TEST_UNREGISTER(test_time_str_to_unit);
	AST_TEST_UNREGISTER(test_time_tv_to_usec);
	AST_TEST_UNREGISTER(test_timezone_watch);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_timezone_watch);
	AST_TEST_REGISTER(test_time_tv_to_usec);
	AST_TEST_REGISTER(test_time_str_to_unit);
	AST_TEST_REGISTER(test_time_create_by_unit);
	AST_TEST_REGISTER(test_time_create_by_unit_str);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Time Tests");
