/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Yaron Nahum <nachum.yaron@gmail.com>
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

ASTERISK_REGISTER_FILE()

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/module.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

#define DEFAULT_LANGUAGE "en"
#define DEFAULT_ENCODING "text/plain"

static int options_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	pjsip_tx_data *tdata;
        pj_status_t status;
	const pjsip_hdr *hdr;
	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();

	status = pjsip_dlg_create_response(session->inv_session->dlg, rdata, 200, NULL,&tdata);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create response (%d)\n", status);
		return status;
	}

	/* Add appropriate headers */
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_ACCEPT, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_ALLOW, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_SUPPORTED, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}

	/*
	 * XXX TODO: pjsip doesn't care a lot about either of these headers -
	 * while it provides specific methods to create them, they are defined
	 * to be the standard string header creation. We never did add them
	 * in chan_sip, although RFC 3261 says they SHOULD. Hard coded here.
	*/
	ast_sip_add_header(tdata, "Accept-Encoding", DEFAULT_ENCODING);
	ast_sip_add_header(tdata, "Accept-Language", DEFAULT_LANGUAGE);

	status = pjsip_dlg_send_response(session->inv_session->dlg, pjsip_rdata_get_tsx(rdata), tdata);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to send response (%d)\n", status);
	}

	return status;
}

static struct ast_sip_session_supplement  dlg_options_supplement = {
	.method = "OPTIONS",
	.incoming_request = options_incoming_request,
};

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	if (ast_sip_session_register_supplement(&dlg_options_supplement)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&dlg_options_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP OPTIONS in dialog handler",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
