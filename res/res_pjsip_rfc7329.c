/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025
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

#include "asterisk/channel.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include <pjlib-util/hmac_sha1.h>

#define RFC7329_MOD_DATA_SESSION_ID "rfc7329_session_id"

/* Per-session datastore for the active Session-ID and linkedid mapping. */
struct rfc7329_store_data {
	char *session_id;
	char *linkedid;
	int linkedid_refcounted;
};

AST_MUTEX_DEFINE_STATIC(rfc7329_secret_lock);
static unsigned char rfc7329_secret[16];
static int rfc7329_secret_initialized;
static const pj_str_t session_id_hdr_name = { "Session-ID", 10 };
static pjsip_module rfc7329_tsx_module;
static struct ao2_container *rfc7329_callid_map;
static struct ao2_container *rfc7329_linkedid_map;
static int rfc7329_active;
static struct ast_sip_session_supplement rfc7329_supplement;
static struct ast_sip_supplement rfc7329_out_of_dialog_supplement;

static void ensure_secret(void);

static int rfc7329_option_enabled(void)
{
	return ast_sip_get_rfc7329_enable();
}

/* Cache for out-of-dialog response matching using Call-ID. */
struct rfc7329_callid_entry {
	char *call_id;
	char *session_id;
};

/* Cross-leg Session-ID mapping keyed by channel linkedid. */
struct rfc7329_linkedid_entry {
	char *linkedid;
	char *session_id;
	int refcount;
};

static void rfc7329_callid_entry_destroy(void *obj)
{
	struct rfc7329_callid_entry *entry = obj;

	ast_free(entry->call_id);
	ast_free(entry->session_id);
}

static int rfc7329_callid_cmp(void *obj, void *arg, int flags)
{
	const struct rfc7329_callid_entry *entry = obj;
	const char *call_id = arg;

	return entry->call_id && call_id && !strcmp(entry->call_id, call_id) ? CMP_MATCH : 0;
}

static int rfc7329_callid_hash(const void *obj, int flags)
{
	const struct rfc7329_callid_entry *entry = obj;
	const char *call_id;

	if (flags & OBJ_SEARCH_KEY) {
		call_id = obj;
	} else {
		call_id = entry ? entry->call_id : NULL;
	}

	return call_id ? (int) ast_str_hash(call_id) : 0;
}

static void rfc7329_linkedid_entry_destroy(void *obj)
{
	struct rfc7329_linkedid_entry *entry = obj;

	ast_free(entry->linkedid);
	ast_free(entry->session_id);
}

static int rfc7329_linkedid_cmp(void *obj, void *arg, int flags)
{
	const struct rfc7329_linkedid_entry *entry = obj;
	const char *linkedid = arg;

	return entry->linkedid && linkedid && !strcmp(entry->linkedid, linkedid) ? CMP_MATCH : 0;
}

static int rfc7329_linkedid_hash(const void *obj, int flags)
{
	const struct rfc7329_linkedid_entry *entry = obj;
	const char *linkedid;

	if (flags & OBJ_SEARCH_KEY) {
		linkedid = obj;
	} else {
		linkedid = entry ? entry->linkedid : NULL;
	}

	return linkedid ? (int) ast_str_hash(linkedid) : 0;
}

static int rfc7329_activate(void)
{
	ensure_secret();
	rfc7329_callid_map = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, 37,
		rfc7329_callid_hash, NULL, rfc7329_callid_cmp);
	rfc7329_linkedid_map = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, 37,
		rfc7329_linkedid_hash, NULL, rfc7329_linkedid_cmp);
	if (!rfc7329_callid_map || !rfc7329_linkedid_map) {
		ao2_cleanup(rfc7329_callid_map);
		rfc7329_callid_map = NULL;
		ao2_cleanup(rfc7329_linkedid_map);
		rfc7329_linkedid_map = NULL;
		return -1;
	}
	ast_sip_session_register_supplement(&rfc7329_supplement);
	ast_sip_register_supplement(&rfc7329_out_of_dialog_supplement);

	if (pjsip_endpt_register_module(ast_sip_get_pjsip_endpoint(), &rfc7329_tsx_module) != PJ_SUCCESS) {
		ao2_cleanup(rfc7329_callid_map);
		rfc7329_callid_map = NULL;
		ao2_cleanup(rfc7329_linkedid_map);
		rfc7329_linkedid_map = NULL;
		ast_sip_session_unregister_supplement(&rfc7329_supplement);
		ast_sip_unregister_supplement(&rfc7329_out_of_dialog_supplement);
		return -1;
	}

	rfc7329_active = 1;
	return 0;
}

