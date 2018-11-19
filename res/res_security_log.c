/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Security Event Logging
 *
 * \todo Make informational security events optional
 * \todo Escape quotes in string payload IE contents
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"
#include "asterisk/security_events.h"
#include "asterisk/stasis.h"
#include "asterisk/json.h"

static const char LOG_SECURITY_NAME[] = "SECURITY";

static int LOG_SECURITY;

static struct stasis_subscription *security_stasis_sub;

AST_THREADSTORAGE(security_event_buf);
static const size_t SECURITY_EVENT_BUF_INIT_LEN = 256;

enum ie_required {
	NOT_REQUIRED,
	REQUIRED
};

static void append_json_single(struct ast_str **str, struct ast_json *json,
		const enum ast_event_ie_type ie_type, enum ie_required required)
{
	const char *ie_type_key = ast_event_get_ie_type_name(ie_type);

	struct ast_json *json_string;

	json_string = ast_json_object_get(json, ie_type_key);

	if (!required && !json_string) {
		/* Optional IE isn't present. Ignore. */
		return;
	}

	/* At this point, it _better_ be there! */
	ast_assert(json_string != NULL);

	ast_str_append(str, 0, ",%s=\"%s\"",
			ie_type_key,
			ast_json_string_get(json_string));
}

static void append_json(struct ast_str **str, struct ast_json *json,
		const struct ast_security_event_ie_type *ies, enum ie_required required)
{
	unsigned int i;

	for (i = 0; ies[i].ie_type != AST_EVENT_IE_END; i++) {
		append_json_single(str, json, ies[i].ie_type, required);
	}
}

static void security_event_stasis_cb(struct ast_json *json)
{
	struct ast_str *str;
	struct ast_json *event_type_json;
	enum ast_security_event_type event_type;

	event_type_json = ast_json_object_get(json, "SecurityEvent");
	event_type = ast_json_integer_get(event_type_json);

	ast_assert((unsigned int)event_type < AST_SECURITY_EVENT_NUM_TYPES);

	if (!(str = ast_str_thread_get(&security_event_buf,
			SECURITY_EVENT_BUF_INIT_LEN))) {
		return;
	}

	ast_str_set(&str, 0, "SecurityEvent=\"%s\"",
			ast_security_event_get_name(event_type));

	append_json(&str, json,
			ast_security_event_get_required_ies(event_type), REQUIRED);
	append_json(&str, json,
			ast_security_event_get_optional_ies(event_type), NOT_REQUIRED);

	ast_log_dynamic_level(LOG_SECURITY, "%s\n", ast_str_buffer(str));
}

static void security_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_json_payload *payload = stasis_message_data(message);

	if (stasis_message_type(message) != ast_security_event_type()) {
		return;
	}

	if (!payload) {
		return;
	}

	security_event_stasis_cb(payload->json);
}

static int load_module(void)
{
	if ((LOG_SECURITY = ast_logger_register_level(LOG_SECURITY_NAME)) == -1) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(security_stasis_sub = stasis_subscribe(ast_security_topic(), security_stasis_cb, NULL))) {
		ast_logger_unregister_level(LOG_SECURITY_NAME);
		LOG_SECURITY = -1;
		return AST_MODULE_LOAD_DECLINE;
	}
	stasis_subscription_accept_message_type(security_stasis_sub, ast_security_event_type());
	stasis_subscription_set_filter(security_stasis_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	ast_verb(3, "Security Logging Enabled\n");

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	if (security_stasis_sub) {
		security_stasis_sub = stasis_unsubscribe_and_join(security_stasis_sub);
	}

	ast_logger_unregister_level(LOG_SECURITY_NAME);

	ast_verb(3, "Security Logging Disabled\n");

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Security Event Logging");
