/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
#include <pjsip_simple.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_outbound_publish.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/datastore.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_outbound_publish" language="en_US">
		<synopsis>SIP resource for outbound publish</synopsis>
		<description><para>
			<emphasis>Outbound Publish</emphasis>
			</para>
			<para>This module allows <literal>res_pjsip</literal> to publish to other SIP servers.</para>
		</description>
		<configFile name="pjsip.conf">
			<configObject name="outbound-publish">
				<synopsis>The configuration for outbound publish</synopsis>
				<description><para>
					Publish is <emphasis>COMPLETELY</emphasis> separate from the rest of
					<literal>pjsip.conf</literal>. A minimal configuration consists of
					setting a <literal>server_uri</literal> and <literal>event</literal>.
				</para></description>
				<configOption name="expiration" default="3600">
					<synopsis>Expiration time for publications in seconds</synopsis>
				</configOption>
				<configOption name="outbound_auth" default="">
					<synopsis>Authentication object to be used for outbound publishes.</synopsis>
				</configOption>
				<configOption name="outbound_proxy" default="">
					<synopsis>SIP URI of the outbound proxy used to send publishes</synopsis>
				</configOption>
				<configOption name="server_uri">
					<synopsis>SIP URI of the server and entity to publish to</synopsis>
					<description><para>
						This is the URI at which to find the entity and server to send the outbound PUBLISH to.
						This URI is used as the request URI of the outbound PUBLISH request from Asterisk.
					</para></description>
				</configOption>
				<configOption name="from_uri">
					<synopsis>SIP URI to use in the From header</synopsis>
					<description><para>
						This is the URI that will be placed into the From header of outgoing PUBLISH
						messages. If no URI is specified then the URI provided in <literal>server_uri</literal>
						will be used.
					</para></description>
				</configOption>
				<configOption name="to_uri">
					<synopsis>SIP URI to use in the To header</synopsis>
					<description><para>
						This is the URI that will be placed into the To header of outgoing PUBLISH
						messages. If no URI is specified then the URI provided in <literal>server_uri</literal>
						will be used.
					</para></description>
				</configOption>
				<configOption name="event" default="">
					<synopsis>Event type of the PUBLISH.</synopsis>
				</configOption>
				<configOption name="max_auth_attempts" default="5">
					<synopsis>Maximum number of authentication attempts before stopping the publication.</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'outbound-publish'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief Queued outbound publish message */
struct sip_outbound_publish_message {
	/*! \brief Optional body */
	struct ast_sip_body body;
	/*! \brief Linked list information */
	AST_LIST_ENTRY(sip_outbound_publish_message) entry;
	/*! \brief Extra space for body contents */
	char body_contents[0];
};

/*! \brief Outbound publish client state information (persists for lifetime that publish should exist) */
struct ast_sip_outbound_publish_client {
	/*! \brief Underlying publish client */
	pjsip_publishc *client;
	/*! \brief Timer entry for refreshing publish */
	pj_timer_entry timer;
	/*! \brief Publisher datastores set up by handlers */
	struct ao2_container *datastores;
	/*! \brief The number of auth attempts done */
	unsigned int auth_attempts;
	/*! \brief Queue of outgoing publish messages to send*/
	AST_LIST_HEAD_NOLOCK(, sip_outbound_publish_message) queue;
	/*! \brief The message currently being sent */
	struct sip_outbound_publish_message *sending;
	/*! \brief Publish client has been fully started and event type informed */
	unsigned int started;
	/*! \brief Publish client should be destroyed */
	unsigned int destroy;
};

