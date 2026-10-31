/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Kinsey Moore
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief Callerid Tests
 *
 * \author\verbatim Kinsey Moore <kmoore@digium.com> \endverbatim
 *
 * This is an Asterisk test module for callerid functionality
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/callerid.h"

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

struct cid_set {
	char *cid;
	char *name;
	char *number;
};

AST_TEST_DEFINE(parse_nominal)
{
	static const struct cid_set cid_sets[] = {
		{"\"name\" <number>", "name", "number"},
		{"\"   name  \" <number>", "   name  ", "number"},
		{"name <number>", "name", "number"},
		{"         name     <number>", "name", "number"},
		{"\"\" <number>", NULL, "number"},
		{"<number>", NULL, "number"},
		{"name", "name", NULL},
		{" name", "name", NULL},
		{"\"name\"", "name", NULL},
		{"\"*10\"", "*10", NULL},
		{" \"*10\"", "*10", NULL},
		{"\"name\" <>", "name", NULL},
		{"name <>", "name", NULL},
		{"1234", NULL, "1234"},
		{" 1234", NULL, "1234"},
		{"\"na\\\"me\" <number>", "na\"me", "number"},
	};
	char *name;
	char *number;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "parse_nominal";
		info->category = "/main/callerid/";
		info->summary = "Callerid nominal parse unit test";
		info->description =
			"This tests parsing of nominal callerid strings.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(cid_sets); i++) {
		RAII_VAR(char *, callerid, ast_strdup(cid_sets[i].cid), ast_free);

		ast_callerid_parse(callerid, &name, &number);
		if (!cid_sets[i].name == !ast_strlen_zero(name) || (cid_sets[i].name && strcmp(name, cid_sets[i].name))) {
			ast_test_status_update(test,
				"Expected callerid name '%s' instead of '%s'\n",
				cid_sets[i].name, name);
			return AST_TEST_FAIL;
		}

		if (!cid_sets[i].number == !ast_strlen_zero(number) || (cid_sets[i].number && strcmp(number, cid_sets[i].number))) {
			ast_test_status_update(test,
				"Expected callerid number '%s' instead of '%s'\n",
				cid_sets[i].number, number);
			return AST_TEST_FAIL;
		}
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(parse_off_nominal)
{
	static const struct cid_set cid_sets[] = {
		{"na\\\"me <number>", "na\"me", "number"},
		{"\"na\"me\" <number>", "na\"me", "number"},
		{"na\"me <number>", "na\"me", "number"},
		{"\"name <number>", "\"name", "number"},
		{"name <number", "name", "number"},
		{"\"name <number>\"", "name", "number"},
	};
	char *name;
	char *number;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "parse_off_nominal";
		info->category = "/main/callerid/";
		info->summary = "Callerid off-nominal parse unit test";
		info->description =
			"This tests parsing of off-nominal callerid strings.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(cid_sets); i++) {
		RAII_VAR(char *, callerid, ast_strdup(cid_sets[i].cid), ast_free);

		ast_callerid_parse(callerid, &name, &number);
		if (!cid_sets[i].name == !ast_strlen_zero(name) || (cid_sets[i].name && strcmp(name, cid_sets[i].name))) {
			ast_test_status_update(test,
				"Expected callerid name '%s' instead of '%s'\n",
				cid_sets[i].name, name);
			return AST_TEST_FAIL;
		}

		if (!cid_sets[i].number == !ast_strlen_zero(number) || (cid_sets[i].number && strcmp(number, cid_sets[i].number))) {
			ast_test_status_update(test,
				"Expected callerid number '%s' instead of '%s'\n",
				cid_sets[i].number, number);
			return AST_TEST_FAIL;
		}
	}

	return AST_TEST_PASS;
}

struct cid_merge_set {
	int bufsiz;
	const char *name;
	const char *number;
	const char *expected;
};

/* Each Hebrew letter below is two bytes in UTF-8 */
#define HEB_SHALOM "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"   /* shin lamed vav mem */
#define HEB_SH_L   "\xd7\xa9\xd7\x9c"                   /* shin lamed */

