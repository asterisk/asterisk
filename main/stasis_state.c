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
	/*! The manager that owns and handles this state */
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
	char id[0];
};

AO2_STRING_FIELD_HASH_FN(stasis_state, id);
AO2_STRING_FIELD_CMP_FN(stasis_state, id);

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
	ast_assert(id != NULL && (id + 1) != NULL);

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
	ao2_cleanup(state->manager);
	state->manager = NULL;

	/* All eids should have been removed */
	ast_assert(AST_VECTOR_SIZE(&state->eids) == 0);
	AST_VECTOR_FREE(&state->eids);
}

/*!
 * \internal
 * \brief Allocate a stasis state object.
 *
 * Create and initialize a state structure. It's required that either a state
 * topic, or an id is specified. If a state topic is not given then one will be
 * created using the given id.
 *
 * \param manager The owning manager
 * \param state_topic A state topic to be managed
 * \param id The unique id for the state
 *
 * \return A stasis_state object or NULL
 * \return NULL on error
 */
static struct stasis_state *state_alloc(struct stasis_state_manager *manager,
	struct stasis_topic *state_topic, const char *id)
{
	struct stasis_state *state;

	if (!state_topic) {
		char *name;

		/* If not given a state topic, then an id is required */
		ast_assert(id != NULL);

		/*
		 * To provide further detail and to ensure that the topic is unique within the
		 * scope of the system we prefix it with the manager's topic name, which should
		 * itself already be unique.
		 */
		if (ast_asprintf(&name, "%s/%s", stasis_topic_name(manager->all_topic), id) < 0) {
			ast_log(LOG_ERROR, "Unable to create state topic name '%s/%s'\n",
					stasis_topic_name(manager->all_topic), id);
			return NULL;
		}

		state_topic = stasis_topic_create(name);

		if (!state_topic) {
			ast_log(LOG_ERROR, "Unable to create state topic '%s'\n", name);
			ast_free(name);
			return NULL;
		}
		ast_free(name);
	}

	if (!id) {
		/* If not given an id, then a state topic is required */
		ast_assert(state_topic != NULL);

		/* Get the id we'll key off of from the state topic */
		id = state_id_by_topic(manager->all_topic, state_topic);
	}

	/*
	 * Since the state topic could have been passed in, go ahead and bump its reference.
	 * By doing this here first, it allows us to consistently decrease the reference on
	 * state allocation error.
	 */
	ao2_ref(state_topic, +1);

	state = ao2_alloc(sizeof(*state) + strlen(id) + 1, state_dtor);
	if (!state) {
		ast_log(LOG_ERROR, "Unable to allocate state '%s' in manager '%s'\n",
				id, stasis_topic_name(manager->all_topic));
		ao2_ref(state_topic, -1);
		return NULL;
	}

	strcpy(state->id, id); /* Safe */
	state->topic = state_topic; /* ref already bumped above */

	state->forward = stasis_forward_all(state->topic, manager->all_topic);
	if (!state->forward) {
		ast_log(LOG_ERROR, "Unable to add state '%s' forward in manager '%s'\n",
				id, stasis_topic_name(manager->all_topic));
		ao2_ref(state, -1);
		return NULL;
	}

	if (AST_VECTOR_INIT(&state->eids, 2)) {
		ast_log(LOG_ERROR, "Unable to initialize eids for state '%s' in manager '%s'\n",
				id, stasis_topic_name(manager->all_topic));
		ao2_ref(state, -1);
		return NULL;
	}

	state->manager = ao2_bump(manager);

	return state;
}

/*!
 * \internal
 * \brief Create a state object, and add it to the manager.
 *
 * \note Locking on the states container is specifically not done here, thus
 * appropriate locks should be applied prior to this function being called.
 *
 * \param manager The manager to be added to
 * \param state_topic A state topic to be managed (if NULL id is required)
 * \param id The unique id for the state (if NULL state_topic is required)
 *
 * \return The added state object
 * \return NULL on error
 */
static struct stasis_state *state_add(struct stasis_state_manager *manager,
	struct stasis_topic *state_topic, const char *id)
{
	struct stasis_state *state = state_alloc(manager, state_topic, id);

	if (!state) {
		return NULL;
	}

	if (!ao2_link_flags(manager->states, state, OBJ_NOLOCK)) {
		ast_log(LOG_ERROR, "Unable to add state '%s' to manager '%s'\n",
				state->id ? state->id : "", stasis_topic_name(manager->all_topic));
		ao2_ref(state, -1);
		return NULL;
	}

	return state;
}

/*!
 * \internal
 * \brief Find a state by id, or create one if not found and add it to the manager.
 *
 * \note Locking on the states container is specifically not done here, thus
 * appropriate locks should be applied prior to this function being called.
 *
 * \param manager The manager to be added to
 * \param state_topic A state topic to be managed (if NULL id is required)
 * \param id The unique id for the state (if NULL state_topic is required)
 *
 * \return The added state object
 * \return NULL on error
 */
