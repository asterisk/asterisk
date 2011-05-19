/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Channel Bridging API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_BRIDGING_TECHNOLOGY_H
#define _ASTERISK_BRIDGING_TECHNOLOGY_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Preference for choosing the bridge technology */
enum ast_bridge_preference {
	/*! Bridge technology should have high precedence over other bridge technologies */
	AST_BRIDGE_PREFERENCE_HIGH = 0,
	/*! Bridge technology is decent, not the best but should still be considered over low */
	AST_BRIDGE_PREFERENCE_MEDIUM,
	/*! Bridge technology is low, it should not be considered unless it is absolutely needed */
	AST_BRIDGE_PREFERENCE_LOW,
};

/*!
 * \brief Structure that is the essence of a bridge technology
 */
struct ast_bridge_technology {
	/*! Unique name to this bridge technology */
	const char *name;
	/*! The capabilities that this bridge technology is capable of */
	int capabilities;
	/*! Preference level that should be used when determining whether to use this bridge technology or not */
	enum ast_bridge_preference preference;
	/*! Callback for when a bridge is being created */
	int (*create)(struct ast_bridge *bridge);
	/*! Callback for when a bridge is being destroyed */
	int (*destroy)(struct ast_bridge *bridge);
	/*! Callback for when a channel is being added to a bridge */
	int (*join)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*! Callback for when a channel is leaving a bridge */
	int (*leave)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*! Callback for when a channel is suspended from the bridge */
	void (*suspend)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*! Callback for when a channel is unsuspended from the bridge */
	void (*unsuspend)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*! Callback to see if a channel is compatible with the bridging technology */
	int (*compatible)(struct ast_bridge_channel *bridge_channel);
	/*! Callback for writing a frame into the bridging technology */
	enum ast_bridge_write_result (*write)(struct ast_bridge *bridge, struct ast_bridge_channel *bridged_channel, struct ast_frame *frame);
	/*! Callback for when a file descriptor trips */
	int (*fd)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, int fd);
	/*! Callback for replacement thread function */
	int (*thread)(struct ast_bridge *bridge);
	/*! Callback for poking a bridge thread */
	int (*poke)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*! Formats that the bridge technology supports */
	format_t formats;
	/*! Bit to indicate whether the bridge technology is currently suspended or not */
	unsigned int suspended:1;
	/*! Module this bridge technology belongs to. Is used for reference counting when creating/destroying a bridge. */
	struct ast_module *mod;
	/*! Linked list information */
	AST_RWLIST_ENTRY(ast_bridge_technology) entry;
};

/*! \brief Register a bridge technology for use
 *
 * \param technology The bridge technology to register
 * \param mod The module that is registering the bridge technology
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_technology_register(&simple_bridge_tech);
 * \endcode
 *
 * This registers a bridge technology declared as the structure
 * simple_bridge_tech with the bridging core and makes it available for
 * use when creating bridges.
 */
int __ast_bridge_technology_register(struct ast_bridge_technology *technology, struct ast_module *mod);

/*! \brief See \ref __ast_bridge_technology_register() */
#define ast_bridge_technology_register(technology) __ast_bridge_technology_register(technology, ast_module_info->self)

/*! \brief Unregister a bridge technology from use
 *
 * \param technology The bridge technology to unregister
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_technology_unregister(&simple_bridge_tech);
 * \endcode
 *
 * This unregisters a bridge technlogy declared as the structure
 * simple_bridge_tech with the bridging core. It will no longer be
 * considered when creating a new bridge.
 */
int ast_bridge_technology_unregister(struct ast_bridge_technology *technology);

/*! \brief Feed notification that a frame is waiting on a channel into the bridging core
 *
 * \param bridge The bridge that the notification should influence
 * \param bridge_channel Bridge channel the notification was received on (if known)
 * \param chan Channel the notification was received on (if known)
 * \param outfd File descriptor that the notification was received on (if known)
 *
 * Example usage:
 *
 * \code
 * ast_bridge_handle_trip(bridge, NULL, chan, -1);
 * \endcode
 *
 * This tells the bridging core that a frame has been received on
 * the channel pointed to by chan and that it should be read and handled.
 *
 * \note This should only be used by bridging technologies.
 */
void ast_bridge_handle_trip(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_channel *chan, int outfd);

/*! \brief Suspend a bridge technology from consideration
 *
 * \param technology The bridge technology to suspend
 *
 * Example usage:
 *
 * \code
 * ast_bridge_technology_suspend(&simple_bridge_tech);
 * \endcode
 *
 * This suspends the bridge technology simple_bridge_tech from being considered
 * when creating a new bridge. Existing bridges using the bridge technology
 * are not affected.
 */
void ast_bridge_technology_suspend(struct ast_bridge_technology *technology);

/*! \brief Unsuspend a bridge technology
 *
 * \param technology The bridge technology to unsuspend
 *
 * Example usage:
 *
 * \code
 * ast_bridge_technology_unsuspend(&simple_bridge_tech);
 * \endcode
 *
 * This makes the bridge technology simple_bridge_tech considered when
 * creating a new bridge again.
 */
void ast_bridge_technology_unsuspend(struct ast_bridge_technology *technology);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BRIDGING_TECHNOLOGY_H */
