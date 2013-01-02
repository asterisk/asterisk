/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Terry Wilson
 *
 * Terry Wilson <twilson@digium.com>
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
 *
 * Please follow coding guidelines
 * http://svn.digium.com/view/asterisk/trunk/doc/CODING-GUIDELINES
 */

/*! \file
 *
 * \brief Confbridge state handling for the EMPTY state
 *
 * \author\verbatim Terry Wilson <twilson@digium.com> \endverbatim
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/devicestate.h"
#include "include/confbridge.h"
#include "include/conf_state.h"

static void join_unmarked(struct conference_bridge_user *cbu);
static void join_waitmarked(struct conference_bridge_user *cbu);
static void join_marked(struct conference_bridge_user *cbu);
static void transition_to_empty(struct conference_bridge_user *cbu);

struct conference_state STATE_EMPTY = {
	.name = "EMPTY",
	.join_unmarked = join_unmarked,
	.join_waitmarked = join_waitmarked,
	.join_marked = join_marked,
	.entry = transition_to_empty,
};

struct conference_state *CONF_STATE_EMPTY = &STATE_EMPTY;

static void join_unmarked(struct conference_bridge_user *cbu)
{
	conf_add_user_active(cbu->conference_bridge, cbu);
	conf_handle_first_join(cbu->conference_bridge);
	conf_add_post_join_action(cbu, conf_handle_only_unmarked);

	conf_change_state(cbu, CONF_STATE_SINGLE);
}

static void join_waitmarked(struct conference_bridge_user *cbu)
{
	conf_default_join_waitmarked(cbu);
	conf_handle_first_join(cbu->conference_bridge);

	conf_change_state(cbu, CONF_STATE_INACTIVE);
}

static void join_marked(struct conference_bridge_user *cbu)
{
	conf_add_user_marked(cbu->conference_bridge, cbu);
	conf_handle_first_join(cbu->conference_bridge);
	conf_add_post_join_action(cbu, conf_handle_first_marked_common);

	conf_change_state(cbu, CONF_STATE_SINGLE_MARKED);
}

static void transition_to_empty(struct conference_bridge_user *cbu)
{
	/* Set device state to "not in use" */
	ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "confbridge:%s", cbu->conference_bridge->name);
	conf_ended(cbu->conference_bridge);
}