static void rfc7329_deactivate(void)
{
	if (!rfc7329_active) {
		return;
	}

	pjsip_endpt_unregister_module(ast_sip_get_pjsip_endpoint(), &rfc7329_tsx_module);
	ast_sip_unregister_supplement(&rfc7329_out_of_dialog_supplement);
	ast_sip_session_unregister_supplement(&rfc7329_supplement);
	ao2_cleanup(rfc7329_callid_map);
	rfc7329_callid_map = NULL;
	ao2_cleanup(rfc7329_linkedid_map);
	rfc7329_linkedid_map = NULL;
	rfc7329_active = 0;
}

/* Return the channel linkedid if the session has a channel. */
static const char *rfc7329_get_linkedid(struct ast_sip_session *session)
{
	if (!session || !session->channel) {
		return NULL;
	}

	return ast_channel_linkedid(session->channel);
}

/* Attach Session-ID to linkedid for later reuse on other call legs. */
static void rfc7329_linkedid_map_add(struct ast_sip_session *session,
	struct rfc7329_store_data *store)
{
	RAII_VAR(struct rfc7329_linkedid_entry *, entry, NULL, ao2_cleanup);
	struct rfc7329_linkedid_entry *new_entry;
	const char *linkedid;
	char *linkedid_copy;

	if (!rfc7329_linkedid_map || !session || !store || store->linkedid_refcounted) {
		return;
	}

	linkedid = rfc7329_get_linkedid(session);
	if (ast_strlen_zero(linkedid) || ast_strlen_zero(store->session_id)) {
		return;
	}

	linkedid_copy = ast_strdup(linkedid);
	if (!linkedid_copy) {
		return;
	}

	entry = ao2_find(rfc7329_linkedid_map, linkedid, OBJ_SEARCH_KEY);
	if (entry) {
		if (entry->session_id && strcmp(entry->session_id, store->session_id)) {
			ast_log(LOG_WARNING,
				"RFC7329: linkedid '%s' already mapped to Session-ID '%s'; keeping existing\n",
				linkedid, entry->session_id);
		} else if (!entry->session_id) {
			entry->session_id = ast_strdup(store->session_id);
		}
		/* Track multiple sessions sharing the same linkedid. */
		entry->refcount++;
	} else {
		new_entry = ao2_alloc(sizeof(*new_entry), rfc7329_linkedid_entry_destroy);
		if (!new_entry) {
			ast_free(linkedid_copy);
			return;
		}

		new_entry->linkedid = ast_strdup(linkedid);
		new_entry->session_id = ast_strdup(store->session_id);
		new_entry->refcount = 1;
		if (!new_entry->linkedid || !new_entry->session_id) {
			ao2_cleanup(new_entry);
			ast_free(linkedid_copy);
			return;
		}

		/* First mapping for this linkedid; used to reuse Session-ID across legs. */
		ao2_link(rfc7329_linkedid_map, new_entry);
		ao2_cleanup(new_entry);
	}

	store->linkedid = linkedid_copy;
	store->linkedid_refcounted = 1;
}

