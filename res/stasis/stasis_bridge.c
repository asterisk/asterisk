/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
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
 * \brief Stasis bridge subclass.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

#include "asterisk/bridge.h"
#include "asterisk/bridge_after.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/bridge_features.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_channels.h"
#include "stasis_bridge.h"
#include "control.h"
#include "command.h"
#include "app.h"
#include "asterisk/stasis_app.h"
#include "asterisk/pbx.h"

/* ------------------------------------------------------------------- */

static struct ast_bridge_methods bridge_stasis_v_table;

static void bridge_stasis_run_cb(struct ast_channel *chan, void *data)
{
	RAII_VAR(char *, app_name, NULL, ast_free);
	struct ast_app *app_stasis;

	/* Take ownership of the swap_app memory from the datastore */
	app_name = app_get_replace_channel_app(chan);
	if (!app_name) {
		ast_log(LOG_ERROR, "Failed to get app name for %s (%p)\n", ast_channel_name(chan), chan);
		return;
	}

	/* find Stasis() */
	app_stasis = pbx_findapp("Stasis");
	if (!app_stasis) {
		ast_log(LOG_WARNING, "Could not find application (Stasis)\n");
		return;
	}

	if (ast_check_hangup_locked(chan)) {
		/* channel hungup, don't run Stasis() */
		return;
	}

	/* run Stasis() */
	pbx_exec(chan, app_stasis, app_name);
}

struct defer_bridge_add_obj {
	/*! Bridge to join (has ref) */
	struct ast_bridge *bridge;
	/*!
	 * \brief Channel to swap with in the bridge. (has ref)
	 *
	 * \note NULL if not swapping with a channel.
	 */
	struct ast_channel *swap;
};

static void defer_bridge_add_dtor(void *obj)
{
	struct defer_bridge_add_obj *defer = obj;

	ao2_cleanup(defer->bridge);
	ast_channel_cleanup(defer->swap);
}

static int defer_bridge_add(
	struct stasis_app_control *control,
	struct ast_channel *chan, void *obj)
{
	struct defer_bridge_add_obj *defer = obj;

	return control_swap_channel_in_bridge(control, defer->bridge, chan, defer->swap);
}

static void bridge_stasis_queue_join_action(struct ast_bridge *self,
	struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct defer_bridge_add_obj *defer;

	defer = ao2_alloc_options(sizeof(*defer), defer_bridge_add_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!defer) {
		return;
	}
	ao2_ref(self, +1);
	defer->bridge = self;
	if (swap) {
		ast_channel_ref(swap->chan);
		defer->swap = swap->chan;
	}

	ast_channel_lock(bridge_channel->chan);
	command_prestart_queue_command(bridge_channel->chan, defer_bridge_add,
		defer, __ao2_cleanup);
	ast_channel_unlock(bridge_channel->chan);
}

/*!
 * \internal
 * \brief Peek at channel before it is pushed into bridge
 * \since 13.2.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel should not be pushed.
 */
static int bridge_stasis_push_peek(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct stasis_app_control *swap_control;
	struct ast_channel_snapshot *to_be_replaced;

	if (!swap) {
		goto done;
	}

	swap_control = stasis_app_control_find_by_channel(swap->chan);
	if (!swap_control) {
		ast_log(LOG_ERROR,"Failed to find stasis app control for swapped channel %s\n", ast_channel_name(swap->chan));
		return -1;
	}
	to_be_replaced = ast_channel_snapshot_get_latest(ast_channel_uniqueid(swap->chan));

	ast_debug(3, "Copying stasis app name %s from %s to %s\n", stasis_app_name(control_app(swap_control)),
		ast_channel_name(swap->chan), ast_channel_name(bridge_channel->chan));

	ast_channel_lock(bridge_channel->chan);

	/* copy the app name from the swap channel */
	app_set_replace_channel_app(bridge_channel->chan, stasis_app_name(control_app(swap_control)));

	/* set the replace channel snapshot */
	app_set_replace_channel_snapshot(bridge_channel->chan, to_be_replaced);

	ast_channel_unlock(bridge_channel->chan);

	ao2_ref(swap_control, -1);
	ao2_cleanup(to_be_replaced);

done:
	return ast_bridge_base_v_table.push_peek(self, bridge_channel, swap);
}

/*!
 * \internal
 * \brief Push this channel into the Stasis bridge.
 * \since 12.5.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 */
