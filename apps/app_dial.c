/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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
 * \brief dial() & retrydial() - Trivial application to dial a channel and send an URL on answer
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>chan_local</depend>
 ***/


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include "asterisk/paths.h" /* use ast_config_AST_DATA_DIR */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/config.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/callerid.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/causes.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/cdr.h"
#include "asterisk/manager.h"
#include "asterisk/privacy.h"
#include "asterisk/stringfields.h"
#include "asterisk/global_datastores.h"
#include "asterisk/dsp.h"
#include "asterisk/cel.h"
#include "asterisk/aoc.h"
#include "asterisk/ccss.h"
#include "asterisk/indications.h"

/*** DOCUMENTATION
	<application name="Dial" language="en_US">
		<synopsis>
			Attempt to connect to another device or endpoint and bridge the call.
		</synopsis>
		<syntax>
			<parameter name="Technology/Resource" required="true" argsep="&amp;">
				<argument name="Technology/Resource" required="true">
					<para>Specification of the device(s) to dial.  These must be in the format of
					<literal>Technology/Resource</literal>, where <replaceable>Technology</replaceable>
					represents a particular channel driver, and <replaceable>Resource</replaceable>
					represents a resource available to that particular channel driver.</para>
				</argument>
				<argument name="Technology2/Resource2" required="false" multiple="true">
					<para>Optional extra devices to dial in parallel</para>
					<para>If you need more then one enter them as
					Technology2/Resource2&amp;Technology3/Resourse3&amp;.....</para>
				</argument>
			</parameter>
			<parameter name="timeout" required="false">
				<para>Specifies the number of seconds we attempt to dial the specified devices</para>
				<para>If not specified, this defaults to 136 years.</para>
			</parameter>
			<parameter name="options" required="false">
			   <optionlist>
				<option name="A">
					<argument name="x" required="true">
						<para>The file to play to the called party</para>
					</argument>
					<para>Play an announcement to the called party, where <replaceable>x</replaceable> is the prompt to be played</para>
				</option>
				<option name="a">
					<para>Immediately answer the calling channel when the called channel answers in
					all cases. Normally, the calling channel is answered when the called channel
					answers, but when options such as A() and M() are used, the calling channel is
					not answered until all actions on the called channel (such as playing an
					announcement) are completed.  This option can be used to answer the calling
					channel before doing anything on the called channel. You will rarely need to use
					this option, the default behavior is adequate in most cases.</para>
				</option>
				<option name="C">
					<para>Reset the call detail record (CDR) for this call.</para>
				</option>
				<option name="c">
					<para>If the Dial() application cancels this call, always set the flag to tell the channel
					driver that the call is answered elsewhere.</para>
				</option>
				<option name="d">
					<para>Allow the calling user to dial a 1 digit extension while waiting for
					a call to be answered. Exit to that extension if it exists in the
					current context, or the context defined in the <variable>EXITCONTEXT</variable> variable,
					if it exists.</para>
				</option>
				<option name="D" argsep=":">
					<argument name="called" />
					<argument name="calling" />
					<argument name="progress" />
					<para>Send the specified DTMF strings <emphasis>after</emphasis> the called
					party has answered, but before the call gets bridged. The 
					<replaceable>called</replaceable> DTMF string is sent to the called party, and the 
					<replaceable>calling</replaceable> DTMF string is sent to the calling party. Both arguments 
					can be used alone.  If <replaceable>progress</replaceable> is specified, its DTMF is sent
					immediately after receiving a PROGRESS message.</para>
				</option>
				<option name="e">
					<para>Execute the <literal>h</literal> extension for peer after the call ends</para>
				</option>
				<option name="f">
					<argument name="x" required="false" />
					<para>If <replaceable>x</replaceable> is not provided, force the CallerID sent on a call-forward or
					deflection to the dialplan extension of this Dial() using a dialplan <literal>hint</literal>.
					For example, some PSTNs do not allow CallerID to be set to anything
					other than the numbers assigned to you.
					If <replaceable>x</replaceable> is provided, force the CallerID sent to <replaceable>x</replaceable>.</para>
				</option>
				<option name="F" argsep="^">
					<argument name="context" required="false" />
					<argument name="exten" required="false" />
					<argument name="priority" required="true" />
					<para>When the caller hangs up, transfer the called party
					to the specified destination and continue execution at that location.</para>
				</option>
				<option name="F">
					<para>Proceed with dialplan execution at the next priority in the current extension if the
					source channel hangs up.</para>
				</option>
				<option name="g">
					<para>Proceed with dialplan execution at the next priority in the current extension if the
					destination channel hangs up.</para>
				</option>
				<option name="G" argsep="^">
					<argument name="context" required="false" />
					<argument name="exten" required="false" />
					<argument name="priority" required="true" />
					<para>If the call is answered, transfer the calling party to
					the specified <replaceable>priority</replaceable> and the called party to the specified 
					<replaceable>priority</replaceable> plus one.</para>
					<note>
						<para>You cannot use any additional action post answer options in conjunction with this option.</para>
					</note>
				</option>
				<option name="h">
					<para>Allow the called party to hang up by sending the <literal>*</literal> DTMF digit.</para>
				</option>
				<option name="H">
					<para>Allow the calling party to hang up by hitting the <literal>*</literal> DTMF digit.</para>
				</option>
				<option name="i">
					<para>Asterisk will ignore any forwarding requests it may receive on this dial attempt.</para>
				</option>
				<option name="I">
					<para>Asterisk will ignore any connected line update requests or redirecting party update
					requests it may receiveon this dial attempt.</para>
				</option>
				<option name="k">
					<para>Allow the called party to enable parking of the call by sending
					the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
				</option>
				<option name="K">
					<para>Allow the calling party to enable parking of the call by sending
					the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
				</option>
				<option name="L" argsep=":">
					<argument name="x" required="true">
						<para>Maximum call time, in milliseconds</para>
					</argument>
					<argument name="y">
						<para>Warning time, in milliseconds</para>
					</argument>
					<argument name="z">
						<para>Repeat time, in milliseconds</para>
					</argument>
					<para>Limit the call to <replaceable>x</replaceable> milliseconds. Play a warning when <replaceable>y</replaceable> milliseconds are
					left. Repeat the warning every <replaceable>z</replaceable> milliseconds until time expires.</para>
					<para>This option is affected by the following variables:</para>
					<variablelist>
						<variable name="LIMIT_PLAYAUDIO_CALLER">
							<value name="yes" default="true" />
							<value name="no" />
							<para>If set, this variable causes Asterisk to play the prompts to the caller.</para>
						</variable>
						<variable name="LIMIT_PLAYAUDIO_CALLEE">
							<value name="yes" />
							<value name="no" default="true"/>
							<para>If set, this variable causes Asterisk to play the prompts to the callee.</para>
						</variable>
						<variable name="LIMIT_TIMEOUT_FILE">
							<value name="filename"/>
							<para>If specified, <replaceable>filename</replaceable> specifies the sound prompt to play when the timeout is reached.
							If not set, the time remaining will be announced.</para>
						</variable>
						<variable name="LIMIT_CONNECT_FILE">
							<value name="filename"/>
							<para>If specified, <replaceable>filename</replaceable> specifies the sound prompt to play when the call begins.
							If not set, the time remaining will be announced.</para>
						</variable>
						<variable name="LIMIT_WARNING_FILE">
							<value name="filename"/>
							<para>If specified, <replaceable>filename</replaceable> specifies the sound prompt to play as
							a warning when time <replaceable>x</replaceable> is reached. If not set, the time remaining will be announced.</para>
						</variable>
					</variablelist>
				</option>
				<option name="m">
					<argument name="class" required="false"/>
					<para>Provide hold music to the calling party until a requested
					channel answers. A specific music on hold <replaceable>class</replaceable>
					(as defined in <filename>musiconhold.conf</filename>) can be specified.</para>
				</option>
				<option name="M" argsep="^">
					<argument name="macro" required="true">
						<para>Name of the macro that should be executed.</para>
					</argument>
					<argument name="arg" multiple="true">
						<para>Macro arguments</para>
					</argument>
					<para>Execute the specified <replaceable>macro</replaceable> for the <emphasis>called</emphasis> channel 
					before connecting to the calling channel. Arguments can be specified to the Macro
					using <literal>^</literal> as a delimiter. The macro can set the variable
					<variable>MACRO_RESULT</variable> to specify the following actions after the macro is
					finished executing:</para>
					<variablelist>
						<variable name="MACRO_RESULT">
							<para>If set, this action will be taken after the macro finished executing.</para>
							<value name="ABORT">
								Hangup both legs of the call
							</value>
							<value name="CONGESTION">
								Behave as if line congestion was encountered
							</value>
							<value name="BUSY">
								Behave as if a busy signal was encountered
							</value>
							<value name="CONTINUE">
								Hangup the called party and allow the calling party to continue dialplan execution at the next priority
							</value>
							<!-- TODO: Fix this syntax up, once we've figured out how to specify the GOTO syntax -->
							<value name="GOTO:&lt;context&gt;^&lt;exten&gt;^&lt;priority&gt;">
								Transfer the call to the specified destination.
							</value>
						</variable>
					</variablelist>
					<note>
						<para>You cannot use any additional action post answer options in conjunction
						with this option. Also, pbx services are not run on the peer (called) channel,
						so you will not be able to set timeouts via the TIMEOUT() function in this macro.</para>
					</note>
					<warning><para>Be aware of the limitations that macros have, specifically with regards to use of
					the <literal>WaitExten</literal> application. For more information, see the documentation for
					Macro()</para></warning>
				</option>
				<option name="n">
				        <argument name="delete">
					        <para>With <replaceable>delete</replaceable> either not specified or set to <literal>0</literal>,
						the recorded introduction will not be deleted if the caller hangs up while the remote party has not
						yet answered.</para>
						<para>With <replaceable>delete</replaceable> set to <literal>1</literal>, the introduction will
						always be deleted.</para>
					</argument>
					<para>This option is a modifier for the call screening/privacy mode. (See the 
					<literal>p</literal> and <literal>P</literal> options.) It specifies
					that no introductions are to be saved in the <directory>priv-callerintros</directory>
					directory.</para>
				</option>
				<option name="N">
					<para>This option is a modifier for the call screening/privacy mode. It specifies
					that if Caller*ID is present, do not screen the call.</para>
				</option>
				<option name="o">
					<argument name="x" required="false" />
					<para>If <replaceable>x</replaceable> is not provided, specify that the CallerID that was present on the
					<emphasis>calling</emphasis> channel be stored as the CallerID on the <emphasis>called</emphasis> channel.
					This was the behavior of Asterisk 1.0 and earlier.
					If <replaceable>x</replaceable> is provided, specify the CallerID stored on the <emphasis>called</emphasis> channel.
					Note that o(${CALLERID(all)}) is similar to option o without the parameter.</para>
				</option>
				<option name="O">
					<argument name="mode">
						<para>With <replaceable>mode</replaceable> either not specified or set to <literal>1</literal>,
						the originator hanging up will cause the phone to ring back immediately.</para>
						<para>With <replaceable>mode</replaceable> set to <literal>2</literal>, when the operator 
						flashes the trunk, it will ring their phone back.</para>
					</argument>
					<para>Enables <emphasis>operator services</emphasis> mode.  This option only
					works when bridging a DAHDI channel to another DAHDI channel
					only. if specified on non-DAHDI interfaces, it will be ignored.
					When the destination answers (presumably an operator services
					station), the originator no longer has control of their line.
					They may hang up, but the switch will not release their line
					until the destination party (the operator) hangs up.</para>
				</option>
				<option name="p">
					<para>This option enables screening mode. This is basically Privacy mode
					without memory.</para>
				</option>
				<option name="P">
					<argument name="x" />
					<para>Enable privacy mode. Use <replaceable>x</replaceable> as the family/key in the AstDB database if
					it is provided. The current extension is used if a database family/key is not specified.</para>
				</option>
				<option name="r">
					<para>Default: Indicate ringing to the calling party, even if the called party isn't actually ringing. Pass no audio to the calling
					party until the called channel has answered.</para>
					<argument name="tone" required="false">
						<para>Indicate progress to calling party. Send audio 'tone' from indications.conf</para>
					</argument>
				</option>
				<option name="S">
					<argument name="x" required="true" />
					<para>Hang up the call <replaceable>x</replaceable> seconds <emphasis>after</emphasis> the called party has
					answered the call.</para>
				</option>
				<option name="s">
					<argument name="x" required="true" />
					<para>Force the outgoing callerid tag parameter to be set to the string <replaceable>x</replaceable>.</para>
					<para>Works with the f option.</para>
				</option>
				<option name="t">
					<para>Allow the called party to transfer the calling party by sending the
					DTMF sequence defined in <filename>features.conf</filename>. This setting does not perform policy enforcement on
					transfers initiated by other methods.</para>
				</option>
				<option name="T">
					<para>Allow the calling party to transfer the called party by sending the
					DTMF sequence defined in <filename>features.conf</filename>. This setting does not perform policy enforcement on
					transfers initiated by other methods.</para>
				</option>
				<option name="U" argsep="^">
					<argument name="x" required="true">
						<para>Name of the subroutine to execute via Gosub</para>
					</argument>
					<argument name="arg" multiple="true" required="false">
						<para>Arguments for the Gosub routine</para>
					</argument>
					<para>Execute via Gosub the routine <replaceable>x</replaceable> for the <emphasis>called</emphasis> channel before connecting
					to the calling channel. Arguments can be specified to the Gosub
					using <literal>^</literal> as a delimiter. The Gosub routine can set the variable
					<variable>GOSUB_RESULT</variable> to specify the following actions after the Gosub returns.</para>
					<variablelist>
						<variable name="GOSUB_RESULT">
							<value name="ABORT">
								Hangup both legs of the call.
							</value>
							<value name="CONGESTION">
								Behave as if line congestion was encountered.
							</value>
							<value name="BUSY">
								Behave as if a busy signal was encountered.
							</value>
							<value name="CONTINUE">
								Hangup the called party and allow the calling party
								to continue dialplan execution at the next priority.
							</value>
							<!-- TODO: Fix this syntax up, once we've figured out how to specify the GOTO syntax -->
							<value name="GOTO:&lt;context&gt;^&lt;exten&gt;^&lt;priority&gt;">
								Transfer the call to the specified priority. Optionally, an extension, or
								extension and priority can be specified.
							</value>
						</variable>
					</variablelist>
					<note>
						<para>You cannot use any additional action post answer options in conjunction
						with this option. Also, pbx services are not run on the peer (called) channel,
						so you will not be able to set timeouts via the TIMEOUT() function in this routine.</para>
					</note>
				</option>
				<option name="u">
					<argument name = "x" required="true">
						<para>Force the outgoing callerid presentation indicator parameter to be set
						to one of the values passed in <replaceable>x</replaceable>:
						<literal>allowed_not_screened</literal>
						<literal>allowed_passed_screen</literal>
						<literal>allowed_failed_screen</literal>
						<literal>allowed</literal>
						<literal>prohib_not_screened</literal>
						<literal>prohib_passed_screen</literal>
						<literal>prohib_failed_screen</literal>
						<literal>prohib</literal>
						<literal>unavailable</literal></para>
					</argument>
					<para>Works with the f option.</para>
				</option>
				<option name="w">
					<para>Allow the called party to enable recording of the call by sending
					the DTMF sequence defined for one-touch recording in <filename>features.conf</filename>.</para>
				</option>
				<option name="W">
					<para>Allow the calling party to enable recording of the call by sending
					the DTMF sequence defined for one-touch recording in <filename>features.conf</filename>.</para>
				</option>
				<option name="x">
					<para>Allow the called party to enable recording of the call by sending
					the DTMF sequence defined for one-touch automixmonitor in <filename>features.conf</filename>.</para>
				</option>
				<option name="X">
					<para>Allow the calling party to enable recording of the call by sending
					the DTMF sequence defined for one-touch automixmonitor in <filename>features.conf</filename>.</para>
				</option>
				<option name="z">
					<para>On a call forward, cancel any dial timeout which has been set for this call.</para>
				</option>
				</optionlist>
			</parameter>
			<parameter name="URL">
				<para>The optional URL will be sent to the called party if the channel driver supports it.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will place calls to one or more specified channels. As soon
			as one of the requested channels answers, the originating channel will be
			answered, if it has not already been answered. These two channels will then
			be active in a bridged call. All other channels that were requested will then
			be hung up.</para>

			<para>Unless there is a timeout specified, the Dial application will wait
			indefinitely until one of the called channels answers, the user hangs up, or
			if all of the called channels are busy or unavailable. Dialplan executing will
			continue if no requested channels can be called, or if the timeout expires.
			This application will report normal termination if the originating channel
			hangs up, or if the call is bridged and either of the parties in the bridge
			ends the call.</para>
			<para>If the <variable>OUTBOUND_GROUP</variable> variable is set, all peer channels created by this
			application will be put into that group (as in Set(GROUP()=...).
			If the <variable>OUTBOUND_GROUP_ONCE</variable> variable is set, all peer channels created by this
			application will be put into that group (as in Set(GROUP()=...). Unlike OUTBOUND_GROUP,
			however, the variable will be unset after use.</para>

			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="DIALEDTIME">
					<para>This is the time from dialing a channel until when it is disconnected.</para>
				</variable>
				<variable name="ANSWEREDTIME">
					<para>This is the amount of time for actual call.</para>
				</variable>
				<variable name="DIALSTATUS">
					<para>This is the status of the call</para>
					<value name="CHANUNAVAIL" />
					<value name="CONGESTION" />
					<value name="NOANSWER" />
					<value name="BUSY" />
					<value name="ANSWER" />
					<value name="CANCEL" />
					<value name="DONTCALL">
						For the Privacy and Screening Modes.
						Will be set if the called party chooses to send the calling party to the 'Go Away' script.
					</value>
					<value name="TORTURE">
						For the Privacy and Screening Modes.
						Will be set if the called party chooses to send the calling party to the 'torture' script.
					</value>
					<value name="INVALIDARGS" />
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="RetryDial" language="en_US">
		<synopsis>
			Place a call, retrying on failure allowing an optional exit extension.
		</synopsis>
		<syntax>
			<parameter name="announce" required="true">
				<para>Filename of sound that will be played when no channel can be reached</para>
			</parameter>
			<parameter name="sleep" required="true">
				<para>Number of seconds to wait after a dial attempt failed before a new attempt is made</para>
			</parameter>
			<parameter name="retries" required="true">
				<para>Number of retries</para>
				<para>When this is reached flow will continue at the next priority in the dialplan</para>
			</parameter>
			<parameter name="dialargs" required="true">
				<para>Same format as arguments provided to the Dial application</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will attempt to place a call using the normal Dial application.
			If no channel can be reached, the <replaceable>announce</replaceable> file will be played.
			Then, it will wait <replaceable>sleep</replaceable> number of seconds before retrying the call.
			After <replaceable>retries</replaceable> number of attempts, the calling channel will continue at the next priority in the dialplan.
			If the <replaceable>retries</replaceable> setting is set to 0, this application will retry endlessly.
			While waiting to retry a call, a 1 digit extension may be dialed. If that
			extension exists in either the context defined in <variable>EXITCONTEXT</variable> or the current
			one, The call will jump to that extension immediately.
			The <replaceable>dialargs</replaceable> are specified in the same format that arguments are provided
			to the Dial application.</para>
		</description>
	</application>
 ***/

