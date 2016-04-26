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
				<configOption name="transport">
					<synopsis>Transport used for outbound publish</synopsis>
					<description>
						<note><para>A <replaceable>transport</replaceable> configured in
						<literal>pjsip.conf</literal>. As with other <literal>res_pjsip</literal> modules, this will use the first available transport of the appropriate type if unconfigured.</para></note>
					</description>
				</configOption>
				<configOption name="multi_user" default="no">
					<synopsis>Enable multi-user support</synopsis>
					<description><para>When enabled the user portion of the server uri is replaced by a dynamically created user</para></description>
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
		/*! \brief Explicit transport to use for publish */
		AST_STRING_FIELD(transport);
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
	/*! \brief The publishing client is used for multiple users when true */
	unsigned int multi_user;
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
	/*! \brief Outbound publish information */
	struct ast_sip_outbound_publish *publish;
	/*! \brief The name of the transport to be used for the publish */
	char *transport_name;
	/*! \brief Memory pool for uri objects */
	pj_pool_t *pool;
	/*! \brief URI for the entity and server */
	pjsip_sip_uri *server_uri;
	/*! \brief URI for the To header */
	pjsip_sip_uri *to_uri;
	/*! \brief URI for the From header */
	pjsip_sip_uri *from_uri;
};

/*! \brief Outbound publish state information (persists for lifetime of a publish) */
struct ast_sip_outbound_publish_state {
	/*! \brief Publish state id - same as publish configuration id */
	char *id;
	/*! \brief Multi-user identity */
	char *user;
	/*! \brief Outbound publish client */
	struct ast_sip_outbound_publish_client *client;
};

/*! \brief Unloading data */
struct unloading_data {
	int is_unloading;
	int count;
	ast_mutex_t lock;
	ast_cond_t cond;
} unloading;

/*! \brief Default number of multi-user publish  buckets */
#define DEFAULT_MULTI_BUCKETS 13
static struct ao2_container *multi_publishes;

/*! \brief hashing function for multi-user publish objects */
static int outbound_publish_multi_hash(const void *obj, const int flags)
{
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		key = ast_sorcery_object_get_id(obj);
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparator function for multi-user publish objects */
static int outbound_publish_multi_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sip_outbound_publish *object_left = obj;
	const struct ast_sip_outbound_publish *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(object_right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(object_left), right_key);
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
 * \brief Used for locking while loading/reloading
 *
 * Loading or reloading items cannot be added or removed from the
 * current_states container. This is because during a load/reload
 * a separate container (new_states) is populated with the new incoming
 * back end data. Once all new objects have been applied this new_states
 * container is swapped and becomes current_states. If objects were
 * added or removed to current_states while new_states is being built
 * then some items may be missed.
 */
AST_RWLOCK_DEFINE_STATIC(load_lock);
/*! \brief Default number of client state container buckets */
#define DEFAULT_STATE_BUCKETS 31
static AO2_GLOBAL_OBJ_STATIC(current_states);
/*! \brief Used on [re]loads to hold new state data */
static struct ao2_container *new_states;

/*! \brief hashing function for state objects */
static int outbound_publish_state_hash(const void *obj, const int flags)
{
	const struct ast_sip_outbound_publish_state *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->id;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparator function (by id) for state objects */
static int outbound_publish_state_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sip_outbound_publish_state *object_left = obj;
	const struct ast_sip_outbound_publish_state *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->id, right_key);
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

/*! \brief Comparator function (by user) for state objects */
static int outbound_publish_state_find_by_user(void *obj, void *arg, int flags)
{
	const struct ast_sip_outbound_publish_state *state = obj;
	const char *user = arg;

	return strcmp(state->user, user) == 0 ? CMP_MATCH : 0;
}

static struct ao2_container *find_states_by_user(const char *user)
{
	struct ao2_container *states, *res;

	ast_assert((states = ao2_global_obj_ref(current_states)) != NULL);

	res = ao2_callback(states, OBJ_MULTIPLE,
			   outbound_publish_state_find_by_user, (void *)user);

	ao2_ref(states, -1);
	return res;
}

static struct ao2_container *get_publishes_and_update_state(void)
{
	struct ao2_container *container;
	SCOPED_WRLOCK(lock, &load_lock);

	container = ast_sorcery_retrieve_by_fields(
		ast_sip_get_sorcery(), "outbound-publish",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	if (!new_states) {
		return container;
	}

	ao2_global_obj_replace_unref(current_states, new_states);
	ao2_cleanup(new_states);
	new_states = NULL;

	return container;
}

AST_RWLIST_HEAD_STATIC(publisher_handlers, ast_sip_event_publisher_handler);

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
static void schedule_publish_refresh(struct ast_sip_outbound_publish_client *client, int expiration)
{
	struct ast_sip_outbound_publish *publish = ao2_bump(client->publish);
	pj_time_val delay = { .sec = 0, };

	cancel_publish_refresh(client);

	if (expiration > 0) {
		delay.sec = expiration - PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH;
	}
	if (publish->expiration && ((delay.sec > publish->expiration) || !delay.sec)) {
		delay.sec = publish->expiration;
	}
	if (delay.sec < PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH) {
		delay.sec = PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH;
	}

	ao2_ref(client, +1);
	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &client->timer, &delay) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to pass timed publish refresh to scheduler\n");
		ao2_ref(client, -1);
	}
	ao2_ref(publish, -1);
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
	struct ast_sip_outbound_publish_client *client = data;

	cancel_publish_refresh(client);
	ao2_ref(client, -1);

	return 0;
}

