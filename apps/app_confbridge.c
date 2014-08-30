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

/*! \li \ref app_confbridge.c uses the configuration file \ref confbridge.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page confbridge.conf confbridge.conf
 * \verbinclude confbridge.conf.sample
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
#include "asterisk/bridge.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/audiohook.h"
#include "asterisk/astobj2.h"
#include "confbridge/include/confbridge.h"
#include "asterisk/paths.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/json.h"

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
				<para>If this parameter is "all", all channels will be muted.</para>
				<para>If this parameter is "participants", all non-admin channels will be muted.</para>
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
				<para>If this parameter is "all", all channels will be unmuted.</para>
				<para>If this parameter is "participants", all non-admin channels will be unmuted.</para>
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
			<parameter name="Channel" required="true" >
				<para>If this parameter is "all", all channels will be kicked from the conference.</para>
				<para>If this parameter is "participants", all non-admin channels will be kicked from the conference.</para>
			</parameter>
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

/* Number of buckets our conference bridges container can have */
#define CONFERENCE_BRIDGE_BUCKETS 53

enum {
	CONF_RECORD_EXIT = 0,
	CONF_RECORD_START,
	CONF_RECORD_STOP,
};

/*! \brief Container to hold all conference bridges in progress */
struct ao2_container *conference_bridges;

static void leave_conference(struct confbridge_user *user);
static int play_sound_number(struct confbridge_conference *conference, int say_number);
static int execute_menu_entry(struct confbridge_conference *conference,
	struct confbridge_user *user,
	struct ast_bridge_channel *bridge_channel,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu);

/*! \brief Hashing function used for conference bridges container */
static int conference_bridge_hash_cb(const void *obj, const int flags)
{
	const struct confbridge_conference *conference = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		name = conference->name;
		/* Fall through */
	case OBJ_KEY:
		hash = ast_str_case_hash(name);
		break;
	case OBJ_PARTIAL_KEY:
		/* Should never happen in hash callback. */
		ast_assert(0);
		hash = 0;
		break;
	}
	return hash;
}

/*! \brief Comparison function used for conference bridges container */
static int conference_bridge_cmp_cb(void *obj, void *arg, int flags)
{
	const struct confbridge_conference *left = obj;
	const struct confbridge_conference *right = arg;
	const char *right_name = arg;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_name = right->name;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcasecmp(left->name, right_name);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncasecmp(left->name, right_name, strlen(right_name));
		break;
	}
	return cmp ? 0 : CMP_MATCH;
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

static void send_conf_stasis(struct confbridge_conference *conference, struct ast_channel *chan,
	struct stasis_message_type *type, struct ast_json *extras, int channel_topic)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);

	json_object = ast_json_pack("{s: s}",
		"conference", conference->name);
	if (!json_object) {
		return;
	}

	if (extras) {
		ast_json_object_update(json_object, extras);
	}

	ast_bridge_lock(conference->bridge);
	msg = ast_bridge_blob_create(type,
		conference->bridge,
		chan,
		json_object);
	ast_bridge_unlock(conference->bridge);
	if (!msg) {
		return;
	}

	if (channel_topic) {
		stasis_publish(ast_channel_topic(chan), msg);
	} else {
		stasis_publish(ast_bridge_topic(conference->bridge), msg);
	}
}

static void send_conf_start_event(struct confbridge_conference *conference)
{
	send_conf_stasis(conference, NULL, confbridge_start_type(), NULL, 0);
}

static void send_conf_end_event(struct confbridge_conference *conference)
{
	send_conf_stasis(conference, NULL, confbridge_end_type(), NULL, 0);
}

static void send_join_event(struct confbridge_user *user, struct confbridge_conference *conference)
{
	struct ast_json *json_object;

	json_object = ast_json_pack("{s: b}",
		"admin", ast_test_flag(&user->u_profile, USER_OPT_ADMIN)
	);
	if (!json_object) {
		return;
	}
	send_conf_stasis(conference, user->chan, confbridge_join_type(), json_object, 0);
	ast_json_unref(json_object);
}

static void send_leave_event(struct confbridge_user *user, struct confbridge_conference *conference)
{
	struct ast_json *json_object;

	json_object = ast_json_pack("{s: b}",
		"admin", ast_test_flag(&user->u_profile, USER_OPT_ADMIN)
	);
	if (!json_object) {
		return;
	}
	send_conf_stasis(conference, user->chan, confbridge_leave_type(), json_object, 0);
	ast_json_unref(json_object);
}

static void send_start_record_event(struct confbridge_conference *conference)
{
	send_conf_stasis(conference, NULL, confbridge_start_record_type(), NULL, 0);
}

static void send_stop_record_event(struct confbridge_conference *conference)
{
	send_conf_stasis(conference, NULL, confbridge_stop_record_type(), NULL, 0);
}

static void send_mute_event(struct confbridge_user *user, struct confbridge_conference *conference)
{
	struct ast_json *json_object;

	json_object = ast_json_pack("{s: b}",
		"admin", ast_test_flag(&user->u_profile, USER_OPT_ADMIN)
	);
	if (!json_object) {
		return;
	}
	send_conf_stasis(conference, user->chan, confbridge_mute_type(), json_object, 1);
	ast_json_unref(json_object);
}

static void send_unmute_event(struct confbridge_user *user, struct confbridge_conference *conference)
{
	struct ast_json *json_object;

	json_object = ast_json_pack("{s: b}",
		"admin", ast_test_flag(&user->u_profile, USER_OPT_ADMIN)
	);
	if (!json_object) {
		return;
	}
	send_conf_stasis(conference, user->chan, confbridge_unmute_type(), json_object, 1);
	ast_json_unref(json_object);
}

static void set_rec_filename(struct confbridge_conference *conference, struct ast_str **filename, int is_new)
{
	char *rec_file = conference->b_profile.rec_file;
	time_t now;
	char *ext;

	if (ast_str_strlen(*filename) && ast_test_flag(&conference->b_profile, BRIDGE_OPT_RECORD_FILE_APPEND) && !is_new) {
		    return;
	}

	time(&now);

	ast_str_reset(*filename);
	if (ast_strlen_zero(rec_file)) {
		ast_str_set(filename, 0, "confbridge-%s-%u.wav", conference->name, (unsigned int)now);
	} else {
		/* insert time before file extension */
		ext = strrchr(rec_file, '.');
		if (ext) {
			ast_str_set_substr(filename, 0, rec_file, ext - rec_file);
			ast_str_append(filename, 0, "-%u%s", (unsigned int)now, ext);
		} else {
			ast_str_set(filename, 0, "%s-%u", rec_file, (unsigned int)now);
		}
	}

	if (ast_test_flag(&conference->b_profile, BRIDGE_OPT_RECORD_FILE_APPEND)) {
		ast_str_append(filename, 0, ",a");
	}
}

static int is_new_rec_file(const char *rec_file, struct ast_str **orig_rec_file)
{
	if (!ast_strlen_zero(rec_file)) {
		if (!*orig_rec_file) {
			*orig_rec_file = ast_str_create(PATH_MAX);
		}

		if (strcmp(ast_str_buffer(*orig_rec_file), rec_file)) {
			ast_str_set(orig_rec_file, 0, "%s", rec_file);
			return 1;
		}
	}
	return 0;
}

static void *record_thread(void *obj)
{
	struct confbridge_conference *conference = obj;
	struct ast_app *mixmonapp = pbx_findapp("MixMonitor");
	struct ast_channel *chan;
	struct ast_str *filename = ast_str_alloca(PATH_MAX);
	struct ast_str *orig_rec_file = NULL;
	struct ast_bridge_features features;

	ast_mutex_lock(&conference->record_lock);
	if (!mixmonapp) {
		ast_log(LOG_WARNING, "Can not record ConfBridge, MixMonitor app is not installed\n");
		conference->record_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&conference->record_lock);
		ao2_ref(conference, -1);
		return NULL;
	}
	if (ast_bridge_features_init(&features)) {
		ast_bridge_features_cleanup(&features);
		conference->record_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&conference->record_lock);
		ao2_ref(conference, -1);
		return NULL;
	}
	ast_set_flag(&features.feature_flags, AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE);

	/* XXX If we get an EXIT right here, START will essentially be a no-op */
	while (conference->record_state != CONF_RECORD_EXIT) {
		set_rec_filename(conference, &filename,
			is_new_rec_file(conference->b_profile.rec_file, &orig_rec_file));
		chan = ast_channel_ref(conference->record_chan);
		ast_answer(chan);
		pbx_exec(chan, mixmonapp, ast_str_buffer(filename));
		ast_bridge_join(conference->bridge, chan, NULL, &features, NULL, 0);

		ast_hangup(chan); /* This will eat this thread's reference to the channel as well */
		/* STOP has been called. Wait for either a START or an EXIT */
		ast_cond_wait(&conference->record_cond, &conference->record_lock);
	}
	ast_bridge_features_cleanup(&features);
	ast_free(orig_rec_file);
	ast_mutex_unlock(&conference->record_lock);
	ao2_ref(conference, -1);
	return NULL;
}

/*! \brief Returns whether or not conference is being recorded.
 * \param conference The bridge to check for recording
 * \retval 1, conference is recording.
 * \retval 0, conference is NOT recording.
 */
static int conf_is_recording(struct confbridge_conference *conference)
{
	return conference->record_state == CONF_RECORD_START;
}

/*! \brief Stop recording a conference bridge
 * \internal
 * \param conference The conference bridge on which to stop the recording
 * \retval -1 Failure
 * \retval 0 Success
 */
