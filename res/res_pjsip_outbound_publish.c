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
	<depend>res_pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>

#include "asterisk/res_pjproject.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_outbound_publish.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadpool.h"
#include "asterisk/datastore.h"
#include "res_pjsip/include/res_pjsip_private.h"

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
					<synopsis>Authentication object(s) to be used for outbound publishes.</synopsis>
					<description><para>
						This is a comma-delimited list of <replaceable>auth</replaceable>
						sections defined in <filename>pjsip.conf</filename> used to respond
						to outbound authentication challenges.</para>
						<note><para>
						Using the same auth section for inbound and outbound
						authentication is not recommended.  There is a difference in
						meaning for an empty realm setting between inbound and outbound
						authentication uses.  See the auth realm description for details.
						</para></note>
					</description>
				</configOption>
				<configOption name="outbound_proxy" default="">
					<synopsis>Full SIP URI of the outbound proxy used to send publishes</synopsis>
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

static int pjsip_max_url_size = PJSIP_MAX_URL_SIZE;

/*! \brief Queued outbound publish message */
struct sip_outbound_publish_message {
	/*! \brief Optional body */
	struct ast_sip_body body;
	/*! \brief Linked list information */
	AST_LIST_ENTRY(sip_outbound_publish_message) entry;
	/*! \brief Extra space for body contents */
	char body_contents[0];
};

/*
 * A note about some of the object types used in this module:
 *
 * The reason we currently have 4 separate object types that relate to configuration,
 * publishing, state, and client information is due to object lifetimes and order of
 * destruction dependencies.
 *
 * Separation of concerns is a good thing and of course it makes sense to have a
 * configuration object type as well as an object type wrapper around pjsip's publishing
 * client class. There also may be run time state data that needs to be tracked, so
 * again having something to handle that is prudent. However, it may be tempting to think
 * "why not combine the state and client object types?" Especially seeing as how they have
 * a one-to-one relationship. The answer is, it's possible, but it'd make the code a bit
 * more awkward.
 *
 * Currently this module maintains a global container of current state objects. When this
 * states container is replaced, or deleted, it un-references all contained objects. Any
 * state with a reference left have probably been carried over from a reload/realtime fetch.
 * States not carried over are destructed and the associated client (and all its publishers)
 * get unpublished.
 *
 * This "unpublishing" goes through a careful process of unpublishing the client, all its
 * publishers, and making sure all the appropriate references are removed in a sane order.
 * This process is essentially kicked off with the destruction of the state. If the state
 * and client objects were to be merged, where clients became the globally tracked object
 * type, this "unpublishing" process would never start because of the multiple references
 * held to the client object over it's lifetime. Meaning the global tracking container
 * would remove its reference to the client object when done with it, but other sources
 * would still be holding a reference to it (namely the datastore and publisher(s)).
 *
 * Thus at this time it is easier to keep them separate.
 */

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

struct sip_outbound_publisher {
	/*! \brief The client object that 'owns' this client

	     \note any potential circular reference problems are accounted
	     for (see publisher alloc for more information)
	*/
	struct ast_sip_outbound_publish_client *owner;
	/*! \brief Underlying publish client */
	pjsip_publishc *client;
	/*! \brief The From URI for this specific publisher */
	char *from_uri;
	/*! \brief The To URI for this specific publisher */
	char *to_uri;
	/*! \brief Timer entry for refreshing publish */
	pj_timer_entry timer;
	/*! \brief The number of auth attempts done */
	unsigned int auth_attempts;
	/*! \brief Queue of outgoing publish messages to send*/
	AST_LIST_HEAD_NOLOCK(, sip_outbound_publish_message) queue;
	/*! \brief The message currently being sent */
	struct sip_outbound_publish_message *sending;
	/*! \brief Publish client should be destroyed */
	unsigned int destroy;
	/*! \brief Serializer for stuff and things */
	struct ast_taskprocessor *serializer;
	/*! \brief User, if any, associated with the publisher */
	char user[0];
};

/*! \brief Outbound publish client state information (persists for lifetime of a publish) */
struct ast_sip_outbound_publish_client {
	/*! \brief Outbound publish information */
	struct ast_sip_outbound_publish *publish;
	/*! \brief Publisher datastores set up by handlers */
	struct ao2_container *datastores;
	/*! \brief Container of all the client publishing objects */
	struct ao2_container *publishers;
	/*! \brief Publishing has been fully started and event type informed */
	unsigned int started;
};

/*! \brief Outbound publish state information (persists for lifetime of a publish) */
struct ast_sip_outbound_publish_state {
	/*! \brief Outbound publish client */
	struct ast_sip_outbound_publish_client *client;
	/* publish state id lookup key - same as publish configuration id */
	char id[0];
};

/*!
 * \brief Used for locking while loading/reloading
 *
 * Mutli-user configurations make it so publishers can be dynamically added and
 * removed. Publishers should not be added or removed during a [re]load since
 * it could cause the current_clients container to be out of sync. Thus the
 * reason for this lock.
 */
