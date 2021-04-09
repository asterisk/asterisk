/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2014, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief PJSIP logging with Homer
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<depend>res_hep</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_hep.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"

static char *assign_uuid(const pj_str_t *call_id, const pj_str_t *local_tag, const pj_str_t *remote_tag)
{
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	pjsip_dialog *dlg;
	char *uuid = NULL;
	enum hep_uuid_type uuid_type = hepv3_get_uuid_type();

	if ((uuid_type == HEP_UUID_TYPE_CHANNEL)
		&& (dlg = pjsip_ua_find_dialog(call_id, local_tag, remote_tag, PJ_FALSE))
	    && (session = ast_sip_dialog_get_session(dlg))
	    && (session->channel)) {

		uuid = ast_strdup(ast_channel_name(session->channel));
	}

	/* If we couldn't get the channel or we never wanted it, default to the call-id */
	if (!uuid) {

		uuid = ast_malloc(pj_strlen(call_id) + 1);
		if (uuid) {
			ast_copy_pj_str(uuid, call_id, pj_strlen(call_id) + 1);
		}
	}

	return uuid;
}

static int transport_to_protocol_id(pjsip_transport *tp)
{
	/* XXX If we ever add SCTP support, we'll need to revisit */
	if (tp->flag & PJSIP_TRANSPORT_RELIABLE) {
		return IPPROTO_TCP;
	}
	return IPPROTO_UDP;
}

static pj_status_t logging_on_tx_msg(pjsip_tx_data *tdata)
{
	char local_buf[256];
	char remote_buf[256];
	char *uuid;
	struct hepv3_capture_info *capture_info;
	pjsip_cid_hdr *cid_hdr;
	pjsip_from_hdr *from_hdr;
	pjsip_to_hdr *to_hdr;

	capture_info = hepv3_create_capture_info(tdata->buf.start, (size_t)(tdata->buf.cur - tdata->buf.start));
	if (!capture_info) {
		return PJ_SUCCESS;
	}

	if (!(tdata->tp_info.transport->flag & PJSIP_TRANSPORT_RELIABLE)) {
		pjsip_tpmgr_fla2_param prm;

		/* Attempt to determine what IP address will we send this packet out of */
		pjsip_tpmgr_fla2_param_default(&prm);
		prm.tp_type = tdata->tp_info.transport->key.type;
		pj_strset2(&prm.dst_host, tdata->tp_info.dst_name);
		prm.local_if = PJ_TRUE;

		/* If we can't get the local address use what we have already */
		if (pjsip_tpmgr_find_local_addr2(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), tdata->pool, &prm) != PJ_SUCCESS) {
			pj_sockaddr_print(&tdata->tp_info.transport->local_addr, local_buf, sizeof(local_buf), 3);
		} else {
			if (prm.tp_type & PJSIP_TRANSPORT_IPV6) {
				snprintf(local_buf, sizeof(local_buf), "[%.*s]:%hu",
					(int)pj_strlen(&prm.ret_addr),
					pj_strbuf(&prm.ret_addr),
					prm.ret_port);
			} else {
				snprintf(local_buf, sizeof(local_buf), "%.*s:%hu",
					(int)pj_strlen(&prm.ret_addr),
					pj_strbuf(&prm.ret_addr),
					prm.ret_port);
			}
		}
	} else {
		/* For reliable transports they can only ever come from the transport
		 * local address.
		 */
		pj_sockaddr_print(&tdata->tp_info.transport->local_addr, local_buf, sizeof(local_buf), 3);
	}

	pj_sockaddr_print(&tdata->tp_info.dst_addr, remote_buf, sizeof(remote_buf), 3);

	cid_hdr = PJSIP_MSG_CID_HDR(tdata->msg);
	from_hdr = PJSIP_MSG_FROM_HDR(tdata->msg);
	to_hdr = PJSIP_MSG_TO_HDR(tdata->msg);

	uuid = assign_uuid(&cid_hdr->id, &to_hdr->tag, &from_hdr->tag);
	if (!uuid) {
		ao2_ref(capture_info, -1);
		return PJ_SUCCESS;
	}

	ast_sockaddr_parse(&capture_info->src_addr, local_buf, PARSE_PORT_REQUIRE);
	ast_sockaddr_parse(&capture_info->dst_addr, remote_buf, PARSE_PORT_REQUIRE);

	capture_info->protocol_id = transport_to_protocol_id(tdata->tp_info.transport);
	capture_info->capture_time = ast_tvnow();
	capture_info->capture_type = HEPV3_CAPTURE_TYPE_SIP;
	capture_info->uuid = uuid;
	capture_info->zipped = 0;

	hepv3_send_packet(capture_info);

	return PJ_SUCCESS;
}

