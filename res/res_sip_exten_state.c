/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
	<depend>res_sip</depend>
	<depend>res_sip_pubsub</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_sip.h"
#include "asterisk/res_sip_pubsub.h"
#include "asterisk/res_sip_exten_state.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "asterisk/app.h"

#define BODY_SIZE 1024
#define EVENT_TYPE_SIZE 50

AST_RWLIST_HEAD_STATIC(providers, ast_sip_exten_state_provider);

/*!
 * \internal
 * \brief Find a provider based on the given accept body type.
 */
static struct ast_sip_exten_state_provider *provider_by_type(const char *type)
{
	struct ast_sip_exten_state_provider *i;
	SCOPED_LOCK(lock, &providers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&providers, i, next) {
		if (!strcmp(i->body_type, type)) {
			return i;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	return NULL;
}

/*!
 * \internal
 * \brief Find a provider based on the given accept body types.
 */
static struct ast_sip_exten_state_provider *provider_by_types(const char *event_name,
							      char **types, int count)
{
	int i;
	struct ast_sip_exten_state_provider *res;
	for (i = 0; i < count; ++i) {
		if ((res = provider_by_type(types[i])) &&
		    !strcmp(event_name, res->event_name)) {
			return res;
		}
	}
	return NULL;
}

/*!
 * \brief A subscription for extension state
 *
 * This structure acts as the owner for the underlying SIP subscription. It
 * also keeps a pointer to an associated "provider" so when a state changes
 * a notify data creator is quickly accessible.
 */
struct exten_state_subscription {
	/*! Watcher id when registering for extension state changes */
	int id;
	/*! The SIP subscription */
	struct ast_sip_subscription *sip_sub;
	/*! The name of the event the subscribed to */
	char event_name[EVENT_TYPE_SIZE];
	/*! The number of body types */
	int body_types_count;
	/*! The subscription body types */
	char **body_types;
	/*! Context in which subscription looks for updates */
	char context[AST_MAX_CONTEXT];
	/*! Extension within the context to receive updates from */
	char exten[AST_MAX_EXTENSION];
	/*! The last known extension state */
	enum ast_extension_states last_exten_state;
};

static void exten_state_subscription_destructor(void *obj)
{
	struct exten_state_subscription *sub = obj;
	int i;

	for (i = 0; i < sub->body_types_count; ++i) {
		ast_free(sub->body_types[i]);
	}

	ast_free(sub->body_types);
	ao2_cleanup(sub->sip_sub);
}

/*!
 * \internal
 * \brief Copies the body types the message wishes to subscribe to.
 */
static void copy_body_types(pjsip_rx_data *rdata,
			    struct exten_state_subscription *exten_state_sub)
{
	int i;
	pjsip_accept_hdr *hdr = (pjsip_accept_hdr*)
		pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_ACCEPT, NULL);

	exten_state_sub->body_types_count = hdr->count;
	exten_state_sub->body_types = ast_malloc(hdr->count * sizeof(char*));

	for (i = 0; i < hdr->count; ++i) {
		exten_state_sub->body_types[i] =
			ast_malloc(hdr->values[i].slen * sizeof(char*) + 1);

		ast_copy_string(exten_state_sub->body_types[i],
				pj_strbuf(&hdr->values[i]), hdr->values[i].slen + 1);
	}
}

/*!
 * \internal
 * \brief Initialize the last extension state to something outside
 * its usual states.
 */
#define INITIAL_LAST_EXTEN_STATE -3

/*!
 * \internal
 * \brief Allocates an exten_state_subscription object.
 *
 * Creates the underlying SIP subscription for the given request. First makes
 * sure that there are registered handler and provider objects available.
 */
static struct exten_state_subscription *exten_state_subscription_alloc(
	struct ast_sip_endpoint *endpoint, enum ast_sip_subscription_role role, pjsip_rx_data *rdata)
{
	static const pj_str_t event_name = { "Event", 5 };
	pjsip_event_hdr *hdr = (pjsip_event_hdr*)pjsip_msg_find_hdr_by_name(
		rdata->msg_info.msg, &event_name, NULL);

