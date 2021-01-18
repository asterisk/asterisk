/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 * this module Copyright (C) 2021, CCX Technologies
 *
 * Charles Eidsness <charles@ccxtechnologies.com>
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

/*!
 * \brief SIGCOMP Transport Module
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/module.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

static pj_bool_t sigcomp_on_rx_request(pjsip_rx_data *rdata)
{
	pj_bool_t res;
	struct ast_sip_endpoint *endpoint;

	ast_log(LOG_ERROR, "SIGCOMP RX Request\n");

	return PJ_FALSE;
}

static pj_bool_t sigcomp_on_rx_response(pjsip_rx_data *rdata)
{
	pj_bool_t res;
	struct ast_sip_endpoint *endpoint;

	ast_log(LOG_ERROR, "SIGCOMP RX Response\n");

	return PJ_FALSE;
}

static pj_status_t sigcomp_on_tx_request(pjsip_tx_data *tdata)
{
	ast_log(LOG_ERROR, "SIGCOMP TX Request\n");

	return PJ_FALSE;
}

static pj_status_t sigcomp_on_tx_response(pjsip_tx_data *tdata)
{
	ast_log(LOG_ERROR, "SIGCOMP TX Response\n");

	return PJ_FALSE;
}


/* === */

static pjsip_module sigcomp_module = {
	.name = { "SIGCOMP Module", 14 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TRANSPORT_LAYER - 1,
	.on_rx_request = sigcomp_on_rx_request,
	.on_rx_response = sigcomp_on_rx_response,
	.on_tx_request = sigcomp_on_tx_request,
	.on_tx_response = sigcomp_on_tx_response,
};

/*! \brief Function called when an INVITE goes out */
static int sigcomp_incoming_invite_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	if (session->inv_session->state == PJSIP_INV_STATE_INCOMING) {
		pjsip_dlg_add_usage(session->inv_session->dlg, &sigcomp_module, NULL);
	}

	return 0;
}

/*! \brief Function called when an INVITE comes in */
static void sigcomp_outgoing_invite_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	if (session->inv_session->state == PJSIP_INV_STATE_NULL) {
		pjsip_dlg_add_usage(session->inv_session->dlg, &sigcomp_module, NULL);
	}
}

/*! \brief Supplement for adding sigcomp functionality to dialog */
static struct ast_sip_session_supplement sigcomp_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST + 1,
	.incoming_request = sigcomp_incoming_invite_request,
	.outgoing_request = sigcomp_outgoing_invite_request,
};

static int load_module(void)
{
	if (ast_sip_register_service(&sigcomp_module) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Could not register SIGCOMP module for incoming and outgoing requests\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sip_session_register_supplement(&sigcomp_supplement);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&sigcomp_module);
	ast_sip_session_unregister_supplement(&sigcomp_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP SIGCOMP Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip",
);
