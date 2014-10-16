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
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

AO2_GLOBAL_OBJ_STATIC(entity_id);

/*!
 * \brief Determine if authentication is required
 *
 * Authentication is required if the endpoint has at least one auth
 * section specified
 */
static int digest_requires_authentication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, artificial, ast_sip_get_artificial_endpoint(), ao2_cleanup);

	return endpoint == artificial || AST_VECTOR_SIZE(&endpoint->inbound_auths) > 0;
}

static void auth_store_cleanup(void *data)
{
	struct ast_sip_auth **auth = data;

	ao2_cleanup(*auth);
	ast_free(data);
}

/*!
 * \brief Thread-local storage for \ref ast_sip_auth
 *
 * The PJSIP authentication API is a bit annoying. When you set
 * up an authentication server, you specify a lookup callback to
 * call into when verifying incoming credentials. The problem
 * with this callback is that it only gives you the realm and
 * authentication username. In 2.0.5, there is a new version of
 * the callback you can use that gives the pjsip_rx_data in
 * addition.
 *
 * Unfortunately, the data we actually \b need is the
 * \ref ast_sip_auth we are currently observing. So we have two
 * choices:
 * 1) Use the current PJSIP API and use thread-local storage
 * to temporarily store our SIP authentication information. Then
 * in the callback, we can retrieve the authentication info and
 * use as needed. Given our threading model, this is safe.
 * 2) Use the 2.0.5 API and temporarily store the authentication
 * information in the rdata's endpoint_info. Then in the callback,
 * we can retrieve the authentication info from the rdata.
 *
 * I've chosen option 1 since it does not require backporting
 * any APIs from future versions of PJSIP, plus I feel the
 * thread-local option is a bit cleaner.
 */
AST_THREADSTORAGE_CUSTOM(auth_store, NULL, auth_store_cleanup);

/*!
 * \brief Store authentication information in thread-local storage
 */
static int store_auth(struct ast_sip_auth *auth)
{
	struct ast_sip_auth **pointing;
	pointing = ast_threadstorage_get(&auth_store, sizeof(pointing));
	if (!pointing || *pointing) {
		return -1;
	}

	ao2_ref(auth, +1);
	*pointing = auth;
	return 0;
}

/*!
 * \brief Remove authentication information from thread-local storage
 */
static int remove_auth(void)
{
	struct ast_sip_auth **pointing;
	pointing = ast_threadstorage_get(&auth_store, sizeof(pointing));
	if (!pointing) {
		return -1;
	}

	ao2_cleanup(*pointing);
	*pointing = NULL;
	return 0;
}

/*!
 * \brief Retrieve authentication information from thread-local storage
 */
static struct ast_sip_auth *get_auth(void)
{
	struct ast_sip_auth **auth;
	auth = ast_threadstorage_get(&auth_store, sizeof(auth));
	if (auth && *auth) {
		ao2_ref(*auth, +1);
		return *auth;
	}
	return NULL;
}

/*!
 * \brief Lookup callback for authentication verification
 *
 * This function is called when we call pjsip_auth_srv_verify(). It
 * expects us to verify that the realm and account name from the
 * Authorization header is correct. We are then supposed to supply
 * a password or MD5 sum of credentials.
 *
 * \param pool A memory pool we can use for allocations
 * \param realm The realm from the Authorization header
 * \param acc_name the user from the Authorization header
 * \param[out] info The credentials we need to fill in
 * \retval PJ_SUCCESS Successful authentication
 * \retval other Unsuccessful
 */
static pj_status_t digest_lookup(pj_pool_t *pool, const pj_str_t *realm,
		const pj_str_t *acc_name, pjsip_cred_info *info)
{
	RAII_VAR(struct ast_sip_auth *, auth, get_auth(), ao2_cleanup);
	if (!auth) {
		return PJSIP_SC_FORBIDDEN;
	}

	if (auth->type == AST_SIP_AUTH_TYPE_ARTIFICIAL) {
		return PJSIP_SC_FORBIDDEN;
	}

	if (pj_strcmp2(realm, auth->realm)) {
		return PJSIP_SC_FORBIDDEN;
	}
	if (pj_strcmp2(acc_name, auth->auth_user)) {
		return PJSIP_SC_FORBIDDEN;
	}

	pj_strdup2(pool, &info->realm, auth->realm);
	pj_strdup2(pool, &info->username, auth->auth_user);

	switch (auth->type) {
	case AST_SIP_AUTH_TYPE_USER_PASS:
		pj_strdup2(pool, &info->data, auth->auth_pass);
		info->data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
		break;
	case AST_SIP_AUTH_TYPE_MD5:
		pj_strdup2(pool, &info->data, auth->md5_creds);
		info->data_type = PJSIP_CRED_DATA_DIGEST;
		break;
	default:
		return PJSIP_SC_FORBIDDEN;
	}
	return PJ_SUCCESS;
}

