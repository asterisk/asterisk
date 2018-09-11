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
 * \file
 * \brief Test the native ARI JSON validators.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_ari_model</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "../res/ari/ari_model_validators.h"

#if defined(TEST_FRAMEWORK)
/*!
 * Wrapper of ast_test_validate_int() so an external function pointer is not used.
 *
 * \note We do this because using an external function pointer
 * did not play nicely when we loaded with RTLD_LAZY.
 */
static int wrap_ast_ari_validate_int(struct ast_json *json)
{
	return ast_ari_validate_int(json);
}
#endif	/* defined(TEST_FRAMEWORK) */

#if defined(TEST_FRAMEWORK)
/*!
 * Wrapper of ast_ari_validate_string() so an external function pointer is not used.
 *
 * \note We do this because using an external function pointer
 * did not play nicely when we loaded with RTLD_LAZY.
 */
static int wrap_ast_ari_validate_string(struct ast_json *json)
{
	return ast_ari_validate_string(json);
}
#endif	/* defined(TEST_FRAMEWORK) */

AST_TEST_DEFINE(validate_byte)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test byte validation";
		info->description =
			"Test byte validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_integer_create(-128);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_ari_validate_byte(uut));

	res = ast_json_integer_set(uut, 0);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, ast_ari_validate_byte(uut));

	res = ast_json_integer_set(uut, 255);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, ast_ari_validate_byte(uut));

	res = ast_json_integer_set(uut, -129);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_byte(uut));

	res = ast_json_integer_set(uut, 256);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_byte(uut));

	str = ast_json_string_create("not a byte");
	ast_test_validate(test, NULL != str);
	ast_test_validate(test, !ast_ari_validate_byte(str));

	/* Even if the string has an integral value */
	res = ast_json_string_set(str, "0");
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_byte(str));

	ast_test_validate(test, !ast_ari_validate_byte(ast_json_null()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(validate_boolean)
{
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test byte validation";
		info->description =
			"Test byte validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_ari_validate_boolean(ast_json_true()));
	ast_test_validate(test, ast_ari_validate_boolean(ast_json_false()));

	str = ast_json_string_create("not a bool");
	ast_test_validate(test, NULL != str);
	ast_test_validate(test, !ast_ari_validate_boolean(str));

	/* Even if the string has a boolean value */
	res = ast_json_string_set(str, "true");
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_boolean(str));

	/* Even if the string has a boolean text in it */
	res = ast_json_string_set(str, "true");
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_boolean(str));

	ast_test_validate(test, !ast_ari_validate_boolean(ast_json_null()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(validate_int)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test int validation";
		info->description =
			"Test int validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_integer_create(-2147483648LL);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_ari_validate_int(uut));

	res = ast_json_integer_set(uut, 0);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, ast_ari_validate_int(uut));

	res = ast_json_integer_set(uut, 2147483647LL);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, ast_ari_validate_int(uut));

	res = ast_json_integer_set(uut, -2147483649LL);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_int(uut));

	res = ast_json_integer_set(uut, 2147483648LL);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_int(uut));

	str = ast_json_string_create("not a int");
	ast_test_validate(test, NULL != str);
	ast_test_validate(test, !ast_ari_validate_int(str));

	/* Even if the string has an integral value */
	res = ast_json_string_set(str, "0");
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_int(str));

	ast_test_validate(test, !ast_ari_validate_int(ast_json_null()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(validate_long)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test long validation";
		info->description =
			"Test long validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_integer_create(0);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_ari_validate_long(uut));

	str = ast_json_string_create("not a long");
	ast_test_validate(test, NULL != str);
	ast_test_validate(test, !ast_ari_validate_long(str));

	/* Even if the string has an integral value */
	res = ast_json_string_set(str, "0");
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_long(str));

	ast_test_validate(test, !ast_ari_validate_long(ast_json_null()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(validate_string)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test string validation";
		info->description =
			"Test string validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_string_create("text");
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_ari_validate_string(uut));

	res = ast_json_string_set(uut, "");
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, ast_ari_validate_string(uut));

	ast_test_validate(test, !ast_ari_validate_string(ast_json_null()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(validate_date)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	enum ast_test_result_state test_res;
	int res;
	int i;
	const char *valid_dates[] = {
		/* Time is optional */
		"2013-06-17",
		/* Seconds are optional */
		"2013-06-17T23:59Z",
		/* Subseconds are optional */
		"2013-06-17T23:59:59Z",
		/* Leap seconds are valid */
		"2013-06-30T23:59:61Z",
		/* Subseconds are allowed */
		"2013-06-17T23:59:59.999999Z",
		/* Now with -06:00 for the timezone */
		"2013-06-17T23:59-06:00",
		"2013-06-17T23:59:59-06:00",
		"2013-06-30T23:59:61-06:00",
		"2013-06-17T23:59:59.999999-06:00",
		/* Again, with +06:30 for the timezone */
		"2013-06-17T23:59+06:30",
		"2013-06-17T23:59:59+06:30",
		"2013-06-30T23:59:61+06:30",
		"2013-06-17T23:59:59.999999+06:30",
		/* So the colon in the timezone is optional */
		"2013-06-17T23:59-0600",
		"2013-06-17T23:59:59-0600",
		"2013-06-30T23:59:61-0600",
		"2013-06-17T23:59:59.999999-0600",
		/* Sure, why not */
		"2013-06-17T23:59+0630",
		"2013-06-17T23:59:59+0630",
		"2013-06-30T23:59:61+0630",
		"2013-06-17T23:59:59.999999+0630",
		"9999-12-31T23:59:61.999999Z",
		/* In fact, you don't even have to specify minutes */
		"2013-06-17T23:59-06",
		"2013-06-17T23:59:59-06",
		"2013-06-30T23:59:61-06",
		"2013-06-17T23:59:59.999999-06",
	};

	/* There are lots of invalid dates that the validator lets through.
	 * Those would be strings properly formatted as a ridiculous date. Such
	 * as 0000-00-00, or 9999-19-39. Those are harder to catch with a regex,
	 * and actually aren't as important. So long as the valid dates pass the
	 * validator, and poorly formatted dates are rejected, it's fine.
	 * Catching the occasional ridiculous date is just bonus.
	 */
	const char *invalid_dates[] = {
		"",
		"Not a date",
		"2013-06-17T", /* Missing time, but has T */
		"2013-06-17T23:59:59.Z", /* Missing subsecond, but has dot */
		"2013-06-17T23:59", /* Missing timezone, but has time */
		"2013-06-17T23:59:59.999999", /* Missing timezone */
		"9999-99-31T23:59:61.999999Z", /* Invalid month */
		"9999-12-99T23:59:61.999999Z", /* Invalid day */
		"9999-12-31T99:59:61.999999Z", /* Invalid hour */
		"9999-12-31T23:99:61.999999Z", /* Invalid minute */
		"9999-12-31T23:59:99.999999Z", /* Invalid second */
		"2013-06-17T23:59:59.999999-99:00", /* Invalid timezone */
		"2013-06-17T23:59:59.999999-06:99", /* Invalid timezone */
		"2013-06-17T23:59:59.999999-06:", /* Invalid timezone */
		"2013-06-17T23:59:59.999999-06:0", /* Invalid timezone */
		"2013-06-17T23:59:59.999999-060", /* Invalid timezone */
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test date validation";
		info->description =
			"Test date validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_string_create("");
	ast_test_validate(test, NULL != uut);

	/* Instead of using ast_test_validate, we'll collect the results from
	 * several test cases, since we have so many */
	test_res = AST_TEST_PASS;
	for (i = 0; i < ARRAY_LEN(valid_dates); ++i) {
		res = ast_json_string_set(uut, valid_dates[i]);
		ast_test_validate(test, 0 == res);
		if (!ast_ari_validate_date(uut)) {
			ast_test_status_update(test,
				"Expected '%s' to be a valid date\n",
				valid_dates[i]);
			test_res = AST_TEST_FAIL;
		}
	}

	for (i = 0; i < ARRAY_LEN(invalid_dates); ++i) {
		res = ast_json_string_set(uut, invalid_dates[i]);
		ast_test_validate(test, 0 == res);
		if (ast_ari_validate_date(uut)) {
			ast_test_status_update(test,
				"Expected '%s' to be an invalid date\n",
				invalid_dates[i]);
			test_res = AST_TEST_FAIL;
		}
	}

	ast_test_validate(test, !ast_ari_validate_string(ast_json_null()));

	return test_res;
}

AST_TEST_DEFINE(validate_list)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, str, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/ari/validators/";
		info->summary = "Test list validation";
		info->description =
			"Test list validation";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_array_create();
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_ari_validate_list(uut, wrap_ast_ari_validate_string));
	ast_test_validate(test, ast_ari_validate_list(uut, wrap_ast_ari_validate_int));

	res = ast_json_array_append(uut, ast_json_string_create(""));
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, ast_ari_validate_list(uut, wrap_ast_ari_validate_string));
	ast_test_validate(test, !ast_ari_validate_list(uut, wrap_ast_ari_validate_int));

	res = ast_json_array_append(uut, ast_json_integer_create(0));
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, !ast_ari_validate_list(uut, wrap_ast_ari_validate_string));
	ast_test_validate(test, !ast_ari_validate_list(uut, wrap_ast_ari_validate_int));

	ast_test_validate(test,
		!ast_ari_validate_list(ast_json_null(), wrap_ast_ari_validate_string));

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(validate_byte);
	AST_TEST_UNREGISTER(validate_boolean);
	AST_TEST_UNREGISTER(validate_int);
	AST_TEST_UNREGISTER(validate_long);
	AST_TEST_UNREGISTER(validate_string);
	AST_TEST_UNREGISTER(validate_date);
	AST_TEST_UNREGISTER(validate_list);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(validate_byte);
	AST_TEST_REGISTER(validate_boolean);
	AST_TEST_REGISTER(validate_int);
	AST_TEST_REGISTER(validate_long);
	AST_TEST_REGISTER(validate_string);
	AST_TEST_REGISTER(validate_date);
	AST_TEST_REGISTER(validate_list);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Skeleton (sample) Test",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_ari_model",
);
