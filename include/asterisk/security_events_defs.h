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
 * \brief Security Event Reporting Data Structures
 *
 * \author Russell Bryant <russell@digium.com>
 */

#ifndef __AST_SECURITY_EVENTS_DEFS_H__
#define __AST_SECURITY_EVENTS_DEFS_H__

#include "asterisk/network.h"
#include "asterisk/netsock2.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief Security event types
 */
enum ast_security_event_type {
	/*!
	 * \brief Failed ACL
	 *
	 * This security event should be generated when an incoming request
	 * was made, but was denied due to configured IP address access control
	 * lists.
	 */
	AST_SECURITY_EVENT_FAILED_ACL,
	/*!
	 * \brief Invalid Account ID
	 *
	 * This event is used when an invalid account identifier is supplied
	 * during authentication.  For example, if an invalid username is given,
	 * this event should be used.
	 */
	AST_SECURITY_EVENT_INVAL_ACCT_ID,
	/*!
	 * \brief Session limit reached
	 *
	 * A request has been denied because a configured session limit has been
	 * reached, such as a call limit.
	 */
	AST_SECURITY_EVENT_SESSION_LIMIT,
	/*!
	 * \brief Memory limit reached
	 *
	 * A request has been denied because a configured memory limit has been
	 * reached.
	 */
	AST_SECURITY_EVENT_MEM_LIMIT,
	/*!
	 * \brief Load Average limit reached
	 *
	 * A request has been denied because a configured load average limit has been
	 * reached.
	 */
	AST_SECURITY_EVENT_LOAD_AVG,
	/*!
	 * \brief A request was made that we understand, but do not support
	 */
	AST_SECURITY_EVENT_REQ_NO_SUPPORT,
	/*!
	 * \brief A request was made that is not allowed
	 */
	AST_SECURITY_EVENT_REQ_NOT_ALLOWED,
	/*!
	 * \brief The attempted authentication method is not allowed
	 */
	AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED,
	/*!
	 * \brief Request received with bad formatting
	 */
	AST_SECURITY_EVENT_REQ_BAD_FORMAT,
	/*!
	 * \brief FYI FWIW, Successful authentication has occurred
	 */
	AST_SECURITY_EVENT_SUCCESSFUL_AUTH,
	/*!
	 * \brief An unexpected source address was seen for a session in progress
	 */
	AST_SECURITY_EVENT_UNEXPECTED_ADDR,
	/*!
	 * \brief An attempt at challenge/response authentication failed
	 */
	AST_SECURITY_EVENT_CHAL_RESP_FAILED,
	/*!
	 * \brief An attempt at basic password authentication failed
	 */
	AST_SECURITY_EVENT_INVAL_PASSWORD,
	/*!
	 * \brief Challenge was sent out, informational
	 */
	AST_SECURITY_EVENT_CHAL_SENT,
	/*!
	 * \brief An attempt to contact a peer on an invalid transport.
	 */
	AST_SECURITY_EVENT_INVAL_TRANSPORT,
	/*!
	 * \brief This _must_ stay at the end.
	 */
	AST_SECURITY_EVENT_NUM_TYPES
};

/*!
 * \brief the severity of a security event
 *
 * This is defined as a bit field to make it easy for consumers of the API to
 * subscribe to any combination of the defined severity levels.
 *
 * XXX \todo Do we need any more levels here?
 */
enum ast_security_event_severity {
	/*! \brief Informational event, not something that has gone wrong */
	AST_SECURITY_EVENT_SEVERITY_INFO  = (1 << 0),
	/*! \brief Something has gone wrong */
	AST_SECURITY_EVENT_SEVERITY_ERROR = (1 << 1),
};

#define AST_SEC_EVT(e) ((struct ast_security_event_common *) e)

struct ast_security_event_ip_addr {
	const struct ast_sockaddr *addr;
	enum ast_transport transport;
};

/*!
 * \brief Common structure elements
 *
 * This is the structure header for all event descriptor structures defined
 * below.  The contents of this structure are very important and must not
 * change.  Even though these structures are exposed via a public API, we have
 * a version field that can be used to ensure ABI safety.  If the event
 * descriptors need to be changed or updated in the future, we can safely do
 * so and can detect ABI changes at runtime.
 */
