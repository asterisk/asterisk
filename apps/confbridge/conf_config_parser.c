/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
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
 * \brief ConfBridge config parser
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "include/confbridge.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/bridge_features.h"
#include "asterisk/stringfields.h"
#include "asterisk/pbx.h"


/*** DOCUMENTATION
	<configInfo name="app_confbridge" language="en_US">
		<synopsis>Conference Bridge Application</synopsis>
		<configFile name="confbridge.conf">
			<configObject name="global">
				<synopsis>Unused, but reserved.</synopsis>
			</configObject>
			<configObject name="user_profile">
				<synopsis>A named profile to apply to specific callers.</synopsis>
				<description><para>Callers in a ConfBridge have a profile associated with them
				that determine their options. A configuration section is determined to be a
				user_profile when the <literal>type</literal> parameter has a value
				of <literal>user</literal>.
				</para></description>
				<configOption name="type">
					<synopsis>Define this configuration category as a user profile.</synopsis>
					<description><para>The type parameter determines how a context in the
					configuration file is interpreted.</para>
					<enumlist>
						<enum name="user"><para>Configure the context as a <replaceable>user_profile</replaceable></para></enum>
						<enum name="bridge"><para>Configure the context as a <replaceable>bridge_profile</replaceable></para></enum>
						<enum name="menu"><para>Configure the context as a <replaceable>menu</replaceable></para></enum>
					</enumlist>
					</description>
				</configOption>
				<configOption name="admin">
					<synopsis>Sets if the user is an admin or not</synopsis>
				</configOption>
				<configOption name="marked">
					<synopsis>Sets if this is a marked user or not</synopsis>
				</configOption>
				<configOption name="startmuted">
					<synopsis>Sets if all users should start out muted</synopsis>
				</configOption>
				<configOption name="music_on_hold_when_empty">
					<synopsis>Play MOH when user is alone or waiting on a marked user</synopsis>
				</configOption>
				<configOption name="quiet">
					<synopsis>Silence enter/leave prompts and user intros for this user</synopsis>
				</configOption>
				<configOption name="announce_user_count">
					<synopsis>Sets if the number of users should be announced to the user</synopsis>
				</configOption>
				<configOption name="announce_user_count_all">
					<synopsis>Announce user count to all the other users when this user joins</synopsis>
					<description><para>Sets if the number of users should be announced to all the other users
					in the conference when this user joins. This option can be either set to 'yes' or
					a number. When set to a number, the announcement will only occur once the user
					count is above the specified number.
					</para></description>
				</configOption>
				<configOption name="announce_only_user">
					<synopsis>Announce to a user when they join an empty conference</synopsis>
				</configOption>
				<configOption name="wait_marked">
					<synopsis>Sets if the user must wait for a marked user to enter before joining a conference</synopsis>
				</configOption>
				<configOption name="end_marked">
					<synopsis>Kick the user from the conference when the last marked user leaves</synopsis>
				</configOption>
				<configOption name="talk_detection_events">
					<synopsis>Set whether or not notifications of when a user begins and ends talking should be sent out as events over AMI</synopsis>
				</configOption>
				<configOption name="dtmf_passthrough">
					<synopsis>Sets whether or not DTMF should pass through the conference</synopsis>
				</configOption>
				<configOption name="announce_join_leave">
					<synopsis>Prompt user for their name when joining a conference and play it to the conference when they enter</synopsis>
				</configOption>
				<configOption name="announce_join_leave_review">
					<synopsis>Prompt user for their name when joining a conference and play it to the conference when they enter.
					The user will be asked to review the recording of their name before entering the conference.</synopsis>
				</configOption>
				<configOption name="pin">
					<synopsis>Sets a PIN the user must enter before joining the conference</synopsis>
				</configOption>
				<configOption name="music_on_hold_class">
					<synopsis>The MOH class to use for this user</synopsis>
				</configOption>
				<configOption name="announcement">
					<synopsis>Sound file to play to the user when they join a conference</synopsis>
				</configOption>
				<configOption name="denoise">
					<synopsis>Apply a denoise filter to the audio before mixing</synopsis>
					<description><para>Sets whether or not a denoise filter should be applied
					to the audio before mixing or not.  Off by default. Requires
					<literal>codec_speex</literal> to be built and installed.  Do not confuse this option
					with <replaceable>drop_silence</replaceable>.  Denoise is useful if there is a lot of background
					noise for a user as it attempts to remove the noise while preserving
					the speech.  This option does NOT remove silence from being mixed into
					the conference and does come at the cost of a slight performance hit.
					</para></description>
				</configOption>
				<configOption name="dsp_drop_silence">
					<synopsis>Drop what Asterisk detects as silence from audio sent to the bridge</synopsis>
					<description><para>
					This option drops what Asterisk detects as silence from
					entering into the bridge.  Enabling this option will drastically
					improve performance and help remove the buildup of background
					noise from the conference. Highly recommended for large conferences
					due to its performance enhancements.
					</para></description>
				</configOption>
				<configOption name="dsp_silence_threshold">
					<synopsis>The number of milliseconds of detected silence necessary to trigger silence detection</synopsis>
					<description><para>
					The time in milliseconds of sound falling within the what
					the dsp has established as baseline silence before a user
					is considered be silent.  This value affects several
					operations and should not be changed unless the impact
					on call quality is fully understood.</para>
					<para>What this value affects internally:</para>
					<para>
						1. When talk detection AMI events are enabled, this value
						determines when the user has stopped talking after a
						period of talking.  If this value is set too low
						AMI events indicating the user has stopped talking
						may get falsely sent out when the user briefly pauses
						during mid sentence.
					</para>
					<para>
						2. The <replaceable>drop_silence</replaceable> option depends on this value to
						determine when the user's audio should begin to be
						dropped from the conference bridge after the user
						stops talking.  If this value is set too low the user's
						audio stream may sound choppy to the other participants.
						This is caused by the user transitioning constantly from
						silence to talking during mid sentence.
					</para>
					<para>
						The best way to approach this option is to set it slightly above
						the maximum amount of ms of silence a user may generate during
						natural speech.
					</para>
					<para>By default this value is 2500ms. Valid values are 1 through 2^31.</para>
					</description>
				</configOption>
				<configOption name="dsp_talking_threshold">
					<synopsis>The number of milliseconds of detected non-silence necessary to triger talk detection</synopsis>
					<description><para>
						The time in milliseconds of sound above what the dsp has
						established as base line silence for a user before a user
						is considered to be talking.  This value affects several
						operations and should not be changed unless the impact on
						call quality is fully understood.</para>
						<para>
						What this value affects internally:
						</para>
						<para>
						1. Audio is only mixed out of a user's incoming audio stream
						if talking is detected.  If this value is set too
						loose the user will hear themselves briefly each
						time they begin talking until the dsp has time to
						establish that they are in fact talking.
						</para>
						<para>
						2. When talk detection AMI events are enabled, this value
						determines when talking has begun which results in
						an AMI event to fire.  If this value is set too tight
						AMI events may be falsely triggered by variants in
						room noise.
						</para>
						<para>
						3. The <replaceable>drop_silence</replaceable> option depends on this value to determine
						when the user's audio should be mixed into the bridge
						after periods of silence.  If this value is too loose
						the beginning of a user's speech will get cut off as they
						transition from silence to talking.
						</para>
						<para>By default this value is 160 ms. Valid values are 1 through 2^31</para>
					</description>
				</configOption>
				<configOption name="jitterbuffer">
					<synopsis>Place a jitter buffer on the user's audio stream before audio mixing is performed</synopsis>
					<description><para>
						Enabling this option places a jitterbuffer on the user's audio stream
						before audio mixing is performed.  This is highly recommended but will
						add a slight delay to the audio.  This option is using the <literal>JITTERBUFFER</literal>
						dialplan function's default adaptive jitterbuffer.  For a more fine tuned
						jitterbuffer, disable this option and use the <literal>JITTERBUFFER</literal> dialplan function
						on the user before entering the ConfBridge application.
					</para></description>
				</configOption>
				<configOption name="template">
					<synopsis>When using the CONFBRIDGE dialplan function, use a user profile as a template for creating a new temporary profile</synopsis>
				</configOption>
			</configObject>
			<configObject name="bridge_profile">
				<synopsis>A named profile to apply to specific bridges.</synopsis>
				<description><para>ConfBridge bridges have a profile associated with them
				that determine their options. A configuration section is determined to be a
				<literal>bridge_profile</literal> when the <literal>type</literal> parameter has a value
				of <literal>bridge</literal>.
				</para></description>
				<configOption name="type">
					<synopsis>Define this configuration category as a bridge profile</synopsis>
					<description><para>The type parameter determines how a context in the
					configuration file is interpreted.</para>
					<enumlist>
						<enum name="user"><para>Configure the context as a <replaceable>user_profile</replaceable></para></enum>
						<enum name="bridge"><para>Configure the context as a <replaceable>bridge_profile</replaceable></para></enum>
						<enum name="menu"><para>Configure the context as a <replaceable>menu</replaceable></para></enum>
					</enumlist>
					</description>
				</configOption>
				<configOption name="jitterbuffer">
					<synopsis>Place a jitter buffer on the conference's audio stream</synopsis>
				</configOption>
				<configOption name="internal_sample_rate">
					<synopsis>Set the internal native sample rate for mixing the conference</synopsis>
					<description><para>
						Sets the internal native sample rate the
						conference is mixed at.  This is set to automatically
						adjust the sample rate to the best quality by default.
						Other values can be anything from 8000-192000.  If a
						sample rate is set that Asterisk does not support, the
						closest sample rate Asterisk does support to the one requested
						will be used.
					</para></description>
				</configOption>
				<configOption name="language" default="en">
					<synopsis>The language used for announcements to the conference.</synopsis>
					<description><para>
						By default, announcements to a conference use English.  Which means
						the prompts played to all users within the conference will be
						English.  By changing the language of a bridge, this will change
						the language of the prompts played to all users.
					</para></description>
				</configOption>
				<configOption name="mixing_interval">
					<synopsis>Sets the internal mixing interval in milliseconds for the bridge</synopsis>
					<description><para>
						Sets the internal mixing interval in milliseconds for the bridge.  This
						number reflects how tight or loose the mixing will be for the conference.
						In order to improve performance a larger mixing interval such as 40ms may
						be chosen.  Using a larger mixing interval comes at the cost of introducing
						larger amounts of delay into the bridge.  Valid values here are 10, 20, 40,
						or 80.
					</para></description>
				</configOption>
				<configOption name="record_conference">
					<synopsis>Record the conference starting with the first active user's entrance and ending with the last active user's exit</synopsis>
					<description><para>
						Records the conference call starting when the first user
						enters the room, and ending when the last user exits the room.
						The default recorded filename is
						<filename>'confbridge-${name of conference bridge}-${start time}.wav'</filename>
						and the default format is 8khz slinear.  This file will be
						located in the configured monitoring directory in <filename>asterisk.conf</filename>.
					</para></description>
				</configOption>
				<configOption name="record_file" default="confbridge-${name of conference bridge}-${start time}.wav">
					<synopsis>The filename of the conference recording</synopsis>
					<description><para>
						When <replaceable>record_conference</replaceable> is set to yes, the specific name of the
						record file can be set using this option.  Note that since multiple
						conferences may use the same bridge profile, this may cause issues
						depending on the configuration.  It is recommended to only use this
						option dynamically with the <literal>CONFBRIDGE()</literal> dialplan function. This
						allows the record name to be specified and a unique name to be chosen.
						By default, the record_file is stored in Asterisk's spool/monitor directory
						with a unique filename starting with the 'confbridge' prefix.
					</para></description>
				</configOption>
				<configOption name="record_file_append" default="yes">
					<synopsis>Append record file when starting/stopping on same conference recording</synopsis>
					<description><para>
						When <replaceable>record_file_append</replaceable> is set to yes, stopping and starting recording on a
						conference adds the new portion to end of current record_file. When this is
						set to no, a new <replaceable>record_file</replaceable> is generated every time you start then stop recording
						on a conference.
					</para></description>
				</configOption>
				<configOption name="video_mode">
					<synopsis>Sets how confbridge handles video distribution to the conference participants</synopsis>
					<description><para>
						Sets how confbridge handles video distribution to the conference participants.
						Note that participants wanting to view and be the source of a video feed
						<emphasis>MUST</emphasis> be sharing the same video codec.  Also, using video in conjunction with
						with the jitterbuffer currently results in the audio being slightly out of sync
						with the video.  This is a result of the jitterbuffer only working on the audio
						stream.  It is recommended to disable the jitterbuffer when video is used.</para>
						<enumlist>
							<enum name="none">
								<para>No video sources are set by default in the conference. It is still
								possible for a user to be set as a video source via AMI or DTMF action
								at any time.</para>
							</enum>
							<enum name="follow_talker">
								<para>The video feed will follow whoever is talking and providing video.</para>
							</enum>
							<enum name="last_marked">
								<para>The last marked user to join the conference with video capabilities
								will be the single source of video distributed to all participants.
								If multiple marked users are capable of video, the last one to join
								is always the source, when that user leaves it goes to the one who
								joined before them.</para>
							</enum>
							<enum name="first_marked">
								<para>The first marked user to join the conference with video capabilities
								is the single source of video distribution among all participants. If
								that user leaves, the marked user to join after them becomes the source.</para>
							</enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="max_members">
					<synopsis>Limit the maximum number of participants for a single conference</synopsis>
					<description><para>
						This option limits the number of participants for a single
						conference to a specific number.  By default conferences
						have no participant limit. After the limit is reached, the
						conference will be locked until someone leaves.  Note however
						that an Admin user will always be alowed to join the conference
						regardless if this limit is reached or not.
					</para></description>
				</configOption>
				<configOption name="^sound_">
					<synopsis>Override the various conference bridge sound files</synopsis>
					<description><para>
						All sounds in the conference are customizable using the bridge profile options below.
						Simply state the option followed by the filename or full path of the filename after
						the option.  Example: <literal>sound_had_joined=conf-hasjoin</literal>  This will play the <literal>conf-hasjoin</literal>
						sound file found in the sounds directory when announcing someone's name is joining the
						conference.</para>
						<enumlist>
							<enum name="sound_join"><para>The sound played to everyone when someone enters the conference.</para></enum>
							<enum name="sound_leave"><para>The sound played to everyone when someone leaves the conference.</para></enum>
							<enum name="sound_has_joined"><para>The sound played before announcing someone's name has
										joined the conference. This is used for user intros.
										Example <literal>"_____ has joined the conference"</literal></para></enum>
							<enum name="sound_has_left"><para>The sound played when announcing someone's name has
										left the conference. This is used for user intros.
										Example <literal>"_____ has left the conference"</literal></para></enum>
							<enum name="sound_kicked"><para>The sound played to a user who has been kicked from the conference.</para></enum>
							<enum name="sound_muted"><para>The sound played when the mute option it toggled on.</para></enum>
							<enum name="sound_unmuted"><para>The sound played when the mute option it toggled off.</para></enum>
							<enum name="sound_only_person"><para>The sound played when the user is the only person in the conference.</para></enum>
							<enum name="sound_only_one"><para>The sound played to a user when there is only one other
										person is in the conference.</para></enum>
							<enum name="sound_there_are"><para>The sound played when announcing how many users there
										are in a conference.</para></enum>
							<enum name="sound_other_in_party"><para>This file is used in conjunction with <literal>sound_there_are</literal>
										when announcing how many users there are in the conference.
										The sounds are stringed together like this.
										<literal>"sound_there_are" ${number of participants} "sound_other_in_party"</literal></para></enum>
							<enum name="sound_place_into_conference"><para>The sound played when someone is placed into the conference
										after waiting for a marked user.</para></enum>
							<enum name="sound_wait_for_leader"><para>The sound played when a user is placed into a conference that
										can not start until a marked user enters.</para></enum>
							<enum name="sound_leader_has_left"><para>The sound played when the last marked user leaves the conference.</para></enum>
							<enum name="sound_get_pin"><para>The sound played when prompting for a conference pin number.</para></enum>
							<enum name="sound_invalid_pin"><para>The sound played when an invalid pin is entered too many times.</para></enum>
							<enum name="sound_locked"><para>The sound played to a user trying to join a locked conference.</para></enum>
							<enum name="sound_locked_now"><para>The sound played to an admin after toggling the conference to locked mode.</para></enum>
							<enum name="sound_unlocked_now"><para>The sound played to an admin after toggling the conference to unlocked mode.</para></enum>
							<enum name="sound_error_menu"><para>The sound played when an invalid menu option is entered.</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="template">
					<synopsis>When using the CONFBRIDGE dialplan function, use a bridge profile as a template for creating a new temporary profile</synopsis>
				</configOption>
			</configObject>
			<configObject name="menu">
				<synopsis>A conference user menu</synopsis>
				<description>
					<para>Conference users, as defined by a <replaceable>conf_user</replaceable>,
					can have a DTMF menu assigned to their profile when they enter the
					<literal>ConfBridge</literal> application.</para>
				</description>
				<configOption name="type">
					<synopsis>Define this configuration category as a menu</synopsis>
					<description><para>The type parameter determines how a context in the
					configuration file is interpreted.</para>
					<enumlist>
						<enum name="user"><para>Configure the context as a <replaceable>user_profile</replaceable></para></enum>
						<enum name="bridge"><para>Configure the context as a <replaceable>bridge_profile</replaceable></para></enum>
						<enum name="menu"><para>Configure the context as a <replaceable>menu</replaceable></para></enum>
					</enumlist>
					</description>
				</configOption>
				<configOption name="template">
					<synopsis>When using the CONFBRIDGE dialplan function, use a menu profile as a template for creating a new temporary profile</synopsis>
				</configOption>
				<configOption name="^[0-9A-D*#]+$">
					<synopsis>DTMF sequences to assign various confbridge actions to</synopsis>
					<description>
					<para>The ConfBridge application also has the ability to apply custom DTMF menus to
					each channel using the application.  Like the User and Bridge profiles a menu
					is passed in to ConfBridge as an argument in the dialplan.</para>
					<para>Below is a list of menu actions that can be assigned to a DTMF sequence.</para>
					<note><para>
						To have the first DTMF digit in a sequence be the '#' character, you need to
						escape it.  If it is not escaped then normal config file processing will
						think it is a directive like #include.  For example: The mute setting is
						toggled when <literal>#1</literal> is pressed.</para>
						<para><literal>\#1=toggle_mute</literal></para>
					</note>
					<note><para>
					A single DTMF sequence can have multiple actions associated with it. This is
					accomplished by stringing the actions together and using a <literal>,</literal> as the
					delimiter.  Example:  Both listening and talking volume is reset when <literal>5</literal> is
					pressed.  <literal>5=reset_talking_volume, reset_listening_volume</literal></para></note>
					<enumlist>
						<enum name="playback(filename&amp;filename2&amp;...)"><para>
							<literal>playback</literal> will play back an audio file to a channel
							and then immediately return to the conference.
							This file can not be interupted by DTMF.
							Multiple files can be chained together using the
							<literal>&amp;</literal> character.</para></enum>
						<enum name="playback_and_continue(filename&amp;filename2&amp;...)"><para>
							<literal>playback_and_continue</literal> will
							play back a prompt while continuing to
							collect the dtmf sequence.  This is useful
							when using a menu prompt that describes all
							the menu options.  Note however that any DTMF
							during this action will terminate the prompts
							playback.  Prompt files can be chained together
							using the <literal>&amp;</literal> character as a delimiter.</para></enum>
						<enum name="toggle_mute"><para>
							Toggle turning on and off mute.  Mute will make the user silent
							to everyone else, but the user will still be able to listen in.
							</para></enum>
						<enum name="no_op"><para>
							This action does nothing (No Operation). Its only real purpose exists for
							being able to reserve a sequence in the config as a menu exit sequence.</para></enum>
						<enum name="decrease_listening_volume"><para>
							Decreases the channel's listening volume.</para></enum>
						<enum name="increase_listening_volume"><para>
							Increases the channel's listening volume.</para></enum>
						<enum name="reset_listening_volume"><para>
							Reset channel's listening volume to default level.</para></enum>
						<enum name="decrease_talking_volume"><para>
							Decreases the channel's talking volume.</para></enum>
						<enum name="increase_talking_volume"><para>
							Increases the channel's talking volume.</para></enum>
						<enum name="reset_talking_volume"><para>
							Reset channel's talking volume to default level.</para></enum>
						<enum name="dialplan_exec(context,exten,priority)"><para>
							The <literal>dialplan_exec</literal> action allows a user
							to escape from the conference and execute
							commands in the dialplan.  Once the dialplan
							exits the user will be put back into the
							conference.  The possibilities are endless!</para></enum>
						<enum name="leave_conference"><para>
							This action allows a user to exit the conference and continue
							execution in the dialplan.</para></enum>
						<enum name="admin_kick_last"><para>
							This action allows an Admin to kick the last participant from the
							conference. This action will only work for admins which allows
							a single menu to be used for both users and admins.</para></enum>
						<enum name="admin_toggle_conference_lock"><para>
							This action allows an Admin to toggle locking and
							unlocking the conference.  Non admins can not use
							this action even if it is in their menu.</para></enum>
						<enum name="set_as_single_video_src"><para>
							This action allows any user to set themselves as the
							single video source distributed to all participants.
							This will make the video feed stick to them regardless
							of what the <literal>video_mode</literal> is set to.</para></enum>
						<enum name="release_as_single_video_src"><para>
							This action allows a user to release themselves as
							the video source.  If <literal>video_mode</literal> is not set to <literal>none</literal>
							this action will result in the conference returning to
							whatever video mode the bridge profile is using.</para>
							<para>Note that this action will have no effect if the user
							is not currently the video source.  Also, the user is
							not guaranteed by using this action that they will not
							become the video source again.  The bridge will return
							to whatever operation the <literal>video_mode</literal> option is set to
							upon release of the video src.</para></enum>
						<enum name="admin_toggle_mute_participants"><para>
							This action allows an administrator to toggle the mute
							state for all non-admins within a conference.  All
							admin users are unaffected by this option.  Note that all
							users, regardless of their admin status, are notified
							that the conference is muted.</para></enum>
						<enum name="participant_count"><para>
							This action plays back the number of participants currently
							in a conference</para></enum>
						</enumlist>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

struct confbridge_cfg {
	struct ao2_container *bridge_profiles;
	struct ao2_container *user_profiles;
	struct ao2_container *menus;
};

static int verify_default_profiles(void);
static void *bridge_profile_alloc(const char *category);
static void *bridge_profile_find(struct ao2_container *container, const char *category);
static struct bridge_profile_sounds *bridge_profile_sounds_alloc(void);

static void bridge_profile_destructor(void *obj)
{
	struct bridge_profile *b_profile = obj;
	ao2_cleanup(b_profile->sounds);
}

static void *bridge_profile_alloc(const char *category)
{
	struct bridge_profile *b_profile;

	if (!(b_profile = ao2_alloc(sizeof(*b_profile), bridge_profile_destructor))) {
		return NULL;
	}

	if (!(b_profile->sounds = bridge_profile_sounds_alloc())) {
		ao2_ref(b_profile, -1);
		return NULL;
	}

	ast_copy_string(b_profile->name, category, sizeof(b_profile->name));

	return b_profile;
}

static void *bridge_profile_find(struct ao2_container *container, const char *category)
{
	return ao2_find(container, category, OBJ_KEY);
}

static struct aco_type bridge_type = {
	.type = ACO_ITEM,
	.name = "bridge_profile",
	.category_match = ACO_BLACKLIST,
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "bridge",
	.item_alloc = bridge_profile_alloc,
	.item_find = bridge_profile_find,
	.item_offset = offsetof(struct confbridge_cfg, bridge_profiles),
};

static void *user_profile_alloc(const char *category);
static void *user_profile_find(struct ao2_container *container, const char *category);
static void user_profile_destructor(void *obj)
{
	return;
}

static void *user_profile_alloc(const char *category)
{
	struct user_profile *u_profile;

	if (!(u_profile = ao2_alloc(sizeof(*u_profile), user_profile_destructor))) {
		return NULL;
	}

	ast_copy_string(u_profile->name, category, sizeof(u_profile->name));

	return u_profile;
}

static void *user_profile_find(struct ao2_container *container, const char *category)
{
	return ao2_find(container, category, OBJ_KEY);
}

static struct aco_type user_type = {
	.type = ACO_ITEM,
	.name  = "user_profile",
	.category_match = ACO_BLACKLIST,
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "user",
	.item_alloc = user_profile_alloc,
	.item_find = user_profile_find,
	.item_offset = offsetof(struct confbridge_cfg, user_profiles),
};

static void *menu_alloc(const char *category);
static void *menu_find(struct ao2_container *container, const char *category);
static void menu_destructor(void *obj);

static void *menu_alloc(const char *category)
{
	struct conf_menu *menu;
	if (!(menu = ao2_alloc(sizeof(*menu), menu_destructor))) {
		return NULL;
	}
	ast_copy_string(menu->name, category, sizeof(menu->name));
	return menu;
}

static void *menu_find(struct ao2_container *container, const char *category)
{
	return ao2_find(container, category, OBJ_KEY);
}

static struct aco_type menu_type = {
	.type = ACO_ITEM,
	.name = "menu",
	.category_match = ACO_BLACKLIST,
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "menu",
	.item_alloc = menu_alloc,
	.item_find = menu_find,
	.item_offset = offsetof(struct confbridge_cfg, menus),
};

/* Used to pass to aco_option_register */
static struct aco_type *bridge_types[] = ACO_TYPES(&bridge_type);
static struct aco_type *menu_types[] = ACO_TYPES(&menu_type);
static struct aco_type *user_types[] = ACO_TYPES(&user_type);

