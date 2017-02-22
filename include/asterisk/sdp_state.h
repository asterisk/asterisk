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
struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *streams, struct ast_sdp_options *options);

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

/*!
 * \brief Get the local SDP.
 *
 * If we have not received a remote SDP yet, this will be an SDP offer based
 * on known streams and options If we have received a remote SDP, this will
 * be the negotiated SDP based on the joint capabilities. The return type is
 * a void pointer because the representation of the SDP is going to be determined based
 * on the SDP options when allocating the SDP state.
 *
 * This function will allocate RTP instances if RTP instances have not already
 * been allocated for the streams.
 *
 * The return here is const. The use case for this is so that a channel can add the SDP to an outgoing
 * message. The API user should not attempt to modify the SDP. SDP modification should only be done through
 * the API.
 */
const void *ast_sdp_state_get_local(struct ast_sdp_state *sdp_state);

/*!
 * \brief Set the remote SDP.
 *
 * This can be used for either a remote offer or answer.
 * This can also be used whenever an UPDATE, re-INVITE, etc. arrives.
 * The type of the "remote" parameter is dictated by whatever SDP representation
 * was set in the ast_sdp_options used during ast_sdp_state allocation
 *
 * This function will NOT allocate RTP instances.
 */
int ast_sdp_state_set_remote(struct ast_sdp_state *sdp_state, void *remote);

/*!
 * \brief Reset the SDP state and stream capabilities as if the SDP state had just been allocated.
 *
 * This is most useful for when a channel driver is sending a session refresh message
 * and needs to re-advertise its initial capabilities instead of the previously-negotiated
 * joint capabilities.
 */
int ast_sdp_state_reset(struct ast_sdp_state *sdp_state);

#endif /* _ASTERISK_SDP_STATE_H */
