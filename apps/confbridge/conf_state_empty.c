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

static void join_unmarked(struct confbridge_user *user);
static void join_waitmarked(struct confbridge_user *user);
static void join_marked(struct confbridge_user *user);
static void transition_to_empty(struct confbridge_user *user);

struct confbridge_state STATE_EMPTY = {
	.name = "EMPTY",
	.join_unmarked = join_unmarked,
	.join_waitmarked = join_waitmarked,
	.join_marked = join_marked,
	.entry = transition_to_empty,
};

struct confbridge_state *CONF_STATE_EMPTY = &STATE_EMPTY;

static void join_unmarked(struct confbridge_user *user)
{
	conf_add_user_active(user->conference, user);
	conf_handle_first_join(user->conference);
	conf_add_post_join_action(user, conf_handle_only_person);

	conf_change_state(user, CONF_STATE_SINGLE);
}

static void join_waitmarked(struct confbridge_user *user)
{
	conf_default_join_waitmarked(user);
	conf_handle_first_join(user->conference);

	conf_change_state(user, CONF_STATE_INACTIVE);
}

static void join_marked(struct confbridge_user *user)
{
	conf_add_user_marked(user->conference, user);
	conf_handle_first_join(user->conference);
	conf_add_post_join_action(user, conf_handle_only_person);

	conf_change_state(user, CONF_STATE_SINGLE_MARKED);
}

static void transition_to_empty(struct confbridge_user *user)
{
	/* Set device state to "not in use" */
	ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "confbridge:%s", user->conference->name);
	conf_ended(user->conference);
}