/* The general category is reserved, but unused */
static struct aco_type general_type = {
	.type = ACO_GLOBAL,
	.name = "global",
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

static struct aco_file confbridge_conf = {
	.filename = "confbridge.conf",
	.types = ACO_TYPES(&bridge_type, &user_type, &menu_type, &general_type),
};

static AO2_GLOBAL_OBJ_STATIC(cfg_handle);

static void *confbridge_cfg_alloc(void);

CONFIG_INFO_STANDARD(cfg_info, cfg_handle, confbridge_cfg_alloc,
	.files = ACO_FILES(&confbridge_conf),
	.pre_apply_config = verify_default_profiles,
);

/*! bridge profile container functions */
static int bridge_cmp_cb(void *obj, void *arg, int flags)
{
	const struct bridge_profile *left = obj;
	const struct bridge_profile *right = arg;
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

static int bridge_hash_cb(const void *obj, const int flags)
{
	const struct bridge_profile *b_profile = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		name = b_profile->name;
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

/*! menu container functions */
static int menu_cmp_cb(void *obj, void *arg, int flags)
{
	const struct conf_menu *left = obj;
	const struct conf_menu *right = arg;
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

static int menu_hash_cb(const void *obj, const int flags)
{
	const struct conf_menu *menu = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		name = menu->name;
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

static void menu_destructor(void *obj)
{
	struct conf_menu *menu = obj;
	struct conf_menu_entry *entry = NULL;

	while ((entry = AST_LIST_REMOVE_HEAD(&menu->entries, entry))) {
		conf_menu_entry_destroy(entry);
		ast_free(entry);
	}
}

/*! User profile container functions */
static int user_cmp_cb(void *obj, void *arg, int flags)
{
	const struct user_profile *left = obj;
	const struct user_profile *right = arg;
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

static int user_hash_cb(const void *obj, const int flags)
{
	const struct user_profile *u_profile = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		name = u_profile->name;
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

/*! Bridge Profile Sounds functions */
static void bridge_profile_sounds_destroy_cb(void *obj)
{
	struct bridge_profile_sounds *sounds = obj;
	ast_string_field_free_memory(sounds);
}

static struct bridge_profile_sounds *bridge_profile_sounds_alloc(void)
{
	struct bridge_profile_sounds *sounds = ao2_alloc(sizeof(*sounds), bridge_profile_sounds_destroy_cb);

	if (!sounds) {
		return NULL;
	}
	if (ast_string_field_init(sounds, 512)) {
		ao2_ref(sounds, -1);
		return NULL;
	}

	return sounds;
}

static int set_sound(const char *sound_name, const char *sound_file, struct bridge_profile *b_profile)
{
	struct bridge_profile_sounds *sounds = b_profile->sounds;
	if (ast_strlen_zero(sound_file)) {
		return -1;
	}

	if (!strcasecmp(sound_name, "sound_only_person")) {
		ast_string_field_set(sounds, onlyperson, sound_file);
	} else if (!strcasecmp(sound_name, "sound_only_one")) {
		ast_string_field_set(sounds, onlyone, sound_file);
	} else if (!strcasecmp(sound_name, "sound_has_joined")) {
		ast_string_field_set(sounds, hasjoin, sound_file);
	} else if (!strcasecmp(sound_name, "sound_has_left")) {
		ast_string_field_set(sounds, hasleft, sound_file);
	} else if (!strcasecmp(sound_name, "sound_kicked")) {
		ast_string_field_set(sounds, kicked, sound_file);
	} else if (!strcasecmp(sound_name, "sound_muted")) {
		ast_string_field_set(sounds, muted, sound_file);
	} else if (!strcasecmp(sound_name, "sound_unmuted")) {
		ast_string_field_set(sounds, unmuted, sound_file);
	} else if (!strcasecmp(sound_name, "sound_there_are")) {
		ast_string_field_set(sounds, thereare, sound_file);
	} else if (!strcasecmp(sound_name, "sound_other_in_party")) {
		ast_string_field_set(sounds, otherinparty, sound_file);
	} else if (!strcasecmp(sound_name, "sound_place_into_conference")) {
		static int deprecation_warning = 1;
		if (deprecation_warning) {
			ast_log(LOG_WARNING, "sound_place_into_conference is deprecated"
				" and unused. Use sound_begin for similar functionality.");
			deprecation_warning = 0;
		}
		ast_string_field_set(sounds, placeintoconf, sound_file);
	} else if (!strcasecmp(sound_name, "sound_wait_for_leader")) {
		ast_string_field_set(sounds, waitforleader, sound_file);
	} else if (!strcasecmp(sound_name, "sound_leader_has_left")) {
		ast_string_field_set(sounds, leaderhasleft, sound_file);
	} else if (!strcasecmp(sound_name, "sound_get_pin")) {
		ast_string_field_set(sounds, getpin, sound_file);
	} else if (!strcasecmp(sound_name, "sound_invalid_pin")) {
		ast_string_field_set(sounds, invalidpin, sound_file);
	} else if (!strcasecmp(sound_name, "sound_locked")) {
		ast_string_field_set(sounds, locked, sound_file);
	} else if (!strcasecmp(sound_name, "sound_unlocked_now")) {
		ast_string_field_set(sounds, unlockednow, sound_file);
	} else if (!strcasecmp(sound_name, "sound_locked_now")) {
		ast_string_field_set(sounds, lockednow, sound_file);
	} else if (!strcasecmp(sound_name, "sound_error_menu")) {
		ast_string_field_set(sounds, errormenu, sound_file);
	} else if (!strcasecmp(sound_name, "sound_join")) {
		ast_string_field_set(sounds, join, sound_file);
	} else if (!strcasecmp(sound_name, "sound_leave")) {
		ast_string_field_set(sounds, leave, sound_file);
	} else if (!strcasecmp(sound_name, "sound_participants_muted")) {
		ast_string_field_set(sounds, participantsmuted, sound_file);
	} else if (!strcasecmp(sound_name, "sound_participants_unmuted")) {
		ast_string_field_set(sounds, participantsunmuted, sound_file);
	} else if (!strcasecmp(sound_name, "sound_begin")) {
		ast_string_field_set(sounds, begin, sound_file);
	} else {
		return -1;
	}

	return 0;
}

/*! CONFBRIDGE dialplan function functions and channel datastore. */
struct func_confbridge_data {
	struct bridge_profile b_profile;
	struct user_profile u_profile;
	struct conf_menu *menu;
	unsigned int b_usable:1; /*!< Tells if bridge profile is usable or not */
	unsigned int u_usable:1; /*!< Tells if user profile is usable or not */
	unsigned int m_usable:1; /*!< Tells if menu profile is usable or not */
};

static void func_confbridge_data_destructor(struct func_confbridge_data *b_data)
{
	conf_bridge_profile_destroy(&b_data->b_profile);
	ao2_cleanup(b_data->menu);
	ast_free(b_data);
}

static void func_confbridge_destroy_cb(void *data)
{
	struct func_confbridge_data *b_data = data;
	func_confbridge_data_destructor(b_data);
};

static const struct ast_datastore_info confbridge_datastore = {
	.type = "confbridge",
	.destroy = func_confbridge_destroy_cb
};

int func_confbridge_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore;
	struct func_confbridge_data *b_data;
	char *parse;
	struct ast_variable tmpvar = { 0, };
	struct ast_variable template = {
		.name = "template",
		.file = "CONFBRIDGE"
	};
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(type);
		AST_APP_ARG(option);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	/* parse all the required arguments and make sure they exist. */
	if (ast_strlen_zero(data)) {
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (ast_strlen_zero(args.type) || ast_strlen_zero(args.option)) {
		return -1;
	}

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL);
	if (!datastore) {
		datastore = ast_datastore_alloc(&confbridge_datastore, NULL);
		if (!datastore) {
			ast_channel_unlock(chan);
			return 0;
		}
		b_data = ast_calloc(1, sizeof(*b_data));
		if (!b_data) {
			ast_channel_unlock(chan);
			ast_datastore_free(datastore);
			return 0;
		}
		datastore->data = b_data;
		b_data->b_profile.sounds = bridge_profile_sounds_alloc();
		if (!b_data->b_profile.sounds) {
			ast_channel_unlock(chan);
			ast_datastore_free(datastore);
			return 0;
		}
		if (!(b_data->menu = menu_alloc("dialplan"))) {
			ast_channel_unlock(chan);
			ast_datastore_free(datastore);
			return 0;
		}
		ast_channel_datastore_add(chan, datastore);
	} else {
		b_data = datastore->data;
	}
	ast_channel_unlock(chan);

	/* SET(CONFBRIDGE(type,option)=value) */
	if (!value) {
		value = "";
	}
	tmpvar.name = args.option;
	tmpvar.value = value;
	tmpvar.file = "CONFBRIDGE";
	if (!strcasecmp(args.type, "bridge")) {
		if (!strcasecmp(args.option, "clear")) {
			b_data->b_usable = 0;
			conf_bridge_profile_destroy(&b_data->b_profile);
			memset(&b_data->b_profile, 0, sizeof(b_data->b_profile)) ;
			if (!(b_data->b_profile.sounds = bridge_profile_sounds_alloc())) {
				/* If this reallocation fails, the datastore has become unusable and must be destroyed. */
				ast_channel_lock(chan);
				ast_channel_datastore_remove(chan, datastore);
				ast_channel_unlock(chan);
				ast_datastore_free(datastore);
			}
			return 0;
		}

		if (b_data && !b_data->b_usable && strcasecmp(args.option, "template")) {
			template.value = DEFAULT_BRIDGE_PROFILE;
			aco_process_var(&bridge_type, "dialplan", &template, &b_data->b_profile);
		}

		if (!aco_process_var(&bridge_type, "dialplan", &tmpvar, &b_data->b_profile)) {
			b_data->b_usable = 1;
			return 0;
		}
	} else if (!strcasecmp(args.type, "user")) {
		if (!strcasecmp(args.option, "clear")) {
			b_data->u_usable = 0;
			user_profile_destructor(&b_data->u_profile);
			memset(&b_data->u_profile, 0, sizeof(b_data->u_profile));
			return 0;
		}

		if (b_data && !b_data->u_usable && strcasecmp(args.option, "template")) {
			template.value = DEFAULT_USER_PROFILE;
			aco_process_var(&user_type, "dialplan", &template, &b_data->u_profile);
		}

		if (!aco_process_var(&user_type, "dialplan", &tmpvar, &b_data->u_profile)) {
			b_data->u_usable = 1;
			return 0;
		}
	} else if (!strcasecmp(args.type, "menu")) {
		if (!strcasecmp(args.option, "clear")) {
			b_data->m_usable = 0;
			ao2_cleanup(b_data->menu);
			if (!(b_data->menu = menu_alloc("dialplan"))) {
				/* If this reallocation fails, the datastore has become unusable and must be destroyed */
				ast_channel_lock(chan);
				ast_channel_datastore_remove(chan, datastore);
				ast_channel_unlock(chan);
				ast_datastore_free(datastore);
			}
			return 0;
		}

		if (b_data && !b_data->m_usable && strcasecmp(args.option, "template")) {
			template.value = DEFAULT_MENU_PROFILE;
			aco_process_var(&menu_type, "dialplan", &template, &b_data->menu);
		}

		if (!aco_process_var(&menu_type, "dialplan", &tmpvar, b_data->menu)) {
			b_data->m_usable = 1;
			return 0;
		}
	}

	ast_log(LOG_WARNING, "%s(%s,%s) cannot be set to '%s'. Invalid type, option, or value.\n",
		cmd, args.type, args.option, value);
	return -1;
}

static int add_action_to_menu_entry(struct conf_menu_entry *menu_entry, enum conf_menu_action_id id, char *databuf)
{
	struct conf_menu_action *menu_action = ast_calloc(1, sizeof(*menu_action));

	if (!menu_action) {
		return -1;
	}
	menu_action->id = id;

	switch (id) {
	case MENU_ACTION_NOOP:
	case MENU_ACTION_TOGGLE_MUTE:
	case MENU_ACTION_INCREASE_LISTENING:
	case MENU_ACTION_DECREASE_LISTENING:
	case MENU_ACTION_INCREASE_TALKING:
	case MENU_ACTION_DECREASE_TALKING:
	case MENU_ACTION_RESET_LISTENING:
	case MENU_ACTION_RESET_TALKING:
	case MENU_ACTION_ADMIN_TOGGLE_LOCK:
	case MENU_ACTION_ADMIN_TOGGLE_MUTE_PARTICIPANTS:
	case MENU_ACTION_PARTICIPANT_COUNT:
	case MENU_ACTION_ADMIN_KICK_LAST:
	case MENU_ACTION_LEAVE:
	case MENU_ACTION_SET_SINGLE_VIDEO_SRC:
	case MENU_ACTION_RELEASE_SINGLE_VIDEO_SRC:
		break;
	case MENU_ACTION_PLAYBACK:
	case MENU_ACTION_PLAYBACK_AND_CONTINUE:
		if (!(ast_strlen_zero(databuf))) {
			ast_copy_string(menu_action->data.playback_file, databuf, sizeof(menu_action->data.playback_file));
		} else {
			ast_free(menu_action);
			return -1;
		}
		break;
	case MENU_ACTION_DIALPLAN_EXEC:
		if (!(ast_strlen_zero(databuf))) {
			AST_DECLARE_APP_ARGS(args,
				AST_APP_ARG(context);
				AST_APP_ARG(exten);
				AST_APP_ARG(priority);
			);
			AST_STANDARD_APP_ARGS(args, databuf);
			if (!ast_strlen_zero(args.context)) {
				ast_copy_string(menu_action->data.dialplan_args.context,
					args.context,
					sizeof(menu_action->data.dialplan_args.context));
			}
			if (!ast_strlen_zero(args.exten)) {
				ast_copy_string(menu_action->data.dialplan_args.exten,
					args.exten,
					sizeof(menu_action->data.dialplan_args.exten));
			}
			menu_action->data.dialplan_args.priority = 1; /* 1 by default */
			if (!ast_strlen_zero(args.priority) &&
				(sscanf(args.priority, "%30d", &menu_action->data.dialplan_args.priority) != 1)) {
				/* invalid priority */
				ast_free(menu_action);
				return -1;
			}
		} else {
			ast_free(menu_action);
			return -1;
		}
	};

	AST_LIST_INSERT_TAIL(&menu_entry->actions, menu_action, action);

	return 0;
}

static int add_menu_entry(struct conf_menu *menu, const char *dtmf, const char *action_names)
{
	struct conf_menu_entry *menu_entry = NULL, *cur = NULL;
	int res = 0;
	char *tmp_action_names = ast_strdupa(action_names);
	char *action = NULL;
	char *action_args;
	char *tmp;
	char buf[PATH_MAX];
	char *delimiter = ",";

	if (!(menu_entry = ast_calloc(1, sizeof(*menu_entry)))) {
		return -1;
	}

	for (;;) {
		char *comma;
		char *startbrace;
		char *endbrace;
		unsigned int action_len;

		if (ast_strlen_zero(tmp_action_names)) {
			break;
		}
		startbrace = strchr(tmp_action_names, '(');
		endbrace = strchr(tmp_action_names, ')');
		comma = strchr(tmp_action_names, ',');

		/* If the next action has brackets with comma delimited arguments in it,
		 * make the delimeter ')' instead of a comma to preserve the argments */
		if (startbrace && endbrace && comma && (comma > startbrace && comma < endbrace)) {
			delimiter = ")";
		} else {
			delimiter = ",";
		}

		if (!(action = strsep(&tmp_action_names, delimiter))) {
			break;
		}

		action = ast_strip(action);
		if (ast_strlen_zero(action)) {
			continue;
		}

		action_len = strlen(action);
		ast_copy_string(menu_entry->dtmf, dtmf, sizeof(menu_entry->dtmf));
		if (!strcasecmp(action, "toggle_mute")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_TOGGLE_MUTE, NULL);
		} else if (!strcasecmp(action, "no_op")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_NOOP, NULL);
		} else if (!strcasecmp(action, "increase_listening_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_INCREASE_LISTENING, NULL);
		} else if (!strcasecmp(action, "decrease_listening_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_DECREASE_LISTENING, NULL);
		} else if (!strcasecmp(action, "increase_talking_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_INCREASE_TALKING, NULL);
		} else if (!strcasecmp(action, "reset_listening_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_RESET_LISTENING, NULL);
		} else if (!strcasecmp(action, "reset_talking_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_RESET_TALKING, NULL);
		} else if (!strcasecmp(action, "decrease_talking_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_DECREASE_TALKING, NULL);
		} else if (!strcasecmp(action, "admin_toggle_conference_lock")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_ADMIN_TOGGLE_LOCK, NULL);
		} else if (!strcasecmp(action, "admin_toggle_mute_participants")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_ADMIN_TOGGLE_MUTE_PARTICIPANTS, NULL);
		} else if (!strcasecmp(action, "participant_count")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_PARTICIPANT_COUNT, NULL);
		} else if (!strcasecmp(action, "admin_kick_last")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_ADMIN_KICK_LAST, NULL);
		} else if (!strcasecmp(action, "leave_conference")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_LEAVE, NULL);
		} else if (!strcasecmp(action, "set_as_single_video_src")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_SET_SINGLE_VIDEO_SRC, NULL);
		} else if (!strcasecmp(action, "release_as_single_video_src")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_RELEASE_SINGLE_VIDEO_SRC, NULL);
		} else if (!strncasecmp(action, "dialplan_exec(", 14)) {
			ast_copy_string(buf, action, sizeof(buf));
			action_args = buf;
			if ((action_args = strchr(action, '('))) {
				action_args++;
			}
			/* it is possible that this argument may or may not
			 * have a closing brace at this point, it all depends on if
			 * comma delimited arguments were provided */
			if ((tmp = strchr(action, ')'))) {
				*tmp = '\0';
			}
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_DIALPLAN_EXEC, action_args);
		} else if (action_len >= 21 && !strncasecmp(action, "playback_and_continue(", 22)) {
			ast_copy_string(buf, action, sizeof(buf));
			action_args = buf;
			if ((action_args = strchr(action, '(')) && (tmp = strrchr(action_args, ')'))) {
				*tmp = '\0';
				action_args++;
			}
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_PLAYBACK_AND_CONTINUE, action_args);
		} else if (action_len >= 8 && !strncasecmp(action, "playback(", 9)) {
			ast_copy_string(buf, action, sizeof(buf));
			action_args = buf;
			if ((action_args = strchr(action, '(')) && (tmp = strrchr(action_args, ')'))) {
				*tmp = '\0';
				action_args++;
			}
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_PLAYBACK, action_args);
		}
	}