static int conf_stop_record(struct confbridge_conference *conference)
{
	struct ast_channel *chan;
	if (conference->record_thread == AST_PTHREADT_NULL || !conf_is_recording(conference)) {
		return -1;
	}
	conference->record_state = CONF_RECORD_STOP;
	chan = ast_channel_ref(conference->record_chan);
	ast_bridge_remove(conference->bridge, chan);
	ast_queue_frame(chan, &ast_null_frame);
	chan = ast_channel_unref(chan);
	ast_test_suite_event_notify("CONF_STOP_RECORD", "Message: stopped conference recording channel\r\nConference: %s", conference->b_profile.name);
	send_stop_record_event(conference);

	return 0;
}

/*!
 * \internal
 * \brief Stops the confbridge recording thread.
 *
 * \note Must be called with the conference locked
 */
static int conf_stop_record_thread(struct confbridge_conference *conference)
{
	if (conference->record_thread == AST_PTHREADT_NULL) {
		return -1;
	}
	conf_stop_record(conference);

	ast_mutex_lock(&conference->record_lock);
	conference->record_state = CONF_RECORD_EXIT;
	ast_cond_signal(&conference->record_cond);
	ast_mutex_unlock(&conference->record_lock);

	pthread_join(conference->record_thread, NULL);
	conference->record_thread = AST_PTHREADT_NULL;

	/* this is the reference given to the channel during the channel alloc */
	if (conference->record_chan) {
		conference->record_chan = ast_channel_unref(conference->record_chan);
	}

	return 0;
}

/*! \brief Start recording the conference
 * \internal
 * \note The conference must be locked when calling this function
 * \param conference The conference bridge to start recording
 * \retval 0 success
 * \rteval non-zero failure
 */
static int conf_start_record(struct confbridge_conference *conference)
{
	struct ast_format_cap *cap;
	struct ast_format format;

	if (conference->record_state != CONF_RECORD_STOP) {
		return -1;
	}

	if (!pbx_findapp("MixMonitor")) {
		ast_log(LOG_WARNING, "Can not record ConfBridge, MixMonitor app is not installed\n");
		return -1;
	}

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK);
	if (!cap) {
		return -1;
	}

	ast_format_cap_add(cap, ast_format_set(&format, AST_FORMAT_SLINEAR, 0));

	conference->record_chan = ast_request("CBRec", cap, NULL, NULL,
		conference->name, NULL);
	cap = ast_format_cap_destroy(cap);
	if (!conference->record_chan) {
		return -1;
	}

	conference->record_state = CONF_RECORD_START;
	ast_mutex_lock(&conference->record_lock);
	ast_cond_signal(&conference->record_cond);
	ast_mutex_unlock(&conference->record_lock);
	ast_test_suite_event_notify("CONF_START_RECORD", "Message: started conference recording channel\r\nConference: %s", conference->b_profile.name);
	send_start_record_event(conference);

	return 0;
}

/*! \brief Start the recording thread on a conference bridge
 * \internal
 * \param conference The conference bridge on which to start the recording thread
 * \retval 0 success
 * \retval -1 failure
 */
static int start_conf_record_thread(struct confbridge_conference *conference)
{
	conf_start_record(conference);

	/*
	 * if the thread has already been started, don't start another
	 */
	if (conference->record_thread != AST_PTHREADT_NULL) {
		return 0;
	}

	ao2_ref(conference, +1); /* give the record thread a ref */

	if (ast_pthread_create_background(&conference->record_thread, NULL, record_thread, conference)) {
		ast_log(LOG_WARNING, "Failed to create recording channel for conference %s\n", conference->name);
		ao2_ref(conference, -1); /* error so remove ref */
		return -1;
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
 * \param conference Conference bridge to peek at
 * \param user Optional Caller
 *
 * \note if caller is NULL, the announcment will be sent to all participants in the conference.
 * \return Returns 0 on success, -1 if the user hung up
 */
static int announce_user_count(struct confbridge_conference *conference, struct confbridge_user *user)
{
	const char *other_in_party = conf_get_sound(CONF_SOUND_OTHER_IN_PARTY, conference->b_profile.sounds);
	const char *only_one = conf_get_sound(CONF_SOUND_ONLY_ONE, conference->b_profile.sounds);
	const char *there_are = conf_get_sound(CONF_SOUND_THERE_ARE, conference->b_profile.sounds);

	if (conference->activeusers <= 1) {
		/* Awww we are the only person in the conference bridge OR we only have waitmarked users */
		return 0;
	} else if (conference->activeusers == 2) {
		if (user) {
			/* Eep, there is one other person */
			if (ast_stream_and_wait(user->chan,
				only_one,
				"")) {
				return -1;
			}
		} else {
			play_sound_file(conference, only_one);
		}
	} else {
		/* Alas multiple others in here */
		if (user) {
			if (ast_stream_and_wait(user->chan,
				there_are,
				"")) {
				return -1;
			}
			if (ast_say_number(user->chan, conference->activeusers - 1, "", ast_channel_language(user->chan), NULL)) {
				return -1;
			}
			if (ast_stream_and_wait(user->chan,
				other_in_party,
				"")) {
				return -1;
			}
		} else if (sound_file_exists(there_are) && sound_file_exists(other_in_party)) {
			play_sound_file(conference, there_are);
			play_sound_number(conference, conference->activeusers - 1);
			play_sound_file(conference, other_in_party);
		}
	}
	return 0;
}

/*!
 * \brief Play back an audio file to a channel
 *
 * \param user User to play audio prompt to
 * \param filename Prompt to play
 *
 * \return Returns 0 on success, -1 if the user hung up
 * \note Generally this should be called when the conference is unlocked to avoid blocking
 * the entire conference while the sound is played. But don't unlock the conference bridge
 * in the middle of a state transition.
 */
static int play_prompt_to_user(struct confbridge_user *user, const char *filename)
{
	return ast_stream_and_wait(user->chan, filename, "");
}

static void handle_video_on_join(struct confbridge_conference *conference, struct ast_channel *chan, int marked)
{
	/* Right now, only marked users are automatically set as the single src of video.*/
	if (!marked) {
		return;
	}

	if (ast_test_flag(&conference->b_profile, BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED)) {
		int set = 1;
		struct confbridge_user *user = NULL;

		ao2_lock(conference);
		/* see if anyone is already the video src */
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			if (user->chan == chan) {
				continue;
			}
			if (ast_bridge_is_video_src(conference->bridge, user->chan)) {
				set = 0;
				break;
			}
		}
		ao2_unlock(conference);
		if (set) {
			ast_bridge_set_single_src_video_mode(conference->bridge, chan);
		}
	} else if (ast_test_flag(&conference->b_profile, BRIDGE_OPT_VIDEO_SRC_LAST_MARKED)) {
		/* we joined and are video capable, we override anyone else that may have already been the video feed */
		ast_bridge_set_single_src_video_mode(conference->bridge, chan);
	}
}

static void handle_video_on_exit(struct confbridge_conference *conference, struct ast_channel *chan)
{
	struct confbridge_user *user = NULL;

	/* if this isn't a video source, nothing to update */
	if (!ast_bridge_is_video_src(conference->bridge, chan)) {
		return;
	}

	ast_bridge_remove_video_src(conference->bridge, chan);

	/* If in follow talker mode, make sure to restore this mode on the
	 * bridge when a source is removed.  It is possible this channel was
	 * only set temporarily as a video source by an AMI or DTMF action. */
	if (ast_test_flag(&conference->b_profile, BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER)) {
		ast_bridge_set_talker_src_video_mode(conference->bridge);
	}

	/* if the video_mode isn't set to automatically pick the video source, do nothing on exit. */
	if (!ast_test_flag(&conference->b_profile, BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED) &&
		!ast_test_flag(&conference->b_profile, BRIDGE_OPT_VIDEO_SRC_LAST_MARKED)) {
		return;
	}

	/* Make the next available marked user the video src.  */
	ao2_lock(conference);
	AST_LIST_TRAVERSE(&conference->active_list, user, list) {
		if (user->chan == chan) {
			continue;
		}
		if (ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER)) {
			ast_bridge_set_single_src_video_mode(conference->bridge, user->chan);
			break;
		}
	}
	ao2_unlock(conference);
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
	struct confbridge_conference *conference = obj;

	ast_debug(1, "Destroying conference bridge '%s'\n", conference->name);

	if (conference->playback_chan) {
		conf_announce_channel_depart(conference->playback_chan);
		ast_hangup(conference->playback_chan);
		conference->playback_chan = NULL;
	}

	/* Destroying a conference bridge is simple, all we have to do is destroy the bridging object */
	if (conference->bridge) {
		ast_bridge_destroy(conference->bridge, 0);
		conference->bridge = NULL;
	}

	conf_bridge_profile_destroy(&conference->b_profile);
	ast_cond_destroy(&conference->record_cond);
	ast_mutex_destroy(&conference->record_lock);
	ast_mutex_destroy(&conference->playback_lock);
}

/*! \brief Call the proper join event handler for the user for the conference bridge's current state
 * \internal
 * \param user The conference bridge user that is joining
 * \retval 0 success
 * \retval -1 failure
 */
static int handle_conf_user_join(struct confbridge_user *user)
{
	conference_event_fn handler;
	if (ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER)) {
		handler = user->conference->state->join_marked;
	} else if (ast_test_flag(&user->u_profile, USER_OPT_WAITMARKED)) {
		handler = user->conference->state->join_waitmarked;
	} else {
		handler = user->conference->state->join_unmarked;
	}

	ast_assert(handler != NULL);

	if (!handler) {
		conf_invalid_event_fn(user);
		return -1;
	}

	handler(user);

	return 0;
}

