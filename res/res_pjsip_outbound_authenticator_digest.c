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

/*!
 * \file
 * \brief PJSIP UAC Authentication
 *
 * This module handles authentication when Asterisk is the UAC.
 *
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
#include "asterisk/vector.h"

/*!
 * \internal
 * \brief Determine proper authenticate header
 *
 * We need to search for different headers depending on whether
 * the response code from the UAS/Proxy was 401 or 407.
 */
static pjsip_hdr_e get_auth_search_type(pjsip_rx_data *challenge)
{
	if (challenge->msg_info.msg->line.status.code == PJSIP_SC_UNAUTHORIZED) {
		return PJSIP_H_WWW_AUTHENTICATE;
	} else if (challenge->msg_info.msg->line.status.code == PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED) {
		return PJSIP_H_PROXY_AUTHENTICATE;
	} else {
		ast_log(LOG_ERROR,
				"Status code %d was received when it should have been 401 or 407.\n",
				challenge->msg_info.msg->line.status.code);
		return PJSIP_H_OTHER;
	}
}

/*!
 * \internal
 * \brief Determine if digest algorithm in the header is one supported by
 * pjproject and OpenSSL.
 */
static const pjsip_auth_algorithm *get_supported_algorithm(pjsip_www_authenticate_hdr *auth_hdr)
{
	const pjsip_auth_algorithm *algo = NULL;

	algo = ast_sip_auth_get_algorithm_by_iana_name(&auth_hdr->challenge.digest.algorithm);
	if (!algo) {
		return NULL;
	}

	if (ast_sip_auth_is_algorithm_supported(algo->algorithm_type)) {
		return algo;
	}
	return NULL;
}

AST_VECTOR(cred_info_vector, pjsip_cred_info);

/*!
 * \brief Get credentials (if any) from auth objects for a WWW/Proxy-Authenticate header
 *
 * \param id                   For logging
 * \param src_name             For logging
 * \param auth_hdr             The *-Authenticate header to check
 * \param auth_object_count    The number of auth objects available
 * \param auth_objects_vector  The vector of available auth objects
 * \param auth_creds           The vector to store the credentials in
 * \param realms               For logging
 *
 */
