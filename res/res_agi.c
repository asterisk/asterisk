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
 * \brief AGI - the Asterisk Gateway Interface
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \todo Convert the rest of the AGI commands over to XML documentation
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>

#include "asterisk/paths.h"	/* use many ast_config_AST_*_DIR */
#include "asterisk/network.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/image.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/musiconhold.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/manager.h"
#include "asterisk/ast_version.h"
#include "asterisk/speech.h"
#include "asterisk/manager.h"
#include "asterisk/term.h"
#include "asterisk/xmldoc.h"
#include "asterisk/srv.h"
#include "asterisk/test.h"
#include "asterisk/netsock2.h"

#define AST_API_MODULE
#include "asterisk/agi.h"

/*** DOCUMENTATION
	<agi name="answer" language="en_US">
		<synopsis>
			Answer channel
		</synopsis>
		<syntax />
		<description>
			<para>Answers channel if not already in answer state. Returns <literal>-1</literal> on
			channel failure, or <literal>0</literal> if successful.</para>
		</description>
		<see-also>
			<ref type="agi">hangup</ref>
		</see-also>
	</agi>
	<agi name="asyncagi break" language="en_US">
		<synopsis>
			Interrupts Async AGI
		</synopsis>
		<syntax />
		<description>
			<para>Interrupts expected flow of Async AGI commands and returns control to previous source
			(typically, the PBX dialplan).</para>
		</description>
		<see-also>
			<ref type="agi">hangup</ref>
		</see-also>
	</agi>
	<agi name="channel status" language="en_US">
		<synopsis>
			Returns status of the connected channel.
		</synopsis>
		<syntax>
			<parameter name="channelname" />
		</syntax>
		<description>
			<para>Returns the status of the specified <replaceable>channelname</replaceable>.
			If no channel name is given then returns the status of the current channel.</para>
			<para>Return values:</para>
			<enumlist>
				<enum name="0">
					<para>Channel is down and available.</para>
				</enum>
				<enum name="1">
					<para>Channel is down, but reserved.</para>
				</enum>
				<enum name="2">
					<para>Channel is off hook.</para>
				</enum>
				<enum name="3">
					<para>Digits (or equivalent) have been dialed.</para>
				</enum>
				<enum name="4">
					<para>Line is ringing.</para>
				</enum>
				<enum name="5">
					<para>Remote end is ringing.</para>
				</enum>
				<enum name="6">
					<para>Line is up.</para>
				</enum>
				<enum name="7">
					<para>Line is busy.</para>
				</enum>
			</enumlist>
		</description>
	</agi>
	<agi name="control stream file" language="en_US">
		<synopsis>
			Sends audio file on channel and allows the listener to control the stream.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true">
				<para>The file extension must not be included in the filename.</para>
			</parameter>
			<parameter name="escape_digits" required="true" />
			<parameter name="skipms" />
			<parameter name="ffchar">
				<para>Defaults to <literal>#</literal></para>
			</parameter>
			<parameter name="rewchr">
				<para>Defaults to <literal>*</literal></para>
			</parameter>
			<parameter name="pausechr" />
		</syntax>
		<description>
			<para>Send the given file, allowing playback to be controlled by the given
			digits, if any. Use double quotes for the digits if you wish none to be
			permitted. Returns <literal>0</literal> if playback completes without a digit
			being pressed, or the ASCII numerical value of the digit if one was pressed,
			or <literal>-1</literal> on error or if the channel was disconnected.</para>
		</description>
	</agi>
	<agi name="database del" language="en_US">
		<synopsis>
			Removes database key/value
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>Deletes an entry in the Asterisk database for a given
			<replaceable>family</replaceable> and <replaceable>key</replaceable>.</para>
			<para>Returns <literal>1</literal> if successful, <literal>0</literal>
			otherwise.</para>
		</description>
	</agi>
	<agi name="database deltree" language="en_US">
		<synopsis>
			Removes database keytree/value
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="keytree" />
		</syntax>
		<description>
			<para>Deletes a <replaceable>family</replaceable> or specific <replaceable>keytree</replaceable>
			within a <replaceable>family</replaceable> in the Asterisk database.</para>
			<para>Returns <literal>1</literal> if successful, <literal>0</literal> otherwise.</para>
		</description>
	</agi>
	<agi name="database get" language="en_US">
		<synopsis>
			Gets database value
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>Retrieves an entry in the Asterisk database for a given <replaceable>family</replaceable>
			and <replaceable>key</replaceable>.</para>
			<para>Returns <literal>0</literal> if <replaceable>key</replaceable> is not set.
			Returns <literal>1</literal> if <replaceable>key</replaceable> is set and returns the variable
			in parenthesis.</para>
			<para>Example return code: 200 result=1 (testvariable)</para>
		</description>
	</agi>
	<agi name="database put" language="en_US">
		<synopsis>
			Adds/updates database value
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="key" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>Adds or updates an entry in the Asterisk database for a given
			<replaceable>family</replaceable>, <replaceable>key</replaceable>, and
			<replaceable>value</replaceable>.</para>
			<para>Returns <literal>1</literal> if successful, <literal>0</literal> otherwise.</para>
		</description>
	</agi>
	<agi name="exec" language="en_US">
		<synopsis>
			Executes a given Application
		</synopsis>
		<syntax>
			<parameter name="application" required="true" />
			<parameter name="options" required="true" />
		</syntax>
		<description>
			<para>Executes <replaceable>application</replaceable> with given
			<replaceable>options</replaceable>.</para>
			<para>Returns whatever the <replaceable>application</replaceable> returns, or
			<literal>-2</literal> on failure to find <replaceable>application</replaceable>.</para>
		</description>
	</agi>
	<agi name="get data" language="en_US">
		<synopsis>
			Prompts for DTMF on a channel
		</synopsis>
		<syntax>
			<parameter name="file" required="true" />
			<parameter name="timeout" />
			<parameter name="maxdigits" />
		</syntax>
		<description>
			<para>Stream the given <replaceable>file</replaceable>, and receive DTMF data.</para>
			<para>Returns the digits received from the channel at the other end.</para>
		</description>
	</agi>
	<agi name="get full variable" language="en_US">
		<synopsis>
			Evaluates a channel expression
		</synopsis>
		<syntax>
			<parameter name="variablename" required="true" />
			<parameter name="channel name" />
		</syntax>
		<description>
			<para>Returns <literal>0</literal> if <replaceable>variablename</replaceable> is not set
			or channel does not exist. Returns <literal>1</literal> if <replaceable>variablename</replaceable>
			is set and returns the variable in parenthesis. Understands complex variable names and builtin
			variables, unlike GET VARIABLE.</para>
			<para>Example return code: 200 result=1 (testvariable)</para>
		</description>
	</agi>
	<agi name="get option" language="en_US">
		<synopsis>
			Stream file, prompt for DTMF, with timeout.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
			<parameter name="escape_digits" required="true" />
			<parameter name="timeout" />
		</syntax>
		<description>
			<para>Behaves similar to STREAM FILE but used with a timeout option.</para>
		</description>
		<see-also>
			<ref type="agi">stream file</ref>
		</see-also>
	</agi>
	<agi name="get variable" language="en_US">
		<synopsis>
			Gets a channel variable.
		</synopsis>
		<syntax>
			<parameter name="variablename" required="true" />
		</syntax>
		<description>
			<para>Returns <literal>0</literal> if <replaceable>variablename</replaceable> is not set.
			Returns <literal>1</literal> if <replaceable>variablename</replaceable> is set and returns
			the variable in parentheses.</para>
			<para>Example return code: 200 result=1 (testvariable)</para>
		</description>
	</agi>
	<agi name="hangup" language="en_US">
		<synopsis>
			Hangup a channel.
		</synopsis>
		<syntax>
			<parameter name="channelname" />
		</syntax>
		<description>
			<para>Hangs up the specified channel. If no channel name is given, hangs
			up the current channel</para>
		</description>
	</agi>
	<agi name="noop" language="en_US">
		<synopsis>
			Does nothing.
		</synopsis>
		<syntax />
		<description>
			<para>Does nothing.</para>
		</description>
	</agi>
	<agi name="receive char" language="en_US">
		<synopsis>
			Receives one character from channels supporting it.
		</synopsis>
		<syntax>
			<parameter name="timeout" required="true">
				<para>The maximum time to wait for input in milliseconds, or <literal>0</literal>
				for infinite. Most channels</para>
			</parameter>
		</syntax>
		<description>
			<para>Receives a character of text on a channel. Most channels do not support
			the reception of text. Returns the decimal value of the character
			if one is received, or <literal>0</literal> if the channel does not support
			text reception. Returns <literal>-1</literal> only on error/hangup.</para>
		</description>
	</agi>
	<agi name="receive text" language="en_US">
		<synopsis>
			Receives text from channels supporting it.
		</synopsis>
		<syntax>
			<parameter name="timeout" required="true">
				<para>The timeout to be the maximum time to wait for input in
				milliseconds, or <literal>0</literal> for infinite.</para>
			</parameter>
		</syntax>
		<description>
			<para>Receives a string of text on a channel. Most channels 
			do not support the reception of text. Returns <literal>-1</literal> for failure
			or <literal>1</literal> for success, and the string in parenthesis.</para> 
		</description>
	</agi>
	<agi name="record file" language="en_US">
		<synopsis>
			Records to a given file.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
			<parameter name="format" required="true" />
			<parameter name="escape_digits" required="true" />
			<parameter name="timeout" required="true" />
			<parameter name="offset samples" />
			<parameter name="BEEP" />
			<parameter name="s=silence" />
		</syntax>
		<description>
			<para>Record to a file until a given dtmf digit in the sequence is received.
			Returns <literal>-1</literal> on hangup or error.  The format will specify what kind of file
			will be recorded. The <replaceable>timeout</replaceable> is the maximum record time in
			milliseconds, or <literal>-1</literal> for no <replaceable>timeout</replaceable>.
			<replaceable>offset samples</replaceable> is optional, and, if provided, will seek
			to the offset without exceeding the end of the file. <replaceable>silence</replaceable> is
			the number of seconds of silence allowed before the function returns despite the
			lack of dtmf digits or reaching <replaceable>timeout</replaceable>. <replaceable>silence</replaceable>
			value must be preceded by <literal>s=</literal> and is also optional.</para>
		</description>
	</agi>
	<agi name="say alpha" language="en_US">
		<synopsis>
			Says a given character string.
		</synopsis>
		<syntax>
			<parameter name="number" required="true" />
			<parameter name="escape_digits" required="true" />
		</syntax>
		<description>
			<para>Say a given character string, returning early if any of the given DTMF digits
			are received on the channel. Returns <literal>0</literal> if playback completes
			without a digit being pressed, or the ASCII numerical value of the digit if one
			was pressed or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="say digits" language="en_US">
		<synopsis>
			Says a given digit string.
		</synopsis>
		<syntax>
			<parameter name="number" required="true" />
			<parameter name="escape_digits" required="true" />
		</syntax>
		<description>
			<para>Say a given digit string, returning early if any of the given DTMF digits
			are received on the channel. Returns <literal>0</literal> if playback completes
			without a digit being pressed, or the ASCII numerical value of the digit if one
			was pressed or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="say number" language="en_US">
		<synopsis>
			Says a given number.
		</synopsis>
		<syntax>
			<parameter name="number" required="true" />
			<parameter name="escape_digits" required="true" />
			<parameter name="gender" />
		</syntax>
		<description>
			<para>Say a given number, returning early if any of the given DTMF digits
			are received on the channel.  Returns <literal>0</literal> if playback
			completes without a digit being pressed, or the ASCII numerical value of
			the digit if one was pressed or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="say phonetic" language="en_US">
		<synopsis>
			Says a given character string with phonetics.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
			<parameter name="escape_digits" required="true" />
		</syntax>
		<description>
			<para>Say a given character string with phonetics, returning early if any of the
			given DTMF digits are received on the channel. Returns <literal>0</literal> if
			playback completes without a digit pressed, the ASCII numerical value of the digit
			if one was pressed, or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="say date" language="en_US">
		<synopsis>
			Says a given date.
		</synopsis>
		<syntax>
			<parameter name="date" required="true">
				<para>Is number of seconds elapsed since 00:00:00 on January 1, 1970.
				Coordinated Universal Time (UTC).</para>
			</parameter>
			<parameter name="escape_digits" required="true" />
		</syntax>
		<description>
			<para>Say a given date, returning early if any of the given DTMF digits are
			received on the channel. Returns <literal>0</literal> if playback
			completes without a digit being pressed, or the ASCII numerical value of the
			digit if one was pressed or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="say time" language="en_US">
		<synopsis>
			Says a given time.
		</synopsis>
		<syntax>
			<parameter name="time" required="true">
				<para>Is number of seconds elapsed since 00:00:00 on January 1, 1970.
				Coordinated Universal Time (UTC).</para>
			</parameter>
			<parameter name="escape_digits" required="true" />
		</syntax>
		<description>
			<para>Say a given time, returning early if any of the given DTMF digits are
			received on the channel. Returns <literal>0</literal> if playback completes
			without a digit being pressed, or the ASCII numerical value of the digit if
			one was pressed or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="say datetime" language="en_US">
		<synopsis>
			Says a given time as specified by the format given.
		</synopsis>
		<syntax>
			<parameter name="time" required="true">
				<para>Is number of seconds elapsed since 00:00:00
				on January 1, 1970, Coordinated Universal Time (UTC)</para>
			</parameter>
			<parameter name="escape_digits" required="true" />
			<parameter name="format">
				<para>Is the format the time should be said in. See
				<filename>voicemail.conf</filename> (defaults to <literal>ABdY
				'digits/at' IMp</literal>).</para>
			</parameter>
			<parameter name="timezone">
				<para>Acceptable values can be found in <filename>/usr/share/zoneinfo</filename>
				Defaults to machine default.</para>
			</parameter>
		</syntax>
		<description>
			<para>Say a given time, returning early if any of the given DTMF digits are
			received on the channel. Returns <literal>0</literal> if playback
			completes without a digit being pressed, or the ASCII numerical value of the
			digit if one was pressed or <literal>-1</literal> on error/hangup.</para>
		</description>
	</agi>
	<agi name="send image" language="en_US">
		<synopsis>
			Sends images to channels supporting it.
		</synopsis>
		<syntax>
			<parameter name="image" required="true" />
		</syntax>
		<description>
			<para>Sends the given image on a channel. Most channels do not support the
			transmission of images. Returns <literal>0</literal> if image is sent, or if
			the channel does not support image transmission.  Returns <literal>-1</literal>
			only on error/hangup. Image names should not include extensions.</para>
		</description>
	</agi>
	<agi name="send text" language="en_US">
		<synopsis>
			Sends text to channels supporting it.
		</synopsis>
		<syntax>
			<parameter name="text to send" required="true">
				<para>Text consisting of greater than one word should be placed
				in quotes since the command only accepts a single argument.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends the given text on a channel. Most channels do not support the
			transmission of text. Returns <literal>0</literal> if text is sent, or if the
			channel does not support text transmission. Returns <literal>-1</literal> only
			on error/hangup.</para>
		</description>
	</agi>
	<agi name="set autohangup" language="en_US">
		<synopsis>
			Autohangup channel in some time.
		</synopsis>
		<syntax>
			<parameter name="time" required="true" />
		</syntax>
		<description>
			<para>Cause the channel to automatically hangup at <replaceable>time</replaceable>
			seconds in the future. Of course it can be hungup before then as well. Setting to
			<literal>0</literal> will cause the autohangup feature to be disabled on this channel.</para>
		</description>
	</agi>
	<agi name="set callerid" language="en_US">
		<synopsis>
			Sets callerid for the current channel.
		</synopsis>
		<syntax>
			<parameter name="number" required="true" />
		</syntax>
		<description>
			<para>Changes the callerid of the current channel.</para>
		</description>
	</agi>
	<agi name="set context" language="en_US">
		<synopsis>
			Sets channel context.
		</synopsis>
		<syntax>
			<parameter name="desired context" required="true" />
		</syntax>
		<description>
			<para>Sets the context for continuation upon exiting the application.</para>
		</description>
	</agi>
	<agi name="set extension" language="en_US">
		<synopsis>
			Changes channel extension.
		</synopsis>
		<syntax>
			<parameter name="new extension" required="true" />
		</syntax>
		<description>
			<para>Changes the extension for continuation upon exiting the application.</para>
		</description>
	</agi>
	<agi name="set music" language="en_US">
		<synopsis>
			Enable/Disable Music on hold generator
		</synopsis>
		<syntax>
			<parameter required="true">
				<enumlist>
					<enum>
						<parameter name="on" literal="true" required="true" />
					</enum>
					<enum>
						<parameter name="off" literal="true" required="true" />
					</enum>
				</enumlist>
			</parameter>
			<parameter name="class" required="true" />
		</syntax>
		<description>
			<para>Enables/Disables the music on hold generator. If <replaceable>class</replaceable>
			is not specified, then the <literal>default</literal> music on hold class will be
			used. This generator will be stopped automatically when playing a file.</para>
			<para>Always returns <literal>0</literal>.</para>
		</description>
	</agi>
	<agi name="set priority" language="en_US">
		<synopsis>
			Set channel dialplan priority.
		</synopsis>
		<syntax>
			<parameter name="priority" required="true" />
		</syntax>
		<description>
			<para>Changes the priority for continuation upon exiting the application.
			The priority must be a valid priority or label.</para>
		</description>
	</agi>
	<agi name="set variable" language="en_US">
		<synopsis>
			Sets a channel variable.
		</synopsis>
		<syntax>
			<parameter name="variablename" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>Sets a variable to the current channel.</para>
		</description>
	</agi>
	<agi name="stream file" language="en_US">
		<synopsis>
			Sends audio file on channel.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true">
				<para>File name to play. The file extension must not be
				included in the <replaceable>filename</replaceable>.</para>
			</parameter>
			<parameter name="escape_digits" required="true">
				<para>Use double quotes for the digits if you wish none to be
				permitted.</para>
			</parameter>
			<parameter name="sample offset">
				<para>If sample offset is provided then the audio will seek to sample
				offset before play starts.</para>
			</parameter>
		</syntax>
		<description>
			<para>Send the given file, allowing playback to be interrupted by the given
			digits, if any. Returns <literal>0</literal> if playback completes without a digit
			being pressed, or the ASCII numerical value of the digit if one was pressed,
			or <literal>-1</literal> on error or if the channel was disconnected. If
			musiconhold is playing before calling stream file it will be automatically
			stopped and will not be restarted after completion.</para>
		</description>
		<see-also>
			<ref type="agi">control stream file</ref>
		</see-also>
	</agi>
	<agi name="tdd mode" language="en_US">
		<synopsis>
			Toggles TDD mode (for the deaf).
		</synopsis>
		<syntax>
			<parameter name="boolean" required="true">
				<enumlist>
					<enum name="on" />
					<enum name="off" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Enable/Disable TDD transmission/reception on a channel. Returns <literal>1</literal> if
			successful, or <literal>0</literal> if channel is not TDD-capable.</para>
		</description>
	</agi>
	<agi name="verbose" language="en_US">
		<synopsis>
			Logs a message to the asterisk verbose log.
		</synopsis>
		<syntax>
			<parameter name="message" required="true" />
			<parameter name="level" required="true" />
		</syntax>
		<description>
			<para>Sends <replaceable>message</replaceable> to the console via verbose
			message system. <replaceable>level</replaceable> is the verbose level (1-4).
			Always returns <literal>1</literal></para>
		</description>
	</agi>
	<agi name="wait for digit" language="en_US">
		<synopsis>
			Waits for a digit to be pressed.
		</synopsis>
		<syntax>
			<parameter name="timeout" required="true" />
		</syntax>
		<description>
			<para>Waits up to <replaceable>timeout</replaceable> milliseconds for channel to
			receive a DTMF digit. Returns <literal>-1</literal> on channel failure, <literal>0</literal>
			if no digit is received in the timeout, or the numerical value of the ascii of the digit if
			one is received. Use <literal>-1</literal> for the <replaceable>timeout</replaceable> value if
			you desire the call to block indefinitely.</para>
		</description>
	</agi>
	<agi name="speech create" language="en_US">
		<synopsis>
			Creates a speech object.
		</synopsis>
		<syntax>
			<parameter name="engine" required="true" />
		</syntax>
		<description>
			<para>Create a speech object to be used by the other Speech AGI commands.</para>
		</description>
	</agi>
	<agi name="speech set" language="en_US">
		<synopsis>
			Sets a speech engine setting.
		</synopsis>
		<syntax>
			<parameter name="name" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>Set an engine-specific setting.</para>
		</description>
	</agi>
	<agi name="speech destroy" language="en_US">
		<synopsis>
			Destroys a speech object.
		</synopsis>
		<syntax>
		</syntax>
		<description>
			<para>Destroy the speech object created by <literal>SPEECH CREATE</literal>.</para>
		</description>
		<see-also>
			<ref type="agi">speech create</ref>
		</see-also>
	</agi>
	<agi name="speech load grammar" language="en_US">
		<synopsis>
			Loads a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar name" required="true" />
			<parameter name="path to grammar" required="true" />
		</syntax>
		<description>
			<para>Loads the specified grammar as the specified name.</para>
		</description>
	</agi>
	<agi name="speech unload grammar" language="en_US">
		<synopsis>
			Unloads a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar name" required="true" />
		</syntax>
		<description>
			<para>Unloads the specified grammar.</para>
		</description>
	</agi>
	<agi name="speech activate grammar" language="en_US">
		<synopsis>
			Activates a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar name" required="true" />
		</syntax>
		<description>
			<para>Activates the specified grammar on the speech object.</para>
		</description>
	</agi>
	<agi name="speech deactivate grammar" language="en_US">
		<synopsis>
			Deactivates a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar name" required="true" />
		</syntax>
		<description>
			<para>Deactivates the specified grammar on the speech object.</para>
		</description>
	</agi>
	<agi name="speech recognize" language="en_US">
		<synopsis>
			Recognizes speech.
		</synopsis>
		<syntax>
			<parameter name="prompt" required="true" />
			<parameter name="timeout" required="true" />
			<parameter name="offset" />
		</syntax>
		<description>
			<para>Plays back given <replaceable>prompt</replaceable> while listening for
			speech and dtmf.</para>
		</description>
	</agi>
	<application name="AGI" language="en_US">
		<synopsis>
			Executes an AGI compliant application.
		</synopsis>
		<syntax>
			<parameter name="command" required="true" />
			<parameter name="args">
				<argument name="arg1" required="true" />
				<argument name="arg2" multiple="yes" />
			</parameter>
		</syntax>
		<description>
			<para>Executes an Asterisk Gateway Interface compliant
			program on a channel. AGI allows Asterisk to launch external programs written
			in any language to control a telephony channel, play audio, read DTMF digits,
			etc. by communicating with the AGI protocol on <emphasis>stdin</emphasis> and
			<emphasis>stdout</emphasis>. As of <literal>1.6.0</literal>, this channel will
			not stop dialplan execution on hangup inside of this application. Dialplan
			execution will continue normally, even upon hangup until the AGI application
			signals a desire to stop (either by exiting or, in the case of a net script, by
			closing the connection). A locally executed AGI script will receive SIGHUP on
			hangup from the channel except when using DeadAGI. A fast AGI server will
			correspondingly receive a HANGUP inline with the command dialog. Both of theses
			signals may be disabled by setting the <variable>AGISIGHUP</variable> channel
			variable to <literal>no</literal> before executing the AGI application.
			Alternatively, if you would like the AGI application to exit immediately
			after a channel hangup is detected, set the <variable>AGIEXITONHANGUP</variable>
			variable to <literal>yes</literal>.</para>
			<para>Use the CLI command <literal>agi show commands</literal> to list available agi
			commands.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="AGISTATUS">
					<para>The status of the attempt to the run the AGI script
					text string, one of:</para>
					<value name="SUCCESS" />
					<value name="FAILURE" />
					<value name="NOTFOUND" />
					<value name="HANGUP" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">EAGI</ref>
			<ref type="application">DeadAGI</ref>
		</see-also>
	</application>
	<application name="EAGI" language="en_US">
		<synopsis>
			Executes an EAGI compliant application.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/syntax/parameter[@name='command'])" />
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/syntax/parameter[@name='args'])" />
		</syntax>
		<description>
			<para>Using 'EAGI' provides enhanced AGI, with incoming audio available out of band
			on file descriptor 3.</para>
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/description/para)" />
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/description/variablelist)" />
		</description>
		<see-also>
			<ref type="application">AGI</ref>
			<ref type="application">DeadAGI</ref>
		</see-also>
	</application>
	<application name="DeadAGI" language="en_US">
		<synopsis>
			Executes AGI on a hungup channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/syntax/parameter[@name='command'])" />
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/syntax/parameter[@name='args'])" />
		</syntax>
		<description>
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/description/para)" />
			<xi:include xpointer="xpointer(/docs/application[@name='AGI']/description/variablelist)" />
		</description>
		<see-also>
			<ref type="application">AGI</ref>
			<ref type="application">EAGI</ref>
		</see-also>
	</application>
	<manager name="AGI" language="en_US">
		<synopsis>
			Add an AGI command to execute by Async AGI.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel that is currently in Async AGI.</para>
			</parameter>
			<parameter name="Command" required="true">
				<para>Application to execute.</para>
			</parameter>
			<parameter name="CommandID">
				<para>This will be sent back in CommandID header of AsyncAGI exec
				event notification.</para>
			</parameter>
		</syntax>
		<description>
			<para>Add an AGI command to the execute queue of the channel in Async AGI.</para>
		</description>
	</manager>
 ***/

