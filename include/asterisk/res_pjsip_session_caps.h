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
struct ast_sip_session_media;
struct ast_sip_session_caps;

/*!
 * \brief Allocate a SIP session capabilities object.
 * \since 18.0.0
 *
 * \retval An ao2 allocated SIP session capabilities object, or NULL on error
 */
struct ast_sip_session_caps *ast_sip_session_caps_alloc(void);

/*!
 * \brief Set the incoming call offer capabilities for a session.
 * \since 18.0.0
 *
 * This will replace any capabilities already present.
 *
 * \param caps A session's capabilities object
 * \param cap The capabilities to set it to
 */
void ast_sip_session_set_incoming_call_offer_cap(struct ast_sip_session_caps *caps,
	struct ast_format_cap *cap);

/*!
 * \brief Get the incoming call offer capabilities.
 * \since 18.0.0
 *
 * \note Returned objects reference is not incremented.
 *
 * \param caps A session's capabilities object
 *
 * \retval An incoming call offer capabilities object
 */
const struct ast_format_cap *ast_sip_session_get_incoming_call_offer_cap(
	const struct ast_sip_session_caps *caps);

/*!
 * \brief Make the incoming call offer capabilities for a session.
 * \since 18.0.0
 *
 * Creates and sets a list of joint capabilities between the given remote
 * capabilities, and pre-configured ones. The resulting joint list is then
 * stored, and 'owned' (reference held) by the session.
 *
 * If the incoming capabilities have been set elsewhere, this will not replace
 * those. It will however, return a pointer to the current set.
 *
 * \note Returned object's reference is not incremented.
 *
 * \param session The session
 * \param session_media An associated media session
 * \param remote Capabilities of a device
 *
 * \retval A pointer to the incoming call offer capabilities
 */
const struct ast_format_cap *ast_sip_session_join_incoming_call_offer_cap(
	const struct ast_sip_session *session, const struct ast_sip_session_media *session_media,
	const struct ast_format_cap *remote);

#endif /* RES_PJSIP_SESSION_CAPS_H */
