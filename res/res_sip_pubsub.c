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
 * \brief Opaque structure representing an RFC 3265 SIP subscription
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_sip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_sip_pubsub.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/astobj2.h"
#include "asterisk/datastore.h"
#include "asterisk/uuid.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/res_sip.h"

static pj_bool_t sub_on_rx_request(pjsip_rx_data *rdata);

static struct pjsip_module sub_module = {
	.name = { "PubSub Module", 13 },
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = sub_on_rx_request,
};

/*!
 * \brief Structure representing a SIP subscription
 */
struct ast_sip_subscription {
	/*! Subscription datastores set up by handlers */
	struct ao2_container *datastores;
	/*! The endpoint with which the subscription is communicating */
	struct ast_sip_endpoint *endpoint;
	/*! Serializer on which to place operations for this subscription */
	struct ast_taskprocessor *serializer;
	/*! The handler for this subscription */
	const struct ast_sip_subscription_handler *handler;
	/*! The role for this subscription */
	enum ast_sip_subscription_role role;
	/*! The underlying PJSIP event subscription structure */
	pjsip_evsub *evsub;
	/*! The underlying PJSIP dialog */
	pjsip_dialog *dlg;
};

#define DATASTORE_BUCKETS 53

#define DEFAULT_EXPIRES 3600

static int datastore_hash(const void *obj, int flags)
{
	const struct ast_datastore *datastore = obj;
	const char *uid = flags & OBJ_KEY ? obj : datastore->uid;

	ast_assert(uid != NULL);

	return ast_str_hash(uid);
}

static int datastore_cmp(void *obj, void *arg, int flags)
{
	const struct ast_datastore *datastore1 = obj;
	const struct ast_datastore *datastore2 = arg;
	const char *uid2 = flags & OBJ_KEY ? arg : datastore2->uid;

	ast_assert(datastore1->uid != NULL);
	ast_assert(uid2 != NULL);

	return strcmp(datastore1->uid, uid2) ? 0 : CMP_MATCH | CMP_STOP;
}

static void subscription_destructor(void *obj)
{
	struct ast_sip_subscription *sub = obj;

	ast_debug(3, "Destroying SIP subscription\n");

	ao2_cleanup(sub->datastores);
	ao2_cleanup(sub->endpoint);

	if (sub->dlg) {
		/* This is why we keep the dialog on the subscription. When the subscription
		 * is destroyed, there is no guarantee that the underlying dialog is ready
		 * to be destroyed. Furthermore, there's no guarantee in the opposite direction
		 * either. The dialog could be destroyed before our subscription is. We fix
		 * this problem by keeping a reference to the dialog until it is time to
		 * destroy the subscription. We need to have the dialog available when the
		 * subscription is destroyed so that we can guarantee that our attempt to
		 * remove the serializer will be successful.
		 */
		ast_sip_dialog_set_serializer(sub->dlg, NULL);
		pjsip_dlg_dec_session(sub->dlg, &sub_module);
	}
	ast_taskprocessor_unreference(sub->serializer);
}

static void pubsub_on_evsub_state(pjsip_evsub *sub, pjsip_event *event);
static void pubsub_on_tsx_state(pjsip_evsub *sub, pjsip_transaction *tsx, pjsip_event *event);
static void pubsub_on_rx_refresh(pjsip_evsub *sub, pjsip_rx_data *rdata,
		int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);
static void pubsub_on_rx_notify(pjsip_evsub *sub, pjsip_rx_data *rdata, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body);
static void pubsub_on_client_refresh(pjsip_evsub *sub);
static void pubsub_on_server_timeout(pjsip_evsub *sub);


static pjsip_evsub_user pubsub_cb = {
	.on_evsub_state = pubsub_on_evsub_state,
	.on_tsx_state = pubsub_on_tsx_state,
	.on_rx_refresh = pubsub_on_rx_refresh,
	.on_rx_notify = pubsub_on_rx_notify,
	.on_client_refresh = pubsub_on_client_refresh,
	.on_server_timeout = pubsub_on_server_timeout,
};

