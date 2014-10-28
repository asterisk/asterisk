/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief The Asterisk Management Interface - AMI
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * OpenSSL http://www.openssl.org - for AMI/SSL
 *
 * At the moment this file contains a number of functions, namely:
 *
 * - data structures storing AMI state
 * - AMI-related API functions, used by internal asterisk components
 * - handlers for AMI-related CLI functions
 * - handlers for AMI functions (available through the AMI socket)
 * - the code for the main AMI listener thread and individual session threads
 * - the http handlers invoked for AMI-over-HTTP by the threads in main/http.c
 *
 * \ref amiconf
 */

/*! \li \ref manager.c uses the configuration file \ref manager.conf and \ref users.conf
 * \addtogroup configuration_file
 */

/*! \page manager.conf manager.conf
 * \verbinclude manager.conf.sample
 */

/*! \page users.conf users.conf
 * \verbinclude users.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use various ast_config_AST_* */
#include <ctype.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/manager.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/callerid.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/md5.h"
#include "asterisk/acl.h"
#include "asterisk/utils.h"
#include "asterisk/tcptls.h"
#include "asterisk/http.h"
#include "asterisk/ast_version.h"
#include "asterisk/threadstorage.h"
#include "asterisk/linkedlists.h"
#include "asterisk/term.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/security_events.h"
#include "asterisk/aoc.h"
#include "asterisk/strings.h"
#include "asterisk/stringfields.h"
#include "asterisk/presencestate.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/test.h"
#include "asterisk/json.h"
#include "asterisk/bridge.h"
#include "asterisk/features_config.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/translate.h"

/*** DOCUMENTATION
	<manager name="Ping" language="en_US">
		<synopsis>
			Keepalive command.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>A 'Ping' action will ellicit a 'Pong' response. Used to keep the
			manager connection open.</para>
		</description>
	</manager>
	<manager name="Events" language="en_US">
		<synopsis>
			Control Event Flow.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="EventMask" required="true">
				<enumlist>
					<enum name="on">
						<para>If all events should be sent.</para>
					</enum>
					<enum name="off">
						<para>If no events should be sent.</para>
					</enum>
					<enum name="system,call,log,...">
						<para>To select which flags events should have to be sent.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Enable/Disable sending of events to this manager client.</para>
		</description>
	</manager>
	<manager name="Logoff" language="en_US">
		<synopsis>
			Logoff Manager.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Logoff the current manager session.</para>
		</description>
	</manager>
	<manager name="Login" language="en_US">
		<synopsis>
			Login Manager.
		</synopsis>
		<syntax>
			<parameter name="ActionID">
				<para>ActionID for this transaction. Will be returned.</para>
			</parameter>
			<parameter name="Username" required="true">
				<para>Username to login with as specified in manager.conf.</para>
			</parameter>
			<parameter name="Secret">
				<para>Secret to login with as specified in manager.conf.</para>
			</parameter>
		</syntax>
		<description>
			<para>Login Manager.</para>
		</description>
	</manager>
	<manager name="Challenge" language="en_US">
		<synopsis>
			Generate Challenge for MD5 Auth.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="AuthType" required="true">
				<para>Digest algorithm to use in the challenge. Valid values are:</para>
				<enumlist>
					<enum name="MD5" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Generate a challenge for MD5 authentication.</para>
		</description>
	</manager>
	<manager name="Hangup" language="en_US">
		<synopsis>
			Hangup channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>The exact channel name to be hungup, or to use a regular expression, set this parameter to: /regex/</para>
				<para>Example exact channel: SIP/provider-0000012a</para>
				<para>Example regular expression: /^SIP/provider-.*$/</para>
			</parameter>
			<parameter name="Cause">
				<para>Numeric hangup cause.</para>
			</parameter>
		</syntax>
		<description>
			<para>Hangup a channel.</para>
		</description>
	</manager>
	<manager name="Status" language="en_US">
		<synopsis>
			List channel status.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="false">
				<para>The name of the channel to query for status.</para>
			</parameter>
			<parameter name="Variables">
				<para>Comma <literal>,</literal> separated list of variable to include.</para>
			</parameter>
		</syntax>
		<description>
			<para>Will return the status information of each channel along with the
			value for the specified channel variables.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="Status">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised in response to a Status command.</synopsis>
			<syntax>
				<parameter name="ActionID" required="false"/>
				<channel_snapshot/>
				<parameter name="Type">
					<para>Type of channel</para>
				</parameter>
				<parameter name="DNID">
					<para>Dialed number identifier</para>
				</parameter>
				<parameter name="TimeToHangup">
					<para>Absolute lifetime of the channel</para>
				</parameter>
				<parameter name="BridgeID">
					<para>Identifier of the bridge the channel is in, may be empty if not in one</para>
				</parameter>
				<parameter name="Linkedid">
				</parameter>
				<parameter name="Application">
					<para>Application currently executing on the channel</para>
				</parameter>
				<parameter name="Data">
					<para>Data given to the currently executing channel</para>
				</parameter>
				<parameter name="Nativeformats">
					<para>Media formats the connected party is willing to send or receive</para>
				</parameter>
				<parameter name="Readformat">
					<para>Media formats that frames from the channel are received in</para>
				</parameter>
				<parameter name="Readtrans">
					<para>Translation path for media received in native formats</para>
				</parameter>
				<parameter name="Writeformat">
					<para>Media formats that frames to the channel are accepted in</para>
				</parameter>
				<parameter name="Writetrans">
					<para>Translation path for media sent to the connected party</para>
				</parameter>
				<parameter name="Callgroup">
					<para>Configured call group on the channel</para>
				</parameter>
				<parameter name="Pickupgroup">
					<para>Configured pickup group on the channel</para>
				</parameter>
				<parameter name="Seconds">
					<para>Number of seconds the channel has been active</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="manager">Status</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<manager name="Setvar" language="en_US">
		<synopsis>
			Sets a channel variable or function value.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel">
				<para>Channel to set variable for.</para>
			</parameter>
			<parameter name="Variable" required="true">
				<para>Variable name, function or expression.</para>
			</parameter>
			<parameter name="Value" required="true">
				<para>Variable or function value.</para>
			</parameter>
		</syntax>
		<description>
			<para>This command can be used to set the value of channel variables or dialplan
			functions.</para>
			<note>
				<para>If a channel name is not provided then the variable is considered global.</para>
			</note>
		</description>
	</manager>
	<manager name="Getvar" language="en_US">
		<synopsis>
			Gets a channel variable or function value.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel">
				<para>Channel to read variable from.</para>
			</parameter>
			<parameter name="Variable" required="true">
				<para>Variable name, function or expression.</para>
			</parameter>
		</syntax>
		<description>
			<para>Get the value of a channel variable or function return.</para>
			<note>
				<para>If a channel name is not provided then the variable is considered global.</para>
			</note>
		</description>
	</manager>
	<manager name="GetConfig" language="en_US">
		<synopsis>
			Retrieve configuration.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Filename" required="true">
				<para>Configuration filename (e.g. <filename>foo.conf</filename>).</para>
			</parameter>
			<parameter name="Category">
				<para>Category in configuration file.</para>
			</parameter>
			<parameter name="Filter">
				<para>A comma separated list of
				<replaceable>name_regex</replaceable>=<replaceable>value_regex</replaceable>
				expressions which will cause only categories whose variables match all expressions
				to be considered.  The special variable name <literal>TEMPLATES</literal>
				can be used to control whether templates are included.  Passing
				<literal>include</literal> as the value will include templates
				along with normal categories. Passing
				<literal>restrict</literal> as the value will restrict the operation to
				ONLY templates.  Not specifying a <literal>TEMPLATES</literal> expression
				results in the default behavior which is to not include templates.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action will dump the contents of a configuration
			file by category and contents or optionally by specified category only.
			In the case where a category name is non-unique, a filter may be specified
			to match only categories with matching variable values.</para>
		</description>
	</manager>
	<manager name="GetConfigJSON" language="en_US">
		<synopsis>
			Retrieve configuration (JSON format).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Filename" required="true">
				<para>Configuration filename (e.g. <filename>foo.conf</filename>).</para>
			</parameter>
			<parameter name="Category">
				<para>Category in configuration file.</para>
			</parameter>
			<parameter name="Filter">
				<xi:include xpointer="xpointer(/docs/manager[@name='GetConfig']/syntax/parameter[@name='Filter']/para[1])" />
			</parameter>
		</syntax>
		<description>
			<para>This action will dump the contents of a configuration file by category
			and contents in JSON format or optionally by specified category only.
			This only makes sense to be used using rawman over the HTTP interface.
			In the case where a category name is non-unique, a filter may be specified
			to match only categories with matching variable values.</para>
		</description>
	</manager>
	<manager name="UpdateConfig" language="en_US">
		<synopsis>
			Update basic configuration.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="SrcFilename" required="true">
				<para>Configuration filename to read (e.g. <filename>foo.conf</filename>).</para>
			</parameter>
			<parameter name="DstFilename" required="true">
				<para>Configuration filename to write (e.g. <filename>foo.conf</filename>)</para>
			</parameter>
			<parameter name="Reload">
				<para>Whether or not a reload should take place (or name of specific module).</para>
			</parameter>
			<parameter name="Action-000000">
				<para>Action to take.</para>
				<para>0's represent 6 digit number beginning with 000000.</para>
				<enumlist>
					<enum name="NewCat" />
					<enum name="RenameCat" />
					<enum name="DelCat" />
					<enum name="EmptyCat" />
					<enum name="Update" />
					<enum name="Delete" />
					<enum name="Append" />
					<enum name="Insert" />
				</enumlist>
			</parameter>
			<parameter name="Cat-000000">
				<para>Category to operate on.</para>
				<xi:include xpointer="xpointer(/docs/manager[@name='UpdateConfig']/syntax/parameter[@name='Action-000000']/para[2])" />
			</parameter>
			<parameter name="Var-000000">
				<para>Variable to work on.</para>
				<xi:include xpointer="xpointer(/docs/manager[@name='UpdateConfig']/syntax/parameter[@name='Action-000000']/para[2])" />
			</parameter>
			<parameter name="Value-000000">
				<para>Value to work on.</para>
				<xi:include xpointer="xpointer(/docs/manager[@name='UpdateConfig']/syntax/parameter[@name='Action-000000']/para[2])" />
			</parameter>
			<parameter name="Match-000000">
				<para>Extra match required to match line.</para>
				<xi:include xpointer="xpointer(/docs/manager[@name='UpdateConfig']/syntax/parameter[@name='Action-000000']/para[2])" />
			</parameter>
			<parameter name="Line-000000">
				<para>Line in category to operate on (used with delete and insert actions).</para>
				<xi:include xpointer="xpointer(/docs/manager[@name='UpdateConfig']/syntax/parameter[@name='Action-000000']/para[2])" />
			</parameter>
			<parameter name="Options-000000">
				<para>A comma separated list of action-specific options.</para>
					<enumlist>
						<enum name="NewCat"><para>One or more of the following... </para>
							<enumlist>
								<enum name="allowdups"><para>Allow duplicate category names.</para></enum>
								<enum name="template"><para>This category is a template.</para></enum>
								<enum name="inherit=&quot;template[,...]&quot;"><para>Templates from which to inherit.</para></enum>
							</enumlist>
						</enum>
					</enumlist>
					<para> </para>
						<para>The following actions share the same options...</para>
					<enumlist>
						<enum name="RenameCat"/>
						<enum name="DelCat"/>
						<enum name="EmptyCat"/>
						<enum name="Update"/>
						<enum name="Delete"/>
						<enum name="Append"/>
						<enum name="Insert"><para> </para>
							<enumlist>
								<enum name="catfilter=&quot;&lt;expression&gt;[,...]&quot;"><para> </para>
									<xi:include xpointer="xpointer(/docs/manager[@name='GetConfig']/syntax/parameter[@name='Filter']/para[1])" />
									<para><literal>catfilter</literal> is most useful when a file
									contains multiple categories with the same name and you wish to
									operate on specific ones instead of all of them.</para>
								</enum>
							</enumlist>
						</enum>
					</enumlist>
				<xi:include xpointer="xpointer(/docs/manager[@name='UpdateConfig']/syntax/parameter[@name='Action-000000']/para[2])" />
			</parameter>
		</syntax>
		<description>
			<para>This action will modify, create, or delete configuration elements
			in Asterisk configuration files.</para>
		</description>
	</manager>
	<manager name="CreateConfig" language="en_US">
		<synopsis>
			Creates an empty file in the configuration directory.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Filename" required="true">
				<para>The configuration filename to create (e.g. <filename>foo.conf</filename>).</para>
			</parameter>
		</syntax>
		<description>
			<para>This action will create an empty file in the configuration
			directory. This action is intended to be used before an UpdateConfig
			action.</para>
		</description>
	</manager>
	<manager name="ListCategories" language="en_US">
		<synopsis>
			List categories in configuration file.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Filename" required="true">
				<para>Configuration filename (e.g. <filename>foo.conf</filename>).</para>
			</parameter>
		</syntax>
		<description>
			<para>This action will dump the categories in a given file.</para>
		</description>
	</manager>
	<manager name="Redirect" language="en_US">
		<synopsis>
			Redirect (transfer) a call.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel to redirect.</para>
			</parameter>
			<parameter name="ExtraChannel">
				<para>Second call leg to transfer (optional).</para>
			</parameter>
			<parameter name="Exten" required="true">
				<para>Extension to transfer to.</para>
			</parameter>
			<parameter name="ExtraExten">
				<para>Extension to transfer extrachannel to (optional).</para>
			</parameter>
			<parameter name="Context" required="true">
				<para>Context to transfer to.</para>
			</parameter>
			<parameter name="ExtraContext">
				<para>Context to transfer extrachannel to (optional).</para>
			</parameter>
			<parameter name="Priority" required="true">
				<para>Priority to transfer to.</para>
			</parameter>
			<parameter name="ExtraPriority">
				<para>Priority to transfer extrachannel to (optional).</para>
			</parameter>
		</syntax>
		<description>
			<para>Redirect (transfer) a call.</para>
		</description>
	</manager>
	<manager name="Atxfer" language="en_US">
		<synopsis>
			Attended transfer.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Transferer's channel.</para>
			</parameter>
			<parameter name="Exten" required="true">
				<para>Extension to transfer to.</para>
			</parameter>
			<parameter name="Context">
				<para>Context to transfer to.</para>
			</parameter>
		</syntax>
		<description>
			<para>Attended transfer.</para>
		</description>
	</manager>
	<manager name="Originate" language="en_US">
		<synopsis>
			Originate a call.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to call.</para>
			</parameter>
			<parameter name="Exten">
				<para>Extension to use (requires <literal>Context</literal> and
				<literal>Priority</literal>)</para>
			</parameter>
			<parameter name="Context">
				<para>Context to use (requires <literal>Exten</literal> and
				<literal>Priority</literal>)</para>
			</parameter>
			<parameter name="Priority">
				<para>Priority to use (requires <literal>Exten</literal> and
				<literal>Context</literal>)</para>
			</parameter>
			<parameter name="Application">
				<para>Application to execute.</para>
			</parameter>
			<parameter name="Data">
				<para>Data to use (requires <literal>Application</literal>).</para>
			</parameter>
			<parameter name="Timeout" default="30000">
				<para>How long to wait for call to be answered (in ms.).</para>
			</parameter>
			<parameter name="CallerID">
				<para>Caller ID to be set on the outgoing channel.</para>
			</parameter>
			<parameter name="Variable">
				<para>Channel variable to set, multiple Variable: headers are allowed.</para>
			</parameter>
			<parameter name="Account">
				<para>Account code.</para>
			</parameter>
			<parameter name="EarlyMedia">
				<para>Set to <literal>true</literal> to force call bridge on early media..</para>
			</parameter>
			<parameter name="Async">
				<para>Set to <literal>true</literal> for fast origination.</para>
			</parameter>
			<parameter name="Codecs">
				<para>Comma-separated list of codecs to use for this call.</para>
			</parameter>
			<parameter name="ChannelId">
				<para>Channel UniqueId to be set on the channel.</para>
			</parameter>
			<parameter name="OtherChannelId">
				<para>Channel UniqueId to be set on the second local channel.</para>
			</parameter>
		</syntax>
		<description>
			<para>Generates an outgoing call to a
			<replaceable>Extension</replaceable>/<replaceable>Context</replaceable>/<replaceable>Priority</replaceable>
			or <replaceable>Application</replaceable>/<replaceable>Data</replaceable></para>
		</description>
		<see-also>
			<ref type="managerEvent">OriginateResponse</ref>
		</see-also>
	</manager>
	<managerEvent language="en_US" name="OriginateResponse">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised in response to an Originate command.</synopsis>
			<syntax>
				<parameter name="ActionID" required="false"/>
				<parameter name="Response">
					<enumlist>
						<enum name="Failure"/>
						<enum name="Success"/>
					</enumlist>
				</parameter>
				<parameter name="Channel"/>
				<parameter name="Context"/>
				<parameter name="Exten"/>
				<parameter name="Reason"/>
				<parameter name="Uniqueid"/>
				<parameter name="CallerIDNum"/>
				<parameter name="CallerIDName"/>
			</syntax>
			<see-also>
				<ref type="manager">Originate</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<manager name="Command" language="en_US">
		<synopsis>
			Execute Asterisk CLI Command.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Command" required="true">
				<para>Asterisk CLI command to run.</para>
			</parameter>
		</syntax>
		<description>
			<para>Run a CLI command.</para>
		</description>
	</manager>
	<manager name="ExtensionState" language="en_US">
		<synopsis>
			Check Extension Status.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Exten" required="true">
				<para>Extension to check state on.</para>
			</parameter>
			<parameter name="Context" required="true">
				<para>Context for extension.</para>
			</parameter>
		</syntax>
		<description>
			<para>Report the extension state for given extension. If the extension has a hint,
			will use devicestate to check the status of the device connected to the extension.</para>
			<para>Will return an <literal>Extension Status</literal> message. The response will include
			the hint for the extension and the status.</para>
		</description>
	</manager>
	<manager name="PresenceState" language="en_US">
		<synopsis>
			Check Presence State
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Provider" required="true">
				<para>Presence Provider to check the state of</para>
			</parameter>
		</syntax>
		<description>
			<para>Report the presence state for the given presence provider.</para>
			<para>Will return a <literal>Presence State</literal> message. The response will include the
			presence state and, if set, a presence subtype and custom message.</para>
		</description>
	</manager>
	<manager name="AbsoluteTimeout" language="en_US">
		<synopsis>
			Set absolute timeout.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to hangup.</para>
			</parameter>
			<parameter name="Timeout" required="true">
				<para>Maximum duration of the call (sec).</para>
			</parameter>
		</syntax>
		<description>
			<para>Hangup a channel after a certain time. Acknowledges set time with
			<literal>Timeout Set</literal> message.</para>
		</description>
	</manager>
	<manager name="MailboxStatus" language="en_US">
		<synopsis>
			Check mailbox.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Mailbox" required="true">
				<para>Full mailbox ID <replaceable>mailbox</replaceable>@<replaceable>vm-context</replaceable>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Checks a voicemail account for status.</para>
			<para>Returns whether there are messages waiting.</para>
			<para>Message: Mailbox Status.</para>
			<para>Mailbox: <replaceable>mailboxid</replaceable>.</para>
			<para>Waiting: <literal>0</literal> if messages waiting, <literal>1</literal>
			if no messages waiting.</para>
		</description>
	</manager>
	<manager name="MailboxCount" language="en_US">
		<synopsis>
			Check Mailbox Message Count.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Mailbox" required="true">
				<para>Full mailbox ID <replaceable>mailbox</replaceable>@<replaceable>vm-context</replaceable>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Checks a voicemail account for new messages.</para>
			<para>Returns number of urgent, new and old messages.</para>
			<para>Message: Mailbox Message Count</para>
			<para>Mailbox: <replaceable>mailboxid</replaceable></para>
			<para>UrgentMessages: <replaceable>count</replaceable></para>
			<para>NewMessages: <replaceable>count</replaceable></para>
			<para>OldMessages: <replaceable>count</replaceable></para>
		</description>
	</manager>
	<manager name="ListCommands" language="en_US">
		<synopsis>
			List available manager commands.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Returns the action name and synopsis for every action that
			is available to the user.</para>
		</description>
	</manager>
	<manager name="SendText" language="en_US">
		<synopsis>
			Send text message to channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel to send message to.</para>
			</parameter>
			<parameter name="Message" required="true">
				<para>Message to send.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends A Text Message to a channel while in a call.</para>
		</description>
	</manager>
	<manager name="UserEvent" language="en_US">
		<synopsis>
			Send an arbitrary event.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="UserEvent" required="true">
				<para>Event string to send.</para>
			</parameter>
			<parameter name="Header1">
				<para>Content1.</para>
			</parameter>
			<parameter name="HeaderN">
				<para>ContentN.</para>
			</parameter>
		</syntax>
		<description>
			<para>Send an event to manager sessions.</para>
		</description>
	</manager>
	<manager name="WaitEvent" language="en_US">
		<synopsis>
			Wait for an event to occur.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Timeout" required="true">
				<para>Maximum time (in seconds) to wait for events, <literal>-1</literal> means forever.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action will ellicit a <literal>Success</literal> response. Whenever
			a manager event is queued. Once WaitEvent has been called on an HTTP manager
			session, events will be generated and queued.</para>
		</description>
	</manager>
	<manager name="CoreSettings" language="en_US">
		<synopsis>
			Show PBX core settings (version etc).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Query for Core PBX settings.</para>
		</description>
	</manager>
	<manager name="CoreStatus" language="en_US">
		<synopsis>
			Show PBX core status variables.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Query for Core PBX status.</para>
		</description>
	</manager>
	<manager name="Reload" language="en_US">
		<synopsis>
			Send a reload event.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Module">
				<para>Name of the module to reload.</para>
			</parameter>
		</syntax>
		<description>
			<para>Send a reload event.</para>
		</description>
	</manager>
	<manager name="CoreShowChannels" language="en_US">
		<synopsis>
			List currently active channels.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>List currently defined channels and some information about them.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="CoreShowChannel">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised in response to a CoreShowChannels command.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
				<channel_snapshot/>
				<parameter name="BridgeId">
					<para>Identifier of the bridge the channel is in, may be empty if not in one</para>
				</parameter>
				<parameter name="Application">
					<para>Application currently executing on the channel</para>
				</parameter>
				<parameter name="ApplicationData">
					<para>Data given to the currently executing application</para>
				</parameter>
				<parameter name="Duration">
					<para>The amount of time the channel has existed</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="manager">CoreShowChannels</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<manager name="ModuleLoad" language="en_US">
		<synopsis>
			Module management.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Module">
				<para>Asterisk module name (including .so extension) or subsystem identifier:</para>
				<enumlist>
					<enum name="cdr" />
					<enum name="dnsmgr" />
					<enum name="extconfig" />
					<enum name="enum" />
					<enum name="acl" />
					<enum name="manager" />
					<enum name="http" />
					<enum name="logger" />
					<enum name="features" />
					<enum name="dsp" />
					<enum name="udptl" />
					<enum name="indications" />
					<enum name="cel" />
					<enum name="plc" />
				</enumlist>
			</parameter>
			<parameter name="LoadType" required="true">
				<para>The operation to be done on module. Subsystem identifiers may only
				be reloaded.</para>
				<enumlist>
					<enum name="load" />
					<enum name="unload" />
					<enum name="reload" />
				</enumlist>
				<para>If no module is specified for a <literal>reload</literal> loadtype,
				all modules are reloaded.</para>
			</parameter>
		</syntax>
		<description>
			<para>Loads, unloads or reloads an Asterisk module in a running system.</para>
		</description>
	</manager>
	<manager name="ModuleCheck" language="en_US">
		<synopsis>
			Check if module is loaded.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Module" required="true">
				<para>Asterisk module name (not including extension).</para>
			</parameter>
		</syntax>
		<description>
			<para>Checks if Asterisk module is loaded. Will return Success/Failure.
			For success returns, the module revision number is included.</para>
		</description>
	</manager>
	<manager name="AOCMessage" language="en_US">
		<synopsis>
			Generate an Advice of Charge message on a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to generate the AOC message on.</para>
			</parameter>
			<parameter name="ChannelPrefix">
				<para>Partial channel prefix.  By using this option one can match the beginning part
				of a channel name without having to put the entire name in.  For example
				if a channel name is SIP/snom-00000001 and this value is set to SIP/snom, then
				that channel matches and the message will be sent.  Note however that only
				the first matched channel has the message sent on it. </para>
			</parameter>
			<parameter name="MsgType" required="true">
				<para>Defines what type of AOC message to create, AOC-D or AOC-E</para>
				<enumlist>
					<enum name="D" />
					<enum name="E" />
				</enumlist>
			</parameter>
			<parameter name="ChargeType" required="true">
				<para>Defines what kind of charge this message represents.</para>
				<enumlist>
					<enum name="NA" />
					<enum name="FREE" />
					<enum name="Currency" />
					<enum name="Unit" />
				</enumlist>
			</parameter>
			<parameter name="UnitAmount(0)">
				<para>This represents the amount of units charged. The ETSI AOC standard specifies that
				this value along with the optional UnitType value are entries in a list.  To accommodate this
				these values take an index value starting at 0 which can be used to generate this list of
				unit entries.  For Example, If two unit entires were required this could be achieved by setting the
				paramter UnitAmount(0)=1234 and UnitAmount(1)=5678.  Note that UnitAmount at index 0 is
				required when ChargeType=Unit, all other entries in the list are optional.
				</para>
			</parameter>
			<parameter name="UnitType(0)">
				<para>Defines the type of unit.  ETSI AOC standard specifies this as an integer
				value between 1 and 16, but this value is left open to accept any positive
				integer.  Like the UnitAmount parameter, this value represents a list entry
				and has an index parameter that starts at 0.
				</para>
			</parameter>
			<parameter name="CurrencyName">
				<para>Specifies the currency's name.  Note that this value is truncated after 10 characters.</para>
			</parameter>
			<parameter name="CurrencyAmount">
				<para>Specifies the charge unit amount as a positive integer.  This value is required
				when ChargeType==Currency.</para>
			</parameter>
			<parameter name="CurrencyMultiplier">
				<para>Specifies the currency multiplier.  This value is required when ChargeType==Currency.</para>
				<enumlist>
					<enum name="OneThousandth" />
					<enum name="OneHundredth" />
					<enum name="OneTenth" />
					<enum name="One" />
					<enum name="Ten" />
					<enum name="Hundred" />
					<enum name="Thousand" />
				</enumlist>
			</parameter>
			<parameter name="TotalType" default="Total">
				<para>Defines what kind of AOC-D total is represented.</para>
				<enumlist>
					<enum name="Total" />
					<enum name="SubTotal" />
				</enumlist>
			</parameter>
			<parameter name="AOCBillingId">
				<para>Represents a billing ID associated with an AOC-D or AOC-E message. Note
				that only the first 3 items of the enum are valid AOC-D billing IDs</para>
				<enumlist>
					<enum name="Normal" />
					<enum name="ReverseCharge" />
					<enum name="CreditCard" />
					<enum name="CallFwdUnconditional" />
					<enum name="CallFwdBusy" />
					<enum name="CallFwdNoReply" />
					<enum name="CallDeflection" />
					<enum name="CallTransfer" />
				</enumlist>
			</parameter>
			<parameter name="ChargingAssociationId">
				<para>Charging association identifier.  This is optional for AOC-E and can be
				set to any value between -32768 and 32767</para>
			</parameter>
			<parameter name="ChargingAssociationNumber">
				<para>Represents the charging association party number.  This value is optional
				for AOC-E.</para>
			</parameter>
			<parameter name="ChargingAssociationPlan">
				<para>Integer representing the charging plan associated with the ChargingAssociationNumber.
				The value is bits 7 through 1 of the Q.931 octet containing the type-of-number and
				numbering-plan-identification fields.</para>
			</parameter>
		</syntax>
		<description>
			<para>Generates an AOC-D or AOC-E message on a channel.</para>
		</description>
	</manager>
	<function name="AMI_CLIENT" language="en_US">
		<synopsis>
			Checks attributes of manager accounts
		</synopsis>
		<syntax>
			<parameter name="loginname" required="true">
				<para>Login name, specified in manager.conf</para>
			</parameter>
			<parameter name="field" required="true">
				<para>The manager account attribute to return</para>
				<enumlist>
					<enum name="sessions"><para>The number of sessions for this AMI account</para></enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>
				Currently, the only supported  parameter is "sessions" which will return the current number of
				active sessions for this AMI account.
			</para>
		</description>
	</function>
	<manager name="Filter" language="en_US">
		<synopsis>
			Dynamically add filters for the current manager session.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Operation">
				<enumlist>
					<enum name="Add">
						<para>Add a filter.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="Filter">
				<para>Filters can be whitelist or blacklist</para>
				<para>Example whitelist filter: "Event: Newchannel"</para>
				<para>Example blacklist filter: "!Channel: DAHDI.*"</para>
				<para>This filter option is used to whitelist or blacklist events per user to be
				reported with regular expressions and are allowed if both the regex matches
				and the user has read access as defined in manager.conf. Filters are assumed to be for whitelisting
				unless preceeded by an exclamation point, which marks it as being black.
				Evaluation of the filters is as follows:</para>
				<para>- If no filters are configured all events are reported as normal.</para>
				<para>- If there are white filters only: implied black all filter processed first, then white filters.</para>
				<para>- If there are black filters only: implied white all filter processed first, then black filters.</para>
				<para>- If there are both white and black filters: implied black all filter processed first, then white
				filters, and lastly black filters.</para>
			</parameter>
		</syntax>
		<description>
			<para>The filters added are only used for the current session.
			Once the connection is closed the filters are removed.</para>
			<para>This comand requires the system permission because
			this command can be used to create filters that may bypass
			filters defined in manager.conf</para>
		</description>
	</manager>
	<manager name="FilterList" language="en_US">
		<synopsis>
			Show current event filters for this session
		</synopsis>
		<description>
			<para>The filters displayed are for the current session.  Only those filters defined in
                        manager.conf will be present upon starting a new session.</para>
		</description>
	</manager>
	<manager name="BlindTransfer" language="en_US">
		<synopsis>
			Blind transfer channel(s) to the given destination
		</synopsis>
		<syntax>
			<parameter name="Channel" required="true">
			</parameter>
			<parameter name="Context">
			</parameter>
			<parameter name="Exten">
			</parameter>
		</syntax>
		<description>
			<para>Redirect all channels currently bridged to the specified channel to the specified destination.</para>
		</description>
		<see-also>
			<ref type="manager">Redirect</ref>
		</see-also>
	</manager>
 ***/

/*! \addtogroup Group_AMI AMI functions
*/
/*! @{
 Doxygen group */

enum error_type {
	UNKNOWN_ACTION = 1,
	UNKNOWN_CATEGORY,
	UNSPECIFIED_CATEGORY,
	UNSPECIFIED_ARGUMENT,
	FAILURE_ALLOCATION,
	FAILURE_NEWCAT,
	FAILURE_DELCAT,
	FAILURE_EMPTYCAT,
	FAILURE_UPDATE,
	FAILURE_DELETE,
	FAILURE_APPEND,
	FAILURE_TEMPLATE
};

enum add_filter_result {
	FILTER_SUCCESS,
	FILTER_ALLOC_FAILED,
	FILTER_COMPILE_FAIL,
};

/*!
 * Linked list of events.
 * Global events are appended to the list by append_event().
 * The usecount is the number of stored pointers to the element,
 * excluding the list pointers. So an element that is only in
 * the list has a usecount of 0, not 1.
 *
 * Clients have a pointer to the last event processed, and for each
 * of these clients we track the usecount of the elements.
 * If we have a pointer to an entry in the list, it is safe to navigate
 * it forward because elements will not be deleted, but only appended.
 * The worst that can happen is seeing the pointer still NULL.
 *
 * When the usecount of an element drops to 0, and the element is the
 * first in the list, we can remove it. Removal is done within the
 * main thread, which is woken up for the purpose.
 *
 * For simplicity of implementation, we make sure the list is never empty.
 */
struct eventqent {
	int usecount;		/*!< # of clients who still need the event */
	int category;
	unsigned int seq;	/*!< sequence number */
	struct timeval tv;  /*!< When event was allocated */
	AST_RWLIST_ENTRY(eventqent) eq_next;
	char eventdata[1];	/*!< really variable size, allocated by append_event() */
};

static AST_RWLIST_HEAD_STATIC(all_events, eventqent);

static int displayconnects = 1;
static int allowmultiplelogin = 1;
static int timestampevents;
static int httptimeout = 60;
static int broken_events_action = 0;
static int manager_enabled = 0;
static int subscribed = 0;
static int webmanager_enabled = 0;
static int manager_debug = 0;	/*!< enable some debugging code in the manager */
static int authtimeout;
static int authlimit;
static char *manager_channelvars;

#define DEFAULT_REALM		"asterisk"
static char global_realm[MAXHOSTNAMELEN];	/*!< Default realm */

static int unauth_sessions = 0;
static struct stasis_subscription *acl_change_sub;

/*! \brief A \ref stasis_topic that all topics AMI cares about will be forwarded to */
static struct stasis_topic *manager_topic;

