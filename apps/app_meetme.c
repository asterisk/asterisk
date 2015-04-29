/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * SLA Implementation by:
 * Russell Bryant <russell@digium.com>
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
 * \brief Meet me conference bridge and Shared Line Appearances
 *
 * \author Mark Spencer <markster@digium.com>
 * \author (SLA) Russell Bryant <russell@digium.com>
 * 
 * \ingroup applications
 */

/*! \li \ref app_meetme.c uses configuration file \ref meetme.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page meetme.conf meetme.conf
 * \verbinclude meetme.conf.sample
 */

/*** MODULEINFO
	<load_priority>devstate_provider</load_priority>
	<depend>dahdi</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
	<replacement>app_confbridge</replacement>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <dahdi/user.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/cli.h"
#include "asterisk/say.h"
#include "asterisk/utils.h"
#include "asterisk/translate.h"
#include "asterisk/ulaw.h"
#include "asterisk/astobj2.h"
#include "asterisk/devicestate.h"
#include "asterisk/dial.h"
#include "asterisk/causes.h"
#include "asterisk/paths.h"
#include "asterisk/data.h"
#include "asterisk/test.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/json.h"
#include "asterisk/format_compatibility.h"

#include "enter.h"
#include "leave.h"

/*** DOCUMENTATION
	<application name="MeetMe" language="en_US">
		<synopsis>
			MeetMe conference bridge.
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
					<option name="b">
						<para>Run AGI script specified in <variable>MEETME_AGI_BACKGROUND</variable>
						Default: <literal>conf-background.agi</literal>.</para>
						<note><para>This does not work with non-DAHDI channels in the same
						conference).</para></note>
					</option>
					<option name="c">
						<para>Announce user(s) count on joining a conference.</para>
					</option>
					<option name="C">
						<para>Continue in dialplan when kicked out of conference.</para>
					</option>
					<option name="d">
						<para>Dynamically add conference.</para>
					</option>
					<option name="D">
						<para>Dynamically add conference, prompting for a PIN.</para>
					</option>
					<option name="e">
						<para>Select an empty conference.</para>
					</option>
					<option name="E">
						<para>Select an empty pinless conference.</para>
					</option>
					<option name="F">
						<para>Pass DTMF through the conference.</para>
					</option>
					<option name="G">
						<argument name="x" required="true">
							<para>The file to playback</para>
						</argument>
						<para>Play an intro announcement in conference.</para>
					</option>
					<option name="i">
						<para>Announce user join/leave with review.</para>
					</option>
					<option name="I">
						<para>Announce user join/leave without review.</para>
					</option>
					<option name="k">
						<para>Close the conference if there's only one active participant left at exit.</para>
					</option>
					<option name="l">
						<para>Set listen only mode (Listen only, no talking).</para>
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
					<option name="n">
						<para>Disable the denoiser. By default, if <literal>func_speex</literal> is loaded, Asterisk
						will apply a denoiser to channels in the MeetMe conference. However, channel
						drivers that present audio with a varying rate will experience degraded
						performance with a denoiser attached. This parameter allows a channel joining
						the conference to choose not to have a denoiser attached without having to
						unload <literal>func_speex</literal>.</para>
					</option>
					<option name="o">
						<para>Set talker optimization - treats talkers who aren't speaking as
						being muted, meaning (a) No encode is done on transmission and (b)
						Received audio that is not registered as talking is omitted causing no
						buildup in background noise.</para>
					</option>
					<option name="p" hasparams="optional">
						<para>Allow user to exit the conference by pressing <literal>#</literal> (default)
						or any of the defined keys. Dial plan execution will continue at the next
						priority following MeetMe. The key used is set to channel variable
						<variable>MEETME_EXIT_KEY</variable>.</para>
						<argument name="keys" required="true" />
						<note>
							<para>Option <literal>s</literal> has priority for <literal>*</literal>
							since it cannot change its activation code.</para>
						</note>
					</option>
					<option name="P">
						<para>Always prompt for the pin even if it is specified.</para>
					</option>
					<option name="q">
						<para>Quiet mode (don't play enter/leave sounds).</para>
					</option>
					<option name="r">
						<para>Record conference (records as <variable>MEETME_RECORDINGFILE</variable>
						using format <variable>MEETME_RECORDINGFORMAT</variable>. Default filename is
						<literal>meetme-conf-rec-${CONFNO}-${UNIQUEID}</literal> and the default format is
						wav.</para>
					</option>
					<option name="s">
						<para>Present menu (user or admin) when <literal>*</literal> is received
						(send to menu).</para>
					</option>
					<option name="t">
						<para>Set talk only mode. (Talk only, no listening).</para>
					</option>
					<option name="T">
						<para>Set talker detection (sent to manager interface and meetme list).</para>
					</option>
					<option name="v" hasparams="optional">
						<para>Announce when a user is joining or leaving the conference.  Use the voicemail greeting as the announcement.
						 If the i or I options are set, the application will fall back to them if no voicemail greeting can be found.</para>
						<argument name="mailbox@[context]" required="true">
							<para>The mailbox and voicemail context to play from.  If no context provided, assumed context is default.</para>
						</argument>
					</option>
					<option name="w" hasparams="optional">
						<para>Wait until the marked user enters the conference.</para>
						<argument name="secs" required="true" />
					</option>
					<option name="x">
						<para>Leave the conference when the last marked user leaves.</para>
					</option>
					<option name="X">
						<para>Allow user to exit the conference by entering a valid single digit
						extension <variable>MEETME_EXIT_CONTEXT</variable> or the current context
						if that variable is not defined.</para>
						<note>
							<para>Option <literal>s</literal> has priority for <literal>*</literal>
							since it cannot change its activation code.</para>
						</note>
					</option>
					<option name="1">
						<para>Do not play message when first person enters</para>
					</option>
					<option name="S">
						<para>Kick the user <replaceable>x</replaceable> seconds <emphasis>after</emphasis> he entered into
						the conference.</para>
						<argument name="x" required="true" />
					</option>
					<option name="L" argsep=":">
						<para>Limit the conference to <replaceable>x</replaceable> ms. Play a warning when
						<replaceable>y</replaceable> ms are left. Repeat the warning every <replaceable>z</replaceable> ms.
						The following special variables can be used with this option:</para>
						<variablelist>
							<variable name="CONF_LIMIT_TIMEOUT_FILE">
								<para>File to play when time is up.</para>
							</variable>
							<variable name="CONF_LIMIT_WARNING_FILE">
								<para>File to play as warning if <replaceable>y</replaceable> is defined. The
								default is to say the time remaining.</para>
							</variable>
						</variablelist>
						<argument name="x" />
						<argument name="y" />
						<argument name="z" />
					</option>
				</optionlist>
			</parameter>
			<parameter name="pin" />
		</syntax>
		<description>
			<para>Enters the user into a specified MeetMe conference.  If the <replaceable>confno</replaceable>
			is omitted, the user will be prompted to enter one.  User can exit the conference by hangup, or
			if the <literal>p</literal> option is specified, by pressing <literal>#</literal>.</para>
			<note><para>The DAHDI kernel modules and a functional DAHDI timing source (see dahdi_test)
			must be present for conferencing to operate properly. In addition, the chan_dahdi channel driver
			must be loaded for the <literal>i</literal> and <literal>r</literal> options to operate at
			all.</para></note>
		</description>
		<see-also>
			<ref type="application">MeetMeCount</ref>
			<ref type="application">MeetMeAdmin</ref>
			<ref type="application">MeetMeChannelAdmin</ref>
		</see-also>
	</application>
	<application name="MeetMeCount" language="en_US">
		<synopsis>
			MeetMe participant count.
		</synopsis>
		<syntax>
			<parameter name="confno" required="true">
				<para>Conference number.</para>
			</parameter>
			<parameter name="var" />
		</syntax>
		<description>
			<para>Plays back the number of users in the specified MeetMe conference.
			If <replaceable>var</replaceable> is specified, playback will be skipped and the value
			will be returned in the variable. Upon application completion, MeetMeCount will hangup
			the channel, unless priority <literal>n+1</literal> exists, in which case priority progress will
			continue.</para>
		</description>
		<see-also>
			<ref type="application">MeetMe</ref>
		</see-also>
	</application>
	<application name="MeetMeAdmin" language="en_US">
		<synopsis>
			MeetMe conference administration.
		</synopsis>
		<syntax>
			<parameter name="confno" required="true" />
			<parameter name="command" required="true">
				<optionlist>
					<option name="e">
						<para>Eject last user that joined.</para>
					</option>
					<option name="E">
						<para>Extend conference end time, if scheduled.</para>
					</option>
					<option name="k">
						<para>Kick one user out of conference.</para>
					</option>
					<option name="K">
						<para>Kick all users out of conference.</para>
					</option>
					<option name="l">
						<para>Unlock conference.</para>
					</option>
					<option name="L">
						<para>Lock conference.</para>
					</option>
					<option name="m">
						<para>Unmute one user.</para>
					</option>
					<option name="M">
						<para>Mute one user.</para>
					</option>
					<option name="n">
						<para>Unmute all users in the conference.</para>
					</option>
					<option name="N">
						<para>Mute all non-admin users in the conference.</para>
					</option>
					<option name="r">
						<para>Reset one user's volume settings.</para>
					</option>
					<option name="R">
						<para>Reset all users volume settings.</para>
					</option>
					<option name="s">
						<para>Lower entire conference speaking volume.</para>
					</option>
					<option name="S">
						<para>Raise entire conference speaking volume.</para>
					</option>
					<option name="t">
						<para>Lower one user's talk volume.</para>
					</option>
					<option name="T">
						<para>Raise one user's talk volume.</para>
					</option>
					<option name="u">
						<para>Lower one user's listen volume.</para>
					</option>
					<option name="U">
						<para>Raise one user's listen volume.</para>
					</option>
					<option name="v">
						<para>Lower entire conference listening volume.</para>
					</option>
					<option name="V">
						<para>Raise entire conference listening volume.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="user" />
		</syntax>
		<description>
			<para>Run admin <replaceable>command</replaceable> for conference <replaceable>confno</replaceable>.</para>
			<para>Will additionally set the variable <variable>MEETMEADMINSTATUS</variable> with one of
			the following values:</para>
			<variablelist>
				<variable name="MEETMEADMINSTATUS">
					<value name="NOPARSE">
						Invalid arguments.
					</value>
					<value name="NOTFOUND">
						User specified was not found.
					</value>
					<value name="FAILED">
						Another failure occurred.
					</value>
					<value name="OK">
						The operation was completed successfully.
					</value>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">MeetMe</ref>
		</see-also>
	</application>
	<application name="MeetMeChannelAdmin" language="en_US">
		<synopsis>
			MeetMe conference Administration (channel specific).
		</synopsis>
		<syntax>
			<parameter name="channel" required="true" />
			<parameter name="command" required="true">
				<optionlist>
					<option name="k">
						<para>Kick the specified user out of the conference he is in.</para>
					</option>
					<option name="m">
						<para>Unmute the specified user.</para>
					</option>
					<option name="M">
						<para>Mute the specified user.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Run admin <replaceable>command</replaceable> for a specific
			<replaceable>channel</replaceable> in any conference.</para>
		</description>
	</application>
	<application name="SLAStation" language="en_US">
		<synopsis>
			Shared Line Appearance Station.
		</synopsis>
		<syntax>
			<parameter name="station" required="true">
				<para>Station name</para>
			</parameter>
		</syntax>
		<description>
			<para>This application should be executed by an SLA station. The argument depends
			on how the call was initiated. If the phone was just taken off hook, then the argument
			<replaceable>station</replaceable> should be just the station name. If the call was
			initiated by pressing a line key, then the station name should be preceded by an underscore
			and the trunk name associated with that line button.</para>
			<para>For example: <literal>station1_line1</literal></para>
			<para>On exit, this application will set the variable <variable>SLASTATION_STATUS</variable> to
			one of the following values:</para>
			<variablelist>
				<variable name="SLASTATION_STATUS">
					<value name="FAILURE" />
					<value name="CONGESTION" />
					<value name="SUCCESS" />
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="SLATrunk" language="en_US">
		<synopsis>
			Shared Line Appearance Trunk.
		</synopsis>
		<syntax>
			<parameter name="trunk" required="true">
				<para>Trunk name</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="M" hasparams="optional">
						<para>Play back the specified MOH <replaceable>class</replaceable>
						instead of ringing</para>
						<argument name="class" required="true" />
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application should be executed by an SLA trunk on an inbound call. The channel calling
			this application should correspond to the SLA trunk with the name <replaceable>trunk</replaceable>
			that is being passed as an argument.</para>
			<para>On exit, this application will set the variable <variable>SLATRUNK_STATUS</variable> to
			one of the following values:</para>
			<variablelist>
				<variable name="SLATRUNK_STATUS">
					<value name="FAILURE" />
					<value name="SUCCESS" />
					<value name="UNANSWERED" />
					<value name="RINGTIMEOUT" />
				</variable>
			</variablelist>
		</description>
	</application>
	<function name="MEETME_INFO" language="en_US">
		<synopsis>
			Query a given conference of various properties.
		</synopsis>
		<syntax>
			<parameter name="keyword" required="true">
				<para>Options:</para>
				<enumlist>
					<enum name="lock">
						<para>Boolean of whether the corresponding conference is locked.</para>
					</enum>
					<enum name="parties">
						<para>Number of parties in a given conference</para>
					</enum>
					<enum name="activity">
						<para>Duration of conference in seconds.</para>
					</enum>
					<enum name="dynamic">
						<para>Boolean of whether the corresponding conference is dynamic.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="confno" required="true">
				<para>Conference number to retrieve information from.</para>
			</parameter>
		</syntax>
		<description />
		<see-also>
			<ref type="application">MeetMe</ref>
			<ref type="application">MeetMeCount</ref>
			<ref type="application">MeetMeAdmin</ref>
			<ref type="application">MeetMeChannelAdmin</ref>
		</see-also>
	</function>
	<manager name="MeetmeMute" language="en_US">
		<synopsis>
			Mute a Meetme user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Meetme" required="true" />
			<parameter name="Usernum" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="MeetmeUnmute" language="en_US">
		<synopsis>
			Unmute a Meetme user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Meetme" required="true" />
			<parameter name="Usernum" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="MeetmeList" language="en_US">
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
			<para>Lists all users in a particular MeetMe conference.
			MeetmeList will follow as separate events, followed by a final event called
			MeetmeListComplete.</para>
		</description>
	</manager>
	<manager name="MeetmeListRooms" language="en_US">
		<synopsis>
			List active conferences.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Lists data about all active conferences.
				MeetmeListRooms will follow as separate events, followed by a final event called
				MeetmeListRoomsComplete.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="MeetmeJoin">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a user joins a MeetMe conference.</synopsis>
			<syntax>
				<parameter name="Meetme">
					<para>The identifier for the MeetMe conference.</para>
				</parameter>
				<parameter name="Usernum">
					<para>The identifier of the MeetMe user who joined.</para>
				</parameter>
				<channel_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">MeetmeLeave</ref>
				<ref type="application">MeetMe</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MeetmeLeave">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a user leaves a MeetMe conference.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeJoin']/managerEventInstance/syntax/parameter)" />
				<channel_snapshot/>
				<parameter name="Duration">
					<para>The length of time in seconds that the Meetme user was in the conference.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">MeetmeJoin</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MeetmeEnd">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a MeetMe conference ends.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeJoin']/managerEventInstance/syntax/parameter[@name='Meetme'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">MeetmeJoin</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MeetmeTalkRequest">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a MeetMe user has started talking.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeJoin']/managerEventInstance/syntax/parameter)" />
				<channel_snapshot/>
				<parameter name="Duration">
					<para>The length of time in seconds that the Meetme user has been in the conference at the time of this event.</para>
				</parameter>
				<parameter name="Status">
					<enumlist>
						<enum name="on"/>
						<enum name="off"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MeetmeTalking">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a MeetMe user begins or ends talking.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeJoin']/managerEventInstance/syntax/parameter)" />
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeTalkRequest']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MeetmeMute">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a MeetMe user is muted or unmuted.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeJoin']/managerEventInstance/syntax/parameter)" />
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='MeetmeTalkRequest']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

#define CONFIG_FILE_NAME	"meetme.conf"
#define SLA_CONFIG_FILE		"sla.conf"
#define STR_CONCISE			"concise"

/*! each buffer is 20ms, so this is 640ms total */
#define DEFAULT_AUDIO_BUFFERS  32

/*! String format for scheduled conferences */
#define DATE_FORMAT "%Y-%m-%d %H:%M:%S"

enum {
	ADMINFLAG_MUTED =     (1 << 1), /*!< User is muted */
	ADMINFLAG_SELFMUTED = (1 << 2), /*!< User muted self */
	ADMINFLAG_KICKME =    (1 << 3),  /*!< User has been kicked */
	/*! User has requested to speak */
	ADMINFLAG_T_REQUEST = (1 << 4),
	ADMINFLAG_HANGUP = (1 << 5),	/*!< User will be leaving the conference */
};

#define MEETME_DELAYDETECTTALK     300
#define MEETME_DELAYDETECTENDTALK  1000

#define AST_FRAME_BITS  32

enum volume_action {
	VOL_UP,
	VOL_DOWN
};

enum entrance_sound {
	ENTER,
	LEAVE
};

enum recording_state {
	MEETME_RECORD_OFF,
	MEETME_RECORD_STARTED,
	MEETME_RECORD_ACTIVE,
	MEETME_RECORD_TERMINATE
};

#define CONF_SIZE  320

enum {
	/*! user has admin access on the conference */
	CONFFLAG_ADMIN = (1 << 0),
	/*! If set the user can only receive audio from the conference */
	CONFFLAG_MONITOR = (1 << 1),
	/*! If set asterisk will exit conference when key defined in p() option is pressed */
	CONFFLAG_KEYEXIT = (1 << 2),
	/*! If set asterisk will provide a menu to the user when '*' is pressed */
	CONFFLAG_STARMENU = (1 << 3),
	/*! If set the use can only send audio to the conference */
	CONFFLAG_TALKER = (1 << 4),
	/*! If set there will be no enter or leave sounds */
	CONFFLAG_QUIET = (1 << 5),
	/*! If set, when user joins the conference, they will be told the number 
	 *  of users that are already in */
	CONFFLAG_ANNOUNCEUSERCOUNT = (1 << 6),
	/*! Set to run AGI Script in Background */
	CONFFLAG_AGI = (1 << 7),
	/*! Set to have music on hold when user is alone in conference */
	CONFFLAG_MOH = (1 << 8),
	/*! If set, the channel will leave the conference if all marked users leave */
	CONFFLAG_MARKEDEXIT = (1 << 9),
	/*! If set, the MeetMe will wait until a marked user enters */
	CONFFLAG_WAITMARKED = (1 << 10),
	/*! If set, the MeetMe will exit to the specified context */
	CONFFLAG_EXIT_CONTEXT = (1 << 11),
	/*! If set, the user will be marked */
	CONFFLAG_MARKEDUSER = (1 << 12),
	/*! If set, user will be ask record name on entry of conference */
	CONFFLAG_INTROUSER = (1 << 13),
	/*! If set, the MeetMe will be recorded */
	CONFFLAG_RECORDCONF = (1<< 14),
	/*! If set, the user will be monitored if the user is talking or not */
	CONFFLAG_MONITORTALKER = (1 << 15),
	CONFFLAG_DYNAMIC = (1 << 16),
	CONFFLAG_DYNAMICPIN = (1 << 17),
	CONFFLAG_EMPTY = (1 << 18),
	CONFFLAG_EMPTYNOPIN = (1 << 19),
	CONFFLAG_ALWAYSPROMPT = (1 << 20),
	/*! If set, treat talking users as muted users */
	CONFFLAG_OPTIMIZETALKER = (1 << 21),
	/*! If set, won't speak the extra prompt when the first person 
	 *  enters the conference */
	CONFFLAG_NOONLYPERSON = (1 << 22),
	/*! If set, user will be asked to record name on entry of conference 
	 *  without review */
	CONFFLAG_INTROUSERNOREVIEW = (1 << 23),
	/*! If set, the user will be initially self-muted */
	CONFFLAG_STARTMUTED = (1 << 24),
	/*! Pass DTMF through the conference */
	CONFFLAG_PASS_DTMF = (1 << 25),
	CONFFLAG_SLA_STATION = (1 << 26),
	CONFFLAG_SLA_TRUNK = (1 << 27),
	/*! If set, the user should continue in the dialplan if kicked out */
	CONFFLAG_KICK_CONTINUE = (1 << 28),
	CONFFLAG_DURATION_STOP = (1 << 29),
	CONFFLAG_DURATION_LIMIT = (1 << 30),
};

/* These flags are defined separately because we ran out of bits that an enum can be used to represent. 
   If you add new flags, be sure to do it in the same way that these are. */
/*! Do not write any audio to this channel until the state is up. */
#define CONFFLAG_NO_AUDIO_UNTIL_UP  (1ULL << 31)
#define CONFFLAG_INTROMSG           (1ULL << 32) /*!< If set play an intro announcement at start of conference */
#define CONFFLAG_INTROUSER_VMREC    (1ULL << 33)
/*! If there's only one person left in a conference when someone leaves, kill the conference */
#define CONFFLAG_KILL_LAST_MAN_STANDING (1ULL << 34)
/*! If set, don't enable a denoiser for the channel */
#define CONFFLAG_DONT_DENOISE       (1ULL << 35)

enum {
	OPT_ARG_WAITMARKED = 0,
	OPT_ARG_EXITKEYS   = 1,
	OPT_ARG_DURATION_STOP = 2,
	OPT_ARG_DURATION_LIMIT = 3,
	OPT_ARG_MOH_CLASS = 4,
	OPT_ARG_INTROMSG = 5,
	OPT_ARG_INTROUSER_VMREC = 6,
	OPT_ARG_ARRAY_SIZE = 7,
};

AST_APP_OPTIONS(meetme_opts, BEGIN_OPTIONS
	AST_APP_OPTION('A', CONFFLAG_MARKEDUSER ),
	AST_APP_OPTION('a', CONFFLAG_ADMIN ),
	AST_APP_OPTION('b', CONFFLAG_AGI ),
	AST_APP_OPTION('c', CONFFLAG_ANNOUNCEUSERCOUNT ),
	AST_APP_OPTION('C', CONFFLAG_KICK_CONTINUE),
	AST_APP_OPTION('D', CONFFLAG_DYNAMICPIN ),
	AST_APP_OPTION('d', CONFFLAG_DYNAMIC ),
	AST_APP_OPTION('E', CONFFLAG_EMPTYNOPIN ),
	AST_APP_OPTION('e', CONFFLAG_EMPTY ),
	AST_APP_OPTION('F', CONFFLAG_PASS_DTMF ),
	AST_APP_OPTION_ARG('G', CONFFLAG_INTROMSG, OPT_ARG_INTROMSG ),
	AST_APP_OPTION_ARG('v', CONFFLAG_INTROUSER_VMREC , OPT_ARG_INTROUSER_VMREC),
	AST_APP_OPTION('i', CONFFLAG_INTROUSER ),
	AST_APP_OPTION('I', CONFFLAG_INTROUSERNOREVIEW ),
	AST_APP_OPTION('k', CONFFLAG_KILL_LAST_MAN_STANDING ),
	AST_APP_OPTION_ARG('M', CONFFLAG_MOH, OPT_ARG_MOH_CLASS ),
	AST_APP_OPTION('m', CONFFLAG_STARTMUTED ),
	AST_APP_OPTION('n', CONFFLAG_DONT_DENOISE ),
	AST_APP_OPTION('o', CONFFLAG_OPTIMIZETALKER ),
	AST_APP_OPTION('P', CONFFLAG_ALWAYSPROMPT ),
	AST_APP_OPTION_ARG('p', CONFFLAG_KEYEXIT, OPT_ARG_EXITKEYS ),
	AST_APP_OPTION('q', CONFFLAG_QUIET ),
	AST_APP_OPTION('r', CONFFLAG_RECORDCONF ),
	AST_APP_OPTION('s', CONFFLAG_STARMENU ),
	AST_APP_OPTION('T', CONFFLAG_MONITORTALKER ),
	AST_APP_OPTION('l', CONFFLAG_MONITOR ),
	AST_APP_OPTION('t', CONFFLAG_TALKER ),
	AST_APP_OPTION_ARG('w', CONFFLAG_WAITMARKED, OPT_ARG_WAITMARKED ),
	AST_APP_OPTION('X', CONFFLAG_EXIT_CONTEXT ),
	AST_APP_OPTION('x', CONFFLAG_MARKEDEXIT ),
	AST_APP_OPTION('1', CONFFLAG_NOONLYPERSON ),
 	AST_APP_OPTION_ARG('S', CONFFLAG_DURATION_STOP, OPT_ARG_DURATION_STOP),
	AST_APP_OPTION_ARG('L', CONFFLAG_DURATION_LIMIT, OPT_ARG_DURATION_LIMIT),
END_OPTIONS );

static const char * const app = "MeetMe";
static const char * const app2 = "MeetMeCount";
static const char * const app3 = "MeetMeAdmin";
static const char * const app4 = "MeetMeChannelAdmin";
static const char * const slastation_app = "SLAStation";
static const char * const slatrunk_app = "SLATrunk";

/* Lookup RealTime conferences based on confno and current time */
static int rt_schedule;
static int fuzzystart;
static int earlyalert;
static int endalert;
static int extendby;

/*! Log participant count to the RealTime backend */
static int rt_log_members;

#define MAX_CONFNUM 80
#define MAX_PIN     80
#define OPTIONS_LEN 100

/* Enough space for "<conference #>,<pin>,<admin pin>" followed by a 0 byte. */
#define MAX_SETTINGS (MAX_CONFNUM + MAX_PIN + MAX_PIN + 3)

enum announcetypes {
	CONF_HASJOIN,
	CONF_HASLEFT
};

struct announce_listitem {
	AST_LIST_ENTRY(announce_listitem) entry;
	char namerecloc[PATH_MAX];				/*!< Name Recorded file Location */
	char language[MAX_LANGUAGE];
	struct ast_channel *confchan;
	int confusers;
	int vmrec;
	enum announcetypes announcetype;
};

/*! \brief The MeetMe Conference object */
struct ast_conference {
	ast_mutex_t playlock;                   /*!< Conference specific lock (players) */
	ast_mutex_t listenlock;                 /*!< Conference specific lock (listeners) */
	char confno[MAX_CONFNUM];               /*!< Conference */
	struct ast_channel *chan;               /*!< Announcements channel */
	struct ast_channel *lchan;              /*!< Listen/Record channel */
	int fd;                                 /*!< Announcements fd */
	int dahdiconf;                            /*!< DAHDI Conf # */
	int users;                              /*!< Number of active users */
	int markedusers;                        /*!< Number of marked users */
	int maxusers;                           /*!< Participant limit if scheduled */
	int endalert;                           /*!< When to play conf ending message */
	time_t start;                           /*!< Start time (s) */
	int refcount;                           /*!< reference count of usage */
	enum recording_state recording:2;       /*!< recording status */
	unsigned int isdynamic:1;               /*!< Created on the fly? */
	unsigned int locked:1;                  /*!< Is the conference locked? */
	unsigned int gmuted:1;                  /*!< Is the conference globally muted? (all non-admins) */
	pthread_t recordthread;                 /*!< thread for recording */
	ast_mutex_t recordthreadlock;           /*!< control threads trying to start recordthread */
	pthread_attr_t attr;                    /*!< thread attribute */
	char *recordingfilename;                /*!< Filename to record the Conference into */
	char *recordingformat;                  /*!< Format to record the Conference in */
	char pin[MAX_PIN];                      /*!< If protected by a PIN */
	char pinadmin[MAX_PIN];                 /*!< If protected by a admin PIN */
	char uniqueid[32];
	long endtime;                           /*!< When to end the conf if scheduled */
	const char *useropts;                   /*!< RealTime user flags */
	const char *adminopts;                  /*!< RealTime moderator flags */
	const char *bookid;                     /*!< RealTime conference id */
	struct ast_frame *transframe[32];
	struct ast_frame *origframe;
	struct ast_trans_pvt *transpath[32];
	struct ao2_container *usercontainer;
	AST_LIST_ENTRY(ast_conference) list;
	/* announce_thread related data */
	pthread_t announcethread;
	ast_mutex_t announcethreadlock;
	unsigned int announcethread_stop:1;
	ast_cond_t announcelist_addition;
	AST_LIST_HEAD_NOLOCK(, announce_listitem) announcelist;
	ast_mutex_t announcelistlock;
};

static AST_LIST_HEAD_STATIC(confs, ast_conference);

static unsigned int conf_map[1024] = {0, };

struct volume {
	int desired;                            /*!< Desired volume adjustment */
	int actual;                             /*!< Actual volume adjustment (for channels that can't adjust) */
};

/*! \brief The MeetMe User object */
struct ast_conf_user {
	int user_no;                            /*!< User Number */
	struct ast_flags64 userflags;           /*!< Flags as set in the conference */
	int adminflags;                         /*!< Flags set by the Admin */
	struct ast_channel *chan;               /*!< Connected channel */
	int talking;                            /*!< Is user talking */
	int dahdichannel;                       /*!< Is a DAHDI channel */
	char usrvalue[50];                      /*!< Custom User Value */
	char namerecloc[PATH_MAX];		/*!< Name Recorded file Location */
	time_t jointime;                        /*!< Time the user joined the conference */
	time_t kicktime;                        /*!< Time the user will be kicked from the conference */
	struct timeval start_time;              /*!< Time the user entered into the conference */
	long timelimit;                         /*!< Time limit for the user to be in the conference L(x:y:z) */
	long play_warning;                      /*!< Play a warning when 'y' ms are left */
	long warning_freq;                      /*!< Repeat the warning every 'z' ms */
	const char *warning_sound;              /*!< File to play as warning if 'y' is defined */
	const char *end_sound;                  /*!< File to play when time is up. */
	struct volume talk;
	struct volume listen;
	AST_LIST_ENTRY(ast_conf_user) list;
};

enum sla_which_trunk_refs {
	ALL_TRUNK_REFS,
	INACTIVE_TRUNK_REFS,
};

enum sla_trunk_state {
	SLA_TRUNK_STATE_IDLE,
	SLA_TRUNK_STATE_RINGING,
	SLA_TRUNK_STATE_UP,
	SLA_TRUNK_STATE_ONHOLD,
	SLA_TRUNK_STATE_ONHOLD_BYME,
};

enum sla_hold_access {
	/*! This means that any station can put it on hold, and any station
	 * can retrieve the call from hold. */
	SLA_HOLD_OPEN,
	/*! This means that only the station that put the call on hold may
	 * retrieve it from hold. */
	SLA_HOLD_PRIVATE,
};

struct sla_trunk_ref;

struct sla_station {
	AST_RWLIST_ENTRY(sla_station) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);	
		AST_STRING_FIELD(device);	
		AST_STRING_FIELD(autocontext);	
	);
	AST_LIST_HEAD_NOLOCK(, sla_trunk_ref) trunks;
	struct ast_dial *dial;
	/*! Ring timeout for this station, for any trunk.  If a ring timeout
	 *  is set for a specific trunk on this station, that will take
	 *  priority over this value. */
	unsigned int ring_timeout;
	/*! Ring delay for this station, for any trunk.  If a ring delay
	 *  is set for a specific trunk on this station, that will take
	 *  priority over this value. */
	unsigned int ring_delay;
	/*! This option uses the values in the sla_hold_access enum and sets the
	 * access control type for hold on this station. */
	unsigned int hold_access:1;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

/*!
 * \brief A reference to a station
 *
 * This struct looks near useless at first glance.  However, its existence
 * in the list of stations in sla_trunk means that this station references
 * that trunk.  We use the mark to keep track of whether it needs to be
 * removed from the sla_trunk's list of stations during a reload.
 */
