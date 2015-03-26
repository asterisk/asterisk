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
#include "asterisk/test.h"

/*** DOCUMENTATION
	<application name="ConfBridge" language="en_US">
		<synopsis>
			Conference bridge application.
		</synopsis>
		<syntax>
			<parameter name="conference" required="true">
				<para>Name of the conference bridge.  You are not limited to just
				numbers.</para>
			</parameter>
			<parameter name="bridge_profile">
				<para>The bridge profile name from confbridge.conf.  When left blank,
				a dynamically built bridge profile created by the CONFBRIDGE dialplan
				function is searched for on the channel and used.  If no dynamic
				profile is present, the 'default_bridge' profile found in
				confbridge.conf is used. </para>
				<para>It is important to note that while user profiles may be unique
				for each participant, mixing bridge profiles on a single conference
				is _NOT_ recommended and will produce undefined results.</para>
			</parameter>
			<parameter name="user_profile">
				<para>The user profile name from confbridge.conf.  When left blank,
				a dynamically built user profile created by the CONFBRIDGE dialplan
				function is searched for on the channel and used.  If no dynamic
				profile is present, the 'default_user' profile found in
				confbridge.conf is used.</para>
			</parameter>
			<parameter name="menu">
				<para>The name of the DTMF menu in confbridge.conf to be applied to
				this channel.  No menu is applied by default if this option is left
				blank.</para>
			</parameter>
		</syntax>
		<description>
			<para>Enters the user into a specified conference bridge.  The user can
			exit the conference by hangup or DTMF menu option.</para>
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
			<parameter name="Conference" required="true">
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
			<parameter name="Channel" required="true">
				<para>If this parameter is not a complete channel name, the first channel with this prefix will be used.</para>
			</parameter>
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
			<parameter name="Channel" required="true">
				<para>If this parameter is not a complete channel name, the first channel with this prefix will be used.</para>
			</parameter>
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
			<parameter name="Channel" required="true">
				<para>If this parameter is not a complete channel name, the first channel with this prefix will be used.</para>
			</parameter>
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

/*! Number of buckets our conference bridges container can have */
#define CONFERENCE_BRIDGE_BUCKETS 53

/*! Initial recording filename space. */
#define RECORD_FILENAME_INITIAL_SPACE	128

/*! \brief Container to hold all conference bridges in progress */
static struct ao2_container *conference_bridges;

static void leave_conference(struct conference_bridge_user *user);
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
	case CONF_SOUND_PARTICIPANTS_MUTED:
		return S_OR(custom_sounds->participantsmuted, "conf-now-muted");
	case CONF_SOUND_PARTICIPANTS_UNMUTED:
		return S_OR(custom_sounds->participantsunmuted, "conf-now-unmuted");
	case CONF_SOUND_BEGIN:
		return S_OR(custom_sounds->begin, "confbridge-conf-begin");
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
static struct ast_channel *rec_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
static struct ast_channel_tech record_tech = {
	.type = "ConfBridgeRec",
	.description = "Conference Bridge Recording Channel",
	.requester = rec_request,
	.read = rec_read,
	.write = rec_write,
};
static struct ast_channel *rec_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
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
	ast_channel_tech_set(tmp, &record_tech);
	ast_format_cap_add_all(ast_channel_nativeformats(tmp));
	ast_format_copy(ast_channel_writeformat(tmp), &fmt);
	ast_format_copy(ast_channel_rawwriteformat(tmp), &fmt);
	ast_format_copy(ast_channel_readformat(tmp), &fmt);
	ast_format_copy(ast_channel_rawreadformat(tmp), &fmt);
	return tmp;
}

static void set_rec_filename(struct conference_bridge *bridge, struct ast_str **filename, int is_new)
{
	char *rec_file = bridge->b_profile.rec_file;
	char *ext;
	time_t now;

	if (ast_str_strlen(*filename)
		&& !is_new) {
		return;
	}

	time(&now);

	ast_str_reset(*filename);
	if (ast_strlen_zero(rec_file)) {
		ast_str_set(filename, 0, "confbridge-%s-%u.wav", bridge->name,
			(unsigned int) now);
	} else {
		/* insert time before file extension */
		ext = strrchr(rec_file, '.');
		if (ext) {
			ast_str_set_substr(filename, 0, rec_file, ext - rec_file);
			ast_str_append(filename, 0, "-%u%s", (unsigned int) now, ext);
		} else {
			ast_str_set(filename, 0, "%s-%u", rec_file, (unsigned int) now);
		}
	}
	ast_str_append(filename, 0, ",a");
}

static int is_new_rec_file(const char *rec_file, struct ast_str **orig_rec_file)
{
	if (!ast_strlen_zero(rec_file)) {
		if (!*orig_rec_file) {
			*orig_rec_file = ast_str_create(RECORD_FILENAME_INITIAL_SPACE);
		}

		if (*orig_rec_file
			&& strcmp(ast_str_buffer(*orig_rec_file), rec_file)) {
			ast_str_set(orig_rec_file, 0, "%s", rec_file);
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Returns whether or not conference is being recorded.
 *
 * \param conference_bridge The bridge to check for recording
 *
 * \note Must be called with conference_bridge locked
 *
 * \retval 1, conference is recording.
 * \retval 0, conference is NOT recording.
 */
static int conf_is_recording(struct conference_bridge *conference_bridge)
{
	return conference_bridge->record_chan != NULL;
}

/*!
 * \internal
 * \brief Stop recording a conference bridge
 *
 * \param conference_bridge The conference bridge on which to stop the recording
 *
 * \note Must be called with conference_bridge locked
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
static int conf_stop_record(struct conference_bridge *conference_bridge)
{
	struct ast_channel *chan;
	struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_HANGUP };

	if (!conf_is_recording(conference_bridge)) {
		return -1;
	}

	/* Remove the recording channel from the conference bridge. */
	chan = conference_bridge->record_chan;
	conference_bridge->record_chan = NULL;
	ast_queue_frame(chan, &f);
	ast_channel_unref(chan);

	ast_test_suite_event_notify("CONF_STOP_RECORD", "Message: stopped conference recording channel\r\nConference: %s", conference_bridge->b_profile.name);

	return 0;
}

/*!
 * \internal
 * \brief Start recording the conference
 *
 * \param conference_bridge The conference bridge to start recording
 *
 * \note Must be called with conference_bridge locked
 *
 * \retval 0 success
 * \retval non-zero failure
 */
static int conf_start_record(struct conference_bridge *conference_bridge)
{
	struct ast_app *mixmonapp;
	struct ast_channel *chan;
	struct ast_format_cap *cap;
	struct ast_format tmpfmt;
	int cause;

	if (conf_is_recording(conference_bridge)) {
		return -1;
	}

	mixmonapp = pbx_findapp("MixMonitor");
	if (!mixmonapp) {
		ast_log(LOG_WARNING, "Cannot record ConfBridge, MixMonitor app is not installed\n");
		return -1;
	}

	cap = ast_format_cap_alloc_nolock();
	if (!cap) {
		return -1;
	}
	ast_format_cap_add(cap, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));

	/* Create the recording channel. */
	chan = ast_request("ConfBridgeRec", cap, NULL, conference_bridge->name, &cause);
	ast_format_cap_destroy(cap);
	if (!chan) {
		return -1;
	}

	/* Start recording. */
	set_rec_filename(conference_bridge, &conference_bridge->record_filename,
		is_new_rec_file(conference_bridge->b_profile.rec_file, &conference_bridge->orig_rec_file));
	ast_answer(chan);
	pbx_exec(chan, mixmonapp, ast_str_buffer(conference_bridge->record_filename));

	/* Put the channel into the conference bridge. */
	ast_channel_ref(chan);
	conference_bridge->record_chan = chan;
	if (ast_bridge_impart(conference_bridge->bridge, chan, NULL, NULL, 1)) {
		ast_hangup(chan);
		ast_channel_unref(chan);
		conference_bridge->record_chan = NULL;
		return -1;
	}

	ast_test_suite_event_notify("CONF_START_RECORD", "Message: started conference recording channel\r\nConference: %s", conference_bridge->b_profile.name);

	return 0;
}

static void send_conf_start_event(const char *conf_name)
{
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a conference starts.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeEnd</ref>
			</see-also>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "ConfbridgeStart", "Conference: %s\r\n", conf_name);
}