	/* if adding any of the actions failed, bail */
	if (res) {
		struct conf_menu_action *menu_action;
		while ((menu_action = AST_LIST_REMOVE_HEAD(&menu_entry->actions, action))) {
			ast_free(menu_action);
		}
		ast_free(menu_entry);
		return -1;
	}

	/* remove any list entry with an identical DTMF sequence for overrides */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&menu->entries, cur, entry) {
		if (!strcasecmp(cur->dtmf, menu_entry->dtmf)) {
			AST_LIST_REMOVE_CURRENT(entry);
			ast_free(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_LIST_INSERT_TAIL(&menu->entries, menu_entry, entry);

	return 0;
}

static char *complete_user_profile_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;
	struct user_profile *u_profile = NULL;
	RAII_VAR(struct confbridge_cfg *, cfg, ao2_global_obj_ref(cfg_handle), ao2_cleanup);

	if (!cfg) {
		return NULL;
	}

	i = ao2_iterator_init(cfg->user_profiles, 0);
	while ((u_profile = ao2_iterator_next(&i))) {
		if (!strncasecmp(u_profile->name, word, wordlen) && ++which > state) {
			res = ast_strdup(u_profile->name);
			ao2_ref(u_profile, -1);
			break;
		}
		ao2_ref(u_profile, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_show_user_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator it;
	struct user_profile *u_profile;
	RAII_VAR(struct confbridge_cfg *, cfg, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile users";
		e->usage =
			"Usage confbridge show profile users\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!(cfg = ao2_global_obj_ref(cfg_handle))) {
		return NULL;
	}

	ast_cli(a->fd,"--------- User Profiles -----------\n");
	ao2_lock(cfg->user_profiles);
	it = ao2_iterator_init(cfg->user_profiles, 0);
	while ((u_profile = ao2_iterator_next(&it))) {
		ast_cli(a->fd,"%s\n", u_profile->name);
		ao2_ref(u_profile, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_unlock(cfg->user_profiles);

	return CLI_SUCCESS;
}
static char *handle_cli_confbridge_show_user_profile(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct user_profile u_profile;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile user";
		e->usage =
			"Usage confbridge show profile user [<profile name>]\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return complete_user_profile_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!(conf_find_user_profile(NULL, a->argv[4], &u_profile))) {
		ast_cli(a->fd, "No conference user profile named '%s' found!\n", a->argv[4]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd,"--------------------------------------------\n");
	ast_cli(a->fd,"Name:                    %s\n",
		u_profile.name);
	ast_cli(a->fd,"Admin:                   %s\n",
		u_profile.flags & USER_OPT_ADMIN ?
		"true" : "false");
	ast_cli(a->fd,"Marked User:             %s\n",
		u_profile.flags & USER_OPT_MARKEDUSER ?
		"true" : "false");
	ast_cli(a->fd,"Start Muted:             %s\n",
		u_profile.flags & USER_OPT_STARTMUTED?
		"true" : "false");
	ast_cli(a->fd,"MOH When Empty:          %s\n",
		u_profile.flags & USER_OPT_MUSICONHOLD ?
		"enabled" : "disabled");
	ast_cli(a->fd,"MOH Class:               %s\n",
		ast_strlen_zero(u_profile.moh_class) ?
		"default" : u_profile.moh_class);
	ast_cli(a->fd,"Announcement:            %s\n",
		u_profile.announcement);
	ast_cli(a->fd,"Quiet:                   %s\n",
		u_profile.flags & USER_OPT_QUIET ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Wait Marked:             %s\n",
		u_profile.flags & USER_OPT_WAITMARKED ?
		"enabled" : "disabled");
	ast_cli(a->fd,"END Marked:              %s\n",
		u_profile.flags & USER_OPT_ENDMARKED ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Drop_silence:            %s\n",
		u_profile.flags & USER_OPT_DROP_SILENCE ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Silence Threshold:       %ums\n",
		u_profile.silence_threshold);
	ast_cli(a->fd,"Talking Threshold:       %ums\n",
		u_profile.talking_threshold);
	ast_cli(a->fd,"Denoise:                 %s\n",
		u_profile.flags & USER_OPT_DENOISE ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Jitterbuffer:            %s\n",
		u_profile.flags & USER_OPT_JITTERBUFFER ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Talk Detect Events:      %s\n",
		u_profile.flags & USER_OPT_TALKER_DETECT ?
		"enabled" : "disabled");
	ast_cli(a->fd,"DTMF Pass Through:       %s\n",
		u_profile.flags & USER_OPT_DTMF_PASS ?
		"enabled" : "disabled");
	ast_cli(a->fd,"PIN:                     %s\n",
		ast_strlen_zero(u_profile.pin) ?
		"None" : u_profile.pin);
	ast_cli(a->fd,"Announce User Count:     %s\n",
		u_profile.flags & USER_OPT_ANNOUNCEUSERCOUNT ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Announce join/leave:     %s\n",
		u_profile.flags & (USER_OPT_ANNOUNCE_JOIN_LEAVE | USER_OPT_ANNOUNCE_JOIN_LEAVE_REVIEW) ?
		u_profile.flags & USER_OPT_ANNOUNCE_JOIN_LEAVE_REVIEW ?
		"enabled (with review)" : "enabled" : "disabled");
	ast_cli(a->fd,"Announce User Count all: %s\n",
		u_profile.flags & USER_OPT_ANNOUNCEUSERCOUNTALL ?
		"enabled" : "disabled");
		ast_cli(a->fd,"\n");

	return CLI_SUCCESS;
}

static char *complete_bridge_profile_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;
	struct bridge_profile *b_profile = NULL;
	RAII_VAR(struct confbridge_cfg *, cfg, ao2_global_obj_ref(cfg_handle), ao2_cleanup);

	if (!cfg) {
		return NULL;
	}

	i = ao2_iterator_init(cfg->bridge_profiles, 0);
	while ((b_profile = ao2_iterator_next(&i))) {
		if (!strncasecmp(b_profile->name, word, wordlen) && ++which > state) {
			res = ast_strdup(b_profile->name);
			ao2_ref(b_profile, -1);
			break;
		}
		ao2_ref(b_profile, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_show_bridge_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator it;
	struct bridge_profile *b_profile;
	RAII_VAR(struct confbridge_cfg *, cfg, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile bridges";
		e->usage =
			"Usage confbridge show profile bridges\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!(cfg = ao2_global_obj_ref(cfg_handle))) {
		return NULL;
	}

	ast_cli(a->fd,"--------- Bridge Profiles -----------\n");
	ao2_lock(cfg->bridge_profiles);
	it = ao2_iterator_init(cfg->bridge_profiles, 0);
	while ((b_profile = ao2_iterator_next(&it))) {
		ast_cli(a->fd,"%s\n", b_profile->name);
		ao2_ref(b_profile, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_unlock(cfg->bridge_profiles);

	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_show_bridge_profile(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct bridge_profile b_profile;
	char tmp[64];

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile bridge";
		e->usage =
			"Usage confbridge show profile bridge <profile name>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return complete_bridge_profile_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!(conf_find_bridge_profile(NULL, a->argv[4], &b_profile))) {
		ast_cli(a->fd, "No conference bridge profile named '%s' found!\n", a->argv[4]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd,"--------------------------------------------\n");
	ast_cli(a->fd,"Name:                 %s\n", b_profile.name);
	ast_cli(a->fd,"Language:             %s\n", b_profile.language);

	if (b_profile.internal_sample_rate) {
		snprintf(tmp, sizeof(tmp), "%u", b_profile.internal_sample_rate);
	} else {
		ast_copy_string(tmp, "auto", sizeof(tmp));
	}
	ast_cli(a->fd,"Internal Sample Rate: %s\n", tmp);

	if (b_profile.mix_interval) {
		ast_cli(a->fd,"Mixing Interval:      %u\n", b_profile.mix_interval);
	} else {
		ast_cli(a->fd,"Mixing Interval:      Default 20ms\n");
	}

	ast_cli(a->fd,"Record Conference:    %s\n",
		b_profile.flags & BRIDGE_OPT_RECORD_CONFERENCE ?
		"yes" : "no");

	ast_cli(a->fd,"Record File Append:    %s\n",
		b_profile.flags & BRIDGE_OPT_RECORD_FILE_APPEND ?
		"yes" : "no");

	ast_cli(a->fd,"Record File:          %s\n",
		ast_strlen_zero(b_profile.rec_file) ? "Auto Generated" :
		b_profile.rec_file);

	if (b_profile.max_members) {
		ast_cli(a->fd,"Max Members:          %u\n", b_profile.max_members);
	} else {
		ast_cli(a->fd,"Max Members:          No Limit\n");
	}

	switch (b_profile.flags
		& (BRIDGE_OPT_VIDEO_SRC_LAST_MARKED | BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED
			| BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER)) {
	case BRIDGE_OPT_VIDEO_SRC_LAST_MARKED:
		ast_cli(a->fd, "Video Mode:           last_marked\n");
		break;
	case BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED:
		ast_cli(a->fd, "Video Mode:           first_marked\n");
		break;
	case BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER:
		ast_cli(a->fd, "Video Mode:           follow_talker\n");
		break;
	case 0:
		ast_cli(a->fd, "Video Mode:           no video\n");
		break;
	default:
		/* Opps.  We have more than one video mode flag set. */
		ast_assert(0);
		break;
	}

	ast_cli(a->fd,"sound_only_person:    %s\n", conf_get_sound(CONF_SOUND_ONLY_PERSON, b_profile.sounds));
	ast_cli(a->fd,"sound_only_one:       %s\n", conf_get_sound(CONF_SOUND_ONLY_ONE, b_profile.sounds));
	ast_cli(a->fd,"sound_has_joined:     %s\n", conf_get_sound(CONF_SOUND_HAS_JOINED, b_profile.sounds));
	ast_cli(a->fd,"sound_has_left:       %s\n", conf_get_sound(CONF_SOUND_HAS_LEFT, b_profile.sounds));
	ast_cli(a->fd,"sound_kicked:         %s\n", conf_get_sound(CONF_SOUND_KICKED, b_profile.sounds));
	ast_cli(a->fd,"sound_muted:          %s\n", conf_get_sound(CONF_SOUND_MUTED, b_profile.sounds));
	ast_cli(a->fd,"sound_unmuted:        %s\n", conf_get_sound(CONF_SOUND_UNMUTED, b_profile.sounds));
	ast_cli(a->fd,"sound_there_are:      %s\n", conf_get_sound(CONF_SOUND_THERE_ARE, b_profile.sounds));
	ast_cli(a->fd,"sound_other_in_party: %s\n", conf_get_sound(CONF_SOUND_OTHER_IN_PARTY, b_profile.sounds));
	ast_cli(a->fd,"sound_place_into_conference: %s\n", conf_get_sound(CONF_SOUND_PLACE_IN_CONF, b_profile.sounds));
	ast_cli(a->fd,"sound_wait_for_leader:       %s\n", conf_get_sound(CONF_SOUND_WAIT_FOR_LEADER, b_profile.sounds));
	ast_cli(a->fd,"sound_leader_has_left:       %s\n", conf_get_sound(CONF_SOUND_LEADER_HAS_LEFT, b_profile.sounds));
	ast_cli(a->fd,"sound_get_pin:        %s\n", conf_get_sound(CONF_SOUND_GET_PIN, b_profile.sounds));
	ast_cli(a->fd,"sound_invalid_pin:    %s\n", conf_get_sound(CONF_SOUND_INVALID_PIN, b_profile.sounds));
	ast_cli(a->fd,"sound_locked:         %s\n", conf_get_sound(CONF_SOUND_LOCKED, b_profile.sounds));
	ast_cli(a->fd,"sound_unlocked_now:   %s\n", conf_get_sound(CONF_SOUND_UNLOCKED_NOW, b_profile.sounds));
	ast_cli(a->fd,"sound_lockednow:      %s\n", conf_get_sound(CONF_SOUND_LOCKED_NOW, b_profile.sounds));
	ast_cli(a->fd,"sound_error_menu:     %s\n", conf_get_sound(CONF_SOUND_ERROR_MENU, b_profile.sounds));
	ast_cli(a->fd,"sound_join:           %s\n", conf_get_sound(CONF_SOUND_JOIN, b_profile.sounds));
	ast_cli(a->fd,"sound_leave:          %s\n", conf_get_sound(CONF_SOUND_LEAVE, b_profile.sounds));
	ast_cli(a->fd,"sound_participants_muted:     %s\n", conf_get_sound(CONF_SOUND_PARTICIPANTS_MUTED, b_profile.sounds));
	ast_cli(a->fd,"sound_participants_unmuted:     %s\n", conf_get_sound(CONF_SOUND_PARTICIPANTS_UNMUTED, b_profile.sounds));
	ast_cli(a->fd,"sound_begin:          %s\n", conf_get_sound(CONF_SOUND_BEGIN, b_profile.sounds));
	ast_cli(a->fd,"\n");

	conf_bridge_profile_destroy(&b_profile);
	return CLI_SUCCESS;
}

static char *complete_menu_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;
	struct conf_menu *menu = NULL;
	RAII_VAR(struct confbridge_cfg *, cfg, ao2_global_obj_ref(cfg_handle), ao2_cleanup);

	if (!cfg) {
		return NULL;
	}

	i = ao2_iterator_init(cfg->menus, 0);
	while ((menu = ao2_iterator_next(&i))) {
		if (!strncasecmp(menu->name, word, wordlen) && ++which > state) {
			res = ast_strdup(menu->name);
			ao2_ref(menu, -1);
			break;
		}
		ao2_ref(menu, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_show_menus(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator it;
	struct conf_menu *menu;
	RAII_VAR(struct confbridge_cfg *, cfg, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show menus";
		e->usage =
			"Usage confbridge show profile menus\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!(cfg = ao2_global_obj_ref(cfg_handle))) {
		return NULL;
	}

	ast_cli(a->fd,"--------- Menus -----------\n");
	ao2_lock(cfg->menus);
	it = ao2_iterator_init(cfg->menus, 0);
	while ((menu = ao2_iterator_next(&it))) {
		ast_cli(a->fd,"%s\n", menu->name);
		ao2_ref(menu, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_unlock(cfg->menus);

	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_show_menu(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct conf_menu *, menu, NULL, ao2_cleanup);
	RAII_VAR(struct confbridge_cfg *, cfg, NULL, ao2_cleanup);
	struct conf_menu_entry *menu_entry = NULL;
	struct conf_menu_action *menu_action = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show menu";
		e->usage =
			"Usage confbridge show menu [<menu name>]\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_menu_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(cfg = ao2_global_obj_ref(cfg_handle))) {
		return NULL;
	}

	if (!(menu = menu_find(cfg->menus, a->argv[3]))) {
		ast_cli(a->fd, "No conference menu named '%s' found!\n", a->argv[3]);
		return CLI_SUCCESS;
	}
	ao2_lock(menu);

	ast_cli(a->fd,"Name: %s\n", menu->name);
	AST_LIST_TRAVERSE(&menu->entries, menu_entry, entry) {
		int action_num = 0;
		ast_cli(a->fd, "%s=", menu_entry->dtmf);
		AST_LIST_TRAVERSE(&menu_entry->actions, menu_action, action) {
			if (action_num) {
				ast_cli(a->fd, ", ");
			}
			switch (menu_action->id) {
			case MENU_ACTION_TOGGLE_MUTE:
				ast_cli(a->fd, "toggle_mute");
				break;
			case MENU_ACTION_NOOP:
				ast_cli(a->fd, "no_op");
				break;
			case MENU_ACTION_INCREASE_LISTENING:
				ast_cli(a->fd, "increase_listening_volume");
				break;
			case MENU_ACTION_DECREASE_LISTENING:
				ast_cli(a->fd, "decrease_listening_volume");
				break;
			case MENU_ACTION_RESET_LISTENING:
				ast_cli(a->fd, "reset_listening_volume");
				break;
			case MENU_ACTION_RESET_TALKING:
				ast_cli(a->fd, "reset_talking_volume");
				break;
			case MENU_ACTION_INCREASE_TALKING:
				ast_cli(a->fd, "increase_talking_volume");
				break;
			case MENU_ACTION_DECREASE_TALKING:
				ast_cli(a->fd, "decrease_talking_volume");
				break;
			case MENU_ACTION_PLAYBACK:
				ast_cli(a->fd, "playback(%s)", menu_action->data.playback_file);
				break;
			case MENU_ACTION_PLAYBACK_AND_CONTINUE:
				ast_cli(a->fd, "playback_and_continue(%s)", menu_action->data.playback_file);
				break;
			case MENU_ACTION_DIALPLAN_EXEC:
				ast_cli(a->fd, "dialplan_exec(%s,%s,%d)",
					menu_action->data.dialplan_args.context,
					menu_action->data.dialplan_args.exten,
					menu_action->data.dialplan_args.priority);
				break;
			case MENU_ACTION_ADMIN_TOGGLE_LOCK:
				ast_cli(a->fd, "admin_toggle_conference_lock");
				break;
			case MENU_ACTION_ADMIN_TOGGLE_MUTE_PARTICIPANTS:
				ast_cli(a->fd, "admin_toggle_mute_participants");
				break;
			case MENU_ACTION_PARTICIPANT_COUNT:
				ast_cli(a->fd, "participant_count");
				break;
			case MENU_ACTION_ADMIN_KICK_LAST:
				ast_cli(a->fd, "admin_kick_last");
				break;
			case MENU_ACTION_LEAVE:
				ast_cli(a->fd, "leave_conference");
				break;
			case MENU_ACTION_SET_SINGLE_VIDEO_SRC:
				ast_cli(a->fd, "set_as_single_video_src");
				break;
			case MENU_ACTION_RELEASE_SINGLE_VIDEO_SRC:
				ast_cli(a->fd, "release_as_single_video_src");
				break;
			}
			action_num++;
		}
		ast_cli(a->fd,"\n");
	}


	ao2_unlock(menu);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_confbridge_parser[] = {
	AST_CLI_DEFINE(handle_cli_confbridge_show_user_profile, "Show a conference user profile."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_bridge_profile, "Show a conference bridge profile."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_menu, "Show a conference menu"),
	AST_CLI_DEFINE(handle_cli_confbridge_show_user_profiles, "Show a list of conference user profiles."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_bridge_profiles, "Show a list of conference bridge profiles."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_menus, "Show a list of conference menus"),

};

static void confbridge_cfg_destructor(void *obj)
{
	struct confbridge_cfg *cfg = obj;
	ao2_cleanup(cfg->user_profiles);
	ao2_cleanup(cfg->bridge_profiles);
	ao2_cleanup(cfg->menus);
}

void *confbridge_cfg_alloc(void)
{
	struct confbridge_cfg *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), confbridge_cfg_destructor))) {
		return NULL;
	}

	if (!(cfg->user_profiles = ao2_container_alloc(283, user_hash_cb, user_cmp_cb))) {
		goto error;
	}

	if (!(cfg->bridge_profiles = ao2_container_alloc(283, bridge_hash_cb, bridge_cmp_cb))) {
		goto error;
	}

	if (!(cfg->menus = ao2_container_alloc(283, menu_hash_cb, menu_cmp_cb))) {
		goto error;
	}

	return cfg;
error:
	ao2_ref(cfg, -1);
	return NULL;
}

static int announce_user_count_all_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct user_profile *u_profile = obj;

	if (strcasecmp(var->name, "announce_user_count_all")) {
		return -1;
	}
	if (ast_true(var->value)) {
		u_profile->flags = u_profile->flags | USER_OPT_ANNOUNCEUSERCOUNTALL;
	} else if (ast_false(var->value)) {
		u_profile->flags = u_profile->flags & ~USER_OPT_ANNOUNCEUSERCOUNTALL;
	} else if (sscanf(var->value, "%30u", &u_profile->announce_user_count_all_after) == 1) {
		u_profile->flags = u_profile->flags | USER_OPT_ANNOUNCEUSERCOUNTALL;
	} else {
		return -1;
	}
	return 0;
}

static int mix_interval_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct bridge_profile *b_profile = obj;

	if (strcasecmp(var->name, "mixing_interval")) {
		return -1;
	}
	if (sscanf(var->value, "%30u", &b_profile->mix_interval) != 1) {
		return -1;
	}
	switch (b_profile->mix_interval) {
	case 10:
	case 20:
	case 40:
	case 80:
		return 0;
	default:
		return -1;
	}
}

static int video_mode_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct bridge_profile *b_profile = obj;

	if (strcasecmp(var->name, "video_mode")) {
		return -1;
	}
	if (!strcasecmp(var->value, "first_marked")) {
		ast_set_flags_to(b_profile,
			BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_LAST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER,
			BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED);
	} else if (!strcasecmp(var->value, "last_marked")) {
		ast_set_flags_to(b_profile,
			BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_LAST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER,
			BRIDGE_OPT_VIDEO_SRC_LAST_MARKED);
	} else if (!strcasecmp(var->value, "follow_talker")) {
		ast_set_flags_to(b_profile,
			BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_LAST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER,
			BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER);
	} else if (!strcasecmp(var->value, "none")) {
		ast_clear_flag(b_profile,
			BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_LAST_MARKED
				| BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER);
	} else {
		return -1;
	}
	return 0;
}

static int user_template_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct user_profile *u_profile = obj;

	return conf_find_user_profile(NULL, var->value, u_profile) ? 0 : -1;
}

static int bridge_template_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct bridge_profile *b_profile = obj;
	struct bridge_profile_sounds *sounds = bridge_profile_sounds_alloc();
	struct bridge_profile_sounds *oldsounds = b_profile->sounds;

	if (!sounds) {
		return -1;
	}
	if (!(conf_find_bridge_profile(NULL, var->value, b_profile))) {
		ao2_ref(sounds, -1);
		return -1;
	}
	/* Using a bridge profile as a template is a little complicated due to the sounds. Since the sounds
	 * structure of a dynamic profile will need to be altered, a completely new sounds structure must be
	 * created instead of simply holding a reference to the one built by the config file. */
	ast_string_field_set(sounds, onlyperson, b_profile->sounds->onlyperson);
	ast_string_field_set(sounds, onlyone, b_profile->sounds->onlyone);
	ast_string_field_set(sounds, hasjoin, b_profile->sounds->hasjoin);
	ast_string_field_set(sounds, hasleft, b_profile->sounds->hasleft);
	ast_string_field_set(sounds, kicked, b_profile->sounds->kicked);
	ast_string_field_set(sounds, muted, b_profile->sounds->muted);
	ast_string_field_set(sounds, unmuted, b_profile->sounds->unmuted);
	ast_string_field_set(sounds, thereare, b_profile->sounds->thereare);
	ast_string_field_set(sounds, otherinparty, b_profile->sounds->otherinparty);
	ast_string_field_set(sounds, placeintoconf, b_profile->sounds->placeintoconf);
	ast_string_field_set(sounds, waitforleader, b_profile->sounds->waitforleader);
	ast_string_field_set(sounds, leaderhasleft, b_profile->sounds->leaderhasleft);
	ast_string_field_set(sounds, getpin, b_profile->sounds->getpin);
	ast_string_field_set(sounds, invalidpin, b_profile->sounds->invalidpin);
	ast_string_field_set(sounds, locked, b_profile->sounds->locked);
	ast_string_field_set(sounds, unlockednow, b_profile->sounds->unlockednow);
	ast_string_field_set(sounds, lockednow, b_profile->sounds->lockednow);
	ast_string_field_set(sounds, errormenu, b_profile->sounds->errormenu);
	ast_string_field_set(sounds, join, b_profile->sounds->join);
	ast_string_field_set(sounds, leave, b_profile->sounds->leave);
	ast_string_field_set(sounds, participantsmuted, b_profile->sounds->participantsmuted);
	ast_string_field_set(sounds, participantsunmuted, b_profile->sounds->participantsunmuted);
	ast_string_field_set(sounds, begin, b_profile->sounds->begin);

	ao2_ref(b_profile->sounds, -1); /* sounds struct copied over to it from the template by reference only. */
	ao2_ref(oldsounds, -1);    /* original sounds struct we don't need anymore */
	b_profile->sounds = sounds;     /* the new sounds struct that is a deep copy of the one from the template. */

	return 0;
}

static int copy_menu_entry(struct conf_menu_entry *dst, struct conf_menu_entry *src)
{
	struct conf_menu_action *menu_action;
	struct conf_menu_action *new_menu_action;

	ast_copy_string(dst->dtmf, src->dtmf, sizeof(dst->dtmf));
	AST_LIST_HEAD_INIT_NOLOCK(&dst->actions);

	AST_LIST_TRAVERSE(&src->actions, menu_action, action) {
		if (!(new_menu_action = ast_calloc(1, sizeof(*new_menu_action)))) {
			return -1;
		}
		memcpy(new_menu_action, menu_action, sizeof(*new_menu_action));
		AST_LIST_NEXT(new_menu_action, action) = NULL;
		AST_LIST_INSERT_TAIL(&dst->actions, new_menu_action, action);
	}

	return 0;
}

static int conf_menu_profile_copy(struct conf_menu *dst, struct conf_menu *src)
{
	/* Copy each menu item to the dst struct */
	struct conf_menu_entry *cur;

	AST_LIST_TRAVERSE(&src->entries, cur, entry) {
		struct conf_menu_entry *cpy;

		if (!(cpy = ast_calloc(1, sizeof(*cpy)))) {
			return -1;
		}

		if (copy_menu_entry(cpy, cur)) {
			conf_menu_entry_destroy(cpy);
			ast_free(cpy);
			return -1;
		}
		AST_LIST_INSERT_TAIL(&dst->entries, cpy, entry);
	}

	return 0;
}

static int menu_template_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct conf_menu *dst_menu = obj;
	struct confbridge_cfg *cfg = aco_pending_config(&cfg_info);
	RAII_VAR(struct conf_menu *, src_menu, NULL, ao2_cleanup);

	if (!cfg) {
		return 0;
	}

	if (!(src_menu = ao2_find(cfg->menus, var->value, OBJ_KEY))) {
		return -1;
	}

	if (conf_menu_profile_copy(dst_menu, src_menu)) {
		return -1;
	}

	return 0;
}

static int sound_option_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	set_sound(var->name, var->value, obj);
	return 0;
}

static int menu_option_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	add_menu_entry(obj, var->name, var->value);
	return 0;
}

static int verify_default_profiles(void)
{
	RAII_VAR(struct user_profile *, user_profile, NULL, ao2_cleanup);
	RAII_VAR(struct bridge_profile *, bridge_profile, NULL, ao2_cleanup);
	RAII_VAR(struct conf_menu *, menu_profile, NULL, ao2_cleanup);
	struct confbridge_cfg *cfg = aco_pending_config(&cfg_info);

	if (!cfg) {
		return 0;
	}

	bridge_profile = ao2_find(cfg->bridge_profiles, DEFAULT_BRIDGE_PROFILE, OBJ_KEY);
	if (!bridge_profile) {
		bridge_profile = bridge_profile_alloc(DEFAULT_BRIDGE_PROFILE);
		if (!bridge_profile) {
			return -1;
		}
		ast_log(AST_LOG_NOTICE, "Adding %s profile to app_confbridge\n", DEFAULT_BRIDGE_PROFILE);
		aco_set_defaults(&bridge_type, DEFAULT_BRIDGE_PROFILE, bridge_profile);
		ao2_link(cfg->bridge_profiles, bridge_profile);
	}

	user_profile = ao2_find(cfg->user_profiles, DEFAULT_USER_PROFILE, OBJ_KEY);
	if (!user_profile) {
		user_profile = user_profile_alloc(DEFAULT_USER_PROFILE);
		if (!user_profile) {
			return -1;
		}
		ast_log(AST_LOG_NOTICE, "Adding %s profile to app_confbridge\n", DEFAULT_USER_PROFILE);
		aco_set_defaults(&user_type, DEFAULT_USER_PROFILE, user_profile);
		ao2_link(cfg->user_profiles, user_profile);
	}

	menu_profile = ao2_find(cfg->menus, DEFAULT_MENU_PROFILE, OBJ_KEY);
	if (!menu_profile) {
		menu_profile = menu_alloc(DEFAULT_MENU_PROFILE);
		if (!menu_profile) {
			return -1;
		}
		ast_log(AST_LOG_NOTICE, "Adding %s menu to app_confbridge\n", DEFAULT_MENU_PROFILE);
		aco_set_defaults(&menu_type, DEFAULT_MENU_PROFILE, menu_profile);
		ao2_link(cfg->menus, menu_profile);
	}

	return 0;
}

int conf_load_config(void)
{
	if (aco_info_init(&cfg_info)) {
		return -1;
	}

	/* User options */
	aco_option_register(&cfg_info, "type", ACO_EXACT, user_types, NULL, OPT_NOOP_T, 0, 0);
	aco_option_register(&cfg_info, "admin", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_ADMIN);
	aco_option_register(&cfg_info, "marked", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_MARKEDUSER);
	aco_option_register(&cfg_info, "startmuted", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_STARTMUTED);
	aco_option_register(&cfg_info, "music_on_hold_when_empty", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_MUSICONHOLD);
	aco_option_register(&cfg_info, "quiet", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_QUIET);
	aco_option_register_custom(&cfg_info, "announce_user_count_all", ACO_EXACT, user_types, "no", announce_user_count_all_handler, 0);
	aco_option_register(&cfg_info, "announce_user_count", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_ANNOUNCEUSERCOUNT);
	/* Negative logic. Defaults to "yes" and evaluates with ast_false(). If !ast_false(), USER_OPT_NOONLYPERSON is cleared */
	aco_option_register(&cfg_info, "announce_only_user", ACO_EXACT, user_types, "yes", OPT_BOOLFLAG_T, 0, FLDSET(struct user_profile, flags), USER_OPT_NOONLYPERSON);
	aco_option_register(&cfg_info, "wait_marked", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_WAITMARKED);
	aco_option_register(&cfg_info, "end_marked", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_ENDMARKED);
	aco_option_register(&cfg_info, "talk_detection_events", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_TALKER_DETECT);
	aco_option_register(&cfg_info, "dtmf_passthrough", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_DTMF_PASS);
	aco_option_register(&cfg_info, "announce_join_leave", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_ANNOUNCE_JOIN_LEAVE);
	aco_option_register(&cfg_info, "announce_join_leave_review", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_ANNOUNCE_JOIN_LEAVE_REVIEW);
	aco_option_register(&cfg_info, "pin", ACO_EXACT, user_types, NULL, OPT_CHAR_ARRAY_T, 0, CHARFLDSET(struct user_profile, pin));
	aco_option_register(&cfg_info, "music_on_hold_class", ACO_EXACT, user_types, NULL, OPT_CHAR_ARRAY_T, 0, CHARFLDSET(struct user_profile, moh_class));
	aco_option_register(&cfg_info, "announcement", ACO_EXACT, user_types, NULL, OPT_CHAR_ARRAY_T, 0, CHARFLDSET(struct user_profile, announcement));
	aco_option_register(&cfg_info, "denoise", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_DENOISE);
	aco_option_register(&cfg_info, "dsp_drop_silence", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_DROP_SILENCE);
	aco_option_register(&cfg_info, "dsp_silence_threshold", ACO_EXACT, user_types, __stringify(DEFAULT_SILENCE_THRESHOLD), OPT_UINT_T, 0, FLDSET(struct user_profile, silence_threshold));
	aco_option_register(&cfg_info, "dsp_talking_threshold", ACO_EXACT, user_types, __stringify(DEFAULT_TALKING_THRESHOLD), OPT_UINT_T, 0, FLDSET(struct user_profile, talking_threshold));
	aco_option_register(&cfg_info, "jitterbuffer", ACO_EXACT, user_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct user_profile, flags), USER_OPT_JITTERBUFFER);
	/* This option should only be used with the CONFBRIDGE dialplan function */
	aco_option_register_custom(&cfg_info, "template", ACO_EXACT, user_types, NULL, user_template_handler, 0);

/* XXX ASTERISK-21271 need a user supplied bridge merge_priority to merge ConfBridges (default = 1, range 1-INT_MAX) */
	/* Bridge options */
	aco_option_register(&cfg_info, "type", ACO_EXACT, bridge_types, NULL, OPT_NOOP_T, 0, 0);
	aco_option_register(&cfg_info, "jitterbuffer", ACO_EXACT, bridge_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct bridge_profile, flags), USER_OPT_JITTERBUFFER);
	/* "auto" will fail to parse as a uint, but we use PARSE_DEFAULT to set the value to 0 in that case, which is the value that auto resolves to */
	aco_option_register(&cfg_info, "internal_sample_rate", ACO_EXACT, bridge_types, "0", OPT_UINT_T, PARSE_DEFAULT, FLDSET(struct bridge_profile, internal_sample_rate), 0);
	aco_option_register_custom(&cfg_info, "mixing_interval", ACO_EXACT, bridge_types, "20", mix_interval_handler, 0);
	aco_option_register(&cfg_info, "record_conference", ACO_EXACT, bridge_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct bridge_profile, flags), BRIDGE_OPT_RECORD_CONFERENCE);
	aco_option_register_custom(&cfg_info, "video_mode", ACO_EXACT, bridge_types, NULL, video_mode_handler, 0);
	aco_option_register(&cfg_info, "record_file_append", ACO_EXACT, bridge_types, "yes", OPT_BOOLFLAG_T, 1, FLDSET(struct bridge_profile, flags), BRIDGE_OPT_RECORD_FILE_APPEND);
	aco_option_register(&cfg_info, "max_members", ACO_EXACT, bridge_types, "0", OPT_UINT_T, 0, FLDSET(struct bridge_profile, max_members));
	aco_option_register(&cfg_info, "record_file", ACO_EXACT, bridge_types, NULL, OPT_CHAR_ARRAY_T, 0, CHARFLDSET(struct bridge_profile, rec_file));
	aco_option_register(&cfg_info, "language", ACO_EXACT, bridge_types, "en", OPT_CHAR_ARRAY_T, 0, CHARFLDSET(struct bridge_profile, language));
	aco_option_register_custom(&cfg_info, "^sound_", ACO_REGEX, bridge_types, NULL, sound_option_handler, 0);
	/* This option should only be used with the CONFBRIDGE dialplan function */
	aco_option_register_custom(&cfg_info, "template", ACO_EXACT, bridge_types, NULL, bridge_template_handler, 0);

	/* Menu options */
	aco_option_register(&cfg_info, "type", ACO_EXACT, menu_types, NULL, OPT_NOOP_T, 0, 0);
	aco_option_register_custom(&cfg_info, "template", ACO_EXACT, menu_types, NULL, menu_template_handler, 0);
	aco_option_register_custom(&cfg_info, "^[0-9A-D*#]+$", ACO_REGEX, menu_types, NULL, menu_option_handler, 0);

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		goto error;
	}

	if (ast_cli_register_multiple(cli_confbridge_parser, ARRAY_LEN(cli_confbridge_parser))) {
		goto error;
	}

	return 0;