struct sla_station_ref {
	AST_LIST_ENTRY(sla_station_ref) entry;
	struct sla_station *station;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

struct sla_trunk {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(device);
		AST_STRING_FIELD(autocontext);	
	);
	AST_LIST_HEAD_NOLOCK(, sla_station_ref) stations;
	/*! Number of stations that use this trunk */
	unsigned int num_stations;
	/*! Number of stations currently on a call with this trunk */
	unsigned int active_stations;
	/*! Number of stations that have this trunk on hold. */
	unsigned int hold_stations;
	struct ast_channel *chan;
	unsigned int ring_timeout;
	/*! If set to 1, no station will be able to join an active call with
	 *  this trunk. */
	unsigned int barge_disabled:1;
	/*! This option uses the values in the sla_hold_access enum and sets the
	 * access control type for hold on this trunk. */
	unsigned int hold_access:1;
	/*! Whether this trunk is currently on hold, meaning that once a station
	 *  connects to it, the trunk channel needs to have UNHOLD indicated to it. */
	unsigned int on_hold:1;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

/*!
 * \brief A station's reference to a trunk
 *
 * An sla_station keeps a list of trunk_refs.  This holds metadata about the
 * stations usage of the trunk.
 */
struct sla_trunk_ref {
	AST_LIST_ENTRY(sla_trunk_ref) entry;
	struct sla_trunk *trunk;
	enum sla_trunk_state state;
	struct ast_channel *chan;
	/*! Ring timeout to use when this trunk is ringing on this specific
	 *  station.  This takes higher priority than a ring timeout set at
	 *  the station level. */
	unsigned int ring_timeout;
	/*! Ring delay to use when this trunk is ringing on this specific
	 *  station.  This takes higher priority than a ring delay set at
	 *  the station level. */
	unsigned int ring_delay;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

static struct ao2_container *sla_stations;
static struct ao2_container *sla_trunks;

static const char sla_registrar[] = "SLA";

/*! \brief Event types that can be queued up for the SLA thread */
enum sla_event_type {
	/*! A station has put the call on hold */
	SLA_EVENT_HOLD,
	/*! The state of a dial has changed */
	SLA_EVENT_DIAL_STATE,
	/*! The state of a ringing trunk has changed */
	SLA_EVENT_RINGING_TRUNK,
};

struct sla_event {
	enum sla_event_type type;
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	AST_LIST_ENTRY(sla_event) entry;
};

/*! \brief A station that failed to be dialed 
 * \note Only used by the SLA thread. */
struct sla_failed_station {
	struct sla_station *station;
	struct timeval last_try;
	AST_LIST_ENTRY(sla_failed_station) entry;
};

/*! \brief A trunk that is ringing */
struct sla_ringing_trunk {
	struct sla_trunk *trunk;
	/*! The time that this trunk started ringing */
	struct timeval ring_begin;
	AST_LIST_HEAD_NOLOCK(, sla_station_ref) timed_out_stations;
	AST_LIST_ENTRY(sla_ringing_trunk) entry;
};

enum sla_station_hangup {
	SLA_STATION_HANGUP_NORMAL,
	SLA_STATION_HANGUP_TIMEOUT,
};

/*! \brief A station that is ringing */
struct sla_ringing_station {
	struct sla_station *station;
	/*! The time that this station started ringing */
	struct timeval ring_begin;
	AST_LIST_ENTRY(sla_ringing_station) entry;
};

/*!
 * \brief A structure for data used by the sla thread
 */
static struct {
	/*! The SLA thread ID */
	pthread_t thread;
	ast_cond_t cond;
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(, sla_ringing_trunk) ringing_trunks;
	AST_LIST_HEAD_NOLOCK(, sla_ringing_station) ringing_stations;
	AST_LIST_HEAD_NOLOCK(, sla_failed_station) failed_stations;
	AST_LIST_HEAD_NOLOCK(, sla_event) event_q;
	unsigned int stop:1;
	/*! Attempt to handle CallerID, even though it is known not to work
	 *  properly in some situations. */
	unsigned int attempt_callerid:1;
} sla = {
	.thread = AST_PTHREADT_NULL,
};

/*! \brief The number of audio buffers to be allocated on pseudo channels
 *  when in a conference */
static int audio_buffers;

/*! \brief Map 'volume' levels from -5 through +5 into decibel (dB) 
 *    settings for channel drivers.
 *
 *  \note these are not a straight linear-to-dB
 *  conversion... the numbers have been modified
 *  to give the user a better level of adjustability.
 */
static const char gain_map[] = {
	-15,
	-13,
	-10,
	-6,
	0,
	0,
	0,
	6,
	10,
	13,
	15,
};

/* Routes the various meetme message types to the meetme stasis callback function to turn them into events */
static struct stasis_message_router *meetme_event_message_router;

STASIS_MESSAGE_TYPE_DEFN_LOCAL(meetme_join_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(meetme_leave_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(meetme_end_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(meetme_mute_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(meetme_talking_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(meetme_talk_request_type);

static void meetme_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message);

static void meetme_stasis_cleanup(void)
{
	if (meetme_event_message_router) {
		stasis_message_router_unsubscribe(meetme_event_message_router);
		meetme_event_message_router = NULL;
	}

	STASIS_MESSAGE_TYPE_CLEANUP(meetme_join_type);
	STASIS_MESSAGE_TYPE_CLEANUP(meetme_leave_type);
	STASIS_MESSAGE_TYPE_CLEANUP(meetme_end_type);
	STASIS_MESSAGE_TYPE_CLEANUP(meetme_mute_type);
	STASIS_MESSAGE_TYPE_CLEANUP(meetme_talking_type);
	STASIS_MESSAGE_TYPE_CLEANUP(meetme_talk_request_type);
}

static int meetme_stasis_init(void)
{

	STASIS_MESSAGE_TYPE_INIT(meetme_join_type);
	STASIS_MESSAGE_TYPE_INIT(meetme_leave_type);
	STASIS_MESSAGE_TYPE_INIT(meetme_end_type);
	STASIS_MESSAGE_TYPE_INIT(meetme_mute_type);
	STASIS_MESSAGE_TYPE_INIT(meetme_talking_type);
	STASIS_MESSAGE_TYPE_INIT(meetme_talk_request_type);

	meetme_event_message_router = stasis_message_router_create(
		ast_channel_topic_all_cached());

	if (!meetme_event_message_router) {
		meetme_stasis_cleanup();
		return -1;
	}

	if (stasis_message_router_add(meetme_event_message_router,
			meetme_join_type(),
			meetme_stasis_cb,
			NULL)) {
		meetme_stasis_cleanup();
		return -1;
	}

	if (stasis_message_router_add(meetme_event_message_router,
			meetme_leave_type(),
			meetme_stasis_cb,
			NULL)) {
		meetme_stasis_cleanup();
		return -1;
	}

	if (stasis_message_router_add(meetme_event_message_router,
			meetme_end_type(),
			meetme_stasis_cb,
			NULL)) {
		meetme_stasis_cleanup();
		return -1;
	}

	if (stasis_message_router_add(meetme_event_message_router,
			meetme_mute_type(),
			meetme_stasis_cb,
			NULL)) {
		meetme_stasis_cleanup();
		return -1;
	}

	if (stasis_message_router_add(meetme_event_message_router,
			meetme_talking_type(),
			meetme_stasis_cb,
			NULL)) {
		meetme_stasis_cleanup();
		return -1;
	}

	if (stasis_message_router_add(meetme_event_message_router,
			meetme_talk_request_type(),
			meetme_stasis_cb,
			NULL)) {
		meetme_stasis_cleanup();
		return -1;
	}

	return 0;
}

static void meetme_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_channel_blob *channel_blob = stasis_message_data(message);
	struct stasis_message_type *message_type;
	const char *event;
	const char *conference_num;
	const char *status;
	struct ast_json *json_cur;
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, extra_text, NULL, ast_free);

	if (!channel_blob) {
		ast_assert(0);
		return;
	}

	message_type = stasis_message_type(message);

	if (!message_type) {
		ast_assert(0);
		return;
	}

	if (message_type == meetme_join_type()) {
		event = "MeetmeJoin";
	} else if (message_type == meetme_leave_type()) {
		event = "MeetmeLeave";
	} else if (message_type == meetme_end_type()) {
		event = "MeetmeEnd";
	} else if (message_type == meetme_mute_type()) {
		event = "MeetmeMute";
	} else if (message_type == meetme_talking_type()) {
		event = "MeetmeTalking";
	} else if (message_type == meetme_talk_request_type()) {
		event = "MeetmeTalkRequest";
	} else {
		ast_assert(0);
		return;
	}

	if (!event) {
		ast_assert(0);
		return;
	}

	conference_num = ast_json_string_get(ast_json_object_get(channel_blob->blob, "Meetme"));
	if (!conference_num) {
		ast_assert(0);
		return;
	}

	status = ast_json_string_get(ast_json_object_get(channel_blob->blob, "status"));
	if (status) {
		ast_str_append_event_header(&extra_text, "Status", status);
	}

	if (channel_blob->snapshot) {
		channel_text = ast_manager_build_channel_state_string(channel_blob->snapshot);
	}

	if ((json_cur = ast_json_object_get(channel_blob->blob, "user"))) {
		int user_number = ast_json_integer_get(json_cur);
		RAII_VAR(struct ast_str *, user_prop_str, ast_str_create(32), ast_free);
		if (!user_prop_str) {
			return;
		}

		ast_str_set(&user_prop_str, 0, "%d", user_number);
		ast_str_append_event_header(&extra_text, "User", ast_str_buffer(user_prop_str));

		if ((json_cur = ast_json_object_get(channel_blob->blob, "duration"))) {
			int duration = ast_json_integer_get(json_cur);
			ast_str_set(&user_prop_str, 0, "%d", duration);
			ast_str_append_event_header(&extra_text, "Duration", ast_str_buffer(user_prop_str));
		}

		json_cur = NULL;
	}

	manager_event(EVENT_FLAG_CALL, event,
		"Meetme: %s\r\n"
		"%s"
		"%s",
		conference_num,
		channel_text ? ast_str_buffer(channel_text) : "",
		extra_text ? ast_str_buffer(extra_text) : "");
}

/*!
 * \internal
 * \brief Build a json object from a status value for inclusion in json extras for meetme_stasis_generate_msg
 * \since 12.0.0
 *
 * \param on if true, then status is on. Otherwise status is off
 * \retval NULL on failure to allocate the JSON blob.
 * \retval pointer to the JSON blob if successful.
 */
static struct ast_json *status_to_json(int on)
{
	struct ast_json *json_object = ast_json_pack("{s: s}",
		"status", on ? "on" : "off");

	return json_object;
}

/*!
 * \internal
 * \brief Generate a stasis message associated with a meetme event
 * \since 12.0.0
 *
 * \param meetme_confere The conference responsible for generating this message
 * \param chan The channel involved in the message (NULL allowed)
 * \param user The conference user involved in the message (NULL allowed)
 * \param message_type the type the stasis message being generated
 * \param extras Additional json fields desired for inclusion
 */
static void meetme_stasis_generate_msg(struct ast_conference *meetme_conference, struct ast_channel *chan,
	struct ast_conf_user *user, struct stasis_message_type *message_type, struct ast_json *extras)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);

	json_object = ast_json_pack("{s: s}",
		"Meetme", meetme_conference->confno);

	if (!json_object) {
		return;
	}

	if (extras) {
		ast_json_object_update(json_object, extras);
	}

	if (user) {
		struct timeval now = ast_tvnow();
		long duration = (long)(now.tv_sec - user->jointime);
		struct ast_json *json_user;
		struct ast_json *json_user_duration;

		json_user = ast_json_integer_create(user->user_no);
		if (!json_user || ast_json_object_set(json_object, "user", json_user)) {
			return;
		}

		if (duration > 0) {
			json_user_duration = ast_json_integer_create(duration);
			if (!json_user_duration
				|| ast_json_object_set(json_object, "duration", json_user_duration)) {
				return;
			}
		}
	}

	if (chan) {
		ast_channel_lock(chan);
	}
	msg = ast_channel_blob_create(chan, message_type, json_object);
	if (chan) {
		ast_channel_unlock(chan);
	}

	if (!msg) {
		return;
	}

	stasis_publish(ast_channel_topic(chan), msg);
}

static int admin_exec(struct ast_channel *chan, const char *data);
static void *recordthread(void *args);

static const char *istalking(int x)
{
	if (x > 0)
		return "(talking)";
	else if (x < 0)
		return "(unmonitored)";
	else 
		return "(not talking)";
}

static int careful_write(int fd, unsigned char *data, int len, int block)
{
	int res;
	int x;

	while (len) {
		if (block) {
			x = DAHDI_IOMUX_WRITE | DAHDI_IOMUX_SIGEVENT;
			res = ioctl(fd, DAHDI_IOMUX, &x);
		} else
			res = 0;
		if (res >= 0)
			res = write(fd, data, len);
		if (res < 1) {
			if (errno != EAGAIN) {
				ast_log(LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
				return -1;
			} else
				return 0;
		}
		len -= res;
		data += res;
	}

	return 0;
}

static int set_talk_volume(struct ast_conf_user *user, int volume)
{
	char gain_adjust;

	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	gain_adjust = gain_map[volume + 5];

	return ast_channel_setoption(user->chan, AST_OPTION_RXGAIN, &gain_adjust, sizeof(gain_adjust), 0);
}

static int set_listen_volume(struct ast_conf_user *user, int volume)
{
	char gain_adjust;

	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	gain_adjust = gain_map[volume + 5];

	return ast_channel_setoption(user->chan, AST_OPTION_TXGAIN, &gain_adjust, sizeof(gain_adjust), 0);
}

static void tweak_volume(struct volume *vol, enum volume_action action)
{
	switch (action) {
	case VOL_UP:
		switch (vol->desired) { 
		case 5:
			break;
		case 0:
			vol->desired = 2;
			break;
		case -2:
			vol->desired = 0;
			break;
		default:
			vol->desired++;
			break;
		}
		break;
	case VOL_DOWN:
		switch (vol->desired) {
		case -5:
			break;
		case 2:
			vol->desired = 0;
			break;
		case 0:
			vol->desired = -2;
			break;
		default:
			vol->desired--;
			break;
		}
	}
}

static void tweak_talk_volume(struct ast_conf_user *user, enum volume_action action)
{
	tweak_volume(&user->talk, action);
	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	if (!set_talk_volume(user, user->talk.desired))
		user->talk.actual = 0;
	else
		user->talk.actual = user->talk.desired;
}

static void tweak_listen_volume(struct ast_conf_user *user, enum volume_action action)
{
	tweak_volume(&user->listen, action);
	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	if (!set_listen_volume(user, user->listen.desired))
		user->listen.actual = 0;
	else
		user->listen.actual = user->listen.desired;
}

static void reset_volumes(struct ast_conf_user *user)
{
	signed char zero_volume = 0;

	ast_channel_setoption(user->chan, AST_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);
	ast_channel_setoption(user->chan, AST_OPTION_RXGAIN, &zero_volume, sizeof(zero_volume), 0);
}

static void conf_play(struct ast_channel *chan, struct ast_conference *conf, enum entrance_sound sound)
{
	unsigned char *data;
	int len;
	int res = -1;

	ast_test_suite_event_notify("CONFPLAY", "Channel: %s\r\n"
		"Conference: %s\r\n"
		"Marked: %d",
		ast_channel_name(chan),
		conf->confno,
		conf->markedusers);

	if (!ast_check_hangup(chan))
		res = ast_autoservice_start(chan);

	AST_LIST_LOCK(&confs);

	switch(sound) {
	case ENTER:
		data = enter;
		len = sizeof(enter);
		break;
	case LEAVE:
		data = leave;
		len = sizeof(leave);
		break;
	default:
		data = NULL;
		len = 0;
	}
	if (data) {
		careful_write(conf->fd, data, len, 1);
	}

	AST_LIST_UNLOCK(&confs);

	if (!res) 
		ast_autoservice_stop(chan);
}

static int user_no_cmp(void *obj, void *arg, int flags)
{
	struct ast_conf_user *user = obj;
	int *user_no = arg;

	if (user->user_no == *user_no) {
		return (CMP_MATCH | CMP_STOP);
	}

	return 0;
}

static int user_max_cmp(void *obj, void *arg, int flags)
{
	struct ast_conf_user *user = obj;
	int *max_no = arg;

	if (user->user_no > *max_no) {
		*max_no = user->user_no;
	}

	return 0;
}

/*!
 * \brief Find or create a conference
 *
 * \param confno The conference name/number
 * \param pin The regular user pin
 * \param pinadmin The admin pin
 * \param make Make the conf if it doesn't exist
 * \param dynamic Mark the newly created conference as dynamic
 * \param refcount How many references to mark on the conference
 * \param chan The asterisk channel
 * \param test
 *
 * \return A pointer to the conference struct, or NULL if it wasn't found and
 *         make or dynamic were not set.
 */
static struct ast_conference *build_conf(const char *confno, const char *pin,
	const char *pinadmin, int make, int dynamic, int refcount,
	const struct ast_channel *chan, struct ast_test *test)
{
	struct ast_conference *cnf;
	struct dahdi_confinfo dahdic = { 0, };
	int confno_int = 0;
	struct ast_format_cap *cap_slin = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	AST_LIST_LOCK(&confs);

	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(confno, cnf->confno)) 
			break;
	}

	if (cnf || (!make && !dynamic) || !cap_slin)
		goto cnfout;

	ast_format_cap_append(cap_slin, ast_format_slin, 0);
	/* Make a new one */
	if (!(cnf = ast_calloc(1, sizeof(*cnf))) ||
		!(cnf->usercontainer = ao2_container_alloc(1, NULL, user_no_cmp))) {
		goto cnfout;
	}

	ast_mutex_init(&cnf->playlock);
	ast_mutex_init(&cnf->listenlock);
	cnf->recordthread = AST_PTHREADT_NULL;
	ast_mutex_init(&cnf->recordthreadlock);
	cnf->announcethread = AST_PTHREADT_NULL;
	ast_mutex_init(&cnf->announcethreadlock);
	ast_copy_string(cnf->confno, confno, sizeof(cnf->confno));
	ast_copy_string(cnf->pin, pin, sizeof(cnf->pin));
	ast_copy_string(cnf->pinadmin, pinadmin, sizeof(cnf->pinadmin));
	ast_copy_string(cnf->uniqueid, ast_channel_uniqueid(chan), sizeof(cnf->uniqueid));

	/* Setup a new dahdi conference */
	dahdic.confno = -1;
	dahdic.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
	cnf->fd = open("/dev/dahdi/pseudo", O_RDWR);
	if (cnf->fd < 0 || ioctl(cnf->fd, DAHDI_SETCONF, &dahdic)) {
		if (test) {
			/* if we are creating a conference for a unit test, it is not neccesary
			 * to open a pseudo channel, so, if we fail continue creating
			 * the conference. */
			ast_test_status_update(test, "Unable to open DAHDI pseudo device\n");
		} else {
			ast_log(LOG_WARNING, "Unable to open DAHDI pseudo device\n");
			if (cnf->fd >= 0)
				close(cnf->fd);
			ao2_ref(cnf->usercontainer, -1);
			ast_mutex_destroy(&cnf->playlock);
			ast_mutex_destroy(&cnf->listenlock);
			ast_mutex_destroy(&cnf->recordthreadlock);
			ast_mutex_destroy(&cnf->announcethreadlock);
			ast_free(cnf);
			cnf = NULL;
			goto cnfout;
		}
	}

	cnf->dahdiconf = dahdic.confno;

	/* Setup a new channel for playback of audio files */
	cnf->chan = ast_request("DAHDI", cap_slin, NULL, chan, "pseudo", NULL);
	if (cnf->chan) {
		ast_set_read_format(cnf->chan, ast_format_slin);
		ast_set_write_format(cnf->chan, ast_format_slin);
		dahdic.chan = 0;
		dahdic.confno = cnf->dahdiconf;
		dahdic.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
		if (ioctl(ast_channel_fd(cnf->chan, 0), DAHDI_SETCONF, &dahdic)) {
			if (test) {
				ast_test_status_update(test, "Error setting conference on pseudo channel\n");
			}
			ast_log(LOG_WARNING, "Error setting conference\n");
			if (cnf->chan)
				ast_hangup(cnf->chan);
			else
				close(cnf->fd);
			ao2_ref(cnf->usercontainer, -1);
			ast_mutex_destroy(&cnf->playlock);
			ast_mutex_destroy(&cnf->listenlock);
			ast_mutex_destroy(&cnf->recordthreadlock);
			ast_mutex_destroy(&cnf->announcethreadlock);
			ast_free(cnf);
			cnf = NULL;
			goto cnfout;
		}
	}

	/* Fill the conference struct */
	cnf->start = time(NULL);
	cnf->maxusers = 0x7fffffff;
	cnf->isdynamic = dynamic ? 1 : 0;
	ast_verb(3, "Created MeetMe conference %d for conference '%s'\n", cnf->dahdiconf, cnf->confno);
	AST_LIST_INSERT_HEAD(&confs, cnf, list);

	/* Reserve conference number in map */
	if ((sscanf(cnf->confno, "%30d", &confno_int) == 1) && (confno_int >= 0 && confno_int < 1024))
		conf_map[confno_int] = 1;
	
cnfout:
	ao2_cleanup(cap_slin);
	if (cnf)
		ast_atomic_fetchadd_int(&cnf->refcount, refcount);

	AST_LIST_UNLOCK(&confs);

	return cnf;
}

static char *complete_confno(const char *word, int state)
{
	struct ast_conference *cnf;
	char *ret = NULL;
	int which = 0;
	int len = strlen(word);

	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strncmp(word, cnf->confno, len) && ++which > state) {
			/* dup before releasing the lock */
			ret = ast_strdup(cnf->confno);
			break;
		}
	}
	AST_LIST_UNLOCK(&confs);
	return ret;
}

static char *complete_userno(struct ast_conference *cnf, const char *word, int state)
{
	char usrno[50];
	struct ao2_iterator iter;
	struct ast_conf_user *usr;
	char *ret = NULL;
	int which = 0;
	int len = strlen(word);

	iter = ao2_iterator_init(cnf->usercontainer, 0);
	for (; (usr = ao2_iterator_next(&iter)); ao2_ref(usr, -1)) {
		snprintf(usrno, sizeof(usrno), "%d", usr->user_no);
		if (!strncmp(word, usrno, len) && ++which > state) {
			ao2_ref(usr, -1);
			ret = ast_strdup(usrno);
			break;
		}
	}
	ao2_iterator_destroy(&iter);
	return ret;
}

static char *complete_meetmecmd_mute_kick(const char *line, const char *word, int pos, int state)
{
	if (pos == 2) {
		return complete_confno(word, state);
	}
	if (pos == 3) {
		int len = strlen(word);
		char *ret = NULL;
		char *saved = NULL;
		char *myline;
		char *confno;
		struct ast_conference *cnf;

		if (!strncasecmp(word, "all", len)) {
			if (state == 0) {
				return ast_strdup("all");
			}
			--state;
		}

		/* Extract the confno from the command line. */
		myline = ast_strdupa(line);
		strtok_r(myline, " ", &saved);
		strtok_r(NULL, " ", &saved);
		confno = strtok_r(NULL, " ", &saved);

		AST_LIST_LOCK(&confs);
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			if (!strcmp(confno, cnf->confno)) {
				ret = complete_userno(cnf, word, state);
				break;
			}
		}
		AST_LIST_UNLOCK(&confs);

		return ret;
	}
	return NULL;
}

static char *complete_meetmecmd_lock(const char *word, int pos, int state)
{
	if (pos == 2) {
		return complete_confno(word, state);
	}
	return NULL;
}

static char *complete_meetmecmd_list(const char *line, const char *word, int pos, int state)
{
	int len;

	if (pos == 2) {
		len = strlen(word);
		if (!strncasecmp(word, STR_CONCISE, len)) {
			if (state == 0) {
				return ast_strdup(STR_CONCISE);
			}
			--state;
		}

		return complete_confno(word, state);
	}
	if (pos == 3 && state == 0) {
		char *saved = NULL;
		char *myline;
		char *confno;

		/* Extract the confno from the command line. */
		myline = ast_strdupa(line);
		strtok_r(myline, " ", &saved);
		strtok_r(NULL, " ", &saved);
		confno = strtok_r(NULL, " ", &saved);

		if (!strcasecmp(confno, STR_CONCISE)) {
			/* There is nothing valid in this position now. */
			return NULL;
		}

		len = strlen(word);
		if (!strncasecmp(word, STR_CONCISE, len)) {
			return ast_strdup(STR_CONCISE);
		}
	}
	return NULL;
}

static char *meetme_show_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	/* Process the command */
	struct ast_conf_user *user;
	struct ast_conference *cnf;
	int hr, min, sec;
	int total = 0;
	time_t now;
#define MC_HEADER_FORMAT "%-14s %-14s %-10s %-8s  %-8s  %-6s\n"
#define MC_DATA_FORMAT "%-12.12s   %4.4d	      %4.4s       %02d:%02d:%02d  %-8s  %-6s\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "meetme list";
		e->usage =
			"Usage: meetme list [<confno>] [" STR_CONCISE "]\n"
			"       List all conferences or a specific conference.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_meetmecmd_list(a->line, a->word, a->pos, a->n);
	}

	if (a->argc == 2 || (a->argc == 3 && !strcasecmp(a->argv[2], STR_CONCISE))) {
		/* List all the conferences */
		int concise = (a->argc == 3);
		struct ast_str *marked_users;

		if (!(marked_users = ast_str_create(30))) {
			return CLI_FAILURE;
		}

		now = time(NULL);
		AST_LIST_LOCK(&confs);
		if (AST_LIST_EMPTY(&confs)) {
			if (!concise) {
				ast_cli(a->fd, "No active MeetMe conferences.\n");
			}
			AST_LIST_UNLOCK(&confs);
			ast_free(marked_users);
			return CLI_SUCCESS;
		}
		if (!concise) {
			ast_cli(a->fd, MC_HEADER_FORMAT, "Conf Num", "Parties", "Marked", "Activity", "Creation", "Locked");
		}
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			hr = (now - cnf->start) / 3600;
			min = ((now - cnf->start) % 3600) / 60;
			sec = (now - cnf->start) % 60;
			if (!concise) {
				if (cnf->markedusers == 0) {
					ast_str_set(&marked_users, 0, "N/A ");
				} else {
					ast_str_set(&marked_users, 0, "%4.4d", cnf->markedusers);
				}
				ast_cli(a->fd, MC_DATA_FORMAT, cnf->confno, cnf->users,
					ast_str_buffer(marked_users), hr, min, sec,
					cnf->isdynamic ? "Dynamic" : "Static", cnf->locked ? "Yes" : "No");
			} else {
				ast_cli(a->fd, "%s!%d!%d!%02d:%02d:%02d!%d!%d\n",
					cnf->confno,
					cnf->users,
					cnf->markedusers,
					hr, min, sec,
					cnf->isdynamic,
					cnf->locked);
			}

			total += cnf->users;
		}
		AST_LIST_UNLOCK(&confs);
		if (!concise) {
			ast_cli(a->fd, "* Total number of MeetMe users: %d\n", total);
		}
		ast_free(marked_users);
		return CLI_SUCCESS;
	}
	if (a->argc == 3 || (a->argc == 4 && !strcasecmp(a->argv[3], STR_CONCISE))) {
		struct ao2_iterator user_iter;
		int concise = (a->argc == 4);

		/* List all the users in a conference */
		if (AST_LIST_EMPTY(&confs)) {
			if (!concise) {
				ast_cli(a->fd, "No active MeetMe conferences.\n");
			}
			return CLI_SUCCESS;
		}
		/* Find the right conference */
		AST_LIST_LOCK(&confs);
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			if (strcmp(cnf->confno, a->argv[2]) == 0) {
				break;
			}
		}
		if (!cnf) {
			if (!concise)
				ast_cli(a->fd, "No such conference: %s.\n", a->argv[2]);
			AST_LIST_UNLOCK(&confs);
			return CLI_SUCCESS;
		}
		/* Show all the users */
		time(&now);
		user_iter = ao2_iterator_init(cnf->usercontainer, 0);
		while((user = ao2_iterator_next(&user_iter))) {
			hr = (now - user->jointime) / 3600;
			min = ((now - user->jointime) % 3600) / 60;
			sec = (now - user->jointime) % 60;
			if (!concise) {
				ast_cli(a->fd, "User #: %-2.2d %12.12s %-20.20s Channel: %s %s %s %s %s %s %02d:%02d:%02d\n",
					user->user_no,
					S_COR(ast_channel_caller(user->chan)->id.number.valid, ast_channel_caller(user->chan)->id.number.str, "<unknown>"),
					S_COR(ast_channel_caller(user->chan)->id.name.valid, ast_channel_caller(user->chan)->id.name.str, "<no name>"),
					ast_channel_name(user->chan),
					ast_test_flag64(&user->userflags, CONFFLAG_ADMIN) ? "(Admin)" : "",
					ast_test_flag64(&user->userflags, CONFFLAG_MONITOR) ? "(Listen only)" : "",
					user->adminflags & ADMINFLAG_MUTED ? "(Admin Muted)" : user->adminflags & ADMINFLAG_SELFMUTED ? "(Muted)" : "",
					user->adminflags & ADMINFLAG_T_REQUEST ? "(Request to Talk)" : "",
					istalking(user->talking), hr, min, sec); 
			} else {
				ast_cli(a->fd, "%d!%s!%s!%s!%s!%s!%s!%s!%d!%02d:%02d:%02d\n",
					user->user_no,
					S_COR(ast_channel_caller(user->chan)->id.number.valid, ast_channel_caller(user->chan)->id.number.str, ""),
					S_COR(ast_channel_caller(user->chan)->id.name.valid, ast_channel_caller(user->chan)->id.name.str, ""),
					ast_channel_name(user->chan),
					ast_test_flag64(&user->userflags, CONFFLAG_ADMIN) ? "1" : "",
					ast_test_flag64(&user->userflags, CONFFLAG_MONITOR) ? "1" : "",
					user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED) ? "1" : "",
					user->adminflags & ADMINFLAG_T_REQUEST ? "1" : "",
					user->talking, hr, min, sec);
			}
			ao2_ref(user, -1);
		}
		ao2_iterator_destroy(&user_iter);
		if (!concise) {
			ast_cli(a->fd, "%d users in that conference.\n", cnf->users);
		}
		AST_LIST_UNLOCK(&confs);
		return CLI_SUCCESS;
	}
	return CLI_SHOWUSAGE;
}


static char *meetme_cmd_helper(struct ast_cli_args *a)
{
	/* Process the command */
	struct ast_str *cmdline;

	/* Max confno length */
	if (!(cmdline = ast_str_create(MAX_CONFNUM))) {
		return CLI_FAILURE;
	}

	ast_str_set(&cmdline, 0, "%s", a->argv[2]);	/* Argv 2: conference number */
	if (strcasestr(a->argv[1], "lock")) {
		if (strcasecmp(a->argv[1], "lock") == 0) {
			/* Lock */
			ast_str_append(&cmdline, 0, ",L");
		} else {
			/* Unlock */
			ast_str_append(&cmdline, 0, ",l");
		}
	} else if (strcasestr(a->argv[1], "mute")) { 
		if (strcasecmp(a->argv[1], "mute") == 0) {
			/* Mute */
			if (strcasecmp(a->argv[3], "all") == 0) {
				ast_str_append(&cmdline, 0, ",N");
			} else {
				ast_str_append(&cmdline, 0, ",M,%s", a->argv[3]);	
			}
		} else {
			/* Unmute */
			if (strcasecmp(a->argv[3], "all") == 0) {
				ast_str_append(&cmdline, 0, ",n");
			} else {
				ast_str_append(&cmdline, 0, ",m,%s", a->argv[3]);
			}
		}
	} else if (strcasecmp(a->argv[1], "kick") == 0) {
		if (strcasecmp(a->argv[3], "all") == 0) {
			/* Kick all */
			ast_str_append(&cmdline, 0, ",K");
		} else {
			/* Kick a single user */
			ast_str_append(&cmdline, 0, ",k,%s", a->argv[3]);
		}
	} else {
		/*
		 * Should never get here because it is already filtered by the
		 * callers.
		 */
		ast_free(cmdline);
		return CLI_SHOWUSAGE;
	}

	ast_debug(1, "Cmdline: %s\n", ast_str_buffer(cmdline));

	admin_exec(NULL, ast_str_buffer(cmdline));
	ast_free(cmdline);

	return CLI_SUCCESS;
}

static char *meetme_lock_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "meetme {lock|unlock}";
		e->usage =
			"Usage: meetme lock|unlock <confno>\n"
			"       Lock or unlock a conference to new users.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_meetmecmd_lock(a->word, a->pos, a->n);
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	return meetme_cmd_helper(a);
}

static char *meetme_kick_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "meetme kick";
		e->usage =
			"Usage: meetme kick <confno> all|<userno>\n"
			"       Kick a conference or a user in a conference.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_meetmecmd_mute_kick(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	return meetme_cmd_helper(a);
}

static char *meetme_mute_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "meetme {mute|unmute}";
		e->usage =
			"Usage: meetme mute|unmute <confno> all|<userno>\n"
			"       Mute or unmute a conference or a user in a conference.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_meetmecmd_mute_kick(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	return meetme_cmd_helper(a);
}

static const char *sla_hold_str(unsigned int hold_access)
{
	const char *hold = "Unknown";

	switch (hold_access) {
	case SLA_HOLD_OPEN:
		hold = "Open";
		break;
	case SLA_HOLD_PRIVATE:
		hold = "Private";
	default:
		break;
	}

	return hold;
}

