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

#ifndef _ASTERISK_SDP_OPTIONS_H
#define _ASTERISK_SDP_OPTIONS_H

struct ast_sdp_options;

/*!
 * \brief ICE options
 *
 * This is an enum because it will support a TRICKLE-ICE option
 * in the future.
 */
enum ast_sdp_options_ice {
	/*! ICE is not enabled on this session */
	AST_SDP_ICE_DISABLED,
	/*! Standard ICE is enabled on this session */
	AST_SDP_ICE_ENABLED_STANDARD,
};

/*!
 * \brief Implementation of the SDP
 *
 * Users of the SDP API set the implementation based on what they
 * natively handle. This indicates the type of SDP that the API expects
 * when being given an SDP, and it indicates the type of SDP that the API
 * returns when asked for one.
 */
enum ast_sdp_options_impl {
	/*! SDP is represented as a string */
	AST_SDP_IMPL_STRING,
	/*! SDP is represented as a pjmedia_sdp_session */
	AST_SDP_IMPL_PJMEDIA,
	/*! End of the list */
	AST_SDP_IMPL_END,
};

/*!
 * \brief SDP encryption options
 */
enum ast_sdp_options_encryption {
	/*! No encryption */
	AST_SDP_ENCRYPTION_DISABLED,
	/*! SRTP SDES encryption */
	AST_SDP_ENCRYPTION_SRTP_SDES,
	/*! DTLS encryption */
	AST_SDP_ENCRYPTION_DTLS,
};

/*!
 * \since 15.0.0
 * \brief Allocate a new SDP options structure.
 *
 * This will heap-allocate an SDP options structure and
 * initialize it to a set of default values.
 *
 * \retval NULL Allocation failure
 * \retval non-NULL Newly allocated SDP options
 */
struct ast_sdp_options *ast_sdp_options_alloc(void);

/*!
 * \since 15.0.0
 * \brief Free an SDP options structure.
 *
 * \note This only needs to be called if an error occurs between
 *       options allocation and a call to ast_sdp_state_alloc()
 *       Otherwise, the SDP state will take care of freeing the
 *       options for you.
 *
 * \param options The options to free
 */
