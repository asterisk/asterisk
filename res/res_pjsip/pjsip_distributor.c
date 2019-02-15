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
#include "asterisk/acl.h"
#include "include/res_pjsip_private.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadpool.h"
#include "asterisk/res_pjsip_cli.h"

static int distribute(void *data);
static pj_bool_t distributor(pjsip_rx_data *rdata);
static pj_status_t record_serializer(pjsip_tx_data *tdata);

static pjsip_module distributor_mod = {
	.name = {"Request Distributor", 19},
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 6,
	.on_tx_request = record_serializer,
	.on_rx_request = distributor,
	.on_rx_response = distributor,
};

struct ast_sched_context *prune_context;

/* From the auth/realm realtime column size */
#define MAX_REALM_LENGTH 40

#define DEFAULT_SUSPECTS_BUCKETS 53

static struct ao2_container *unidentified_requests;
static unsigned int unidentified_count;
static unsigned int unidentified_period;
static unsigned int unidentified_prune_interval;
static int using_auth_username;
static enum ast_sip_taskprocessor_overload_trigger overload_trigger;

struct unidentified_request{
	struct timeval first_seen;
	int count;
	char src_name[];
};

/*! Number of serializers in pool if one not otherwise known.  (Best if prime number) */
#define DISTRIBUTOR_POOL_SIZE		31

/*! Pool of serializers to use if not supplied. */
static struct ast_taskprocessor *distributor_pool[DISTRIBUTOR_POOL_SIZE];

/*!
 * \internal
 * \brief Record the task's serializer name on the tdata structure.
 * \since 14.0.0
 *
 * \param tdata The outgoing message.
 *
 * \retval PJ_SUCCESS.
 */
static pj_status_t record_serializer(pjsip_tx_data *tdata)
{
	struct ast_taskprocessor *serializer;

	serializer = ast_threadpool_serializer_get_current();
	if (serializer) {
		const char *name;

		name = ast_taskprocessor_name(serializer);
		if (!ast_strlen_zero(name)
			&& (!tdata->mod_data[distributor_mod.id]
				|| strcmp(tdata->mod_data[distributor_mod.id], name))) {
			char *tdata_name;

			/* The serializer in use changed. */
			tdata_name = pj_pool_alloc(tdata->pool, strlen(name) + 1);
			strcpy(tdata_name, name);/* Safe */

			tdata->mod_data[distributor_mod.id] = tdata_name;
		}
	}

	return PJ_SUCCESS;
}

/*!
 * \internal
 * \brief Find the request tdata to get the serializer it used.
 * \since 14.0.0
 *
 * \param rdata The incoming message.
 *
 * \retval serializer on success.
 * \retval NULL on error or could not find the serializer.
 */
static struct ast_taskprocessor *find_request_serializer(pjsip_rx_data *rdata)
{
	struct ast_taskprocessor *serializer = NULL;
	pj_str_t tsx_key;
	pjsip_transaction *tsx;

	pjsip_tsx_create_key(rdata->tp_info.pool, &tsx_key, PJSIP_ROLE_UAC,
		&rdata->msg_info.cseq->method, rdata);

	tsx = pjsip_tsx_layer_find_tsx(&tsx_key, PJ_TRUE);
	if (!tsx) {
		ast_debug(1, "Could not find transaction for %s.\n",
			pjsip_rx_data_get_info(rdata));
		return NULL;
	}
	ast_debug(3, "Found transaction %s for %s.\n",
		tsx->obj_name, pjsip_rx_data_get_info(rdata));

	if (tsx->last_tx) {
		const char *serializer_name;

		serializer_name = tsx->last_tx->mod_data[distributor_mod.id];
		if (!ast_strlen_zero(serializer_name)) {
			serializer = ast_taskprocessor_get(serializer_name, TPS_REF_IF_EXISTS);
			if (serializer) {
				ast_debug(3, "Found serializer %s on transaction %s\n",
						serializer_name, tsx->obj_name);
			}
		}
	}

#ifdef HAVE_PJ_TRANSACTION_GRP_LOCK
	pj_grp_lock_release(tsx->grp_lock);
#else
	pj_mutex_unlock(tsx->mutex);
#endif

	return serializer;
}

/*! Dialog-specific information the distributor uses */
struct distributor_dialog_data {
	/*! dialog_associations ao2 container key */
	pjsip_dialog *dlg;
	/*! Serializer to distribute tasks to for this dialog */
	struct ast_taskprocessor *serializer;
	/*! Endpoint associated with this dialog */
	struct ast_sip_endpoint *endpoint;
};

#define DIALOG_ASSOCIATIONS_BUCKETS 251

static struct ao2_container *dialog_associations;

/*!
 * \internal
 * \brief Compute a hash value on an arbitrary buffer.
 * \since 13.17.0
 *
 * \param[in] pos The buffer to add to the hash
 * \param[in] len The buffer length to add to the hash
 * \param[in] hash The hash value to add to
 *
 * \details
 * This version of the function is for when you need to compute a
 * hash of more than one buffer.
 *
 * This famous hash algorithm was written by Dan Bernstein and is
 * commonly used.
 *
 * \sa http://www.cse.yorku.ca/~oz/hash.html
 */
