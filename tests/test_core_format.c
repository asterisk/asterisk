/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Core Format API Unit Tests
 *
 * \author Joshua Colp <jcolp@digium.com>
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
#include "asterisk/codec.h"
#include "asterisk/format.h"

#define TEST_CATEGORY "/main/core_format/"

static void test_core_format_destroy(struct ast_format *format);
static int test_core_format_clone(const struct ast_format *src, struct ast_format *dst);
static enum ast_format_cmp_res test_core_format_cmp(const struct ast_format *format1, const struct ast_format *format2);
static struct ast_format *test_core_format_get_joint(const struct ast_format *format1, const struct ast_format *format2);
static struct ast_format *test_core_format_attribute_set(const struct ast_format *format, const char *name, const char *value);
static struct ast_format *test_core_format_parse_sdp_fmtp(const struct ast_format *format, const char *attributes);
static void test_core_format_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str);

/*! \brief A format attribute 'module' used by the unit tests */
static struct ast_format_interface test_core_format_attr = {
	.format_destroy = &test_core_format_destroy,
	.format_clone = &test_core_format_clone,
	.format_cmp = &test_core_format_cmp,
	.format_get_joint = &test_core_format_get_joint,
	.format_attribute_set = &test_core_format_attribute_set,
	.format_parse_sdp_fmtp = &test_core_format_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = &test_core_format_generate_sdp_fmtp,
};

/*! \brief A test piece of data to associate with \ref test_core_format_attr */
struct test_core_format_pvt {
	/*! Some data field */
	int field_one;
	/*! Another arbitrary data field */
	int field_two;
};

/*! \brief A test codec for these unit tests. Should be used with \c test_core_format */
static struct ast_codec test_core_format_codec = {
	.name = "test_core_format_codec",
	.description = "Unit test codec used by test_core_format",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

/*! \brief Tracking object used to verify format attribute callbacks */
struct callbacks_called {
	/*! Number of times \ref test_core_format_destroy was called */
	int format_destroy;
	/*! Number of times \ref test_core_format_clone was called */
	int format_clone;
	/*! Number of times \ref test_core_format_cmp was called */
	int format_cmp;
	/*! Number of times \ref test_core_format_get_joint was called */
	int format_get_joint;
	/*! Number of times \ref test_core_format_attribute_set was called */
	int format_attribute_set;
	/*! Number of times \ref test_core_format_parse_sdp_fmtp was called */
	int format_parse_sdp_fmtp;
	/*! Number of times \ref test_core_format_generate_sdp_fmtp was called */
	int format_generate_sdp_fmtp;
};

/*! \brief A global tracking object. Cleared out by the test init cb */
static struct callbacks_called test_callbacks_called;

/*! \brief Format attribute callback for when format attributes are to be destroyed */
static void test_core_format_destroy(struct ast_format *format)
{
	struct test_core_format_pvt *pvt = ast_format_get_attribute_data(format);

	ast_free(pvt);
	++test_callbacks_called.format_destroy;
}

/*! \brief Format attribute callback called during format cloning */
static int test_core_format_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct test_core_format_pvt *pvt = ast_format_get_attribute_data(src);
	struct test_core_format_pvt *new_pvt;

	new_pvt = ast_calloc(1, sizeof(*new_pvt));
	if (!new_pvt) {
		return -1;
	}

	if (pvt) {
		*new_pvt = *pvt;
	}
	ast_format_set_attribute_data(dst, new_pvt);

	++test_callbacks_called.format_clone;

	return 0;
}

/*! \brief Format attribute callback called during format comparison */
static enum ast_format_cmp_res test_core_format_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	struct test_core_format_pvt *pvt1 = ast_format_get_attribute_data(format1);
	struct test_core_format_pvt *pvt2 = ast_format_get_attribute_data(format2);

	++test_callbacks_called.format_cmp;
	if (pvt1 == pvt2) {
		return AST_FORMAT_CMP_EQUAL;
	}

	if ((!pvt1 && pvt2 && (pvt2->field_one != 0 || pvt2->field_two != 0))
		|| (pvt1 && !pvt2 && (pvt1->field_one != 0 || pvt1->field_two != 0))) {
		return AST_FORMAT_CMP_NOT_EQUAL;
	}

	if (pvt1 && pvt2) {
		if (!memcmp(pvt1, pvt2, sizeof(*pvt1))) {
			return AST_FORMAT_CMP_EQUAL;
		} else {
			return AST_FORMAT_CMP_NOT_EQUAL;
		}
	}

	return AST_FORMAT_CMP_EQUAL;
}

/*!
 * \brief Format attribute callback called during joint format capability
 * \note Our test will assume the max of attributes \c field_one and \c field_two
 */
