/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jason Parker <jparker@digium.com>
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_sip.h"
#include "asterisk/res_sip_session.h"
#include "asterisk/module.h"

static int dtmf_info_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	int res = 0;
	pjsip_msg_body *body = rdata->msg_info.msg->body;

	pjsip_tx_data *tdata;

	char buf[body->len];
	char *cur = buf;
	char *line;

	char event = '\0';
	unsigned int duration = 0;

	if (pj_strcmp2(&body->content_type.type, "application") ||
	    pj_strcmp2(&body->content_type.subtype, "dtmf-relay")) {
		return 0;
	}

	body->print_body(body, buf, body->len);

	while ((line = strsep(&cur, "\r\n"))) {
		char *c;

		if (!(c = strchr(line, '='))) {
			continue;
		}
		*c++ = '\0';

		c = ast_skip_blanks(c);

		if (!strcasecmp(line, "signal")) {
			if (c[0] == '!' || c[0] == '*' || c[0] == '#' ||
			    ('0' <= c[0] && c[0] <= '9') ||
			    ('A' <= c[0] && c[0] <= 'D') ||
			    ('a' <= c[0] && c[0] <= 'd')) {
				event = c[0];
			} else {
				ast_log(LOG_ERROR, "Invalid DTMF event signal in INFO message.\n");
				res = -1;
				break;
			}
		} else if (!strcasecmp(line, "duration")) {
			sscanf(c, "%30u", &duration);
		}
	}

	if (!duration) {
		duration = 100;
	}

	if (event == '!') {
		struct ast_frame f = { AST_FRAME_CONTROL, { AST_CONTROL_FLASH, } };

		ast_queue_frame(session->channel, &f);
	} else if (event != '\0') {
		struct ast_frame f = { AST_FRAME_DTMF, };
		f.len = duration;
		f.subclass.integer = event;

		ast_queue_frame(session->channel, &f);
	} else {
		res = -1;
	}

	if (pjsip_dlg_create_response(session->inv_session->dlg, rdata, !res ? 200 : 500, NULL, &tdata) == PJ_SUCCESS) {
		struct pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);

		pjsip_dlg_send_response(session->inv_session->dlg, tsx, tdata);
	}

	return res;
}

static struct ast_sip_session_supplement dtmf_info_supplement = {
	.method = "INFO",
	.incoming_request = dtmf_info_incoming_request,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&dtmf_info_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&dtmf_info_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP DTMF INFO Support",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