static struct stasis_state *state_find_or_add(struct stasis_state_manager *manager,
	struct stasis_topic *state_topic, const char *id)
{
	struct stasis_state *state;

	if (ast_strlen_zero(id)) {
		id = state_id_by_topic(manager->all_topic, state_topic);
	}

	state = ao2_find(manager->states, id, OBJ_SEARCH_KEY | OBJ_NOLOCK);

	return state ? state : state_add(manager, state_topic, id);
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
		STATE_BUCKETS, stasis_state_hash_fn, NULL, stasis_state_cmp_fn);
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

	ao2_lock(manager->states);
	state = state_find_or_add(manager, NULL, id);
	ao2_unlock(manager->states);

	if (!state) {
		return NULL;
	}

	topic = state->topic;
	ao2_ref(state, -1);
	return topic;
}

/*!
 * \internal
 * \brief Remove a state from the stasis manager
 *
 * State should only be removed from the manager under the following conditions:
 *
 *   There are no more subscribers to it
 *   There are no more explicit publishers publishing to it
 *   There are no more implicit publishers publishing to it
 *
 * Subscribers and explicit publishers hold a reference to the state object itself, so
 * once a state's reference count drops to 2 (1 for the manager, 1 passed in) then we
 * know there are no more subscribers or explicit publishers. Implicit publishers are
 * tracked by eids, so once that container is empty no more implicit publishers exist
 * for the state either. Only then can a state be removed.
 *
 * \param state The state to remove
 */
static void state_remove(struct stasis_state *state)
{
	ao2_lock(state);

	/*
	 * The manager's state container also needs to be locked here prior to checking
	 * the state's reference count, and potentially removing since we don't want its
	 * count to possibly increase between the check and unlinking.
	 */
	ao2_lock(state->manager->states);

	/*
	 * If there are only 2 references left then it's the one owned by the manager,
	 * and the one passed in to this function. However, before removing it from the
	 * manager we need to also check that no eid is associated with the given state.
	 * If an eid still remains then this means that an implicit publisher is still
	 * publishing to this state.
	 */
	if (ao2_ref(state, 0) == 2 && AST_VECTOR_SIZE(&state->eids) == 0) {
		ao2_unlink_flags(state->manager->states, state, 0);
	}

	ao2_unlock(state->manager->states);
	ao2_unlock(state);

	/* Now it's safe to remove the reference that is held on the given state */
	ao2_ref(state, -1);
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

	state_remove(sub->state);
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

	ao2_lock(manager->states);
	sub->state = state_find_or_add(manager, NULL, id);
	if (!sub->state) {
		ao2_unlock(manager->states);
		ao2_ref(sub, -1);
		return NULL;
	}
	ao2_unlock(manager->states);

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
	sub->stasis_sub = stasis_unsubscribe_and_join(sub->stasis_sub);
	ao2_ref(sub, -1);
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

	state_remove(pub->state);
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

	ao2_lock(manager->states);
	pub->state = state_find_or_add(manager, NULL, id);
	if (!pub->state) {
		ao2_unlock(manager->states);
		ao2_ref(pub, -1);
		return NULL;
	}
	ao2_unlock(manager->states);

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
		AST_VECTOR_APPEND(&state->eids, *eid);
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
			return;
		}
	}
}

void stasis_state_publish_by_id(struct stasis_state_manager *manager, const char *id,
	const struct ast_eid *eid, struct stasis_message *msg)
{
	struct stasis_state *state;

	ao2_lock(manager->states);
	state = state_find_or_add(manager, NULL, id);
	ao2_unlock(manager->states);

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
	struct stasis_state *state = ao2_find(manager->states, id, OBJ_SEARCH_KEY);

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

	state_remove(state);
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

static int handle_stasis_state(void *obj, void *arg, void *data, int flags)
{
	struct stasis_state *state = obj;
	on_stasis_state handler = arg;
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

void stasis_state_callback_all(struct stasis_state_manager *manager, on_stasis_state handler,
	void *data)
{
	ast_assert(handler != NULL);

	ao2_callback_data(manager->states, OBJ_MULTIPLE | OBJ_NODATA,
		handle_stasis_state, handler, data);
}

static int handle_stasis_state_subscribed(void *obj, void *arg, void *data, int flags)
{
	struct stasis_state *state = obj;

	if (state->num_subscribers) {
		return handle_stasis_state(obj, arg, data, flags);
	}

	return 0;
}

void stasis_state_callback_subscribed(struct stasis_state_manager *manager, on_stasis_state handler,
	void *data)
{
	ast_assert(handler != NULL);

	ao2_callback_data(manager->states, OBJ_MULTIPLE | OBJ_NODATA,
		handle_stasis_state_subscribed, handler, data);
}
