/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/stasis_state.h"

/*!
 * \internal
 * \brief Used to link a stasis_state to it's manager
 */
struct stasis_state_proxy {
	AO2_WEAKPROXY();
	/*! The manager that owns and handles this state */
	struct stasis_state_manager *manager;
	/*! A unique id for this state object. */
	char id[0];
};

/*!
 * \internal
 * \brief Associates a stasis topic to its last known published message
 *
 * This object's lifetime is tracked by the number of publishers and subscribers to it.
 * Once all publishers and subscribers have been removed this object is removed from the
 * manager's collection and destroyed. While a single object type (namely this one) could
 * be utilized for both publishers, and subscribers this implementation purposely keeps
 * them separated. This was done to maintain readability, make debugging easier, and allow
 * for better logging and future enhancements.
 */
struct stasis_state {
	/*! The number of state subscribers */
	unsigned int num_subscribers;
	/*!
	 * \brief The manager that owns and handles this state
	 * \note This reference is owned by stasis_state_proxy
	 */
	struct stasis_state_manager *manager;
	/*! Forwarding information, i.e. this topic to manager's topic */
	struct stasis_forward *forward;
	/*! The managed topic */
	struct stasis_topic *topic;
	/*! The actual state data */
	struct stasis_message *msg;
	/*!
	 * A container of eids. It's assumed that there is only a single publisher per
	 * eid per topic. Thus the publisher is tracked by the system's eid.
	 */
	AST_VECTOR(, struct ast_eid) eids;
	/*! A unique id for this state object. */
	char *id;
};

AO2_STRING_FIELD_HASH_FN(stasis_state_proxy, id);
AO2_STRING_FIELD_CMP_FN(stasis_state_proxy, id);

/*! The number of buckets to use for managed states */
#define STATE_BUCKETS 57

struct stasis_state_manager {
	/*! Holds all state objects handled by this manager */
	struct ao2_container *states;
	/*! The manager's topic. All state topics are forward to this one */
	struct stasis_topic *all_topic;
	/*! A collection of manager event handlers */
	AST_VECTOR_RW(, struct stasis_state_observer *) observers;
};

/*!
 * \internal
 * \brief Retrieve a state's topic name without the manager topic.
 *
 * State topics have names that consist of the manager's topic name
 * combined with a unique id separated by a slash. For instance:
 *
 *    manager topic's name/unique id
 *
 * This method retrieves the unique id part from the state's topic name.
 *
 * \param manager_topic The manager's topic
 * \param state_topic A state topic
 *
 * \return The state's topic unique id
 */
static const char *state_id_by_topic(struct stasis_topic *manager_topic,
	const struct stasis_topic *state_topic)
{
	const char *id;

	/* This topic should always belong to the manager */
	ast_assert(ast_begins_with(stasis_topic_name(manager_topic),
		stasis_topic_name(state_topic)));

	id = strchr(stasis_topic_name(state_topic), '/');

	/* The state's unique id should always exist */
	ast_assert(id != NULL && *(id + 1) != '\0');

	return (id + 1);
}

static void state_dtor(void *obj)
{
	struct stasis_state *state = obj;

	state->forward = stasis_forward_cancel(state->forward);
	ao2_cleanup(state->topic);
	state->topic = NULL;
	ao2_cleanup(state->msg);
	state->msg = NULL;

	/* All eids should have been removed */
	ast_assert(AST_VECTOR_SIZE(&state->eids) == 0);
	AST_VECTOR_FREE(&state->eids);
}

static void state_proxy_dtor(void *obj) {
	struct stasis_state_proxy *proxy = obj;

	ao2_cleanup(proxy->manager);
}

static void state_proxy_sub_cb(void *obj, void *data)
{
	struct stasis_state_proxy *proxy = obj;

	ao2_unlink(proxy->manager->states, proxy);
}

/*!
 * \internal
 * \brief Allocate a stasis state object and add it to the manager.
 *
 * Create and initialize a state structure. It's required that either a state
 * topic, or an id is specified. If a state topic is not given then one will be
 * created using the given id.
 *
 * \param manager The owning manager
 * \param state_topic A state topic to be managed
 * \param id The unique id for the state
 * \param file, line, func
 *
 * \return A stasis_state object or NULL
 * \retval NULL on error
 *
 * \pre manager->states must be locked.
 * \pre manager->states does not contain an object matching key \a id.
 */
