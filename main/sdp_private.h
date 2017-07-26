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

#ifndef _MAIN_SDP_PRIVATE_H
#define _MAIN_SDP_PRIVATE_H

#include "asterisk/stringfields.h"
#include "asterisk/sdp_options.h"

struct ast_sdp_options {
	AST_DECLARE_STRING_FIELDS(
		/*! Media address to advertise in SDP session c= line */
		AST_STRING_FIELD(media_address);
		/*! Optional address of the interface media should use. */
		AST_STRING_FIELD(interface_address);
		/*! SDP origin username */
		AST_STRING_FIELD(sdpowner);
		/*! SDP session name */
		AST_STRING_FIELD(sdpsession);
		/*! RTP Engine Name */
		AST_STRING_FIELD(rtp_engine);
	);
	/*! Scheduler context for the media stream types (Mainly for RTP) */
	struct ast_sched_context *sched[AST_MEDIA_TYPE_END];
	/*! Capabilities to create new streams of the indexed media type. */
	struct ast_format_cap *caps[AST_MEDIA_TYPE_END];
	/*! User supplied context data pointer for the SDP state. */
	void *state_context;
	/*! Modify negotiated topology before create answer SDP callback. */
	ast_sdp_answerer_modify_cb answerer_modify_cb;
	/*! Modify proposed topology before create offer SDP callback. */
	ast_sdp_offerer_modify_cb offerer_modify_cb;
	/*! Configure proposed topology extra stream options before create offer SDP callback. */
	ast_sdp_offerer_config_cb offerer_config_cb;
	/*! Negotiated topology is about to be applied callback. */
	ast_sdp_preapply_cb preapply_cb;
	/*! Negotiated topology was just applied callback. */
	ast_sdp_postapply_cb postapply_cb;
	struct {
		unsigned int rtp_symmetric:1;
		unsigned int udptl_symmetric:1;
		unsigned int rtp_ipv6:1;
		unsigned int g726_non_standard:1;
		unsigned int rtcp_mux:1;
		unsigned int ssrc:1;
	};
	struct {
		unsigned int tos_audio;
		unsigned int cos_audio;
		unsigned int tos_video;
		unsigned int cos_video;
		unsigned int udptl_far_max_datagram;
		/*! Maximum number of streams to allow. */
		unsigned int max_streams;
	};
	enum ast_sdp_options_dtmf dtmf;
	enum ast_sdp_options_ice ice;
	enum ast_sdp_options_impl impl;
	enum ast_sdp_options_encryption encryption;
	enum ast_t38_ec_modes udptl_error_correction;
};

#endif /* _MAIN_SDP_PRIVATE_H */