/*! \brief The \ref stasis_message_router for all \ref stasis messages */
static struct stasis_message_router *stasis_router;

/*! \brief The \ref stasis_subscription for forwarding the RTP topic to the AMI topic */
static struct stasis_forward *rtp_topic_forwarder;

/*! \brief The \ref stasis_subscription for forwarding the Security topic to the AMI topic */
static struct stasis_forward *security_topic_forwarder;

#ifdef TEST_FRAMEWORK
struct stasis_subscription *test_suite_sub;
#endif

#define MGR_SHOW_TERMINAL_WIDTH 80

#define MAX_VARS 128

/*! \brief Fake event class used to end sessions at shutdown */
#define EVENT_FLAG_SHUTDOWN -1

/*! \brief
 * Descriptor for a manager session, either on the AMI socket or over HTTP.
 *
 * \note
 * AMI session have managerid == 0; the entry is created upon a connect,
 * and destroyed with the socket.
 * HTTP sessions have managerid != 0, the value is used as a search key
 * to lookup sessions (using the mansession_id cookie, or nonce key from
 * Digest Authentication http header).
 */
#define MAX_BLACKLIST_CMD_LEN 2
static const struct {
	const char *words[AST_MAX_CMD_LEN];
} command_blacklist[] = {
	{{ "module", "load", NULL }},
	{{ "module", "unload", NULL }},
	{{ "restart", "gracefully", NULL }},
};

static void acl_change_stasis_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message);

static void acl_change_stasis_subscribe(void)
{
	if (!acl_change_sub) {
		acl_change_sub = stasis_subscribe(ast_security_topic(),
			acl_change_stasis_cb, NULL);
	}
}

static void acl_change_stasis_unsubscribe(void)
{
	acl_change_sub = stasis_unsubscribe_and_join(acl_change_sub);
}

/* In order to understand what the heck is going on with the
 * mansession_session and mansession structs, we need to have a bit of a history
 * lesson.
 *
 * In the beginning, there was the mansession. The mansession contained data that was
 * intrinsic to a manager session, such as the time that it started, the name of the logged-in
 * user, etc. In addition to these parameters were the f and fd parameters. For typical manager
 * sessions, these were used to represent the TCP socket over which the AMI session was taking
 * place. It makes perfect sense for these fields to be a part of the session-specific data since
 * the session actually defines this information.
 *
 * Then came the HTTP AMI sessions. With these, the f and fd fields need to be opened and closed
 * for every single action that occurs. Thus the f and fd fields aren't really specific to the session
 * but rather to the action that is being executed. Because a single session may execute many commands
 * at once, some sort of safety needed to be added in order to be sure that we did not end up with fd
 * leaks from one action overwriting the f and fd fields used by a previous action before the previous action
 * has had a chance to properly close its handles.
 *
 * The initial idea to solve this was to use thread synchronization, but this prevented multiple actions
 * from being run at the same time in a single session. Some manager actions may block for a long time, thus
 * creating a large queue of actions to execute. In addition, this fix did not address the basic architectural
 * issue that for HTTP manager sessions, the f and fd variables are not really a part of the session, but are
 * part of the action instead.
 *
 * The new idea was to create a structure on the stack for each HTTP Manager action. This structure would
 * contain the action-specific information, such as which file to write to. In order to maintain expectations
 * of action handlers and not have to change the public API of the manager code, we would need to name this
 * new stacked structure 'mansession' and contain within it the old mansession struct that we used to use.
 * We renamed the old mansession struct 'mansession_session' to hopefully convey that what is in this structure
 * is session-specific data. The structure that it is wrapped in, called a 'mansession' really contains action-specific
 * data.
 */
struct mansession_session {
				/*! \todo XXX need to document which fields it is protecting */
	struct ast_sockaddr addr;	/*!< address we are connecting from */
	FILE *f;		/*!< fdopen() on the underlying fd */
	int fd;			/*!< descriptor used for output. Either the socket (AMI) or a temporary file (HTTP) */
	int inuse;		/*!< number of HTTP sessions using this entry */
	int needdestroy;	/*!< Whether an HTTP session should be destroyed */
	pthread_t waiting_thread;	/*!< Sleeping thread using this descriptor */
	uint32_t managerid;	/*!< Unique manager identifier, 0 for AMI sessions */
	time_t sessionstart;    /*!< Session start time */
	struct timeval sessionstart_tv; /*!< Session start time */
	time_t sessiontimeout;	/*!< Session timeout if HTTP */
	char username[80];	/*!< Logged in username */
	char challenge[10];	/*!< Authentication challenge */
	int authenticated;	/*!< Authentication status */
	int readperm;		/*!< Authorization for reading */
	int writeperm;		/*!< Authorization for writing */
	char inbuf[1025];	/*!< Buffer -  we use the extra byte to add a '\\0' and simplify parsing */
	int inlen;		/*!< number of buffered bytes */
	struct ao2_container *whitefilters;	/*!< Manager event filters - white list */
	struct ao2_container *blackfilters;	/*!< Manager event filters - black list */
	struct ast_variable *chanvars;  /*!< Channel variables to set for originate */
	int send_events;	/*!<  XXX what ? */
	struct eventqent *last_ev;	/*!< last event processed. */
	int writetimeout;	/*!< Timeout for ast_carefulwrite() */
	time_t authstart;
	int pending_event;         /*!< Pending events indicator in case when waiting_thread is NULL */
	time_t noncetime;	/*!< Timer for nonce value expiration */
	unsigned long oldnonce;	/*!< Stale nonce value */
	unsigned long nc;	/*!< incremental  nonce counter */
	AST_LIST_HEAD_NOLOCK(mansession_datastores, ast_datastore) datastores; /*!< Data stores on the session */
	AST_LIST_ENTRY(mansession_session) list;
};

enum mansession_message_parsing {
	MESSAGE_OKAY,
	MESSAGE_LINE_TOO_LONG
};

/*! \brief In case you didn't read that giant block of text above the mansession_session struct, the
 * \ref struct mansession is named this solely to keep the API the same in Asterisk. This structure really
 * represents data that is different from Manager action to Manager action. The mansession_session pointer
 * contained within points to session-specific data.
 */
struct mansession {
	struct mansession_session *session;
	struct ast_tcptls_session_instance *tcptls_session;
	FILE *f;
	int fd;
	enum mansession_message_parsing parsing;
	int write_error:1;
	struct manager_custom_hook *hook;
	ast_mutex_t lock;
};

/*! Active manager connection sessions container. */
static AO2_GLOBAL_OBJ_STATIC(mgr_sessions);

/*! \brief user descriptor, as read from the config file.
 *
 * \note It is still missing some fields -- e.g. we can have multiple permit and deny
 * lines which are not supported here, and readperm/writeperm/writetimeout
 * are not stored.
 */
struct ast_manager_user {
	char username[80];
	char *secret;			/*!< Secret for logging in */
	int readperm;			/*!< Authorization for reading */
	int writeperm;			/*!< Authorization for writing */
	int writetimeout;		/*!< Per user Timeout for ast_carefulwrite() */
	int displayconnects;		/*!< XXX unused */
	int allowmultiplelogin; /*!< Per user option*/
	int keep;			/*!< mark entries created on a reload */
	struct ao2_container *whitefilters; /*!< Manager event filters - white list */
	struct ao2_container *blackfilters; /*!< Manager event filters - black list */
	struct ast_acl_list *acl;       /*!< ACL setting */
	char *a1_hash;			/*!< precalculated A1 for Digest auth */
	struct ast_variable *chanvars;  /*!< Channel variables to set for originate */
	AST_RWLIST_ENTRY(ast_manager_user) list;
};

/*! \brief list of users found in the config file */
static AST_RWLIST_HEAD_STATIC(users, ast_manager_user);

/*! \brief list of actions registered */
static AST_RWLIST_HEAD_STATIC(actions, manager_action);

/*! \brief list of hooks registered */
static AST_RWLIST_HEAD_STATIC(manager_hooks, manager_custom_hook);

/*! \brief A container of event documentation nodes */
static AO2_GLOBAL_OBJ_STATIC(event_docs);

static enum add_filter_result manager_add_filter(const char *filter_pattern, struct ao2_container *whitefilters, struct ao2_container *blackfilters);

static int match_filter(struct mansession *s, char *eventdata);

/*!
 * @{ \brief Define AMI message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_manager_get_generic_type);
/*! @} */

/*!
 * \internal
 * \brief Find a registered action object.
 *
 * \param name Name of AMI action to find.
 *
 * \return Reffed action found or NULL
 */
static struct manager_action *action_find(const char *name)
{
	struct manager_action *act;

	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, act, list) {
		if (!strcasecmp(name, act->action)) {
			ao2_t_ref(act, +1, "found action object");
			break;
		}
	}
	AST_RWLIST_UNLOCK(&actions);

	return act;
}

struct stasis_topic *ast_manager_get_topic(void)
{
	return manager_topic;
}

struct stasis_message_router *ast_manager_get_message_router(void)
{
	return stasis_router;
}

static void manager_json_value_str_append(struct ast_json *value, const char *key,
					  struct ast_str **res)
{
	switch (ast_json_typeof(value)) {
	case AST_JSON_STRING:
		ast_str_append(res, 0, "%s: %s\r\n", key, ast_json_string_get(value));
		break;
	case AST_JSON_INTEGER:
		ast_str_append(res, 0, "%s: %jd\r\n", key, ast_json_integer_get(value));
		break;
	case AST_JSON_TRUE:
		ast_str_append(res, 0, "%s: True\r\n", key);
		break;
	case AST_JSON_FALSE:
		ast_str_append(res, 0, "%s: False\r\n", key);
		break;
	default:
		ast_str_append(res, 0, "%s: \r\n", key);
		break;
	}
}

static void manager_json_to_ast_str(struct ast_json *obj, const char *key,
				    struct ast_str **res, key_exclusion_cb exclusion_cb);

static void manager_json_array_with_key(struct ast_json *obj, const char* key,
					size_t index, struct ast_str **res,
					key_exclusion_cb exclusion_cb)
{
	struct ast_str *key_str = ast_str_alloca(64);
	ast_str_set(&key_str, 0, "%s(%zu)", key, index);
	manager_json_to_ast_str(obj, ast_str_buffer(key_str),
				res, exclusion_cb);
}

static void manager_json_obj_with_key(struct ast_json *obj, const char* key,
				      const char *parent_key, struct ast_str **res,
				      key_exclusion_cb exclusion_cb)
{
	if (parent_key) {
		struct ast_str *key_str = ast_str_alloca(64);
		ast_str_set(&key_str, 0, "%s/%s", parent_key, key);
		manager_json_to_ast_str(obj, ast_str_buffer(key_str),
					res, exclusion_cb);
		return;
	}

	manager_json_to_ast_str(obj, key, res, exclusion_cb);
}

void manager_json_to_ast_str(struct ast_json *obj, const char *key,
			     struct ast_str **res, key_exclusion_cb exclusion_cb)
{
	struct ast_json_iter *i;

	if (!obj || (!res && !(*res) && (!(*res = ast_str_create(1024))))) {
		return;
	}

	if (exclusion_cb && key && exclusion_cb(key)) {
		return;
	}

	if (ast_json_typeof(obj) != AST_JSON_OBJECT &&
	    ast_json_typeof(obj) != AST_JSON_ARRAY) {
		manager_json_value_str_append(obj, key, res);
		return;
	}

	if (ast_json_typeof(obj) == AST_JSON_ARRAY) {
		size_t j;
		for (j = 0; j < ast_json_array_size(obj); ++j) {
			manager_json_array_with_key(ast_json_array_get(obj, j),
						    key, j, res, exclusion_cb);
		}
		return;
	}

	for (i = ast_json_object_iter(obj); i;
	     i = ast_json_object_iter_next(obj, i)) {
		manager_json_obj_with_key(ast_json_object_iter_value(i),
					  ast_json_object_iter_key(i),
					  key, res, exclusion_cb);
	}
}


struct ast_str *ast_manager_str_from_json_object(struct ast_json *blob, key_exclusion_cb exclusion_cb)
{
	struct ast_str *res = ast_str_create(1024);
	manager_json_to_ast_str(blob, NULL, &res, exclusion_cb);
	return res;
}

static void manager_default_msg_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	RAII_VAR(struct ast_manager_event_blob *, ev, NULL, ao2_cleanup);

	ev = stasis_message_to_ami(message);

	if (ev == NULL) {
		/* Not and AMI message; disregard */
		return;
	}

	manager_event(ev->event_flags, ev->manager_event, "%s",
		ev->extra_fields);
}

static void manager_generic_msg_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ast_json_payload *payload = stasis_message_data(message);
	int class_type = ast_json_integer_get(ast_json_object_get(payload->json, "class_type"));
	const char *type = ast_json_string_get(ast_json_object_get(payload->json, "type"));
	struct ast_json *event = ast_json_object_get(payload->json, "event");
	RAII_VAR(struct ast_str *, event_buffer, NULL, ast_free);

	event_buffer = ast_manager_str_from_json_object(event, NULL);
	if (!event_buffer) {
		ast_log(AST_LOG_WARNING, "Error while creating payload for event %s\n", type);
		return;
	}
	manager_event(class_type, type, "%s", ast_str_buffer(event_buffer));
}

void ast_manager_publish_event(const char *type, int class_type, struct ast_json *obj)
{
	RAII_VAR(struct ast_json *, event_info, NULL, ast_json_unref);
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	if (!obj) {
		return;
	}

	ast_json_ref(obj);
	event_info = ast_json_pack("{s: s, s: i, s: o}",
			"type", type,
			"class_type", class_type,
			"event", obj);
	if (!event_info) {
		return;
	}

	payload = ast_json_payload_create(event_info);
	if (!payload) {
		return;
	}
	message = stasis_message_create(ast_manager_get_generic_type(), payload);
	if (!message) {
		return;
	}
	stasis_publish(ast_manager_get_topic(), message);
}

/*! \brief Add a custom hook to be called when an event is fired */
void ast_manager_register_hook(struct manager_custom_hook *hook)
{
	AST_RWLIST_WRLOCK(&manager_hooks);
	AST_RWLIST_INSERT_TAIL(&manager_hooks, hook, list);
	AST_RWLIST_UNLOCK(&manager_hooks);
}

/*! \brief Delete a custom hook to be called when an event is fired */
void ast_manager_unregister_hook(struct manager_custom_hook *hook)
{
	AST_RWLIST_WRLOCK(&manager_hooks);
	AST_RWLIST_REMOVE(&manager_hooks, hook, list);
	AST_RWLIST_UNLOCK(&manager_hooks);
}

int check_manager_enabled(void)
{
	return manager_enabled;
}

int check_webmanager_enabled(void)
{
	return (webmanager_enabled && manager_enabled);
}

/*!
 * Grab a reference to the last event, update usecount as needed.
 * Can handle a NULL pointer.
 */
static struct eventqent *grab_last(void)
{
	struct eventqent *ret;

	AST_RWLIST_WRLOCK(&all_events);
	ret = AST_RWLIST_LAST(&all_events);
	/* the list is never empty now, but may become so when
	 * we optimize it in the future, so be prepared.
	 */
	if (ret) {
		ast_atomic_fetchadd_int(&ret->usecount, 1);
	}
	AST_RWLIST_UNLOCK(&all_events);
	return ret;
}

/*!
 * Purge unused events. Remove elements from the head
 * as long as their usecount is 0 and there is a next element.
 */
static void purge_events(void)
{
	struct eventqent *ev;
	struct timeval now = ast_tvnow();

	AST_RWLIST_WRLOCK(&all_events);
	while ( (ev = AST_RWLIST_FIRST(&all_events)) &&
	    ev->usecount == 0 && AST_RWLIST_NEXT(ev, eq_next)) {
		AST_RWLIST_REMOVE_HEAD(&all_events, eq_next);
		ast_free(ev);
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&all_events, ev, eq_next) {
		/* Never release the last event */
		if (!AST_RWLIST_NEXT(ev, eq_next)) {
			break;
		}

		/* 2.5 times whatever the HTTP timeout is (maximum 2.5 hours) is the maximum time that we will definitely cache an event */
		if (ev->usecount == 0 && ast_tvdiff_sec(now, ev->tv) > (httptimeout > 3600 ? 3600 : httptimeout) * 2.5) {
			AST_RWLIST_REMOVE_CURRENT(eq_next);
			ast_free(ev);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&all_events);
}

/*!
 * helper functions to convert back and forth between
 * string and numeric representation of set of flags
 */
static const struct permalias {
	int num;
	const char *label;
} perms[] = {
	{ EVENT_FLAG_SYSTEM, "system" },
	{ EVENT_FLAG_CALL, "call" },
	{ EVENT_FLAG_LOG, "log" },
	{ EVENT_FLAG_VERBOSE, "verbose" },
	{ EVENT_FLAG_COMMAND, "command" },
	{ EVENT_FLAG_AGENT, "agent" },
	{ EVENT_FLAG_USER, "user" },
	{ EVENT_FLAG_CONFIG, "config" },
	{ EVENT_FLAG_DTMF, "dtmf" },
	{ EVENT_FLAG_REPORTING, "reporting" },
	{ EVENT_FLAG_CDR, "cdr" },
	{ EVENT_FLAG_DIALPLAN, "dialplan" },
	{ EVENT_FLAG_ORIGINATE, "originate" },
	{ EVENT_FLAG_AGI, "agi" },
	{ EVENT_FLAG_CC, "cc" },
	{ EVENT_FLAG_AOC, "aoc" },
	{ EVENT_FLAG_TEST, "test" },
	{ EVENT_FLAG_SECURITY, "security" },
	{ EVENT_FLAG_MESSAGE, "message" },
	{ INT_MAX, "all" },
	{ 0, "none" },
};

/*! \brief Checks to see if a string which can be used to evaluate functions should be rejected */
static int function_capable_string_allowed_with_auths(const char *evaluating, int writepermlist)
{
	if (!(writepermlist & EVENT_FLAG_SYSTEM)
		&& (
			strstr(evaluating, "SHELL") ||       /* NoOp(${SHELL(rm -rf /)})  */
			strstr(evaluating, "EVAL")           /* NoOp(${EVAL(${some_var_containing_SHELL})}) */
		)) {
		return 0;
	}
	return 1;
}

/*! \brief Convert authority code to a list of options for a user. This will only
 * display those authority codes that have an explicit match on authority */
static const char *user_authority_to_str(int authority, struct ast_str **res)
{
	int i;
	char *sep = "";

	ast_str_reset(*res);
	for (i = 0; i < ARRAY_LEN(perms) - 1; i++) {
		if ((authority & perms[i].num) == perms[i].num) {
			ast_str_append(res, 0, "%s%s", sep, perms[i].label);
			sep = ",";
		}
	}

	if (ast_str_strlen(*res) == 0)	/* replace empty string with something sensible */
		ast_str_append(res, 0, "<none>");

	return ast_str_buffer(*res);
}


/*! \brief Convert authority code to a list of options. Note that the EVENT_FLAG_ALL
 * authority will always be returned. */
static const char *authority_to_str(int authority, struct ast_str **res)
{
	int i;
	char *sep = "";

	ast_str_reset(*res);
	for (i = 0; i < ARRAY_LEN(perms) - 1; i++) {
		if (authority & perms[i].num) {
			ast_str_append(res, 0, "%s%s", sep, perms[i].label);
			sep = ",";
		}
	}

	if (ast_str_strlen(*res) == 0)	/* replace empty string with something sensible */
		ast_str_append(res, 0, "<none>");

	return ast_str_buffer(*res);
}

/*! Tells you if smallstr exists inside bigstr
   which is delim by delim and uses no buf or stringsep
   ast_instring("this|that|more","this",'|') == 1;

   feel free to move this to app.c -anthm */
static int ast_instring(const char *bigstr, const char *smallstr, const char delim)
{
	const char *val = bigstr, *next;

	do {
		if ((next = strchr(val, delim))) {
			if (!strncmp(val, smallstr, (next - val))) {
				return 1;
			} else {
				continue;
			}
		} else {
			return !strcmp(smallstr, val);
		}
	} while (*(val = (next + 1)));

	return 0;
}

static int get_perm(const char *instr)
{
	int x = 0, ret = 0;

	if (!instr) {
		return 0;
	}

	for (x = 0; x < ARRAY_LEN(perms); x++) {
		if (ast_instring(instr, perms[x].label, ',')) {
			ret |= perms[x].num;
		}
	}

	return ret;
}

/*!
 * A number returns itself, false returns 0, true returns all flags,
 * other strings return the flags that are set.
 */
static int strings_to_mask(const char *string)
{
	const char *p;

	if (ast_strlen_zero(string)) {
		return -1;
	}

	for (p = string; *p; p++) {
		if (*p < '0' || *p > '9') {
			break;
		}
	}
	if (!*p) { /* all digits */
		return atoi(string);
	}
	if (ast_false(string)) {
		return 0;
	}
	if (ast_true(string)) {	/* all permissions */
		int x, ret = 0;
		for (x = 0; x < ARRAY_LEN(perms); x++) {
			ret |= perms[x].num;
		}
		return ret;
	}
	return get_perm(string);
}

/*! \brief Unreference manager session object.
     If no more references, then go ahead and delete it */
static struct mansession_session *unref_mansession(struct mansession_session *s)
{
	int refcount = ao2_ref(s, -1);
	if (manager_debug) {
		ast_debug(1, "Mansession: %p refcount now %d\n", s, refcount - 1);
	}
	return NULL;
}

static void event_filter_destructor(void *obj)
{
	regex_t *regex_filter = obj;
	regfree(regex_filter);
}

static void session_destructor(void *obj)
{
	struct mansession_session *session = obj;
	struct eventqent *eqe = session->last_ev;
	struct ast_datastore *datastore;

	/* Get rid of each of the data stores on the session */
	while ((datastore = AST_LIST_REMOVE_HEAD(&session->datastores, entry))) {
		/* Free the data store */
		ast_datastore_free(datastore);
	}

	if (session->f != NULL) {
		fflush(session->f);
		fclose(session->f);
	}
	if (eqe) {
		ast_atomic_fetchadd_int(&eqe->usecount, -1);
	}
	if (session->chanvars) {
		ast_variables_destroy(session->chanvars);
	}

	if (session->whitefilters) {
		ao2_t_ref(session->whitefilters, -1, "decrement ref for white container, should be last one");
	}

	if (session->blackfilters) {
		ao2_t_ref(session->blackfilters, -1, "decrement ref for black container, should be last one");
	}
}

/*! \brief Allocate manager session structure and add it to the list of sessions */
static struct mansession_session *build_mansession(const struct ast_sockaddr *addr)
{
	struct ao2_container *sessions;
	struct mansession_session *newsession;

	newsession = ao2_alloc(sizeof(*newsession), session_destructor);
	if (!newsession) {
		return NULL;
	}

	newsession->whitefilters = ao2_container_alloc(1, NULL, NULL);
	newsession->blackfilters = ao2_container_alloc(1, NULL, NULL);
	if (!newsession->whitefilters || !newsession->blackfilters) {
		ao2_ref(newsession, -1);
		return NULL;
	}

	newsession->fd = -1;
	newsession->waiting_thread = AST_PTHREADT_NULL;
	newsession->writetimeout = 100;
	newsession->send_events = -1;
	ast_sockaddr_copy(&newsession->addr, addr);

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		ao2_link(sessions, newsession);
		ao2_ref(sessions, -1);
	}

	return newsession;
}

static int mansession_cmp_fn(void *obj, void *arg, int flags)
{
	struct mansession_session *s = obj;
	char *str = arg;
	return !strcasecmp(s->username, str) ? CMP_MATCH : 0;
}

static void session_destroy(struct mansession_session *s)
{
	struct ao2_container *sessions;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		ao2_unlink(sessions, s);
		ao2_ref(sessions, -1);
	}
	unref_mansession(s);
}


static int check_manager_session_inuse(const char *name)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	int inuse = 0;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		session = ao2_find(sessions, (char *) name, 0);
		ao2_ref(sessions, -1);
		if (session) {
			unref_mansession(session);
			inuse = 1;
		}
	}
	return inuse;
}


/*!
 * lookup an entry in the list of registered users.
 * must be called with the list lock held.
 */
static struct ast_manager_user *get_manager_by_name_locked(const char *name)
{
	struct ast_manager_user *user = NULL;

	AST_RWLIST_TRAVERSE(&users, user, list) {
		if (!strcasecmp(user->username, name)) {
			break;
		}
	}

	return user;
}

/*! \brief Get displayconnects config option.
 *  \param session manager session to get parameter from.
 *  \return displayconnects config option value.
 */
static int manager_displayconnects(struct mansession_session *session)
{
	struct ast_manager_user *user = NULL;
	int ret = 0;

	AST_RWLIST_RDLOCK(&users);
	if ((user = get_manager_by_name_locked(session->username))) {
		ret = user->displayconnects;
	}
	AST_RWLIST_UNLOCK(&users);

	return ret;
}

static char *handle_showmancmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct manager_action *cur;
	struct ast_str *authority;
	int num, l, which;
	char *ret = NULL;
#ifdef AST_XML_DOCS
	char syntax_title[64], description_title[64], synopsis_title[64], seealso_title[64], arguments_title[64], privilege_title[64];
#endif

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show command";
		e->usage =
			"Usage: manager show command <actionname> [<actionname> [<actionname> [...]]]\n"
			"	Shows the detailed description for a specific Asterisk manager interface command.\n";
		return NULL;
	case CLI_GENERATE:
		l = strlen(a->word);
		which = 0;
		AST_RWLIST_RDLOCK(&actions);
		AST_RWLIST_TRAVERSE(&actions, cur, list) {
			if (!strncasecmp(a->word, cur->action, l) && ++which > a->n) {
				ret = ast_strdup(cur->action);
				break;	/* make sure we exit even if ast_strdup() returns NULL */
			}
		}
		AST_RWLIST_UNLOCK(&actions);
		return ret;
	}
	authority = ast_str_alloca(80);
	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

#ifdef AST_XML_DOCS
	/* setup the titles */
	term_color(synopsis_title, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
	term_color(description_title, "[Description]\n", COLOR_MAGENTA, 0, 40);
	term_color(syntax_title, "[Syntax]\n", COLOR_MAGENTA, 0, 40);
	term_color(seealso_title, "[See Also]\n", COLOR_MAGENTA, 0, 40);
	term_color(arguments_title, "[Arguments]\n", COLOR_MAGENTA, 0, 40);
	term_color(privilege_title, "[Privilege]\n", COLOR_MAGENTA, 0, 40);
#endif

	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		for (num = 3; num < a->argc; num++) {
			if (!strcasecmp(cur->action, a->argv[num])) {
				authority_to_str(cur->authority, &authority);

#ifdef AST_XML_DOCS
				if (cur->docsrc == AST_XML_DOC) {
					char *syntax = ast_xmldoc_printable(S_OR(cur->syntax, "Not available"), 1);
					char *synopsis = ast_xmldoc_printable(S_OR(cur->synopsis, "Not available"), 1);
					char *description = ast_xmldoc_printable(S_OR(cur->description, "Not available"), 1);
					char *arguments = ast_xmldoc_printable(S_OR(cur->arguments, "Not available"), 1);
					char *seealso = ast_xmldoc_printable(S_OR(cur->seealso, "Not available"), 1);
					char *privilege = ast_xmldoc_printable(S_OR(authority->str, "Not available"), 1);
					ast_cli(a->fd, "%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n",
						syntax_title, syntax,
						synopsis_title, synopsis,
						description_title, description,
						arguments_title, arguments,
						seealso_title, seealso,
						privilege_title, privilege);
				} else
#endif
				{
					ast_cli(a->fd, "Action: %s\nSynopsis: %s\nPrivilege: %s\n%s\n",
						cur->action, cur->synopsis,
						authority->str,
						S_OR(cur->description, ""));
				}
			}
		}
	}
	AST_RWLIST_UNLOCK(&actions);

	return CLI_SUCCESS;
}

static char *handle_mandebug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager set debug [on|off]";
		e->usage = "Usage: manager set debug [on|off]\n	Show, enable, disable debugging of the manager code.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 3) {
		ast_cli(a->fd, "manager debug is %s\n", manager_debug? "on" : "off");
	} else if (a->argc == 4) {
		if (!strcasecmp(a->argv[3], "on")) {
			manager_debug = 1;
		} else if (!strcasecmp(a->argv[3], "off")) {
			manager_debug = 0;
		} else {
			return CLI_SHOWUSAGE;
		}
	}
	return CLI_SUCCESS;
}

static char *handle_showmanager(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_manager_user *user = NULL;
	int l, which;
	char *ret = NULL;
	struct ast_str *rauthority = ast_str_alloca(128);
	struct ast_str *wauthority = ast_str_alloca(128);
	struct ast_variable *v;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show user";
		e->usage =
			" Usage: manager show user <user>\n"
			"        Display all information related to the manager user specified.\n";
		return NULL;
	case CLI_GENERATE:
		l = strlen(a->word);
		which = 0;
		if (a->pos != 3) {
			return NULL;
		}
		AST_RWLIST_RDLOCK(&users);
		AST_RWLIST_TRAVERSE(&users, user, list) {
			if ( !strncasecmp(a->word, user->username, l) && ++which > a->n ) {
				ret = ast_strdup(user->username);
				break;
			}
		}
		AST_RWLIST_UNLOCK(&users);
		return ret;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&users);

	if (!(user = get_manager_by_name_locked(a->argv[3]))) {
		ast_cli(a->fd, "There is no manager called %s\n", a->argv[3]);
		AST_RWLIST_UNLOCK(&users);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "\n");
	ast_cli(a->fd,
		"          username: %s\n"
		"            secret: %s\n"
		"               ACL: %s\n"
		"         read perm: %s\n"
		"        write perm: %s\n"
		"   displayconnects: %s\n"
		"allowmultiplelogin: %s\n",
		(user->username ? user->username : "(N/A)"),
		(user->secret ? "<Set>" : "(N/A)"),
		((user->acl && !ast_acl_list_is_empty(user->acl)) ? "yes" : "no"),
		user_authority_to_str(user->readperm, &rauthority),
		user_authority_to_str(user->writeperm, &wauthority),
		(user->displayconnects ? "yes" : "no"),
		(user->allowmultiplelogin ? "yes" : "no"));
	ast_cli(a->fd, "         Variables: \n");
		for (v = user->chanvars ; v ; v = v->next) {
			ast_cli(a->fd, "                 %s = %s\n", v->name, v->value);
		}

	AST_RWLIST_UNLOCK(&users);

	return CLI_SUCCESS;
}

static char *handle_showmanagers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_manager_user *user = NULL;
	int count_amu = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show users";
		e->usage =
			"Usage: manager show users\n"
			"       Prints a listing of all managers that are currently configured on that\n"
			" system.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&users);

	/* If there are no users, print out something along those lines */
	if (AST_RWLIST_EMPTY(&users)) {
		ast_cli(a->fd, "There are no manager users.\n");
		AST_RWLIST_UNLOCK(&users);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "\nusername\n--------\n");

	AST_RWLIST_TRAVERSE(&users, user, list) {
		ast_cli(a->fd, "%s\n", user->username);
		count_amu++;
	}

	AST_RWLIST_UNLOCK(&users);

	ast_cli(a->fd,"-------------------\n"
		      "%d manager users configured.\n", count_amu);
	return CLI_SUCCESS;
}

/*! \brief  CLI command  manager list commands */
static char *handle_showmancmds(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct manager_action *cur;
	int name_len = 1;
	int space_remaining;
#define HSMC_FORMAT "  %-*.*s  %-.*s\n"
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show commands";
		e->usage =
			"Usage: manager show commands\n"
			"	Prints a listing of all the available Asterisk manager interface commands.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		int incoming_len = strlen(cur->action);
		if (incoming_len > name_len) {
			name_len = incoming_len;
		}
	}

	space_remaining = MGR_SHOW_TERMINAL_WIDTH - name_len - 4;
	if (space_remaining < 0) {
		space_remaining = 0;
	}

	ast_cli(a->fd, HSMC_FORMAT, name_len, name_len, "Action", space_remaining, "Synopsis");
	ast_cli(a->fd, HSMC_FORMAT, name_len, name_len, "------", space_remaining, "--------");

	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		ast_cli(a->fd, HSMC_FORMAT, name_len, name_len, cur->action, space_remaining, cur->synopsis);
	}
	AST_RWLIST_UNLOCK(&actions);

	return CLI_SUCCESS;
}

/*! \brief CLI command manager list connected */
static char *handle_showmanconn(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	time_t now = time(NULL);
#define HSMCONN_FORMAT1 "  %-15.15s  %-55.55s  %-10.10s  %-10.10s  %-8.8s  %-8.8s  %-5.5s  %-5.5s\n"
#define HSMCONN_FORMAT2 "  %-15.15s  %-55.55s  %-10d  %-10d  %-8d  %-8d  %-5.5d  %-5.5d\n"
	int count = 0;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show connected";
		e->usage =
			"Usage: manager show connected\n"
			"	Prints a listing of the users that are currently connected to the\n"
			"Asterisk manager interface.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, HSMCONN_FORMAT1, "Username", "IP Address", "Start", "Elapsed", "FileDes", "HttpCnt", "Read", "Write");

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		i = ao2_iterator_init(sessions, 0);
		ao2_ref(sessions, -1);
		while ((session = ao2_iterator_next(&i))) {
			ao2_lock(session);
			ast_cli(a->fd, HSMCONN_FORMAT2, session->username,
				ast_sockaddr_stringify_addr(&session->addr),
				(int) (session->sessionstart),
				(int) (now - session->sessionstart),
				session->fd,
				session->inuse,
				session->readperm,
				session->writeperm);
			count++;
			ao2_unlock(session);
			unref_mansession(session);
		}
		ao2_iterator_destroy(&i);
	}
	ast_cli(a->fd, "%d users connected.\n", count);

	return CLI_SUCCESS;
}

