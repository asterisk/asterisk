/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Terry Wilson
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
 * \brief Linked List Tests
 *
 * \author Terry Wilson <twilson@digium.com>
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/strings.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"

struct test_val {
	const char *name;
	AST_LIST_ENTRY(test_val) list;
};

static struct test_val a = { "A" };
static struct test_val b = { "B" };
static struct test_val c = { "C" };
static struct test_val d = { "D" };

AST_LIST_HEAD_NOLOCK(test_list, test_val);

static int list_expect(struct test_list *test_list, char *expect, struct ast_str **buf)
{
	struct test_val *i;

	ast_str_reset(*buf);
	AST_LIST_TRAVERSE(test_list, i, list) {
		ast_str_append(buf, 0, "%s", i->name);
	}

	return strcmp(expect, ast_str_buffer(*buf));
}

#define MATCH_OR_FAIL(list, val, retbuf) \
	if (list_expect(list, val, &retbuf)) { \
		ast_test_status_update(test, "Expected: %s, Got: %s\n", val, ast_str_buffer(retbuf)); \
		ast_free(retbuf); \
		return AST_TEST_FAIL; \
	}

#define ELEM_OR_FAIL(x,y) \
	if ((x) != (y)) { \
		ast_test_status_update(test, "Expected: %s, Got: %s\n", (x)->name, (y)->name); \
		return AST_TEST_FAIL; \
	}

AST_TEST_DEFINE(ll_tests)
{
	struct ast_str *buf;
	struct test_list test_list = { 0, };
	struct test_val *bogus;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ll_tests";
		info->category = "/main/linkedlists";
		info->summary = "linked list unit test";
		info->description =
			"Test the linked list API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(buf = ast_str_create(16))) {
		return AST_TEST_FAIL;
	}

	if (!(bogus = alloca(sizeof(*bogus)))) {
		return AST_TEST_FAIL;
	}

	if (AST_LIST_REMOVE(&test_list, bogus, list)) {
		ast_test_status_update(test, "AST_LIST_REMOVE should safely return NULL for missing element from empty list\n");
		return AST_TEST_FAIL;
	}

	/* INSERT_HEAD and REMOVE_HEAD tests */
	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_HEAD(&test_list, &b, list);
	MATCH_OR_FAIL(&test_list, "BA", buf);
	AST_LIST_INSERT_HEAD(&test_list, &c, list);
	MATCH_OR_FAIL(&test_list, "CBA", buf);
	AST_LIST_INSERT_HEAD(&test_list, &d, list);
	MATCH_OR_FAIL(&test_list, "DCBA", buf);
	AST_LIST_REMOVE_HEAD(&test_list, list);
	MATCH_OR_FAIL(&test_list, "CBA", buf);
	AST_LIST_REMOVE_HEAD(&test_list, list);
	MATCH_OR_FAIL(&test_list, "BA", buf);
	AST_LIST_REMOVE_HEAD(&test_list, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_REMOVE_HEAD(&test_list, list);
	MATCH_OR_FAIL(&test_list, "", buf);

	if (AST_LIST_REMOVE_HEAD(&test_list, list)) {
		ast_test_status_update(test, "Somehow removed an item from the head of a list that didn't exist\n");
		return AST_TEST_FAIL;
	}

	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty\n");
		return AST_TEST_FAIL;
	}

	AST_LIST_INSERT_TAIL(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &b, list);
	MATCH_OR_FAIL(&test_list, "AB", buf);
	AST_LIST_INSERT_TAIL(&test_list, &c, list);
	MATCH_OR_FAIL(&test_list, "ABC", buf);
	AST_LIST_INSERT_TAIL(&test_list, &d, list);
	MATCH_OR_FAIL(&test_list, "ABCD", buf);

	if (AST_LIST_REMOVE(&test_list, bogus, list)) {
		ast_test_status_update(test, "AST_LIST_REMOVE should safely return NULL for missing element\n");
		return AST_TEST_FAIL;
	}

	bogus = NULL;

	if (AST_LIST_REMOVE(&test_list, bogus, list)) {
		ast_test_status_update(test, "AST_LIST_REMOVE should safely return NULL for element set to NULL\n");
		return AST_TEST_FAIL;
	}

	AST_LIST_REMOVE(&test_list, &b, list);
	MATCH_OR_FAIL(&test_list, "ACD", buf);
	AST_LIST_REMOVE(&test_list, &d, list);
	MATCH_OR_FAIL(&test_list, "AC", buf);
	AST_LIST_REMOVE(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "C", buf);
	AST_LIST_REMOVE(&test_list, &c, list);
	MATCH_OR_FAIL(&test_list, "", buf);

	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty\n");
		return AST_TEST_FAIL;
	}

	if (AST_LIST_REMOVE(&test_list, bogus, list)) {
		ast_test_status_update(test, "AST_LIST_REMOVE should safely return NULL asked to remove a NULL pointer from an empty list\n");
		return AST_TEST_FAIL;
	}

	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &b, list);
	MATCH_OR_FAIL(&test_list, "AB", buf);
	AST_LIST_INSERT_AFTER(&test_list, &a, &c, list);
	MATCH_OR_FAIL(&test_list, "ACB", buf);
	AST_LIST_INSERT_AFTER(&test_list, &b, &d, list);
	MATCH_OR_FAIL(&test_list, "ACBD", buf);

	ELEM_OR_FAIL(AST_LIST_FIRST(&test_list), &a);
	ELEM_OR_FAIL(AST_LIST_LAST(&test_list), &d);
	ELEM_OR_FAIL(AST_LIST_NEXT(&a, list), &c);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, list) {
		AST_LIST_REMOVE_CURRENT(list);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(ll_tests);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(ll_tests);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Test Linked Lists");