static pj_bool_t logging_on_rx_msg(pjsip_rx_data *rdata)
{
	char local_buf[256];
	char remote_buf[256];
	char *uuid;
	struct hepv3_capture_info *capture_info;

	capture_info = hepv3_create_capture_info(&rdata->pkt_info.packet, rdata->pkt_info.len);
	if (!capture_info) {
		return PJ_SUCCESS;
	}

	if (!rdata->pkt_info.src_addr_len) {
		return PJ_SUCCESS;
	}
	pj_sockaddr_print(&rdata->pkt_info.src_addr, remote_buf, sizeof(remote_buf), 3);

	if (!(rdata->tp_info.transport->flag & PJSIP_TRANSPORT_RELIABLE)) {
		pjsip_tpmgr_fla2_param prm;

		/* Attempt to determine what IP address we probably received this packet on */
		pjsip_tpmgr_fla2_param_default(&prm);
		prm.tp_type = rdata->tp_info.transport->key.type;
		pj_strset2(&prm.dst_host, rdata->pkt_info.src_name);
		prm.local_if = PJ_TRUE;

		/* If we can't get the local address use what we have already */
		if (pjsip_tpmgr_find_local_addr2(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), rdata->tp_info.pool, &prm) != PJ_SUCCESS) {
			pj_sockaddr_print(&rdata->tp_info.transport->local_addr, local_buf, sizeof(local_buf), 3);
		} else {
			if (prm.tp_type & PJSIP_TRANSPORT_IPV6) {
				snprintf(local_buf, sizeof(local_buf), "[%.*s]:%hu",
					(int)pj_strlen(&prm.ret_addr),
					pj_strbuf(&prm.ret_addr),
					prm.ret_port);
			} else {
				snprintf(local_buf, sizeof(local_buf), "%.*s:%hu",
					(int)pj_strlen(&prm.ret_addr),
					pj_strbuf(&prm.ret_addr),
					prm.ret_port);
			}
		}
	} else {
		pj_sockaddr_print(&rdata->tp_info.transport->local_addr, local_buf, sizeof(local_buf), 3);
	}

	uuid = assign_uuid(&rdata->msg_info.cid->id, &rdata->msg_info.to->tag, &rdata->msg_info.from->tag);
	if (!uuid) {
		ao2_ref(capture_info, -1);
		return PJ_SUCCESS;
	}

	ast_sockaddr_parse(&capture_info->src_addr, remote_buf, PARSE_PORT_REQUIRE);
	ast_sockaddr_parse(&capture_info->dst_addr, local_buf, PARSE_PORT_REQUIRE);

	capture_info->protocol_id = transport_to_protocol_id(rdata->tp_info.transport);
	capture_info->capture_time.tv_sec = rdata->pkt_info.timestamp.sec;
	capture_info->capture_time.tv_usec = rdata->pkt_info.timestamp.msec * 1000;
	capture_info->capture_type = HEPV3_CAPTURE_TYPE_SIP;
	capture_info->uuid = uuid;
	capture_info->zipped = 0;

	hepv3_send_packet(capture_info);

	return PJ_FALSE;
}

static pjsip_module logging_module = {
	.name = { "HEPv3 Logging Module", 20 },
	.priority = 0,
	.on_rx_request = logging_on_rx_msg,
	.on_rx_response = logging_on_rx_msg,
	.on_tx_request = logging_on_tx_msg,
	.on_tx_response = logging_on_tx_msg,
};


static int load_module(void)
{
	if (!hepv3_is_loaded()) {
		ast_log(AST_LOG_WARNING, "res_hep is disabled; declining module load\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sip_register_service(&logging_module);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&logging_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "PJSIP HEPv3 Logger",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip,res_pjsip_session,res_hep",
);