struct ast_security_event_common {
	/*! \brief The security event sub-type */
	enum ast_security_event_type event_type;
	/*! \brief security event version */
	uint32_t version;
	/*!
	 * \brief Service that generated the event
	 * \note Always required
	 *
	 * Examples: "SIP", "AMI"
	 */
	const char *service;
	/*!
	 * \brief Module, Normally the AST_MODULE define
	 * \note Always optional
	 */
	const char *module;
	/*!
	 * \brief Account ID, specific to the service type
	 * \note optional/required, depending on event type
	 */
	const char *account_id;
	/*!
	 * \brief Session ID, specific to the service type
	 * \note Always required
	 */
	const char *session_id;
	/*!
	 * \brief Session timeval, when the session started
	 * \note Always optional
	 */
	const struct timeval *session_tv;
	/*!
	 * \brief Local address the request came in on
	 * \note Always required
	 */
	struct ast_security_event_ip_addr local_addr;
	/*!
	 * \brief Remote address the request came from
	 * \note Always required
	 */
	struct ast_security_event_ip_addr remote_addr;
};

/*!
 * \brief Checking against an IP access control list failed
 */
struct ast_security_event_failed_acl {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_FAILED_ACL_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief ACL name, identifies which ACL was hit
	 * \note optional
	 */
	const char *acl_name;
};

/*!
 * \brief Invalid account ID specified (invalid username, for example)
 */
struct ast_security_event_inval_acct_id {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
};

/*!
 * \brief Request denied because of a session limit
 */
struct ast_security_event_session_limit {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_SESSION_LIMIT_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
};

/*!
 * \brief Request denied because of a memory limit
 */
struct ast_security_event_mem_limit {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_MEM_LIMIT_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
};

/*!
 * \brief Request denied because of a load average limit
 */
struct ast_security_event_load_avg {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_LOAD_AVG_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
};

/*!
 * \brief Request denied because we don't support it
 */
struct ast_security_event_req_no_support {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_REQ_NO_SUPPORT_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Request type that was made
	 * \note required
	 */
	const char *request_type;
};

/*!
 * \brief Request denied because it's not allowed
 */
struct ast_security_event_req_not_allowed {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_REQ_NOT_ALLOWED_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Request type that was made
	 * \note required
	 */
	const char *request_type;
	/*!
	 * \brief Request type that was made
	 * \note optional
	 */
	const char *request_params;
};

/*!
 * \brief Auth method used not allowed
 */
struct ast_security_event_auth_method_not_allowed {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Auth method attempted
	 * \note required
	 */
	const char *auth_method;
};

/*!
 * \brief Invalid formatting of request
 */
struct ast_security_event_req_bad_format {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_REQ_BAD_FORMAT_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID optional
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Request type that was made
	 * \note required
	 */
	const char *request_type;
	/*!
	 * \brief Request type that was made
	 * \note optional
	 */
	const char *request_params;
};

/*!
 * \brief Successful authentication
 */
struct ast_security_event_successful_auth {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Using password - if a password was used or not
	 * \note required, 0 = no, 1 = yes
	 */
	uint32_t using_password;
};

/*!
 * \brief Unexpected source address for a session in progress
 */
struct ast_security_event_unexpected_addr {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_UNEXPECTED_ADDR_VERSION 2
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Expected remote address
	 * \note required
	 */
	struct ast_security_event_ip_addr expected_addr;
};

/*!
 * \brief An attempt at challenge/response auth failed
 */
struct ast_security_event_chal_resp_failed {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Challenge provided
	 * \note required
	 */
	const char *challenge;
	/*!
	 * \brief Response received
	 * \note required
	 */
	const char *response;
	/*!
	 * \brief Response expected to be received
	 * \note required
	 */
	const char *expected_response;
};

/*!
 * \brief An attempt at basic password auth failed
 */
struct ast_security_event_inval_password {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_INVAL_PASSWORD_VERSION 2
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Challenge provided
	 * \note required
	 */
	const char *challenge;
	/*!
	 * \brief Challenge received
	 * \note required
	 */
	const char *received_challenge;
	/*!
	 * \brief Hash received
	 * \note required
	 */
	const char *received_hash;
};

/*!
 * \brief A challenge was sent out
 */
struct ast_security_event_chal_sent {
	/*!
	 * \brief Event descriptor version
	 * \note This _must_ be changed if this event descriptor is changed.
	 */
	#define AST_SECURITY_EVENT_CHAL_SENT_VERSION 1
	/*!
	 * \brief Common security event descriptor elements
	 * \note Account ID required
	 */
	struct ast_security_event_common common;
	/*!
	 * \brief Challenge sent
	 * \note required
	 */
	const char *challenge;
};

/*!
 * \brief Attempt to contact peer on invalid transport
 */
struct ast_security_event_inval_transport {
        /*!
         * \brief Event descriptor version
         * \note This _must_ be changed if this event descriptor is changed.
         */
        #define AST_SECURITY_EVENT_INVAL_TRANSPORT_VERSION 1
        /*!
         * \brief Common security event descriptor elements
         * \note Account ID required
         */
	struct ast_security_event_common common;
	/*!
	 * \brief Attempted transport
	 * \note required
	 */
	const char *transport;
};

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* __AST_SECURITY_EVENTS_DEFS_H__ */
