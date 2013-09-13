/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
	<defaultenabled>no</defaultenabled>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"

static pj_status_t logging_on_tx_msg(pjsip_tx_data *tdata)
{
	ast_verbose("<--- Transmitting SIP %s (%d bytes) to %s:%s:%d --->\n%.*s\n",
		    tdata->msg->type == PJSIP_REQUEST_MSG ? "request" : "response",
		    (int) (tdata->buf.cur - tdata->buf.start),
		    tdata->tp_info.transport->type_name,
		    tdata->tp_info.dst_name,
		    tdata->tp_info.dst_port,
		    (int) (tdata->buf.end - tdata->buf.start), tdata->buf.start);
	return PJ_SUCCESS;
}

static pj_bool_t logging_on_rx_msg(pjsip_rx_data *rdata)
{
	ast_verbose("<--- Received SIP %s (%d bytes) from %s:%s:%d --->\n%s\n",
		    rdata->msg_info.msg->type == PJSIP_REQUEST_MSG ? "request" : "response",
		    rdata->msg_info.len,
		    rdata->tp_info.transport->type_name,
		    rdata->pkt_info.src_name,
		    rdata->pkt_info.src_port,
		    rdata->pkt_info.packet);
	return PJ_FALSE;
}

static pjsip_module logging_module = {
	.name = { "Logging Module", 14 },
	.priority = 0,
	.on_rx_request = logging_on_rx_msg,
	.on_rx_response = logging_on_rx_msg,
	.on_tx_request = logging_on_tx_msg,
	.on_tx_response = logging_on_tx_msg,
};

static int load_module(void)
{
	ast_sip_register_service(&logging_module);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&logging_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Packet Logger",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
