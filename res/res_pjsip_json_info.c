/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Bradley Latus <brad.latus@gmail.com>
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
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"
#include "asterisk/json.h"
#include "asterisk/stasis_channels.h"

static void send_response(struct ast_sip_session *session,
		struct pjsip_rx_data *rdata, int code)
{
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg = session->inv_session->dlg;

	if (pjsip_dlg_create_response(dlg, rdata, code, NULL, &tdata) == PJ_SUCCESS) {
		struct pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);
		pjsip_dlg_send_response(dlg, tsx, tdata);
	}
}

static void send_json_received_event(struct ast_channel *chan, const char data)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	char data_str[] = { data, '\0' };

	blob = ast_json_pack("{ s: s, s: s }",
		"data", data_str,
		"direction", "Received");
	if (!blob) {
		return;
	}

	ast_channel_publish_cached_blob(chan, ast_channel_json_received_type(), blob);
}

static int is_json_type(pjsip_rx_data *rdata, char *subtype)
{
	return rdata->msg_info.ctype
		&& !pj_strcmp2(&rdata->msg_info.ctype->media.type, "application")
		&& !pj_strcmp2(&rdata->msg_info.ctype->media.subtype, subtype);
}

// static char get_data(const char *c)
// {
// 	blob = ast_json_pack("{s: s}", "data", c);
// }

static int json_info_incoming_request(struct ast_sip_session *session,
		struct pjsip_rx_data *rdata)
{
	pjsip_msg_body *body = rdata->msg_info.msg->body;
	char buf[body ? body->len + 1 : 1];
	char *cur = buf;
	// char event = '\0';
	int res;

	if (!session->channel) {
		return 0;
	}

	if (!is_json_type(rdata, "json")) {
		/* Let another module respond */
		return 0;
	}

	if (!body || !body->len) {
		/* need to return 200 OK on empty body */
		send_response(session, rdata, 200);
		return 1;
	}

	res = body->print_body(body, buf, body->len);
	if (res < 0) {
		send_response(session, rdata, 500);
		return 1;
	}
	buf[res] = '\0';

	ast_verb(3, "<%s> SIP INFO application/json message received: %s\n", ast_channel_name(session->channel), cur);
	send_json_received_event(session->channel, cur);

	/* Need to return 200 OK */
	send_response(session, rdata, 200);
	return 1;

}

static struct ast_sip_session_supplement json_info_supplement = {
	.method = "INFO",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST,
	.incoming_request = json_info_incoming_request,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&json_info_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&json_info_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP JSON INFO Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