static pjsip_evsub *allocate_evsub(const char *event, enum ast_sip_subscription_role role,
		struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pjsip_dialog *dlg)
{
	pjsip_evsub *evsub;
	/* PJSIP is kind enough to have some built-in support for certain
	 * events. We need to use the correct initialization function for the
	 * built-in events
	 */
	if (role == AST_SIP_NOTIFIER) {
		if (!strcmp(event, "message-summary")) {
			pjsip_mwi_create_uas(dlg, &pubsub_cb, rdata, &evsub);
		} else {
			pjsip_evsub_create_uas(dlg, &pubsub_cb, rdata, 0, &evsub);
		}
	} else {
		if (!strcmp(event, "message-summary")) {
			pjsip_mwi_create_uac(dlg, &pubsub_cb, 0, &evsub);
		} else {
			pj_str_t pj_event;
			pj_cstr(&pj_event, event);
			pjsip_evsub_create_uac(dlg, &pubsub_cb, &pj_event, 0, &evsub);
		}
	}
	return evsub;
}

struct ast_sip_subscription *ast_sip_create_subscription(const struct ast_sip_subscription_handler *handler,
        enum ast_sip_subscription_role role, struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	struct ast_sip_subscription *sub = ao2_alloc(sizeof(*sub), subscription_destructor);
	pjsip_dialog *dlg;

	if (!sub) {
		return NULL;
	}
	sub->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!sub->datastores) {
		ao2_ref(sub, -1);
		return NULL;
	}
	sub->serializer = ast_sip_create_serializer();
	if (!sub->serializer) {
		ao2_ref(sub, -1);
		return NULL;
	}
	sub->role = role;
	if (role == AST_SIP_NOTIFIER) {
		pjsip_dlg_create_uas(pjsip_ua_instance(), rdata, NULL, &dlg);
	} else {
		RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

		contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
		if (!contact || ast_strlen_zero(contact->uri)) {
			ast_log(LOG_WARNING, "No contacts configured for endpoint %s. Unable to create SIP subsription\n",
					ast_sorcery_object_get_id(endpoint));
			ao2_ref(sub, -1);
			return NULL;
		}
		dlg = ast_sip_create_dialog(endpoint, contact->uri, NULL);
	}
	if (!dlg) {
		ast_log(LOG_WARNING, "Unable to create dialog for SIP subscription\n");
		ao2_ref(sub, -1);
		return NULL;
	}
	sub->evsub = allocate_evsub(handler->event_name, role, endpoint, rdata, dlg);
	/* We keep a reference to the dialog until our subscription is destroyed. See
	 * the subscription_destructor for more details
	 */
	pjsip_dlg_inc_session(dlg, &sub_module);
	sub->dlg = dlg;
	ast_sip_dialog_set_serializer(dlg, sub->serializer);
	pjsip_evsub_set_mod_data(sub->evsub, sub_module.id, sub);
	ao2_ref(endpoint, +1);
	sub->endpoint = endpoint;
	sub->handler = handler;
	return sub;
}
 
struct ast_sip_endpoint *ast_sip_subscription_get_endpoint(struct ast_sip_subscription *sub)
{
	ast_assert(sub->endpoint != NULL);
	ao2_ref(sub->endpoint, +1);
	return sub->endpoint;
}
 
struct ast_taskprocessor *ast_sip_subscription_get_serializer(struct ast_sip_subscription *sub)
{
	ast_assert(sub->serializer != NULL);
	return sub->serializer;
}

pjsip_evsub *ast_sip_subscription_get_evsub(struct ast_sip_subscription *sub)
{
	return sub->evsub;
}

pjsip_dialog *ast_sip_subscription_get_dlg(struct ast_sip_subscription *sub)
{
	return sub->dlg;
}

int ast_sip_subscription_send_request(struct ast_sip_subscription *sub, pjsip_tx_data *tdata)
{
	return pjsip_evsub_send_request(ast_sip_subscription_get_evsub(sub),
			tdata) == PJ_SUCCESS ? 0 : -1;
}

static void subscription_datastore_destroy(void *obj)
{
	struct ast_datastore *datastore = obj;

	/* Using the destroy function (if present) destroy the data */
	if (datastore->info->destroy != NULL && datastore->data != NULL) {
		datastore->info->destroy(datastore->data);
		datastore->data = NULL;
	}

	ast_free((void *) datastore->uid);
	datastore->uid = NULL;
}