/*! \brief CLI command manager list eventq */
/* Should change to "manager show connected" */
static char *handle_showmaneventq(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct eventqent *s;
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show eventq";
		e->usage =
			"Usage: manager show eventq\n"
			"	Prints a listing of all events pending in the Asterisk manger\n"
			"event queue.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	AST_RWLIST_RDLOCK(&all_events);
	AST_RWLIST_TRAVERSE(&all_events, s, eq_next) {
		ast_cli(a->fd, "Usecount: %d\n", s->usecount);
		ast_cli(a->fd, "Category: %d\n", s->category);
		ast_cli(a->fd, "Event:\n%s", s->eventdata);
	}
	AST_RWLIST_UNLOCK(&all_events);

	return CLI_SUCCESS;
}

/*! \brief CLI command manager reload */
static char *handle_manager_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager reload";
		e->usage =
			"Usage: manager reload\n"
			"       Reloads the manager configuration.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc > 2) {
		return CLI_SHOWUSAGE;
	}
	reload_manager();
	return CLI_SUCCESS;
}

static struct eventqent *advance_event(struct eventqent *e)
{
	struct eventqent *next;

	AST_RWLIST_RDLOCK(&all_events);
	if ((next = AST_RWLIST_NEXT(e, eq_next))) {
		ast_atomic_fetchadd_int(&next->usecount, 1);
		ast_atomic_fetchadd_int(&e->usecount, -1);
	}
	AST_RWLIST_UNLOCK(&all_events);
	return next;
}

#define	GET_HEADER_FIRST_MATCH	0
#define	GET_HEADER_LAST_MATCH	1
#define	GET_HEADER_SKIP_EMPTY	2

/*!
 * \brief Return a matching header value.
 *
 * \details
 * Generic function to return either the first or the last
 * matching header from a list of variables, possibly skipping
 * empty strings.
 *
 * \note At the moment there is only one use of this function in
 * this file, so we make it static.
 *
 * \note Never returns NULL.
 */
static const char *__astman_get_header(const struct message *m, char *var, int mode)
{
	int x, l = strlen(var);
	const char *result = "";

	if (!m) {
		return result;
	}

	for (x = 0; x < m->hdrcount; x++) {
		const char *h = m->headers[x];
		if (!strncasecmp(var, h, l) && h[l] == ':') {
			const char *value = h + l + 1;
			value = ast_skip_blanks(value); /* ignore leading spaces in the value */
			/* found a potential candidate */
			if ((mode & GET_HEADER_SKIP_EMPTY) && ast_strlen_zero(value)) {
				continue;	/* not interesting */
			}
			if (mode & GET_HEADER_LAST_MATCH) {
				result = value;	/* record the last match so far */
			} else {
				return value;
			}
		}
	}

	return result;
}

/*!
 * \brief Return the first matching variable from an array.
 *
 * \note This is the legacy function and is implemented in
 * therms of __astman_get_header().
 *
 * \note Never returns NULL.
 */
const char *astman_get_header(const struct message *m, char *var)
{
	return __astman_get_header(m, var, GET_HEADER_FIRST_MATCH);
}

/*!
 * \internal
 * \brief Process one "Variable:" header value string.
 *
 * \param head Current list of AMI variables to get new values added.
 * \param hdr_val Header value string to process.
 *
 * \return New variable list head.
 */
static struct ast_variable *man_do_variable_value(struct ast_variable *head, const char *hdr_val)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vars)[64];
	);

	hdr_val = ast_skip_blanks(hdr_val); /* ignore leading spaces in the value */
	parse = ast_strdupa(hdr_val);

	/* Break the header value string into name=val pair items. */
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc) {
		int y;

		/* Process each name=val pair item. */
		for (y = 0; y < args.argc; y++) {
			struct ast_variable *cur;
			char *var;
			char *val;

			if (!args.vars[y]) {
				continue;
			}
			var = val = args.vars[y];
			strsep(&val, "=");

			/* XXX We may wish to trim whitespace from the strings. */
			if (!val || ast_strlen_zero(var)) {
				continue;
			}

			/* Create new variable list node and prepend it to the list. */
			cur = ast_variable_new(var, val, "");
			if (cur) {
				cur->next = head;
				head = cur;
			}
		}
	}

	return head;
}

struct ast_variable *astman_get_variables(const struct message *m)
{
	return astman_get_variables_order(m, ORDER_REVERSE);
}

struct ast_variable *astman_get_variables_order(const struct message *m,
	enum variable_orders order)
{
	int varlen;
	int x;
	struct ast_variable *head = NULL;

	static const char var_hdr[] = "Variable:";

	/* Process all "Variable:" headers. */
	varlen = strlen(var_hdr);
	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp(var_hdr, m->headers[x], varlen)) {
			continue;
		}
		head = man_do_variable_value(head, m->headers[x] + varlen);
	}

	if (order == ORDER_NATURAL) {
		head = ast_variables_reverse(head);
	}

	return head;
}

/*! \brief access for hooks to send action messages to ami */
int ast_hook_send_action(struct manager_custom_hook *hook, const char *msg)
{
	const char *action;
	int ret = 0;
	struct manager_action *act_found;
	struct mansession s = {.session = NULL, };
	struct message m = { 0 };
	char *dup_str;
	char *src;
	int x = 0;
	int curlen;

	if (hook == NULL) {
		return -1;
	}

	/* Create our own copy of the AMI action msg string. */
	src = dup_str = ast_strdup(msg);
	if (!dup_str) {
		return -1;
	}

	/* convert msg string to message struct */
	curlen = strlen(src);
	for (x = 0; x < curlen; x++) {
		int cr;	/* set if we have \r */
		if (src[x] == '\r' && x+1 < curlen && src[x+1] == '\n')
			cr = 2;	/* Found. Update length to include \r\n */
		else if (src[x] == '\n')
			cr = 1;	/* also accept \n only */
		else
			continue;
		/* don't keep empty lines */
		if (x && m.hdrcount < ARRAY_LEN(m.headers)) {
			/* ... but trim \r\n and terminate the header string */
			src[x] = '\0';
			m.headers[m.hdrcount++] = src;
		}
		x += cr;
		curlen -= x;		/* remaining size */
		src += x;		/* update pointer */
		x = -1;			/* reset loop */
	}

	action = astman_get_header(&m, "Action");
	if (strcasecmp(action, "login")) {
		act_found = action_find(action);
		if (act_found) {
			/*
			 * we have to simulate a session for this action request
			 * to be able to pass it down for processing
			 * This is necessary to meet the previous design of manager.c
			 */
			s.hook = hook;
			s.f = (void*)1; /* set this to something so our request will make it through all functions that test it*/

			ao2_lock(act_found);
			if (act_found->registered && act_found->func) {
				if (act_found->module) {
					ast_module_ref(act_found->module);
				}
				ao2_unlock(act_found);
				ret = act_found->func(&s, &m);
				ao2_lock(act_found);
				if (act_found->module) {
					ast_module_unref(act_found->module);
				}
			} else {
				ret = -1;
			}
			ao2_unlock(act_found);
			ao2_t_ref(act_found, -1, "done with found action object");
		}
	}
	ast_free(dup_str);
	return ret;
}


/*!
 * helper function to send a string to the socket.
 * Return -1 on error (e.g. buffer full).
 */
static int send_string(struct mansession *s, char *string)
{
	int res;
	FILE *f = s->f ? s->f : s->session->f;
	int fd = s->f ? s->fd : s->session->fd;

	/* It's a result from one of the hook's action invocation */
	if (s->hook) {
		/*
		 * to send responses, we're using the same function
		 * as for receiving events. We call the event "HookResponse"
		 */
		s->hook->helper(EVENT_FLAG_HOOKRESPONSE, "HookResponse", string);
		return 0;
	}

	if ((res = ast_careful_fwrite(f, fd, string, strlen(string), s->session->writetimeout))) {
		s->write_error = 1;
	}

	return res;
}

/*!
 * \brief thread local buffer for astman_append
 *
 * \note This can not be defined within the astman_append() function
 *       because it declares a couple of functions that get used to
 *       initialize the thread local storage key.
 */
AST_THREADSTORAGE(astman_append_buf);

AST_THREADSTORAGE(userevent_buf);

/*! \brief initial allocated size for the astman_append_buf and astman_send_*_va */
#define ASTMAN_APPEND_BUF_INITSIZE   256

/*!
 * utility functions for creating AMI replies
 */
void astman_append(struct mansession *s, const char *fmt, ...)
{
	va_list ap;
	struct ast_str *buf;

	if (!(buf = ast_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE))) {
		return;
	}

	va_start(ap, fmt);
	ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	if (s->f != NULL || s->session->f != NULL) {
		send_string(s, ast_str_buffer(buf));
	} else {
		ast_verbose("fd == -1 in astman_append, should not happen\n");
	}
}

/*! \note NOTE: XXX this comment is unclear and possibly wrong.
   Callers of astman_send_error(), astman_send_response() or astman_send_ack() must EITHER
   hold the session lock _or_ be running in an action callback (in which case s->session->busy will
   be non-zero). In either of these cases, there is no need to lock-protect the session's
   fd, since no other output will be sent (events will be queued), and no input will
   be read until either the current action finishes or get_input() obtains the session
   lock.
 */

/*! \todo XXX MSG_MOREDATA should go to a header file. */
#define MSG_MOREDATA	((char *)astman_send_response)

/*! \brief send a response with an optional message,
 * and terminate it with an empty line.
 * m is used only to grab the 'ActionID' field.
 *
 * Use the explicit constant MSG_MOREDATA to remove the empty line.
 * XXX MSG_MOREDATA should go to a header file.
 */
static void astman_send_response_full(struct mansession *s, const struct message *m, char *resp, char *msg, char *listflag)
{
	const char *id = astman_get_header(m, "ActionID");

	astman_append(s, "Response: %s\r\n", resp);
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
	if (listflag) {
		astman_append(s, "EventList: %s\r\n", listflag);	/* Start, complete, cancelled */
	}
	if (msg == MSG_MOREDATA) {
		return;
	} else if (msg) {
		astman_append(s, "Message: %s\r\n\r\n", msg);
	} else {
		astman_append(s, "\r\n");
	}
}

void astman_send_response(struct mansession *s, const struct message *m, char *resp, char *msg)
{
	astman_send_response_full(s, m, resp, msg, NULL);
}

void astman_send_error(struct mansession *s, const struct message *m, char *error)
{
	astman_send_response_full(s, m, "Error", error, NULL);
}

void astman_send_error_va(struct mansession *s, const struct message *m, const char *fmt, ...)
{
	va_list ap;
	struct ast_str *buf;
	char *msg;

	if (!(buf = ast_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE))) {
		return;
	}

	va_start(ap, fmt);
	ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	/* astman_append will use the same underlying buffer, so copy the message out
	 * before sending the response */
	msg = ast_str_buffer(buf);
	if (msg) {
		msg = ast_strdupa(msg);
	}
	astman_send_response_full(s, m, "Error", msg, NULL);
}

void astman_send_ack(struct mansession *s, const struct message *m, char *msg)
{
	astman_send_response_full(s, m, "Success", msg, NULL);
}

static void astman_start_ack(struct mansession *s, const struct message *m)
{
	astman_send_response_full(s, m, "Success", MSG_MOREDATA, NULL);
}

void astman_send_listack(struct mansession *s, const struct message *m, char *msg, char *listflag)
{
	astman_send_response_full(s, m, "Success", msg, listflag);
}

/*! \brief Lock the 'mansession' structure. */
static void mansession_lock(struct mansession *s)
{
	ast_mutex_lock(&s->lock);
}

/*! \brief Unlock the 'mansession' structure. */
static void mansession_unlock(struct mansession *s)
{
	ast_mutex_unlock(&s->lock);
}

/*! \brief
   Rather than braindead on,off this now can also accept a specific int mask value
   or a ',' delim list of mask strings (the same as manager.conf) -anthm
*/
static int set_eventmask(struct mansession *s, const char *eventmask)
{
	int maskint = strings_to_mask(eventmask);

	ao2_lock(s->session);
	if (maskint >= 0) {
		s->session->send_events = maskint;
	}
	ao2_unlock(s->session);

	return maskint;
}

static enum ast_transport mansession_get_transport(const struct mansession *s)
{
	return s->tcptls_session->parent->tls_cfg ? AST_TRANSPORT_TLS :
			AST_TRANSPORT_TCP;
}

static void report_invalid_user(const struct mansession *s, const char *username)
{
	char session_id[32];
	struct ast_security_event_inval_acct_id inval_acct_id = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_ACCT_ID,
		.common.version    = AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION,
		.common.service    = "AMI",
		.common.account_id = username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s);

	ast_security_event_report(AST_SEC_EVT(&inval_acct_id));
}

static void report_failed_acl(const struct mansession *s, const char *username)
{
	char session_id[32];
	struct ast_security_event_failed_acl failed_acl_event = {
		.common.event_type = AST_SECURITY_EVENT_FAILED_ACL,
		.common.version    = AST_SECURITY_EVENT_FAILED_ACL_VERSION,
		.common.service    = "AMI",
		.common.account_id = username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&failed_acl_event));
}

static void report_inval_password(const struct mansession *s, const char *username)
{
	char session_id[32];
	struct ast_security_event_inval_password inval_password = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_PASSWORD,
		.common.version    = AST_SECURITY_EVENT_INVAL_PASSWORD_VERSION,
		.common.service    = "AMI",
		.common.account_id = username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&inval_password));
}

static void report_auth_success(const struct mansession *s)
{
	char session_id[32];
	struct ast_security_event_successful_auth successful_auth = {
		.common.event_type = AST_SECURITY_EVENT_SUCCESSFUL_AUTH,
		.common.version    = AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&successful_auth));
}

static void report_req_not_allowed(const struct mansession *s, const char *action)
{
	char session_id[32];
	char request_type[64];
	struct ast_security_event_req_not_allowed req_not_allowed = {
		.common.event_type = AST_SECURITY_EVENT_REQ_NOT_ALLOWED,
		.common.version    = AST_SECURITY_EVENT_REQ_NOT_ALLOWED_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,

		.request_type      = request_type,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);
	snprintf(request_type, sizeof(request_type), "Action: %s", action);

	ast_security_event_report(AST_SEC_EVT(&req_not_allowed));
}

static void report_req_bad_format(const struct mansession *s, const char *action)
{
	char session_id[32];
	char request_type[64];
	struct ast_security_event_req_bad_format req_bad_format = {
		.common.event_type = AST_SECURITY_EVENT_REQ_BAD_FORMAT,
		.common.version    = AST_SECURITY_EVENT_REQ_BAD_FORMAT_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,

		.request_type      = request_type,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);
	snprintf(request_type, sizeof(request_type), "Action: %s", action);

	ast_security_event_report(AST_SEC_EVT(&req_bad_format));
}

static void report_failed_challenge_response(const struct mansession *s,
		const char *response, const char *expected_response)
{
	char session_id[32];
	struct ast_security_event_chal_resp_failed chal_resp_failed = {
		.common.event_type = AST_SECURITY_EVENT_CHAL_RESP_FAILED,
		.common.version    = AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,

		.challenge         = s->session->challenge,
		.response          = response,
		.expected_response = expected_response,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&chal_resp_failed));
}

static void report_session_limit(const struct mansession *s)
{
	char session_id[32];
	struct ast_security_event_session_limit session_limit = {
		.common.event_type = AST_SECURITY_EVENT_SESSION_LIMIT,
		.common.version    = AST_SECURITY_EVENT_SESSION_LIMIT_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&session_limit));
}

/*
 * Here we start with action_ handlers for AMI actions,
 * and the internal functions used by them.
 * Generally, the handlers are called action_foo()
 */

/* helper function for action_login() */
static int authenticate(struct mansession *s, const struct message *m)
{
	const char *username = astman_get_header(m, "Username");
	const char *password = astman_get_header(m, "Secret");
	int error = -1;
	struct ast_manager_user *user = NULL;
	regex_t *regex_filter;
	struct ao2_iterator filter_iter;

	if (ast_strlen_zero(username)) {	/* missing username */
		return -1;
	}

	/* locate user in locked state */
	AST_RWLIST_WRLOCK(&users);

	if (!(user = get_manager_by_name_locked(username))) {
		report_invalid_user(s, username);
		ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_sockaddr_stringify_addr(&s->session->addr), username);
	} else if (user->acl && (ast_apply_acl(user->acl, &s->session->addr, "Manager User ACL: ") == AST_SENSE_DENY)) {
		report_failed_acl(s, username);
		ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_sockaddr_stringify_addr(&s->session->addr), username);
	} else if (!strcasecmp(astman_get_header(m, "AuthType"), "MD5")) {
		const char *key = astman_get_header(m, "Key");
		if (!ast_strlen_zero(key) && !ast_strlen_zero(s->session->challenge) && user->secret) {
			int x;
			int len = 0;
			char md5key[256] = "";
			struct MD5Context md5;
			unsigned char digest[16];

			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *) s->session->challenge, strlen(s->session->challenge));
			MD5Update(&md5, (unsigned char *) user->secret, strlen(user->secret));
			MD5Final(digest, &md5);
			for (x = 0; x < 16; x++)
				len += sprintf(md5key + len, "%2.2x", (unsigned)digest[x]);
			if (!strcmp(md5key, key)) {
				error = 0;
			} else {
				report_failed_challenge_response(s, key, md5key);
			}
		} else {
			ast_debug(1, "MD5 authentication is not possible.  challenge: '%s'\n",
				S_OR(s->session->challenge, ""));
		}
	} else if (user->secret) {
		if (!strcmp(password, user->secret)) {
			error = 0;
		} else {
			report_inval_password(s, username);
		}
	}

	if (error) {
		ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", ast_sockaddr_stringify_addr(&s->session->addr), username);
		AST_RWLIST_UNLOCK(&users);
		return -1;
	}

	/* auth complete */

	/* All of the user parameters are copied to the session so that in the event
	* of a reload and a configuration change, the session parameters are not
	* changed. */
	ast_copy_string(s->session->username, username, sizeof(s->session->username));
	s->session->readperm = user->readperm;
	s->session->writeperm = user->writeperm;
	s->session->writetimeout = user->writetimeout;
	if (user->chanvars) {
		s->session->chanvars = ast_variables_dup(user->chanvars);
	}

	filter_iter = ao2_iterator_init(user->whitefilters, 0);
	while ((regex_filter = ao2_iterator_next(&filter_iter))) {
		ao2_t_link(s->session->whitefilters, regex_filter, "add white user filter to session");
		ao2_t_ref(regex_filter, -1, "remove iterator ref");
	}
	ao2_iterator_destroy(&filter_iter);

	filter_iter = ao2_iterator_init(user->blackfilters, 0);
	while ((regex_filter = ao2_iterator_next(&filter_iter))) {
		ao2_t_link(s->session->blackfilters, regex_filter, "add black user filter to session");
		ao2_t_ref(regex_filter, -1, "remove iterator ref");
	}
	ao2_iterator_destroy(&filter_iter);

	s->session->sessionstart = time(NULL);
	s->session->sessionstart_tv = ast_tvnow();
	set_eventmask(s, astman_get_header(m, "Events"));

	report_auth_success(s);

	AST_RWLIST_UNLOCK(&users);
	return 0;
}

static int action_ping(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	struct timeval now = ast_tvnow();

	astman_append(s, "Response: Success\r\n");
	if (!ast_strlen_zero(actionid)){
		astman_append(s, "ActionID: %s\r\n", actionid);
	}
	astman_append(
		s,
		"Ping: Pong\r\n"
		"Timestamp: %ld.%06lu\r\n"
		"\r\n",
		(long) now.tv_sec, (unsigned long) now.tv_usec);
	return 0;
}

static int action_getconfig(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	const char *category = astman_get_header(m, "Category");
	const char *filter = astman_get_header(m, "Filter");
	const char *category_name;
	int catcount = 0;
	int lineno = 0;
	struct ast_category *cur_category = NULL;
	struct ast_variable *v;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	cfg = ast_config_load2(fn, "manager", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}

	astman_start_ack(s, m);
	while ((cur_category = ast_category_browse_filtered(cfg, category, cur_category, filter))) {
		struct ast_str *templates;

		category_name = ast_category_get_name(cur_category);
		lineno = 0;
		astman_append(s, "Category-%06d: %s\r\n", catcount, category_name);

		if (ast_category_is_template(cur_category)) {
			astman_append(s, "IsTemplate-%06d: %d\r\n", catcount, 1);
		}

		if ((templates = ast_category_get_templates(cur_category))
			&& ast_str_strlen(templates) > 0) {
			astman_append(s, "Templates-%06d: %s\r\n", catcount, ast_str_buffer(templates));
			ast_free(templates);
		}

		for (v = ast_category_first(cur_category); v; v = v->next) {
			astman_append(s, "Line-%06d-%06d: %s=%s\r\n", catcount, lineno++, v->name, v->value);
		}

		catcount++;
	}

	if (!ast_strlen_zero(category) && catcount == 0) { /* TODO: actually, a config with no categories doesn't even get loaded */
		astman_append(s, "No categories found\r\n");
	}

	ast_config_destroy(cfg);
	astman_append(s, "\r\n");

	return 0;
}

static int action_listcategories(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	const char *match = astman_get_header(m, "Match");
	struct ast_category *category = NULL;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };
	int catcount = 0;

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	if (!(cfg = ast_config_load2(fn, "manager", config_flags))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}

	astman_start_ack(s, m);
	while ((category = ast_category_browse_filtered(cfg, NULL, category, match))) {
		astman_append(s, "Category-%06d: %s\r\n", catcount, ast_category_get_name(category));
		catcount++;
	}

	if (catcount == 0) { /* TODO: actually, a config with no categories doesn't even get loaded */
		astman_append(s, "Error: no categories found\r\n");
	}

	ast_config_destroy(cfg);
	astman_append(s, "\r\n");

	return 0;
}

/*! The amount of space in out must be at least ( 2 * strlen(in) + 1 ) */
static void json_escape(char *out, const char *in)
{
	for (; *in; in++) {
		if (*in == '\\' || *in == '\"') {
			*out++ = '\\';
		}
		*out++ = *in;
	}
	*out = '\0';
}

/*!
 * \internal
 * \brief Append a JSON escaped string to the manager stream.
 *
 * \param s AMI stream to append a string.
 * \param str String to append to the stream after JSON escaping it.
 *
 * \return Nothing
 */
static void astman_append_json(struct mansession *s, const char *str)
{
	char *buf;

	buf = ast_alloca(2 * strlen(str) + 1);
	json_escape(buf, str);
	astman_append(s, "%s", buf);
}

static int action_getconfigjson(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	const char *filter = astman_get_header(m, "Filter");
	const char *category = astman_get_header(m, "Category");
	struct ast_category *cur_category = NULL;
	const char *category_name;
	struct ast_variable *v;
	int comma1 = 0;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	if (!(cfg = ast_config_load2(fn, "manager", config_flags))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}

	astman_start_ack(s, m);
	astman_append(s, "JSON: {");
	while ((cur_category = ast_category_browse_filtered(cfg, category, cur_category, filter))) {
		int comma2 = 0;
		struct ast_str *templates;

		category_name = ast_category_get_name(cur_category);
		astman_append(s, "%s\"", comma1 ? "," : "");
		astman_append_json(s, category_name);
		astman_append(s, "\":[");
		comma1 = 1;

		if (ast_category_is_template(cur_category)) {
			astman_append(s, "istemplate:1");
			comma2 = 1;
		}

		if ((templates = ast_category_get_templates(cur_category))
			&& ast_str_strlen(templates) > 0) {
			astman_append(s, "%s", comma2 ? "," : "");
			astman_append(s, "templates:\"%s\"", ast_str_buffer(templates));
			ast_free(templates);
			comma2 = 1;
		}

		for (v = ast_category_first(cur_category); v; v = v->next) {
			astman_append(s, "%s\"", comma2 ? "," : "");
			astman_append_json(s, v->name);
			astman_append(s, "\":\"");
			astman_append_json(s, v->value);
			astman_append(s, "\"");
			comma2 = 1;
		}

		astman_append(s, "]");
	}
	astman_append(s, "}\r\n\r\n");

	ast_config_destroy(cfg);

	return 0;
}

/*! \brief helper function for action_updateconfig */
static enum error_type handle_updates(struct mansession *s, const struct message *m, struct ast_config *cfg, const char *dfn)
{
	int x;
	char hdr[40];
	const char *action, *cat, *var, *value, *match, *line, *options;
	struct ast_variable *v;
	struct ast_str *str1 = ast_str_create(16), *str2 = ast_str_create(16);
	enum error_type result = 0;

	for (x = 0; x < 100000; x++) {	/* 100000 = the max number of allowed updates + 1 */
		unsigned int object = 0;
		char *dupoptions;
		int allowdups = 0;
		int istemplate = 0;
		int ignoreerror = 0;
		char *inherit = NULL;
		char *catfilter = NULL;
		char *token;
		int foundvar = 0;
		int foundcat = 0;
		struct ast_category *category = NULL;

		snprintf(hdr, sizeof(hdr), "Action-%06d", x);
		action = astman_get_header(m, hdr);
		if (ast_strlen_zero(action))		/* breaks the for loop if no action header */
			break;							/* this could cause problems if actions come in misnumbered */

		snprintf(hdr, sizeof(hdr), "Cat-%06d", x);
		cat = astman_get_header(m, hdr);
		if (ast_strlen_zero(cat)) {		/* every action needs a category */
			result =  UNSPECIFIED_CATEGORY;
			break;
		}

		snprintf(hdr, sizeof(hdr), "Var-%06d", x);
		var = astman_get_header(m, hdr);

		snprintf(hdr, sizeof(hdr), "Value-%06d", x);
		value = astman_get_header(m, hdr);

		if (!ast_strlen_zero(value) && *value == '>') {
			object = 1;
			value++;
		}

		snprintf(hdr, sizeof(hdr), "Match-%06d", x);
		match = astman_get_header(m, hdr);

		snprintf(hdr, sizeof(hdr), "Line-%06d", x);
		line = astman_get_header(m, hdr);

		snprintf(hdr, sizeof(hdr), "Options-%06d", x);
		options = astman_get_header(m, hdr);
		if (!ast_strlen_zero(options)) {
			dupoptions = ast_strdupa(options);
			while ((token = ast_strsep(&dupoptions, ',', AST_STRSEP_STRIP))) {
				if (!strcasecmp("allowdups", token)) {
					allowdups = 1;
					continue;
				}
				if (!strcasecmp("template", token)) {
					istemplate = 1;
					continue;
				}
				if (!strcasecmp("ignoreerror", token)) {
					ignoreerror = 1;
					continue;
				}
				if (ast_begins_with(token, "inherit")) {
					char *c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					if (c) {
						inherit = ast_strdupa(c);
					}
					continue;
				}
				if (ast_begins_with(token, "catfilter")) {
					char *c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					if (c) {
						catfilter = ast_strdupa(c);
					}
					continue;
				}
			}
		}

		if (!strcasecmp(action, "newcat")) {
			struct ast_category *template;
			char *tmpl_name = NULL;

			if (!allowdups) {
				if (ast_category_get(cfg, cat, "TEMPLATES=include")) {
					if (ignoreerror) {
						continue;
					} else {
						result = FAILURE_NEWCAT;	/* already exist */
						break;
					}
				}
			}

			if (istemplate) {
				category = ast_category_new_template(cat, dfn, -1);
			} else {
				category = ast_category_new(cat, dfn, -1);
			}

			if (!category) {
				result = FAILURE_ALLOCATION;
				break;
			}

			if (inherit) {
				while ((tmpl_name = ast_strsep(&inherit, ',', AST_STRSEP_STRIP))) {
					if ((template = ast_category_get(cfg, tmpl_name, "TEMPLATES=restrict"))) {
						ast_category_inherit(category, template);
					} else {
						ast_category_destroy(category);
						category = NULL;
						result = FAILURE_TEMPLATE;	/* template not found */
						break;
					}
				}
			}

			if (category != NULL) {
				if (ast_strlen_zero(match)) {
					ast_category_append(cfg, category);
				} else {
					if (ast_category_insert(cfg, category, match)) {
						ast_category_destroy(category);
						result = FAILURE_NEWCAT;
						break;
					}
				}
			}
		} else if (!strcasecmp(action, "renamecat")) {
			if (ast_strlen_zero(value)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				ast_category_rename(category, value);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "delcat")) {
			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				category = ast_category_delete(cfg, category);
				foundcat = 1;
			}

			if (!foundcat && !ignoreerror) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "emptycat")) {
			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				ast_category_empty(category);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "update")) {
			if (ast_strlen_zero(var)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			foundvar = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!ast_variable_update(category, var, value, match, object)) {
					foundvar = 1;
				}
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}

			if (!foundvar) {
				result = FAILURE_UPDATE;
				break;
			}
		} else if (!strcasecmp(action, "delete")) {
			if ((ast_strlen_zero(var) && ast_strlen_zero(line))) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			foundvar = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!ast_variable_delete(category, var, match, line)) {
					foundvar = 1;
				}
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}

			if (!foundvar && !ignoreerror) {
				result = FAILURE_UPDATE;
				break;
			}
		} else if (!strcasecmp(action, "append")) {
			if (ast_strlen_zero(var)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!(v = ast_variable_new(var, value, dfn))) {
					result = FAILURE_ALLOCATION;
					break;
				}
				if (object || (match && !strcasecmp(match, "object"))) {
					v->object = 1;
				}
				ast_variable_append(category, v);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "insert")) {
			if (ast_strlen_zero(var) || ast_strlen_zero(line)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!(v = ast_variable_new(var, value, dfn))) {
					result = FAILURE_ALLOCATION;
					break;
				}
				ast_variable_insert(category, v, line);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		}
		else {
			ast_log(LOG_WARNING, "Action-%06d: %s not handled\n", x, action);
			result = UNKNOWN_ACTION;
			break;
		}
	}
	ast_free(str1);
	ast_free(str2);
	return result;
}