/*! \brief Outbound publish information */
struct ast_sip_outbound_publish {
	/*! \brief Sorcery object details */
	SORCERY_OBJECT(details);
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! \brief URI for the entity and server */
		AST_STRING_FIELD(server_uri);
		/*! \brief URI for the From header */
		AST_STRING_FIELD(from_uri);
		/*! \brief URI for the To header */
		AST_STRING_FIELD(to_uri);
		/*! \brief Outbound proxy to use */
		AST_STRING_FIELD(outbound_proxy);
		/*! \brief The event type to publish */
		AST_STRING_FIELD(event);
	);
	/*! \brief Requested expiration time */
	unsigned int expiration;
	/*! \brief Maximum number of auth attempts before stopping the publish client */
	unsigned int max_auth_attempts;
	/*! \brief Configured authentication credentials */
	struct ast_sip_auth_vector outbound_auths;
	/*! \brief Outbound publish state */
	struct ast_sip_outbound_publish_client *state;
};

AST_RWLIST_HEAD_STATIC(publisher_handlers, ast_sip_event_publisher_handler);

/*! \brief Container of currently active publish clients */
static AO2_GLOBAL_OBJ_STATIC(active);

static void sub_add_handler(struct ast_sip_event_publisher_handler *handler)
{
	AST_RWLIST_INSERT_TAIL(&publisher_handlers, handler, next);
	ast_module_ref(ast_module_info->self);
}

static struct ast_sip_event_publisher_handler *find_publisher_handler_for_event_name(const char *event_name)
{
	struct ast_sip_event_publisher_handler *iter;

	AST_RWLIST_TRAVERSE(&publisher_handlers, iter, next) {
		if (!strcmp(iter->event_name, event_name)) {
			break;
		}
	}
	return iter;
}

/*! \brief Helper function which cancels the refresh timer on a client */
static void cancel_publish_refresh(struct ast_sip_outbound_publish_client *client)
{
	if (pj_timer_heap_cancel(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()), &client->timer)) {
		/* The timer was successfully cancelled, drop the refcount of the client */
		ao2_ref(client, -1);
	}
}

/*! \brief Helper function which sets up the timer to send publication */
static void schedule_publish_refresh(struct ast_sip_outbound_publish *publish, pjsip_rx_data *rdata)
{
	pj_time_val delay = { .sec = 0, };
	pjsip_expires_hdr *expires;

	cancel_publish_refresh(publish->state);

	/* Determine when we should refresh - we favor the Expires header if possible */
	expires = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);
	if (expires) {
		delay.sec = expires->ivalue - PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH;
	}
	if (publish->expiration && ((delay.sec > publish->expiration) || !delay.sec)) {
		delay.sec = publish->expiration;
	}
	if (delay.sec < PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH) {
		delay.sec = PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH;
	}

	ao2_ref(publish->state, +1);
	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &publish->state->timer, &delay) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to pass timed publish refresh to scheduler\n");
		ao2_ref(publish->state, -1);
	}
}

/*! \brief Publish client timer callback function */
static void sip_outbound_publish_timer_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	struct ast_sip_outbound_publish_client *client = entry->user_data;

	ao2_lock(client);
	if (AST_LIST_EMPTY(&client->queue)) {
		/* If there are no outstanding messages send an empty PUBLISH message so our publication doesn't expire */
		ast_sip_publish_client_send(client, NULL);
	}
	ao2_unlock(client);

	ao2_ref(client, -1);
}

/*! \brief Task for cancelling a refresh timer */
static int cancel_refresh_timer_task(void *data)
{
	struct ast_sip_outbound_publish_client *state = data;

	cancel_publish_refresh(state);
	ao2_ref(state, -1);

	return 0;
}

/*! \brief Task for sending an unpublish */
static int send_unpublish_task(void *data)
{
	struct ast_sip_outbound_publish_client *state = data;
	pjsip_tx_data *tdata;

	if (pjsip_publishc_unpublish(state->client, &tdata) == PJ_SUCCESS) {
		pjsip_publishc_send(state->client, tdata);
	}

	ao2_ref(state, -1);

	return 0;
}

