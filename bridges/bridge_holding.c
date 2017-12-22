/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Bridging technology for storing channels in a bridge for
 *        the purpose of holding, parking, queues, and other such
 *        states where a channel may need to be in a bridge but not
 *        actually communicating with anything.
 *
 * \author Jonathan Rose <jrose@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/frame.h"
#include "asterisk/musiconhold.h"
#include "asterisk/format_cache.h"

enum holding_roles {
	HOLDING_ROLE_PARTICIPANT,
	HOLDING_ROLE_ANNOUNCER,
};

enum idle_modes {
	IDLE_MODE_NONE,
	IDLE_MODE_MOH,
	IDLE_MODE_RINGING,
	IDLE_MODE_SILENCE,
	IDLE_MODE_HOLD,
};

/*! \brief Structure which contains per-channel role information */
struct holding_channel {
	struct ast_silence_generator *silence_generator;
	enum holding_roles role;
	enum idle_modes idle_mode;
	/*! TRUE if the entertainment is started. */
	unsigned int entertainment_active:1;
};

typedef void (*deferred_cb)(struct ast_bridge_channel *bridge_channel);

struct deferred_data {
	/*! Deferred holding technology callback */
	deferred_cb callback;
};

static void deferred_action(struct ast_bridge_channel *bridge_channel, const void *payload, size_t payload_size);

/*!
 * \internal
 * \brief Defer an action to a bridge_channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to operate on.
 * \param callback action to defer.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int defer_action(struct ast_bridge_channel *bridge_channel, deferred_cb callback)
{
	struct deferred_data data = { .callback = callback };
	int res;

	res = ast_bridge_channel_queue_callback(bridge_channel, 0, deferred_action,
		&data, sizeof(data));
	if (res) {
		ast_log(LOG_WARNING, "Bridge %s: Could not defer action on %s.\n",
			bridge_channel->bridge->uniqueid, ast_channel_name(bridge_channel->chan));
	}
	return res;
}

/*!
 * \internal
 * \brief Setup participant idle mode from channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to setup idle mode.
 *
 * \return Nothing
 */
static void participant_idle_mode_setup(struct ast_bridge_channel *bridge_channel)
{
	const char *idle_mode = ast_bridge_channel_get_role_option(bridge_channel, "holding_participant", "idle_mode");
	struct holding_channel *hc = bridge_channel->tech_pvt;

	ast_assert(hc != NULL);

	if (ast_strlen_zero(idle_mode)) {
		hc->idle_mode = IDLE_MODE_MOH;
	} else if (!strcmp(idle_mode, "musiconhold")) {
		hc->idle_mode = IDLE_MODE_MOH;
	} else if (!strcmp(idle_mode, "ringing")) {
		hc->idle_mode = IDLE_MODE_RINGING;
	} else if (!strcmp(idle_mode, "none")) {
		hc->idle_mode = IDLE_MODE_NONE;
	} else if (!strcmp(idle_mode, "silence")) {
		hc->idle_mode = IDLE_MODE_SILENCE;
	} else if (!strcmp(idle_mode, "hold")) {
		hc->idle_mode = IDLE_MODE_HOLD;
	} else {
		/* Invalid idle mode requested. */
		ast_debug(1, "channel %s idle mode '%s' doesn't match any defined idle mode\n",
			ast_channel_name(bridge_channel->chan), idle_mode);
		ast_assert(0);
	}
}

static void participant_entertainment_stop(struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;

	ast_assert(hc != NULL);

	if (!hc->entertainment_active) {
		/* Already stopped */
		return;
	}
	hc->entertainment_active = 0;

	switch (hc->idle_mode) {
	case IDLE_MODE_MOH:
		ast_moh_stop(bridge_channel->chan);
		break;
	case IDLE_MODE_RINGING:
		ast_indicate(bridge_channel->chan, -1);
		break;
	case IDLE_MODE_NONE:
		break;
	case IDLE_MODE_SILENCE:
		if (hc->silence_generator) {
			ast_channel_stop_silence_generator(bridge_channel->chan, hc->silence_generator);
			hc->silence_generator = NULL;
		}
		break;
	case IDLE_MODE_HOLD:
		ast_indicate(bridge_channel->chan, AST_CONTROL_UNHOLD);
		break;
	}
}

