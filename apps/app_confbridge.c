/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * David Vossel <dvossel@digium.com>
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
 * \author\verbatim David Vossel <dvossel@digium.com> \endverbatim
 *
 * This is a conference bridge application utilizing the bridging core.
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "asterisk/cli.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/bridging.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/audiohook.h"
#include "asterisk/astobj2.h"
#include "confbridge/include/confbridge.h"
#include "asterisk/paths.h"
#include "asterisk/manager.h"

/*** DOCUMENTATION
    <application name="ConfBridge" language="en_US">
            <synopsis>
                    Conference bridge application.
            </synopsis>
            <syntax>
                    <parameter name="confno">
                            <para>The conference number</para>
                    </parameter>
                    <parameter name="bridge_profile">
                            <para>The bridge profile name from confbridge.conf.  When left blank, a dynamically built bridge profile created by the CONFBRIDGE dialplan function is searched for on the channel and used.  If no dynamic profile is present, the 'default_bridge' profile found in confbridge.conf is used. </para>
                            <para>It is important to note that while user profiles may be unique for each participant, mixing bridge profiles on a single conference is _NOT_ recommended and will produce undefined results.</para>
                    </parameter>
                    <parameter name="user_profile">
                            <para>The user profile name from confbridge.conf.  When left blank, a dynamically built user profile created by the CONFBRIDGE dialplan function is searched for on the channel and used.  If no dynamic profile is present, the 'default_user' profile found in confbridge.conf is used.</para>
                    </parameter>
                    <parameter name="menu">
                            <para>The name of the DTMF menu in confbridge.conf to be applied to this channel.  No menu is applied by default if this option is left blank.</para>
                    </parameter>
            </syntax>
            <description>
                    <para>Enters the user into a specified conference bridge. The user can exit the conference by hangup or DTMF menu option.</para>
            </description>
			<see-also>
				<ref type="application">ConfBridge</ref>
				<ref type="function">CONFBRIDGE</ref>
				<ref type="function">CONFBRIDGE_INFO</ref>
			</see-also>
    </application>
	<function name="CONFBRIDGE" language="en_US">
		<synopsis>
			Set a custom dynamic bridge and user profile on a channel for the ConfBridge application using the same options defined in confbridge.conf.
		</synopsis>
		<syntax>
			<parameter name="type" required="true">
				<para>Type refers to which type of profile the option belongs too.  Type can be <literal>bridge</literal> or <literal>user</literal>.</para>
			</parameter>
            <parameter name="option" required="true">
				<para>Option refers to <filename>confbridge.conf</filename> option that is being set dynamically on this channel.</para>
			</parameter>
		</syntax>
		<description>
			<para>---- Example 1 ----</para>
			<para>In this example the custom set user profile on this channel will automatically be used by the ConfBridge app.</para> 
			<para>exten => 1,1,Answer() </para>
			<para>exten => 1,n,Set(CONFBRIDGE(user,announce_join_leave)=yes)</para>
			<para>exten => 1,n,Set(CONFBRIDGE(user,startmuted)=yes)</para>
			<para>exten => 1,n,ConfBridge(1) </para>
			<para>---- Example 2 ----</para>
			<para>This example shows how to use a predefined user or bridge profile in confbridge.conf as a template for a dynamic profile. Here we make a admin/marked user out of the default_user profile that is already defined in confbridge.conf.</para> 
			<para>exten => 1,1,Answer() </para>
			<para>exten => 1,n,Set(CONFBRIDGE(user,template)=default_user)</para>
			<para>exten => 1,n,Set(CONFBRIDGE(user,admin)=yes)</para>
			<para>exten => 1,n,Set(CONFBRIDGE(user,marked)=yes)</para>
			<para>exten => 1,n,ConfBridge(1)</para>
		</description>
	</function>
	<function name="CONFBRIDGE_INFO" language="en_US">
		<synopsis>
			Get information about a ConfBridge conference.
		</synopsis>
		<syntax>
			<parameter name="type" required="true">
				<para>Type can be <literal>parties</literal>, <literal>admins</literal>, <literal>marked</literal>, or <literal>locked</literal>.</para>
			</parameter>
			<parameter name="conf" required="true">
				<para>Conf refers to the name of the conference being referenced.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function returns a non-negative integer for valid conference identifiers (0 or 1 for <literal>locked</literal>) and "" for invalid conference identifiers.</para> 
		</description>
	</function>
	<manager name="ConfbridgeList" language="en_US">
		<synopsis>
			List participants in a conference.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="false">
				<para>Conference number.</para>
			</parameter>
		</syntax>
		<description>
			<para>Lists all users in a particular ConfBridge conference.
			ConfbridgeList will follow as separate events, followed by a final event called
			ConfbridgeListComplete.</para>
		</description>
	</manager>
	<manager name="ConfbridgeListRooms" language="en_US">
		<synopsis>
			List active conferences.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Lists data about all active conferences.
				ConfbridgeListRooms will follow as separate events, followed by a final event called
				ConfbridgeListRoomsComplete.</para>
		</description>
	</manager>
	<manager name="ConfbridgeMute" language="en_US">
		<synopsis>
			Mute a Confbridge user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
			<parameter name="Channel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="ConfbridgeUnmute" language="en_US">
		<synopsis>
			Unmute a Confbridge user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
			<parameter name="Channel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="ConfbridgeKick" language="en_US">
		<synopsis>
			Kick a Confbridge user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
			<parameter name="Channel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="ConfbridgeLock" language="en_US">
		<synopsis>
			Lock a Confbridge conference.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="ConfbridgeUnlock" language="en_US">
		<synopsis>
			Unlock a Confbridge conference.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="ConfbridgeStartRecord" language="en_US">
		<synopsis>
			Start recording a Confbridge conference.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
			<parameter name="RecordFile" required="false" />
		</syntax>
		<description>
			<para>Start recording a conference. If recording is already present an error will be returned. If RecordFile is not provided, the default record file specified in the conference's bridge profile will be used, if that is not present either a file will automatically be generated in the monitor directory.</para>
		</description>
	</manager>
	<manager name="ConfbridgeStopRecord" language="en_US">
		<synopsis>
			Stop recording a Confbridge conference.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="ConfbridgeSetSingleVideoSrc" language="en_US">
		<synopsis>
			Set a conference user as the single video source distributed to all other participants.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Conference" required="true" />
			<parameter name="Channel" required="true" />
		</syntax>
		<description>
		</description>
	</manager>

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

static const char app[] = "ConfBridge";

/* Number of buckets our conference bridges container can have */
#define CONFERENCE_BRIDGE_BUCKETS 53

/*! \brief Container to hold all conference bridges in progress */
static struct ao2_container *conference_bridges;

static int play_sound_file(struct conference_bridge *conference_bridge, const char *filename);
static int play_sound_number(struct conference_bridge *conference_bridge, int say_number);
static int execute_menu_entry(struct conference_bridge *conference_bridge,
	struct conference_bridge_user *conference_bridge_user,
	struct ast_bridge_channel *bridge_channel,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu);

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

const char *conf_get_sound(enum conf_sounds sound, struct bridge_profile_sounds *custom_sounds)
{
	switch (sound) {
	case CONF_SOUND_HAS_JOINED:
		return S_OR(custom_sounds->hasjoin, "conf-hasjoin");
	case CONF_SOUND_HAS_LEFT:
		return S_OR(custom_sounds->hasleft, "conf-hasleft");
	case CONF_SOUND_KICKED:
		return S_OR(custom_sounds->kicked, "conf-kicked");
	case CONF_SOUND_MUTED:
		return S_OR(custom_sounds->muted, "conf-muted");
	case CONF_SOUND_UNMUTED:
		return S_OR(custom_sounds->unmuted, "conf-unmuted");
	case CONF_SOUND_ONLY_ONE:
		return S_OR(custom_sounds->onlyone, "conf-onlyone");
	case CONF_SOUND_THERE_ARE:
		return S_OR(custom_sounds->thereare, "conf-thereare");
	case CONF_SOUND_OTHER_IN_PARTY:
		return S_OR(custom_sounds->otherinparty, "conf-otherinparty");
	case CONF_SOUND_PLACE_IN_CONF:
		return S_OR(custom_sounds->placeintoconf, "conf-placeintoconf");
	case CONF_SOUND_WAIT_FOR_LEADER:
		return S_OR(custom_sounds->waitforleader, "conf-waitforleader");
	case CONF_SOUND_LEADER_HAS_LEFT:
		return S_OR(custom_sounds->leaderhasleft, "conf-leaderhasleft");
	case CONF_SOUND_GET_PIN:
		return S_OR(custom_sounds->getpin, "conf-getpin");
	case CONF_SOUND_INVALID_PIN:
		return S_OR(custom_sounds->invalidpin, "conf-invalidpin");
	case CONF_SOUND_ONLY_PERSON:
		return S_OR(custom_sounds->onlyperson, "conf-onlyperson");
	case CONF_SOUND_LOCKED:
		return S_OR(custom_sounds->locked, "conf-locked");
	case CONF_SOUND_LOCKED_NOW:
		return S_OR(custom_sounds->lockednow, "conf-lockednow");
	case CONF_SOUND_UNLOCKED_NOW:
		return S_OR(custom_sounds->unlockednow, "conf-unlockednow");
	case CONF_SOUND_ERROR_MENU:
		return S_OR(custom_sounds->errormenu, "conf-errormenu");
	case CONF_SOUND_JOIN:
		return S_OR(custom_sounds->join, "confbridge-join");
	case CONF_SOUND_LEAVE:
		return S_OR(custom_sounds->leave, "confbridge-leave");
	}

	return "";
}

