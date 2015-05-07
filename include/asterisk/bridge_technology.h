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

/*!
 * \brief Base preference values for choosing a bridge technology.
 *
 * \note Higher is more preference.
 */
enum ast_bridge_preference {
	AST_BRIDGE_PREFERENCE_BASE_HOLDING  = 50,
	AST_BRIDGE_PREFERENCE_BASE_EARLY    = 100,
	AST_BRIDGE_PREFERENCE_BASE_NATIVE   = 90,
	AST_BRIDGE_PREFERENCE_BASE_1TO1MIX  = 50,
	AST_BRIDGE_PREFERENCE_BASE_MULTIMIX = 10,
};

/*!
 * \brief Structure specific to bridge technologies capable of
 * performing talking optimizations.
 */
struct ast_bridge_tech_optimizations {
	/*! The amount of time in ms that talking must be detected before
	 *  the dsp determines that talking has occurred */
	unsigned int talking_threshold;
	/*! The amount of time in ms that silence must be detected before
	 *  the dsp determines that talking has stopped */
	unsigned int silence_threshold;
	/*! Whether or not the bridging technology should drop audio
	 *  detected as silence from the mix. */
	unsigned int drop_silence:1;
};

/*!
 * \brief Structure that is the essence of a bridge technology
 */
struct ast_bridge_technology {
	/*! Unique name to this bridge technology */
	const char *name;
	/*! The capabilities that this bridge technology is capable of.  This has nothing to do with
	 * format capabilities. */
	uint32_t capabilities;
	/*! Preference level that should be used when determining whether to use this bridge technology or not */
	enum ast_bridge_preference preference;
	/*!
	 * \brief Create a bridge technology instance for a bridge.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure
	 *
	 * \note On entry, bridge may or may not already be locked.
	 * However, it can be accessed as if it were locked.
	 */
	int (*create)(struct ast_bridge *bridge);
	/*!
	 * \brief Request a bridge technology instance start operations.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure
	 *
	 * \note On entry, bridge may or may not already be locked.
	 * However, it can be accessed as if it were locked.
	 */
	int (*start)(struct ast_bridge *bridge);
	/*!
	 * \brief Request a bridge technology instance stop in preparation for being destroyed.
	 *
	 * \note On entry, bridge is already locked.
	 */
	void (*stop)(struct ast_bridge *bridge);
	/*!
	 * \brief Destroy a bridging technology instance for a bridge.
	 *
	 * \note On entry, bridge must NOT be locked.
	 */
	void (*destroy)(struct ast_bridge *bridge);
	/*!
	 * \brief Add a channel to a bridging technology instance for a bridge.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure
	 *
	 * \note On entry, bridge is already locked.
	 */
	int (*join)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*!
	 * \brief Remove a channel from a bridging technology instance for a bridge.
	 *
	 * \note On entry, bridge is already locked.
	 * \note Do not make assumptions about the number of channels in the bridge when
	 * this callback is called. When a channel is swapped into a bridge for another
	 * channel, the leave callback is called after the new channel has been added to
	 * the bridge.
	 */
	void (*leave)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*!
	 * \brief Suspend a channel on a bridging technology instance for a bridge.
	 *
	 * \note On entry, bridge is already locked.
	 */
	void (*suspend)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*!
	 * \brief Unsuspend a channel on a bridging technology instance for a bridge.
	 *
	 * \note On entry, bridge is already locked.
	 */
	void (*unsuspend)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
	/*!
	 * \brief Check if a bridge is compatible with the bridging technology.
	 *
	 * \retval 0 if not compatible
	 * \retval non-zero if compatible
	 *
	 * \note On entry, bridge may or may not already be locked.
	 * However, it can be accessed as if it were locked.
	 */
	int (*compatible)(struct ast_bridge *bridge);
	/*!
	 * \brief Write a frame into the bridging technology instance for a bridge.
	 *
	 * \note The bridge must be tolerant of bridge_channel being NULL.
	 *
	 * \retval 0 Frame accepted into the bridge.
	 * \retval -1 Frame needs to be deferred.
	 *
	 * \note On entry, bridge is already locked.
	 */
	int (*write)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame);
	/*! TRUE if the bridge technology is currently suspended. */
	unsigned int suspended:1;
	/*! Module this bridge technology belongs to. It is used for reference counting bridges using the technology. */
	struct ast_module *mod;
	/*! Linked list information */
	AST_RWLIST_ENTRY(ast_bridge_technology) entry;
};

/*!
 * \brief Register a bridge technology for use
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
#define ast_bridge_technology_register(technology) __ast_bridge_technology_register(technology, AST_MODULE_SELF)

/*!
 * \brief Unregister a bridge technology from use
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

/*!
 * \brief Suspend a bridge technology from consideration
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

/*!
 * \brief Unsuspend a bridge technology
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