/*! \brief Task for sending an unpublish */
static int send_unpublish_task(void *data)
{
	struct ast_sip_outbound_publish_client *client = data;
	pjsip_tx_data *tdata;

	if (pjsip_publishc_unpublish(client->client, &tdata) == PJ_SUCCESS) {
		if (!ast_strlen_zero(client->transport_name)) {
			pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
			ast_sip_set_tpselector_from_transport_name(client->transport_name, &selector);
			pjsip_tx_data_set_transport(tdata, &selector);
		}

		pjsip_publishc_send(client->client, tdata);
	}

	ao2_ref(client, -1);

	return 0;
}

/*! \brief Helper function which starts or stops publish clients when applicable */
static void sip_outbound_publish_synchronize(struct ast_sip_event_publisher_handler *removed)
{
	RAII_VAR(struct ao2_container *, publishes, get_publishes_and_update_state(), ao2_cleanup);
	struct ao2_container *states;
	struct ao2_iterator i;
	struct ast_sip_outbound_publish_state *state;

	if (!publishes) {
		return;
	}

	states = ao2_global_obj_ref(current_states);
	if (!states) {
		return;
	}

	i = ao2_iterator_init(states, 0);
	while ((state = ao2_iterator_next(&i))) {
		struct ast_sip_outbound_publish *publish = ao2_bump(state->client->publish);
		struct ast_sip_event_publisher_handler *handler = find_publisher_handler_for_event_name(publish->event);

		if (!state->client->started) {
			/* If the publisher client has not yet been started try to start it */
			if (!handler) {
				ast_debug(2, "Could not find handler for event '%s' for outbound publish client '%s'\n",
					  publish->event, ast_sorcery_object_get_id(publish));
			} else if (handler->start_publishing(publish, state->client)) {
				ast_log(LOG_ERROR, "Failed to start outbound publish with event '%s' for client '%s'\n",
					publish->event, ast_sorcery_object_get_id(publish));
			} else {
				state->client->started = 1;
			}
		} else if (state->client->started && !handler && removed && !strcmp(publish->event, removed->event_name)) {
			/* If the publisher client has been started but it is going away stop it */
			removed->stop_publishing(state->client);
			state->client->started = 0;
			if (ast_sip_push_task(NULL, cancel_refresh_timer_task, ao2_bump(state->client))) {
				ast_log(LOG_WARNING, "Could not stop refresh timer on client '%s'\n",
					ast_sorcery_object_get_id(publish));
				ao2_ref(state->client, -1);
			}
		}
		ao2_ref(publish, -1);
		ao2_ref(state, -1);
	}
	ao2_iterator_destroy(&i);
	ao2_ref(states, -1);
}

struct ast_sip_outbound_publish_client *ast_sip_publish_client_get(const char *name)
{
	RAII_VAR(struct ao2_container *, states,
		 ao2_global_obj_ref(current_states), ao2_cleanup);
	RAII_VAR(struct ast_sip_outbound_publish_state *, state, NULL, ao2_cleanup);

	if (!states) {
		return NULL;
	}

	state = ao2_find(states, name, OBJ_SEARCH_KEY);
	if (!state) {
		return NULL;
	}

	ao2_ref(state->client, +1);
	return state->client;
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

