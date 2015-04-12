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
 *
 * \brief Channel Bridging Roles API
 *
 * \author Jonathan Rose <jrose@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <signal.h>

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/datastore.h"
#include "asterisk/linkedlists.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_roles.h"
#include "asterisk/stringfields.h"

struct bridge_role_option {
	AST_LIST_ENTRY(bridge_role_option) list;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(option);
		AST_STRING_FIELD(value);
	);
};

struct bridge_role {
	AST_LIST_ENTRY(bridge_role) list;
	AST_LIST_HEAD(, bridge_role_option) options;
	char role[AST_ROLE_LEN];
};

struct bridge_roles_datastore {
	AST_LIST_HEAD(, bridge_role) role_list;
};

/*!
 * \internal
 * \brief Destructor function for a bridge role
 * \since 12.0.0
 *
 * \param role bridge_role being destroyed
 *
 * \return Nothing
 */
static void bridge_role_destroy(struct bridge_role *role)
{
	struct bridge_role_option *role_option;
	while ((role_option = AST_LIST_REMOVE_HEAD(&role->options, list))) {
		ast_string_field_free_memory(role_option);
		ast_free(role_option);
	}
	ast_free(role);
}

/*!
 * \internal
 * \brief Destructor function for bridge role datastores
 * \since 12.0.0
 *
 * \param data Pointer to the datastore being destroyed
 *
 * \return Nothing
 */
static void bridge_role_datastore_destroy(void *data)
{
	struct bridge_roles_datastore *roles_datastore = data;
	struct bridge_role *role;

	while ((role = AST_LIST_REMOVE_HEAD(&roles_datastore->role_list, list))) {
		bridge_role_destroy(role);
	}

	ast_free(roles_datastore);
}

static const struct ast_datastore_info bridge_role_info = {
	.type = "bridge roles",
	.destroy = bridge_role_datastore_destroy,
};

/*!
 * \internal
 * \brief Setup a bridge role datastore on a channel
 * \since 12.0.0
 *
 * \param chan Chan the datastore is being setup on
 *
 * \retval NULL if failed
 * \retval pointer to the newly created datastore
 */
static struct bridge_roles_datastore *setup_bridge_roles_datastore(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct bridge_roles_datastore *roles_datastore = NULL;

	if (!(datastore = ast_datastore_alloc(&bridge_role_info, NULL))) {
		return NULL;
	}

	if (!(roles_datastore = ast_calloc(1, sizeof(*roles_datastore)))) {
		ast_datastore_free(datastore);
		return NULL;
	}

	datastore->data = roles_datastore;
	ast_channel_datastore_add(chan, datastore);
	return roles_datastore;
}

/*!
 * \internal
 * \brief Get the bridge_roles_datastore from a channel if it exists. Don't create one if it doesn't.
 * \since 12.0.0
 *
 * \param chan Channel we want the bridge_roles_datastore from
 *
 * \retval NULL if we can't find the datastore
 * \retval pointer to the bridge_roles_datastore
 */
static struct bridge_roles_datastore *fetch_bridge_roles_datastore(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &bridge_role_info, NULL))) {
		ast_channel_unlock(chan);
		return NULL;
	}
	ast_channel_unlock(chan);

	return datastore->data;
}

/*!
 * \internal
 * \brief Get the bridge_roles_datastore from a channel if it exists. If not, create one.
 * \since 12.0.0
 *
 * \param chan Channel we want the bridge_roles_datastore from
 *
 * \retval NULL If we can't find and can't create the datastore
 * \retval pointer to the bridge_roles_datastore
 */
static struct bridge_roles_datastore *fetch_or_create_bridge_roles_datastore(struct ast_channel *chan)
{
	struct bridge_roles_datastore *roles_datastore;

	ast_channel_lock(chan);
	roles_datastore = fetch_bridge_roles_datastore(chan);
	if (!roles_datastore) {
		roles_datastore = setup_bridge_roles_datastore(chan);
	}
	ast_channel_unlock(chan);

	return roles_datastore;
}

/*!
 * \internal
 * \brief Obtain a role from a bridge_roles_datastore if the datastore has it
 * \since 12.0.0
 *
 * \param roles_datastore The bridge_roles_datastore we are looking for the role of
 * \param role_name Name of the role being sought
 *
 * \retval NULL if the datastore does not have the requested role
 * \retval pointer to the requested role
 */
static struct bridge_role *get_role_from_datastore(struct bridge_roles_datastore *roles_datastore, const char *role_name)
{
	struct bridge_role *role;

