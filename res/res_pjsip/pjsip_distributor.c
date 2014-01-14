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

#include <pjsip.h>

#include "asterisk/res_pjsip.h"

static int distribute(void *data);
static pj_bool_t distributor(pjsip_rx_data *rdata);

static pjsip_module distributor_mod = {
	.name = {"Request Distributor", 19},
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 6,
	.on_rx_request = distributor,
	.on_rx_response = distributor,
};

/*! Dialog-specific information the distributor uses */
struct distributor_dialog_data {
	/* Serializer to distribute tasks to for this dialog */
	struct ast_taskprocessor *serializer;
	/* Endpoint associated with this dialog */
	struct ast_sip_endpoint *endpoint;
};

/*!
 * \internal
 *
 * \note Call this with the dialog locked
 */
static struct distributor_dialog_data *distributor_dialog_data_alloc(pjsip_dialog *dlg)
{
	struct distributor_dialog_data *dist;

	dist = PJ_POOL_ZALLOC_T(dlg->pool, struct distributor_dialog_data);
	pjsip_dlg_set_mod_data(dlg, distributor_mod.id, dist);

	return dist;
}

void ast_sip_dialog_set_serializer(pjsip_dialog *dlg, struct ast_taskprocessor *serializer)
{
	struct distributor_dialog_data *dist;
	SCOPED_LOCK(lock, dlg, pjsip_dlg_inc_lock, pjsip_dlg_dec_lock);

	dist = pjsip_dlg_get_mod_data(dlg, distributor_mod.id);
	if (!dist) {
		dist = distributor_dialog_data_alloc(dlg);
	}
	dist->serializer = serializer;
}

void ast_sip_dialog_set_endpoint(pjsip_dialog *dlg, struct ast_sip_endpoint *endpoint)
{
	struct distributor_dialog_data *dist;
	SCOPED_LOCK(lock, dlg, pjsip_dlg_inc_lock, pjsip_dlg_dec_lock);

	dist = pjsip_dlg_get_mod_data(dlg, distributor_mod.id);
	if (!dist) {
		dist = distributor_dialog_data_alloc(dlg);
	}
	dist->endpoint = endpoint;
}

struct ast_sip_endpoint *ast_sip_dialog_get_endpoint(pjsip_dialog *dlg)
{
	struct distributor_dialog_data *dist;
	SCOPED_LOCK(lock, dlg, pjsip_dlg_inc_lock, pjsip_dlg_dec_lock);

	dist = pjsip_dlg_get_mod_data(dlg, distributor_mod.id);
	if (!dist || !dist->endpoint) {
		return NULL;
	}
	ao2_ref(dist->endpoint, +1);
	return dist->endpoint;
}

static pjsip_dialog *find_dialog(pjsip_rx_data *rdata)
{
	pj_str_t tsx_key;
	pjsip_transaction *tsx;
	pjsip_dialog *dlg;
	pj_str_t *local_tag;
	pj_str_t *remote_tag;

	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
		local_tag = &rdata->msg_info.to->tag;
		remote_tag = &rdata->msg_info.from->tag;
	} else {
		local_tag = &rdata->msg_info.from->tag;
		remote_tag = &rdata->msg_info.to->tag;
	}

	/* We can only call the convenient method for
	 *  1) responses
	 *  2) non-CANCEL requests
	 *  3) CANCEL requests with a to-tag
	 */
	if (rdata->msg_info.msg->type == PJSIP_RESPONSE_MSG ||
			pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_cancel_method) ||
			rdata->msg_info.to->tag.slen != 0) {
		return pjsip_ua_find_dialog(&rdata->msg_info.cid->id, local_tag,
				remote_tag, PJ_TRUE);
	}

	/* Incoming CANCEL without a to-tag can't use same method for finding the
	 * dialog. Instead, we have to find the matching INVITE transaction and
	 * then get the dialog from the transaction
	 */
	pjsip_tsx_create_key(rdata->tp_info.pool, &tsx_key, PJSIP_ROLE_UAS,
			pjsip_get_invite_method(), rdata);

	tsx = pjsip_tsx_layer_find_tsx(&tsx_key, PJ_TRUE);
	if (!tsx) {
		ast_log(LOG_ERROR, "Could not find matching INVITE transaction for CANCEL request\n");
		return NULL;
	}

	dlg = pjsip_tsx_get_dlg(tsx);