static int buf_hash_add(const char *pos, size_t len, int hash)
{
	while (len--) {
		hash = hash * 33 ^ *pos++;
	}

	return hash;
}

/*!
 * \internal
 * \brief Compute a hash value on an arbitrary buffer.
 * \since 13.17.0
 *
 * \param[in] pos The buffer to add to the hash
 * \param[in] len The buffer length to add to the hash
 *
 * \details
 * This version of the function is for when you need to compute a
 * hash of more than one buffer.
 *
 * This famous hash algorithm was written by Dan Bernstein and is
 * commonly used.
 *
 * \sa http://www.cse.yorku.ca/~oz/hash.html
 */
static int buf_hash(const char *pos, size_t len)
{
	return buf_hash_add(pos, len, 5381);
}

static int dialog_associations_hash(const void *obj, int flags)
{
	const struct distributor_dialog_data *object;
	union {
		const pjsip_dialog *dlg;
		const char buf[sizeof(pjsip_dialog *)];
	} key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key.dlg = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key.dlg = object->dlg;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash_restrict(buf_hash(key.buf, sizeof(key.buf)));
}

static int dialog_associations_cmp(void *obj, void *arg, int flags)
{
	const struct distributor_dialog_data *object_left = obj;
	const struct distributor_dialog_data *object_right = arg;
	const pjsip_dialog *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->dlg;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (object_left->dlg == right_key) {
			cmp = CMP_MATCH;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* There is no such thing for this container. */
		ast_assert(0);
		break;
	default:
		cmp = 0;
		break;
	}
	return cmp;
}

void ast_sip_dialog_set_serializer(pjsip_dialog *dlg, struct ast_taskprocessor *serializer)
{
	struct distributor_dialog_data *dist;

	ao2_wrlock(dialog_associations);
	dist = ao2_find(dialog_associations, dlg, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!dist) {
		if (serializer) {
			dist = ao2_alloc(sizeof(*dist), NULL);
			if (dist) {
				dist->dlg = dlg;
				dist->serializer = serializer;
				ao2_link_flags(dialog_associations, dist, OBJ_NOLOCK);
				ao2_ref(dist, -1);
			}
		}
	} else {
		ao2_lock(dist);
		dist->serializer = serializer;
		if (!dist->serializer && !dist->endpoint) {
			ao2_unlink_flags(dialog_associations, dist, OBJ_NOLOCK);
		}
		ao2_unlock(dist);
		ao2_ref(dist, -1);
	}
	ao2_unlock(dialog_associations);
}

void ast_sip_dialog_set_endpoint(pjsip_dialog *dlg, struct ast_sip_endpoint *endpoint)
{
	struct distributor_dialog_data *dist;

	ao2_wrlock(dialog_associations);
	dist = ao2_find(dialog_associations, dlg, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!dist) {
		if (endpoint) {
			dist = ao2_alloc(sizeof(*dist), NULL);
			if (dist) {
				dist->dlg = dlg;
				dist->endpoint = endpoint;
				ao2_link_flags(dialog_associations, dist, OBJ_NOLOCK);
				ao2_ref(dist, -1);
			}
		}
	} else {
		ao2_lock(dist);
		dist->endpoint = endpoint;
		if (!dist->serializer && !dist->endpoint) {
			ao2_unlink_flags(dialog_associations, dist, OBJ_NOLOCK);
		}
		ao2_unlock(dist);
		ao2_ref(dist, -1);
	}
	ao2_unlock(dialog_associations);
}

struct ast_sip_endpoint *ast_sip_dialog_get_endpoint(pjsip_dialog *dlg)
{
	struct distributor_dialog_data *dist;
	struct ast_sip_endpoint *endpoint;

	dist = ao2_find(dialog_associations, dlg, OBJ_SEARCH_KEY);
	if (dist) {
		ao2_lock(dist);
		endpoint = ao2_bump(dist->endpoint);
		ao2_unlock(dist);
		ao2_ref(dist, -1);
	} else {
		endpoint = NULL;
	}
	return endpoint;
}

static pjsip_dialog *find_dialog(pjsip_rx_data *rdata)
{
	pj_str_t tsx_key;
	pjsip_transaction *tsx;
	pjsip_dialog *dlg;
	pj_str_t *local_tag;
	pj_str_t *remote_tag;

	if (!rdata->msg_info.msg) {
		return NULL;
	}

	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
		local_tag = &rdata->msg_info.to->tag;
		remote_tag = &rdata->msg_info.from->tag;
	} else {
		local_tag = &rdata->msg_info.from->tag;
		remote_tag = &rdata->msg_info.to->tag;
	}

	/* We can only call the convenient method for
	 *  1) responses
	 *  2) non-CANCEL requests
	 *  3) CANCEL requests with a to-tag
	 */
	if (rdata->msg_info.msg->type == PJSIP_RESPONSE_MSG ||
			pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_cancel_method) ||
			rdata->msg_info.to->tag.slen != 0) {
		dlg = pjsip_ua_find_dialog(&rdata->msg_info.cid->id, local_tag,
				remote_tag, PJ_FALSE);
		if (dlg) {
			return dlg;
		}
	}

	/*
	 * There may still be a matching dialog if this is
	 * 1) an incoming CANCEL request without a to-tag
	 * 2) an incoming response to a dialog-creating request.
	 */
	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
		/* CANCEL requests will need to match the INVITE we initially received. Any
		 * other request type will either have been matched already or is not in
		 * dialog
		 */
		pjsip_tsx_create_key(rdata->tp_info.pool, &tsx_key, PJSIP_ROLE_UAS,
				pjsip_get_invite_method(), rdata);
	} else {
		pjsip_tsx_create_key(rdata->tp_info.pool, &tsx_key, PJSIP_ROLE_UAC,
				&rdata->msg_info.cseq->method, rdata);
	}

	tsx = pjsip_tsx_layer_find_tsx(&tsx_key, PJ_TRUE);
	if (!tsx) {
		ast_debug(3, "Could not find matching transaction for %s\n",
			pjsip_rx_data_get_info(rdata));
		return NULL;
	}

	dlg = pjsip_tsx_get_dlg(tsx);

