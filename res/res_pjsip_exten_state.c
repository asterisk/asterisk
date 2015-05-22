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
#include "asterisk/taskprocessor.h"

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
	/*! The serializer to use for notifications */
	struct ast_taskprocessor *serializer;
	/*! Context in which subscription looks for updates */
	char context[AST_MAX_CONTEXT];
	/*! Extension within the context to receive updates from */
	char exten[AST_MAX_EXTENSION];
	/*! The subscription's user agent */
	char *user_agent;
	/*! The last known extension state */
	enum ast_extension_states last_exten_state;
	/*! The last known presence state */
	enum ast_presence_state last_presence_state;
};

#define DEFAULT_PRESENCE_BODY "application/pidf+xml"
#define DEFAULT_DIALOG_BODY "application/dialog-info+xml"

static void subscription_shutdown(struct ast_sip_subscription *sub);
static int new_subscribe(struct ast_sip_endpoint *endpoint, const char *resource);
static int subscription_established(struct ast_sip_subscription *sub);
static void *get_notify_data(struct ast_sip_subscription *sub);
static void to_ami(struct ast_sip_subscription *sub,
		   struct ast_str **buf);

struct ast_sip_notifier presence_notifier = {
	.default_accept = DEFAULT_PRESENCE_BODY,
	.new_subscribe = new_subscribe,
	.subscription_established = subscription_established,
	.get_notify_data = get_notify_data,
};

struct ast_sip_notifier dialog_notifier = {
	.default_accept = DEFAULT_DIALOG_BODY,
	.new_subscribe = new_subscribe,
	.subscription_established = subscription_established,
	.get_notify_data = get_notify_data,
};

struct ast_sip_subscription_handler presence_handler = {
	.event_name = "presence",
	.body_type = AST_SIP_EXTEN_STATE_DATA,
	.accept = { DEFAULT_PRESENCE_BODY, },
	.subscription_shutdown = subscription_shutdown,
	.to_ami = to_ami,
	.notifier = &presence_notifier,
};

struct ast_sip_subscription_handler dialog_handler = {
	.event_name = "dialog",
	.body_type = AST_SIP_EXTEN_STATE_DATA,
	.accept = { DEFAULT_DIALOG_BODY, },
	.subscription_shutdown = subscription_shutdown,
	.to_ami = to_ami,
	.notifier = &dialog_notifier,
};

static void exten_state_subscription_destructor(void *obj)
{
	struct exten_state_subscription *sub = obj;

	ast_free(sub->user_agent);
	ao2_cleanup(sub->sip_sub);
	ast_taskprocessor_unreference(sub->serializer);
}

static char *get_user_agent(const struct ast_sip_subscription *sip_sub)
{
	size_t size;
	char *user_agent = NULL;
	pjsip_user_agent_hdr *user_agent_hdr = ast_sip_subscription_get_header(
			sip_sub, "User-Agent");

	if (!user_agent_hdr) {
		return NULL;
	}

	size = pj_strlen(&user_agent_hdr->hvalue) + 1;
	user_agent = ast_malloc(size);
	ast_copy_pj_str(user_agent, &user_agent_hdr->hvalue, size);
	return ast_str_to_lower(user_agent);
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
		struct ast_sip_subscription *sip_sub, struct ast_sip_endpoint *endpoint)
{
	struct exten_state_subscription * exten_state_sub;

	exten_state_sub = ao2_alloc(sizeof(*exten_state_sub), exten_state_subscription_destructor);
	if (!exten_state_sub) {
		return NULL;
	}

	exten_state_sub->sip_sub = ao2_bump(sip_sub);

	/* We keep our own reference to the serializer as there is no guarantee in state_changed
	 * that the subscription tree is still valid when it is called. This can occur when
	 * the subscription is terminated at around the same time as the state_changed
	 * callback is invoked.
	 */
	exten_state_sub->serializer = ao2_bump(ast_sip_subscription_get_serializer(sip_sub));
	exten_state_sub->last_exten_state = INITIAL_LAST_EXTEN_STATE;
	exten_state_sub->last_presence_state = AST_PRESENCE_NOT_SET;
	exten_state_sub->user_agent = get_user_agent(sip_sub);
	return exten_state_sub;
}

struct notify_task_data {
	struct ast_sip_exten_state_data exten_state_data;
	struct exten_state_subscription *exten_state_sub;
	int terminate;
};