static const char app[] = "Dial";
static const char rapp[] = "RetryDial";

enum {
	OPT_ANNOUNCE =          (1 << 0),
	OPT_RESETCDR =          (1 << 1),
	OPT_DTMF_EXIT =         (1 << 2),
	OPT_SENDDTMF =          (1 << 3),
	OPT_FORCECLID =         (1 << 4),
	OPT_GO_ON =             (1 << 5),
	OPT_CALLEE_HANGUP =     (1 << 6),
	OPT_CALLER_HANGUP =     (1 << 7),
	OPT_ORIGINAL_CLID =     (1 << 8),
	OPT_DURATION_LIMIT =    (1 << 9),
	OPT_MUSICBACK =         (1 << 10),
	OPT_CALLEE_MACRO =      (1 << 11),
	OPT_SCREEN_NOINTRO =    (1 << 12),
	OPT_SCREEN_NOCALLERID = (1 << 13),
	OPT_IGNORE_CONNECTEDLINE = (1 << 14),
	OPT_SCREENING =         (1 << 15),
	OPT_PRIVACY =           (1 << 16),
	OPT_RINGBACK =          (1 << 17),
	OPT_DURATION_STOP =     (1 << 18),
	OPT_CALLEE_TRANSFER =   (1 << 19),
	OPT_CALLER_TRANSFER =   (1 << 20),
	OPT_CALLEE_MONITOR =    (1 << 21),
	OPT_CALLER_MONITOR =    (1 << 22),
	OPT_GOTO =              (1 << 23),
	OPT_OPERMODE =          (1 << 24),
	OPT_CALLEE_PARK =       (1 << 25),
	OPT_CALLER_PARK =       (1 << 26),
	OPT_IGNORE_FORWARDING = (1 << 27),
	OPT_CALLEE_GOSUB =      (1 << 28),
	OPT_CALLEE_MIXMONITOR = (1 << 29),
	OPT_CALLER_MIXMONITOR = (1 << 30),
	OPT_CALLER_ANSWER =	(1 << 31),
};

#define DIAL_STILLGOING      (1 << 31)
#define DIAL_NOFORWARDHTML   ((uint64_t)1 << 32) /* flags are now 64 bits, so keep it up! */
#define DIAL_CALLERID_ABSENT ((uint64_t)1 << 33) /* TRUE if caller id is not available for connected line. */
#define OPT_CANCEL_ELSEWHERE ((uint64_t)1 << 34)
#define OPT_PEER_H           ((uint64_t)1 << 35)
#define OPT_CALLEE_GO_ON     ((uint64_t)1 << 36)
#define OPT_CANCEL_TIMEOUT   ((uint64_t)1 << 37)
#define OPT_FORCE_CID_TAG    ((uint64_t)1 << 38)
#define OPT_FORCE_CID_PRES   ((uint64_t)1 << 39)

enum {
	OPT_ARG_ANNOUNCE = 0,
	OPT_ARG_SENDDTMF,
	OPT_ARG_GOTO,
	OPT_ARG_DURATION_LIMIT,
	OPT_ARG_MUSICBACK,
	OPT_ARG_CALLEE_MACRO,
	OPT_ARG_RINGBACK,
	OPT_ARG_CALLEE_GOSUB,
	OPT_ARG_CALLEE_GO_ON,
	OPT_ARG_PRIVACY,
	OPT_ARG_DURATION_STOP,
	OPT_ARG_OPERMODE,
	OPT_ARG_SCREEN_NOINTRO,
	OPT_ARG_ORIGINAL_CLID,
	OPT_ARG_FORCECLID,
	OPT_ARG_FORCE_CID_TAG,
	OPT_ARG_FORCE_CID_PRES,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(dial_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('A', OPT_ANNOUNCE, OPT_ARG_ANNOUNCE),
	AST_APP_OPTION('a', OPT_CALLER_ANSWER),
	AST_APP_OPTION('C', OPT_RESETCDR),
	AST_APP_OPTION('c', OPT_CANCEL_ELSEWHERE),
	AST_APP_OPTION('d', OPT_DTMF_EXIT),
	AST_APP_OPTION_ARG('D', OPT_SENDDTMF, OPT_ARG_SENDDTMF),
	AST_APP_OPTION('e', OPT_PEER_H),
	AST_APP_OPTION_ARG('f', OPT_FORCECLID, OPT_ARG_FORCECLID),
	AST_APP_OPTION_ARG('F', OPT_CALLEE_GO_ON, OPT_ARG_CALLEE_GO_ON),
	AST_APP_OPTION('g', OPT_GO_ON),
	AST_APP_OPTION_ARG('G', OPT_GOTO, OPT_ARG_GOTO),
	AST_APP_OPTION('h', OPT_CALLEE_HANGUP),
	AST_APP_OPTION('H', OPT_CALLER_HANGUP),
	AST_APP_OPTION('i', OPT_IGNORE_FORWARDING),
	AST_APP_OPTION('I', OPT_IGNORE_CONNECTEDLINE),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION_ARG('L', OPT_DURATION_LIMIT, OPT_ARG_DURATION_LIMIT),
	AST_APP_OPTION_ARG('m', OPT_MUSICBACK, OPT_ARG_MUSICBACK),
	AST_APP_OPTION_ARG('M', OPT_CALLEE_MACRO, OPT_ARG_CALLEE_MACRO),
	AST_APP_OPTION_ARG('n', OPT_SCREEN_NOINTRO, OPT_ARG_SCREEN_NOINTRO),
	AST_APP_OPTION('N', OPT_SCREEN_NOCALLERID),
	AST_APP_OPTION_ARG('o', OPT_ORIGINAL_CLID, OPT_ARG_ORIGINAL_CLID),
	AST_APP_OPTION_ARG('O', OPT_OPERMODE, OPT_ARG_OPERMODE),
	AST_APP_OPTION('p', OPT_SCREENING),
	AST_APP_OPTION_ARG('P', OPT_PRIVACY, OPT_ARG_PRIVACY),
	AST_APP_OPTION_ARG('r', OPT_RINGBACK, OPT_ARG_RINGBACK),
	AST_APP_OPTION_ARG('S', OPT_DURATION_STOP, OPT_ARG_DURATION_STOP),
	AST_APP_OPTION_ARG('s', OPT_FORCE_CID_TAG, OPT_ARG_FORCE_CID_TAG),
	AST_APP_OPTION_ARG('u', OPT_FORCE_CID_PRES, OPT_ARG_FORCE_CID_PRES),
	AST_APP_OPTION('t', OPT_CALLEE_TRANSFER),
	AST_APP_OPTION('T', OPT_CALLER_TRANSFER),
	AST_APP_OPTION_ARG('U', OPT_CALLEE_GOSUB, OPT_ARG_CALLEE_GOSUB),
	AST_APP_OPTION('w', OPT_CALLEE_MONITOR),
	AST_APP_OPTION('W', OPT_CALLER_MONITOR),
	AST_APP_OPTION('x', OPT_CALLEE_MIXMONITOR),
	AST_APP_OPTION('X', OPT_CALLER_MIXMONITOR),
	AST_APP_OPTION('z', OPT_CANCEL_TIMEOUT),
END_OPTIONS );

#define CAN_EARLY_BRIDGE(flags,chan,peer) (!ast_test_flag64(flags, OPT_CALLEE_HANGUP | \
	OPT_CALLER_HANGUP | OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER | \
	OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR | OPT_CALLEE_PARK |  \
	OPT_CALLER_PARK | OPT_ANNOUNCE | OPT_CALLEE_MACRO | OPT_CALLEE_GOSUB) && \
	!chan->audiohooks && !peer->audiohooks)

/*
 * The list of active channels
 */
struct chanlist {
	struct chanlist *next;
	struct ast_channel *chan;
	uint64_t flags;
	/*! Saved connected party info from an AST_CONTROL_CONNECTED_LINE. */
	struct ast_party_connected_line connected;
	/*! TRUE if an AST_CONTROL_CONNECTED_LINE update was saved to the connected element. */
	unsigned int pending_connected_update:1;
	struct ast_aoc_decoded *aoc_s_rate_list;
};

static int detect_disconnect(struct ast_channel *chan, char code, struct ast_str *featurecode);

static void chanlist_free(struct chanlist *outgoing)
{
	ast_party_connected_line_free(&outgoing->connected);
	ast_aoc_destroy_decoded(outgoing->aoc_s_rate_list);
	ast_free(outgoing);
}

static void hanguptree(struct chanlist *outgoing, struct ast_channel *exception, int answered_elsewhere)
{
	/* Hang up a tree of stuff */
	struct chanlist *oo;
	while (outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception)) {
			if (answered_elsewhere) {
				/* The flag is used for local channel inheritance and stuff */
				ast_set_flag(outgoing->chan, AST_FLAG_ANSWERED_ELSEWHERE);
				/* This is for the channel drivers */
				outgoing->chan->hangupcause = AST_CAUSE_ANSWERED_ELSEWHERE;
			}
			ast_hangup(outgoing->chan);
		}
		oo = outgoing;
		outgoing = outgoing->next;
		chanlist_free(oo);
	}
}

#define AST_MAX_WATCHERS 256

/*
 * argument to handle_cause() and other functions.
 */
struct cause_args {
	struct ast_channel *chan;
	int busy;
	int congestion;
	int nochan;
};

static void handle_cause(int cause, struct cause_args *num)
{
	struct ast_cdr *cdr = num->chan->cdr;

	switch(cause) {
	case AST_CAUSE_BUSY:
		if (cdr)
			ast_cdr_busy(cdr);
		num->busy++;
		break;

	case AST_CAUSE_CONGESTION:
		if (cdr)
			ast_cdr_failed(cdr);
		num->congestion++;
		break;

	case AST_CAUSE_NO_ROUTE_DESTINATION:
	case AST_CAUSE_UNREGISTERED:
		if (cdr)
			ast_cdr_failed(cdr);
		num->nochan++;
		break;

	case AST_CAUSE_NO_ANSWER:
		if (cdr) {
			ast_cdr_noanswer(cdr);
		}
		break;
	case AST_CAUSE_NORMAL_CLEARING:
		break;

	default:
		num->nochan++;
		break;
	}
}

static int onedigit_goto(struct ast_channel *chan, const char *context, char exten, int pri)
{
	char rexten[2] = { exten, '\0' };

	if (context) {
		if (!ast_goto_if_exists(chan, context, rexten, pri))
			return 1;
	} else {
		if (!ast_goto_if_exists(chan, chan->context, rexten, pri))
			return 1;
		else if (!ast_strlen_zero(chan->macrocontext)) {
			if (!ast_goto_if_exists(chan, chan->macrocontext, rexten, pri))
				return 1;
		}
	}
	return 0;
}

