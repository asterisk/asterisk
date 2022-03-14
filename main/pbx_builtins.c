/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015 Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Core PBX builtin routines.
 *
 * \author George Joseph <george.joseph@fairview5.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/indications.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/conversions.h"
#include "pbx_private.h"

/*** DOCUMENTATION
	<application name="Answer" language="en_US">
		<synopsis>
			Answer a channel if ringing.
		</synopsis>
		<syntax>
			<parameter name="delay">
				<para>Asterisk will wait this number of milliseconds before returning to
				the dialplan after answering the call.</para>
			</parameter>
		</syntax>
		<description>
			<para>If the call has not been answered, this application will
			answer it. Otherwise, it has no effect on the call.</para>
		</description>
		<see-also>
			<ref type="application">Hangup</ref>
		</see-also>
	</application>
	<application name="BackGround" language="en_US">
		<synopsis>
			Play an audio file while waiting for digits of an extension to go to.
		</synopsis>
		<syntax>
			<parameter name="filenames" required="true" argsep="&amp;">
				<argument name="filename1" required="true" />
				<argument name="filename2" multiple="true" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="s">
						<para>Causes the playback of the message to be skipped
						if the channel is not in the <literal>up</literal> state (i.e. it
						hasn't been answered yet). If this happens, the
						application will return immediately.</para>
					</option>
					<option name="n">
						<para>Don't answer the channel before playing the files.</para>
					</option>
					<option name="m">
						<para>Only break if a digit hit matches a one digit
						extension in the destination context.</para>
					</option>
					<option name="p">
						<para>Do not allow playback to be interrupted with digits.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="langoverride">
				<para>Explicitly specifies which language to attempt to use for the requested sound files.</para>
			</parameter>
			<parameter name="context">
				<para>This is the dialplan context that this application will use when exiting
				to a dialed extension.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will play the given list of files <emphasis>(do not put extension)</emphasis>
			while waiting for an extension to be dialed by the calling channel. To continue waiting
			for digits after this application has finished playing files, the <literal>WaitExten</literal>
			application should be used.</para>
			<para>If one of the requested sound files does not exist, call processing will be terminated.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="BACKGROUNDSTATUS">
					<para>The status of the background attempt as a text string.</para>
					<value name="SUCCESS" />
					<value name="FAILED" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">ControlPlayback</ref>
			<ref type="application">WaitExten</ref>
			<ref type="application">BackgroundDetect</ref>
			<ref type="function">TIMEOUT</ref>
		</see-also>
	</application>
	<application name="Busy" language="en_US">
		<synopsis>
			Indicate the Busy condition.
		</synopsis>
		<syntax>
			<parameter name="timeout">
				<para>If specified, the calling channel will be hung up after the specified number of seconds.
				Otherwise, this application will wait until the calling channel hangs up.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will indicate the busy condition to the calling channel.</para>
		</description>
		<see-also>
			<ref type="application">Congestion</ref>
			<ref type="application">Progress</ref>
			<ref type="application">Playtones</ref>
			<ref type="application">Hangup</ref>
		</see-also>
	</application>
	<application name="Congestion" language="en_US">
		<synopsis>
			Indicate the Congestion condition.
		</synopsis>
		<syntax>
			<parameter name="timeout">
				<para>If specified, the calling channel will be hung up after the specified number of seconds.
				Otherwise, this application will wait until the calling channel hangs up.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will indicate the congestion condition to the calling channel.</para>
		</description>
		<see-also>
			<ref type="application">Busy</ref>
			<ref type="application">Progress</ref>
			<ref type="application">Playtones</ref>
			<ref type="application">Hangup</ref>
		</see-also>
	</application>
	<application name="ExecIfTime" language="en_US">
		<synopsis>
			Conditional application execution based on the current time.
		</synopsis>
		<syntax argsep="?">
			<parameter name="day_condition" required="true">
				<argument name="times" required="true" />
				<argument name="weekdays" required="true" />
				<argument name="mdays" required="true" />
				<argument name="months" required="true" />
				<argument name="timezone" required="false" />
			</parameter>
			<parameter name="appname" required="true" hasparams="optional">
				<argument name="appargs" required="true" />
			</parameter>
		</syntax>
		<description>
			<para>This application will execute the specified dialplan application, with optional
			arguments, if the current time matches the given time specification.</para>
		</description>
		<see-also>
			<ref type="application">Exec</ref>
			<ref type="application">ExecIf</ref>
			<ref type="application">TryExec</ref>
			<ref type="application">GotoIfTime</ref>
		</see-also>
	</application>
	<application name="Goto" language="en_US">
		<synopsis>
			Jump to a particular priority, extension, or context.
		</synopsis>
		<syntax>
			<parameter name="context" />
			<parameter name="extensions" />
			<parameter name="priority" required="true" />
		</syntax>
		<description>
			<para>This application will set the current context, extension, and priority in the channel structure.
			After it completes, the pbx engine will continue dialplan execution at the specified location.
			If no specific <replaceable>extension</replaceable>, or <replaceable>extension</replaceable> and
			<replaceable>context</replaceable>, are specified, then this application will
			just set the specified <replaceable>priority</replaceable> of the current extension.</para>
			<para>At least a <replaceable>priority</replaceable> is required as an argument, or the goto will
			return a <literal>-1</literal>,	and the channel and call will be terminated.</para>
			<para>If the location that is put into the channel information is bogus, and asterisk cannot
			find that location in the dialplan, then the execution engine will try to find and execute the code in
			the <literal>i</literal> (invalid) extension in the current context. If that does not exist, it will try to execute the
			<literal>h</literal> extension. If neither the <literal>h</literal> nor <literal>i</literal> extensions
			have been defined, the channel is hung up, and the execution of instructions on the channel is terminated.
			What this means is that, for example, you specify a context that does not exist, then
			it will not be possible to find the <literal>h</literal> or <literal>i</literal> extensions,
			and the call will terminate!</para>
		</description>
		<see-also>
			<ref type="application">GotoIf</ref>
			<ref type="application">GotoIfTime</ref>
			<ref type="application">Gosub</ref>
			<ref type="application">Macro</ref>
		</see-also>
	</application>
	<application name="GotoIf" language="en_US">
		<synopsis>
			Conditional goto.
		</synopsis>
		<syntax argsep="?">
			<parameter name="condition" required="true" />
			<parameter name="destination" required="true" argsep=":">
				<argument name="labeliftrue">
					<para>Continue at <replaceable>labeliftrue</replaceable> if the condition is true.
					Takes the form similar to Goto() of [[context,]extension,]priority.</para>
				</argument>
				<argument name="labeliffalse">
					<para>Continue at <replaceable>labeliffalse</replaceable> if the condition is false.
					Takes the form similar to Goto() of [[context,]extension,]priority.</para>
				</argument>
			</parameter>
		</syntax>
		<description>
			<para>This application will set the current context, extension, and priority in the channel structure
			based on the evaluation of the given condition. After this application completes, the
			pbx engine will continue dialplan execution at the specified location in the dialplan.
			The labels are specified with the same syntax as used within the Goto application.
			If the label chosen by the condition is omitted, no jump is performed, and the execution passes to the
			next instruction. If the target location is bogus, and does not exist, the execution engine will try
			to find and execute the code in the <literal>i</literal> (invalid) extension in the current context.
			If that does not exist, it will try to execute the <literal>h</literal> extension.
			If neither the <literal>h</literal> nor <literal>i</literal> extensions have been defined,
			the channel is hung up, and the execution of instructions on the channel is terminated.
			Remember that this command can set the current context, and if the context specified
			does not exist, then it will not be able to find any 'h' or 'i' extensions there, and
			the channel and call will both be terminated!.</para>
		</description>
		<see-also>
			<ref type="application">Goto</ref>
			<ref type="application">GotoIfTime</ref>
			<ref type="application">GosubIf</ref>
			<ref type="application">MacroIf</ref>
		</see-also>
	</application>
	<application name="GotoIfTime" language="en_US">
		<synopsis>
			Conditional Goto based on the current time.
		</synopsis>
		<syntax argsep="?">
			<parameter name="condition" required="true">
				<argument name="times" required="true" />
				<argument name="weekdays" required="true" />
				<argument name="mdays" required="true" />
				<argument name="months" required="true" />
				<argument name="timezone" required="false" />
			</parameter>
			<parameter name="destination" required="true" argsep=":">
				<argument name="labeliftrue">
					<para>Continue at <replaceable>labeliftrue</replaceable> if the condition is true.
					Takes the form similar to Goto() of [[context,]extension,]priority.</para>
				</argument>
				<argument name="labeliffalse">
					<para>Continue at <replaceable>labeliffalse</replaceable> if the condition is false.
					Takes the form similar to Goto() of [[context,]extension,]priority.</para>
				</argument>
			</parameter>
		</syntax>
		<description>
			<para>This application will set the context, extension, and priority in the channel structure
			based on the evaluation of the given time specification. After this application completes,
			the pbx engine will continue dialplan execution at the specified location in the dialplan.
			If the current time is within the given time specification, the channel will continue at
			<replaceable>labeliftrue</replaceable>. Otherwise the channel will continue at <replaceable>labeliffalse</replaceable>.
			If the label chosen by the condition is omitted, no jump is performed, and execution passes to the next
			instruction. If the target jump location is bogus, the same actions would be taken as for <literal>Goto</literal>.
			Further information on the time specification can be found in examples
			illustrating how to do time-based context includes in the dialplan.</para>
		</description>
		<see-also>
			<ref type="application">GotoIf</ref>
			<ref type="application">Goto</ref>
			<ref type="function">IFTIME</ref>
			<ref type="function">TESTTIME</ref>
		</see-also>
	</application>
	<application name="ImportVar" language="en_US">
		<synopsis>
			Import a variable from a channel into a new variable.
		</synopsis>
		<syntax argsep="=">
			<parameter name="newvar" required="true" />
			<parameter name="vardata" required="true">
				<argument name="channelname" required="true" />
				<argument name="variable" required="true" />
			</parameter>
		</syntax>
		<description>
			<para>This application imports a <replaceable>variable</replaceable> from the specified
			<replaceable>channel</replaceable> (as opposed to the current one) and stores it as a variable
			(<replaceable>newvar</replaceable>) in the current channel (the channel that is calling this
			application). Variables created by this application have the same inheritance properties as those
			created with the <literal>Set</literal> application.</para>
		</description>
		<see-also>
			<ref type="application">Set</ref>
		</see-also>
	</application>
	<application name="Hangup" language="en_US">
		<synopsis>
			Hang up the calling channel.
		</synopsis>
		<syntax>
			<parameter name="causecode">
				<para>If a <replaceable>causecode</replaceable> is given the channel's
				hangup cause will be set to the given value.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application will hang up the calling channel.</para>
		</description>
		<see-also>
			<ref type="application">Answer</ref>
			<ref type="application">Busy</ref>
			<ref type="application">Congestion</ref>
		</see-also>
	</application>
	<application name="Incomplete" language="en_US">
		<synopsis>
			Returns AST_PBX_INCOMPLETE value.
		</synopsis>
		<syntax>
			<parameter name="n">
				<para>If specified, then Incomplete will not attempt to answer the channel first.</para>
				<note><para>Most channel types need to be in Answer state in order to receive DTMF.</para></note>
			</parameter>
		</syntax>
		<description>
			<para>Signals the PBX routines that the previous matched extension is incomplete
			and that further input should be allowed before matching can be considered
			to be complete.  Can be used within a pattern match when certain criteria warrants
			a longer match.</para>
		</description>
	</application>
	<application name="NoOp" language="en_US">
		<synopsis>
			Do Nothing (No Operation).
		</synopsis>
		<syntax>
			<parameter name="text">
				<para>Any text provided can be viewed at the Asterisk CLI.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application does nothing. However, it is useful for debugging purposes.</para>
			<para>This method can be used to see the evaluations of variables or functions without having any effect.</para>
		</description>
		<see-also>
			<ref type="application">Verbose</ref>
			<ref type="application">Log</ref>
		</see-also>
	</application>
	<application name="Proceeding" language="en_US">
		<synopsis>
			Indicate proceeding.
		</synopsis>
		<syntax />
		<description>
			<para>This application will request that a proceeding message be provided to the calling channel.</para>
		</description>
	</application>
	<application name="Progress" language="en_US">
		<synopsis>
			Indicate progress.
		</synopsis>
		<syntax />
		<description>
			<para>This application will request that in-band progress information be provided to the calling channel.</para>
		</description>
		<see-also>
			<ref type="application">Busy</ref>
			<ref type="application">Congestion</ref>
			<ref type="application">Ringing</ref>
			<ref type="application">Playtones</ref>
		</see-also>
	</application>
	<application name="RaiseException" language="en_US">
		<synopsis>
			Handle an exceptional condition.
		</synopsis>
		<syntax>
			<parameter name="reason" required="true" />
		</syntax>
		<description>
			<para>This application will jump to the <literal>e</literal> extension in the current context, setting the
			dialplan function EXCEPTION(). If the <literal>e</literal> extension does not exist, the call will hangup.</para>
		</description>
		<see-also>
			<ref type="function">Exception</ref>
		</see-also>
	</application>
	<application name="Ringing" language="en_US">
		<synopsis>
			Indicate ringing tone.
		</synopsis>
		<syntax />
		<description>
			<para>This application will request that the channel indicate a ringing tone to the user.</para>
		</description>
		<see-also>
			<ref type="application">Busy</ref>
			<ref type="application">Congestion</ref>
			<ref type="application">Progress</ref>
			<ref type="application">Playtones</ref>
		</see-also>
	</application>
	<application name="SayAlpha" language="en_US">
		<synopsis>
			Say Alpha.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>This application will play the sounds that correspond to the letters
			of the given <replaceable>string</replaceable>. If the channel variable
			<variable>SAY_DTMF_INTERRUPT</variable> is set to 'true' (case insensitive),
			then this application will react to DTMF in the	same way as
			<literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayOrdinal</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
			<ref type="function">SAYFILES</ref>
		</see-also>
	</application>
	<application name="SayAlphaCase" language="en_US">
		<synopsis>
			Say Alpha.
		</synopsis>
		<syntax>
			<parameter name="casetype" required="true" >
				<enumlist>
					<enum name="a">
						<para>Case sensitive (all) pronunciation.
						(Ex: SayAlphaCase(a,aBc); - lowercase a uppercase b lowercase c).</para>
					</enum>
					<enum name="l">
						<para>Case sensitive (lower) pronunciation.
						(Ex: SayAlphaCase(l,aBc); - lowercase a b lowercase c).</para>
					</enum>
					<enum name="n">
						<para>Case insensitive pronunciation. Equivalent to SayAlpha.
						(Ex: SayAlphaCase(n,aBc) - a b c).</para>
					</enum>
					<enum name="u">
						<para>Case sensitive (upper) pronunciation.
						(Ex: SayAlphaCase(u,aBc); - a uppercase b c).</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>This application will play the sounds that correspond to the letters of the
			given <replaceable>string</replaceable>.  Optionally, a <replaceable>casetype</replaceable> may be
			specified.  This will be used for case-insensitive or case-sensitive pronunciations. If the channel
			variable <variable>SAY_DTMF_INTERRUPT</variable> is set to 'true' (case insensitive), then this
			application will react to DTMF in the same way as <literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayOrdinal</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="application">SayAlpha</ref>
			<ref type="function">CHANNEL</ref>
		</see-also>
	</application>
	<application name="SayDigits" language="en_US">
		<synopsis>
			Say Digits.
		</synopsis>
		<syntax>
			<parameter name="digits" required="true" />
		</syntax>
		<description>
			<para>This application will play the sounds that correspond to the digits of
			the given number. This will use the language that is currently set for the channel.
			If the channel variable <variable>SAY_DTMF_INTERRUPT</variable> is set to 'true'
			(case insensitive), then this application will react to DTMF in the same way as
			<literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayOrdinal</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
			<ref type="function">SAYFILES</ref>
		</see-also>
	</application>
	<application name="SayMoney" language="en_US">
		<synopsis>
			Say Money.
		</synopsis>
		<syntax>
			<parameter name="dollars" required="true" />
		</syntax>
		<description>
			<para>This application will play the currency sounds for the given floating point number
			in the current language. Currently only English and US Dollars is supported.
			If the channel variable <variable>SAY_DTMF_INTERRUPT</variable> is set to 'true'
			(case insensitive), then this application will react to DTMF in the same way as
			<literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayOrdinal</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
			<ref type="function">SAYFILES</ref>
		</see-also>
	</application>
	<application name="SayNumber" language="en_US">
		<synopsis>
			Say Number.
		</synopsis>
		<syntax>
			<parameter name="digits" required="true" />
			<parameter name="gender" />
		</syntax>
		<description>
			<para>This application will play the sounds that correspond to the given
			<replaceable>digits</replaceable>. Optionally, a <replaceable>gender</replaceable> may be
			specified. This will use the language that is currently set for the channel. See the CHANNEL()
			function for more information on setting the language for the channel. If the channel variable
			<variable>SAY_DTMF_INTERRUPT</variable> is set to 'true' (case insensitive), then this
			application will react to DTMF in the same way as <literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
			<ref type="function">SAYFILES</ref>
		</see-also>
	</application>
	<application name="SayOrdinal" language="en_US">
		<synopsis>
			Say Ordinal Number.
		</synopsis>
		<syntax>
			<parameter name="digits" required="true" />
			<parameter name="gender" />
		</syntax>
		<description>
			<para>This application will play the ordinal sounds that correspond to the given
			<replaceable>digits</replaceable> (e.g. 1st, 42nd). Currently only English is supported.</para>
			<para>Optionally, a <replaceable>gender</replaceable> may be
			specified. This will use the language that is currently set for the channel. See the CHANNEL()
			function for more information on setting the language for the channel. If the channel variable
			<variable>SAY_DTMF_INTERRUPT</variable> is set to 'true' (case insensitive), then this
			application will react to DTMF in the same way as <literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
			<ref type="function">SAYFILES</ref>
		</see-also>
	</application>
	<application name="SayPhonetic" language="en_US">
		<synopsis>
			Say Phonetic.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>This application will play the sounds from the phonetic alphabet that correspond to the
			letters in the given <replaceable>string</replaceable>. If the channel variable
			<variable>SAY_DTMF_INTERRUPT</variable> is set to 'true' (case insensitive), then this
			application will react to DTMF in the same way as <literal>Background</literal>.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayOrdinal</ref>
			<ref type="function">SAYFILES</ref>
		</see-also>
	</application>
	<application name="SetAMAFlags" language="en_US">
		<synopsis>
			Set the AMA Flags.
		</synopsis>
		<syntax>
			<parameter name="flag" />
		</syntax>
		<description>
			<para>This application will set the channel's AMA Flags for billing purposes.</para>
			<warning><para>This application is deprecated. Please use the CHANNEL function instead.</para></warning>
		</description>
		<see-also>
			<ref type="function">CDR</ref>
			<ref type="function">CHANNEL</ref>
		</see-also>
	</application>
	<application name="Wait" language="en_US">
		<synopsis>
			Waits for some time.
		</synopsis>
		<syntax>
			<parameter name="seconds" required="true">
				<para>Can be passed with fractions of a second. For example, <literal>1.5</literal> will ask the
				application to wait for 1.5 seconds.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application waits for a specified number of <replaceable>seconds</replaceable>.</para>
		</description>
	</application>
	<application name="WaitDigit" language="en_US">
		<synopsis>
			Waits for a digit to be entered.
		</synopsis>
		<syntax>
			<parameter name="seconds">
				<para>Can be passed with fractions of a second. For example, <literal>1.5</literal> will ask the
				application to wait for 1.5 seconds.</para>
			</parameter>
			<parameter name="digits">
				<para>Digits to accept, all others are ignored.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application waits for the user to press one of the accepted
			<replaceable>digits</replaceable> for a specified number of
			<replaceable>seconds</replaceable>.</para>
			<variablelist>
				<variable name="WAITDIGITSTATUS">
					<para>This is the final status of the command</para>
					<value name="ERROR">Parameters are invalid.</value>
					<value name="DTMF">An accepted digit was received.</value>
					<value name="TIMEOUT">The timeout passed before any acceptable digits were received.</value>
					<value name="CANCEL">The channel has hungup or was redirected.</value>
				</variable>
				<variable name="WAITDIGITRESULT">
					<para>The digit that was received, only set if
					<variable>WAITDIGITSTATUS</variable> is <literal>DTMF</literal>.</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Wait</ref>
			<ref type="application">WaitExten</ref>
		</see-also>
	</application>
	<application name="WaitExten" language="en_US">
		<synopsis>
			Waits for an extension to be entered.
		</synopsis>
		<syntax>
			<parameter name="seconds">
				<para>Can be passed with fractions of a second. For example, <literal>1.5</literal> will ask the
				application to wait for 1.5 seconds.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="m">
						<para>Provide music on hold to the caller while waiting for an extension.</para>
						<argument name="x">
							<para>Specify the class for music on hold. <emphasis>CHANNEL(musicclass) will
							be used instead if set</emphasis></para>
						</argument>
					</option>
					<option name="d">
						<para>Play <literal>dial</literal> indications tone on channel while waiting
						for digits.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application waits for the user to enter a new extension for a specified number
			of <replaceable>seconds</replaceable>.</para>
			<xi:include xpointer="xpointer(/docs/application[@name='Macro']/description/warning[2])" />
		</description>
		<see-also>
			<ref type="application">Background</ref>
			<ref type="function">TIMEOUT</ref>
		</see-also>
	</application>
 ***/