/*! \brief Call the proper leave event handler for the user for the conference bridge's current state
 * \internal
 * \param user The conference bridge user that is leaving
 * \retval 0 success
 * \retval -1 failure
 */
static int handle_conf_user_leave(struct confbridge_user *user)
{
	conference_event_fn handler;
	if (ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER)) {
		handler = user->conference->state->leave_marked;
	} else if (ast_test_flag(&user->u_profile, USER_OPT_WAITMARKED)) {
		handler = user->conference->state->leave_waitmarked;
	} else {
		handler = user->conference->state->leave_unmarked;
	}

	ast_assert(handler != NULL);

	if (!handler) {
		/* This should never happen. If it does, though, it is bad. The user will not have been removed
		 * from the appropriate list, so counts will be off and stuff. The conference won't be torn down, etc.
		 * Shouldn't happen, though. */
		conf_invalid_event_fn(user);
		return -1;
	}

	handler(user);

	return 0;
}

void conf_update_user_mute(struct confbridge_user *user)
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
		|| (!user->conference->markedusers
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

void conf_moh_stop(struct confbridge_user *user)
{
	user->playing_moh = 0;
	if (!user->suspended_moh) {
		int in_bridge;

		/*
		 * Locking the ast_bridge here is the only way to hold off the
		 * call to ast_bridge_join() in confbridge_exec() from
		 * interfering with the bridge and MOH operations here.
		 */
		ast_bridge_lock(user->conference->bridge);

		/*
		 * Temporarily suspend the user from the bridge so we have
		 * control to stop MOH if needed.
		 */
		in_bridge = !ast_bridge_suspend(user->conference->bridge, user->chan);
		ast_moh_stop(user->chan);
		if (in_bridge) {
			ast_bridge_unsuspend(user->conference->bridge, user->chan);
		}

		ast_bridge_unlock(user->conference->bridge);
	}
}

void conf_moh_start(struct confbridge_user *user)
{
	user->playing_moh = 1;
	if (!user->suspended_moh) {
		int in_bridge;

		/*
		 * Locking the ast_bridge here is the only way to hold off the
		 * call to ast_bridge_join() in confbridge_exec() from
		 * interfering with the bridge and MOH operations here.
		 */
		ast_bridge_lock(user->conference->bridge);

		/*
		 * Temporarily suspend the user from the bridge so we have
		 * control to start MOH if needed.
		 */
		in_bridge = !ast_bridge_suspend(user->conference->bridge, user->chan);
		ast_moh_start(user->chan, user->u_profile.moh_class, NULL);
		if (in_bridge) {
			ast_bridge_unsuspend(user->conference->bridge, user->chan);
		}

		ast_bridge_unlock(user->conference->bridge);
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
static void conf_moh_unsuspend(struct confbridge_user *user)
{
	ao2_lock(user->conference);
	if (--user->suspended_moh == 0 && user->playing_moh) {
		ast_moh_start(user->chan, user->u_profile.moh_class, NULL);
	}
	ao2_unlock(user->conference);
}

/*!
 * \internal
 * \brief Suspend MOH for the conference user.
 *
 * \param user Conference user to suspend MOH on.
 *
 * \return Nothing
 */
static void conf_moh_suspend(struct confbridge_user *user)
{
	ao2_lock(user->conference);
	if (user->suspended_moh++ == 0 && user->playing_moh) {
		ast_moh_stop(user->chan);
	}
	ao2_unlock(user->conference);
}

int conf_handle_inactive_waitmarked(struct confbridge_user *user)
{
	/* If we have not been quieted play back that they are waiting for the leader */
	if (!ast_test_flag(&user->u_profile, USER_OPT_QUIET) && play_prompt_to_user(user,
			conf_get_sound(CONF_SOUND_WAIT_FOR_LEADER, user->b_profile.sounds))) {
		/* user hungup while the sound was playing */
		return -1;
	}
	return 0;
}

int conf_handle_only_unmarked(struct confbridge_user *user)
{
	/* If audio prompts have not been quieted or this prompt quieted play it on out */
	if (!ast_test_flag(&user->u_profile, USER_OPT_QUIET | USER_OPT_NOONLYPERSON)) {
		if (play_prompt_to_user(user,
			conf_get_sound(CONF_SOUND_ONLY_PERSON, user->b_profile.sounds))) {
			/* user hungup while the sound was playing */
			return -1;
		}
	}
	return 0;
}

int conf_add_post_join_action(struct confbridge_user *user, int (*func)(struct confbridge_user *user))
{
	struct post_join_action *action;
	if (!(action = ast_calloc(1, sizeof(*action)))) {
		return -1;
	}
	action->func = func;
	AST_LIST_INSERT_TAIL(&user->post_join_list, action, list);
	return 0;
}


void conf_handle_first_join(struct confbridge_conference *conference)
{
	ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "confbridge:%s", conference->name);
}

void conf_handle_second_active(struct confbridge_conference *conference)
{
	/* If we are the second participant we may need to stop music on hold on the first */
	struct confbridge_user *first_user = AST_LIST_FIRST(&conference->active_list);

	if (ast_test_flag(&first_user->u_profile, USER_OPT_MUSICONHOLD)) {
		conf_moh_stop(first_user);
	}
	conf_update_user_mute(first_user);
}

void conf_ended(struct confbridge_conference *conference)
{
	/* Called with a reference to conference */
	ao2_unlink(conference_bridges, conference);
	send_conf_end_event(conference);
	conf_stop_record_thread(conference);
}

/*!
 * \brief Join a conference bridge
 *
 * \param conference_name The conference name
 * \param user Conference bridge user structure
 *
 * \return A pointer to the conference bridge struct, or NULL if the conference room wasn't found.
 */
static struct confbridge_conference *join_conference_bridge(const char *conference_name, struct confbridge_user *user)
{
	struct confbridge_conference *conference;
	struct post_join_action *action;
	int max_members_reached = 0;

	/* We explictly lock the conference bridges container ourselves so that other callers can not create duplicate conferences at the same */
	ao2_lock(conference_bridges);

	ast_debug(1, "Trying to find conference bridge '%s'\n", conference_name);

	/* Attempt to find an existing conference bridge */
	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (conference && conference->b_profile.max_members) {
		max_members_reached = conference->b_profile.max_members > conference->activeusers ? 0 : 1;
	}

	/* When finding a conference bridge that already exists make sure that it is not locked, and if so that we are not an admin */
	if (conference && (max_members_reached || conference->locked) && !ast_test_flag(&user->u_profile, USER_OPT_ADMIN)) {
		ao2_unlock(conference_bridges);
		ao2_ref(conference, -1);
		ast_debug(1, "Conference '%s' is locked and caller is not an admin\n", conference_name);
		ast_stream_and_wait(user->chan,
				conf_get_sound(CONF_SOUND_LOCKED, user->b_profile.sounds),
				"");
		return NULL;
	}

	/* If no conference bridge was found see if we can create one */
	if (!conference) {
		/* Try to allocate memory for a new conference bridge, if we fail... this won't end well. */
		if (!(conference = ao2_alloc(sizeof(*conference), destroy_conference_bridge))) {
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR, "Conference '%s' could not be created.\n", conference_name);
			return NULL;
		}

		/* Setup lock for playback channel */
		ast_mutex_init(&conference->playback_lock);

		/* Setup lock for the record channel */
		ast_mutex_init(&conference->record_lock);
		ast_cond_init(&conference->record_cond, NULL);

		/* Setup conference bridge parameters */
		conference->record_thread = AST_PTHREADT_NULL;
		ast_copy_string(conference->name, conference_name, sizeof(conference->name));
		conf_bridge_profile_copy(&conference->b_profile, &user->b_profile);

		/* Create an actual bridge that will do the audio mixing */
		conference->bridge = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_MULTIMIX,
			AST_BRIDGE_FLAG_MASQUERADE_ONLY | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY,
			app, conference_name, NULL);
		if (!conference->bridge) {
			ao2_ref(conference, -1);
			conference = NULL;
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR, "Conference '%s' mixing bridge could not be created.\n", conference_name);
			return NULL;
		}

		/* Set the internal sample rate on the bridge from the bridge profile */
		ast_bridge_set_internal_sample_rate(conference->bridge, conference->b_profile.internal_sample_rate);
		/* Set the internal mixing interval on the bridge from the bridge profile */
		ast_bridge_set_mixing_interval(conference->bridge, conference->b_profile.mix_interval);

		if (ast_test_flag(&conference->b_profile, BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER)) {
			ast_bridge_set_talker_src_video_mode(conference->bridge);
		}

		/* Link it into the conference bridges container */
		if (!ao2_link(conference_bridges, conference)) {
			ao2_ref(conference, -1);
			conference = NULL;
			ao2_unlock(conference_bridges);
			ast_log(LOG_ERROR,
				"Conference '%s' could not be added to the conferences list.\n", conference_name);
			return NULL;
		}

		/* Set the initial state to EMPTY */
		conference->state = CONF_STATE_EMPTY;

		conference->record_state = CONF_RECORD_STOP;
		if (ast_test_flag(&conference->b_profile, BRIDGE_OPT_RECORD_CONFERENCE)) {
			ao2_lock(conference);
			start_conf_record_thread(conference);
			ao2_unlock(conference);
		}

		send_conf_start_event(conference);
		ast_debug(1, "Created conference '%s' and linked to container.\n", conference_name);
	}

	ao2_unlock(conference_bridges);

	/* Setup conference bridge user parameters */
	user->conference = conference;

	ao2_lock(conference);

	/*
	 * Suspend any MOH until the user actually joins the bridge of
	 * the conference.  This way any pre-join file playback does not
	 * need to worry about MOH.
	 */
	user->suspended_moh = 1;

	if (handle_conf_user_join(user)) {
		/* Invalid event, nothing was done, so we don't want to process a leave. */
		ao2_unlock(conference);
		ao2_ref(conference, -1);
		return NULL;
	}

	if (ast_check_hangup(user->chan)) {
		ao2_unlock(conference);
		leave_conference(user);
		return NULL;
	}

	ao2_unlock(conference);

	/* If an announcement is to be played play it */
	if (!ast_strlen_zero(user->u_profile.announcement)) {
		if (play_prompt_to_user(user,
			user->u_profile.announcement)) {
			leave_conference(user);
			return NULL;
		}
	}

	/* Announce number of users if need be */
	if (ast_test_flag(&user->u_profile, USER_OPT_ANNOUNCEUSERCOUNT)) {
		if (announce_user_count(conference, user)) {
			leave_conference(user);
			return NULL;
		}
	}

	if (ast_test_flag(&user->u_profile, USER_OPT_ANNOUNCEUSERCOUNTALL) &&
		(conference->activeusers > user->u_profile.announce_user_count_all_after)) {
		int user_count_res;

		/*
		 * We have to autoservice the new user because he has not quite
		 * joined the conference yet.
		 */
		ast_autoservice_start(user->chan);
		user_count_res = announce_user_count(conference, NULL);
		ast_autoservice_stop(user->chan);
		if (user_count_res) {
			leave_conference(user);
			return NULL;
		}
	}

	/* Handle post-join actions */
	while ((action = AST_LIST_REMOVE_HEAD(&user->post_join_list, list))) {
		action->func(user);
		ast_free(action);
	}

	return conference;
}

