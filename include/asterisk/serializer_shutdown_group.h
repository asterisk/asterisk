/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012-2013, Digium, Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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


#ifndef _ASTERISK_SERIALIZER_SHUTDOWN_GROUP_H
#define _ASTERISK_SERIALIZER_SHUTDOWN_GROUP_H

struct ast_serializer_shutdown_group;

/*!
 * \brief Create a serializer group shutdown control object.
 * \since 13.5.0
 *
 * \return ao2 object to control shutdown of a serializer group.
 */
struct ast_serializer_shutdown_group *ast_serializer_shutdown_group_alloc(void);

/*!
 * \brief Wait for the serializers in the group to shutdown with timeout.
 * \since 13.5.0
 *
 * \param shutdown_group Group shutdown controller. (Returns 0 immediately if NULL)
 * \param timeout Number of seconds to wait for the serializers in the group to shutdown.
 *     Zero if the timeout is disabled.
 *
 * \return Number of serializers that did not get shutdown within the timeout.
 */
int ast_serializer_shutdown_group_join(struct ast_serializer_shutdown_group *shutdown_group, int timeout);

/*!
 * \brief Increment the number of serializer members in the group.
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * \param shutdown_group Group shutdown controller.
 */
 void ast_serializer_shutdown_group_inc(struct ast_serializer_shutdown_group *shutdown_group);

 /*!
 * \brief Decrement the number of serializer members in the group.
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * \param shutdown_group Group shutdown controller.
 */
void ast_serializer_shutdown_group_dec(struct ast_serializer_shutdown_group *shutdown_group);

#endif /* ASTERISK_SERIALIZER_SHUTDOWN_GROUP_H */
