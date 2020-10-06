/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
 * \brief res_pjsip_session format caps tests
 *
 * \author George Joseph <gjoseph@sangoma.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/utils.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_pjsip_session_caps.h"

static enum ast_test_result_state test_create_joint(struct ast_test *test, const char *local_string,
	const char *remote_string, const char *pref_string, int is_outgoing, const char *expected_string,
	enum ast_test_result_state expected_result)
{
	RAII_VAR(struct ast_format_cap *, local, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, remote, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, joint, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	struct ast_str *joint_str = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
	const char *joint_string;
	char *stripped_joint;
	struct ast_flags codec_prefs;
	int rc;
	int i;

	ast_test_status_update(test, "Testing local: (%s), remote: (%s), pref: (%-12s), outgoing: (%s), expected: (%s) expected result: (%s)\n",
		local_string, remote_string, pref_string, is_outgoing ? "yes" : "no ", expected_string,
		expected_result == AST_TEST_PASS ? "PASS" : "FAIL");

	ast_test_validate(test, local != NULL && remote != NULL && joint != NULL);

	rc = ast_format_cap_update_by_allow_disallow(local, local_string, 1);
	if (rc != 0) {
		ast_test_status_update(test, "    %sxpected Failure: Coulldn't parse local codecs (%s)\n",
			expected_result == AST_TEST_FAIL ? "E" : "Une", local_string);
		return expected_result == AST_TEST_FAIL ? AST_TEST_PASS : AST_TEST_FAIL;
	}
	rc = ast_format_cap_update_by_allow_disallow(remote, remote_string, 1);
	if (rc != 0) {
		ast_test_status_update(test, "    %sxpected Failure: Coulldn't parse remote codecs (%s)\n",
			expected_result == AST_TEST_FAIL ? "E" : "Une", remote_string);
		return expected_result == AST_TEST_FAIL ? AST_TEST_PASS : AST_TEST_FAIL;
	}

	rc = ast_sip_call_codec_str_to_pref(&codec_prefs, pref_string, is_outgoing);
	if (rc != 0) {
		ast_test_status_update(test, "    %sxpected Failure: Invalid preference string incoming/outgoing combination.\n",
			expected_result == AST_TEST_FAIL ? "E" : "Une");
		return expected_result == AST_TEST_FAIL ? AST_TEST_PASS : AST_TEST_FAIL;
	}

	joint = ast_sip_create_joint_call_cap(remote, local, AST_MEDIA_TYPE_AUDIO, codec_prefs);
	if (joint == NULL) {
		ast_test_status_update(test, "    %sxpected Failure: No joint caps.\n",
			expected_result == AST_TEST_FAIL ? "E" : "Une");
		return expected_result == AST_TEST_FAIL ? AST_TEST_PASS : AST_TEST_FAIL;
	}

	joint_string = ast_format_cap_get_names(joint, &joint_str);
	stripped_joint = ast_str_truncate(joint_str, ast_str_strlen(joint_str) - 1) + 1;
	for(i = 0; i <= strlen(stripped_joint); i++) {
		if(stripped_joint[i] == '|') {
			stripped_joint[i] = ',';
		}
	}

	if (!joint_string || strcmp(stripped_joint, expected_string) != 0) {
		ast_test_status_update(test, "    %sxpected Failure: Expected: (%s) Actual: (%s)\n",
			expected_result == AST_TEST_FAIL ? "E" : "Une", expected_string, stripped_joint);
		return expected_result == AST_TEST_FAIL ? AST_TEST_PASS : AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

#define RUN_CREATE_JOINT(local, remote, pref, outgoing, expected, result) \
do { \
	if (test_create_joint(test, local, remote, pref, outgoing, expected, result) != AST_TEST_PASS) { \
		rc += 1; \
	} \
} while (0)

AST_TEST_DEFINE(low_level)
{
	int rc = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/res_pjsip_session/caps/";
		info->summary = "Test res_pjsip_session_caps";
		info->description = "Test res_pjsip_session_caps";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Incoming */

	ast_test_status_update(test, "Testing incoming expected pass\n");
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,alaw,g729",	"local", 		0,	"alaw,g722",	AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,alaw,g729",	"local_first",	0,	"alaw",			AST_TEST_PASS);
	RUN_CREATE_JOINT("slin",			"all",				"local",		0,	"slin",			AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,alaw,g729",	"remote",		0,	"g722,alaw",	AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,alaw,g729",	"remote_first",	0,	"g722",			AST_TEST_PASS);
	RUN_CREATE_JOINT("all",				"slin",				"remote_first",	0,	"slin",			AST_TEST_PASS);

	ast_test_status_update(test, "Testing incoming expected fail\n");
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g729",				"local",		0,	"",				AST_TEST_FAIL);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,alaw,g729",	"local_merge",	0,	"",				AST_TEST_FAIL);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,alaw,g729",	"remote_merge",	0,	"",				AST_TEST_FAIL);

	ast_test_status_update(test, "Testing outgoing expected pass\n");
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,g729,alaw",	"local",		1,	"alaw,g722",			AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,g729,alaw",	"local_first",	1,	"alaw",					AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,g729,alaw",	"local_merge",	1,	"ulaw,alaw,g722",	AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,g729,alaw",	"remote",		1,	"g722,alaw",			AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,g729,alaw",	"remote_first",	1,	"g722",					AST_TEST_PASS);
	RUN_CREATE_JOINT("ulaw,alaw,g722",	"g722,g729,alaw",	"remote_merge",	1,	"g722,alaw,ulaw",	AST_TEST_PASS);
	RUN_CREATE_JOINT("!all",			"g722,g729,alaw",	"remote_merge",	1,	"nothing",		AST_TEST_PASS);

	return rc >= 1 ? AST_TEST_FAIL : AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(low_level);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(low_level);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "res_pjsip_session caps test module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip_session",
);
