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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis_message_router.h"

/*! \internal */
struct stasis_message_route {
	/*! Message type handle by this route. */
	struct stasis_message_type *message_type;
	/*! Callback function for incoming message processing. */
	stasis_subscription_cb callback;
	/*! Data pointer to be handed to the callback. */
	void *data;
};

struct route_table {
	/*! Current number of entries in the route table */
	size_t current_size;
	/*! Allocated number of entires in the route table */
	size_t max_size;
	/*! The route table itself */
	struct stasis_message_route routes[];
};

static struct stasis_message_route *table_find_route(struct route_table *table,
	struct stasis_message_type *message_type)
{
	size_t idx;

	/* While a linear search for routes may seem very inefficient, most
	 * route tables have six routes or less. For such small data, it's
	 * hard to beat a linear search. If we start having larger route
	 * tables, then we can look into containers with more efficient
	 * lookups.
	 */
	for (idx = 0; idx < table->current_size; ++idx) {
		if (table->routes[idx].message_type == message_type) {
			return &table->routes[idx];
		}
	}

	return NULL;
}

static int table_add_route(struct route_table **table_ptr,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data)
{
	struct route_table *table = *table_ptr;
	struct stasis_message_route *route;

	ast_assert(table_find_route(table, message_type) == NULL);

	if (table->current_size + 1 > table->max_size) {
		size_t new_max_size = table->max_size ? table->max_size * 2 : 1;
		struct route_table *new_table = ast_realloc(table,
			sizeof(*new_table) +
			sizeof(new_table->routes[0]) * new_max_size);
		if (!new_table) {
			return -1;
		}
		*table_ptr = table = new_table;
		table->max_size = new_max_size;
	}

	route = &table->routes[table->current_size++];

	route->message_type = ao2_bump(message_type);
	route->callback = callback;
	route->data = data;

	return 0;
}

static int table_remove_route(struct route_table *table,
	struct stasis_message_type *message_type)
{
	size_t idx;

	for (idx = 0; idx < table->current_size; ++idx) {
		if (table->routes[idx].message_type == message_type) {
			ao2_cleanup(message_type);
			table->routes[idx] =
				table->routes[--table->current_size];
			return 0;
		}
	}
	return -1;
}

/*! \internal */
struct stasis_message_router {
	/*! Subscription to the upstream topic */
	struct stasis_subscription *subscription;
	/*! Subscribed routes */
	struct route_table *routes;
	/*! Subscribed routes for \ref stasis_cache_update messages */
	struct route_table *cache_routes;
	/*! Route of last resort */
	struct stasis_message_route default_route;
};

static void router_dtor(void *obj)
{
	struct stasis_message_router *router = obj;

	ast_assert(!stasis_subscription_is_subscribed(router->subscription));
	ast_assert(stasis_subscription_is_done(router->subscription));
	router->subscription = NULL;

	ast_free(router->routes);
	router->routes = NULL;

	ast_free(router->cache_routes);
	router->cache_routes = NULL;
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
		route = table_find_route(router->cache_routes, update->type);
	}

	if (route == NULL) {
		/* Find a regular route */
		route = table_find_route(router->routes, type);
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

struct stasis_message_router *stasis_message_router_create(
	struct stasis_topic *topic)
{
	RAII_VAR(struct stasis_message_router *, router, NULL, ao2_cleanup);

	router = ao2_alloc(sizeof(*router), router_dtor);
	if (!router) {
		return NULL;
	}

	router->routes = ast_calloc(1, sizeof(*router->routes));
	if (!router->routes) {
		return NULL;
	}

	router->cache_routes = ast_calloc(1, sizeof(*router->cache_routes));
	if (!router->cache_routes) {
		return NULL;
	}

	router->subscription = stasis_subscribe(topic, router_dispatch, router);
	if (!router->subscription) {
		return NULL;
	}

	ao2_ref(router, +1);
	return router;
}

void stasis_message_router_unsubscribe(struct stasis_message_router *router)
{
	if (!router) {
		return;
	}

	stasis_unsubscribe(router->subscription);
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

int stasis_message_router_add(struct stasis_message_router *router,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data)
{
	SCOPED_AO2LOCK(lock, router);
	return table_add_route(&router->routes, message_type, callback, data);
}

int stasis_message_router_add_cache_update(struct stasis_message_router *router,
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback, void *data)
{
	SCOPED_AO2LOCK(lock, router);
	return table_add_route(&router->cache_routes, message_type, callback, data);
}

void stasis_message_router_remove(struct stasis_message_router *router,
	struct stasis_message_type *message_type)
{
	SCOPED_AO2LOCK(lock, router);
	table_remove_route(router->routes, message_type);
}

void stasis_message_router_remove_cache_update(
	struct stasis_message_router *router,
	struct stasis_message_type *message_type)
{
	SCOPED_AO2LOCK(lock, router);
	table_remove_route(router->cache_routes, message_type);
}

int stasis_message_router_set_default(struct stasis_message_router *router,
				      stasis_subscription_cb callback,
				      void *data)
{
	SCOPED_AO2LOCK(lock, router);
	router->default_route.callback = callback;
	router->default_route.data = data;
	/* While this implementation can never fail, it used to be able to */
	return 0;
}
