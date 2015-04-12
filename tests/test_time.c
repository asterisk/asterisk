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

ASTERISK_REGISTER_FILE(__FILE__)

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
					res = AST_TEST_FAIL;
					ast_test_status_update(test, "Failed %s test: %d(%s) = %d(%s)\n", type == 0 ? "deletion" : "symlink", atm[i].tm_hour, zones[i], atm[i-1].tm_hour, zones[i-1]);
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

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_timezone_watch);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_timezone_watch);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Time Tests");
