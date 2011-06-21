/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

/*! \file
 *
 * \brief Conference Bridge application
 *
 * \author\verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 *
 * This is a conference bridge application utilizing the bridging core.
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/bridging.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/audiohook.h"
#include "asterisk/astobj2.h"

/*** DOCUMENTATION
        <application name="ConfBridge" language="en_US">
                <synopsis>
                        Conference bridge application.
                </synopsis>
                <syntax>
                        <parameter name="confno">
                                <para>The conference number</para>
                        </parameter>
                        <parameter name="options">
                                <optionlist>
                                        <option name="a">
                                                <para>Set admin mode.</para>
                                        </option>
                                        <option name="A">
                                                <para>Set marked mode.</para>
                                        </option>
                                        <option name="c">
                                                <para>Announce user(s) count on joining a conference.</para>
                                        </option>
                                        <option name="m">
                                                <para>Set initially muted.</para>
                                        </option>
                                        <option name="M" hasparams="optional">
                                                <para>Enable music on hold when the conference has a single caller. Optionally,
                                                specify a musiconhold class to use. If one is not provided, it will use the
                                                channel's currently set music class, or <literal>default</literal>.</para>
                                                <argument name="class" required="true" />
                                        </option>
                                        <option name="1">
                                                <para>Do not play message when first person enters</para>
                                        </option>
                                        <option name="s">
                                                <para>Present menu (user or admin) when <literal>*</literal> is received
                                                (send to menu).</para>
                                        </option>
                                        <option name="w">
                                                <para>Wait until the marked user enters the conference.</para>
                                        </option>
                                        <option name="q">
                                                <para>Quiet mode (don't play enter/leave sounds).</para>
                                        </option>
				</optionlist>
		      </parameter>
                </syntax>
                <description>
                        <para>Enters the user into a specified conference bridge. The user can exit the conference by hangup only.</para>
                        <para>The join sound can be set using the <literal>CONFBRIDGE_JOIN_SOUND</literal> variable and the leave sound can be set using the <literal>CONFBRIDGE_LEAVE_SOUND</literal> variable. These can be unique to the caller.</para>
			<note><para>This application will not automatically answer the channel.</para></note>
                </description>
        </application>
***/

/*!
 * \par Playing back a file to a channel in a conference
 * You might notice in this application that while playing a sound file
 * to a channel the actual conference bridge lock is not held. This is done so
 * that other channels are not blocked from interacting with the conference bridge.
 * Unfortunately because of this it is possible for things to change after the sound file
 * is done being played. Data must therefore be checked after reacquiring the conference
 * bridge lock if it is important.
 */

static const char *app = "ConfBridge";

enum {
	OPTION_ADMIN = (1 << 0),             /*!< Set if the caller is an administrator */
	OPTION_MENU = (1 << 1),              /*!< Set if the caller should have access to the conference bridge IVR menu */
	OPTION_MUSICONHOLD = (1 << 2),       /*!< Set if music on hold should be played if nobody else is in the conference bridge */
	OPTION_NOONLYPERSON = (1 << 3),      /*!< Set if the "you are currently the only person in this conference" sound file should not be played */
	OPTION_STARTMUTED = (1 << 4),        /*!< Set if the caller should be initially set muted */
	OPTION_ANNOUNCEUSERCOUNT = (1 << 5), /*!< Set if the number of users should be announced to the caller */
	OPTION_MARKEDUSER = (1 << 6),        /*!< Set if the caller is a marked user */
	OPTION_WAITMARKED = (1 << 7),        /*!< Set if the conference must wait for a marked user before starting */
	OPTION_QUIET = (1 << 8),             /*!< Set if no audio prompts should be played */
};

