/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/devicestate.h"

static struct stasis_forward *topic_forwarder;

static int unload_module(void)
{
	topic_forwarder = stasis_forward_cancel(topic_forwarder);
	return 0;
}

static int load_module(void)
{
	struct stasis_topic *manager_topic;

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return AST_MODULE_LOAD_DECLINE;
	}

	topic_forwarder = stasis_forward_all(ast_device_state_topic_all(), manager_topic);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Manager Device State Topic Forwarder",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_DEVSTATE_CONSUMER,
	);