/*! \brief Helper function which starts or stops publish clients when applicable */
static void sip_outbound_publish_synchronize(struct ast_sip_event_publisher_handler *removed)
{
	RAII_VAR(struct ao2_container *, publishes, ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "outbound-publish", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL), ao2_cleanup);
	struct ao2_iterator i;
	struct ast_sip_outbound_publish *publish;

	if (!publishes) {
		return;
	}

	i = ao2_iterator_init(publishes, 0);
	while ((publish = ao2_iterator_next(&i))) {
		struct ast_sip_event_publisher_handler *handler = find_publisher_handler_for_event_name(publish->event);

		if (!publish->state->started) {
			/* If the publisher client has not yet been started try to start it */
			if (!handler) {
				ast_debug(2, "Could not find handler for event '%s' for outbound publish client '%s'\n",
					publish->event, ast_sorcery_object_get_id(publish));
			} else if (handler->start_publishing(publish, publish->state)) {
				ast_log(LOG_ERROR, "Failed to start outbound publish with event '%s' for client '%s'\n",
					publish->event, ast_sorcery_object_get_id(publish));
			} else {
				publish->state->started = 1;
			}
		} else if (publish->state->started && !handler && removed && !strcmp(publish->event, removed->event_name)) {
			/* If the publisher client has been started but it is going away stop it */
			removed->stop_publishing(publish->state);
			publish->state->started = 0;
			if (ast_sip_push_task(NULL, cancel_refresh_timer_task, ao2_bump(publish->state))) {
				ast_log(LOG_WARNING, "Could not stop refresh timer on client '%s'\n",
					ast_sorcery_object_get_id(publish));
				ao2_ref(publish->state, -1);
			}
		}
		ao2_ref(publish, -1);
	}
	ao2_iterator_destroy(&i);
}

struct ast_sip_outbound_publish_client *ast_sip_publish_client_get(const char *name)
{
	RAII_VAR(struct ast_sip_outbound_publish *, publish, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "outbound-publish", name), ao2_cleanup);

	if (!publish) {
		return NULL;
	}

	return ao2_bump(publish->state);
}