enum {
	OPTION_MUSICONHOLD_CLASS,            /*!< If the 'M' option is set, the music on hold class to play */
	/*This must be the last element */
	OPTION_ARRAY_SIZE,
};

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('A', OPTION_MARKEDUSER),
	AST_APP_OPTION('a', OPTION_ADMIN),
	AST_APP_OPTION('c', OPTION_ANNOUNCEUSERCOUNT),
	AST_APP_OPTION('m', OPTION_STARTMUTED),
	AST_APP_OPTION_ARG('M', OPTION_MUSICONHOLD, OPTION_MUSICONHOLD_CLASS),
	AST_APP_OPTION('1', OPTION_NOONLYPERSON),
	AST_APP_OPTION('s', OPTION_MENU),
	AST_APP_OPTION('w', OPTION_WAITMARKED),
	AST_APP_OPTION('q', OPTION_QUIET),
});

/* Maximum length of a conference bridge name */
#define MAX_CONF_NAME 32

/* Number of buckets our conference bridges container can have */
#define CONFERENCE_BRIDGE_BUCKETS 53

/*! \brief The structure that represents a conference bridge */
struct conference_bridge {
	char name[MAX_CONF_NAME];                                         /*!< Name of the conference bridge */
	struct ast_bridge *bridge;                                        /*!< Bridge structure doing the mixing */
	unsigned int users;                                               /*!< Number of users present */
	unsigned int markedusers;                                         /*!< Number of marked users present */
	unsigned int locked:1;                                            /*!< Is this conference bridge locked? */
	AST_LIST_HEAD_NOLOCK(, conference_bridge_user) users_list;        /*!< List of users participating in the conference bridge */
	struct ast_channel *playback_chan;                                /*!< Channel used for playback into the conference bridge */
	ast_mutex_t playback_lock;                                        /*!< Lock used for playback channel */
};

/*! \brief The structure that represents a conference bridge user */
struct conference_bridge_user {
	struct conference_bridge *conference_bridge; /*!< Conference bridge they are participating in */
	struct ast_channel *chan;                    /*!< Asterisk channel participating */
	struct ast_flags flags;                      /*!< Flags passed in when the application was called */
	char *opt_args[OPTION_ARRAY_SIZE];           /*!< Arguments to options passed when application was called */
	struct ast_bridge_features features;         /*!< Bridge features structure */
	unsigned int kicked:1;                       /*!< User has been kicked from the conference */
	AST_LIST_ENTRY(conference_bridge_user) list; /*!< Linked list information */
};

/*! \brief Container to hold all conference bridges in progress */
static struct ao2_container *conference_bridges;

static int play_sound_file(struct conference_bridge *conference_bridge, const char *filename);

/*! \brief Hashing function used for conference bridges container */
static int conference_bridge_hash_cb(const void *obj, const int flags)
{
	const struct conference_bridge *conference_bridge = obj;
	return ast_str_case_hash(conference_bridge->name);
}

/*! \brief Comparison function used for conference bridges container */
static int conference_bridge_cmp_cb(void *obj, void *arg, int flags)
{
	const struct conference_bridge *conference_bridge0 = obj, *conference_bridge1 = arg;
	return (!strcasecmp(conference_bridge0->name, conference_bridge1->name) ? CMP_MATCH | CMP_STOP : 0);
}

/*!
 * \brief Announce number of users in the conference bridge to the caller
 *
 * \param conference_bridge Conference bridge to peek at
 * \param conference_bridge_user Caller
 *
 * \return Returns 0 on success, -1 if the user hung up
 */
static int announce_user_count(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	if (conference_bridge->users == 1) {
		/* Awww we are the only person in the conference bridge */
		return 0;
	} else if (conference_bridge->users == 2) {
		/* Eep, there is one other person */
		if (ast_stream_and_wait(conference_bridge_user->chan, "conf-onlyone", "")) {
			return -1;
		}
	} else {
		/* Alas multiple others in here */
		if (ast_stream_and_wait(conference_bridge_user->chan, "conf-thereare", "")) {
			return -1;
		}
		if (ast_say_number(conference_bridge_user->chan, conference_bridge->users - 1, "", conference_bridge_user->chan->language, NULL)) {
			return -1;
		}
		if (ast_stream_and_wait(conference_bridge_user->chan, "conf-otherinparty", "")) {
			return -1;
		}
	}
	return 0;
}

