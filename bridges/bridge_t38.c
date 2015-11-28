/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Bridging module for maintaining T.38 state for faxing channels
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/frame.h"

/*! \brief The current state of the T.38 fax for the channels in our bridge */
struct t38_bridge_state {
	/* \brief First channel in the bridge */
	struct ast_bridge_channel *bc0;
	/*! \brief Second channel in the bridge */
	struct ast_bridge_channel *bc1;
	/*! \brief T.38 state of \c bc0 */
	enum ast_t38_state c0_state;
	/*! \brief T.38 state of \c bc1 */
	enum ast_t38_state c1_state;
};

static void t38_bridge_destroy(struct ast_bridge *bridge)
{
	struct t38_bridge_state *state = bridge->tech_pvt;

	ast_free(state);
	bridge->tech_pvt = NULL;
}

static int t38_bridge_create(struct ast_bridge *bridge)
{
	struct t38_bridge_state *state;

	state = ast_calloc(1, sizeof(*state));
	if (!state) {
		return -1;
	}

	bridge->tech_pvt = state;

	return 0;
}

static int t38_bridge_start(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bc0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *bc1 = AST_LIST_LAST(&bridge->channels);
	struct t38_bridge_state *state = bridge->tech_pvt;

	state->bc0 = bc0;
	state->bc1 = bc1;
	state->c0_state = ast_channel_get_t38_state(state->bc0->chan);
	state->c1_state = ast_channel_get_t38_state(state->bc1->chan);

	return 0;
}

static void send_termination_update(struct ast_bridge *bridge,  struct ast_bridge_channel *bridge_channel, enum ast_t38_state chan_state)
{
	/* Inform the other side that T.38 faxing is done */
	struct ast_control_t38_parameters parameters = { .request_response = 0, };

	if (!bridge_channel) {
		return;
	}

	ast_debug(5, "Bridge %s T.38: Current state of %s is %d\n",
		bridge->uniqueid, ast_channel_name(bridge_channel->chan), chan_state);
	if (chan_state == T38_STATE_NEGOTIATING) {
		parameters.request_response = AST_T38_REFUSED;
	} else if (chan_state == T38_STATE_NEGOTIATED) {
		parameters.request_response = AST_T38_TERMINATED;
	}

	if (parameters.request_response) {
		struct ast_frame f = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = AST_CONTROL_T38_PARAMETERS,
			.data.ptr = &parameters,
			.datalen = sizeof(parameters),
		};

		/* When sending a termination update to a channel, the bridge is highly
		 * likely to be getting torn down. Queueing a frame through the bridging
		 * framework won't work, as the frame will likely just get tossed as the
		 * bridge collapses. Hence, we write directly to the channel to ensure that
		 * they know they aren't in a T.38 fax any longer.
		 */
		ast_debug(3, "Bridge %s T.38: Informing %s to switch to %d\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan), parameters.request_response);
		ast_write(bridge_channel->chan, &f);
	}
}

static void t38_bridge_stop(struct ast_bridge *bridge)
{
	struct t38_bridge_state *state = bridge->tech_pvt;

	send_termination_update(bridge, state->bc0, state->c0_state);
	send_termination_update(bridge, state->bc1, state->c1_state);
}

static void t38_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct t38_bridge_state *state = bridge->tech_pvt;

	if (bridge_channel == state->bc0) {
		send_termination_update(bridge, state->bc0, state->c0_state);
		state->bc0 = NULL;
		state->c0_state = T38_STATE_UNKNOWN;
	} else {
		send_termination_update(bridge, state->bc1, state->c1_state);
		state->bc1 = NULL;
		state->c1_state = T38_STATE_UNKNOWN;
	}
}

static int t38_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct t38_bridge_state *state = bridge->tech_pvt;
	enum ast_t38_state *c_state;
	enum ast_t38_state *other_state;

	if (!bridge_channel) {
		return -1;
	}

	c_state = bridge_channel == state->bc0 ? &(state->c0_state) : &(state->c1_state);
	other_state = bridge_channel == state->bc0 ? &(state->c1_state) : &(state->c0_state);

	switch (frame->frametype) {
	case AST_FRAME_CONTROL:
		switch (frame->subclass.integer) {
		case AST_CONTROL_T38_PARAMETERS:
		{
			struct ast_control_t38_parameters *parameters = frame->data.ptr;

			switch (parameters->request_response) {
				case AST_T38_REQUEST_NEGOTIATE:
					*c_state = T38_STATE_NEGOTIATING;
					*other_state = T38_STATE_NEGOTIATING;
					break;
				case AST_T38_NEGOTIATED:
					*c_state = T38_STATE_NEGOTIATED;
					break;
				case AST_T38_TERMINATED:
				case AST_T38_REQUEST_TERMINATE:
				case AST_T38_REFUSED:
					*c_state = T38_STATE_REJECTED;
					break;
				case AST_T38_REQUEST_PARMS:
				default:
					/* No state change */
					break;
			}
			ast_debug(3, "Bridge %s T.38 state: %s: %d; %s: %d\n",
				bridge->uniqueid, ast_channel_name(state->bc0->chan), state->c0_state,
				ast_channel_name(state->bc1->chan), state->c1_state);
			break;
		}
		default:
			break;
		}
		break;
	default:
		break;
	}

	return ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
}

static int t38_bridge_compatible(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bc0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *bc1 = AST_LIST_LAST(&bridge->channels);
	enum ast_t38_state c0_state;
	enum ast_t38_state c1_state;

	/* We must have two, and only two, channels in a T.38 bridge */
	if (bridge->num_channels != 2) {
		ast_debug(1, "Bridge '%s' can not use T.38 bridge as two channels are required\n",
			bridge->uniqueid);
		return 0;
	}

	/* We can be the bridge tech so long as one side is in the process
	 * of negotiating T.38
	 */
	c0_state = ast_channel_get_t38_state(bc0->chan);
	c1_state = ast_channel_get_t38_state(bc1->chan);
	if (c0_state != T38_STATE_NEGOTIATING && c0_state != T38_STATE_NEGOTIATED
		&& c1_state != T38_STATE_NEGOTIATING && c1_state != T38_STATE_NEGOTIATED) {
		ast_debug(1, "Bridge '%s' can not use T.38 bridge: channel %s has T.38 state %d; channel %s has T.38 state %d\n",
			bridge->uniqueid, ast_channel_name(bc0->chan), c0_state, ast_channel_name(bc1->chan), c1_state);
		return 0;
	}

	return 1;
}

static struct ast_bridge_technology t38_bridge = {
	.name = "t38_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX,
	.preference = AST_BRIDGE_PREFERENCE_BASE_1TO1MIX + 1,
	.create = t38_bridge_create,
	.destroy = t38_bridge_destroy,
	.start = t38_bridge_start,
	.stop = t38_bridge_stop,
	.leave = t38_bridge_leave,
	.write = t38_bridge_write,
	.compatible = t38_bridge_compatible,
};

static int unload_module(void)
{
	ast_bridge_technology_unregister(&t38_bridge);

	return 0;
}

static int load_module(void)
{
	if (ast_bridge_technology_register(&t38_bridge)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Two channel bridging module that maintains T.38 state");
