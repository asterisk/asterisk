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
#include "asterisk/test.h"

/*!
 * \file
 * \brief PJSIP UAS Authentication
 *
 * This module handles authentication when Asterisk is the UAS.
 *
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

static char default_realm[AST_SIP_AUTH_MAX_REALM_LENGTH + 1];

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
 * \brief Store shallow copy authentication information in thread-local storage
 */
static int store_auth(const struct ast_sip_auth *auth)
{
	const struct ast_sip_auth **pointing;

	pointing = ast_threadstorage_get(&auth_store, sizeof(pointing));
	if (!pointing) {
		return -1;
	}

	*pointing = auth;
	return 0;
}

/*!
 * \brief Remove shallow copy authentication information from thread-local storage
 */
static int remove_auth(void)
{
	struct ast_sip_auth **pointing;

	pointing = ast_threadstorage_get(&auth_store, sizeof(pointing));
	if (!pointing) {
		return -1;
	}

	*pointing = NULL;
	return 0;
}

/*!
 * \brief Retrieve shallow copy authentication information from thread-local storage
 */
static const struct ast_sip_auth *get_auth(void)
{
	struct ast_sip_auth **auth;

	auth = ast_threadstorage_get(&auth_store, sizeof(auth));
	if (auth) {
		return *auth;
	}
	return NULL;
}

static struct pjsip_authorization_hdr *get_authorization_hdr(
	const char *auth_id, const char *realm, const pjsip_rx_data *rdata)
{
	const char *src_name = rdata->pkt_info.src_name;
	struct pjsip_authorization_hdr *auth_hdr =
		(pjsip_authorization_hdr *) &rdata->msg_info.msg->hdr;
	SCOPE_ENTER(3, "%s:%s: realm: %s\n", auth_id, src_name, realm);

	while ((auth_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg,
		PJSIP_H_AUTHORIZATION, auth_hdr ? auth_hdr->next : NULL))) {
		if (pj_strcmp2(&auth_hdr->credential.common.realm, realm) == 0) {
			SCOPE_EXIT_RTN_VALUE(auth_hdr, "%s:%s: realm: %s Found header\n",
				auth_id, src_name, realm);
		}
	}
	SCOPE_EXIT_RTN_VALUE(NULL, "%s:%s: realm: %s No auth header found\n",
		auth_id, src_name, realm);
}

/*!
 * \brief Lookup callback for authentication verification
 *
 * This function is called when we call pjsip_auth_srv_verify(). It
 * expects us to verify that the realm and account name from the
 * Authorization header are correct and that we can support the digest
 * algorithm specified. We are then supposed to supply a password or
 * password_digest for the algorithm.
 *
 * The auth object must have previously been saved to thread-local storage.
 *
 * \param pool A memory pool we can use for allocations
 * \param param Contains the realm, username, rdata and auth header
 * \param cred_info The credentials we need to fill in
 * \retval PJ_SUCCESS Successful authentication
 * \retval other Unsuccessful
 */