static void notify_task_data_destructor(void *obj)
{
	struct notify_task_data *task_data = obj;

	ao2_ref(task_data->exten_state_sub, -1);
	ao2_cleanup(task_data->exten_state_data.device_state_info);
	ast_free(task_data->exten_state_data.presence_subtype);
	ast_free(task_data->exten_state_data.presence_message);
	ast_free(task_data->exten_state_data.user_agent);
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

	task_data->exten_state_sub = exten_state_sub;
	task_data->exten_state_sub->last_exten_state = info->exten_state;
	task_data->exten_state_sub->last_presence_state = info->presence_state;
	ao2_ref(task_data->exten_state_sub, +1);

	task_data->exten_state_data.exten = exten_state_sub->exten;
	task_data->exten_state_data.exten_state = info->exten_state;
	task_data->exten_state_data.presence_state = info->presence_state;
	task_data->exten_state_data.presence_subtype = ast_strdup(info->presence_subtype);
	task_data->exten_state_data.presence_message = ast_strdup(info->presence_message);
	task_data->exten_state_data.user_agent = ast_strdup(exten_state_sub->user_agent);
	task_data->exten_state_data.device_state_info = ao2_bump(info->device_state_info);
	task_data->exten_state_data.sub = exten_state_sub->sip_sub;

	if ((info->exten_state == AST_EXTENSION_DEACTIVATED) ||
	    (info->exten_state == AST_EXTENSION_REMOVED)) {
		ast_verb(2, "Watcher for hint %s %s\n", exten, info->exten_state
			 == AST_EXTENSION_REMOVED ? "removed" : "deactivated");
		task_data->terminate = 1;
	}

	return task_data;
}

static int notify_task(void *obj)
{
	RAII_VAR(struct notify_task_data *, task_data, obj, ao2_cleanup);
	struct ast_sip_body_data data = {
		.body_type = AST_SIP_EXTEN_STATE_DATA,
		.body_data = &task_data->exten_state_data,
	};

	/* Terminated subscriptions are no longer associated with a valid tree, and sending
	 * NOTIFY messages on a subscription which has already been terminated won't work.
	 */
	if (ast_sip_subscription_is_terminated(task_data->exten_state_sub->sip_sub)) {
		return 0;
	}

	/* All access to the subscription must occur within a task executed within its serializer */
	ast_sip_subscription_get_local_uri(task_data->exten_state_sub->sip_sub,
			task_data->exten_state_data.local, sizeof(task_data->exten_state_data.local));
	ast_sip_subscription_get_remote_uri(task_data->exten_state_sub->sip_sub,
			task_data->exten_state_data.remote, sizeof(task_data->exten_state_data.remote));

	/* Pool allocation has to happen here so that we allocate within a PJLIB thread */
	task_data->exten_state_data.pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
			"exten_state", 1024, 1024);
	if (!task_data->exten_state_data.pool) {
		return -1;
	}

	task_data->exten_state_data.sub = task_data->exten_state_sub->sip_sub;

	ast_sip_subscription_notify(task_data->exten_state_sub->sip_sub, &data,
			task_data->terminate);

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

	if (!(task_data = alloc_notify_task_data(exten, exten_state_sub, info))) {
		return -1;
	}

	/* safe to push this async since we copy the data from info and
	   add a ref for the device state info */
	if (ast_sip_push_task(task_data->exten_state_sub->serializer, notify_task,
		task_data)) {
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

static int new_subscribe(struct ast_sip_endpoint *endpoint,
		const char *resource)
{
	if (!ast_exists_extension(NULL, endpoint->context, resource, PRIORITY_HINT, NULL)) {
		ast_log(LOG_NOTICE, "Extension state subscription failed: Extension %s does not exist in context '%s' or has no associated hint\n",
			resource, endpoint->context);
		return 404;
	}

	return 200;
}

static int subscription_established(struct ast_sip_subscription *sip_sub)
{
	struct ast_sip_endpoint *endpoint = ast_sip_subscription_get_endpoint(sip_sub);
	const char *resource = ast_sip_subscription_get_resource_name(sip_sub);
	struct exten_state_subscription *exten_state_sub;

	if (!(exten_state_sub = exten_state_subscription_alloc(sip_sub, endpoint))) {
		ao2_cleanup(endpoint);
		return -1;
	}

	ast_copy_string(exten_state_sub->context, endpoint->context, sizeof(exten_state_sub->context));
	ast_copy_string(exten_state_sub->exten, resource, sizeof(exten_state_sub->exten));

	if ((exten_state_sub->id = ast_extension_state_add_destroy_extended(
		     exten_state_sub->context, exten_state_sub->exten,
		     state_changed, state_changed_destroy, exten_state_sub)) < 0) {
		ast_log(LOG_WARNING, "Unable to subscribe endpoint '%s' to extension '%s@%s'\n",
			ast_sorcery_object_get_id(endpoint), exten_state_sub->exten,
			exten_state_sub->context);
		ao2_cleanup(endpoint);
		ao2_cleanup(exten_state_sub);
		return -1;
	}

	/* Go ahead and cleanup the endpoint since we don't need it anymore */
	ao2_cleanup(endpoint);

	/* bump the ref since ast_extension_state_add holds a reference */
	ao2_ref(exten_state_sub, +1);

	if (add_datastore(exten_state_sub)) {
		ast_log(LOG_WARNING, "Unable to add to subscription datastore.\n");
		ao2_cleanup(exten_state_sub);
		return -1;
	}

	ao2_cleanup(exten_state_sub);
	return 0;
}

static void exten_state_data_destructor(void *obj)
{
	struct ast_sip_exten_state_data *exten_state_data = obj;

	ao2_cleanup(exten_state_data->device_state_info);
	ast_free(exten_state_data->presence_subtype);
	ast_free(exten_state_data->presence_message);
	if (exten_state_data->pool) {
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), exten_state_data->pool);
	}
}