/* Lookup Session-ID by linkedid for a new leg. */
static const char *rfc7329_find_linkedid_map(struct ast_sip_session *session)
{
	RAII_VAR(struct rfc7329_linkedid_entry *, entry, NULL, ao2_cleanup);
	const char *linkedid;

	if (!rfc7329_linkedid_map || !session) {
		return NULL;
	}

	linkedid = rfc7329_get_linkedid(session);
	if (ast_strlen_zero(linkedid)) {
		return NULL;
	}

	entry = ao2_find(rfc7329_linkedid_map, linkedid, OBJ_SEARCH_KEY);
	if (!entry) {
		return NULL;
	}

	return entry->session_id;
}

/* Drop linkedid mapping when the last session referencing it ends. */
static void rfc7329_linkedid_map_remove(struct rfc7329_store_data *store)
{
	RAII_VAR(struct rfc7329_linkedid_entry *, entry, NULL, ao2_cleanup);

	if (!rfc7329_linkedid_map || !store || !store->linkedid_refcounted
		|| ast_strlen_zero(store->linkedid)) {
		return;
	}

	entry = ao2_find(rfc7329_linkedid_map, store->linkedid, OBJ_SEARCH_KEY);
	if (!entry) {
		return;
	}

	if (entry->refcount > 1) {
		entry->refcount--;
		return;
	}

	ao2_unlink(rfc7329_linkedid_map, entry);
}

static void datastore_destroy_cb(void *data)
{
	struct rfc7329_store_data *store = data;

	if (!store) {
		return;
	}

	ast_free(store->session_id);
	ast_free(store->linkedid);
	ast_free(store);
}

static const struct ast_datastore_info rfc7329_store_datastore = {
	.type = "rfc7329_session_id",
	.destroy = datastore_destroy_cb,
};

static void ensure_secret(void)
{
	int i;

	if (rfc7329_secret_initialized) {
		return;
	}

	ast_mutex_lock(&rfc7329_secret_lock);
	if (!rfc7329_secret_initialized) {
		for (i = 0; i < (int) sizeof(rfc7329_secret); ++i) {
			rfc7329_secret[i] = ast_random() & 0xFF;
		}
		rfc7329_secret_initialized = 1;
	}
	ast_mutex_unlock(&rfc7329_secret_lock);
}

static void hmac_sha1_128_hex(const unsigned char *msg, size_t msg_len, char out_hex[33])
{
	unsigned char digest[20];
	int i;

	ensure_secret();
	pj_hmac_sha1(msg, msg_len, rfc7329_secret, sizeof(rfc7329_secret), digest);

	for (i = 0; i < 16; ++i) {
		sprintf(out_hex + (i * 2), "%02x", digest[i]);
	}
	out_hex[32] = '\0';
}

static char *dup_pj_str(const pj_str_t *value)
{
	char *buf;

	if (!value || !value->slen) {
		return NULL;
	}

	buf = ast_malloc(value->slen + 1);
	if (!buf) {
		return NULL;
	}

	ast_copy_pj_str(buf, value, value->slen + 1);
	return buf;
}

static char *pj_strdup2_pool(pj_pool_t *pool, const pj_str_t *value)
{
	char *buf;

	if (!pool || !value || !value->slen) {
		return NULL;
	}

	buf = pj_pool_alloc(pool, value->slen + 1);
	if (!buf) {
		return NULL;
	}

	pj_memcpy(buf, value->ptr, value->slen);
	buf[value->slen] = '\0';

	return buf;
}

static char *dup_cstr_pool(pj_pool_t *pool, const char *value)
{
	size_t len;
	char *buf;

	if (!pool || !value) {
		return NULL;
	}

	len = strlen(value);
	buf = pj_pool_alloc(pool, len + 1);
	if (!buf) {
		return NULL;
	}

	memcpy(buf, value, len + 1);
	return buf;
}

static int header_value_matches(pjsip_generic_string_hdr *hdr, const char *value)
{
	size_t len;

	if (!hdr || !value) {
		return 0;
	}

	len = strlen(value);
	return hdr->hvalue.slen == (pj_ssize_t) len
		&& !pj_memcmp(hdr->hvalue.ptr, value, len);
}