int ast_sip_register_event_publisher_handler(struct ast_sip_event_publisher_handler *handler)
{
	struct ast_sip_event_publisher_handler *existing;
	SCOPED_LOCK(lock, &publisher_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	if (!handler->start_publishing || !handler->stop_publishing) {
		ast_log(LOG_ERROR, "Handler does not implement required callbacks. Cannot register\n");
		return -1;
	} else if (ast_strlen_zero(handler->event_name)) {
		ast_log(LOG_ERROR, "No event package specified for event publisher handler. Cannot register\n");
		return -1;
	}

	existing = find_publisher_handler_for_event_name(handler->event_name);
	if (existing) {
		ast_log(LOG_ERROR, "Unable to register event publisher handler for event %s. "
				"A handler is already registered\n", handler->event_name);
		return -1;
	}

	sub_add_handler(handler);

	sip_outbound_publish_synchronize(NULL);

	return 0;
}

void ast_sip_unregister_event_publisher_handler(struct ast_sip_event_publisher_handler *handler)
{
	struct ast_sip_event_publisher_handler *iter;
	SCOPED_LOCK(lock, &publisher_handlers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&publisher_handlers, iter, next) {
		if (handler == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	sip_outbound_publish_synchronize(handler);
}

/*! \brief Destructor function for publish information */
static void sip_outbound_publish_destroy(void *obj)
{
	struct ast_sip_outbound_publish *publish = obj;
	SCOPED_LOCK(lock, &publisher_handlers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	struct ast_sip_event_publisher_handler *handler = find_publisher_handler_for_event_name(publish->event);

	if (handler) {
		handler->stop_publishing(publish->state);
	}
	if (publish->state) {
		cancel_publish_refresh(publish->state);
		ao2_ref(publish->state, -1);
	}
	ast_sip_auth_vector_destroy(&publish->outbound_auths);

	ast_string_field_free_memory(publish);
}

/*! \brief Allocator function for publish information */
static void *sip_outbound_publish_alloc(const char *name)
{
	struct ast_sip_outbound_publish *publish = ast_sorcery_generic_alloc(sizeof(*publish),
		sip_outbound_publish_destroy);

	if (!publish || ast_string_field_init(publish, 256)) {
		ao2_cleanup(publish);
		return NULL;
	}

	return publish;
}

static void sip_outbound_publish_datastore_destroy(void *obj)
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

struct ast_datastore *ast_sip_publish_client_alloc_datastore(const struct ast_datastore_info *info, const char *uid)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	const char *uid_ptr = uid;
	char uuid_buf[AST_UUID_STR_LEN];

	if (!info) {
		return NULL;
	}

	datastore = ao2_alloc(sizeof(*datastore), sip_outbound_publish_datastore_destroy);
	if (!datastore) {
		return NULL;
	}

	datastore->info = info;
	if (ast_strlen_zero(uid)) {
		/* They didn't provide an ID so we'll provide one ourself */
		uid_ptr = ast_uuid_generate_str(uuid_buf, sizeof(uuid_buf));
	}

	datastore->uid = ast_strdup(uid_ptr);
	if (!datastore->uid) {
		return NULL;
	}

	ao2_ref(datastore, +1);
	return datastore;
}

int ast_sip_publish_client_add_datastore(struct ast_sip_outbound_publish_client *client,
	struct ast_datastore *datastore)
{
	ast_assert(datastore != NULL);
	ast_assert(datastore->info != NULL);
	ast_assert(!ast_strlen_zero(datastore->uid));

	if (!ao2_link(client->datastores, datastore)) {
		return -1;
	}
	return 0;
}

struct ast_datastore *ast_sip_publish_client_get_datastore(struct ast_sip_outbound_publish_client *client,
	const char *name)
{
	return ao2_find(client->datastores, name, OBJ_SEARCH_KEY);
}

void ast_sip_publish_client_remove_datastore(struct ast_sip_outbound_publish_client *client,
	const char *name)
{
	ao2_find(client->datastores, name, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
}

static int sip_publish_client_service_queue(void *data)
{
	RAII_VAR(struct ast_sip_outbound_publish_client *, client, data, ao2_cleanup);
	SCOPED_AO2LOCK(lock, client);
	struct sip_outbound_publish_message *message;
	pjsip_tx_data *tdata;
	pj_status_t status;

	if (client->destroy || client->sending || !(message = AST_LIST_FIRST(&client->queue))) {
		return 0;
	}

	if (pjsip_publishc_publish(client->client, PJ_FALSE, &tdata) != PJ_SUCCESS) {
		goto fatal;
	}

	if (!ast_strlen_zero(message->body.type) && !ast_strlen_zero(message->body.subtype) &&
		ast_sip_add_body(tdata, &message->body)) {
		pjsip_tx_data_dec_ref(tdata);
		goto fatal;
	}

	status = pjsip_publishc_send(client->client, tdata);
	if (status == PJ_EBUSY) {
		/* We attempted to send the message but something else got there first */
		goto service;
	} else if (status != PJ_SUCCESS) {
		goto fatal;
	}

	client->sending = message;

	return 0;

fatal:
	AST_LIST_REMOVE_HEAD(&client->queue, entry);
	ast_free(message);

service:
	if (ast_sip_push_task(NULL, sip_publish_client_service_queue, ao2_bump(client))) {
		ao2_ref(client, -1);
	}
	return -1;
}

int ast_sip_publish_client_send(struct ast_sip_outbound_publish_client *client,
	const struct ast_sip_body *body)
{
	SCOPED_AO2LOCK(lock, client);
	struct sip_outbound_publish_message *message;
	size_t type_len = 0, subtype_len = 0, body_text_len = 0;
	int res;

	if (!client->client) {
		return -1;
	}

	/* If a body is present we need more space for the contents of it */
	if (body) {
		type_len = strlen(body->type) + 1;
		subtype_len = strlen(body->subtype) + 1;
		body_text_len = strlen(body->body_text) + 1;
	}

	message = ast_calloc(1, sizeof(*message) + type_len + subtype_len + body_text_len);
	if (!message) {
		return -1;
	}

	if (body) {
		char *dst = message->body_contents;

		message->body.type = strcpy(dst, body->type);
		dst += type_len;
		message->body.subtype = strcpy(dst, body->subtype);
		dst += subtype_len;
		message->body.body_text = strcpy(dst, body->body_text);
	}

	AST_LIST_INSERT_TAIL(&client->queue, message, entry);

	res = ast_sip_push_task(NULL, sip_publish_client_service_queue, ao2_bump(client));
	if (res) {
		ao2_ref(client, -1);
	}

	return res;
}

/*! \brief Destructor function for publish state */
static void sip_outbound_publish_client_destroy(void *obj)
{
	struct ast_sip_outbound_publish_client *state = obj;
	struct sip_outbound_publish_message *message;

	/* You might be tempted to think "the publish client isn't being destroyed" but it actually is - just elsewhere */

	while ((message = AST_LIST_REMOVE_HEAD(&state->queue, entry))) {
		ast_free(message);
	}

	ao2_cleanup(state->datastores);
}

/*!
 * \internal
 * \brief Check if a publish can be reused
 *
 * This checks if the existing outbound publish's configuration differs from a newly-applied
 * outbound publish.
 *
 * \param existing The pre-existing outbound publish
 * \param applied The newly-created publish
 */
static int can_reuse_publish(struct ast_sip_outbound_publish *existing, struct ast_sip_outbound_publish *applied)
{
	int i;

	if (strcmp(existing->server_uri, applied->server_uri) || strcmp(existing->from_uri, applied->from_uri) ||
		strcmp(existing->to_uri, applied->to_uri) || strcmp(existing->outbound_proxy, applied->outbound_proxy) ||
		strcmp(existing->event, applied->event) ||
		AST_VECTOR_SIZE(&existing->outbound_auths) != AST_VECTOR_SIZE(&applied->outbound_auths)) {
		return 0;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&existing->outbound_auths); ++i) {
		if (strcmp(AST_VECTOR_GET(&existing->outbound_auths, i), AST_VECTOR_GET(&applied->outbound_auths, i))) {
			return 0;
		}
	}

	return 1;
}

static void sip_outbound_publish_callback(struct pjsip_publishc_cbparam *param);

/*! \brief Helper function that allocates a pjsip publish client and configures it */
static int sip_outbound_publish_client_alloc(void *data)
{
	struct ast_sip_outbound_publish *publish = data;
	pjsip_publishc_opt opt = {
		.queue_request = PJ_FALSE,
	};
	pj_str_t event, server_uri, to_uri, from_uri;
	pj_status_t status;

	if (publish->state->client) {
		return 0;
	} else if (pjsip_publishc_create(ast_sip_get_pjsip_endpoint(), &opt, ao2_bump(publish), sip_outbound_publish_callback,
		&publish->state->client) != PJ_SUCCESS) {
		ao2_ref(publish, -1);
		return -1;
	}

	if (!ast_strlen_zero(publish->outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };

		pj_list_init(&route_set);

		if (!(route = pjsip_parse_hdr(pjsip_publishc_get_pool(publish->state->client), &ROUTE_HNAME,
			(char*)publish->outbound_proxy, strlen(publish->outbound_proxy), NULL))) {
			pjsip_publishc_destroy(publish->state->client);
			return -1;
		}
		pj_list_insert_nodes_before(&route_set, route);

		pjsip_publishc_set_route_set(publish->state->client, &route_set);
	}

	pj_cstr(&event, publish->event);
	pj_cstr(&server_uri, publish->server_uri);
	pj_cstr(&to_uri, S_OR(publish->to_uri, publish->server_uri));
	pj_cstr(&from_uri, S_OR(publish->from_uri, publish->server_uri));

	status = pjsip_publishc_init(publish->state->client, &event, &server_uri, &from_uri, &to_uri,
		publish->expiration);
	if (status == PJSIP_EINVALIDURI) {
		pj_pool_t *pool;
		pj_str_t tmp;
		pjsip_uri *uri;

		pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "URI Validation", 256, 256);
		if (!pool) {
			ast_log(LOG_ERROR, "Could not create pool for URI validation on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
			pjsip_publishc_destroy(publish->state->client);
			return -1;
		}

		pj_strdup2_with_null(pool, &tmp, publish->server_uri);
		uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
		if (!uri) {
			ast_log(LOG_ERROR, "Invalid server URI '%s' specified on outbound publish '%s'\n",
				publish->server_uri, ast_sorcery_object_get_id(publish));
		}

		if (!ast_strlen_zero(publish->to_uri)) {
			pj_strdup2_with_null(pool, &tmp, publish->to_uri);
			uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
			if (!uri) {
				ast_log(LOG_ERROR, "Invalid to URI '%s' specified on outbound publish '%s'\n",
					publish->to_uri, ast_sorcery_object_get_id(publish));
			}
		}

		if (!ast_strlen_zero(publish->from_uri)) {
			pj_strdup2_with_null(pool, &tmp, publish->from_uri);
			uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
			if (!uri) {
				ast_log(LOG_ERROR, "Invalid from URI '%s' specified on outbound publish '%s'\n",
					publish->from_uri, ast_sorcery_object_get_id(publish));
			}
		}

		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		pjsip_publishc_destroy(publish->state->client);
		return -1;
	} else if (status != PJ_SUCCESS) {
		pjsip_publishc_destroy(publish->state->client);
		return -1;
	}

	return 0;
}

/*! \brief Callback function for publish client responses */
static void sip_outbound_publish_callback(struct pjsip_publishc_cbparam *param)
{
	RAII_VAR(struct ast_sip_outbound_publish *, publish, ao2_bump(param->token), ao2_cleanup);
	SCOPED_AO2LOCK(lock, publish->state);
	pjsip_tx_data *tdata;

	if (publish->state->destroy) {
		if (publish->state->sending) {
			publish->state->sending = NULL;
			if (!ast_sip_push_task(NULL, send_unpublish_task, ao2_bump(publish->state))) {
				return;
			}
			ast_log(LOG_WARNING, "Could not send unpublish message on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
			ao2_ref(publish->state, -1);
		}
		/* Once the destroy is called this callback will not get called any longer, so drop the publish ref */
		pjsip_publishc_destroy(publish->state->client);
		ao2_ref(publish, -1);
		return;
	}

	if (param->code == 401 || param->code == 407) {
		if (!ast_sip_create_request_with_auth(&publish->outbound_auths,
				param->rdata, pjsip_rdata_get_tsx(param->rdata), &tdata)) {
			pjsip_publishc_send(publish->state->client, tdata);
		}
		publish->state->auth_attempts++;

		if (publish->state->auth_attempts == publish->max_auth_attempts) {
			pjsip_publishc_destroy(publish->state->client);
			publish->state->client = NULL;

			ast_log(LOG_ERROR, "Reached maximum number of PUBLISH authentication attempts on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));

			goto end;
		}

		return;
	}

	publish->state->auth_attempts = 0;

	if (param->code == 412) {
		pjsip_publishc_destroy(publish->state->client);
		publish->state->client = NULL;

		if (sip_outbound_publish_client_alloc(publish)) {
			ast_log(LOG_ERROR, "Failed to create a new outbound publish client for '%s' on 412 response\n",
				ast_sorcery_object_get_id(publish));
			goto end;
		}

		/* Setting this to NULL will cause a new PUBLISH to get created and sent for the same underlying body */
		publish->state->sending = NULL;
	} else if (param->code == 423) {
		/* Update the expiration with the new expiration time if available */
		pjsip_expires_hdr *expires;

		expires = pjsip_msg_find_hdr(param->rdata->msg_info.msg, PJSIP_H_MIN_EXPIRES, NULL);
		if (!expires || !expires->ivalue) {
			ast_log(LOG_ERROR, "Received 423 response on outbound publish '%s' without a Min-Expires header\n",
				ast_sorcery_object_get_id(publish));
			pjsip_publishc_destroy(publish->state->client);
			publish->state->client = NULL;
			goto end;
		}

		pjsip_publishc_update_expires(publish->state->client, expires->ivalue);
		publish->state->sending = NULL;
	} else if (publish->state->sending) {
		/* Remove the message currently being sent so that when the queue is serviced another will get sent */
		AST_LIST_REMOVE_HEAD(&publish->state->queue, entry);
		ast_free(publish->state->sending);
		publish->state->sending = NULL;
	}

	if (AST_LIST_EMPTY(&publish->state->queue)) {
		schedule_publish_refresh(publish, param->rdata);
	}

end:
	if (!publish->state->client) {
		struct sip_outbound_publish_message *message;

		while ((message = AST_LIST_REMOVE_HEAD(&publish->state->queue, entry))) {
			ast_free(message);
		}
	} else {
		if (ast_sip_push_task(NULL, sip_publish_client_service_queue, ao2_bump(publish->state))) {
			ao2_ref(publish->state, -1);
		}
	}
}

#define DATASTORE_BUCKETS 53

static int datastore_hash(const void *obj, int flags)
{
	const struct ast_datastore *datastore;
	const char *uid;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		uid = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		datastore = obj;
		uid = datastore->uid;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}

	return ast_str_hash(uid);
}

static int datastore_cmp(void *obj, void *arg, int flags)
{
	const struct ast_datastore *object_left = obj;
	const struct ast_datastore *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->uid;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->uid, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
        cmp = strncmp(object_left->uid, right_key, strlen(right_key));
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

/*! \brief Allocator function for publish client state */
static struct ast_sip_outbound_publish_client *sip_outbound_publish_state_alloc(void)
{
	struct ast_sip_outbound_publish_client *state = ao2_alloc(sizeof(*state), sip_outbound_publish_client_destroy);

	if (!state) {
		return NULL;
	}

	state->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!state->datastores) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->timer.user_data = state;
	state->timer.cb = sip_outbound_publish_timer_cb;

	return state;
}

/*! \brief Apply function which finds or allocates a state structure */
static int sip_outbound_publish_apply(const struct ast_sorcery *sorcery, void *obj)
{
	RAII_VAR(struct ast_sip_outbound_publish *, existing, ast_sorcery_retrieve_by_id(sorcery, "outbound-publish", ast_sorcery_object_get_id(obj)), ao2_cleanup);
	struct ast_sip_outbound_publish *applied = obj;

	if (ast_strlen_zero(applied->server_uri)) {
		ast_log(LOG_ERROR, "No server URI specified on outbound publish '%s'\n",
			ast_sorcery_object_get_id(applied));
		return -1;
	} else if (ast_strlen_zero(applied->event)) {
		ast_log(LOG_ERROR, "No event type specified for outbound publish '%s'\n",
			ast_sorcery_object_get_id(applied));
		return -1;
	}

	if (!existing) {
		/* If no existing publish exists we can just start fresh easily */
		applied->state = sip_outbound_publish_state_alloc();
	} else {
		/* If there is an existing publish things are more complicated, we can immediately reuse this state if most stuff remains unchanged */
		if (can_reuse_publish(existing, applied)) {
			applied->state = existing->state;
			ao2_ref(applied->state, +1);
		} else {
			applied->state = sip_outbound_publish_state_alloc();
		}
	}

	if (!applied->state) {
		return -1;
	}

	return ast_sip_push_task_synchronous(NULL, sip_outbound_publish_client_alloc, applied);
}

static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_outbound_publish *publish = obj;

	return ast_sip_auth_vector_init(&publish->outbound_auths, var->value);
}

/*! \brief Helper function which prunes old publish clients */
static void prune_publish_clients(const char *object_type)
{
	struct ao2_container *old, *current;

	old = ao2_global_obj_ref(active);
	if (old) {
		struct ao2_iterator i;
		struct ast_sip_outbound_publish *existing;

		i = ao2_iterator_init(old, 0);
		for (; (existing = ao2_iterator_next(&i)); ao2_ref(existing, -1)) {
			struct ast_sip_outbound_publish *created;

			created = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "outbound-publish",
				ast_sorcery_object_get_id(existing));
			if (created) {
				if (created->state == existing->state) {
					ao2_ref(created, -1);
					continue;
				}
				ao2_ref(created, -1);
			}

			ao2_lock(existing->state);

			/* If this publish client is currently publishing stop and terminate any refresh timer */
			if (existing->state->started) {
				struct ast_sip_event_publisher_handler *handler = find_publisher_handler_for_event_name(existing->event);

				if (handler) {
					handler->stop_publishing(existing->state);
				}

				if (ast_sip_push_task(NULL, cancel_refresh_timer_task, ao2_bump(existing->state))) {
					ast_log(LOG_WARNING, "Could not stop refresh timer on outbound publish '%s'\n",
						ast_sorcery_object_get_id(existing));
					ao2_ref(existing->state, -1);
				}
			}

			/* If nothing is being sent right now send the unpublish - the destroy will happen in the subsequent callback */
			if (!existing->state->sending) {
				if (ast_sip_push_task(NULL, send_unpublish_task, ao2_bump(existing->state))) {
					ast_log(LOG_WARNING, "Could not send unpublish message on outbound publish '%s'\n",
						ast_sorcery_object_get_id(existing));
					ao2_ref(existing->state, -1);
				}
			}

			existing->state->destroy = 1;
			ao2_unlock(existing->state);
		}
		ao2_iterator_destroy(&i);

		ao2_ref(old, -1);
	}

	current = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "outbound-publish", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	ao2_global_obj_replace_unref(active, current);
}

static struct ast_sorcery_observer outbound_publish_observer = {
	.loaded = prune_publish_clients,
};

static int load_module(void)
{
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "outbound-publish", "config", "pjsip.conf,criteria=type=outbound-publish");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "outbound-publish", sip_outbound_publish_alloc, NULL,
		sip_outbound_publish_apply)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "outbound-publish", &outbound_publish_observer);

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "server_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, server_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "from_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, from_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "event", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, event));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "to_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, to_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, outbound_proxy));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "expiration", "3600", OPT_UINT_T, 0, FLDSET(struct ast_sip_outbound_publish, expiration));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "max_auth_attempts", "5", OPT_UINT_T, 0, FLDSET(struct ast_sip_outbound_publish, max_auth_attempts));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "outbound-publish", "outbound_auth", "", outbound_auth_handler, NULL, NULL, 0, 0);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "outbound-publish");

	AST_RWLIST_RDLOCK(&publisher_handlers);
	sip_outbound_publish_synchronize(NULL);
	AST_RWLIST_UNLOCK(&publisher_handlers);

	pjsip_publishc_init_module(ast_sip_get_pjsip_endpoint());

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "outbound-publish");

	AST_RWLIST_RDLOCK(&publisher_handlers);
	sip_outbound_publish_synchronize(NULL);
	AST_RWLIST_UNLOCK(&publisher_handlers);
	return 0;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Outbound Publish Support",
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	       );
