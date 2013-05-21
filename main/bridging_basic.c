/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief Basic bridge class.  It is a subclass of struct ast_bridge.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_basic.h"
#include "asterisk/astobj2.h"

/* ------------------------------------------------------------------- */

static const struct ast_datastore_info dtmf_features_info = {
	.type = "bridge-dtmf-features",
	.destroy = ast_free_ptr,
};

int ast_bridge_features_ds_set(struct ast_channel *chan, struct ast_flags *flags)
{
	struct ast_datastore *datastore;
	struct ast_flags *ds_flags;

	datastore = ast_channel_datastore_find(chan, &dtmf_features_info, NULL);
	if (datastore) {
		ds_flags = datastore->data;
		*ds_flags = *flags;
		return 0;
	}

	datastore = ast_datastore_alloc(&dtmf_features_info, NULL);
	if (!datastore) {
		return -1;
	}

	ds_flags = ast_malloc(sizeof(*ds_flags));
	if (!ds_flags) {
		ast_datastore_free(datastore);
		return -1;
	}

	*ds_flags = *flags;
	datastore->data = ds_flags;
	ast_channel_datastore_add(chan, datastore);
	return 0;
}

struct ast_flags *ast_bridge_features_ds_get(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &dtmf_features_info, NULL);
	if (!datastore) {
		return NULL;
	}
	return datastore->data;
}

/*!
 * \internal
 * \brief Determine if we should dissolve the bridge from a hangup.
 * \since 12.0.0
 *
 * \param bridge The bridge that the channel is part of
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
static int basic_hangup_hook(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
/* BUGBUG Race condition.  If all parties but one hangup at the same time, the bridge may not be dissolved on the remaining party. */
	ast_bridge_channel_lock_bridge(bridge_channel);
	if (2 < bridge_channel->bridge->num_channels) {
		/* Just allow this channel to leave the multi-party bridge. */
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	}
	ast_bridge_unlock(bridge_channel->bridge);
	return 0;
}

/*!
 * \internal
 * \brief ast_bridge basic push method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 * \note Stub because of nothing to do.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_basic_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	if (ast_bridge_hangup_hook(bridge_channel->features, basic_hangup_hook, NULL, NULL, 1)
		|| ast_bridge_channel_setup_features(bridge_channel)) {
		return -1;
	}

	return ast_bridge_base_v_table.push(self, bridge_channel, swap);
}

struct ast_bridge_methods ast_bridge_basic_v_table;

struct ast_bridge *ast_bridge_basic_new(void)
{
	void *bridge;

	bridge = ast_bridge_alloc(sizeof(struct ast_bridge), &ast_bridge_basic_v_table);
	bridge = ast_bridge_base_init(bridge,
		AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_1TO1MIX
			| AST_BRIDGE_CAPABILITY_MULTIMIX,
		AST_BRIDGE_FLAG_DISSOLVE_HANGUP | AST_BRIDGE_FLAG_DISSOLVE_EMPTY
			| AST_BRIDGE_FLAG_SMART);
	bridge = ast_bridge_register(bridge);
	return bridge;
}

void ast_bridging_init_basic(void)
{
	/* Setup bridge basic subclass v_table. */
	ast_bridge_basic_v_table = ast_bridge_base_v_table;
	ast_bridge_basic_v_table.name = "basic";
	ast_bridge_basic_v_table.push = bridge_basic_push;
}