static char *build_session_id_from_msg(pj_pool_t *pool, pjsip_msg *msg)
{
	pjsip_generic_string_hdr *hdr;
	pjsip_cid_hdr *call_id;
	char hex_id[33];

	if (!pool || !msg) {
		return NULL;
	}

	hdr = pjsip_msg_find_hdr_by_name(msg, &session_id_hdr_name, NULL);
	if (hdr && hdr->hvalue.slen) {
		return pj_strdup2_pool(pool, &hdr->hvalue);
	}

	call_id = pjsip_msg_find_hdr(msg, PJSIP_H_CALL_ID, NULL);
	if (!call_id || !call_id->id.slen) {
		return NULL;
	}

	hmac_sha1_128_hex((const unsigned char *) call_id->id.ptr, call_id->id.slen, hex_id);
	return dup_cstr_pool(pool, hex_id);
}

static char *build_session_id_from_call_id(pj_pool_t *pool, pjsip_msg *msg)
{
	pjsip_cid_hdr *call_id;
	char hex_id[33];

	if (!pool || !msg) {
		return NULL;
	}

	call_id = pjsip_msg_find_hdr(msg, PJSIP_H_CALL_ID, NULL);
	if (!call_id || !call_id->id.slen) {
		return NULL;
	}

	hmac_sha1_128_hex((const unsigned char *) call_id->id.ptr, call_id->id.slen, hex_id);
	return dup_cstr_pool(pool, hex_id);
}

static const char *store_session_id_str(struct ast_sip_session *session, const char *value)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	struct rfc7329_store_data *store;

	if (ast_strlen_zero(value)) {
		return NULL;
	}

	datastore = ast_sip_session_get_datastore(session, "rfc7329_session_id");
	if (datastore) {
		store = datastore->data;
		if (store) {
			rfc7329_linkedid_map_add(session, store);
		}
		return store ? store->session_id : NULL;
	}

	datastore = ast_sip_session_alloc_datastore(&rfc7329_store_datastore, "rfc7329_session_id");
	if (!datastore) {
		return NULL;
	}

	store = ast_calloc(1, sizeof(*store));
	if (!store) {
		return NULL;
	}

	store->session_id = ast_strdup(value);
	if (!store->session_id) {
		ast_free(store);
		return NULL;
	}

	datastore->data = store;
	ast_sip_session_add_datastore(session, datastore);
	rfc7329_linkedid_map_add(session, store);

	return store->session_id;
}

static const char *store_session_id_pjstr(struct ast_sip_session *session, const pj_str_t *value)
{
	char *buf;
	const char *stored;

	buf = dup_pj_str(value);
	if (!buf) {
		return NULL;
	}

	stored = store_session_id_str(session, buf);
	ast_free(buf);
	return stored;
}

static const char *get_stored_session_id(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	struct rfc7329_store_data *store;

	if (!session) {
		return NULL;
	}

	datastore = ast_sip_session_get_datastore(session, RFC7329_MOD_DATA_SESSION_ID);
	if (!datastore) {
		return NULL;
	}

	store = datastore->data;
	if (store) {
		rfc7329_linkedid_map_add(session, store);
	}
	return store ? store->session_id : NULL;
}

static void rfc7329_store_callid_map(const pj_str_t *call_id, const char *session_id)
{
	RAII_VAR(struct rfc7329_callid_entry *, entry, NULL, ao2_cleanup);
	struct rfc7329_callid_entry *new_entry;
	char call_id_buf[256];

	if (!rfc7329_callid_map || !call_id || !call_id->slen || ast_strlen_zero(session_id)) {
		return;
	}

	if (call_id->slen >= (pj_ssize_t) sizeof(call_id_buf)) {
		return;
	}

	ast_copy_pj_str(call_id_buf, call_id, sizeof(call_id_buf));
	entry = ao2_find(rfc7329_callid_map, call_id_buf, OBJ_SEARCH_KEY);
	if (entry) {
		ast_free(entry->session_id);
		entry->session_id = ast_strdup(session_id);
		return;
	}

	new_entry = ao2_alloc(sizeof(*new_entry), rfc7329_callid_entry_destroy);
	if (!new_entry) {
		return;
	}

	new_entry->call_id = ast_strdup(call_id_buf);
	new_entry->session_id = ast_strdup(session_id);
	if (!new_entry->call_id || !new_entry->session_id) {
		ao2_cleanup(new_entry);
		return;
	}

	ao2_link(rfc7329_callid_map, new_entry);
	ao2_cleanup(new_entry);
}

