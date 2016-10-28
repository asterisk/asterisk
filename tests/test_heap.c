/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \brief Heap data structure test module
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/heap.h"
#include "asterisk/test.h"

struct node {
	long val;
	size_t index;
};

static int node_cmp(void *_n1, void *_n2)
{
	struct node *n1 = _n1;
	struct node *n2 = _n2;

	if (n1->val < n2->val) {
		return -1;
	} else if (n1->val == n2->val) {
		return 0;
	} else {
		return 1;
	}
}

AST_TEST_DEFINE(heap_test_1)
{
	struct ast_heap *h;
	struct node *obj;
	struct node nodes[3] = {
		{ 1, } ,
		{ 2, } ,
		{ 3, } ,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "heap_test_1";
		info->category = "/main/heap/";
		info->summary = "push and pop elements";
		info->description = "Push a few elements onto a heap and make sure that they come back off in the right order.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(h = ast_heap_create(8, node_cmp, offsetof(struct node, index)))) {
		return AST_TEST_FAIL;
	}

	ast_heap_push(h, &nodes[0]);

	ast_heap_push(h, &nodes[1]);

	ast_heap_push(h, &nodes[2]);

	obj = ast_heap_pop(h);
	if (obj->val != 3) {
		ast_test_status_update(test, "expected 3, got %ld\n", obj->val);
		return AST_TEST_FAIL;
	}

	obj = ast_heap_pop(h);
	if (obj->val != 2) {
		ast_test_status_update(test, "expected 2, got %ld\n", obj->val);
		return AST_TEST_FAIL;
	}

	obj = ast_heap_pop(h);
	if (obj->val != 1) {
		ast_test_status_update(test, "expected 1, got %ld\n", obj->val);
		return AST_TEST_FAIL;
	}

	obj = ast_heap_pop(h);
	if (obj) {
		ast_test_status_update(test, "got unexpected object\n");
		return AST_TEST_FAIL;
	}

	h = ast_heap_destroy(h);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(heap_test_2)
{
	struct ast_heap *h = NULL;
	static const unsigned int total = 100000;
	struct node *nodes = NULL;
	struct node *node;
	unsigned int i = total;
	long last = LONG_MAX;
	long cur;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "heap_test_2";
		info->category = "/main/heap/";
		info->summary = "load test";
		info->description =
				"Push one hundred thousand random elements on to a heap, "
				"verify that the heap has been properly constructed, "
				"and then ensure that the elements are come back off "
				"in the proper order.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(nodes = ast_malloc(total * sizeof(*node)))) {
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (!(h = ast_heap_create(20, node_cmp, offsetof(struct node, index)))) {
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	while (i--) {
		nodes[i].val = ast_random();
		ast_heap_push(h, &nodes[i]);
	}

	if (ast_heap_verify(h)) {
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	i = 0;
	while ((node = ast_heap_pop(h))) {
		cur = node->val;
		if (cur > last) {
			ast_test_status_update(test, "i: %u, cur: %ld, last: %ld\n", i, cur, last);
			res = AST_TEST_FAIL;
			goto return_cleanup;
		}
		last = cur;
		i++;
	}

	if (i != total) {
		ast_test_status_update(test, "Stopped popping off after only getting %u nodes\n", i);
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

return_cleanup:
	if (h) {
		h = ast_heap_destroy(h);
	}
	if (nodes) {
		ast_free(nodes);
	}

	return res;
}

AST_TEST_DEFINE(heap_test_3)
{
	struct ast_heap *h = NULL;
	struct node *nodes = NULL;
	struct node *node;
	static const unsigned int test_size = 100000;
	unsigned int i = test_size;
	long last = LONG_MAX, cur;
	int random_index;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "heap_test_3";
		info->category = "/main/heap/";
		info->summary = "random element removal test";
		info->description =
			"Push a hundred thousand random elements on to a heap, "
			"verify that the heap has been properly constructed, "
			"randomly remove and re-add 10000 elements, and then "
			"ensure that the elements come back off in the proper order.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(nodes = ast_malloc(test_size * sizeof(*node)))) {
		ast_test_status_update(test, "memory allocation failure\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (!(h = ast_heap_create(20, node_cmp, offsetof(struct node, index)))) {
		ast_test_status_update(test, "Failed to allocate heap\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	while (i--) {
		nodes[i].val = ast_random();
		ast_heap_push(h, &nodes[i]);
	}

	if (ast_heap_verify(h)) {
		ast_test_status_update(test, "Failed to verify heap after populating it\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	i = test_size / 10;
	while (i--) {
		random_index = ast_random() % test_size;
		node = ast_heap_remove(h, &nodes[random_index]);
		if (nodes[random_index].val != node->val){
			ast_test_status_update(test, "Failed to remove what we expected to\n");
			res = AST_TEST_FAIL;
			goto return_cleanup;
		}
		ast_heap_push(h, &nodes[random_index]);
	}

	if (ast_heap_verify(h)) {
		ast_test_status_update(test, "Failed to verify after removals\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	i = 0;
	while ((node = ast_heap_pop(h))) {
		cur = node->val;
		if (cur > last) {
			ast_test_status_update(test, "i: %u, cur: %ld, last: %ld\n", i, cur, last);
			res = AST_TEST_FAIL;
			goto return_cleanup;
		}
		last = cur;
		i++;
	}

	if (i != test_size) {
		ast_test_status_update(test, "Stopped popping off after only getting %u nodes\n", i);
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

return_cleanup:
	if (h) {
		h = ast_heap_destroy(h);
	}
	if (nodes) {
		ast_free(nodes);
	}

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(heap_test_1);
	AST_TEST_UNREGISTER(heap_test_2);
	AST_TEST_UNREGISTER(heap_test_3);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(heap_test_1);
	AST_TEST_REGISTER(heap_test_2);
	AST_TEST_REGISTER(heap_test_3);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Heap test module");
