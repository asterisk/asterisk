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
 * \brief Confbridge state handling for the SINGLE state
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

static void join_unmarked(struct confbridge_user *user);
static void join_marked(struct confbridge_user *user);
static void leave_unmarked(struct confbridge_user *user);
static void transition_to_single(struct confbridge_user *user);

struct confbridge_state STATE_SINGLE = {
	.name = "SINGLE",
	.join_unmarked = join_unmarked,
	.join_waitmarked = conf_default_join_waitmarked,
	.join_marked = join_marked,
	.leave_unmarked = leave_unmarked,
	.leave_waitmarked = conf_default_leave_waitmarked,
	.entry = transition_to_single,
};
struct confbridge_state *CONF_STATE_SINGLE = &STATE_SINGLE;

static void join_unmarked(struct confbridge_user *user)
{
	conf_add_user_active(user->conference, user);
	conf_handle_second_active(user->conference);
	conf_update_user_mute(user);

	conf_change_state(user, CONF_STATE_MULTI);
}

static void join_marked(struct confbridge_user *user)
{
	conf_add_user_marked(user->conference, user);
	conf_handle_second_active(user->conference);
	conf_update_user_mute(user);

	conf_change_state(user, CONF_STATE_MULTI_MARKED);
}

static void leave_unmarked(struct confbridge_user *user)
{
	conf_remove_user_active(user->conference, user);
	if (user->playing_moh) {
		conf_moh_stop(user);
	}

	if (user->conference->waitingusers) {
		conf_change_state(user, CONF_STATE_INACTIVE);
	} else {
		conf_change_state(user, CONF_STATE_EMPTY);
	}
}

static void transition_to_single(struct confbridge_user *user)
{
	conf_mute_only_active(user->conference);
}
