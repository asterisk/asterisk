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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_technology.h"
#include "asterisk/frame.h"
#include "asterisk/musiconhold.h"

enum role_flags {
	HOLDING_ROLE_PARTICIPANT = (1 << 0),
	HOLDING_ROLE_ANNOUNCER = (1 << 1),
};

/* BUGBUG Add IDLE_MODE_HOLD option to put channel on hold using AST_CONTROL_HOLD/AST_CONTROL_UNHOLD while in bridge */
/* BUGBUG Add IDLE_MODE_SILENCE to send silence media frames to channel while in bridge (uses a silence generator) */
/* BUGBUG A channel without the holding_participant role will assume IDLE_MODE_MOH with the default music class. */
enum idle_modes {
	IDLE_MODE_NONE = 0,
	IDLE_MODE_MOH,
	IDLE_MODE_RINGING,
};

/*! \brief Structure which contains per-channel role information */
struct holding_channel {
	struct ast_flags holding_roles;
	enum idle_modes idle_mode;
};

static void participant_stop_hold_audio(struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;
	if (!hc) {
		return;
	}

	switch (hc->idle_mode) {
	case IDLE_MODE_MOH:
		ast_moh_stop(bridge_channel->chan);
		break;
	case IDLE_MODE_RINGING:
		ast_indicate(bridge_channel->chan, -1);
		break;
	case IDLE_MODE_NONE:
		break;
	}
}

static void participant_reaction_announcer_join(struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *chan;
	chan = bridge_channel->chan;
	participant_stop_hold_audio(bridge_channel);
	if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Could not make participant %s compatible.\n", ast_channel_name(chan));
	}
}

/* This should only be called on verified holding_participants. */
static void participant_start_hold_audio(struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;
	const char *moh_class;

	if (!hc) {
		return;
	}

	switch(hc->idle_mode) {
	case IDLE_MODE_MOH:
		moh_class = ast_bridge_channel_get_role_option(bridge_channel, "holding_participant", "moh_class");
		ast_moh_start(bridge_channel->chan, ast_strlen_zero(moh_class) ? NULL : moh_class, NULL);
		break;
	case IDLE_MODE_RINGING:
		ast_indicate(bridge_channel->chan, AST_CONTROL_RINGING);
		break;
	case IDLE_MODE_NONE:
		break;
	}
}

static void handle_participant_join(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *announcer_channel)
{
	struct ast_channel *us = bridge_channel->chan;
	struct holding_channel *hc = bridge_channel->tech_pvt;
	const char *idle_mode = ast_bridge_channel_get_role_option(bridge_channel, "holding_participant", "idle_mode");


	if (!hc) {
		return;
	}

	if (ast_strlen_zero(idle_mode)) {
		hc->idle_mode = IDLE_MODE_NONE;
	} else if (!strcmp(idle_mode, "musiconhold")) {
		hc->idle_mode = IDLE_MODE_MOH;
	} else if (!strcmp(idle_mode, "ringing")) {
		hc->idle_mode = IDLE_MODE_RINGING;
	} else {
		ast_debug(2, "channel %s idle mode '%s' doesn't match any expected idle mode\n", ast_channel_name(us), idle_mode);
	}

	/* If the announcer channel isn't present, we need to set up ringing, music on hold, or whatever. */
	if (!announcer_channel) {
		participant_start_hold_audio(bridge_channel);
		return;
	}

	/* If it is present though, we need to establish compatability. */
	if (ast_set_write_format_by_id(us, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Could not make participant %s compatible.\n", ast_channel_name(us));
	}
}

static int holding_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *other_channel;
	struct ast_bridge_channel *announcer_channel;
	struct holding_channel *hc;
	struct ast_channel *us = bridge_channel->chan; /* The joining channel */

	if (!(hc = ast_calloc(1, sizeof(*hc)))) {
		return -1;
	}

	bridge_channel->tech_pvt = hc;

	/* The bridge pvt holds the announcer channel if we have one. */
	announcer_channel = bridge->tech_pvt;

	if (ast_bridge_channel_has_role(bridge_channel, "announcer")) {
		/* If another announcer already exists, scrap the holding channel struct so we know to ignore it in the future */
		if (announcer_channel) {
			bridge_channel->tech_pvt = NULL;
			ast_free(hc);
			ast_log(LOG_WARNING, "A second announcer channel %s attempted to enter a holding bridge.\n",
				ast_channel_name(announcer_channel->chan));
			return -1;
		}

		bridge->tech_pvt = bridge_channel;
		ast_set_flag(&hc->holding_roles, HOLDING_ROLE_ANNOUNCER);

		/* The announcer should always be made compatible with signed linear */
		if (ast_set_read_format_by_id(us, AST_FORMAT_SLINEAR)) {
			ast_log(LOG_ERROR, "Could not make announcer %s compatible.\n", ast_channel_name(us));
		}

		/* Make everyone compatible. While we are at it we should stop music on hold and ringing. */
		AST_LIST_TRAVERSE(&bridge->channels, other_channel, entry) {
			/* Skip the reaction if we are the channel in question */
			if (bridge_channel == other_channel) {
				continue;
			}
			participant_reaction_announcer_join(other_channel);
		}

		return 0;
	}

	/* If the entering channel isn't an announcer then we need to setup it's properties and put it in its holding state if necessary */
	ast_set_flag(&hc->holding_roles, HOLDING_ROLE_PARTICIPANT);
	handle_participant_join(bridge_channel, announcer_channel);
	return 0;
}

