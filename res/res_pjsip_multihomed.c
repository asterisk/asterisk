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

static pj_status_t multihomed_on_tx_message(pjsip_tx_data *tdata)
{
	pjsip_tpmgr_fla2_param prm;
	pjsip_transport *transport;
	pjsip_contact_hdr *contact;
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

	/* If the transport it is going out on is different reflect it in the message */
	transport = multihomed_get_udp_transport(&prm.ret_addr, prm.ret_port);
	if (transport && (tdata->tp_info.transport != transport)) {
		tdata->tp_info.transport = transport;
	}

	/* If the message needs to be updated with new address do so */
	contact = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL);
	if (contact && (PJSIP_URI_SCHEME_IS_SIP(contact->uri) || PJSIP_URI_SCHEME_IS_SIPS(contact->uri))) {
		pjsip_sip_uri *uri = pjsip_uri_get_uri(contact->uri);

		/* prm.ret_addr is allocated from the tdata pool so it is perfectly fine to just do an assignment like this */
		pj_strassign(&uri->host, &prm.ret_addr);
		uri->port = prm.ret_port;

		pjsip_tx_data_invalidate_msg(tdata);
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
	pj_sockaddr addr;

	if (!pj_gethostip(pj_AF_INET(), &addr)) {
		pj_sockaddr_print(&addr, host_ipv4, sizeof(host_ipv4), 2);
	}

	if (!pj_gethostip(pj_AF_INET6(), &addr)) {
		pj_sockaddr_print(&addr, host_ipv6, sizeof(host_ipv6), 2);
	}

	if (ast_sip_register_service(&multihomed_module)) {
		ast_log(LOG_ERROR, "Could not register multihomed module for incoming and outgoing requests\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Multihomed Routing Support",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
