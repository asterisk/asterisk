/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Terry Wilson <twilson@digium.com>
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
 * \brief AstDB Unit Tests
 *
 * \author Terry Wilson <twilson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/logger.h"

enum {
	FAMILY = 0,
	KEY    = 1,
	VALUE  = 2,
};

/* Longest value we can support is 256 for family/key/ so, with
 * family = astdbtest and two slashes we are left with 244 bytes */
static const char long_val[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

AST_TEST_DEFINE(put_get_del)
{
	int res = AST_TEST_PASS;
	const char *inputs[][3] = {
		{"family", "key", "value"},
		{"astdbtest", "a", "b"},
		{"astdbtest", "a", "a"},
		{"astdbtest", "b", "a"},
		{"astdbtest", "b", "b"},
		{"astdbtest", "b", "!@#$%^&*()|+-<>?"},
		{"astdbtest", long_val, "b"},
		{"astdbtest", "b", long_val},
		{"astdbtest", "!@#$%^&*()|+-<>?", "b"},
	};
	size_t x;
	char buf[sizeof(long_val)] = { 0, };

	switch (cmd) {
	case TEST_INIT:
		info->name = "put_get_del";
		info->category = "/main/astdb/";
		info->summary = "ast_db_(put|get|del) unit test";
		info->description =
			"Ensures that the ast_db put, get, and del functions work";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (x = 0; x < ARRAY_LEN(inputs); x++) {
		if (ast_db_put(inputs[x][FAMILY], inputs[x][KEY], inputs[x][VALUE])) {
			ast_test_status_update(test, "Failed to put %s : %s : %s\n", inputs[x][FAMILY], inputs[x][KEY], inputs[x][VALUE]);
			res = AST_TEST_FAIL;
		}
		if (ast_db_get(inputs[x][FAMILY], inputs[x][KEY], buf, sizeof(buf))) {
			ast_test_status_update(test, "Failed to get %s : %s : %s\n", inputs[x][FAMILY], inputs[x][KEY], inputs[x][VALUE]);
			res = AST_TEST_FAIL;
		} else if (strcmp(buf, inputs[x][VALUE])) {
			ast_test_status_update(test, "Failed to match key '%s/%s' value '%s' to '%s'\n", inputs[x][FAMILY], inputs[x][KEY], inputs[x][VALUE], buf);
			res = AST_TEST_FAIL;
		}
		if (ast_db_del(inputs[x][FAMILY], inputs[x][KEY])) {
			ast_test_status_update(test, "Failed to del %s : %s\n", inputs[x][FAMILY], inputs[x][KEY]);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(gettree_deltree)
{
	int res = AST_TEST_PASS;
	const char *inputs[][3] = {
#define BASE "astdbtest"
#define SUB1 "one"
#define SUB2 "two"
#define FAM1 BASE "/" SUB1
#define FAM2 BASE "/" SUB2
		{FAM1, "one", "blah"},
		{FAM1, "two", "bling"},
		{FAM1, "three", "blast"},
		{FAM2, "one", "blah"},
		{FAM2, "two", "bling"},
		{FAM2, "three", "blast"},
	};
	size_t x;
	struct ast_db_entry *dbes, *cur;
	int num_deleted;

	switch (cmd) {
	case TEST_INIT:
		info->name = "gettree_deltree";
		info->category = "/main/astdb/";
		info->summary = "ast_db_(gettree|deltree) unit test";
		info->description =
			"Ensures that the ast_db gettree and deltree functions work";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (x = 0; x < ARRAY_LEN(inputs); x++) {
		if (ast_db_put(inputs[x][FAMILY], inputs[x][KEY], inputs[x][VALUE])) {
			ast_test_status_update(test, "Failed to put %s : %s : %s\n", inputs[x][FAMILY], inputs[x][KEY], inputs[x][VALUE]);
			res = AST_TEST_FAIL;
		}
	}

	if (!(dbes = ast_db_gettree(BASE, NULL))) {
		ast_test_status_update(test, "Failed to ast_db_gettree family %s\n", BASE);
		res = AST_TEST_FAIL;
	}

	for (cur = dbes, x = 0; cur; cur = cur->next, x++) {
		int found = 0;
		size_t z;
		for (z = 0; z < ARRAY_LEN(inputs); z++) {
			char buf[256];
			snprintf(buf, sizeof(buf), "/%s/%s", inputs[z][FAMILY], inputs[z][KEY]);
			if (!strcmp(buf, cur->key) && !strcmp(inputs[z][VALUE], cur->data)) {
				found = 1;
			}
		}
		if (!found) {
			ast_test_status_update(test, "inputs array has no entry for %s == %s\n", cur->key, cur->data);
			res = AST_TEST_FAIL;
		}
	}

	if (x != ARRAY_LEN(inputs)) {
		ast_test_status_update(test, "ast_db_gettree returned %zu entries when we expected %zu\n", x, ARRAY_LEN(inputs));
		res = AST_TEST_FAIL;
	}

	ast_db_freetree(dbes);

	if (!(dbes = ast_db_gettree(BASE, SUB1))) {
		ast_test_status_update(test, "Failed to ast_db_gettree for %s/%s\n", BASE, SUB1);
		res = AST_TEST_FAIL;
	}

	for (cur = dbes, x = 0; cur; cur = cur->next, x++) {
		int found = 0;
		size_t z;
		for (z = 0; z < ARRAY_LEN(inputs); z++) {
			char buf[256];
			snprintf(buf, sizeof(buf), "/%s/%s", inputs[z][FAMILY], inputs[z][KEY]);
			if (!strcmp(buf, cur->key) && !strcmp(inputs[z][VALUE], cur->data)) {
				found = 1;
			}
		}
		if (!found) {
			ast_test_status_update(test, "inputs array has no entry for %s == %s\n", cur->key, cur->data);
			res = AST_TEST_FAIL;
		}
	}

	if (x != (ARRAY_LEN(inputs) / 2)) {
		ast_test_status_update(test, "ast_db_gettree returned %zu entries when we expected %zu\n", x, ARRAY_LEN(inputs) / 2);
		res = AST_TEST_FAIL;
	}

	ast_db_freetree(dbes);

	if ((num_deleted = ast_db_deltree(BASE, SUB2)) != ARRAY_LEN(inputs) / 2) {
		ast_test_status_update(test, "Failed to deltree %s/%s, expected %zu deletions and got %d\n", BASE, SUB2, ARRAY_LEN(inputs) / 2, num_deleted);
		res = AST_TEST_FAIL;
	}

	if ((num_deleted = ast_db_deltree(BASE, NULL)) != ARRAY_LEN(inputs) / 2) {
		ast_test_status_update(test, "Failed to deltree %s, expected %zu deletions and got %d\n", BASE, ARRAY_LEN(inputs) / 2, num_deleted);
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(perftest)
{
	int res = AST_TEST_PASS;
	size_t x;
	char buf[10];

	switch (cmd) {
	case TEST_INIT:
		info->name = "perftest";
		info->category = "/main/astdb/";
		info->summary = "astdb performance unit test";
		info->description =
			"Measure astdb performance";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (x = 0; x < 100000; x++) {
		sprintf(buf, "%zu", x);
		ast_db_put("astdbtest", buf, buf);
	}
	ast_db_deltree("astdbtest", NULL);

	return res;
}

AST_TEST_DEFINE(put_get_long)
{
	int res = AST_TEST_PASS;
	struct ast_str *s;
	int i, j;

#define STR_FILL_32 "abcdefghijklmnopqrstuvwxyz123456"

	switch (cmd) {
	case TEST_INIT:
		info->name = "put_get_long";
		info->category = "/main/astdb/";
		info->summary = "ast_db_(put|get_allocated) unit test";
		info->description =
			"Ensures that the ast_db_put and ast_db_get_allocated functions work";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(s = ast_str_create(4096))) {
		return AST_TEST_FAIL;
	}

	for (i = 1024; i <= 1024 * 1024 * 8; i *= 2) {
		char *out = NULL;

		ast_str_reset(s);

		for (j = 0; j < i; j += sizeof(STR_FILL_32) - 1) {
			ast_str_append(&s, 0, "%s", STR_FILL_32);
		}

		if (ast_db_put("astdbtest", "long", ast_str_buffer(s))) {
			ast_test_status_update(test, "Failed to put value of %zu bytes\n", ast_str_strlen(s));
			res = AST_TEST_FAIL;
		} else if (ast_db_get_allocated("astdbtest", "long", &out)) {
			ast_test_status_update(test, "Failed to get value of %zu bytes\n", ast_str_strlen(s));
			res = AST_TEST_FAIL;
		} else if (strcmp(ast_str_buffer(s), out)) {
			ast_test_status_update(test, "Failed to match value of %zu bytes\n", ast_str_strlen(s));
			res = AST_TEST_FAIL;
		} else if (ast_db_del("astdbtest", "long")) {
			ast_test_status_update(test, "Failed to delete astdbtest/long\n");
			res = AST_TEST_FAIL;
		}

		if (out) {
			ast_free(out);
		}
	}

	ast_free(s);

	return res;
}

static int load_module(void)
{
	AST_TEST_REGISTER(put_get_del);
	AST_TEST_REGISTER(gettree_deltree);
	AST_TEST_REGISTER(perftest);
	AST_TEST_REGISTER(put_get_long);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "AstDB test module");