static void participant_reaction_announcer_join(struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *chan;

	chan = bridge_channel->chan;
	participant_entertainment_stop(bridge_channel);
	if (ast_set_write_format(chan, ast_format_slin)) {
		ast_log(LOG_WARNING, "Could not make participant %s compatible.\n", ast_channel_name(chan));
	}
}

/* This should only be called on verified holding_participants. */
static void participant_entertainment_start(struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;
	const char *moh_class;
	size_t moh_length;

	ast_assert(hc != NULL);

	if (hc->entertainment_active) {
		/* Already started */
		return;
	}
	hc->entertainment_active = 1;

	participant_idle_mode_setup(bridge_channel);
	switch(hc->idle_mode) {
	case IDLE_MODE_MOH:
		moh_class = ast_bridge_channel_get_role_option(bridge_channel, "holding_participant", "moh_class");
		if (ast_moh_start(bridge_channel->chan, moh_class, NULL)) {
			ast_log(LOG_WARNING, "Failed to start moh, starting silence generator instead\n");
			hc->idle_mode = IDLE_MODE_SILENCE;
			hc->silence_generator = ast_channel_start_silence_generator(bridge_channel->chan);
		}
		break;
	case IDLE_MODE_RINGING:
		ast_indicate(bridge_channel->chan, AST_CONTROL_RINGING);
		break;
	case IDLE_MODE_NONE:
		break;
	case IDLE_MODE_SILENCE:
		hc->silence_generator = ast_channel_start_silence_generator(bridge_channel->chan);
		break;
	case IDLE_MODE_HOLD:
		moh_class = ast_bridge_channel_get_role_option(bridge_channel, "holding_participant", "moh_class");
		moh_length = moh_class ? strlen(moh_class + 1) : 0;
		ast_indicate_data(bridge_channel->chan, AST_CONTROL_HOLD, moh_class, moh_length);
		break;
	}
}

static void handle_participant_join(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *announcer_channel)
{
	struct ast_channel *us = bridge_channel->chan;

	/* If the announcer channel isn't present, we need to set up ringing, music on hold, or whatever. */
	if (!announcer_channel) {
		defer_action(bridge_channel, participant_entertainment_start);
		return;
	}

	/* We need to get compatible with the announcer. */
	if (ast_set_write_format(us, ast_format_slin)) {
		ast_log(LOG_WARNING, "Could not make participant %s compatible.\n", ast_channel_name(us));
	}
}

static int holding_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *other_channel;
	struct ast_bridge_channel *announcer_channel;
	struct holding_channel *hc;
	struct ast_channel *us = bridge_channel->chan; /* The joining channel */

	ast_assert(bridge_channel->tech_pvt == NULL);

	if (!(hc = ast_calloc(1, sizeof(*hc)))) {
		return -1;
	}

	bridge_channel->tech_pvt = hc;

	/* The bridge pvt holds the announcer channel if we have one. */
	announcer_channel = bridge->tech_pvt;

	if (ast_bridge_channel_has_role(bridge_channel, "announcer")) {
		if (announcer_channel) {
			/* Another announcer already exists. */
			bridge_channel->tech_pvt = NULL;
			ast_free(hc);
			ast_log(LOG_WARNING, "Bridge %s: Channel %s tried to be an announcer.  Bridge already has one.\n",
				bridge->uniqueid, ast_channel_name(bridge_channel->chan));
			return -1;
		}

		bridge->tech_pvt = bridge_channel;
		hc->role = HOLDING_ROLE_ANNOUNCER;

		/* The announcer should always be made compatible with signed linear */
		if (ast_set_read_format(us, ast_format_slin)) {
			ast_log(LOG_ERROR, "Could not make announcer %s compatible.\n", ast_channel_name(us));
		}

		/* Make everyone listen to the announcer. */
		AST_LIST_TRAVERSE(&bridge->channels, other_channel, entry) {
			/* Skip the reaction if we are the channel in question */
			if (bridge_channel == other_channel) {
				continue;
			}
			defer_action(other_channel, participant_reaction_announcer_join);
		}

		return 0;
	}

	hc->role = HOLDING_ROLE_PARTICIPANT;
	handle_participant_join(bridge_channel, announcer_channel);
	return 0;
}