static void get_creds_for_header(const char *id, const char *src_name,
	pjsip_www_authenticate_hdr *auth_hdr, size_t auth_object_count,
	const struct ast_sip_auth_objects_vector *auth_objects_vector,
	struct cred_info_vector *auth_creds, struct ast_str **realms)
{
	int exact_match_index = -1;
	int wildcard_match_index = -1;
	struct ast_sip_auth *found_auth = NULL;
	const pjsip_auth_algorithm *challenge_algorithm =
		get_supported_algorithm(auth_hdr);
	int i = 0;
	pjsip_cred_info auth_cred;
	const char *cred_data;
	int res = 0;
	SCOPE_ENTER(4, "%s:%s: Testing header realm: '" PJSTR_PRINTF_SPEC "' algorithm: '"
		PJSTR_PRINTF_SPEC "'\n", id, src_name,
		PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm),
		PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.algorithm));

	if (!challenge_algorithm) {
		SCOPE_EXIT_RTN("%s:%s: Skipping header with realm '" PJSTR_PRINTF_SPEC "' "
			"and unsupported " PJSTR_PRINTF_SPEC "' algorithm \n", id, src_name,
			PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm),
			PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.algorithm));
	}

	/*
	 * If we already have credentials for this realm, we don't need to
	 * process this header.  We can just skip it.
	 */
	for (i = 0; i < AST_VECTOR_SIZE(auth_creds); i++) {
		pjsip_cred_info auth_cred = AST_VECTOR_GET(auth_creds, i);
		if (pj_stricmp(&auth_cred.realm, &auth_hdr->challenge.common.realm) == 0) {
			SCOPE_EXIT_RTN("%s:%s: Skipping header with realm '" PJSTR_PRINTF_SPEC "' "
				"because we already have credentials for it\n", id, src_name,
				PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm));
		}
	}

	/*
	 * Appending "realm/agorithm" to realms is strictly so
	 * digest_create_request_with_auth() can display good error messages.
	 */
	if (*realms) {
		ast_str_append(realms, 0, PJSTR_PRINTF_SPEC "/" PJSTR_PRINTF_SPEC ", ",
			PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm),
			PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.algorithm));
	}

	/*
	 * Now that we have a valid header, we can loop over the auths available to
	 * find either an exact realm match or, failing that, a wildcard auth (an
	 * auth with an empty or "*" realm).
	 *
	 * NOTE: We never use the global default realm when we're the UAC responding
	 * to a 401 or 407.  We only use that when we're the UAS (handled elsewhere)
	 * and the auth object didn't have a realm.
	 */
	ast_trace(-1, "%s:%s: Searching %zu auths to find matching ones for header with realm "
		"'" PJSTR_PRINTF_SPEC "' and algorithm '" PJSTR_PRINTF_SPEC "'\n",
		id, src_name, auth_object_count,
		PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm),
		PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.algorithm));

	for (i = 0; i < auth_object_count; ++i) {
		struct ast_sip_auth *auth = AST_VECTOR_GET(auth_objects_vector, i);
		const char *auth_id = ast_sorcery_object_get_id(auth);
		SCOPE_ENTER(5, "%s:%s: Checking auth '%s' with realm '%s'\n",
			id, src_name, auth_id, auth->realm);

		/*
		 * Is the challenge algorithm in the auth's supported_algorithms_uac
		 * and is there either a plain text password or a password_digest
		 * for the algorithm?
		 */
		if (!ast_sip_auth_is_algorithm_available(auth, &auth->supported_algorithms_uac,
			challenge_algorithm->algorithm_type)) {
			SCOPE_EXIT_EXPR(continue, "%s:%s: Skipping auth '%s' with realm '%s' because it doesn't support "
				" algorithm '" PJSTR_PRINTF_SPEC "'\n", id, src_name,
				auth_id, auth->realm,
				PJSTR_PRINTF_VAR(challenge_algorithm->iana_name));
		}

		/*
		 * If this auth object's realm exactly matches the one
		 * from the header, we can just break out and use it.
		 *
		 * NOTE: If there's more than one auth object for an endpoint with
		 * a matching realm it's a misconfiguration.  We'll only use the first.
		 */
		if (pj_stricmp2(&auth_hdr->challenge.digest.realm, auth->realm) == 0) {
			exact_match_index = i;
			/*
			 * If we found an exact realm match, there's no need to keep
			 * looking for a wildcard.
			 */
			SCOPE_EXIT_EXPR(break, "%s:%s: Found matching auth '%s' with realm '%s'\n",
				id, src_name, auth_id, auth->realm);
		}

		/*
		 * If this auth object's realm is empty or a "*", it's a wildcard
		 * auth object.  We going to save its index but keep iterating over
		 * the vector in case we find an exact match later.
		 *
		 * NOTE: If there's more than one wildcard auth object for an endpoint
		 * it's a misconfiguration.  We'll only use the first.
		 */
		if (wildcard_match_index < 0
			&& (ast_strlen_zero(auth->realm) || ast_strings_equal(auth->realm, "*"))) {
			ast_trace(-1, "%s:%s: Found wildcard auth '%s' for realm '" PJSTR_PRINTF_SPEC "'\n",
				id, src_name, auth_id,
				PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm));
			wildcard_match_index = i;
		}
		SCOPE_EXIT("%s:%s: Done checking auth '%s' with realm '%s'. "
			"Found exact? %s  Found wildcard? %s\n", id, src_name,
			auth_id, auth->realm, exact_match_index >= 0 ? "yes" : "no",
				wildcard_match_index >= 0 ? "yes" : "no");
	} /* End auth object loop */

	if (exact_match_index < 0 && wildcard_match_index < 0) {
		/*
		 * Didn't find either a wildcard or an exact realm match.
		 * Move on to the next header.
		 */
		SCOPE_EXIT_RTN("%s:%s: No auth matching realm or no wildcard found for realm '" PJSTR_PRINTF_SPEC "'\n",
			id, src_name, PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm));
	}

	if (exact_match_index >= 0) {
		/*
		 * If we found an exact match, we'll always prefer that.
		 */
		found_auth = AST_VECTOR_GET(auth_objects_vector, exact_match_index);
		ast_trace(-1, "%s:%s: Using matched auth '%s' with realm '" PJSTR_PRINTF_SPEC "'\n",
			id, src_name, ast_sorcery_object_get_id(found_auth),
			PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm));
	} else {
		/*
		 * We'll only use the wildcard if we didn't find an exact match.
		 */
		found_auth = AST_VECTOR_GET(auth_objects_vector, wildcard_match_index);
		ast_trace(-1, "%s:%s: Using wildcard auth '%s' for realm '" PJSTR_PRINTF_SPEC "'\n",
			id, src_name, ast_sorcery_object_get_id(found_auth),
			PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm));
	}

	/*
	 * Now that we have an auth object to use, we need to create a
	 * pjsip_cred_info structure for each algorithm we support.
	 */

	memset(&auth_cred, 0, sizeof(auth_cred));
	/*
	 * Copy the fields from the auth_object to the
	 * pjsip_cred_info structure.
	 */
	auth_cred.realm = auth_hdr->challenge.common.realm;
	pj_cstr(&auth_cred.username, found_auth->auth_user);
	pj_cstr(&auth_cred.scheme, "digest");

	/*
	 * auth_cred.data_type tells us whether the credential is a plain text
	 * password or a pre-digested one.
	 */
	cred_data = SCOPE_CALL_WITH_RESULT(-1, const char *, ast_sip_auth_get_creds,
		found_auth, challenge_algorithm->algorithm_type, &auth_cred.data_type);
	/*
	 * This can't really fail because we already called
	 * ast_sip_auth_is_algorithm_available() for the auth
	 * but we check anyway.
	 */
	if (!cred_data) {
		SCOPE_EXIT_RTN("%s:%s: Shouldn't have happened\n", id, src_name);
	}

	pj_cstr(&auth_cred.data, cred_data);