	struct ast_sip_exten_state_provider *provider;
	RAII_VAR(struct exten_state_subscription *, exten_state_sub,
		 ao2_alloc(sizeof(*exten_state_sub), exten_state_subscription_destructor), ao2_cleanup);

	if (!exten_state_sub) {
		return NULL;
	}

	ast_copy_pj_str(exten_state_sub->event_name, &hdr->event_type,
			sizeof(exten_state_sub->event_name));

	copy_body_types(rdata, exten_state_sub);
	if (!(provider = provider_by_types(exten_state_sub->event_name,
					   exten_state_sub->body_types,
					   exten_state_sub->body_types_count))) {
		ast_log(LOG_WARNING, "Unable to locate subscription handler\n");
		return NULL;
	}

	if (!(exten_state_sub->sip_sub = ast_sip_create_subscription(
		      provider->handler, role, endpoint, rdata))) {
		ast_log(LOG_WARNING, "Unable to create SIP subscription for endpoint %s\n",
			ast_sorcery_object_get_id(endpoint));
		return NULL;
	}

	exten_state_sub->last_exten_state = INITIAL_LAST_EXTEN_STATE;

	ao2_ref(exten_state_sub, +1);
	return exten_state_sub;
}

/*!
 * \internal
 * \brief Create and send a NOTIFY request to the subscriber.
 */
static void create_send_notify(struct exten_state_subscription *exten_state_sub, const char *reason,
			       pjsip_evsub_state evsub_state, struct ast_sip_exten_state_data *exten_state_data)
{
	RAII_VAR(struct ast_str *, body_text, ast_str_create(BODY_SIZE), ast_free_ptr);
	pj_str_t reason_str;
	const pj_str_t *reason_str_ptr = NULL;
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg;
	char local[PJSIP_MAX_URL_SIZE], remote[PJSIP_MAX_URL_SIZE];
	struct ast_sip_body body;

	struct ast_sip_exten_state_provider *provider = provider_by_types(
		exten_state_sub->event_name, exten_state_sub->body_types,
		exten_state_sub->body_types_count);

	if (!provider) {
		ast_log(LOG_ERROR, "Unable to locate provider for subscription\n");
		return;
	}

	body.type = provider->type;
	body.subtype = provider->subtype;

	dlg = ast_sip_subscription_get_dlg(exten_state_sub->sip_sub);
	ast_copy_pj_str(local, &dlg->local.info_str, sizeof(local));
	ast_copy_pj_str(remote, &dlg->remote.info_str, sizeof(remote));

	if (provider->create_body(exten_state_data, local, remote, &body_text)) {
		ast_log(LOG_ERROR, "Unable to create body on NOTIFY request\n");
		return;
	}

	body.body_text = ast_str_buffer(body_text);

	if (reason) {
		pj_cstr(&reason_str, reason);
		reason_str_ptr = &reason_str;
	}

	if (pjsip_evsub_notify(ast_sip_subscription_get_evsub(exten_state_sub->sip_sub),
			      evsub_state, NULL, reason_str_ptr, &tdata) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to create NOTIFY request\n");
		return;
	}

	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_WARNING, "Unable to add body to NOTIFY request\n");
		pjsip_tx_data_dec_ref(tdata);
		return;
	}

	if (ast_sip_subscription_send_request(exten_state_sub->sip_sub, tdata) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to send NOTIFY request\n");
		pjsip_tx_data_dec_ref(tdata);
	}
}

/*!
 * \internal
 * \brief Get device state information and send notification to the subscriber.
 */