error:
	conf_destroy_config();
	return -1;
}

int conf_reload_config(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		/* On a reload, just keep the config we already have in place. */
		return -1;
	}
	return 0;
}

static void conf_user_profile_copy(struct user_profile *dst, struct user_profile *src)
{
	*dst = *src;
}

const struct user_profile *conf_find_user_profile(struct ast_channel *chan, const char *user_profile_name, struct user_profile *result)
{
	struct user_profile *tmp2;
	struct ast_datastore *datastore = NULL;
	struct func_confbridge_data *b_data = NULL;
	RAII_VAR(struct confbridge_cfg *, cfg, ao2_global_obj_ref(cfg_handle), ao2_cleanup);

	if (chan && ast_strlen_zero(user_profile_name)) {
		ast_channel_lock(chan);
		datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL);
		ast_channel_unlock(chan);
		if (datastore) {
			b_data = datastore->data;
			if (b_data->u_usable) {
				conf_user_profile_copy(result, &b_data->u_profile);
				return result;
			}
		}
	}

	if (!cfg) {
		return NULL;
	}
	if (ast_strlen_zero(user_profile_name)) {
		user_profile_name = DEFAULT_USER_PROFILE;
	}
	if (!(tmp2 = ao2_find(cfg->user_profiles, user_profile_name, OBJ_KEY))) {
		return NULL;
	}
	ao2_lock(tmp2);
	conf_user_profile_copy(result, tmp2);
	ao2_unlock(tmp2);
	ao2_ref(tmp2, -1);

	return result;
}