static struct ast_frame *rec_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}
static int rec_write(struct ast_channel *ast, struct ast_frame *f)
{
	return 0;
}
static struct ast_channel *rec_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause);
static struct ast_channel_tech record_tech = {
	.type = "ConfBridgeRec",
	.description = "Conference Bridge Recording Channel",
	.requester = rec_request,
	.read = rec_read,
	.write = rec_write,
};
static struct ast_channel *rec_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause)
{
	struct ast_channel *tmp;
	struct ast_format fmt;
	const char *conf_name = data;
	if (!(tmp = ast_channel_alloc(1, AST_STATE_UP, 0, 0, "", "", "", NULL, 0,
		"ConfBridgeRecorder/conf-%s-uid-%d",
		conf_name,
		(int) ast_random()))) {
		return NULL;
	}
	ast_format_set(&fmt, AST_FORMAT_SLINEAR, 0);
	tmp->tech = &record_tech;
	ast_format_cap_add_all(tmp->nativeformats);
	ast_format_copy(&tmp->writeformat, &fmt);
	ast_format_copy(&tmp->rawwriteformat, &fmt);
	ast_format_copy(&tmp->readformat, &fmt);
	ast_format_copy(&tmp->rawreadformat, &fmt);
	return tmp;
}

static void *record_thread(void *obj)
{
	struct conference_bridge *conference_bridge = obj;
	struct ast_app *mixmonapp = pbx_findapp("MixMonitor");
	struct ast_channel *chan;
	struct ast_str *filename = ast_str_alloca(PATH_MAX);

	if (!mixmonapp) {
		ao2_ref(conference_bridge, -1);
		return NULL;
	}

	ao2_lock(conference_bridge);
	if (!(conference_bridge->record_chan)) {
		conference_bridge->record_thread = AST_PTHREADT_NULL;
		ao2_unlock(conference_bridge);
		ao2_ref(conference_bridge, -1);
		return NULL;
	}
	chan = ast_channel_ref(conference_bridge->record_chan);

	if (!(ast_strlen_zero(conference_bridge->b_profile.rec_file))) {
		ast_str_append(&filename, 0, "%s", conference_bridge->b_profile.rec_file);
	} else {
		time_t now;
		time(&now);
		ast_str_append(&filename, 0, "confbridge-%s-%u.wav",
			conference_bridge->name,
			(unsigned int) now);
	}
	ao2_unlock(conference_bridge);

	ast_answer(chan);
	pbx_exec(chan, mixmonapp, ast_str_buffer(filename));
	ast_bridge_join(conference_bridge->bridge, chan, NULL, NULL, NULL);

	ao2_lock(conference_bridge);
	conference_bridge->record_thread = AST_PTHREADT_NULL;
	ao2_unlock(conference_bridge);

	ast_hangup(chan); /* This will eat this threads reference to the channel as well */
	ao2_ref(conference_bridge, -1);
	return NULL;
}

/*!
 * \internal
 * \brief Returns whether or not conference is being recorded.
 * \retval 1, conference is recording.
 * \retval 0, conference is NOT recording.
 */
static int conf_is_recording(struct conference_bridge *conference_bridge)
{
	int res = 0;
	ao2_lock(conference_bridge);
	if (conference_bridge->record_chan || conference_bridge->record_thread != AST_PTHREADT_NULL) {
		res = 1;
	}
	ao2_unlock(conference_bridge);
	return res;
}

/*!
 * \internal
 * \brief Stops the confbridge recording thread.
 *
 * \note do not call this function with any locks
 */
static int conf_stop_record(struct conference_bridge *conference_bridge)
{
	ao2_lock(conference_bridge);

	if (conference_bridge->record_thread != AST_PTHREADT_NULL) {
		struct ast_channel *chan = ast_channel_ref(conference_bridge->record_chan);
		pthread_t thread = conference_bridge->record_thread;
		ao2_unlock(conference_bridge);

		ast_bridge_remove(conference_bridge->bridge, chan);
		ast_queue_frame(chan, &ast_null_frame);

		chan = ast_channel_unref(chan);
		pthread_join(thread, NULL);

		ao2_lock(conference_bridge);
	}

	/* this is the reference given to the channel during the channel alloc */
	if (conference_bridge->record_chan) {
		conference_bridge->record_chan = ast_channel_unref(conference_bridge->record_chan);
	}

	ao2_unlock(conference_bridge);
	return 0;
}

static int conf_start_record(struct conference_bridge *conference_bridge)
{
	struct ast_format_cap *cap = ast_format_cap_alloc_nolock();
	struct ast_format tmpfmt;
	int cause;

	ao2_lock(conference_bridge);
	if (conference_bridge->record_chan || conference_bridge->record_thread != AST_PTHREADT_NULL) {
		ao2_unlock(conference_bridge);
		return -1; /* already recording */
	}
	if (!cap) {
		ao2_unlock(conference_bridge);
		return -1;
	}
	if (!pbx_findapp("MixMonitor")) {
		ast_log(LOG_WARNING, "Can not record ConfBridge, MixMonitor app is not installed\n");
		cap = ast_format_cap_destroy(cap);
		ao2_unlock(conference_bridge);
		return -1;
	}
	ast_format_cap_add(cap, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));
	if (!(conference_bridge->record_chan = ast_request("ConfBridgeRec", cap, NULL, conference_bridge->name, &cause))) {
		cap = ast_format_cap_destroy(cap);
		ao2_unlock(conference_bridge);
		return -1;
	}

	cap = ast_format_cap_destroy(cap);
	ao2_ref(conference_bridge, +1); /* give the record thread a ref */

	if (ast_pthread_create_background(&conference_bridge->record_thread, NULL, record_thread, conference_bridge)) {
		ast_log(LOG_WARNING, "Failed to create recording channel for conference %s\n", conference_bridge->name);

		ao2_unlock(conference_bridge);
		ao2_ref(conference_bridge, -1); /* error so remove ref */
		return -1;
	}

	ao2_unlock(conference_bridge);
	return 0;
}

static void send_conf_start_event(const char *conf_name)
{
	manager_event(EVENT_FLAG_CALL, "ConfbridgeStart", "Conference: %s\r\n", conf_name);
}

static void send_conf_end_event(const char *conf_name)
{
	manager_event(EVENT_FLAG_CALL, "ConfbridgeEnd", "Conference: %s\r\n", conf_name);
}

static void send_join_event(struct ast_channel *chan, const char *conf_name)
{
	ast_manager_event(chan, EVENT_FLAG_CALL, "ConfbridgeJoin",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Conference: %s\r\n"
		"CallerIDnum: %s\r\n"
		"CallerIDname: %s\r\n",
		chan->name,
		chan->uniqueid,
		conf_name,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, "<unknown>"),
		S_COR(chan->caller.id.name.valid, chan->caller.id.name.str, "<unknown>")
	);
}

static void send_leave_event(struct ast_channel *chan, const char *conf_name)
{
	ast_manager_event(chan, EVENT_FLAG_CALL, "ConfbridgeLeave",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Conference: %s\r\n"
		"CallerIDnum: %s\r\n"
		"CallerIDname: %s\r\n",
		chan->name,
		chan->uniqueid,
		conf_name,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, "<unknown>"),
		S_COR(chan->caller.id.name.valid, chan->caller.id.name.str, "<unknown>")
	);
}

/*!
 * \brief Announce number of users in the conference bridge to the caller
 *
 * \param conference_bridge Conference bridge to peek at
 * \param (OPTIONAL) conference_bridge_user Caller
 *
 * \note if caller is NULL, the announcment will be sent to all participants in the conference.
 * \return Returns 0 on success, -1 if the user hung up
 */
static int announce_user_count(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	const char *other_in_party = conf_get_sound(CONF_SOUND_OTHER_IN_PARTY, conference_bridge->b_profile.sounds);
	const char *only_one = conf_get_sound(CONF_SOUND_ONLY_ONE, conference_bridge->b_profile.sounds);
	const char *there_are = conf_get_sound(CONF_SOUND_THERE_ARE, conference_bridge->b_profile.sounds);

	if (conference_bridge->users == 1) {
		/* Awww we are the only person in the conference bridge */
		return 0;
	} else if (conference_bridge->users == 2) {
		if (conference_bridge_user) {
			/* Eep, there is one other person */
			if (ast_stream_and_wait(conference_bridge_user->chan,
				only_one,
				"")) {
				return -1;
			}
		} else {
			play_sound_file(conference_bridge, only_one);
		}
	} else {
		/* Alas multiple others in here */
		if (conference_bridge_user) {
			if (ast_stream_and_wait(conference_bridge_user->chan,
				there_are,
				"")) {
				return -1;
			}
			if (ast_say_number(conference_bridge_user->chan, conference_bridge->users - 1, "", conference_bridge_user->chan->language, NULL)) {
				return -1;
			}
			if (ast_stream_and_wait(conference_bridge_user->chan,
				other_in_party,
				"")) {
				return -1;
			}
		} else {
			play_sound_file(conference_bridge, there_are);
			play_sound_number(conference_bridge, conference_bridge->users - 1);
			play_sound_file(conference_bridge, other_in_party);
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

static void handle_video_on_join(struct conference_bridge *conference_bridge, struct ast_channel *chan, int marked)
{
	/* Right now, only marked users are automatically set as the single src of video.*/
	if (!marked) {
		return;
	}

	if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED)) {
		int set = 1;
		struct conference_bridge_user *tmp_user = NULL;
		ao2_lock(conference_bridge);
		/* see if anyone is already the video src */
		AST_LIST_TRAVERSE(&conference_bridge->users_list, tmp_user, list) {
			if (tmp_user->chan == chan) {
				continue;
			}
			if (ast_bridge_is_video_src(conference_bridge->bridge, tmp_user->chan)) {
				set = 0;
				break;
			}
		}
		ao2_unlock(conference_bridge);
		if (set) {
			ast_bridge_set_single_src_video_mode(conference_bridge->bridge, chan);
		}
	} else if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_LAST_MARKED)) {
		/* we joined and are video capable, we override anyone else that may have already been the video feed */
		ast_bridge_set_single_src_video_mode(conference_bridge->bridge, chan);
	}
}

