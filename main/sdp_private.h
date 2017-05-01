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
		/*! Optional media address to use in SDP */
		AST_STRING_FIELD(media_address);
		/*! SDP origin username */
		AST_STRING_FIELD(sdpowner);
		/*! SDP session name */
		AST_STRING_FIELD(sdpsession);
		/*! RTP Engine Name */
		AST_STRING_FIELD(rtp_engine);
	);
	struct {
		unsigned int bind_rtp_to_media_address : 1;
		unsigned int bind_udptl_to_media_address : 1;
		unsigned int rtp_symmetric : 1;
		unsigned int udptl_symmetric : 1;
		unsigned int telephone_event : 1;
		unsigned int rtp_ipv6 : 1;
		unsigned int g726_non_standard : 1;
		unsigned int locally_held : 1;
		unsigned int rtcp_mux: 1;
		unsigned int ssrc: 1;
	};
	struct {
		unsigned int tos_audio;
		unsigned int cos_audio;
		unsigned int tos_video;
		unsigned int cos_video;
		unsigned int udptl_far_max_datagram;
	};
	enum ast_sdp_options_ice ice;
	enum ast_sdp_options_impl impl;
	enum ast_sdp_options_encryption encryption;
	enum ast_t38_ec_modes udptl_error_correction;
};

#endif /* _MAIN_SDP_PRIVATE_H */
