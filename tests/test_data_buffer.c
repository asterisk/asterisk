/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2018, Digium, Inc.
 *
 * Ben Ford <bford@digium.com>
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
 * \author Ben Ford <bford@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/data_buffer.h"

#define BUFFER_MAX_NOMINAL 10

struct mock_payload
{
	int id;
};

/* Ensures that RAII_VAR will not trip ast_assert(buffer != NULL) in the callback */
static void ast_data_buffer_free_wrapper(struct ast_data_buffer *buffer)
{
	if (!buffer) {
		return;
	}

	ast_data_buffer_free(buffer);
}

AST_TEST_DEFINE(buffer_create)
{
	RAII_VAR(struct ast_data_buffer *, buffer, NULL, ast_data_buffer_free_wrapper);

	switch (cmd) {
	case TEST_INIT:
		info->name = "buffer_create";
		info->category = "/main/data_buffer/";
		info->summary = "buffer create unit test";
		info->description =
			"Test that creating a data buffer results in a buffer with the expected values";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_data_buffer_alloc(ast_free_ptr, BUFFER_MAX_NOMINAL);

	ast_test_validate(test, buffer != NULL,
			"Failed to create buffer with valid arguments");
	ast_test_validate(test, ast_data_buffer_count(buffer) == 0,
			"Newly created buffer does not have the expected payload count");
	ast_test_validate(test, ast_data_buffer_max(buffer) == BUFFER_MAX_NOMINAL,
			"Newly created buffer does not have the expected max size");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(buffer_put)
{
	RAII_VAR(struct ast_data_buffer *, buffer, NULL, ast_data_buffer_free_wrapper);
	struct mock_payload *payload;
	struct mock_payload *fetched_payload;
	int ret;

	switch (cmd) {
	case TEST_INIT:
		info->name = "buffer_put";
		info->category = "/main/data_buffer/";
		info->summary = "buffer put unit test";
		info->description =
			"Test that putting payloads in the buffer yields the expected results";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_data_buffer_alloc(ast_free_ptr, 2);

	ast_test_validate(test, buffer != NULL,
			"Failed to create buffer with valid arguments");
	ast_test_validate(test, ast_data_buffer_count(buffer) == 0,
			"Newly created buffer is not empty");

	payload = ast_calloc(1, sizeof(*payload));

	ast_test_validate(test, payload != NULL,
			"Failed to allocate memory for first payload");

	payload->id = 2;
	ret = ast_data_buffer_put(buffer, 2, payload);

	ast_test_validate(test, ret == 0,
			"Adding a payload to an empty buffer did not return the expected value");
	ast_test_validate(test, ast_data_buffer_count(buffer) == 1,
			"Adding a payload to an empty buffer did not update count to the expected value");

	fetched_payload = (struct mock_payload *)ast_data_buffer_get(buffer, 2);

	ast_test_validate(test, fetched_payload != NULL,
			"Failed to get only payload from buffer given valid arguments");

	ast_data_buffer_put(buffer, 2, payload);

	ast_test_validate(test, ast_data_buffer_count(buffer) == 1,
			"Adding a payload that is already in the buffer should not do anything");

	payload = ast_calloc(1, sizeof(*payload));

	ast_test_validate(test, payload != NULL,
			"Failed to allocate memory for second payload");

	payload->id = 1;
	ast_data_buffer_put(buffer, 1, payload);
	fetched_payload = ast_data_buffer_get(buffer, 1);

	ast_test_validate(test, fetched_payload != NULL,
			"Failed to get a payload from buffer given valid arguments");
	ast_test_validate(test, ast_data_buffer_count(buffer) == 2,
			"Buffer does not have the expected count after removing a payload");
	ast_test_validate(test, fetched_payload->id == 1,
			"Did not get the expected payload from the buffer");

	payload = ast_calloc(1, sizeof(*payload));

	ast_test_validate(test, payload != NULL,
			"Failed to allocate memory for third payload");

	payload->id = 3;
	ret = ast_data_buffer_put(buffer, 3, payload);

	ast_test_validate(test, ret == 0,
			"Failed to replace a payload in the buffer");
	ast_test_validate(test, ast_data_buffer_count(buffer) <= 2,
			"Buffer count exceeded the max");

	fetched_payload = (struct mock_payload *)ast_data_buffer_get(buffer, 3);

	ast_test_validate(test, fetched_payload != NULL,
			"Failed to get a payload from buffer at position 3 given valid arguments");
	ast_test_validate(test, fetched_payload->id == 3,
			"Did not get the expected payload at position 3 from the buffer");

	fetched_payload = (struct mock_payload *)ast_data_buffer_get(buffer, 2);

	ast_test_validate(test, fetched_payload != NULL,
			"Failed to get a payload from buffer at position 2 given valid arguments");
	ast_test_validate(test, fetched_payload->id == 2,
			"Did not get the expected payload at position 2 from the buffer");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(buffer_resize)
{
	RAII_VAR(struct ast_data_buffer *, buffer, NULL, ast_data_buffer_free_wrapper);

	switch (cmd) {
	case TEST_INIT:
		info->name = "buffer_resize";
		info->category = "/main/data_buffer/";
		info->summary = "buffer resize unit test";
		info->description =
			"Tests resizing a data buffer to make sure it has the expected outcome";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_data_buffer_alloc(ast_free_ptr, BUFFER_MAX_NOMINAL);

	ast_test_validate(test, buffer != NULL,
			"Failed to create buffer with valid arguments");

	ast_data_buffer_resize(buffer, BUFFER_MAX_NOMINAL);

	ast_test_validate(test, ast_data_buffer_max(buffer) == BUFFER_MAX_NOMINAL,
			"Trying to resize buffer to same size should not change its max size");

	ast_data_buffer_resize(buffer, BUFFER_MAX_NOMINAL + 2);

	ast_test_validate(test, ast_data_buffer_max(buffer) == BUFFER_MAX_NOMINAL + 2,
			"Increasing buffer size did not return the expected max");

	ast_data_buffer_resize(buffer, 1);

	ast_test_validate(test, ast_data_buffer_max(buffer) == 1,
			"Decreasing buffer size did not return the expected max");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(buffer_nominal)
{
	RAII_VAR(struct ast_data_buffer *, buffer, NULL, ast_data_buffer_free_wrapper);
	RAII_VAR(struct mock_payload *, removed_payload, NULL, ast_free_ptr);
	struct mock_payload *payload;
	struct mock_payload *fetched_payload;
	int ret;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "buffer_nominal";
		info->category = "/main/data_buffer/";
		info->summary = "buffer nominal unit test";
		info->description =
			"Tests the normal usage of a data buffer to ensure the expected payloads "
			"are present after multiple insertions";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_data_buffer_alloc(ast_free_ptr, BUFFER_MAX_NOMINAL);

	ast_test_validate(test, buffer != NULL,
			"Failed to create buffer with valid arguments");

	for (i = 1; i <= BUFFER_MAX_NOMINAL; i++) {
		payload = ast_calloc(1, sizeof(*payload));

		ast_test_validate(test, payload != NULL,
				"Failed to allocate memory for payload %d", i);

		ret = ast_data_buffer_put(buffer, i, payload);
		if (ret) {
			ast_free(payload);
		}

		ast_test_validate(test, ret == 0,
				"Failed to add payload %d to buffer", i);
	}

	ast_test_validate(test, ast_data_buffer_count(buffer) == BUFFER_MAX_NOMINAL,
			"Buffer does not have the expected count after adding payloads");

	for (i = 1; i <= BUFFER_MAX_NOMINAL; i++) {
		fetched_payload = (struct mock_payload *)ast_data_buffer_get(buffer, i);

		ast_test_validate(test, fetched_payload != NULL,
				"Failed to get payload at position %d during first loop", i);
	}

	for (i = 1; i <= BUFFER_MAX_NOMINAL; i++) {
		payload = ast_calloc(1, sizeof(*payload));

		ast_test_validate(test, payload != NULL,
				"Failed to allocate memory for payload %d", i + BUFFER_MAX_NOMINAL);

		payload->id = i;
		ret = ast_data_buffer_put(buffer, i + BUFFER_MAX_NOMINAL, payload);
		if (ret) {
			ast_free(payload);
		}

		ast_test_validate(test, ret == 0,
				"Failed to add payload %d to buffer", i + BUFFER_MAX_NOMINAL);
	}

	ast_test_validate(test, ast_data_buffer_count(buffer) == BUFFER_MAX_NOMINAL,
			"Buffer does not have the expected count after replacing payloads");

	for (i = 1; i <= BUFFER_MAX_NOMINAL; i++) {
		fetched_payload = (struct mock_payload *)ast_data_buffer_get(buffer, i);

		ast_test_validate(test, fetched_payload == NULL,
				"Got an unexpected payload at position %d", i);

		fetched_payload = (struct mock_payload *)ast_data_buffer_get(buffer, i + BUFFER_MAX_NOMINAL);

		ast_test_validate(test, fetched_payload != NULL,
				"Failed to get payload at position %d during second loop", i + BUFFER_MAX_NOMINAL);
	}

	removed_payload = (struct mock_payload *)ast_data_buffer_remove_head(buffer);

	ast_test_validate(test, removed_payload != NULL,
			"Failed to get the payload at the HEAD of the buffer");

	ast_test_validate(test, ast_data_buffer_count(buffer) == BUFFER_MAX_NOMINAL - 1,
			"Removing payload from HEAD of buffer did not decrease buffer size");

	ast_test_validate(test, removed_payload->id == 1,
			"Removing payload from HEAD of buffer did not return expected payload");

	ast_free(removed_payload);

	removed_payload = (struct mock_payload *)ast_data_buffer_remove(buffer, BUFFER_MAX_NOMINAL * 2);

	ast_test_validate(test, removed_payload != NULL,
			"Failed to get payload at position %d from buffer", BUFFER_MAX_NOMINAL * 2);

	ast_test_validate(test, ast_data_buffer_count(buffer) == BUFFER_MAX_NOMINAL - 2,
			"Removing payload from buffer did not decrease buffer size");

	ast_test_validate(test, removed_payload->id == BUFFER_MAX_NOMINAL,
			"Removing payload from buffer did not return expected payload");

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(buffer_create);
	AST_TEST_UNREGISTER(buffer_put);
	AST_TEST_UNREGISTER(buffer_resize);
	AST_TEST_UNREGISTER(buffer_nominal);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(buffer_create);
	AST_TEST_REGISTER(buffer_put);
	AST_TEST_REGISTER(buffer_resize);
	AST_TEST_REGISTER(buffer_nominal);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Data buffer API test module");
