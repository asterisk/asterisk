/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/network.h"
#include "asterisk/security_events.h"
#include "asterisk/netsock2.h"
#include "asterisk/stasis.h"
#include "asterisk/json.h"
#include "asterisk/astobj2.h"

static const size_t TIMESTAMP_STR_LEN = 32;

/*! \brief Security Topic */
static struct stasis_topic *security_topic;

struct stasis_topic *ast_security_topic(void)
{
	return security_topic;
}

/*! \brief Message type for security events */
STASIS_MESSAGE_TYPE_DEFN(ast_security_event_type);

static void security_stasis_cleanup(void)
{
	ao2_cleanup(security_topic);
	security_topic = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(ast_security_event_type);
}

int ast_security_stasis_init(void)
{
	ast_register_cleanup(security_stasis_cleanup);

	security_topic = stasis_topic_create("ast_security");
	if (!security_topic) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_security_event_type)) {
		return -1;
	}


	return 0;
}

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
		{ AST_EVENT_IE_USING_PASSWORD, SEC_EVT_FIELD(successful_auth, using_password) },
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
		{ AST_EVENT_IE_CHALLENGE, SEC_EVT_FIELD(inval_password, challenge) },
		{ AST_EVENT_IE_RECEIVED_CHALLENGE, SEC_EVT_FIELD(inval_password, received_challenge) },
		{ AST_EVENT_IE_RECEIVED_HASH, SEC_EVT_FIELD(inval_password, received_hash) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_CHAL_SENT] = {
	.name     = "ChallengeSent",
	.version  = AST_SECURITY_EVENT_CHAL_SENT_VERSION,
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
		{ AST_EVENT_IE_CHALLENGE, SEC_EVT_FIELD(chal_sent, challenge) },
		{ AST_EVENT_IE_END, 0 }
	},
	.optional_ies = {
		{ AST_EVENT_IE_MODULE, SEC_EVT_FIELD(common, module) },
		{ AST_EVENT_IE_SESSION_TV, SEC_EVT_FIELD(common, session_tv) },
		{ AST_EVENT_IE_END, 0 }
	},
},