	if (!ast_strlen_zero(client->transport_name)) {
		pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
		ast_sip_set_tpselector_from_transport_name(client->transport_name, &selector);
		pjsip_tx_data_set_transport(tdata, &selector);
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

static int publish_client_send(void *obj, void *arg, int flags)
{
	struct ast_sip_outbound_publish_state *state = obj;

	return ast_sip_publish_client_send(state->client, arg);
}

static struct ast_sip_outbound_publish_state *validate_and_create_state(
	struct ast_sip_outbound_publish *publish, const char *user);
static int apply_initialize_state(struct ast_sip_outbound_publish *publish,
	struct ast_sip_outbound_publish_state *state);

static int publish_create_client_send(void *obj, void *arg, void *data, int flags)
{
	struct ast_sip_outbound_publish *publish = obj;
	struct ast_sip_outbound_publish_state *new_state;
	struct ao2_container *states;
	int res;

	ast_assert((states = ao2_global_obj_ref(current_states)) != NULL);

	if (!(new_state = validate_and_create_state(publish, data)) ||
	    (apply_initialize_state(publish, new_state))) {
		ao2_cleanup(new_state);
		ao2_ref(states, -1);
		return -1;
	}

	/* Can't have any new items added to current_states while we are [re]loading */
	ast_rwlock_wrlock(&load_lock);
	if (!ao2_link(states, new_state)) {
		ast_rwlock_unlock(&load_lock);
		ao2_ref(new_state, -1);
		ao2_ref(states, -1);
		return -1;
	}
	ast_rwlock_unlock(&load_lock);

	res = ast_sip_publish_client_send(new_state->client, arg);

	ao2_ref(new_state, -1);
	ao2_ref(states, -1);
	return res;
}

int ast_sip_publish_user_send(const char *user, const struct ast_sip_body *body)
{
	struct ao2_container *states = find_states_by_user(user);

	if (states && ao2_container_count(states)) {
		/* User already has publish client(s) */
		ao2_callback(states, OBJ_NODATA, publish_client_send, (void *)body);
		ao2_ref(states, -1);
		return 0;
	}
	ao2_cleanup(states);

	/*
	 * Since the user does not have publish clients created yet we'll
	 * create and publish them now. We need to do this for each multi
	 * user configuration.
	 */
	ao2_callback_data(multi_publishes, OBJ_NODATA,
		publish_create_client_send, (void *)body, (void *)user);
	return 0;
}

void ast_sip_publish_user_remove(const char *user)
{
	struct ao2_container *states;

	/* Can't remove items from current_states while [re]loading */
	SCOPED_WRLOCK(lock, &load_lock);
	ast_assert((states = ao2_global_obj_ref(current_states)) != NULL);

	ao2_callback(states, OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NODATA,
		     outbound_publish_state_find_by_user, (void *)user);
	ao2_ref(states, -1);
}

static int release_pjsip_pool(void *data)
{
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), data);
	return 0;
}

/*! \brief Destructor function for publish client */
static void sip_outbound_publish_client_destroy(void *obj)
{
	struct ast_sip_outbound_publish_client *client = obj;
	struct sip_outbound_publish_message *message;

	/* You might be tempted to think "the publish client isn't being destroyed" but it actually is - just elsewhere */

	while ((message = AST_LIST_REMOVE_HEAD(&client->queue, entry))) {
		ast_free(message);
	}

	ao2_cleanup(client->datastores);
	ao2_cleanup(client->publish);
	ast_free(client->transport_name);

	if (client->pool) {
		ast_sip_push_task_synchronous(NULL, release_pjsip_pool, client->pool);
	}

	/* if unloading the module and all objects have been unpublished
	   send the signal to finish unloading */
	if (unloading.is_unloading) {
		ast_mutex_lock(&unloading.lock);
		if (--unloading.count == 0) {
			ast_cond_signal(&unloading.cond);
		}
		ast_mutex_unlock(&unloading.lock);
	}
}

static int explicit_publish_destroy(void *data)
{
	struct ast_sip_outbound_publish_client *client = data;

	if (client->client) {
		pjsip_publishc_destroy(client->client);
		ao2_ref(client, -1);
	}

	return 0;
}