/*!
 * \brief Play back an audio file to a channel
 *
 * \param conference_bridge Conference bridge they are in
 * \param chan Channel to play audio prompt to
 * \param file Prompt to play
 *
 * \return Returns 0 on success, -1 if the user hung up
 *
 * \note This function assumes that conference_bridge is locked
 */
static int play_prompt_to_channel(struct conference_bridge *conference_bridge, struct ast_channel *chan, const char *file)
{
	int res;
	ao2_unlock(conference_bridge);
	res = ast_stream_and_wait(chan, file, "");
	ao2_lock(conference_bridge);
	return res;
}

/*!
 * \brief Perform post-joining marked specific actions
 *
 * \param conference_bridge Conference bridge being joined
 * \param conference_bridge_user Conference bridge user joining
 *
 * \return Returns 0 on success, -1 if the user hung up
 */
static int post_join_marked(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	if (ast_test_flag(&conference_bridge_user->flags, OPTION_MARKEDUSER)) {
		struct conference_bridge_user *other_conference_bridge_user = NULL;

		/* If we are not the first marked user to join just bail out now */
		if (conference_bridge->markedusers >= 2) {
			return 0;
		}

		/* Iterate through every participant stopping MOH on them if need be */
		AST_LIST_TRAVERSE(&conference_bridge->users_list, other_conference_bridge_user, list) {
			if (other_conference_bridge_user == conference_bridge_user) {
				continue;
			}
			if (ast_test_flag(&other_conference_bridge_user->flags, OPTION_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, other_conference_bridge_user->chan)) {
				ast_moh_stop(other_conference_bridge_user->chan);
				ast_bridge_unsuspend(conference_bridge->bridge, other_conference_bridge_user->chan);
			}
		}

		/* Next play the audio file stating they are going to be placed into the conference */
		if (!ast_test_flag(&conference_bridge_user->flags, OPTION_QUIET)) {
			ao2_unlock(conference_bridge);
			ast_autoservice_start(conference_bridge_user->chan);
			play_sound_file(conference_bridge, "conf-placeintoconf");
			ast_autoservice_stop(conference_bridge_user->chan);
			ao2_lock(conference_bridge);
		}

		/* Finally iterate through and unmute them all */
		AST_LIST_TRAVERSE(&conference_bridge->users_list, other_conference_bridge_user, list) {
			if (other_conference_bridge_user == conference_bridge_user) {
				continue;
			}
			other_conference_bridge_user->features.mute = 0;
		}

	} else {
		/* If a marked user already exists in the conference bridge we can just bail out now */
		if (conference_bridge->markedusers) {
			return 0;
		}
		/* Be sure we are muted so we can't talk to anybody else waiting */
		conference_bridge_user->features.mute = 1;
		/* If we have not been quieted play back that they are waiting for the leader */
		if (!ast_test_flag(&conference_bridge_user->flags, OPTION_QUIET)) {
			if (play_prompt_to_channel(conference_bridge, conference_bridge_user->chan, "conf-waitforleader")) {
				/* user hung up while the sound was playing */
				return -1;
			}
		}
		/* Start music on hold if needed */
		/* We need to recheck the markedusers value here. play_prompt_to_channel unlocks the conference bridge, potentially
		 * allowing a marked user to enter while the prompt was playing
		 */
		if (!conference_bridge->markedusers && ast_test_flag(&conference_bridge_user->flags, OPTION_MUSICONHOLD)) {
			ast_moh_start(conference_bridge_user->chan, conference_bridge_user->opt_args[OPTION_MUSICONHOLD_CLASS], NULL);
		}
	}
	return 0;
}

/*!
 * \brief Perform post-joining non-marked specific actions
 *
 * \param conference_bridge Conference bridge being joined
 * \param conference_bridge_user Conference bridge user joining
 *
 * \return Returns 0 on success, -1 if the user hung up
 */