static int bridge_stasis_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct stasis_app_control *control = stasis_app_control_find_by_channel(bridge_channel->chan);

	if (!control && !stasis_app_channel_is_internal(bridge_channel->chan)) {
		/* channel not in Stasis(), get it there */
		ast_debug(1, "Bridge %s: pushing non-stasis %p(%s) setup to come back in under stasis\n",
			self->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));

		/* Attach after-bridge callback and pass ownership of swap_app to it */
		if (ast_bridge_set_after_callback(bridge_channel->chan,
			bridge_stasis_run_cb, NULL, NULL)) {
			ast_log(LOG_ERROR,
				"Failed to set after bridge callback for bridge %s non-stasis push of %s\n",
				self->uniqueid, ast_channel_name(bridge_channel->chan));
			return -1;
		}

		bridge_stasis_queue_join_action(self, bridge_channel, swap);

		/* Return -1 so the push fails and the after-bridge callback gets called
		 * This keeps the bridging framework from putting the channel into the bridge
		 * until the Stasis thread gets started, and then the channel is put into the bridge.
		 */
		return -1;
	}
	ao2_cleanup(control);

	/*
	 * If going into a holding bridge, default the role to participant, if
	 * it has no compatible role currently
	 */
	if ((self->technology->capabilities & AST_BRIDGE_CAPABILITY_HOLDING)
	    && !ast_channel_has_role(bridge_channel->chan, "announcer")
	    && !ast_channel_has_role(bridge_channel->chan, "holding_participant")) {
		if (ast_channel_add_bridge_role(bridge_channel->chan, "holding_participant")) {
			ast_log(LOG_ERROR, "Failed to set holding participant on %s\n", ast_channel_name(bridge_channel->chan));
			return -1;
		}

		if (ast_channel_set_bridge_role_option(bridge_channel->chan, "holding_participant", "idle_mode", "none")) {
			ast_log(LOG_ERROR, "Failed to set holding participant mode on %s\n", ast_channel_name(bridge_channel->chan));
			return -1;
		}
	}

	if (self->allowed_capabilities & STASIS_BRIDGE_MIXING_CAPABILITIES) {
		ast_bridge_channel_update_linkedids(bridge_channel, swap);
		if (ast_test_flag(&self->feature_flags, AST_BRIDGE_FLAG_SMART)) {
			ast_bridge_channel_update_accountcodes(bridge_channel, swap);
		}
	}

	return ast_bridge_base_v_table.push(self, bridge_channel, swap);
}

static int bridge_stasis_moving(struct ast_bridge_channel *bridge_channel, void *hook_pvt,
		struct ast_bridge *src, struct ast_bridge *dst)
{
	if (src->v_table == &bridge_stasis_v_table &&
			dst->v_table != &bridge_stasis_v_table) {
		struct stasis_app_control *control;
		struct ast_channel *chan;

		chan = bridge_channel->chan;
		ast_assert(chan != NULL);

		control = stasis_app_control_find_by_channel(chan);
		if (!control) {
			return -1;
		}

		stasis_app_channel_set_stasis_end_published(chan);
		app_send_end_msg(control_app(control), chan);
		ao2_ref(control, -1);
	}

	return -1;
}

/*!
 * \internal
 * \brief Pull this channel from the Stasis bridge.
 * \since 12.5.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \note On entry, self is already locked.
 */
static void bridge_stasis_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	if ((self->allowed_capabilities & STASIS_BRIDGE_MIXING_CAPABILITIES)
		&& ast_test_flag(&self->feature_flags, AST_BRIDGE_FLAG_SMART)) {
		ast_bridge_channel_update_accountcodes(NULL, bridge_channel);
	}

	if (self->technology->capabilities & AST_BRIDGE_CAPABILITY_HOLDING) {
		ast_channel_clear_bridge_roles(bridge_channel->chan);
	}

	ast_bridge_move_hook(bridge_channel->features, bridge_stasis_moving, NULL, NULL, 0);

	ast_bridge_base_v_table.pull(self, bridge_channel);
}

struct ast_bridge *bridge_stasis_new(uint32_t capabilities, unsigned int flags, const char *name, const char *id, enum ast_bridge_video_mode_type video_mode)
{
	void *bridge;

	bridge = bridge_alloc(sizeof(struct ast_bridge), &bridge_stasis_v_table);
	bridge = bridge_base_init(bridge, capabilities, flags, "Stasis", name, id);
	if (!bridge) {
		return NULL;
	}

	if (video_mode == AST_BRIDGE_VIDEO_MODE_SFU) {
		ast_bridge_set_sfu_video_mode(bridge);
		/* We require a minimum 5 seconds between video updates to stop floods from clients,
		 * this should rarely be changed but should become configurable in the future.
		 */
		ast_bridge_set_video_update_discard(bridge, 5);
	} else if (video_mode == AST_BRIDGE_VIDEO_MODE_SINGLE_SRC) {
		ast_bridge_set_single_src_video_mode(bridge, NULL);
	} else {
		ast_bridge_set_talker_src_video_mode(bridge);
	}

	bridge = bridge_register(bridge);

	return bridge;
}

void bridge_stasis_init(void)
{
	/* Setup the Stasis bridge subclass v_table. */
	bridge_stasis_v_table = ast_bridge_base_v_table;
	bridge_stasis_v_table.name = "stasis";
	bridge_stasis_v_table.push = bridge_stasis_push;
	bridge_stasis_v_table.pull = bridge_stasis_pull;
	bridge_stasis_v_table.push_peek = bridge_stasis_push_peek;
}
