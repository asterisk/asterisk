/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \file
 *
 * \brief Stasis out-of-call text message support
 *
 * \author Matt Jordan <mjordan@digium.com>
 */

#include "asterisk.h"

#include "asterisk/message.h"
#include "asterisk/endpoints.h"
#include "asterisk/astobj2.h"
#include "asterisk/vector.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "messaging.h"

/*!
 * \brief Subscription to all technologies
 */
#define TECH_WILDCARD "__AST_ALL_TECH"

/*!
 * \brief Number of buckets for the \ref endpoint_subscriptions container
 */
#define ENDPOINTS_NUM_BUCKETS 127

/*! \brief Storage object for an application */
struct application_tuple {
	/*! ao2 ref counted private object to pass to the callback */
	void *pvt;
	/*! The callback to call when this application has a message */
	message_received_cb callback;
	/*! The name (key) of the application */
	char app_name[];
};

/*! \brief A subscription to some endpoint or technology */
struct message_subscription {
	/*! The applications that have subscribed to this endpoint or tech */
	AST_VECTOR(, struct application_tuple *) applications;
	/*! The name of this endpoint or tech */
	char token[];
};

/*! \brief The subscriptions to endpoints */
static struct ao2_container *endpoint_subscriptions;

/*!
 * \brief The subscriptions to technologies
 *
 * \note These are stored separately from standard endpoints, given how
 * relatively few of them there are.
 */
static AST_VECTOR(,struct message_subscription *) tech_subscriptions;

/*! \brief RWLock for \c tech_subscriptions */
static ast_rwlock_t tech_subscriptions_lock;

/*! \internal \brief Destructor for \c application_tuple */
static void application_tuple_dtor(void *obj)
{
	struct application_tuple *tuple = obj;

	ao2_cleanup(tuple->pvt);
}

/*! \internal \brief Constructor for \c application_tuple */
static struct application_tuple *application_tuple_alloc(const char *app_name, message_received_cb callback, void *pvt)
{
	struct application_tuple *tuple;
	size_t size = sizeof(*tuple) + strlen(app_name) + 1;

	ast_assert(callback != NULL);

	tuple = ao2_alloc_options(size, application_tuple_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!tuple) {
		return NULL;
	}

	strcpy(tuple->app_name, app_name); /* Safe */
	tuple->pvt = ao2_bump(pvt);
	tuple->callback = callback;

	return tuple;
}

/*! \internal \brief Destructor for \ref message_subscription */
static void message_subscription_dtor(void *obj)
{
	struct message_subscription *sub = obj;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&sub->applications); i++) {
		struct application_tuple *tuple = AST_VECTOR_GET(&sub->applications, i);

		ao2_cleanup(tuple);
	}
	AST_VECTOR_FREE(&sub->applications);
}

/*! \internal \brief Constructor for \ref message_subscription */
static struct message_subscription *message_subscription_alloc(const char *token)
{
	struct message_subscription *sub;
	size_t size = sizeof(*sub) + strlen(token) + 1;

	sub = ao2_alloc_options(size, message_subscription_dtor, AO2_ALLOC_OPT_LOCK_RWLOCK);
	if (!sub) {
		return NULL;
	}
	strcpy(sub->token, token); /* Safe */

	return sub;
}

