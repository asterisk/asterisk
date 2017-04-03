/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014-2016, Digium, Inc.
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "include/res_pjsip_private.h"

#define MOD_DATA_RESTRICTIONS "restrictions"

static pj_status_t multihomed_on_tx_message(pjsip_tx_data *tdata);
static pj_bool_t multihomed_on_rx_message(pjsip_rx_data *rdata);

/*! \brief Outgoing message modification restrictions */
struct multihomed_message_restrictions {
	/*! \brief Disallow modification of the From domain */
	unsigned int disallow_from_domain_modification;
};

static pjsip_module multihomed_module = {
	.name = { "Multihomed Routing", 18 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 1,
	.on_tx_request = multihomed_on_tx_message,
	.on_tx_response = multihomed_on_tx_message,
	.on_rx_request = multihomed_on_rx_message,
};

/*! \brief Helper function to get (or allocate if not already present) restrictions on a message */
static struct multihomed_message_restrictions *multihomed_get_restrictions(pjsip_tx_data *tdata)
{
	struct multihomed_message_restrictions *restrictions;

	restrictions = ast_sip_mod_data_get(tdata->mod_data, multihomed_module.id, MOD_DATA_RESTRICTIONS);
	if (restrictions) {
		return restrictions;
	}

	restrictions = PJ_POOL_ALLOC_T(tdata->pool, struct multihomed_message_restrictions);
	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, multihomed_module.id, MOD_DATA_RESTRICTIONS, restrictions);

	return restrictions;
}

/*! \brief Callback invoked on non-session outgoing messages */
static void multihomed_outgoing_message(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, struct pjsip_tx_data *tdata)
{
	struct multihomed_message_restrictions *restrictions = multihomed_get_restrictions(tdata);

	restrictions->disallow_from_domain_modification = !ast_strlen_zero(endpoint->fromdomain);
}

/*! \brief PJSIP Supplement for tagging messages with restrictions */
static struct ast_sip_supplement multihomed_supplement = {
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST,
	.outgoing_request = multihomed_outgoing_message,
	.outgoing_response = multihomed_outgoing_message,
};

/*! \brief Callback invoked on session outgoing messages */
static void multihomed_session_outgoing_message(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	struct multihomed_message_restrictions *restrictions = multihomed_get_restrictions(tdata);

	restrictions->disallow_from_domain_modification = !ast_strlen_zero(session->endpoint->fromdomain);
}

/*! \brief PJSIP Session Supplement for tagging messages with restrictions */
static struct ast_sip_session_supplement multihomed_session_supplement = {
	.priority = 1,
	.outgoing_request = multihomed_session_outgoing_message,
	.outgoing_response = multihomed_session_outgoing_message,
};

/*! \brief Helper function which returns a UDP transport bound to the given address and port */
static pjsip_transport *multihomed_get_udp_transport(pj_str_t *address, int port)
{
	struct ao2_container *transport_states = ast_sip_get_transport_states();
	struct ast_sip_transport_state *transport_state;
	struct ao2_iterator iter;
	pjsip_transport *sip_transport = NULL;

	if (!transport_states) {
		return NULL;
	}

	for (iter = ao2_iterator_init(transport_states, 0); (transport_state = ao2_iterator_next(&iter)); ao2_ref(transport_state, -1)) {
		if (transport_state && ((transport_state->type != AST_TRANSPORT_UDP) ||
			(pj_strcmp(&transport_state->transport->local_name.host, address)) ||
			(transport_state->transport->local_name.port != port))) {
			continue;
		}

		sip_transport = transport_state->transport;
		break;
	}
	ao2_iterator_destroy(&iter);

	ao2_ref(transport_states, -1);

	return sip_transport;
}

