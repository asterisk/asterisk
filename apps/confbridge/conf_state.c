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
 * \brief Confbridge state handling
 *
 * \author\verbatim Terry Wilson <twilson@digium.com> \endverbatim
 *
 * This file contains functions that are used from multiple conf_state
 * files for handling stage change behavior.
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/test.h"
#include "include/conf_state.h"
#include "include/confbridge.h"

void conf_invalid_event_fn(struct confbridge_user *user)
{
	ast_log(LOG_ERROR, "Invalid event for confbridge user '%s'\n", user->u_profile.name);
}

/*!
 * \internal
 * \brief Mute the user and play MOH if the user requires it.
 *
 * \param user Conference user to mute and optionally start MOH on.
 */
static void conf_mute_moh_inactive_waitmarked(struct confbridge_user *user)
{
	/* Start music on hold if needed */
	if (ast_test_flag(&user->u_profile, USER_OPT_MUSICONHOLD)) {
		conf_moh_start(user);
	}
	conf_update_user_mute(user);
}

void conf_default_join_waitmarked(struct confbridge_user *user)
{
	conf_add_user_waiting(user->conference, user);
	conf_mute_moh_inactive_waitmarked(user);
	conf_add_post_join_action(user, conf_handle_inactive_waitmarked);
}

void conf_default_leave_waitmarked(struct confbridge_user *user)
{
	conf_remove_user_waiting(user->conference, user);
	if (user->playing_moh) {
		conf_moh_stop(user);
	}
}

void conf_change_state(struct confbridge_user *user, struct confbridge_state *newstate)
{
	ast_debug(1, "Changing conference '%s' state from %s to %s\n", user->conference->name, user->conference->state->name, newstate->name);
	ast_test_suite_event_notify("CONF_CHANGE_STATE", "Conference: %s\r\nOldState: %s\r\nNewState: %s\r\n",
			user->conference->name,
			user->conference->state->name,
			newstate->name);
	if (user->conference->state->exit) {
		user->conference->state->exit(user);
	}
	user->conference->state = newstate;
	if (user->conference->state->entry) {
		user->conference->state->entry(user);
	}
}
