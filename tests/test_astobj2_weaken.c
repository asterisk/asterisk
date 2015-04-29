/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, CFWare, LLC
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

/*!
 * \file
 * \brief astobj2 weakproxy test module
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/astobj2.h"

static int destructor_called;
static int weakproxydestroyed;

static void test_obj_destructor(void *obj)
{
	destructor_called++;
}

static void weakproxy_destructor(void *obj)
{
	weakproxydestroyed++;
}

static void test_obj_destroy_notify(void *obj, void *data)
{
	int *i = data;

	++*i;
}

struct my_weakproxy {
	AO2_WEAKPROXY();
	int f1;
};

AST_TEST_DEFINE(astobj2_weak1)
{
	void *obj1 = NULL;
	void *obj2 = NULL;
	void *obj3 = NULL;
	void *strong1 = NULL;
	struct my_weakproxy *weakref1 = NULL;
	struct my_weakproxy *weakref2 = NULL;
	int notify0_called = 0;
	int notify1_called = 0;
	int notify2_called = 0;
	int notify3_called = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "astobj2_weak1";
		info->category = "/main/astobj2/";
		info->summary = "Test ao2 weak objects";
		info->description = "Test ao2 weak objects.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	destructor_called = weakproxydestroyed = 0;
	obj1 = ao2_t_alloc(0, test_obj_destructor, "obj1");
	if (!obj1) {
		return AST_TEST_FAIL;
	}

	weakref1 = ao2_t_weakproxy_alloc(sizeof(*weakref1), weakproxy_destructor, "weakref1");
	if (!weakref1) {
		ast_test_status_update(test, "Failed to allocate weakref1.\n");
		goto fail_cleanup;
	}
	weakref1->f1 = 5315;

	if (ao2_weakproxy_subscribe(weakref1, test_obj_destroy_notify, &notify0_called, 0)) {
		ast_test_status_update(test, "Failed to subscribe to weakref1.\n");
		goto fail_cleanup;
	}
	if (!notify0_called) {
		ast_test_status_update(test, "Subscribe failed to immediately run callback for empty weakproxy.\n");
		goto fail_cleanup;
	}

	if (ao2_t_weakproxy_set_object(weakref1, obj1, 0, "set weakref1 to obj1")) {
		ast_test_status_update(test, "Failed to set obj1 on weakref1.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_subscribe(weakref1, test_obj_destroy_notify, &notify1_called, 0)) {
		ast_test_status_update(test, "Failed to add a subscription to weakref1.\n");
		goto fail_cleanup;
	}

	weakref2 = ao2_t_get_weakproxy(obj1, "get weakref2 from obj1");
	if (weakref1 != weakref2) {
		ast_test_status_update(test, "weakref1 != weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_subscribe(weakref2, test_obj_destroy_notify, &notify2_called, 0)) {
		ast_test_status_update(test, "Failed to add a subscription to weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_subscribe(weakref2, test_obj_destroy_notify, &notify2_called, 0)) {
		ast_test_status_update(test, "Failed to add a duplicate subscription to weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_subscribe(weakref2, test_obj_destroy_notify, &notify2_called, 0)) {
		ast_test_status_update(test, "Failed to add a second duplicate subscription to weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_unsubscribe(weakref2, test_obj_destroy_notify, &notify2_called, 0) != 1) {
		ast_test_status_update(test, "Failed to remove a subscription to weakref2.\n");
		goto fail_cleanup;
	}

	ao2_t_cleanup(weakref1, "weakref1");
	ao2_t_cleanup(weakref2, "weakref2");

	weakref2 = ao2_t_get_weakproxy(obj1, "get weakref2 from obj1");
	if (weakref1 != weakref2) {
		weakref1 = NULL;
		ast_test_status_update(test, "weakref1 != weakref2.\n");
		goto fail_cleanup;
	}
	weakref1 = NULL;

	obj2 = ao2_t_alloc(0, NULL, "obj2");
	if (obj2) {
		int ret = ao2_t_weakproxy_set_object(weakref2, obj2, 0, "set weakref2 to obj2");

		ao2_ref(obj2, -1);
		if (!ret) {
			ast_test_status_update(test, "Set obj2 to weakref2 when it already had an object.\n");
			goto fail_cleanup;
		}
	}

	if (ao2_weakproxy_subscribe(weakref2, test_obj_destroy_notify, &notify3_called, 0)) {
		ast_test_status_update(test, "Failed to add a subscription to weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_subscribe(weakref2, test_obj_destroy_notify, &notify3_called, 0)) {
		ast_test_status_update(test, "Failed to add a duplicate subscription to weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_weakproxy_unsubscribe(weakref2, test_obj_destroy_notify, &notify3_called, OBJ_MULTIPLE) != 2) {
		ast_test_status_update(test, "Failed to remove the correct number of subscriptions to weakref2.\n");
		goto fail_cleanup;
	}

	if (destructor_called || notify1_called || notify2_called || notify3_called) {
		ast_test_status_update(test, "Destructor or notifications called early.\n");
		goto fail_cleanup;
	}

	strong1 = ao2_t_weakproxy_get_object(weakref2, 0, "get strong1 from weakref2");
	ao2_t_cleanup(strong1, "strong1");

	if (obj1 != strong1) {
		ast_test_status_update(test, "obj1 != strong1.\n");
		goto fail_cleanup;
	}

	if (destructor_called || notify1_called || notify2_called || notify3_called) {
		ast_test_status_update(test, "Destructor or notification called early.\n");
		goto fail_cleanup;
	}

	ao2_t_ref(obj1, -1, "obj1");
	obj1 = NULL;

	if (destructor_called != 1 || notify1_called != 1 || notify2_called != 2 || notify3_called != 0) {
		ast_test_status_update(test, "Destructor or notification not called the expected number of times.\n");
		goto fail_cleanup;
	}

	if (ao2_t_weakproxy_get_object(weakref2, 0, "impossible get of weakref2") != NULL) {
		ast_test_status_update(test, "Get object on weakref2 worked when it shouldn't\n");
		goto fail_cleanup;
	}

	obj3 = ao2_t_alloc(0, test_obj_destructor, "obj3");
	if (!obj3) {
		ast_test_status_update(test, "Failed to allocate obj3.\n");
		goto fail_cleanup;
	}

	if (ao2_t_weakproxy_set_object(weakref2, obj3, 0, "set weakref2 to obj3")) {
		ast_test_status_update(test, "Failed to set obj3 to weakref2.\n");
		goto fail_cleanup;
	}

	if (ao2_t_weakproxy_ref_object(obj3, +1, 0, "ao2_ref should never see this") != -2) {
		ast_test_status_update(test,
			"Expected -2 from ao2_t_weakproxy_ref_object against normal ao2 object.\n");
		goto fail_cleanup;
	}

	if (ao2_t_weakproxy_ref_object(weakref2, +1, 0, "weakref2 ref_object") != 2) {
		ast_test_status_update(test, "Expected 2 from weakref2 ref_object.\n");
		goto fail_cleanup;
	}

	if (ao2_t_ref(obj3, -1, "balance weakref2 ref_object") != 3) {
		ast_test_status_update(test, "Expected 3 from obj3 ao2_t_ref.\n");
		goto fail_cleanup;
	}

	ao2_ref(obj3, -1);

	if (ao2_weakproxy_ref_object(weakref2, +1, 0) != -1) {
		ast_test_status_update(test, "Expected -1 from weakref2 ref_object because obj3 is gone.\n");
		goto fail_cleanup;
	}

	ao2_t_ref(weakref2, -1, "weakref2");

	if (!weakproxydestroyed) {
		ast_test_status_update(test, "Destructor never called for weakproxy, likely a leak.\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;

fail_cleanup:
	ao2_cleanup(obj1);
	ao2_cleanup(obj3);
	ao2_cleanup(weakref1);
	ao2_cleanup(weakref2);

	return AST_TEST_FAIL;
}

static int load_module(void)
{
	AST_TEST_REGISTER(astobj2_weak1);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "ASTOBJ2 Weak Reference Unit Tests");
