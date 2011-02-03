/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Tests for the ast_event API
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup tests
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/strings.h"

/*! These are the keys for accessing attributes */
enum test_attr_keys {
	TEST_ATTR_KEY_SAMP_RATE,
	TEST_ATTR_KEY_STRING,
};

/*! These are the values for the TEST_ATTR_KEY_SAMP_RATE key */
enum test_attr_vals_samp {
	TEST_ATTR_VAL_SAMP_8KHZ  = (1 << 0),
	TEST_ATTR_VAL_SAMP_12KHZ = (1 << 1),
	TEST_ATTR_VAL_SAMP_16KHZ = (1 << 2),
	TEST_ATTR_VAL_SAMP_32KHZ = (1 << 3),
	TEST_ATTR_VAL_SAMP_48KHZ = (1 << 4),
};

/*! This is the attribute structure used for our test interface. */
struct test_attr {
	enum test_attr_vals_samp samp_flags;
	char string[32];
};

static enum ast_format_cmp_res test_cmp(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2)
{
	struct test_attr *attr1 = (struct test_attr *) fattr1;
	struct test_attr *attr2 = (struct test_attr *) fattr2;

	if ((attr1->samp_flags == attr2->samp_flags) &&
		!(strcmp(attr1->string, attr2->string))) {
		return AST_FORMAT_CMP_EQUAL;
	}
	if ((attr1->samp_flags != (attr1->samp_flags & attr2->samp_flags)) ||
		(!ast_strlen_zero(attr1->string) && strcmp(attr1->string, attr2->string))) {
		return AST_FORMAT_CMP_NOT_EQUAL;
	}
	return AST_FORMAT_CMP_SUBSET;
}

static int test_getjoint(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result)
{
	struct test_attr *attr1 = (struct test_attr *) fattr1;
	struct test_attr *attr2 = (struct test_attr *) fattr2;
	struct test_attr *attr_res = (struct test_attr *) result;
	int joint = -1;

	attr_res->samp_flags = (attr1->samp_flags & attr2->samp_flags);

	if (attr_res->samp_flags) {
		joint = 0;
	}

	if (!strcmp(attr1->string, attr2->string)) {
		ast_copy_string(attr_res->string, attr1->string, sizeof(attr_res->string));
		joint = 0;
	}

	return joint;
}

static void test_set(struct ast_format_attr *fattr, va_list ap)
{
	enum test_attr_keys key;
	struct test_attr *attr = (struct test_attr *) fattr;
	char *string;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case TEST_ATTR_KEY_SAMP_RATE:
			attr->samp_flags = (va_arg(ap, int) | attr->samp_flags);
			break;
		case TEST_ATTR_KEY_STRING:
			string = va_arg(ap, char *);
			if (!ast_strlen_zero(string)) {
				ast_copy_string(attr->string, string, sizeof(attr->string));
			}
			break;
		default:
			ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
		}
	}
}

/*! uLaw does not actually have any attributes associated with it.
 * This is just for the purpose of testing. We are guaranteed there
 * will never exist a interface for uLaw already. */
static struct ast_format_attr_interface test_interface = {
	.id = AST_FORMAT_TESTLAW,
	.format_attr_cmp = test_cmp,
	.format_attr_get_joint = test_getjoint,
	.format_attr_set = test_set
};

/*!
 * \internal
 */