/*! AO2 hash function for \ref message_subscription */
static int message_subscription_hash_cb(const void *obj, const int flags)
{
	const struct message_subscription *sub;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		sub = obj;
		key = sub->token;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! AO2 comparison function for \ref message_subscription */
static int message_subscription_compare_cb(void *obj, void *arg, int flags)
{
	const struct message_subscription *object_left = obj;
	const struct message_subscription *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->token;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->token, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(object_left->token, right_key, strlen(right_key));
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

/*! \internal \brief Convert a \c ast_msg To/From URI to a Stasis endpoint name */
static void msg_to_endpoint(const struct ast_msg *msg, char *buf, size_t len)
{
	const char *endpoint = ast_msg_get_endpoint(msg);

	snprintf(buf, len, "%s%s%s", ast_msg_get_tech(msg),
		ast_strlen_zero(endpoint) ? "" : "/",
		S_OR(endpoint, ""));
}

/*! \internal
 * \brief Callback from the \c message API that determines if we can handle
 * this message
 */
static int has_destination_cb(const struct ast_msg *msg)
{
	struct message_subscription *sub;
	int i;
	char buf[256];

	msg_to_endpoint(msg, buf, sizeof(buf));

	ast_rwlock_rdlock(&tech_subscriptions_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&tech_subscriptions); i++) {
		sub = AST_VECTOR_GET(&tech_subscriptions, i);

		if (!sub) {
			continue;
		}

		if (!strcmp(sub->token, TECH_WILDCARD)
		    || !strncasecmp(sub->token, buf, strlen(sub->token))
		    || !strncasecmp(sub->token, buf, strlen(sub->token))) {
			ast_rwlock_unlock(&tech_subscriptions_lock);
			goto match;
		}

	}
	ast_rwlock_unlock(&tech_subscriptions_lock);

	sub = ao2_find(endpoint_subscriptions, buf, OBJ_SEARCH_KEY);
	if (sub) {
		ao2_ref(sub, -1);
		goto match;
	}

	ast_debug(1, "No subscription found for %s\n", buf);
	return 0;

match:
	return 1;
}

static struct ast_json *msg_to_json(struct ast_msg *msg)
{
	struct ast_json *json_obj;
	struct ast_json *json_vars;
	struct ast_msg_var_iterator *it_vars;
	const char *name;
	const char *value;

	it_vars = ast_msg_var_iterator_init(msg);
	if (!it_vars) {
		return NULL;
	}

	json_vars = ast_json_array_create();
	if (!json_vars) {
		ast_msg_var_iterator_destroy(it_vars);
		return NULL;
	}

	while (ast_msg_var_iterator_next(msg, it_vars, &name, &value)) {
		struct ast_json *json_tuple;

		json_tuple = ast_json_pack("{s: s}", name, value);
		if (!json_tuple) {
			ast_json_unref(json_vars);
			ast_msg_var_iterator_destroy(it_vars);
			return NULL;
		}

		ast_json_array_append(json_vars, json_tuple);
		ast_msg_var_unref_current(it_vars);
	}
	ast_msg_var_iterator_destroy(it_vars);

	json_obj = ast_json_pack("{s: s, s: s, s: s, s: o}",
		"from", ast_msg_get_from(msg),
		"to", ast_msg_get_to(msg),
		"body", ast_msg_get_body(msg),
		"variables", json_vars);

	return json_obj;
}

static int handle_msg_cb(struct ast_msg *msg)
{
	struct message_subscription *sub;
	int i;
	char buf[256];
	const char *endpoint_name;
	struct ast_json *json_msg;

	msg_to_endpoint(msg, buf, sizeof(buf));

	ast_rwlock_rdlock(&tech_subscriptions_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&tech_subscriptions); i++) {
		sub = AST_VECTOR_GET(&tech_subscriptions, i);

		if (!sub) {
			continue;
		}

		if (!strcmp(sub->token, TECH_WILDCARD)
		    || !strncasecmp(sub->token, buf, strlen(sub->token))) {
			ast_rwlock_unlock(&tech_subscriptions_lock);
			ao2_bump(sub);
			endpoint_name = buf;
			goto match;
		}
	}
	ast_rwlock_unlock(&tech_subscriptions_lock);

	sub = ao2_find(endpoint_subscriptions, buf, OBJ_SEARCH_KEY);
	if (sub) {
		endpoint_name = buf;
		goto match;
	}

	return -1;

match:
	ast_debug(3, "Dispatching message for %s\n", endpoint_name);

	json_msg = msg_to_json(msg);
	if (!json_msg) {
		ao2_ref(sub, -1);
		return -1;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&sub->applications); i++) {
		struct application_tuple *tuple = AST_VECTOR_GET(&sub->applications, i);

		tuple->callback(endpoint_name, json_msg, tuple->pvt);
	}

	ast_json_unref(json_msg);
	ao2_ref(sub, -1);
	return 0;
}

struct ast_msg_handler ari_msg_handler = {
	.name = "ari",
	.handle_msg = handle_msg_cb,
	.has_destination = has_destination_cb,
};

static int messaging_subscription_cmp(struct message_subscription *sub, const char *key)
{
	return !strcmp(sub->token, key) ? 1 : 0;
}

static int application_tuple_cmp(struct application_tuple *item, const char *key)
{
	return !strcmp(item->app_name, key) ? 1 : 0;
}

static int is_app_subscribed(struct message_subscription *sub, const char *app_name)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&sub->applications); i++) {
		struct application_tuple *tuple;

		tuple = AST_VECTOR_GET(&sub->applications, i);
		if (tuple && !strcmp(tuple->app_name, app_name)) {
			return 1;
		}
	}

	return 0;
}

static struct message_subscription *get_subscription(struct ast_endpoint *endpoint)
{
	struct message_subscription *sub = NULL;

	if (endpoint && !ast_strlen_zero(ast_endpoint_get_resource(endpoint))) {
		sub = ao2_find(endpoint_subscriptions, endpoint, OBJ_SEARCH_KEY);
	} else {
		int i;

		ast_rwlock_rdlock(&tech_subscriptions_lock);
		for (i = 0; i < AST_VECTOR_SIZE(&tech_subscriptions); i++) {
			sub = AST_VECTOR_GET(&tech_subscriptions, i);

			if (sub && !strcmp(sub->token, endpoint ? ast_endpoint_get_tech(endpoint) : TECH_WILDCARD)) {
				ao2_bump(sub);
				break;
			}
		}
		ast_rwlock_unlock(&tech_subscriptions_lock);
	}

	return sub;
}