	AST_LIST_TRAVERSE(&roles_datastore->role_list, role, list) {
		if (!strcmp(role->role, role_name)) {
			return role;
		}
	}

	return NULL;
}

/*!
 * \internal
 * \brief Obtain a role from a channel structure if the channel's datastore has it
 * \since 12.0.0
 *
 * \param channel The channel we are checking the role of
 * \param role_name Name of the role sought
 *
 * \retval NULL if the channel's datastore does not have the requested role
 * \retval pointer to the requested role
 */
static struct bridge_role *get_role_from_channel(struct ast_channel *channel, const char *role_name)
{
	struct bridge_roles_datastore *roles_datastore = fetch_bridge_roles_datastore(channel);
	return roles_datastore ? get_role_from_datastore(roles_datastore, role_name) : NULL;
}

/*!
 * \internal
 * \brief Obtain a role option from a bridge role if it exists in the bridge role's option list
 * \since 12.0.0
 *
 * \param role a pointer to the bridge role wea re searching for the option of
 * \param option Name of the option sought
 *
 * \retval NULL if the bridge role doesn't have the requested option
 * \retval pointer to the requested option
 */
static struct bridge_role_option *get_role_option(struct bridge_role *role, const char *option)
{
	struct bridge_role_option *role_option = NULL;
	AST_LIST_TRAVERSE(&role->options, role_option, list) {
		if (!strcmp(role_option->option, option)) {
			return role_option;
		}
	}
	return NULL;
}

/*!
 * \internal
 * \brief Setup a bridge role on an existing bridge role datastore
 * \since 12.0.0
 *
 * \param roles_datastore bridge_roles_datastore receiving the new role
 * \param role_name Name of the role being received
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int setup_bridge_role(struct bridge_roles_datastore *roles_datastore, const char *role_name)
{
	struct bridge_role *role;
	role = ast_calloc(1, sizeof(*role));

	if (!role) {
		return -1;
	}

	ast_copy_string(role->role, role_name, sizeof(role->role));

	AST_LIST_INSERT_TAIL(&roles_datastore->role_list, role, list);
	ast_debug(3, "Set role '%s'\n", role_name);

	return 0;
}

/*!
 * \internal
 * \brief Setup a bridge role option on an existing bridge role
 * \since 12.0.0
 *
 * \param role The role receiving the option
 * \param option Name of the option
 * \param value the option's value
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int setup_bridge_role_option(struct bridge_role *role, const char *option, const char *value)
{
	struct bridge_role_option *role_option;

	if (!value) {
		value = "";
	}

	role_option = ast_calloc(1, sizeof(*role_option));
	if (!role_option) {
		return -1;
	}

	if (ast_string_field_init(role_option, 32)) {
		ast_free(role_option);
		return -1;
	}

	ast_string_field_set(role_option, option, option);
	ast_string_field_set(role_option, value, value);

	AST_LIST_INSERT_TAIL(&role->options, role_option, list);

	return 0;
}

int ast_channel_add_bridge_role(struct ast_channel *chan, const char *role_name)
{
	struct bridge_roles_datastore *roles_datastore = fetch_or_create_bridge_roles_datastore(chan);

	if (!roles_datastore) {
		ast_log(LOG_WARNING, "Unable to set up bridge role datastore on channel %s\n", ast_channel_name(chan));
		return -1;
	}

	/* Check to make sure we aren't adding a redundant role */
	if (get_role_from_datastore(roles_datastore, role_name)) {
		ast_debug(2, "Bridge role %s is already applied to the channel %s\n", role_name, ast_channel_name(chan));
		return 0;
	}

	/* It wasn't already there, so we can just finish setting it up now. */
	return setup_bridge_role(roles_datastore, role_name);
}

