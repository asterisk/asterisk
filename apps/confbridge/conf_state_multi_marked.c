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
 * \brief Confbridge state handling for the MULTI_MARKED state
 *
 * \author\verbatim Terry Wilson <twilson@digium.com> \endverbatim
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "include/confbridge.h"
#include "asterisk/musiconhold.h"
#include "include/conf_state.h"

static void join_active(struct confbridge_user *user);
static void join_marked(struct confbridge_user *user);
static void leave_active(struct confbridge_user *user);
static void leave_marked(struct confbridge_user *user);
static void transition_to_marked(struct confbridge_user *user);

static struct confbridge_state STATE_MULTI_MARKED = {
	.name = "MULTI_MARKED",
	.join_unmarked = join_active,
	.join_waitmarked = join_active,
	.join_marked = join_marked,
	.leave_unmarked = leave_active,
	.leave_waitmarked = leave_active,
	.leave_marked = leave_marked,
	.entry = transition_to_marked,
};
struct confbridge_state *CONF_STATE_MULTI_MARKED = &STATE_MULTI_MARKED;

static void join_active(struct confbridge_user *user)
{
	conf_add_user_active(user->conference, user);
	conf_update_user_mute(user);
}

static void join_marked(struct confbridge_user *user)
{
	conf_add_user_marked(user->conference, user);
	conf_update_user_mute(user);
}

static void leave_active(struct confbridge_user *user)
{
	conf_remove_user_active(user->conference, user);
	if (user->conference->activeusers == 1) {
		conf_change_state(user, CONF_STATE_SINGLE_MARKED);
	}
}

static void leave_marked(struct confbridge_user *user)
{
	struct confbridge_user *user_iter;
	int need_prompt = 0;

	conf_remove_user_marked(user->conference, user);

	if (user->conference->markedusers == 0) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&user->conference->active_list, user_iter, list) {
			/* Kick ENDMARKED cbu_iters */
			if (ast_test_flag(&user_iter->u_profile, USER_OPT_ENDMARKED) && !user_iter->kicked) {
				if (ast_test_flag(&user_iter->u_profile, USER_OPT_WAITMARKED)
					&& !ast_test_flag(&user_iter->u_profile, USER_OPT_MARKEDUSER)) {
					AST_LIST_REMOVE_CURRENT(list);
					user_iter->conference->activeusers--;
					AST_LIST_INSERT_TAIL(&user_iter->conference->waiting_list, user_iter, list);
					user_iter->conference->waitingusers++;
				}
				user_iter->kicked = 1;
				ast_bridge_remove(user_iter->conference->bridge, user_iter->chan);
			} else if (ast_test_flag(&user_iter->u_profile, USER_OPT_WAITMARKED)
				&& !ast_test_flag(&user_iter->u_profile, USER_OPT_MARKEDUSER)) {
				need_prompt = 1;

				AST_LIST_REMOVE_CURRENT(list);
				user_iter->conference->activeusers--;
				AST_LIST_INSERT_TAIL(&user_iter->conference->waiting_list, user_iter, list);
				user_iter->conference->waitingusers++;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	switch (user->conference->activeusers) {
	case 0:
		/* Implies markedusers == 0 */
		switch (user->conference->waitingusers) {
		case 0:
			conf_change_state(user, CONF_STATE_EMPTY);
			break;
		default:
			conf_change_state(user, CONF_STATE_INACTIVE);
			break;
		}
		break;
	case 1:
		switch (user->conference->markedusers) {
		case 0:
			conf_change_state(user, CONF_STATE_SINGLE);
			break;
		case 1:
			/* XXX I seem to remember doing this for a reason, but right now it escapes me
			 * how we could possibly ever have a waiting user while we have a marked user */
			switch (user->conference->waitingusers) {
			case 0:
				conf_change_state(user, CONF_STATE_SINGLE_MARKED);
				break;
			case 1:
				break; /* Stay in marked */
			}
			break;
		}
		break;
	default:
		switch (user->conference->markedusers) {
		case 0:
			conf_change_state(user, CONF_STATE_MULTI);
			break;
		default:
			break; /* Stay in marked */
		}
	}

	if (need_prompt) {
		/* Play back the audio prompt saying the leader has left the conference */
		if (!ast_test_flag(&user->u_profile, USER_OPT_QUIET)) {
			ao2_unlock(user->conference);
			ast_autoservice_start(user->chan);
			play_sound_file(user->conference,
				conf_get_sound(CONF_SOUND_LEADER_HAS_LEFT, user->b_profile.sounds));
			ast_autoservice_stop(user->chan);
			ao2_lock(user->conference);
		}

		AST_LIST_TRAVERSE(&user->conference->waiting_list, user_iter, list) {
			if (user_iter->kicked) {
				continue;
			}

			if (ast_test_flag(&user_iter->u_profile, USER_OPT_MUSICONHOLD)) {
				conf_moh_start(user_iter);
			}

			conf_update_user_mute(user_iter);
		}
	}
}

static int post_join_play_begin(struct confbridge_user *cbu)
{
	int res;

	ast_autoservice_start(cbu->chan);
	res = play_sound_file(cbu->conference,
		conf_get_sound(CONF_SOUND_BEGIN, cbu->b_profile.sounds));
	ast_autoservice_stop(cbu->chan);
	return res;
}

static void transition_to_marked(struct confbridge_user *user)
{
	struct confbridge_user *user_iter;
	int waitmarked_moved = 0;

	/* Move all waiting users to active, stopping MOH and unmuting if necessary */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&user->conference->waiting_list, user_iter, list) {
		AST_LIST_REMOVE_CURRENT(list);
		user->conference->waitingusers--;
		AST_LIST_INSERT_TAIL(&user->conference->active_list, user_iter, list);
		user->conference->activeusers++;
		if (user_iter->playing_moh) {
			conf_moh_stop(user_iter);
		}
		conf_update_user_mute(user_iter);
		waitmarked_moved++;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* Play the audio file stating that the conference is beginning */
	if (user->conference->markedusers == 1
		&& ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER)
		&& !ast_test_flag(&user->u_profile, USER_OPT_QUIET)
		&& waitmarked_moved) {
		conf_add_post_join_action(user, post_join_play_begin);
	}
}