static int post_join_unmarked(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	/* Play back audio prompt and start MOH if need be if we are the first participant */
	if (conference_bridge->users == 1) {
		/* If audio prompts have not been quieted or this prompt quieted play it on out */
		if (!ast_test_flag(&conference_bridge_user->flags, OPTION_QUIET | OPTION_NOONLYPERSON)) {
			if (play_prompt_to_channel(conference_bridge, conference_bridge_user->chan, "conf-onlyperson")) {
				/* user hung up while the sound was playing */
				return -1;
			}
		}
		/* If we need to start music on hold on the channel do so now */
		/* We need to re-check the number of users in the conference bridge here because another conference bridge
		 * participant could have joined while the above prompt was playing for the first user.
		 */
		if (conference_bridge->users == 1 && ast_test_flag(&conference_bridge_user->flags, OPTION_MUSICONHOLD)) {
			ast_moh_start(conference_bridge_user->chan, conference_bridge_user->opt_args[OPTION_MUSICONHOLD_CLASS], NULL);
		}
		return 0;
	}

	/* Announce number of users if need be */
	if (ast_test_flag(&conference_bridge_user->flags, OPTION_ANNOUNCEUSERCOUNT)) {
		ao2_unlock(conference_bridge);
		if (announce_user_count(conference_bridge, conference_bridge_user)) {
			ao2_lock(conference_bridge);
			return -1;
		}
		ao2_lock(conference_bridge);
	}

	/* If we are the second participant we may need to stop music on hold on the first */
	if (conference_bridge->users == 2) {
		struct conference_bridge_user *first_participant = AST_LIST_FIRST(&conference_bridge->users_list);

		/* Temporarily suspend the above participant from the bridge so we have control to stop MOH if needed */
		if (ast_test_flag(&first_participant->flags, OPTION_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, first_participant->chan)) {
			ast_moh_stop(first_participant->chan);
			ast_bridge_unsuspend(conference_bridge->bridge, first_participant->chan);
		}
	}
	return 0;
}

/*!
 * \brief Destroy a conference bridge
 *
 * \param obj The conference bridge object
 *
 * \return Returns nothing
 */
static void destroy_conference_bridge(void *obj)
{
	struct conference_bridge *conference_bridge = obj;

	ast_debug(1, "Destroying conference bridge '%s'\n", conference_bridge->name);

	ast_mutex_destroy(&conference_bridge->playback_lock);

	if (conference_bridge->playback_chan) {
		struct ast_channel *underlying_channel = conference_bridge->playback_chan->tech->bridged_channel(conference_bridge->playback_chan, NULL);
		ast_hangup(underlying_channel);
		ast_hangup(conference_bridge->playback_chan);
		conference_bridge->playback_chan = NULL;
	}

	/* Destroying a conference bridge is simple, all we have to do is destroy the bridging object */
	if (conference_bridge->bridge) {
		ast_bridge_destroy(conference_bridge->bridge);
		conference_bridge->bridge = NULL;
	}
}

static void leave_conference_bridge(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user);

/*!
 * \brief Join a conference bridge
 *
 * \param name The conference name
 * \param conference_bridge_user Conference bridge user structure
 *
 * \return A pointer to the conference bridge struct, or NULL if the conference room wasn't found.
 */
static struct conference_bridge *join_conference_bridge(const char *name, struct conference_bridge_user *conference_bridge_user)
{
	struct conference_bridge *conference_bridge = NULL;
	struct conference_bridge tmp;

	ast_copy_string(tmp.name, name, sizeof(tmp.name));

	/* We explictly lock the conference bridges container ourselves so that other callers can not create duplicate conferences at the same */
	ao2_lock(conference_bridges);

	ast_debug(1, "Trying to find conference bridge '%s'\n", name);