static pj_status_t digest_lookup(pj_pool_t *pool,
	const pjsip_auth_lookup_cred_param *param,
	pjsip_cred_info *cred_info)
{
	const struct ast_sip_auth *auth = get_auth();
	const char *realm = S_OR(auth->realm, default_realm);
	const char *creds;
	const char *auth_name = (auth ? ast_sorcery_object_get_id(auth) : "none");
	struct pjsip_authorization_hdr *auth_hdr = get_authorization_hdr(auth_name, realm, param->rdata);
	const pjsip_auth_algorithm *algorithm = auth_hdr ?
		ast_sip_auth_get_algorithm_by_iana_name(&auth_hdr->credential.digest.algorithm) : NULL;
	const char *src_name = param->rdata->pkt_info.src_name;
	SCOPE_ENTER(4, "%s:%s:"
		" srv realm: " PJSTR_PRINTF_SPEC
		" auth realm: %s"
		" auth user: %s"
		" hdr user: " PJSTR_PRINTF_SPEC
		"\n",
		auth_name, src_name,
		PJSTR_PRINTF_VAR(param->realm),
		realm,
		auth->auth_user,
		PJSTR_PRINTF_VAR(param->acc_name));

	/*
	 * If a client is responding correctly, most of the error conditions below
	 * can't happen because we sent them the correct info in the 401 response.
	 * However, if a client is trying to authenticate with us without
	 * having received a challenge or if they are trying to
	 * authenticate with a different realm or algorithm than we sent them,
	 * we need to catch that.
	 */

	if (!auth) {
		/* This can only happen if the auth object was not saved to thread-local storage */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN, "%s:%s: No auth object found\n",
			auth_name, src_name);
	}

	if (auth_hdr == NULL) {
		/*
		 * This can only happen if the incoming request did not have an
		 * Authorization header or the realm in the header was missing or incorrect.
		 */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN,
			"%s:%s: No Authorization header found for realm '%s'\n",
			auth_name, src_name, realm);
	}

	if (algorithm == NULL) {
		/*
		 * This can only happen if the incoming request had an algorithm
		 * we don't support.
		 */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN,
			"%s:%s: Unsupported algorithm '" PJSTR_PRINTF_SPEC "'\n",
			auth_name, src_name, PJSTR_PRINTF_VAR(auth_hdr->credential.digest.algorithm));
	}

	if (auth->type == AST_SIP_AUTH_TYPE_ARTIFICIAL) {
		/*
		 * This shouldn't happen because this function can only be invoked
		 * if there was an Authorization header in the incoming request.
		 */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN, "%s:%s: Artificial auth object\n",
			auth_name, src_name);
	}

	if (pj_strcmp2(&param->realm, realm) != 0) {
		/*
		 * This shouldn't happen because param->realm was passed in from the auth
		 * when we called pjsip_auth_srv_init2.
		 */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN, "%s:%s: Realm '%s' mismatch\n",
			auth_name, src_name, realm);
	}

	if (pj_strcmp2(&param->acc_name, auth->auth_user) != 0) {
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN, "%s:%s: Username '%s' mismatch\n",
			auth_name, src_name, auth->auth_user);
	}

	if (!ast_sip_auth_is_algorithm_available(auth, &auth->supported_algorithms_uas,
		algorithm->algorithm_type)) {
		/*
		 * This shouldn't happen because we shouldn't have sent a challenge for
		 * an unsupported algorithm.
		 */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN, "%s:%s: Algorithm '" PJSTR_PRINTF_SPEC
			"' not supported or auth doesn't contain appropriate credentials\n",
			auth_name, src_name, PJSTR_PRINTF_VAR(algorithm->iana_name));
	}

	pj_strdup2(pool, &cred_info->realm, realm);
	pj_strdup2(pool, &cred_info->username, auth->auth_user);

	creds = ast_sip_auth_get_creds(auth, algorithm->algorithm_type, &cred_info->data_type);
	if (!creds) {
		/*
		 * This shouldn't happen because we checked the auth object when we
		 * loaded it to make sure it had the appropriate credentials for each
		 * algorithm in supported_algorithms_uas.
		 */
		SCOPE_EXIT_RTN_VALUE(PJSIP_SC_FORBIDDEN, "%s:%s: No plain text or digest password found for algorithm '" PJSTR_PRINTF_SPEC "'\n",
			auth_name, src_name, PJSTR_PRINTF_VAR(algorithm->iana_name));
	}
	pj_strdup2(pool, &cred_info->data, creds);
#ifdef HAVE_PJSIP_AUTH_NEW_DIGESTS
	if (cred_info->data_type == PJSIP_CRED_DATA_DIGEST) {
		cred_info->algorithm_type = algorithm->algorithm_type;
	}
#endif

	SCOPE_EXIT_RTN_VALUE(PJ_SUCCESS, "%s:%s: Success.  Data type: %s  Algorithm '" PJSTR_PRINTF_SPEC "'\n",
		auth_name, src_name, cred_info->data_type ? "digest" : "plain text", PJSTR_PRINTF_VAR(algorithm->iana_name));
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
 * \param nonce
 * \param timestamp A UNIX timestamp expressed as a string
 * \param rdata The incoming request
 * \param realm The realm for which authentication should occur
 */