static void send_notify(struct exten_state_subscription *exten_state_sub, const char *reason,
	pjsip_evsub_state evsub_state)
{
	RAII_VAR(struct ao2_container*, info, NULL, ao2_cleanup);
	char *subtype = NULL, *message = NULL;

	struct ast_sip_exten_state_data exten_state_data = {
		.exten = exten_state_sub->exten,
		.presence_state = ast_hint_presence_state(NULL, exten_state_sub->context,
							  exten_state_sub->exten, &subtype, &message),
	};

	if ((exten_state_data.exten_state = ast_extension_state_extended(
		     NULL, exten_state_sub->context, exten_state_sub->exten, &info)) < 0) {

		ast_log(LOG_WARNING, "Unable to get device hint/info for extension %s\n",
			exten_state_sub->exten);
		return;
	}

	exten_state_data.device_state_info = info;
	create_send_notify(exten_state_sub, reason, evsub_state, &exten_state_data);
}

struct notify_task_data {
	struct ast_sip_exten_state_data exten_state_data;
	struct exten_state_subscription *exten_state_sub;
	pjsip_evsub_state evsub_state;
};

static void notify_task_data_destructor(void *obj)
{
	struct notify_task_data *task_data = obj;

	ao2_ref(task_data->exten_state_sub, -1);
	ao2_cleanup(task_data->exten_state_data.device_state_info);
}

static struct notify_task_data *alloc_notify_task_data(char *exten, struct exten_state_subscription *exten_state_sub,
						       struct ast_state_cb_info *info)
{
	struct notify_task_data *task_data =
		ao2_alloc(sizeof(*task_data), notify_task_data_destructor);

	if (!task_data) {
		ast_log(LOG_WARNING, "Unable to create notify task data\n");
		return NULL;
	}

	task_data->evsub_state = PJSIP_EVSUB_STATE_ACTIVE;
	task_data->exten_state_sub = exten_state_sub;
	task_data->exten_state_sub->last_exten_state = info->exten_state;
	ao2_ref(task_data->exten_state_sub, +1);

	task_data->exten_state_data.exten = exten_state_sub->exten;
	task_data->exten_state_data.exten_state = info->exten_state;
	task_data->exten_state_data.presence_state = info->presence_state;
	task_data->exten_state_data.device_state_info = info->device_state_info;

	if (task_data->exten_state_data.device_state_info) {
		ao2_ref(task_data->exten_state_data.device_state_info, +1);
	}

	if ((info->exten_state == AST_EXTENSION_DEACTIVATED) ||
	    (info->exten_state == AST_EXTENSION_REMOVED)) {
		task_data->evsub_state = PJSIP_EVSUB_STATE_TERMINATED;
		ast_log(LOG_WARNING, "Watcher for hint %s %s\n", exten, info->exten_state
			 == AST_EXTENSION_REMOVED ? "removed" : "deactivated");
	}

	return task_data;
}

static int notify_task(void *obj)
{
	RAII_VAR(struct notify_task_data *, task_data, obj, ao2_cleanup);

	create_send_notify(task_data->exten_state_sub, task_data->evsub_state ==
			   PJSIP_EVSUB_STATE_TERMINATED ? "noresource" : NULL,
			   task_data->evsub_state, &task_data->exten_state_data);
	return 0;
}

/*!
 * \internal
 * \brief Callback for exten/device state changes.
 *
 * Upon state change, send the appropriate notification to the subscriber.
 */
static int state_changed(char *context, char *exten,
			 struct ast_state_cb_info *info, void *data)
{
	struct notify_task_data *task_data;
	struct exten_state_subscription *exten_state_sub = data;

	if (exten_state_sub->last_exten_state == info->exten_state) {
		return 0;
	}

	if (!(task_data = alloc_notify_task_data(exten, exten_state_sub, info))) {
		return -1;
	}

	/* safe to push this async since we copy the data from info and
	   add a ref for the device state info */
	if (ast_sip_push_task(ast_sip_subscription_get_serializer(task_data->exten_state_sub->sip_sub),
			      notify_task, task_data)) {
		ao2_cleanup(task_data);
		return -1;
	}
	return 0;
}

static void state_changed_destroy(int id, void *data)
{
	struct exten_state_subscription *exten_state_sub = data;
	ao2_cleanup(exten_state_sub);
}

