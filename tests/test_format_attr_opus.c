/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Remi Quezada
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

/*! \file
 * \brief Opus format comparison tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_format_attr_opus</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_cap.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

AST_TEST_DEFINE(opus_format_cmp)
{
	static const struct {
		const char *name;
		const char *value;
	} attributes[] = {
		{ "maxaveragebitrate", "32000" },
		{ "maxplaybackrate", "16000" },
		{ "ptime", "40" },
		{ "stereo", "1" },
		{ "cbr", "1" },
		{ "useinbandfec", "1" },
		{ "usedtx", "1" },
		{ "sprop-maxcapturerate", "16000" },
		{ "sprop-stereo", "1" },
		{ "maxptime", "60" },
	};
	RAII_VAR(struct ast_format *, defaults, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	size_t i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "opus_format_cmp";
		info->category = "/res/format_attr/opus/";
		info->summary = "Opus format equality and compatibility";
		info->description = "Check defaults and each negotiated attribute without rejecting compatible Opus formats";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	defaults = ast_format_parse_sdp_fmtp(ast_format_opus, "");
	ast_test_validate(test, defaults != NULL);
	ast_test_validate(test, ast_format_cmp(ast_format_opus, defaults) == AST_FORMAT_CMP_EQUAL);
	ast_test_validate(test, ast_format_cmp(defaults, ast_format_opus) == AST_FORMAT_CMP_EQUAL);
	ast_test_validate(test, ast_format_cmp(defaults, ast_format_ulaw) == AST_FORMAT_CMP_NOT_EQUAL);

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	ast_test_validate(test, caps != NULL);
	ast_test_validate(test, !ast_format_cap_append(caps, defaults, 0));

	for (i = 0; i < ARRAY_LEN(attributes); ++i) {
		RAII_VAR(struct ast_format *, changed, NULL, ao2_cleanup);
		RAII_VAR(struct ast_format *, clone, NULL, ao2_cleanup);
		RAII_VAR(struct ast_format *, joint, NULL, ao2_cleanup);
		const char *value = attributes[i].value;

		/* FEC's default is build-dependent, so always exercise a real change. */
		if (!strcmp(attributes[i].name, "useinbandfec")) {
			const int *fec = ast_format_attribute_get(defaults, attributes[i].name);

			value = fec && *fec ? "0" : "1";
		}

		ast_test_status_update(test, "Comparing attribute %s\n", attributes[i].name);
		changed = ast_format_attribute_set(defaults, attributes[i].name, value);
		ast_test_validate(test, changed != NULL);
		ast_test_validate(test, ast_format_cmp(defaults, changed) == AST_FORMAT_CMP_SUBSET);
		ast_test_validate(test, ast_format_cmp(changed, defaults) == AST_FORMAT_CMP_SUBSET);
		clone = ast_format_clone(changed);
		ast_test_validate(test, clone != NULL);
		ast_test_validate(test, ast_format_cmp(changed, clone) == AST_FORMAT_CMP_EQUAL);
		joint = ast_format_cap_get_compatible_format(caps, changed);
		ast_test_validate(test, joint != NULL);
	}

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(opus_format_cmp);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(opus_format_cmp);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Opus format attribute tests",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_format_attr_opus",
);
