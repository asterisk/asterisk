/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
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

#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"

#define AST_API_MODULE
#include "stir_shaken.h"

static struct ast_sorcery *sorcery;

struct ast_sorcery *ss_sorcery(void)
{
	return sorcery;
}

static const char *rfc9410_response_map[] = {
	[AST_STIR_SHAKEN_VS_RFC9410_NOT_SET] = "not_set",
	[AST_STIR_SHAKEN_VS_RFC9410_YES] = "yes",
	[AST_STIR_SHAKEN_VS_RFC9410_NO] = "no",
};

enum ast_stir_shaken_use_rfc9410_responses
	ast_stir_shaken_str_to_use_rfc9410_responses(const char *use_rfc9410_str)
{
	if (!strcasecmp(use_rfc9410_str, rfc9410_response_map[AST_STIR_SHAKEN_VS_RFC9410_NOT_SET])) {
		return AST_STIR_SHAKEN_VS_RFC9410_NOT_SET;
	} else if (ast_true(use_rfc9410_str)) {
		return AST_STIR_SHAKEN_VS_RFC9410_YES;
	} else if (ast_false(use_rfc9410_str)) {
		return AST_STIR_SHAKEN_VS_RFC9410_NO;
	}
	ast_log(LOG_WARNING, "Unknown rfc9410 response value '%s'\n", use_rfc9410_str);
	return AST_STIR_SHAKEN_VS_RFC9410_UNKNOWN;
}

const char *ast_stir_shaken_use_rfc9410_responses_to_str(
	enum ast_stir_shaken_use_rfc9410_responses use_rfc9410)
{
	return ARRAY_IN_BOUNDS(use_rfc9410, rfc9410_response_map) ?
		rfc9410_response_map[use_rfc9410] : NULL;
}

static const char *failure_action_map[] = {
	[AST_STIR_SHAKEN_VS_FAILURE_NOT_SET] = "not_set",
	[AST_STIR_SHAKEN_VS_FAILURE_CONTINUE] = "continue",
	[AST_STIR_SHAKEN_VS_FAILURE_REJECT_REQUEST] = "reject_request",
	[AST_STIR_SHAKEN_VS_FAILURE_CONTINUE_RETURN_REASON] = "continue_return_reason"
};

enum ast_stir_shaken_failure_action ast_stir_shaken_str_to_failure_action(
	const char *action_str)
{
	if (!strcasecmp(action_str, failure_action_map[AST_STIR_SHAKEN_VS_FAILURE_NOT_SET])) {
		return AST_STIR_SHAKEN_VS_FAILURE_NOT_SET;
	} else if (!strcasecmp(action_str, failure_action_map[AST_STIR_SHAKEN_VS_FAILURE_CONTINUE])) {
		return AST_STIR_SHAKEN_VS_FAILURE_CONTINUE;
	} else if (!strcasecmp(action_str, failure_action_map[AST_STIR_SHAKEN_VS_FAILURE_REJECT_REQUEST])) {
		return AST_STIR_SHAKEN_VS_FAILURE_REJECT_REQUEST;
	} else if (!strcasecmp(action_str, failure_action_map[AST_STIR_SHAKEN_VS_FAILURE_CONTINUE_RETURN_REASON])) {
		return AST_STIR_SHAKEN_VS_FAILURE_CONTINUE_RETURN_REASON;
	}
	ast_log(LOG_WARNING, "Unknown failure action value '%s'\n", action_str);
	return AST_STIR_SHAKEN_VS_FAILURE_UNKNOWN;
}

const char *ast_stir_shaken_failure_action_to_str(
	enum ast_stir_shaken_failure_action action)
{
	return ARRAY_IN_BOUNDS(action, failure_action_map) ?
		failure_action_map[action] : NULL;
}

static const char *attest_level_map[] = {
	[AST_STIR_SHAKEN_ATTEST_LEVEL_A] = "A",
	[AST_STIR_SHAKEN_ATTEST_LEVEL_B] = "B",
	[AST_STIR_SHAKEN_ATTEST_LEVEL_C] = "C",
};

enum ast_stir_shaken_attest_level ast_stir_shaken_str_to_attest_level(
	const char *level_str)
{
	if (!strcasecmp(level_str, attest_level_map[AST_STIR_SHAKEN_ATTEST_LEVEL_A])) {
		return AST_STIR_SHAKEN_ATTEST_LEVEL_A;
	} else if (!strcasecmp(level_str, attest_level_map[AST_STIR_SHAKEN_ATTEST_LEVEL_B])) {
		return AST_STIR_SHAKEN_ATTEST_LEVEL_B;
	} else if (!strcasecmp(level_str, attest_level_map[AST_STIR_SHAKEN_ATTEST_LEVEL_C])) {
		return AST_STIR_SHAKEN_ATTEST_LEVEL_C;
	}
	ast_log(LOG_WARNING, "Unknown attest level value '%s'\n", level_str);
	return AST_STIR_SHAKEN_ATTEST_LEVEL_UNKNOWN;
}

const char *ast_stir_shaken_attest_level_to_str(
	enum ast_stir_shaken_attest_level level)
{
	return ARRAY_IN_BOUNDS(level, attest_level_map) ?
		attest_level_map[level] : NULL;
}

