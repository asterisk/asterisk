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
/* Needed for SUBSCRIBE, NOTIFY, and PUBLISH method definitions */
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_sip.h"
#include "res_sip/include/res_sip_private.h"
#include "asterisk/linkedlists.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/threadpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/uuid.h"
#include "asterisk/sorcery.h"

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_sorcery_config</depend>
	<support_level>core</support_level>
 ***/

static pjsip_endpoint *ast_pjsip_endpoint;

static struct ast_threadpool *sip_threadpool;

static int register_service(void *data)
{
	pjsip_module **module = data;
	if (!ast_pjsip_endpoint) {
		ast_log(LOG_ERROR, "There is no PJSIP endpoint. Unable to register services\n");
		return -1;
	}
	if (pjsip_endpt_register_module(ast_pjsip_endpoint, *module) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to register module %.*s\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name));
		return -1;
	}
	ast_debug(1, "Registered SIP service %.*s (%p)\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name), *module);
	ast_module_ref(ast_module_info->self);
	return 0;
}

int ast_sip_register_service(pjsip_module *module)
{
	return ast_sip_push_task_synchronous(NULL, register_service, &module);
}

static int unregister_service(void *data)
{
	pjsip_module **module = data;
	ast_module_unref(ast_module_info->self);
	if (!ast_pjsip_endpoint) {
		return -1;
	}
	pjsip_endpt_unregister_module(ast_pjsip_endpoint, *module);
	ast_debug(1, "Unregistered SIP service %.*s\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name));
	return 0;
}

void ast_sip_unregister_service(pjsip_module *module)
{
	ast_sip_push_task_synchronous(NULL, unregister_service, &module);
}

static struct ast_sip_authenticator *registered_authenticator;

int ast_sip_register_authenticator(struct ast_sip_authenticator *auth)
{
	if (registered_authenticator) {
		ast_log(LOG_WARNING, "Authenticator %p is already registered. Cannot register a new one\n", registered_authenticator);
		return -1;
	}
	registered_authenticator = auth;
	ast_debug(1, "Registered SIP authenticator module %p\n", auth);
	ast_module_ref(ast_module_info->self);
	return 0;
}

void ast_sip_unregister_authenticator(struct ast_sip_authenticator *auth)
{
	if (registered_authenticator != auth) {
		ast_log(LOG_WARNING, "Trying to unregister authenticator %p but authenticator %p registered\n",
				auth, registered_authenticator);
		return;
	}
	registered_authenticator = NULL;
	ast_debug(1, "Unregistered SIP authenticator %p\n", auth);
	ast_module_unref(ast_module_info->self);
}

int ast_sip_requires_authentication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	if (!registered_authenticator) {
		ast_log(LOG_WARNING, "No SIP authenticator registered. Assuming authentication is not required\n");
		return 0;
	}

	return registered_authenticator->requires_authentication(endpoint, rdata);
}

enum ast_sip_check_auth_result ast_sip_check_authentication(struct ast_sip_endpoint *endpoint,
		pjsip_rx_data *rdata, pjsip_tx_data *tdata)
{
	if (!registered_authenticator) {
		ast_log(LOG_WARNING, "No SIP authenticator registered. Assuming authentication is successful\n");
		return 0;
	}
	return registered_authenticator->check_authentication(endpoint, rdata, tdata);
}

static struct ast_sip_outbound_authenticator *registered_outbound_authenticator;

int ast_sip_register_outbound_authenticator(struct ast_sip_outbound_authenticator *auth)
{
	if (registered_outbound_authenticator) {
		ast_log(LOG_WARNING, "Outbound authenticator %p is already registered. Cannot register a new one\n", registered_outbound_authenticator);
		return -1;
	}
	registered_outbound_authenticator = auth;
	ast_debug(1, "Registered SIP outbound authenticator module %p\n", auth);
	ast_module_ref(ast_module_info->self);
	return 0;
}

