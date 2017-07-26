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

#include "asterisk/udptl.h"
#include "asterisk/format_cap.h"

struct ast_sdp_options;

/*!
 * \brief SDP DTMF mode options
 */
enum ast_sdp_options_dtmf {
	/*! No DTMF to be used */
	AST_SDP_DTMF_NONE,
	/*! Use RFC 4733 events for DTMF */
	AST_SDP_DTMF_RFC_4733,
	/*! Use DTMF in the audio stream */
	AST_SDP_DTMF_INBAND,
	/*! Use SIP 4733 if supported by the other side or INBAND if not */
	AST_SDP_DTMF_AUTO,
};

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
 * \brief Callback when processing an offer SDP for our answer SDP.
 * \since 15.0.0
 *
 * \details
 * This callback is called after merging our last negotiated topology
 * with the remote's offer topology and before we have sent our answer
 * SDP.  At this point you can alter new_topology streams.  You can
 * decline, remove formats, or rename streams.  Changing anything else
 * on the streams is likely to not end well.
 *
 * * To decline a stream simply set the stream state to
 *   AST_STREAM_STATE_REMOVED.  You could implement a maximum number
 *   of active streams of a given type policy.
 *
 * * To remove formats use the format API to remove any formats from a
 *   stream.  The streams have the current joint negotiated formats.
 *   Most likely you would want to remove all but the first format.
 *
 * * To rename a stream you need to clone the stream and give it a
 *   new name and then set it in new_topology using
 *   ast_stream_topology_set_stream().
 *
 * \note Removing all formats is an error.  You should decline the
 * stream instead.
 *
 * \param context User supplied context data pointer for the SDP
 * state.
 * \param old_topology Active negotiated topology.  NULL if this is
 * the first SDP negotiation.  The old topology is available so you
 * can tell if any streams are new or changing type.
 * \param new_topology New negotiated topology that we intend to
 * generate the answer SDP.
 *
 * \return Nothing
 */
typedef void (*ast_sdp_answerer_modify_cb)(void *context,
	const struct ast_stream_topology *old_topology,
	struct ast_stream_topology *new_topology);

/*!
 * \internal
 * \brief Callback when generating a topology for our SDP offer.
 * \since 15.0.0
 *
 * \details
 * This callback is called after merging any topology updates from the
 * system by ast_sdp_state_update_local_topology() and before we have
 * sent our offer SDP.  At this point you can alter new_topology
 * streams.  You can decline, add/remove/update formats, or rename
 * streams.  Changing anything else on the streams is likely to not
 * end well.
 *
 * * To decline a stream simply set the stream state to
 *   AST_STREAM_STATE_REMOVED.  You could implement a maximum number
 *   of active streams of a given type policy.
 *
 * * To update formats use the format API to change formats of the
 *   streams.  The streams have the current proposed formats.  You
 *   could do whatever you want for formats but you should stay within
 *   the configured formats for the stream type's endpoint.  However,
 *   you should use ast_sdp_state_update_local_topology() instead of
 *   this backdoor method.
 *
 * * To rename a stream you need to clone the stream and give it a
 *   new name and then set it in new_topology using
 *   ast_stream_topology_set_stream().
 *
 * \note Removing all formats is an error.  You should decline the
 * stream instead.
 *
 * \note Declined new streams that are in slots higher than present in
 * old_topology are removed so the SDP can be smaller.  The remote has
 * never seen those slots so we shouldn't bother keeping them.
 *
 * \param context User supplied context data pointer for the SDP
 * state.
 * \param old_topology Active negotiated topology.  NULL if this is
 * the first SDP negotiation.  The old topology is available so you
 * can tell if any streams are new or changing type.
 * \param new_topology Merged topology that we intend to generate the
 * offer SDP.
 *
 * \return Nothing
 */
typedef void (*ast_sdp_offerer_modify_cb)(void *context,
	const struct ast_stream_topology *old_topology,
	struct ast_stream_topology *new_topology);

/*!
 * \brief Callback when generating an offer SDP to configure extra stream data.
 * \since 15.0.0
 *
 * \details
 * This callback is called after any ast_sdp_offerer_modify_cb
 * callback and before we have sent our offer SDP.  The callback can
 * call several SDP API calls to configure the proposed capabilities
 * of streams before we create the SDP offer.  For example, the
 * callback could configure a stream specific connection address, T.38
 * parameters, RTP instance, or UDPTL instance parameters.
 *
 * \param context User supplied context data pointer for the SDP
 * state.
 * \param topology Topology ready to configure extra stream options.
 *
 * \return Nothing
 */