static const char *send_mky_map[] = {
	[AST_STIR_SHAKEN_AS_SEND_MKY_NOT_SET] = "not_set",
	[AST_STIR_SHAKEN_AS_SEND_MKY_YES] = "yes",
	[AST_STIR_SHAKEN_AS_SEND_MKY_NO] = "no",
};

enum ast_stir_shaken_send_mky
	ast_stir_shaken_str_to_send_mky(const char *send_mky_str)
{
	if (!strcasecmp(send_mky_str, send_mky_map[AST_STIR_SHAKEN_AS_SEND_MKY_NOT_SET])) {
		return AST_STIR_SHAKEN_AS_SEND_MKY_NOT_SET;
	} else if (ast_true(send_mky_str)) {
		return AST_STIR_SHAKEN_AS_SEND_MKY_YES;
	} else if (ast_false(send_mky_str)) {
		return AST_STIR_SHAKEN_AS_SEND_MKY_NO;
	}
	ast_log(LOG_WARNING, "Unknown send_mky value '%s'\n", send_mky_str);
	return AST_STIR_SHAKEN_AS_SEND_MKY_UNKNOWN;
}

const char *ast_stir_shaken_send_mky_to_str(
	enum ast_stir_shaken_send_mky send_mky)
{
	return ARRAY_IN_BOUNDS(send_mky, send_mky_map) ?
		send_mky_map[send_mky] : NULL;
}


static const char *check_tn_cert_public_url_map[] = {
	[AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NOT_SET] = "not_set",
	[AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_YES] = "yes",
	[AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NO] = "no",
};

enum ast_stir_shaken_check_tn_cert_public_url
	ast_stir_shaken_str_to_check_tn_cert_public_url(const char *check_tn_cert_public_url_str)
{
	if (!strcasecmp(check_tn_cert_public_url_str, check_tn_cert_public_url_map[AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NOT_SET])) {
		return AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NOT_SET;
	} else if (ast_true(check_tn_cert_public_url_str)) {
		return AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_YES;
	} else if (ast_false(check_tn_cert_public_url_str)) {
		return AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NO;
	}
	ast_log(LOG_WARNING, "Unknown send_mky value '%s'\n", check_tn_cert_public_url_str);
	return AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_UNKNOWN;
}

const char *ast_stir_shaken_check_tn_cert_public_url_to_str(
	enum ast_stir_shaken_check_tn_cert_public_url check_tn_cert_public_url)
{
	return ARRAY_IN_BOUNDS(check_tn_cert_public_url, check_tn_cert_public_url_map) ?
		send_mky_map[check_tn_cert_public_url] : NULL;
}

static const char *behavior_map[] = {
	[AST_STIR_SHAKEN_BEHAVIOR_OFF] = "off",
	[AST_STIR_SHAKEN_BEHAVIOR_ATTEST] = "attest",
	[AST_STIR_SHAKEN_BEHAVIOR_VERIFY] = "verify",
	[AST_STIR_SHAKEN_BEHAVIOR_ON] = "on",
};

enum ast_stir_shaken_behavior ast_stir_shaken_str_to_behavior(
	const char *behavior_str)
{
	if (ast_false(behavior_str)) {
		return AST_STIR_SHAKEN_BEHAVIOR_OFF;
	} else if (!strcasecmp(behavior_str, behavior_map[AST_STIR_SHAKEN_BEHAVIOR_ATTEST])) {
		return AST_STIR_SHAKEN_BEHAVIOR_ATTEST;
	} else if (!strcasecmp(behavior_str, behavior_map[AST_STIR_SHAKEN_BEHAVIOR_VERIFY])) {
		return AST_STIR_SHAKEN_BEHAVIOR_VERIFY;
	} else if (ast_true(behavior_str)) {
		return AST_STIR_SHAKEN_BEHAVIOR_ON;
	} else if (!strcasecmp(behavior_str, "both")) {
		return AST_STIR_SHAKEN_BEHAVIOR_ON;
	}
	ast_log(LOG_WARNING, "Unknown behavior value '%s'.  Defaulting to 'off'\n", behavior_str);
	return AST_STIR_SHAKEN_BEHAVIOR_OFF;
}

const char *ast_stir_shaken_behavior_to_str(
	enum ast_stir_shaken_behavior behavior)
{
	return ARRAY_IN_BOUNDS(behavior, behavior_map) ?
		behavior_map[behavior] : NULL;
}

int ss_config_reload(void)
{
	if (ss_vs_reload()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_as_reload()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_tn_reload()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_profile_reload()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

int ss_config_unload(void)
{
	ss_profile_unload();
	ss_tn_unload();
	ss_as_unload();
	ss_vs_unload();

	ast_sorcery_unref(sorcery);
	sorcery = NULL;

	return 0;
}

int ss_config_load(void)
{
	if (!(sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "stir/shaken - failed to open sorcery\n");
		ss_config_unload();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_vs_load()) {
		ss_config_unload();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_as_load()) {
		ss_config_unload();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_tn_load()) {
		ss_config_unload();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_profile_load()) {
		ss_config_unload();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