#define BACKGROUND_SKIP		(1 << 0)
#define BACKGROUND_NOANSWER	(1 << 1)
#define BACKGROUND_MATCHEXTEN	(1 << 2)
#define BACKGROUND_PLAYBACK	(1 << 3)

AST_APP_OPTIONS(background_opts, {
	AST_APP_OPTION('s', BACKGROUND_SKIP),
	AST_APP_OPTION('n', BACKGROUND_NOANSWER),
	AST_APP_OPTION('m', BACKGROUND_MATCHEXTEN),
	AST_APP_OPTION('p', BACKGROUND_PLAYBACK),
});

#define WAITEXTEN_MOH		(1 << 0)
#define WAITEXTEN_DIALTONE	(1 << 1)

AST_APP_OPTIONS(waitexten_opts, {
	AST_APP_OPTION_ARG('m', WAITEXTEN_MOH, 0),
	AST_APP_OPTION_ARG('d', WAITEXTEN_DIALTONE, 0),
});

int pbx_builtin_raise_exception(struct ast_channel *chan, const char *reason)
{
	/* Priority will become 1, next time through the AUTOLOOP */
	return raise_exception(chan, reason, 0);
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_proceeding(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_PROCEEDING);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_progress(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_PROGRESS);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_ringing(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_RINGING);
	return 0;
}