static int build_nonce(struct ast_str **nonce, const char *timestamp,
	const pjsip_rx_data *rdata, const char *realm)
{
	struct ast_str *str = ast_str_alloca(256);
	RAII_VAR(char *, eid, ao2_global_obj_ref(entity_id), ao2_cleanup);
	char hash[33];

	/*
	 * Note you may be tempted to think why not include the port. The reason
	 * is that when using TCP the port can potentially differ from before.
	 */
	ast_str_append(&str, 0, "%s", timestamp);
	ast_str_append(&str, 0, ":%s", rdata->pkt_info.src_name);
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

	build_nonce(&calculated, timestamp, rdata, S_OR(auth->realm, default_realm));
	ast_debug(3, "Calculated nonce %s. Actual nonce is %s\n", ast_str_buffer(calculated), candidate);
	if (strcmp(ast_str_buffer(calculated), candidate)) {
		return 0;
	}
	return 1;
}

/*!
 * \brief Result of digest verification
 */
enum digest_verify_result {
	/*! Authentication credentials incorrect */
	AUTH_FAIL = 0,
	/*! Authentication credentials correct */
	AUTH_SUCCESS,
	/*! Authentication credentials correct but nonce mismatch */
	AUTH_STALE,
	/*! Authentication credentials were not provided */
	AUTH_NOAUTH,
};

static char *verify_result_str[] = {
	"FAIL",
	"SUCCESS",
	"STALE",
	"NOAUTH"
};

static enum digest_verify_result find_authorization(const char *endpoint_id,
	const struct ast_sip_auth *auth, const pjsip_rx_data *rdata)
{
	const char *auth_id = ast_sorcery_object_get_id(auth);
	const char *src_name = rdata->pkt_info.src_name;
	const char *realm = S_OR(auth->realm, default_realm);
	struct pjsip_authorization_hdr *auth_hdr =
		(pjsip_authorization_hdr *) &rdata->msg_info.msg->hdr;
	enum digest_verify_result res = AUTH_NOAUTH;
	int authorization_found = 0;
	char nonce[64];
	SCOPE_ENTER(3, "%s:%s:%s: realm: %s\n",
		endpoint_id, auth_id, src_name, realm);

	while ((auth_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg,
		PJSIP_H_AUTHORIZATION, auth_hdr ? auth_hdr->next : NULL))) {
		ast_copy_pj_str(nonce, &auth_hdr->credential.digest.nonce, sizeof(nonce));
		ast_trace(-1, "%s:%s:%s: Checking nonce %s  hdr-realm: " PJSTR_PRINTF_SPEC "  hdr-algo: " PJSTR_PRINTF_SPEC " \n",
			endpoint_id, auth_id, src_name, nonce,
			PJSTR_PRINTF_VAR(auth_hdr->credential.digest.realm),
			PJSTR_PRINTF_VAR(auth_hdr->credential.digest.algorithm));
		authorization_found++;
		if (check_nonce(nonce, rdata, auth)
			&& pj_strcmp2(&auth_hdr->credential.digest.realm, realm) == 0) {
			res = AUTH_SUCCESS;
			break;
		} else {
			res = AUTH_STALE;
		}
	}
	if (!authorization_found) {
		ast_trace(-1, "%s:%s:%s: No Authorization header found\n",
			endpoint_id, auth_id, src_name);
		res = AUTH_NOAUTH;
	}

	SCOPE_EXIT_RTN_VALUE(res, "%s:%s:%s: realm: %s Result %s\n",
		endpoint_id, auth_id, src_name, realm, verify_result_str[res]);
}

/*!
 * \brief Common code for initializing a pjsip_auth_srv
 */
static void setup_auth_srv(pj_pool_t *pool, pjsip_auth_srv *auth_server, const char *realm)
{
	pjsip_auth_srv_init_param *param = pj_pool_alloc(pool, sizeof(*param));
	pj_str_t *pj_realm = pj_pool_alloc(pool, sizeof(*pj_realm));

	pj_cstr(pj_realm, realm);
	param->realm = pj_realm;
	param->lookup2 = digest_lookup;
	param->options = 0;

	pjsip_auth_srv_init2(pool, auth_server, param);
}