static const char *rfc7329_find_callid_map(const pj_str_t *call_id)
{
	RAII_VAR(struct rfc7329_callid_entry *, entry, NULL, ao2_cleanup);
	char call_id_buf[256];

	if (!rfc7329_callid_map || !call_id || !call_id->slen) {
		return NULL;
	}

	if (call_id->slen >= (pj_ssize_t) sizeof(call_id_buf)) {
		return NULL;
	}

	ast_copy_pj_str(call_id_buf, call_id, sizeof(call_id_buf));
	entry = ao2_find(rfc7329_callid_map, call_id_buf, OBJ_SEARCH_KEY);
	if (!entry) {
		return NULL;
	}

	return entry->session_id;
}

static void rfc7329_remove_callid_map(const pj_str_t *call_id)
{
	RAII_VAR(struct rfc7329_callid_entry *, entry, NULL, ao2_cleanup);
	char call_id_buf[256];

	if (!rfc7329_callid_map || !call_id || !call_id->slen) {
		return;
	}

	if (call_id->slen >= (pj_ssize_t) sizeof(call_id_buf)) {
		return;
	}

	ast_copy_pj_str(call_id_buf, call_id, sizeof(call_id_buf));
	entry = ao2_find(rfc7329_callid_map, call_id_buf, OBJ_SEARCH_KEY);
	if (!entry) {
		return;
	}

	ao2_unlink(rfc7329_callid_map, entry);
}

static const char *get_session_id(struct ast_sip_session *session, pjsip_msg *msg)
{
	pjsip_generic_string_hdr *hdr;
	pjsip_cid_hdr *call_id;
	char hex_id[33];
	const char *stored;

	if (!session || !msg) {
		return NULL;
	}

	stored = get_stored_session_id(session);
	if (stored) {
		return stored;
	}

	hdr = pjsip_msg_find_hdr_by_name(msg, &session_id_hdr_name, NULL);
	if (hdr && hdr->hvalue.slen) {
		return store_session_id_pjstr(session, &hdr->hvalue);
	}

	/* Reuse Session-ID across call legs using linkedid. */
	stored = rfc7329_find_linkedid_map(session);
	if (stored) {
		return store_session_id_str(session, stored);
	}

	call_id = pjsip_msg_find_hdr(msg, PJSIP_H_CALL_ID, NULL);
	if (call_id && call_id->id.slen) {
		hmac_sha1_128_hex((const unsigned char *) call_id->id.ptr, call_id->id.slen, hex_id);
		return store_session_id_str(session, hex_id);
	}

	return NULL;
}

static void set_or_replace_session_id_header(pjsip_tx_data *tdata, const char *expected)
{
	pjsip_generic_string_hdr *hdr;
	pj_str_t hdr_value;

	if (!tdata || ast_strlen_zero(expected)) {
		return;
	}

	hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL);
	if (!hdr) {
		hdr_value = pj_str((char *) expected);
		hdr = pjsip_generic_string_hdr_create(tdata->pool, &session_id_hdr_name, &hdr_value);
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
		return;
	}

	if (!header_value_matches(hdr, expected)) {
		ast_log(LOG_WARNING, "RFC7329: Replacing Session-ID header value '%.*s' with '%s'\n",
			(int) hdr->hvalue.slen, hdr->hvalue.ptr, expected);
		pj_strdup2(tdata->pool, &hdr->hvalue, expected);
	}
}