#ifdef HAVE_PJ_TRANSACTION_GRP_LOCK
	pj_grp_lock_release(tsx->grp_lock);
#else
	pj_mutex_unlock(tsx->mutex);
#endif

	return dlg;
}

/*!
 * \internal
 * \brief Compute a hash value on a pjlib string
 * \since 13.10.0
 *
 * \param[in] str The pjlib string to add to the hash
 * \param[in] hash The hash value to add to
 *
 * \details
 * This version of the function is for when you need to compute a
 * string hash of more than one string.
 *
 * This famous hash algorithm was written by Dan Bernstein and is
 * commonly used.
 *
 * \sa http://www.cse.yorku.ca/~oz/hash.html
 */
static int pjstr_hash_add(pj_str_t *str, int hash)
{
	return buf_hash_add(pj_strbuf(str), pj_strlen(str), hash);
}

/*!
 * \internal
 * \brief Compute a hash value on a pjlib string
 * \since 13.10.0
 *
 * \param[in] str The pjlib string to hash
 *
 * This famous hash algorithm was written by Dan Bernstein and is
 * commonly used.
 *
 * http://www.cse.yorku.ca/~oz/hash.html
 */
static int pjstr_hash(pj_str_t *str)
{
	return pjstr_hash_add(str, 5381);
}

struct ast_taskprocessor *ast_sip_get_distributor_serializer(pjsip_rx_data *rdata)
{
	int hash;
	pj_str_t *remote_tag;
	struct ast_taskprocessor *serializer;

	if (!rdata->msg_info.msg) {
		return NULL;
	}

	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
		remote_tag = &rdata->msg_info.from->tag;
	} else {
		remote_tag = &rdata->msg_info.to->tag;
	}

	/* Compute the hash from the SIP message call-id and remote-tag */
	hash = pjstr_hash(&rdata->msg_info.cid->id);
	hash = pjstr_hash_add(remote_tag, hash);
	hash = ast_str_hash_restrict(hash);

	serializer = ao2_bump(distributor_pool[hash % ARRAY_LEN(distributor_pool)]);
	if (serializer) {
		ast_debug(3, "Calculated serializer %s to use for %s\n",
			ast_taskprocessor_name(serializer), pjsip_rx_data_get_info(rdata));
	}
	return serializer;
}

static pj_bool_t endpoint_lookup(pjsip_rx_data *rdata);

static pjsip_module endpoint_mod = {
	.name = {"Endpoint Identifier", 19},
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 3,
	.on_rx_request = endpoint_lookup,
};