/*!
 * \brief Verify incoming credentials
 *
 * \param endpoint_id  For logging
 * \param auth         The ast_sip_auth to check against
 * \param rdata        The incoming request
 * \param pool         A pool to use for the auth server
 * \return One of digest_verify_result
 */
static int verify(const char *endpoint_id, const struct ast_sip_auth *auth,
	pjsip_rx_data *rdata, pj_pool_t *pool)
{
	const char *auth_id = ast_sorcery_object_get_id(auth);
	const char *realm = S_OR(auth->realm, default_realm);
	const char *src_name = rdata->pkt_info.src_name;
	pj_status_t authed;
	int response_code;
	pjsip_auth_srv auth_server;
	int stale = 0;
	enum digest_verify_result res = AUTH_FAIL;
	SCOPE_ENTER(3, "%s:%s:%s: realm: %s\n",
		endpoint_id, auth_id, src_name, realm);

	res = find_authorization(endpoint_id, auth, rdata);
	if (res == AUTH_NOAUTH)
	{
		ast_test_suite_event_notify("INCOMING_AUTH_VERIFY_RESULT",
			"Realm: %s\r\n"
			"Username: %s\r\n"
			"Status: %s",
			realm, auth->auth_user, verify_result_str[res]);
		SCOPE_EXIT_RTN_VALUE(res, "%s:%s:%s: No Authorization header found\n",
			endpoint_id, auth_id, src_name);
	}

	if (res == AUTH_STALE) {
		/* Couldn't find an authorization with a sane nonce.
		 * Nonce mismatch may just be due to staleness.
		 */
		stale = 1;
	}

	setup_auth_srv(pool, &auth_server, realm);
	store_auth(auth);
	/* pjsip_auth_srv_verify will invoke digest_lookup */
	authed = SCOPE_CALL_WITH_RESULT(-1, pj_status_t, pjsip_auth_srv_verify, &auth_server, rdata, &response_code);
	remove_auth();
	if (authed == PJ_SUCCESS) {
		if (stale) {
			res = AUTH_STALE;
		} else {
			res = AUTH_SUCCESS;
		}
	} else {
		char err[256];
		res = AUTH_FAIL;
		pj_strerror(authed, err, sizeof(err));
		ast_trace(-1, "%s:%s:%s: authed: %s\n", endpoint_id, auth_id, src_name, err);
	}

	ast_test_suite_event_notify("INCOMING_AUTH_VERIFY_RESULT",
		"Realm: %s\r\n"
		"Username: %s\r\n"
		"Status: %s",
		realm, auth->auth_user, verify_result_str[res]);

	SCOPE_EXIT_RTN_VALUE(res, "%s:%s:%s: Realm: %s  Username: %s  Result: %s\n",
		endpoint_id, auth_id, src_name, realm,
		auth->auth_user, verify_result_str[res]);
}

/*!
 * \brief Send a WWW-Authenticate challenge
 *
 * \param endpoint_id  For logging
 * \param auth The auth object to use for the challenge
 * \param tdata The response to add the challenge to
 * \param rdata The request the challenge is in response to
 * \param is_stale Indicates whether nonce on incoming request was stale
 * \param algorithm_type The algorithm to use for the challenge
 */