void conf_bridge_profile_copy(struct bridge_profile *dst, struct bridge_profile *src)
{
	*dst = *src;
	if (src->sounds) {
		ao2_ref(src->sounds, +1);
	}
}

void conf_bridge_profile_destroy(struct bridge_profile *b_profile)
{
	if (b_profile->sounds) {
		ao2_ref(b_profile->sounds, -1);
		b_profile->sounds = NULL;
	}
}

const struct bridge_profile *conf_find_bridge_profile(struct ast_channel *chan, const char *bridge_profile_name, struct bridge_profile *result)
{
	struct bridge_profile *tmp2;
	struct ast_datastore *datastore = NULL;
	struct func_confbridge_data *b_data = NULL;
	RAII_VAR(struct confbridge_cfg *, cfg, ao2_global_obj_ref(cfg_handle), ao2_cleanup);

	if (chan && ast_strlen_zero(bridge_profile_name)) {
		ast_channel_lock(chan);
		datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL);
		ast_channel_unlock(chan);
		if (datastore) {
			b_data = datastore->data;
			if (b_data->b_usable) {
				conf_bridge_profile_copy(result, &b_data->b_profile);
				return result;
			}
		}
	}

	if (!cfg) {
		return NULL;
	}
	if (ast_strlen_zero(bridge_profile_name)) {
		bridge_profile_name = DEFAULT_BRIDGE_PROFILE;
	}
	if (!(tmp2 = ao2_find(cfg->bridge_profiles, bridge_profile_name, OBJ_KEY))) {
		return NULL;
	}
	ao2_lock(tmp2);
	conf_bridge_profile_copy(result, tmp2);
	ao2_unlock(tmp2);
	ao2_ref(tmp2, -1);

	return result;
}