static struct ast_datastore_info ds_info = { };
static const char ds_name[] = "exten state datastore";

/*!
 * \internal
 * \brief Add a datastore for exten exten_state_subscription.
 *
 * Adds the exten_state_subscription wrapper object to a datastore so it can be retrieved
 * later based upon its association with the ast_sip_subscription.
 */
static int add_datastore(struct exten_state_subscription *exten_state_sub)
{
	RAII_VAR(struct ast_datastore *, datastore,
		 ast_sip_subscription_alloc_datastore(&ds_info, ds_name), ao2_cleanup);

	if (!datastore) {
		return -1;
	}

	datastore->data = exten_state_sub;
	ast_sip_subscription_add_datastore(exten_state_sub->sip_sub, datastore);
	ao2_ref(exten_state_sub, +1);
	return 0;
}

/*!
 * \internal
 * \brief Get the exten_state_subscription object associated with the given
 * ast_sip_subscription in the datastore.
 */
static struct exten_state_subscription *get_exten_state_sub(
	struct ast_sip_subscription *sub)
{
	RAII_VAR(struct ast_datastore *, datastore,
		 ast_sip_subscription_get_datastore(sub, ds_name), ao2_cleanup);

	return datastore ? datastore->data : NULL;
}

static void subscription_shutdown(struct ast_sip_subscription *sub)
{
	struct exten_state_subscription *exten_state_sub = get_exten_state_sub(sub);

	if (!exten_state_sub) {
		return;
	}

	ast_extension_state_del(exten_state_sub->id, state_changed);
	ast_sip_subscription_remove_datastore(exten_state_sub->sip_sub, ds_name);
	/* remove data store reference */
	ao2_cleanup(exten_state_sub);
}

static struct ast_sip_subscription *new_subscribe(struct ast_sip_endpoint *endpoint,
						  pjsip_rx_data *rdata)
{
	pjsip_uri *uri = rdata->msg_info.msg->line.req.uri;
	pjsip_sip_uri *sip_uri = pjsip_uri_get_uri(uri);
	RAII_VAR(struct exten_state_subscription *, exten_state_sub, NULL, ao2_cleanup);

	if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri)) {
		ast_log(LOG_WARNING, "Attempt to SUBSCRIBE to a non-SIP URI\n");
		return NULL;
	}

	if (!(exten_state_sub = exten_state_subscription_alloc(endpoint, AST_SIP_NOTIFIER, rdata))) {
		return NULL;
	}

	ast_copy_string(exten_state_sub->context, endpoint->context, sizeof(exten_state_sub->context));
	ast_copy_pj_str(exten_state_sub->exten, &sip_uri->user, sizeof(exten_state_sub->exten));

	if ((exten_state_sub->id = ast_extension_state_add_destroy_extended(
		     exten_state_sub->context, exten_state_sub->exten,
		     state_changed, state_changed_destroy, exten_state_sub)) < 0) {
		ast_log(LOG_WARNING, "Unable to subscribe endpoint '%s' to extension '%s@%s'\n",
			ast_sorcery_object_get_id(endpoint), exten_state_sub->exten,
			exten_state_sub->context);
		pjsip_evsub_terminate(ast_sip_subscription_get_evsub(exten_state_sub->sip_sub), PJ_FALSE);
		return NULL;
	}

	/* bump the ref since ast_extension_state_add holds a reference */
	ao2_ref(exten_state_sub, +1);

	if (add_datastore(exten_state_sub)) {
		ast_log(LOG_WARNING, "Unable to add to subscription datastore.\n");
		pjsip_evsub_terminate(ast_sip_subscription_get_evsub(exten_state_sub->sip_sub), PJ_FALSE);
		return NULL;
	}

	if (pjsip_evsub_accept(ast_sip_subscription_get_evsub(exten_state_sub->sip_sub),
			       rdata, 200, NULL) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to accept the incoming extension state subscription.\n");
		pjsip_evsub_terminate(ast_sip_subscription_get_evsub(exten_state_sub->sip_sub), PJ_FALSE);
		return NULL;
	}

	send_notify(exten_state_sub, NULL, PJSIP_EVSUB_STATE_ACTIVE);
	return exten_state_sub->sip_sub;
}