	/* Attempt to find an existing conference bridge */
	conference_bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);

	/* When finding a conference bridge that already exists make sure that it is not locked, and if so that we are not an admin */
	if (conference_bridge && conference_bridge->locked && !ast_test_flag(&conference_bridge_user->flags, OPTION_ADMIN)) {
		ao2_unlock(conference_bridges);
		ao2_ref(conference_bridge, -1);
		ast_debug(1, "Conference bridge '%s' is locked and caller is not an admin\n", name);
		ast_stream_and_wait(conference_bridge_user->chan, "conf-locked", "");
		return NULL;
	}

	/* If no conference bridge was found see if we can create one */
	if (!conference_bridge) {
		/* Try to allocate memory for a new conference bridge, if we fail... this won't end well. */
		if (!(conference_bridge = ao2_alloc(sizeof(*conference_bridge), destroy_conference_bridge))) {
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR, "Conference bridge '%s' does not exist.\n", name);
			return NULL;
		}

		/* Setup conference bridge parameters */
		ast_copy_string(conference_bridge->name, name, sizeof(conference_bridge->name));

		/* Create an actual bridge that will do the audio mixing */
		if (!(conference_bridge->bridge = ast_bridge_new(AST_BRIDGE_CAPABILITY_1TO1MIX, AST_BRIDGE_FLAG_SMART))) {
			ao2_ref(conference_bridge, -1);
			conference_bridge = NULL;
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR, "Conference bridge '%s' could not be created.\n", name);
			return NULL;
		}

		/* Setup lock for playback channel */
		ast_mutex_init(&conference_bridge->playback_lock);

		/* Link it into the conference bridges container */
		ao2_link(conference_bridges, conference_bridge);

		ast_debug(1, "Created conference bridge '%s' and linked to container '%p'\n", name, conference_bridges);
	}

	ao2_unlock(conference_bridges);

	/* Setup conference bridge user parameters */
	conference_bridge_user->conference_bridge = conference_bridge;

	ao2_lock(conference_bridge);

	/* All good to go, add them in */
	AST_LIST_INSERT_TAIL(&conference_bridge->users_list, conference_bridge_user, list);

	/* Increment the users count on the bridge, but record it as it is going to need to be known right after this */
	conference_bridge->users++;

	/* If the caller is a marked user bump up the count */
	if (ast_test_flag(&conference_bridge_user->flags, OPTION_MARKEDUSER)) {
		conference_bridge->markedusers++;
	}

	/* If the caller is a marked user or is waiting for a marked user to enter pass 'em off, otherwise pass them off to do regular joining stuff */
	if (ast_test_flag(&conference_bridge_user->flags, OPTION_MARKEDUSER | OPTION_WAITMARKED)) {
		if (post_join_marked(conference_bridge, conference_bridge_user)) {
			ao2_unlock(conference_bridge);
			leave_conference_bridge(conference_bridge, conference_bridge_user);
			return NULL;
		}
	} else {
		if (post_join_unmarked(conference_bridge, conference_bridge_user)) {
			ao2_unlock(conference_bridge);
			leave_conference_bridge(conference_bridge, conference_bridge_user);
			return NULL;
		}
	}

	ao2_unlock(conference_bridge);

	return conference_bridge;
}

/*!
 * \brief Leave a conference bridge
 *
 * \param conference_bridge The conference bridge to leave
 * \param conference_bridge_user The conference bridge user structure
 *
 */
static void  leave_conference_bridge(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	ao2_lock(conference_bridge);

	/* If this caller is a marked user bump down the count */
	if (ast_test_flag(&conference_bridge_user->flags, OPTION_MARKEDUSER)) {
		conference_bridge->markedusers--;
	}

	/* Decrement the users count while keeping the previous participant count */
	conference_bridge->users--;

	/* Drop conference bridge user from the list, they be going bye bye */
	AST_LIST_REMOVE(&conference_bridge->users_list, conference_bridge_user, list);

	/* If there are still users in the conference bridge we may need to do things (such as start MOH on them) */
	if (conference_bridge->users) {
		if (ast_test_flag(&conference_bridge_user->flags, OPTION_MARKEDUSER) && !conference_bridge->markedusers) {
			struct conference_bridge_user *other_participant = NULL;

			/* Start out with muting everyone */
			AST_LIST_TRAVERSE(&conference_bridge->users_list, other_participant, list) {
				other_participant->features.mute = 1;
			}

			/* Play back the audio prompt saying the leader has left the conference */
			if (!ast_test_flag(&conference_bridge_user->flags, OPTION_QUIET)) {
				ao2_unlock(conference_bridge);
				ast_autoservice_start(conference_bridge_user->chan);
				play_sound_file(conference_bridge, "conf-leaderhasleft");
				ast_autoservice_stop(conference_bridge_user->chan);
				ao2_lock(conference_bridge);
			}

			/* Now on to starting MOH if needed */
			AST_LIST_TRAVERSE(&conference_bridge->users_list, other_participant, list) {
				if (ast_test_flag(&other_participant->flags, OPTION_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, other_participant->chan)) {
					ast_moh_start(other_participant->chan, other_participant->opt_args[OPTION_MUSICONHOLD_CLASS], NULL);
					ast_bridge_unsuspend(conference_bridge->bridge, other_participant->chan);
				}
			}
		} else if (conference_bridge->users == 1) {
			/* Of course if there is one other person in here we may need to start up MOH on them */
			struct conference_bridge_user *first_participant = AST_LIST_FIRST(&conference_bridge->users_list);

			if (ast_test_flag(&first_participant->flags, OPTION_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, first_participant->chan)) {
				ast_moh_start(first_participant->chan, first_participant->opt_args[OPTION_MUSICONHOLD_CLASS], NULL);
				ast_bridge_unsuspend(conference_bridge->bridge, first_participant->chan);
			}
		}
	} else {
		ao2_unlink(conference_bridges, conference_bridge);
	}

	/* Done mucking with the conference bridge, huzzah */
	ao2_unlock(conference_bridge);

	ao2_ref(conference_bridge, -1);
}