static struct stasis_state *state_alloc(struct stasis_state_manager *manager,
	struct stasis_topic *state_topic, const char *id,
	const char *file, int line, const char *func)
{
	struct stasis_state_proxy *proxy = NULL;
	struct stasis_state *state = NULL;

	if (!id) {
		/* If not given an id, then a state topic is required */
		ast_assert(state_topic != NULL);

		/* Get the id we'll key off of from the state topic */
		id = state_id_by_topic(manager->all_topic, state_topic);
	}

	state = __ao2_alloc(sizeof(*state), state_dtor, AO2_ALLOC_OPT_LOCK_MUTEX, id, file, line, func);
	if (!state) {
		goto error_return;
	}

	if (!state_topic) {
		char *name;

		/*
		 * To provide further detail and to ensure that the topic is unique within the
		 * scope of the system we prefix it with the manager's topic name, which should
		 * itself already be unique.
		 */
		if (ast_asprintf(&name, "%s/%s", stasis_topic_name(manager->all_topic), id) < 0) {
			goto error_return;
		}

		state->topic = stasis_topic_create(name);

		ast_free(name);
		if (!state->topic) {
			goto error_return;
		}
	} else {
		/*
		 * Since the state topic was passed in, go ahead and bump its reference.
		 * By doing this here first, it allows us to consistently decrease the reference on
		 * state allocation error.
		 */
		ao2_ref(state_topic, +1);
		state->topic = state_topic;
	}

	proxy = ao2_t_weakproxy_alloc(sizeof(*proxy) + strlen(id) + 1, state_proxy_dtor, id);
	if (!proxy) {
		goto error_return;
	}

	strcpy(proxy->id, id); /* Safe */

	state->id = proxy->id;
	proxy->manager = ao2_bump(manager);
	state->manager = proxy->manager; /* state->manager is owned by the proxy */

	state->forward = stasis_forward_all(state->topic, manager->all_topic);
	if (!state->forward) {
		goto error_return;
	}

	if (AST_VECTOR_INIT(&state->eids, 2)) {
		goto error_return;
	}

	if (ao2_t_weakproxy_set_object(proxy, state, OBJ_NOLOCK, "weakproxy link")) {
		goto error_return;
	}

	if (ao2_weakproxy_subscribe(proxy, state_proxy_sub_cb, NULL, OBJ_NOLOCK)) {
		goto error_return;
	}

	if (!ao2_link_flags(manager->states, proxy, OBJ_NOLOCK)) {
		goto error_return;
	}

	ao2_ref(proxy, -1);

	return state;

error_return:
	ast_log(LOG_ERROR, "Unable to allocate state '%s' in manager '%s'\n",
			id, stasis_topic_name(manager->all_topic));
	ao2_cleanup(state);
	ao2_cleanup(proxy);
	return NULL;
}

/*!
 * \internal
 * \brief Find a state by id, or create one if not found and add it to the manager.
 *
 * \param manager The manager to be added to
 * \param state_topic A state topic to be managed (if NULL id is required)
 * \param id The unique id for the state (if NULL state_topic is required)
 *
 * \return The added state object
 * \retval NULL on error
 */
#define state_find_or_add(manager, state_topic, id) __state_find_or_add(manager, state_topic, id, __FILE__, __LINE__, __PRETTY_FUNCTION__)
static struct stasis_state *__state_find_or_add(struct stasis_state_manager *manager,
	struct stasis_topic *state_topic, const char *id,
	const char *file, int line, const char *func)
{
	struct stasis_state *state;

	ao2_lock(manager->states);
	if (ast_strlen_zero(id)) {
		id = state_id_by_topic(manager->all_topic, state_topic);
	}

	state = ao2_weakproxy_find(manager->states, id, OBJ_SEARCH_KEY | OBJ_NOLOCK, "");
	if (!state) {
		state = state_alloc(manager, state_topic, id, file, line, func);
	}

	ao2_unlock(manager->states);

	return state;
}

static void state_manager_dtor(void *obj)
{
	struct stasis_state_manager *manager = obj;

#ifdef AO2_DEBUG
	{
		char *container_name =
			ast_alloca(strlen(stasis_topic_name(manager->all_topic)) + strlen("-manager") + 1);
		sprintf(container_name, "%s-manager", stasis_topic_name(manager->all_topic));
		ao2_container_unregister(container_name);
	}
#endif

	ao2_cleanup(manager->states);
	manager->states = NULL;
	ao2_cleanup(manager->all_topic);
	manager->all_topic = NULL;
	AST_VECTOR_RW_FREE(&manager->observers);
}

