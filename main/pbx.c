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
 * \brief Core PBX routines.
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_SYSTEM_NAME */
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#if defined(HAVE_SYSINFO)
#include <sys/sysinfo.h>
#endif
#if defined(SOLARIS)
#include <sys/loadavg.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/cdr.h"
#include "asterisk/cel.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/time.h"
#include "asterisk/manager.h"
#include "asterisk/ast_expr.h"
#include "asterisk/linkedlists.h"
#define	SAY_STUBS	/* generate declarations and stubs for say methods */
#include "asterisk/say.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"
#include "asterisk/devicestate.h"
#include "asterisk/presencestate.h"
#include "asterisk/event.h"
#include "asterisk/hashtab.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/xmldoc.h"
#include "asterisk/astobj2.h"

/*!
 * \note I M P O R T A N T :
 *
 *		The speed of extension handling will likely be among the most important
 * aspects of this PBX.  The switching scheme as it exists right now isn't
 * terribly bad (it's O(N+M), where N is the # of extensions and M is the avg #
 * of priorities, but a constant search time here would be great ;-)
 *
 * A new algorithm to do searching based on a 'compiled' pattern tree is introduced
 * here, and shows a fairly flat (constant) search time, even for over
 * 10000 patterns.
 *
 * Also, using a hash table for context/priority name lookup can help prevent
 * the find_extension routines from absorbing exponential cpu cycles as the number
 * of contexts/priorities grow. I've previously tested find_extension with red-black trees,
 * which have O(log2(n)) speed. Right now, I'm using hash tables, which do
 * searches (ideally) in O(1) time. While these techniques do not yield much
 * speed in small dialplans, they are worth the trouble in large dialplans.
 *
 */

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
			<parameter name="nocdr">
				<para>Asterisk will send an answer signal to the calling phone, but will not
				set the disposition or answer time in the CDR for this call.</para>
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
	<application name="ResetCDR" language="en_US">
		<synopsis>
			Resets the Call Data Record.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="w">
						<para>Store the current CDR record before resetting it.</para>
					</option>
					<option name="a">
						<para>Store any stacked records.</para>
					</option>
					<option name="v">
						<para>Save CDR variables.</para>
					</option>
					<option name="e">
						<para>Enable CDR only (negate effects of NoCDR).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application causes the Call Data Record to be reset.</para>
		</description>
		<see-also>
			<ref type="application">ForkCDR</ref>
			<ref type="application">NoCDR</ref>
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
			<para>This application will play the sounds that correspond to the letters of the
			given <replaceable>string</replaceable>.</para>
		</description>
		<see-also>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayPhonetic</ref>
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
			the given number. This will use the language that is currently set for the channel.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
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
			<para>This application will play the sounds that correspond to the given <replaceable>digits</replaceable>.
			Optionally, a <replaceable>gender</replaceable> may be specified. This will use the language that is currently
			set for the channel. See the CHANNEL() function for more information on setting the language for the channel.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayPhonetic</ref>
			<ref type="function">CHANNEL</ref>
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
			letters in the given <replaceable>string</replaceable>.</para>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayNumber</ref>
		</see-also>
	</application>
	<application name="Set" language="en_US">
		<synopsis>
			Set channel variable or function value.
		</synopsis>
		<syntax argsep="=">
			<parameter name="name" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>This function can be used to set the value of channel variables or dialplan functions.
			When setting variables, if the variable name is prefixed with <literal>_</literal>,
			the variable will be inherited into channels created from the current channel.
			If the variable name is prefixed with <literal>__</literal>, the variable will be
			inherited into channels created from the current channel and all children channels.</para>
			<note><para>If (and only if), in <filename>/etc/asterisk/asterisk.conf</filename>, you have
			a <literal>[compat]</literal> category, and you have <literal>app_set = 1.4</literal> under that, then
			the behavior of this app changes, and strips surrounding quotes from the right hand side as
			it did previously in 1.4.
			The advantages of not stripping out quoting, and not caring about the separator characters (comma and vertical bar)
			were sufficient to make these changes in 1.6. Confusion about how many backslashes would be needed to properly
			protect separators and quotes in various database access strings has been greatly
			reduced by these changes.</para></note>
		</description>
		<see-also>
			<ref type="application">MSet</ref>
			<ref type="function">GLOBAL</ref>
			<ref type="function">SET</ref>
			<ref type="function">ENV</ref>
		</see-also>
	</application>
	<application name="MSet" language="en_US">
		<synopsis>
			Set channel variable(s) or function value(s).
		</synopsis>
		<syntax>
			<parameter name="set1" required="true" argsep="=">
				<argument name="name1" required="true" />
				<argument name="value1" required="true" />
			</parameter>
			<parameter name="set2" multiple="true" argsep="=">
				<argument name="name2" required="true" />
				<argument name="value2" required="true" />
			</parameter>
		</syntax>
		<description>
			<para>This function can be used to set the value of channel variables or dialplan functions.
			When setting variables, if the variable name is prefixed with <literal>_</literal>,
			the variable will be inherited into channels created from the current channel
			If the variable name is prefixed with <literal>__</literal>, the variable will be
			inherited into channels created from the current channel and all children channels.
			MSet behaves in a similar fashion to the way Set worked in 1.2/1.4 and is thus
			prone to doing things that you may not expect. For example, it strips surrounding
			double-quotes from the right-hand side (value). If you need to put a separator
			character (comma or vert-bar), you will need to escape them by inserting a backslash
			before them. Avoid its use if possible.</para>
		</description>
		<see-also>
			<ref type="application">Set</ref>
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
		</description>
		<see-also>
			<ref type="function">CDR</ref>
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
	<function name="EXCEPTION" language="en_US">
		<synopsis>
			Retrieve the details of the current dialplan exception.
		</synopsis>
		<syntax>
			<parameter name="field" required="true">
				<para>The following fields are available for retrieval:</para>
				<enumlist>
					<enum name="reason">
						<para>INVALID, ERROR, RESPONSETIMEOUT, ABSOLUTETIMEOUT, or custom
						value set by the RaiseException() application</para>
					</enum>
					<enum name="context">
						<para>The context executing when the exception occurred.</para>
					</enum>
					<enum name="exten">
						<para>The extension executing when the exception occurred.</para>
					</enum>
					<enum name="priority">
						<para>The numeric priority executing when the exception occurred.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Retrieve the details (specified <replaceable>field</replaceable>) of the current dialplan exception.</para>
		</description>
		<see-also>
			<ref type="application">RaiseException</ref>
		</see-also>
	</function>
	<function name="TESTTIME" language="en_US">
		<synopsis>
			Sets a time to be used with the channel to test logical conditions.
		</synopsis>
		<syntax>
			<parameter name="date" required="true" argsep=" ">
				<para>Date in ISO 8601 format</para>
			</parameter>
			<parameter name="time" required="true" argsep=" ">
				<para>Time in HH:MM:SS format (24-hour time)</para>
			</parameter>
			<parameter name="zone" required="false">
				<para>Timezone name</para>
			</parameter>
		</syntax>
		<description>
			<para>To test dialplan timing conditions at times other than the current time, use
			this function to set an alternate date and time.  For example, you may wish to evaluate
			whether a location will correctly identify to callers that the area is closed on Christmas
			Day, when Christmas would otherwise fall on a day when the office is normally open.</para>
		</description>
		<see-also>
			<ref type="application">GotoIfTime</ref>
		</see-also>
	</function>
	<manager name="ShowDialPlan" language="en_US">
		<synopsis>
			Show dialplan contexts and extensions
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Extension">
				<para>Show a specific extension.</para>
			</parameter>
			<parameter name="Context">
				<para>Show a specific context.</para>
			</parameter>
		</syntax>
		<description>
			<para>Show dialplan contexts and extensions. Be aware that showing the full dialplan
			may take a lot of capacity.</para>
		</description>
	</manager>
 ***/

#ifdef LOW_MEMORY
#define EXT_DATA_SIZE 256
#else
#define EXT_DATA_SIZE 8192
#endif

#define SWITCH_DATA_LENGTH 256

#define VAR_BUF_SIZE 4096

#define	VAR_NORMAL		1
#define	VAR_SOFTTRAN	2
#define	VAR_HARDTRAN	3

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

struct ast_context;
struct ast_app;

static struct ast_taskprocessor *extension_state_tps;

AST_THREADSTORAGE(switch_data);
AST_THREADSTORAGE(extensionstate_buf);
/*!
 * \brief A thread local indicating whether the current thread can run
 * 'dangerous' dialplan functions.
 */
AST_THREADSTORAGE(thread_inhibit_escalations_tl);

/*!
 * \brief Set to true (non-zero) to globally allow all dangerous dialplan
 * functions to run.
 */
static int live_dangerously;

/*!
   \brief ast_exten: An extension
	The dialplan is saved as a linked list with each context
	having it's own linked list of extensions - one item per
	priority.
*/
struct ast_exten {
	char *exten;			/*!< Extension name */
	int matchcid;			/*!< Match caller id ? */
	const char *cidmatch;		/*!< Caller id to match for this extension */
	int priority;			/*!< Priority */
	const char *label;		/*!< Label */
	struct ast_context *parent;	/*!< The context this extension belongs to  */
	const char *app;		/*!< Application to execute */
	struct ast_app *cached_app;     /*!< Cached location of application */
	void *data;			/*!< Data to use (arguments) */
	void (*datad)(void *);		/*!< Data destructor */
	struct ast_exten *peer;		/*!< Next higher priority with our extension */
	struct ast_hashtab *peer_table;    /*!< Priorities list in hashtab form -- only on the head of the peer list */
	struct ast_hashtab *peer_label_table; /*!< labeled priorities in the peers -- only on the head of the peer list */
	const char *registrar;		/*!< Registrar */
	struct ast_exten *next;		/*!< Extension with a greater ID */
	char stuff[0];
};

/*! \brief ast_include: include= support in extensions.conf */
struct ast_include {
	const char *name;
	const char *rname;			/*!< Context to include */
	const char *registrar;			/*!< Registrar */
	int hastime;				/*!< If time construct exists */
	struct ast_timing timing;               /*!< time construct */
	struct ast_include *next;		/*!< Link them together */
	char stuff[0];
};

/*! \brief ast_sw: Switch statement in extensions.conf */
struct ast_sw {
	char *name;
	const char *registrar;			/*!< Registrar */
	char *data;				/*!< Data load */
	int eval;
	AST_LIST_ENTRY(ast_sw) list;
	char stuff[0];
};

/*! \brief ast_ignorepat: Ignore patterns in dial plan */
struct ast_ignorepat {
	const char *registrar;
	struct ast_ignorepat *next;
	const char pattern[0];
};

/*! \brief match_char: forms a syntax tree for quick matching of extension patterns */
struct match_char
{
	int is_pattern; /* the pattern started with '_' */
	int deleted;    /* if this is set, then... don't return it */
	int specificity; /* simply the strlen of x, or 10 for X, 9 for Z, and 8 for N; and '.' and '!' will add 11 ? */
	struct match_char *alt_char;
	struct match_char *next_char;
	struct ast_exten *exten; /* attached to last char of a pattern for exten */
	char x[1];       /* the pattern itself-- matches a single char */
};

struct scoreboard  /* make sure all fields are 0 before calling new_find_extension */
{
	int total_specificity;
	int total_length;
	char last_char;   /* set to ! or . if they are the end of the pattern */
	int canmatch;     /* if the string to match was just too short */
	struct match_char *node;
	struct ast_exten *canmatch_exten;
	struct ast_exten *exten;
};

/*! \brief ast_context: An extension context */
struct ast_context {
	ast_rwlock_t lock;			/*!< A lock to prevent multiple threads from clobbering the context */
	struct ast_exten *root;			/*!< The root of the list of extensions */
	struct ast_hashtab *root_table;            /*!< For exact matches on the extensions in the pattern tree, and for traversals of the pattern_tree  */
	struct match_char *pattern_tree;        /*!< A tree to speed up extension pattern matching */
	struct ast_context *next;		/*!< Link them together */
	struct ast_include *includes;		/*!< Include other contexts */
	struct ast_ignorepat *ignorepats;	/*!< Patterns for which to continue playing dialtone */
	char *registrar;			/*!< Registrar -- make sure you malloc this, as the registrar may have to survive module unloads */
	int refcount;                   /*!< each module that would have created this context should inc/dec this as appropriate */
	AST_LIST_HEAD_NOLOCK(, ast_sw) alts;	/*!< Alternative switches */
	ast_mutex_t macrolock;			/*!< A lock to implement "exclusive" macros - held whilst a call is executing in the macro */
	char name[0];				/*!< Name of the context */
};

/*! \brief ast_app: A registered application */
struct ast_app {
	int (*execute)(struct ast_channel *chan, const char *data);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(synopsis);     /*!< Synopsis text for 'show applications' */
		AST_STRING_FIELD(description);  /*!< Description (help text) for 'show application &lt;name&gt;' */
		AST_STRING_FIELD(syntax);       /*!< Syntax text for 'core show applications' */
		AST_STRING_FIELD(arguments);    /*!< Arguments description */
		AST_STRING_FIELD(seealso);      /*!< See also */
	);
#ifdef AST_XML_DOCS
	enum ast_doc_src docsrc;		/*!< Where the documentation come from. */
#endif
	AST_RWLIST_ENTRY(ast_app) list;		/*!< Next app in list */
	struct ast_module *module;		/*!< Module this app belongs to */
	char name[0];				/*!< Name of the application */
};

/*! \brief ast_state_cb: An extension state notify register item */
struct ast_state_cb {
	/*! Watcher ID returned when registered. */
	int id;
	/*! Arbitrary data passed for callbacks. */
	void *data;
	/*! Flag if this callback is an extended callback containing detailed device status */
	int extended;
	/*! Callback when state changes. */
	ast_state_cb_type change_cb;
	/*! Callback when destroyed so any resources given by the registerer can be freed. */
	ast_state_cb_destroy_type destroy_cb;
	/*! \note Only used by ast_merge_contexts_and_delete */
	AST_LIST_ENTRY(ast_state_cb) entry;
};

/*!
 * \brief Structure for dial plan hints
 *
 * \note Hints are pointers from an extension in the dialplan to
 * one or more devices (tech/name)
 *
 * See \ref AstExtState
 */
struct ast_hint {
	/*!
	 * \brief Hint extension
	 *
	 * \note
	 * Will never be NULL while the hint is in the hints container.
	 */
	struct ast_exten *exten;
	struct ao2_container *callbacks; /*!< Device state callback container for this extension */

	/*! Dev state variables */
	int laststate;			/*!< Last known device state */

	/*! Presence state variables */
	int last_presence_state;     /*!< Last known presence state */
	char *last_presence_subtype; /*!< Last known presence subtype string */
	char *last_presence_message; /*!< Last known presence message string */

	char context_name[AST_MAX_CONTEXT];/*!< Context of destroyed hint extension. */
	char exten_name[AST_MAX_EXTENSION];/*!< Extension of destroyed hint extension. */
};


#define HINTDEVICE_DATA_LENGTH 16
AST_THREADSTORAGE(hintdevice_data);

/* --- Hash tables of various objects --------*/
#ifdef LOW_MEMORY
#define HASH_EXTENHINT_SIZE 17
#else
#define HASH_EXTENHINT_SIZE 563
#endif


/*! \brief Container for hint devices */
static struct ao2_container *hintdevices;

/*!
 * \brief Structure for dial plan hint devices
 * \note hintdevice is one device pointing to a hint.
 */
struct ast_hintdevice {
	/*!
	 * \brief Hint this hintdevice belongs to.
	 * \note Holds a reference to the hint object.
	 */
	struct ast_hint *hint;
	/*! Name of the hint device. */
	char hintdevice[1];
};


/*!
 * \note Using the device for hash
 */
static int hintdevice_hash_cb(const void *obj, const int flags)
{
	const struct ast_hintdevice *ext = obj;

	return ast_str_case_hash(ext->hintdevice);
}
/*!
 * \note Devices on hints are not unique so no CMP_STOP is returned
 * Dont use ao2_find against hintdevices container cause there always
 * could be more than one result.
 */
static int hintdevice_cmp_multiple(void *obj, void *arg, int flags)
{
	struct ast_hintdevice *ext = obj, *ext2 = arg;

	return !strcasecmp(ext->hintdevice, ext2->hintdevice) ? CMP_MATCH  : 0;
}

/*
 * \details This is used with ao2_callback to remove old devices
 * when they are linked to the hint
*/
static int hintdevice_remove_cb(void *deviceobj, void *arg, int flags)
{
	struct ast_hintdevice *device = deviceobj;
	struct ast_hint *hint = arg;

	return (device->hint == hint) ? CMP_MATCH : 0;
}

static int remove_hintdevice(struct ast_hint *hint)
{
	/* iterate through all devices and remove the devices which are linked to this hint */
	ao2_t_callback(hintdevices, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK,
		hintdevice_remove_cb, hint,
		"callback to remove all devices which are linked to a hint");
	return 0;
}

static char *parse_hint_device(struct ast_str *hint_args);
/*!
 * \internal
 * \brief Destroy the given hintdevice object.
 *
 * \param obj Hint device to destroy.
 *
 * \return Nothing
 */
static void hintdevice_destroy(void *obj)
{
	struct ast_hintdevice *doomed = obj;

	if (doomed->hint) {
		ao2_ref(doomed->hint, -1);
		doomed->hint = NULL;
	}
}

/*! \brief add hintdevice structure and link it into the container.
 */
static int add_hintdevice(struct ast_hint *hint, const char *devicelist)
{
	struct ast_str *str;
	char *parse;
	char *cur;
	struct ast_hintdevice *device;
	int devicelength;

	if (!hint || !devicelist) {
		/* Trying to add garbage? Don't bother. */
		return 0;
	}
	if (!(str = ast_str_thread_get(&hintdevice_data, 16))) {
		return -1;
	}
	ast_str_set(&str, 0, "%s", devicelist);
	parse = parse_hint_device(str);

	while ((cur = strsep(&parse, "&"))) {
		devicelength = strlen(cur);
		device = ao2_t_alloc(sizeof(*device) + devicelength, hintdevice_destroy,
			"allocating a hintdevice structure");
		if (!device) {
			return -1;
		}
		strcpy(device->hintdevice, cur);
		ao2_ref(hint, +1);
		device->hint = hint;
		ao2_t_link(hintdevices, device, "Linking device into hintdevice container.");
		ao2_t_ref(device, -1, "hintdevice is linked so we can unref");
	}

	return 0;
}


static const struct cfextension_states {
	int extension_state;
	const char * const text;
} extension_states[] = {
	{ AST_EXTENSION_NOT_INUSE,                     "Idle" },
	{ AST_EXTENSION_INUSE,                         "InUse" },
	{ AST_EXTENSION_BUSY,                          "Busy" },
	{ AST_EXTENSION_UNAVAILABLE,                   "Unavailable" },
	{ AST_EXTENSION_RINGING,                       "Ringing" },
	{ AST_EXTENSION_INUSE | AST_EXTENSION_RINGING, "InUse&Ringing" },
	{ AST_EXTENSION_ONHOLD,                        "Hold" },
	{ AST_EXTENSION_INUSE | AST_EXTENSION_ONHOLD,  "InUse&Hold" }
};

struct presencechange {
	char *provider;
	int state;
	char *subtype;
	char *message;
};

struct statechange {
	AST_LIST_ENTRY(statechange) entry;
	char dev[0];
};

struct pbx_exception {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(context);	/*!< Context associated with this exception */
		AST_STRING_FIELD(exten);	/*!< Exten associated with this exception */
		AST_STRING_FIELD(reason);		/*!< The exception reason */
	);

	int priority;				/*!< Priority associated with this exception */
};

static int pbx_builtin_answer(struct ast_channel *, const char *);
static int pbx_builtin_goto(struct ast_channel *, const char *);
static int pbx_builtin_hangup(struct ast_channel *, const char *);
static int pbx_builtin_background(struct ast_channel *, const char *);
static int pbx_builtin_wait(struct ast_channel *, const char *);
static int pbx_builtin_waitexten(struct ast_channel *, const char *);
static int pbx_builtin_incomplete(struct ast_channel *, const char *);
static int pbx_builtin_resetcdr(struct ast_channel *, const char *);
static int pbx_builtin_setamaflags(struct ast_channel *, const char *);
static int pbx_builtin_ringing(struct ast_channel *, const char *);
static int pbx_builtin_proceeding(struct ast_channel *, const char *);
static int pbx_builtin_progress(struct ast_channel *, const char *);
static int pbx_builtin_congestion(struct ast_channel *, const char *);
static int pbx_builtin_busy(struct ast_channel *, const char *);
static int pbx_builtin_noop(struct ast_channel *, const char *);
static int pbx_builtin_gotoif(struct ast_channel *, const char *);
static int pbx_builtin_gotoiftime(struct ast_channel *, const char *);
static int pbx_builtin_execiftime(struct ast_channel *, const char *);
static int pbx_builtin_saynumber(struct ast_channel *, const char *);
static int pbx_builtin_saydigits(struct ast_channel *, const char *);
static int pbx_builtin_saycharacters(struct ast_channel *, const char *);
static int pbx_builtin_sayphonetic(struct ast_channel *, const char *);
static int matchcid(const char *cidpattern, const char *callerid);
#ifdef NEED_DEBUG
static void log_match_char_tree(struct match_char *node, char *prefix); /* for use anywhere */
#endif
static int pbx_builtin_importvar(struct ast_channel *, const char *);
static void set_ext_pri(struct ast_channel *c, const char *exten, int pri);
static void new_find_extension(const char *str, struct scoreboard *score,
		struct match_char *tree, int length, int spec, const char *callerid,
		const char *label, enum ext_match_t action);
static struct match_char *already_in_tree(struct match_char *current, char *pat, int is_pattern);
static struct match_char *add_exten_to_pattern_tree(struct ast_context *con,
		struct ast_exten *e1, int findonly);
static void create_match_char_tree(struct ast_context *con);
static struct ast_exten *get_canmatch_exten(struct match_char *node);
static void destroy_pattern_tree(struct match_char *pattern_tree);
static int hashtab_compare_extens(const void *ha_a, const void *ah_b);
static int hashtab_compare_exten_numbers(const void *ah_a, const void *ah_b);
static int hashtab_compare_exten_labels(const void *ah_a, const void *ah_b);
static unsigned int hashtab_hash_extens(const void *obj);
static unsigned int hashtab_hash_priority(const void *obj);
static unsigned int hashtab_hash_labels(const void *obj);
static void __ast_internal_context_destroy( struct ast_context *con);
static int ast_add_extension_nolock(const char *context, int replace, const char *extension,
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar);
static int ast_add_extension2_lockopt(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar, int lock_context);
static struct ast_context *find_context_locked(const char *context);
static struct ast_context *find_context(const char *context);
static void get_device_state_causing_channels(struct ao2_container *c);

/*!
 * \internal
 * \brief Character array comparison function for qsort.
 *
 * \param a Left side object.
 * \param b Right side object.
 *
 * \retval <0 if a < b
 * \retval =0 if a = b
 * \retval >0 if a > b
 */
static int compare_char(const void *a, const void *b)
{
	const unsigned char *ac = a;
	const unsigned char *bc = b;

	return *ac - *bc;
}

/* labels, contexts are case sensitive  priority numbers are ints */
int ast_hashtab_compare_contexts(const void *ah_a, const void *ah_b)
{
	const struct ast_context *ac = ah_a;
	const struct ast_context *bc = ah_b;
	if (!ac || !bc) /* safety valve, but it might prevent a crash you'd rather have happen */
		return 1;
	/* assume context names are registered in a string table! */
	return strcmp(ac->name, bc->name);
}

static int hashtab_compare_extens(const void *ah_a, const void *ah_b)
{
	const struct ast_exten *ac = ah_a;
	const struct ast_exten *bc = ah_b;
	int x = strcmp(ac->exten, bc->exten);
	if (x) { /* if exten names are diff, then return */
		return x;
	}

	/* but if they are the same, do the cidmatch values match? */
	/* not sure which side may be using ast_ext_matchcid_types, so check both */
	if (ac->matchcid == AST_EXT_MATCHCID_ANY || bc->matchcid == AST_EXT_MATCHCID_ANY) {
		return 0;
	}
	if (ac->matchcid == AST_EXT_MATCHCID_OFF && bc->matchcid == AST_EXT_MATCHCID_OFF) {
		return 0;
	}
	if (ac->matchcid != bc->matchcid) {
		return 1;
	}
	/* all other cases already disposed of, match now required on callerid string (cidmatch) */
	/* although ast_add_extension2_lockopt() enforces non-zero ptr, caller may not have */
	if (ast_strlen_zero(ac->cidmatch) && ast_strlen_zero(bc->cidmatch)) {
		return 0;
	}
	return strcmp(ac->cidmatch, bc->cidmatch);
}

static int hashtab_compare_exten_numbers(const void *ah_a, const void *ah_b)
{
	const struct ast_exten *ac = ah_a;
	const struct ast_exten *bc = ah_b;
	return ac->priority != bc->priority;
}

static int hashtab_compare_exten_labels(const void *ah_a, const void *ah_b)
{
	const struct ast_exten *ac = ah_a;
	const struct ast_exten *bc = ah_b;
	return strcmp(S_OR(ac->label, ""), S_OR(bc->label, ""));
}

unsigned int ast_hashtab_hash_contexts(const void *obj)
{
	const struct ast_context *ac = obj;
	return ast_hashtab_hash_string(ac->name);
}

static unsigned int hashtab_hash_extens(const void *obj)
{
	const struct ast_exten *ac = obj;
	unsigned int x = ast_hashtab_hash_string(ac->exten);
	unsigned int y = 0;
	if (ac->matchcid == AST_EXT_MATCHCID_ON)
		y = ast_hashtab_hash_string(ac->cidmatch);
	return x+y;
}

static unsigned int hashtab_hash_priority(const void *obj)
{
	const struct ast_exten *ac = obj;
	return ast_hashtab_hash_int(ac->priority);
}

static unsigned int hashtab_hash_labels(const void *obj)
{
	const struct ast_exten *ac = obj;
	return ast_hashtab_hash_string(S_OR(ac->label, ""));
}


AST_RWLOCK_DEFINE_STATIC(globalslock);
static struct varshead globals = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

static int autofallthrough = 1;
static int extenpatternmatchnew = 0;
static char *overrideswitch = NULL;

/*! \brief Subscription for device state change events */
static struct ast_event_sub *device_state_sub;
/*! \brief Subscription for presence state change events */
static struct ast_event_sub *presence_state_sub;

AST_MUTEX_DEFINE_STATIC(maxcalllock);
static int countcalls;
static int totalcalls;

static AST_RWLIST_HEAD_STATIC(acf_root, ast_custom_function);

/*!
 * \brief Extra information for an \ref ast_custom_function holding privilege
 * escalation information. Kept in a separate structure for ABI compatibility.
 */
struct ast_custom_escalating_function {
	AST_RWLIST_ENTRY(ast_custom_escalating_function) list;
	const struct ast_custom_function *acf;
	unsigned int read_escalates:1;
	unsigned int write_escalates:1;
};

static AST_RWLIST_HEAD_STATIC(escalation_root, ast_custom_escalating_function);

/*! \brief Declaration of builtin applications */
static struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, const char *data);
} builtins[] =
{
	/* These applications are built into the PBX core and do not
	   need separate modules */

	{ "Answer",         pbx_builtin_answer },
	{ "BackGround",     pbx_builtin_background },
	{ "Busy",           pbx_builtin_busy },
	{ "Congestion",     pbx_builtin_congestion },
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
	{ "ResetCDR",       pbx_builtin_resetcdr },
	{ "Ringing",        pbx_builtin_ringing },
	{ "SayAlpha",       pbx_builtin_saycharacters },
	{ "SayDigits",      pbx_builtin_saydigits },
	{ "SayNumber",      pbx_builtin_saynumber },
	{ "SayPhonetic",    pbx_builtin_sayphonetic },
	{ "Set",            pbx_builtin_setvar },
	{ "MSet",           pbx_builtin_setvar_multiple },
	{ "SetAMAFlags",    pbx_builtin_setamaflags },
	{ "Wait",           pbx_builtin_wait },
	{ "WaitExten",      pbx_builtin_waitexten }
};

static struct ast_context *contexts;
static struct ast_hashtab *contexts_table = NULL;

/*!
 * \brief Lock for the ast_context list
 * \note
 * This lock MUST be recursive, or a deadlock on reload may result.  See
 * https://issues.asterisk.org/view.php?id=17643
 */
AST_MUTEX_DEFINE_STATIC(conlock);

/*!
 * \brief Lock to hold off restructuring of hints by ast_merge_contexts_and_delete.
 */
AST_MUTEX_DEFINE_STATIC(context_merge_lock);

static AST_RWLIST_HEAD_STATIC(apps, ast_app);

static AST_RWLIST_HEAD_STATIC(switches, ast_switch);

static int stateid = 1;
/*!
 * \note When holding this container's lock, do _not_ do
 * anything that will cause conlock to be taken, unless you
 * _already_ hold it.  The ast_merge_contexts_and_delete function
 * will take the locks in conlock/hints order, so any other
 * paths that require both locks must also take them in that
 * order.
 */
static struct ao2_container *hints;

static struct ao2_container *statecbs;

#ifdef CONTEXT_DEBUG

/* these routines are provided for doing run-time checks
   on the extension structures, in case you are having
   problems, this routine might help you localize where
   the problem is occurring. It's kinda like a debug memory
   allocator's arena checker... It'll eat up your cpu cycles!
   but you'll see, if you call it in the right places,
   right where your problems began...
*/

/* you can break on the check_contexts_trouble()
routine in your debugger to stop at the moment
there's a problem */
void check_contexts_trouble(void);

void check_contexts_trouble(void)
{
	int x = 1;
	x = 2;
}

int check_contexts(char *, int);

int check_contexts(char *file, int line )
{
	struct ast_hashtab_iter *t1;
	struct ast_context *c1, *c2;
	int found = 0;
	struct ast_exten *e1, *e2, *e3;
	struct ast_exten ex;

	/* try to find inconsistencies */
	/* is every context in the context table in the context list and vice-versa ? */

	if (!contexts_table) {
		ast_log(LOG_NOTICE,"Called from: %s:%d: No contexts_table!\n", file, line);
		usleep(500000);
	}

	t1 = ast_hashtab_start_traversal(contexts_table);
	while( (c1 = ast_hashtab_next(t1))) {
		for(c2=contexts;c2;c2=c2->next) {
			if (!strcmp(c1->name, c2->name)) {
				found = 1;
				break;
			}
		}
		if (!found) {
			ast_log(LOG_NOTICE,"Called from: %s:%d: Could not find the %s context in the linked list\n", file, line, c1->name);
			check_contexts_trouble();
		}
	}
	ast_hashtab_end_traversal(t1);
	for(c2=contexts;c2;c2=c2->next) {
		c1 = find_context_locked(c2->name);
		if (!c1) {
			ast_log(LOG_NOTICE,"Called from: %s:%d: Could not find the %s context in the hashtab\n", file, line, c2->name);
			check_contexts_trouble();
		} else
			ast_unlock_contexts();
	}

	/* loop thru all contexts, and verify the exten structure compares to the
	   hashtab structure */
	for(c2=contexts;c2;c2=c2->next) {
		c1 = find_context_locked(c2->name);
		if (c1) {
			ast_unlock_contexts();

			/* is every entry in the root list also in the root_table? */
			for(e1 = c1->root; e1; e1=e1->next)
			{
				char dummy_name[1024];
				ex.exten = dummy_name;
				ex.matchcid = e1->matchcid;
				ex.cidmatch = e1->cidmatch;
				ast_copy_string(dummy_name, e1->exten, sizeof(dummy_name));
				e2 = ast_hashtab_lookup(c1->root_table, &ex);
				if (!e2) {
					if (e1->matchcid == AST_EXT_MATCHCID_ON) {
						ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context records the exten %s (CID match: %s) but it is not in its root_table\n", file, line, c2->name, dummy_name, e1->cidmatch );
					} else {
						ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context records the exten %s but it is not in its root_table\n", file, line, c2->name, dummy_name );
					}
					check_contexts_trouble();
				}
			}

			/* is every entry in the root_table also in the root list? */
			if (!c2->root_table) {
				if (c2->root) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: No c2->root_table for context %s!\n", file, line, c2->name);
					usleep(500000);
				}
			} else {
				t1 = ast_hashtab_start_traversal(c2->root_table);
				while( (e2 = ast_hashtab_next(t1)) ) {
					for(e1=c2->root;e1;e1=e1->next) {
						if (!strcmp(e1->exten, e2->exten)) {
							found = 1;
							break;
						}
					}
					if (!found) {
						ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context records the exten %s but it is not in its root_table\n", file, line, c2->name, e2->exten);
						check_contexts_trouble();
					}

				}
				ast_hashtab_end_traversal(t1);
			}
		}
		/* is every priority reflected in the peer_table at the head of the list? */

		/* is every entry in the root list also in the root_table? */
		/* are the per-extension peer_tables in the right place? */

		for(e1 = c2->root; e1; e1 = e1->next) {

			for(e2=e1;e2;e2=e2->peer) {
				ex.priority = e2->priority;
				if (e2 != e1 && e2->peer_table) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority has a peer_table entry, and shouldn't!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}

				if (e2 != e1 && e2->peer_label_table) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority has a peer_label_table entry, and shouldn't!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}

				if (e2 == e1 && !e2->peer_table){
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority doesn't have a peer_table!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}

				if (e2 == e1 && !e2->peer_label_table) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority doesn't have a peer_label_table!\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}


				e3 = ast_hashtab_lookup(e1->peer_table, &ex);
				if (!e3) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority is not reflected in the peer_table\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}
			}

			if (!e1->peer_table){
				ast_log(LOG_NOTICE,"Called from: %s:%d: No e1->peer_table!\n", file, line);
				usleep(500000);
			}

			/* is every entry in the peer_table also in the peer list? */
			t1 = ast_hashtab_start_traversal(e1->peer_table);
			while( (e2 = ast_hashtab_next(t1)) ) {
				for(e3=e1;e3;e3=e3->peer) {
					if (e3->priority == e2->priority) {
						found = 1;
						break;
					}
				}
				if (!found) {
					ast_log(LOG_NOTICE,"Called from: %s:%d: The %s context, %s exten, %d priority is not reflected in the peer list\n", file, line, c2->name, e1->exten, e2->priority );
					check_contexts_trouble();
				}
			}
			ast_hashtab_end_traversal(t1);
		}
	}
	return 0;
}
#endif

/*
   \note This function is special. It saves the stack so that no matter
   how many times it is called, it returns to the same place */
int pbx_exec(struct ast_channel *c,	/*!< Channel */
	     struct ast_app *app,	/*!< Application */
	     const char *data)		/*!< Data for execution */
{
	int res;
	struct ast_module_user *u = NULL;
	const char *saved_c_appl;
	const char *saved_c_data;

	if (ast_channel_cdr(c) && !ast_check_hangup(c))
		ast_cdr_setapp(ast_channel_cdr(c), app->name, data);

	/* save channel values */
	saved_c_appl= ast_channel_appl(c);
	saved_c_data= ast_channel_data(c);

	ast_channel_appl_set(c, app->name);
	ast_channel_data_set(c, data);
	ast_cel_report_event(c, AST_CEL_APP_START, NULL, NULL, NULL);

	if (app->module)
		u = __ast_module_user_add(app->module, c);
	if (strcasecmp(app->name, "system") && !ast_strlen_zero(data) &&
			strchr(data, '|') && !strchr(data, ',') && !ast_opt_dont_warn) {
		ast_log(LOG_WARNING, "The application delimiter is now the comma, not "
			"the pipe.  Did you forget to convert your dialplan?  (%s(%s))\n",
			app->name, (char *) data);
	}
	res = app->execute(c, S_OR(data, ""));
	if (app->module && u)
		__ast_module_user_remove(app->module, u);
	ast_cel_report_event(c, AST_CEL_APP_END, NULL, NULL, NULL);
	/* restore channel values */
	ast_channel_appl_set(c, saved_c_appl);
	ast_channel_data_set(c, saved_c_data);
	return res;
}

/*! \brief Find application handle in linked list
 */
struct ast_app *pbx_findapp(const char *app)
{
	struct ast_app *tmp;

	AST_RWLIST_RDLOCK(&apps);
	AST_RWLIST_TRAVERSE(&apps, tmp, list) {
		if (!strcasecmp(tmp->name, app))
			break;
	}
	AST_RWLIST_UNLOCK(&apps);

	return tmp;
}

static struct ast_switch *pbx_findswitch(const char *sw)
{
	struct ast_switch *asw;

	AST_RWLIST_RDLOCK(&switches);
	AST_RWLIST_TRAVERSE(&switches, asw, list) {
		if (!strcasecmp(asw->name, sw))
			break;
	}
	AST_RWLIST_UNLOCK(&switches);

	return asw;
}

static inline int include_valid(struct ast_include *i)
{
	if (!i->hastime)
		return 1;

	return ast_check_timing(&(i->timing));
}

static void pbx_destroy(struct ast_pbx *p)
{
	ast_free(p);
}

/* form a tree that fully describes all the patterns in a context's extensions
 * in this tree, a "node" represents an individual character or character set
 * meant to match the corresponding character in a dial string. The tree
 * consists of a series of match_char structs linked in a chain
 * via the alt_char pointers. More than one pattern can share the same parts of the
 * tree as other extensions with the same pattern to that point.
 * My first attempt to duplicate the finding of the 'best' pattern was flawed in that
 * I misunderstood the general algorithm. I thought that the 'best' pattern
 * was the one with lowest total score. This was not true. Thus, if you have
 * patterns "1XXXXX" and "X11111", you would be tempted to say that "X11111" is
 * the "best" match because it has fewer X's, and is therefore more specific,
 * but this is not how the old algorithm works. It sorts matching patterns
 * in a similar collating sequence as sorting alphabetic strings, from left to
 * right. Thus, "1XXXXX" comes before "X11111", and would be the "better" match,
 * because "1" is more specific than "X".
 * So, to accomodate this philosophy, I sort the tree branches along the alt_char
 * line so they are lowest to highest in specificity numbers. This way, as soon
 * as we encounter our first complete match, we automatically have the "best"
 * match and can stop the traversal immediately. Same for CANMATCH/MATCHMORE.
 * If anyone would like to resurrect the "wrong" pattern trie searching algorithm,
 * they are welcome to revert pbx to before 1 Apr 2008.
 * As an example, consider these 4 extensions:
 * (a) NXXNXXXXXX
 * (b) 307754XXXX
 * (c) fax
 * (d) NXXXXXXXXX
 *
 * In the above, between (a) and (d), (a) is a more specific pattern than (d), and would win over
 * most numbers. For all numbers beginning with 307754, (b) should always win.
 *
 * These pattern should form a (sorted) tree that looks like this:
 *   { "3" }  --next-->  { "0" }  --next--> { "7" } --next--> { "7" } --next--> { "5" } ... blah ... --> { "X" exten_match: (b) }
 *      |
 *      |alt
 *      |
 *   { "f" }  --next-->  { "a" }  --next--> { "x"  exten_match: (c) }
 *   { "N" }  --next-->  { "X" }  --next--> { "X" } --next--> { "N" } --next--> { "X" } ... blah ... --> { "X" exten_match: (a) }
 *      |                                                        |
 *      |                                                        |alt
 *      |alt                                                     |
 *      |                                                     { "X" } --next--> { "X" } ... blah ... --> { "X" exten_match: (d) }
 *      |
 *     NULL
 *
 *   In the above, I could easily turn "N" into "23456789", but I think that a quick "if( *z >= '2' && *z <= '9' )" might take
 *   fewer CPU cycles than a call to strchr("23456789",*z), where *z is the char to match...
 *
 *   traversal is pretty simple: one routine merely traverses the alt list, and for each matching char in the pattern,  it calls itself
 *   on the corresponding next pointer, incrementing also the pointer of the string to be matched, and passing the total specificity and length.
 *   We pass a pointer to a scoreboard down through, also.
 *   The scoreboard isn't as necessary to the revised algorithm, but I kept it as a handy way to return the matched extension.
 *   The first complete match ends the traversal, which should make this version of the pattern matcher faster
 *   the previous. The same goes for "CANMATCH" or "MATCHMORE"; the first such match ends the traversal. In both
 *   these cases, the reason we can stop immediately, is because the first pattern match found will be the "best"
 *   according to the sort criteria.
 *   Hope the limit on stack depth won't be a problem... this routine should
 *   be pretty lean as far a stack usage goes. Any non-match terminates the recursion down a branch.
 *
 *   In the above example, with the number "3077549999" as the pattern, the traversor could match extensions a, b and d.  All are
 *   of length 10; they have total specificities of  24580, 10246, and 25090, respectively, not that this matters
 *   at all. (b) wins purely because the first character "3" is much more specific (lower specificity) than "N". I have
 *   left the specificity totals in the code as an artifact; at some point, I will strip it out.
 *
 *   Just how much time this algorithm might save over a plain linear traversal over all possible patterns is unknown,
 *   because it's a function of how many extensions are stored in a context. With thousands of extensions, the speedup
 *   can be very noticeable. The new matching algorithm can run several hundreds of times faster, if not a thousand or
 *   more times faster in extreme cases.
 *
 *   MatchCID patterns are also supported, and stored in the tree just as the extension pattern is. Thus, you
 *   can have patterns in your CID field as well.
 *
 * */


static void update_scoreboard(struct scoreboard *board, int length, int spec, struct ast_exten *exten, char last, const char *callerid, int deleted, struct match_char *node)
{
	/* if this extension is marked as deleted, then skip this -- if it never shows
	   on the scoreboard, it will never be found, nor will halt the traversal. */
	if (deleted)
		return;
	board->total_specificity = spec;
	board->total_length = length;
	board->exten = exten;
	board->last_char = last;
	board->node = node;
#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE,"Scoreboarding (LONGER) %s, len=%d, score=%d\n", exten->exten, length, spec);
#endif
}

#ifdef NEED_DEBUG
static void log_match_char_tree(struct match_char *node, char *prefix)
{
	char extenstr[40];
	struct ast_str *my_prefix = ast_str_alloca(1024);

	extenstr[0] = '\0';

	if (node && node->exten)
		snprintf(extenstr, sizeof(extenstr), "(%p)", node->exten);

	if (strlen(node->x) > 1) {
		ast_debug(1, "%s[%s]:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y':'N',
			node->deleted? 'D':'-', node->specificity, node->exten? "EXTEN:":"",
			node->exten ? node->exten->exten : "", extenstr);
	} else {
		ast_debug(1, "%s%s:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y':'N',
			node->deleted? 'D':'-', node->specificity, node->exten? "EXTEN:":"",
			node->exten ? node->exten->exten : "", extenstr);
	}

	ast_str_set(&my_prefix, 0, "%s+       ", prefix);

	if (node->next_char)
		log_match_char_tree(node->next_char, ast_str_buffer(my_prefix));

	if (node->alt_char)
		log_match_char_tree(node->alt_char, prefix);
}
#endif

static void cli_match_char_tree(struct match_char *node, char *prefix, int fd)
{
	char extenstr[40];
	struct ast_str *my_prefix = ast_str_alloca(1024);

	extenstr[0] = '\0';

	if (node->exten) {
		snprintf(extenstr, sizeof(extenstr), "(%p)", node->exten);
	}

	if (strlen(node->x) > 1) {
		ast_cli(fd, "%s[%s]:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y' : 'N',
			node->deleted ? 'D' : '-', node->specificity, node->exten? "EXTEN:" : "",
			node->exten ? node->exten->exten : "", extenstr);
	} else {
		ast_cli(fd, "%s%s:%c:%c:%d:%s%s%s\n", prefix, node->x, node->is_pattern ? 'Y' : 'N',
			node->deleted ? 'D' : '-', node->specificity, node->exten? "EXTEN:" : "",
			node->exten ? node->exten->exten : "", extenstr);
	}

	ast_str_set(&my_prefix, 0, "%s+       ", prefix);

	if (node->next_char)
		cli_match_char_tree(node->next_char, ast_str_buffer(my_prefix), fd);

	if (node->alt_char)
		cli_match_char_tree(node->alt_char, prefix, fd);
}

static struct ast_exten *get_canmatch_exten(struct match_char *node)
{
	/* find the exten at the end of the rope */
	struct match_char *node2 = node;

	for (node2 = node; node2; node2 = node2->next_char) {
		if (node2->exten) {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"CanMatch_exten returns exten %s(%p)\n", node2->exten->exten, node2->exten);
#endif
			return node2->exten;
		}
	}
#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE,"CanMatch_exten returns NULL, match_char=%s\n", node->x);
#endif
	return 0;
}

static struct ast_exten *trie_find_next_match(struct match_char *node)
{
	struct match_char *m3;
	struct match_char *m4;
	struct ast_exten *e3;

	if (node && node->x[0] == '.' && !node->x[1]) { /* dot and ! will ALWAYS be next match in a matchmore */
		return node->exten;
	}

	if (node && node->x[0] == '!' && !node->x[1]) {
		return node->exten;
	}

	if (!node || !node->next_char) {
		return NULL;
	}

	m3 = node->next_char;

	if (m3->exten) {
		return m3->exten;
	}
	for (m4 = m3->alt_char; m4; m4 = m4->alt_char) {
		if (m4->exten) {
			return m4->exten;
		}
	}
	for (m4 = m3; m4; m4 = m4->alt_char) {
		e3 = trie_find_next_match(m3);
		if (e3) {
			return e3;
		}
	}

	return NULL;
}

#ifdef DEBUG_THIS
static char *action2str(enum ext_match_t action)
{
	switch (action) {
	case E_MATCH:
		return "MATCH";
	case E_CANMATCH:
		return "CANMATCH";
	case E_MATCHMORE:
		return "MATCHMORE";
	case E_FINDLABEL:
		return "FINDLABEL";
	case E_SPAWN:
		return "SPAWN";
	default:
		return "?ACTION?";
	}
}

#endif

static void new_find_extension(const char *str, struct scoreboard *score, struct match_char *tree, int length, int spec, const char *callerid, const char *label, enum ext_match_t action)
{
	struct match_char *p; /* note minimal stack storage requirements */
	struct ast_exten pattern = { .label = label };
#ifdef DEBUG_THIS
	if (tree)
		ast_log(LOG_NOTICE,"new_find_extension called with %s on (sub)tree %s action=%s\n", str, tree->x, action2str(action));
	else
		ast_log(LOG_NOTICE,"new_find_extension called with %s on (sub)tree NULL action=%s\n", str, action2str(action));
#endif
	for (p = tree; p; p = p->alt_char) {
		if (p->is_pattern) {
			if (p->x[0] == 'N') {
				if (p->x[1] == 0 && *str >= '2' && *str <= '9' ) {
#define	NEW_MATCHER_CHK_MATCH	       \
					if (p->exten && !(*(str + 1))) { /* if a shorter pattern matches along the way, might as well report it */             \
						if (action == E_MATCH || action == E_SPAWN || action == E_FINDLABEL) { /* if in CANMATCH/MATCHMORE, don't let matches get in the way */   \
							update_scoreboard(score, length + 1, spec + p->specificity, p->exten, 0, callerid, p->deleted, p);                 \
							if (!p->deleted) {                                                                                           \
								if (action == E_FINDLABEL) {                                                                             \
									if (ast_hashtab_lookup(score->exten->peer_label_table, &pattern)) {                                  \
										ast_debug(4, "Found label in preferred extension\n");                                            \
										return;                                                                                          \
									}                                                                                                    \
								} else {                                                                                                 \
									ast_debug(4, "returning an exact match-- first found-- %s\n", p->exten->exten);                       \
									return; /* the first match, by definition, will be the best, because of the sorted tree */           \
								}                                                                                                        \
							}                                                                                                            \
						}                                                                                                                \
					}

#define	NEW_MATCHER_RECURSE	           \
					if (p->next_char && (*(str + 1) || (p->next_char->x[0] == '/' && p->next_char->x[1] == 0)                 \
		                                       || p->next_char->x[0] == '!')) {                                          \
						if (*(str + 1) || p->next_char->x[0] == '!') {                                                         \
							new_find_extension(str + 1, score, p->next_char, length + 1, spec + p->specificity, callerid, label, action); \
							if (score->exten)  {                                                                             \
						        ast_debug(4 ,"returning an exact match-- %s\n", score->exten->exten);                         \
								return; /* the first match is all we need */                                                 \
							}												                                                 \
						} else {                                                                                             \
							new_find_extension("/", score, p->next_char, length + 1, spec + p->specificity, callerid, label, action);	 \
							if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {      \
						        ast_debug(4,"returning a (can/more) match--- %s\n", score->exten ? score->exten->exten :     \
		                               "NULL");                                                                        \
								return; /* the first match is all we need */                                                 \
							}												                                                 \
						}                                                                                                    \
					} else if ((p->next_char || action == E_CANMATCH) && !*(str + 1)) {                                                                  \
						score->canmatch = 1;                                                                                 \
						score->canmatch_exten = get_canmatch_exten(p);                                                       \
						if (action == E_CANMATCH || action == E_MATCHMORE) {                                                 \
					        ast_debug(4, "returning a canmatch/matchmore--- str=%s\n", str);                                  \
							return;                                                                                          \
						}												                                                     \
					}

					NEW_MATCHER_CHK_MATCH;
					NEW_MATCHER_RECURSE;
				}
			} else if (p->x[0] == 'Z') {
				if (p->x[1] == 0 && *str >= '1' && *str <= '9' ) {
					NEW_MATCHER_CHK_MATCH;
					NEW_MATCHER_RECURSE;
				}
			} else if (p->x[0] == 'X') {
				if (p->x[1] == 0 && *str >= '0' && *str <= '9' ) {
					NEW_MATCHER_CHK_MATCH;
					NEW_MATCHER_RECURSE;
				}
			} else if (p->x[0] == '.' && p->x[1] == 0) {
				/* how many chars will the . match against? */
				int i = 0;
				const char *str2 = str;
				while (*str2 && *str2 != '/') {
					str2++;
					i++;
				}
				if (p->exten && *str2 != '/') {
					update_scoreboard(score, length + i, spec + (i * p->specificity), p->exten, '.', callerid, p->deleted, p);
					if (score->exten) {
						ast_debug(4,"return because scoreboard has a match with '/'--- %s\n", score->exten->exten);
						return; /* the first match is all we need */
					}
				}
				if (p->next_char && p->next_char->x[0] == '/' && p->next_char->x[1] == 0) {
					new_find_extension("/", score, p->next_char, length + i, spec+(p->specificity*i), callerid, label, action);
					if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {
						ast_debug(4, "return because scoreboard has exact match OR CANMATCH/MATCHMORE & canmatch set--- %s\n", score->exten ? score->exten->exten : "NULL");
						return; /* the first match is all we need */
					}
				}
			} else if (p->x[0] == '!' && p->x[1] == 0) {
				/* how many chars will the . match against? */
				int i = 1;
				const char *str2 = str;
				while (*str2 && *str2 != '/') {
					str2++;
					i++;
				}
				if (p->exten && *str2 != '/') {
					update_scoreboard(score, length + 1, spec + (p->specificity * i), p->exten, '!', callerid, p->deleted, p);
					if (score->exten) {
						ast_debug(4, "return because scoreboard has a '!' match--- %s\n", score->exten->exten);
						return; /* the first match is all we need */
					}
				}
				if (p->next_char && p->next_char->x[0] == '/' && p->next_char->x[1] == 0) {
					new_find_extension("/", score, p->next_char, length + i, spec + (p->specificity * i), callerid, label, action);
					if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {
						ast_debug(4, "return because scoreboard has exact match OR CANMATCH/MATCHMORE & canmatch set with '/' and '!'--- %s\n", score->exten ? score->exten->exten : "NULL");
						return; /* the first match is all we need */
					}
				}
			} else if (p->x[0] == '/' && p->x[1] == 0) {
				/* the pattern in the tree includes the cid match! */
				if (p->next_char && callerid && *callerid) {
					new_find_extension(callerid, score, p->next_char, length + 1, spec, callerid, label, action);
					if (score->exten || ((action == E_CANMATCH || action == E_MATCHMORE) && score->canmatch)) {
						ast_debug(4, "return because scoreboard has exact match OR CANMATCH/MATCHMORE & canmatch set with '/'--- %s\n", score->exten ? score->exten->exten : "NULL");
						return; /* the first match is all we need */
					}
				}
			} else if (strchr(p->x, *str)) {
				ast_debug(4, "Nothing strange about this match\n");
				NEW_MATCHER_CHK_MATCH;
				NEW_MATCHER_RECURSE;
			}
		} else if (strchr(p->x, *str)) {
			ast_debug(4, "Nothing strange about this match\n");
			NEW_MATCHER_CHK_MATCH;
			NEW_MATCHER_RECURSE;
		}
	}
	ast_debug(4, "return at end of func\n");
}

/* the algorithm for forming the extension pattern tree is also a bit simple; you
 * traverse all the extensions in a context, and for each char of the extension,
 * you see if it exists in the tree; if it doesn't, you add it at the appropriate
 * spot. What more can I say? At the end of each exten, you cap it off by adding the
 * address of the extension involved. Duplicate patterns will be complained about.
 *
 * Ideally, this would be done for each context after it is created and fully
 * filled. It could be done as a finishing step after extensions.conf or .ael is
 * loaded, or it could be done when the first search is encountered. It should only
 * have to be done once, until the next unload or reload.
 *
 * I guess forming this pattern tree would be analogous to compiling a regex. Except
 * that a regex only handles 1 pattern, really. This trie holds any number
 * of patterns. Well, really, it **could** be considered a single pattern,
 * where the "|" (or) operator is allowed, I guess, in a way, sort of...
 */

static struct match_char *already_in_tree(struct match_char *current, char *pat, int is_pattern)
{
	struct match_char *t;

	if (!current) {
		return 0;
	}

	for (t = current; t; t = t->alt_char) {
		if (is_pattern == t->is_pattern && !strcmp(pat, t->x)) {/* uh, we may want to sort exploded [] contents to make matching easy */
			return t;
		}
	}

	return 0;
}

/* The first arg is the location of the tree ptr, or the
   address of the next_char ptr in the node, so we can mess
   with it, if we need to insert at the beginning of the list */

static void insert_in_next_chars_alt_char_list(struct match_char **parent_ptr, struct match_char *node)
{
	struct match_char *curr, *lcurr;

	/* insert node into the tree at "current", so the alt_char list from current is
	   sorted in increasing value as you go to the leaves */
	if (!(*parent_ptr)) {
		*parent_ptr = node;
		return;
	}

	if ((*parent_ptr)->specificity > node->specificity) {
		/* insert at head */
		node->alt_char = (*parent_ptr);
		*parent_ptr = node;
		return;
	}

	lcurr = *parent_ptr;
	for (curr = (*parent_ptr)->alt_char; curr; curr = curr->alt_char) {
		if (curr->specificity > node->specificity) {
			node->alt_char = curr;
			lcurr->alt_char = node;
			break;
		}
		lcurr = curr;
	}

	if (!curr) {
		lcurr->alt_char = node;
	}

}

struct pattern_node {
	/*! Pattern node specificity */
	int specif;
	/*! Pattern node match characters. */
	char buf[256];
};

static struct match_char *add_pattern_node(struct ast_context *con, struct match_char *current, const struct pattern_node *pattern, int is_pattern, int already, struct match_char **nextcharptr)
{
	struct match_char *m;

	if (!(m = ast_calloc(1, sizeof(*m) + strlen(pattern->buf)))) {
		return NULL;
	}

	/* strcpy is safe here since we know its size and have allocated
	 * just enough space for when we allocated m
	 */
	strcpy(m->x, pattern->buf);

	/* the specificity scores are the same as used in the old
	   pattern matcher. */
	m->is_pattern = is_pattern;
	if (pattern->specif == 1 && is_pattern && pattern->buf[0] == 'N') {
		m->specificity = 0x0832;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == 'Z') {
		m->specificity = 0x0931;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == 'X') {
		m->specificity = 0x0a30;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == '.') {
		m->specificity = 0x18000;
	} else if (pattern->specif == 1 && is_pattern && pattern->buf[0] == '!') {
		m->specificity = 0x28000;
	} else {
		m->specificity = pattern->specif;
	}

	if (!con->pattern_tree) {
		insert_in_next_chars_alt_char_list(&con->pattern_tree, m);
	} else {
		if (already) { /* switch to the new regime (traversing vs appending)*/
			insert_in_next_chars_alt_char_list(nextcharptr, m);
		} else {
			insert_in_next_chars_alt_char_list(&current->next_char, m);
		}
	}

	return m;
}

/*!
 * \internal
 * \brief Extract the next exten pattern node.
 *
 * \param node Pattern node to fill.
 * \param src Next source character to read.
 * \param pattern TRUE if the exten is a pattern.
 * \param extenbuf Original exten buffer to use in diagnostic messages.
 *
 * \retval Ptr to next extenbuf pos to read.
 */
static const char *get_pattern_node(struct pattern_node *node, const char *src, int pattern, const char *extenbuf)
{
#define INC_DST_OVERFLOW_CHECK							\
	do {												\
		if (dst - node->buf < sizeof(node->buf) - 1) {	\
			++dst;										\
		} else {										\
			overflow = 1;								\
		}												\
	} while (0)

	node->specif = 0;
	node->buf[0] = '\0';
	while (*src) {
		if (*src == '[' && pattern) {
			char *dst = node->buf;
			const char *src_next;
			int length;
			int overflow = 0;

			/* get past the '[' */
			++src;
			for (;;) {
				if (*src == '\\') {
					/* Escaped character. */
					++src;
					if (*src == '[' || *src == '\\' || *src == '-' || *src == ']') {
						*dst = *src++;
						INC_DST_OVERFLOW_CHECK;
					}
				} else if (*src == '-') {
					unsigned char first;
					unsigned char last;

					src_next = src;
					first = *(src_next - 1);
					last = *++src_next;

					if (last == '\\') {
						/* Escaped character. */
						last = *++src_next;
					}

					/* Possible char range. */
					if (node->buf[0] && last) {
						/* Expand the char range. */
						while (++first <= last) {
							*dst = first;
							INC_DST_OVERFLOW_CHECK;
						}
						src = src_next + 1;
					} else {
						/*
						 * There was no left or right char for the range.
						 * It is just a '-'.
						 */
						*dst = *src++;
						INC_DST_OVERFLOW_CHECK;
					}
				} else if (*src == '\0') {
					ast_log(LOG_WARNING,
						"A matching ']' was not found for '[' in exten pattern '%s'\n",
						extenbuf);
					break;
				} else if (*src == ']') {
					++src;
					break;
				} else {
					*dst = *src++;
					INC_DST_OVERFLOW_CHECK;
				}
			}
			/* null terminate the exploded range */
			*dst = '\0';

			if (overflow) {
				ast_log(LOG_ERROR,
					"Expanded character set too large to deal with in exten pattern '%s'. Ignoring character set.\n",
					extenbuf);
				node->buf[0] = '\0';
				continue;
			}

			/* Sort the characters in character set. */
			length = strlen(node->buf);
			if (!length) {
				ast_log(LOG_WARNING, "Empty character set in exten pattern '%s'. Ignoring.\n",
					extenbuf);
				node->buf[0] = '\0';
				continue;
			}
			qsort(node->buf, length, 1, compare_char);

			/* Remove duplicate characters from character set. */
			dst = node->buf;
			src_next = node->buf;
			while (*src_next++) {
				if (*dst != *src_next) {
					*++dst = *src_next;
				}
			}

			length = strlen(node->buf);
			length <<= 8;
			node->specif = length | (unsigned char) node->buf[0];
			break;
		} else if (*src == '-') {
			/* Skip dashes in all extensions. */
			++src;
		} else {
			if (*src == '\\') {
				/*
				 * XXX The escape character here does not remove any special
				 * meaning to characters except the '[', '\\', and '-'
				 * characters since they are special only in this function.
				 */
				node->buf[0] = *++src;
				if (!node->buf[0]) {
					break;
				}
			} else {
				node->buf[0] = *src;
				if (pattern) {
					/* make sure n,x,z patterns are canonicalized to N,X,Z */
					if (node->buf[0] == 'n') {
						node->buf[0] = 'N';
					} else if (node->buf[0] == 'x') {
						node->buf[0] = 'X';
					} else if (node->buf[0] == 'z') {
						node->buf[0] = 'Z';
					}
				}
			}
			node->buf[1] = '\0';
			node->specif = 1;
			++src;
			break;
		}
	}
	return src;

#undef INC_DST_OVERFLOW_CHECK
}

static struct match_char *add_exten_to_pattern_tree(struct ast_context *con, struct ast_exten *e1, int findonly)
{
	struct match_char *m1 = NULL;
	struct match_char *m2 = NULL;
	struct match_char **m0;
	const char *pos;
	int already;
	int pattern = 0;
	int idx_cur;
	int idx_next;
	char extenbuf[512];
	struct pattern_node pat_node[2];

	if (e1->matchcid) {
		if (sizeof(extenbuf) < strlen(e1->exten) + strlen(e1->cidmatch) + 2) {
			ast_log(LOG_ERROR,
				"The pattern %s/%s is too big to deal with: it will be ignored! Disaster!\n",
				e1->exten, e1->cidmatch);
			return NULL;
		}
		sprintf(extenbuf, "%s/%s", e1->exten, e1->cidmatch);/* Safe.  We just checked. */
	} else {
		ast_copy_string(extenbuf, e1->exten, sizeof(extenbuf));
	}

#ifdef NEED_DEBUG
	ast_debug(1, "Adding exten %s to tree\n", extenbuf);
#endif
	m1 = con->pattern_tree; /* each pattern starts over at the root of the pattern tree */
	m0 = &con->pattern_tree;
	already = 1;

	pos = extenbuf;
	if (*pos == '_') {
		pattern = 1;
		++pos;
	}
	idx_cur = 0;
	pos = get_pattern_node(&pat_node[idx_cur], pos, pattern, extenbuf);
	for (; pat_node[idx_cur].buf[0]; idx_cur = idx_next) {
		idx_next = (idx_cur + 1) % ARRAY_LEN(pat_node);
		pos = get_pattern_node(&pat_node[idx_next], pos, pattern, extenbuf);

		/* See about adding node to tree. */
		m2 = NULL;
		if (already && (m2 = already_in_tree(m1, pat_node[idx_cur].buf, pattern))
			&& m2->next_char) {
			if (!pat_node[idx_next].buf[0]) {
				/*
				 * This is the end of the pattern, but not the end of the tree.
				 * Mark this node with the exten... a shorter pattern might win
				 * if the longer one doesn't match.
				 */
				if (findonly) {
					return m2;
				}
				if (m2->exten) {
					ast_log(LOG_WARNING, "Found duplicate exten. Had %s found %s\n",
						m2->deleted ? "(deleted/invalid)" : m2->exten->exten, e1->exten);
				}
				m2->exten = e1;
				m2->deleted = 0;
			}
			m1 = m2->next_char; /* m1 points to the node to compare against */
			m0 = &m2->next_char; /* m0 points to the ptr that points to m1 */
		} else { /* not already OR not m2 OR nor m2->next_char */
			if (m2) {
				if (findonly) {
					return m2;
				}
				m1 = m2; /* while m0 stays the same */
			} else {
				if (findonly) {
					return m1;
				}
				m1 = add_pattern_node(con, m1, &pat_node[idx_cur], pattern, already, m0);
				if (!m1) { /* m1 is the node just added */
					return NULL;
				}
				m0 = &m1->next_char;
			}
			if (!pat_node[idx_next].buf[0]) {
				if (m2 && m2->exten) {
					ast_log(LOG_WARNING, "Found duplicate exten. Had %s found %s\n",
						m2->deleted ? "(deleted/invalid)" : m2->exten->exten, e1->exten);
				}
				m1->deleted = 0;
				m1->exten = e1;
			}

			/* The 'already' variable is a mini-optimization designed to make it so that we
			 * don't have to call already_in_tree when we know it will return false.
			 */
			already = 0;
		}
	}
	return m1;
}

static void create_match_char_tree(struct ast_context *con)
{
	struct ast_hashtab_iter *t1;
	struct ast_exten *e1;
#ifdef NEED_DEBUG
	int biggest_bucket, resizes, numobjs, numbucks;

	ast_debug(1, "Creating Extension Trie for context %s(%p)\n", con->name, con);
	ast_hashtab_get_stats(con->root_table, &biggest_bucket, &resizes, &numobjs, &numbucks);
	ast_debug(1, "This tree has %d objects in %d bucket lists, longest list=%d objects, and has resized %d times\n",
			numobjs, numbucks, biggest_bucket, resizes);
#endif
	t1 = ast_hashtab_start_traversal(con->root_table);
	while ((e1 = ast_hashtab_next(t1))) {
		if (e1->exten) {
			add_exten_to_pattern_tree(con, e1, 0);
		} else {
			ast_log(LOG_ERROR, "Attempt to create extension with no extension name.\n");
		}
	}
	ast_hashtab_end_traversal(t1);
}

static void destroy_pattern_tree(struct match_char *pattern_tree) /* pattern tree is a simple binary tree, sort of, so the proper way to destroy it is... recursively! */
{
	/* destroy all the alternates */
	if (pattern_tree->alt_char) {
		destroy_pattern_tree(pattern_tree->alt_char);
		pattern_tree->alt_char = 0;
	}
	/* destroy all the nexts */
	if (pattern_tree->next_char) {
		destroy_pattern_tree(pattern_tree->next_char);
		pattern_tree->next_char = 0;
	}
	pattern_tree->exten = 0; /* never hurts to make sure there's no pointers laying around */
	ast_free(pattern_tree);
}

/*!
 * \internal
 * \brief Get the length of the exten string.
 *
 * \param str Exten to get length.
 *
 * \retval strlen of exten.
 */
static int ext_cmp_exten_strlen(const char *str)
{
	int len;

	len = 0;
	for (;;) {
		/* Ignore '-' chars as eye candy fluff. */
		while (*str == '-') {
			++str;
		}
		if (!*str) {
			break;
		}
		++str;
		++len;
	}
	return len;
}

/*!
 * \internal
 * \brief Partial comparison of non-pattern extens.
 *
 * \param left Exten to compare.
 * \param right Exten to compare.  Also matches if this string ends first.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp_exten_partial(const char *left, const char *right)
{
	int cmp;

	for (;;) {
		/* Ignore '-' chars as eye candy fluff. */
		while (*left == '-') {
			++left;
		}
		while (*right == '-') {
			++right;
		}

		if (!*right) {
			/*
			 * Right ended first for partial match or both ended at the same
			 * time for a match.
			 */
			cmp = 0;
			break;
		}

		cmp = *left - *right;
		if (cmp) {
			break;
		}
		++left;
		++right;
	}
	return cmp;
}

/*!
 * \internal
 * \brief Comparison of non-pattern extens.
 *
 * \param left Exten to compare.
 * \param right Exten to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp_exten(const char *left, const char *right)
{
	int cmp;

	for (;;) {
		/* Ignore '-' chars as eye candy fluff. */
		while (*left == '-') {
			++left;
		}
		while (*right == '-') {
			++right;
		}

		cmp = *left - *right;
		if (cmp) {
			break;
		}
		if (!*left) {
			/*
			 * Get here only if both strings ended at the same time.  cmp
			 * would be non-zero if only one string ended.
			 */
			break;
		}
		++left;
		++right;
	}
	return cmp;
}

/*
 * Special characters used in patterns:
 *	'_'	underscore is the leading character of a pattern.
 *		In other position it is treated as a regular char.
 *	'-' The '-' is a separator and ignored.  Why?  So patterns like NXX-XXX-XXXX work.
 *	.	one or more of any character. Only allowed at the end of
 *		a pattern.
 *	!	zero or more of anything. Also impacts the result of CANMATCH
 *		and MATCHMORE. Only allowed at the end of a pattern.
 *		In the core routine, ! causes a match with a return code of 2.
 *		In turn, depending on the search mode: (XXX check if it is implemented)
 *		- E_MATCH retuns 1 (does match)
 *		- E_MATCHMORE returns 0 (no match)
 *		- E_CANMATCH returns 1 (does match)
 *
 *	/	should not appear as it is considered the separator of the CID info.
 *		XXX at the moment we may stop on this char.
 *
 *	X Z N	match ranges 0-9, 1-9, 2-9 respectively.
 *	[	denotes the start of a set of character. Everything inside
 *		is considered literally. We can have ranges a-d and individual
 *		characters. A '[' and '-' can be considered literally if they
 *		are just before ']'.
 *		XXX currently there is no way to specify ']' in a range, nor \ is
 *		considered specially.
 *
 * When we compare a pattern with a specific extension, all characters in the extension
 * itself are considered literally.
 * XXX do we want to consider space as a separator as well ?
 * XXX do we want to consider the separators in non-patterns as well ?
 */

/*!
 * \brief helper functions to sort extension patterns in the desired way,
 * so that more specific patterns appear first.
 *
 * \details
 * The function compares individual characters (or sets of), returning
 * an int where bits 0-7 are the ASCII code of the first char in the set,
 * bits 8-15 are the number of characters in the set, and bits 16-20 are
 * for special cases.
 * This way more specific patterns (smaller character sets) appear first.
 * Wildcards have a special value, so that we can directly compare them to
 * sets by subtracting the two values. In particular:
 *  0x001xx     one character, character set starting with xx
 *  0x0yyxx     yy characters, character set starting with xx
 *  0x18000     '.' (one or more of anything)
 *  0x28000     '!' (zero or more of anything)
 *  0x30000     NUL (end of string)
 *  0x40000     error in set.
 * The pointer to the string is advanced according to needs.
 * NOTES:
 *  1. the empty set is ignored.
 *  2. given that a full set has always 0 as the first element,
 *     we could encode the special cases as 0xffXX where XX
 *     is 1, 2, 3, 4 as used above.
 */
static int ext_cmp_pattern_pos(const char **p, unsigned char *bitwise)
{
#define BITS_PER	8	/* Number of bits per unit (byte). */
	unsigned char c;
	unsigned char cmin;
	int count;
	const char *end;

	do {
		/* Get character and advance. (Ignore '-' chars as eye candy fluff.) */
		do {
			c = *(*p)++;
		} while (c == '-');

		/* always return unless we have a set of chars */
		switch (c) {
		default:
			/* ordinary character */
			bitwise[c / BITS_PER] = 1 << ((BITS_PER - 1) - (c % BITS_PER));
			return 0x0100 | c;

		case 'n':
		case 'N':
			/* 2..9 */
			bitwise[6] = 0x3f;
			bitwise[7] = 0xc0;
			return 0x0800 | '2';

		case 'x':
		case 'X':
			/* 0..9 */
			bitwise[6] = 0xff;
			bitwise[7] = 0xc0;
			return 0x0A00 | '0';

		case 'z':
		case 'Z':
			/* 1..9 */
			bitwise[6] = 0x7f;
			bitwise[7] = 0xc0;
			return 0x0900 | '1';

		case '.':
			/* wildcard */
			return 0x18000;

		case '!':
			/* earlymatch */
			return 0x28000;	/* less specific than '.' */

		case '\0':
			/* empty string */
			*p = NULL;
			return 0x30000;

		case '[':
			/* char set */
			break;
		}
		/* locate end of set */
		end = strchr(*p, ']');

		if (!end) {
			ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
			return 0x40000;	/* XXX make this entry go last... */
		}

		count = 0;
		cmin = 0xFF;
		for (; *p < end; ++*p) {
			unsigned char c1;	/* first char in range */
			unsigned char c2;	/* last char in range */

			c1 = (*p)[0];
			if (*p + 2 < end && (*p)[1] == '-') { /* this is a range */
				c2 = (*p)[2];
				*p += 2;    /* skip a total of 3 chars */
			} else {        /* individual character */
				c2 = c1;
			}
			if (c1 < cmin) {
				cmin = c1;
			}
			for (; c1 <= c2; ++c1) {
				unsigned char mask = 1 << ((BITS_PER - 1) - (c1 % BITS_PER));

				/*
				 * Note: If two character sets score the same, the one with the
				 * lowest ASCII values will compare as coming first.  Must fill
				 * in most significant bits for lower ASCII values to accomplish
				 * the desired sort order.
				 */
				if (!(bitwise[c1 / BITS_PER] & mask)) {
					/* Add the character to the set. */
					bitwise[c1 / BITS_PER] |= mask;
					count += 0x100;
				}
			}
		}
		++*p;
	} while (!count);/* While the char set was empty. */
	return count | cmin;
}

/*!
 * \internal
 * \brief Comparison of exten patterns.
 *
 * \param left Pattern to compare.
 * \param right Pattern to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp_pattern(const char *left, const char *right)
{
	int cmp;
	int left_pos;
	int right_pos;

	for (;;) {
		unsigned char left_bitwise[32] = { 0, };
		unsigned char right_bitwise[32] = { 0, };

		left_pos = ext_cmp_pattern_pos(&left, left_bitwise);
		right_pos = ext_cmp_pattern_pos(&right, right_bitwise);
		cmp = left_pos - right_pos;
		if (!cmp) {
			/*
			 * Are the character sets different, even though they score the same?
			 *
			 * Note: Must swap left and right to get the sense of the
			 * comparison correct.  Otherwise, we would need to multiply by
			 * -1 instead.
			 */
			cmp = memcmp(right_bitwise, left_bitwise, ARRAY_LEN(left_bitwise));
		}
		if (cmp) {
			break;
		}
		if (!left) {
			/*
			 * Get here only if both patterns ended at the same time.  cmp
			 * would be non-zero if only one pattern ended.
			 */
			break;
		}
	}
	return cmp;
}

/*!
 * \internal
 * \brief Comparison of dialplan extens for sorting purposes.
 *
 * \param left Exten/pattern to compare.
 * \param right Exten/pattern to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int ext_cmp(const char *left, const char *right)
{
	/* Make sure non-pattern extens come first. */
	if (left[0] != '_') {
		if (right[0] == '_') {
			return -1;
		}
		/* Compare two non-pattern extens. */
		return ext_cmp_exten(left, right);
	}
	if (right[0] != '_') {
		return 1;
	}

	/*
	 * OK, we need full pattern sorting routine.
	 *
	 * Skip past the underscores
	 */
	return ext_cmp_pattern(left + 1, right + 1);
}

int ast_extension_cmp(const char *a, const char *b)
{
	int cmp;

	cmp = ext_cmp(a, b);
	if (cmp < 0) {
		return -1;
	}
	if (cmp > 0) {
		return 1;
	}
	return 0;
}

/*!
 * \internal
 * \brief used ast_extension_{match|close}
 * mode is as follows:
 *	E_MATCH		success only on exact match
 *	E_MATCHMORE	success only on partial match (i.e. leftover digits in pattern)
 *	E_CANMATCH	either of the above.
 * \retval 0 on no-match
 * \retval 1 on match
 * \retval 2 on early match.
 */

static int _extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	mode &= E_MATCH_MASK;	/* only consider the relevant bits */

#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE,"match core: pat: '%s', dat: '%s', mode=%d\n", pattern, data, (int)mode);
#endif

	if (pattern[0] != '_') { /* not a pattern, try exact or partial match */
		int lp = ext_cmp_exten_strlen(pattern);
		int ld = ext_cmp_exten_strlen(data);

		if (lp < ld) {		/* pattern too short, cannot match */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (0) - pattern too short, cannot match\n");
#endif
			return 0;
		}
		/* depending on the mode, accept full or partial match or both */
		if (mode == E_MATCH) {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (!ext_cmp_exten(%s,%s) when mode== E_MATCH)\n", pattern, data);
#endif
			return !ext_cmp_exten(pattern, data); /* 1 on match, 0 on fail */
		}
		if (ld == 0 || !ext_cmp_exten_partial(pattern, data)) { /* partial or full match */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (mode(%d) == E_MATCHMORE ? lp(%d) > ld(%d) : 1)\n", mode, lp, ld);
#endif
			return (mode == E_MATCHMORE) ? lp > ld : 1; /* XXX should consider '!' and '/' ? */
		} else {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (0) when ld(%d) > 0 && pattern(%s) != data(%s)\n", ld, pattern, data);
#endif
			return 0;
		}
	}
	if (mode == E_MATCH && data[0] == '_') {
		/*
		 * XXX It is bad design that we don't know if we should be
		 * comparing data and pattern as patterns or comparing data if
		 * it conforms to pattern when the function is called.  First,
		 * assume they are both patterns.  If they don't match then try
		 * to see if data conforms to the given pattern.
		 *
		 * note: if this test is left out, then _x. will not match _x. !!!
		 */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "Comparing as patterns first. pattern:%s data:%s\n", pattern, data);
#endif
		if (!ext_cmp_pattern(pattern + 1, data + 1)) {
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"return (1) - pattern matches pattern\n");
#endif
			return 1;
		}
	}

	++pattern; /* skip leading _ */
	/*
	 * XXX below we stop at '/' which is a separator for the CID info. However we should
	 * not store '/' in the pattern at all. When we insure it, we can remove the checks.
	 */
	for (;;) {
		const char *end;

		/* Ignore '-' chars as eye candy fluff. */
		while (*data == '-') {
			++data;
		}
		while (*pattern == '-') {
			++pattern;
		}
		if (!*data || !*pattern || *pattern == '/') {
			break;
		}

		switch (*pattern) {
		case '[':	/* a range */
			++pattern;
			end = strchr(pattern, ']'); /* XXX should deal with escapes ? */
			if (!end) {
				ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
				return 0;	/* unconditional failure */
			}
			if (pattern == end) {
				/* Ignore empty character sets. */
				++pattern;
				continue;
			}
			for (; pattern < end; ++pattern) {
				if (pattern+2 < end && pattern[1] == '-') { /* this is a range */
					if (*data >= pattern[0] && *data <= pattern[2])
						break;	/* match found */
					else {
						pattern += 2; /* skip a total of 3 chars */
						continue;
					}
				} else if (*data == pattern[0])
					break;	/* match found */
			}
			if (pattern >= end) {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) when pattern>=end\n");
#endif
				return 0;
			}
			pattern = end;	/* skip and continue */
			break;
		case 'n':
		case 'N':
			if (*data < '2' || *data > '9') {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) N is not matched\n");
#endif
				return 0;
			}
			break;
		case 'x':
		case 'X':
			if (*data < '0' || *data > '9') {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) X is not matched\n");
#endif
				return 0;
			}
			break;
		case 'z':
		case 'Z':
			if (*data < '1' || *data > '9') {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"return (0) Z is not matched\n");
#endif
				return 0;
			}
			break;
		case '.':	/* Must match, even with more digits */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE, "return (1) when '.' is matched\n");
#endif
			return 1;
		case '!':	/* Early match */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE, "return (2) when '!' is matched\n");
#endif
			return 2;
		default:
			if (*data != *pattern) {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE, "return (0) when *data(%c) != *pattern(%c)\n", *data, *pattern);
#endif
				return 0;
			}
			break;
		}
		++data;
		++pattern;
	}
	if (*data)			/* data longer than pattern, no match */ {
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "return (0) when data longer than pattern\n");
#endif
		return 0;
	}

	/*
	 * match so far, but ran off the end of data.
	 * Depending on what is next, determine match or not.
	 */
	if (*pattern == '\0' || *pattern == '/') {	/* exact match */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "at end, return (%d) in 'exact match'\n", (mode==E_MATCHMORE) ? 0 : 1);
#endif
		return (mode == E_MATCHMORE) ? 0 : 1;	/* this is a failure for E_MATCHMORE */
	} else if (*pattern == '!')	{		/* early match */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "at end, return (2) when '!' is matched\n");
#endif
		return 2;
	} else {						/* partial match */
#ifdef NEED_DEBUG_HERE
		ast_log(LOG_NOTICE, "at end, return (%d) which deps on E_MATCH\n", (mode == E_MATCH) ? 0 : 1);
#endif
		return (mode == E_MATCH) ? 0 : 1;	/* this is a failure for E_MATCH */
	}
}

/*
 * Wrapper around _extension_match_core() to do performance measurement
 * using the profiling code.
 */
static int extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	int i;
	static int prof_id = -2;	/* marker for 'unallocated' id */
	if (prof_id == -2) {
		prof_id = ast_add_profile("ext_match", 0);
	}
	ast_mark(prof_id, 1);
	i = _extension_match_core(ast_strlen_zero(pattern) ? "" : pattern, ast_strlen_zero(data) ? "" : data, mode);
	ast_mark(prof_id, 0);
	return i;
}

int ast_extension_match(const char *pattern, const char *data)
{
	return extension_match_core(pattern, data, E_MATCH);
}

int ast_extension_close(const char *pattern, const char *data, int needmore)
{
	if (needmore != E_MATCHMORE && needmore != E_CANMATCH)
		ast_log(LOG_WARNING, "invalid argument %d\n", needmore);
	return extension_match_core(pattern, data, needmore);
}

struct fake_context /* this struct is purely for matching in the hashtab */
{
	ast_rwlock_t lock;
	struct ast_exten *root;
	struct ast_hashtab *root_table;
	struct match_char *pattern_tree;
	struct ast_context *next;
	struct ast_include *includes;
	struct ast_ignorepat *ignorepats;
	const char *registrar;
	int refcount;
	AST_LIST_HEAD_NOLOCK(, ast_sw) alts;
	ast_mutex_t macrolock;
	char name[256];
};

struct ast_context *ast_context_find(const char *name)
{
	struct ast_context *tmp;
	struct fake_context item;

	if (!name) {
		return NULL;
	}
	ast_rdlock_contexts();
	if (contexts_table) {
		ast_copy_string(item.name, name, sizeof(item.name));
		tmp = ast_hashtab_lookup(contexts_table, &item);
	} else {
		tmp = NULL;
		while ((tmp = ast_walk_contexts(tmp))) {
			if (!strcasecmp(name, tmp->name)) {
				break;
			}
		}
	}
	ast_unlock_contexts();
	return tmp;
}

#define STATUS_NO_CONTEXT	1
#define STATUS_NO_EXTENSION	2
#define STATUS_NO_PRIORITY	3
#define STATUS_NO_LABEL		4
#define STATUS_SUCCESS		5

static int matchcid(const char *cidpattern, const char *callerid)
{
	/* If the Caller*ID pattern is empty, then we're matching NO Caller*ID, so
	   failing to get a number should count as a match, otherwise not */

	if (ast_strlen_zero(callerid)) {
		return ast_strlen_zero(cidpattern) ? 1 : 0;
	}

	return ast_extension_match(cidpattern, callerid);
}

struct ast_exten *pbx_find_extension(struct ast_channel *chan,
	struct ast_context *bypass, struct pbx_find_info *q,
	const char *context, const char *exten, int priority,
	const char *label, const char *callerid, enum ext_match_t action)
{
	int x, res;
	struct ast_context *tmp = NULL;
	struct ast_exten *e = NULL, *eroot = NULL;
	struct ast_include *i = NULL;
	struct ast_sw *sw = NULL;
	struct ast_exten pattern = {NULL, };
	struct scoreboard score = {0, };
	struct ast_str *tmpdata = NULL;

	pattern.label = label;
	pattern.priority = priority;
#ifdef NEED_DEBUG_HERE
	ast_log(LOG_NOTICE, "Looking for cont/ext/prio/label/action = %s/%s/%d/%s/%d\n", context, exten, priority, label, (int) action);
#endif

	/* Initialize status if appropriate */
	if (q->stacklen == 0) {
		q->status = STATUS_NO_CONTEXT;
		q->swo = NULL;
		q->data = NULL;
		q->foundcontext = NULL;
	} else if (q->stacklen >= AST_PBX_MAX_STACK) {
		ast_log(LOG_WARNING, "Maximum PBX stack exceeded\n");
		return NULL;
	}

	/* Check first to see if we've already been checked */
	for (x = 0; x < q->stacklen; x++) {
		if (!strcasecmp(q->incstack[x], context))
			return NULL;
	}

	if (bypass) { /* bypass means we only look there */
		tmp = bypass;
	} else {      /* look in contexts */
		tmp = find_context(context);
		if (!tmp) {
			return NULL;
		}
	}

	if (q->status < STATUS_NO_EXTENSION)
		q->status = STATUS_NO_EXTENSION;

	/* Do a search for matching extension */

	eroot = NULL;
	score.total_specificity = 0;
	score.exten = 0;
	score.total_length = 0;
	if (!tmp->pattern_tree && tmp->root_table) {
		create_match_char_tree(tmp);
#ifdef NEED_DEBUG
		ast_debug(1, "Tree Created in context %s:\n", context);
		log_match_char_tree(tmp->pattern_tree," ");
#endif
	}
#ifdef NEED_DEBUG
	ast_log(LOG_NOTICE, "The Trie we are searching in:\n");
	log_match_char_tree(tmp->pattern_tree, "::  ");
#endif

	do {
		if (!ast_strlen_zero(overrideswitch)) {
			char *osw = ast_strdupa(overrideswitch), *name;
			struct ast_switch *asw;
			ast_switch_f *aswf = NULL;
			char *datap;
			int eval = 0;

			name = strsep(&osw, "/");
			asw = pbx_findswitch(name);

			if (!asw) {
				ast_log(LOG_WARNING, "No such switch '%s'\n", name);
				break;
			}

			if (osw && strchr(osw, '$')) {
				eval = 1;
			}

			if (eval && !(tmpdata = ast_str_thread_get(&switch_data, 512))) {
				ast_log(LOG_WARNING, "Can't evaluate overrideswitch?!\n");
				break;
			} else if (eval) {
				/* Substitute variables now */
				pbx_substitute_variables_helper(chan, osw, ast_str_buffer(tmpdata), ast_str_size(tmpdata));
				datap = ast_str_buffer(tmpdata);
			} else {
				datap = osw;
			}

			/* equivalent of extension_match_core() at the switch level */
			if (action == E_CANMATCH)
				aswf = asw->canmatch;
			else if (action == E_MATCHMORE)
				aswf = asw->matchmore;
			else /* action == E_MATCH */
				aswf = asw->exists;
			if (!aswf) {
				res = 0;
			} else {
				if (chan) {
					ast_autoservice_start(chan);
				}
				res = aswf(chan, context, exten, priority, callerid, datap);
				if (chan) {
					ast_autoservice_stop(chan);
				}
			}
			if (res) {	/* Got a match */
				q->swo = asw;
				q->data = datap;
				q->foundcontext = context;
				/* XXX keep status = STATUS_NO_CONTEXT ? */
				return NULL;
			}
		}
	} while (0);

	if (extenpatternmatchnew) {
		new_find_extension(exten, &score, tmp->pattern_tree, 0, 0, callerid, label, action);
		eroot = score.exten;

		if (score.last_char == '!' && action == E_MATCHMORE) {
			/* We match an extension ending in '!'.
			 * The decision in this case is final and is NULL (no match).
			 */
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"Returning MATCHMORE NULL with exclamation point.\n");
#endif
			return NULL;
		}

		if (!eroot && (action == E_CANMATCH || action == E_MATCHMORE) && score.canmatch_exten) {
			q->status = STATUS_SUCCESS;
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE,"Returning CANMATCH exten %s\n", score.canmatch_exten->exten);
#endif
			return score.canmatch_exten;
		}

		if ((action == E_MATCHMORE || action == E_CANMATCH)  && eroot) {
			if (score.node) {
				struct ast_exten *z = trie_find_next_match(score.node);
				if (z) {
#ifdef NEED_DEBUG_HERE
					ast_log(LOG_NOTICE,"Returning CANMATCH/MATCHMORE next_match exten %s\n", z->exten);
#endif
				} else {
					if (score.canmatch_exten) {
#ifdef NEED_DEBUG_HERE
						ast_log(LOG_NOTICE,"Returning CANMATCH/MATCHMORE canmatchmatch exten %s(%p)\n", score.canmatch_exten->exten, score.canmatch_exten);
#endif
						return score.canmatch_exten;
					} else {
#ifdef NEED_DEBUG_HERE
						ast_log(LOG_NOTICE,"Returning CANMATCH/MATCHMORE next_match exten NULL\n");
#endif
					}
				}
				return z;
			}
#ifdef NEED_DEBUG_HERE
			ast_log(LOG_NOTICE, "Returning CANMATCH/MATCHMORE NULL (no next_match)\n");
#endif
			return NULL;  /* according to the code, complete matches are null matches in MATCHMORE mode */
		}

		if (eroot) {
			/* found entry, now look for the right priority */
			if (q->status < STATUS_NO_PRIORITY)
				q->status = STATUS_NO_PRIORITY;
			e = NULL;
			if (action == E_FINDLABEL && label ) {
				if (q->status < STATUS_NO_LABEL)
					q->status = STATUS_NO_LABEL;
				e = ast_hashtab_lookup(eroot->peer_label_table, &pattern);
			} else {
				e = ast_hashtab_lookup(eroot->peer_table, &pattern);
			}
			if (e) {	/* found a valid match */
				q->status = STATUS_SUCCESS;
				q->foundcontext = context;
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"Returning complete match of exten %s\n", e->exten);
#endif
				return e;
			}
		}
	} else {   /* the old/current default exten pattern match algorithm */

		/* scan the list trying to match extension and CID */
		eroot = NULL;
		while ( (eroot = ast_walk_context_extensions(tmp, eroot)) ) {
			int match = extension_match_core(eroot->exten, exten, action);
			/* 0 on fail, 1 on match, 2 on earlymatch */

			if (!match || (eroot->matchcid && !matchcid(eroot->cidmatch, callerid)))
				continue;	/* keep trying */
			if (match == 2 && action == E_MATCHMORE) {
				/* We match an extension ending in '!'.
				 * The decision in this case is final and is NULL (no match).
				 */
				return NULL;
			}
			/* found entry, now look for the right priority */
			if (q->status < STATUS_NO_PRIORITY)
				q->status = STATUS_NO_PRIORITY;
			e = NULL;
			if (action == E_FINDLABEL && label ) {
				if (q->status < STATUS_NO_LABEL)
					q->status = STATUS_NO_LABEL;
				e = ast_hashtab_lookup(eroot->peer_label_table, &pattern);
			} else {
				e = ast_hashtab_lookup(eroot->peer_table, &pattern);
			}
			if (e) {	/* found a valid match */
				q->status = STATUS_SUCCESS;
				q->foundcontext = context;
				return e;
			}
		}
	}

	/* Check alternative switches */
	AST_LIST_TRAVERSE(&tmp->alts, sw, list) {
		struct ast_switch *asw = pbx_findswitch(sw->name);
		ast_switch_f *aswf = NULL;
		char *datap;

		if (!asw) {
			ast_log(LOG_WARNING, "No such switch '%s'\n", sw->name);
			continue;
		}

		/* Substitute variables now */
		if (sw->eval) {
			if (!(tmpdata = ast_str_thread_get(&switch_data, 512))) {
				ast_log(LOG_WARNING, "Can't evaluate switch?!\n");
				continue;
			}
			pbx_substitute_variables_helper(chan, sw->data, ast_str_buffer(tmpdata), ast_str_size(tmpdata));
		}

		/* equivalent of extension_match_core() at the switch level */
		if (action == E_CANMATCH)
			aswf = asw->canmatch;
		else if (action == E_MATCHMORE)
			aswf = asw->matchmore;
		else /* action == E_MATCH */
			aswf = asw->exists;
		datap = sw->eval ? ast_str_buffer(tmpdata) : sw->data;
		if (!aswf)
			res = 0;
		else {
			if (chan)
				ast_autoservice_start(chan);
			res = aswf(chan, context, exten, priority, callerid, datap);
			if (chan)
				ast_autoservice_stop(chan);
		}
		if (res) {	/* Got a match */
			q->swo = asw;
			q->data = datap;
			q->foundcontext = context;
			/* XXX keep status = STATUS_NO_CONTEXT ? */
			return NULL;
		}
	}
	q->incstack[q->stacklen++] = tmp->name;	/* Setup the stack */
	/* Now try any includes we have in this context */
	for (i = tmp->includes; i; i = i->next) {
		if (include_valid(i)) {
			if ((e = pbx_find_extension(chan, bypass, q, i->rname, exten, priority, label, callerid, action))) {
#ifdef NEED_DEBUG_HERE
				ast_log(LOG_NOTICE,"Returning recursive match of %s\n", e->exten);
#endif
				return e;
			}
			if (q->swo)
				return NULL;
		}
	}
	return NULL;
}

/*!
 * \brief extract offset:length from variable name.
 * \return 1 if there is a offset:length part, which is
 * trimmed off (values go into variables)
 */
static int parse_variable_name(char *var, int *offset, int *length, int *isfunc)
{
	int parens = 0;

	*offset = 0;
	*length = INT_MAX;
	*isfunc = 0;
	for (; *var; var++) {
		if (*var == '(') {
			(*isfunc)++;
			parens++;
		} else if (*var == ')') {
			parens--;
		} else if (*var == ':' && parens == 0) {
			*var++ = '\0';
			sscanf(var, "%30d:%30d", offset, length);
			return 1; /* offset:length valid */
		}
	}
	return 0;
}

/*!
 *\brief takes a substring. It is ok to call with value == workspace.
 * \param value
 * \param offset < 0 means start from the end of the string and set the beginning
 *   to be that many characters back.
 * \param length is the length of the substring, a value less than 0 means to leave
 * that many off the end.
 * \param workspace
 * \param workspace_len
 * Always return a copy in workspace.
 */
static char *substring(const char *value, int offset, int length, char *workspace, size_t workspace_len)
{
	char *ret = workspace;
	int lr;	/* length of the input string after the copy */

	ast_copy_string(workspace, value, workspace_len); /* always make a copy */

	lr = strlen(ret); /* compute length after copy, so we never go out of the workspace */

	/* Quick check if no need to do anything */
	if (offset == 0 && length >= lr)	/* take the whole string */
		return ret;

	if (offset < 0)	{	/* translate negative offset into positive ones */
		offset = lr + offset;
		if (offset < 0) /* If the negative offset was greater than the length of the string, just start at the beginning */
			offset = 0;
	}

	/* too large offset result in empty string so we know what to return */
	if (offset >= lr)
		return ret + lr;	/* the final '\0' */

	ret += offset;		/* move to the start position */
	if (length >= 0 && length < lr - offset)	/* truncate if necessary */
		ret[length] = '\0';
	else if (length < 0) {
		if (lr > offset - length) /* After we remove from the front and from the rear, is there anything left? */
			ret[lr + length - offset] = '\0';
		else
			ret[0] = '\0';
	}

	return ret;
}

static const char *ast_str_substring(struct ast_str *value, int offset, int length)
{
	int lr;	/* length of the input string after the copy */

	lr = ast_str_strlen(value); /* compute length after copy, so we never go out of the workspace */

	/* Quick check if no need to do anything */
	if (offset == 0 && length >= lr)	/* take the whole string */
		return ast_str_buffer(value);

	if (offset < 0)	{	/* translate negative offset into positive ones */
		offset = lr + offset;
		if (offset < 0) /* If the negative offset was greater than the length of the string, just start at the beginning */
			offset = 0;
	}

	/* too large offset result in empty string so we know what to return */
	if (offset >= lr) {
		ast_str_reset(value);
		return ast_str_buffer(value);
	}

	if (offset > 0) {
		/* Go ahead and chop off the beginning */
		memmove(ast_str_buffer(value), ast_str_buffer(value) + offset, ast_str_strlen(value) - offset + 1);
		lr -= offset;
	}

	if (length >= 0 && length < lr) {	/* truncate if necessary */
		char *tmp = ast_str_buffer(value);
		tmp[length] = '\0';
		ast_str_update(value);
	} else if (length < 0) {
		if (lr > -length) { /* After we remove from the front and from the rear, is there anything left? */
			char *tmp = ast_str_buffer(value);
			tmp[lr + length] = '\0';
			ast_str_update(value);
		} else {
			ast_str_reset(value);
		}
	} else {
		/* Nothing to do, but update the buffer length */
		ast_str_update(value);
	}

	return ast_str_buffer(value);
}

/*! \brief  Support for Asterisk built-in variables in the dialplan

\note	See also
	- \ref AstVar	Channel variables
	- \ref AstCauses The HANGUPCAUSE variable
 */
void pbx_retrieve_variable(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp)
{
	struct ast_str *str = ast_str_create(16);
	const char *cret;

	cret = ast_str_retrieve_variable(&str, 0, c, headp, var);
	ast_copy_string(workspace, ast_str_buffer(str), workspacelen);
	*ret = cret ? workspace : NULL;
	ast_free(str);
}

const char *ast_str_retrieve_variable(struct ast_str **str, ssize_t maxlen, struct ast_channel *c, struct varshead *headp, const char *var)
{
	const char not_found = '\0';
	char *tmpvar;
	const char *ret;
	const char *s;	/* the result */
	int offset, length;
	int i, need_substring;
	struct varshead *places[2] = { headp, &globals };	/* list of places where we may look */
	char workspace[20];

	if (c) {
		ast_channel_lock(c);
		places[0] = ast_channel_varshead(c);
	}
	/*
	 * Make a copy of var because parse_variable_name() modifies the string.
	 * Then if called directly, we might need to run substring() on the result;
	 * remember this for later in 'need_substring', 'offset' and 'length'
	 */
	tmpvar = ast_strdupa(var);	/* parse_variable_name modifies the string */
	need_substring = parse_variable_name(tmpvar, &offset, &length, &i /* ignored */);

	/*
	 * Look first into predefined variables, then into variable lists.
	 * Variable 's' points to the result, according to the following rules:
	 * s == &not_found (set at the beginning) means that we did not find a
	 *	matching variable and need to look into more places.
	 * If s != &not_found, s is a valid result string as follows:
	 * s = NULL if the variable does not have a value;
	 *	you typically do this when looking for an unset predefined variable.
	 * s = workspace if the result has been assembled there;
	 *	typically done when the result is built e.g. with an snprintf(),
	 *	so we don't need to do an additional copy.
	 * s != workspace in case we have a string, that needs to be copied
	 *	(the ast_copy_string is done once for all at the end).
	 *	Typically done when the result is already available in some string.
	 */
	s = &not_found;	/* default value */
	if (c) {	/* This group requires a valid channel */
		/* Names with common parts are looked up a piece at a time using strncmp. */
		if (!strncmp(var, "CALL", 4)) {
			if (!strncmp(var + 4, "ING", 3)) {
				if (!strcmp(var + 7, "PRES")) {			/* CALLINGPRES */
					ast_str_set(str, maxlen, "%d",
						ast_party_id_presentation(&ast_channel_caller(c)->id));
					s = ast_str_buffer(*str);
				} else if (!strcmp(var + 7, "ANI2")) {		/* CALLINGANI2 */
					ast_str_set(str, maxlen, "%d", ast_channel_caller(c)->ani2);
					s = ast_str_buffer(*str);
				} else if (!strcmp(var + 7, "TON")) {		/* CALLINGTON */
					ast_str_set(str, maxlen, "%d", ast_channel_caller(c)->id.number.plan);
					s = ast_str_buffer(*str);
				} else if (!strcmp(var + 7, "TNS")) {		/* CALLINGTNS */
					ast_str_set(str, maxlen, "%d", ast_channel_dialed(c)->transit_network_select);
					s = ast_str_buffer(*str);
				}
			}
		} else if (!strcmp(var, "HINT")) {
			s = ast_str_get_hint(str, maxlen, NULL, 0, c, ast_channel_context(c), ast_channel_exten(c)) ? ast_str_buffer(*str) : NULL;
		} else if (!strcmp(var, "HINTNAME")) {
			s = ast_str_get_hint(NULL, 0, str, maxlen, c, ast_channel_context(c), ast_channel_exten(c)) ? ast_str_buffer(*str) : NULL;
		} else if (!strcmp(var, "EXTEN")) {
			s = ast_channel_exten(c);
		} else if (!strcmp(var, "CONTEXT")) {
			s = ast_channel_context(c);
		} else if (!strcmp(var, "PRIORITY")) {
			ast_str_set(str, maxlen, "%d", ast_channel_priority(c));
			s = ast_str_buffer(*str);
		} else if (!strcmp(var, "CHANNEL")) {
			s = ast_channel_name(c);
		} else if (!strcmp(var, "UNIQUEID")) {
			s = ast_channel_uniqueid(c);
		} else if (!strcmp(var, "HANGUPCAUSE")) {
			ast_str_set(str, maxlen, "%d", ast_channel_hangupcause(c));
			s = ast_str_buffer(*str);
		}
	}
	if (s == &not_found) { /* look for more */
		if (!strcmp(var, "EPOCH")) {
			ast_str_set(str, maxlen, "%d", (int) time(NULL));
			s = ast_str_buffer(*str);
		} else if (!strcmp(var, "SYSTEMNAME")) {
			s = ast_config_AST_SYSTEM_NAME;
		} else if (!strcmp(var, "ASTETCDIR")) {
			s = ast_config_AST_CONFIG_DIR;
		} else if (!strcmp(var, "ASTMODDIR")) {
			s = ast_config_AST_MODULE_DIR;
		} else if (!strcmp(var, "ASTVARLIBDIR")) {
			s = ast_config_AST_VAR_DIR;
		} else if (!strcmp(var, "ASTDBDIR")) {
			s = ast_config_AST_DB;
		} else if (!strcmp(var, "ASTKEYDIR")) {
			s = ast_config_AST_KEY_DIR;
		} else if (!strcmp(var, "ASTDATADIR")) {
			s = ast_config_AST_DATA_DIR;
		} else if (!strcmp(var, "ASTAGIDIR")) {
			s = ast_config_AST_AGI_DIR;
		} else if (!strcmp(var, "ASTSPOOLDIR")) {
			s = ast_config_AST_SPOOL_DIR;
		} else if (!strcmp(var, "ASTRUNDIR")) {
			s = ast_config_AST_RUN_DIR;
		} else if (!strcmp(var, "ASTLOGDIR")) {
			s = ast_config_AST_LOG_DIR;
		} else if (!strcmp(var, "ENTITYID")) {
			ast_eid_to_str(workspace, sizeof(workspace), &ast_eid_default);
			s = workspace;
		}
	}
	/* if not found, look into chanvars or global vars */
	for (i = 0; s == &not_found && i < ARRAY_LEN(places); i++) {
		struct ast_var_t *variables;
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_rwlock_rdlock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (!strcasecmp(ast_var_name(variables), var)) {
				s = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_rwlock_unlock(&globalslock);
	}
	if (s == &not_found || s == NULL) {
		ast_debug(5, "Result of '%s' is NULL\n", var);
		ret = NULL;
	} else {
		ast_debug(5, "Result of '%s' is '%s'\n", var, s);
		if (s != ast_str_buffer(*str)) {
			ast_str_set(str, maxlen, "%s", s);
		}
		ret = ast_str_buffer(*str);
		if (need_substring) {
			ret = ast_str_substring(*str, offset, length);
			ast_debug(2, "Final result of '%s' is '%s'\n", var, ret);
		}
	}

	if (c) {
		ast_channel_unlock(c);
	}
	return ret;
}

static void exception_store_free(void *data)
{
	struct pbx_exception *exception = data;
	ast_string_field_free_memory(exception);
	ast_free(exception);
}

static const struct ast_datastore_info exception_store_info = {
	.type = "EXCEPTION",
	.destroy = exception_store_free,
};

/*!
 * \internal
 * \brief Set the PBX to execute the exception extension.
 *
 * \param chan Channel to raise the exception on.
 * \param reason Reason exception is raised.
 * \param priority Dialplan priority to set.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int raise_exception(struct ast_channel *chan, const char *reason, int priority)
{
	struct ast_datastore *ds = ast_channel_datastore_find(chan, &exception_store_info, NULL);
	struct pbx_exception *exception = NULL;

	if (!ds) {
		ds = ast_datastore_alloc(&exception_store_info, NULL);
		if (!ds)
			return -1;
		if (!(exception = ast_calloc_with_stringfields(1, struct pbx_exception, 128))) {
			ast_datastore_free(ds);
			return -1;
		}
		ds->data = exception;
		ast_channel_datastore_add(chan, ds);
	} else
		exception = ds->data;

	ast_string_field_set(exception, reason, reason);
	ast_string_field_set(exception, context, ast_channel_context(chan));
	ast_string_field_set(exception, exten, ast_channel_exten(chan));
	exception->priority = ast_channel_priority(chan);
	set_ext_pri(chan, "e", priority);
	return 0;
}

int pbx_builtin_raise_exception(struct ast_channel *chan, const char *reason)
{
	/* Priority will become 1, next time through the AUTOLOOP */
	return raise_exception(chan, reason, 0);
}

static int acf_exception_read(struct ast_channel *chan, const char *name, char *data, char *buf, size_t buflen)
{
	struct ast_datastore *ds = ast_channel_datastore_find(chan, &exception_store_info, NULL);
	struct pbx_exception *exception = NULL;
	if (!ds || !ds->data)
		return -1;
	exception = ds->data;
	if (!strcasecmp(data, "REASON"))
		ast_copy_string(buf, exception->reason, buflen);
	else if (!strcasecmp(data, "CONTEXT"))
		ast_copy_string(buf, exception->context, buflen);
	else if (!strncasecmp(data, "EXTEN", 5))
		ast_copy_string(buf, exception->exten, buflen);
	else if (!strcasecmp(data, "PRIORITY"))
		snprintf(buf, buflen, "%d", exception->priority);
	else
		return -1;
	return 0;
}

static struct ast_custom_function exception_function = {
	.name = "EXCEPTION",
	.read = acf_exception_read,
};

static char *handle_show_functions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_custom_function *acf;
	int count_acf = 0;
	int like = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show functions [like]";
		e->usage =
			"Usage: core show functions [like <text>]\n"
			"       List builtin functions, optionally only those matching a given string\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 5 && (!strcmp(a->argv[3], "like")) ) {
		like = 1;
	} else if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "%s Custom Functions:\n--------------------------------------------------------------------------------\n", like ? "Matching" : "Installed");

	AST_RWLIST_RDLOCK(&acf_root);
	AST_RWLIST_TRAVERSE(&acf_root, acf, acflist) {
		if (!like || strstr(acf->name, a->argv[4])) {
			count_acf++;
			ast_cli(a->fd, "%-20.20s  %-35.35s  %s\n",
				S_OR(acf->name, ""),
				S_OR(acf->syntax, ""),
				S_OR(acf->synopsis, ""));
		}
	}
	AST_RWLIST_UNLOCK(&acf_root);

	ast_cli(a->fd, "%d %scustom functions installed.\n", count_acf, like ? "matching " : "");

	return CLI_SUCCESS;
}

static char *handle_show_function(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_custom_function *acf;
	/* Maximum number of characters added by terminal coloring is 22 */
	char infotitle[64 + AST_MAX_APP + 22], syntitle[40], destitle[40], argtitle[40], seealsotitle[40];
	char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL, *seealso = NULL;
	char stxtitle[40], *syntax = NULL, *arguments = NULL;
	int syntax_size, description_size, synopsis_size, arguments_size, seealso_size;
	char *ret = NULL;
	int which = 0;
	int wordlen;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show function";
		e->usage =
			"Usage: core show function <function>\n"
			"       Describe a particular dialplan function.\n";
		return NULL;
	case CLI_GENERATE:
		wordlen = strlen(a->word);
		/* case-insensitive for convenience in this 'complete' function */
		AST_RWLIST_RDLOCK(&acf_root);
		AST_RWLIST_TRAVERSE(&acf_root, acf, acflist) {
			if (!strncasecmp(a->word, acf->name, wordlen) && ++which > a->n) {
				ret = ast_strdup(acf->name);
				break;
			}
		}
		AST_RWLIST_UNLOCK(&acf_root);

		return ret;
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(acf = ast_custom_function_find(a->argv[3]))) {
		ast_cli(a->fd, "No function by that name registered.\n");
		return CLI_FAILURE;
	}

	syntax_size = strlen(S_OR(acf->syntax, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
	if (!(syntax = ast_malloc(syntax_size))) {
		ast_cli(a->fd, "Memory allocation failure!\n");
		return CLI_FAILURE;
	}

	snprintf(info, sizeof(info), "\n  -= Info about function '%s' =- \n\n", acf->name);
	term_color(infotitle, info, COLOR_MAGENTA, 0, sizeof(infotitle));
	term_color(syntitle, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
	term_color(destitle, "[Description]\n", COLOR_MAGENTA, 0, 40);
	term_color(stxtitle, "[Syntax]\n", COLOR_MAGENTA, 0, 40);
	term_color(argtitle, "[Arguments]\n", COLOR_MAGENTA, 0, 40);
	term_color(seealsotitle, "[See Also]\n", COLOR_MAGENTA, 0, 40);
	term_color(syntax, S_OR(acf->syntax, "Not available"), COLOR_CYAN, 0, syntax_size);
#ifdef AST_XML_DOCS
	if (acf->docsrc == AST_XML_DOC) {
		arguments = ast_xmldoc_printable(S_OR(acf->arguments, "Not available"), 1);
		synopsis = ast_xmldoc_printable(S_OR(acf->synopsis, "Not available"), 1);
		description = ast_xmldoc_printable(S_OR(acf->desc, "Not available"), 1);
		seealso = ast_xmldoc_printable(S_OR(acf->seealso, "Not available"), 1);
	} else
#endif
	{
		synopsis_size = strlen(S_OR(acf->synopsis, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		synopsis = ast_malloc(synopsis_size);

		description_size = strlen(S_OR(acf->desc, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		description = ast_malloc(description_size);

		arguments_size = strlen(S_OR(acf->arguments, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		arguments = ast_malloc(arguments_size);

		seealso_size = strlen(S_OR(acf->seealso, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		seealso = ast_malloc(seealso_size);

		/* check allocated memory. */
		if (!synopsis || !description || !arguments || !seealso) {
			ast_free(synopsis);
			ast_free(description);
			ast_free(arguments);
			ast_free(seealso);
			ast_free(syntax);
			return CLI_FAILURE;
		}

		term_color(arguments, S_OR(acf->arguments, "Not available"), COLOR_CYAN, 0, arguments_size);
		term_color(synopsis, S_OR(acf->synopsis, "Not available"), COLOR_CYAN, 0, synopsis_size);
		term_color(description, S_OR(acf->desc, "Not available"), COLOR_CYAN, 0, description_size);
		term_color(seealso, S_OR(acf->seealso, "Not available"), COLOR_CYAN, 0, seealso_size);
	}

	ast_cli(a->fd, "%s%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n",
			infotitle, syntitle, synopsis, destitle, description,
			stxtitle, syntax, argtitle, arguments, seealsotitle, seealso);

	ast_free(arguments);
	ast_free(synopsis);
	ast_free(description);
	ast_free(seealso);
	ast_free(syntax);

	return CLI_SUCCESS;
}

struct ast_custom_function *ast_custom_function_find(const char *name)
{
	struct ast_custom_function *acf = NULL;

	AST_RWLIST_RDLOCK(&acf_root);
	AST_RWLIST_TRAVERSE(&acf_root, acf, acflist) {
		if (!strcmp(name, acf->name))
			break;
	}
	AST_RWLIST_UNLOCK(&acf_root);

	return acf;
}

int ast_custom_function_unregister(struct ast_custom_function *acf)
{
	struct ast_custom_function *cur;
	struct ast_custom_escalating_function *cur_escalation;

	if (!acf) {
		return -1;
	}

	AST_RWLIST_WRLOCK(&acf_root);
	if ((cur = AST_RWLIST_REMOVE(&acf_root, acf, acflist))) {
#ifdef AST_XML_DOCS
		if (cur->docsrc == AST_XML_DOC) {
			ast_string_field_free_memory(acf);
		}
#endif
		ast_verb(2, "Unregistered custom function %s\n", cur->name);
	}
	AST_RWLIST_UNLOCK(&acf_root);

	/* Remove from the escalation list */
	AST_RWLIST_WRLOCK(&escalation_root);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&escalation_root, cur_escalation, list) {
		if (cur_escalation->acf == acf) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(cur_escalation);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&escalation_root);

	return cur ? 0 : -1;
}

/*!
 * \brief Returns true if given custom function escalates privileges on read.
 *
 * \param acf Custom function to query.
 * \return True (non-zero) if reads escalate privileges.
 * \return False (zero) if reads just read.
 */
static int read_escalates(const struct ast_custom_function *acf) {
	int res = 0;
	struct ast_custom_escalating_function *cur_escalation;

	AST_RWLIST_RDLOCK(&escalation_root);
	AST_RWLIST_TRAVERSE(&escalation_root, cur_escalation, list) {
		if (cur_escalation->acf == acf) {
			res = cur_escalation->read_escalates;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&escalation_root);
	return res;
}

/*!
 * \brief Returns true if given custom function escalates privileges on write.
 *
 * \param acf Custom function to query.
 * \return True (non-zero) if writes escalate privileges.
 * \return False (zero) if writes just write.
 */
static int write_escalates(const struct ast_custom_function *acf) {
	int res = 0;
	struct ast_custom_escalating_function *cur_escalation;

	AST_RWLIST_RDLOCK(&escalation_root);
	AST_RWLIST_TRAVERSE(&escalation_root, cur_escalation, list) {
		if (cur_escalation->acf == acf) {
			res = cur_escalation->write_escalates;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&escalation_root);
	return res;
}

/*! \internal
 *  \brief Retrieve the XML documentation of a specified ast_custom_function,
 *         and populate ast_custom_function string fields.
 *  \param acf ast_custom_function structure with empty 'desc' and 'synopsis'
 *             but with a function 'name'.
 *  \retval -1 On error.
 *  \retval 0 On succes.
 */
static int acf_retrieve_docs(struct ast_custom_function *acf)
{
#ifdef AST_XML_DOCS
	char *tmpxml;

	/* Let's try to find it in the Documentation XML */
	if (!ast_strlen_zero(acf->desc) || !ast_strlen_zero(acf->synopsis)) {
		return 0;
	}

	if (ast_string_field_init(acf, 128)) {
		return -1;
	}

	/* load synopsis */
	tmpxml = ast_xmldoc_build_synopsis("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, synopsis, tmpxml);
	ast_free(tmpxml);

	/* load description */
	tmpxml = ast_xmldoc_build_description("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, desc, tmpxml);
	ast_free(tmpxml);

	/* load syntax */
	tmpxml = ast_xmldoc_build_syntax("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, syntax, tmpxml);
	ast_free(tmpxml);

	/* load arguments */
	tmpxml = ast_xmldoc_build_arguments("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, arguments, tmpxml);
	ast_free(tmpxml);

	/* load seealso */
	tmpxml = ast_xmldoc_build_seealso("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, seealso, tmpxml);
	ast_free(tmpxml);

	acf->docsrc = AST_XML_DOC;
#endif

	return 0;
}

int __ast_custom_function_register(struct ast_custom_function *acf, struct ast_module *mod)
{
	struct ast_custom_function *cur;
	char tmps[80];

	if (!acf) {
		return -1;
	}

	acf->mod = mod;
#ifdef AST_XML_DOCS
	acf->docsrc = AST_STATIC_DOC;
#endif

	if (acf_retrieve_docs(acf)) {
		return -1;
	}

	AST_RWLIST_WRLOCK(&acf_root);

	AST_RWLIST_TRAVERSE(&acf_root, cur, acflist) {
		if (!strcmp(acf->name, cur->name)) {
			ast_log(LOG_ERROR, "Function %s already registered.\n", acf->name);
			AST_RWLIST_UNLOCK(&acf_root);
			return -1;
		}
	}

	/* Store in alphabetical order */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&acf_root, cur, acflist) {
		if (strcasecmp(acf->name, cur->name) < 0) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(acf, acflist);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!cur) {
		AST_RWLIST_INSERT_TAIL(&acf_root, acf, acflist);
	}

	AST_RWLIST_UNLOCK(&acf_root);

	ast_verb(2, "Registered custom function '%s'\n", term_color(tmps, acf->name, COLOR_BRCYAN, 0, sizeof(tmps)));

	return 0;
}

int __ast_custom_function_register_escalating(struct ast_custom_function *acf, enum ast_custom_function_escalation escalation, struct ast_module *mod)
{
	struct ast_custom_escalating_function *acf_escalation = NULL;
	int res;

	res = __ast_custom_function_register(acf, mod);
	if (res != 0) {
		return -1;
	}

	if (escalation == AST_CFE_NONE) {
		/* No escalations; no need to do anything else */
		return 0;
	}

	acf_escalation = ast_calloc(1, sizeof(*acf_escalation));
	if (!acf_escalation) {
		ast_custom_function_unregister(acf);
		return -1;
	}

	acf_escalation->acf = acf;
	switch (escalation) {
	case AST_CFE_NONE:
		break;
	case AST_CFE_READ:
		acf_escalation->read_escalates = 1;
		break;
	case AST_CFE_WRITE:
		acf_escalation->write_escalates = 1;
		break;
	case AST_CFE_BOTH:
		acf_escalation->read_escalates = 1;
		acf_escalation->write_escalates = 1;
		break;
	}

	AST_RWLIST_WRLOCK(&escalation_root);
	AST_RWLIST_INSERT_TAIL(&escalation_root, acf_escalation, list);
	AST_RWLIST_UNLOCK(&escalation_root);

	return 0;
}

/*! \brief return a pointer to the arguments of the function,
 * and terminates the function name with '\\0'
 */
static char *func_args(char *function)
{
	char *args = strchr(function, '(');

	if (!args) {
		ast_log(LOG_WARNING, "Function '%s' doesn't contain parentheses.  Assuming null argument.\n", function);
	} else {
		char *p;
		*args++ = '\0';
		if ((p = strrchr(args, ')'))) {
			*p = '\0';
		} else {
			ast_log(LOG_WARNING, "Can't find trailing parenthesis for function '%s(%s'?\n", function, args);
		}
	}
	return args;
}

void pbx_live_dangerously(int new_live_dangerously)
{
	if (new_live_dangerously && !live_dangerously) {
		ast_log(LOG_WARNING, "Privilege escalation protection disabled!\n"
			"See https://wiki.asterisk.org/wiki/x/1gKfAQ for more details.\n");
	}

	if (!new_live_dangerously && live_dangerously) {
		ast_log(LOG_NOTICE, "Privilege escalation protection enabled.\n");
	}
	live_dangerously = new_live_dangerously;
}

int ast_thread_inhibit_escalations(void)
{
	int *thread_inhibit_escalations;

	thread_inhibit_escalations = ast_threadstorage_get(
		&thread_inhibit_escalations_tl, sizeof(*thread_inhibit_escalations));

	if (thread_inhibit_escalations == NULL) {
		ast_log(LOG_ERROR, "Error inhibiting privilege escalations for current thread\n");
		return -1;
	}

	*thread_inhibit_escalations = 1;
	return 0;
}

/*!
 * \brief Indicates whether the current thread inhibits the execution of
 * dangerous functions.
 *
 * \return True (non-zero) if dangerous function execution is inhibited.
 * \return False (zero) if dangerous function execution is allowed.
 */
static int thread_inhibits_escalations(void)
{
	int *thread_inhibit_escalations;

	thread_inhibit_escalations = ast_threadstorage_get(
		&thread_inhibit_escalations_tl, sizeof(*thread_inhibit_escalations));

	if (thread_inhibit_escalations == NULL) {
		ast_log(LOG_ERROR, "Error checking thread's ability to run dangerous functions\n");
		/* On error, assume that we are inhibiting */
		return 1;
	}

	return *thread_inhibit_escalations;
}

/*!
 * \brief Determines whether execution of a custom function's read function
 * is allowed.
 *
 * \param acfptr Custom function to check
 * \return True (non-zero) if reading is allowed.
 * \return False (zero) if reading is not allowed.
 */
static int is_read_allowed(struct ast_custom_function *acfptr)
{
	if (!acfptr) {
		return 1;
	}

	if (!read_escalates(acfptr)) {
		return 1;
	}

	if (!thread_inhibits_escalations()) {
		return 1;
	}

	if (live_dangerously) {
		/* Global setting overrides the thread's preference */
		ast_debug(2, "Reading %s from a dangerous context\n",
			acfptr->name);
		return 1;
	}

	/* We have no reason to allow this function to execute */
	return 0;
}

/*!
 * \brief Determines whether execution of a custom function's write function
 * is allowed.
 *
 * \param acfptr Custom function to check
 * \return True (non-zero) if writing is allowed.
 * \return False (zero) if writing is not allowed.
 */
static int is_write_allowed(struct ast_custom_function *acfptr)
{
	if (!acfptr) {
		return 1;
	}

	if (!write_escalates(acfptr)) {
		return 1;
	}

	if (!thread_inhibits_escalations()) {
		return 1;
	}

	if (live_dangerously) {
		/* Global setting overrides the thread's preference */
		ast_debug(2, "Writing %s from a dangerous context\n",
			acfptr->name);
		return 1;
	}

	/* We have no reason to allow this function to execute */
	return 0;
}

int ast_func_read(struct ast_channel *chan, const char *function, char *workspace, size_t len)
{
	char *copy = ast_strdupa(function);
	char *args = func_args(copy);
	struct ast_custom_function *acfptr = ast_custom_function_find(copy);
	int res;
	struct ast_module_user *u = NULL;

	if (acfptr == NULL) {
		ast_log(LOG_ERROR, "Function %s not registered\n", copy);
	} else if (!acfptr->read && !acfptr->read2) {
		ast_log(LOG_ERROR, "Function %s cannot be read\n", copy);
	} else if (!is_read_allowed(acfptr)) {
		ast_log(LOG_ERROR, "Dangerous function %s read blocked\n", copy);
	} else if (acfptr->read) {
		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		res = acfptr->read(chan, copy, args, workspace, len);
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}
		return res;
	} else {
		struct ast_str *str = ast_str_create(16);
		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		res = acfptr->read2(chan, copy, args, &str, 0);
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}
		ast_copy_string(workspace, ast_str_buffer(str), len > ast_str_size(str) ? ast_str_size(str) : len);
		ast_free(str);
		return res;
	}
	return -1;
}

int ast_func_read2(struct ast_channel *chan, const char *function, struct ast_str **str, ssize_t maxlen)
{
	char *copy = ast_strdupa(function);
	char *args = func_args(copy);
	struct ast_custom_function *acfptr = ast_custom_function_find(copy);
	int res;
	struct ast_module_user *u = NULL;

	if (acfptr == NULL) {
		ast_log(LOG_ERROR, "Function %s not registered\n", copy);
	} else if (!acfptr->read && !acfptr->read2) {
		ast_log(LOG_ERROR, "Function %s cannot be read\n", copy);
	} else if (!is_read_allowed(acfptr)) {
		ast_log(LOG_ERROR, "Dangerous function %s read blocked\n", copy);
	} else {
		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		ast_str_reset(*str);
		if (acfptr->read2) {
			/* ast_str enabled */
			res = acfptr->read2(chan, copy, args, str, maxlen);
		} else {
			/* Legacy function pointer, allocate buffer for result */
			int maxsize = ast_str_size(*str);
			if (maxlen > -1) {
				if (maxlen == 0) {
					if (acfptr->read_max) {
						maxsize = acfptr->read_max;
					} else {
						maxsize = VAR_BUF_SIZE;
					}
				} else {
					maxsize = maxlen;
				}
				ast_str_make_space(str, maxsize);
			}
			res = acfptr->read(chan, copy, args, ast_str_buffer(*str), maxsize);
		}
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}
		return res;
	}
	return -1;
}

int ast_func_write(struct ast_channel *chan, const char *function, const char *value)
{
	char *copy = ast_strdupa(function);
	char *args = func_args(copy);
	struct ast_custom_function *acfptr = ast_custom_function_find(copy);

	if (acfptr == NULL) {
		ast_log(LOG_ERROR, "Function %s not registered\n", copy);
	} else if (!acfptr->write) {
		ast_log(LOG_ERROR, "Function %s cannot be written to\n", copy);
	} else if (!is_write_allowed(acfptr)) {
		ast_log(LOG_ERROR, "Dangerous function %s write blocked\n", copy);
	} else {
		int res;
		struct ast_module_user *u = NULL;
		if (acfptr->mod)
			u = __ast_module_user_add(acfptr->mod, chan);
		res = acfptr->write(chan, copy, args, value);
		if (acfptr->mod && u)
			__ast_module_user_remove(acfptr->mod, u);
		return res;
	}

	return -1;
}

void ast_str_substitute_variables_full(struct ast_str **buf, ssize_t maxlen, struct ast_channel *c, struct varshead *headp, const char *templ, size_t *used)
{
	/* Substitutes variables into buf, based on string templ */
	char *cp4 = NULL;
	const char *whereweare;
	int orig_size = 0;
	int offset, offset2, isfunction;
	const char *nextvar, *nextexp, *nextthing;
	const char *vars, *vare;
	char *finalvars;
	int pos, brackets, needsub, len;
	struct ast_str *substr1 = ast_str_create(16), *substr2 = NULL, *substr3 = ast_str_create(16);

	ast_str_reset(*buf);
	whereweare = templ;
	while (!ast_strlen_zero(whereweare)) {
		/* reset our buffer */
		ast_str_reset(substr3);

		/* Assume we're copying the whole remaining string */
		pos = strlen(whereweare);
		nextvar = NULL;
		nextexp = NULL;
		nextthing = strchr(whereweare, '$');
		if (nextthing) {
			switch (nextthing[1]) {
			case '{':
				nextvar = nextthing;
				pos = nextvar - whereweare;
				break;
			case '[':
				nextexp = nextthing;
				pos = nextexp - whereweare;
				break;
			default:
				pos = 1;
			}
		}

		if (pos) {
			/* Copy that many bytes */
			ast_str_append_substr(buf, maxlen, whereweare, pos);

			templ += pos;
			whereweare += pos;
		}

		if (nextvar) {
			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
				} else if (vare[0] == '{') {
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '['))
					needsub++;
				vare++;
			}
			if (brackets)
				ast_log(LOG_WARNING, "Error in extension logic (missing '}')\n");
			len = vare - vars - 1;

			/* Skip totally over variable string */
			whereweare += (len + 3);

			/* Store variable name (and truncate) */
			ast_str_set_substr(&substr1, 0, vars, len);
			ast_debug(5, "Evaluating '%s' (from '%s' len %d)\n", ast_str_buffer(substr1), vars, len);

			/* Substitute if necessary */
			if (needsub) {
				size_t used;
				if (!substr2) {
					substr2 = ast_str_create(16);
				}

				ast_str_substitute_variables_full(&substr2, 0, c, headp, ast_str_buffer(substr1), &used);
				finalvars = ast_str_buffer(substr2);
			} else {
				finalvars = ast_str_buffer(substr1);
			}

			parse_variable_name(finalvars, &offset, &offset2, &isfunction);
			if (isfunction) {
				/* Evaluate function */
				if (c || !headp) {
					cp4 = ast_func_read2(c, finalvars, &substr3, 0) ? NULL : ast_str_buffer(substr3);
				} else {
					struct varshead old;
					struct ast_channel *bogus = ast_dummy_channel_alloc();
					if (bogus) {
						memcpy(&old, ast_channel_varshead(bogus), sizeof(old));
						memcpy(ast_channel_varshead(bogus), headp, sizeof(*ast_channel_varshead(bogus)));
						cp4 = ast_func_read2(c, finalvars, &substr3, 0) ? NULL : ast_str_buffer(substr3);
						/* Don't deallocate the varshead that was passed in */
						memcpy(ast_channel_varshead(bogus), &old, sizeof(*ast_channel_varshead(bogus)));
						ast_channel_unref(bogus);
					} else {
						ast_log(LOG_ERROR, "Unable to allocate bogus channel for variable substitution.  Function results may be blank.\n");
					}
				}
				ast_debug(2, "Function %s result is '%s'\n", finalvars, cp4 ? cp4 : "(null)");
			} else {
				/* Retrieve variable value */
				ast_str_retrieve_variable(&substr3, 0, c, headp, finalvars);
				cp4 = ast_str_buffer(substr3);
			}
			if (cp4) {
				ast_str_substring(substr3, offset, offset2);
				ast_str_append(buf, maxlen, "%s", ast_str_buffer(substr3));
			}
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '[') {
					brackets++;
				} else if (vare[0] == ']') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			if (brackets)
				ast_log(LOG_WARNING, "Error in extension logic (missing ']')\n");
			len = vare - vars - 1;

			/* Skip totally over expression */
			whereweare += (len + 3);

			/* Store variable name (and truncate) */
			ast_str_set_substr(&substr1, 0, vars, len);

			/* Substitute if necessary */
			if (needsub) {
				size_t used;
				if (!substr2) {
					substr2 = ast_str_create(16);
				}

				ast_str_substitute_variables_full(&substr2, 0, c, headp, ast_str_buffer(substr1), &used);
				finalvars = ast_str_buffer(substr2);
			} else {
				finalvars = ast_str_buffer(substr1);
			}

			if (ast_str_expr(&substr3, 0, c, finalvars)) {
				ast_debug(2, "Expression result is '%s'\n", ast_str_buffer(substr3));
			}
			ast_str_append(buf, maxlen, "%s", ast_str_buffer(substr3));
		}
	}
	*used = ast_str_strlen(*buf) - orig_size;
	ast_free(substr1);
	ast_free(substr2);
	ast_free(substr3);
}

void ast_str_substitute_variables(struct ast_str **buf, ssize_t maxlen, struct ast_channel *chan, const char *templ)
{
	size_t used;
	ast_str_substitute_variables_full(buf, maxlen, chan, NULL, templ, &used);
}

void ast_str_substitute_variables_varshead(struct ast_str **buf, ssize_t maxlen, struct varshead *headp, const char *templ)
{
	size_t used;
	ast_str_substitute_variables_full(buf, maxlen, NULL, headp, templ, &used);
}

void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int count, size_t *used)
{
	/* Substitutes variables into cp2, based on string cp1, cp2 NO LONGER NEEDS TO BE ZEROED OUT!!!!  */
	char *cp4 = NULL;
	const char *whereweare, *orig_cp2 = cp2;
	int length, offset, offset2, isfunction;
	char *workspace = NULL;
	char *ltmp = NULL, *var = NULL;
	char *nextvar, *nextexp, *nextthing;
	char *vars, *vare;
	int pos, brackets, needsub, len;

	*cp2 = 0; /* just in case nothing ends up there */
	whereweare = cp1;
	while (!ast_strlen_zero(whereweare) && count) {
		/* Assume we're copying the whole remaining string */
		pos = strlen(whereweare);
		nextvar = NULL;
		nextexp = NULL;
		nextthing = strchr(whereweare, '$');
		if (nextthing) {
			switch (nextthing[1]) {
			case '{':
				nextvar = nextthing;
				pos = nextvar - whereweare;
				break;
			case '[':
				nextexp = nextthing;
				pos = nextexp - whereweare;
				break;
			default:
				pos = 1;
			}
		}

		if (pos) {
			/* Can't copy more than 'count' bytes */
			if (pos > count)
				pos = count;

			/* Copy that many bytes */
			memcpy(cp2, whereweare, pos);

			count -= pos;
			cp2 += pos;
			whereweare += pos;
			*cp2 = 0;
		}

		if (nextvar) {
			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
				} else if (vare[0] == '{') {
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '['))
					needsub++;
				vare++;
			}
			if (brackets)
				ast_log(LOG_WARNING, "Error in extension logic (missing '}')\n");
			len = vare - vars - 1;

			/* Skip totally over variable string */
			whereweare += (len + 3);

			if (!var)
				var = ast_alloca(VAR_BUF_SIZE);

			/* Store variable name (and truncate) */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				size_t used;
				if (!ltmp)
					ltmp = ast_alloca(VAR_BUF_SIZE);

				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1, &used);
				vars = ltmp;
			} else {
				vars = var;
			}

			if (!workspace)
				workspace = ast_alloca(VAR_BUF_SIZE);

			workspace[0] = '\0';

			parse_variable_name(vars, &offset, &offset2, &isfunction);
			if (isfunction) {
				/* Evaluate function */
				if (c || !headp)
					cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
				else {
					struct varshead old;
					struct ast_channel *c = ast_dummy_channel_alloc();
					if (c) {
						memcpy(&old, ast_channel_varshead(c), sizeof(old));
						memcpy(ast_channel_varshead(c), headp, sizeof(*ast_channel_varshead(c)));
						cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
						/* Don't deallocate the varshead that was passed in */
						memcpy(ast_channel_varshead(c), &old, sizeof(*ast_channel_varshead(c)));
						c = ast_channel_unref(c);
					} else {
						ast_log(LOG_ERROR, "Unable to allocate bogus channel for variable substitution.  Function results may be blank.\n");
					}
				}
				ast_debug(2, "Function %s result is '%s'\n", vars, cp4 ? cp4 : "(null)");
			} else {
				/* Retrieve variable value */
				pbx_retrieve_variable(c, vars, &cp4, workspace, VAR_BUF_SIZE, headp);
			}
			if (cp4) {
				cp4 = substring(cp4, offset, offset2, workspace, VAR_BUF_SIZE);

				length = strlen(cp4);
				if (length > count)
					length = count;
				memcpy(cp2, cp4, length);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '[') {
					brackets++;
				} else if (vare[0] == ']') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			if (brackets)
				ast_log(LOG_WARNING, "Error in extension logic (missing ']')\n");
			len = vare - vars - 1;

			/* Skip totally over expression */
			whereweare += (len + 3);

			if (!var)
				var = ast_alloca(VAR_BUF_SIZE);

			/* Store variable name (and truncate) */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				size_t used;
				if (!ltmp)
					ltmp = ast_alloca(VAR_BUF_SIZE);

				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1, &used);
				vars = ltmp;
			} else {
				vars = var;
			}

			length = ast_expr(vars, cp2, count, c);

			if (length) {
				ast_debug(1, "Expression result is '%s'\n", cp2);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		}
	}
	*used = cp2 - orig_cp2;
}

void pbx_substitute_variables_helper(struct ast_channel *c, const char *cp1, char *cp2, int count)
{
	size_t used;
	pbx_substitute_variables_helper_full(c, (c) ? ast_channel_varshead(c) : NULL, cp1, cp2, count, &used);
}

void pbx_substitute_variables_varshead(struct varshead *headp, const char *cp1, char *cp2, int count)
{
	size_t used;
	pbx_substitute_variables_helper_full(NULL, headp, cp1, cp2, count, &used);
}

/*!
 * \brief The return value depends on the action:
 *
 * E_MATCH, E_CANMATCH, E_MATCHMORE require a real match,
 *	and return 0 on failure, -1 on match;
 * E_FINDLABEL maps the label to a priority, and returns
 *	the priority on success, ... XXX
 * E_SPAWN, spawn an application,
 *
 * \retval 0 on success.
 * \retval  -1 on failure.
 *
 * \note The channel is auto-serviced in this function, because doing an extension
 * match may block for a long time.  For example, if the lookup has to use a network
 * dialplan switch, such as DUNDi or IAX2, it may take a while.  However, the channel
 * auto-service code will queue up any important signalling frames to be processed
 * after this is done.
 */
static int pbx_extension_helper(struct ast_channel *c, struct ast_context *con,
  const char *context, const char *exten, int priority,
  const char *label, const char *callerid, enum ext_match_t action, int *found, int combined_find_spawn)
{
	struct ast_exten *e;
	struct ast_app *app;
	char *substitute = NULL;
	int res;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	char passdata[EXT_DATA_SIZE];

	int matching_action = (action == E_MATCH || action == E_CANMATCH || action == E_MATCHMORE);

	ast_rdlock_contexts();
	if (found)
		*found = 0;

	e = pbx_find_extension(c, con, &q, context, exten, priority, label, callerid, action);
	if (e) {
		if (found)
			*found = 1;
		if (matching_action) {
			ast_unlock_contexts();
			return -1;	/* success, we found it */
		} else if (action == E_FINDLABEL) { /* map the label to a priority */
			res = e->priority;
			ast_unlock_contexts();
			return res;	/* the priority we were looking for */
		} else {	/* spawn */
			if (!e->cached_app)
				e->cached_app = pbx_findapp(e->app);
			app = e->cached_app;
			if (ast_strlen_zero(e->data)) {
				*passdata = '\0';
			} else {
				const char *tmp;
				if ((!(tmp = strchr(e->data, '$'))) || (!strstr(tmp, "${") && !strstr(tmp, "$["))) {
					/* no variables to substitute, copy on through */
					ast_copy_string(passdata, e->data, sizeof(passdata));
				} else {
					/* save e->data on stack for later processing after lock released */
					substitute = ast_strdupa(e->data);
				}
			}
			ast_unlock_contexts();
			if (!app) {
				ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
				return -1;
			}
			if (ast_channel_context(c) != context)
				ast_channel_context_set(c, context);
			if (ast_channel_exten(c) != exten)
				ast_channel_exten_set(c, exten);
			ast_channel_priority_set(c, priority);
			if (substitute) {
				pbx_substitute_variables_helper(c, substitute, passdata, sizeof(passdata)-1);
			}
#ifdef CHANNEL_TRACE
			ast_channel_trace_update(c);
#endif
			ast_debug(1, "Launching '%s'\n", app->name);
			if (VERBOSITY_ATLEAST(3)) {
				char tmp[80], tmp2[80], tmp3[EXT_DATA_SIZE];
				ast_verb(3, "Executing [%s@%s:%d] %s(\"%s\", \"%s\") %s\n",
					exten, context, priority,
					term_color(tmp, app->name, COLOR_BRCYAN, 0, sizeof(tmp)),
					term_color(tmp2, ast_channel_name(c), COLOR_BRMAGENTA, 0, sizeof(tmp2)),
					term_color(tmp3, passdata, COLOR_BRMAGENTA, 0, sizeof(tmp3)),
					"in new stack");
			}
			/*** DOCUMENTATION
				<managerEventInstance>
					<synopsis>Raised when a channel enters a new context, extension, priority.</synopsis>
					<syntax>
						<parameter name="Application">
							<para>The application about to be executed.</para>
						</parameter>
						<parameter name="AppData">
							<para>The data to be passed to the application.</para>
						</parameter>
					</syntax>
				</managerEventInstance>
			***/
			manager_event(EVENT_FLAG_DIALPLAN, "Newexten",
					"Channel: %s\r\n"
					"Context: %s\r\n"
					"Extension: %s\r\n"
					"Priority: %d\r\n"
					"Application: %s\r\n"
					"AppData: %s\r\n"
					"Uniqueid: %s\r\n",
					ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), app->name, passdata, ast_channel_uniqueid(c));
			return pbx_exec(c, app, passdata);	/* 0 on success, -1 on failure */
		}
	} else if (q.swo) {	/* not found here, but in another switch */
		if (found)
			*found = 1;
		ast_unlock_contexts();
		if (matching_action) {
			return -1;
		} else {
			if (!q.swo->exec) {
				ast_log(LOG_WARNING, "No execution engine for switch %s\n", q.swo->name);
				res = -1;
			}
			return q.swo->exec(c, q.foundcontext ? q.foundcontext : context, exten, priority, callerid, q.data);
		}
	} else {	/* not found anywhere, see what happened */
		ast_unlock_contexts();
		/* Using S_OR here because Solaris doesn't like NULL being passed to ast_log */
		switch (q.status) {
		case STATUS_NO_CONTEXT:
			if (!matching_action && !combined_find_spawn)
				ast_log(LOG_NOTICE, "Cannot find extension context '%s'\n", S_OR(context, ""));
			break;
		case STATUS_NO_EXTENSION:
			if (!matching_action && !combined_find_spawn)
				ast_log(LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, S_OR(context, ""));
			break;
		case STATUS_NO_PRIORITY:
			if (!matching_action && !combined_find_spawn)
				ast_log(LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, S_OR(context, ""));
			break;
		case STATUS_NO_LABEL:
			if (context && !combined_find_spawn)
				ast_log(LOG_NOTICE, "No such label '%s' in extension '%s' in context '%s'\n", label, exten, S_OR(context, ""));
			break;
		default:
			ast_debug(1, "Shouldn't happen!\n");
		}

		return (matching_action) ? 0 : -1;
	}
}

/*! \brief Find hint for given extension in context */
static struct ast_exten *ast_hint_extension_nolock(struct ast_channel *c, const char *context, const char *exten)
{
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is set in pbx_find_context */
	return pbx_find_extension(c, NULL, &q, context, exten, PRIORITY_HINT, NULL, "", E_MATCH);
}

static struct ast_exten *ast_hint_extension(struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e;
	ast_rdlock_contexts();
	e = ast_hint_extension_nolock(c, context, exten);
	ast_unlock_contexts();
	return e;
}

enum ast_extension_states ast_devstate_to_extenstate(enum ast_device_state devstate)
{
	switch (devstate) {
	case AST_DEVICE_ONHOLD:
		return AST_EXTENSION_ONHOLD;
	case AST_DEVICE_BUSY:
		return AST_EXTENSION_BUSY;
	case AST_DEVICE_UNKNOWN:
		return AST_EXTENSION_NOT_INUSE;
	case AST_DEVICE_UNAVAILABLE:
	case AST_DEVICE_INVALID:
		return AST_EXTENSION_UNAVAILABLE;
	case AST_DEVICE_RINGINUSE:
		return (AST_EXTENSION_INUSE | AST_EXTENSION_RINGING);
	case AST_DEVICE_RINGING:
		return AST_EXTENSION_RINGING;
	case AST_DEVICE_INUSE:
		return AST_EXTENSION_INUSE;
	case AST_DEVICE_NOT_INUSE:
		return AST_EXTENSION_NOT_INUSE;
	case AST_DEVICE_TOTAL: /* not a device state, included for completeness */
		break;
	}

	return AST_EXTENSION_NOT_INUSE;
}

/*!
 * \internal
 * \brief Parse out the presence portion of the hint string
 */
static char *parse_hint_presence(struct ast_str *hint_args)
{
	char *copy = ast_strdupa(ast_str_buffer(hint_args));
	char *tmp = "";

	if ((tmp = strrchr(copy, ','))) {
		*tmp = '\0';
		tmp++;
	} else {
		return NULL;
	}
	ast_str_set(&hint_args, 0, "%s", tmp);
	return ast_str_buffer(hint_args);
}

/*!
 * \internal
 * \brief Parse out the device portion of the hint string
 */
static char *parse_hint_device(struct ast_str *hint_args)
{
	char *copy = ast_strdupa(ast_str_buffer(hint_args));
	char *tmp;

	if ((tmp = strrchr(copy, ','))) {
		*tmp = '\0';
	}

	ast_str_set(&hint_args, 0, "%s", copy);
	return ast_str_buffer(hint_args);
}

static void device_state_info_dt(void *obj)
{
	struct ast_device_state_info *info = obj;

	ao2_cleanup(info->causing_channel);
}

static struct ao2_container *alloc_device_state_info(void)
{
	return ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1, NULL, NULL);
}

static int ast_extension_state3(struct ast_str *hint_app, struct ao2_container *device_state_info)
{
	char *cur;
	char *rest;
	struct ast_devstate_aggregate agg;

	/* One or more devices separated with a & character */
	rest = parse_hint_device(hint_app);

	ast_devstate_aggregate_init(&agg);
	while ((cur = strsep(&rest, "&"))) {
		enum ast_device_state state = ast_device_state(cur);

		ast_devstate_aggregate_add(&agg, state);
		if (device_state_info) {
			struct ast_device_state_info *obj;

			obj = ao2_alloc_options(sizeof(*obj) + strlen(cur), device_state_info_dt, AO2_ALLOC_OPT_LOCK_NOLOCK);
			/* if failed we cannot add this device */
			if (obj) {
				obj->device_state = state;
				strcpy(obj->device_name, cur);
				ao2_link(device_state_info, obj);
				ao2_ref(obj, -1);
			}
		}
	}

	return ast_devstate_to_extenstate(ast_devstate_aggregate_result(&agg));
}

/*! \brief Check state of extension by using hints */
static int ast_extension_state2(struct ast_exten *e, struct ao2_container *device_state_info)
{
	struct ast_str *hint_app = ast_str_thread_get(&extensionstate_buf, 32);

	if (!e || !hint_app) {
		return -1;
	}

	ast_str_set(&hint_app, 0, "%s", ast_get_extension_app(e));
	return ast_extension_state3(hint_app, device_state_info);
}

/*! \brief Return extension_state as string */
const char *ast_extension_state2str(int extension_state)
{
	int i;

	for (i = 0; (i < ARRAY_LEN(extension_states)); i++) {
		if (extension_states[i].extension_state == extension_state)
			return extension_states[i].text;
	}
	return "Unknown";
}

/*! \internal \brief Check extension state for an extension by using hint */
static int internal_extension_state_extended(struct ast_channel *c, const char *context, const char *exten,
	struct ao2_container *device_state_info)
{
	struct ast_exten *e;

	if (!(e = ast_hint_extension(c, context, exten))) {  /* Do we have a hint for this extension ? */
		return -1;                   /* No hint, return -1 */
	}

	if (e->exten[0] == '_') {
		/* Create this hint on-the-fly */
		ast_add_extension(e->parent->name, 0, exten, e->priority, e->label,
			e->matchcid ? e->cidmatch : NULL, e->app, ast_strdup(e->data), ast_free_ptr,
			e->registrar);
		if (!(e = ast_hint_extension(c, context, exten))) {
			/* Improbable, but not impossible */
			return -1;
		}
	}

	return ast_extension_state2(e, device_state_info);  /* Check all devices in the hint */
}

/*! \brief Check extension state for an extension by using hint */
int ast_extension_state(struct ast_channel *c, const char *context, const char *exten)
{
	return internal_extension_state_extended(c, context, exten, NULL);
}

/*! \brief Check extended extension state for an extension by using hint */
int ast_extension_state_extended(struct ast_channel *c, const char *context, const char *exten,
	struct ao2_container **device_state_info)
{
	struct ao2_container *container = NULL;
	int ret;

	if (device_state_info) {
		container = alloc_device_state_info();
	}

	ret = internal_extension_state_extended(c, context, exten, container);
	if (ret < 0 && container) {
		ao2_ref(container, -1);
		container = NULL;
	}

	if (device_state_info) {
		get_device_state_causing_channels(container);
		*device_state_info = container;
	}

	return ret;
}

static int extension_presence_state_helper(struct ast_exten *e, char **subtype, char **message)
{
	struct ast_str *hint_app = ast_str_thread_get(&extensionstate_buf, 32);
	char *presence_provider;
	const char *app;

	if (!e || !hint_app) {
		return -1;
	}

	app = ast_get_extension_app(e);
	if (ast_strlen_zero(app)) {
		return -1;
	}

	ast_str_set(&hint_app, 0, "%s", app);
	presence_provider = parse_hint_presence(hint_app);

	if (ast_strlen_zero(presence_provider)) {
		/* No presence string in the hint */
		return 0;
	}

	return ast_presence_state(presence_provider, subtype, message);
}

int ast_hint_presence_state(struct ast_channel *c, const char *context, const char *exten, char **subtype, char **message)
{
	struct ast_exten *e;

	if (!(e = ast_hint_extension(c, context, exten))) {  /* Do we have a hint for this extension ? */
		return -1;                   /* No hint, return -1 */
	}

	if (e->exten[0] == '_') {
		/* Create this hint on-the-fly */
		ast_add_extension(e->parent->name, 0, exten, e->priority, e->label,
			e->matchcid ? e->cidmatch : NULL, e->app, ast_strdup(e->data), ast_free_ptr,
			e->registrar);
		if (!(e = ast_hint_extension(c, context, exten))) {
			/* Improbable, but not impossible */
			return -1;
		}
	}

	return extension_presence_state_helper(e, subtype, message);
}

static int execute_state_callback(ast_state_cb_type cb,
	const char *context,
	const char *exten,
	void *data,
	enum ast_state_cb_update_reason reason,
	struct ast_hint *hint,
	struct ao2_container *device_state_info)
{
	int res = 0;
	struct ast_state_cb_info info = { 0, };

	info.reason = reason;

	/* Copy over current hint data */
	if (hint) {
		ao2_lock(hint);
		info.exten_state = hint->laststate;
		info.device_state_info = device_state_info;
		info.presence_state = hint->last_presence_state;
		if (!(ast_strlen_zero(hint->last_presence_subtype))) {
			info.presence_subtype = ast_strdupa(hint->last_presence_subtype);
		} else {
			info.presence_subtype = "";
		}
		if (!(ast_strlen_zero(hint->last_presence_message))) {
			info.presence_message = ast_strdupa(hint->last_presence_message);
		} else {
			info.presence_message = "";
		}
		ao2_unlock(hint);
	} else {
		info.exten_state = AST_EXTENSION_REMOVED;
	}

	/* NOTE: The casts will not be needed for v10 and later */
	res = cb((char *) context, (char *) exten, &info, data);

	return res;
}

static int handle_presencechange(void *datap)
{
	struct ast_hint *hint;
	struct ast_str *hint_app = NULL;
	struct presencechange *pc = datap;
	struct ao2_iterator i;
	struct ao2_iterator cb_iter;
	char context_name[AST_MAX_CONTEXT];
	char exten_name[AST_MAX_EXTENSION];
	int res = -1;

	hint_app = ast_str_create(1024);
	if (!hint_app) {
		goto presencechange_cleanup;
	}

	ast_mutex_lock(&context_merge_lock);/* Hold off ast_merge_contexts_and_delete */
	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		struct ast_state_cb *state_cb;
		const char *app;
		char *parse;

		ao2_lock(hint);

		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}

		/* Does this hint monitor the device that changed state? */
		app = ast_get_extension_app(hint->exten);
		if (ast_strlen_zero(app)) {
			/* The hint does not monitor presence at all. */
			ao2_unlock(hint);
			continue;
		}

		ast_str_set(&hint_app, 0, "%s", app);
		parse = parse_hint_presence(hint_app);
		if (ast_strlen_zero(parse)) {
			ao2_unlock(hint);
			continue;
		}
		if (strcasecmp(parse, pc->provider)) {
			/* The hint does not monitor the presence provider. */
			ao2_unlock(hint);
			continue;
		}

		/*
		 * Save off strings in case the hint extension gets destroyed
		 * while we are notifying the watchers.
		 */
		ast_copy_string(context_name,
			ast_get_context_name(ast_get_extension_context(hint->exten)),
			sizeof(context_name));
		ast_copy_string(exten_name, ast_get_extension_name(hint->exten),
			sizeof(exten_name));
		ast_str_set(&hint_app, 0, "%s", ast_get_extension_app(hint->exten));

		/* Check to see if update is necessary */
		if ((hint->last_presence_state == pc->state) &&
			((hint->last_presence_subtype && pc->subtype && !strcmp(hint->last_presence_subtype, pc->subtype)) || (!hint->last_presence_subtype && !pc->subtype)) &&
			((hint->last_presence_message && pc->message && !strcmp(hint->last_presence_message, pc->message)) || (!hint->last_presence_message && !pc->message))) {

			/* this update is the same as the last, do nothing */
			ao2_unlock(hint);
			continue;
		}

		/* update new values */
		ast_free(hint->last_presence_subtype);
		ast_free(hint->last_presence_message);
		hint->last_presence_state = pc->state;
		hint->last_presence_subtype = pc->subtype ? ast_strdup(pc->subtype) : NULL;
		hint->last_presence_message = pc->message ? ast_strdup(pc->message) : NULL;

		ao2_unlock(hint);

		/* For general callbacks */
		cb_iter = ao2_iterator_init(statecbs, 0);
		for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			execute_state_callback(state_cb->change_cb,
				context_name,
				exten_name,
				state_cb->data,
				AST_HINT_UPDATE_PRESENCE,
				hint,
				NULL);
		}
		ao2_iterator_destroy(&cb_iter);

		/* For extension callbacks */
		cb_iter = ao2_iterator_init(hint->callbacks, 0);
		for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			execute_state_callback(state_cb->change_cb,
				context_name,
				exten_name,
				state_cb->data,
				AST_HINT_UPDATE_PRESENCE,
				hint,
				NULL);
		}
		ao2_iterator_destroy(&cb_iter);
	}
	ao2_iterator_destroy(&i);
	ast_mutex_unlock(&context_merge_lock);

	res = 0;

presencechange_cleanup:
	ast_free(hint_app);
	ao2_ref(pc, -1);

	return res;
}

/*!
 * /internal
 * /brief Identify a channel for every device which is supposedly responsible for the device state.
 *
 * Especially when the device is ringing, the oldest ringing channel is chosen.
 * For all other cases the first encountered channel in the specific state is chosen.
 */
static void get_device_state_causing_channels(struct ao2_container *c)
{
	struct ao2_iterator iter;
	struct ast_device_state_info *info;
	struct ast_channel *chan;

	if (!c || !ao2_container_count(c)) {
		return;
	}
	iter = ao2_iterator_init(c, 0);
	for (; (info = ao2_iterator_next(&iter)); ao2_ref(info, -1)) {
		enum ast_channel_state search_state = 0; /* prevent false uninit warning */
		char match[AST_CHANNEL_NAME];
		struct ast_channel_iterator *chan_iter;
		struct timeval chantime = {0, }; /* prevent false uninit warning */

		switch (info->device_state) {
		case AST_DEVICE_RINGING:
		case AST_DEVICE_RINGINUSE:
			/* find ringing channel */
			search_state = AST_STATE_RINGING;
			break;
		case AST_DEVICE_BUSY:
			/* find busy channel */
			search_state = AST_STATE_BUSY;
			break;
		case AST_DEVICE_ONHOLD:
		case AST_DEVICE_INUSE:
			/* find up channel */
			search_state = AST_STATE_UP;
			break;
		case AST_DEVICE_UNKNOWN:
		case AST_DEVICE_NOT_INUSE:
		case AST_DEVICE_INVALID:
		case AST_DEVICE_UNAVAILABLE:
		case AST_DEVICE_TOTAL /* not a state */:
			/* no channels are of interest */
			continue;
		}

		/* iterate over all channels of the device */
	        snprintf(match, sizeof(match), "%s-", info->device_name);
		chan_iter = ast_channel_iterator_by_name_new(match, strlen(match));
		for (; (chan = ast_channel_iterator_next(chan_iter)); ast_channel_unref(chan)) {
			ast_channel_lock(chan);
			/* this channel's state doesn't match */
			if (search_state != ast_channel_state(chan)) {
				ast_channel_unlock(chan);
				continue;
			}
			/* any non-ringing channel will fit */
			if (search_state != AST_STATE_RINGING) {
				ast_channel_unlock(chan);
				info->causing_channel = chan; /* is kept ref'd! */
				break;
			}
			/* but we need the oldest ringing channel of the device to match with undirected pickup */
			if (!info->causing_channel) {
				chantime = ast_channel_creationtime(chan);
				ast_channel_ref(chan); /* must ref it! */
				info->causing_channel = chan;
			} else if (ast_tvcmp(ast_channel_creationtime(chan), chantime) < 0) {
				chantime = ast_channel_creationtime(chan);
				ast_channel_unref(info->causing_channel);
				ast_channel_ref(chan); /* must ref it! */
				info->causing_channel = chan;
			}
			ast_channel_unlock(chan);
		}
		ast_channel_iterator_destroy(chan_iter);
	}
	ao2_iterator_destroy(&iter);
}

static int handle_statechange(void *datap)
{
	struct ast_hint *hint;
	struct ast_str *hint_app;
	struct ast_hintdevice *device;
	struct ast_hintdevice *cmpdevice;
	struct statechange *sc = datap;
	struct ao2_iterator *dev_iter;
	struct ao2_iterator cb_iter;
	char context_name[AST_MAX_CONTEXT];
	char exten_name[AST_MAX_EXTENSION];

	if (ao2_container_count(hintdevices) == 0) {
		/* There are no hints monitoring devices. */
		ast_free(sc);
		return 0;
	}

	hint_app = ast_str_create(1024);
	if (!hint_app) {
		ast_free(sc);
		return -1;
	}

	cmpdevice = ast_alloca(sizeof(*cmpdevice) + strlen(sc->dev));
	strcpy(cmpdevice->hintdevice, sc->dev);

	ast_mutex_lock(&context_merge_lock);/* Hold off ast_merge_contexts_and_delete */
	dev_iter = ao2_t_callback(hintdevices,
		OBJ_POINTER | OBJ_MULTIPLE,
		hintdevice_cmp_multiple,
		cmpdevice,
		"find devices in container");
	if (!dev_iter) {
		ast_mutex_unlock(&context_merge_lock);
		ast_free(hint_app);
		ast_free(sc);
		return -1;
	}

	for (; (device = ao2_iterator_next(dev_iter)); ao2_t_ref(device, -1, "Next device")) {
		struct ast_state_cb *state_cb;
		int state;
		int same_state;
		struct ao2_container *device_state_info;
		int first_extended_cb_call = 1;

		if (!device->hint) {
			/* Should never happen. */
			continue;
		}
		hint = device->hint;

		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}

		/*
		 * Save off strings in case the hint extension gets destroyed
		 * while we are notifying the watchers.
		 */
		ast_copy_string(context_name,
			ast_get_context_name(ast_get_extension_context(hint->exten)),
			sizeof(context_name));
		ast_copy_string(exten_name, ast_get_extension_name(hint->exten),
			sizeof(exten_name));
		ast_str_set(&hint_app, 0, "%s", ast_get_extension_app(hint->exten));
		ao2_unlock(hint);

		/*
		 * Get device state for this hint.
		 *
		 * NOTE: We cannot hold any locks while determining the hint
		 * device state or notifying the watchers without causing a
		 * deadlock.  (conlock, hints, and hint)
		 */
		/* Make a container so state3 can fill it if we wish.
		 * If that failed we simply do not provide the extended state info.
		 */
		device_state_info = alloc_device_state_info();
		state = ast_extension_state3(hint_app, device_state_info);
		if ((same_state = state == hint->laststate) && (~state & AST_EXTENSION_RINGING)) {
			ao2_cleanup(device_state_info);
			continue;
		}

		/* Device state changed since last check - notify the watchers. */
		hint->laststate = state;	/* record we saw the change */

		/* For general callbacks */
		cb_iter = ao2_iterator_init(statecbs, 0);
		for (; !same_state && (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			execute_state_callback(state_cb->change_cb,
				context_name,
				exten_name,
				state_cb->data,
				AST_HINT_UPDATE_DEVICE,
				hint,
				NULL);
		}
		ao2_iterator_destroy(&cb_iter);

		/* For extension callbacks */
		/* extended callbacks are called when the state changed or when AST_EVENT_RINGING is
		 * included. Normal callbacks are only called when the state changed.
		 */
		cb_iter = ao2_iterator_init(hint->callbacks, 0);
		for (; (state_cb = ao2_iterator_next(&cb_iter)); ao2_ref(state_cb, -1)) {
			if (state_cb->extended && first_extended_cb_call) {
				/* Fill detailed device_state_info now that we know it is used by extd. callback */
				first_extended_cb_call = 0;
				get_device_state_causing_channels(device_state_info);
			}
			if (state_cb->extended || !same_state) {
				execute_state_callback(state_cb->change_cb,
					context_name,
					exten_name,
					state_cb->data,
					AST_HINT_UPDATE_DEVICE,
					hint,
					state_cb->extended ? device_state_info : NULL);
			}
		}
		ao2_iterator_destroy(&cb_iter);

		ao2_cleanup(device_state_info);
	}
	ast_mutex_unlock(&context_merge_lock);

	ao2_iterator_destroy(dev_iter);
	ast_free(hint_app);
	ast_free(sc);
	return 0;
}

/*!
 * \internal
 * \brief Destroy the given state callback object.
 *
 * \param doomed State callback to destroy.
 *
 * \return Nothing
 */
static void destroy_state_cb(void *doomed)
{
	struct ast_state_cb *state_cb = doomed;

	if (state_cb->destroy_cb) {
		state_cb->destroy_cb(state_cb->id, state_cb->data);
	}
}

/*! \internal \brief Add watcher for extension states with destructor */
static int extension_state_add_destroy(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data, int extended)
{
	struct ast_hint *hint;
	struct ast_state_cb *state_cb;
	struct ast_exten *e;
	int id;

	/* If there's no context and extension:  add callback to statecbs list */
	if (!context && !exten) {
		/* Prevent multiple adds from adding the same change_cb at the same time. */
		ao2_lock(statecbs);

		/* Remove any existing change_cb. */
		ao2_find(statecbs, change_cb, OBJ_UNLINK | OBJ_NODATA);

		/* Now insert the change_cb */
		if (!(state_cb = ao2_alloc(sizeof(*state_cb), destroy_state_cb))) {
			ao2_unlock(statecbs);
			return -1;
		}
		state_cb->id = 0;
		state_cb->change_cb = change_cb;
		state_cb->destroy_cb = destroy_cb;
		state_cb->data = data;
		state_cb->extended = extended;
		ao2_link(statecbs, state_cb);

		ao2_ref(state_cb, -1);
		ao2_unlock(statecbs);
		return 0;
	}

	if (!context || !exten)
		return -1;

	/* This callback type is for only one hint, so get the hint */
	e = ast_hint_extension(NULL, context, exten);
	if (!e) {
		return -1;
	}

	/* If this is a pattern, dynamically create a new extension for this
	 * particular match.  Note that this will only happen once for each
	 * individual extension, because the pattern will no longer match first.
	 */
	if (e->exten[0] == '_') {
		ast_add_extension(e->parent->name, 0, exten, e->priority, e->label,
			e->matchcid ? e->cidmatch : NULL, e->app, ast_strdup(e->data), ast_free_ptr,
			e->registrar);
		e = ast_hint_extension(NULL, context, exten);
		if (!e || e->exten[0] == '_') {
			return -1;
		}
	}

	/* Find the hint in the hints container */
	ao2_lock(hints);/* Locked to hold off ast_merge_contexts_and_delete */
	hint = ao2_find(hints, e, 0);
	if (!hint) {
		ao2_unlock(hints);
		return -1;
	}

	/* Now insert the callback in the callback list  */
	if (!(state_cb = ao2_alloc(sizeof(*state_cb), destroy_state_cb))) {
		ao2_ref(hint, -1);
		ao2_unlock(hints);
		return -1;
	}
	do {
		id = stateid++;		/* Unique ID for this callback */
		/* Do not allow id to ever be -1 or 0. */
	} while (id == -1 || id == 0);
	state_cb->id = id;
	state_cb->change_cb = change_cb;	/* Pointer to callback routine */
	state_cb->destroy_cb = destroy_cb;
	state_cb->data = data;		/* Data for the callback */
	state_cb->extended = extended;
	ao2_link(hint->callbacks, state_cb);

	ao2_ref(state_cb, -1);
	ao2_ref(hint, -1);
	ao2_unlock(hints);

	return id;
}

/*! \brief Add watcher for extension states with destructor */
int ast_extension_state_add_destroy(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, destroy_cb, data, 0);
}

/*! \brief Add watcher for extension states */
int ast_extension_state_add(const char *context, const char *exten,
	ast_state_cb_type change_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, NULL, data, 0);
}

/*! \brief Add watcher for extended extension states with destructor */
int ast_extension_state_add_destroy_extended(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, destroy_cb, data, 1);
}

/*! \brief Add watcher for extended extension states */
int ast_extension_state_add_extended(const char *context, const char *exten,
	ast_state_cb_type change_cb, void *data)
{
	return extension_state_add_destroy(context, exten, change_cb, NULL, data, 1);
}

/*! \brief Find Hint by callback id */
static int find_hint_by_cb_id(void *obj, void *arg, int flags)
{
	struct ast_state_cb *state_cb;
	const struct ast_hint *hint = obj;
	int *id = arg;

	if ((state_cb = ao2_find(hint->callbacks, id, 0))) {
		ao2_ref(state_cb, -1);
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

/*! \brief  ast_extension_state_del: Remove a watcher from the callback list */
int ast_extension_state_del(int id, ast_state_cb_type change_cb)
{
	struct ast_state_cb *p_cur;
	int ret = -1;

	if (!id) {	/* id == 0 is a callback without extension */
		if (!change_cb) {
			return ret;
		}
		p_cur = ao2_find(statecbs, change_cb, OBJ_UNLINK);
		if (p_cur) {
			ret = 0;
			ao2_ref(p_cur, -1);
		}
	} else { /* callback with extension, find the callback based on ID */
		struct ast_hint *hint;

		ao2_lock(hints);/* Locked to hold off ast_merge_contexts_and_delete */
		hint = ao2_callback(hints, 0, find_hint_by_cb_id, &id);
		if (hint) {
			p_cur = ao2_find(hint->callbacks, &id, OBJ_UNLINK);
			if (p_cur) {
				ret = 0;
				ao2_ref(p_cur, -1);
			}
			ao2_ref(hint, -1);
		}
		ao2_unlock(hints);
	}

	return ret;
}

static int hint_id_cmp(void *obj, void *arg, int flags)
{
	const struct ast_state_cb *cb = obj;
	int *id = arg;

	return (cb->id == *id) ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \internal
 * \brief Destroy the given hint object.
 *
 * \param obj Hint to destroy.
 *
 * \return Nothing
 */
static void destroy_hint(void *obj)
{
	struct ast_hint *hint = obj;

	if (hint->callbacks) {
		struct ast_state_cb *state_cb;
		const char *context_name;
		const char *exten_name;

		if (hint->exten) {
			context_name = ast_get_context_name(ast_get_extension_context(hint->exten));
			exten_name = ast_get_extension_name(hint->exten);
			hint->exten = NULL;
		} else {
			/* The extension has already been destroyed */
			context_name = hint->context_name;
			exten_name = hint->exten_name;
		}
		hint->laststate = AST_EXTENSION_DEACTIVATED;
		while ((state_cb = ao2_callback(hint->callbacks, OBJ_UNLINK, NULL, NULL))) {
			/* Notify with -1 and remove all callbacks */
			execute_state_callback(state_cb->change_cb,
				context_name,
				exten_name,
				state_cb->data,
				AST_HINT_UPDATE_DEVICE,
				hint,
				NULL);
			ao2_ref(state_cb, -1);
		}
		ao2_ref(hint->callbacks, -1);
	}
	ast_free(hint->last_presence_subtype);
	ast_free(hint->last_presence_message);
}

/*! \brief Remove hint from extension */
static int ast_remove_hint(struct ast_exten *e)
{
	/* Cleanup the Notifys if hint is removed */
	struct ast_hint *hint;

	if (!e) {
		return -1;
	}

	hint = ao2_find(hints, e, OBJ_UNLINK);
	if (!hint) {
		return -1;
	}

	remove_hintdevice(hint);

	/*
	 * The extension is being destroyed so we must save some
	 * information to notify that the extension is deactivated.
	 */
	ao2_lock(hint);
	ast_copy_string(hint->context_name,
		ast_get_context_name(ast_get_extension_context(hint->exten)),
		sizeof(hint->context_name));
	ast_copy_string(hint->exten_name, ast_get_extension_name(hint->exten),
		sizeof(hint->exten_name));
	hint->exten = NULL;
	ao2_unlock(hint);

	ao2_ref(hint, -1);

	return 0;
}

/*! \brief Add hint to hint list, check initial extension state */
static int ast_add_hint(struct ast_exten *e)
{
	struct ast_hint *hint_new;
	struct ast_hint *hint_found;
	char *message = NULL;
	char *subtype = NULL;
	int presence_state;

	if (!e) {
		return -1;
	}

	/*
	 * We must create the hint we wish to add before determining if
	 * it is already in the hints container to avoid possible
	 * deadlock when getting the current extension state.
	 */
	hint_new = ao2_alloc(sizeof(*hint_new), destroy_hint);
	if (!hint_new) {
		return -1;
	}

	/* Initialize new hint. */
	hint_new->callbacks = ao2_container_alloc(1, NULL, hint_id_cmp);
	if (!hint_new->callbacks) {
		ao2_ref(hint_new, -1);
		return -1;
	}
	hint_new->exten = e;
	if (strstr(e->app, "${") && e->exten[0] == '_') {
		/* The hint is dynamic and hasn't been evaluted yet */
		hint_new->laststate = AST_DEVICE_INVALID;
		hint_new->last_presence_state = AST_PRESENCE_INVALID;
	} else {
		hint_new->laststate = ast_extension_state2(e, NULL);
		if ((presence_state = extension_presence_state_helper(e, &subtype, &message)) > 0) {
			hint_new->last_presence_state = presence_state;
			hint_new->last_presence_subtype = subtype;
			hint_new->last_presence_message = message;
			message = subtype = NULL;
		}
	}

	/* Prevent multiple add hints from adding the same hint at the same time. */
	ao2_lock(hints);

	/* Search if hint exists, do nothing */
	hint_found = ao2_find(hints, e, 0);
	if (hint_found) {
		ao2_ref(hint_found, -1);
		ao2_unlock(hints);
		ao2_ref(hint_new, -1);
		ast_debug(2, "HINTS: Not re-adding existing hint %s: %s\n",
			ast_get_extension_name(e), ast_get_extension_app(e));
		return -1;
	}

	/* Add new hint to the hints container */
	ast_debug(2, "HINTS: Adding hint %s: %s\n",
		ast_get_extension_name(e), ast_get_extension_app(e));
	ao2_link(hints, hint_new);
	if (add_hintdevice(hint_new, ast_get_extension_app(e))) {
		ast_log(LOG_WARNING, "Could not add devices for hint: %s@%s.\n",
			ast_get_extension_name(e),
			ast_get_context_name(ast_get_extension_context(e)));
	}

	ao2_unlock(hints);
	ao2_ref(hint_new, -1);

	return 0;
}

/*! \brief Change hint for an extension */
static int ast_change_hint(struct ast_exten *oe, struct ast_exten *ne)
{
	struct ast_hint *hint;

	if (!oe || !ne) {
		return -1;
	}

	ao2_lock(hints);/* Locked to hold off others while we move the hint around. */

	/*
	 * Unlink the hint from the hints container as the extension
	 * name (which is the hash value) could change.
	 */
	hint = ao2_find(hints, oe, OBJ_UNLINK);
	if (!hint) {
		ao2_unlock(hints);
		return -1;
	}

	remove_hintdevice(hint);

	/* Update the hint and put it back in the hints container. */
	ao2_lock(hint);
	hint->exten = ne;
	ao2_unlock(hint);
	ao2_link(hints, hint);
	if (add_hintdevice(hint, ast_get_extension_app(ne))) {
		ast_log(LOG_WARNING, "Could not add devices for hint: %s@%s.\n",
			ast_get_extension_name(ne),
			ast_get_context_name(ast_get_extension_context(ne)));
	}

	ao2_unlock(hints);
	ao2_ref(hint, -1);

	return 0;
}


/*! \brief Get hint for channel */
int ast_get_hint(char *hint, int hintsize, char *name, int namesize, struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e = ast_hint_extension(c, context, exten);

	if (e) {
		if (hint)
			ast_copy_string(hint, ast_get_extension_app(e), hintsize);
		if (name) {
			const char *tmp = ast_get_extension_app_data(e);
			if (tmp)
				ast_copy_string(name, tmp, namesize);
		}
		return -1;
	}
	return 0;
}

/*! \brief Get hint for channel */
int ast_str_get_hint(struct ast_str **hint, ssize_t hintsize, struct ast_str **name, ssize_t namesize, struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_exten *e = ast_hint_extension(c, context, exten);

	if (!e) {
		return 0;
	}

	if (hint) {
		ast_str_set(hint, hintsize, "%s", ast_get_extension_app(e));
	}
	if (name) {
		const char *tmp = ast_get_extension_app_data(e);
		if (tmp) {
			ast_str_set(name, namesize, "%s", tmp);
		}
	}
	return -1;
}

int ast_exists_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_MATCH, 0, 0);
}

int ast_findlabel_extension(struct ast_channel *c, const char *context, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, 0, label, callerid, E_FINDLABEL, 0, 0);
}

int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, con, NULL, exten, 0, label, callerid, E_FINDLABEL, 0, 0);
}

int ast_canmatch_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_CANMATCH, 0, 0);
}

int ast_matchmore_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_MATCHMORE, 0, 0);
}

int ast_spawn_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid, int *found, int combined_find_spawn)
{
	return pbx_extension_helper(c, NULL, context, exten, priority, NULL, callerid, E_SPAWN, found, combined_find_spawn);
}

void ast_pbx_h_exten_run(struct ast_channel *chan, const char *context)
{
	int autoloopflag;
	int found;
	int spawn_error;

	ast_channel_lock(chan);

	if (ast_channel_cdr(chan) && ast_opt_end_cdr_before_h_exten) {
		ast_cdr_end(ast_channel_cdr(chan));
	}

	/* Set h exten location */
	if (context != ast_channel_context(chan)) {
		ast_channel_context_set(chan, context);
	}
	ast_channel_exten_set(chan, "h");
	ast_channel_priority_set(chan, 1);

	/*
	 * Make sure that the channel is marked as hungup since we are
	 * going to run the h exten on it.
	 */
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_APPUNLOAD);

	/* Save autoloop flag */
	autoloopflag = ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);
	ast_channel_unlock(chan);

	for (;;) {
		spawn_error = ast_spawn_extension(chan, ast_channel_context(chan),
			ast_channel_exten(chan), ast_channel_priority(chan),
			S_COR(ast_channel_caller(chan)->id.number.valid,
				ast_channel_caller(chan)->id.number.str, NULL), &found, 1);

		ast_channel_lock(chan);
		if (spawn_error) {
			/* The code after the loop needs the channel locked. */
			break;
		}
		ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);
		ast_channel_unlock(chan);
	}
	if (found && spawn_error) {
		/* Something bad happened, or a hangup has been requested. */
		ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n",
			ast_channel_context(chan), ast_channel_exten(chan),
			ast_channel_priority(chan), ast_channel_name(chan));
		ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n",
			ast_channel_context(chan), ast_channel_exten(chan),
			ast_channel_priority(chan), ast_channel_name(chan));
	}

	/* An "h" exten has been run, so indicate that one has been run. */
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_HANGUP_RUN);

	/* Restore autoloop flag */
	ast_set2_flag(ast_channel_flags(chan), autoloopflag, AST_FLAG_IN_AUTOLOOP);
	ast_channel_unlock(chan);
}

int ast_pbx_hangup_handler_run(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;

	ast_channel_lock(chan);
	handlers = ast_channel_hangup_handlers(chan);
	if (AST_LIST_EMPTY(handlers)) {
		ast_channel_unlock(chan);
		return 0;
	}

	if (ast_channel_cdr(chan) && ast_opt_end_cdr_before_h_exten) {
		ast_cdr_end(ast_channel_cdr(chan));
	}

	/*
	 * Make sure that the channel is marked as hungup since we are
	 * going to run the hangup handlers on it.
	 */
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_APPUNLOAD);

	for (;;) {
		handlers = ast_channel_hangup_handlers(chan);
		h_handler = AST_LIST_REMOVE_HEAD(handlers, node);
		if (!h_handler) {
			break;
		}

		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when a hangup handler is about to be called.</synopsis>
				<syntax>
					<parameter name="Handler">
						<para>Hangup handler parameter string passed to the Gosub application.</para>
					</parameter>
				</syntax>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_DIALPLAN, "HangupHandlerRun",
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Handler: %s\r\n",
			ast_channel_name(chan),
			ast_channel_uniqueid(chan),
			h_handler->args);
		ast_channel_unlock(chan);

		ast_app_exec_sub(NULL, chan, h_handler->args, 1);
		ast_free(h_handler);

		ast_channel_lock(chan);
	}
	ast_channel_unlock(chan);
	return 1;
}

void ast_pbx_hangup_handler_init(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;

	handlers = ast_channel_hangup_handlers(chan);
	AST_LIST_HEAD_INIT_NOLOCK(handlers);
}

void ast_pbx_hangup_handler_destroy(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;

	ast_channel_lock(chan);

	/* Get rid of each of the hangup handlers on the channel */
	handlers = ast_channel_hangup_handlers(chan);
	while ((h_handler = AST_LIST_REMOVE_HEAD(handlers, node))) {
		ast_free(h_handler);
	}

	ast_channel_unlock(chan);
}

int ast_pbx_hangup_handler_pop(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;

	ast_channel_lock(chan);
	handlers = ast_channel_hangup_handlers(chan);
	h_handler = AST_LIST_REMOVE_HEAD(handlers, node);
	if (h_handler) {
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>
					Raised when a hangup handler is removed from the handler
					stack by the CHANNEL() function.
				</synopsis>
				<syntax>
					<parameter name="Handler">
						<para>Hangup handler parameter string passed to the Gosub application.</para>
					</parameter>
				</syntax>
				<see-also>
					<ref type="managerEvent">HangupHandlerPush</ref>
					<ref type="function">CHANNEL</ref>
				</see-also>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_DIALPLAN, "HangupHandlerPop",
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Handler: %s\r\n",
			ast_channel_name(chan),
			ast_channel_uniqueid(chan),
			h_handler->args);
	}
	ast_channel_unlock(chan);
	if (h_handler) {
		ast_free(h_handler);
		return 1;
	}
	return 0;
}

void ast_pbx_hangup_handler_push(struct ast_channel *chan, const char *handler)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;
	const char *expanded_handler;

	if (ast_strlen_zero(handler)) {
		return;
	}

	expanded_handler = ast_app_expand_sub_args(chan, handler);
	if (!expanded_handler) {
		return;
	}
	h_handler = ast_malloc(sizeof(*h_handler) + 1 + strlen(expanded_handler));
	if (!h_handler) {
		ast_free((char *) expanded_handler);
		return;
	}
	strcpy(h_handler->args, expanded_handler);/* Safe */
	ast_free((char *) expanded_handler);

	ast_channel_lock(chan);

	handlers = ast_channel_hangup_handlers(chan);
	AST_LIST_INSERT_HEAD(handlers, h_handler, node);

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>
				Raised when a hangup handler is added to the handler
				stack by the CHANNEL() function.
			</synopsis>
			<syntax>
				<parameter name="Handler">
					<para>Hangup handler parameter string passed to the Gosub application.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">HangupHandlerPop</ref>
				<ref type="function">CHANNEL</ref>
			</see-also>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_DIALPLAN, "HangupHandlerPush",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Handler: %s\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		h_handler->args);

	ast_channel_unlock(chan);
}

#define HANDLER_FORMAT	"%-30s %s\n"

/*!
 * \internal
 * \brief CLI output the hangup handler headers.
 * \since 11.0
 *
 * \param fd CLI file descriptor to use.
 *
 * \return Nothing
 */
static void ast_pbx_hangup_handler_headers(int fd)
{
	ast_cli(fd, HANDLER_FORMAT, "Channel", "Handler");
}

/*!
 * \internal
 * \brief CLI output the channel hangup handlers.
 * \since 11.0
 *
 * \param fd CLI file descriptor to use.
 * \param chan Channel to show hangup handlers.
 *
 * \return Nothing
 */
static void ast_pbx_hangup_handler_show(int fd, struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;
	int first = 1;

	ast_channel_lock(chan);
	handlers = ast_channel_hangup_handlers(chan);
	AST_LIST_TRAVERSE(handlers, h_handler, node) {
		ast_cli(fd, HANDLER_FORMAT, first ? ast_channel_name(chan) : "", h_handler->args);
		first = 0;
	}
	ast_channel_unlock(chan);
}

/*
 * \brief 'show hanguphandlers <channel>' CLI command implementation function...
 */
static char *handle_show_hangup_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hanguphandlers";
		e->usage =
			"Usage: core show hanguphandlers <channel>\n"
			"       Show hangup handlers of a specified channel.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, e->args);
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	chan = ast_channel_get_by_name(a->argv[3]);
	if (!chan) {
		ast_cli(a->fd, "Channel does not exist.\n");
		return CLI_FAILURE;
	}

	ast_pbx_hangup_handler_headers(a->fd);
	ast_pbx_hangup_handler_show(a->fd, chan);

	ast_channel_unref(chan);

	return CLI_SUCCESS;
}

/*
 * \brief 'show hanguphandlers all' CLI command implementation function...
 */
static char *handle_show_hangup_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel_iterator *iter;
	struct ast_channel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hanguphandlers all";
		e->usage =
			"Usage: core show hanguphandlers all\n"
			"       Show hangup handlers for all channels.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, e->args);
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	iter = ast_channel_iterator_all_new();
	if (!iter) {
		return CLI_FAILURE;
	}

	ast_pbx_hangup_handler_headers(a->fd);
	for (; (chan = ast_channel_iterator_next(iter)); ast_channel_unref(chan)) {
		ast_pbx_hangup_handler_show(a->fd, chan);
	}
	ast_channel_iterator_destroy(iter);

	return CLI_SUCCESS;
}

/*! helper function to set extension and priority */
static void set_ext_pri(struct ast_channel *c, const char *exten, int pri)
{
	ast_channel_lock(c);
	ast_channel_exten_set(c, exten);
	ast_channel_priority_set(c, pri);
	ast_channel_unlock(c);
}

/*!
 * \brief collect digits from the channel into the buffer.
 * \param c, buf, buflen, pos
 * \param waittime is in milliseconds
 * \retval 0 on timeout or done.
 * \retval -1 on error.
*/
static int collect_digits(struct ast_channel *c, int waittime, char *buf, int buflen, int pos)
{
	int digit;

	buf[pos] = '\0';	/* make sure it is properly terminated */
	while (ast_matchmore_extension(c, ast_channel_context(c), buf, 1,
		S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
		/* As long as we're willing to wait, and as long as it's not defined,
		   keep reading digits until we can't possibly get a right answer anymore.  */
		digit = ast_waitfordigit(c, waittime);
		if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_ASYNCGOTO) {
			ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
		} else {
			if (!digit)	/* No entry */
				break;
			if (digit < 0)	/* Error, maybe a  hangup */
				return -1;
			if (pos < buflen - 1) {	/* XXX maybe error otherwise ? */
				buf[pos++] = digit;
				buf[pos] = '\0';
			}
			waittime = ast_channel_pbx(c)->dtimeoutms;
		}
	}
	return 0;
}

static enum ast_pbx_result __ast_pbx_run(struct ast_channel *c,
		struct ast_pbx_args *args)
{
	int found = 0;	/* set if we find at least one match */
	int res = 0;
	int autoloopflag;
	int error = 0;		/* set an error conditions */
	struct ast_pbx *pbx;
	struct ast_callid *callid;

	/* A little initial setup here */
	if (ast_channel_pbx(c)) {
		ast_log(LOG_WARNING, "%s already has PBX structure??\n", ast_channel_name(c));
		/* XXX and now what ? */
		ast_free(ast_channel_pbx(c));
	}
	if (!(pbx = ast_calloc(1, sizeof(*pbx)))) {
		return -1;
	}

	callid = ast_read_threadstorage_callid();
	/* If the thread isn't already associated with a callid, we should create that association. */
	if (!callid) {
		/* Associate new PBX thread with the channel call id if it is availble.
		 * If not, create a new one instead.
		 */
		callid = ast_channel_callid(c);
		if (!callid) {
			callid = ast_create_callid();
			if (callid) {
				ast_channel_callid_set(c, callid);
			}
		}
		ast_callid_threadassoc_add(callid);
		callid = ast_callid_unref(callid);
	} else {
		/* Nothing to do here, The thread is already bound to a callid.  Let's just get rid of the reference. */
		ast_callid_unref(callid);
	}

	ast_channel_pbx_set(c, pbx);
	/* Set reasonable defaults */
	ast_channel_pbx(c)->rtimeoutms = 10000;
	ast_channel_pbx(c)->dtimeoutms = 5000;

	autoloopflag = ast_test_flag(ast_channel_flags(c), AST_FLAG_IN_AUTOLOOP);	/* save value to restore at the end */
	ast_set_flag(ast_channel_flags(c), AST_FLAG_IN_AUTOLOOP);

	if (ast_strlen_zero(ast_channel_exten(c))) {
		/* If not successful fall back to 's' - but only if there is no given exten  */
		ast_verb(2, "Starting %s at %s,%s,%d failed so falling back to exten 's'\n", ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c));
		/* XXX the original code used the existing priority in the call to
		 * ast_exists_extension(), and reset it to 1 afterwards.
		 * I believe the correct thing is to set it to 1 immediately.
		*/
		set_ext_pri(c, "s", 1);
	}

	ast_channel_lock(c);
	if (ast_channel_cdr(c)) {
		/* allow CDR variables that have been collected after channel was created to be visible during call */
		ast_cdr_update(c);
	}
	ast_channel_unlock(c);
	for (;;) {
		char dst_exten[256];	/* buffer to accumulate digits */
		int pos = 0;		/* XXX should check bounds */
		int digit = 0;
		int invalid = 0;
		int timeout = 0;

		/* No digits pressed yet */
		dst_exten[pos] = '\0';

		/* loop on priorities in this context/exten */
		while (!(res = ast_spawn_extension(c, ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c),
			S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL),
			&found, 1))) {
			if (!ast_check_hangup(c)) {
				ast_channel_priority_set(c, ast_channel_priority(c) + 1);
				continue;
			}

			/* Check softhangup flags. */
			if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_ASYNCGOTO) {
				ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
				continue;
			}
			if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_TIMEOUT) {
				if (ast_exists_extension(c, ast_channel_context(c), "T", 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					set_ext_pri(c, "T", 1);
					/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
					memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
					continue;
				} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					raise_exception(c, "ABSOLUTETIMEOUT", 1);
					/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
					memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
					continue;
				}

				/* Call timed out with no special extension to jump to. */
				error = 1;
				break;
			}
			ast_debug(1, "Extension %s, priority %d returned normally even though call was hung up\n",
				ast_channel_exten(c), ast_channel_priority(c));
			error = 1;
			break;
		} /* end while  - from here on we can use 'break' to go out */
		if (found && res) {
			/* Something bad happened, or a hangup has been requested. */
			if (strchr("0123456789ABCDEF*#", res)) {
				ast_debug(1, "Oooh, got something to jump out with ('%c')!\n", res);
				pos = 0;
				dst_exten[pos++] = digit = res;
				dst_exten[pos] = '\0';
			} else if (res == AST_PBX_INCOMPLETE) {
				ast_debug(1, "Spawn extension (%s,%s,%d) exited INCOMPLETE on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
				ast_verb(2, "Spawn extension (%s, %s, %d) exited INCOMPLETE on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));

				/* Don't cycle on incomplete - this will happen if the only extension that matches is our "incomplete" extension */
				if (!ast_matchmore_extension(c, ast_channel_context(c), ast_channel_exten(c), 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					invalid = 1;
				} else {
					ast_copy_string(dst_exten, ast_channel_exten(c), sizeof(dst_exten));
					digit = 1;
					pos = strlen(dst_exten);
				}
			} else {
				ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
				ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));

				if ((res == AST_PBX_ERROR)
					&& ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
					/* if we are already on the 'e' exten, don't jump to it again */
					if (!strcmp(ast_channel_exten(c), "e")) {
						ast_verb(2, "Spawn extension (%s, %s, %d) exited ERROR while already on 'e' exten on '%s'\n", ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c), ast_channel_name(c));
						error = 1;
					} else {
						raise_exception(c, "ERROR", 1);
						continue;
					}
				}

				if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_ASYNCGOTO) {
					ast_channel_clear_softhangup(c, AST_SOFTHANGUP_ASYNCGOTO);
					continue;
				}
				if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_TIMEOUT) {
					if (ast_exists_extension(c, ast_channel_context(c), "T", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						set_ext_pri(c, "T", 1);
						/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
						memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
						ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
						continue;
					} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						raise_exception(c, "ABSOLUTETIMEOUT", 1);
						/* If the AbsoluteTimeout is not reset to 0, we'll get an infinite loop */
						memset(ast_channel_whentohangup(c), 0, sizeof(*ast_channel_whentohangup(c)));
						ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
						continue;
					}
					/* Call timed out with no special extension to jump to. */
				}
				ast_channel_lock(c);
				if (ast_channel_cdr(c)) {
					ast_cdr_update(c);
				}
				ast_channel_unlock(c);
				error = 1;
				break;
			}
		}
		if (error)
			break;

		/*!\note
		 * We get here on a failure of some kind:  non-existing extension or
		 * hangup.  We have options, here.  We can either catch the failure
		 * and continue, or we can drop out entirely. */

		if (invalid
			|| (ast_strlen_zero(dst_exten) &&
				!ast_exists_extension(c, ast_channel_context(c), ast_channel_exten(c), 1,
				S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL)))) {
			/*!\note
			 * If there is no match at priority 1, it is not a valid extension anymore.
			 * Try to continue at "i" (for invalid) or "e" (for exception) or exit if
			 * neither exist.
			 */
			if (ast_exists_extension(c, ast_channel_context(c), "i", 1,
				S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
				ast_verb(3, "Channel '%s' sent to invalid extension: context,exten,priority=%s,%s,%d\n",
					ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c));
				pbx_builtin_setvar_helper(c, "INVALID_EXTEN", ast_channel_exten(c));
				set_ext_pri(c, "i", 1);
			} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
				S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
				raise_exception(c, "INVALID", 1);
			} else {
				ast_log(LOG_WARNING, "Channel '%s' sent to invalid extension but no invalid handler: context,exten,priority=%s,%s,%d\n",
					ast_channel_name(c), ast_channel_context(c), ast_channel_exten(c), ast_channel_priority(c));
				error = 1; /* we know what to do with it */
				break;
			}
		} else if (ast_channel_softhangup_internal_flag(c) & AST_SOFTHANGUP_TIMEOUT) {
			/* If we get this far with AST_SOFTHANGUP_TIMEOUT, then we know that the "T" extension is next. */
			ast_channel_clear_softhangup(c, AST_SOFTHANGUP_TIMEOUT);
		} else {	/* keypress received, get more digits for a full extension */
			int waittime = 0;
			if (digit)
				waittime = ast_channel_pbx(c)->dtimeoutms;
			else if (!autofallthrough)
				waittime = ast_channel_pbx(c)->rtimeoutms;
			if (!waittime) {
				const char *status = pbx_builtin_getvar_helper(c, "DIALSTATUS");
				if (!status)
					status = "UNKNOWN";
				ast_verb(3, "Auto fallthrough, channel '%s' status is '%s'\n", ast_channel_name(c), status);
				if (!strcasecmp(status, "CONGESTION"))
					res = pbx_builtin_congestion(c, "10");
				else if (!strcasecmp(status, "CHANUNAVAIL"))
					res = pbx_builtin_congestion(c, "10");
				else if (!strcasecmp(status, "BUSY"))
					res = pbx_builtin_busy(c, "10");
				error = 1; /* XXX disable message */
				break;	/* exit from the 'for' loop */
			}

			if (collect_digits(c, waittime, dst_exten, sizeof(dst_exten), pos))
				break;
			if (res == AST_PBX_INCOMPLETE && ast_strlen_zero(&dst_exten[pos]))
				timeout = 1;
			if (!timeout
				&& ast_exists_extension(c, ast_channel_context(c), dst_exten, 1,
					S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) { /* Prepare the next cycle */
				set_ext_pri(c, dst_exten, 1);
			} else {
				/* No such extension */
				if (!timeout && !ast_strlen_zero(dst_exten)) {
					/* An invalid extension */
					if (ast_exists_extension(c, ast_channel_context(c), "i", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						ast_verb(3, "Invalid extension '%s' in context '%s' on %s\n", dst_exten, ast_channel_context(c), ast_channel_name(c));
						pbx_builtin_setvar_helper(c, "INVALID_EXTEN", dst_exten);
						set_ext_pri(c, "i", 1);
					} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						raise_exception(c, "INVALID", 1);
					} else {
						ast_log(LOG_WARNING,
							"Invalid extension '%s', but no rule 'i' or 'e' in context '%s'\n",
							dst_exten, ast_channel_context(c));
						found = 1; /* XXX disable message */
						break;
					}
				} else {
					/* A simple timeout */
					if (ast_exists_extension(c, ast_channel_context(c), "t", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						ast_verb(3, "Timeout on %s\n", ast_channel_name(c));
						set_ext_pri(c, "t", 1);
					} else if (ast_exists_extension(c, ast_channel_context(c), "e", 1,
						S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, NULL))) {
						raise_exception(c, "RESPONSETIMEOUT", 1);
					} else {
						ast_log(LOG_WARNING,
							"Timeout, but no rule 't' or 'e' in context '%s'\n",
							ast_channel_context(c));
						found = 1; /* XXX disable message */
						break;
					}
				}
			}
			ast_channel_lock(c);
			if (ast_channel_cdr(c)) {
				ast_verb(2, "CDR updated on %s\n",ast_channel_name(c));
				ast_cdr_update(c);
			}
			ast_channel_unlock(c);
		}
	}

	if (!found && !error) {
		ast_log(LOG_WARNING, "Don't know what to do with '%s'\n", ast_channel_name(c));
	}

	if (!args || !args->no_hangup_chan) {
		ast_softhangup(c, AST_SOFTHANGUP_APPUNLOAD);
		if (!ast_test_flag(ast_channel_flags(c), AST_FLAG_BRIDGE_HANGUP_RUN)
			&& ast_exists_extension(c, ast_channel_context(c), "h", 1,
				S_COR(ast_channel_caller(c)->id.number.valid,
					ast_channel_caller(c)->id.number.str, NULL))) {
			ast_pbx_h_exten_run(c, ast_channel_context(c));
		}
		ast_pbx_hangup_handler_run(c);
	}

	ast_set2_flag(ast_channel_flags(c), autoloopflag, AST_FLAG_IN_AUTOLOOP);
	ast_clear_flag(ast_channel_flags(c), AST_FLAG_BRIDGE_HANGUP_RUN); /* from one round to the next, make sure this gets cleared */
	pbx_destroy(ast_channel_pbx(c));
	ast_channel_pbx_set(c, NULL);

	if (!args || !args->no_hangup_chan) {
		ast_hangup(c);
	}

	return 0;
}

/*!
 * \brief Increase call count for channel
 * \retval 0 on success
 * \retval non-zero if a configured limit (maxcalls, maxload, minmemfree) was reached
*/
static int increase_call_count(const struct ast_channel *c)
{
	int failed = 0;
	double curloadavg;
#if defined(HAVE_SYSINFO)
	long curfreemem;
	struct sysinfo sys_info;
#endif

	ast_mutex_lock(&maxcalllock);
	if (option_maxcalls) {
		if (countcalls >= option_maxcalls) {
			ast_log(LOG_WARNING, "Maximum call limit of %d calls exceeded by '%s'!\n", option_maxcalls, ast_channel_name(c));
			failed = -1;
		}
	}
	if (option_maxload) {
		getloadavg(&curloadavg, 1);
		if (curloadavg >= option_maxload) {
			ast_log(LOG_WARNING, "Maximum loadavg limit of %f load exceeded by '%s' (currently %f)!\n", option_maxload, ast_channel_name(c), curloadavg);
			failed = -1;
		}
	}
#if defined(HAVE_SYSINFO)
	if (option_minmemfree) {
		if (!sysinfo(&sys_info)) {
			/* make sure that the free system memory is above the configured low watermark
			 * convert the amount of freeram from mem_units to MB */
			curfreemem = sys_info.freeram * sys_info.mem_unit;
			curfreemem /= 1024 * 1024;
			if (curfreemem < option_minmemfree) {
				ast_log(LOG_WARNING, "Available system memory (~%ldMB) is below the configured low watermark (%ldMB)\n", curfreemem, option_minmemfree);
				failed = -1;
			}
		}
	}
#endif

	if (!failed) {
		countcalls++;
		totalcalls++;
	}
	ast_mutex_unlock(&maxcalllock);

	return failed;
}

static void decrease_call_count(void)
{
	ast_mutex_lock(&maxcalllock);
	if (countcalls > 0)
		countcalls--;
	ast_mutex_unlock(&maxcalllock);
}

static void destroy_exten(struct ast_exten *e)
{
	if (e->priority == PRIORITY_HINT)
		ast_remove_hint(e);

	if (e->peer_table)
		ast_hashtab_destroy(e->peer_table,0);
	if (e->peer_label_table)
		ast_hashtab_destroy(e->peer_label_table, 0);
	if (e->datad)
		e->datad(e->data);
	ast_free(e);
}

static void *pbx_thread(void *data)
{
	/* Oh joyeous kernel, we're a new thread, with nothing to do but
	   answer this channel and get it going.
	*/
	/* NOTE:
	   The launcher of this function _MUST_ increment 'countcalls'
	   before invoking the function; it will be decremented when the
	   PBX has finished running on the channel
	 */
	struct ast_channel *c = data;

	__ast_pbx_run(c, NULL);
	decrease_call_count();

	pthread_exit(NULL);

	return NULL;
}

enum ast_pbx_result ast_pbx_start(struct ast_channel *c)
{
	pthread_t t;

	if (!c) {
		ast_log(LOG_WARNING, "Asked to start thread on NULL channel?\n");
		return AST_PBX_FAILED;
	}

	if (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		ast_log(LOG_WARNING, "PBX requires Asterisk to be fully booted\n");
		return AST_PBX_FAILED;
	}

	if (increase_call_count(c))
		return AST_PBX_CALL_LIMIT;

	/* Start a new thread, and get something handling this channel. */
	if (ast_pthread_create_detached(&t, NULL, pbx_thread, c)) {
		ast_log(LOG_WARNING, "Failed to create new channel thread\n");
		decrease_call_count();
		return AST_PBX_FAILED;
	}

	return AST_PBX_SUCCESS;
}

enum ast_pbx_result ast_pbx_run_args(struct ast_channel *c, struct ast_pbx_args *args)
{
	enum ast_pbx_result res = AST_PBX_SUCCESS;

	if (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		ast_log(LOG_WARNING, "PBX requires Asterisk to be fully booted\n");
		return AST_PBX_FAILED;
	}

	if (increase_call_count(c)) {
		return AST_PBX_CALL_LIMIT;
	}

	res = __ast_pbx_run(c, args);

	decrease_call_count();

	return res;
}

enum ast_pbx_result ast_pbx_run(struct ast_channel *c)
{
	return ast_pbx_run_args(c, NULL);
}

int ast_active_calls(void)
{
	return countcalls;
}

int ast_processed_calls(void)
{
	return totalcalls;
}

int pbx_set_autofallthrough(int newval)
{
	int oldval = autofallthrough;
	autofallthrough = newval;
	return oldval;
}

int pbx_set_extenpatternmatchnew(int newval)
{
	int oldval = extenpatternmatchnew;
	extenpatternmatchnew = newval;
	return oldval;
}

void pbx_set_overrideswitch(const char *newval)
{
	if (overrideswitch) {
		ast_free(overrideswitch);
	}
	if (!ast_strlen_zero(newval)) {
		overrideswitch = ast_strdup(newval);
	} else {
		overrideswitch = NULL;
	}
}

/*!
 * \brief lookup for a context with a given name,
 * \retval found context or NULL if not found.
 */
static struct ast_context *find_context(const char *context)
{
	struct fake_context item;

	ast_copy_string(item.name, context, sizeof(item.name));

	return ast_hashtab_lookup(contexts_table, &item);
}

/*!
 * \brief lookup for a context with a given name,
 * \retval with conlock held if found.
 * \retval NULL if not found.
 */
static struct ast_context *find_context_locked(const char *context)
{
	struct ast_context *c;
	struct fake_context item;

	ast_copy_string(item.name, context, sizeof(item.name));

	ast_rdlock_contexts();
	c = ast_hashtab_lookup(contexts_table, &item);
	if (!c) {
		ast_unlock_contexts();
	}

	return c;
}

/*!
 * \brief Remove included contexts.
 * This function locks contexts list by &conlist, search for the right context
 * structure, leave context list locked and call ast_context_remove_include2
 * which removes include, unlock contexts list and return ...
 */
int ast_context_remove_include(const char *context, const char *include, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		/* found, remove include from this context ... */
		ret = ast_context_remove_include2(c, include, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

/*!
 * \brief Locks context, remove included contexts, unlocks context.
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_context_remove_include2(struct ast_context *con, const char *include, const char *registrar)
{
	struct ast_include *i, *pi = NULL;
	int ret = -1;

	ast_wrlock_context(con);

	/* find our include */
	for (i = con->includes; i; pi = i, i = i->next) {
		if (!strcmp(i->name, include) &&
				(!registrar || !strcmp(i->registrar, registrar))) {
			/* remove from list */
			ast_verb(3, "Removing inclusion of context '%s' in context '%s; registrar=%s'\n", include, ast_get_context_name(con), registrar);
			if (pi)
				pi->next = i->next;
			else
				con->includes = i->next;
			/* free include and return */
			ast_destroy_timing(&(i->timing));
			ast_free(i);
			ret = 0;
			break;
		}
	}

	ast_unlock_context(con);

	return ret;
}

/*!
 * \note This function locks contexts list by &conlist, search for the rigt context
 * structure, leave context list locked and call ast_context_remove_switch2
 * which removes switch, unlock contexts list and return ...
 */
int ast_context_remove_switch(const char *context, const char *sw, const char *data, const char *registrar)
{
	int ret = -1; /* default error return */
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		/* remove switch from this context ... */
		ret = ast_context_remove_switch2(c, sw, data, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

/*!
 * \brief This function locks given context, removes switch, unlock context and
 * return.
 * \note When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 */
int ast_context_remove_switch2(struct ast_context *con, const char *sw, const char *data, const char *registrar)
{
	struct ast_sw *i;
	int ret = -1;

	ast_wrlock_context(con);

	/* walk switches */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&con->alts, i, list) {
		if (!strcmp(i->name, sw) && !strcmp(i->data, data) &&
			(!registrar || !strcmp(i->registrar, registrar))) {
			/* found, remove from list */
			ast_verb(3, "Removing switch '%s' from context '%s; registrar=%s'\n", sw, ast_get_context_name(con), registrar);
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(i); /* free switch and return */
			ret = 0;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ast_unlock_context(con);

	return ret;
}

/*! \note This function will lock conlock. */
int ast_context_remove_extension(const char *context, const char *extension, int priority, const char *registrar)
{
	return ast_context_remove_extension_callerid(context, extension, priority, NULL, AST_EXT_MATCHCID_ANY, registrar);
}

int ast_context_remove_extension_callerid(const char *context, const char *extension, int priority, const char *callerid, int matchcallerid, const char *registrar)
{
	int ret = -1; /* default error return */
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) { /* ... remove extension ... */
		ret = ast_context_remove_extension_callerid2(c, extension, priority, callerid,
			matchcallerid, registrar, 0);
		ast_unlock_contexts();
	}

	return ret;
}

/*!
 * \brief This functionc locks given context, search for the right extension and
 * fires out all peer in this extensions with given priority. If priority
 * is set to 0, all peers are removed. After that, unlock context and
 * return.
 * \note When do you want to call this function, make sure that &conlock is locked,
 * because some process can handle with your *con context before you lock
 * it.
 *
 */
int ast_context_remove_extension2(struct ast_context *con, const char *extension, int priority, const char *registrar, int already_locked)
{
	return ast_context_remove_extension_callerid2(con, extension, priority, NULL, AST_EXT_MATCHCID_ANY, registrar, already_locked);
}

int ast_context_remove_extension_callerid2(struct ast_context *con, const char *extension, int priority, const char *callerid, int matchcallerid, const char *registrar, int already_locked)
{
	struct ast_exten *exten, *prev_exten = NULL;
	struct ast_exten *peer;
	struct ast_exten ex, *exten2, *exten3;
	char dummy_name[1024];
	struct ast_exten *previous_peer = NULL;
	struct ast_exten *next_peer = NULL;
	int found = 0;

	if (!already_locked)
		ast_wrlock_context(con);

#ifdef NEED_DEBUG
	ast_verb(3,"Removing %s/%s/%d%s%s from trees, registrar=%s\n", con->name, extension, priority, matchcallerid ? "/" : "", matchcallerid ? callerid : "", registrar);
#endif
#ifdef CONTEXT_DEBUG
	check_contexts(__FILE__, __LINE__);
#endif
	/* find this particular extension */
	ex.exten = dummy_name;
	ex.matchcid = matchcallerid;
	ex.cidmatch = callerid;
	ast_copy_string(dummy_name, extension, sizeof(dummy_name));
	exten = ast_hashtab_lookup(con->root_table, &ex);
	if (exten) {
		if (priority == 0) {
			exten2 = ast_hashtab_remove_this_object(con->root_table, exten);
			if (!exten2)
				ast_log(LOG_ERROR,"Trying to delete the exten %s from context %s, but could not remove from the root_table\n", extension, con->name);
			if (con->pattern_tree) {
				struct match_char *x = add_exten_to_pattern_tree(con, exten, 1);

				if (x->exten) { /* this test for safety purposes */
					x->deleted = 1; /* with this marked as deleted, it will never show up in the scoreboard, and therefore never be found */
					x->exten = 0; /* get rid of what will become a bad pointer */
				} else {
					ast_log(LOG_WARNING,"Trying to delete an exten from a context, but the pattern tree node returned isn't a full extension\n");
				}
			}
		} else {
			ex.priority = priority;
			exten2 = ast_hashtab_lookup(exten->peer_table, &ex);
			if (exten2) {
				if (exten2->label) { /* if this exten has a label, remove that, too */
					exten3 = ast_hashtab_remove_this_object(exten->peer_label_table,exten2);
					if (!exten3)
						ast_log(LOG_ERROR,"Did not remove this priority label (%d/%s) from the peer_label_table of context %s, extension %s!\n", priority, exten2->label, con->name, exten2->exten);
				}

				exten3 = ast_hashtab_remove_this_object(exten->peer_table, exten2);
				if (!exten3)
					ast_log(LOG_ERROR,"Did not remove this priority (%d) from the peer_table of context %s, extension %s!\n", priority, con->name, exten2->exten);
				if (exten2 == exten && exten2->peer) {
					exten2 = ast_hashtab_remove_this_object(con->root_table, exten);
					ast_hashtab_insert_immediate(con->root_table, exten2->peer);
				}
				if (ast_hashtab_size(exten->peer_table) == 0) {
					/* well, if the last priority of an exten is to be removed,
					   then, the extension is removed, too! */
					exten3 = ast_hashtab_remove_this_object(con->root_table, exten);
					if (!exten3)
						ast_log(LOG_ERROR,"Did not remove this exten (%s) from the context root_table (%s) (priority %d)\n", exten->exten, con->name, priority);
					if (con->pattern_tree) {
						struct match_char *x = add_exten_to_pattern_tree(con, exten, 1);
						if (x->exten) { /* this test for safety purposes */
							x->deleted = 1; /* with this marked as deleted, it will never show up in the scoreboard, and therefore never be found */
							x->exten = 0; /* get rid of what will become a bad pointer */
						}
					}
				}
			} else {
				ast_log(LOG_ERROR,"Could not find priority %d of exten %s in context %s!\n",
						priority, exten->exten, con->name);
			}
		}
	} else {
		/* hmmm? this exten is not in this pattern tree? */
		ast_log(LOG_WARNING,"Cannot find extension %s in root_table in context %s\n",
				extension, con->name);
	}
#ifdef NEED_DEBUG
	if (con->pattern_tree) {
		ast_log(LOG_NOTICE,"match char tree after exten removal:\n");
		log_match_char_tree(con->pattern_tree, " ");
	}
#endif

	/* scan the extension list to find first matching extension-registrar */
	for (exten = con->root; exten; prev_exten = exten, exten = exten->next) {
		if (!strcmp(exten->exten, extension) &&
			(!registrar || !strcmp(exten->registrar, registrar)) &&
			(!matchcallerid || (!ast_strlen_zero(callerid) && !ast_strlen_zero(exten->cidmatch) && !strcmp(exten->cidmatch, callerid)) || (ast_strlen_zero(callerid) && ast_strlen_zero(exten->cidmatch))))
			break;
	}
	if (!exten) {
		/* we can't find right extension */
		if (!already_locked)
			ast_unlock_context(con);
		return -1;
	}

	/* scan the priority list to remove extension with exten->priority == priority */
	for (peer = exten, next_peer = exten->peer ? exten->peer : exten->next;
		 peer && !strcmp(peer->exten, extension) &&
			(!callerid || (!matchcallerid && !peer->matchcid) || (matchcallerid && peer->matchcid && !strcmp(peer->cidmatch, callerid))) ;
			peer = next_peer, next_peer = next_peer ? (next_peer->peer ? next_peer->peer : next_peer->next) : NULL) {

		if ((priority == 0 || peer->priority == priority) &&
				(!registrar || !strcmp(peer->registrar, registrar) )) {
			found = 1;

			/* we are first priority extension? */
			if (!previous_peer) {
				/*
				 * We are first in the priority chain, so must update the extension chain.
				 * The next node is either the next priority or the next extension
				 */
				struct ast_exten *next_node = peer->peer ? peer->peer : peer->next;
				if (peer->peer) {
					/* move the peer_table and peer_label_table down to the next peer, if
					   it is there */
					peer->peer->peer_table = peer->peer_table;
					peer->peer->peer_label_table = peer->peer_label_table;
					peer->peer_table = NULL;
					peer->peer_label_table = NULL;
				}
				if (!prev_exten) {	/* change the root... */
					con->root = next_node;
				} else {
					prev_exten->next = next_node; /* unlink */
				}
				if (peer->peer)	{ /* update the new head of the pri list */
					peer->peer->next = peer->next;
				}
			} else { /* easy, we are not first priority in extension */
				previous_peer->peer = peer->peer;
			}


			/* now, free whole priority extension */
			destroy_exten(peer);
		} else {
			previous_peer = peer;
		}
	}
	if (!already_locked)
		ast_unlock_context(con);
	return found ? 0 : -1;
}


/*!
 * \note This function locks contexts list by &conlist, searches for the right context
 * structure, and locks the macrolock mutex in that context.
 * macrolock is used to limit a macro to be executed by one call at a time.
 */
int ast_context_lockmacro(const char *context)
{
	struct ast_context *c;
	int ret = -1;

	c = find_context_locked(context);
	if (c) {
		ast_unlock_contexts();

		/* if we found context, lock macrolock */
		ret = ast_mutex_lock(&c->macrolock);
	}

	return ret;
}

/*!
 * \note This function locks contexts list by &conlist, searches for the right context
 * structure, and unlocks the macrolock mutex in that context.
 * macrolock is used to limit a macro to be executed by one call at a time.
 */
int ast_context_unlockmacro(const char *context)
{
	struct ast_context *c;
	int ret = -1;

	c = find_context_locked(context);
	if (c) {
		ast_unlock_contexts();

		/* if we found context, unlock macrolock */
		ret = ast_mutex_unlock(&c->macrolock);
	}

	return ret;
}

/*! \brief Dynamically register a new dial plan application */
int ast_register_application2(const char *app, int (*execute)(struct ast_channel *, const char *), const char *synopsis, const char *description, void *mod)
{
	struct ast_app *tmp, *cur = NULL;
	char tmps[80];
	int length, res;
#ifdef AST_XML_DOCS
	char *tmpxml;
#endif

	AST_RWLIST_WRLOCK(&apps);
	AST_RWLIST_TRAVERSE(&apps, tmp, list) {
		if (!(res = strcasecmp(app, tmp->name))) {
			ast_log(LOG_WARNING, "Already have an application '%s'\n", app);
			AST_RWLIST_UNLOCK(&apps);
			return -1;
		} else if (res < 0)
			break;
	}

	length = sizeof(*tmp) + strlen(app) + 1;

	if (!(tmp = ast_calloc(1, length))) {
		AST_RWLIST_UNLOCK(&apps);
		return -1;
	}

	if (ast_string_field_init(tmp, 128)) {
		AST_RWLIST_UNLOCK(&apps);
		ast_free(tmp);
		return -1;
	}

	strcpy(tmp->name, app);
	tmp->execute = execute;
	tmp->module = mod;

#ifdef AST_XML_DOCS
	/* Try to lookup the docs in our XML documentation database */
	if (ast_strlen_zero(synopsis) && ast_strlen_zero(description)) {
		/* load synopsis */
		tmpxml = ast_xmldoc_build_synopsis("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, synopsis, tmpxml);
		ast_free(tmpxml);

		/* load description */
		tmpxml = ast_xmldoc_build_description("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, description, tmpxml);
		ast_free(tmpxml);

		/* load syntax */
		tmpxml = ast_xmldoc_build_syntax("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, syntax, tmpxml);
		ast_free(tmpxml);

		/* load arguments */
		tmpxml = ast_xmldoc_build_arguments("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, arguments, tmpxml);
		ast_free(tmpxml);

		/* load seealso */
		tmpxml = ast_xmldoc_build_seealso("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, seealso, tmpxml);
		ast_free(tmpxml);
		tmp->docsrc = AST_XML_DOC;
	} else {
#endif
		ast_string_field_set(tmp, synopsis, synopsis);
		ast_string_field_set(tmp, description, description);
#ifdef AST_XML_DOCS
		tmp->docsrc = AST_STATIC_DOC;
	}
#endif

	/* Store in alphabetical order */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&apps, cur, list) {
		if (strcasecmp(tmp->name, cur->name) < 0) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(tmp, list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	if (!cur)
		AST_RWLIST_INSERT_TAIL(&apps, tmp, list);

	ast_verb(2, "Registered application '%s'\n", term_color(tmps, tmp->name, COLOR_BRCYAN, 0, sizeof(tmps)));

	AST_RWLIST_UNLOCK(&apps);

	return 0;
}

/*
 * Append to the list. We don't have a tail pointer because we need
 * to scan the list anyways to check for duplicates during insertion.
 */
int ast_register_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp;

	AST_RWLIST_WRLOCK(&switches);
	AST_RWLIST_TRAVERSE(&switches, tmp, list) {
		if (!strcasecmp(tmp->name, sw->name)) {
			AST_RWLIST_UNLOCK(&switches);
			ast_log(LOG_WARNING, "Switch '%s' already found\n", sw->name);
			return -1;
		}
	}
	AST_RWLIST_INSERT_TAIL(&switches, sw, list);
	AST_RWLIST_UNLOCK(&switches);

	return 0;
}

void ast_unregister_switch(struct ast_switch *sw)
{
	AST_RWLIST_WRLOCK(&switches);
	AST_RWLIST_REMOVE(&switches, sw, list);
	AST_RWLIST_UNLOCK(&switches);
}

/*
 * Help for CLI commands ...
 */

static void print_app_docs(struct ast_app *aa, int fd)
{
	/* Maximum number of characters added by terminal coloring is 22 */
	char infotitle[64 + AST_MAX_APP + 22], syntitle[40], destitle[40], stxtitle[40], argtitle[40];
	char seealsotitle[40];
	char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL, *syntax = NULL, *arguments = NULL;
	char *seealso = NULL;
	int syntax_size, synopsis_size, description_size, arguments_size, seealso_size;

	snprintf(info, sizeof(info), "\n  -= Info about application '%s' =- \n\n", aa->name);
	term_color(infotitle, info, COLOR_MAGENTA, 0, sizeof(infotitle));

	term_color(syntitle, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
	term_color(destitle, "[Description]\n", COLOR_MAGENTA, 0, 40);
	term_color(stxtitle, "[Syntax]\n", COLOR_MAGENTA, 0, 40);
	term_color(argtitle, "[Arguments]\n", COLOR_MAGENTA, 0, 40);
	term_color(seealsotitle, "[See Also]\n", COLOR_MAGENTA, 0, 40);

#ifdef AST_XML_DOCS
	if (aa->docsrc == AST_XML_DOC) {
		description = ast_xmldoc_printable(S_OR(aa->description, "Not available"), 1);
		arguments = ast_xmldoc_printable(S_OR(aa->arguments, "Not available"), 1);
		synopsis = ast_xmldoc_printable(S_OR(aa->synopsis, "Not available"), 1);
		seealso = ast_xmldoc_printable(S_OR(aa->seealso, "Not available"), 1);

		if (!synopsis || !description || !arguments || !seealso) {
			goto return_cleanup;
		}
	} else
#endif
	{
		synopsis_size = strlen(S_OR(aa->synopsis, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		synopsis = ast_malloc(synopsis_size);

		description_size = strlen(S_OR(aa->description, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		description = ast_malloc(description_size);

		arguments_size = strlen(S_OR(aa->arguments, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		arguments = ast_malloc(arguments_size);

		seealso_size = strlen(S_OR(aa->seealso, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		seealso = ast_malloc(seealso_size);

		if (!synopsis || !description || !arguments || !seealso) {
			goto return_cleanup;
		}

		term_color(synopsis, S_OR(aa->synopsis, "Not available"), COLOR_CYAN, 0, synopsis_size);
		term_color(description, S_OR(aa->description, "Not available"),	COLOR_CYAN, 0, description_size);
		term_color(arguments, S_OR(aa->arguments, "Not available"), COLOR_CYAN, 0, arguments_size);
		term_color(seealso, S_OR(aa->seealso, "Not available"), COLOR_CYAN, 0, seealso_size);
	}

	/* Handle the syntax the same for both XML and raw docs */
	syntax_size = strlen(S_OR(aa->syntax, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
	if (!(syntax = ast_malloc(syntax_size))) {
		goto return_cleanup;
	}
	term_color(syntax, S_OR(aa->syntax, "Not available"), COLOR_CYAN, 0, syntax_size);

	ast_cli(fd, "%s%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n",
			infotitle, syntitle, synopsis, destitle, description,
			stxtitle, syntax, argtitle, arguments, seealsotitle, seealso);

return_cleanup:
	ast_free(description);
	ast_free(arguments);
	ast_free(synopsis);
	ast_free(seealso);
	ast_free(syntax);
}

/*
 * \brief 'show application' CLI command implementation function...
 */
static char *handle_show_application(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_app *aa;
	int app, no_registered_app = 1;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show application";
		e->usage =
			"Usage: core show application <application> [<application> [<application> [...]]]\n"
			"       Describes a particular application.\n";
		return NULL;
	case CLI_GENERATE:
		/*
		 * There is a possibility to show informations about more than one
		 * application at one time. You can type 'show application Dial Echo' and
		 * you will see informations about these two applications ...
		 */
		return ast_complete_applications(a->line, a->word, a->n);
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&apps);
	AST_RWLIST_TRAVERSE(&apps, aa, list) {
		/* Check for each app that was supplied as an argument */
		for (app = 3; app < a->argc; app++) {
			if (strcasecmp(aa->name, a->argv[app])) {
				continue;
			}

			/* We found it! */
			no_registered_app = 0;

			print_app_docs(aa, a->fd);
		}
	}
	AST_RWLIST_UNLOCK(&apps);

	/* we found at least one app? no? */
	if (no_registered_app) {
		ast_cli(a->fd, "Your application(s) is (are) not registered\n");
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

/*! \brief  handle_show_hints: CLI support for listing registered dial plan hints */
static char *handle_show_hints(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_hint *hint;
	int num = 0;
	int watchers;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hints";
		e->usage =
			"Usage: core show hints\n"
			"       List registered hints.\n"
			"       Hint details are shown in four columns. In order from left to right, they are:\n"
			"       1. Hint extension URI.\n"
			"       2. Mapped device state identifiers.\n"
			"       3. Current extension state. The aggregate of mapped device states.\n"
			"       4. Watchers - number of subscriptions and other entities watching this hint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (ao2_container_count(hints) == 0) {
		ast_cli(a->fd, "There are no registered dialplan hints\n");
		return CLI_SUCCESS;
	}
	/* ... we have hints ... */
	ast_cli(a->fd, "\n    -= Registered Asterisk Dial Plan Hints =-\n");

	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}
		watchers = ao2_container_count(hint->callbacks);
		ast_cli(a->fd, "   %20s@%-20.20s: %-20.20s  State:%-15.15s Watchers %2d\n",
			ast_get_extension_name(hint->exten),
			ast_get_context_name(ast_get_extension_context(hint->exten)),
			ast_get_extension_app(hint->exten),
			ast_extension_state2str(hint->laststate), watchers);
		ao2_unlock(hint);
		num++;
	}
	ao2_iterator_destroy(&i);

	ast_cli(a->fd, "----------------\n");
	ast_cli(a->fd, "- %d hints registered\n", num);
	return CLI_SUCCESS;
}

/*! \brief autocomplete for CLI command 'core show hint' */
static char *complete_core_show_hint(const char *line, const char *word, int pos, int state)
{
	struct ast_hint *hint;
	char *ret = NULL;
	int which = 0;
	int wordlen;
	struct ao2_iterator i;

	if (pos != 3)
		return NULL;

	wordlen = strlen(word);

	/* walk through all hints */
	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}
		if (!strncasecmp(word, ast_get_extension_name(hint->exten), wordlen) && ++which > state) {
			ret = ast_strdup(ast_get_extension_name(hint->exten));
			ao2_unlock(hint);
			ao2_ref(hint, -1);
			break;
		}
		ao2_unlock(hint);
	}
	ao2_iterator_destroy(&i);

	return ret;
}

/*! \brief  handle_show_hint: CLI support for listing registered dial plan hint */
static char *handle_show_hint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_hint *hint;
	int watchers;
	int num = 0, extenlen;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hint";
		e->usage =
			"Usage: core show hint <exten>\n"
			"       List registered hint.\n"
			"       Hint details are shown in four columns. In order from left to right, they are:\n"
			"       1. Hint extension URI.\n"
			"       2. Mapped device state identifiers.\n"
			"       3. Current extension state. The aggregate of mapped device states.\n"
			"       4. Watchers - number of subscriptions and other entities watching this hint.\n";

		return NULL;
	case CLI_GENERATE:
		return complete_core_show_hint(a->line, a->word, a->pos, a->n);
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;

	if (ao2_container_count(hints) == 0) {
		ast_cli(a->fd, "There are no registered dialplan hints\n");
		return CLI_SUCCESS;
	}

	extenlen = strlen(a->argv[3]);
	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		ao2_lock(hint);
		if (!hint->exten) {
			/* The extension has already been destroyed */
			ao2_unlock(hint);
			continue;
		}
		if (!strncasecmp(ast_get_extension_name(hint->exten), a->argv[3], extenlen)) {
			watchers = ao2_container_count(hint->callbacks);
			ast_cli(a->fd, "   %20s@%-20.20s: %-20.20s  State:%-15.15s Watchers %2d\n",
				ast_get_extension_name(hint->exten),
				ast_get_context_name(ast_get_extension_context(hint->exten)),
				ast_get_extension_app(hint->exten),
				ast_extension_state2str(hint->laststate), watchers);
			num++;
		}
		ao2_unlock(hint);
	}
	ao2_iterator_destroy(&i);
	if (!num)
		ast_cli(a->fd, "No hints matching extension %s\n", a->argv[3]);
	else
		ast_cli(a->fd, "%d hint%s matching extension %s\n", num, (num!=1 ? "s":""), a->argv[3]);
	return CLI_SUCCESS;
}


/*! \brief  handle_show_switches: CLI support for listing registered dial plan switches */
static char *handle_show_switches(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_switch *sw;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show switches";
		e->usage =
			"Usage: core show switches\n"
			"       List registered switches\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_RWLIST_RDLOCK(&switches);

	if (AST_RWLIST_EMPTY(&switches)) {
		AST_RWLIST_UNLOCK(&switches);
		ast_cli(a->fd, "There are no registered alternative switches\n");
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "\n    -= Registered Asterisk Alternative Switches =-\n");
	AST_RWLIST_TRAVERSE(&switches, sw, list)
		ast_cli(a->fd, "%s: %s\n", sw->name, sw->description);

	AST_RWLIST_UNLOCK(&switches);

	return CLI_SUCCESS;
}

static char *handle_show_applications(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_app *aa;
	int like = 0, describing = 0;
	int total_match = 0;    /* Number of matches in like clause */
	int total_apps = 0;     /* Number of apps registered */
	static const char * const choices[] = { "like", "describing", NULL };

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show applications [like|describing]";
		e->usage =
			"Usage: core show applications [{like|describing} <text>]\n"
			"       List applications which are currently available.\n"
			"       If 'like', <text> will be a substring of the app name\n"
			"       If 'describing', <text> will be a substring of the description\n";
		return NULL;
	case CLI_GENERATE:
		return (a->pos != 3) ? NULL : ast_cli_complete(a->word, choices, a->n);
	}

	AST_RWLIST_RDLOCK(&apps);

	if (AST_RWLIST_EMPTY(&apps)) {
		ast_cli(a->fd, "There are no registered applications\n");
		AST_RWLIST_UNLOCK(&apps);
		return CLI_SUCCESS;
	}

	/* core list applications like <keyword> */
	if ((a->argc == 5) && (!strcmp(a->argv[3], "like"))) {
		like = 1;
	} else if ((a->argc > 4) && (!strcmp(a->argv[3], "describing"))) {
		describing = 1;
	}

	/* core list applications describing <keyword1> [<keyword2>] [...] */
	if ((!like) && (!describing)) {
		ast_cli(a->fd, "    -= Registered Asterisk Applications =-\n");
	} else {
		ast_cli(a->fd, "    -= Matching Asterisk Applications =-\n");
	}

	AST_RWLIST_TRAVERSE(&apps, aa, list) {
		int printapp = 0;
		total_apps++;
		if (like) {
			if (strcasestr(aa->name, a->argv[4])) {
				printapp = 1;
				total_match++;
			}
		} else if (describing) {
			if (aa->description) {
				/* Match all words on command line */
				int i;
				printapp = 1;
				for (i = 4; i < a->argc; i++) {
					if (!strcasestr(aa->description, a->argv[i])) {
						printapp = 0;
					} else {
						total_match++;
					}
				}
			}
		} else {
			printapp = 1;
		}

		if (printapp) {
			ast_cli(a->fd,"  %20s: %s\n", aa->name, aa->synopsis ? aa->synopsis : "<Synopsis not available>");
		}
	}
	if ((!like) && (!describing)) {
		ast_cli(a->fd, "    -= %d Applications Registered =-\n",total_apps);
	} else {
		ast_cli(a->fd, "    -= %d Applications Matching =-\n",total_match);
	}

	AST_RWLIST_UNLOCK(&apps);

	return CLI_SUCCESS;
}

/*
 * 'show dialplan' CLI command implementation functions ...
 */
static char *complete_show_dialplan_context(const char *line, const char *word, int pos,
	int state)
{
	struct ast_context *c = NULL;
	char *ret = NULL;
	int which = 0;
	int wordlen;

	/* we are do completion of [exten@]context on second position only */
	if (pos != 2)
		return NULL;

	ast_rdlock_contexts();

	wordlen = strlen(word);

	/* walk through all contexts and return the n-th match */
	while ( (c = ast_walk_contexts(c)) ) {
		if (!strncasecmp(word, ast_get_context_name(c), wordlen) && ++which > state) {
			ret = ast_strdup(ast_get_context_name(c));
			break;
		}
	}

	ast_unlock_contexts();

	return ret;
}

/*! \brief Counters for the show dialplan manager command */
struct dialplan_counters {
	int total_items;
	int total_context;
	int total_exten;
	int total_prio;
	int context_existence;
	int extension_existence;
};

/*! \brief helper function to print an extension */
static void print_ext(struct ast_exten *e, char * buf, int buflen)
{
	int prio = ast_get_extension_priority(e);
	if (prio == PRIORITY_HINT) {
		snprintf(buf, buflen, "hint: %s",
			ast_get_extension_app(e));
	} else {
		snprintf(buf, buflen, "%d. %s(%s)",
			prio, ast_get_extension_app(e),
			(!ast_strlen_zero(ast_get_extension_app_data(e)) ? (char *)ast_get_extension_app_data(e) : ""));
	}
}

/* XXX not verified */
static int show_dialplan_helper(int fd, const char *context, const char *exten, struct dialplan_counters *dpc, struct ast_include *rinclude, int includecount, const char *includes[])
{
	struct ast_context *c = NULL;
	int res = 0, old_total_exten = dpc->total_exten;

	ast_rdlock_contexts();

	/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		struct ast_exten *e;
		struct ast_include *i;
		struct ast_ignorepat *ip;
#ifndef LOW_MEMORY
		char buf[1024], buf2[1024];
#else
		char buf[256], buf2[256];
#endif
		int context_info_printed = 0;

		if (context && strcmp(ast_get_context_name(c), context))
			continue;	/* skip this one, name doesn't match */

		dpc->context_existence = 1;

		ast_rdlock_context(c);

		/* are we looking for exten too? if yes, we print context
		 * only if we find our extension.
		 * Otherwise print context even if empty ?
		 * XXX i am not sure how the rinclude is handled.
		 * I think it ought to go inside.
		 */
		if (!exten) {
			dpc->total_context++;
			ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
				ast_get_context_name(c), ast_get_context_registrar(c));
			context_info_printed = 1;
		}

		/* walk extensions ... */
		e = NULL;
		while ( (e = ast_walk_context_extensions(c, e)) ) {
			struct ast_exten *p;

			if (exten && !ast_extension_match(ast_get_extension_name(e), exten))
				continue;	/* skip, extension match failed */

			dpc->extension_existence = 1;

			/* may we print context info? */
			if (!context_info_printed) {
				dpc->total_context++;
				if (rinclude) { /* TODO Print more info about rinclude */
					ast_cli(fd, "[ Included context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
				} else {
					ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
				}
				context_info_printed = 1;
			}
			dpc->total_prio++;

			/* write extension name and first peer */
			if (e->matchcid == AST_EXT_MATCHCID_ON)
				snprintf(buf, sizeof(buf), "'%s' (CID match '%s') => ", ast_get_extension_name(e), e->cidmatch);
			else
				snprintf(buf, sizeof(buf), "'%s' =>", ast_get_extension_name(e));

			print_ext(e, buf2, sizeof(buf2));

			ast_cli(fd, "  %-17s %-45s [%s]\n", buf, buf2,
				ast_get_extension_registrar(e));

			dpc->total_exten++;
			/* walk next extension peers */
			p = e;	/* skip the first one, we already got it */
			while ( (p = ast_walk_extension_priorities(e, p)) ) {
				const char *el = ast_get_extension_label(p);
				dpc->total_prio++;
				if (el)
					snprintf(buf, sizeof(buf), "   [%s]", el);
				else
					buf[0] = '\0';
				print_ext(p, buf2, sizeof(buf2));

				ast_cli(fd,"  %-17s %-45s [%s]\n", buf, buf2,
					ast_get_extension_registrar(p));
			}
		}

		/* walk included and write info ... */
		i = NULL;
		while ( (i = ast_walk_context_includes(c, i)) ) {
			snprintf(buf, sizeof(buf), "'%s'", ast_get_include_name(i));
			if (exten) {
				/* Check all includes for the requested extension */
				if (includecount >= AST_PBX_MAX_STACK) {
					ast_log(LOG_WARNING, "Maximum include depth exceeded!\n");
				} else {
					int dupe = 0;
					int x;
					for (x = 0; x < includecount; x++) {
						if (!strcasecmp(includes[x], ast_get_include_name(i))) {
							dupe++;
							break;
						}
					}
					if (!dupe) {
						includes[includecount] = ast_get_include_name(i);
						show_dialplan_helper(fd, ast_get_include_name(i), exten, dpc, i, includecount + 1, includes);
					} else {
						ast_log(LOG_WARNING, "Avoiding circular include of %s within %s\n", ast_get_include_name(i), context);
					}
				}
			} else {
				ast_cli(fd, "  Include =>        %-45s [%s]\n",
					buf, ast_get_include_registrar(i));
			}
		}

		/* walk ignore patterns and write info ... */
		ip = NULL;
		while ( (ip = ast_walk_context_ignorepats(c, ip)) ) {
			const char *ipname = ast_get_ignorepat_name(ip);
			char ignorepat[AST_MAX_EXTENSION];
			snprintf(buf, sizeof(buf), "'%s'", ipname);
			snprintf(ignorepat, sizeof(ignorepat), "_%s.", ipname);
			if (!exten || ast_extension_match(ignorepat, exten)) {
				ast_cli(fd, "  Ignore pattern => %-45s [%s]\n",
					buf, ast_get_ignorepat_registrar(ip));
			}
		}
		if (!rinclude) {
			struct ast_sw *sw = NULL;
			while ( (sw = ast_walk_context_switches(c, sw)) ) {
				snprintf(buf, sizeof(buf), "'%s/%s'",
					ast_get_switch_name(sw),
					ast_get_switch_data(sw));
				ast_cli(fd, "  Alt. Switch =>    %-45s [%s]\n",
					buf, ast_get_switch_registrar(sw));
			}
		}

		ast_unlock_context(c);

		/* if we print something in context, make an empty line */
		if (context_info_printed)
			ast_cli(fd, "\n");
	}
	ast_unlock_contexts();

	return (dpc->total_exten == old_total_exten) ? -1 : res;
}

static int show_debug_helper(int fd, const char *context, const char *exten, struct dialplan_counters *dpc, struct ast_include *rinclude, int includecount, const char *includes[])
{
	struct ast_context *c = NULL;
	int res = 0, old_total_exten = dpc->total_exten;

	ast_cli(fd,"\n     In-mem exten Trie for Fast Extension Pattern Matching:\n\n");

	ast_cli(fd,"\n           Explanation: Node Contents Format = <char(s) to match>:<pattern?>:<specif>:[matched extension]\n");
	ast_cli(fd,    "                        Where <char(s) to match> is a set of chars, any one of which should match the current character\n");
	ast_cli(fd,    "                              <pattern?>: Y if this a pattern match (eg. _XZN[5-7]), N otherwise\n");
	ast_cli(fd,    "                              <specif>: an assigned 'exactness' number for this matching char. The lower the number, the more exact the match\n");
	ast_cli(fd,    "                              [matched exten]: If all chars matched to this point, which extension this matches. In form: EXTEN:<exten string>\n");
	ast_cli(fd,    "                        In general, you match a trie node to a string character, from left to right. All possible matching chars\n");
	ast_cli(fd,    "                        are in a string vertically, separated by an unbroken string of '+' characters.\n\n");
	ast_rdlock_contexts();

	/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		int context_info_printed = 0;

		if (context && strcmp(ast_get_context_name(c), context))
			continue;	/* skip this one, name doesn't match */

		dpc->context_existence = 1;

		if (!c->pattern_tree) {
			/* Ignore check_return warning from Coverity for ast_exists_extension below */
			ast_exists_extension(NULL, c->name, "s", 1, ""); /* do this to force the trie to built, if it is not already */
		}

		ast_rdlock_context(c);

		dpc->total_context++;
		ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
			ast_get_context_name(c), ast_get_context_registrar(c));
		context_info_printed = 1;

		if (c->pattern_tree)
		{
			cli_match_char_tree(c->pattern_tree, " ", fd);
		} else {
			ast_cli(fd,"\n     No Pattern Trie present. Perhaps the context is empty...or there is trouble...\n\n");
		}

		ast_unlock_context(c);

		/* if we print something in context, make an empty line */
		if (context_info_printed)
			ast_cli(fd, "\n");
	}
	ast_unlock_contexts();

	return (dpc->total_exten == old_total_exten) ? -1 : res;
}

static char *handle_show_dialplan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *exten = NULL, *context = NULL;
	/* Variables used for different counters */
	struct dialplan_counters counters;
	const char *incstack[AST_PBX_MAX_STACK];

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan show";
		e->usage =
			"Usage: dialplan show [[exten@]context]\n"
			"       Show dialplan\n";
		return NULL;
	case CLI_GENERATE:
		return complete_show_dialplan_context(a->line, a->word, a->pos, a->n);
	}

	memset(&counters, 0, sizeof(counters));

	if (a->argc != 2 && a->argc != 3)
		return CLI_SHOWUSAGE;

	/* we obtain [exten@]context? if yes, split them ... */
	if (a->argc == 3) {
		if (strchr(a->argv[2], '@')) {	/* split into exten & context */
			context = ast_strdupa(a->argv[2]);
			exten = strsep(&context, "@");
			/* change empty strings to NULL */
			if (ast_strlen_zero(exten))
				exten = NULL;
		} else { /* no '@' char, only context given */
			context = ast_strdupa(a->argv[2]);
		}
		if (ast_strlen_zero(context))
			context = NULL;
	}
	/* else Show complete dial plan, context and exten are NULL */
	show_dialplan_helper(a->fd, context, exten, &counters, NULL, 0, incstack);

	/* check for input failure and throw some error messages */
	if (context && !counters.context_existence) {
		ast_cli(a->fd, "There is no existence of '%s' context\n", context);
		return CLI_FAILURE;
	}

	if (exten && !counters.extension_existence) {
		if (context)
			ast_cli(a->fd, "There is no existence of %s@%s extension\n",
				exten, context);
		else
			ast_cli(a->fd,
				"There is no existence of '%s' extension in all contexts\n",
				exten);
		return CLI_FAILURE;
	}

	ast_cli(a->fd,"-= %d %s (%d %s) in %d %s. =-\n",
				counters.total_exten, counters.total_exten == 1 ? "extension" : "extensions",
				counters.total_prio, counters.total_prio == 1 ? "priority" : "priorities",
				counters.total_context, counters.total_context == 1 ? "context" : "contexts");

	/* everything ok */
	return CLI_SUCCESS;
}

/*! \brief Send ack once */
static char *handle_debug_dialplan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *exten = NULL, *context = NULL;
	/* Variables used for different counters */
	struct dialplan_counters counters;
	const char *incstack[AST_PBX_MAX_STACK];

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan debug";
		e->usage =
			"Usage: dialplan debug [context]\n"
			"       Show dialplan context Trie(s). Usually only useful to folks debugging the deep internals of the fast pattern matcher\n";
		return NULL;
	case CLI_GENERATE:
		return complete_show_dialplan_context(a->line, a->word, a->pos, a->n);
	}

	memset(&counters, 0, sizeof(counters));

	if (a->argc != 2 && a->argc != 3)
		return CLI_SHOWUSAGE;

	/* we obtain [exten@]context? if yes, split them ... */
	/* note: we ignore the exten totally here .... */
	if (a->argc == 3) {
		if (strchr(a->argv[2], '@')) {	/* split into exten & context */
			context = ast_strdupa(a->argv[2]);
			exten = strsep(&context, "@");
			/* change empty strings to NULL */
			if (ast_strlen_zero(exten))
				exten = NULL;
		} else { /* no '@' char, only context given */
			context = ast_strdupa(a->argv[2]);
		}
		if (ast_strlen_zero(context))
			context = NULL;
	}
	/* else Show complete dial plan, context and exten are NULL */
	show_debug_helper(a->fd, context, exten, &counters, NULL, 0, incstack);

	/* check for input failure and throw some error messages */
	if (context && !counters.context_existence) {
		ast_cli(a->fd, "There is no existence of '%s' context\n", context);
		return CLI_FAILURE;
	}


	ast_cli(a->fd,"-= %d %s. =-\n",
			counters.total_context, counters.total_context == 1 ? "context" : "contexts");

	/* everything ok */
	return CLI_SUCCESS;
}

/*! \brief Send ack once */
static void manager_dpsendack(struct mansession *s, const struct message *m)
{
	astman_send_listack(s, m, "DialPlan list will follow", "start");
}

/*! \brief Show dialplan extensions
 * XXX this function is similar but not exactly the same as the CLI's
 * show dialplan. Must check whether the difference is intentional or not.
 */
static int manager_show_dialplan_helper(struct mansession *s, const struct message *m,
					const char *actionidtext, const char *context,
					const char *exten, struct dialplan_counters *dpc,
					struct ast_include *rinclude)
{
	struct ast_context *c;
	int res = 0, old_total_exten = dpc->total_exten;

	if (ast_strlen_zero(exten))
		exten = NULL;
	if (ast_strlen_zero(context))
		context = NULL;

	ast_debug(3, "manager_show_dialplan: Context: -%s- Extension: -%s-\n", context, exten);

	/* try to lock contexts */
	if (ast_rdlock_contexts()) {
		astman_send_error(s, m, "Failed to lock contexts");
		ast_log(LOG_WARNING, "Failed to lock contexts list for manager: listdialplan\n");
		return -1;
	}

	c = NULL;		/* walk all contexts ... */
	while ( (c = ast_walk_contexts(c)) ) {
		struct ast_exten *e;
		struct ast_include *i;
		struct ast_ignorepat *ip;

		if (context && strcmp(ast_get_context_name(c), context) != 0)
			continue;	/* not the name we want */

		dpc->context_existence = 1;
		dpc->total_context++;

		ast_debug(3, "manager_show_dialplan: Found Context: %s \n", ast_get_context_name(c));

		if (ast_rdlock_context(c)) {	/* failed to lock */
			ast_debug(3, "manager_show_dialplan: Failed to lock context\n");
			continue;
		}

		/* XXX note- an empty context is not printed */
		e = NULL;		/* walk extensions in context  */
		while ( (e = ast_walk_context_extensions(c, e)) ) {
			struct ast_exten *p;

			/* looking for extension? is this our extension? */
			if (exten && !ast_extension_match(ast_get_extension_name(e), exten)) {
				/* not the one we are looking for, continue */
				ast_debug(3, "manager_show_dialplan: Skipping extension %s\n", ast_get_extension_name(e));
				continue;
			}
			ast_debug(3, "manager_show_dialplan: Found Extension: %s \n", ast_get_extension_name(e));

			dpc->extension_existence = 1;

			dpc->total_exten++;

			p = NULL;		/* walk next extension peers */
			while ( (p = ast_walk_extension_priorities(e, p)) ) {
				int prio = ast_get_extension_priority(p);

				dpc->total_prio++;
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nExtension: %s\r\n", ast_get_context_name(c), ast_get_extension_name(e) );

				/* XXX maybe make this conditional, if p != e ? */
				if (ast_get_extension_label(p))
					astman_append(s, "ExtensionLabel: %s\r\n", ast_get_extension_label(p));

				if (prio == PRIORITY_HINT) {
					astman_append(s, "Priority: hint\r\nApplication: %s\r\n", ast_get_extension_app(p));
				} else {
					astman_append(s, "Priority: %d\r\nApplication: %s\r\nAppData: %s\r\n", prio, ast_get_extension_app(p), (char *) ast_get_extension_app_data(p));
				}
				astman_append(s, "Registrar: %s\r\n\r\n", ast_get_extension_registrar(e));
			}
		}

		i = NULL;		/* walk included and write info ... */
		while ( (i = ast_walk_context_includes(c, i)) ) {
			if (exten) {
				/* Check all includes for the requested extension */
				manager_show_dialplan_helper(s, m, actionidtext, ast_get_include_name(i), exten, dpc, i);
			} else {
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nIncludeContext: %s\r\nRegistrar: %s\r\n", ast_get_context_name(c), ast_get_include_name(i), ast_get_include_registrar(i));
				astman_append(s, "\r\n");
				ast_debug(3, "manager_show_dialplan: Found Included context: %s \n", ast_get_include_name(i));
			}
		}

		ip = NULL;	/* walk ignore patterns and write info ... */
		while ( (ip = ast_walk_context_ignorepats(c, ip)) ) {
			const char *ipname = ast_get_ignorepat_name(ip);
			char ignorepat[AST_MAX_EXTENSION];

			snprintf(ignorepat, sizeof(ignorepat), "_%s.", ipname);
			if (!exten || ast_extension_match(ignorepat, exten)) {
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nIgnorePattern: %s\r\nRegistrar: %s\r\n", ast_get_context_name(c), ipname, ast_get_ignorepat_registrar(ip));
				astman_append(s, "\r\n");
			}
		}
		if (!rinclude) {
			struct ast_sw *sw = NULL;
			while ( (sw = ast_walk_context_switches(c, sw)) ) {
				if (!dpc->total_items++)
					manager_dpsendack(s, m);
				astman_append(s, "Event: ListDialplan\r\n%s", actionidtext);
				astman_append(s, "Context: %s\r\nSwitch: %s/%s\r\nRegistrar: %s\r\n", ast_get_context_name(c), ast_get_switch_name(sw), ast_get_switch_data(sw), ast_get_switch_registrar(sw));
				astman_append(s, "\r\n");
				ast_debug(3, "manager_show_dialplan: Found Switch : %s \n", ast_get_switch_name(sw));
			}
		}

		ast_unlock_context(c);
	}
	ast_unlock_contexts();

	if (dpc->total_exten == old_total_exten) {
		ast_debug(3, "manager_show_dialplan: Found nothing new\n");
		/* Nothing new under the sun */
		return -1;
	} else {
		return res;
	}
}

/*! \brief  Manager listing of dial plan */
static int manager_show_dialplan(struct mansession *s, const struct message *m)
{
	const char *exten, *context;
	const char *id = astman_get_header(m, "ActionID");
	char idtext[256];

	/* Variables used for different counters */
	struct dialplan_counters counters;

	if (!ast_strlen_zero(id))
		snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);
	else
		idtext[0] = '\0';

	memset(&counters, 0, sizeof(counters));

	exten = astman_get_header(m, "Extension");
	context = astman_get_header(m, "Context");

	manager_show_dialplan_helper(s, m, idtext, context, exten, &counters, NULL);

	if (!ast_strlen_zero(context) && !counters.context_existence) {
		char errorbuf[BUFSIZ];

		snprintf(errorbuf, sizeof(errorbuf), "Did not find context %s", context);
		astman_send_error(s, m, errorbuf);
		return 0;
	}
	if (!ast_strlen_zero(exten) && !counters.extension_existence) {
		char errorbuf[BUFSIZ];

		if (!ast_strlen_zero(context))
			snprintf(errorbuf, sizeof(errorbuf), "Did not find extension %s@%s", exten, context);
		else
			snprintf(errorbuf, sizeof(errorbuf), "Did not find extension %s in any context", exten);
		astman_send_error(s, m, errorbuf);
		return 0;
	}

	if (!counters.total_items) {
		manager_dpsendack(s, m);
	}

	astman_append(s, "Event: ShowDialPlanComplete\r\n"
		"EventList: Complete\r\n"
		"ListItems: %d\r\n"
		"ListExtensions: %d\r\n"
		"ListPriorities: %d\r\n"
		"ListContexts: %d\r\n"
		"%s"
		"\r\n", counters.total_items, counters.total_exten, counters.total_prio, counters.total_context, idtext);

	/* everything ok */
	return 0;
}

/*! \brief CLI support for listing global variables in a parseable way */
static char *handle_show_globals(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i = 0;
	struct ast_var_t *newvariable;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan show globals";
		e->usage =
			"Usage: dialplan show globals\n"
			"       List current global dialplan variables and their values\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_rwlock_rdlock(&globalslock);
	AST_LIST_TRAVERSE (&globals, newvariable, entries) {
		i++;
		ast_cli(a->fd, "   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_rwlock_unlock(&globalslock);
	ast_cli(a->fd, "\n    -- %d variable(s)\n", i);

	return CLI_SUCCESS;
}

#ifdef AST_DEVMODE
static char *handle_show_device2extenstate(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_devstate_aggregate agg;
	int i, j, exten, combined;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show device2extenstate";
		e->usage =
			"Usage: core show device2extenstate\n"
			"       Lists device state to extension state combinations.\n";
	case CLI_GENERATE:
		return NULL;
	}
	for (i = 0; i < AST_DEVICE_TOTAL; i++) {
		for (j = 0; j < AST_DEVICE_TOTAL; j++) {
			ast_devstate_aggregate_init(&agg);
			ast_devstate_aggregate_add(&agg, i);
			ast_devstate_aggregate_add(&agg, j);
			combined = ast_devstate_aggregate_result(&agg);
			exten = ast_devstate_to_extenstate(combined);
			ast_cli(a->fd, "\n Exten:%14s  CombinedDevice:%12s  Dev1:%12s  Dev2:%12s", ast_extension_state2str(exten), ast_devstate_str(combined), ast_devstate_str(j), ast_devstate_str(i));
		}
	}
	ast_cli(a->fd, "\n");
	return CLI_SUCCESS;
}
#endif

/*! \brief CLI support for listing chanvar's variables in a parseable way */
static char *handle_show_chanvar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan = NULL;
	struct ast_str *vars = ast_str_alloca(BUFSIZ * 4); /* XXX large because we might have lots of channel vars */

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan show chanvar";
		e->usage =
			"Usage: dialplan show chanvar <channel>\n"
			"       List current channel variables and their values\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
	}

	if (a->argc != e->args + 1)
		return CLI_SHOWUSAGE;

	if (!(chan = ast_channel_get_by_name(a->argv[e->args]))) {
		ast_cli(a->fd, "Channel '%s' not found\n", a->argv[e->args]);
		return CLI_FAILURE;
	}

	pbx_builtin_serialize_variables(chan, &vars);

	if (ast_str_strlen(vars)) {
		ast_cli(a->fd, "\nVariables for channel %s:\n%s\n", a->argv[e->args], ast_str_buffer(vars));
	}

	chan = ast_channel_unref(chan);

	return CLI_SUCCESS;
}

static char *handle_set_global(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set global";
		e->usage =
			"Usage: dialplan set global <name> <value>\n"
			"       Set global dialplan variable <name> to <value>\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args + 2)
		return CLI_SHOWUSAGE;

	pbx_builtin_setvar_helper(NULL, a->argv[3], a->argv[4]);
	ast_cli(a->fd, "\n    -- Global variable '%s' set to '%s'\n", a->argv[3], a->argv[4]);

	return CLI_SUCCESS;
}

static char *handle_set_chanvar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	const char *chan_name, *var_name, *var_value;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set chanvar";
		e->usage =
			"Usage: dialplan set chanvar <channel> <varname> <value>\n"
			"       Set channel variable <varname> to <value>\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
	}

	if (a->argc != e->args + 3)
		return CLI_SHOWUSAGE;

	chan_name = a->argv[e->args];
	var_name = a->argv[e->args + 1];
	var_value = a->argv[e->args + 2];

	if (!(chan = ast_channel_get_by_name(chan_name))) {
		ast_cli(a->fd, "Channel '%s' not found\n", chan_name);
		return CLI_FAILURE;
	}

	pbx_builtin_setvar_helper(chan, var_name, var_value);

	chan = ast_channel_unref(chan);

	ast_cli(a->fd, "\n    -- Channel variable '%s' set to '%s' for '%s'\n",  var_name, var_value, chan_name);

	return CLI_SUCCESS;
}

static char *handle_set_extenpatternmatchnew(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set extenpatternmatchnew true";
		e->usage =
			"Usage: dialplan set extenpatternmatchnew true|false\n"
			"       Use the NEW extension pattern matching algorithm, true or false.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	oldval =  pbx_set_extenpatternmatchnew(1);

	if (oldval)
		ast_cli(a->fd, "\n    -- Still using the NEW pattern match algorithm for extension names in the dialplan.\n");
	else
		ast_cli(a->fd, "\n    -- Switched to using the NEW pattern match algorithm for extension names in the dialplan.\n");

	return CLI_SUCCESS;
}

static char *handle_unset_extenpatternmatchnew(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set extenpatternmatchnew false";
		e->usage =
			"Usage: dialplan set extenpatternmatchnew true|false\n"
			"       Use the NEW extension pattern matching algorithm, true or false.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	oldval =  pbx_set_extenpatternmatchnew(0);

	if (!oldval)
		ast_cli(a->fd, "\n    -- Still using the OLD pattern match algorithm for extension names in the dialplan.\n");
	else
		ast_cli(a->fd, "\n    -- Switched to using the OLD pattern match algorithm for extension names in the dialplan.\n");

	return CLI_SUCCESS;
}

/*
 * CLI entries for upper commands ...
 */
static struct ast_cli_entry pbx_cli[] = {
	AST_CLI_DEFINE(handle_show_applications, "Shows registered dialplan applications"),
	AST_CLI_DEFINE(handle_show_functions, "Shows registered dialplan functions"),
	AST_CLI_DEFINE(handle_show_switches, "Show alternative switches"),
	AST_CLI_DEFINE(handle_show_hints, "Show dialplan hints"),
	AST_CLI_DEFINE(handle_show_hint, "Show dialplan hint"),
	AST_CLI_DEFINE(handle_show_globals, "Show global dialplan variables"),
#ifdef AST_DEVMODE
	AST_CLI_DEFINE(handle_show_device2extenstate, "Show expected exten state from multiple device states"),
#endif
	AST_CLI_DEFINE(handle_show_chanvar, "Show channel variables"),
	AST_CLI_DEFINE(handle_show_function, "Describe a specific dialplan function"),
	AST_CLI_DEFINE(handle_show_hangup_all, "Show hangup handlers of all channels"),
	AST_CLI_DEFINE(handle_show_hangup_channel, "Show hangup handlers of a specified channel"),
	AST_CLI_DEFINE(handle_show_application, "Describe a specific dialplan application"),
	AST_CLI_DEFINE(handle_set_global, "Set global dialplan variable"),
	AST_CLI_DEFINE(handle_set_chanvar, "Set a channel variable"),
	AST_CLI_DEFINE(handle_show_dialplan, "Show dialplan"),
	AST_CLI_DEFINE(handle_debug_dialplan, "Show fast extension pattern matching data structures"),
	AST_CLI_DEFINE(handle_unset_extenpatternmatchnew, "Use the Old extension pattern matching algorithm."),
	AST_CLI_DEFINE(handle_set_extenpatternmatchnew, "Use the New extension pattern matching algorithm."),
};

static void unreference_cached_app(struct ast_app *app)
{
	struct ast_context *context = NULL;
	struct ast_exten *eroot = NULL, *e = NULL;

	ast_rdlock_contexts();
	while ((context = ast_walk_contexts(context))) {
		while ((eroot = ast_walk_context_extensions(context, eroot))) {
			while ((e = ast_walk_extension_priorities(eroot, e))) {
				if (e->cached_app == app)
					e->cached_app = NULL;
			}
		}
	}
	ast_unlock_contexts();

	return;
}

int ast_unregister_application(const char *app)
{
	struct ast_app *tmp;

	AST_RWLIST_WRLOCK(&apps);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&apps, tmp, list) {
		if (!strcasecmp(app, tmp->name)) {
			unreference_cached_app(tmp);
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_verb(2, "Unregistered application '%s'\n", tmp->name);
			ast_string_field_free_memory(tmp);
			ast_free(tmp);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&apps);

	return tmp ? 0 : -1;
}

struct ast_context *ast_context_find_or_create(struct ast_context **extcontexts, struct ast_hashtab *exttable, const char *name, const char *registrar)
{
	struct ast_context *tmp, **local_contexts;
	struct fake_context search;
	int length = sizeof(struct ast_context) + strlen(name) + 1;

	if (!contexts_table) {
		/* Protect creation of contexts_table from reentrancy. */
		ast_wrlock_contexts();
		if (!contexts_table) {
			contexts_table = ast_hashtab_create(17,
				ast_hashtab_compare_contexts,
				ast_hashtab_resize_java,
				ast_hashtab_newsize_java,
				ast_hashtab_hash_contexts,
				0);
		}
		ast_unlock_contexts();
	}

	ast_copy_string(search.name, name, sizeof(search.name));
	if (!extcontexts) {
		ast_rdlock_contexts();
		local_contexts = &contexts;
		tmp = ast_hashtab_lookup(contexts_table, &search);
		ast_unlock_contexts();
		if (tmp) {
			tmp->refcount++;
			return tmp;
		}
	} else { /* local contexts just in a linked list; search there for the new context; slow, linear search, but not frequent */
		local_contexts = extcontexts;
		tmp = ast_hashtab_lookup(exttable, &search);
		if (tmp) {
			tmp->refcount++;
			return tmp;
		}
	}

	if ((tmp = ast_calloc(1, length))) {
		ast_rwlock_init(&tmp->lock);
		ast_mutex_init(&tmp->macrolock);
		strcpy(tmp->name, name);
		tmp->root = NULL;
		tmp->root_table = NULL;
		tmp->registrar = ast_strdup(registrar);
		tmp->includes = NULL;
		tmp->ignorepats = NULL;
		tmp->refcount = 1;
	} else {
		ast_log(LOG_ERROR, "Danger! We failed to allocate a context for %s!\n", name);
		return NULL;
	}

	if (!extcontexts) {
		ast_wrlock_contexts();
		tmp->next = *local_contexts;
		*local_contexts = tmp;
		ast_hashtab_insert_safe(contexts_table, tmp); /*put this context into the tree */
		ast_unlock_contexts();
		ast_debug(1, "Registered context '%s'(%p) in table %p registrar: %s\n", tmp->name, tmp, contexts_table, registrar);
		ast_verb(3, "Registered extension context '%s'; registrar: %s\n", tmp->name, registrar);
	} else {
		tmp->next = *local_contexts;
		if (exttable)
			ast_hashtab_insert_immediate(exttable, tmp); /*put this context into the tree */

		*local_contexts = tmp;
		ast_debug(1, "Registered context '%s'(%p) in local table %p; registrar: %s\n", tmp->name, tmp, exttable, registrar);
		ast_verb(3, "Registered extension context '%s'; registrar: %s\n", tmp->name, registrar);
	}
	return tmp;
}

void __ast_context_destroy(struct ast_context *list, struct ast_hashtab *contexttab, struct ast_context *con, const char *registrar);

struct store_hint {
	char *context;
	char *exten;
	AST_LIST_HEAD_NOLOCK(, ast_state_cb) callbacks;
	int laststate;
	int last_presence_state;
	char *last_presence_subtype;
	char *last_presence_message;

	AST_LIST_ENTRY(store_hint) list;
	char data[1];
};

AST_LIST_HEAD_NOLOCK(store_hints, store_hint);

static void context_merge_incls_swits_igps_other_registrars(struct ast_context *new, struct ast_context *old, const char *registrar)
{
	struct ast_include *i;
	struct ast_ignorepat *ip;
	struct ast_sw *sw;

	ast_verb(3, "merging incls/swits/igpats from old(%s) to new(%s) context, registrar = %s\n", ast_get_context_name(old), ast_get_context_name(new), registrar);
	/* copy in the includes, switches, and ignorepats */
	/* walk through includes */
	for (i = NULL; (i = ast_walk_context_includes(old, i)) ; ) {
		if (strcmp(ast_get_include_registrar(i), registrar) == 0)
			continue; /* not mine */
		ast_context_add_include2(new, ast_get_include_name(i), ast_get_include_registrar(i));
	}

	/* walk through switches */
	for (sw = NULL; (sw = ast_walk_context_switches(old, sw)) ; ) {
		if (strcmp(ast_get_switch_registrar(sw), registrar) == 0)
			continue; /* not mine */
		ast_context_add_switch2(new, ast_get_switch_name(sw), ast_get_switch_data(sw), ast_get_switch_eval(sw), ast_get_switch_registrar(sw));
	}

	/* walk thru ignorepats ... */
	for (ip = NULL; (ip = ast_walk_context_ignorepats(old, ip)); ) {
		if (strcmp(ast_get_ignorepat_registrar(ip), registrar) == 0)
			continue; /* not mine */
		ast_context_add_ignorepat2(new, ast_get_ignorepat_name(ip), ast_get_ignorepat_registrar(ip));
	}
}


/* the purpose of this routine is to duplicate a context, with all its substructure,
   except for any extens that have a matching registrar */
static void context_merge(struct ast_context **extcontexts, struct ast_hashtab *exttable, struct ast_context *context, const char *registrar)
{
	struct ast_context *new = ast_hashtab_lookup(exttable, context); /* is there a match in the new set? */
	struct ast_exten *exten_item, *prio_item, *new_exten_item, *new_prio_item;
	struct ast_hashtab_iter *exten_iter;
	struct ast_hashtab_iter *prio_iter;
	int insert_count = 0;
	int first = 1;

	/* We'll traverse all the extensions/prios, and see which are not registrar'd with
	   the current registrar, and copy them to the new context. If the new context does not
	   exist, we'll create it "on demand". If no items are in this context to copy, then we'll
	   only create the empty matching context if the old one meets the criteria */

	if (context->root_table) {
		exten_iter = ast_hashtab_start_traversal(context->root_table);
		while ((exten_item=ast_hashtab_next(exten_iter))) {
			if (new) {
				new_exten_item = ast_hashtab_lookup(new->root_table, exten_item);
			} else {
				new_exten_item = NULL;
			}
			prio_iter = ast_hashtab_start_traversal(exten_item->peer_table);
			while ((prio_item=ast_hashtab_next(prio_iter))) {
				int res1;
				char *dupdstr;

				if (new_exten_item) {
					new_prio_item = ast_hashtab_lookup(new_exten_item->peer_table, prio_item);
				} else {
					new_prio_item = NULL;
				}
				if (strcmp(prio_item->registrar,registrar) == 0) {
					continue;
				}
				/* make sure the new context exists, so we have somewhere to stick this exten/prio */
				if (!new) {
					new = ast_context_find_or_create(extcontexts, exttable, context->name, prio_item->registrar); /* a new context created via priority from a different context in the old dialplan, gets its registrar from the prio's registrar */
				}

				/* copy in the includes, switches, and ignorepats */
				if (first) { /* but, only need to do this once */
					context_merge_incls_swits_igps_other_registrars(new, context, registrar);
					first = 0;
				}

				if (!new) {
					ast_log(LOG_ERROR,"Could not allocate a new context for %s in merge_and_delete! Danger!\n", context->name);
					ast_hashtab_end_traversal(prio_iter);
					ast_hashtab_end_traversal(exten_iter);
					return; /* no sense continuing. */
				}
				/* we will not replace existing entries in the new context with stuff from the old context.
				   but, if this is because of some sort of registrar conflict, we ought to say something... */

				dupdstr = ast_strdup(prio_item->data);

				res1 = ast_add_extension2(new, 0, prio_item->exten, prio_item->priority, prio_item->label,
										  prio_item->matchcid ? prio_item->cidmatch : NULL, prio_item->app, dupdstr, ast_free_ptr, prio_item->registrar);
				if (!res1 && new_exten_item && new_prio_item){
					ast_verb(3,"Dropping old dialplan item %s/%s/%d [%s(%s)] (registrar=%s) due to conflict with new dialplan\n",
							context->name, prio_item->exten, prio_item->priority, prio_item->app, (char*)prio_item->data, prio_item->registrar);
				} else {
					/* we do NOT pass the priority data from the old to the new -- we pass a copy of it, so no changes to the current dialplan take place,
					 and no double frees take place, either! */
					insert_count++;
				}
			}
			ast_hashtab_end_traversal(prio_iter);
		}
		ast_hashtab_end_traversal(exten_iter);
	} else if (new) {
		/* If the context existed but had no extensions, we still want to merge
		 * the includes, switches and ignore patterns.
		 */
		context_merge_incls_swits_igps_other_registrars(new, context, registrar);
	}

	if (!insert_count && !new && (strcmp(context->registrar, registrar) != 0 ||
		  (strcmp(context->registrar, registrar) == 0 && context->refcount > 1))) {
		/* we could have given it the registrar of the other module who incremented the refcount,
		   but that's not available, so we give it the registrar we know about */
		new = ast_context_find_or_create(extcontexts, exttable, context->name, context->registrar);

		/* copy in the includes, switches, and ignorepats */
		context_merge_incls_swits_igps_other_registrars(new, context, registrar);
	}
}


/* XXX this does not check that multiple contexts are merged */
void ast_merge_contexts_and_delete(struct ast_context **extcontexts, struct ast_hashtab *exttable, const char *registrar)
{
	double ft;
	struct ast_context *tmp;
	struct ast_context *oldcontextslist;
	struct ast_hashtab *oldtable;
	struct store_hints hints_stored = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct store_hints hints_removed = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct store_hint *saved_hint;
	struct ast_hint *hint;
	struct ast_exten *exten;
	int length;
	struct ast_state_cb *thiscb;
	struct ast_hashtab_iter *iter;
	struct ao2_iterator i;
	struct timeval begintime;
	struct timeval writelocktime;
	struct timeval endlocktime;
	struct timeval enddeltime;

	/*
	 * It is very important that this function hold the hints
	 * container lock _and_ the conlock during its operation; not
	 * only do we need to ensure that the list of contexts and
	 * extensions does not change, but also that no hint callbacks
	 * (watchers) are added or removed during the merge/delete
	 * process
	 *
	 * In addition, the locks _must_ be taken in this order, because
	 * there are already other code paths that use this order
	 */

	begintime = ast_tvnow();
	ast_mutex_lock(&context_merge_lock);/* Serialize ast_merge_contexts_and_delete */
	ast_wrlock_contexts();

	if (!contexts_table) {
		/* Well, that's odd. There are no contexts. */
		contexts_table = exttable;
		contexts = *extcontexts;
		ast_unlock_contexts();
		ast_mutex_unlock(&context_merge_lock);
		return;
	}

	iter = ast_hashtab_start_traversal(contexts_table);
	while ((tmp = ast_hashtab_next(iter))) {
		context_merge(extcontexts, exttable, tmp, registrar);
	}
	ast_hashtab_end_traversal(iter);

	ao2_lock(hints);
	writelocktime = ast_tvnow();

	/* preserve all watchers for hints */
	i = ao2_iterator_init(hints, AO2_ITERATOR_DONTLOCK);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		if (ao2_container_count(hint->callbacks)) {
			ao2_lock(hint);
			if (!hint->exten) {
				/* The extension has already been destroyed. (Should never happen here) */
				ao2_unlock(hint);
				continue;
			}

			length = strlen(hint->exten->exten) + strlen(hint->exten->parent->name) + 2
				+ sizeof(*saved_hint);
			if (!(saved_hint = ast_calloc(1, length))) {
				ao2_unlock(hint);
				continue;
			}

			/* This removes all the callbacks from the hint into saved_hint. */
			while ((thiscb = ao2_callback(hint->callbacks, OBJ_UNLINK, NULL, NULL))) {
				AST_LIST_INSERT_TAIL(&saved_hint->callbacks, thiscb, entry);
				/*
				 * We intentionally do not unref thiscb to account for the
				 * non-ao2 reference in saved_hint->callbacks
				 */
			}

			saved_hint->laststate = hint->laststate;
			saved_hint->context = saved_hint->data;
			strcpy(saved_hint->data, hint->exten->parent->name);
			saved_hint->exten = saved_hint->data + strlen(saved_hint->context) + 1;
			strcpy(saved_hint->exten, hint->exten->exten);
			if (hint->last_presence_subtype) {
				saved_hint->last_presence_subtype = ast_strdup(hint->last_presence_subtype);
			}
			if (hint->last_presence_message) {
				saved_hint->last_presence_message = ast_strdup(hint->last_presence_message);
			}
			saved_hint->last_presence_state = hint->last_presence_state;
			ao2_unlock(hint);
			AST_LIST_INSERT_HEAD(&hints_stored, saved_hint, list);
		}
	}
	ao2_iterator_destroy(&i);

	/* save the old table and list */
	oldtable = contexts_table;
	oldcontextslist = contexts;

	/* move in the new table and list */
	contexts_table = exttable;
	contexts = *extcontexts;

	/*
	 * Restore the watchers for hints that can be found; notify
	 * those that cannot be restored.
	 */
	while ((saved_hint = AST_LIST_REMOVE_HEAD(&hints_stored, list))) {
		struct pbx_find_info q = { .stacklen = 0 };

		exten = pbx_find_extension(NULL, NULL, &q, saved_hint->context, saved_hint->exten,
			PRIORITY_HINT, NULL, "", E_MATCH);
		/*
		 * If this is a pattern, dynamically create a new extension for this
		 * particular match.  Note that this will only happen once for each
		 * individual extension, because the pattern will no longer match first.
		 */
		if (exten && exten->exten[0] == '_') {
			ast_add_extension_nolock(exten->parent->name, 0, saved_hint->exten,
				PRIORITY_HINT, NULL, 0, exten->app, ast_strdup(exten->data), ast_free_ptr,
				exten->registrar);
			/* rwlocks are not recursive locks */
			exten = ast_hint_extension_nolock(NULL, saved_hint->context,
				saved_hint->exten);
		}

		/* Find the hint in the hints container */
		hint = exten ? ao2_find(hints, exten, 0) : NULL;
		if (!hint) {
			/*
			 * Notify watchers of this removed hint later when we aren't
			 * encumberd by so many locks.
			 */
			AST_LIST_INSERT_HEAD(&hints_removed, saved_hint, list);
		} else {
			ao2_lock(hint);
			while ((thiscb = AST_LIST_REMOVE_HEAD(&saved_hint->callbacks, entry))) {
				ao2_link(hint->callbacks, thiscb);
				/* Ref that we added when putting into saved_hint->callbacks */
				ao2_ref(thiscb, -1);
			}
			hint->laststate = saved_hint->laststate;
			hint->last_presence_state = saved_hint->last_presence_state;
			hint->last_presence_subtype = saved_hint->last_presence_subtype;
			hint->last_presence_message = saved_hint->last_presence_message;
			ao2_unlock(hint);
			ao2_ref(hint, -1);
			/*
			 * The free of saved_hint->last_presence_subtype and
			 * saved_hint->last_presence_message is not necessary here.
			 */
			ast_free(saved_hint);
		}
	}

	ao2_unlock(hints);
	ast_unlock_contexts();

	/*
	 * Notify watchers of all removed hints with the same lock
	 * environment as handle_statechange().
	 */
	while ((saved_hint = AST_LIST_REMOVE_HEAD(&hints_removed, list))) {
		/* this hint has been removed, notify the watchers */
		while ((thiscb = AST_LIST_REMOVE_HEAD(&saved_hint->callbacks, entry))) {
			execute_state_callback(thiscb->change_cb,
				saved_hint->context,
				saved_hint->exten,
				thiscb->data,
				AST_HINT_UPDATE_DEVICE,
				NULL,
				NULL);
			/* Ref that we added when putting into saved_hint->callbacks */
			ao2_ref(thiscb, -1);
		}
		ast_free(saved_hint->last_presence_subtype);
		ast_free(saved_hint->last_presence_message);
		ast_free(saved_hint);
	}

	ast_mutex_unlock(&context_merge_lock);
	endlocktime = ast_tvnow();

	/*
	 * The old list and hashtab no longer are relevant, delete them
	 * while the rest of asterisk is now freely using the new stuff
	 * instead.
	 */

	ast_hashtab_destroy(oldtable, NULL);

	for (tmp = oldcontextslist; tmp; ) {
		struct ast_context *next;	/* next starting point */

		next = tmp->next;
		__ast_internal_context_destroy(tmp);
		tmp = next;
	}
	enddeltime = ast_tvnow();

	ft = ast_tvdiff_us(writelocktime, begintime);
	ft /= 1000000.0;
	ast_verb(3,"Time to scan old dialplan and merge leftovers back into the new: %8.6f sec\n", ft);

	ft = ast_tvdiff_us(endlocktime, writelocktime);
	ft /= 1000000.0;
	ast_verb(3,"Time to restore hints and swap in new dialplan: %8.6f sec\n", ft);

	ft = ast_tvdiff_us(enddeltime, endlocktime);
	ft /= 1000000.0;
	ast_verb(3,"Time to delete the old dialplan: %8.6f sec\n", ft);

	ft = ast_tvdiff_us(enddeltime, begintime);
	ft /= 1000000.0;
	ast_verb(3,"Total time merge_contexts_delete: %8.6f sec\n", ft);
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int ast_context_add_include(const char *context, const char *include, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_context_add_include2(c, include, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

/*! \brief Helper for get_range.
 * return the index of the matching entry, starting from 1.
 * If names is not supplied, try numeric values.
 */
static int lookup_name(const char *s, const char * const names[], int max)
{
	int i;

	if (names && *s > '9') {
		for (i = 0; names[i]; i++) {
			if (!strcasecmp(s, names[i])) {
				return i;
			}
		}
	}

	/* Allow months and weekdays to be specified as numbers, as well */
	if (sscanf(s, "%2d", &i) == 1 && i >= 1 && i <= max) {
		/* What the array offset would have been: "1" would be at offset 0 */
		return i - 1;
	}
	return -1; /* error return */
}

/*! \brief helper function to return a range up to max (7, 12, 31 respectively).
 * names, if supplied, is an array of names that should be mapped to numbers.
 */
static unsigned get_range(char *src, int max, const char * const names[], const char *msg)
{
	int start, end; /* start and ending position */
	unsigned int mask = 0;
	char *part;

	/* Check for whole range */
	if (ast_strlen_zero(src) || !strcmp(src, "*")) {
		return (1 << max) - 1;
	}

	while ((part = strsep(&src, "&"))) {
		/* Get start and ending position */
		char *endpart = strchr(part, '-');
		if (endpart) {
			*endpart++ = '\0';
		}
		/* Find the start */
		if ((start = lookup_name(part, names, max)) < 0) {
			ast_log(LOG_WARNING, "Invalid %s '%s', skipping element\n", msg, part);
			continue;
		}
		if (endpart) { /* find end of range */
			if ((end = lookup_name(endpart, names, max)) < 0) {
				ast_log(LOG_WARNING, "Invalid end %s '%s', skipping element\n", msg, endpart);
				continue;
			}
		} else {
			end = start;
		}
		/* Fill the mask. Remember that ranges are cyclic */
		mask |= (1 << end);   /* initialize with last element */
		while (start != end) {
			mask |= (1 << start);
			if (++start >= max) {
				start = 0;
			}
		}
	}
	return mask;
}

/*! \brief store a bitmask of valid times, one bit each 1 minute */
static void get_timerange(struct ast_timing *i, char *times)
{
	char *endpart, *part;
	int x;
	int st_h, st_m;
	int endh, endm;
	int minute_start, minute_end;

	/* start disabling all times, fill the fields with 0's, as they may contain garbage */
	memset(i->minmask, 0, sizeof(i->minmask));

	/* 1-minute per bit */
	/* Star is all times */
	if (ast_strlen_zero(times) || !strcmp(times, "*")) {
		/* 48, because each hour takes 2 integers; 30 bits each */
		for (x = 0; x < 48; x++) {
			i->minmask[x] = 0x3fffffff; /* 30 bits */
		}
		return;
	}
	/* Otherwise expect a range */
	while ((part = strsep(&times, "&"))) {
		if (!(endpart = strchr(part, '-'))) {
			if (sscanf(part, "%2d:%2d", &st_h, &st_m) != 2 || st_h < 0 || st_h > 23 || st_m < 0 || st_m > 59) {
				ast_log(LOG_WARNING, "%s isn't a valid time.\n", part);
				continue;
			}
			i->minmask[st_h * 2 + (st_m >= 30 ? 1 : 0)] |= (1 << (st_m % 30));
			continue;
		}
		*endpart++ = '\0';
		/* why skip non digits? Mostly to skip spaces */
		while (*endpart && !isdigit(*endpart)) {
			endpart++;
		}
		if (!*endpart) {
			ast_log(LOG_WARNING, "Invalid time range starting with '%s-'.\n", part);
			continue;
		}
		if (sscanf(part, "%2d:%2d", &st_h, &st_m) != 2 || st_h < 0 || st_h > 23 || st_m < 0 || st_m > 59) {
			ast_log(LOG_WARNING, "'%s' isn't a valid start time.\n", part);
			continue;
		}
		if (sscanf(endpart, "%2d:%2d", &endh, &endm) != 2 || endh < 0 || endh > 23 || endm < 0 || endm > 59) {
			ast_log(LOG_WARNING, "'%s' isn't a valid end time.\n", endpart);
			continue;
		}
		minute_start = st_h * 60 + st_m;
		minute_end = endh * 60 + endm;
		/* Go through the time and enable each appropriate bit */
		for (x = minute_start; x != minute_end; x = (x + 1) % (24 * 60)) {
			i->minmask[x / 30] |= (1 << (x % 30));
		}
		/* Do the last one */
		i->minmask[x / 30] |= (1 << (x % 30));
	}
	/* All done */
	return;
}

static const char * const days[] =
{
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",
	NULL,
};

static const char * const months[] =
{
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",
	NULL,
};

int ast_build_timing(struct ast_timing *i, const char *info_in)
{
	char *info;
	int j, num_fields, last_sep = -1;

	i->timezone = NULL;

	/* Check for empty just in case */
	if (ast_strlen_zero(info_in)) {
		return 0;
	}

	/* make a copy just in case we were passed a static string */
	info = ast_strdupa(info_in);

	/* count the number of fields in the timespec */
	for (j = 0, num_fields = 1; info[j] != '\0'; j++) {
		if (info[j] == ',') {
			last_sep = j;
			num_fields++;
		}
	}

	/* save the timezone, if it is specified */
	if (num_fields == 5) {
		i->timezone = ast_strdup(info + last_sep + 1);
	}

	/* Assume everything except time */
	i->monthmask = 0xfff;	/* 12 bits */
	i->daymask = 0x7fffffffU; /* 31 bits */
	i->dowmask = 0x7f; /* 7 bits */
	/* on each call, use strsep() to move info to the next argument */
	get_timerange(i, strsep(&info, "|,"));
	if (info)
		i->dowmask = get_range(strsep(&info, "|,"), 7, days, "day of week");
	if (info)
		i->daymask = get_range(strsep(&info, "|,"), 31, NULL, "day");
	if (info)
		i->monthmask = get_range(strsep(&info, "|,"), 12, months, "month");
	return 1;
}

int ast_check_timing(const struct ast_timing *i)
{
	return ast_check_timing2(i, ast_tvnow());
}

int ast_check_timing2(const struct ast_timing *i, const struct timeval tv)
{
	struct ast_tm tm;

	ast_localtime(&tv, &tm, i->timezone);

	/* If it's not the right month, return */
	if (!(i->monthmask & (1 << tm.tm_mon)))
		return 0;

	/* If it's not that time of the month.... */
	/* Warning, tm_mday has range 1..31! */
	if (!(i->daymask & (1 << (tm.tm_mday-1))))
		return 0;

	/* If it's not the right day of the week */
	if (!(i->dowmask & (1 << tm.tm_wday)))
		return 0;

	/* Sanity check the hour just to be safe */
	if ((tm.tm_hour < 0) || (tm.tm_hour > 23)) {
		ast_log(LOG_WARNING, "Insane time...\n");
		return 0;
	}

	/* Now the tough part, we calculate if it fits
	   in the right time based on min/hour */
	if (!(i->minmask[tm.tm_hour * 2 + (tm.tm_min >= 30 ? 1 : 0)] & (1 << (tm.tm_min >= 30 ? tm.tm_min - 30 : tm.tm_min))))
		return 0;

	/* If we got this far, then we're good */
	return 1;
}

int ast_destroy_timing(struct ast_timing *i)
{
	if (i->timezone) {
		ast_free(i->timezone);
		i->timezone = NULL;
	}
	return 0;
}
/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int ast_context_add_include2(struct ast_context *con, const char *value,
	const char *registrar)
{
	struct ast_include *new_include;
	char *c;
	struct ast_include *i, *il = NULL; /* include, include_last */
	int length;
	char *p;

	length = sizeof(struct ast_include);
	length += 2 * (strlen(value) + 1);

	/* allocate new include structure ... */
	if (!(new_include = ast_calloc(1, length)))
		return -1;
	/* Fill in this structure. Use 'p' for assignments, as the fields
	 * in the structure are 'const char *'
	 */
	p = new_include->stuff;
	new_include->name = p;
	strcpy(p, value);
	p += strlen(value) + 1;
	new_include->rname = p;
	strcpy(p, value);
	/* Strip off timing info, and process if it is there */
	if ( (c = strchr(p, ',')) ) {
		*c++ = '\0';
		new_include->hastime = ast_build_timing(&(new_include->timing), c);
	}
	new_include->next      = NULL;
	new_include->registrar = registrar;

	ast_wrlock_context(con);

	/* ... go to last include and check if context is already included too... */
	for (i = con->includes; i; i = i->next) {
		if (!strcasecmp(i->name, new_include->name)) {
			ast_destroy_timing(&(new_include->timing));
			ast_free(new_include);
			ast_unlock_context(con);
			errno = EEXIST;
			return -1;
		}
		il = i;
	}

	/* ... include new context into context list, unlock, return */
	if (il)
		il->next = new_include;
	else
		con->includes = new_include;
	ast_verb(3, "Including context '%s' in context '%s'\n", new_include->name, ast_get_context_name(con));

	ast_unlock_context(con);

	return 0;
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENOENT - no existence of context
 */
int ast_context_add_switch(const char *context, const char *sw, const char *data, int eval, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) { /* found, add switch to this context */
		ret = ast_context_add_switch2(c, sw, data, eval, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int ast_context_add_switch2(struct ast_context *con, const char *value,
	const char *data, int eval, const char *registrar)
{
	struct ast_sw *new_sw;
	struct ast_sw *i;
	int length;
	char *p;

	length = sizeof(struct ast_sw);
	length += strlen(value) + 1;
	if (data)
		length += strlen(data);
	length++;

	/* allocate new sw structure ... */
	if (!(new_sw = ast_calloc(1, length)))
		return -1;
	/* ... fill in this structure ... */
	p = new_sw->stuff;
	new_sw->name = p;
	strcpy(new_sw->name, value);
	p += strlen(value) + 1;
	new_sw->data = p;
	if (data) {
		strcpy(new_sw->data, data);
		p += strlen(data) + 1;
	} else {
		strcpy(new_sw->data, "");
		p++;
	}
	new_sw->eval	  = eval;
	new_sw->registrar = registrar;

	/* ... try to lock this context ... */
	ast_wrlock_context(con);

	/* ... go to last sw and check if context is already swd too... */
	AST_LIST_TRAVERSE(&con->alts, i, list) {
		if (!strcasecmp(i->name, new_sw->name) && !strcasecmp(i->data, new_sw->data)) {
			ast_free(new_sw);
			ast_unlock_context(con);
			errno = EEXIST;
			return -1;
		}
	}

	/* ... sw new context into context list, unlock, return */
	AST_LIST_INSERT_TAIL(&con->alts, new_sw, list);

	ast_verb(3, "Including switch '%s/%s' in context '%s'\n", new_sw->name, new_sw->data, ast_get_context_name(con));

	ast_unlock_context(con);

	return 0;
}

/*
 * EBUSY  - can't lock
 * ENOENT - there is not context existence
 */
int ast_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_context_remove_ignorepat2(c, ignorepat, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_context_remove_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar)
{
	struct ast_ignorepat *ip, *ipl = NULL;

	ast_wrlock_context(con);

	for (ip = con->ignorepats; ip; ip = ip->next) {
		if (!strcmp(ip->pattern, ignorepat) &&
			(!registrar || (registrar == ip->registrar))) {
			if (ipl) {
				ipl->next = ip->next;
				ast_free(ip);
			} else {
				con->ignorepats = ip->next;
				ast_free(ip);
			}
			ast_unlock_context(con);
			return 0;
		}
		ipl = ip;
	}

	ast_unlock_context(con);
	errno = EINVAL;
	return -1;
}

/*
 * EBUSY - can't lock
 * ENOENT - there is no existence of context
 */
int ast_context_add_ignorepat(const char *context, const char *value, const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_context_add_ignorepat2(c, value, registrar);
		ast_unlock_contexts();
	}
	return ret;
}

int ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	struct ast_ignorepat *ignorepat, *ignorepatc, *ignorepatl = NULL;
	int length;
	char *pattern;
	length = sizeof(struct ast_ignorepat);
	length += strlen(value) + 1;
	if (!(ignorepat = ast_calloc(1, length)))
		return -1;
	/* The cast to char * is because we need to write the initial value.
	 * The field is not supposed to be modified otherwise.  Also, gcc 4.2
	 * sees the cast as dereferencing a type-punned pointer and warns about
	 * it.  This is the workaround (we're telling gcc, yes, that's really
	 * what we wanted to do).
	 */
	pattern = (char *) ignorepat->pattern;
	strcpy(pattern, value);
	ignorepat->next = NULL;
	ignorepat->registrar = registrar;
	ast_wrlock_context(con);
	for (ignorepatc = con->ignorepats; ignorepatc; ignorepatc = ignorepatc->next) {
		ignorepatl = ignorepatc;
		if (!strcasecmp(ignorepatc->pattern, value)) {
			/* Already there */
			ast_unlock_context(con);
			ast_free(ignorepat);
			errno = EEXIST;
			return -1;
		}
	}
	if (ignorepatl)
		ignorepatl->next = ignorepat;
	else
		con->ignorepats = ignorepat;
	ast_unlock_context(con);
	return 0;

}

int ast_ignore_pattern(const char *context, const char *pattern)
{
	struct ast_context *con = ast_context_find(context);

	if (con) {
		struct ast_ignorepat *pat;

		for (pat = con->ignorepats; pat; pat = pat->next) {
			if (ast_extension_match(pat->pattern, pattern))
				return 1;
		}
	}

	return 0;
}

/*
 * ast_add_extension_nolock -- use only in situations where the conlock is already held
 * ENOENT  - no existence of context
 *
 */
static int ast_add_extension_nolock(const char *context, int replace, const char *extension,
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context(context);
	if (c) {
		ret = ast_add_extension2_lockopt(c, replace, extension, priority, label, callerid,
			application, data, datad, registrar, 1);
	}

	return ret;
}
/*
 * EBUSY   - can't lock
 * ENOENT  - no existence of context
 *
 */
int ast_add_extension(const char *context, int replace, const char *extension,
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar)
{
	int ret = -1;
	struct ast_context *c;

	c = find_context_locked(context);
	if (c) {
		ret = ast_add_extension2(c, replace, extension, priority, label, callerid,
			application, data, datad, registrar);
		ast_unlock_contexts();
	}

	return ret;
}

int ast_explicit_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	if (!chan)
		return -1;

	ast_channel_lock(chan);

	if (!ast_strlen_zero(context))
		ast_channel_context_set(chan, context);
	if (!ast_strlen_zero(exten))
		ast_channel_exten_set(chan, exten);
	if (priority > -1) {
		/* see flag description in channel.h for explanation */
		if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP)) {
			--priority;
		}
		ast_channel_priority_set(chan, priority);
	}

	ast_channel_unlock(chan);

	return 0;
}

int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	int res = 0;
	struct ast_channel *tmpchan;
	struct {
		char *accountcode;
		char *exten;
		char *context;
		char *linkedid;
		char *name;
		struct ast_cdr *cdr;
		int amaflags;
		int state;
		struct ast_format readformat;
		struct ast_format writeformat;
	} tmpvars = { 0, };

	ast_channel_lock(chan);
	if (ast_channel_pbx(chan)) { /* This channel is currently in the PBX */
		ast_explicit_goto(chan, context, exten, priority + 1);
		ast_softhangup_nolock(chan, AST_SOFTHANGUP_ASYNCGOTO);
		ast_channel_unlock(chan);
		return res;
	}

	/* In order to do it when the channel doesn't really exist within
	 * the PBX, we have to make a new channel, masquerade, and start the PBX
	 * at the new location */
	tmpvars.accountcode = ast_strdupa(ast_channel_accountcode(chan));
	tmpvars.exten = ast_strdupa(ast_channel_exten(chan));
	tmpvars.context = ast_strdupa(ast_channel_context(chan));
	tmpvars.linkedid = ast_strdupa(ast_channel_linkedid(chan));
	tmpvars.name = ast_strdupa(ast_channel_name(chan));
	tmpvars.amaflags = ast_channel_amaflags(chan);
	tmpvars.state = ast_channel_state(chan);
	ast_format_copy(&tmpvars.writeformat, ast_channel_writeformat(chan));
	ast_format_copy(&tmpvars.readformat, ast_channel_readformat(chan));
	tmpvars.cdr = ast_channel_cdr(chan) ? ast_cdr_dup(ast_channel_cdr(chan)) : NULL;

	ast_channel_unlock(chan);

	/* Do not hold any channel locks while calling channel_alloc() since the function
	 * locks the channel container when linking the new channel in. */
	if (!(tmpchan = ast_channel_alloc(0, tmpvars.state, 0, 0, tmpvars.accountcode, tmpvars.exten, tmpvars.context, tmpvars.linkedid, tmpvars.amaflags, "AsyncGoto/%s", tmpvars.name))) {
		ast_cdr_discard(tmpvars.cdr);
		return -1;
	}

	/* copy the cdr info over */
	if (tmpvars.cdr) {
		ast_cdr_discard(ast_channel_cdr(tmpchan));
		ast_channel_cdr_set(tmpchan, tmpvars.cdr);
		tmpvars.cdr = NULL;
	}

	/* Make formats okay */
	ast_format_copy(ast_channel_readformat(tmpchan), &tmpvars.readformat);
	ast_format_copy(ast_channel_writeformat(tmpchan), &tmpvars.writeformat);

	/* Setup proper location. Never hold another channel lock while calling this function. */
	ast_explicit_goto(tmpchan, S_OR(context, tmpvars.context), S_OR(exten, tmpvars.exten), priority);

	/* Masquerade into tmp channel */
	if (ast_channel_masquerade(tmpchan, chan)) {
		/* Failed to set up the masquerade.  It's probably chan_local
		 * in the middle of optimizing itself out.  Sad. :( */
		ast_hangup(tmpchan);
		tmpchan = NULL;
		res = -1;
	} else {
		ast_do_masquerade(tmpchan);
		/* Start the PBX going on our stolen channel */
		if (ast_pbx_start(tmpchan)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmpchan));
			ast_hangup(tmpchan);
			res = -1;
		}
	}

	return res;
}

int ast_async_goto_by_name(const char *channame, const char *context, const char *exten, int priority)
{
	struct ast_channel *chan;
	int res = -1;

	if ((chan = ast_channel_get_by_name(channame))) {
		res = ast_async_goto(chan, context, exten, priority);
		chan = ast_channel_unref(chan);
	}

	return res;
}

/*! \brief copy a string skipping whitespace */
static int ext_strncpy(char *dst, const char *src, int len)
{
	int count = 0;
	int insquares = 0;

	while (*src && (count < len - 1)) {
		if (*src == '[') {
			insquares = 1;
		} else if (*src == ']') {
			insquares = 0;
		} else if (*src == ' ' && !insquares) {
			src++;
			continue;
		}
		*dst = *src;
		dst++;
		src++;
		count++;
	}
	*dst = '\0';

	return count;
}

/*!
 * \brief add the extension in the priority chain.
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int add_priority(struct ast_context *con, struct ast_exten *tmp,
	struct ast_exten *el, struct ast_exten *e, int replace)
{
	struct ast_exten *ep;
	struct ast_exten *eh=e;
	int repeated_label = 0; /* Track if this label is a repeat, assume no. */

	for (ep = NULL; e ; ep = e, e = e->peer) {
		if (e->label && tmp->label && e->priority != tmp->priority && !strcmp(e->label, tmp->label)) {
			if (strcmp(e->exten, tmp->exten)) {
				ast_log(LOG_WARNING,
					"Extension '%s' priority %d in '%s', label '%s' already in use at aliased extension '%s' priority %d\n",
					tmp->exten, tmp->priority, con->name, tmp->label, e->exten, e->priority);
			} else {
				ast_log(LOG_WARNING,
					"Extension '%s' priority %d in '%s', label '%s' already in use at priority %d\n",
					tmp->exten, tmp->priority, con->name, tmp->label, e->priority);
			}
			repeated_label = 1;
		}
		if (e->priority >= tmp->priority) {
			break;
		}
	}

	if (repeated_label) {	/* Discard the label since it's a repeat. */
		tmp->label = NULL;
	}

	if (!e) {	/* go at the end, and ep is surely set because the list is not empty */
		ast_hashtab_insert_safe(eh->peer_table, tmp);

		if (tmp->label) {
			ast_hashtab_insert_safe(eh->peer_label_table, tmp);
		}
		ep->peer = tmp;
		return 0;	/* success */
	}
	if (e->priority == tmp->priority) {
		/* Can't have something exactly the same.  Is this a
		   replacement?  If so, replace, otherwise, bonk. */
		if (!replace) {
			if (strcmp(e->exten, tmp->exten)) {
				ast_log(LOG_WARNING,
					"Unable to register extension '%s' priority %d in '%s', already in use by aliased extension '%s'\n",
					tmp->exten, tmp->priority, con->name, e->exten);
			} else {
				ast_log(LOG_WARNING,
					"Unable to register extension '%s' priority %d in '%s', already in use\n",
					tmp->exten, tmp->priority, con->name);
			}

			return -1;
		}
		/* we are replacing e, so copy the link fields and then update
		 * whoever pointed to e to point to us
		 */
		tmp->next = e->next;	/* not meaningful if we are not first in the peer list */
		tmp->peer = e->peer;	/* always meaningful */
		if (ep)	{		/* We're in the peer list, just insert ourselves */
			ast_hashtab_remove_object_via_lookup(eh->peer_table,e);

			if (e->label) {
				ast_hashtab_remove_object_via_lookup(eh->peer_label_table,e);
			}

			ast_hashtab_insert_safe(eh->peer_table,tmp);
			if (tmp->label) {
				ast_hashtab_insert_safe(eh->peer_label_table,tmp);
			}

			ep->peer = tmp;
		} else if (el) {		/* We're the first extension. Take over e's functions */
			struct match_char *x = add_exten_to_pattern_tree(con, e, 1);
			tmp->peer_table = e->peer_table;
			tmp->peer_label_table = e->peer_label_table;
			ast_hashtab_remove_object_via_lookup(tmp->peer_table,e);
			ast_hashtab_insert_safe(tmp->peer_table,tmp);
			if (e->label) {
				ast_hashtab_remove_object_via_lookup(tmp->peer_label_table, e);
			}
			if (tmp->label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}

			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			el->next = tmp;
			/* The pattern trie points to this exten; replace the pointer,
			   and all will be well */
			if (x) { /* if the trie isn't formed yet, don't sweat this */
				if (x->exten) { /* this test for safety purposes */
					x->exten = tmp; /* replace what would become a bad pointer */
				} else {
					ast_log(LOG_ERROR,"Trying to delete an exten from a context, but the pattern tree node returned isn't an extension\n");
				}
			}
		} else {			/* We're the very first extension.  */
			struct match_char *x = add_exten_to_pattern_tree(con, e, 1);
			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			tmp->peer_table = e->peer_table;
			tmp->peer_label_table = e->peer_label_table;
			ast_hashtab_remove_object_via_lookup(tmp->peer_table, e);
			ast_hashtab_insert_safe(tmp->peer_table, tmp);
			if (e->label) {
				ast_hashtab_remove_object_via_lookup(tmp->peer_label_table, e);
			}
			if (tmp->label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}

			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			con->root = tmp;
			/* The pattern trie points to this exten; replace the pointer,
			   and all will be well */
			if (x) { /* if the trie isn't formed yet; no problem */
				if (x->exten) { /* this test for safety purposes */
					x->exten = tmp; /* replace what would become a bad pointer */
				} else {
					ast_log(LOG_ERROR,"Trying to delete an exten from a context, but the pattern tree node returned isn't an extension\n");
				}
			}
		}
		if (tmp->priority == PRIORITY_HINT)
			ast_change_hint(e,tmp);
		/* Destroy the old one */
		if (e->datad)
			e->datad(e->data);
		ast_free(e);
	} else {	/* Slip ourselves in just before e */
		tmp->peer = e;
		tmp->next = e->next;	/* extension chain, or NULL if e is not the first extension */
		if (ep) {			/* Easy enough, we're just in the peer list */
			if (tmp->label) {
				ast_hashtab_insert_safe(eh->peer_label_table, tmp);
			}
			ast_hashtab_insert_safe(eh->peer_table, tmp);
			ep->peer = tmp;
		} else {			/* we are the first in some peer list, so link in the ext list */
			tmp->peer_table = e->peer_table;
			tmp->peer_label_table = e->peer_label_table;
			e->peer_table = 0;
			e->peer_label_table = 0;
			ast_hashtab_insert_safe(tmp->peer_table, tmp);
			if (tmp->label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}
			ast_hashtab_remove_object_via_lookup(con->root_table, e);
			ast_hashtab_insert_safe(con->root_table, tmp);
			if (el)
				el->next = tmp;	/* in the middle... */
			else
				con->root = tmp; /* ... or at the head */
			e->next = NULL;	/* e is no more at the head, so e->next must be reset */
		}
		/* And immediately return success. */
		if (tmp->priority == PRIORITY_HINT) {
			ast_add_hint(tmp);
		}
	}
	return 0;
}

/*! \brief
 * Main interface to add extensions to the list for out context.
 *
 * We sort extensions in order of matching preference, so that we can
 * stop the search as soon as we find a suitable match.
 * This ordering also takes care of wildcards such as '.' (meaning
 * "one or more of any character") and '!' (which is 'earlymatch',
 * meaning "zero or more of any character" but also impacts the
 * return value from CANMATCH and EARLYMATCH.
 *
 * The extension match rules defined in the devmeeting 2006.05.05 are
 * quite simple: WE SELECT THE LONGEST MATCH.
 * In detail, "longest" means the number of matched characters in
 * the extension. In case of ties (e.g. _XXX and 333) in the length
 * of a pattern, we give priority to entries with the smallest cardinality
 * (e.g, [5-9] comes before [2-8] before the former has only 5 elements,
 * while the latter has 7, etc.
 * In case of same cardinality, the first element in the range counts.
 * If we still have a tie, any final '!' will make this as a possibly
 * less specific pattern.
 *
 * EBUSY - can't lock
 * EEXIST - extension with the same priority exist and no replace is set
 *
 */
int ast_add_extension2(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar)
{
	return ast_add_extension2_lockopt(con, replace, extension, priority, label, callerid,
		application, data, datad, registrar, 1);
}

/*!
 * \brief Same as ast_add_extension2() but controls the context locking.
 *
 * \details
 * Does all the work of ast_add_extension2, but adds an arg to
 * determine if context locking should be done.
 */
static int ast_add_extension2_lockopt(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar, int lock_context)
{
	/*
	 * Sort extensions (or patterns) according to the rules indicated above.
	 * These are implemented by the function ext_cmp()).
	 * All priorities for the same ext/pattern/cid are kept in a list,
	 * using the 'peer' field  as a link field..
	 */
	struct ast_exten *tmp, *tmp2, *e, *el = NULL;
	int res;
	int length;
	char *p;
	char expand_buf[VAR_BUF_SIZE];
	struct ast_exten dummy_exten = {0};
	char dummy_name[1024];

	if (ast_strlen_zero(extension)) {
		ast_log(LOG_ERROR,"You have to be kidding-- add exten '' to context %s? Figure out a name and call me back. Action ignored.\n",
				con->name);
		return -1;
	}

	/* If we are adding a hint evalulate in variables and global variables */
	if (priority == PRIORITY_HINT && strstr(application, "${") && extension[0] != '_') {
		struct ast_channel *c = ast_dummy_channel_alloc();

		if (c) {
			ast_channel_exten_set(c, extension);
			ast_channel_context_set(c, con->name);
		}
		pbx_substitute_variables_helper(c, application, expand_buf, sizeof(expand_buf));
		application = expand_buf;
		if (c) {
			ast_channel_unref(c);
		}
	}

	length = sizeof(struct ast_exten);
	length += strlen(extension) + 1;
	length += strlen(application) + 1;
	if (label)
		length += strlen(label) + 1;
	if (callerid)
		length += strlen(callerid) + 1;
	else
		length ++;	/* just the '\0' */

	/* Be optimistic:  Build the extension structure first */
	if (!(tmp = ast_calloc(1, length)))
		return -1;

	if (ast_strlen_zero(label)) /* let's turn empty labels to a null ptr */
		label = 0;

	/* use p as dst in assignments, as the fields are const char * */
	p = tmp->stuff;
	if (label) {
		tmp->label = p;
		strcpy(p, label);
		p += strlen(label) + 1;
	}
	tmp->exten = p;
	p += ext_strncpy(p, extension, strlen(extension) + 1) + 1;
	tmp->priority = priority;
	tmp->cidmatch = p;	/* but use p for assignments below */

	/* Blank callerid and NULL callerid are two SEPARATE things.  Do NOT confuse the two!!! */
	if (callerid) {
		p += ext_strncpy(p, callerid, strlen(callerid) + 1) + 1;
		tmp->matchcid = AST_EXT_MATCHCID_ON;
	} else {
		*p++ = '\0';
		tmp->matchcid = AST_EXT_MATCHCID_OFF;
	}
	tmp->app = p;
	strcpy(p, application);
	tmp->parent = con;
	tmp->data = data;
	tmp->datad = datad;
	tmp->registrar = registrar;

	if (lock_context) {
		ast_wrlock_context(con);
	}

	if (con->pattern_tree) { /* usually, on initial load, the pattern_tree isn't formed until the first find_exten; so if we are adding
								an extension, and the trie exists, then we need to incrementally add this pattern to it. */
		ast_copy_string(dummy_name, extension, sizeof(dummy_name));
		dummy_exten.exten = dummy_name;
		dummy_exten.matchcid = AST_EXT_MATCHCID_OFF;
		dummy_exten.cidmatch = 0;
		tmp2 = ast_hashtab_lookup(con->root_table, &dummy_exten);
		if (!tmp2) {
			/* hmmm, not in the trie; */
			add_exten_to_pattern_tree(con, tmp, 0);
			ast_hashtab_insert_safe(con->root_table, tmp); /* for the sake of completeness */
		}
	}
	res = 0; /* some compilers will think it is uninitialized otherwise */
	for (e = con->root; e; el = e, e = e->next) {   /* scan the extension list */
		res = ext_cmp(e->exten, tmp->exten);
		if (res == 0) { /* extension match, now look at cidmatch */
			if (e->matchcid == AST_EXT_MATCHCID_OFF && tmp->matchcid == AST_EXT_MATCHCID_OFF)
				res = 0;
			else if (tmp->matchcid == AST_EXT_MATCHCID_ON && e->matchcid == AST_EXT_MATCHCID_OFF)
				res = 1;
			else if (e->matchcid == AST_EXT_MATCHCID_ON && tmp->matchcid == AST_EXT_MATCHCID_OFF)
				res = -1;
			else
				res = ext_cmp(e->cidmatch, tmp->cidmatch);
		}
		if (res >= 0)
			break;
	}
	if (e && res == 0) { /* exact match, insert in the priority chain */
		res = add_priority(con, tmp, el, e, replace);
		if (res < 0) {
			if (con->pattern_tree) {
				struct match_char *x = add_exten_to_pattern_tree(con, tmp, 1);

				if (x->exten) {
					x->deleted = 1;
					x->exten = 0;
				}

				ast_hashtab_remove_this_object(con->root_table, tmp);
			}

			if (tmp->datad) {
				tmp->datad(tmp->data);
				/* if you free this, null it out */
				tmp->data = NULL;
			}

			ast_free(tmp);
		}
		if (lock_context) {
			ast_unlock_context(con);
		}
		if (res < 0) {
			errno = EEXIST;	/* XXX do we care ? */
			return 0; /* XXX should we return -1 maybe ? */
		}
	} else {
		/*
		 * not an exact match, this is the first entry with this pattern,
		 * so insert in the main list right before 'e' (if any)
		 */
		tmp->next = e;
		if (el) {  /* there is another exten already in this context */
			el->next = tmp;
			tmp->peer_table = ast_hashtab_create(13,
							hashtab_compare_exten_numbers,
							ast_hashtab_resize_java,
							ast_hashtab_newsize_java,
							hashtab_hash_priority,
							0);
			tmp->peer_label_table = ast_hashtab_create(7,
								hashtab_compare_exten_labels,
								ast_hashtab_resize_java,
								ast_hashtab_newsize_java,
								hashtab_hash_labels,
								0);
			if (label) {
				ast_hashtab_insert_safe(tmp->peer_label_table, tmp);
			}
			ast_hashtab_insert_safe(tmp->peer_table, tmp);
		} else {  /* this is the first exten in this context */
			if (!con->root_table)
				con->root_table = ast_hashtab_create(27,
													hashtab_compare_extens,
													ast_hashtab_resize_java,
													ast_hashtab_newsize_java,
													hashtab_hash_extens,
													0);
			con->root = tmp;
			con->root->peer_table = ast_hashtab_create(13,
								hashtab_compare_exten_numbers,
								ast_hashtab_resize_java,
								ast_hashtab_newsize_java,
								hashtab_hash_priority,
								0);
			con->root->peer_label_table = ast_hashtab_create(7,
									hashtab_compare_exten_labels,
									ast_hashtab_resize_java,
									ast_hashtab_newsize_java,
									hashtab_hash_labels,
									0);
			if (label) {
				ast_hashtab_insert_safe(con->root->peer_label_table, tmp);
			}
			ast_hashtab_insert_safe(con->root->peer_table, tmp);

		}
		ast_hashtab_insert_safe(con->root_table, tmp);
		if (lock_context) {
			ast_unlock_context(con);
		}
		if (tmp->priority == PRIORITY_HINT) {
			ast_add_hint(tmp);
		}
	}
	if (option_debug) {
		if (tmp->matchcid == AST_EXT_MATCHCID_ON) {
			ast_debug(1, "Added extension '%s' priority %d (CID match '%s') to %s (%p)\n",
					  tmp->exten, tmp->priority, tmp->cidmatch, con->name, con);
		} else {
			ast_debug(1, "Added extension '%s' priority %d to %s (%p)\n",
					  tmp->exten, tmp->priority, con->name, con);
		}
	}

	if (tmp->matchcid == AST_EXT_MATCHCID_ON) {
		ast_verb(3, "Added extension '%s' priority %d (CID match '%s') to %s\n",
				 tmp->exten, tmp->priority, tmp->cidmatch, con->name);
	} else {
		ast_verb(3, "Added extension '%s' priority %d to %s\n",
				 tmp->exten, tmp->priority, con->name);
	}

	return 0;
}

struct async_stat {
	pthread_t p;
	struct ast_channel *chan;
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	int priority;
	int timeout;
	char app[AST_MAX_EXTENSION];
	char appdata[1024];
	int early_media;			/* Connect the bridge if early media arrives, don't wait for answer */
};

static void *async_wait(void *data)
{
	struct async_stat *as = data;
	struct ast_channel *chan = as->chan;
	int timeout = as->timeout;
	int res;
	struct ast_frame *f;
	struct ast_app *app;
	int have_early_media = 0;
	struct timeval start = ast_tvnow();
	int ms;

	if (chan) {
		struct ast_callid *callid = ast_channel_callid(chan);
		if (callid) {
			ast_callid_threadassoc_add(callid);
			ast_callid_unref(callid);
		}
	}

	while ((ms = ast_remaining_ms(start, timeout)) &&
			ast_channel_state(chan) != AST_STATE_UP) {
		res = ast_waitfor(chan, ms);
		if (res < 1)
			break;

		f = ast_read(chan);
		if (!f)
			break;
		if (f->frametype == AST_FRAME_CONTROL) {
			if ((f->subclass.integer == AST_CONTROL_BUSY)  ||
			    (f->subclass.integer == AST_CONTROL_CONGESTION) ) {
				ast_frfree(f);
				break;
			}
			if (as->early_media && f->subclass.integer == AST_CONTROL_PROGRESS) {
				have_early_media = 1;
				ast_frfree(f);
				break;
			}
		}
		ast_frfree(f);
	}
	if (ast_channel_state(chan) == AST_STATE_UP || have_early_media) {
		if (have_early_media) {
			ast_debug(2, "Activating pbx since we have early media \n");
		}
		if (!ast_strlen_zero(as->app)) {
			app = pbx_findapp(as->app);
			if (app) {
				ast_verb(3, "Launching %s(%s) on %s\n", as->app, as->appdata, ast_channel_name(chan));
				pbx_exec(chan, app, as->appdata);
			} else
				ast_log(LOG_WARNING, "No such application '%s'\n", as->app);
		} else {
			if (!ast_strlen_zero(as->context))
				ast_channel_context_set(chan, as->context);
			if (!ast_strlen_zero(as->exten))
				ast_channel_exten_set(chan, as->exten);
			if (as->priority > 0)
				ast_channel_priority_set(chan, as->priority);
			/* Run the PBX */
			if (ast_pbx_run(chan)) {
				ast_log(LOG_ERROR, "Failed to start PBX on %s\n", ast_channel_name(chan));
			} else {
				/* PBX will have taken care of this */
				chan = NULL;
			}
		}
	}
	ast_free(as);
	if (chan)
		ast_hangup(chan);
	return NULL;
}

/*!
 * \brief Function to post an empty cdr after a spool call fails.
 * \note This function posts an empty cdr for a failed spool call
*/
static int ast_pbx_outgoing_cdr_failed(void)
{
	/* allocate a channel */
	struct ast_channel *chan = ast_dummy_channel_alloc();

	if (!chan)
		return -1;  /* failure */

	ast_channel_cdr_set(chan, ast_cdr_alloc());
	if (!ast_channel_cdr(chan)) {
		/* allocation of the cdr failed */
		chan = ast_channel_unref(chan);   /* free the channel */
		return -1;                /* return failure */
	}

	/* allocation of the cdr was successful */
	ast_cdr_init(ast_channel_cdr(chan), chan);  /* initialize our channel's cdr */
	ast_cdr_start(ast_channel_cdr(chan));       /* record the start and stop time */
	ast_cdr_end(ast_channel_cdr(chan));
	ast_cdr_failed(ast_channel_cdr(chan));      /* set the status to failed */
	ast_cdr_detach(ast_channel_cdr(chan));      /* post and free the record */
	ast_channel_cdr_set(chan, NULL);
	chan = ast_channel_unref(chan);         /* free the channel */

	return 0;  /* success */
}

int ast_pbx_outgoing_exten(const char *type, struct ast_format_cap *cap, const char *addr, int timeout, const char *context, const char *exten, int priority, int *reason, int synchronous, const char *cid_num, const char *cid_name, struct ast_variable *vars, const char *account, struct ast_channel **channel, int early_media)
{
	struct ast_channel *chan;
	struct async_stat *as;
	struct ast_callid *callid;
	int callid_created = 0;
	int res = -1, cdr_res = -1;
	struct outgoing_helper oh;

	oh.connect_on_early_media = early_media;

	callid_created = ast_callid_threadstorage_auto(&callid);

	if (synchronous) {
		oh.context = context;
		oh.exten = exten;
		oh.priority = priority;
		oh.cid_num = cid_num;
		oh.cid_name = cid_name;
		oh.account = account;
		oh.vars = vars;
		oh.parent_channel = NULL;

		chan = __ast_request_and_dial(type, cap, NULL, addr, timeout, reason, cid_num, cid_name, &oh);
		if (channel) {
			*channel = chan;
			if (chan)
				ast_channel_lock(chan);
		}
		if (chan) {
			/* Bind the callid to the channel if it doesn't already have one on creation */
			struct ast_callid *channel_callid = ast_channel_callid(chan);
			if (channel_callid) {
				ast_callid_unref(channel_callid);
			} else {
				if (callid) {
					ast_channel_callid_set(chan, callid);
				}
			}

			if (ast_channel_state(chan) == AST_STATE_UP || (early_media && *reason == AST_CONTROL_PROGRESS)) {
					res = 0;
				ast_verb(4, "Channel %s %s\n", ast_channel_name(chan), ast_channel_state(chan) == AST_STATE_UP ? "was answered" : "got early media");

				if (synchronous > 1) {
					if (channel)
						ast_channel_unlock(chan);
					if (ast_pbx_run(chan)) {
						ast_log(LOG_ERROR, "Unable to run PBX on %s\n", ast_channel_name(chan));
						if (channel)
							*channel = NULL;
						ast_hangup(chan);
						chan = NULL;
						res = -1;
					}
				} else {
					if (ast_pbx_start(chan)) {
						ast_log(LOG_ERROR, "Unable to start PBX on %s\n", ast_channel_name(chan));
						if (channel) {
							*channel = NULL;
							ast_channel_unlock(chan);
						}
						ast_hangup(chan);
						res = -1;
					}
					chan = NULL;
				}
			} else {
				ast_verb(4, "Channel %s was never answered.\n", ast_channel_name(chan));

				if (ast_channel_cdr(chan)) { /* update the cdr */
					/* here we update the status of the call, which sould be busy.
					 * if that fails then we set the status to failed */
					if (ast_cdr_disposition(ast_channel_cdr(chan), ast_channel_hangupcause(chan)))
						ast_cdr_failed(ast_channel_cdr(chan));
				}

				if (channel) {
					*channel = NULL;
					ast_channel_unlock(chan);
				}
				ast_hangup(chan);
				chan = NULL;
			}
		}

		if (res < 0) { /* the call failed for some reason */
			if (*reason == 0) { /* if the call failed (not busy or no answer)
				            * update the cdr with the failed message */
				cdr_res = ast_pbx_outgoing_cdr_failed();
				if (cdr_res != 0) {
					res = cdr_res;
					goto outgoing_exten_cleanup;
				}
			}

			/* create a fake channel and execute the "failed" extension (if it exists) within the requested context */
			/* check if "failed" exists */
			if (ast_exists_extension(chan, context, "failed", 1, NULL)) {
				chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", NULL, 0, "OutgoingSpoolFailed");
				if (chan) {
					char failed_reason[4] = "";
					if (!ast_strlen_zero(context))
						ast_channel_context_set(chan, context);
					set_ext_pri(chan, "failed", 1);
					ast_set_variables(chan, vars);
					snprintf(failed_reason, sizeof(failed_reason), "%d", *reason);
					pbx_builtin_setvar_helper(chan, "REASON", failed_reason);
					if (account)
						ast_cdr_setaccount(chan, account);
					if (ast_pbx_run(chan)) {
						ast_log(LOG_ERROR, "Unable to run PBX on %s\n", ast_channel_name(chan));
						ast_hangup(chan);
					}
					chan = NULL;
				}
			}
		}
	} else {
		struct ast_callid *channel_callid;
		if (!(as = ast_calloc(1, sizeof(*as)))) {
			res = -1;
			goto outgoing_exten_cleanup;
		}
		chan = ast_request_and_dial(type, cap, NULL, addr, timeout, reason, cid_num, cid_name);
		if (channel) {
			*channel = chan;
			if (chan)
				ast_channel_lock(chan);
		}
		if (!chan) {
			ast_free(as);
			res = -1;
			goto outgoing_exten_cleanup;
		}

		/* Bind the newly created callid to the channel if it doesn't already have one on creation. */
		channel_callid = ast_channel_callid(chan);
		if (channel_callid) {
			ast_callid_unref(channel_callid);
		} else {
			if (callid) {
				ast_channel_callid_set(chan, callid);
			}
		}

		as->chan = chan;
		ast_copy_string(as->context, context, sizeof(as->context));
		set_ext_pri(as->chan,  exten, priority);
		as->timeout = timeout;
		ast_set_variables(chan, vars);
		if (account)
			ast_cdr_setaccount(chan, account);
		if (ast_pthread_create_detached(&as->p, NULL, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			ast_free(as);
			if (channel) {
				*channel = NULL;
				ast_channel_unlock(chan);
			}
			ast_hangup(chan);
			res = -1;
			goto outgoing_exten_cleanup;
		}
		res = 0;
	}

outgoing_exten_cleanup:
	ast_callid_threadstorage_auto_clean(callid, callid_created);
	ast_variables_destroy(vars);
	return res;
}

struct app_tmp {
	struct ast_channel *chan;
	pthread_t t;
	AST_DECLARE_STRING_FIELDS (
		AST_STRING_FIELD(app);
		AST_STRING_FIELD(data);
	);
};

/*! \brief run the application and free the descriptor once done */
static void *ast_pbx_run_app(void *data)
{
	struct app_tmp *tmp = data;
	struct ast_app *app;
	app = pbx_findapp(tmp->app);
	if (app) {
		ast_verb(4, "Launching %s(%s) on %s\n", tmp->app, tmp->data, ast_channel_name(tmp->chan));
		pbx_exec(tmp->chan, app, tmp->data);
	} else
		ast_log(LOG_WARNING, "No such application '%s'\n", tmp->app);
	ast_hangup(tmp->chan);
	ast_string_field_free_memory(tmp);
	ast_free(tmp);
	return NULL;
}

int ast_pbx_outgoing_app(const char *type, struct ast_format_cap *cap, const char *addr, int timeout, const char *app, const char *appdata, int *reason, int synchronous, const char *cid_num, const char *cid_name, struct ast_variable *vars, const char *account, struct ast_channel **locked_channel)
{
	struct ast_channel *chan;
	struct app_tmp *tmp;
	struct ast_callid *callid;
	int callid_created;
	int res = -1, cdr_res = -1;
	struct outgoing_helper oh;

	/* Start by checking for a callid in threadstorage, and if none is found, bind one. */
	callid_created = ast_callid_threadstorage_auto(&callid);

	memset(&oh, 0, sizeof(oh));
	oh.vars = vars;
	oh.account = account;

	if (locked_channel)
		*locked_channel = NULL;
	if (ast_strlen_zero(app)) {
		res = -1;
		goto outgoing_app_cleanup;
	}
	if (synchronous) {
		chan = __ast_request_and_dial(type, cap, NULL, addr, timeout, reason, cid_num, cid_name, &oh);
		if (chan) {
			/* Bind the newly created callid to the channel if it doesn't already have one on creation */
			struct ast_callid *channel_callid = ast_channel_callid(chan);
			if (channel_callid) {
				ast_callid_unref(channel_callid);
			} else {
				if (callid) {
					ast_channel_callid_set(chan, callid);
				}
			}

			ast_set_variables(chan, vars);
			if (account)
				ast_cdr_setaccount(chan, account);
			if (ast_channel_state(chan) == AST_STATE_UP) {
				res = 0;
				ast_verb(4, "Channel %s was answered.\n", ast_channel_name(chan));
				tmp = ast_calloc(1, sizeof(*tmp));
				if (!tmp || ast_string_field_init(tmp, 252)) {
					if (tmp) {
						ast_free(tmp);
					}
					res = -1;
				} else {
					ast_string_field_set(tmp, app, app);
					ast_string_field_set(tmp, data, appdata);
					tmp->chan = chan;
					if (synchronous > 1) {
						if (locked_channel)
							ast_channel_unlock(chan);
						ast_pbx_run_app(tmp);
					} else {
						if (locked_channel)
							ast_channel_lock(chan);
						if (ast_pthread_create_detached(&tmp->t, NULL, ast_pbx_run_app, tmp)) {
							ast_log(LOG_WARNING, "Unable to spawn execute thread on %s: %s\n", ast_channel_name(chan), strerror(errno));
							ast_string_field_free_memory(tmp);
							ast_free(tmp);
							if (locked_channel)
								ast_channel_unlock(chan);
							ast_hangup(chan);
							res = -1;
						} else {
							if (locked_channel)
								*locked_channel = chan;
						}
					}
				}
			} else {
				ast_verb(4, "Channel %s was never answered.\n", ast_channel_name(chan));
				if (ast_channel_cdr(chan)) { /* update the cdr */
					/* here we update the status of the call, which sould be busy.
					 * if that fails then we set the status to failed */
					if (ast_cdr_disposition(ast_channel_cdr(chan), ast_channel_hangupcause(chan)))
						ast_cdr_failed(ast_channel_cdr(chan));
				}
				ast_hangup(chan);
			}
		}

		if (res < 0) { /* the call failed for some reason */
			if (*reason == 0) { /* if the call failed (not busy or no answer)
				            * update the cdr with the failed message */
				cdr_res = ast_pbx_outgoing_cdr_failed();
				if (cdr_res != 0) {
					res = cdr_res;
					goto outgoing_app_cleanup;
				}
			}
		}

	} else {
		struct async_stat *as;
		struct ast_callid *channel_callid;
		if (!(as = ast_calloc(1, sizeof(*as)))) {
			res = -1;
			goto outgoing_app_cleanup;
		}
		chan = __ast_request_and_dial(type, cap, NULL, addr, timeout, reason, cid_num, cid_name, &oh);
		if (!chan) {
			ast_free(as);
			res = -1;
			goto outgoing_app_cleanup;
		}

		/* Bind the newly created callid to the channel if it doesn't already have one on creation. */
		channel_callid = ast_channel_callid(chan);
		if (channel_callid) {
			ast_callid_unref(channel_callid);
		} else {
			if (callid) {
				ast_channel_callid_set(chan, callid);
			}
		}

		as->chan = chan;
		ast_copy_string(as->app, app, sizeof(as->app));
		if (appdata)
			ast_copy_string(as->appdata,  appdata, sizeof(as->appdata));
		as->timeout = timeout;
		ast_set_variables(chan, vars);
		if (account)
			ast_cdr_setaccount(chan, account);
		/* Start a new thread, and get something handling this channel. */
		if (locked_channel)
			ast_channel_lock(chan);
		if (ast_pthread_create_detached(&as->p, NULL, async_wait, as)) {
			ast_log(LOG_WARNING, "Failed to start async wait\n");
			ast_free(as);
			if (locked_channel)
				ast_channel_unlock(chan);
			ast_hangup(chan);
			res = -1;
			goto outgoing_app_cleanup;
		} else {
			if (locked_channel)
				*locked_channel = chan;
		}
		res = 0;
	}

outgoing_app_cleanup:
	ast_callid_threadstorage_auto_clean(callid, callid_created);
	ast_variables_destroy(vars);
	return res;
}

/* this is the guts of destroying a context --
   freeing up the structure, traversing and destroying the
   extensions, switches, ignorepats, includes, etc. etc. */

static void __ast_internal_context_destroy( struct ast_context *con)
{
	struct ast_include *tmpi;
	struct ast_sw *sw;
	struct ast_exten *e, *el, *en;
	struct ast_ignorepat *ipi;
	struct ast_context *tmp = con;

	for (tmpi = tmp->includes; tmpi; ) { /* Free includes */
		struct ast_include *tmpil = tmpi;
		tmpi = tmpi->next;
		ast_free(tmpil);
	}
	for (ipi = tmp->ignorepats; ipi; ) { /* Free ignorepats */
		struct ast_ignorepat *ipl = ipi;
		ipi = ipi->next;
		ast_free(ipl);
	}
	if (tmp->registrar)
		ast_free(tmp->registrar);

	/* destroy the hash tabs */
	if (tmp->root_table) {
		ast_hashtab_destroy(tmp->root_table, 0);
	}
	/* and destroy the pattern tree */
	if (tmp->pattern_tree)
		destroy_pattern_tree(tmp->pattern_tree);

	while ((sw = AST_LIST_REMOVE_HEAD(&tmp->alts, list)))
		ast_free(sw);
	for (e = tmp->root; e;) {
		for (en = e->peer; en;) {
			el = en;
			en = en->peer;
			destroy_exten(el);
		}
		el = e;
		e = e->next;
		destroy_exten(el);
	}
	tmp->root = NULL;
	ast_rwlock_destroy(&tmp->lock);
	ast_mutex_destroy(&tmp->macrolock);
	ast_free(tmp);
}


void __ast_context_destroy(struct ast_context *list, struct ast_hashtab *contexttab, struct ast_context *con, const char *registrar)
{
	struct ast_context *tmp, *tmpl=NULL;
	struct ast_exten *exten_item, *prio_item;

	for (tmp = list; tmp; ) {
		struct ast_context *next = NULL;	/* next starting point */
			/* The following code used to skip forward to the next
			   context with matching registrar, but this didn't
			   make sense; individual priorities registrar'd to
			   the matching registrar could occur in any context! */
		ast_debug(1, "Investigate ctx %s %s\n", tmp->name, tmp->registrar);
		if (con) {
			for (; tmp; tmpl = tmp, tmp = tmp->next) { /* skip to the matching context */
				ast_debug(1, "check ctx %s %s\n", tmp->name, tmp->registrar);
				if ( !strcasecmp(tmp->name, con->name) ) {
					break;	/* found it */
				}
			}
		}

		if (!tmp)	/* not found, we are done */
			break;
		ast_wrlock_context(tmp);

		if (registrar) {
			/* then search thru and remove any extens that match registrar. */
			struct ast_hashtab_iter *exten_iter;
			struct ast_hashtab_iter *prio_iter;
			struct ast_ignorepat *ip, *ipl = NULL, *ipn = NULL;
			struct ast_include *i, *pi = NULL, *ni = NULL;
			struct ast_sw *sw = NULL;

			/* remove any ignorepats whose registrar matches */
			for (ip = tmp->ignorepats; ip; ip = ipn) {
				ipn = ip->next;
				if (!strcmp(ip->registrar, registrar)) {
					if (ipl) {
						ipl->next = ip->next;
						ast_free(ip);
						continue; /* don't change ipl */
					} else {
						tmp->ignorepats = ip->next;
						ast_free(ip);
						continue; /* don't change ipl */
					}
				}
				ipl = ip;
			}
			/* remove any includes whose registrar matches */
			for (i = tmp->includes; i; i = ni) {
				ni = i->next;
				if (strcmp(i->registrar, registrar) == 0) {
					/* remove from list */
					if (pi) {
						pi->next = i->next;
						/* free include */
						ast_free(i);
						continue; /* don't change pi */
					} else {
						tmp->includes = i->next;
						/* free include */
						ast_free(i);
						continue; /* don't change pi */
					}
				}
				pi = i;
			}
			/* remove any switches whose registrar matches */
			AST_LIST_TRAVERSE_SAFE_BEGIN(&tmp->alts, sw, list) {
				if (strcmp(sw->registrar,registrar) == 0) {
					AST_LIST_REMOVE_CURRENT(list);
					ast_free(sw);
				}
			}
			AST_LIST_TRAVERSE_SAFE_END;

			if (tmp->root_table) { /* it is entirely possible that the context is EMPTY */
				exten_iter = ast_hashtab_start_traversal(tmp->root_table);
				while ((exten_item=ast_hashtab_next(exten_iter))) {
					int end_traversal = 1;
					prio_iter = ast_hashtab_start_traversal(exten_item->peer_table);
					while ((prio_item=ast_hashtab_next(prio_iter))) {
						char extension[AST_MAX_EXTENSION];
						char cidmatch[AST_MAX_EXTENSION];
						if (!prio_item->registrar || strcmp(prio_item->registrar, registrar) != 0) {
							continue;
						}
						ast_verb(3, "Remove %s/%s/%d, registrar=%s; con=%s(%p); con->root=%p\n",
								 tmp->name, prio_item->exten, prio_item->priority, registrar, con? con->name : "<nil>", con, con? con->root_table: NULL);
						ast_copy_string(extension, prio_item->exten, sizeof(extension));
						if (prio_item->cidmatch) {
							ast_copy_string(cidmatch, prio_item->cidmatch, sizeof(cidmatch));
						}
						end_traversal &= ast_context_remove_extension_callerid2(tmp, extension, prio_item->priority, cidmatch, prio_item->matchcid, NULL, 1);
					}
					/* Explanation:
					 * ast_context_remove_extension_callerid2 will destroy the extension that it comes across. This
					 * destruction includes destroying the exten's peer_table, which we are currently traversing. If
					 * ast_context_remove_extension_callerid2 ever should return '0' then this means we have destroyed
					 * the hashtable which we are traversing, and thus calling ast_hashtab_end_traversal will result
					 * in reading invalid memory. Thus, if we detect that we destroyed the hashtable, then we will simply
					 * free the iterator
					 */
					if (end_traversal) {
						ast_hashtab_end_traversal(prio_iter);
					} else {
						ast_free(prio_iter);
					}
				}
				ast_hashtab_end_traversal(exten_iter);
			}

			/* delete the context if it's registrar matches, is empty, has refcount of 1, */
			/* it's not empty, if it has includes, ignorepats, or switches that are registered from
			   another registrar. It's not empty if there are any extensions */
			if (strcmp(tmp->registrar, registrar) == 0 && tmp->refcount < 2 && !tmp->root && !tmp->ignorepats && !tmp->includes && AST_LIST_EMPTY(&tmp->alts)) {
				ast_debug(1, "delete ctx %s %s\n", tmp->name, tmp->registrar);
				ast_hashtab_remove_this_object(contexttab, tmp);

				next = tmp->next;
				if (tmpl)
					tmpl->next = next;
				else
					contexts = next;
				/* Okay, now we're safe to let it go -- in a sense, we were
				   ready to let it go as soon as we locked it. */
				ast_unlock_context(tmp);
				__ast_internal_context_destroy(tmp);
			} else {
				ast_debug(1,"Couldn't delete ctx %s/%s; refc=%d; tmp.root=%p\n", tmp->name, tmp->registrar,
						  tmp->refcount, tmp->root);
				ast_unlock_context(tmp);
				next = tmp->next;
				tmpl = tmp;
			}
		} else if (con) {
			ast_verb(3, "Deleting context %s registrar=%s\n", tmp->name, tmp->registrar);
			ast_debug(1, "delete ctx %s %s\n", tmp->name, tmp->registrar);
			ast_hashtab_remove_this_object(contexttab, tmp);

			next = tmp->next;
			if (tmpl)
				tmpl->next = next;
			else
				contexts = next;
			/* Okay, now we're safe to let it go -- in a sense, we were
			   ready to let it go as soon as we locked it. */
			ast_unlock_context(tmp);
			__ast_internal_context_destroy(tmp);
		}

		/* if we have a specific match, we are done, otherwise continue */
		tmp = con ? NULL : next;
	}
}

void ast_context_destroy(struct ast_context *con, const char *registrar)
{
	ast_wrlock_contexts();
	__ast_context_destroy(contexts, contexts_table, con,registrar);
	ast_unlock_contexts();
}

static void wait_for_hangup(struct ast_channel *chan, const void *data)
{
	int res;
	struct ast_frame *f;
	double waitsec;
	int waittime;

	if (ast_strlen_zero(data) || (sscanf(data, "%30lg", &waitsec) != 1) || (waitsec < 0))
		waitsec = -1;
	if (waitsec > -1) {
		waittime = waitsec * 1000.0;
		ast_safe_sleep(chan, waittime);
	} else do {
		res = ast_waitfor(chan, -1);
		if (res < 0)
			return;
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
	} while(f);
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
static int pbx_builtin_busy(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_BUSY);
	/* Don't change state of an UP channel, just indicate
	   busy in audio */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_setstate(chan, AST_STATE_BUSY);
		ast_cdr_busy(ast_channel_cdr(chan));
	}
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_congestion(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	/* Don't change state of an UP channel, just indicate
	   congestion in audio */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_setstate(chan, AST_STATE_BUSY);
		ast_cdr_congestion(ast_channel_cdr(chan));
	}
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_answer(struct ast_channel *chan, const char *data)
{
	int delay = 0;
	int answer_cdr = 1;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(delay);
		AST_APP_ARG(answer_cdr);
	);

	if (ast_strlen_zero(data)) {
		return __ast_answer(chan, 0, 1);
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.delay) && (ast_channel_state(chan) != AST_STATE_UP))
		delay = atoi(data);

	if (delay < 0) {
		delay = 0;
	}

	if (!ast_strlen_zero(args.answer_cdr) && !strcasecmp(args.answer_cdr, "nocdr")) {
		answer_cdr = 0;
	}

	return __ast_answer(chan, delay, answer_cdr);
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
		__ast_answer(chan, 0, 1);
	}

	ast_indicate(chan, AST_CONTROL_INCOMPLETE);

	return AST_PBX_INCOMPLETE;
}

AST_APP_OPTIONS(resetcdr_opts, {
	AST_APP_OPTION('w', AST_CDR_FLAG_POSTED),
	AST_APP_OPTION('a', AST_CDR_FLAG_LOCKED),
	AST_APP_OPTION('v', AST_CDR_FLAG_KEEP_VARS),
	AST_APP_OPTION('e', AST_CDR_FLAG_POST_ENABLE),
});

/*!
 * \ingroup applications
 */
static int pbx_builtin_resetcdr(struct ast_channel *chan, const char *data)
{
	char *args;
	struct ast_flags flags = { 0 };

	if (!ast_strlen_zero(data)) {
		args = ast_strdupa(data);
		ast_app_parse_options(resetcdr_opts, &flags, NULL, args);
	}

	ast_cdr_reset(ast_channel_cdr(chan), &flags);

	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_setamaflags(struct ast_channel *chan, const char *data)
{
	/* Copy the AMA Flags as specified */
	ast_channel_lock(chan);
	ast_cdr_setamaflags(chan, data ? data : "");
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

/*!
 * \ingroup functions
 */
static int testtime_write(struct ast_channel *chan, const char *cmd, char *var, const char *value)
{
	struct ast_tm tm;
	struct timeval tv;
	char *remainder, result[30], timezone[80];

	/* Turn off testing? */
	if (!pbx_checkcondition(value)) {
		pbx_builtin_setvar_helper(chan, "TESTTIME", NULL);
		return 0;
	}

	/* Parse specified time */
	if (!(remainder = ast_strptime(value, "%Y/%m/%d %H:%M:%S", &tm))) {
		return -1;
	}
	sscanf(remainder, "%79s", timezone);
	tv = ast_mktime(&tm, S_OR(timezone, NULL));

	snprintf(result, sizeof(result), "%ld", (long) tv.tv_sec);
	pbx_builtin_setvar_helper(chan, "__TESTTIME", result);
	return 0;
}

static struct ast_custom_function testtime_function = {
	.name = "TESTTIME",
	.write = testtime_write,
};

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
		&& (exten[0] = res)
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


int pbx_builtin_serialize_variables(struct ast_channel *chan, struct ast_str **buf)
{
	struct ast_var_t *variables;
	const char *var, *val;
	int total = 0;

	if (!chan)
		return 0;

	ast_str_reset(*buf);

	ast_channel_lock(chan);

	AST_LIST_TRAVERSE(ast_channel_varshead(chan), variables, entries) {
		if ((var = ast_var_name(variables)) && (val = ast_var_value(variables))
		   /* && !ast_strlen_zero(var) && !ast_strlen_zero(val) */
		   ) {
			if (ast_str_append(buf, 0, "%s=%s\n", var, val) < 0) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			} else
				total++;
		} else
			break;
	}

	ast_channel_unlock(chan);

	return total;
}

const char *pbx_builtin_getvar_helper(struct ast_channel *chan, const char *name)
{
	struct ast_var_t *variables;
	const char *ret = NULL;
	int i;
	struct varshead *places[2] = { NULL, &globals };

	if (!name)
		return NULL;

	if (chan) {
		ast_channel_lock(chan);
		places[0] = ast_channel_varshead(chan);
	}

	for (i = 0; i < 2; i++) {
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_rwlock_rdlock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (!strcmp(name, ast_var_name(variables))) {
				ret = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_rwlock_unlock(&globalslock);
		if (ret)
			break;
	}

	if (chan)
		ast_channel_unlock(chan);

	return ret;
}

void pbx_builtin_pushvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;

	if (name[strlen(name)-1] == ')') {
		char *function = ast_strdupa(name);

		ast_log(LOG_WARNING, "Cannot push a value onto a function\n");
		ast_func_write(chan, function, value);
		return;
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = ast_channel_varshead(chan);
	} else {
		ast_rwlock_wrlock(&globalslock);
		headp = &globals;
	}

	if (value && (newvariable = ast_var_assign(name, value))) {
		if (headp == &globals)
			ast_verb(2, "Setting global variable '%s' to '%s'\n", name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_rwlock_unlock(&globalslock);
}

int pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *nametail = name;

	if (name[strlen(name) - 1] == ')') {
		char *function = ast_strdupa(name);

		return ast_func_write(chan, function, value);
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = ast_channel_varshead(chan);
	} else {
		ast_rwlock_wrlock(&globalslock);
		headp = &globals;
	}

	/* For comparison purposes, we have to strip leading underscores */
	if (*nametail == '_') {
		nametail++;
		if (*nametail == '_')
			nametail++;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(headp, newvariable, entries) {
		if (strcmp(ast_var_name(newvariable), nametail) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE_CURRENT(entries);
			ast_var_delete(newvariable);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (value && (newvariable = ast_var_assign(name, value))) {
		if (headp == &globals)
			ast_verb(2, "Setting global variable '%s' to '%s'\n", name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when a variable is set to a particular value.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_DIALPLAN, "VarSet",
			"Channel: %s\r\n"
			"Variable: %s\r\n"
			"Value: %s\r\n"
			"Uniqueid: %s\r\n",
			chan ? ast_channel_name(chan) : "none", name, value,
			chan ? ast_channel_uniqueid(chan) : "none");
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_rwlock_unlock(&globalslock);
	return 0;
}

int pbx_builtin_setvar(struct ast_channel *chan, const char *data)
{
	char *name, *value, *mydata;

	if (ast_compat_app_set) {
		return pbx_builtin_setvar_multiple(chan, data);
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Set requires one variable name/value pair.\n");
		return 0;
	}

	mydata = ast_strdupa(data);
	name = strsep(&mydata, "=");
	value = mydata;
	if (!value) {
		ast_log(LOG_WARNING, "Set requires an '=' to be a valid assignment.\n");
		return 0;
	}

	if (strchr(name, ' ')) {
		ast_log(LOG_WARNING, "Please avoid unnecessary spaces on variables as it may lead to unexpected results ('%s' set to '%s').\n", name, mydata);
	}

	pbx_builtin_setvar_helper(chan, name, value);

	return 0;
}

int pbx_builtin_setvar_multiple(struct ast_channel *chan, const char *vdata)
{
	char *data;
	int x;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(pair)[24];
	);
	AST_DECLARE_APP_ARGS(pair,
		AST_APP_ARG(name);
		AST_APP_ARG(value);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "MSet requires at least one variable name/value pair.\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	for (x = 0; x < args.argc; x++) {
		AST_NONSTANDARD_APP_ARGS(pair, args.pair[x], '=');
		if (pair.argc == 2) {
			pbx_builtin_setvar_helper(chan, pair.name, pair.value);
			if (strchr(pair.name, ' '))
				ast_log(LOG_WARNING, "Please avoid unnecessary spaces on variables as it may lead to unexpected results ('%s' set to '%s').\n", pair.name, pair.value);
		} else if (!chan) {
			ast_log(LOG_WARNING, "MSet: ignoring entry '%s' with no '='\n", pair.name);
		} else {
			ast_log(LOG_WARNING, "MSet: ignoring entry '%s' with no '=' (in %s@%s:%d\n", pair.name, ast_channel_exten(chan), ast_channel_context(chan), ast_channel_priority(chan));
		}
	}

	return 0;
}

int pbx_builtin_importvar(struct ast_channel *chan, const char *data)
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

static int pbx_builtin_noop(struct ast_channel *chan, const char *data)
{
	return 0;
}

void pbx_builtin_clear_globals(void)
{
	struct ast_var_t *vardata;

	ast_rwlock_wrlock(&globalslock);
	while ((vardata = AST_LIST_REMOVE_HEAD(&globals, entries)))
		ast_var_delete(vardata);
	ast_rwlock_unlock(&globalslock);
}

int pbx_checkcondition(const char *condition)
{
	int res;
	if (ast_strlen_zero(condition)) {                /* NULL or empty strings are false */
		return 0;
	} else if (sscanf(condition, "%30d", &res) == 1) { /* Numbers are evaluated for truth */
		return res;
	} else {                                         /* Strings are true */
		return 1;
	}
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

static int pbx_builtin_saynumber(struct ast_channel *chan, const char *data)
{
	char tmp[256];
	char *number = tmp;
	char *options;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayNumber requires an argument (number)\n");
		return -1;
	}
	ast_copy_string(tmp, data, sizeof(tmp));
	strsep(&number, ",");
	options = strsep(&number, ",");
	if (options) {
		if ( strcasecmp(options, "f") && strcasecmp(options, "m") &&
			strcasecmp(options, "c") && strcasecmp(options, "n") ) {
			ast_log(LOG_WARNING, "SayNumber gender option is either 'f', 'm', 'c' or 'n'\n");
			return -1;
		}
	}

	if (ast_say_number(chan, atoi(tmp), "", ast_channel_language(chan), options)) {
		ast_log(LOG_WARNING, "We were unable to say the number %s, is it too large?\n", tmp);
	}

	return 0;
}

static int pbx_builtin_saydigits(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data)
		res = ast_say_digit_str(chan, data, "", ast_channel_language(chan));
	return res;
}

static int pbx_builtin_saycharacters(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data)
		res = ast_say_character_str(chan, data, "", ast_channel_language(chan));
	return res;
}

static int pbx_builtin_sayphonetic(struct ast_channel *chan, const char *data)
{
	int res = 0;

	if (data)
		res = ast_say_phonetic_str(chan, data, "", ast_channel_language(chan));
	return res;
}

static void presencechange_destroy(void *data)
{
	struct presencechange *pc = data;
	ast_free(pc->provider);
	ast_free(pc->subtype);
	ast_free(pc->message);
}

static void presence_state_cb(const struct ast_event *event, void *unused)
{
	struct presencechange *pc;
	const char *tmp;

	if (!(pc = ao2_alloc(sizeof(*pc), presencechange_destroy))) {
		return;
	}

	tmp = ast_event_get_ie_str(event, AST_EVENT_IE_PRESENCE_PROVIDER);
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_ERROR, "Received invalid event that had no presence provider IE\n");
		ao2_ref(pc, -1);
		return;
	}
	pc->provider = ast_strdup(tmp);

	pc->state = ast_event_get_ie_uint(event, AST_EVENT_IE_PRESENCE_STATE);
	if (pc->state < 0) {
		ao2_ref(pc, -1);
		return;
	}

	if ((tmp = ast_event_get_ie_str(event, AST_EVENT_IE_PRESENCE_SUBTYPE))) {
		pc->subtype = ast_strdup(tmp);
	}

	if ((tmp = ast_event_get_ie_str(event, AST_EVENT_IE_PRESENCE_MESSAGE))) {
		pc->message = ast_strdup(tmp);
	}

	/* The task processor thread is taking our reference to the presencechange object. */
	if (ast_taskprocessor_push(extension_state_tps, handle_presencechange, pc) < 0) {
		ao2_ref(pc, -1);
	}
}

static void device_state_cb(const struct ast_event *event, void *unused)
{
	const char *device;
	struct statechange *sc;

	device = ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE);
	if (ast_strlen_zero(device)) {
		ast_log(LOG_ERROR, "Received invalid event that had no device IE\n");
		return;
	}

	if (!(sc = ast_calloc(1, sizeof(*sc) + strlen(device) + 1)))
		return;
	strcpy(sc->dev, device);
	if (ast_taskprocessor_push(extension_state_tps, handle_statechange, sc) < 0) {
		ast_free(sc);
	}
}

/*!
 * \internal
 * \brief Implements the hints data provider.
 */
static int hints_data_provider_get(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct ast_data *data_hint;
	struct ast_hint *hint;
	int watchers;
	struct ao2_iterator i;

	if (ao2_container_count(hints) == 0) {
		return 0;
	}

	i = ao2_iterator_init(hints, 0);
	for (; (hint = ao2_iterator_next(&i)); ao2_ref(hint, -1)) {
		watchers = ao2_container_count(hint->callbacks);
		data_hint = ast_data_add_node(data_root, "hint");
		if (!data_hint) {
			continue;
		}
		ast_data_add_str(data_hint, "extension", ast_get_extension_name(hint->exten));
		ast_data_add_str(data_hint, "context", ast_get_context_name(ast_get_extension_context(hint->exten)));
		ast_data_add_str(data_hint, "application", ast_get_extension_app(hint->exten));
		ast_data_add_str(data_hint, "state", ast_extension_state2str(hint->laststate));
		ast_data_add_str(data_hint, "presence_state", ast_presence_state2str(hint->last_presence_state));
		ast_data_add_str(data_hint, "presence_subtype", S_OR(hint->last_presence_subtype, ""));
		ast_data_add_str(data_hint, "presence_subtype", S_OR(hint->last_presence_message, ""));
		ast_data_add_int(data_hint, "watchers", watchers);

		if (!ast_data_search_match(search, data_hint)) {
			ast_data_remove_node(data_root, data_hint);
		}
	}
	ao2_iterator_destroy(&i);

	return 0;
}

static const struct ast_data_handler hints_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = hints_data_provider_get
};

static const struct ast_data_entry pbx_data_providers[] = {
	AST_DATA_ENTRY("asterisk/core/hints", &hints_data_provider),
};

/*! \internal \brief Clean up resources on Asterisk shutdown.
 * \note Cleans up resources allocated in load_pbx */
static void unload_pbx(void)
{
	int x;

	if (presence_state_sub) {
		presence_state_sub = ast_event_unsubscribe(presence_state_sub);
	}
	if (device_state_sub) {
		device_state_sub = ast_event_unsubscribe(device_state_sub);
	}

	/* Unregister builtin applications */
	for (x = 0; x < ARRAY_LEN(builtins); x++) {
		ast_unregister_application(builtins[x].name);
	}
	ast_manager_unregister("ShowDialPlan");
	ast_cli_unregister_multiple(pbx_cli, ARRAY_LEN(pbx_cli));
	ast_custom_function_unregister(&exception_function);
	ast_custom_function_unregister(&testtime_function);
	ast_data_unregister(NULL);
	if (extension_state_tps) {
		extension_state_tps = ast_taskprocessor_unreference(extension_state_tps);
	}
}

int load_pbx(void)
{
	int x;

	ast_register_atexit(unload_pbx);

	/* Initialize the PBX */
	ast_verb(1, "Asterisk PBX Core Initializing\n");
	if (!(extension_state_tps = ast_taskprocessor_get("pbx-core", 0))) {
		ast_log(LOG_WARNING, "failed to create pbx-core taskprocessor\n");
	}

	ast_verb(1, "Registering builtin applications:\n");
	ast_cli_register_multiple(pbx_cli, ARRAY_LEN(pbx_cli));
	ast_data_register_multiple_core(pbx_data_providers, ARRAY_LEN(pbx_data_providers));
	__ast_custom_function_register(&exception_function, NULL);
	__ast_custom_function_register(&testtime_function, NULL);

	/* Register builtin applications */
	for (x = 0; x < ARRAY_LEN(builtins); x++) {
		ast_verb(1, "[%s]\n", builtins[x].name);
		if (ast_register_application2(builtins[x].name, builtins[x].execute, NULL, NULL, NULL)) {
			ast_log(LOG_ERROR, "Unable to register builtin application '%s'\n", builtins[x].name);
			return -1;
		}
	}

	/* Register manager application */
	ast_manager_register_xml_core("ShowDialPlan", EVENT_FLAG_CONFIG | EVENT_FLAG_REPORTING, manager_show_dialplan);

	if (!(device_state_sub = ast_event_subscribe(AST_EVENT_DEVICE_STATE, device_state_cb, "pbx Device State Change", NULL,
			AST_EVENT_IE_END))) {
		return -1;
	}

	if (!(presence_state_sub = ast_event_subscribe(AST_EVENT_PRESENCE_STATE, presence_state_cb, "pbx Presence State Change", NULL,
			AST_EVENT_IE_END))) {
		return -1;
	}

	return 0;
}

/*
 * Lock context list functions ...
 */
int ast_wrlock_contexts(void)
{
	return ast_mutex_lock(&conlock);
}

int ast_rdlock_contexts(void)
{
	return ast_mutex_lock(&conlock);
}

int ast_unlock_contexts(void)
{
	return ast_mutex_unlock(&conlock);
}

/*
 * Lock context ...
 */
int ast_wrlock_context(struct ast_context *con)
{
	return ast_rwlock_wrlock(&con->lock);
}

int ast_rdlock_context(struct ast_context *con)
{
	return ast_rwlock_rdlock(&con->lock);
}

int ast_unlock_context(struct ast_context *con)
{
	return ast_rwlock_unlock(&con->lock);
}

/*
 * Name functions ...
 */
const char *ast_get_context_name(struct ast_context *con)
{
	return con ? con->name : NULL;
}

struct ast_context *ast_get_extension_context(struct ast_exten *exten)
{
	return exten ? exten->parent : NULL;
}

const char *ast_get_extension_name(struct ast_exten *exten)
{
	return exten ? exten->exten : NULL;
}

const char *ast_get_extension_label(struct ast_exten *exten)
{
	return exten ? exten->label : NULL;
}

const char *ast_get_include_name(struct ast_include *inc)
{
	return inc ? inc->name : NULL;
}

const char *ast_get_ignorepat_name(struct ast_ignorepat *ip)
{
	return ip ? ip->pattern : NULL;
}

int ast_get_extension_priority(struct ast_exten *exten)
{
	return exten ? exten->priority : -1;
}

/*
 * Registrar info functions ...
 */
const char *ast_get_context_registrar(struct ast_context *c)
{
	return c ? c->registrar : NULL;
}

const char *ast_get_extension_registrar(struct ast_exten *e)
{
	return e ? e->registrar : NULL;
}

const char *ast_get_include_registrar(struct ast_include *i)
{
	return i ? i->registrar : NULL;
}

const char *ast_get_ignorepat_registrar(struct ast_ignorepat *ip)
{
	return ip ? ip->registrar : NULL;
}

int ast_get_extension_matchcid(struct ast_exten *e)
{
	return e ? e->matchcid : 0;
}

const char *ast_get_extension_cidmatch(struct ast_exten *e)
{
	return e ? e->cidmatch : NULL;
}

const char *ast_get_extension_app(struct ast_exten *e)
{
	return e ? e->app : NULL;
}

void *ast_get_extension_app_data(struct ast_exten *e)
{
	return e ? e->data : NULL;
}

const char *ast_get_switch_name(struct ast_sw *sw)
{
	return sw ? sw->name : NULL;
}

const char *ast_get_switch_data(struct ast_sw *sw)
{
	return sw ? sw->data : NULL;
}

int ast_get_switch_eval(struct ast_sw *sw)
{
	return sw->eval;
}

const char *ast_get_switch_registrar(struct ast_sw *sw)
{
	return sw ? sw->registrar : NULL;
}

/*
 * Walking functions ...
 */
struct ast_context *ast_walk_contexts(struct ast_context *con)
{
	return con ? con->next : contexts;
}

struct ast_exten *ast_walk_context_extensions(struct ast_context *con,
	struct ast_exten *exten)
{
	if (!exten)
		return con ? con->root : NULL;
	else
		return exten->next;
}

struct ast_sw *ast_walk_context_switches(struct ast_context *con,
	struct ast_sw *sw)
{
	if (!sw)
		return con ? AST_LIST_FIRST(&con->alts) : NULL;
	else
		return AST_LIST_NEXT(sw, list);
}

struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority)
{
	return priority ? priority->peer : exten;
}

struct ast_include *ast_walk_context_includes(struct ast_context *con,
	struct ast_include *inc)
{
	if (!inc)
		return con ? con->includes : NULL;
	else
		return inc->next;
}

struct ast_ignorepat *ast_walk_context_ignorepats(struct ast_context *con,
	struct ast_ignorepat *ip)
{
	if (!ip)
		return con ? con->ignorepats : NULL;
	else
		return ip->next;
}

int ast_context_verify_includes(struct ast_context *con)
{
	struct ast_include *inc = NULL;
	int res = 0;

	while ( (inc = ast_walk_context_includes(con, inc)) ) {
		if (ast_context_find(inc->rname))
			continue;

		res = -1;
		ast_log(LOG_WARNING, "Context '%s' tries to include nonexistent context '%s'\n",
			ast_get_context_name(con), inc->rname);
		break;
	}

	return res;
}


static int __ast_goto_if_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, int async)
{
	int (*goto_func)(struct ast_channel *chan, const char *context, const char *exten, int priority);

	if (!chan)
		return -2;

	if (context == NULL)
		context = ast_channel_context(chan);
	if (exten == NULL)
		exten = ast_channel_exten(chan);

	goto_func = (async) ? ast_async_goto : ast_explicit_goto;
	if (ast_exists_extension(chan, context, exten, priority,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL)))
		return goto_func(chan, context, exten, priority);
	else {
		return AST_PBX_GOTO_FAILED;
	}
}

int ast_goto_if_exists(struct ast_channel *chan, const char* context, const char *exten, int priority)
{
	return __ast_goto_if_exists(chan, context, exten, priority, 0);
}

int ast_async_goto_if_exists(struct ast_channel *chan, const char * context, const char *exten, int priority)
{
	return __ast_goto_if_exists(chan, context, exten, priority, 1);
}

static int pbx_parseable_goto(struct ast_channel *chan, const char *goto_string, int async)
{
	char *exten, *pri, *context;
	char *stringp;
	int ipri;
	int mode = 0;

	if (ast_strlen_zero(goto_string)) {
		ast_log(LOG_WARNING, "Goto requires an argument ([[context,]extension,]priority)\n");
		return -1;
	}
	stringp = ast_strdupa(goto_string);
	context = strsep(&stringp, ",");	/* guaranteed non-null */
	exten = strsep(&stringp, ",");
	pri = strsep(&stringp, ",");
	if (!exten) {	/* Only a priority in this one */
		pri = context;
		exten = NULL;
		context = NULL;
	} else if (!pri) {	/* Only an extension and priority in this one */
		pri = exten;
		exten = context;
		context = NULL;
	}
	if (*pri == '+') {
		mode = 1;
		pri++;
	} else if (*pri == '-') {
		mode = -1;
		pri++;
	}
	if (sscanf(pri, "%30d", &ipri) != 1) {
		ipri = ast_findlabel_extension(chan, context ? context : ast_channel_context(chan),
			exten ? exten : ast_channel_exten(chan), pri,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL));
		if (ipri < 1) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0, or valid label\n", pri);
			return -1;
		} else
			mode = 0;
	}
	/* At this point we have a priority and maybe an extension and a context */

	if (mode)
		ipri = ast_channel_priority(chan) + (ipri * mode);

	if (async)
		ast_async_goto(chan, context, exten, ipri);
	else
		ast_explicit_goto(chan, context, exten, ipri);

	return 0;

}

int ast_parseable_goto(struct ast_channel *chan, const char *goto_string)
{
	return pbx_parseable_goto(chan, goto_string, 0);
}

int ast_async_parseable_goto(struct ast_channel *chan, const char *goto_string)
{
	return pbx_parseable_goto(chan, goto_string, 1);
}

char *ast_complete_applications(const char *line, const char *word, int state)
{
	struct ast_app *app = NULL;
	int which = 0;
	char *ret = NULL;
	size_t wordlen = strlen(word);

	AST_RWLIST_RDLOCK(&apps);
	AST_RWLIST_TRAVERSE(&apps, app, list) {
		if (!strncasecmp(word, app->name, wordlen) && ++which > state) {
			ret = ast_strdup(app->name);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&apps);

	return ret;
}

static int hint_hash(const void *obj, const int flags)
{
	const struct ast_hint *hint = obj;
	const char *exten_name;
	int res;

	exten_name = ast_get_extension_name(hint->exten);
	if (ast_strlen_zero(exten_name)) {
		/*
		 * If the exten or extension name isn't set, return 0 so that
		 * the ao2_find() search will start in the first bucket.
		 */
		res = 0;
	} else {
		res = ast_str_case_hash(exten_name);
	}

	return res;
}

static int hint_cmp(void *obj, void *arg, int flags)
{
	const struct ast_hint *hint = obj;
	const struct ast_exten *exten = arg;

	return (hint->exten == exten) ? CMP_MATCH | CMP_STOP : 0;
}

static int statecbs_cmp(void *obj, void *arg, int flags)
{
	const struct ast_state_cb *state_cb = obj;
	ast_state_cb_type change_cb = arg;

	return (state_cb->change_cb == change_cb) ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void pbx_shutdown(void)
{
	if (hints) {
		ao2_ref(hints, -1);
		hints = NULL;
	}
	if (hintdevices) {
		ao2_ref(hintdevices, -1);
		hintdevices = NULL;
	}
	if (statecbs) {
		ao2_ref(statecbs, -1);
		statecbs = NULL;
	}
	if (contexts_table) {
		ast_hashtab_destroy(contexts_table, NULL);
	}
	pbx_builtin_clear_globals();
}

int ast_pbx_init(void)
{
	hints = ao2_container_alloc(HASH_EXTENHINT_SIZE, hint_hash, hint_cmp);
	hintdevices = ao2_container_alloc(HASH_EXTENHINT_SIZE, hintdevice_hash_cb, hintdevice_cmp_multiple);
	statecbs = ao2_container_alloc(1, NULL, statecbs_cmp);

	ast_register_atexit(pbx_shutdown);

	return (hints && hintdevices && statecbs) ? 0 : -1;
}