void ast_channel_remove_bridge_role(struct ast_channel *chan, const char *role_name)
{
	struct bridge_roles_datastore *roles_datastore = fetch_bridge_roles_datastore(chan);
	struct bridge_role *role;

	if (!roles_datastore) {
		/* The roles datastore didn't already exist, so there is no need to remove a role */
		ast_debug(2, "Role %s did not exist on channel %s\n", role_name, ast_channel_name(chan));
		return;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&roles_datastore->role_list, role, list) {
		if (!strcmp(role->role, role_name)) {
			ast_debug(2, "Removing bridge role %s from channel %s\n", role_name, ast_channel_name(chan));
			AST_LIST_REMOVE_CURRENT(list);
			bridge_role_destroy(role);
			return;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ast_debug(2, "Role %s did not exist on channel %s\n", role_name, ast_channel_name(chan));
}

void ast_channel_clear_bridge_roles(struct ast_channel *chan)
{
	struct bridge_roles_datastore *roles_datastore = fetch_bridge_roles_datastore(chan);
	struct bridge_role *role;

	if (!roles_datastore) {
		/* The roles datastore didn't already exist, so there is no need to remove any roles */
		ast_debug(2, "Roles did not exist on channel %s\n", ast_channel_name(chan));
		return;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&roles_datastore->role_list, role, list) {
		ast_debug(2, "Removing bridge role %s from channel %s\n", role->role, ast_channel_name(chan));
		AST_LIST_REMOVE_CURRENT(list);
		bridge_role_destroy(role);
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

int ast_channel_set_bridge_role_option(struct ast_channel *channel, const char *role_name, const char *option, const char *value)
{
	struct bridge_role *role = get_role_from_channel(channel, role_name);
	struct bridge_role_option *role_option;

	if (!role) {
		return -1;
	}

	role_option = get_role_option(role, option);

	if (role_option) {
		ast_string_field_set(role_option, value, value);
		return 0;
	}

	return setup_bridge_role_option(role, option, value);
}

int ast_channel_has_role(struct ast_channel *channel, const char *role_name)
{
	return get_role_from_channel(channel, role_name) ? 1 : 0;
}

const char *ast_channel_get_role_option(struct ast_channel *channel, const char *role_name, const char *option)
{
	struct bridge_role *role;
	struct bridge_role_option *role_option;

	role = get_role_from_channel(channel, role_name);
	if (!role) {
		return NULL;
	}

	role_option = get_role_option(role, option);

	return role_option ? role_option->value : NULL;
}

int ast_bridge_channel_has_role(struct ast_bridge_channel *bridge_channel, const char *role_name)
{
	if (!bridge_channel->bridge_roles) {
		return 0;
	}

	return get_role_from_datastore(bridge_channel->bridge_roles, role_name) ? 1 : 0;
}

const char *ast_bridge_channel_get_role_option(struct ast_bridge_channel *bridge_channel, const char *role_name, const char *option)
{
	struct bridge_role *role;
	struct bridge_role_option *role_option = NULL;

	if (!bridge_channel->bridge_roles) {
		return NULL;
	}

	role = get_role_from_datastore(bridge_channel->bridge_roles, role_name);

	if (!role) {
		return NULL;
	}

	role_option = get_role_option(role, option);

	return role_option ? role_option->value : NULL;
}

int ast_bridge_channel_establish_roles(struct ast_bridge_channel *bridge_channel)
{
	struct bridge_roles_datastore *roles_datastore;
	struct bridge_role *role = NULL;
	struct bridge_role_option *role_option;

	if (!bridge_channel->chan) {
		ast_debug(2, "Attempted to set roles on a bridge channel that has no associated channel. That's a bad idea.\n");
		return -1;
	}

	if (bridge_channel->bridge_roles) {
		ast_debug(2, "Attempted to reset roles while roles were already established. Purge existing roles first.\n");
		return -1;
	}

	roles_datastore = fetch_bridge_roles_datastore(bridge_channel->chan);
	if (!roles_datastore) {
		/* No roles to establish. */
		return 0;
	}

	if (!(bridge_channel->bridge_roles = ast_calloc(1, sizeof(*bridge_channel->bridge_roles)))) {
		return -1;
	}

	AST_LIST_TRAVERSE(&roles_datastore->role_list, role, list) {
		struct bridge_role *this_role_copy;

		if (setup_bridge_role(bridge_channel->bridge_roles, role->role)) {
			/* We need to abandon the copy because we couldn't setup a role */
			ast_bridge_channel_clear_roles(bridge_channel);
			return -1;
		}
		this_role_copy = AST_LIST_LAST(&bridge_channel->bridge_roles->role_list);

		AST_LIST_TRAVERSE(&role->options, role_option, list) {
			if (setup_bridge_role_option(this_role_copy, role_option->option, role_option->value)) {
				/* We need to abandon the copy because we couldn't setup a role option */
				ast_bridge_channel_clear_roles(bridge_channel);
				return -1;
			}
		}
	}

	return 0;
}

void ast_bridge_channel_clear_roles(struct ast_bridge_channel *bridge_channel)
{
	if (bridge_channel->bridge_roles) {
		bridge_role_datastore_destroy(bridge_channel->bridge_roles);
		bridge_channel->bridge_roles = NULL;
	}
}