#ifdef HAVE_PJSIP_AUTH_NEW_DIGESTS
	if (auth_cred.data_type == PJSIP_CRED_DATA_DIGEST) {
		auth_cred.algorithm_type = challenge_algorithm->algorithm_type;
	}
#endif
	/*
	 * Because the vector contains actual structures and not pointers
	 * to structures, the call to AST_VECTOR_APPEND results in a simple
	 * assign of one structure to another, effectively copying the auth_cred
	 * structure contents to the array element.
	 *
	 * Also note that the calls to pj_cstr above set their respective
	 * auth_cred fields to the _pointers_ of their corresponding auth
	 * object fields.  This is safe because the call to
	 * pjsip_auth_clt_set_credentials() below strdups them before we
	 * return to the calling function which decrements the reference
	 * counts.
	 */
	res = AST_VECTOR_APPEND(auth_creds, auth_cred);
	SCOPE_EXIT_RTN("%s:%s: %s credential for realm: '" PJSTR_PRINTF_SPEC "' algorithm: '"
		PJSTR_PRINTF_SPEC "'\n", id, src_name,
		res == 0 ? "Added" : "Failed to add",
		PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.realm),
		PJSTR_PRINTF_VAR(auth_hdr->challenge.digest.algorithm));
}

/*!
 * \internal
 * \brief Initialize pjproject with a valid set of credentials
 *
 * RFC7616 and RFC8760 allow more than one WWW-Authenticate or
 * Proxy-Authenticate header per realm, each with different digest
 * algorithms (including new ones like SHA-256 and SHA-512-256). However,
 * a UAS can NOT send back multiple Authenticate headers for
 * the same realm with the same digest algorithm.  The UAS is also
 * supposed to send the headers in order of preference with the first one
 * being the most preferred.
 *
 * We're supposed to send an Authorization header for the first one we
 * encounter for a realm that we can support.
 *
 * The UAS can also send multiple realms, especially when it's a proxy
 * that has forked the request in which case the proxy will aggregate all
 * of the Authenticate headers into one response back to the UAC.
 *
 * It doesn't stop there though... Each realm can require a different
 * username from the others. There's also nothing preventing each digest
 * algorithm from having a unique password although I'm not sure if
 * that adds any benefit.
 *
 * So now... For each WWW/Proxy-Authenticate header we encounter, we have to
 * determine if we support the digest algorithm and, if not, just skip the
 * header.  We then have to find an auth object that matches the realm AND
 * the digest algorithm or find a wildcard object that matches the digest
 * algorithm. If we find one, we add it to the results vector and read the
 * next Authenticate header. If the next header is for the same realm AND
 * we already added an auth object for that realm, we skip the header.
 * Otherwise we repeat the process for the next header.
 *
 * In the end, we'll have accumulated a list of credentials, one per realm,
 * we can pass to pjproject that it can use to add Authentication headers
 * to a request.
 */