void ast_sip_unregister_outbound_authenticator(struct ast_sip_outbound_authenticator *auth)
{
	if (registered_outbound_authenticator != auth) {
		ast_log(LOG_WARNING, "Trying to unregister outbound authenticator %p but outbound authenticator %p registered\n",
				auth, registered_outbound_authenticator);
		return;
	}
	registered_outbound_authenticator = NULL;
	ast_debug(1, "Unregistered SIP outbound authenticator %p\n", auth);
	ast_module_unref(ast_module_info->self);
}

int ast_sip_create_request_with_auth(const char **auths, size_t num_auths, pjsip_rx_data *challenge,
		pjsip_transaction *tsx, pjsip_tx_data **new_request)
{
	if (!registered_outbound_authenticator) {
		ast_log(LOG_WARNING, "No SIP outbound authenticator registered. Cannot respond to authentication challenge\n");
		return -1;
	}
	return registered_outbound_authenticator->create_request_with_auth(auths, num_auths, challenge, tsx, new_request);
}

struct endpoint_identifier_list {
	struct ast_sip_endpoint_identifier *identifier;
	AST_RWLIST_ENTRY(endpoint_identifier_list) list;
};

static AST_RWLIST_HEAD_STATIC(endpoint_identifiers, endpoint_identifier_list);

int ast_sip_register_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier)
{
	struct endpoint_identifier_list *id_list_item;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	id_list_item = ast_calloc(1, sizeof(*id_list_item));
	if (!id_list_item) {
		ast_log(LOG_ERROR, "Unabled to add endpoint identifier. Out of memory.\n");
		return -1;
	}
	id_list_item->identifier = identifier;

	AST_RWLIST_INSERT_TAIL(&endpoint_identifiers, id_list_item, list);
	ast_debug(1, "Registered endpoint identifier %p\n", identifier);

	ast_module_ref(ast_module_info->self);
	return 0;
}

void ast_sip_unregister_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier)
{
	struct endpoint_identifier_list *iter;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_identifiers, iter, list) {
		if (iter->identifier == identifier) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(iter);
			ast_debug(1, "Unregistered endpoint identifier %p\n", identifier);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

struct ast_sip_endpoint *ast_sip_identify_endpoint(pjsip_rx_data *rdata)
{
	struct endpoint_identifier_list *iter;
	struct ast_sip_endpoint *endpoint = NULL;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE(&endpoint_identifiers, iter, list) {
		ast_assert(iter->identifier->identify_endpoint != NULL);
		endpoint = iter->identifier->identify_endpoint(rdata);
		if (endpoint) {
			break;
		}
	}
	return endpoint;
}

pjsip_endpoint *ast_sip_get_pjsip_endpoint(void)
{
	return ast_pjsip_endpoint;
}

static int sip_dialog_create_from(pj_pool_t *pool, pj_str_t *from, const char *user, const pj_str_t *target, pjsip_tpselector *selector)
{
	pj_str_t tmp, local_addr;
	pjsip_uri *uri;
	pjsip_sip_uri *sip_uri;
	pjsip_transport_type_e type = PJSIP_TRANSPORT_UNSPECIFIED;
	int local_port;
	char uuid_str[AST_UUID_STR_LEN];

	if (!user) {
		RAII_VAR(struct ast_uuid *, uuid, ast_uuid_generate(), ast_free_ptr);
		if (!uuid) {
			return -1;
		}
		user = ast_uuid_to_str(uuid, uuid_str, sizeof(uuid_str));
	}

	/* Parse the provided target URI so we can determine what transport it will end up using */
	pj_strdup_with_null(pool, &tmp, target);

	if (!(uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0)) ||
	    (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return -1;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	/* Determine the transport type to use */
	if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri)) {
		type = PJSIP_TRANSPORT_TLS;
	} else if (!sip_uri->transport_param.slen) {
		type = PJSIP_TRANSPORT_UDP;
	} else {
		type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
	}

	if (type == PJSIP_TRANSPORT_UNSPECIFIED) {
		return -1;
	}

	/* If the host is IPv6 turn the transport into an IPv6 version */
	if (pj_strchr(&sip_uri->host, ':')) {
		type = (pjsip_transport_type_e)(((int)type) + PJSIP_TRANSPORT_IPV6);
	}

	/* Get the local bound address for the transport that will be used when communicating with the provided URI */
	if (pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), pool, type, selector,
							      &local_addr, &local_port) != PJ_SUCCESS) {
		return -1;
	}

	/* If IPv6 was not specified in the host but is in the transport, set the proper type */
	if (!pj_strchr(&sip_uri->host, ':') && pj_strchr(&local_addr, ':')) {
		type = (pjsip_transport_type_e)(((int)type) + PJSIP_TRANSPORT_IPV6);
	}

	from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
	from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				      "<%s:%s@%s%.*s%s:%d%s%s>",
				      (pjsip_transport_get_flag_from_type(type) & PJSIP_TRANSPORT_SECURE) ? "sips" : "sip",
				      user,
				      (type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
				      (int)local_addr.slen,
				      local_addr.ptr,
				      (type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
				      local_port,
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");

	return 0;
}

static int sip_get_tpselector_from_endpoint(const struct ast_sip_endpoint *endpoint, pjsip_tpselector *selector)
{
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);
	const char *transport_name = endpoint->transport;

	if (ast_strlen_zero(transport_name)) {
		return 0;
	}

	transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_name);

	if (!transport || !transport->state) {
		return -1;
	}

	if (transport->type == AST_TRANSPORT_UDP) {
		selector->type = PJSIP_TPSELECTOR_TRANSPORT;
		selector->u.transport = transport->state->transport;
	} else if (transport->type == AST_TRANSPORT_TCP || transport->type == AST_TRANSPORT_TLS) {
		selector->type = PJSIP_TPSELECTOR_LISTENER;
		selector->u.listener = transport->state->factory;
	} else {
		return -1;
	}

	return 0;
}