[AST_SECURITY_EVENT_INVAL_TRANSPORT] = {
	.name     = "InvalidTransport",
	.version  = AST_SECURITY_EVENT_INVAL_TRANSPORT_VERSION,
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
		{ AST_EVENT_IE_ATTEMPTED_TRANSPORT, SEC_EVT_FIELD(inval_transport, transport) },
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

static int add_ip_json_object(struct ast_json *json, enum ast_event_ie_type ie_type,
		const struct ast_security_event_ip_addr *addr)
{
	struct ast_json *json_ip;

	json_ip = ast_json_ipaddr(addr->addr, addr->transport);
	if (!json_ip) {
		return -1;
	}

	return ast_json_object_set(json, ast_event_get_ie_type_name(ie_type), json_ip);
}

enum ie_required {
	NOT_REQUIRED,
	REQUIRED
};

static int add_json_object(struct ast_json *json, const struct ast_security_event_common *sec,
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
	case AST_EVENT_IE_RECEIVED_CHALLENGE:
	case AST_EVENT_IE_RECEIVED_HASH:
	case AST_EVENT_IE_ATTEMPTED_TRANSPORT:
	{
		const char *str;
		struct ast_json *json_string;

		str = *((const char **)(((const char *) sec) + ie_type->offset));

		if (req && !str) {
			ast_log(LOG_WARNING, "Required IE '%d' for security event "
					"type '%d' not present\n", ie_type->ie_type,
					sec->event_type);
			res = -1;
			break;
		}

		if (!str) {
			break;
		}

		json_string = ast_json_string_create(str);
		if (!json_string) {
			res = -1;
			break;
		}

		res = ast_json_object_set(json, ast_event_get_ie_type_name(ie_type->ie_type), json_string);
		break;
	}
	case AST_EVENT_IE_EVENT_VERSION:
	case AST_EVENT_IE_USING_PASSWORD:
	{
		struct ast_json *json_string;
		uint32_t val;
		val = *((const uint32_t *)(((const char *) sec) + ie_type->offset));

		json_string = ast_json_stringf("%d", val);
		if (!json_string) {
			res = -1;
			break;
		}

		res = ast_json_object_set(json, ast_event_get_ie_type_name(ie_type->ie_type), json_string);
		break;
	}
	case AST_EVENT_IE_LOCAL_ADDR:
	case AST_EVENT_IE_REMOTE_ADDR:
	case AST_EVENT_IE_EXPECTED_ADDR:
	{
		const struct ast_security_event_ip_addr *addr;

		addr = (const struct ast_security_event_ip_addr *)(((const char *) sec) + ie_type->offset);

		if (req && !addr->addr) {
			ast_log(LOG_WARNING, "Required IE '%d' for security event "
					"type '%d' not present\n", ie_type->ie_type,
					sec->event_type);
			res = -1;
		}

		if (addr->addr) {
			res = add_ip_json_object(json, ie_type->ie_type, addr);
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
			struct ast_json *json_tval = ast_json_timeval(*tval, NULL);
			if (!json_tval) {
				res = -1;
				break;
			}
			res = ast_json_object_set(json, ast_event_get_ie_type_name(ie_type->ie_type), json_tval);
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

static struct ast_json *alloc_security_event_json_object(const struct ast_security_event_common *sec)
{
	struct timeval tv = ast_tvnow();
	const char *severity_str;
	struct ast_json *json_temp;
	RAII_VAR(struct ast_json *, json_object, ast_json_object_create(), ast_json_unref);

	if (!json_object) {
		return NULL;
	}

	/* NOTE: Every time ast_json_object_set is used, json_temp becomes a stale pointer since the reference is taken.
	 *       This is true even if ast_json_object_set fails.
	 */

	/* AST_EVENT_IE_SECURITY_EVENT */
	json_temp = ast_json_integer_create(sec->event_type);
	if (!json_temp || ast_json_object_set(json_object, ast_event_get_ie_type_name(AST_EVENT_IE_SECURITY_EVENT), json_temp)) {
		return NULL;
	}

	/* AST_EVENT_IE_EVENT_VERSION */
	json_temp = ast_json_stringf("%d", sec->version);
	if (!json_temp || ast_json_object_set(json_object, ast_event_get_ie_type_name(AST_EVENT_IE_EVENT_VERSION), json_temp)) {
		return NULL;
	}

	/* AST_EVENT_IE_EVENT_TV */
	json_temp  = ast_json_timeval(tv, NULL);
	if (!json_temp || ast_json_object_set(json_object, ast_event_get_ie_type_name(AST_EVENT_IE_EVENT_TV), json_temp)) {
		return NULL;
	}

	/* AST_EVENT_IE_SERVICE */
	json_temp = ast_json_string_create(sec->service);
	if (!json_temp || ast_json_object_set(json_object, ast_event_get_ie_type_name(AST_EVENT_IE_SERVICE), json_temp)) {
		return NULL;
	}

	/* AST_EVENT_IE_SEVERITY */
	severity_str = S_OR(
		ast_security_event_severity_get_name(sec_events[sec->event_type].severity),
		"Unknown"
	);

	json_temp = ast_json_string_create(severity_str);
	if (!json_temp || ast_json_object_set(json_object, ast_event_get_ie_type_name(AST_EVENT_IE_SEVERITY), json_temp)) {
		return NULL;
	}

	return ast_json_ref(json_object);
}

static int handle_security_event(const struct ast_security_event_common *sec)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json_payload *, json_payload, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);

	const struct ast_security_event_ie_type *ies;
	unsigned int i;

	json_object = alloc_security_event_json_object(sec);

	if (!json_object) {
		return -1;
	}

	for (ies = ast_security_event_get_required_ies(sec->event_type), i = 0;
			ies[i].ie_type != AST_EVENT_IE_END;
			i++) {
		if (add_json_object(json_object, sec, ies + i, REQUIRED)) {
			goto return_error;
		}
	}

	for (ies = ast_security_event_get_optional_ies(sec->event_type), i = 0;
			ies[i].ie_type != AST_EVENT_IE_END;
			i++) {
		if (add_json_object(json_object, sec, ies + i, NOT_REQUIRED)) {
			goto return_error;
		}
	}

	/* The json blob is ready.  Throw it in the payload and send it out over stasis. */
	if (!(json_payload = ast_json_payload_create(json_object))) {
		goto return_error;
	}

	msg = stasis_message_create(ast_security_event_type(), json_payload);

	if (!msg) {
		goto return_error;
	}

	stasis_publish(ast_security_topic(), msg);

	return 0;

return_error:
	return -1;
}

int ast_security_event_report(const struct ast_security_event_common *sec)
{
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

	if (handle_security_event(sec)) {
		ast_log(LOG_ERROR, "Failed to issue security event of type %s.\n",
				ast_security_event_get_name(sec->event_type));
	}

	return 0;
}