#ifdef HAVE_PJ_TRANSACTION_GRP_LOCK
	pj_grp_lock_release(tsx->grp_lock);
#else
	pj_mutex_unlock(tsx->mutex);
#endif

	if (!dlg) {
		return NULL;
	}

	pjsip_dlg_inc_lock(dlg);
	return dlg;
}

static pj_bool_t distributor(pjsip_rx_data *rdata)
{
	pjsip_dialog *dlg = find_dialog(rdata);
	struct distributor_dialog_data *dist = NULL;
	struct ast_taskprocessor *serializer = NULL;
	pjsip_rx_data *clone;

	if (dlg) {
		dist = pjsip_dlg_get_mod_data(dlg, distributor_mod.id);
		if (dist) {
			serializer = dist->serializer;
		}
	}

	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG && (
		!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_cancel_method) || 
		!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_bye_method)) &&
		!serializer) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 481, NULL, NULL, NULL);
		goto end;
	}

	pjsip_rx_data_clone(rdata, 0, &clone);

	if (dist) {
		clone->endpt_info.mod_data[distributor_mod.id] = dist->endpoint;
	}

	ast_sip_push_task(serializer, distribute, clone);

end:
	if (dlg) {
		pjsip_dlg_dec_lock(dlg);
	}

	return PJ_TRUE;
}

static pj_bool_t endpoint_lookup(pjsip_rx_data *rdata);

static pjsip_module endpoint_mod = {
	.name = {"Endpoint Identifier", 19},
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 3,
	.on_rx_request = endpoint_lookup,
};

static struct ast_sip_auth *artificial_auth;

static int create_artificial_auth(void)
{
	if (!(artificial_auth = ast_sorcery_alloc(
		      ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE, "artificial"))) {
		ast_log(LOG_ERROR, "Unable to create artificial auth\n");
		return -1;
	}

	ast_string_field_set(artificial_auth, realm, "asterisk");
	ast_string_field_set(artificial_auth, auth_user, "");
	ast_string_field_set(artificial_auth, auth_pass, "");
	artificial_auth->type = AST_SIP_AUTH_TYPE_ARTIFICIAL;
	return 0;
}

struct ast_sip_auth *ast_sip_get_artificial_auth(void)
{
	ao2_ref(artificial_auth, +1);
	return artificial_auth;
}

static struct ast_sip_endpoint *artificial_endpoint;

static int create_artificial_endpoint(void)
{
	if (!(artificial_endpoint = ast_sorcery_alloc(
		      ast_sip_get_sorcery(), "endpoint", NULL))) {
		return -1;
	}

	AST_VECTOR_INIT(&artificial_endpoint->inbound_auths, 1);
	/* Pushing a bogus value into the vector will ensure that
	 * the proper size of the vector is returned. This value is
	 * not actually used anywhere
	 */
	AST_VECTOR_APPEND(&artificial_endpoint->inbound_auths, "artificial-auth");
	return 0;
}

struct ast_sip_endpoint *ast_sip_get_artificial_endpoint(void)
{
	ao2_ref(artificial_endpoint, +1);
	return artificial_endpoint;
}

static void log_unidentified_request(pjsip_rx_data *rdata)
{
	char from_buf[PJSIP_MAX_URL_SIZE];
	char callid_buf[PJSIP_MAX_URL_SIZE];
	pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, rdata->msg_info.from->uri, from_buf, PJSIP_MAX_URL_SIZE);
	ast_copy_pj_str(callid_buf, &rdata->msg_info.cid->id, PJSIP_MAX_URL_SIZE);
	ast_log(LOG_NOTICE, "Request from '%s' failed for '%s:%d' (callid: %s) - No matching endpoint found\n",
		from_buf, rdata->pkt_info.src_name, rdata->pkt_info.src_port, callid_buf);
}