/*!
 * \brief Play sound file into conference bridge
 *
 * \param conference_bridge The conference bridge to play sound file into
 * \param filename Sound file to play
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int play_sound_file(struct conference_bridge *conference_bridge, const char *filename)
{
	struct ast_channel *underlying_channel;

	ast_mutex_lock(&conference_bridge->playback_lock);

	if (!(conference_bridge->playback_chan)) {
		int cause;

		if (!(conference_bridge->playback_chan = ast_request("Bridge", AST_FORMAT_SLINEAR, "", &cause))) {
			ast_mutex_unlock(&conference_bridge->playback_lock);
			return -1;
		}

		conference_bridge->playback_chan->bridge = conference_bridge->bridge;

		if (ast_call(conference_bridge->playback_chan, "", 0)) {
			ast_hangup(conference_bridge->playback_chan);
			conference_bridge->playback_chan = NULL;
			ast_mutex_unlock(&conference_bridge->playback_lock);
			return -1;
		}

		ast_debug(1, "Created a playback channel to conference bridge '%s'\n", conference_bridge->name);

		underlying_channel = conference_bridge->playback_chan->tech->bridged_channel(conference_bridge->playback_chan, NULL);
	} else {
		/* Channel was already available so we just need to add it back into the bridge */
		underlying_channel = conference_bridge->playback_chan->tech->bridged_channel(conference_bridge->playback_chan, NULL);
		ast_bridge_impart(conference_bridge->bridge, underlying_channel, NULL, NULL);
	}

	/* The channel is all under our control, in goes the prompt */
	ast_stream_and_wait(conference_bridge->playback_chan, filename, "");

	ast_debug(1, "Departing underlying channel '%s' from bridge '%p'\n", underlying_channel->name, conference_bridge->bridge);
	ast_bridge_depart(conference_bridge->bridge, underlying_channel);

	ast_mutex_unlock(&conference_bridge->playback_lock);

	return 0;
}