/*!
 * \ingroup applications
 */
int indicate_busy(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_BUSY);
	/* Don't change state of an UP channel, just indicate
	   busy in audio */
	ast_channel_lock(chan);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_channel_hangupcause_set(chan, AST_CAUSE_BUSY);
		ast_setstate(chan, AST_STATE_BUSY);
	}
	ast_channel_unlock(chan);
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
int indicate_congestion(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	/* Don't change state of an UP channel, just indicate
	   congestion in audio */
	ast_channel_lock(chan);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_channel_hangupcause_set(chan, AST_CAUSE_CONGESTION);
		ast_setstate(chan, AST_STATE_BUSY);
	}
	ast_channel_unlock(chan);
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_answer(struct ast_channel *chan, const char *data)
{
	int delay = 0;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(delay);
		AST_APP_ARG(answer_cdr);
	);

	if (ast_strlen_zero(data)) {
		return __ast_answer(chan, 0);
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.delay) && (ast_channel_state(chan) != AST_STATE_UP))
		delay = atoi(data);

	if (delay < 0) {
		delay = 0;
	}

	if (!ast_strlen_zero(args.answer_cdr) && !strcasecmp(args.answer_cdr, "nocdr")) {
		ast_log(AST_LOG_WARNING, "The nocdr option for the Answer application has been removed and is no longer supported.\n");
	}

	return __ast_answer(chan, delay);
}