AST_TEST_DEFINE(format_test1)
{
	struct ast_format format1 = { 0, };
	struct ast_format format2 = { 0, };
	struct ast_format joint = { 0, };

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_format_test1";
		info->category = "/main/format/";
		info->summary = "Test ast_format with attributes.";
		info->description =
			"This test exercises the Ast Format API by creating and registering "
			"a custom ast_format_attr_interface and performing various function "
			"calls on ast_formats using the interface. ";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_format_attr_reg_interface(&test_interface)) {
		ast_test_status_update(test, "test_interface failed to register.\n");
		return AST_TEST_FAIL;
	}

	/* set a format with a single attribute. */
	ast_format_set(&format1, AST_FORMAT_TESTLAW, 1,
		TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
		AST_FORMAT_ATTR_END);
	if (ast_format_isset(&format1, TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ, AST_FORMAT_ATTR_END)) {
		ast_test_status_update(test, "format1 did not set number attribute correctly.\n");
		return AST_TEST_FAIL;
	}
	if (!ast_format_isset(&format1, TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_12KHZ, AST_FORMAT_ATTR_END)) {
		ast_test_status_update(test, "format1 did not determine isset on number correctly. \n");
		return AST_TEST_FAIL;
	}

	/* append the string attribute to a format with previous attributes already set */
	ast_format_append(&format1,
		TEST_ATTR_KEY_STRING,"String",
		AST_FORMAT_ATTR_END);
	if (ast_format_isset(&format1, TEST_ATTR_KEY_STRING, "String", AST_FORMAT_ATTR_END)) {
		ast_test_status_update(test, "format1 did not set string attribute correctly.\n");
		return AST_TEST_FAIL;
	}
	if (!ast_format_isset(&format1, TEST_ATTR_KEY_STRING, "Not a string", AST_FORMAT_ATTR_END)) {
		ast_test_status_update(test, "format1 did not determine isset on string correctly. \n");
		return AST_TEST_FAIL;
	}

	/* set format2 with both STRING and NUMBER at the same time */
	ast_format_set(&format2, AST_FORMAT_TESTLAW, 1,
		TEST_ATTR_KEY_STRING, "MOOOoo",
		TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
		TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
		AST_FORMAT_ATTR_END);
	/* perform isset with multiple key value pairs. */

	if (ast_format_isset(&format2,
			TEST_ATTR_KEY_STRING, "MOOOoo",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			AST_FORMAT_ATTR_END)) {

		ast_test_status_update(test, "format2 did not set attributes correctly.\n");
		return AST_TEST_FAIL;
	}
	if (!ast_format_isset(&format2,
			TEST_ATTR_KEY_STRING, "WRONG",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			AST_FORMAT_ATTR_END)) {

		ast_test_status_update(test, "format2 did not deterine isset correctly.\n");
		return AST_TEST_FAIL;
	}

	/* get joint attributes between format1 and format2. */
	if (ast_format_joint(&format1, &format2, &joint)) {
		ast_test_status_update(test, "failed to get joint attributes.\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_isset(&joint, TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ, AST_FORMAT_ATTR_END)) {
		ast_test_status_update(test, "joint attribute was not what we expected.\n");
		return AST_TEST_FAIL;
	}

	/* exercise compare functions */
	if (ast_format_cmp(&format1, &format2) != AST_FORMAT_CMP_NOT_EQUAL) {
		ast_test_status_update(test, "cmp 1 failed.\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cmp(&format1, &format1) != AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "cmp 2 failed.\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cmp(&joint, &format1) != AST_FORMAT_CMP_SUBSET) {
		ast_test_status_update(test, "cmp 3 failed.\n");
		return AST_TEST_FAIL;
	}

	/* unregister interface */
	if (ast_format_attr_unreg_interface(&test_interface)) {
		ast_test_status_update(test, "test_interface failed to unregister.\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

/*!
 * \internal
 */
AST_TEST_DEFINE(format_test2)
{
	struct ast_format format = { 0, };

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_format_test2";
		info->category = "/main/format/";
		info->summary = "Test ast_format unique id and category system";
		info->description =
			"This test exercises the Ast Format unique id and category "
			"system by creating formats of various types and verifying "
			"their category matches what we expect.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_format_set(&format, AST_FORMAT_ULAW, 0);
	if (AST_FORMAT_GET_TYPE(format.id) != AST_FORMAT_TYPE_AUDIO) {
		ast_test_status_update(test, "audio type failed\n");
		return AST_TEST_FAIL;
	}

	ast_format_set(&format, AST_FORMAT_H264, 0);
	if (AST_FORMAT_GET_TYPE(format.id) != AST_FORMAT_TYPE_VIDEO) {
		ast_test_status_update(test, "video type failed\n");
		return AST_TEST_FAIL;
	}

	ast_format_set(&format, AST_FORMAT_JPEG, 0);
	if (AST_FORMAT_GET_TYPE(format.id) != AST_FORMAT_TYPE_IMAGE) {
		ast_test_status_update(test, "image type failed\n");
		return AST_TEST_FAIL;
	}

	ast_format_set(&format, AST_FORMAT_T140, 0);
	if (AST_FORMAT_GET_TYPE(format.id) != AST_FORMAT_TYPE_TEXT) {
		ast_test_status_update(test, "text type failed\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int container_test1_helper(struct ast_format_cap *cap1, struct ast_format_cap *cap2, struct ast_test *test)
{

	int res = AST_TEST_PASS;
	struct ast_format_cap *cap_joint;
	struct ast_format tmpformat;

	if (ast_format_attr_reg_interface(&test_interface)) {
		ast_test_status_update(test, "test_interface failed to register.\n");
		ast_format_cap_destroy(cap1);
		ast_format_cap_destroy(cap2);
		return AST_TEST_FAIL;
	}

	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_G722, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_ALAW, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_H264, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_H263, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_T140, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_JPEG, 0));
	ast_format_cap_add(cap1, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_STRING, "testing caps hooray",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_32KHZ,
			AST_FORMAT_ATTR_END));

	/* Test is compatible */
	if (!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_ALAW, 0)) ||
		!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_ULAW, 0)) ||
		!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0)) ||
		!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_H264, 0)) ||
		!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_JPEG, 0)) ||
		!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_T140, 0))) {
		ast_test_status_update(test, "ast cap1 failed to properly detect compatibility test 1.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* Test things that are not compatible */
	if (ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_SPEEX, 0)) ||
		ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_SPEEX16, 0)) ||
		ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_H261, 0))) {
		ast_test_status_update(test, "ast cap1 failed to properly detect compatibility test 2.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* Test compatiblity with format with attributes. */
	if (!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_STRING, "testing caps hooray",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			AST_FORMAT_ATTR_END))) {

		ast_test_status_update(test, "ast cap1 failed to properly detect compatibility test 3.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	if (!ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			AST_FORMAT_ATTR_END))) {

		ast_test_status_update(test, "ast cap1 failed to properly detect compatibility test 4.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	if (ast_format_cap_iscompatible(cap1, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_48KHZ, /* 48khz was not compatible, so this should fail iscompatible check */
			AST_FORMAT_ATTR_END))) {

		ast_test_status_update(test, "ast cap1 failed to properly detect compatibility test 5.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* Lets start testing the functions that compare ast_format_cap objects.
	 * Genreate the cap2 object to contain some similar formats as cap1
	 * and some different formats as well. */
	ast_format_cap_add(cap2, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0));
	ast_format_cap_add(cap2, ast_format_set(&tmpformat, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(cap2, ast_format_set(&tmpformat, AST_FORMAT_SIREN7, 0));
	ast_format_cap_add(cap2, ast_format_set(&tmpformat, AST_FORMAT_H261, 0));
	ast_format_cap_add(cap2, ast_format_set(&tmpformat, AST_FORMAT_T140, 0));
	ast_format_cap_add(cap2, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_STRING, "testing caps hooray",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_12KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_32KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_48KHZ,
			AST_FORMAT_ATTR_END));


	/* find joint formats between cap1 and cap2 */
	cap_joint = ast_format_cap_joint(cap1, cap2);

	if (!cap_joint) {
		ast_test_status_update(test, "failed to create joint capabilities correctly.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* determine if cap_joint is what we think it should be */
	if (!ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0)) ||
		!ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_ULAW, 0)) ||
		!ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_T140, 0)) ||
		!ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_STRING, "testing caps hooray",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			AST_FORMAT_ATTR_END))) {

		ast_test_status_update(test, "ast cap_joint failed to properly detect compatibility test 1.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* make sure joint cap does not have formats that should not be there */
	if (ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_SIREN7, 0)) ||
		ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_TESTLAW, 1,
			TEST_ATTR_KEY_STRING, "testing caps hooray",
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_8KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_16KHZ,
			TEST_ATTR_KEY_SAMP_RATE, TEST_ATTR_VAL_SAMP_48KHZ,
			AST_FORMAT_ATTR_END))) {

		ast_test_status_update(test, "ast cap_joint failed to properly detect compatibility test 1.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* Lets test removing a capability */
	if (ast_format_cap_remove(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_T140, 0))) {
		ast_test_status_update(test, "ast_format_cap_remove failed. \n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* Lets make sure what we just removed does not still exist */
	if (ast_format_cap_iscompatible(cap_joint, &tmpformat)) {
		ast_test_status_update(test, "ast_format_cap_remove failed 2. \n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* Lets test removing a capability by id.*/
	if (ast_format_cap_remove_byid(cap_joint, AST_FORMAT_GSM)) {
		ast_test_status_update(test, "ast_format_cap_remove failed 3. \n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* Lets make sure what we just removed does not still exist */
	if (ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0))) {
		ast_test_status_update(test, "ast_format_cap_remove failed 4. \n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* lets test getting joint formats by type */
	ast_format_cap_destroy(cap_joint);
	if (!(cap_joint = ast_format_cap_get_type(cap1, AST_FORMAT_TYPE_VIDEO))) {
		ast_test_status_update(test, "ast_format_cap_get_type failed.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* lets make sure our joint capability structure has what we expect */
	if (!ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_H264, 0)) ||
		!ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_H263, 0))) {
		ast_test_status_update(test, "get_type failed 2.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	/* now make sure joint does not have anything but video */
	if (ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_ALAW, 0)) ||
		ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_ULAW, 0)) ||
		ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0)) ||
		ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_JPEG, 0)) ||
		ast_format_cap_iscompatible(cap_joint, ast_format_set(&tmpformat, AST_FORMAT_T140, 0))) {
		ast_test_status_update(test, "get_type failed 3.\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* now lets remove everythign from cap_joint */
	ast_format_cap_remove_all(cap_joint);
	if (!ast_format_cap_is_empty(cap_joint)) {
		ast_test_status_update(test, "failed to remove all\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

	/* now lets add all by type */
	ast_format_cap_add_all_by_type(cap_joint, AST_FORMAT_TYPE_AUDIO);
	if (ast_format_cap_is_empty(cap_joint)) {
			ast_test_status_update(test, "failed to add all by type AUDIO\n");
			res = AST_TEST_FAIL;
	}
	ast_format_cap_iter_start(cap_joint);
	while (!(ast_format_cap_iter_next(cap_joint, &tmpformat))) {
		if (AST_FORMAT_GET_TYPE(tmpformat.id) != AST_FORMAT_TYPE_AUDIO) {
			ast_test_status_update(test, "failed to add all by type AUDIO\n");
			res = AST_TEST_FAIL;
			ast_format_cap_iter_end(cap_joint);
			goto test3_cleanup;
		}
	}
	ast_format_cap_iter_end(cap_joint);

	/* test append */
	ast_format_cap_append(cap_joint, cap1);
	ast_format_cap_iter_start(cap1);
	while (!(ast_format_cap_iter_next(cap1, &tmpformat))) {
		if (!ast_format_cap_iscompatible(cap_joint, &tmpformat)) {
			ast_test_status_update(test, "failed to append format capabilities.\n");
			res = AST_TEST_FAIL;
			ast_format_cap_iter_end(cap1);
			goto test3_cleanup;
		}
	}
	ast_format_cap_iter_end(cap1);

	/* test copy */
	cap1 = ast_format_cap_destroy(cap1);
	cap1 = ast_format_cap_dup(cap_joint);
	if (!ast_format_cap_identical(cap_joint, cap1)) {
			ast_test_status_update(test, "failed to copy capabilities\n");
			res = AST_TEST_FAIL;
			goto test3_cleanup;
	}

	/* test remove by type */
	ast_format_cap_remove_bytype(cap_joint, AST_FORMAT_TYPE_AUDIO);
	if (ast_format_cap_has_type(cap_joint, AST_FORMAT_TYPE_AUDIO)) {
		ast_test_status_update(test, "failed to remove all by type audio\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	if (!ast_format_cap_has_type(cap_joint, AST_FORMAT_TYPE_TEXT)) { /* it should still have text */
		ast_test_status_update(test, "failed to remove all by type audio\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}
	ast_format_cap_iter_start(cap_joint);
	while (!(ast_format_cap_iter_next(cap_joint, &tmpformat))) {
		if (AST_FORMAT_GET_TYPE(tmpformat.id) == AST_FORMAT_TYPE_AUDIO) {
			ast_test_status_update(test, "failed to remove all by type audio\n");
			res = AST_TEST_FAIL;
			ast_format_cap_iter_end(cap_joint);
			goto test3_cleanup;
		}
	}
	ast_format_cap_iter_end(cap_joint);

	/* test add all */
	ast_format_cap_remove_all(cap_joint);
	ast_format_cap_add_all(cap_joint);
	{
		int video = 0, audio = 0, text = 0, image = 0;
		ast_format_cap_iter_start(cap_joint);
		while (!(ast_format_cap_iter_next(cap_joint, &tmpformat))) {
			switch (AST_FORMAT_GET_TYPE(tmpformat.id)) {
			case AST_FORMAT_TYPE_AUDIO:
				audio++;
				break;
			case AST_FORMAT_TYPE_VIDEO:
				video++;
				break;
			case AST_FORMAT_TYPE_TEXT:
				text++;
				break;
			case AST_FORMAT_TYPE_IMAGE:
				image++;
				break;
			}
		}
		ast_format_cap_iter_end(cap_joint);
		if (!video || !audio || !text || !image) {
			ast_test_status_update(test, "failed to add all\n");
			res = AST_TEST_FAIL;
			ast_format_cap_iter_end(cap_joint);
			goto test3_cleanup;
		}
	}

	/* test copy2 */
	ast_format_cap_copy(cap2, cap_joint);
	if (!ast_format_cap_identical(cap2, cap_joint)) {
		ast_test_status_update(test, "ast_format_cap_copy failed\n");
		res = AST_TEST_FAIL;
		goto test3_cleanup;
	}

test3_cleanup:
	ast_format_cap_destroy(cap1);
	ast_format_cap_destroy(cap2);
	ast_format_cap_destroy(cap_joint);

	/* unregister interface */
	if (ast_format_attr_unreg_interface(&test_interface)) {
		ast_test_status_update(test, "test_interface failed to unregister.\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

/*!
 * \internal
 */
AST_TEST_DEFINE(container_test1_nolock)
{
	struct ast_format_cap *cap1;
	struct ast_format_cap *cap2;

	switch (cmd) {
	case TEST_INIT:
		info->name = "container_test_1_no_locking";
		info->category = "/main/format/";
		info->summary = "Test ast_format and ast_format_cap structures, no locking";
		info->description =
			"This test exercises the Ast Format Capability API by creating "
			"capability structures and performing various API calls on them.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cap1 = ast_format_cap_alloc_nolock();
	cap2 = ast_format_cap_alloc_nolock();

	if (!cap1 || !cap2) {
		ast_test_status_update(test, "cap alloc failed.\n");
		return AST_TEST_FAIL;
	}
	return container_test1_helper(cap1, cap2, test);
}


/*!
 * \internal
 */
AST_TEST_DEFINE(container_test1_withlock)
{
	struct ast_format_cap *cap1;
	struct ast_format_cap *cap2;

	switch (cmd) {
	case TEST_INIT:
		info->name = "container_test1_with_locking";
		info->category = "/main/format/";
		info->summary = "Test ast_format and ast_format_cap structures, with locking";
		info->description =
			"This test exercises the Ast Format Capability API by creating "
			"capability structures and performing various API calls on them.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cap1 = ast_format_cap_alloc();
	cap2 = ast_format_cap_alloc();

	if (!cap1 || !cap2) {
		ast_test_status_update(test, "cap alloc failed.\n");
		return AST_TEST_FAIL;
	}
	return container_test1_helper(cap1, cap2, test);
}

static int container_test2_no_locking_helper(struct ast_format_cap *cap, struct ast_test *test)
{
	int num = 0;
	struct ast_format tmpformat = { 0, };

	ast_format_cap_add(cap, ast_format_set(&tmpformat, AST_FORMAT_GSM, 0));
	ast_format_cap_add(cap, ast_format_set(&tmpformat, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(cap, ast_format_set(&tmpformat, AST_FORMAT_G722, 0));

	ast_format_cap_iter_start(cap);
	while (!ast_format_cap_iter_next(cap, &tmpformat)) {
		num++;
	}
	ast_format_cap_iter_end(cap);

	ast_format_cap_iter_start(cap);
	while (!ast_format_cap_iter_next(cap, &tmpformat)) {
		num++;
	}
	ast_format_cap_iter_end(cap);

	ast_format_cap_destroy(cap);
	ast_test_status_update(test, "%d items iterated over\n", num);
	return (num == 6) ? AST_TEST_PASS : AST_TEST_FAIL;

}

/*!
 * \internal
 */
AST_TEST_DEFINE(container_test2_no_locking)
{
	struct ast_format_cap *cap;

	switch (cmd) {
	case TEST_INIT:
		info->name = "container_test2_no_locking";
		info->category = "/main/format/";
		info->summary = "Test ast_format_cap iterator, no locking";
		info->description =
			"This test exercises the Ast Capability API iterators.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cap = ast_format_cap_alloc_nolock();
	if (!cap) {
		ast_test_status_update(test, "alloc failed\n");
		return AST_TEST_FAIL;
	}
	return container_test2_no_locking_helper(cap, test);
}

/*!
 * \internal
 */
AST_TEST_DEFINE(container_test2_with_locking)
{
	struct ast_format_cap *cap;

	switch (cmd) {
	case TEST_INIT:
		info->name = "container_test2_with_locking";
		info->category = "/main/format/";
		info->summary = "Test ast_format_cap iterator, with locking";
		info->description =
			"This test exercises the Ast Capability API iterators.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cap = ast_format_cap_alloc();
	if (!cap) {
		ast_test_status_update(test, "alloc failed\n");
		return AST_TEST_FAIL;
	}
	return container_test2_no_locking_helper(cap, test);
}


static int container_test3_helper(int nolocking, struct ast_test *test)
{
	int x;
	int res = AST_TEST_PASS;
	struct ast_format_cap *cap1;
	struct ast_format_cap *cap2;
	struct ast_format_cap *joint;

	for (x = 0; x < 2000; x++) {
		if (nolocking) {
			cap1 = ast_format_cap_alloc_nolock();
			cap2 = ast_format_cap_alloc_nolock();
			joint = ast_format_cap_alloc_nolock();
		} else {
			cap1 = ast_format_cap_alloc();
			cap2 = ast_format_cap_alloc();
			joint = ast_format_cap_alloc();
		}
		if (!cap1 || !cap2 || !joint) {
			ast_test_status_update(test, "cap alloc fail\n");
			return AST_TEST_FAIL;
		}
		ast_format_cap_add_all(cap1);
		ast_format_cap_add_all_by_type(cap2, AST_FORMAT_TYPE_AUDIO);
		ast_format_cap_joint_copy(cap1, cap2, joint);
		if (!(ast_format_cap_identical(cap2, joint))) {
			ast_test_status_update(test, "failed identical test\n");
			res = AST_TEST_FAIL;
			cap1 = ast_format_cap_destroy(cap1);
			cap2 = ast_format_cap_destroy(cap2);
			joint = ast_format_cap_destroy(joint);
			break;
		}
		cap1 = ast_format_cap_destroy(cap1);
		cap2 = ast_format_cap_destroy(cap2);
		joint = ast_format_cap_destroy(joint);
	}
	return res;
}

/*!
 * \internal
 */
AST_TEST_DEFINE(container_test3_nolock)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "container_test3_no_locking";
		info->category = "/main/format/";
		info->summary = "Load Test ast_format_cap no locking.";
		info->description =
			"This test exercises the Ast Capability API and its iterators for the purpose "
			"of measuring performance.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return container_test3_helper(1, test);
}

/*!
 * \internal
 */
AST_TEST_DEFINE(container_test3_withlock)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "container_test3_with_locking";
		info->category = "/main/format/";
		info->summary = "Load Test ast_format_cap with locking.";
		info->description =
			"This test exercises the Ast Capability API and its iterators for the purpose "
			"of measuring performance.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return container_test3_helper(0, test);
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(format_test1);
	AST_TEST_UNREGISTER(format_test2);
	AST_TEST_UNREGISTER(container_test1_nolock);
	AST_TEST_UNREGISTER(container_test1_withlock);
	AST_TEST_UNREGISTER(container_test2_no_locking);
	AST_TEST_UNREGISTER(container_test2_with_locking);
	AST_TEST_UNREGISTER(container_test3_nolock);
	AST_TEST_UNREGISTER(container_test3_withlock);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(format_test1);
	AST_TEST_REGISTER(format_test2);
	AST_TEST_REGISTER(container_test1_nolock);
	AST_TEST_REGISTER(container_test1_withlock);
	AST_TEST_REGISTER(container_test2_no_locking);
	AST_TEST_REGISTER(container_test2_with_locking);
	AST_TEST_REGISTER(container_test3_nolock);
	AST_TEST_REGISTER(container_test3_withlock);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ast_format API Tests");
