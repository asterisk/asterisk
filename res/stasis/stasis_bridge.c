/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief Stasis bridge subclass.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/bridge.h"
#include "asterisk/bridge_internal.h"
#include "stasis_bridge.h"

/* ------------------------------------------------------------------- */

/*!
 * \internal
 * \brief Push this channel into the Stasis bridge.
 * \since 12.5.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 */
static int bridge_stasis_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	if (self->allowed_capabilities & STASIS_BRIDGE_MIXING_CAPABILITIES) {
		ast_bridge_channel_update_linkedids(bridge_channel, swap);
		if (ast_test_flag(&self->feature_flags, AST_BRIDGE_FLAG_SMART)) {
			ast_bridge_channel_update_accountcodes(bridge_channel, swap);
		}
	}

	return ast_bridge_base_v_table.push(self, bridge_channel, swap);
}

/*!
 * \internal
 * \brief Pull this channel from the Stasis bridge.
 * \since 12.5.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_stasis_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	if ((self->allowed_capabilities & STASIS_BRIDGE_MIXING_CAPABILITIES)
		&& ast_test_flag(&self->feature_flags, AST_BRIDGE_FLAG_SMART)) {
		ast_bridge_channel_update_accountcodes(NULL, bridge_channel);
	}

	ast_bridge_base_v_table.pull(self, bridge_channel);
}

static struct ast_bridge_methods bridge_stasis_v_table;

struct ast_bridge *bridge_stasis_new(uint32_t capabilities, unsigned int flags, const char *name, const char *id)
{
	void *bridge;

	bridge = bridge_alloc(sizeof(struct ast_bridge), &bridge_stasis_v_table);
	bridge = bridge_base_init(bridge, capabilities, flags, "Stasis", name, id);
	bridge = bridge_register(bridge);

	return bridge;
}

void bridge_stasis_init(void)
{
	/* Setup the Stasis bridge subclass v_table. */
	bridge_stasis_v_table = ast_bridge_base_v_table;
	bridge_stasis_v_table.name = "stasis";
	bridge_stasis_v_table.push = bridge_stasis_push;
	bridge_stasis_v_table.pull = bridge_stasis_pull;
}