/* do not call with chan lock held */
static const char *get_cid_name(char *name, int namelen, struct ast_channel *chan)
{
	const char *context;
	const char *exten;

	ast_channel_lock(chan);
	context = ast_strdupa(S_OR(chan->macrocontext, chan->context));
	exten = ast_strdupa(S_OR(chan->macroexten, chan->exten));
	ast_channel_unlock(chan);

	return ast_get_hint(NULL, 0, name, namelen, chan, context, exten) ? name : "";
}

static void senddialevent(struct ast_channel *src, struct ast_channel *dst, const char *dialstring)
{
	struct ast_channel *chans[] = { src, dst };
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Dial", 2, chans,
		"SubEvent: Begin\r\n"
		"Channel: %s\r\n"
		"Destination: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"UniqueID: %s\r\n"
		"DestUniqueID: %s\r\n"
		"Dialstring: %s\r\n",
		src->name, dst->name,
		S_COR(src->caller.id.number.valid, src->caller.id.number.str, "<unknown>"),
		S_COR(src->caller.id.name.valid, src->caller.id.name.str, "<unknown>"),
		src->uniqueid, dst->uniqueid,
		dialstring ? dialstring : "");
}

static void senddialendevent(struct ast_channel *src, const char *dialstatus)
{
	ast_manager_event(src, EVENT_FLAG_CALL, "Dial",
		"SubEvent: End\r\n"
		"Channel: %s\r\n"
		"UniqueID: %s\r\n"
		"DialStatus: %s\r\n",
		src->name, src->uniqueid, dialstatus);
}

/*!
 * helper function for wait_for_answer()
 *
 * XXX this code is highly suspicious, as it essentially overwrites
 * the outgoing channel without properly deleting it.
 *
 * \todo eventually this function should be intergrated into and replaced by ast_call_forward() 
 */
static void do_forward(struct chanlist *o,
	struct cause_args *num, struct ast_flags64 *peerflags, int single, int *to,
	struct ast_party_id *forced_clid, struct ast_party_id *stored_clid)
{
	char tmpchan[256];
	struct ast_channel *original = o->chan;
	struct ast_channel *c = o->chan; /* the winner */
	struct ast_channel *in = num->chan; /* the input channel */
	char *stuff;
	char *tech;
	int cause;
	struct ast_party_caller caller;

	ast_copy_string(tmpchan, c->call_forward, sizeof(tmpchan));
	if ((stuff = strchr(tmpchan, '/'))) {
		*stuff++ = '\0';
		tech = tmpchan;
	} else {
		const char *forward_context;
		ast_channel_lock(c);
		forward_context = pbx_builtin_getvar_helper(c, "FORWARD_CONTEXT");
		if (ast_strlen_zero(forward_context)) {
			forward_context = NULL;
		}
		snprintf(tmpchan, sizeof(tmpchan), "%s@%s", c->call_forward, forward_context ? forward_context : c->context);
		ast_channel_unlock(c);
		stuff = tmpchan;
		tech = "Local";
	}

	ast_cel_report_event(in, AST_CEL_FORWARD, NULL, c->call_forward, NULL);

	/* Before processing channel, go ahead and check for forwarding */
	ast_verb(3, "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, c->name);
	/* If we have been told to ignore forwards, just set this channel to null and continue processing extensions normally */
	if (ast_test_flag64(peerflags, OPT_IGNORE_FORWARDING)) {
		ast_verb(3, "Forwarding %s to '%s/%s' prevented.\n", in->name, tech, stuff);
		c = o->chan = NULL;
		cause = AST_CAUSE_BUSY;
	} else {
		/* Setup parameters */
		c = o->chan = ast_request(tech, in->nativeformats, in, stuff, &cause);
		if (c) {
			if (single)
				ast_channel_make_compatible(o->chan, in);
			ast_channel_inherit_variables(in, o->chan);
			ast_channel_datastore_inherit(in, o->chan);
			/* When a call is forwarded, we don't want to track new interfaces
			 * dialed for CC purposes. Setting the done flag will ensure that
			 * any Dial operations that happen later won't record CC interfaces.
			 */
			ast_ignore_cc(o->chan);
			ast_log(LOG_NOTICE, "Not accepting call completion offers from call-forward recipient %s\n", o->chan->name);
		} else
			ast_log(LOG_NOTICE,
				"Forwarding failed to create channel to dial '%s/%s' (cause = %d)\n",
				tech, stuff, cause);
	}
	if (!c) {
		ast_clear_flag64(o, DIAL_STILLGOING);
		handle_cause(cause, num);
		ast_hangup(original);
	} else {
		struct ast_party_redirecting redirecting;

		if (single && CAN_EARLY_BRIDGE(peerflags, c, in)) {
			ast_rtp_instance_early_bridge_make_compatible(c, in);
		}

		ast_channel_set_redirecting(c, &original->redirecting, NULL);
		ast_channel_lock(c);
		while (ast_channel_trylock(in)) {
			CHANNEL_DEADLOCK_AVOIDANCE(c);
		}
		if (!c->redirecting.from.number.valid
			|| ast_strlen_zero(c->redirecting.from.number.str)) {
			/*
			 * The call was not previously redirected so it is
			 * now redirected from this number.
			 */
			ast_party_number_free(&c->redirecting.from.number);
			ast_party_number_init(&c->redirecting.from.number);
			c->redirecting.from.number.valid = 1;
			c->redirecting.from.number.str =
				ast_strdup(S_OR(in->macroexten, in->exten));
		}

		c->dialed.transit_network_select = in->dialed.transit_network_select;

		/* Determine CallerID to store in outgoing channel. */
		ast_party_caller_set_init(&caller, &c->caller);
		if (ast_test_flag64(peerflags, OPT_ORIGINAL_CLID)) {
			caller.id = *stored_clid;
			ast_channel_set_caller_event(c, &caller, NULL);
		} else if (ast_strlen_zero(S_COR(c->caller.id.number.valid,
			c->caller.id.number.str, NULL))) {
			/*
			 * The new channel has no preset CallerID number by the channel
			 * driver.  Use the dialplan extension and hint name.
			 */
			caller.id = *stored_clid;
			ast_channel_set_caller_event(c, &caller, NULL);
		}

		/* Determine CallerID for outgoing channel to send. */
		if (ast_test_flag64(o, OPT_FORCECLID)) {
			struct ast_party_connected_line connected;

			ast_party_connected_line_init(&connected);
			connected.id = *forced_clid;
			ast_party_connected_line_copy(&c->connected, &connected);
		} else {
			ast_connected_line_copy_from_caller(&c->connected, &in->caller);
		}

		ast_string_field_set(c, accountcode, in->accountcode);

		c->appl = "AppDial";
		c->data = "(Outgoing Line)";
		/*
		 * We must unlock c before calling ast_channel_redirecting_macro, because
		 * we put c into autoservice there. That is pretty much a guaranteed
		 * deadlock. This is why the handling of c's lock may seem a bit unusual
		 * here.
		 */
		ast_party_redirecting_init(&redirecting);
		ast_party_redirecting_copy(&redirecting, &c->redirecting);
		ast_channel_unlock(c);
		if (ast_channel_redirecting_macro(c, in, &redirecting, 1, 0)) {
			ast_channel_update_redirecting(in, &redirecting, NULL);
		}
		ast_party_redirecting_free(&redirecting);
		ast_channel_unlock(in);

		ast_clear_flag64(peerflags, OPT_IGNORE_CONNECTEDLINE);
		if (ast_test_flag64(peerflags, OPT_CANCEL_TIMEOUT)) {
			*to = -1;
		}

		if (ast_call(c, stuff, 0)) {
			ast_log(LOG_NOTICE, "Forwarding failed to dial '%s/%s'\n",
				tech, stuff);
			ast_clear_flag64(o, DIAL_STILLGOING);
			ast_hangup(original);
			ast_hangup(c);
			c = o->chan = NULL;
			num->nochan++;
		} else {
			ast_channel_lock(c);
			while (ast_channel_trylock(in)) {
				CHANNEL_DEADLOCK_AVOIDANCE(c);
			}
			senddialevent(in, c, stuff);
			ast_channel_unlock(in);
			ast_channel_unlock(c);
			/* Hangup the original channel now, in case we needed it */
			ast_hangup(original);
		}
		if (single) {
			ast_indicate(in, -1);
		}
	}
}

/* argument used for some functions. */
struct privacy_args {
	int sentringing;
	int privdb_val;
	char privcid[256];
	char privintro[1024];
	char status[256];
};

static struct ast_channel *wait_for_answer(struct ast_channel *in,
	struct chanlist *outgoing, int *to, struct ast_flags64 *peerflags,
	char *opt_args[],
	struct privacy_args *pa,
	const struct cause_args *num_in, int *result, char *dtmf_progress,
	const int ignore_cc,
	struct ast_party_id *forced_clid, struct ast_party_id *stored_clid)
{
	struct cause_args num = *num_in;
	int prestart = num.busy + num.congestion + num.nochan;
	int orig = *to;
	struct ast_channel *peer = NULL;
	/* single is set if only one destination is enabled */
	int single = outgoing && !outgoing->next;
#ifdef HAVE_EPOLL
	struct chanlist *epollo;
#endif
	struct ast_party_connected_line connected_caller;
	struct ast_str *featurecode = ast_str_alloca(FEATURE_MAX_LEN + 1);
	int cc_recall_core_id;
	int is_cc_recall;
	int cc_frame_received = 0;
	int num_ringing = 0;

	ast_party_connected_line_init(&connected_caller);
	if (single) {
		/* Turn off hold music, etc */
		if (!ast_test_flag64(outgoing, OPT_MUSICBACK | OPT_RINGBACK)) {
			ast_deactivate_generator(in);
			/* If we are calling a single channel, and not providing ringback or music, */
			/* then, make them compatible for in-band tone purpose */
			if (ast_channel_make_compatible(outgoing->chan, in) < 0) {
				/* If these channels can not be made compatible, 
				 * there is no point in continuing.  The bridge
				 * will just fail if it gets that far.
				 */
				*to = -1;
				strcpy(pa->status, "CONGESTION");
				ast_cdr_failed(in->cdr);
				return NULL;
			}
		}

		if (!ast_test_flag64(peerflags, OPT_IGNORE_CONNECTEDLINE) && !ast_test_flag64(outgoing, DIAL_CALLERID_ABSENT)) {
			ast_channel_lock(outgoing->chan);
			ast_connected_line_copy_from_caller(&connected_caller, &outgoing->chan->caller);
			ast_channel_unlock(outgoing->chan);
			connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
			ast_channel_update_connected_line(in, &connected_caller, NULL);
			ast_party_connected_line_free(&connected_caller);
		}
	}

	is_cc_recall = ast_cc_is_recall(in, &cc_recall_core_id, NULL);

#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->next)
		ast_poll_channel_add(in, epollo->chan);
#endif