static void add_session_id_header_from_value(pjsip_tx_data *tdata, const char *session_id)
{
	if (!tdata || ast_strlen_zero(session_id)) {
		return;
	}

	set_or_replace_session_id_header(tdata, session_id);
}

static void add_session_id_header_from_call_id(pjsip_tx_data *tdata)
{
	char *session_id;

	if (!tdata) {
		return;
	}

	if (pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL)) {
		return;
	}

	session_id = build_session_id_from_call_id(tdata->pool, tdata->msg);
	if (!session_id) {
		return;
	}

	add_session_id_header_from_value(tdata, session_id);
}

static void add_session_id_header_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	const char *session_id;
	pjsip_generic_string_hdr *hdr;
	pj_str_t hdr_value;

	if (!session || !tdata) {
		return;
	}

	hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL);
	session_id = get_stored_session_id(session);
	if (!session_id) {
		session_id = get_session_id(session, tdata->msg);
	}
	if (!session_id) {
		return;
	}

	if (hdr && header_value_matches(hdr, session_id)) {
		return;
	}

	hdr_value = pj_str((char *) session_id);
	if (!hdr) {
		hdr = pjsip_generic_string_hdr_create(tdata->pool, &session_id_hdr_name, &hdr_value);
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
	} else {
		pj_strdup2(tdata->pool, &hdr->hvalue, session_id);
	}
}

static void add_session_id_header_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	const char *session_id;
	pjsip_generic_string_hdr *hdr;
	pj_str_t hdr_value;

	if (!session || !tdata) {
		return;
	}

	hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL);
	session_id = get_stored_session_id(session);

	if (!session_id) {
		if (hdr && hdr->hvalue.slen) {
			store_session_id_pjstr(session, &hdr->hvalue);
		}
		return;
	}

	if (hdr && header_value_matches(hdr, session_id)) {
		return;
	}

	hdr_value = pj_str((char *) session_id);
	if (!hdr) {
		hdr = pjsip_generic_string_hdr_create(tdata->pool, &session_id_hdr_name, &hdr_value);
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
	} else {
		pj_strdup2(tdata->pool, &hdr->hvalue, session_id);
	}
}

static int rfc7329_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	if (!rfc7329_option_enabled()) {
		return 0;
	}

	if (!session || !rdata) {
		return 0;
	}

	get_session_id(session, rdata->msg_info.msg);
	return 0;
}

static void rfc7329_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	if (!rfc7329_option_enabled()) {
		return;
	}

	add_session_id_header_request(session, tdata);
}

static void rfc7329_outgoing_response(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	if (!rfc7329_option_enabled()) {
		return;
	}

	add_session_id_header_response(session, tdata);
}

static void rfc7329_session_begin(struct ast_sip_session *session)
{
	if (!rfc7329_option_enabled()) {
		return;
	}

	if (!session) {
		return;
	}

	get_stored_session_id(session);
}

static void rfc7329_session_channel_created(struct ast_sip_session *session)
{
	if (!rfc7329_option_enabled()) {
		return;
	}

	if (!session) {
		return;
	}

	/* Session-ID may be known before channel creation; link it now. */
	get_stored_session_id(session);
}

static void rfc7329_session_destroy(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	struct rfc7329_store_data *store;

	if (!rfc7329_option_enabled()) {
		return;
	}

	if (!session) {
		return;
	}

	datastore = ast_sip_session_get_datastore(session, RFC7329_MOD_DATA_SESSION_ID);
	if (!datastore) {
		return;
	}

	store = datastore->data;
	rfc7329_linkedid_map_remove(store);
}

static struct ast_sip_session_supplement rfc7329_supplement = {
	.session_begin = rfc7329_session_begin,
	.session_channel_created = rfc7329_session_channel_created,
	.incoming_request = rfc7329_incoming_request,
	.outgoing_request = rfc7329_outgoing_request,
	.outgoing_response = rfc7329_outgoing_response,
	.session_destroy = rfc7329_session_destroy,
};