typedef void (*ast_sdp_offerer_config_cb)(void *context, const struct ast_stream_topology *topology);

/*!
 * \brief Callback before applying a topology.
 * \since 15.0.0
 *
 * \details
 * This callback is called before the topology is applied so the
 * using module can do what is necessary before the topology becomes
 * active.
 *
 * \param context User supplied context data pointer for the SDP
 * state.
 * \param topology Topology ready to be applied.
 *
 * \return Nothing
 */
typedef void (*ast_sdp_preapply_cb)(void *context, const struct ast_stream_topology *topology);

/*!
 * \brief Callback after applying a topology.
 * \since 15.0.0
 *
 * \details
 * This callback is called after the topology is applied so the
 * using module can do what is necessary after the topology becomes
 * active.
 *
 * \param context User supplied context data pointer for the SDP
 * state.
 * \param topology Topology already applied.
 *
 * \return Nothing
 */
typedef void (*ast_sdp_postapply_cb)(void *context, const struct ast_stream_topology *topology);

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
 * \brief Set SDP Options interface_address
 *
 * \param options SDP Options
 * \param interface_address
 */
void ast_sdp_options_set_interface_address(struct ast_sdp_options *options,
	const char *interface_address);

/*!
 * \since 15.0.0
 * \brief Get SDP Options interface_address
 *
 * \param options SDP Options
 *
 * \returns interface_address
 */
const char *ast_sdp_options_get_interface_address(const struct ast_sdp_options *options);

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

void ast_sdp_options_set_state_context(struct ast_sdp_options *options, void *state_context);
void *ast_sdp_options_get_state_context(const struct ast_sdp_options *options);

void ast_sdp_options_set_answerer_modify_cb(struct ast_sdp_options *options, ast_sdp_answerer_modify_cb answerer_modify_cb);
ast_sdp_answerer_modify_cb ast_sdp_options_get_answerer_modify_cb(const struct ast_sdp_options *options);

void ast_sdp_options_set_offerer_modify_cb(struct ast_sdp_options *options, ast_sdp_offerer_modify_cb offerer_modify_cb);
ast_sdp_offerer_modify_cb ast_sdp_options_get_offerer_modify_cb(const struct ast_sdp_options *options);

void ast_sdp_options_set_offerer_config_cb(struct ast_sdp_options *options, ast_sdp_offerer_config_cb offerer_config_cb);
ast_sdp_offerer_config_cb ast_sdp_options_get_offerer_config_cb(const struct ast_sdp_options *options);

void ast_sdp_options_set_preapply_cb(struct ast_sdp_options *options, ast_sdp_preapply_cb preapply_cb);
ast_sdp_preapply_cb ast_sdp_options_get_preapply_cb(const struct ast_sdp_options *options);

void ast_sdp_options_set_postapply_cb(struct ast_sdp_options *options, ast_sdp_postapply_cb postapply_cb);
ast_sdp_postapply_cb ast_sdp_options_get_postapply_cb(const struct ast_sdp_options *options);

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
 * \brief Set SDP Options dtmf
 *
 * \param options SDP Options
 * \param dtmf
 */
void ast_sdp_options_set_dtmf(struct ast_sdp_options *options,
	enum ast_sdp_options_dtmf dtmf);

/*!
 * \since 15.0.0
 * \brief Get SDP Options dtmf
 *
 * \param options SDP Options
 *
 * \returns dtmf
 */
enum ast_sdp_options_dtmf ast_sdp_options_get_dtmf(const struct ast_sdp_options *options);

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

/*!
 * \since 15.0.0
 * \brief Set SDP Options udptl_symmetric
 *
 * \param options SDP Options
 * \param udptl_symmetric
 */
void ast_sdp_options_set_udptl_symmetric(struct ast_sdp_options *options,
	unsigned int udptl_symmetric);

/*!
 * \since 15.0.0
 * \brief Get SDP Options udptl_symmetric
 *
 * \param options SDP Options
 *
 * \returns udptl_symmetric
 */