	while (*to && !peer) {
		struct chanlist *o;
		int pos = 0; /* how many channels do we handle */
		int numlines = prestart;
		struct ast_channel *winner;
		struct ast_channel *watchers[AST_MAX_WATCHERS];

		watchers[pos++] = in;
		for (o = outgoing; o; o = o->next) {
			/* Keep track of important channels */
			if (ast_test_flag64(o, DIAL_STILLGOING) && o->chan)
				watchers[pos++] = o->chan;
			numlines++;
		}
		if (pos == 1) { /* only the input channel is available */
			if (numlines == (num.busy + num.congestion + num.nochan)) {
				ast_verb(2, "Everyone is busy/congested at this time (%d:%d/%d/%d)\n", numlines, num.busy, num.congestion, num.nochan);
				if (num.busy)
					strcpy(pa->status, "BUSY");
				else if (num.congestion)
					strcpy(pa->status, "CONGESTION");
				else if (num.nochan)
					strcpy(pa->status, "CHANUNAVAIL");
			} else {
				ast_verb(3, "No one is available to answer at this time (%d:%d/%d/%d)\n", numlines, num.busy, num.congestion, num.nochan);
			}
			*to = 0;
			if (is_cc_recall) {
				ast_cc_failed(cc_recall_core_id, "Everyone is busy/congested for the recall. How sad");
			}
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		for (o = outgoing; o; o = o->next) {
			struct ast_frame *f;
			struct ast_channel *c = o->chan;

			if (c == NULL)
				continue;
			if (ast_test_flag64(o, DIAL_STILLGOING) && c->_state == AST_STATE_UP) {
				if (!peer) {
					ast_verb(3, "%s answered %s\n", c->name, in->name);
					if (!single && !ast_test_flag64(peerflags, OPT_IGNORE_CONNECTEDLINE)) {
						if (o->pending_connected_update) {
							if (ast_channel_connected_line_macro(c, in, &o->connected, 1, 0)) {
								ast_channel_update_connected_line(in, &o->connected, NULL);
							}
						} else if (!ast_test_flag64(o, DIAL_CALLERID_ABSENT)) {
							ast_channel_lock(c);
							ast_connected_line_copy_from_caller(&connected_caller, &c->caller);
							ast_channel_unlock(c);
							connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
							ast_channel_update_connected_line(in, &connected_caller, NULL);
							ast_party_connected_line_free(&connected_caller);
						}
					}
					if (o->aoc_s_rate_list) {
						size_t encoded_size;
						struct ast_aoc_encoded *encoded;
						if ((encoded = ast_aoc_encode(o->aoc_s_rate_list, &encoded_size, o->chan))) {
							ast_indicate_data(in, AST_CONTROL_AOC, encoded, encoded_size);
							ast_aoc_destroy_encoded(encoded);
						}
					}
					peer = c;
					ast_copy_flags64(peerflags, o,
						OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
						OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
						OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
						OPT_CALLEE_PARK | OPT_CALLER_PARK |
						OPT_CALLEE_MIXMONITOR | OPT_CALLER_MIXMONITOR |
						DIAL_NOFORWARDHTML);
					ast_string_field_set(c, dialcontext, "");
					ast_copy_string(c->exten, "", sizeof(c->exten));
				}
				continue;
			}
			if (c != winner)
				continue;
			/* here, o->chan == c == winner */
			if (!ast_strlen_zero(c->call_forward)) {
				pa->sentringing = 0;
				if (!ignore_cc && (f = ast_read(c))) {
					if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_CC) {
						/* This channel is forwarding the call, and is capable of CC, so
						 * be sure to add the new device interface to the list
						 */
						ast_handle_cc_control_frame(in, c, f->data.ptr);
					}
					ast_frfree(f);
				}
				do_forward(o, &num, peerflags, single, to, forced_clid, stored_clid);
				continue;
			}
			f = ast_read(winner);
			if (!f) {
				in->hangupcause = c->hangupcause;
#ifdef HAVE_EPOLL
				ast_poll_channel_del(in, c);
#endif
				ast_hangup(c);
				c = o->chan = NULL;
				ast_clear_flag64(o, DIAL_STILLGOING);
				handle_cause(in->hangupcause, &num);
				continue;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				switch (f->subclass.integer) {
				case AST_CONTROL_ANSWER:
					/* This is our guy if someone answered. */
					if (!peer) {
						ast_verb(3, "%s answered %s\n", c->name, in->name);
						if (!single && !ast_test_flag64(peerflags, OPT_IGNORE_CONNECTEDLINE)) {
							if (o->pending_connected_update) {
								if (ast_channel_connected_line_macro(c, in, &o->connected, 1, 0)) {
									ast_channel_update_connected_line(in, &o->connected, NULL);
								}
							} else if (!ast_test_flag64(o, DIAL_CALLERID_ABSENT)) {
								ast_channel_lock(c);
								ast_connected_line_copy_from_caller(&connected_caller, &c->caller);
								ast_channel_unlock(c);
								connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
								ast_channel_update_connected_line(in, &connected_caller, NULL);
								ast_party_connected_line_free(&connected_caller);
							}
						}
						if (o->aoc_s_rate_list) {
							size_t encoded_size;
							struct ast_aoc_encoded *encoded;
							if ((encoded = ast_aoc_encode(o->aoc_s_rate_list, &encoded_size, o->chan))) {
								ast_indicate_data(in, AST_CONTROL_AOC, encoded, encoded_size);
								ast_aoc_destroy_encoded(encoded);
							}
						}
						peer = c;
						if (peer->cdr) {
							peer->cdr->answer = ast_tvnow();
							peer->cdr->disposition = AST_CDR_ANSWERED;
						}
						ast_copy_flags64(peerflags, o,
							OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
							OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
							OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
							OPT_CALLEE_PARK | OPT_CALLER_PARK |
							OPT_CALLEE_MIXMONITOR | OPT_CALLER_MIXMONITOR |
							DIAL_NOFORWARDHTML);
						ast_string_field_set(c, dialcontext, "");
						ast_copy_string(c->exten, "", sizeof(c->exten));
						if (CAN_EARLY_BRIDGE(peerflags, in, peer))
							/* Setup early bridge if appropriate */
							ast_channel_early_bridge(in, peer);
					}
					/* If call has been answered, then the eventual hangup is likely to be normal hangup */
					in->hangupcause = AST_CAUSE_NORMAL_CLEARING;
					c->hangupcause = AST_CAUSE_NORMAL_CLEARING;
					break;
				case AST_CONTROL_BUSY:
					ast_verb(3, "%s is busy\n", c->name);
					in->hangupcause = c->hangupcause;
					ast_hangup(c);
					c = o->chan = NULL;
					ast_clear_flag64(o, DIAL_STILLGOING);
					handle_cause(AST_CAUSE_BUSY, &num);
					break;
				case AST_CONTROL_CONGESTION:
					ast_verb(3, "%s is circuit-busy\n", c->name);
					in->hangupcause = c->hangupcause;
					ast_hangup(c);
					c = o->chan = NULL;
					ast_clear_flag64(o, DIAL_STILLGOING);
					handle_cause(AST_CAUSE_CONGESTION, &num);
					break;
				case AST_CONTROL_RINGING:
					/* This is a tricky area to get right when using a native
					 * CC agent. The reason is that we do the best we can to send only a
					 * single ringing notification to the caller.
					 *
					 * Call completion complicates the logic used here. CCNR is typically
					 * offered during a ringing message. Let's say that party A calls
					 * parties B, C, and D. B and C do not support CC requests, but D
					 * does. If we were to receive a ringing notification from B before
					 * the others, then we would end up sending a ringing message to
					 * A with no CCNR offer present.
					 *
					 * The approach that we have taken is that if we receive a ringing
					 * response from a party and no CCNR offer is present, we need to
					 * wait. Specifically, we need to wait until either a) a called party
					 * offers CCNR in its ringing response or b) all called parties have
					 * responded in some way to our call and none offers CCNR.
					 *
					 * The drawback to this is that if one of the parties has a delayed
					 * response or, god forbid, one just plain doesn't respond to our
					 * outgoing call, then this will result in a significant delay between
					 * when the caller places the call and hears ringback.
					 *
					 * Note also that if CC is disabled for this call, then it is perfectly
					 * fine for ringing frames to get sent through.
					 */
					++num_ringing;
					if (ignore_cc || cc_frame_received || num_ringing == numlines) {
						ast_verb(3, "%s is ringing\n", c->name);
						/* Setup early media if appropriate */
						if (single && CAN_EARLY_BRIDGE(peerflags, in, c))
							ast_channel_early_bridge(in, c);
						if (!(pa->sentringing) && !ast_test_flag64(outgoing, OPT_MUSICBACK) && ast_strlen_zero(opt_args[OPT_ARG_RINGBACK])) {
							ast_indicate(in, AST_CONTROL_RINGING);
							pa->sentringing++;
						}
					}
					break;
				case AST_CONTROL_PROGRESS:
					ast_verb(3, "%s is making progress passing it to %s\n", c->name, in->name);
					/* Setup early media if appropriate */
					if (single && CAN_EARLY_BRIDGE(peerflags, in, c))
						ast_channel_early_bridge(in, c);
					if (!ast_test_flag64(outgoing, OPT_RINGBACK)) {
						if (single || (!single && !pa->sentringing)) {
							ast_indicate(in, AST_CONTROL_PROGRESS);
						}
					}
					if (!ast_strlen_zero(dtmf_progress)) {
						ast_verb(3,
							"Sending DTMF '%s' to the called party as result of receiving a PROGRESS message.\n",
							dtmf_progress);
						ast_dtmf_stream(c, in, dtmf_progress, 250, 0);
					}
					break;
				case AST_CONTROL_VIDUPDATE:
					ast_verb(3, "%s requested a video update, passing it to %s\n", c->name, in->name);
					ast_indicate(in, AST_CONTROL_VIDUPDATE);
					break;
				case AST_CONTROL_SRCUPDATE:
					ast_verb(3, "%s requested a source update, passing it to %s\n", c->name, in->name);
					ast_indicate(in, AST_CONTROL_SRCUPDATE);
					break;
				case AST_CONTROL_CONNECTED_LINE:
					if (ast_test_flag64(peerflags, OPT_IGNORE_CONNECTEDLINE)) {
						ast_verb(3, "Connected line update to %s prevented.\n", in->name);
					} else if (!single) {
						struct ast_party_connected_line connected;
						ast_verb(3, "%s connected line has changed. Saving it until answer for %s\n", c->name, in->name);
						ast_party_connected_line_set_init(&connected, &o->connected);
						ast_connected_line_parse_data(f->data.ptr, f->datalen, &connected);
						ast_party_connected_line_set(&o->connected, &connected, NULL);
						ast_party_connected_line_free(&connected);
						o->pending_connected_update = 1;
					} else {
						if (ast_channel_connected_line_macro(c, in, f, 1, 1)) {
							ast_indicate_data(in, AST_CONTROL_CONNECTED_LINE, f->data.ptr, f->datalen);
						}
					}
					break;
				case AST_CONTROL_AOC:
					{
						struct ast_aoc_decoded *decoded = ast_aoc_decode(f->data.ptr, f->datalen, o->chan);
						if (decoded && (ast_aoc_get_msg_type(decoded) == AST_AOC_S)) {
							ast_aoc_destroy_decoded(o->aoc_s_rate_list);
							o->aoc_s_rate_list = decoded;
						} else {
							ast_aoc_destroy_decoded(decoded);
						}
					}
					break;
				case AST_CONTROL_REDIRECTING:
					if (ast_test_flag64(peerflags, OPT_IGNORE_CONNECTEDLINE)) {
						ast_verb(3, "Redirecting update to %s prevented.\n", in->name);
					} else if (single) {
						ast_verb(3, "%s redirecting info has changed, passing it to %s\n", c->name, in->name);
						if (ast_channel_redirecting_macro(c, in, f, 1, 1)) {
							ast_indicate_data(in, AST_CONTROL_REDIRECTING, f->data.ptr, f->datalen);
						}
						pa->sentringing = 0;
					}
					break;
				case AST_CONTROL_PROCEEDING:
					ast_verb(3, "%s is proceeding passing it to %s\n", c->name, in->name);
					if (single && CAN_EARLY_BRIDGE(peerflags, in, c))
						ast_channel_early_bridge(in, c);
					if (!ast_test_flag64(outgoing, OPT_RINGBACK))
						ast_indicate(in, AST_CONTROL_PROCEEDING);
					break;
				case AST_CONTROL_HOLD:
					ast_verb(3, "Call on %s placed on hold\n", c->name);
					ast_indicate(in, AST_CONTROL_HOLD);
					break;
				case AST_CONTROL_UNHOLD:
					ast_verb(3, "Call on %s left from hold\n", c->name);
					ast_indicate(in, AST_CONTROL_UNHOLD);
					break;
				case AST_CONTROL_OFFHOOK:
				case AST_CONTROL_FLASH:
					/* Ignore going off hook and flash */
					break;
				case AST_CONTROL_CC:
					if (!ignore_cc) {
						ast_handle_cc_control_frame(in, c, f->data.ptr);
						cc_frame_received = 1;
					}
					break;
				case -1:
					if (!ast_test_flag64(outgoing, OPT_RINGBACK | OPT_MUSICBACK)) {
						ast_verb(3, "%s stopped sounds\n", c->name);
						ast_indicate(in, -1);
						pa->sentringing = 0;
					}
					break;
				default:
					ast_debug(1, "Dunno what to do with control type %d\n", f->subclass.integer);
				}
			} else if (single) {
				switch (f->frametype) {
				case AST_FRAME_VOICE:
				case AST_FRAME_IMAGE:
				case AST_FRAME_TEXT:
					if (ast_write(in, f)) {
						ast_log(LOG_WARNING, "Unable to write frametype: %d\n",
							f->frametype);
					}
					break;
				case AST_FRAME_HTML:
					if (!ast_test_flag64(outgoing, DIAL_NOFORWARDHTML)
						&& ast_channel_sendhtml(in, f->subclass.integer, f->data.ptr, f->datalen) == -1) {
						ast_log(LOG_WARNING, "Unable to send URL\n");
					}
					break;
				default:
					break;
				}
			}
			ast_frfree(f);
		} /* end for */
		if (winner == in) {
			struct ast_frame *f = ast_read(in);
#if 0
			if (f && (f->frametype != AST_FRAME_VOICE))
				printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != AST_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to = -1;
				strcpy(pa->status, "CANCEL");
				ast_cdr_noanswer(in->cdr);
				if (f) {
					if (f->data.uint32) {
						in->hangupcause = f->data.uint32;
					}
					ast_frfree(f);
				}
				if (is_cc_recall) {
					ast_cc_completed(in, "CC completed, although the caller hung up (cancelled)");
				}
				return NULL;
			}

			/* now f is guaranteed non-NULL */
			if (f->frametype == AST_FRAME_DTMF) {
				if (ast_test_flag64(peerflags, OPT_DTMF_EXIT)) {
					const char *context;
					ast_channel_lock(in);
					context = pbx_builtin_getvar_helper(in, "EXITCONTEXT");
					if (onedigit_goto(in, context, (char) f->subclass.integer, 1)) {
						ast_verb(3, "User hit %c to disconnect call.\n", f->subclass.integer);
						*to = 0;
						ast_cdr_noanswer(in->cdr);
						*result = f->subclass.integer;
						strcpy(pa->status, "CANCEL");
						ast_frfree(f);
						ast_channel_unlock(in);
						if (is_cc_recall) {
							ast_cc_completed(in, "CC completed, but the caller used DTMF to exit");
						}
						return NULL;
					}
					ast_channel_unlock(in);
				}

				if (ast_test_flag64(peerflags, OPT_CALLER_HANGUP) &&
					detect_disconnect(in, f->subclass.integer, featurecode)) {
					ast_verb(3, "User requested call disconnect.\n");
					*to = 0;
					strcpy(pa->status, "CANCEL");
					ast_cdr_noanswer(in->cdr);
					ast_frfree(f);
					if (is_cc_recall) {
						ast_cc_completed(in, "CC completed, but the caller hung up with DTMF");
					}
					return NULL;
				}
			}

			/* Send the frame from the in channel to all outgoing channels. */
			for (o = outgoing; o; o = o->next) {
				if (!o->chan || !ast_test_flag64(o, DIAL_STILLGOING)) {
					/* This outgoing channel has died so don't send the frame to it. */
					continue;
				}
				switch (f->frametype) {
				case AST_FRAME_HTML:
					/* Forward HTML stuff */
					if (!ast_test_flag64(o, DIAL_NOFORWARDHTML)
						&& ast_channel_sendhtml(o->chan, f->subclass.integer, f->data.ptr, f->datalen) == -1) {
						ast_log(LOG_WARNING, "Unable to send URL\n");
					}
					break;
				case AST_FRAME_VOICE:
				case AST_FRAME_IMAGE:
				case AST_FRAME_TEXT:
				case AST_FRAME_DTMF_BEGIN:
				case AST_FRAME_DTMF_END:
					if (ast_write(o->chan, f)) {
						ast_log(LOG_WARNING, "Unable to forward frametype: %d\n",
							f->frametype);
					}
					break;
				case AST_FRAME_CONTROL:
					switch (f->subclass.integer) {
					case AST_CONTROL_HOLD:
					case AST_CONTROL_UNHOLD:
					case AST_CONTROL_VIDUPDATE:
					case AST_CONTROL_SRCUPDATE:
						ast_verb(3, "%s requested special control %d, passing it to %s\n",
							in->name, f->subclass.integer, o->chan->name);
						ast_indicate_data(o->chan, f->subclass.integer, f->data.ptr, f->datalen);
						break;
					case AST_CONTROL_CONNECTED_LINE:
						if (ast_channel_connected_line_macro(in, o->chan, f, 0, 1)) {
							ast_indicate_data(o->chan, f->subclass.integer, f->data.ptr, f->datalen);
						}
						break;
					case AST_CONTROL_REDIRECTING:
						if (ast_channel_redirecting_macro(in, o->chan, f, 0, 1)) {
							ast_indicate_data(o->chan, f->subclass.integer, f->data.ptr, f->datalen);
						}
						break;
					default:
						break;
					}
					break;
				default:
					break;
				}
			}
			ast_frfree(f);
		}
		if (!*to)
			ast_verb(3, "Nobody picked up in %d ms\n", orig);
		if (!*to || ast_check_hangup(in))
			ast_cdr_noanswer(in->cdr);
	}

#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->next) {
		if (epollo->chan)
			ast_poll_channel_del(in, epollo->chan);
	}
#endif

	if (is_cc_recall) {
		ast_cc_completed(in, "Recall completed!");
	}
	return peer;
}

static int detect_disconnect(struct ast_channel *chan, char code, struct ast_str *featurecode)
{
	struct ast_flags features = { AST_FEATURE_DISCONNECT }; /* only concerned with disconnect feature */
	struct ast_call_feature feature = { 0, };
	int res;

	ast_str_append(&featurecode, 1, "%c", code);

	res = ast_feature_detect(chan, &features, ast_str_buffer(featurecode), &feature);

	if (res != AST_FEATURE_RETURN_STOREDIGITS) {
		ast_str_reset(featurecode);
	}
	if (feature.feature_mask & AST_FEATURE_DISCONNECT) {
		return 1;
	}

	return 0;
}

static void replace_macro_delimiter(char *s)
{
	for (; *s; s++)
		if (*s == '^')
			*s = ',';
}