/*! \brief Helper function which cancels and un-publishes a no longer used client */
static int cancel_and_unpublish(struct ast_sip_outbound_publish_client *client)
{
	struct ast_sip_event_publisher_handler *handler;
	SCOPED_AO2LOCK(lock, client);

	if (!client->started) {
		/* If the client was never started, there's nothing to unpublish, so just
		 * destroy the publication.
		 */
		ast_sip_push_task_synchronous(NULL, explicit_publish_destroy, client);
		return 0;
	}

	handler = find_publisher_handler_for_event_name(client->publish->event);
	if (handler) {
		handler->stop_publishing(client);
	}

	client->started = 0;
	if (ast_sip_push_task(NULL, cancel_refresh_timer_task, ao2_bump(client))) {
		ast_log(LOG_WARNING, "Could not stop refresh timer on outbound publish '%s'\n",
			ast_sorcery_object_get_id(client->publish));
		ao2_ref(client, -1);
	}

	/* If nothing is being sent right now send the unpublish - the destroy will happen in the subsequent callback */
	if (!client->sending) {
		if (ast_sip_push_task(NULL, send_unpublish_task, ao2_bump(client))) {
			ast_log(LOG_WARNING, "Could not send unpublish message on outbound publish '%s'\n",
				ast_sorcery_object_get_id(client->publish));
			ao2_ref(client, -1);
		}
	}
	client->destroy = 1;
	return 0;
}

/*! \brief Destructor function for publish state */
static void sip_outbound_publish_state_destroy(void *obj)
{
	struct ast_sip_outbound_publish_state *state = obj;

	cancel_and_unpublish(state->client);
	ao2_cleanup(state->client);

	ast_free(state->user);
	ast_free(state->id);
}

/*!
 * \internal
 * \brief Check if a publish can be reused
 *
 * This checks if the existing outbound state and publish's configuration
 * differs from a newly-applied outbound state and publish.
 *
 * \param existing The pre-existing state
 * \param applied The newly-created state
 */
static int can_reuse_publish(struct ast_sip_outbound_publish_state *existing,
			     struct ast_sip_outbound_publish_state *applied)
{
	struct ast_sip_outbound_publish_client *ec = existing->client;
	struct ast_sip_outbound_publish *ep = ec->publish;
	struct ast_sip_outbound_publish_client *ac = applied->client;
	struct ast_sip_outbound_publish *ap = ac->publish;
	int i;

	if (pjsip_uri_cmp(PJSIP_URI_IN_OTHER, ec->server_uri, ac->server_uri) != PJ_SUCCESS ||
	    pjsip_uri_cmp(PJSIP_URI_IN_OTHER, ec->to_uri, ac->to_uri) != PJ_SUCCESS ||
	    pjsip_uri_cmp(PJSIP_URI_IN_OTHER, ec->from_uri, ac->from_uri) != PJ_SUCCESS ||
	    strcmp(ep->outbound_proxy, ap->outbound_proxy) || strcmp(ep->event, ap->event) ||
	    AST_VECTOR_SIZE(&ep->outbound_auths) != AST_VECTOR_SIZE(&ap->outbound_auths)) {
		return 0;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&ep->outbound_auths); ++i) {
		if (strcmp(AST_VECTOR_GET(&ep->outbound_auths, i), AST_VECTOR_GET(&ap->outbound_auths, i))) {
			return 0;
		}
	}

	return 1;
}

static void sip_outbound_publish_callback(struct pjsip_publishc_cbparam *param);