AST_RWLOCK_DEFINE_STATIC(load_lock);

#define DEFAULT_PUBLISHER_BUCKETS 119
AO2_STRING_FIELD_HASH_FN(sip_outbound_publisher, user);
AO2_STRING_FIELD_CMP_FN(sip_outbound_publisher, user);

/*! Time needs to be long enough for a transaction to timeout if nothing replies. */
#define MAX_UNLOAD_TIMEOUT_TIME		35	/* Seconds */

/*! Shutdown group to monitor sip_outbound_registration_client_state serializers. */
static struct ast_serializer_shutdown_group *shutdown_group;

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

/*! \brief comparator function for client objects */
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

/*! \brief Helper function which cancels the refresh timer on a publisher */
static void cancel_publish_refresh(struct sip_outbound_publisher *publisher)
{
	if (pj_timer_heap_cancel_if_active(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()),
		&publisher->timer, 0)) {
		/* The timer was successfully cancelled, drop the refcount of the publisher */
		ao2_ref(publisher, -1);
	}
}

/*! \brief Helper function which sets up the timer to send publication */
static void schedule_publish_refresh(struct sip_outbound_publisher *publisher, int expiration)
{
	struct ast_sip_outbound_publish *publish = ao2_bump(publisher->owner->publish);
	pj_time_val delay = { .sec = 0, };

	cancel_publish_refresh(publisher);

	if (expiration > 0) {
		delay.sec = expiration - PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH;
	}
	if (publish->expiration && ((delay.sec > publish->expiration) || !delay.sec)) {
		delay.sec = publish->expiration;
	}
	if (delay.sec < PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH) {
		delay.sec = PJSIP_PUBLISHC_DELAY_BEFORE_REFRESH;
	}

	ao2_ref(publisher, +1);
	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &publisher->timer, &delay) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to pass timed publish refresh to scheduler\n");
		ao2_ref(publisher, -1);
	}
	ao2_ref(publish, -1);
}

static int publisher_client_send(void *obj, void *arg, void *data, int flags);

/*! \brief Publish client timer callback function */
static void sip_outbound_publish_timer_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	struct sip_outbound_publisher *publisher = entry->user_data;

	ao2_lock(publisher);
	if (AST_LIST_EMPTY(&publisher->queue)) {
		int res;
		/* If there are no outstanding messages send an empty PUBLISH message so our publication doesn't expire */
		publisher_client_send(publisher, NULL, &res, 0);
	}
	ao2_unlock(publisher);

	ao2_ref(publisher, -1);
}

/*! \brief Task for cancelling a refresh timer */
static int cancel_refresh_timer_task(void *data)
{
	struct sip_outbound_publisher *publisher = data;

	cancel_publish_refresh(publisher);
	ao2_ref(publisher, -1);

	return 0;
}

static void set_transport(struct sip_outbound_publisher *publisher, pjsip_tx_data *tdata)
{
	if (!ast_strlen_zero(publisher->owner->publish->transport)) {
		pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
		ast_sip_set_tpselector_from_transport_name(
			publisher->owner->publish->transport, &selector);
		pjsip_tx_data_set_transport(tdata, &selector);
	}
}

/*! \brief Task for sending an unpublish */
static int send_unpublish_task(void *data)
{
	struct sip_outbound_publisher *publisher = data;
	pjsip_tx_data *tdata;

	if (pjsip_publishc_unpublish(publisher->client, &tdata) == PJ_SUCCESS) {
		set_transport(publisher, tdata);
		pjsip_publishc_send(publisher->client, tdata);
	}

	ao2_ref(publisher, -1);

	return 0;
}

static void stop_publishing(struct ast_sip_outbound_publish_client *client,
			    struct ast_sip_event_publisher_handler *handler)
{
	if (!handler) {
		handler = find_publisher_handler_for_event_name(client->publish->event);
	}

	if (handler) {
		handler->stop_publishing(client);
	}
}

static int cancel_and_unpublish(void *obj, void *arg, int flags);

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
			stop_publishing(state->client, removed);
			ao2_callback(state->client->publishers, OBJ_NODATA, cancel_and_unpublish, NULL);
			state->client->started = 0;
		}
		ao2_ref(publish, -1);
		ao2_ref(state, -1);
	}
	ao2_iterator_destroy(&i);
	ao2_ref(states, -1);
}

static struct ast_sip_outbound_publish_state *sip_publish_state_get(const char *id)
{
	struct ao2_container *states = ao2_global_obj_ref(current_states);
	struct ast_sip_outbound_publish_state *res;

	if (!states) {
		return NULL;
	}

	res = ao2_find(states, id, OBJ_SEARCH_KEY);
	ao2_ref(states, -1);
	return res;
}

struct ast_sip_outbound_publish_client *ast_sip_publish_client_get(const char *name)
{
	struct ast_sip_outbound_publish_state *state = sip_publish_state_get(name);

	if (!state) {
		return NULL;
	}

	ao2_ref(state->client, +1);
	ao2_ref(state, -1);
	return state->client;
}

