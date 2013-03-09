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
 * See https://wiki.asterisk.org/wiki/display/AST/Confbridge+state+changes for
 * a more complete description of how conference states work.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#ifndef _CONF_STATE_H_
#define _CONF_STATE_H_

struct confbridge_state;
struct confbridge_conference;
struct confbridge_user;

typedef void (*conference_event_fn)(struct confbridge_user *user);
typedef void (*conference_entry_fn)(struct confbridge_user *user);
typedef void (*conference_exit_fn)(struct confbridge_user *user);

/*! \brief A conference state object to hold the various state callback functions */
struct confbridge_state {
	const char *name;
	conference_event_fn join_unmarked;    /*!< Handle an unmarked join event */
	conference_event_fn join_waitmarked;  /*!< Handle a waitmarked join event */
	conference_event_fn join_marked;      /*!< Handle a marked join event */
	conference_event_fn leave_unmarked;   /*!< Handle an unmarked leave event */
	conference_event_fn leave_waitmarked; /*!< Handle a waitmarked leave event */
	conference_event_fn leave_marked;     /*!< Handle a marked leave event */
	conference_entry_fn entry;            /*!< Function to handle entry to a state */
	conference_exit_fn exit;              /*!< Function to handle exiting from a state */
};

/*! \brief Conference state with no active or waiting users */
extern struct confbridge_state *CONF_STATE_EMPTY;

/*! \brief Conference state with only waiting users */
extern struct confbridge_state *CONF_STATE_INACTIVE;

/*! \brief Conference state with only a single unmarked active user */
extern struct confbridge_state *CONF_STATE_SINGLE;

/*! \brief Conference state with only a single marked active user */
extern struct confbridge_state *CONF_STATE_SINGLE_MARKED;

/*! \brief Conference state with multiple active users, but no marked users */
extern struct confbridge_state *CONF_STATE_MULTI;

/*! \brief Conference state with multiple active users and at least one marked user */
extern struct confbridge_state *CONF_STATE_MULTI_MARKED;

/*! \brief Execute conference state transition because of a user action
 * \param user The user that joined/left
 * \param newstate The state to transition to
 */
void conf_change_state(struct confbridge_user *user, struct confbridge_state *newstate);

/* Common event handlers shared between different states */

/*! \brief Logic to execute every time a waitmarked user joins an unmarked conference */
void conf_default_join_waitmarked(struct confbridge_user *user);

/*! \brief Logic to execute every time a waitmarked user leaves an unmarked conference */
void conf_default_leave_waitmarked(struct confbridge_user *user);

/*! \brief A handler for join/leave events that are invalid in a particular state */
void conf_invalid_event_fn(struct confbridge_user *user);

#endif