static pj_bool_t distributor(pjsip_rx_data *rdata)
{
	pjsip_dialog *dlg;
	struct distributor_dialog_data *dist = NULL;
	struct ast_taskprocessor *serializer = NULL;
	pjsip_rx_data *clone;

	if (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		/*
		 * Ignore everything until we are fully booted.  Let the
		 * peer retransmit messages until we are ready.
		 */
		return PJ_TRUE;
	}

	dlg = find_dialog(rdata);
	if (dlg) {
		ast_debug(3, "Searching for serializer associated with dialog %s for %s\n",
			dlg->obj_name, pjsip_rx_data_get_info(rdata));
		dist = ao2_find(dialog_associations, dlg, OBJ_SEARCH_KEY);
		if (dist) {
			ao2_lock(dist);
			serializer = ao2_bump(dist->serializer);
			ao2_unlock(dist);
			if (serializer) {
				ast_debug(3, "Found serializer %s associated with dialog %s\n",
					ast_taskprocessor_name(serializer), dlg->obj_name);
			}
		}
	}

	if (serializer) {
		/* We have a serializer so we know where to send the message. */
	} else if (rdata->msg_info.msg->type == PJSIP_RESPONSE_MSG) {
		ast_debug(3, "No dialog serializer for %s.  Using request transaction as basis.\n",
			pjsip_rx_data_get_info(rdata));
		serializer = find_request_serializer(rdata);
		if (!serializer) {
			/*
			 * Pick a serializer for the unmatched response.
			 * We couldn't determine what serializer originally
			 * sent the request or the serializer is gone.
			 */
			serializer = ast_sip_get_distributor_serializer(rdata);
		}
	} else if (!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_cancel_method)
		|| !pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_bye_method)) {
		/* We have a BYE or CANCEL request without a serializer. */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
			PJSIP_SC_CALL_TSX_DOES_NOT_EXIST, NULL, NULL, NULL);
		ao2_cleanup(dist);
		return PJ_TRUE;
	} else {
		if ((overload_trigger == TASKPROCESSOR_OVERLOAD_TRIGGER_GLOBAL &&
			ast_taskprocessor_alert_get())
			|| (overload_trigger == TASKPROCESSOR_OVERLOAD_TRIGGER_PJSIP_ONLY &&
			ast_taskprocessor_get_subsystem_alert("pjsip"))) {
			/*
			 * When taskprocessors get backed up, there is a good chance that
			 * we are being overloaded and need to defer adding new work to
			 * the system.  To defer the work we will ignore the request and
			 * rely on the peer's transport layer to retransmit the message.
			 * We usually work off the overload within a few seconds.
			 * If transport is non-UDP we send a 503 response instead.
			 */
			switch (rdata->tp_info.transport->key.type) {
			case PJSIP_TRANSPORT_UDP6:
			case PJSIP_TRANSPORT_UDP:
				ast_debug(3, "Taskprocessor overload alert: Ignoring '%s'.\n",
					pjsip_rx_data_get_info(rdata));
				break;
			default:
				ast_debug(3, "Taskprocessor overload on non-udp transport. Received:'%s'. "
					"Responding with a 503.\n", pjsip_rx_data_get_info(rdata));
				pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
					PJSIP_SC_SERVICE_UNAVAILABLE, NULL, NULL, NULL);
				break;
			}
			ao2_cleanup(dist);
			return PJ_TRUE;
		}

		/* Pick a serializer for the out-of-dialog request. */
		serializer = ast_sip_get_distributor_serializer(rdata);
	}

	if (pjsip_rx_data_clone(rdata, 0, &clone) != PJ_SUCCESS) {
		ast_taskprocessor_unreference(serializer);
		ao2_cleanup(dist);
		return PJ_TRUE;
	}

	if (dist) {
		ao2_lock(dist);
		clone->endpt_info.mod_data[endpoint_mod.id] = ao2_bump(dist->endpoint);
		ao2_unlock(dist);
		ao2_cleanup(dist);
	}

	if (ast_sip_push_task(serializer, distribute, clone)) {
		ao2_cleanup(clone->endpt_info.mod_data[endpoint_mod.id]);
		pjsip_rx_data_free_cloned(clone);
	}

	ast_taskprocessor_unreference(serializer);

	return PJ_TRUE;
}

static struct ast_sip_auth *alloc_artificial_auth(char *default_realm)
{
	struct ast_sip_auth *fake_auth;

	fake_auth = ast_sorcery_alloc(ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE,
		"artificial");
	if (!fake_auth) {
		return NULL;
	}

	ast_string_field_set(fake_auth, realm, default_realm);
	ast_string_field_set(fake_auth, auth_user, "");
	ast_string_field_set(fake_auth, auth_pass, "");
	fake_auth->type = AST_SIP_AUTH_TYPE_ARTIFICIAL;

	return fake_auth;
}

static AO2_GLOBAL_OBJ_STATIC(artificial_auth);

static int create_artificial_auth(void)
{
	char default_realm[MAX_REALM_LENGTH + 1];
	struct ast_sip_auth *fake_auth;

	ast_sip_get_default_realm(default_realm, sizeof(default_realm));
	fake_auth = alloc_artificial_auth(default_realm);
	if (!fake_auth) {
		ast_log(LOG_ERROR, "Unable to create artificial auth\n");
		return -1;
	}

	ao2_global_obj_replace_unref(artificial_auth, fake_auth);
	ao2_ref(fake_auth, -1);
	return 0;
}

struct ast_sip_auth *ast_sip_get_artificial_auth(void)
{
	return ao2_global_obj_ref(artificial_auth);
}

static struct ast_sip_endpoint *artificial_endpoint = NULL;

static int create_artificial_endpoint(void)
{
	artificial_endpoint = ast_sorcery_alloc(ast_sip_get_sorcery(), "endpoint", NULL);
	if (!artificial_endpoint) {
		return -1;
	}

	AST_VECTOR_INIT(&artificial_endpoint->inbound_auths, 1);
	/* Pushing a bogus value into the vector will ensure that
	 * the proper size of the vector is returned. This value is
	 * not actually used anywhere
	 */
	AST_VECTOR_APPEND(&artificial_endpoint->inbound_auths, ast_strdup("artificial-auth"));
	return 0;
}

struct ast_sip_endpoint *ast_sip_get_artificial_endpoint(void)
{
	ao2_ref(artificial_endpoint, +1);
	return artificial_endpoint;
}