static char *sla_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct sla_trunk *trunk;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sla show trunks";
		e->usage =
			"Usage: sla show trunks\n"
			"       This will list all trunks defined in sla.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Configured SLA Trunks ===================================\n"
	            "=============================================================\n"
	            "===\n");
	i = ao2_iterator_init(sla_trunks, 0);
	for (; (trunk = ao2_iterator_next(&i)); ao2_ref(trunk, -1)) {
		struct sla_station_ref *station_ref;
		char ring_timeout[16] = "(none)";

		ao2_lock(trunk);

		if (trunk->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout), "%u Seconds", trunk->ring_timeout);
		}

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		            "=== Trunk Name:       %s\n"
		            "=== ==> Device:       %s\n"
		            "=== ==> AutoContext:  %s\n"
		            "=== ==> RingTimeout:  %s\n"
		            "=== ==> BargeAllowed: %s\n"
		            "=== ==> HoldAccess:   %s\n"
		            "=== ==> Stations ...\n",
		            trunk->name, trunk->device, 
		            S_OR(trunk->autocontext, "(none)"), 
		            ring_timeout,
		            trunk->barge_disabled ? "No" : "Yes",
		            sla_hold_str(trunk->hold_access));

		AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
			ast_cli(a->fd, "===    ==> Station name: %s\n", station_ref->station->name);
		}

		ast_cli(a->fd, "=== ---------------------------------------------------------\n===\n");

		ao2_unlock(trunk);
	}
	ao2_iterator_destroy(&i);
	ast_cli(a->fd, "=============================================================\n\n");

	return CLI_SUCCESS;
}

static const char *trunkstate2str(enum sla_trunk_state state)
{
#define S(e) case e: return # e;
	switch (state) {
	S(SLA_TRUNK_STATE_IDLE)
	S(SLA_TRUNK_STATE_RINGING)
	S(SLA_TRUNK_STATE_UP)
	S(SLA_TRUNK_STATE_ONHOLD)
	S(SLA_TRUNK_STATE_ONHOLD_BYME)
	}
	return "Uknown State";
#undef S
}

static char *sla_show_stations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct sla_station *station;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sla show stations";
		e->usage =
			"Usage: sla show stations\n"
			"       This will list all stations defined in sla.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\n" 
	            "=============================================================\n"
	            "=== Configured SLA Stations =================================\n"
	            "=============================================================\n"
	            "===\n");
	i = ao2_iterator_init(sla_stations, 0);
	for (; (station = ao2_iterator_next(&i)); ao2_ref(station, -1)) {
		struct sla_trunk_ref *trunk_ref;
		char ring_timeout[16] = "(none)";
		char ring_delay[16] = "(none)";

		ao2_lock(station);

		if (station->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout), 
				"%u", station->ring_timeout);
		}
		if (station->ring_delay) {
			snprintf(ring_delay, sizeof(ring_delay), 
				"%u", station->ring_delay);
		}
		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		            "=== Station Name:    %s\n"
		            "=== ==> Device:      %s\n"
		            "=== ==> AutoContext: %s\n"
		            "=== ==> RingTimeout: %s\n"
		            "=== ==> RingDelay:   %s\n"
		            "=== ==> HoldAccess:  %s\n"
		            "=== ==> Trunks ...\n",
		            station->name, station->device,
		            S_OR(station->autocontext, "(none)"), 
		            ring_timeout, ring_delay,
		            sla_hold_str(station->hold_access));
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->ring_timeout) {
				snprintf(ring_timeout, sizeof(ring_timeout),
					"%u", trunk_ref->ring_timeout);
			} else
				strcpy(ring_timeout, "(none)");
			if (trunk_ref->ring_delay) {
				snprintf(ring_delay, sizeof(ring_delay),
					"%u", trunk_ref->ring_delay);
			} else
				strcpy(ring_delay, "(none)");
				ast_cli(a->fd, "===    ==> Trunk Name: %s\n"
			            "===       ==> State:       %s\n"
			            "===       ==> RingTimeout: %s\n"
			            "===       ==> RingDelay:   %s\n",
			            trunk_ref->trunk->name,
			            trunkstate2str(trunk_ref->state),
			            ring_timeout, ring_delay);
		}
		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		            "===\n");

		ao2_unlock(station);
	}
	ao2_iterator_destroy(&i);
	ast_cli(a->fd, "============================================================\n"
	            "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_meetme[] = {
	AST_CLI_DEFINE(meetme_kick_cmd, "Kick a conference or a user in a conference."),
	AST_CLI_DEFINE(meetme_show_cmd, "List all conferences or a specific conference."),
	AST_CLI_DEFINE(meetme_lock_cmd, "Lock or unlock a conference to new users."),
	AST_CLI_DEFINE(meetme_mute_cmd, "Mute or unmute a conference or a user in a conference."),
	AST_CLI_DEFINE(sla_show_trunks, "Show SLA Trunks"),
	AST_CLI_DEFINE(sla_show_stations, "Show SLA Stations"),
};

static void conf_flush(int fd, struct ast_channel *chan)
{
	int x;

	/* read any frames that may be waiting on the channel
	   and throw them away
	*/
	if (chan) {
		struct ast_frame *f;

		/* when no frames are available, this will wait
		   for 1 millisecond maximum
		*/
		while (ast_waitfor(chan, 1) > 0) {
			f = ast_read(chan);
			if (f)
				ast_frfree(f);
			else /* channel was hung up or something else happened */
				break;
		}
	}

	/* flush any data sitting in the pseudo channel */
	x = DAHDI_FLUSH_ALL;
	if (ioctl(fd, DAHDI_FLUSH, &x))
		ast_log(LOG_WARNING, "Error flushing channel\n");

}

/*! \brief Remove the conference from the list and free it.

   We assume that this was called while holding conflock. */
static int conf_free(struct ast_conference *conf)
{
	int x;
	struct announce_listitem *item;

	AST_LIST_REMOVE(&confs, conf, list);

	meetme_stasis_generate_msg(conf, NULL, NULL, meetme_end_type(), NULL);

	if (conf->recording == MEETME_RECORD_ACTIVE) {
		conf->recording = MEETME_RECORD_TERMINATE;
		AST_LIST_UNLOCK(&confs);
		while (1) {
			usleep(1);
			AST_LIST_LOCK(&confs);
			if (conf->recording == MEETME_RECORD_OFF)
				break;
			AST_LIST_UNLOCK(&confs);
		}
	}

	for (x = 0; x < AST_FRAME_BITS; x++) {
		if (conf->transframe[x])
			ast_frfree(conf->transframe[x]);
		if (conf->transpath[x])
			ast_translator_free_path(conf->transpath[x]);
	}
	if (conf->announcethread != AST_PTHREADT_NULL) {
		ast_mutex_lock(&conf->announcelistlock);
		conf->announcethread_stop = 1;
		ast_softhangup(conf->chan, AST_SOFTHANGUP_EXPLICIT);
		ast_cond_signal(&conf->announcelist_addition);
		ast_mutex_unlock(&conf->announcelistlock);
		pthread_join(conf->announcethread, NULL);
	
		while ((item = AST_LIST_REMOVE_HEAD(&conf->announcelist, entry))) {
			/* If it's a voicemail greeting file we don't want to remove it */
			if (!item->vmrec){
				ast_filedelete(item->namerecloc, NULL);
			}
			ao2_ref(item, -1);
		}
		ast_mutex_destroy(&conf->announcelistlock);
	}

	if (conf->origframe)
		ast_frfree(conf->origframe);
	ast_hangup(conf->lchan);
	ast_hangup(conf->chan);
	if (conf->fd >= 0)
		close(conf->fd);
	if (conf->recordingfilename) {
		ast_free(conf->recordingfilename);
	}
	if (conf->usercontainer) {
		ao2_ref(conf->usercontainer, -1);
	}
	if (conf->recordingformat) {
		ast_free(conf->recordingformat);
	}
	ast_mutex_destroy(&conf->playlock);
	ast_mutex_destroy(&conf->listenlock);
	ast_mutex_destroy(&conf->recordthreadlock);
	ast_mutex_destroy(&conf->announcethreadlock);
	ast_free(conf);

	return 0;
}

static void conf_queue_dtmf(const struct ast_conference *conf,
	const struct ast_conf_user *sender, struct ast_frame *f)
{
	struct ast_conf_user *user;
	struct ao2_iterator user_iter;

	user_iter = ao2_iterator_init(conf->usercontainer, 0);
	while ((user = ao2_iterator_next(&user_iter))) {
		if (user == sender) {
			ao2_ref(user, -1);
			continue;
		}
		if (ast_write(user->chan, f) < 0)
			ast_log(LOG_WARNING, "Error writing frame to channel %s\n", ast_channel_name(user->chan));
		ao2_ref(user, -1);
	}
	ao2_iterator_destroy(&user_iter);
}

static void sla_queue_event_full(enum sla_event_type type, 
	struct sla_trunk_ref *trunk_ref, struct sla_station *station, int lock)
{
	struct sla_event *event;

	if (sla.thread == AST_PTHREADT_NULL) {
		ao2_ref(station, -1);
		ao2_ref(trunk_ref, -1);
		return;
	}

	if (!(event = ast_calloc(1, sizeof(*event)))) {
		ao2_ref(station, -1);
		ao2_ref(trunk_ref, -1);
		return;
	}

	event->type = type;
	event->trunk_ref = trunk_ref;
	event->station = station;

	if (!lock) {
		AST_LIST_INSERT_TAIL(&sla.event_q, event, entry);
		return;
	}

	ast_mutex_lock(&sla.lock);
	AST_LIST_INSERT_TAIL(&sla.event_q, event, entry);
	ast_cond_signal(&sla.cond);
	ast_mutex_unlock(&sla.lock);
}

static void sla_queue_event_nolock(enum sla_event_type type)
{
	sla_queue_event_full(type, NULL, NULL, 0);
}

static void sla_queue_event(enum sla_event_type type)
{
	sla_queue_event_full(type, NULL, NULL, 1);
}

/*! \brief Queue a SLA event from the conference */
static void sla_queue_event_conf(enum sla_event_type type, struct ast_channel *chan,
	struct ast_conference *conf)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref = NULL;
	char *trunk_name;
	struct ao2_iterator i;

	trunk_name = ast_strdupa(conf->confno);
	strsep(&trunk_name, "_");
	if (ast_strlen_zero(trunk_name)) {
		ast_log(LOG_ERROR, "Invalid conference name for SLA - '%s'!\n", conf->confno);
		return;
	}

	i = ao2_iterator_init(sla_stations, 0);
	while ((station = ao2_iterator_next(&i))) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->chan == chan && !strcmp(trunk_ref->trunk->name, trunk_name)) {
				ao2_ref(trunk_ref, 1);
				break;
			}
		}
		ao2_unlock(station);
		if (trunk_ref) {
			/* station reference given to sla_queue_event_full() */
			break;
		}
		ao2_ref(station, -1);
	}
	ao2_iterator_destroy(&i);

	if (!trunk_ref) {
		ast_debug(1, "Trunk not found for event!\n");
		return;
	}

	sla_queue_event_full(type, trunk_ref, station, 1);
}

/*! \brief Decrement reference counts, as incremented by find_conf() */
static int dispose_conf(struct ast_conference *conf)
{
	int res = 0;
	int confno_int = 0;

	AST_LIST_LOCK(&confs);
	if (ast_atomic_dec_and_test(&conf->refcount)) {
		/* Take the conference room number out of an inuse state */
		if ((sscanf(conf->confno, "%4d", &confno_int) == 1) && (confno_int >= 0 && confno_int < 1024)) {
			conf_map[confno_int] = 0;
		}
		conf_free(conf);
		res = 1;
	}
	AST_LIST_UNLOCK(&confs);

	return res;
}

static int rt_extend_conf(const char *confno)
{
	char currenttime[32];
	char endtime[32];
	struct timeval now;
	struct ast_tm tm;
	struct ast_variable *var, *orig_var;
	char bookid[51];

	if (!extendby) {
		return 0;
	}

	now = ast_tvnow();

	ast_localtime(&now, &tm, NULL);
	ast_strftime(currenttime, sizeof(currenttime), DATE_FORMAT, &tm);

	var = ast_load_realtime("meetme", "confno",
		confno, "startTime<= ", currenttime,
		"endtime>= ", currenttime, NULL);

	orig_var = var;

	/* Identify the specific RealTime conference */
	while (var) {
		if (!strcasecmp(var->name, "bookid")) {
			ast_copy_string(bookid, var->value, sizeof(bookid));
		}
		if (!strcasecmp(var->name, "endtime")) {
			ast_copy_string(endtime, var->value, sizeof(endtime));
		}

		var = var->next;
	}
	ast_variables_destroy(orig_var);

	ast_strptime(endtime, DATE_FORMAT, &tm);
	now = ast_mktime(&tm, NULL);

	now.tv_sec += extendby;

	ast_localtime(&now, &tm, NULL);
	ast_strftime(currenttime, sizeof(currenttime), DATE_FORMAT, &tm);
	strcat(currenttime, "0"); /* Seconds needs to be 00 */

	var = ast_load_realtime("meetme", "confno",
		confno, "startTime<= ", currenttime,
		"endtime>= ", currenttime, NULL);

	/* If there is no conflict with extending the conference, update the DB */
	if (!var) {
		ast_debug(3, "Trying to update the endtime of Conference %s to %s\n", confno, currenttime);
		ast_update_realtime("meetme", "bookid", bookid, "endtime", currenttime, NULL);
		return 0;

	}

	ast_variables_destroy(var);
	return -1;
}

static void conf_start_moh(struct ast_channel *chan, const char *musicclass)
{
	char *original_moh;

	ast_channel_lock(chan);
	original_moh = ast_strdupa(ast_channel_musicclass(chan));
	ast_channel_musicclass_set(chan, musicclass);
	ast_channel_unlock(chan);

	ast_moh_start(chan, original_moh, NULL);

	ast_channel_lock(chan);
	ast_channel_musicclass_set(chan, original_moh);
	ast_channel_unlock(chan);
}

static const char *get_announce_filename(enum announcetypes type)
{
	switch (type) {
	case CONF_HASLEFT:
		return "conf-hasleft";
		break;
	case CONF_HASJOIN:
		return "conf-hasjoin";
		break;
	default:
		return "";
	}
}

static void *announce_thread(void *data)
{
	struct announce_listitem *current;
	struct ast_conference *conf = data;
	int res;
	char filename[PATH_MAX] = "";
	AST_LIST_HEAD_NOLOCK(, announce_listitem) local_list;
	AST_LIST_HEAD_INIT_NOLOCK(&local_list);

	while (!conf->announcethread_stop) {
		ast_mutex_lock(&conf->announcelistlock);
		if (conf->announcethread_stop) {
			ast_mutex_unlock(&conf->announcelistlock);
			break;
		}
		if (AST_LIST_EMPTY(&conf->announcelist))
			ast_cond_wait(&conf->announcelist_addition, &conf->announcelistlock);

		AST_LIST_APPEND_LIST(&local_list, &conf->announcelist, entry);
		AST_LIST_HEAD_INIT_NOLOCK(&conf->announcelist);

		ast_mutex_unlock(&conf->announcelistlock);
		if (conf->announcethread_stop) {
			break;
		}

		for (res = 1; !conf->announcethread_stop && (current = AST_LIST_REMOVE_HEAD(&local_list, entry)); ao2_ref(current, -1)) {
			ast_debug(1, "About to play %s\n", current->namerecloc);
			if (!ast_fileexists(current->namerecloc, NULL, NULL))
				continue;
			if ((current->confchan) && (current->confusers > 1) && !ast_check_hangup(current->confchan)) {
				if (!ast_streamfile(current->confchan, current->namerecloc, current->language))
					res = ast_waitstream(current->confchan, "");
				if (!res) {
					ast_copy_string(filename, get_announce_filename(current->announcetype), sizeof(filename));
					if (!ast_streamfile(current->confchan, filename, current->language))
						ast_waitstream(current->confchan, "");
				}
			}
			if (current->announcetype == CONF_HASLEFT && current->announcetype && !current->vmrec) {
				/* only remove it if it isn't a VM recording file */
				ast_filedelete(current->namerecloc, NULL);
			}
		}
	}

	/* thread marked to stop, clean up */
	while ((current = AST_LIST_REMOVE_HEAD(&local_list, entry))) {
		/* only delete if it's a vm rec */
		if (!current->vmrec) {
			ast_filedelete(current->namerecloc, NULL);
		}
		ao2_ref(current, -1);
	}
	return NULL;
}

static int can_write(struct ast_channel *chan, struct ast_flags64 *confflags)
{
	if (!ast_test_flag64(confflags, CONFFLAG_NO_AUDIO_UNTIL_UP)) {
		return 1;
	}

	return (ast_channel_state(chan) == AST_STATE_UP);
}

static void send_talking_event(struct ast_channel *chan, struct ast_conference *conf, struct ast_conf_user *user, int talking)
{
	RAII_VAR(struct ast_json *, status_blob, status_to_json(talking), ast_json_unref);
	meetme_stasis_generate_msg(conf, chan, user, meetme_talking_type(), status_blob);
}

static void set_user_talking(struct ast_channel *chan, struct ast_conference *conf, struct ast_conf_user *user, int talking, int monitor)
{
	int last_talking = user->talking;
	if (last_talking == talking)
		return;

	user->talking = talking;

	if (monitor) {
		/* Check if talking state changed. Take care of -1 which means unmonitored */
		int was_talking = (last_talking > 0);
		int now_talking = (talking > 0);
		if (was_talking != now_talking) {
			send_talking_event(chan, conf, user, now_talking);
		}
	}
}

static int user_set_hangup_cb(void *obj, void *check_admin_arg, int flags)
{
	struct ast_conf_user *user = obj;
	/* actual pointer contents of check_admin_arg is irrelevant */

	if (!check_admin_arg || (check_admin_arg && !ast_test_flag64(&user->userflags, CONFFLAG_ADMIN))) {
		user->adminflags |= ADMINFLAG_HANGUP;
	}
	return 0;
}

static int user_set_kickme_cb(void *obj, void *check_admin_arg, int flags)
{
	struct ast_conf_user *user = obj;
	/* actual pointer contents of check_admin_arg is irrelevant */

	if (!check_admin_arg || (check_admin_arg && !ast_test_flag64(&user->userflags, CONFFLAG_ADMIN))) {
		user->adminflags |= ADMINFLAG_KICKME;
	}
	return 0;
}

static int user_set_unmuted_cb(void *obj, void *check_admin_arg, int flags)
{
	struct ast_conf_user *user = obj;
	/* actual pointer contents of check_admin_arg is irrelevant */

	if (!check_admin_arg || !ast_test_flag64(&user->userflags, CONFFLAG_ADMIN)) {
		user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED | ADMINFLAG_T_REQUEST);
	}
	return 0;
}

static int user_set_muted_cb(void *obj, void *check_admin_arg, int flags)
{
	struct ast_conf_user *user = obj;
	/* actual pointer contents of check_admin_arg is irrelevant */

	if (!check_admin_arg || !ast_test_flag64(&user->userflags, CONFFLAG_ADMIN)) {
		user->adminflags |= ADMINFLAG_MUTED;
	}
	return 0;
}

enum menu_modes {
	MENU_DISABLED = 0,
	MENU_NORMAL,
	MENU_ADMIN,
	MENU_ADMIN_EXTENDED,
};

/*! \internal
 * \brief Processes menu options for the standard menu (accessible through the 's' option for app_meetme)
 *
 * \param menu_mode a pointer to the currently active menu_mode.
 * \param dtmf a pointer to the dtmf value currently being processed against the menu.
 * \param conf the active conference for which the user has called the menu from.
 * \param confflags flags used by conf for various options
 * \param chan ast_channel belonging to the user who called the menu
 * \param user which meetme conference user invoked the menu
 */
