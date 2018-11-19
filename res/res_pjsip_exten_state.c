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
	<depend>res_pjsip_outbound_publish</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <regex.h>

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_outbound_publish.h"
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
 * \brief The number of buckets to use for storing publishers
 */
#define PUBLISHER_BUCKETS 31

/*!
 * \brief Container of active outbound extension state publishers
 */
static struct ao2_container *publishers;

/*! Serializer for outbound extension state publishing. */
static struct ast_taskprocessor *publish_exten_state_serializer;

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

/*!
 * \brief An extension state publisher
 *
 */
struct exten_state_publisher {
	/*! Regular expression for context filtering */
	regex_t context_regex;
	/*! Regular expression for extension filtering */
	regex_t exten_regex;
	/*! Publish client to use for sending publish messages */
	struct ast_sip_outbound_publish_client *client;
	/*! Datastores container to hold persistent information */
	struct ao2_container *datastores;
	/*! Whether context filtering is active */
	unsigned int context_filter;
	/*! Whether extension filtering is active */
	unsigned int exten_filter;
	/*! The body type to use for this publisher - stored after the name */
	char *body_type;
	/*! The body subtype to use for this publisher - stored after the body type */
	char *body_subtype;
	/*! The name of this publisher */
	char name[0];
};

#define DEFAULT_PRESENCE_BODY "application/pidf+xml"
#define DEFAULT_DIALOG_BODY "application/dialog-info+xml"

static void subscription_shutdown(struct ast_sip_subscription *sub);
static int new_subscribe(struct ast_sip_endpoint *endpoint, const char *resource);
static int subscription_established(struct ast_sip_subscription *sub);
static void *get_notify_data(struct ast_sip_subscription *sub);
static void to_ami(struct ast_sip_subscription *sub,
		   struct ast_str **buf);
static int publisher_start(struct ast_sip_outbound_publish *configuration,
			   struct ast_sip_outbound_publish_client *client);
static int publisher_stop(struct ast_sip_outbound_publish_client *client);

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

struct ast_sip_event_publisher_handler presence_publisher = {
	.event_name = "presence",
	.start_publishing = publisher_start,
	.stop_publishing = publisher_stop,
};

struct ast_sip_subscription_handler dialog_handler = {
	.event_name = "dialog",
	.body_type = AST_SIP_EXTEN_STATE_DATA,
	.accept = { DEFAULT_DIALOG_BODY, },
	.subscription_shutdown = subscription_shutdown,
	.to_ami = to_ami,
	.notifier = &dialog_notifier,
};

struct ast_sip_event_publisher_handler dialog_publisher = {
	.event_name = "dialog",
	.start_publishing = publisher_start,
	.stop_publishing = publisher_stop,
};