static int pbx_builtin_incomplete(struct ast_channel *chan, const char *data)
{
	const char *options = data;
	int answer = 1;

	/* Some channels can receive DTMF in unanswered state; some cannot */
	if (!ast_strlen_zero(options) && strchr(options, 'n')) {
		answer = 0;
	}

	/* If the channel is hungup, stop waiting */
	if (ast_check_hangup(chan)) {
		return -1;
	} else if (ast_channel_state(chan) != AST_STATE_UP && answer) {
		__ast_answer(chan, 0);
	}

	ast_indicate(chan, AST_CONTROL_INCOMPLETE);

	return AST_PBX_INCOMPLETE;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_setamaflags(struct ast_channel *chan, const char *data)
{
	ast_log(AST_LOG_WARNING, "The SetAMAFlags application is deprecated. Please use the CHANNEL function instead.\n");

	if (ast_strlen_zero(data)) {
		ast_log(AST_LOG_WARNING, "No parameter passed to SetAMAFlags\n");
		return 0;
	}
	/* Copy the AMA Flags as specified */
	ast_channel_lock(chan);
	if (isdigit(data[0])) {
		int amaflags;
		if (sscanf(data, "%30d", &amaflags) != 1) {
			ast_log(AST_LOG_WARNING, "Unable to set AMA flags on channel %s\n", ast_channel_name(chan));
			ast_channel_unlock(chan);
			return 0;
		}
		ast_channel_amaflags_set(chan, amaflags);
	} else {
		ast_channel_amaflags_set(chan, ast_channel_string2amaflag(data));
	}
	ast_channel_unlock(chan);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_hangup(struct ast_channel *chan, const char *data)
{
	int cause;

	ast_set_hangupsource(chan, "dialplan/builtin", 0);

	if (!ast_strlen_zero(data)) {
		cause = ast_str2cause(data);
		if (cause <= 0) {
			if (sscanf(data, "%30d", &cause) != 1 || cause <= 0) {
				ast_log(LOG_WARNING, "Invalid cause given to Hangup(): \"%s\"\n", data);
				cause = 0;
			}
		}
	} else {
		cause = 0;
	}

	ast_channel_lock(chan);
	if (cause <= 0) {
		cause = ast_channel_hangupcause(chan);
		if (cause <= 0) {
			cause = AST_CAUSE_NORMAL_CLEARING;
		}
	}
	ast_channel_hangupcause_set(chan, cause);
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_EXPLICIT);
	ast_channel_unlock(chan);

	return -1;
}

/*! Goto
 * \ingroup applications
 */
static int pbx_builtin_goto(struct ast_channel *chan, const char *data)
{
	int res = ast_parseable_goto(chan, data);
	if (!res)
		ast_verb(3, "Goto (%s,%s,%d)\n", ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 1);
	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_gotoiftime(struct ast_channel *chan, const char *data)
{
	char *s, *ts, *branch1, *branch2, *branch;
	struct ast_timing timing;
	const char *ctime;
	struct timeval tv = ast_tvnow();
	long timesecs;

	if (!chan) {
		ast_log(LOG_WARNING, "GotoIfTime requires a channel on which to operate\n");
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "GotoIfTime requires an argument:\n  <time range>,<days of week>,<days of month>,<months>[,<timezone>]?'labeliftrue':'labeliffalse'\n");
		return -1;
	}

	ts = s = ast_strdupa(data);

	ast_channel_lock(chan);
	if ((ctime = pbx_builtin_getvar_helper(chan, "TESTTIME")) && sscanf(ctime, "%ld", &timesecs) == 1) {
		tv.tv_sec = timesecs;
	} else if (ctime) {
		ast_log(LOG_WARNING, "Using current time to evaluate\n");
		/* Reset when unparseable */
		pbx_builtin_setvar_helper(chan, "TESTTIME", NULL);
	}
	ast_channel_unlock(chan);

	/* Separate the Goto path */
	strsep(&ts, "?");
	branch1 = strsep(&ts,":");
	branch2 = strsep(&ts,"");

	/* struct ast_include include contained garbage here, fixed by zeroing it on get_timerange */
	if (ast_build_timing(&timing, s) && ast_check_timing2(&timing, tv)) {
		branch = branch1;
	} else {
		branch = branch2;
	}
	ast_destroy_timing(&timing);

	if (ast_strlen_zero(branch)) {
		ast_debug(1, "Not taking any branch\n");
		return 0;
	}

	return pbx_builtin_goto(chan, branch);
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_execiftime(struct ast_channel *chan, const char *data)
{
	char *s, *appname;
	struct ast_timing timing;
	struct ast_app *app;
	static const char * const usage = "ExecIfTime requires an argument:\n  <time range>,<days of week>,<days of month>,<months>[,<timezone>]?<appname>[(<appargs>)]";

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s\n", usage);
		return -1;
	}

	appname = ast_strdupa(data);

	s = strsep(&appname, "?");	/* Separate the timerange and application name/data */
	if (!appname) {	/* missing application */
		ast_log(LOG_WARNING, "%s\n", usage);
		return -1;
	}

	if (!ast_build_timing(&timing, s)) {
		ast_log(LOG_WARNING, "Invalid Time Spec: %s\nCorrect usage: %s\n", s, usage);
		ast_destroy_timing(&timing);
		return -1;
	}

	if (!ast_check_timing(&timing))	{ /* outside the valid time window, just return */
		ast_destroy_timing(&timing);
		return 0;
	}
	ast_destroy_timing(&timing);

	/* now split appname(appargs) */
	if ((s = strchr(appname, '('))) {
		char *e;
		*s++ = '\0';
		if ((e = strrchr(s, ')')))
			*e = '\0';
		else
			ast_log(LOG_WARNING, "Failed to find closing parenthesis\n");
	}


	if ((app = pbx_findapp(appname))) {
		return pbx_exec(chan, app, S_OR(s, ""));
	} else {
		ast_log(LOG_WARNING, "Cannot locate application %s\n", appname);
		return -1;
	}
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_wait(struct ast_channel *chan, const char *data)
{
	int ms;

	/* Wait for "n" seconds */
	if (!ast_app_parse_timelen(data, &ms, TIMELEN_SECONDS) && ms > 0) {
		return ast_safe_sleep(chan, ms);
	}
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_waitdigit(struct ast_channel *chan, const char *data)
{
	int res;
	int ms;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeout);
		AST_APP_ARG(digits);
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_app_parse_timelen(args.timeout, &ms, TIMELEN_SECONDS) || ms < 0) {
		pbx_builtin_setvar_helper(chan, "WAITDIGITSTATUS", "ERROR");
		return 0;
	}

	/* Wait for "n" seconds */
	res = ast_waitfordigit_full(chan, ms, S_OR(args.digits, AST_DIGIT_ANY), -1, -1);
	if (res < 0) {
		pbx_builtin_setvar_helper(chan, "WAITDIGITSTATUS", "CANCEL");
		return -1;
	}

	if (res == 0) {
		pbx_builtin_setvar_helper(chan, "WAITDIGITSTATUS", "TIMEOUT");
	} else {
		char key[2];

		snprintf(key, sizeof(key), "%c", res);
		pbx_builtin_setvar_helper(chan, "WAITDIGITRESULT", key);
		pbx_builtin_setvar_helper(chan, "WAITDIGITSTATUS", "DTMF");
	}

	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_waitexten(struct ast_channel *chan, const char *data)
{
	int ms, res;
	struct ast_flags flags = {0};
	char *opts[1] = { NULL };
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
	);

	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, parse);
	} else
		memset(&args, 0, sizeof(args));

	if (args.options)
		ast_app_parse_options(waitexten_opts, &flags, opts, args.options);

	if (ast_test_flag(&flags, WAITEXTEN_MOH) && !opts[0] ) {
		ast_log(LOG_WARNING, "The 'm' option has been specified for WaitExten without a class.\n");
	} else if (ast_test_flag(&flags, WAITEXTEN_MOH)) {
		ast_indicate_data(chan, AST_CONTROL_HOLD, S_OR(opts[0], NULL),
			!ast_strlen_zero(opts[0]) ? strlen(opts[0]) + 1 : 0);
	} else if (ast_test_flag(&flags, WAITEXTEN_DIALTONE)) {
		struct ast_tone_zone_sound *ts = ast_get_indication_tone(ast_channel_zone(chan), "dial");
		if (ts) {
			ast_playtones_start(chan, 0, ts->data, 0);
			ts = ast_tone_zone_sound_unref(ts);
		} else {
			ast_tonepair_start(chan, 350, 440, 0, 0);
		}
	}
	/* Wait for "n" seconds */
	if (!ast_app_parse_timelen(args.timeout, &ms, TIMELEN_SECONDS) && ms > 0) {
		/* Yay! */
	} else if (ast_channel_pbx(chan)) {
		ms = ast_channel_pbx(chan)->rtimeoutms;
	} else {
		ms = 10000;
	}

	res = ast_waitfordigit(chan, ms);
	if (!res) {
		if (ast_check_hangup(chan)) {
			/* Call is hungup for some reason. */
			res = -1;
		} else if (ast_exists_extension(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_verb(3, "Timeout on %s, continuing...\n", ast_channel_name(chan));
		} else if (ast_exists_extension(chan, ast_channel_context(chan), "t", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_verb(3, "Timeout on %s, going to 't'\n", ast_channel_name(chan));
			set_ext_pri(chan, "t", 0); /* 0 will become 1, next time through the loop */
		} else if (ast_exists_extension(chan, ast_channel_context(chan), "e", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			raise_exception(chan, "RESPONSETIMEOUT", 0); /* 0 will become 1, next time through the loop */
		} else {
			ast_log(LOG_WARNING, "Timeout but no rule 't' or 'e' in context '%s'\n",
				ast_channel_context(chan));
			res = -1;
		}
	}

	if (ast_test_flag(&flags, WAITEXTEN_MOH))
		ast_indicate(chan, AST_CONTROL_UNHOLD);
	else if (ast_test_flag(&flags, WAITEXTEN_DIALTONE))
		ast_playtones_stop(chan);

	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_background(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int mres = 0;
	struct ast_flags flags = {0};
	char *parse, exten[2] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
		AST_APP_ARG(lang);
		AST_APP_ARG(context);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Background requires an argument (filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.lang))
		args.lang = (char *)ast_channel_language(chan);	/* XXX this is const */

	if (ast_strlen_zero(args.context)) {
		const char *context;
		ast_channel_lock(chan);
		if ((context = pbx_builtin_getvar_helper(chan, "MACRO_CONTEXT"))) {
			args.context = ast_strdupa(context);
		} else {
			args.context = ast_strdupa(ast_channel_context(chan));
		}
		ast_channel_unlock(chan);
	}

	if (args.options) {
		if (!strcasecmp(args.options, "skip"))
			flags.flags = BACKGROUND_SKIP;
		else if (!strcasecmp(args.options, "noanswer"))
			flags.flags = BACKGROUND_NOANSWER;
		else
			ast_app_parse_options(background_opts, &flags, NULL, args.options);
	}

	/* Answer if need be */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_test_flag(&flags, BACKGROUND_SKIP)) {
			goto done;
		} else if (!ast_test_flag(&flags, BACKGROUND_NOANSWER)) {
			res = ast_answer(chan);
		}
	}

	if (!res) {
		char *back = ast_strip(args.filename);
		char *front;

		ast_stopstream(chan);		/* Stop anything playing */
		/* Stream the list of files */
		while (!res && (front = strsep(&back, "&")) ) {
			if ( (res = ast_streamfile(chan, front, args.lang)) ) {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", ast_channel_name(chan), (char*)data);
				res = 0;
				mres = 1;
				break;
			}
			if (ast_test_flag(&flags, BACKGROUND_PLAYBACK)) {
				res = ast_waitstream(chan, "");
			} else if (ast_test_flag(&flags, BACKGROUND_MATCHEXTEN)) {
				res = ast_waitstream_exten(chan, args.context);
			} else {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			}
			ast_stopstream(chan);
		}
	}

	/* If ast_waitstream didn't give us back a digit, there is nothing else to do */
	if (res <= 0) {
		goto done;
	}

	exten[0] = res;

	/*
	 * If the single digit DTMF is an extension in the specified context, then
	 * go there and signal no DTMF.  Otherwise, we should exit with that DTMF.
	 * If we're in Macro, we'll exit and seek that DTMF as the beginning of an
	 * extension in the Macro's calling context.  If we're not in Macro, then
	 * we'll simply seek that extension in the calling context.  Previously,
	 * someone complained about the behavior as it related to the interior of a
	 * Gosub routine, and the fix (#14011) inadvertently broke FreePBX
	 * (#14940).  This change should fix both of these situations, but with the
	 * possible incompatibility that if a single digit extension does not exist
	 * (but a longer extension COULD have matched), it would have previously
	 * gone immediately to the "i" extension, but will now need to wait for a
	 * timeout.
	 *
	 * Later, we had to add a flag to disable this workaround, because AGI
	 * users can EXEC Background and reasonably expect that the DTMF code will
	 * be returned (see #16434).
	 */
	if (!ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_WORKAROUNDS)
		&& ast_canmatch_extension(chan, args.context, exten, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))
		&& !ast_matchmore_extension(chan, args.context, exten, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
		char buf[2] = { 0, };
		snprintf(buf, sizeof(buf), "%c", res);
		ast_channel_exten_set(chan, buf);
		ast_channel_context_set(chan, args.context);
		ast_channel_priority_set(chan, 0);
		res = 0;
	}