/*! \brief Helper function that allocates a pjsip publish client and configures it */
static int sip_outbound_publish_client_alloc(void *data)
{
	struct ast_sip_outbound_publish_client *client = data;
	RAII_VAR(struct ast_sip_outbound_publish *, publish, NULL, ao2_cleanup);
	pjsip_publishc_opt opt = {
		.queue_request = PJ_FALSE,
	};
	char server_buf[128], to_buf[128], from_buf[128];
	pj_str_t event, server_uri, to_uri, from_uri;
	pj_status_t status;
	int size;

	if (client->client) {
		return 0;
	} else if (pjsip_publishc_create(ast_sip_get_pjsip_endpoint(), &opt, ao2_bump(client), sip_outbound_publish_callback,
		&client->client) != PJ_SUCCESS) {
		ao2_ref(client, -1);
		return -1;
	}

	publish = ao2_bump(client->publish);

	if (!ast_strlen_zero(publish->outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };

		pj_list_init(&route_set);

		if (!(route = pjsip_parse_hdr(pjsip_publishc_get_pool(client->client), &ROUTE_HNAME,
			(char*)publish->outbound_proxy, strlen(publish->outbound_proxy), NULL))) {
			pjsip_publishc_destroy(client->client);
			return -1;
		}
		pj_list_insert_nodes_before(&route_set, route);

		pjsip_publishc_set_route_set(client->client, &route_set);
	}

	if ((size = pjsip_uri_print(PJSIP_URI_IN_OTHER, client->server_uri,
				    server_buf, sizeof(server_buf) - 1)) <= 0) {
		return -1;
	}
	pj_strset(&server_uri, server_buf, size);

	if ((size = pjsip_uri_print(PJSIP_URI_IN_OTHER, client->to_uri,
				    to_buf, sizeof(to_buf) - 1)) <= 0) {
		return -1;
	}
	pj_strset(&to_uri, to_buf, size);

	if ((size = pjsip_uri_print(PJSIP_URI_IN_OTHER, client->from_uri,
				    from_buf, sizeof(from_buf) - 1)) <= 0) {
		return -1;
	}
	pj_strset(&from_uri, from_buf, size);

	pj_cstr(&event, publish->event);
	status = pjsip_publishc_init(client->client, &event, &server_uri,
				     &from_uri, &to_uri, publish->expiration);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Could not initialize outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		pjsip_publishc_destroy(client->client);
		return -1;
	}

	return 0;
}