#define MAX_ARGS 128
#define MAX_CMD_LEN 80
#define AGI_NANDFS_RETRY 3
#define AGI_BUF_LEN 2048
#define SRV_PREFIX "_agi._tcp."

static char *app = "AGI";

static char *eapp = "EAGI";

static char *deadapp = "DeadAGI";

static int agidebug = 0;

#define TONE_BLOCK_SIZE 200

/* Max time to connect to an AGI remote host */
#define MAX_AGI_CONNECT 2000

#define AGI_PORT 4573

/*! Special return code for "asyncagi break" command. */
#define ASYNC_AGI_BREAK	3

enum agi_result {
	AGI_RESULT_FAILURE = -1,
	AGI_RESULT_SUCCESS,
	AGI_RESULT_SUCCESS_FAST,
	AGI_RESULT_SUCCESS_ASYNC,
	AGI_RESULT_NOTFOUND,
	AGI_RESULT_HANGUP,
};

static agi_command *find_command(const char * const cmds[], int exact);

AST_THREADSTORAGE(agi_buf);
#define AGI_BUF_INITSIZE 256

int AST_OPTIONAL_API_NAME(ast_agi_send)(int fd, struct ast_channel *chan, char *fmt, ...)
{
	int res = 0;
	va_list ap;
	struct ast_str *buf;

	if (!(buf = ast_str_thread_get(&agi_buf, AGI_BUF_INITSIZE)))
		return -1;

	va_start(ap, fmt);
	res = ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	if (res == -1) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	if (agidebug) {
		if (chan) {
			ast_verbose("<%s>AGI Tx >> %s", ast_channel_name(chan), ast_str_buffer(buf));
		} else {
			ast_verbose("AGI Tx >> %s", ast_str_buffer(buf));
		}
	}

	return ast_carefulwrite(fd, ast_str_buffer(buf), ast_str_strlen(buf), 100);
}

/* linked list of AGI commands ready to be executed by Async AGI */
struct agi_cmd {
	char *cmd_buffer;
	char *cmd_id;
	AST_LIST_ENTRY(agi_cmd) entry;
};

static void free_agi_cmd(struct agi_cmd *cmd)
{
	ast_free(cmd->cmd_buffer);
	ast_free(cmd->cmd_id);
	ast_free(cmd);
}

/* AGI datastore destructor */
static void agi_destroy_commands_cb(void *data)
{
	struct agi_cmd *cmd;
	AST_LIST_HEAD(, agi_cmd) *chan_cmds = data;
	AST_LIST_LOCK(chan_cmds);
	while ( (cmd = AST_LIST_REMOVE_HEAD(chan_cmds, entry)) ) {
		free_agi_cmd(cmd);
	}
	AST_LIST_UNLOCK(chan_cmds);
	AST_LIST_HEAD_DESTROY(chan_cmds);
	ast_free(chan_cmds);
}

/* channel datastore to keep the queue of AGI commands in the channel */
static const struct ast_datastore_info agi_commands_datastore_info = {
	.type = "AsyncAGI",
	.destroy = agi_destroy_commands_cb
};

static struct agi_cmd *get_agi_cmd(struct ast_channel *chan)
{
	struct ast_datastore *store;
	struct agi_cmd *cmd;
	AST_LIST_HEAD(, agi_cmd) *agi_commands;

	ast_channel_lock(chan);
	store = ast_channel_datastore_find(chan, &agi_commands_datastore_info, NULL);
	ast_channel_unlock(chan);
	if (!store) {
		ast_log(LOG_ERROR, "Huh? Async AGI datastore disappeared on Channel %s!\n",
			ast_channel_name(chan));
		return NULL;
	}
	agi_commands = store->data;
	AST_LIST_LOCK(agi_commands);
	cmd = AST_LIST_REMOVE_HEAD(agi_commands, entry);
	AST_LIST_UNLOCK(agi_commands);
	return cmd;
}

/* channel is locked when calling this one either from the CLI or manager thread */
static int add_agi_cmd(struct ast_channel *chan, const char *cmd_buff, const char *cmd_id)
{
	struct ast_datastore *store;
	struct agi_cmd *cmd;
	AST_LIST_HEAD(, agi_cmd) *agi_commands;

	store = ast_channel_datastore_find(chan, &agi_commands_datastore_info, NULL);
	if (!store) {
		ast_log(LOG_WARNING, "Channel %s is not setup for Async AGI.\n", ast_channel_name(chan));
		return -1;
	}
	agi_commands = store->data;
	cmd = ast_calloc(1, sizeof(*cmd));
	if (!cmd) {
		return -1;
	}
	cmd->cmd_buffer = ast_strdup(cmd_buff);
	if (!cmd->cmd_buffer) {
		ast_free(cmd);
		return -1;
	}
	cmd->cmd_id = ast_strdup(cmd_id);
	if (!cmd->cmd_id) {
		ast_free(cmd->cmd_buffer);
		ast_free(cmd);
		return -1;
	}
	AST_LIST_LOCK(agi_commands);
	AST_LIST_INSERT_TAIL(agi_commands, cmd, entry);
	AST_LIST_UNLOCK(agi_commands);
	return 0;
}

static int add_to_agi(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	AST_LIST_HEAD(, agi_cmd) *agi_cmds_list;

	/* check if already on AGI */
	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &agi_commands_datastore_info, NULL);
	ast_channel_unlock(chan);
	if (datastore) {
		/* we already have an AGI datastore, let's just
		   return success */
		return 0;
	}

	/* the channel has never been on Async AGI,
	   let's allocate it's datastore */
	datastore = ast_datastore_alloc(&agi_commands_datastore_info, "AGI");
	if (!datastore) {
		return -1;
	}
	agi_cmds_list = ast_calloc(1, sizeof(*agi_cmds_list));
	if (!agi_cmds_list) {
		ast_log(LOG_ERROR, "Unable to allocate Async AGI commands list.\n");
		ast_datastore_free(datastore);
		return -1;
	}
	datastore->data = agi_cmds_list;
	AST_LIST_HEAD_INIT(agi_cmds_list);
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
	return 0;
}

