/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
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

/*!
 * \file
 * \brief Private Bridging API
 *
 * Functions in this file are intended to be used by the Bridging API,
 * bridge mixing technologies, and bridge sub-classes. Users of bridges
 * that do not fit those three categories should *not* use the API
 * defined in this file.
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_PRIVATE_BRIDGING_H
#define _ASTERISK_PRIVATE_BRIDGING_H

struct ast_bridge;
struct ast_bridge_channel;
struct ast_bridge_methods;

/*!
 * \brief Register the new bridge with the system.
 * \since 12.0.0
 *
 * \param bridge What to register. (Tolerates a NULL pointer)
 *
 * \code
 * struct ast_bridge *ast_bridge_basic_new(uint32_t capabilities, int flags, uint32 dtmf_features)
 * {
 *     void *bridge;
 *
 *     bridge = bridge_alloc(sizeof(struct ast_bridge_basic), &ast_bridge_basic_v_table);
 *     bridge = bridge_base_init(bridge, capabilities, flags);
 *     bridge = ast_bridge_basic_init(bridge, dtmf_features);
 *     bridge = bridge_register(bridge);
 *     return bridge;
 * }
 * \endcode
 *
 * \note This must be done after a bridge constructor has
 * completed setting up the new bridge but before it returns.
 *
 * \note After a bridge is registered, ast_bridge_destroy() must
 * eventually be called to get rid of the bridge.
 *
 * \retval bridge on success.
 * \retval NULL on error.
 */
struct ast_bridge *bridge_register(struct ast_bridge *bridge);

/*!
 * \internal
 * \brief Allocate the bridge class object memory.
 * \since 12.0.0
 *
 * \param size Size of the bridge class structure to allocate.
 * \param v_table Bridge class virtual method table.
 *
 * \retval bridge on success.
 * \retval NULL on error.
 */
struct ast_bridge *bridge_alloc(size_t size, const struct ast_bridge_methods *v_table);

/*!
 * \brief Initialize the base class of the bridge.
 *
 * \param self Bridge to operate upon. (Tolerates a NULL pointer)
 * \param capabilities The capabilities that we require to be used on the bridge
 * \param flags Flags that will alter the behavior of the bridge
 * \param creator Entity that created the bridge (optional)
 * \param name Name given to the bridge by its creator (optional, requires named creator)
 * \param id Unique ID given to the bridge by its creator (optional)
 *
 * \retval self on success
 * \retval NULL on failure, self is already destroyed
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge *bridge;
 * bridge = bridge_alloc(sizeof(*bridge), &ast_bridge_base_v_table);
 * bridge = bridge_base_init(bridge, AST_BRIDGE_CAPABILITY_1TO1MIX, AST_BRIDGE_FLAG_DISSOLVE_HANGUP, NULL, NULL, NULL);
 * \endcode
 *
 * This creates a no frills two party bridge that will be
 * destroyed once one of the channels hangs up.
 */
struct ast_bridge *bridge_base_init(struct ast_bridge *self, uint32_t capabilities, unsigned int flags, const char *creator, const char *name, const char *id);

/*!
 * \internal
 * \brief Move a bridge channel from one bridge to another.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of bridge channel move.
 * \param bridge_channel Channel moving from one bridge to another.
 * \param attempt_recovery TRUE if failure attempts to push channel back into original bridge.
 * \param optimized Indicates whether the move is part of an unreal channel optimization.
 *
 * \note A ref is not held by bridge_channel->swap when calling because the
 * move with swap happens immediately.
 *
 * \note The dst_bridge and bridge_channel->bridge are assumed already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int bridge_do_move(struct ast_bridge *dst_bridge, struct ast_bridge_channel *bridge_channel,
		int attempt_recovery, unsigned int optimized);

/*!
 * \internal
 * \brief Do the merge of two bridges.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of merge.
 * \param src_bridge Source bridge of merge.
 * \param kick_me Array of channels to kick from the bridges.
 * \param num_kick Number of channels in the kick_me array.
 * \param optimized Indicates whether the merge is part of an unreal channel optimization.
 *
 * \return Nothing
 *
 * \note The two bridges are assumed already locked.
 *
 * This moves the channels in src_bridge into the bridge pointed
 * to by dst_bridge.
 */
void bridge_do_merge(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge,
		struct ast_bridge_channel **kick_me, unsigned int num_kick, unsigned int optimized);

/*!
 * \internal
 * \brief Helper function to find a bridge channel given a channel.
 *
 * \param bridge What to search
 * \param chan What to search for.
 *
 * \note On entry, bridge is already locked.
 *
 * \retval bridge_channel if channel is in the bridge.
 * \retval NULL if not in bridge.
 */
struct ast_bridge_channel *bridge_find_channel(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \internal
 * \brief Adjust the bridge merge inhibit request count.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 * \param request Inhibit request increment.
 *     (Positive to add requests.  Negative to remove requests.)
 *
 * \note This function assumes bridge is locked.
 *
 * \return Nothing
 */
void bridge_merge_inhibit_nolock(struct ast_bridge *bridge, int request);

/*!
 * \internal
 * \brief Notify the bridge that it has been reconfigured.
 * \since 12.0.0
 *
 * \param bridge Reconfigured bridge.
 * \param colp_update Whether to perform COLP updates.
 *
 * \details
 * After a series of bridge_channel_internal_push and
 * bridge_channel_internal_pull calls, you need to call this function
 * to cause the bridge to complete restructuring for the change
 * in the channel makeup of the bridge.
 *
 * \note On entry, the bridge is already locked.
 *
 * \return Nothing
 */
void bridge_reconfigured(struct ast_bridge *bridge, unsigned int colp_update);

/*!
 * \internal
 * \brief Dissolve the bridge.
 * \since 12.0.0
 *
 * \param bridge Bridge to eject all channels
 * \param cause Cause of bridge being dissolved.  (If cause <= 0 then use AST_CAUSE_NORMAL_CLEARING)
 *
 * \details
 * Force out all channels that are not already going out of the
 * bridge.  Any new channels joining will leave immediately.
 *
 * \note On entry, bridge is already locked.
 *
 * \return Nothing
 */
void bridge_dissolve(struct ast_bridge *bridge, int cause);

#endif /* _ASTERISK_PRIVATE_BRIDGING_H */
