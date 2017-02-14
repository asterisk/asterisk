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

/* NOTE: It is unlikely that you need to include this file. You probably will only need
 * this if you are an SDP translator, or if you are an inner part of the SDP API
 */

#ifndef _SDP_PRIV_H
#define _SDP_PRIV_H

#include "asterisk/vector.h"

/*!
 * \brief Structure representing an SDP attribute
 */
struct ast_sdp_a_line {
	/*! Attribute name */
	char *name;
	/*! Attribute value. For attributes that have no value, this will be an empty string */
	char *value;
};

/*!
 * \brief Structure representing an SDP connection
 */
struct ast_sdp_c_line {
	/* IP family string (e.g. IP4 or IP6) */
	char *family;
	/* Connection address. Can be an IP address or FQDN */
	char *addr;
};

/*!
 * \brief A collection of SDP attributes
 */
AST_VECTOR(ast_sdp_a_line_vector, struct ast_sdp_a_line);

/*!
 * \brief An SDP media stream
 *
 * This contains both the m line, as well as its
 * constituent a lines.
 */
struct ast_sdp_m_line {
	/*! Media type (e.g. "audio" or "video") */
	char *type;
	/*! Port number in m line */
	uint16_t port;
	/*! Number of ports specified in m line */
	uint16_t port_count;
	/*! RTP profile string (e.g. "RTP/AVP") */
	char *profile;
	/*! RTP payloads */
	AST_VECTOR(, char *) payloads;
	/*! Connection information for this media stream */
	struct ast_sdp_c_line c_line;
	/*! The attributes for this media stream */
	struct ast_sdp_a_line_vector a_lines;
};

/*!
 * \brief SDP time information
 */
struct ast_sdp_t_line {
	/*! Session start time */
	uint32_t start;
	/*! Session end time */
	uint32_t end;
};

/*!
 * \brief An SDP
 */
struct ast_sdp {
	/*! SDP Origin line */
	struct {
		/*! Origin user name */
		char *user;
		/*! Origin id */
		uint32_t id;
		/*! Origin version */
		uint32_t version;
		/*! Origin IP address family (e.g. "IP4" or "IP6") */
		char *family;
		/*! Origin address. Can be an IP address or FQDN */
		char *addr;
	} o_line;
	/*! SDP Session name */
	char *s_line;
	/*! SDP top-level connection information */
	struct ast_sdp_c_line c_line;
	/*! SDP timing information */
	struct ast_sdp_t_line t_line;
	/*! SDP top-level attributes */
	struct ast_sdp_a_line_vector a_lines;
	/*! SDP media streams */
	AST_VECTOR(, struct ast_sdp_m_line) m_lines;
};

/*!
 * \brief Allocate a new SDP.
 *
 * \note This does not perform any initialization.
 *
 * \retval NULL FAIL
 * \retval non-NULL New SDP
 */
struct ast_sdp *ast_sdp_alloc(void);

/*!
 * \brief Free an SDP and all its constituent parts
 */
void ast_sdp_free(struct ast_sdp *dead);

#endif /* _SDP_PRIV_H */
