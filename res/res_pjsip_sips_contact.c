/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"

/*!
 * \brief Upgrade Contact URIs on outgoing SIP requests to SIPS if required.
 *
 * The rules being used here are according to RFC 3261 section 8.1.1.8. In
 * brief, if the request URI is SIPS or the topmost Route header is SIPS,
 * then the Contact header we send must also be SIPS.
 */
static pj_status_t sips_contact_on_tx_request(pjsip_tx_data *tdata)
{
	pjsip_contact_hdr *contact;
	pjsip_route_hdr *route;
	pjsip_sip_uri *contact_uri;

	contact = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL);
	if (!contact) {
		return PJ_SUCCESS;
	}

	contact_uri = pjsip_uri_get_uri(contact->uri);
	if (PJSIP_URI_SCHEME_IS_SIPS(contact_uri)) {
		/* If the Contact header is already SIPS, then we don't need to do anything */
		return PJ_SUCCESS;
	}

	if (PJSIP_URI_SCHEME_IS_SIPS(tdata->msg->line.req.uri)) {
		ast_debug(1, "Upgrading contact URI on outgoing SIP request to SIPS due to SIPS Request URI\n");
		pjsip_sip_uri_set_secure(contact_uri, PJ_TRUE);
		return PJ_SUCCESS;
	}

	route = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_ROUTE, NULL);
	if (!route) {
		return PJ_SUCCESS;
	}

	if (!PJSIP_URI_SCHEME_IS_SIPS(&route->name_addr)) {
		return PJ_SUCCESS;
	}

	/* Our Contact header is not a SIPS URI, but our topmost Route header is. */
	ast_debug(1, "Upgrading contact URI on outgoing SIP request to SIPS due to SIPS Route header\n");
	pjsip_sip_uri_set_secure(contact_uri, PJ_TRUE);

	return PJ_SUCCESS;
}

static pjsip_module sips_contact_module = {
	.name = {"SIPS Contact", 12 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 2,
	.on_tx_request = sips_contact_on_tx_request,
};

static int unload_module(void)
{
	ast_sip_unregister_service(&sips_contact_module);
	return 0;
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	if (ast_sip_register_service(&sips_contact_module)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "UAC SIPS Contact support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
