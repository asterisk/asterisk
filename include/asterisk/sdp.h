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
#include "asterisk/format.h"
#include "asterisk/sdp_state.h"
#include "asterisk/stream.h"

/*!
 * \brief Structure representing an SDP Attribute
 */
struct ast_sdp_a_line {
	/*! Attribute name */
	char *name;
	/*! Attribute value. For attributes that have no value, this will be an empty string */
	char *value;
};

/*!
 * \brief A collection of SDP Attributes
 */
AST_VECTOR(ast_sdp_a_lines, struct ast_sdp_a_line *);

/*!
 * \brief Structure representing an SDP Connection
 */
struct ast_sdp_c_line {
	/* IP family string (e.g. IP4 or IP6) */
	char *address_type;
	/* Connection address. Can be an IP address or FQDN */
	char *address;
};

/*!
 * \brief Structre representing SDP Media Payloads
 */
struct ast_sdp_payload {
	/* Media format description */
	char *fmt;
};

/*!
 * \brief A collection of SDP Media Payloads
 */
AST_VECTOR(ast_sdp_payloads, struct ast_sdp_payload *);

/*!
 * \brief Structure representing an SDP Media Stream
 *
 * This contains both the m line, as well as its
 * constituent a lines.
 */
struct ast_sdp_m_line {
	/*! Media type (e.g. "audio" or "video") */
	char *type;
	/*! RTP profile string (e.g. "RTP/AVP") */
	char *proto;
	/*! Port number in m line */
	uint16_t port;
	/*! Number of ports specified in m line */
	uint16_t port_count;
	/*! RTP payloads */
	struct ast_sdp_payloads *payloads;
	/*! Connection information for this media stream */
	struct ast_sdp_c_line *c_line;
	/*! The attributes for this media stream */
	struct ast_sdp_a_lines *a_lines;
};

/*!
 * \brief A collection of SDP Media Streams
 */
AST_VECTOR(ast_sdp_m_lines, struct ast_sdp_m_line *);

/*!
 * \brief Structure representing an SDP Origin
 */
struct ast_sdp_o_line {
	/*! Origin user name */
	char *username;
	/*! Origin id */
	uint64_t session_id;
	/*! Origin version */
	uint64_t session_version;
	/*! Origin IP address type (e.g. "IP4" or "IP6") */
	char *address_type;
	/*! Origin address. Can be an IP address or FQDN */
	char *address;
};

/*!
 * \brief Structure representing an SDP Session Name
 */
struct ast_sdp_s_line {
	/* Session Name */
	char *session_name;
};

/*!
 * \brief Structure representing SDP Timing
 */
struct ast_sdp_t_line {
	/*! Session start time */
	uint64_t start_time;
	/*! Session end time */
	uint64_t stop_time;
};

/*!
 * \brief An SDP
 */
struct ast_sdp {
	/*! SDP Origin line */
	struct ast_sdp_o_line *o_line;
	/*! SDP Session name */
	struct ast_sdp_s_line *s_line;
	/*! SDP top-level connection information */
	struct ast_sdp_c_line *c_line;
	/*! SDP timing information */
	struct ast_sdp_t_line *t_line;
	/*! SDP top-level attributes */
	struct ast_sdp_a_lines *a_lines;
	/*! SDP media streams */
	struct ast_sdp_m_lines *m_lines;
};

/*!
 * \brief A structure representing an SDP rtpmap attribute
 */
struct ast_sdp_rtpmap {
	/*! The RTP payload number for the rtpmap */
	int payload;
	/*! The Name of the codec */
	char *encoding_name;
	/*! The clock rate of the codec */
	int clock_rate;
	/*! Optional encoding parameters */
	char *encoding_parameters;
	/*! Area where strings are stored */
	char buf[0];
};

/*!
 * \brief Free an SDP Attribute
 *
 * \param a_line The attribute to free
 *
 * \since 15
 */
void ast_sdp_a_free(struct ast_sdp_a_line *a_line);

/*!
 * \brief Free an SDP Attribute collection
 *
 * \param a_lines The attribute collection to free
 *
 * \since 15
 */
void ast_sdp_a_lines_free(struct ast_sdp_a_lines *a_lines);

/*!
 * \brief Free SDP Connection Data
 *
 * \param c_line The connection data to free
 *
 * \since 15
 */