static void exten_state_subscription_destructor(void *obj)
{
	struct exten_state_subscription *sub = obj;

	ast_free(sub->user_agent);
	ast_sip_subscription_destroy(sub->sip_sub);
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

	exten_state_sub->sip_sub = sip_sub;

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

static struct notify_task_data *alloc_notify_task_data(const char *exten,
	struct exten_state_subscription *exten_state_sub,
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
	task_data->exten_state_data.datastores = ast_sip_subscription_get_datastores(exten_state_sub->sip_sub);

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
	task_data->exten_state_data.datastores = ast_sip_subscription_get_datastores(task_data->exten_state_sub->sip_sub);

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
static int state_changed(const char *context, const char *exten,
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
	const char *context = S_OR(endpoint->subscription.context, endpoint->context);

	if (!ast_exists_extension(NULL, context, resource, PRIORITY_HINT, NULL)) {
		ast_log(LOG_NOTICE, "Endpoint '%s' state subscription failed: "
			"Extension '%s' does not exist in context '%s' or has no associated hint\n",
			ast_sorcery_object_get_id(endpoint), resource, context);
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

	ast_copy_string(exten_state_sub->context,
		S_OR(endpoint->subscription.context, endpoint->context),
		sizeof(exten_state_sub->context));
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
	exten_state_data->datastores = ast_sip_subscription_get_datastores(sip_sub);

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

struct exten_state_pub_data {
	/*! Publishers needing state update */
	AST_VECTOR(name, struct exten_state_publisher *) pubs;
	/*! Body generator state data */
	struct ast_sip_exten_state_data exten_state_data;
};

static void exten_state_pub_data_destroy(struct exten_state_pub_data *doomed)
{
	if (!doomed) {
		return;
	}

	ast_free((void *) doomed->exten_state_data.exten);
	ast_free(doomed->exten_state_data.presence_subtype);
	ast_free(doomed->exten_state_data.presence_message);
	ao2_cleanup(doomed->exten_state_data.device_state_info);

	AST_VECTOR_CALLBACK_VOID(&doomed->pubs, ao2_ref, -1);
	AST_VECTOR_FREE(&doomed->pubs);

	ast_free(doomed);
}

static struct exten_state_pub_data *exten_state_pub_data_alloc(const char *exten, struct ast_state_cb_info *info)
{
	struct exten_state_pub_data *pub_data;

	pub_data = ast_calloc(1, sizeof(*pub_data));
	if (!pub_data) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&pub_data->pubs, ao2_container_count(publishers))) {
		exten_state_pub_data_destroy(pub_data);
		return NULL;
	}

	/* Save off currently known information for the body generators. */
	pub_data->exten_state_data.exten = ast_strdup(exten);
	pub_data->exten_state_data.exten_state = info->exten_state;
	pub_data->exten_state_data.presence_state = info->presence_state;
	pub_data->exten_state_data.presence_subtype = ast_strdup(info->presence_subtype);
	pub_data->exten_state_data.presence_message = ast_strdup(info->presence_message);
	pub_data->exten_state_data.device_state_info = ao2_bump(info->device_state_info);
	if (!pub_data->exten_state_data.exten
		|| !pub_data->exten_state_data.presence_subtype
		|| !pub_data->exten_state_data.presence_message) {
		exten_state_pub_data_destroy(pub_data);
		return NULL;
	}
	return pub_data;
}

/*!
 * \internal
 * \brief Create exten state PUBLISH messages under PJSIP thread.
 * \since 14.0.0
 *
 * \return 0
 */
static int exten_state_publisher_cb(void *data)
{
	struct exten_state_pub_data *pub_data = data;
	struct exten_state_publisher *publisher;
	size_t idx;
	struct ast_str *body_text;
	pj_pool_t *pool;
	struct ast_sip_body_data gen_data = {
		.body_type = AST_SIP_EXTEN_STATE_DATA,
		.body_data = &pub_data->exten_state_data,
	};
	struct ast_sip_body body;

	body_text = ast_str_create(64);
	if (!body_text) {
		exten_state_pub_data_destroy(pub_data);
		return 0;
	}

	/* Need a PJSIP memory pool to generate the bodies. */
	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "pub_state_body",
		1024, 1024);
	if (!pool) {
		ast_log(LOG_WARNING, "Exten state publishing unable to create memory pool\n");
		exten_state_pub_data_destroy(pub_data);
		ast_free(body_text);
		return 0;
	}
	pub_data->exten_state_data.pool = pool;

	for (idx = 0; idx < AST_VECTOR_SIZE(&pub_data->pubs); ++idx) {
		const char *uri;
		int res;

		publisher = AST_VECTOR_GET(&pub_data->pubs, idx);

		uri = ast_sip_publish_client_get_user_from_uri(publisher->client, pub_data->exten_state_data.exten,
			pub_data->exten_state_data.local, sizeof(pub_data->exten_state_data.local));
		if (ast_strlen_zero(uri)) {
			ast_log(LOG_WARNING, "PUBLISH client '%s' has no from_uri or server_uri defined.\n",
				publisher->name);
			continue;
		}

		uri = ast_sip_publish_client_get_user_to_uri(publisher->client, pub_data->exten_state_data.exten,
			pub_data->exten_state_data.remote, sizeof(pub_data->exten_state_data.remote));
		if (ast_strlen_zero(uri)) {
			ast_log(LOG_WARNING, "PUBLISH client '%s' has no to_uri or server_uri defined.\n",
				publisher->name);
			continue;
		}

		pub_data->exten_state_data.datastores = publisher->datastores;

		res = ast_sip_pubsub_generate_body_content(publisher->body_type,
			publisher->body_subtype, &gen_data, &body_text);
		pj_pool_reset(pool);
		if (res) {
			ast_log(LOG_WARNING,
				"PUBLISH client '%s' unable to generate %s/%s PUBLISH body.\n",
				publisher->name, publisher->body_type, publisher->body_subtype);
			continue;
		}

		body.type = publisher->body_type;
		body.subtype = publisher->body_subtype;
		body.body_text = ast_str_buffer(body_text);
		ast_sip_publish_client_user_send(publisher->client, pub_data->exten_state_data.exten, &body);
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);

	ast_free(body_text);
	exten_state_pub_data_destroy(pub_data);
	return 0;
}

/*!
 * \brief Global extension state callback function
 */
static int exten_state_publisher_state_cb(const char *context, const char *exten, struct ast_state_cb_info *info, void *data)
{
	struct ao2_iterator publisher_iter;
	struct exten_state_publisher *publisher;
	struct exten_state_pub_data *pub_data = NULL;

	ast_debug(5, "Exten state publisher: %s@%s Reason:%s State:%s Presence:%s Subtype:'%s' Message:'%s'\n",
		exten, context,
		info->reason == AST_HINT_UPDATE_DEVICE
			? "Device"
			: info->reason == AST_HINT_UPDATE_PRESENCE
				? "Presence"
				: "Unknown",
		ast_extension_state2str(info->exten_state),
		ast_presence_state2str(info->presence_state),
		S_OR(info->presence_subtype, ""),
		S_OR(info->presence_message, ""));
	publisher_iter = ao2_iterator_init(publishers, 0);
	for (; (publisher = ao2_iterator_next(&publisher_iter)); ao2_ref(publisher, -1)) {
		if ((publisher->context_filter && regexec(&publisher->context_regex, context, 0, NULL, 0)) ||
		    (publisher->exten_filter && regexec(&publisher->exten_regex, exten, 0, NULL, 0))) {
			continue;
		}

		if (!pub_data) {
			pub_data = exten_state_pub_data_alloc(exten, info);
			if (!pub_data) {
				ao2_ref(publisher, -1);
				break;
			}
		}

		ao2_ref(publisher, +1);
		if (AST_VECTOR_APPEND(&pub_data->pubs, publisher)) {
			ao2_ref(publisher, -1);
		} else {
			ast_debug(5, "'%s' will publish exten state\n", publisher->name);
		}
	}
	ao2_iterator_destroy(&publisher_iter);

	if (pub_data
		&& ast_sip_push_task(publish_exten_state_serializer, exten_state_publisher_cb,
			pub_data)) {
		exten_state_pub_data_destroy(pub_data);
	}

	return 0;
}

/*!
 * \brief Hashing function for extension state publisher
 */
static int exten_state_publisher_hash(const void *obj, const int flags)
{
	const struct exten_state_publisher *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*!
 * \brief Comparator function for extension state publisher
 */
static int exten_state_publisher_cmp(void *obj, void *arg, int flags)
{
	const struct exten_state_publisher *object_left = obj;
	const struct exten_state_publisher *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container. */
		ast_assert(0);
		return 0;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

/*!
 * \brief Destructor for extension state publisher
 */
static void exten_state_publisher_destroy(void *obj)
{
	struct exten_state_publisher *publisher = obj;

	if (publisher->context_filter) {
		regfree(&publisher->context_regex);
	}

	if (publisher->exten_filter) {
		regfree(&publisher->exten_regex);
	}

	ao2_cleanup(publisher->client);
	ao2_cleanup(publisher->datastores);
}

static int build_regex(regex_t *regex, const char *text)
{
	int res;

	if ((res = regcomp(regex, text, REG_EXTENDED | REG_ICASE | REG_NOSUB))) {
		size_t len = regerror(res, regex, NULL, 0);
		char buf[len];
		regerror(res, regex, buf, len);
		ast_log(LOG_ERROR, "Could not compile regex '%s': %s\n", text, buf);
		return -1;
	}

	return 0;
}

static int publisher_start(struct ast_sip_outbound_publish *configuration, struct ast_sip_outbound_publish_client *client)
{
	struct exten_state_publisher *publisher;
	size_t name_size;
	size_t body_type_size;
	size_t body_subtype_size;
	char *body_subtype;
	const char *body_full;
	const char *body_type;
	const char *name;
	const char *context;
	const char *exten;

	name = ast_sorcery_object_get_id(configuration);

	body_full = ast_sorcery_object_get_extended(configuration, "body");
	if (ast_strlen_zero(body_full)) {
		ast_log(LOG_ERROR, "Outbound extension state publisher '%s': Body not set\n",
			name);
		return -1;
	}

	body_subtype = ast_strdupa(body_full);
	body_type = strsep(&body_subtype, "/");
	if (ast_strlen_zero(body_type) || ast_strlen_zero(body_subtype)) {
		ast_log(LOG_ERROR, "Outbound extension state publisher '%s': Body '%s' missing type or subtype\n",
			name, body_full);
		return -1;
	}

	if (!ast_sip_pubsub_is_body_generator_registered(body_type, body_subtype)) {
		ast_log(LOG_ERROR, "Outbound extension state publisher '%s': '%s' body generator not registered\n",
			name, body_full);
		return -1;
	}

	name_size = strlen(name) + 1;
	body_type_size = strlen(body_type) + 1;
	body_subtype_size = strlen(body_subtype) + 1;

	publisher = ao2_alloc_options(
		sizeof(*publisher) + name_size + body_type_size + body_subtype_size,
		exten_state_publisher_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!publisher) {
		return -1;
	}

	ast_copy_string(publisher->name, name, name_size);
	publisher->body_type = publisher->name + name_size;
	ast_copy_string(publisher->body_type, body_type, body_type_size);
	publisher->body_subtype = publisher->body_type + body_type_size;
	ast_copy_string(publisher->body_subtype, body_subtype, body_subtype_size);

	context = ast_sorcery_object_get_extended(configuration, "context");
	if (!ast_strlen_zero(context)) {
		if (build_regex(&publisher->context_regex, context)) {
			ast_log(LOG_ERROR, "Outbound extension state publisher '%s': Could not build context filter '%s'\n",
				name, context);
			ao2_ref(publisher, -1);
			return -1;
		}

		publisher->context_filter = 1;
	}

	exten = ast_sorcery_object_get_extended(configuration, "exten");
	if (!ast_strlen_zero(exten)) {
		if (build_regex(&publisher->exten_regex, exten)) {
			ast_log(LOG_ERROR, "Outbound extension state publisher '%s': Could not build exten filter '%s'\n",
				name, exten);
			ao2_ref(publisher, -1);
			return -1;
		}

		publisher->exten_filter = 1;
	}

	publisher->datastores = ast_datastores_alloc();
	if (!publisher->datastores) {
		ast_log(LOG_ERROR, "Outbound extension state publisher '%s': Could not create datastores container\n",
			name);
		ao2_ref(publisher, -1);
		return -1;
	}

	publisher->client = ao2_bump(client);

	ao2_lock(publishers);
	if (!ao2_container_count(publishers)) {
		ast_extension_state_add(NULL, NULL, exten_state_publisher_state_cb, NULL);
	}
	ao2_link_flags(publishers, publisher, OBJ_NOLOCK);
	ao2_unlock(publishers);

	ao2_ref(publisher, -1);

	return 0;
}

static int publisher_stop(struct ast_sip_outbound_publish_client *client)
{
	ao2_find(publishers, ast_sorcery_object_get_id(client), OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
	return 0;
}

static int unload_module(void)
{
	ast_sip_unregister_event_publisher_handler(&dialog_publisher);
	ast_sip_unregister_subscription_handler(&dialog_handler);
	ast_sip_unregister_event_publisher_handler(&presence_publisher);
	ast_sip_unregister_subscription_handler(&presence_handler);

	ast_extension_state_del(0, exten_state_publisher_state_cb);

	ast_taskprocessor_unreference(publish_exten_state_serializer);
	publish_exten_state_serializer = NULL;

	ao2_cleanup(publishers);
	publishers = NULL;

	return 0;
}

static int load_module(void)
{
	publishers = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		PUBLISHER_BUCKETS, exten_state_publisher_hash, NULL, exten_state_publisher_cmp);
	if (!publishers) {
		ast_log(LOG_WARNING, "Unable to create container to store extension state publishers\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	publish_exten_state_serializer = ast_sip_create_serializer("pjsip/exten_state");
	if (!publish_exten_state_serializer) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_register_subscription_handler(&presence_handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n",
			presence_handler.event_name);
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_register_event_publisher_handler(&presence_publisher)) {
		ast_log(LOG_WARNING, "Unable to register presence publisher %s\n",
			presence_publisher.event_name);
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_register_subscription_handler(&dialog_handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n",
			dialog_handler.event_name);
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_register_event_publisher_handler(&dialog_publisher)) {
		ast_log(LOG_WARNING, "Unable to register presence publisher %s\n",
			dialog_publisher.event_name);
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State Notifications",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND + 5,
	.requires = "res_pjsip,res_pjsip_pubsub,res_pjsip_outbound_publish",
);