static void log_failed_request(pjsip_rx_data *rdata, char *msg, unsigned int count, unsigned int period)
{
	char from_buf[PJSIP_MAX_URL_SIZE];
	char callid_buf[PJSIP_MAX_URL_SIZE];
	char method_buf[PJSIP_MAX_URL_SIZE];
	char src_addr_buf[AST_SOCKADDR_BUFLEN];
	pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, rdata->msg_info.from->uri, from_buf, PJSIP_MAX_URL_SIZE);
	ast_copy_pj_str(callid_buf, &rdata->msg_info.cid->id, PJSIP_MAX_URL_SIZE);
	ast_copy_pj_str(method_buf, &rdata->msg_info.msg->line.req.method.name, PJSIP_MAX_URL_SIZE);
	if (count) {
		ast_log(LOG_NOTICE, "Request '%s' from '%s' failed for '%s' (callid: %s) - %s"
			" after %u tries in %.3f ms\n",
			method_buf, from_buf,
			pj_sockaddr_print(&rdata->pkt_info.src_addr, src_addr_buf, sizeof(src_addr_buf), 3),
			callid_buf, msg, count, period / 1000.0);
	} else {
		ast_log(LOG_NOTICE, "Request '%s' from '%s' failed for '%s' (callid: %s) - %s\n",
			method_buf, from_buf,
			pj_sockaddr_print(&rdata->pkt_info.src_addr, src_addr_buf, sizeof(src_addr_buf), 3),
			callid_buf, msg);
	}
}

static void check_endpoint(pjsip_rx_data *rdata, struct unidentified_request *unid,
	const char *name)
{
	int64_t ms = ast_tvdiff_ms(ast_tvnow(), unid->first_seen);

	ao2_wrlock(unid);
	unid->count++;

	if (ms < (unidentified_period * 1000) && unid->count >= unidentified_count) {
		log_failed_request(rdata, "No matching endpoint found", unid->count, ms);
		ast_sip_report_invalid_endpoint(name, rdata);
	}
	ao2_unlock(unid);
}

static int apply_endpoint_acl(pjsip_rx_data *rdata, struct ast_sip_endpoint *endpoint);
static int apply_endpoint_contact_acl(pjsip_rx_data *rdata, struct ast_sip_endpoint *endpoint);

static void apply_acls(pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint;

	/* Is the endpoint allowed with the source or contact address? */
	endpoint = rdata->endpt_info.mod_data[endpoint_mod.id];
	if (endpoint != artificial_endpoint
		&& (apply_endpoint_acl(rdata, endpoint)
			|| apply_endpoint_contact_acl(rdata, endpoint))) {
		ast_debug(1, "Endpoint '%s' not allowed by ACL\n",
			ast_sorcery_object_get_id(endpoint));

		/* Replace the rdata endpoint with the artificial endpoint. */
		ao2_replace(rdata->endpt_info.mod_data[endpoint_mod.id], artificial_endpoint);
	}
}

static pj_bool_t endpoint_lookup(pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint;
	struct unidentified_request *unid;
	int is_ack = rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD;

	endpoint = rdata->endpt_info.mod_data[endpoint_mod.id];
	if (endpoint) {
		/*
		 * ao2_find with OBJ_UNLINK always write locks the container before even searching
		 * for the object.  Since the majority case is that the object won't be found, do
		 * the find without OBJ_UNLINK to prevent the unnecessary write lock, then unlink
		 * if needed.
		 */
		unid = ao2_find(unidentified_requests, rdata->pkt_info.src_name, OBJ_SEARCH_KEY);
		if (unid) {
			ao2_unlink(unidentified_requests, unid);
			ao2_ref(unid, -1);
		}
		apply_acls(rdata);
		return PJ_FALSE;
	}

	endpoint = ast_sip_identify_endpoint(rdata);
	if (endpoint) {
		unid = ao2_find(unidentified_requests, rdata->pkt_info.src_name, OBJ_SEARCH_KEY);
		if (unid) {
			ao2_unlink(unidentified_requests, unid);
			ao2_ref(unid, -1);
		}
	}

	if (!endpoint) {
		/* always use an artificial endpoint - per discussion no reason
		   to have "alwaysauthreject" as an option.  It is felt using it
		   was a bug fix and it is not needed since we are not worried about
		   breaking old stuff and we really don't want to enable the discovery
		   of SIP accounts */
		endpoint = ast_sip_get_artificial_endpoint();
	}

	/* endpoint ref held by mod_data[] */
	rdata->endpt_info.mod_data[endpoint_mod.id] = endpoint;

	if (endpoint == artificial_endpoint && !is_ack) {
		char name[AST_UUID_STR_LEN] = "";
		pjsip_uri *from = rdata->msg_info.from->uri;

		if (PJSIP_URI_SCHEME_IS_SIP(from) || PJSIP_URI_SCHEME_IS_SIPS(from)) {
			pjsip_sip_uri *sip_from = pjsip_uri_get_uri(from);
			ast_copy_pj_str(name, &sip_from->user, sizeof(name));
		}

		unid = ao2_find(unidentified_requests, rdata->pkt_info.src_name, OBJ_SEARCH_KEY);
		if (unid) {
			check_endpoint(rdata, unid, name);
			ao2_ref(unid, -1);
		} else if (using_auth_username) {
			ao2_wrlock(unidentified_requests);
			/* Checking again with the write lock held allows us to eliminate the DUPS_REPLACE and sort_fn */
			unid = ao2_find(unidentified_requests, rdata->pkt_info.src_name,
				OBJ_SEARCH_KEY | OBJ_NOLOCK);
			if (unid) {
				check_endpoint(rdata, unid, name);
			} else {
				unid = ao2_alloc_options(sizeof(*unid) + strlen(rdata->pkt_info.src_name) + 1,
					NULL, AO2_ALLOC_OPT_LOCK_RWLOCK);
				if (!unid) {
					ao2_unlock(unidentified_requests);
					pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
					return PJ_TRUE;
				}
				strcpy(unid->src_name, rdata->pkt_info.src_name); /* Safe */
				unid->first_seen = ast_tvnow();
				unid->count = 1;
				ao2_link_flags(unidentified_requests, unid, OBJ_NOLOCK);
			}
			ao2_ref(unid, -1);
			ao2_unlock(unidentified_requests);
		} else {
			log_failed_request(rdata, "No matching endpoint found", 0, 0);
			ast_sip_report_invalid_endpoint(name, rdata);
		}
	}

	apply_acls(rdata);
	return PJ_FALSE;
}

