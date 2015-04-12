/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Michael L. Young <elgueromexicano@gmail.com>
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
 * \brief Generate security events in the SIP channel
 *
 * \author Michael L. Young <elgueromexicano@gmail.com>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "include/sip.h"
#include "include/security_events.h"

/*! \brief Determine transport type used to receive request*/

static enum ast_transport security_event_get_transport(const struct sip_pvt *p)
{
	return p->socket.type;
}

void sip_report_invalid_peer(const struct sip_pvt *p)
{
	char session_id[32];

	struct ast_security_event_inval_acct_id inval_acct_id = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_ACCT_ID,
		.common.version    = AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION,
		.common.service    = "SIP",
		.common.account_id = p->exten,
		.common.local_addr = {
			.addr      = &p->ourip,
			.transport = security_event_get_transport(p)
		},
		.common.remote_addr = {
			.addr       = &p->sa,
			.transport = security_event_get_transport(p)
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", p);

	ast_security_event_report(AST_SEC_EVT(&inval_acct_id));
}

void sip_report_failed_acl(const struct sip_pvt *p, const char *aclname)
{
        char session_id[32];

        struct ast_security_event_failed_acl failed_acl_event = {
                .common.event_type  = AST_SECURITY_EVENT_FAILED_ACL,
                .common.version     = AST_SECURITY_EVENT_FAILED_ACL_VERSION,
                .common.service     = "SIP",
                .common.account_id  = p->exten,
                .common.local_addr  = {
                        .addr       = &p->ourip,
                        .transport  = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr       = &p->sa,
                        .transport  = security_event_get_transport(p)
                },
                .common.session_id  = session_id,
                .acl_name           = aclname,
        };

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&failed_acl_event));
}

void sip_report_inval_password(const struct sip_pvt *p, const char *response_challenge, const char *response_hash)
{
        char session_id[32];

        struct ast_security_event_inval_password inval_password = {
                .common.event_type  = AST_SECURITY_EVENT_INVAL_PASSWORD,
                .common.version     = AST_SECURITY_EVENT_INVAL_PASSWORD_VERSION,
                .common.service     = "SIP",
                .common.account_id  = p->exten,
                .common.local_addr  = {
                        .addr       = &p->ourip,
                        .transport  = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr       = &p->sa,
                        .transport  = security_event_get_transport(p)
                },
                .common.session_id  = session_id,

		.challenge	    = p->nonce,
		.received_challenge = response_challenge,
		.received_hash	    = response_hash,
        };

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&inval_password));
}

void sip_report_auth_success(const struct sip_pvt *p, uint32_t *using_password)
{
        char session_id[32];

        struct ast_security_event_successful_auth successful_auth = {
                .common.event_type  = AST_SECURITY_EVENT_SUCCESSFUL_AUTH,
                .common.version     = AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION,
                .common.service     = "SIP",
                .common.account_id  = p->exten,
                .common.local_addr  = {
                        .addr       = &p->ourip,
                        .transport  = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr       = &p->sa,
                        .transport  = security_event_get_transport(p)
                },
                .common.session_id  = session_id,
                .using_password     = using_password,
        };

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&successful_auth));
}

void sip_report_session_limit(const struct sip_pvt *p)
{
        char session_id[32];

        struct ast_security_event_session_limit session_limit = {
                .common.event_type = AST_SECURITY_EVENT_SESSION_LIMIT,
                .common.version    = AST_SECURITY_EVENT_SESSION_LIMIT_VERSION,
                .common.service    = "SIP",
                .common.account_id = p->exten,
                .common.local_addr = {
                        .addr      = &p->ourip,
                        .transport = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr      = &p->sa,
                        .transport = security_event_get_transport(p)
                },
                .common.session_id = session_id,
        };

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&session_limit));
}

void sip_report_failed_challenge_response(const struct sip_pvt *p, const char *response, const char *expected_response)
{
	char session_id[32];
	char account_id[256];

	struct ast_security_event_chal_resp_failed chal_resp_failed = {
                .common.event_type = AST_SECURITY_EVENT_CHAL_RESP_FAILED,
                .common.version    = AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION,
                .common.service    = "SIP",
                .common.account_id = account_id,
                .common.local_addr = {
                        .addr      = &p->ourip,
                        .transport = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr      = &p->sa,
                        .transport = security_event_get_transport(p)
                },
                .common.session_id = session_id,

                .challenge         = p->nonce,
                .response          = response,
                .expected_response = expected_response,
        };

	if (!ast_strlen_zero(p->from)) { /* When dialing, show account making call */
                ast_copy_string(account_id, p->from, sizeof(account_id));
        } else {
                ast_copy_string(account_id, p->exten, sizeof(account_id));
        }

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&chal_resp_failed));
}