static void challenge(const char *endpoint_id, struct ast_sip_auth *auth,
	pjsip_tx_data *tdata, const pjsip_rx_data *rdata, int is_stale,
	const pjsip_auth_algorithm *algorithm)
{
	pj_str_t qop;
	pj_str_t pj_nonce;
	pjsip_auth_srv auth_server;
	struct ast_str *nonce = ast_str_alloca(256);
	char time_buf[32];
	time_t timestamp = time(NULL);
	pj_status_t res;
	const char *realm = S_OR(auth->realm, default_realm);
	const char *auth_id = ast_sorcery_object_get_id(auth);
	const char *src_name = rdata->pkt_info.src_name;
	SCOPE_ENTER(5, "%s:%s:%s: realm: %s time: %d algorithm: " PJSTR_PRINTF_SPEC " stale? %s\n",
		endpoint_id, auth_id, src_name, realm, (int)timestamp,
		PJSTR_PRINTF_VAR(algorithm->iana_name), is_stale ? "yes" : "no");

	snprintf(time_buf, sizeof(time_buf), "%d", (int) timestamp);

	build_nonce(&nonce, time_buf, rdata, realm);

	setup_auth_srv(tdata->pool, &auth_server, realm);

	pj_cstr(&pj_nonce, ast_str_buffer(nonce));
	pj_cstr(&qop, "auth");
#ifdef HAVE_PJSIP_AUTH_NEW_DIGESTS
	res = pjsip_auth_srv_challenge2(&auth_server, &qop, &pj_nonce,
		NULL, is_stale ? PJ_TRUE : PJ_FALSE, tdata, algorithm->algorithm_type);
#else
	res = pjsip_auth_srv_challenge(&auth_server, &qop, &pj_nonce,
		NULL, is_stale ? PJ_TRUE : PJ_FALSE, tdata);
#endif
	SCOPE_EXIT_RTN("%s:%s:%s: Sending challenge for realm: %s algorithm: " PJSTR_PRINTF_SPEC
		" %s\n",
		endpoint_id, auth_id, src_name, realm, PJSTR_PRINTF_VAR(algorithm->iana_name),
		res == PJ_SUCCESS ? "succeeded" : "failed");
}

static char *check_auth_result_str[] = {
    "CHALLENGE",
    "SUCCESS",
    "FAILED",
    "ERROR",
};


/*!
 * \brief Check authentication using Digest scheme
 *
 * This function will check an incoming message against configured authentication
 * options. If \b any of the incoming Authorization headers result in successful
 * authentication, then authentication is considered successful.
 *
 * \warning The return code from the function is used by the distributor to
 * determine which log messages (if any) are emitted.  Many admins will be
 * using log parsers like fail2ban to block IPs that are repeatedly failing
 * to authenticate so changing the return code could have unintended
 * consequences.
 *
 * \retval AST_SIP_AUTHENTICATION_SUCCESS There was an Authorization header
 * in the request and it verified successfully with at least one auth object
 * on the endpoint.  No further challenges sent.
 *
 * \retval AST_SIP_AUTHENTICATION_CHALLENGE There was NO Authorization header
 * in the incoming request.  We sent a 401 with one or more challenges.
 *
 * \retval AST_SIP_AUTHENTICATION_FAILED There were one or more Authorization
 * headers in the request but they all failed to verify with any auth object
 * on the endpoint. We sent a 401 with one or more challenges.
 *
 * \retval AST_SIP_AUTHENTICATION_ERROR An internal error occurred. No challenges
 * were sent.
 *
 * \see ast_sip_check_authentication
 */