static void participant_reaction_announcer_leave(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_restore_formats(bridge_channel);
	participant_entertainment_start(bridge_channel);
}

static void holding_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *other_channel;
	struct holding_channel *hc = bridge_channel->tech_pvt;

	if (!hc) {
		return;
	}

	switch (hc->role) {
	case HOLDING_ROLE_ANNOUNCER:
		/* The announcer is leaving */
		bridge->tech_pvt = NULL;

		/* Reset the other channels back to moh/ringing. */
		AST_LIST_TRAVERSE(&bridge->channels, other_channel, entry) {
			defer_action(other_channel, participant_reaction_announcer_leave);
		}
		break;
	default:
		/* Nothing needs to react to its departure. */
		participant_entertainment_stop(bridge_channel);
		break;
	}
	bridge_channel->tech_pvt = NULL;
	ast_free(hc);
}

static int holding_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct holding_channel *hc = bridge_channel ? bridge_channel->tech_pvt : NULL;

	/* If there is no tech_pvt, then the channel failed to allocate one when it joined and is borked. Don't listen to him. */
	if (!hc) {
		/* "Accept" the frame and discard it. */
		return 0;
	}

	switch (hc->role) {
	case HOLDING_ROLE_ANNOUNCER:
		/* Write the frame to all other channels if any. */
		ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
		break;
	default:
		/* "Accept" the frame and discard it. */
		break;
	}

	return 0;
}

static void holding_bridge_suspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;

	if (!hc) {
		return;
	}

	switch (hc->role) {
	case HOLDING_ROLE_PARTICIPANT:
		participant_entertainment_stop(bridge_channel);
		break;
	default:
		break;
	}
}

static void holding_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;
	struct ast_bridge_channel *announcer_channel = bridge->tech_pvt;

	if (!hc) {
		return;
	}

	switch (hc->role) {
	case HOLDING_ROLE_PARTICIPANT:
		if (announcer_channel) {
			/* There is an announcer channel in the bridge. */
			break;
		}
		/* We need to restart the entertainment. */
		participant_entertainment_start(bridge_channel);
		break;
	default:
		break;
	}
}

static struct ast_bridge_technology holding_bridge = {
	.name = "holding_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_HOLDING,
	.preference = AST_BRIDGE_PREFERENCE_BASE_HOLDING,
	.write = holding_bridge_write,
	.join = holding_bridge_join,
	.leave = holding_bridge_leave,
	.suspend = holding_bridge_suspend,
	.unsuspend = holding_bridge_unsuspend,
};

/*!
 * \internal
 * \brief Deferred action to start/stop participant entertainment.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to operate on.
 * \param payload Data to pass to the callback. (NULL if none).
 * \param payload_size Size of the payload if payload is non-NULL.  A number otherwise.
 *
 * \return Nothing
 */
static void deferred_action(struct ast_bridge_channel *bridge_channel, const void *payload, size_t payload_size)
{
	const struct deferred_data *data = payload;

	ast_bridge_channel_lock_bridge(bridge_channel);
	if (bridge_channel->bridge->technology != &holding_bridge
		|| !bridge_channel->tech_pvt) {
		/* Not valid anymore. */
		ast_bridge_unlock(bridge_channel->bridge);
		return;
	}
	data->callback(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

static int unload_module(void)
{
	ast_bridge_technology_unregister(&holding_bridge);
	return 0;
}

static int load_module(void)
{
	if (ast_bridge_technology_register(&holding_bridge)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Holding bridge module");