void ast_sdp_c_free(struct ast_sdp_c_line *c_line);

/*!
 * \brief Free an SDP Media Description Payload
 *
 * \param payload The payload to free
 *
 * \since 15
 */
void ast_sdp_payload_free(struct ast_sdp_payload *payload);

/*!
 * \brief Free an SDP Media Description Payload collection
 *
 * \param payloads collection to free
 *
 * \since 15
 */
void ast_sdp_payloads_free(struct ast_sdp_payloads *payloads);

/*!
 * \brief Free an SDP Media Description
 * Frees the media description and all resources it contains
 *
 * \param m_line The media description to free
 *
 * \since 15
 */
void ast_sdp_m_free(struct ast_sdp_m_line *m_line);

/*!
 * \brief Free an SDP Media Description collection
 *
 * \param m_lines The collection description to free
 *
 * \since 15
 */
void ast_sdp_m_lines_free(struct ast_sdp_m_lines *m_lines);

/*!
 * \brief Free an SDP Origin
 *
 * \param o_line The origin description to free
 *
 * \since 15
 */
void ast_sdp_o_free(struct ast_sdp_o_line *o_line);

/*!
 * \brief Free an SDP Session
 *
 * \param s_line The session to free
 *
 * \since 15
 */
void ast_sdp_s_free(struct ast_sdp_s_line *s_line);

/*!
 * \brief Free SDP Timing
 *
 * \param t_line The timing description to free
 *
 * \since 15
 */
void ast_sdp_t_free(struct ast_sdp_t_line *t_line);

/*!
 * \brief Allocate an SDP Attribute
 *
 * \param name Attribute Name
 * \param value Attribute Name
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_a_line *ast_sdp_a_alloc(const char *name, const char *value);

/*!
 * \brief Allocate an SDP Connection
 *
 * \param family Family ("IN", etc)
 * \param addr Address
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_c_line *ast_sdp_c_alloc(const char *family, const char *addr);

/*!
 * \brief Allocate an SDP Media Description Payload
 *
 * \param fmt The media format description
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_payload *ast_sdp_payload_alloc(const char *fmt);

/*!
 * \brief Allocate an SDP Media Description
 *
 * \param type ("audio", "video", etc)
 * \param port Starting port
 * \param port_count Port pairs to allocate
 * \param proto ("RTP/AVP", "RTP/SAVP", "udp")
 * \param c_line Connection to add.  May be NULL
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_m_line *ast_sdp_m_alloc(const char *type, uint16_t port,
	uint16_t port_count, 	const char *proto, struct ast_sdp_c_line *c_line);

/*!
 * \brief Allocate an SDP Session
 *
 * \param session_name The session name
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_s_line *ast_sdp_s_alloc(const char *session_name);

/*!
 * \brief Allocate SDP Timing
 *
 * \param start_time (Seconds since 1900)
 * \param end_time (Seconds since 1900)
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_t_line *ast_sdp_t_alloc(uint64_t start_time, uint64_t stop_time);

/*!
 * \brief Allocate an SDP Origin
 *
 * \param username User name
 * \param sesison_id Session ID
 * \param sesison_version Session Version
 * \param address_type Address type ("IN4", "IN6", etc)
 * \param address Unicast address
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_o_line *ast_sdp_o_alloc(const char *username, uint64_t session_id,
	uint64_t session_version, const char *address_type, const char *address);

/*!
 * \brief Add an SDP Attribute to an SDP
 *
 * \param sdp SDP
 * \param a_line Attribute
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_add_a(struct ast_sdp *sdp, struct ast_sdp_a_line *a_line);

/*!
 * \brief Get the count of Attributes on an SDP
 *
 * \param sdp SDP
 *
 * \returns Number of Attributes
 *
 * \since 15
 */
int ast_sdp_get_a_count(const struct ast_sdp *sdp);