/*!
 * \brief CLI command to add applications to execute in Async AGI
 * \param e
 * \param cmd
 * \param a
 *
 * \retval CLI_SUCCESS on success
 * \retval NULL when init or tab completion is used
*/
static char *handle_cli_agi_add_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	switch (cmd) {
	case CLI_INIT:
		e->command = "agi exec";
		e->usage = "Usage: agi exec <channel name> <app and arguments> [id]\n"
			   "       Add AGI command to the execute queue of the specified channel in Async AGI\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2)
			return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
		return NULL;
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(chan = ast_channel_get_by_name(a->argv[2]))) {
		ast_cli(a->fd, "Channel %s does not exist.\n", a->argv[2]);
		return CLI_FAILURE;
	}

	ast_channel_lock(chan);

	if (add_agi_cmd(chan, a->argv[3], (a->argc > 4 ? a->argv[4] : ""))) {
		ast_cli(a->fd, "Failed to add AGI command to queue of channel %s\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		chan = ast_channel_unref(chan);
		return CLI_FAILURE;
	}

	ast_debug(1, "Added AGI command to channel %s queue\n", ast_channel_name(chan));

	ast_channel_unlock(chan);
	chan = ast_channel_unref(chan);

	return CLI_SUCCESS;
}

/*!
 * \brief Add a new command to execute by the Async AGI application
 * \param s
 * \param m
 *
 * It will append the application to the specified channel's queue
 * if the channel is not inside Async AGI application it will return an error
 * \retval 0 on success or incorrect use
 * \retval 1 on failure to add the command ( most likely because the channel
 * is not in Async AGI loop )
*/
static int action_add_agi_cmd(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *cmdbuff = astman_get_header(m, "Command");
	const char *cmdid   = astman_get_header(m, "CommandID");
	struct ast_channel *chan;
	char buf[256];

	if (ast_strlen_zero(channel) || ast_strlen_zero(cmdbuff)) {
		astman_send_error(s, m, "Both, Channel and Command are *required*");
		return 0;
	}

	if (!(chan = ast_channel_get_by_name(channel))) {
		snprintf(buf, sizeof(buf), "Channel %s does not exist.", channel);
		astman_send_error(s, m, buf);
		return 0;
	}

	ast_channel_lock(chan);

	if (add_agi_cmd(chan, cmdbuff, cmdid)) {
		snprintf(buf, sizeof(buf), "Failed to add AGI command to channel %s queue", ast_channel_name(chan));
		astman_send_error(s, m, buf);
		ast_channel_unlock(chan);
		chan = ast_channel_unref(chan);
		return 0;
	}

	ast_channel_unlock(chan);
	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "Added AGI command to queue");

	return 0;
}

static enum agi_result agi_handle_command(struct ast_channel *chan, AGI *agi, char *buf, int dead);
static void setup_env(struct ast_channel *chan, char *request, int fd, int enhanced, int argc, char *argv[]);

/*!
 * \internal
 * \brief Read and handle a channel frame for Async AGI.
 *
 * \param chan Channel to read a frame from.
 *
 * \retval AGI_RESULT_SUCCESS on success.
 * \retval AGI_RESULT_HANGUP on hangup.
 * \retval AGI_RESULT_FAILURE on error.
 */
static enum agi_result async_agi_read_frame(struct ast_channel *chan)
{
	struct ast_frame *f;

	f = ast_read(chan);
	if (!f) {
		ast_debug(3, "No frame read on channel %s, going out ...\n", ast_channel_name(chan));
		return AGI_RESULT_HANGUP;
	}
	if (f->frametype == AST_FRAME_CONTROL) {
		/*
		 * Is there any other frame we should care about besides
		 * AST_CONTROL_HANGUP?
		 */
		switch (f->subclass.integer) {
		case AST_CONTROL_HANGUP:
			ast_debug(3, "Got HANGUP frame on channel %s, going out ...\n", ast_channel_name(chan));
			ast_frfree(f);
			return AGI_RESULT_HANGUP;
		default:
			break;
		}
	}
	ast_frfree(f);

	return AGI_RESULT_SUCCESS;
}

static enum agi_result launch_asyncagi(struct ast_channel *chan, char *argv[], int *efd)
{
/* This buffer sizes might cause truncation if the AGI command writes more data
   than AGI_BUF_SIZE as result. But let's be serious, is there an AGI command
   that writes a response larger than 1024 bytes?, I don't think so, most of
   them are just result=blah stuff. However probably if GET VARIABLE is called
   and the variable has large amount of data, that could be a problem. We could
   make this buffers dynamic, but let's leave that as a second step.

   AMI_BUF_SIZE is twice AGI_BUF_SIZE just for the sake of choosing a safe
   number. Some characters of AGI buf will be url encoded to be sent to manager
   clients.  An URL encoded character will take 3 bytes, but again, to cause
   truncation more than about 70% of the AGI buffer should be URL encoded for
   that to happen.  Not likely at all.

   On the other hand. I wonder if read() could eventually return less data than
   the amount already available in the pipe? If so, how to deal with that?
   So far, my tests on Linux have not had any problems.
 */
#define AGI_BUF_SIZE 1024
#define AMI_BUF_SIZE 2048
	enum agi_result cmd_status;
	struct agi_cmd *cmd;
	int res;
	int fds[2];
	int hungup;
	int timeout = 100;
	char agi_buffer[AGI_BUF_SIZE + 1];
	char ami_buffer[AMI_BUF_SIZE];
	enum agi_result returnstatus = AGI_RESULT_SUCCESS;
	AGI async_agi;

	if (efd) {
		ast_log(LOG_WARNING, "Async AGI does not support Enhanced AGI yet\n");
		return AGI_RESULT_FAILURE;
	}

	/* add AsyncAGI datastore to the channel */
	if (add_to_agi(chan)) {
		ast_log(LOG_ERROR, "Failed to start Async AGI on channel %s\n", ast_channel_name(chan));
		return AGI_RESULT_FAILURE;
	}

	/* this pipe allows us to create a "fake" AGI struct to use
	   the AGI commands */
	res = pipe(fds);
	if (res) {
		ast_log(LOG_ERROR, "Failed to create Async AGI pipe\n");
		/*
		 * Intentionally do not remove the datastore added with
		 * add_to_agi() the from channel.  It will be removed when the
		 * channel is hung up anyway.
		 */
		return AGI_RESULT_FAILURE;
	}

	/* handlers will get the pipe write fd and we read the AGI responses
	   from the pipe read fd */
	async_agi.fd = fds[1];
	async_agi.ctrl = fds[1];
	async_agi.audio = -1; /* no audio support */
	async_agi.fast = 0;
	async_agi.speech = NULL;

	/* notify possible manager users of a new channel ready to
	   receive commands */
	setup_env(chan, "async", fds[1], 0, 0, NULL);
	/* read the environment */
	res = read(fds[0], agi_buffer, AGI_BUF_SIZE);
	if (res <= 0) {
		ast_log(LOG_ERROR, "Failed to read from Async AGI pipe on channel %s: %s\n",
				ast_channel_name(chan), res < 0 ? strerror(errno) : "EOF");
		returnstatus = AGI_RESULT_FAILURE;
		goto async_agi_abort;
	}
	agi_buffer[res] = '\0';
	/* encode it and send it thru the manager so whoever is going to take
	   care of AGI commands on this channel can decide which AGI commands
	   to execute based on the setup info */
	ast_uri_encode(agi_buffer, ami_buffer, AMI_BUF_SIZE, ast_uri_http);
	manager_event(EVENT_FLAG_AGI, "AsyncAGI",
		"SubEvent: Start\r\n"
		"Channel: %s\r\n"
		"Env: %s\r\n", ast_channel_name(chan), ami_buffer);
	hungup = ast_check_hangup(chan);
	for (;;) {
		/*
		 * Process as many commands as we can.  Commands are added via
		 * the manager or the cli threads.
		 */
		while (!hungup && (cmd = get_agi_cmd(chan))) {
			/* OK, we have a command, let's call the command handler. */
			cmd_status = agi_handle_command(chan, &async_agi, cmd->cmd_buffer, 0);

			/*
			 * The command handler must have written to our fake AGI struct
			 * fd (the pipe), let's read the response.
			 */
			res = read(fds[0], agi_buffer, AGI_BUF_SIZE);
			if (res <= 0) {
				ast_log(LOG_ERROR, "Failed to read from Async AGI pipe on channel %s: %s\n",
					ast_channel_name(chan), res < 0 ? strerror(errno) : "EOF");
				free_agi_cmd(cmd);
				returnstatus = AGI_RESULT_FAILURE;
				goto async_agi_done;
			}
			/*
			 * We have a response, let's send the response thru the manager.
			 * Include the CommandID if it was specified when the command
			 * was added.
			 */
			agi_buffer[res] = '\0';
			ast_uri_encode(agi_buffer, ami_buffer, AMI_BUF_SIZE, ast_uri_http);
			if (ast_strlen_zero(cmd->cmd_id)) {
				manager_event(EVENT_FLAG_AGI, "AsyncAGI",
					"SubEvent: Exec\r\n"
					"Channel: %s\r\n"
					"Result: %s\r\n", ast_channel_name(chan), ami_buffer);
			} else {
				manager_event(EVENT_FLAG_AGI, "AsyncAGI",
					"SubEvent: Exec\r\n"
					"Channel: %s\r\n"
					"CommandID: %s\r\n"
					"Result: %s\r\n", ast_channel_name(chan), cmd->cmd_id, ami_buffer);
			}
			free_agi_cmd(cmd);

			/*
			 * Check the command status to determine if we should continue
			 * executing more commands.
			 */
			hungup = ast_check_hangup(chan);
			switch (cmd_status) {
			case AGI_RESULT_FAILURE:
				if (!hungup) {
					/* The failure was not because of a hangup. */
					returnstatus = AGI_RESULT_FAILURE;
					goto async_agi_done;
				}
				break;
			case AGI_RESULT_SUCCESS_ASYNC:
				/* Only the "asyncagi break" command does this. */
				returnstatus = AGI_RESULT_SUCCESS_ASYNC;
				goto async_agi_done;
			default:
				break;
			}
		}

		if (!hungup) {
			/* Wait a bit for a frame to read or to poll for a new command. */
			res = ast_waitfor(chan, timeout);
			if (res < 0) {
				ast_debug(1, "ast_waitfor returned <= 0 on chan %s\n", ast_channel_name(chan));
				returnstatus = AGI_RESULT_FAILURE;
				break;
			}
		} else {
			/*
			 * Read the channel control queue until it is dry so we can
			 * quit.
			 */
			res = 1;
		}
		if (0 < res) {
			do {
				cmd_status = async_agi_read_frame(chan);
				if (cmd_status != AGI_RESULT_SUCCESS) {
					returnstatus = cmd_status;
					goto async_agi_done;
				}
				hungup = ast_check_hangup(chan);
			} while (hungup);
		} else {
			hungup = ast_check_hangup(chan);
		}
	}
async_agi_done:

	if (async_agi.speech) {
		ast_speech_destroy(async_agi.speech);
	}
	/* notify manager users this channel cannot be
	   controlled anymore by Async AGI */
	manager_event(EVENT_FLAG_AGI, "AsyncAGI",
		"SubEvent: End\r\n"
		"Channel: %s\r\n", ast_channel_name(chan));

async_agi_abort:
	/* close the pipe */
	close(fds[0]);
	close(fds[1]);

	/*
	 * Intentionally do not remove the datastore added with
	 * add_to_agi() the from channel.  There might be commands still
	 * in the queue or in-flight to us and AsyncAGI may get called
	 * again.  The datastore destructor will be called on channel
	 * destruction anyway.
	 */

	if (returnstatus == AGI_RESULT_SUCCESS) {
		returnstatus = AGI_RESULT_SUCCESS_ASYNC;
	}
	return returnstatus;

#undef AGI_BUF_SIZE
#undef AMI_BUF_SIZE
}

/*!
 * \internal
 * \brief Handle the connection that was started by launch_netscript.
 *
 * \param agiurl Url that we are trying to connect to.
 * \param addr Address that host was resolved to.
 * \param netsockfd File descriptor of socket.
 *
 * \retval 0 when connection is succesful.
 * \retval 1 when there is an error.
 */
static int handle_connection(const char *agiurl, const struct ast_sockaddr addr, const int netsockfd)
{
	struct pollfd pfds[1];
	int res, conresult;
	socklen_t reslen;

	reslen = sizeof(conresult);

	pfds[0].fd = netsockfd;
	pfds[0].events = POLLOUT;

	while ((res = ast_poll(pfds, 1, MAX_AGI_CONNECT)) != 1) {
		if (errno != EINTR) {
			if (!res) {
				ast_log(LOG_WARNING, "FastAGI connection to '%s' timed out after MAX_AGI_CONNECT (%d) milliseconds.\n",
					agiurl, MAX_AGI_CONNECT);
			} else {
				ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", agiurl, strerror(errno));
			}

			return 1;
		}
	}

	if (getsockopt(pfds[0].fd, SOL_SOCKET, SO_ERROR, &conresult, &reslen) < 0) {
		ast_log(LOG_WARNING, "Connection to %s failed with error: %s\n",
			ast_sockaddr_stringify(&addr), strerror(errno));
		return 1;
	}

	if (conresult) {
		ast_log(LOG_WARNING, "Connecting to '%s' failed for url '%s': %s\n",
			ast_sockaddr_stringify(&addr), agiurl, strerror(conresult));
		return 1;
	}

	return 0;
}

/* launch_netscript: The fastagi handler.
	FastAGI defaults to port 4573 */
static enum agi_result launch_netscript(char *agiurl, char *argv[], int *fds)
{
	int s = 0, flags;
	char *host, *script;
	int num_addrs = 0, i = 0;
	struct ast_sockaddr *addrs;

	/* agiurl is "agi://host.domain[:port][/script/name]" */
	host = ast_strdupa(agiurl + 6);	/* Remove agi:// */

	/* Strip off any script name */
	if ((script = strchr(host, '/'))) {
		*script++ = '\0';
	} else {
		script = "";
	}

	if (!(num_addrs = ast_sockaddr_resolve(&addrs, host, 0, AST_AF_UNSPEC))) {
		ast_log(LOG_WARNING, "Unable to locate host '%s'\n", host);
		return AGI_RESULT_FAILURE;
	}

	for (i = 0; i < num_addrs; i++) {
		if (!ast_sockaddr_port(&addrs[i])) {
			ast_sockaddr_set_port(&addrs[i], AGI_PORT);
		}

		if ((s = socket(addrs[i].ss.ss_family, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
			continue;
		}

		if ((flags = fcntl(s, F_GETFL)) < 0) {
			ast_log(LOG_WARNING, "fcntl(F_GETFL) failed: %s\n", strerror(errno));
			close(s);
			continue;
		}

		if (fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "fnctl(F_SETFL) failed: %s\n", strerror(errno));
			close(s);
			continue;
		}

		if (ast_connect(s, &addrs[i]) && errno == EINPROGRESS) {

			if (handle_connection(agiurl, addrs[i], s)) {
				close(s);
				continue;
			}

		} else {
			ast_log(LOG_WARNING, "Connection to %s failed with unexpected error: %s\n",
			ast_sockaddr_stringify(&addrs[i]), strerror(errno));
		}

		break;
	}

	ast_free(addrs);

	if (i == num_addrs) {
		ast_log(LOG_WARNING, "Couldn't connect to any host.  FastAGI failed.\n");
		return AGI_RESULT_FAILURE;
	}

	if (ast_agi_send(s, NULL, "agi_network: yes\n") < 0) {
		if (errno != EINTR) {
			ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", agiurl, strerror(errno));
			close(s);
			return AGI_RESULT_FAILURE;
		}
	}

	/* If we have a script parameter, relay it to the fastagi server */
	/* Script parameters take the form of: AGI(agi://my.example.com/?extension=${EXTEN}) */
	if (!ast_strlen_zero(script)) {
		ast_agi_send(s, NULL, "agi_network_script: %s\n", script);
	}

	ast_debug(4, "Wow, connected!\n");
	fds[0] = s;
	fds[1] = s;
	return AGI_RESULT_SUCCESS_FAST;
}

/*!
 * \internal
 * \brief The HA fastagi handler.
 * \param agiurl The request URL as passed to Agi() in the dial plan
 * \param argv The parameters after the URL passed to Agi() in the dial plan
 * \param fds Input/output file descriptors
 *
 * Uses SRV lookups to try to connect to a list of FastAGI servers. The hostname in
 * the URI is prefixed with _agi._tcp. prior to the DNS resolution. For
 * example, if you specify the URI \a hagi://agi.example.com/foo.agi the DNS
 * query would be for \a _agi._tcp.agi.example.com and you'll need to make sure
 * this resolves.
 *
 * This function parses the URI, resolves the SRV service name, forms new URIs
 * with the results of the DNS lookup, and then calls launch_netscript on the
 * new URIs until one succeeds.
 *
 * \return the result of the AGI operation.
 */
static enum agi_result launch_ha_netscript(char *agiurl, char *argv[], int *fds)
{
	char *host, *script;
	enum agi_result result;
	struct srv_context *context = NULL;
	int srv_ret;
	char service[256];
	char resolved_uri[1024];
	const char *srvhost;
	unsigned short srvport;