static int action_updateconfig(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *sfn = astman_get_header(m, "SrcFilename");
	const char *dfn = astman_get_header(m, "DstFilename");
	int res;
	const char *rld = astman_get_header(m, "Reload");
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };
	enum error_type result;

	if (ast_strlen_zero(sfn) || ast_strlen_zero(dfn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (!(cfg = ast_config_load2(sfn, "manager", config_flags))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}
	result = handle_updates(s, m, cfg, dfn);
	if (!result) {
		ast_include_rename(cfg, sfn, dfn); /* change the include references from dfn to sfn, so things match up */
		res = ast_config_text_file_save(dfn, cfg, "Manager");
		ast_config_destroy(cfg);
		if (res) {
			astman_send_error(s, m, "Save of config failed");
			return 0;
		}
		astman_send_ack(s, m, NULL);
		if (!ast_strlen_zero(rld)) {
			if (ast_true(rld)) {
				rld = NULL;
			}
			ast_module_reload(rld);
		}
	} else {
		ast_config_destroy(cfg);
		switch(result) {
		case UNKNOWN_ACTION:
			astman_send_error(s, m, "Unknown action command");
			break;
		case UNKNOWN_CATEGORY:
			astman_send_error(s, m, "Given category does not exist");
			break;
		case UNSPECIFIED_CATEGORY:
			astman_send_error(s, m, "Category not specified");
			break;
		case UNSPECIFIED_ARGUMENT:
			astman_send_error(s, m, "Problem with category, value, or line (if required)");
			break;
		case FAILURE_ALLOCATION:
			astman_send_error(s, m, "Memory allocation failure, this should not happen");
			break;
		case FAILURE_NEWCAT:
			astman_send_error(s, m, "Create category did not complete successfully");
			break;
		case FAILURE_DELCAT:
			astman_send_error(s, m, "Delete category did not complete successfully");
			break;
		case FAILURE_EMPTYCAT:
			astman_send_error(s, m, "Empty category did not complete successfully");
			break;
		case FAILURE_UPDATE:
			astman_send_error(s, m, "Update did not complete successfully");
			break;
		case FAILURE_DELETE:
			astman_send_error(s, m, "Delete did not complete successfully");
			break;
		case FAILURE_APPEND:
			astman_send_error(s, m, "Append did not complete successfully");
			break;
		case FAILURE_TEMPLATE:
			astman_send_error(s, m, "Template category not found");
			break;
		}
	}
	return 0;
}

static int action_createconfig(struct mansession *s, const struct message *m)
{
	int fd;
	const char *fn = astman_get_header(m, "Filename");
	struct ast_str *filepath = ast_str_alloca(PATH_MAX);
	ast_str_set(&filepath, 0, "%s/", ast_config_AST_CONFIG_DIR);
	ast_str_append(&filepath, 0, "%s", fn);

	if ((fd = open(ast_str_buffer(filepath), O_CREAT | O_EXCL, AST_FILE_MODE)) != -1) {
		close(fd);
		astman_send_ack(s, m, "New configuration file created successfully");
	} else {
		astman_send_error(s, m, strerror(errno));
	}

	return 0;
}

static int action_waitevent(struct mansession *s, const struct message *m)
{
	const char *timeouts = astman_get_header(m, "Timeout");
	int timeout = -1;
	int x;
	int needexit = 0;
	const char *id = astman_get_header(m, "ActionID");
	char idText[256];

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	} else {
		idText[0] = '\0';
	}

	if (!ast_strlen_zero(timeouts)) {
		sscanf(timeouts, "%30i", &timeout);
		if (timeout < -1) {
			timeout = -1;
		}
		/* XXX maybe put an upper bound, or prevent the use of 0 ? */
	}

	ao2_lock(s->session);
	if (s->session->waiting_thread != AST_PTHREADT_NULL) {
		pthread_kill(s->session->waiting_thread, SIGURG);
	}

	if (s->session->managerid) { /* AMI-over-HTTP session */
		/*
		 * Make sure the timeout is within the expire time of the session,
		 * as the client will likely abort the request if it does not see
		 * data coming after some amount of time.
		 */
		time_t now = time(NULL);
		int max = s->session->sessiontimeout - now - 10;

		if (max < 0) {	/* We are already late. Strange but possible. */
			max = 0;
		}
		if (timeout < 0 || timeout > max) {
			timeout = max;
		}
		if (!s->session->send_events) {	/* make sure we record events */
			s->session->send_events = -1;
		}
	}
	ao2_unlock(s->session);

	/* XXX should this go inside the lock ? */
	s->session->waiting_thread = pthread_self();	/* let new events wake up this thread */
	ast_debug(1, "Starting waiting for an event!\n");

	for (x = 0; x < timeout || timeout < 0; x++) {
		ao2_lock(s->session);
		if (AST_RWLIST_NEXT(s->session->last_ev, eq_next)) {
			needexit = 1;
		}
		/* We can have multiple HTTP session point to the same mansession entry.
		 * The way we deal with it is not very nice: newcomers kick out the previous
		 * HTTP session. XXX this needs to be improved.
		 */
		if (s->session->waiting_thread != pthread_self()) {
			needexit = 1;
		}
		if (s->session->needdestroy) {
			needexit = 1;
		}
		ao2_unlock(s->session);
		if (needexit) {
			break;
		}
		if (s->session->managerid == 0) {	/* AMI session */
			if (ast_wait_for_input(s->session->fd, 1000)) {
				break;
			}
		} else {	/* HTTP session */
			sleep(1);
		}
	}
	ast_debug(1, "Finished waiting for an event!\n");

	ao2_lock(s->session);
	if (s->session->waiting_thread == pthread_self()) {
		struct eventqent *eqe = s->session->last_ev;
		astman_send_response(s, m, "Success", "Waiting for Event completed.");
		while ((eqe = advance_event(eqe))) {
			if (((s->session->readperm & eqe->category) == eqe->category)
				&& ((s->session->send_events & eqe->category) == eqe->category)
				&& match_filter(s, eqe->eventdata)) {
				astman_append(s, "%s", eqe->eventdata);
			}
			s->session->last_ev = eqe;
		}
		astman_append(s,
			"Event: WaitEventComplete\r\n"
			"%s"
			"\r\n", idText);
		s->session->waiting_thread = AST_PTHREADT_NULL;
	} else {
		ast_debug(1, "Abandoning event request!\n");
	}
	ao2_unlock(s->session);

	return 0;
}

static int action_listcommands(struct mansession *s, const struct message *m)
{
	struct manager_action *cur;
	struct ast_str *temp = ast_str_alloca(256);

	astman_start_ack(s, m);
	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		if ((s->session->writeperm & cur->authority) || cur->authority == 0) {
			astman_append(s, "%s: %s (Priv: %s)\r\n",
				cur->action, cur->synopsis, authority_to_str(cur->authority, &temp));
		}
	}
	AST_RWLIST_UNLOCK(&actions);
	astman_append(s, "\r\n");

	return 0;
}

static int action_events(struct mansession *s, const struct message *m)
{
	const char *mask = astman_get_header(m, "EventMask");
	int res, x;
	const char *id = astman_get_header(m, "ActionID");
	char id_text[256];

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}

	res = set_eventmask(s, mask);
	if (broken_events_action) {
		/* if this option is set we should not return a response on
		 * error, or when all events are set */

		if (res > 0) {
			for (x = 0; x < ARRAY_LEN(perms); x++) {
				if (!strcasecmp(perms[x].label, "all") && res == perms[x].num) {
					return 0;
				}
			}
			astman_append(s, "Response: Success\r\n%s"
					 "Events: On\r\n\r\n", id_text);
		} else if (res == 0)
			astman_append(s, "Response: Success\r\n%s"
					 "Events: Off\r\n\r\n", id_text);
		return 0;
	}

	if (res > 0)
		astman_append(s, "Response: Success\r\n%s"
				 "Events: On\r\n\r\n", id_text);
	else if (res == 0)
		astman_append(s, "Response: Success\r\n%s"
				 "Events: Off\r\n\r\n", id_text);
	else
		astman_send_error(s, m, "Invalid event mask");

	return 0;
}

static int action_logoff(struct mansession *s, const struct message *m)
{
	astman_send_response(s, m, "Goodbye", "Thanks for all the fish.");
	return -1;
}

static int action_login(struct mansession *s, const struct message *m)
{

	/* still authenticated - don't process again */
	if (s->session->authenticated) {
		astman_send_ack(s, m, "Already authenticated");
		return 0;
	}

	if (authenticate(s, m)) {
		sleep(1);
		astman_send_error(s, m, "Authentication failed");
		return -1;
	}
	s->session->authenticated = 1;
	ast_atomic_fetchadd_int(&unauth_sessions, -1);
	if (manager_displayconnects(s->session)) {
		ast_verb(2, "%sManager '%s' logged on from %s\n", (s->session->managerid ? "HTTP " : ""), s->session->username, ast_sockaddr_stringify_addr(&s->session->addr));
	}
	astman_send_ack(s, m, "Authentication accepted");
	if ((s->session->send_events & EVENT_FLAG_SYSTEM)
		&& (s->session->readperm & EVENT_FLAG_SYSTEM)
		&& ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		struct ast_str *auth = ast_str_alloca(80);
		const char *cat_str = authority_to_str(EVENT_FLAG_SYSTEM, &auth);
		astman_append(s, "Event: FullyBooted\r\n"
			"Privilege: %s\r\n"
			"Status: Fully Booted\r\n\r\n", cat_str);
	}
	return 0;
}

static int action_challenge(struct mansession *s, const struct message *m)
{
	const char *authtype = astman_get_header(m, "AuthType");

	if (!strcasecmp(authtype, "MD5")) {
		if (ast_strlen_zero(s->session->challenge)) {
			snprintf(s->session->challenge, sizeof(s->session->challenge), "%ld", ast_random());
		}
		mansession_lock(s);
		astman_start_ack(s, m);
		astman_append(s, "Challenge: %s\r\n\r\n", s->session->challenge);
		mansession_unlock(s);
	} else {
		astman_send_error(s, m, "Must specify AuthType");
	}
	return 0;
}

static int action_hangup(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	int causecode = 0; /* all values <= 0 mean 'do not set hangupcause in channel' */
	const char *id = astman_get_header(m, "ActionID");
	const char *name_or_regex = astman_get_header(m, "Channel");
	const char *cause = astman_get_header(m, "Cause");
	char idText[256];
	regex_t regexbuf;
	struct ast_channel_iterator *iter = NULL;
	struct ast_str *regex_string;
	int channels_matched = 0;

	if (ast_strlen_zero(name_or_regex)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	} else {
		idText[0] = '\0';
	}

	if (!ast_strlen_zero(cause)) {
		char *endptr;
		causecode = strtol(cause, &endptr, 10);
		if (causecode < 0 || causecode > 127 || *endptr != '\0') {
			ast_log(LOG_NOTICE, "Invalid 'Cause: %s' in manager action Hangup\n", cause);
			/* keep going, better to hangup without cause than to not hang up at all */
			causecode = 0; /* do not set channel's hangupcause */
		}
	}

	/************************************************/
	/* Regular explicit match channel byname hangup */

	if (name_or_regex[0] != '/') {
		if (!(c = ast_channel_get_by_name(name_or_regex))) {
			ast_log(LOG_NOTICE, "Request to hangup non-existent channel: %s\n",
				name_or_regex);
			astman_send_error(s, m, "No such channel");
			return 0;
		}

		ast_verb(3, "%sManager '%s' from %s, hanging up channel: %s\n",
			(s->session->managerid ? "HTTP " : ""),
			s->session->username,
			ast_sockaddr_stringify_addr(&s->session->addr),
			ast_channel_name(c));

		ast_channel_softhangup_withcause_locked(c, causecode);
		c = ast_channel_unref(c);

		astman_send_ack(s, m, "Channel Hungup");

		return 0;
	}

	/***********************************************/
	/* find and hangup any channels matching regex */

	regex_string = ast_str_create(strlen(name_or_regex));
	if (!regex_string) {
		astman_send_error(s, m, "Memory Allocation Failure");
		return 0;
	}

	/* Make "/regex/" into "regex" */
	if (ast_regex_string_to_regex_pattern(name_or_regex, &regex_string) != 0) {
		astman_send_error(s, m, "Regex format invalid, Channel param should be /regex/");
		ast_free(regex_string);
		return 0;
	}

	/* if regex compilation fails, hangup fails */
	if (regcomp(&regexbuf, ast_str_buffer(regex_string), REG_EXTENDED | REG_NOSUB)) {
		astman_send_error_va(s, m, "Regex compile failed on: %s", name_or_regex);
		ast_free(regex_string);
		return 0;
	}

	astman_send_listack(s, m, "Channels hung up will follow", "start");

	iter = ast_channel_iterator_all_new();
	if (iter) {
		for (; (c = ast_channel_iterator_next(iter)); ast_channel_unref(c)) {
			if (regexec(&regexbuf, ast_channel_name(c), 0, NULL, 0)) {
				continue;
			}

			ast_verb(3, "%sManager '%s' from %s, hanging up channel: %s\n",
				(s->session->managerid ? "HTTP " : ""),
				s->session->username,
				ast_sockaddr_stringify_addr(&s->session->addr),
				ast_channel_name(c));

			ast_channel_softhangup_withcause_locked(c, causecode);
			channels_matched++;

			astman_append(s,
				"Event: ChannelHungup\r\n"
				"Channel: %s\r\n"
				"%s"
				"\r\n", ast_channel_name(c), idText);
		}
		ast_channel_iterator_destroy(iter);
	}

	regfree(&regexbuf);
	ast_free(regex_string);

	astman_append(s,
		"Event: ChannelsHungupListComplete\r\n"
		"EventList: Complete\r\n"
		"ListItems: %d\r\n"
		"%s"
		"\r\n", channels_matched, idText);

	return 0;
}

static int action_setvar(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *varname = astman_get_header(m, "Variable");
	const char *varval = astman_get_header(m, "Value");
	int res = 0;

	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	if (!ast_strlen_zero(name)) {
		if (!(c = ast_channel_get_by_name(name))) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	res = pbx_builtin_setvar_helper(c, varname, S_OR(varval, ""));

	if (c) {
		c = ast_channel_unref(c);
	}
	if (res == 0) {
		astman_send_ack(s, m, "Variable Set");
	} else {
		astman_send_error(s, m, "Variable not set");
	}
	return 0;
}

static int action_getvar(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *varname = astman_get_header(m, "Variable");
	char *varval;
	char workspace[1024];

	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	/* We don't want users with insufficient permissions using certain functions. */
	if (!(function_capable_string_allowed_with_auths(varname, s->session->writeperm))) {
		astman_send_error(s, m, "GetVar Access Forbidden: Variable");
		return 0;
	}

	if (!ast_strlen_zero(name)) {
		if (!(c = ast_channel_get_by_name(name))) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	workspace[0] = '\0';
	if (varname[strlen(varname) - 1] == ')') {
		if (!c) {
			c = ast_dummy_channel_alloc();
			if (c) {
				ast_func_read(c, (char *) varname, workspace, sizeof(workspace));
			} else
				ast_log(LOG_ERROR, "Unable to allocate bogus channel for variable substitution.  Function results may be blank.\n");
		} else {
			ast_func_read(c, (char *) varname, workspace, sizeof(workspace));
		}
		varval = workspace;
	} else {
		pbx_retrieve_variable(c, varname, &varval, workspace, sizeof(workspace), NULL);
	}

	if (c) {
		c = ast_channel_unref(c);
	}

	astman_start_ack(s, m);
	astman_append(s, "Variable: %s\r\nValue: %s\r\n\r\n", varname, S_OR(varval, ""));

	return 0;
}

/*! \brief Manager "status" command to show channels */
/* Needs documentation... */
static int action_status(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *chan_variables = astman_get_header(m, "Variables");
	const char *id = astman_get_header(m, "ActionID");
	char *variables = ast_strdupa(S_OR(chan_variables, ""));
	struct ast_str *variable_str = ast_str_create(1024);
	struct ast_str *write_transpath = ast_str_alloca(256);
	struct ast_str *read_transpath = ast_str_alloca(256);
	struct ast_channel *chan;
	char nativeformats[256];
	int channels = 0;
	int all = ast_strlen_zero(name); /* set if we want all channels */
	char id_text[256];
	struct ast_channel_iterator *it_chans = NULL;
	AST_DECLARE_APP_ARGS(vars,
		AST_APP_ARG(name)[100];
	);

	if (!variable_str) {
		astman_send_error(s, m, "Memory Allocation Failure");
		return 1;
	}

	if (!(function_capable_string_allowed_with_auths(variables, s->session->writeperm))) {
		ast_free(variable_str);
		astman_send_error(s, m, "Status Access Forbidden: Variables");
		return 0;
	}

	if (all) {
		if (!(it_chans = ast_channel_iterator_all_new())) {
			ast_free(variable_str);
			astman_send_error(s, m, "Memory Allocation Failure");
			return 1;
		}
		chan = ast_channel_iterator_next(it_chans);
	} else {
		chan = ast_channel_get_by_name(name);
		if (!chan) {
			astman_send_error(s, m, "No such channel");
			ast_free(variable_str);
			return 0;
		}
	}

	astman_send_ack(s, m, "Channel status will follow");

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}

	if (!ast_strlen_zero(chan_variables)) {
		AST_STANDARD_APP_ARGS(vars, variables);
	}

	/* if we look by name, we break after the first iteration */
	for (; chan; all ? chan = ast_channel_iterator_next(it_chans) : 0) {
		struct timeval now;
		long elapsed_seconds;
		struct ast_bridge *bridge;

		ast_channel_lock(chan);

		now = ast_tvnow();
		elapsed_seconds = ast_tvdiff_sec(now, ast_channel_creationtime(chan));

		if (!ast_strlen_zero(chan_variables)) {
			int i;
			ast_str_reset(variable_str);
			for (i = 0; i < vars.argc; i++) {
				char valbuf[512], *ret = NULL;

				if (vars.name[i][strlen(vars.name[i]) - 1] == ')') {
					if (ast_func_read(chan, vars.name[i], valbuf, sizeof(valbuf)) < 0) {
						valbuf[0] = '\0';
					}
					ret = valbuf;
				} else {
					pbx_retrieve_variable(chan, vars.name[i], &ret, valbuf, sizeof(valbuf), NULL);
				}

				ast_str_append(&variable_str, 0, "Variable: %s=%s\r\n", vars.name[i], ret);
			}
		}

		channels++;

		bridge = ast_channel_get_bridge(chan);

		astman_append(s,
			"Event: Status\r\n"
			"Privilege: Call\r\n"
			"Channel: %s\r\n"
			"ChannelState: %u\r\n"
			"ChannelStateDesc: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"ConnectedLineNum: %s\r\n"
			"ConnectedLineName: %s\r\n"
			"Accountcode: %s\r\n"
			"Context: %s\r\n"
			"Exten: %s\r\n"
			"Priority: %d\r\n"
			"Uniqueid: %s\r\n"
			"Type: %s\r\n"
			"DNID: %s\r\n"
			"EffectiveConnectedLineNum: %s\r\n"
			"EffectiveConnectedLineName: %s\r\n"
			"TimeToHangup: %ld\r\n"
			"BridgeID: %s\r\n"
			"Linkedid: %s\r\n"
			"Application: %s\r\n"
			"Data: %s\r\n"
			"Nativeformats: %s\r\n"
			"Readformat: %s\r\n"
			"Readtrans: %s\r\n"
			"Writeformat: %s\r\n"
			"Writetrans: %s\r\n"
			"Callgroup: %llu\r\n"
			"Pickupgroup: %llu\r\n"
			"Seconds: %ld\r\n"
			"%s"
			"%s"
			"\r\n",
			ast_channel_name(chan),
			ast_channel_state(chan),
			ast_state2str(ast_channel_state(chan)),
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<unknown>"),
			S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, "<unknown>"),
			S_COR(ast_channel_connected(chan)->id.number.valid, ast_channel_connected(chan)->id.number.str, "<unknown>"),
			S_COR(ast_channel_connected(chan)->id.name.valid, ast_channel_connected(chan)->id.name.str, "<unknown>"),
			ast_channel_accountcode(chan),
			ast_channel_context(chan),
			ast_channel_exten(chan),
			ast_channel_priority(chan),
			ast_channel_uniqueid(chan),
			ast_channel_tech(chan)->type,
			S_OR(ast_channel_dialed(chan)->number.str, ""),
			S_COR(ast_channel_connected_effective_id(chan).number.valid, ast_channel_connected_effective_id(chan).number.str, "<unknown>"),
			S_COR(ast_channel_connected_effective_id(chan).name.valid, ast_channel_connected_effective_id(chan).name.str, "<unknown>"),
			ast_channel_whentohangup(chan)->tv_sec,
			bridge ? bridge->uniqueid : "",
			ast_channel_linkedid(chan),
			ast_channel_appl(chan),
			ast_channel_data(chan),
			ast_getformatname_multiple(nativeformats, sizeof(nativeformats), ast_channel_nativeformats(chan)),
			ast_getformatname(ast_channel_readformat(chan)),
			ast_translate_path_to_str(ast_channel_readtrans(chan), &read_transpath),
			ast_getformatname(ast_channel_writeformat(chan)),
			ast_translate_path_to_str(ast_channel_writetrans(chan), &write_transpath),
			ast_channel_callgroup(chan),
			ast_channel_pickupgroup(chan),
			(long)elapsed_seconds,
			ast_str_buffer(variable_str),
			id_text);

		ao2_cleanup(bridge);

		ast_channel_unlock(chan);
		chan = ast_channel_unref(chan);
	}

	if (it_chans) {
		ast_channel_iterator_destroy(it_chans);
	}

	astman_append(s,
		"Event: StatusComplete\r\n"
		"%s"
		"Items: %d\r\n"
		"\r\n", id_text, channels);

	ast_free(variable_str);

	return 0;
}

static int action_sendtext(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *textmsg = astman_get_header(m, "Message");
	int res = 0;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (ast_strlen_zero(textmsg)) {
		astman_send_error(s, m, "No Message specified");
		return 0;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	res = ast_sendtext(c, textmsg);
	c = ast_channel_unref(c);

	if (res >= 0) {
		astman_send_ack(s, m, "Success");
	} else {
		astman_send_error(s, m, "Failure");
	}

	return 0;
}

/*! \brief  action_redirect: The redirect manager command */
static int action_redirect(struct mansession *s, const struct message *m)
{
	char buf[256];
	const char *name = astman_get_header(m, "Channel");
	const char *name2 = astman_get_header(m, "ExtraChannel");
	const char *exten = astman_get_header(m, "Exten");
	const char *exten2 = astman_get_header(m, "ExtraExten");
	const char *context = astman_get_header(m, "Context");
	const char *context2 = astman_get_header(m, "ExtraContext");
	const char *priority = astman_get_header(m, "Priority");
	const char *priority2 = astman_get_header(m, "ExtraPriority");
	struct ast_channel *chan;
	struct ast_channel *chan2;
	int pi = 0;
	int pi2 = 0;
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}

	if (ast_strlen_zero(context)) {
		astman_send_error(s, m, "Context not specified");
		return 0;
	}
	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "Exten not specified");
		return 0;
	}
	if (ast_strlen_zero(priority)) {
		astman_send_error(s, m, "Priority not specified");
		return 0;
	}
	if (sscanf(priority, "%30d", &pi) != 1) {
		pi = ast_findlabel_extension(NULL, context, exten, priority, NULL);
	}
	if (pi < 1) {
		astman_send_error(s, m, "Priority is invalid");
		return 0;
	}

	if (!ast_strlen_zero(name2) && !ast_strlen_zero(context2)) {
		/* We have an ExtraChannel and an ExtraContext */
		if (ast_strlen_zero(exten2)) {
			astman_send_error(s, m, "ExtraExten not specified");
			return 0;
		}
		if (ast_strlen_zero(priority2)) {
			astman_send_error(s, m, "ExtraPriority not specified");
			return 0;
		}
		if (sscanf(priority2, "%30d", &pi2) != 1) {
			pi2 = ast_findlabel_extension(NULL, context2, exten2, priority2, NULL);
		}
		if (pi2 < 1) {
			astman_send_error(s, m, "ExtraPriority is invalid");
			return 0;
		}
	}

	chan = ast_channel_get_by_name(name);
	if (!chan) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", name);
		astman_send_error(s, m, buf);
		return 0;
	}
	if (ast_check_hangup_locked(chan)) {
		astman_send_error(s, m, "Redirect failed, channel not up.");
		chan = ast_channel_unref(chan);
		return 0;
	}

	if (ast_strlen_zero(name2)) {
		/* Single channel redirect in progress. */
		res = ast_async_goto(chan, context, exten, pi);
		if (!res) {
			astman_send_ack(s, m, "Redirect successful");
		} else {
			astman_send_error(s, m, "Redirect failed");
		}
		chan = ast_channel_unref(chan);
		return 0;
	}

	chan2 = ast_channel_get_by_name(name2);
	if (!chan2) {
		snprintf(buf, sizeof(buf), "ExtraChannel does not exist: %s", name2);
		astman_send_error(s, m, buf);
		chan = ast_channel_unref(chan);
		return 0;
	}
	if (ast_check_hangup_locked(chan2)) {
		astman_send_error(s, m, "Redirect failed, extra channel not up.");
		chan2 = ast_channel_unref(chan2);
		chan = ast_channel_unref(chan);
		return 0;
	}

	/* Dual channel redirect in progress. */
	if (ast_channel_pbx(chan)) {
		ast_channel_lock(chan);
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
		ast_channel_unlock(chan);
	}
	if (ast_channel_pbx(chan2)) {
		ast_channel_lock(chan2);
		ast_set_flag(ast_channel_flags(chan2), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
		ast_channel_unlock(chan2);
	}
	res = ast_async_goto(chan, context, exten, pi);
	if (!res) {
		if (!ast_strlen_zero(context2)) {
			res = ast_async_goto(chan2, context2, exten2, pi2);
		} else {
			res = ast_async_goto(chan2, context, exten, pi);
		}
		if (!res) {
			astman_send_ack(s, m, "Dual Redirect successful");
		} else {
			astman_send_error(s, m, "Secondary redirect failed");
		}
	} else {
		astman_send_error(s, m, "Redirect failed");
	}

	/* Release the bridge wait. */
	if (ast_channel_pbx(chan)) {
		ast_channel_lock(chan);
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
		ast_channel_unlock(chan);
	}
	if (ast_channel_pbx(chan2)) {
		ast_channel_lock(chan2);
		ast_clear_flag(ast_channel_flags(chan2), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
		ast_channel_unlock(chan2);
	}

	chan2 = ast_channel_unref(chan2);
	chan = ast_channel_unref(chan);
	return 0;
}

static int action_blind_transfer(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "No extension specified");
		return 0;
	}

	chan = ast_channel_get_by_name(name);
	if (!chan) {
		astman_send_error(s, m, "Channel specified does not exist");
		return 0;
	}

	if (ast_strlen_zero(context)) {
		context = ast_channel_context(chan);
	}

	switch (ast_bridge_transfer_blind(1, chan, exten, context, NULL, NULL)) {
	case AST_BRIDGE_TRANSFER_NOT_PERMITTED:
		astman_send_error(s, m, "Transfer not permitted");
		break;
	case AST_BRIDGE_TRANSFER_INVALID:
		astman_send_error(s, m, "Transfer invalid");
		break;
	case AST_BRIDGE_TRANSFER_FAIL:
		astman_send_error(s, m, "Transfer failed");
		break;
	case AST_BRIDGE_TRANSFER_SUCCESS:
		astman_send_ack(s, m, "Transfer succeeded");
		break;
	}

	return 0;
}

static int action_atxfer(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	struct ast_channel *chan = NULL;
	char feature_code[AST_FEATURE_MAX_LEN];
	const char *digit;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "No extension specified");
		return 0;
	}

	if (!(chan = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "Channel specified does not exist");
		return 0;
	}

	ast_channel_lock(chan);
	if (ast_get_builtin_feature(chan, "atxfer", feature_code, sizeof(feature_code)) ||
			ast_strlen_zero(feature_code)) {
		ast_channel_unlock(chan);
		astman_send_error(s, m, "No attended transfer feature code found");
		ast_channel_unref(chan);
		return 0;
	}
	ast_channel_unlock(chan);

	if (!ast_strlen_zero(context)) {
		pbx_builtin_setvar_helper(chan, "TRANSFER_CONTEXT", context);
	}

	for (digit = feature_code; *digit; ++digit) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = *digit };
		ast_queue_frame(chan, &f);
	}

	for (digit = exten; *digit; ++digit) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = *digit };
		ast_queue_frame(chan, &f);
	}

	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "Atxfer successfully queued");

	return 0;
}

static int check_blacklist(const char *cmd)
{
	char *cmd_copy, *cur_cmd;
	char *cmd_words[MAX_BLACKLIST_CMD_LEN] = { NULL, };
	int i;

	cmd_copy = ast_strdupa(cmd);
	for (i = 0; i < MAX_BLACKLIST_CMD_LEN && (cur_cmd = strsep(&cmd_copy, " ")); i++) {
		cur_cmd = ast_strip(cur_cmd);
		if (ast_strlen_zero(cur_cmd)) {
			i--;
			continue;
		}

		cmd_words[i] = cur_cmd;
	}

	for (i = 0; i < ARRAY_LEN(command_blacklist); i++) {
		int j, match = 1;

		for (j = 0; command_blacklist[i].words[j]; j++) {
			if (ast_strlen_zero(cmd_words[j]) || strcasecmp(cmd_words[j], command_blacklist[i].words[j])) {
				match = 0;
				break;
			}
		}

		if (match) {
			return 1;
		}
	}

	return 0;
}

/*! \brief  Manager command "command" - execute CLI command */
static int action_command(struct mansession *s, const struct message *m)
{
	const char *cmd = astman_get_header(m, "Command");
	const char *id = astman_get_header(m, "ActionID");
	char *buf = NULL, *final_buf = NULL;
	char template[] = "/tmp/ast-ami-XXXXXX";	/* template for temporary file */
	int fd;
	off_t l;

	if (ast_strlen_zero(cmd)) {
		astman_send_error(s, m, "No command provided");
		return 0;
	}

	if (check_blacklist(cmd)) {
		astman_send_error(s, m, "Command blacklisted");
		return 0;
	}

	if ((fd = mkstemp(template)) < 0) {
		ast_log(AST_LOG_WARNING, "Failed to create temporary file for command: %s\n", strerror(errno));
		astman_send_error(s, m, "Command response construction error");
		return 0;
	}

	astman_append(s, "Response: Follows\r\nPrivilege: Command\r\n");
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
	/* FIXME: Wedge a ActionID response in here, waiting for later changes */
	ast_cli_command(fd, cmd);	/* XXX need to change this to use a FILE * */
	/* Determine number of characters available */
	if ((l = lseek(fd, 0, SEEK_END)) < 0) {
		ast_log(LOG_WARNING, "Failed to determine number of characters for command: %s\n", strerror(errno));
		goto action_command_cleanup;
	}

	/* This has a potential to overflow the stack.  Hence, use the heap. */
	buf = ast_malloc(l + 1);
	final_buf = ast_malloc(l + 1);

	if (!buf || !final_buf) {
		ast_log(LOG_WARNING, "Failed to allocate memory for temporary buffer\n");
		goto action_command_cleanup;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		ast_log(LOG_WARNING, "Failed to set position on temporary file for command: %s\n", strerror(errno));
		goto action_command_cleanup;
	}

	if (read(fd, buf, l) < 0) {
		ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
		goto action_command_cleanup;
	}

	buf[l] = '\0';
	term_strip(final_buf, buf, l);
	final_buf[l] = '\0';
	astman_append(s, "%s", final_buf);

action_command_cleanup:

	close(fd);
	unlink(template);
	astman_append(s, "--END COMMAND--\r\n\r\n");

	ast_free(buf);
	ast_free(final_buf);

	return 0;
}

/*! \brief helper function for originate */
struct fast_originate_helper {
	int timeout;
	struct ast_format_cap *cap;				/*!< Codecs used for a call */
	int early_media;
	AST_DECLARE_STRING_FIELDS (
		AST_STRING_FIELD(tech);
		/*! data can contain a channel name, extension number, username, password, etc. */
		AST_STRING_FIELD(data);
		AST_STRING_FIELD(app);
		AST_STRING_FIELD(appdata);
		AST_STRING_FIELD(cid_name);
		AST_STRING_FIELD(cid_num);
		AST_STRING_FIELD(context);
		AST_STRING_FIELD(exten);
		AST_STRING_FIELD(idtext);
		AST_STRING_FIELD(account);
		AST_STRING_FIELD(channelid);
		AST_STRING_FIELD(otherchannelid);
	);
	int priority;
	struct ast_variable *vars;
};

/*!
 * \internal
 *
 * \param doomed Struct to destroy.
 *
 * \return Nothing
 */
static void destroy_fast_originate_helper(struct fast_originate_helper *doomed)
{
	ast_format_cap_destroy(doomed->cap);
	ast_variables_destroy(doomed->vars);
	ast_string_field_free_memory(doomed);
	ast_free(doomed);
}

static void *fast_originate(void *data)
{
	struct fast_originate_helper *in = data;
	int res;
	int reason = 0;
	struct ast_channel *chan = NULL, *chans[1];
	char requested_channel[AST_CHANNEL_NAME];
	struct ast_assigned_ids assignedids = {
		.uniqueid = in->channelid,
		.uniqueid2 = in->otherchannelid
	};

	if (!ast_strlen_zero(in->app)) {
		res = ast_pbx_outgoing_app(in->tech, in->cap, in->data,
			in->timeout, in->app, in->appdata, &reason, 1,
			S_OR(in->cid_num, NULL),
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan, &assignedids);
	} else {
		res = ast_pbx_outgoing_exten(in->tech, in->cap, in->data,
			in->timeout, in->context, in->exten, in->priority, &reason, 1,
			S_OR(in->cid_num, NULL),
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan, in->early_media, &assignedids);
	}
	/* Any vars memory was passed to the ast_pbx_outgoing_xxx() calls. */
	in->vars = NULL;

	if (!chan) {
		snprintf(requested_channel, AST_CHANNEL_NAME, "%s/%s", in->tech, in->data);
	}
	/* Tell the manager what happened with the channel */
	chans[0] = chan;
	ast_manager_event_multichan(EVENT_FLAG_CALL, "OriginateResponse", chan ? 1 : 0, chans,
		"%s"
		"Response: %s\r\n"
		"Channel: %s\r\n"
		"Context: %s\r\n"
		"Exten: %s\r\n"
		"Reason: %d\r\n"
		"Uniqueid: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n",
		in->idtext, res ? "Failure" : "Success",
		chan ? ast_channel_name(chan) : requested_channel, in->context, in->exten, reason,
		chan ? ast_channel_uniqueid(chan) : "<null>",
		S_OR(in->cid_num, "<unknown>"),
		S_OR(in->cid_name, "<unknown>")
		);

	/* Locked and ref'd by ast_pbx_outgoing_exten or ast_pbx_outgoing_app */
	if (chan) {
		ast_channel_unlock(chan);
		ast_channel_unref(chan);
	}
	destroy_fast_originate_helper(in);
	return NULL;
}

static int aocmessage_get_unit_entry(const struct message *m, struct ast_aoc_unit_entry *entry, unsigned int entry_num)
{
	const char *unitamount;
	const char *unittype;
	struct ast_str *str = ast_str_alloca(32);

	memset(entry, 0, sizeof(*entry));

	ast_str_set(&str, 0, "UnitAmount(%u)", entry_num);
	unitamount = astman_get_header(m, ast_str_buffer(str));

	ast_str_set(&str, 0, "UnitType(%u)", entry_num);
	unittype = astman_get_header(m, ast_str_buffer(str));

	if (!ast_strlen_zero(unitamount) && (sscanf(unitamount, "%30u", &entry->amount) == 1)) {
		entry->valid_amount = 1;
	}

	if (!ast_strlen_zero(unittype) && sscanf(unittype, "%30u", &entry->type) == 1) {
		entry->valid_type = 1;
	}

	return 0;
}

