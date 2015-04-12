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
 * \brief Format Cache API Unit Tests
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
#include "asterisk/format_cache.h"

AST_TEST_DEFINE(format_cache_set)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cache_set";
		info->category = "/main/format_cache/";
		info->summary = "format cache add unit test";
		info->description =
			"Test that adding of a cached format succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create_named("ulaw@20_1", codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cache_set(format)) {
		ast_test_status_update(test, "Could not add just created format to cache\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cache_set_duplicate)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cache_set_duplicate";
		info->category = "/main/format_cache/";
		info->summary = "format cache add unit test";
		info->description =
			"Test that adding of a cached format multiple times succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create_named("ulaw@20_2", codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cache_set(format)) {
		ast_test_status_update(test, "Could not add just created format to cache\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cache_set(format)) {
		ast_test_status_update(test, "Failed to update cached format\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cache_set_null)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cache_set_null";
		info->category = "/main/format_cache/";
		info->summary = "format cache add unit test";
		info->description =
			"Test that adding a NULL or empty format to the cache does not succeed";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create_named("", codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (!ast_format_cache_set(format)) {
		ast_test_status_update(test, "Successfully cached a format with an empty name\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cache_get)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, cached, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cache_get";
		info->category = "/main/format_cache/";
		info->summary = "format cache get unit test";
		info->description =
			"Test that getting of a cached format succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create_named("ulaw@20", codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cache_set(format)) {
		ast_test_status_update(test, "Could not add just created format to cache\n");
		return AST_TEST_FAIL;
	}

	cached = ast_format_cache_get("ulaw@20");
	if (!cached) {
		ast_test_status_update(test, "Failed to retrieve a format we just cached\n");
		return AST_TEST_FAIL;
	} else if (cached != format) {
		ast_test_status_update(test, "Returned cached format does not match format we just added\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cache_get_nonexistent)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, cached, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cache_get_nonxistent";
		info->category = "/main/format_cache/";
		info->summary = "format cache get unit test";
		info->description =
			"Test that getting of a non-existent cached format does not succeed";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	format = ast_format_create_named("ulaw@40", codec);
	if (!format) {
		ast_test_status_update(test, "Could not create format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cache_set(format)) {
		ast_test_status_update(test, "Could not add just created format to cache\n");
		return AST_TEST_FAIL;
	}

	cached = ast_format_cache_get("ulaw@60");
	if (cached) {
		ast_test_status_update(test, "Retrieved a cached format when one should not have existed\n");
		return AST_TEST_FAIL;
	}

	cached = ast_format_cache_get("");
	if (cached) {
		ast_test_status_update(test, "Retrieved a cached format when we provided an empty name\n");
		return AST_TEST_FAIL;
	}

	cached = ast_format_cache_get(NULL);
	if (cached) {
		ast_test_status_update(test, "Retrieved a cached format when we provided a NULL name\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(format_cache_set);
	AST_TEST_UNREGISTER(format_cache_set_duplicate);
	AST_TEST_UNREGISTER(format_cache_set_null);
	AST_TEST_UNREGISTER(format_cache_get);
	AST_TEST_UNREGISTER(format_cache_get_nonexistent);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(format_cache_set);
	AST_TEST_REGISTER(format_cache_set_duplicate);
	AST_TEST_REGISTER(format_cache_set_null);
	AST_TEST_REGISTER(format_cache_get);
	AST_TEST_REGISTER(format_cache_get_nonexistent);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Format cache API test module");