static pj_status_t set_auth_creds(const char *id, pjsip_auth_clt_sess *auth_sess,
	const struct ast_sip_auth_objects_vector *auth_objects_vector,
	pjsip_rx_data *challenge, struct ast_str **realms)
{
	size_t auth_object_count;
	pjsip_www_authenticate_hdr *auth_hdr = NULL;
	pj_status_t res = PJ_SUCCESS;
	pjsip_hdr_e search_type;
	size_t cred_count = 0;
	pjsip_cred_info *creds_array;
	char *pj_err = NULL;
	const char *src_name = challenge->pkt_info.src_name;
	/*
	 * Normally vector elements are pointers to something else, usually
	 * structures. In this case however, the elements are the
	 * structures themselves instead of pointers to them.  This is due
	 * to the fact that pjsip_auth_clt_set_credentials() expects an
	 * array of structures, not an array of pointers to structures.
	 * Thankfully, vectors allow you to "steal" their underlying
	 * arrays, in this case an array of pjsip_cred_info structures,
	 * which we'll pass to pjsip_auth_clt_set_credentials() at the
	 * end.
	 */
	struct cred_info_vector auth_creds;
	SCOPE_ENTER(3, "%s:%s\n", id, src_name);

	search_type = get_auth_search_type(challenge);
	if (search_type == PJSIP_H_OTHER) {
		/*
		 * The status code on the response wasn't 401 or 407
		 * so there are no WWW-Authenticate or Proxy-Authenticate
		 * headers to process.
		 */
		SCOPE_EXIT_RTN_VALUE(PJ_ENOTSUP, "%s:%s: Status code %d was received when it should have been 401 or 407.\n",
		id, src_name, challenge->msg_info.msg->line.status.code);
	}

	auth_object_count = AST_VECTOR_SIZE(auth_objects_vector);
	if (auth_object_count == 0) {
		/* This shouldn't happen but we'll check anyway. */
		SCOPE_EXIT_RTN_VALUE(PJ_EINVAL, "%s:%s No auth objects available\n", id, src_name);
	}

	/*
	 * The number of pjsip_cred_infos we send to pjproject can
	 * vary based on the number of acceptable headers received
	 * and the number of acceptable auth objects on the endpoint
	 * so we just use a vector to accumulate them.
	 *
	 * NOTE: You have to call AST_VECTOR_FREE() on the vector
	 * but you don't have to free the elements because they're
	 * actual structures, not pointers to structures.
	 */
	if (AST_VECTOR_INIT(&auth_creds, 5) != 0) {
		SCOPE_EXIT_RTN_VALUE(PJ_ENOMEM);
	}

	/*
	 * There may be multiple WWW/Proxy-Authenticate headers each one having
	 * a different realm/algorithm pair. Test each to see if we have credentials
	 * for it and accumulate them in the auth_creds vector.
	 * The code doesn't really care but just for reference, RFC-7616 says
	 * a UAS can't send multiple headers for the same realm with the same
	 * algorithm.  It also says the UAS should send the headers in order
	 * of preference with the first one being the most preferred.
	 */
	while ((auth_hdr = pjsip_msg_find_hdr(challenge->msg_info.msg,
		search_type, auth_hdr ? auth_hdr->next : NULL))) {

		get_creds_for_header(id, src_name, auth_hdr, auth_object_count,
			auth_objects_vector, &auth_creds, realms);

	} /* End header loop */

	if (*realms && ast_str_strlen(*realms)) {
		/*
		 * Chop off the trailing ", " on the last realm-algorithm.
		 */
		ast_str_truncate(*realms, ast_str_strlen(*realms) - 2);
	}

	if (AST_VECTOR_SIZE(&auth_creds) == 0) {
		/* No matching auth objects were found. */
		res = PJSIP_ENOCREDENTIAL;
		goto cleanup;
	}

	/*
	 * Here's where we steal the cred info structures from the vector.
	 *
	 * The steal effectively returns a pointer to the underlying
	 * array of pjsip_cred_info structures which is exactly what we need
	 * to pass to pjsip_auth_clt_set_credentials().
	 *
	 * <struct cred info><struct cred info>...<struct cred info>
	 * ^pointer
	 *
	 * Since we stole the array from the vector, we have to free it ourselves.
	 *
	 * We also have to copy the size before we steal because stealing
	 * resets the vector size to 0.
	 */
	cred_count = AST_VECTOR_SIZE(&auth_creds);
	creds_array = AST_VECTOR_STEAL_ELEMENTS(&auth_creds);

	res = pjsip_auth_clt_set_credentials(auth_sess, cred_count, creds_array);
	ast_free(creds_array);

cleanup:
	AST_VECTOR_FREE(&auth_creds);
	if (res != PJ_SUCCESS) {
		pj_err = ast_alloca(PJ_ERR_MSG_SIZE);
		pj_strerror(res, pj_err, PJ_ERR_MSG_SIZE);
	}
	SCOPE_EXIT_RTN_VALUE(res, "%s:%s: Set %zu credentials in auth session: %s\n",
		id, src_name, cred_count, S_OR(pj_err, "success"));
}