static void meetme_menu_normal(enum menu_modes *menu_mode, int *dtmf, struct ast_conference *conf, struct ast_flags64 *confflags, struct ast_channel *chan, struct ast_conf_user *user)
{
	switch (*dtmf) {
	case '1': /* Un/Mute */
		*menu_mode = MENU_DISABLED;

		/* user can only toggle the self-muted state */
		user->adminflags ^= ADMINFLAG_SELFMUTED;

		/* they can't override the admin mute state */
		if (ast_test_flag64(confflags, CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED))) {
			if (!ast_streamfile(chan, "conf-muted", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		} else {
			if (!ast_streamfile(chan, "conf-unmuted", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		}
		break;

	case '2':
		*menu_mode = MENU_DISABLED;
		if (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) {
			user->adminflags |= ADMINFLAG_T_REQUEST;
		}

		if (user->adminflags & ADMINFLAG_T_REQUEST) {
			if (!ast_streamfile(chan, "beep", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		}
		break;

	case '4':
		tweak_listen_volume(user, VOL_DOWN);
		break;
	case '5':
		/* Extend RT conference */
		if (rt_schedule) {
			rt_extend_conf(conf->confno);
		}
		*menu_mode = MENU_DISABLED;
		break;

	case '6':
		tweak_listen_volume(user, VOL_UP);
		break;

	case '7':
		tweak_talk_volume(user, VOL_DOWN);
		break;

	case '8':
		*menu_mode = MENU_DISABLED;
		break;

	case '9':
		tweak_talk_volume(user, VOL_UP);
		break;

	default:
		*menu_mode = MENU_DISABLED;
		if (!ast_streamfile(chan, "conf-errormenu", ast_channel_language(chan))) {
			ast_waitstream(chan, "");
		}
		break;
	}
}

/*! \internal
 * \brief Processes menu options for the adminstrator menu (accessible through the 's' option for app_meetme)
 *
 * \param menu_mode a pointer to the currently active menu_mode.
 * \param dtmf a pointer to the dtmf value currently being processed against the menu.
 * \param conf the active conference for which the user has called the menu from.
 * \param confflags flags used by conf for various options
 * \param chan ast_channel belonging to the user who called the menu
 * \param user which meetme conference user invoked the menu
 */
static void meetme_menu_admin(enum menu_modes *menu_mode, int *dtmf, struct ast_conference *conf, struct ast_flags64 *confflags, struct ast_channel *chan, struct ast_conf_user *user)
{
	switch(*dtmf) {
	case '1': /* Un/Mute */
		*menu_mode = MENU_DISABLED;
		/* for admin, change both admin and use flags */
		if (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) {
			user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
		} else {
			user->adminflags |= (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
		}

		if (ast_test_flag64(confflags, CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED))) {
			if (!ast_streamfile(chan, "conf-muted", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		} else {
			if (!ast_streamfile(chan, "conf-unmuted", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		}
		break;

	case '2': /* Un/Lock the Conference */
		*menu_mode = MENU_DISABLED;
		if (conf->locked) {
			conf->locked = 0;
			if (!ast_streamfile(chan, "conf-unlockednow", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		} else {
			conf->locked = 1;
			if (!ast_streamfile(chan, "conf-lockednow", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		}
		break;

	case '3': /* Eject last user */
	{
		struct ast_conf_user *usr = NULL;
		int max_no = 0;
		ao2_callback(conf->usercontainer, OBJ_NODATA, user_max_cmp, &max_no);
		*menu_mode = MENU_DISABLED;
		usr = ao2_find(conf->usercontainer, &max_no, 0);
		if ((ast_channel_name(usr->chan) == ast_channel_name(chan)) || ast_test_flag64(&usr->userflags, CONFFLAG_ADMIN)) {
			if (!ast_streamfile(chan, "conf-errormenu", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		} else {
			usr->adminflags |= ADMINFLAG_KICKME;
		}
		ao2_ref(usr, -1);
		ast_stopstream(chan);
		break;
	}

	case '4':
		tweak_listen_volume(user, VOL_DOWN);
		break;

	case '5':
		/* Extend RT conference */
		if (rt_schedule) {
			if (!rt_extend_conf(conf->confno)) {
				if (!ast_streamfile(chan, "conf-extended", ast_channel_language(chan))) {
					ast_waitstream(chan, "");
				}
			} else {
				if (!ast_streamfile(chan, "conf-nonextended", ast_channel_language(chan))) {
					ast_waitstream(chan, "");
				}
			}
			ast_stopstream(chan);
		}
		*menu_mode = MENU_DISABLED;
		break;

	case '6':
		tweak_listen_volume(user, VOL_UP);
		break;

	case '7':
		tweak_talk_volume(user, VOL_DOWN);
		break;

	case '8':
		if (!ast_streamfile(chan, "conf-adminmenu-menu8", ast_channel_language(chan))) {
			/* If the user provides DTMF while playing the sound, we want to drop right into the extended menu function with new DTMF once we get out of here. */
			*dtmf = ast_waitstream(chan, AST_DIGIT_ANY);
			ast_stopstream(chan);
		}
		*menu_mode = MENU_ADMIN_EXTENDED;
		break;

	case '9':
		tweak_talk_volume(user, VOL_UP);
		break;
	default:
		menu_mode = MENU_DISABLED;
		/* Play an error message! */
		if (!ast_streamfile(chan, "conf-errormenu", ast_channel_language(chan))) {
			ast_waitstream(chan, "");
		}
		break;
	}

}

/*! \internal
 * \brief Processes menu options for the extended administrator menu (accessible through option 8 on the administrator menu)
 *
 * \param menu_mode a pointer to the currently active menu_mode.
 * \param dtmf a pointer to the dtmf value currently being processed against the menu.
 * \param conf the active conference for which the user has called the menu from.
 * \param confflags flags used by conf for various options
 * \param chan ast_channel belonging to the user who called the menu
 * \param user which meetme conference user invoked the menu
 * \param recordingtmp character buffer which may hold the name of the conference recording file
 */
static void meetme_menu_admin_extended(enum menu_modes *menu_mode, int *dtmf,
	struct ast_conference *conf, struct ast_flags64 *confflags, struct ast_channel *chan,
	struct ast_conf_user *user, char *recordingtmp, int recordingtmp_size,
	struct ast_format_cap *cap_slin)
{
	int keepplaying;
	int playednamerec;
	int res;
	struct ao2_iterator user_iter;
	struct ast_conf_user *usr = NULL;

	switch(*dtmf) {
	case '1': /* *81 Roll call */
		keepplaying = 1;
		playednamerec = 0;
		if (conf->users == 1) {
			if (keepplaying && !ast_streamfile(chan, "conf-onlyperson", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0) {
					keepplaying = 0;
				}
			}
		} else if (conf->users == 2) {
			if (keepplaying && !ast_streamfile(chan, "conf-onlyone", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0) {
					keepplaying = 0;
				}
			}
		} else {
			if (keepplaying && !ast_streamfile(chan, "conf-thereare", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0) {
					keepplaying = 0;
				}
			}
			if (keepplaying) {
				res = ast_say_number(chan, conf->users - 1, AST_DIGIT_ANY, ast_channel_language(chan), (char *) NULL);
				ast_stopstream(chan);
				if (res > 0) {
					keepplaying = 0;
				}
			}
			if (keepplaying && !ast_streamfile(chan, "conf-otherinparty", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0) {
					keepplaying = 0;
				}
			}
		}
		user_iter = ao2_iterator_init(conf->usercontainer, 0);
		while((usr = ao2_iterator_next(&user_iter))) {
			if (ast_fileexists(usr->namerecloc, NULL, NULL)) {
				if (keepplaying && !ast_streamfile(chan, usr->namerecloc, ast_channel_language(chan))) {
					res = ast_waitstream(chan, AST_DIGIT_ANY);
					ast_stopstream(chan);
					if (res > 0) {
						keepplaying = 0;
					}
				}
				playednamerec = 1;
			}
			ao2_ref(usr, -1);
		}
		ao2_iterator_destroy(&user_iter);
		if (keepplaying && playednamerec && !ast_streamfile(chan, "conf-roll-callcomplete", ast_channel_language(chan))) {
			res = ast_waitstream(chan, AST_DIGIT_ANY);
			ast_stopstream(chan);
			if (res > 0) {
				keepplaying = 0;
			}
		}

		*menu_mode = MENU_DISABLED;
		break;

	case '2': /* *82 Eject all non-admins */
		if (conf->users == 1) {
			if(!ast_streamfile(chan, "conf-errormenu", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		} else {
			ao2_callback(conf->usercontainer, OBJ_NODATA, user_set_kickme_cb, &conf);
		}
		ast_stopstream(chan);
		*menu_mode = MENU_DISABLED;
		break;

	case '3': /* *83 (Admin) mute/unmute all non-admins */
		if(conf->gmuted) {
			conf->gmuted = 0;
			ao2_callback(conf->usercontainer, OBJ_NODATA, user_set_unmuted_cb, &conf);
			if (!ast_streamfile(chan, "conf-now-unmuted", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		} else {
			conf->gmuted = 1;
			ao2_callback(conf->usercontainer, OBJ_NODATA, user_set_muted_cb, &conf);
			if (!ast_streamfile(chan, "conf-now-muted", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		}
		ast_stopstream(chan);
		*menu_mode = MENU_DISABLED;
		break;

	case '4': /* *84 Record conference */
		if (conf->recording != MEETME_RECORD_ACTIVE) {
			ast_set_flag64(confflags, CONFFLAG_RECORDCONF);
			if (!conf->recordingfilename) {
				const char *var;
				ast_channel_lock(chan);
				if ((var = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFILE"))) {
					conf->recordingfilename = ast_strdup(var);
				}
				if ((var = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFORMAT"))) {
					conf->recordingformat = ast_strdup(var);
				}
				ast_channel_unlock(chan);
				if (!conf->recordingfilename) {
					snprintf(recordingtmp, recordingtmp_size, "meetme-conf-rec-%s-%s", conf->confno, ast_channel_uniqueid(chan));
					conf->recordingfilename = ast_strdup(recordingtmp);
				}
				if (!conf->recordingformat) {
					conf->recordingformat = ast_strdup("wav");
				}
				ast_verb(4, "Starting recording of MeetMe Conference %s into file %s.%s.\n",
				conf->confno, conf->recordingfilename, conf->recordingformat);
			}

			ast_mutex_lock(&conf->recordthreadlock);
			if ((conf->recordthread == AST_PTHREADT_NULL) && ast_test_flag64(confflags, CONFFLAG_RECORDCONF) && ((conf->lchan = ast_request("DAHDI", cap_slin, NULL, chan, "pseudo", NULL)))) {
				struct dahdi_confinfo dahdic;

				ast_set_read_format(conf->lchan, ast_format_slin);
				ast_set_write_format(conf->lchan, ast_format_slin);
				dahdic.chan = 0;
				dahdic.confno = conf->dahdiconf;
				dahdic.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
				if (ioctl(ast_channel_fd(conf->lchan, 0), DAHDI_SETCONF, &dahdic)) {
					ast_log(LOG_WARNING, "Error starting listen channel\n");
					ast_hangup(conf->lchan);
					conf->lchan = NULL;
				} else {
					ast_pthread_create_detached_background(&conf->recordthread, NULL, recordthread, conf);
				}
			}
			ast_mutex_unlock(&conf->recordthreadlock);
			if (!ast_streamfile(chan, "conf-now-recording", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
		}

		ast_stopstream(chan);
		*menu_mode = MENU_DISABLED;
		break;

	case '8': /* *88 Exit the menu and return to the conference... without an error message */
		ast_stopstream(chan);
		*menu_mode = MENU_DISABLED;
		break;

	default:
		if (!ast_streamfile(chan, "conf-errormenu", ast_channel_language(chan))) {
			ast_waitstream(chan, "");
		}
		ast_stopstream(chan);
		*menu_mode = MENU_DISABLED;
		break;
	}
}

/*! \internal
 * \brief Processes menu options for the various menu types (accessible through the 's' option for app_meetme)
 *
 * \param menu_mode a pointer to the currently active menu_mode.
 * \param dtmf a pointer to the dtmf value currently being processed against the menu.
 * \param conf the active conference for which the user has called the menu from.
 * \param confflags flags used by conf for various options
 * \param chan ast_channel belonging to the user who called the menu
 * \param user which meetme conference user invoked the menu
 * \param recordingtmp character buffer which may hold the name of the conference recording file
 */
static void meetme_menu(enum menu_modes *menu_mode, int *dtmf,
	struct ast_conference *conf, struct ast_flags64 *confflags, struct ast_channel *chan,
	struct ast_conf_user *user, char *recordingtmp, int recordingtmp_size,
	struct ast_format_cap *cap_slin)
{
	switch (*menu_mode) {
	case MENU_DISABLED:
		break;
	case MENU_NORMAL:
		meetme_menu_normal(menu_mode, dtmf, conf, confflags, chan, user);
		break;
	case MENU_ADMIN:
		meetme_menu_admin(menu_mode, dtmf, conf, confflags, chan, user);
		/* Admin Menu is capable of branching into another menu, in which case it will reset dtmf and change the menu mode. */
		if (*menu_mode != MENU_ADMIN_EXTENDED || (*dtmf <= 0)) {
			break;
		}
	case MENU_ADMIN_EXTENDED:
		meetme_menu_admin_extended(menu_mode, dtmf, conf, confflags, chan, user,
			recordingtmp, recordingtmp_size, cap_slin);
		break;
	}
}

static int conf_run(struct ast_channel *chan, struct ast_conference *conf, struct ast_flags64 *confflags, char *optargs[])
{
	struct ast_conf_user *user = NULL;
	int fd;
	struct dahdi_confinfo dahdic, dahdic_empty;
	struct ast_frame *f;
	struct ast_channel *c;
	struct ast_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int retrydahdi;
	int origfd;
	int musiconhold = 0, mohtempstopped = 0;
	int firstpass = 0;
	int lastmarked = 0;
	int currentmarked = 0;
	int ret = -1;
	int x;
	enum menu_modes menu_mode = MENU_DISABLED;
	int talkreq_manager = 0;
	int using_pseudo = 0;
	int duration = 20;
	int sent_event = 0;
	int checked = 0;
	int announcement_played = 0;
	struct timeval now;
	struct ast_dsp *dsp = NULL;
	struct ast_app *agi_app;
	char *agifile, *mod_speex;
	const char *agifiledefault = "conf-background.agi", *tmpvar;
	char meetmesecs[30] = "";
	char exitcontext[AST_MAX_CONTEXT] = "";
	char recordingtmp[AST_MAX_EXTENSION] = "";
	char members[10] = "";
	int dtmf = 0, opt_waitmarked_timeout = 0;
	time_t timeout = 0;
	struct dahdi_bufferinfo bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;
	char *exitkeys = NULL;
	unsigned int calldurationlimit = 0;
	long timelimit = 0;
	long play_warning = 0;
	long warning_freq = 0;
	const char *warning_sound = NULL;
	const char *end_sound = NULL;
	char *parse;
	long time_left_ms = 0;
	struct timeval nexteventts = { 0, };
	int to;
	int setusercount = 0;
	int confsilence = 0, totalsilence = 0;
	char *mailbox, *context;
	struct ast_format_cap *cap_slin = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	if (!cap_slin) {
		goto conf_run_cleanup;
	}
	ast_format_cap_append(cap_slin, ast_format_slin, 0);

	if (!(user = ao2_alloc(sizeof(*user), NULL))) {
		goto conf_run_cleanup;
	}

	/* Possible timeout waiting for marked user */
	if (ast_test_flag64(confflags, CONFFLAG_WAITMARKED) &&
		!ast_strlen_zero(optargs[OPT_ARG_WAITMARKED]) &&
		(sscanf(optargs[OPT_ARG_WAITMARKED], "%30d", &opt_waitmarked_timeout) == 1) &&
		(opt_waitmarked_timeout > 0)) {
		timeout = time(NULL) + opt_waitmarked_timeout;
	}

	if (ast_test_flag64(confflags, CONFFLAG_DURATION_STOP) && !ast_strlen_zero(optargs[OPT_ARG_DURATION_STOP])) {
		calldurationlimit = atoi(optargs[OPT_ARG_DURATION_STOP]);
		ast_verb(3, "Setting call duration limit to %u seconds.\n", calldurationlimit);
	}

	if (ast_test_flag64(confflags, CONFFLAG_DURATION_LIMIT) && !ast_strlen_zero(optargs[OPT_ARG_DURATION_LIMIT])) {
		char *limit_str, *warning_str, *warnfreq_str;
		const char *var;

		parse = optargs[OPT_ARG_DURATION_LIMIT];
		limit_str = strsep(&parse, ":");
		warning_str = strsep(&parse, ":");
		warnfreq_str = parse;

		timelimit = atol(limit_str);
		if (warning_str)
			play_warning = atol(warning_str);
		if (warnfreq_str)
			warning_freq = atol(warnfreq_str);

		if (!timelimit) {
			timelimit = play_warning = warning_freq = 0;
			warning_sound = NULL;
		} else if (play_warning > timelimit) {
			if (!warning_freq) {
				play_warning = 0;
			} else {
				while (play_warning > timelimit)
					play_warning -= warning_freq;
				if (play_warning < 1)
					play_warning = warning_freq = 0;
			}
		}

		ast_verb(3, "Setting conference duration limit to: %ldms.\n", timelimit);
		if (play_warning) {
			ast_verb(3, "Setting warning time to %ldms from the conference duration limit.\n", play_warning);
		}
		if (warning_freq) {
			ast_verb(3, "Setting warning frequency to %ldms.\n", warning_freq);
		}

		ast_channel_lock(chan);
		if ((var = pbx_builtin_getvar_helper(chan, "CONF_LIMIT_WARNING_FILE"))) {
			var = ast_strdupa(var);
		}
		ast_channel_unlock(chan);

		warning_sound = var ? var : "timeleft";

		ast_channel_lock(chan);
		if ((var = pbx_builtin_getvar_helper(chan, "CONF_LIMIT_TIMEOUT_FILE"))) {
			var = ast_strdupa(var);
		}
		ast_channel_unlock(chan);

		end_sound = var ? var : NULL;

		/* undo effect of S(x) in case they are both used */
		calldurationlimit = 0;
		/* more efficient do it like S(x) does since no advanced opts */
		if (!play_warning && !end_sound && timelimit) {
			calldurationlimit = timelimit / 1000;
			timelimit = play_warning = warning_freq = 0;
		} else {
			ast_debug(2, "Limit Data for this call:\n");
			ast_debug(2, "- timelimit     = %ld\n", timelimit);
			ast_debug(2, "- play_warning  = %ld\n", play_warning);
			ast_debug(2, "- warning_freq  = %ld\n", warning_freq);
			ast_debug(2, "- warning_sound = %s\n", warning_sound ? warning_sound : "UNDEF");
			ast_debug(2, "- end_sound     = %s\n", end_sound ? end_sound : "UNDEF");
		}
	}

	/* Get exit keys */
	if (ast_test_flag64(confflags, CONFFLAG_KEYEXIT)) {
		if (!ast_strlen_zero(optargs[OPT_ARG_EXITKEYS]))
			exitkeys = ast_strdupa(optargs[OPT_ARG_EXITKEYS]);
		else
			exitkeys = ast_strdupa("#"); /* Default */
	}
	
	if (ast_test_flag64(confflags, CONFFLAG_RECORDCONF)) {
		if (!conf->recordingfilename) {
			const char *var;
			ast_channel_lock(chan);
			if ((var = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFILE"))) {
				conf->recordingfilename = ast_strdup(var);
			}
			if ((var = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFORMAT"))) {
				conf->recordingformat = ast_strdup(var);
			}
			ast_channel_unlock(chan);
			if (!conf->recordingfilename) {
				snprintf(recordingtmp, sizeof(recordingtmp), "meetme-conf-rec-%s-%s", conf->confno, ast_channel_uniqueid(chan));
				conf->recordingfilename = ast_strdup(recordingtmp);
			}
			if (!conf->recordingformat) {
				conf->recordingformat = ast_strdup("wav");
			}
			ast_verb(4, "Starting recording of MeetMe Conference %s into file %s.%s.\n",
				    conf->confno, conf->recordingfilename, conf->recordingformat);
		}
	}

	ast_mutex_lock(&conf->recordthreadlock);
	if ((conf->recordthread == AST_PTHREADT_NULL) && ast_test_flag64(confflags, CONFFLAG_RECORDCONF) &&
		((conf->lchan = ast_request("DAHDI", cap_slin, NULL, chan, "pseudo", NULL)))) {
		ast_set_read_format(conf->lchan, ast_format_slin);
		ast_set_write_format(conf->lchan, ast_format_slin);
		dahdic.chan = 0;
		dahdic.confno = conf->dahdiconf;
		dahdic.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
		if (ioctl(ast_channel_fd(conf->lchan, 0), DAHDI_SETCONF, &dahdic)) {
			ast_log(LOG_WARNING, "Error starting listen channel\n");
			ast_hangup(conf->lchan);
			conf->lchan = NULL;
		} else {
			ast_pthread_create_detached_background(&conf->recordthread, NULL, recordthread, conf);
		}
	}
	ast_mutex_unlock(&conf->recordthreadlock);

	ast_mutex_lock(&conf->announcethreadlock);
	if ((conf->announcethread == AST_PTHREADT_NULL) && !ast_test_flag64(confflags, CONFFLAG_QUIET) &&
		ast_test_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW | CONFFLAG_INTROUSER_VMREC)) {
		ast_mutex_init(&conf->announcelistlock);
		AST_LIST_HEAD_INIT_NOLOCK(&conf->announcelist);
		ast_pthread_create_background(&conf->announcethread, NULL, announce_thread, conf);
	}
	ast_mutex_unlock(&conf->announcethreadlock);

	time(&user->jointime);
	
	user->timelimit = timelimit;
	user->play_warning = play_warning;
	user->warning_freq = warning_freq;
	user->warning_sound = warning_sound;
	user->end_sound = end_sound;	
	
	if (calldurationlimit > 0) {
		time(&user->kicktime);
		user->kicktime = user->kicktime + calldurationlimit;
	}
	
	if (ast_tvzero(user->start_time))
		user->start_time = ast_tvnow();
	time_left_ms = user->timelimit;
	
	if (user->timelimit) {
		nexteventts = ast_tvadd(user->start_time, ast_samp2tv(user->timelimit, 1000));
		nexteventts = ast_tvsub(nexteventts, ast_samp2tv(user->play_warning, 1000));
	}

	if (conf->locked && (!ast_test_flag64(confflags, CONFFLAG_ADMIN))) {
		/* Sorry, but this conference is locked! */	
		if (!ast_streamfile(chan, "conf-locked", ast_channel_language(chan)))
			ast_waitstream(chan, "");
		goto outrun;
	}

   	ast_mutex_lock(&conf->playlock);

	if (rt_schedule && conf->maxusers) {
		if (conf->users >= conf->maxusers) {
			/* Sorry, but this confernce has reached the participant limit! */	
			ast_mutex_unlock(&conf->playlock);
			if (!ast_streamfile(chan, "conf-full", ast_channel_language(chan)))
				ast_waitstream(chan, "");
			goto outrun;
		}
	}

	ao2_lock(conf->usercontainer);
	ao2_callback(conf->usercontainer, OBJ_NODATA, user_max_cmp, &user->user_no);
	user->user_no++;
	ao2_link(conf->usercontainer, user);
	ao2_unlock(conf->usercontainer);

	user->chan = chan;
	user->userflags = *confflags;
	user->adminflags = ast_test_flag64(confflags, CONFFLAG_STARTMUTED) ? ADMINFLAG_SELFMUTED : 0;
	user->adminflags |= (conf->gmuted) ? ADMINFLAG_MUTED : 0;
	user->talking = -1;

	ast_mutex_unlock(&conf->playlock);

	if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && (ast_test_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW | CONFFLAG_INTROUSER_VMREC))) {
		char destdir[PATH_MAX];

		snprintf(destdir, sizeof(destdir), "%s/meetme", ast_config_AST_SPOOL_DIR);

		if (ast_mkdir(destdir, 0777) != 0) {
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", destdir, strerror(errno));
			goto outrun;
		}

		if (ast_test_flag64(confflags, CONFFLAG_INTROUSER_VMREC)){
			context = ast_strdupa(optargs[OPT_ARG_INTROUSER_VMREC]);
			mailbox = strsep(&context, "@");

			if (ast_strlen_zero(mailbox)) {
				/* invalid input, clear the v flag*/
				ast_clear_flag64(confflags,CONFFLAG_INTROUSER_VMREC);
				ast_log(LOG_WARNING,"You must specify a mailbox in the v() option\n");
			} else {
				if (ast_strlen_zero(context)) {
				    context = "default";
				}
				/* if there is no mailbox we don't need to do this logic  */
				snprintf(user->namerecloc, sizeof(user->namerecloc),
					 "%s/voicemail/%s/%s/greet",ast_config_AST_SPOOL_DIR,context,mailbox);

				/* if the greeting doesn't exist then use the temp file method instead, clear flag v */
				if (!ast_fileexists(user->namerecloc, NULL, NULL)){
					snprintf(user->namerecloc, sizeof(user->namerecloc),
						 "%s/meetme-username-%s-%d", destdir,
						 conf->confno, user->user_no);
					ast_clear_flag64(confflags, CONFFLAG_INTROUSER_VMREC);
				}
			}
		} else {
			snprintf(user->namerecloc, sizeof(user->namerecloc),
				 "%s/meetme-username-%s-%d", destdir,
				 conf->confno, user->user_no);
		}

		res = 0;
		if (ast_test_flag64(confflags, CONFFLAG_INTROUSERNOREVIEW) && !ast_fileexists(user->namerecloc, NULL, NULL))
			res = ast_play_and_record(chan, "vm-rec-name", user->namerecloc, 10, "sln", &duration, NULL, ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE), 0, NULL);
		else if (ast_test_flag64(confflags, CONFFLAG_INTROUSER) && !ast_fileexists(user->namerecloc, NULL, NULL))
			res = ast_record_review(chan, "vm-rec-name", user->namerecloc, 10, "sln", &duration, NULL);
		if (res == -1)
			goto outrun;

	}

	ast_mutex_lock(&conf->playlock);

	if (ast_test_flag64(confflags, CONFFLAG_MARKEDUSER))
		conf->markedusers++;
	conf->users++;
	if (rt_log_members) {
		/* Update table */
		snprintf(members, sizeof(members), "%d", conf->users);
		ast_realtime_require_field("meetme",
			"confno", strlen(conf->confno) > 7 ? RQ_UINTEGER4 : strlen(conf->confno) > 4 ? RQ_UINTEGER3 : RQ_UINTEGER2, strlen(conf->confno),
			"members", RQ_UINTEGER1, strlen(members),
			NULL);
		ast_update_realtime("meetme", "confno", conf->confno, "members", members, NULL);
	}
	setusercount = 1;

	/* This device changed state now - if this is the first user */
	if (conf->users == 1)
		ast_devstate_changed(AST_DEVICE_INUSE, (conf->isdynamic ? AST_DEVSTATE_NOT_CACHABLE : AST_DEVSTATE_CACHABLE), "meetme:%s", conf->confno);

	ast_mutex_unlock(&conf->playlock);

	/* return the unique ID of the conference */
	pbx_builtin_setvar_helper(chan, "MEETMEUNIQUEID", conf->uniqueid);

	if (ast_test_flag64(confflags, CONFFLAG_EXIT_CONTEXT)) {
		ast_channel_lock(chan);
		if ((tmpvar = pbx_builtin_getvar_helper(chan, "MEETME_EXIT_CONTEXT"))) {
			ast_copy_string(exitcontext, tmpvar, sizeof(exitcontext));
		} else if (!ast_strlen_zero(ast_channel_macrocontext(chan))) {
			ast_copy_string(exitcontext, ast_channel_macrocontext(chan), sizeof(exitcontext));
		} else {
			ast_copy_string(exitcontext, ast_channel_context(chan), sizeof(exitcontext));
		}
		ast_channel_unlock(chan);
	}

	/* Play an arbitrary intro message */
	if (ast_test_flag64(confflags, CONFFLAG_INTROMSG) &&
			!ast_strlen_zero(optargs[OPT_ARG_INTROMSG])) {
		if (!ast_streamfile(chan, optargs[OPT_ARG_INTROMSG], ast_channel_language(chan))) {
			ast_waitstream(chan, "");
		}
	}

	if (!ast_test_flag64(confflags, (CONFFLAG_QUIET | CONFFLAG_NOONLYPERSON))) {
		if (conf->users == 1 && !ast_test_flag64(confflags, CONFFLAG_WAITMARKED))
			if (!ast_streamfile(chan, "conf-onlyperson", ast_channel_language(chan)))
				ast_waitstream(chan, "");
		if (ast_test_flag64(confflags, CONFFLAG_WAITMARKED) && conf->markedusers == 0)
			if (!ast_streamfile(chan, "conf-waitforleader", ast_channel_language(chan)))
				ast_waitstream(chan, "");
	}

	if (ast_test_flag64(confflags, CONFFLAG_ANNOUNCEUSERCOUNT) && conf->users > 1) {
		int keepplaying = 1;

		if (conf->users == 2) { 
			if (!ast_streamfile(chan, "conf-onlyone", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0)
					keepplaying = 0;
				else if (res == -1)
					goto outrun;
			}
		} else { 
			if (!ast_streamfile(chan, "conf-thereare", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0)
					keepplaying = 0;
				else if (res == -1)
					goto outrun;
			}
			if (keepplaying) {
				res = ast_say_number(chan, conf->users - 1, AST_DIGIT_ANY, ast_channel_language(chan), (char *) NULL);
				if (res > 0)
					keepplaying = 0;
				else if (res == -1)
					goto outrun;
			}
			if (keepplaying && !ast_streamfile(chan, "conf-otherinparty", ast_channel_language(chan))) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0)
					keepplaying = 0;
				else if (res == -1) 
					goto outrun;
			}
		}
	}

	if (!ast_test_flag64(confflags, CONFFLAG_NO_AUDIO_UNTIL_UP)) {
		/* We're leaving this alone until the state gets changed to up */
		ast_indicate(chan, -1);
	}

	if (ast_set_write_format(chan, ast_format_slin) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to write linear mode\n", ast_channel_name(chan));
		goto outrun;
	}

	if (ast_set_read_format(chan, ast_format_slin) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to read linear mode\n", ast_channel_name(chan));
		goto outrun;
	}

	/* Reduce background noise from each participant */
	if (!ast_test_flag64(confflags, CONFFLAG_DONT_DENOISE) &&
		(mod_speex = ast_module_helper("", "func_speex", 0, 0, 0, 0))) {
		ast_free(mod_speex);
		ast_func_write(chan, "DENOISE(rx)", "on");
	}

	retrydahdi = (strcasecmp(ast_channel_tech(chan)->type, "DAHDI") || (ast_channel_audiohooks(chan) || ast_channel_monitor(chan)) ? 1 : 0);
	user->dahdichannel = !retrydahdi;

 dahdiretry:
	origfd = ast_channel_fd(chan, 0);
	if (retrydahdi) {
		/* open pseudo in non-blocking mode */
		fd = open("/dev/dahdi/pseudo", O_RDWR | O_NONBLOCK);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to open DAHDI pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		using_pseudo = 1;
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE / 2;
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = audio_buffers;
		if (ioctl(fd, DAHDI_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		x = 1;
		if (ioctl(fd, DAHDI_SETLINEAR, &x)) {
			ast_log(LOG_WARNING, "Unable to set linear mode: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		nfds = 1;
	} else {
		/* XXX Make sure we're not running on a pseudo channel XXX */
		fd = ast_channel_fd(chan, 0);
		nfds = 0;
	}
	memset(&dahdic, 0, sizeof(dahdic));
	memset(&dahdic_empty, 0, sizeof(dahdic_empty));
	/* Check to see if we're in a conference... */
	dahdic.chan = 0;
	if (ioctl(fd, DAHDI_GETCONF, &dahdic)) {
		ast_log(LOG_WARNING, "Error getting conference\n");
		close(fd);
		goto outrun;
	}
	if (dahdic.confmode) {
		/* Whoa, already in a conference...  Retry... */
		if (!retrydahdi) {
			ast_debug(1, "DAHDI channel is in a conference already, retrying with pseudo\n");
			retrydahdi = 1;
			goto dahdiretry;
		}
	}
	memset(&dahdic, 0, sizeof(dahdic));
	/* Add us to the conference */
	dahdic.chan = 0;
	dahdic.confno = conf->dahdiconf;

	if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && (ast_test_flag64(confflags, CONFFLAG_INTROUSER) ||
			ast_test_flag64(confflags, CONFFLAG_INTROUSERNOREVIEW) || ast_test_flag64(confflags, CONFFLAG_INTROUSER_VMREC)) && conf->users > 1) {
		struct announce_listitem *item;
		if (!(item = ao2_alloc(sizeof(*item), NULL)))
			goto outrun;
		ast_copy_string(item->namerecloc, user->namerecloc, sizeof(item->namerecloc));
		ast_copy_string(item->language, ast_channel_language(chan), sizeof(item->language));
		item->confchan = conf->chan;
		item->confusers = conf->users;
		if (ast_test_flag64(confflags, CONFFLAG_INTROUSER_VMREC)){
			item->vmrec = 1;
		}
		item->announcetype = CONF_HASJOIN;
		ast_mutex_lock(&conf->announcelistlock);
		ao2_ref(item, +1); /* add one more so we can determine when announce_thread is done playing it */
		AST_LIST_INSERT_TAIL(&conf->announcelist, item, entry);
		ast_cond_signal(&conf->announcelist_addition);
		ast_mutex_unlock(&conf->announcelistlock);

		while (!ast_check_hangup(conf->chan) && ao2_ref(item, 0) == 2 && !ast_safe_sleep(chan, 1000)) {
			;
		}
		ao2_ref(item, -1);
	}

	if (ast_test_flag64(confflags, CONFFLAG_WAITMARKED) && !conf->markedusers)
		dahdic.confmode = DAHDI_CONF_CONF;
	else if (ast_test_flag64(confflags, CONFFLAG_MONITOR))
		dahdic.confmode = DAHDI_CONF_CONFMON | DAHDI_CONF_LISTENER;
	else if (ast_test_flag64(confflags, CONFFLAG_TALKER))
		dahdic.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
	else
		dahdic.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;

	if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		goto outrun;
	}
	ast_debug(1, "Placed channel %s in DAHDI conf %d\n", ast_channel_name(chan), conf->dahdiconf);

	if (!sent_event) {
		meetme_stasis_generate_msg(conf, chan, user, meetme_join_type(), NULL);
		sent_event = 1;
	}

	if (!firstpass && !ast_test_flag64(confflags, CONFFLAG_MONITOR) &&
		!ast_test_flag64(confflags, CONFFLAG_ADMIN)) {
		firstpass = 1;
		if (!ast_test_flag64(confflags, CONFFLAG_QUIET))
			if (!ast_test_flag64(confflags, CONFFLAG_WAITMARKED) || (ast_test_flag64(confflags, CONFFLAG_MARKEDUSER) &&
				(conf->markedusers >= 1))) {
				conf_play(chan, conf, ENTER);
			}
	}

	conf_flush(fd, chan);

	if (dsp)
		ast_dsp_free(dsp);

	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		res = -1;
	}

	if (ast_test_flag64(confflags, CONFFLAG_AGI)) {
		/* Get name of AGI file to run from $(MEETME_AGI_BACKGROUND)
		   or use default filename of conf-background.agi */

		ast_channel_lock(chan);
		if ((tmpvar = pbx_builtin_getvar_helper(chan, "MEETME_AGI_BACKGROUND"))) {
			agifile = ast_strdupa(tmpvar);
		} else {
			agifile = ast_strdupa(agifiledefault);
		}
		ast_channel_unlock(chan);
		
		if (user->dahdichannel) {
			/*  Set CONFMUTE mode on DAHDI channel to mute DTMF tones */
			x = 1;
			ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
		}
		/* Find a pointer to the agi app and execute the script */
		agi_app = pbx_findapp("agi");
		if (agi_app) {
			ret = pbx_exec(chan, agi_app, agifile);
			ao2_ref(agi_app, -1);
		} else {
			ast_log(LOG_WARNING, "Could not find application (agi)\n");
			ret = -2;
		}
		if (user->dahdichannel) {
			/*  Remove CONFMUTE mode on DAHDI channel */
			x = 0;
			ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
		}
	} else {
		int lastusers = conf->users;
		if (user->dahdichannel && ast_test_flag64(confflags, CONFFLAG_STARMENU)) {
			/*  Set CONFMUTE mode on DAHDI channel to mute DTMF tones when the menu is enabled */
			x = 1;
			ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
		}

		for (;;) {
			int menu_was_active = 0;

			outfd = -1;
			ms = -1;
			now = ast_tvnow();

			if (rt_schedule && conf->endtime) {
				char currenttime[32];
				long localendtime = 0;
				int extended = 0;
				struct ast_tm tm;
				struct ast_variable *var, *origvar;
				struct timeval tmp;

				if (now.tv_sec % 60 == 0) {
					if (!checked) {
						ast_localtime(&now, &tm, NULL);
						ast_strftime(currenttime, sizeof(currenttime), DATE_FORMAT, &tm);
						var = origvar = ast_load_realtime("meetme", "confno",
							conf->confno, "starttime <=", currenttime,
							 "endtime >=", currenttime, NULL);

						for ( ; var; var = var->next) {
							if (!strcasecmp(var->name, "endtime")) {
								struct ast_tm endtime_tm;
								ast_strptime(var->value, "%Y-%m-%d %H:%M:%S", &endtime_tm);
								tmp = ast_mktime(&endtime_tm, NULL);
								localendtime = tmp.tv_sec;
							}
						}
						ast_variables_destroy(origvar);

						/* A conference can be extended from the
						   Admin/User menu or by an external source */
						if (localendtime > conf->endtime){
							conf->endtime = localendtime;
							extended = 1;
						}

						if (conf->endtime && (now.tv_sec >= conf->endtime)) {
							ast_verbose("Quitting time...\n");
							goto outrun;
						}

						if (!announcement_played && conf->endalert) {
							if (now.tv_sec + conf->endalert >= conf->endtime) {
								if (!ast_streamfile(chan, "conf-will-end-in", ast_channel_language(chan)))
									ast_waitstream(chan, "");
								ast_say_digits(chan, (conf->endtime - now.tv_sec) / 60, "", ast_channel_language(chan));
								if (!ast_streamfile(chan, "minutes", ast_channel_language(chan)))
									ast_waitstream(chan, "");
								if (musiconhold) {
									conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
								}
								announcement_played = 1;
							}
						}

						if (extended) {
							announcement_played = 0;
						}

						checked = 1;
					}
				} else {
					checked = 0;
				}
			}

 			if (user->kicktime && (user->kicktime <= now.tv_sec)) {
				if (ast_test_flag64(confflags, CONFFLAG_KICK_CONTINUE)) {
					ret = 0;
				} else {
					ret = -1;
				}
				break;
			}
  
 			to = -1;
 			if (user->timelimit) {
				int minutes = 0, seconds = 0, remain = 0;
 
 				to = ast_tvdiff_ms(nexteventts, now);
 				if (to < 0) {
 					to = 0;
				}
 				time_left_ms = user->timelimit - ast_tvdiff_ms(now, user->start_time);
 				if (time_left_ms < to) {
 					to = time_left_ms;
				}
 	
 				if (time_left_ms <= 0) {
 					if (user->end_sound) {						
 						res = ast_streamfile(chan, user->end_sound, ast_channel_language(chan));
 						res = ast_waitstream(chan, "");
 					}
					if (ast_test_flag64(confflags, CONFFLAG_KICK_CONTINUE)) {
						ret = 0;
					} else {
						ret = -1;
					}
 					break;
 				}
 				
 				if (!to) {
 					if (time_left_ms >= 5000) {						
 						
 						remain = (time_left_ms + 500) / 1000;
 						if (remain / 60 >= 1) {
 							minutes = remain / 60;
 							seconds = remain % 60;
 						} else {
 							seconds = remain;
 						}
 						
 						/* force the time left to round up if appropriate */
 						if (user->warning_sound && user->play_warning) {
 							if (!strcmp(user->warning_sound, "timeleft")) {
 								
 								res = ast_streamfile(chan, "vm-youhave", ast_channel_language(chan));
 								res = ast_waitstream(chan, "");
 								if (minutes) {
 									res = ast_say_number(chan, minutes, AST_DIGIT_ANY, ast_channel_language(chan), (char *) NULL);
 									res = ast_streamfile(chan, "queue-minutes", ast_channel_language(chan));
 									res = ast_waitstream(chan, "");
 								}
 								if (seconds) {
 									res = ast_say_number(chan, seconds, AST_DIGIT_ANY, ast_channel_language(chan), (char *) NULL);
 									res = ast_streamfile(chan, "queue-seconds", ast_channel_language(chan));
 									res = ast_waitstream(chan, "");
 								}
 							} else {
 								res = ast_streamfile(chan, user->warning_sound, ast_channel_language(chan));
 								res = ast_waitstream(chan, "");
 							}
							if (musiconhold) {
								conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
							}
 						}
 					}
 					if (user->warning_freq) {
 						nexteventts = ast_tvadd(nexteventts, ast_samp2tv(user->warning_freq, 1000));
 					} else {
 						nexteventts = ast_tvadd(user->start_time, ast_samp2tv(user->timelimit, 1000));
					}
 				}
 			}

			now = ast_tvnow();
			if (timeout && now.tv_sec >= timeout) {
				if (ast_test_flag64(confflags, CONFFLAG_KICK_CONTINUE)) {
					ret = 0;
				} else {
					ret = -1;
				}
				break;
			}

			/* if we have just exited from the menu, and the user had a channel-driver
			   volume adjustment, restore it
			*/
			if (!menu_mode && menu_was_active && user->listen.desired && !user->listen.actual) {
				set_talk_volume(user, user->listen.desired);
			}

			menu_was_active = menu_mode;

			currentmarked = conf->markedusers;
			if (!ast_test_flag64(confflags, CONFFLAG_QUIET) &&
			    ast_test_flag64(confflags, CONFFLAG_MARKEDUSER) &&
			    ast_test_flag64(confflags, CONFFLAG_WAITMARKED) &&
			    lastmarked == 0) {
				if (currentmarked == 1 && conf->users > 1) {
					ast_say_number(chan, conf->users - 1, AST_DIGIT_ANY, ast_channel_language(chan), (char *) NULL);
					if (conf->users - 1 == 1) {
						if (!ast_streamfile(chan, "conf-userwilljoin", ast_channel_language(chan))) {
							ast_waitstream(chan, "");
						}
					} else {
						if (!ast_streamfile(chan, "conf-userswilljoin", ast_channel_language(chan))) {
							ast_waitstream(chan, "");
						}
					}
				}
				if (conf->users == 1 && !ast_test_flag64(confflags, CONFFLAG_MARKEDUSER)) {
					if (!ast_streamfile(chan, "conf-onlyperson", ast_channel_language(chan))) {
						ast_waitstream(chan, "");
					}
				}
			}

			/* Update the struct with the actual confflags */
			user->userflags = *confflags;

			if (ast_test_flag64(confflags, CONFFLAG_WAITMARKED)) {
				if (currentmarked == 0) {
					if (lastmarked != 0) {
						if (!ast_test_flag64(confflags, CONFFLAG_QUIET)) {
							if (!ast_streamfile(chan, "conf-leaderhasleft", ast_channel_language(chan))) {
								ast_waitstream(chan, "");
							}
						}
						if (ast_test_flag64(confflags, CONFFLAG_MARKEDEXIT)) {
							if (ast_test_flag64(confflags, CONFFLAG_KICK_CONTINUE)) {
								ret = 0;
							}
							break;
						} else {
							dahdic.confmode = DAHDI_CONF_CONF;
							if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
								ast_log(LOG_WARNING, "Error setting conference\n");
								close(fd);
								goto outrun;
							}
						}
					}
					if (!musiconhold && (ast_test_flag64(confflags, CONFFLAG_MOH))) {
						conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
						musiconhold = 1;
					}
				} else if (currentmarked >= 1 && lastmarked == 0) {
					/* Marked user entered, so cancel timeout */
					timeout = 0;
					if (ast_test_flag64(confflags, CONFFLAG_MONITOR)) {
						dahdic.confmode = DAHDI_CONF_CONFMON | DAHDI_CONF_LISTENER;
					} else if (ast_test_flag64(confflags, CONFFLAG_TALKER)) {
						dahdic.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
					} else {
						dahdic.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
					}
					if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						goto outrun;
					}
					if (musiconhold && (ast_test_flag64(confflags, CONFFLAG_MOH))) {
						ast_moh_stop(chan);
						musiconhold = 0;
					}
					if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && 
						!ast_test_flag64(confflags, CONFFLAG_MARKEDUSER)) {
						if (!ast_streamfile(chan, "conf-placeintoconf", ast_channel_language(chan))) {
							ast_waitstream(chan, "");
						}
						conf_play(chan, conf, ENTER);
					}
				}
			}

			/* trying to add moh for single person conf */
			if (ast_test_flag64(confflags, CONFFLAG_MOH) && !ast_test_flag64(confflags, CONFFLAG_WAITMARKED)) {
				if (conf->users == 1) {
					if (!musiconhold) {
						conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
						musiconhold = 1;
					} 
				} else {
					if (musiconhold) {
						ast_moh_stop(chan);
						musiconhold = 0;
					}
				}
			}
			
			/* Leave if the last marked user left */
			if (currentmarked == 0 && lastmarked != 0 && ast_test_flag64(confflags, CONFFLAG_MARKEDEXIT)) {
				if (ast_test_flag64(confflags, CONFFLAG_KICK_CONTINUE)) {
					ret = 0;
				} else {
					ret = -1;
				}
				break;
			}

			/* Throw a TestEvent if a user exit did not cause this user to leave the conference */
			if (conf->users != lastusers) {
				if (conf->users < lastusers) {
					ast_test_suite_event_notify("NOEXIT", "Message: CONFFLAG_MARKEDEXIT\r\nLastUsers: %d\r\nUsers: %d", lastusers, conf->users);
				}
				lastusers = conf->users;
			}

			/* Check if my modes have changed */

			/* If I should be muted but am still talker, mute me */
			if ((user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && (dahdic.confmode & DAHDI_CONF_TALKER)) {
				RAII_VAR(struct ast_json *, status_blob, status_to_json(1), ast_json_unref);
				dahdic.confmode ^= DAHDI_CONF_TALKER;
				if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
					ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
					ret = -1;
					break;
				}

				/* Indicate user is not talking anymore - change him to unmonitored state */
				if (ast_test_flag64(confflags,  (CONFFLAG_MONITORTALKER | CONFFLAG_OPTIMIZETALKER))) {
					set_user_talking(chan, conf, user, -1, ast_test_flag64(confflags, CONFFLAG_MONITORTALKER));
				}
				meetme_stasis_generate_msg(conf, chan, user, meetme_mute_type(), status_blob);
			}

			/* If I should be un-muted but am not talker, un-mute me */
			if (!(user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && !ast_test_flag64(confflags, CONFFLAG_MONITOR) && !(dahdic.confmode & DAHDI_CONF_TALKER)) {
				RAII_VAR(struct ast_json *, status_blob, status_to_json(0), ast_json_unref);
				dahdic.confmode |= DAHDI_CONF_TALKER;
				if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
					ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
					ret = -1;
					break;
				}
				meetme_stasis_generate_msg(conf, chan, user, meetme_mute_type(), status_blob);
			}

			if ((user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && 
				(user->adminflags & ADMINFLAG_T_REQUEST) && !(talkreq_manager)) {

				RAII_VAR(struct ast_json *, status_blob, status_to_json(1), ast_json_unref);
				talkreq_manager = 1;
				meetme_stasis_generate_msg(conf, chan, user, meetme_talk_request_type(), status_blob);
			}

			if (!(user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && 
				!(user->adminflags & ADMINFLAG_T_REQUEST) && (talkreq_manager)) {
				RAII_VAR(struct ast_json *, status_blob, status_to_json(0), ast_json_unref);
				talkreq_manager = 0;
				meetme_stasis_generate_msg(conf, chan, user, meetme_talk_request_type(), status_blob);
			}

			/* If user have been hung up, exit the conference */
			if (user->adminflags & ADMINFLAG_HANGUP) {
				ret = 0;
				break;
			}

			/* If I have been kicked, exit the conference */
			if (user->adminflags & ADMINFLAG_KICKME) {
				/* You have been kicked. */
				if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && 
					!ast_streamfile(chan, "conf-kicked", ast_channel_language(chan))) {
					ast_waitstream(chan, "");
				}
				ret = 0;
				break;
			}

			/* Perform a hangup check here since ast_waitfor_nandfds will not always be able to get a channel after a hangup has occurred */
			if (ast_check_hangup(chan)) {
				break;
			}

			c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);

			if (c) {
				char dtmfstr[2] = "";

				if (ast_channel_fd(c, 0) != origfd || (user->dahdichannel && (ast_channel_audiohooks(c) || ast_channel_monitor(c)))) {
					if (using_pseudo) {
						/* Kill old pseudo */
						close(fd);
						using_pseudo = 0;
					}
					ast_debug(1, "Ooh, something swapped out under us, starting over\n");
					retrydahdi = (strcasecmp(ast_channel_tech(c)->type, "DAHDI") || (ast_channel_audiohooks(c) || ast_channel_monitor(c)) ? 1 : 0);
					user->dahdichannel = !retrydahdi;
					goto dahdiretry;
				}
				if (ast_test_flag64(confflags, CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED))) {
					f = ast_read_noaudio(c);
				} else {
					f = ast_read(c);
				}
				if (!f) {
					break;
				}
				if (f->frametype == AST_FRAME_DTMF) {
					dtmfstr[0] = f->subclass.integer;
					dtmfstr[1] = '\0';
				}

				if ((f->frametype == AST_FRAME_VOICE) && (ast_format_cmp(f->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)) {
					if (user->talk.actual) {
						ast_frame_adjust_volume(f, user->talk.actual);
					}

					if (ast_test_flag64(confflags, (CONFFLAG_OPTIMIZETALKER | CONFFLAG_MONITORTALKER))) {
						if (user->talking == -1) {
							user->talking = 0;
						}

						res = ast_dsp_silence(dsp, f, &totalsilence);
						if (!user->talking && totalsilence < MEETME_DELAYDETECTTALK) {
							set_user_talking(chan, conf, user, 1, ast_test_flag64(confflags, CONFFLAG_MONITORTALKER));
						}

						if (user->talking && totalsilence > MEETME_DELAYDETECTENDTALK) {
							set_user_talking(chan, conf, user, 0, ast_test_flag64(confflags, CONFFLAG_MONITORTALKER));
						}
					}
					if (using_pseudo) {
						/* Absolutely do _not_ use careful_write here...
						   it is important that we read data from the channel
						   as fast as it arrives, and feed it into the conference.
						   The buffering in the pseudo channel will take care of any
						   timing differences, unless they are so drastic as to lose
						   audio frames (in which case carefully writing would only
						   have delayed the audio even further).
						*/
						/* As it turns out, we do want to use careful write.  We just
						   don't want to block, but we do want to at least *try*
						   to write out all the samples.
						 */
						if (user->talking || !ast_test_flag64(confflags, CONFFLAG_OPTIMIZETALKER)) {
							careful_write(fd, f->data.ptr, f->datalen, 0);
						}
					}
				} else if (((f->frametype == AST_FRAME_DTMF) && (f->subclass.integer == '*') && ast_test_flag64(confflags, CONFFLAG_STARMENU)) || ((f->frametype == AST_FRAME_DTMF) && menu_mode)) {
					if (ast_test_flag64(confflags, CONFFLAG_PASS_DTMF)) {
						conf_queue_dtmf(conf, user, f);
					}
					/* Take out of conference */
					if (ioctl(fd, DAHDI_SETCONF, &dahdic_empty)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						ast_frfree(f);
						goto outrun;
					}

					/* if we are entering the menu, and the user has a channel-driver
					   volume adjustment, clear it
					*/
					if (!menu_mode && user->talk.desired && !user->talk.actual) {
						set_talk_volume(user, 0);
					}

					if (musiconhold) {
						ast_moh_stop(chan);
					} else if (!menu_mode) {
						char *menu_to_play;
						if (ast_test_flag64(confflags, CONFFLAG_ADMIN)) {
							menu_mode = MENU_ADMIN;
							menu_to_play = "conf-adminmenu-18";
						} else {
							menu_mode = MENU_NORMAL;
							menu_to_play = "conf-usermenu-162";
						}

						if (!ast_streamfile(chan, menu_to_play, ast_channel_language(chan))) {
							dtmf = ast_waitstream(chan, AST_DIGIT_ANY);
							ast_stopstream(chan);
						} else {
							dtmf = 0;
						}
					} else {
						dtmf = f->subclass.integer;
					}

					if (dtmf > 0) {
						meetme_menu(&menu_mode, &dtmf, conf, confflags,
							chan, user, recordingtmp, sizeof(recordingtmp), cap_slin);
					}

					if (musiconhold && !menu_mode) {
						conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
					}

					/* Put back into conference */
					if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						ast_frfree(f);
						goto outrun;
					}

					conf_flush(fd, chan);
				/*
				 * Since options using DTMF could absorb DTMF meant for the
				 * conference menu, we have to check them after the menu.
				 */
				} else if ((f->frametype == AST_FRAME_DTMF) && ast_test_flag64(confflags, CONFFLAG_EXIT_CONTEXT) && ast_exists_extension(chan, exitcontext, dtmfstr, 1, "")) {
					if (ast_test_flag64(confflags, CONFFLAG_PASS_DTMF)) {
						conf_queue_dtmf(conf, user, f);
					}

					if (!ast_goto_if_exists(chan, exitcontext, dtmfstr, 1)) {
						ast_debug(1, "Got DTMF %c, goto context %s\n", dtmfstr[0], exitcontext);
						ret = 0;
						ast_frfree(f);
						break;
					} else {
						ast_debug(2, "Exit by single digit did not work in meetme. Extension %s does not exist in context %s\n", dtmfstr, exitcontext);
					}
				} else if ((f->frametype == AST_FRAME_DTMF) && ast_test_flag64(confflags, CONFFLAG_KEYEXIT) &&
					(strchr(exitkeys, f->subclass.integer))) {
					pbx_builtin_setvar_helper(chan, "MEETME_EXIT_KEY", dtmfstr);

					if (ast_test_flag64(confflags, CONFFLAG_PASS_DTMF)) {
						conf_queue_dtmf(conf, user, f);
					}
					ret = 0;
					ast_frfree(f);
					break;
				} else if ((f->frametype == AST_FRAME_DTMF_BEGIN || f->frametype == AST_FRAME_DTMF_END)
					&& ast_test_flag64(confflags, CONFFLAG_PASS_DTMF)) {
					conf_queue_dtmf(conf, user, f);
				} else if (ast_test_flag64(confflags, CONFFLAG_SLA_STATION) && f->frametype == AST_FRAME_CONTROL) {
					switch (f->subclass.integer) {
					case AST_CONTROL_HOLD:
						sla_queue_event_conf(SLA_EVENT_HOLD, chan, conf);
						break;
					default:
						break;
					}
				} else if (f->frametype == AST_FRAME_NULL) {
					/* Ignore NULL frames. It is perfectly normal to get these if the person is muted. */
				} else if (f->frametype == AST_FRAME_CONTROL) {
					switch (f->subclass.integer) {
					case AST_CONTROL_BUSY:
					case AST_CONTROL_CONGESTION:
						ast_frfree(f);
						goto outrun;
						break;
					default:
						ast_debug(1,
							"Got ignored control frame on channel %s, f->frametype=%u,f->subclass=%d\n",
							ast_channel_name(chan), f->frametype, f->subclass.integer);
					}
				} else {
					ast_debug(1,
						"Got unrecognized frame on channel %s, f->frametype=%u,f->subclass=%d\n",
						ast_channel_name(chan), f->frametype, f->subclass.integer);
				}
				ast_frfree(f);
			} else if (outfd > -1) {
				res = read(outfd, buf, CONF_SIZE);
				if (res > 0) {
					memset(&fr, 0, sizeof(fr));
					fr.frametype = AST_FRAME_VOICE;
					fr.subclass.format = ast_format_slin;
					fr.datalen = res;
					fr.samples = res / 2;
					fr.data.ptr = buf;
					fr.offset = AST_FRIENDLY_OFFSET;
					if (!user->listen.actual &&
						(ast_test_flag64(confflags, CONFFLAG_MONITOR) ||
						 (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) ||
						 (!user->talking && ast_test_flag64(confflags, CONFFLAG_OPTIMIZETALKER))
						 )) {
						int idx;
						for (idx = 0; idx < AST_FRAME_BITS; idx++) {
							if (ast_format_compatibility_format2bitfield(ast_channel_rawwriteformat(chan)) & (1 << idx)) {
								break;
							}
						}
						if (idx >= AST_FRAME_BITS) {
							goto bailoutandtrynormal;
						}
						ast_mutex_lock(&conf->listenlock);
						if (!conf->transframe[idx]) {
							if (conf->origframe) {
								if (musiconhold
									&& !ast_test_flag64(confflags, CONFFLAG_WAITMARKED)
									&& !ast_dsp_silence(dsp, conf->origframe, &confsilence)
									&& confsilence < MEETME_DELAYDETECTTALK) {
									ast_moh_stop(chan);
									mohtempstopped = 1;
								}
								if (!conf->transpath[idx]) {
									conf->transpath[idx] = ast_translator_build_path(ast_channel_rawwriteformat(chan), ast_format_slin);
								}
								if (conf->transpath[idx]) {
									conf->transframe[idx] = ast_translate(conf->transpath[idx], conf->origframe, 0);
									if (!conf->transframe[idx]) {
										conf->transframe[idx] = &ast_null_frame;
									}
								}
							}
						}
						if (conf->transframe[idx]) {
 							if ((conf->transframe[idx]->frametype != AST_FRAME_NULL) &&
							    can_write(chan, confflags)) {
								struct ast_frame *cur;
								/* the translator may have returned a list of frames, so
								   write each one onto the channel
								*/
								for (cur = conf->transframe[idx]; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
									if (ast_write(chan, cur)) {
										ast_log(LOG_WARNING, "Unable to write frame to channel %s\n", ast_channel_name(chan));
										break;
									}
								}
								if (musiconhold && mohtempstopped && confsilence > MEETME_DELAYDETECTENDTALK) {
									mohtempstopped = 0;
									conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
								}
							}
						} else {
							ast_mutex_unlock(&conf->listenlock);
							goto bailoutandtrynormal;
						}
						ast_mutex_unlock(&conf->listenlock);
					} else {
bailoutandtrynormal:
						if (musiconhold
							&& !ast_test_flag64(confflags, CONFFLAG_WAITMARKED)
							&& !ast_dsp_silence(dsp, &fr, &confsilence)
							&& confsilence < MEETME_DELAYDETECTTALK) {
							ast_moh_stop(chan);
							mohtempstopped = 1;
						}
						if (user->listen.actual) {
							ast_frame_adjust_volume(&fr, user->listen.actual);
						}
						if (can_write(chan, confflags) && ast_write(chan, &fr) < 0) {
							ast_log(LOG_WARNING, "Unable to write frame to channel %s\n", ast_channel_name(chan));
						}
						if (musiconhold && mohtempstopped && confsilence > MEETME_DELAYDETECTENDTALK) {
							mohtempstopped = 0;
							conf_start_moh(chan, optargs[OPT_ARG_MOH_CLASS]);
						}
					}
				} else {
					ast_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
				}
			}
			lastmarked = currentmarked;
		}
	}

	if (musiconhold) {
		ast_moh_stop(chan);
	}
	
	if (using_pseudo) {
		close(fd);
	} else {
		/* Take out of conference */
		dahdic.chan = 0;	
		dahdic.confno = 0;
		dahdic.confmode = 0;
		if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
		}
	}

	reset_volumes(user);

	if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && !ast_test_flag64(confflags, CONFFLAG_MONITOR) &&
		!ast_test_flag64(confflags, CONFFLAG_ADMIN)) {
		conf_play(chan, conf, LEAVE);
	}

	if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && ast_test_flag64(confflags, CONFFLAG_INTROUSER |CONFFLAG_INTROUSERNOREVIEW | CONFFLAG_INTROUSER_VMREC) && conf->users > 1) {
		struct announce_listitem *item;
		if (!(item = ao2_alloc(sizeof(*item), NULL)))
			goto outrun;
		ast_copy_string(item->namerecloc, user->namerecloc, sizeof(item->namerecloc));
		ast_copy_string(item->language, ast_channel_language(chan), sizeof(item->language));
		item->confchan = conf->chan;
		item->confusers = conf->users;
		item->announcetype = CONF_HASLEFT;
		if (ast_test_flag64(confflags, CONFFLAG_INTROUSER_VMREC)){
			item->vmrec = 1;
		}
		ast_mutex_lock(&conf->announcelistlock);
		AST_LIST_INSERT_TAIL(&conf->announcelist, item, entry);
		ast_cond_signal(&conf->announcelist_addition);
		ast_mutex_unlock(&conf->announcelistlock);
	} else if (!ast_test_flag64(confflags, CONFFLAG_QUIET) && ast_test_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW) && !ast_test_flag64(confflags, CONFFLAG_INTROUSER_VMREC) && conf->users == 1) {
		/* Last person is leaving, so no reason to try and announce, but should delete the name recording */
		ast_filedelete(user->namerecloc, NULL);
	}

 outrun:
	AST_LIST_LOCK(&confs);

	if (dsp) {
		ast_dsp_free(dsp);
	}
	
	if (user->user_no) {
		/* Only cleanup users who really joined! */
		now = ast_tvnow();

		if (sent_event) {
			meetme_stasis_generate_msg(conf, chan, user, meetme_leave_type(), NULL);
		}

		if (setusercount) {
			conf->users--;
			if (rt_log_members) {
				/* Update table */
				snprintf(members, sizeof(members), "%d", conf->users);
				ast_realtime_require_field("meetme",
					"confno", strlen(conf->confno) > 7 ? RQ_UINTEGER4 : strlen(conf->confno) > 4 ? RQ_UINTEGER3 : RQ_UINTEGER2, strlen(conf->confno),
					"members", RQ_UINTEGER1, strlen(members),
					NULL);
				ast_update_realtime("meetme", "confno", conf->confno, "members", members, NULL);
			}
			if (ast_test_flag64(confflags, CONFFLAG_MARKEDUSER)) {
				conf->markedusers--;
			}
		}
		/* Remove ourselves from the container */
		ao2_unlink(conf->usercontainer, user); 

		/* Change any states */
		if (!conf->users) {
			ast_devstate_changed(AST_DEVICE_NOT_INUSE, (conf->isdynamic ? AST_DEVSTATE_NOT_CACHABLE : AST_DEVSTATE_CACHABLE), "meetme:%s", conf->confno);
		}

 		/* This flag is meant to kill a conference with only one participant remaining.  */
		if (conf->users == 1 && ast_test_flag64(confflags, CONFFLAG_KILL_LAST_MAN_STANDING)) {
 			ao2_callback(conf->usercontainer, 0, user_set_hangup_cb, NULL);
 		}

		/* Return the number of seconds the user was in the conf */
		snprintf(meetmesecs, sizeof(meetmesecs), "%d", (int) (time(NULL) - user->jointime));
		pbx_builtin_setvar_helper(chan, "MEETMESECS", meetmesecs);

		/* Return the RealTime bookid for CDR linking */
		if (rt_schedule) {
			pbx_builtin_setvar_helper(chan, "MEETMEBOOKID", conf->bookid);
		}
	}
	ao2_ref(user, -1);
	AST_LIST_UNLOCK(&confs);


conf_run_cleanup:
	ao2_cleanup(cap_slin);

	return ret;
}

static struct ast_conference *find_conf_realtime(struct ast_channel *chan, char *confno, int make, int dynamic,
				char *dynamic_pin, size_t pin_buf_len, int refcount, struct ast_flags64 *confflags, int *too_early, char **optargs)
{
	struct ast_variable *var, *origvar;
	struct ast_conference *cnf;

	*too_early = 0;

	/* Check first in the conference list */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(confno, cnf->confno)) {
			break;
		}
	}
	if (cnf) {
		cnf->refcount += refcount;
	}
	AST_LIST_UNLOCK(&confs);

	if (!cnf) {
		char *pin = NULL, *pinadmin = NULL; /* For temp use */
		int maxusers = 0;
		struct timeval now;
		char recordingfilename[256] = "";
		char recordingformat[11] = "";
		char currenttime[32] = "";
		char eatime[32] = "";
		char bookid[51] = "";
		char recordingtmp[AST_MAX_EXTENSION] = "";
		char useropts[OPTIONS_LEN + 1] = ""; /* Used for RealTime conferences */
		char adminopts[OPTIONS_LEN + 1] = "";
		struct ast_tm tm, etm;
		struct timeval endtime = { .tv_sec = 0 };
		const char *var2;

		if (rt_schedule) {
			now = ast_tvnow();

			ast_localtime(&now, &tm, NULL);
			ast_strftime(currenttime, sizeof(currenttime), DATE_FORMAT, &tm);

			ast_debug(1, "Looking for conference %s that starts after %s\n", confno, currenttime);

			var = ast_load_realtime("meetme", "confno",
				confno, "starttime <= ", currenttime, "endtime >= ",
				currenttime, NULL);

			if (!var && fuzzystart) {
				now = ast_tvnow();
				now.tv_sec += fuzzystart;

				ast_localtime(&now, &tm, NULL);
				ast_strftime(currenttime, sizeof(currenttime), DATE_FORMAT, &tm);
				var = ast_load_realtime("meetme", "confno",
					confno, "starttime <= ", currenttime, "endtime >= ",
					currenttime, NULL);
			}

			if (!var && earlyalert) {
				now = ast_tvnow();
				now.tv_sec += earlyalert;
				ast_localtime(&now, &etm, NULL);
				ast_strftime(eatime, sizeof(eatime), DATE_FORMAT, &etm);
				var = ast_load_realtime("meetme", "confno",
					confno, "starttime <= ", eatime, "endtime >= ",
					currenttime, NULL);
				if (var) {
					*too_early = 1;
				}
			}

		} else {
			 var = ast_load_realtime("meetme", "confno", confno, NULL);
		}

		if (!var) {
			return NULL;
		}

		if (rt_schedule && *too_early) {
			/* Announce that the caller is early and exit */
			if (!ast_streamfile(chan, "conf-has-not-started", ast_channel_language(chan))) {
				ast_waitstream(chan, "");
			}
			ast_variables_destroy(var);
			return NULL;
		}

		for (origvar = var; var; var = var->next) {
			if (!strcasecmp(var->name, "pin")) {
				pin = ast_strdupa(var->value);
			} else if (!strcasecmp(var->name, "adminpin")) {
				pinadmin = ast_strdupa(var->value);
			} else if (!strcasecmp(var->name, "bookId")) {
				ast_copy_string(bookid, var->value, sizeof(bookid));
			} else if (!strcasecmp(var->name, "opts")) {
				ast_copy_string(useropts, var->value, sizeof(char[OPTIONS_LEN + 1]));
			} else if (!strcasecmp(var->name, "maxusers")) {
				maxusers = atoi(var->value);
			} else if (!strcasecmp(var->name, "adminopts")) {
				ast_copy_string(adminopts, var->value, sizeof(char[OPTIONS_LEN + 1]));
			} else if (!strcasecmp(var->name, "recordingfilename")) {
				ast_copy_string(recordingfilename, var->value, sizeof(recordingfilename));
			} else if (!strcasecmp(var->name, "recordingformat")) {
				ast_copy_string(recordingformat, var->value, sizeof(recordingformat));
			} else if (!strcasecmp(var->name, "endtime")) {
				struct ast_tm endtime_tm;
				ast_strptime(var->value, "%Y-%m-%d %H:%M:%S", &endtime_tm);
				endtime = ast_mktime(&endtime_tm, NULL);
			}
		}

		ast_variables_destroy(origvar);

		cnf = build_conf(confno, pin ? pin : "", pinadmin ? pinadmin : "", make, dynamic, refcount, chan, NULL);

		if (cnf) {
			struct ast_flags64 tmp_flags;

			cnf->maxusers = maxusers;
			cnf->endalert = endalert;
			cnf->endtime = endtime.tv_sec;
			cnf->useropts = ast_strdup(useropts);
			cnf->adminopts = ast_strdup(adminopts);
			cnf->bookid = ast_strdup(bookid);
			if (!ast_strlen_zero(recordingfilename)) {
				cnf->recordingfilename = ast_strdup(recordingfilename);
			}
			if (!ast_strlen_zero(recordingformat)) {
				cnf->recordingformat = ast_strdup(recordingformat);
			}

			/* Parse the other options into confflags -- need to do this in two
			 * steps, because the parse_options routine zeroes the buffer. */
			ast_app_parse_options64(meetme_opts, &tmp_flags, optargs, useropts);
			ast_copy_flags64(confflags, &tmp_flags, tmp_flags.flags);

			if (strchr(cnf->useropts, 'r')) {
				if (ast_strlen_zero(recordingfilename)) { /* If the recordingfilename in the database is empty, use the channel definition or use the default. */
					ast_channel_lock(chan);
					if ((var2 = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFILE"))) {
						ast_free(cnf->recordingfilename);
						cnf->recordingfilename = ast_strdup(var2);
					}
					ast_channel_unlock(chan);
					if (ast_strlen_zero(cnf->recordingfilename)) {
						snprintf(recordingtmp, sizeof(recordingtmp), "meetme-conf-rec-%s-%s", cnf->confno, ast_channel_uniqueid(chan));
						ast_free(cnf->recordingfilename);
						cnf->recordingfilename = ast_strdup(recordingtmp);
					}
				}
				if (ast_strlen_zero(cnf->recordingformat)) {/* If the recording format is empty, use the wav as default */
					ast_channel_lock(chan);
					if ((var2 = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFORMAT"))) {
						ast_free(cnf->recordingformat);
						cnf->recordingformat = ast_strdup(var2);
					}
					ast_channel_unlock(chan);
					if (ast_strlen_zero(cnf->recordingformat)) {
						ast_free(cnf->recordingformat);
						cnf->recordingformat = ast_strdup("wav");
					}
				}
				ast_verb(4, "Starting recording of MeetMe Conference %s into file %s.%s.\n", cnf->confno, cnf->recordingfilename, cnf->recordingformat);
			}
		}
	}

	if (cnf) {
		if (confflags->flags && !cnf->chan &&
		    !ast_test_flag64(confflags, CONFFLAG_QUIET) &&
		    ast_test_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW) | CONFFLAG_INTROUSER_VMREC) {
			ast_log(LOG_WARNING, "No DAHDI channel available for conference, user introduction disabled (is chan_dahdi loaded?)\n");
			ast_clear_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW | CONFFLAG_INTROUSER_VMREC);
		}

		if (confflags && !cnf->chan &&
		    ast_test_flag64(confflags, CONFFLAG_RECORDCONF)) {
			ast_log(LOG_WARNING, "No DAHDI channel available for conference, conference recording disabled (is chan_dahdi loaded?)\n");
			ast_clear_flag64(confflags, CONFFLAG_RECORDCONF);
		}
	}

	return cnf;
}


static struct ast_conference *find_conf(struct ast_channel *chan, char *confno, int make, int dynamic,
					char *dynamic_pin, size_t pin_buf_len, int refcount, struct ast_flags64 *confflags)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_flags config_flags = { 0 };
	struct ast_conference *cnf;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(pin);
		AST_APP_ARG(pinadmin);
	);

	/* Check first in the conference list */
	ast_debug(1, "The requested confno is '%s'?\n", confno);
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		ast_debug(3, "Does conf %s match %s?\n", confno, cnf->confno);
		if (!strcmp(confno, cnf->confno))
			break;
	}
	if (cnf) {
		cnf->refcount += refcount;
	}
	AST_LIST_UNLOCK(&confs);

	if (!cnf) {
		if (dynamic) {
			/* No need to parse meetme.conf */
			ast_debug(1, "Building dynamic conference '%s'\n", confno);
			if (dynamic_pin) {
				if (dynamic_pin[0] == 'q') {
					/* Query the user to enter a PIN */
					if (ast_app_getdata(chan, "conf-getpin", dynamic_pin, pin_buf_len - 1, 0) < 0)
						return NULL;
				}
				cnf = build_conf(confno, dynamic_pin, "", make, dynamic, refcount, chan, NULL);
			} else {
				cnf = build_conf(confno, "", "", make, dynamic, refcount, chan, NULL);
			}
		} else {
			/* Check the config */
			cfg = ast_config_load(CONFIG_FILE_NAME, config_flags);
			if (!cfg) {
				ast_log(LOG_WARNING, "No %s file :(\n", CONFIG_FILE_NAME);
				return NULL;
			} else if (cfg == CONFIG_STATUS_FILEINVALID) {
				ast_log(LOG_ERROR, "Config file " CONFIG_FILE_NAME " is in an invalid format.  Aborting.\n");
				return NULL;
			}

			for (var = ast_variable_browse(cfg, "rooms"); var; var = var->next) {
				char parse[MAX_SETTINGS];

				if (strcasecmp(var->name, "conf"))
					continue;

				ast_copy_string(parse, var->value, sizeof(parse));

				AST_STANDARD_APP_ARGS(args, parse);
				ast_debug(3, "Will conf %s match %s?\n", confno, args.confno);
				if (!strcasecmp(args.confno, confno)) {
					/* Bingo it's a valid conference */
					cnf = build_conf(args.confno,
							S_OR(args.pin, ""),
							S_OR(args.pinadmin, ""),
							make, dynamic, refcount, chan, NULL);
					break;
				}
			}
			if (!var) {
				ast_debug(1, "%s isn't a valid conference\n", confno);
			}
			ast_config_destroy(cfg);
		}
	} else if (dynamic_pin) {
		/* Correct for the user selecting 'D' instead of 'd' to have
		   someone join into a conference that has already been created
		   with a pin. */
		if (dynamic_pin[0] == 'q') {
			dynamic_pin[0] = '\0';
		}
	}

	if (cnf) {
		if (confflags && !cnf->chan &&
		    !ast_test_flag64(confflags, CONFFLAG_QUIET) &&
		    ast_test_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW  | CONFFLAG_INTROUSER_VMREC)) {
			ast_log(LOG_WARNING, "No DAHDI channel available for conference, user introduction disabled (is chan_dahdi loaded?)\n");
			ast_clear_flag64(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW | CONFFLAG_INTROUSER_VMREC);
		}
		
		if (confflags && !cnf->chan &&
		    ast_test_flag64(confflags, CONFFLAG_RECORDCONF)) {
			ast_log(LOG_WARNING, "No DAHDI channel available for conference, conference recording disabled (is chan_dahdi loaded?)\n");
			ast_clear_flag64(confflags, CONFFLAG_RECORDCONF);
		}
	}

	return cnf;
}

/*! \brief The MeetmeCount application */
static int count_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_conference *conf;
	int count;
	char *localdata;
	char val[80] = "0"; 
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(varname);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MeetMeCount requires an argument (conference number)\n");
		return -1;
	}
	
	localdata = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, localdata);
	
	conf = find_conf(chan, args.confno, 0, 0, NULL, 0, 1, NULL);

	if (conf) {
		count = conf->users;
		dispose_conf(conf);
		conf = NULL;
	} else
		count = 0;

	if (!ast_strlen_zero(args.varname)) {
		/* have var so load it and exit */
		snprintf(val, sizeof(val), "%d", count);
		pbx_builtin_setvar_helper(chan, args.varname, val);
	} else {
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
		}
		res = ast_say_number(chan, count, "", ast_channel_language(chan), (char *) NULL); /* Needs gender */
	}

	return res;
}

/*! \brief The meetme() application */
static int conf_exec(struct ast_channel *chan, const char *data)
{
	int res = -1;
	char confno[MAX_CONFNUM] = "";
	int allowretry = 0;
	int retrycnt = 0;
	struct ast_conference *cnf = NULL;
	struct ast_flags64 confflags = {0};
	struct ast_flags config_flags = { 0 };
	int dynamic = 0;
	int empty = 0, empty_no_pin = 0;
	int always_prompt = 0;
	const char *notdata;
	char *info, the_pin[MAX_PIN] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(options);
		AST_APP_ARG(pin);
	);
	char *optargs[OPT_ARG_ARRAY_SIZE] = { NULL, };

	if (ast_strlen_zero(data)) {
		allowretry = 1;
		notdata = "";
	} else {
		notdata = data;
	}
	
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	info = ast_strdupa(notdata);

	AST_STANDARD_APP_ARGS(args, info);	

	if (args.confno) {
		ast_copy_string(confno, args.confno, sizeof(confno));
		if (ast_strlen_zero(confno)) {
			allowretry = 1;
		}
	}
	
	if (args.pin)
		ast_copy_string(the_pin, args.pin, sizeof(the_pin));

	if (args.options) {
		ast_app_parse_options64(meetme_opts, &confflags, optargs, args.options);
		dynamic = ast_test_flag64(&confflags, CONFFLAG_DYNAMIC | CONFFLAG_DYNAMICPIN);
		if (ast_test_flag64(&confflags, CONFFLAG_DYNAMICPIN) && ast_strlen_zero(args.pin))
			strcpy(the_pin, "q");

		empty = ast_test_flag64(&confflags, CONFFLAG_EMPTY | CONFFLAG_EMPTYNOPIN);
		empty_no_pin = ast_test_flag64(&confflags, CONFFLAG_EMPTYNOPIN);
		always_prompt = ast_test_flag64(&confflags, CONFFLAG_ALWAYSPROMPT | CONFFLAG_DYNAMICPIN);
	}

	do {
		if (retrycnt > 3)
			allowretry = 0;
		if (empty) {
			int i;
			struct ast_config *cfg;
			struct ast_variable *var;
			int confno_int;

			/* We only need to load the config file for static and empty_no_pin (otherwise we don't care) */
			if ((empty_no_pin) || (!dynamic)) {
				cfg = ast_config_load(CONFIG_FILE_NAME, config_flags);
				if (cfg && cfg != CONFIG_STATUS_FILEINVALID) {
					var = ast_variable_browse(cfg, "rooms");
					while (var) {
						char parse[MAX_SETTINGS], *stringp = parse, *confno_tmp;
						if (!strcasecmp(var->name, "conf")) {
							int found = 0;
							ast_copy_string(parse, var->value, sizeof(parse));
							confno_tmp = strsep(&stringp, "|,");
							if (!dynamic) {
								/* For static:  run through the list and see if this conference is empty */
								AST_LIST_LOCK(&confs);
								AST_LIST_TRAVERSE(&confs, cnf, list) {
									if (!strcmp(confno_tmp, cnf->confno)) {
										/* The conference exists, therefore it's not empty */
										found = 1;
										break;
									}
								}
								AST_LIST_UNLOCK(&confs);
								cnf = NULL;
								if (!found) {
									/* At this point, we have a confno_tmp (static conference) that is empty */
									if ((empty_no_pin && ast_strlen_zero(stringp)) || (!empty_no_pin)) {
										/* Case 1:  empty_no_pin and pin is nonexistent (NULL)
										 * Case 2:  empty_no_pin and pin is blank (but not NULL)
										 * Case 3:  not empty_no_pin
										 */
										ast_copy_string(confno, confno_tmp, sizeof(confno));
										break;
									}
								}
							}
						}
						var = var->next;
					}
					ast_config_destroy(cfg);
				}

				if (ast_strlen_zero(confno) && (cfg = ast_load_realtime_multientry("meetme", "confno LIKE", "%", SENTINEL))) {
					const char *catg;
					for (catg = ast_category_browse(cfg, NULL); catg; catg = ast_category_browse(cfg, catg)) {
						const char *confno_tmp = ast_variable_retrieve(cfg, catg, "confno");
						const char *pin_tmp = ast_variable_retrieve(cfg, catg, "pin");
						if (ast_strlen_zero(confno_tmp)) {
							continue;
						}
						if (!dynamic) {
							int found = 0;
							/* For static:  run through the list and see if this conference is empty */
							AST_LIST_LOCK(&confs);
							AST_LIST_TRAVERSE(&confs, cnf, list) {
								if (!strcmp(confno_tmp, cnf->confno)) {
									/* The conference exists, therefore it's not empty */
									found = 1;
									break;
								}
							}
							AST_LIST_UNLOCK(&confs);
							if (!found) {
								/* At this point, we have a confno_tmp (realtime conference) that is empty */
								if ((empty_no_pin && ast_strlen_zero(pin_tmp)) || (!empty_no_pin)) {
									/* Case 1:  empty_no_pin and pin is nonexistent (NULL)
									 * Case 2:  empty_no_pin and pin is blank (but not NULL)
									 * Case 3:  not empty_no_pin
									 */
									ast_copy_string(confno, confno_tmp, sizeof(confno));
									break;
								}
							}
						}
					}
					ast_config_destroy(cfg);
				}
			}

			/* Select first conference number not in use */
			if (ast_strlen_zero(confno) && dynamic) {
				AST_LIST_LOCK(&confs);
				for (i = 0; i < ARRAY_LEN(conf_map); i++) {
					if (!conf_map[i]) {
						snprintf(confno, sizeof(confno), "%d", i);
						conf_map[i] = 1;
						break;
					}
				}
				AST_LIST_UNLOCK(&confs);
			}

			/* Not found? */
			if (ast_strlen_zero(confno)) {
				res = ast_streamfile(chan, "conf-noempty", ast_channel_language(chan));
				ast_test_suite_event_notify("PLAYBACK", "Message: conf-noempty");
				if (!res)
					ast_waitstream(chan, "");
			} else {
				if (sscanf(confno, "%30d", &confno_int) == 1) {
					if (!ast_test_flag64(&confflags, CONFFLAG_QUIET)) {
						res = ast_streamfile(chan, "conf-enteringno", ast_channel_language(chan));
						if (!res) {
							ast_waitstream(chan, "");
							res = ast_say_digits(chan, confno_int, "", ast_channel_language(chan));
						}
					}
				} else {
					ast_log(LOG_ERROR, "Could not scan confno '%s'\n", confno);
				}
			}
		}

		while (allowretry && (ast_strlen_zero(confno)) && (++retrycnt < 4)) {
			/* Prompt user for conference number */
			res = ast_app_getdata(chan, "conf-getconfno", confno, sizeof(confno) - 1, 0);
			if (res < 0) {
				/* Don't try to validate when we catch an error */
				confno[0] = '\0';
				allowretry = 0;
				break;
			}
		}
		if (!ast_strlen_zero(confno)) {
			/* Check the validity of the conference */
			cnf = find_conf(chan, confno, 1, dynamic, the_pin, 
				sizeof(the_pin), 1, &confflags);
			if (!cnf) {
				int too_early = 0;

				cnf = find_conf_realtime(chan, confno, 1, dynamic, 
					the_pin, sizeof(the_pin), 1, &confflags, &too_early, optargs);
				if (rt_schedule && too_early)
					allowretry = 0;
			}

			if (!cnf) {
				if (allowretry) {
					confno[0] = '\0';
					res = ast_streamfile(chan, "conf-invalid", ast_channel_language(chan));
					if (!res)
						ast_waitstream(chan, "");
					res = -1;
				}
			} else {
				/* Conference requires a pin for specified access level */
				int req_pin = !ast_strlen_zero(cnf->pin) ||
					(!ast_strlen_zero(cnf->pinadmin) &&
						ast_test_flag64(&confflags, CONFFLAG_ADMIN));
				/* The following logic was derived from a
				 * 4 variable truth table and defines which
				 * circumstances are not exempt from pin
				 * checking.
				 * If this needs to be modified, write the
				 * truth table back out from the boolean
				 * expression AB+A'D+C', change the erroneous
				 * result, and rederive the expression.
				 * Variables:
				 *  A: pin provided?
				 *  B: always prompt?
				 *  C: dynamic?
				 *  D: has users? */
				int not_exempt = !cnf->isdynamic;
				not_exempt = not_exempt || (!ast_strlen_zero(args.pin) && ast_test_flag64(&confflags, CONFFLAG_ALWAYSPROMPT));
				not_exempt = not_exempt || (ast_strlen_zero(args.pin) && cnf->users);
				if (req_pin && not_exempt) {
					char pin[MAX_PIN] = "";
					int j;

					/* Allow the pin to be retried up to 3 times */
					for (j = 0; j < 3; j++) {
						if (*the_pin && (always_prompt == 0)) {
							ast_copy_string(pin, the_pin, sizeof(pin));
							res = 0;
						} else {
							/* Prompt user for pin if pin is required */
							ast_test_suite_event_notify("PLAYBACK", "Message: conf-getpin\r\n"
								"Channel: %s",
								ast_channel_name(chan));
							res = ast_app_getdata(chan, "conf-getpin", pin + strlen(pin), sizeof(pin) - 1 - strlen(pin), 0);
						}
						if (res >= 0) {
							if ((!strcasecmp(pin, cnf->pin) &&
							     (ast_strlen_zero(cnf->pinadmin) ||
							      !ast_test_flag64(&confflags, CONFFLAG_ADMIN))) ||
							     (!ast_strlen_zero(cnf->pinadmin) &&
							      !strcasecmp(pin, cnf->pinadmin))) {
								/* Pin correct */
								allowretry = 0;
								if (!ast_strlen_zero(cnf->pinadmin) && !strcasecmp(pin, cnf->pinadmin)) {
									if (!ast_strlen_zero(cnf->adminopts)) {
										char *opts = ast_strdupa(cnf->adminopts);
										ast_app_parse_options64(meetme_opts, &confflags, optargs, opts);
									}
								} else {
									if (!ast_strlen_zero(cnf->useropts)) {
										char *opts = ast_strdupa(cnf->useropts);
										ast_app_parse_options64(meetme_opts, &confflags, optargs, opts);
									}
								}
								/* Run the conference */
								ast_verb(4, "Starting recording of MeetMe Conference %s into file %s.%s.\n", cnf->confno, cnf->recordingfilename, cnf->recordingformat);
								res = conf_run(chan, cnf, &confflags, optargs);
								break;
							} else {
								/* Pin invalid */
								if (!ast_streamfile(chan, "conf-invalidpin", ast_channel_language(chan))) {
									res = ast_waitstream(chan, AST_DIGIT_ANY);
									ast_stopstream(chan);
								} else {
									ast_log(LOG_WARNING, "Couldn't play invalid pin msg!\n");
									break;
								}
								if (res < 0)
									break;
								pin[0] = res;
								pin[1] = '\0';
								res = -1;
								if (allowretry)
									confno[0] = '\0';
							}
						} else {
							/* failed when getting the pin */
							res = -1;
							allowretry = 0;
							/* see if we need to get rid of the conference */
							break;
						}

						/* Don't retry pin with a static pin */
						if (*the_pin && (always_prompt == 0)) {
							break;
						}
					}
				} else {
					/* No pin required */
					allowretry = 0;

					/* For RealTime conferences without a pin 
					 * should still support loading options
					 */
					if (!ast_strlen_zero(cnf->useropts)) {
						char *opts = ast_strdupa(cnf->useropts);
						ast_app_parse_options64(meetme_opts, &confflags, optargs, opts);
					}

					/* Run the conference */
					res = conf_run(chan, cnf, &confflags, optargs);
				}
				dispose_conf(cnf);
				cnf = NULL;
			}
		}
	} while (allowretry);

	if (cnf)
		dispose_conf(cnf);
	
	return res;
}

static struct ast_conf_user *find_user(struct ast_conference *conf, const char *callerident)
{
	struct ast_conf_user *user = NULL;
	int cid;

	if (conf && callerident && sscanf(callerident, "%30d", &cid) == 1) {
		user = ao2_find(conf->usercontainer, &cid, 0);
		/* reference decremented later in admin_exec */
		return user;
	}
	return NULL;
}

static int user_listen_volup_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_listen_volume(user, VOL_UP);
	return 0;
}

static int user_listen_voldown_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_listen_volume(user, VOL_DOWN);
	return 0;
}

static int user_talk_volup_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_talk_volume(user, VOL_UP);
	return 0;
}

