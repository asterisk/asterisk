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
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_PRIVATE_BRIDGING_H
#define _ASTERISK_PRIVATE_BRIDGING_H

struct ast_bridge;
struct ast_bridge_channel;

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
 * \note The dst_bridge and bridge_channel->bridge are assumed already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int bridge_move_do(struct ast_bridge *dst_bridge, struct ast_bridge_channel *bridge_channel,
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
void bridge_merge_do(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge,
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
struct ast_bridge_channel *find_bridge_channel(struct ast_bridge *bridge, struct ast_channel *chan);

#endif /* _ASTERISK_PRIVATE_BRIDGING_H */