static struct ast_sip_exten_state_data *exten_state_data_alloc(struct ast_sip_subscription *sip_sub,
		struct exten_state_subscription *exten_state_sub)
{
	struct ast_sip_exten_state_data *exten_state_data;
	char *subtype = NULL;
	char *message = NULL;
	int presence_state;

	exten_state_data = ao2_alloc(sizeof(*exten_state_data), exten_state_data_destructor);
	if (!exten_state_data) {
		return NULL;
	}

	exten_state_data->exten = exten_state_sub->exten;
	presence_state = ast_hint_presence_state(NULL, exten_state_sub->context, exten_state_sub->exten, &subtype, &message);
	if (presence_state  == -1 || presence_state == AST_PRESENCE_INVALID) {
		ao2_cleanup(exten_state_data);
		return NULL;
	}
	exten_state_data->presence_state = presence_state;
	exten_state_data->presence_subtype = subtype;
	exten_state_data->presence_message = message;
	exten_state_data->user_agent = exten_state_sub->user_agent;
	ast_sip_subscription_get_local_uri(sip_sub, exten_state_data->local,
			sizeof(exten_state_data->local));
	ast_sip_subscription_get_remote_uri(sip_sub, exten_state_data->remote,
			sizeof(exten_state_data->remote));
	exten_state_data->sub = sip_sub;

	exten_state_data->exten_state = ast_extension_state_extended(
			NULL, exten_state_sub->context, exten_state_sub->exten,
			&exten_state_data->device_state_info);
	if (exten_state_data->exten_state < 0) {
		ao2_cleanup(exten_state_data);
		return NULL;
	}

	exten_state_data->pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
			"exten_state", 1024, 1024);
	if (!exten_state_data->pool) {
		ao2_cleanup(exten_state_data);
		return NULL;
	}

	return exten_state_data;
}

static void *get_notify_data(struct ast_sip_subscription *sub)
{
	struct exten_state_subscription *exten_state_sub;

	exten_state_sub = get_exten_state_sub(sub);
	if (!exten_state_sub) {
		return NULL;
	}

	return exten_state_data_alloc(sub, exten_state_sub);
}

static void to_ami(struct ast_sip_subscription *sub,
		   struct ast_str **buf)
{
	struct exten_state_subscription *exten_state_sub =
		get_exten_state_sub(sub);

	if (!exten_state_sub) {
		return;
	}

	ast_str_append(buf, 0, "SubscriptionType: extension_state\r\n"
		       "Extension: %s\r\nExtensionStates: %s\r\n",
		       exten_state_sub->exten, ast_extension_state2str(
			       exten_state_sub->last_exten_state));
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	if (ast_sip_register_subscription_handler(&presence_handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n",
			presence_handler.event_name);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_register_subscription_handler(&dialog_handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n",
			dialog_handler.event_name);
		ast_sip_unregister_subscription_handler(&presence_handler);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_subscription_handler(&dialog_handler);
	ast_sip_unregister_subscription_handler(&presence_handler);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State Notifications",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