/*! \brief Helper function which determines if a transport is bound to any */
static int multihomed_bound_any(pjsip_transport *transport)
{
	pj_uint32_t loop6[4] = {0, 0, 0, 0};

	if ((transport->local_addr.addr.sa_family == pj_AF_INET() &&
		transport->local_addr.ipv4.sin_addr.s_addr == PJ_INADDR_ANY) ||
		(transport->local_addr.addr.sa_family == pj_AF_INET6() &&
		!pj_memcmp(&transport->local_addr.ipv6.sin6_addr, loop6, sizeof(loop6)))) {
		return 1;
	}

	return 0;
}

/*! \brief Helper function which determines if the address within SDP should be rewritten */
static int multihomed_rewrite_sdp(struct pjmedia_sdp_session *sdp)
{
	if (!sdp->conn) {
		return 0;
	}

	/* If the host address is used in the SDP replace it with the address of what this is going out on */
	if ((!pj_strcmp2(&sdp->conn->addr_type, "IP4") && !pj_strcmp2(&sdp->conn->addr,
		ast_sip_get_host_ip_string(pj_AF_INET()))) ||
		(!pj_strcmp2(&sdp->conn->addr_type, "IP6") && !pj_strcmp2(&sdp->conn->addr,
		ast_sip_get_host_ip_string(pj_AF_INET6())))) {
		return 1;
	}

	return 0;
}

static void sanitize_tdata(pjsip_tx_data *tdata)
{
	static const pj_str_t x_name = { AST_SIP_X_AST_TXP, AST_SIP_X_AST_TXP_LEN };
	pjsip_param *x_transport;
	pjsip_sip_uri *uri;
	pjsip_fromto_hdr *fromto;
	pjsip_contact_hdr *contact;
	pjsip_hdr *hdr;

	if (tdata->msg->type == PJSIP_REQUEST_MSG) {
		uri = pjsip_uri_get_uri(tdata->msg->line.req.uri);
		x_transport = pjsip_param_find(&uri->other_param, &x_name);
		if (x_transport) {
			pj_list_erase(x_transport);
		}
	}

	for (hdr = tdata->msg->hdr.next; hdr != &tdata->msg->hdr; hdr = hdr->next) {
		if (hdr->type == PJSIP_H_TO || hdr->type == PJSIP_H_FROM) {
			fromto = (pjsip_fromto_hdr *) hdr;
			uri = pjsip_uri_get_uri(fromto->uri);
			x_transport = pjsip_param_find(&uri->other_param, &x_name);
			if (x_transport) {
				pj_list_erase(x_transport);
			}
		} else if (hdr->type == PJSIP_H_CONTACT) {
			contact = (pjsip_contact_hdr *) hdr;
			uri = pjsip_uri_get_uri(contact->uri);
			x_transport = pjsip_param_find(&uri->other_param, &x_name);
			if (x_transport) {
				pj_list_erase(x_transport);
			}
		}
	}

	pjsip_tx_data_invalidate_msg(tdata);
}