struct ast_datastore *ast_sip_subscription_alloc_datastore(const struct ast_datastore_info *info, const char *uid)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	const char *uid_ptr = uid;

	if (!info) {
		return NULL;
	}

	datastore = ao2_alloc(sizeof(*datastore), subscription_datastore_destroy);
	if (!datastore) {
		return NULL;
	}

	datastore->info = info;
	if (ast_strlen_zero(uid)) {
		/* They didn't provide an ID so we'll provide one ourself */
		struct ast_uuid *uuid = ast_uuid_generate();
		char uuid_buf[AST_UUID_STR_LEN];
		if (!uuid) {
			return NULL;
		}
		uid_ptr = ast_uuid_to_str(uuid, uuid_buf, sizeof(uuid_buf));
		ast_free(uuid);
	}

	datastore->uid = ast_strdup(uid_ptr);
	if (!datastore->uid) {
		return NULL;
	}

	ao2_ref(datastore, +1);
	return datastore;
}
 
int ast_sip_subscription_add_datastore(struct ast_sip_subscription *subscription, struct ast_datastore *datastore)
{
	ast_assert(datastore != NULL);
	ast_assert(datastore->info != NULL);
	ast_assert(ast_strlen_zero(datastore->uid) == 0);

	if (!ao2_link(subscription->datastores, datastore)) {
		return -1;
	}
	return 0;
}

struct ast_datastore *ast_sip_subscription_get_datastore(struct ast_sip_subscription *subscription, const char *name)
{
	return ao2_find(subscription->datastores, name, OBJ_KEY);
}

void ast_sip_subscription_remove_datastore(struct ast_sip_subscription *subscription, const char *name)
{
	ao2_callback(subscription->datastores, OBJ_KEY | OBJ_UNLINK | OBJ_NODATA, NULL, (void *) name);
}

AST_RWLIST_HEAD_STATIC(subscription_handlers, ast_sip_subscription_handler);

static void add_handler(struct ast_sip_subscription_handler *handler)
{
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&subscription_handlers, handler, next);
	ast_module_ref(ast_module_info->self);
}

static int handler_exists_for_event_name(const char *event_name)
{
	struct ast_sip_subscription_handler *iter;
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE(&subscription_handlers, iter, next) {
		if (!strcmp(iter->event_name, event_name)) {
			return 1;
		}
	}
	return 0;
}

int ast_sip_register_subscription_handler(struct ast_sip_subscription_handler *handler)
{
	pj_str_t accept[AST_SIP_MAX_ACCEPT];
	int i;

	if (ast_strlen_zero(handler->event_name)) {
		ast_log(LOG_ERROR, "No event package specifief for subscription handler. Cannot register\n");
		return -1;
	}

	if (ast_strlen_zero(handler->accept[0])) {
		ast_log(LOG_ERROR, "Subscription handler must supply at least one 'Accept' format\n");
		return -1;
	}

	for (i = 0; i < AST_SIP_MAX_ACCEPT && !ast_strlen_zero(handler->accept[i]); ++i) {
		pj_cstr(&accept[i], handler->accept[i]);
	}

	if (!handler_exists_for_event_name(handler->event_name)) {
		pj_str_t event;

		pj_cstr(&event, handler->event_name);

		if (!strcmp(handler->event_name, "message-summary")) {
			pjsip_mwi_init_module(ast_sip_get_pjsip_endpoint(), pjsip_evsub_instance());
		} else {
			pjsip_evsub_register_pkg(&sub_module, &event, DEFAULT_EXPIRES, i, accept);
		}
	} else {
		pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), &sub_module, PJSIP_H_ACCEPT, NULL,
			i, accept);
	}

	add_handler(handler);
	return 0;
}