static void send_conf_end_event(const char *conf_name)
{
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a conference ends.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ConfbridgeStart']/managerEventInstance/syntax/parameter[@name='Conference'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeStart</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "ConfbridgeEnd", "Conference: %s\r\n", conf_name);
}

static void send_join_event(struct ast_channel *chan, const char *conf_name)
{
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a channel joins a Confbridge conference.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ConfbridgeStart']/managerEventInstance/syntax/parameter[@name='Conference'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeLeave</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "ConfbridgeJoin",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Conference: %s\r\n"
		"CallerIDnum: %s\r\n"
		"CallerIDname: %s\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		conf_name,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, "<unknown>")
	);
}

static void send_leave_event(struct ast_channel *chan, const char *conf_name)
{
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a channel leaves a Confbridge conference.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ConfbridgeStart']/managerEventInstance/syntax/parameter[@name='Conference'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeJoin</ref>
			</see-also>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "ConfbridgeLeave",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Conference: %s\r\n"
		"CallerIDnum: %s\r\n"
		"CallerIDname: %s\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		conf_name,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, "<unknown>")
	);
}

/* \brief Playback the given filename and monitor for any dtmf interrupts.
 *
 * Use this function to playback sound files to the given channel that can be
 * interrupted by dtmf. Note, too this function can be used to playback a sound
 * file without allowing interruptions.
 *
 * \retval -1 failure during playback, 0 on file was fully played, 1 on dtmf interrupt.
 */
