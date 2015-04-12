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

ASTERISK_REGISTER_FILE(__FILE__)

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

static int unload_module(void)
{
	AST_TEST_UNREGISTER(parse_nominal);
	AST_TEST_UNREGISTER(parse_off_nominal);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(parse_nominal);
	AST_TEST_REGISTER(parse_off_nominal);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Callerid Parse Tests");
