/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief The Asterisk Management Interface - AMI (endpoint handling)
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author David M. Lee, II <dlee@digium.com>
 *
  */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/manager.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_endpoints.h"

static struct stasis_message_router *endpoint_router;

/*! \brief The \ref stasis subscription returned by the forwarding of the endpoint topic
 * to the manager topic
 */
static struct stasis_subscription *topic_forwarder;

static void manager_endpoints_shutdown(void)
{
	stasis_message_router_unsubscribe_and_join(endpoint_router);
	endpoint_router = NULL;

	stasis_unsubscribe(topic_forwarder);
	topic_forwarder = NULL;
}

int manager_endpoints_init(void)
{
	struct stasis_topic *manager_topic;
	struct stasis_topic *endpoint_topic;
	struct stasis_message_router *message_router;
	int ret = 0;

	if (endpoint_router) {
		/* Already initialized */
		return 0;
	}

	ast_register_atexit(manager_endpoints_shutdown);

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return -1;
	}
	message_router = ast_manager_get_message_router();
	if (!message_router) {
		return -1;
	}
	endpoint_topic = stasis_caching_get_topic(ast_endpoint_topic_all_cached());
	if (!endpoint_topic) {
		return -1;
	}

	topic_forwarder = stasis_forward_all(endpoint_topic, manager_topic);
	if (!topic_forwarder) {
		return -1;
	}

	endpoint_router = stasis_message_router_create(endpoint_topic);

	if (!endpoint_router) {
		return -1;
	}

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_endpoints_shutdown();
		return -1;
	}

	return 0;
}