static int action_aocmessage(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *pchannel = astman_get_header(m, "ChannelPrefix");
	const char *msgtype = astman_get_header(m, "MsgType");
	const char *chargetype = astman_get_header(m, "ChargeType");
	const char *currencyname = astman_get_header(m, "CurrencyName");
	const char *currencyamount = astman_get_header(m, "CurrencyAmount");
	const char *mult = astman_get_header(m, "CurrencyMultiplier");
	const char *totaltype = astman_get_header(m, "TotalType");
	const char *aocbillingid = astman_get_header(m, "AOCBillingId");
	const char *association_id= astman_get_header(m, "ChargingAssociationId");
	const char *association_num = astman_get_header(m, "ChargingAssociationNumber");
	const char *association_plan = astman_get_header(m, "ChargingAssociationPlan");

	enum ast_aoc_type _msgtype;
	enum ast_aoc_charge_type _chargetype;
	enum ast_aoc_currency_multiplier _mult = AST_AOC_MULT_ONE;
	enum ast_aoc_total_type _totaltype = AST_AOC_TOTAL;
	enum ast_aoc_billing_id _billingid = AST_AOC_BILLING_NA;
	unsigned int _currencyamount = 0;
	int _association_id = 0;
	unsigned int _association_plan = 0;
	struct ast_channel *chan = NULL;

	struct ast_aoc_decoded *decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t encoded_size = 0;

	if (ast_strlen_zero(channel) && ast_strlen_zero(pchannel)) {
		astman_send_error(s, m, "Channel and PartialChannel are not specified. Specify at least one of these.");
		goto aocmessage_cleanup;
	}

	if (!(chan = ast_channel_get_by_name(channel)) && !ast_strlen_zero(pchannel)) {
		chan = ast_channel_get_by_name_prefix(pchannel, strlen(pchannel));
	}

	if (!chan) {
		astman_send_error(s, m, "No such channel");
		goto aocmessage_cleanup;
	}

	if (ast_strlen_zero(msgtype) || (strcasecmp(msgtype, "d") && strcasecmp(msgtype, "e"))) {
		astman_send_error(s, m, "Invalid MsgType");
		goto aocmessage_cleanup;
	}

	if (ast_strlen_zero(chargetype)) {
		astman_send_error(s, m, "ChargeType not specified");
		goto aocmessage_cleanup;
	}

	_msgtype = strcasecmp(msgtype, "d") ? AST_AOC_E : AST_AOC_D;

	if (!strcasecmp(chargetype, "NA")) {
		_chargetype = AST_AOC_CHARGE_NA;
	} else if (!strcasecmp(chargetype, "Free")) {
		_chargetype = AST_AOC_CHARGE_FREE;
	} else if (!strcasecmp(chargetype, "Currency")) {
		_chargetype = AST_AOC_CHARGE_CURRENCY;
	} else if (!strcasecmp(chargetype, "Unit")) {
		_chargetype = AST_AOC_CHARGE_UNIT;
	} else {
		astman_send_error(s, m, "Invalid ChargeType");
		goto aocmessage_cleanup;
	}

	if (_chargetype == AST_AOC_CHARGE_CURRENCY) {

		if (ast_strlen_zero(currencyamount) || (sscanf(currencyamount, "%30u", &_currencyamount) != 1)) {
			astman_send_error(s, m, "Invalid CurrencyAmount, CurrencyAmount is a required when ChargeType is Currency");
			goto aocmessage_cleanup;
		}

		if (ast_strlen_zero(mult)) {
			astman_send_error(s, m, "ChargeMultiplier unspecified, ChargeMultiplier is required when ChargeType is Currency.");
			goto aocmessage_cleanup;
		} else if (!strcasecmp(mult, "onethousandth")) {
			_mult = AST_AOC_MULT_ONETHOUSANDTH;
		} else if (!strcasecmp(mult, "onehundredth")) {
			_mult = AST_AOC_MULT_ONEHUNDREDTH;
		} else if (!strcasecmp(mult, "onetenth")) {
			_mult = AST_AOC_MULT_ONETENTH;
		} else if (!strcasecmp(mult, "one")) {
			_mult = AST_AOC_MULT_ONE;
		} else if (!strcasecmp(mult, "ten")) {
			_mult = AST_AOC_MULT_TEN;
		} else if (!strcasecmp(mult, "hundred")) {
			_mult = AST_AOC_MULT_HUNDRED;
		} else if (!strcasecmp(mult, "thousand")) {
			_mult = AST_AOC_MULT_THOUSAND;
		} else {
			astman_send_error(s, m, "Invalid ChargeMultiplier");
			goto aocmessage_cleanup;
		}
	}

	/* create decoded object and start setting values */
	if (!(decoded = ast_aoc_create(_msgtype, _chargetype, 0))) {
			astman_send_error(s, m, "Message Creation Failed");
			goto aocmessage_cleanup;
	}

	if (_msgtype == AST_AOC_D) {
		if (!ast_strlen_zero(totaltype) && !strcasecmp(totaltype, "subtotal")) {
			_totaltype = AST_AOC_SUBTOTAL;
		}

		if (ast_strlen_zero(aocbillingid)) {
			/* ignore this is optional */
		} else if (!strcasecmp(aocbillingid, "Normal")) {
			_billingid = AST_AOC_BILLING_NORMAL;
		} else if (!strcasecmp(aocbillingid, "ReverseCharge")) {
			_billingid = AST_AOC_BILLING_REVERSE_CHARGE;
		} else if (!strcasecmp(aocbillingid, "CreditCard")) {
			_billingid = AST_AOC_BILLING_CREDIT_CARD;
		} else {
			astman_send_error(s, m, "Invalid AOC-D AOCBillingId");
			goto aocmessage_cleanup;
		}
	} else {
		if (ast_strlen_zero(aocbillingid)) {
			/* ignore this is optional */
		} else if (!strcasecmp(aocbillingid, "Normal")) {
			_billingid = AST_AOC_BILLING_NORMAL;
		} else if (!strcasecmp(aocbillingid, "ReverseCharge")) {
			_billingid = AST_AOC_BILLING_REVERSE_CHARGE;
		} else if (!strcasecmp(aocbillingid, "CreditCard")) {
			_billingid = AST_AOC_BILLING_CREDIT_CARD;
		} else if (!strcasecmp(aocbillingid, "CallFwdUnconditional")) {
			_billingid = AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL;
		} else if (!strcasecmp(aocbillingid, "CallFwdBusy")) {
			_billingid = AST_AOC_BILLING_CALL_FWD_BUSY;
		} else if (!strcasecmp(aocbillingid, "CallFwdNoReply")) {
			_billingid = AST_AOC_BILLING_CALL_FWD_NO_REPLY;
		} else if (!strcasecmp(aocbillingid, "CallDeflection")) {
			_billingid = AST_AOC_BILLING_CALL_DEFLECTION;
		} else if (!strcasecmp(aocbillingid, "CallTransfer")) {
			_billingid = AST_AOC_BILLING_CALL_TRANSFER;
		} else {
			astman_send_error(s, m, "Invalid AOC-E AOCBillingId");
			goto aocmessage_cleanup;
		}

		if (!ast_strlen_zero(association_id) && (sscanf(association_id, "%30d", &_association_id) != 1)) {
			astman_send_error(s, m, "Invalid ChargingAssociationId");
			goto aocmessage_cleanup;
		}
		if (!ast_strlen_zero(association_plan) && (sscanf(association_plan, "%30u", &_association_plan) != 1)) {
			astman_send_error(s, m, "Invalid ChargingAssociationPlan");
			goto aocmessage_cleanup;
		}

		if (_association_id) {
			ast_aoc_set_association_id(decoded, _association_id);
		} else if (!ast_strlen_zero(association_num)) {
			ast_aoc_set_association_number(decoded, association_num, _association_plan);
		}
	}

	if (_chargetype == AST_AOC_CHARGE_CURRENCY) {
		ast_aoc_set_currency_info(decoded, _currencyamount, _mult, ast_strlen_zero(currencyname) ? NULL : currencyname);
	} else if (_chargetype == AST_AOC_CHARGE_UNIT) {
		struct ast_aoc_unit_entry entry;
		int i;

		/* multiple unit entries are possible, lets get them all */
		for (i = 0; i < 32; i++) {
			if (aocmessage_get_unit_entry(m, &entry, i)) {
				break; /* that's the end then */
			}

			ast_aoc_add_unit_entry(decoded, entry.valid_amount, entry.amount, entry.valid_type, entry.type);
		}

		/* at least one unit entry is required */
		if (!i) {
			astman_send_error(s, m, "Invalid UnitAmount(0), At least one valid unit entry is required when ChargeType is set to Unit");
			goto aocmessage_cleanup;
		}

	}

	ast_aoc_set_billing_id(decoded, _billingid);
	ast_aoc_set_total_type(decoded, _totaltype);


	if ((encoded = ast_aoc_encode(decoded, &encoded_size, NULL)) && !ast_indicate_data(chan, AST_CONTROL_AOC, encoded, encoded_size)) {
		astman_send_ack(s, m, "AOC Message successfully queued on channel");
	} else {
		astman_send_error(s, m, "Error encoding AOC message, could not queue onto channel");
	}

aocmessage_cleanup:

	ast_aoc_destroy_decoded(decoded);
	ast_aoc_destroy_encoded(encoded);

	if (chan) {
		chan = ast_channel_unref(chan);
	}
	return 0;
}

static int action_originate(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	const char *priority = astman_get_header(m, "Priority");
	const char *timeout = astman_get_header(m, "Timeout");
	const char *callerid = astman_get_header(m, "CallerID");
	const char *account = astman_get_header(m, "Account");
	const char *app = astman_get_header(m, "Application");
	const char *appdata = astman_get_header(m, "Data");
	const char *async = astman_get_header(m, "Async");
	const char *id = astman_get_header(m, "ActionID");
	const char *codecs = astman_get_header(m, "Codecs");
	const char *early_media = astman_get_header(m, "Earlymedia");
	struct ast_assigned_ids assignedids = {
		.uniqueid = astman_get_header(m, "ChannelId"),
		.uniqueid2 = astman_get_header(m, "OtherChannelId"),
	};
	struct ast_variable *vars = NULL;
	char *tech, *data;
	char *l = NULL, *n = NULL;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;
	char tmp[256];
	char tmp2[256];
	struct ast_format_cap *cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK);
	struct ast_format tmp_fmt;
	pthread_t th;
	int bridge_early = 0;

	if (!cap) {
		astman_send_error(s, m, "Internal Error. Memory allocation failure.");
		return 0;
	}
	ast_format_cap_add(cap, ast_format_set(&tmp_fmt, AST_FORMAT_SLINEAR, 0));

	if ((assignedids.uniqueid && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid))
		|| (assignedids.uniqueid2 && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid2))) {
		astman_send_error_va(s, m, "Uniqueid length exceeds maximum of %d\n",
			AST_MAX_PUBLIC_UNIQUEID);
		res = 0;
		goto fast_orig_cleanup;
	}

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		res = 0;
		goto fast_orig_cleanup;
	}
	if (!ast_strlen_zero(priority) && (sscanf(priority, "%30d", &pi) != 1)) {
		if ((pi = ast_findlabel_extension(NULL, context, exten, priority, NULL)) < 1) {
			astman_send_error(s, m, "Invalid priority");
			res = 0;
			goto fast_orig_cleanup;
		}
	}
	if (!ast_strlen_zero(timeout) && (sscanf(timeout, "%30d", &to) != 1)) {
		astman_send_error(s, m, "Invalid timeout");
		res = 0;
		goto fast_orig_cleanup;
	}
	ast_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		astman_send_error(s, m, "Invalid channel");
		res = 0;
		goto fast_orig_cleanup;
	}
	*data++ = '\0';
	ast_copy_string(tmp2, callerid, sizeof(tmp2));
	ast_callerid_parse(tmp2, &n, &l);
	if (n) {
		if (ast_strlen_zero(n)) {
			n = NULL;
		}
	}
	if (l) {
		ast_shrink_phone_number(l);
		if (ast_strlen_zero(l)) {
			l = NULL;
		}
	}
	if (!ast_strlen_zero(codecs)) {
		ast_format_cap_remove_all(cap);
		ast_parse_allow_disallow(NULL, cap, codecs, 1);
	}

	if (!ast_strlen_zero(app) && s->session) {
		int bad_appdata = 0;
		/* To run the System application (or anything else that goes to
		 * shell), you must have the additional System privilege */
		if (!(s->session->writeperm & EVENT_FLAG_SYSTEM)
			&& (
				strcasestr(app, "system") ||      /* System(rm -rf /)
				                                     TrySystem(rm -rf /)       */
				strcasestr(app, "exec") ||        /* Exec(System(rm -rf /))
				                                     TryExec(System(rm -rf /)) */
				strcasestr(app, "agi") ||         /* AGI(/bin/rm,-rf /)
				                                     EAGI(/bin/rm,-rf /)       */
				strcasestr(app, "mixmonitor") ||  /* MixMonitor(blah,,rm -rf)  */
				strcasestr(app, "externalivr") || /* ExternalIVR(rm -rf)       */
				(strstr(appdata, "SHELL") && (bad_appdata = 1)) ||       /* NoOp(${SHELL(rm -rf /)})  */
				(strstr(appdata, "EVAL") && (bad_appdata = 1))           /* NoOp(${EVAL(${some_var_containing_SHELL})}) */
				)) {
			char error_buf[64];
			snprintf(error_buf, sizeof(error_buf), "Originate Access Forbidden: %s", bad_appdata ? "Data" : "Application");
			astman_send_error(s, m, error_buf);
			res = 0;
			goto fast_orig_cleanup;
		}
	}

	/* Check early if the extension exists. If not, we need to bail out here. */
	if (exten && context && pi) {
		if (! ast_exists_extension(NULL, context, exten, pi, l)) {
			/* The extension does not exist. */
			astman_send_error(s, m, "Extension does not exist.");
			res = 0;
			goto fast_orig_cleanup;
		}
	}

	/* Allocate requested channel variables */
	vars = astman_get_variables(m);
	if (s->session && s->session->chanvars) {
		struct ast_variable *v, *old;
		old = vars;
		vars = NULL;

		/* The variables in the AMI originate action are appended at the end of the list, to override any user variables that apply*/

		vars = ast_variables_dup(s->session->chanvars);
		if (old) {
			for (v = vars; v->next; v = v->next );
			if (v->next) {
				v->next = old;	/* Append originate variables at end of list */
			}
		}
	}

	/* For originate async - we can bridge in early media stage */
	bridge_early = ast_true(early_media);

	if (ast_true(async)) {
		struct fast_originate_helper *fast;

		fast = ast_calloc(1, sizeof(*fast));
		if (!fast || ast_string_field_init(fast, 252)) {
			ast_free(fast);
			ast_variables_destroy(vars);
			res = -1;
		} else {
			if (!ast_strlen_zero(id)) {
				ast_string_field_build(fast, idtext, "ActionID: %s\r\n", id);
			}
			ast_string_field_set(fast, tech, tech);
			ast_string_field_set(fast, data, data);
			ast_string_field_set(fast, app, app);
			ast_string_field_set(fast, appdata, appdata);
			ast_string_field_set(fast, cid_num, l);
			ast_string_field_set(fast, cid_name, n);
			ast_string_field_set(fast, context, context);
			ast_string_field_set(fast, exten, exten);
			ast_string_field_set(fast, account, account);
			ast_string_field_set(fast, channelid, assignedids.uniqueid);
			ast_string_field_set(fast, otherchannelid, assignedids.uniqueid2);
			fast->vars = vars;
			fast->cap = cap;
			cap = NULL; /* transfered originate helper the capabilities structure.  It is now responsible for freeing it. */
			fast->timeout = to;
			fast->early_media = bridge_early;
			fast->priority = pi;
			if (ast_pthread_create_detached(&th, NULL, fast_originate, fast)) {
				destroy_fast_originate_helper(fast);
				res = -1;
			} else {
				res = 0;
			}
		}
	} else if (!ast_strlen_zero(app)) {
		res = ast_pbx_outgoing_app(tech, cap, data, to, app, appdata, &reason, 1, l, n, vars, account, NULL, assignedids.uniqueid ? &assignedids : NULL);
		/* Any vars memory was passed to ast_pbx_outgoing_app(). */
	} else {
		if (exten && context && pi) {
			res = ast_pbx_outgoing_exten(tech, cap, data, to, context, exten, pi, &reason, 1, l, n, vars, account, NULL, bridge_early, assignedids.uniqueid ? &assignedids : NULL);
			/* Any vars memory was passed to ast_pbx_outgoing_exten(). */
		} else {
			astman_send_error(s, m, "Originate with 'Exten' requires 'Context' and 'Priority'");
			ast_variables_destroy(vars);
			res = 0;
			goto fast_orig_cleanup;
		}
	}
	if (!res) {
		astman_send_ack(s, m, "Originate successfully queued");
	} else {
		astman_send_error(s, m, "Originate failed");
	}

fast_orig_cleanup:
	ast_format_cap_destroy(cap);
	return 0;
}

static int action_mailboxstatus(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	int ret;

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ret = ast_app_has_voicemail(mailbox, NULL);
	astman_start_ack(s, m);
	astman_append(s, "Message: Mailbox Status\r\n"
			 "Mailbox: %s\r\n"
			 "Waiting: %d\r\n\r\n", mailbox, ret);
	return 0;
}

static int action_mailboxcount(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	int newmsgs = 0, oldmsgs = 0, urgentmsgs = 0;;

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ast_app_inboxcount2(mailbox, &urgentmsgs, &newmsgs, &oldmsgs);
	astman_start_ack(s, m);
	astman_append(s,   "Message: Mailbox Message Count\r\n"
			   "Mailbox: %s\r\n"
			   "UrgMessages: %d\r\n"
			   "NewMessages: %d\r\n"
			   "OldMessages: %d\r\n"
			   "\r\n",
			   mailbox, urgentmsgs, newmsgs, oldmsgs);
	return 0;
}

static int action_extensionstate(struct mansession *s, const struct message *m)
{
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	char hint[256] = "";
	int status;
	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "Extension not specified");
		return 0;
	}
	if (ast_strlen_zero(context)) {
		context = "default";
	}
	status = ast_extension_state(NULL, context, exten);
	ast_get_hint(hint, sizeof(hint) - 1, NULL, 0, NULL, context, exten);
	astman_start_ack(s, m);
	astman_append(s,   "Message: Extension Status\r\n"
			   "Exten: %s\r\n"
			   "Context: %s\r\n"
			   "Hint: %s\r\n"
			   "Status: %d\r\n\r\n",
			   exten, context, hint, status);
	return 0;
}

static int action_presencestate(struct mansession *s, const struct message *m)
{
	const char *provider = astman_get_header(m, "Provider");
	enum ast_presence_state state;
	char *subtype;
	char *message;
	char subtype_header[256] = "";
	char message_header[256] = "";

	if (ast_strlen_zero(provider)) {
		astman_send_error(s, m, "No provider specified");
		return 0;
	}

	state = ast_presence_state(provider, &subtype, &message);
	if (state == AST_PRESENCE_INVALID) {
		astman_send_error_va(s, m, "Invalid provider %s or provider in invalid state", provider);
		return 0;
	}

	if (!ast_strlen_zero(subtype)) {
		snprintf(subtype_header, sizeof(subtype_header),
				"Subtype: %s\r\n", subtype);
	}

	if (!ast_strlen_zero(message)) {
		snprintf(message_header, sizeof(message_header),
				"Message: %s\r\n", message);
	}

	astman_start_ack(s, m);
	astman_append(s, "Message: Presence State\r\n"
			"State: %s\r\n"
			"%s"
			"%s"
			"\r\n",
			ast_presence_state2str(state),
			subtype_header,
			message_header);
	return 0;
}

static int action_timeout(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	double timeout = atof(astman_get_header(m, "Timeout"));
	struct timeval when = { timeout, 0 };

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (!timeout || timeout < 0) {
		astman_send_error(s, m, "No timeout specified");
		return 0;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	when.tv_usec = (timeout - when.tv_sec) * 1000000.0;

	ast_channel_lock(c);
	ast_channel_setwhentohangup_tv(c, when);
	ast_channel_unlock(c);
	c = ast_channel_unref(c);

	astman_send_ack(s, m, "Timeout Set");

	return 0;
}

static int whitefilter_cmp_fn(void *obj, void *arg, void *data, int flags)
{
	regex_t *regex_filter = obj;
	const char *eventdata = arg;
	int *result = data;

	if (!regexec(regex_filter, eventdata, 0, NULL, 0)) {
		*result = 1;
		return (CMP_MATCH | CMP_STOP);
	}

	return 0;
}

static int blackfilter_cmp_fn(void *obj, void *arg, void *data, int flags)
{
	regex_t *regex_filter = obj;
	const char *eventdata = arg;
	int *result = data;

	if (!regexec(regex_filter, eventdata, 0, NULL, 0)) {
		*result = 0;
		return (CMP_MATCH | CMP_STOP);
	}

	*result = 1;
	return 0;
}

/*!
 * \brief Manager command to add an event filter to a manager session
 * \see For more details look at manager_add_filter
 */
static int action_filter(struct mansession *s, const struct message *m)
{
	const char *filter = astman_get_header(m, "Filter");
	const char *operation = astman_get_header(m, "Operation");
	int res;

	if (!strcasecmp(operation, "Add")) {
		res = manager_add_filter(filter, s->session->whitefilters, s->session->blackfilters);

	        if (res != FILTER_SUCCESS) {
		        if (res == FILTER_ALLOC_FAILED) {
				astman_send_error(s, m, "Internal Error. Failed to allocate regex for filter");
		                return 0;
		        } else if (res == FILTER_COMPILE_FAIL) {
				astman_send_error(s, m, "Filter did not compile.  Check the syntax of the filter given.");
		                return 0;
		        } else {
				astman_send_error(s, m, "Internal Error. Failed adding filter.");
		                return 0;
	                }
		}

		astman_send_ack(s, m, "Success");
		return 0;
	}

	astman_send_error(s, m, "Unknown operation");
	return 0;
}

/*!
 * \brief Add an event filter to a manager session
 *
 * \param filter_pattern  Filter syntax to add, see below for syntax
 *
 * \return FILTER_ALLOC_FAILED   Memory allocation failure
 * \return FILTER_COMPILE_FAIL   If the filter did not compile
 * \return FILTER_SUCCESS        Success
 *
 * Filter will be used to match against each line of a manager event
 * Filter can be any valid regular expression
 * Filter can be a valid regular expression prefixed with !, which will add the filter as a black filter
 *
 * Examples:
 * \code
 *   filter_pattern = "Event: Newchannel"
 *   filter_pattern = "Event: New.*"
 *   filter_pattern = "!Channel: DAHDI.*"
 * \endcode
 *
 */
static enum add_filter_result manager_add_filter(const char *filter_pattern, struct ao2_container *whitefilters, struct ao2_container *blackfilters) {
	regex_t *new_filter = ao2_t_alloc(sizeof(*new_filter), event_filter_destructor, "event_filter allocation");
	int is_blackfilter;

	if (!new_filter) {
		return FILTER_ALLOC_FAILED;
	}

	if (filter_pattern[0] == '!') {
		is_blackfilter = 1;
		filter_pattern++;
	} else {
		is_blackfilter = 0;
	}

	if (regcomp(new_filter, filter_pattern, 0)) { /* XXX: the only place we use non-REG_EXTENDED */
		ao2_t_ref(new_filter, -1, "failed to make regex");
		return FILTER_COMPILE_FAIL;
	}

	if (is_blackfilter) {
		ao2_t_link(blackfilters, new_filter, "link new filter into black user container");
	} else {
		ao2_t_link(whitefilters, new_filter, "link new filter into white user container");
	}

	ao2_ref(new_filter, -1);

	return FILTER_SUCCESS;
}

static int match_filter(struct mansession *s, char *eventdata)
{
	int result = 0;

	ast_debug(3, "Examining AMI event:\n%s\n", eventdata);
	if (!ao2_container_count(s->session->whitefilters) && !ao2_container_count(s->session->blackfilters)) {
		return 1; /* no filtering means match all */
	} else if (ao2_container_count(s->session->whitefilters) && !ao2_container_count(s->session->blackfilters)) {
		/* white filters only: implied black all filter processed first, then white filters */
		ao2_t_callback_data(s->session->whitefilters, OBJ_NODATA, whitefilter_cmp_fn, eventdata, &result, "find filter in session filter container");
	} else if (!ao2_container_count(s->session->whitefilters) && ao2_container_count(s->session->blackfilters)) {
		/* black filters only: implied white all filter processed first, then black filters */
		ao2_t_callback_data(s->session->blackfilters, OBJ_NODATA, blackfilter_cmp_fn, eventdata, &result, "find filter in session filter container");
	} else {
		/* white and black filters: implied black all filter processed first, then white filters, and lastly black filters */
		ao2_t_callback_data(s->session->whitefilters, OBJ_NODATA, whitefilter_cmp_fn, eventdata, &result, "find filter in session filter container");
		if (result) {
			result = 0;
			ao2_t_callback_data(s->session->blackfilters, OBJ_NODATA, blackfilter_cmp_fn, eventdata, &result, "find filter in session filter container");
		}
	}

	return result;
}

/*!
 * Send any applicable events to the client listening on this socket.
 * Wait only for a finite time on each event, and drop all events whether
 * they are successfully sent or not.
 */
static int process_events(struct mansession *s)
{
	int ret = 0;

	ao2_lock(s->session);
	if (s->session->f != NULL) {
		struct eventqent *eqe = s->session->last_ev;

		while ((eqe = advance_event(eqe))) {
			if (eqe->category == EVENT_FLAG_SHUTDOWN) {
				ast_debug(3, "Received CloseSession event\n");
				ret = -1;
			}
			if (!ret && s->session->authenticated &&
			    (s->session->readperm & eqe->category) == eqe->category &&
			    (s->session->send_events & eqe->category) == eqe->category) {
					if (match_filter(s, eqe->eventdata)) {
						if (send_string(s, eqe->eventdata) < 0)
							ret = -1;	/* don't send more */
					}
			}
			s->session->last_ev = eqe;
		}
	}
	ao2_unlock(s->session);
	return ret;
}

static int action_userevent(struct mansession *s, const struct message *m)
{
	const char *event = astman_get_header(m, "UserEvent");
	struct ast_str *body = ast_str_thread_get(&userevent_buf, 16);
	int x;

	ast_str_reset(body);

	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp("UserEvent:", m->headers[x], strlen("UserEvent:"))) {
			ast_str_append(&body, 0, "%s\r\n", m->headers[x]);
		}
	}

	astman_send_ack(s, m, "Event Sent");
	manager_event(EVENT_FLAG_USER, "UserEvent", "UserEvent: %s\r\n%s", event, ast_str_buffer(body));
	return 0;
}

/*! \brief Show PBX core settings information */
static int action_coresettings(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[150];

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	} else {
		idText[0] = '\0';
	}

	astman_append(s, "Response: Success\r\n"
			"%s"
			"AMIversion: %s\r\n"
			"AsteriskVersion: %s\r\n"
			"SystemName: %s\r\n"
			"CoreMaxCalls: %d\r\n"
			"CoreMaxLoadAvg: %f\r\n"
			"CoreRunUser: %s\r\n"
			"CoreRunGroup: %s\r\n"
			"CoreMaxFilehandles: %d\r\n"
			"CoreRealTimeEnabled: %s\r\n"
			"CoreCDRenabled: %s\r\n"
			"CoreHTTPenabled: %s\r\n"
			"\r\n",
			idText,
			AMI_VERSION,
			ast_get_version(),
			ast_config_AST_SYSTEM_NAME,
			ast_option_maxcalls,
			ast_option_maxload,
			ast_config_AST_RUN_USER,
			ast_config_AST_RUN_GROUP,
			ast_option_maxfiles,
			AST_CLI_YESNO(ast_realtime_enabled()),
			AST_CLI_YESNO(ast_cdr_is_enabled()),
			AST_CLI_YESNO(check_webmanager_enabled())
			);
	return 0;
}

/*! \brief Show PBX core status information */
static int action_corestatus(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[150];
	char startuptime[150], startupdate[150];
	char reloadtime[150], reloaddate[150];
	struct ast_tm tm;

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	} else {
		idText[0] = '\0';
	}

	ast_localtime(&ast_startuptime, &tm, NULL);
	ast_strftime(startuptime, sizeof(startuptime), "%H:%M:%S", &tm);
	ast_strftime(startupdate, sizeof(startupdate), "%Y-%m-%d", &tm);
	ast_localtime(&ast_lastreloadtime, &tm, NULL);
	ast_strftime(reloadtime, sizeof(reloadtime), "%H:%M:%S", &tm);
	ast_strftime(reloaddate, sizeof(reloaddate), "%Y-%m-%d", &tm);

	astman_append(s, "Response: Success\r\n"
			"%s"
			"CoreStartupDate: %s\r\n"
			"CoreStartupTime: %s\r\n"
			"CoreReloadDate: %s\r\n"
			"CoreReloadTime: %s\r\n"
			"CoreCurrentCalls: %d\r\n"
			"\r\n",
			idText,
			startupdate,
			startuptime,
			reloaddate,
			reloadtime,
			ast_active_channels()
			);
	return 0;
}

/*! \brief Send a reload event */
static int action_reload(struct mansession *s, const struct message *m)
{
	const char *module = astman_get_header(m, "Module");
	enum ast_module_reload_result res = ast_module_reload(S_OR(module, NULL));

	switch (res) {
	case AST_MODULE_RELOAD_NOT_FOUND:
		astman_send_error(s, m, "No such module");
		break;
	case AST_MODULE_RELOAD_NOT_IMPLEMENTED:
		astman_send_error(s, m, "Module does not support reload");
		break;
	case AST_MODULE_RELOAD_ERROR:
		astman_send_error(s, m, "An unknown error occurred");
		break;
	case AST_MODULE_RELOAD_IN_PROGRESS:
		astman_send_error(s, m, "A reload is in progress");
		break;
	case AST_MODULE_RELOAD_UNINITIALIZED:
		astman_send_error(s, m, "Module not initialized");
		break;
	case AST_MODULE_RELOAD_QUEUED:
	case AST_MODULE_RELOAD_SUCCESS:
		/* Treat a queued request as success */
		astman_send_ack(s, m, "Module Reloaded");
		break;
	}
	return 0;
}

/*! \brief  Manager command "CoreShowChannels" - List currently defined channels
 *          and some information about them. */
static int action_coreshowchannels(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[256];
	int numchans = 0;
	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);
	struct ao2_iterator it_chans;
	struct stasis_message *msg;

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	} else {
		idText[0] = '\0';
	}

	if (!(channels = stasis_cache_dump(ast_channel_cache_by_name(), ast_channel_snapshot_type()))) {
		astman_send_error(s, m, "Could not get cached channels");
		return 0;
	}

	astman_send_listack(s, m, "Channels will follow", "start");

	it_chans = ao2_iterator_init(channels, 0);
	for (; (msg = ao2_iterator_next(&it_chans)); ao2_ref(msg, -1)) {
		struct ast_channel_snapshot *cs = stasis_message_data(msg);
		struct ast_str *built = ast_manager_build_channel_state_string_prefix(cs, "");
		char durbuf[10] = "";

		if (!built) {
			continue;
		}

		if (!ast_tvzero(cs->creationtime)) {
			int duration, durh, durm, durs;

			duration = (int)(ast_tvdiff_ms(ast_tvnow(), cs->creationtime) / 1000);
			durh = duration / 3600;
			durm = (duration % 3600) / 60;
			durs = duration % 60;
			snprintf(durbuf, sizeof(durbuf), "%02d:%02d:%02d", durh, durm, durs);
		}

		astman_append(s,
			"Event: CoreShowChannel\r\n"
			"%s"
			"%s"
			"Application: %s\r\n"
			"ApplicationData: %s\r\n"
			"Duration: %s\r\n"
			"BridgeId: %s\r\n"
			"\r\n",
			idText,
			ast_str_buffer(built),
			cs->appl,
			cs->data,
			durbuf,
			cs->bridgeid);

		numchans++;

		ast_free(built);
	}
	ao2_iterator_destroy(&it_chans);

	astman_append(s,
		"Event: CoreShowChannelsComplete\r\n"
		"EventList: Complete\r\n"
		"ListItems: %d\r\n"
		"%s"
		"\r\n", numchans, idText);

	return 0;
}

