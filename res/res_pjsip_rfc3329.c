/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Commend International
 *
 * Maximilian Fridrich <m.fridrich@commend.com>
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

/*! \brief Private data structure used with the modules's datastore */
struct rfc3329_store_data {
	int last_rx_status_code;
};

static void datastore_destroy_cb(void *data)
{
	struct rfc3329_store_data *d = data;
	if (d) {
		ast_free(d);
	}
}

/*! \brief The channel datastore the module uses to store state */
static const struct ast_datastore_info rfc3329_store_datastore = {
	.type = "rfc3329_store",
	.destroy = datastore_destroy_cb
};

static void rfc3329_incoming_response(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session, "rfc3329_store"), ao2_cleanup);
	static const pj_str_t str_security_server = { "Security-Server", 15 };
	struct ast_sip_contact_status *contact_status = NULL;
	struct ast_sip_security_mechanism *mech;
	struct rfc3329_store_data *store_data;
	pjsip_generic_string_hdr *header;
	char buf[128];
	char *hdr_val;
	char *mechanism;

	if (!session || !session->endpoint || !session->endpoint->security_negotiation
		|| !session->contact || !(contact_status = ast_sip_get_contact_status(session->contact))
		|| !session->inv_session->dlg) {
		return;
	}

	ao2_lock(contact_status);
	if (AST_VECTOR_SIZE(&contact_status->security_mechanisms)) {
		goto out;
	}

	if (!datastore
		&& (datastore = ast_sip_session_alloc_datastore(&rfc3329_store_datastore, "rfc3329_store"))
		&& (store_data = ast_calloc(1, sizeof(struct rfc3329_store_data)))) {

		store_data->last_rx_status_code = rdata->msg_info.msg->line.status.code;
		datastore->data = store_data;
		ast_sip_session_add_datastore(session, datastore);
	} else {
		ast_log(AST_LOG_WARNING, "Could not store session data. Still attempting requests, but they might be missing necessary headers.\n");
	}

	header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_security_server, NULL);
	for (; header;
		header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_security_server, header->next)) {
		/* Parse Security-Server headers and add to contact status to use for future requests. */
		ast_copy_pj_str(buf, &header->hvalue, sizeof(buf));
		hdr_val = ast_skip_blanks(buf);

		while ((mechanism = ast_strsep(&hdr_val, ',', AST_STRSEP_ALL))) {
			if (!ast_sip_str_to_security_mechanism(&mech, mechanism)) {
				AST_VECTOR_APPEND(&contact_status->security_mechanisms, mech);
			}
		}
	}

out:
	ao2_unlock(contact_status);
	ao2_cleanup(contact_status);
}

static void add_outgoing_request_headers(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, struct pjsip_tx_data *tdata,
	struct ast_datastore *datastore)
{
	static const pj_str_t security_verify = { "Security-Verify", 15 };
	struct pjsip_generic_string_hdr *hdr = NULL;
	struct ast_sip_contact_status *contact_status = NULL;
	struct rfc3329_store_data *store_data;
	
	if (endpoint->security_negotiation != AST_SIP_SECURITY_NEG_MEDIASEC) {
		return;
	}

	contact_status = ast_sip_get_contact_status(contact);
	hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &security_verify, NULL);

	if (contact_status == NULL) {
		return;
	}

	ao2_lock(contact_status);
	if (AST_VECTOR_SIZE(&contact_status->security_mechanisms) && hdr == NULL) {
		/* Add Security-Verify headers (with q-value) */
		ast_sip_add_security_headers(&contact_status->security_mechanisms, "Security-Verify", 0, tdata);
	}
	if (datastore) {
		store_data = datastore->data;
		if (store_data->last_rx_status_code == 401) {
			/* Add Security-Client headers (no q-value) */
			ast_sip_add_security_headers(&endpoint->security_mechanisms, "Security-Client", 0, tdata);
		}
	}
	ao2_unlock(contact_status);

	ao2_cleanup(contact_status);
}

static void rfc3329_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session, "rfc3329_store"), ao2_cleanup);
	if (session->contact == NULL) {
		return;
	}
	add_outgoing_request_headers(session->endpoint, session->contact, tdata, datastore);
}

static struct ast_sip_session_supplement rfc3329_supplement = {
	.incoming_response = rfc3329_incoming_response,
	.outgoing_request = rfc3329_outgoing_request,
};

static void rfc3329_options_request(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, struct pjsip_tx_data *tdata)
{
	add_outgoing_request_headers(endpoint, contact, tdata, NULL);
}

static struct ast_sip_supplement rfc3329_options_supplement = {
	.method = "OPTIONS",
	.outgoing_request = rfc3329_options_request,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&rfc3329_supplement);
	ast_sip_register_supplement(&rfc3329_options_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&rfc3329_supplement);
	ast_sip_unregister_supplement(&rfc3329_options_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP RFC3329 Support (partial)",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
