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

#include "asterisk/stream.h"
#include "asterisk/sdp_options.h"

struct ast_sdp_state;

/*!
 * \brief Allocate a new SDP state
 *
 * SDP state keeps tabs on everything SDP-related for a media session.
 * Most SDP operations will require the state to be provided.
 * Ownership of the SDP options is taken on by the SDP state.
 * A good strategy is to call this during session creation.
 */
struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *streams,
	struct ast_sdp_options *options);

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
struct ast_rtp_instance *ast_sdp_state_get_rtp_instance(const struct ast_sdp_state *sdp_state,
	int stream_index);

/*!
 * \brief Get the joint negotiated streams based on local and remote capabilities.
 *
 * If this is called prior to receiving a remote SDP, then this will just mirror
 * the local configured endpoint capabilities.
 */
const struct ast_stream_topology *ast_sdp_state_get_joint_topology(
	const struct ast_sdp_state *sdp_state);

/*!
 * \brief Get the local topology
 *
 */
const struct ast_stream_topology *ast_sdp_state_get_local_topology(
	const struct ast_sdp_state *sdp_state);

/*!
 * \brief Get the sdp_state options
 *
 */
const struct ast_sdp_options *ast_sdp_state_get_options(
	const struct ast_sdp_state *sdp_state);


/*!
 * \brief Get the local SDP.
 *
 * \param sdp_state
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \note
 * This function will allocate a new SDP with RTP instances if it has not already
 * been allocated.
 *
 */
const struct ast_sdp *ast_sdp_state_get_local_sdp(struct ast_sdp_state *sdp_state);

/*!
 * \brief Get the local SDP Implementation.
 *
 * \param sdp_state
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \note
 * This function calls ast_sdp_state_get_local_sdp then translates it into
 * the defined implementation.
 *
 * The return here is const. The use case for this is so that a channel can add
 * the SDP to an outgoing message. The API user should not attempt to modify the SDP.
 * SDP modification should only be done through the API.
 *
 * \since 15
 */
const void *ast_sdp_state_get_local_sdp_impl(struct ast_sdp_state *sdp_state);

/*!
 * \brief Set the remote SDP
 *
 * \param sdp_state
 * \param sdp
 *
 * \since 15
 */
void ast_sdp_state_set_remote_sdp(struct ast_sdp_state *sdp_state, struct ast_sdp *sdp);

/*!
 * \brief Set the remote SDP from an Implementation
 *
 * \param sdp_state
 * \param remote The implementation's representation of an SDP.
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_state_set_remote_sdp_from_impl(struct ast_sdp_state *sdp_state, void *remote);

/*!
 * \brief Reset the SDP state and stream capabilities as if the SDP state had just been allocated.
 *
 * \param sdp_state
 * \param remote The implementation's representation of an SDP.
 *
 * \retval 0 Success
 *
 * \note
 * This is most useful for when a channel driver is sending a session refresh message
 * and needs to re-advertise its initial capabilities instead of the previously-negotiated
 * joint capabilities.
 *
 * \since 15
 */
int ast_sdp_state_reset(struct ast_sdp_state *sdp_state);

#endif /* _ASTERISK_SDP_STATE_H */