static void rfc7329_outgoing_request_out_of_dialog(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, struct pjsip_tx_data *tdata)
{
	if (!rfc7329_option_enabled()) {
		return;
	}

	if (!tdata) {
		return;
	}

	add_session_id_header_from_call_id(tdata);
}

static struct ast_sip_supplement rfc7329_out_of_dialog_supplement = {
	.outgoing_request = rfc7329_outgoing_request_out_of_dialog,
};

/* Ensure Session-ID on in-dialog requests (e.g., ACK) that bypass supplements. */
static pj_status_t rfc7329_on_tx_request(pjsip_tx_data *tdata)
{
	pjsip_dialog *dlg;
	struct ast_sip_session *session;
	const char *session_id;

	if (!rfc7329_option_enabled()) {
		return PJ_SUCCESS;
	}

	if (!tdata || tdata->msg->type != PJSIP_REQUEST_MSG) {
		return PJ_SUCCESS;
	}

	if (pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL)) {
		return PJ_SUCCESS;
	}

	dlg = pjsip_tdata_get_dlg(tdata);
	session = dlg ? ast_sip_dialog_get_session(dlg) : NULL;
	if (session) {
		/* Cover ACK and other in-dialog requests that bypass session supplements. */
		session_id = get_stored_session_id(session);
		if (!session_id) {
			session_id = get_session_id(session, tdata->msg);
		}
		if (session_id) {
			set_or_replace_session_id_header(tdata, session_id);
			ao2_cleanup(session);
			return PJ_SUCCESS;
		}
		ao2_cleanup(session);
	}

	add_session_id_header_from_call_id(tdata);
	return PJ_SUCCESS;
}

static pj_bool_t rfc7329_on_rx_request(pjsip_rx_data *rdata)
{
	pjsip_transaction *tsx;
	pjsip_dialog *dlg;
	char *session_id;
	pjsip_generic_string_hdr *hdr;
	pjsip_cid_hdr *call_id;
	char hex_id[33];

	if (!rfc7329_option_enabled()) {
		return PJ_FALSE;
	}

	if (!rdata) {
		return PJ_FALSE;
	}

	dlg = pjsip_rdata_get_dlg(rdata);
	if (dlg) {
		return PJ_FALSE;
	}

	call_id = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CALL_ID, NULL);
	if (call_id && call_id->id.slen) {
		hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &session_id_hdr_name, NULL);
		if (hdr && hdr->hvalue.slen) {
			char session_id_buf[64];
			ast_copy_pj_str(session_id_buf, &hdr->hvalue, sizeof(session_id_buf));
			rfc7329_store_callid_map(&call_id->id, session_id_buf);
		} else {
			hmac_sha1_128_hex((const unsigned char *) call_id->id.ptr, call_id->id.slen, hex_id);
			rfc7329_store_callid_map(&call_id->id, hex_id);
		}
	}

	tsx = pjsip_rdata_get_tsx(rdata);
	if (!tsx) {
		return PJ_FALSE;
	}

	if (ast_sip_mod_data_get(tsx->mod_data, rfc7329_tsx_module.id, RFC7329_MOD_DATA_SESSION_ID)) {
		return PJ_FALSE;
	}

	session_id = build_session_id_from_msg(tsx->pool, rdata->msg_info.msg);
	if (session_id) {
		ast_sip_mod_data_set(tsx->pool, tsx->mod_data, rfc7329_tsx_module.id,
			RFC7329_MOD_DATA_SESSION_ID, session_id);
	}

	return PJ_FALSE;
}