/* returns true if there is a valid privacy reply */
static int valid_priv_reply(struct ast_flags64 *opts, int res)
{
	if (res < '1')
		return 0;
	if (ast_test_flag64(opts, OPT_PRIVACY) && res <= '5')
		return 1;
	if (ast_test_flag64(opts, OPT_SCREENING) && res <= '4')
		return 1;
	return 0;
}

static int do_privacy(struct ast_channel *chan, struct ast_channel *peer,
	struct ast_flags64 *opts, char **opt_args, struct privacy_args *pa)
{

	int res2;
	int loopcount = 0;

	/* Get the user's intro, store it in priv-callerintros/$CID,
	   unless it is already there-- this should be done before the
	   call is actually dialed  */

	/* all ring indications and moh for the caller has been halted as soon as the
	   target extension was picked up. We are going to have to kill some
	   time and make the caller believe the peer hasn't picked up yet */

	if (ast_test_flag64(opts, OPT_MUSICBACK) && !ast_strlen_zero(opt_args[OPT_ARG_MUSICBACK])) {
		char *original_moh = ast_strdupa(chan->musicclass);
		ast_indicate(chan, -1);
		ast_string_field_set(chan, musicclass, opt_args[OPT_ARG_MUSICBACK]);
		ast_moh_start(chan, opt_args[OPT_ARG_MUSICBACK], NULL);
		ast_string_field_set(chan, musicclass, original_moh);
	} else if (ast_test_flag64(opts, OPT_RINGBACK)) {
		ast_indicate(chan, AST_CONTROL_RINGING);
		pa->sentringing++;
	}

	/* Start autoservice on the other chan ?? */
	res2 = ast_autoservice_start(chan);
	/* Now Stream the File */
	for (loopcount = 0; loopcount < 3; loopcount++) {
		if (res2 && loopcount == 0) /* error in ast_autoservice_start() */
			break;
		if (!res2) /* on timeout, play the message again */
			res2 = ast_play_and_wait(peer, "priv-callpending");
		if (!valid_priv_reply(opts, res2))
			res2 = 0;
		/* priv-callpending script:
		   "I have a caller waiting, who introduces themselves as:"
		*/
		if (!res2)
			res2 = ast_play_and_wait(peer, pa->privintro);
		if (!valid_priv_reply(opts, res2))
			res2 = 0;
		/* now get input from the called party, as to their choice */
		if (!res2) {
			/* XXX can we have both, or they are mutually exclusive ? */
			if (ast_test_flag64(opts, OPT_PRIVACY))
				res2 = ast_play_and_wait(peer, "priv-callee-options");
			if (ast_test_flag64(opts, OPT_SCREENING))
				res2 = ast_play_and_wait(peer, "screen-callee-options");
		}
		/*! \page DialPrivacy Dial Privacy scripts
		\par priv-callee-options script:
			"Dial 1 if you wish this caller to reach you directly in the future,
				and immediately connect to their incoming call
			 Dial 2 if you wish to send this caller to voicemail now and
				forevermore.
			 Dial 3 to send this caller to the torture menus, now and forevermore.
			 Dial 4 to send this caller to a simple "go away" menu, now and forevermore.
			 Dial 5 to allow this caller to come straight thru to you in the future,
				but right now, just this once, send them to voicemail."
		\par screen-callee-options script:
			"Dial 1 if you wish to immediately connect to the incoming call
			 Dial 2 if you wish to send this caller to voicemail.
			 Dial 3 to send this caller to the torture menus.
			 Dial 4 to send this caller to a simple "go away" menu.
		*/
		if (valid_priv_reply(opts, res2))
			break;
		/* invalid option */
		res2 = ast_play_and_wait(peer, "vm-sorry");
	}

	if (ast_test_flag64(opts, OPT_MUSICBACK)) {
		ast_moh_stop(chan);
	} else if (ast_test_flag64(opts, OPT_RINGBACK)) {
		ast_indicate(chan, -1);
		pa->sentringing = 0;
	}
	ast_autoservice_stop(chan);
	if (ast_test_flag64(opts, OPT_PRIVACY) && (res2 >= '1' && res2 <= '5')) {
		/* map keypresses to various things, the index is res2 - '1' */
		static const char * const _val[] = { "ALLOW", "DENY", "TORTURE", "KILL", "ALLOW" };
		static const int _flag[] = { AST_PRIVACY_ALLOW, AST_PRIVACY_DENY, AST_PRIVACY_TORTURE, AST_PRIVACY_KILL, AST_PRIVACY_ALLOW};
		int i = res2 - '1';
		ast_verb(3, "--Set privacy database entry %s/%s to %s\n",
			opt_args[OPT_ARG_PRIVACY], pa->privcid, _val[i]);
		ast_privacy_set(opt_args[OPT_ARG_PRIVACY], pa->privcid, _flag[i]);
	}
	switch (res2) {
	case '1':
		break;
	case '2':
		ast_copy_string(pa->status, "NOANSWER", sizeof(pa->status));
		break;
	case '3':
		ast_copy_string(pa->status, "TORTURE", sizeof(pa->status));
		break;
	case '4':
		ast_copy_string(pa->status, "DONTCALL", sizeof(pa->status));
		break;
	case '5':
		/* XXX should we set status to DENY ? */
		if (ast_test_flag64(opts, OPT_PRIVACY))
			break;
		/* if not privacy, then 5 is the same as "default" case */
	default: /* bad input or -1 if failure to start autoservice */
		/* well, if the user messes up, ... he had his chance... What Is The Best Thing To Do?  */
		/* well, there seems basically two choices. Just patch the caller thru immediately,
			  or,... put 'em thru to voicemail. */
		/* since the callee may have hung up, let's do the voicemail thing, no database decision */
		ast_log(LOG_NOTICE, "privacy: no valid response from the callee. Sending the caller to voicemail, the callee isn't responding\n");
		/* XXX should we set status to DENY ? */
		/* XXX what about the privacy flags ? */
		break;
	}

	if (res2 == '1') { /* the only case where we actually connect */
		/* if the intro is NOCALLERID, then there's no reason to leave it on disk, it'll
		   just clog things up, and it's not useful information, not being tied to a CID */
		if (strncmp(pa->privcid, "NOCALLERID", 10) == 0 || ast_test_flag64(opts, OPT_SCREEN_NOINTRO)) {
			ast_filedelete(pa->privintro, NULL);
			if (ast_fileexists(pa->privintro, NULL, NULL) > 0)
				ast_log(LOG_NOTICE, "privacy: ast_filedelete didn't do its job on %s\n", pa->privintro);
			else
				ast_verb(3, "Successfully deleted %s intro file\n", pa->privintro);
		}
		return 0; /* the good exit path */
	} else {
		ast_hangup(peer); /* hang up on the callee -- he didn't want to talk anyway! */
		return -1;
	}
}

/*! \brief returns 1 if successful, 0 or <0 if the caller should 'goto out' */
static int setup_privacy_args(struct privacy_args *pa,
	struct ast_flags64 *opts, char *opt_args[], struct ast_channel *chan)
{
	char callerid[60];
	int res;
	char *l;
	int silencethreshold;

	if (chan->caller.id.number.valid
		&& !ast_strlen_zero(chan->caller.id.number.str)) {
		l = ast_strdupa(chan->caller.id.number.str);
		ast_shrink_phone_number(l);
		if (ast_test_flag64(opts, OPT_PRIVACY) ) {
			ast_verb(3, "Privacy DB is '%s', clid is '%s'\n", opt_args[OPT_ARG_PRIVACY], l);
			pa->privdb_val = ast_privacy_check(opt_args[OPT_ARG_PRIVACY], l);
		} else {
			ast_verb(3, "Privacy Screening, clid is '%s'\n", l);
			pa->privdb_val = AST_PRIVACY_UNKNOWN;
		}
	} else {
		char *tnam, *tn2;

		tnam = ast_strdupa(chan->name);
		/* clean the channel name so slashes don't try to end up in disk file name */
		for (tn2 = tnam; *tn2; tn2++) {
			if (*tn2 == '/')  /* any other chars to be afraid of? */
				*tn2 = '=';
		}
		ast_verb(3, "Privacy-- callerid is empty\n");

		snprintf(callerid, sizeof(callerid), "NOCALLERID_%s%s", chan->exten, tnam);
		l = callerid;
		pa->privdb_val = AST_PRIVACY_UNKNOWN;
	}

	ast_copy_string(pa->privcid, l, sizeof(pa->privcid));

	if (strncmp(pa->privcid, "NOCALLERID", 10) != 0 && ast_test_flag64(opts, OPT_SCREEN_NOCALLERID)) {
		/* if callerid is set and OPT_SCREEN_NOCALLERID is set also */
		ast_verb(3, "CallerID set (%s); N option set; Screening should be off\n", pa->privcid);
		pa->privdb_val = AST_PRIVACY_ALLOW;
	} else if (ast_test_flag64(opts, OPT_SCREEN_NOCALLERID) && strncmp(pa->privcid, "NOCALLERID", 10) == 0) {
		ast_verb(3, "CallerID blank; N option set; Screening should happen; dbval is %d\n", pa->privdb_val);
	}
	
	if (pa->privdb_val == AST_PRIVACY_DENY) {
		ast_verb(3, "Privacy DB reports PRIVACY_DENY for this callerid. Dial reports unavailable\n");
		ast_copy_string(pa->status, "NOANSWER", sizeof(pa->status));
		return 0;
	} else if (pa->privdb_val == AST_PRIVACY_KILL) {
		ast_copy_string(pa->status, "DONTCALL", sizeof(pa->status));
		return 0; /* Is this right? */
	} else if (pa->privdb_val == AST_PRIVACY_TORTURE) {
		ast_copy_string(pa->status, "TORTURE", sizeof(pa->status));
		return 0; /* is this right??? */
	} else if (pa->privdb_val == AST_PRIVACY_UNKNOWN) {
		/* Get the user's intro, store it in priv-callerintros/$CID,
		   unless it is already there-- this should be done before the
		   call is actually dialed  */

		/* make sure the priv-callerintros dir actually exists */
		snprintf(pa->privintro, sizeof(pa->privintro), "%s/sounds/priv-callerintros", ast_config_AST_DATA_DIR);
		if ((res = ast_mkdir(pa->privintro, 0755))) {
			ast_log(LOG_WARNING, "privacy: can't create directory priv-callerintros: %s\n", strerror(res));
			return -1;
		}

		snprintf(pa->privintro, sizeof(pa->privintro), "priv-callerintros/%s", pa->privcid);
		if (ast_fileexists(pa->privintro, NULL, NULL ) > 0 && strncmp(pa->privcid, "NOCALLERID", 10) != 0) {
			/* the DELUX version of this code would allow this caller the
			   option to hear and retape their previously recorded intro.
			*/
		} else {
			int duration; /* for feedback from play_and_wait */
			/* the file doesn't exist yet. Let the caller submit his
			   vocal intro for posterity */
			/* priv-recordintro script:

			   "At the tone, please say your name:"

			*/
			silencethreshold = ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE);
			ast_answer(chan);
			res = ast_play_and_record(chan, "priv-recordintro", pa->privintro, 4, "sln", &duration, silencethreshold, 2000, 0);  /* NOTE: I've reduced the total time to 4 sec */
									/* don't think we'll need a lock removed, we took care of
									   conflicts by naming the pa.privintro file */
			if (res == -1) {
				/* Delete the file regardless since they hung up during recording */
				ast_filedelete(pa->privintro, NULL);
				if (ast_fileexists(pa->privintro, NULL, NULL) > 0)
					ast_log(LOG_NOTICE, "privacy: ast_filedelete didn't do its job on %s\n", pa->privintro);
				else
					ast_verb(3, "Successfully deleted %s intro file\n", pa->privintro);
				return -1;
			}
			if (!ast_streamfile(chan, "vm-dialout", chan->language) )
				ast_waitstream(chan, "");
		}
	}
	return 1; /* success */
}

static void end_bridge_callback(void *data)
{
	char buf[80];
	time_t end;
	struct ast_channel *chan = data;

	if (!chan->cdr) {
		return;
	}

	time(&end);

	ast_channel_lock(chan);
	if (chan->cdr->answer.tv_sec) {
		snprintf(buf, sizeof(buf), "%ld", (long) end - chan->cdr->answer.tv_sec);
		pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", buf);
	}

	if (chan->cdr->start.tv_sec) {
		snprintf(buf, sizeof(buf), "%ld", (long) end - chan->cdr->start.tv_sec);
		pbx_builtin_setvar_helper(chan, "DIALEDTIME", buf);
	}
	ast_channel_unlock(chan);
}

static void end_bridge_callback_data_fixup(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator) {
	bconfig->end_bridge_callback_data = originator;
}

static int dial_handle_playtones(struct ast_channel *chan, const char *data)
{
	struct ast_tone_zone_sound *ts = NULL;
	int res;
	const char *str = data;

	if (ast_strlen_zero(str)) {
		ast_debug(1,"Nothing to play\n");
		return -1;
	}

	ts = ast_get_indication_tone(chan->zone, str);

	if (ts && ts->data[0]) {
		res = ast_playtones_start(chan, 0, ts->data, 0);
	} else {
		res = -1;
	}

	if (ts) {
		ts = ast_tone_zone_sound_unref(ts);
	}

	if (res) {
		ast_log(LOG_WARNING, "Unable to start playtone \'%s\'\n", str);
	}

	return res;
}

