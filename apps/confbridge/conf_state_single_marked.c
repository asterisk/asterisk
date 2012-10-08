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
 * \brief Confbridge state handling for the SINGLE_MARKED state
 *
 * \author\verbatim Terry Wilson <twilson@digium.com> \endverbatim
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "include/confbridge.h"
#include "include/conf_state.h"

static void join_active(struct conference_bridge_user *cbu);
static void join_marked(struct conference_bridge_user *cbu);
static void leave_marked(struct conference_bridge_user *cbu);
static void transition_to_single_marked(struct conference_bridge_user *cbu);

struct conference_state STATE_SINGLE_MARKED = {
	.name = "SINGLE_MARKED",
	.join_unmarked = join_active,
	.join_waitmarked = join_active,
	.join_marked = join_marked,
	.leave_marked = leave_marked,
	.entry = transition_to_single_marked,
};
struct conference_state *CONF_STATE_SINGLE_MARKED = &STATE_SINGLE_MARKED;

static void join_active(struct conference_bridge_user *cbu)
{
	conf_add_user_active(cbu->conference_bridge, cbu);
	conf_handle_second_active(cbu->conference_bridge);

	conf_change_state(cbu, CONF_STATE_MULTI_MARKED);
}

static void join_marked(struct conference_bridge_user *cbu)
{
	conf_add_user_marked(cbu->conference_bridge, cbu);
	conf_handle_second_active(cbu->conference_bridge);

	conf_change_state(cbu, CONF_STATE_MULTI_MARKED);
}

static void leave_marked(struct conference_bridge_user *cbu)
{
	conf_remove_user_marked(cbu->conference_bridge, cbu);

	conf_change_state(cbu, CONF_STATE_EMPTY);
}

static void transition_to_single_marked(struct conference_bridge_user *cbu)
{
	conf_mute_only_active(cbu->conference_bridge);
}