/*!
 * \brief Leave a conference
 *
 * \param user The conference user
 */
static void leave_conference(struct confbridge_user *user)
{
	struct post_join_action *action;

	ao2_lock(user->conference);
	handle_conf_user_leave(user);
	ao2_unlock(user->conference);

	/* Discard any post-join actions */
	while ((action = AST_LIST_REMOVE_HEAD(&user->post_join_list, list))) {
		ast_free(action);
	}

	/* Done mucking with the conference, huzzah */
	ao2_ref(user->conference, -1);
	user->conference = NULL;
}

/*!
 * \internal
 * \brief Allocate playback channel for a conference.
 * \pre expects conference to be locked before calling this function
 */
static int alloc_playback_chan(struct confbridge_conference *conference)
{
	struct ast_format_cap *cap;
	struct ast_format format;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK);
	if (!cap) {
		return -1;
	}
	ast_format_cap_add(cap, ast_format_set(&format, AST_FORMAT_SLINEAR, 0));
	conference->playback_chan = ast_request("CBAnn", cap, NULL, NULL,
		conference->name, NULL);
	cap = ast_format_cap_destroy(cap);
	if (!conference->playback_chan) {
		return -1;
	}

	/* To make sure playback_chan has the same language of that profile */
	ast_channel_lock(conference->playback_chan);
	ast_channel_language_set(conference->playback_chan, conference->b_profile.language);
	ast_channel_unlock(conference->playback_chan);

	ast_debug(1, "Created announcer channel '%s' to conference bridge '%s'\n",
		ast_channel_name(conference->playback_chan), conference->name);
	return 0;
}

static int play_sound_helper(struct confbridge_conference *conference, const char *filename, int say_number)
{
	/* Do not waste resources trying to play files that do not exist */
	if (!ast_strlen_zero(filename) && !sound_file_exists(filename)) {
		return 0;
	}

	ast_mutex_lock(&conference->playback_lock);
	if (!conference->playback_chan && alloc_playback_chan(conference)) {
		ast_mutex_unlock(&conference->playback_lock);
		return -1;
	}
	if (conf_announce_channel_push(conference->playback_chan)) {
		ast_mutex_unlock(&conference->playback_lock);
		return -1;
	}

	/* The channel is all under our control, in goes the prompt */
	if (!ast_strlen_zero(filename)) {
		ast_stream_and_wait(conference->playback_chan, filename, "");
	} else if (say_number >= 0) {
		ast_say_number(conference->playback_chan, say_number, "",
			ast_channel_language(conference->playback_chan), NULL);
	}

	ast_debug(1, "Departing announcer channel '%s' from conference bridge '%s'\n",
		ast_channel_name(conference->playback_chan), conference->name);
	conf_announce_channel_depart(conference->playback_chan);

	ast_mutex_unlock(&conference->playback_lock);

	return 0;
}

int play_sound_file(struct confbridge_conference *conference, const char *filename)
{
	return play_sound_helper(conference, filename, -1);
}

/*!
 * \brief Play number into the conference bridge
 *
 * \param conference The conference bridge to say the number into
 * \param say_number number to say
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int play_sound_number(struct confbridge_conference *conference, int say_number)
{
	return play_sound_helper(conference, NULL, say_number);
}

static int conf_handle_talker_cb(struct ast_bridge_channel *bridge_channel, void *hook_pvt, int talking)
{
	const struct confbridge_user *user = hook_pvt;
	RAII_VAR(struct confbridge_conference *, conference, NULL, ao2_cleanup);
	struct ast_json *talking_extras;

	conference = ao2_find(conference_bridges, user->conference->name, OBJ_KEY);
	if (!conference) {
		/* Remove the hook since the conference does not exist. */
		return -1;
	}

	talking_extras = ast_json_pack("{s: s, s: b}",
		"talking_status", talking ? "on" : "off",
		"admin", ast_test_flag(&user->u_profile, USER_OPT_ADMIN));
	if (!talking_extras) {
		return 0;
	}

	send_conf_stasis(conference, bridge_channel->chan, confbridge_talking_type(), talking_extras, 0);
	ast_json_unref(talking_extras);
	return 0;
}