done:
	pbx_builtin_setvar_helper(chan, "BACKGROUNDSTATUS", mres ? "FAILED" : "SUCCESS");
	return res;
}

static int pbx_builtin_noop(struct ast_channel *chan, const char *data)
{
	return 0;
}

static int pbx_builtin_gotoif(struct ast_channel *chan, const char *data)
{
	char *condition, *branch1, *branch2, *branch;
	char *stringp;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to check\n");
		return 0;
	}

	stringp = ast_strdupa(data);
	condition = strsep(&stringp,"?");
	branch1 = strsep(&stringp,":");
	branch2 = strsep(&stringp,"");
	branch = pbx_checkcondition(condition) ? branch1 : branch2;

	if (ast_strlen_zero(branch)) {
		ast_debug(1, "Not taking any branch\n");
		return 0;
	}

	return pbx_builtin_goto(chan, branch);
}

/*!
 * \brief Determine if DTMF interruption was requested.
 *
 * If the SAY_DTMF_INTERRUPT channel variable is truthy, the caller has
 * requested DTMF interruption be enabled.
 *
 * \param chan the channel to examine
 *
 * \retval -1 if DTMF interruption was requested
 * \retval  0 if DTMF interruption was not requested
 */
static int permit_dtmf_interrupt(struct ast_channel *chan)
{
	int interrupt;

	ast_channel_lock(chan);
	interrupt = ast_true(pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT"));
	ast_channel_unlock(chan);

	return interrupt;
}

static int pbx_builtin_saynumber(struct ast_channel *chan, const char *data)
{
	char tmp[256];
	char *number = tmp;
	int number_val;
	char *options;
	int res;
	int interrupt = permit_dtmf_interrupt(chan);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayNumber requires an argument (number)\n");
		return -1;
	}
	ast_copy_string(tmp, data, sizeof(tmp));
	strsep(&number, ",");

	if (ast_str_to_int(tmp, &number_val)) {
		ast_log(LOG_WARNING, "argument '%s' to SayNumber could not be parsed as a number.\n", tmp);
		return 0;
	}

	options = strsep(&number, ",");
	if (options) {
		if ( strcasecmp(options, "f") && strcasecmp(options, "m") &&
			strcasecmp(options, "c") && strcasecmp(options, "n") ) {
			ast_log(LOG_WARNING, "SayNumber gender option is either 'f', 'm', 'c' or 'n'\n");
			return -1;
		}
	}

	res = ast_say_number(chan, number_val, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan), options);

	if (res < 0 && !ast_check_hangup_locked(chan)) {
		ast_log(LOG_WARNING, "We were unable to say the number %s, is it too large?\n", tmp);
	}

	return interrupt ? res : 0;
}