struct dtmf_menu_hook_pvt {
	struct confbridge_user *user;
	struct conf_menu_entry menu_entry;
	struct conf_menu *menu;
};

static void menu_hook_destroy(void *hook_pvt)
{
	struct dtmf_menu_hook_pvt *pvt = hook_pvt;
	struct conf_menu_action *action = NULL;

	ao2_cleanup(pvt->menu);

	while ((action = AST_LIST_REMOVE_HEAD(&pvt->menu_entry.actions, action))) {
		ast_free(action);
	}
	ast_free(pvt);
}

static int menu_hook_callback(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct dtmf_menu_hook_pvt *pvt = hook_pvt;

	return conf_handle_dtmf(bridge_channel, pvt->user, &pvt->menu_entry, pvt->menu);
}

void conf_menu_entry_destroy(struct conf_menu_entry *menu_entry)
{
	struct conf_menu_action *menu_action = NULL;
	while ((menu_action = AST_LIST_REMOVE_HEAD(&menu_entry->actions, action))) {
		ast_free(menu_action);
	}
}

int conf_find_menu_entry_by_sequence(const char *dtmf_sequence, struct conf_menu *menu, struct conf_menu_entry *result)
{
	struct conf_menu_entry *menu_entry = NULL;

	ao2_lock(menu);
	AST_LIST_TRAVERSE(&menu->entries, menu_entry, entry) {
		if (!strcasecmp(menu_entry->dtmf, dtmf_sequence)) {
			copy_menu_entry(result, menu_entry);
			ao2_unlock(menu);
			return 1;
		}
	}
	ao2_unlock(menu);

	return 0;
}