/*! \brief Manager function to check if module is loaded */
static int manager_modulecheck(struct mansession *s, const struct message *m)
{
	int res;
	const char *module = astman_get_header(m, "Module");
	const char *id = astman_get_header(m, "ActionID");
	char idText[256];
#if !defined(LOW_MEMORY)
	const char *version;
#endif
	char filename[PATH_MAX];
	char *cut;

	ast_copy_string(filename, module, sizeof(filename));
	if ((cut = strchr(filename, '.'))) {
		*cut = '\0';
	} else {
		cut = filename + strlen(filename);
	}
	snprintf(cut, (sizeof(filename) - strlen(filename)) - 1, ".so");
	ast_debug(1, "**** ModuleCheck .so file %s\n", filename);
	res = ast_module_check(filename);
	if (!res) {
		astman_send_error(s, m, "Module not loaded");
		return 0;
	}
	snprintf(cut, (sizeof(filename) - strlen(filename)) - 1, ".c");
	ast_debug(1, "**** ModuleCheck .c file %s\n", filename);
#if !defined(LOW_MEMORY)
	version = ast_file_version_find(filename);
#endif

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	} else {
		idText[0] = '\0';
	}
	astman_append(s, "Response: Success\r\n%s", idText);
#if !defined(LOW_MEMORY)
	astman_append(s, "Version: %s\r\n\r\n", version ? version : "");
#endif
	return 0;
}

static int manager_moduleload(struct mansession *s, const struct message *m)
{
	int res;
	const char *module = astman_get_header(m, "Module");
	const char *loadtype = astman_get_header(m, "LoadType");

	if (!loadtype || strlen(loadtype) == 0) {
		astman_send_error(s, m, "Incomplete ModuleLoad action.");
	}
	if ((!module || strlen(module) == 0) && strcasecmp(loadtype, "reload") != 0) {
		astman_send_error(s, m, "Need module name");
	}

	if (!strcasecmp(loadtype, "load")) {
		res = ast_load_resource(module);
		if (res) {
			astman_send_error(s, m, "Could not load module.");
		} else {
			astman_send_ack(s, m, "Module loaded.");
		}
	} else if (!strcasecmp(loadtype, "unload")) {
		res = ast_unload_resource(module, AST_FORCE_SOFT);
		if (res) {
			astman_send_error(s, m, "Could not unload module.");
		} else {
			astman_send_ack(s, m, "Module unloaded.");
		}
	} else if (!strcasecmp(loadtype, "reload")) {
		if (!ast_strlen_zero(module)) {
			res = ast_module_reload(module);
			if (res == 0) {
				astman_send_error(s, m, "No such module.");
			} else if (res == 1) {
				astman_send_error(s, m, "Module does not support reload action.");
			} else {
				astman_send_ack(s, m, "Module reloaded.");
			}
		} else {
			ast_module_reload(NULL);	/* Reload all modules */
			astman_send_ack(s, m, "All modules reloaded");
		}
	} else
		astman_send_error(s, m, "Incomplete ModuleLoad action.");
	return 0;
}

/*
 * Done with the action handlers here, we start with the code in charge
 * of accepting connections and serving them.
 * accept_thread() forks a new thread for each connection, session_do(),
 * which in turn calls get_input() repeatedly until a full message has
 * been accumulated, and then invokes process_message() to pass it to
 * the appropriate handler.
 */

/*! \brief
 * Process an AMI message, performing desired action.
 * Return 0 on success, -1 on error that require the session to be destroyed.
 */
static int process_message(struct mansession *s, const struct message *m)
{
	int ret = 0;
	struct manager_action *act_found;
	struct ast_manager_user *user = NULL;
	const char *username;
	const char *action;

	action = __astman_get_header(m, "Action", GET_HEADER_SKIP_EMPTY);
	if (ast_strlen_zero(action)) {
		report_req_bad_format(s, "NONE");
		mansession_lock(s);
		astman_send_error(s, m, "Missing action in request");
		mansession_unlock(s);
		return 0;
	}

	if (!s->session->authenticated
		&& strcasecmp(action, "Login")
		&& strcasecmp(action, "Logoff")
		&& strcasecmp(action, "Challenge")) {
		if (!s->session->authenticated) {
			report_req_not_allowed(s, action);
		}
		mansession_lock(s);
		astman_send_error(s, m, "Permission denied");
		mansession_unlock(s);
		return 0;
	}

	if (!s->session->authenticated
		&& (!strcasecmp(action, "Login")
			|| !strcasecmp(action, "Challenge"))) {
		username = astman_get_header(m, "Username");

		if (!ast_strlen_zero(username) && check_manager_session_inuse(username)) {
			AST_RWLIST_WRLOCK(&users);
			user = get_manager_by_name_locked(username);
			if (user && !user->allowmultiplelogin) {
				AST_RWLIST_UNLOCK(&users);
				report_session_limit(s);
				sleep(1);
				mansession_lock(s);
				astman_send_error(s, m, "Login Already In Use");
				mansession_unlock(s);
				return -1;
			}
			AST_RWLIST_UNLOCK(&users);
		}
	}

	act_found = action_find(action);
	if (act_found) {
		/* Found the requested AMI action. */
		int acted = 0;

		if ((s->session->writeperm & act_found->authority)
			|| act_found->authority == 0) {
			/* We have the authority to execute the action. */
			ao2_lock(act_found);
			if (act_found->registered && act_found->func) {
				ast_debug(1, "Running action '%s'\n", act_found->action);
				if (act_found->module) {
					ast_module_ref(act_found->module);
				}
				ao2_unlock(act_found);
				ret = act_found->func(s, m);
				acted = 1;
				ao2_lock(act_found);
				if (act_found->module) {
					ast_module_unref(act_found->module);
				}
			}
			ao2_unlock(act_found);
		}
		if (!acted) {
			/*
			 * We did not execute the action because access was denied, it
			 * was no longer registered, or no action was really registered.
			 * Complain about it and leave.
			 */
			report_req_not_allowed(s, action);
			mansession_lock(s);
			astman_send_error(s, m, "Permission denied");
			mansession_unlock(s);
		}
		ao2_t_ref(act_found, -1, "done with found action object");
	} else {
		char buf[512];

		report_req_bad_format(s, action);
		snprintf(buf, sizeof(buf), "Invalid/unknown command: %s. Use Action: ListCommands to show available commands.", action);
		mansession_lock(s);
		astman_send_error(s, m, buf);
		mansession_unlock(s);
	}
	if (ret) {
		return ret;
	}
	/* Once done with our message, deliver any pending events unless the
	   requester doesn't want them as part of this response.
	*/
	if (ast_strlen_zero(astman_get_header(m, "SuppressEvents"))) {
		return process_events(s);
	} else {
		return ret;
	}
}

/*!
 * Read one full line (including crlf) from the manager socket.
 * \note \verbatim
 * \r\n is the only valid terminator for the line.
 * (Note that, later, '\0' will be considered as the end-of-line marker,
 * so everything between the '\0' and the '\r\n' will not be used).
 * Also note that we assume output to have at least "maxlen" space.
 * \endverbatim
 */
static int get_input(struct mansession *s, char *output)
{
	int res, x;
	int maxlen = sizeof(s->session->inbuf) - 1;
	char *src = s->session->inbuf;
	int timeout = -1;
	time_t now;

	/*
	 * Look for \r\n within the buffer. If found, copy to the output
	 * buffer and return, trimming the \r\n (not used afterwards).
	 */
	for (x = 0; x < s->session->inlen; x++) {
		int cr;	/* set if we have \r */
		if (src[x] == '\r' && x+1 < s->session->inlen && src[x + 1] == '\n') {
			cr = 2;	/* Found. Update length to include \r\n */
		} else if (src[x] == '\n') {
			cr = 1;	/* also accept \n only */
		} else {
			continue;
		}
		memmove(output, src, x);	/*... but trim \r\n */
		output[x] = '\0';		/* terminate the string */
		x += cr;			/* number of bytes used */
		s->session->inlen -= x;			/* remaining size */
		memmove(src, src + x, s->session->inlen); /* remove used bytes */
		return 1;
	}
	if (s->session->inlen >= maxlen) {
		/* no crlf found, and buffer full - sorry, too long for us */
		ast_log(LOG_WARNING, "Discarding message from %s. Line too long: %.25s...\n", ast_sockaddr_stringify_addr(&s->session->addr), src);
		s->session->inlen = 0;
		s->parsing = MESSAGE_LINE_TOO_LONG;
	}
	res = 0;
	while (res == 0) {
		/* calculate a timeout if we are not authenticated */
		if (!s->session->authenticated) {
			if(time(&now) == -1) {
				ast_log(LOG_ERROR, "error executing time(): %s\n", strerror(errno));
				return -1;
			}

			timeout = (authtimeout - (now - s->session->authstart)) * 1000;
			if (timeout < 0) {
				/* we have timed out */
				return 0;
			}
		}

		ao2_lock(s->session);
		if (s->session->pending_event) {
			s->session->pending_event = 0;
			ao2_unlock(s->session);
			return 0;
		}
		s->session->waiting_thread = pthread_self();
		ao2_unlock(s->session);

		res = ast_wait_for_input(s->session->fd, timeout);

		ao2_lock(s->session);
		s->session->waiting_thread = AST_PTHREADT_NULL;
		ao2_unlock(s->session);
	}
	if (res < 0) {
		/* If we get a signal from some other thread (typically because
		 * there are new events queued), return 0 to notify the caller.
		 */
		if (errno == EINTR || errno == EAGAIN) {
			return 0;
		}
		ast_log(LOG_WARNING, "poll() returned error: %s\n", strerror(errno));
		return -1;
	}

	ao2_lock(s->session);
	res = fread(src + s->session->inlen, 1, maxlen - s->session->inlen, s->session->f);
	if (res < 1) {
		res = -1;	/* error return */
	} else {
		s->session->inlen += res;
		src[s->session->inlen] = '\0';
		res = 0;
	}
	ao2_unlock(s->session);
	return res;
}

/*!
 * \internal
 * \brief Error handling for sending parse errors. This function handles locking, and clearing the
 * parse error flag.
 *
 * \param s AMI session to process action request.
 * \param m Message that's in error.
 * \param error Error message to send.
 */
static void handle_parse_error(struct mansession *s, struct message *m, char *error)
{
	mansession_lock(s);
	astman_send_error(s, m, error);
	s->parsing = MESSAGE_OKAY;
	mansession_unlock(s);
}

/*!
 * \internal
 * \brief Read and process an AMI action request.
 *
 * \param s AMI session to process action request.
 *
 * \retval 0 Retain AMI connection for next command.
 * \retval -1 Drop AMI connection due to logoff or connection error.
 */
static int do_message(struct mansession *s)
{
	struct message m = { 0 };
	char header_buf[sizeof(s->session->inbuf)] = { '\0' };
	int res;
	int idx;
	int hdr_loss;
	time_t now;

	hdr_loss = 0;
	for (;;) {
		/* Check if any events are pending and do them if needed */
		if (process_events(s)) {
			res = -1;
			break;
		}
		res = get_input(s, header_buf);
		if (res == 0) {
			/* No input line received. */
			if (!s->session->authenticated) {
				if (time(&now) == -1) {
					ast_log(LOG_ERROR, "error executing time(): %s\n", strerror(errno));
					res = -1;
					break;
				}

				if (now - s->session->authstart > authtimeout) {
					if (displayconnects) {
						ast_verb(2, "Client from %s, failed to authenticate in %d seconds\n", ast_sockaddr_stringify_addr(&s->session->addr), authtimeout);
					}
					res = -1;
					break;
				}
			}
			continue;
		} else if (res > 0) {
			/* Input line received. */
			if (ast_strlen_zero(header_buf)) {
				if (hdr_loss) {
					mansession_lock(s);
					astman_send_error(s, &m, "Too many lines in message or allocation failure");
					mansession_unlock(s);
					res = 0;
				} else {
					switch (s->parsing) {
					case MESSAGE_OKAY:
						res = process_message(s, &m) ? -1 : 0;
						break;
					case MESSAGE_LINE_TOO_LONG:
						handle_parse_error(s, &m, "Failed to parse message: line too long");
						res = 0;
						break;
					}
				}
				break;
			} else if (m.hdrcount < ARRAY_LEN(m.headers)) {
				m.headers[m.hdrcount] = ast_strdup(header_buf);
				if (!m.headers[m.hdrcount]) {
					/* Allocation failure. */
					hdr_loss = 1;
				} else {
					++m.hdrcount;
				}
			} else {
				/* Too many lines in message. */
				hdr_loss = 1;
			}
		} else {
			/* Input error. */
			break;
		}
	}

	/* Free AMI request headers. */
	for (idx = 0; idx < m.hdrcount; ++idx) {
		ast_free((void *) m.headers[idx]);
	}
	return res;
}

/*! \brief The body of the individual manager session.
 * Call get_input() to read one line at a time
 * (or be woken up on new events), collect the lines in a
 * message until found an empty line, and execute the request.
 * In any case, deliver events asynchronously through process_events()
 * (called from here if no line is available, or at the end of
 * process_message(). )
 */
static void *session_do(void *data)
{
	struct ast_tcptls_session_instance *ser = data;
	struct mansession_session *session;
	struct mansession s = {
		.tcptls_session = data,
	};
	int flags;
	int res;
	struct ast_sockaddr ser_remote_address_tmp;
	struct protoent *p;

	if (ast_atomic_fetchadd_int(&unauth_sessions, +1) >= authlimit) {
		fclose(ser->f);
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		goto done;
	}

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	session = build_mansession(&ser_remote_address_tmp);

	if (session == NULL) {
		fclose(ser->f);
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		goto done;
	}

	/* here we set TCP_NODELAY on the socket to disable Nagle's algorithm.
	 * This is necessary to prevent delays (caused by buffering) as we
	 * write to the socket in bits and pieces. */
	p = getprotobyname("tcp");
	if (p) {
		int arg = 1;
		if( setsockopt(ser->fd, p->p_proto, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0 ) {
			ast_log(LOG_WARNING, "Failed to set manager tcp connection to TCP_NODELAY mode: %s\nSome manager actions may be slow to respond.\n", strerror(errno));
		}
	} else {
		ast_log(LOG_WARNING, "Failed to set manager tcp connection to TCP_NODELAY, getprotobyname(\"tcp\") failed\nSome manager actions may be slow to respond.\n");
	}

	/* make sure socket is non-blocking */
	flags = fcntl(ser->fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(ser->fd, F_SETFL, flags);

	ao2_lock(session);
	/* Hook to the tail of the event queue */
	session->last_ev = grab_last();

	ast_mutex_init(&s.lock);

	/* these fields duplicate those in the 'ser' structure */
	session->fd = s.fd = ser->fd;
	session->f = s.f = ser->f;
	ast_sockaddr_copy(&session->addr, &ser_remote_address_tmp);
	s.session = session;

	AST_LIST_HEAD_INIT_NOLOCK(&session->datastores);

	if(time(&session->authstart) == -1) {
		ast_log(LOG_ERROR, "error executing time(): %s; disconnecting client\n", strerror(errno));
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		ao2_unlock(session);
		session_destroy(session);
		goto done;
	}
	ao2_unlock(session);

	/*
	 * We cannot let the stream exclusively wait for data to arrive.
	 * We have to wake up the task to send async events.
	 */
	ast_tcptls_stream_set_exclusive_input(ser->stream_cookie, 0);

	ast_tcptls_stream_set_timeout_sequence(ser->stream_cookie,
		ast_tvnow(), authtimeout * 1000);

	astman_append(&s, "Asterisk Call Manager/%s\r\n", AMI_VERSION);	/* welcome prompt */
	for (;;) {
		if ((res = do_message(&s)) < 0 || s.write_error) {
			break;
		}
		if (session->authenticated) {
			ast_tcptls_stream_set_timeout_disable(ser->stream_cookie);
		}
	}
	/* session is over, explain why and terminate */
	if (session->authenticated) {
		if (manager_displayconnects(session)) {
			ast_verb(2, "Manager '%s' logged off from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
		}
	} else {
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		if (displayconnects) {
			ast_verb(2, "Connect attempt from '%s' unable to authenticate\n", ast_sockaddr_stringify_addr(&session->addr));
		}
	}

	session_destroy(session);

	ast_mutex_destroy(&s.lock);
done:
	ao2_ref(ser, -1);
	ser = NULL;
	return NULL;
}

/*! \brief remove at most n_max stale session from the list. */
static void purge_sessions(int n_max)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	time_t now = time(NULL);
	struct ao2_iterator i;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i)) && n_max > 0) {
		ao2_lock(session);
		if (session->sessiontimeout && (now > session->sessiontimeout) && !session->inuse) {
			if (session->authenticated
				&& VERBOSITY_ATLEAST(2)
				&& manager_displayconnects(session)) {
				ast_verb(2, "HTTP Manager '%s' timed out from %s\n",
					session->username, ast_sockaddr_stringify_addr(&session->addr));
			}
			ao2_unlock(session);
			session_destroy(session);
			n_max--;
		} else {
			ao2_unlock(session);
			unref_mansession(session);
		}
	}
	ao2_iterator_destroy(&i);
}

/*! \brief
 * events are appended to a queue from where they
 * can be dispatched to clients.
 */
static int append_event(const char *str, int category)
{
	struct eventqent *tmp = ast_malloc(sizeof(*tmp) + strlen(str));
	static int seq;	/* sequence number */

	if (!tmp) {
		return -1;
	}

	/* need to init all fields, because ast_malloc() does not */
	tmp->usecount = 0;
	tmp->category = category;
	tmp->seq = ast_atomic_fetchadd_int(&seq, 1);
	tmp->tv = ast_tvnow();
	AST_RWLIST_NEXT(tmp, eq_next) = NULL;
	strcpy(tmp->eventdata, str);

	AST_RWLIST_WRLOCK(&all_events);
	AST_RWLIST_INSERT_TAIL(&all_events, tmp, eq_next);
	AST_RWLIST_UNLOCK(&all_events);

	return 0;
}

static void append_channel_vars(struct ast_str **pbuf, struct ast_channel *chan)
{
	RAII_VAR(struct varshead *, vars, NULL, ao2_cleanup);
	struct ast_var_t *var;

	vars = ast_channel_get_manager_vars(chan);

	if (!vars) {
		return;
	}

	AST_LIST_TRAVERSE(vars, var, entries) {
		ast_str_append(pbuf, 0, "ChanVariable(%s): %s=%s\r\n", ast_channel_name(chan), var->name, var->value);
	}
}

/* XXX see if can be moved inside the function */
AST_THREADSTORAGE(manager_event_buf);
#define MANAGER_EVENT_BUF_INITSIZE   256

int __ast_manager_event_multichan(int category, const char *event, int chancount,
	struct ast_channel **chans, const char *file, int line, const char *func,
	const char *fmt, ...)
{
	RAII_VAR(struct ao2_container *, sessions, ao2_global_obj_ref(mgr_sessions), ao2_cleanup);
	struct mansession_session *session;
	struct manager_custom_hook *hook;
	struct ast_str *auth = ast_str_alloca(80);
	const char *cat_str;
	va_list ap;
	struct timeval now;
	struct ast_str *buf;
	int i;

	if (!(sessions && ao2_container_count(sessions)) && AST_RWLIST_EMPTY(&manager_hooks)) {
		return 0;
	}

	if (!(buf = ast_str_thread_get(&manager_event_buf, MANAGER_EVENT_BUF_INITSIZE))) {
		return -1;
	}

	cat_str = authority_to_str(category, &auth);
	ast_str_set(&buf, 0,
			"Event: %s\r\nPrivilege: %s\r\n",
			 event, cat_str);

	if (timestampevents) {
		now = ast_tvnow();
		ast_str_append(&buf, 0,
				"Timestamp: %ld.%06lu\r\n",
				 (long)now.tv_sec, (unsigned long) now.tv_usec);
	}
	if (manager_debug) {
		static int seq;
		ast_str_append(&buf, 0,
				"SequenceNumber: %d\r\n",
				 ast_atomic_fetchadd_int(&seq, 1));
		ast_str_append(&buf, 0,
				"File: %s\r\nLine: %d\r\nFunc: %s\r\n", file, line, func);
	}
	if (!ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		ast_str_append(&buf, 0,
				"SystemName: %s\r\n",
				 ast_config_AST_SYSTEM_NAME);
	}

	va_start(ap, fmt);
	ast_str_append_va(&buf, 0, fmt, ap);
	va_end(ap);
	for (i = 0; i < chancount; i++) {
		append_channel_vars(&buf, chans[i]);
	}

	ast_str_append(&buf, 0, "\r\n");

	append_event(ast_str_buffer(buf), category);

	/* Wake up any sleeping sessions */
	if (sessions) {
		struct ao2_iterator i;
		i = ao2_iterator_init(sessions, 0);
		while ((session = ao2_iterator_next(&i))) {
			ao2_lock(session);
			if (session->waiting_thread != AST_PTHREADT_NULL) {
				pthread_kill(session->waiting_thread, SIGURG);
			} else {
				/* We have an event to process, but the mansession is
				 * not waiting for it. We still need to indicate that there
				 * is an event waiting so that get_input processes the pending
				 * event instead of polling.
				 */
				session->pending_event = 1;
			}
			ao2_unlock(session);
			unref_mansession(session);
		}
		ao2_iterator_destroy(&i);
	}

	if (category != EVENT_FLAG_SHUTDOWN && !AST_RWLIST_EMPTY(&manager_hooks)) {
		AST_RWLIST_RDLOCK(&manager_hooks);
		AST_RWLIST_TRAVERSE(&manager_hooks, hook, list) {
			hook->helper(category, event, ast_str_buffer(buf));
		}
		AST_RWLIST_UNLOCK(&manager_hooks);
	}

	return 0;
}

/*! \brief
 * support functions to register/unregister AMI action handlers,
 */
int ast_manager_unregister(const char *action)
{
	struct manager_action *cur;

	AST_RWLIST_WRLOCK(&actions);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&actions, cur, list) {
		if (!strcasecmp(action, cur->action)) {
			AST_RWLIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&actions);

	if (cur) {
		/*
		 * We have removed the action object from the container so we
		 * are no longer in a hurry.
		 */
		ao2_lock(cur);
		cur->registered = 0;
		ao2_unlock(cur);

		ao2_t_ref(cur, -1, "action object removed from list");
		ast_verb(2, "Manager unregistered action %s\n", action);
	}

	return 0;
}

static int manager_state_cb(char *context, char *exten, struct ast_state_cb_info *info, void *data)
{
	/* Notify managers of change */
	char hint[512];

	ast_get_hint(hint, sizeof(hint), NULL, 0, NULL, context, exten);

	switch(info->reason) {
	case AST_HINT_UPDATE_DEVICE:
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an extension state has changed.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_CALL, "ExtensionStatus",
			"Exten: %s\r\n"
			"Context: %s\r\n"
			"Hint: %s\r\n"
			"Status: %d\r\n",
			exten,
			context,
			hint,
			info->exten_state);
		break;
	case AST_HINT_UPDATE_PRESENCE:
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when a presence state has changed.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_CALL, "PresenceStatus",
			"Exten: %s\r\n"
			"Context: %s\r\n"
			"Hint: %s\r\n"
			"Status: %s\r\n"
			"Subtype: %s\r\n"
			"Message: %s\r\n",
			exten,
			context,
			hint,
			ast_presence_state2str(info->presence_state),
			info->presence_subtype,
			info->presence_message);
		break;
	}
	return 0;
}

static int ast_manager_register_struct(struct manager_action *act)
{
	struct manager_action *cur, *prev = NULL;

	AST_RWLIST_WRLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		int ret;

		ret = strcasecmp(cur->action, act->action);
		if (ret == 0) {
			ast_log(LOG_WARNING, "Manager: Action '%s' already registered\n", act->action);
			AST_RWLIST_UNLOCK(&actions);
			return -1;
		}
		if (ret > 0) { /* Insert these alphabetically */
			prev = cur;
			break;
		}
	}

	ao2_t_ref(act, +1, "action object added to list");
	act->registered = 1;
	if (prev) {
		AST_RWLIST_INSERT_AFTER(&actions, prev, act, list);
	} else {
		AST_RWLIST_INSERT_HEAD(&actions, act, list);
	}

	ast_verb(2, "Manager registered action %s\n", act->action);

	AST_RWLIST_UNLOCK(&actions);

	return 0;
}

/*!
 * \internal
 * \brief Destroy the registered AMI action object.
 *
 * \param obj Object to destroy.
 *
 * \return Nothing
 */
static void action_destroy(void *obj)
{
	struct manager_action *doomed = obj;

	if (doomed->synopsis) {
		/* The string fields were initialized. */
		ast_string_field_free_memory(doomed);
	}
}

/*! \brief register a new command with manager, including online help. This is
	the preferred way to register a manager command */