const char *ast_sip_publish_client_get_from_uri(struct ast_sip_outbound_publish_client *client)
{
	struct ast_sip_outbound_publish *publish = client->publish;

	return S_OR(publish->from_uri, S_OR(publish->server_uri, ""));
}

static struct sip_outbound_publisher *sip_outbound_publish_client_add_publisher(
        struct ast_sip_outbound_publish_client *client, const char *user);

static struct sip_outbound_publisher *sip_outbound_publish_client_get_publisher(
	struct ast_sip_outbound_publish_client *client, const char *user)
{
	struct sip_outbound_publisher *publisher;

	/*
	 * Lock before searching since there could be a race between searching and adding.
	 * Just use the load_lock since we might need to lock it anyway (if adding) and
	 * also it simplifies the code (otherwise we'd have to lock the publishers, no-
	 * lock the search and pass a flag to 'add publisher to no-lock the potential link).
	 */
	ast_rwlock_wrlock(&load_lock);
	publisher = ao2_find(client->publishers, user, OBJ_SEARCH_KEY);
	if (!publisher) {
		if (!(publisher = sip_outbound_publish_client_add_publisher(client, user))) {
			ast_rwlock_unlock(&load_lock);
			return NULL;
		}
	}
	ast_rwlock_unlock(&load_lock);

	return publisher;
}

const char *ast_sip_publish_client_get_user_from_uri(struct ast_sip_outbound_publish_client *client, const char *user,
	char *uri, size_t size)
{
	struct sip_outbound_publisher *publisher;

	publisher = sip_outbound_publish_client_get_publisher(client, user);
	if (!publisher) {
		return NULL;
	}

	ast_copy_string(uri, publisher->from_uri, size);
	ao2_ref(publisher, -1);

	return uri;
}

const char *ast_sip_publish_client_get_to_uri(struct ast_sip_outbound_publish_client *client)
{
	struct ast_sip_outbound_publish *publish = client->publish;

	return S_OR(publish->to_uri, S_OR(publish->server_uri, ""));
}