static int dial_exec_full(struct ast_channel *chan, const char *data, struct ast_flags64 *peerflags, int *continue_exec)
{
	int res = -1; /* default: error */
	char *rest, *cur; /* scan the list of destinations */
	struct chanlist *outgoing = NULL; /* list of destinations */
	struct ast_channel *peer;
	int to; /* timeout */
	struct cause_args num = { chan, 0, 0, 0 };
	int cause;
	char numsubst[256];

	struct ast_bridge_config config = { { 0, } };
	struct timeval calldurationlimit = { 0, };
	char *dtmfcalled = NULL, *dtmfcalling = NULL, *dtmf_progress=NULL;
	struct privacy_args pa = {
		.sentringing = 0,
		.privdb_val = 0,
		.status = "INVALIDARGS",
	};
	int sentringing = 0, moh = 0;
	const char *outbound_group = NULL;
	int result = 0;
	char *parse;
	int opermode = 0;
	int delprivintro = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(peers);
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
		AST_APP_ARG(url);
	);
	struct ast_flags64 opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct ast_datastore *datastore = NULL;
	int fulldial = 0, num_dialed = 0;
	int ignore_cc = 0;
	char device_name[AST_CHANNEL_NAME];
	char forced_clid_name[AST_MAX_EXTENSION];
	char stored_clid_name[AST_MAX_EXTENSION];
	int force_forwards_only;	/*!< TRUE if force CallerID on call forward only. Legacy behaviour.*/
	/*!
	 * \brief Forced CallerID party information to send.
	 * \note This will not have any malloced strings so do not free it.
	 */
	struct ast_party_id forced_clid;
	/*!
	 * \brief Stored CallerID information if needed.
	 *
	 * \note If OPT_ORIGINAL_CLID set then this is the o option
	 * CallerID.  Otherwise it is the dialplan extension and hint
	 * name.
	 *
	 * \note This will not have any malloced strings so do not free it.
	 */
	struct ast_party_id stored_clid;
	/*!
	 * \brief CallerID party information to store.
	 * \note This will not have any malloced strings so do not free it.
	 */
	struct ast_party_caller caller;

	/* Reset all DIAL variables back to blank, to prevent confusion (in case we don't reset all of them). */
	pbx_builtin_setvar_helper(chan, "DIALSTATUS", "");
	pbx_builtin_setvar_helper(chan, "DIALEDPEERNUMBER", "");
	pbx_builtin_setvar_helper(chan, "DIALEDPEERNAME", "");
	pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", "");
	pbx_builtin_setvar_helper(chan, "DIALEDTIME", "");

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology/number)\n");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options) &&
		ast_app_parse_options64(dial_exec_options, &opts, opt_args, args.options)) {
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		goto done;
	}

	if (ast_strlen_zero(args.peers)) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology/number)\n");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		goto done;
	}

	if (ast_cc_call_init(chan, &ignore_cc)) {
		goto done;
	}

	if (ast_test_flag64(&opts, OPT_SCREEN_NOINTRO) && !ast_strlen_zero(opt_args[OPT_ARG_SCREEN_NOINTRO])) {
		delprivintro = atoi(opt_args[OPT_ARG_SCREEN_NOINTRO]);

		if (delprivintro < 0 || delprivintro > 1) {
			ast_log(LOG_WARNING, "Unknown argument %d specified to n option, ignoring\n", delprivintro);
			delprivintro = 0;
		}
	}

	if (!ast_test_flag64(&opts, OPT_RINGBACK)) {
		opt_args[OPT_ARG_RINGBACK] = NULL;
	}

	if (ast_test_flag64(&opts, OPT_OPERMODE)) {
		opermode = ast_strlen_zero(opt_args[OPT_ARG_OPERMODE]) ? 1 : atoi(opt_args[OPT_ARG_OPERMODE]);
		ast_verb(3, "Setting operator services mode to %d.\n", opermode);
	}

	if (ast_test_flag64(&opts, OPT_DURATION_STOP) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_STOP])) {
		calldurationlimit.tv_sec = atoi(opt_args[OPT_ARG_DURATION_STOP]);
		if (!calldurationlimit.tv_sec) {
			ast_log(LOG_WARNING, "Dial does not accept S(%s), hanging up.\n", opt_args[OPT_ARG_DURATION_STOP]);
			pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
			goto done;
		}
		ast_verb(3, "Setting call duration limit to %.3lf seconds.\n", calldurationlimit.tv_sec + calldurationlimit.tv_usec / 1000000.0);
	}

	if (ast_test_flag64(&opts, OPT_SENDDTMF) && !ast_strlen_zero(opt_args[OPT_ARG_SENDDTMF])) {
		dtmf_progress = opt_args[OPT_ARG_SENDDTMF];
		dtmfcalled = strsep(&dtmf_progress, ":");
		dtmfcalling = strsep(&dtmf_progress, ":");
	}

	if (ast_test_flag64(&opts, OPT_DURATION_LIMIT) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_LIMIT])) {
		if (ast_bridge_timelimit(chan, &config, opt_args[OPT_ARG_DURATION_LIMIT], &calldurationlimit))
			goto done;
	}

	/* Setup the forced CallerID information to send if used. */
	ast_party_id_init(&forced_clid);
	force_forwards_only = 0;
	if (ast_test_flag64(&opts, OPT_FORCECLID)) {
		if (ast_strlen_zero(opt_args[OPT_ARG_FORCECLID])) {
			ast_channel_lock(chan);
			forced_clid.number.str = ast_strdupa(S_OR(chan->macroexten, chan->exten));
			ast_channel_unlock(chan);
			forced_clid_name[0] = '\0';
			forced_clid.name.str = (char *) get_cid_name(forced_clid_name,
				sizeof(forced_clid_name), chan);
			force_forwards_only = 1;
		} else {
			/* Note: The opt_args[OPT_ARG_FORCECLID] string value is altered here. */
			ast_callerid_parse(opt_args[OPT_ARG_FORCECLID], &forced_clid.name.str,
				&forced_clid.number.str);
		}
		if (!ast_strlen_zero(forced_clid.name.str)) {
			forced_clid.name.valid = 1;
		}
		if (!ast_strlen_zero(forced_clid.number.str)) {
			forced_clid.number.valid = 1;
		}
	}
	if (ast_test_flag64(&opts, OPT_FORCE_CID_TAG)
		&& !ast_strlen_zero(opt_args[OPT_ARG_FORCE_CID_TAG])) {
		forced_clid.tag = opt_args[OPT_ARG_FORCE_CID_TAG];
	}
	forced_clid.number.presentation = AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
	if (ast_test_flag64(&opts, OPT_FORCE_CID_PRES)
		&& !ast_strlen_zero(opt_args[OPT_ARG_FORCE_CID_PRES])) {
		int pres;

		pres = ast_parse_caller_presentation(opt_args[OPT_ARG_FORCE_CID_PRES]);
		if (0 <= pres) {
			forced_clid.number.presentation = pres;
		}
	}

	/* Setup the stored CallerID information if needed. */
	ast_party_id_init(&stored_clid);
	if (ast_test_flag64(&opts, OPT_ORIGINAL_CLID)) {
		if (ast_strlen_zero(opt_args[OPT_ARG_ORIGINAL_CLID])) {
			ast_channel_lock(chan);
			ast_party_id_set_init(&stored_clid, &chan->caller.id);
			if (!ast_strlen_zero(chan->caller.id.name.str)) {
				stored_clid.name.str = ast_strdupa(chan->caller.id.name.str);
			}
			if (!ast_strlen_zero(chan->caller.id.number.str)) {
				stored_clid.number.str = ast_strdupa(chan->caller.id.number.str);
			}
			if (!ast_strlen_zero(chan->caller.id.subaddress.str)) {
				stored_clid.subaddress.str = ast_strdupa(chan->caller.id.subaddress.str);
			}
			if (!ast_strlen_zero(chan->caller.id.tag)) {
				stored_clid.tag = ast_strdupa(chan->caller.id.tag);
			}
			ast_channel_unlock(chan);
		} else {
			/* Note: The opt_args[OPT_ARG_ORIGINAL_CLID] string value is altered here. */
			ast_callerid_parse(opt_args[OPT_ARG_ORIGINAL_CLID], &stored_clid.name.str,
				&stored_clid.number.str);
			if (!ast_strlen_zero(stored_clid.name.str)) {
				stored_clid.name.valid = 1;
			}
			if (!ast_strlen_zero(stored_clid.number.str)) {
				stored_clid.number.valid = 1;
			}
		}
	} else {
		/*
		 * In case the new channel has no preset CallerID number by the
		 * channel driver, setup the dialplan extension and hint name.
		 */
		stored_clid_name[0] = '\0';
		stored_clid.name.str = (char *) get_cid_name(stored_clid_name,
			sizeof(stored_clid_name), chan);
		if (ast_strlen_zero(stored_clid.name.str)) {
			stored_clid.name.str = NULL;
		} else {
			stored_clid.name.valid = 1;
		}
		ast_channel_lock(chan);
		stored_clid.number.str = ast_strdupa(S_OR(chan->macroexten, chan->exten));
		stored_clid.number.valid = 1;
		ast_channel_unlock(chan);
	}

	if (ast_test_flag64(&opts, OPT_RESETCDR) && chan->cdr)
		ast_cdr_reset(chan->cdr, NULL);
	if (ast_test_flag64(&opts, OPT_PRIVACY) && ast_strlen_zero(opt_args[OPT_ARG_PRIVACY]))
		opt_args[OPT_ARG_PRIVACY] = ast_strdupa(chan->exten);

	if (ast_test_flag64(&opts, OPT_PRIVACY) || ast_test_flag64(&opts, OPT_SCREENING)) {
		res = setup_privacy_args(&pa, &opts, opt_args, chan);
		if (res <= 0)
			goto out;
		res = -1; /* reset default */
	}

	if (ast_test_flag64(&opts, OPT_DTMF_EXIT) || ast_test_flag64(&opts, OPT_CALLER_HANGUP)) {
		__ast_answer(chan, 0, 0);
	}

	if (continue_exec)
		*continue_exec = 0;

	/* If a channel group has been specified, get it for use when we create peer channels */

	ast_channel_lock(chan);
	if ((outbound_group = pbx_builtin_getvar_helper(chan, "OUTBOUND_GROUP_ONCE"))) {
		outbound_group = ast_strdupa(outbound_group);	
		pbx_builtin_setvar_helper(chan, "OUTBOUND_GROUP_ONCE", NULL);
	} else if ((outbound_group = pbx_builtin_getvar_helper(chan, "OUTBOUND_GROUP"))) {
		outbound_group = ast_strdupa(outbound_group);
	}
	ast_channel_unlock(chan);	
	ast_copy_flags64(peerflags, &opts, OPT_DTMF_EXIT | OPT_GO_ON | OPT_ORIGINAL_CLID | OPT_CALLER_HANGUP | OPT_IGNORE_FORWARDING | OPT_IGNORE_CONNECTEDLINE |
			 OPT_CANCEL_TIMEOUT | OPT_ANNOUNCE | OPT_CALLEE_MACRO | OPT_CALLEE_GOSUB | OPT_FORCECLID);

	/* loop through the list of dial destinations */
	rest = args.peers;
	while ((cur = strsep(&rest, "&")) ) {
		struct chanlist *tmp;
		struct ast_channel *tc; /* channel for this destination */
		/* Get a technology/[device:]number pair */
		char *number = cur;
		char *interface = ast_strdupa(number);
		char *tech = strsep(&number, "/");
		/* find if we already dialed this interface */
		struct ast_dialed_interface *di;
		AST_LIST_HEAD(, ast_dialed_interface) *dialed_interfaces;
		num_dialed++;
		if (ast_strlen_zero(number)) {
			ast_log(LOG_WARNING, "Dial argument takes format (technology/[device:]number1)\n");
			goto out;
		}
		if (!(tmp = ast_calloc(1, sizeof(*tmp))))
			goto out;
		if (opts.flags) {
			ast_copy_flags64(tmp, &opts,
				OPT_CANCEL_ELSEWHERE |
				OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
				OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
				OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
				OPT_CALLEE_PARK | OPT_CALLER_PARK |
				OPT_CALLEE_MIXMONITOR | OPT_CALLER_MIXMONITOR |
				OPT_RINGBACK | OPT_MUSICBACK | OPT_FORCECLID);
			ast_set2_flag64(tmp, args.url, DIAL_NOFORWARDHTML);
		}
		ast_copy_string(numsubst, number, sizeof(numsubst));
		/* Request the peer */

		ast_channel_lock(chan);
		datastore = ast_channel_datastore_find(chan, &dialed_interface_info, NULL);
		/*
		 * Seed the chanlist's connected line information with previously
		 * acquired connected line info from the incoming channel.  The
		 * previously acquired connected line info could have been set
		 * through the CONNECTED_LINE dialplan function.
		 */
		ast_party_connected_line_copy(&tmp->connected, &chan->connected);
		ast_channel_unlock(chan);

		if (datastore)
			dialed_interfaces = datastore->data;
		else {
			if (!(datastore = ast_datastore_alloc(&dialed_interface_info, NULL))) {
				ast_log(LOG_WARNING, "Unable to create channel datastore for dialed interfaces. Aborting!\n");
				chanlist_free(tmp);
				goto out;
			}

			datastore->inheritance = DATASTORE_INHERIT_FOREVER;

			if (!(dialed_interfaces = ast_calloc(1, sizeof(*dialed_interfaces)))) {
				ast_datastore_free(datastore);
				chanlist_free(tmp);
				goto out;
			}

			datastore->data = dialed_interfaces;
			AST_LIST_HEAD_INIT(dialed_interfaces);

			ast_channel_lock(chan);
			ast_channel_datastore_add(chan, datastore);
			ast_channel_unlock(chan);
		}

		AST_LIST_LOCK(dialed_interfaces);
		AST_LIST_TRAVERSE(dialed_interfaces, di, list) {
			if (!strcasecmp(di->interface, interface)) {
				ast_log(LOG_WARNING, "Skipping dialing interface '%s' again since it has already been dialed\n",
					di->interface);
				break;
			}
		}
		AST_LIST_UNLOCK(dialed_interfaces);

		if (di) {
			fulldial++;
			chanlist_free(tmp);
			continue;
		}

		/* It is always ok to dial a Local interface.  We only keep track of
		 * which "real" interfaces have been dialed.  The Local channel will
		 * inherit this list so that if it ends up dialing a real interface,
		 * it won't call one that has already been called. */
		if (strcasecmp(tech, "Local")) {
			if (!(di = ast_calloc(1, sizeof(*di) + strlen(interface)))) {
				AST_LIST_UNLOCK(dialed_interfaces);
				chanlist_free(tmp);
				goto out;
			}
			strcpy(di->interface, interface);

			AST_LIST_LOCK(dialed_interfaces);
			AST_LIST_INSERT_TAIL(dialed_interfaces, di, list);
			AST_LIST_UNLOCK(dialed_interfaces);
		}

		tc = ast_request(tech, chan->nativeformats, chan, numsubst, &cause);
		if (!tc) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_WARNING, "Unable to create channel of type '%s' (cause %d - %s)\n",
				tech, cause, ast_cause2str(cause));
			handle_cause(cause, &num);
			if (!rest) /* we are on the last destination */
				chan->hangupcause = cause;
			chanlist_free(tmp);
			if (!ignore_cc && (cause == AST_CAUSE_BUSY || cause == AST_CAUSE_CONGESTION)) {
				if (!ast_cc_callback(chan, tech, numsubst, ast_cc_busy_interface)) {
					ast_cc_extension_monitor_add_dialstring(chan, interface, "");
				}
			}
			continue;
		}
		ast_channel_get_device_name(tc, device_name, sizeof(device_name));
		if (!ignore_cc) {
			ast_cc_extension_monitor_add_dialstring(chan, interface, device_name);
		}
		pbx_builtin_setvar_helper(tc, "DIALEDPEERNUMBER", numsubst);

		ast_channel_lock(tc);
		while (ast_channel_trylock(chan)) {
			CHANNEL_DEADLOCK_AVOIDANCE(tc);
		}
		/* Setup outgoing SDP to match incoming one */
		if (!outgoing && !rest && CAN_EARLY_BRIDGE(peerflags, chan, tc)) {
			ast_rtp_instance_early_bridge_make_compatible(tc, chan);
		}
		
		/* Inherit specially named variables from parent channel */
		ast_channel_inherit_variables(chan, tc);
		ast_channel_datastore_inherit(chan, tc);

		tc->appl = "AppDial";
		tc->data = "(Outgoing Line)";
		memset(&tc->whentohangup, 0, sizeof(tc->whentohangup));

		/* Determine CallerID to store in outgoing channel. */
		ast_party_caller_set_init(&caller, &tc->caller);
		if (ast_test_flag64(peerflags, OPT_ORIGINAL_CLID)) {
			caller.id = stored_clid;
			ast_channel_set_caller_event(tc, &caller, NULL);
			ast_set_flag64(tmp, DIAL_CALLERID_ABSENT);
		} else if (ast_strlen_zero(S_COR(tc->caller.id.number.valid,
			tc->caller.id.number.str, NULL))) {
			/*
			 * The new channel has no preset CallerID number by the channel
			 * driver.  Use the dialplan extension and hint name.
			 */
			caller.id = stored_clid;
			if (!caller.id.name.valid
				&& !ast_strlen_zero(S_COR(chan->connected.id.name.valid,
					chan->connected.id.name.str, NULL))) {
				/*
				 * No hint name available.  We have a connected name supplied by
				 * the dialplan we can use instead.
				 */
				caller.id.name.valid = 1;
				caller.id.name = chan->connected.id.name;
			}
			ast_channel_set_caller_event(tc, &caller, NULL);
			ast_set_flag64(tmp, DIAL_CALLERID_ABSENT);
		} else if (ast_strlen_zero(S_COR(tc->caller.id.name.valid, tc->caller.id.name.str,
			NULL))) {
			/* The new channel has no preset CallerID name by the channel driver. */
			if (!ast_strlen_zero(S_COR(chan->connected.id.name.valid,
				chan->connected.id.name.str, NULL))) {
				/*
				 * We have a connected name supplied by the dialplan we can
				 * use instead.
				 */
				caller.id.name.valid = 1;
				caller.id.name = chan->connected.id.name;
				ast_channel_set_caller_event(tc, &caller, NULL);
			}
		}

		/* Determine CallerID for outgoing channel to send. */
		if (ast_test_flag64(peerflags, OPT_FORCECLID) && !force_forwards_only) {
			struct ast_party_connected_line connected;

			ast_party_connected_line_set_init(&connected, &tc->connected);
			connected.id = forced_clid;
			ast_channel_set_connected_line(tc, &connected, NULL);
		} else {
			ast_connected_line_copy_from_caller(&tc->connected, &chan->caller);
		}

		ast_party_redirecting_copy(&tc->redirecting, &chan->redirecting);

		tc->dialed.transit_network_select = chan->dialed.transit_network_select;

		if (!ast_strlen_zero(chan->accountcode)) {
			ast_string_field_set(tc, peeraccount, chan->accountcode);
		}
		if (ast_strlen_zero(tc->musicclass))
			ast_string_field_set(tc, musicclass, chan->musicclass);

		/* Pass ADSI CPE and transfer capability */
		tc->adsicpe = chan->adsicpe;
		tc->transfercapability = chan->transfercapability;

		/* If we have an outbound group, set this peer channel to it */
		if (outbound_group)
			ast_app_group_set_channel(tc, outbound_group);
		/* If the calling channel has the ANSWERED_ELSEWHERE flag set, inherit it. This is to support local channels */
		if (ast_test_flag(chan, AST_FLAG_ANSWERED_ELSEWHERE))
			ast_set_flag(tc, AST_FLAG_ANSWERED_ELSEWHERE);

		/* Check if we're forced by configuration */
		if (ast_test_flag64(&opts, OPT_CANCEL_ELSEWHERE))
			 ast_set_flag(tc, AST_FLAG_ANSWERED_ELSEWHERE);


		/* Inherit context and extension */
		ast_string_field_set(tc, dialcontext, ast_strlen_zero(chan->macrocontext) ? chan->context : chan->macrocontext);
		if (!ast_strlen_zero(chan->macroexten))
			ast_copy_string(tc->exten, chan->macroexten, sizeof(tc->exten));
		else
			ast_copy_string(tc->exten, chan->exten, sizeof(tc->exten));

		ast_channel_unlock(tc);
		res = ast_call(tc, numsubst, 0); /* Place the call, but don't wait on the answer */

		/* Save the info in cdr's that we called them */
		if (chan->cdr)
			ast_cdr_setdestchan(chan->cdr, tc->name);

		/* check the results of ast_call */
		if (res) {
			/* Again, keep going even if there's an error */
			ast_debug(1, "ast call on peer returned %d\n", res);
			ast_verb(3, "Couldn't call %s/%s\n", tech, numsubst);
			if (tc->hangupcause) {
				chan->hangupcause = tc->hangupcause;
			}
			ast_channel_unlock(chan);
			ast_cc_call_failed(chan, tc, interface);
			ast_hangup(tc);
			tc = NULL;
			chanlist_free(tmp);
			continue;
		} else {
			senddialevent(chan, tc, numsubst);
			ast_verb(3, "Called %s/%s\n", tech, numsubst);
			ast_channel_unlock(chan);
		}
		/* Put them in the list of outgoing thingies...  We're ready now.
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		ast_set_flag64(tmp, DIAL_STILLGOING);
		tmp->chan = tc;
		tmp->next = outgoing;
		outgoing = tmp;
		/* If this line is up, don't try anybody else */
		if (outgoing->chan->_state == AST_STATE_UP)
			break;
	}
	
	if (ast_strlen_zero(args.timeout)) {
		to = -1;
	} else {
		to = atoi(args.timeout);
		if (to > 0)
			to *= 1000;
		else {
			ast_log(LOG_WARNING, "Invalid timeout specified: '%s'. Setting timeout to infinite\n", args.timeout);
			to = -1;
		}
	}

	if (!outgoing) {
		strcpy(pa.status, "CHANUNAVAIL");
		if (fulldial == num_dialed) {
			res = -1;
			goto out;
		}
	} else {
		/* Our status will at least be NOANSWER */
		strcpy(pa.status, "NOANSWER");
		if (ast_test_flag64(outgoing, OPT_MUSICBACK)) {
			moh = 1;
			if (!ast_strlen_zero(opt_args[OPT_ARG_MUSICBACK])) {
				char *original_moh = ast_strdupa(chan->musicclass);
				ast_string_field_set(chan, musicclass, opt_args[OPT_ARG_MUSICBACK]);
				ast_moh_start(chan, opt_args[OPT_ARG_MUSICBACK], NULL);
				ast_string_field_set(chan, musicclass, original_moh);
			} else {
				ast_moh_start(chan, NULL, NULL);
			}
			ast_indicate(chan, AST_CONTROL_PROGRESS);
		} else if (ast_test_flag64(outgoing, OPT_RINGBACK)) {
			if (!ast_strlen_zero(opt_args[OPT_ARG_RINGBACK])) {
				if (dial_handle_playtones(chan, opt_args[OPT_ARG_RINGBACK])){
					ast_indicate(chan, AST_CONTROL_RINGING);
					sentringing++;
				} else {
					ast_indicate(chan, AST_CONTROL_PROGRESS);
				}
			} else {
				ast_indicate(chan, AST_CONTROL_RINGING);
				sentringing++;
			}
		}
	}

	peer = wait_for_answer(chan, outgoing, &to, peerflags, opt_args, &pa, &num, &result,
		dtmf_progress, ignore_cc, &forced_clid, &stored_clid);

	/* The ast_channel_datastore_remove() function could fail here if the
	 * datastore was moved to another channel during a masquerade. If this is
	 * the case, don't free the datastore here because later, when the channel
	 * to which the datastore was moved hangs up, it will attempt to free this
	 * datastore again, causing a crash
	 */
	ast_channel_lock(chan);
	if (!ast_channel_datastore_remove(chan, datastore)) {
		ast_datastore_free(datastore);
	}
	ast_channel_unlock(chan);
	if (!peer) {
		if (result) {
			res = result;
		} else if (to) { /* Musta gotten hung up */
			res = -1;
		} else { /* Nobody answered, next please? */
			res = 0;
		}

		/* SIP, in particular, sends back this error code to indicate an
		 * overlap dialled number needs more digits. */
		if (chan->hangupcause == AST_CAUSE_INVALID_NUMBER_FORMAT) {
			res = AST_PBX_INCOMPLETE;
		}

		/* almost done, although the 'else' block is 400 lines */
	} else {
		const char *number;

		if (ast_test_flag64(&opts, OPT_CALLER_ANSWER))
			ast_answer(chan);

		strcpy(pa.status, "ANSWER");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the
		   conversation.  */
		hanguptree(outgoing, peer, 1);
		outgoing = NULL;
		/* If appropriate, log that we have a destination channel and set the answer time */
		if (chan->cdr) {
			ast_cdr_setdestchan(chan->cdr, peer->name);
			ast_cdr_setanswer(chan->cdr, peer->cdr->answer);
		}
		if (peer->name)
			pbx_builtin_setvar_helper(chan, "DIALEDPEERNAME", peer->name);
		
		ast_channel_lock(peer);
		number = pbx_builtin_getvar_helper(peer, "DIALEDPEERNUMBER"); 
		if (!number)
			number = numsubst;
		pbx_builtin_setvar_helper(chan, "DIALEDPEERNUMBER", number);
		ast_channel_unlock(peer);

		if (!ast_strlen_zero(args.url) && ast_channel_supports_html(peer) ) {
			ast_debug(1, "app_dial: sendurl=%s.\n", args.url);
			ast_channel_sendurl( peer, args.url );
		}
		if ( (ast_test_flag64(&opts, OPT_PRIVACY) || ast_test_flag64(&opts, OPT_SCREENING)) && pa.privdb_val == AST_PRIVACY_UNKNOWN) {
			if (do_privacy(chan, peer, &opts, opt_args, &pa)) {
				res = 0;
				goto out;
			}
		}
		if (!ast_test_flag64(&opts, OPT_ANNOUNCE) || ast_strlen_zero(opt_args[OPT_ARG_ANNOUNCE])) {
			res = 0;
		} else {
			int digit = 0;
			struct ast_channel *chans[2];
			struct ast_channel *active_chan;

			chans[0] = chan;
			chans[1] = peer;

			/* we need to stream the announcment while monitoring the caller for a hangup */

			/* stream the file */
			res = ast_streamfile(peer, opt_args[OPT_ARG_ANNOUNCE], peer->language);
			if (res) {
				res = 0;
				ast_log(LOG_ERROR, "error streaming file '%s' to callee\n", opt_args[OPT_ARG_ANNOUNCE]);
			}

			ast_set_flag(peer, AST_FLAG_END_DTMF_ONLY);
			while (peer->stream) {
				int ms;

				ms = ast_sched_wait(peer->sched);

				if (ms < 0 && !peer->timingfunc) {
					ast_stopstream(peer);
					break;
				}
				if (ms < 0)
					ms = 1000;

				active_chan = ast_waitfor_n(chans, 2, &ms);
				if (active_chan) {
					struct ast_frame *fr = ast_read(active_chan);
					if (!fr) {
						ast_hangup(peer);
						res = -1;
						goto done;
					}
					switch(fr->frametype) {
						case AST_FRAME_DTMF_END:
							digit = fr->subclass.integer;
							if (active_chan == peer && strchr(AST_DIGIT_ANY, res)) {
								ast_stopstream(peer);
								res = ast_senddigit(chan, digit, 0);
							}
							break;
						case AST_FRAME_CONTROL:
							switch (fr->subclass.integer) {
								case AST_CONTROL_HANGUP:
									ast_frfree(fr);
									ast_hangup(peer);
									res = -1;
									goto done;
								default:
									break;
							}
							break;
						default:
							/* Ignore all others */
							break;
					}
					ast_frfree(fr);
				}
				ast_sched_runq(peer->sched);
			}
			ast_clear_flag(peer, AST_FLAG_END_DTMF_ONLY);
		}

		if (chan && peer && ast_test_flag64(&opts, OPT_GOTO) && !ast_strlen_zero(opt_args[OPT_ARG_GOTO])) {
			/* chan and peer are going into the PBX, they both
			 * should probably get CDR records. */
			ast_clear_flag(chan->cdr, AST_CDR_FLAG_DIALED);
			ast_clear_flag(peer->cdr, AST_CDR_FLAG_DIALED);

			replace_macro_delimiter(opt_args[OPT_ARG_GOTO]);
			ast_parseable_goto(chan, opt_args[OPT_ARG_GOTO]);
			/* peer goes to the same context and extension as chan, so just copy info from chan*/
			ast_copy_string(peer->context, chan->context, sizeof(peer->context));
			ast_copy_string(peer->exten, chan->exten, sizeof(peer->exten));
			peer->priority = chan->priority + 2;
			ast_pbx_start(peer);
			hanguptree(outgoing, NULL, ast_test_flag64(&opts, OPT_CANCEL_ELSEWHERE) ? 1 : 0);
			if (continue_exec)
				*continue_exec = 1;
			res = 0;
			goto done;
		}

		if (ast_test_flag64(&opts, OPT_CALLEE_MACRO) && !ast_strlen_zero(opt_args[OPT_ARG_CALLEE_MACRO])) {
			struct ast_app *theapp;
			const char *macro_result;

			res = ast_autoservice_start(chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}

			theapp = pbx_findapp("Macro");

			if (theapp && !res) { /* XXX why check res here ? */
				/* Set peer->exten and peer->context so that MACRO_EXTEN and MACRO_CONTEXT get set */
				ast_copy_string(peer->context, chan->context, sizeof(peer->context));
				ast_copy_string(peer->exten, chan->exten, sizeof(peer->exten));

				replace_macro_delimiter(opt_args[OPT_ARG_CALLEE_MACRO]);
				res = pbx_exec(peer, theapp, opt_args[OPT_ARG_CALLEE_MACRO]);
				ast_debug(1, "Macro exited with status %d\n", res);
				res = 0;
			} else {
				ast_log(LOG_ERROR, "Could not find application Macro\n");
				res = -1;
			}

			if (ast_autoservice_stop(chan) < 0) {
				res = -1;
			}

			ast_channel_lock(peer);

			if (!res && (macro_result = pbx_builtin_getvar_helper(peer, "MACRO_RESULT"))) {
				char *macro_transfer_dest;

				if (!strcasecmp(macro_result, "BUSY")) {
					ast_copy_string(pa.status, macro_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(macro_result, "CONGESTION") || !strcasecmp(macro_result, "CHANUNAVAIL")) {
					ast_copy_string(pa.status, macro_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(macro_result, "CONTINUE")) {
					/* hangup peer and keep chan alive assuming the macro has changed
					   the context / exten / priority or perhaps
					   the next priority in the current exten is desired.
					*/
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(macro_result, "ABORT")) {
					/* Hangup both ends unless the caller has the g flag */
					res = -1;
				} else if (!strncasecmp(macro_result, "GOTO:", 5) && (macro_transfer_dest = ast_strdupa(macro_result + 5))) {
					res = -1;
					/* perform a transfer to a new extension */
					if (strchr(macro_transfer_dest, '^')) { /* context^exten^priority*/
						replace_macro_delimiter(macro_transfer_dest);
						if (!ast_parseable_goto(chan, macro_transfer_dest))
							ast_set_flag64(peerflags, OPT_GO_ON);
					}
				}
			}

			ast_channel_unlock(peer);
		}

		if (ast_test_flag64(&opts, OPT_CALLEE_GOSUB) && !ast_strlen_zero(opt_args[OPT_ARG_CALLEE_GOSUB])) {
			struct ast_app *theapp;
			const char *gosub_result;
			char *gosub_args, *gosub_argstart;
			int res9 = -1;

			res9 = ast_autoservice_start(chan);
			if (res9) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res9 = -1;
			}

			theapp = pbx_findapp("Gosub");

			if (theapp && !res9) {
				replace_macro_delimiter(opt_args[OPT_ARG_CALLEE_GOSUB]);

				/* Set where we came from */
				ast_copy_string(peer->context, "app_dial_gosub_virtual_context", sizeof(peer->context));
				ast_copy_string(peer->exten, "s", sizeof(peer->exten));
				peer->priority = 0;

				gosub_argstart = strchr(opt_args[OPT_ARG_CALLEE_GOSUB], ',');
				if (gosub_argstart) {
					const char *what_is_s = "s";
					*gosub_argstart = 0;
					if (!ast_exists_extension(peer, opt_args[OPT_ARG_CALLEE_GOSUB], "s", 1, S_COR(peer->caller.id.number.valid, peer->caller.id.number.str, NULL)) &&
						 ast_exists_extension(peer, opt_args[OPT_ARG_CALLEE_GOSUB], "~~s~~", 1, S_COR(peer->caller.id.number.valid, peer->caller.id.number.str, NULL))) {
						what_is_s = "~~s~~";
					}
					if (asprintf(&gosub_args, "%s,%s,1(%s)", opt_args[OPT_ARG_CALLEE_GOSUB], what_is_s, gosub_argstart + 1) < 0) {
						ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
						gosub_args = NULL;
					}
					*gosub_argstart = ',';
				} else {
					const char *what_is_s = "s";
					if (!ast_exists_extension(peer, opt_args[OPT_ARG_CALLEE_GOSUB], "s", 1, S_COR(peer->caller.id.number.valid, peer->caller.id.number.str, NULL)) &&
						 ast_exists_extension(peer, opt_args[OPT_ARG_CALLEE_GOSUB], "~~s~~", 1, S_COR(peer->caller.id.number.valid, peer->caller.id.number.str, NULL))) {
						what_is_s = "~~s~~";
					}
					if (asprintf(&gosub_args, "%s,%s,1", opt_args[OPT_ARG_CALLEE_GOSUB], what_is_s) < 0) {
						ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
						gosub_args = NULL;
					}
				}

				if (gosub_args) {
					res9 = pbx_exec(peer, theapp, gosub_args);
					if (!res9) {
						struct ast_pbx_args args;
						/* A struct initializer fails to compile for this case ... */
						memset(&args, 0, sizeof(args));
						args.no_hangup_chan = 1;
						ast_pbx_run_args(peer, &args);
					}
					ast_free(gosub_args);
					ast_debug(1, "Gosub exited with status %d\n", res9);
				} else {
					ast_log(LOG_ERROR, "Could not Allocate string for Gosub arguments -- Gosub Call Aborted!\n");
				}

			} else if (!res9) {
				ast_log(LOG_ERROR, "Could not find application Gosub\n");
				res9 = -1;
			}

			if (ast_autoservice_stop(chan) < 0) {
				ast_log(LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res9 = -1;
			}
			
			ast_channel_lock(peer);

			if (!res9 && (gosub_result = pbx_builtin_getvar_helper(peer, "GOSUB_RESULT"))) {
				char *gosub_transfer_dest;
				const char *gosub_retval = pbx_builtin_getvar_helper(peer, "GOSUB_RETVAL");

				/* Inherit return value from the peer, so it can be used in the master */
				if (gosub_retval) {
					pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", gosub_retval);
				}

				if (!strcasecmp(gosub_result, "BUSY")) {
					ast_copy_string(pa.status, gosub_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(gosub_result, "CONGESTION") || !strcasecmp(gosub_result, "CHANUNAVAIL")) {
					ast_copy_string(pa.status, gosub_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(gosub_result, "CONTINUE")) {
					/* hangup peer and keep chan alive assuming the macro has changed
					   the context / exten / priority or perhaps
					   the next priority in the current exten is desired.
					*/
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(gosub_result, "ABORT")) {
					/* Hangup both ends unless the caller has the g flag */
					res = -1;
				} else if (!strncasecmp(gosub_result, "GOTO:", 5) && (gosub_transfer_dest = ast_strdupa(gosub_result + 5))) {
					res = -1;
					/* perform a transfer to a new extension */
					if (strchr(gosub_transfer_dest, '^')) { /* context^exten^priority*/
						replace_macro_delimiter(gosub_transfer_dest);
						if (!ast_parseable_goto(chan, gosub_transfer_dest))
							ast_set_flag64(peerflags, OPT_GO_ON);
					}
				}
			}

			ast_channel_unlock(peer);	
		}

		if (!res) {
			if (!ast_tvzero(calldurationlimit)) {
				struct timeval whentohangup = calldurationlimit;
				peer->whentohangup = ast_tvadd(ast_tvnow(), whentohangup);
			}
			if (!ast_strlen_zero(dtmfcalled)) {
				ast_verb(3, "Sending DTMF '%s' to the called party.\n", dtmfcalled);
				res = ast_dtmf_stream(peer, chan, dtmfcalled, 250, 0);
			}
			if (!ast_strlen_zero(dtmfcalling)) {
				ast_verb(3, "Sending DTMF '%s' to the calling party.\n", dtmfcalling);
				res = ast_dtmf_stream(chan, peer, dtmfcalling, 250, 0);
			}
		}

		if (res) { /* some error */
			res = -1;
		} else {
			if (ast_test_flag64(peerflags, OPT_CALLEE_TRANSFER))
				ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
			if (ast_test_flag64(peerflags, OPT_CALLER_TRANSFER))
				ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
			if (ast_test_flag64(peerflags, OPT_CALLEE_HANGUP))
				ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
			if (ast_test_flag64(peerflags, OPT_CALLER_HANGUP))
				ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
			if (ast_test_flag64(peerflags, OPT_CALLEE_MONITOR))
				ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
			if (ast_test_flag64(peerflags, OPT_CALLER_MONITOR))
				ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
			if (ast_test_flag64(peerflags, OPT_CALLEE_PARK))
				ast_set_flag(&(config.features_callee), AST_FEATURE_PARKCALL);
			if (ast_test_flag64(peerflags, OPT_CALLER_PARK))
				ast_set_flag(&(config.features_caller), AST_FEATURE_PARKCALL);
			if (ast_test_flag64(peerflags, OPT_CALLEE_MIXMONITOR))
				ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMIXMON);
			if (ast_test_flag64(peerflags, OPT_CALLER_MIXMONITOR))
				ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMIXMON);
			if (ast_test_flag64(peerflags, OPT_GO_ON))
				ast_set_flag(&(config.features_caller), AST_FEATURE_NO_H_EXTEN);

			config.end_bridge_callback = end_bridge_callback;
			config.end_bridge_callback_data = chan;
			config.end_bridge_callback_data_fixup = end_bridge_callback_data_fixup;
			
			if (moh) {
				moh = 0;
				ast_moh_stop(chan);
			} else if (sentringing) {
				sentringing = 0;
				ast_indicate(chan, -1);
			}
			/* Be sure no generators are left on it and reset the visible indication */
			ast_deactivate_generator(chan);
			chan->visible_indication = 0;
			/* Make sure channels are compatible */
			res = ast_channel_make_compatible(chan, peer);
			if (res < 0) {
				ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", chan->name, peer->name);
				ast_hangup(peer);
				res = -1;
				goto done;
			}
			if (opermode) {
				struct oprmode oprmode;

				oprmode.peer = peer;
				oprmode.mode = opermode;

				ast_channel_setoption(chan, AST_OPTION_OPRMODE, &oprmode, sizeof(oprmode), 0);
			}
			res = ast_bridge_call(chan, peer, &config);
		}

		strcpy(peer->context, chan->context);

		if (ast_test_flag64(&opts, OPT_PEER_H)
			&& ast_exists_extension(peer, peer->context, "h", 1,
				S_COR(peer->caller.id.number.valid, peer->caller.id.number.str, NULL))) {
			int autoloopflag;
			int found;
			int res9;
			
			strcpy(peer->exten, "h");
			peer->priority = 1;
			autoloopflag = ast_test_flag(peer, AST_FLAG_IN_AUTOLOOP); /* save value to restore at the end */
			ast_set_flag(peer, AST_FLAG_IN_AUTOLOOP);

			while ((res9 = ast_spawn_extension(peer, peer->context, peer->exten,
				peer->priority,
				S_COR(peer->caller.id.number.valid, peer->caller.id.number.str, NULL),
				&found, 1)) == 0) {
				peer->priority++;
			}

			if (found && res9) {
				/* Something bad happened, or a hangup has been requested. */
				ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", peer->context, peer->exten, peer->priority, peer->name);
				ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", peer->context, peer->exten, peer->priority, peer->name);
			}
			ast_set2_flag(peer, autoloopflag, AST_FLAG_IN_AUTOLOOP);  /* set it back the way it was */
		}
		if (!ast_check_hangup(peer) && ast_test_flag64(&opts, OPT_CALLEE_GO_ON)) {
			if(!ast_strlen_zero(opt_args[OPT_ARG_CALLEE_GO_ON])) {
				replace_macro_delimiter(opt_args[OPT_ARG_CALLEE_GO_ON]);
				ast_parseable_goto(peer, opt_args[OPT_ARG_CALLEE_GO_ON]);
			} else { /* F() */
				int res;
				res = ast_goto_if_exists(peer, chan->context, chan->exten, (chan->priority) + 1); 
				if (res == AST_PBX_GOTO_FAILED) {
					ast_hangup(peer);
					goto out;
				}
			}
			ast_pbx_start(peer);
		} else {
			if (!ast_check_hangup(chan))
				chan->hangupcause = peer->hangupcause;
			ast_hangup(peer);
		}
	}
out:
	if (moh) {
		moh = 0;
		ast_moh_stop(chan);
	} else if (sentringing) {
		sentringing = 0;
		ast_indicate(chan, -1);
	}

	if (delprivintro && ast_fileexists(pa.privintro, NULL, NULL) > 0) {
		ast_filedelete(pa.privintro, NULL);
		if (ast_fileexists(pa.privintro, NULL, NULL) > 0) {
			ast_log(LOG_NOTICE, "privacy: ast_filedelete didn't do its job on %s\n", pa.privintro);
		} else {
			ast_verb(3, "Successfully deleted %s intro file\n", pa.privintro);
		}
	}

	ast_channel_early_bridge(chan, NULL);
	hanguptree(outgoing, NULL, 0); /* In this case, there's no answer anywhere */
	pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
	senddialendevent(chan, pa.status);
	ast_debug(1, "Exiting with DIALSTATUS=%s.\n", pa.status);
	
	if ((ast_test_flag64(peerflags, OPT_GO_ON)) && !ast_check_hangup(chan) && (res != AST_PBX_INCOMPLETE)) {
		if (!ast_tvzero(calldurationlimit))
			memset(&chan->whentohangup, 0, sizeof(chan->whentohangup));
		res = 0;
	}

done:
	if (config.warning_sound) {
		ast_free((char *)config.warning_sound);
	}
	if (config.end_sound) {
		ast_free((char *)config.end_sound);
	}
	if (config.start_sound) {
		ast_free((char *)config.start_sound);
	}
	ast_ignore_cc(chan);
	return res;
}

static int dial_exec(struct ast_channel *chan, const char *data)
{
	struct ast_flags64 peerflags;

	memset(&peerflags, 0, sizeof(peerflags));

	return dial_exec_full(chan, data, &peerflags, NULL);
}

static int retrydial_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	const char *context = NULL;
	int sleepms = 0, loops = 0, res = -1;
	struct ast_flags64 peerflags = { 0, };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(announce);
		AST_APP_ARG(sleep);
		AST_APP_ARG(retries);
		AST_APP_ARG(dialdata);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RetryDial requires an argument!\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.sleep) && (sleepms = atoi(args.sleep)))
		sleepms *= 1000;

	if (!ast_strlen_zero(args.retries)) {
		loops = atoi(args.retries);
	}

	if (!args.dialdata) {
		ast_log(LOG_ERROR, "%s requires a 4th argument (dialdata)\n", rapp);
		goto done;
	}

	if (sleepms < 1000)
		sleepms = 10000;

	if (!loops)
		loops = -1; /* run forever */

	ast_channel_lock(chan);
	context = pbx_builtin_getvar_helper(chan, "EXITCONTEXT");
	context = !ast_strlen_zero(context) ? ast_strdupa(context) : NULL;
	ast_channel_unlock(chan);

	res = 0;
	while (loops) {
		int continue_exec;

		chan->data = "Retrying";
		if (ast_test_flag(chan, AST_FLAG_MOH))
			ast_moh_stop(chan);

		res = dial_exec_full(chan, args.dialdata, &peerflags, &continue_exec);
		if (continue_exec)
			break;

		if (res == 0) {
			if (ast_test_flag64(&peerflags, OPT_DTMF_EXIT)) {
				if (!ast_strlen_zero(args.announce)) {
					if (ast_fileexists(args.announce, NULL, chan->language) > 0) {
						if (!(res = ast_streamfile(chan, args.announce, chan->language)))
							ast_waitstream(chan, AST_DIGIT_ANY);
					} else
						ast_log(LOG_WARNING, "Announce file \"%s\" specified in Retrydial does not exist\n", args.announce);
				}
				if (!res && sleepms) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL, NULL);
					res = ast_waitfordigit(chan, sleepms);
				}
			} else {
				if (!ast_strlen_zero(args.announce)) {
					if (ast_fileexists(args.announce, NULL, chan->language) > 0) {
						if (!(res = ast_streamfile(chan, args.announce, chan->language)))
							res = ast_waitstream(chan, "");
					} else
						ast_log(LOG_WARNING, "Announce file \"%s\" specified in Retrydial does not exist\n", args.announce);
				}
				if (sleepms) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL, NULL);
					if (!res)
						res = ast_waitfordigit(chan, sleepms);
				}
			}
		}

		if (res < 0 || res == AST_PBX_INCOMPLETE) {
			break;
		} else if (res > 0) { /* Trying to send the call elsewhere (1 digit ext) */
			if (onedigit_goto(chan, context, (char) res, 1)) {
				res = 0;
				break;
			}
		}
		loops--;
	}
	if (loops == 0)
		res = 0;
	else if (res == 1)
		res = 0;

	if (ast_test_flag(chan, AST_FLAG_MOH))
		ast_moh_stop(chan);
 done:
	return res;
}

static int unload_module(void)
{
	int res;
	struct ast_context *con;

	res = ast_unregister_application(app);
	res |= ast_unregister_application(rapp);

	if ((con = ast_context_find("app_dial_gosub_virtual_context"))) {
		ast_context_remove_extension2(con, "s", 1, NULL, 0);
		ast_context_destroy(con, "app_dial"); /* leave nothing behind */
	}

	return res;
}

static int load_module(void)
{
	int res;
	struct ast_context *con;

	con = ast_context_find_or_create(NULL, NULL, "app_dial_gosub_virtual_context", "app_dial");
	if (!con)
		ast_log(LOG_ERROR, "Dial virtual context 'app_dial_gosub_virtual_context' does not exist and unable to create\n");
	else
		ast_add_extension2(con, 1, "s", 1, NULL, NULL, "NoOp", ast_strdup(""), ast_free_ptr, "app_dial");

	res = ast_register_application_xml(app, dial_exec);
	res |= ast_register_application_xml(rapp, retrydial_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialing Application");