static void handle_video_on_exit(struct conference_bridge *conference_bridge, struct ast_channel *chan)
{
	struct conference_bridge_user *tmp_user = NULL;

	/* if this isn't a video source, nothing to update */
	if (!ast_bridge_is_video_src(conference_bridge->bridge, chan)) {
		return;
	}

	ast_bridge_remove_video_src(conference_bridge->bridge, chan);

	/* If in follow talker mode, make sure to restore this mode on the
	 * bridge when a source is removed.  It is possible this channel was
	 * only set temporarily as a video source by an AMI or DTMF action. */
	if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER)) {
		ast_bridge_set_talker_src_video_mode(conference_bridge->bridge);
	}

	/* if the video_mode isn't set to automatically pick the video source, do nothing on exit. */
	if (!ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED) &&
		!ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_LAST_MARKED)) {
		return;
	}

	/* Make the next available marked user the video src.  */
	ao2_lock(conference_bridge);
	AST_LIST_TRAVERSE(&conference_bridge->users_list, tmp_user, list) {
		if (tmp_user->chan == chan) {
			continue;
		}
		if (ast_test_flag(&tmp_user->u_profile, USER_OPT_MARKEDUSER)) {
			ast_bridge_set_single_src_video_mode(conference_bridge->bridge, tmp_user->chan);
			break;
		}
	}
	ao2_unlock(conference_bridge);
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
	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MARKEDUSER)) {
		struct conference_bridge_user *other_conference_bridge_user = NULL;

		/* If we are not the first user to join, then the users are already
		 * in the conference so we do not need to update them. */
		if (conference_bridge->markedusers >= 2) {
			return 0;
		}

		/* Iterate through every participant stopping MOH on them if need be */
		AST_LIST_TRAVERSE(&conference_bridge->users_list, other_conference_bridge_user, list) {
			if (other_conference_bridge_user == conference_bridge_user) {
				continue;
			}
			if (other_conference_bridge_user->playing_moh && !ast_bridge_suspend(conference_bridge->bridge, other_conference_bridge_user->chan)) {
				other_conference_bridge_user->playing_moh = 0;
				ast_moh_stop(other_conference_bridge_user->chan);
				ast_bridge_unsuspend(conference_bridge->bridge, other_conference_bridge_user->chan);
			}
		}

		/* Next play the audio file stating they are going to be placed into the conference */
		if (!ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_QUIET)) {
			ao2_unlock(conference_bridge);
			ast_autoservice_start(conference_bridge_user->chan);
			play_sound_file(conference_bridge,
				conf_get_sound(CONF_SOUND_PLACE_IN_CONF, conference_bridge_user->b_profile.sounds));
			ast_autoservice_stop(conference_bridge_user->chan);
			ao2_lock(conference_bridge);
		}

		/* Finally iterate through and unmute them all */
		AST_LIST_TRAVERSE(&conference_bridge->users_list, other_conference_bridge_user, list) {
			if (other_conference_bridge_user == conference_bridge_user) {
				continue;
			}
			/* only unmute them if they are not supposed to start muted */
			if (!ast_test_flag(&other_conference_bridge_user->u_profile, USER_OPT_STARTMUTED)) {
				other_conference_bridge_user->features.mute = 0;
			}
		}
	} else {
		/* If a marked user already exists in the conference bridge we can just bail out now */
		if (conference_bridge->markedusers) {
			return 0;
		}
		/* Be sure we are muted so we can't talk to anybody else waiting */
		conference_bridge_user->features.mute = 1;
		/* If we have not been quieted play back that they are waiting for the leader */
		if (!ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_QUIET)) {
			if (play_prompt_to_channel(conference_bridge,
				conference_bridge_user->chan,
				conf_get_sound(CONF_SOUND_WAIT_FOR_LEADER, conference_bridge_user->b_profile.sounds))) {
				/* user hungup while the sound was playing */
				return -1;
			}
		}
		/* Start music on hold if needed */
		/* We need to recheck the markedusers value here. play_prompt_to_channel unlocks the conference bridge, potentially
		 * allowing a marked user to enter while the prompt was playing
		 */
		if (!conference_bridge->markedusers && ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MUSICONHOLD)) {
			ast_moh_start(conference_bridge_user->chan, conference_bridge_user->u_profile.moh_class, NULL);
			conference_bridge_user->playing_moh = 1;
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
		if (!ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_QUIET | USER_OPT_NOONLYPERSON)) {
			if (play_prompt_to_channel(conference_bridge,
				conference_bridge_user->chan,
				conf_get_sound(CONF_SOUND_ONLY_PERSON, conference_bridge_user->b_profile.sounds))) {
				/* user hungup while the sound was playing */
				return -1;
			}
		}
		/* If we need to start music on hold on the channel do so now */
		/* We need to re-check the number of users in the conference bridge here because another conference bridge
		 * participant could have joined while the above prompt was playing for the first user.
		 */
		if (conference_bridge->users == 1 && ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MUSICONHOLD)) {
			ast_moh_start(conference_bridge_user->chan, conference_bridge_user->u_profile.moh_class, NULL);
			conference_bridge_user->playing_moh = 1;
		}
		return 0;
	}

	/* Announce number of users if need be */
	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ANNOUNCEUSERCOUNT)) {
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
		if (ast_test_flag(&first_participant->u_profile, USER_OPT_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, first_participant->chan)) {
			first_participant->playing_moh = 0;
			ast_moh_stop(first_participant->chan);
			ast_bridge_unsuspend(conference_bridge->bridge, first_participant->chan);
		}
	}

	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ANNOUNCEUSERCOUNTALL) &&
		(conference_bridge->users > conference_bridge_user->u_profile.announce_user_count_all_after)) {
		ao2_unlock(conference_bridge);
		if (announce_user_count(conference_bridge, NULL)) {
			ao2_lock(conference_bridge);
			return -1;
		}
		ao2_lock(conference_bridge);
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
	conf_bridge_profile_destroy(&conference_bridge->b_profile);
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
	int start_record = 0;
	int max_members_reached = 0;

	ast_copy_string(tmp.name, name, sizeof(tmp.name));

	/* We explictly lock the conference bridges container ourselves so that other callers can not create duplicate conferences at the same */
	ao2_lock(conference_bridges);

	ast_debug(1, "Trying to find conference bridge '%s'\n", name);

	/* Attempt to find an existing conference bridge */
	conference_bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);

	if (conference_bridge && conference_bridge->b_profile.max_members) {
		max_members_reached = conference_bridge->b_profile.max_members > conference_bridge->users ? 0 : 1;
	}

	/* When finding a conference bridge that already exists make sure that it is not locked, and if so that we are not an admin */
	if (conference_bridge && (max_members_reached || conference_bridge->locked) && !ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ADMIN)) {
		ao2_unlock(conference_bridges);
		ao2_ref(conference_bridge, -1);
		ast_debug(1, "Conference bridge '%s' is locked and caller is not an admin\n", name);
		ast_stream_and_wait(conference_bridge_user->chan,
				conf_get_sound(CONF_SOUND_LOCKED, conference_bridge_user->b_profile.sounds),
				"");
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
		conference_bridge->record_thread = AST_PTHREADT_NULL;
		ast_copy_string(conference_bridge->name, name, sizeof(conference_bridge->name));
		conf_bridge_profile_copy(&conference_bridge->b_profile, &conference_bridge_user->b_profile);

		/* Create an actual bridge that will do the audio mixing */
		if (!(conference_bridge->bridge = ast_bridge_new(AST_BRIDGE_CAPABILITY_MULTIMIX, 0))) {
			ao2_ref(conference_bridge, -1);
			conference_bridge = NULL;
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR, "Conference bridge '%s' could not be created.\n", name);
			return NULL;
		}

		/* Set the internal sample rate on the bridge from the bridge profile */
		ast_bridge_set_internal_sample_rate(conference_bridge->bridge, conference_bridge->b_profile.internal_sample_rate);
		/* Set the internal mixing interval on the bridge from the bridge profile */
		ast_bridge_set_mixing_interval(conference_bridge->bridge, conference_bridge->b_profile.mix_interval);

		if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER)) {
			ast_bridge_set_talker_src_video_mode(conference_bridge->bridge);
		}

		/* Setup lock for playback channel */
		ast_mutex_init(&conference_bridge->playback_lock);

		/* Link it into the conference bridges container */
		ao2_link(conference_bridges, conference_bridge);


		send_conf_start_event(conference_bridge->name);
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
	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MARKEDUSER)) {
		conference_bridge->markedusers++;
	}

	/* Set the device state for this conference */
	if (conference_bridge->users == 1) {
		ast_devstate_changed(AST_DEVICE_INUSE, "confbridge:%s", conference_bridge->name);
	}

	/* If the caller is a marked user or is waiting for a marked user to enter pass 'em off, otherwise pass them off to do regular joining stuff */
	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MARKEDUSER | USER_OPT_WAITMARKED)) {
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

	/* check to see if recording needs to be started or not */
	if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_RECORD_CONFERENCE) && !conf_is_recording(conference_bridge)) {
		start_record = 1;
	}

	ao2_unlock(conference_bridge);

	if (start_record) {
		conf_start_record(conference_bridge);
	}

	return conference_bridge;
}

/*!
 * \brief Leave a conference bridge
 *
 * \param conference_bridge The conference bridge to leave
 * \param conference_bridge_user The conference bridge user structure
 *
 */