/*!
 * \brief Calculate a nonce
 *
 * We use this in order to create authentication challenges. We also use this in order
 * to verify that an incoming request with credentials could be in response to one
 * of our challenges.
 *
 * The nonce is calculated from a timestamp, the source IP address, the source port, a
 * unique ID for us, and the realm. This helps to ensure that the incoming request
 * is from the same source that the nonce was calculated for. Including the realm
 * ensures that multiple challenges to the same request have different nonces.
 *
 * \param A UNIX timestamp expressed as a string
 * \param rdata The incoming request
 * \param realm The realm for which authentication should occur
 */
static int build_nonce(struct ast_str **nonce, const char *timestamp, const pjsip_rx_data *rdata, const char *realm)
{
	struct ast_str *str = ast_str_alloca(256);
	RAII_VAR(char *, eid, ao2_global_obj_ref(entity_id), ao2_cleanup);
	char hash[33];

	ast_str_append(&str, 0, "%s", timestamp);
	ast_str_append(&str, 0, ":%s", rdata->pkt_info.src_name);
	ast_str_append(&str, 0, ":%d", rdata->pkt_info.src_port);
	ast_str_append(&str, 0, ":%s", eid);
	ast_str_append(&str, 0, ":%s", realm);
	ast_md5_hash(hash, ast_str_buffer(str));

	ast_str_append(nonce, 0, "%s/%s", timestamp, hash);
	return 0;
}

/*!
 * \brief Ensure that a nonce on an incoming request is sane.
 *
 * The nonce in an incoming Authorization header needs to pass some scrutiny in order
 * for us to consider accepting it. What we do is re-build a nonce based on request
 * data and a realm and see if it matches the nonce they sent us.
 * \param candidate The nonce on an incoming request
 * \param rdata The incoming request
 * \param auth The auth credentials we are trying to match against.
 * \retval 0 Nonce does not pass validity checks
 * \retval 1 Nonce passes validity check
 */
static int check_nonce(const char *candidate, const pjsip_rx_data *rdata, const struct ast_sip_auth *auth)
{
	char *copy = ast_strdupa(candidate);
	char *timestamp = strsep(&copy, "/");
	int timestamp_int;
	time_t now = time(NULL);
	struct ast_str *calculated = ast_str_alloca(64);

	if (!copy) {
		/* Clearly a bad nonce! */
		return 0;
	}

	if (sscanf(timestamp, "%30d", &timestamp_int) != 1) {
		return 0;
	}

	if ((int) now - timestamp_int > auth->nonce_lifetime) {
		return 0;
	}

	build_nonce(&calculated, timestamp, rdata, auth->realm);
	ast_debug(3, "Calculated nonce %s. Actual nonce is %s\n", ast_str_buffer(calculated), candidate);
	if (strcmp(ast_str_buffer(calculated), candidate)) {
		return 0;
	}
	return 1;
}

static int find_challenge(const pjsip_rx_data *rdata, const struct ast_sip_auth *auth)
{
	struct pjsip_authorization_hdr *auth_hdr = (pjsip_authorization_hdr *) &rdata->msg_info.msg->hdr;
	int challenge_found = 0;
	char nonce[64];

	while ((auth_hdr = (pjsip_authorization_hdr *) pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_AUTHORIZATION, auth_hdr->next))) {
		ast_copy_pj_str(nonce, &auth_hdr->credential.digest.nonce, sizeof(nonce));
		if (check_nonce(nonce, rdata, auth) && !pj_strcmp2(&auth_hdr->credential.digest.realm, auth->realm)) {
			challenge_found = 1;
			break;
		}
	}

	return challenge_found;
}

/*!
 * \brief Common code for initializing a pjsip_auth_srv
 */
static void setup_auth_srv(pj_pool_t *pool, pjsip_auth_srv *auth_server, const char *realm)
{
	pj_str_t realm_str;
	pj_cstr(&realm_str, realm);

	pjsip_auth_srv_init(pool, auth_server, &realm_str, digest_lookup, 0);
}

/*!
 * \brief Result of digest verification
 */
enum digest_verify_result {
	/*! Authentication credentials incorrect */
	AUTH_FAIL,
	/*! Authentication credentials correct */
	AUTH_SUCCESS,
	/*! Authentication credentials correct but nonce mismatch */
	AUTH_STALE,
	/*! Authentication credentials were not provided */
	AUTH_NOAUTH,
};

/*!
 * \brief astobj2 callback for verifying incoming credentials
 *
 * \param auth The ast_sip_auth to check against
 * \param rdata The incoming request
 * \param pool A pool to use for the auth server
 * \return CMP_MATCH on successful authentication
 * \return 0 on failed authentication
 */
