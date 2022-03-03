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
#include "asterisk/vector.h"

pj_str_t supported_digest_algorithms[] = {
	{ "MD5", 3}
};

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
 * \brief Determine if digest algorithm in the header is one we support
 *
 * \retval 1 If we support the algorithm
 * \retval 0 If we do not
 *
 */
static int is_digest_algorithm_supported(pjsip_www_authenticate_hdr *auth_hdr)
{
	int digest;

	/* An empty digest is assumed to be md5 */
	if (pj_strlen(&auth_hdr->challenge.digest.algorithm) == 0) {
		return 1;
	}

	for (digest = 0; digest < ARRAY_LEN(supported_digest_algorithms); digest++) {
		if (pj_stricmp(&auth_hdr->challenge.digest.algorithm, &supported_digest_algorithms[digest]) == 0) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Initialize pjproject with a valid set of credentials
 *
 * RFC7616 and RFC8760 allow more than one WWW-Authenticate or
 * Proxy-Authenticate header per realm, each with different digest
 * algorithms (including new ones like SHA-256 and SHA-512-256). However,
 * thankfully, a UAS can NOT send back multiple Authenticate headers for
 * the same realm with the same digest algorithm.  The UAS is also
 * supposed to send the headers in order of preference with the first one
 * being the most preferred.
 *
 * We're supposed to send an Authorization header for the first one we
 * encounter for a realm that we can support.
 *
 * The UAS can also send multiple realms, especially when it's a proxy
 * that has forked the request in which case the proxy will aggregate all
 * of the Authenticate and then them all back to the UAC.
 *
 * It doesn't stop there though... Each realm can require a different
 * username from the others. There's also nothing preventing each digest
 * algorithm from having a unique password although I'm not sure if
 * that adds any benefit.
 *
 * So now... For each Authenticate header we encounter, we have to
 * determine if we support the digest algorithm and, if not, just skip the
 * header.  We then have to find an auth object that matches the realm AND
 * the digest algorithm or find a wildcard object that matches the digest
 * algorithm. If we find one, we add it to the results vector and read the
 * next Authenticate header. If the next header is for the same realm AND
 * we already added an auth object for that realm, we skip the header.
 * Otherwise we repeat the process for the next header.
 *
 * In the end, we'll have accumulated a list of credentials we can pass to
 * pjproject that it can use to add Authentication headers to a request.
 *
 * \note: Neither we nor pjproject can currently handle digest algorithms
 * other than MD5.  We don't even have a place for it in the ast_sip_auth
 * object. For this reason, we just skip processing any Authenticate
 * header that's not MD5.  When we support the others, we'll move the
 * check into the loop that searches the objects.
 */
static pj_status_t set_outbound_authentication_credentials(pjsip_auth_clt_sess *auth_sess,
		const struct ast_sip_auth_objects_vector *auth_objects_vector, pjsip_rx_data *challenge,
		struct ast_str **realms)
{
	int i;
	size_t auth_object_count;
	pjsip_www_authenticate_hdr *auth_hdr = NULL;
	pj_status_t res = PJ_SUCCESS;
	pjsip_hdr_e search_type;
	size_t cred_count;
	pjsip_cred_info *creds_array;

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
	AST_VECTOR(cred_info, pjsip_cred_info) auth_creds;

	search_type = get_auth_search_type(challenge);
	if (search_type == PJSIP_H_OTHER) {
		/*
		 * The status code on the response wasn't 401 or 407
		 * so there are no WWW-Authenticate or Proxy-Authenticate
		 * headers to process.
		 */
		return PJ_ENOTSUP;
	}

	auth_object_count = AST_VECTOR_SIZE(auth_objects_vector);
	if (auth_object_count == 0) {
		/* This shouldn't happen but we'll check anyway. */
		return PJ_EINVAL;
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
		return PJ_ENOMEM;
	}

	/*
	 * It's going to be rare that we actually have more than one
	 * WWW-Authentication header or more than one auth object to
	 * match to it so the following nested loop should be fine.
	 */
	while ((auth_hdr = pjsip_msg_find_hdr(challenge->msg_info.msg,
		search_type, auth_hdr ? auth_hdr->next : NULL))) {
		int exact_match_index = -1;
		int wildcard_match_index = -1;
		int match_index = 0;
		pjsip_cred_info auth_cred;
		struct ast_sip_auth *auth = NULL;

		memset(&auth_cred, 0, sizeof(auth_cred));
		/*
		 * Since we only support the MD5 algorithm at the current time,
		 * there's no sense searching for auth objects that match the algorithm.
		 * In fact, the auth_object structure doesn't even have a member
		 * for it.
		 *
		 * When we do support more algorithms, this check will need to be
		 * moved inside the auth object loop below.
		 *
		 * Note: The header may not have specified an algorithm at all in which
		 * case it's assumed to be MD5. is_digest_algorithm_supported() returns
		 * true for that case.
		 */
		if (!is_digest_algorithm_supported(auth_hdr)) {
			ast_debug(3, "Skipping header with realm '%.*s' and unsupported '%.*s' algorithm \n",
				(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr,
				(int)auth_hdr->challenge.digest.algorithm.slen, auth_hdr->challenge.digest.algorithm.ptr);
			continue;
		}

		/*
		 * Appending the realms is strictly so digest_create_request_with_auth()
		 * can display good error messages.  Since we only support one algorithm,
		 * there can't be more than one header with the same realm.  No need to worry
		 * about duplicate realms until then.
		 */
		if (*realms) {
			ast_str_append(realms, 0, "%.*s, ",
				(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr);
		}

		ast_debug(3, "Searching auths to find matching ones for header with realm '%.*s' and algorithm '%.*s'\n",
			(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr,
			(int)auth_hdr->challenge.digest.algorithm.slen, auth_hdr->challenge.digest.algorithm.ptr);

		/*
		 * Now that we have a valid header, we can loop over the auths available to
		 * find either an exact realm match or, failing that, a wildcard auth (an
		 * auth with an empty or "*" realm).
		 *
		 * NOTE: We never use the global default realm when we're the UAC responding
		 * to a 401 or 407.  We only use that when we're the UAS (handled elsewhere)
		 * and the auth object didn't have a realm.
		 */
		for (i = 0; i < auth_object_count; ++i) {
			auth = AST_VECTOR_GET(auth_objects_vector, i);

			/*
			 * If this auth object's realm exactly matches the one
			 * from the header, we can just break out and use it.
			 *
			 * NOTE: If there's more than one auth object for an endpoint with
			 * a matching realm it's a misconfiguration.  We'll only use the first.
			 */
			if (pj_stricmp2(&auth_hdr->challenge.digest.realm, auth->realm) == 0) {
				ast_debug(3, "Found matching auth '%s' with realm '%s'\n", ast_sorcery_object_get_id(auth),
					auth->realm);
				exact_match_index = i;
				/*
				 * If we found an exact realm match, there's no need to keep
				 * looking for a wildcard.
				 */
				break;
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
				ast_debug(3, "Found wildcard auth '%s' for realm '%.*s'\n", ast_sorcery_object_get_id(auth),
					(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr);
				wildcard_match_index = i;
			}
		}

		if (exact_match_index < 0 && wildcard_match_index < 0) {
			/*
			 * Didn't find either a wildcard or an exact realm match.
			 * Move on to the next header.
			 */
			ast_debug(3, "No auth matching realm or no wildcard found for realm '%.*s'\n",
				(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr);
			continue;
		}

		if (exact_match_index >= 0) {
			/*
			 * If we found an exact match, we'll always prefer that.
			 */
			match_index = exact_match_index;
			auth = AST_VECTOR_GET(auth_objects_vector, match_index);
			ast_debug(3, "Using matched auth '%s' with realm '%.*s'\n", ast_sorcery_object_get_id(auth),
				(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr);
		} else {
			/*
			 * We'll only use the wildcard if we didn't find an exact match.
			 */
			match_index = wildcard_match_index;
			auth = AST_VECTOR_GET(auth_objects_vector, match_index);
			ast_debug(3, "Using wildcard auth '%s' for realm '%.*s'\n", ast_sorcery_object_get_id(auth),
				(int)auth_hdr->challenge.digest.realm.slen, auth_hdr->challenge.digest.realm.ptr);
		}

		/*
		 * Copy the fields from the auth_object to the
		 * pjsip_cred_info structure.
		 */
		auth_cred.realm = auth_hdr->challenge.common.realm;
		pj_cstr(&auth_cred.username, auth->auth_user);
		pj_cstr(&auth_cred.scheme, "digest");
		switch (auth->type) {
		case AST_SIP_AUTH_TYPE_USER_PASS:
			pj_cstr(&auth_cred.data, auth->auth_pass);
			auth_cred.data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
			break;
		case AST_SIP_AUTH_TYPE_MD5:
			pj_cstr(&auth_cred.data, auth->md5_creds);
			auth_cred.data_type = PJSIP_CRED_DATA_DIGEST;
			break;
		case AST_SIP_AUTH_TYPE_GOOGLE_OAUTH:
			/* nothing to do. handled seperately in res_pjsip_outbound_registration */
			break;
		case AST_SIP_AUTH_TYPE_ARTIFICIAL:
			ast_log(LOG_ERROR,
				"Trying to set artificial outbound auth credentials shouldn't happen.\n");
			continue;
		} /* End auth object loop */

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
		res = AST_VECTOR_APPEND(&auth_creds, auth_cred);
		if (res != PJ_SUCCESS) {
			res = PJ_ENOMEM;
			goto cleanup;
		}
	} /* End header loop */

	if (*realms && ast_str_strlen(*realms)) {
		/*
		 * Again, this is strictly so digest_create_request_with_auth()
		 * can display good error messages.
		 *
		 * Chop off the trailing ", " on the last realm.
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
	if (res == PJ_SUCCESS) {
		ast_debug(3, "Set %"PRIu64" credentials in auth session\n", cred_count);
	} else {
		ast_log(LOG_ERROR, "Failed to set %"PRIu64" credentials in auth session\n", cred_count);
	}

cleanup:
	AST_VECTOR_FREE(&auth_creds);
	return res;
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
	struct ast_sip_endpoint *endpoint;
	char *id = NULL;
	const char *id_type;
	struct ast_str *realms = NULL;
	pjsip_dialog *dlg;
	int res = -1;

	/*
	 * Some older compilers have an issue with initializing structures with
	 * pjsip_auth_clt_sess auth_sess = { 0, };
	 * so we'll just do it the old fashioned way.
	 */
	memset(&auth_sess, 0, sizeof(auth_sess));

	dlg = pjsip_rdata_get_dlg(challenge);
	if (dlg) {
		/* The only thing we use endpoint for is to get an id for error/debug messages */
		endpoint = ast_sip_dialog_get_endpoint(dlg);
		id = endpoint ? ast_strdupa(ast_sorcery_object_get_id(endpoint)) : NULL;
		ao2_cleanup(endpoint);
		id_type = "Endpoint";
	}

	/* If there was no dialog, then this is probably a REGISTER so no endpoint */
	if (!id) {
		/* The only thing we use the address for is to get an id for error/debug messages */
		id = ast_alloca(AST_SOCKADDR_BUFLEN);
		pj_sockaddr_print(&challenge->pkt_info.src_addr, id, AST_SOCKADDR_BUFLEN, 3);
		id_type = "Host";
	}

	if (!auth_ids_vector || AST_VECTOR_SIZE(auth_ids_vector) == 0) {
		ast_log(LOG_ERROR, "%s: '%s': There were no auth ids available\n", id_type, id);
		return -1;
	}

	if (AST_VECTOR_INIT(&auth_objects_vector, AST_VECTOR_SIZE(auth_ids_vector)) != 0) {
		ast_log(LOG_ERROR, "%s: '%s': Couldn't initialize auth object vector\n", id_type, id);
		return -1;
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
		goto cleanup;
	}

	if (pjsip_auth_clt_init(&auth_sess, ast_sip_get_pjsip_endpoint(),
				old_request->pool, 0) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "%s: '%s': Failed to initialize client authentication session\n",
			id_type, id);
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
	status = set_outbound_authentication_credentials(&auth_sess, &auth_objects_vector, challenge, &realms);
	switch (status) {
	case PJ_SUCCESS:
		break;
	case PJSIP_ENOCREDENTIAL:
		ast_log(LOG_WARNING,
			"%s: '%s': No auth objects matching realm(s) '%s' from challenge found.\n", id_type, id,
			realms ? ast_str_buffer(realms) : "<none>");
		res = -1;
		goto cleanup;
	default:
		ast_log(LOG_WARNING, "%s: '%s': Failed to set authentication credentials\n", id_type, id);
		res = -1;
		goto cleanup;
	}

	/*
	 * reinit_req actually creates the Authorization headers to send on
	 * the next request.  If reinit_req already has a cached credential
	 * from an earlier successful authorization, it'll use it. Otherwise
	 * it'll create a new authorization and cache it.
	 */
	status = pjsip_auth_clt_reinit_req(&auth_sess, challenge, old_request, new_request);

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
		goto cleanup;
	case PJSIP_ENOCREDENTIAL:
		/*
		 * This should be rare since set_outbound_authentication_credentials()
		 * did the matching but you never know.
		 */
		ast_log(LOG_WARNING,
			"%s: '%s': No auth objects matching realm(s) '%s' from challenge found.\n", id_type, id,
			realms ? ast_str_buffer(realms) : "<none>");
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

	return res;
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
