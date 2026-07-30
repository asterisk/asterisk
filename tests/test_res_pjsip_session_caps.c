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
#include "asterisk/rtp_engine.h"
#include "asterisk/format_cache.h"

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

static int payload_merge_test_mapping(struct ast_rtp_codecs *codecs,
	int payload, const char *subtype)
{
	if (ast_rtp_codecs_payloads_set_rtpmap_type(
			codecs, NULL, payload, "audio", (char *) subtype, 0)) {
		return -1;
	}
	ast_rtp_codecs_payloads_xover(codecs, codecs, NULL);
	return 0;
}

static int payload_merge_test_has_rx(struct ast_rtp_codecs *codecs,
	int payload, struct ast_format *format)
{
	RAII_VAR(struct ast_rtp_payload_type *, type,
		ast_rtp_codecs_get_payload(codecs, payload), ao2_cleanup);

	if (!format) {
		return !type;
	}
	return type && type->asterisk_format
		&& ast_format_cmp(type->format, format) == AST_FORMAT_CMP_EQUAL;
}

AST_TEST_DEFINE(payload_merge_pending_state)
{
	struct ast_sip_session_media_state *state = NULL;
	struct ast_rtp_codecs active1 = AST_RTP_CODECS_NULL_INIT;
	struct ast_rtp_codecs active2 = AST_RTP_CODECS_NULL_INIT;
	struct ast_rtp_codecs offer1 = AST_RTP_CODECS_NULL_INIT;
	struct ast_rtp_codecs offer2 = AST_RTP_CODECS_NULL_INIT;
	enum ast_test_result_state result = AST_TEST_PASS;
	int active1_initialized = 0;
	int active2_initialized = 0;
	int offer1_initialized = 0;
	int offer2_initialized = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "pending_state";
		info->category = "/res/res_pjsip_session/payload_merge/";
		info->summary = "Test pending media state ownership of RTP payload merge transactions";
		info->description =
			"Verifies aggregated rollback for a shared RTP codecs structure and "
			"commit isolation between separate RTP codecs structures. Also "
			"verifies the pending marker used to detect abandoned offers.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	state = ast_sip_session_media_state_alloc();
	if (!state) {
		ast_test_status_update(test, "Unable to initialize pending media transaction test\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	if (ast_rtp_codecs_payloads_initialize(&active1)) {
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	active1_initialized = 1;
	if (ast_rtp_codecs_payloads_initialize(&active2)) {
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	active2_initialized = 1;
	if (ast_rtp_codecs_payloads_initialize(&offer1)) {
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	offer1_initialized = 1;
	if (ast_rtp_codecs_payloads_initialize(&offer2)) {
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	offer2_initialized = 1;

	if (payload_merge_test_mapping(&active1, 96, "PCMU")
		|| payload_merge_test_mapping(&active2, 98, "G722")
		|| payload_merge_test_mapping(&offer1, 97, "PCMA")
		|| payload_merge_test_mapping(&offer2, 99, "PCMA")) {
		ast_test_status_update(test, "Unable to create dynamic RTP mappings\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}

	/*
	 * Two provisional updates to the same destination must share the original
	 * snapshot. Payload-only rollback at media-state activation must restore
	 * the pre-negotiation state, not the state present between the first and
	 * second merges.
	 */
	if (ast_sip_session_media_state_payloads_merge(
			state, &offer1, &active1, NULL)
		|| ast_sip_session_media_state_payloads_merge(
			state, &offer2, &active1, NULL)) {
		ast_test_status_update(test, "Unable to add aggregated provisional merges\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	if (!ast_sip_session_media_state_payloads_pending(state)) {
		ast_test_status_update(test, "Provisional merges were not marked pending\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	ast_sip_session_media_state_payloads_rollback(state);

	if (ast_sip_session_media_state_payloads_pending(state)) {
		ast_test_status_update(test, "Finalization rollback left a merge pending\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	if (!payload_merge_test_has_rx(&active1, 96, ast_format_ulaw)
		|| !payload_merge_test_has_rx(&active1, 97, NULL)
		|| !payload_merge_test_has_rx(&active1, 99, NULL)) {
		ast_test_status_update(test, "Finalization rollback did not restore the original shared RTP state\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}

	/*
	 * Committing one destination must not commit another. Reset retains the
	 * committed provisional state on active1 and rolls active2 back.
	 */
	if (ast_sip_session_media_state_payloads_merge(
			state, &offer1, &active1, NULL)
		|| ast_sip_session_media_state_payloads_merge(
			state, &offer2, &active2, NULL)) {
		ast_test_status_update(test, "Unable to add isolated provisional merges\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	ast_sip_session_media_state_payloads_commit_one(state, &active1);
	if (!ast_sip_session_media_state_payloads_pending(state)) {
		ast_test_status_update(test, "Commit-one incorrectly cleared another pending merge\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	ast_sip_session_media_state_reset(state);

	if (ast_sip_session_media_state_payloads_pending(state)) {
		ast_test_status_update(test, "Reset did not clear the remaining pending merge\n");
		result = AST_TEST_FAIL;
		goto cleanup;
	}
	if (!payload_merge_test_has_rx(&active1, 97, ast_format_alaw)
		|| !payload_merge_test_has_rx(&active2, 98, ast_format_g722)
		|| !payload_merge_test_has_rx(&active2, 99, NULL)) {
		ast_test_status_update(test, "Commit-one did not isolate pending RTP transactions\n");
		result = AST_TEST_FAIL;
	}

cleanup:
	ast_sip_session_media_state_free(state);
	if (offer2_initialized) {
		ast_rtp_codecs_payloads_destroy(&offer2);
	}
	if (offer1_initialized) {
		ast_rtp_codecs_payloads_destroy(&offer1);
	}
	if (active2_initialized) {
		ast_rtp_codecs_payloads_destroy(&active2);
	}
	if (active1_initialized) {
		ast_rtp_codecs_payloads_destroy(&active1);
	}
	return result;
}

static int load_module(void)
{
	AST_TEST_REGISTER(low_level);
	AST_TEST_REGISTER(payload_merge_pending_state);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(payload_merge_pending_state);
	AST_TEST_UNREGISTER(low_level);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "res_pjsip_session caps test module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip_session",
);