/*!
 * \internal
 * \brief Create new tdata with auth based on original tdata
 * \param auth_ids_vector  Vector of auth IDs retrieved from endpoint
 * \param challenge rdata of the response from the UAS with challenge
 * \param old_request tdata from the original request
 * \param new_request tdata of the new request with the auth
 *
 * This function is what's registered with ast_sip_register_outbound_authenticator()
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int digest_create_request_with_auth(const struct ast_sip_auth_vector *auth_ids_vector,
	pjsip_rx_data *challenge, pjsip_tx_data *old_request, pjsip_tx_data **new_request)
{
	pjsip_auth_clt_sess auth_sess;
	pjsip_cseq_hdr *cseq;
	pj_status_t status;
	struct ast_sip_auth_objects_vector auth_objects_vector;
	size_t auth_object_count = 0;
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(challenge);
	struct ast_sip_endpoint *endpoint = (dlg ? ast_sip_dialog_get_endpoint(dlg) : NULL);
	/*
	 * We're ast_strdupa'ing the endpoint id because we're going to
	 * clean up the endpoint immediately after this. We only needed
	 * it to get the id for logging.
	 */
	char *endpoint_id = endpoint ? ast_strdupa(ast_sorcery_object_get_id(endpoint)) : NULL;
	char *id = endpoint_id ?: "noendpoint";
	char *src_name = challenge->pkt_info.src_name;
	struct ast_str *realms = NULL;
	int res = -1;
	char *pj_err = NULL;
	SCOPE_ENTER(3, "%s:%s\n", id, src_name);

	/* We only needed endpoint to get the id */
	ao2_cleanup(endpoint);

	/*
	 * Some older compilers have an issue with initializing structures with
	 * pjsip_auth_clt_sess auth_sess = { 0, };
	 * so we'll just do it the old fashioned way.
	 */
	memset(&auth_sess, 0, sizeof(auth_sess));

	if (!auth_ids_vector || AST_VECTOR_SIZE(auth_ids_vector) == 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s:%s: There were no auth ids available\n",
			id, src_name);
		return -1;
	}

	/*
	 * auth_ids_vector contains only ids but we need the complete objects.
	 */
	if (AST_VECTOR_INIT(&auth_objects_vector, AST_VECTOR_SIZE(auth_ids_vector)) != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s:%s: Couldn't initialize auth object vector\n",
			id, src_name);
	}

	/*
	 * We don't really care about ast_sip_retrieve_auths_vector()'s return code
	 * because we're checking the count of objects in the vector.
	 *
	 * Don't forget to call
	 * 	ast_sip_cleanup_auth_objects_vector(&auth_objects_vector);
	 *  AST_VECTOR_FREE(&auth_objects_vector);
	 * when you're done with the vector
	 */
	ast_trace(-1, "%s:%s: Retrieving %d auth objects\n", id, src_name,
		(int)AST_VECTOR_SIZE(auth_ids_vector));
	ast_sip_retrieve_auths_vector(auth_ids_vector, &auth_objects_vector);
	auth_object_count = AST_VECTOR_SIZE(&auth_objects_vector);
	if (auth_object_count == 0) {
		/*
		 * If none of the auth ids were found, we can't continue.
		 * We're OK if there's at least one left.
		 * ast_sip_retrieve_auths_vector() will print a warning for every
		 * id that wasn't found.
		 */
		res = -1;
		ast_trace(-1, "%s:%s: No auth objects found\n", id, src_name);
		goto cleanup;
	}
	ast_trace(-1, "%s:%s: Retrieved %d auth objects\n", id, src_name,
		(int)auth_object_count);

	status = pjsip_auth_clt_init(&auth_sess, ast_sip_get_pjsip_endpoint(),
		old_request->pool, 0);
	if (status != PJ_SUCCESS) {
		pj_err = ast_alloca(PJ_ERR_MSG_SIZE);
		pj_strerror(status, pj_err, PJ_ERR_MSG_SIZE);
		ast_log(LOG_ERROR, "%s:%s: Failed to initialize client authentication session: %s\n",
			id, src_name, pj_err);
		res = -1;
		goto cleanup;
	}

	/*
	 * realms is used only for displaying good error messages.
	 */
	realms = ast_str_create(32);
	if (!realms) {
		res = -1;
		goto cleanup;
	}

	/*
	 * Load pjproject with the valid credentials for the Authentication headers
	 * received on the 401 or 407 response.
	 */
	status = SCOPE_CALL_WITH_RESULT(-1, pj_status_t, set_auth_creds, id, &auth_sess, &auth_objects_vector, challenge, &realms);
	if (status != PJ_SUCCESS && status != PJSIP_ENOCREDENTIAL) {
		pj_err = ast_alloca(PJ_ERR_MSG_SIZE);
	}

	switch (status) {
	case PJ_SUCCESS:
		break;
	case PJSIP_ENOCREDENTIAL:
		ast_log(LOG_WARNING,
			"%s:%s: No auth objects matching realm/algorithm(s) '%s' from challenge found.\n",
			id, src_name, realms ? ast_str_buffer(realms) : "<none>");
		res = -1;
		goto cleanup;
	default:
		pj_strerror(status, pj_err, PJ_ERR_MSG_SIZE);
		ast_log(LOG_WARNING, "%s:%s: Failed to set authentication credentials: %s\n",
			id, src_name, pj_err);
		res = -1;
		goto cleanup;
	}

	/*
	 * reinit_req actually creates the Authorization headers to send on
	 * the next request.  If reinit_req already has a cached credential
	 * from an earlier successful authorization, it'll use it. Otherwise
	 * it'll create a new authorization and cache it.
	 */
	status = SCOPE_CALL_WITH_RESULT(-1, pj_status_t, pjsip_auth_clt_reinit_req,
		&auth_sess, challenge, old_request, new_request);
	if (status != PJ_SUCCESS) {
		pj_err = ast_alloca(PJ_ERR_MSG_SIZE);
	}

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
		res = 0;
		ast_trace(-1, "%s:%s: Created new request with auth\n", id, src_name);
		goto cleanup;
	case PJSIP_ENOCREDENTIAL:
		/*
		 * This should be rare since set_outbound_authentication_credentials()
		 * did the matching but you never know.
		 */
		ast_log(LOG_WARNING,
			"%s:%s: No auth objects matching realm(s) '%s' from challenge found.\n",
			id, src_name, realms ? ast_str_buffer(realms) : "<none>");
		break;
	case PJSIP_EAUTHSTALECOUNT:
		pj_strerror(status, pj_err, PJ_ERR_MSG_SIZE);
		ast_log(LOG_WARNING,
			"%s:%s: Unable to create request with auth: %s\n",
			id, src_name, pj_err);
		break;
	case PJSIP_EFAILEDCREDENTIAL:
		pj_strerror(status, pj_err, PJ_ERR_MSG_SIZE);
		ast_log(LOG_WARNING, "%s:%s: Authentication credentials not accepted by server. %s\n",
			id, src_name, pj_err);
		break;
	default:
		pj_strerror(status, pj_err, PJ_ERR_MSG_SIZE);
		ast_log(LOG_WARNING, "%s:%s: Unable to create request with auth: %s\n",
			id, src_name, pj_err);
		break;
	}
	res = -1;

cleanup:
#if defined(HAVE_PJSIP_AUTH_CLT_DEINIT)
	/* If we initialized the auth_sess, clean it up */
	if (auth_sess.endpt) {
		pjsip_auth_clt_deinit(&auth_sess);
	}
#endif

	ast_sip_cleanup_auth_objects_vector(&auth_objects_vector);
	AST_VECTOR_FREE(&auth_objects_vector);
	ast_free(realms);

	SCOPE_EXIT_RTN_VALUE(res, "%s:%s: result: %s\n", id, src_name,
		res == 0 ? "success" : "failure");
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
