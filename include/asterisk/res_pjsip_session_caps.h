/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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
#ifndef RES_PJSIP_SESSION_CAPS_H
#define RES_PJSIP_SESSION_CAPS_H

struct ast_format_cap;
struct ast_sip_session;

/*!
 * \brief Create joint capabilities
 * \since 18.0.0
 *
 * Creates a list of joint capabilities between the given remote capabilities, and local ones.
 * "local" and "remote" reference the values in ast_sip_call_codec_pref.
 *
 * \param remote The "remote" capabilities
 * \param local The "local" capabilities
 * \param media_type The media type
 * \param codec_pref One or more of enum ast_sip_call_codec_pref
 *
 * \retval A pointer to the joint capabilities (which may be empty).
 *         NULL will be returned only if no memory was available to allocate the structure.
 * \note Returned object's reference must be released at some point,
 */
struct ast_format_cap *ast_sip_create_joint_call_cap(const struct ast_format_cap *remote,
	struct ast_format_cap *local, enum ast_media_type media_type,
	struct ast_flags codec_pref);

/*!
 * \brief Create a new stream of joint capabilities
 * \since 18.0.0
 *
 * Creates a new stream with capabilities between the given session's local capabilities,
 * and the remote stream's.  Codec selection is based on the session->endpoint's codecs, the
 * session->endpoint's codec call preferences, and the stream passed by the core (for
 * outgoing calls) or created by the incoming SDP (for incoming calls).
 *
 * \param session The session
 * \param remote The remote stream
 *
 * \retval A pointer to a new stream with the joint capabilities (which may be empty),
 *         NULL will be returned only if no memory was available to allocate the structure.
 */
struct ast_stream *ast_sip_session_create_joint_call_stream(const struct ast_sip_session *session,
	struct ast_stream *remote);

/*!
 * \brief Create joint capabilities
 * \since 18.0.0
 *
 * Creates a list of joint capabilities between the given session's local capabilities,
 * and the remote capabilities. Codec selection is based on the session->endpoint's codecs, the
 * session->endpoint's codec call preferences, and the "remote" capabilities passed by the core (for
 * outgoing calls) or created by the incoming SDP (for incoming calls).
 *
 * \param session The session
 * \param media_type The media type
 * \param remote Capabilities received in an SDP offer or from the core
 *
 * \retval A pointer to the joint capabilities (which may be empty).
 *         NULL will be returned only if no memory was available to allocate the structure.
 * \note Returned object's reference must be released at some point,
 */
struct ast_format_cap *ast_sip_session_create_joint_call_cap(const struct ast_sip_session *session,
	enum ast_media_type media_type, const struct ast_format_cap *remote);

#endif /* RES_PJSIP_SESSION_CAPS_H */