static int apply_endpoint_acl(pjsip_rx_data *rdata, struct ast_sip_endpoint *endpoint)
{
	struct ast_sockaddr addr;

	if (ast_acl_list_is_empty(endpoint->acl)) {
		return 0;
	}

	memset(&addr, 0, sizeof(addr));
	ast_sockaddr_parse(&addr, rdata->pkt_info.src_name, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(&addr, rdata->pkt_info.src_port);

	if (ast_apply_acl(endpoint->acl, &addr, "SIP ACL: ") != AST_SENSE_ALLOW) {
		log_failed_request(rdata, "Not match Endpoint ACL", 0, 0);
		ast_sip_report_failed_acl(endpoint, rdata, "not_match_endpoint_acl");
		return 1;
	}
	return 0;
}

static int extract_contact_addr(pjsip_contact_hdr *contact, struct ast_sockaddr **addrs)
{
	pjsip_sip_uri *sip_uri;
	char host[256];

	if (!contact || contact->star) {
		*addrs = NULL;
		return 0;
	}
	if (!PJSIP_URI_SCHEME_IS_SIP(contact->uri) && !PJSIP_URI_SCHEME_IS_SIPS(contact->uri)) {
		*addrs = NULL;
		return 0;
	}
	sip_uri = pjsip_uri_get_uri(contact->uri);
	ast_copy_pj_str(host, &sip_uri->host, sizeof(host));
	return ast_sockaddr_resolve(addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC);
}

static int apply_endpoint_contact_acl(pjsip_rx_data *rdata, struct ast_sip_endpoint *endpoint)
{
	int num_contact_addrs;
	int forbidden = 0;
	struct ast_sockaddr *contact_addrs;
	int i;
	pjsip_contact_hdr *contact = (pjsip_contact_hdr *)&rdata->msg_info.msg->hdr;

	if (ast_acl_list_is_empty(endpoint->contact_acl)) {
		return 0;
	}

	while ((contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, contact->next))) {
		num_contact_addrs = extract_contact_addr(contact, &contact_addrs);
		if (num_contact_addrs <= 0) {
			continue;
		}
		for (i = 0; i < num_contact_addrs; ++i) {
			if (ast_apply_acl(endpoint->contact_acl, &contact_addrs[i], "SIP Contact ACL: ") != AST_SENSE_ALLOW) {
				log_failed_request(rdata, "Not match Endpoint Contact ACL", 0, 0);
				ast_sip_report_failed_acl(endpoint, rdata, "not_match_endpoint_contact_acl");
				forbidden = 1;
				break;
			}
		}
		ast_free(contact_addrs);
		if (forbidden) {
			/* No use checking other contacts if we already have failed ACL check */
			break;
		}
	}

	return forbidden;
}

static pj_bool_t authenticate(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, ast_pjsip_rdata_get_endpoint(rdata), ao2_cleanup);
	int is_ack = rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD;

	ast_assert(endpoint != NULL);

	if (is_ack) {
		return PJ_FALSE;
	}

	if (ast_sip_requires_authentication(endpoint, rdata)) {
		pjsip_tx_data *tdata;
		struct unidentified_request *unid;

		pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, 401, NULL, &tdata);
		switch (ast_sip_check_authentication(endpoint, rdata, tdata)) {
		case AST_SIP_AUTHENTICATION_CHALLENGE:
			/* Send the 401 we created for them */
			ast_sip_report_auth_challenge_sent(endpoint, rdata, tdata);
			if (pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL) != PJ_SUCCESS) {
				pjsip_tx_data_dec_ref(tdata);
			}
			return PJ_TRUE;
		case AST_SIP_AUTHENTICATION_SUCCESS:
			/* See note in endpoint_lookup about not holding an unnecessary write lock */
			unid = ao2_find(unidentified_requests, rdata->pkt_info.src_name, OBJ_SEARCH_KEY);
			if (unid) {
				ao2_unlink(unidentified_requests, unid);
				ao2_ref(unid, -1);
			}
			ast_sip_report_auth_success(endpoint, rdata);
			break;
		case AST_SIP_AUTHENTICATION_FAILED:
			log_failed_request(rdata, "Failed to authenticate", 0, 0);
			ast_sip_report_auth_failed_challenge_response(endpoint, rdata);
			if (pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL) != PJ_SUCCESS) {
				pjsip_tx_data_dec_ref(tdata);
			}
			return PJ_TRUE;
		case AST_SIP_AUTHENTICATION_ERROR:
			log_failed_request(rdata, "Error to authenticate", 0, 0);
			ast_sip_report_auth_failed_challenge_response(endpoint, rdata);
			pjsip_tx_data_dec_ref(tdata);
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
			return PJ_TRUE;
		}
		pjsip_tx_data_dec_ref(tdata);
	} else if (endpoint == artificial_endpoint) {
		/* Uh. Oh.  The artificial endpoint couldn't challenge so block the request. */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	return PJ_FALSE;
}

