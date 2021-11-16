/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jason Parker <jparker@digium.com>
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

#ifndef _ASTERISK_STASIS_SYSTEM_H
#define _ASTERISK_STASIS_SYSTEM_H

#include "asterisk/stasis.h"

/*!
 * \since 12
 * \brief Publish a channel driver outgoing registration message
 *
 * \param channeltype The channel driver that published the message
 * \param username The username that was used to register
 * \param domain The domain that was used to register
 * \param status The result of the registration
 * \param cause The reason for the result
 */
void ast_system_publish_registry(const char *channeltype, const char *username, const char *domain, const char *status, const char *cause);

/*!
 * \since 12
 * \brief A \ref stasis topic which publishes messages regarding system changes
 *
 * \return \ref stasis_topic for system level changes
 * \retval NULL on error
 */
struct stasis_topic *ast_system_topic(void);

/*!
 * \since 12
 * \brief A \ref stasis_message_type for network changes
 *
 * \retval NULL on error
 * \return \ref stasis_message_type for network changes
 *
 * \note Messages of this type should always be issued on and expected from
 *       the \ref ast_system_topic \ref stasis topic
 */
struct stasis_message_type *ast_network_change_type(void);

/*!
 * \brief A \ref stasis_message_type for outbound registration.
 * \since 12
 */
struct stasis_message_type *ast_system_registry_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Available messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_available_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Offer Timer Start messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_offertimerstart_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Requested messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_requested_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Request Acknowledged messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_requestacknowledged_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Caller Stop Monitoring messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_callerstopmonitoring_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Caller Start Monitoring messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_callerstartmonitoring_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Caller Recalling messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_callerrecalling_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Recall Complete messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_recallcomplete_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Failure messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_failure_type(void);

/*!
 * \brief A \ref stasis_message_type for CCSS Monitor Failed messages.
 * \since 12
 */
struct stasis_message_type *ast_cc_monitorfailed_type(void);

/*!
 * \brief A \ref stasis_message_type for Cluster discovery
 * \since 13.11.0
 */
struct stasis_message_type *ast_cluster_discovery_type(void);

/*!
 * \brief Initialize the stasis system topic and message types
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_stasis_system_init(void);

#endif /* _ASTERISK_STASIS_SYSTEM_H */
