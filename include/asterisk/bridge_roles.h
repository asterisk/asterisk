/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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

/*! \file
 * \brief Channel Bridging Roles API
 * \author Jonathan Rose <jrose@digium.com>
 */

#ifndef _ASTERISK_BRIDGING_ROLES_H
#define _ASTERISK_BRIDGING_ROLES_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/linkedlists.h"

#define AST_ROLE_LEN 32

/*!
 * \brief Adds a bridge role to a channel
 *
 * \param chan Acquirer of the requested role
 * \param role_name Name of the role being attached
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_channel_add_bridge_role(struct ast_channel *chan, const char *role_name);

/*!
 * \brief Removes a bridge role from a channel
 *
 * \param chan Channel the role is being removed from
 * \param role_name Name of the role being removed
 */
void ast_channel_remove_bridge_role(struct ast_channel *chan, const char *role_name);

/*!
 * \brief Removes all bridge roles currently on a channel
 *
 * \param chan Channel the roles are being removed from
 */
void ast_channel_clear_bridge_roles(struct ast_channel *chan);

/*!
 * \brief Set a role option on a channel
 * \param channel Channel receiving the role option
 * \param role_name Role the role option is applied to
 * \param option Name of the option
 * \param value Value of the option
 *
 * \param 0 on success
 * \retval -1 on failure
 */
int ast_channel_set_bridge_role_option(struct ast_channel *channel, const char *role_name, const char *option, const char *value);

/*!
 * \brief Check if a role exists on a channel
 *
 * \param channel The channel to check
 * \param role_name The name of the role to search for
 *
 * \retval 0 The requested role does not exist on the channel
 * \retval 1 The requested role exists on the channel
 *
 * This is an alternative to \ref ast_bridge_channel_has_role that is useful if bridge
 * roles have not yet been established on a channel's bridge_channel. A possible example of
 * when this could be used is in a bridge v_table's push() callback.
 */
int ast_channel_has_role(struct ast_channel *channel, const char *role_name);

/*!
 * \brief Retrieve the value of a requested role option from a channel
 *
 * \param channel The channel to retrieve the requested option from
 * \param role_name The role to which the option belongs
 * \param option The name of the option to retrieve
 *
 * \retval NULL The option does not exist
 * \retval non-NULL The value of the option
 *
 * This is an alternative to \ref ast_bridge_channel_get_role_option that is useful if bridge
 * roles have not yet been established on a channel's bridge_channel. A possible example of
 * when this could be used is in a bridge v_table's push() callback.
 */
const char *ast_channel_get_role_option(struct ast_channel *channel, const char *role_name, const char *option);

/*!
 * \brief Check to see if a bridge channel inherited a specific role from its channel
 *
 * \param bridge_channel The bridge channel being checked
 * \param role_name Name of the role being checked
 *
 * \retval 0 The bridge channel does not have the requested role
 * \retval 1 The bridge channel does have the requested role
 *
 * \note Before a bridge_channel can effectively check roles against a bridge, ast_bridge_channel_establish_roles
 *       should be called on the bridge_channel so that roles and their respective role options can be copied from the channel
 *       datastore into the bridge_channel roles list. Otherwise this function will just return 0 because the list will be NULL.
 */
int ast_bridge_channel_has_role(struct ast_bridge_channel *bridge_channel, const char *role_name);

/*!
 * \brief Retrieve the value of a requested role option from a bridge channel
 *
 * \param bridge_channel The bridge channel we are retrieving the option from
 * \param role_name Name of the role the option will be retrieved from
 * \param option Name of the option we are retrieving the value of
 *
 * \retval NULL If either the role does not exist on the bridge_channel or the role does exist but the option has not been set
 * \retval The value of the option
 *
 * \note See ast_channel_set_role_option note about the need to call ast_bridge_channel_establish_roles.
 *
 * \note The returned character pointer is only valid as long as the bridge_channel is guaranteed to be alive and hasn't had
 *       ast_bridge_channel_clear_roles called against it (as this will free all roles and role options in the bridge
 *       channel). If you need this value after one of these destruction events occurs, you must make a local copy while it is
 *       still valid.
 */
const char *ast_bridge_channel_get_role_option(struct ast_bridge_channel *bridge_channel, const char *role_name, const char *option);

/*!
 * \brief Clone the roles from a bridge_channel's attached ast_channel onto the bridge_channel's role list
 *
 * \param bridge_channel The bridge channel that we are preparing
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \details
 * This function should always be called when the bridge_channel binds to an ast_channel at some point before the bridge_channel
 * joins or is imparted onto a bridge. Failure to do so will result in an empty role list. While the list remains established,
 * changes to roles on the ast_channel will not propagate to the bridge channel and roles can not be re-established on the bridge
 * channel without first clearing the roles with ast_bridge_roles_bridge_channel_clear_roles.
 */
int ast_bridge_channel_establish_roles(struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Clear all roles from a bridge_channel's role list
 *
 * \param bridge_channel the bridge channel that we are scrubbing
 *
 * \details
 * If roles are already established on a bridge channel, ast_bridge_channel_establish_roles will fail unconditionally
 * without changing any roles. In order to update a bridge channel's roles, they must first be cleared from the bridge channel using
 * this function.
 *
 * \note
 * ast_bridge_channel_clear_roles also serves as the destructor for the role list of a bridge channel.
 */
void ast_bridge_channel_clear_roles(struct ast_bridge_channel *bridge_channel);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BRIDGING_ROLES_H */