static pj_bool_t endpoint_lookup(pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint;
	int is_ack = rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD;

	endpoint = rdata->endpt_info.mod_data[distributor_mod.id];
	if (endpoint) {
		/* Bumping the refcount makes refcounting consistent whether an endpoint
		 * is looked up or not */
		ao2_ref(endpoint, +1);
	} else {
		endpoint = ast_sip_identify_endpoint(rdata);
	}

	if (!endpoint && !is_ack) {
		char name[AST_UUID_STR_LEN] = "";
		pjsip_uri *from = rdata->msg_info.from->uri;

		/* always use an artificial endpoint - per discussion no reason
		   to have "alwaysauthreject" as an option.  It is felt using it
		   was a bug fix and it is not needed since we are not worried about
		   breaking old stuff and we really don't want to enable the discovery
		   of SIP accounts */
		endpoint = ast_sip_get_artificial_endpoint();

		if (PJSIP_URI_SCHEME_IS_SIP(from) || PJSIP_URI_SCHEME_IS_SIPS(from)) {
			pjsip_sip_uri *sip_from = pjsip_uri_get_uri(from);
			ast_copy_pj_str(name, &sip_from->user, sizeof(name));
		}

		log_unidentified_request(rdata);
		ast_sip_report_invalid_endpoint(name, rdata);
	}
	rdata->endpt_info.mod_data[endpoint_mod.id] = endpoint;
	return PJ_FALSE;
}

static pj_bool_t authenticate(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, ast_pjsip_rdata_get_endpoint(rdata), ao2_cleanup);
	int is_ack = rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD;

	ast_assert(endpoint != NULL);

	if (!is_ack && ast_sip_requires_authentication(endpoint, rdata)) {
		pjsip_tx_data *tdata;
		pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, 401, NULL, &tdata);
		switch (ast_sip_check_authentication(endpoint, rdata, tdata)) {
		case AST_SIP_AUTHENTICATION_CHALLENGE:
			/* Send the 401 we created for them */
			ast_sip_report_auth_challenge_sent(endpoint, rdata, tdata);
			pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL);
			return PJ_TRUE;
		case AST_SIP_AUTHENTICATION_SUCCESS:
			ast_sip_report_auth_success(endpoint, rdata);
			pjsip_tx_data_dec_ref(tdata);
			return PJ_FALSE;
		case AST_SIP_AUTHENTICATION_FAILED:
			ast_sip_report_auth_failed_challenge_response(endpoint, rdata);
			pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL);
			return PJ_TRUE;
		case AST_SIP_AUTHENTICATION_ERROR:
			ast_sip_report_auth_failed_challenge_response(endpoint, rdata);
			pjsip_tx_data_dec_ref(tdata);
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
			return PJ_TRUE;
		}
	}

	return PJ_FALSE;
}

static pjsip_module auth_mod = {
	.name = {"Request Authenticator", 21},
	.priority = PJSIP_MOD_PRIORITY_APPLICATION - 1,
	.on_rx_request = authenticate,
};

static int distribute(void *data)
{
	static pjsip_process_rdata_param param = {
		.start_mod = &distributor_mod,
		.idx_after_start = 1,
	};
	pj_bool_t handled;
	pjsip_rx_data *rdata = data;
	int is_request = rdata->msg_info.msg->type == PJSIP_REQUEST_MSG;
	int is_ack = is_request ? rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD : 0;
	struct ast_sip_endpoint *endpoint;

	pjsip_endpt_process_rx_data(ast_sip_get_pjsip_endpoint(), rdata, &param, &handled);
	if (!handled && is_request && !is_ack) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 501, NULL, NULL, NULL);
	}

	/* The endpoint_mod stores an endpoint reference in the mod_data of rdata. This
	 * is the only appropriate spot to actually decrement the reference.
	 */
	endpoint = rdata->endpt_info.mod_data[endpoint_mod.id];
	ao2_cleanup(endpoint);
	pjsip_rx_data_free_cloned(rdata);
	return 0;
}

struct ast_sip_endpoint *ast_pjsip_rdata_get_endpoint(pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint = rdata->endpt_info.mod_data[endpoint_mod.id];
	if (endpoint) {
		ao2_ref(endpoint, +1);
	}
	return endpoint;
}

int ast_sip_initialize_distributor(void)
{
	if (create_artificial_endpoint() || create_artificial_auth()) {
		return -1;
	}

	if (ast_sip_register_service(&distributor_mod)) {
		return -1;
	}
	if (ast_sip_register_service(&endpoint_mod)) {
		return -1;
	}
	if (ast_sip_register_service(&auth_mod)) {
		return -1;
	}

	return 0;
}

void ast_sip_destroy_distributor(void)
{
	ast_sip_unregister_service(&distributor_mod);
	ast_sip_unregister_service(&endpoint_mod);
	ast_sip_unregister_service(&auth_mod);

	ao2_cleanup(artificial_auth);
	ao2_cleanup(artificial_endpoint);
}
