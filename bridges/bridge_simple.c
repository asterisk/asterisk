/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Simple two channel bridging module
 *
 * \author Joshua Colp <jcolp@digium.com>
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
#include "asterisk/stream.h"

static void simple_bridge_stream_topology_changed(struct ast_bridge *bridge,
		struct ast_bridge_channel *bridge_channel);

static int simple_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *c0 = AST_LIST_FIRST(&bridge->channels)->chan;
	struct ast_channel *c1 = AST_LIST_LAST(&bridge->channels)->chan;

	/*
	 * If this is the first channel we can't make it compatible...
	 * unless we make it compatible with itself.  O.o
	 */
	if (c0 == c1) {
		return 0;
	}

	if (ast_channel_make_compatible(c0, c1)) {
		return -1;
	}

	/* Align stream topologies */
	simple_bridge_stream_topology_changed(bridge, NULL);
	return 0;
}

static int simple_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	const struct ast_control_t38_parameters *t38_parameters;
	int defer = 0;

	if (!ast_bridge_queue_everyone_else(bridge, bridge_channel, frame)) {
		/* This frame was successfully queued so no need to defer */
		return 0;
	}

	/* Depending on the frame defer it so when the next channel joins it receives it */
	switch (frame->frametype) {
	case AST_FRAME_CONTROL:
		switch (frame->subclass.integer) {
		case AST_CONTROL_T38_PARAMETERS:
			t38_parameters = frame->data.ptr;
			switch (t38_parameters->request_response) {
			case AST_T38_REQUEST_NEGOTIATE:
				defer = -1;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return defer;
}

static struct ast_bridge_technology simple_bridge = {
	.name = "simple_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX,
	.preference = AST_BRIDGE_PREFERENCE_BASE_1TO1MIX,
	.join = simple_bridge_join,
	.write = simple_bridge_write,
	.stream_topology_changed = simple_bridge_stream_topology_changed,
};

static void simple_bridge_request_stream_topology_change(struct ast_channel *chan,
	struct ast_stream_topology *requested_topology)
{
	struct ast_stream_topology *existing_topology = ast_channel_get_stream_topology(chan);
	struct ast_stream *stream;
	struct ast_format_cap *audio_formats = NULL;
	struct ast_stream_topology *new_topology;
	int i;

	/* We find an existing stream with negotiated audio formats that we can place into
	 * any audio streams in the new topology to ensure that negotiation succeeds. Some
	 * endpoints incorrectly terminate the call if SDP negotiation fails.
	 */
	for (i = 0; i < ast_stream_topology_get_count(existing_topology); ++i) {
		stream = ast_stream_topology_get_stream(existing_topology, i);

		if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_AUDIO ||
			ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			continue;
		}

		audio_formats = ast_stream_get_formats(stream);
		break;
	}

	if (!audio_formats) {
		ast_channel_request_stream_topology_change(chan, requested_topology, &simple_bridge);
		return;
	}

	new_topology = ast_stream_topology_clone(requested_topology);
	if (!new_topology) {
		ast_channel_request_stream_topology_change(chan, requested_topology, &simple_bridge);
		return;
	}

	for (i = 0; i < ast_stream_topology_get_count(new_topology); ++i) {
		stream = ast_stream_topology_get_stream(new_topology, i);

		if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_AUDIO ||
			ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			continue;
		}

		ast_format_cap_append_from_cap(ast_stream_get_formats(stream), audio_formats, AST_MEDIA_TYPE_AUDIO);
	}

	ast_channel_request_stream_topology_change(chan, new_topology, &simple_bridge);

	ast_stream_topology_free(new_topology);
}

static void simple_bridge_stream_topology_changed(struct ast_bridge *bridge,
		struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *c0 = AST_LIST_FIRST(&bridge->channels)->chan;
	struct ast_channel *c1 = AST_LIST_LAST(&bridge->channels)->chan;
	struct ast_stream_topology *t0 = ast_channel_get_stream_topology(c0);
	struct ast_stream_topology *t1 = ast_channel_get_stream_topology(c1);

	if (bridge_channel) {
		ast_bridge_channel_stream_map(bridge_channel);
	}
	/*
	 * The bridge_channel should only be NULL after both channels join
	 * the bridge and their topologies are being aligned.
	 */
	if (bridge_channel && ast_channel_get_stream_topology_change_source(
		    bridge_channel->chan) == &simple_bridge) {
		return;
	}

	/* Align topologies according to size or first channel to join */
	if (ast_stream_topology_get_count(t0) < ast_stream_topology_get_count(t1)) {
		simple_bridge_request_stream_topology_change(c0, t1);
	} else {
		simple_bridge_request_stream_topology_change(c1, t0);
	}
}

static int unload_module(void)
{
	ast_bridge_technology_unregister(&simple_bridge);
	return 0;
}

static int load_module(void)
{
	if (ast_bridge_technology_register(&simple_bridge)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Simple two channel bridging module");