/*!
 * \brief Get an Attribute from an SDP
 *
 * \param sdp SDP
 * \param index Attribute index
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_a_line *ast_sdp_get_a(const struct ast_sdp *sdp, int index);

/*!
 * \brief Add a Media Description to an SDP
 *
 * \param sdp SDP
 * \param m_line Media Description
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_add_m(struct ast_sdp *sdp, struct ast_sdp_m_line *m_line);

/*!
 * \brief Add an RTP Media Description to an SDP
 *
 * \param sdp SDP
 * \param sdp_state SDP state information
 * \param options SDP Options
 * \param stream_index stream
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_add_m_from_rtp_stream(struct ast_sdp *sdp, const struct ast_sdp_state *sdp_state,
	const struct ast_sdp_options *options, int stream_index);

/*!
 * \brief Get the count of Media Descriptions on an SDP
 *
 * \param sdp SDP
 *
 * \returns The number of Media Descriptions
 *
 * \since 15
 */
int ast_sdp_get_m_count(const struct ast_sdp *sdp);

/*!
 * \brief Get a Media Descriptions from an SDP
 *
 * \param sdp SDP
 * \param index Media Description index
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_m_line *ast_sdp_get_m(const struct ast_sdp *sdp, int index);

/*!
 * \brief Add an SDP Attribute to a Media Description
 *
 * \param m_line Media Description
 * \param a_line Attribute
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_m_add_a(struct ast_sdp_m_line *m_line, struct ast_sdp_a_line *a_line);

/*!
 * \brief Get the count of Attributes on a Media Description
 *
 * \param m_line Media Description
 *
 * \returns Number of Attributes
 *
 * \since 15
 */
int ast_sdp_m_get_a_count(const struct ast_sdp_m_line *m_line);

/*!
 * \brief Get an Attribute from a Media Description
 *
 * \param m_line Media Description
 * \param index Attribute index
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_a_line *ast_sdp_m_get_a(const struct ast_sdp_m_line *m_line, int index);

/*!
 * \brief Add a Payload to a Media Description
 *
 * \param m_line Media Description
 * \param payload Payload
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_m_add_payload(struct ast_sdp_m_line *m_line,
	struct ast_sdp_payload *payload);

/*!
 * \brief Get the count of Payloads on a Media Description
 *
 * \param m_line Media Description
 *
 * \returns Number of Attributes
 *
 * \since 15
 */
int ast_sdp_m_get_payload_count(const struct ast_sdp_m_line *m_line);

/*!
 * \brief Get a Payload from a Media Description
 *
 * \param m_line Media Description
 * \param index Payload index
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp_payload *ast_sdp_m_get_payload(const struct ast_sdp_m_line *m_line, int index);

/*!
 * \brief Add a Format to a Media Description
 *
 * \param m_line Media Description
 * \param options SDP Options
 * \param rtp_code rtp_code from ast_rtp_codecs_payload_code
 * \param asterisk_format True if the value in format is to be used.
 * \param format Format
 * \param code from AST_RTP list
 *
 * \retval 0 Success
 * \retval non-0 Failure
 *
 * \since 15
 */
int ast_sdp_m_add_format(struct ast_sdp_m_line *m_line, const struct ast_sdp_options *options,
	int rtp_code, int asterisk_format, const struct ast_format *format, int code);

/*!
 * \brief Create an SDP ao2 object
 *
 * \param o_line Origin
 * \param c_line Connection
 * \param s_line Session
 * \param t_line Timing
 *
 * \retval non-NULL Success
 * \retval NULL Failure
 *
 * \since 15
 */
struct ast_sdp *ast_sdp_alloc(struct ast_sdp_o_line *o_line,
	struct ast_sdp_c_line *c_line, struct ast_sdp_s_line *s_line,
	struct ast_sdp_t_line *t_line);

/*!
 * \brief Find the first attribute match index in the top-level SDP
 *
 * \note This will not search within streams for the given attribute.
 *
 * \param sdp The SDP in which to search
 * \param attr_name The name of the attribute to search for
 * \param payload Optional payload number to search for. If irrelevant, set to -1
 *
 * \retval index of attribute line on success.
 * \retval -1 on failure or not found.
 *
 * \since 15.0.0
 */
int ast_sdp_find_a_first(const struct ast_sdp *sdp, const char *attr_name, int payload);

/*!
 * \brief Find the next attribute match index in the top-level SDP
 *
 * \note This will not search within streams for the given attribute.
 *
 * \param sdp The SDP in which to search
 * \param last The last matching index found
 * \param attr_name The name of the attribute to search for
 * \param payload Optional payload number to search for. If irrelevant, set to -1
 *
 * \retval index of attribute line on success.
 * \retval -1 on failure or not found.
 *
 * \since 15.0.0
 */
