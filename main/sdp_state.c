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
#include "asterisk/sdp_priv.h"
#include "asterisk/vector.h"
#include "asterisk/utils.h"
#include "asterisk/stream.h"

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
	/*! RTP instance for each media stream */
	AST_VECTOR(, struct ast_rtp_instance *) rtp;
};

struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *streams, struct ast_sdp_options *options)
{
	struct ast_sdp_state *sdp_state;

	sdp_state = ast_calloc(1, sizeof(*sdp_state));
	if (!sdp_state) {
		return NULL;
	}

	sdp_state->options = options;

	sdp_state->translator = ast_sdp_translator_new(ast_sdp_options_get_repr(sdp_state->options));
	if (!sdp_state->translator) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}

	sdp_state->local_capabilities = ast_stream_topology_clone(streams);
	if (!sdp_state->local_capabilities) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}

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

struct ast_rtp_instance *ast_sdp_state_get_rtp_instance(struct ast_sdp_state *sdp_state, int stream_index)
{
	if (stream_index >= AST_VECTOR_SIZE(&sdp_state->rtp)) {
		return NULL;
	}

	return AST_VECTOR_GET(&sdp_state->rtp, stream_index);
}

struct ast_stream_topology *ast_sdp_state_get_joint_topology(struct ast_sdp_state *sdp_state)
{
	if (sdp_state->joint_capabilities) {
		return sdp_state->joint_capabilities;
	} else {
		return sdp_state->local_capabilities;
	}
}