static pj_status_t multihomed_on_tx_message(pjsip_tx_data *tdata)
{
	struct multihomed_message_restrictions *restrictions = ast_sip_mod_data_get(tdata->mod_data, multihomed_module.id, MOD_DATA_RESTRICTIONS);
	pjsip_tpmgr_fla2_param prm;
	pjsip_cseq_hdr *cseq;
	pjsip_via_hdr *via;
	pjsip_fromto_hdr *from;

	sanitize_tdata(tdata);

	/* Use the destination information to determine what local interface this message will go out on */
	pjsip_tpmgr_fla2_param_default(&prm);
	prm.tp_type = tdata->tp_info.transport->key.type;
	pj_strset2(&prm.dst_host, tdata->tp_info.dst_name);
	prm.local_if = PJ_TRUE;

	/* If we can't get the local address use best effort and let it pass */
	if (pjsip_tpmgr_find_local_addr2(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), tdata->pool, &prm) != PJ_SUCCESS) {
		return PJ_SUCCESS;
	}

	/* For UDP we can have multiple transports so the port needs to be maintained */
	if (tdata->tp_info.transport->key.type == PJSIP_TRANSPORT_UDP ||
		tdata->tp_info.transport->key.type == PJSIP_TRANSPORT_UDP6) {
		prm.ret_port = tdata->tp_info.transport->local_name.port;
	}

	/* If the IP source differs from the existing transport see if we need to update it */
	if (pj_strcmp(&prm.ret_addr, &tdata->tp_info.transport->local_name.host)) {

		/* If the transport it is going out on is different reflect it in the message */
		if (tdata->tp_info.transport->key.type == PJSIP_TRANSPORT_UDP ||
			tdata->tp_info.transport->key.type == PJSIP_TRANSPORT_UDP6) {
			pjsip_transport *transport;

			transport = multihomed_get_udp_transport(&prm.ret_addr, prm.ret_port);

			if (transport) {
				tdata->tp_info.transport = transport;
			}
		}

		/* If the chosen transport is not bound to any we can't use the source address as it won't get back to us */
		if (!multihomed_bound_any(tdata->tp_info.transport)) {
			pj_strassign(&prm.ret_addr, &tdata->tp_info.transport->local_name.host);
		}
	} else {
		/* The transport chosen will deliver this but ensure it is updated with the right information */
		pj_strassign(&prm.ret_addr, &tdata->tp_info.transport->local_name.host);
	}

	/* If the message needs to be updated with new address do so */
	if (tdata->msg->type == PJSIP_REQUEST_MSG || !(cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL)) ||
		pj_strcmp2(&cseq->method.name, "REGISTER")) {
		pjsip_contact_hdr *contact = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL);
		if (contact && (PJSIP_URI_SCHEME_IS_SIP(contact->uri) || PJSIP_URI_SCHEME_IS_SIPS(contact->uri))
			&& !(tdata->msg->type == PJSIP_RESPONSE_MSG && tdata->msg->line.status.code / 100 == 3)) {
			pjsip_sip_uri *uri = pjsip_uri_get_uri(contact->uri);

			/* prm.ret_addr is allocated from the tdata pool OR the transport so it is perfectly fine to just do an assignment like this */
			pj_strassign(&uri->host, &prm.ret_addr);
			uri->port = prm.ret_port;
			ast_debug(4, "Re-wrote Contact URI host/port to %.*s:%d\n",
				(int)pj_strlen(&uri->host), pj_strbuf(&uri->host), uri->port);

			if (tdata->tp_info.transport->key.type == PJSIP_TRANSPORT_UDP ||
				tdata->tp_info.transport->key.type == PJSIP_TRANSPORT_UDP6) {
				uri->transport_param.slen = 0;
			} else {
				pj_strdup2(tdata->pool, &uri->transport_param, pjsip_transport_get_type_name(tdata->tp_info.transport->key.type));
			}

			pjsip_tx_data_invalidate_msg(tdata);
		}
	}

	if (tdata->msg->type == PJSIP_REQUEST_MSG && (via = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_VIA, NULL))) {
		pj_strassign(&via->sent_by.host, &prm.ret_addr);
		via->sent_by.port = prm.ret_port;

		pjsip_tx_data_invalidate_msg(tdata);
	}

	if (tdata->msg->type == PJSIP_REQUEST_MSG && (from = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_FROM, NULL)) &&
		(restrictions && !restrictions->disallow_from_domain_modification)) {
		pjsip_name_addr *id_name_addr = (pjsip_name_addr *)from->uri;
		pjsip_sip_uri *uri = pjsip_uri_get_uri(id_name_addr);
		pj_sockaddr ip;

		if (pj_strcmp2(&uri->host, "localhost") && pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &uri->host, &ip) == PJ_SUCCESS) {
			pj_strassign(&uri->host, &prm.ret_addr);
			pjsip_tx_data_invalidate_msg(tdata);
		}
	}

	/* Update the SDP if it is present */
	if (tdata->msg->body && ast_sip_is_content_type(&tdata->msg->body->content_type, "application", "sdp") &&
		multihomed_rewrite_sdp(tdata->msg->body->data)) {
		struct pjmedia_sdp_session *sdp = tdata->msg->body->data;
		static const pj_str_t STR_IP4 = { "IP4", 3 };
		static const pj_str_t STR_IP6 = { "IP6", 3 };
		pj_str_t STR_IP;
		int stream;

		STR_IP = tdata->tp_info.transport->key.type & PJSIP_TRANSPORT_IPV6 ? STR_IP6 : STR_IP4;

		pj_strassign(&sdp->origin.addr, &prm.ret_addr);
		sdp->origin.addr_type = STR_IP;
		pj_strassign(&sdp->conn->addr, &prm.ret_addr);
		sdp->conn->addr_type = STR_IP;

		for (stream = 0; stream < sdp->media_count; ++stream) {
			if (sdp->media[stream]->conn) {
				pj_strassign(&sdp->media[stream]->conn->addr, &prm.ret_addr);
				sdp->media[stream]->conn->addr_type = STR_IP;
			}
		}

		pjsip_tx_data_invalidate_msg(tdata);
	}

	return PJ_SUCCESS;
}

