/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jason Parker <jparker@digium.com>
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
 * \brief System AMI event handling
 *
 * \author Jason Parker <jparker@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis_system.h"

/*! \brief The \ref stasis subscription returned by the forwarding of the system topic
 * to the manager topic
 */
static struct stasis_forward *topic_forwarder;

static void manager_system_shutdown(void)
{
	stasis_forward_cancel(topic_forwarder);
	topic_forwarder = NULL;
}

int manager_system_init(void)
{
	int ret = 0;
	struct stasis_topic *manager_topic;
	struct stasis_topic *system_topic;
	struct stasis_message_router *message_router;

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return -1;
	}
	message_router = ast_manager_get_message_router();
	if (!message_router) {
		return -1;
	}
	system_topic = ast_system_topic();
	if (!system_topic) {
		return -1;
	}

	topic_forwarder = stasis_forward_all(system_topic, manager_topic);
	if (!topic_forwarder) {
		return -1;
	}

	ast_register_cleanup(manager_system_shutdown);

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_system_shutdown();
		return -1;
	}

	return 0;
}