static int user_talk_voldown_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_talk_volume(user, VOL_DOWN);
	return 0;
}

static int user_reset_vol_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	reset_volumes(user);
	return 0;
}

static int user_chan_cb(void *obj, void *args, int flags)
{
	struct ast_conf_user *user = obj;
	const char *channel = args;

	if (!strcmp(ast_channel_name(user->chan), channel)) {
		return (CMP_MATCH | CMP_STOP);
	}

	return 0;
}

/*! \brief The MeetMeadmin application 

  MeetMeAdmin(confno, command, caller) */
static int admin_exec(struct ast_channel *chan, const char *data) {
	char *params;
	struct ast_conference *cnf;
	struct ast_conf_user *user = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(command);
		AST_APP_ARG(user);
	);
	int res = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MeetMeAdmin requires an argument!\n");
		pbx_builtin_setvar_helper(chan, "MEETMEADMINSTATUS", "NOPARSE");
		return -1;
	}

	params = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, params);

	if (!args.command) {
		ast_log(LOG_WARNING, "MeetmeAdmin requires a command!\n");
		pbx_builtin_setvar_helper(chan, "MEETMEADMINSTATUS", "NOPARSE");
		return -1;
	}

	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(cnf->confno, args.confno))
			break;
	}

	if (!cnf) {
		ast_log(LOG_WARNING, "Conference number '%s' not found!\n", args.confno);
		AST_LIST_UNLOCK(&confs);
		pbx_builtin_setvar_helper(chan, "MEETMEADMINSTATUS", "NOTFOUND");
		return 0;
	}

	ast_atomic_fetchadd_int(&cnf->refcount, 1);

	if (args.user) {
		user = find_user(cnf, args.user);
		if (!user) {
			ast_log(LOG_NOTICE, "Specified User not found!\n");
			res = -2;
			goto usernotfound;
		}
	} else {
		/* fail for commands that require a user */
		switch (*args.command) {
		case 'm': /* Unmute */
		case 'M': /* Mute */
		case 't': /* Lower user's talk volume */
		case 'T': /* Raise user's talk volume */
		case 'u': /* Lower user's listen volume */
		case 'U': /* Raise user's listen volume */
		case 'r': /* Reset user's volume level */
		case 'k': /* Kick user */
			res = -2;
			ast_log(LOG_NOTICE, "No user specified!\n");
			goto usernotfound;
		default:
			break;
		}
	}

	switch (*args.command) {
	case 76: /* L: Lock */ 
		cnf->locked = 1;
		break;
	case 108: /* l: Unlock */ 
		cnf->locked = 0;
		break;
	case 75: /* K: kick all users */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_set_kickme_cb, NULL);
		break;
	case 101: /* e: Eject last user*/
	{
		int max_no = 0;
		RAII_VAR(struct ast_conf_user *, eject_user, NULL, ao2_cleanup);

		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_max_cmp, &max_no);
		eject_user = ao2_find(cnf->usercontainer, &max_no, 0);
		if (!eject_user) {
			res = -1;
			ast_log(LOG_NOTICE, "No last user to kick!\n");
			break;
		}

		if (!ast_test_flag64(&eject_user->userflags, CONFFLAG_ADMIN)) {
			eject_user->adminflags |= ADMINFLAG_KICKME;
		} else {
			res = -1;
			ast_log(LOG_NOTICE, "Not kicking last user, is an Admin!\n");
		}
		break;
	}
	case 77: /* M: Mute */ 
		user->adminflags |= ADMINFLAG_MUTED;
		break;
	case 78: /* N: Mute all (non-admin) users */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_set_muted_cb, &cnf);
		break;					
	case 109: /* m: Unmute */ 
		user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED | ADMINFLAG_T_REQUEST);
		break;
	case 110: /* n: Unmute all users */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_set_unmuted_cb, NULL);
		break;
	case 107: /* k: Kick user */ 
		user->adminflags |= ADMINFLAG_KICKME;
		break;
	case 118: /* v: Lower all users listen volume */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_listen_voldown_cb, NULL);
		break;
	case 86: /* V: Raise all users listen volume */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_listen_volup_cb, NULL);
		break;
	case 115: /* s: Lower all users speaking volume */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_talk_voldown_cb, NULL);
		break;
	case 83: /* S: Raise all users speaking volume */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_talk_volup_cb, NULL);
		break;
	case 82: /* R: Reset all volume levels */
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_reset_vol_cb, NULL);
		break;
	case 114: /* r: Reset user's volume level */
		reset_volumes(user);
		break;
	case 85: /* U: Raise user's listen volume */
		tweak_listen_volume(user, VOL_UP);
		break;
	case 117: /* u: Lower user's listen volume */
		tweak_listen_volume(user, VOL_DOWN);
		break;
	case 84: /* T: Raise user's talk volume */
		tweak_talk_volume(user, VOL_UP);
		break;
	case 116: /* t: Lower user's talk volume */
		tweak_talk_volume(user, VOL_DOWN);
		break;
	case 'E': /* E: Extend conference */
		if (rt_extend_conf(args.confno)) {
			res = -1;
		}
		break;
	}

	if (args.user) {
		/* decrement reference from find_user */
		ao2_ref(user, -1);
	}