void sip_report_chal_sent(const struct sip_pvt *p)
{
	char session_id[32];
	char account_id[256];

	struct ast_security_event_chal_sent chal_sent = {
                .common.event_type = AST_SECURITY_EVENT_CHAL_SENT,
                .common.version    = AST_SECURITY_EVENT_CHAL_SENT_VERSION,
                .common.service    = "SIP",
                .common.account_id = account_id,
                .common.local_addr = {
                        .addr      = &p->ourip,
                        .transport = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr      = &p->sa,
                        .transport = security_event_get_transport(p)
                },
                .common.session_id = session_id,

                .challenge         = p->nonce,
        };

	if (!ast_strlen_zero(p->from)) { /* When dialing, show account making call */
		ast_copy_string(account_id, p->from, sizeof(account_id));
	} else {
		ast_copy_string(account_id, p->exten, sizeof(account_id));
	}

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&chal_sent));
}

void sip_report_inval_transport(const struct sip_pvt *p, const char *transport)
{
        char session_id[32];

        struct ast_security_event_inval_transport inval_transport = {
                .common.event_type = AST_SECURITY_EVENT_INVAL_TRANSPORT,
                .common.version    = AST_SECURITY_EVENT_INVAL_TRANSPORT_VERSION,
                .common.service    = "SIP",
                .common.account_id = p->exten,
                .common.local_addr = {
                        .addr      = &p->ourip,
                        .transport = security_event_get_transport(p)
                },
                .common.remote_addr = {
                        .addr      = &p->sa,
                        .transport = security_event_get_transport(p)
                },
                .common.session_id = session_id,

                .transport         = transport,
        };

        snprintf(session_id, sizeof(session_id), "%p", p);

        ast_security_event_report(AST_SEC_EVT(&inval_transport));
}

int sip_report_security_event(const struct sip_pvt *p, const struct sip_request *req, const int res) {

	struct sip_peer *peer_report;
	enum check_auth_result res_report = res;
	struct ast_str *buf;
	char *c;
	const char *authtoken;
	char *reqheader, *respheader;
	int result = 0;
	char aclname[256];
	struct digestkeys keys[] = {
		[K_RESP]  = { "response=", "" },
		[K_URI]   = { "uri=", "" },
		[K_USER]  = { "username=", "" },
		[K_NONCE] = { "nonce=", "" },
		[K_LAST]  = { NULL, NULL}
	};

	peer_report = sip_find_peer(p->exten, NULL, TRUE, FINDPEERS, FALSE, 0);

	switch(res_report) {
	case AUTH_DONT_KNOW:
		break;
	case AUTH_SUCCESSFUL:
		if (peer_report) {
			if (ast_strlen_zero(peer_report->secret) && ast_strlen_zero(peer_report->md5secret)) {
			sip_report_auth_success(p, (uint32_t *) 0);
			} else {
				sip_report_auth_success(p, (uint32_t *) 1);
			}
		}
		break;
	case AUTH_CHALLENGE_SENT:
		sip_report_chal_sent(p);
		break;
	case AUTH_SECRET_FAILED:
	case AUTH_USERNAME_MISMATCH:
		sip_auth_headers(WWW_AUTH, &respheader, &reqheader);
		authtoken = sip_get_header(req, reqheader);
		buf = ast_str_thread_get(&check_auth_buf, CHECK_AUTH_BUF_INITLEN);
		ast_str_set(&buf, 0, "%s", authtoken);
		c = ast_str_buffer(buf);

		sip_digest_parser(c, keys);

		if (res_report == AUTH_SECRET_FAILED) {
			sip_report_inval_password(p, keys[K_NONCE].s, keys[K_RESP].s);
		} else {
			if (peer_report) {
				sip_report_failed_challenge_response(p, keys[K_USER].s, peer_report->username);
			}
		}
		break;
	case AUTH_NOT_FOUND:
		/* with sip_cfg.alwaysauthreject on, generates 2 events */
		sip_report_invalid_peer(p);
		break;
	case AUTH_UNKNOWN_DOMAIN:
		snprintf(aclname, sizeof(aclname), "domain_must_match");
		sip_report_failed_acl(p, aclname);
		break;
	case AUTH_PEER_NOT_DYNAMIC:
		snprintf(aclname, sizeof(aclname), "peer_not_dynamic");
		sip_report_failed_acl(p, aclname);
		break;
	case AUTH_ACL_FAILED:
		/* with sip_cfg.alwaysauthreject on, generates 2 events */
		snprintf(aclname, sizeof(aclname), "device_must_match_acl");
		sip_report_failed_acl(p, aclname);
		break;
	case AUTH_BAD_TRANSPORT:
		sip_report_inval_transport(p, sip_get_transport(req->socket.type));
		break;
	case AUTH_RTP_FAILED:
		break;
	case AUTH_SESSION_LIMIT:
		sip_report_session_limit(p);
		break;
	}

	if (peer_report) {
		sip_unref_peer(peer_report, "sip_report_security_event: sip_unref_peer: from handle_incoming");
	}

	return result;
}

