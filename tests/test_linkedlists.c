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

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/strings.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dlinkedlists.h"

struct test_val {
	const char *name;
	AST_LIST_ENTRY(test_val) list;
	AST_DLLIST_ENTRY(test_val) dbl_list;
};

static struct test_val a = { "A" };
static struct test_val b = { "B" };
static struct test_val c = { "C" };
static struct test_val d = { "D" };

AST_LIST_HEAD_NOLOCK(test_llist, test_val);
AST_DLLIST_HEAD_NOLOCK(test_dbl_llist, test_val);

static int list_expect(struct test_llist *test_list, const char *expect, struct ast_str **buf)
{
	struct test_val *i;

	ast_str_reset(*buf);
	AST_LIST_TRAVERSE(test_list, i, list) {
		ast_str_append(buf, 0, "%s", i->name);
	}

	return strcmp(expect, ast_str_buffer(*buf));
}

static int dbl_list_expect_forward(struct test_dbl_llist *test_list, const char *expect, struct ast_str **buf)
{
	struct test_val *i;

	ast_str_reset(*buf);
	AST_DLLIST_TRAVERSE(test_list, i, dbl_list) {
		ast_str_append(buf, 0, "%s", i->name);
	}

	return strcmp(expect, ast_str_buffer(*buf));
}

static int dbl_list_expect_reverse(struct test_dbl_llist *test_list, const char *expect, struct ast_str **buf)
{
	struct test_val *i;
	char *str;
	int len = strlen(expect);
	int idx;

	ast_str_reset(*buf);
	AST_DLLIST_TRAVERSE_BACKWARDS(test_list, i, dbl_list) {
		ast_str_append(buf, 0, "%s", i->name);
	}

	/* Check reverse string. */
	str = ast_str_buffer(*buf);
	if (len != strlen(str)) {
		return 1;
	}
	for (idx = 0; idx < len; ++idx) {
		if (expect[idx] != str[len - idx - 1]) {
			return 1;
		}
	}
	return 0;
}

#define MATCH_OR_FAIL(list, val, retbuf) \
	if (list_expect(list, val, &retbuf)) { \
		ast_test_status_update(test, "Expected: %s, Got: %s\n", val, ast_str_buffer(retbuf)); \
		return AST_TEST_FAIL; \
	}

#define MATCH_OR_FAIL_DBL(list, val, retbuf) \
	if (dbl_list_expect_forward(list, val, &retbuf)) { \
		ast_test_status_update(test, "Expected: %s, Got: %s\n", val, ast_str_buffer(retbuf)); \
		return AST_TEST_FAIL; \
	} \
	if (dbl_list_expect_reverse(list, val, &retbuf)) { \
		ast_test_status_update(test, "Expected reverse of: %s, Got: %s\n", val, ast_str_buffer(retbuf)); \
		return AST_TEST_FAIL; \
	}

#define ELEM_OR_FAIL(x,y) \
	if ((x) != (y)) { \
		ast_test_status_update(test, "Expected: %s, Got: %s\n", (x)->name, (y)->name); \
		return AST_TEST_FAIL; \
	}