void ast_sip_unregister_subscription_handler(struct ast_sip_subscription_handler *handler)
{
	struct ast_sip_subscription_handler *iter;
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&subscription_handlers, iter, next) {
		if (handler == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static struct ast_sip_subscription_handler *find_handler(const char *event, char accept[AST_SIP_MAX_ACCEPT][64], size_t num_accept)
{
	struct ast_sip_subscription_handler *iter;
	int match = 0;
	SCOPED_LOCK(lock, &subscription_handlers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE(&subscription_handlers, iter, next) {
		int i;
		int j;
		if (strcmp(event, iter->event_name)) {
			ast_debug(3, "Event %s does not match %s\n", event, iter->event_name);
			continue;
		}
		ast_debug(3, "Event name match: %s = %s\n", event, iter->event_name);
		for (i = 0; i < num_accept; ++i) {
			for (j = 0; j < num_accept; ++j) {
				if (ast_strlen_zero(iter->accept[i])) {
					ast_debug(3, "Breaking because subscription handler has run out of 'accept' types\n");
					break;
				}
				if (!strcmp(accept[j], iter->accept[i])) {
					ast_debug(3, "Accept headers match: %s = %s\n", accept[j], iter->accept[i]);
					match = 1;
					break;
				}
				ast_debug(3, "Accept %s does not match %s\n", accept[j], iter->accept[i]);
			}
			if (match) {
				break;
			}
		}
		if (match) {
			break;
		}
	}

	return iter;
}

static pj_bool_t sub_on_rx_request(pjsip_rx_data *rdata)
{
	static const pj_str_t event_name = { "Event", 5 };
	char event[32];
	char accept[AST_SIP_MAX_ACCEPT][64];
	pjsip_accept_hdr *accept_header;
	pjsip_event_hdr *event_header;
	struct ast_sip_subscription_handler *handler;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct ast_sip_subscription *sub;
	int i;

	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, pjsip_get_subscribe_method())) {
		return PJ_FALSE;
	}

	endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	ast_assert(endpoint != NULL);

	event_header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &event_name, rdata->msg_info.msg->hdr.next);
	if (!event_header) {
		ast_log(LOG_WARNING, "Incoming SUBSCRIBE request with no Event header\n");
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 489, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	accept_header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_ACCEPT, rdata->msg_info.msg->hdr.next);
	if (!accept_header) {
		ast_log(LOG_WARNING, "Incoming SUBSCRIBE request with no Accept header\n");
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 400, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	ast_copy_pj_str(event, &event_header->event_type, sizeof(event));
	for (i = 0; i < accept_header->count; ++i) {
		ast_copy_pj_str(accept[i], &accept_header->values[i], sizeof(accept[i]));
	}

	handler = find_handler(event, accept, accept_header->count);
	if (!handler) {
		ast_log(LOG_WARNING, "No registered handler for event %s\n", event);
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 489, NULL, NULL, NULL);
		return PJ_TRUE;
	}
	sub = handler->new_subscribe(endpoint, rdata);
	if (!sub) {
		pjsip_transaction *trans = pjsip_rdata_get_tsx(rdata);

		if (trans) {
			pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
			pjsip_tx_data *tdata;

			if (pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, &tdata) != PJ_SUCCESS) {
				return PJ_TRUE;
			}
			pjsip_dlg_send_response(dlg, trans, tdata);
		} else {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		}
	}
	return PJ_TRUE;
}

static void pubsub_on_evsub_state(pjsip_evsub *evsub, pjsip_event *event)
{
	struct ast_sip_subscription *sub;
	if (pjsip_evsub_get_state(evsub) != PJSIP_EVSUB_STATE_TERMINATED) {
		return;
	}

	sub = pjsip_evsub_get_mod_data(evsub, sub_module.id);
	if (!sub) {
		return;
	}

	if (event->type == PJSIP_EVENT_RX_MSG) {
		sub->handler->subscription_terminated(sub, event->body.rx_msg.rdata);
	}

	if (event->type == PJSIP_EVENT_TSX_STATE &&
			event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		sub->handler->subscription_terminated(sub, event->body.tsx_state.src.rdata);
	}

	if (sub->handler->subscription_shutdown) {
		sub->handler->subscription_shutdown(sub);
	}
	pjsip_evsub_set_mod_data(evsub, sub_module.id, NULL);
}

static void pubsub_on_tsx_state(pjsip_evsub *evsub, pjsip_transaction *tsx, pjsip_event *event)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, sub_module.id);

	if (!sub) {
		return;
	}

	if (sub->handler->notify_response && tsx->role == PJSIP_ROLE_UAC &&
	    event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		sub->handler->notify_response(sub, event->body.tsx_state.src.rdata);
	}
}