static struct ast_format *test_core_format_get_joint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct test_core_format_pvt *pvt1 = ast_format_get_attribute_data(format1);
	struct test_core_format_pvt *pvt2 = ast_format_get_attribute_data(format2);
	struct ast_format *joint;
	struct test_core_format_pvt *joint_pvt;

	joint = ast_format_clone(format1);
	if (!joint) {
		return NULL;
	}
	joint_pvt = ast_format_get_attribute_data(joint);

	joint_pvt->field_one = MAX(pvt1 ? pvt1->field_one : 0, pvt2 ? pvt2->field_one : 0);
	joint_pvt->field_two = MAX(pvt2 ? pvt2->field_two : 0, pvt2 ? pvt2->field_two : 0);

	++test_callbacks_called.format_get_joint;

	return joint;
}

/*! \brief Format attribute callback for setting an attribute on a format */
static struct ast_format *test_core_format_attribute_set(const struct ast_format *format, const char *name, const char *value)
{
	struct ast_format *clone = ast_format_clone(format);
	struct test_core_format_pvt *clone_pvt;

	if (!clone) {
		return NULL;
	}
	clone_pvt = ast_format_get_attribute_data(clone);

	if (!strcmp(name, "one")) {
		clone_pvt->field_one = atoi(value);
	} else if (!strcmp(name, "two")) {
		clone_pvt->field_two = atoi(value);
	}
	++test_callbacks_called.format_attribute_set;

	return clone;
}

/*! \brief Format attribute callback to construct a format from an SDP fmtp line */
static struct ast_format *test_core_format_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	struct ast_format *clone = ast_format_clone(format);
	struct test_core_format_pvt *pvt;

	if (!clone) {
		return NULL;
	}

	pvt = ast_format_get_attribute_data(clone);

	if (sscanf(attributes, "one=%d;two=%d", &pvt->field_one, &pvt->field_two) != 2) {
		ao2_ref(clone, -1);
		return NULL;
	}

	++test_callbacks_called.format_parse_sdp_fmtp;
	return clone;
}

/*! \brief Format attribute callback to generate an SDP fmtp line from a format */
static void test_core_format_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct test_core_format_pvt *pvt = ast_format_get_attribute_data(format);

	if (!pvt) {
		return;
	}

	ast_str_append(str, 0, "a=fmtp:%u one=%d;two=%d\r\n", payload, pvt->field_one, pvt->field_two);

	++test_callbacks_called.format_generate_sdp_fmtp;
}

