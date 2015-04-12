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
 * \brief Format Capabilities API Unit Tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/codec.h"
#include "asterisk/frame.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"

AST_TEST_DEFINE(format_cap_alloc)
{
	struct ast_format_cap *caps;

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_alloc";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities allocation unit test";
		info->description =
			"Test that allocation of a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}
	ao2_ref(caps, -1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_single)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, retrieved, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities adding unit test";
		info->description =
			"Test that adding a single format to a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
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

	if (ast_format_cap_append(caps, format, 42)) {
		ast_test_status_update(test, "Could not add newly created format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps) != 1) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 1 but is %zu\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	retrieved = ast_format_cap_get_format(caps, 0);
	if (!retrieved) {
		ast_test_status_update(test, "Attempted to get single format from capabilities structure but got nothing\n");
		return AST_TEST_FAIL;
	} else if (retrieved != format) {
		ast_test_status_update(test, "Retrieved format is not the same as the one we added\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_get_format_framing(caps, retrieved) != 42) {
		ast_test_status_update(test, "Framing for format in capabilities structure does not match what we provided\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_multiple)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, retrieved, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities adding unit test";
		info->description =
			"Test that adding multiple formats to a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ulaw_format, 42)) {
		ast_test_status_update(test, "Could not add newly created ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(caps, alaw_format, 84)) {
		ast_test_status_update(test, "Could not add newly created alaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps) != 2) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 2 but is %zu\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	retrieved = ast_format_cap_get_format(caps, 0);
	if (!retrieved) {
		ast_test_status_update(test, "Attempted to get first format from capabilities structure but got nothing\n");
		return AST_TEST_FAIL;
	} else if (retrieved != ulaw_format) {
		ast_test_status_update(test, "First retrieved format is not the ulaw one we added\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_get_format_framing(caps, retrieved) != 42) {
		ast_test_status_update(test, "Framing for ulaw format in capabilities structure does not match what we provided\n");
	}
	ao2_ref(retrieved, -1);

	retrieved = ast_format_cap_get_format(caps, 1);
	if (!retrieved) {
		ast_test_status_update(test, "Attempted to get second format from capabilities structure but got nothing\n");
		return AST_TEST_FAIL;
	} else if (retrieved != alaw_format) {
		ast_test_status_update(test, "First retrieved format is not the alaw one we added\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_get_format_framing(caps, retrieved) != 84) {
		ast_test_status_update(test, "Framing for alaw format in capabilities structure does not match what we provided\n");
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_all_unknown)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities adding unit test";
		info->description =
			"Test that adding of all formats to a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(caps, AST_MEDIA_TYPE_UNKNOWN)) {
		ast_test_status_update(test, "Failed to add all media formats of all types to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Added all media formats but no audio formats exist when they should\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Added all media formats but no video formats exist when they should\n");
		return AST_TEST_FAIL;
	} else if ((ast_format_cap_count(caps) + 1) != ast_codec_get_max()) {
		ast_test_status_update(test, "The number of formats in the capabilities structure does not match known number\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_all_audio)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities adding unit test";
		info->description =
			"Test that adding of all audio formats to a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Failed to add all audio media formats to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Added audio media formats but no audio formats exist when they should\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_has_type(caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Added only audio media formats but video formats exist when they should not\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_has_type(caps, AST_MEDIA_TYPE_TEXT)) {
		ast_test_status_update(test, "Added only audio media formats but text formats exist when they should not\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_has_type(caps, AST_MEDIA_TYPE_IMAGE)) {
		ast_test_status_update(test, "Added only audio media formats but image formats exist when they should not\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_duplicate)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format_named, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, retrieved, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities duplication unit test";
		info->description =
			"Test that adding a single format multiple times to a capabilities structure results in only a single format";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
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

	format_named = ast_format_create_named("ulaw@20", codec);
	if (!format_named) {
		ast_test_status_update(test, "Could not create named format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, format, 42)) {
		ast_test_status_update(test, "Could not add newly created format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps) != 1) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 1 but is %zu\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	/* Note: regardless of it being a duplicate, ast_format_cap_append should return success */
	if (ast_format_cap_append(caps, format, 0)) {
		ast_test_status_update(test, "Adding of duplicate format to capabilities structure failed\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps) != 1) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 1 but is %zu\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, format_named, 0)) {
		ast_test_status_update(test, "Adding of duplicate named format to capabilities structure failed\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps) != 1) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 1 but is %zu\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	retrieved = ast_format_cap_get_format(caps, 0);
	if (!retrieved) {
		ast_test_status_update(test, "Attempted to get single format from capabilities structure but got nothing\n");
		return AST_TEST_FAIL;
	} else if (retrieved != format) {
		ast_test_status_update(test, "Retrieved format is not the same as the one we added\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_get_format_framing(caps, retrieved) != 42) {
		ast_test_status_update(test, "Framing for format in capabilities structure does not match what we provided\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_from_cap)
{
	RAII_VAR(struct ast_format_cap *, dst_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, src_caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities append unit test";
		info->description =
			"Test that appending video formats from one capabilities structure to another succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	dst_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!dst_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(dst_caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Failed to add all audio media formats to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	src_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!src_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(src_caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Failed to add all video media formats to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append_from_cap(dst_caps, src_caps, AST_MEDIA_TYPE_UNKNOWN)) {
		ast_test_status_update(test, "Failed to append formats to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(dst_caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Successfully appended video formats to destination capabilities but it no longer contains audio formats\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(dst_caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Successfully appended formats but video formats do not exist in destination capabilities\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_append_from_cap_duplicate)
{
	RAII_VAR(struct ast_format_cap *, dst_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, src_caps, NULL, ao2_cleanup);
	unsigned int count;
	unsigned int total_count;

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities append duplicate unit test";
		info->description =
			"Test that appending capabilities structures multiple times does not result in duplicate formats";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	dst_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!dst_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(dst_caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Failed to add all audio media formats to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	src_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!src_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(src_caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Failed to add all video media formats to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	total_count = ast_format_cap_count(src_caps) + ast_format_cap_count(dst_caps);

	if (ast_format_cap_append_from_cap(dst_caps, src_caps, AST_MEDIA_TYPE_UNKNOWN)) {
		ast_test_status_update(test, "Failed to append formats to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(dst_caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Successfully appended video formats to destination capabilities but it no longer contains audio formats\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(dst_caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Successfully appended formats but video formats do not exist in destination capabilities\n");
		return AST_TEST_FAIL;
	}

	count = ast_format_cap_count(dst_caps);

	if (ast_format_cap_append_from_cap(dst_caps, src_caps, AST_MEDIA_TYPE_UNKNOWN)) {
		ast_test_status_update(test, "Failed to append duplicate formats to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ast_test_validate(test, count == ast_format_cap_count(dst_caps));
	ast_test_validate(test, count == total_count);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_set_framing)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_set_framing";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities framing unit test";
		info->description =
			"Test that global framing on a format capabilities structure is used when it should be";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ast_format_cap_set_framing(caps, 160);

	ast_test_validate(test, ast_format_cap_get_framing(caps) == 160);

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ulaw_format, 42)) {
		ast_test_status_update(test, "Could not add newly created ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(caps, alaw_format, 0)) {
		ast_test_status_update(test, "Could not add newly created alaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_get_format_framing(caps, ulaw_format) != 42) {
		ast_test_status_update(test, "Added ulaw format to capabilities structure with explicit framing but did not get it back\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_get_format_framing(caps, alaw_format) != ast_format_get_default_ms(alaw_format)) {
		ast_test_status_update(test, "Added alaw format to capabilities structure with no explicit framing but did not get global back\n");
		return AST_TEST_FAIL;
	}
	ast_test_validate(test, ast_format_cap_get_framing(caps) == ast_format_get_default_ms(alaw_format));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_remove_single)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, codec, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_remove_single";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities removal unit test";
		info->description =
			"Test that removing a single format from a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
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

	if (ast_format_cap_append(caps, format, 42)) {
		ast_test_status_update(test, "Could not add newly created format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_remove(caps, format)) {
		ast_test_status_update(test, "Could not remove format that was just added to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_remove(caps, format)) {
		ast_test_status_update(test, "Successfully removed a format twice from the capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps)) {
		ast_test_status_update(test, "Capabilities structure should be empty but instead it contains '%zu' formats\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_remove_multiple)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, retrieved, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_remove_multiple";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities removal unit test";
		info->description =
			"Test that removing a format from a format capabilities structure containing multiple formats succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ulaw_format, 42)) {
		ast_test_status_update(test, "Could not add newly created ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(caps, alaw_format, 84)) {
		ast_test_status_update(test, "Could not add newly created alaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_remove(caps, ulaw_format)) {
		ast_test_status_update(test, "Could not remove the ulaw format we just added to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_count(caps) != 1) {
		ast_test_status_update(test, "Capabilities structure should contain 1 format but it contains '%zu'\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	retrieved = ast_format_cap_get_format(caps, 0);
	if (!retrieved) {
		ast_test_status_update(test, "Attempted to get first format from capabilities structure but got nothing\n");
		return AST_TEST_FAIL;
	} else if (retrieved != alaw_format) {
		ast_test_status_update(test, "First retrieved format is not the alaw one we added\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_remove_bytype)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_remove_bytype";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities removal unit test";
		info->description =
			"Test that removal of a specific type of format from a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(caps, AST_MEDIA_TYPE_UNKNOWN)) {
		ast_test_status_update(test, "Failed to add all media formats of all types to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ast_format_cap_remove_by_type(caps, AST_MEDIA_TYPE_AUDIO);
	if (ast_format_cap_has_type(caps, AST_MEDIA_TYPE_AUDIO)) {
		ast_test_status_update(test, "Removed all audio type formats from capabilities structure but some remain\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_has_type(caps, AST_MEDIA_TYPE_VIDEO)) {
		ast_test_status_update(test, "Removed audio type formats from capabilities structure but video are gone as well\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_remove_all)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_remove_all";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities removal unit test";
		info->description =
			"Test that removal of all formats from a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append_by_type(caps, AST_MEDIA_TYPE_UNKNOWN)) {
		ast_test_status_update(test, "Failed to add all media formats of all types to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ast_format_cap_remove_by_type(caps, AST_MEDIA_TYPE_UNKNOWN);

	if (ast_format_cap_count(caps)) {
		ast_test_status_update(test, "Removed all formats from capabilities structure but some remain\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_get_compatible_format)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, compatible, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_get_compatible_format";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities negotiation unit test";
		info->description =
			"Test that getting a compatible format from a capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ulaw_format, 42)) {
		ast_test_status_update(test, "Could not add newly created ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	}

	compatible = ast_format_cap_get_compatible_format(caps, alaw_format);
	if (compatible) {
		ast_test_status_update(test, "Retrieved a compatible format from capabilities structure when none should exist\n");
		return AST_TEST_FAIL;
	}

	compatible = ast_format_cap_get_compatible_format(caps, ulaw_format);
	if (!compatible) {
		ast_test_status_update(test, "Did not retrieve a compatible format from capabilities structure when there should be one\n");
		return AST_TEST_FAIL;
	} else if (compatible != ulaw_format) {
		ast_test_status_update(test, "Compatible format is not the format we added to the capabilities structure\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_iscompatible_format)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_iscompatible_format";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities negotiation unit test";
		info->description =
			"Test that checking whether a format is compatible with a capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ulaw_format, 42)) {
		ast_test_status_update(test, "Could not add newly created ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_iscompatible_format(caps, alaw_format) != AST_FORMAT_CMP_NOT_EQUAL) {
		ast_test_status_update(test, "Alaw format is compatible with capabilities structure when it only contains ulaw\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_iscompatible_format(caps, ulaw_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_test_status_update(test, "Ulaw format is not compatible with capabilities structure when it should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_get_compatible)
{
	RAII_VAR(struct ast_format_cap *, alaw_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, ulaw_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, compatible_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_get_compatible";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities negotiation unit test";
		info->description =
			"Test that getting the compatible formats between two capabilities structures succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	alaw_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!alaw_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!ulaw_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	compatible_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!compatible_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(ulaw_caps, ulaw_format, 0)) {
		ast_test_status_update(test, "Could not add ulaw format to ulaw capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(alaw_caps, alaw_format, 0)) {
		ast_test_status_update(test, "Could not add alaw format to alaw capabilities\n");
		return AST_TEST_FAIL;
	}

	ast_format_cap_get_compatible(ulaw_caps, alaw_caps, compatible_caps);
	if (ast_format_cap_count(compatible_caps)) {
		ast_test_status_update(test, "A compatible format exists when none should\n");
		return AST_TEST_FAIL;
	}

	ast_format_cap_get_compatible(ulaw_caps, ulaw_caps, compatible_caps);
	if (!ast_format_cap_count(compatible_caps)) {
		ast_test_status_update(test, "No compatible formats exist when 1 should\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_iscompatible)
{
	RAII_VAR(struct ast_format_cap *, alaw_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, ulaw_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_iscompatible";
		info->category = "/main/format_cap/";
		info->summary = "format capabilities negotiation unit test";
		info->description =
			"Test that checking if there are compatible formats between two capabilities structures succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	alaw_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!alaw_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!ulaw_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(ulaw_caps, ulaw_format, 0)) {
		ast_test_status_update(test, "Could not add ulaw format to ulaw capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(alaw_caps, alaw_format, 0)) {
		ast_test_status_update(test, "Could not add alaw format to alaw capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_iscompatible(ulaw_caps, alaw_caps)) {
		ast_test_status_update(test, "Two capability structures that should not be compatible are\n");
		return AST_TEST_FAIL;
	} else if (!ast_format_cap_iscompatible(ulaw_caps, ulaw_caps)) {
		ast_test_status_update(test, "Capability structure is not compatible with itself\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_get_names)
{
	RAII_VAR(struct ast_format_cap *, empty_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, multi_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, alaw_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, ulaw_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);
	struct ast_str *buffer = ast_str_alloca(128);

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_get_names";
		info->category = "/main/format_cap/";
		info->summary = "Test getting the names of formats";
		info->description =
			"Test that obtaining the names from a format capabilities structure\n"
			"produces the expected output.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	empty_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!empty_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	multi_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!multi_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	alaw_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!alaw_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!ulaw_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(ulaw_caps, ulaw_format, 0)) {
		ast_test_status_update(test, "Could not add ulaw format to ulaw capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(alaw_caps, alaw_format, 0)) {
		ast_test_status_update(test, "Could not add alaw format to alaw capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(multi_caps, ulaw_format, 0)) {
		ast_test_status_update(test, "Could not add ulaw format to multi capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(multi_caps, alaw_format, 0)) {
		ast_test_status_update(test, "Could not add alaw format to multi capabilities\n");
		return AST_TEST_FAIL;
	}

	ast_format_cap_get_names(empty_caps, &buffer);
	ast_test_validate(test, !strcmp(ast_str_buffer(buffer), "(nothing)"));
	ast_format_cap_get_names(ulaw_caps, &buffer);
	ast_test_validate(test, !strcmp(ast_str_buffer(buffer), "(ulaw)"));
	ast_format_cap_get_names(alaw_caps, &buffer);
	ast_test_validate(test, !strcmp(ast_str_buffer(buffer), "(alaw)"));
	ast_format_cap_get_names(multi_caps, &buffer);
	ast_test_validate(test, !strcmp(ast_str_buffer(buffer), "(ulaw|alaw)"));


	return AST_TEST_PASS;
}

AST_TEST_DEFINE(format_cap_best_by_type)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, h263, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, h263_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, best_format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities best by type unit test";
		info->description =
			"Test that we can get the best format type out of a capabilities structure";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("ulaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve built-in ulaw codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	h263 = ast_codec_get("h263", AST_MEDIA_TYPE_VIDEO, 0);
	if (!h263) {
		ast_test_status_update(test, "Could not retrieve built-in h263 codec\n");
		return AST_TEST_FAIL;
	}

	h263_format = ast_format_create(h263);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create h263 format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ulaw_format, 0)) {
		ast_test_status_update(test, "Could not add ulaw format to capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(caps, alaw_format, 0)) {
		ast_test_status_update(test, "Could not add alaw format to capabilities\n");
		return AST_TEST_FAIL;
	} else if (ast_format_cap_append(caps, h263_format, 0)) {
		ast_test_status_update(test, "Could not add h263 format to capabilities\n");
		return AST_TEST_FAIL;
	}

	best_format = ast_format_cap_get_best_by_type(caps, AST_MEDIA_TYPE_UNKNOWN);
	ast_test_validate(test, ast_format_cmp(best_format, ulaw_format) == AST_FORMAT_CMP_EQUAL);
	ao2_ref(best_format, -1);

	best_format = ast_format_cap_get_best_by_type(caps, AST_MEDIA_TYPE_AUDIO);
	ast_test_validate(test, ast_format_cmp(best_format, ulaw_format) == AST_FORMAT_CMP_EQUAL);
	ao2_ref(best_format, -1);

	best_format = ast_format_cap_get_best_by_type(caps, AST_MEDIA_TYPE_VIDEO);
	ast_test_validate(test, ast_format_cmp(best_format, h263_format) == AST_FORMAT_CMP_EQUAL);
	ao2_ref(best_format, -1);

	best_format = ast_format_cap_get_best_by_type(caps, AST_MEDIA_TYPE_IMAGE);
	ast_test_validate(test, best_format == NULL);

	best_format = ast_format_cap_get_best_by_type(caps, AST_MEDIA_TYPE_TEXT);
	ast_test_validate(test, best_format == NULL);

	return AST_TEST_PASS;
}

static int test_law_samples(struct ast_frame *frame)
{
	return frame->datalen;
}

static int test_law_length(unsigned int samples)
{
	return samples;
}

static struct ast_codec test_law = {
	.name = "test_law",
	.description = "format cap unit test codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
	.samples_count = test_law_samples,
	.get_length = test_law_length,
	.smooth = 1,
};

static enum ast_format_cmp_res test_law_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	ast_log(LOG_ERROR, "Comparing format1 %p and format2 %p\n", format1, format2);
	return format1 == format2 ? AST_FORMAT_CMP_EQUAL : AST_FORMAT_CMP_NOT_EQUAL;
}

static void test_law_destroy(struct ast_format *format)
{
}

static int test_law_clone(const struct ast_format *src, struct ast_format *dst)
{
	return 0;
}

static struct ast_format_interface test_law_interface = {
	.format_cmp = test_law_cmp,
	.format_clone = test_law_clone,
	.format_destroy = test_law_destroy,
};

AST_TEST_DEFINE(format_cap_replace_from_cap)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, replace_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, result_caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, ulaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, ulaw_format_variant, NULL, ao2_cleanup);
	RAII_VAR(struct ast_codec *, alaw, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, alaw_format, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __PRETTY_FUNCTION__;
		info->category = "/main/format_cap/";
		info->summary = "format capabilities adding unit test";
		info->description =
			"Test that adding multiple formats to a format capabilities structure succeeds";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	replace_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	result_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps || !replace_caps || !result_caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	ulaw = ast_codec_get("test_law", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!ulaw) {
		ast_test_status_update(test, "Could not retrieve test_law codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format = ast_format_create(ulaw);
	if (!ulaw_format) {
		ast_test_status_update(test, "Could not create ulaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	ulaw_format_variant = ast_format_create(ulaw);
	if (!ulaw_format_variant) {
		ast_test_status_update(test, "Could not create ulaw format variant using built-in codec\n");
		return AST_TEST_FAIL;
	}

	alaw = ast_codec_get("alaw", AST_MEDIA_TYPE_AUDIO, 8000);
	if (!alaw) {
		ast_test_status_update(test, "Could not retrieve built-in alaw codec\n");
		return AST_TEST_FAIL;
	}

	alaw_format = ast_format_create(alaw);
	if (!alaw_format) {
		ast_test_status_update(test, "Could not create alaw format using built-in codec\n");
		return AST_TEST_FAIL;
	}

	/* fill caps with ulaw and alaw */
	if (ast_format_cap_append(caps, ulaw_format, 42)) {
		ast_test_status_update(test, "Could not add ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cap_append(caps, alaw_format, 84)) {
		ast_test_status_update(test, "Could not add alaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cap_count(caps) != 2) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 2 but is %zu\n",
			ast_format_cap_count(caps));
		return AST_TEST_FAIL;
	}

	/* fill replace_caps with the ulaw variant */
	if (ast_format_cap_append(replace_caps, ulaw_format_variant, 42)) {
		ast_test_status_update(test, "Could not add ulaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cap_count(replace_caps) != 1) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 1 but is %zu\n",
			ast_format_cap_count(replace_caps));
		return AST_TEST_FAIL;
	}

	/* fill result_caps with ulaw_variant and alaw */
	if (ast_format_cap_append(result_caps, ulaw_format_variant, 42)) {
		ast_test_status_update(test, "Could not add ulaw variant to capabilities structure\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cap_append(result_caps, alaw_format, 84)) {
		ast_test_status_update(test, "Could not add alaw format to capabilities structure\n");
		return AST_TEST_FAIL;
	}
	if (ast_format_cap_count(result_caps) != 2) {
		ast_test_status_update(test, "Number of formats in capabilities structure should be 2 but is %zu\n",
			ast_format_cap_count(result_caps));
		return AST_TEST_FAIL;
	}

	/* replace caps formats from replace_caps */
	ast_format_cap_replace_from_cap(caps, replace_caps, AST_MEDIA_TYPE_UNKNOWN);

	/* compare result_caps with caps */
	if (!ast_format_cap_identical(caps, result_caps)) {
		ast_test_status_update(test, "Actual and expected result caps differ\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(format_cap_alloc);
	AST_TEST_UNREGISTER(format_cap_append_single);
	AST_TEST_UNREGISTER(format_cap_append_multiple);
	AST_TEST_UNREGISTER(format_cap_append_all_unknown);
	AST_TEST_UNREGISTER(format_cap_append_all_audio);
	AST_TEST_UNREGISTER(format_cap_append_duplicate);
	AST_TEST_UNREGISTER(format_cap_append_from_cap);
	AST_TEST_UNREGISTER(format_cap_append_from_cap_duplicate);
	AST_TEST_UNREGISTER(format_cap_set_framing);
	AST_TEST_UNREGISTER(format_cap_remove_single);
	AST_TEST_UNREGISTER(format_cap_remove_multiple);
	AST_TEST_UNREGISTER(format_cap_remove_bytype);
	AST_TEST_UNREGISTER(format_cap_remove_all);
	AST_TEST_UNREGISTER(format_cap_get_names);
	AST_TEST_UNREGISTER(format_cap_get_compatible_format);
	AST_TEST_UNREGISTER(format_cap_iscompatible_format);
	AST_TEST_UNREGISTER(format_cap_get_compatible);
	AST_TEST_UNREGISTER(format_cap_iscompatible);
	AST_TEST_UNREGISTER(format_cap_best_by_type);
	AST_TEST_UNREGISTER(format_cap_replace_from_cap);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(format_cap_alloc);
	AST_TEST_REGISTER(format_cap_append_single);
	AST_TEST_REGISTER(format_cap_append_multiple);
	AST_TEST_REGISTER(format_cap_append_all_unknown);
	AST_TEST_REGISTER(format_cap_append_all_audio);
	AST_TEST_REGISTER(format_cap_append_duplicate);
	AST_TEST_REGISTER(format_cap_append_from_cap);
	AST_TEST_REGISTER(format_cap_append_from_cap_duplicate);
	AST_TEST_REGISTER(format_cap_set_framing);
	AST_TEST_REGISTER(format_cap_remove_single);
	AST_TEST_REGISTER(format_cap_remove_multiple);
	AST_TEST_REGISTER(format_cap_remove_bytype);
	AST_TEST_REGISTER(format_cap_remove_all);
	AST_TEST_REGISTER(format_cap_get_names);
	AST_TEST_REGISTER(format_cap_get_compatible_format);
	AST_TEST_REGISTER(format_cap_iscompatible_format);
	AST_TEST_REGISTER(format_cap_get_compatible);
	AST_TEST_REGISTER(format_cap_iscompatible);
	AST_TEST_REGISTER(format_cap_best_by_type);
	AST_TEST_REGISTER(format_cap_replace_from_cap);
	ast_codec_register(&test_law);
	ast_format_interface_register("test_law", &test_law_interface);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Format capabilities API test module");