/*!
 * \brief DTMF Menu Callback
 *
 * \param bridge Bridge this is involving
 * \param bridge_channel Bridged channel this is involving
 * \param hook_pvt User's conference bridge structure
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int menu_callback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct conference_bridge_user *conference_bridge_user = hook_pvt;
	struct conference_bridge *conference_bridge = conference_bridge_user->conference_bridge;
	int digit, res = 0, isadmin = ast_test_flag(&conference_bridge_user->flags, OPTION_ADMIN);

	/* See if music on hold is playing */
	ao2_lock(conference_bridge);
	if (conference_bridge->users == 1 && ast_test_flag(&conference_bridge_user->flags, OPTION_MUSICONHOLD)) {
		/* Just us so MOH is probably indeed going, let's stop it */
		ast_moh_stop(bridge_channel->chan);
	}
	ao2_unlock(conference_bridge);

	/* Try to play back the user menu, if it fails pass this back up so the bridging core will act on it */
	if (ast_streamfile(bridge_channel->chan, (isadmin ? "conf-adminmenu" : "conf-usermenu"), bridge_channel->chan->language)) {
		res = -1;
		goto finished;
	}

	/* Wait for them to enter a digit from the user menu options */
	digit = ast_waitstream(bridge_channel->chan, AST_DIGIT_ANY);
	ast_stopstream(bridge_channel->chan);

	if (digit == '1') {
		/* 1 - Mute or unmute yourself, note we only allow manipulation if they aren't waiting for a marked user or if marked users exist */
		if (!ast_test_flag(&conference_bridge_user->flags, OPTION_WAITMARKED) || conference_bridge->markedusers) {
			conference_bridge_user->features.mute = (!conference_bridge_user->features.mute ? 1 : 0);
		}
		res = ast_stream_and_wait(bridge_channel->chan, (conference_bridge_user->features.mute ? "conf-muted" : "conf-unmuted"), "");
	} else if (isadmin && digit == '2') {
		/* 2 - Unlock or lock conference */
		conference_bridge->locked = (!conference_bridge->locked ? 1 : 0);
		res = ast_stream_and_wait(bridge_channel->chan, (conference_bridge->locked ? "conf-lockednow" : "conf-unlockednow"), "");
	} else if (isadmin && digit == '3') {
		/* 3 - Eject last user */
		struct conference_bridge_user *last_participant = NULL;

		ao2_lock(conference_bridge);
		if (((last_participant = AST_LIST_LAST(&conference_bridge->users_list)) == conference_bridge_user) || (ast_test_flag(&last_participant->flags, OPTION_ADMIN))) {
			ao2_unlock(conference_bridge);
			res = ast_stream_and_wait(bridge_channel->chan, "conf-errormenu", "");
		} else {
			last_participant->kicked = 1;
			ast_bridge_remove(conference_bridge->bridge, last_participant->chan);
			ao2_unlock(conference_bridge);
		}
	} else if (digit == '4') {
		/* 4 - Decrease listening volume */
		ast_audiohook_volume_adjust(conference_bridge_user->chan, AST_AUDIOHOOK_DIRECTION_WRITE, -1);
	} else if (digit == '6') {
		/* 6 - Increase listening volume */
		ast_audiohook_volume_adjust(conference_bridge_user->chan, AST_AUDIOHOOK_DIRECTION_WRITE, 1);
	} else if (digit == '7') {
		/* 7 - Decrease talking volume */
		ast_audiohook_volume_adjust(conference_bridge_user->chan, AST_AUDIOHOOK_DIRECTION_READ, -1);
	} else if (digit == '8') {
		/* 8 - Exit the IVR */
	} else if (digit == '9') {
		/* 9 - Increase talking volume */
		ast_audiohook_volume_adjust(conference_bridge_user->chan, AST_AUDIOHOOK_DIRECTION_READ, 1);
	} else {
		/* No valid option was selected */
		res = ast_stream_and_wait(bridge_channel->chan, "conf-errormenu", "");
	}

 finished:
	/* See if music on hold needs to be started back up again */
	ao2_lock(conference_bridge);
	if (conference_bridge->users == 1 && ast_test_flag(&conference_bridge_user->flags, OPTION_MUSICONHOLD)) {
		ast_moh_start(bridge_channel->chan, conference_bridge_user->opt_args[OPTION_MUSICONHOLD_CLASS], NULL);
	}
	ao2_unlock(conference_bridge);

	bridge_channel->state = AST_BRIDGE_CHANNEL_STATE_WAIT;

	return res;
}

