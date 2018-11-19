/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

/*! \file
 *
 * \brief Stasis message router implementation.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/vector.h"

/*! \internal */
struct stasis_message_route {
	/*! Message type handle by this route. */
	struct stasis_message_type *message_type;
	/*! Callback function for incoming message processing. */
	stasis_subscription_cb callback;
	/*! Data pointer to be handed to the callback. */
	void *data;
};

AST_VECTOR(route_table, struct stasis_message_route);

static struct stasis_message_route *route_table_find(struct route_table *table,
	struct stasis_message_type *message_type)
{
	size_t idx;
	struct stasis_message_route *route;

	/* While a linear search for routes may seem very inefficient, most
	 * route tables have six routes or less. For such small data, it's
	 * hard to beat a linear search. If we start having larger route
	 * tables, then we can look into containers with more efficient
	 * lookups.
	 */
	for (idx = 0; idx < AST_VECTOR_SIZE(table); ++idx) {
		route = AST_VECTOR_GET_ADDR(table, idx);
		if (route->message_type == message_type) {
			return route;
		}
	}

	return NULL;
}

/*!
 * \brief route_table comparator for AST_VECTOR_REMOVE_CMP_UNORDERED()
 *
 * \param elem Element to compare against
 * \param value Value to compare with the vector element.
 *
 * \return 0 if element does not match.
 * \return Non-zero if element matches.
 */
#define ROUTE_TABLE_ELEM_CMP(elem, value) ((elem).message_type == (value))

/*!
 * \brief route_table vector element cleanup.
 *
 * \param elem Element to cleanup
 *
 * \return Nothing
 */
#define ROUTE_TABLE_ELEM_CLEANUP(elem)  ao2_cleanup((elem).message_type)

static int route_table_remove(struct route_table *table,
	struct stasis_message_type *message_type)
{
	return AST_VECTOR_REMOVE_CMP_UNORDERED(table, message_type, ROUTE_TABLE_ELEM_CMP,
		ROUTE_TABLE_ELEM_CLEANUP);
}

static int route_table_add(struct route_table *table,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data)
{
	struct stasis_message_route route;
	int res;

	ast_assert(callback != NULL);
	ast_assert(route_table_find(table, message_type) == NULL);

	route.message_type = ao2_bump(message_type);
	route.callback = callback;
	route.data = data;

	res = AST_VECTOR_APPEND(table, route);
	if (res) {
		ROUTE_TABLE_ELEM_CLEANUP(route);
	}
	return res;
}

static void route_table_dtor(struct route_table *table)
{
	size_t idx;
	struct stasis_message_route *route;

	for (idx = 0; idx < AST_VECTOR_SIZE(table); ++idx) {
		route = AST_VECTOR_GET_ADDR(table, idx);
		ROUTE_TABLE_ELEM_CLEANUP(*route);
	}
	AST_VECTOR_FREE(table);
}

/*! \internal */
struct stasis_message_router {
	/*! Subscription to the upstream topic */
	struct stasis_subscription *subscription;
	/*! Subscribed routes */
	struct route_table routes;
	/*! Subscribed routes for \ref stasis_cache_update messages */
	struct route_table cache_routes;
	/*! Route of last resort */
	struct stasis_message_route default_route;
};

static void router_dtor(void *obj)
{
	struct stasis_message_router *router = obj;

	ast_assert(!stasis_subscription_is_subscribed(router->subscription));
	ast_assert(stasis_subscription_is_done(router->subscription));

	router->subscription = NULL;

	route_table_dtor(&router->routes);
	route_table_dtor(&router->cache_routes);
}

static int find_route(
	struct stasis_message_router *router,
	struct stasis_message *message,
	struct stasis_message_route *route_out)
{
	struct stasis_message_route *route = NULL;
	struct stasis_message_type *type = stasis_message_type(message);
	SCOPED_AO2LOCK(lock, router);

	ast_assert(route_out != NULL);

	if (type == stasis_cache_update_type()) {
		/* Find a cache route */
		struct stasis_cache_update *update =
			stasis_message_data(message);
		route = route_table_find(&router->cache_routes, update->type);
	}

	if (route == NULL) {
		/* Find a regular route */
		route = route_table_find(&router->routes, type);
	}

	if (route == NULL && router->default_route.callback) {
		/* Maybe the default route, then? */
		route = &router->default_route;
	}

	if (!route) {
		return -1;
	}

	*route_out = *route;
	return 0;
}

static void router_dispatch(void *data,
			    struct stasis_subscription *sub,
			    struct stasis_message *message)
{
	struct stasis_message_router *router = data;
	struct stasis_message_route route;

	if (find_route(router, message, &route) == 0) {
		route.callback(route.data, sub, message);
	}

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(router);
	}
}

static struct stasis_message_router *stasis_message_router_create_internal(
	struct stasis_topic *topic, int use_thread_pool)
{
	int res;
	struct stasis_message_router *router;