static void participant_reaction_announcer_leave(struct ast_bridge_channel *bridge_channel)
{
	struct holding_channel *hc = bridge_channel->tech_pvt;

	if (!hc) {
		/* We are dealing with a channel that failed to join properly. Skip it. */
		return;
	}

	ast_bridge_channel_restore_formats(bridge_channel);
	if (ast_test_flag(&hc->holding_roles, HOLDING_ROLE_PARTICIPANT)) {
		participant_start_hold_audio(bridge_channel);
	}
}

static void holding_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *other_channel;
	struct holding_channel *hc = bridge_channel->tech_pvt;

	if (!hc) {
		return;
	}

	if (!ast_test_flag(&hc->holding_roles, HOLDING_ROLE_ANNOUNCER)) {
		/* It's not an announcer so nothing needs to react to its departure. Just free the tech_pvt. */
		if (!bridge->tech_pvt) {
			/* Since no announcer is in the channel, we may be playing MOH/ringing. Stop that. */
			participant_stop_hold_audio(bridge_channel);
		}
		ast_free(hc);
		bridge_channel->tech_pvt = NULL;
		return;
	}

	/* When the announcer leaves, the other channels should reset their formats and go back to moh/ringing */
	AST_LIST_TRAVERSE(&bridge->channels, other_channel, entry) {
		participant_reaction_announcer_leave(other_channel);
	}

	/* Since the announcer is leaving, we should clear the tech_pvt pointing to it */
	bridge->tech_pvt = NULL;

	ast_free(hc);
	bridge_channel->tech_pvt = NULL;
}

static int holding_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *cur;
	struct holding_channel *hc = bridge_channel->tech_pvt;

	/* If there is no tech_pvt, then the channel failed to allocate one when it joined and is borked. Don't listen to him. */
	if (!hc) {
		return -1;
	}

	/* If we aren't an announcer, we never have any business writing anything. */
	if (!ast_test_flag(&hc->holding_roles, HOLDING_ROLE_ANNOUNCER)) {
		return -1;
	}

	/* Ok, so we are the announcer and there are one or more people available to receive our writes. Let's do it. */
	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (bridge_channel == cur || !cur->tech_pvt) {
			continue;
		}

		ast_bridge_channel_queue_frame(cur, frame);
	}

	return 0;
}

static struct ast_bridge_technology holding_bridge = {
	.name = "holding_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_HOLDING,
	.preference = AST_BRIDGE_PREFERENCE_BASE_HOLDING,
	.write = holding_bridge_write,
	.join = holding_bridge_join,
	.leave = holding_bridge_leave,
};

static int unload_module(void)
{
	ast_format_cap_destroy(holding_bridge.format_capabilities);
	return ast_bridge_technology_unregister(&holding_bridge);
}

static int load_module(void)
{
	if (!(holding_bridge.format_capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_add_all_by_type(holding_bridge.format_capabilities, AST_FORMAT_TYPE_AUDIO);
	ast_format_cap_add_all_by_type(holding_bridge.format_capabilities, AST_FORMAT_TYPE_VIDEO);
	ast_format_cap_add_all_by_type(holding_bridge.format_capabilities, AST_FORMAT_TYPE_TEXT);

	return ast_bridge_technology_register(&holding_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Holding bridge module");

