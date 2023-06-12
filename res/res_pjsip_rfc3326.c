/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"
#include "asterisk/causes.h"
#include "asterisk/threadpool.h"

static void rfc3326_use_reason_header(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	static const pj_str_t str_reason = { "Reason", 6 };
	pjsip_generic_string_hdr *header;
	char buf[20];
	char *cause;
	int code_q850 = 0, code_sip = 0;

	header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_reason, NULL);
	for (; header;
		header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_reason, header->next)) {
		int cause_q850, cause_sip;
		ast_copy_pj_str(buf, &header->hvalue, sizeof(buf));
		cause = ast_skip_blanks(buf);

		cause_q850 = !strncasecmp(cause, "Q.850", 5);
		cause_sip = !strncasecmp(cause, "SIP", 3);
		if ((cause_q850 || cause_sip) && (cause = strstr(cause, "cause="))) {
			int *code = cause_q850 ? &code_q850 : &code_sip;
			if (sscanf(cause, "cause=%30d", code) != 1) {
				*code = 0;
			}
		}
	}

	if (code_q850) {
		ast_channel_hangupcause_set(session->channel, code_q850 & 0x7f);
	} else if (code_sip) {
		ast_channel_hangupcause_set(session->channel, ast_sip_hangup_sip2cause(code_sip));
	}
}

static int rfc3326_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	if ((pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_bye_method) &&
	     pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_cancel_method)) ||
	    !session->channel) {
		return 0;
	}

	rfc3326_use_reason_header(session, rdata);

	return 0;
}

static void rfc3326_incoming_response(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;

	if ((status.code < 300) || !session->channel) {
		return;
	}

	rfc3326_use_reason_header(session, rdata);
}

static void rfc3326_add_reason_header(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	char buf[20];

	if (ast_channel_hangupcause(session->channel) == AST_CAUSE_ANSWERED_ELSEWHERE) {
		ast_sip_add_header(tdata, "Reason", "SIP;cause=200;text=\"Call completed elsewhere\"");
	}

	if (session->endpoint && session->endpoint->suppress_q850_reason_headers) {
		ast_debug(1, "A Q.850 '%s'(%i) Reason header was suppressed for endpoint '%s'\n",
			ast_cause2str((ast_channel_hangupcause(session->channel) & 0x7f)),
			(ast_channel_hangupcause(session->channel) & 0x7f),
			ast_sorcery_object_get_id(session->endpoint));
	} else {
		snprintf(buf, sizeof(buf), "Q.850;cause=%i", ast_channel_hangupcause(session->channel) & 0x7f);
		ast_sip_add_header(tdata, "Reason", buf);
	}
}

static void rfc3326_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	if ((pjsip_method_cmp(&tdata->msg->line.req.method, &pjsip_bye_method)
			&& pjsip_method_cmp(&tdata->msg->line.req.method, &pjsip_cancel_method))
		|| !session->channel
		/*
		 * The session->channel has been seen to go away on us between
		 * checks so we must also be running under the call's serializer
		 * thread.
		 */
		|| session->serializer != ast_threadpool_serializer_get_current()) {
		return;
	}

	rfc3326_add_reason_header(session, tdata);
}

static void rfc3326_outgoing_response(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	struct pjsip_status_line status = tdata->msg->line.status;

	if (status.code < 300
		|| !session->channel
		|| session->serializer != ast_threadpool_serializer_get_current()) {
		return;
	}

	rfc3326_add_reason_header(session, tdata);
}

static struct ast_sip_session_supplement rfc3326_supplement = {
	.incoming_request = rfc3326_incoming_request,
	.incoming_response = rfc3326_incoming_response,
	.outgoing_request = rfc3326_outgoing_request,
	.outgoing_response = rfc3326_outgoing_response,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&rfc3326_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&rfc3326_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP RFC3326 Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