static int play_file(struct ast_channel *channel, const char *filename, int allow_dtmf_interrupt)
{
	const char *stop_digits = allow_dtmf_interrupt ? AST_DIGIT_ANY : AST_DIGIT_NONE;
	int digit;

	digit = ast_stream_and_wait(channel, filename, stop_digits);
	if (digit < 0) {
		ast_log(LOG_WARNING, "Failed to playback file '%s' to channel\n", filename);
		return -1;
	}

	if (digit > 0) {
		/* file playback was interrupted by a key press, re-queue it */
		struct ast_frame frame;

		memset(&frame, 0, sizeof(frame));
		frame.frametype = AST_FRAME_DTMF_END;
		frame.subclass.integer = digit;

		ast_stopstream(channel);
		ast_queue_frame_head(channel, &frame);
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Complain if the given sound file does not exist.
 *
 * \param filename Sound file to check if exists.
 *
 * \retval non-zero if the file exists.
 */
static int sound_file_exists(const char *filename)
{
	if (ast_fileexists(filename, NULL, NULL)) {
		return -1;
	}
	ast_log(LOG_WARNING, "File %s does not exist in any format\n", filename);
	return 0;
}

/*!
 * \brief Announce number of users in the conference bridge to the caller
 *
 * \param conference_bridge Conference bridge to peek at
 * \param (OPTIONAL) conference_bridge_user Caller
 * \param allow_dtmf_interrupt allow dtmf to interrupt playback
 *
 * \note if caller is NULL, the announcment will be sent to all participants in the conference.
 * \return Returns 0 on success, -1 if the user hung up
 */
static int announce_user_count(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user,
			       int allow_dtmf_interrupt)
{
	const char *other_in_party = conf_get_sound(CONF_SOUND_OTHER_IN_PARTY, conference_bridge->b_profile.sounds);
	const char *only_one = conf_get_sound(CONF_SOUND_ONLY_ONE, conference_bridge->b_profile.sounds);
	const char *there_are = conf_get_sound(CONF_SOUND_THERE_ARE, conference_bridge->b_profile.sounds);

	if (conference_bridge->activeusers <= 1) {
		/* Awww we are the only person in the conference bridge OR we only have waitmarked users */
		return 0;
	} else if (conference_bridge->activeusers == 2) {
		if (conference_bridge_user) {
			/* Eep, there is one other person */
			if (play_file(conference_bridge_user->chan, only_one, allow_dtmf_interrupt) < 0) {
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
			if (ast_say_number(conference_bridge_user->chan, conference_bridge->activeusers - 1, "", ast_channel_language(conference_bridge_user->chan), NULL)) {
				return -1;
			}
			if (play_file(conference_bridge_user->chan, other_in_party, allow_dtmf_interrupt) < 0) {
				return -1;
			}
		} else if (sound_file_exists(there_are) && sound_file_exists(other_in_party)) {
			play_sound_file(conference_bridge, there_are);
			play_sound_number(conference_bridge, conference_bridge->activeusers - 1);
			play_sound_file(conference_bridge, other_in_party);
		}
	}
	return 0;
}

/*!
 * \brief Play back an audio file to a channel
 *
 * \param cbu User to play audio prompt to
 * \param filename Prompt to play
 *
 * \return Returns 0 on success, -1 if the user hung up
 * \note Generally this should be called when the conference is unlocked to avoid blocking
 * the entire conference while the sound is played. But don't unlock the conference bridge
 * in the middle of a state transition.
 */
static int play_prompt_to_user(struct conference_bridge_user *cbu, const char *filename)
{
	return ast_stream_and_wait(cbu->chan, filename, "");
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
		AST_LIST_TRAVERSE(&conference_bridge->active_list, tmp_user, list) {
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
	AST_LIST_TRAVERSE(&conference_bridge->active_list, tmp_user, list) {
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

	if (conference_bridge->playback_chan) {
		struct ast_channel *underlying_channel = ast_channel_tech(conference_bridge->playback_chan)->bridged_channel(conference_bridge->playback_chan, NULL);
		if (underlying_channel) {
			ast_hangup(underlying_channel);
		}
		ast_hangup(conference_bridge->playback_chan);
		conference_bridge->playback_chan = NULL;
	}

	/* Destroying a conference bridge is simple, all we have to do is destroy the bridging object */
	if (conference_bridge->bridge) {
		ast_bridge_destroy(conference_bridge->bridge);
		conference_bridge->bridge = NULL;
	}

	if (conference_bridge->record_chan) {
		ast_channel_unref(conference_bridge->record_chan);
	}
	ast_free(conference_bridge->orig_rec_file);
	ast_free(conference_bridge->record_filename);

	conf_bridge_profile_destroy(&conference_bridge->b_profile);
	ast_mutex_destroy(&conference_bridge->playback_lock);
}

/*! \brief Call the proper join event handler for the user for the conference bridge's current state
 * \internal
 * \param cbu The conference bridge user that is joining
 * \retval 0 success
 * \retval -1 failure
 */
static int handle_conf_user_join(struct conference_bridge_user *cbu)
{
	conference_event_fn handler;
	if (ast_test_flag(&cbu->u_profile, USER_OPT_MARKEDUSER)) {
		handler = cbu->conference_bridge->state->join_marked;
	} else if (ast_test_flag(&cbu->u_profile, USER_OPT_WAITMARKED)) {
		handler = cbu->conference_bridge->state->join_waitmarked;
	} else {
		handler = cbu->conference_bridge->state->join_unmarked;
	}

	ast_assert(handler != NULL);

	if (!handler) {
		conf_invalid_event_fn(cbu);
		return -1;
	}

	handler(cbu);

	return 0;
}

/*! \brief Call the proper leave event handler for the user for the conference bridge's current state
 * \internal
 * \param cbu The conference bridge user that is leaving
 * \retval 0 success
 * \retval -1 failure
 */
static int handle_conf_user_leave(struct conference_bridge_user *cbu)
{
	conference_event_fn handler;
	if (ast_test_flag(&cbu->u_profile, USER_OPT_MARKEDUSER)) {
		handler = cbu->conference_bridge->state->leave_marked;
	} else if (ast_test_flag(&cbu->u_profile, USER_OPT_WAITMARKED)) {
		handler = cbu->conference_bridge->state->leave_waitmarked;
	} else {
		handler = cbu->conference_bridge->state->leave_unmarked;
	}

	ast_assert(handler != NULL);

	if (!handler) {
		/* This should never happen. If it does, though, it is bad. The user will not have been removed
		 * from the appropriate list, so counts will be off and stuff. The conference won't be torn down, etc.
		 * Shouldn't happen, though. */
		conf_invalid_event_fn(cbu);
		return -1;
	}

	handler(cbu);

	return 0;
}

void conf_update_user_mute(struct conference_bridge_user *user)
{
	int mute_user;
	int mute_system;
	int mute_effective;

	/* User level mute request. */
	mute_user = user->muted;

	/* System level mute request. */
	mute_system = user->playing_moh
		/*
		 * Do not allow waitmarked users to talk to anyone unless there
		 * is a marked user present.
		 */
		|| (!user->conference_bridge->markedusers
			&& ast_test_flag(&user->u_profile, USER_OPT_WAITMARKED));

	mute_effective = mute_user || mute_system;

	ast_debug(1, "User %s is %s: user:%d system:%d.\n",
		ast_channel_name(user->chan), mute_effective ? "muted" : "unmuted",
		mute_user, mute_system);
	user->features.mute = mute_effective;
	ast_test_suite_event_notify("CONF_MUTE_UPDATE",
		"Mode: %s\r\n"
		"Conference: %s\r\n"
		"Channel: %s",
		mute_effective ? "muted" : "unmuted",
		user->b_profile.name,
		ast_channel_name(user->chan));
}

void conf_moh_stop(struct conference_bridge_user *user)
{
	user->playing_moh = 0;
	if (!user->suspended_moh) {
		int in_bridge;

		/*
		 * Locking the ast_bridge here is the only way to hold off the
		 * call to ast_bridge_join() in confbridge_exec() from
		 * interfering with the bridge and MOH operations here.
		 */
		ast_bridge_lock(user->conference_bridge->bridge);

		/*
		 * Temporarily suspend the user from the bridge so we have
		 * control to stop MOH if needed.
		 */
		in_bridge = !ast_bridge_suspend(user->conference_bridge->bridge, user->chan);
		ast_moh_stop(user->chan);
		if (in_bridge) {
			ast_bridge_unsuspend(user->conference_bridge->bridge, user->chan);
		}

		ast_bridge_unlock(user->conference_bridge->bridge);
	}
}

void conf_moh_start(struct conference_bridge_user *user)
{
	user->playing_moh = 1;
	if (!user->suspended_moh) {
		int in_bridge;

		/*
		 * Locking the ast_bridge here is the only way to hold off the
		 * call to ast_bridge_join() in confbridge_exec() from
		 * interfering with the bridge and MOH operations here.
		 */
		ast_bridge_lock(user->conference_bridge->bridge);

		/*
		 * Temporarily suspend the user from the bridge so we have
		 * control to start MOH if needed.
		 */
		in_bridge = !ast_bridge_suspend(user->conference_bridge->bridge, user->chan);
		ast_moh_start(user->chan, user->u_profile.moh_class, NULL);
		if (in_bridge) {
			ast_bridge_unsuspend(user->conference_bridge->bridge, user->chan);
		}

		ast_bridge_unlock(user->conference_bridge->bridge);
	}
}

/*!
 * \internal
 * \brief Unsuspend MOH for the conference user.
 *
 * \param user Conference user to unsuspend MOH on.
 *
 * \return Nothing
 */
static void conf_moh_unsuspend(struct conference_bridge_user *user)
{
	ao2_lock(user->conference_bridge);
	if (--user->suspended_moh == 0 && user->playing_moh) {
		ast_moh_start(user->chan, user->u_profile.moh_class, NULL);
	}
	ao2_unlock(user->conference_bridge);
}

/*!
 * \internal
 * \brief Suspend MOH for the conference user.
 *
 * \param user Conference user to suspend MOH on.
 *
 * \return Nothing
 */
static void conf_moh_suspend(struct conference_bridge_user *user)
{
	ao2_lock(user->conference_bridge);
	if (user->suspended_moh++ == 0 && user->playing_moh) {
		ast_moh_stop(user->chan);
	}
	ao2_unlock(user->conference_bridge);
}

int conf_handle_inactive_waitmarked(struct conference_bridge_user *cbu)
{
	/* If we have not been quieted play back that they are waiting for the leader */
	if (!ast_test_flag(&cbu->u_profile, USER_OPT_QUIET) && play_prompt_to_user(cbu,
			conf_get_sound(CONF_SOUND_WAIT_FOR_LEADER, cbu->b_profile.sounds))) {
		/* user hungup while the sound was playing */
		return -1;
	}
	return 0;
}

int conf_handle_only_unmarked(struct conference_bridge_user *cbu)
{
	/* If audio prompts have not been quieted or this prompt quieted play it on out */
	if (!ast_test_flag(&cbu->u_profile, USER_OPT_QUIET | USER_OPT_NOONLYPERSON)) {
		if (play_prompt_to_user(cbu,
			conf_get_sound(CONF_SOUND_ONLY_PERSON, cbu->b_profile.sounds))) {
			/* user hungup while the sound was playing */
			return -1;
		}
	}
	return 0;
}

int conf_add_post_join_action(struct conference_bridge_user *cbu, int (*func)(struct conference_bridge_user *cbu))
{
	struct post_join_action *action;
	if (!(action = ast_calloc(1, sizeof(*action)))) {
		return -1;
	}
	action->func = func;
	AST_LIST_INSERT_TAIL(&cbu->post_join_list, action, list);
	return 0;
}


void conf_handle_first_join(struct conference_bridge *conference_bridge)
{
	ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "confbridge:%s", conference_bridge->name);
}

void conf_handle_second_active(struct conference_bridge *conference_bridge)
{
	/* If we are the second participant we may need to stop music on hold on the first */
	struct conference_bridge_user *first_participant = AST_LIST_FIRST(&conference_bridge->active_list);

	if (ast_test_flag(&first_participant->u_profile, USER_OPT_MUSICONHOLD)) {
		conf_moh_stop(first_participant);
	}
	conf_update_user_mute(first_participant);
}

void conf_ended(struct conference_bridge *conference_bridge)
{
	/* Called with a reference to conference_bridge */
	ao2_unlink(conference_bridges, conference_bridge);
	send_conf_end_event(conference_bridge->name);
	ao2_lock(conference_bridge);
	conf_stop_record(conference_bridge);
	ao2_unlock(conference_bridge);
}

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
	struct post_join_action *action;
	struct conference_bridge tmp;
	int max_members_reached = 0;

	ast_copy_string(tmp.name, name, sizeof(tmp.name));

	/* We explictly lock the conference bridges container ourselves so that other callers can not create duplicate conferences at the same */
	ao2_lock(conference_bridges);

	ast_debug(1, "Trying to find conference bridge '%s'\n", name);

	/* Attempt to find an existing conference bridge */
	conference_bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);

	if (conference_bridge && conference_bridge->b_profile.max_members) {
		max_members_reached = conference_bridge->b_profile.max_members > conference_bridge->activeusers ? 0 : 1;
	}

	/* When finding a conference bridge that already exists make sure that it is not locked, and if so that we are not an admin */
	if (conference_bridge && (max_members_reached || conference_bridge->locked) && !ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ADMIN)) {
		ao2_unlock(conference_bridges);
		ao2_ref(conference_bridge, -1);
		ast_debug(1, "Conference '%s' is locked and caller is not an admin\n", name);
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
			ast_log(LOG_ERROR, "Conference '%s' could not be created.\n", name);
			return NULL;
		}

		/* Setup lock for playback channel */
		ast_mutex_init(&conference_bridge->playback_lock);

		/* Setup for the record channel */
		conference_bridge->record_filename = ast_str_create(RECORD_FILENAME_INITIAL_SPACE);
		if (!conference_bridge->record_filename) {
			ao2_ref(conference_bridge, -1);
			ao2_unlock(conference_bridges);
			return NULL;
		}

		/* Setup conference bridge parameters */
		ast_copy_string(conference_bridge->name, name, sizeof(conference_bridge->name));
		conf_bridge_profile_copy(&conference_bridge->b_profile, &conference_bridge_user->b_profile);

		/* Create an actual bridge that will do the audio mixing */
		if (!(conference_bridge->bridge = ast_bridge_new(AST_BRIDGE_CAPABILITY_MULTIMIX, 0))) {
			ao2_ref(conference_bridge, -1);
			conference_bridge = NULL;
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR, "Conference '%s' mixing bridge could not be created.\n", name);
			return NULL;
		}

		/* Set the internal sample rate on the bridge from the bridge profile */
		ast_bridge_set_internal_sample_rate(conference_bridge->bridge, conference_bridge->b_profile.internal_sample_rate);
		/* Set the internal mixing interval on the bridge from the bridge profile */
		ast_bridge_set_mixing_interval(conference_bridge->bridge, conference_bridge->b_profile.mix_interval);

		if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER)) {
			ast_bridge_set_talker_src_video_mode(conference_bridge->bridge);
		}

		/* Link it into the conference bridges container */
		if (!ao2_link(conference_bridges, conference_bridge)) {
			ao2_ref(conference_bridge, -1);
			conference_bridge = NULL;
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR,
				"Conference '%s' could not be added to the conferences list.\n", name);
			return NULL;
		}

		/* Set the initial state to EMPTY */
		conference_bridge->state = CONF_STATE_EMPTY;

		if (ast_test_flag(&conference_bridge->b_profile, BRIDGE_OPT_RECORD_CONFERENCE)) {
			ao2_lock(conference_bridge);
			conf_start_record(conference_bridge);
			ao2_unlock(conference_bridge);
		}

		send_conf_start_event(conference_bridge->name);
		ast_debug(1, "Created conference '%s' and linked to container.\n", name);
	}

	ao2_unlock(conference_bridges);

	/* Setup conference bridge user parameters */
	conference_bridge_user->conference_bridge = conference_bridge;

	ao2_lock(conference_bridge);

	/*
	 * Suspend any MOH until the user actually joins the bridge of
	 * the conference.  This way any pre-join file playback does not
	 * need to worry about MOH.
	 */
	conference_bridge_user->suspended_moh = 1;

	if (handle_conf_user_join(conference_bridge_user)) {
		/* Invalid event, nothing was done, so we don't want to process a leave. */
		ao2_unlock(conference_bridge);
		ao2_ref(conference_bridge, -1);
		return NULL;
	}

	if (ast_check_hangup(conference_bridge_user->chan)) {
		ao2_unlock(conference_bridge);
		leave_conference(conference_bridge_user);
		return NULL;
	}

	ao2_unlock(conference_bridge);

	/* If an announcement is to be played play it */
	if (!ast_strlen_zero(conference_bridge_user->u_profile.announcement)) {
		if (play_prompt_to_user(conference_bridge_user,
			conference_bridge_user->u_profile.announcement)) {
			leave_conference(conference_bridge_user);
			return NULL;
		}
	}

	/* Announce number of users if need be */
	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ANNOUNCEUSERCOUNT)) {
		if (announce_user_count(conference_bridge, conference_bridge_user, 0)) {
			leave_conference(conference_bridge_user);
			return NULL;
		}
	}

	if (ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ANNOUNCEUSERCOUNTALL) &&
		(conference_bridge->activeusers > conference_bridge_user->u_profile.announce_user_count_all_after)) {
		int user_count_res;

		/*
		 * We have to autoservice the new user because he has not quite
		 * joined the conference yet.
		 */
		ast_autoservice_start(conference_bridge_user->chan);
		user_count_res = announce_user_count(conference_bridge, NULL, 0);
		ast_autoservice_stop(conference_bridge_user->chan);
		if (user_count_res) {
			leave_conference(conference_bridge_user);
			return NULL;
		}
	}

	/* Handle post-join actions */
	while ((action = AST_LIST_REMOVE_HEAD(&conference_bridge_user->post_join_list, list))) {
		action->func(conference_bridge_user);
		ast_free(action);
	}

	return conference_bridge;
}