pjsip_dialog *ast_sip_create_dialog(const struct ast_sip_endpoint *endpoint, const char *uri, const char *request_user)
{
	pj_str_t local_uri = { "sip:temp@temp", 13 }, remote_uri;
	pjsip_dialog *dlg = NULL;
	const char *outbound_proxy = endpoint->outbound_proxy;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	static const pj_str_t HCONTACT = { "Contact", 7 };

	pj_cstr(&remote_uri, uri);

	if (pjsip_dlg_create_uac(pjsip_ua_instance(), &local_uri, NULL, &remote_uri, NULL, &dlg) != PJ_SUCCESS) {
		return NULL;
	}

	if (sip_get_tpselector_from_endpoint(endpoint, &selector)) {
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

	if (sip_dialog_create_from(dlg->pool, &local_uri, NULL, &remote_uri, &selector)) {
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

	/* Update the dialog with the new local URI, we do it afterwards so we can use the dialog pool for construction */
	pj_strdup_with_null(dlg->pool, &dlg->local.info_str, &local_uri);
	dlg->local.info->uri = pjsip_parse_uri(dlg->pool, dlg->local.info_str.ptr, dlg->local.info_str.slen, 0);
	dlg->local.contact = pjsip_parse_hdr(dlg->pool, &HCONTACT, local_uri.ptr, local_uri.slen, NULL);

	/* If a request user has been specified and we are permitted to change it, do so */
	if (!ast_strlen_zero(request_user) && (PJSIP_URI_SCHEME_IS_SIP(dlg->target) || PJSIP_URI_SCHEME_IS_SIPS(dlg->target))) {
		pjsip_sip_uri *target = pjsip_uri_get_uri(dlg->target);
		pj_strdup2(dlg->pool, &target->user, request_user);
	}

	/* We have to temporarily bump up the sess_count here so the dialog is not prematurely destroyed */
	dlg->sess_count++;

	pjsip_dlg_set_transport(dlg, &selector);

	if (!ast_strlen_zero(outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };
		pj_str_t tmp;

		pj_list_init(&route_set);

		pj_strdup2_with_null(dlg->pool, &tmp, outbound_proxy);
		if (!(route = pjsip_parse_hdr(dlg->pool, &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL))) {
			pjsip_dlg_terminate(dlg);
			return NULL;
		}
		pj_list_push_back(&route_set, route);

		pjsip_dlg_set_route_set(dlg, &route_set);
	}

	dlg->sess_count--;

	return dlg;
}

/* PJSIP doesn't know about the INFO method, so we have to define it ourselves */
const pjsip_method pjsip_info_method = {PJSIP_OTHER_METHOD, {"INFO", 4} };

static struct {
	const char *method;
	const pjsip_method *pmethod;
} methods [] = {
	{ "INVITE", &pjsip_invite_method },
	{ "CANCEL", &pjsip_cancel_method },
	{ "ACK", &pjsip_ack_method },
	{ "BYE", &pjsip_bye_method },
	{ "REGISTER", &pjsip_register_method },
	{ "OPTIONS", &pjsip_options_method },
	{ "SUBSCRIBE", &pjsip_subscribe_method },
	{ "NOTIFY", &pjsip_notify_method },
	{ "PUBLISH", &pjsip_publish_method },
	{ "INFO", &pjsip_info_method },
};

static const pjsip_method *get_pjsip_method(const char *method)
{
	int i;
	for (i = 0; i < ARRAY_LEN(methods); ++i) {
		if (!strcmp(method, methods[i].method)) {
			return methods[i].pmethod;
		}
	}
	return NULL;
}

static int create_in_dialog_request(const pjsip_method *method, struct pjsip_dialog *dlg, pjsip_tx_data **tdata)
{
	if (pjsip_dlg_create_request(dlg, method, -1, tdata) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to create in-dialog request.\n");
		return -1;
	}

	return 0;
}

static int create_out_of_dialog_request(const pjsip_method *method, struct ast_sip_endpoint *endpoint,
		const char *uri, pjsip_tx_data **tdata)
{
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);
	pj_str_t remote_uri;
	pj_str_t from;
	pj_pool_t *pool;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };

	if (ast_strlen_zero(uri)) {
		contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
		if (!contact || ast_strlen_zero(contact->uri)) {
			ast_log(LOG_ERROR, "Unable to retrieve contact for endpoint %s\n",
					ast_sorcery_object_get_id(endpoint));
			return -1;
		}

		pj_cstr(&remote_uri, contact->uri);
	} else {
		pj_cstr(&remote_uri, uri);
	}

	if (sip_get_tpselector_from_endpoint(endpoint, &selector)) {
		ast_log(LOG_ERROR, "Unable to retrieve PJSIP transport selector for endpoint %s\n",
				ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Outbound request", 256, 256);

	if (!pool) {
		ast_log(LOG_ERROR, "Unable to create PJLIB memory pool\n");
		return -1;
	}

	if (sip_dialog_create_from(pool, &from, NULL, &remote_uri, &selector)) {
		ast_log(LOG_ERROR, "Unable to create From header for %.*s request to endpoint %s\n",
				(int) pj_strlen(&method->name), pj_strbuf(&method->name), ast_sorcery_object_get_id(endpoint));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	if (pjsip_endpt_create_request(ast_sip_get_pjsip_endpoint(), method, &remote_uri,
			&from, &remote_uri, &from, NULL, -1, NULL, tdata) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create outbound %.*s request to endpoint %s\n",
				(int) pj_strlen(&method->name), pj_strbuf(&method->name), ast_sorcery_object_get_id(endpoint));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	/* We can release this pool since request creation copied all the necessary
	 * data into the outbound request's pool
	 */
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return 0;
}

int ast_sip_create_request(const char *method, struct pjsip_dialog *dlg,
		struct ast_sip_endpoint *endpoint, const char *uri, pjsip_tx_data **tdata)
{
	const pjsip_method *pmethod = get_pjsip_method(method);

	if (!pmethod) {
		ast_log(LOG_WARNING, "Unknown method '%s'. Cannot send request\n", method);
		return -1;
	}

	if (dlg) {
		return create_in_dialog_request(pmethod, dlg, tdata);
	} else {
		return create_out_of_dialog_request(pmethod, endpoint, uri, tdata);
	}
}

static int send_in_dialog_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg)
{
	if (pjsip_dlg_send_request(dlg, tdata, -1, NULL) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to send in-dialog request.\n");
		return -1;
	}
	return 0;
}

static void send_request_cb(void *token, pjsip_event *e)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, token, ao2_cleanup);
	pjsip_transaction *tsx = e->body.tsx_state.tsx;
	pjsip_rx_data *challenge = e->body.tsx_state.src.rdata;
	pjsip_tx_data *tdata;

	if (tsx->status_code != 401 && tsx->status_code != 407) {
		return;
	}

	if (!ast_sip_create_request_with_auth(endpoint->sip_outbound_auths, endpoint->num_outbound_auths, challenge, tsx, &tdata)) {
		pjsip_endpt_send_request(ast_sip_get_pjsip_endpoint(), tdata, -1, NULL, NULL);
	}
}