static pj_bool_t multihomed_on_rx_message(pjsip_rx_data *rdata)
{
	pjsip_contact_hdr *contact;
	pjsip_sip_uri *uri;
	const char *transport_id;
	struct ast_sip_transport *transport;
	pjsip_param *x_transport;

	if (rdata->msg_info.msg->type != PJSIP_REQUEST_MSG) {
		return PJ_FALSE;
	}

	contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
	if (!(contact && contact->uri
		&& ast_begins_with(rdata->tp_info.transport->info, AST_SIP_X_AST_TXP ":"))) {
		return PJ_FALSE;
	}

	uri = pjsip_uri_get_uri(contact->uri);

	transport_id = rdata->tp_info.transport->info + AST_SIP_X_AST_TXP_LEN + 1;
	transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_id);

	if (!(transport && transport->symmetric_transport)) {
		ao2_cleanup(transport);
		return PJ_FALSE;
	}
	ao2_cleanup(transport);

	x_transport = PJ_POOL_ALLOC_T(rdata->tp_info.pool, pjsip_param);
	x_transport->name = pj_strdup3(rdata->tp_info.pool, AST_SIP_X_AST_TXP);
	x_transport->value = pj_strdup3(rdata->tp_info.pool, transport_id);

	pj_list_insert_before(&uri->other_param, x_transport);

	ast_debug(1, "Set transport '%s' on %.*s from %.*s:%d\n", transport_id,
		(int)rdata->msg_info.msg->line.req.method.name.slen,
		rdata->msg_info.msg->line.req.method.name.ptr,
		(int)uri->host.slen, uri->host.ptr, uri->port);

	return PJ_FALSE;
}

void ast_res_pjsip_cleanup_message_ip_updater(void)
{
	ast_sip_unregister_service(&multihomed_module);
	ast_sip_unregister_supplement(&multihomed_supplement);
	ast_sip_session_unregister_supplement(&multihomed_session_supplement);
}

int ast_res_pjsip_init_message_ip_updater(void)
{
	if (ast_sip_session_register_supplement(&multihomed_session_supplement)) {
		ast_log(LOG_ERROR, "Could not register multihomed session supplement for outgoing requests\n");
		return -1;
	}

	if (ast_sip_register_supplement(&multihomed_supplement)) {
		ast_log(LOG_ERROR, "Could not register multihomed supplement for outgoing requests\n");
		ast_res_pjsip_cleanup_message_ip_updater();
		return -1;
	}

	if (ast_sip_register_service(&multihomed_module)) {
		ast_log(LOG_ERROR, "Could not register multihomed module for incoming and outgoing requests\n");
		ast_res_pjsip_cleanup_message_ip_updater();
		return -1;
	}

	return 0;
}