	/* format of agiurl is "hagi://host.domain[:port][/script/name]" */
	if (strlen(agiurl) < 7) { /* Remove hagi:// */
		ast_log(LOG_WARNING, "An error occurred parsing the AGI URI: %s", agiurl);
		return AGI_RESULT_FAILURE;
	}
	host = ast_strdupa(agiurl + 7);

	/* Strip off any script name */
	if ((script = strchr(host, '/'))) {
		*script++ = '\0';
	} else {
		script = "";
	}

	if (strchr(host, ':')) {
		ast_log(LOG_WARNING, "Specifying a port number disables SRV lookups: %s\n", agiurl);
		return launch_netscript(agiurl + 1, argv, fds); /* +1 to strip off leading h from hagi:// */
	}

	snprintf(service, sizeof(service), "%s%s", SRV_PREFIX, host);

	while (!(srv_ret = ast_srv_lookup(&context, service, &srvhost, &srvport))) {
		snprintf(resolved_uri, sizeof(resolved_uri), "agi://%s:%d/%s", srvhost, srvport, script);
		result = launch_netscript(resolved_uri, argv, fds);
		if (result == AGI_RESULT_FAILURE || result == AGI_RESULT_NOTFOUND) {
			ast_log(LOG_WARNING, "AGI request failed for host '%s' (%s:%d)\n", host, srvhost, srvport);
		} else {
			/* The script launched so we must cleanup the context. */
			ast_srv_cleanup(&context);
			return result;
		}
	}
	/*
	 * The DNS SRV lookup failed or we ran out of servers to check.
	 * ast_srv_lookup() has already cleaned up the context for us.
	 */
	if (srv_ret < 0) {
		ast_log(LOG_WARNING, "SRV lookup failed for %s\n", agiurl);
	}

	return AGI_RESULT_FAILURE;
}

static enum agi_result launch_script(struct ast_channel *chan, char *script, char *argv[], int *fds, int *efd, int *opid)
{
	char tmp[256];
	int pid, toast[2], fromast[2], audio[2], res;
	struct stat st;

	if (!strncasecmp(script, "agi://", 6)) {
		return (efd == NULL) ? launch_netscript(script, argv, fds) : AGI_RESULT_FAILURE;
	}
	if (!strncasecmp(script, "hagi://", 7)) {
		return (efd == NULL) ? launch_ha_netscript(script, argv, fds) : AGI_RESULT_FAILURE;
	}
	if (!strncasecmp(script, "agi:async", sizeof("agi:async") - 1)) {
		return launch_asyncagi(chan, argv, efd);
	}

	if (script[0] != '/') {
		snprintf(tmp, sizeof(tmp), "%s/%s", ast_config_AST_AGI_DIR, script);
		script = tmp;
	}

	/* Before even trying let's see if the file actually exists */
	if (stat(script, &st)) {
		ast_log(LOG_WARNING, "Failed to execute '%s': File does not exist.\n", script);
		return AGI_RESULT_NOTFOUND;
	}

	if (pipe(toast)) {
		ast_log(LOG_WARNING, "Unable to create toast pipe: %s\n",strerror(errno));
		return AGI_RESULT_FAILURE;
	}
	if (pipe(fromast)) {
		ast_log(LOG_WARNING, "unable to create fromast pipe: %s\n", strerror(errno));
		close(toast[0]);
		close(toast[1]);
		return AGI_RESULT_FAILURE;
	}
	if (efd) {
		if (pipe(audio)) {
			ast_log(LOG_WARNING, "unable to create audio pipe: %s\n", strerror(errno));
			close(fromast[0]);
			close(fromast[1]);
			close(toast[0]);
			close(toast[1]);
			return AGI_RESULT_FAILURE;
		}
		res = fcntl(audio[1], F_GETFL);
		if (res > -1)
			res = fcntl(audio[1], F_SETFL, res | O_NONBLOCK);
		if (res < 0) {
			ast_log(LOG_WARNING, "unable to set audio pipe parameters: %s\n", strerror(errno));
			close(fromast[0]);
			close(fromast[1]);
			close(toast[0]);
			close(toast[1]);
			close(audio[0]);
			close(audio[1]);
			return AGI_RESULT_FAILURE;
		}
	}

	if ((pid = ast_safe_fork(1)) < 0) {
		ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		return AGI_RESULT_FAILURE;
	}
	if (!pid) {
		/* Pass paths to AGI via environmental variables */
		setenv("AST_CONFIG_DIR", ast_config_AST_CONFIG_DIR, 1);
		setenv("AST_CONFIG_FILE", ast_config_AST_CONFIG_FILE, 1);
		setenv("AST_MODULE_DIR", ast_config_AST_MODULE_DIR, 1);
		setenv("AST_SPOOL_DIR", ast_config_AST_SPOOL_DIR, 1);
		setenv("AST_MONITOR_DIR", ast_config_AST_MONITOR_DIR, 1);
		setenv("AST_VAR_DIR", ast_config_AST_VAR_DIR, 1);
		setenv("AST_DATA_DIR", ast_config_AST_DATA_DIR, 1);
		setenv("AST_LOG_DIR", ast_config_AST_LOG_DIR, 1);
		setenv("AST_AGI_DIR", ast_config_AST_AGI_DIR, 1);
		setenv("AST_KEY_DIR", ast_config_AST_KEY_DIR, 1);
		setenv("AST_RUN_DIR", ast_config_AST_RUN_DIR, 1);

		/* Don't run AGI scripts with realtime priority -- it causes audio stutter */
		ast_set_priority(0);

		/* Redirect stdin and out, provide enhanced audio channel if desired */
		dup2(fromast[0], STDIN_FILENO);
		dup2(toast[1], STDOUT_FILENO);
		if (efd)
			dup2(audio[0], STDERR_FILENO + 1);
		else
			close(STDERR_FILENO + 1);

		/* Close everything but stdin/out/error */
		ast_close_fds_above_n(STDERR_FILENO + 1);

		/* Execute script */
		/* XXX argv should be deprecated in favor of passing agi_argX paramaters */
		execv(script, argv);
		/* Can't use ast_log since FD's are closed */
		ast_child_verbose(1, "Failed to execute '%s': %s", script, strerror(errno));
		/* Special case to set status of AGI to failure */
		fprintf(stdout, "failure\n");
		fflush(stdout);
		_exit(1);
	}
	ast_verb(3, "Launched AGI Script %s\n", script);
	fds[0] = toast[0];
	fds[1] = fromast[1];
	if (efd)
		*efd = audio[1];
	/* close what we're not using in the parent */
	close(toast[1]);
	close(fromast[0]);

	if (efd)
		close(audio[0]);

	*opid = pid;
	return AGI_RESULT_SUCCESS;
}

static void setup_env(struct ast_channel *chan, char *request, int fd, int enhanced, int argc, char *argv[])
{
	int count;

	/* Print initial environment, with agi_request always being the first
	   thing */
	ast_agi_send(fd, chan, "agi_request: %s\n", request);
	ast_agi_send(fd, chan, "agi_channel: %s\n", ast_channel_name(chan));
	ast_agi_send(fd, chan, "agi_language: %s\n", ast_channel_language(chan));
	ast_agi_send(fd, chan, "agi_type: %s\n", ast_channel_tech(chan)->type);
	ast_agi_send(fd, chan, "agi_uniqueid: %s\n", ast_channel_uniqueid(chan));
	ast_agi_send(fd, chan, "agi_version: %s\n", ast_get_version());

	/* ANI/DNIS */
	ast_agi_send(fd, chan, "agi_callerid: %s\n",
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "unknown"));
	ast_agi_send(fd, chan, "agi_calleridname: %s\n",
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, "unknown"));
	ast_agi_send(fd, chan, "agi_callingpres: %d\n",
		ast_party_id_presentation(&ast_channel_caller(chan)->id));
	ast_agi_send(fd, chan, "agi_callingani2: %d\n", ast_channel_caller(chan)->ani2);
	ast_agi_send(fd, chan, "agi_callington: %d\n", ast_channel_caller(chan)->id.number.plan);
	ast_agi_send(fd, chan, "agi_callingtns: %d\n", ast_channel_dialed(chan)->transit_network_select);
	ast_agi_send(fd, chan, "agi_dnid: %s\n", S_OR(ast_channel_dialed(chan)->number.str, "unknown"));
	ast_agi_send(fd, chan, "agi_rdnis: %s\n",
		S_COR(ast_channel_redirecting(chan)->from.number.valid, ast_channel_redirecting(chan)->from.number.str, "unknown"));

	/* Context information */
	ast_agi_send(fd, chan, "agi_context: %s\n", ast_channel_context(chan));
	ast_agi_send(fd, chan, "agi_extension: %s\n", ast_channel_exten(chan));
	ast_agi_send(fd, chan, "agi_priority: %d\n", ast_channel_priority(chan));
	ast_agi_send(fd, chan, "agi_enhanced: %s\n", enhanced ? "1.0" : "0.0");

	/* User information */
	ast_agi_send(fd, chan, "agi_accountcode: %s\n", ast_channel_accountcode(chan) ? ast_channel_accountcode(chan) : "");
	ast_agi_send(fd, chan, "agi_threadid: %ld\n", (long)pthread_self());

	/* Send any parameters to the fastagi server that have been passed via the agi application */
	/* Agi application paramaters take the form of: AGI(/path/to/example/script|${EXTEN}) */
	for(count = 1; count < argc; count++)
		ast_agi_send(fd, chan, "agi_arg_%d: %s\n", count, argv[count]);

	/* End with empty return */
	ast_agi_send(fd, chan, "\n");
}