static int conf_get_pin(struct ast_channel *chan, struct confbridge_user *user)
{
	char pin_guess[MAX_PIN+1] = { 0, };
	const char *pin = user->u_profile.pin;
	char *tmp = pin_guess;
	int i, res;
	unsigned int len = MAX_PIN ;

	/* give them three tries to get the pin right */
	for (i = 0; i < 3; i++) {
		if (ast_app_getdata(chan,
			conf_get_sound(CONF_SOUND_GET_PIN, user->b_profile.sounds),
			tmp, len, 0) >= 0) {
			if (!strcasecmp(pin, pin_guess)) {
				return 0;
			}
		}
		ast_streamfile(chan,
			conf_get_sound(CONF_SOUND_INVALID_PIN, user->b_profile.sounds),
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

static int conf_rec_name(struct confbridge_user *user, const char *conf_name)
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
	struct confbridge_conference *conference = NULL;
	struct confbridge_user user = {
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

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ast_bridge_features_init(&user.features)) {
		res = -1;
		goto confbridge_cleanup;
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
	if (!conf_find_bridge_profile(chan, b_profile_name, &user.b_profile)) {
		ast_log(LOG_WARNING, "Conference bridge profile %s does not exist\n", b_profile_name);
		res = -1;
		goto confbridge_cleanup;
	}

	/* user profile name */
	if (args.argc > 2 && !ast_strlen_zero(args.u_profile_name)) {
		u_profile_name = args.u_profile_name;
	}
	if (!conf_find_user_profile(chan, u_profile_name, &user.u_profile)) {
		ast_log(LOG_WARNING, "Conference user profile %s does not exist\n", u_profile_name);
		res = -1;
		goto confbridge_cleanup;
	}

	quiet = ast_test_flag(&user.u_profile, USER_OPT_QUIET);

	/* ask for a PIN immediately after finding user profile.  This has to be
	 * prompted for requardless of quiet setting. */
	if (!ast_strlen_zero(user.u_profile.pin)) {
		if (conf_get_pin(chan, &user)) {
			res = -1; /* invalid PIN */
			goto confbridge_cleanup;
		}
	}

	/* See if we need them to record a intro name */
	if (!quiet && ast_test_flag(&user.u_profile, USER_OPT_ANNOUNCE_JOIN_LEAVE)) {
		conf_rec_name(&user, args.conf_name);
	}

	/* menu name */
	if (args.argc > 3 && !ast_strlen_zero(args.menu_name)) {
		ast_copy_string(user.menu_name, args.menu_name, sizeof(user.menu_name));
		if (conf_set_menu_to_user(user.menu_name, &user)) {
			ast_log(LOG_WARNING, "Conference menu %s does not exist and can not be applied to confbridge user.\n",
				args.menu_name);
			res = -1;
			goto confbridge_cleanup;
		}
	}

	/* Set if DTMF should pass through for this user or not */
	if (ast_test_flag(&user.u_profile, USER_OPT_DTMF_PASS)) {
		user.features.dtmf_passthrough = 1;
	} else {
		user.features.dtmf_passthrough = 0;
	}

	/* Set dsp threshold values if present */
	if (user.u_profile.talking_threshold) {
		user.tech_args.talking_threshold = user.u_profile.talking_threshold;
	}
	if (user.u_profile.silence_threshold) {
		user.tech_args.silence_threshold = user.u_profile.silence_threshold;
	}

	/* Set a talker indicate call back if talking detection is requested */
	if (ast_test_flag(&user.u_profile, USER_OPT_TALKER_DETECT)) {
		if (ast_bridge_talk_detector_hook(&user.features, conf_handle_talker_cb,
			&user, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
			res = -1;
			goto confbridge_cleanup;
		}
	}

	/* If the caller should be joined already muted, set the flag before we join. */
	if (ast_test_flag(&user.u_profile, USER_OPT_STARTMUTED)) {
		/* Set user level mute request. */
		user.muted = 1;
	}

	/* Look for a conference bridge matching the provided name */
	if (!(conference = join_conference_bridge(args.conf_name, &user))) {
		res = -1;
		goto confbridge_cleanup;
	}

	/* Keep a copy of volume adjustments so we can restore them later if need be */
	volume_adjustments[0] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_READ);
	volume_adjustments[1] = ast_audiohook_volume_get(chan, AST_AUDIOHOOK_DIRECTION_WRITE);

	if (ast_test_flag(&user.u_profile, USER_OPT_DROP_SILENCE)) {
		user.tech_args.drop_silence = 1;
	}

	if (ast_test_flag(&user.u_profile, USER_OPT_JITTERBUFFER)) {
		char *func_jb;
		if ((func_jb = ast_module_helper("", "func_jitterbuffer", 0, 0, 0, 0))) {
			ast_free(func_jb);
			ast_func_write(chan, "JITTERBUFFER(adaptive)", "default");
		}
	}

	if (ast_test_flag(&user.u_profile, USER_OPT_DENOISE)) {
		char *mod_speex;
		/* Reduce background noise from each participant */
		if ((mod_speex = ast_module_helper("", "codec_speex", 0, 0, 0, 0))) {
			ast_free(mod_speex);
			ast_func_write(chan, "DENOISE(rx)", "on");
		}
	}

	/* if this user has a intro, play it before entering */
	if (!ast_strlen_zero(user.name_rec_location)) {
		ast_autoservice_start(chan);
		play_sound_file(conference, user.name_rec_location);
		play_sound_file(conference,
			conf_get_sound(CONF_SOUND_HAS_JOINED, user.b_profile.sounds));
		ast_autoservice_stop(chan);
	}

	/* Play the Join sound to both the conference and the user entering. */
	if (!quiet) {
		const char *join_sound = conf_get_sound(CONF_SOUND_JOIN, user.b_profile.sounds);

		ast_stream_and_wait(chan, join_sound, "");
		ast_autoservice_start(chan);
		play_sound_file(conference, join_sound);
		ast_autoservice_stop(chan);
	}

	/* See if we need to automatically set this user as a video source or not */
	handle_video_on_join(conference, user.chan, ast_test_flag(&user.u_profile, USER_OPT_MARKEDUSER));

	conf_moh_unsuspend(&user);

	/* Join our conference bridge for real */
	send_join_event(&user, conference);
	ast_bridge_join(conference->bridge,
		chan,
		NULL,
		&user.features,
		&user.tech_args,
		0);
	send_leave_event(&user, conference);

	/* if we're shutting down, don't attempt to do further processing */
	if (ast_shutting_down()) {
		leave_conference(&user);
		conference = NULL;
		goto confbridge_cleanup;
	}

	/* If this user was a video source, we need to clean up and possibly pick a new source. */
	handle_video_on_exit(conference, user.chan);

	/* if this user has a intro, play it when leaving */
	if (!quiet && !ast_strlen_zero(user.name_rec_location)) {
		ast_autoservice_start(chan);
		play_sound_file(conference, user.name_rec_location);
		play_sound_file(conference,
			conf_get_sound(CONF_SOUND_HAS_LEFT, user.b_profile.sounds));
		ast_autoservice_stop(chan);
	}

	/* play the leave sound */
	if (!quiet) {
		const char *leave_sound = conf_get_sound(CONF_SOUND_LEAVE, user.b_profile.sounds);
		ast_autoservice_start(chan);
		play_sound_file(conference, leave_sound);
		ast_autoservice_stop(chan);
	}

	/* Easy as pie, depart this channel from the conference bridge */
	leave_conference(&user);
	conference = NULL;

	/* If the user was kicked from the conference play back the audio prompt for it */
	if (!quiet && user.kicked) {
		res = ast_stream_and_wait(chan,
			conf_get_sound(CONF_SOUND_KICKED, user.b_profile.sounds),
			"");
	}

	/* Restore volume adjustments to previous values in case they were changed */
	if (volume_adjustments[0]) {
		ast_audiohook_volume_set(chan, AST_AUDIOHOOK_DIRECTION_READ, volume_adjustments[0]);
	}
	if (volume_adjustments[1]) {
		ast_audiohook_volume_set(chan, AST_AUDIOHOOK_DIRECTION_WRITE, volume_adjustments[1]);
	}

	if (!ast_strlen_zero(user.name_rec_location)) {
		ast_filedelete(user.name_rec_location, NULL);
	}

confbridge_cleanup:
	ast_bridge_features_cleanup(&user.features);
	conf_bridge_profile_destroy(&user.b_profile);
	return res;
}

static int action_toggle_mute(struct confbridge_conference *conference,
	struct confbridge_user *user)
{
	int mute;

	/* Toggle user level mute request. */
	mute = !user->muted;
	user->muted = mute;

	conf_update_user_mute(user);
	ast_test_suite_event_notify("CONF_MUTE",
		"Message: participant %s %s\r\n"
		"Conference: %s\r\n"
		"Channel: %s",
		ast_channel_name(user->chan),
		mute ? "muted" : "unmuted",
		user->b_profile.name,
		ast_channel_name(user->chan));
	if (mute) {
		send_mute_event(user, conference);
	} else {
		send_unmute_event(user, conference);
	}

	return ast_stream_and_wait(user->chan, (mute ?
		conf_get_sound(CONF_SOUND_MUTED, user->b_profile.sounds) :
		conf_get_sound(CONF_SOUND_UNMUTED, user->b_profile.sounds)),
		"");
}

static int action_toggle_mute_participants(struct confbridge_conference *conference, struct confbridge_user *user)
{
	struct confbridge_user *cur_user = NULL;
	const char *sound_to_play;
	int mute;

	ao2_lock(conference);

	/* Toggle bridge level mute request. */
	mute = !conference->muted;
	conference->muted = mute;

	AST_LIST_TRAVERSE(&conference->active_list, cur_user, list) {
		if (!ast_test_flag(&cur_user->u_profile, USER_OPT_ADMIN)) {
			/* Set user level to bridge level mute request. */
			cur_user->muted = mute;
			conf_update_user_mute(cur_user);
		}
	}

	ao2_unlock(conference);

	sound_to_play = conf_get_sound((mute ? CONF_SOUND_PARTICIPANTS_MUTED : CONF_SOUND_PARTICIPANTS_UNMUTED),
		user->b_profile.sounds);

	/* The host needs to hear it seperately, as they don't get the audio from play_sound_helper */
	ast_stream_and_wait(user->chan, sound_to_play, "");

	/* Announce to the group that all participants are muted */
	ast_autoservice_start(user->chan);
	play_sound_helper(conference, sound_to_play, 0);
	ast_autoservice_stop(user->chan);

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

static int action_playback_and_continue(struct confbridge_conference *conference,
	struct confbridge_user *user,
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
		execute_menu_entry(conference,
			user,
			bridge_channel,
			&new_menu_entry, menu);
		conf_menu_entry_destroy(&new_menu_entry);
	}
	return 0;
}

static int action_kick_last(struct confbridge_conference *conference,
	struct ast_bridge_channel *bridge_channel,
	struct confbridge_user *user)
{
	struct confbridge_user *last_user = NULL;
	int isadmin = ast_test_flag(&user->u_profile, USER_OPT_ADMIN);

	if (!isadmin) {
		ast_stream_and_wait(bridge_channel->chan,
			conf_get_sound(CONF_SOUND_ERROR_MENU, user->b_profile.sounds),
			"");
		ast_log(LOG_WARNING, "Only admin users can use the kick_last menu action. Channel %s of conf %s is not an admin.\n",
			ast_channel_name(bridge_channel->chan),
			conference->name);
		return -1;
	}

	ao2_lock(conference);
	if (((last_user = AST_LIST_LAST(&conference->active_list)) == user)
		|| (ast_test_flag(&last_user->u_profile, USER_OPT_ADMIN))) {
		ao2_unlock(conference);
		ast_stream_and_wait(bridge_channel->chan,
			conf_get_sound(CONF_SOUND_ERROR_MENU, user->b_profile.sounds),
			"");
	} else if (last_user && !last_user->kicked) {
		last_user->kicked = 1;
		ast_bridge_remove(conference->bridge, last_user->chan);
		ao2_unlock(conference);
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

static int execute_menu_entry(struct confbridge_conference *conference,
	struct confbridge_user *user,
	struct ast_bridge_channel *bridge_channel,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu)
{
	struct conf_menu_action *menu_action;
	int isadmin = ast_test_flag(&user->u_profile, USER_OPT_ADMIN);
	int stop_prompts = 0;
	int res = 0;

	AST_LIST_TRAVERSE(&menu_entry->actions, menu_action, action) {
		switch (menu_action->id) {
		case MENU_ACTION_TOGGLE_MUTE:
			res |= action_toggle_mute(conference, user);
			break;
		case MENU_ACTION_ADMIN_TOGGLE_MUTE_PARTICIPANTS:
			if (!isadmin) {
				break;
			}
			action_toggle_mute_participants(conference, user);
			break;
		case MENU_ACTION_PARTICIPANT_COUNT:
			announce_user_count(conference, user);
			break;
		case MENU_ACTION_PLAYBACK:
			if (!stop_prompts) {
				res |= action_playback(bridge_channel, menu_action->data.playback_file);
			}
			break;
		case MENU_ACTION_RESET_LISTENING:
			ast_audiohook_volume_set(user->chan, AST_AUDIOHOOK_DIRECTION_WRITE, 0);
			break;
		case MENU_ACTION_RESET_TALKING:
			ast_audiohook_volume_set(user->chan, AST_AUDIOHOOK_DIRECTION_READ, 0);
			break;
		case MENU_ACTION_INCREASE_LISTENING:
			ast_audiohook_volume_adjust(user->chan,
				AST_AUDIOHOOK_DIRECTION_WRITE, 1);
			break;
		case MENU_ACTION_DECREASE_LISTENING:
			ast_audiohook_volume_adjust(user->chan,
				AST_AUDIOHOOK_DIRECTION_WRITE, -1);
			break;
		case MENU_ACTION_INCREASE_TALKING:
			ast_audiohook_volume_adjust(user->chan,
				AST_AUDIOHOOK_DIRECTION_READ, 1);
			break;
		case MENU_ACTION_DECREASE_TALKING:
			ast_audiohook_volume_adjust(user->chan,
				AST_AUDIOHOOK_DIRECTION_READ, -1);
			break;
		case MENU_ACTION_PLAYBACK_AND_CONTINUE:
			if (!(stop_prompts)) {
				res |= action_playback_and_continue(conference,
					user,
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
			conference->locked = (!conference->locked ? 1 : 0);
			res |= ast_stream_and_wait(bridge_channel->chan,
				(conference->locked ?
				conf_get_sound(CONF_SOUND_LOCKED_NOW, user->b_profile.sounds) :
				conf_get_sound(CONF_SOUND_UNLOCKED_NOW, user->b_profile.sounds)),
				"");

			break;
		case MENU_ACTION_ADMIN_KICK_LAST:
			res |= action_kick_last(conference, bridge_channel, user);
			break;
		case MENU_ACTION_LEAVE:
			ao2_lock(conference);
			ast_bridge_remove(conference->bridge, bridge_channel->chan);
			ao2_unlock(conference);
			break;
		case MENU_ACTION_NOOP:
			break;
		case MENU_ACTION_SET_SINGLE_VIDEO_SRC:
			ao2_lock(conference);
			ast_bridge_set_single_src_video_mode(conference->bridge, bridge_channel->chan);
			ao2_unlock(conference);
			break;
		case MENU_ACTION_RELEASE_SINGLE_VIDEO_SRC:
			handle_video_on_exit(conference, bridge_channel->chan);
			break;
		}
	}
	return res;
}

int conf_handle_dtmf(struct ast_bridge_channel *bridge_channel,
	struct confbridge_user *user,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu)
{
	/* See if music on hold is playing */
	conf_moh_suspend(user);

	/* execute the list of actions associated with this menu entry */
	execute_menu_entry(user->conference, user, bridge_channel, menu_entry, menu);

	/* See if music on hold needs to be started back up again */
	conf_moh_unsuspend(user);

	return 0;
}

static int kick_conference_participant(struct confbridge_conference *conference,
	const char *channel)
{
	int res = -1;
	int match;
	struct confbridge_user *user = NULL;
	int all = !strcasecmp("all", channel);
	int participants = !strcasecmp("participants", channel);

	SCOPED_AO2LOCK(bridge_lock, conference);

	AST_LIST_TRAVERSE(&conference->active_list, user, list) {
		if (user->kicked) {
			continue;
		}
		match = !strcasecmp(channel, ast_channel_name(user->chan));
		if (match || all
				|| (participants && !ast_test_flag(&user->u_profile, USER_OPT_ADMIN))) {
			user->kicked = 1;
			ast_bridge_remove(conference->bridge, user->chan);
			res = 0;
			if (match) {
				return res;
			}
		}
	}
	AST_LIST_TRAVERSE(&conference->waiting_list, user, list) {
		if (user->kicked) {
			continue;
		}
		match = !strcasecmp(channel, ast_channel_name(user->chan));
		if (match || all
				|| (participants && !ast_test_flag(&user->u_profile, USER_OPT_ADMIN))) {
			user->kicked = 1;
			ast_bridge_remove(conference->bridge, user->chan);
			res = 0;
			if (match) {
				return res;
			}
		}
	}

	return res;
}

static char *complete_confbridge_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct confbridge_conference *conference;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator iter;

	iter = ao2_iterator_init(conference_bridges, 0);
	while ((conference = ao2_iterator_next(&iter))) {
		if (!strncasecmp(conference->name, word, wordlen) && ++which > state) {
			res = ast_strdup(conference->name);
			ao2_ref(conference, -1);
			break;
		}
		ao2_ref(conference, -1);
	}
	ao2_iterator_destroy(&iter);

	return res;
}

static char *complete_confbridge_participant(const char *conference_name, const char *line, const char *word, int pos, int state)
{
	int which = 0;
	RAII_VAR(struct confbridge_conference *, conference, NULL, ao2_cleanup);
	struct confbridge_user *user;
	char *res = NULL;
	int wordlen = strlen(word);

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		return NULL;
	}

	if (!strncasecmp("all", word, wordlen) && ++which > state) {
		return ast_strdup("all");
	}

	if (!strncasecmp("participants", word, wordlen) && ++which > state) {
		return ast_strdup("participants");
	}

	{
		SCOPED_AO2LOCK(bridge_lock, conference);
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			if (!strncasecmp(ast_channel_name(user->chan), word, wordlen) && ++which > state) {
				res = ast_strdup(ast_channel_name(user->chan));
				return res;
			}
		}
		AST_LIST_TRAVERSE(&conference->waiting_list, user, list) {
			if (!strncasecmp(ast_channel_name(user->chan), word, wordlen) && ++which > state) {
				res = ast_strdup(ast_channel_name(user->chan));
				return res;
			}
		}
	}

	return NULL;
}

static char *handle_cli_confbridge_kick(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct confbridge_conference *conference;
	int not_found;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge kick";
		e->usage =
			"Usage: confbridge kick <conference> <channel>\n"
			"       Kicks a channel out of the conference bridge.\n"
			"             (all to kick everyone, participants to kick non-admins).\n";
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

	conference = ao2_find(conference_bridges, a->argv[2], OBJ_KEY);
	if (!conference) {
		ast_cli(a->fd, "No conference bridge named '%s' found!\n", a->argv[2]);
		return CLI_SUCCESS;
	}
	not_found = kick_conference_participant(conference, a->argv[3]);
	ao2_ref(conference, -1);
	if (not_found) {
		if (!strcasecmp("all", a->argv[3]) || !strcasecmp("participants", a->argv[3])) {
			ast_cli(a->fd, "No participants found!\n");
		} else {
			ast_cli(a->fd, "No participant named '%s' found!\n", a->argv[3]);
		}
		return CLI_SUCCESS;
	}
	ast_cli(a->fd, "Kicked '%s' out of conference '%s'\n", a->argv[3], a->argv[2]);
	return CLI_SUCCESS;
}

static void handle_cli_confbridge_list_item(struct ast_cli_args *a, struct confbridge_user *user, int waiting)
{
	char flag_str[6 + 1];/* Max flags + terminator */
	int pos = 0;

	/* Build flags column string. */
	if (ast_test_flag(&user->u_profile, USER_OPT_ADMIN)) {
		flag_str[pos++] = 'A';
	}
	if (ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER)) {
		flag_str[pos++] = 'M';
	}
	if (ast_test_flag(&user->u_profile, USER_OPT_WAITMARKED)) {
		flag_str[pos++] = 'W';
	}
	if (ast_test_flag(&user->u_profile, USER_OPT_ENDMARKED)) {
		flag_str[pos++] = 'E';
	}
	if (user->muted) {
		flag_str[pos++] = 'm';
	}
	if (waiting) {
		flag_str[pos++] = 'w';
	}
	flag_str[pos] = '\0';

	ast_cli(a->fd, "%-30s %-6s %-16s %-16s %-16s %s\n",
		ast_channel_name(user->chan),
		flag_str,
		user->u_profile.name,
		user->b_profile.name,
		user->menu_name,
		S_COR(ast_channel_caller(user->chan)->id.number.valid,
			ast_channel_caller(user->chan)->id.number.str, "<unknown>"));
}

static char *handle_cli_confbridge_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct confbridge_conference *conference;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge list";
		e->usage =
			"Usage: confbridge list [<name>]\n"
			"       Lists all currently active conference bridges or a specific conference bridge.\n"
			"\n"
			"       When a conference bridge name is provided, flags may be shown for users. Below\n"
			"       are the flags and what they represent.\n"
			"\n"
			"       Flags:\n"
			"         A - The user is an admin\n"
			"         M - The user is a marked user\n"
			"         W - The user must wait for a marked user to join\n"
			"         E - The user will be kicked after the last marked user leaves the conference\n"
			"         m - The user is muted\n"
			"         w - The user is waiting for a marked user to join\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_confbridge_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc == 2) {
		struct ao2_iterator iter;

		ast_cli(a->fd, "Conference Bridge Name           Users  Marked Locked?\n");
		ast_cli(a->fd, "================================ ====== ====== ========\n");
		iter = ao2_iterator_init(conference_bridges, 0);
		while ((conference = ao2_iterator_next(&iter))) {
			ast_cli(a->fd, "%-32s %6u %6u %s\n", conference->name, conference->activeusers + conference->waitingusers, conference->markedusers, (conference->locked ? "locked" : "unlocked"));
			ao2_ref(conference, -1);
		}
		ao2_iterator_destroy(&iter);
		return CLI_SUCCESS;
	}

	if (a->argc == 3) {
		struct confbridge_user *user;

		conference = ao2_find(conference_bridges, a->argv[2], OBJ_KEY);
		if (!conference) {
			ast_cli(a->fd, "No conference bridge named '%s' found!\n", a->argv[2]);
			return CLI_SUCCESS;
		}
		ast_cli(a->fd, "Channel                        Flags  User Profile     Bridge Profile   Menu             CallerID\n");
		ast_cli(a->fd, "============================== ====== ================ ================ ================ ================\n");
		ao2_lock(conference);
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			handle_cli_confbridge_list_item(a, user, 0);
		}
		AST_LIST_TRAVERSE(&conference->waiting_list, user, list) {
			handle_cli_confbridge_list_item(a, user, 1);
		}
		ao2_unlock(conference);
		ao2_ref(conference, -1);
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
static int generic_lock_unlock_helper(int lock, const char *conference_name)
{
	struct confbridge_conference *conference;
	int res = 0;

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		return -1;
	}
	ao2_lock(conference);
	conference->locked = lock;
	ast_test_suite_event_notify("CONF_LOCK", "Message: conference %s\r\nConference: %s", conference->locked ? "locked" : "unlocked", conference->b_profile.name);
	ao2_unlock(conference);
	ao2_ref(conference, -1);

	return res;
}

/* \internal
 * \brief Mute/unmute a single user.
 */
static void generic_mute_unmute_user(struct confbridge_conference *conference, struct confbridge_user *user, int mute)
{
	/* Set user level mute request. */
	user->muted = mute ? 1 : 0;

	conf_update_user_mute(user);
	ast_test_suite_event_notify("CONF_MUTE",
		"Message: participant %s %s\r\n"
		"Conference: %s\r\n"
		"Channel: %s",
		ast_channel_name(user->chan),
		mute ? "muted" : "unmuted",
		conference->b_profile.name,
		ast_channel_name(user->chan));
	if (mute) {
		send_mute_event(user, conference);
	} else {
		send_unmute_event(user, conference);
	}
}

/* \internal
 * \brief finds a conference user by channel name and mutes/unmutes them.
 *
 * \retval 0 success
 * \retval -1 conference not found
 * \retval -2 user not found
 */
static int generic_mute_unmute_helper(int mute, const char *conference_name,
	const char *chan_name)
{
	RAII_VAR(struct confbridge_conference *, conference, NULL, ao2_cleanup);
	struct confbridge_user *user;
	int all = !strcasecmp("all", chan_name);
	int participants = !strcasecmp("participants", chan_name);
	int res = -2;

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		return -1;
	}

	{
		SCOPED_AO2LOCK(bridge_lock, conference);
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			int match = !strncasecmp(chan_name, ast_channel_name(user->chan),
				strlen(chan_name));
			if (match || all
				|| (participants && !ast_test_flag(&user->u_profile, USER_OPT_ADMIN))) {
				generic_mute_unmute_user(conference, user, mute);
				res = 0;
				if (match) {
					return res;
				}
			}
		}

		AST_LIST_TRAVERSE(&conference->waiting_list, user, list) {
			int match = !strncasecmp(chan_name, ast_channel_name(user->chan),
				strlen(chan_name));
			if (match || all
				|| (participants && !ast_test_flag(&user->u_profile, USER_OPT_ADMIN))) {
				generic_mute_unmute_user(conference, user, mute);
				res = 0;
				if (match) {
					return res;
				}
			}
		}
	}

	return res;
}