static void resubscribe(struct ast_sip_subscription *sub, pjsip_rx_data *rdata,
			struct ast_sip_subscription_response_data *response_data)
{
	struct exten_state_subscription *exten_state_sub = get_exten_state_sub(sub);

	if (!exten_state_sub) {
		return;
	}

	send_notify(exten_state_sub, NULL, PJSIP_EVSUB_STATE_ACTIVE);
}

static void subscription_timeout(struct ast_sip_subscription *sub)
{
	struct exten_state_subscription *exten_state_sub = get_exten_state_sub(sub);

	if (!exten_state_sub) {
		return;
	}

	ast_verbose(VERBOSE_PREFIX_3 "Subscription has timed out.\n");
	send_notify(exten_state_sub, "timeout", PJSIP_EVSUB_STATE_TERMINATED);
}

static void subscription_terminated(struct ast_sip_subscription *sub,
				    pjsip_rx_data *rdata)
{
	struct exten_state_subscription *exten_state_sub = get_exten_state_sub(sub);

	if (!exten_state_sub) {
		return;
	}

	ast_verbose(VERBOSE_PREFIX_3 "Subscription has been terminated.\n");
	send_notify(exten_state_sub, NULL, PJSIP_EVSUB_STATE_TERMINATED);
}

/*!
 * \internal
 * \brief Create and register a subscription handler.
 *
 * Creates a subscription handler that can be registered with the pub/sub
 * framework for the given event_name and accept value.
 */
static struct ast_sip_subscription_handler *create_and_register_handler(
	const char *event_name, const char *accept)
{
	struct ast_sip_subscription_handler *handler =
		ao2_alloc(sizeof(*handler), NULL);

	if (!handler) {
		return NULL;
	}

	handler->event_name = event_name;
	handler->accept[0] = accept;

	handler->subscription_shutdown = subscription_shutdown;
	handler->new_subscribe = new_subscribe;
	handler->resubscribe = resubscribe;
	handler->subscription_timeout = subscription_timeout;
	handler->subscription_terminated = subscription_terminated;

	if (ast_sip_register_subscription_handler(handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n",
			handler->event_name);
		ao2_cleanup(handler);
		return NULL;
	}

	return handler;
}

int ast_sip_register_exten_state_provider(struct ast_sip_exten_state_provider *obj)
{
	if (ast_strlen_zero(obj->type)) {
		ast_log(LOG_WARNING, "Type not specified on provider for event %s\n",
			obj->event_name);
		return -1;
	}

	if (ast_strlen_zero(obj->subtype)) {
		ast_log(LOG_WARNING, "Subtype not specified on provider for event %s\n",
			obj->event_name);
		return -1;
	}

	if (!obj->create_body) {
		ast_log(LOG_WARNING, "Body handler not specified on provide for event %s\n",
		    obj->event_name);
		return -1;
	}

	if (!(obj->handler = create_and_register_handler(obj->event_name, obj->body_type))) {
		ast_log(LOG_WARNING, "Handler could not be registered for provider event %s\n",
		    obj->event_name);
		return -1;
	}

	/* scope to avoid mix declarations */
	{
		SCOPED_LOCK(lock, &providers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
		AST_RWLIST_INSERT_TAIL(&providers, obj, next);
		ast_module_ref(ast_module_info->self);
	}

	return 0;
}

void ast_sip_unregister_exten_state_provider(struct ast_sip_exten_state_provider *obj)
{
	struct ast_sip_exten_state_provider *i;
	SCOPED_LOCK(lock, &providers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&providers, i, next) {
		if (i == obj) {
			ast_sip_unregister_subscription_handler(i->handler);
			ao2_cleanup(i->handler);
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static int load_module(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "SIP Extension State Notifications",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
