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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/event.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"
#include "asterisk/security_events.h"

static const char LOG_SECURITY_NAME[] = "SECURITY";

static int LOG_SECURITY;

static struct ast_event_sub *security_event_sub;

AST_THREADSTORAGE(security_event_buf);
static const size_t SECURITY_EVENT_BUF_INIT_LEN = 256;

enum ie_required {
	NOT_REQUIRED,
	REQUIRED
};

static int ie_is_present(const struct ast_event *event,
		const enum ast_event_ie_type ie_type)
{
	return (ast_event_get_ie_raw(event, ie_type) != NULL);
}

static void append_ie(struct ast_str **str, const struct ast_event *event,
		const enum ast_event_ie_type ie_type, enum ie_required required)
{
	if (!required && !ie_is_present(event, ie_type)) {
		/* Optional IE isn't present.  Ignore. */
		return;
	}

	/* At this point, it _better_ be there! */
	ast_assert(ie_is_present(event, ie_type));

	switch (ast_event_get_ie_pltype(ie_type)) {
	case AST_EVENT_IE_PLTYPE_UINT:
		ast_str_append(str, 0, ",%s=\"%u\"",
				ast_event_get_ie_type_name(ie_type),
				ast_event_get_ie_uint(event, ie_type));
		break;
	case AST_EVENT_IE_PLTYPE_STR:
		ast_str_append(str, 0, ",%s=\"%s\"",
				ast_event_get_ie_type_name(ie_type),
				ast_event_get_ie_str(event, ie_type));
		break;
	case AST_EVENT_IE_PLTYPE_BITFLAGS:
		ast_str_append(str, 0, ",%s=\"%u\"",
				ast_event_get_ie_type_name(ie_type),
				ast_event_get_ie_bitflags(event, ie_type));
		break;
	case AST_EVENT_IE_PLTYPE_UNKNOWN:
	case AST_EVENT_IE_PLTYPE_EXISTS:
	case AST_EVENT_IE_PLTYPE_RAW:
		ast_log(LOG_WARNING, "Unexpected payload type for IE '%s'\n",
				ast_event_get_ie_type_name(ie_type));
		break;
	}
}

static void append_ies(struct ast_str **str, const struct ast_event *event,
		const struct ast_security_event_ie_type *ies, enum ie_required required)
{
	unsigned int i;

	for (i = 0; ies[i].ie_type != AST_EVENT_IE_END; i++) {
		append_ie(str, event, ies[i].ie_type, required);
	}
}

static void security_event_cb(const struct ast_event *event, void *data)
{
	struct ast_str *str;
	enum ast_security_event_type event_type;

	if (!(str = ast_str_thread_get(&security_event_buf,
			SECURITY_EVENT_BUF_INIT_LEN))) {
		return;
	}

	/* Note that the event type is guaranteed to be valid here. */
	event_type = ast_event_get_ie_uint(event, AST_EVENT_IE_SECURITY_EVENT);
	ast_assert(event_type >= 0 && event_type < AST_SECURITY_EVENT_NUM_TYPES);

	ast_str_set(&str, 0, "%s=\"%s\"",
			ast_event_get_ie_type_name(AST_EVENT_IE_SECURITY_EVENT),
			ast_security_event_get_name(event_type));

	append_ies(&str, event,
			ast_security_event_get_required_ies(event_type), REQUIRED);
	append_ies(&str, event,
			ast_security_event_get_optional_ies(event_type), NOT_REQUIRED);

	ast_log_dynamic_level(LOG_SECURITY, "%s\n", ast_str_buffer(str));
}

static int load_module(void)
{
	if ((LOG_SECURITY = ast_logger_register_level(LOG_SECURITY_NAME)) == -1) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(security_event_sub = ast_event_subscribe(AST_EVENT_SECURITY,
			security_event_cb, "Security Event Logger",
			NULL, AST_EVENT_IE_END))) {
		ast_logger_unregister_level(LOG_SECURITY_NAME);
		LOG_SECURITY = -1;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_verb(3, "Security Logging Enabled\n");

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	if (security_event_sub) {
		security_event_sub = ast_event_unsubscribe(security_event_sub);
	}

	ast_verb(3, "Security Logging Disabled\n");

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Security Event Logging");