AST_TEST_DEFINE(merge)
{
	static const struct cid_merge_set cid_sets[] = {
		/* Everything fits, output is unchanged */
		{80, "name", "1234", "\"name\" <1234>"},
		{80, "na\"me", "1234", "\"na\\\"me\" <1234>"},
		{80, "", "1234", "\"\" <1234>"},
		{80, HEB_SHALOM, "0501234567", "\"" HEB_SHALOM "\" <0501234567>"},
		/* Exactly fits: 4 + 10 + 6 = 20 */
		{20, "abcd", "0501234567", "\"abcd\" <0501234567>"},
		/* Name is shortened so the full number is kept */
		{20, "abcdefghijklmnopqrstuvwxyz", "0501234567", "\"abcd\" <0501234567>"},
		/* Name is cut on a UTF-8 character boundary (room for 5 bytes) */
		{21, HEB_SHALOM, "0501234567", "\"" HEB_SH_L "\" <0501234567>"},
		/* Name is never cut inside an escape sequence (room for 3 bytes) */
		{19, "ab\"cd", "0501234567", "\"ab\" <0501234567>"},
		/* Room for one byte, which cannot hold a whole character */
		{17, HEB_SHALOM, "0501234567", "<0501234567>"},
		/* No room for any of the name, <number> just fits */
		{13, "name", "0501234567", "<0501234567>"},
		/* Not even <number> fits, the number is truncated */
		{8, "name", "0501234567", "0501234"},
		/* Name or number only */
		{80, "name", NULL, "name"},
		{80, NULL, "1234", "1234"},
		{80, NULL, NULL, "<unknown>"},
	};
	/* Guard bytes before and after the destination catch any overrun */
	char buf[1 + 600 + 16];
	char *dst = buf + 1;
	char long_name[256];
	char expected[600];
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "merge";
		info->category = "/main/callerid/";
		info->summary = "Callerid merge unit test";
		info->description =
			"This tests merging of callerid name and number, including "
			"shortening of long UTF-8 names so that the number is kept.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(cid_sets); i++) {
		int j;

		memset(buf, 'X', sizeof(buf));
		ast_callerid_merge(dst, cid_sets[i].bufsiz, cid_sets[i].name, cid_sets[i].number, NULL);
		if (strcmp(dst, cid_sets[i].expected)) {
			ast_test_status_update(test,
				"Case %d: expected '%s' instead of '%s'\n",
				i, cid_sets[i].expected, dst);
			return AST_TEST_FAIL;
		}
		if (buf[0] != 'X') {
			ast_test_status_update(test, "Case %d: wrote before the buffer\n", i);
			return AST_TEST_FAIL;
		}
		for (j = cid_sets[i].bufsiz; j < 600; j++) {
			if (dst[j] != 'X') {
				ast_test_status_update(test, "Case %d: wrote past bufsiz %d\n",
					i, cid_sets[i].bufsiz);
				return AST_TEST_FAIL;
			}
		}
	}

	/* A zero sized buffer must not be written to */
	memset(buf, 'X', sizeof(buf));
	ast_callerid_merge(dst, 0, "name", "1234", NULL);
	ast_callerid_merge(dst, 0, "name", NULL, NULL);
	ast_callerid_merge(dst, 0, NULL, "1234", NULL);
	ast_callerid_merge(dst, 0, NULL, NULL, NULL);
	for (i = 0; i < sizeof(buf); i++) {
		if (buf[i] != 'X') {
			ast_test_status_update(test, "Zero bufsiz: wrote at offset %d\n", i - 1);
			return AST_TEST_FAIL;
		}
	}

	/*
	 * A 200 byte UTF-8 name, longer than the old 128 byte name buffer,
	 * is kept whole when the destination is large enough (e.g. CDR clid).
	 */
	long_name[0] = '\0';
	for (i = 0; i < 25; i++) {
		strcat(long_name, HEB_SHALOM);
	}
	snprintf(expected, sizeof(expected), "\"%s\" <0501234567>", long_name);
	ast_callerid_merge(dst, 256, long_name, "0501234567", NULL);
	if (strcmp(dst, expected)) {
		ast_test_status_update(test, "Long name: expected '%s' instead of '%s'\n",
			expected, dst);
		return AST_TEST_FAIL;
	}

	/* The same name in an 80 byte destination keeps the number */
	ast_callerid_merge(dst, 80, long_name, "0501234567", NULL);
	if (strlen(dst) >= 80 || !ast_ends_with(dst, "\" <0501234567>")
		|| strncmp(dst + 1, long_name, strlen(dst) - 15)
		|| (strlen(dst) - 15) % 2) {
		ast_test_status_update(test, "Long name in 80 bytes: unexpected '%s'\n", dst);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(parse_nominal);
	AST_TEST_UNREGISTER(parse_off_nominal);
	AST_TEST_UNREGISTER(merge);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(parse_nominal);
	AST_TEST_REGISTER(parse_off_nominal);
	AST_TEST_REGISTER(merge);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Callerid Parse Tests");
