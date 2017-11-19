/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Matthew Jordan <mjordan@digium.com>
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
 * \brief Statsd Endpoint stats.
 *
 * This module subscribes to Stasis endpoints and send statistics
 * based on their state.
 *
 * \author Matthew Jordan <mjordan@digium.com>
 * \since 13.7.0
 */

/*** MODULEINFO
	<depend>res_statsd</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/statsd.h"

/*! Stasis message router */
static struct stasis_message_router *router;

static void update_endpoint_state(struct ast_endpoint_snapshot *snapshot, const char *delta)
{
	switch (snapshot->state) {
	case AST_ENDPOINT_UNKNOWN:
		ast_statsd_log_string("endpoints.state.unknown", AST_STATSD_GAUGE, delta, 1.0);
		break;
	case AST_ENDPOINT_OFFLINE:
		ast_statsd_log_string("endpoints.state.offline", AST_STATSD_GAUGE, delta, 1.0);
		break;
	case AST_ENDPOINT_ONLINE:
		ast_statsd_log_string("endpoints.state.online", AST_STATSD_GAUGE, delta, 1.0);
		break;
	}
}

static void handle_endpoint_update(struct ast_endpoint_snapshot *old_snapshot, struct ast_endpoint_snapshot *new_snapshot)
{
	if (!old_snapshot && new_snapshot) {
		ast_statsd_log_string("endpoints.count", AST_STATSD_GAUGE, "+1", 1.0);
		update_endpoint_state(new_snapshot, "+1");
	} else if (old_snapshot && !new_snapshot) {
		ast_statsd_log_string("endpoints.count", AST_STATSD_GAUGE, "-1", 1.0);
		update_endpoint_state(old_snapshot, "-1");
	} else {
		if (old_snapshot->state != new_snapshot->state) {
			update_endpoint_state(old_snapshot, "-1");
			update_endpoint_state(new_snapshot, "+1");
		}
		ast_statsd_log_full_va("endpoints.%s.%s.channels", AST_STATSD_GAUGE, new_snapshot->num_channels, 1.0,
			new_snapshot->tech, new_snapshot->resource);
	}
}

static void cache_update_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_cache_update *update = stasis_message_data(message);
	struct ast_endpoint_snapshot *old_snapshot;
	struct ast_endpoint_snapshot *new_snapshot;

	if (ast_endpoint_snapshot_type() != update->type) {
		return;
	}

	old_snapshot = stasis_message_data(update->old_snapshot);
	new_snapshot = stasis_message_data(update->new_snapshot);

	handle_endpoint_update(old_snapshot, new_snapshot);
}

static int dump_cache_load(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	struct ast_endpoint_snapshot *snapshot = stasis_message_data(msg);

	handle_endpoint_update(NULL, snapshot);

	return 0;
}

static int dump_cache_unload(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	struct ast_endpoint_snapshot *snapshot = stasis_message_data(msg);

	handle_endpoint_update(snapshot, NULL);

	return 0;
}

static int load_module(void)
{
	struct ao2_container *endpoints;

	router = stasis_message_router_create(ast_endpoint_topic_all_cached());
	if (!router) {
		return AST_MODULE_LOAD_DECLINE;
	}
	stasis_message_router_add(router, stasis_cache_update_type(), cache_update_cb, NULL);

	endpoints = stasis_cache_dump(ast_endpoint_cache(), ast_endpoint_snapshot_type());
	if (endpoints) {
		ao2_callback(endpoints, OBJ_MULTIPLE | OBJ_NODATA | OBJ_NOLOCK, dump_cache_load, NULL);
		ao2_ref(endpoints, -1);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	struct ao2_container *endpoints;

	endpoints = stasis_cache_dump(ast_endpoint_cache(), ast_endpoint_snapshot_type());
	if (endpoints) {
		ao2_callback(endpoints, OBJ_MULTIPLE | OBJ_NODATA | OBJ_NOLOCK, dump_cache_unload, NULL);
		ao2_ref(endpoints, -1);
	}

	stasis_message_router_unsubscribe_and_join(router);
	router = NULL;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Endpoint statistics",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_statsd"
	);