/*!
 * \brief Leave a conference
 *
 * \param user The conference user
 */
static void leave_conference(struct conference_bridge_user *user)
{
	struct post_join_action *action;

	ao2_lock(user->conference_bridge);
	handle_conf_user_leave(user);
	ao2_unlock(user->conference_bridge);

	/* Discard any post-join actions */
	while ((action = AST_LIST_REMOVE_HEAD(&user->post_join_list, list))) {
		ast_free(action);
	}

	/* Done mucking with the conference, huzzah */
	ao2_ref(user->conference_bridge, -1);
	user->conference_bridge = NULL;
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

	ast_channel_internal_bridge_set(conference_bridge->playback_chan, conference_bridge->bridge);

	/* To make sure playback_chan has the same language of that profile */
	ast_channel_language_set(conference_bridge->playback_chan, conference_bridge->b_profile.language);

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

	/* Do not waste resources trying to play files that do not exist */
	if (!ast_strlen_zero(filename) && !sound_file_exists(filename)) {
		return 0;
	}

	ast_mutex_lock(&conference_bridge->playback_lock);
	if (!(conference_bridge->playback_chan)) {
		if (alloc_playback_chan(conference_bridge)) {
			ast_mutex_unlock(&conference_bridge->playback_lock);
			return -1;
		}
		underlying_channel = ast_channel_tech(conference_bridge->playback_chan)->bridged_channel(conference_bridge->playback_chan, NULL);
	} else {
		/* Channel was already available so we just need to add it back into the bridge */
		underlying_channel = ast_channel_tech(conference_bridge->playback_chan)->bridged_channel(conference_bridge->playback_chan, NULL);
		if (ast_bridge_impart(conference_bridge->bridge, underlying_channel, NULL, NULL, 0)) {
			ast_mutex_unlock(&conference_bridge->playback_lock);
			return -1;
		}
	}

	/* The channel is all under our control, in goes the prompt */
	if (!ast_strlen_zero(filename)) {
		ast_stream_and_wait(conference_bridge->playback_chan, filename, "");
	} else if (say_number >= 0) {
		ast_say_number(conference_bridge->playback_chan, say_number, "", ast_channel_language(conference_bridge->playback_chan), NULL);
	}

	ast_debug(1, "Departing underlying channel '%s' from bridge '%p'\n", ast_channel_name(underlying_channel), conference_bridge->bridge);
	ast_bridge_depart(conference_bridge->bridge, underlying_channel);

	ast_mutex_unlock(&conference_bridge->playback_lock);

	return 0;
}

