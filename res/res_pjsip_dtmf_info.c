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
	<load_priority>app_depend</load_priority>
	<depend>pjproject</depend>
	<use type="module">res_pjsip</use>
	<use type="module">res_pjsip_session</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"

static int is_media_type(pjsip_rx_data *rdata, char *subtype)
{
	return rdata->msg_info.ctype
		&& !pj_strcmp2(&rdata->msg_info.ctype->media.type, "application")
		&& !pj_strcmp2(&rdata->msg_info.ctype->media.subtype, subtype);
}

static void send_response(struct ast_sip_session *session,
			  struct pjsip_rx_data *rdata, int code)
{
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg = session->inv_session->dlg;

	if (pjsip_dlg_create_response(dlg, rdata, code,
				      NULL, &tdata) == PJ_SUCCESS) {
		struct pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);
		pjsip_dlg_send_response(dlg, tsx, tdata);
	}
}

static char get_event(const char *c)
{
	unsigned int event;

	if (*c == '!' || *c == '*' || *c == '#' ||
	    ('A' <= *c && *c <= 'D') ||
	    ('a' <= *c && *c <= 'd')) {
		return *c;
	}

	if ((sscanf(c, "%30u", &event) != 1) || event > 16) {
		return '\0';
	}

	if (event < 10) {
		return *c;
	}

	switch (event) {
	case 10: return '*';
	case 11: return '#';
	case 16: return '!';
	}

	return 'A' + (event - 12);
}

static int dtmf_info_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	pjsip_msg_body *body = rdata->msg_info.msg->body;
	char buf[body ? body->len : 0];
	char *cur = buf;
	char *line;

	char event = '\0';
	unsigned int duration = 100;

	char is_dtmf = is_media_type(rdata, "dtmf");

	if (!is_dtmf && !is_media_type(rdata, "dtmf-relay")) {
		return 0;
	}

	if (!body || !body->len) {
		/* need to return 200 OK on empty body */
		send_response(session, rdata, 200);
		return 0;
	}

	body->print_body(body, buf, body->len);

	if (is_dtmf) {
		/* directly use what is in the message body */
		event = get_event(cur);
	} else { /* content type = application/dtmf-relay */
		while ((line = strsep(&cur, "\r\n"))) {
			char *c;

			if (!(c = strchr(line, '='))) {
				continue;
			}

			*c++ = '\0';
			c = ast_skip_blanks(c);

			if (!strcasecmp(line, "signal")) {
				if (!(event = get_event(c))) {
					break;
				}
			} else if (!strcasecmp(line, "duration")) {
				sscanf(c, "%30u", &duration);
			}
		}
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
		ast_log(LOG_ERROR, "Invalid DTMF event signal in INFO message.\n");
	}

	send_response(session, rdata, event ? 200 : 500);
	return event ? 0 : -1;
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

static void unload_module(void)
{
	ast_sip_session_unregister_supplement(&dtmf_info_supplement);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "PJSIP DTMF INFO Support");
