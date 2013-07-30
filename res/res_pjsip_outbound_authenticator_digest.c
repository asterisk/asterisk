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

static int set_outbound_authentication_credentials(pjsip_auth_clt_sess *auth_sess, const struct ast_sip_auth_array *array)
{
	struct ast_sip_auth **auths = ast_alloca(array->num * sizeof(*auths));
	pjsip_cred_info *auth_creds = ast_alloca(array->num * sizeof(*auth_creds));
	int res = 0;
	int i;

	if (ast_sip_retrieve_auths(array, auths)) {
		res = -1;
		goto cleanup;
	}

	for (i = 0; i < array->num; ++i) {
		pj_cstr(&auth_creds[i].realm, auths[i]->realm);
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

	pjsip_auth_clt_set_credentials(auth_sess, array->num, auth_creds);

cleanup:
	ast_sip_cleanup_auths(auths, array->num);
	return res;
}

static int digest_create_request_with_auth(const struct ast_sip_auth_array *auths, pjsip_rx_data *challenge,
		pjsip_transaction *tsx, pjsip_tx_data **new_request)
{
	pjsip_auth_clt_sess auth_sess;

	if (pjsip_auth_clt_init(&auth_sess, ast_sip_get_pjsip_endpoint(),
				tsx->pool, 0) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to initialize client authentication session\n");
		return -1;
	}

	if (set_outbound_authentication_credentials(&auth_sess, auths)) {
		ast_log(LOG_WARNING, "Failed to set authentication credentials\n");
		return -1;
	}

	if (pjsip_auth_clt_reinit_req(&auth_sess, challenge,
				tsx->last_tx, new_request) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to create new request with authentication credentials\n");
		return -1;
	}

	return 0;
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
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
