/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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

/*! \file
 *
 * \brief Module for managing send to voicemail requests in SIP
 *        REFER messages against PJSIP channels
 *
 * \author Jonathan Rose <jrose@digium.com>
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

#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"

#define DATASTORE_NAME "call_feature_send_to_vm_datastore"

#define SEND_TO_VM_HEADER "PJSIP_HEADER(add,X-Digium-Call-Feature)"
#define SEND_TO_VM_HEADER_VALUE "feature_send_to_vm"

#define SEND_TO_VM_REDIRECT "REDIRECTING(reason)"
#define SEND_TO_VM_REDIRECT_VALUE "\"send_to_vm\""

static void send_response(struct ast_sip_session *session, int code, struct pjsip_rx_data *rdata)
{
	pjsip_tx_data *tdata;

	if (pjsip_dlg_create_response(session->inv_session->dlg, rdata, code, NULL, &tdata) == PJ_SUCCESS) {
		struct pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);

		pjsip_dlg_send_response(session->inv_session->dlg, tsx, tdata);
	}
}

static void channel_cleanup_wrapper(void *data)
{
	struct ast_channel *chan = data;
	ast_channel_cleanup(chan);
}

static struct ast_datastore_info call_feature_info = {
	.type = "REFER call feature info",
	.destroy = channel_cleanup_wrapper,
};

static pjsip_param *get_diversion_reason(pjsip_fromto_hdr *hdr)
{
	static const pj_str_t reason_str = { "reason", 6 };
	return pjsip_param_find(&hdr->other_param, &reason_str);
}

static pjsip_fromto_hdr *get_diversion_header(pjsip_rx_data *rdata)
{
	static const pj_str_t from_str = { "From", 4 };
	static const pj_str_t diversion_str = { "Diversion", 9 };

	pjsip_generic_string_hdr *hdr;
	pj_str_t value;

	if (!(hdr = pjsip_msg_find_hdr_by_name(
		      rdata->msg_info.msg, &diversion_str, NULL))) {
		return NULL;
	}

	pj_strdup_with_null(rdata->tp_info.pool, &value, &hdr->hvalue);

	/* parse as a fromto header */
	return pjsip_parse_hdr(rdata->tp_info.pool, &from_str, value.ptr,
			       pj_strlen(&value), NULL);
}

static int has_diversion_reason(pjsip_rx_data *rdata)
{
	pjsip_param *reason;
	pjsip_fromto_hdr *hdr = get_diversion_header(rdata);

	return hdr &&
		(reason = get_diversion_reason(hdr)) &&
		!pj_stricmp2(&reason->value, SEND_TO_VM_REDIRECT_VALUE);
}

static int has_call_feature(pjsip_rx_data *rdata)
{
	static const pj_str_t call_feature_str = { "X-Digium-Call-Feature", 21 };

	pjsip_generic_string_hdr *hdr = pjsip_msg_find_hdr_by_name(
		rdata->msg_info.msg, &call_feature_str, NULL);

	return hdr && !pj_stricmp2(&hdr->hvalue, SEND_TO_VM_HEADER_VALUE);
}

static int handle_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	struct ast_datastore *sip_session_datastore;
	struct ast_channel *other_party;
	int has_feature;
	int has_reason;

	if (!session->channel) {
		return 0;
	}

	has_feature = has_call_feature(rdata);
	has_reason = has_diversion_reason(rdata);
	if (!has_feature && !has_reason) {
		/* If we don't have a call feature or diversion reason or if
		   it's not a feature this module is related to then there
		   is nothing to do. */
		return 0;
	}

	/* Check bridge status... */
	other_party = ast_channel_bridge_peer(session->channel);
	if (!other_party) {
		/* The channel wasn't in a two party bridge */
		ast_log(LOG_WARNING, "%s (%s) attempted to transfer to voicemail, "
			"but was not in a two party bridge.\n",
			ast_sorcery_object_get_id(session->endpoint),
			ast_channel_name(session->channel));
		send_response(session, 400, rdata);
		return -1;
	}

	sip_session_datastore = ast_sip_session_alloc_datastore(
		&call_feature_info, DATASTORE_NAME);
	if (!sip_session_datastore) {
		ast_channel_unref(other_party);
		send_response(session, 500, rdata);
		return -1;
	}

	sip_session_datastore->data = other_party;

	if (ast_sip_session_add_datastore(session, sip_session_datastore)) {
		ast_channel_unref(other_party);
		ao2_ref(sip_session_datastore, -1);
		send_response(session, 500, rdata);
		return -1;
	}
	ao2_ref(sip_session_datastore, -1);

	if (has_feature) {
		pbx_builtin_setvar_helper(other_party, SEND_TO_VM_HEADER,
					  SEND_TO_VM_HEADER_VALUE);
	}

	if (has_reason) {
		pbx_builtin_setvar_helper(other_party, SEND_TO_VM_REDIRECT,
					  SEND_TO_VM_REDIRECT_VALUE);
	}

	return 0;
}

static void handle_outgoing_response(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	pjsip_status_line status = tdata->msg->line.status;
	struct ast_datastore *feature_datastore =
		ast_sip_session_get_datastore(session, DATASTORE_NAME);
	struct ast_channel *target_chan;

	if (!feature_datastore) {
		return;
	}

	/* Since we are handling the response, there is no need to keep the datastore in the session anymore. */
	ast_sip_session_remove_datastore(session, DATASTORE_NAME);

	/* If the response >= 300, the refer failed and we need to clear the feature. */
	if (status.code >= 300) {
		target_chan = feature_datastore->data;
		pbx_builtin_setvar_helper(target_chan, SEND_TO_VM_HEADER, NULL);
		pbx_builtin_setvar_helper(target_chan, SEND_TO_VM_REDIRECT, NULL);
	}
	ao2_ref(feature_datastore, -1);
}

static struct ast_sip_session_supplement refer_supplement = {
	.method = "REFER",
	.incoming_request = handle_incoming_request,
	.outgoing_response = handle_outgoing_response,
};

static int load_module(void)
{
	CHECK_PJSIP_SESSION_MODULE_LOADED();

	if (ast_sip_session_register_supplement(&refer_supplement)) {
		ast_log(LOG_ERROR, "Unable to register Send to Voicemail supplement\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&refer_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP REFER Send to Voicemail Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	);
