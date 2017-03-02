/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
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
#include "asterisk/sdp_state.h"
#include "asterisk/sdp_options.h"
#include "asterisk/sdp_translator.h"
#include "asterisk/vector.h"
#include "asterisk/utils.h"

#include "../include/asterisk/sdp.h"
#include "asterisk/stream.h"

enum ast_sdp_state_machine {
	/*! \brief The initial state.
	 *
	 * The state machine starts here. It also goes back to this
	 * state whenever ast_sdp_state_reset() is called.
	 */
	SDP_STATE_INITIAL,
	/*! \brief We are the SDP offerer.
	 *
	 * The state machine enters this state if in the initial state
	 * and ast_sdp_state_get_local() is called. When this state is
	 * entered, a local SDP is created and then returned.
	 */
	SDP_STATE_OFFERER,
	/*! \brief We are the SDP answerer.
	 *
	 * The state machine enters this state if in the initial state
	 * and ast_sdp_state_set_remote() is called.
	 */
	SDP_STATE_ANSWERER,
	/*! \brief The SDP has been negotiated.
	 *
	 * This state can be entered from either the offerer or answerer
	 * state. When this state is entered, a joint SDP is created.
	 */
	SDP_STATE_NEGOTIATED,
	/*! \brief Not an actual state.
	 *
	 * This is just here to mark the end of the enumeration.
	 */
	SDP_STATE_END,
};

typedef int (*state_fn)(struct ast_sdp_state *state);

struct ast_sdp_state {
	/*! Local capabilities, learned through configuration */
	struct ast_stream_topology *local_capabilities;
	/*! Remote capabilities, learned through remote SDP */
	struct ast_stream_topology *remote_capabilities;
	/*! Joint capabilities. The combined local and remote capabilities. */
	struct ast_stream_topology *joint_capabilities;
	/*! Local SDP. Generated via the options and local capabilities. */
	struct ast_sdp *local_sdp;
	/*! Remote SDP. Received directly from a peer. */
	struct ast_sdp *remote_sdp;
	/*! Joint SDP. The merged local and remote SDPs. */
	struct ast_sdp *joint_sdp;
	/*! SDP options. Configured options beyond media capabilities. */
	struct ast_sdp_options *options;
	/*! Translator that puts SDPs into the expected representation */
	struct ast_sdp_translator *translator;
	/*! The current state machine state that we are in */
	enum ast_sdp_state_machine state;
};

struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *streams,
	struct ast_sdp_options *options)
{
	struct ast_sdp_state *sdp_state;

	sdp_state = ast_calloc(1, sizeof(*sdp_state));
	if (!sdp_state) {
		return NULL;
	}

	sdp_state->options = options;

	sdp_state->translator = ast_sdp_translator_new(ast_sdp_options_get_impl(sdp_state->options));
	if (!sdp_state->translator) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}

	sdp_state->local_capabilities = ast_stream_topology_clone(streams);
	if (!sdp_state->local_capabilities) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}
	sdp_state->state = SDP_STATE_INITIAL;

	return sdp_state;
}

void ast_sdp_state_free(struct ast_sdp_state *sdp_state)
{
	if (!sdp_state) {
		return;
	}

	ast_stream_topology_free(sdp_state->local_capabilities);
	ast_stream_topology_free(sdp_state->remote_capabilities);
	ast_stream_topology_free(sdp_state->joint_capabilities);
	ast_sdp_free(sdp_state->local_sdp);
	ast_sdp_free(sdp_state->remote_sdp);
	ast_sdp_free(sdp_state->joint_sdp);
	ast_sdp_options_free(sdp_state->options);
	ast_sdp_translator_free(sdp_state->translator);
}

struct ast_rtp_instance *ast_sdp_state_get_rtp_instance(
	const struct ast_sdp_state *sdp_state, int stream_index)
{
	struct ast_stream *stream;

	ast_assert(sdp_state != NULL);

	stream = ast_stream_topology_get_stream(sdp_state->local_capabilities, stream_index);
	if (!stream) {
		return NULL;
	}

	return (struct ast_rtp_instance *)ast_stream_get_data(stream, AST_STREAM_DATA_RTP_INSTANCE);
}

const struct ast_stream_topology *ast_sdp_state_get_joint_topology(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);
	if (sdp_state->state == SDP_STATE_NEGOTIATED) {
		return sdp_state->joint_capabilities;
	} else {
		return sdp_state->local_capabilities;
	}
}

const struct ast_stream_topology *ast_sdp_state_get_local_topology(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->local_capabilities;
}

const struct ast_sdp_options *ast_sdp_state_get_options(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->options;
}

#if 0
static int merge_sdps(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state->local_sdp != NULL);
	ast_assert(sdp_state->remote_sdp != NULL);
	/* XXX STUB */
	/* The goal of this function is to take
	 * sdp_state->local_sdp and sdp_state->remote_sdp
	 * and negotiate those into a joint SDP. This joint
	 * SDP should be stored in sdp_state->joint_sdp. After
	 * the joint SDP is created, the joint SDP should be
	 * used to create the joint topology. Finally, if necessary,
	 * the RTP session may need to be adjusted in some ways. For
	 * instance, if we previously opened three ports for three
	 * streams, but we negotiate down to two streams, then we
	 * can shut down the port for the third stream. Similarly,
	 * if we end up negotiating something like BUNDLE, then we may
	 * need to tell the RTP layer to close ports and to multiplex
	 * streams.
	 */

	return 0;
}
#endif

const struct ast_sdp *ast_sdp_state_get_local_sdp(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	if (!sdp_state->local_sdp) {
		sdp_state->local_sdp = ast_sdp_create_from_state(sdp_state);
	}

	return sdp_state->local_sdp;
}

const void *ast_sdp_state_get_local_sdp_impl(struct ast_sdp_state *sdp_state)
{
	const struct ast_sdp *sdp = ast_sdp_state_get_local_sdp(sdp_state);

	if (!sdp) {
		return NULL;
	}

	return ast_sdp_translator_from_sdp(sdp_state->translator, sdp);
}

void ast_sdp_state_set_remote_sdp(struct ast_sdp_state *sdp_state, struct ast_sdp *sdp)
{
	ast_assert(sdp_state != NULL);

	sdp_state->remote_sdp = sdp;
}

int ast_sdp_state_set_remote_sdp_from_impl(struct ast_sdp_state *sdp_state, void *remote)
{
	struct ast_sdp *sdp;

	ast_assert(sdp_state != NULL);

	sdp = ast_sdp_translator_to_sdp(sdp_state->translator, remote);
	if (!sdp) {
		return -1;
	}

	sdp_state->remote_sdp = sdp;

	return 0;
}

int ast_sdp_state_reset(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	ast_sdp_free(sdp_state->local_sdp);
	sdp_state->local_sdp = NULL;

	ast_sdp_free(sdp_state->remote_sdp);
	sdp_state->remote_sdp = NULL;

	ast_sdp_free(sdp_state->joint_sdp);
	sdp_state->joint_sdp = NULL;

	ast_stream_topology_free(sdp_state->remote_capabilities);
	sdp_state->remote_capabilities = NULL;

	ast_stream_topology_free(sdp_state->joint_capabilities);
	sdp_state->joint_capabilities = NULL;

	sdp_state->state = SDP_STATE_INITIAL;

	return 0;
}