static pjsip_module auth_mod = {
	.name = {"Request Authenticator", 21},
	.priority = PJSIP_MOD_PRIORITY_APPLICATION - 2,
	.on_rx_request = authenticate,
};

static int distribute(void *data)
{
	static pjsip_process_rdata_param param = {
		.start_mod = &distributor_mod,
		.idx_after_start = 1,
	};
	pj_bool_t handled = PJ_FALSE;
	pjsip_rx_data *rdata = data;
	int is_request = rdata->msg_info.msg->type == PJSIP_REQUEST_MSG;
	int is_ack = is_request ? rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD : 0;
	struct ast_sip_endpoint *endpoint;

	pjsip_endpt_process_rx_data(ast_sip_get_pjsip_endpoint(), rdata, &param, &handled);
	if (!handled && is_request && !is_ack) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 501, NULL, NULL, NULL);
	}

	/* The endpoint_mod stores an endpoint reference in the mod_data of rdata. This
	 * is the only appropriate spot to actually decrement the reference.
	 */
	endpoint = rdata->endpt_info.mod_data[endpoint_mod.id];
	ao2_cleanup(endpoint);
	pjsip_rx_data_free_cloned(rdata);
	return 0;
}

struct ast_sip_endpoint *ast_pjsip_rdata_get_endpoint(pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint = rdata->endpt_info.mod_data[endpoint_mod.id];
	if (endpoint) {
		ao2_ref(endpoint, +1);
	}
	return endpoint;
}

static int suspects_sort(const void *obj, const void *arg, int flags)
{
	const struct unidentified_request *object_left = obj;
	const struct unidentified_request *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->src_name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->src_name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(object_left->src_name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}
	return cmp;
}

static int suspects_compare(void *obj, void *arg, int flags)
{
	const struct unidentified_request *object_left = obj;
	const struct unidentified_request *object_right = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->src_name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (strcmp(object_left->src_name, right_key) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(object_left->src_name, right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}
	return cmp;
}

static int suspects_hash(const void *obj, int flags)
{
	const struct unidentified_request *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->src_name;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

static struct ao2_container *cli_unid_get_container(const char *regex)
{
	struct ao2_container *s_container;

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		suspects_sort, suspects_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, unidentified_requests, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static int cli_unid_iterate(void *container, ao2_callback_fn callback, void *args)
{
	ao2_callback(container, 0, callback, args);

	return 0;
}

static void *cli_unid_retrieve_by_id(const char *id)
{
	return ao2_find(unidentified_requests, id, OBJ_SEARCH_KEY);
}

static const char *cli_unid_get_id(const void *obj)
{
	const struct unidentified_request *unid = obj;

	return unid->src_name;
}

static int cli_unid_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	RAII_VAR(struct ast_sip_cli_formatter_entry *, formatter_entry, NULL, ao2_cleanup);

	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 7;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <IP Address%*.*s>  <Count> <Age(sec)>\n",
		indent, "Request", filler, filler, CLI_HEADER_FILLER);

	return 0;
}

static int cli_unid_print_body(void *obj, void *arg, int flags)
{
	struct unidentified_request *unid = obj;
	struct ast_sip_cli_context *context = arg;
	int indent;
	int flexwidth;
	int64_t ms = ast_tvdiff_ms(ast_tvnow(), unid->first_seen);

	ast_assert(context->output_buffer != NULL);

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - 4;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-*.*s  %7d %10.3f\n",
		indent,
		"Request",
		flexwidth, flexwidth,
		unid->src_name, unid->count,  ms / 1000.0);

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Unidentified Requests",
		.command = "pjsip show unidentified_requests",
		.usage = "Usage: pjsip show unidentified_requests\n"
				"       Show the PJSIP Unidentified Requests\n"),
};

struct ast_sip_cli_formatter_entry *unid_formatter;

static int expire_requests(void *object, void *arg, int flags)
{
	struct unidentified_request *unid = object;
	int *maxage = arg;
	int64_t ms = ast_tvdiff_ms(ast_tvnow(), unid->first_seen);

	if (ms > (*maxage) * 2 * 1000) {
		return CMP_MATCH;
	}

	return 0;
}

static int prune_task(const void *data)
{
	unsigned int maxage;

	ast_sip_get_unidentified_request_thresholds(&unidentified_count, &unidentified_period, &unidentified_prune_interval);
	maxage = unidentified_period * 2;
	ao2_callback(unidentified_requests, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK, expire_requests, &maxage);

	return unidentified_prune_interval * 1000;
}

static int clean_task(const void *data)
{
	return 0;
}

