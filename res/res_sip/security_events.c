/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Generate security events in the PJSIP channel
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pjsip.h>

#include "asterisk/res_sip.h"
#include "asterisk/security_events.h"

static int find_transport_in_use(void *obj, void *arg, int flags)
{
	struct ast_sip_transport *transport = obj;
	pjsip_rx_data *rdata = arg;

	if ((transport->state->transport == rdata->tp_info.transport) ||
		(transport->state->factory && !pj_strcmp(&transport->state->factory->addr_name.host, &rdata->tp_info.transport->local_name.host) &&
			transport->state->factory->addr_name.port == rdata->tp_info.transport->local_name.port)) {
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

static enum ast_transport security_event_get_transport(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ao2_container *, transports, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);

	/* It should be impossible for these to fail as the transport has to exist for the message to exist */
	transports = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "transport", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	ast_assert(transports != NULL);

	transport = ao2_callback(transports, 0, find_transport_in_use, rdata);

	ast_assert(transport != NULL);

	return transport->type;
}

static void security_event_populate(pjsip_rx_data *rdata, char *call_id, size_t call_id_size, struct ast_sockaddr *local, struct ast_sockaddr *remote)
{
	char host[NI_MAXHOST];

	ast_copy_pj_str(call_id, &rdata->msg_info.cid->id, call_id_size);

	ast_copy_pj_str(host, &rdata->tp_info.transport->local_name.host, sizeof(host));
	ast_sockaddr_parse(local, host, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(local, rdata->tp_info.transport->local_name.port);

	ast_sockaddr_parse(remote, rdata->pkt_info.src_name, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(remote, rdata->pkt_info.src_port);
}

void ast_sip_report_invalid_endpoint(const char *name, pjsip_rx_data *rdata)
{
	enum ast_transport transport = security_event_get_transport(rdata);
	char call_id[pj_strlen(&rdata->msg_info.cid->id) + 1];
	struct ast_sockaddr local, remote;

	struct ast_security_event_inval_acct_id inval_acct_id = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_ACCT_ID,
		.common.version    = AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION,
		.common.service    = "PJSIP",
		.common.account_id = name,
		.common.local_addr = {
			.addr      = &local,
			.transport = transport,
		},
		.common.remote_addr = {
			.addr       = &remote,
			.transport = transport,
		},
		.common.session_id = call_id,
	};

	security_event_populate(rdata, call_id, sizeof(call_id), &local, &remote);

	ast_security_event_report(AST_SEC_EVT(&inval_acct_id));
}

void ast_sip_report_failed_acl(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, const char *name)
{
	enum ast_transport transport = security_event_get_transport(rdata);
	char call_id[pj_strlen(&rdata->msg_info.cid->id) + 1];
	struct ast_sockaddr local, remote;

	struct ast_security_event_failed_acl failed_acl_event = {
			.common.event_type  = AST_SECURITY_EVENT_FAILED_ACL,
			.common.version     = AST_SECURITY_EVENT_FAILED_ACL_VERSION,
			.common.service     = "PJSIP",
			.common.account_id  = ast_sorcery_object_get_id(endpoint),
			.common.local_addr  = {
					.addr       = &local,
					.transport  = transport,
			},
			.common.remote_addr = {
					.addr       = &remote,
					.transport  = transport,
			},
			.common.session_id  = call_id,
			.acl_name           = name,
	};

	security_event_populate(rdata, call_id, sizeof(call_id), &local, &remote);

	ast_security_event_report(AST_SEC_EVT(&failed_acl_event));
}

void ast_sip_report_auth_failed_challenge_response(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	pjsip_authorization_hdr *auth = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_AUTHORIZATION, NULL);
	enum ast_transport transport = security_event_get_transport(rdata);
	char call_id[pj_strlen(&rdata->msg_info.cid->id) + 1];
	char nonce[64] = "", response[256] = "";
	struct ast_sockaddr local, remote;

	struct ast_security_event_chal_resp_failed chal_resp_failed = {
				.common.event_type = AST_SECURITY_EVENT_CHAL_RESP_FAILED,
				.common.version    = AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION,
				.common.service    = "PJSIP",
				.common.account_id = ast_sorcery_object_get_id(endpoint),
				.common.local_addr = {
						.addr      = &local,
						.transport = transport,
				},
				.common.remote_addr = {
						.addr      = &remote,
						.transport = transport,
				},
				.common.session_id = call_id,

				.challenge         = nonce,
				.response          = response,
				.expected_response = "",
		};

	if (auth && !pj_strcmp2(&auth->scheme, "digest")) {
		ast_copy_pj_str(nonce, &auth->credential.digest.nonce, sizeof(nonce));
		ast_copy_pj_str(response, &auth->credential.digest.response, sizeof(response));
	}

	security_event_populate(rdata, call_id, sizeof(call_id), &local, &remote);

	ast_security_event_report(AST_SEC_EVT(&chal_resp_failed));
}

void ast_sip_report_auth_success(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	pjsip_authorization_hdr *auth = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_AUTHORIZATION, NULL);
	enum ast_transport transport = security_event_get_transport(rdata);
	char call_id[pj_strlen(&rdata->msg_info.cid->id) + 1];
	struct ast_sockaddr local, remote;

	struct ast_security_event_successful_auth successful_auth = {
			.common.event_type  = AST_SECURITY_EVENT_SUCCESSFUL_AUTH,
			.common.version     = AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION,
			.common.service     = "PJSIP",
			.common.account_id  = ast_sorcery_object_get_id(endpoint),
			.common.local_addr  = {
					.addr       = &local,
					.transport  = transport,
			},
			.common.remote_addr = {
					.addr       = &remote,
					.transport  = transport,
			},
			.common.session_id  = call_id,
			.using_password     = auth ? (uint32_t *)1 : (uint32_t *)0,
	};

	security_event_populate(rdata, call_id, sizeof(call_id), &local, &remote);

	ast_security_event_report(AST_SEC_EVT(&successful_auth));
}

void ast_sip_report_auth_challenge_sent(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pjsip_tx_data *tdata)
{
	pjsip_www_authenticate_hdr *auth = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_WWW_AUTHENTICATE, NULL);
	enum ast_transport transport = security_event_get_transport(rdata);
	char nonce[64] = "", call_id[pj_strlen(&rdata->msg_info.cid->id) + 1];
	struct ast_sockaddr local, remote;

	struct ast_security_event_chal_sent chal_sent = {
			.common.event_type = AST_SECURITY_EVENT_CHAL_SENT,
			.common.version    = AST_SECURITY_EVENT_CHAL_SENT_VERSION,
			.common.service    = "PJSIP",
			.common.account_id = ast_sorcery_object_get_id(endpoint),
			.common.local_addr = {
					.addr      = &local,
					.transport = transport,
			},
			.common.remote_addr = {
					.addr      = &remote,
					.transport = transport,
			},
			.common.session_id = call_id,
			.challenge         = nonce,
	};

	if (auth && !pj_strcmp2(&auth->scheme, "digest")) {
		ast_copy_pj_str(nonce, &auth->challenge.digest.nonce, sizeof(nonce));
	}

	security_event_populate(rdata, call_id, sizeof(call_id), &local, &remote);

	ast_security_event_report(AST_SEC_EVT(&chal_sent));
}
