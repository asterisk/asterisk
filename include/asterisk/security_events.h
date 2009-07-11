/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file
 *
 * \brief Security Event Reporting API
 *
 * \author Russell Bryant <russell@digium.com>
 */

#ifndef __AST_SECURITY_EVENTS_H__
#define __AST_SECURITY_EVENTS_H__

#include "asterisk/event.h"

/* Data structure definitions */
#include "asterisk/security_events_defs.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief Report a security event
 *
 * \param[in] sec security event data.  Callers of this function should never
 *            declare an instance of ast_security_event_common directly.  The
 *            argument should be an instance of a specific security event
 *            descriptor which has ast_security_event_common at the very
 *            beginning.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_security_event_report(const struct ast_security_event_common *sec);

struct ast_security_event_ie_type {
	enum ast_event_ie_type ie_type;
	/*! \brief For internal usage */
	size_t offset;
};

/*!
 * \brief Get the list of required IEs for a given security event sub-type
 *
 * \param[in] event_type security event sub-type
 *
 * \retval NULL invalid event_type
 * \retval non-NULL An array terminated with the value AST_EVENT_IE_END
 *
 * \since 1.6.3
 */
const struct ast_security_event_ie_type *ast_security_event_get_required_ies(
		const enum ast_security_event_type event_type);

/*!
 * \brief Get the list of optional IEs for a given security event sub-type
 *
 * \param[in] event_type security event sub-type
 *
 * \retval NULL invalid event_type
 * \retval non-NULL An array terminated with the value AST_EVENT_IE_END
 *
 * \since 1.6.3
 */
const struct ast_security_event_ie_type *ast_security_event_get_optional_ies(
		const enum ast_security_event_type event_type);

/*!
 * \brief Get the name of a security event sub-type
 *
 * \param[in] event_type security event sub-type
 *
 * \retval NULL if event_type is invalid
 * \retval non-NULL the name of the security event type
 *
 * \since 1.6.3
 */
const char *ast_security_event_get_name(const enum ast_security_event_type event_type);

/*!
 * \brief Get the name of a security event severity
 *
 * \param[in] severity security event severity
 *
 * \retval NULL if severity is invalid
 * \retval non-NULL the name of the security event severity
 *
 * \since 1.6.3
 */
const char *ast_security_event_severity_get_name(
		const enum ast_security_event_severity severity);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* __AST_SECURITY_EVENTS_H__ */
