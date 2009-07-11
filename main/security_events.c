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
 * \brief Security Event Reporting Helpers
 *
 * \author Russell Bryant <russell@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/network.h"
#include "asterisk/security_events.h"

static const size_t TIMESTAMP_STR_LEN = 32;

static const struct {
	const char *name;
	uint32_t version;
	enum ast_security_event_severity severity;
#define MAX_SECURITY_IES 12
	struct ast_security_event_ie_type required_ies[MAX_SECURITY_IES];
	struct ast_security_event_ie_type optional_ies[MAX_SECURITY_IES];
#undef MAX_SECURITY_IES
} sec_events[AST_SECURITY_EVENT_NUM_TYPES] = {

#define SEC_EVT_FIELD(e, field) (offsetof(struct ast_security_event_##e, field))

[AST_SECURITY_EVENT_FAILED_ACL] = {
	.name     = "FailedACL",
	.version  = AST_SECURITY_EVENT_FAILED_ACL_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_ACL_NAME, SEC_EVT_FIELD(failed_acl, acl_name) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_INVAL_ACCT_ID] = {
	.name     = "InvalidAccountID",
	.version  = AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_SESSION_LIMIT] = {
	.name     = "SessionLimit",
	.version  = AST_SECURITY_EVENT_SESSION_LIMIT_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_MEM_LIMIT] = {
	.name     = "MemoryLimit",
	.version  = AST_SECURITY_EVENT_MEM_LIMIT_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_LOAD_AVG] = {
	.name     = "LoadAverageLimit",
	.version  = AST_SECURITY_EVENT_LOAD_AVG_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_REQ_NO_SUPPORT] = {
	.name     = "RequestNotSupported",
	.version  = AST_SECURITY_EVENT_REQ_NO_SUPPORT_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_REQUEST_TYPE, SEC_EVT_FIELD(req_no_support, request_type) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_REQ_NOT_ALLOWED] = {
	.name     = "RequestNotAllowed",
	.version  = AST_SECURITY_EVENT_REQ_NOT_ALLOWED_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_REQUEST_TYPE, SEC_EVT_FIELD(req_not_allowed, request_type) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_REQUEST_PARAMS, SEC_EVT_FIELD(req_not_allowed, request_params) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED] = {
	.name     = "AuthMethodNotAllowed",
	.version  = AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_AUTH_METHOD, SEC_EVT_FIELD(auth_method_not_allowed, auth_method) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_REQ_BAD_FORMAT] = {
	.name     = "RequestBadFormat",
	.version  = AST_SECURITY_EVENT_REQ_BAD_FORMAT_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_REQUEST_TYPE, SEC_EVT_FIELD(req_bad_format, request_type) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_REQUEST_PARAMS, SEC_EVT_FIELD(req_bad_format, request_params) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_SUCCESSFUL_AUTH] = {
	.name     = "SuccessfulAuth",
	.version  = AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_INFO,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_UNEXPECTED_ADDR] = {
	.name     = "UnexpectedAddress",
	.version  = AST_SECURITY_EVENT_UNEXPECTED_ADDR_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_EXPECTED_ADDR, SEC_EVT_FIELD(unexpected_addr, expected_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_CHAL_RESP_FAILED] = {
	.name     = "ChallengeResponseFailed",
	.version  = AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_CHALLENGE, SEC_EVT_FIELD(chal_resp_failed, challenge) },
		{ AST_EVENT_IE_RESPONSE, SEC_EVT_FIELD(chal_resp_failed, response) },
		{ AST_EVENT_IE_EXPECTED_RESPONSE, SEC_EVT_FIELD(chal_resp_failed, expected_response) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_INVAL_PASSWORD] = {
	.name     = "InvalidPassword",
	.version  = AST_SECURITY_EVENT_INVAL_PASSWORD_VERSION,
	.severity = AST_SECURITY_EVENT_SEVERITY_ERROR,
	.required_ies = {
		{ AST_EVENT_IE_EVENT_TV, 0 },
		{ AST_EVENT_IE_SEVERITY, 0 },
		{ AST_EVENT_IE_SERVICE, SEC_EVT_FIELD(common, service) },
		{ AST_EVENT_IE_EVENT_VERSION, SEC_EVT_FIELD(common, version) },
		{ AST_EVENT_IE_ACCOUNT_ID, SEC_EVT_FIELD(common, account_id) },
		{ AST_EVENT_IE_SESSION_ID, SEC_EVT_FIELD(common, session_id) },
		{ AST_EVENT_IE_LOCAL_ADDR, SEC_EVT_FIELD(common, local_addr) },
		{ AST_EVENT_IE_REMOTE_ADDR, SEC_EVT_FIELD(common, remote_addr) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

#undef SEC_EVT_FIELD

};

static const struct {
	enum ast_security_event_severity severity;
	const char *str;
} severities[] = {
	{ AST_SECURITY_EVENT_SEVERITY_INFO,  "Informational" },
	{ AST_SECURITY_EVENT_SEVERITY_ERROR, "Error" },
};

const char *ast_security_event_severity_get_name(
		const enum ast_security_event_severity severity)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(severities); i++) {
		if (severities[i].severity == severity) {
			return severities[i].str;
		}
	}

	return NULL;
}

static int check_event_type(const enum ast_security_event_type event_type)
{
	if (event_type < 0 || event_type >= AST_SECURITY_EVENT_NUM_TYPES) {
		ast_log(LOG_ERROR, "Invalid security event type %u\n", event_type);
		return -1;
	}

	return 0;
}

const char *ast_security_event_get_name(const enum ast_security_event_type event_type)
{
	if (check_event_type(event_type)) {
		return NULL;
	}

	return sec_events[event_type].name;
}

const struct ast_security_event_ie_type *ast_security_event_get_required_ies(
		const enum ast_security_event_type event_type)
{
	if (check_event_type(event_type)) {
		return NULL;
	}

	return sec_events[event_type].required_ies;
}

const struct ast_security_event_ie_type *ast_security_event_get_optional_ies(
		const enum ast_security_event_type event_type)
{
	if (check_event_type(event_type)) {
		return NULL;
	}

	return sec_events[event_type].optional_ies;
}

static void encode_timestamp(struct ast_str **str, const struct timeval *tv)
{
	ast_str_set(str, 0, "%u-%u",
			(unsigned int) tv->tv_sec,
			(unsigned int) tv->tv_usec);
}

static struct ast_event *alloc_event(const struct ast_security_event_common *sec)
{
	struct ast_str *str = ast_str_alloca(TIMESTAMP_STR_LEN);
	struct timeval tv = ast_tvnow();
	const char *severity_str;

	if (check_event_type(sec->event_type)) {
		return NULL;
	}

	encode_timestamp(&str, &tv);

	severity_str = S_OR(
		ast_security_event_severity_get_name(sec_events[sec->event_type].severity),
		"Unknown"
	);

	return ast_event_new(AST_EVENT_SECURITY,
		AST_EVENT_IE_SECURITY_EVENT, AST_EVENT_IE_PLTYPE_UINT, sec->event_type,
		AST_EVENT_IE_EVENT_VERSION, AST_EVENT_IE_PLTYPE_UINT, sec->version,
		AST_EVENT_IE_EVENT_TV, AST_EVENT_IE_PLTYPE_STR, str->str,
		AST_EVENT_IE_SERVICE, AST_EVENT_IE_PLTYPE_STR, sec->service,
		AST_EVENT_IE_SEVERITY, AST_EVENT_IE_PLTYPE_STR, severity_str,
		AST_EVENT_IE_END);
}

static int add_timeval_ie(struct ast_event **event, enum ast_event_ie_type ie_type,
		const struct timeval *tv)
{
	struct ast_str *str = ast_str_alloca(TIMESTAMP_STR_LEN);

	encode_timestamp(&str, tv);

	return ast_event_append_ie_str(event, ie_type, ast_str_buffer(str));
}

static int add_ipv4_ie(struct ast_event **event, enum ast_event_ie_type ie_type,
		const struct ast_security_event_ipv4_addr *addr)
{
	struct ast_str *str = ast_str_alloca(64);

	ast_str_set(&str, 0, "IPV4/");

	switch (addr->transport) {
	case AST_SECURITY_EVENT_TRANSPORT_UDP:
		ast_str_append(&str, 0, "UDP/");
		break;
	case AST_SECURITY_EVENT_TRANSPORT_TCP:
		ast_str_append(&str, 0, "TCP/");
		break;
	case AST_SECURITY_EVENT_TRANSPORT_TLS:
		ast_str_append(&str, 0, "TLS/");
		break;
	}

	ast_str_append(&str, 0, "%s/%hu",
			ast_inet_ntoa(addr->sin->sin_addr),
			ntohs(addr->sin->sin_port));

	return ast_event_append_ie_str(event, ie_type, ast_str_buffer(str));
}

enum ie_required {
	NOT_REQUIRED,
	REQUIRED
};

static int add_ie(struct ast_event **event, const struct ast_security_event_common *sec,
		const struct ast_security_event_ie_type *ie_type, enum ie_required req)
{
	int res = 0;

	switch (ie_type->ie_type) {
	case AST_EVENT_IE_SERVICE:
	case AST_EVENT_IE_ACCOUNT_ID:
	case AST_EVENT_IE_SESSION_ID:
	case AST_EVENT_IE_MODULE:
	case AST_EVENT_IE_ACL_NAME:
	case AST_EVENT_IE_REQUEST_TYPE:
	case AST_EVENT_IE_REQUEST_PARAMS:
	case AST_EVENT_IE_AUTH_METHOD:
	case AST_EVENT_IE_CHALLENGE:
	case AST_EVENT_IE_RESPONSE:
	case AST_EVENT_IE_EXPECTED_RESPONSE:
	{
		const char *str;

		str = *((const char **)(((const char *) sec) + ie_type->offset));

		if (req && !str) {
			ast_log(LOG_WARNING, "Required IE '%d' for security event "
					"type '%d' not present\n", ie_type->ie_type,
					sec->event_type);
			res = -1;
		}

		if (str) {
			res = ast_event_append_ie_str(event, ie_type->ie_type, str);
		}

		break;
	}
	case AST_EVENT_IE_EVENT_VERSION:
	{
		uint32_t val;
		val = *((const uint32_t *)(((const char *) sec) + ie_type->offset));
		res = ast_event_append_ie_uint(event, ie_type->ie_type, val);
		break;
	}
	case AST_EVENT_IE_LOCAL_ADDR:
	case AST_EVENT_IE_REMOTE_ADDR:
	case AST_EVENT_IE_EXPECTED_ADDR:
	{
		const struct ast_security_event_ipv4_addr *addr;

		addr = (const struct ast_security_event_ipv4_addr *)(((const char *) sec) + ie_type->offset);

		if (req && !addr->sin) {
			ast_log(LOG_WARNING, "Required IE '%d' for security event "
					"type '%d' not present\n", ie_type->ie_type,
					sec->event_type);
			res = -1;
		}

		if (addr->sin) {
			res = add_ipv4_ie(event, ie_type->ie_type, addr);
		}
		break;
	}
	case AST_EVENT_IE_SESSION_TV:
	{
		const struct timeval *tval;

		tval = *((const struct timeval **)(((const char *) sec) + ie_type->offset));

		if (req && !tval) {
			ast_log(LOG_WARNING, "Required IE '%d' for security event "
					"type '%d' not present\n", ie_type->ie_type,
					sec->event_type);
			res = -1;
		}

		if (tval) {
			add_timeval_ie(event, ie_type->ie_type, tval);
		}

		break;
	}
	case AST_EVENT_IE_EVENT_TV:
	case AST_EVENT_IE_SEVERITY:
		/* Added automatically, nothing to do here. */
		break;
	default:
		ast_log(LOG_WARNING, "Unhandled IE type '%d', this security event "
				"will be missing data.\n", ie_type->ie_type);
		break;
	}

	return res;
}

static int handle_security_event(const struct ast_security_event_common *sec)
{
	struct ast_event *event;
	const struct ast_security_event_ie_type *ies;
	unsigned int i;

	if (!(event = alloc_event(sec))) {
		return -1;
	}

	for (ies = ast_security_event_get_required_ies(sec->event_type), i = 0;
			ies[i].ie_type != AST_EVENT_IE_END;
			i++) {
		if (add_ie(&event, sec, ies + i, REQUIRED)) {
			goto return_error;
		}
	}

	for (ies = ast_security_event_get_optional_ies(sec->event_type), i = 0;
			ies[i].ie_type != AST_EVENT_IE_END;
			i++) {
		if (add_ie(&event, sec, ies + i, NOT_REQUIRED)) {
			goto return_error;
		}
	}


	if (ast_event_queue(event)) {
		goto return_error;
	}

	return 0;

return_error:
	if (event) {
		ast_event_destroy(event);
	}

	return -1;
}

int ast_security_event_report(const struct ast_security_event_common *sec)
{
	int res;

	if (sec->event_type < 0 || sec->event_type >= AST_SECURITY_EVENT_NUM_TYPES) {
		ast_log(LOG_ERROR, "Invalid security event type\n");
		return -1;
	}

	if (!sec_events[sec->event_type].name) {
		ast_log(LOG_WARNING, "Security event type %u not handled\n",
				sec->event_type);
		return -1;
	}

	if (sec->version != sec_events[sec->event_type].version) {
		ast_log(LOG_WARNING, "Security event %u version mismatch\n",
				sec->event_type);
		return -1;
	}

	res = handle_security_event(sec);

	return res;
}


