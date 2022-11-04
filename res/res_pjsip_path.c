/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
#include "asterisk/strings.h"

static const pj_str_t PATH_NAME = { "Path", 4 };
static pj_str_t PATH_SUPPORTED_NAME = { "path", 4 };

static struct ast_sip_aor *find_aor(struct ast_sip_contact *contact)
{
	if (!contact) {
		return NULL;
	}
	if (ast_strlen_zero(contact->aor)) {
		return NULL;
	}

	return ast_sip_location_retrieve_aor(contact->aor);
}

/*!
 * \brief Get the path string associated with this contact and tdata
 *
 * \param pool
 * \param contact The URI identifying the associated contact
 * \param path_str The place to store the retrieved path information
 *
 * \retval zero on success
 * \retval non-zero on failure or no available path information
 */
static int path_get_string(pj_pool_t *pool, struct ast_sip_contact *contact, pj_str_t *path_str)
{
	if (!contact || ast_strlen_zero(contact->path)) {
		return -1;
	}

	*path_str = pj_strdup3(pool, contact->path);
	return 0;
}

static int add_supported(pjsip_tx_data *tdata)
{
	pjsip_supported_hdr *hdr;
	int i;

	hdr = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_SUPPORTED, NULL);
	if (!hdr) {
		/* insert a new Supported header */
		hdr = pjsip_supported_hdr_create(tdata->pool);
		if (!hdr) {
			return -1;
		}

		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)hdr);
	}

	/* Don't add the value if it's already there */
	for (i = 0; i < hdr->count; ++i) {
		if (pj_stricmp(&hdr->values[i], &PATH_SUPPORTED_NAME) == 0) {
			return 0;
		}
	}

	if (hdr->count >= PJSIP_GENERIC_ARRAY_MAX_COUNT) {
		return -1;
	}

	/* add on to the existing Supported header */
	pj_strassign(&hdr->values[hdr->count++], &PATH_SUPPORTED_NAME);

	return 0;
}

/*!
 * \internal
 * \brief Adds a Route header to an outgoing request if
 * path information is available.
 *
 * \param endpoint The endpoint with which this request is associated
 * \param contact The contact to which this request is being sent
 * \param tdata The outbound request
 */
static void path_outgoing_request(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, pjsip_tx_data *tdata)
{
	RAII_VAR(struct ast_sip_aor *, aor, NULL, ao2_cleanup);

	if (!endpoint) {
		return;
	}

	aor = find_aor(contact);
	if (!aor || !aor->support_path) {
		return;
	}

	if (add_supported(tdata)) {
		return;
	}

	if (contact && !ast_strlen_zero(contact->path)) {
		ast_sip_set_outbound_proxy(tdata, contact->path);
	}
}

static void path_session_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	path_outgoing_request(session->endpoint, session->contact, tdata);
}

/*!
 * \internal
 * \brief Adds a path header to an outgoing 2XX response
 *
 * \param endpoint The endpoint to which the INVITE response is to be sent
 * \param contact The contact to which the INVITE response is to be sent
 * \param tdata The outbound INVITE response
 */
static void path_outgoing_response(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact, pjsip_tx_data *tdata)
{
	struct pjsip_status_line status = tdata->msg->line.status;
	pj_str_t path_dup;
	pjsip_generic_string_hdr *path_hdr;
	RAII_VAR(struct ast_sip_aor *, aor, NULL, ao2_cleanup);
	pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);
	const pj_str_t REGISTER_METHOD = {"REGISTER", 8};

	if (!endpoint
		|| !pj_stristr(&REGISTER_METHOD, &cseq->method.name)
		|| !PJSIP_IS_STATUS_IN_CLASS(status.code, 200)) {
		return;
	}

	aor = find_aor(contact);
	if (!aor || !aor->support_path || add_supported(tdata)
		|| path_get_string(tdata->pool, contact, &path_dup)) {
		return;
	}

	path_hdr = pjsip_generic_string_hdr_create(tdata->pool, &PATH_NAME, &path_dup);
	if (!path_hdr) {
		return;
	}

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)path_hdr);
}

static void path_session_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	path_outgoing_response(session->endpoint, session->contact, tdata);
}

static struct ast_sip_supplement path_supplement = {
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL - 100,
	.outgoing_request = path_outgoing_request,
	.outgoing_response = path_outgoing_response,
};

static struct ast_sip_session_supplement path_session_supplement = {
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL - 100,
	.outgoing_request = path_session_outgoing_request,
	.outgoing_response = path_session_outgoing_response,
};

static int load_module(void)
{
	ast_sip_register_supplement(&path_supplement);
	ast_sip_session_register_supplement(&path_session_supplement);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_supplement(&path_supplement);
	ast_sip_session_unregister_supplement(&path_session_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Path Header Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