/*! \brief The ConfBridge application */
static int confbridge_exec(struct ast_channel *chan, void *data)
{
	int res = 0, volume_adjustments[2];
	char *parse;
	struct conference_bridge *conference_bridge = NULL;
	struct conference_bridge_user conference_bridge_user = {
		.chan = chan,
	};
	const char *tmp, *join_sound = NULL, *leave_sound = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(conf_name);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (conference name[,options])\n", app);
		return -1;
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc == 2) {
		ast_app_parse_options(app_opts, &conference_bridge_user.flags, conference_bridge_user.opt_args, args.options);
	}

	/* Look for a conference bridge matching the provided name */
	if (!(conference_bridge = join_conference_bridge(args.conf_name, &conference_bridge_user))) {
		return -1;
	}

	/* Keep a copy of volume adjustments so we can restore them later if need be */
	volume_adjustments[0] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_READ);
	volume_adjustments[1] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_WRITE);

	/* Always initialize the features structure, we are in most cases always going to need it. */
	ast_bridge_features_init(&conference_bridge_user.features);

	/* If the menu option is enabled provide a user or admin menu as a custom feature hook */
	if (ast_test_flag(&conference_bridge_user.flags, OPTION_MENU)) {
		ast_bridge_features_hook(&conference_bridge_user.features, "#", menu_callback, &conference_bridge_user);
	}

	/* If the caller should be joined already muted, make it so */
	if (ast_test_flag(&conference_bridge_user.flags, OPTION_STARTMUTED)) {
		conference_bridge_user.features.mute = 1;
	}

	/* Grab join/leave sounds from the channel */
	ast_channel_lock(chan);
	if ((tmp = pbx_builtin_getvar_helper(chan, "CONFBRIDGE_JOIN_SOUND"))) {
		join_sound = ast_strdupa(tmp);
	}
	if ((tmp = pbx_builtin_getvar_helper(chan, "CONFBRIDGE_LEAVE_SOUND"))) {
		leave_sound = ast_strdupa(tmp);
	}
	ast_channel_unlock(chan);

	/* If there is 1 or more people already in the conference then play our join sound unless overridden */
	if (!ast_test_flag(&conference_bridge_user.flags, OPTION_QUIET) && !ast_strlen_zero(join_sound) && conference_bridge->users >= 2) {
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, join_sound);
		ast_autoservice_stop(chan);
	}

	/* Join our conference bridge for real */
	ast_bridge_join(conference_bridge->bridge, chan, NULL, &conference_bridge_user.features);

	/* If there is 1 or more people (not including us) already in the conference then play our leave sound unless overridden */
	if (!ast_test_flag(&conference_bridge_user.flags, OPTION_QUIET) && !ast_strlen_zero(leave_sound) && conference_bridge->users >= 2) {
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, leave_sound);
		ast_autoservice_stop(chan);
	}

	/* Easy as pie, depart this channel from the conference bridge */
	leave_conference_bridge(conference_bridge, &conference_bridge_user);
	conference_bridge = NULL;

	/* Can't forget to clean up the features structure, or else we risk a memory leak */
	ast_bridge_features_cleanup(&conference_bridge_user.features);

	/* If the user was kicked from the conference play back the audio prompt for it */
	if (!ast_test_flag(&conference_bridge_user.flags, OPTION_QUIET) && conference_bridge_user.kicked) {
		res = ast_stream_and_wait(chan, "conf-kicked", "");
	}

	/* Restore volume adjustments to previous values in case they were changed */
	if (volume_adjustments[0]) {
		ast_audiohook_volume_set(chan, AST_AUDIOHOOK_DIRECTION_READ, volume_adjustments[0]);
	}
	if (volume_adjustments[1]) {
		ast_audiohook_volume_set(chan, AST_AUDIOHOOK_DIRECTION_WRITE, volume_adjustments[1]);
	}

	return res;
}

/*! \brief Called when module is being unloaded */
static int unload_module(void)
{
	int res = ast_unregister_application(app);

	/* Get rid of the conference bridges container. Since we only allow dynamic ones none will be active. */
	ao2_ref(conference_bridges, -1);

	return res;
}

/*! \brief Called when module is being loaded */
static int load_module(void)
{
	/* Create a container to hold the conference bridges */
	if (!(conference_bridges = ao2_container_alloc(CONFERENCE_BRIDGE_BUCKETS, conference_bridge_hash_cb, conference_bridge_cmp_cb))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_register_application_xml(app, confbridge_exec)) {
		ao2_ref(conference_bridges, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Conference Bridge Application");