static int send_out_of_dialog_request(pjsip_tx_data *tdata, struct ast_sip_endpoint *endpoint)
{
	ao2_ref(endpoint, +1);
	if (pjsip_endpt_send_request(ast_sip_get_pjsip_endpoint(), tdata, -1, endpoint, send_request_cb) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Error attempting to send outbound %.*s request to endpoint %s\n",
				(int) pj_strlen(&tdata->msg->line.req.method.name),
				pj_strbuf(&tdata->msg->line.req.method.name),
				ast_sorcery_object_get_id(endpoint));
		ao2_ref(endpoint, -1);
		return -1;
	}

	return 0;
}

int ast_sip_send_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg, struct ast_sip_endpoint *endpoint)
{
	ast_assert(tdata->msg->type == PJSIP_REQUEST_MSG);

	if (dlg) {
		return send_in_dialog_request(tdata, dlg);
	} else {
		return send_out_of_dialog_request(tdata, endpoint);
	}
}

int ast_sip_add_header(pjsip_tx_data *tdata, const char *name, const char *value)
{
	pj_str_t hdr_name;
	pj_str_t hdr_value;
	pjsip_generic_string_hdr *hdr;
	
	pj_cstr(&hdr_name, name);
	pj_cstr(&hdr_value, value);

	hdr = pjsip_generic_string_hdr_create(tdata->pool, &hdr_name, &hdr_value);

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
	return 0;
}