usernotfound:
	AST_LIST_UNLOCK(&confs);

	dispose_conf(cnf);
	pbx_builtin_setvar_helper(chan, "MEETMEADMINSTATUS", res == -2 ? "NOTFOUND" : res ? "FAILED" : "OK");

	return 0;
}

/*! \brief The MeetMeChannelAdmin application 
	MeetMeChannelAdmin(channel, command) */
static int channel_admin_exec(struct ast_channel *chan, const char *data) {
	char *params;
	struct ast_conference *conf = NULL;
	struct ast_conf_user *user = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(command);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MeetMeChannelAdmin requires two arguments!\n");
		return -1;
	}
	
	params = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, params);

	if (!args.channel) {
		ast_log(LOG_WARNING, "MeetMeChannelAdmin requires a channel name!\n");
		return -1;
	}

	if (!args.command) {
		ast_log(LOG_WARNING, "MeetMeChannelAdmin requires a command!\n");
		return -1;
	}

	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if ((user = ao2_callback(conf->usercontainer, 0, user_chan_cb, args.channel))) {
			break;
		}
	}
	
	if (!user) {
		ast_log(LOG_NOTICE, "Specified user (%s) not found\n", args.channel);
		AST_LIST_UNLOCK(&confs);
		return 0;
	}
	
	/* perform the specified action */
	switch (*args.command) {
		case 77: /* M: Mute */ 
			user->adminflags |= ADMINFLAG_MUTED;
			break;
		case 109: /* m: Unmute */ 
			user->adminflags &= ~ADMINFLAG_MUTED;
			break;
		case 107: /* k: Kick user */ 
			user->adminflags |= ADMINFLAG_KICKME;
			break;
		default: /* unknown command */
			ast_log(LOG_WARNING, "Unknown MeetMeChannelAdmin command '%s'\n", args.command);
			break;
	}
	ao2_ref(user, -1);
	AST_LIST_UNLOCK(&confs);
	
	return 0;
}

static int meetmemute(struct mansession *s, const struct message *m, int mute)
{
	struct ast_conference *conf;
	struct ast_conf_user *user;
	const char *confid = astman_get_header(m, "Meetme");
	char *userid = ast_strdupa(astman_get_header(m, "Usernum"));
	int userno;

	if (ast_strlen_zero(confid)) {
		astman_send_error(s, m, "Meetme conference not specified");
		return 0;
	}

	if (ast_strlen_zero(userid)) {
		astman_send_error(s, m, "Meetme user number not specified");
		return 0;
	}

	userno = strtoul(userid, &userid, 10);

	if (*userid) {
		astman_send_error(s, m, "Invalid user number");
		return 0;
	}

	/* Look in the conference list */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if (!strcmp(confid, conf->confno))
			break;
	}

	if (!conf) {
		AST_LIST_UNLOCK(&confs);
		astman_send_error(s, m, "Meetme conference does not exist");
		return 0;
	}

	user = ao2_find(conf->usercontainer, &userno, 0);

	if (!user) {
		AST_LIST_UNLOCK(&confs);
		astman_send_error(s, m, "User number not found");
		return 0;
	}

	if (mute)
		user->adminflags |= ADMINFLAG_MUTED;	/* request user muting */
	else
		user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED | ADMINFLAG_T_REQUEST);	/* request user unmuting */

	AST_LIST_UNLOCK(&confs);

	ast_log(LOG_NOTICE, "Requested to %smute conf %s user %d userchan %s uniqueid %s\n", mute ? "" : "un", conf->confno, user->user_no, ast_channel_name(user->chan), ast_channel_uniqueid(user->chan));

	ao2_ref(user, -1);
	astman_send_ack(s, m, mute ? "User muted" : "User unmuted");
	return 0;
}

static int action_meetmemute(struct mansession *s, const struct message *m)
{
	return meetmemute(s, m, 1);
}

static int action_meetmeunmute(struct mansession *s, const struct message *m)
{
	return meetmemute(s, m, 0);
}

static int action_meetmelist(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	const char *conference = astman_get_header(m, "Conference");
	char idText[80] = "";
	struct ast_conference *cnf;
	struct ast_conf_user *user;
	struct ao2_iterator user_iter;
	int total = 0;

	if (!ast_strlen_zero(actionid))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);

	if (AST_LIST_EMPTY(&confs)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	astman_send_listack(s, m, "Meetme user list will follow", "start");

	/* Find the right conference */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		/* If we ask for one particular, and this isn't it, skip it */
		if (!ast_strlen_zero(conference) && strcmp(cnf->confno, conference))
			continue;

		/* Show all the users */
		user_iter = ao2_iterator_init(cnf->usercontainer, 0);
		while ((user = ao2_iterator_next(&user_iter))) {
			total++;
			astman_append(s,
				"Event: MeetmeList\r\n"
				"%s"
				"Conference: %s\r\n"
				"UserNumber: %d\r\n"
				"CallerIDNum: %s\r\n"
				"CallerIDName: %s\r\n"
				"ConnectedLineNum: %s\r\n"
				"ConnectedLineName: %s\r\n"
				"Channel: %s\r\n"
				"Admin: %s\r\n"
				"Role: %s\r\n"
				"MarkedUser: %s\r\n"
				"Muted: %s\r\n"
				"Talking: %s\r\n"
				"\r\n",
				idText,
				cnf->confno,
				user->user_no,
				S_COR(ast_channel_caller(user->chan)->id.number.valid, ast_channel_caller(user->chan)->id.number.str, "<unknown>"),
				S_COR(ast_channel_caller(user->chan)->id.name.valid, ast_channel_caller(user->chan)->id.name.str, "<no name>"),
				S_COR(ast_channel_connected(user->chan)->id.number.valid, ast_channel_connected(user->chan)->id.number.str, "<unknown>"),
				S_COR(ast_channel_connected(user->chan)->id.name.valid, ast_channel_connected(user->chan)->id.name.str, "<no name>"),
				ast_channel_name(user->chan),
				ast_test_flag64(&user->userflags, CONFFLAG_ADMIN) ? "Yes" : "No",
				ast_test_flag64(&user->userflags, CONFFLAG_MONITOR) ? "Listen only" : ast_test_flag64(&user->userflags, CONFFLAG_TALKER) ? "Talk only" : "Talk and listen",
				ast_test_flag64(&user->userflags, CONFFLAG_MARKEDUSER) ? "Yes" : "No",
				user->adminflags & ADMINFLAG_MUTED ? "By admin" : user->adminflags & ADMINFLAG_SELFMUTED ? "By self" : "No",
				user->talking > 0 ? "Yes" : user->talking == 0 ? "No" : "Not monitored");
			ao2_ref(user, -1);
		}
		ao2_iterator_destroy(&user_iter);
	}
	AST_LIST_UNLOCK(&confs);

	/* Send final confirmation */
	astman_send_list_complete_start(s, m, "MeetmeListComplete", total);
	astman_send_list_complete_end(s);
	return 0;
}

static int action_meetmelistrooms(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[80] = "";
	struct ast_conference *cnf;
	int totalitems = 0;
	int hr, min, sec;
	time_t now;
	char markedusers[5];

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	}

	if (AST_LIST_EMPTY(&confs)) {
		astman_send_error(s, m, "No active conferences.");
		return 0;
	}

	astman_send_listack(s, m, "Meetme conferences will follow", "start");

	now = time(NULL);

	/* Traverse the conference list */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		totalitems++;

		if (cnf->markedusers == 0) {
			strcpy(markedusers, "N/A");
		} else {
			sprintf(markedusers, "%.4d", cnf->markedusers);
		}
		hr = (now - cnf->start) / 3600;
		min = ((now - cnf->start) % 3600) / 60;
		sec = (now - cnf->start) % 60;

		astman_append(s,
		"Event: MeetmeListRooms\r\n"
		"%s"
		"Conference: %s\r\n"
		"Parties: %d\r\n"
		"Marked: %s\r\n"
		"Activity: %2.2d:%2.2d:%2.2d\r\n"
		"Creation: %s\r\n"
		"Locked: %s\r\n"
		"\r\n",
		idText,
		cnf->confno,
		cnf->users,
		markedusers,
		hr,  min, sec,
		cnf->isdynamic ? "Dynamic" : "Static",
		cnf->locked ? "Yes" : "No"); 
	}
	AST_LIST_UNLOCK(&confs);

	/* Send final confirmation */
	astman_send_list_complete_start(s, m, "MeetmeListRoomsComplete", totalitems);
	astman_send_list_complete_end(s);
	return 0;
}

/*! \internal
 * \brief creates directory structure and assigns absolute path from relative paths for filenames
 *
 * \param filename contains the absolute or relative path to the desired file
 * \param buffer stores completed filename, absolutely must be a buffer of PATH_MAX length
 */
static void filename_parse(char *filename, char *buffer)
{
	char *slash;
	if (ast_strlen_zero(filename)) {
		ast_log(LOG_WARNING, "No file name was provided for a file save option.\n");
	} else if (filename[0] != '/') {
		snprintf(buffer, PATH_MAX, "%s/meetme/%s", ast_config_AST_SPOOL_DIR, filename);
	} else {
		ast_copy_string(buffer, filename, PATH_MAX);
	}

	slash = buffer;
	if ((slash = strrchr(slash, '/'))) {
		*slash = '\0';
		ast_mkdir(buffer, 0777);
		*slash = '/';
	}
}

static void *recordthread(void *args)
{
	struct ast_conference *cnf = args;
	struct ast_frame *f = NULL;
	int flags;
	struct ast_filestream *s = NULL;
	int res = 0;
	int x;
	const char *oldrecordingfilename = NULL;
	char filename_buffer[PATH_MAX];

	if (!cnf || !cnf->lchan) {
		pthread_exit(0);
	}

	filename_buffer[0] = '\0';
	filename_parse(cnf->recordingfilename, filename_buffer);

	ast_stopstream(cnf->lchan);
	flags = O_CREAT | O_TRUNC | O_WRONLY;


	cnf->recording = MEETME_RECORD_ACTIVE;
	while (ast_waitfor(cnf->lchan, -1) > -1) {
		if (cnf->recording == MEETME_RECORD_TERMINATE) {
			AST_LIST_LOCK(&confs);
			AST_LIST_UNLOCK(&confs);
			break;
		}
		if (!s && !(ast_strlen_zero(filename_buffer)) && (filename_buffer != oldrecordingfilename)) {
			s = ast_writefile(filename_buffer, cnf->recordingformat, NULL, flags, 0, AST_FILE_MODE);
			oldrecordingfilename = filename_buffer;
		}
		
		f = ast_read(cnf->lchan);
		if (!f) {
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			ast_mutex_lock(&cnf->listenlock);
			for (x = 0; x < AST_FRAME_BITS; x++) {
				/* Free any translations that have occured */
				if (cnf->transframe[x]) {
					ast_frfree(cnf->transframe[x]);
					cnf->transframe[x] = NULL;
				}
			}
			if (cnf->origframe)
				ast_frfree(cnf->origframe);
			cnf->origframe = ast_frdup(f);
			ast_mutex_unlock(&cnf->listenlock);
			if (s)
				res = ast_writestream(s, f);
			if (res) {
				ast_frfree(f);
				break;
			}
		}
		ast_frfree(f);
	}
	cnf->recording = MEETME_RECORD_OFF;
	if (s)
		ast_closestream(s);
	
	pthread_exit(0);
}

/*! \brief Callback for devicestate providers */
static enum ast_device_state meetmestate(const char *data)
{
	struct ast_conference *conf;

	/* Find conference */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if (!strcmp(data, conf->confno))
			break;
	}
	AST_LIST_UNLOCK(&confs);
	if (!conf)
		return AST_DEVICE_INVALID;


	/* SKREP to fill */
	if (!conf->users)
		return AST_DEVICE_NOT_INUSE;

	return AST_DEVICE_INUSE;
}

static void meetme_set_defaults(void)
{
	/*  Scheduling support is off by default */
	rt_schedule = 0;
	fuzzystart = 0;
	earlyalert = 0;
	endalert = 0;
	extendby = 0;

	/*  Logging of participants defaults to ON for compatibility reasons */
	rt_log_members = 1;
}

static void load_config_meetme(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	const char *val;

	if (!reload) {
		meetme_set_defaults();
	}

	if (!(cfg = ast_config_load(CONFIG_FILE_NAME, config_flags))) {
		return;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " CONFIG_FILE_NAME " is in an invalid format.  Aborting.\n");
		return;
	}

	if (reload) {
		meetme_set_defaults();
	}

	if ((val = ast_variable_retrieve(cfg, "general", "audiobuffers"))) {
		if ((sscanf(val, "%30d", &audio_buffers) != 1)) {
			ast_log(LOG_WARNING, "audiobuffers setting must be a number, not '%s'\n", val);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		} else if ((audio_buffers < DAHDI_DEFAULT_NUM_BUFS) || (audio_buffers > DAHDI_MAX_NUM_BUFS)) {
			ast_log(LOG_WARNING, "audiobuffers setting must be between %d and %d\n",
				DAHDI_DEFAULT_NUM_BUFS, DAHDI_MAX_NUM_BUFS);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		}
		if (audio_buffers != DEFAULT_AUDIO_BUFFERS)
			ast_log(LOG_NOTICE, "Audio buffers per channel set to %d\n", audio_buffers);
	}

	if ((val = ast_variable_retrieve(cfg, "general", "schedule")))
		rt_schedule = ast_true(val);
	if ((val = ast_variable_retrieve(cfg, "general", "logmembercount")))
		rt_log_members = ast_true(val);
	if ((val = ast_variable_retrieve(cfg, "general", "fuzzystart"))) {
		if ((sscanf(val, "%30d", &fuzzystart) != 1)) {
			ast_log(LOG_WARNING, "fuzzystart must be a number, not '%s'\n", val);
			fuzzystart = 0;
		} 
	}
	if ((val = ast_variable_retrieve(cfg, "general", "earlyalert"))) {
		if ((sscanf(val, "%30d", &earlyalert) != 1)) {
			ast_log(LOG_WARNING, "earlyalert must be a number, not '%s'\n", val);
			earlyalert = 0;
		} 
	}
	if ((val = ast_variable_retrieve(cfg, "general", "endalert"))) {
		if ((sscanf(val, "%30d", &endalert) != 1)) {
			ast_log(LOG_WARNING, "endalert must be a number, not '%s'\n", val);
			endalert = 0;
		} 
	}
	if ((val = ast_variable_retrieve(cfg, "general", "extendby"))) {
		if ((sscanf(val, "%30d", &extendby) != 1)) {
			ast_log(LOG_WARNING, "extendby must be a number, not '%s'\n", val);
			extendby = 0;
		} 
	}

	ast_config_destroy(cfg);
}

/*!
 * \internal
 * \brief Find an SLA trunk by name
 */
static struct sla_trunk *sla_find_trunk(const char *name)
{
	struct sla_trunk tmp_trunk = {
		.name = name,
	};

	return ao2_find(sla_trunks, &tmp_trunk, OBJ_POINTER);
}

/*!
 * \internal
 * \brief Find an SLA station by name
 */
static struct sla_station *sla_find_station(const char *name)
{
	struct sla_station tmp_station = {
		.name = name,
	};

	return ao2_find(sla_stations, &tmp_station, OBJ_POINTER);
}

static int sla_check_station_hold_access(const struct sla_trunk *trunk,
	const struct sla_station *station)
{
	struct sla_station_ref *station_ref;
	struct sla_trunk_ref *trunk_ref;

	/* For each station that has this call on hold, check for private hold. */
	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		AST_LIST_TRAVERSE(&station_ref->station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || station_ref->station == station)
				continue;
			if (trunk_ref->state == SLA_TRUNK_STATE_ONHOLD_BYME &&
				station_ref->station->hold_access == SLA_HOLD_PRIVATE)
				return 1;
			return 0;
		}
	}

	return 0;
}

/*!
 * \brief Find a trunk reference on a station by name
 * \param station the station
 * \param name the trunk's name
 * \pre sla_station is locked
 * \return a pointer to the station's trunk reference.  If the trunk
 *         is not found, it is not idle and barge is disabled, or if
 *         it is on hold and private hold is set, then NULL will be returned.
 */
static struct sla_trunk_ref *sla_find_trunk_ref_byname(const struct sla_station *station,
	const char *name)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (strcasecmp(trunk_ref->trunk->name, name))
			continue;

		if ( (trunk_ref->trunk->barge_disabled 
			&& trunk_ref->state == SLA_TRUNK_STATE_UP) ||
			(trunk_ref->trunk->hold_stations 
			&& trunk_ref->trunk->hold_access == SLA_HOLD_PRIVATE
			&& trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) ||
			sla_check_station_hold_access(trunk_ref->trunk, station) ) 
		{
			trunk_ref = NULL;
		}

		break;
	}

	if (trunk_ref) {
		ao2_ref(trunk_ref, 1);
	}

	return trunk_ref;
}

static void sla_station_ref_destructor(void *obj)
{
	struct sla_station_ref *station_ref = obj;

	if (station_ref->station) {
		ao2_ref(station_ref->station, -1);
		station_ref->station = NULL;
	}
}

static struct sla_station_ref *sla_create_station_ref(struct sla_station *station)
{
	struct sla_station_ref *station_ref;

	if (!(station_ref = ao2_alloc(sizeof(*station_ref), sla_station_ref_destructor))) {
		return NULL;
	}

	ao2_ref(station, 1);
	station_ref->station = station;

	return station_ref;
}

static struct sla_ringing_station *sla_create_ringing_station(struct sla_station *station)
{
	struct sla_ringing_station *ringing_station;

