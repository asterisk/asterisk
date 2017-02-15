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

#ifndef _ASTERISK_SDP_STATE_H
#define _ASTERISK_SDP_STATE_H

struct ast_sdp_state;
struct ast_sdp_options;
struct ast_stream_topology;

/*!
 * \brief Allocate a new SDP state
 *
 * SDP state keeps tabs on everything SDP-related for a media session.
 * Most SDP operations will require the state to be provided.
 * Ownership of the SDP options is taken on by the SDP state.
 * A good strategy is to call this during session creation.
 */
struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *streams, const struct ast_sdp_options *options);

/*!
 * \brief Free the SDP state.
 *
 * A good strategy is to call this during session destruction
 */
void ast_sdp_state_free(struct ast_sdp_state *sdp_state);

/*!
 * \brief Get the associated RTP instance for a particular stream on the SDP state.
 *
 * Stream numbers correspond to the streams in the topology of the associated channel
 */
struct ast_rtp_instance *ast_sdp_state_get_rtp_instance(struct ast_sdp_state *sdp_state, int stream_index);

/*!
 * \brief Get the joint negotiated streams based on local and remote capabilities.
 *
 * If this is called prior to receiving a remote SDP, then this will just mirror
 * the local configured endpoint capabilities.
 */
struct ast_stream_topology *ast_sdp_state_get_joint_topology(struct ast_sdp_state *sdp_state);

#endif /* _ASTERISK_SDP_STATE_H */
