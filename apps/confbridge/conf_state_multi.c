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
 * \brief Confbridge state handling for the MULTI state
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

static void join_unmarked(struct conference_bridge_user *cbu);
static void join_marked(struct conference_bridge_user *cbu);
static void leave_unmarked(struct conference_bridge_user *cbu);
void transition_to_multi(struct conference_bridge_user *cbu);

struct conference_state STATE_MULTI = {
	.name = "MULTI",
	.join_unmarked = join_unmarked,
	.join_waitmarked = conf_default_join_waitmarked,
	.join_marked = join_marked,
	.leave_unmarked = leave_unmarked,
	.leave_waitmarked = conf_default_leave_waitmarked,
	.entry = transition_to_multi,
};
struct conference_state *CONF_STATE_MULTI = &STATE_MULTI;

static void join_unmarked(struct conference_bridge_user *cbu)
{
	conf_add_user_active(cbu->conference_bridge, cbu);
}

static void join_marked(struct conference_bridge_user *cbu)
{
	conf_add_user_marked(cbu->conference_bridge, cbu);

	conf_change_state(cbu, CONF_STATE_MULTI_MARKED);
}

static void leave_unmarked(struct conference_bridge_user *cbu)
{
	conf_remove_user_active(cbu->conference_bridge, cbu);
	if (cbu->conference_bridge->activeusers == 1) {
		conf_change_state(cbu, CONF_STATE_SINGLE);
	}
}

void transition_to_multi(struct conference_bridge_user *cbu)
{
	return;
}