static void rfc7329_on_tsx_state(pjsip_transaction *tsx, pjsip_event *event)
{
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg;
	const char *stored;
	pjsip_rx_data *rdata;
	pjsip_cid_hdr *call_id_hdr;
	const char *mapped;

	if (!rfc7329_option_enabled()) {
		return;
	}

	if (!tsx || !event) {
		return;
	}

	if (event->type != PJSIP_EVENT_TSX_STATE) {
		return;
	}

	if (event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		rdata = event->body.tsx_state.src.rdata;
		if (!rdata || rdata->msg_info.msg->type != PJSIP_REQUEST_MSG) {
			return;
		}
		dlg = pjsip_tsx_get_dlg(tsx);
		if (dlg) {
			return;
		}

		if (!ast_sip_mod_data_get(tsx->mod_data, rfc7329_tsx_module.id, RFC7329_MOD_DATA_SESSION_ID)) {
			char *session_id = build_session_id_from_msg(tsx->pool, rdata->msg_info.msg);
			if (session_id) {
				ast_sip_mod_data_set(tsx->pool, tsx->mod_data, rfc7329_tsx_module.id,
					RFC7329_MOD_DATA_SESSION_ID, session_id);
			}
		}
		return;
	}

	if (event->body.tsx_state.type != PJSIP_EVENT_TX_MSG) {
		return;
	}

	dlg = pjsip_tsx_get_dlg(tsx);
	if (dlg) {
		return;
	}

	tdata = event->body.tsx_state.src.tdata;
	if (!tdata || tdata->msg->type != PJSIP_RESPONSE_MSG) {
		return;
	}

	if (pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL)) {
		return;
	}

	stored = ast_sip_mod_data_get(tsx->mod_data, rfc7329_tsx_module.id, RFC7329_MOD_DATA_SESSION_ID);
	if (stored) {
		set_or_replace_session_id_header(tdata, stored);
		return;
	}

	call_id_hdr = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CALL_ID, NULL);
	mapped = call_id_hdr ? rfc7329_find_callid_map(&call_id_hdr->id) : NULL;
	if (mapped) {
		set_or_replace_session_id_header(tdata, mapped);
		if (tdata->msg->line.status.code >= 200) {
			rfc7329_remove_callid_map(&call_id_hdr->id);
		}
		return;
	}

	add_session_id_header_from_call_id(tdata);
}

static pj_status_t rfc7329_on_tx_response(pjsip_tx_data *tdata)
{
	pjsip_cid_hdr *call_id_hdr;
	const char *mapped;

	if (!rfc7329_option_enabled()) {
		return PJ_SUCCESS;
	}

	if (!tdata || tdata->msg->type != PJSIP_RESPONSE_MSG) {
		return PJ_SUCCESS;
	}

	if (pjsip_msg_find_hdr_by_name(tdata->msg, &session_id_hdr_name, NULL)) {
		return PJ_SUCCESS;
	}

	call_id_hdr = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CALL_ID, NULL);
	mapped = call_id_hdr ? rfc7329_find_callid_map(&call_id_hdr->id) : NULL;
	if (mapped) {
		set_or_replace_session_id_header(tdata, mapped);
		if (tdata->msg->line.status.code >= 200) {
			rfc7329_remove_callid_map(&call_id_hdr->id);
		}
		return PJ_SUCCESS;
	}

	add_session_id_header_from_call_id(tdata);
	return PJ_SUCCESS;
}

static pjsip_module rfc7329_tsx_module = {
	.name = { "RFC7329 Session-ID TSX", 24 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 1,
	.on_rx_request = rfc7329_on_rx_request,
	.on_tx_request = rfc7329_on_tx_request,
	.on_tx_response = rfc7329_on_tx_response,
	.on_tsx_state = rfc7329_on_tsx_state,
};

static int load_module(void)
{
	if (!rfc7329_option_enabled()) {
		rfc7329_active = 0;
		return AST_MODULE_LOAD_SUCCESS;
	}

	if (rfc7329_activate()) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	rfc7329_deactivate();
	return 0;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	if (rfc7329_option_enabled() && !rfc7329_active) {
		return rfc7329_activate();
	}

	if (!rfc7329_option_enabled() && rfc7329_active) {
		rfc7329_deactivate();
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP RFC7329 Session-ID Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
