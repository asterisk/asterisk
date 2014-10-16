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

static struct ast_sip_aor *find_aor(struct ast_sip_endpoint *endpoint, pjsip_uri *uri)
{
	char *configured_aors, *aor_name;
	pjsip_sip_uri *sip_uri;
	char *domain_name;
	RAII_VAR(struct ast_str *, id, NULL, ast_free);

	if (ast_strlen_zero(endpoint->aors)) {
		return NULL;
	}

	sip_uri = pjsip_uri_get_uri(uri);
	domain_name = ast_alloca(sip_uri->host.slen + 1);
	ast_copy_pj_str(domain_name, &sip_uri->host, sip_uri->host.slen + 1);

	configured_aors = ast_strdupa(endpoint->aors);

	/* Iterate the configured AORs to see if the user or the user+domain match */
	while ((aor_name = strsep(&configured_aors, ","))) {
		struct ast_sip_domain_alias *alias = NULL;

		if (!pj_strcmp2(&sip_uri->user, aor_name)) {
			break;
		}

		if (!id && !(id = ast_str_create(sip_uri->user.slen + sip_uri->host.slen + 2))) {
			return NULL;
		}

		ast_str_set(&id, 0, "%.*s@", (int)sip_uri->user.slen, sip_uri->user.ptr);
		if ((alias = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "domain_alias", domain_name))) {
			ast_str_append(&id, 0, "%s", alias->domain);
			ao2_cleanup(alias);
		} else {
			ast_str_append(&id, 0, "%s", domain_name);
		}

		if (!strcmp(aor_name, ast_str_buffer(id))) {
			ast_free(id);
			break;
		}
	}

	if (ast_strlen_zero(aor_name)) {
		return NULL;
	}

	return ast_sip_location_retrieve_aor(aor_name);
}

/*!
 * \brief Get the path string associated with this contact and tdata
 *
 * \param endpoint The endpoint from which to pull associated path data
 * \param contact_uri The URI identifying the associated contact
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

	hdr = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_SUPPORTED, NULL);
	if (!hdr) {
		/* insert a new Supported header */
		hdr = pjsip_supported_hdr_create(tdata->pool);
		if (!hdr) {
			return -1;
		}

		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)hdr);
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

	aor = find_aor(endpoint, tdata->msg->line.req.uri);
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
	pjsip_contact_hdr *contact_hdr;
	RAII_VAR(struct ast_sip_aor *, aor, NULL, ao2_cleanup);
	pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);
	const pj_str_t REGISTER_METHOD = {"REGISTER", 8};

	if (!endpoint
		|| !pj_stristr(&REGISTER_METHOD, &cseq->method.name)
		|| !PJSIP_IS_STATUS_IN_CLASS(status.code, 200)) {
		return;
	}

	contact_hdr = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL);
	if (!contact_hdr) {
		return;
	}

	aor = find_aor(endpoint, contact_hdr->uri);
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
	CHECK_PJSIP_SESSION_MODULE_LOADED();

	if (ast_sip_register_supplement(&path_supplement)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_session_register_supplement(&path_session_supplement)) {
		ast_sip_unregister_supplement(&path_supplement);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_supplement(&path_supplement);
	ast_sip_session_unregister_supplement(&path_session_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Path Header Support",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
);