unsigned int ast_sdp_options_get_udptl_symmetric(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options udptl_error_correction
 *
 * \param options SDP Options
 * \param error_correction
 */
void ast_sdp_options_set_udptl_error_correction(struct ast_sdp_options *options,
	enum ast_t38_ec_modes error_correction);

/*!
 * \since 15.0.0
 * \brief Get SDP Options udptl_error_correction
 *
 * \param options SDP Options
 *
 * \returns udptl_error_correction
 */
enum ast_t38_ec_modes ast_sdp_options_get_udptl_error_correction(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options udptl_far_max_datagram
 *
 * \param options SDP Options
 * \param far_max_datagram
 */
void ast_sdp_options_set_udptl_far_max_datagram(struct ast_sdp_options *options,
	unsigned int far_max_datagram);

/*!
 * \since 15.0.0
 * \brief Get SDP Options udptl_far_max_datagram
 *
 * \param options SDP Options
 *
 * \returns udptl_far_max_datagram
 */
unsigned int ast_sdp_options_get_udptl_far_max_datagram(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Set SDP Options max_streams
 *
 * \param options SDP Options
 * \param max_streams
 */
void ast_sdp_options_set_max_streams(struct ast_sdp_options *options,
	unsigned int max_streams);

/*!
 * \since 15.0.0
 * \brief Get SDP Options max_streams
 *
 * \param options SDP Options
 *
 * \returns max_streams
 */
unsigned int ast_sdp_options_get_max_streams(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Enable setting SSRC level attributes on SDPs
 *
 * \param options SDP Options
 * \param ssrc Boolean indicating if SSRC attributes should be included in generated SDPs
 */
void ast_sdp_options_set_ssrc(struct ast_sdp_options *options, unsigned int ssrc);

/*!
 * \since 15.0.0
 * \brief Get SDP Options ssrc
 *
 * \param options SDP Options
 *
 * \returns Whether SSRC-level attributes will be added to our SDP.
 */
unsigned int ast_sdp_options_get_ssrc(const struct ast_sdp_options *options);

/*!
 * \brief Set the SDP options scheduler context used to create new streams of the type.
 * \since 15.0.0
 *
 * \param options SDP Options
 * \param type Media type the scheduler context is for.
 * \param sched Scheduler context to use for the specified media type.
 *
 * \return Nothing
 */
void ast_sdp_options_set_sched_type(struct ast_sdp_options *options,
	enum ast_media_type type, struct ast_sched_context *sched);

/*!
 * \brief Get the SDP options scheduler context used to create new streams of the type.
 * \since 15.0.0
 *
 * \param options SDP Options
 * \param type Media type the format cap represents.
 *
 * \return The stored scheduler context to create new streams of the type.
 */
struct ast_sched_context *ast_sdp_options_get_sched_type(const struct ast_sdp_options *options,
	enum ast_media_type type);

/*!
 * \brief Set all allowed stream types to create new streams.
 * \since 15.0.0
 *
 * \param options SDP Options
 * \param cap Format capabilities to set all allowed stream types at once.
 *            Could be NULL to disable creating any new streams.
 *
 * \return Nothing
 */
void ast_sdp_options_set_format_caps(struct ast_sdp_options *options,
	struct ast_format_cap *cap);

/*!
 * \brief Set the SDP options format cap used to create new streams of the type.
 * \since 15.0.0
 *
 * \param options SDP Options
 * \param type Media type the format cap represents.
 * \param cap Format capabilities to use for the specified media type.
 *            Could be NULL to disable creating new streams of type.
 *
 * \return Nothing
 */
void ast_sdp_options_set_format_cap_type(struct ast_sdp_options *options,
	enum ast_media_type type, struct ast_format_cap *cap);

/*!
 * \brief Get the SDP options format cap used to create new streams of the type.
 * \since 15.0.0
 *
 * \param options SDP Options
 * \param type Media type the format cap represents.
 *
 * \retval NULL if stream not allowed to be created.
 * \retval cap to use in negotiating the new stream.
 *
 * \note The returned cap does not have its own ao2 ref.
 */
struct ast_format_cap *ast_sdp_options_get_format_cap_type(const struct ast_sdp_options *options,
	enum ast_media_type type);

#endif /* _ASTERISK_SDP_OPTIONS_H */
