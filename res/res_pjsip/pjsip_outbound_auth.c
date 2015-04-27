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

#include "asterisk.h"
#undef bzero
#define bzero bzero
#include "pjsip.h"

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "include/res_pjsip_private.h"

static pj_bool_t outbound_auth(pjsip_rx_data *rdata);

static pjsip_module outbound_auth_mod = {
	.name = {"Outbound Authentication", 19},
	.priority = PJSIP_MOD_PRIORITY_DIALOG_USAGE,
	.on_rx_response = outbound_auth,
};

struct outbound_auth_cb_data {
	ast_sip_dialog_outbound_auth_cb cb;
	void *user_data;
};

static pj_bool_t outbound_auth(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	pjsip_transaction *tsx;
	pjsip_dialog *dlg;
	struct outbound_auth_cb_data *cb_data;
	pjsip_tx_data *tdata;

	if (rdata->msg_info.msg->line.status.code != 401 &&
			rdata->msg_info.msg->line.status.code != 407) {
		/* Doesn't pertain to us. Move on */
		return PJ_FALSE;
	}

	tsx = pjsip_rdata_get_tsx(rdata);
	dlg = pjsip_rdata_get_dlg(rdata);
	if (!dlg || !tsx) {
		return PJ_FALSE;
	}

	endpoint = ast_sip_dialog_get_endpoint(dlg);
	if (!endpoint) {
		return PJ_FALSE;
	}

	if (ast_sip_create_request_with_auth(&endpoint->outbound_auths, rdata, tsx->last_tx, &tdata)) {
		return PJ_FALSE;
	}

	cb_data = dlg->mod_data[outbound_auth_mod.id];
	if (cb_data) {
		cb_data->cb(dlg, tdata, cb_data->user_data);
		return PJ_TRUE;
	}

	pjsip_dlg_send_request(dlg, tdata, -1, NULL);
	return PJ_TRUE;
}

int ast_sip_dialog_setup_outbound_authentication(pjsip_dialog *dlg, const struct ast_sip_endpoint *endpoint,
		ast_sip_dialog_outbound_auth_cb cb, void *user_data)
{
	struct outbound_auth_cb_data *cb_data = PJ_POOL_ZALLOC_T(dlg->pool, struct outbound_auth_cb_data);
	cb_data->cb = cb;
	cb_data->user_data = user_data;

	dlg->sess_count++;
	pjsip_dlg_add_usage(dlg, &outbound_auth_mod, cb_data);
	dlg->sess_count--;

	return 0;
}

int internal_sip_initialize_outbound_authentication(void) {
	return internal_sip_register_service(&outbound_auth_mod);
}


void internal_sip_destroy_outbound_authentication(void) {
	internal_sip_unregister_service(&outbound_auth_mod);
}