static void set_parameters_from_response_data(pj_pool_t *pool, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body,
		struct ast_sip_subscription_response_data *response_data)
{
	ast_assert(response_data->status_code >= 200 && response_data->status_code <= 699);
	*p_st_code = response_data->status_code;

	if (!ast_strlen_zero(response_data->status_text)) {
		pj_strdup2(pool, *p_st_text, response_data->status_text);
	}

	if (response_data->headers) {
		struct ast_variable *iter;
		for (iter = response_data->headers; iter; iter = iter->next) {
			pj_str_t header_name;
			pj_str_t header_value;	
			pjsip_generic_string_hdr *hdr;

			pj_cstr(&header_name, iter->name);
			pj_cstr(&header_value, iter->value);
			hdr = pjsip_generic_string_hdr_create(pool, &header_name, &header_value);
			pj_list_insert_before(res_hdr, hdr);
		}
	}

	if (response_data->body) {
		pj_str_t type;
		pj_str_t subtype;
		pj_str_t body_text;

		pj_cstr(&type, response_data->body->type);
		pj_cstr(&subtype, response_data->body->subtype);
		pj_cstr(&body_text, response_data->body->body_text);

		*p_body = pjsip_msg_body_create(pool, &type, &subtype, &body_text);
	}
}

static int response_data_changed(struct ast_sip_subscription_response_data *response_data)
{
	if (response_data->status_code != 200 ||
			!ast_strlen_zero(response_data->status_text) ||
			response_data->headers ||
			response_data->body) {
		return 1;
	}
	return 0;
}

static void pubsub_on_rx_refresh(pjsip_evsub *evsub, pjsip_rx_data *rdata,
		int *p_st_code, pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, sub_module.id);
	struct ast_sip_subscription_response_data response_data = {
		.status_code = 200,
	};

	if (!sub) {
		return;
	}

	sub->handler->resubscribe(sub, rdata, &response_data);

	if (!response_data_changed(&response_data)) {
		return;
	}

	set_parameters_from_response_data(rdata->tp_info.pool, p_st_code, p_st_text,
			res_hdr, p_body, &response_data);
}

static void pubsub_on_rx_notify(pjsip_evsub *evsub, pjsip_rx_data *rdata, int *p_st_code,
		pj_str_t **p_st_text, pjsip_hdr *res_hdr, pjsip_msg_body **p_body)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, sub_module.id);
	struct ast_sip_subscription_response_data response_data = {
		.status_code = 200,
	};

	if (!sub || !sub->handler->notify_request) {
		return;
	}

	sub->handler->notify_request(sub, rdata, &response_data);

	if (!response_data_changed(&response_data)) {
		return;
	}

	set_parameters_from_response_data(rdata->tp_info.pool, p_st_code, p_st_text,
			res_hdr, p_body, &response_data);
}

static int serialized_pubsub_on_client_refresh(void *userdata)
{
	struct ast_sip_subscription *sub = userdata;

	sub->handler->refresh_subscription(sub);
	ao2_cleanup(sub);
	return 0;
}

static void pubsub_on_client_refresh(pjsip_evsub *evsub)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, sub_module.id);

	ao2_ref(sub, +1);
	ast_sip_push_task(sub->serializer, serialized_pubsub_on_client_refresh, sub);
}

static int serialized_pubsub_on_server_timeout(void *userdata)
{
	struct ast_sip_subscription *sub = userdata;

	sub->handler->subscription_timeout(sub);
	ao2_cleanup(sub);
	return 0;
}

static void pubsub_on_server_timeout(pjsip_evsub *evsub)
{
	struct ast_sip_subscription *sub = pjsip_evsub_get_mod_data(evsub, sub_module.id);

	ao2_ref(sub, +1);
	ast_sip_push_task(sub->serializer, serialized_pubsub_on_server_timeout, sub);
}

static int load_module(void)
{
	pjsip_evsub_init_module(ast_sip_get_pjsip_endpoint());
	if (ast_sip_register_service(&sub_module)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "SIP event resource",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