int play_sound_file(struct conference_bridge *conference_bridge, const char *filename)
{
	return play_sound_helper(conference_bridge, filename, -1);
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
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a conference participant has started or stopped talking.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ConfbridgeStart']/managerEventInstance/syntax/parameter[@name='Conference'])" />
				<parameter name="TalkingStatus">
					<enumlist>
						<enum name="on"/>
						<enum name="off"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	ast_manager_event(bridge_channel->chan, EVENT_FLAG_CALL, "ConfbridgeTalking",
	      "Channel: %s\r\n"
	      "Uniqueid: %s\r\n"
	      "Conference: %s\r\n"
	      "TalkingStatus: %s\r\n",
	      ast_channel_name(bridge_channel->chan), ast_channel_uniqueid(bridge_channel->chan), conf_name, talking ? "on" : "off");
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
			ast_channel_language(chan));
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
		 conf_name, ast_channel_uniqueid(user->chan));

	res = ast_play_and_record(user->chan,
		"vm-rec-name",
		user->name_rec_location,
		10,
		"sln",
		&duration,
		NULL,
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

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.conf_name)) {
		ast_log(LOG_WARNING, "%s requires an argument (conference name[,options])\n", app);
		res = -1;
		goto confbridge_cleanup;
	}

	if (strlen(args.conf_name) >= MAX_CONF_NAME) {
		ast_log(LOG_WARNING, "%s does not accept conference names longer than %d\n", app, MAX_CONF_NAME - 1);
		res = -1;
		goto confbridge_cleanup;
	}

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
			res = -1;
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
			res = -1;
			goto confbridge_cleanup;
		}
		ast_bridge_features_set_talk_detector(&conference_bridge_user.features,
			conf_handle_talker_cb,
			conf_handle_talker_destructor,
			conf_name);
	}

	/* If the caller should be joined already muted, set the flag before we join. */
	if (ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_STARTMUTED)) {
		/* Set user level mute request. */
		conference_bridge_user.muted = 1;
	}

	/* Look for a conference bridge matching the provided name */
	if (!(conference_bridge = join_conference_bridge(args.conf_name, &conference_bridge_user))) {
		res = -1;
		goto confbridge_cleanup;
	}

	/* Keep a copy of volume adjustments so we can restore them later if need be */
	volume_adjustments[0] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_READ);
	volume_adjustments[1] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_WRITE);

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

		ast_stream_and_wait(chan, join_sound, "");
		ast_autoservice_start(chan);
		play_sound_file(conference_bridge, join_sound);
		ast_autoservice_stop(chan);
	}

	/* See if we need to automatically set this user as a video source or not */
	handle_video_on_join(conference_bridge, conference_bridge_user.chan, ast_test_flag(&conference_bridge_user.u_profile, USER_OPT_MARKEDUSER));

	conf_moh_unsuspend(&conference_bridge_user);

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
		leave_conference(&conference_bridge_user);
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
	leave_conference(&conference_bridge_user);
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
	int mute;

	/* Toggle user level mute request. */
	mute = !conference_bridge_user->muted;
	conference_bridge_user->muted = mute;

	conf_update_user_mute(conference_bridge_user);
	ast_test_suite_event_notify("CONF_MUTE",
		"Message: participant %s %s\r\n"
		"Conference: %s\r\n"
		"Channel: %s",
		ast_channel_name(chan),
		mute ? "muted" : "unmuted",
		conference_bridge_user->b_profile.name,
		ast_channel_name(chan));

	return play_file(chan, (mute ?
		conf_get_sound(CONF_SOUND_MUTED, conference_bridge_user->b_profile.sounds) :
		conf_get_sound(CONF_SOUND_UNMUTED, conference_bridge_user->b_profile.sounds)), 1) < 0;
}