#ifdef AO2_DEBUG
static void state_prnt_obj(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct stasis_state *state = v_obj;

	if (!state) {
		return;
	}
	prnt(where, "%s", stasis_topic_name(state->topic));
}
#endif

struct stasis_state_manager *stasis_state_manager_create(const char *topic_name)
{
	struct stasis_state_manager *manager;

	manager = ao2_alloc_options(sizeof(*manager), state_manager_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!manager) {
		return NULL;
	}

	manager->states = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		STATE_BUCKETS, stasis_state_proxy_hash_fn, NULL, stasis_state_proxy_cmp_fn);
	if (!manager->states) {
		ao2_ref(manager, -1);
		return NULL;
	}

	manager->all_topic = stasis_topic_create(topic_name);
	if (!manager->all_topic) {
		ao2_ref(manager, -1);
		return NULL;
	}

	if (AST_VECTOR_RW_INIT(&manager->observers, 2) != 0) {
		ao2_ref(manager, -1);
		return NULL;
	}

#ifdef AO2_DEBUG
	{
		char *container_name =
			ast_alloca(strlen(stasis_topic_name(manager->all_topic)) + strlen("-manager") + 1);
		sprintf(container_name, "%s-manager", stasis_topic_name(manager->all_topic));
		ao2_container_register(container_name, manager->states, state_prnt_obj);
	}
#endif

	return manager;
}

struct stasis_topic *stasis_state_all_topic(struct stasis_state_manager *manager)
{
	return manager->all_topic;
}

struct stasis_topic *stasis_state_topic(struct stasis_state_manager *manager, const char *id)
{
	struct stasis_topic *topic;
	struct stasis_state *state;

	state = state_find_or_add(manager, NULL, id);
	if (!state) {
		return NULL;
	}

	topic = state->topic;
	ao2_ref(state, -1);
	return topic;
}

struct stasis_state_subscriber {
	/*! The stasis state subscribed to */
	struct stasis_state *state;
	/*! The stasis subscription. */
	struct stasis_subscription *stasis_sub;
};

static void subscriber_dtor(void *obj)
{
	size_t i;
	struct stasis_state_subscriber *sub = obj;
	struct stasis_state_manager *manager = sub->state->manager;

	AST_VECTOR_RW_RDLOCK(&manager->observers);
	for (i = 0; i < AST_VECTOR_SIZE(&manager->observers); ++i) {
		if (AST_VECTOR_GET(&manager->observers, i)->on_unsubscribe) {
			AST_VECTOR_GET(&manager->observers, i)->on_unsubscribe(sub->state->id, sub);
		}
	}
	AST_VECTOR_RW_UNLOCK(&manager->observers);

	ao2_lock(sub->state);
	--sub->state->num_subscribers;
	ao2_unlock(sub->state);

	ao2_ref(sub->state, -1);
}

struct stasis_state_subscriber *stasis_state_add_subscriber(
	struct stasis_state_manager *manager, const char *id)
{
	size_t i;
	struct stasis_state_subscriber *sub = ao2_alloc_options(
		sizeof(*sub), subscriber_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);

	if (!sub) {
		ast_log(LOG_ERROR, "Unable to create subscriber to %s/%s\n",
			stasis_topic_name(manager->all_topic), id);
		return NULL;
	}

	sub->state = state_find_or_add(manager, NULL, id);
	if (!sub->state) {
		ao2_ref(sub, -1);
		return NULL;
	}

	ao2_lock(sub->state);
	++sub->state->num_subscribers;
	ao2_unlock(sub->state);

	AST_VECTOR_RW_RDLOCK(&manager->observers);
	for (i = 0; i < AST_VECTOR_SIZE(&manager->observers); ++i) {
		if (AST_VECTOR_GET(&manager->observers, i)->on_subscribe) {
			AST_VECTOR_GET(&manager->observers, i)->on_subscribe(id, sub);
		}
	}
	AST_VECTOR_RW_UNLOCK(&manager->observers);

	return sub;
}

struct stasis_state_subscriber *stasis_state_subscribe_pool(struct stasis_state_manager *manager,
	const char *id, stasis_subscription_cb callback, void *data)
{
	struct stasis_topic *topic;
	struct stasis_state_subscriber *sub = stasis_state_add_subscriber(manager, id);

	if (!sub) {
		return NULL;
	}

	topic = sub->state->topic;
	ast_debug(3, "Creating stasis state subscription to id '%s'. Topic: '%s':%p %d\n",
		id, stasis_topic_name(topic), topic, (int)ao2_ref(topic, 0));

	sub->stasis_sub = stasis_subscribe_pool(topic, callback, data);

	if (!sub->stasis_sub) {
		ao2_ref(sub, -1);
		return NULL;
	}

	return sub;
}