static int pbx_builtin_sayordinal(struct ast_channel *chan, const char *data)
{
	char tmp[256];
	char *number = tmp;
	int number_val;
	char *options;
	int res;
	int interrupt = permit_dtmf_interrupt(chan);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayOrdinal requires an argument (number)\n");
		return -1;
	}
	ast_copy_string(tmp, data, sizeof(tmp));
	strsep(&number, ",");

	if (ast_str_to_int(tmp, &number_val)) {
		ast_log(LOG_WARNING, "argument '%s' to SayOrdinal could not be parsed as a number.\n", tmp);
		return 0;
	}

	options = strsep(&number, ",");
	if (options) {
		if ( strcasecmp(options, "f") && strcasecmp(options, "m") &&
			strcasecmp(options, "c") && strcasecmp(options, "n") ) {
			ast_log(LOG_WARNING, "SayOrdinal gender option is either 'f', 'm', 'c' or 'n'\n");
			return -1;
		}
	}

	res = ast_say_ordinal(chan, number_val, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan), options);

	if (res < 0 && !ast_check_hangup_locked(chan)) {
		ast_log(LOG_WARNING, "We were unable to say the number %s, is it too large?\n", tmp);
	}

	return interrupt ? res : 0;
}

static int pbx_builtin_saydigits(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data) {
		res = ast_say_digit_str(chan, data, permit_dtmf_interrupt(chan) ? AST_DIGIT_ANY : "", ast_channel_language(chan));
	}

	return res;
}