static int action_toggle_mute_participants(struct conference_bridge *conference_bridge, struct conference_bridge_user *conference_bridge_user)
{
	struct conference_bridge_user *participant = NULL;
	const char *sound_to_play;
	int mute;

	ao2_lock(conference_bridge);

	/* Toggle bridge level mute request. */
	mute = !conference_bridge->muted;
	conference_bridge->muted = mute;

	AST_LIST_TRAVERSE(&conference_bridge->active_list, participant, list) {
		if (!ast_test_flag(&participant->u_profile, USER_OPT_ADMIN)) {
			/* Set user level to bridge level mute request. */
			participant->muted = mute;
			conf_update_user_mute(participant);
		}
	}

	ao2_unlock(conference_bridge);

	sound_to_play = conf_get_sound((mute ? CONF_SOUND_PARTICIPANTS_MUTED : CONF_SOUND_PARTICIPANTS_UNMUTED),
		conference_bridge_user->b_profile.sounds);

	/* The host needs to hear it seperately, as they don't get the audio from play_sound_helper */
	ast_stream_and_wait(conference_bridge_user->chan, sound_to_play, "");

	/* Announce to the group that all participants are muted */
	ast_autoservice_start(conference_bridge_user->chan);
	play_sound_helper(conference_bridge, sound_to_play, 0);
	ast_autoservice_stop(conference_bridge_user->chan);

	return 0;
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
		if (ast_streamfile(bridge_channel->chan, file, ast_channel_language(bridge_channel->chan))) {
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
	struct conference_bridge_user *conference_bridge_user)
{
	struct conference_bridge_user *last_participant = NULL;
	int isadmin = ast_test_flag(&conference_bridge_user->u_profile, USER_OPT_ADMIN);

	if (!isadmin) {
		play_file(conference_bridge_user->chan,
			conf_get_sound(CONF_SOUND_ERROR_MENU, conference_bridge_user->b_profile.sounds), 1);
		ast_log(LOG_WARNING, "Only admin users can use the kick_last menu action. Channel %s of conf %s is not an admin.\n",
			ast_channel_name(conference_bridge_user->chan),
			conference_bridge->name);
		return -1;
	}

	ao2_lock(conference_bridge);
	if (((last_participant = AST_LIST_LAST(&conference_bridge->active_list)) == conference_bridge_user)
		|| (ast_test_flag(&last_participant->u_profile, USER_OPT_ADMIN))) {
		ao2_unlock(conference_bridge);
		play_file(conference_bridge_user->chan,
			conf_get_sound(CONF_SOUND_ERROR_MENU, conference_bridge_user->b_profile.sounds), 1);
	} else if (last_participant && !last_participant->kicked) {
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
	exten = ast_strdupa(ast_channel_exten(bridge_channel->chan));
	context = ast_strdupa(ast_channel_context(bridge_channel->chan));
	priority = ast_channel_priority(bridge_channel->chan);
	pbx = ast_channel_pbx(bridge_channel->chan);
	ast_channel_pbx_set(bridge_channel->chan, NULL);

	/*set new*/
	ast_channel_exten_set(bridge_channel->chan, menu_action->data.dialplan_args.exten);
	ast_channel_context_set(bridge_channel->chan, menu_action->data.dialplan_args.context);
	ast_channel_priority_set(bridge_channel->chan, menu_action->data.dialplan_args.priority);

	ast_channel_unlock(bridge_channel->chan);

	/*execute*/
	res = ast_pbx_run_args(bridge_channel->chan, &args);

	/*restore*/
	ast_channel_lock(bridge_channel->chan);

	ast_channel_exten_set(bridge_channel->chan, exten);
	ast_channel_context_set(bridge_channel->chan, context);
	ast_channel_priority_set(bridge_channel->chan, priority);
	ast_channel_pbx_set(bridge_channel->chan, pbx);

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
		case MENU_ACTION_ADMIN_TOGGLE_MUTE_PARTICIPANTS:
			if (!isadmin) {
				break;
			}
			action_toggle_mute_participants(conference_bridge, conference_bridge_user);
			break;
		case MENU_ACTION_PARTICIPANT_COUNT:
			announce_user_count(conference_bridge, conference_bridge_user, 1);
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
			res |= play_file(bridge_channel->chan,
				(conference_bridge->locked ?
				conf_get_sound(CONF_SOUND_LOCKED_NOW, conference_bridge_user->b_profile.sounds) :
				conf_get_sound(CONF_SOUND_UNLOCKED_NOW, conference_bridge_user->b_profile.sounds)), 1) < 0;
			break;
		case MENU_ACTION_ADMIN_KICK_LAST:
			res |= action_kick_last(conference_bridge, conference_bridge_user);
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
	/* See if music on hold is playing */
	conf_moh_suspend(conference_bridge_user);

	/* execute the list of actions associated with this menu entry */
	execute_menu_entry(conference_bridge_user->conference_bridge, conference_bridge_user, bridge_channel, menu_entry, menu);

	/* See if music on hold needs to be started back up again */
	conf_moh_unsuspend(conference_bridge_user);

	return 0;
}

static int kick_conference_participant(struct conference_bridge *bridge, const char *channel)
{
	struct conference_bridge_user *participant = NULL;

	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
		if (!strcasecmp(ast_channel_name(participant->chan), channel) && !participant->kicked) {
			participant->kicked = 1;
			ast_bridge_remove(bridge->bridge, participant->chan);
			ao2_unlock(bridge);
			return 0;
		}
	}
	AST_LIST_TRAVERSE(&bridge->waiting_list, participant, list) {
		if (!strcasecmp(ast_channel_name(participant->chan), channel) && !participant->kicked) {
			participant->kicked = 1;
			ast_bridge_remove(bridge->bridge, participant->chan);
			ao2_unlock(bridge);
			return 0;
		}
	}
	ao2_unlock(bridge);

	return -1;
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

static char *complete_confbridge_participant(const char *bridge_name, const char *line, const char *word, int pos, int state)
{
	int which = 0;
	RAII_VAR(struct conference_bridge *, bridge, NULL, ao2_cleanup);
	struct conference_bridge tmp;
	struct conference_bridge_user *participant;
	char *res = NULL;
	int wordlen = strlen(word);

	ast_copy_string(tmp.name, bridge_name, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		return NULL;
	}

	ao2_lock(bridge);
	AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
		if (!strncasecmp(ast_channel_name(participant->chan), word, wordlen) && ++which > state) {
			res = ast_strdup(ast_channel_name(participant->chan));
			ao2_unlock(bridge);
			return res;
		}
	}

	AST_LIST_TRAVERSE(&bridge->waiting_list, participant, list) {
		if (!strncasecmp(ast_channel_name(participant->chan), word, wordlen) && ++which > state) {
			res = ast_strdup(ast_channel_name(participant->chan));
			ao2_unlock(bridge);
			return res;
		}
	}
	ao2_unlock(bridge);

	return NULL;
}

static char *handle_cli_confbridge_kick(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	int not_found;

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
		if (a->pos == 3) {
			return complete_confbridge_participant(a->argv[2], a->line, a->word, a->pos, a->n);
		}
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
	not_found = kick_conference_participant(bridge, a->argv[3]);
	ao2_ref(bridge, -1);
	if (not_found) {
		ast_cli(a->fd, "No participant named '%s' found!\n", a->argv[3]);
		return CLI_SUCCESS;
	}
	ast_cli(a->fd, "Participant '%s' kicked out of conference '%s'\n", a->argv[3], a->argv[2]);
	return CLI_SUCCESS;
}

static void handle_cli_confbridge_list_item(struct ast_cli_args *a, struct conference_bridge_user *participant)
{
	ast_cli(a->fd, "%-30s %-16s %-16s %-16s %-16s %s\n",
		ast_channel_name(participant->chan),
		participant->u_profile.name,
		participant->b_profile.name,
		participant->menu_name,
		S_COR(ast_channel_caller(participant->chan)->id.number.valid,
			ast_channel_caller(participant->chan)->id.number.str, "<unknown>"),
		AST_CLI_YESNO(participant->muted));
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
			ast_cli(a->fd, "%-32s %6u %6u %s\n", bridge->name, bridge->activeusers + bridge->waitingusers, bridge->markedusers, (bridge->locked ? "locked" : "unlocked"));
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
		ast_cli(a->fd, "Channel                        User Profile     Bridge Profile   Menu             CallerID         Muted\n");
		ast_cli(a->fd, "============================== ================ ================ ================ ================ =====\n");
		ao2_lock(bridge);
		AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
			handle_cli_confbridge_list_item(a, participant);
		}
		AST_LIST_TRAVERSE(&bridge->waiting_list, participant, list) {
			handle_cli_confbridge_list_item(a, participant);
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
	ast_test_suite_event_notify("CONF_LOCK", "Message: conference %s\r\nConference: %s", bridge->locked ? "locked" : "unlocked", bridge->b_profile.name);
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
	AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
		if (!strncmp(user, ast_channel_name(participant->chan), strlen(user))) {
			break;
		}
	}
	if (!participant) {
		/* user is not in the active list so check the waiting list as well */
		AST_LIST_TRAVERSE(&bridge->waiting_list, participant, list) {
			if (!strncmp(user, ast_channel_name(participant->chan), strlen(user))) {
				break;
			}
		}
	}
	if (participant) {
		/* Set user level mute request. */
		participant->muted = mute ? 1 : 0;

		conf_update_user_mute(participant);
		ast_test_suite_event_notify("CONF_MUTE",
			"Message: participant %s %s\r\n"
			"Conference: %s\r\n"
			"Channel: %s",
			ast_channel_name(participant->chan),
			mute ? "muted" : "unmuted",
			bridge->b_profile.name,
			ast_channel_name(participant->chan));
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
			"Usage: confbridge mute <conference> <channel>\n"
			"       Mute a channel in a conference.\n"
			"       If the specified channel is a prefix,\n"
			"       the action will be taken on the first\n"
			"       matching channel.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		if (a->pos == 3) {
			return complete_confbridge_participant(a->argv[2], a->line, a->word, a->pos, a->n);
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
			"Usage: confbridge unmute <conference> <channel>\n"
			"       Unmute a channel in a conference.\n"
			"       If the specified channel is a prefix,\n"
			"       the action will be taken on the first\n"
			"       matching channel.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		if (a->pos == 3) {
			return complete_confbridge_participant(a->argv[2], a->line, a->word, a->pos, a->n);
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
			"Usage: confbridge lock <conference>\n"
			"       Lock a conference. While locked, no new non-admins\n"
			"       may join the conference.\n";
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
			"Usage: confbridge unlock <conference>\n"
			"       Unlock a previously locked conference.\n";
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
	ao2_lock(bridge);
	if (conf_is_recording(bridge)) {
		ast_cli(a->fd, "Conference is already being recorded.\n");
		ao2_unlock(bridge);
		ao2_ref(bridge, -1);
		return CLI_SUCCESS;
	}
	if (!ast_strlen_zero(rec_file)) {
		ast_copy_string(bridge->b_profile.rec_file, rec_file, sizeof(bridge->b_profile.rec_file));
	}

	if (conf_start_record(bridge)) {
		ast_cli(a->fd, "Could not start recording due to internal error.\n");
		ao2_unlock(bridge);
		ao2_ref(bridge, -1);
		return CLI_FAILURE;
	}
	ao2_unlock(bridge);

	ast_cli(a->fd, "Recording started\n");
	ao2_ref(bridge, -1);
	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_stop_record(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	int ret;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge record stop";
		e->usage =
			"Usage: confbridge record stop <conference>\n"
			"       Stop a previously started recording.\n";
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
	ao2_lock(bridge);
	ret = conf_stop_record(bridge);
	ao2_unlock(bridge);
	ast_cli(a->fd, "Recording %sstopped.\n", ret ? "could not be " : "");
	ao2_ref(bridge, -1);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_confbridge[] = {
	AST_CLI_DEFINE(handle_cli_confbridge_list, "List conference bridges and participants."),
	AST_CLI_DEFINE(handle_cli_confbridge_kick, "Kick participants out of conference bridges."),
	AST_CLI_DEFINE(handle_cli_confbridge_mute, "Mute a participant."),
	AST_CLI_DEFINE(handle_cli_confbridge_unmute, "Unmute a participant."),
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

static void action_confbridgelist_item(struct mansession *s, const char *id_text, struct conference_bridge *bridge, struct conference_bridge_user *participant)
{
	astman_append(s,
		"Event: ConfbridgeList\r\n"
		"%s"
		"Conference: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Channel: %s\r\n"
		"Admin: %s\r\n"
		"MarkedUser: %s\r\n"
		"Muted: %s\r\n"
		"\r\n",
		id_text,
		bridge->name,
		S_COR(ast_channel_caller(participant->chan)->id.number.valid, ast_channel_caller(participant->chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(participant->chan)->id.name.valid, ast_channel_caller(participant->chan)->id.name.str, "<no name>"),
		ast_channel_name(participant->chan),
		ast_test_flag(&participant->u_profile, USER_OPT_ADMIN) ? "Yes" : "No",
		ast_test_flag(&participant->u_profile, USER_OPT_MARKEDUSER) ? "Yes" : "No",
		participant->muted ? "Yes" : "No");
}

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
	AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
		total++;
		action_confbridgelist_item(s, id_text, bridge, participant);
	}
	AST_LIST_TRAVERSE(&bridge->waiting_list, participant, list) {
		total++;
		action_confbridgelist_item(s, id_text, bridge, participant);
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
		"Parties: %u\r\n"
		"Marked: %u\r\n"
		"Locked: %s\r\n"
		"\r\n",
		id_text,
		bridge->name,
		bridge->activeusers + bridge->waitingusers,
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
	struct conference_bridge *bridge = NULL;
	struct conference_bridge tmp;
	int found;

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

	found = !kick_conference_participant(bridge, channel);
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

	ao2_lock(bridge);
	if (conf_is_recording(bridge)) {
		astman_send_error(s, m, "Conference is already being recorded.");
		ao2_unlock(bridge);
		ao2_ref(bridge, -1);
		return 0;
	}

	if (!ast_strlen_zero(recordfile)) {
		ast_copy_string(bridge->b_profile.rec_file, recordfile, sizeof(bridge->b_profile.rec_file));
	}

	if (conf_start_record(bridge)) {
		astman_send_error(s, m, "Internal error starting conference recording.");
		ao2_unlock(bridge);
		ao2_ref(bridge, -1);
		return 0;
	}
	ao2_unlock(bridge);

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

	ao2_lock(bridge);
	if (conf_stop_record(bridge)) {
		ao2_unlock(bridge);
		astman_send_error(s, m, "Internal error while stopping recording.");
		ao2_ref(bridge, -1);
		return 0;
	}
	ao2_unlock(bridge);

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
	AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
		if (!strncmp(channel, ast_channel_name(participant->chan), strlen(channel))) {
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
		snprintf(buf, len, "0");
		return 0;
	}
	ast_copy_string(tmp.name, args.confno, sizeof(tmp.name));
	bridge = ao2_find(conference_bridges, &tmp, OBJ_POINTER);
	if (!bridge) {
		snprintf(buf, len, "0");
		return 0;
	}

	/* get the correct count for the type requested */
	ao2_lock(bridge);
	if (!strncasecmp(args.type, "parties", 7)) {
		AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
			count++;
		}
		AST_LIST_TRAVERSE(&bridge->waiting_list, participant, list) {
			count++;
		}
	} else if (!strncasecmp(args.type, "admins", 6)) {
		AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
			if (ast_test_flag(&participant->u_profile, USER_OPT_ADMIN)) {
				count++;
			}
		}
	} else if (!strncasecmp(args.type, "marked", 6)) {
		AST_LIST_TRAVERSE(&bridge->active_list, participant, list) {
			if (ast_test_flag(&participant->u_profile, USER_OPT_MARKEDUSER)) {
				count++;
			}
		}
	} else if (!strncasecmp(args.type, "locked", 6)) {
		count = bridge->locked;
	} else {
		ast_log(LOG_ERROR, "Invalid keyword '%s' passed to CONFBRIDGE_INFO.  Should be one of: "
			"parties, admins, marked, or locked.\n", args.type);
	}
	snprintf(buf, len, "%d", count);
	ao2_unlock(bridge);
	ao2_ref(bridge, -1);
	return 0;
}

void conf_add_user_active(struct conference_bridge *conference_bridge, struct conference_bridge_user *cbu)
{
	AST_LIST_INSERT_TAIL(&conference_bridge->active_list, cbu, list);
	conference_bridge->activeusers++;
}

void conf_add_user_marked(struct conference_bridge *conference_bridge, struct conference_bridge_user *cbu)
{
	AST_LIST_INSERT_TAIL(&conference_bridge->active_list, cbu, list);
	conference_bridge->activeusers++;
	conference_bridge->markedusers++;
}

void conf_add_user_waiting(struct conference_bridge *conference_bridge, struct conference_bridge_user *cbu)
{
	AST_LIST_INSERT_TAIL(&conference_bridge->waiting_list, cbu, list);
	conference_bridge->waitingusers++;
}

void conf_remove_user_active(struct conference_bridge *conference_bridge, struct conference_bridge_user *cbu)
{
	AST_LIST_REMOVE(&conference_bridge->active_list, cbu, list);
	conference_bridge->activeusers--;
}

void conf_remove_user_marked(struct conference_bridge *conference_bridge, struct conference_bridge_user *cbu)
{
	AST_LIST_REMOVE(&conference_bridge->active_list, cbu, list);
	conference_bridge->activeusers--;
	conference_bridge->markedusers--;
}

void conf_mute_only_active(struct conference_bridge *conference_bridge)
{
	struct conference_bridge_user *only_participant = AST_LIST_FIRST(&conference_bridge->active_list);

	/* Turn on MOH if the single participant is set up for it */
	if (ast_test_flag(&only_participant->u_profile, USER_OPT_MUSICONHOLD)) {
		conf_moh_start(only_participant);
	}
	conf_update_user_mute(only_participant);
}

void conf_remove_user_waiting(struct conference_bridge *conference_bridge, struct conference_bridge_user *cbu)
{
	AST_LIST_REMOVE(&conference_bridge->waiting_list, cbu, list);
	conference_bridge->waitingusers--;
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
	res |= ast_manager_unregister("ConfbridgeSetSingleVideoSrc");

	return res;
}

/*! \brief Called when module is being loaded */
static int load_module(void)
{
	int res = 0;

	if (conf_load_config()) {
		ast_log(LOG_ERROR, "Unable to load config. Not loading module.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if ((ast_custom_function_register_escalating(&confbridge_function, AST_CFE_WRITE))) {
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
	res |= ast_manager_register_xml("ConfbridgeStartRecord", EVENT_FLAG_SYSTEM, action_confbridgestartrecord);
	res |= ast_manager_register_xml("ConfbridgeStopRecord", EVENT_FLAG_CALL, action_confbridgestoprecord);
	res |= ast_manager_register_xml("ConfbridgeSetSingleVideoSrc", EVENT_FLAG_CALL, action_confbridgesetsinglevideosrc);
	if (res) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	return conf_reload_config();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Conference Bridge Application",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);