	if (!(ringing_station = ast_calloc(1, sizeof(*ringing_station))))
		return NULL;

	ao2_ref(station, 1);
	ringing_station->station = station;
	ringing_station->ring_begin = ast_tvnow();

	return ringing_station;
}

static void sla_ringing_station_destroy(struct sla_ringing_station *ringing_station)
{
	if (ringing_station->station) {
		ao2_ref(ringing_station->station, -1);
		ringing_station->station = NULL;
	}

	ast_free(ringing_station);
}

static struct sla_failed_station *sla_create_failed_station(struct sla_station *station)
{
	struct sla_failed_station *failed_station;

	if (!(failed_station = ast_calloc(1, sizeof(*failed_station)))) {
		return NULL;
	}

	ao2_ref(station, 1);
	failed_station->station = station;
	failed_station->last_try = ast_tvnow();

	return failed_station;
}

static void sla_failed_station_destroy(struct sla_failed_station *failed_station)
{
	if (failed_station->station) {
		ao2_ref(failed_station->station, -1);
		failed_station->station = NULL;
	}

	ast_free(failed_station);
}

static enum ast_device_state sla_state_to_devstate(enum sla_trunk_state state)
{
	switch (state) {
	case SLA_TRUNK_STATE_IDLE:
		return AST_DEVICE_NOT_INUSE;
	case SLA_TRUNK_STATE_RINGING:
		return AST_DEVICE_RINGING;
	case SLA_TRUNK_STATE_UP:
		return AST_DEVICE_INUSE;
	case SLA_TRUNK_STATE_ONHOLD:
	case SLA_TRUNK_STATE_ONHOLD_BYME:
		return AST_DEVICE_ONHOLD;
	}

	return AST_DEVICE_UNKNOWN;
}

static void sla_change_trunk_state(const struct sla_trunk *trunk, enum sla_trunk_state state, 
	enum sla_which_trunk_refs inactive_only, const struct sla_trunk_ref *exclude)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	struct ao2_iterator i;

	i = ao2_iterator_init(sla_stations, 0);
	while ((station = ao2_iterator_next(&i))) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || (inactive_only ? trunk_ref->chan : 0)
					|| trunk_ref == exclude) {
				continue;
			}
			trunk_ref->state = state;
			ast_devstate_changed(sla_state_to_devstate(state), AST_DEVSTATE_CACHABLE,
					     "SLA:%s_%s", station->name, trunk->name);
			break;
		}
		ao2_unlock(station);
		ao2_ref(station, -1);
	}
	ao2_iterator_destroy(&i);
}

struct run_station_args {
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

static void answer_trunk_chan(struct ast_channel *chan)
{
	ast_answer(chan);
	ast_indicate(chan, -1);
}

static void *run_station(void *data)
{
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	struct ast_str *conf_name = ast_str_create(16);
	struct ast_flags64 conf_flags = { 0 };
	struct ast_conference *conf;

	{
		struct run_station_args *args = data;
		station = args->station;
		trunk_ref = args->trunk_ref;
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		/* args is no longer valid here. */
	}

	ast_atomic_fetchadd_int((int *) &trunk_ref->trunk->active_stations, 1);
	ast_str_set(&conf_name, 0, "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag64(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_STATION);
	answer_trunk_chan(trunk_ref->chan);
	conf = build_conf(ast_str_buffer(conf_name), "", "", 0, 0, 1, trunk_ref->chan, NULL);
	if (conf) {
		conf_run(trunk_ref->chan, conf, &conf_flags, NULL);
		dispose_conf(conf);
		conf = NULL;
	}
	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
		trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
		ast_str_append(&conf_name, 0, ",K");
		admin_exec(NULL, ast_str_buffer(conf_name));
		trunk_ref->trunk->hold_stations = 0;
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}

	ast_dial_join(station->dial);
	ast_dial_destroy(station->dial);
	station->dial = NULL;
	ast_free(conf_name);

	return NULL;
}

static void sla_ringing_trunk_destroy(struct sla_ringing_trunk *ringing_trunk);

static void sla_stop_ringing_trunk(struct sla_ringing_trunk *ringing_trunk)
{
	char buf[80];
	struct sla_station_ref *station_ref;

	snprintf(buf, sizeof(buf), "SLA_%s,K", ringing_trunk->trunk->name);
	admin_exec(NULL, buf);
	sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	while ((station_ref = AST_LIST_REMOVE_HEAD(&ringing_trunk->timed_out_stations, entry))) {
		ao2_ref(station_ref, -1);
	}

	sla_ringing_trunk_destroy(ringing_trunk);
}

static void sla_stop_ringing_station(struct sla_ringing_station *ringing_station,
	enum sla_station_hangup hangup)
{
	struct sla_ringing_trunk *ringing_trunk;
	struct sla_trunk_ref *trunk_ref;
	struct sla_station_ref *station_ref;

	ast_dial_join(ringing_station->station->dial);
	ast_dial_destroy(ringing_station->station->dial);
	ringing_station->station->dial = NULL;

	if (hangup == SLA_STATION_HANGUP_NORMAL)
		goto done;

	/* If the station is being hung up because of a timeout, then add it to the
	 * list of timed out stations on each of the ringing trunks.  This is so
	 * that when doing further processing to figure out which stations should be
	 * ringing, which trunk to answer, determining timeouts, etc., we know which
	 * ringing trunks we should ignore. */
	AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk)
				break;
		}
		if (!trunk_ref)
			continue;
		if (!(station_ref = sla_create_station_ref(ringing_station->station)))
			continue;
		AST_LIST_INSERT_TAIL(&ringing_trunk->timed_out_stations, station_ref, entry);
	}

done:
	sla_ringing_station_destroy(ringing_station);
}

static void sla_dial_state_callback(struct ast_dial *dial)
{
	sla_queue_event(SLA_EVENT_DIAL_STATE);
}

/*! \brief Check to see if dialing this station already timed out for this ringing trunk
 * \note Assumes sla.lock is locked
 */
static int sla_check_timed_out_station(const struct sla_ringing_trunk *ringing_trunk,
	const struct sla_station *station)
{
	struct sla_station_ref *timed_out_station;

	AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, timed_out_station, entry) {
		if (station == timed_out_station->station)
			return 1;
	}

	return 0;
}

/*! \brief Choose the highest priority ringing trunk for a station
 * \param station the station
 * \param rm remove the ringing trunk once selected
 * \param trunk_ref a place to store the pointer to this stations reference to
 *        the selected trunk
 * \return a pointer to the selected ringing trunk, or NULL if none found
 * \note Assumes that sla.lock is locked
 */
static struct sla_ringing_trunk *sla_choose_ringing_trunk(struct sla_station *station, 
	struct sla_trunk_ref **trunk_ref, int rm)
{
	struct sla_trunk_ref *s_trunk_ref;
	struct sla_ringing_trunk *ringing_trunk = NULL;

	AST_LIST_TRAVERSE(&station->trunks, s_trunk_ref, entry) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
			/* Make sure this is the trunk we're looking for */
			if (s_trunk_ref->trunk != ringing_trunk->trunk)
				continue;

			/* This trunk on the station is ringing.  But, make sure this station
			 * didn't already time out while this trunk was ringing. */
			if (sla_check_timed_out_station(ringing_trunk, station))
				continue;

			if (rm)
				AST_LIST_REMOVE_CURRENT(entry);

			if (trunk_ref) {
				ao2_ref(s_trunk_ref, 1);
				*trunk_ref = s_trunk_ref;
			}

			break;
		}
		AST_LIST_TRAVERSE_SAFE_END;
	
		if (ringing_trunk)
			break;
	}

	return ringing_trunk;
}

static void sla_handle_dial_state_event(void)
{
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		RAII_VAR(struct sla_trunk_ref *, s_trunk_ref, NULL, ao2_cleanup);
		struct sla_ringing_trunk *ringing_trunk = NULL;
		struct run_station_args args;
		enum ast_dial_result dial_res;
		pthread_t dont_care;
		ast_mutex_t cond_lock;
		ast_cond_t cond;

		switch ((dial_res = ast_dial_state(ringing_station->station->dial))) {
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			AST_LIST_REMOVE_CURRENT(entry);
			sla_stop_ringing_station(ringing_station, SLA_STATION_HANGUP_NORMAL);
			break;
		case AST_DIAL_RESULT_ANSWERED:
			AST_LIST_REMOVE_CURRENT(entry);
			/* Find the appropriate trunk to answer. */
			ast_mutex_lock(&sla.lock);
			ringing_trunk = sla_choose_ringing_trunk(ringing_station->station, &s_trunk_ref, 1);
			ast_mutex_unlock(&sla.lock);
			if (!ringing_trunk) {
				/* This case happens in a bit of a race condition.  If two stations answer
				 * the outbound call at the same time, the first one will get connected to
				 * the trunk.  When the second one gets here, it will not see any trunks
				 * ringing so we have no idea what to conect it to.  So, we just hang up
				 * on it. */
				ast_debug(1, "Found no ringing trunk for station '%s' to answer!\n", ringing_station->station->name);
				ast_dial_join(ringing_station->station->dial);
				ast_dial_destroy(ringing_station->station->dial);
				ringing_station->station->dial = NULL;
				sla_ringing_station_destroy(ringing_station);
				break;
			}
			/* Track the channel that answered this trunk */
			s_trunk_ref->chan = ast_dial_answered(ringing_station->station->dial);
			/* Actually answer the trunk */
			answer_trunk_chan(ringing_trunk->trunk->chan);
			sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
			/* Now, start a thread that will connect this station to the trunk.  The rest of
			 * the code here sets up the thread and ensures that it is able to save the arguments
			 * before they are no longer valid since they are allocated on the stack. */
			ao2_ref(s_trunk_ref, 1);
			args.trunk_ref = s_trunk_ref;
			ao2_ref(ringing_station->station, 1);
			args.station = ringing_station->station;
			args.cond = &cond;
			args.cond_lock = &cond_lock;
			sla_ringing_trunk_destroy(ringing_trunk);
			sla_ringing_station_destroy(ringing_station);
			ast_mutex_init(&cond_lock);
			ast_cond_init(&cond, NULL);
			ast_mutex_lock(&cond_lock);
			ast_pthread_create_detached_background(&dont_care, NULL, run_station, &args);
			ast_cond_wait(&cond, &cond_lock);
			ast_mutex_unlock(&cond_lock);
			ast_mutex_destroy(&cond_lock);
			ast_cond_destroy(&cond);
			break;
		case AST_DIAL_RESULT_TRYING:
		case AST_DIAL_RESULT_RINGING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_PROCEEDING:
			break;
		}
		if (dial_res == AST_DIAL_RESULT_ANSWERED) {
			/* Queue up reprocessing ringing trunks, and then ringing stations again */
			sla_queue_event(SLA_EVENT_RINGING_TRUNK);
			sla_queue_event(SLA_EVENT_DIAL_STATE);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

/*! \brief Check to see if this station is already ringing 
 * \note Assumes sla.lock is locked 
 */
static int sla_check_ringing_station(const struct sla_station *station)
{
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE(&sla.ringing_stations, ringing_station, entry) {
		if (station == ringing_station->station)
			return 1;
	}

	return 0;
}

/*! \brief Check to see if this station has failed to be dialed in the past minute
 * \note assumes sla.lock is locked
 */
static int sla_check_failed_station(const struct sla_station *station)
{
	struct sla_failed_station *failed_station;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.failed_stations, failed_station, entry) {
		if (station != failed_station->station)
			continue;
		if (ast_tvdiff_ms(ast_tvnow(), failed_station->last_try) > 1000) {
			AST_LIST_REMOVE_CURRENT(entry);
			sla_failed_station_destroy(failed_station);
			break;
		}
		res = 1;
	}
	AST_LIST_TRAVERSE_SAFE_END

	return res;
}

/*! \brief Ring a station
 * \note Assumes sla.lock is locked
 */
static int sla_ring_station(struct sla_ringing_trunk *ringing_trunk, struct sla_station *station)
{
	char *tech, *tech_data;
	struct ast_dial *dial;
	struct sla_ringing_station *ringing_station;
	enum ast_dial_result res;
	int caller_is_saved;
	struct ast_party_caller caller;

	if (!(dial = ast_dial_create()))
		return -1;

	ast_dial_set_state_callback(dial, sla_dial_state_callback);
	tech_data = ast_strdupa(station->device);
	tech = strsep(&tech_data, "/");

	if (ast_dial_append(dial, tech, tech_data, NULL) == -1) {
		ast_dial_destroy(dial);
		return -1;
	}

	/* Do we need to save off the caller ID data? */
	caller_is_saved = 0;
	if (!sla.attempt_callerid) {
		caller_is_saved = 1;
		caller = *ast_channel_caller(ringing_trunk->trunk->chan);
		ast_party_caller_init(ast_channel_caller(ringing_trunk->trunk->chan));
	}

	res = ast_dial_run(dial, ringing_trunk->trunk->chan, 1);
	
	/* Restore saved caller ID */
	if (caller_is_saved) {
		ast_party_caller_free(ast_channel_caller(ringing_trunk->trunk->chan));
		ast_channel_caller_set(ringing_trunk->trunk->chan, &caller);
	}
	
	if (res != AST_DIAL_RESULT_TRYING) {
		struct sla_failed_station *failed_station;
		ast_dial_destroy(dial);
		if ((failed_station = sla_create_failed_station(station))) {
			AST_LIST_INSERT_HEAD(&sla.failed_stations, failed_station, entry);
		}
		return -1;
	}
	if (!(ringing_station = sla_create_ringing_station(station))) {
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		return -1;
	}

	station->dial = dial;

	AST_LIST_INSERT_HEAD(&sla.ringing_stations, ringing_station, entry);

	return 0;
}

/*! \brief Check to see if a station is in use
 */
static int sla_check_inuse_station(const struct sla_station *station)
{
	struct sla_trunk_ref *trunk_ref;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->chan)
			return 1;
	}

	return 0;
}

static struct sla_trunk_ref *sla_find_trunk_ref(const struct sla_station *station,
	const struct sla_trunk *trunk)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk)
			break;
	}

	ao2_ref(trunk_ref, 1);

	return trunk_ref;
}

/*! \brief Calculate the ring delay for a given ringing trunk on a station
 * \param station the station
 * \param ringing_trunk the trunk.  If NULL, the highest priority ringing trunk will be used
 * \return the number of ms left before the delay is complete, or INT_MAX if there is no delay
 */
static int sla_check_station_delay(struct sla_station *station, 
	struct sla_ringing_trunk *ringing_trunk)
{
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	unsigned int delay = UINT_MAX;
	int time_left, time_elapsed;

	if (!ringing_trunk)
		ringing_trunk = sla_choose_ringing_trunk(station, &trunk_ref, 0);
	else
		trunk_ref = sla_find_trunk_ref(station, ringing_trunk->trunk);

	if (!ringing_trunk || !trunk_ref)
		return delay;

	/* If this station has a ring delay specific to the highest priority
	 * ringing trunk, use that.  Otherwise, use the ring delay specified
	 * globally for the station. */
	delay = trunk_ref->ring_delay;
	if (!delay)
		delay = station->ring_delay;
	if (!delay)
		return INT_MAX;

	time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
	time_left = (delay * 1000) - time_elapsed;

	return time_left;
}

/*! \brief Ring stations based on current set of ringing trunks
 * \note Assumes that sla.lock is locked
 */
static void sla_ring_stations(void)
{
	struct sla_station_ref *station_ref;
	struct sla_ringing_trunk *ringing_trunk;

	/* Make sure that every station that uses at least one of the ringing
	 * trunks, is ringing. */
	AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_trunk->trunk->stations, station_ref, entry) {
			int time_left;

			/* Is this station already ringing? */
			if (sla_check_ringing_station(station_ref->station))
				continue;

			/* Is this station already in a call? */
			if (sla_check_inuse_station(station_ref->station))
				continue;

			/* Did we fail to dial this station earlier?  If so, has it been
 			 * a minute since we tried? */
			if (sla_check_failed_station(station_ref->station))
				continue;

			/* If this station already timed out while this trunk was ringing,
			 * do not dial it again for this ringing trunk. */
			if (sla_check_timed_out_station(ringing_trunk, station_ref->station))
				continue;

			/* Check for a ring delay in progress */
			time_left = sla_check_station_delay(station_ref->station, ringing_trunk);
			if (time_left != INT_MAX && time_left > 0)
				continue;

			/* It is time to make this station begin to ring.  Do it! */
			sla_ring_station(ringing_trunk, station_ref->station);
		}
	}
	/* Now, all of the stations that should be ringing, are ringing. */
}

static void sla_hangup_stations(void)
{
	struct sla_trunk_ref *trunk_ref;
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			struct sla_ringing_trunk *ringing_trunk;
			ast_mutex_lock(&sla.lock);
			AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
				if (trunk_ref->trunk == ringing_trunk->trunk)
					break;
			}
			ast_mutex_unlock(&sla.lock);
			if (ringing_trunk)
				break;
		}
		if (!trunk_ref) {
			AST_LIST_REMOVE_CURRENT(entry);
			ast_dial_join(ringing_station->station->dial);
			ast_dial_destroy(ringing_station->station->dial);
			ringing_station->station->dial = NULL;
			sla_ringing_station_destroy(ringing_station);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
}

static void sla_handle_ringing_trunk_event(void)
{
	ast_mutex_lock(&sla.lock);
	sla_ring_stations();
	ast_mutex_unlock(&sla.lock);

	/* Find stations that shouldn't be ringing anymore. */
	sla_hangup_stations();
}

static void sla_handle_hold_event(struct sla_event *event)
{
	ast_atomic_fetchadd_int((int *) &event->trunk_ref->trunk->hold_stations, 1);
	event->trunk_ref->state = SLA_TRUNK_STATE_ONHOLD_BYME;
	ast_devstate_changed(AST_DEVICE_ONHOLD, AST_DEVSTATE_CACHABLE, "SLA:%s_%s",
			     event->station->name, event->trunk_ref->trunk->name);
	sla_change_trunk_state(event->trunk_ref->trunk, SLA_TRUNK_STATE_ONHOLD, 
		INACTIVE_TRUNK_REFS, event->trunk_ref);

	if (event->trunk_ref->trunk->active_stations == 1) {
		/* The station putting it on hold is the only one on the call, so start
		 * Music on hold to the trunk. */
		event->trunk_ref->trunk->on_hold = 1;
		ast_indicate(event->trunk_ref->trunk->chan, AST_CONTROL_HOLD);
	}

	ast_softhangup(event->trunk_ref->chan, AST_SOFTHANGUP_DEV);
	event->trunk_ref->chan = NULL;
}

/*! \brief Process trunk ring timeouts
 * \note Called with sla.lock locked
 * \return non-zero if a change to the ringing trunks was made
 */
static int sla_calc_trunk_timeouts(unsigned int *timeout)
{
	struct sla_ringing_trunk *ringing_trunk;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
		int time_left, time_elapsed;
		if (!ringing_trunk->trunk->ring_timeout)
			continue;
		time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
		time_left = (ringing_trunk->trunk->ring_timeout * 1000) - time_elapsed;
		if (time_left <= 0) {
			pbx_builtin_setvar_helper(ringing_trunk->trunk->chan, "SLATRUNK_STATUS", "RINGTIMEOUT");
			AST_LIST_REMOVE_CURRENT(entry);
			sla_stop_ringing_trunk(ringing_trunk);
			res = 1;
			continue;
		}
		if (time_left < *timeout)
			*timeout = time_left;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

/*! \brief Process station ring timeouts
 * \note Called with sla.lock locked
 * \return non-zero if a change to the ringing stations was made
 */
static int sla_calc_station_timeouts(unsigned int *timeout)
{
	struct sla_ringing_trunk *ringing_trunk;
	struct sla_ringing_station *ringing_station;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		unsigned int ring_timeout = 0;
		int time_elapsed, time_left = INT_MAX, final_trunk_time_left = INT_MIN;
		struct sla_trunk_ref *trunk_ref;

		/* If there are any ring timeouts specified for a specific trunk
		 * on the station, then use the highest per-trunk ring timeout.
		 * Otherwise, use the ring timeout set for the entire station. */
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			struct sla_station_ref *station_ref;
			int trunk_time_elapsed, trunk_time_left;

			AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
				if (ringing_trunk->trunk == trunk_ref->trunk)
					break;
			}
			if (!ringing_trunk)
				continue;

			/* If there is a trunk that is ringing without a timeout, then the
			 * only timeout that could matter is a global station ring timeout. */
			if (!trunk_ref->ring_timeout)
				break;

			/* This trunk on this station is ringing and has a timeout.
			 * However, make sure this trunk isn't still ringing from a
			 * previous timeout.  If so, don't consider it. */
			AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, station_ref, entry) {
				if (station_ref->station == ringing_station->station)
					break;
			}
			if (station_ref)
				continue;

			trunk_time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
			trunk_time_left = (trunk_ref->ring_timeout * 1000) - trunk_time_elapsed;
			if (trunk_time_left > final_trunk_time_left)
				final_trunk_time_left = trunk_time_left;
		}

		/* No timeout was found for ringing trunks, and no timeout for the entire station */
		if (final_trunk_time_left == INT_MIN && !ringing_station->station->ring_timeout)
			continue;

		/* Compute how much time is left for a global station timeout */
		if (ringing_station->station->ring_timeout) {
			ring_timeout = ringing_station->station->ring_timeout;
			time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_station->ring_begin);
			time_left = (ring_timeout * 1000) - time_elapsed;
		}

		/* If the time left based on the per-trunk timeouts is smaller than the
		 * global station ring timeout, use that. */
		if (final_trunk_time_left > INT_MIN && final_trunk_time_left < time_left)
			time_left = final_trunk_time_left;

		/* If there is no time left, the station needs to stop ringing */
		if (time_left <= 0) {
			AST_LIST_REMOVE_CURRENT(entry);
			sla_stop_ringing_station(ringing_station, SLA_STATION_HANGUP_TIMEOUT);
			res = 1;
			continue;
		}

		/* There is still some time left for this station to ring, so save that
		 * timeout if it is the first event scheduled to occur */
		if (time_left < *timeout)
			*timeout = time_left;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

/*! \brief Calculate the ring delay for a station
 * \note Assumes sla.lock is locked
 */
static int sla_calc_station_delays(unsigned int *timeout)
{
	struct sla_station *station;
	int res = 0;
	struct ao2_iterator i;

	i = ao2_iterator_init(sla_stations, 0);
	for (; (station = ao2_iterator_next(&i)); ao2_ref(station, -1)) {
		struct sla_ringing_trunk *ringing_trunk;
		int time_left;

		/* Ignore stations already ringing */
		if (sla_check_ringing_station(station))
			continue;

		/* Ignore stations already on a call */
		if (sla_check_inuse_station(station))
			continue;

		/* Ignore stations that don't have one of their trunks ringing */
		if (!(ringing_trunk = sla_choose_ringing_trunk(station, NULL, 0)))
			continue;

		if ((time_left = sla_check_station_delay(station, ringing_trunk)) == INT_MAX)
			continue;

		/* If there is no time left, then the station needs to start ringing.
		 * Return non-zero so that an event will be queued up an event to 
		 * make that happen. */
		if (time_left <= 0) {
			res = 1;
			continue;
		}

		if (time_left < *timeout)
			*timeout = time_left;
	}
	ao2_iterator_destroy(&i);

	return res;
}

/*! \brief Calculate the time until the next known event
 *  \note Called with sla.lock locked */
static int sla_process_timers(struct timespec *ts)
{
	unsigned int timeout = UINT_MAX;
	struct timeval wait;
	unsigned int change_made = 0;

	/* Check for ring timeouts on ringing trunks */
	if (sla_calc_trunk_timeouts(&timeout))
		change_made = 1;

	/* Check for ring timeouts on ringing stations */
	if (sla_calc_station_timeouts(&timeout))
		change_made = 1;

	/* Check for station ring delays */
	if (sla_calc_station_delays(&timeout))
		change_made = 1;

	/* queue reprocessing of ringing trunks */
	if (change_made)
		sla_queue_event_nolock(SLA_EVENT_RINGING_TRUNK);

	/* No timeout */
	if (timeout == UINT_MAX)
		return 0;

	if (ts) {
		wait = ast_tvadd(ast_tvnow(), ast_samp2tv(timeout, 1000));
		ts->tv_sec = wait.tv_sec;
		ts->tv_nsec = wait.tv_usec * 1000;
	}

	return 1;
}

static void sla_event_destroy(struct sla_event *event)
{
	if (event->trunk_ref) {
		ao2_ref(event->trunk_ref, -1);
		event->trunk_ref = NULL;
	}

	if (event->station) {
		ao2_ref(event->station, -1);
		event->station = NULL;
	}

	ast_free(event);
}

static void *sla_thread(void *data)
{
	struct sla_failed_station *failed_station;
	struct sla_ringing_station *ringing_station;

	ast_mutex_lock(&sla.lock);

	while (!sla.stop) {
		struct sla_event *event;
		struct timespec ts = { 0, };
		unsigned int have_timeout = 0;

		if (AST_LIST_EMPTY(&sla.event_q)) {
			if ((have_timeout = sla_process_timers(&ts)))
				ast_cond_timedwait(&sla.cond, &sla.lock, &ts);
			else
				ast_cond_wait(&sla.cond, &sla.lock);
			if (sla.stop)
				break;
		}

		if (have_timeout)
			sla_process_timers(NULL);

		while ((event = AST_LIST_REMOVE_HEAD(&sla.event_q, entry))) {
			ast_mutex_unlock(&sla.lock);
			switch (event->type) {
			case SLA_EVENT_HOLD:
				sla_handle_hold_event(event);
				break;
			case SLA_EVENT_DIAL_STATE:
				sla_handle_dial_state_event();
				break;
			case SLA_EVENT_RINGING_TRUNK:
				sla_handle_ringing_trunk_event();
				break;
			}
			sla_event_destroy(event);
			ast_mutex_lock(&sla.lock);
		}
	}

	ast_mutex_unlock(&sla.lock);

	while ((ringing_station = AST_LIST_REMOVE_HEAD(&sla.ringing_stations, entry))) {
		sla_ringing_station_destroy(ringing_station);
	}

	while ((failed_station = AST_LIST_REMOVE_HEAD(&sla.failed_stations, entry))) {
		sla_failed_station_destroy(failed_station);
	}

	return NULL;
}

struct dial_trunk_args {
	struct sla_trunk_ref *trunk_ref;
	struct sla_station *station;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

static void *dial_trunk(void *data)
{
	struct dial_trunk_args *args = data;
	struct ast_dial *dial;
	char *tech, *tech_data;
	enum ast_dial_result dial_res;
	char conf_name[MAX_CONFNUM];
	struct ast_conference *conf;
	struct ast_flags64 conf_flags = { 0 };
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, args->trunk_ref, ao2_cleanup);
	RAII_VAR(struct sla_station *, station, args->station, ao2_cleanup);
	int caller_is_saved;
	struct ast_party_caller caller;
	int last_state = 0;
	int current_state = 0;

	if (!(dial = ast_dial_create())) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		return NULL;
	}

	tech_data = ast_strdupa(trunk_ref->trunk->device);
	tech = strsep(&tech_data, "/");
	if (ast_dial_append(dial, tech, tech_data, NULL) == -1) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	/* Do we need to save of the caller ID data? */
	caller_is_saved = 0;
	if (!sla.attempt_callerid) {
		caller_is_saved = 1;
		caller = *ast_channel_caller(trunk_ref->chan);
		ast_party_caller_init(ast_channel_caller(trunk_ref->chan));
	}

	dial_res = ast_dial_run(dial, trunk_ref->chan, 1);

	/* Restore saved caller ID */
	if (caller_is_saved) {
		ast_party_caller_free(ast_channel_caller(trunk_ref->chan));
		ast_channel_caller_set(trunk_ref->chan, &caller);
	}

	if (dial_res != AST_DIAL_RESULT_TRYING) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	for (;;) {
		unsigned int done = 0;
		switch ((dial_res = ast_dial_state(dial))) {
		case AST_DIAL_RESULT_ANSWERED:
			trunk_ref->trunk->chan = ast_dial_answered(dial);
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			done = 1;
			break;
		case AST_DIAL_RESULT_TRYING:
			current_state = AST_CONTROL_PROGRESS;
			break;
		case AST_DIAL_RESULT_RINGING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_PROCEEDING:
			current_state = AST_CONTROL_RINGING;
			break;
		}
		if (done)
			break;

		/* check that SLA station that originated trunk call is still alive */
		if (station && ast_device_state(station->device) == AST_DEVICE_NOT_INUSE) {
			ast_debug(3, "Originating station device %s no longer active\n", station->device);
			trunk_ref->trunk->chan = NULL;
			break;
		}

		/* If trunk line state changed, send indication back to originating SLA Station channel */
		if (current_state != last_state) {
			ast_debug(3, "Indicating State Change %d to channel %s\n", current_state, ast_channel_name(trunk_ref->chan));
			ast_indicate(trunk_ref->chan, current_state);
			last_state = current_state;
		}

		/* avoid tight loop... sleep for 1/10th second */
		ast_safe_sleep(trunk_ref->chan, 100);
	}

	if (!trunk_ref->trunk->chan) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		return NULL;
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag64(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_MARKEDUSER | 
		CONFFLAG_PASS_DTMF | CONFFLAG_SLA_TRUNK);
	conf = build_conf(conf_name, "", "", 1, 1, 1, trunk_ref->trunk->chan, NULL);

	ast_mutex_lock(args->cond_lock);
	ast_cond_signal(args->cond);
	ast_mutex_unlock(args->cond_lock);

	if (conf) {
		conf_run(trunk_ref->trunk->chan, conf, &conf_flags, NULL);
		dispose_conf(conf);
		conf = NULL;
	}

	/* If the trunk is going away, it is definitely now IDLE. */
	sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	trunk_ref->trunk->chan = NULL;
	trunk_ref->trunk->on_hold = 0;

	ast_dial_join(dial);
	ast_dial_destroy(dial);

	return NULL;
}

/*!
 * \brief For a given station, choose the highest priority idle trunk
 * \pre sla_station is locked
 */
static struct sla_trunk_ref *sla_choose_idle_trunk(const struct sla_station *station)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->state == SLA_TRUNK_STATE_IDLE) {
			ao2_ref(trunk_ref, 1);
			break;
		}
	}

	return trunk_ref;
}

static int sla_station_exec(struct ast_channel *chan, const char *data)
{
	char *station_name, *trunk_name;
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	char conf_name[MAX_CONFNUM];
	struct ast_flags64 conf_flags = { 0 };
	struct ast_conference *conf;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Invalid Arguments to SLAStation!\n");
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	trunk_name = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	if (ast_strlen_zero(station_name)) {
		ast_log(LOG_WARNING, "Invalid Arguments to SLAStation!\n");
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	station = sla_find_station(station_name);

	if (!station) {
		ast_log(LOG_WARNING, "Station '%s' not found!\n", station_name);
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	ao2_lock(station);
	if (!ast_strlen_zero(trunk_name)) {
		trunk_ref = sla_find_trunk_ref_byname(station, trunk_name);
	} else {
		trunk_ref = sla_choose_idle_trunk(station);
	}
	ao2_unlock(station);

	if (!trunk_ref) {
		if (ast_strlen_zero(trunk_name))
			ast_log(LOG_NOTICE, "No trunks available for call.\n");
		else {
			ast_log(LOG_NOTICE, "Can't join existing call on trunk "
				"'%s' due to access controls.\n", trunk_name);
		}
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "CONGESTION");
		return 0;
	}

	if (trunk_ref->state == SLA_TRUNK_STATE_ONHOLD_BYME) {
		if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->hold_stations) == 1)
			sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		else {
			trunk_ref->state = SLA_TRUNK_STATE_UP;
			ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE,
					     "SLA:%s_%s", station->name, trunk_ref->trunk->name);
		}
	} else if (trunk_ref->state == SLA_TRUNK_STATE_RINGING) {
		struct sla_ringing_trunk *ringing_trunk;

		ast_mutex_lock(&sla.lock);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk) {
				AST_LIST_REMOVE_CURRENT(entry);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		ast_mutex_unlock(&sla.lock);

		if (ringing_trunk) {
			answer_trunk_chan(ringing_trunk->trunk->chan);
			sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);

			sla_ringing_trunk_destroy(ringing_trunk);

			/* Queue up reprocessing ringing trunks, and then ringing stations again */
			sla_queue_event(SLA_EVENT_RINGING_TRUNK);
			sla_queue_event(SLA_EVENT_DIAL_STATE);
		}
	}

	trunk_ref->chan = chan;

	if (!trunk_ref->trunk->chan) {
		ast_mutex_t cond_lock;
		ast_cond_t cond;
		pthread_t dont_care;
		struct dial_trunk_args args = {
			.trunk_ref = trunk_ref,
			.station = station,
			.cond_lock = &cond_lock,
			.cond = &cond,
		};
		ao2_ref(trunk_ref, 1);
		ao2_ref(station, 1);
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		/* Create a thread to dial the trunk and dump it into the conference.
		 * However, we want to wait until the trunk has been dialed and the
		 * conference is created before continuing on here. */
		ast_autoservice_start(chan);
		ast_mutex_init(&cond_lock);
		ast_cond_init(&cond, NULL);
		ast_mutex_lock(&cond_lock);
		ast_pthread_create_detached_background(&dont_care, NULL, dial_trunk, &args);
		ast_cond_wait(&cond, &cond_lock);
		ast_mutex_unlock(&cond_lock);
		ast_mutex_destroy(&cond_lock);
		ast_cond_destroy(&cond);
		ast_autoservice_stop(chan);
		if (!trunk_ref->trunk->chan) {
			ast_debug(1, "Trunk didn't get created. chan: %lx\n", (unsigned long) trunk_ref->trunk->chan);
			pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "CONGESTION");
			sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
			trunk_ref->chan = NULL;
			return 0;
		}
	}

	if (ast_atomic_fetchadd_int((int *) &trunk_ref->trunk->active_stations, 1) == 0 &&
		trunk_ref->trunk->on_hold) {
		trunk_ref->trunk->on_hold = 0;
		ast_indicate(trunk_ref->trunk->chan, AST_CONTROL_UNHOLD);
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag64(&conf_flags,
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_STATION);
	ast_answer(chan);
	conf = build_conf(conf_name, "", "", 0, 0, 1, chan, NULL);
	if (conf) {
		conf_run(chan, conf, &conf_flags, NULL);
		dispose_conf(conf);
		conf = NULL;
	}
	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
		trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
		strncat(conf_name, ",K", sizeof(conf_name) - strlen(conf_name) - 1);
		admin_exec(NULL, conf_name);
		trunk_ref->trunk->hold_stations = 0;
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}
	
	pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "SUCCESS");

	return 0;
}