static int verify(struct ast_sip_auth *auth, pjsip_rx_data *rdata, pj_pool_t *pool)
{
	pj_status_t authed;
	int response_code;
	pjsip_auth_srv auth_server;
	int stale = 0;

	if (!find_challenge(rdata, auth)) {
		/* Couldn't find a challenge with a sane nonce.
		 * Nonce mismatch may just be due to staleness.
		 */
		stale = 1;
	}

	setup_auth_srv(pool, &auth_server, auth->realm);

	store_auth(auth);

	authed = pjsip_auth_srv_verify(&auth_server, rdata, &response_code);

	remove_auth();

	if (authed == PJ_SUCCESS) {
		if (stale) {
			return AUTH_STALE;
		} else {
			return AUTH_SUCCESS;
		}
	}

	if (authed == PJSIP_EAUTHNOAUTH) {
		return AUTH_NOAUTH;
	}

	return AUTH_FAIL;
}

/*!
 * \brief astobj2 callback for adding digest challenges to responses
 *
 * \param realm An auth's realm to build a challenge from
 * \param tdata The response to add the challenge to
 * \param rdata The request the challenge is in response to
 * \param is_stale Indicates whether nonce on incoming request was stale
 */
static void challenge(const char *realm, pjsip_tx_data *tdata, const pjsip_rx_data *rdata, int is_stale)
{
	pj_str_t qop;
	pj_str_t pj_nonce;
	pjsip_auth_srv auth_server;
	struct ast_str *nonce = ast_str_alloca(256);
	char time_buf[32];
	time_t timestamp = time(NULL);
	snprintf(time_buf, sizeof(time_buf), "%d", (int) timestamp);

	build_nonce(&nonce, time_buf, rdata, realm);

	setup_auth_srv(tdata->pool, &auth_server, realm);

	pj_cstr(&pj_nonce, ast_str_buffer(nonce));
	pj_cstr(&qop, "auth");
	pjsip_auth_srv_challenge(&auth_server, &qop, &pj_nonce, NULL, is_stale ? PJ_TRUE : PJ_FALSE, tdata);
}

/*!
 * \brief Check authentication using Digest scheme
 *
 * This function will check an incoming message against configured authentication
 * options. If \b any of the incoming Authorization headers result in successful
 * authentication, then authentication is considered successful.
 *
 * \see ast_sip_check_authentication
 */
static enum ast_sip_check_auth_result digest_check_auth(struct ast_sip_endpoint *endpoint,
		pjsip_rx_data *rdata, pjsip_tx_data *tdata)
{
	struct ast_sip_auth **auths;
	enum digest_verify_result *verify_res;
	enum ast_sip_check_auth_result res;
	int i;
	int failures = 0;
	size_t auth_size;

	RAII_VAR(struct ast_sip_endpoint *, artificial_endpoint,
		 ast_sip_get_artificial_endpoint(), ao2_cleanup);

	auth_size = AST_VECTOR_SIZE(&endpoint->inbound_auths);

	auths = ast_alloca(auth_size * sizeof(*auths));
	verify_res = ast_alloca(auth_size * sizeof(*verify_res));

	if (!auths) {
		return AST_SIP_AUTHENTICATION_ERROR;
	}

	if (endpoint == artificial_endpoint) {
		auths[0] = ast_sip_get_artificial_auth();
	} else if (ast_sip_retrieve_auths(&endpoint->inbound_auths, auths)) {
		res = AST_SIP_AUTHENTICATION_ERROR;
		goto cleanup;
	}

	for (i = 0; i < auth_size; ++i) {
		if (ast_strlen_zero(auths[i]->realm)) {
			ast_string_field_set(auths[i], realm, "asterisk");
		}
		verify_res[i] = verify(auths[i], rdata, tdata->pool);
		if (verify_res[i] == AUTH_SUCCESS) {
			res = AST_SIP_AUTHENTICATION_SUCCESS;
			goto cleanup;
		}
		if (verify_res[i] == AUTH_FAIL) {
			failures++;
		}
	}

	for (i = 0; i < auth_size; ++i) {
		challenge(auths[i]->realm, tdata, rdata, verify_res[i] == AUTH_STALE);
	}

	if (failures == auth_size) {
		res = AST_SIP_AUTHENTICATION_FAILED;
	} else {
		res = AST_SIP_AUTHENTICATION_CHALLENGE;
	}

cleanup:
	ast_sip_cleanup_auths(auths, auth_size);
	return res;
}

static struct ast_sip_authenticator digest_authenticator = {
	.requires_authentication = digest_requires_authentication,
	.check_authentication = digest_check_auth,
};

static int build_entity_id(void)
{
	char *eid;

	eid = ao2_alloc(AST_UUID_STR_LEN, NULL);
	if (!eid) {
		return -1;
	}

	ast_uuid_generate_str(eid, AST_UUID_STR_LEN);
	ao2_global_obj_replace_unref(entity_id, eid);
	ao2_ref(eid, -1);
	return 0;
}

static int reload_module(void)
{
	if (build_entity_id()) {
		return -1;
	}
	return 0;
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	if (build_entity_id()) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_sip_register_authenticator(&digest_authenticator)) {
		ao2_global_obj_release(entity_id);
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_authenticator(&digest_authenticator);
	ao2_global_obj_release(entity_id);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP authentication resource",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