static void leave_conference_bridge(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	ao2_lock(conference_bridge);

	/* If this caller is a marked user bump down the count */
	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MARKEDUSER)) {
		conference_bridge->markedusers--;
	}

	/* Decrement the users count while keeping the previous participant count */
	conference_bridge->users--;

	/* Drop conference bridge user from the list, they be going bye bye */
	AST_LIST_REMOVE(&conference_bridge->users_list, conference_bridge_user, list);

	/* If there are still users in the conference bridge we may need to do things (such as start MOH on them) */
	if (conference_bridge->users) {
		if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_MARKEDUSER) && !conference_bridge->markedusers) {
			struct conference_bridge_user *other_participant = NULL;

			/* Start out with muting everyone */
			AST_LIST_TRAVERSE(&conference_bridge->users_list, other_participant, list) {
				other_participant->features.mute = 1;
			}

			/* Play back the audio prompt saying the leader has left the conference */
			if (!ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_QUIET)) {
				ao2_unlock(conference_bridge);
				ast_autoservice_start(conference_bridge_user->chan);
				play_sound_file(conference_bridge,
					conf_get_sound(CONF_SOUND_LEADER_HAS_LEFT, conference_bridge_user->b_profile.sounds));
				ast_autoservice_stop(conference_bridge_user->chan);
				ao2_lock(conference_bridge);
			}

			/* Now on to starting MOH or kick if needed */
			AST_LIST_TRAVERSE(&conference_bridge->users_list, other_participant, list) {
				if (ast_test_flag(&other_participant->u_profile, USER_OPT_ENDMARKED)) {
					other_participant->kicked = 1;
					ast_bridge_remove(conference_bridge->bridge, other_participant->chan);
				} else if (ast_test_flag(&other_participant->u_profile, USER_OPT_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, other_participant->chan)) {
					ast_moh_start(other_participant->chan, other_participant->u_profile.moh_class, NULL);
					other_participant->playing_moh = 1;
					ast_bridge_unsuspend(conference_bridge->bridge, other_participant->chan);
				}
			}
		} else if (conference_bridge->users == 1) {
			/* Of course if there is one other person in here we may need to start up MOH on them */
			struct conference_bridge_user *first_participant = AST_LIST_FIRST(&conference_bridge->users_list);

			if (ast_test_flag(&first_participant->u_profile, USER_OPT_MUSICONHOLD) && !ast_bridge_suspend(conference_bridge->bridge, first_participant->chan)) {
				ast_moh_start(first_participant->chan, first_participant->u_profile.moh_class, NULL);
				first_participant->playing_moh = 1;
				ast_bridge_unsuspend(conference_bridge->bridge, first_participant->chan);
			}
		}
	} else {
		/* Set device state to "not in use" */
		ast_devstate_changed(AST_DEVICE_NOT_INUSE, "confbridge:%s", conference_bridge->name);

		ao2_unlink(conference_bridges, conference_bridge);
		send_conf_end_event(conference_bridge->name);
	}

	/* Done mucking with the conference bridge, huzzah */
	ao2_unlock(conference_bridge);

	if (!conference_bridge->users) {
		conf_stop_record(conference_bridge);
	}

	ao2_ref(conference_bridge, -1);
}

/*!
 * \internal
 * \brief allocates playback chan on a channel
 * \pre expects conference to be locked before calling this function
 */
static int alloc_playback_chan(struct conference_bridge *conference_bridge)
{
	int cause;
	struct ast_format_cap *cap;
	struct ast_format tmpfmt;

	if (conference_bridge->playback_chan) {
		return 0;
	}
	if (!(cap = ast_format_cap_alloc_nolock())) {
		return -1;
	}
	ast_format_cap_add(cap, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));
	if (!(conference_bridge->playback_chan = ast_request("Bridge", cap, NULL, "", &cause))) {
		cap = ast_format_cap_destroy(cap);
		return -1;
	}
	cap = ast_format_cap_destroy(cap);

	conference_bridge->playback_chan->bridge = conference_bridge->bridge;

	if (ast_call(conference_bridge->playback_chan, "", 0)) {
		ast_hangup(conference_bridge->playback_chan);
		conference_bridge->playback_chan = NULL;
		return -1;
	}

	ast_debug(1, "Created a playback channel to conference bridge '%s'\n", conference_bridge->name);
	return 0;
}

static int play_sound_helper(struct conference_bridge *conference_bridge, const char *filename, int say_number)
{
	struct ast_channel *underlying_channel;

	ast_mutex_lock(&conference_bridge->playback_lock);
	if (!(conference_bridge->playback_chan)) {
		if (alloc_playback_chan(conference_bridge)) {
			ast_mutex_unlock(&conference_bridge->playback_lock);
			return -1;
		}
		underlying_channel = conference_bridge->playback_chan->tech->bridged_channel(conference_bridge->playback_chan, NULL);
	} else {
		/* Channel was already available so we just need to add it back into the bridge */
		underlying_channel = conference_bridge->playback_chan->tech->bridged_channel(conference_bridge->playback_chan, NULL);
		ast_bridge_impart(conference_bridge->bridge, underlying_channel, NULL, NULL);
	}

	/* The channel is all under our control, in goes the prompt */
	if (!ast_strlen_zero(filename)) {
		ast_stream_and_wait(conference_bridge->playback_chan, filename, "");
	} else {
		ast_say_number(conference_bridge->playback_chan, say_number, "", conference_bridge->playback_chan->language, NULL);
	}

	ast_debug(1, "Departing underlying channel '%s' from bridge '%p'\n", underlying_channel->name, conference_bridge->bridge);
	ast_bridge_depart(conference_bridge->bridge, underlying_channel);

	ast_mutex_unlock(&conference_bridge->playback_lock);

	return 0;
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
	return play_sound_helper(conference_bridge, filename, 0);
}

/*!
 * \brief Play number into the conference bridge
 *
 * \param conference_bridge The conference bridge to say the number into
 * \param number to say
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int play_sound_number(struct conference_bridge *conference_bridge, int say_number)
{
	return play_sound_helper(conference_bridge, NULL, say_number);
}

static void conf_handle_talker_destructor(void *pvt_data)
{
	ast_free(pvt_data);
}

static void conf_handle_talker_cb(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *pvt_data)
{
	char *conf_name = pvt_data;
	int talking;

	switch (bridge_channel->state) {
	case AST_BRIDGE_CHANNEL_STATE_START_TALKING:
		talking = 1;
		break;
	case AST_BRIDGE_CHANNEL_STATE_STOP_TALKING:
		talking = 0;
		break;
	default:
		return; /* uhh this shouldn't happen, but bail if it does. */
	}

	/* notify AMI someone is has either started or stopped talking */
	ast_manager_event(bridge_channel->chan, EVENT_FLAG_CALL, "ConfbridgeTalking",
	      "Channel: %s\r\n"
	      "Uniqueid: %s\r\n"
	      "Conference: %s\r\n"
	      "TalkingStatus: %s\r\n",
	      bridge_channel->chan->name, bridge_channel->chan->uniqueid, conf_name, talking ? "on" : "off");
}

static int conf_get_pin(struct ast_channel *chan, struct conference_bridge_user *conference_bridge_user)
{
	char pin_guess[MAX_PIN+1] = { 0, };
	const char *pin = conference_bridge_user->u_profile.pin;
	char *tmp = pin_guess;
	int i, res;
	unsigned int len = MAX_PIN ;

	/* give them three tries to get the pin right */
	for (i = 0; i < 3; i++) {
		if (ast_app_getdata(chan,
			conf_get_sound(CONF_SOUND_GET_PIN, conference_bridge_user->b_profile.sounds),
			tmp, len, 0) >= 0) {
			if (!strcasecmp(pin, pin_guess)) {
				return 0;
			}
		}
		ast_streamfile(chan,
			conf_get_sound(CONF_SOUND_INVALID_PIN, conference_bridge_user->b_profile.sounds),
			chan->language);
		res = ast_waitstream(chan, AST_DIGIT_ANY);
		if (res > 0) {
			/* Account for digit already read during ivalid pin playback
			 * resetting pin buf. */
			pin_guess[0] = res;
			pin_guess[1] = '\0';
			tmp = pin_guess + 1;
			len = MAX_PIN - 1;
		} else {
			/* reset pin buf as empty buffer. */
			tmp = pin_guess;
			len = MAX_PIN;
		}
	}
	return -1;
}

static int conf_rec_name(struct conference_bridge_user *user, const char *conf_name)
{
	char destdir[PATH_MAX];
	int res;
	int duration = 20;

	snprintf(destdir, sizeof(destdir), "%s/confbridge", ast_config_AST_SPOOL_DIR);

	if (ast_mkdir(destdir, 0777) != 0) {
		ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", destdir, strerror(errno));
		return -1;
	}
	snprintf(user->name_rec_location, sizeof(user->name_rec_location),
		 "%s/confbridge-name-%s-%s", destdir,
		 conf_name, user->chan->uniqueid);

	res = ast_play_and_record(user->chan,
		"vm-rec-name",
		user->name_rec_location,
		10,
		"sln",
		&duration,
		ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE),
		0,
		NULL);

	if (res == -1) {
		user->name_rec_location[0] = '\0';
		return -1;
	}
	return 0;
}

