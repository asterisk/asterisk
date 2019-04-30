/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Schmooze Com, Inc.
 *
 * Jason Parker <jason.parker@schmoozecom.com>
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

#include "asterisk/mwi.h"
#include "asterisk/devicestate.h"
#include "asterisk/module.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis.h"

static struct stasis_subscription *mwi_sub;

static void mwi_update_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *msg)
{
	struct ast_mwi_state *mwi_state;

	if (ast_mwi_state_type() != stasis_message_type(msg)) {
		return;
	}

	mwi_state = stasis_message_data(msg);
	if (!mwi_state) {
		return;
	}

	if (mwi_state->new_msgs > 0) {
		ast_debug(1, "Sending inuse devstate change for MWI:%s\n", mwi_state->uniqueid);
		ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "MWI:%s", mwi_state->uniqueid);
	} else {
		ast_debug(1, "Sending not inuse devstate change for MWI:%s\n", mwi_state->uniqueid);
		ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "MWI:%s", mwi_state->uniqueid);
	}
}

static int mwi_cached_cb(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	mwi_update_cb(NULL, mwi_sub, msg);

	return 0;
}

static int unload_module(void)
{
	mwi_sub = stasis_unsubscribe(mwi_sub);

	return 0;
}

static int load_module(void)
{
	struct ao2_container *cached;

	if (!(mwi_sub = stasis_subscribe(ast_mwi_topic_all(), mwi_update_cb, NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stasis_subscription_accept_message_type(mwi_sub, ast_mwi_state_type())) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stasis_subscription_set_filter(mwi_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	cached = stasis_cache_dump(ast_mwi_state_cache(), NULL);
	if (!cached) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	ao2_callback(cached, OBJ_NODATA, mwi_cached_cb, NULL);
	ao2_ref(cached, -1);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "MWI Device State Subscriptions",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);
