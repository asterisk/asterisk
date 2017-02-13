/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
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
 * \brief Media Stream API Unit Tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/stream.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"

AST_TEST_DEFINE(stream_create)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_create";
		info->category = "/main/stream/";
		info->summary = "stream create unit test";
		info->description =
			"Test that creating a stream results in a stream with the expected values";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_state(stream) != AST_STREAM_STATE_INACTIVE) {
		ast_test_status_update(test, "Newly created stream does not have expected inactive stream state\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "Newly created stream does not have expected audio media type\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(ast_stream_get_name(stream), "test")) {
		ast_test_status_update(test, "Newly created stream does not have expected name of test\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_create_no_name)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_create_no_name";
		info->category = "/main/stream/";
		info->summary = "stream create (without a name) unit test";
		info->description =
			"Test that creating a stream with no name works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_create(NULL, AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_type)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_set_type";
		info->category = "/main/stream/";
		info->summary = "stream type setting unit test";
		info->description =
			"Test that changing the type of a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "Newly created stream does not have expected audio media type\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_type(stream, AST_MEDIA_TYPE_VIDEO);

	if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_VIDEO) {
		ast_test_status_update(test, "Changed stream does not have expected video media type\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_formats)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_set_formats";
		info->category = "/main/stream/";
		info->summary = "stream formats setting unit test";
		info->description =
			"Test that changing the formats of a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Failed to create a format capabilities structure for testing\n");
		return AST_TEST_FAIL;
	}

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_formats(stream, caps);

	if (ast_stream_get_formats(stream) != caps) {
		ast_test_status_update(test, "Changed stream does not have expected formats\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_formats(stream, NULL);

	if (ast_stream_get_formats(stream)) {
		ast_test_status_update(test, "Retrieved formats from stream despite removing them\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_state)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_set_state";
		info->category = "/main/stream/";
		info->summary = "stream state setting unit test";
		info->description =
			"Test that changing the state of a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_state(stream) != AST_STREAM_STATE_INACTIVE) {
		ast_test_status_update(test, "Newly created stream does not have expected inactive stream state\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);

	if (ast_stream_get_state(stream) != AST_STREAM_STATE_SENDRECV) {
		ast_test_status_update(test, "Changed stream does not have expected sendrecv state\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(stream_create);
	AST_TEST_UNREGISTER(stream_create_no_name);
	AST_TEST_UNREGISTER(stream_set_type);
	AST_TEST_UNREGISTER(stream_set_formats);
	AST_TEST_UNREGISTER(stream_set_state);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(stream_create);
	AST_TEST_REGISTER(stream_create_no_name);
	AST_TEST_REGISTER(stream_set_type);
	AST_TEST_REGISTER(stream_set_formats);
	AST_TEST_REGISTER(stream_set_state);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Media Stream API test module");
