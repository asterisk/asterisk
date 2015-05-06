/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, malleable, llc.
 *
 * Sean Bright <sean@malleable.com>
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

#include "asterisk/features.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"
#include "asterisk/features_config.h"

static void send_response(struct ast_sip_session *session, int code, struct pjsip_rx_data *rdata)
{
	pjsip_tx_data *tdata;

	if (pjsip_dlg_create_response(session->inv_session->dlg, rdata, code, NULL, &tdata) == PJ_SUCCESS) {
		struct pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);

		pjsip_dlg_send_response(session->inv_session->dlg, tsx, tdata);
	}
}

static int handle_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	static const pj_str_t rec_str = { "Record", 6 };
	pjsip_generic_string_hdr *record;
	int feature_res;
	char feature_code[AST_FEATURE_MAX_LEN];
	const char *feature;
	char *digit;

	record = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &rec_str, NULL);

	/* If we don't have Record header, we have nothing to do */
	if (!record) {
		return 0;
	}

	if (!pj_stricmp2(&record->hvalue, "on")) {
		feature = session->endpoint->info.recording.onfeature;
	} else if (!pj_stricmp2(&record->hvalue, "off")) {
		feature = session->endpoint->info.recording.offfeature;
	} else {
		/* Don't send response because another module may handle this */
		return 0;
	}

	if (!session->channel) {
		send_response(session, 481, rdata);
		return 0;
	}

	/* Is this endpoint configured with One Touch Recording? */
	if (!session->endpoint->info.recording.enabled || ast_strlen_zero(feature)) {
		send_response(session, 403, rdata);
		return 0;
	}

	ast_channel_lock(session->channel);
	feature_res = ast_get_feature(session->channel, feature, feature_code, sizeof(feature_code));
	ast_channel_unlock(session->channel);

	if (feature_res || ast_strlen_zero(feature_code)) {
		send_response(session, 403, rdata);
		return 0;
	}

	for (digit = feature_code; *digit; ++digit) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = *digit, .len = 100 };
		ast_queue_frame(session->channel, &f);
	}

	send_response(session, 200, rdata);

	return 0;
}

static struct ast_sip_session_supplement info_supplement = {
	.method = "INFO",
	.incoming_request = handle_incoming_request,
};

static int load_module(void)
{
	CHECK_PJSIP_SESSION_MODULE_LOADED();

	if (ast_sip_session_register_supplement(&info_supplement)) {
		ast_log(LOG_ERROR, "Unable to register One Touch Recording supplement\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&info_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP INFO One Touch Recording Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
