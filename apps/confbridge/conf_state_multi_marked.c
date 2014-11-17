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

static void join_active(struct conference_bridge_user *cbu);
static void join_marked(struct conference_bridge_user *cbu);
static void leave_active(struct conference_bridge_user *cbu);
static void leave_marked(struct conference_bridge_user *cbu);
static void transition_to_marked(struct conference_bridge_user *cbu);

static struct conference_state STATE_MULTI_MARKED = {
	.name = "MULTI_MARKED",
	.join_unmarked = join_active,
	.join_waitmarked = join_active,
	.join_marked = join_marked,
	.leave_unmarked = leave_active,
	.leave_waitmarked = leave_active,
	.leave_marked = leave_marked,
	.entry = transition_to_marked,
};
struct conference_state *CONF_STATE_MULTI_MARKED = &STATE_MULTI_MARKED;

static void join_active(struct conference_bridge_user *cbu)
{
	conf_add_user_active(cbu->conference_bridge, cbu);
	conf_update_user_mute(cbu);
}

static void join_marked(struct conference_bridge_user *cbu)
{
	conf_add_user_marked(cbu->conference_bridge, cbu);
	conf_update_user_mute(cbu);
}

static void leave_active(struct conference_bridge_user *cbu)
{
	conf_remove_user_active(cbu->conference_bridge, cbu);
	if (cbu->conference_bridge->activeusers == 1) {
		conf_change_state(cbu, CONF_STATE_SINGLE_MARKED);
	}
}

static void leave_marked(struct conference_bridge_user *cbu)
{
	struct conference_bridge_user *cbu_iter;
	int need_prompt = 0;

	conf_remove_user_marked(cbu->conference_bridge, cbu);

	if (cbu->conference_bridge->markedusers == 0) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&cbu->conference_bridge->active_list, cbu_iter, list) {
			/* Kick ENDMARKED cbu_iters */
			if (ast_test_flag(&cbu_iter->u_profile, USER_OPT_ENDMARKED) && !cbu_iter->kicked) {
				if (ast_test_flag(&cbu_iter->u_profile, USER_OPT_WAITMARKED)
					&& !ast_test_flag(&cbu_iter->u_profile, USER_OPT_MARKEDUSER)) {
					AST_LIST_REMOVE_CURRENT(list);
					cbu_iter->conference_bridge->activeusers--;
					AST_LIST_INSERT_TAIL(&cbu_iter->conference_bridge->waiting_list, cbu_iter, list);
					cbu_iter->conference_bridge->waitingusers++;
				}
				cbu_iter->kicked = 1;
				ast_bridge_remove(cbu_iter->conference_bridge->bridge, cbu_iter->chan);
			} else if (ast_test_flag(&cbu_iter->u_profile, USER_OPT_WAITMARKED)
				&& !ast_test_flag(&cbu_iter->u_profile, USER_OPT_MARKEDUSER)) {
				need_prompt = 1;

				AST_LIST_REMOVE_CURRENT(list);
				cbu_iter->conference_bridge->activeusers--;
				AST_LIST_INSERT_TAIL(&cbu_iter->conference_bridge->waiting_list, cbu_iter, list);
				cbu_iter->conference_bridge->waitingusers++;
			} else {
				/* User is neither wait_marked nor end_marked; however, they
				 * should still hear the prompt.
				 */
				need_prompt = 1;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	switch (cbu->conference_bridge->activeusers) {
	case 0:
		/* Implies markedusers == 0 */
		switch (cbu->conference_bridge->waitingusers) {
		case 0:
			conf_change_state(cbu, CONF_STATE_EMPTY);
			break;
		default:
			conf_change_state(cbu, CONF_STATE_INACTIVE);
			break;
		}
		break;
	case 1:
		switch (cbu->conference_bridge->markedusers) {
		case 0:
			conf_change_state(cbu, CONF_STATE_SINGLE);
			break;
		case 1:
			/* XXX I seem to remember doing this for a reason, but right now it escapes me
			 * how we could possibly ever have a waiting user while we have a marked user */
			switch (cbu->conference_bridge->waitingusers) {
			case 0:
				conf_change_state(cbu, CONF_STATE_SINGLE_MARKED);
				break;
			case 1:
				break; /* Stay in marked */
			}
			break;
		}
		break;
	default:
		switch (cbu->conference_bridge->markedusers) {
		case 0:
			conf_change_state(cbu, CONF_STATE_MULTI);
			break;
		default:
			break; /* Stay in marked */
		}
	}

	if (need_prompt) {
		/* Play back the audio prompt saying the leader has left the conference */
		if (!ast_test_flag(&cbu->u_profile, USER_OPT_QUIET)) {
			ao2_unlock(cbu->conference_bridge);
			ast_autoservice_start(cbu->chan);
			play_sound_file(cbu->conference_bridge,
				conf_get_sound(CONF_SOUND_LEADER_HAS_LEFT, cbu->b_profile.sounds));
			ast_autoservice_stop(cbu->chan);
			ao2_lock(cbu->conference_bridge);
		}

		AST_LIST_TRAVERSE(&cbu->conference_bridge->waiting_list, cbu_iter, list) {
			if (cbu_iter->kicked) {
				continue;
			}

			if (ast_test_flag(&cbu_iter->u_profile, USER_OPT_MUSICONHOLD)) {
				conf_moh_start(cbu_iter);
			}

			conf_update_user_mute(cbu_iter);
		}
	}
}

static int post_join_play_begin(struct conference_bridge_user *cbu)
{
	int res;

	ast_autoservice_start(cbu->chan);
	res = play_sound_file(cbu->conference_bridge,
		conf_get_sound(CONF_SOUND_BEGIN, cbu->b_profile.sounds));
	ast_autoservice_stop(cbu->chan);
	return res;
}

static void transition_to_marked(struct conference_bridge_user *cbu)
{
	struct conference_bridge_user *cbu_iter;
	int waitmarked_moved = 0;

	/* Move all waiting users to active, stopping MOH and unmuting if necessary */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&cbu->conference_bridge->waiting_list, cbu_iter, list) {
		AST_LIST_REMOVE_CURRENT(list);
		cbu->conference_bridge->waitingusers--;
		AST_LIST_INSERT_TAIL(&cbu->conference_bridge->active_list, cbu_iter, list);
		cbu->conference_bridge->activeusers++;
		if (cbu_iter->playing_moh) {
			conf_moh_stop(cbu_iter);
		}
		conf_update_user_mute(cbu_iter);
		waitmarked_moved++;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* Play the audio file stating that the conference is beginning */
	if (cbu->conference_bridge->markedusers == 1
		&& ast_test_flag(&cbu->u_profile, USER_OPT_MARKEDUSER)
		&& !ast_test_flag(&cbu->u_profile, USER_OPT_QUIET)
		&& waitmarked_moved) {
		conf_add_post_join_action(cbu, post_join_play_begin);
	}
}