static int apply_menu_to_user(struct confbridge_user *user, struct conf_menu *menu)
{
	struct conf_menu_entry *menu_entry;

	SCOPED_AO2LOCK(menu_lock, menu);
	AST_LIST_TRAVERSE(&menu->entries, menu_entry, entry) {
		struct dtmf_menu_hook_pvt *pvt;

		if (!(pvt = ast_calloc(1, sizeof(*pvt)))) {
			return -1;
		}
		pvt->user = user;
		pvt->menu = ao2_bump(menu);

		if (copy_menu_entry(&pvt->menu_entry, menu_entry)) {
			menu_hook_destroy(pvt);
			return -1;
		}

		if (ast_bridge_dtmf_hook(&user->features, pvt->menu_entry.dtmf,
			menu_hook_callback, pvt, menu_hook_destroy, 0)) {
			menu_hook_destroy(pvt);
		}
	}
	strcpy(user->menu_name, menu->name); /* Safe */

	return 0;
}

int conf_set_menu_to_user(struct ast_channel *chan, struct confbridge_user *user, const char *menu_profile_name)
{
	RAII_VAR(struct confbridge_cfg *, cfg, ao2_global_obj_ref(cfg_handle), ao2_cleanup);
	RAII_VAR(struct conf_menu *, menu, NULL, ao2_cleanup);

	if (chan && ast_strlen_zero(menu_profile_name)) {
		struct ast_datastore *datastore;
		struct func_confbridge_data *b_data;

		ast_channel_lock(chan);
		datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL);
		ast_channel_unlock(chan);
		if (datastore) {
			/* If a menu exists in the CONFBRIDGE function datastore, use it. */
			b_data = datastore->data;
			if (b_data->m_usable) {
				menu = ao2_bump(b_data->menu);
				return apply_menu_to_user(user, menu);
			}
		}
	}

	/* Otherwise, we need to get whatever menu profile is specified to use (or default). */
	if (!cfg) {
		return -1;
	}

	if (ast_strlen_zero(menu_profile_name)) {
		menu_profile_name = DEFAULT_MENU_PROFILE;
	}

	if (!(menu = ao2_find(cfg->menus, menu_profile_name, OBJ_KEY))) {
		return -1;
	}

	return apply_menu_to_user(user, menu);
}

void conf_destroy_config(void)
{
	ast_cli_unregister_multiple(cli_confbridge_parser, ARRAY_LEN(cli_confbridge_parser));
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(cfg_handle);
}
