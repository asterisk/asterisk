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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

	if ((dlg = pjsip_ua_find_dialog(call_id, local_tag, remote_tag, PJ_FALSE))
	    && (session = ast_sip_dialog_get_session(dlg))
	    && (session->channel)) {

		uuid = ast_strdup(ast_channel_name(session->channel));
	} else {

		uuid = ast_malloc(pj_strlen(call_id) + 1);
		if (uuid) {
			ast_copy_pj_str(uuid, call_id, pj_strlen(call_id) + 1);
		}
	}

	return uuid;
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

	pj_sockaddr_print(&tdata->tp_info.transport->local_addr, local_buf, sizeof(local_buf), 3);
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

	if (rdata->tp_info.transport->addr_len) {
		pj_sockaddr_print(&rdata->tp_info.transport->local_addr, local_buf, sizeof(local_buf), 3);
	}
	if (rdata->pkt_info.src_addr_len) {
		pj_sockaddr_print(&rdata->pkt_info.src_addr, remote_buf, sizeof(remote_buf), 3);
	}

	uuid = assign_uuid(&rdata->msg_info.cid->id, &rdata->msg_info.to->tag, &rdata->msg_info.from->tag);
	if (!uuid) {
		ao2_ref(capture_info, -1);
		return PJ_SUCCESS;
	}

	ast_sockaddr_parse(&capture_info->src_addr, remote_buf, PARSE_PORT_REQUIRE);
	ast_sockaddr_parse(&capture_info->dst_addr, local_buf, PARSE_PORT_REQUIRE);
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
	CHECK_PJSIP_MODULE_LOADED();

	ast_sip_register_service(&logging_module);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&logging_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP HEPv3 Logger",
		.support_level = AST_MODULE_SUPPORT_EXTENDED,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_DEFAULT,
	       );