static int pbx_builtin_saymoney(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data) {
		res = ast_say_money_str(chan, data, permit_dtmf_interrupt(chan) ? AST_DIGIT_ANY : "", ast_channel_language(chan));
	}

	return res;
}

static int pbx_builtin_saycharacters_case(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int sensitivity = 0;
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
		AST_APP_ARG(characters);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayAlphaCase requires two arguments (options, characters)\n");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!args.options || strlen(args.options) != 1) {
		ast_log(LOG_WARNING, "SayAlphaCase options are mutually exclusive and required\n");
		return 0;
	}

	switch (args.options[0]) {
	case 'a':
		sensitivity = AST_SAY_CASE_ALL;
		break;
	case 'l':
		sensitivity = AST_SAY_CASE_LOWER;
		break;
	case 'n':
		sensitivity = AST_SAY_CASE_NONE;
		break;
	case 'u':
		sensitivity = AST_SAY_CASE_UPPER;
		break;
	default:
		ast_log(LOG_WARNING, "Invalid option: '%s'\n", args.options);
		return 0;
	}

	res = ast_say_character_str(chan, args.characters, permit_dtmf_interrupt(chan) ? AST_DIGIT_ANY : "", ast_channel_language(chan), sensitivity);

	return res;
}

static int pbx_builtin_saycharacters(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data) {
		res = ast_say_character_str(chan, data, permit_dtmf_interrupt(chan) ? AST_DIGIT_ANY : "", ast_channel_language(chan), AST_SAY_CASE_NONE);
	}

	return res;
}