static void sla_trunk_ref_destructor(void *obj)
{
	struct sla_trunk_ref *trunk_ref = obj;

	if (trunk_ref->trunk) {
		ao2_ref(trunk_ref->trunk, -1);
		trunk_ref->trunk = NULL;
	}
}

static struct sla_trunk_ref *create_trunk_ref(struct sla_trunk *trunk)
{
	struct sla_trunk_ref *trunk_ref;

	if (!(trunk_ref = ao2_alloc(sizeof(*trunk_ref), sla_trunk_ref_destructor))) {
		return NULL;
	}

	ao2_ref(trunk, 1);
	trunk_ref->trunk = trunk;

	return trunk_ref;
}

static struct sla_ringing_trunk *queue_ringing_trunk(struct sla_trunk *trunk)
{
	struct sla_ringing_trunk *ringing_trunk;

	if (!(ringing_trunk = ast_calloc(1, sizeof(*ringing_trunk)))) {
		return NULL;
	}

	ao2_ref(trunk, 1);
	ringing_trunk->trunk = trunk;
	ringing_trunk->ring_begin = ast_tvnow();

	sla_change_trunk_state(trunk, SLA_TRUNK_STATE_RINGING, ALL_TRUNK_REFS, NULL);

	ast_mutex_lock(&sla.lock);
	AST_LIST_INSERT_HEAD(&sla.ringing_trunks, ringing_trunk, entry);
	ast_mutex_unlock(&sla.lock);

	sla_queue_event(SLA_EVENT_RINGING_TRUNK);

	return ringing_trunk;
}

static void sla_ringing_trunk_destroy(struct sla_ringing_trunk *ringing_trunk)
{
	if (ringing_trunk->trunk) {
		ao2_ref(ringing_trunk->trunk, -1);
		ringing_trunk->trunk = NULL;
	}

	ast_free(ringing_trunk);
}

enum {
	SLA_TRUNK_OPT_MOH = (1 << 0),
};

enum {
	SLA_TRUNK_OPT_ARG_MOH_CLASS = 0,
	SLA_TRUNK_OPT_ARG_ARRAY_SIZE = 1,
};

AST_APP_OPTIONS(sla_trunk_opts, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('M', SLA_TRUNK_OPT_MOH, SLA_TRUNK_OPT_ARG_MOH_CLASS),
END_OPTIONS );

static int sla_trunk_exec(struct ast_channel *chan, const char *data)
{
	char conf_name[MAX_CONFNUM];
	struct ast_conference *conf;
	struct ast_flags64 conf_flags = { 0 };
	RAII_VAR(struct sla_trunk *, trunk, NULL, ao2_cleanup);
	struct sla_ringing_trunk *ringing_trunk;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(trunk_name);
		AST_APP_ARG(options);
	);
	char *opts[SLA_TRUNK_OPT_ARG_ARRAY_SIZE] = { NULL, };
	struct ast_flags opt_flags = { 0 };
	char *parse;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "The SLATrunk application requires an argument, the trunk name\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc == 2) {
		if (ast_app_parse_options(sla_trunk_opts, &opt_flags, opts, args.options)) {
			ast_log(LOG_ERROR, "Error parsing options for SLATrunk\n");
			return -1;
		}
	}

	trunk = sla_find_trunk(args.trunk_name);

	if (!trunk) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' not found!\n", args.trunk_name);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	if (trunk->chan) {
		ast_log(LOG_ERROR, "Call came in on %s, but the trunk is already in use!\n",
			args.trunk_name);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	trunk->chan = chan;

	if (!(ringing_trunk = queue_ringing_trunk(trunk))) {
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", args.trunk_name);
	conf = build_conf(conf_name, "", "", 1, 1, 1, chan, NULL);
	if (!conf) {
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}
	ast_set_flag64(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_MARKEDUSER | CONFFLAG_PASS_DTMF | CONFFLAG_NO_AUDIO_UNTIL_UP);

	if (ast_test_flag(&opt_flags, SLA_TRUNK_OPT_MOH)) {
		ast_indicate(chan, -1);
		ast_set_flag64(&conf_flags, CONFFLAG_MOH);
	} else
		ast_indicate(chan, AST_CONTROL_RINGING);

	conf_run(chan, conf, &conf_flags, opts);
	dispose_conf(conf);
	conf = NULL;
	trunk->chan = NULL;
	trunk->on_hold = 0;

	sla_change_trunk_state(trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	if (!pbx_builtin_getvar_helper(chan, "SLATRUNK_STATUS"))
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "SUCCESS");

	/* Remove the entry from the list of ringing trunks if it is still there. */
	ast_mutex_lock(&sla.lock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
		if (ringing_trunk->trunk == trunk) {
			AST_LIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	ast_mutex_unlock(&sla.lock);
	if (ringing_trunk) {
		sla_ringing_trunk_destroy(ringing_trunk);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "UNANSWERED");
		/* Queue reprocessing of ringing trunks to make stations stop ringing
		 * that shouldn't be ringing after this trunk stopped. */
		sla_queue_event(SLA_EVENT_RINGING_TRUNK);
	}

	return 0;
}

static enum ast_device_state sla_state(const char *data)
{
	char *buf, *station_name, *trunk_name;
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	struct sla_trunk_ref *trunk_ref;
	enum ast_device_state res = AST_DEVICE_INVALID;

	trunk_name = buf = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	station = sla_find_station(station_name);
	if (station) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (!strcasecmp(trunk_name, trunk_ref->trunk->name)) {
				res = sla_state_to_devstate(trunk_ref->state);
				break;
			}
		}
		ao2_unlock(station);
	}

	if (res == AST_DEVICE_INVALID) {
		ast_log(LOG_ERROR, "Could not determine state for trunk %s on station %s!\n",
			trunk_name, station_name);
	}

	return res;
}

static int sla_trunk_release_refs(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj;
	struct sla_station_ref *station_ref;

	while ((station_ref = AST_LIST_REMOVE_HEAD(&trunk->stations, entry))) {
		ao2_ref(station_ref, -1);
	}

	return 0;
}

static int sla_station_release_refs(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj;
	struct sla_trunk_ref *trunk_ref;

	while ((trunk_ref = AST_LIST_REMOVE_HEAD(&station->trunks, entry))) {
		ao2_ref(trunk_ref, -1);
	}

	return 0;
}

static void sla_station_destructor(void *obj)
{
	struct sla_station *station = obj;

	ast_debug(1, "sla_station destructor for '%s'\n", station->name);

	if (!ast_strlen_zero(station->autocontext)) {
		struct sla_trunk_ref *trunk_ref;

		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_APP];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "SLA:%s", exten);
			ast_context_remove_extension(station->autocontext, exten, 
				1, sla_registrar);
			ast_context_remove_extension(station->autocontext, hint, 
				PRIORITY_HINT, sla_registrar);
		}
	}

	sla_station_release_refs(station, NULL, 0);

	ast_string_field_free_memory(station);
}

static int sla_trunk_hash(const void *obj, const int flags)
{
	const struct sla_trunk *trunk = obj;

	return ast_str_case_hash(trunk->name);
}

static int sla_trunk_cmp(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj, *trunk2 = arg;

	return !strcasecmp(trunk->name, trunk2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static int sla_station_hash(const void *obj, const int flags)
{
	const struct sla_station *station = obj;

	return ast_str_case_hash(station->name);
}

static int sla_station_cmp(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj, *station2 = arg;

	return !strcasecmp(station->name, station2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static void sla_destroy(void)
{
	if (sla.thread != AST_PTHREADT_NULL) {
		ast_mutex_lock(&sla.lock);
		sla.stop = 1;
		ast_cond_signal(&sla.cond);
		ast_mutex_unlock(&sla.lock);
		pthread_join(sla.thread, NULL);
	}

	/* Drop any created contexts from the dialplan */
	ast_context_destroy(NULL, sla_registrar);

	ast_mutex_destroy(&sla.lock);
	ast_cond_destroy(&sla.cond);

	ao2_callback(sla_trunks, 0, sla_trunk_release_refs, NULL);
	ao2_callback(sla_stations, 0, sla_station_release_refs, NULL);

	ao2_ref(sla_trunks, -1);
	sla_trunks = NULL;

	ao2_ref(sla_stations, -1);
	sla_stations = NULL;
}

static int sla_check_device(const char *device)
{
	char *tech, *tech_data;

	tech_data = ast_strdupa(device);
	tech = strsep(&tech_data, "/");

	if (ast_strlen_zero(tech) || ast_strlen_zero(tech_data))
		return -1;

	return 0;
}

static void sla_trunk_destructor(void *obj)
{
	struct sla_trunk *trunk = obj;

	ast_debug(1, "sla_trunk destructor for '%s'\n", trunk->name);

	if (!ast_strlen_zero(trunk->autocontext)) {
		ast_context_remove_extension(trunk->autocontext, "s", 1, sla_registrar);
	}

	sla_trunk_release_refs(trunk, NULL, 0);

	ast_string_field_free_memory(trunk);
}

static int sla_build_trunk(struct ast_config *cfg, const char *cat)
{
	RAII_VAR(struct sla_trunk *, trunk, NULL, ao2_cleanup);
	struct ast_variable *var;
	const char *dev;
	int existing_trunk = 0;

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' defined with no device!\n", cat);
		return -1;
	}

	if (sla_check_device(dev)) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' defined with invalid device '%s'!\n",
			cat, dev);
		return -1;
	}

	if ((trunk = sla_find_trunk(cat))) {
		trunk->mark = 0;
		existing_trunk = 1;
	} else if ((trunk = ao2_alloc(sizeof(*trunk), sla_trunk_destructor))) {
		if (ast_string_field_init(trunk, 32)) {
			return -1;
		}
		ast_string_field_set(trunk, name, cat);
	} else {
		return -1;
	}

	ao2_lock(trunk);

	ast_string_field_set(trunk, device, dev);

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "autocontext"))
			ast_string_field_set(trunk, autocontext, var->value);
		else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &trunk->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for trunk '%s'\n",
					var->value, trunk->name);
				trunk->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "barge"))
			trunk->barge_disabled = ast_false(var->value);
		else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private"))
				trunk->hold_access = SLA_HOLD_PRIVATE;
			else if (!strcasecmp(var->value, "open"))
				trunk->hold_access = SLA_HOLD_OPEN;
			else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on trunk %s\n",
					var->value, trunk->name);
			}
		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n",
				var->name, var->lineno, SLA_CONFIG_FILE);
		}
	}

	ao2_unlock(trunk);

	if (!ast_strlen_zero(trunk->autocontext)) {
		struct ast_context *context;
		context = ast_context_find_or_create(NULL, NULL, trunk->autocontext, sla_registrar);
		if (!context) {
			ast_log(LOG_ERROR, "Failed to automatically find or create "
				"context '%s' for SLA!\n", trunk->autocontext);
			return -1;
		}
		if (ast_add_extension2(context, 0 /* don't replace */, "s", 1,
			NULL, NULL, slatrunk_app, ast_strdup(trunk->name), ast_free_ptr, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension "
				"for trunk '%s'!\n", trunk->name);
			return -1;
		}
	}

	if (!existing_trunk) {
		ao2_link(sla_trunks, trunk);
	}

	return 0;
}

/*!
 * \internal
 * \pre station is not locked
 */
static void sla_add_trunk_to_station(struct sla_station *station, struct ast_variable *var)
{
	RAII_VAR(struct sla_trunk *, trunk, NULL, ao2_cleanup);
	struct sla_trunk_ref *trunk_ref = NULL;
	struct sla_station_ref *station_ref;
	char *trunk_name, *options, *cur;
	int existing_trunk_ref = 0;
	int existing_station_ref = 0;

	options = ast_strdupa(var->value);
	trunk_name = strsep(&options, ",");

	trunk = sla_find_trunk(trunk_name);
	if (!trunk) {
		ast_log(LOG_ERROR, "Trunk '%s' not found!\n", var->value);
		return;
	}

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk) {
			trunk_ref->mark = 0;
			existing_trunk_ref = 1;
			break;
		}
	}

	if (!trunk_ref && !(trunk_ref = create_trunk_ref(trunk))) {
		return;
	}

	trunk_ref->state = SLA_TRUNK_STATE_IDLE;

	while ((cur = strsep(&options, ","))) {
		char *name, *value = cur;
		name = strsep(&value, "=");
		if (!strcasecmp(name, "ringtimeout")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout value '%s' for "
					"trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_timeout = 0;
			}
		} else if (!strcasecmp(name, "ringdelay")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay value '%s' for "
					"trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_delay = 0;
			}
		} else {
			ast_log(LOG_WARNING, "Invalid option '%s' for "
				"trunk '%s' on station '%s'\n", name, trunk->name, station->name);
		}
	}

	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		if (station_ref->station == station) {
			station_ref->mark = 0;
			existing_station_ref = 1;
			break;
		}
	}

	if (!station_ref && !(station_ref = sla_create_station_ref(station))) {
		if (!existing_trunk_ref) {
			ao2_ref(trunk_ref, -1);
		} else {
			trunk_ref->mark = 1;
		}
		return;
	}

	if (!existing_station_ref) {
		ao2_lock(trunk);
		AST_LIST_INSERT_TAIL(&trunk->stations, station_ref, entry);
		ast_atomic_fetchadd_int((int *) &trunk->num_stations, 1);
		ao2_unlock(trunk);
	}

	if (!existing_trunk_ref) {
		ao2_lock(station);
		AST_LIST_INSERT_TAIL(&station->trunks, trunk_ref, entry);
		ao2_unlock(station);
	}
}

static int sla_build_station(struct ast_config *cfg, const char *cat)
{
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	struct ast_variable *var;
	const char *dev;
	int existing_station = 0;

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "SLA Station '%s' defined with no device!\n", cat);
		return -1;
	}

	if ((station = sla_find_station(cat))) {
		station->mark = 0;
		existing_station = 1;
	} else if ((station = ao2_alloc(sizeof(*station), sla_station_destructor))) {
		if (ast_string_field_init(station, 32)) {
			return -1;
		}
		ast_string_field_set(station, name, cat);
	} else {
		return -1;
	}

	ao2_lock(station);

	ast_string_field_set(station, device, dev);

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "trunk")) {
			ao2_unlock(station);
			sla_add_trunk_to_station(station, var);
			ao2_lock(station);
		} else if (!strcasecmp(var->name, "autocontext")) {
			ast_string_field_set(station, autocontext, var->value);
		} else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &station->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for station '%s'\n",
					var->value, station->name);
				station->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "ringdelay")) {
			if (sscanf(var->value, "%30u", &station->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay '%s' specified for station '%s'\n",
					var->value, station->name);
				station->ring_delay = 0;
			}
		} else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private"))
				station->hold_access = SLA_HOLD_PRIVATE;
			else if (!strcasecmp(var->value, "open"))
				station->hold_access = SLA_HOLD_OPEN;
			else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on station %s\n",
					var->value, station->name);
			}

		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n",
				var->name, var->lineno, SLA_CONFIG_FILE);
		}
	}

	ao2_unlock(station);

	if (!ast_strlen_zero(station->autocontext)) {
		struct ast_context *context;
		struct sla_trunk_ref *trunk_ref;
		context = ast_context_find_or_create(NULL, NULL, station->autocontext, sla_registrar);
		if (!context) {
			ast_log(LOG_ERROR, "Failed to automatically find or create "
				"context '%s' for SLA!\n", station->autocontext);
			return -1;
		}
		/* The extension for when the handset goes off-hook.
		 * exten => station1,1,SLAStation(station1) */
		if (ast_add_extension2(context, 0 /* don't replace */, station->name, 1,
			NULL, NULL, slastation_app, ast_strdup(station->name), ast_free_ptr, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension "
				"for trunk '%s'!\n", station->name);
			return -1;
		}
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_APP];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "SLA:%s", exten);
			/* Extension for this line button 
			 * exten => station1_line1,1,SLAStation(station1_line1) */
			if (ast_add_extension2(context, 0 /* don't replace */, exten, 1,
				NULL, NULL, slastation_app, ast_strdup(exten), ast_free_ptr, sla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create extension "
					"for trunk '%s'!\n", station->name);
				return -1;
			}
			/* Hint for this line button 
			 * exten => station1_line1,hint,SLA:station1_line1 */
			if (ast_add_extension2(context, 0 /* don't replace */, exten, PRIORITY_HINT,
				NULL, NULL, hint, NULL, NULL, sla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create hint "
					"for trunk '%s'!\n", station->name);
				return -1;
			}
		}
	}

	if (!existing_station) {
		ao2_link(sla_stations, station);
	}

	return 0;
}

static int sla_trunk_mark(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj;
	struct sla_station_ref *station_ref;

	ao2_lock(trunk);

	trunk->mark = 1;

	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		station_ref->mark = 1;
	}

	ao2_unlock(trunk);

	return 0;
}

static int sla_station_mark(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj;
	struct sla_trunk_ref *trunk_ref;

	ao2_lock(station);

	station->mark = 1;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		trunk_ref->mark = 1;
	}

	ao2_unlock(station);

	return 0;
}

static int sla_trunk_is_marked(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj;

	ao2_lock(trunk);

	if (trunk->mark) {
		/* Only remove all of the station references if the trunk itself is going away */
		sla_trunk_release_refs(trunk, NULL, 0);
	} else {
		struct sla_station_ref *station_ref;

		/* Otherwise only remove references to stations no longer in the config */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&trunk->stations, station_ref, entry) {
			if (!station_ref->mark) {
				continue;
			}
			AST_LIST_REMOVE_CURRENT(entry);
			ao2_ref(station_ref, -1);
		}
		AST_LIST_TRAVERSE_SAFE_END
	}

	ao2_unlock(trunk);

	return trunk->mark ? CMP_MATCH : 0;
}

static int sla_station_is_marked(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj;

	ao2_lock(station);

	if (station->mark) {
		/* Only remove all of the trunk references if the station itself is going away */
		sla_station_release_refs(station, NULL, 0);
	} else {
		struct sla_trunk_ref *trunk_ref;

		/* Otherwise only remove references to trunks no longer in the config */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&station->trunks, trunk_ref, entry) {
			if (!trunk_ref->mark) {
				continue;
			}
			AST_LIST_REMOVE_CURRENT(entry);
			ao2_ref(trunk_ref, -1);
		}
		AST_LIST_TRAVERSE_SAFE_END
	}

	ao2_unlock(station);

	return station->mark ? CMP_MATCH : 0;
}

static int sla_in_use(void)
{
	return ao2_container_count(sla_trunks) || ao2_container_count(sla_stations);
}

static int sla_load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *cat = NULL;
	int res = 0;
	const char *val;

	if (!reload) {
		ast_mutex_init(&sla.lock);
		ast_cond_init(&sla.cond, NULL);
		sla_trunks = ao2_container_alloc(1, sla_trunk_hash, sla_trunk_cmp);
		sla_stations = ao2_container_alloc(1, sla_station_hash, sla_station_cmp);
	}

	if (!(cfg = ast_config_load(SLA_CONFIG_FILE, config_flags))) {
		return 0; /* Treat no config as normal */
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " SLA_CONFIG_FILE " is in an invalid format.  Aborting.\n");
		return 0;
	}

	if (reload) {
		ao2_callback(sla_trunks, 0, sla_trunk_mark, NULL);
		ao2_callback(sla_stations, 0, sla_station_mark, NULL);
	}

	if ((val = ast_variable_retrieve(cfg, "general", "attemptcallerid")))
		sla.attempt_callerid = ast_true(val);

	while ((cat = ast_category_browse(cfg, cat)) && !res) {
		const char *type;
		if (!strcasecmp(cat, "general"))
			continue;
		if (!(type = ast_variable_retrieve(cfg, cat, "type"))) {
			ast_log(LOG_WARNING, "Invalid entry in %s defined with no type!\n",
				SLA_CONFIG_FILE);
			continue;
		}
		if (!strcasecmp(type, "trunk"))
			res = sla_build_trunk(cfg, cat);
		else if (!strcasecmp(type, "station"))
			res = sla_build_station(cfg, cat);
		else {
			ast_log(LOG_WARNING, "Entry in %s defined with invalid type '%s'!\n",
				SLA_CONFIG_FILE, type);
		}
	}

	ast_config_destroy(cfg);

	if (reload) {
		ao2_callback(sla_trunks, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, sla_trunk_is_marked, NULL);
		ao2_callback(sla_stations, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, sla_station_is_marked, NULL);
	}

	/* Start SLA event processing thread once SLA has been configured. */
	if (sla.thread == AST_PTHREADT_NULL && sla_in_use()) {
		ast_pthread_create(&sla.thread, NULL, sla_thread, NULL);
	}

	return res;
}

static int acf_meetme_info_eval(const char *keyword, const struct ast_conference *conf)
{
	if (!strcasecmp("lock", keyword)) {
		return conf->locked;
	} else if (!strcasecmp("parties", keyword)) {
		return conf->users;
	} else if (!strcasecmp("activity", keyword)) {
		time_t now;
		now = time(NULL);
		return (now - conf->start);
	} else if (!strcasecmp("dynamic", keyword)) {
		return conf->isdynamic;
	} else {
		return -1;
	}

}

static int acf_meetme_info(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_conference *conf;
	char *parse;
	int result = -2; /* only non-negative numbers valid, -1 is used elsewhere */
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(keyword);
		AST_APP_ARG(confno);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Syntax: MEETME_INFO() requires two arguments\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.keyword)) {
		ast_log(LOG_ERROR, "Syntax: MEETME_INFO() requires a keyword\n");
		return -1;
	}

	if (ast_strlen_zero(args.confno)) {
		ast_log(LOG_ERROR, "Syntax: MEETME_INFO() requires a conference number\n");
		return -1;
	}

	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if (!strcmp(args.confno, conf->confno)) {
			result = acf_meetme_info_eval(args.keyword, conf);
			break;
		}
	}
	AST_LIST_UNLOCK(&confs);

	if (result > -1) {
		snprintf(buf, len, "%d", result);
	} else if (result == -1) {
		ast_log(LOG_NOTICE, "Error: invalid keyword: '%s'\n", args.keyword);
		snprintf(buf, len, "0");
	} else if (result == -2) {
		ast_log(LOG_NOTICE, "Error: conference (%s) not found\n", args.confno); 
		snprintf(buf, len, "0");
	}

	return 0;
}


static struct ast_custom_function meetme_info_acf = {
	.name = "MEETME_INFO",
	.read = acf_meetme_info,
};


static int load_config(int reload)
{
	load_config_meetme(reload);
	return sla_load_config(reload);
}

#define MEETME_DATA_EXPORT(MEMBER)					\
	MEMBER(ast_conference, confno, AST_DATA_STRING)			\
	MEMBER(ast_conference, dahdiconf, AST_DATA_INTEGER)		\
	MEMBER(ast_conference, users, AST_DATA_INTEGER)			\
	MEMBER(ast_conference, markedusers, AST_DATA_INTEGER)		\
	MEMBER(ast_conference, maxusers, AST_DATA_INTEGER)		\
	MEMBER(ast_conference, isdynamic, AST_DATA_BOOLEAN)		\
	MEMBER(ast_conference, locked, AST_DATA_BOOLEAN)		\
	MEMBER(ast_conference, recordingfilename, AST_DATA_STRING)	\
	MEMBER(ast_conference, recordingformat, AST_DATA_STRING)	\
	MEMBER(ast_conference, pin, AST_DATA_PASSWORD)			\
	MEMBER(ast_conference, pinadmin, AST_DATA_PASSWORD)		\
	MEMBER(ast_conference, start, AST_DATA_TIMESTAMP)		\
	MEMBER(ast_conference, endtime, AST_DATA_TIMESTAMP)

AST_DATA_STRUCTURE(ast_conference, MEETME_DATA_EXPORT);

#define MEETME_USER_DATA_EXPORT(MEMBER)					\
	MEMBER(ast_conf_user, user_no, AST_DATA_INTEGER)		\
	MEMBER(ast_conf_user, talking, AST_DATA_BOOLEAN)		\
	MEMBER(ast_conf_user, dahdichannel, AST_DATA_BOOLEAN)		\
	MEMBER(ast_conf_user, jointime, AST_DATA_TIMESTAMP)		\
	MEMBER(ast_conf_user, kicktime, AST_DATA_TIMESTAMP)		\
	MEMBER(ast_conf_user, timelimit, AST_DATA_MILLISECONDS)		\
	MEMBER(ast_conf_user, play_warning, AST_DATA_MILLISECONDS)	\
	MEMBER(ast_conf_user, warning_freq, AST_DATA_MILLISECONDS)

AST_DATA_STRUCTURE(ast_conf_user, MEETME_USER_DATA_EXPORT);

static int user_add_provider_cb(void *obj, void *arg, int flags)
{
	struct ast_data *data_meetme_user;
	struct ast_data *data_meetme_user_channel;
	struct ast_data *data_meetme_user_volume;

	struct ast_conf_user *user = obj;
	struct ast_data *data_meetme_users = arg;

	data_meetme_user = ast_data_add_node(data_meetme_users, "user");
	if (!data_meetme_user) {
		return 0;
	}
	/* user structure */
	ast_data_add_structure(ast_conf_user, data_meetme_user, user);

	/* user's channel */
	data_meetme_user_channel = ast_data_add_node(data_meetme_user, "channel");
	if (!data_meetme_user_channel) {
		return 0;
	}

	ast_channel_data_add_structure(data_meetme_user_channel, user->chan, 1);

	/* volume structure */
	data_meetme_user_volume = ast_data_add_node(data_meetme_user, "listen-volume");
	if (!data_meetme_user_volume) {
		return 0;
	}
	ast_data_add_int(data_meetme_user_volume, "desired", user->listen.desired);
	ast_data_add_int(data_meetme_user_volume, "actual", user->listen.actual);

	data_meetme_user_volume = ast_data_add_node(data_meetme_user, "talk-volume");
	if (!data_meetme_user_volume) {
		return 0;
	}
	ast_data_add_int(data_meetme_user_volume, "desired", user->talk.desired);
	ast_data_add_int(data_meetme_user_volume, "actual", user->talk.actual);

	return 0;
}

/*!
 * \internal
 * \brief Implements the meetme data provider.
 */
static int meetme_data_provider_get(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct ast_conference *cnf;
	struct ast_data *data_meetme, *data_meetme_users;

	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		data_meetme = ast_data_add_node(data_root, "meetme");
		if (!data_meetme) {
			continue;
		}

		ast_data_add_structure(ast_conference, data_meetme, cnf);

		if (ao2_container_count(cnf->usercontainer)) {
			data_meetme_users = ast_data_add_node(data_meetme, "users");
			if (!data_meetme_users) {
				ast_data_remove_node(data_root, data_meetme);
				continue;
			}

			ao2_callback(cnf->usercontainer, OBJ_NODATA, user_add_provider_cb, data_meetme_users); 
		}

		if (!ast_data_search_match(search, data_meetme)) {
			ast_data_remove_node(data_root, data_meetme);
		}
	}
	AST_LIST_UNLOCK(&confs);

	return 0;
}

static const struct ast_data_handler meetme_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = meetme_data_provider_get
};

static const struct ast_data_entry meetme_data_providers[] = {
	AST_DATA_ENTRY("asterisk/application/meetme/list", &meetme_data_provider),
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_meetme_data_provider)
{
	struct ast_channel *chan;
	struct ast_conference *cnf;
	struct ast_data *node;
	struct ast_data_query query = {
		.path = "/asterisk/application/meetme/list",
		.search = "list/meetme/confno=9898"
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "meetme_get_data_test";
		info->category = "/main/data/app_meetme/list/";
		info->summary = "Meetme data provider unit test";
		info->description =
			"Tests whether the Meetme data provider implementation works as expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	chan = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "MeetMeTest");
	if (!chan) {
		ast_test_status_update(test, "Channel allocation failed\n");
		return AST_TEST_FAIL;
	}

	ast_channel_unlock(chan);

	cnf = build_conf("9898", "", "1234", 1, 1, 1, chan, test);
	if (!cnf) {
		ast_test_status_update(test, "Build of test conference 9898 failed\n");
		ast_hangup(chan);
		return AST_TEST_FAIL;
	}

	node = ast_data_get(&query);
	if (!node) {
		ast_test_status_update(test, "Data query for test conference 9898 failed\n");
		dispose_conf(cnf);
		ast_hangup(chan);
		return AST_TEST_FAIL;
	}

	if (strcmp(ast_data_retrieve_string(node, "meetme/confno"), "9898")) {
		ast_test_status_update(test, "Query returned the wrong conference\n");
		dispose_conf(cnf);
		ast_hangup(chan);
		ast_data_free(node);
		return AST_TEST_FAIL;
	}

	ast_data_free(node);
	dispose_conf(cnf);
	ast_hangup(chan);

	return AST_TEST_PASS;
}
#endif

static void unload_module(void)
{
	ast_data_unregister(NULL);

	sla_destroy();

	ast_unload_realtime("meetme");

	meetme_stasis_cleanup();
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

	res |= load_config(0);

	res |= meetme_stasis_init();

	ast_cli_register_multiple(cli_meetme, ARRAY_LEN(cli_meetme));
	res |= ast_manager_register_xml("MeetmeMute", EVENT_FLAG_CALL, action_meetmemute);
	res |= ast_manager_register_xml("MeetmeUnmute", EVENT_FLAG_CALL, action_meetmeunmute);
	res |= ast_manager_register_xml("MeetmeList", EVENT_FLAG_REPORTING, action_meetmelist);
	res |= ast_manager_register_xml("MeetmeListRooms", EVENT_FLAG_REPORTING, action_meetmelistrooms);
	res |= ast_register_application_xml(app4, channel_admin_exec);
	res |= ast_register_application_xml(app3, admin_exec);
	res |= ast_register_application_xml(app2, count_exec);
	res |= ast_register_application_xml(app, conf_exec);
	res |= ast_register_application_xml(slastation_app, sla_station_exec);
	res |= ast_register_application_xml(slatrunk_app, sla_trunk_exec);

#ifdef TEST_FRAMEWORK
	AST_TEST_REGISTER(test_meetme_data_provider);
#endif
	ast_data_register_multiple(meetme_data_providers, ARRAY_LEN(meetme_data_providers));

	res |= ast_devstate_prov_add("Meetme", meetmestate);
	res |= ast_devstate_prov_add("SLA", sla_state);

	res |= ast_custom_function_register(&meetme_info_acf);
	ast_realtime_require_field("meetme", "confno", RQ_UINTEGER2, 3, "members", RQ_UINTEGER1, 3, NULL);

	return res;
}

static int reload_module(void)
{
	ast_unload_realtime("meetme");
	return load_config(1);
}

AST_MODULE_INFO_RELOADABLE(ASTERISK_GPL_KEY, "MeetMe conference bridge");