static void global_loaded(const char *object_type)
{
	char default_realm[MAX_REALM_LENGTH + 1];
	struct ast_sip_auth *fake_auth;
	char *identifier_order;

	/* Update using_auth_username */
	identifier_order = ast_sip_get_endpoint_identifier_order();
	if (identifier_order) {
		char *identify_method;
		char *io_copy = ast_strdupa(identifier_order);
		int new_using = 0;

		ast_free(identifier_order);
		while ((identify_method = ast_strip(strsep(&io_copy, ",")))) {
			if (!strcmp(identify_method, "auth_username")) {
				new_using = 1;
				break;
			}
		}
		using_auth_username = new_using;
	}

	/* Update default_realm of artificial_auth */
	ast_sip_get_default_realm(default_realm, sizeof(default_realm));
	fake_auth = ast_sip_get_artificial_auth();
	if (!fake_auth || strcmp(fake_auth->realm, default_realm)) {
		ao2_cleanup(fake_auth);

		fake_auth = alloc_artificial_auth(default_realm);
		if (fake_auth) {
			ao2_global_obj_replace_unref(artificial_auth, fake_auth);
		}
	}
	ao2_cleanup(fake_auth);

	ast_sip_get_unidentified_request_thresholds(&unidentified_count, &unidentified_period, &unidentified_prune_interval);

	overload_trigger = ast_sip_get_taskprocessor_overload_trigger();

	/* Clean out the old task, if any */
	ast_sched_clean_by_callback(prune_context, prune_task, clean_task);
	/* Have to do something with the return value to shut up the stupid compiler. */
	if (ast_sched_add_variable(prune_context, unidentified_prune_interval * 1000, prune_task, NULL, 1) < 0) {
		return;
	}
}

/*! \brief Observer which is used to update our interval and default_realm when the global setting changes */
static struct ast_sorcery_observer global_observer = {
	.loaded = global_loaded,
};

/*!
 * \internal
 * \brief Shutdown the serializers in the distributor pool.
 * \since 13.10.0
 *
 * \return Nothing
 */
static void distributor_pool_shutdown(void)
{
	int idx;

	for (idx = 0; idx < ARRAY_LEN(distributor_pool); ++idx) {
		ast_taskprocessor_unreference(distributor_pool[idx]);
		distributor_pool[idx] = NULL;
	}
}

/*!
 * \internal
 * \brief Setup the serializers in the distributor pool.
 * \since 13.10.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int distributor_pool_setup(void)
{
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];
	int idx;

	for (idx = 0; idx < ARRAY_LEN(distributor_pool); ++idx) {
		/* Create name with seq number appended. */
		ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/distributor");

		distributor_pool[idx] = ast_sip_create_serializer(tps_name);
		if (!distributor_pool[idx]) {
			return -1;
		}
	}
	return 0;
}

int ast_sip_initialize_distributor(void)
{
	unidentified_requests = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0,
		DEFAULT_SUSPECTS_BUCKETS, suspects_hash, NULL, suspects_compare);
	if (!unidentified_requests) {
		return -1;
	}

	dialog_associations = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0,
		DIALOG_ASSOCIATIONS_BUCKETS, dialog_associations_hash, NULL,
		dialog_associations_cmp);
	if (!dialog_associations) {
		ast_sip_destroy_distributor();
		return -1;
	}

	if (distributor_pool_setup()) {
		ast_sip_destroy_distributor();
		return -1;
	}

	prune_context = ast_sched_context_create();
	if (!prune_context) {
		ast_sip_destroy_distributor();
		return -1;
	}

	if (ast_sched_start_thread(prune_context)) {
		ast_sip_destroy_distributor();
		return -1;
	}

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	if (create_artificial_endpoint() || create_artificial_auth()) {
		ast_sip_destroy_distributor();
		return -1;
	}

	if (ast_sip_register_service(&distributor_mod)) {
		ast_sip_destroy_distributor();
		return -1;
	}
	if (ast_sip_register_service(&endpoint_mod)) {
		ast_sip_destroy_distributor();
		return -1;
	}
	if (ast_sip_register_service(&auth_mod)) {
		ast_sip_destroy_distributor();
		return -1;
	}

	unid_formatter = ao2_alloc_options(sizeof(struct ast_sip_cli_formatter_entry), NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!unid_formatter) {
		ast_sip_destroy_distributor();
		ast_log(LOG_ERROR, "Unable to allocate memory for unid_formatter\n");
		return -1;
	}
	unid_formatter->name = "unidentified_request";
	unid_formatter->print_header = cli_unid_print_header;
	unid_formatter->print_body = cli_unid_print_body;
	unid_formatter->get_container = cli_unid_get_container;
	unid_formatter->iterate = cli_unid_iterate;
	unid_formatter->get_id = cli_unid_get_id;
	unid_formatter->retrieve_by_id = cli_unid_retrieve_by_id;
	ast_sip_register_cli_formatter(unid_formatter);

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return 0;
}

void ast_sip_destroy_distributor(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(unid_formatter);

	ast_sip_unregister_service(&auth_mod);
	ast_sip_unregister_service(&endpoint_mod);
	ast_sip_unregister_service(&distributor_mod);

	ao2_global_obj_release(artificial_auth);
	ao2_cleanup(artificial_endpoint);

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &global_observer);

	if (prune_context) {
		ast_sched_context_destroy(prune_context);
	}

	distributor_pool_shutdown();

	ao2_cleanup(dialog_associations);
	ao2_cleanup(unidentified_requests);
}
