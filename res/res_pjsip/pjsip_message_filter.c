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
#include "asterisk/module.h"

#define MOD_DATA_RESTRICTIONS "restrictions"

static pj_status_t filter_on_tx_message(pjsip_tx_data *tdata);
static pj_bool_t filter_on_rx_message(pjsip_rx_data *rdata);

/*! \brief Outgoing message modification restrictions */
struct filter_message_restrictions {
	/*! \brief Disallow modification of the From domain */
	unsigned int disallow_from_domain_modification;
};

static pjsip_module filter_module_transport = {
	.name = { "Message Filtering Transport", 27 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TRANSPORT_LAYER,
	.on_rx_request = filter_on_rx_message,
};

static pjsip_module filter_module_tsx = {
	.name = { "Message Filtering TSX", 21 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 1,
	.on_tx_request = filter_on_tx_message,
	.on_tx_response = filter_on_tx_message,
};

/*! \brief Helper function to get (or allocate if not already present) restrictions on a message */
static struct filter_message_restrictions *get_restrictions(pjsip_tx_data *tdata)
{
	struct filter_message_restrictions *restrictions;

	restrictions = ast_sip_mod_data_get(tdata->mod_data, filter_module_tsx.id, MOD_DATA_RESTRICTIONS);
	if (restrictions) {
		return restrictions;
	}

	restrictions = PJ_POOL_ALLOC_T(tdata->pool, struct filter_message_restrictions);
	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, filter_module_tsx.id, MOD_DATA_RESTRICTIONS, restrictions);

	return restrictions;
}

/*! \brief Callback invoked on non-session outgoing messages */
static void filter_outgoing_message(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, struct pjsip_tx_data *tdata)
{
	struct filter_message_restrictions *restrictions = get_restrictions(tdata);

	restrictions->disallow_from_domain_modification = !ast_strlen_zero(endpoint->fromdomain);
}

/*! \brief PJSIP Supplement for tagging messages with restrictions */
static struct ast_sip_supplement filter_supplement = {
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST,
	.outgoing_request = filter_outgoing_message,
	.outgoing_response = filter_outgoing_message,
};

/*! \brief Callback invoked on session outgoing messages */
static void filter_session_outgoing_message(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	struct filter_message_restrictions *restrictions = get_restrictions(tdata);

	restrictions->disallow_from_domain_modification = !ast_strlen_zero(session->endpoint->fromdomain);
}

/*! \brief PJSIP Session Supplement for tagging messages with restrictions */
static struct ast_sip_session_supplement filter_session_supplement = {
	.priority = 1,
	.outgoing_request = filter_session_outgoing_message,
	.outgoing_response = filter_session_outgoing_message,
};

/*! \brief Helper function which returns a UDP transport bound to the given address and port */
static pjsip_transport *get_udp_transport(pj_str_t *address, int port)
{
	struct ao2_container *transport_states = ast_sip_get_transport_states();
	struct ast_sip_transport_state *transport_state;
	struct ao2_iterator iter;
	pjsip_transport *sip_transport = NULL;

	if (!transport_states) {
		return NULL;
	}

	for (iter = ao2_iterator_init(transport_states, 0); (transport_state = ao2_iterator_next(&iter)); ao2_ref(transport_state, -1)) {
		if (!transport_state->flow &&
			transport_state->type == AST_TRANSPORT_UDP &&
			!pj_strcmp(&transport_state->transport->local_name.host, address) &&
			transport_state->transport->local_name.port == port) {
			sip_transport = transport_state->transport;
			ao2_ref(transport_state, -1);
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	ao2_ref(transport_states, -1);

	return sip_transport;
}

/*! \brief Helper function which determines if a transport is bound to any */
static int is_bound_any(pjsip_transport *transport)
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

#define is_sip_uri(uri) \
	(PJSIP_URI_SCHEME_IS_SIP(uri) || PJSIP_URI_SCHEME_IS_SIPS(uri))

static void print_sanitize_debug(char *msg, pjsip_uri_context_e context, pjsip_sip_uri *uri)
{
#ifdef AST_DEVMODE
	char hdrbuf[512];
	int hdrbuf_len;

	hdrbuf_len = pjsip_uri_print(context, uri, hdrbuf, 512);
	hdrbuf[hdrbuf_len] = '\0';
	ast_debug(2, "%s: %s\n", msg, hdrbuf);
#endif
}

/* If in DEVMODE, prevent inlining to assist in debugging */
#ifdef AST_DEVMODE
#define FUNC_ATTRS __attribute__ ((noinline))
#else
#define FUNC_ATTRS
#endif

static void FUNC_ATTRS sanitize_tdata(pjsip_tx_data *tdata)
{
	static const pj_str_t x_name = { AST_SIP_X_AST_TXP, AST_SIP_X_AST_TXP_LEN };
	pjsip_param *x_transport;
	pjsip_sip_uri *uri;
	pjsip_hdr *hdr;

	if (tdata->msg->type == PJSIP_REQUEST_MSG) {
		if (is_sip_uri(tdata->msg->line.req.uri)) {
			uri = pjsip_uri_get_uri(tdata->msg->line.req.uri);
			print_sanitize_debug("Sanitizing Request", PJSIP_URI_IN_REQ_URI, uri);
			while ((x_transport = pjsip_param_find(&uri->other_param, &x_name))) {
				pj_list_erase(x_transport);
			}
		}
	}

	for (hdr = tdata->msg->hdr.next; hdr != &tdata->msg->hdr; hdr = hdr->next) {
		if (hdr->type == PJSIP_H_TO || hdr->type == PJSIP_H_FROM) {
			if (is_sip_uri(((pjsip_fromto_hdr *) hdr)->uri)) {
				uri = pjsip_uri_get_uri(((pjsip_fromto_hdr *) hdr)->uri);
				print_sanitize_debug("Sanitizing From/To header", PJSIP_URI_IN_FROMTO_HDR, uri);
				while ((x_transport = pjsip_param_find(&uri->other_param, &x_name))) {
					pj_list_erase(x_transport);
				}
			}
		} else if (hdr->type == PJSIP_H_CONTACT) {
			if (!((pjsip_contact_hdr *) hdr)->star && is_sip_uri(((pjsip_contact_hdr *) hdr)->uri)) {
				uri = pjsip_uri_get_uri(((pjsip_contact_hdr *) hdr)->uri);
				print_sanitize_debug("Sanitizing Contact header", PJSIP_URI_IN_CONTACT_HDR, uri);
				while ((x_transport = pjsip_param_find(&uri->other_param, &x_name))) {
					pj_list_erase(x_transport);
				}
			}
		}
	}

	pjsip_tx_data_invalidate_msg(tdata);
}

static pj_status_t filter_on_tx_message(pjsip_tx_data *tdata)
{
	struct filter_message_restrictions *restrictions =
		ast_sip_mod_data_get(tdata->mod_data, filter_module_transport.id, MOD_DATA_RESTRICTIONS);
	pjsip_tpmgr_fla2_param prm;
	pjsip_cseq_hdr *cseq;
	pjsip_via_hdr *via;
	pjsip_fromto_hdr *from;
	pjsip_tpselector sel;
	pjsip_sdp_info *sdp_info;
	pjmedia_sdp_session *sdp;

	sanitize_tdata(tdata);

	/* Use the destination information to determine what local interface this message will go out on */
	pjsip_tpmgr_fla2_param_default(&prm);
	prm.tp_type = tdata->tp_info.transport->key.type;
	pj_strset2(&prm.dst_host, tdata->tp_info.dst_name);
	prm.local_if = PJ_TRUE;

	if ((tdata->tp_info.transport->key.type != PJSIP_TRANSPORT_UDP) &&
		(tdata->tp_info.transport->key.type != PJSIP_TRANSPORT_UDP6)) {
		sel.type = PJSIP_TPSELECTOR_LISTENER;
		sel.u.listener = tdata->tp_info.transport->factory;
		prm.tp_sel = &sel;
	}

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

			transport = get_udp_transport(&prm.ret_addr, prm.ret_port);

			if (transport) {
				tdata->tp_info.transport = transport;
			}
		}

		/* If the chosen transport is not bound to any we can't use the source address as it won't get back to us */
		if (!is_bound_any(tdata->tp_info.transport)) {
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
			ast_debug(5, "Re-wrote Contact URI host/port to %.*s:%d (this may be re-written again later)\n",
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

	/* If there's no body in the tdata we can just return here. */
	if (!tdata->msg->body) {
		return PJ_SUCCESS;
	}

	/*
	 * pjsip_get_sdp_info will search for an SDP even if it's in
	 * a multipart message body.
	 */
	sdp_info = pjsip_get_sdp_info(tdata->pool, tdata->msg->body, NULL, &pjsip_media_type_application_sdp);
	if (sdp_info->sdp_err != PJ_SUCCESS || !sdp_info->sdp) {
		return PJ_SUCCESS;
	}

	sdp = sdp_info->sdp;

	if (multihomed_rewrite_sdp(sdp)) {
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

enum uri_type {
	URI_TYPE_REQUEST = -1,
	URI_TYPE_TO = PJSIP_H_TO,
	URI_TYPE_FROM = PJSIP_H_FROM,
	URI_TYPE_CONTACT = PJSIP_H_CONTACT,
};

static void print_uri_debug(enum uri_type ut, pjsip_rx_data *rdata, pjsip_hdr *hdr)
{
#ifdef AST_DEVMODE
	pjsip_uri *local_uri = NULL;
	char hdrbuf[512];
	int hdrbuf_len;
	char *request_uri;
	pjsip_uri_context_e context = PJSIP_URI_IN_OTHER;
	char header_name[32];

	switch (ut) {
	case(URI_TYPE_REQUEST):
		context = PJSIP_URI_IN_REQ_URI;
		strcpy(header_name, "Request"); /* Safe */
		local_uri = rdata->msg_info.msg->line.req.uri;
		break;
	case(PJSIP_H_FROM):
		strcpy(header_name, "From"); /* Safe */
		context = PJSIP_URI_IN_FROMTO_HDR;
		local_uri = pjsip_uri_get_uri(((pjsip_from_hdr *)hdr)->uri);
		break;
	case(PJSIP_H_TO):
		strcpy(header_name, "To"); /* Safe */
		context = PJSIP_URI_IN_FROMTO_HDR;
		local_uri = pjsip_uri_get_uri(((pjsip_to_hdr *)hdr)->uri);
		break;
	case(PJSIP_H_CONTACT):
		strcpy(header_name, "Contact"); /* Safe */
		context = PJSIP_URI_IN_CONTACT_HDR;
		local_uri = pjsip_uri_get_uri(((pjsip_contact_hdr *)hdr)->uri);
		break;
	}

	hdrbuf_len = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, rdata->msg_info.msg->line.req.uri, hdrbuf, 512);
	hdrbuf[hdrbuf_len] = '\0';
	request_uri = ast_strdupa(hdrbuf);
	hdrbuf_len = pjsip_uri_print(context, local_uri, hdrbuf, 512);
	hdrbuf[hdrbuf_len] = '\0';

	ast_debug(2, "There was a non sip(s) URI scheme in %s URI '%s' for request '%*.*s %s'\n",
		header_name, hdrbuf,
		(int)rdata->msg_info.msg->line.req.method.name.slen,
		(int)rdata->msg_info.msg->line.req.method.name.slen,
		rdata->msg_info.msg->line.req.method.name.ptr, request_uri);
#endif
}

/*!
 * /internal
 *
 * We want to make sure that any incoming requests don't already
 * have x-ast-* parameters in any URIs or we may get confused
 * if symmetric transport (x-ast-txp) or rewrite_contact (x-ast-orig-host)
 * is used later on.
 */
static void remove_x_ast_params(pjsip_uri *header_uri){
	pjsip_sip_uri *uri;
	pjsip_param *param;

	if (!header_uri) {
		return;
	}

	uri = pjsip_uri_get_uri(header_uri);
	if (!uri) {
		return;
	}

	param = uri->other_param.next;

	while (param != &uri->other_param) {
		/* We need to save off 'next' because pj_list_erase will remove it */
		pjsip_param *next = param->next;

		if (pj_strncmp2(&param->name, "x-ast-", 6) == 0) {
			pj_list_erase(param);
		}
		param = next;
	}
}

static pj_bool_t on_rx_process_uris(pjsip_rx_data *rdata)
{
	pjsip_contact_hdr *contact = NULL;

	if (rdata->msg_info.msg->type != PJSIP_REQUEST_MSG) {
		return PJ_FALSE;
	}

	if (!is_sip_uri(rdata->msg_info.msg->line.req.uri)) {
		print_uri_debug(URI_TYPE_REQUEST, rdata, NULL);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
			PJSIP_SC_UNSUPPORTED_URI_SCHEME, NULL, NULL, NULL);
		return PJ_TRUE;
	}
	remove_x_ast_params(rdata->msg_info.msg->line.req.uri);

	if (!is_sip_uri(rdata->msg_info.from->uri)) {
		print_uri_debug(URI_TYPE_FROM, rdata, (pjsip_hdr *)rdata->msg_info.from);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
			PJSIP_SC_UNSUPPORTED_URI_SCHEME, NULL, NULL, NULL);
		return PJ_TRUE;
	}
	remove_x_ast_params(rdata->msg_info.from->uri);

	if (!is_sip_uri(rdata->msg_info.to->uri)) {
		print_uri_debug(URI_TYPE_TO, rdata, (pjsip_hdr *)rdata->msg_info.to);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
			PJSIP_SC_UNSUPPORTED_URI_SCHEME, NULL, NULL, NULL);
		return PJ_TRUE;
	}
	remove_x_ast_params(rdata->msg_info.to->uri);

	contact = (pjsip_contact_hdr *) pjsip_msg_find_hdr(
		rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);

	if (!contact && pjsip_method_creates_dialog(&rdata->msg_info.msg->line.req.method)) {
		/* A contact header is required for dialog creating methods */
		static const pj_str_t missing_contact = { "Missing Contact header", 22 };
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 400,
				&missing_contact, NULL, NULL);
		return PJ_TRUE;
	}

	while (contact) {
		if (!contact->star && !is_sip_uri(contact->uri)) {
			print_uri_debug(URI_TYPE_CONTACT, rdata, (pjsip_hdr *)contact);
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
				PJSIP_SC_UNSUPPORTED_URI_SCHEME, NULL, NULL, NULL);
			return PJ_TRUE;
		}
		remove_x_ast_params(contact->uri);

		contact = (pjsip_contact_hdr *) pjsip_msg_find_hdr(
			rdata->msg_info.msg, PJSIP_H_CONTACT, contact->next);
	}

	return PJ_FALSE;
}