static pjsip_msg_body *ast_body_to_pjsip_body(pj_pool_t *pool, const struct ast_sip_body *body)
{
	pj_str_t type;
	pj_str_t subtype;
	pj_str_t body_text;

	pj_cstr(&type, body->type);
	pj_cstr(&subtype, body->subtype);
	pj_cstr(&body_text, body->body_text);
	
	return pjsip_msg_body_create(pool, &type, &subtype, &body_text);
}

int ast_sip_add_body(pjsip_tx_data *tdata, const struct ast_sip_body *body)
{
	pjsip_msg_body *pjsip_body = ast_body_to_pjsip_body(tdata->pool, body);
	tdata->msg->body = pjsip_body;
	return 0;
}

int ast_sip_add_body_multipart(pjsip_tx_data *tdata, const struct ast_sip_body *bodies[], int num_bodies)
{
	int i;
	/* NULL for type and subtype automatically creates "multipart/mixed" */
	pjsip_msg_body *body = pjsip_multipart_create(tdata->pool, NULL, NULL);

	for (i = 0; i < num_bodies; ++i) {
		pjsip_multipart_part *part = pjsip_multipart_create_part(tdata->pool);
		part->body = ast_body_to_pjsip_body(tdata->pool, bodies[i]);
		pjsip_multipart_add_part(tdata->pool, body, part);
	}

	tdata->msg->body = body;
	return 0;
}

int ast_sip_append_body(pjsip_tx_data *tdata, const char *body_text)
{
	size_t combined_size = strlen(body_text) + tdata->msg->body->len;
	struct ast_str *body_buffer = ast_str_alloca(combined_size);

	ast_str_set(&body_buffer, 0, "%.*s%s", (int) tdata->msg->body->len, (char *) tdata->msg->body->data, body_text);

	tdata->msg->body->data = pj_pool_alloc(tdata->pool, combined_size);
	pj_memcpy(tdata->msg->body->data, ast_str_buffer(body_buffer), combined_size);
	tdata->msg->body->len = combined_size;

	return 0;
}