AST_TEST_DEFINE(format_create)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format creation unit test";
		info->description =
			"Test creation of a format";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(format) != codec->id) {
		ast_test_status_update(test, "Created format does not contain provided codec\n");
		return AST_TEST_FAIL;
	}

	ao2_ref(format, -1);
	format = ast_format_create_named("super_ulaw", codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(format) != codec->id) {
		ast_test_status_update(test, "Created format does not contain provided codec\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_create_attr)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format_w_attr, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format creation w/ attributes unit test";
		info->description =
			"Test creation of a format with attributes";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("test_core_format_codec", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(format) != codec->id) {
		ast_test_status_update(test, "Created format does not contain provided codec\n");
		return AST_TEST_FAIL;
	}

	format_w_attr = ast_format_attribute_set(format, "one", "1");
	if (!format_w_attr) {
		ast_test_status_update(test, "Could not create format with attributes using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(format_w_attr) != codec->id) {
		ast_test_status_update(test, "Created format does not contain provided codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cmp(format, format_w_attr) == AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Format with attributes should not be equal to format without attributes\n");
		return AST_TEST_FAIL;
	}

	ast_test_validate(test, test_callbacks_called.format_attribute_set == 1);
	ast_test_validate(test, test_callbacks_called.format_cmp == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_clone)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format_w_attr, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, clone, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format cloning unit test";
		info->description =
			"Test cloning of a format";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("test_core_format_codec", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(format) != codec->id) {
		ast_test_status_update(test, "Created format does not contain provided codec\n");
		return AST_TEST_FAIL;
	}

	format_w_attr = ast_format_attribute_set(format, "one", "1");
	if (!format_w_attr) {
		ast_test_status_update(test, "Could not create format with attributes using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(format_w_attr) != codec->id) {
		ast_test_status_update(test, "Created format does not contain provided codec\n");
		return AST_TEST_FAIL;
	}

	/* Test cloning a format without attributes */
	clone = ast_format_clone(format);
	if (!clone) {
		ast_test_status_update(test, "Could not create cloned format\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(clone) != codec->id) {
		ast_test_status_update(test, "Cloned format does not contain provided codec\n");
		return AST_TEST_FAIL;
	} else if (clone == format) {
		ast_test_status_update(test, "Cloned format pointer is the same as original format pointer\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cmp(clone, format) != AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Cloned format is not the same as its original format\n");
		return AST_TEST_FAIL;
	}
	ao2_ref(clone, -1);

	/* Test cloning a format with attributes */
	clone = ast_format_clone(format_w_attr);
	if (!clone) {
		ast_test_status_update(test, "Could not create cloned format\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(clone) != codec->id) {
		ast_test_status_update(test, "Cloned format does not contain provided codec\n");
		return AST_TEST_FAIL;
	} else if (clone == format_w_attr) {
		ast_test_status_update(test, "Cloned format pointer is the same as original format pointer\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cmp(clone, format_w_attr) != AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Cloned format is not the same as its original format\n");
		return AST_TEST_FAIL;
	}
	ast_test_validate(test, test_callbacks_called.format_attribute_set == 1);
	ast_test_validate(test, test_callbacks_called.format_clone == 3);
	ast_test_validate(test, test_callbacks_called.format_cmp == 2);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cmp_same_codec)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, first, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, second, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, named, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format comparison unit test";
		info->description =
			"Test comparison of two different formats with same codec";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	first = ast_format_create(codec);
	if (!first) {
		ast_test_status_update(test, "Could not create first format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	second = ast_format_create(codec);
	if (!second) {
		ast_test_status_update(test, "Could not create second format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	named = ast_format_create_named("super_ulaw", codec);
	if (!named) {
		ast_test_status_update(test, "Could not create named format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cmp(first, second) != AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Two formats that are the same compared as not being equal\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cmp(first, named) != AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Two formats that are the same compared as not being equal\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cmp_different_codec)
{
	RAII_VAR(struct ast_codec *, first_codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, second_codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, first, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, second, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format comparison unit test";
		info->description =
			"Test comparison of two different formats with different codec";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	first_codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!first_codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	first = ast_format_create(first_codec);
	if (!first) {
		ast_test_status_update(test, "Could not create first format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	second_codec = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!second_codec) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	second = ast_format_create(second_codec);
	if (!second) {
		ast_test_status_update(test, "Could not create second format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cmp(first, second) != AST_FORMAT_CMP_NOT_EQUAL) {
		ast_test_status_update(test, "Two formats that have different codecs did not compare as being not equal\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_attr_cmp_same_codec)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, first, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, second, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, original, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format with attributes comparison unit test";
		info->description =
			"Test comparison of two different formats with attributes with same codec";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("test_core_format_codec", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	original = ast_format_create(codec);
	if (!original) {
		ast_test_status_update(test, "Could not create format using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	first = ast_format_attribute_set(original, "one", "1");
	if (!first) {
		ast_test_status_update(test, "Could not create first format with attributes\n");
		return AST_TEST_FAIL;
	}

	second = ast_format_attribute_set(original, "two", "1");
	if (!second) {
		ast_test_status_update(test, "Could not create second format with attributes\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cmp(first, second) == AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Formats with different attributes were compared to be equal when they should not\n");
		return AST_TEST_FAIL;
	}

	ao2_ref(second, -1);
	second = ast_format_attribute_set(original, "one", "1");

	if (ast_format_cmp(first, second) != AST_FORMAT_CMP_EQUAL) {
		ast_test_status_update(test, "Formats with the same attributes should be equal\n");
		return AST_TEST_FAIL;
	}

	ast_test_validate(test, test_callbacks_called.format_attribute_set == 3);
	ast_test_validate(test, test_callbacks_called.format_cmp == 2);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_joint_same_codec)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, first, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, second, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, joint, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Joint format unit test";
		info->description =
			"Test joint format creation using two different formats with same codec";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	first = ast_format_create(codec);
	if (!first) {
		ast_test_status_update(test, "Could not create first format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	second = ast_format_create(codec);
	if (!second) {
		ast_test_status_update(test, "Could not create second format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	joint = ast_format_joint(first, second);
	if (!joint) {
		ast_test_status_update(test, "Failed to create a joint format using two formats of same codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(joint) != codec->id) {
		ast_test_status_update(test, "Returned joint format does not contain expected codec\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_attr_joint_same_codec)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, original, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, first, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, second, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, joint, NULL, ao2_cleanup);
	struct ast_str *fmtp = ast_str_alloca(64);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Joint format attribute unit test";
		info->description =
			"Test joint format creation using two different formats with attributes and with same codec";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("test_core_format_codec", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	original = ast_format_create(codec);
	if (!original) {
		ast_test_status_update(test, "Could not create format from test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	first = ast_format_attribute_set(original, "one", "2");
	if (!first) {
		ast_test_status_update(test, "Could not create first format using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	second = ast_format_attribute_set(original, "one", "5");
	if (!second) {
		ast_test_status_update(test, "Could not create second format using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	joint = ast_format_joint(first, second);
	if (!joint) {
		ast_test_status_update(test, "Failed to create a joint format using two formats of same codec\n");
		return AST_TEST_FAIL;
	} else if (ast_format_get_codec_id(joint) != codec->id) {
		ast_test_status_update(test, "Returned joint format does not contain expected codec\n");
		return AST_TEST_FAIL;
	}

	ast_format_generate_sdp_fmtp(joint, 100, &fmtp);
	ast_test_validate(test, strcmp("a=fmtp:100 one=5;two=0\r\n", ast_str_buffer(fmtp)) == 0);

	ast_test_validate(test, test_callbacks_called.format_attribute_set == 2);
	ast_test_validate(test, test_callbacks_called.format_get_joint == 1);
	ast_test_validate(test, test_callbacks_called.format_generate_sdp_fmtp == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_joint_different_codec)
{
	RAII_VAR(struct ast_codec *, first_codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, second_codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, first, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, second, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, joint, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Joint format unit test";
		info->description =
			"Test that there is no joint format between two different formats with different codec";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	first_codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!first_codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	first = ast_format_create(first_codec);
	if (!first) {
		ast_test_status_update(test, "Could not create first format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	second_codec = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!second_codec) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	second = ast_format_create(second_codec);
	if (!second) {
		ast_test_status_update(test, "Could not create second format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	joint = ast_format_joint(first, second);
	if (joint) {
		ast_test_status_update(test, "Got a joint format between two formats with different codecs\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_copy)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, copy, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format copying unit test";
		info->description =
			"Test copying of a format";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	copy = ao2_bump(format);
	if (!copy) {
		ast_test_status_update(test, "Copying of a just created format failed\n");
		return AST_TEST_FAIL;
	} else if (copy != format) {
		ast_test_status_update(test, "Copying of a format returned a new format instead of the same one\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_attribute_set_without_interface)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format attribute setting unit test";
		info->description =
			"Test that attribute setting on a format without an interface fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (!ast_format_attribute_set(format, "bees", "cool")) {
		ast_test_status_update(test, "Successfully set an attribute on a format without an interface\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_parse_sdp_fmtp_without_interface)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, generated, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format sdp parse unit test";
		info->description =
			"Test that sdp parsing on a format without an interface fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	generated = ast_format_parse_sdp_fmtp(format, "tacos");
	if (generated != format) {
		ast_test_status_update(test, "Successfully parsed SDP on a format without an interface\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_parse_and_generate_sdp_fmtp)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, generated, NULL, ao2_cleanup);
	struct ast_str *fmtp = ast_str_alloca(64);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = TEST_CATEGORY;
		info->summary = "Format sdp parse/generate unit test";
		info->description =
			"Test that sdp parsing and generation on a format with an interface succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("test_core_format_codec", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create(codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using test_core_format_codec codec\n");
		return AST_TEST_FAIL;
	}

	generated = ast_format_parse_sdp_fmtp(format, "one=1000;two=256");
	if (format == generated) {
		ast_test_status_update(test, "Failed to parse SDP on a format without an interface\n");
		return AST_TEST_FAIL;
	}

	ast_format_generate_sdp_fmtp(generated, 8, &fmtp);

	ast_test_validate(test, strcmp("a=fmtp:8 one=1000;two=256\r\n", ast_str_buffer(fmtp)) == 0);
	ast_test_validate(test, test_callbacks_called.format_parse_sdp_fmtp == 1);
	ast_test_validate(test, test_callbacks_called.format_generate_sdp_fmtp == 1);

	return AST_TEST_PASS;
}

static int test_core_format_init(struct ast_test_info *info, struct ast_test *test)
{
	memset(&test_callbacks_called, 0, sizeof(test_callbacks_called));

	return 0;
}

static int load_module(void)
{
	/* Test codec/format interface used by this module */
	if (ast_codec_register(&test_core_format_codec)) {
		ast_log(AST_LOG_ERROR, "Failed to register test_core_format_codec\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_format_interface_register("test_core_format_codec", &test_core_format_attr)) {
		ast_log(AST_LOG_ERROR, "Failed to register format interface for test_core_format_codec\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(format_create);
	AST_TEST_REGISTER(format_create_attr);
	AST_TEST_REGISTER(format_clone);
	AST_TEST_REGISTER(format_cmp_same_codec);
	AST_TEST_REGISTER(format_attr_cmp_same_codec);
	AST_TEST_REGISTER(format_cmp_different_codec);
	AST_TEST_REGISTER(format_joint_same_codec);
	AST_TEST_REGISTER(format_attr_joint_same_codec);
	AST_TEST_REGISTER(format_joint_different_codec);
	AST_TEST_REGISTER(format_copy);
	AST_TEST_REGISTER(format_attribute_set_without_interface);
	AST_TEST_REGISTER(format_parse_sdp_fmtp_without_interface);
	AST_TEST_REGISTER(format_parse_and_generate_sdp_fmtp);

	ast_test_register_init(TEST_CATEGORY, &test_core_format_init);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Core format API test module");