static enum ast_sip_check_auth_result digest_check_auth(struct ast_sip_endpoint *endpoint,
		pjsip_rx_data *rdata, pjsip_tx_data *tdata)
{
	struct ast_sip_auth **auths;
	enum digest_verify_result *verify_res;
	struct ast_sip_endpoint *artificial_endpoint;
	enum ast_sip_check_auth_result res = AST_SIP_AUTHENTICATION_ERROR;
	int idx;
	int is_artificial;
	int failures = 0;
	size_t auth_size;
	const char *endpoint_id = ast_sorcery_object_get_id(endpoint);
	char *src_name = rdata->pkt_info.src_name;
	SCOPE_ENTER(3, "%s:%s\n", endpoint_id, src_name);

	auth_size = AST_VECTOR_SIZE(&endpoint->inbound_auths);
	ast_assert(0 < auth_size);

	auths = ast_alloca(auth_size * sizeof(*auths));
	verify_res = ast_alloca(auth_size * sizeof(*verify_res));

	artificial_endpoint = ast_sip_get_artificial_endpoint();
	if (!artificial_endpoint) {
		/* Should not happen except possibly if we are shutting down. */
		SCOPE_EXIT_RTN_VALUE(AST_SIP_AUTHENTICATION_ERROR);
	}

	is_artificial = endpoint == artificial_endpoint;
	ao2_ref(artificial_endpoint, -1);
	if (is_artificial) {
		ast_trace(3, "%s:%s: Using artificial endpoint for authentication\n",
			endpoint_id, src_name);
		ast_assert(auth_size == 1);
		auths[0] = ast_sip_get_artificial_auth();
		if (!auths[0]) {
			/* Should not happen except possibly if we are shutting down. */
			SCOPE_EXIT_RTN_VALUE(AST_SIP_AUTHENTICATION_ERROR);
		}
	} else {
		ast_trace(3, "%s:%s: Using endpoint for authentication\n",
			endpoint_id, src_name);
		memset(auths, 0, auth_size * sizeof(*auths));
		/*
		 * If ast_sip_retrieve_auths returns a failure we still need
		 * to cleanup the auths array because it may have been partially
		 * filled in.
		 */
		if (ast_sip_retrieve_auths(&endpoint->inbound_auths, auths)) {
			ast_sip_cleanup_auths(auths, auth_size);
			SCOPE_EXIT_RTN_VALUE(AST_SIP_AUTHENTICATION_ERROR,
				"%s:%s: Failed to retrieve some or all auth objects from endpoint\n",
				endpoint_id, src_name);
		}
	}

	/*
	 * Verify any Authorization headers in the incoming request against the
	 * auth objects on the endpoint.  If there aren't any Authorization headers
	 * verify() will return AUTH_NOAUTH.
	 *
	 * NOTE:  The only reason to use multiple auth objects as a UAS might
	 * be to send challenges for multiple realms however we currently don't
	 * know of anyone actually doing this.
	 */
	for (idx = 0; idx < auth_size; ++idx) {
		struct ast_sip_auth *auth = auths[idx];
		const char *auth_id = ast_sorcery_object_get_id(auth);
		SCOPE_ENTER(4, "%s:%s:%s: Auth %d of %d: Verifying\n",
			endpoint_id, auth_id, src_name, idx + 1, (int)auth_size);

		verify_res[idx] = SCOPE_CALL_WITH_RESULT(-1, int, verify, endpoint_id, auth, rdata, tdata->pool);
		switch((int)verify_res[idx]) {
		case AUTH_SUCCESS:
			res = AST_SIP_AUTHENTICATION_SUCCESS;
			break;
		case AUTH_FAIL:
			failures++;
			break;
		case AUTH_NOAUTH:
		case AUTH_STALE:
			break;
		}

		SCOPE_EXIT("%s:%s:%s: Auth %d of %d: Result: %s  Failure count: %d\n",
			endpoint_id, auth_id, src_name, idx + 1, (int)auth_size,
			verify_result_str[verify_res[idx]], failures);

		/*
		 * If there was a success or there was no Authorization header in the
		 * incoming request, we can stop verifying the rest of the auth objects.
		 */
		if (verify_res[idx] == AUTH_SUCCESS || verify_res[idx] == AUTH_NOAUTH) {
			break;
		}
	}

	if (res == AST_SIP_AUTHENTICATION_SUCCESS) {
		ast_sip_cleanup_auths(auths, auth_size);
		SCOPE_EXIT_RTN_VALUE(res, "%s:%s: Result: %s\n",
			endpoint_id, src_name,
			check_auth_result_str[res]);
	}
	ast_trace(-1, "%s:%s: Done with verification. Failures: %d of %d\n",
		endpoint_id, src_name, failures, (int)auth_size);

	/*
	 * If none of the Authorization headers in the incoming request were
	 * successfully verified, or there were no Authorization headers in the
	 * request, we need to send challenges for each auth object
	 * on the endpoint.
	 */
	for (idx = 0; idx < auth_size; ++idx) {
		int i = 0;
		struct ast_sip_auth *auth = auths[idx];
		const char *realm = S_OR(auth->realm, default_realm);
		const char *auth_id = ast_sorcery_object_get_id(auth);
		SCOPE_ENTER(4, "%s:%s:%s: Auth %d of %d: Sending challenges\n",
			endpoint_id, auth_id, src_name, idx + 1, (int)auth_size);

		for (i = 0; i < AST_VECTOR_SIZE(&auth->supported_algorithms_uas); i++) {
			pjsip_auth_algorithm_type algorithm_type = AST_VECTOR_GET(&auth->supported_algorithms_uas, i);
			const pjsip_auth_algorithm *algorithm = ast_sip_auth_get_algorithm_by_type(algorithm_type);
			pjsip_www_authenticate_hdr *auth_hdr = NULL;
			int already_sent_challenge = 0;
			SCOPE_ENTER(5, "%s:%s:%s: Auth %d of %d: Challenging with " PJSTR_PRINTF_SPEC "\n",
				endpoint_id, auth_id, src_name, idx + 1, (int)auth_size,
				PJSTR_PRINTF_VAR(algorithm->iana_name));

			/*
			 * Per RFC 7616, if we've already sent a challenge for this realm
			 * and algorithm, we must not send another.
			 */
			while ((auth_hdr = pjsip_msg_find_hdr(tdata->msg,
				PJSIP_H_WWW_AUTHENTICATE, auth_hdr ? auth_hdr->next : NULL))) {
				if (pj_strcmp2(&auth_hdr->challenge.common.realm, realm) == 0 &&
					!pj_stricmp(&auth_hdr->challenge.digest.algorithm, &algorithm->iana_name)) {
					ast_trace(-1, "%s:%s:%s: Auth %d of %d: Not sending duplicate challenge for realm: %s algorithm: "
						PJSTR_PRINTF_SPEC "\n",
						endpoint_id, auth_id, src_name, idx + 1, (int)auth_size,
						realm, PJSTR_PRINTF_VAR(algorithm->iana_name));
					already_sent_challenge = 1;
				}
			}
			if (already_sent_challenge) {
				SCOPE_EXIT_EXPR(continue);
			}

			SCOPE_CALL(5, challenge, endpoint_id, auth, tdata, rdata,
				verify_res[idx] == AUTH_STALE, algorithm);
			res = AST_SIP_AUTHENTICATION_CHALLENGE;

			SCOPE_EXIT("%s:%s:%s: Auth %d of %d: Challenged with " PJSTR_PRINTF_SPEC "\n",
				endpoint_id, auth_id, src_name, idx + 1, (int)auth_size,
				PJSTR_PRINTF_VAR(algorithm->iana_name));
		}
		SCOPE_EXIT("%s:%s:%s: Auth %d of %d: Done with challenges\n",
			endpoint_id, auth_id, src_name, idx + 1, (int)auth_size);
	}

	/*
	 * If we've sent challenges for multiple auth objects, we currently
	 * return SUCCESS when the first one succeeds. We may want to change
	 * this in the future to require that all succeed but as stated above,
	 * currently we don't have a use case for even using more than one
	 * auth object as a UAS.
	 */

	/*
	 * If the authentication failed for any reason, we want to send
	 * a 401 with a challenge.  If it was because there was no
	 * Authorization header or there was a stale nonce, fine.  That's not
	 * unusual so we return AST_SIP_AUTHENTICATION_CHALLENGE.  If it
	 * failed because of a user/password mismatch then we return
	 * AST_SIP_AUTHENTICATION_FAILED which causes the distributor to
	 * print a "Failed to authenticate" message.
	 */
	if (failures == auth_size) {
		res = AST_SIP_AUTHENTICATION_FAILED;
	}

	ast_sip_cleanup_auths(auths, auth_size);
	SCOPE_EXIT_RTN_VALUE(res, "%s:%s: Result: %s\n",
		endpoint_id, src_name,
		check_auth_result_str[res]);

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

static void global_loaded(const char *object_type)
{
	ast_sip_get_default_realm(default_realm, sizeof(default_realm));
}

/*! \brief Observer which is used to update our default_realm when the global setting changes */
static struct ast_sorcery_observer global_observer = {
	.loaded = global_loaded,
};

static int reload_module(void)
{
	if (build_entity_id()) {
		return -1;
	}
	return 0;
}

static int load_module(void)
{
	if (build_entity_id()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	if (ast_sip_register_authenticator(&digest_authenticator)) {
		ao2_global_obj_release(entity_id);
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sip_unregister_authenticator(&digest_authenticator);
	ao2_global_obj_release(entity_id);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP authentication resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
	.requires = "res_pjsip",
);