/*! \brief The ConfBridge application */
static int confbridge_exec(struct ast_channel *chan, const char *data)
{
	int res = 0, volume_adjustments[2];
	int quiet = 0;
	char *parse;
	const char *b_profile_name = DEFAULT_BRIDGE_PROFILE;
	const char *u_profile_name = DEFAULT_USER_PROFILE;
	struct conference_bridge *conference_bridge = NULL;
	struct conference_bridge_user conference_bridge_user = {
		.chan = chan,
		.tech_args.talking_threshold = DEFAULT_TALKING_THRESHOLD,
		.tech_args.silence_threshold = DEFAULT_SILENCE_THRESHOLD,
		.tech_args.drop_silence = 0,
	};
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(conf_name);
		AST_APP_ARG(b_profile_name);
		AST_APP_ARG(u_profile_name);
		AST_APP_ARG(menu_name);
	);
	ast_bridge_features_init(&conference_bridge_user.features);

	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (conference name[,options])\n", app);
		res = -1; /* invalid PIN */
		goto confbridge_cleanup;
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	/* bridge profile name */
	if (args.argc > 1 && !ast_strlen_zero(args.b_profile_name)) {
		b_profile_name = args.b_profile_name;
	}
	if (!conf_find_bridge_profile(chan, b_profile_name, &conference_bridge_user.b_profile)) {
		ast_log(LOG_WARNING, "Conference bridge profile %s does not exist\n", b_profile_name);
		res = -1;
		goto confbridge_cleanup;
	}

	/* user profile name */
	if (args.argc > 2 && !ast_strlen_zero(args.u_profile_name)) {
		u_profile_name = args.u_profile_name;
	}

	if (!conf_find_user_profile(chan, u_profile_name, &conference_bridge_user.u_profile)) {
		ast_log(LOG_WARNING, "Conference user profile %s does not exist\n", u_profile_name);
		res = -1;
		goto confbridge_cleanup;
	}
	quiet = ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_QUIET);

	/* ask for a PIN immediately after finding user profile.  This has to be
	 * prompted for requardless of quiet setting. */
	if (!ast_strlen_zero(conference_bridge_user.u_profile.pin)) {
		if (conf_get_pin(chan, &conference_bridge_user)) {
			res = -1; /* invalid PIN */
			goto confbridge_cleanup;
		}
	}

	/* See if we need them to record a intro name */
	if (!quiet && ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_ANNOUNCE_JOIN_LEAVE)) {
		conf_rec_name(&conference_bridge_user, args.conf_name);
	}

	/* menu name */
	if (args.argc > 3 && !ast_strlen_zero(args.menu_name)) {
		ast_copy_string(conference_bridge_user.menu_name, args.menu_name, sizeof(conference_bridge_user.menu_name));
		if (conf_set_menu_to_user(conference_bridge_user.menu_name, &conference_bridge_user)) {
			ast_log(LOG_WARNING, "Conference menu %s does not exist and can not be applied to confbridge user.\n",
				args.menu_name);
			res = -1; /* invalid PIN */
			goto confbridge_cleanup;
		}
	}

	/* Set if DTMF should pass through for this user or not */
	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_DTMF_PASS)) {
		conference_bridge_user.features.dtmf_passthrough = 1;
	}

	/* Set dsp threshold values if present */
	if (conference_bridge_user.u_profile.talking_threshold) {
		conference_bridge_user.tech_args.talking_threshold = conference_bridge_user.u_profile.talking_threshold;
	}
	if (conference_bridge_user.u_profile.silence_threshold) {
		conference_bridge_user.tech_args.silence_threshold = conference_bridge_user.u_profile.silence_threshold;
	}

	/* Set a talker indicate call back if talking detection is requested */
	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_TALKER_DETECT)) {
		char *conf_name = ast_strdup(args.conf_name); /* this is freed during feature cleanup */
		if (!(conf_name)) {
			res = -1; /* invalid PIN */
			goto confbridge_cleanup;
		}
		ast_bridge_features_set_talk_detector(&conference_bridge_user.features,
			conf_handle_talker_cb,
			conf_handle_talker_destructor,
			conf_name);
	}

	/* Look for a conference bridge matching the provided name */
	if (!(conference_bridge = join_conference_bridge(args.conf_name, &conference_bridge_user))) {
		res = -1; /* invalid PIN */
		goto confbridge_cleanup;
	}

	/* Keep a copy of volume adjustments so we can restore them later if need be */
	volume_adjustments[0] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_READ);
	volume_adjustments[1] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_WRITE);

	/* If the caller should be joined already muted, make it so */
	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_STARTMUTED)) {
		conference_bridge_user.features.mute = 1;
	}

	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_DROP_SILENCE)) {
		conference_bridge_user.tech_args.drop_silence = 1;
	}

	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_JITTERBUFFER)) {
		char *func_jb;
		if ((func_jb = ast_module_helper("", "func_jitterbuffer", 0, 0, 0, 0))) {
			ast_free(func_jb);
			ast_func_write(chan, "JITTERBUFFER(adaptive)", "default");
		}
	}

	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_DENOISE)) {
		char *mod_speex;
		/* Reduce background noise from each participant */
		if ((mod_speex = ast_module_helper("", "codec_speex", 0, 0, 0, 0))) {
			ast_free(mod_speex);
			ast_func_write(chan, "DENOISE(rx)", "on");
		}
	}

	/* if this user has a intro, play it before entering */
	if (!ast_strlen_zero(conference_bridge_user.name_rec_location)) {
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, conference_bridge_user.name_rec_location);
		play_sound_file(conference_bridge,
			conf_get_sound(CONF_SOUND_HAS_JOINED, conference_bridge_user.b_profile.sounds));
		ast_autoservice_stop(chan);
	}

	/* Play the Join sound to both the conference and the user entering. */
	if (!quiet) {
		const char *join_sound = conf_get_sound(CONF_SOUND_JOIN, conference_bridge_user.b_profile.sounds);
		if (conference_bridge_user.playing_moh) {
			ast_moh_stop(chan);
		}
		ast_stream_and_wait(chan, join_sound, "");
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, join_sound);
		ast_autoservice_stop(chan);
		if (conference_bridge_user.playing_moh) {
			ast_moh_start(chan, conference_bridge_user.u_profile.moh_class, NULL);
		}
	}

	/* See if we need to automatically set this user as a video source or not */
	handle_video_on_join(conference_bridge, conference_bridge_user.chan, ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_MARKEDUSER));

	/* Join our conference bridge for real */
	send_join_event(conference_bridge_user.chan, conference_bridge->name);
	ast_bridge_join(conference_bridge->bridge,
		chan,
		NULL,
		&conference_bridge_user.features,
		&conference_bridge_user.tech_args);
	send_leave_event(conference_bridge_user.chan, conference_bridge->name);

	/* if we're shutting down, don't attempt to do further processing */
	if (ast_shutting_down()) {
		leave_conference_bridge(conference_bridge, &conference_bridge_user);
		conference_bridge = NULL;
		goto confbridge_cleanup;
	}

	/* If this user was a video source, we need to clean up and possibly pick a new source. */
	handle_video_on_exit(conference_bridge, conference_bridge_user.chan);

	/* if this user has a intro, play it when leaving */
	if (!quiet && !ast_strlen_zero(conference_bridge_user.name_rec_location)) {
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, conference_bridge_user.name_rec_location);
		play_sound_file(conference_bridge,
			conf_get_sound(CONF_SOUND_HAS_LEFT, conference_bridge_user.b_profile.sounds));
		ast_autoservice_stop(chan);
	}

	/* play the leave sound */
	if (!quiet) {
		const char *leave_sound = conf_get_sound(CONF_SOUND_LEAVE, conference_bridge_user.b_profile.sounds);
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, leave_sound);
		ast_autoservice_stop(chan);
	}

	/* Easy as pie, depart this channel from the conference bridge */
	leave_conference_bridge(conference_bridge, &conference_bridge_user);
	conference_bridge = NULL;

	/* If the user was kicked from the conference play back the audio prompt for it */
	if (!quiet && conference_bridge_user.kicked) {
		res = ast_stream_and_wait(chan,
			conf_get_sound(CONF_SOUND_KICKED, conference_bridge_user.b_profile.sounds),
			"");
	}

	/* Restore volume adjustments to previous values in case they were changed */
	if (volume_adjustments[0]) {
		ast_audiohook_volume_set(chan, AST_AUDIOHOOK_DIRECTION_READ, volume_adjustments[0]);
	}
	if (volume_adjustments[1]) {
		ast_audiohook_volume_set(chan, AST_AUDIOHOOK_DIRECTION_WRITE, volume_adjustments[1]);
	}

	if (!ast_strlen_zero(conference_bridge_user.name_rec_location)) {
		ast_filedelete(conference_bridge_user.name_rec_location, NULL);
	}

confbridge_cleanup:
	ast_bridge_features_cleanup(&conference_bridge_user.features);
	conf_bridge_profile_destroy(&conference_bridge_user.b_profile);
	return res;
}

static int action_toggle_mute(struct conference_bridge *conference_bridge,
	struct conference_bridge_user *conference_bridge_user,
	struct ast_channel *chan)
{
	/* Mute or unmute yourself, note we only allow manipulation if they aren't waiting for a marked user or if marked users exist */
	if (!ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_WAITMARKED) || conference_bridge->markedusers) {
		conference_bridge_user->features.mute = (!conference_bridge_user->features.mute ? 1 : 0);
	}
	return ast_stream_and_wait(chan, (conference_bridge_user->features.mute ?
		conf_get_sound(CONF_SOUND_MUTED, conference_bridge_user->b_profile.sounds) :
		conf_get_sound(CONF_SOUND_UNMUTED, conference_bridge_user->b_profile.sounds)),
		"");
}

static int action_playback(struct ast_bridge_channel *bridge_channel, const char *playback_file)
{
	char *file_copy = ast_strdupa(playback_file);
	char *file = NULL;

	while ((file = strsep(&file_copy, "&"))) {
		if (ast_stream_and_wait(bridge_channel->chan, file, "")) {
			ast_log(LOG_WARNING, "Failed to playback file %s to channel\n", file);
			return -1;
		}
	}
	return 0;
}

static int action_playback_and_continue(struct conference_bridge *conference_bridge,
	struct conference_bridge_user *conference_bridge_user,
	struct ast_bridge_channel *bridge_channel,
	struct conf_menu *menu,
	const char *playback_file,
	const char *cur_dtmf,
	int *stop_prompts)
{
	int i;
	int digit = 0;
	char dtmf[MAXIMUM_DTMF_FEATURE_STRING];
	struct conf_menu_entry new_menu_entry = { { 0, }, };
	char *file_copy = ast_strdupa(playback_file);
	char *file = NULL;

	while ((file = strsep(&file_copy, "&"))) {
		if (ast_streamfile(bridge_channel->chan, file, bridge_channel->chan->language)) {
			ast_log(LOG_WARNING, "Failed to playback file %s to channel\n", file);
			return -1;
		}

		/* now wait for more digits. */
		if (!(digit = ast_waitstream(bridge_channel->chan, AST_DIGIT_ANY))) {
			/* streaming finished and no DTMF was entered */
			continue;
		} else if (digit == -1) {
			/* error */
			return -1;
		} else {
			break; /* dtmf was entered */
		}
	}
	if (!digit) {
		/* streaming finished on all files and no DTMF was entered */
		return -1;
	}
	ast_stopstream(bridge_channel->chan);

	/* If we get here, then DTMF has been entered, This means no
	 * additional prompts should be played for this menu entry */
	*stop_prompts = 1;

	/* If a digit was pressed during the payback, update
	 * the dtmf string and look for a new menu entry in the
	 * menu structure */
	ast_copy_string(dtmf, cur_dtmf, sizeof(dtmf));
	for (i = 0; i < (MAXIMUM_DTMF_FEATURE_STRING - 1); i++) {
		dtmf[i] = cur_dtmf[i];
		if (!dtmf[i]) {
			dtmf[i] = (char) digit;
			dtmf[i + 1] = '\0';
			i = -1;
			break;
		}
	}
	/* If i is not -1 then the new dtmf digit was _NOT_ added to the string.
	 * If this is the case, no new DTMF sequence should be looked for. */
	if (i != -1) {
		return 0;
	}

	if (conf_find_menu_entry_by_sequence(dtmf, menu, &new_menu_entry)) {
		execute_menu_entry(conference_bridge,
			conference_bridge_user,
			bridge_channel,
			&new_menu_entry, menu);
		conf_menu_entry_destroy(&new_menu_entry);
	}
	return 0;
}