static int cli_mute_unmute_helper(int mute, struct ast_cli_args *a)
{
	int res = generic_mute_unmute_helper(mute, a->argv[2], a->argv[3]);

	if (res == -1) {
		ast_cli(a->fd, "No conference bridge named '%s' found!\n", a->argv[2]);
		return -1;
	} else if (res == -2) {
		if (!strcasecmp("all", a->argv[3]) || !strcasecmp("participants", a->argv[3])) {
			ast_cli(a->fd, "No participants found in conference %s\n", a->argv[2]);
		} else {
			ast_cli(a->fd, "No channel named '%s' found in conference %s\n", a->argv[3], a->argv[2]);
		}
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
			"              (all to mute everyone, participants to mute non-admins)\n"
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
			"              (all to unmute everyone, participants to unmute non-admins)\n"
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
	struct confbridge_conference *conference;

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

	conference = ao2_find(conference_bridges, a->argv[3], OBJ_KEY);
	if (!conference) {
		ast_cli(a->fd, "Conference not found.\n");
		return CLI_FAILURE;
	}
	ao2_lock(conference);
	if (conf_is_recording(conference)) {
		ast_cli(a->fd, "Conference is already being recorded.\n");
		ao2_unlock(conference);
		ao2_ref(conference, -1);
		return CLI_SUCCESS;
	}
	if (!ast_strlen_zero(rec_file)) {
		ast_copy_string(conference->b_profile.rec_file, rec_file, sizeof(conference->b_profile.rec_file));
	}

	if (start_conf_record_thread(conference)) {
		ast_cli(a->fd, "Could not start recording due to internal error.\n");
		ao2_unlock(conference);
		ao2_ref(conference, -1);
		return CLI_FAILURE;
	}
	ao2_unlock(conference);

	ast_cli(a->fd, "Recording started\n");
	ao2_ref(conference, -1);
	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_stop_record(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct confbridge_conference *conference;
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

	conference = ao2_find(conference_bridges, a->argv[3], OBJ_KEY);
	if (!conference) {
		ast_cli(a->fd, "Conference not found.\n");
		return CLI_SUCCESS;
	}
	ao2_lock(conference);
	ret = conf_stop_record(conference);
	ao2_unlock(conference);
	ast_cli(a->fd, "Recording %sstopped.\n", ret ? "could not be " : "");
	ao2_ref(conference, -1);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_confbridge[] = {
	AST_CLI_DEFINE(handle_cli_confbridge_list, "List conference bridges and participants."),
	AST_CLI_DEFINE(handle_cli_confbridge_kick, "Kick participants out of conference bridges."),
	AST_CLI_DEFINE(handle_cli_confbridge_mute, "Mute participants."),
	AST_CLI_DEFINE(handle_cli_confbridge_unmute, "Unmute participants."),
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

static void action_confbridgelist_item(struct mansession *s, const char *id_text, struct confbridge_conference *conference, struct confbridge_user *user, int waiting)
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
		"WaitMarked: %s\r\n"
		"EndMarked: %s\r\n"
		"Waiting: %s\r\n"
		"Muted: %s\r\n"
		"AnsweredTime: %d\r\n"
		"\r\n",
		id_text,
		conference->name,
		S_COR(ast_channel_caller(user->chan)->id.number.valid, ast_channel_caller(user->chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(user->chan)->id.name.valid, ast_channel_caller(user->chan)->id.name.str, "<no name>"),
		ast_channel_name(user->chan),
		ast_test_flag(&user->u_profile, USER_OPT_ADMIN) ? "Yes" : "No",
		ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER) ? "Yes" : "No",
		ast_test_flag(&user->u_profile, USER_OPT_WAITMARKED) ? "Yes" : "No",
		ast_test_flag(&user->u_profile, USER_OPT_ENDMARKED) ? "Yes" : "No",
		waiting ? "Yes" : "No",
		user->muted ? "Yes" : "No",
		ast_channel_get_up_time(user->chan));
}

static int action_confbridgelist(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	const char *conference_name = astman_get_header(m, "Conference");
	struct confbridge_user *user;
	struct confbridge_conference *conference;
	char id_text[80];
	int total = 0;

	id_text[0] = '\0';
	if (!ast_strlen_zero(actionid)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", actionid);
	}
	if (ast_strlen_zero(conference_name)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}
	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	astman_send_listack(s, m, "Confbridge user list will follow", "start");

	ao2_lock(conference);
	AST_LIST_TRAVERSE(&conference->active_list, user, list) {
		total++;
		action_confbridgelist_item(s, id_text, conference, user, 0);
	}
	AST_LIST_TRAVERSE(&conference->waiting_list, user, list) {
		total++;
		action_confbridgelist_item(s, id_text, conference, user, 1);
	}
	ao2_unlock(conference);
	ao2_ref(conference, -1);

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
	struct confbridge_conference *conference;
	struct ao2_iterator iter;
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
	iter = ao2_iterator_init(conference_bridges, 0);
	while ((conference = ao2_iterator_next(&iter))) {
		totalitems++;

		ao2_lock(conference);
		astman_append(s,
		"Event: ConfbridgeListRooms\r\n"
		"%s"
		"Conference: %s\r\n"
		"Parties: %u\r\n"
		"Marked: %u\r\n"
		"Locked: %s\r\n"
		"\r\n",
		id_text,
		conference->name,
		conference->activeusers + conference->waitingusers,
		conference->markedusers,
		conference->locked ? "Yes" : "No");
		ao2_unlock(conference);

		ao2_ref(conference, -1);
	}
	ao2_iterator_destroy(&iter);

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
	const char *conference_name = astman_get_header(m, "Conference");
	const char *channel_name = astman_get_header(m, "Channel");
	int res = 0;

	if (ast_strlen_zero(conference_name)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (ast_strlen_zero(channel_name)) {
		astman_send_error(s, m, "No channel name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	res = generic_mute_unmute_helper(mute, conference_name, channel_name);

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
	const char *conference_name = astman_get_header(m, "Conference");
	int res = 0;

	if (ast_strlen_zero(conference_name)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}
	if ((res = generic_lock_unlock_helper(lock, conference_name))) {
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
	const char *conference_name = astman_get_header(m, "Conference");
	const char *channel = astman_get_header(m, "Channel");
	struct confbridge_conference *conference;
	int found;

	if (ast_strlen_zero(conference_name)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	found = !kick_conference_participant(conference, channel);
	ao2_ref(conference, -1);

	if (found) {
		astman_send_ack(s, m, !strcmp("all", channel) ? "All participants kicked" : "User kicked");
	} else {
		astman_send_error(s, m, "No Channel by that name found in Conference.");
	}
	return 0;
}

static int action_confbridgestartrecord(struct mansession *s, const struct message *m)
{
	const char *conference_name = astman_get_header(m, "Conference");
	const char *recordfile = astman_get_header(m, "RecordFile");
	struct confbridge_conference *conference;

	if (ast_strlen_zero(conference_name)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	ao2_lock(conference);
	if (conf_is_recording(conference)) {
		astman_send_error(s, m, "Conference is already being recorded.");
		ao2_unlock(conference);
		ao2_ref(conference, -1);
		return 0;
	}

	if (!ast_strlen_zero(recordfile)) {
		ast_copy_string(conference->b_profile.rec_file, recordfile, sizeof(conference->b_profile.rec_file));
	}

	if (start_conf_record_thread(conference)) {
		astman_send_error(s, m, "Internal error starting conference recording.");
		ao2_unlock(conference);
		ao2_ref(conference, -1);
		return 0;
	}
	ao2_unlock(conference);

	ao2_ref(conference, -1);
	astman_send_ack(s, m, "Conference Recording Started.");
	return 0;
}
static int action_confbridgestoprecord(struct mansession *s, const struct message *m)
{
	const char *conference_name = astman_get_header(m, "Conference");
	struct confbridge_conference *conference;

	if (ast_strlen_zero(conference_name)) {
		astman_send_error(s, m, "No Conference name provided.");
		return 0;
	}
	if (!ao2_container_count(conference_bridges)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	ao2_lock(conference);
	if (conf_stop_record(conference)) {
		ao2_unlock(conference);
		astman_send_error(s, m, "Internal error while stopping recording.");
		ao2_ref(conference, -1);
		return 0;
	}
	ao2_unlock(conference);

	ao2_ref(conference, -1);
	astman_send_ack(s, m, "Conference Recording Stopped.");
	return 0;
}

static int action_confbridgesetsinglevideosrc(struct mansession *s, const struct message *m)
{
	const char *conference_name = astman_get_header(m, "Conference");
	const char *channel = astman_get_header(m, "Channel");
	struct confbridge_user *user;
	struct confbridge_conference *conference;

	if (ast_strlen_zero(conference_name)) {
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

	conference = ao2_find(conference_bridges, conference_name, OBJ_KEY);
	if (!conference) {
		astman_send_error(s, m, "No Conference by that name found.");
		return 0;
	}

	/* find channel and set as video src. */
	ao2_lock(conference);
	AST_LIST_TRAVERSE(&conference->active_list, user, list) {
		if (!strncmp(channel, ast_channel_name(user->chan), strlen(channel))) {
			ast_bridge_set_single_src_video_mode(conference->bridge, user->chan);
			break;
		}
	}
	ao2_unlock(conference);
	ao2_ref(conference, -1);

	/* do not access user after conference unlock.  We are just
	 * using this check to see if it was found or not */
	if (!user) {
		astman_send_error(s, m, "No channel by that name found in conference.");
		return 0;
	}
	astman_send_ack(s, m, "Conference single video source set.");
	return 0;
}

static int func_confbridge_info(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parse;
	struct confbridge_conference *conference;
	struct confbridge_user *user;
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
	conference = ao2_find(conference_bridges, args.confno, OBJ_KEY);
	if (!conference) {
		snprintf(buf, len, "0");
		return 0;
	}

	/* get the correct count for the type requested */
	ao2_lock(conference);
	if (!strncasecmp(args.type, "parties", 7)) {
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			count++;
		}
		AST_LIST_TRAVERSE(&conference->waiting_list, user, list) {
			count++;
		}
	} else if (!strncasecmp(args.type, "admins", 6)) {
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			if (ast_test_flag(&user->u_profile, USER_OPT_ADMIN)) {
				count++;
			}
		}
	} else if (!strncasecmp(args.type, "marked", 6)) {
		AST_LIST_TRAVERSE(&conference->active_list, user, list) {
			if (ast_test_flag(&user->u_profile, USER_OPT_MARKEDUSER)) {
				count++;
			}
		}
	} else if (!strncasecmp(args.type, "locked", 6)) {
		count = conference->locked;
	} else {
		ast_log(LOG_ERROR, "Invalid keyword '%s' passed to CONFBRIDGE_INFO.  Should be one of: "
			"parties, admins, marked, or locked.\n", args.type);
	}
	snprintf(buf, len, "%d", count);
	ao2_unlock(conference);
	ao2_ref(conference, -1);
	return 0;
}

void conf_add_user_active(struct confbridge_conference *conference, struct confbridge_user *user)
{
	AST_LIST_INSERT_TAIL(&conference->active_list, user, list);
	conference->activeusers++;
}

void conf_add_user_marked(struct confbridge_conference *conference, struct confbridge_user *user)
{
	AST_LIST_INSERT_TAIL(&conference->active_list, user, list);
	conference->activeusers++;
	conference->markedusers++;
}

void conf_add_user_waiting(struct confbridge_conference *conference, struct confbridge_user *user)
{
	AST_LIST_INSERT_TAIL(&conference->waiting_list, user, list);
	conference->waitingusers++;
}

void conf_remove_user_active(struct confbridge_conference *conference, struct confbridge_user *user)
{
	AST_LIST_REMOVE(&conference->active_list, user, list);
	conference->activeusers--;
}

void conf_remove_user_marked(struct confbridge_conference *conference, struct confbridge_user *user)
{
	AST_LIST_REMOVE(&conference->active_list, user, list);
	conference->activeusers--;
	conference->markedusers--;
}

void conf_mute_only_active(struct confbridge_conference *conference)
{
	struct confbridge_user *only_user = AST_LIST_FIRST(&conference->active_list);

	/* Turn on MOH if the single participant is set up for it */
	if (ast_test_flag(&only_user->u_profile, USER_OPT_MUSICONHOLD)) {
		conf_moh_start(only_user);
	}
	conf_update_user_mute(only_user);
}

void conf_remove_user_waiting(struct confbridge_conference *conference, struct confbridge_user *user)
{
	AST_LIST_REMOVE(&conference->waiting_list, user, list);
	conference->waitingusers--;
}

/*!
 * \internal
 * \brief Unregister a ConfBridge channel technology.
 * \since 12.0.0
 *
 * \param tech What to unregister.
 *
 * \return Nothing
 */
static void unregister_channel_tech(struct ast_channel_tech *tech)
{
	ast_channel_unregister(tech);
	tech->capabilities = ast_format_cap_destroy(tech->capabilities);
}

/*!
 * \internal
 * \brief Register a ConfBridge channel technology.
 * \since 12.0.0
 *
 * \param tech What to register.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int register_channel_tech(struct ast_channel_tech *tech)
{
	tech->capabilities = ast_format_cap_alloc(0);
	if (!tech->capabilities) {
		return -1;
	}
	ast_format_cap_add_all(tech->capabilities);
	if (ast_channel_register(tech)) {
		ast_log(LOG_ERROR, "Unable to register channel technology %s(%s).\n",
			tech->type, tech->description);
		return -1;
	}
	return 0;
}

/*! \brief Called when module is being unloaded */
static int unload_module(void)
{
	ast_unregister_application(app);

	ast_custom_function_unregister(&confbridge_function);
	ast_custom_function_unregister(&confbridge_info_function);

	ast_cli_unregister_multiple(cli_confbridge, ARRAY_LEN(cli_confbridge));

	ast_manager_unregister("ConfbridgeList");
	ast_manager_unregister("ConfbridgeListRooms");
	ast_manager_unregister("ConfbridgeMute");
	ast_manager_unregister("ConfbridgeUnmute");
	ast_manager_unregister("ConfbridgeKick");
	ast_manager_unregister("ConfbridgeUnlock");
	ast_manager_unregister("ConfbridgeLock");
	ast_manager_unregister("ConfbridgeStartRecord");
	ast_manager_unregister("ConfbridgeStopRecord");
	ast_manager_unregister("ConfbridgeSetSingleVideoSrc");

	/* Unsubscribe from stasis confbridge message type and clean it up. */
	manager_confbridge_shutdown();

	/* Get rid of the conference bridges container. Since we only allow dynamic ones none will be active. */
	ao2_cleanup(conference_bridges);
	conference_bridges = NULL;

	conf_destroy_config();

	unregister_channel_tech(conf_announce_get_tech());
	unregister_channel_tech(conf_record_get_tech());

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	int res = 0;

	if (conf_load_config()) {
		ast_log(LOG_ERROR, "Unable to load config. Not loading module.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (register_channel_tech(conf_record_get_tech())
		|| register_channel_tech(conf_announce_get_tech())) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	/* Create a container to hold the conference bridges */
	conference_bridges = ao2_container_alloc(CONFERENCE_BRIDGE_BUCKETS,
		conference_bridge_hash_cb, conference_bridge_cmp_cb);
	if (!conference_bridges) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	/* Setup manager stasis subscriptions */
	res |= manager_confbridge_init();

	res |= ast_register_application_xml(app, confbridge_exec);

	res |= ast_custom_function_register(&confbridge_function);
	res |= ast_custom_function_register(&confbridge_info_function);

	res |= ast_cli_register_multiple(cli_confbridge, ARRAY_LEN(cli_confbridge));

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
	if (res) {
		unload_module();
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