int ast_sdp_find_a_next(const struct ast_sdp *sdp, int last, const char *attr_name, int payload);

/*!
 * \brief Find an attribute in the top-level SDP
 *
 * \note This will not search within streams for the given attribute.
 *
 * \param sdp The SDP in which to search
 * \param attr_name The name of the attribute to search for
 * \param payload Optional payload number to search for. If irrelevant, set to -1
 *
 * \retval NULL Could not find the given attribute
 * \retval Non-NULL The attribute to find
 *
 * \since 15.0.0
 */
struct ast_sdp_a_line *ast_sdp_find_attribute(const struct ast_sdp *sdp,
	const char *attr_name, int payload);

/*!
 * \brief Find the first attribute match index in an SDP stream (m-line)
 *
 * \param m_line The SDP m-line in which to search
 * \param attr_name The name of the attribute to search for
 * \param payload Optional payload number to search for. If irrelevant, set to -1
 *
 * \retval index of attribute line on success.
 * \retval -1 on failure or not found.
 *
 * \since 15.0.0
 */
int ast_sdp_m_find_a_first(const struct ast_sdp_m_line *m_line, const char *attr_name,
	int payload);

/*!
 * \brief Find the next attribute match index in an SDP stream (m-line)
 *
 * \param m_line The SDP m-line in which to search
 * \param last The last matching index found
 * \param attr_name The name of the attribute to search for
 * \param payload Optional payload number to search for. If irrelevant, set to -1
 *
 * \retval index of attribute line on success.
 * \retval -1 on failure or not found.
 *
 * \since 15.0.0
 */
int ast_sdp_m_find_a_next(const struct ast_sdp_m_line *m_line, int last,
	const char *attr_name, int payload);

/*!
 * \brief Find an attribute in an SDP stream (m-line)
 *
 * \param m_line The SDP m-line in which to search
 * \param attr_name The name of the attribute to search for
 * \param payload Optional payload number to search for. If irrelevant, set to -1
 *
 * \retval NULL Could not find the given attribute
 * \retval Non-NULL The attribute to find
 *
 * \since 15.0.0
 */
struct ast_sdp_a_line *ast_sdp_m_find_attribute(const struct ast_sdp_m_line *m_line,
	const char *attr_name, int payload);

/*!
 * \brief Convert an SDP a_line into an rtpmap
 *
 * The returned value is heap-allocated and must be freed with
 * ast_sdp_rtpmap_free()
 *
 * \param a_line The SDP a_line to convert
 *
 * \retval NULL Fail
 * \retval non-NULL Success
 *
 * \since 15.0.0
 */
struct ast_sdp_rtpmap *ast_sdp_a_get_rtpmap(const struct ast_sdp_a_line *a_line);


/*!
 * \brief Allocate a new SDP rtpmap
 *
 * \param payload The RTP payload number
 * \param encoding_name The human-readable name for the codec
 * \param clock_rate The rate of the codec, in cycles per second
 * \param encoding_parameters Optional codec-specific parameters (such as number of channels)
 *
 * \retval NULL Fail
 * \retval non-NULL Success
 *
 * \since 15.0.0
 */
struct ast_sdp_rtpmap *ast_sdp_rtpmap_alloc(int payload, const char *encoding_name,
	int clock_rate, const char *encoding_parameters);

/*!
 * \brief Free an SDP rtpmap
 *
 * \since 15.0.0
 */
void ast_sdp_rtpmap_free(struct ast_sdp_rtpmap *rtpmap);

/*!
 * \brief Turn an SDP into a stream topology
 *
 * This traverses the m-lines of the SDP and creates a stream topology, with
 * each m-line corresponding to a stream in the created topology.
 *
 * \param sdp The SDP to convert
 * \param g726_non_standard Non-zero if G.726 is non-standard
 *
 * \retval NULL An error occurred when converting
 * \retval non-NULL The generated stream topology
 *
 * \since 15.0.0
 */
struct ast_stream_topology *ast_get_topology_from_sdp(const struct ast_sdp *sdp, int g726_non_standard);
#endif /* _SDP_PRIV_H */