struct ast_taskprocessor *ast_sip_create_serializer(void)
{
	struct ast_taskprocessor *serializer;
	RAII_VAR(struct ast_uuid *, uuid, ast_uuid_generate(), ast_free_ptr);
	char name[AST_UUID_STR_LEN];

	if (!uuid) {
		return NULL;
	}

	ast_uuid_to_str(uuid, name, sizeof(name));

	serializer = ast_threadpool_serializer(name, sip_threadpool);
	if (!serializer) {
		return NULL;
	}
	return serializer;
}

int ast_sip_push_task(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (serializer) {
		return ast_taskprocessor_push(serializer, sip_task, task_data);
	} else {
		return ast_threadpool_push(sip_threadpool, sip_task, task_data);
	}
}

struct sync_task_data {
	ast_mutex_t lock;
	ast_cond_t cond;
	int complete;
	int fail;
	int (*task)(void *);
	void *task_data;
};

static int sync_task(void *data)
{
	struct sync_task_data *std = data;
	std->fail = std->task(std->task_data);

	ast_mutex_lock(&std->lock);
	std->complete = 1;
	ast_cond_signal(&std->cond);
	ast_mutex_unlock(&std->lock);
	return std->fail;
}

int ast_sip_push_task_synchronous(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	/* This method is an onion */
	struct sync_task_data std;
	ast_mutex_init(&std.lock);
	ast_cond_init(&std.cond, NULL);
	std.fail = std.complete = 0;
	std.task = sip_task;
	std.task_data = task_data;

	if (serializer) {
		if (ast_taskprocessor_push(serializer, sync_task, &std)) {
			return -1;
		}
	} else {
		if (ast_threadpool_push(sip_threadpool, sync_task, &std)) {
			return -1;
		}
	}

	ast_mutex_lock(&std.lock);
	while (!std.complete) {
		ast_cond_wait(&std.cond, &std.lock);
	}
	ast_mutex_unlock(&std.lock);

	ast_mutex_destroy(&std.lock);
	ast_cond_destroy(&std.cond);
	return std.fail;
}

void ast_copy_pj_str(char *dest, pj_str_t *src, size_t size)
{
	size_t chars_to_copy = MIN(size - 1, pj_strlen(src));
	memcpy(dest, pj_strbuf(src), chars_to_copy);
	dest[chars_to_copy] = '\0';
}

pj_caching_pool caching_pool;
pj_pool_t *memory_pool;
pj_thread_t *monitor_thread;
static int monitor_continue;

static void *monitor_thread_exec(void *endpt)
{
	while (monitor_continue) {
		const pj_time_val delay = {0, 10};
		pjsip_endpt_handle_events(ast_pjsip_endpoint, &delay);
	}
	return NULL;
}

static void stop_monitor_thread(void)
{
	monitor_continue = 0;
	pj_thread_join(monitor_thread);
}

AST_THREADSTORAGE(pj_thread_storage);
AST_THREADSTORAGE(servant_id_storage);
#define SIP_SERVANT_ID 0xDEFECA7E

static void sip_thread_start(void)
{
	pj_thread_desc *desc;
	pj_thread_t *thread;
	uint32_t *servant_id;

	servant_id = ast_threadstorage_get(&servant_id_storage, sizeof(*servant_id));
	if (!servant_id) {
		ast_log(LOG_ERROR, "Could not set SIP servant ID in thread-local storage.\n");
		return;
	}
	*servant_id = SIP_SERVANT_ID;

	desc = ast_threadstorage_get(&pj_thread_storage, sizeof(pj_thread_desc));
	if (!desc) {
		ast_log(LOG_ERROR, "Could not get thread desc from thread-local storage. Expect awful things to occur\n");
		return;
	}
	pj_bzero(*desc, sizeof(*desc));

	if (pj_thread_register("Asterisk Thread", *desc, &thread) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Couldn't register thread with PJLIB.\n");
	}
}