/*! \brief Callback function for publish client responses */
static void sip_outbound_publish_callback(struct pjsip_publishc_cbparam *param)
{
	RAII_VAR(struct ast_sip_outbound_publish_client *, client, ao2_bump(param->token), ao2_cleanup);
	RAII_VAR(struct ast_sip_outbound_publish *, publish, ao2_bump(client->publish), ao2_cleanup);
	SCOPED_AO2LOCK(lock, client);
	pjsip_tx_data *tdata;

	if (client->destroy) {
		if (client->sending) {
			client->sending = NULL;

			if (!ast_sip_push_task(NULL, send_unpublish_task, ao2_bump(client))) {
				return;
			}
			ast_log(LOG_WARNING, "Could not send unpublish message on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
			ao2_ref(client, -1);
		}
		/* Once the destroy is called this callback will not get called any longer, so drop the client ref */
		pjsip_publishc_destroy(client->client);
		ao2_ref(client, -1);
		return;
	}

	if (param->code == 401 || param->code == 407) {
		pjsip_transaction *tsx = pjsip_rdata_get_tsx(param->rdata);

		if (!ast_sip_create_request_with_auth(&publish->outbound_auths,
				param->rdata, tsx->last_tx, &tdata)) {
			if (!ast_strlen_zero(client->transport_name)) {
				pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
				ast_sip_set_tpselector_from_transport_name(client->transport_name, &selector);
				pjsip_tx_data_set_transport(tdata, &selector);
			}
			pjsip_publishc_send(client->client, tdata);
		}
		client->auth_attempts++;

		if (client->auth_attempts == publish->max_auth_attempts) {
			pjsip_publishc_destroy(client->client);
			client->client = NULL;

			ast_log(LOG_ERROR, "Reached maximum number of PUBLISH authentication attempts on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));

			goto end;
		}
		return;
	}

	client->auth_attempts = 0;

	if (param->code == 412) {
		pjsip_publishc_destroy(client->client);
		client->client = NULL;

		if (sip_outbound_publish_client_alloc(client)) {
			ast_log(LOG_ERROR, "Failed to create a new outbound publish client for '%s' on 412 response\n",
				ast_sorcery_object_get_id(publish));
			goto end;
		}

		/* Setting this to NULL will cause a new PUBLISH to get created and sent for the same underlying body */
		client->sending = NULL;
	} else if (param->code == 423) {
		/* Update the expiration with the new expiration time if available */
		pjsip_expires_hdr *expires;

		expires = pjsip_msg_find_hdr(param->rdata->msg_info.msg, PJSIP_H_MIN_EXPIRES, NULL);
		if (!expires || !expires->ivalue) {
			ast_log(LOG_ERROR, "Received 423 response on outbound publish '%s' without a Min-Expires header\n",
				ast_sorcery_object_get_id(publish));
			pjsip_publishc_destroy(client->client);
			client->client = NULL;
			goto end;
		}

		pjsip_publishc_update_expires(client->client, expires->ivalue);
		client->sending = NULL;
	} else if (client->sending) {
		/* Remove the message currently being sent so that when the queue is serviced another will get sent */
		AST_LIST_REMOVE_HEAD(&client->queue, entry);
		ast_free(client->sending);
		client->sending = NULL;
		if (!param->rdata) {
			ast_log(LOG_NOTICE, "No response received for outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
		}
	}

	if (AST_LIST_EMPTY(&client->queue)) {
		schedule_publish_refresh(client, param->expiration);
	}

end:
	if (!client->client) {
		struct sip_outbound_publish_message *message;

		while ((message = AST_LIST_REMOVE_HEAD(&client->queue, entry))) {
			ast_free(message);
		}
	} else {
		if (ast_sip_push_task(NULL, sip_publish_client_service_queue, ao2_bump(client))) {
			ao2_ref(client, -1);
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

/*!
 * \internal
 * \brief Set the user, if given, on the uri
 *
 * \param uri the uri to set the user on
 * \param client the client object
 * \param user the user to set on the uri
 */
static pjsip_sip_uri *sip_outbound_publish_client_set_uri(const char *uri,
	struct ast_sip_outbound_publish_client *client, const char *user)
{
	pj_str_t tmp;
	pjsip_uri *parsed;
	pjsip_sip_uri *parsed_uri;

	pj_strdup2_with_null(client->pool, &tmp, uri);
	if (!(parsed = pjsip_parse_uri(client->pool, tmp.ptr, tmp.slen, 0))) {
		return NULL;
	}

	if (!(parsed_uri = pjsip_uri_get_uri(parsed))) {
		return NULL;
	}

	if (user) {
		pj_strdup2(client->pool, &parsed_uri->user, user);
	}

	return parsed_uri;
}

struct set_uris_data {
	struct ast_sip_outbound_publish *publish;
	struct ast_sip_outbound_publish_client *client;
	const char *user;
	int res;
};

static int sip_outbound_publish_client_set_uris(void *data)
{
	struct set_uris_data *uris_data = data;
	struct ast_sip_outbound_publish *publish = uris_data->publish;
	struct ast_sip_outbound_publish_client *client = uris_data->client;

	uris_data->res = -1;
	if (!client->pool) {
		client->pool = pjsip_endpt_create_pool(
			ast_sip_get_pjsip_endpoint(), "URI Validation", 256, 256);
		if (!client->pool) {
			ast_log(LOG_ERROR, "Could not create pool for URI validation on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
			return -1;
		}
	}

	client->server_uri = sip_outbound_publish_client_set_uri(
		publish->server_uri, client, uris_data->user);
	if (!client->server_uri) {
		ast_log(LOG_ERROR, "Invalid server URI '%s' specified on outbound publish '%s'\n",
			publish->server_uri, ast_sorcery_object_get_id(publish));
		return -1;
	}

	client->to_uri = ast_strlen_zero(publish->to_uri) ? client->server_uri :
		sip_outbound_publish_client_set_uri(publish->to_uri, client, uris_data->user);

	if (!client->to_uri) {
		ast_log(LOG_ERROR, "Invalid to URI '%s' specified on outbound publish '%s'\n",
			publish->to_uri, ast_sorcery_object_get_id(publish));
		return -1;
	}

	client->from_uri = ast_strlen_zero(publish->from_uri) ? client->server_uri :
		sip_outbound_publish_client_set_uri(publish->from_uri, client, uris_data->user);

	if (!client->from_uri) {
		ast_log(LOG_ERROR, "Invalid from URI '%s' specified on outbound publish '%s'\n",
			publish->from_uri, ast_sorcery_object_get_id(publish));
		return -1;
	}

	uris_data->res = 0;
	return 0;
}

/*! \brief Allocator function for publish client */
static struct ast_sip_outbound_publish_state *sip_outbound_publish_state_alloc(
	struct ast_sip_outbound_publish *publish, const char *user)
{
	const char *id = ast_sorcery_object_get_id(publish);
	struct ast_sip_outbound_publish_state *state =
		ao2_alloc(sizeof(*state), sip_outbound_publish_state_destroy);
	struct set_uris_data uris_data;

	if (!state) {
		return NULL;
	}

	state->client = ao2_alloc(sizeof(*state->client), sip_outbound_publish_client_destroy);
	if (!state->client) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->client->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!state->client->datastores) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->client->timer.user_data = state->client;
	state->client->timer.cb = sip_outbound_publish_timer_cb;
	state->client->transport_name = ast_strdup(publish->transport);
	state->client->publish = ao2_bump(publish);

	if (!(state->id = ast_strdup(id))) {
		ao2_ref(state, -1);
		return NULL;
	}

	if (user && !(state->user = ast_strdup(user))) {
		ao2_ref(state, -1);
		return NULL;
	}

	uris_data.publish = publish;
	uris_data.client = state->client;
	uris_data.user = user;
	if (ast_sip_push_task_synchronous(NULL, sip_outbound_publish_client_set_uris,
					  &uris_data) || uris_data.res) {
		ao2_ref(state, -1);
		return NULL;
	}

	return state;
}

static int apply_initialize_state(struct ast_sip_outbound_publish *publish,
				  struct ast_sip_outbound_publish_state *state)
{
	if (ast_sip_push_task_synchronous(NULL, sip_outbound_publish_client_alloc, state->client)) {
		ast_log(LOG_ERROR, "Unable to create client for outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		return -1;
	}

	return 0;
}

static struct ast_sip_outbound_publish_state *validate_and_create_state(
	struct ast_sip_outbound_publish *publish, const char *user)
{
	struct ast_sip_outbound_publish_state *state;

	if (ast_strlen_zero(publish->server_uri)) {
		ast_log(LOG_ERROR, "No server URI specified on outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		return NULL;
	}

	if (ast_strlen_zero(publish->event)) {
		ast_log(LOG_ERROR, "No event type specified for outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		return NULL;
	}

	state = sip_outbound_publish_state_alloc(publish, user);
	if (!state) {
		ast_log(LOG_ERROR, "Unable to create state for outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		return NULL;
	};

	return state;
}

static int apply_update_current(void *obj, void *arg, void *data, int flags)
{
	RAII_VAR(struct ast_sip_outbound_publish_state *, new_state, NULL, ao2_cleanup);
	struct ast_sip_outbound_publish_state *state = obj;
	struct ast_sip_outbound_publish *old, *publish = arg;
	int *reuse = data;

	/* Process only those object with matching ids */
	if (strcmp(state->id, ast_sorcery_object_get_id(publish))) {
		return 0;
	}

	/*
	 * Don't maintain the old state object(s) if the multi_user option changed.
	 */
	if ((!publish->multi_user && state->client->publish->multi_user) ||
	    (publish->multi_user && !state->client->publish->multi_user)) {
		/*
		 * By returning non-zero here we'll be stopping iteration. However,
		 * that is okay since it's safe to assume any other objects in the
		 * container that could match fall under the above condition.
		 */
		return -1;
	}

	/*
	 * If we have more than one item in the container it is safe to assume each
	 * item pertains to a multi-user configuration state. That being the case if
	 * one item validates and can be reused then all items will validate and can
	 * be reused. Thus we can skip those checks for subsequent states.
	 */
	if (*reuse != 1) {
		if (!(new_state = validate_and_create_state(publish, state->user))) {
			/*
			 * The updated configuration had an error in it, so we want to
			 * keep the current state (i.e. add the current state object to
			 * the new_states container).
			 */
			return !ao2_link(new_states, state);
		}

		/*
		 * If we can't reuse the current state then initialize the
		 * new state and use that.
		 */
		if (!can_reuse_publish(state, new_state)) {
			*reuse = 0;
			if (apply_initialize_state(publish, new_state)) {
				/* If it fails to initialize keep the old */
				return !ao2_link(new_states, state);
			}
			return !ao2_link(new_states, new_state);
		}
	}

	*reuse = 1;

	/*
	 * If we can reuse the current state then keep it, but swap out
	 * the underlying publish object with the new one.
	 */
	old = state->client->publish;
	state->client->publish = publish;
	if (apply_initialize_state(publish, state)) {
		/*
		 * If the state object fails to [re]initialize then
		 * swap the old publish info back in and add keep the
		 * current state object around.
		 */
		state->client->publish = publish;
	} else {
		/*
		 * Since we swapped out the publish object the new one
		 * needs a ref while the old one needs to go away.
		 */
		ao2_ref(state->client->publish, +1);
		ao2_cleanup(old);
	}

	return !ao2_link(new_states, state);
}

/*! \brief Apply function which finds or allocates a state structure */
static int sip_outbound_publish_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ao2_container *states;
	struct ast_sip_outbound_publish_state *new_state = NULL;
	struct ast_sip_outbound_publish *applied = obj;
	int existing = -1;

	/*
	 * New states are being loaded or reloaded. We'll need to add the new
	 * object if created/updated, or keep the old object if an error occurs.
	 */
	if (!new_states) {
		new_states = ao2_container_alloc_options(
			AO2_ALLOC_OPT_LOCK_NOLOCK, DEFAULT_STATE_BUCKETS,
			outbound_publish_state_hash, outbound_publish_state_cmp);

		if (!new_states) {
			ast_log(LOG_ERROR, "Unable to allocate new states container\n");
			return -1;
		}
	}

	if ((states = ao2_global_obj_ref(current_states))) {
		/*
		 * If state objects already exist that are associated with the object
		 * being applied then we'll be updating each of those objects.
		 */
		ao2_callback_data(states, OBJ_NODATA, apply_update_current,
				  applied, &existing);
		ao2_ref(states, -1);
	}

	if (applied->multi_user) {
		/*
		 * If a multi-user configuration then drop the old one if it exists and
		 * then add the new configuration. Note, nothing is added to the current
		 * states at this time as states are dynamically added later for multi-
		 * user configurations.
		 */
		ao2_find(multi_publishes, ast_sorcery_object_get_id(obj),
			 OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
		ao2_link(multi_publishes, applied);
		return 0;
	}

	/*
	 * If not multi-user and if it doesn't already exist then
	 * create and add it to new_states.
	 */
	if (existing <= 0 &&
	    (new_state = validate_and_create_state(applied, NULL)) &&
	    !apply_initialize_state(applied, new_state)) {
		ao2_link(new_states, new_state);
	}

	ao2_cleanup(new_state);
	return 0;
}

static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_outbound_publish *publish = obj;

	return ast_sip_auth_vector_init(&publish->outbound_auths, var->value);
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	multi_publishes = ao2_container_alloc(DEFAULT_MULTI_BUCKETS,
		outbound_publish_multi_hash, outbound_publish_multi_cmp);
	if (!multi_publishes) {
		ast_log(LOG_ERROR, "Unable to allocate multi-publishes container\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_config(ast_sip_get_sorcery(), "res_pjsip_outbound_publish");
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "outbound-publish", "config", "pjsip.conf,criteria=type=outbound-publish");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "outbound-publish", sip_outbound_publish_alloc, NULL,
		sip_outbound_publish_apply)) {
		ao2_ref(multi_publishes, -1);
		ast_log(LOG_ERROR, "Unable to register 'outbound-publish' type with sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "server_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, server_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "from_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, from_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "event", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, event));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "to_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, to_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, outbound_proxy));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "expiration", "3600", OPT_UINT_T, 0, FLDSET(struct ast_sip_outbound_publish, expiration));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "max_auth_attempts", "5", OPT_UINT_T, 0, FLDSET(struct ast_sip_outbound_publish, max_auth_attempts));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "transport", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_outbound_publish, transport));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "outbound-publish", "outbound_auth", "", outbound_auth_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "outbound-publish", "multi_user", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_outbound_publish, multi_user));

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
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 10,
		.tv_nsec = start.tv_usec * 1000
	};
	int res = 0;
	struct ao2_container *states = ao2_global_obj_ref(current_states);

	if (!states || !(unloading.count = ao2_container_count(states))) {
		return 0;
	}

	ao2_ref(states, -1);

	ast_mutex_init(&unloading.lock);
	ast_cond_init(&unloading.cond, NULL);
	ast_mutex_lock(&unloading.lock);

	unloading.is_unloading = 1;
	ao2_ref(multi_publishes, -1);
	ao2_global_obj_release(current_states);

	/* wait for items to unpublish */
	ast_verb(5, "Waiting to complete unpublishing task(s)\n");
	while (unloading.count && !res) {
		res = ast_cond_timedwait(&unloading.cond, &unloading.lock, &end);
	}
	ast_mutex_unlock(&unloading.lock);

	ast_mutex_destroy(&unloading.lock);
	ast_cond_destroy(&unloading.cond);

	if (res) {
		ast_verb(5, "At least %d items were unable to unpublish "
			"in the allowed time\n", unloading.count);
	} else {
		ast_verb(5, "All items successfully unpublished\n");
		ast_sorcery_object_unregister(ast_sip_get_sorcery(), "outbound-publish");
	}

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Outbound Publish Support",
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