static int handle_answer(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res = 0;

	/* Answer the channel */
	if (ast_channel_state(chan) != AST_STATE_UP)
		res = ast_answer(chan);

	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_asyncagi_break(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return ASYNC_AGI_BREAK;
}

static int handle_waitfordigit(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, to;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[3], "%30d", &to) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_waitfordigit_full(chan, to, agi->audio, agi->ctrl);
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_sendtext(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	/* At the moment, the parser (perhaps broken) returns with
	   the last argument PLUS the newline at the end of the input
	   buffer. This probably needs to be fixed, but I wont do that
	   because other stuff may break as a result. The right way
	   would probably be to strip off the trailing newline before
	   parsing, then here, add a newline at the end of the string
	   before sending it to ast_sendtext --DUDE */
	res = ast_sendtext(chan, argv[2]);
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_recvchar(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	res = ast_recvchar(chan,atoi(argv[2]));
	if (res == 0) {
		ast_agi_send(agi->fd, chan, "200 result=%d (timeout)\n", res);
		return RESULT_SUCCESS;
	}
	if (res > 0) {
		ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
		return RESULT_SUCCESS;
	}
	ast_agi_send(agi->fd, chan, "200 result=%d (hangup)\n", res);
	return RESULT_FAILURE;
}

static int handle_recvtext(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	char *buf;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	buf = ast_recvtext(chan, atoi(argv[2]));
	if (buf) {
		ast_agi_send(agi->fd, chan, "200 result=1 (%s)\n", buf);
		ast_free(buf);
	} else {
		ast_agi_send(agi->fd, chan, "200 result=-1\n");
	}
	return RESULT_SUCCESS;
}

static int handle_tddmode(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, x;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	if (!strncasecmp(argv[2],"on",2)) {
		x = 1;
	} else  {
		x = 0;
	}
	if (!strncasecmp(argv[2],"mate",4))  {
		x = 2;
	}
	if (!strncasecmp(argv[2],"tdd",3)) {
		x = 1;
	}
	res = ast_channel_setoption(chan, AST_OPTION_TDD, &x, sizeof(char), 0);
	if (res) {
		/* Set channel option failed */
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	} else {
		ast_agi_send(agi->fd, chan, "200 result=1\n");
	}
	return RESULT_SUCCESS;
}

static int handle_sendimage(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	res = ast_send_image(chan, argv[2]);
	if (!ast_check_hangup(chan)) {
		res = 0;
	}
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_controlstreamfile(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res = 0, skipms = 3000;
	const char *fwd = "#", *rev = "*", *suspend = NULL, *stop = NULL;	/* Default values */

	if (argc < 5 || argc > 9) {
		return RESULT_SHOWUSAGE;
	}

	if (!ast_strlen_zero(argv[4])) {
		stop = argv[4];
	}

	if ((argc > 5) && (sscanf(argv[5], "%30d", &skipms) != 1)) {
		return RESULT_SHOWUSAGE;
	}

	if (argc > 6 && !ast_strlen_zero(argv[6])) {
		fwd = argv[6];
	}

	if (argc > 7 && !ast_strlen_zero(argv[7])) {
		rev = argv[7];
	}

	if (argc > 8 && !ast_strlen_zero(argv[8])) {
		suspend = argv[8];
	}

	res = ast_control_streamfile(chan, argv[3], fwd, rev, stop, suspend, NULL, skipms, NULL);

	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);

	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_streamfile(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;
	struct ast_filestream *fs, *vfs;
	long sample_offset = 0, max_length;
	const char *edigits = "";

	if (argc < 4 || argc > 5) {
		return RESULT_SHOWUSAGE;
	}

	if (argv[3]) {
		edigits = argv[3];
	}

	if ((argc > 4) && (sscanf(argv[4], "%30ld", &sample_offset) != 1)) {
		return RESULT_SHOWUSAGE;
	}

	if (!(fs = ast_openstream(chan, argv[2], ast_channel_language(chan)))) {
		ast_agi_send(agi->fd, chan, "200 result=-1 endpos=%ld\n", sample_offset);
		return RESULT_FAILURE;
	}

	if ((vfs = ast_openvstream(chan, argv[2], ast_channel_language(chan)))) {
		ast_debug(1, "Ooh, found a video stream, too\n");
	}

	ast_verb(3, "Playing '%s' (escape_digits=%s) (sample_offset %ld)\n", argv[2], edigits, sample_offset);

	ast_seekstream(fs, 0, SEEK_END);
	max_length = ast_tellstream(fs);
	ast_seekstream(fs, sample_offset, SEEK_SET);
	res = ast_applystream(chan, fs);
	if (vfs) {
		ast_applystream(chan, vfs);
	}
	ast_playstream(fs);
	if (vfs) {
		ast_playstream(vfs);
	}

	res = ast_waitstream_full(chan, argv[3], agi->audio, agi->ctrl);
	/* this is to check for if ast_waitstream closed the stream, we probably are at
	 * the end of the stream, return that amount, else check for the amount */
	sample_offset = (ast_channel_stream(chan)) ? ast_tellstream(fs) : max_length;
	ast_stopstream(chan);
	if (res == 1) {
		/* Stop this command, don't print a result line, as there is a new command */
		return RESULT_SUCCESS;
	}
	ast_agi_send(agi->fd, chan, "200 result=%d endpos=%ld\n", res, sample_offset);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

/*! \brief get option - really similar to the handle_streamfile, but with a timeout */
static int handle_getoption(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;
	struct ast_filestream *fs, *vfs;
	long sample_offset = 0, max_length;
	int timeout = 0;
	const char *edigits = "";

	if ( argc < 4 || argc > 5 )
		return RESULT_SHOWUSAGE;

	if ( argv[3] )
		edigits = argv[3];

	if ( argc == 5 )
		timeout = atoi(argv[4]);
	else if (ast_channel_pbx(chan)->dtimeoutms) {
		/* by default dtimeout is set to 5sec */
		timeout = ast_channel_pbx(chan)->dtimeoutms; /* in msec */
	}

	if (!(fs = ast_openstream(chan, argv[2], ast_channel_language(chan)))) {
		ast_agi_send(agi->fd, chan, "200 result=-1 endpos=%ld\n", sample_offset);
		ast_log(LOG_WARNING, "Unable to open %s\n", argv[2]);
		return RESULT_FAILURE;
	}

	if ((vfs = ast_openvstream(chan, argv[2], ast_channel_language(chan))))
		ast_debug(1, "Ooh, found a video stream, too\n");

	ast_verb(3, "Playing '%s' (escape_digits=%s) (timeout %d)\n", argv[2], edigits, timeout);

	ast_seekstream(fs, 0, SEEK_END);
	max_length = ast_tellstream(fs);
	ast_seekstream(fs, sample_offset, SEEK_SET);
	res = ast_applystream(chan, fs);
	if (vfs)
		ast_applystream(chan, vfs);
	ast_playstream(fs);
	if (vfs)
		ast_playstream(vfs);

	res = ast_waitstream_full(chan, argv[3], agi->audio, agi->ctrl);
	/* this is to check for if ast_waitstream closed the stream, we probably are at
	 * the end of the stream, return that amount, else check for the amount */
	sample_offset = (ast_channel_stream(chan))?ast_tellstream(fs):max_length;
	ast_stopstream(chan);
	if (res == 1) {
		/* Stop this command, don't print a result line, as there is a new command */
		return RESULT_SUCCESS;
	}

	/* If the user didnt press a key, wait for digitTimeout*/
	if (res == 0 ) {
		res = ast_waitfordigit_full(chan, timeout, agi->audio, agi->ctrl);
		/* Make sure the new result is in the escape digits of the GET OPTION */
		if ( !strchr(edigits,res) )
			res=0;
	}

	ast_agi_send(agi->fd, chan, "200 result=%d endpos=%ld\n", res, sample_offset);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}




/*! \brief Say number in various language syntaxes */
/* While waiting, we're sending a NULL.  */
static int handle_saynumber(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, num;

	if (argc < 4 || argc > 5)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_number_full(chan, num, argv[3], ast_channel_language(chan), argc > 4 ? argv[4] : NULL, agi->audio, agi->ctrl);
	if (res == 1)
		return RESULT_SUCCESS;
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saydigits(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;

	res = ast_say_digit_str_full(chan, argv[2], argv[3], ast_channel_language(chan), agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_sayalpha(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	res = ast_say_character_str_full(chan, argv[2], argv[3], ast_channel_language(chan), agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saydate(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_date(chan, num, argv[3], ast_channel_language(chan));
	if (res == 1)
		return RESULT_SUCCESS;
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saytime(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_time(chan, num, argv[3], ast_channel_language(chan));
	if (res == 1)
		return RESULT_SUCCESS;
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saydatetime(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res = 0;
	time_t unixtime;
	const char *format, *zone = NULL;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (argc > 4) {
		format = argv[4];
	} else {
		/* XXX this doesn't belong here, but in the 'say' module */
		if (!strcasecmp(ast_channel_language(chan), "de")) {
			format = "A dBY HMS";
		} else {
			format = "ABdY 'digits/at' IMp";
		}
	}

	if (argc > 5 && !ast_strlen_zero(argv[5]))
		zone = argv[5];

	if (ast_get_time_t(argv[2], &unixtime, 0, NULL))
		return RESULT_SHOWUSAGE;

	res = ast_say_date_with_format(chan, unixtime, argv[3], ast_channel_language(chan), format, zone);
	if (res == 1)
		return RESULT_SUCCESS;

	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_sayphonetic(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	res = ast_say_phonetic_str_full(chan, argv[2], argv[3], ast_channel_language(chan), agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_getdata(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, max, timeout;
	char data[1024];

	if (argc < 3)
		return RESULT_SHOWUSAGE;
	if (argc >= 4)
		timeout = atoi(argv[3]);
	else
		timeout = 0;
	if (argc >= 5)
		max = atoi(argv[4]);
	else
		max = 1024;
	res = ast_app_getdata_full(chan, argv[2], data, max, timeout, agi->audio, agi->ctrl);
	if (res == 2)			/* New command */
		return RESULT_SUCCESS;
	else if (res == 1)
		ast_agi_send(agi->fd, chan, "200 result=%s (timeout)\n", data);
	else if (res < 0 )
		ast_agi_send(agi->fd, chan, "200 result=-1\n");
	else
		ast_agi_send(agi->fd, chan, "200 result=%s\n", data);
	return RESULT_SUCCESS;
}

static int handle_setcontext(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_channel_context_set(chan, argv[2]);
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setextension(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_channel_exten_set(chan, argv[2]);
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setpriority(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int pri;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	if (sscanf(argv[2], "%30d", &pri) != 1) {
		pri = ast_findlabel_extension(chan, ast_channel_context(chan), ast_channel_exten(chan), argv[2],
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL));
		if (pri < 1)
			return RESULT_SHOWUSAGE;
	}

	ast_explicit_goto(chan, NULL, NULL, pri);
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_recordfile(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	struct ast_filestream *fs;
	struct ast_frame *f;
	struct timeval start;
	long sample_offset = 0;
	int res = 0;
	int ms;

	struct ast_dsp *sildet=NULL;         /* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int silence = 0;                /* amount of silence to allow */
	int gotsilence = 0;             /* did we timeout for silence? */
	char *silencestr = NULL;
	struct ast_format rfmt;
	ast_format_clear(&rfmt);

	/* XXX EAGI FIXME XXX */

	if (argc < 6)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[5], "%30d", &ms) != 1)
		return RESULT_SHOWUSAGE;

	if (argc > 6)
		silencestr = strchr(argv[6],'s');
	if ((argc > 7) && (!silencestr))
		silencestr = strchr(argv[7],'s');
	if ((argc > 8) && (!silencestr))
		silencestr = strchr(argv[8],'s');

	if (silencestr) {
		if (strlen(silencestr) > 2) {
			if ((silencestr[0] == 's') && (silencestr[1] == '=')) {
				silencestr++;
				silencestr++;
				if (silencestr)
					silence = atoi(silencestr);
				if (silence > 0)
					silence *= 1000;
			}
		}
	}

	if (silence > 0) {
		ast_format_copy(&rfmt, ast_channel_readformat(chan));
		res = ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			ast_agi_send(agi->fd, chan, "200 result=%d\n", res);
			return RESULT_FAILURE;
		}
		sildet = ast_dsp_new();
		if (!sildet) {
			ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
			ast_agi_send(agi->fd, chan, "200 result=-1\n");
			return RESULT_FAILURE;
		}
		ast_dsp_set_threshold(sildet, ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE));
	}
	
	/* backward compatibility, if no offset given, arg[6] would have been
	 * caught below and taken to be a beep, else if it is a digit then it is a
	 * offset */
	if ((argc >6) && (sscanf(argv[6], "%30ld", &sample_offset) != 1) && (!strchr(argv[6], '=')))
		res = ast_streamfile(chan, "beep", ast_channel_language(chan));

	if ((argc > 7) && (!strchr(argv[7], '=')))
		res = ast_streamfile(chan, "beep", ast_channel_language(chan));

	if (!res)
		res = ast_waitstream(chan, argv[4]);
	if (res) {
		ast_agi_send(agi->fd, chan, "200 result=%d (randomerror) endpos=%ld\n", res, sample_offset);
	} else {
		fs = ast_writefile(argv[2], argv[3], NULL, O_CREAT | O_WRONLY | (sample_offset ? O_APPEND : 0), 0, AST_FILE_MODE);
		if (!fs) {
			res = -1;
			ast_agi_send(agi->fd, chan, "200 result=%d (writefile)\n", res);
			if (sildet)
				ast_dsp_free(sildet);
			return RESULT_FAILURE;
		}

		/* Request a video update */
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);

		ast_channel_stream_set(chan, fs);
		ast_applystream(chan,fs);
		/* really should have checks */
		ast_seekstream(fs, sample_offset, SEEK_SET);
		ast_truncstream(fs);

		start = ast_tvnow();
		while ((ms < 0) || ast_tvdiff_ms(ast_tvnow(), start) < ms) {
			res = ast_waitfor(chan, ms - ast_tvdiff_ms(ast_tvnow(), start));
			if (res < 0) {
				ast_closestream(fs);
				ast_agi_send(agi->fd, chan, "200 result=%d (waitfor) endpos=%ld\n", res,sample_offset);
				if (sildet)
					ast_dsp_free(sildet);
				return RESULT_FAILURE;
			}
			f = ast_read(chan);
			if (!f) {
				ast_agi_send(agi->fd, chan, "200 result=%d (hangup) endpos=%ld\n", -1, sample_offset);
				ast_closestream(fs);
				if (sildet)
					ast_dsp_free(sildet);
				return RESULT_FAILURE;
			}
			switch(f->frametype) {
			case AST_FRAME_DTMF:
				if (strchr(argv[4], f->subclass.integer)) {
					/* This is an interrupting chracter, so rewind to chop off any small
					   amount of DTMF that may have been recorded
					*/
					ast_stream_rewind(fs, 200);
					ast_truncstream(fs);
					sample_offset = ast_tellstream(fs);
					ast_agi_send(agi->fd, chan, "200 result=%d (dtmf) endpos=%ld\n", f->subclass.integer, sample_offset);
					ast_closestream(fs);
					ast_frfree(f);
					if (sildet)
						ast_dsp_free(sildet);
					return RESULT_SUCCESS;
				}
				break;
			case AST_FRAME_VOICE:
				ast_writestream(fs, f);
				/* this is a safe place to check progress since we know that fs
				 * is valid after a write, and it will then have our current
				 * location */
				sample_offset = ast_tellstream(fs);
				if (silence > 0) {
					dspsilence = 0;
					ast_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence) {
						totalsilence = dspsilence;
					} else {
						totalsilence = 0;
					}
					if (totalsilence > silence) {
						/* Ended happily with silence */
						gotsilence = 1;
						break;
					}
				}
				break;
			case AST_FRAME_VIDEO:
				ast_writestream(fs, f);
			default:
				/* Ignore all other frames */
				break;
			}
			ast_frfree(f);
			if (gotsilence)
				break;
		}

		if (gotsilence) {
			ast_stream_rewind(fs, silence-1000);
			ast_truncstream(fs);
			sample_offset = ast_tellstream(fs);
		}
		ast_agi_send(agi->fd, chan, "200 result=%d (timeout) endpos=%ld\n", res, sample_offset);
		ast_closestream(fs);
	}

	if (silence > 0) {
		res = ast_set_read_format(chan, &rfmt);
		if (res)
			ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", ast_channel_name(chan));
		ast_dsp_free(sildet);
	}

	return RESULT_SUCCESS;
}

static int handle_autohangup(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	double timeout;
	struct timeval whentohangup = { 0, 0 };

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30lf", &timeout) != 1)
		return RESULT_SHOWUSAGE;
	if (timeout < 0)
		timeout = 0;
	if (timeout) {
		whentohangup.tv_sec = timeout;
		whentohangup.tv_usec = (timeout - whentohangup.tv_sec) * 1000000.0;
	}
	ast_channel_setwhentohangup_tv(chan, whentohangup);
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_hangup(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	struct ast_channel *c;

	if (argc == 1) {
		/* no argument: hangup the current channel */
		ast_set_hangupsource(chan, "dialplan/agi", 0);
		ast_softhangup(chan,AST_SOFTHANGUP_EXPLICIT);
		ast_agi_send(agi->fd, chan, "200 result=1\n");
		return RESULT_SUCCESS;
	} else if (argc == 2) {
		/* one argument: look for info on the specified channel */
		if ((c = ast_channel_get_by_name(argv[1]))) {
			/* we have a matching channel */
			ast_set_hangupsource(c, "dialplan/agi", 0);
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			c = ast_channel_unref(c);
			ast_agi_send(agi->fd, chan, "200 result=1\n");
			return RESULT_SUCCESS;
		}
		/* if we get this far no channel name matched the argument given */
		ast_agi_send(agi->fd, chan, "200 result=-1\n");
		return RESULT_SUCCESS;
	} else {
		return RESULT_SHOWUSAGE;
	}
}

static int handle_exec(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res, workaround;
	struct ast_app *app_to_exec;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	ast_verb(3, "AGI Script Executing Application: (%s) Options: (%s)\n", argv[1], argc >= 3 ? argv[2] : "");

	if ((app_to_exec = pbx_findapp(argv[1]))) {
		if (!(workaround = ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_WORKAROUNDS))) {
			ast_set_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_WORKAROUNDS);
		}
		if (ast_compat_res_agi && argc >= 3 && !ast_strlen_zero(argv[2])) {
			char *compat = ast_alloca(strlen(argv[2]) * 2 + 1), *cptr;
			const char *vptr;
			for (cptr = compat, vptr = argv[2]; *vptr; vptr++) {
				if (*vptr == ',') {
					*cptr++ = '\\';
					*cptr++ = ',';
				} else if (*vptr == '|') {
					*cptr++ = ',';
				} else {
					*cptr++ = *vptr;
				}
			}
			*cptr = '\0';
			res = pbx_exec(chan, app_to_exec, compat);
		} else {
			res = pbx_exec(chan, app_to_exec, argc == 2 ? "" : argv[2]);
		}
		if (!workaround) {
			ast_clear_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_WORKAROUNDS);
		}
	} else {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", argv[1]);
		res = -2;
	}
	ast_agi_send(agi->fd, chan, "200 result=%d\n", res);

	/* Even though this is wrong, users are depending upon this result. */
	return res;
}

static int handle_setcallerid(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	char tmp[256]="";
	char *l = NULL, *n = NULL;

	if (argv[2]) {
		ast_copy_string(tmp, argv[2], sizeof(tmp));
		ast_callerid_parse(tmp, &n, &l);
		if (l)
			ast_shrink_phone_number(l);
		else
			l = "";
		if (!n)
			n = "";
		ast_set_callerid(chan, l, n, NULL);
	}

	ast_agi_send(agi->fd, chan, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_channelstatus(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	struct ast_channel *c;
	if (argc == 2) {
		/* no argument: supply info on the current channel */
		ast_agi_send(agi->fd, chan, "200 result=%u\n", ast_channel_state(chan));
		return RESULT_SUCCESS;
	} else if (argc == 3) {
		/* one argument: look for info on the specified channel */
		if ((c = ast_channel_get_by_name(argv[2]))) {
			ast_agi_send(agi->fd, chan, "200 result=%u\n", ast_channel_state(c));
			c = ast_channel_unref(c);
			return RESULT_SUCCESS;
		}
		/* if we get this far no channel name matched the argument given */
		ast_agi_send(agi->fd, chan, "200 result=-1\n");
		return RESULT_SUCCESS;
	} else {
		return RESULT_SHOWUSAGE;
	}
}

static int handle_setvariable(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argv[3])
		pbx_builtin_setvar_helper(chan, argv[2], argv[3]);

	ast_agi_send(agi->fd, chan, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_getvariable(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	char *ret;
	char tempstr[1024] = "";

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	/* check if we want to execute an ast_custom_function */
	if (!ast_strlen_zero(argv[2]) && (argv[2][strlen(argv[2]) - 1] == ')')) {
		ret = ast_func_read(chan, argv[2], tempstr, sizeof(tempstr)) ? NULL : tempstr;
	} else {
		pbx_retrieve_variable(chan, argv[2], &ret, tempstr, sizeof(tempstr), NULL);
	}

	if (ret)
		ast_agi_send(agi->fd, chan, "200 result=1 (%s)\n", ret);
	else
		ast_agi_send(agi->fd, chan, "200 result=0\n");

	return RESULT_SUCCESS;
}

static int handle_getvariablefull(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	struct ast_channel *chan2 = NULL;

	if (argc != 4 && argc != 5) {
		return RESULT_SHOWUSAGE;
	}

	if (argc == 5) {
		chan2 = ast_channel_get_by_name(argv[4]);
	} else {
		chan2 = ast_channel_ref(chan);
	}

	if (chan2) {
		struct ast_str *str = ast_str_create(16);
		if (!str) {
			ast_agi_send(agi->fd, chan, "200 result=0\n");
			return RESULT_SUCCESS;
		}
		ast_str_substitute_variables(&str, 0, chan2, argv[3]);
		ast_agi_send(agi->fd, chan, "200 result=1 (%s)\n", ast_str_buffer(str));
		ast_free(str);
	} else {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	}

	if (chan2) {
		chan2 = ast_channel_unref(chan2);
	}

	return RESULT_SUCCESS;
}

static int handle_verbose(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int level = 0;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	if (argv[2])
		sscanf(argv[2], "%30d", &level);

	ast_verb(level, "%s: %s\n", ast_channel_data(chan), argv[1]);

	ast_agi_send(agi->fd, chan, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_dbget(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;
	struct ast_str *buf;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	if (!(buf = ast_str_create(16))) {
		ast_agi_send(agi->fd, chan, "200 result=-1\n");
		return RESULT_SUCCESS;
	}

	do {
		res = ast_db_get(argv[2], argv[3], ast_str_buffer(buf), ast_str_size(buf));
		ast_str_update(buf);
		if (ast_str_strlen(buf) < ast_str_size(buf) - 1) {
			break;
		}
		if (ast_str_make_space(&buf, ast_str_size(buf) * 2)) {
			break;
		}
	} while (1);
	
	if (res)
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	else
		ast_agi_send(agi->fd, chan, "200 result=1 (%s)\n", ast_str_buffer(buf));

	ast_free(buf);
	return RESULT_SUCCESS;
}

static int handle_dbput(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 5)
		return RESULT_SHOWUSAGE;
	res = ast_db_put(argv[2], argv[3], argv[4]);
	ast_agi_send(agi->fd, chan, "200 result=%c\n", res ? '0' : '1');
	return RESULT_SUCCESS;
}

static int handle_dbdel(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int res;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_del(argv[2], argv[3]);
	ast_agi_send(agi->fd, chan, "200 result=%c\n", res ? '0' : '1');
	return RESULT_SUCCESS;
}

static int handle_dbdeltree(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	int num_deleted;

	if ((argc < 3) || (argc > 4)) {
		return RESULT_SHOWUSAGE;
	}
	if (argc == 4) {
		num_deleted = ast_db_deltree(argv[2], argv[3]);
	} else {
		num_deleted = ast_db_deltree(argv[2], NULL);
	}

	ast_agi_send(agi->fd, chan, "200 result=%c\n", num_deleted > 0 ? '0' : '1');
	return RESULT_SUCCESS;
}

static char *handle_cli_agi_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "agi set debug [on|off]";
		e->usage =
			"Usage: agi set debug [on|off]\n"
			"       Enables/disables dumping of AGI transactions for\n"
			"       debugging purposes.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (strncasecmp(a->argv[3], "off", 3) == 0) {
		agidebug = 0;
	} else if (strncasecmp(a->argv[3], "on", 2) == 0) {
		agidebug = 1;
	} else {
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, "AGI Debugging %sabled\n", agidebug ? "En" : "Dis");
	return CLI_SUCCESS;
}

static int handle_noop(struct ast_channel *chan, AGI *agi, int arg, const char * const argv[])
{
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setmusic(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	if (!strncasecmp(argv[2], "on", 2))
		ast_moh_start(chan, argc > 3 ? argv[3] : NULL, NULL);
	else if (!strncasecmp(argv[2], "off", 3))
		ast_moh_stop(chan);
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_speechcreate(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	struct ast_format_cap *cap;
	struct ast_format tmpfmt;

	/* If a structure already exists, return an error */
	if (agi->speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	if (!(cap = ast_format_cap_alloc_nolock())) {
		return RESULT_FAILURE;
	}
	ast_format_cap_add(cap, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));
	if ((agi->speech = ast_speech_new(argv[2], cap))) {
		ast_agi_send(agi->fd, chan, "200 result=1\n");
	} else {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	}
	cap = ast_format_cap_destroy(cap);

	return RESULT_SUCCESS;
}

static int handle_speechset(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	/* Check for minimum arguments */
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	/* Check to make sure speech structure exists */
	if (!agi->speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	ast_speech_change(agi->speech, argv[2], argv[3]);
	ast_agi_send(agi->fd, chan, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_speechdestroy(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (agi->speech) {
		ast_speech_destroy(agi->speech);
		agi->speech = NULL;
		ast_agi_send(agi->fd, chan, "200 result=1\n");
	} else {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	}

	return RESULT_SUCCESS;
}

static int handle_speechloadgrammar(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argc != 5)
		return RESULT_SHOWUSAGE;

	if (!agi->speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	if (ast_speech_grammar_load(agi->speech, argv[3], argv[4]))
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	else
		ast_agi_send(agi->fd, chan, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_speechunloadgrammar(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	if (!agi->speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	if (ast_speech_grammar_unload(agi->speech, argv[3]))
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	else
		ast_agi_send(agi->fd, chan, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_speechactivategrammar(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	if (!agi->speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	if (ast_speech_grammar_activate(agi->speech, argv[3]))
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	else
		ast_agi_send(agi->fd, chan, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_speechdeactivategrammar(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;

	if (!agi->speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	if (ast_speech_grammar_deactivate(agi->speech, argv[3]))
		ast_agi_send(agi->fd, chan, "200 result=0\n");
	else
		ast_agi_send(agi->fd, chan, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int speech_streamfile(struct ast_channel *chan, const char *filename, const char *preflang, int offset)
{
	struct ast_filestream *fs = NULL;

	if (!(fs = ast_openstream(chan, filename, preflang)))
		return -1;

	if (offset)
		ast_seekstream(fs, offset, SEEK_SET);

	if (ast_applystream(chan, fs))
		return -1;

	if (ast_playstream(fs))
		return -1;

	return 0;
}

static int handle_speechrecognize(struct ast_channel *chan, AGI *agi, int argc, const char * const argv[])
{
	struct ast_speech *speech = agi->speech;
	const char *prompt;
	char dtmf = 0, tmp[4096] = "", *buf = tmp;
	int timeout = 0, offset = 0, res = 0, i = 0;
	struct ast_format old_read_format;
	long current_offset = 0;
	const char *reason = NULL;
	struct ast_frame *fr = NULL;
	struct ast_speech_result *result = NULL;
	size_t left = sizeof(tmp);
	time_t start = 0, current;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (!speech) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	prompt = argv[2];
	timeout = atoi(argv[3]);

	/* If offset is specified then convert from text to integer */
	if (argc == 5)
		offset = atoi(argv[4]);

	/* We want frames coming in signed linear */
	ast_format_copy(&old_read_format, ast_channel_readformat(chan));
	if (ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR)) {
		ast_agi_send(agi->fd, chan, "200 result=0\n");
		return RESULT_SUCCESS;
	}

	/* Setup speech structure */
	if (speech->state == AST_SPEECH_STATE_NOT_READY || speech->state == AST_SPEECH_STATE_DONE) {
		ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);
		ast_speech_start(speech);
	}

	/* Start playing prompt */
	speech_streamfile(chan, prompt, ast_channel_language(chan), offset);

	/* Go into loop reading in frames, passing to speech thingy, checking for hangup, all that jazz */
	while (ast_strlen_zero(reason)) {
		/* Run scheduled items */
                ast_sched_runq(ast_channel_sched(chan));

		/* See maximum time of waiting */
		if ((res = ast_sched_wait(ast_channel_sched(chan))) < 0)
			res = 1000;

		/* Wait for frame */
		if (ast_waitfor(chan, res) > 0) {
			if (!(fr = ast_read(chan))) {
				reason = "hangup";
				break;
			}
		}

		/* Perform timeout check */
		if ((timeout > 0) && (start > 0)) {
			time(&current);
			if ((current - start) >= timeout) {
				reason = "timeout";
				if (fr)
					ast_frfree(fr);
				break;
			}
		}

		/* Check the speech structure for any changes */
		ast_mutex_lock(&speech->lock);

		/* See if we need to quiet the audio stream playback */
		if (ast_test_flag(speech, AST_SPEECH_QUIET) && ast_channel_stream(chan)) {
			current_offset = ast_tellstream(ast_channel_stream(chan));
			ast_stopstream(chan);
			ast_clear_flag(speech, AST_SPEECH_QUIET);
		}

		/* Check each state */
		switch (speech->state) {
		case AST_SPEECH_STATE_READY:
			/* If the stream is done, start timeout calculation */
			if ((timeout > 0) && start == 0 && ((!ast_channel_stream(chan)) || (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL))) {
				ast_stopstream(chan);
				time(&start);
			}
			/* Write audio frame data into speech engine if possible */
			if (fr && fr->frametype == AST_FRAME_VOICE)
				ast_speech_write(speech, fr->data.ptr, fr->datalen);
			break;
		case AST_SPEECH_STATE_WAIT:
			/* Cue waiting sound if not already playing */
			if ((!ast_channel_stream(chan)) || (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL)) {
				ast_stopstream(chan);
				/* If a processing sound exists, or is not none - play it */
				if (!ast_strlen_zero(speech->processing_sound) && strcasecmp(speech->processing_sound, "none"))
					speech_streamfile(chan, speech->processing_sound, ast_channel_language(chan), 0);
			}
			break;
		case AST_SPEECH_STATE_DONE:
			/* Get the results */
			speech->results = ast_speech_results_get(speech);
			/* Change state to not ready */
			ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);
			reason = "speech";
			break;
		default:
			break;
		}
		ast_mutex_unlock(&speech->lock);

		/* Check frame for DTMF or hangup */
		if (fr) {
			if (fr->frametype == AST_FRAME_DTMF) {
				reason = "dtmf";
				dtmf = fr->subclass.integer;
			} else if (fr->frametype == AST_FRAME_CONTROL && fr->subclass.integer == AST_CONTROL_HANGUP) {
				reason = "hangup";
			}
			ast_frfree(fr);
		}
	}

	if (!strcasecmp(reason, "speech")) {
		/* Build string containing speech results */
                for (result = speech->results; result; result = AST_LIST_NEXT(result, list)) {
			/* Build result string */
			ast_build_string(&buf, &left, "%sscore%d=%d text%d=\"%s\" grammar%d=%s", (i > 0 ? " " : ""), i, result->score, i, result->text, i, result->grammar);
                        /* Increment result count */
			i++;
		}
                /* Print out */
		ast_agi_send(agi->fd, chan, "200 result=1 (speech) endpos=%ld results=%d %s\n", current_offset, i, tmp);
	} else if (!strcasecmp(reason, "dtmf")) {
		ast_agi_send(agi->fd, chan, "200 result=1 (digit) digit=%c endpos=%ld\n", dtmf, current_offset);
	} else if (!strcasecmp(reason, "hangup") || !strcasecmp(reason, "timeout")) {
		ast_agi_send(agi->fd, chan, "200 result=1 (%s) endpos=%ld\n", reason, current_offset);
	} else {
		ast_agi_send(agi->fd, chan, "200 result=0 endpos=%ld\n", current_offset);
	}

	return RESULT_SUCCESS;
}

/*!
 * \brief AGI commands list
 */
static struct agi_command commands[] = {
	{ { "answer", NULL }, handle_answer, NULL, NULL, 0 },
	{ { "asyncagi", "break", NULL }, handle_asyncagi_break, NULL, NULL, 1 },
	{ { "channel", "status", NULL }, handle_channelstatus, NULL, NULL, 0 },
	{ { "database", "del", NULL }, handle_dbdel, NULL, NULL, 1 },
	{ { "database", "deltree", NULL }, handle_dbdeltree, NULL, NULL, 1 },
	{ { "database", "get", NULL }, handle_dbget, NULL, NULL, 1 },
	{ { "database", "put", NULL }, handle_dbput, NULL, NULL, 1 },
	{ { "exec", NULL }, handle_exec, NULL, NULL, 1 },
	{ { "get", "data", NULL }, handle_getdata, NULL, NULL, 0 },
	{ { "get", "full", "variable", NULL }, handle_getvariablefull, NULL, NULL, 1 },
	{ { "get", "option", NULL }, handle_getoption, NULL, NULL, 0 },
	{ { "get", "variable", NULL }, handle_getvariable, NULL, NULL, 1 },
	{ { "hangup", NULL }, handle_hangup, NULL, NULL, 0 },
	{ { "noop", NULL }, handle_noop, NULL, NULL, 1 },
	{ { "receive", "char", NULL }, handle_recvchar, NULL, NULL, 0 },
	{ { "receive", "text", NULL }, handle_recvtext, NULL, NULL, 0 },
	{ { "record", "file", NULL }, handle_recordfile, NULL, NULL, 0 }, 
	{ { "say", "alpha", NULL }, handle_sayalpha, NULL, NULL, 0},
	{ { "say", "digits", NULL }, handle_saydigits, NULL, NULL, 0 },
	{ { "say", "number", NULL }, handle_saynumber, NULL, NULL, 0 },
	{ { "say", "phonetic", NULL }, handle_sayphonetic, NULL, NULL, 0}, 
	{ { "say", "date", NULL }, handle_saydate, NULL, NULL, 0}, 
	{ { "say", "time", NULL }, handle_saytime, NULL, NULL, 0}, 
	{ { "say", "datetime", NULL }, handle_saydatetime, NULL, NULL, 0},
	{ { "send", "image", NULL }, handle_sendimage, NULL, NULL, 0}, 
	{ { "send", "text", NULL }, handle_sendtext, NULL, NULL, 0},
	{ { "set", "autohangup", NULL }, handle_autohangup, NULL, NULL, 0},
	{ { "set", "callerid", NULL }, handle_setcallerid, NULL, NULL, 0},
	{ { "set", "context", NULL }, handle_setcontext, NULL, NULL, 0},
	{ { "set", "extension", NULL }, handle_setextension, NULL, NULL, 0},
	{ { "set", "music", NULL }, handle_setmusic, NULL, NULL, 0 },
	{ { "set", "priority", NULL }, handle_setpriority, NULL, NULL, 0 },
	{ { "set", "variable", NULL }, handle_setvariable, NULL, NULL, 1 },
	{ { "stream", "file", NULL }, handle_streamfile, NULL, NULL, 0 },
	{ { "control", "stream", "file", NULL }, handle_controlstreamfile, NULL, NULL, 0 },
	{ { "tdd", "mode", NULL }, handle_tddmode, NULL, NULL, 0 },
	{ { "verbose", NULL }, handle_verbose, NULL, NULL, 1 },
	{ { "wait", "for", "digit", NULL }, handle_waitfordigit, NULL, NULL, 0 },
	{ { "speech", "create", NULL }, handle_speechcreate, NULL, NULL, 0 },
	{ { "speech", "set", NULL }, handle_speechset, NULL, NULL, 0 },
	{ { "speech", "destroy", NULL }, handle_speechdestroy, NULL, NULL, 1 },
	{ { "speech", "load", "grammar", NULL }, handle_speechloadgrammar, NULL, NULL, 0 },
	{ { "speech", "unload", "grammar", NULL }, handle_speechunloadgrammar, NULL, NULL, 1 },
	{ { "speech", "activate", "grammar", NULL }, handle_speechactivategrammar, NULL, NULL, 0 },
	{ { "speech", "deactivate", "grammar", NULL }, handle_speechdeactivategrammar, NULL, NULL, 0 },
	{ { "speech", "recognize", NULL }, handle_speechrecognize, NULL, NULL, 0 },
};

static AST_RWLIST_HEAD_STATIC(agi_commands, agi_command);

static char *help_workhorse(int fd, const char * const match[])
{
	char fullcmd[MAX_CMD_LEN], matchstr[MAX_CMD_LEN];
	struct agi_command *e;

	if (match)
		ast_join(matchstr, sizeof(matchstr), match);

	ast_cli(fd, "%5.5s %30.30s   %s\n","Dead","Command","Description");
	AST_RWLIST_RDLOCK(&agi_commands);
	AST_RWLIST_TRAVERSE(&agi_commands, e, list) {
		if (!e->cmda[0])
			break;
		/* Hide commands that start with '_' */
		if ((e->cmda[0])[0] == '_')
			continue;
		ast_join(fullcmd, sizeof(fullcmd), e->cmda);
		if (match && strncasecmp(matchstr, fullcmd, strlen(matchstr)))
			continue;
		ast_cli(fd, "%5.5s %30.30s   %s\n", e->dead ? "Yes" : "No" , fullcmd, S_OR(e->summary, "Not available"));
	}
	AST_RWLIST_UNLOCK(&agi_commands);

	return CLI_SUCCESS;
}

int AST_OPTIONAL_API_NAME(ast_agi_register)(struct ast_module *mod, agi_command *cmd)
{
	char fullcmd[MAX_CMD_LEN];

	ast_join(fullcmd, sizeof(fullcmd), cmd->cmda);

	if (!find_command(cmd->cmda, 1)) {
		*((enum ast_doc_src *) &cmd->docsrc) = AST_STATIC_DOC;
		if (ast_strlen_zero(cmd->summary) && ast_strlen_zero(cmd->usage)) {
#ifdef AST_XML_DOCS
			*((char **) &cmd->summary) = ast_xmldoc_build_synopsis("agi", fullcmd, NULL);
			*((char **) &cmd->usage) = ast_xmldoc_build_description("agi", fullcmd, NULL);
			*((char **) &cmd->syntax) = ast_xmldoc_build_syntax("agi", fullcmd, NULL);
			*((char **) &cmd->seealso) = ast_xmldoc_build_seealso("agi", fullcmd, NULL);
			*((enum ast_doc_src *) &cmd->docsrc) = AST_XML_DOC;
#endif
#ifndef HAVE_NULLSAFE_PRINTF
			if (!cmd->summary) {
				*((char **) &cmd->summary) = ast_strdup("");
			}
			if (!cmd->usage) {
				*((char **) &cmd->usage) = ast_strdup("");
			}
			if (!cmd->syntax) {
				*((char **) &cmd->syntax) = ast_strdup("");
			}
			if (!cmd->seealso) {
				*((char **) &cmd->seealso) = ast_strdup("");
			}
#endif
		}

		cmd->mod = mod;
		AST_RWLIST_WRLOCK(&agi_commands);
		AST_LIST_INSERT_TAIL(&agi_commands, cmd, list);
		AST_RWLIST_UNLOCK(&agi_commands);
		if (mod != ast_module_info->self)
			ast_module_ref(ast_module_info->self);
		ast_verb(2, "AGI Command '%s' registered\n",fullcmd);
		return 1;
	} else {
		ast_log(LOG_WARNING, "Command already registered!\n");
		return 0;
	}
}

int AST_OPTIONAL_API_NAME(ast_agi_unregister)(struct ast_module *mod, agi_command *cmd)
{
	struct agi_command *e;
	int unregistered = 0;
	char fullcmd[MAX_CMD_LEN];

	ast_join(fullcmd, sizeof(fullcmd), cmd->cmda);

	AST_RWLIST_WRLOCK(&agi_commands);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&agi_commands, e, list) {
		if (cmd == e) {
			AST_RWLIST_REMOVE_CURRENT(list);
			if (mod != ast_module_info->self)
				ast_module_unref(ast_module_info->self);
#ifdef AST_XML_DOCS
			if (e->docsrc == AST_XML_DOC) {
				ast_free((char *) e->summary);
				ast_free((char *) e->usage);
				ast_free((char *) e->syntax);
				ast_free((char *) e->seealso);
				*((char **) &e->summary) = NULL;
				*((char **) &e->usage) = NULL;
				*((char **) &e->syntax) = NULL;
				*((char **) &e->seealso) = NULL;
			}
#endif
			unregistered=1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&agi_commands);
	if (unregistered)
		ast_verb(2, "AGI Command '%s' unregistered\n",fullcmd);
	else
		ast_log(LOG_WARNING, "Unable to unregister command: '%s'!\n",fullcmd);
	return unregistered;
}

int AST_OPTIONAL_API_NAME(ast_agi_register_multiple)(struct ast_module *mod, struct agi_command *cmd, unsigned int len)
{
	unsigned int i, x = 0;

	for (i = 0; i < len; i++) {
		if (ast_agi_register(mod, cmd + i) == 1) {
			x++;
			continue;
		}

		/* registration failed, unregister everything
		   that had been registered up to that point
		*/
		for (; x > 0; x--) {
			/* we are intentionally ignoring the
			   result of ast_agi_unregister() here,
			   but it should be safe to do so since
			   we just registered these commands and
			   the only possible way for unregistration
			   to fail is if the command is not
			   registered
			*/
			(void) ast_agi_unregister(mod, cmd + x - 1);
		}
		return -1;
	}

	return 0;
}

int AST_OPTIONAL_API_NAME(ast_agi_unregister_multiple)(struct ast_module *mod, struct agi_command *cmd, unsigned int len)
{
	unsigned int i;
	int res = 0;

	for (i = 0; i < len; i++) {
		/* remember whether any of the unregistration
		   attempts failed... there is no recourse if
		   any of them do
		*/
		res |= ast_agi_unregister(mod, cmd + i);
	}

	return res;
}

static agi_command *find_command(const char * const cmds[], int exact)
{
	int y, match;
	struct agi_command *e;

	AST_RWLIST_RDLOCK(&agi_commands);
	AST_RWLIST_TRAVERSE(&agi_commands, e, list) {
		if (!e->cmda[0])
			break;
		/* start optimistic */
		match = 1;
		for (y = 0; match && cmds[y]; y++) {
			/* If there are no more words in the command (and we're looking for
			   an exact match) or there is a difference between the two words,
			   then this is not a match */
			if (!e->cmda[y] && !exact)
				break;
			/* don't segfault if the next part of a command doesn't exist */
			if (!e->cmda[y]) {
				AST_RWLIST_UNLOCK(&agi_commands);
				return NULL;
			}
			if (strcasecmp(e->cmda[y], cmds[y]))
				match = 0;
		}
		/* If more words are needed to complete the command then this is not
		   a candidate (unless we're looking for a really inexact answer  */
		if ((exact > -1) && e->cmda[y])
			match = 0;
		if (match) {
			AST_RWLIST_UNLOCK(&agi_commands);
			return e;
		}
	}
	AST_RWLIST_UNLOCK(&agi_commands);
	return NULL;
}

static int parse_args(char *s, int *max, const char *argv[])
{
	int x = 0, quoted = 0, escaped = 0, whitespace = 1;
	char *cur;

	cur = s;
	while(*s) {
		switch(*s) {
		case '"':
			/* If it's escaped, put a literal quote */
			if (escaped)
				goto normal;
			else
				quoted = !quoted;
			if (quoted && whitespace) {
				/* If we're starting a quote, coming off white space start a new word, too */
				argv[x++] = cur;
				whitespace=0;
			}
			escaped = 0;
		break;
		case ' ':
		case '\t':
			if (!quoted && !escaped) {
				/* If we're not quoted, mark this as whitespace, and
				   end the previous argument */
				whitespace = 1;
				*(cur++) = '\0';
			} else
				/* Otherwise, just treat it as anything else */
				goto normal;
			break;
		case '\\':
			/* If we're escaped, print a literal, otherwise enable escaping */
			if (escaped) {
				goto normal;
			} else {
				escaped=1;
			}
			break;
		default:
normal:
			if (whitespace) {
				if (x >= MAX_ARGS -1) {
					ast_log(LOG_WARNING, "Too many arguments, truncating\n");
					break;
				}
				/* Coming off of whitespace, start the next argument */
				argv[x++] = cur;
				whitespace=0;
			}
			*(cur++) = *s;
			escaped=0;
		}
		s++;
	}
	/* Null terminate */
	*(cur++) = '\0';
	argv[x] = NULL;
	*max = x;
	return 0;
}

static enum agi_result agi_handle_command(struct ast_channel *chan, AGI *agi, char *buf, int dead)
{
	const char *argv[MAX_ARGS];
	int argc = MAX_ARGS;
	int res;
	agi_command *c;
	const char *ami_res;
	char *ami_cmd = ast_strdupa(buf);
	int command_id = ast_random();
	int resultcode;

	manager_event(EVENT_FLAG_AGI, "AGIExec",
			"SubEvent: Start\r\n"
			"Channel: %s\r\n"
			"CommandId: %d\r\n"
			"Command: %s\r\n", ast_channel_name(chan), command_id, ami_cmd);
	parse_args(buf, &argc, argv);
	c = find_command(argv, 0);
	if (c && (!dead || (dead && c->dead))) {
		/* if this command wasn't registered by res_agi, be sure to usecount
		the module we are using */
		if (c->mod != ast_module_info->self)
			ast_module_ref(c->mod);
		/* If the AGI command being executed is an actual application (using agi exec)
		the app field will be updated in pbx_exec via handle_exec */
		if (ast_channel_cdr(chan) && !ast_check_hangup(chan) && strcasecmp(argv[0], "EXEC"))
			ast_cdr_setapp(ast_channel_cdr(chan), "AGI", buf);

		res = c->handler(chan, agi, argc, argv);
		if (c->mod != ast_module_info->self)
			ast_module_unref(c->mod);
		switch (res) {
		case RESULT_SHOWUSAGE:
			ami_res = "Usage";
			resultcode = 520;
			break;
		case RESULT_FAILURE:
			ami_res = "Failure";
			resultcode = -1;
			break;
		case ASYNC_AGI_BREAK:
		case RESULT_SUCCESS:
			ami_res = "Success";
			resultcode = 200;
			break;
		default:
			ami_res = "Unknown Result";
			resultcode = 200;
			break;
		}
		manager_event(EVENT_FLAG_AGI, "AGIExec",
				"SubEvent: End\r\n"
				"Channel: %s\r\n"
				"CommandId: %d\r\n"
				"Command: %s\r\n"
				"ResultCode: %d\r\n"
				"Result: %s\r\n", ast_channel_name(chan), command_id, ami_cmd, resultcode, ami_res);
		switch (res) {
		case RESULT_SHOWUSAGE:
			if (ast_strlen_zero(c->usage)) {
				ast_agi_send(agi->fd, chan, "520 Invalid command syntax.  Proper usage not available.\n");
			} else {
				ast_agi_send(agi->fd, chan, "520-Invalid command syntax.  Proper usage follows:\n");
				ast_agi_send(agi->fd, chan, "%s", c->usage);
				ast_agi_send(agi->fd, chan, "520 End of proper usage.\n");
			}
			break;
		case ASYNC_AGI_BREAK:
			return AGI_RESULT_SUCCESS_ASYNC;
		case RESULT_FAILURE:
			/* The RESULT_FAILURE code is usually because the channel hungup. */
			return AGI_RESULT_FAILURE;
		default:
			break;
		}
	} else if (c) {
		ast_agi_send(agi->fd, chan, "511 Command Not Permitted on a dead channel\n");
		manager_event(EVENT_FLAG_AGI, "AGIExec",
				"SubEvent: End\r\n"
				"Channel: %s\r\n"
				"CommandId: %d\r\n"
				"Command: %s\r\n"
				"ResultCode: 511\r\n"
				"Result: Command not permitted on a dead channel\r\n", ast_channel_name(chan), command_id, ami_cmd);
	} else {
		ast_agi_send(agi->fd, chan, "510 Invalid or unknown command\n");
		manager_event(EVENT_FLAG_AGI, "AGIExec",
				"SubEvent: End\r\n"
				"Channel: %s\r\n"
				"CommandId: %d\r\n"
				"Command: %s\r\n"
				"ResultCode: 510\r\n"
				"Result: Invalid or unknown command\r\n", ast_channel_name(chan), command_id, ami_cmd);
	}
	return AGI_RESULT_SUCCESS;
}
static enum agi_result run_agi(struct ast_channel *chan, char *request, AGI *agi, int pid, int *status, int dead, int argc, char *argv[])
{
	struct ast_channel *c;
	int outfd;
	int ms;
	int needhup = 0;
	enum agi_result returnstatus = AGI_RESULT_SUCCESS;
	struct ast_frame *f;
	char buf[AGI_BUF_LEN];
	char *res = NULL;
	FILE *readf;
	/* how many times we'll retry if ast_waitfor_nandfs will return without either
	  channel or file descriptor in case select is interrupted by a system call (EINTR) */
	int retry = AGI_NANDFS_RETRY;
	int send_sighup;
	const char *sighup_str;
	const char *exit_on_hangup_str;
	int exit_on_hangup;
	
	ast_channel_lock(chan);
	sighup_str = pbx_builtin_getvar_helper(chan, "AGISIGHUP");
	send_sighup = !ast_false(sighup_str);
	exit_on_hangup_str = pbx_builtin_getvar_helper(chan, "AGIEXITONHANGUP");
	exit_on_hangup = ast_true(exit_on_hangup_str);
	ast_channel_unlock(chan);

	if (!(readf = fdopen(agi->ctrl, "r"))) {
		ast_log(LOG_WARNING, "Unable to fdopen file descriptor\n");
		if (send_sighup && pid > -1)
			kill(pid, SIGHUP);
		close(agi->ctrl);
		return AGI_RESULT_FAILURE;
	}
	
	setlinebuf(readf);
	setup_env(chan, request, agi->fd, (agi->audio > -1), argc, argv);
	for (;;) {
		if (needhup) {
			needhup = 0;
			dead = 1;
			if (send_sighup) {
				if (pid > -1) {
					kill(pid, SIGHUP);
				} else if (agi->fast) {
					ast_agi_send(agi->fd, chan, "HANGUP\n");
				}
			}
			if (exit_on_hangup) {
				break;
			}
		}
		ms = -1;
		if (dead) {
			c = ast_waitfor_nandfds(&chan, 0, &agi->ctrl, 1, NULL, &outfd, &ms);
		} else if (!ast_check_hangup(chan)) {
			c = ast_waitfor_nandfds(&chan, 1, &agi->ctrl, 1, NULL, &outfd, &ms);
		} else {
			/*
			 * Read the channel control queue until it is dry so we can
			 * switch to dead mode.
			 */
			c = chan;
		}
		if (c) {
			retry = AGI_NANDFS_RETRY;
			/* Idle the channel until we get a command */
			f = ast_read(c);
			if (!f) {
				ast_debug(1, "%s hungup\n", ast_channel_name(chan));
				needhup = 1;
				if (!returnstatus) {
					returnstatus = AGI_RESULT_HANGUP;
				}
			} else {
				/* If it's voice, write it to the audio pipe */
				if ((agi->audio > -1) && (f->frametype == AST_FRAME_VOICE)) {
					/* Write, ignoring errors */
					if (write(agi->audio, f->data.ptr, f->datalen) < 0) {
					}
				}
				ast_frfree(f);
			}
		} else if (outfd > -1) {
			size_t len = sizeof(buf);
			size_t buflen = 0;
			enum agi_result cmd_status;

			retry = AGI_NANDFS_RETRY;
			buf[0] = '\0';

			while (len > 1) {
				res = fgets(buf + buflen, len, readf);
				if (feof(readf))
					break;
				if (ferror(readf) && ((errno != EINTR) && (errno != EAGAIN)))
					break;
				if (res != NULL && !agi->fast)
					break;
				buflen = strlen(buf);
				if (buflen && buf[buflen - 1] == '\n')
					break;
				len = sizeof(buf) - buflen;
				if (agidebug)
					ast_verbose("AGI Rx << temp buffer %s - errno %s\nNo \\n received, checking again.\n", buf, strerror(errno));
			}

			if (!buf[0]) {
				/* Program terminated */
				ast_verb(3, "<%s>AGI Script %s completed, returning %d\n", ast_channel_name(chan), request, returnstatus);
				if (pid > 0)
					waitpid(pid, status, 0);
				/* No need to kill the pid anymore, since they closed us */
				pid = -1;
				break;
			}

			/* Special case for inability to execute child process */
			if (*buf && strncasecmp(buf, "failure", 7) == 0) {
				returnstatus = AGI_RESULT_FAILURE;
				break;
			}

			/* get rid of trailing newline, if any */
			buflen = strlen(buf);
			if (buflen && buf[buflen - 1] == '\n') {
				buf[buflen - 1] = '\0';
			}

			if (agidebug)
				ast_verbose("<%s>AGI Rx << %s\n", ast_channel_name(chan), buf);
			cmd_status = agi_handle_command(chan, agi, buf, dead);
			switch (cmd_status) {
			case AGI_RESULT_FAILURE:
				if (dead || !ast_check_hangup(chan)) {
					/* The failure was not because of a hangup. */
					returnstatus = AGI_RESULT_FAILURE;
				}
				break;
			default:
				break;
			}
		} else {
			if (--retry <= 0) {
				ast_log(LOG_WARNING, "No channel, no fd?\n");
				returnstatus = AGI_RESULT_FAILURE;
				break;
			}
		}
	}
	if (agi->speech) {
		ast_speech_destroy(agi->speech);
	}
	/* Notify process */
	if (send_sighup) {
		if (pid > -1) {
			if (kill(pid, SIGHUP)) {
				ast_log(LOG_WARNING, "unable to send SIGHUP to AGI process %d: %s\n", pid, strerror(errno));
			} else { /* Give the process a chance to die */
				usleep(1);
			}
			waitpid(pid, status, WNOHANG);
		} else if (agi->fast) {
			ast_agi_send(agi->fd, chan, "HANGUP\n");
		}
	}
	fclose(readf);
	return returnstatus;
}

static char *handle_cli_agi_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct agi_command *command;
	char fullcmd[MAX_CMD_LEN];
	int error = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "agi show commands [topic]";
		e->usage =
			"Usage: agi show commands [topic] <topic>\n"
			"       When called with a topic as an argument, displays usage\n"
			"       information on the given command.  If called without a\n"
			"       topic, it provides a list of AGI commands.\n";
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < e->args - 1 || (a->argc >= e->args && strcasecmp(a->argv[e->args - 1], "topic")))
		return CLI_SHOWUSAGE;
	if (a->argc > e->args - 1) {
		command = find_command(a->argv + e->args, 1);
		if (command) {
			char *synopsis = NULL, *description = NULL, *syntax = NULL, *seealso = NULL;
			char info[30 + MAX_CMD_LEN];					/* '-= Info about...' */
			char infotitle[30 + MAX_CMD_LEN + AST_TERM_MAX_ESCAPE_CHARS];	/* '-= Info about...' with colors */
			char syntitle[11 + AST_TERM_MAX_ESCAPE_CHARS];			/* [Syntax]\n with colors */
			char desctitle[15 + AST_TERM_MAX_ESCAPE_CHARS];			/* [Description]\n with colors */
			char deadtitle[13 + AST_TERM_MAX_ESCAPE_CHARS];			/* [Runs Dead]\n with colors */
			char deadcontent[3 + AST_TERM_MAX_ESCAPE_CHARS];		/* 'Yes' or 'No' with colors */
			char seealsotitle[12 + AST_TERM_MAX_ESCAPE_CHARS];		/* [See Also]\n with colors */
			char stxtitle[10 + AST_TERM_MAX_ESCAPE_CHARS];			/* [Syntax]\n with colors */
			size_t synlen, desclen, seealsolen, stxlen;

			term_color(syntitle, "[Synopsis]\n", COLOR_MAGENTA, 0, sizeof(syntitle));
			term_color(desctitle, "[Description]\n", COLOR_MAGENTA, 0, sizeof(desctitle));
			term_color(deadtitle, "[Runs Dead]\n", COLOR_MAGENTA, 0, sizeof(deadtitle));
			term_color(seealsotitle, "[See Also]\n", COLOR_MAGENTA, 0, sizeof(seealsotitle));
			term_color(stxtitle, "[Syntax]\n", COLOR_MAGENTA, 0, sizeof(stxtitle));
			term_color(deadcontent, command->dead ? "Yes" : "No", COLOR_CYAN, 0, sizeof(deadcontent));

			ast_join(fullcmd, sizeof(fullcmd), a->argv + e->args);
			snprintf(info, sizeof(info), "\n  -= Info about agi '%s' =- ", fullcmd);
			term_color(infotitle, info, COLOR_CYAN, 0, sizeof(infotitle));
#ifdef AST_XML_DOCS
			if (command->docsrc == AST_XML_DOC) {
				synopsis = ast_xmldoc_printable(S_OR(command->summary, "Not available"), 1);
				description = ast_xmldoc_printable(S_OR(command->usage, "Not available"), 1);
				seealso = ast_xmldoc_printable(S_OR(command->seealso, "Not available"), 1);
				if (!seealso || !description || !synopsis) {
					error = 1;
					goto return_cleanup;
				}
			} else
#endif
			{
				synlen = strlen(S_OR(command->summary, "Not available")) + AST_TERM_MAX_ESCAPE_CHARS;
				synopsis = ast_malloc(synlen);

				desclen = strlen(S_OR(command->usage, "Not available")) + AST_TERM_MAX_ESCAPE_CHARS;
				description = ast_malloc(desclen);

				seealsolen = strlen(S_OR(command->seealso, "Not available")) + AST_TERM_MAX_ESCAPE_CHARS;
				seealso = ast_malloc(seealsolen);

				if (!synopsis || !description || !seealso) {
					error = 1;
					goto return_cleanup;
				}
				term_color(synopsis, S_OR(command->summary, "Not available"), COLOR_CYAN, 0, synlen);
				term_color(description, S_OR(command->usage, "Not available"), COLOR_CYAN, 0, desclen);
				term_color(seealso, S_OR(command->seealso, "Not available"), COLOR_CYAN, 0, seealsolen);
			}

			stxlen = strlen(S_OR(command->syntax, "Not available")) + AST_TERM_MAX_ESCAPE_CHARS;
			syntax = ast_malloc(stxlen);
			if (!syntax) {
				error = 1;
				goto return_cleanup;
			}
			term_color(syntax, S_OR(command->syntax, "Not available"), COLOR_CYAN, 0, stxlen);

			ast_cli(a->fd, "%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n", infotitle, stxtitle, syntax,
					desctitle, description, syntitle, synopsis, deadtitle, deadcontent,
					seealsotitle, seealso);
return_cleanup:
			ast_free(synopsis);
			ast_free(description);
			ast_free(syntax);
			ast_free(seealso);
		} else {
			if (find_command(a->argv + e->args, -1)) {
				return help_workhorse(a->fd, a->argv + e->args);
			} else {
				ast_join(fullcmd, sizeof(fullcmd), a->argv + e->args);
				ast_cli(a->fd, "No such command '%s'.\n", fullcmd);
			}
		}
	} else {
		return help_workhorse(a->fd, NULL);
	}
	return (error ? CLI_FAILURE : CLI_SUCCESS);
}

/*! \brief Convert string to use HTML escaped characters
	\note Maybe this should be a generic function?
*/
static void write_html_escaped(FILE *htmlfile, char *str)
{
	char *cur = str;

	while(*cur) {
		switch (*cur) {
		case '<':
			fprintf(htmlfile, "%s", "&lt;");
			break;
		case '>':
			fprintf(htmlfile, "%s", "&gt;");
			break;
		case '&':
			fprintf(htmlfile, "%s", "&amp;");
			break;
		case '"':
			fprintf(htmlfile, "%s", "&quot;");
			break;
		default:
			fprintf(htmlfile, "%c", *cur);
			break;
		}
		cur++;
	}

	return;
}

static int write_htmldump(const char *filename)
{
	struct agi_command *command;
	char fullcmd[MAX_CMD_LEN];
	FILE *htmlfile;

	if (!(htmlfile = fopen(filename, "wt")))
		return -1;

	fprintf(htmlfile, "<HTML>\n<HEAD>\n<TITLE>AGI Commands</TITLE>\n</HEAD>\n");
	fprintf(htmlfile, "<BODY>\n<CENTER><B><H1>AGI Commands</H1></B></CENTER>\n\n");
	fprintf(htmlfile, "<TABLE BORDER=\"0\" CELLSPACING=\"10\">\n");

	AST_RWLIST_RDLOCK(&agi_commands);
	AST_RWLIST_TRAVERSE(&agi_commands, command, list) {
		char *tempstr, *stringp;

		if (!command->cmda[0])	/* end ? */
			break;
		/* Hide commands that start with '_' */
		if ((command->cmda[0])[0] == '_')
			continue;
		ast_join(fullcmd, sizeof(fullcmd), command->cmda);

		fprintf(htmlfile, "<TR><TD><TABLE BORDER=\"1\" CELLPADDING=\"5\" WIDTH=\"100%%\">\n");
		fprintf(htmlfile, "<TR><TH ALIGN=\"CENTER\"><B>%s - %s</B></TH></TR>\n", fullcmd, command->summary);
#ifdef AST_XML_DOCS
		stringp = ast_xmldoc_printable(command->usage, 0);
#else
		stringp = ast_strdup(command->usage);
#endif
		tempstr = strsep(&stringp, "\n");

		fprintf(htmlfile, "<TR><TD ALIGN=\"CENTER\">");
		write_html_escaped(htmlfile, tempstr);
		fprintf(htmlfile, "</TD></TR>\n");
		fprintf(htmlfile, "<TR><TD ALIGN=\"CENTER\">\n");

		while ((tempstr = strsep(&stringp, "\n")) != NULL) {
			write_html_escaped(htmlfile, tempstr);
			fprintf(htmlfile, "<BR>\n");
		}
		fprintf(htmlfile, "</TD></TR>\n");
		fprintf(htmlfile, "</TABLE></TD></TR>\n\n");
		ast_free(stringp);
	}
	AST_RWLIST_UNLOCK(&agi_commands);
	fprintf(htmlfile, "</TABLE>\n</BODY>\n</HTML>\n");
	fclose(htmlfile);
	return 0;
}

static char *handle_cli_agi_dump_html(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "agi dump html";
		e->usage =
			"Usage: agi dump html <filename>\n"
			"       Dumps the AGI command list in HTML format to the given\n"
			"       file.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != e->args + 1)
		return CLI_SHOWUSAGE;

	if (write_htmldump(a->argv[e->args]) < 0) {
		ast_cli(a->fd, "Could not create file '%s'\n", a->argv[e->args]);
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, "AGI HTML commands dumped to: %s\n", a->argv[e->args]);
	return CLI_SUCCESS;
}

static int agi_exec_full(struct ast_channel *chan, const char *data, int enhanced, int dead)
{
	enum agi_result res;
	char *buf;
	int fds[2], efd = -1, pid = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(arg)[MAX_ARGS];
	);
	AGI agi;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AGI requires an argument (script)\n");
		return -1;
	}
	if (dead)
		ast_debug(3, "Hungup channel detected, running agi in dead mode.\n");
	memset(&agi, 0, sizeof(agi));
	buf = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, buf);
	args.argv[args.argc] = NULL;
#if 0
	 /* Answer if need be */
	if (chan->_state != AST_STATE_UP) {
		if (ast_answer(chan))
			return -1;
	}
#endif
	res = launch_script(chan, args.argv[0], args.argv, fds, enhanced ? &efd : NULL, &pid);
	/* Async AGI do not require run_agi(), so just proceed if normal AGI
	   or Fast AGI are setup with success. */
	if (res == AGI_RESULT_SUCCESS || res == AGI_RESULT_SUCCESS_FAST) {
		int status = 0;
		agi.fd = fds[1];
		agi.ctrl = fds[0];
		agi.audio = efd;
		agi.fast = (res == AGI_RESULT_SUCCESS_FAST) ? 1 : 0;
		res = run_agi(chan, args.argv[0], &agi, pid, &status, dead, args.argc, args.argv);
		/* If the fork'd process returns non-zero, set AGISTATUS to FAILURE */
		if ((res == AGI_RESULT_SUCCESS || res == AGI_RESULT_SUCCESS_FAST) && status)
			res = AGI_RESULT_FAILURE;
		if (fds[1] != fds[0])
			close(fds[1]);
		if (efd > -1)
			close(efd);
	}
	ast_safe_fork_cleanup();

	switch (res) {
	case AGI_RESULT_SUCCESS:
	case AGI_RESULT_SUCCESS_FAST:
	case AGI_RESULT_SUCCESS_ASYNC:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "SUCCESS");
		break;
	case AGI_RESULT_FAILURE:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "FAILURE");
		break;
	case AGI_RESULT_NOTFOUND:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "NOTFOUND");
		break;
	case AGI_RESULT_HANGUP:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "HANGUP");
		return -1;
	}

	return 0;
}

static int agi_exec(struct ast_channel *chan, const char *data)
{
	if (!ast_check_hangup(chan))
		return agi_exec_full(chan, data, 0, 0);
	else
		return agi_exec_full(chan, data, 0, 1);
}

static int eagi_exec(struct ast_channel *chan, const char *data)
{
	int res;
	struct ast_format readformat;

	if (ast_check_hangup(chan)) {
		ast_log(LOG_ERROR, "EAGI cannot be run on a dead/hungup channel, please use AGI.\n");
		return 0;
	}
	ast_format_copy(&readformat, ast_channel_readformat(chan));
	if (ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set channel '%s' to linear mode\n", ast_channel_name(chan));
		return -1;
	}
	res = agi_exec_full(chan, data, 1, 0);
	if (!res) {
		if (ast_set_read_format(chan, &readformat)) {
			ast_log(LOG_WARNING, "Unable to restore channel '%s' to format %s\n", ast_channel_name(chan), ast_getformatname(&readformat));
		}
	}
	return res;
}

static int deadagi_exec(struct ast_channel *chan, const char *data)
{
	ast_log(LOG_WARNING, "DeadAGI has been deprecated, please use AGI in all cases!\n");
	return agi_exec(chan, data);
}

static struct ast_cli_entry cli_agi[] = {
	AST_CLI_DEFINE(handle_cli_agi_add_cmd,   "Add AGI command to a channel in Async AGI"),
	AST_CLI_DEFINE(handle_cli_agi_debug,     "Enable/Disable AGI debugging"),
	AST_CLI_DEFINE(handle_cli_agi_show,      "List AGI commands or specific help"),
	AST_CLI_DEFINE(handle_cli_agi_dump_html, "Dumps a list of AGI commands in HTML format")
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_agi_null_docs)
{
	int res = AST_TEST_PASS;
	struct agi_command noop_command =
		{ { "testnoop", NULL }, handle_noop, NULL, NULL, 0 };

	switch (cmd) {
	case TEST_INIT:
		info->name = "null_agi_docs";
		info->category = "/res/agi/";
		info->summary = "AGI command with no documentation";
		info->description = "Test whether an AGI command with no documentation will crash Asterisk";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_agi_register(ast_module_info->self, &noop_command) == 0) {
		ast_test_status_update(test, "Unable to register testnoop command, because res_agi is not loaded.\n");
		return AST_TEST_NOT_RUN;
	}

#ifndef HAVE_NULLSAFE_PRINTF
	/* Test for condition without actually crashing Asterisk */
	if (noop_command.usage == NULL) {
		ast_test_status_update(test, "AGI testnoop usage was not updated properly.\n");
		res = AST_TEST_FAIL;
	}
	if (noop_command.syntax == NULL) {
		ast_test_status_update(test, "AGI testnoop syntax was not updated properly.\n");
		res = AST_TEST_FAIL;
	}
#endif

	ast_agi_unregister(ast_module_info->self, &noop_command);
	return res;
}
#endif

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_agi, ARRAY_LEN(cli_agi));
	/* we can safely ignore the result of ast_agi_unregister_multiple() here, since it cannot fail, as
	   we know that these commands were registered by this module and are still registered
	*/
	(void) ast_agi_unregister_multiple(ast_module_info->self, commands, ARRAY_LEN(commands));
	ast_unregister_application(eapp);
	ast_unregister_application(deadapp);
	ast_manager_unregister("AGI");
	AST_TEST_UNREGISTER(test_agi_null_docs);
	return ast_unregister_application(app);
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_agi, ARRAY_LEN(cli_agi));
	/* we can safely ignore the result of ast_agi_register_multiple() here, since it cannot fail, as
	   no other commands have been registered yet
	*/
	(void) ast_agi_register_multiple(ast_module_info->self, commands, ARRAY_LEN(commands));
	ast_register_application_xml(deadapp, deadagi_exec);
	ast_register_application_xml(eapp, eagi_exec);
	ast_manager_register_xml("AGI", EVENT_FLAG_AGI, action_add_agi_cmd);
	AST_TEST_REGISTER(test_agi_null_docs);
	return ast_register_application_xml(app, agi_exec);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Asterisk Gateway Interface (AGI)",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
		);
