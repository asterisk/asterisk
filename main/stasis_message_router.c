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

static void route_dtor(void *obj)
{
	struct stasis_message_route *route = obj;

	ao2_cleanup(route->message_type);
	route->message_type = NULL;
}

static int route_hash(const void *obj, const int flags)
{
	const struct stasis_message_route *route = obj;
	const struct stasis_message_type *message_type = (flags & OBJ_KEY) ? obj : route->message_type;

	return ast_str_hash(stasis_message_type_name(message_type));
}

static int route_cmp(void *obj, void *arg, int flags)
{
	const struct stasis_message_route *left = obj;
	const struct stasis_message_route *right = arg;
	const struct stasis_message_type *message_type = (flags & OBJ_KEY) ? arg : right->message_type;

	return (left->message_type == message_type) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \internal */
struct stasis_message_router {
	/*! Subscription to the upstream topic */
	struct stasis_subscription *subscription;
	/*! Subscribed routes */
	struct ao2_container *routes;
	/*! Route of last resort */
	struct stasis_message_route *default_route;
};

static void router_dtor(void *obj)
{
	struct stasis_message_router *router = obj;

	ast_assert(!stasis_subscription_is_subscribed(router->subscription));
	ast_assert(stasis_subscription_is_done(router->subscription));
	router->subscription = NULL;

	ao2_cleanup(router->routes);
	router->routes = NULL;

	ao2_cleanup(router->default_route);
	router->default_route = NULL;
}

static struct stasis_message_route *find_route(struct stasis_message_router *router, struct stasis_message_type *message_type)
{
	return ao2_find(router->routes, message_type, OBJ_KEY);
}

static void router_dispatch(void *data,
			    struct stasis_subscription *sub,
			    struct stasis_topic *topic,
			    struct stasis_message *message)
{
	struct stasis_message_router *router = data;
	RAII_VAR(struct stasis_message_router *, router_needs_cleanup, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_route *, route, NULL, ao2_cleanup);
	struct stasis_message_type *type = stasis_message_type(message);

	{
		SCOPED_AO2LOCK(lock, router);

		if (!(route = find_route(router, type))) {
			if ((route = router->default_route)) {
				ao2_ref(route, +1);
			}
		}
	}

	if (route) {
		route->callback(route->data, sub, topic, message);
	}

	if (stasis_subscription_final_message(sub, message)) {
		router_needs_cleanup = router;
		return;
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

	if (!(router->routes = ao2_container_alloc(7, route_hash, route_cmp))) {
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


static struct stasis_message_route *route_create(
	struct stasis_message_type *message_type,
	stasis_subscription_cb callback,
	void *data)
{
	RAII_VAR(struct stasis_message_route *, route, NULL, ao2_cleanup);

	route = ao2_alloc(sizeof(*route), route_dtor);
	if (!route) {
		return NULL;
	}

	if (message_type) {
		ao2_ref(message_type, +1);
	}
	route->message_type = message_type;
	route->callback = callback;
	route->data = data;

	ao2_ref(route, +1);
	return route;
}

static int add_route(struct stasis_message_router *router,
		     struct stasis_message_route *route)
{
	SCOPED_AO2LOCK(lock, router);
	RAII_VAR(struct stasis_message_route *, existing_route, NULL, ao2_cleanup);

	if ((existing_route = find_route(router, route->message_type))) {
		return -1;
	}

	ao2_link(router->routes, route);
	return 0;
}

int stasis_message_router_add(struct stasis_message_router *router,
			      struct stasis_message_type *message_type,
			      stasis_subscription_cb callback,
			      void *data)
{
	RAII_VAR(struct stasis_message_route *, route, NULL, ao2_cleanup);

	route = route_create(message_type, callback, data);
	if (!route) {
		return -1;
	}

	return add_route(router, route);
}

void stasis_message_router_remove(struct stasis_message_router *router,
			      struct stasis_message_type *message_type)
{
	SCOPED_AO2LOCK(lock, router);

	ao2_find(router->routes, message_type, OBJ_UNLINK | OBJ_NODATA | OBJ_KEY);
}

int stasis_message_router_set_default(struct stasis_message_router *router,
				      stasis_subscription_cb callback,
				      void *data)
{
	SCOPED_AO2LOCK(lock, router);
	ao2_cleanup(router->default_route);
	router->default_route = route_create(NULL, callback, data);
	return router->default_route ? 0 : -1;
}