int ast_manager_register2(const char *action, int auth, int (*func)(struct mansession *s, const struct message *m), struct ast_module *module, const char *synopsis, const char *description)
{
	struct manager_action *cur;

	cur = ao2_alloc(sizeof(*cur), action_destroy);
	if (!cur) {
		return -1;
	}
	if (ast_string_field_init(cur, 128)) {
		ao2_t_ref(cur, -1, "action object creation failed");
		return -1;
	}

	cur->action = action;
	cur->authority = auth;
	cur->func = func;
	cur->module = module;
#ifdef AST_XML_DOCS
	if (ast_strlen_zero(synopsis) && ast_strlen_zero(description)) {
		char *tmpxml;

		tmpxml = ast_xmldoc_build_synopsis("manager", action, NULL);
		ast_string_field_set(cur, synopsis, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_syntax("manager", action, NULL);
		ast_string_field_set(cur, syntax, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_description("manager", action, NULL);
		ast_string_field_set(cur, description, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_seealso("manager", action, NULL);
		ast_string_field_set(cur, seealso, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_arguments("manager", action, NULL);
		ast_string_field_set(cur, arguments, tmpxml);
		ast_free(tmpxml);

		cur->docsrc = AST_XML_DOC;
	} else
#endif
	{
		ast_string_field_set(cur, synopsis, synopsis);
		ast_string_field_set(cur, description, description);
#ifdef AST_XML_DOCS
		cur->docsrc = AST_STATIC_DOC;
#endif
	}
	if (ast_manager_register_struct(cur)) {
		ao2_t_ref(cur, -1, "action object registration failed");
		return -1;
	}

	ao2_t_ref(cur, -1, "action object registration successful");
	return 0;
}
/*! @}
 END Doxygen group */

/*
 * The following are support functions for AMI-over-http.
 * The common entry point is generic_http_callback(),
 * which extracts HTTP header and URI fields and reformats
 * them into AMI messages, locates a proper session
 * (using the mansession_id Cookie or GET variable),
 * and calls process_message() as for regular AMI clients.
 * When done, the output (which goes to a temporary file)
 * is read back into a buffer and reformatted as desired,
 * then fed back to the client over the original socket.
 */

enum output_format {
	FORMAT_RAW,
	FORMAT_HTML,
	FORMAT_XML,
};

static const char * const contenttype[] = {
	[FORMAT_RAW] = "plain",
	[FORMAT_HTML] = "html",
	[FORMAT_XML] =  "xml",
};

/*!
 * locate an http session in the list. The search key (ident) is
 * the value of the mansession_id cookie (0 is not valid and means
 * a session on the AMI socket).
 */
static struct mansession_session *find_session(uint32_t ident, int incinuse)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	struct ao2_iterator i;

	if (ident == 0) {
		return NULL;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return NULL;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if (session->managerid == ident && !session->needdestroy) {
			ast_atomic_fetchadd_int(&session->inuse, incinuse ? 1 : 0);
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return session;
}

/*!
 * locate an http session in the list.
 * The search keys (nonce) and (username) is value from received
 * "Authorization" http header.
 * As well as in find_session() function, the value of the nonce can't be zero.
 * (0 meansi, that the session used for AMI socket connection).
 * Flag (stale) is set, if client used valid, but old, nonce value.
 *
 */
static struct mansession_session *find_session_by_nonce(const char *username, unsigned long nonce, int *stale)
{
	struct mansession_session *session;
	struct ao2_container *sessions;
	struct ao2_iterator i;

	if (nonce == 0 || username == NULL || stale == NULL) {
		return NULL;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return NULL;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if (!strcasecmp(session->username, username) && session->managerid == nonce) {
			*stale = 0;
			break;
		} else if (!strcasecmp(session->username, username) && session->oldnonce == nonce) {
			*stale = 1;
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return session;
}

int astman_is_authed(uint32_t ident)
{
	int authed;
	struct mansession_session *session;

	if (!(session = find_session(ident, 0)))
		return 0;

	authed = (session->authenticated != 0);

	ao2_unlock(session);
	unref_mansession(session);

	return authed;
}

int astman_verify_session_readpermissions(uint32_t ident, int perm)
{
	int result = 0;
	struct mansession_session *session;
	struct ao2_container *sessions;
	struct ao2_iterator i;

	if (ident == 0) {
		return 0;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return 0;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if ((session->managerid == ident) && (session->readperm & perm)) {
			result = 1;
			ao2_unlock(session);
			unref_mansession(session);
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return result;
}

int astman_verify_session_writepermissions(uint32_t ident, int perm)
{
	int result = 0;
	struct mansession_session *session;
	struct ao2_container *sessions;
	struct ao2_iterator i;

	if (ident == 0) {
		return 0;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return 0;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if ((session->managerid == ident) && (session->writeperm & perm)) {
			result = 1;
			ao2_unlock(session);
			unref_mansession(session);
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return result;
}

/*
 * convert to xml with various conversion:
 * mode & 1	-> lowercase;
 * mode & 2	-> replace non-alphanumeric chars with underscore
 */
static void xml_copy_escape(struct ast_str **out, const char *src, int mode)
{
	/* store in a local buffer to avoid calling ast_str_append too often */
	char buf[256];
	char *dst = buf;
	int space = sizeof(buf);
	/* repeat until done and nothing to flush */
	for ( ; *src || dst != buf ; src++) {
		if (*src == '\0' || space < 10) {	/* flush */
			*dst++ = '\0';
			ast_str_append(out, 0, "%s", buf);
			dst = buf;
			space = sizeof(buf);
			if (*src == '\0') {
				break;
			}
		}

		if ( (mode & 2) && !isalnum(*src)) {
			*dst++ = '_';
			space--;
			continue;
		}
		switch (*src) {
		case '<':
			strcpy(dst, "&lt;");
			dst += 4;
			space -= 4;
			break;
		case '>':
			strcpy(dst, "&gt;");
			dst += 4;
			space -= 4;
			break;
		case '\"':
			strcpy(dst, "&quot;");
			dst += 6;
			space -= 6;
			break;
		case '\'':
			strcpy(dst, "&apos;");
			dst += 6;
			space -= 6;
			break;
		case '&':
			strcpy(dst, "&amp;");
			dst += 5;
			space -= 5;
			break;

		default:
			*dst++ = mode ? tolower(*src) : *src;
			space--;
		}
	}
}

struct variable_count {
	char *varname;
	int count;
};

static int variable_count_hash_fn(const void *vvc, const int flags)
{
	const struct variable_count *vc = vvc;

	return ast_str_hash(vc->varname);
}

static int variable_count_cmp_fn(void *obj, void *vstr, int flags)
{
	/* Due to the simplicity of struct variable_count, it makes no difference
	 * if you pass in objects or strings, the same operation applies. This is
	 * due to the fact that the hash occurs on the first element, which means
	 * the address of both the struct and the string are exactly the same. */
	struct variable_count *vc = obj;
	char *str = vstr;
	return !strcmp(vc->varname, str) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Convert the input into XML or HTML.
 * The input is supposed to be a sequence of lines of the form
 *	Name: value
 * optionally followed by a blob of unformatted text.
 * A blank line is a section separator. Basically, this is a
 * mixture of the format of Manager Interface and CLI commands.
 * The unformatted text is considered as a single value of a field
 * named 'Opaque-data'.
 *
 * At the moment the output format is the following (but it may
 * change depending on future requirements so don't count too
 * much on it when writing applications):
 *
 * General: the unformatted text is used as a value of
 * XML output:  to be completed
 *
 * \verbatim
 *   Each section is within <response type="object" id="xxx">
 *   where xxx is taken from ajaxdest variable or defaults to unknown
 *   Each row is reported as an attribute Name="value" of an XML
 *   entity named from the variable ajaxobjtype, default to "generic"
 * \endverbatim
 *
 * HTML output:
 *   each Name-value pair is output as a single row of a two-column table.
 *   Sections (blank lines in the input) are separated by a <HR>
 *
 */
static void xml_translate(struct ast_str **out, char *in, struct ast_variable *get_vars, enum output_format format)
{
	struct ast_variable *v;
	const char *dest = NULL;
	char *var, *val;
	const char *objtype = NULL;
	int in_data = 0;	/* parsing data */
	int inobj = 0;
	int xml = (format == FORMAT_XML);
	struct variable_count *vc = NULL;
	struct ao2_container *vco = NULL;

	if (xml) {
		/* dest and objtype need only for XML format */
		for (v = get_vars; v; v = v->next) {
			if (!strcasecmp(v->name, "ajaxdest")) {
				dest = v->value;
			} else if (!strcasecmp(v->name, "ajaxobjtype")) {
				objtype = v->value;
			}
		}
		if (ast_strlen_zero(dest)) {
			dest = "unknown";
		}
		if (ast_strlen_zero(objtype)) {
			objtype = "generic";
		}
	}

	/* we want to stop when we find an empty line */
	while (in && *in) {
		val = strsep(&in, "\r\n");	/* mark start and end of line */
		if (in && *in == '\n') {	/* remove trailing \n if any */
			in++;
		}
		ast_trim_blanks(val);
		ast_debug(5, "inobj %d in_data %d line <%s>\n", inobj, in_data, val);
		if (ast_strlen_zero(val)) {
			/* empty line */
			if (in_data) {
				/* close data in Opaque mode */
				ast_str_append(out, 0, xml ? "'" : "</td></tr>\n");
				in_data = 0;
			}

			if (inobj) {
				/* close block */
				ast_str_append(out, 0, xml ? " /></response>\n" :
					"<tr><td colspan=\"2\"><hr></td></tr>\r\n");
				inobj = 0;
				ao2_ref(vco, -1);
				vco = NULL;
			}
			continue;
		}

		if (!inobj) {
			/* start new block */
			if (xml) {
				ast_str_append(out, 0, "<response type='object' id='%s'><%s", dest, objtype);
			}
			vco = ao2_container_alloc(37, variable_count_hash_fn, variable_count_cmp_fn);
			inobj = 1;
		}

		if (in_data) {
			/* Process data field in Opaque mode. This is a
			 * followup, so we re-add line feeds. */
			ast_str_append(out, 0, xml ? "\n" : "<br>\n");
			xml_copy_escape(out, val, 0);   /* data field */
			continue;
		}

		/* We expect "Name: value" line here */
		var = strsep(&val, ":");
		if (val) {
			/* found the field name */
			val = ast_skip_blanks(val);
			ast_trim_blanks(var);
		} else {
			/* field name not found, switch to opaque mode */
			val = var;
			var = "Opaque-data";
			in_data = 1;
		}


		ast_str_append(out, 0, xml ? " " : "<tr><td>");
		if ((vc = ao2_find(vco, var, 0))) {
			vc->count++;
		} else {
			/* Create a new entry for this one */
			vc = ao2_alloc(sizeof(*vc), NULL);
			vc->varname = var;
			vc->count = 1;
			ao2_link(vco, vc);
		}

		xml_copy_escape(out, var, xml ? 1 | 2 : 0); /* data name */
		if (vc->count > 1) {
			ast_str_append(out, 0, "-%d", vc->count);
		}
		ao2_ref(vc, -1);
		ast_str_append(out, 0, xml ? "='" : "</td><td>");
		xml_copy_escape(out, val, 0);	/* data field */
		if (!in_data || !*in) {
			ast_str_append(out, 0, xml ? "'" : "</td></tr>\n");
		}
	}

	if (inobj) {
		ast_str_append(out, 0, xml ? " /></response>\n" :
			"<tr><td colspan=\"2\"><hr></td></tr>\r\n");
		ao2_ref(vco, -1);
	}
}

static void close_mansession_file(struct mansession *s)
{
	if (s->f) {
		if (fclose(s->f)) {
			ast_log(LOG_ERROR, "fclose() failed: %s\n", strerror(errno));
		}
		s->f = NULL;
		s->fd = -1;
	} else if (s->fd != -1) {
		/*
		 * Issuing shutdown() is necessary here to avoid a race
		 * condition where the last data written may not appear
		 * in the TCP stream.  See ASTERISK-23548
		 */
		shutdown(s->fd, SHUT_RDWR);
		if (close(s->fd)) {
			ast_log(LOG_ERROR, "close() failed: %s\n", strerror(errno));
		}
		s->fd = -1;
	} else {
		ast_log(LOG_ERROR, "Attempted to close file/file descriptor on mansession without a valid file or file descriptor.\n");
	}
}

static void process_output(struct mansession *s, struct ast_str **out, struct ast_variable *params, enum output_format format)
{
	char *buf;
	size_t l;

	if (!s->f)
		return;

	/* Ensure buffer is NULL-terminated */
	fprintf(s->f, "%c", 0);
	fflush(s->f);

	if ((l = ftell(s->f)) > 0) {
		if (MAP_FAILED == (buf = mmap(NULL, l, PROT_READ | PROT_WRITE, MAP_PRIVATE, s->fd, 0))) {
			ast_log(LOG_WARNING, "mmap failed.  Manager output was not processed\n");
		} else {
			if (format == FORMAT_XML || format == FORMAT_HTML) {
				xml_translate(out, buf, params, format);
			} else {
				ast_str_append(out, 0, "%s", buf);
			}
			munmap(buf, l);
		}
	} else if (format == FORMAT_XML || format == FORMAT_HTML) {
		xml_translate(out, "", params, format);
	}

	close_mansession_file(s);
}

static int generic_http_callback(struct ast_tcptls_session_instance *ser,
					     enum ast_http_method method,
					     enum output_format format,
					     const struct ast_sockaddr *remote_address, const char *uri,
					     struct ast_variable *get_params,
					     struct ast_variable *headers)
{
	struct mansession s = { .session = NULL, .tcptls_session = ser };
	struct mansession_session *session = NULL;
	uint32_t ident;
	int blastaway = 0;
	struct ast_variable *v;
	struct ast_variable *params = get_params;
	char template[] = "/tmp/ast-http-XXXXXX";	/* template for temporary file */
	struct ast_str *http_header = NULL, *out = NULL;
	struct message m = { 0 };
	unsigned int idx;
	size_t hdrlen;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD && method != AST_HTTP_POST) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return 0;
	}

	ident = ast_http_manid_from_vars(headers);

	if (!(session = find_session(ident, 1))) {

		/**/
		/* Create new session.
		 * While it is not in the list we don't need any locking
		 */
		if (!(session = build_mansession(remote_address))) {
			ast_http_request_close_on_completion(ser);
			ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
			return 0;
		}
		ao2_lock(session);
		session->send_events = 0;
		session->inuse = 1;
		/*!
		 * \note There is approximately a 1 in 1.8E19 chance that the following
		 * calculation will produce 0, which is an invalid ID, but due to the
		 * properties of the rand() function (and the constantcy of s), that
		 * won't happen twice in a row.
		 */
		while ((session->managerid = ast_random() ^ (unsigned long) session) == 0) {
		}
		session->last_ev = grab_last();
		AST_LIST_HEAD_INIT_NOLOCK(&session->datastores);
	}
	ao2_unlock(session);

	http_header = ast_str_create(128);
	out = ast_str_create(2048);

	ast_mutex_init(&s.lock);

	if (http_header == NULL || out == NULL) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (ast_str_create() out of memory)");
		goto generic_callback_out;
	}

	s.session = session;
	s.fd = mkstemp(template);	/* create a temporary file for command output */
	unlink(template);
	if (s.fd <= -1) {
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (mkstemp failed)");
		goto generic_callback_out;
	}
	s.f = fdopen(s.fd, "w+");
	if (!s.f) {
		ast_log(LOG_WARNING, "HTTP Manager, fdopen failed: %s!\n", strerror(errno));
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (fdopen failed)");
		close(s.fd);
		goto generic_callback_out;
	}

	if (method == AST_HTTP_POST) {
		params = ast_http_get_post_vars(ser, headers);
		if (!params) {
			switch (errno) {
			case EFBIG:
				ast_http_error(ser, 413, "Request Entity Too Large", "Body too large");
				close_mansession_file(&s);
				goto generic_callback_out;
			case ENOMEM:
				ast_http_request_close_on_completion(ser);
				ast_http_error(ser, 500, "Server Error", "Out of memory");
				close_mansession_file(&s);
				goto generic_callback_out;
			case EIO:
				ast_http_error(ser, 400, "Bad Request", "Error parsing request body");
				close_mansession_file(&s);
				goto generic_callback_out;
			}
		}
	}

	for (v = params; v && m.hdrcount < ARRAY_LEN(m.headers); v = v->next) {
		hdrlen = strlen(v->name) + strlen(v->value) + 3;
		m.headers[m.hdrcount] = ast_malloc(hdrlen);
		if (!m.headers[m.hdrcount]) {
			/* Allocation failure */
			continue;
		}
		snprintf((char *) m.headers[m.hdrcount], hdrlen, "%s: %s", v->name, v->value);
		ast_debug(1, "HTTP Manager add header %s\n", m.headers[m.hdrcount]);
		++m.hdrcount;
	}

	if (process_message(&s, &m)) {
		if (session->authenticated) {
			if (manager_displayconnects(session)) {
				ast_verb(2, "HTTP Manager '%s' logged off from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
			}
		} else {
			if (displayconnects) {
				ast_verb(2, "HTTP Connect attempt from '%s' unable to authenticate\n", ast_sockaddr_stringify_addr(&session->addr));
			}
		}
		session->needdestroy = 1;
	}

	/* Free request headers. */
	for (idx = 0; idx < m.hdrcount; ++idx) {
		ast_free((void *) m.headers[idx]);
		m.headers[idx] = NULL;
	}

	ast_str_append(&http_header, 0,
		"Content-type: text/%s\r\n"
		"Set-Cookie: mansession_id=\"%08x\"; Version=1; Max-Age=%d\r\n"
		"Pragma: SuppressEvents\r\n",
		contenttype[format],
		session->managerid, httptimeout);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "<ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		/*
		 * When handling AMI-over-HTTP in HTML format, we provide a simple form for
		 * debugging purposes. This HTML code should not be here, we
		 * should read from some config file...
		 */

#define ROW_FMT	"<tr><td colspan=\"2\" bgcolor=\"#f1f1ff\">%s</td></tr>\r\n"
#define TEST_STRING \
	"<form action=\"manager\" method=\"post\">\n\
	Action: <select name=\"action\">\n\
		<option value=\"\">-----&gt;</option>\n\
		<option value=\"login\">login</option>\n\
		<option value=\"command\">Command</option>\n\
		<option value=\"waitevent\">waitevent</option>\n\
		<option value=\"listcommands\">listcommands</option>\n\
	</select>\n\
	or <input name=\"action\"><br/>\n\
	CLI Command <input name=\"command\"><br>\n\
	user <input name=\"username\"> pass <input type=\"password\" name=\"secret\"><br>\n\
	<input type=\"submit\">\n</form>\n"

		ast_str_append(&out, 0, "<title>Asterisk&trade; Manager Interface</title>");
		ast_str_append(&out, 0, "<body bgcolor=\"#ffffff\"><table align=center bgcolor=\"#f1f1f1\" width=\"500\">\r\n");
		ast_str_append(&out, 0, ROW_FMT, "<h1>Manager Tester</h1>");
		ast_str_append(&out, 0, ROW_FMT, TEST_STRING);
	}

	process_output(&s, &out, params, format);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "</ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		ast_str_append(&out, 0, "</table></body>\r\n");
	}

	ao2_lock(session);
	/* Reset HTTP timeout.  If we're not authenticated, keep it extremely short */
	session->sessiontimeout = time(NULL) + ((session->authenticated || httptimeout < 5) ? httptimeout : 5);

	if (session->needdestroy) {
		if (session->inuse == 1) {
			ast_debug(1, "Need destroy, doing it now!\n");
			blastaway = 1;
		} else {
			ast_debug(1, "Need destroy, but can't do it yet!\n");
			if (session->waiting_thread != AST_PTHREADT_NULL) {
				pthread_kill(session->waiting_thread, SIGURG);
			}
			session->inuse--;
		}
	} else {
		session->inuse--;
	}
	ao2_unlock(session);

	ast_http_send(ser, method, 200, NULL, http_header, out, 0, 0);
	http_header = NULL;
	out = NULL;

generic_callback_out:
	ast_mutex_destroy(&s.lock);

	/* Clear resource */

	if (method == AST_HTTP_POST && params) {
		ast_variables_destroy(params);
	}
	ast_free(http_header);
	ast_free(out);

	if (session && blastaway) {
		session_destroy(session);
	} else if (session && session->f) {
		fclose(session->f);
		session->f = NULL;
	}

	return 0;
}

static int auth_http_callback(struct ast_tcptls_session_instance *ser,
					     enum ast_http_method method,
					     enum output_format format,
					     const struct ast_sockaddr *remote_address, const char *uri,
					     struct ast_variable *get_params,
					     struct ast_variable *headers)
{
	struct mansession_session *session = NULL;
	struct mansession s = { .session = NULL, .tcptls_session = ser };
	struct ast_variable *v, *params = get_params;
	char template[] = "/tmp/ast-http-XXXXXX";	/* template for temporary file */
	struct ast_str *http_header = NULL, *out = NULL;
	size_t result_size;
	struct message m = { 0 };
	unsigned int idx;
	size_t hdrlen;

	time_t time_now = time(NULL);
	unsigned long nonce = 0, nc;
	struct ast_http_digest d = { NULL, };
	struct ast_manager_user *user = NULL;
	int stale = 0;
	char resp_hash[256]="";
	/* Cache for user data */
	char u_username[80];
	int u_readperm;
	int u_writeperm;
	int u_writetimeout;
	int u_displayconnects;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD && method != AST_HTTP_POST) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return 0;
	}

	/* Find "Authorization: " header */
	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Authorization")) {
			break;
		}
	}

	if (!v || ast_strlen_zero(v->value)) {
		goto out_401; /* Authorization Header not present - send auth request */
	}

	/* Digest found - parse */
	if (ast_string_field_init(&d, 128)) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
		return 0;
	}

	if (ast_parse_digest(v->value, &d, 0, 1)) {
		/* Error in Digest - send new one */
		nonce = 0;
		goto out_401;
	}
	if (sscanf(d.nonce, "%30lx", &nonce) != 1) {
		ast_log(LOG_WARNING, "Received incorrect nonce in Digest <%s>\n", d.nonce);
		nonce = 0;
		goto out_401;
	}

	AST_RWLIST_WRLOCK(&users);
	user = get_manager_by_name_locked(d.username);
	if(!user) {
		AST_RWLIST_UNLOCK(&users);
		ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_sockaddr_stringify_addr(&session->addr), d.username);
		nonce = 0;
		goto out_401;
	}

	/* --- We have User for this auth, now check ACL */
	if (user->acl && !ast_apply_acl(user->acl, remote_address, "Manager User ACL:")) {
		AST_RWLIST_UNLOCK(&users);
		ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_sockaddr_stringify_addr(&session->addr), d.username);
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 403, "Permission denied", "Permission denied");
		return 0;
	}

	/* --- We have auth, so check it */

	/* compute the expected response to compare with what we received */
	{
		char a2[256];
		char a2_hash[256];
		char resp[256];

		/* XXX Now request method are hardcoded in A2 */
		snprintf(a2, sizeof(a2), "%s:%s", ast_get_http_method(method), d.uri);
		ast_md5_hash(a2_hash, a2);

		if (d.qop) {
			/* RFC 2617 */
			snprintf(resp, sizeof(resp), "%s:%08lx:%s:%s:auth:%s", user->a1_hash, nonce, d.nc, d.cnonce, a2_hash);
		}  else {
			/* RFC 2069 */
			snprintf(resp, sizeof(resp), "%s:%08lx:%s", user->a1_hash, nonce, a2_hash);
		}
		ast_md5_hash(resp_hash, resp);
	}

	if (strncasecmp(d.response, resp_hash, strlen(resp_hash))) {
		/* Something was wrong, so give the client to try with a new challenge */
		AST_RWLIST_UNLOCK(&users);
		nonce = 0;
		goto out_401;
	}

	/*
	 * User are pass Digest authentication.
	 * Now, cache the user data and unlock user list.
	 */
	ast_copy_string(u_username, user->username, sizeof(u_username));
	u_readperm = user->readperm;
	u_writeperm = user->writeperm;
	u_displayconnects = user->displayconnects;
	u_writetimeout = user->writetimeout;
	AST_RWLIST_UNLOCK(&users);

	if (!(session = find_session_by_nonce(d.username, nonce, &stale))) {
		/*
		 * Create new session.
		 * While it is not in the list we don't need any locking
		 */
		if (!(session = build_mansession(remote_address))) {
			ast_http_request_close_on_completion(ser);
			ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
			return 0;
		}
		ao2_lock(session);

		ast_copy_string(session->username, u_username, sizeof(session->username));
		session->managerid = nonce;
		session->last_ev = grab_last();
		AST_LIST_HEAD_INIT_NOLOCK(&session->datastores);

		session->readperm = u_readperm;
		session->writeperm = u_writeperm;
		session->writetimeout = u_writetimeout;

		if (u_displayconnects) {
			ast_verb(2, "HTTP Manager '%s' logged in from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
		}
		session->noncetime = session->sessionstart = time_now;
		session->authenticated = 1;
	} else if (stale) {
		/*
		 * Session found, but nonce is stale.
		 *
		 * This could be because an old request (w/old nonce) arrived.
		 *
		 * This may be as the result of http proxy usage (separate delay or
		 * multipath) or in a situation where a page was refreshed too quickly
		 * (seen in Firefox).
		 *
		 * In this situation, we repeat the 401 auth with the current nonce
		 * value.
		 */
		nonce = session->managerid;
		ao2_unlock(session);
		stale = 1;
		goto out_401;
	} else {
		sscanf(d.nc, "%30lx", &nc);
		if (session->nc >= nc || ((time_now - session->noncetime) > 62) ) {
			/*
			 * Nonce time expired (> 2 minutes) or something wrong with nonce
			 * counter.
			 *
			 * Create new nonce key and resend Digest auth request. Old nonce
			 * is saved for stale checking...
			 */
			session->nc = 0; /* Reset nonce counter */
			session->oldnonce = session->managerid;
			nonce = session->managerid = ast_random();
			session->noncetime = time_now;
			ao2_unlock(session);
			stale = 1;
			goto out_401;
		} else {
			session->nc = nc; /* All OK, save nonce counter */
		}
	}


	/* Reset session timeout. */
	session->sessiontimeout = time(NULL) + (httptimeout > 5 ? httptimeout : 5);
	ao2_unlock(session);

	ast_mutex_init(&s.lock);
	s.session = session;
	s.fd = mkstemp(template);	/* create a temporary file for command output */
	unlink(template);
	if (s.fd <= -1) {
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (mkstemp failed)");
		goto auth_callback_out;
	}
	s.f = fdopen(s.fd, "w+");
	if (!s.f) {
		ast_log(LOG_WARNING, "HTTP Manager, fdopen failed: %s!\n", strerror(errno));
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (fdopen failed)");
		close(s.fd);
		goto auth_callback_out;
	}

	if (method == AST_HTTP_POST) {
		params = ast_http_get_post_vars(ser, headers);
		if (!params) {
			switch (errno) {
			case EFBIG:
				ast_http_error(ser, 413, "Request Entity Too Large", "Body too large");
				close_mansession_file(&s);
				goto auth_callback_out;
			case ENOMEM:
				ast_http_request_close_on_completion(ser);
				ast_http_error(ser, 500, "Server Error", "Out of memory");
				close_mansession_file(&s);
				goto auth_callback_out;
			case EIO:
				ast_http_error(ser, 400, "Bad Request", "Error parsing request body");
				close_mansession_file(&s);
				goto auth_callback_out;
			}
		}
	}

	for (v = params; v && m.hdrcount < ARRAY_LEN(m.headers); v = v->next) {
		hdrlen = strlen(v->name) + strlen(v->value) + 3;
		m.headers[m.hdrcount] = ast_malloc(hdrlen);
		if (!m.headers[m.hdrcount]) {
			/* Allocation failure */
			continue;
		}
		snprintf((char *) m.headers[m.hdrcount], hdrlen, "%s: %s", v->name, v->value);
		ast_verb(4, "HTTP Manager add header %s\n", m.headers[m.hdrcount]);
		++m.hdrcount;
	}

	if (process_message(&s, &m)) {
		if (u_displayconnects) {
			ast_verb(2, "HTTP Manager '%s' logged off from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
		}

		session->needdestroy = 1;
	}

	/* Free request headers. */
	for (idx = 0; idx < m.hdrcount; ++idx) {
		ast_free((void *) m.headers[idx]);
		m.headers[idx] = NULL;
	}

	result_size = ftell(s.f); /* Calculate approx. size of result */

	http_header = ast_str_create(80);
	out = ast_str_create(result_size * 2 + 512);
	if (http_header == NULL || out == NULL) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (ast_str_create() out of memory)");
		close_mansession_file(&s);
		goto auth_callback_out;
	}

	ast_str_append(&http_header, 0, "Content-type: text/%s\r\n", contenttype[format]);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "<ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		ast_str_append(&out, 0,
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<html><head>\r\n"
		"<title>Asterisk&trade; Manager Interface</title>\r\n"
		"</head><body style=\"background-color: #ffffff;\">\r\n"
		"<form method=\"POST\">\r\n"
		"<table align=\"center\" style=\"background-color: #f1f1f1;\" width=\"500\">\r\n"
		"<tr><th colspan=\"2\" style=\"background-color: #f1f1ff;\"><h1>Manager Tester</h1></th></tr>\r\n"
		"<tr><th colspan=\"2\" style=\"background-color: #f1f1ff;\">Action: <input name=\"action\" /> Cmd: <input name=\"command\" /><br>"
		"<input type=\"submit\" value=\"Send request\" /></th></tr>\r\n");
	}

	process_output(&s, &out, params, format);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "</ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		ast_str_append(&out, 0, "</table></form></body></html>\r\n");
	}

	ast_http_send(ser, method, 200, NULL, http_header, out, 0, 0);
	http_header = NULL;
	out = NULL;

auth_callback_out:
	ast_mutex_destroy(&s.lock);

	/* Clear resources and unlock manager session */
	if (method == AST_HTTP_POST && params) {
		ast_variables_destroy(params);
	}

	ast_free(http_header);
	ast_free(out);

	ao2_lock(session);
	if (session->f) {
		fclose(session->f);
	}
	session->f = NULL;
	session->fd = -1;
	ao2_unlock(session);

	if (session->needdestroy) {
		ast_debug(1, "Need destroy, doing it now!\n");
		session_destroy(session);
	}
	ast_string_field_free_memory(&d);
	return 0;

out_401:
	if (!nonce) {
		nonce = ast_random();
	}

	ast_http_auth(ser, global_realm, nonce, nonce, stale, NULL);
	ast_string_field_free_memory(&d);
	return 0;
}

