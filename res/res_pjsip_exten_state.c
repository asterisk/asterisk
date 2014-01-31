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
	<depend>res_pjsip</depend>
	<depend>res_pjsip_pubsub</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_body_generator_types.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "asterisk/app.h"

#define BODY_SIZE 1024
#define EVENT_TYPE_SIZE 50

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
	/*! Context in which subscription looks for updates */
	char context[AST_MAX_CONTEXT];
	/*! Extension within the context to receive updates from */
	char exten[AST_MAX_EXTENSION];
	/*! The last known extension state */
	enum ast_extension_states last_exten_state;
};

#define DEFAULT_PRESENCE_BODY "application/pidf+xml"

static void subscription_shutdown(struct ast_sip_subscription *sub);
static struct ast_sip_subscription *new_subscribe(struct ast_sip_endpoint *endpoint,
						  pjsip_rx_data *rdata);
static void resubscribe(struct ast_sip_subscription *sub, pjsip_rx_data *rdata,
			struct ast_sip_subscription_response_data *response_data);
static void subscription_timeout(struct ast_sip_subscription *sub);
static void subscription_terminated(struct ast_sip_subscription *sub,
				    pjsip_rx_data *rdata);
static void to_ami(struct ast_sip_subscription *sub,
		   struct ast_str **buf);

struct ast_sip_subscription_handler presence_handler = {
	.event_name = "presence",
	.accept = { DEFAULT_PRESENCE_BODY, },
	.default_accept = DEFAULT_PRESENCE_BODY,
	.subscription_shutdown = subscription_shutdown,
	.new_subscribe = new_subscribe,
	.resubscribe = resubscribe,
	.subscription_timeout = subscription_timeout,
	.subscription_terminated = subscription_terminated,
	.to_ami = to_ami,
};

static void exten_state_subscription_destructor(void *obj)
{
	struct exten_state_subscription *sub = obj;

	ao2_cleanup(sub->sip_sub);
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
	RAII_VAR(struct exten_state_subscription *, exten_state_sub,
		 ao2_alloc(sizeof(*exten_state_sub), exten_state_subscription_destructor), ao2_cleanup);

	if (!exten_state_sub) {
		return NULL;
	}

	if (!(exten_state_sub->sip_sub = ast_sip_create_subscription(
		      &presence_handler, role, endpoint, rdata))) {
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
	struct ast_sip_body body;

	body.type = ast_sip_subscription_get_body_type(exten_state_sub->sip_sub);
	body.subtype = ast_sip_subscription_get_body_subtype(exten_state_sub->sip_sub);

	if (ast_sip_pubsub_generate_body_content(body.type, body.subtype,
				exten_state_data, &body_text)) {
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
	pjsip_dialog *dlg;
	struct ast_sip_exten_state_data exten_state_data = {
		.exten = exten_state_sub->exten,
		.presence_state = ast_hint_presence_state(NULL, exten_state_sub->context,
							  exten_state_sub->exten, &subtype, &message),
	};

	dlg = ast_sip_subscription_get_dlg(exten_state_sub->sip_sub);
	ast_copy_pj_str(exten_state_data.local, &dlg->local.info_str,
			sizeof(exten_state_data.local));
	ast_copy_pj_str(exten_state_data.remote, &dlg->remote.info_str,
			sizeof(exten_state_data.remote));

	if ((exten_state_data.exten_state = ast_extension_state_extended(
		     NULL, exten_state_sub->context, exten_state_sub->exten, &info)) < 0) {

		ast_log(LOG_WARNING, "Unable to get device hint/info for extension %s\n",
			exten_state_sub->exten);
		return;
	}

	exten_state_data.pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
			"exten_state", 1024, 1024);

	exten_state_data.device_state_info = info;
	create_send_notify(exten_state_sub, reason, evsub_state, &exten_state_data);
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), exten_state_data.pool);
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
	struct pjsip_dialog *dlg;

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

	dlg = ast_sip_subscription_get_dlg(exten_state_sub->sip_sub);
	ast_copy_pj_str(task_data->exten_state_data.local, &dlg->local.info_str,
			sizeof(task_data->exten_state_data.local));
	ast_copy_pj_str(task_data->exten_state_data.remote, &dlg->remote.info_str,
			sizeof(task_data->exten_state_data.remote));

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

	/* Pool allocation has to happen here so that we allocate within a PJLIB thread */
	task_data->exten_state_data.pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
			"exten_state", 1024, 1024);

	create_send_notify(task_data->exten_state_sub, task_data->evsub_state ==
			   PJSIP_EVSUB_STATE_TERMINATED ? "noresource" : NULL,
			   task_data->evsub_state, &task_data->exten_state_data);

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(),
			task_data->exten_state_data.pool);
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

static void to_ami(struct ast_sip_subscription *sub,
		   struct ast_str **buf)
{
	struct exten_state_subscription *exten_state_sub =
		get_exten_state_sub(sub);

	ast_str_append(buf, 0, "SubscriptionType: extension_state\r\n"
		       "Extension: %s\r\nExtensionStates: %s\r\n",
		       exten_state_sub->exten, ast_extension_state2str(
			       exten_state_sub->last_exten_state));
}

static int load_module(void)
{
	if (ast_sip_register_subscription_handler(&presence_handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n",
			presence_handler.event_name);
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_subscription_handler(&presence_handler);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State Notifications",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