static int action_kick_last(struct conference_bridge *conference_bridge,
	struct ast_bridge_channel *bridge_channel,
	struct conference_bridge_user *conference_bridge_user)
{
	struct conference_bridge_user *last_participant = NULL;
	int isadmin = ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ADMIN);

	if (!isadmin) {
		ast_stream_and_wait(bridge_channel->chan,
			conf_get_sound(CONF_SOUND_ERROR_MENU, conference_bridge_user->b_profile.sounds),
			"");
		ast_log(LOG_WARNING, "Only admin users can use the kick_last menu action. Channel %s of conf %s is not an admin.\n",
			bridge_channel->chan->name,
			conference_bridge->name);
		return -1;
	}

	ao2_lock(conference_bridge);
	if (((last_participant = AST_LIST_LAST(&conference_bridge->users_list)) == conference_bridge_user)
		|| (ast_test_flag(&last_participant->u_profile, USER_OPT_ADMIN))) {
		ao2_unlock(conference_bridge);
		ast_stream_and_wait(bridge_channel->chan,
			conf_get_sound(CONF_SOUND_ERROR_MENU, conference_bridge_user->b_profile.sounds),
			"");
	} else if (last_participant) {
		last_participant->kicked = 1;
		ast_bridge_remove(conference_bridge->bridge, last_participant->chan);
		ao2_unlock(conference_bridge);
	}
	return 0;
}

static int action_dialplan_exec(struct ast_bridge_channel *bridge_channel, struct conf_menu_action *menu_action)
{
	struct ast_pbx_args args;
	struct ast_pbx *pbx;
	char *exten;
	char *context;
	int priority;
	int res;

	memset(&args, 0, sizeof(args));
	args.no_hangup_chan = 1;

	ast_channel_lock(bridge_channel->chan);

	/*save off*/
	exten = ast_strdupa(bridge_channel->chan->exten);
	context = ast_strdupa(bridge_channel->chan->context);
	priority = bridge_channel->chan->priority;
	pbx = bridge_channel->chan->pbx;
	bridge_channel->chan->pbx = NULL;

	/*set new*/
	ast_copy_string(bridge_channel->chan->exten, menu_action->data.dialplan_args.exten, sizeof(bridge_channel->chan->exten));
	ast_copy_string(bridge_channel->chan->context, menu_action->data.dialplan_args.context, sizeof(bridge_channel->chan->context));
	bridge_channel->chan->priority = menu_action->data.dialplan_args.priority;

	ast_channel_unlock(bridge_channel->chan);

	/*execute*/
	res = ast_pbx_run_args(bridge_channel->chan, &args);

	/*restore*/
	ast_channel_lock(bridge_channel->chan);

	ast_copy_string(bridge_channel->chan->exten, exten, sizeof(bridge_channel->chan->exten));
	ast_copy_string(bridge_channel->chan->context, context, sizeof(bridge_channel->chan->context));
	bridge_channel->chan->priority = priority;
	bridge_channel->chan->pbx = pbx;

	ast_channel_unlock(bridge_channel->chan);

	return res;
}

static int execute_menu_entry(struct conference_bridge *conference_bridge,
	struct conference_bridge_user *conference_bridge_user,
	struct ast_bridge_channel *bridge_channel,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu)
{
	struct conf_menu_action *menu_action;
	int isadmin = ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ADMIN);
	int stop_prompts = 0;
	int res = 0;

	AST_LIST_TRAVERSE(&menu_entry->actions, menu_action, action) {
		switch (menu_action->id) {
		case MENU_ACTION_TOGGLE_MUTE:
			res |= action_toggle_mute(conference_bridge,
				conference_bridge_user,
				bridge_channel->chan);
			break;
		case MENU_ACTION_PLAYBACK:
			if (!stop_prompts) {
				res |= action_playback(bridge_channel, menu_action->data.playback_file);
			}
			break;
		case MENU_ACTION_RESET_LISTENING:
			ast_audiohook_volume_set(conference_bridge_user->chan, AST_AUDIOHOOK_DIRECTION_WRITE, 0);
			break;
		case MENU_ACTION_RESET_TALKING:
			ast_audiohook_volume_set(conference_bridge_user->chan, AST_AUDIOHOOK_DIRECTION_READ, 0);
			break;
		case MENU_ACTION_INCREASE_LISTENING:
			ast_audiohook_volume_adjust(conference_bridge_user->chan,
				AST_AUDIOHOOK_DIRECTION_WRITE, 1);
			break;
		case MENU_ACTION_DECREASE_LISTENING:
			ast_audiohook_volume_adjust(conference_bridge_user->chan,
				AST_AUDIOHOOK_DIRECTION_WRITE, -1);
			break;
		case MENU_ACTION_INCREASE_TALKING:
			ast_audiohook_volume_adjust(conference_bridge_user->chan,
				AST_AUDIOHOOK_DIRECTION_READ, 1);
			break;
		case MENU_ACTION_DECREASE_TALKING:
			ast_audiohook_volume_adjust(conference_bridge_user->chan,
				AST_AUDIOHOOK_DIRECTION_READ, -1);
			break;
		case MENU_ACTION_PLAYBACK_AND_CONTINUE:
			if (!(stop_prompts)) {
				res |= action_playback_and_continue(conference_bridge,
					conference_bridge_user,
					bridge_channel,
					menu,
					menu_action->data.playback_file,
					menu_entry->dtmf,
					&stop_prompts);
			}
			break;
		case MENU_ACTION_DIALPLAN_EXEC:
			res |= action_dialplan_exec(bridge_channel, menu_action);
			break;
		case MENU_ACTION_ADMIN_TOGGLE_LOCK:
			if (!isadmin) {
				break;
			}
			conference_bridge->locked = (!conference_bridge->locked ? 1 : 0);
			res |= ast_stream_and_wait(bridge_channel->chan,
				(conference_bridge->locked ?
				conf_get_sound(CONF_SOUND_LOCKED_NOW, conference_bridge_user->b_profile.sounds) :
				conf_get_sound(CONF_SOUND_UNLOCKED_NOW, conference_bridge_user->b_profile.sounds)),
				"");

			break;
		case MENU_ACTION_ADMIN_KICK_LAST:
			res |= action_kick_last(conference_bridge, bridge_channel, conference_bridge_user);
			break;
		case MENU_ACTION_LEAVE:
			ao2_lock(conference_bridge);
			ast_bridge_remove(conference_bridge->bridge, bridge_channel->chan);
			ao2_unlock(conference_bridge);
			break;
		case MENU_ACTION_NOOP:
			break;
		case MENU_ACTION_SET_SINGLE_VIDEO_SRC:
			ao2_lock(conference_bridge);
			ast_bridge_set_single_src_video_mode(conference_bridge->bridge, bridge_channel->chan);
			ao2_unlock(conference_bridge);
			break;
		case MENU_ACTION_RELEASE_SINGLE_VIDEO_SRC:
			handle_video_on_exit(conference_bridge, bridge_channel->chan);
			break;
		}
	}
	return res;
}

int conf_handle_dtmf(struct ast_bridge_channel *bridge_channel,
	struct conference_bridge_user *conference_bridge_user,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu)
{
	struct conference_bridge *conference_bridge = conference_bridge_user->conference_bridge;

	/* See if music on hold is playing */
	ao2_lock(conference_bridge);
	if (conference_bridge_user->playing_moh) {
		/* MOH is going, let's stop it */
		ast_moh_stop(bridge_channel->chan);
	}
	ao2_unlock(conference_bridge);

	/* execute the list of actions associated with this menu entry */
	execute_menu_entry(conference_bridge, conference_bridge_user, bridge_channel, menu_entry, menu);

	/* See if music on hold needs to be started back up again */
	ao2_lock(conference_bridge);
	if (conference_bridge_user->playing_moh) {
		ast_moh_start(bridge_channel->chan, conference_bridge_user->u_profile.moh_class, NULL);
	}
	ao2_unlock(conference_bridge);

	return 0;
}

