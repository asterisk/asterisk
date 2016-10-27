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
 * \brief Core Codec API Unit Tests
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
#include "asterisk/codec.h"

static struct ast_codec known_unknown = {
	.name = "unit_test",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

static struct ast_codec doubly = {
	.name = "unit_test_double",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

static struct ast_codec unknown = {
	.name = "unit_test_unknown",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_UNKNOWN,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

static struct ast_codec audio_without_rate = {
	.name = "unit_test_audio_without_rate",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

static struct ast_codec audio_get = {
	.name = "unit_test_audio_get",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

static struct ast_codec audio_get_unknown = {
	.name = "unit_test_audio_get_unknown",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

static struct ast_codec audio_get_id = {
	.name = "unit_test_audio_get_id",
	.description = "Unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
};

AST_TEST_DEFINE(codec_register)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_register";
		info->category = "/main/core_codec/";
		info->summary = "codec registration unit test";
		info->description =
			"Test registration of a core codec that is known to be unknown";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_codec_register(&known_unknown)) {
		ast_test_status_update(test, "Unsuccessfully registered a codec that is known to be unknown\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_register_twice)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_register_twice";
		info->category = "/main/core_codec/";
		info->summary = "codec registration unit test";
		info->description =
			"Test double registration of a core codec to confirm it fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_codec_register(&doubly)) {
		ast_test_status_update(test, "Unsuccessfully registered a codec that is known to be unknown\n");
		return AST_TEST_FAIL;
	}

	if (!ast_codec_register(&doubly)) {
		ast_test_status_update(test, "Successfully registered a codec twice\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_register_unknown)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_register_unknown";
		info->category = "/main/core_codec/";
		info->summary = "codec registration unit test";
		info->description =
			"Test that registration of an unknown codec type fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!ast_codec_register(&unknown)) {
		ast_test_status_update(test, "Successfully registered a codec with an unknown media type\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_register_audio_no_sample_rate)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_register_audio_no_sample_rate";
		info->category = "/main/core_codec/";
		info->summary = "codec registration unit test";
		info->description =
			"Test that registration of an audio codec without sample rate fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!ast_codec_register(&audio_without_rate)) {
		ast_test_status_update(test, "Successfully registered an audio codec without a sample rate\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_get)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_get";
		info->category = "/main/core_codec/";
		info->summary = "codec get unit test";
		info->description =
			"Test that getting of a known codec succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_codec_register(&audio_get)) {
		ast_test_status_update(test, "Unsucessfully registered a codec for getting\n");
		return AST_TEST_FAIL;
	}

	codec = ast_codec_get("unit_test_audio_get", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!codec) {
		ast_test_status_update(test, "Unsuccessfully retrieved a codec we just registered\n");
		return AST_TEST_FAIL;
	} else if (strcmp(codec->name, audio_get.name)) {
		ast_test_status_update(test, "Name of retrieved codec does not match registered codec\n");
		return AST_TEST_FAIL;
	} else if (codec->type != audio_get.type) {
		ast_test_status_update(test, "Type of retrieved codec does not match registered codec\n");
		return AST_TEST_FAIL;
	} else if (codec->sample_rate != audio_get.sample_rate) {
		ast_test_status_update(test, "Sample rate of retrieved codec does not match registered codec\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_get_unregistered)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_get_unregistered";
		info->category = "/main/core_codec/";
		info->summary = "codec get unit test";
		info->description =
			"Test that getting of a codec that is not registered fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	codec = ast_codec_get("goats", AST_MEDIA_TYPE_AUDIO, 8000);
	if (codec) {
		ast_test_status_update(test, "Successfully got a codec named '%s' when getting a codec named 'goats'\n",
			codec->name);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_get_unknown)
{
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_get_unknown";
		info->category = "/main/core_codec/";
		info->summary = "codec get unit test";
		info->description =
			"Test that getting of a known codec using name and unknown type succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_codec_register(&audio_get_unknown)) {
		ast_test_status_update(test, "Unsucessfully registered a codec for getting\n");
		return AST_TEST_FAIL;
	}

	codec = ast_codec_get("unit_test_audio_get_unknown", AST_MEDIA_TYPE_UNKNOWN, 8000);
	if (!codec) {
		ast_test_status_update(test, "Unsuccessfully retrieved a codec we just registered\n");
		return AST_TEST_FAIL;
	} else if (strcmp(codec->name, audio_get_unknown.name)) {
		ast_test_status_update(test, "Name of retrieved codec does not match registered codec\n");
		return AST_TEST_FAIL;
	} else if (codec->type != audio_get_unknown.type) {
		ast_test_status_update(test, "Type of retrieved codec does not match registered codec\n");
		return AST_TEST_FAIL;
	} else if (codec->sample_rate != audio_get_unknown.sample_rate) {
		ast_test_status_update(test, "Sample rate of retrieved codec does not match registered codec\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(codec_get_id)
{
	RAII_VAR(struct ast_codec *, named, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "codec_get_unknown";
		info->category = "/main/core_codec/";
		info->summary = "codec get unit test";
		info->description =
			"Test that getting of a known codec using name and unknown type succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_codec_register(&audio_get_id)) {
		ast_test_status_update(test, "Unsucessfully registered a codec for getting\n");
		return AST_TEST_FAIL;
	}

	named = ast_codec_get("unit_test_audio_get_id", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!named) {
		ast_test_status_update(test, "Unsuccessfully retrieved a codec we just registered\n");
		return AST_TEST_FAIL;
	}

	codec = ast_codec_get_by_id(named->id);
	if (!codec) {
		ast_test_status_update(test, "Unsuccessfully retrieved a codec using id of a named codec we just got\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(codec_register);
	AST_TEST_UNREGISTER(codec_register_twice);
	AST_TEST_UNREGISTER(codec_register_unknown);
	AST_TEST_UNREGISTER(codec_register_audio_no_sample_rate);
	AST_TEST_UNREGISTER(codec_get);
	AST_TEST_UNREGISTER(codec_get_unregistered);
	AST_TEST_UNREGISTER(codec_get_unknown);
	AST_TEST_UNREGISTER(codec_get_id);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(codec_register);
	AST_TEST_REGISTER(codec_register_twice);
	AST_TEST_REGISTER(codec_register_unknown);
	AST_TEST_REGISTER(codec_register_audio_no_sample_rate);
	AST_TEST_REGISTER(codec_get);
	AST_TEST_REGISTER(codec_get_unregistered);
	AST_TEST_REGISTER(codec_get_unknown);
	AST_TEST_REGISTER(codec_get_id);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Core codec API test module");
