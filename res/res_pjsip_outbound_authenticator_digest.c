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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"

static pjsip_www_authenticate_hdr *get_auth_header(pjsip_rx_data *challenge,
	const void *start)
{
	pjsip_hdr_e search_type;

	if (challenge->msg_info.msg->line.status.code == PJSIP_SC_UNAUTHORIZED) {
		search_type = PJSIP_H_WWW_AUTHENTICATE;
	} else if (challenge->msg_info.msg->line.status.code == PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED) {
		search_type = PJSIP_H_PROXY_AUTHENTICATE;
	} else {
		ast_log(LOG_ERROR,
				"Status code %d was received when it should have been 401 or 407.\n",
				challenge->msg_info.msg->line.status.code);
		return NULL ;
	}

	return pjsip_msg_find_hdr(challenge->msg_info.msg, search_type, start);

}

static int set_outbound_authentication_credentials(pjsip_auth_clt_sess *auth_sess,
		const struct ast_sip_auth_vector *auth_vector, pjsip_rx_data *challenge,
		pjsip_www_authenticate_hdr *auth_hdr)
{
	size_t auth_size = AST_VECTOR_SIZE(auth_vector);
	struct ast_sip_auth **auths = ast_alloca(auth_size * sizeof(*auths));
	pjsip_cred_info *auth_creds = ast_alloca(auth_size * sizeof(*auth_creds));
	int res = 0;
	int i;

	if (ast_sip_retrieve_auths(auth_vector, auths)) {
		res = -1;
		goto cleanup;
	}

	for (i = 0; i < auth_size; ++i) {
		if (ast_strlen_zero(auths[i]->realm)) {
			auth_creds[i].realm = auth_hdr->challenge.common.realm;
		} else {
			pj_cstr(&auth_creds[i].realm, auths[i]->realm);
		}
		pj_cstr(&auth_creds[i].username, auths[i]->auth_user);
		pj_cstr(&auth_creds[i].scheme, "digest");
		switch (auths[i]->type) {
		case AST_SIP_AUTH_TYPE_USER_PASS:
			pj_cstr(&auth_creds[i].data, auths[i]->auth_pass);
			auth_creds[i].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
			break;
		case AST_SIP_AUTH_TYPE_MD5:
			pj_cstr(&auth_creds[i].data, auths[i]->md5_creds);
			auth_creds[i].data_type = PJSIP_CRED_DATA_DIGEST;
			break;
		case AST_SIP_AUTH_TYPE_ARTIFICIAL:
			ast_log(LOG_ERROR, "Trying to set artificial outbound auth credentials shouldn't happen.\n");
			break;
		}
	}

	pjsip_auth_clt_set_credentials(auth_sess, auth_size, auth_creds);

cleanup:
	ast_sip_cleanup_auths(auths, auth_size);
	return res;
}

static int digest_create_request_with_auth(const struct ast_sip_auth_vector *auths,
	pjsip_rx_data *challenge, pjsip_tx_data *old_request, pjsip_tx_data **new_request)
{
	pjsip_auth_clt_sess auth_sess;
	pjsip_cseq_hdr *cseq;
	pj_status_t status;
	struct ast_sip_endpoint *endpoint;
	char *id = NULL;
	const char *id_type;
	pjsip_www_authenticate_hdr *auth_hdr;
	struct ast_str *realms;
	pjsip_dialog *dlg;

	dlg = pjsip_rdata_get_dlg(challenge);
	if (dlg) {
		endpoint = ast_sip_dialog_get_endpoint(dlg);
		id = endpoint ? ast_strdupa(ast_sorcery_object_get_id(endpoint)) : NULL;
		ao2_cleanup(endpoint);
		id_type = "Endpoint";
	}
	/* If there was no dialog, then this is probably a REGISTER so no endpoint */
	if (!id) {
		id = ast_alloca(AST_SOCKADDR_BUFLEN);
		pj_sockaddr_print(&challenge->pkt_info.src_addr, id, AST_SOCKADDR_BUFLEN, 3);
		id_type = "Host";
	}

	auth_hdr = get_auth_header(challenge, NULL);
	if (auth_hdr == NULL) {
		ast_log(LOG_ERROR, "%s: '%s': Unable to find authenticate header in challenge.\n",
			id_type, id);
		return -1;
	}

	if (pjsip_auth_clt_init(&auth_sess, ast_sip_get_pjsip_endpoint(),
				old_request->pool, 0) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "%s: '%s': Failed to initialize client authentication session\n",
			id_type, id);
		return -1;
	}

	if (set_outbound_authentication_credentials(&auth_sess, auths, challenge, auth_hdr)) {
		ast_log(LOG_WARNING, "%s: '%s': Failed to set authentication credentials\n",
			id_type, id);
#if defined(HAVE_PJSIP_AUTH_CLT_DEINIT)
		/* In case it is not a noop here in the future. */
		pjsip_auth_clt_deinit(&auth_sess);
#endif
		return -1;
	}

	status = pjsip_auth_clt_reinit_req(&auth_sess, challenge, old_request, new_request);
#if defined(HAVE_PJSIP_AUTH_CLT_DEINIT)
	/* Release any cached auths */
	pjsip_auth_clt_deinit(&auth_sess);
#endif

	switch (status) {
	case PJ_SUCCESS:
		/* PJSIP creates a new transaction for new_request (meaning it creates a new
		 * branch). However, it recycles the Call-ID, from-tag, and CSeq from the
		 * original request. Some SIP implementations will not process the new request
		 * since the CSeq is the same as the original request. Incrementing it here
		 * fixes the interop issue
		 */
		cseq = pjsip_msg_find_hdr((*new_request)->msg, PJSIP_H_CSEQ, NULL);
		ast_assert(cseq != NULL);
		++cseq->cseq;
		return 0;
	case PJSIP_ENOCREDENTIAL:
		realms = ast_str_create(32);
		if (realms) {
			ast_str_append(&realms, 0, "%.*s", (int)auth_hdr->challenge.common.realm.slen,
				auth_hdr->challenge.common.realm.ptr);
			while((auth_hdr = get_auth_header(challenge, auth_hdr->next))) {
				ast_str_append(&realms, 0, ",%.*s", (int)auth_hdr->challenge.common.realm.slen,
					auth_hdr->challenge.common.realm.ptr);
			}
		}
		ast_log(LOG_WARNING,
			"%s: '%s': Unable to create request with auth. "
			"No auth credentials for realm(s) '%s' in challenge.\n", id_type, id,
			realms ? ast_str_buffer(realms) : "<unknown>");
		ast_free(realms);
		break;
	case PJSIP_EAUTHSTALECOUNT:
		ast_log(LOG_WARNING,
			"%s: '%s': Unable to create request with auth.  Number of stale retries exceeded.\n",
			id_type, id);
		break;
	case PJSIP_EFAILEDCREDENTIAL:
		ast_log(LOG_WARNING, "%s: '%s': Authentication credentials not accepted by server.\n",
			id_type, id);
		break;
	default:
		ast_log(LOG_WARNING, "%s: '%s': Unable to create request with auth. Unknown failure.\n",
			id_type, id);
		break;
	}

	return -1;
}

static struct ast_sip_outbound_authenticator digest_authenticator = {
	.create_request_with_auth = digest_create_request_with_auth,
};

static int load_module(void)
{
	if (ast_sip_register_outbound_authenticator(&digest_authenticator)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_outbound_authenticator(&digest_authenticator);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP authentication resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_pjsip",
);