static char *complete_confbridge_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct conference_bridge *bridge = NULL;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;

	i = ao2_iterator_init(conference_bridges, 0);
	while ((bridge = ao2_iterator_next(&i))) {
		if (!strncasecmp(bridge->name, word, wordlen) && ++which > state) {
			res = ast_strdup(bridge->name);
			ao2_ref(bridge, -1);
			break;
		}
		ao2_ref(bridge, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_kick(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	struct conference_bridge_user *participant = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge kick";
		e->usage =
			"Usage: confbridge kick <conference> <channel>\n"
			"       Kicks a channel out of the conference bridge.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		/*
		if (a->pos == 3) {
			return complete_confbridge_channel(a->line, a->word, a->pos, a->n);
		}
		*/
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_copy_string(tmp.name, a->argv[2], sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		ast_cli(a->fd, "No conference bridge named '%s' found!\n", a->argv[2]);
		return CLI_SUCCESS;
	}
	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
		if (!strncmp(a->argv[3], participant->chan->name, strlen(participant->chan->name))) {
			break;
		}
	}
	if (participant) {
		ast_cli(a->fd, "Kicking %s from confbridge %s\n", participant->chan->name, bridge->name);
		participant->kicked = 1;
		ast_bridge_remove(bridge->bridge, participant->chan);
	}
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);
	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	struct conference_bridge_user *participant = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge list";
		e->usage =
			"Usage: confbridge list [<name>]\n"
			"       Lists all currently active conference bridges.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc == 2) {
		ast_cli(a->fd, "Conference Bridge Name           Users  Marked Locked?\n");
		ast_cli(a->fd, "================================ ====== ====== ========\n");
		i = ao2_iterator_init(conference_bridges, 0);
		while ((bridge = ao2_iterator_next(&i))) {
			ast_cli(a->fd, "%-32s %6i %6i %s\n", bridge->name, bridge->users, bridge->markedusers, (bridge->locked ? "locked" : "unlocked"));
			ao2_ref(bridge, -1);
		}
		ao2_iterator_destroy(&i);
		return CLI_SUCCESS;
	}

	if (a->argc == 3) {
		ast_copy_string(tmp.name, a->argv[2], sizeof(tmp.name));
		bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
		if (!bridge) {
			ast_cli(a->fd, "No conference bridge named '%s' found!\n", a->argv[2]);
			return CLI_SUCCESS;
		}
		ast_cli(a->fd, "Channel                       User Profile     Bridge Profile   Menu\n");
		ast_cli(a->fd, "============================= ================ ================ ================\n");
		ao2_lock(bridge);
		AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
			ast_cli(a->fd, "%-29s ", participant->chan->name);
			ast_cli(a->fd, "%-17s", participant->u_profile.name);
			ast_cli(a->fd, "%-17s", participant->b_profile.name);
			ast_cli(a->fd, "%-17s", participant->menu_name);
			ast_cli(a->fd, "\n");
		}
		ao2_unlock(bridge);
		ao2_ref(bridge, -1);
		return CLI_SUCCESS;
	}

	return CLI_SHOWUSAGE;
}

/* \internal
 * \brief finds a conference by name and locks/unlocks.
 *
 * \retval 0 success
 * \retval -1 conference not found
 */
static int generic_lock_unlock_helper(int lock, const char *conference)
{
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	int res = 0;

	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		return -1;
	}
	ao2_lock(bridge);
	bridge->locked = lock;
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);

	return res;
}

/* \internal
 * \brief finds a conference user by channel name and mutes/unmutes them.
 *
 * \retval 0 success
 * \retval -1 conference not found
 * \retval -2 user not found
 */
static int generic_mute_unmute_helper(int mute, const char *conference, const char *user)
{
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	struct conference_bridge_user *participant = NULL;
	int res = 0;
	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		return -1;
	}
	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
		if (!strncmp(user, participant->chan->name, strlen(user))) {
			break;
		}
	}
	if (participant) {
		participant->features.mute = mute;
	} else {
		res = -2;;
	}
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);

	return res;
}

static int cli_mute_unmute_helper(int mute, struct ast_cli_args *a)
{
	int res = generic_mute_unmute_helper(mute, a->argv[2], a->argv[3]);

	if (res == -1) {
		ast_cli(a->fd, "No conference bridge named '%s' found!\n", a->argv[2]);
		return -1;
	} else if (res == -2) {
		ast_cli(a->fd, "No channel named '%s' found in conference %s\n", a->argv[3], a->argv[2]);
		return -1;
	}
	ast_cli(a->fd, "%s %s from confbridge %s\n", mute ? "Muting" : "Unmuting", a->argv[3], a->argv[2]);
	return 0;
}

static char *handle_cli_confbridge_mute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge mute";
		e->usage =
			"Usage: confbridge mute <conference> <channel>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}
	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cli_mute_unmute_helper(1, a);

	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_unmute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge unmute";
		e->usage =
			"Usage: confbridge unmute <conference> <channel>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}
	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cli_mute_unmute_helper(0, a);

	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_lock(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge lock";
		e->usage =
			"Usage: confbridge lock <conference>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	if (generic_lock_unlock_helper(1, a->argv[2])) {
		ast_cli(a->fd, "Conference %s is not found\n", a->argv[2]);
	} else {
		ast_cli(a->fd, "Conference %s is locked.\n", a->argv[2]);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_unlock(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge unlock";
		e->usage =
			"Usage: confbridge unlock <conference>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	if (generic_lock_unlock_helper(0, a->argv[2])) {
		ast_cli(a->fd, "Conference %s is not found\n", a->argv[2]);
	} else {
		ast_cli(a->fd, "Conference %s is unlocked.\n", a->argv[2]);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_start_record(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *rec_file = NULL;
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge record start";
		e->usage =
			"Usage: confbridge record start <conference> <file>\n"
			"       <file> is optional, Otherwise the bridge profile\n"
			"       record file will be used.  If the bridge profile\n"
			"       has no record file specified, a file will automatically\n"
			"       be generated in the monitor directory\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}
	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}
	if (a->argc == 5) {
		rec_file = a->argv[4];
	}

	ast_copy_string(tmp.name, a->argv[3], sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		ast_cli(a->fd, "Conference not found.\n");
		return CLI_FAILURE;
	}
	if (conf_is_recording(bridge)) {
		ast_cli(a->fd, "Conference is already being recorded.\n");
		ao2_ref(bridge, -1);
		return CLI_SUCCESS;
	}
	if (!ast_strlen_zero(rec_file)) {
		ao2_lock(bridge);
		ast_copy_string(bridge->b_profile.rec_file, rec_file, sizeof(bridge->b_profile.rec_file));
		ao2_unlock(bridge);
	}
	if (conf_start_record(bridge)) {
		ast_cli(a->fd, "Could not start recording due to internal error.\n");
		ao2_ref(bridge, -1);
		return CLI_FAILURE;
	}
	ast_cli(a->fd, "Recording started\n");
	ao2_ref(bridge, -1);
	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_stop_record(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge record stop";
		e->usage =
			"Usage: confbridge record stop <conference>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}
	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_copy_string(tmp.name, a->argv[3], sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		ast_cli(a->fd, "Conference not found.\n");
		return CLI_SUCCESS;
	}
	conf_stop_record(bridge);
	ast_cli(a->fd, "Recording stopped.\n");
	ao2_ref(bridge, -1);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_confbridge[] = {
	AST_CLI_DEFINE(handle_cli_confbridge_list, "List conference bridges and participants."),
	AST_CLI_DEFINE(handle_cli_confbridge_kick, "Kick participants out of conference bridges."),
	AST_CLI_DEFINE(handle_cli_confbridge_mute, "Mute a participant."),
	AST_CLI_DEFINE(handle_cli_confbridge_unmute, "Mute a participant."),
	AST_CLI_DEFINE(handle_cli_confbridge_lock, "Lock a conference."),
	AST_CLI_DEFINE(handle_cli_confbridge_unlock, "Unlock a conference."),
	AST_CLI_DEFINE(handle_cli_confbridge_start_record, "Start recording a conference"),
	AST_CLI_DEFINE(handle_cli_confbridge_stop_record, "Stop recording a conference."),
};
static struct ast_custom_function confbridge_function = {
	.name = "CONFBRIDGE",
	.write = func_confbridge_helper,
};

static int func_confbridge_info(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);
static struct ast_custom_function confbridge_info_function = {
	.name = "CONFBRIDGE_INFO",
	.read = func_confbridge_info,
};

static int action_confbridgelist(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	const char *conference = astman_get_header(m, "Conference");
	struct conference_bridge_user *participant = NULL;
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	char id_text[80] = "";
	int total = 0;

	if (!ast_strlen_zero(actionid)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", actionid);
	}
	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}
	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	astman_send_listack(s, m, "Confbridge user list will follow", "start");

	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
		total++;
		astman_append(s,
			"Event: ConfbridgeList\r\n"
			"%s"
			"Conference: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"Channel: %s\r\n"
			"Admin: %s\r\n"
			"MarkedUser: %s\r\n"
			"\r\n",
			id_text,
			bridge->name,
			S_COR(participant->chan->caller.id.number.valid, participant->chan->caller.id.number.str, "<unknown>"),
			S_COR(participant->chan->caller.id.name.valid, participant->chan->caller.id.name.str, "<no name>"),
			participant->chan->name,
			ast_test_flag(&participant->u_profile, USER_OPT_ADMIN) ? "Yes" : "No",
			ast_test_flag(&participant->u_profile, USER_OPT_MARKEDUSER) ? "Yes" : "No");
	}
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);

	astman_append(s,
	"Event: ConfbridgeListComplete\r\n"
	"EventList: Complete\r\n"
	"ListItems: %d\r\n"
	"%s"
	"\r\n", total, id_text);

	return 0;
}

static int action_confbridgelistrooms(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	struct conference_bridge *bridge = NULL;
	struct ao2_iterator i;
	char id_text[512] = "";
	int totalitems = 0;

	if (!ast_strlen_zero(actionid)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", actionid);
	}

	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	astman_send_listack(s, m, "Confbridge conferences will follow", "start");

	/* Traverse the conference list */
	i = ao2_iterator_init(conference_bridges, 0);
	while ((bridge = ao2_iterator_next(&i))) {
		totalitems++;

		ao2_lock(bridge);
		astman_append(s,
		"Event: ConfbridgeListRooms\r\n"
		"%s"
		"Conference: %s\r\n"
		"Parties: %d\r\n"
		"Marked: %d\r\n"
		"Locked: %s\r\n"
		"\r\n",
		id_text,
		bridge->name,
		bridge->users,
		bridge->markedusers,
		bridge->locked ? "Yes" : "No"); 
		ao2_unlock(bridge);

		ao2_ref(bridge, -1);
	}
	ao2_iterator_destroy(&i);

	/* Send final confirmation */
	astman_append(s,
	"Event: ConfbridgeListRoomsComplete\r\n"
	"EventList: Complete\r\n"
	"ListItems: %d\r\n"
	"%s"
	"\r\n", totalitems, id_text);
	return 0;
}