int ast_sip_thread_is_servant(void)
{
	uint32_t *servant_id;

	servant_id = ast_threadstorage_get(&servant_id_storage, sizeof(*servant_id));
	if (!servant_id) {
		return 0;
	}

	return *servant_id == SIP_SERVANT_ID;
}

static int load_module(void)
{
    /* The third parameter is just copied from
     * example code from PJLIB. This can be adjusted
     * if necessary.
	 */
	pj_status_t status;

	/* XXX For the time being, create hard-coded threadpool
	 * options. Just bump up by five threads every time we
	 * don't have any available threads. Idle threads time
	 * out after a minute. No maximum size
	 */
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.auto_increment = 5,
		.max_size = 0,
		.idle_timeout = 60,
		.initial_size = 0,
		.thread_start = sip_thread_start,
	};
	sip_threadpool = ast_threadpool_create("SIP", NULL, &options);

	if (pj_init() != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjlib_util_init() != PJ_SUCCESS) {
		pj_shutdown();
		return AST_MODULE_LOAD_DECLINE;
	}

	pj_caching_pool_init(&caching_pool, NULL, 1024 * 1024);
	if (pjsip_endpt_create(&caching_pool.factory, "SIP", &ast_pjsip_endpoint) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to create PJSIP endpoint structure. Aborting load\n");
		goto error;
	}
	memory_pool = pj_pool_create(&caching_pool.factory, "SIP", 1024, 1024, NULL);
	if (!memory_pool) {
		ast_log(LOG_ERROR, "Failed to create memory pool for SIP. Aborting load\n");
		goto error;
	}

	pjsip_tsx_layer_init_module(ast_pjsip_endpoint);
	pjsip_ua_init_module(ast_pjsip_endpoint, NULL);

	monitor_continue = 1;
	status = pj_thread_create(memory_pool, "SIP", (pj_thread_proc *) &monitor_thread_exec,
			NULL, PJ_THREAD_DEFAULT_STACK_SIZE * 2, 0, &monitor_thread);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to start SIP monitor thread. Aborting load\n");
		goto error;
	}

	if (ast_res_sip_initialize_configuration()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP configuration. Aborting load\n");
		goto error;
	}

	if (ast_sip_initialize_distributor()) {
		ast_log(LOG_ERROR, "Failed to register distributor module. Aborting load\n");
		goto error;
	}

	if (ast_sip_initialize_outbound_authentication()) {
		ast_log(LOG_ERROR, "Failed to initialize outbound authentication. Aborting load\n");
		goto error;
	}

	ast_res_sip_init_options_handling(0);

return AST_MODULE_LOAD_SUCCESS;

error:
	ast_res_sip_destroy_configuration();
	if (monitor_thread) {
		stop_monitor_thread();
	}
	if (memory_pool) {
		pj_pool_release(memory_pool);
		memory_pool = NULL;
	}
	if (ast_pjsip_endpoint) {
		pjsip_endpt_destroy(ast_pjsip_endpoint);
		ast_pjsip_endpoint = NULL;
	}
	pj_caching_pool_destroy(&caching_pool);
	/* XXX Should have a way of stopping monitor thread */
	return AST_MODULE_LOAD_DECLINE;
}

static int reload_module(void)
{
	if (ast_res_sip_reload_configuration()) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_res_sip_init_options_handling(1);
	return 0;
}

static int unload_pjsip(void *data)
{
	if (memory_pool) {
		pj_pool_release(memory_pool);
		memory_pool = NULL;
	}
	if (ast_pjsip_endpoint) {
		pjsip_endpt_destroy(ast_pjsip_endpoint);
		ast_pjsip_endpoint = NULL;
	}
	pj_caching_pool_destroy(&caching_pool);
	return 0;
}

static int unload_module(void)
{
	ast_res_sip_destroy_configuration();
	if (monitor_thread) {
		stop_monitor_thread();
	}
	/* The thread this is called from cannot call PJSIP/PJLIB functions,
	 * so we have to push the work to the threadpool to handle
	 */
	ast_sip_push_task_synchronous(NULL, unload_pjsip, NULL);

	ast_threadpool_shutdown(sip_threadpool);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Basic SIP resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
);