static pj_bool_t on_rx_process_symmetric_transport(pjsip_rx_data *rdata)
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

static pj_bool_t filter_on_rx_message(pjsip_rx_data *rdata)
{
	pj_bool_t rc;

	rc = on_rx_process_uris(rdata);
	if (rc == PJ_TRUE) {
		return rc;
	}

	rc = on_rx_process_symmetric_transport(rdata);
	if (rc == PJ_TRUE) {
		return rc;
	}

	return PJ_FALSE;
}

void ast_res_pjsip_cleanup_message_filter(void)
{
	ast_sip_unregister_service(&filter_module_tsx);
	ast_sip_unregister_service(&filter_module_transport);
	ast_sip_unregister_supplement(&filter_supplement);
	ast_sip_session_unregister_supplement(&filter_session_supplement);
}

int ast_res_pjsip_init_message_filter(void)
{
	ast_sip_session_register_supplement(&filter_session_supplement);
	ast_sip_register_supplement(&filter_supplement);

	if (ast_sip_register_service(&filter_module_transport)) {
		ast_log(LOG_ERROR, "Could not register message filter module for incoming and outgoing requests\n");
		ast_res_pjsip_cleanup_message_filter();
		return -1;
	}

	if (ast_sip_register_service(&filter_module_tsx)) {
		ast_log(LOG_ERROR, "Could not register message filter module for incoming and outgoing requests\n");
		ast_res_pjsip_cleanup_message_filter();
		return -1;
	}

	return 0;
}