static int action_mute_unmute_helper(struct mansession *s, const struct message *m, int mute)
{
	const char *conference = astman_get_header(m, "Conference");
	const char *channel = astman_get_header(m, "Channel");
	int res = 0;

	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	res = generic_mute_unmute_helper(mute, conference, channel);

	if (res == -1) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	} else if (res == -2) {
		astman_send_error(s, m, "No Channel by that name found in Conference.");
		return 0;
	}

	astman_send_ack(s, m, mute ? "User muted" : "User unmuted");
	return 0;
}

static int action_confbridgeunmute(struct mansession *s, const struct message *m)
{
	return action_mute_unmute_helper(s, m, 0);
}
static int action_confbridgemute(struct mansession *s, const struct message *m)
{
	return action_mute_unmute_helper(s, m, 1);
}

static int action_lock_unlock_helper(struct mansession *s, const struct message *m, int lock)
{
	const char *conference = astman_get_header(m, "Conference");
	int res = 0;

	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}
	if ((res = generic_lock_unlock_helper(lock, conference))) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}
	astman_send_ack(s, m, lock ? "Conference locked" : "Conference unlocked");
	return 0;
}
static int action_confbridgeunlock(struct mansession *s, const struct message *m)
{
	return action_lock_unlock_helper(s, m, 0);
}
static int action_confbridgelock(struct mansession *s, const struct message *m)
{
	return action_lock_unlock_helper(s, m, 1);
}

static int action_confbridgekick(struct mansession *s, const struct message *m)
{
	const char *conference = astman_get_header(m, "Conference");
	const char *channel = astman_get_header(m, "Channel");
	struct conference_bridge_user *participant = NULL;
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	int found = 0;

	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}
	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
		if (!strcasecmp(participant->chan->name, channel)) {
			participant->kicked = 1;
			ast_bridge_remove(bridge->bridge, participant->chan);
			found = 1;
			break;
		}
	}
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);

	if (found) {
		astman_send_ack(s, m, "User kicked");
	} else {
		astman_send_error(s, m, "No Channel by that name found in Conference.");
	}
	return 0;
}

static int action_confbridgestartrecord(struct mansession *s, const struct message *m)
{
	const char *conference = astman_get_header(m, "Conference");
	const char *recordfile = astman_get_header(m, "RecordFile");
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;

	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	if (conf_is_recording(bridge)) {
		astman_send_error(s, m, "Conference is already being recorded.");
		ao2_ref(bridge, -1);
		return 0;
	}

	if (!ast_strlen_zero(recordfile)) {
		ao2_lock(bridge);
		ast_copy_string(bridge->b_profile.rec_file, recordfile, sizeof(bridge->b_profile.rec_file));
		ao2_unlock(bridge);
	}

	if (conf_start_record(bridge)) {
		astman_send_error(s, m, "Internal error starting conference recording.");
		ao2_ref(bridge, -1);
		return 0;
	}

	ao2_ref(bridge, -1);
	astman_send_ack(s, m, "Conference Recording Started.");
	return 0;
}
static int action_confbridgestoprecord(struct mansession *s, const struct message *m)
{
	const char *conference = astman_get_header(m, "Conference");
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;

	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	if (conf_stop_record(bridge)) {
		astman_send_error(s, m, "Internal error while stopping recording.");
		ao2_ref(bridge, -1);
		return 0;
	}

	ao2_ref(bridge, -1);
	astman_send_ack(s, m, "Conference Recording Stopped.");
	return 0;
}

static int action_confbridgesetsinglevideosrc(struct mansession *s, const struct message *m)
{
	const char *conference = astman_get_header(m, "Conference");
	const char *channel = astman_get_header(m, "Channel");
	struct conference_bridge_user *participant = NULL;
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;

	if (ast_strlen_zero(conference)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "No channel name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	ast_copy_string(tmp.name, conference, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	/* find channel and set as video src. */
	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
		if (!strncmp(channel, participant->chan->name, strlen(channel))) {
			ast_bridge_set_single_src_video_mode(bridge->bridge, participant->chan);
			break;
		}
	}
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);

	/* do not access participant after bridge unlock.  We are just
	 * using this check to see if it was found or not */
	if (!participant) {
		astman_send_error(s, m, "No channel by that name found in conference.");
		return 0;
	}
	astman_send_ack(s, m, "Conference single video source set.");
	return 0;
}

static int func_confbridge_info(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parse = NULL;
	struct conference_bridge *bridge = NULL;
	struct conference_bridge_user *participant = NULL;
	struct conference_bridge tmp;
	int count = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(type);
		AST_APP_ARG(confno);
	);

	/* parse all the required arguments and make sure they exist. */
	if (ast_strlen_zero(data)) {
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (ast_strlen_zero(args.confno) || ast_strlen_zero(args.type)) {
		return -1;
	}
	if (!ao2_container_count(conference_bridges)) {
		ast_log(LOG_ERROR, "No active conferences.\n");
		return -1;
	}
	ast_copy_string(tmp.name, args.confno, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		ast_log(LOG_ERROR, "Conference '%s' not found.\n", args.confno);
		return -1;
	}

	/* get the correct count for the type requested */
	ao2_lock(bridge);
	if (!strncasecmp(args.type, "parties", 7)) {
		AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
			count++;
		}
	} else if (!strncasecmp(args.type, "admins", 6)) {
		AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
			if (ast_test_flag(&participant->u_profile, USER_OPT_ADMIN)) {
				count++;
			}
		}
	} else if (!strncasecmp(args.type, "marked", 6)) {
		AST_LIST_TRAVERSE(&bridge->users_list, participant, list) {
			if (ast_test_flag(&participant->u_profile, USER_OPT_MARKEDUSER)) {
				count++;
			}
		}
	} else if (!strncasecmp(args.type, "locked", 6)) {
		count = bridge->locked;
	} else {
		ao2_unlock(bridge);
		ao2_ref(bridge, -1);
		return -1;
	}
	snprintf(buf, len, "%d", count);
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);
	return 0;
}

/*! \brief Called when module is being unloaded */
static int unload_module(void)
{
	int res = ast_unregister_application(app);

	ast_custom_function_unregister(&confbridge_function);
	ast_custom_function_unregister(&confbridge_info_function);

	ast_cli_unregister_multiple(cli_confbridge, sizeof(cli_confbridge) / sizeof(struct ast_cli_entry));

	/* Get rid of the conference bridges container. Since we only allow dynamic ones none will be active. */
	ao2_ref(conference_bridges, -1);

	conf_destroy_config();

	ast_channel_unregister(&record_tech);
	record_tech.capabilities = ast_format_cap_destroy(record_tech.capabilities);

	res |= ast_manager_unregister("ConfbridgeList");
	res |= ast_manager_unregister("ConfbridgeListRooms");
	res |= ast_manager_unregister("ConfbridgeMute");
	res |= ast_manager_unregister("ConfbridgeUnmute");
	res |= ast_manager_unregister("ConfbridgeKick");
	res |= ast_manager_unregister("ConfbridgeUnlock");
	res |= ast_manager_unregister("ConfbridgeLock");
	res |= ast_manager_unregister("ConfbridgeStartRecord");
	res |= ast_manager_unregister("ConfbridgeStopRecord");

	return res;
}

/*! \brief Called when module is being loaded */
static int load_module(void)
{
	int res = 0;
	if ((ast_custom_function_register(&confbridge_function))) {
		return AST_MODULE_LOAD_FAILURE;
	}
	if ((ast_custom_function_register(&confbridge_info_function))) {
		return AST_MODULE_LOAD_FAILURE;
	}
	if (!(record_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_add_all(record_tech.capabilities);
	if (ast_channel_register(&record_tech)) {
		ast_log(LOG_ERROR, "Unable to register ConfBridge recorder.\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	/* Create a container to hold the conference bridges */
	if (!(conference_bridges = ao2_container_alloc(CONFERENCE_BRIDGE_BUCKETS, conference_bridge_hash_cb, conference_bridge_cmp_cb))) {
		return AST_MODULE_LOAD_FAILURE;
	}
	if (ast_register_application_xml(app, confbridge_exec)) {
		ao2_ref(conference_bridges, -1);
		return AST_MODULE_LOAD_FAILURE;
	}

	res |= ast_cli_register_multiple(cli_confbridge, sizeof(cli_confbridge) / sizeof(struct ast_cli_entry));
	res |= ast_manager_register_xml("ConfbridgeList", EVENT_FLAG_REPORTING, action_confbridgelist);
	res |= ast_manager_register_xml("ConfbridgeListRooms", EVENT_FLAG_REPORTING, action_confbridgelistrooms);
	res |= ast_manager_register_xml("ConfbridgeMute", EVENT_FLAG_CALL, action_confbridgemute);
	res |= ast_manager_register_xml("ConfbridgeUnmute", EVENT_FLAG_CALL, action_confbridgeunmute);
	res |= ast_manager_register_xml("ConfbridgeKick", EVENT_FLAG_CALL, action_confbridgekick);
	res |= ast_manager_register_xml("ConfbridgeUnlock", EVENT_FLAG_CALL, action_confbridgeunlock);
	res |= ast_manager_register_xml("ConfbridgeLock", EVENT_FLAG_CALL, action_confbridgelock);
	res |= ast_manager_register_xml("ConfbridgeStartRecord", EVENT_FLAG_CALL, action_confbridgestartrecord);
	res |= ast_manager_register_xml("ConfbridgeStopRecord", EVENT_FLAG_CALL, action_confbridgestoprecord);
	res |= ast_manager_register_xml("ConfbridgeSetSingleVideoSrc", EVENT_FLAG_CALL, action_confbridgesetsinglevideosrc);

	conf_load_config(0);
	return res;
}

static int reload(void)
{
	return conf_load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Conference Bridge Application",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);