static int manager_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params,  struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = generic_http_callback(ser, method, FORMAT_HTML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int mxml_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = generic_http_callback(ser, method, FORMAT_XML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int rawman_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = generic_http_callback(ser, method, FORMAT_RAW, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static struct ast_http_uri rawmanuri = {
	.description = "Raw HTTP Manager Event Interface",
	.uri = "rawman",
	.callback = rawman_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri manageruri = {
	.description = "HTML Manager Event Interface",
	.uri = "manager",
	.callback = manager_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri managerxmluri = {
	.description = "XML Manager Event Interface",
	.uri = "mxml",
	.callback = mxml_http_callback,
	.data = NULL,
	.key = __FILE__,
};


/* Callback with Digest authentication */
static int auth_manager_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params,  struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = auth_http_callback(ser, method, FORMAT_HTML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int auth_mxml_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = auth_http_callback(ser, method, FORMAT_XML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int auth_rawman_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = auth_http_callback(ser, method, FORMAT_RAW, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static struct ast_http_uri arawmanuri = {
	.description = "Raw HTTP Manager Event Interface w/Digest authentication",
	.uri = "arawman",
	.has_subtree = 0,
	.callback = auth_rawman_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri amanageruri = {
	.description = "HTML Manager Event Interface w/Digest authentication",
	.uri = "amanager",
	.has_subtree = 0,
	.callback = auth_manager_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri amanagerxmluri = {
	.description = "XML Manager Event Interface w/Digest authentication",
	.uri = "amxml",
	.has_subtree = 0,
	.callback = auth_mxml_http_callback,
	.data = NULL,
	.key = __FILE__,
};

/*! \brief Get number of logged in sessions for a login name */
static int get_manager_sessions_cb(void *obj, void *arg, void *data, int flags)
{
	struct mansession_session *session = obj;
	const char *login = (char *)arg;
	int *no_sessions = data;

	if (strcasecmp(session->username, login) == 0) {
		(*no_sessions)++;
	}

	return 0;
}


/*! \brief  ${AMI_CLIENT()} Dialplan function - reads manager client data */
static int function_amiclient(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_manager_user *user = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(name);
		AST_APP_ARG(param);
	);


	if (ast_strlen_zero(data) ) {
		ast_log(LOG_WARNING, "AMI_CLIENT() requires two arguments: AMI_CLIENT(<name>[,<arg>])\n");
		return -1;
	}
	AST_STANDARD_APP_ARGS(args, data);
	args.name = ast_strip(args.name);
	args.param = ast_strip(args.param);

	AST_RWLIST_RDLOCK(&users);
	if (!(user = get_manager_by_name_locked(args.name))) {
		AST_RWLIST_UNLOCK(&users);
		ast_log(LOG_ERROR, "There's no manager user called : \"%s\"\n", args.name);
		return -1;
	}
	AST_RWLIST_UNLOCK(&users);

	if (!strcasecmp(args.param, "sessions")) {
		int no_sessions = 0;
		struct ao2_container *sessions;

		sessions = ao2_global_obj_ref(mgr_sessions);
		if (sessions) {
			ao2_callback_data(sessions, 0, get_manager_sessions_cb, /*login name*/ data, &no_sessions);
			ao2_ref(sessions, -1);
		}
		snprintf(buf, len, "%d", no_sessions);
	} else {
		ast_log(LOG_ERROR, "Invalid arguments provided to function AMI_CLIENT: %s\n", args.param);
		return -1;

	}

	return 0;
}


/*! \brief description of AMI_CLIENT dialplan function */
static struct ast_custom_function managerclient_function = {
	.name = "AMI_CLIENT",
	.read = function_amiclient,
	.read_max = 12,
};

static int webregged = 0;

/*! \brief cleanup code called at each iteration of server_root,
 * guaranteed to happen every 5 seconds at most
 */
static void purge_old_stuff(void *data)
{
	purge_sessions(1);
	purge_events();
}

static struct ast_tls_config ami_tls_cfg;
static struct ast_tcptls_session_args ami_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = NULL,
	.poll_timeout = 5000,	/* wake up every 5 seconds */
	.periodic_fn = purge_old_stuff,
	.name = "AMI server",
	.accept_fn = ast_tcptls_server_root,	/* thread doing the accept() */
	.worker_fn = session_do,	/* thread handling the session */
};

static struct ast_tcptls_session_args amis_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = &ami_tls_cfg,
	.poll_timeout = -1,	/* the other does the periodic cleanup */
	.name = "AMI TLS server",
	.accept_fn = ast_tcptls_server_root,	/* thread doing the accept() */
	.worker_fn = session_do,	/* thread handling the session */
};

/*! \brief CLI command manager show settings */
static char *handle_manager_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show settings";
		e->usage =
			"Usage: manager show settings\n"
			"       Provides detailed list of the configuration of the Manager.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#define FORMAT "  %-25.25s  %-15.55s\n"
#define FORMAT2 "  %-25.25s  %-15d\n"
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, "\nGlobal Settings:\n");
	ast_cli(a->fd, "----------------\n");
	ast_cli(a->fd, FORMAT, "Manager (AMI):", AST_CLI_YESNO(manager_enabled));
	ast_cli(a->fd, FORMAT, "Web Manager (AMI/HTTP):", AST_CLI_YESNO(webmanager_enabled));
	ast_cli(a->fd, FORMAT, "TCP Bindaddress:", manager_enabled != 0 ? ast_sockaddr_stringify(&ami_desc.local_address) : "Disabled");
	ast_cli(a->fd, FORMAT2, "HTTP Timeout (minutes):", httptimeout);
	ast_cli(a->fd, FORMAT, "TLS Enable:", AST_CLI_YESNO(ami_tls_cfg.enabled));
	ast_cli(a->fd, FORMAT, "TLS Bindaddress:", ami_tls_cfg.enabled != 0 ? ast_sockaddr_stringify(&amis_desc.local_address) : "Disabled");
	ast_cli(a->fd, FORMAT, "TLS Certfile:", ami_tls_cfg.certfile);
	ast_cli(a->fd, FORMAT, "TLS Privatekey:", ami_tls_cfg.pvtfile);
	ast_cli(a->fd, FORMAT, "TLS Cipher:", ami_tls_cfg.cipher);
	ast_cli(a->fd, FORMAT, "Allow multiple login:", AST_CLI_YESNO(allowmultiplelogin));
	ast_cli(a->fd, FORMAT, "Display connects:", AST_CLI_YESNO(displayconnects));
	ast_cli(a->fd, FORMAT, "Timestamp events:", AST_CLI_YESNO(timestampevents));
	ast_cli(a->fd, FORMAT, "Channel vars:", S_OR(manager_channelvars, ""));
	ast_cli(a->fd, FORMAT, "Debug:", AST_CLI_YESNO(manager_debug));
#undef FORMAT
#undef FORMAT2

	return CLI_SUCCESS;
}

#ifdef AST_XML_DOCS

static int ast_xml_doc_item_cmp_fn(const void *a, const void *b)
{
	struct ast_xml_doc_item **item_a = (struct ast_xml_doc_item **)a;
	struct ast_xml_doc_item **item_b = (struct ast_xml_doc_item **)b;
	return strcmp((*item_a)->name, (*item_b)->name);
}

static char *handle_manager_show_events(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *events;
	struct ao2_iterator *it_events;
	struct ast_xml_doc_item *item;
	struct ast_xml_doc_item **items;
	struct ast_str *buffer;
	int i = 0, totalitems = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show events";
		e->usage =
			"Usage: manager show events\n"
				"	Prints a listing of the available Asterisk manager interface events.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return CLI_SUCCESS;
	}

	events = ao2_global_obj_ref(event_docs);
	if (!events) {
		ast_cli(a->fd, "No manager event documentation loaded\n");
		ast_free(buffer);
		return CLI_SUCCESS;
	}

	ao2_lock(events);
	if (!(it_events = ao2_callback(events, OBJ_MULTIPLE | OBJ_NOLOCK, NULL, NULL))) {
		ao2_unlock(events);
		ast_log(AST_LOG_ERROR, "Unable to create iterator for events container\n");
		ast_free(buffer);
		ao2_ref(events, -1);
		return CLI_SUCCESS;
	}
	if (!(items = ast_calloc(sizeof(struct ast_xml_doc_item *), ao2_container_count(events)))) {
		ao2_unlock(events);
		ast_log(AST_LOG_ERROR, "Unable to create temporary sorting array for events\n");
		ao2_iterator_destroy(it_events);
		ast_free(buffer);
		ao2_ref(events, -1);
		return CLI_SUCCESS;
	}
	ao2_unlock(events);

	while ((item = ao2_iterator_next(it_events))) {
		items[totalitems++] = item;
		ao2_ref(item, -1);
	}

	qsort(items, totalitems, sizeof(struct ast_xml_doc_item *), ast_xml_doc_item_cmp_fn);

	ast_cli(a->fd, "Events:\n");
	ast_cli(a->fd, "  --------------------  --------------------  --------------------  \n");
	for (i = 0; i < totalitems; i++) {
		ast_str_append(&buffer, 0, "  %-20.20s", items[i]->name);
		if ((i + 1) % 3 == 0) {
			ast_cli(a->fd, "%s\n", ast_str_buffer(buffer));
			ast_str_set(&buffer, 0, "%s", "");
		}
	}
	if ((i + 1) % 3 != 0) {
		ast_cli(a->fd, "%s\n", ast_str_buffer(buffer));
	}

	ao2_iterator_destroy(it_events);
	ast_free(items);
	ao2_ref(events, -1);
	ast_free(buffer);

	return CLI_SUCCESS;
}

static char *handle_manager_show_event(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, events, NULL, ao2_cleanup);
	struct ao2_iterator it_events;
	struct ast_xml_doc_item *item, *temp;
	int length;
	int which;
	char *match = NULL;
	char syntax_title[64], description_title[64], synopsis_title[64], seealso_title[64], arguments_title[64];

	if (cmd == CLI_INIT) {
		e->command = "manager show event";
		e->usage =
			"Usage: manager show event <eventname>\n"
			"       Provides a detailed description a Manager interface event.\n";
		return NULL;
	}

	events = ao2_global_obj_ref(event_docs);
	if (!events) {
		ast_cli(a->fd, "No manager event documentation loaded\n");
		return CLI_SUCCESS;
	}

	if (cmd == CLI_GENERATE) {
		length = strlen(a->word);
		which = 0;
		it_events = ao2_iterator_init(events, 0);
		while ((item = ao2_iterator_next(&it_events))) {
			if (!strncasecmp(a->word, item->name, length) && ++which > a->n) {
				match = ast_strdup(item->name);
				ao2_ref(item, -1);
				break;
			}
			ao2_ref(item, -1);
		}
		ao2_iterator_destroy(&it_events);
		return match;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(item = ao2_find(events, a->argv[3], OBJ_KEY))) {
		ast_cli(a->fd, "Could not find event '%s'\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	term_color(synopsis_title, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
	term_color(description_title, "[Description]\n", COLOR_MAGENTA, 0, 40);
	term_color(syntax_title, "[Syntax]\n", COLOR_MAGENTA, 0, 40);
	term_color(seealso_title, "[See Also]\n", COLOR_MAGENTA, 0, 40);
	term_color(arguments_title, "[Arguments]\n", COLOR_MAGENTA, 0, 40);

	ast_cli(a->fd, "Event: %s\n", a->argv[3]);
	for (temp = item; temp; temp = temp->next) {
		if (!ast_strlen_zero(ast_str_buffer(temp->synopsis))) {
			char *synopsis = ast_xmldoc_printable(ast_str_buffer(temp->synopsis), 1);
			ast_cli(a->fd, "%s%s\n\n", synopsis_title, synopsis);
			ast_free(synopsis);
		}
		if (!ast_strlen_zero(ast_str_buffer(temp->syntax))) {
			char *syntax = ast_xmldoc_printable(ast_str_buffer(temp->syntax), 1);
			ast_cli(a->fd, "%s%s\n\n", syntax_title, syntax);
			ast_free(syntax);
		}
		if (!ast_strlen_zero(ast_str_buffer(temp->description))) {
			char *description = ast_xmldoc_printable(ast_str_buffer(temp->description), 1);
			ast_cli(a->fd, "%s%s\n\n", description_title, description);
			ast_free(description);
		}
		if (!ast_strlen_zero(ast_str_buffer(temp->arguments))) {
			char *arguments = ast_xmldoc_printable(ast_str_buffer(temp->arguments), 1);
			ast_cli(a->fd, "%s%s\n\n", arguments_title, arguments);
			ast_free(arguments);
		}
		if (!ast_strlen_zero(ast_str_buffer(temp->seealso))) {
			char *seealso = ast_xmldoc_printable(ast_str_buffer(temp->seealso), 1);
			ast_cli(a->fd, "%s%s\n\n", seealso_title, seealso);
			ast_free(seealso);
		}
	}

	ao2_ref(item, -1);
	return CLI_SUCCESS;
}

#endif

static struct ast_cli_entry cli_manager[] = {
	AST_CLI_DEFINE(handle_showmancmd, "Show a manager interface command"),
	AST_CLI_DEFINE(handle_showmancmds, "List manager interface commands"),
	AST_CLI_DEFINE(handle_showmanconn, "List connected manager interface users"),
	AST_CLI_DEFINE(handle_showmaneventq, "List manager interface queued events"),
	AST_CLI_DEFINE(handle_showmanagers, "List configured manager users"),
	AST_CLI_DEFINE(handle_showmanager, "Display information on a specific manager user"),
	AST_CLI_DEFINE(handle_mandebug, "Show, enable, disable debugging of the manager code"),
	AST_CLI_DEFINE(handle_manager_reload, "Reload manager configurations"),
	AST_CLI_DEFINE(handle_manager_show_settings, "Show manager global settings"),
#ifdef AST_XML_DOCS
	AST_CLI_DEFINE(handle_manager_show_events, "List manager interface events"),
	AST_CLI_DEFINE(handle_manager_show_event, "Show a manager interface event"),
#endif
};

/*!
 * \internal
 * \brief Load the config channelvars variable.
 *
 * \param var Config variable to load.
 *
 * \return Nothing
 */
static void load_channelvars(struct ast_variable *var)
{
        char *parse = NULL;
        AST_DECLARE_APP_ARGS(args,
                AST_APP_ARG(vars)[MAX_VARS];
        );

	ast_free(manager_channelvars);
	manager_channelvars = ast_strdup(var->value);

	/* parse the setting */
	parse = ast_strdupa(manager_channelvars);
	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_set_manager_vars(args.argc, args.vars);
}

#ifdef TEST_FRAMEWORK

static void test_suite_event_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_test_suite_message_payload *payload;
	struct ast_json *blob;
	const char *type;

	if (stasis_message_type(message) != ast_test_suite_message_type()) {
		return;
	}

	payload = stasis_message_data(message);
	if (!payload) {
		return;
	}
	blob = ast_test_suite_get_blob(payload);
	if (!blob) {
		return;
	}

	type = ast_json_string_get(ast_json_object_get(blob, "type"));
	if (ast_strlen_zero(type) || strcmp("testevent", type)) {
		return;
	}

	manager_event(EVENT_FLAG_TEST, "TestEvent",
		"Type: StateChange\r\n"
		"State: %s\r\n"
		"AppFile: %s\r\n"
		"AppFunction: %s\r\n"
		"AppLine: %jd\r\n"
		"%s\r\n",
		ast_json_string_get(ast_json_object_get(blob, "state")),
		ast_json_string_get(ast_json_object_get(blob, "appfile")),
		ast_json_string_get(ast_json_object_get(blob, "appfunction")),
		ast_json_integer_get(ast_json_object_get(blob, "line")),
		ast_json_string_get(ast_json_object_get(blob, "data")));
}

#endif

/*!
 * \internal
 * \brief Free a user record.  Should already be removed from the list
 */
static void manager_free_user(struct ast_manager_user *user)
{
	ast_free(user->a1_hash);
	ast_free(user->secret);
	if (user->whitefilters) {
		ao2_t_ref(user->whitefilters, -1, "decrement ref for white container, should be last one");
	}
	if (user->blackfilters) {
		ao2_t_ref(user->blackfilters, -1, "decrement ref for black container, should be last one");
	}
	user->acl = ast_free_acl_list(user->acl);
	ast_variables_destroy(user->chanvars);
	ast_free(user);
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void manager_shutdown(void)
{
	struct ast_manager_user *user;

	/* This event is not actually transmitted, but causes all TCP sessions to be closed */
	manager_event(EVENT_FLAG_SHUTDOWN, "CloseSession", "CloseSession: true\r\n");

	ast_manager_unregister("Ping");
	ast_manager_unregister("Events");
	ast_manager_unregister("Logoff");
	ast_manager_unregister("Login");
	ast_manager_unregister("Challenge");
	ast_manager_unregister("Hangup");
	ast_manager_unregister("Status");
	ast_manager_unregister("Setvar");
	ast_manager_unregister("Getvar");
	ast_manager_unregister("GetConfig");
	ast_manager_unregister("GetConfigJSON");
	ast_manager_unregister("UpdateConfig");
	ast_manager_unregister("CreateConfig");
	ast_manager_unregister("ListCategories");
	ast_manager_unregister("Redirect");
	ast_manager_unregister("Atxfer");
	ast_manager_unregister("Originate");
	ast_manager_unregister("Command");
	ast_manager_unregister("ExtensionState");
	ast_manager_unregister("PresenceState");
	ast_manager_unregister("AbsoluteTimeout");
	ast_manager_unregister("MailboxStatus");
	ast_manager_unregister("MailboxCount");
	ast_manager_unregister("ListCommands");
	ast_manager_unregister("SendText");
	ast_manager_unregister("UserEvent");
	ast_manager_unregister("WaitEvent");
	ast_manager_unregister("CoreSettings");
	ast_manager_unregister("CoreStatus");
	ast_manager_unregister("Reload");
	ast_manager_unregister("CoreShowChannels");
	ast_manager_unregister("ModuleLoad");
	ast_manager_unregister("ModuleCheck");
	ast_manager_unregister("AOCMessage");
	ast_manager_unregister("Filter");
	ast_manager_unregister("BlindTransfer");
	ast_custom_function_unregister(&managerclient_function);
	ast_cli_unregister_multiple(cli_manager, ARRAY_LEN(cli_manager));

#ifdef AST_XML_DOCS
	ao2_t_global_obj_release(event_docs, "Dispose of event_docs");
#endif

#ifdef TEST_FRAMEWORK
	stasis_unsubscribe(test_suite_sub);
#endif

	if (stasis_router) {
		stasis_message_router_unsubscribe_and_join(stasis_router);
		stasis_router = NULL;
	}
	stasis_forward_cancel(rtp_topic_forwarder);
	rtp_topic_forwarder = NULL;
	stasis_forward_cancel(security_topic_forwarder);
	security_topic_forwarder = NULL;
	ao2_cleanup(manager_topic);
	manager_topic = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(ast_manager_get_generic_type);

	ast_tcptls_server_stop(&ami_desc);
	ast_tcptls_server_stop(&amis_desc);

	ast_free(ami_tls_cfg.certfile);
	ami_tls_cfg.certfile = NULL;
	ast_free(ami_tls_cfg.pvtfile);
	ami_tls_cfg.pvtfile = NULL;
	ast_free(ami_tls_cfg.cipher);
	ami_tls_cfg.cipher = NULL;

	ao2_global_obj_release(mgr_sessions);

	while ((user = AST_LIST_REMOVE_HEAD(&users, list))) {
		manager_free_user(user);
	}
}


/*! \brief Initialize all \ref stasis topics and routers used by the various
 * sub-components of AMI
 */
static int manager_subscriptions_init(void)
{
	int res = 0;

	rtp_topic_forwarder = stasis_forward_all(ast_rtp_topic(), manager_topic);
	if (!rtp_topic_forwarder) {
		return -1;
	}

	security_topic_forwarder = stasis_forward_all(ast_security_topic(), manager_topic);
	if (!security_topic_forwarder) {
		return -1;
	}

	stasis_router = stasis_message_router_create(manager_topic);
	if (!stasis_router) {
		return -1;
	}

	res |= stasis_message_router_set_default(stasis_router,
		manager_default_msg_cb, NULL);

	res |= stasis_message_router_add(stasis_router,
		ast_manager_get_generic_type(), manager_generic_msg_cb, NULL);

	if (res != 0) {
		return -1;
	}
	return 0;
}

static int subscribe_all(void)
{
	if (manager_subscriptions_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager subscriptions\n");
		return -1;
	}
	if (manager_system_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager system handling\n");
		return -1;
	}
	if (manager_channels_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager channel handling\n");
		return -1;
	}
	if (manager_mwi_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager MWI handling\n");
		return -1;
	}
	if (manager_bridging_init()) {
		return -1;
	}
	if (manager_endpoints_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager endpoints handling\n");
		return -1;
	}

	subscribed = 1;
	return 0;
}

static void manager_set_defaults(void)
{
	manager_enabled = 0;
	displayconnects = 1;
	broken_events_action = 0;
	authtimeout = 30;
	authlimit = 50;
	manager_debug = 0;		/* Debug disabled by default */

	/* default values */
	ast_copy_string(global_realm, S_OR(ast_config_AST_SYSTEM_NAME, DEFAULT_REALM),
		sizeof(global_realm));
	ast_sockaddr_setnull(&ami_desc.local_address);
	ast_sockaddr_setnull(&amis_desc.local_address);

	ami_tls_cfg.enabled = 0;
	ast_free(ami_tls_cfg.certfile);
	ami_tls_cfg.certfile = ast_strdup(AST_CERTFILE);
	ast_free(ami_tls_cfg.pvtfile);
	ami_tls_cfg.pvtfile = ast_strdup("");
	ast_free(ami_tls_cfg.cipher);
	ami_tls_cfg.cipher = ast_strdup("");
}

static int __init_manager(int reload, int by_external_config)
{
	struct ast_config *ucfg = NULL, *cfg = NULL;
	const char *val;
	char *cat = NULL;
	int newhttptimeout = 60;
	struct ast_manager_user *user = NULL;
	struct ast_variable *var;
	struct ast_flags config_flags = { (reload && !by_external_config) ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	char a1[256];
	char a1_hash[256];
	struct ast_sockaddr ami_desc_local_address_tmp;
	struct ast_sockaddr amis_desc_local_address_tmp;
	int tls_was_enabled = 0;
	int acl_subscription_flag = 0;

	if (!reload) {
		struct ao2_container *sessions;
#ifdef AST_XML_DOCS
		struct ao2_container *temp_event_docs;
#endif
		int res;

		ast_register_atexit(manager_shutdown);

		res = STASIS_MESSAGE_TYPE_INIT(ast_manager_get_generic_type);
		if (res != 0) {
			return -1;
		}
		manager_topic = stasis_topic_create("manager_topic");
		if (!manager_topic) {
			return -1;
		}

		/* Register default actions */
		ast_manager_register_xml_core("Ping", 0, action_ping);
		ast_manager_register_xml_core("Events", 0, action_events);
		ast_manager_register_xml_core("Logoff", 0, action_logoff);
		ast_manager_register_xml_core("Login", 0, action_login);
		ast_manager_register_xml_core("Challenge", 0, action_challenge);
		ast_manager_register_xml_core("Hangup", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, action_hangup);
		ast_manager_register_xml_core("Status", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_status);
		ast_manager_register_xml_core("Setvar", EVENT_FLAG_CALL, action_setvar);
		ast_manager_register_xml_core("Getvar", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_getvar);
		ast_manager_register_xml_core("GetConfig", EVENT_FLAG_SYSTEM | EVENT_FLAG_CONFIG, action_getconfig);
		ast_manager_register_xml_core("GetConfigJSON", EVENT_FLAG_SYSTEM | EVENT_FLAG_CONFIG, action_getconfigjson);
		ast_manager_register_xml_core("UpdateConfig", EVENT_FLAG_CONFIG, action_updateconfig);
		ast_manager_register_xml_core("CreateConfig", EVENT_FLAG_CONFIG, action_createconfig);
		ast_manager_register_xml_core("ListCategories", EVENT_FLAG_CONFIG, action_listcategories);
		ast_manager_register_xml_core("Redirect", EVENT_FLAG_CALL, action_redirect);
		ast_manager_register_xml_core("Atxfer", EVENT_FLAG_CALL, action_atxfer);
		ast_manager_register_xml_core("Originate", EVENT_FLAG_ORIGINATE, action_originate);
		ast_manager_register_xml_core("Command", EVENT_FLAG_COMMAND, action_command);
		ast_manager_register_xml_core("ExtensionState", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_extensionstate);
		ast_manager_register_xml_core("PresenceState", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_presencestate);
		ast_manager_register_xml_core("AbsoluteTimeout", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, action_timeout);
		ast_manager_register_xml_core("MailboxStatus", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_mailboxstatus);
		ast_manager_register_xml_core("MailboxCount", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_mailboxcount);
		ast_manager_register_xml_core("ListCommands", 0, action_listcommands);
		ast_manager_register_xml_core("SendText", EVENT_FLAG_CALL, action_sendtext);
		ast_manager_register_xml_core("UserEvent", EVENT_FLAG_USER, action_userevent);
		ast_manager_register_xml_core("WaitEvent", 0, action_waitevent);
		ast_manager_register_xml_core("CoreSettings", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_coresettings);
		ast_manager_register_xml_core("CoreStatus", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_corestatus);
		ast_manager_register_xml_core("Reload", EVENT_FLAG_CONFIG | EVENT_FLAG_SYSTEM, action_reload);
		ast_manager_register_xml_core("CoreShowChannels", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_coreshowchannels);
		ast_manager_register_xml_core("ModuleLoad", EVENT_FLAG_SYSTEM, manager_moduleload);
		ast_manager_register_xml_core("ModuleCheck", EVENT_FLAG_SYSTEM, manager_modulecheck);
		ast_manager_register_xml_core("AOCMessage", EVENT_FLAG_AOC, action_aocmessage);
		ast_manager_register_xml_core("Filter", EVENT_FLAG_SYSTEM, action_filter);
		ast_manager_register_xml_core("BlindTransfer", EVENT_FLAG_CALL, action_blind_transfer);

#ifdef TEST_FRAMEWORK
		test_suite_sub = stasis_subscribe(ast_test_suite_topic(), test_suite_event_cb, NULL);
#endif

		ast_cli_register_multiple(cli_manager, ARRAY_LEN(cli_manager));
		__ast_custom_function_register(&managerclient_function, NULL);
		ast_extension_state_add(NULL, NULL, manager_state_cb, NULL);

		/* Append placeholder event so master_eventq never runs dry */
		if (append_event("Event: Placeholder\r\n\r\n", 0)) {
			return -1;
		}

#ifdef AST_XML_DOCS
		temp_event_docs = ast_xmldoc_build_documentation("managerEvent");
		if (temp_event_docs) {
			ao2_t_global_obj_replace_unref(event_docs, temp_event_docs, "Toss old event docs");
			ao2_t_ref(temp_event_docs, -1, "Remove creation ref - container holds only ref now");
		}
#endif

		/* If you have a NULL hash fn, you only need a single bucket */
		sessions = ao2_container_alloc(1, NULL, mansession_cmp_fn);
		if (!sessions) {
			return -1;
		}
		ao2_global_obj_replace_unref(mgr_sessions, sessions);
		ao2_ref(sessions, -1);

		/* Initialize all settings before first configuration load. */
		manager_set_defaults();
	}

	cfg = ast_config_load2("manager.conf", "manager", config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_NOTICE, "Unable to open AMI configuration manager.conf, or configuration is invalid.\n");
		return 0;
	}

	/* If this wasn't performed due to a forced reload (because those can be created by ACL change events, we need to unsubscribe to ACL change events. */
	if (!by_external_config) {
		acl_change_stasis_unsubscribe();
	}

	if (reload) {
		/* Reset all settings before reloading configuration */
		tls_was_enabled = ami_tls_cfg.enabled;
		manager_set_defaults();
	}

	ast_sockaddr_parse(&ami_desc_local_address_tmp, "[::]", 0);
	ast_sockaddr_set_port(&ami_desc_local_address_tmp, DEFAULT_MANAGER_PORT);

	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		val = var->value;

		/* read tls config options while preventing unsupported options from being set */
		if (strcasecmp(var->name, "tlscafile")
			&& strcasecmp(var->name, "tlscapath")
			&& strcasecmp(var->name, "tlscadir")
			&& strcasecmp(var->name, "tlsverifyclient")
			&& strcasecmp(var->name, "tlsdontverifyserver")
			&& strcasecmp(var->name, "tlsclientmethod")
			&& strcasecmp(var->name, "sslclientmethod")
			&& !ast_tls_read_conf(&ami_tls_cfg, &amis_desc, var->name, val)) {
			continue;
		}

		if (!strcasecmp(var->name, "enabled")) {
			manager_enabled = ast_true(val);
		} else if (!strcasecmp(var->name, "webenabled")) {
			webmanager_enabled = ast_true(val);
		} else if (!strcasecmp(var->name, "port")) {
			int bindport;
			if (ast_parse_arg(val, PARSE_UINT32|PARSE_IN_RANGE, &bindport, 1024, 65535)) {
				ast_log(LOG_WARNING, "Invalid port number '%s'\n", val);
			}
			ast_sockaddr_set_port(&ami_desc_local_address_tmp, bindport);
		} else if (!strcasecmp(var->name, "bindaddr")) {
			/* remember port if it has already been set */
			int setport = ast_sockaddr_port(&ami_desc_local_address_tmp);

			if (ast_parse_arg(val, PARSE_ADDR|PARSE_PORT_IGNORE, NULL)) {
				ast_log(LOG_WARNING, "Invalid address '%s' specified, default '%s' will be used\n", val,
						ast_sockaddr_stringify_addr(&ami_desc_local_address_tmp));
			} else {
				ast_sockaddr_parse(&ami_desc_local_address_tmp, val, PARSE_PORT_IGNORE);
			}

			if (setport) {
				ast_sockaddr_set_port(&ami_desc_local_address_tmp, setport);
			}

		} else if (!strcasecmp(var->name, "brokeneventsaction")) {
			broken_events_action = ast_true(val);
		} else if (!strcasecmp(var->name, "allowmultiplelogin")) {
			allowmultiplelogin = ast_true(val);
		} else if (!strcasecmp(var->name, "displayconnects")) {
			displayconnects = ast_true(val);
		} else if (!strcasecmp(var->name, "timestampevents")) {
			timestampevents = ast_true(val);
		} else if (!strcasecmp(var->name, "debug")) {
			manager_debug = ast_true(val);
		} else if (!strcasecmp(var->name, "httptimeout")) {
			newhttptimeout = atoi(val);
		} else if (!strcasecmp(var->name, "authtimeout")) {
			int timeout = atoi(var->value);

			if (timeout < 1) {
				ast_log(LOG_WARNING, "Invalid authtimeout value '%s', using default value\n", var->value);
			} else {
				authtimeout = timeout;
			}
		} else if (!strcasecmp(var->name, "authlimit")) {
			int limit = atoi(var->value);

			if (limit < 1) {
				ast_log(LOG_WARNING, "Invalid authlimit value '%s', using default value\n", var->value);
			} else {
				authlimit = limit;
			}
		} else if (!strcasecmp(var->name, "channelvars")) {
			load_channelvars(var);
		} else {
			ast_log(LOG_NOTICE, "Invalid keyword <%s> = <%s> in manager.conf [general]\n",
				var->name, val);
		}
	}

	if (manager_enabled && !subscribed) {
		if (subscribe_all() != 0) {
			ast_log(LOG_ERROR, "Manager subscription error\n");
			return -1;
		}
	}

	ast_sockaddr_copy(&amis_desc_local_address_tmp, &amis_desc.local_address);

	/* if the amis address has not been set, default is the same as non secure ami */
	if (ast_sockaddr_isnull(&amis_desc_local_address_tmp)) {
		ast_sockaddr_copy(&amis_desc_local_address_tmp, &ami_desc_local_address_tmp);
	}

	/* if the amis address was not set, it will have non-secure ami port set; if
	   amis address was set, we need to check that a port was set or not, if not
	   use the default tls port */
	if (ast_sockaddr_port(&amis_desc_local_address_tmp) == 0 ||
			(ast_sockaddr_port(&ami_desc_local_address_tmp) == ast_sockaddr_port(&amis_desc_local_address_tmp))) {

		ast_sockaddr_set_port(&amis_desc_local_address_tmp, DEFAULT_MANAGER_TLS_PORT);
	}

	if (manager_enabled) {
		ast_sockaddr_copy(&ami_desc.local_address, &ami_desc_local_address_tmp);
		ast_sockaddr_copy(&amis_desc.local_address, &amis_desc_local_address_tmp);
	}

	AST_RWLIST_WRLOCK(&users);

	/* First, get users from users.conf */
	ucfg = ast_config_load2("users.conf", "manager", config_flags);
	if (ucfg && (ucfg != CONFIG_STATUS_FILEUNCHANGED) && ucfg != CONFIG_STATUS_FILEINVALID) {
		const char *hasmanager;
		int genhasmanager = ast_true(ast_variable_retrieve(ucfg, "general", "hasmanager"));

		while ((cat = ast_category_browse(ucfg, cat))) {
			if (!strcasecmp(cat, "general")) {
				continue;
			}

			hasmanager = ast_variable_retrieve(ucfg, cat, "hasmanager");
			if ((!hasmanager && genhasmanager) || ast_true(hasmanager)) {
				const char *user_secret = ast_variable_retrieve(ucfg, cat, "secret");
				const char *user_read = ast_variable_retrieve(ucfg, cat, "read");
				const char *user_write = ast_variable_retrieve(ucfg, cat, "write");
				const char *user_displayconnects = ast_variable_retrieve(ucfg, cat, "displayconnects");
				const char *user_allowmultiplelogin = ast_variable_retrieve(ucfg, cat, "allowmultiplelogin");
				const char *user_writetimeout = ast_variable_retrieve(ucfg, cat, "writetimeout");

				/* Look for an existing entry,
				 * if none found - create one and add it to the list
				 */
				if (!(user = get_manager_by_name_locked(cat))) {
					if (!(user = ast_calloc(1, sizeof(*user)))) {
						break;
					}

					/* Copy name over */
					ast_copy_string(user->username, cat, sizeof(user->username));
					/* Insert into list */
					AST_LIST_INSERT_TAIL(&users, user, list);
					user->acl = NULL;
					user->keep = 1;
					user->readperm = -1;
					user->writeperm = -1;
					/* Default displayconnect from [general] */
					user->displayconnects = displayconnects;
					/* Default allowmultiplelogin from [general] */
					user->allowmultiplelogin = allowmultiplelogin;
					user->writetimeout = 100;
				}

				if (!user_secret) {
					user_secret = ast_variable_retrieve(ucfg, "general", "secret");
				}
				if (!user_read) {
					user_read = ast_variable_retrieve(ucfg, "general", "read");
				}
				if (!user_write) {
					user_write = ast_variable_retrieve(ucfg, "general", "write");
				}
				if (!user_displayconnects) {
					user_displayconnects = ast_variable_retrieve(ucfg, "general", "displayconnects");
				}
				if (!user_allowmultiplelogin) {
					user_allowmultiplelogin = ast_variable_retrieve(ucfg, "general", "allowmultiplelogin");
				}
				if (!user_writetimeout) {
					user_writetimeout = ast_variable_retrieve(ucfg, "general", "writetimeout");
				}

				if (!ast_strlen_zero(user_secret)) {
					ast_free(user->secret);
					user->secret = ast_strdup(user_secret);
				}

				if (user_read) {
					user->readperm = get_perm(user_read);
				}
				if (user_write) {
					user->writeperm = get_perm(user_write);
				}
				if (user_displayconnects) {
					user->displayconnects = ast_true(user_displayconnects);
				}
				if (user_allowmultiplelogin) {
					user->allowmultiplelogin = ast_true(user_allowmultiplelogin);
				}
				if (user_writetimeout) {
					int value = atoi(user_writetimeout);
					if (value < 100) {
						ast_log(LOG_WARNING, "Invalid writetimeout value '%d' in users.conf\n", value);
					} else {
						user->writetimeout = value;
					}
				}
			}
		}
		ast_config_destroy(ucfg);
	}

	/* cat is NULL here in any case */

	while ((cat = ast_category_browse(cfg, cat))) {
		struct ast_acl_list *oldacl;

		if (!strcasecmp(cat, "general")) {
			continue;
		}

		/* Look for an existing entry, if none found - create one and add it to the list */
		if (!(user = get_manager_by_name_locked(cat))) {
			if (!(user = ast_calloc(1, sizeof(*user)))) {
				break;
			}
			/* Copy name over */
			ast_copy_string(user->username, cat, sizeof(user->username));

			user->acl = NULL;
			user->readperm = 0;
			user->writeperm = 0;
			/* Default displayconnect from [general] */
			user->displayconnects = displayconnects;
			/* Default allowmultiplelogin from [general] */
			user->allowmultiplelogin = allowmultiplelogin;
			user->writetimeout = 100;
			user->whitefilters = ao2_container_alloc(1, NULL, NULL);
			user->blackfilters = ao2_container_alloc(1, NULL, NULL);
			if (!user->whitefilters || !user->blackfilters) {
				manager_free_user(user);
				break;
			}

			/* Insert into list */
			AST_RWLIST_INSERT_TAIL(&users, user, list);
		} else {
			ao2_t_callback(user->whitefilters, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "unlink all white filters");
			ao2_t_callback(user->blackfilters, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "unlink all black filters");
		}

		/* Make sure we keep this user and don't destroy it during cleanup */
		user->keep = 1;
		oldacl = user->acl;
		user->acl = NULL;
		ast_variables_destroy(user->chanvars);

		var = ast_variable_browse(cfg, cat);
		for (; var; var = var->next) {
			if (!strcasecmp(var->name, "secret")) {
				ast_free(user->secret);
				user->secret = ast_strdup(var->value);
			} else if (!strcasecmp(var->name, "deny") ||
				       !strcasecmp(var->name, "permit") ||
				       !strcasecmp(var->name, "acl")) {
				ast_append_acl(var->name, var->value, &user->acl, NULL, &acl_subscription_flag);
			}  else if (!strcasecmp(var->name, "read") ) {
				user->readperm = get_perm(var->value);
			}  else if (!strcasecmp(var->name, "write") ) {
				user->writeperm = get_perm(var->value);
			}  else if (!strcasecmp(var->name, "displayconnects") ) {
				user->displayconnects = ast_true(var->value);
			}  else if (!strcasecmp(var->name, "allowmultiplelogin") ) {
				user->allowmultiplelogin = ast_true(var->value);
			} else if (!strcasecmp(var->name, "writetimeout")) {
				int value = atoi(var->value);
				if (value < 100) {
					ast_log(LOG_WARNING, "Invalid writetimeout value '%s' at line %d\n", var->value, var->lineno);
				} else {
					user->writetimeout = value;
				}
			} else if (!strcasecmp(var->name, "setvar")) {
				struct ast_variable *tmpvar;
				char varbuf[256];
				char *varval;
				char *varname;

				ast_copy_string(varbuf, var->value, sizeof(varbuf));
				varname = varbuf;

				if ((varval = strchr(varname,'='))) {
					*varval++ = '\0';
					if ((tmpvar = ast_variable_new(varname, varval, ""))) {
						tmpvar->next = user->chanvars;
						user->chanvars = tmpvar;
					}
				}
			} else if (!strcasecmp(var->name, "eventfilter")) {
				const char *value = var->value;
				manager_add_filter(value, user->whitefilters, user->blackfilters);
			} else {
				ast_debug(1, "%s is an unknown option.\n", var->name);
			}
		}

		oldacl = ast_free_acl_list(oldacl);
	}
	ast_config_destroy(cfg);

	/* Check the flag for named ACL event subscription and if we need to, register a subscription. */
	if (acl_subscription_flag && !by_external_config) {
		acl_change_stasis_subscribe();
	}

	/* Perform cleanup - essentially prune out old users that no longer exist */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&users, user, list) {
		if (user->keep) {	/* valid record. clear flag for the next round */
			user->keep = 0;

			/* Calculate A1 for Digest auth */
			snprintf(a1, sizeof(a1), "%s:%s:%s", user->username, global_realm, user->secret);
			ast_md5_hash(a1_hash,a1);
			ast_free(user->a1_hash);
			user->a1_hash = ast_strdup(a1_hash);
			continue;
		}
		/* We do not need to keep this user so take them out of the list */
		AST_RWLIST_REMOVE_CURRENT(list);
		ast_debug(4, "Pruning user '%s'\n", user->username);
		manager_free_user(user);
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&users);

	if (webmanager_enabled && manager_enabled) {
		if (!webregged) {
			ast_http_uri_link(&rawmanuri);
			ast_http_uri_link(&manageruri);
			ast_http_uri_link(&managerxmluri);

			ast_http_uri_link(&arawmanuri);
			ast_http_uri_link(&amanageruri);
			ast_http_uri_link(&amanagerxmluri);
			webregged = 1;
		}
	} else {
		if (webregged) {
			ast_http_uri_unlink(&rawmanuri);
			ast_http_uri_unlink(&manageruri);
			ast_http_uri_unlink(&managerxmluri);

			ast_http_uri_unlink(&arawmanuri);
			ast_http_uri_unlink(&amanageruri);
			ast_http_uri_unlink(&amanagerxmluri);
			webregged = 0;
		}
	}

	if (newhttptimeout > 0) {
		httptimeout = newhttptimeout;
	}

	ast_tcptls_server_start(&ami_desc);
	if (tls_was_enabled && !ami_tls_cfg.enabled) {
		ast_tcptls_server_stop(&amis_desc);
	} else if (ast_ssl_setup(amis_desc.tls_cfg)) {
		ast_tcptls_server_start(&amis_desc);
	}

	return 0;
}

static void acl_change_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	if (stasis_message_type(message) != ast_named_acl_change_type()) {
		return;
	}

	/* For now, this is going to be performed simply and just execute a forced reload. */
	ast_log(LOG_NOTICE, "Reloading manager in response to ACL change event.\n");
	__init_manager(1, 1);
}

int init_manager(void)
{
	return __init_manager(0, 0);
}

int reload_manager(void)
{
	return __init_manager(1, 0);
}

int astman_datastore_add(struct mansession *s, struct ast_datastore *datastore)
{
	AST_LIST_INSERT_HEAD(&s->session->datastores, datastore, entry);

	return 0;
}

int astman_datastore_remove(struct mansession *s, struct ast_datastore *datastore)
{
	return AST_LIST_REMOVE(&s->session->datastores, datastore, entry) ? 0 : -1;
}

struct ast_datastore *astman_datastore_find(struct mansession *s, const struct ast_datastore_info *info, const char *uid)
{
	struct ast_datastore *datastore = NULL;

	if (info == NULL)
		return NULL;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&s->session->datastores, datastore, entry) {
		if (datastore->info != info) {
			continue;
		}

		if (uid == NULL) {
			/* matched by type only */
			break;
		}

		if ((datastore->uid != NULL) && !strcasecmp(uid, datastore->uid)) {
			/* Matched by type AND uid */
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return datastore;
}

int ast_str_append_event_header(struct ast_str **fields_string,
					const char *header, const char *value)
{
	struct ast_str *working_str = *fields_string;

	if (!working_str) {
		working_str = ast_str_create(128);
		if (!working_str) {
			return -1;
		}
		*fields_string = working_str;
	}

	ast_str_append(&working_str, 0,
		"%s: %s\r\n",
		header, value);

	return 0;
}

static void manager_event_blob_dtor(void *obj)
{
	struct ast_manager_event_blob *ev = obj;
	ast_string_field_free_memory(ev);
}

struct ast_manager_event_blob *
__attribute__((format(printf, 3, 4)))
ast_manager_event_blob_create(
	int event_flags,
	const char *manager_event,
	const char *extra_fields_fmt,
	...)
{
	RAII_VAR(struct ast_manager_event_blob *, ev, NULL, ao2_cleanup);
	va_list argp;

	ast_assert(extra_fields_fmt != NULL);
	ast_assert(manager_event != NULL);

	ev = ao2_alloc(sizeof(*ev), manager_event_blob_dtor);
	if (!ev) {
		return NULL;
	}

	if (ast_string_field_init(ev, 20)) {
		return NULL;
	}

	ev->manager_event = manager_event;
	ev->event_flags = event_flags;

	va_start(argp, extra_fields_fmt);
	ast_string_field_ptr_build_va(ev, &ev->extra_fields, extra_fields_fmt,
				      argp);
	va_end(argp);

	ao2_ref(ev, +1);
	return ev;
}