void messaging_app_unsubscribe_endpoint(const char *app_name, const char *endpoint_id)
{
	RAII_VAR(struct message_subscription *, sub, NULL, ao2_cleanup);
	RAII_VAR(struct ast_endpoint *, endpoint, NULL, ao2_cleanup);

	endpoint = ast_endpoint_find_by_id(endpoint_id);
	sub = get_subscription(endpoint);
	if (!sub) {
		return;
	}

	ao2_lock(sub);
	if (!is_app_subscribed(sub, app_name)) {
		ao2_unlock(sub);
		return;
	}

	AST_VECTOR_REMOVE_CMP_UNORDERED(&sub->applications, app_name, application_tuple_cmp, ao2_cleanup);
	if (AST_VECTOR_SIZE(&sub->applications) == 0) {
		if (endpoint && !ast_strlen_zero(ast_endpoint_get_resource(endpoint))) {
			ao2_unlink(endpoint_subscriptions, sub);
		} else {
			ast_rwlock_wrlock(&tech_subscriptions_lock);
			AST_VECTOR_REMOVE_CMP_UNORDERED(&tech_subscriptions, endpoint ? ast_endpoint_get_id(endpoint) : TECH_WILDCARD,
				messaging_subscription_cmp, AST_VECTOR_ELEM_CLEANUP_NOOP);
			ast_rwlock_unlock(&tech_subscriptions_lock);
		}
	}
	ao2_unlock(sub);
	ao2_ref(sub, -1);

	ast_debug(3, "App '%s' unsubscribed to messages from endpoint '%s'\n", app_name, endpoint ? ast_endpoint_get_id(endpoint) : "-- ALL --");
	ast_test_suite_event_notify("StasisMessagingSubscription", "SubState: Unsubscribed\r\nAppName: %s\r\nToken: %s\r\n",
		app_name, endpoint ? ast_endpoint_get_id(endpoint) : "ALL");
}

static struct message_subscription *get_or_create_subscription(struct ast_endpoint *endpoint)
{
	struct message_subscription *sub = get_subscription(endpoint);

	if (sub) {
		return sub;
	}

	sub = message_subscription_alloc(endpoint ? ast_endpoint_get_id(endpoint) : TECH_WILDCARD);
	if (!sub) {
		return NULL;
	}

	if (endpoint && !ast_strlen_zero(ast_endpoint_get_resource(endpoint))) {
		ao2_link(endpoint_subscriptions, sub);
	} else {
		ast_rwlock_wrlock(&tech_subscriptions_lock);
		ao2_ref(sub, +1);
		if (AST_VECTOR_APPEND(&tech_subscriptions, sub)) {
			/* Release the refs that were for the vector and the allocation. */
			ao2_ref(sub, -2);
			sub = NULL;
		}
		ast_rwlock_unlock(&tech_subscriptions_lock);
	}

	return sub;
}

int messaging_app_subscribe_endpoint(const char *app_name, struct ast_endpoint *endpoint, message_received_cb callback, void *pvt)
{
	RAII_VAR(struct message_subscription *, sub, NULL, ao2_cleanup);
	struct application_tuple *tuple;

	sub = get_or_create_subscription(endpoint);
	if (!sub) {
		return -1;
	}

	ao2_lock(sub);
	if (is_app_subscribed(sub, app_name)) {
		ao2_unlock(sub);
		return 0;
	}

	tuple = application_tuple_alloc(app_name, callback, pvt);
	if (!tuple) {
		ao2_unlock(sub);
		return -1;
	}
	if (AST_VECTOR_APPEND(&sub->applications, tuple)) {
		ao2_ref(tuple, -1);
		ao2_unlock(sub);
		return -1;
	}
	ao2_unlock(sub);

	ast_debug(3, "App '%s' subscribed to messages from endpoint '%s'\n", app_name, endpoint ? ast_endpoint_get_id(endpoint) : "-- ALL --");
	ast_test_suite_event_notify("StasisMessagingSubscription", "SubState: Subscribed\r\nAppName: %s\r\nToken: %s\r\n",
		app_name, endpoint ? ast_endpoint_get_id(endpoint) : "ALL");

	return 0;
}


int messaging_cleanup(void)
{
	ast_msg_handler_unregister(&ari_msg_handler);
	ao2_ref(endpoint_subscriptions, -1);
	AST_VECTOR_FREE(&tech_subscriptions);
	ast_rwlock_destroy(&tech_subscriptions_lock);\

	return 0;
}

int messaging_init(void)
{
	endpoint_subscriptions = ao2_t_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0,
		ENDPOINTS_NUM_BUCKETS, message_subscription_hash_cb, NULL,
		message_subscription_compare_cb, "Endpoint messaging subscription container creation");
	if (!endpoint_subscriptions) {
		return -1;
	}

	if (AST_VECTOR_INIT(&tech_subscriptions, 4)) {
		ao2_ref(endpoint_subscriptions, -1);
		return -1;
	}

	if (ast_rwlock_init(&tech_subscriptions_lock)) {
		ao2_ref(endpoint_subscriptions, -1);
		AST_VECTOR_FREE(&tech_subscriptions);
		return -1;
	}

	if (ast_msg_handler_register(&ari_msg_handler)) {
		ao2_ref(endpoint_subscriptions, -1);
		AST_VECTOR_FREE(&tech_subscriptions);
		ast_rwlock_destroy(&tech_subscriptions_lock);
		return -1;
	}

	return 0;
}