	router = ao2_t_alloc(sizeof(*router), router_dtor, stasis_topic_name(topic));
	if (!router) {
		return NULL;
	}

	res = 0;
	res |= AST_VECTOR_INIT(&router->routes, 0);
	res |= AST_VECTOR_INIT(&router->cache_routes, 0);
	if (res) {
		ao2_ref(router, -1);

		return NULL;
	}

	if (use_thread_pool) {
		router->subscription = stasis_subscribe_pool(topic, router_dispatch, router);
	} else {
		router->subscription = stasis_subscribe(topic, router_dispatch, router);
	}
	if (!router->subscription) {
		ao2_ref(router, -1);

		return NULL;
	}

	/* We need to receive subscription change messages so we know when our subscription goes away */
	stasis_subscription_accept_message_type(router->subscription, stasis_subscription_change_type());

	return router;
}

struct stasis_message_router *stasis_message_router_create(
	struct stasis_topic *topic)
{
	return stasis_message_router_create_internal(topic, 0);
}

struct stasis_message_router *stasis_message_router_create_pool(
	struct stasis_topic *topic)
{
	return stasis_message_router_create_internal(topic, 1);
}

void stasis_message_router_unsubscribe(struct stasis_message_router *router)
{
	if (!router) {
		return;
	}

	ao2_lock(router);
	router->subscription = stasis_unsubscribe(router->subscription);
	ao2_unlock(router);
}

void stasis_message_router_unsubscribe_and_join(
	struct stasis_message_router *router)
{
	if (!router) {
		return;
	}
	stasis_unsubscribe_and_join(router->subscription);
}

int stasis_message_router_is_done(struct stasis_message_router *router)
{
	if (!router) {
		/* Null router is about as done as you can get */
		return 1;
	}

	return stasis_subscription_is_done(router->subscription);
}

void stasis_message_router_publish_sync(struct stasis_message_router *router,
	struct stasis_message *message)
{
	ast_assert(router != NULL);

	ao2_bump(router);
	stasis_publish_sync(router->subscription, message);
	ao2_cleanup(router);
}

int stasis_message_router_set_congestion_limits(struct stasis_message_router *router,
	long low_water, long high_water)
{
	int res = -1;

	if (router) {
		res = stasis_subscription_set_congestion_limits(router->subscription,
			low_water, high_water);
	}
	return res;
}

int stasis_message_router_add(struct stasis_message_router *router,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data)
{
	int res;

	ast_assert(router != NULL);

	if (!message_type) {
		/* Cannot route to NULL type. */
		return -1;
	}
	ao2_lock(router);
	res = route_table_add(&router->routes, message_type, callback, data);
	if (!res) {
		stasis_subscription_accept_message_type(router->subscription, message_type);
		/* Until a specific message type was added we would already drop the message, so being
		 * selective now doesn't harm us. If we have a default route then we are already forced
		 * to filter nothing and messages will come in regardless.
		 */
		stasis_subscription_set_filter(router->subscription, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
	}
	ao2_unlock(router);
	return res;
}

int stasis_message_router_add_cache_update(struct stasis_message_router *router,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data)
{
	int res;

	ast_assert(router != NULL);

	if (!message_type) {
		/* Cannot cache a route to NULL type. */
		return -1;
	}
	ao2_lock(router);
	res = route_table_add(&router->cache_routes, message_type, callback, data);
	if (!res) {
		stasis_subscription_accept_message_type(router->subscription, stasis_cache_update_type());
		stasis_subscription_set_filter(router->subscription, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
	}
	ao2_unlock(router);
	return res;
}

void stasis_message_router_remove(struct stasis_message_router *router,
	struct stasis_message_type *message_type)
{
	ast_assert(router != NULL);

	if (!message_type) {
		/* Cannot remove a NULL type. */
		return;
	}
	ao2_lock(router);
	route_table_remove(&router->routes, message_type);
	ao2_unlock(router);
}

void stasis_message_router_remove_cache_update(
	struct stasis_message_router *router,
	struct stasis_message_type *message_type)
{
	ast_assert(router != NULL);

	if (!message_type) {
		/* Cannot remove a NULL type. */
		return;
	}
	ao2_lock(router);
	route_table_remove(&router->cache_routes, message_type);
	ao2_unlock(router);
}

int stasis_message_router_set_default(struct stasis_message_router *router,
	stasis_subscription_cb callback,
	void *data)
{
	ast_assert(router != NULL);
	ast_assert(callback != NULL);

	ao2_lock(router);
	router->default_route.callback = callback;
	router->default_route.data = data;
	ao2_unlock(router);

	stasis_subscription_set_filter(router->subscription, STASIS_SUBSCRIPTION_FILTER_FORCED_NONE);

	/* While this implementation can never fail, it used to be able to */
	return 0;
}