void ast_sdp_options_free(struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options media_address
 *
 * \param options SDP Options
 * \param media_address
 */
void ast_sdp_options_set_media_address(struct ast_sdp_options *options,
	const char *media_address);

/*!
 * \since 15.0.0
 * \brief Get SDP Options media_address
 *
 * \param options SDP Options
 *
 * \returns media_address
 */
const char *ast_sdp_options_get_media_address(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options sdpowner
 *
 * \param options SDP Options
 * \param sdpowner
 */
void ast_sdp_options_set_sdpowner(struct ast_sdp_options *options,
	const char *sdpowner);

/*!
 * \since 15.0.0
 * \brief Get SDP Options sdpowner
 *
 * \param options SDP Options
 *
 * \returns sdpowner
 */
const char *ast_sdp_options_get_sdpowner(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options sdpsession
 *
 * \param options SDP Options
 * \param sdpsession
 */
void ast_sdp_options_set_sdpsession(struct ast_sdp_options *options,
	const char *sdpsession);

/*!
 * \since 15.0.0
 * \brief Get SDP Options sdpsession
 *
 * \param options SDP Options
 *
 * \returns sdpsession
 */
const char *ast_sdp_options_get_sdpsession(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options rtp_engine
 *
 * \param options SDP Options
 * \param rtp_engine
 */
void ast_sdp_options_set_rtp_engine(struct ast_sdp_options *options,
	const char *rtp_engine);

/*!
 * \since 15.0.0
 * \brief Get SDP Options rtp_engine
 *
 * \param options SDP Options
 *
 * \returns rtp_engine
 */
const char *ast_sdp_options_get_rtp_engine(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options bind_rtp_to_media_address
 *
 * \param options SDP Options
 * \param bind_rtp_to_media_address
 */
void ast_sdp_options_set_bind_rtp_to_media_address(struct ast_sdp_options *options,
	unsigned int bind_rtp_to_media_address);

/*!
 * \since 15.0.0
 * \brief Get SDP Options bind_rtp_to_media_address
 *
 * \param options SDP Options
 *
 * \returns bind_rtp_to_media_address
 */
unsigned int ast_sdp_options_get_bind_rtp_to_media_address(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options rtp_symmetric
 *
 * \param options SDP Options
 * \param rtp_symmetric
 */
void ast_sdp_options_set_rtp_symmetric(struct ast_sdp_options *options,
	unsigned int rtp_symmetric);

/*!
 * \since 15.0.0
 * \brief Get SDP Options rtp_symmetric
 *
 * \param options SDP Options
 *
 * \returns rtp_symmetric
 */
unsigned int ast_sdp_options_get_rtp_symmetric(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options telephone_event
 *
 * \param options SDP Options
 * \param telephone_event
 */
void ast_sdp_options_set_telephone_event(struct ast_sdp_options *options,
	unsigned int telephone_event);

/*!
 * \since 15.0.0
 * \brief Get SDP Options telephone_event
 *
 * \param options SDP Options
 *
 * \returns telephone_event
 */
unsigned int ast_sdp_options_get_telephone_event(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options rtp_ipv6
 *
 * \param options SDP Options
 * \param rtp_ipv6
 */
void ast_sdp_options_set_rtp_ipv6(struct ast_sdp_options *options,
	unsigned int rtp_ipv6);

/*!
 * \since 15.0.0
 * \brief Get SDP Options rtp_ipv6
 *
 * \param options SDP Options
 *
 * \returns rtp_ipv6
 */
unsigned int ast_sdp_options_get_rtp_ipv6(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options g726_non_standard
 *
 * \param options SDP Options
 * \param g726_non_standard
 */
void ast_sdp_options_set_g726_non_standard(struct ast_sdp_options *options,
	unsigned int g726_non_standard);

/*!
 * \since 15.0.0
 * \brief Get SDP Options g726_non_standard
 *
 * \param options SDP Options
 *
 * \returns g726_non_standard
 */
unsigned int ast_sdp_options_get_g726_non_standard(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options tos_audio
 *
 * \param options SDP Options
 * \param tos_audio
 */
void ast_sdp_options_set_tos_audio(struct ast_sdp_options *options,
	unsigned int tos_audio);

/*!
 * \since 15.0.0
 * \brief Get SDP Options tos_audio
 *
 * \param options SDP Options
 *
 * \returns tos_audio
 */
unsigned int ast_sdp_options_get_tos_audio(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options cos_audio
 *
 * \param options SDP Options
 * \param cos_audio
 */
void ast_sdp_options_set_cos_audio(struct ast_sdp_options *options,
	unsigned int cos_audio);

/*!
 * \since 15.0.0
 * \brief Get SDP Options cos_audio
 *
 * \param options SDP Options
 *
 * \returns cos_audio
 */
unsigned int ast_sdp_options_get_cos_audio(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options tos_video
 *
 * \param options SDP Options
 * \param tos_video
 */
void ast_sdp_options_set_tos_video(struct ast_sdp_options *options,
	unsigned int tos_video);

/*!
 * \since 15.0.0
 * \brief Get SDP Options tos_video
 *
 * \param options SDP Options
 *
 * \returns tos_video
 */
unsigned int ast_sdp_options_get_tos_video(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options cos_video
 *
 * \param options SDP Options
 * \param cos_video
 */
void ast_sdp_options_set_cos_video(struct ast_sdp_options *options,
	unsigned int cos_video);

/*!
 * \since 15.0.0
 * \brief Get SDP Options cos_video
 *
 * \param options SDP Options
 *
 * \returns cos_video
 */
unsigned int ast_sdp_options_get_cos_video(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options ice
 *
 * \param options SDP Options
 * \param ice
 */
void ast_sdp_options_set_ice(struct ast_sdp_options *options,
	enum ast_sdp_options_ice ice);

/*!
 * \since 15.0.0
 * \brief Get SDP Options ice
 *
 * \param options SDP Options
 *
 * \returns ice
 */
enum ast_sdp_options_ice ast_sdp_options_get_ice(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options impl
 *
 * \param options SDP Options
 * \param impl
 */
void ast_sdp_options_set_impl(struct ast_sdp_options *options,
	enum ast_sdp_options_impl impl);

/*!
 * \since 15.0.0
 * \brief Get SDP Options impl
 *
 * \param options SDP Options
 *
 * \returns impl
 */
enum ast_sdp_options_impl ast_sdp_options_get_impl(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options encryption
 *
 * \param options SDP Options
 * \param encryption
 */
void ast_sdp_options_set_encryption(struct ast_sdp_options *options,
	enum ast_sdp_options_encryption encryption);

/*!
 * \since 15.0.0
 * \brief Get SDP Options encryption
 *
 * \param options SDP Options
 *
 * \returns encryption
 */
enum ast_sdp_options_encryption ast_sdp_options_get_encryption(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Get SDP Options RTCP MUX
 *
 * \param options SDP Options
 *
 * \returns Boolean indicating if RTCP MUX is enabled.
 */
unsigned int ast_sdp_options_get_rtcp_mux(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options RTCP MUX
 *
 * \param options SDP Options
 * \param value Boolean that indicates if RTCP MUX should be enabled.
 */
void ast_sdp_options_set_rtcp_mux(struct ast_sdp_options *options, unsigned int value);

#endif /* _ASTERISK_SDP_OPTIONS_H */
