/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \file \brief Test endpoints.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/astobj2.h"
#include "asterisk/endpoints.h"
#include "asterisk/module.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/test.h"

static const char *test_category = "/core/endpoints/";

AST_TEST_DEFINE(create)
{
	RAII_VAR(struct ast_endpoint *, uut, NULL, ast_endpoint_shutdown);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test endpoint creation";
		info->description = "Test endpoint creation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, NULL == ast_endpoint_create(NULL, NULL));
	ast_test_validate(test, NULL == ast_endpoint_create("", ""));
	ast_test_validate(test, NULL == ast_endpoint_create("TEST", ""));
	ast_test_validate(test, NULL == ast_endpoint_create("", "test_res"));

	uut = ast_endpoint_create("TEST", "test_res");
	ast_test_validate(test, NULL != uut);

	ast_test_validate(test,
		0 == strcmp("TEST", ast_endpoint_get_tech(uut)));
	ast_test_validate(test,
		0 == strcmp("test_res", ast_endpoint_get_resource(uut)));

	return AST_TEST_PASS;
}


AST_TEST_DEFINE(defaults)
{
	RAII_VAR(struct ast_endpoint *, uut, NULL, ast_endpoint_shutdown);
	RAII_VAR(struct ast_endpoint_snapshot *, snapshot, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test defaults for new endpoints";
		info->description = "Test defaults for new endpoints";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_endpoint_create("TEST", "test_res");
	ast_test_validate(test, NULL != uut);
	snapshot = ast_endpoint_snapshot_create(uut);
	ast_test_validate(test, NULL != snapshot);

	ast_test_validate(test, 0 == strcmp("TEST/test_res", snapshot->id));
	ast_test_validate(test, 0 == strcmp("TEST", snapshot->tech));
	ast_test_validate(test, 0 == strcmp("test_res", snapshot->resource));
	ast_test_validate(test, AST_ENDPOINT_UNKNOWN == snapshot->state);
	ast_test_validate(test, -1 == snapshot->max_channels);
	ast_test_validate(test, 0 == snapshot->num_channels);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(setters)
{
	RAII_VAR(struct ast_endpoint *, uut, NULL, ast_endpoint_shutdown);
	RAII_VAR(struct ast_endpoint_snapshot *, snapshot, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test endpoint setters";
		info->description = "Test endpoint setters";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_endpoint_create("TEST", "test_res");
	ast_test_validate(test, NULL != uut);

	ast_endpoint_set_state(uut, AST_ENDPOINT_ONLINE);
	ast_endpoint_set_max_channels(uut, 314159);

	snapshot = ast_endpoint_snapshot_create(uut);
	ast_test_validate(test, NULL != snapshot);

	ast_test_validate(test, AST_ENDPOINT_ONLINE == snapshot->state);
	ast_test_validate(test, 314159 == snapshot->max_channels);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(create);
	AST_TEST_UNREGISTER(defaults);
	AST_TEST_UNREGISTER(setters);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(create);
	AST_TEST_REGISTER(defaults);
	AST_TEST_REGISTER(setters);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Endpoint testing",
	.load = load_module,
	.unload = unload_module,
	);