void *stasis_state_unsubscribe(struct stasis_state_subscriber *sub)
{
	sub->stasis_sub = stasis_unsubscribe(sub->stasis_sub);
	ao2_ref(sub, -1);
	return NULL;
}

void *stasis_state_unsubscribe_and_join(struct stasis_state_subscriber *sub)
{
	if (sub) {
		sub->stasis_sub = stasis_unsubscribe_and_join(sub->stasis_sub);
		ao2_ref(sub, -1);
	}

	return NULL;
}

const char *stasis_state_subscriber_id(const struct stasis_state_subscriber *sub)
{
	return sub->state->id;
}

struct stasis_topic *stasis_state_subscriber_topic(struct stasis_state_subscriber *sub)
{
	return sub->state->topic;
}

void *stasis_state_subscriber_data(struct stasis_state_subscriber *sub)
{
	void *res;

	/*
	 * The data's reference needs to be bumped before returning so it doesn't disappear
	 * for the caller. Lock state, so the underlying message data is not replaced while
	 * retrieving.
	 */
	ao2_lock(sub->state);
	res = ao2_bump(stasis_message_data(sub->state->msg));
	ao2_unlock(sub->state);

	return res;
}

struct stasis_subscription *stasis_state_subscriber_subscription(
	struct stasis_state_subscriber *sub)
{
	return sub->stasis_sub;
}

struct stasis_state_publisher {
	/*! The stasis state to publish to */
	struct stasis_state *state;
};

static void publisher_dtor(void *obj)
{
	struct stasis_state_publisher *pub = obj;

	ao2_ref(pub->state, -1);
}

struct stasis_state_publisher *stasis_state_add_publisher(
	struct stasis_state_manager *manager, const char *id)
{
	struct stasis_state_publisher *pub = ao2_alloc_options(
		sizeof(*pub), publisher_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);

	if (!pub) {
		ast_log(LOG_ERROR, "Unable to create publisher to %s/%s\n",
			stasis_topic_name(manager->all_topic), id);
		return NULL;
	}

	pub->state = state_find_or_add(manager, NULL, id);
	if (!pub->state) {
		ao2_ref(pub, -1);
		return NULL;
	}

	return pub;
}

const char *stasis_state_publisher_id(const struct stasis_state_publisher *pub)
{
	return pub->state->id;
}

struct stasis_topic *stasis_state_publisher_topic(struct stasis_state_publisher *pub)
{
	return pub->state->topic;
}

void stasis_state_publish(struct stasis_state_publisher *pub, struct stasis_message *msg)
{
	ao2_lock(pub->state);
	ao2_replace(pub->state->msg, msg);
	ao2_unlock(pub->state);

	stasis_publish(pub->state->topic, msg);
}

/*!
 * \internal
 * \brief Find, or add the given eid to the state object
 *
 * Publishers can be tracked implicitly using eids. This allows us to add, and subsequently
 * remove state objects from the managed states container in a deterministic way. Using the
 * eids in this way is possible because it's guaranteed that there will only ever be a single
 * publisher for a uniquely named topic (topics tracked by this module) on a system.
 *
 * \note The vector does not use locking. Instead we use the state object for that, so it
 * needs to be locked prior to calling this method.
 *
 * \param state The state object
 * \param eid The system id to add to the state object
 */
static void state_find_or_add_eid(struct stasis_state *state, const struct ast_eid *eid)
{
	size_t i;

	if (!eid) {
		eid = &ast_eid_default;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&state->eids); ++i) {
		if (!ast_eid_cmp(AST_VECTOR_GET_ADDR(&state->eids, i), eid)) {
			break;
		}
	}

	if (i == AST_VECTOR_SIZE(&state->eids)) {
		if (!AST_VECTOR_APPEND(&state->eids, *eid)) {
			/* This ensures state cannot be freed if it has any eids */
			ao2_ref(state, +1);
		}
	}
}

/*!
 * \internal
 * \brief Find, and remove the given eid from the state object
 *
 * Used to remove an eid from an implicit publisher.
 *
 * \note The vector does not use locking. Instead we use the state object for that, so it
 * needs to be locked prior to calling this method.
 *
 * \param state The state object
 * \param eid The system id to remove from the state object
 */