AST_TEST_DEFINE(single_ll_tests)
{
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	struct test_llist test_list = { 0, };
	struct test_llist other_list = { 0, };
	struct test_val *bogus;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ll_tests";
		info->category = "/main/linkedlists/";
		info->summary = "single linked list unit test";
		info->description =
			"Test the single linked list API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(buf = ast_str_create(16))) {
		return AST_TEST_FAIL;
	}

	if (!(bogus = ast_alloca(sizeof(*bogus)))) {
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
	AST_LIST_REMOVE_HEAD(&test_list, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_REMOVE_HEAD(&test_list, list);
	MATCH_OR_FAIL(&test_list, "", buf);
	if (AST_LIST_REMOVE_HEAD(&test_list, list)) {
		ast_test_status_update(test, "Somehow removed an item from the head of a list that didn't exist\n");
		return AST_TEST_FAIL;
	}
	MATCH_OR_FAIL(&test_list, "", buf);

	/* Check empty list test */

	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty\n");
		return AST_TEST_FAIL;
	}

	/* Insert tail and remove specific item tests. */

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

	/* Insert item after specific item tests */

	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &c, list);
	MATCH_OR_FAIL(&test_list, "AC", buf);
	AST_LIST_INSERT_AFTER(&test_list, &a, &b, list);
	MATCH_OR_FAIL(&test_list, "ABC", buf);
	AST_LIST_INSERT_AFTER(&test_list, &c, &d, list);
	MATCH_OR_FAIL(&test_list, "ABCD", buf);

	ELEM_OR_FAIL(AST_LIST_FIRST(&test_list), &a);
	ELEM_OR_FAIL(AST_LIST_LAST(&test_list), &d);
	ELEM_OR_FAIL(AST_LIST_NEXT(&a, list), &b);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, list) {
		AST_LIST_REMOVE_CURRENT(list);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/* Append list test */

	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &b, list);
	MATCH_OR_FAIL(&test_list, "AB", buf);
	AST_LIST_INSERT_HEAD(&other_list, &c, list);
	MATCH_OR_FAIL(&other_list, "C", buf);
	AST_LIST_INSERT_TAIL(&other_list, &d, list);
	MATCH_OR_FAIL(&other_list, "CD", buf);
	AST_LIST_APPEND_LIST(&test_list, &other_list, list);
	MATCH_OR_FAIL(&test_list, "ABCD", buf);
	MATCH_OR_FAIL(&other_list, "", buf);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, list) {
		AST_LIST_REMOVE_CURRENT(list);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/* Insert list after specific item in middle test */

	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &d, list);
	MATCH_OR_FAIL(&test_list, "AD", buf);
	AST_LIST_INSERT_HEAD(&other_list, &b, list);
	MATCH_OR_FAIL(&other_list, "B", buf);
	AST_LIST_INSERT_TAIL(&other_list, &c, list);
	MATCH_OR_FAIL(&other_list, "BC", buf);
	AST_LIST_INSERT_LIST_AFTER(&test_list, &other_list, &a, list);
	MATCH_OR_FAIL(&test_list, "ABCD", buf);
	MATCH_OR_FAIL(&other_list, "", buf);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, list) {
		AST_LIST_REMOVE_CURRENT(list);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/* Insert list after specific item on end test */

	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &b, list);
	MATCH_OR_FAIL(&test_list, "AB", buf);
	AST_LIST_INSERT_HEAD(&other_list, &c, list);
	MATCH_OR_FAIL(&other_list, "C", buf);
	AST_LIST_INSERT_TAIL(&other_list, &d, list);
	MATCH_OR_FAIL(&other_list, "CD", buf);
	AST_LIST_INSERT_LIST_AFTER(&test_list, &other_list, &b, list);
	MATCH_OR_FAIL(&test_list, "ABCD", buf);
	MATCH_OR_FAIL(&other_list, "", buf);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, list) {
		AST_LIST_REMOVE_CURRENT(list);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	if (!AST_LIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/* Safe traversal list modification tests */

	AST_LIST_INSERT_HEAD(&test_list, &a, list);
	MATCH_OR_FAIL(&test_list, "A", buf);
	AST_LIST_INSERT_TAIL(&test_list, &d, list);
	MATCH_OR_FAIL(&test_list, "AD", buf);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, list) {
		if (bogus == &d) {
			AST_LIST_INSERT_BEFORE_CURRENT(&b, list);
			MATCH_OR_FAIL(&test_list, "ABD", buf);
			AST_LIST_INSERT_BEFORE_CURRENT(&c, list);
			MATCH_OR_FAIL(&test_list, "ABCD", buf);
			AST_LIST_REMOVE_CURRENT(list);
			MATCH_OR_FAIL(&test_list, "ABC", buf);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	MATCH_OR_FAIL(&test_list, "ABC", buf);
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

AST_TEST_DEFINE(double_ll_tests)
{
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	struct test_dbl_llist test_list = { 0, };
	struct test_dbl_llist other_list = { 0, };
	struct test_val *bogus;

	switch (cmd) {
	case TEST_INIT:
		info->name = "double_ll_tests";
		info->category = "/main/linkedlists/";
		info->summary = "double linked list unit test";
		info->description =
			"Test the double linked list API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(buf = ast_str_create(16))) {
		return AST_TEST_FAIL;
	}

	bogus = ast_alloca(sizeof(*bogus));

	if (AST_DLLIST_REMOVE_VERIFY(&test_list, bogus, dbl_list)) {
		ast_test_status_update(test, "AST_DLLIST_REMOVE_VERIFY should safely return NULL for missing element from empty list\n");
		return AST_TEST_FAIL;
	}

	/* INSERT_HEAD and REMOVE_HEAD tests */
	AST_DLLIST_INSERT_HEAD(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_INSERT_HEAD(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "BA", buf);
	AST_DLLIST_REMOVE_HEAD(&test_list, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_REMOVE_HEAD(&test_list, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "", buf);
	if (AST_DLLIST_REMOVE_HEAD(&test_list, dbl_list)) {
		ast_test_status_update(test, "Somehow removed an item from the head of a list that didn't exist\n");
		return AST_TEST_FAIL;
	}
	MATCH_OR_FAIL_DBL(&test_list, "", buf);

	/* Check empty list test */

	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty\n");
		return AST_TEST_FAIL;
	}

	/* Insert tail and remove specific item tests. */

	AST_DLLIST_INSERT_TAIL(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AB", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &c, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
	if (AST_DLLIST_REMOVE_VERIFY(&test_list, bogus, dbl_list)) {
		ast_test_status_update(test, "AST_DLLIST_REMOVE_VERIFY should safely return NULL for missing element\n");
		return AST_TEST_FAIL;
	}
	bogus = NULL;
	if (AST_DLLIST_REMOVE_VERIFY(&test_list, bogus, dbl_list)) {
		ast_test_status_update(test, "AST_DLLIST_REMOVE_VERIFY should safely return NULL for element set to NULL\n");
		return AST_TEST_FAIL;
	}
	AST_DLLIST_REMOVE(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
	AST_DLLIST_REMOVE(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AC", buf);
	AST_DLLIST_REMOVE(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "C", buf);
	AST_DLLIST_REMOVE(&test_list, &c, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "", buf);
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty\n");
		return AST_TEST_FAIL;
	}
	if (AST_DLLIST_REMOVE_VERIFY(&test_list, bogus, dbl_list)) {
		ast_test_status_update(test, "AST_DLLIST_REMOVE_VERIFY should safely return NULL asked to remove a NULL pointer from an empty list\n");
		return AST_TEST_FAIL;
	}

	/* Insert item after and before specific item tests */

	AST_DLLIST_INSERT_HEAD(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &c, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AC", buf);
	AST_DLLIST_INSERT_AFTER(&test_list, &a, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
	AST_DLLIST_INSERT_AFTER(&test_list, &c, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
	AST_DLLIST_REMOVE_TAIL(&test_list, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
	AST_DLLIST_REMOVE_TAIL(&test_list, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AB", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
	AST_DLLIST_INSERT_BEFORE(&test_list, &d, &c, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
	AST_DLLIST_REMOVE_HEAD(&test_list, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "BCD", buf);
	AST_DLLIST_INSERT_BEFORE(&test_list, &b, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);

	ELEM_OR_FAIL(AST_DLLIST_FIRST(&test_list), &a);
	ELEM_OR_FAIL(AST_DLLIST_LAST(&test_list), &d);
	ELEM_OR_FAIL(AST_DLLIST_NEXT(&a, dbl_list), &b);
	ELEM_OR_FAIL(AST_DLLIST_PREV(&b, dbl_list), &a);

	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;

	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/* Append list test */

	AST_DLLIST_INSERT_HEAD(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AB", buf);
	AST_DLLIST_INSERT_HEAD(&other_list, &c, dbl_list);
	MATCH_OR_FAIL_DBL(&other_list, "C", buf);
	AST_DLLIST_INSERT_TAIL(&other_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&other_list, "CD", buf);
	AST_DLLIST_APPEND_DLLIST(&test_list, &other_list, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
	MATCH_OR_FAIL_DBL(&other_list, "", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/*
	 * Safe traversal list modification tests
	 * Traverse starting from first element
	 */

	AST_DLLIST_INSERT_HEAD(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AD", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		if (bogus == &d) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(&b, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
			AST_DLLIST_INSERT_BEFORE_CURRENT(&c, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
			AST_DLLIST_REMOVE_CURRENT(dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
		}
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	AST_DLLIST_INSERT_HEAD(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "B", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "BD", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		if (bogus == &b) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(&a, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
			AST_DLLIST_INSERT_AFTER_CURRENT(&c, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
			AST_DLLIST_REMOVE_CURRENT(dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
		}
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	AST_DLLIST_INSERT_HEAD(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "B", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		if (bogus == &b) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(&a, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "AB", buf);
			AST_DLLIST_INSERT_AFTER_CURRENT(&d, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
			AST_DLLIST_INSERT_AFTER_CURRENT(&c, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
			AST_DLLIST_REMOVE_CURRENT(dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
		}
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	/*
	 * Safe traversal list modification tests
	 * Traverse starting from last element
	 */

	AST_DLLIST_INSERT_HEAD(&test_list, &a, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "A", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "AD", buf);
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		if (bogus == &d) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(&b, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
			AST_DLLIST_INSERT_BEFORE_CURRENT(&c, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
			AST_DLLIST_REMOVE_CURRENT(dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
		}
	}
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
	MATCH_OR_FAIL_DBL(&test_list, "ABC", buf);
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	AST_DLLIST_INSERT_HEAD(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "B", buf);
	AST_DLLIST_INSERT_TAIL(&test_list, &d, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "BD", buf);
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		if (bogus == &b) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(&a, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
			AST_DLLIST_INSERT_AFTER_CURRENT(&c, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
			AST_DLLIST_REMOVE_CURRENT(dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
		}
	}
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
	MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	AST_DLLIST_INSERT_HEAD(&test_list, &b, dbl_list);
	MATCH_OR_FAIL_DBL(&test_list, "B", buf);
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		if (bogus == &b) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(&a, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "AB", buf);
			AST_DLLIST_INSERT_AFTER_CURRENT(&d, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABD", buf);
			AST_DLLIST_INSERT_AFTER_CURRENT(&c, dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ABCD", buf);
			AST_DLLIST_REMOVE_CURRENT(dbl_list);
			MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
		}
	}
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
	MATCH_OR_FAIL_DBL(&test_list, "ACD", buf);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&test_list, bogus, dbl_list) {
		AST_DLLIST_REMOVE_CURRENT(dbl_list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (!AST_DLLIST_EMPTY(&test_list)) {
		ast_test_status_update(test, "List should be empty after traversing and removal. It wasn't.\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(single_ll_tests);
	AST_TEST_UNREGISTER(double_ll_tests);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(single_ll_tests);
	AST_TEST_REGISTER(double_ll_tests);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Test Linked Lists");