const char *ast_sip_publish_client_get_user_to_uri(struct ast_sip_outbound_publish_client *client, const char *user,
	char *uri, size_t size)
{
	struct sip_outbound_publisher *publisher;

	publisher = sip_outbound_publish_client_get_publisher(client, user);
	if (!publisher) {
		return NULL;
	}

	ast_copy_string(uri, publisher->to_uri, size);
	ao2_ref(publisher, -1);

	return uri;
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

static int sip_publisher_service_queue(void *data)
{
	RAII_VAR(struct sip_outbound_publisher *, publisher, data, ao2_cleanup);
	SCOPED_AO2LOCK(lock, publisher);
	struct sip_outbound_publish_message *message;
	pjsip_tx_data *tdata;
	pj_status_t status;

	if (publisher->destroy || publisher->sending || !(message = AST_LIST_FIRST(&publisher->queue))) {
		return 0;
	}

	if (pjsip_publishc_publish(publisher->client, PJ_FALSE, &tdata) != PJ_SUCCESS) {
		goto fatal;
	}

	if (!ast_strlen_zero(message->body.type) && !ast_strlen_zero(message->body.subtype) &&
		ast_sip_add_body(tdata, &message->body)) {
		pjsip_tx_data_dec_ref(tdata);
		goto fatal;
	}

	set_transport(publisher, tdata);

	status = pjsip_publishc_send(publisher->client, tdata);
	if (status == PJ_EBUSY) {
		/* We attempted to send the message but something else got there first */
		goto service;
	} else if (status != PJ_SUCCESS) {
		goto fatal;
	}

	publisher->sending = message;

	return 0;

fatal:
	AST_LIST_REMOVE_HEAD(&publisher->queue, entry);
	ast_free(message);

service:
	if (ast_sip_push_task(publisher->serializer, sip_publisher_service_queue, ao2_bump(publisher))) {
		ao2_ref(publisher, -1);
	}
	return -1;
}

static int publisher_client_send(void *obj, void *arg, void *data, int flags)
{
	struct sip_outbound_publisher *publisher = obj;
	const struct ast_sip_body *body = arg;
	struct sip_outbound_publish_message *message;
	size_t type_len = 0, subtype_len = 0, body_text_len = 0;
	int *res = data;
	SCOPED_AO2LOCK(lock, publisher);

	*res = -1;
	if (!publisher->client) {
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

	AST_LIST_INSERT_TAIL(&publisher->queue, message, entry);

	*res = ast_sip_push_task(publisher->serializer, sip_publisher_service_queue, ao2_bump(publisher));
	if (*res) {
		ao2_ref(publisher, -1);
	}

	return *res;
}

int ast_sip_publish_client_send(struct ast_sip_outbound_publish_client *client,
	const struct ast_sip_body *body)
{
	SCOPED_AO2LOCK(lock, client);
	int res = 0;

	ao2_callback_data(client->publishers, OBJ_NODATA,
			  publisher_client_send, (void *)body, &res);
	return res;
}

static int sip_outbound_publisher_set_uri(
	pj_pool_t *pool, const char *uri, const char *user, pj_str_t *res_uri)
{
	pj_str_t tmp;
	pjsip_uri *parsed;
	pjsip_sip_uri *parsed_uri;
	int size;

	pj_strdup2_with_null(pool, &tmp, uri);
	if (!(parsed = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0))) {
		return -1;
	}

	if (!(parsed_uri = pjsip_uri_get_uri(parsed))) {
		return -1;
	}

	if (!ast_strlen_zero(user)) {
		pj_strdup2(pool, &parsed_uri->user, user);
	}

	res_uri->ptr = (char*) pj_pool_alloc(pool, pjsip_max_url_size);
	if (!res_uri->ptr) {
		return -1;
	}

	if ((size = pjsip_uri_print(PJSIP_URI_IN_OTHER, parsed_uri, res_uri->ptr,
				    pjsip_max_url_size - 1)) <= 0) {
		return -1;
	}
	res_uri->ptr[size] = '\0';
	res_uri->slen = size;

	return 0;
}

static int sip_outbound_publisher_set_uris(
	pj_pool_t *pool, struct sip_outbound_publisher *publisher,
	pj_str_t *server_uri, pj_str_t *to_uri, pj_str_t *from_uri)
{
	struct ast_sip_outbound_publish *publish = publisher->owner->publish;

	if (sip_outbound_publisher_set_uri(pool, publish->server_uri, publisher->user, server_uri)) {
		ast_log(LOG_ERROR, "Invalid server URI '%s' specified on outbound publish '%s'\n",
			publish->server_uri, ast_sorcery_object_get_id(publish));
		return -1;
	}

	if (ast_strlen_zero(publish->to_uri)) {
		to_uri->ptr = server_uri->ptr;
		to_uri->slen = server_uri->slen;
	} else if (sip_outbound_publisher_set_uri(pool, publish->to_uri, publisher->user, to_uri)) {
		ast_log(LOG_ERROR, "Invalid to URI '%s' specified on outbound publish '%s'\n",
			publish->to_uri, ast_sorcery_object_get_id(publish));
		return -1;
	}

	publisher->to_uri = ast_strdup(to_uri->ptr);
	if (!publisher->to_uri) {
		return -1;
	}

	if (ast_strlen_zero(publish->from_uri)) {
		from_uri->ptr = server_uri->ptr;
		from_uri->slen = server_uri->slen;
	} else if (sip_outbound_publisher_set_uri(pool, publish->from_uri, publisher->user, from_uri)) {
		ast_log(LOG_ERROR, "Invalid from URI '%s' specified on outbound publish '%s'\n",
			publish->from_uri, ast_sorcery_object_get_id(publish));
		return -1;
	}

	publisher->from_uri = ast_strdup(from_uri->ptr);
	if (!publisher->from_uri) {
		return -1;
	}

	return 0;
}

static void sip_outbound_publish_callback(struct pjsip_publishc_cbparam *param);

/*! \brief Helper function that allocates a pjsip publish client and configures it */
static int sip_outbound_publisher_init(void *data)
{
	struct sip_outbound_publisher *publisher = data;
	RAII_VAR(struct ast_sip_outbound_publish *, publish, NULL, ao2_cleanup);
	pjsip_publishc_opt opt = {
		.queue_request = PJ_FALSE,
	};
	pj_pool_t *pool;
	pj_str_t event, server_uri, to_uri, from_uri;

	if (publisher->client) {
		return 0;
	}

	if (pjsip_publishc_create(ast_sip_get_pjsip_endpoint(), &opt,
				  ao2_bump(publisher), sip_outbound_publish_callback,
		&publisher->client) != PJ_SUCCESS) {
		ao2_ref(publisher, -1);
		return -1;
	}

	publish = ao2_bump(publisher->owner->publish);

	if (!ast_strlen_zero(publish->outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };

		pj_list_init(&route_set);

		if (!(route = pjsip_parse_hdr(pjsip_publishc_get_pool(publisher->client), &ROUTE_HNAME,
			(char*)publish->outbound_proxy, strlen(publish->outbound_proxy), NULL))) {
			pjsip_publishc_destroy(publisher->client);
			return -1;
		}
		pj_list_insert_nodes_before(&route_set, route);

		pjsip_publishc_set_route_set(publisher->client, &route_set);
	}

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "URI Validation",
				       pjsip_max_url_size, pjsip_max_url_size);
	if (!pool) {
		ast_log(LOG_ERROR, "Could not create pool for URI validation on outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		pjsip_publishc_destroy(publisher->client);
		return -1;
	}

	if (sip_outbound_publisher_set_uris(pool, publisher, &server_uri, &from_uri, &to_uri)) {
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		pjsip_publishc_destroy(publisher->client);
		return -1;
	}

	pj_cstr(&event, publish->event);
	if (pjsip_publishc_init(publisher->client, &event, &server_uri, &from_uri, &to_uri,
			publish->expiration) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to initialize publishing client on outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		pjsip_publishc_destroy(publisher->client);
		return -1;
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return 0;
}

static int sip_outbound_publisher_reinit(void *obj, void *arg, int flags)
{
	return sip_outbound_publisher_init(obj);
}

static int sip_outbound_publisher_reinit_all(void *data)
{
	ao2_callback(data, OBJ_NODATA, sip_outbound_publisher_reinit, NULL);
	return 0;
}

/*! \brief Destructor function for publish client */
static void sip_outbound_publisher_destroy(void *obj)
{
	struct sip_outbound_publisher *publisher = obj;
	struct sip_outbound_publish_message *message;

	/* You might be tempted to think "the publish client isn't being destroyed" but it actually is - just elsewhere */

	while ((message = AST_LIST_REMOVE_HEAD(&publisher->queue, entry))) {
		ast_free(message);
	}

	ao2_cleanup(publisher->owner);
	ast_free(publisher->from_uri);
	ast_free(publisher->to_uri);

	ast_taskprocessor_unreference(publisher->serializer);
}

static struct sip_outbound_publisher *sip_outbound_publisher_alloc(
	struct ast_sip_outbound_publish_client *client, const char *user)
{
	struct sip_outbound_publisher *publisher;
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

	publisher = ao2_alloc(sizeof(*publisher) + (user ? strlen(user) : 0) + 1,
			      sip_outbound_publisher_destroy);
	if (!publisher) {
		return NULL;
	}

	/*
	 * Bump the ref to the client. This essentially creates a circular reference,
	 * but it is needed in order to make sure the client object doesn't get pulled
	 * out from under us when the publisher stops publishing.
	 *
	 * The circular reference is alleviated by calling cancel_and_unpublish for
	 * each client, from the state's destructor. By calling it there all references
	 * to the publishers should go to zero, thus calling the publisher's destructor.
	 * This in turn removes the client reference we added here. The state then removes
	 * its reference to the client, which should take it to zero.
	 */
	publisher->owner = ao2_bump(client);
	publisher->timer.user_data = publisher;
	publisher->timer.cb = sip_outbound_publish_timer_cb;
	if (user) {
		strcpy(publisher->user, user);
	} else {
		*publisher->user = '\0';
	}

	ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/outpub/%s",
		ast_sorcery_object_get_id(client->publish));

	publisher->serializer = ast_sip_create_serializer_group(tps_name,
		shutdown_group);
	if (!publisher->serializer) {
		ao2_ref(publisher, -1);
		return NULL;
	}

	if (ast_sip_push_task_wait_servant(NULL, sip_outbound_publisher_init, publisher)) {
		ast_log(LOG_ERROR, "Unable to create publisher for outbound publish '%s'\n",
			ast_sorcery_object_get_id(client->publish));
		ao2_ref(publisher, -1);
		return NULL;
	}

	return publisher;
}

static struct sip_outbound_publisher *sip_outbound_publish_client_add_publisher(
	struct ast_sip_outbound_publish_client *client, const char *user)
{
	struct sip_outbound_publisher *publisher =
		sip_outbound_publisher_alloc(client, user);

	if (!publisher) {
		return NULL;
	}

	if (!ao2_link(client->publishers, publisher)) {
		/*
		 * No need to bump the reference here. The task will take care of
		 * removing the reference.
		 */
		if (ast_sip_push_task(publisher->serializer, cancel_refresh_timer_task, publisher)) {
			ao2_ref(publisher, -1);
		}
		return NULL;
	}

	return publisher;
}

int ast_sip_publish_client_user_send(struct ast_sip_outbound_publish_client *client,
				     const char *user, const struct ast_sip_body *body)
{
	struct sip_outbound_publisher *publisher;
	int res;

	publisher = sip_outbound_publish_client_get_publisher(client, user);
	if (!publisher) {
		return -1;
	}

	publisher_client_send(publisher, (void *)body, &res, 0);
	ao2_ref(publisher, -1);
	return res;
}

void ast_sip_publish_client_remove(struct ast_sip_outbound_publish_client *client,
				   const char *user)
{
	SCOPED_WRLOCK(lock, &load_lock);
	ao2_find(client->publishers, user, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
}

static int explicit_publish_destroy(void *data)
{
	struct sip_outbound_publisher *publisher = data;

	/*
	 * If there is no pjsip publishing client then we obviously don't need
	 * to destroy it. Also, the ref for the Asterisk publishing client that
	 * pjsip had would not exist or should already be gone as well.
	 */
	if (publisher->client) {
		pjsip_publishc_destroy(publisher->client);
		ao2_ref(publisher, -1);
	}

	ao2_ref(publisher, -1);

	return 0;
}

/*! \brief Helper function which cancels and un-publishes a no longer used client */
static int cancel_and_unpublish(void *obj, void *arg, int flags)
{
	struct sip_outbound_publisher *publisher = obj;
	struct ast_sip_outbound_publish_client *client = publisher->owner;

	SCOPED_AO2LOCK(lock, publisher);

	if (!client->started) {
		/* If the publisher was never started, there's nothing to unpublish, so just
		 * destroy the publication and remove its reference to the publisher.
		 */
		if (ast_sip_push_task(publisher->serializer, explicit_publish_destroy, ao2_bump(publisher))) {
			ao2_ref(publisher, -1);
		}
		return 0;
	}

	if (ast_sip_push_task(publisher->serializer, cancel_refresh_timer_task, ao2_bump(publisher))) {
		ast_log(LOG_WARNING, "Could not stop refresh timer on outbound publish '%s'\n",
			ast_sorcery_object_get_id(client->publish));
		ao2_ref(publisher, -1);
	}

	/* If nothing is being sent right now send the unpublish - the destroy will happen in the subsequent callback */
	if (!publisher->sending) {
		if (ast_sip_push_task(publisher->serializer, send_unpublish_task, ao2_bump(publisher))) {
			ast_log(LOG_WARNING, "Could not send unpublish message on outbound publish '%s'\n",
				ast_sorcery_object_get_id(client->publish));
			ao2_ref(publisher, -1);
		}
	}
	publisher->destroy = 1;
	return 0;
}

/*! \brief Destructor function for publish client */
static void sip_outbound_publish_client_destroy(void *obj)
{
	struct ast_sip_outbound_publish_client *client = obj;

	ao2_cleanup(client->datastores);

	/*
	 * The client's publishers have already been unpublished and destroyed
	 * by this point, so it is safe to finally remove the reference to the
	 * publish object. The client needed to hold a reference to it until
	 * the publishers were done with it.
	 */
	ao2_cleanup(client->publish);
}

/*! \brief Destructor function for publish state */
static void sip_outbound_publish_state_destroy(void *obj)
{
	struct ast_sip_outbound_publish_state *state = obj;

	stop_publishing(state->client, NULL);
	/*
	 * Since the state is being destroyed the associated client needs to also
	 * be destroyed. However simply removing the reference to the client will
	 * not initiate client destruction since the client's publisher(s) hold a
	 * reference to the client object as well. So we need to unpublish the
	 * the client's publishers here, which will remove the publisher's client
	 * reference during that process.
	 *
	 * That being said we don't want to remove the client's reference to the
	 * publish object just yet. We'll hold off on that until client destruction
	 * itself. This is because the publishers need access to the client's
	 * publish object while they are unpublishing.
	 */
	ao2_callback(state->client->publishers, OBJ_NODATA | OBJ_UNLINK, cancel_and_unpublish, NULL);
	ao2_cleanup(state->client->publishers);

	state->client->started = 0;
	ao2_cleanup(state->client);
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

/*! \brief Callback function for publish client responses */
static void sip_outbound_publish_callback(struct pjsip_publishc_cbparam *param)
{
#define DESTROY_CLIENT() do {			   \
	pjsip_publishc_destroy(publisher->client); \
	publisher->client = NULL; \
	ao2_ref(publisher, -1); } while (0)

	RAII_VAR(struct sip_outbound_publisher *, publisher, ao2_bump(param->token), ao2_cleanup);
	RAII_VAR(struct ast_sip_outbound_publish *, publish, ao2_bump(publisher->owner->publish), ao2_cleanup);
	SCOPED_AO2LOCK(lock, publisher);
	pjsip_tx_data *tdata;

	if (publisher->destroy) {
		if (publisher->sending) {
			publisher->sending = NULL;

			if (!ast_sip_push_task(publisher->serializer, send_unpublish_task, ao2_bump(publisher))) {
				return;
			}
			ast_log(LOG_WARNING, "Could not send unpublish message on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
			ao2_ref(publisher, -1);
		}
		/* Once the destroy is called this callback will not get called any longer, so drop the publisher ref */
		DESTROY_CLIENT();
		return;
	}

	if (param->code == 401 || param->code == 407) {
		pjsip_transaction *tsx = pjsip_rdata_get_tsx(param->rdata);

		if (!ast_sip_create_request_with_auth(&publish->outbound_auths,
				param->rdata, tsx->last_tx, &tdata)) {
			set_transport(publisher, tdata);
			pjsip_publishc_send(publisher->client, tdata);
		}
		publisher->auth_attempts++;

		if (publisher->auth_attempts == publish->max_auth_attempts) {
			DESTROY_CLIENT();
			ast_log(LOG_ERROR, "Reached maximum number of PUBLISH authentication attempts on outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));

			goto end;
		}
		return;
	}

	publisher->auth_attempts = 0;

	if (param->code == 412) {
		DESTROY_CLIENT();
		if (sip_outbound_publisher_init(publisher)) {
			ast_log(LOG_ERROR, "Failed to create a new outbound publish client for '%s' on 412 response\n",
				ast_sorcery_object_get_id(publish));
			goto end;
		}

		/* Setting this to NULL will cause a new PUBLISH to get created and sent for the same underlying body */
		publisher->sending = NULL;
	} else if (param->code == 423) {
		/* Update the expiration with the new expiration time if available */
		pjsip_expires_hdr *expires;

		expires = pjsip_msg_find_hdr(param->rdata->msg_info.msg, PJSIP_H_MIN_EXPIRES, NULL);
		if (!expires || !expires->ivalue) {
			DESTROY_CLIENT();
			ast_log(LOG_ERROR, "Received 423 response on outbound publish '%s' without a Min-Expires header\n",
				ast_sorcery_object_get_id(publish));
			goto end;
		}

		pjsip_publishc_update_expires(publisher->client, expires->ivalue);
		publisher->sending = NULL;
	} else if (publisher->sending) {
		/* Remove the message currently being sent so that when the queue is serviced another will get sent */
		AST_LIST_REMOVE_HEAD(&publisher->queue, entry);
		ast_free(publisher->sending);
		publisher->sending = NULL;
		if (!param->rdata) {
			ast_log(LOG_NOTICE, "No response received for outbound publish '%s'\n",
				ast_sorcery_object_get_id(publish));
		}
	}

	if (AST_LIST_EMPTY(&publisher->queue)) {
		schedule_publish_refresh(publisher, param->expiration);
	}

end:
	if (!publisher->client) {
		struct sip_outbound_publish_message *message;

		while ((message = AST_LIST_REMOVE_HEAD(&publisher->queue, entry))) {
			ast_free(message);
		}
	} else {
		if (ast_sip_push_task(publisher->serializer, sip_publisher_service_queue, ao2_bump(publisher))) {
			ao2_ref(publisher, -1);
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

/*! \brief Allocator function for publish client */
static struct ast_sip_outbound_publish_state *sip_outbound_publish_state_alloc(
	struct ast_sip_outbound_publish *publish)
{
	const char *id = ast_sorcery_object_get_id(publish);
	struct ast_sip_outbound_publish_state *state =
		ao2_alloc(sizeof(*state) + strlen(id) + 1, sip_outbound_publish_state_destroy);

	if (!state) {
		return NULL;
	}

	state->client = ao2_alloc(sizeof(*state->client), sip_outbound_publish_client_destroy);
	if (!state->client) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->client->datastores = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DATASTORE_BUCKETS, datastore_hash, NULL, datastore_cmp);
	if (!state->client->datastores) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->client->publishers = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DATASTORE_BUCKETS,
		sip_outbound_publisher_hash_fn, NULL, sip_outbound_publisher_cmp_fn);
	if (!state->client->publishers) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->client->publish = ao2_bump(publish);

	strcpy(state->id, id);
	return state;
}

static int validate_publish_config(struct ast_sip_outbound_publish *publish)
{
	if (ast_strlen_zero(publish->server_uri)) {
		ast_log(LOG_ERROR, "No server URI specified on outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		return -1;
	} else if (ast_sip_validate_uri_length(publish->server_uri)) {
		ast_log(LOG_ERROR, "Server URI or hostname length exceeds pjproject limit or is not a sip(s) uri: '%s' on outbound publish '%s'\n",
			publish->server_uri,
			ast_sorcery_object_get_id(publish));
		return -1;
	} else if (ast_strlen_zero(publish->event)) {
		ast_log(LOG_ERROR, "No event type specified for outbound publish '%s'\n",
			ast_sorcery_object_get_id(publish));
		return -1;
	} else if (!ast_strlen_zero(publish->from_uri)
		&& ast_sip_validate_uri_length(publish->from_uri)) {
		ast_log(LOG_ERROR, "From URI or hostname length exceeds pjproject limit or is not a sip(s) uri: '%s' on outbound publish '%s'\n",
			publish->from_uri,
			ast_sorcery_object_get_id(publish));
		return -1;
	} else if (!ast_strlen_zero(publish->to_uri)
		&& ast_sip_validate_uri_length(publish->to_uri)) {
		ast_log(LOG_ERROR, "To URI or hostname length exceeds pjproject limit or is not a sip(s) uri: '%s' on outbound publish '%s'\n",
			publish->to_uri,
			ast_sorcery_object_get_id(publish));
		return -1;
	}
	return 0;
}

static int current_state_reusable(struct ast_sip_outbound_publish *publish,
				  struct ast_sip_outbound_publish_state *current_state)
{
	struct ast_sip_outbound_publish *old_publish;

	/*
	 * Don't maintain the old state/client objects if the multi_user option changed.
	 */
	if ((!publish->multi_user && current_state->client->publish->multi_user) ||
	    (publish->multi_user && !current_state->client->publish->multi_user)) {
		return 0;
	}


	if (!can_reuse_publish(current_state->client->publish, publish)) {
		/*
		 * Something significant has changed in the configuration, so we are
		 * unable to use the old state object. The current state needs to go
		 * away and a new one needs to be created.
		 */
		return 0;
	}

	/*
	 * We can reuse the current state object so keep it, but swap out the
	 * underlying publish object with the new one.
	 */
	old_publish = current_state->client->publish;
	current_state->client->publish = publish;
	if (ast_sip_push_task_wait_servant(NULL, sip_outbound_publisher_reinit_all,
		current_state->client->publishers)) {
		/*
		 * If the state object fails to re-initialize then swap
		 * the old publish info back in.
		 */
		current_state->client->publish = publish;
		ast_log(LOG_ERROR, "Unable to reinitialize client(s) for outbound publish '%s'\n",
			ast_sorcery_object_get_id(current_state->client->publish));
		return -1;
	}

	/*
	 * Since we swapped out the publish object the new one needs a ref
	 * while the old one needs to go away.
	 */
	ao2_ref(current_state->client->publish, +1);
	ao2_cleanup(old_publish);

	/* Tell the caller that the current state object should be used */
	return 1;
}

/*! \brief Apply function which finds or allocates a state structure */
static int sip_outbound_publish_apply(const struct ast_sorcery *sorcery, void *obj)
{
#define ADD_TO_NEW_STATES(__obj) \
	do { if (__obj) { \
	     ao2_link(new_states, __obj); \
	     ao2_ref(__obj, -1); } } while (0)

	struct ast_sip_outbound_publish *applied = obj;
	struct ast_sip_outbound_publish_state *current_state, *new_state;
	struct sip_outbound_publisher *publisher = NULL;
	int res;

	/*
	 * New states are being loaded or reloaded. We'll need to add the new
	 * object if created/updated, or keep the old object if an error occurs.
	 */
	if (!new_states) {
		new_states = ao2_container_alloc_hash(
			AO2_ALLOC_OPT_LOCK_NOLOCK, 0, DEFAULT_STATE_BUCKETS,
			outbound_publish_state_hash, NULL, outbound_publish_state_cmp);

		if (!new_states) {
			ast_log(LOG_ERROR, "Unable to allocate new states container\n");
			return -1;
		}
	}

	/* If there is current state we'll want to maintain it if any errors occur */
	current_state = sip_publish_state_get(ast_sorcery_object_get_id(applied));

	if ((res = validate_publish_config(applied))) {
		ADD_TO_NEW_STATES(current_state);
		return res;
	}

	if (current_state && (res = current_state_reusable(applied, current_state))) {
		/*
		 * The current state object was able to be reused, or an error
		 * occurred. Either way we keep the current state and be done.
		 */
		ADD_TO_NEW_STATES(current_state);
		return res == 1 ? 0 : -1;
	}

	/*
	 * No current state was found or it was unable to be reused. Either way
	 * we'll need to create a new state object.
	 */
	new_state = sip_outbound_publish_state_alloc(applied);
	if (!new_state) {
		ast_log(LOG_ERROR, "Unable to create state for outbound publish '%s'\n",
			ast_sorcery_object_get_id(applied));
		ADD_TO_NEW_STATES(current_state);
		return -1;
	};

	if (!applied->multi_user &&
	    !(publisher = sip_outbound_publish_client_add_publisher(new_state->client, NULL))) {
		ADD_TO_NEW_STATES(current_state);
		ao2_ref(new_state, -1);
		return -1;
	}
	ao2_cleanup(publisher);

	ADD_TO_NEW_STATES(new_state);
	ao2_cleanup(current_state);
	return res;
}

static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_outbound_publish *publish = obj;

	return ast_sip_auth_vector_init(&publish->outbound_auths, var->value);
}


static int unload_module(void)
{
	int remaining;

	ast_sorcery_object_unregister(ast_sip_get_sorcery(), "outbound-publish");

	ao2_global_obj_release(current_states);

	/* Wait for publication serializers to get destroyed. */
	ast_debug(2, "Waiting for publication to complete for unload.\n");
	remaining = ast_serializer_shutdown_group_join(shutdown_group, MAX_UNLOAD_TIMEOUT_TIME);
	if (remaining) {
		ast_log(LOG_WARNING, "Unload incomplete.  Could not stop %d outbound publications.  Try again later.\n",
			remaining);
		return -1;
	}

	ast_debug(2, "Successful shutdown.\n");

	ao2_cleanup(shutdown_group);
	shutdown_group = NULL;

	return 0;
}

static int load_module(void)
{
	/* As of pjproject 2.4.5, PJSIP_MAX_URL_SIZE isn't exposed yet but we try anyway. */
	ast_pjproject_get_buildopt("PJSIP_MAX_URL_SIZE", "%d", &pjsip_max_url_size);

	shutdown_group = ast_serializer_shutdown_group_alloc();
	if (!shutdown_group) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_config(ast_sip_get_sorcery(), "res_pjsip_outbound_publish");
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "outbound-publish", "config", "pjsip.conf,criteria=type=outbound-publish");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "outbound-publish", sip_outbound_publish_alloc, NULL,
		sip_outbound_publish_apply)) {
		ast_log(LOG_ERROR, "Unable to register 'outbound-publish' type with sorcery\n");
		unload_module();
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Outbound Publish Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_pjproject,res_pjsip",
);