static void state_find_and_remove_eid(struct stasis_state *state, const struct ast_eid *eid)
{
	size_t i;

	if (!eid) {
		eid = &ast_eid_default;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&state->eids); ++i) {
		if (!ast_eid_cmp(AST_VECTOR_GET_ADDR(&state->eids, i), eid)) {
			AST_VECTOR_REMOVE_UNORDERED(&state->eids, i);
			/* Balance the reference from state_find_or_add_eid */
			ao2_ref(state, -1);
			return;
		}
	}
}

void stasis_state_publish_by_id(struct stasis_state_manager *manager, const char *id,
	const struct ast_eid *eid, struct stasis_message *msg)
{
	struct stasis_state *state;

	state = state_find_or_add(manager, NULL, id);
	if (!state) {
		return;
	}

	ao2_lock(state);
	state_find_or_add_eid(state, eid);
	ao2_replace(state->msg, msg);
	ao2_unlock(state);

	stasis_publish(state->topic, msg);

	ao2_ref(state, -1);
}

void stasis_state_remove_publish_by_id(struct stasis_state_manager *manager,
	const char *id, const struct ast_eid *eid, struct stasis_message *msg)
{
	struct stasis_state *state = ao2_weakproxy_find(manager->states, id, OBJ_SEARCH_KEY, "");

	if (!state) {
		/*
		 * In most circumstances state should already exist here. However, if there is no
		 * state then it can mean one of a few things:
		 *
		 * 1. This function was called prior to an implicit publish for the same given
		 *    manager, and id.
		 * 2. This function was called more than once for the same manager, and id.
		 * 3. There is ref count problem with the explicit subscribers, and publishers.
		 */
		ast_debug(5, "Attempted to remove state for id '%s', but state not found\n", id);
		return;
	}

	if (msg) {
		stasis_publish(state->topic, msg);
	}

	ao2_lock(state);
	state_find_and_remove_eid(state, eid);
	ao2_unlock(state);

	ao2_ref(state, -1);
}

int stasis_state_add_observer(struct stasis_state_manager *manager,
	struct stasis_state_observer *observer)
{
	int res;

	AST_VECTOR_RW_WRLOCK(&manager->observers);
	res = AST_VECTOR_APPEND(&manager->observers, observer);
	AST_VECTOR_RW_UNLOCK(&manager->observers);

	return res;
}

void stasis_state_remove_observer(struct stasis_state_manager *manager,
	struct stasis_state_observer *observer)
{
	AST_VECTOR_RW_WRLOCK(&manager->observers);
	AST_VECTOR_REMOVE_ELEM_UNORDERED(&manager->observers, observer, AST_VECTOR_ELEM_CLEANUP_NOOP);
	AST_VECTOR_RW_UNLOCK(&manager->observers);
}

static int handle_stasis_state(struct stasis_state *state, on_stasis_state handler, void *data)
{
	struct stasis_message *msg;
	int res;

	/*
	 * State needs to be locked here while we retrieve and bump the reference on its message
	 * object. Doing so guarantees the message object will live throughout its handling.
	 */
	ao2_lock(state);
	msg = ao2_bump(state->msg);
	ao2_unlock(state);

	res = handler(state->id, msg, data);
	ao2_cleanup(msg);
	return res;
}

static int handle_stasis_state_proxy(void *obj, void *arg, void *data, int flags)
{
	struct stasis_state *state = ao2_weakproxy_get_object(obj, 0);

	if (state) {
		int res;
		res = handle_stasis_state(state, arg, data);
		ao2_ref(state, -1);
		return res;
	}

	return 0;
}

void stasis_state_callback_all(struct stasis_state_manager *manager, on_stasis_state handler,
	void *data)
{
	ast_assert(handler != NULL);

	ao2_callback_data(manager->states, OBJ_MULTIPLE | OBJ_NODATA,
		handle_stasis_state_proxy, handler, data);
}

static int handle_stasis_state_subscribed(void *obj, void *arg, void *data, int flags)
{
	struct stasis_state *state = ao2_weakproxy_get_object(obj, 0);
	int res = 0;

	if (state && state->num_subscribers) {
		res = handle_stasis_state(state, arg, data);
	}

	ao2_cleanup(state);

	return res;
}

void stasis_state_callback_subscribed(struct stasis_state_manager *manager, on_stasis_state handler,
	void *data)
{
	ast_assert(handler != NULL);

	ao2_callback_data(manager->states, OBJ_MULTIPLE | OBJ_NODATA,
		handle_stasis_state_subscribed, handler, data);
}
