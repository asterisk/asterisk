/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"

/*! \brief Local host address for IPv4 */
static char host_ipv4[PJ_INET_ADDRSTRLEN + 2];

/*! \brief Local host address for IPv6 */
static char host_ipv6[PJ_INET6_ADDRSTRLEN + 2];

/*! \brief Helper function which returns a UDP transport bound to the given address and port */
static pjsip_transport *multihomed_get_udp_transport(pj_str_t *address, int port)
{
	struct ao2_container *transports = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "transport",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	struct ast_sip_transport *transport;
	struct ao2_iterator iter;
	pjsip_transport *sip_transport = NULL;

	if (!transports) {
		return NULL;
	}

	for (iter = ao2_iterator_init(transports, 0); (transport = ao2_iterator_next(&iter)); ao2_ref(transport, -1)) {
		if ((transport->type != AST_TRANSPORT_UDP) ||
			(pj_strcmp(&transport->state->transport->local_name.host, address)) ||
			(transport->state->transport->local_name.port != port)) {
			continue;
		}

		sip_transport = transport->state->transport;
		ao2_ref(transport, -1);
		break;
	}
	ao2_iterator_destroy(&iter);

	ao2_ref(transports, -1);

	return sip_transport;
}

/*! \brief Helper function which determines if the address within SDP should be rewritten */
static int multihomed_rewrite_sdp(struct pjmedia_sdp_session *sdp)
{
	if (!sdp->conn) {
		return 0;
	}

	/* If the host address is used in the SDP replace it with the address of what this is going out on */
	if ((!pj_strcmp2(&sdp->conn->addr_type, "IP4") && !pj_strcmp2(&sdp->conn->addr, host_ipv4)) ||
		(!pj_strcmp2(&sdp->conn->addr_type, "IP6") && !pj_strcmp2(&sdp->conn->addr, host_ipv6))) {
		return 1;
	}

	return 0;
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

static pj_status_t multihomed_on_tx_message(pjsip_tx_data *tdata)
{
	pjsip_tpmgr_fla2_param prm;
	pjsip_cseq_hdr *cseq;
	pjsip_via_hdr *via;

	/* Use the destination information to determine what local interface this message will go out on */
	pjsip_tpmgr_fla2_param_default(&prm);
	prm.tp_type = tdata->tp_info.transport->key.type;
	pj_strset2(&prm.dst_host, tdata->tp_info.dst_name);
	prm.local_if = PJ_TRUE;

	/* If we can't get the local address use best effort and let it pass */
	if (pjsip_tpmgr_find_local_addr2(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), tdata->pool, &prm) != PJ_SUCCESS) {
		return PJ_SUCCESS;
	}

	/* The port in the message should always be that of the original transport */
	prm.ret_port = tdata->tp_info.transport->local_name.port;

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

			pjsip_tx_data_invalidate_msg(tdata);
		}
	}

	if (tdata->msg->type == PJSIP_REQUEST_MSG && (via = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_VIA, NULL))) {
		pj_strassign(&via->sent_by.host, &prm.ret_addr);
		via->sent_by.port = prm.ret_port;

		pjsip_tx_data_invalidate_msg(tdata);
	}

	/* Update the SDP if it is present */
	if (tdata->msg->body && ast_sip_is_content_type(&tdata->msg->body->content_type, "application", "sdp") &&
		multihomed_rewrite_sdp(tdata->msg->body->data)) {
		struct pjmedia_sdp_session *sdp = tdata->msg->body->data;
		int stream;

		pj_strassign(&sdp->conn->addr, &prm.ret_addr);

		for (stream = 0; stream < sdp->media_count; ++stream) {
			if (sdp->media[stream]->conn) {
				pj_strassign(&sdp->media[stream]->conn->addr, &prm.ret_addr);
			}
		}

		pjsip_tx_data_invalidate_msg(tdata);
	}

	return PJ_SUCCESS;
}

static pjsip_module multihomed_module = {
	.name = { "Multihomed Routing", 18 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 1,
	.on_tx_request = multihomed_on_tx_message,
	.on_tx_response = multihomed_on_tx_message,
};

static int unload_module(void)
{
	ast_sip_unregister_service(&multihomed_module);
	return 0;
}

static int load_module(void)
{
	char hostname[MAXHOSTNAMELEN] = "";
	pj_sockaddr addr;

	CHECK_PJSIP_MODULE_LOADED();

	if (!gethostname(hostname, sizeof(hostname) - 1)) {
		ast_verb(2, "Performing DNS resolution of local hostname '%s' to get local IPv4 and IPv6 address\n",
			hostname);
	}

	if (!pj_gethostip(pj_AF_INET(), &addr)) {
		pj_sockaddr_print(&addr, host_ipv4, sizeof(host_ipv4), 2);
		ast_verb(3, "Local IPv4 address determined to be: %s\n", host_ipv4);
	}

	if (!pj_gethostip(pj_AF_INET6(), &addr)) {
		pj_sockaddr_print(&addr, host_ipv6, sizeof(host_ipv6), 2);
		ast_verb(3, "Local IPv6 address determined to be: %s\n", host_ipv6);
	}

	if (ast_sip_register_service(&multihomed_module)) {
		ast_log(LOG_ERROR, "Could not register multihomed module for incoming and outgoing requests\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Multihomed Routing Support",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