static int pbx_builtin_sayphonetic(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data) {
		res = ast_say_phonetic_str(chan, data, permit_dtmf_interrupt(chan) ? AST_DIGIT_ANY : "", ast_channel_language(chan));
	}

	return res;
}

static int pbx_builtin_importvar(struct ast_channel *chan, const char *data)
{
	char *name;
	char *value;
	char *channel;
	char tmp[VAR_BUF_SIZE];
	static int deprecation_warning = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}
	tmp[0] = 0;
	if (!deprecation_warning) {
		ast_log(LOG_WARNING, "ImportVar is deprecated.  Please use Set(varname=${IMPORT(channel,variable)}) instead.\n");
		deprecation_warning = 1;
	}

	value = ast_strdupa(data);
	name = strsep(&value,"=");
	channel = strsep(&value,",");
	if (channel && value && name) { /*! \todo XXX should do !ast_strlen_zero(..) of the args ? */
		struct ast_channel *chan2 = ast_channel_get_by_name(channel);
		if (chan2) {
			char *s = ast_alloca(strlen(value) + 4);
			sprintf(s, "${%s}", value);
			pbx_substitute_variables_helper(chan2, s, tmp, sizeof(tmp) - 1);
			chan2 = ast_channel_unref(chan2);
		}
		pbx_builtin_setvar_helper(chan, name, tmp);
	}

	return(0);
}

/*! \brief Declaration of builtin applications */
struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, const char *data);
} builtins[] =
{
	/* These applications are built into the PBX core and do not
	   need separate modules */

	{ "Answer",         pbx_builtin_answer },
	{ "BackGround",     pbx_builtin_background },
	{ "Busy",           indicate_busy },
	{ "Congestion",     indicate_congestion },
	{ "ExecIfTime",     pbx_builtin_execiftime },
	{ "Goto",           pbx_builtin_goto },
	{ "GotoIf",         pbx_builtin_gotoif },
	{ "GotoIfTime",     pbx_builtin_gotoiftime },
	{ "ImportVar",      pbx_builtin_importvar },
	{ "Hangup",         pbx_builtin_hangup },
	{ "Incomplete",     pbx_builtin_incomplete },
	{ "NoOp",           pbx_builtin_noop },
	{ "Proceeding",     pbx_builtin_proceeding },
	{ "Progress",       pbx_builtin_progress },
	{ "RaiseException", pbx_builtin_raise_exception },
	{ "Ringing",        pbx_builtin_ringing },
	{ "SayAlpha",       pbx_builtin_saycharacters },
	{ "SayAlphaCase",   pbx_builtin_saycharacters_case },
	{ "SayDigits",      pbx_builtin_saydigits },
	{ "SayMoney",       pbx_builtin_saymoney },
	{ "SayNumber",      pbx_builtin_saynumber },
	{ "SayOrdinal",     pbx_builtin_sayordinal },
	{ "SayPhonetic",    pbx_builtin_sayphonetic },
	{ "SetAMAFlags",    pbx_builtin_setamaflags },
	{ "Wait",           pbx_builtin_wait },
	{ "WaitDigit",      pbx_builtin_waitdigit },
	{ "WaitExten",      pbx_builtin_waitexten }
};

static void unload_pbx_builtins(void)
{
	int x;

	/* Unregister builtin applications */
	for (x = 0; x < ARRAY_LEN(builtins); x++) {
		ast_unregister_application(builtins[x].name);
	}
}

int load_pbx_builtins(void)
{
	int x;

	/* Register builtin applications */
	for (x = 0; x < ARRAY_LEN(builtins); x++) {
		if (ast_register_application2(builtins[x].name, builtins[x].execute, NULL, NULL, NULL)) {
			ast_log(LOG_ERROR, "Unable to register builtin application '%s'\n", builtins[x].name);
			return -1;
		}
	}

	ast_register_cleanup(unload_pbx_builtins);

	return 0;
}
