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
 * \brief True call queues with optional send URL on answer
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \par Development notes
 * \note 2004-11-25: Persistent Dynamic Members added by:
 *             NetNation Communications (www.netnation.com)
 *             Kevin Lindsay <kevinl@netnation.com>
 *
 *             Each dynamic agent in each queue is now stored in the astdb.
 *             When asterisk is restarted, each agent will be automatically
 *             readded into their recorded queues. This feature can be
 *             configured with the 'persistent_members=<1|0>' setting in the
 *             '[general]' category in queues.conf. The default is on.
 *
 * \note 2004-06-04: Priorities in queues added by inAccess Networks (work funded by Hellas On Line (HOL) www.hol.gr).
 *
 * \note These features added by David C. Troy <dave@toad.net>:
 *    - Per-queue holdtime calculation
 *    - Estimated holdtime announcement
 *    - Position announcement
 *    - Abandoned/completed call counters
 *    - Failout timer passed as optional app parameter
 *    - Optional monitoring of calls, started when call is answered
 *
 * Patch Version 1.07 2003-12-24 01
 *
 * Added servicelevel statistic by Michiel Betel <michiel@betel.nl>
 * Added Priority jumping code for adding and removing queue members by Jonathan Stanton <asterisk@doilooklikeicare.com>
 *
 * Fixed to work with CVS as of 2004-02-25 and released as 1.07a
 * by Matthew Enger <m.enger@xi.com.au>
 *
 * \ingroup applications
 */

/*! \li \ref app_queues.c uses configuration file \ref queues.conf
 * \addtogroup configuration_file
 */

/*! \page queues.conf queues.conf
 * \verbinclude queues.conf.sample
 */

/*** MODULEINFO
	<use type="module">res_monitor</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <ctype.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/monitor.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astdb.h"
#include "asterisk/devicestate.h"
#include "asterisk/stringfields.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/global_datastores.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/aoc.h"
#include "asterisk/callerid.h"
#include "asterisk/data.h"
#include "asterisk/term.h"
#include "asterisk/dial.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/bridge_after.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/core_local.h"
#include "asterisk/mixmonitor.h"
#include "asterisk/core_unreal.h"
#include "asterisk/bridge_basic.h"

/* Define, to debug reference counts on queues, without debugging reference counts on queue members */
/* #define REF_DEBUG_ONLY_QUEUES */

/*!
 * \par Please read before modifying this file.
 * There are three locks which are regularly used
 * throughout this file, the queue list lock, the lock
 * for each individual queue, and the interface list lock.
 * Please be extra careful to always lock in the following order
 * 1) queue list lock
 * 2) individual queue lock
 * 3) interface list lock
 * This order has sort of "evolved" over the lifetime of this
 * application, but it is now in place this way, so please adhere
 * to this order!
 */

/*** DOCUMENTATION
	<application name="Queue" language="en_US">
		<synopsis>
			Queue a call for a call queue.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="options">
				<optionlist>
					<option name="C">
						<para>Mark all calls as "answered elsewhere" when cancelled.</para>
					</option>
					<option name="c">
						<para>Continue in the dialplan if the callee hangs up.</para>
					</option>
					<option name="d">
						<para>data-quality (modem) call (minimum delay).</para>
					</option>
					<option name="F" argsep="^">
						<argument name="context" required="false" />
						<argument name="exten" required="false" />
						<argument name="priority" required="true" />
						<para>When the caller hangs up, transfer the <emphasis>called member</emphasis>
						to the specified destination and <emphasis>start</emphasis> execution at that location.</para>
						<note>
							<para>Any channel variables you want the called channel to inherit from the caller channel must be
							prefixed with one or two underbars ('_').</para>
						</note>
					</option>
					<option name="F">
						<para>When the caller hangs up, transfer the <emphasis>called member</emphasis> to the next priority of
						the current extension and <emphasis>start</emphasis> execution at that location.</para>
						<note>
							<para>Any channel variables you want the called channel to inherit from the caller channel must be
							prefixed with one or two underbars ('_').</para>
						</note>
						<note>
							<para>Using this option from a Macro() or GoSub() might not make sense as there would be no return points.</para>
						</note>
					</option>
					<option name="h">
						<para>Allow <emphasis>callee</emphasis> to hang up by pressing <literal>*</literal>.</para>
					</option>
					<option name="H">
						<para>Allow <emphasis>caller</emphasis> to hang up by pressing <literal>*</literal>.</para>
					</option>
					<option name="n">
						<para>No retries on the timeout; will exit this application and
						go to the next step.</para>
					</option>
					<option name="i">
						<para>Ignore call forward requests from queue members and do nothing
						when they are requested.</para>
					</option>
					<option name="I">
						<para>Asterisk will ignore any connected line update requests or any redirecting party
						update requests it may receive on this dial attempt.</para>
					</option>
					<option name="r">
						<para>Ring instead of playing MOH. Periodic Announcements are still made, if applicable.</para>
					</option>
					<option name="R">
						<para>Ring instead of playing MOH when a member channel is actually ringing.</para>
					</option>
					<option name="t">
						<para>Allow the <emphasis>called</emphasis> user to transfer the calling user.</para>
					</option>
					<option name="T">
						<para>Allow the <emphasis>calling</emphasis> user to transfer the call.</para>
					</option>
					<option name="w">
						<para>Allow the <emphasis>called</emphasis> user to write the conversation to
						disk via Monitor.</para>
					</option>
					<option name="W">
						<para>Allow the <emphasis>calling</emphasis> user to write the conversation to
						disk via Monitor.</para>
					</option>
					<option name="k">
						<para>Allow the <emphasis>called</emphasis> party to enable parking of the call by sending
						the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
					</option>
					<option name="K">
						<para>Allow the <emphasis>calling</emphasis> party to enable parking of the call by sending
						the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
					</option>
					<option name="x">
						<para>Allow the <emphasis>called</emphasis> user to write the conversation
						to disk via MixMonitor.</para>
					</option>
					<option name="X">
						<para>Allow the <emphasis>calling</emphasis> user to write the conversation to
						disk via MixMonitor.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="URL">
				<para><replaceable>URL</replaceable> will be sent to the called party if the channel supports it.</para>
			</parameter>
			<parameter name="announceoverride" />
			<parameter name="timeout">
				<para>Will cause the queue to fail out after a specified number of
				seconds, checked between each <filename>queues.conf</filename> <replaceable>timeout</replaceable> and
				<replaceable>retry</replaceable> cycle.</para>
			</parameter>
			<parameter name="AGI">
				<para>Will setup an AGI script to be executed on the calling party's channel once they are
				connected to a queue member.</para>
			</parameter>
			<parameter name="macro">
				<para>Will run a macro on the called party's channel (the queue member) once the parties are connected.</para>
			</parameter>
			<parameter name="gosub">
				<para>Will run a gosub on the called party's channel (the queue member) once the parties are connected.</para>
			</parameter>
			<parameter name="rule">
				<para>Will cause the queue's defaultrule to be overridden by the rule specified.</para>
			</parameter>
			<parameter name="position">
				<para>Attempt to enter the caller into the queue at the numerical position specified. <literal>1</literal>
				would attempt to enter the caller at the head of the queue, and <literal>3</literal> would attempt to place
				the caller third in the queue.</para>
			</parameter>
		</syntax>
		<description>
			<para>In addition to transferring the call, a call may be parked and then picked
			up by another user.</para>
			<para>This application will return to the dialplan if the queue does not exist, or
			any of the join options cause the caller to not enter the queue.</para>
			<para>This application does not automatically answer and should be preceeded
			by an application such as Answer(), Progress(), or Ringing().</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="QUEUESTATUS">
					<para>The status of the call as a text string.</para>
					<value name="TIMEOUT" />
					<value name="FULL" />
					<value name="JOINEMPTY" />
					<value name="LEAVEEMPTY" />
					<value name="JOINUNAVAIL" />
					<value name="LEAVEUNAVAIL" />
					<value name="CONTINUE" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</application>
	<application name="AddQueueMember" language="en_US">
		<synopsis>
			Dynamically adds queue members.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="interface" />
			<parameter name="penalty" />
			<parameter name="options" />
			<parameter name="membername" />
			<parameter name="stateinterface" />
		</syntax>
		<description>
			<para>Dynamically adds interface to an existing queue. If the interface is
			already in the queue it will return an error.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="AQMSTATUS">
					<para>The status of the attempt to add a queue member as a text string.</para>
					<value name="ADDED" />
					<value name="MEMBERALREADY" />
					<value name="NOSUCHQUEUE" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</application>
	<application name="RemoveQueueMember" language="en_US">
		<synopsis>
			Dynamically removes queue members.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="interface" />
		</syntax>
		<description>
			<para>If the interface is <emphasis>NOT</emphasis> in the queue it will return an error.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="RQMSTATUS">
					<value name="REMOVED" />
					<value name="NOTINQUEUE" />
					<value name="NOSUCHQUEUE" />
					<value name="NOTDYNAMIC" />
				</variable>
			</variablelist>
			<para>Example: RemoveQueueMember(techsupport,SIP/3000)</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</application>
	<application name="PauseQueueMember" language="en_US">
		<synopsis>
			Pauses a queue member.
		</synopsis>
		<syntax>
			<parameter name="queuename" />
			<parameter name="interface" required="true" />
			<parameter name="options" />
			<parameter name="reason">
				<para>Is used to add extra information to the appropriate queue_log entries and manager events.</para>
			</parameter>
		</syntax>
		<description>
			<para>Pauses (blocks calls for) a queue member. The given interface will be paused in the given queue.
			This prevents any calls from being sent from the queue to the interface until it is
			unpaused with UnpauseQueueMember or the manager interface.  If no queuename is given,
			the interface is paused in every queue it is a member of. The application will fail if the
			interface is not found.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="PQMSTATUS">
					<para>The status of the attempt to pause a queue member as a text string.</para>
					<value name="PAUSED" />
					<value name="NOTFOUND" />
				</variable>
			</variablelist>
			<para>Example: PauseQueueMember(,SIP/3000)</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</application>
	<application name="UnpauseQueueMember" language="en_US">
		<synopsis>
			Unpauses a queue member.
		</synopsis>
		<syntax>
			<parameter name="queuename" />
			<parameter name="interface" required="true" />
			<parameter name="options" />
			<parameter name="reason">
				<para>Is used to add extra information to the appropriate queue_log entries and manager events.</para>
			</parameter>
		</syntax>
		<description>
			<para>Unpauses (resumes calls to) a queue member. This is the counterpart to <literal>PauseQueueMember()</literal>
			and operates exactly the same way, except it unpauses instead of pausing the given interface.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="UPQMSTATUS">
					<para>The status of the attempt to unpause a queue member as a text string.</para>
					<value name="UNPAUSED" />
					<value name="NOTFOUND" />
				</variable>
			</variablelist>
			<para>Example: UnpauseQueueMember(,SIP/3000)</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</application>
	<application name="QueueLog" language="en_US">
		<synopsis>
			Writes to the queue_log file.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="uniqueid" required="true" />
			<parameter name="agent" required="true" />
			<parameter name="event" required="true" />
			<parameter name="additionalinfo" />
		</syntax>
		<description>
			<para>Allows you to write your own events into the queue log.</para>
			<para>Example: QueueLog(101,${UNIQUEID},${AGENT},WENTONBREAK,600)</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</application>
	<function name="QUEUE_VARIABLES" language="en_US">
		<synopsis>
			Return Queue information in variables.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true">
				<enumlist>
					<enum name="QUEUEMAX">
						<para>Maxmimum number of calls allowed.</para>
					</enum>
					<enum name="QUEUESTRATEGY">
						<para>The strategy of the queue.</para>
					</enum>
					<enum name="QUEUECALLS">
						<para>Number of calls currently in the queue.</para>
					</enum>
					<enum name="QUEUEHOLDTIME">
						<para>Current average hold time.</para>
					</enum>
					<enum name="QUEUECOMPLETED">
						<para>Number of completed calls for the queue.</para>
					</enum>
					<enum name="QUEUEABANDONED">
						<para>Number of abandoned calls.</para>
					</enum>
					<enum name="QUEUESRVLEVEL">
						<para>Queue service level.</para>
					</enum>
					<enum name="QUEUESRVLEVELPERF">
						<para>Current service level performance.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Makes the following queue variables available.</para>
			<para>Returns <literal>0</literal> if queue is found and setqueuevar is defined, <literal>-1</literal> otherwise.</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_MEMBER" language="en_US">
		<synopsis>
			Count number of members answering a queue.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="option" required="true">
				<enumlist>
					<enum name="logged">
						<para>Returns the number of logged-in members for the specified queue.</para>
					</enum>
					<enum name="free">
						<para>Returns the number of logged-in members for the specified queue that either can take calls or are currently wrapping up after a previous call.</para>
					</enum>
					<enum name="ready">
						<para>Returns the number of logged-in members for the specified queue that are immediately available to answer a call.</para>
					</enum>
					<enum name="count">
						<para>Returns the total number of members for the specified queue.</para>
					</enum>
					<enum name="penalty">
						<para>Gets or sets queue member penalty.</para>
					</enum>
					<enum name="paused">
						<para>Gets or sets queue member paused status.</para>
					</enum>
					<enum name="ringinuse">
						<para>Gets or sets queue member ringinuse.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="interface" required="false" />
		</syntax>
		<description>
			<para>Allows access to queue counts [R] and member information [R/W].</para>
			<para>
				<replaceable>queuename</replaceable> is required for all operations
				<replaceable>interface</replaceable> is required for all member operations.
			</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_MEMBER_COUNT" language="en_US">
		<synopsis>
			Count number of members answering a queue.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
		</syntax>
		<description>
			<para>Returns the number of members currently associated with the specified <replaceable>queuename</replaceable>.</para>
			<warning><para>This function has been deprecated in favor of the <literal>QUEUE_MEMBER()</literal> function</para></warning>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_EXISTS" language="en_US">
		<synopsis>
			Check if a named queue exists on this server
		</synopsis>
		<syntax>
			<parameter name="queuename" />
		</syntax>
		<description>
			<para>Returns 1 if the specified queue exists, 0 if it does not</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_GET_CHANNEL" language="en_US">
		<synopsis>
			Return caller at the specified position in a queue.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="position" />
		</syntax>
		<description>
			<para>Returns the caller channel at <replaceable>position</replaceable> in the specified <replaceable>queuename</replaceable>.</para>
			<para>If <replaceable>position</replaceable> is unspecified the first channel is returned.</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_WAITING_COUNT" language="en_US">
		<synopsis>
			Count number of calls currently waiting in a queue.
		</synopsis>
		<syntax>
			<parameter name="queuename" />
		</syntax>
		<description>
			<para>Returns the number of callers currently waiting in the specified <replaceable>queuename</replaceable>.</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_MEMBER_LIST" language="en_US">
		<synopsis>
			Returns a list of interfaces on a queue.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
		</syntax>
		<description>
			<para>Returns a comma-separated list of members associated with the specified <replaceable>queuename</replaceable>.</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<function name="QUEUE_MEMBER_PENALTY" language="en_US">
		<synopsis>
			Gets or sets queue members penalty.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="interface" required="true" />
		</syntax>
		<description>
			<para>Gets or sets queue members penalty.</para>
			<warning><para>This function has been deprecated in favor of the <literal>QUEUE_MEMBER()</literal> function</para></warning>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">QueueLog</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">QUEUE_VARIABLES</ref>
			<ref type="function">QUEUE_MEMBER</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_EXISTS</ref>
			<ref type="function">QUEUE_GET_CHANNEL</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_MEMBER_PENALTY</ref>
		</see-also>
	</function>
	<manager name="Queues" language="en_US">
		<synopsis>
			Queues.
		</synopsis>
		<syntax>
		</syntax>
		<description>
			<para>Show queues information.</para>
		</description>
	</manager>
	<manager name="QueueStatus" language="en_US">
		<synopsis>
			Show queue status.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue">
				<para>Limit the response to the status of the specified queue.</para>
			</parameter>
			<parameter name="Member">
				<para>Limit the response to the status of the specified member.</para>
			</parameter>
		</syntax>
		<description>
			<para>Check the status of one or more queues.</para>
		</description>
	</manager>
	<manager name="QueueSummary" language="en_US">
		<synopsis>
			Show queue summary.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue">
				<para>Queue for which the summary is requested.</para>
			</parameter>
		</syntax>
		<description>
			<para>Request the manager to send a QueueSummary event.</para>
		</description>
	</manager>
	<manager name="QueueAdd" language="en_US">
		<synopsis>
			Add interface to queue.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" required="true">
				<para>Queue's name.</para>
			</parameter>
			<parameter name="Interface" required="true">
				<para>The name of the interface (tech/name) to add to the queue.</para>
			</parameter>
			<parameter name="Penalty">
				<para>A penalty (number) to apply to this member. Asterisk will distribute calls to members with higher penalties only after attempting to distribute calls to those with lower penalty.</para>
			</parameter>
			<parameter name="Paused">
				<para>To pause or not the member initially (true/false or 1/0).</para>
			</parameter>
			<parameter name="MemberName">
				<para>Text alias for the interface.</para>
			</parameter>
			<parameter name="StateInterface" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueRemove" language="en_US">
		<synopsis>
			Remove interface from queue.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" required="true">
				<para>The name of the queue to take action on.</para>
			</parameter>
			<parameter name="Interface" required="true">
				<para>The interface (tech/name) to remove from queue.</para>
			</parameter>
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueuePause" language="en_US">
		<synopsis>
			Makes a queue member temporarily unavailable.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Interface" required="true">
				<para>The name of the interface (tech/name) to pause or unpause.</para>
			</parameter>
			<parameter name="Paused" required="true">
				<para>Pause or unpause the interface. Set to 'true' to pause the member or 'false' to unpause.</para>
			</parameter>
			<parameter name="Queue">
				<para>The name of the queue in which to pause or unpause this member. If not specified, the member will be paused or unpaused in all the queues it is a member of.</para>
			</parameter>
			<parameter name="Reason">
				<para>Text description, returned in the event QueueMemberPaused.</para>
			</parameter>
		</syntax>
		<description>
			<para>Pause or unpause a member in a queue.</para>
		</description>
	</manager>
	<manager name="QueueLog" language="en_US">
		<synopsis>
			Adds custom entry in queue_log.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" required="true" />
			<parameter name="Event" required="true" />
			<parameter name="Uniqueid" />
			<parameter name="Interface" />
			<parameter name="Message" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueuePenalty" language="en_US">
		<synopsis>
			Set the penalty for a queue member.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Interface" required="true">
				<para>The interface (tech/name) of the member whose penalty to change.</para>
			</parameter>
			<parameter name="Penalty" required="true">
				<para>The new penalty (number) for the member. Must be nonnegative.</para>
			</parameter>
			<parameter name="Queue">
				<para>If specified, only set the penalty for the member of this queue. Otherwise, set the penalty for the member in all queues to which the member belongs.</para>
			</parameter>
		</syntax>
		<description>
			<para>Change the penalty of a queue member</para>
		</description>
	</manager>
	<manager name="QueueMemberRingInUse" language="en_US">
		<synopsis>
			Set the ringinuse value for a queue member.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Interface" required="true" />
			<parameter name="RingInUse" required="true" />
			<parameter name="Queue" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueRule" language="en_US">
		<synopsis>
			Queue Rules.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Rule">
				<para>The name of the rule in queuerules.conf whose contents to list.</para>
			</parameter>
		</syntax>
		<description>
			<para>List queue rules defined in queuerules.conf</para>
		</description>
	</manager>
	<manager name="QueueReload" language="en_US">
		<synopsis>
			Reload a queue, queues, or any sub-section of a queue or queues.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue">
				<para>The name of the queue to take action on. If no queue name is specified, then all queues are affected.</para>
			</parameter>
			<parameter name="Members">
				<para>Whether to reload the queue's members.</para>
				<enumlist>
					<enum name="yes" />
					<enum name="no" />
				</enumlist>
			</parameter>
			<parameter name="Rules">
				<para>Whether to reload queuerules.conf</para>
				<enumlist>
					<enum name="yes" />
					<enum name="no" />
				</enumlist>
			</parameter>
			<parameter name="Parameters">
				<para>Whether to reload the other queue options.</para>
				<enumlist>
					<enum name="yes" />
					<enum name="no" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueReset" language="en_US">
		<synopsis>
			Reset queue statistics.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue">
				<para>The name of the queue on which to reset statistics.</para>
			</parameter>
		</syntax>
		<description>
			<para>Reset the statistics for a queue.</para>
		</description>
	</manager>

	<managerEvent language="en_US" name="QueueMemberStatus">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a Queue member's status has changed.</synopsis>
			<syntax>
				<parameter name="Queue">
					<para>The name of the queue.</para>
				</parameter>
				<parameter name="MemberName">
					<para>The name of the queue member.</para>
				</parameter>
				<parameter name="Interface">
					<para>The queue member's channel technology or location.</para>
				</parameter>
				<parameter name="StateInterface">
					<para>Channel technology or location from which to read device state changes.</para>
				</parameter>
				<parameter name="Membership">
					<enumlist>
						<enum name="dynamic"/>
						<enum name="realtime"/>
						<enum name="static"/>
					</enumlist>
				</parameter>
				<parameter name="Penalty">
					<para>The penalty associated with the queue member.</para>
				</parameter>
				<parameter name="CallsTaken">
					<para>The number of calls this queue member has serviced.</para>
				</parameter>
				<parameter name="LastCall">
					<para>The time this member last took a call, expressed in seconds since 00:00, Jan 1, 1970 UTC.</para>
				</parameter>
				<parameter name="Status">
					<para>The numeric device state status of the queue member.</para>
					<enumlist>
						<enum name="0"><para>AST_DEVICE_UNKNOWN</para></enum>
						<enum name="1"><para>AST_DEVICE_NOT_INUSE</para></enum>
						<enum name="2"><para>AST_DEVICE_INUSE</para></enum>
						<enum name="3"><para>AST_DEVICE_BUSY</para></enum>
						<enum name="4"><para>AST_DEVICE_INVALID</para></enum>
						<enum name="5"><para>AST_DEVICE_UNAVAILABLE</para></enum>
						<enum name="6"><para>AST_DEVICE_RINGING</para></enum>
						<enum name="7"><para>AST_DEVICE_RINGINUSE</para></enum>
						<enum name="8"><para>AST_DEVICE_ONHOLD</para></enum>
					</enumlist>
				</parameter>
				<parameter name="Paused">
					<enumlist>
						<enum name="0"/>
						<enum name="1"/>
					</enumlist>
				</parameter>
				<parameter name="Ringinuse">
					<enumlist>
						<enum name="0"/>
						<enum name="1"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueMemberAdded">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a member is added to the queue.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">QueueMemberRemoved</ref>
				<ref type="application">AddQueueMember</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueMemberRemoved">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a member is removed from the queue.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">QueueMemberAdded</ref>
				<ref type="application">RemoveQueueMember</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueMemberPause">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a member is paused/unpaused in the queue.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter)" />
				<parameter name="Reason">
					<para>The reason a member was paused.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">PauseQueueMember</ref>
				<ref type="application">UnPauseQueueMember</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueMemberPenalty">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a member's penalty is changed.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="function">QUEUE_MEMBER</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueMemberRinginuse">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a member's ringinuse setting is changed.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="function">QUEUE_MEMBER</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueCallerJoin">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a caller joins a Queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<parameter name="Position">
					<para>This channel's current position in the queue.</para>
				</parameter>
				<parameter name="Count">
					<para>The total number of channels in the queue.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">QueueCallerLeave</ref>
				<ref type="application">Queue</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueCallerLeave">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a caller leaves a Queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueCallerJoin']/managerEventInstance/syntax/parameter[@name='Count'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueCallerJoin']/managerEventInstance/syntax/parameter[@name='Position'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">QueueCallerJoin</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="QueueCallerAbandon">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a caller abandons the queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueCallerJoin']/managerEventInstance/syntax/parameter[@name='Position'])" />
				<parameter name="OriginalPosition">
					<para>The channel's original position in the queue.</para>
				</parameter>
				<parameter name="HoldTime">
					<para>The time the channel was in the queue, expressed in seconds since 00:00, Jan 1, 1970 UTC.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentCalled">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when an queue member is notified of a caller in the queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='MemberName'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Interface'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">AgentRingNoAnswer</ref>
				<ref type="managerEvent">AgentComplete</ref>
				<ref type="managerEvent">AgentConnect</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentRingNoAnswer">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a queue member is notified of a caller in the queue and fails to answer.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='MemberName'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Interface'])" />
				<parameter name="RingTime">
					<para>The time the queue member was rung, expressed in seconds since 00:00, Jan 1, 1970 UTC.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">AgentCalled</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentComplete">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a queue member has finished servicing a caller in the queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='MemberName'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Interface'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueCallerAbandon']/managerEventInstance/syntax/parameter[@name='HoldTime'])" />
				<parameter name="TalkTime">
					<para>The time the queue member talked with the caller in the queue, expressed in seconds since 00:00, Jan 1, 1970 UTC.</para>
				</parameter>
				<parameter name="Reason">
					<enumlist>
						<enum name="caller"/>
						<enum name="agent"/>
						<enum name="transfer"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">AgentCalled</ref>
				<ref type="managerEvent">AgentConnect</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentDump">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a queue member hangs up on a caller in the queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='MemberName'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Interface'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">AgentCalled</ref>
				<ref type="managerEvent">AgentConnect</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentConnect">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>Raised when a queue member answers and is bridged to a caller in the queue.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Queue'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='MemberName'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueMemberStatus']/managerEventInstance/syntax/parameter[@name='Interface'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='AgentRingNoAnswer']/managerEventInstance/syntax/parameter[@name='RingTime'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='QueueCallerAbandon']/managerEventInstance/syntax/parameter[@name='HoldTime'])" />
			</syntax>
			<see-also>
				<ref type="managerEvent">AgentCalled</ref>
				<ref type="managerEvent">AgentComplete</ref>
				<ref type="managerEvent">AgentDump</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

enum {
	OPT_MARK_AS_ANSWERED =       (1 << 0),
	OPT_GO_ON =                  (1 << 1),
	OPT_DATA_QUALITY =           (1 << 2),
	OPT_CALLEE_GO_ON =           (1 << 3),
	OPT_CALLEE_HANGUP =          (1 << 4),
	OPT_CALLER_HANGUP =          (1 << 5),
	OPT_IGNORE_CALL_FW =         (1 << 6),
	OPT_IGNORE_CONNECTEDLINE =   (1 << 7),
	OPT_CALLEE_PARK =            (1 << 8),
	OPT_CALLER_PARK =            (1 << 9),
	OPT_NO_RETRY =               (1 << 10),
	OPT_RINGING =                (1 << 11),
	OPT_RING_WHEN_RINGING =      (1 << 12),
	OPT_CALLEE_TRANSFER =        (1 << 13),
	OPT_CALLER_TRANSFER =        (1 << 14),
	OPT_CALLEE_AUTOMIXMON =      (1 << 15),
	OPT_CALLER_AUTOMIXMON =      (1 << 16),
	OPT_CALLEE_AUTOMON =         (1 << 17),
	OPT_CALLER_AUTOMON =         (1 << 18),
};

enum {
	OPT_ARG_CALLEE_GO_ON = 0,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE
};

AST_APP_OPTIONS(queue_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('C', OPT_MARK_AS_ANSWERED),
	AST_APP_OPTION('c', OPT_GO_ON),
	AST_APP_OPTION('d', OPT_DATA_QUALITY),
	AST_APP_OPTION_ARG('F', OPT_CALLEE_GO_ON, OPT_ARG_CALLEE_GO_ON),
	AST_APP_OPTION('h', OPT_CALLEE_HANGUP),
	AST_APP_OPTION('H', OPT_CALLER_HANGUP),
	AST_APP_OPTION('i', OPT_IGNORE_CALL_FW),
	AST_APP_OPTION('I', OPT_IGNORE_CONNECTEDLINE),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION('n', OPT_NO_RETRY),
	AST_APP_OPTION('r', OPT_RINGING),
	AST_APP_OPTION('R', OPT_RING_WHEN_RINGING),
	AST_APP_OPTION('t', OPT_CALLEE_TRANSFER),
	AST_APP_OPTION('T', OPT_CALLER_TRANSFER),
	AST_APP_OPTION('x', OPT_CALLEE_AUTOMIXMON),
	AST_APP_OPTION('X', OPT_CALLER_AUTOMIXMON),
	AST_APP_OPTION('w', OPT_CALLEE_AUTOMON),
	AST_APP_OPTION('W', OPT_CALLER_AUTOMON),
END_OPTIONS);

enum {
	QUEUE_STRATEGY_RINGALL = 0,
	QUEUE_STRATEGY_LEASTRECENT,
	QUEUE_STRATEGY_FEWESTCALLS,
	QUEUE_STRATEGY_RANDOM,
	QUEUE_STRATEGY_RRMEMORY,
	QUEUE_STRATEGY_LINEAR,
	QUEUE_STRATEGY_WRANDOM,
	QUEUE_STRATEGY_RRORDERED,
};

enum {
     QUEUE_AUTOPAUSE_OFF = 0,
     QUEUE_AUTOPAUSE_ON,
     QUEUE_AUTOPAUSE_ALL
};

enum queue_reload_mask {
	QUEUE_RELOAD_PARAMETERS = (1 << 0),
	QUEUE_RELOAD_MEMBER = (1 << 1),
	QUEUE_RELOAD_RULES = (1 << 2),
	QUEUE_RESET_STATS = (1 << 3),
};

static const struct strategy {
	int strategy;
	const char *name;
} strategies[] = {
	{ QUEUE_STRATEGY_RINGALL, "ringall" },
	{ QUEUE_STRATEGY_LEASTRECENT, "leastrecent" },
	{ QUEUE_STRATEGY_FEWESTCALLS, "fewestcalls" },
	{ QUEUE_STRATEGY_RANDOM, "random" },
	{ QUEUE_STRATEGY_RRMEMORY, "rrmemory" },
	{ QUEUE_STRATEGY_RRMEMORY, "roundrobin" },
	{ QUEUE_STRATEGY_LINEAR, "linear" },
	{ QUEUE_STRATEGY_WRANDOM, "wrandom"},
	{ QUEUE_STRATEGY_RRORDERED, "rrordered"},
};

static const struct autopause {
	int autopause;
	const char *name;
} autopausesmodes [] = {
	{ QUEUE_AUTOPAUSE_OFF,"no" },
	{ QUEUE_AUTOPAUSE_ON, "yes" },
	{ QUEUE_AUTOPAUSE_ALL,"all" },
};

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK			1		/*!< Recheck every second to see we we're at the top yet */
#define MAX_PERIODIC_ANNOUNCEMENTS 10           /*!< The maximum periodic announcements we can have */
/*!
 * \brief The minimum number of seconds between position announcements.
 * \note The default value of 15 provides backwards compatibility.
 */
#define DEFAULT_MIN_ANNOUNCE_FREQUENCY 15

#define MAX_QUEUE_BUCKETS 53

#define	RES_OKAY	0		/*!< Action completed */
#define	RES_EXISTS	(-1)		/*!< Entry already exists */
#define	RES_OUTOFMEMORY	(-2)		/*!< Out of memory */
#define	RES_NOSUCHQUEUE	(-3)		/*!< No such queue */
#define RES_NOT_DYNAMIC (-4)		/*!< Member is not dynamic */

static char *app = "Queue";

static char *app_aqm = "AddQueueMember" ;

static char *app_rqm = "RemoveQueueMember" ;

static char *app_pqm = "PauseQueueMember" ;

static char *app_upqm = "UnpauseQueueMember" ;

static char *app_ql = "QueueLog" ;

/*! \brief Persistent Members astdb family */
static const char * const pm_family = "Queue/PersistentMembers";

/*! \brief queues.conf [general] option */
static int queue_persistent_members = 0;

/*! \brief queues.conf per-queue weight option */
static int use_weight = 0;

/*! \brief queues.conf [general] option */
static int autofill_default = 1;

/*! \brief queues.conf [general] option */
static int montype_default = 0;

/*! \brief queues.conf [general] option */
static int shared_lastcall = 1;

/*! \brief queuesrules.conf [general] option */
static int realtime_rules = 0;

/*! \brief Subscription to device state change messages */
static struct stasis_subscription *device_state_sub;

/*! \brief queues.conf [general] option */
static int update_cdr = 0;

/*! \brief queues.conf [general] option */
static int negative_penalty_invalid = 0;

/*! \brief queues.conf [general] option */
static int log_membername_as_agent = 0;

/*! \brief name of the ringinuse field in the realtime database */
static char *realtime_ringinuse_field;

enum queue_result {
	QUEUE_UNKNOWN = 0,
	QUEUE_TIMEOUT = 1,
	QUEUE_JOINEMPTY = 2,
	QUEUE_LEAVEEMPTY = 3,
	QUEUE_JOINUNAVAIL = 4,
	QUEUE_LEAVEUNAVAIL = 5,
	QUEUE_FULL = 6,
	QUEUE_CONTINUE = 7,
};

static const struct {
	enum queue_result id;
	char *text;
} queue_results[] = {
	{ QUEUE_UNKNOWN, "UNKNOWN" },
	{ QUEUE_TIMEOUT, "TIMEOUT" },
	{ QUEUE_JOINEMPTY,"JOINEMPTY" },
	{ QUEUE_LEAVEEMPTY, "LEAVEEMPTY" },
	{ QUEUE_JOINUNAVAIL, "JOINUNAVAIL" },
	{ QUEUE_LEAVEUNAVAIL, "LEAVEUNAVAIL" },
	{ QUEUE_FULL, "FULL" },
	{ QUEUE_CONTINUE, "CONTINUE" },
};

enum queue_timeout_priority {
	TIMEOUT_PRIORITY_APP,
	TIMEOUT_PRIORITY_CONF,
};

/*! \brief We define a custom "local user" structure because we
 *  use it not only for keeping track of what is in use but
 *  also for keeping track of who we're dialing.
 *
 *  There are two "links" defined in this structure, q_next and call_next.
 *  q_next links ALL defined callattempt structures into a linked list. call_next is
 *  a link which allows for a subset of the callattempts to be traversed. This subset
 *  is used in wait_for_answer so that irrelevant callattempts are not traversed. This
 *  also is helpful so that queue logs are always accurate in the case where a call to
 *  a member times out, especially if using the ringall strategy.
*/

struct callattempt {
	struct callattempt *q_next;
	struct callattempt *call_next;
	struct ast_channel *chan;
	char interface[256];			/*!< An Asterisk dial string (not a channel name) */
	int metric;
	time_t lastcall;
	struct call_queue *lastqueue;
	struct member *member;
	/*! Saved connected party info from an AST_CONTROL_CONNECTED_LINE. */
	struct ast_party_connected_line connected;
	/*! TRUE if an AST_CONTROL_CONNECTED_LINE update was saved to the connected element. */
	unsigned int pending_connected_update:1;
	/*! TRUE if the connected line update is blocked. */
	unsigned int block_connected_update:1;
	/*! TRUE if caller id is not available for connected line */
	unsigned int dial_callerid_absent:1;
	/*! TRUE if the call is still active */
	unsigned int stillgoing:1;
	struct ast_aoc_decoded *aoc_s_rate_list;
};


struct queue_ent {
	struct call_queue *parent;             /*!< What queue is our parent */
	char moh[MAX_MUSICCLASS];              /*!< Name of musiconhold to be used */
	char announce[PATH_MAX];               /*!< Announcement to play for member when call is answered */
	char context[AST_MAX_CONTEXT];         /*!< Context when user exits queue */
	char digits[AST_MAX_EXTENSION];        /*!< Digits entered while in queue */
	int valid_digits;                      /*!< Digits entered correspond to valid extension. Exited */
	int pos;                               /*!< Where we are in the queue */
	int prio;                              /*!< Our priority */
	int last_pos_said;                     /*!< Last position we told the user */
	int ring_when_ringing;                 /*!< Should we only use ring indication when a channel is ringing? */
	time_t last_periodic_announce_time;    /*!< The last time we played a periodic announcement */
	int last_periodic_announce_sound;      /*!< The last periodic announcement we made */
	time_t last_pos;                       /*!< Last time we told the user their position */
	int opos;                              /*!< Where we started in the queue */
	int handled;                           /*!< Whether our call was handled */
	int pending;                           /*!< Non-zero if we are attempting to call a member */
	int max_penalty;                       /*!< Limit the members that can take this call to this penalty or lower */
	int min_penalty;                       /*!< Limit the members that can take this call to this penalty or higher */
	int linpos;                            /*!< If using linear strategy, what position are we at? */
	int linwrapped;                        /*!< Is the linpos wrapped? */
	time_t start;                          /*!< When we started holding */
	time_t expire;                         /*!< When this entry should expire (time out of queue) */
	int cancel_answered_elsewhere;	       /*!< Whether we should force the CAE flag on this call (C) option*/
	struct ast_channel *chan;              /*!< Our channel */
	AST_LIST_HEAD_NOLOCK(,penalty_rule) qe_rules; /*!< Local copy of the queue's penalty rules */
	struct penalty_rule *pr;               /*!< Pointer to the next penalty rule to implement */
	struct queue_ent *next;                /*!< The next queue entry */
};

struct member {
	char interface[AST_CHANNEL_NAME];    /*!< Technology/Location to dial to reach this member*/
	char state_exten[AST_MAX_EXTENSION]; /*!< Extension to get state from (if using hint) */
	char state_context[AST_MAX_CONTEXT]; /*!< Context to use when getting state (if using hint) */
	char state_interface[AST_CHANNEL_NAME]; /*!< Technology/Location from which to read devicestate changes */
	char membername[80];                 /*!< Member name to use in queue logs */
	int penalty;                         /*!< Are we a last resort? */
	int calls;                           /*!< Number of calls serviced by this member */
	int dynamic;                         /*!< Are we dynamically added? */
	int realtime;                        /*!< Is this member realtime? */
	int status;                          /*!< Status of queue member */
	int paused;                          /*!< Are we paused (not accepting calls)? */
	int queuepos;                        /*!< In what order (pertains to certain strategies) should this member be called? */
	time_t lastcall;                     /*!< When last successful call was hungup */
	struct call_queue *lastqueue;	     /*!< Last queue we received a call */
	unsigned int dead:1;                 /*!< Used to detect members deleted in realtime */
	unsigned int delme:1;                /*!< Flag to delete entry on reload */
	unsigned int call_pending:1;         /*!< TRUE if the Q is attempting to place a call to the member. */
	char rt_uniqueid[80];                /*!< Unique id of realtime member entry */
	unsigned int ringinuse:1;            /*!< Flag to ring queue members even if their status is 'inuse' */
};

enum empty_conditions {
	QUEUE_EMPTY_PENALTY = (1 << 0),
	QUEUE_EMPTY_PAUSED = (1 << 1),
	QUEUE_EMPTY_INUSE = (1 << 2),
	QUEUE_EMPTY_RINGING = (1 << 3),
	QUEUE_EMPTY_UNAVAILABLE = (1 << 4),
	QUEUE_EMPTY_INVALID = (1 << 5),
	QUEUE_EMPTY_UNKNOWN = (1 << 6),
	QUEUE_EMPTY_WRAPUP = (1 << 7),
};

enum member_properties {
	MEMBER_PENALTY = 0,
	MEMBER_RINGINUSE = 1,
};

/* values used in multi-bit flags in call_queue */
#define ANNOUNCEHOLDTIME_ALWAYS 1
#define ANNOUNCEHOLDTIME_ONCE 2
#define QUEUE_EVENT_VARIABLES 3

struct penalty_rule {
	int time;                           /*!< Number of seconds that need to pass before applying this rule */
	int max_value;                      /*!< The amount specified in the penalty rule for max penalty */
	int min_value;                      /*!< The amount specified in the penalty rule for min penalty */
	int max_relative;                   /*!< Is the max adjustment relative? 1 for relative, 0 for absolute */
	int min_relative;                   /*!< Is the min adjustment relative? 1 for relative, 0 for absolute */
	AST_LIST_ENTRY(penalty_rule) list;  /*!< Next penalty_rule */
};

#define ANNOUNCEPOSITION_YES 1 /*!< We announce position */
#define ANNOUNCEPOSITION_NO 2 /*!< We don't announce position */
#define ANNOUNCEPOSITION_MORE_THAN 3 /*!< We say "Currently there are more than <limit>" */
#define ANNOUNCEPOSITION_LIMIT 4 /*!< We not announce position more than <limit> */

struct call_queue {
	AST_DECLARE_STRING_FIELDS(
		/*! Queue name */
		AST_STRING_FIELD(name);
		/*! Music on Hold class */
		AST_STRING_FIELD(moh);
		/*! Announcement to play when call is answered */
		AST_STRING_FIELD(announce);
		/*! Exit context */
		AST_STRING_FIELD(context);
		/*! Macro to run upon member connection */
		AST_STRING_FIELD(membermacro);
		/*! Gosub to run upon member connection */
		AST_STRING_FIELD(membergosub);
		/*! Default rule to use if none specified in call to Queue() */
		AST_STRING_FIELD(defaultrule);
		/*! Sound file: "Your call is now first in line" (def. queue-youarenext) */
		AST_STRING_FIELD(sound_next);
		/*! Sound file: "There are currently" (def. queue-thereare) */
		AST_STRING_FIELD(sound_thereare);
		/*! Sound file: "calls waiting to speak to a representative." (def. queue-callswaiting) */
		AST_STRING_FIELD(sound_calls);
		/*! Sound file: "Currently there are more than" (def. queue-quantity1) */
		AST_STRING_FIELD(queue_quantity1);
		/*! Sound file: "callers waiting to speak with a representative" (def. queue-quantity2) */
		AST_STRING_FIELD(queue_quantity2);
		/*! Sound file: "The current estimated total holdtime is" (def. queue-holdtime) */
		AST_STRING_FIELD(sound_holdtime);
		/*! Sound file: "minutes." (def. queue-minutes) */
		AST_STRING_FIELD(sound_minutes);
		/*! Sound file: "minute." (def. queue-minute) */
		AST_STRING_FIELD(sound_minute);
		/*! Sound file: "seconds." (def. queue-seconds) */
		AST_STRING_FIELD(sound_seconds);
		/*! Sound file: "Thank you for your patience." (def. queue-thankyou) */
		AST_STRING_FIELD(sound_thanks);
		/*! Sound file: Custom announce for caller, no default */
		AST_STRING_FIELD(sound_callerannounce);
		/*! Sound file: "Hold time" (def. queue-reporthold) */
		AST_STRING_FIELD(sound_reporthold);
	);
	/*! Sound files: Custom announce, no default */
	struct ast_str *sound_periodicannounce[MAX_PERIODIC_ANNOUNCEMENTS];
	unsigned int dead:1;
	unsigned int ringinuse:1;
	unsigned int announce_to_first_user:1; /*!< Whether or not we announce to the first user in a queue */
	unsigned int setinterfacevar:1;
	unsigned int setqueuevar:1;
	unsigned int setqueueentryvar:1;
	unsigned int reportholdtime:1;
	unsigned int wrapped:1;
	unsigned int timeoutrestart:1;
	unsigned int announceholdtime:2;
	unsigned int announceposition:3;
	int strategy:4;
	unsigned int realtime:1;
	unsigned int found:1;
	unsigned int relativeperiodicannounce:1;
	unsigned int autopausebusy:1;
	unsigned int autopauseunavail:1;
	enum empty_conditions joinempty;
	enum empty_conditions leavewhenempty;
	int announcepositionlimit;          /*!< How many positions we announce? */
	int announcefrequency;              /*!< How often to announce their position */
	int minannouncefrequency;           /*!< The minimum number of seconds between position announcements (def. 15) */
	int periodicannouncefrequency;      /*!< How often to play periodic announcement */
	int numperiodicannounce;            /*!< The number of periodic announcements configured */
	int randomperiodicannounce;         /*!< Are periodic announcments randomly chosen */
	int roundingseconds;                /*!< How many seconds do we round to? */
	int holdtime;                       /*!< Current avg holdtime, based on an exponential average */
	int talktime;                       /*!< Current avg talktime, based on the same exponential average */
	int callscompleted;                 /*!< Number of queue calls completed */
	int callsabandoned;                 /*!< Number of queue calls abandoned */
	int servicelevel;                   /*!< seconds setting for servicelevel*/
	int callscompletedinsl;             /*!< Number of calls answered with servicelevel*/
	char monfmt[8];                     /*!< Format to use when recording calls */
	int montype;                        /*!< Monitor type  Monitor vs. MixMonitor */
	int count;                          /*!< How many entries */
	int maxlen;                         /*!< Max number of entries */
	int wrapuptime;                     /*!< Wrapup Time */
	int penaltymemberslimit;            /*!< Disregard penalty when queue has fewer than this many members */

	int retry;                          /*!< Retry calling everyone after this amount of time */
	int timeout;                        /*!< How long to wait for an answer */
	int weight;                         /*!< Respective weight */
	int autopause;                      /*!< Auto pause queue members if they fail to answer */
	int autopausedelay;                 /*!< Delay auto pause for autopausedelay seconds since last call */
	int timeoutpriority;                /*!< Do we allow a fraction of the timeout to occur for a ring? */

	/* Queue strategy things */
	int rrpos;                          /*!< Round Robin - position */
	int memberdelay;                    /*!< Seconds to delay connecting member to caller */
	int autofill;                       /*!< Ignore the head call status and ring an available agent */

	struct ao2_container *members;             /*!< Head of the list of members */
	struct queue_ent *head;             /*!< Head of the list of callers */
	AST_LIST_ENTRY(call_queue) list;    /*!< Next call queue */
	AST_LIST_HEAD_NOLOCK(, penalty_rule) rules; /*!< The list of penalty rules to invoke */
};

struct rule_list {
	char name[80];
	AST_LIST_HEAD_NOLOCK(,penalty_rule) rules;
	AST_LIST_ENTRY(rule_list) list;
};

static AST_LIST_HEAD_STATIC(rule_lists, rule_list);

static struct ao2_container *queues;

static void update_realtime_members(struct call_queue *q);
static struct member *interface_exists(struct call_queue *q, const char *interface);
static int set_member_paused(const char *queuename, const char *interface, const char *reason, int paused);

static struct member *find_member_by_queuename_and_interface(const char *queuename, const char *interface);
/*! \brief sets the QUEUESTATUS channel variable */
static void set_queue_result(struct ast_channel *chan, enum queue_result res)
{
	int i;

	for (i = 0; i < ARRAY_LEN(queue_results); i++) {
		if (queue_results[i].id == res) {
			pbx_builtin_setvar_helper(chan, "QUEUESTATUS", queue_results[i].text);
			return;
		}
	}
}

static const char *int2strat(int strategy)
{
	int x;

	for (x = 0; x < ARRAY_LEN(strategies); x++) {
		if (strategy == strategies[x].strategy) {
			return strategies[x].name;
		}
	}

	return "<unknown>";
}

static int strat2int(const char *strategy)
{
	int x;

	for (x = 0; x < ARRAY_LEN(strategies); x++) {
		if (!strcasecmp(strategy, strategies[x].name)) {
			return strategies[x].strategy;
		}
	}

	return -1;
}

static int autopause2int(const char *autopause)
{
	int x;
	/*This 'double check' that default value is OFF */
	if (ast_strlen_zero(autopause)) {
		return QUEUE_AUTOPAUSE_OFF;
	}

	/*This 'double check' is to ensure old values works */
	if(ast_true(autopause)) {
		return QUEUE_AUTOPAUSE_ON;
	}

	for (x = 0; x < ARRAY_LEN(autopausesmodes); x++) {
		if (!strcasecmp(autopause, autopausesmodes[x].name)) {
			return autopausesmodes[x].autopause;
		}
	}

	/*This 'double check' that default value is OFF */
	return QUEUE_AUTOPAUSE_OFF;
}

static int queue_hash_cb(const void *obj, const int flags)
{
	const struct call_queue *q = obj;

	return ast_str_case_hash(q->name);
}

static int queue_cmp_cb(void *obj, void *arg, int flags)
{
	struct call_queue *q = obj, *q2 = arg;
	return !strcasecmp(q->name, q2->name) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \internal
 * \brief ao2_callback, Decreases queuepos of all followers with a queuepos greater than arg.
 * \param obj the member being acted on
 * \param arg pointer to an integer containing the position value that was removed and requires reduction for anything above
 */
static int queue_member_decrement_followers(void *obj, void *arg, int flag)
{
	struct member *mem = obj;
	int *decrement_followers_after = arg;

	if (mem->queuepos > *decrement_followers_after) {
		mem->queuepos--;
	}

	return 0;
}

/*! \internal
 * \brief ao2_callback, finds members in a queue marked for deletion and in a cascading fashion runs queue_member_decrement_followers
 *        on them. This callback should always be ran before performing mass unlinking of delmarked members from queues.
 * \param obj member being acted on
 * \param arg pointer to the queue members are being removed from
 */
static int queue_delme_members_decrement_followers(void *obj, void *arg, int flag)
{
	struct member *mem = obj;
	struct call_queue *queue = arg;
	int rrpos = mem->queuepos;

	if (mem->delme) {
		ao2_callback(queue->members, OBJ_NODATA | OBJ_MULTIPLE, queue_member_decrement_followers, &rrpos);
	}

	return 0;
}

/*! \internal
 * \brief Use this to decrement followers during removal of a member
 * \param queue which queue the member is being removed from
 * \param mem which member is being removed from the queue
 */
static void queue_member_follower_removal(struct call_queue *queue, struct member *mem)
{
	int pos = mem->queuepos;

	/* If the position being removed is less than the current place in the queue, reduce the queue position by one so that we don't skip the member
	 * who would have been next otherwise. */
	if (pos < queue->rrpos) {
		queue->rrpos--;
	}

	ao2_callback(queue->members, OBJ_NODATA | OBJ_MULTIPLE, queue_member_decrement_followers, &pos);
}

#ifdef REF_DEBUG_ONLY_QUEUES
#define queue_ref(q)				_queue_ref(q, "", __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define queue_unref(q)				_queue_unref(q, "", __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define queue_t_ref(q, tag)			_queue_ref(q, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define queue_t_unref(q, tag)		_queue_unref(q, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define queues_t_link(c, q, tag)	__ao2_link_debug(c, q, 0, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define queues_t_unlink(c, q, tag)	__ao2_unlink_debug(c, q, 0, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline struct call_queue *_queue_ref(struct call_queue *q, const char *tag, const char *file, int line, const char *filename)
{
	__ao2_ref_debug(q, 1, tag, file, line, filename);
	return q;
}

static inline struct call_queue *_queue_unref(struct call_queue *q, const char *tag, const char *file, int line, const char *filename)
{
	if (q) {
		__ao2_ref_debug(q, -1, tag, file, line, filename);
	}
	return NULL;
}

#else

#define queue_t_ref(q, tag)			queue_ref(q)
#define queue_t_unref(q, tag)		queue_unref(q)
#define queues_t_link(c, q, tag)	ao2_t_link(c, q, tag)
#define queues_t_unlink(c, q, tag)	ao2_t_unlink(c, q, tag)

static inline struct call_queue *queue_ref(struct call_queue *q)
{
	ao2_ref(q, 1);
	return q;
}

static inline struct call_queue *queue_unref(struct call_queue *q)
{
	if (q) {
		ao2_ref(q, -1);
	}
	return NULL;
}
#endif

/*! \brief Set variables of queue */
static void set_queue_variables(struct call_queue *q, struct ast_channel *chan)
{
	char interfacevar[256]="";
	float sl = 0;

	ao2_lock(q);

	if (q->setqueuevar) {
		sl = 0;
		if (q->callscompleted > 0) {
			sl = 100 * ((float) q->callscompletedinsl / (float) q->callscompleted);
		}

		snprintf(interfacevar, sizeof(interfacevar),
			"QUEUENAME=%s,QUEUEMAX=%d,QUEUESTRATEGY=%s,QUEUECALLS=%d,QUEUEHOLDTIME=%d,QUEUETALKTIME=%d,QUEUECOMPLETED=%d,QUEUEABANDONED=%d,QUEUESRVLEVEL=%d,QUEUESRVLEVELPERF=%2.1f",
			q->name, q->maxlen, int2strat(q->strategy), q->count, q->holdtime, q->talktime, q->callscompleted, q->callsabandoned,  q->servicelevel, sl);

		ao2_unlock(q);

		pbx_builtin_setvar_multiple(chan, interfacevar);
	} else {
		ao2_unlock(q);
	}
}

/*! \brief Insert the 'new' entry after the 'prev' entry of queue 'q' */
static inline void insert_entry(struct call_queue *q, struct queue_ent *prev, struct queue_ent *new, int *pos)
{
	struct queue_ent *cur;

	if (!q || !new)
		return;
	if (prev) {
		cur = prev->next;
		prev->next = new;
	} else {
		cur = q->head;
		q->head = new;
	}
	new->next = cur;

	/* every queue_ent must have a reference to it's parent call_queue, this
	 * reference does not go away until the end of the queue_ent's life, meaning
	 * that even when the queue_ent leaves the call_queue this ref must remain. */
	queue_ref(q);
	new->parent = q;
	new->pos = ++(*pos);
	new->opos = *pos;
}

static struct ast_manager_event_blob *queue_channel_to_ami(const char *type, struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_str *, channel_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, event_string, NULL, ast_free);

	channel_string = ast_manager_build_channel_state_string(obj->snapshot);
	event_string = ast_manager_str_from_json_object(obj->blob, NULL);
	if (!channel_string || !event_string) {
		return NULL;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_AGENT, type,
		"%s"
		"%s",
		ast_str_buffer(channel_string),
		ast_str_buffer(event_string));
}

static struct ast_manager_event_blob *queue_caller_join_to_ami(struct stasis_message *message)
{
	return queue_channel_to_ami("QueueCallerJoin", message);
}

static struct ast_manager_event_blob *queue_caller_leave_to_ami(struct stasis_message *message)
{
	return queue_channel_to_ami("QueueCallerLeave", message);
}

static struct ast_manager_event_blob *queue_caller_abandon_to_ami(struct stasis_message *message)
{
	return queue_channel_to_ami("QueueCallerAbandon", message);
}

STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_caller_join_type,
	.to_ami = queue_caller_join_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_caller_leave_type,
	.to_ami = queue_caller_leave_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_caller_abandon_type,
	.to_ami = queue_caller_abandon_to_ami,
	);

static struct ast_manager_event_blob *queue_member_to_ami(const char *type, struct stasis_message *message)
{
	struct ast_json_payload *payload = stasis_message_data(message);
	RAII_VAR(struct ast_str *, event_string, NULL, ast_free);

	event_string = ast_manager_str_from_json_object(payload->json, NULL);
	if (!event_string) {
		return NULL;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_AGENT, type,
		"%s",
		ast_str_buffer(event_string));
}

static struct ast_manager_event_blob *queue_member_status_to_ami(struct stasis_message *message)
{
	return queue_member_to_ami("QueueMemberStatus", message);
}

static struct ast_manager_event_blob *queue_member_added_to_ami(struct stasis_message *message)
{
	return queue_member_to_ami("QueueMemberAdded", message);
}

static struct ast_manager_event_blob *queue_member_removed_to_ami(struct stasis_message *message)
{
	return queue_member_to_ami("QueueMemberRemoved", message);
}

static struct ast_manager_event_blob *queue_member_pause_to_ami(struct stasis_message *message)
{
	return queue_member_to_ami("QueueMemberPause", message);
}

static struct ast_manager_event_blob *queue_member_penalty_to_ami(struct stasis_message *message)
{
	return queue_member_to_ami("QueueMemberPenalty", message);
}

static struct ast_manager_event_blob *queue_member_ringinuse_to_ami(struct stasis_message *message)
{
	return queue_member_to_ami("QueueMemberRinginuse", message);
}

STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_member_status_type,
	.to_ami = queue_member_status_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_member_added_type,
	.to_ami = queue_member_added_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_member_removed_type,
	.to_ami = queue_member_removed_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_member_pause_type,
	.to_ami = queue_member_pause_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_member_penalty_type,
	.to_ami = queue_member_penalty_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_member_ringinuse_type,
	.to_ami = queue_member_ringinuse_to_ami,
	);

static struct ast_manager_event_blob *queue_multi_channel_to_ami(const char *type, struct stasis_message *message)
{
	struct ast_multi_channel_blob *obj = stasis_message_data(message);
	struct ast_channel_snapshot *caller;
	struct ast_channel_snapshot *agent;
	RAII_VAR(struct ast_str *, caller_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, agent_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, event_string, NULL, ast_free);

	caller = ast_multi_channel_blob_get_channel(obj, "caller");
	if (caller) {
		caller_event_string = ast_manager_build_channel_state_string(caller);
		if (!caller_event_string) {
			ast_log(LOG_NOTICE, "No caller event string, bailing\n");
			return NULL;
		}
	}

	agent = ast_multi_channel_blob_get_channel(obj, "agent");
	if (agent) {
		agent_event_string = ast_manager_build_channel_state_string_prefix(agent, "Dest");
		if (!agent_event_string) {
			ast_log(LOG_NOTICE, "No agent event string, bailing\n");
			return NULL;
		}
	}

	event_string = ast_manager_str_from_json_object(ast_multi_channel_blob_get_json(obj), NULL);
	if (!event_string) {
		return NULL;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_AGENT, type,
		"%s"
		"%s"
		"%s",
		caller_event_string ? ast_str_buffer(caller_event_string) : "",
		agent_event_string ? ast_str_buffer(agent_event_string) : "",
		ast_str_buffer(event_string));
}

static struct ast_manager_event_blob *queue_agent_called_to_ami(struct stasis_message *message)
{
	return queue_multi_channel_to_ami("AgentCalled", message);
}

static struct ast_manager_event_blob *queue_agent_connect_to_ami(struct stasis_message *message)
{
	return queue_multi_channel_to_ami("AgentConnect", message);
}

static struct ast_manager_event_blob *queue_agent_complete_to_ami(struct stasis_message *message)
{
	return queue_multi_channel_to_ami("AgentComplete", message);
}

static struct ast_manager_event_blob *queue_agent_dump_to_ami(struct stasis_message *message)
{
	return queue_multi_channel_to_ami("AgentDump", message);
}

static struct ast_manager_event_blob *queue_agent_ringnoanswer_to_ami(struct stasis_message *message)
{
	return queue_multi_channel_to_ami("AgentRingNoAnswer", message);
}

STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_agent_called_type,
	.to_ami = queue_agent_called_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_agent_connect_type,
	.to_ami = queue_agent_connect_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_agent_complete_type,
	.to_ami = queue_agent_complete_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_agent_dump_type,
	.to_ami = queue_agent_dump_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(queue_agent_ringnoanswer_type,
	.to_ami = queue_agent_ringnoanswer_to_ami,
	);

static void queue_publish_multi_channel_snapshot_blob(struct stasis_topic *topic,
		struct ast_channel_snapshot *caller_snapshot,
		struct ast_channel_snapshot *agent_snapshot,
		struct stasis_message_type *type, struct ast_json *blob)
{
	RAII_VAR(struct ast_multi_channel_blob *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!type) {
		return;
	}

	payload = ast_multi_channel_blob_create(blob);
	if (!payload) {
		return;
	}

	ast_multi_channel_blob_add_channel(payload, "caller", caller_snapshot);
	if (agent_snapshot) {
		ast_multi_channel_blob_add_channel(payload, "agent", agent_snapshot);
	}

	msg = stasis_message_create(type, payload);
	if (!msg) {
		return;
	}

	stasis_publish(topic, msg);
}

static void queue_publish_multi_channel_blob(struct ast_channel *caller, struct ast_channel *agent,
		struct stasis_message_type *type, struct ast_json *blob)
{
	RAII_VAR(struct ast_channel_snapshot *, caller_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, agent_snapshot, NULL, ao2_cleanup);

	ast_channel_lock(caller);
	caller_snapshot = ast_channel_snapshot_create(caller);
	ast_channel_unlock(caller);
	ast_channel_lock(agent);
	agent_snapshot = ast_channel_snapshot_create(agent);
	ast_channel_unlock(agent);

	if (!caller_snapshot || !agent_snapshot) {
		return;
	}

	queue_publish_multi_channel_snapshot_blob(ast_channel_topic(caller), caller_snapshot,
			agent_snapshot, type, blob);
}

/*!
 * \internal
 * \brief Publish the member blob.
 * \since 12.0.0
 *
 * \param type Stasis message type to publish.
 * \param blob The information being published.
 *
 * \note The json blob reference is passed to this function.
 *
 * \return Nothing
 */
static void queue_publish_member_blob(struct stasis_message_type *type, struct ast_json *blob)
{
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!blob || !type) {
		return;
	}

	payload = ast_json_payload_create(blob);
	ast_json_unref(blob);
	if (!payload) {
		return;
	}

	msg = stasis_message_create(type, payload);
	if (!msg) {
		return;
	}

	stasis_publish(ast_manager_get_topic(), msg);
}

static struct ast_json *queue_member_blob_create(struct call_queue *q, struct member *mem)
{
	return ast_json_pack("{s: s, s: s, s: s, s: s, s: s, s: i, s: i, s: i, s: i, s: i, s: i}",
		"Queue", q->name,
		"MemberName", mem->membername,
		"Interface", mem->interface,
		"StateInterface", mem->state_interface,
		"Membership", (mem->dynamic ? "dynamic" : (mem->realtime ? "realtime" : "static")),
		"Penalty", mem->penalty,
		"CallsTaken", mem->calls,
		"LastCall", (int)mem->lastcall,
		"Status", mem->status,
		"Paused", mem->paused,
		"Ringinuse", mem->ringinuse);
}

/*! \brief Check if members are available
 *
 * This function checks to see if members are available to be called. If any member
 * is available, the function immediately returns 0. If no members are available,
 * then -1 is returned.
 */
static int get_member_status(struct call_queue *q, int max_penalty, int min_penalty, enum empty_conditions conditions, int devstate)
{
	struct member *member;
	struct ao2_iterator mem_iter;

	ao2_lock(q);
	mem_iter = ao2_iterator_init(q->members, 0);
	for (; (member = ao2_iterator_next(&mem_iter)); ao2_ref(member, -1)) {
		if ((max_penalty != INT_MAX && member->penalty > max_penalty) || (min_penalty != INT_MAX && member->penalty < min_penalty)) {
			if (conditions & QUEUE_EMPTY_PENALTY) {
				ast_debug(4, "%s is unavailable because his penalty is not between %d and %d\n", member->membername, min_penalty, max_penalty);
				continue;
			}
		}

		switch (devstate ? ast_device_state(member->state_interface) : member->status) {
		case AST_DEVICE_INVALID:
			if (conditions & QUEUE_EMPTY_INVALID) {
				ast_debug(4, "%s is unavailable because his device state is 'invalid'\n", member->membername);
				break;
			}
			goto default_case;
		case AST_DEVICE_UNAVAILABLE:
			if (conditions & QUEUE_EMPTY_UNAVAILABLE) {
				ast_debug(4, "%s is unavailable because his device state is 'unavailable'\n", member->membername);
				break;
			}
			goto default_case;
		case AST_DEVICE_INUSE:
			if (conditions & QUEUE_EMPTY_INUSE) {
				ast_debug(4, "%s is unavailable because his device state is 'inuse'\n", member->membername);
				break;
			}
			goto default_case;
		case AST_DEVICE_RINGING:
			if (conditions & QUEUE_EMPTY_RINGING) {
				ast_debug(4, "%s is unavailable because his device state is 'ringing'\n", member->membername);
				break;
			}
			goto default_case;
		case AST_DEVICE_UNKNOWN:
			if (conditions & QUEUE_EMPTY_UNKNOWN) {
				ast_debug(4, "%s is unavailable because his device state is 'unknown'\n", member->membername);
				break;
			}
			/* Fall-through */
		default:
		default_case:
			if (member->paused && (conditions & QUEUE_EMPTY_PAUSED)) {
				ast_debug(4, "%s is unavailable because he is paused'\n", member->membername);
				break;
			} else if ((conditions & QUEUE_EMPTY_WRAPUP) && member->lastcall && q->wrapuptime && (time(NULL) - q->wrapuptime < member->lastcall)) {
				ast_debug(4, "%s is unavailable because it has only been %d seconds since his last call (wrapup time is %d)\n", member->membername, (int) (time(NULL) - member->lastcall), q->wrapuptime);
				break;
			} else {
				ao2_ref(member, -1);
				ao2_iterator_destroy(&mem_iter);
				ao2_unlock(q);
				ast_debug(4, "%s is available.\n", member->membername);
				return 0;
			}
			break;
		}
	}
	ao2_iterator_destroy(&mem_iter);
	ao2_unlock(q);

	if (!devstate && (conditions & QUEUE_EMPTY_RINGING)) {
		/* member state still may be RINGING due to lag in event message - check again with device state */
		return get_member_status(q, max_penalty, min_penalty, conditions, 1);
	}
	return -1;
}

/*! \brief set a member's status based on device state of that member's state_interface.
 *
 * Lock interface list find sc, iterate through each queues queue_member list for member to
 * update state inside queues
*/
static void update_status(struct call_queue *q, struct member *m, const int status)
{
	m->status = status;

	queue_publish_member_blob(queue_member_status_type(), queue_member_blob_create(q, m));
}

/*!
 * \internal
 * \brief Determine if a queue member is available
 * \retval 1 if the member is available
 * \retval 0 if the member is not available
 */
static int is_member_available(struct call_queue *q, struct member *mem)
{
	int available = 0;

	switch (mem->status) {
		case AST_DEVICE_INVALID:
		case AST_DEVICE_UNAVAILABLE:
			break;
		case AST_DEVICE_INUSE:
		case AST_DEVICE_BUSY:
		case AST_DEVICE_RINGING:
		case AST_DEVICE_RINGINUSE:
		case AST_DEVICE_ONHOLD:
			if (!mem->ringinuse) {
				break;
			}
			/* else fall through */
		case AST_DEVICE_NOT_INUSE:
		case AST_DEVICE_UNKNOWN:
			if (!mem->paused) {
				available = 1;
			}
			break;
	}

	/* Let wrapuptimes override device state availability */
	if (mem->lastcall && q->wrapuptime && (time(NULL) - q->wrapuptime < mem->lastcall)) {
		available = 0;
	}
	return available;
}

/*! \brief set a member's status based on device state of that member's interface*/
static void device_state_cb(void *unused, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ao2_iterator miter, qiter;
	struct ast_device_state_message *dev_state;
	struct member *m;
	struct call_queue *q;
	char interface[80], *slash_pos;
	int found = 0;			/* Found this member in any queue */
	int found_member;		/* Found this member in this queue */
	int avail = 0;			/* Found an available member in this queue */

	if (ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	dev_state = stasis_message_data(msg);
	if (dev_state->eid) {
		/* ignore non-aggregate states */
		return;
	}

	qiter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&qiter, "Iterate over queues"))) {
		ao2_lock(q);

		avail = 0;
		found_member = 0;
		miter = ao2_iterator_init(q->members, 0);
		for (; (m = ao2_iterator_next(&miter)); ao2_ref(m, -1)) {
			if (!found_member) {
				ast_copy_string(interface, m->state_interface, sizeof(interface));

				if ((slash_pos = strchr(interface, '/'))) {
					if (!strncasecmp(interface, "Local/", 6) && (slash_pos = strchr(slash_pos + 1, '/'))) {
						*slash_pos = '\0';
					}
				}

				if (!strcasecmp(interface, dev_state->device)) {
					found_member = 1;
					update_status(q, m, dev_state->state);
				}
			}

			/* check every member until we find one NOT_INUSE */
			if (!avail) {
				avail = is_member_available(q, m);
			}
			if (avail && found_member) {
				/* early exit as we've found an available member and the member of interest */
				ao2_ref(m, -1);
				break;
			}
		}

		if (found_member) {
			found = 1;
			if (avail) {
				ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
			} else {
				ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
			}
		}

		ao2_iterator_destroy(&miter);

		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&qiter);

	if (found) {
		ast_debug(1, "Device '%s' changed to state '%u' (%s)\n",
			dev_state->device,
			dev_state->state,
			ast_devstate2str(dev_state->state));
	} else {
		ast_debug(3, "Device '%s' changed to state '%u' (%s) but we don't care because they're not a member of any queue.\n",
			dev_state->device,
			dev_state->state,
			ast_devstate2str(dev_state->state));
	}

	return;
}

/*! \brief Helper function which converts from extension state to device state values */
static int extensionstate2devicestate(int state)
{
	switch (state) {
	case AST_EXTENSION_NOT_INUSE:
		state = AST_DEVICE_NOT_INUSE;
		break;
	case AST_EXTENSION_INUSE:
		state = AST_DEVICE_INUSE;
		break;
	case AST_EXTENSION_BUSY:
		state = AST_DEVICE_BUSY;
		break;
	case AST_EXTENSION_RINGING:
		state = AST_DEVICE_RINGING;
		break;
	case AST_EXTENSION_ONHOLD:
		state = AST_DEVICE_ONHOLD;
		break;
	case AST_EXTENSION_UNAVAILABLE:
		state = AST_DEVICE_UNAVAILABLE;
		break;
	case AST_EXTENSION_REMOVED:
	case AST_EXTENSION_DEACTIVATED:
	default:
		state = AST_DEVICE_INVALID;
		break;
	}

	return state;
}

static int extension_state_cb(char *context, char *exten, struct ast_state_cb_info *info, void *data)
{
	struct ao2_iterator miter, qiter;
	struct member *m;
	struct call_queue *q;
	int state = info->exten_state;
	int found = 0, device_state = extensionstate2devicestate(state);

	/* only interested in extension state updates involving device states */
	if (info->reason != AST_HINT_UPDATE_DEVICE) {
		return 0;
	}

	qiter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&qiter, "Iterate through queues"))) {
		ao2_lock(q);

		miter = ao2_iterator_init(q->members, 0);
		for (; (m = ao2_iterator_next(&miter)); ao2_ref(m, -1)) {
			if (!strcmp(m->state_context, context) && !strcmp(m->state_exten, exten)) {
				update_status(q, m, device_state);
				ao2_ref(m, -1);
				found = 1;
				break;
			}
		}
		ao2_iterator_destroy(&miter);

		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&qiter);

        if (found) {
		ast_debug(1, "Extension '%s@%s' changed to state '%d' (%s)\n", exten, context, device_state, ast_devstate2str(device_state));
	} else {
		ast_debug(3, "Extension '%s@%s' changed to state '%d' (%s) but we don't care because they're not a member of any queue.\n",
			  exten, context, device_state, ast_devstate2str(device_state));
	}

	return 0;
}

/*! \brief Return the current state of a member */
static int get_queue_member_status(struct member *cur)
{
	return ast_strlen_zero(cur->state_exten) ? ast_device_state(cur->state_interface) : extensionstate2devicestate(ast_extension_state(NULL, cur->state_context, cur->state_exten));
}

/*! \brief allocate space for new queue member and set fields based on parameters passed */
static struct member *create_queue_member(const char *interface, const char *membername, int penalty, int paused, const char *state_interface, int ringinuse)
{
	struct member *cur;

	if ((cur = ao2_alloc(sizeof(*cur), NULL))) {
		cur->ringinuse = ringinuse;
		cur->penalty = penalty;
		cur->paused = paused;
		ast_copy_string(cur->interface, interface, sizeof(cur->interface));
		if (!ast_strlen_zero(state_interface)) {
			ast_copy_string(cur->state_interface, state_interface, sizeof(cur->state_interface));
		} else {
			ast_copy_string(cur->state_interface, interface, sizeof(cur->state_interface));
		}
		if (!ast_strlen_zero(membername)) {
			ast_copy_string(cur->membername, membername, sizeof(cur->membername));
		} else {
			ast_copy_string(cur->membername, interface, sizeof(cur->membername));
		}
		if (!strchr(cur->interface, '/')) {
			ast_log(LOG_WARNING, "No location at interface '%s'\n", interface);
		}
		if (!strncmp(cur->state_interface, "hint:", 5)) {
			char *tmp = ast_strdupa(cur->state_interface), *context = tmp;
			char *exten = strsep(&context, "@") + 5;

			ast_copy_string(cur->state_exten, exten, sizeof(cur->state_exten));
			ast_copy_string(cur->state_context, S_OR(context, "default"), sizeof(cur->state_context));
		}
		cur->status = get_queue_member_status(cur);
	}

	return cur;
}


static int compress_char(const char c)
{
	if (c < 32) {
		return 0;
	} else if (c > 96) {
		return c - 64;
	}
	return c - 32;
}

static int member_hash_fn(const void *obj, const int flags)
{
	const struct member *mem = obj;
	const char *interface = (flags & OBJ_KEY) ? obj : mem->interface;
	const char *chname = strchr(interface, '/');
	int ret = 0, i;

	if (!chname) {
		chname = interface;
	}
	for (i = 0; i < 5 && chname[i]; i++) {
		ret += compress_char(chname[i]) << (i * 6);
	}
	return ret;
}

static int member_cmp_fn(void *obj1, void *obj2, int flags)
{
	struct member *mem1 = obj1;
	struct member *mem2 = obj2;
	const char *interface = (flags & OBJ_KEY) ? obj2 : mem2->interface;

	return strcasecmp(mem1->interface, interface) ? 0 : CMP_MATCH | CMP_STOP;
}

/*!
 * \brief Initialize Queue default values.
 * \note the queue's lock  must be held before executing this function
*/
static void init_queue(struct call_queue *q)
{
	int i;
	struct penalty_rule *pr_iter;

	q->dead = 0;
	q->retry = DEFAULT_RETRY;
	q->timeout = DEFAULT_TIMEOUT;
	q->maxlen = 0;
	q->announcefrequency = 0;
	q->minannouncefrequency = DEFAULT_MIN_ANNOUNCE_FREQUENCY;
	q->announceholdtime = 1;
	q->announcepositionlimit = 10; /* Default 10 positions */
	q->announceposition = ANNOUNCEPOSITION_YES; /* Default yes */
	q->roundingseconds = 0; /* Default - don't announce seconds */
	q->servicelevel = 0;
	q->ringinuse = 1;
	q->announce_to_first_user = 0;
	q->setinterfacevar = 0;
	q->setqueuevar = 0;
	q->setqueueentryvar = 0;
	q->autofill = autofill_default;
	q->montype = montype_default;
	q->monfmt[0] = '\0';
	q->reportholdtime = 0;
	q->wrapuptime = 0;
	q->penaltymemberslimit = 0;
	q->joinempty = 0;
	q->leavewhenempty = 0;
	q->memberdelay = 0;
	q->weight = 0;
	q->timeoutrestart = 0;
	q->periodicannouncefrequency = 0;
	q->randomperiodicannounce = 0;
	q->numperiodicannounce = 0;
	q->autopause = QUEUE_AUTOPAUSE_OFF;
	q->timeoutpriority = TIMEOUT_PRIORITY_APP;
	q->autopausedelay = 0;
	if (!q->members) {
		if (q->strategy == QUEUE_STRATEGY_LINEAR || q->strategy == QUEUE_STRATEGY_RRORDERED) {
			/* linear strategy depends on order, so we have to place all members in a single bucket */
			q->members = ao2_container_alloc(1, member_hash_fn, member_cmp_fn);
		} else {
			q->members = ao2_container_alloc(37, member_hash_fn, member_cmp_fn);
		}
	}
	q->found = 1;

	ast_string_field_set(q, sound_next, "queue-youarenext");
	ast_string_field_set(q, sound_thereare, "queue-thereare");
	ast_string_field_set(q, sound_calls, "queue-callswaiting");
	ast_string_field_set(q, queue_quantity1, "queue-quantity1");
	ast_string_field_set(q, queue_quantity2, "queue-quantity2");
	ast_string_field_set(q, sound_holdtime, "queue-holdtime");
	ast_string_field_set(q, sound_minutes, "queue-minutes");
	ast_string_field_set(q, sound_minute, "queue-minute");
	ast_string_field_set(q, sound_seconds, "queue-seconds");
	ast_string_field_set(q, sound_thanks, "queue-thankyou");
	ast_string_field_set(q, sound_reporthold, "queue-reporthold");

	if (!q->sound_periodicannounce[0]) {
		q->sound_periodicannounce[0] = ast_str_create(32);
	}

	if (q->sound_periodicannounce[0]) {
		ast_str_set(&q->sound_periodicannounce[0], 0, "queue-periodic-announce");
	}

	for (i = 1; i < MAX_PERIODIC_ANNOUNCEMENTS; i++) {
		if (q->sound_periodicannounce[i]) {
			ast_str_set(&q->sound_periodicannounce[i], 0, "%s", "");
		}
	}

	while ((pr_iter = AST_LIST_REMOVE_HEAD(&q->rules,list))) {
		ast_free(pr_iter);
	}

	/* On restart assume no members are available.
	 * The queue_avail hint is a boolean state to indicate whether a member is available or not.
	 *
	 * This seems counter intuitive, but is required to light a BLF
	 * AST_DEVICE_INUSE indicates no members are available.
	 * AST_DEVICE_NOT_INUSE indicates a member is available.
	 */
	ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
}

static void clear_queue(struct call_queue *q)
{
	q->holdtime = 0;
	q->callscompleted = 0;
	q->callsabandoned = 0;
	q->callscompletedinsl = 0;
	q->talktime = 0;

	if (q->members) {
		struct member *mem;
		struct ao2_iterator mem_iter = ao2_iterator_init(q->members, 0);
		while ((mem = ao2_iterator_next(&mem_iter))) {
			mem->calls = 0;
			mem->lastcall = 0;
			ao2_ref(mem, -1);
		}
		ao2_iterator_destroy(&mem_iter);
	}
}

/*!
 * \brief Change queue penalty by adding rule.
 *
 * Check rule for errors with time or fomatting, see if rule is relative to rest
 * of queue, iterate list of rules to find correct insertion point, insert and return.
 * \retval -1 on failure
 * \retval 0 on success
 * \note Call this with the rule_lists locked
*/
static int insert_penaltychange(const char *list_name, const char *content, const int linenum)
{
	char *timestr, *maxstr, *minstr, *contentdup;
	struct penalty_rule *rule = NULL, *rule_iter;
	struct rule_list *rl_iter;
	int penaltychangetime, inserted = 0;

	if (!(rule = ast_calloc(1, sizeof(*rule)))) {
		return -1;
	}

	contentdup = ast_strdupa(content);

	if (!(maxstr = strchr(contentdup, ','))) {
		ast_log(LOG_WARNING, "Improperly formatted penaltychange rule at line %d. Ignoring.\n", linenum);
		ast_free(rule);
		return -1;
	}

	*maxstr++ = '\0';
	timestr = contentdup;

	if ((penaltychangetime = atoi(timestr)) < 0) {
		ast_log(LOG_WARNING, "Improper time parameter specified for penaltychange rule at line %d. Ignoring.\n", linenum);
		ast_free(rule);
		return -1;
	}

	rule->time = penaltychangetime;

	if ((minstr = strchr(maxstr,','))) {
		*minstr++ = '\0';
	}

	/* The last check will evaluate true if either no penalty change is indicated for a given rule
	 * OR if a min penalty change is indicated but no max penalty change is */
	if (*maxstr == '+' || *maxstr == '-' || *maxstr == '\0') {
		rule->max_relative = 1;
	}

	rule->max_value = atoi(maxstr);

	if (!ast_strlen_zero(minstr)) {
		if (*minstr == '+' || *minstr == '-') {
			rule->min_relative = 1;
		}
		rule->min_value = atoi(minstr);
	} else { /*there was no minimum specified, so assume this means no change*/
		rule->min_relative = 1;
	}

	/*We have the rule made, now we need to insert it where it belongs*/
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list){
		if (strcasecmp(rl_iter->name, list_name)) {
			continue;
		}

		AST_LIST_TRAVERSE_SAFE_BEGIN(&rl_iter->rules, rule_iter, list) {
			if (rule->time < rule_iter->time) {
				AST_LIST_INSERT_BEFORE_CURRENT(rule, list);
				inserted = 1;
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (!inserted) {
			AST_LIST_INSERT_TAIL(&rl_iter->rules, rule, list);
			inserted = 1;
		}

		break;
	}

	if (!inserted) {
		ast_log(LOG_WARNING, "Unknown rule list name %s; ignoring.\n", list_name);
		ast_free(rule);
		return -1;
	}
	return 0;
}

/*!
 * \brief Load queue rules from realtime.
 *
 * Check rule for errors with time or fomatting, see if rule is relative to rest
 * of queue, iterate list of rules to find correct insertion point, insert and return.
 * \retval -1 on failure
 * \retval 0 on success
 * \note Call this with the rule_lists locked
*/
static int load_realtime_rules(void)
{
	struct ast_config *cfg;
        struct rule_list *rl_iter, *new_rl;
        struct penalty_rule *pr_iter;
        char *rulecat = NULL;

	if (!ast_check_realtime("queue_rules")) {
		ast_log(LOG_WARNING, "Missing \"queue_rules\" in extconfig.conf\n");
		return 0;
	}
	if (!(cfg = ast_load_realtime_multientry("queue_rules", "rule_name LIKE", "%", SENTINEL))) {
		ast_log(LOG_WARNING, "Failed to load queue rules from realtime\n");
		return 0;
	}
	while ((rulecat = ast_category_browse(cfg, rulecat)) && !ast_strlen_zero(rulecat)) {
		const char *timestr, *maxstr, *minstr;
		int penaltychangetime, rule_exists = 0, inserted = 0;
		int max_penalty = 0, min_penalty = 0, min_relative = 0, max_relative = 0;
		struct penalty_rule *new_penalty_rule = NULL;
		AST_LIST_TRAVERSE(&rule_lists, rl_iter, list) {
			if (!(strcasecmp(rl_iter->name, rulecat))) {
				rule_exists = 1;
				new_rl = rl_iter;
				break;
			}
		}
		if (!rule_exists) {
			if (!(new_rl = ast_calloc(1, sizeof(*new_rl)))) {
				ast_config_destroy(cfg);
				return -1;
			}
			ast_copy_string(new_rl->name, rulecat, sizeof(new_rl->name));
			AST_LIST_INSERT_TAIL(&rule_lists, new_rl, list);
		}
		timestr = ast_variable_retrieve(cfg, rulecat, "time");
		if (!(timestr) || sscanf(timestr, "%30d", &penaltychangetime) != 1) {
			ast_log(LOG_NOTICE, "Failed to parse time (%s) for one of the %s rules,	skipping it\n",
				(ast_strlen_zero(timestr) ? "invalid value" : timestr), rulecat);
			continue;
		}
		if (!(new_penalty_rule = ast_calloc(1, sizeof(*new_penalty_rule)))) {
			ast_config_destroy(cfg);
			return -1;
		}
		if (!(maxstr = ast_variable_retrieve(cfg, rulecat, "max_penalty")) ||
			ast_strlen_zero(maxstr) || sscanf(maxstr, "%30d", &max_penalty) != 1) {
			max_penalty = 0;
			max_relative = 1;
		} else {
			if (*maxstr == '+' || *maxstr == '-') {
				max_relative = 1;
			}
		}
		if (!(minstr = ast_variable_retrieve(cfg, rulecat, "min_penalty")) ||
			ast_strlen_zero(minstr) || sscanf(minstr, "%30d", &min_penalty) != 1) {
			min_penalty = 0;
			min_relative = 1;
		} else {
			if (*minstr == '+' || *minstr == '-') {
				min_relative = 1;
			}
		}
		new_penalty_rule->time = penaltychangetime;
		new_penalty_rule->max_relative = max_relative;
		new_penalty_rule->max_value = max_penalty;
		new_penalty_rule->min_relative = min_relative;
		new_penalty_rule->min_value = min_penalty;
		AST_LIST_TRAVERSE_SAFE_BEGIN(&new_rl->rules, pr_iter, list) {
			if (new_penalty_rule->time < pr_iter->time) {
				AST_LIST_INSERT_BEFORE_CURRENT(new_penalty_rule, list);
				inserted = 1;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
		if (!inserted) {
			AST_LIST_INSERT_TAIL(&new_rl->rules, new_penalty_rule, list);
		}
	}

	ast_config_destroy(cfg);
	return 0;
}

static void parse_empty_options(const char *value, enum empty_conditions *empty, int joinempty)
{
	char *value_copy = ast_strdupa(value);
	char *option = NULL;
	while ((option = strsep(&value_copy, ","))) {
		if (!strcasecmp(option, "paused")) {
			*empty |= QUEUE_EMPTY_PAUSED;
		} else if (!strcasecmp(option, "penalty")) {
			*empty |= QUEUE_EMPTY_PENALTY;
		} else if (!strcasecmp(option, "inuse")) {
			*empty |= QUEUE_EMPTY_INUSE;
		} else if (!strcasecmp(option, "ringing")) {
			*empty |= QUEUE_EMPTY_RINGING;
		} else if (!strcasecmp(option, "invalid")) {
			*empty |= QUEUE_EMPTY_INVALID;
		} else if (!strcasecmp(option, "wrapup")) {
			*empty |= QUEUE_EMPTY_WRAPUP;
		} else if (!strcasecmp(option, "unavailable")) {
			*empty |= QUEUE_EMPTY_UNAVAILABLE;
		} else if (!strcasecmp(option, "unknown")) {
			*empty |= QUEUE_EMPTY_UNKNOWN;
		} else if (!strcasecmp(option, "loose")) {
			*empty = (QUEUE_EMPTY_PENALTY | QUEUE_EMPTY_INVALID);
		} else if (!strcasecmp(option, "strict")) {
			*empty = (QUEUE_EMPTY_PENALTY | QUEUE_EMPTY_INVALID | QUEUE_EMPTY_PAUSED | QUEUE_EMPTY_UNAVAILABLE);
		} else if ((ast_false(option) && joinempty) || (ast_true(option) && !joinempty)) {
			*empty = (QUEUE_EMPTY_PENALTY | QUEUE_EMPTY_INVALID | QUEUE_EMPTY_PAUSED);
		} else if ((ast_false(option) && !joinempty) || (ast_true(option) && joinempty)) {
			*empty = 0;
		} else {
			ast_log(LOG_WARNING, "Unknown option %s for '%s'\n", option, joinempty ? "joinempty" : "leavewhenempty");
		}
	}
}

/*! \brief Configure a queue parameter.
 *
 * The failunknown flag is set for config files (and static realtime) to show
 * errors for unknown parameters. It is cleared for dynamic realtime to allow
 *  extra fields in the tables.
 * \note For error reporting, line number is passed for .conf static configuration,
 * for Realtime queues, linenum is -1.
*/
static void queue_set_param(struct call_queue *q, const char *param, const char *val, int linenum, int failunknown)
{
	if (!strcasecmp(param, "musicclass") ||
		!strcasecmp(param, "music") || !strcasecmp(param, "musiconhold")) {
		ast_string_field_set(q, moh, val);
	} else if (!strcasecmp(param, "announce")) {
		ast_string_field_set(q, announce, val);
	} else if (!strcasecmp(param, "context")) {
		ast_string_field_set(q, context, val);
	} else if (!strcasecmp(param, "timeout")) {
		q->timeout = atoi(val);
		if (q->timeout < 0) {
			q->timeout = DEFAULT_TIMEOUT;
		}
	} else if (!strcasecmp(param, "ringinuse")) {
		q->ringinuse = ast_true(val);
	} else if (!strcasecmp(param, "setinterfacevar")) {
		q->setinterfacevar = ast_true(val);
	} else if (!strcasecmp(param, "setqueuevar")) {
		q->setqueuevar = ast_true(val);
	} else if (!strcasecmp(param, "setqueueentryvar")) {
		q->setqueueentryvar = ast_true(val);
	} else if (!strcasecmp(param, "monitor-format")) {
		ast_copy_string(q->monfmt, val, sizeof(q->monfmt));
	} else if (!strcasecmp(param, "membermacro")) {
		ast_string_field_set(q, membermacro, val);
	} else if (!strcasecmp(param, "membergosub")) {
		ast_string_field_set(q, membergosub, val);
	} else if (!strcasecmp(param, "queue-youarenext")) {
		ast_string_field_set(q, sound_next, val);
	} else if (!strcasecmp(param, "queue-thereare")) {
		ast_string_field_set(q, sound_thereare, val);
	} else if (!strcasecmp(param, "queue-callswaiting")) {
		ast_string_field_set(q, sound_calls, val);
	} else if (!strcasecmp(param, "queue-quantity1")) {
		ast_string_field_set(q, queue_quantity1, val);
	} else if (!strcasecmp(param, "queue-quantity2")) {
		ast_string_field_set(q, queue_quantity2, val);
	} else if (!strcasecmp(param, "queue-holdtime")) {
		ast_string_field_set(q, sound_holdtime, val);
	} else if (!strcasecmp(param, "queue-minutes")) {
		ast_string_field_set(q, sound_minutes, val);
	} else if (!strcasecmp(param, "queue-minute")) {
		ast_string_field_set(q, sound_minute, val);
	} else if (!strcasecmp(param, "queue-seconds")) {
		ast_string_field_set(q, sound_seconds, val);
	} else if (!strcasecmp(param, "queue-thankyou")) {
		ast_string_field_set(q, sound_thanks, val);
	} else if (!strcasecmp(param, "queue-callerannounce")) {
		ast_string_field_set(q, sound_callerannounce, val);
	} else if (!strcasecmp(param, "queue-reporthold")) {
		ast_string_field_set(q, sound_reporthold, val);
	} else if (!strcasecmp(param, "announce-frequency")) {
		q->announcefrequency = atoi(val);
	} else if (!strcasecmp(param, "announce-to-first-user")) {
		q->announce_to_first_user = ast_true(val);
	} else if (!strcasecmp(param, "min-announce-frequency")) {
		q->minannouncefrequency = atoi(val);
		ast_debug(1, "%s=%s for queue '%s'\n", param, val, q->name);
	} else if (!strcasecmp(param, "announce-round-seconds")) {
		q->roundingseconds = atoi(val);
		/* Rounding to any other values just doesn't make sense... */
		if (!(q->roundingseconds == 0 || q->roundingseconds == 5 || q->roundingseconds == 10
			|| q->roundingseconds == 15 || q->roundingseconds == 20 || q->roundingseconds == 30)) {
			if (linenum >= 0) {
				ast_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s' at line %d of queues.conf\n",
					val, param, q->name, linenum);
			} else {
				ast_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s'\n", val, param, q->name);
			}
			q->roundingseconds=0;
		}
	} else if (!strcasecmp(param, "announce-holdtime")) {
		if (!strcasecmp(val, "once")) {
			q->announceholdtime = ANNOUNCEHOLDTIME_ONCE;
		} else if (ast_true(val)) {
			q->announceholdtime = ANNOUNCEHOLDTIME_ALWAYS;
		} else {
			q->announceholdtime = 0;
		}
	} else if (!strcasecmp(param, "announce-position")) {
		if (!strcasecmp(val, "limit")) {
			q->announceposition = ANNOUNCEPOSITION_LIMIT;
		} else if (!strcasecmp(val, "more")) {
			q->announceposition = ANNOUNCEPOSITION_MORE_THAN;
		} else if (ast_true(val)) {
			q->announceposition = ANNOUNCEPOSITION_YES;
		} else {
			q->announceposition = ANNOUNCEPOSITION_NO;
		}
	} else if (!strcasecmp(param, "announce-position-limit")) {
		q->announcepositionlimit = atoi(val);
	} else if (!strcasecmp(param, "periodic-announce")) {
		if (strchr(val, ',')) {
			char *s, *buf = ast_strdupa(val);
			unsigned int i = 0;

			while ((s = strsep(&buf, ",|"))) {
				if (!q->sound_periodicannounce[i]) {
					q->sound_periodicannounce[i] = ast_str_create(16);
				}
				ast_str_set(&q->sound_periodicannounce[i], 0, "%s", s);
				i++;
				if (i == MAX_PERIODIC_ANNOUNCEMENTS) {
					break;
				}
			}
			q->numperiodicannounce = i;
		} else {
			ast_str_set(&q->sound_periodicannounce[0], 0, "%s", val);
			q->numperiodicannounce = 1;
		}
	} else if (!strcasecmp(param, "periodic-announce-frequency")) {
		q->periodicannouncefrequency = atoi(val);
	} else if (!strcasecmp(param, "relative-periodic-announce")) {
		q->relativeperiodicannounce = ast_true(val);
	} else if (!strcasecmp(param, "random-periodic-announce")) {
		q->randomperiodicannounce = ast_true(val);
	} else if (!strcasecmp(param, "retry")) {
		q->retry = atoi(val);
		if (q->retry <= 0) {
			q->retry = DEFAULT_RETRY;
		}
	} else if (!strcasecmp(param, "wrapuptime")) {
		q->wrapuptime = atoi(val);
	} else if (!strcasecmp(param, "penaltymemberslimit")) {
		if ((sscanf(val, "%10d", &q->penaltymemberslimit) != 1)) {
			q->penaltymemberslimit = 0;
		}
	} else if (!strcasecmp(param, "autofill")) {
		q->autofill = ast_true(val);
	} else if (!strcasecmp(param, "monitor-type")) {
		if (!strcasecmp(val, "mixmonitor")) {
			q->montype = 1;
		}
	} else if (!strcasecmp(param, "autopause")) {
		q->autopause = autopause2int(val);
	} else if (!strcasecmp(param, "autopausedelay")) {
		q->autopausedelay = atoi(val);
	} else if (!strcasecmp(param, "autopausebusy")) {
		q->autopausebusy = ast_true(val);
	} else if (!strcasecmp(param, "autopauseunavail")) {
		q->autopauseunavail = ast_true(val);
	} else if (!strcasecmp(param, "maxlen")) {
		q->maxlen = atoi(val);
		if (q->maxlen < 0) {
			q->maxlen = 0;
		}
	} else if (!strcasecmp(param, "servicelevel")) {
		q->servicelevel= atoi(val);
	} else if (!strcasecmp(param, "strategy")) {
		int strategy;

		/* We are a static queue and already have set this, no need to do it again */
		if (failunknown) {
			return;
		}
		strategy = strat2int(val);
		if (strategy < 0) {
			ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
				val, q->name);
			q->strategy = QUEUE_STRATEGY_RINGALL;
		}
		if (strategy == q->strategy) {
			return;
		}
		if (strategy == QUEUE_STRATEGY_LINEAR) {
			ast_log(LOG_WARNING, "Changing to the linear strategy currently requires asterisk to be restarted.\n");
			return;
		}
		q->strategy = strategy;
	} else if (!strcasecmp(param, "joinempty")) {
		parse_empty_options(val, &q->joinempty, 1);
	} else if (!strcasecmp(param, "leavewhenempty")) {
		parse_empty_options(val, &q->leavewhenempty, 0);
	} else if (!strcasecmp(param, "reportholdtime")) {
		q->reportholdtime = ast_true(val);
	} else if (!strcasecmp(param, "memberdelay")) {
		q->memberdelay = atoi(val);
	} else if (!strcasecmp(param, "weight")) {
		q->weight = atoi(val);
	} else if (!strcasecmp(param, "timeoutrestart")) {
		q->timeoutrestart = ast_true(val);
	} else if (!strcasecmp(param, "defaultrule")) {
		ast_string_field_set(q, defaultrule, val);
	} else if (!strcasecmp(param, "timeoutpriority")) {
		if (!strcasecmp(val, "conf")) {
			q->timeoutpriority = TIMEOUT_PRIORITY_CONF;
		} else {
			q->timeoutpriority = TIMEOUT_PRIORITY_APP;
		}
	} else if (failunknown) {
		if (linenum >= 0) {
			ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s at line %d of queues.conf\n",
				q->name, param, linenum);
		} else {
			ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s\n", q->name, param);
		}
	}
}


#define QUEUE_PAUSED_DEVSTATE AST_DEVICE_INUSE
#define QUEUE_UNPAUSED_DEVSTATE AST_DEVICE_NOT_INUSE
#define QUEUE_UNKNOWN_PAUSED_DEVSTATE AST_DEVICE_NOT_INUSE

/*! \internal
 * \brief If adding a single new member to a queue, use this function instead of ao2_linking.
 *        This adds round robin queue position data for a fresh member as well as links it.
 * \param queue Which queue the member is being added to
 * \param mem Which member is being added to the queue
 */
static void member_add_to_queue(struct call_queue *queue, struct member *mem)
{
	ao2_lock(queue->members);
	mem->queuepos = ao2_container_count(queue->members);
	ao2_link(queue->members, mem);
	ast_devstate_changed(mem->paused ? QUEUE_PAUSED_DEVSTATE : QUEUE_UNPAUSED_DEVSTATE,
		AST_DEVSTATE_CACHABLE, "Queue:%s_pause_%s", queue->name, mem->interface);
	ao2_unlock(queue->members);
}

/*! \internal
 * \brief If removing a single member from a queue, use this function instead of ao2_unlinking.
 *        This will perform round robin queue position reordering for the remaining members.
 * \param queue Which queue the member is being removed from
 * \param member Which member is being removed from the queue
 */
static void member_remove_from_queue(struct call_queue *queue, struct member *mem)
{
	ao2_lock(queue->members);
	ast_devstate_changed(QUEUE_UNKNOWN_PAUSED_DEVSTATE, AST_DEVSTATE_CACHABLE, "Queue:%s_pause_%s", queue->name, mem->interface);
	queue_member_follower_removal(queue, mem);
	ao2_unlink(queue->members, mem);
	ao2_unlock(queue->members);
}

/*!
 * \brief Find rt member record to update otherwise create one.
 *
 * Search for member in queue, if found update penalty/paused state,
 * if no member exists create one flag it as a RT member and add to queue member list.
*/
static void rt_handle_member_record(struct call_queue *q, char *interface, struct ast_config *member_config)
{
	struct member *m;
	struct ao2_iterator mem_iter;
	int penalty = 0;
	int paused  = 0;
	int found = 0;
	int ringinuse = q->ringinuse;

	const char *config_val;
	const char *rt_uniqueid = ast_variable_retrieve(member_config, interface, "uniqueid");
	const char *membername = S_OR(ast_variable_retrieve(member_config, interface, "membername"), interface);
	const char *state_interface = S_OR(ast_variable_retrieve(member_config, interface, "state_interface"), interface);
	const char *penalty_str = ast_variable_retrieve(member_config, interface, "penalty");
	const char *paused_str = ast_variable_retrieve(member_config, interface, "paused");

	if (ast_strlen_zero(rt_uniqueid)) {
		ast_log(LOG_WARNING, "Realtime field uniqueid is empty for member %s\n", S_OR(membername, "NULL"));
		return;
	}

	if (penalty_str) {
		penalty = atoi(penalty_str);
		if ((penalty < 0) && negative_penalty_invalid) {
			return;
		} else if (penalty < 0) {
			penalty = 0;
		}
	}

	if (paused_str) {
		paused = atoi(paused_str);
		if (paused < 0) {
			paused = 0;
		}
	}

	if ((config_val = ast_variable_retrieve(member_config, interface, realtime_ringinuse_field))) {
		if (ast_true(config_val)) {
			ringinuse = 1;
		} else if (ast_false(config_val)) {
			ringinuse = 0;
		} else {
			ast_log(LOG_WARNING, "Invalid value of '%s' field for %s in queue '%s'\n", realtime_ringinuse_field, interface, q->name);
		}
	}

	/* Find member by realtime uniqueid and update */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (!strcasecmp(m->rt_uniqueid, rt_uniqueid)) {
			m->dead = 0;	/* Do not delete this one. */
			ast_copy_string(m->rt_uniqueid, rt_uniqueid, sizeof(m->rt_uniqueid));
			if (paused_str) {
				m->paused = paused;
				ast_devstate_changed(m->paused ? QUEUE_PAUSED_DEVSTATE : QUEUE_UNPAUSED_DEVSTATE,
					AST_DEVSTATE_CACHABLE, "Queue:%s_pause_%s", q->name, m->interface);
			}
			if (strcasecmp(state_interface, m->state_interface)) {
				ast_copy_string(m->state_interface, state_interface, sizeof(m->state_interface));
			}
			m->penalty = penalty;
			m->ringinuse = ringinuse;
			found = 1;
			ao2_ref(m, -1);
			break;
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	/* Create a new member */
	if (!found) {
		if ((m = create_queue_member(interface, membername, penalty, paused, state_interface, ringinuse))) {
			m->dead = 0;
			m->realtime = 1;
			ast_copy_string(m->rt_uniqueid, rt_uniqueid, sizeof(m->rt_uniqueid));
			if (!log_membername_as_agent) {
				ast_queue_log(q->name, "REALTIME", m->interface, "ADDMEMBER", "%s", paused ? "PAUSED" : "");
			} else {
				ast_queue_log(q->name, "REALTIME", m->membername, "ADDMEMBER", "%s", paused ? "PAUSED" : "");
			}
			member_add_to_queue(q, m);
			ao2_ref(m, -1);
			m = NULL;
		}
	}
}

/*! \brief Iterate through queue's member list and delete them */
static void free_members(struct call_queue *q, int all)
{
	/* Free non-dynamic members */
	struct member *cur;
	struct ao2_iterator mem_iter = ao2_iterator_init(q->members, 0);

	while ((cur = ao2_iterator_next(&mem_iter))) {
		if (all || !cur->dynamic) {
			member_remove_from_queue(q, cur);
		}
		ao2_ref(cur, -1);
	}
	ao2_iterator_destroy(&mem_iter);
}

/*! \brief Free queue's member list then its string fields */
static void destroy_queue(void *obj)
{
	struct call_queue *q = obj;
	int i;

	free_members(q, 1);
	ast_string_field_free_memory(q);
	for (i = 0; i < MAX_PERIODIC_ANNOUNCEMENTS; i++) {
		if (q->sound_periodicannounce[i]) {
			ast_free(q->sound_periodicannounce[i]);
		}
	}
	ao2_ref(q->members, -1);
}

static struct call_queue *alloc_queue(const char *queuename)
{
	struct call_queue *q;

	if ((q = ao2_t_alloc(sizeof(*q), destroy_queue, "Allocate queue"))) {
		if (ast_string_field_init(q, 64)) {
			queue_t_unref(q, "String field allocation failed");
			return NULL;
		}
		ast_string_field_set(q, name, queuename);
	}
	return q;
}

/*!
 * \brief Reload a single queue via realtime.
 *
 * Check for statically defined queue first, check if deleted RT queue,
 * check for new RT queue, if queue vars are not defined init them with defaults.
 * reload RT queue vars, set RT queue members dead and reload them, return finished queue.
 * \retval the queue,
 * \retval NULL if it doesn't exist.
 * \note Should be called with the "queues" container locked.
*/
static struct call_queue *find_queue_by_name_rt(const char *queuename, struct ast_variable *queue_vars, struct ast_config *member_config)
{
	struct ast_variable *v;
	struct call_queue *q, tmpq = {
		.name = queuename,
	};
	struct member *m;
	struct ao2_iterator mem_iter;
	char *interface = NULL;
	const char *tmp_name;
	char *tmp;
	char tmpbuf[64];	/* Must be longer than the longest queue param name. */

	/* Static queues override realtime. */
	if ((q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Check if static queue exists"))) {
		ao2_lock(q);
		if (!q->realtime) {
			if (q->dead) {
				ao2_unlock(q);
				queue_t_unref(q, "Queue is dead; can't return it");
				return NULL;
			}
			ast_log(LOG_WARNING, "Static queue '%s' already exists. Not loading from realtime\n", q->name);
			ao2_unlock(q);
			return q;
		}
	} else if (!member_config) {
		/* Not found in the list, and it's not realtime ... */
		return NULL;
	}
	/* Check if queue is defined in realtime. */
	if (!queue_vars) {
		/* Delete queue from in-core list if it has been deleted in realtime. */
		if (q) {
			/*! \note Hmm, can't seem to distinguish a DB failure from a not
			   found condition... So we might delete an in-core queue
			   in case of DB failure. */
			ast_debug(1, "Queue %s not found in realtime.\n", queuename);

			q->dead = 1;
			/* Delete if unused (else will be deleted when last caller leaves). */
			queues_t_unlink(queues, q, "Unused; removing from container");
			ao2_unlock(q);
			queue_t_unref(q, "Queue is dead; can't return it");
		}
		return NULL;
	}

	/* Create a new queue if an in-core entry does not exist yet. */
	if (!q) {
		struct ast_variable *tmpvar = NULL;
		if (!(q = alloc_queue(queuename))) {
			return NULL;
		}
		ao2_lock(q);
		clear_queue(q);
		q->realtime = 1;
		/*Before we initialize the queue, we need to set the strategy, so that linear strategy
		 * will allocate the members properly
		 */
		for (tmpvar = queue_vars; tmpvar; tmpvar = tmpvar->next) {
			if (!strcasecmp(tmpvar->name, "strategy")) {
				q->strategy = strat2int(tmpvar->value);
				if (q->strategy < 0) {
					ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
					tmpvar->value, q->name);
					q->strategy = QUEUE_STRATEGY_RINGALL;
				}
				break;
			}
		}
		/* We traversed all variables and didn't find a strategy */
		if (!tmpvar) {
			q->strategy = QUEUE_STRATEGY_RINGALL;
		}
		queues_t_link(queues, q, "Add queue to container");
	}
	init_queue(q);		/* Ensure defaults for all parameters not set explicitly. */

	memset(tmpbuf, 0, sizeof(tmpbuf));
	for (v = queue_vars; v; v = v->next) {
		/* Convert to dashes `-' from underscores `_' as the latter are more SQL friendly. */
		if (strchr(v->name, '_')) {
			ast_copy_string(tmpbuf, v->name, sizeof(tmpbuf));
			tmp_name = tmpbuf;
			tmp = tmpbuf;
			while ((tmp = strchr(tmp, '_'))) {
				*tmp++ = '-';
			}
		} else {
			tmp_name = v->name;
		}

		/* NULL values don't get returned from realtime; blank values should
		 * still get set.  If someone doesn't want a value to be set, they
		 * should set the realtime column to NULL, not blank. */
		queue_set_param(q, tmp_name, v->value, -1, 0);
	}

	/* Temporarily set realtime members dead so we can detect deleted ones. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->realtime) {
			m->dead = 1;
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	while ((interface = ast_category_browse(member_config, interface))) {
		rt_handle_member_record(q, interface, member_config);
	}

	/* Delete all realtime members that have been deleted in DB. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->dead) {
			if (ast_strlen_zero(m->membername) || !log_membername_as_agent) {
				ast_queue_log(q->name, "REALTIME", m->interface, "REMOVEMEMBER", "%s", "");
			} else {
				ast_queue_log(q->name, "REALTIME", m->membername, "REMOVEMEMBER", "%s", "");
			}
			member_remove_from_queue(q, m);
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	ao2_unlock(q);

	return q;
}

/*!
 * note  */

/*!
 * \internal
 * \brief Returns reference to the named queue. If the queue is realtime, it will load the queue as well.
 * \param queuename - name of the desired queue
 *
 * \retval the queue
 * \retval NULL if it doesn't exist
 */
static struct call_queue *find_load_queue_rt_friendly(const char *queuename)
{
	struct ast_variable *queue_vars;
	struct ast_config *member_config = NULL;
	struct call_queue *q = NULL, tmpq = {
		.name = queuename,
	};
	int prev_weight = 0;

	/* Find the queue in the in-core list first. */
	q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Look for queue in memory first");

	if (!q || q->realtime) {
		/*! \note Load from realtime before taking the "queues" container lock, to avoid blocking all
		   queue operations while waiting for the DB.

		   This will be two separate database transactions, so we might
		   see queue parameters as they were before another process
		   changed the queue and member list as it was after the change.
		   Thus we might see an empty member list when a queue is
		   deleted. In practise, this is unlikely to cause a problem. */

		queue_vars = ast_load_realtime("queues", "name", queuename, SENTINEL);
		if (queue_vars) {
			member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", queuename, SENTINEL);
			if (!member_config) {
				ast_debug(1, "No queue_members defined in config extconfig.conf\n");
				member_config = ast_config_new();
			}
		}
		if (q) {
			prev_weight = q->weight ? 1 : 0;
			queue_t_unref(q, "Need to find realtime queue");
		}

		q = find_queue_by_name_rt(queuename, queue_vars, member_config);
		ast_config_destroy(member_config);
		ast_variables_destroy(queue_vars);

		/* update the use_weight value if the queue's has gained or lost a weight */
		if (q) {
			if (!q->weight && prev_weight) {
				ast_atomic_fetchadd_int(&use_weight, -1);
			}
			if (q->weight && !prev_weight) {
				ast_atomic_fetchadd_int(&use_weight, +1);
			}
		}
		/* Other cases will end up with the proper value for use_weight */
	} else {
		update_realtime_members(q);
	}
	return q;
}

static int update_realtime_member_field(struct member *mem, const char *queue_name, const char *field, const char *value)
{
	int ret = -1;

	if (ast_strlen_zero(mem->rt_uniqueid)) {
 		return ret;
	}

	if ((ast_update_realtime("queue_members", "uniqueid", mem->rt_uniqueid, field, value, SENTINEL)) > 0) {
		ret = 0;
	}

	return ret;
}


static void update_realtime_members(struct call_queue *q)
{
	struct ast_config *member_config = NULL;
	struct member *m;
	char *interface = NULL;
	struct ao2_iterator mem_iter;

	if (!(member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", q->name , SENTINEL))) {
		/* This queue doesn't have realtime members. If the queue still has any realtime
		 * members in memory, they need to be removed.
		 */
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			if (m->realtime) {
				member_remove_from_queue(q, m);
			}
			ao2_ref(m, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ast_debug(3, "Queue %s has no realtime members defined. No need for update\n", q->name);
		ao2_unlock(q);
		return;
	}

	ao2_lock(q);

	/* Temporarily set realtime  members dead so we can detect deleted ones.*/
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->realtime) {
			m->dead = 1;
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	while ((interface = ast_category_browse(member_config, interface))) {
		rt_handle_member_record(q, interface, member_config);
	}

	/* Delete all realtime members that have been deleted in DB. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->dead) {
			if (ast_strlen_zero(m->membername) || !log_membername_as_agent) {
				ast_queue_log(q->name, "REALTIME", m->interface, "REMOVEMEMBER", "%s", "");
			} else {
				ast_queue_log(q->name, "REALTIME", m->membername, "REMOVEMEMBER", "%s", "");
			}
			member_remove_from_queue(q, m);
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);
	ao2_unlock(q);
	ast_config_destroy(member_config);
}

static int join_queue(char *queuename, struct queue_ent *qe, enum queue_result *reason, int position)
{
	struct call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;

	if (!(q = find_load_queue_rt_friendly(queuename))) {
		return res;
	}
	ao2_lock(q);

	/* This is our one */
	if (q->joinempty) {
		int status = 0;
		if ((status = get_member_status(q, qe->max_penalty, qe->min_penalty, q->joinempty, 0))) {
			*reason = QUEUE_JOINEMPTY;
			ao2_unlock(q);
			queue_t_unref(q, "Done with realtime queue");
			return res;
		}
	}
	if (*reason == QUEUE_UNKNOWN && q->maxlen && (q->count >= q->maxlen)) {
		*reason = QUEUE_FULL;
	} else if (*reason == QUEUE_UNKNOWN) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

		/* There's space for us, put us at the right position inside
		 * the queue.
		 * Take into account the priority of the calling user */
		inserted = 0;
		prev = NULL;
		cur = q->head;
		while (cur) {
			/* We have higher priority than the current user, enter
			 * before him, after all the other users with priority
			 * higher or equal to our priority. */
			if ((!inserted) && (qe->prio > cur->prio)) {
				insert_entry(q, prev, qe, &pos);
				inserted = 1;
			}
			/* <= is necessary for the position comparison because it may not be possible to enter
			 * at our desired position since higher-priority callers may have taken the position we want
			 */
			if (!inserted && (qe->prio >= cur->prio) && position && (position <= pos + 1)) {
				insert_entry(q, prev, qe, &pos);
				inserted = 1;
				/*pos is incremented inside insert_entry, so don't need to add 1 here*/
				if (position < pos) {
					ast_log(LOG_NOTICE, "Asked to be inserted at position %d but forced into position %d due to higher priority callers\n", position, pos);
				}
			}
			cur->pos = ++pos;
			prev = cur;
			cur = cur->next;
		}
		/* No luck, join at the end of the queue */
		if (!inserted) {
			insert_entry(q, prev, qe, &pos);
		}
		ast_copy_string(qe->moh, q->moh, sizeof(qe->moh));
		ast_copy_string(qe->announce, q->announce, sizeof(qe->announce));
		ast_copy_string(qe->context, q->context, sizeof(qe->context));
		q->count++;
		if (q->count == 1) {
			ast_devstate_changed(AST_DEVICE_RINGING, AST_DEVSTATE_CACHABLE, "Queue:%s", q->name);
		}

		res = 0;

		blob = ast_json_pack("{s: s, s: i, s: i}",
				     "Queue", q->name,
				     "Position", qe->pos,
				     "Count", q->count);
		ast_channel_publish_cached_blob(qe->chan, queue_caller_join_type(), blob);
		ast_debug(1, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, ast_channel_name(qe->chan), qe->pos );
	}
	ao2_unlock(q);
	queue_t_unref(q, "Done with realtime queue");

	return res;
}

static int play_file(struct ast_channel *chan, const char *filename)
{
	int res;

	if (ast_strlen_zero(filename)) {
		return 0;
	}

	if (!ast_fileexists(filename, NULL, ast_channel_language(chan))) {
		return 0;
	}

	ast_stopstream(chan);

	res = ast_streamfile(chan, filename, ast_channel_language(chan));
	if (!res) {
		res = ast_waitstream(chan, AST_DIGIT_ANY);
	}

	ast_stopstream(chan);

	return res;
}

/*!
 * \brief Check for valid exit from queue via goto
 * \retval 0 if failure
 * \retval 1 if successful
*/
static int valid_exit(struct queue_ent *qe, char digit)
{
	int digitlen = strlen(qe->digits);

	/* Prevent possible buffer overflow */
	if (digitlen < sizeof(qe->digits) - 2) {
		qe->digits[digitlen] = digit;
		qe->digits[digitlen + 1] = '\0';
	} else {
		qe->digits[0] = '\0';
		return 0;
	}

	/* If there's no context to goto, short-circuit */
	if (ast_strlen_zero(qe->context)) {
		return 0;
	}

	/* If the extension is bad, then reset the digits to blank */
	if (!ast_canmatch_extension(qe->chan, qe->context, qe->digits, 1,
		S_COR(ast_channel_caller(qe->chan)->id.number.valid, ast_channel_caller(qe->chan)->id.number.str, NULL))) {
		qe->digits[0] = '\0';
		return 0;
	}

	/* We have an exact match */
	if (!ast_goto_if_exists(qe->chan, qe->context, qe->digits, 1)) {
		qe->valid_digits = 1;
		/* Return 1 on a successful goto */
		return 1;
	}

	return 0;
}

static int say_position(struct queue_ent *qe, int ringing)
{
	int res = 0, avgholdmins, avgholdsecs, announceposition = 0;
	int say_thanks = 1;
	time_t now;

	/* Let minannouncefrequency seconds pass between the start of each position announcement */
	time(&now);
	if ((now - qe->last_pos) < qe->parent->minannouncefrequency) {
		return 0;
	}

	/* If either our position has changed, or we are over the freq timer, say position */
	if ((qe->last_pos_said == qe->pos) && ((now - qe->last_pos) < qe->parent->announcefrequency)) {
		return 0;
	}

	if (ringing) {
		ast_indicate(qe->chan,-1);
	} else {
		ast_moh_stop(qe->chan);
	}

	if (qe->parent->announceposition == ANNOUNCEPOSITION_YES ||
		qe->parent->announceposition == ANNOUNCEPOSITION_MORE_THAN ||
		(qe->parent->announceposition == ANNOUNCEPOSITION_LIMIT &&
		qe->pos <= qe->parent->announcepositionlimit)) {
			announceposition = 1;
	}


	if (announceposition == 1) {
		/* Say we're next, if we are */
		if (qe->pos == 1) {
			res = play_file(qe->chan, qe->parent->sound_next);
			if (res) {
				goto playout;
			}
			goto posout;
		} else {
			if (qe->parent->announceposition == ANNOUNCEPOSITION_MORE_THAN && qe->pos > qe->parent->announcepositionlimit){
				/* More than Case*/
				res = play_file(qe->chan, qe->parent->queue_quantity1);
				if (res) {
					goto playout;
				}
				res = ast_say_number(qe->chan, qe->parent->announcepositionlimit, AST_DIGIT_ANY, ast_channel_language(qe->chan), NULL); /* Needs gender */
				if (res) {
					goto playout;
				}
			} else {
				/* Normal Case */
				res = play_file(qe->chan, qe->parent->sound_thereare);
				if (res) {
					goto playout;
				}
				res = ast_say_number(qe->chan, qe->pos, AST_DIGIT_ANY, ast_channel_language(qe->chan), NULL); /* Needs gender */
				if (res) {
					goto playout;
				}
			}
			if (qe->parent->announceposition == ANNOUNCEPOSITION_MORE_THAN && qe->pos > qe->parent->announcepositionlimit){
				/* More than Case*/
				res = play_file(qe->chan, qe->parent->queue_quantity2);
				if (res) {
					goto playout;
				}
			} else {
				res = play_file(qe->chan, qe->parent->sound_calls);
				if (res) {
					goto playout;
				}
			}
		}
	}
	/* Round hold time to nearest minute */
	avgholdmins = abs(((qe->parent->holdtime + 30) - (now - qe->start)) / 60);

	/* If they have specified a rounding then round the seconds as well */
	if (qe->parent->roundingseconds) {
		avgholdsecs = (abs(((qe->parent->holdtime + 30) - (now - qe->start))) - 60 * avgholdmins) / qe->parent->roundingseconds;
		avgholdsecs *= qe->parent->roundingseconds;
	} else {
		avgholdsecs = 0;
	}

	ast_verb(3, "Hold time for %s is %d minute(s) %d seconds\n", qe->parent->name, avgholdmins, avgholdsecs);

	/* If the hold time is >1 min, if it's enabled, and if it's not
	   supposed to be only once and we have already said it, say it */
    if ((avgholdmins+avgholdsecs) > 0 && qe->parent->announceholdtime &&
        ((qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE && !qe->last_pos) ||
        !(qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE))) {
		res = play_file(qe->chan, qe->parent->sound_holdtime);
		if (res) {
			goto playout;
		}

		if (avgholdmins >= 1) {
			res = ast_say_number(qe->chan, avgholdmins, AST_DIGIT_ANY, ast_channel_language(qe->chan), NULL);
			if (res) {
				goto playout;
			}

			if (avgholdmins == 1) {
				res = play_file(qe->chan, qe->parent->sound_minute);
				if (res) {
					goto playout;
				}
			} else {
				res = play_file(qe->chan, qe->parent->sound_minutes);
				if (res) {
					goto playout;
				}
			}
		}
		if (avgholdsecs >= 1) {
			res = ast_say_number(qe->chan, avgholdsecs, AST_DIGIT_ANY, ast_channel_language(qe->chan), NULL);
			if (res) {
				goto playout;
			}

			res = play_file(qe->chan, qe->parent->sound_seconds);
			if (res) {
				goto playout;
			}
		}
	} else if (qe->parent->announceholdtime && !qe->parent->announceposition) {
		say_thanks = 0;
	}

posout:
	if (qe->parent->announceposition) {
		ast_verb(3, "Told %s in %s their queue position (which was %d)\n",
			ast_channel_name(qe->chan), qe->parent->name, qe->pos);
	}
	if (say_thanks) {
		res = play_file(qe->chan, qe->parent->sound_thanks);
	}
playout:

	if ((res > 0 && !valid_exit(qe, res))) {
		res = 0;
	}

	/* Set our last_pos indicators */
	qe->last_pos = now;
	qe->last_pos_said = qe->pos;

	/* Don't restart music on hold if we're about to exit the caller from the queue */
	if (!res) {
		if (ringing) {
			ast_indicate(qe->chan, AST_CONTROL_RINGING);
		} else {
			ast_moh_start(qe->chan, qe->moh, NULL);
		}
	}
	return res;
}

static void recalc_holdtime(struct queue_ent *qe, int newholdtime)
{
	int oldvalue;

	/* Calculate holdtime using an exponential average */
	/* Thanks to SRT for this contribution */
	/* 2^2 (4) is the filter coefficient; a higher exponent would give old entries more weight */

	ao2_lock(qe->parent);
	oldvalue = qe->parent->holdtime;
	qe->parent->holdtime = (((oldvalue << 2) - oldvalue) + newholdtime) >> 2;
	ao2_unlock(qe->parent);
}

/*! \brief Caller leaving queue.
 *
 * Search the queue to find the leaving client, if found remove from queue
 * create manager event, move others up the queue.
*/
static void leave_queue(struct queue_ent *qe)
{
	struct call_queue *q;
	struct queue_ent *current, *prev = NULL;
	struct penalty_rule *pr_iter;
	int pos = 0;

	if (!(q = qe->parent)) {
		return;
	}
	queue_t_ref(q, "Copy queue pointer from queue entry");
	ao2_lock(q);

	prev = NULL;
	for (current = q->head; current; current = current->next) {
		if (current == qe) {
			RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
			char posstr[20];
			q->count--;
			if (!q->count) {
				ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s", q->name);
			}

			blob = ast_json_pack("{s: s, s: i, s: i}",
					     "Queue", q->name,
					     "Position", qe->pos,
					     "Count", q->count);
			ast_channel_publish_cached_blob(qe->chan, queue_caller_leave_type(), blob);
			ast_debug(1, "Queue '%s' Leave, Channel '%s'\n", q->name, ast_channel_name(qe->chan));
			/* Take us out of the queue */
			if (prev) {
				prev->next = current->next;
			} else {
				q->head = current->next;
			}
			/* Free penalty rules */
			while ((pr_iter = AST_LIST_REMOVE_HEAD(&qe->qe_rules, list))) {
				ast_free(pr_iter);
			}
			snprintf(posstr, sizeof(posstr), "%d", qe->pos);
			pbx_builtin_setvar_helper(qe->chan, "QUEUEPOSITION", posstr);
		} else {
			/* Renumber the people after us in the queue based on a new count */
			current->pos = ++pos;
			prev = current;
		}
	}
	ao2_unlock(q);

	/*If the queue is a realtime queue, check to see if it's still defined in real time*/
	if (q->realtime) {
		struct ast_variable *var;
		if (!(var = ast_load_realtime("queues", "name", q->name, SENTINEL))) {
			q->dead = 1;
		} else {
			ast_variables_destroy(var);
		}
	}

	if (q->dead) {
		/* It's dead and nobody is in it, so kill it */
		queues_t_unlink(queues, q, "Queue is now dead; remove it from the container");
	}
	/* unref the explicit ref earlier in the function */
	queue_t_unref(q, "Expire copied reference");
}

/*!
 * \internal
 * \brief Destroy the given callattempt structure and free it.
 * \since 1.8
 *
 * \param doomed callattempt structure to destroy.
 *
 * \return Nothing
 */
static void callattempt_free(struct callattempt *doomed)
{
	if (doomed->member) {
		ao2_ref(doomed->member, -1);
	}
	ast_party_connected_line_free(&doomed->connected);
	ast_free(doomed);
}

static void publish_dial_end_event(struct ast_channel *in, struct callattempt *outgoing, struct ast_channel *exception, const char *status)
{
	struct callattempt *cur;

	for (cur = outgoing; cur; cur = cur->q_next) {
		if (cur->chan && cur->chan != exception) {
			ast_channel_publish_dial(in, cur->chan, NULL, status);
		}
	}
}

/*! \brief Hang up a list of outgoing calls */
static void hangupcalls(struct queue_ent *qe, struct callattempt *outgoing, struct ast_channel *exception, int cancel_answered_elsewhere)
{
	struct callattempt *oo;

	while (outgoing) {
		/* If someone else answered the call we should indicate this in the CANCEL */
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception)) {
			if (exception || cancel_answered_elsewhere) {
				ast_channel_hangupcause_set(outgoing->chan, AST_CAUSE_ANSWERED_ELSEWHERE);
			}
			ast_channel_publish_dial(qe->chan, outgoing->chan, outgoing->interface, "CANCEL");
			ast_hangup(outgoing->chan);
		}
		oo = outgoing;
		outgoing = outgoing->q_next;
		ast_aoc_destroy_decoded(oo->aoc_s_rate_list);
		callattempt_free(oo);
	}
}

/*!
 * \brief Get the number of members available to accept a call.
 *
 * \note The queue passed in should be locked prior to this function call
 *
 * \param[in] q The queue for which we are couting the number of available members
 * \return Return the number of available members in queue q
 */
static int num_available_members(struct call_queue *q)
{
	struct member *mem;
	int avl = 0;
	struct ao2_iterator mem_iter;

	mem_iter = ao2_iterator_init(q->members, 0);
	while ((mem = ao2_iterator_next(&mem_iter))) {

		avl += is_member_available(q, mem);
		ao2_ref(mem, -1);

		/* If autofill is not enabled or if the queue's strategy is ringall, then
		 * we really don't care about the number of available members so much as we
		 * do that there is at least one available.
		 *
		 * In fact, we purposely will return from this function stating that only
		 * one member is available if either of those conditions hold. That way,
		 * functions which determine what action to take based on the number of available
		 * members will operate properly. The reasoning is that even if multiple
		 * members are available, only the head caller can actually be serviced.
		 */
		if ((!q->autofill || q->strategy == QUEUE_STRATEGY_RINGALL) && avl) {
			break;
		}
	}
	ao2_iterator_destroy(&mem_iter);

	return avl;
}

/* traverse all defined queues which have calls waiting and contain this member
   return 0 if no other queue has precedence (higher weight) or 1 if found  */
static int compare_weight(struct call_queue *rq, struct member *member)
{
	struct call_queue *q;
	struct member *mem;
	int found = 0;
	struct ao2_iterator queue_iter;

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		if (q == rq) { /* don't check myself, could deadlock */
			queue_t_unref(q, "Done with iterator");
			continue;
		}
		ao2_lock(q);
		if (q->count && q->members) {
			if ((mem = ao2_find(q->members, member, OBJ_POINTER))) {
				ast_debug(1, "Found matching member %s in queue '%s'\n", mem->interface, q->name);
				if (q->weight > rq->weight && q->count >= num_available_members(q)) {
					ast_debug(1, "Queue '%s' (weight %d, calls %d) is preferred over '%s' (weight %d, calls %d)\n", q->name, q->weight, q->count, rq->name, rq->weight, rq->count);
					found = 1;
				}
				ao2_ref(mem, -1);
			}
		}
		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
		if (found) {
			break;
		}
	}
	ao2_iterator_destroy(&queue_iter);
	return found;
}

/*! \brief common hangup actions */
static void do_hang(struct callattempt *o)
{
	o->stillgoing = 0;
	ast_hangup(o->chan);
	o->chan = NULL;
}

/*!
 * \internal
 * \brief Check if the member status is available.
 *
 * \param status Member status to check if available.
 *
 * \retval non-zero if the member status is available.
 */
static int member_status_available(int status)
{
	return status == AST_DEVICE_NOT_INUSE || status == AST_DEVICE_UNKNOWN;
}

/*!
 * \internal
 * \brief Clear the member call pending flag.
 *
 * \param mem Queue member.
 *
 * \return Nothing
 */
static void member_call_pending_clear(struct member *mem)
{
	ao2_lock(mem);
	mem->call_pending = 0;
	ao2_unlock(mem);
}

/*!
 * \internal
 * \brief Set the member call pending flag.
 *
 * \param mem Queue member.
 *
 * \retval non-zero if call pending flag was already set.
 */
static int member_call_pending_set(struct member *mem)
{
	int old_pending;

	ao2_lock(mem);
	old_pending = mem->call_pending;
	mem->call_pending = 1;
	ao2_unlock(mem);

	return old_pending;
}

/*!
 * \internal
 * \brief Determine if can ring a queue entry.
 *
 * \param qe Queue entry to check.
 * \param call Member call attempt.
 *
 * \retval non-zero if an entry can be called.
 */
static int can_ring_entry(struct queue_ent *qe, struct callattempt *call)
{
	if (call->member->paused) {
		ast_debug(1, "%s paused, can't receive call\n", call->interface);
		return 0;
	}

	if (!call->member->ringinuse && !member_status_available(call->member->status)) {
		ast_debug(1, "%s not available, can't receive call\n", call->interface);
		return 0;
	}

	if ((call->lastqueue && call->lastqueue->wrapuptime && (time(NULL) - call->lastcall < call->lastqueue->wrapuptime))
		|| (!call->lastqueue && qe->parent->wrapuptime && (time(NULL) - call->lastcall < qe->parent->wrapuptime))) {
		ast_debug(1, "Wrapuptime not yet expired on queue %s for %s\n",
			(call->lastqueue ? call->lastqueue->name : qe->parent->name),
			call->interface);
		return 0;
	}

	if (use_weight && compare_weight(qe->parent, call->member)) {
		ast_debug(1, "Priority queue delaying call to %s:%s\n",
			qe->parent->name, call->interface);
		return 0;
	}

	if (!call->member->ringinuse) {
		if (member_call_pending_set(call->member)) {
			ast_debug(1, "%s has another call pending, can't receive call\n",
				call->interface);
			return 0;
		}

		/*
		 * The queue member is available.  Get current status to be sure
		 * because the device state and extension state callbacks may
		 * not have updated the status yet.
		 */
		if (!member_status_available(get_queue_member_status(call->member))) {
			ast_debug(1, "%s actually not available, can't receive call\n",
				call->interface);
			member_call_pending_clear(call->member);
			return 0;
		}
	}

	return 1;
}

/*!
 * \brief Part 2 of ring_one
 *
 * Does error checking before attempting to request a channel and call a member.
 * This function is only called from ring_one().
 * Failure can occur if:
 * - Agent on call
 * - Agent is paused
 * - Wrapup time not expired
 * - Priority by another queue
 *
 * \retval 1 on success to reach a free agent
 * \retval 0 on failure to get agent.
 */
static int ring_entry(struct queue_ent *qe, struct callattempt *tmp, int *busies)
{
	int res;
	int status;
	char tech[256];
	char *location;
	const char *macrocontext, *macroexten;
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	/* on entry here, we know that tmp->chan == NULL */
	if (!can_ring_entry(qe, tmp)) {
		tmp->stillgoing = 0;
		++*busies;
		return 0;
	}
	ast_assert(tmp->member->ringinuse || tmp->member->call_pending);

	ast_copy_string(tech, tmp->interface, sizeof(tech));
	if ((location = strchr(tech, '/'))) {
		*location++ = '\0';
	} else {
		location = "";
	}

	/* Request the peer */
	tmp->chan = ast_request(tech, ast_channel_nativeformats(qe->chan), NULL, qe->chan, location, &status);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
		ao2_lock(qe->parent);
		qe->parent->rrpos++;
		qe->linpos++;
		ao2_unlock(qe->parent);

		member_call_pending_clear(tmp->member);

		publish_dial_end_event(qe->chan, tmp, NULL, "BUSY");
		tmp->stillgoing = 0;
		++*busies;
		return 0;
	}

	ast_channel_lock_both(tmp->chan, qe->chan);

	ast_channel_req_accountcodes_precious(tmp->chan, qe->chan,
		AST_CHANNEL_REQUESTOR_BRIDGE_PEER);
	if (qe->cancel_answered_elsewhere) {
		ast_channel_hangupcause_set(tmp->chan, AST_CAUSE_ANSWERED_ELSEWHERE);
	}
	ast_channel_appl_set(tmp->chan, "AppQueue");
	ast_channel_data_set(tmp->chan, "(Outgoing Line)");
	memset(ast_channel_whentohangup(tmp->chan), 0, sizeof(*ast_channel_whentohangup(tmp->chan)));

	/* If the new channel has no callerid, try to guess what it should be */
	if (!ast_channel_caller(tmp->chan)->id.number.valid) {
		if (ast_channel_connected(qe->chan)->id.number.valid) {
			struct ast_party_caller caller;

			ast_party_caller_set_init(&caller, ast_channel_caller(tmp->chan));
			caller.id = ast_channel_connected(qe->chan)->id;
			caller.ani = ast_channel_connected(qe->chan)->ani;
			ast_channel_set_caller_event(tmp->chan, &caller, NULL);
		} else if (!ast_strlen_zero(ast_channel_dialed(qe->chan)->number.str)) {
			ast_set_callerid(tmp->chan, ast_channel_dialed(qe->chan)->number.str, NULL, NULL);
		} else if (!ast_strlen_zero(S_OR(ast_channel_macroexten(qe->chan), ast_channel_exten(qe->chan)))) {
			ast_set_callerid(tmp->chan, S_OR(ast_channel_macroexten(qe->chan), ast_channel_exten(qe->chan)), NULL, NULL);
		}
		tmp->dial_callerid_absent = 1;
	}

	ast_party_redirecting_copy(ast_channel_redirecting(tmp->chan), ast_channel_redirecting(qe->chan));

	ast_channel_dialed(tmp->chan)->transit_network_select = ast_channel_dialed(qe->chan)->transit_network_select;

	ast_connected_line_copy_from_caller(ast_channel_connected(tmp->chan), ast_channel_caller(qe->chan));

	/* Inherit specially named variables from parent channel */
	ast_channel_inherit_variables(qe->chan, tmp->chan);
	ast_channel_datastore_inherit(qe->chan, tmp->chan);

	/* Presense of ADSI CPE on outgoing channel follows ours */
	ast_channel_adsicpe_set(tmp->chan, ast_channel_adsicpe(qe->chan));

	/* Inherit context and extension */
	macrocontext = pbx_builtin_getvar_helper(qe->chan, "MACRO_CONTEXT");
	ast_channel_dialcontext_set(tmp->chan, ast_strlen_zero(macrocontext) ? ast_channel_context(qe->chan) : macrocontext);
	macroexten = pbx_builtin_getvar_helper(qe->chan, "MACRO_EXTEN");
	if (!ast_strlen_zero(macroexten)) {
		ast_channel_exten_set(tmp->chan, macroexten);
	} else {
		ast_channel_exten_set(tmp->chan, ast_channel_exten(qe->chan));
	}

	ast_channel_unlock(tmp->chan);
	ast_channel_unlock(qe->chan);

	/* Place the call, but don't wait on the answer */
	if ((res = ast_call(tmp->chan, location, 0))) {
		/* Again, keep going even if there's an error */
		ast_verb(3, "Couldn't call %s\n", tmp->interface);
		do_hang(tmp);
		member_call_pending_clear(tmp->member);
		++*busies;
		return 0;
	}

	ast_channel_lock_both(tmp->chan, qe->chan);

	blob = ast_json_pack("{s: s, s: s, s: s}",
			     "Queue", qe->parent->name,
			     "Interface", tmp->interface,
			     "MemberName", tmp->member->membername);
	queue_publish_multi_channel_blob(qe->chan, tmp->chan, queue_agent_called_type(), blob);

	ast_channel_publish_dial(qe->chan, tmp->chan, tmp->interface, NULL);

	ast_channel_unlock(tmp->chan);
	ast_channel_unlock(qe->chan);

	ast_verb(3, "Called %s\n", tmp->interface);

	member_call_pending_clear(tmp->member);
	return 1;
}

/*! \brief find the entry with the best metric, or NULL */
static struct callattempt *find_best(struct callattempt *outgoing)
{
	struct callattempt *best = NULL, *cur;

	for (cur = outgoing; cur; cur = cur->q_next) {
		if (cur->stillgoing &&					/* Not already done */
			!cur->chan &&					/* Isn't already going */
			(!best || cur->metric < best->metric)) {		/* We haven't found one yet, or it's better */
			best = cur;
		}
	}

	return best;
}

/*!
 * \brief Place a call to a queue member.
 *
 * Once metrics have been calculated for each member, this function is used
 * to place a call to the appropriate member (or members). The low-level
 * channel-handling and error detection is handled in ring_entry
 *
 * \retval 1 if a member was called successfully
 * \retval 0 otherwise
 */
static int ring_one(struct queue_ent *qe, struct callattempt *outgoing, int *busies)
{
	int ret = 0;

	while (ret == 0) {
		struct callattempt *best = find_best(outgoing);
		if (!best) {
			ast_debug(1, "Nobody left to try ringing in queue\n");
			break;
		}
		if (qe->parent->strategy == QUEUE_STRATEGY_RINGALL) {
			struct callattempt *cur;
			/* Ring everyone who shares this best metric (for ringall) */
			for (cur = outgoing; cur; cur = cur->q_next) {
				if (cur->stillgoing && !cur->chan && cur->metric <= best->metric) {
					ast_debug(1, "(Parallel) Trying '%s' with metric %d\n", cur->interface, cur->metric);
					ret |= ring_entry(qe, cur, busies);
				}
			}
		} else {
			/* Ring just the best channel */
			ast_debug(1, "Trying '%s' with metric %d\n", best->interface, best->metric);
			ret = ring_entry(qe, best, busies);
		}

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			ast_debug(1, "Queue timed out while ringing members.\n");
			ret = 0;
			break;
		}
	}

	return ret;
}

/*! \brief Search for best metric and add to Round Robbin queue */
static int store_next_rr(struct queue_ent *qe, struct callattempt *outgoing)
{
	struct callattempt *best = find_best(outgoing);

	if (best) {
		/* Ring just the best channel */
		ast_debug(1, "Next is '%s' with metric %d\n", best->interface, best->metric);
		qe->parent->rrpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (qe->parent->wrapped) {
			/* No more channels, start over */
			qe->parent->rrpos = 0;
		} else {
			/* Prioritize next entry */
			qe->parent->rrpos++;
		}
	}
	qe->parent->wrapped = 0;

	return 0;
}

/*! \brief Search for best metric and add to Linear queue */
static int store_next_lin(struct queue_ent *qe, struct callattempt *outgoing)
{
	struct callattempt *best = find_best(outgoing);

	if (best) {
		/* Ring just the best channel */
		ast_debug(1, "Next is '%s' with metric %d\n", best->interface, best->metric);
		qe->linpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (qe->linwrapped) {
			/* No more channels, start over */
			qe->linpos = 0;
		} else {
			/* Prioritize next entry */
			qe->linpos++;
		}
	}
	qe->linwrapped = 0;

	return 0;
}

/*! \brief Playback announcement to queued members if period has elapsed */
static int say_periodic_announcement(struct queue_ent *qe, int ringing)
{
	int res = 0;
	time_t now;

	/* Get the current time */
	time(&now);

	/* Check to see if it is time to announce */
	if ((now - qe->last_periodic_announce_time) < qe->parent->periodicannouncefrequency) {
		return 0;
	}

	/* Stop the music on hold so we can play our own file */
	if (ringing) {
		ast_indicate(qe->chan,-1);
	} else {
		ast_moh_stop(qe->chan);
	}

	ast_verb(3, "Playing periodic announcement\n");

	if (qe->parent->randomperiodicannounce && qe->parent->numperiodicannounce) {
		qe->last_periodic_announce_sound = ((unsigned long) ast_random()) % qe->parent->numperiodicannounce;
	} else if (qe->last_periodic_announce_sound >= qe->parent->numperiodicannounce ||
		ast_str_strlen(qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound]) == 0) {
		qe->last_periodic_announce_sound = 0;
	}

	/* play the announcement */
	res = play_file(qe->chan, ast_str_buffer(qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound]));

	if (res > 0 && !valid_exit(qe, res)) {
		res = 0;
	}

	/* Resume Music on Hold if the caller is going to stay in the queue */
	if (!res) {
		if (ringing) {
			ast_indicate(qe->chan, AST_CONTROL_RINGING);
		} else {
			ast_moh_start(qe->chan, qe->moh, NULL);
		}
	}

	/* update last_periodic_announce_time */
	if (qe->parent->relativeperiodicannounce) {
		time(&qe->last_periodic_announce_time);
	} else {
		qe->last_periodic_announce_time = now;
	}

	/* Update the current periodic announcement to the next announcement */
	if (!qe->parent->randomperiodicannounce) {
		qe->last_periodic_announce_sound++;
	}

	return res;
}

/*! \brief Record that a caller gave up on waiting in queue */
static void record_abandoned(struct queue_ent *qe)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	set_queue_variables(qe->parent, qe->chan);
	ao2_lock(qe->parent);
	blob = ast_json_pack("{s: s, s: i, s: i, s: i}",
			     "Queue", qe->parent->name,
			     "Position", qe->pos,
			     "OriginalPosition", qe->opos,
			     "HoldTime", (int)(time(NULL) - qe->start));

	qe->parent->callsabandoned++;
	ao2_unlock(qe->parent);

	ast_channel_publish_cached_blob(qe->chan, queue_caller_abandon_type(), blob);
}

/*! \brief RNA == Ring No Answer. Common code that is executed when we try a queue member and they don't answer. */
static void rna(int rnatime, struct queue_ent *qe, struct ast_channel *peer, char *interface, char *membername, int autopause)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	ast_verb(3, "Nobody picked up in %d ms\n", rnatime);

	/* Stop ringing, and resume MOH if specified */
	if (qe->ring_when_ringing) {
		ast_indicate(qe->chan, -1);
		ast_moh_start(qe->chan, qe->moh, NULL);
	}

	blob = ast_json_pack("{s: s, s: s, s: s, s: i}",
			     "Queue", qe->parent->name,
			     "Interface", interface,
			     "MemberName", membername,
			     "RingTime", rnatime);
	queue_publish_multi_channel_blob(qe->chan, peer, queue_agent_ringnoanswer_type(), blob);

	ast_queue_log(qe->parent->name, ast_channel_uniqueid(qe->chan), membername, "RINGNOANSWER", "%d", rnatime);
	if (qe->parent->autopause != QUEUE_AUTOPAUSE_OFF && autopause) {
		if (qe->parent->autopausedelay > 0) {
			struct member *mem;
			ao2_lock(qe->parent);
			if ((mem = interface_exists(qe->parent, interface))) {
				time_t idletime = time(&idletime)-mem->lastcall;
				if ((mem->lastcall != 0) && (qe->parent->autopausedelay > idletime)) {
					ao2_unlock(qe->parent);
					ao2_ref(mem, -1);
					return;
				}
				ao2_ref(mem, -1);
			}
			ao2_unlock(qe->parent);
		}
		if (qe->parent->autopause == QUEUE_AUTOPAUSE_ON) {
			if (!set_member_paused(qe->parent->name, interface, "Auto-Pause", 1)) {
				ast_verb(3, "Auto-Pausing Queue Member %s in queue %s since they failed to answer.\n",
					interface, qe->parent->name);
			} else {
				ast_verb(3, "Failed to pause Queue Member %s in queue %s!\n", interface, qe->parent->name);
			}
		} else {
			/* If queue autopause is mode all, just don't send any queue to stop.
			* the function will stop in all queues */
			if (!set_member_paused("", interface, "Auto-Pause", 1)) {
				ast_verb(3, "Auto-Pausing Queue Member %s in all queues since they failed to answer on queue %s.\n",
						interface, qe->parent->name);
			} else {
					ast_verb(3, "Failed to pause Queue Member %s in all queues!\n", interface);
			}
		}
	}
	return;
}

#define AST_MAX_WATCHERS 256
/*!
 * \brief Wait for a member to answer the call
 *
 * \param[in] qe the queue_ent corresponding to the caller in the queue
 * \param[in] outgoing the list of callattempts. Relevant ones will have their chan and stillgoing parameters non-zero
 * \param[in] to the amount of time (in milliseconds) to wait for a response
 * \param[out] digit if a user presses a digit to exit the queue, this is the digit the caller pressed
 * \param[in] prebusies number of busy members calculated prior to calling wait_for_answer
 * \param[in] caller_disconnect if the 'H' option is used when calling Queue(), this is used to detect if the caller pressed * to disconnect the call
 * \param[in] forwardsallowed used to detect if we should allow call forwarding, based on the 'i' option to Queue()
 *
 * \todo eventually all call forward logic should be intergerated into and replaced by ast_call_forward()
 */
static struct callattempt *wait_for_answer(struct queue_ent *qe, struct callattempt *outgoing, int *to, char *digit, int prebusies, int caller_disconnect, int forwardsallowed, int ringing)
{
	const char *queue = qe->parent->name;
	struct callattempt *o, *start = NULL, *prev = NULL;
	int status;
	int numbusies = prebusies;
	int numnochan = 0;
	int stillgoing = 0;
	int orig = *to;
	struct ast_frame *f;
	struct callattempt *peer = NULL;
	struct ast_channel *winner;
	struct ast_channel *in = qe->chan;
	char on[80] = "";
	char membername[80] = "";
	long starttime = 0;
	long endtime = 0;
#ifdef HAVE_EPOLL
	struct callattempt *epollo;
#endif
	struct ast_party_connected_line connected_caller;
	char *inchan_name;
	struct timeval start_time_tv = ast_tvnow();

	ast_party_connected_line_init(&connected_caller);

	ast_channel_lock(qe->chan);
	inchan_name = ast_strdupa(ast_channel_name(qe->chan));
	ast_channel_unlock(qe->chan);

	starttime = (long) time(NULL);
#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->q_next) {
		if (epollo->chan) {
			ast_poll_channel_add(in, epollo->chan);
		}
	}
#endif

	while ((*to = ast_remaining_ms(start_time_tv, orig)) && !peer) {
		int numlines, retry, pos = 1;
		struct ast_channel *watchers[AST_MAX_WATCHERS];
		watchers[0] = in;
		start = NULL;

		for (retry = 0; retry < 2; retry++) {
			numlines = 0;
			for (o = outgoing; o; o = o->q_next) { /* Keep track of important channels */
				if (o->stillgoing) {	/* Keep track of important channels */
					stillgoing = 1;
					if (o->chan) {
						if (pos < AST_MAX_WATCHERS) {
							watchers[pos++] = o->chan;
						}
						if (!start) {
							start = o;
						} else {
							prev->call_next = o;
						}
						prev = o;
					}
				} else if (prev) {
					prev->call_next = NULL;
				}
				numlines++;
			}
			if (pos > 1 /* found */ || !stillgoing /* nobody listening */ ||
				(qe->parent->strategy != QUEUE_STRATEGY_RINGALL) /* ring would not be delivered */) {
				break;
			}
			/* On "ringall" strategy we only move to the next penalty level
			   when *all* ringing phones are done in the current penalty level */
			ring_one(qe, outgoing, &numbusies);
			/* and retry... */
		}
		if (pos == 1 /* not found */) {
			if (numlines == (numbusies + numnochan)) {
				ast_debug(1, "Everyone is busy at this time\n");
			} else {
				ast_debug(3, "No one is answering queue '%s' (%d numlines / %d busies / %d failed channels)\n", queue, numlines, numbusies, numnochan);
			}
			*to = 0;
			return NULL;
		}

		/* Poll for events from both the incoming channel as well as any outgoing channels */
		winner = ast_waitfor_n(watchers, pos, to);

		/* Service all of the outgoing channels */
		for (o = start; o; o = o->call_next) {
			/* We go with a fixed buffer here instead of using ast_strdupa. Using
			 * ast_strdupa in a loop like this one can cause a stack overflow
			 */
			char ochan_name[AST_CHANNEL_NAME];

			if (o->chan) {
				ast_channel_lock(o->chan);
				ast_copy_string(ochan_name, ast_channel_name(o->chan), sizeof(ochan_name));
				ast_channel_unlock(o->chan);
			}
			if (o->stillgoing && (o->chan) &&  (ast_channel_state(o->chan) == AST_STATE_UP)) {
				if (!peer) {
					ast_verb(3, "%s answered %s\n", ochan_name, inchan_name);
					if (!o->block_connected_update) {
						if (o->pending_connected_update) {
							if (ast_channel_connected_line_sub(o->chan, in, &o->connected, 0) &&
								ast_channel_connected_line_macro(o->chan, in, &o->connected, 1, 0)) {
								ast_channel_update_connected_line(in, &o->connected, NULL);
							}
						} else if (!o->dial_callerid_absent) {
							ast_channel_lock(o->chan);
							ast_connected_line_copy_from_caller(&connected_caller, ast_channel_caller(o->chan));
							ast_channel_unlock(o->chan);
							connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
							if (ast_channel_connected_line_sub(o->chan, in, &connected_caller, 0) &&
								ast_channel_connected_line_macro(o->chan, in, &connected_caller, 1, 0)) {
								ast_channel_update_connected_line(in, &connected_caller, NULL);
							}
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
					peer = o;
				}
			} else if (o->chan && (o->chan == winner)) {

				ast_copy_string(on, o->member->interface, sizeof(on));
				ast_copy_string(membername, o->member->membername, sizeof(membername));

				/* Before processing channel, go ahead and check for forwarding */
				if (!ast_strlen_zero(ast_channel_call_forward(o->chan)) && !forwardsallowed) {
					ast_verb(3, "Forwarding %s to '%s' prevented.\n", inchan_name, ast_channel_call_forward(o->chan));
					numnochan++;
					do_hang(o);
					winner = NULL;
					continue;
				} else if (!ast_strlen_zero(ast_channel_call_forward(o->chan))) {
					struct ast_channel *original = o->chan;
					char tmpchan[256];
					char *stuff;
					char *tech;

					ast_copy_string(tmpchan, ast_channel_call_forward(o->chan), sizeof(tmpchan));
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff++ = '\0';
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", ast_channel_call_forward(o->chan), ast_channel_context(o->chan));
						stuff = tmpchan;
						tech = "Local";
					}
					if (!strcasecmp(tech, "Local")) {
						/*
						 * Drop the connected line update block for local channels since
						 * this is going to run dialplan and the user can change his
						 * mind about what connected line information he wants to send.
						 */
						o->block_connected_update = 0;
					}

					ast_verb(3, "Now forwarding %s to '%s/%s' (thanks to %s)\n", inchan_name, tech, stuff, ochan_name);
					/* Setup parameters */
					o->chan = ast_request(tech, ast_channel_nativeformats(in), NULL, in, stuff, &status);
					if (!o->chan) {
						ast_log(LOG_NOTICE,
							"Forwarding failed to create channel to dial '%s/%s'\n",
							tech, stuff);
						o->stillgoing = 0;
						numnochan++;
					} else {
						ast_channel_lock_both(o->chan, original);
						ast_party_redirecting_copy(ast_channel_redirecting(o->chan),
							ast_channel_redirecting(original));
						ast_channel_unlock(o->chan);
						ast_channel_unlock(original);

						ast_channel_lock_both(o->chan, in);
						ast_channel_inherit_variables(in, o->chan);
						ast_channel_datastore_inherit(in, o->chan);

						if (o->pending_connected_update) {
							/*
							 * Re-seed the callattempt's connected line information with
							 * previously acquired connected line info from the queued
							 * channel.  The previously acquired connected line info could
							 * have been set through the CONNECTED_LINE dialplan function.
							 */
							o->pending_connected_update = 0;
							ast_party_connected_line_copy(&o->connected, ast_channel_connected(in));
						}

						ast_channel_req_accountcodes(o->chan, in, AST_CHANNEL_REQUESTOR_BRIDGE_PEER);

						if (!ast_channel_redirecting(o->chan)->from.number.valid
							|| ast_strlen_zero(ast_channel_redirecting(o->chan)->from.number.str)) {
							/*
							 * The call was not previously redirected so it is
							 * now redirected from this number.
							 */
							ast_party_number_free(&ast_channel_redirecting(o->chan)->from.number);
							ast_party_number_init(&ast_channel_redirecting(o->chan)->from.number);
							ast_channel_redirecting(o->chan)->from.number.valid = 1;
							ast_channel_redirecting(o->chan)->from.number.str =
								ast_strdup(S_OR(ast_channel_macroexten(in), ast_channel_exten(in)));
						}

						ast_channel_dialed(o->chan)->transit_network_select = ast_channel_dialed(in)->transit_network_select;

						o->dial_callerid_absent = !ast_channel_caller(o->chan)->id.number.valid
							|| ast_strlen_zero(ast_channel_caller(o->chan)->id.number.str);
						ast_connected_line_copy_from_caller(ast_channel_connected(o->chan),
							ast_channel_caller(in));

						ast_channel_unlock(in);
						if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL
							&& !o->block_connected_update) {
							struct ast_party_redirecting redirecting;

							/*
							 * Redirecting updates to the caller make sense only on single
							 * call at a time strategies.
							 *
							 * We must unlock o->chan before calling
							 * ast_channel_redirecting_macro, because we put o->chan into
							 * autoservice there.  That is pretty much a guaranteed
							 * deadlock.  This is why the handling of o->chan's lock may
							 * seem a bit unusual here.
							 */
							ast_party_redirecting_init(&redirecting);
							ast_party_redirecting_copy(&redirecting, ast_channel_redirecting(o->chan));
							ast_channel_unlock(o->chan);
							if (ast_channel_redirecting_sub(o->chan, in, &redirecting, 0) &&
								ast_channel_redirecting_macro(o->chan, in, &redirecting, 1, 0)) {
								ast_channel_update_redirecting(in, &redirecting, NULL);
							}
							ast_party_redirecting_free(&redirecting);
						} else {
							ast_channel_unlock(o->chan);
						}

						if (ast_call(o->chan, stuff, 0)) {
							ast_log(LOG_NOTICE, "Forwarding failed to dial '%s/%s'\n",
								tech, stuff);
							do_hang(o);
							numnochan++;
						}
					}

					ast_channel_publish_dial(qe->chan, o->chan, stuff, NULL);
					ast_channel_publish_dial_forward(qe->chan, original, o->chan, NULL,
						"CANCEL", ast_channel_call_forward(original));

					/* Hangup the original channel now, in case we needed it */
					ast_hangup(winner);
					continue;
				}
				f = ast_read(winner);
				if (f) {
					if (f->frametype == AST_FRAME_CONTROL) {
						switch (f->subclass.integer) {
						case AST_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								ast_verb(3, "%s answered %s\n", ochan_name, inchan_name);
								ast_channel_publish_dial(qe->chan, o->chan, on, "ANSWER");
								publish_dial_end_event(qe->chan, outgoing, o->chan, "CANCEL");
								if (!o->block_connected_update) {
									if (o->pending_connected_update) {
										if (ast_channel_connected_line_sub(o->chan, in, &o->connected, 0) &&
											ast_channel_connected_line_macro(o->chan, in, &o->connected, 1, 0)) {
											ast_channel_update_connected_line(in, &o->connected, NULL);
										}
									} else if (!o->dial_callerid_absent) {
										ast_channel_lock(o->chan);
										ast_connected_line_copy_from_caller(&connected_caller, ast_channel_caller(o->chan));
										ast_channel_unlock(o->chan);
										connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
										if (ast_channel_connected_line_sub(o->chan, in, &connected_caller, 0) &&
											ast_channel_connected_line_macro(o->chan, in, &connected_caller, 1, 0)) {
											ast_channel_update_connected_line(in, &connected_caller, NULL);
										}
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
								peer = o;
							}
							break;
						case AST_CONTROL_BUSY:
							ast_verb(3, "%s is busy\n", ochan_name);
							ast_channel_publish_dial(qe->chan, o->chan, on, "BUSY");
							endtime = (long) time(NULL);
							endtime -= starttime;
							rna(endtime * 1000, qe, o->chan, on, membername, qe->parent->autopausebusy);
							do_hang(o);
							if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
								if (qe->parent->timeoutrestart) {
									start_time_tv = ast_tvnow();
								}
								/* Have enough time for a queue member to answer? */
								if (ast_remaining_ms(start_time_tv, orig) > 500) {
									ring_one(qe, outgoing, &numbusies);
									starttime = (long) time(NULL);
								}
							}
							numbusies++;
							break;
						case AST_CONTROL_CONGESTION:
							ast_verb(3, "%s is circuit-busy\n", ochan_name);
							ast_channel_publish_dial(qe->chan, o->chan, on, "CONGESTION");
							endtime = (long) time(NULL);
							endtime -= starttime;
							rna(endtime * 1000, qe, o->chan, on, membername, qe->parent->autopauseunavail);
							do_hang(o);
							if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
								if (qe->parent->timeoutrestart) {
									start_time_tv = ast_tvnow();
								}
								if (ast_remaining_ms(start_time_tv, orig) > 500) {
									ring_one(qe, outgoing, &numbusies);
									starttime = (long) time(NULL);
								}
							}
							numbusies++;
							break;
						case AST_CONTROL_RINGING:
							ast_verb(3, "%s is ringing\n", ochan_name);

							/* Start ring indication when the channel is ringing, if specified */
							if (qe->ring_when_ringing) {
								ast_moh_stop(qe->chan);
								ast_indicate(qe->chan, AST_CONTROL_RINGING);
							}
							break;
						case AST_CONTROL_OFFHOOK:
							/* Ignore going off hook */
							break;
						case AST_CONTROL_CONNECTED_LINE:
							if (o->block_connected_update) {
								ast_verb(3, "Connected line update to %s prevented.\n", inchan_name);
								break;
							}
							if (qe->parent->strategy == QUEUE_STRATEGY_RINGALL) {
								struct ast_party_connected_line connected;

								ast_verb(3, "%s connected line has changed. Saving it until answer for %s\n", ochan_name, inchan_name);
								ast_party_connected_line_set_init(&connected, &o->connected);
								ast_connected_line_parse_data(f->data.ptr, f->datalen, &connected);
								ast_party_connected_line_set(&o->connected, &connected, NULL);
								ast_party_connected_line_free(&connected);
								o->pending_connected_update = 1;
								break;
							}

							/*
							 * Prevent using the CallerID from the outgoing channel since we
							 * got a connected line update from it.
							 */
							o->dial_callerid_absent = 1;

							if (ast_channel_connected_line_sub(o->chan, in, f, 1) &&
								ast_channel_connected_line_macro(o->chan, in, f, 1, 1)) {
								ast_indicate_data(in, AST_CONTROL_CONNECTED_LINE, f->data.ptr, f->datalen);
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
							if (qe->parent->strategy == QUEUE_STRATEGY_RINGALL) {
								/*
								 * Redirecting updates to the caller make sense only on single
								 * call at a time strategies.
								 */
								break;
							}
							if (o->block_connected_update) {
								ast_verb(3, "Redirecting update to %s prevented\n",
									inchan_name);
								break;
							}
							ast_verb(3, "%s redirecting info has changed, passing it to %s\n",
								ochan_name, inchan_name);
							if (ast_channel_redirecting_sub(o->chan, in, f, 1) &&
								ast_channel_redirecting_macro(o->chan, in, f, 1, 1)) {
								ast_indicate_data(in, AST_CONTROL_REDIRECTING, f->data.ptr, f->datalen);
							}
							break;
						case AST_CONTROL_PVT_CAUSE_CODE:
							ast_indicate_data(in, AST_CONTROL_PVT_CAUSE_CODE, f->data.ptr, f->datalen);
							break;
						default:
							ast_debug(1, "Dunno what to do with control type %d\n", f->subclass.integer);
							break;
						}
					}
					ast_frfree(f);
				} else { /* ast_read() returned NULL */
					endtime = (long) time(NULL) - starttime;
					ast_channel_publish_dial(qe->chan, o->chan, on, "NOANSWER");
					rna(endtime * 1000, qe, o->chan, on, membername, 1);
					do_hang(o);
					if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
						if (qe->parent->timeoutrestart) {
							start_time_tv = ast_tvnow();
						}
						if (ast_remaining_ms(start_time_tv, orig) > 500) {
							ring_one(qe, outgoing, &numbusies);
							starttime = (long) time(NULL);
						}
					}
				}
			}
		}

		/* If we received an event from the caller, deal with it. */
		if (winner == in) {
			f = ast_read(in);
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to = -1;
				publish_dial_end_event(in, outgoing, NULL, "CANCEL");
				if (f) {
					if (f->data.uint32) {
						ast_channel_hangupcause_set(in, f->data.uint32);
					}
					ast_frfree(f);
				}
				return NULL;
			}

			if ((f->frametype == AST_FRAME_DTMF) && caller_disconnect && (f->subclass.integer == '*')) {
				ast_verb(3, "User hit %c to disconnect call.\n", f->subclass.integer);
				*to = 0;
				publish_dial_end_event(in, outgoing, NULL, "CANCEL");
				ast_frfree(f);
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && valid_exit(qe, f->subclass.integer)) {
				ast_verb(3, "User pressed digit: %c\n", f->subclass.integer);
				*to = 0;
				publish_dial_end_event(in, outgoing, NULL, "CANCEL");
				*digit = f->subclass.integer;
				ast_frfree(f);
				return NULL;
			}

			/* Send the frame from the in channel to all outgoing channels. */
			for (o = start; o; o = o->call_next) {
				if (!o->stillgoing || !o->chan) {
					/* This outgoing channel has died so don't send the frame to it. */
					continue;
				}
				switch (f->frametype) {
				case AST_FRAME_CONTROL:
					switch (f->subclass.integer) {
					case AST_CONTROL_CONNECTED_LINE:
						if (ast_channel_connected_line_sub(in, o->chan, f, 1) &&
							ast_channel_connected_line_macro(in, o->chan, f, 0, 1)) {
							ast_indicate_data(o->chan, f->subclass.integer, f->data.ptr, f->datalen);
						}
						break;
					case AST_CONTROL_REDIRECTING:
						if (ast_channel_redirecting_sub(in, o->chan, f, 1) &&
							ast_channel_redirecting_macro(in, o->chan, f, 0, 1)) {
							ast_indicate_data(o->chan, f->subclass.integer, f->data.ptr, f->datalen);
						}
						break;
					default:
						/* We are not going to do anything with this frame. */
						goto skip_frame;
					}
					break;
				default:
					/* We are not going to do anything with this frame. */
					goto skip_frame;
				}
			}
skip_frame:;

			ast_frfree(f);
		}
	}

	/* Make a position announcement, if enabled */
 	if (qe->parent->announcefrequency && qe->parent->announce_to_first_user) {
		say_position(qe, ringing);
	}

 	/* Make a periodic announcement, if enabled */
 	if (qe->parent->periodicannouncefrequency && qe->parent->announce_to_first_user) {
 		say_periodic_announcement(qe, ringing);
 	}

	if (!*to) {
		for (o = start; o; o = o->call_next) {
			if (o->chan) {
				rna(orig, qe, o->chan, o->interface, o->member->membername, 1);
			}
		}

		publish_dial_end_event(qe->chan, outgoing, NULL, "NOANSWER");
	}

#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->q_next) {
		if (epollo->chan) {
			ast_poll_channel_del(in, epollo->chan);
		}
	}
#endif

	return peer;
}

/*!
 * \brief Check if we should start attempting to call queue members.
 *
 * A simple process, really. Count the number of members who are available
 * to take our call and then see if we are in a position in the queue at
 * which a member could accept our call.
 *
 * \param[in] qe The caller who wants to know if it is his turn
 * \retval 0 It is not our turn
 * \retval 1 It is our turn
 */
static int is_our_turn(struct queue_ent *qe)
{
	struct queue_ent *ch;
	int res;
	int avl;
	int idx = 0;
	/* This needs a lock. How many members are available to be served? */
	ao2_lock(qe->parent);

	avl = num_available_members(qe->parent);

	ch = qe->parent->head;

	ast_debug(1, "There %s %d available %s.\n", avl != 1 ? "are" : "is", avl, avl != 1 ? "members" : "member");

	while ((idx < avl) && (ch) && (ch != qe)) {
		if (!ch->pending) {
			idx++;
		}
		ch = ch->next;
	}

	ao2_unlock(qe->parent);
	/* If the queue entry is within avl [the number of available members] calls from the top ...
	 * Autofill and position check added to support autofill=no (as only calls
	 * from the front of the queue are valid when autofill is disabled)
	 */
	if (ch && idx < avl && (qe->parent->autofill || qe->pos == 1)) {
		ast_debug(1, "It's our turn (%s).\n", ast_channel_name(qe->chan));
		res = 1;
	} else {
		ast_debug(1, "It's not our turn (%s).\n", ast_channel_name(qe->chan));
		res = 0;
	}

	return res;
}

/*!
 * \brief update rules for queues
 *
 * Calculate min/max penalties making sure if relative they stay within bounds.
 * Update queues penalty and set dialplan vars, goto next list entry.
*/
static void update_qe_rule(struct queue_ent *qe)
{
	int max_penalty = INT_MAX;

	if (qe->max_penalty != INT_MAX) {
		char max_penalty_str[20];

		if (qe->pr->max_relative) {
			max_penalty = qe->max_penalty + qe->pr->max_value;
		} else {
			max_penalty = qe->pr->max_value;
		}

		/* a relative change to the penalty could put it below 0 */
		if (max_penalty < 0) {
			max_penalty = 0;
		}

		snprintf(max_penalty_str, sizeof(max_penalty_str), "%d", max_penalty);
		pbx_builtin_setvar_helper(qe->chan, "QUEUE_MAX_PENALTY", max_penalty_str);
		qe->max_penalty = max_penalty;
		ast_debug(3, "Setting max penalty to %d for caller %s since %d seconds have elapsed\n",
			qe->max_penalty, ast_channel_name(qe->chan), qe->pr->time);
	}

	if (qe->min_penalty != INT_MAX) {
		char min_penalty_str[20];
		int min_penalty;

		if (qe->pr->min_relative) {
			min_penalty = qe->min_penalty + qe->pr->min_value;
		} else {
			min_penalty = qe->pr->min_value;
		}

		/* a relative change to the penalty could put it below 0 */
		if (min_penalty < 0) {
			min_penalty = 0;
		}

		if (max_penalty != INT_MAX && min_penalty > max_penalty) {
			min_penalty = max_penalty;
		}

		snprintf(min_penalty_str, sizeof(min_penalty_str), "%d", min_penalty);
		pbx_builtin_setvar_helper(qe->chan, "QUEUE_MIN_PENALTY", min_penalty_str);
		qe->min_penalty = min_penalty;
		ast_debug(3, "Setting min penalty to %d for caller %s since %d seconds have elapsed\n",
			qe->min_penalty, ast_channel_name(qe->chan), qe->pr->time);
	}

	qe->pr = AST_LIST_NEXT(qe->pr, list);
}

/*! \brief The waiting areas for callers who are not actively calling members
 *
 * This function is one large loop. This function will return if a caller
 * either exits the queue or it becomes that caller's turn to attempt calling
 * queue members. Inside the loop, we service the caller with periodic announcements,
 * holdtime announcements, etc. as configured in queues.conf
 *
 * \retval  0 if the caller's turn has arrived
 * \retval -1 if the caller should exit the queue.
 */
static int wait_our_turn(struct queue_ent *qe, int ringing, enum queue_result *reason)
{
	int res = 0;

	/* This is the holding pen for callers 2 through maxlen */
	for (;;) {

		if (is_our_turn(qe)) {
			break;
		}

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}

		if (qe->parent->leavewhenempty) {
			int status = 0;

			if ((status = get_member_status(qe->parent, qe->max_penalty, qe->min_penalty, qe->parent->leavewhenempty, 0))) {
				*reason = QUEUE_LEAVEEMPTY;
				ast_queue_log(qe->parent->name, ast_channel_uniqueid(qe->chan), "NONE", "EXITEMPTY", "%d|%d|%ld", qe->pos, qe->opos, (long) time(NULL) - qe->start);
				leave_queue(qe);
				break;
			}
		}

		/* Make a position announcement, if enabled */
		if (qe->parent->announcefrequency &&
			(res = say_position(qe,ringing))) {
			break;
		}

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}

		/* Make a periodic announcement, if enabled */
		if (qe->parent->periodicannouncefrequency &&
			(res = say_periodic_announcement(qe,ringing)))
			break;

		/* see if we need to move to the next penalty level for this queue */
		while (qe->pr && ((time(NULL) - qe->start) >= qe->pr->time)) {
			update_qe_rule(qe);
		}

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}

		/* Wait a second before checking again */
		if ((res = ast_waitfordigit(qe->chan, RECHECK * 1000))) {
			if (res > 0 && !valid_exit(qe, res)) {
				res = 0;
			} else {
				break;
			}
		}

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}
	}

	return res;
}

/*!
 * \brief update the queue status
 * \retval Always 0
*/
static int update_queue(struct call_queue *q, struct member *member, int callcompletedinsl, int newtalktime)
{
	int oldtalktime;

	struct member *mem;
	struct call_queue *qtmp;
	struct ao2_iterator queue_iter;

	if (shared_lastcall) {
		queue_iter = ao2_iterator_init(queues, 0);
		while ((qtmp = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
			ao2_lock(qtmp);
			if ((mem = ao2_find(qtmp->members, member, OBJ_POINTER))) {
				time(&mem->lastcall);
				mem->calls++;
				mem->lastqueue = q;
				ao2_ref(mem, -1);
			}
			ao2_unlock(qtmp);
			queue_t_unref(qtmp, "Done with iterator");
		}
		ao2_iterator_destroy(&queue_iter);
	} else {
		ao2_lock(q);
		time(&member->lastcall);
		member->calls++;
		member->lastqueue = q;
		ao2_unlock(q);
	}
	ao2_lock(q);
	q->callscompleted++;
	if (callcompletedinsl) {
		q->callscompletedinsl++;
	}
	/* Calculate talktime using the same exponential average as holdtime code*/
	oldtalktime = q->talktime;
	q->talktime = (((oldtalktime << 2) - oldtalktime) + newtalktime) >> 2;
	ao2_unlock(q);
	return 0;
}

/*! \brief Calculate the metric of each member in the outgoing callattempts
 *
 * A numeric metric is given to each member depending on the ring strategy used
 * by the queue. Members with lower metrics will be called before members with
 * higher metrics
 * \retval -1 if penalties are exceeded
 * \retval 0 otherwise
 */
static int calc_metric(struct call_queue *q, struct member *mem, int pos, struct queue_ent *qe, struct callattempt *tmp)
{
	/* disregarding penalty on too few members? */
	int membercount = ao2_container_count(q->members);
	unsigned char usepenalty = (membercount <= q->penaltymemberslimit) ? 0 : 1;

	if (usepenalty) {
		if ((qe->max_penalty != INT_MAX && mem->penalty > qe->max_penalty) ||
			(qe->min_penalty != INT_MAX && mem->penalty < qe->min_penalty)) {
			return -1;
		}
	} else {
		ast_debug(1, "Disregarding penalty, %d members and %d in penaltymemberslimit.\n",
			  membercount, q->penaltymemberslimit);
	}

	switch (q->strategy) {
	case QUEUE_STRATEGY_RINGALL:
		/* Everyone equal, except for penalty */
		tmp->metric = mem->penalty * 1000000 * usepenalty;
		break;
	case QUEUE_STRATEGY_LINEAR:
		if (pos < qe->linpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > qe->linpos) {
				/* Indicate there is another priority */
				qe->linwrapped = 1;
			}
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000 * usepenalty;
		break;
	case QUEUE_STRATEGY_RRORDERED:
	case QUEUE_STRATEGY_RRMEMORY:
		pos = mem->queuepos;
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos) {
				/* Indicate there is another priority */
				q->wrapped = 1;
			}
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000 * usepenalty;
		break;
	case QUEUE_STRATEGY_RANDOM:
		tmp->metric = ast_random() % 1000;
		tmp->metric += mem->penalty * 1000000 * usepenalty;
		break;
	case QUEUE_STRATEGY_WRANDOM:
		tmp->metric = ast_random() % ((1 + mem->penalty) * 1000);
		break;
	case QUEUE_STRATEGY_FEWESTCALLS:
		tmp->metric = mem->calls;
		tmp->metric += mem->penalty * 1000000 * usepenalty;
		break;
	case QUEUE_STRATEGY_LEASTRECENT:
		if (!mem->lastcall) {
			tmp->metric = 0;
		} else {
			tmp->metric = 1000000 - (time(NULL) - mem->lastcall);
		}
		tmp->metric += mem->penalty * 1000000 * usepenalty;
		break;
	default:
		ast_log(LOG_WARNING, "Can't calculate metric for unknown strategy %d\n", q->strategy);
		break;
	}
	return 0;
}

enum agent_complete_reason {
	CALLER,
	AGENT,
	TRANSFER
};

/*! \brief Send out AMI message with member call completion status information */
static void send_agent_complete(const char *queuename, struct ast_channel_snapshot *caller,
	struct ast_channel_snapshot *peer, const struct member *member, time_t holdstart,
	time_t callstart, enum agent_complete_reason rsn)
{
	const char *reason = NULL;	/* silence dumb compilers */
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	switch (rsn) {
	case CALLER:
		reason = "caller";
		break;
	case AGENT:
		reason = "agent";
		break;
	case TRANSFER:
		reason = "transfer";
		break;
	}

	blob = ast_json_pack("{s: s, s: s, s: s, s: i, s: i, s: s}",
			     "Queue", queuename,
			     "Interface", member->interface,
			     "MemberName", member->membername,
			     "HoldTime", (long)(callstart - holdstart),
			     "TalkTime", (long)(time(NULL) - callstart),
			     "Reason", reason);

	queue_publish_multi_channel_snapshot_blob(ast_queue_topic(queuename), caller, peer,
			queue_agent_complete_type(), blob);
}

static void queue_agent_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct ast_channel_blob *agent_blob;

	agent_blob = stasis_message_data(msg);

	if (ast_channel_agent_login_type() == stasis_message_type(msg)) {
		ast_queue_log("NONE", agent_blob->snapshot->uniqueid,
			ast_json_string_get(ast_json_object_get(agent_blob->blob, "agent")),
			"AGENTLOGIN", "%s", agent_blob->snapshot->name);
	} else if (ast_channel_agent_logoff_type() == stasis_message_type(msg)) {
		ast_queue_log("NONE", agent_blob->snapshot->uniqueid,
			ast_json_string_get(ast_json_object_get(agent_blob->blob, "agent")),
			"AGENTLOGOFF", "%s|%ld", agent_blob->snapshot->name,
			(long) ast_json_integer_get(ast_json_object_get(agent_blob->blob, "logintime")));
	}
}

/*!
 * \brief Structure representing relevant data during a local channel optimization
 *
 * The reason we care about local channel optimizations is that we want to be able
 * to accurately report when the caller and queue member have stopped talking to
 * each other. A local channel optimization can cause it to appear that the conversation
 * has stopped immediately after it has begun. By tracking that the relevant channels
 * to monitor have changed due to a local channel optimization, we can give accurate
 * reports.
 *
 * Local channel optimizations for queues are restricted from their normal operation.
 * Bridges created by queues can only be the destination of local channel optimizations,
 * not the source. In addition, move-swap local channel optimizations are the only
 * permitted types of local channel optimization.
 *
 * This data is populated when we are told that a local channel optimization begin
 * is occurring. When we get told the optimization has ended successfully, we then
 * apply the data here into the queue_stasis_data.
 */
struct local_optimization {
	/*! The uniqueid of the channel that will be taking the place of the caller or member */
	const char *source_chan_uniqueid;
	/*! Indication of whether we think there is a local channel optimization in progress */
	int in_progress;
	/*! The identifier for this local channel optimization */
	unsigned int id;
};

/*!
 * \brief User data for stasis subscriptions used for queue calls.
 *
 * app_queue subscribes to channel and bridge events for all bridged calls.
 * app_queue cares about the following events:
 *
 * \li bridge enter: To determine the unique ID of the bridge created for the call.
 * \li blind transfer: To send an appropriate agent complete event.
 * \li attended transfer: To send an appropriate agent complete event.
 * \li local optimization: To update caller and member unique IDs for the call.
 * \li hangup: To send an appropriate agent complete event.
 *
 * The stasis subscriptions last until we determine that the caller and the member
 * are no longer bridged with each other.
 */
struct queue_stasis_data {
	AST_DECLARE_STRING_FIELDS(
		/*! The unique ID of the caller's channel. */
		AST_STRING_FIELD(caller_uniqueid);
		/*! The unique ID of the queue member's channel */
		AST_STRING_FIELD(member_uniqueid);
		/*! The unique ID of the bridge created by the queue */
		AST_STRING_FIELD(bridge_uniqueid);
	);
	/*! The relevant queue */
	struct call_queue *queue;
	/*! The queue member that has answered the call */
	struct member *member;
	/*! The time at which the caller entered the queue. Start of the caller's hold time */
	time_t holdstart;
	/*! The time at which the member answered the call. */
	time_t starttime;
	/*! The original position of the caller when he entered the queue */
	int caller_pos;
	/*! Indication if the call was answered within the configured service level of the queue */
	int callcompletedinsl;
	/*! Indicates if the stasis subscriptions are shutting down */
	int dying;
	/*! The stasis message router for bridge events */
	struct stasis_message_router *bridge_router;
	/*! The stasis message router for channel events */
	struct stasis_message_router *channel_router;
	/*! Local channel optimization details for the caller */
	struct local_optimization caller_optimize;
	/*! Local channel optimization details for the member */
	struct local_optimization member_optimize;
};

/*!
 * \internal
 * \brief Free memory for a queue_stasis_data
 */
static void queue_stasis_data_destructor(void *obj)
{
	struct queue_stasis_data *queue_data = obj;

	/* This can only happen if refcounts for this object have got severely messed up */
	ast_assert(queue_data->bridge_router == NULL);
	ast_assert(queue_data->channel_router == NULL);

	ao2_cleanup(queue_data->member);
	queue_unref(queue_data->queue);
	ast_string_field_free_memory(queue_data);
}

/*!
 * \internal
 * \brief End all stasis subscriptions on a queue_stasis_data
 */
static void remove_stasis_subscriptions(struct queue_stasis_data *queue_data)
{
	SCOPED_AO2LOCK(lock, queue_data);

	queue_data->dying = 1;
	stasis_message_router_unsubscribe(queue_data->bridge_router);
	queue_data->bridge_router = NULL;
	stasis_message_router_unsubscribe(queue_data->channel_router);
	queue_data->channel_router = NULL;
}

/*!
 * \internal
 * \brief Allocate a queue_stasis_data and initialize its data.
 */
static struct queue_stasis_data *queue_stasis_data_alloc(struct queue_ent *qe,
		struct ast_channel *peer, struct member *mem, time_t holdstart,
		time_t starttime, int callcompletedinsl)
{
	struct queue_stasis_data *queue_data;

	queue_data = ao2_alloc(sizeof(*queue_data), queue_stasis_data_destructor);
	if (!queue_data) {
		return NULL;
	}

	if (ast_string_field_init(queue_data, 64)) {
		ao2_cleanup(queue_data);
		return NULL;
	}

	ast_string_field_set(queue_data, caller_uniqueid, ast_channel_uniqueid(qe->chan));
	ast_string_field_set(queue_data, member_uniqueid, ast_channel_uniqueid(peer));
	queue_data->queue = queue_ref(qe->parent);
	queue_data->starttime = starttime;
	queue_data->holdstart = holdstart;
	queue_data->callcompletedinsl = callcompletedinsl;
	queue_data->caller_pos = qe->opos;
	ao2_ref(mem, +1);
	queue_data->member = mem;
	return queue_data;
}

/*!
 * \internal
 * \brief Log an attended transfer in the queue log.
 *
 * Attended transfer queue log messages vary based on the method by which the
 * attended transfer was completed.
 *
 * \param queue_data Data pertaining to the particular call in the queue.
 * \param caller The channel snapshot for the caller channel in the queue.
 * \param atxfer_msg The stasis attended transfer message data.
 */
static void log_attended_transfer(struct queue_stasis_data *queue_data, struct ast_channel_snapshot *caller,
		struct ast_attended_transfer_message *atxfer_msg)
{
	RAII_VAR(struct ast_str *, transfer_str, ast_str_create(32), ast_free);

	if (!transfer_str) {
		ast_log(LOG_WARNING, "Unable to log attended transfer to queue log\n");
		return;
	}

	switch (atxfer_msg->dest_type) {
	case AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE:
		ast_str_set(&transfer_str, 0, "BRIDGE|%s", atxfer_msg->dest.bridge);
		break;
	case AST_ATTENDED_TRANSFER_DEST_APP:
	case AST_ATTENDED_TRANSFER_DEST_LOCAL_APP:
		ast_str_set(&transfer_str, 0, "APP|%s", atxfer_msg->dest.app);
		break;
	case AST_ATTENDED_TRANSFER_DEST_LINK:
		ast_str_set(&transfer_str, 0, "LINK|%s|%s", atxfer_msg->dest.links[0]->name,
				atxfer_msg->dest.links[1]->name);
		break;
	case AST_ATTENDED_TRANSFER_DEST_THREEWAY:
	case AST_ATTENDED_TRANSFER_DEST_FAIL:
		/* Threeways are headed off and should not be logged here */
		ast_assert(0);
		return;
	}

	ast_queue_log(queue_data->queue->name, caller->uniqueid, queue_data->member->membername, "ATTENDEDTRANSFER", "%s|%ld|%ld|%d",
			ast_str_buffer(transfer_str),
			(long) queue_data->starttime - queue_data->holdstart,
			(long) time(NULL) - queue_data->starttime, queue_data->caller_pos);
}

/*!
 * \internal
 * \brief Handle a stasis bridge enter event.
 *
 * We track this particular event in order to learn what bridge
 * was created for the queue call.
 *
 * \param userdata Data pertaining to the particular call in the queue.
 * \param sub The stasis subscription on which the message occurred.
 * \param topic The topic for this event.
 * \param msg The stasis message for the bridge enter event
 */
static void handle_bridge_enter(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct queue_stasis_data *queue_data = userdata;
	struct ast_bridge_blob *enter_blob = stasis_message_data(msg);

	if (queue_data->dying) {
		return;
	}

	if (!ast_strlen_zero(queue_data->bridge_uniqueid)) {
		return;
	}

	if (!strcmp(enter_blob->channel->uniqueid, queue_data->caller_uniqueid)) {
		ast_string_field_set(queue_data, bridge_uniqueid,
				enter_blob->bridge->uniqueid);
		ast_debug(3, "Detected entry of caller channel %s into bridge %s\n",
				enter_blob->channel->name, queue_data->bridge_uniqueid);
	}
}

/*!
 * \brief Handle a blind transfer event
 *
 * This event is important in order to be able to log the end of the
 * call to the queue log and to stasis.
 *
 * \param userdata Data pertaining to the particular call in the queue.
 * \param sub The stasis subscription on which the message occurred.
 * \param topic The topic for this event.
 * \param msg The stasis message for the blind transfer event
 */
static void handle_blind_transfer(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct queue_stasis_data *queue_data = userdata;
	struct ast_blind_transfer_message *transfer_msg = stasis_message_data(msg);
	const char *exten;
	const char *context;
	RAII_VAR(struct ast_channel_snapshot *, caller_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, member_snapshot, NULL, ao2_cleanup);

	if (queue_data->dying) {
		return;
	}

	if (transfer_msg->result != AST_BRIDGE_TRANSFER_SUCCESS) {
		return;
	}

	ao2_lock(queue_data);

	if (ast_strlen_zero(queue_data->bridge_uniqueid) ||
			strcmp(queue_data->bridge_uniqueid, transfer_msg->to_transferee.bridge_snapshot->uniqueid)) {
		ao2_unlock(queue_data);
		return;
	}

	caller_snapshot = ast_channel_snapshot_get_latest(queue_data->caller_uniqueid);
	member_snapshot = ast_channel_snapshot_get_latest(queue_data->member_uniqueid);

	ao2_unlock(queue_data);

	exten = transfer_msg->exten;
	context = transfer_msg->context;

	ast_debug(3, "Detected blind transfer in queue %s\n", queue_data->queue->name);
	ast_queue_log(queue_data->queue->name, caller_snapshot->uniqueid, queue_data->member->membername,
			"BLINDTRANSFER", "%s|%s|%ld|%ld|%d",
			exten, context,
			(long) queue_data->starttime - queue_data->holdstart,
			(long) time(NULL) - queue_data->starttime, queue_data->caller_pos);

	send_agent_complete(queue_data->queue->name, caller_snapshot, member_snapshot, queue_data->member,
			queue_data->holdstart, queue_data->starttime, TRANSFER);
	update_queue(queue_data->queue, queue_data->member, queue_data->callcompletedinsl,
			time(NULL) - queue_data->starttime);
	remove_stasis_subscriptions(queue_data);
}

/*!
 * \brief Handle an attended transfer event
 *
 * This event is important in order to be able to log the end of the
 * call to the queue log and to stasis.
 *
 * \param userdata Data pertaining to the particular call in the queue.
 * \param sub The stasis subscription on which the message occurred.
 * \param topic The topic for this event.
 * \param msg The stasis message for the attended transfer event.
 */
static void handle_attended_transfer(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct queue_stasis_data *queue_data = userdata;
	struct ast_attended_transfer_message *atxfer_msg = stasis_message_data(msg);
	RAII_VAR(struct ast_channel_snapshot *, caller_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, member_snapshot, NULL, ao2_cleanup);

	if (queue_data->dying) {
		return;
	}

	if (atxfer_msg->result != AST_BRIDGE_TRANSFER_SUCCESS ||
			atxfer_msg->dest_type == AST_ATTENDED_TRANSFER_DEST_THREEWAY) {
		return;
	}

	ao2_lock(queue_data);

	if (ast_strlen_zero(queue_data->bridge_uniqueid)) {
		ao2_unlock(queue_data);
		return;
	}

	if ((!atxfer_msg->to_transferee.bridge_snapshot || strcmp(queue_data->bridge_uniqueid,
					atxfer_msg->to_transferee.bridge_snapshot->uniqueid)) &&
			 (!atxfer_msg->to_transfer_target.bridge_snapshot || strcmp(queue_data->bridge_uniqueid,
				 atxfer_msg->to_transfer_target.bridge_snapshot->uniqueid))) {
		ao2_unlock(queue_data);
		return;
	}

	caller_snapshot = ast_channel_snapshot_get_latest(queue_data->caller_uniqueid);
	member_snapshot = ast_channel_snapshot_get_latest(queue_data->member_uniqueid);

	ao2_unlock(queue_data);

	ast_debug(3, "Detected attended transfer in queue %s\n", queue_data->queue->name);

	log_attended_transfer(queue_data, caller_snapshot, atxfer_msg);

	send_agent_complete(queue_data->queue->name, caller_snapshot, member_snapshot, queue_data->member,
			queue_data->holdstart, queue_data->starttime, TRANSFER);
	update_queue(queue_data->queue, queue_data->member, queue_data->callcompletedinsl,
			time(NULL) - queue_data->starttime);
	remove_stasis_subscriptions(queue_data);
}

/*!
 * \internal
 * \brief Callback for all stasis bridge events
 *
 * Based on the event and what bridge it is on, the task is farmed out to relevant
 * subroutines for further processing.
 */
static void queue_bridge_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	if (stasis_subscription_final_message(sub, msg)) {
		ao2_cleanup(userdata);
	}
}

/*!
 * \internal
 * \brief Handler for the beginning of a local channel optimization
 *
 * This method gathers data relevant to the local channel optimization and stores
 * it to be used once the local optimization completes.
 *
 * \param userdata Data pertaining to the particular call in the queue.
 * \param sub The stasis subscription on which the message occurred.
 * \param topic The topic for this event.
 * \param msg The stasis message for the local optimization begin event
 */
static void handle_local_optimization_begin(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct queue_stasis_data *queue_data = userdata;
	struct ast_multi_channel_blob *optimization_blob = stasis_message_data(msg);
	struct ast_channel_snapshot *local_one = ast_multi_channel_blob_get_channel(optimization_blob, "1");
	struct ast_channel_snapshot *local_two = ast_multi_channel_blob_get_channel(optimization_blob, "2");
	struct ast_channel_snapshot *source = ast_multi_channel_blob_get_channel(optimization_blob, "source");
	struct local_optimization *optimization;
	unsigned int id;
	SCOPED_AO2LOCK(lock, queue_data);

	if (queue_data->dying) {
		return;
	}

	if (!strcmp(local_one->uniqueid, queue_data->member_uniqueid)) {
		optimization = &queue_data->member_optimize;
	} else if (!strcmp(local_two->uniqueid, queue_data->caller_uniqueid)) {
		optimization = &queue_data->caller_optimize;
	} else {
		return;
	}

	/* We only allow move-swap optimizations, so there had BETTER be a source */
	ast_assert(source != NULL);

	optimization->source_chan_uniqueid = ast_strdup(source->uniqueid);
	if (!optimization->source_chan_uniqueid) {
		ast_log(LOG_ERROR, "Unable to track local channel optimization for channel %s. Expect further errors\n", local_one->name);
		return;
	}
	id = ast_json_integer_get(ast_json_object_get(ast_multi_channel_blob_get_json(optimization_blob), "id"));

	optimization->id = id;
	optimization->in_progress = 1;
}

/*!
 * \internal
 * \brief Handler for the end of a local channel optimization
 *
 * This method takes the data gathered during the local channel optimization begin
 * event and applies it to the queue stasis data appropriately. This generally involves
 * updating the caller or member unique ID with the channel that is taking the place of
 * the previous caller or member.
 *
 * \param userdata Data pertaining to the particular call in the queue.
 * \param sub The stasis subscription on which the message occurred.
 * \param topic The topic for this event.
 * \param msg The stasis message for the local optimization end event
 */
static void handle_local_optimization_end(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct queue_stasis_data *queue_data = userdata;
	struct ast_multi_channel_blob *optimization_blob = stasis_message_data(msg);
	struct ast_channel_snapshot *local_one = ast_multi_channel_blob_get_channel(optimization_blob, "1");
	struct ast_channel_snapshot *local_two = ast_multi_channel_blob_get_channel(optimization_blob, "2");
	struct local_optimization *optimization;
	int is_caller;
	unsigned int id;
	SCOPED_AO2LOCK(lock, queue_data);

	if (queue_data->dying) {
		return;
	}

	if (!strcmp(local_one->uniqueid, queue_data->member_uniqueid)) {
		optimization = &queue_data->member_optimize;
		is_caller = 0;
	} else if (!strcmp(local_two->uniqueid, queue_data->caller_uniqueid)) {
		optimization = &queue_data->caller_optimize;
		is_caller = 1;
	} else {
		return;
	}

	id = ast_json_integer_get(ast_json_object_get(ast_multi_channel_blob_get_json(optimization_blob), "id"));

	if (!optimization->in_progress) {
		ast_log(LOG_WARNING, "Told of a local optimization end when we had no previous begin\n");
		return;
	}

	if (id != optimization->id) {
		ast_log(LOG_WARNING, "Local optimization end event ID does not match begin (%u != %u)\n",
				id, optimization->id);
		return;
	}

	if (is_caller) {
		ast_debug(3, "Local optimization: Changing queue caller uniqueid from %s to %s\n",
				queue_data->caller_uniqueid, optimization->source_chan_uniqueid);
		ast_string_field_set(queue_data, caller_uniqueid, optimization->source_chan_uniqueid);
	} else {
		ast_debug(3, "Local optimization: Changing queue member uniqueid from %s to %s\n",
				queue_data->member_uniqueid, optimization->source_chan_uniqueid);
		ast_string_field_set(queue_data, member_uniqueid, optimization->source_chan_uniqueid);
	}

	optimization->in_progress = 0;
}

/*!
 * \internal
 * \brief Handler for hangup stasis event
 *
 * This is how we determine that the caller or member has hung up and the call
 * has ended. An appropriate queue log and stasis message are raised in this
 * callback.
 *
 * \param userdata Data pertaining to the particular call in the queue.
 * \param sub The stasis subscription on which the message occurred.
 * \param topic The topic for this event.
 * \param msg The stasis message for the hangup event.
 */
static void handle_hangup(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct queue_stasis_data *queue_data = userdata;
	struct ast_channel_blob *channel_blob = stasis_message_data(msg);
	RAII_VAR(struct ast_channel_snapshot *, caller_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, member_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);
	enum agent_complete_reason reason;

	if (queue_data->dying) {
		return;
	}

	ao2_lock(queue_data);

	if (!strcmp(channel_blob->snapshot->uniqueid, queue_data->caller_uniqueid)) {
		reason = CALLER;
	} else if (!strcmp(channel_blob->snapshot->uniqueid, queue_data->member_uniqueid)) {
		reason = AGENT;
	} else {
		ao2_unlock(queue_data);
		return;
	}

	chan = ast_channel_get_by_name(channel_blob->snapshot->name);
	if (chan && ast_channel_has_role(chan, AST_TRANSFERER_ROLE_NAME)) {
		/* Channel that is hanging up is doing it as part of a transfer.
		 * We'll get a transfer event later
		 */
		ao2_unlock(queue_data);
		return;
	}

	caller_snapshot = ast_channel_snapshot_get_latest(queue_data->caller_uniqueid);
	member_snapshot = ast_channel_snapshot_get_latest(queue_data->member_uniqueid);

	ao2_unlock(queue_data);

	ast_debug(3, "Detected hangup of queue %s channel %s\n", reason == CALLER ? "caller" : "member",
			channel_blob->snapshot->name);

	ast_queue_log(queue_data->queue->name, queue_data->caller_uniqueid, queue_data->member->membername,
			reason == CALLER ? "COMPLETECALLER" : "COMPLETEAGENT", "%ld|%ld|%d",
		(long) (queue_data->starttime - queue_data->holdstart),
		(long) (time(NULL) - queue_data->starttime), queue_data->caller_pos);

	send_agent_complete(queue_data->queue->name, caller_snapshot, member_snapshot, queue_data->member,
			queue_data->holdstart, queue_data->starttime, reason);
	update_queue(queue_data->queue, queue_data->member, queue_data->callcompletedinsl,
			time(NULL) - queue_data->starttime);
	remove_stasis_subscriptions(queue_data);
}

/*!
 * \internal
 * \brief Callback for all stasis channel events
 *
 * Based on the event and the channels involved, the work is farmed out into
 * subroutines for further processing.
 */
static void queue_channel_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	if (stasis_subscription_final_message(sub, msg)) {
		ao2_cleanup(userdata);
	}
}

/*!
 * \internal
 * \brief Create stasis subscriptions for a particular call in the queue.
 *
 * These subscriptions are created once the call has been answered. The subscriptions
 * are put in place so that call progress may be tracked. Once the call can be determined
 * to have ended, then messages are logged to the queue log and stasis events are emitted.
 *
 * \param qe The queue entry representing the caller
 * \param peer The channel that has answered the call
 * \param mem The queue member that answered the call
 * \param holdstart The time at which the caller entered the queue
 * \param starttime The time at which the call was answered
 * \param callcompletedinsl Indicates if the call was answered within the configured service level of the queue.
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int setup_stasis_subs(struct queue_ent *qe, struct ast_channel *peer, struct member *mem,
		time_t holdstart, time_t starttime, int callcompletedinsl)
{
	struct queue_stasis_data *queue_data = queue_stasis_data_alloc(qe, peer, mem, holdstart, starttime, callcompletedinsl);

	if (!queue_data) {
		return -1;
	}

	queue_data->bridge_router = stasis_message_router_create(ast_bridge_topic_all());
	if (!queue_data->bridge_router) {
		ao2_ref(queue_data, -1);
		return -1;
	}

	stasis_message_router_add(queue_data->bridge_router, ast_channel_entered_bridge_type(),
			handle_bridge_enter, queue_data);
	stasis_message_router_add(queue_data->bridge_router, ast_blind_transfer_type(),
			handle_blind_transfer, queue_data);
	stasis_message_router_add(queue_data->bridge_router, ast_attended_transfer_type(),
			handle_attended_transfer, queue_data);
	stasis_message_router_set_default(queue_data->bridge_router,
			queue_bridge_cb, queue_data);

	queue_data->channel_router = stasis_message_router_create(ast_channel_topic_all());
	if (!queue_data->channel_router) {
		/* Unsubscribing from the bridge router will remove the only ref of queue_data,
		 * thus beginning the destruction process
		 */
		stasis_message_router_unsubscribe(queue_data->bridge_router);
		queue_data->bridge_router = NULL;
		return -1;
	}

	ao2_ref(queue_data, +1);
	stasis_message_router_add(queue_data->channel_router, ast_local_optimization_begin_type(),
			handle_local_optimization_begin, queue_data);
	stasis_message_router_add(queue_data->channel_router, ast_local_optimization_end_type(),
			handle_local_optimization_end, queue_data);
	stasis_message_router_add(queue_data->channel_router, ast_channel_hangup_request_type(),
			handle_hangup, queue_data);
	stasis_message_router_set_default(queue_data->channel_router,
			queue_channel_cb, queue_data);

	return 0;
}

struct queue_end_bridge {
	struct call_queue *q;
	struct ast_channel *chan;
};

static void end_bridge_callback_data_fixup(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator)
{
	struct queue_end_bridge *qeb = bconfig->end_bridge_callback_data;
	ao2_ref(qeb, +1);
	qeb->chan = originator;
}

static void end_bridge_callback(void *data)
{
	struct queue_end_bridge *qeb = data;
	struct call_queue *q = qeb->q;
	struct ast_channel *chan = qeb->chan;

	if (ao2_ref(qeb, -1) == 1) {
		set_queue_variables(q, chan);
		/* This unrefs the reference we made in try_calling when we allocated qeb */
		queue_t_unref(q, "Expire bridge_config reference");
	}
}

/*!
 * \internal
 * \brief Setup the after bridge goto location on the peer.
 * \since 12.0.0
 *
 * \param chan Calling channel for bridge.
 * \param peer Peer channel for bridge.
 * \param opts Dialing option flags.
 * \param opt_args Dialing option argument strings.
 *
 * \return Nothing
 */
static void setup_peer_after_bridge_goto(struct ast_channel *chan, struct ast_channel *peer, struct ast_flags *opts, char *opt_args[])
{
	const char *context;
	const char *extension;
	int priority;

	if (ast_test_flag(opts, OPT_CALLEE_GO_ON)) {
		ast_channel_lock(chan);
		context = ast_strdupa(ast_channel_context(chan));
		extension = ast_strdupa(ast_channel_exten(chan));
		priority = ast_channel_priority(chan);
		ast_channel_unlock(chan);
		ast_bridge_set_after_go_on(peer, context, extension, priority,
			opt_args[OPT_ARG_CALLEE_GO_ON]);
	}
}

static void escape_and_substitute(struct ast_channel *chan, const char *input,
		char *output, size_t size)
{
	const char *m = input;
	char escaped[size];
	char *p;

	for (p = escaped; p < escaped + size - 1; p++, m++) {
		switch (*m) {
		case '^':
			if (*(m + 1) == '{') {
				*p = '$';
			}
			break;
		case ',':
			*p++ = '\\';
			/* Fall through */
		default:
			*p = *m;
		}
		if (*m == '\0')
			break;
	}

	if (p == escaped + size) {
		escaped[size - 1] = '\0';
	}

	pbx_substitute_variables_helper(chan, escaped, output, size - 1);
}

static void setup_mixmonitor(struct queue_ent *qe, const char *filename)
{
	char escaped_filename[256];
	char file_with_ext[256];
	char mixmonargs[1512];
	char escaped_monitor_exec[1024];
	const char *monitor_options;
	const char *monitor_exec;

	escaped_monitor_exec[0] = '\0';

	if (filename) {
		escape_and_substitute(qe->chan, filename, escaped_filename, sizeof(escaped_filename));
	} else {
		ast_copy_string(escaped_filename, ast_channel_uniqueid(qe->chan), sizeof(escaped_filename));
	}

	ast_channel_lock(qe->chan);
	if ((monitor_exec = pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC"))) {
		monitor_exec = ast_strdupa(monitor_exec);
	}
	if ((monitor_options = pbx_builtin_getvar_helper(qe->chan, "MONITOR_OPTIONS"))) {
		monitor_options = ast_strdupa(monitor_options);
	} else {
		monitor_options = "";
	}
	ast_channel_unlock(qe->chan);

	if (monitor_exec) {
		escape_and_substitute(qe->chan, monitor_exec, escaped_monitor_exec, sizeof(escaped_monitor_exec));
	}

	snprintf(file_with_ext, sizeof(file_with_ext), "%s.%s", escaped_filename, qe->parent->monfmt);

	if (!ast_strlen_zero(escaped_monitor_exec)) {
		snprintf(mixmonargs, sizeof(mixmonargs), "b%s,%s", monitor_options, escaped_monitor_exec);
	} else {
		snprintf(mixmonargs, sizeof(mixmonargs), "b%s", monitor_options);
	}

	ast_debug(1, "Arguments being passed to MixMonitor: %s,%s\n", file_with_ext, mixmonargs);

	if (ast_start_mixmonitor(qe->chan, file_with_ext, mixmonargs)) {
		ast_log(LOG_WARNING, "Unable to start mixmonitor. Is the MixMonitor app loaded?\n");
	}
}

/*!
 * \internal
 * \brief A large function which calls members, updates statistics, and bridges the caller and a member
 *
 * Here is the process of this function
 * 1. Process any options passed to the Queue() application. Options here mean the third argument to Queue()
 * 2. Iterate trough the members of the queue, creating a callattempt corresponding to each member. During this
 *    iteration, we also check the dialed_interfaces datastore to see if we have already attempted calling this
 *    member. If we have, we do not create a callattempt. This is in place to prevent call forwarding loops. Also
 *    during each iteration, we call calc_metric to determine which members should be rung when.
 * 3. Call ring_one to place a call to the appropriate member(s)
 * 4. Call wait_for_answer to wait for an answer. If no one answers, return.
 * 5. Take care of any holdtime announcements, member delays, or other options which occur after a call has been answered.
 * 6. Start the monitor or mixmonitor if the option is set
 * 7. Remove the caller from the queue to allow other callers to advance
 * 8. Bridge the call.
 * 9. Do any post processing after the call has disconnected.
 *
 * \param[in] qe the queue_ent structure which corresponds to the caller attempting to reach members
 * \param[in] opts the options passed as the third parameter to the Queue() application
 * \param[in] opt_args the options passed as the third parameter to the Queue() application
 * \param[in] announceoverride filename to play to user when waiting
 * \param[in] url the url passed as the fourth parameter to the Queue() application
 * \param[in,out] tries the number of times we have tried calling queue members
 * \param[out] noption set if the call to Queue() has the 'n' option set.
 * \param[in] agi the agi passed as the fifth parameter to the Queue() application
 * \param[in] macro the macro passed as the sixth parameter to the Queue() application
 * \param[in] gosub the gosub passed as the seventh parameter to the Queue() application
 * \param[in] ringing 1 if the 'r' option is set, otherwise 0
 */
static int try_calling(struct queue_ent *qe, struct ast_flags opts, char **opt_args, char *announceoverride, const char *url, int *tries, int *noption, const char *agi, const char *macro, const char *gosub, int ringing)
{
	struct member *cur;
	struct callattempt *outgoing = NULL; /* the list of calls we are building */
	int to, orig;
	char oldexten[AST_MAX_EXTENSION]="";
	char oldcontext[AST_MAX_CONTEXT]="";
	char queuename[256]="";
	char interfacevar[256]="";
	struct ast_channel *peer;
	struct ast_channel *which;
	struct callattempt *lpeer;
	struct member *member;
	struct ast_app *application;
	int res = 0, bridge = 0;
	int numbusies = 0;
	int x=0;
	char *announce = NULL;
	char digit = 0;
	time_t callstart;
	time_t now = time(NULL);
	struct ast_bridge_config bridge_config;
	char nondataquality = 1;
	char *agiexec = NULL;
	char *macroexec = NULL;
	char *gosubexec = NULL;
	const char *monitorfilename;
	char tmpid[256];
	int forwardsallowed = 1;
	int block_connected_line = 0;
	int callcompletedinsl;
	struct ao2_iterator memi;
	struct ast_datastore *datastore;
	struct queue_end_bridge *queue_end_bridge = NULL;

	ast_channel_lock(qe->chan);
	datastore = ast_channel_datastore_find(qe->chan, &dialed_interface_info, NULL);
	ast_channel_unlock(qe->chan);

	memset(&bridge_config, 0, sizeof(bridge_config));
	tmpid[0] = 0;
	time(&now);

	/* If we've already exceeded our timeout, then just stop
	 * This should be extremely rare. queue_exec will take care
	 * of removing the caller and reporting the timeout as the reason.
	 */
	if (qe->expire && now >= qe->expire) {
		res = 0;
		goto out;
	}

	if (ast_test_flag(&opts, OPT_CALLEE_TRANSFER)) {
		ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_REDIRECT);
	}
	if (ast_test_flag(&opts, OPT_CALLER_TRANSFER)) {
		ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_REDIRECT);
	}
	if (ast_test_flag(&opts, OPT_CALLEE_AUTOMON)) {
		ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_AUTOMON);
	}
	if (ast_test_flag(&opts, OPT_CALLER_AUTOMON)) {
		ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_AUTOMON);
	}
	if (ast_test_flag(&opts, OPT_DATA_QUALITY)) {
		nondataquality = 0;
	}
	if (ast_test_flag(&opts, OPT_CALLEE_HANGUP)) {
		ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_DISCONNECT);
	}
	if (ast_test_flag(&opts, OPT_CALLER_HANGUP)) {
		ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT);
	}
	if (ast_test_flag(&opts, OPT_CALLEE_PARK)) {
		ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_PARKCALL);
	}
	if (ast_test_flag(&opts, OPT_CALLER_PARK)) {
		ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_PARKCALL);
	}
	if (ast_test_flag(&opts, OPT_NO_RETRY)) {
		if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY || qe->parent->strategy == QUEUE_STRATEGY_LINEAR
			|| qe->parent->strategy == QUEUE_STRATEGY_RRORDERED) {
			(*tries)++;
		} else {
			*tries = ao2_container_count(qe->parent->members);
		}
		*noption = 1;
	}
	if (ast_test_flag(&opts, OPT_IGNORE_CALL_FW)) {
		forwardsallowed = 0;
	}
	if (ast_test_flag(&opts, OPT_IGNORE_CONNECTEDLINE)) {
		block_connected_line = 1;
	}
	if (ast_test_flag(&opts, OPT_CALLEE_AUTOMIXMON)) {
		ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_AUTOMIXMON);
	}
	if (ast_test_flag(&opts, OPT_CALLER_AUTOMIXMON)) {
		ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_AUTOMIXMON);
	}
	if (ast_test_flag(&opts, OPT_MARK_AS_ANSWERED)) {
		qe->cancel_answered_elsewhere = 1;
	}

	/* if the calling channel has AST_CAUSE_ANSWERED_ELSEWHERE set, make sure this is inherited.
		(this is mainly to support unreal/local channels)
	*/
	if (ast_channel_hangupcause(qe->chan) == AST_CAUSE_ANSWERED_ELSEWHERE) {
		qe->cancel_answered_elsewhere = 1;
	}

	ao2_lock(qe->parent);
	ast_debug(1, "%s is trying to call a queue member.\n",
							ast_channel_name(qe->chan));
	ast_copy_string(queuename, qe->parent->name, sizeof(queuename));
	if (!ast_strlen_zero(qe->announce)) {
		announce = qe->announce;
	}
	if (!ast_strlen_zero(announceoverride)) {
		announce = announceoverride;
	}

	memi = ao2_iterator_init(qe->parent->members, 0);
	while ((cur = ao2_iterator_next(&memi))) {
		struct callattempt *tmp = ast_calloc(1, sizeof(*tmp));
		struct ast_dialed_interface *di;
		AST_LIST_HEAD(,ast_dialed_interface) *dialed_interfaces;
		if (!tmp) {
			ao2_ref(cur, -1);
			ao2_iterator_destroy(&memi);
			ao2_unlock(qe->parent);
			goto out;
		}
		if (!datastore) {
			if (!(datastore = ast_datastore_alloc(&dialed_interface_info, NULL))) {
				callattempt_free(tmp);
				ao2_ref(cur, -1);
				ao2_iterator_destroy(&memi);
				ao2_unlock(qe->parent);
				goto out;
			}
			datastore->inheritance = DATASTORE_INHERIT_FOREVER;
			if (!(dialed_interfaces = ast_calloc(1, sizeof(*dialed_interfaces)))) {
				callattempt_free(tmp);
				ao2_ref(cur, -1);
				ao2_iterator_destroy(&memi);
				ao2_unlock(qe->parent);
				goto out;
			}
			datastore->data = dialed_interfaces;
			AST_LIST_HEAD_INIT(dialed_interfaces);

			ast_channel_lock(qe->chan);
			ast_channel_datastore_add(qe->chan, datastore);
			ast_channel_unlock(qe->chan);
		} else
			dialed_interfaces = datastore->data;

		AST_LIST_LOCK(dialed_interfaces);
		AST_LIST_TRAVERSE(dialed_interfaces, di, list) {
			if (!strcasecmp(cur->interface, di->interface)) {
				ast_debug(1, "Skipping dialing interface '%s' since it has already been dialed\n",
					di->interface);
				break;
			}
		}
		AST_LIST_UNLOCK(dialed_interfaces);

		if (di) {
			callattempt_free(tmp);
			ao2_ref(cur, -1);
			continue;
		}

		/* It is always ok to dial a Local interface.  We only keep track of
		 * which "real" interfaces have been dialed.  The Local channel will
		 * inherit this list so that if it ends up dialing a real interface,
		 * it won't call one that has already been called. */
		if (strncasecmp(cur->interface, "Local/", 6)) {
			if (!(di = ast_calloc(1, sizeof(*di) + strlen(cur->interface)))) {
				callattempt_free(tmp);
				ao2_ref(cur, -1);
				ao2_iterator_destroy(&memi);
				ao2_unlock(qe->parent);
				goto out;
			}
			strcpy(di->interface, cur->interface);

			AST_LIST_LOCK(dialed_interfaces);
			AST_LIST_INSERT_TAIL(dialed_interfaces, di, list);
			AST_LIST_UNLOCK(dialed_interfaces);
		}

		/*
		 * Seed the callattempt's connected line information with previously
		 * acquired connected line info from the queued channel.  The
		 * previously acquired connected line info could have been set
		 * through the CONNECTED_LINE dialplan function.
		 */
		ast_channel_lock(qe->chan);
		ast_party_connected_line_copy(&tmp->connected, ast_channel_connected(qe->chan));
		ast_channel_unlock(qe->chan);

		tmp->block_connected_update = block_connected_line;
		tmp->stillgoing = 1;
		tmp->member = cur;/* Place the reference for cur into callattempt. */
		tmp->lastcall = cur->lastcall;
		tmp->lastqueue = cur->lastqueue;
		ast_copy_string(tmp->interface, cur->interface, sizeof(tmp->interface));
		/* Special case: If we ring everyone, go ahead and ring them, otherwise
		   just calculate their metric for the appropriate strategy */
		if (!calc_metric(qe->parent, cur, x++, qe, tmp)) {
			/* Put them in the list of outgoing thingies...  We're ready now.
			   XXX If we're forcibly removed, these outgoing calls won't get
			   hung up XXX */
			tmp->q_next = outgoing;
			outgoing = tmp;
			/* If this line is up, don't try anybody else */
			if (outgoing->chan && (ast_channel_state(outgoing->chan) == AST_STATE_UP))
				break;
		} else {
			callattempt_free(tmp);
		}
	}
	ao2_iterator_destroy(&memi);

	if (qe->parent->timeoutpriority == TIMEOUT_PRIORITY_APP) {
		/* Application arguments have higher timeout priority (behaviour for <=1.6) */
		if (qe->expire && (!qe->parent->timeout || (qe->expire - now) <= qe->parent->timeout)) {
			to = (qe->expire - now) * 1000;
		} else {
			to = (qe->parent->timeout) ? qe->parent->timeout * 1000 : -1;
		}
	} else {
		/* Config timeout is higher priority thatn application timeout */
		if (qe->expire && qe->expire<=now) {
			to = 0;
		} else if (qe->parent->timeout) {
			to = qe->parent->timeout * 1000;
		} else {
			to = -1;
		}
	}
	orig = to;
	++qe->pending;
	ao2_unlock(qe->parent);
	ring_one(qe, outgoing, &numbusies);
	lpeer = wait_for_answer(qe, outgoing, &to, &digit, numbusies,
		ast_test_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT),
		forwardsallowed, ringing);
	/* The ast_channel_datastore_remove() function could fail here if the
	 * datastore was moved to another channel during a masquerade. If this is
	 * the case, don't free the datastore here because later, when the channel
	 * to which the datastore was moved hangs up, it will attempt to free this
	 * datastore again, causing a crash
	 */
	ast_channel_lock(qe->chan);
	if (datastore && !ast_channel_datastore_remove(qe->chan, datastore)) {
		ast_datastore_free(datastore);
	}
	ast_channel_unlock(qe->chan);
	ao2_lock(qe->parent);
	if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY || qe->parent->strategy == QUEUE_STRATEGY_RRORDERED) {
		store_next_rr(qe, outgoing);

	}
	if (qe->parent->strategy == QUEUE_STRATEGY_LINEAR) {
		store_next_lin(qe, outgoing);
	}
	ao2_unlock(qe->parent);
	peer = lpeer ? lpeer->chan : NULL;
	if (!peer) {
		qe->pending = 0;
		if (to) {
			/* Must gotten hung up */
			res = -1;
		} else {
			/* User exited by pressing a digit */
			res = digit;
		}
		if (res == -1) {
			ast_debug(1, "%s: Nobody answered.\n", ast_channel_name(qe->chan));
		}
	} else { /* peer is valid */
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the
		   conversation.  */
		if (!strcmp(ast_channel_tech(qe->chan)->type, "DAHDI")) {
			ast_channel_setoption(qe->chan, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		}
		if (!strcmp(ast_channel_tech(peer)->type, "DAHDI")) {
			ast_channel_setoption(peer, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		}
		/* Update parameters for the queue */
		time(&now);
		recalc_holdtime(qe, (now - qe->start));
		ao2_lock(qe->parent);
		callcompletedinsl = ((now - qe->start) <= qe->parent->servicelevel);
		ao2_unlock(qe->parent);
		member = lpeer->member;
		/* Increment the refcount for this member, since we're going to be using it for awhile in here. */
		ao2_ref(member, 1);
		hangupcalls(qe, outgoing, peer, qe->cancel_answered_elsewhere);
		outgoing = NULL;
		if (announce || qe->parent->reportholdtime || qe->parent->memberdelay) {
			int res2;

			res2 = ast_autoservice_start(qe->chan);
			if (!res2) {
				if (qe->parent->memberdelay) {
					ast_log(LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
					res2 = ast_safe_sleep(peer, qe->parent->memberdelay * 1000);
				}
				if (!res2 && announce) {
					if (play_file(peer, announce) < 0) {
						ast_log(LOG_ERROR, "play_file failed for '%s' on %s\n", announce, ast_channel_name(peer));
					}
				}
				if (!res2 && qe->parent->reportholdtime) {
					if (!play_file(peer, qe->parent->sound_reporthold)) {
						int holdtime, holdtimesecs;

						time(&now);
						holdtime = abs((now - qe->start) / 60);
						holdtimesecs = abs((now - qe->start) % 60);
						if (holdtime > 0) {
							ast_say_number(peer, holdtime, AST_DIGIT_ANY, ast_channel_language(peer), NULL);
							if (play_file(peer, qe->parent->sound_minutes) < 0) {
								ast_log(LOG_ERROR, "play_file failed for '%s' on %s\n", qe->parent->sound_minutes, ast_channel_name(peer));
							}
						}
						if (holdtimesecs > 1) {
							ast_say_number(peer, holdtimesecs, AST_DIGIT_ANY, ast_channel_language(peer), NULL);
							if (play_file(peer, qe->parent->sound_seconds) < 0) {
								ast_log(LOG_ERROR, "play_file failed for '%s' on %s\n", qe->parent->sound_seconds, ast_channel_name(peer));
							}
						}
					}
				}
				ast_autoservice_stop(qe->chan);
			}
			if (ast_check_hangup(peer)) {
				/* Agent must have hung up */
				ast_log(LOG_WARNING, "Agent on %s hungup on the customer.\n", ast_channel_name(peer));
				ast_queue_log(queuename, ast_channel_uniqueid(qe->chan), member->membername, "AGENTDUMP", "%s", "");

				blob = ast_json_pack("{s: s, s: s, s: s}",
						     "Queue", queuename,
						     "Interface", member->interface,
						     "MemberName", member->membername);
				queue_publish_multi_channel_blob(qe->chan, peer, queue_agent_dump_type(), blob);

				ast_channel_publish_dial(qe->chan, peer, member->interface, ast_hangup_cause_to_dial_status(ast_channel_hangupcause(peer)));
				ast_autoservice_chan_hangup_peer(qe->chan, peer);
				ao2_ref(member, -1);
				goto out;
			} else if (ast_check_hangup(qe->chan)) {
				/* Caller must have hung up just before being connected */
				ast_log(LOG_NOTICE, "Caller was about to talk to agent on %s but the caller hungup.\n", ast_channel_name(peer));
				ast_queue_log(queuename, ast_channel_uniqueid(qe->chan), member->membername, "ABANDON", "%d|%d|%ld", qe->pos, qe->opos, (long) time(NULL) - qe->start);
				record_abandoned(qe);
				ast_channel_publish_dial(qe->chan, peer, member->interface, ast_hangup_cause_to_dial_status(ast_channel_hangupcause(peer)));
				ast_autoservice_chan_hangup_peer(qe->chan, peer);
				ao2_ref(member, -1);
				return -1;
			}
		}
		/* Stop music on hold */
		if (ringing) {
			ast_indicate(qe->chan,-1);
		} else {
			ast_moh_stop(qe->chan);
		}

		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(qe->chan, peer);
		if (res < 0) {
			ast_queue_log(queuename, ast_channel_uniqueid(qe->chan), member->membername, "SYSCOMPAT", "%s", "");
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", ast_channel_name(qe->chan), ast_channel_name(peer));
			record_abandoned(qe);
			ast_channel_publish_dial(qe->chan, peer, member->interface, ast_hangup_cause_to_dial_status(ast_channel_hangupcause(peer)));
			ast_autoservice_chan_hangup_peer(qe->chan, peer);
			ao2_ref(member, -1);
			return -1;
		}

		/* Play announcement to the caller telling it's his turn if defined */
		if (!ast_strlen_zero(qe->parent->sound_callerannounce)) {
			if (play_file(qe->chan, qe->parent->sound_callerannounce)) {
				ast_log(LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", qe->parent->sound_callerannounce);
			}
		}

		ao2_lock(qe->parent);
		/* if setinterfacevar is defined, make member variables available to the channel */
		/* use  pbx_builtin_setvar to set a load of variables with one call */
		if (qe->parent->setinterfacevar) {
			snprintf(interfacevar, sizeof(interfacevar), "MEMBERINTERFACE=%s,MEMBERNAME=%s,MEMBERCALLS=%d,MEMBERLASTCALL=%ld,MEMBERPENALTY=%d,MEMBERDYNAMIC=%d,MEMBERREALTIME=%d",
				member->interface, member->membername, member->calls, (long)member->lastcall, member->penalty, member->dynamic, member->realtime);
		 	pbx_builtin_setvar_multiple(qe->chan, interfacevar);
			pbx_builtin_setvar_multiple(peer, interfacevar);
		}

		/* if setqueueentryvar is defined, make queue entry (i.e. the caller) variables available to the channel */
		/* use  pbx_builtin_setvar to set a load of variables with one call */
		if (qe->parent->setqueueentryvar) {
			snprintf(interfacevar, sizeof(interfacevar), "QEHOLDTIME=%ld,QEORIGINALPOS=%d",
				(long) time(NULL) - qe->start, qe->opos);
			pbx_builtin_setvar_multiple(qe->chan, interfacevar);
			pbx_builtin_setvar_multiple(peer, interfacevar);
		}

		ao2_unlock(qe->parent);

		/* try to set queue variables if configured to do so*/
		set_queue_variables(qe->parent, qe->chan);
		set_queue_variables(qe->parent, peer);

		setup_peer_after_bridge_goto(qe->chan, peer, &opts, opt_args);
		ast_channel_lock(qe->chan);
		if ((monitorfilename = pbx_builtin_getvar_helper(qe->chan, "MONITOR_FILENAME"))) {
				monitorfilename = ast_strdupa(monitorfilename);
		}
		ast_channel_unlock(qe->chan);

		/* Begin Monitoring */
		if (qe->parent->monfmt && *qe->parent->monfmt) {
			if (!qe->parent->montype) {
				const char *monexec;
				ast_debug(1, "Starting Monitor as requested.\n");
				ast_channel_lock(qe->chan);
				if ((monexec = pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC")) || pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC_ARGS")) {
					which = qe->chan;
					monexec = monexec ? ast_strdupa(monexec) : NULL;
				} else {
					which = peer;
				}
				ast_channel_unlock(qe->chan);
				if (monitorfilename) {
					ast_monitor_start(which, qe->parent->monfmt, monitorfilename, 1, X_REC_IN | X_REC_OUT, NULL);
				} else if (qe->chan) {
					ast_monitor_start(which, qe->parent->monfmt, ast_channel_uniqueid(qe->chan), 1, X_REC_IN | X_REC_OUT, NULL);
				} else {
					/* Last ditch effort -- no channel, make up something */
					snprintf(tmpid, sizeof(tmpid), "chan-%lx", (unsigned long)ast_random());
					ast_monitor_start(which, qe->parent->monfmt, tmpid, 1, X_REC_IN | X_REC_OUT, NULL);
				}
				if (!ast_strlen_zero(monexec)) {
					ast_monitor_setjoinfiles(which, 1);
				}
			} else {
				setup_mixmonitor(qe, monitorfilename);
			}
		}
		/* Drop out of the queue at this point, to prepare for next caller */
		leave_queue(qe);
		if (!ast_strlen_zero(url) && ast_channel_supports_html(peer)) {
			ast_debug(1, "app_queue: sendurl=%s.\n", url);
			ast_channel_sendurl(peer, url);
		}

		/* run a macro for this connection if defined. The macro simply returns, no action is taken on the result */
		/* use macro from dialplan if passed as a option, otherwise use the default queue macro */
		if (!ast_strlen_zero(macro)) {
			macroexec = ast_strdupa(macro);
		} else {
			if (qe->parent->membermacro) {
				macroexec = ast_strdupa(qe->parent->membermacro);
			}
		}

		if (!ast_strlen_zero(macroexec)) {
			ast_debug(1, "app_queue: macro=%s.\n", macroexec);
			ast_app_exec_macro(qe->chan, peer, macroexec);
		}

		/* run a gosub for this connection if defined. The gosub simply returns, no action is taken on the result */
		/* use gosub from dialplan if passed as a option, otherwise use the default queue gosub */
		if (!ast_strlen_zero(gosub)) {
			gosubexec = ast_strdupa(gosub);
		} else {
			if (qe->parent->membergosub) {
				gosubexec = ast_strdupa(qe->parent->membergosub);
			}
		}

		if (!ast_strlen_zero(gosubexec)) {
			char *gosub_args = NULL;
			char *gosub_argstart;

			ast_debug(1, "app_queue: gosub=%s.\n", gosubexec);

			gosub_argstart = strchr(gosubexec, ',');
			if (gosub_argstart) {
				const char *what_is_s = "s";
				*gosub_argstart = 0;
				if (!ast_exists_extension(peer, gosubexec, "s", 1, S_COR(ast_channel_caller(peer)->id.number.valid, ast_channel_caller(peer)->id.number.str, NULL)) &&
					 ast_exists_extension(peer, gosubexec, "~~s~~", 1, S_COR(ast_channel_caller(peer)->id.number.valid, ast_channel_caller(peer)->id.number.str, NULL))) {
					what_is_s = "~~s~~";
				}
				if (ast_asprintf(&gosub_args, "%s,%s,1(%s)", gosubexec, what_is_s, gosub_argstart + 1) < 0) {
					gosub_args = NULL;
				}
				*gosub_argstart = ',';
			} else {
				const char *what_is_s = "s";
				if (!ast_exists_extension(peer, gosubexec, "s", 1, S_COR(ast_channel_caller(peer)->id.number.valid, ast_channel_caller(peer)->id.number.str, NULL)) &&
					 ast_exists_extension(peer, gosubexec, "~~s~~", 1, S_COR(ast_channel_caller(peer)->id.number.valid, ast_channel_caller(peer)->id.number.str, NULL))) {
					what_is_s = "~~s~~";
				}
				if (ast_asprintf(&gosub_args, "%s,%s,1", gosubexec, what_is_s) < 0) {
					gosub_args = NULL;
				}
			}
			if (gosub_args) {
				ast_app_exec_sub(qe->chan, peer, gosub_args, 0);
				ast_free(gosub_args);
			} else {
				ast_log(LOG_ERROR, "Could not Allocate string for Gosub arguments -- Gosub Call Aborted!\n");
			}
		}

		if (!ast_strlen_zero(agi)) {
			ast_debug(1, "app_queue: agi=%s.\n", agi);
			application = pbx_findapp("agi");
			if (application) {
				agiexec = ast_strdupa(agi);
				pbx_exec(qe->chan, application, agiexec);
			} else {
				ast_log(LOG_WARNING, "Asked to execute an AGI on this channel, but could not find application (agi)!\n");
			}
		}
		qe->handled++;
		ast_queue_log(queuename, ast_channel_uniqueid(qe->chan), member->membername, "CONNECT", "%ld|%s|%ld", (long) time(NULL) - qe->start, ast_channel_uniqueid(peer),
													(long)(orig - to > 0 ? (orig - to) / 1000 : 0));

		blob = ast_json_pack("{s: s, s: s, s: s, s: i, s: i}",
				     "Queue", queuename,
				     "Interface", member->interface,
				     "MemberName", member->membername,
				     "HoldTime", (long) time(NULL) - qe->start,
				     "RingTime", (long)(orig - to > 0 ? (orig - to) / 1000 : 0));
		queue_publish_multi_channel_blob(qe->chan, peer, queue_agent_connect_type(), blob);

		ast_copy_string(oldcontext, ast_channel_context(qe->chan), sizeof(oldcontext));
		ast_copy_string(oldexten, ast_channel_exten(qe->chan), sizeof(oldexten));

		if ((queue_end_bridge = ao2_alloc(sizeof(*queue_end_bridge), NULL))) {
			queue_end_bridge->q = qe->parent;
			queue_end_bridge->chan = qe->chan;
			bridge_config.end_bridge_callback = end_bridge_callback;
			bridge_config.end_bridge_callback_data = queue_end_bridge;
			bridge_config.end_bridge_callback_data_fixup = end_bridge_callback_data_fixup;
			/* Since queue_end_bridge can survive beyond the life of this call to Queue, we need
			 * to make sure to increase the refcount of this queue so it cannot be freed until we
			 * are done with it. We remove this reference in end_bridge_callback.
			 */
			queue_t_ref(qe->parent, "For bridge_config reference");
		}

		time(&callstart);
		setup_stasis_subs(qe, peer, member, qe->start, callstart, callcompletedinsl);
		bridge = ast_bridge_call_with_flags(qe->chan, peer, &bridge_config,
				AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM | AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM);

		res = bridge ? bridge : 1;
		ao2_ref(member, -1);
	}
out:
	hangupcalls(qe, outgoing, NULL, qe->cancel_answered_elsewhere);

	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	/* Don't need to hold the lock while we setup the outgoing calls */
	int retrywait = qe->parent->retry * 1000;

	int res = ast_waitfordigit(qe->chan, retrywait);
	if (res > 0 && !valid_exit(qe, res)) {
		res = 0;
	}

	return res;
}

static struct member *interface_exists(struct call_queue *q, const char *interface)
{
	struct member *mem;
	struct ao2_iterator mem_iter;

	if (!q) {
		return NULL;
	}
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((mem = ao2_iterator_next(&mem_iter))) {
		if (!strcasecmp(interface, mem->interface)) {
			ao2_iterator_destroy(&mem_iter);
			return mem;
		}
		ao2_ref(mem, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	return NULL;
}


/*! \brief Dump all members in a specific queue to the database
 * \code
 * <pm_family>/<queuename> = <interface>;<penalty>;<paused>;<state_interface>[|...]
 * \endcode
 */
static void dump_queue_members(struct call_queue *pm_queue)
{
	struct member *cur_member;
	struct ast_str *value;
	struct ao2_iterator mem_iter;

	if (!pm_queue) {
		return;
	}

	/* 4K is a reasonable default for most applications, but we grow to
	 * accommodate more if necessary. */
	if (!(value = ast_str_create(4096))) {
		return;
	}

	mem_iter = ao2_iterator_init(pm_queue->members, 0);
	while ((cur_member = ao2_iterator_next(&mem_iter))) {
		if (!cur_member->dynamic) {
			ao2_ref(cur_member, -1);
			continue;
		}

		ast_str_append(&value, 0, "%s%s;%d;%d;%s;%s",
			ast_str_strlen(value) ? "|" : "",
			cur_member->interface,
			cur_member->penalty,
			cur_member->paused,
			cur_member->membername,
			cur_member->state_interface);

		ao2_ref(cur_member, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	if (ast_str_strlen(value) && !cur_member) {
		if (ast_db_put(pm_family, pm_queue->name, ast_str_buffer(value))) {
			ast_log(LOG_WARNING, "failed to create persistent dynamic entry!\n");
		}
	} else {
		/* Delete the entry if the queue is empty or there is an error */
		ast_db_del(pm_family, pm_queue->name);
	}

	ast_free(value);
}

/*! \brief Remove member from queue
 * \retval RES_NOT_DYNAMIC when they aren't a RT member
 * \retval RES_NOSUCHQUEUE queue does not exist
 * \retval RES_OKAY removed member from queue
 * \retval RES_EXISTS queue exists but no members
*/
static int remove_from_queue(const char *queuename, const char *interface)
{
	struct call_queue *q, tmpq = {
		.name = queuename,
	};
	struct member *mem, tmpmem;
	int res = RES_NOSUCHQUEUE;

	ast_copy_string(tmpmem.interface, interface, sizeof(tmpmem.interface));
	if ((q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Temporary reference for interface removal"))) {
		ao2_lock(q);
		if ((mem = ao2_find(q->members, &tmpmem, OBJ_POINTER))) {
			/* XXX future changes should beware of this assumption!! */
			/*Change Penalty on realtime users*/
			if (mem->realtime && !ast_strlen_zero(mem->rt_uniqueid) && negative_penalty_invalid) {
				update_realtime_member_field(mem, q->name, "penalty", "-1");
			} else if (!mem->dynamic) {
				ao2_ref(mem, -1);
				ao2_unlock(q);
				queue_t_unref(q, "Interface wasn't dynamic, expiring temporary reference");
				return RES_NOT_DYNAMIC;
			}
			queue_publish_member_blob(queue_member_removed_type(), queue_member_blob_create(q, mem));

			member_remove_from_queue(q, mem);
			ao2_ref(mem, -1);

			if (queue_persistent_members) {
				dump_queue_members(q);
			}

			if (!num_available_members(q)) {
				ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
			}

			res = RES_OKAY;
		} else {
			res = RES_EXISTS;
		}
		ao2_unlock(q);
		queue_t_unref(q, "Expiring temporary reference");
	}

	return res;
}

/*! \brief Add member to queue
 * \retval RES_NOT_DYNAMIC when they aren't a RT member
 * \retval RES_NOSUCHQUEUE queue does not exist
 * \retval RES_OKAY added member from queue
 * \retval RES_EXISTS queue exists but no members
 * \retval RES_OUT_OF_MEMORY queue exists but not enough memory to create member
*/
static int add_to_queue(const char *queuename, const char *interface, const char *membername, int penalty, int paused, int dump, const char *state_interface)
{
	struct call_queue *q;
	struct member *new_member, *old_member;
	int res = RES_NOSUCHQUEUE;

	/*! \note Ensure the appropriate realtime queue is loaded.  Note that this
	 * short-circuits if the queue is already in memory. */
	if (!(q = find_load_queue_rt_friendly(queuename))) {
		return res;
	}

	ao2_lock(q);
	if ((old_member = interface_exists(q, interface)) == NULL) {
		if ((new_member = create_queue_member(interface, membername, penalty, paused, state_interface, q->ringinuse))) {
			new_member->ringinuse = q->ringinuse;
			new_member->dynamic = 1;
			member_add_to_queue(q, new_member);
			queue_publish_member_blob(queue_member_added_type(), queue_member_blob_create(q, new_member));

			if (is_member_available(q, new_member)) {
				ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
			}

			ao2_ref(new_member, -1);
			new_member = NULL;

			if (dump) {
				dump_queue_members(q);
			}

			res = RES_OKAY;
		} else {
			res = RES_OUTOFMEMORY;
		}
	} else {
		ao2_ref(old_member, -1);
		res = RES_EXISTS;
	}
	ao2_unlock(q);
	queue_t_unref(q, "Expiring temporary reference");

	return res;
}

static int publish_queue_member_pause(struct call_queue *q, struct member *member, const char *reason)
{
	struct ast_json *json_blob = queue_member_blob_create(q, member);

	if (!json_blob) {
		return -1;
	}

	if (!ast_strlen_zero(reason)) {
		ast_json_object_set(json_blob, "Reason", ast_json_string_create(reason));
	}

	queue_publish_member_blob(queue_member_pause_type(), json_blob);

	return 0;
}

static int set_member_paused(const char *queuename, const char *interface, const char *reason, int paused)
{
	int found = 0;
	struct call_queue *q;
	struct ao2_iterator queue_iter;

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate over queues"))) {
		ao2_lock(q);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename)) {
			struct member *mem;

			if ((mem = interface_exists(q, interface))) {
				if (mem->paused == paused) {
					ast_debug(1, "%spausing already-%spaused queue member %s:%s\n", (paused ? "" : "un"), (paused ? "" : "un"), q->name, interface);
				}

				if (mem->realtime) {
					if (update_realtime_member_field(mem, q->name, "paused", paused ? "1" : "0")) {
						ast_log(LOG_WARNING, "Failed %spausing realtime queue member %s:%s\n", (paused ? "" : "un"), q->name, interface);
						ao2_ref(mem, -1);
						ao2_unlock(q);
						queue_t_unref(q, "Done with iterator");
						continue;
					}
				}

				mem->paused = paused;
				ast_devstate_changed(mem->paused ? QUEUE_PAUSED_DEVSTATE : QUEUE_UNPAUSED_DEVSTATE,
					AST_DEVSTATE_CACHABLE, "Queue:%s_pause_%s", q->name, mem->interface);
				found++;

				/* Before we do the PAUSE/UNPAUSE log, if this was a PAUSEALL/UNPAUSEALL, log that here, but only on the first found entry. */
				if (found == 1) {

					/* XXX In all other cases, we use the membername, but since this affects all queues, we cannot */
					if (ast_strlen_zero(queuename)) {
						ast_queue_log("NONE", "NONE", interface, (paused ? "PAUSEALL" : "UNPAUSEALL"), "%s", "");
					}
				}

				if (queue_persistent_members) {
					dump_queue_members(q);
				}

				if (is_member_available(q, mem)) {
					ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
				} else if (!num_available_members(q)) {
					ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "Queue:%s_avail", q->name);
				}

				ast_queue_log(q->name, "NONE", mem->membername, (paused ? "PAUSE" : "UNPAUSE"), "%s", S_OR(reason, ""));

				publish_queue_member_pause(q, mem, reason);
				ao2_ref(mem, -1);
			}
		}

		if (!ast_strlen_zero(queuename) && !strcasecmp(queuename, q->name)) {
			ao2_unlock(q);
			queue_t_unref(q, "Done with iterator");
			break;
		}

		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&queue_iter);

	return found ? RESULT_SUCCESS : RESULT_FAILURE;
}

/*!
 * \internal
 * \brief helper function for set_member_penalty - given a queue, sets all member penalties with the interface
 * \param[in] q queue which is having its member's penalty changed - must be unlocked prior to calling
 * \param[in] interface String of interface used to search for queue members being changed
 * \param[in] penalty Value penalty is being changed to for the member.
 * \retval 0 if the there is no member with interface belonging to q and no change is made
 * \retval 1 if the there is a member with interface belonging to q and changes are made
 */
static int set_member_penalty_help_members(struct call_queue *q, const char *interface, int penalty)
{
	struct member *mem;
	int foundinterface = 0;

	ao2_lock(q);
	if ((mem = interface_exists(q, interface))) {
		foundinterface++;
		if (mem->realtime) {
			char rtpenalty[80];

			sprintf(rtpenalty, "%i", penalty);
			update_realtime_member_field(mem, q->name, "penalty", rtpenalty);
		}

		mem->penalty = penalty;

		ast_queue_log(q->name, "NONE", interface, "PENALTY", "%d", penalty);
		queue_publish_member_blob(queue_member_penalty_type(), queue_member_blob_create(q, mem));
		ao2_ref(mem, -1);
	}
	ao2_unlock(q);

	return foundinterface;
}

static int set_member_ringinuse_help_members(struct call_queue *q, const char *interface, int ringinuse)
{
	struct member *mem;
	int foundinterface = 0;

	ao2_lock(q);
	if ((mem = interface_exists(q, interface))) {
		foundinterface++;
		if (mem->realtime) {
			char rtringinuse[80];

			sprintf(rtringinuse, "%i", ringinuse);
			update_realtime_member_field(mem, q->name, realtime_ringinuse_field, rtringinuse);
		}

		mem->ringinuse = ringinuse;

		ast_queue_log(q->name, "NONE", interface, "RINGINUSE", "%d", ringinuse);
		queue_publish_member_blob(queue_member_ringinuse_type(), queue_member_blob_create(q, mem));
		ao2_ref(mem, -1);
	}
	ao2_unlock(q);

	return foundinterface;
}

static int set_member_value_help_members(struct call_queue *q, const char *interface, int property, int value)
{
	switch(property) {
	case MEMBER_PENALTY:
		return set_member_penalty_help_members(q, interface, value);

	case MEMBER_RINGINUSE:
		return set_member_ringinuse_help_members(q, interface, value);

	default:
		ast_log(LOG_ERROR, "Attempted to set invalid property\n");
		return 0;
	}
}

/*!
 * \internal
 * \brief Sets members penalty, if queuename=NULL we set member penalty in all the queues.
 * \param[in] queuename If specified, only act on a member if it belongs to this queue
 * \param[in] interface Interface of queue member(s) having priority set.
 * \param[in] property Which queue property is being set
 * \param[in] penalty Value penalty is being changed to for each member
 */
static int set_member_value(const char *queuename, const char *interface, int property, int value)
{
	int foundinterface = 0, foundqueue = 0;
	struct call_queue *q;
	struct ast_config *queue_config = NULL;
	struct ao2_iterator queue_iter;

	/* property dependent restrictions on values should be checked in this switch */
	switch (property) {
	case MEMBER_PENALTY:
		if (value < 0 && !negative_penalty_invalid) {
			ast_log(LOG_ERROR, "Invalid penalty (%d)\n", value);
			return RESULT_FAILURE;
		}
	}

	if (ast_strlen_zero(queuename)) { /* This means we need to iterate through all the queues. */
		if (ast_check_realtime("queues")) {
			char *name;
			queue_config = ast_load_realtime_multientry("queues", "name LIKE", "%", SENTINEL);
			if (queue_config) {
				for (name = ast_category_browse(queue_config, NULL);
					 !ast_strlen_zero(name);
					 name = ast_category_browse(queue_config, name)) {
					if ((q = find_load_queue_rt_friendly(name))) {
						foundqueue++;
						foundinterface += set_member_value_help_members(q, interface, property, value);
					}
				}
			}
		}

		/* After hitting realtime queues, go back and get the regular ones. */
		queue_iter = ao2_iterator_init(queues, 0);
		while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
			foundqueue++;
			foundinterface += set_member_value_help_members(q, interface, property, value);
		}
		ao2_iterator_destroy(&queue_iter);
	} else { /* We actually have a queuename, so we can just act on the single queue. */
		if ((q = find_load_queue_rt_friendly(queuename))) {
			foundqueue++;
			foundinterface += set_member_value_help_members(q, interface, property, value);
		}
	}

	if (foundinterface) {
		return RESULT_SUCCESS;
	} else if (!foundqueue) {
		ast_log (LOG_ERROR, "Invalid queuename\n");
	} else {
		ast_log (LOG_ERROR, "Invalid interface\n");
	}

	return RESULT_FAILURE;
}

/* \brief Gets members penalty.
 * \return Return the members penalty or RESULT_FAILURE on error.
*/
static int get_member_penalty(char *queuename, char *interface)
{
	int foundqueue = 0, penalty;
	struct call_queue *q, tmpq = {
		.name = queuename,
	};
	struct member *mem;

	if ((q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Search for queue"))) {
		foundqueue = 1;
		ao2_lock(q);
		if ((mem = interface_exists(q, interface))) {
			penalty = mem->penalty;
			ao2_ref(mem, -1);
			ao2_unlock(q);
			queue_t_unref(q, "Search complete");
			return penalty;
		}
		ao2_unlock(q);
		queue_t_unref(q, "Search complete");
	}

	/* some useful debuging */
	if (foundqueue) {
		ast_log (LOG_ERROR, "Invalid queuename\n");
	} else {
		ast_log (LOG_ERROR, "Invalid interface\n");
	}

	return RESULT_FAILURE;
}

/*! \brief Reload dynamic queue members persisted into the astdb */
static void reload_queue_members(void)
{
	char *cur_ptr;
	const char *queue_name;
	char *member;
	char *interface;
	char *membername = NULL;
	char *state_interface;
	char *penalty_tok;
	int penalty = 0;
	char *paused_tok;
	int paused = 0;
	struct ast_db_entry *db_tree;
	struct ast_db_entry *entry;
	struct call_queue *cur_queue;
	char *queue_data;

	/* Each key in 'pm_family' is the name of a queue */
	db_tree = ast_db_gettree(pm_family, NULL);
	for (entry = db_tree; entry; entry = entry->next) {

		queue_name = entry->key + strlen(pm_family) + 2;

		{
			struct call_queue tmpq = {
				.name = queue_name,
			};
			cur_queue = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Reload queue members");
		}

		if (!cur_queue) {
			cur_queue = find_load_queue_rt_friendly(queue_name);
		}

		if (!cur_queue) {
			/* If the queue no longer exists, remove it from the
			 * database */
			ast_log(LOG_WARNING, "Error loading persistent queue: '%s': it does not exist\n", queue_name);
			ast_db_del(pm_family, queue_name);
			continue;
		}

		if (ast_db_get_allocated(pm_family, queue_name, &queue_data)) {
			queue_t_unref(cur_queue, "Expire reload reference");
			continue;
		}

		cur_ptr = queue_data;
		while ((member = strsep(&cur_ptr, ",|"))) {
			if (ast_strlen_zero(member)) {
				continue;
			}

			interface = strsep(&member, ";");
			penalty_tok = strsep(&member, ";");
			paused_tok = strsep(&member, ";");
			membername = strsep(&member, ";");
			state_interface = strsep(&member, ";");

			if (!penalty_tok) {
				ast_log(LOG_WARNING, "Error parsing persistent member string for '%s' (penalty)\n", queue_name);
				break;
			}
			penalty = strtol(penalty_tok, NULL, 10);
			if (errno == ERANGE) {
				ast_log(LOG_WARNING, "Error converting penalty: %s: Out of range.\n", penalty_tok);
				break;
			}

			if (!paused_tok) {
				ast_log(LOG_WARNING, "Error parsing persistent member string for '%s' (paused)\n", queue_name);
				break;
			}
			paused = strtol(paused_tok, NULL, 10);
			if ((errno == ERANGE) || paused < 0 || paused > 1) {
				ast_log(LOG_WARNING, "Error converting paused: %s: Expected 0 or 1.\n", paused_tok);
				break;
			}

			ast_debug(1, "Reload Members: Queue: %s  Member: %s  Name: %s  Penalty: %d  Paused: %d\n", queue_name, interface, membername, penalty, paused);

			if (add_to_queue(queue_name, interface, membername, penalty, paused, 0, state_interface) == RES_OUTOFMEMORY) {
				ast_log(LOG_ERROR, "Out of Memory when reloading persistent queue member\n");
				break;
			}
		}
		queue_t_unref(cur_queue, "Expire reload reference");
		ast_free(queue_data);
	}

	if (db_tree) {
		ast_log(LOG_NOTICE, "Queue members successfully reloaded from database.\n");
		ast_db_freetree(db_tree);
	}
}

/*! \brief PauseQueueMember application */
static int pqm_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
		AST_APP_ARG(reason);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "PauseQueueMember requires an argument ([queuename],interface[,options][,reason])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.interface)) {
		ast_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename],interface[,options[,reason]])\n");
		return -1;
	}

	if (set_member_paused(args.queuename, args.interface, args.reason, 1)) {
		ast_log(LOG_WARNING, "Attempt to pause interface %s, not found\n", args.interface);
		pbx_builtin_setvar_helper(chan, "PQMSTATUS", "NOTFOUND");
		return 0;
	}

	pbx_builtin_setvar_helper(chan, "PQMSTATUS", "PAUSED");

	return 0;
}

/*! \brief UnPauseQueueMember application */
static int upqm_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
		AST_APP_ARG(reason);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "UnpauseQueueMember requires an argument ([queuename],interface[,options[,reason]])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.interface)) {
		ast_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename],interface[,options[,reason]])\n");
		return -1;
	}

	if (set_member_paused(args.queuename, args.interface, args.reason, 0)) {
		ast_log(LOG_WARNING, "Attempt to unpause interface %s, not found\n", args.interface);
		pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "NOTFOUND");
		return 0;
	}

	pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "UNPAUSED");

	return 0;
}

/*! \brief RemoveQueueMember application */
static int rqm_exec(struct ast_channel *chan, const char *data)
{
	int res=-1;
	char *parse, *temppos = NULL;
	struct member *mem = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
	);


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename[,interface])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(ast_channel_name(chan));
		temppos = strrchr(args.interface, '-');
		if (temppos) {
			*temppos = '\0';
		}
	}

	ast_debug(1, "queue: %s, member: %s\n", args.queuename, args.interface);

	if (log_membername_as_agent) {
		mem = find_member_by_queuename_and_interface(args.queuename, args.interface);
	}

	switch (remove_from_queue(args.queuename, args.interface)) {
	case RES_OKAY:
		if (!mem || ast_strlen_zero(mem->membername)) {
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), args.interface, "REMOVEMEMBER", "%s", "");
		} else {
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), mem->membername, "REMOVEMEMBER", "%s", "");
		}
		ast_log(LOG_NOTICE, "Removed interface '%s' from queue '%s'\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "REMOVED");
		res = 0;
		break;
	case RES_EXISTS:
		ast_debug(1, "Unable to remove interface '%s' from queue '%s': Not there\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOTINQUEUE");
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOSUCHQUEUE");
		res = 0;
		break;
	case RES_NOT_DYNAMIC:
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': '%s' is not a dynamic member\n", args.queuename, args.interface);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOTDYNAMIC");
		res = 0;
		break;
	}

	if (mem) {
		ao2_ref(mem, -1);
	}

	return res;
}

/*! \brief AddQueueMember application */
static int aqm_exec(struct ast_channel *chan, const char *data)
{
	int res=-1;
	char *parse, *temppos = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(penalty);
		AST_APP_ARG(options);
		AST_APP_ARG(membername);
		AST_APP_ARG(state_interface);
	);
	int penalty = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AddQueueMember requires an argument (queuename[,interface[,penalty[,options[,membername[,stateinterface]]]]])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(ast_channel_name(chan));
		temppos = strrchr(args.interface, '-');
		if (temppos) {
			*temppos = '\0';
		}
	}

	if (!ast_strlen_zero(args.penalty)) {
		if ((sscanf(args.penalty, "%30d", &penalty) != 1) || penalty < 0) {
			ast_log(LOG_WARNING, "Penalty '%s' is invalid, must be an integer >= 0\n", args.penalty);
			penalty = 0;
		}
	}

	switch (add_to_queue(args.queuename, args.interface, args.membername, penalty, 0, queue_persistent_members, args.state_interface)) {
	case RES_OKAY:
		if (ast_strlen_zero(args.membername) || !log_membername_as_agent) {
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), args.interface, "ADDMEMBER", "%s", "");
		} else {
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), args.membername, "ADDMEMBER", "%s", "");
		}
		ast_log(LOG_NOTICE, "Added interface '%s' to queue '%s'\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "ADDED");
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_WARNING, "Unable to add interface '%s' to queue '%s': Already there\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "MEMBERALREADY");
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "NOSUCHQUEUE");
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		ast_log(LOG_ERROR, "Out of memory adding interface %s to queue %s\n", args.interface, args.queuename);
		break;
	}

	return res;
}

/*! \brief QueueLog application */
static int ql_exec(struct ast_channel *chan, const char *data)
{
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(uniqueid);
		AST_APP_ARG(membername);
		AST_APP_ARG(event);
		AST_APP_ARG(params);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "QueueLog requires arguments (queuename,uniqueid,membername,event[,additionalinfo]\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.queuename) || ast_strlen_zero(args.uniqueid)
	    || ast_strlen_zero(args.membername) || ast_strlen_zero(args.event)) {
		ast_log(LOG_WARNING, "QueueLog requires arguments (queuename,uniqueid,membername,event[,additionalinfo])\n");
		return -1;
	}

	ast_queue_log(args.queuename, args.uniqueid, args.membername, args.event,
		"%s", args.params ? args.params : "");

	return 0;
}

/*! \brief Copy rule from global list into specified queue */
static void copy_rules(struct queue_ent *qe, const char *rulename)
{
	struct penalty_rule *pr_iter;
	struct rule_list *rl_iter;
	const char *tmp = ast_strlen_zero(rulename) ? qe->parent->defaultrule : rulename;
	AST_LIST_LOCK(&rule_lists);
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list) {
		if (!strcasecmp(rl_iter->name, tmp)) {
			break;
		}
	}
	if (rl_iter) {
		AST_LIST_TRAVERSE(&rl_iter->rules, pr_iter, list) {
			struct penalty_rule *new_pr = ast_calloc(1, sizeof(*new_pr));
			if (!new_pr) {
				ast_log(LOG_ERROR, "Memory allocation error when copying penalty rules! Aborting!\n");
				break;
			}
			new_pr->time = pr_iter->time;
			new_pr->max_value = pr_iter->max_value;
			new_pr->min_value = pr_iter->min_value;
			new_pr->max_relative = pr_iter->max_relative;
			new_pr->min_relative = pr_iter->min_relative;
			AST_LIST_INSERT_TAIL(&qe->qe_rules, new_pr, list);
		}
	}
	AST_LIST_UNLOCK(&rule_lists);
}

/*!\brief The starting point for all queue calls
 *
 * The process involved here is to
 * 1. Parse the options specified in the call to Queue()
 * 2. Join the queue
 * 3. Wait in a loop until it is our turn to try calling a queue member
 * 4. Attempt to call a queue member
 * 5. If 4. did not result in a bridged call, then check for between
 *    call options such as periodic announcements etc.
 * 6. Try 4 again unless some condition (such as an expiration time) causes us to
 *    exit the queue.
 */
static int queue_exec(struct ast_channel *chan, const char *data)
{
	int res=-1;
	int ringing=0;
	const char *user_priority;
	const char *max_penalty_str;
	const char *min_penalty_str;
	int prio;
	int qcontinue = 0;
	int max_penalty, min_penalty;
	enum queue_result reason = QUEUE_UNKNOWN;
	/* whether to exit Queue application after the timeout hits */
	int tries = 0;
	int noption = 0;
	char *parse;
	int makeannouncement = 0;
	int position = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(options);
		AST_APP_ARG(url);
		AST_APP_ARG(announceoverride);
		AST_APP_ARG(queuetimeoutstr);
		AST_APP_ARG(agi);
		AST_APP_ARG(macro);
		AST_APP_ARG(gosub);
		AST_APP_ARG(rule);
		AST_APP_ARG(position);
	);
	/* Our queue entry */
	struct queue_ent qe = { 0 };
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Queue requires an argument: queuename[,options[,URL[,announceoverride[,timeout[,agi[,macro[,gosub[,rule[,position]]]]]]]]]\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	ast_debug(1, "queue: %s, options: %s, url: %s, announce: %s, timeout: %s, agi: %s, macro: %s, gosub: %s, rule: %s, position: %s\n",
		args.queuename,
		S_OR(args.options, ""),
		S_OR(args.url, ""),
		S_OR(args.announceoverride, ""),
		S_OR(args.queuetimeoutstr, ""),
		S_OR(args.agi, ""),
		S_OR(args.macro, ""),
		S_OR(args.gosub, ""),
		S_OR(args.rule, ""),
		S_OR(args.position, ""));

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(queue_exec_options, &opts, opt_args, args.options);
	}

	/* Setup our queue entry */
	qe.start = time(NULL);

	/* set the expire time based on the supplied timeout; */
	if (!ast_strlen_zero(args.queuetimeoutstr)) {
		qe.expire = qe.start + atoi(args.queuetimeoutstr);
	} else {
		qe.expire = 0;
	}

	/* Get the priority from the variable ${QUEUE_PRIO} */
	ast_channel_lock(chan);
	user_priority = pbx_builtin_getvar_helper(chan, "QUEUE_PRIO");
	if (user_priority) {
		if (sscanf(user_priority, "%30d", &prio) == 1) {
			ast_debug(1, "%s: Got priority %d from ${QUEUE_PRIO}.\n", ast_channel_name(chan), prio);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
				user_priority, ast_channel_name(chan));
			prio = 0;
		}
	} else {
		ast_debug(3, "NO QUEUE_PRIO variable found. Using default.\n");
		prio = 0;
	}

	/* Get the maximum penalty from the variable ${QUEUE_MAX_PENALTY} */

	if ((max_penalty_str = pbx_builtin_getvar_helper(chan, "QUEUE_MAX_PENALTY"))) {
		if (sscanf(max_penalty_str, "%30d", &max_penalty) == 1) {
			ast_debug(1, "%s: Got max penalty %d from ${QUEUE_MAX_PENALTY}.\n", ast_channel_name(chan), max_penalty);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_MAX_PENALTY}: Invalid value (%s), channel %s.\n",
				max_penalty_str, ast_channel_name(chan));
			max_penalty = INT_MAX;
		}
	} else {
		max_penalty = INT_MAX;
	}

	if ((min_penalty_str = pbx_builtin_getvar_helper(chan, "QUEUE_MIN_PENALTY"))) {
		if (sscanf(min_penalty_str, "%30d", &min_penalty) == 1) {
			ast_debug(1, "%s: Got min penalty %d from ${QUEUE_MIN_PENALTY}.\n", ast_channel_name(chan), min_penalty);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_MIN_PENALTY}: Invalid value (%s), channel %s.\n",
				min_penalty_str, ast_channel_name(chan));
			min_penalty = INT_MAX;
		}
	} else {
		min_penalty = INT_MAX;
	}
	ast_channel_unlock(chan);

	if (ast_test_flag(&opts, OPT_RINGING)) {
		ringing = 1;
	}

	if (ringing != 1 && ast_test_flag(&opts, OPT_RING_WHEN_RINGING)) {
		qe.ring_when_ringing = 1;
	}

	if (ast_test_flag(&opts, OPT_GO_ON)) {
		qcontinue = 1;
	}

	if (args.position) {
		position = atoi(args.position);
		if (position < 0) {
			ast_log(LOG_WARNING, "Invalid position '%s' given for call to queue '%s'. Assuming no preference for position\n", args.position, args.queuename);
			position = 0;
		}
	}

	ast_debug(1, "queue: %s, expires: %ld, priority: %d\n",
		args.queuename, (long)qe.expire, prio);

	qe.chan = chan;
	qe.prio = prio;
	qe.max_penalty = max_penalty;
	qe.min_penalty = min_penalty;
	qe.last_pos_said = 0;
	qe.last_pos = 0;
	qe.last_periodic_announce_time = time(NULL);
	qe.last_periodic_announce_sound = 0;
	qe.valid_digits = 0;
	if (join_queue(args.queuename, &qe, &reason, position)) {
		ast_log(LOG_WARNING, "Unable to join queue '%s'\n", args.queuename);
		set_queue_result(chan, reason);
		return 0;
	}
	ast_assert(qe.parent != NULL);

	ast_queue_log(args.queuename, ast_channel_uniqueid(chan), "NONE", "ENTERQUEUE", "%s|%s|%d",
		S_OR(args.url, ""),
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""),
		qe.opos);
	copy_rules(&qe, args.rule);
	qe.pr = AST_LIST_FIRST(&qe.qe_rules);
check_turns:
	if (ringing) {
		ast_indicate(chan, AST_CONTROL_RINGING);
	} else {
		ast_moh_start(chan, qe.moh, NULL);
	}

	/* This is the wait loop for callers 2 through maxlen */
	res = wait_our_turn(&qe, ringing, &reason);
	if (res) {
		goto stop;
	}

	makeannouncement = 0;

	for (;;) {
		/* This is the wait loop for the head caller*/
		/* To exit, they may get their call answered; */
		/* they may dial a digit from the queue context; */
		/* or, they may timeout. */

		/* Leave if we have exceeded our queuetimeout */
		if (qe.expire && (time(NULL) >= qe.expire)) {
			record_abandoned(&qe);
			reason = QUEUE_TIMEOUT;
			res = 0;
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan),"NONE", "EXITWITHTIMEOUT", "%d|%d|%ld",
				qe.pos, qe.opos, (long) time(NULL) - qe.start);
			break;
		}

		if (makeannouncement) {
			/* Make a position announcement, if enabled */
			if (qe.parent->announcefrequency)
				if ((res = say_position(&qe,ringing)))
					goto stop;
		}
		makeannouncement = 1;

		/* Make a periodic announcement, if enabled */
		if (qe.parent->periodicannouncefrequency) {
			if ((res = say_periodic_announcement(&qe,ringing))) {
				goto stop;
			}
		}

		/* Leave if we have exceeded our queuetimeout */
		if (qe.expire && (time(NULL) >= qe.expire)) {
			record_abandoned(&qe);
			reason = QUEUE_TIMEOUT;
			res = 0;
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
			break;
		}

		/* see if we need to move to the next penalty level for this queue */
		while (qe.pr && ((time(NULL) - qe.start) > qe.pr->time)) {
			update_qe_rule(&qe);
		}

		/* Try calling all queue members for 'timeout' seconds */
		res = try_calling(&qe, opts, opt_args, args.announceoverride, args.url, &tries, &noption, args.agi, args.macro, args.gosub, ringing);
		if (res) {
			goto stop;
		}

		if (qe.parent->leavewhenempty) {
			int status = 0;
			if ((status = get_member_status(qe.parent, qe.max_penalty, qe.min_penalty, qe.parent->leavewhenempty, 0))) {
				record_abandoned(&qe);
				reason = QUEUE_LEAVEEMPTY;
				ast_queue_log(args.queuename, ast_channel_uniqueid(chan), "NONE", "EXITEMPTY", "%d|%d|%ld", qe.pos, qe.opos, (long)(time(NULL) - qe.start));
				res = 0;
				break;
			}
		}

		/* exit after 'timeout' cycle if 'n' option enabled */
		if (noption && tries >= ao2_container_count(qe.parent->members)) {
			ast_verb(3, "Exiting on time-out cycle\n");
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
			record_abandoned(&qe);
			reason = QUEUE_TIMEOUT;
			res = 0;
			break;
		}


		/* Leave if we have exceeded our queuetimeout */
		if (qe.expire && (time(NULL) >= qe.expire)) {
			record_abandoned(&qe);
			reason = QUEUE_TIMEOUT;
			res = 0;
			ast_queue_log(qe.parent->name, ast_channel_uniqueid(qe.chan),"NONE", "EXITWITHTIMEOUT", "%d|%d|%ld", qe.pos, qe.opos, (long) time(NULL) - qe.start);
			break;
		}

		/* If using dynamic realtime members, we should regenerate the member list for this queue */
		update_realtime_members(qe.parent);
		/* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
		res = wait_a_bit(&qe);
		if (res) {
			goto stop;
		}

		/* Since this is a priority queue and
		 * it is not sure that we are still at the head
		 * of the queue, go and check for our turn again.
		 */
		if (!is_our_turn(&qe)) {
			ast_debug(1, "Darn priorities, going back in queue (%s)!\n", ast_channel_name(qe.chan));
			goto check_turns;
		}
	}

stop:
	if (res) {
		if (res < 0) {
			if (!qe.handled) {
				record_abandoned(&qe);
				ast_queue_log(args.queuename, ast_channel_uniqueid(chan), "NONE", "ABANDON",
					"%d|%d|%ld", qe.pos, qe.opos,
					(long) time(NULL) - qe.start);
				res = -1;
			} else if (qcontinue) {
				reason = QUEUE_CONTINUE;
				res = 0;
			}
		} else if (qe.valid_digits) {
			ast_queue_log(args.queuename, ast_channel_uniqueid(chan), "NONE", "EXITWITHKEY",
				"%s|%d|%d|%ld", qe.digits, qe.pos, qe.opos, (long) time(NULL) - qe.start);
		}
	}

	/* Don't allow return code > 0 */
	if (res >= 0) {
		res = 0;
		if (ringing) {
			ast_indicate(chan, -1);
		} else {
			ast_moh_stop(chan);
		}
		ast_stopstream(chan);
	}

	set_queue_variables(qe.parent, qe.chan);

	leave_queue(&qe);
	if (reason != QUEUE_UNKNOWN)
		set_queue_result(chan, reason);

	/*
	 * every queue_ent is given a reference to it's parent
	 * call_queue when it joins the queue.  This ref must be taken
	 * away right before the queue_ent is destroyed.  In this case
	 * the queue_ent is about to be returned on the stack
	 */
	qe.parent = queue_unref(qe.parent);

	return res;
}

/*!
 * \brief create interface var with all queue details.
 * \retval 0 on success
 * \retval -1 on error
*/
static int queue_function_var(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int res = -1;
	struct call_queue *q, tmpq = {
		.name = data,
	};

	char interfacevar[256] = "";
	float sl = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	if ((q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Find for QUEUE() function"))) {
		ao2_lock(q);
		if (q->setqueuevar) {
			sl = 0;
			res = 0;

			if (q->callscompleted > 0) {
				sl = 100 * ((float) q->callscompletedinsl / (float) q->callscompleted);
			}

			snprintf(interfacevar, sizeof(interfacevar),
				"QUEUEMAX=%d,QUEUESTRATEGY=%s,QUEUECALLS=%d,QUEUEHOLDTIME=%d,QUEUETALKTIME=%d,QUEUECOMPLETED=%d,QUEUEABANDONED=%d,QUEUESRVLEVEL=%d,QUEUESRVLEVELPERF=%2.1f",
				q->maxlen, int2strat(q->strategy), q->count, q->holdtime, q->talktime, q->callscompleted, q->callsabandoned,  q->servicelevel, sl);

			pbx_builtin_setvar_multiple(chan, interfacevar);
		}

		ao2_unlock(q);
		queue_t_unref(q, "Done with QUEUE() function");
	} else {
		ast_log(LOG_WARNING, "queue %s was not found\n", data);
	}

	snprintf(buf, len, "%d", res);

	return 0;
}

/*!
 * \brief Check if a given queue exists
 *
 */
static int queue_function_exists(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct call_queue *q;

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}
	q = find_load_queue_rt_friendly(data);
	snprintf(buf, len, "%d", q != NULL? 1 : 0);
	if (q) {
		queue_t_unref(q, "Done with temporary reference in QUEUE_EXISTS()");
	}

	return 0;
}

/*!
 * \brief Get number either busy / free / ready or total members of a specific queue
 * \brief Get or set member properties penalty / paused / ringinuse
 * \retval number of members (busy / free / ready / total) or member info (penalty / paused / ringinuse)
 * \retval -1 on error
*/
static int queue_function_mem_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct member *m;
	struct ao2_iterator mem_iter;
	struct call_queue *q;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(option);
		AST_APP_ARG(interface);
	);
	/* Make sure the returned value on error is zero length string. */
	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Missing required argument. %s(<queuename>,<option>[<interface>])\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 2) {
		ast_log(LOG_ERROR, "Missing required argument. %s(<queuename>,<option>[<interface>])\n", cmd);
		return -1;
	}

	if ((q = find_load_queue_rt_friendly(args.queuename))) {
		ao2_lock(q);
		if (!strcasecmp(args.option, "logged")) {
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((m = ao2_iterator_next(&mem_iter))) {
				/* Count the agents who are logged in and presently answering calls */
				if ((m->status != AST_DEVICE_UNAVAILABLE) && (m->status != AST_DEVICE_INVALID)) {
					count++;
				}
				ao2_ref(m, -1);
			}
			ao2_iterator_destroy(&mem_iter);
		} else if (!strcasecmp(args.option, "free")) {
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((m = ao2_iterator_next(&mem_iter))) {
				/* Count the agents who are logged in and presently answering calls */
				if ((m->status == AST_DEVICE_NOT_INUSE) && (!m->paused)) {
					count++;
				}
				ao2_ref(m, -1);
			}
			ao2_iterator_destroy(&mem_iter);
		} else if (!strcasecmp(args.option, "ready")) {
			time_t now;
			time(&now);
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((m = ao2_iterator_next(&mem_iter))) {
				/* Count the agents who are logged in, not paused and not wrapping up */
				if ((m->status == AST_DEVICE_NOT_INUSE) && (!m->paused) &&
						!(m->lastcall && q->wrapuptime && ((now - q->wrapuptime) < m->lastcall))) {
					count++;
				}
				ao2_ref(m, -1);
			}
			ao2_iterator_destroy(&mem_iter);
		} else if (!strcasecmp(args.option, "count") || ast_strlen_zero(args.option)) {
			count = ao2_container_count(q->members);
		} else if (!strcasecmp(args.option, "penalty") && !ast_strlen_zero(args.interface) &&
			   ((m = interface_exists(q, args.interface)))) {
			count = m->penalty;
			ao2_ref(m, -1);
		} else if (!strcasecmp(args.option, "paused") && !ast_strlen_zero(args.interface) &&
			   ((m = interface_exists(q, args.interface)))) {
			count = m->paused;
			ao2_ref(m, -1);
		} else if ( (!strcasecmp(args.option, "ignorebusy") || !strcasecmp(args.option, "ringinuse")) &&
			   !ast_strlen_zero(args.interface) &&
			   ((m = interface_exists(q, args.interface)))) {
			count = m->ringinuse;
			ao2_ref(m, -1);
		} else if (!ast_strlen_zero(args.interface)) {
			ast_log(LOG_ERROR, "Queue member interface %s not in queue %s\n",
				args.interface, args.queuename);
		} else {
			ast_log(LOG_ERROR, "Unknown option %s provided to %s, valid values are: "
				"logged, free, ready, count, penalty, paused, ringinuse\n", args.option, cmd);
		}
		ao2_unlock(q);
		queue_t_unref(q, "Done with temporary reference in QUEUE_MEMBER()");
	} else {
		ast_log(LOG_WARNING, "queue %s was not found\n", args.queuename);
	}

	snprintf(buf, len, "%d", count);

	return 0;
}

/*! \brief Dialplan function QUEUE_MEMBER() Sets the members penalty / paused / ringinuse. */
static int queue_function_mem_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int memvalue;
	struct call_queue *q;
	struct member *m;
	char rtvalue[80];

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(option);
		AST_APP_ARG(interface);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_MEMBER(<queuename>,<option>,<interface>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 3) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_MEMBER_PENALTY(<queuename>,<interface>)\n");
		return -1;
	}

	if (ast_strlen_zero(args.interface) && ast_strlen_zero(args.option)) {
		ast_log (LOG_ERROR, "<interface> and <option> parameter's can't be null\n");
		return -1;
	}

	memvalue = atoi(value);
	if (!strcasecmp(args.option, "penalty")) {
		/* if queuename = NULL then penalty will be set for interface in all the queues.*/
		if (set_member_value(args.queuename, args.interface, MEMBER_PENALTY, memvalue)) {
			ast_log(LOG_ERROR, "Invalid interface, queue or penalty\n");
			return -1;
		}
	} else if ((q = find_load_queue_rt_friendly(args.queuename))) {
		ao2_lock(q);
		if ((m = interface_exists(q, args.interface))) {
			sprintf(rtvalue, "%s",(memvalue <= 0) ? "0" : "1");
			if (!strcasecmp(args.option, "paused")) {
				if (m->realtime) {
					update_realtime_member_field(m, q->name, args.option, rtvalue);
				}
				m->paused = (memvalue <= 0) ? 0 : 1;
				ast_devstate_changed(m->paused ? QUEUE_PAUSED_DEVSTATE : QUEUE_UNPAUSED_DEVSTATE,
					AST_DEVSTATE_CACHABLE, "Queue:%s_pause_%s", q->name, args.interface);

			} else if ((!strcasecmp(args.option, "ignorebusy")) || (!strcasecmp(args.option, "ringinuse"))) {
				if (m->realtime) {
					update_realtime_member_field(m, q->name, args.option, rtvalue);
				}

				m->ringinuse = (memvalue <= 0) ? 0 : 1;
			} else {
				ast_log(LOG_ERROR, "Invalid option, only penalty , paused or ringinuse/ignorebusy are valid\n");
				ao2_ref(m, -1);
				ao2_unlock(q);
				ao2_ref(q, -1);
				return -1;
			}
			ao2_ref(m, -1);
		} else {
			ao2_unlock(q);
			ao2_ref(q, -1);
			ast_log(LOG_ERROR, "Invalid interface for queue\n");
			return -1;
		}
		ao2_unlock(q);
		ao2_ref(q, -1);
        } else {
		ast_log(LOG_ERROR, "Invalid queue\n");
		return -1;
	}
	return 0;
}

/*!
 * \brief Get the total number of members in a specific queue (Deprecated)
 * \retval number of members
 * \retval -1 on error
*/
static int queue_function_qac_dep(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct member *m;
	struct call_queue *q;
	struct ao2_iterator mem_iter;
	static int depflag = 1;

	if (depflag) {
		depflag = 0;
		ast_log(LOG_NOTICE, "The function QUEUE_MEMBER_COUNT has been deprecated in favor of the QUEUE_MEMBER function and will not be in further releases.\n");
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	if ((q = find_load_queue_rt_friendly(data))) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			/* Count the agents who are logged in and presently answering calls */
			if ((m->status != AST_DEVICE_UNAVAILABLE) && (m->status != AST_DEVICE_INVALID)) {
				count++;
			}
			ao2_ref(m, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
		queue_t_unref(q, "Done with temporary reference in QUEUE_MEMBER_COUNT");
	} else {
		ast_log(LOG_WARNING, "queue %s was not found\n", data);
	}

	snprintf(buf, len, "%d", count);

	return 0;
}

/*! \brief Dialplan function QUEUE_GET_CHANNEL() Get caller channel waiting at specified position in the queue */
static int queue_function_queuegetchannel(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int position;
	char *parse;
	struct call_queue *q;
	struct ast_variable *var;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(position);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_GET_CHANNEL(<queuename>,<position>)\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.queuename)) {
		ast_log (LOG_ERROR, "The <queuename> parameter is required.\n");
		return -1;
	}

	if (ast_strlen_zero(args.position)) {
		position = 1;
	} else {
		if (sscanf(args.position, "%30d", &position) != 1) {
			ast_log (LOG_ERROR, "<position> parameter must be an integer.\n");
			return -1;
		}
		if (position < 1) {
			ast_log (LOG_ERROR, "<position> parameter must be an integer greater than zero.\n");
			return -1;
		}
	}

	{
		struct call_queue tmpq = {
			.name = args.queuename,
		};

		q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Find for QUEUE_GET_CHANNEL()");
	}
	if (q) {
		ao2_lock(q);
		if (q->count >= position) {
			struct queue_ent *qe;

			for (qe = q->head; qe; qe = qe->next) {
				if (qe->pos == position) {
					ast_copy_string(buf, ast_channel_name(qe->chan), len);
					break;
				}
			}
		}
		ao2_unlock(q);
		queue_t_unref(q, "Done with reference in QUEUE_GET_CHANNEL()");
		return 0;
	}

	var = ast_load_realtime("queues", "name", args.queuename, SENTINEL);
	if (var) {
		/* if the queue is realtime but was not found in memory, this
		 * means that the queue had been deleted from memory since it was
		 * "dead."
		 */
		ast_variables_destroy(var);
		return 0;
	}

	ast_log(LOG_WARNING, "queue %s was not found\n", args.queuename);
	return 0;
}

/*! \brief Dialplan function QUEUE_WAITING_COUNT() Get number callers waiting in a specific queue */
static int queue_function_queuewaitingcount(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct call_queue *q, tmpq = {
		.name = data,
	};
	struct ast_variable *var = NULL;

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "QUEUE_WAITING_COUNT requires an argument: queuename\n");
		return -1;
	}

	if ((q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Find for QUEUE_WAITING_COUNT()"))) {
		ao2_lock(q);
		count = q->count;
		ao2_unlock(q);
		queue_t_unref(q, "Done with reference in QUEUE_WAITING_COUNT()");
	} else if ((var = ast_load_realtime("queues", "name", data, SENTINEL))) {
		/* if the queue is realtime but was not found in memory, this
		 * means that the queue had been deleted from memory since it was
		 * "dead." This means it has a 0 waiting count
		 */
		count = 0;
		ast_variables_destroy(var);
	} else {
		ast_log(LOG_WARNING, "queue %s was not found\n", data);
	}

	snprintf(buf, len, "%d", count);

	return 0;
}

/*! \brief Dialplan function QUEUE_MEMBER_LIST() Get list of members in a specific queue */
static int queue_function_queuememberlist(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct call_queue *q, tmpq = {
		.name = data,
	};
	struct member *m;

	/* Ensure an otherwise empty list doesn't return garbage */
	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "QUEUE_MEMBER_LIST requires an argument: queuename\n");
		return -1;
	}

	if ((q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Find for QUEUE_MEMBER_LIST()"))) {
		int buflen = 0, count = 0;
		struct ao2_iterator mem_iter;

		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			/* strcat() is always faster than printf() */
			if (count++) {
				strncat(buf + buflen, ",", len - buflen - 1);
				buflen++;
			}
			strncat(buf + buflen, m->interface, len - buflen - 1);
			buflen += strlen(m->interface);
			/* Safeguard against overflow (negative length) */
			if (buflen >= len - 2) {
				ao2_ref(m, -1);
				ast_log(LOG_WARNING, "Truncating list\n");
				break;
			}
			ao2_ref(m, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
		queue_t_unref(q, "Done with QUEUE_MEMBER_LIST()");
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	/* We should already be terminated, but let's make sure. */
	buf[len - 1] = '\0';

	return 0;
}

/*! \brief Dialplan function QUEUE_MEMBER_PENALTY() Gets the members penalty. */
static int queue_function_memberpenalty_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int penalty;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
	);
	/* Make sure the returned value on error is NULL. */
	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_MEMBER_PENALTY(<queuename>,<interface>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 2) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_MEMBER_PENALTY(<queuename>,<interface>)\n");
		return -1;
	}

	penalty = get_member_penalty (args.queuename, args.interface);

	if (penalty >= 0) { /* remember that buf is already '\0' */
		snprintf (buf, len, "%d", penalty);
	}

	return 0;
}

/*! \brief Dialplan function QUEUE_MEMBER_PENALTY() Sets the members penalty. */
static int queue_function_memberpenalty_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int penalty;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_MEMBER_PENALTY(<queuename>,<interface>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 2) {
		ast_log(LOG_ERROR, "Missing argument. QUEUE_MEMBER_PENALTY(<queuename>,<interface>)\n");
		return -1;
	}

	penalty = atoi(value);

	if (ast_strlen_zero(args.interface)) {
		ast_log (LOG_ERROR, "<interface> parameter can't be null\n");
		return -1;
	}

	/* if queuename = NULL then penalty will be set for interface in all the queues. */
	if (set_member_value(args.queuename, args.interface, MEMBER_PENALTY, penalty)) {
		ast_log(LOG_ERROR, "Invalid interface, queue or penalty\n");
		return -1;
	}

	return 0;
}

static struct ast_custom_function queueexists_function = {
	.name = "QUEUE_EXISTS",
	.read = queue_function_exists,
};

static struct ast_custom_function queuevar_function = {
	.name = "QUEUE_VARIABLES",
	.read = queue_function_var,
};

static struct ast_custom_function queuemembercount_function = {
	.name = "QUEUE_MEMBER",
	.read = queue_function_mem_read,
	.write = queue_function_mem_write,
};

static struct ast_custom_function queuemembercount_dep = {
	.name = "QUEUE_MEMBER_COUNT",
	.read = queue_function_qac_dep,
};

static struct ast_custom_function queuegetchannel_function = {
	.name = "QUEUE_GET_CHANNEL",
	.read = queue_function_queuegetchannel,
};

static struct ast_custom_function queuewaitingcount_function = {
	.name = "QUEUE_WAITING_COUNT",
	.read = queue_function_queuewaitingcount,
};

static struct ast_custom_function queuememberlist_function = {
	.name = "QUEUE_MEMBER_LIST",
	.read = queue_function_queuememberlist,
};

static struct ast_custom_function queuememberpenalty_function = {
	.name = "QUEUE_MEMBER_PENALTY",
	.read = queue_function_memberpenalty_read,
	.write = queue_function_memberpenalty_write,
};

/*! Set the global queue rules parameters as defined in the "general" section of queuerules.conf */
static void queue_rules_set_global_params(struct ast_config *cfg)
{
        const char *general_val = NULL;
        realtime_rules = 0;
        if ((general_val = ast_variable_retrieve(cfg, "general", "realtime_rules"))) {
                realtime_rules = ast_true(general_val);
        }
}

/*! \brief Reload the rules defined in queuerules.conf
 *
 * \param reload If 1, then only process queuerules.conf if the file
 * has changed since the last time we inspected it.
 * \return Always returns AST_MODULE_LOAD_SUCCESS
 */
static int reload_queue_rules(int reload)
{
	struct ast_config *cfg;
	struct rule_list *rl_iter, *new_rl;
	struct penalty_rule *pr_iter;
	char *rulecat = NULL;
	struct ast_variable *rulevar = NULL;
	struct ast_flags config_flags = { (reload && !realtime_rules) ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if (!(cfg = ast_config_load("queuerules.conf", config_flags))) {
		ast_log(LOG_NOTICE, "No queuerules.conf file found, queues will not follow penalty rules\n");
		return AST_MODULE_LOAD_SUCCESS;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_log(LOG_NOTICE, "queuerules.conf has not changed since it was last loaded. Not taking any action.\n");
		return AST_MODULE_LOAD_SUCCESS;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file queuerules.conf is in an invalid format.  Aborting.\n");
		return AST_MODULE_LOAD_SUCCESS;
	}

	AST_LIST_LOCK(&rule_lists);
	while ((rl_iter = AST_LIST_REMOVE_HEAD(&rule_lists, list))) {
		while ((pr_iter = AST_LIST_REMOVE_HEAD(&rl_iter->rules, list)))
			ast_free(pr_iter);
		ast_free(rl_iter);
	}
	while ((rulecat = ast_category_browse(cfg, rulecat))) {
		if (!strcasecmp(rulecat, "general")) {
			queue_rules_set_global_params(cfg);
			continue;
		}
		if (!(new_rl = ast_calloc(1, sizeof(*new_rl)))) {
			AST_LIST_UNLOCK(&rule_lists);
			ast_config_destroy(cfg);
			return AST_MODULE_LOAD_FAILURE;
		} else {
			ast_copy_string(new_rl->name, rulecat, sizeof(new_rl->name));
			AST_LIST_INSERT_TAIL(&rule_lists, new_rl, list);
			for (rulevar = ast_variable_browse(cfg, rulecat); rulevar; rulevar = rulevar->next)
				if(!strcasecmp(rulevar->name, "penaltychange"))
					insert_penaltychange(new_rl->name, rulevar->value, rulevar->lineno);
				else
					ast_log(LOG_WARNING, "Don't know how to handle rule type '%s' on line %d\n", rulevar->name, rulevar->lineno);
		}
	}

	ast_config_destroy(cfg);

	if (realtime_rules && load_realtime_rules()) {
		AST_LIST_UNLOCK(&rule_lists);
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_LIST_UNLOCK(&rule_lists);
	return AST_MODULE_LOAD_SUCCESS;
}

/*! Set the global queue parameters as defined in the "general" section of queues.conf */
static void queue_set_global_params(struct ast_config *cfg)
{
	const char *general_val = NULL;
	queue_persistent_members = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "persistentmembers"))) {
		queue_persistent_members = ast_true(general_val);
	}
	autofill_default = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "autofill"))) {
		autofill_default = ast_true(general_val);
	}
	montype_default = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "monitor-type"))) {
		if (!strcasecmp(general_val, "mixmonitor"))
			montype_default = 1;
	}
	update_cdr = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "updatecdr"))) {
		update_cdr = ast_true(general_val);
	}
	shared_lastcall = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "shared_lastcall"))) {
		shared_lastcall = ast_true(general_val);
	}
	negative_penalty_invalid = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "negative_penalty_invalid"))) {
		negative_penalty_invalid = ast_true(general_val);
	}
	log_membername_as_agent = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "log_membername_as_agent"))) {
		log_membername_as_agent = ast_true(general_val);
	}
}

/*! \brief reload information pertaining to a single member
 *
 * This function is called when a member = line is encountered in
 * queues.conf.
 *
 * \param memberdata The part after member = in the config file
 * \param q The queue to which this member belongs
 */
static void reload_single_member(const char *memberdata, struct call_queue *q)
{
	char *membername, *interface, *state_interface, *tmp;
	char *parse;
	struct member *cur, *newm;
	struct member tmpmem;
	int penalty;
	int ringinuse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(interface);
		AST_APP_ARG(penalty);
		AST_APP_ARG(membername);
		AST_APP_ARG(state_interface);
		AST_APP_ARG(ringinuse);
	);

	if (ast_strlen_zero(memberdata)) {
		ast_log(LOG_WARNING, "Empty queue member definition. Moving on!\n");
		return;
	}

	/* Add a new member */
	parse = ast_strdupa(memberdata);

	AST_STANDARD_APP_ARGS(args, parse);

	interface = args.interface;
	if (!ast_strlen_zero(args.penalty)) {
		tmp = args.penalty;
		ast_strip(tmp);
		penalty = atoi(tmp);
		if (penalty < 0) {
			penalty = 0;
		}
	} else {
		penalty = 0;
	}

	if (!ast_strlen_zero(args.membername)) {
		membername = args.membername;
		ast_strip(membername);
	} else {
		membername = interface;
	}

	if (!ast_strlen_zero(args.state_interface)) {
		state_interface = args.state_interface;
		ast_strip(state_interface);
	} else {
		state_interface = interface;
	}

	if (!ast_strlen_zero(args.ringinuse)) {
		tmp = args.ringinuse;
		ast_strip(tmp);
		if (ast_true(tmp)) {
			ringinuse = 1;
		} else if (ast_false(tmp)) {
			ringinuse = 0;
		} else {
			ast_log(LOG_ERROR, "Member %s has an invalid ringinuse value. Using %s ringinuse value.\n",
				membername, q->name);
			ringinuse = q->ringinuse;
		}
	} else {
		ringinuse = q->ringinuse;
	}

	/* Find the old position in the list */
	ast_copy_string(tmpmem.interface, interface, sizeof(tmpmem.interface));
	cur = ao2_find(q->members, &tmpmem, OBJ_POINTER);

	if ((newm = create_queue_member(interface, membername, penalty, cur ? cur->paused : 0, state_interface, ringinuse))) {
		if (cur) {
			/* Round Robin Queue Position must be copied if this is replacing an existing member */
			ao2_lock(q->members);
			newm->queuepos = cur->queuepos;
			ao2_link(q->members, newm);
			ao2_unlink(q->members, cur);
			ao2_unlock(q->members);
		} else {
			/* Otherwise we need to add using the function that will apply a round robin queue position manually. */
			member_add_to_queue(q, newm);
		}
		ao2_ref(newm, -1);
	}
	newm = NULL;

	if (cur) {
		ao2_ref(cur, -1);
	}
}

static int mark_member_dead(void *obj, void *arg, int flags)
{
	struct member *member = obj;
	if (!member->dynamic && !member->realtime) {
		member->delme = 1;
	}
	return 0;
}

static int kill_dead_members(void *obj, void *arg, int flags)
{
	struct member *member = obj;

	if (!member->delme) {
		member->status = get_queue_member_status(member);
		return 0;
	} else {
		return CMP_MATCH;
	}
}

/*! \brief Reload information pertaining to a particular queue
 *
 * Once we have isolated a queue within reload_queues, we call this. This will either
 * reload information for the queue or if we're just reloading member information, we'll just
 * reload that without touching other settings within the queue
 *
 * \param cfg The configuration which we are reading
 * \param mask Tells us what information we need to reload
 * \param queuename The name of the queue we are reloading information from
 * \retval void
 */
static void reload_single_queue(struct ast_config *cfg, struct ast_flags *mask, const char *queuename)
{
	int new;
	struct call_queue *q = NULL;
	/*We're defining a queue*/
	struct call_queue tmpq = {
		.name = queuename,
	};
	const char *tmpvar;
	const int queue_reload = ast_test_flag(mask, QUEUE_RELOAD_PARAMETERS);
	const int member_reload = ast_test_flag(mask, QUEUE_RELOAD_MEMBER);
	int prev_weight = 0;
	struct ast_variable *var;
	if (!(q = ao2_t_find(queues, &tmpq, OBJ_POINTER, "Find queue for reload"))) {
		if (queue_reload) {
			/* Make one then */
			if (!(q = alloc_queue(queuename))) {
				return;
			}
		} else {
			/* Since we're not reloading queues, this means that we found a queue
			 * in the configuration file which we don't know about yet. Just return.
			 */
			return;
		}
		new = 1;
	} else {
		new = 0;
	}

	if (!new) {
		ao2_lock(q);
		prev_weight = q->weight ? 1 : 0;
	}
	/* Check if we already found a queue with this name in the config file */
	if (q->found) {
		ast_log(LOG_WARNING, "Queue '%s' already defined! Skipping!\n", queuename);
		if (!new) {
			/* It should be impossible to *not* hit this case*/
			ao2_unlock(q);
		}
		queue_t_unref(q, "We exist! Expiring temporary pointer");
		return;
	}
	/* Due to the fact that the "linear" strategy will have a different allocation
	 * scheme for queue members, we must devise the queue's strategy before other initializations.
	 * To be specific, the linear strategy needs to function like a linked list, meaning the ao2
	 * container used will have only a single bucket instead of the typical number.
	 */
	if (queue_reload) {
		if ((tmpvar = ast_variable_retrieve(cfg, queuename, "strategy"))) {
			q->strategy = strat2int(tmpvar);
			if (q->strategy < 0) {
				ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
				tmpvar, q->name);
				q->strategy = QUEUE_STRATEGY_RINGALL;
			}
		} else {
			q->strategy = QUEUE_STRATEGY_RINGALL;
		}
		init_queue(q);
	}
	if (member_reload) {
		ao2_callback(q->members, OBJ_NODATA, mark_member_dead, NULL);
		q->found = 1;
	}

	/* On the first pass we just read the parameters of the queue */
	for (var = ast_variable_browse(cfg, queuename); var; var = var->next) {
		if (queue_reload && strcasecmp(var->name, "member")) {
			queue_set_param(q, var->name, var->value, var->lineno, 1);
		}
	}

	/* On the second pass, we read members */
	for (var = ast_variable_browse(cfg, queuename); var; var = var->next) {
		if (member_reload && !strcasecmp(var->name, "member")) {
			reload_single_member(var->value, q);
		}
	}

	/* At this point, we've determined if the queue has a weight, so update use_weight
	 * as appropriate
	 */
	if (!q->weight && prev_weight) {
		ast_atomic_fetchadd_int(&use_weight, -1);
	} else if (q->weight && !prev_weight) {
		ast_atomic_fetchadd_int(&use_weight, +1);
	}

	/* Free remaining members marked as delme */
	if (member_reload) {
		ao2_lock(q->members);
		ao2_callback(q->members, OBJ_NODATA | OBJ_MULTIPLE, queue_delme_members_decrement_followers, q);
		ao2_callback(q->members, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, kill_dead_members, q);
		ao2_unlock(q->members);
	}

	if (new) {
		queues_t_link(queues, q, "Add queue to container");
	} else {
		ao2_unlock(q);
	}
	queue_t_unref(q, "Expiring creation reference");
}

static int remove_members_and_mark_unfound(void *obj, void *arg, int flags)
{
	struct call_queue *q = obj;
	char *queuename = arg;
	if (!q->realtime && (ast_strlen_zero(queuename) || !strcasecmp(queuename, q->name))) {
		q->found = 0;

	}
	return 0;
}

static int mark_dead_and_unfound(void *obj, void *arg, int flags)
{
	struct call_queue *q = obj;
	char *queuename = arg;
	if (!q->realtime && (ast_strlen_zero(queuename) || !strcasecmp(queuename, q->name))) {
		q->dead = 1;
		q->found = 0;
	}
	return 0;
}

static int kill_dead_queues(void *obj, void *arg, int flags)
{
	struct call_queue *q = obj;
	char *queuename = arg;
	if ((ast_strlen_zero(queuename) || !strcasecmp(queuename, q->name)) && q->dead) {
		return CMP_MATCH;
	} else {
		return 0;
	}
}

/*! \brief reload the queues.conf file
 *
 * This function reloads the information in the general section of the queues.conf
 * file and potentially more, depending on the value of mask.
 *
 * \param reload 0 if we are calling this the first time, 1 every other time
 * \param mask Gives flags telling us what information to actually reload
 * \param queuename If set to a non-zero string, then only reload information from
 * that particular queue. Otherwise inspect all queues
 * \retval -1 Failure occurred
 * \retval 0 All clear!
 */
static int reload_queues(int reload, struct ast_flags *mask, const char *queuename)
{
	struct ast_config *cfg;
	char *cat;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const int queue_reload = ast_test_flag(mask, QUEUE_RELOAD_PARAMETERS);
	const int member_reload = ast_test_flag(mask, QUEUE_RELOAD_MEMBER);

	if (!(cfg = ast_config_load("queues.conf", config_flags))) {
		ast_log(LOG_NOTICE, "No call queueing config file (queues.conf), so no call queues\n");
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file queues.conf is in an invalid format.  Aborting.\n");
		return -1;
	}

	/* We've made it here, so it looks like we're doing operations on all queues. */
	ao2_lock(queues);

	/* Mark all queues as dead for the moment if we're reloading queues.
	 * For clarity, we could just be reloading members, in which case we don't want to mess
	 * with the other queue parameters at all*/
	if (queue_reload) {
		ao2_callback(queues, OBJ_NODATA | OBJ_NOLOCK, mark_dead_and_unfound, (char *) queuename);
	}

	if (member_reload) {
		ao2_callback(queues, OBJ_NODATA, remove_members_and_mark_unfound, (char *) queuename);
	}

	/* Chug through config file */
	cat = NULL;
	while ((cat = ast_category_browse(cfg, cat)) ) {
		if (!strcasecmp(cat, "general") && queue_reload) {
			queue_set_global_params(cfg);
			continue;
		}
		if (ast_strlen_zero(queuename) || !strcasecmp(cat, queuename))
			reload_single_queue(cfg, mask, cat);
	}

	ast_config_destroy(cfg);
	/* Unref all the dead queues if we were reloading queues */
	if (queue_reload) {
		ao2_callback(queues, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NOLOCK, kill_dead_queues, (char *) queuename);
	}
	ao2_unlock(queues);
	return 0;
}

/*! \brief Facilitates resetting statistics for a queue
 *
 * This function actually does not reset any statistics, but
 * rather finds a call_queue struct which corresponds to the
 * passed-in queue name and passes that structure to the
 * clear_queue function. If no queuename is passed in, then
 * all queues will have their statistics reset.
 *
 * \param queuename The name of the queue to reset the statistics
 * for. If this is NULL or zero-length, then this means to reset
 * the statistics for all queues
 * \retval void
 */
static int clear_stats(const char *queuename)
{
	struct call_queue *q;
	struct ao2_iterator queue_iter;

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		ao2_lock(q);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename))
			clear_queue(q);
		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&queue_iter);
	return 0;
}

/*! \brief The command center for all reload operations
 *
 * Whenever any piece of queue information is to be reloaded, this function
 * is called. It interprets the flags set in the mask parameter and acts
 * based on how they are set.
 *
 * \param reload True if we are reloading information, false if we are loading
 * information for the first time.
 * \param mask A bitmask which tells the handler what actions to take
 * \param queuename The name of the queue on which we wish to take action
 * \retval 0 All reloads were successful
 * \retval non-zero There was a failure
 */
static int reload_handler(int reload, struct ast_flags *mask, const char *queuename)
{
	int res = 0;

	if (ast_test_flag(mask, QUEUE_RELOAD_RULES)) {
		res |= reload_queue_rules(reload);
	}
	if (ast_test_flag(mask, QUEUE_RESET_STATS)) {
		res |= clear_stats(queuename);
	}
	if (ast_test_flag(mask, (QUEUE_RELOAD_PARAMETERS | QUEUE_RELOAD_MEMBER))) {
		res |= reload_queues(reload, mask, queuename);
	}
	return res;
}

/*! \brief direct ouput to manager or cli with proper terminator */
static void do_print(struct mansession *s, int fd, const char *str)
{
	if (s) {
		astman_append(s, "%s\r\n", str);
	} else {
		ast_cli(fd, "%s\n", str);
	}
}

/*!
 * \brief Show queue(s) status and statistics
 *
 * List the queues strategy, calls processed, members logged in,
 * other queue statistics such as avg hold time.
*/
static char *__queues_show(struct mansession *s, int fd, int argc, const char * const *argv)
{
	struct call_queue *q;
	struct ast_str *out = ast_str_alloca(240);
	int found = 0;
	time_t now = time(NULL);
	struct ao2_iterator queue_iter;
	struct ao2_iterator mem_iter;

	if (argc != 2 && argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (argc == 3)	{ /* specific queue */
		if ((q = find_load_queue_rt_friendly(argv[2]))) {
			queue_t_unref(q, "Done with temporary pointer");
		}
	} else if (ast_check_realtime("queues")) {
		/* This block is to find any queues which are defined in realtime but
		 * which have not yet been added to the in-core container
		 */
		struct ast_config *cfg = ast_load_realtime_multientry("queues", "name LIKE", "%", SENTINEL);
		char *queuename;
		if (cfg) {
			for (queuename = ast_category_browse(cfg, NULL); !ast_strlen_zero(queuename); queuename = ast_category_browse(cfg, queuename)) {
				if ((q = find_load_queue_rt_friendly(queuename))) {
					queue_t_unref(q, "Done with temporary pointer");
				}
			}
			ast_config_destroy(cfg);
		}
	}

	ao2_lock(queues);
	queue_iter = ao2_iterator_init(queues, AO2_ITERATOR_DONTLOCK);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		float sl;
		struct call_queue *realtime_queue = NULL;

		ao2_lock(q);
		/* This check is to make sure we don't print information for realtime
		 * queues which have been deleted from realtime but which have not yet
		 * been deleted from the in-core container. Only do this if we're not
		 * looking for a specific queue.
		 */
		if (argc < 3 && q->realtime) {
			realtime_queue = find_load_queue_rt_friendly(q->name);
			if (!realtime_queue) {
				ao2_unlock(q);
				queue_t_unref(q, "Done with iterator");
				continue;
			}
			queue_t_unref(realtime_queue, "Queue is already in memory");
		}

		if (argc == 3 && strcasecmp(q->name, argv[2])) {
			ao2_unlock(q);
			queue_t_unref(q, "Done with iterator");
			continue;
		}
		found = 1;

		ast_str_set(&out, 0, "%s has %d calls (max ", q->name, q->count);
		if (q->maxlen) {
			ast_str_append(&out, 0, "%d", q->maxlen);
		} else {
			ast_str_append(&out, 0, "unlimited");
		}
		sl = 0;
		if (q->callscompleted > 0) {
			sl = 100 * ((float) q->callscompletedinsl / (float) q->callscompleted);
		}
		ast_str_append(&out, 0, ") in '%s' strategy (%ds holdtime, %ds talktime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds",
			int2strat(q->strategy), q->holdtime, q->talktime, q->weight,
			q->callscompleted, q->callsabandoned,sl,q->servicelevel);
		do_print(s, fd, ast_str_buffer(out));
		if (!ao2_container_count(q->members)) {
			do_print(s, fd, "   No Members");
		} else {
			struct member *mem;

			do_print(s, fd, "   Members: ");
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((mem = ao2_iterator_next(&mem_iter))) {
				ast_str_set(&out, 0, "      %s", mem->membername);
				if (strcasecmp(mem->membername, mem->interface)) {
					ast_str_append(&out, 0, " (%s", mem->interface);
					if (mem->state_interface) {
						ast_str_append(&out, 0, " from %s", mem->state_interface);
					}
					ast_str_append(&out, 0, ")");
				}
				if (mem->penalty) {
					ast_str_append(&out, 0, " with penalty %d", mem->penalty);
				}

				ast_str_append(&out, 0, " (ringinuse %s)", mem->ringinuse ? "enabled" : "disabled");

				ast_str_append(&out, 0, "%s%s%s%s%s%s%s%s%s (%s%s%s)",
					mem->dynamic ? ast_term_color(COLOR_CYAN, COLOR_BLACK) : "", mem->dynamic ? " (dynamic)" : "", ast_term_reset(),
					mem->realtime ? ast_term_color(COLOR_MAGENTA, COLOR_BLACK) : "", mem->realtime ? " (realtime)" : "", ast_term_reset(),
					mem->paused ? ast_term_color(COLOR_BROWN, COLOR_BLACK) : "", mem->paused ? " (paused)" : "", ast_term_reset(),
					ast_term_color(
						mem->status == AST_DEVICE_UNAVAILABLE || mem->status == AST_DEVICE_UNKNOWN ?
							COLOR_RED : COLOR_GREEN, COLOR_BLACK),
						ast_devstate2str(mem->status), ast_term_reset());
				if (mem->calls) {
					ast_str_append(&out, 0, " has taken %d calls (last was %ld secs ago)",
						mem->calls, (long) (time(NULL) - mem->lastcall));
				} else {
					ast_str_append(&out, 0, " has taken no calls yet");
				}
				do_print(s, fd, ast_str_buffer(out));
				ao2_ref(mem, -1);
			}
			ao2_iterator_destroy(&mem_iter);
		}
		if (!q->head) {
			do_print(s, fd, "   No Callers");
		} else {
			struct queue_ent *qe;
			int pos = 1;

			do_print(s, fd, "   Callers: ");
			for (qe = q->head; qe; qe = qe->next) {
				ast_str_set(&out, 0, "      %d. %s (wait: %ld:%2.2ld, prio: %d)",
					pos++, ast_channel_name(qe->chan), (long) (now - qe->start) / 60,
					(long) (now - qe->start) % 60, qe->prio);
				do_print(s, fd, ast_str_buffer(out));
			}
		}
		do_print(s, fd, "");	/* blank line between entries */
		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator"); /* Unref the iterator's reference */
	}
	ao2_iterator_destroy(&queue_iter);
	ao2_unlock(queues);
	if (!found) {
		if (argc == 3) {
			ast_str_set(&out, 0, "No such queue: %s.", argv[2]);
		} else {
			ast_str_set(&out, 0, "No queues.");
		}
		do_print(s, fd, ast_str_buffer(out));
	}
	return CLI_SUCCESS;
}

/*!
 * \brief Check if a given word is in a space-delimited list
 *
 * \param list Space delimited list of words
 * \param word The word used to search the list
 *
 * \note This function will not return 1 if the word is at the very end of the
 * list (followed immediately by a \0, not a space) since it is used for
 * checking tab-completion and a word at the end is still being tab-completed.
 *
 * \return Returns 1 if the word is found
 * \return Returns 0 if the word is not found
*/
static int word_in_list(const char *list, const char *word) {
	int list_len, word_len = strlen(word);
	const char *find, *end_find, *end_list;

	/* strip whitespace from front */
	while(isspace(*list)) {
		list++;
	}

	while((find = strstr(list, word))) {
		/* beginning of find starts inside another word? */
		if (find != list && *(find - 1) != ' ') {
			list = find;
			/* strip word from front */
			while(!isspace(*list) && *list != '\0') {
				list++;
			}
			/* strip whitespace from front */
			while(isspace(*list)) {
				list++;
			}
			continue;
		}

		/* end of find ends inside another word or at very end of list? */
		list_len = strlen(list);
		end_find = find + word_len;
		end_list = list + list_len;
		if (end_find == end_list || *end_find != ' ') {
			list = find;
			/* strip word from front */
			while(!isspace(*list) && *list != '\0') {
				list++;
			}
			/* strip whitespace from front */
			while(isspace(*list)) {
				list++;
			}
			continue;
		}

		/* terminating conditions satisfied, word at beginning or separated by ' ' */
		return 1;
	}

	return 0;
}

/*!
 * \brief Check if a given word is in a space-delimited list
 *
 * \param line The line as typed not including the current word being completed
 * \param word The word currently being completed
 * \param pos The number of completed words in line
 * \param state The nth desired completion option
 * \param word_list_offset Offset into the line where the list of queues begins.  If non-zero, queues in the list will not be offered for further completion.
 *
 * \return Returns the queue tab-completion for the given word and state
*/
static char *complete_queue(const char *line, const char *word, int pos, int state, ptrdiff_t word_list_offset)
{
	struct call_queue *q;
	char *ret = NULL;
	int which = 0;
	int wordlen = strlen(word);
	struct ao2_iterator queue_iter;
	const char *word_list = NULL;

	/* for certain commands, already completed items should be left out of
	 * the list */
	if (word_list_offset && strlen(line) >= word_list_offset) {
		word_list = line + word_list_offset;
	}

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		if (!strncasecmp(word, q->name, wordlen) && ++which > state
			&& (!word_list_offset || !word_in_list(word_list, q->name))) {
			ret = ast_strdup(q->name);
			queue_t_unref(q, "Done with iterator");
			break;
		}
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&queue_iter);

	/* Pretend "rules" is at the end of the queues list in certain
	 * circumstances since it is an alternate command that should be
	 * tab-completable for "queue show" */
	if (!ret && which == state && !wordlen && !strncmp("queue show", line, 10)) {
		ret = ast_strdup("rules");
	}

	return ret;
}

static char *complete_queue_show(const char *line, const char *word, int pos, int state)
{
	if (pos == 2) {
		return complete_queue(line, word, pos, state, 0);
	}
	return NULL;
}

static char *queue_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch ( cmd ) {
	case CLI_INIT:
		e->command = "queue show";
		e->usage =
			"Usage: queue show\n"
			"       Provides summary information on a specified queue.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_queue_show(a->line, a->word, a->pos, a->n);
	}

	return __queues_show(NULL, a->fd, a->argc, a->argv);
}

/*!\brief callback to display queues status in manager
   \addtogroup Group_AMI
 */
static int manager_queues_show(struct mansession *s, const struct message *m)
{
	static const char * const a[] = { "queue", "show" };

	__queues_show(s, -1, 2, a);
	astman_append(s, "\r\n\r\n");	/* Properly terminate Manager output */

	return RESULT_SUCCESS;
}

static int manager_queue_rule_show(struct mansession *s, const struct message *m)
{
	const char *rule = astman_get_header(m, "Rule");
	const char *id = astman_get_header(m, "ActionID");
	struct rule_list *rl_iter;
	struct penalty_rule *pr_iter;

	astman_append(s, "Response: Success\r\n");
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	AST_LIST_LOCK(&rule_lists);
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list) {
		if (ast_strlen_zero(rule) || !strcasecmp(rule, rl_iter->name)) {
			astman_append(s, "RuleList: %s\r\n", rl_iter->name);
			AST_LIST_TRAVERSE(&rl_iter->rules, pr_iter, list) {
				astman_append(s, "Rule: %d,%s%d,%s%d\r\n", pr_iter->time, pr_iter->max_relative && pr_iter->max_value >= 0 ? "+" : "", pr_iter->max_value, pr_iter->min_relative && pr_iter->min_value >= 0 ? "+" : "", pr_iter->min_value );
			}
			if (!ast_strlen_zero(rule)) {
				break;
			}
		}
	}
	AST_LIST_UNLOCK(&rule_lists);

	/*
	 * Two blank lines instead of one because the Response and
	 * ActionID headers used to not be present.
	 */
	astman_append(s, "\r\n\r\n");

	return RESULT_SUCCESS;
}

/*! \brief Summary of queue info via the AMI */
static int manager_queues_summary(struct mansession *s, const struct message *m)
{
	time_t now;
	int qmemcount = 0;
	int qmemavail = 0;
	int qchancount = 0;
	int qlongestholdtime = 0;
	const char *id = astman_get_header(m, "ActionID");
	const char *queuefilter = astman_get_header(m, "Queue");
	char idText[256] = "";
	struct call_queue *q;
	struct queue_ent *qe;
	struct member *mem;
	struct ao2_iterator queue_iter;
	struct ao2_iterator mem_iter;

	astman_send_ack(s, m, "Queue summary will follow");
	time(&now);
	if (!ast_strlen_zero(id)) {
		snprintf(idText, 256, "ActionID: %s\r\n", id);
	}
	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		ao2_lock(q);

		/* List queue properties */
		if (ast_strlen_zero(queuefilter) || !strcmp(q->name, queuefilter)) {
			/* Reset the necessary local variables if no queuefilter is set*/
			qmemcount = 0;
			qmemavail = 0;
			qchancount = 0;
			qlongestholdtime = 0;

			/* List Queue Members */
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((mem = ao2_iterator_next(&mem_iter))) {
				if ((mem->status != AST_DEVICE_UNAVAILABLE) && (mem->status != AST_DEVICE_INVALID)) {
					++qmemcount;
					if (member_status_available(mem->status) && !mem->paused) {
						++qmemavail;
					}
				}
				ao2_ref(mem, -1);
			}
			ao2_iterator_destroy(&mem_iter);
			for (qe = q->head; qe; qe = qe->next) {
				if ((now - qe->start) > qlongestholdtime) {
					qlongestholdtime = now - qe->start;
				}
				++qchancount;
			}
			astman_append(s, "Event: QueueSummary\r\n"
				"Queue: %s\r\n"
				"LoggedIn: %d\r\n"
				"Available: %d\r\n"
				"Callers: %d\r\n"
				"HoldTime: %d\r\n"
				"TalkTime: %d\r\n"
				"LongestHoldTime: %d\r\n"
				"%s"
				"\r\n",
				q->name, qmemcount, qmemavail, qchancount, q->holdtime, q->talktime, qlongestholdtime, idText);
		}
		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&queue_iter);
	astman_append(s,
		"Event: QueueSummaryComplete\r\n"
		"%s"
		"\r\n", idText);

	return RESULT_SUCCESS;
}

/*! \brief Queue status info via AMI */
static int manager_queues_status(struct mansession *s, const struct message *m)
{
	time_t now;
	int pos;
	const char *id = astman_get_header(m,"ActionID");
	const char *queuefilter = astman_get_header(m,"Queue");
	const char *memberfilter = astman_get_header(m,"Member");
	char idText[256] = "";
	struct call_queue *q;
	struct queue_ent *qe;
	float sl = 0;
	struct member *mem;
	struct ao2_iterator queue_iter;
	struct ao2_iterator mem_iter;

	astman_send_ack(s, m, "Queue status will follow");
	time(&now);
	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	}

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		ao2_lock(q);

		/* List queue properties */
		if (ast_strlen_zero(queuefilter) || !strcmp(q->name, queuefilter)) {
			sl = ((q->callscompleted > 0) ? 100 * ((float)q->callscompletedinsl / (float)q->callscompleted) : 0);
			astman_append(s, "Event: QueueParams\r\n"
				"Queue: %s\r\n"
				"Max: %d\r\n"
				"Strategy: %s\r\n"
				"Calls: %d\r\n"
				"Holdtime: %d\r\n"
				"TalkTime: %d\r\n"
				"Completed: %d\r\n"
				"Abandoned: %d\r\n"
				"ServiceLevel: %d\r\n"
				"ServicelevelPerf: %2.1f\r\n"
				"Weight: %d\r\n"
				"%s"
				"\r\n",
				q->name, q->maxlen, int2strat(q->strategy), q->count, q->holdtime, q->talktime, q->callscompleted,
				q->callsabandoned, q->servicelevel, sl, q->weight, idText);
			/* List Queue Members */
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((mem = ao2_iterator_next(&mem_iter))) {
				if (ast_strlen_zero(memberfilter) || !strcmp(mem->interface, memberfilter) || !strcmp(mem->membername, memberfilter)) {
					astman_append(s, "Event: QueueMember\r\n"
						"Queue: %s\r\n"
						"Name: %s\r\n"
						"Location: %s\r\n"
						"StateInterface: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %d\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n"
						"%s"
						"\r\n",
						q->name, mem->membername, mem->interface, mem->state_interface, mem->dynamic ? "dynamic" : "static",
						mem->penalty, mem->calls, (int)mem->lastcall, mem->status, mem->paused, idText);
				}
				ao2_ref(mem, -1);
			}
			ao2_iterator_destroy(&mem_iter);
			/* List Queue Entries */
			pos = 1;
			for (qe = q->head; qe; qe = qe->next) {
				astman_append(s, "Event: QueueEntry\r\n"
					"Queue: %s\r\n"
					"Position: %d\r\n"
					"Channel: %s\r\n"
					"Uniqueid: %s\r\n"
					"CallerIDNum: %s\r\n"
					"CallerIDName: %s\r\n"
					"ConnectedLineNum: %s\r\n"
					"ConnectedLineName: %s\r\n"
					"Wait: %ld\r\n"
					"%s"
					"\r\n",
					q->name, pos++, ast_channel_name(qe->chan), ast_channel_uniqueid(qe->chan),
					S_COR(ast_channel_caller(qe->chan)->id.number.valid, ast_channel_caller(qe->chan)->id.number.str, "unknown"),
					S_COR(ast_channel_caller(qe->chan)->id.name.valid, ast_channel_caller(qe->chan)->id.name.str, "unknown"),
					S_COR(ast_channel_connected(qe->chan)->id.number.valid, ast_channel_connected(qe->chan)->id.number.str, "unknown"),
					S_COR(ast_channel_connected(qe->chan)->id.name.valid, ast_channel_connected(qe->chan)->id.name.str, "unknown"),
					(long) (now - qe->start), idText);
			}
		}
		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&queue_iter);

	astman_append(s,
		"Event: QueueStatusComplete\r\n"
		"%s"
		"\r\n",idText);

	return RESULT_SUCCESS;
}

static int manager_add_queue_member(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface, *penalty_s, *paused_s, *membername, *state_interface;
	int paused, penalty = 0;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");
	penalty_s = astman_get_header(m, "Penalty");
	paused_s = astman_get_header(m, "Paused");
	membername = astman_get_header(m, "MemberName");
	state_interface = astman_get_header(m, "StateInterface");

	if (ast_strlen_zero(queuename)) {
		astman_send_error(s, m, "'Queue' not specified.");
		return 0;
	}

	if (ast_strlen_zero(interface)) {
		astman_send_error(s, m, "'Interface' not specified.");
		return 0;
	}

	if (ast_strlen_zero(penalty_s)) {
		penalty = 0;
	} else if (sscanf(penalty_s, "%30d", &penalty) != 1 || penalty < 0) {
		penalty = 0;
	}

	if (ast_strlen_zero(paused_s)) {
		paused = 0;
	} else {
		paused = abs(ast_true(paused_s));
	}

	switch (add_to_queue(queuename, interface, membername, penalty, paused, queue_persistent_members, state_interface)) {
	case RES_OKAY:
		if (ast_strlen_zero(membername) || !log_membername_as_agent) {
			ast_queue_log(queuename, "MANAGER", interface, "ADDMEMBER", "%s", paused ? "PAUSED" : "");
		} else {
			ast_queue_log(queuename, "MANAGER", membername, "ADDMEMBER", "%s", paused ? "PAUSED" : "");
		}
		astman_send_ack(s, m, "Added interface to queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to add interface: Already there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to add interface to queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}

	return 0;
}

static int manager_remove_queue_member(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface;
	struct member *mem = NULL;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");

	if (ast_strlen_zero(queuename) || ast_strlen_zero(interface)) {
		astman_send_error(s, m, "Need 'Queue' and 'Interface' parameters.");
		return 0;
	}

	if (log_membername_as_agent) {
		mem = find_member_by_queuename_and_interface(queuename, interface);
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		if (!mem || ast_strlen_zero(mem->membername)) {
			ast_queue_log(queuename, "MANAGER", interface, "REMOVEMEMBER", "%s", "");
		} else {
			ast_queue_log(queuename, "MANAGER", mem->membername, "REMOVEMEMBER", "%s", "");
		}
		astman_send_ack(s, m, "Removed interface from queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to remove interface: Not there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to remove interface from queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	case RES_NOT_DYNAMIC:
		astman_send_error(s, m, "Member not dynamic");
		break;
	}

	if (mem) {
		ao2_ref(mem, -1);
	}

	return 0;
}

static int manager_pause_queue_member(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface, *paused_s, *reason;
	int paused;

	interface = astman_get_header(m, "Interface");
	paused_s = astman_get_header(m, "Paused");
	queuename = astman_get_header(m, "Queue");      /* Optional - if not supplied, pause the given Interface in all queues */
	reason = astman_get_header(m, "Reason");        /* Optional - Only used for logging purposes */

	if (ast_strlen_zero(interface) || ast_strlen_zero(paused_s)) {
		astman_send_error(s, m, "Need 'Interface' and 'Paused' parameters.");
		return 0;
	}

	paused = abs(ast_true(paused_s));

	if (set_member_paused(queuename, interface, reason, paused)) {
		astman_send_error(s, m, "Interface not found");
	} else {
		astman_send_ack(s, m, paused ? "Interface paused successfully" : "Interface unpaused successfully");
	}
	return 0;
}

static int manager_queue_log_custom(struct mansession *s, const struct message *m)
{
	const char *queuename, *event, *message, *interface, *uniqueid;

	queuename = astman_get_header(m, "Queue");
	uniqueid = astman_get_header(m, "UniqueId");
	interface = astman_get_header(m, "Interface");
	event = astman_get_header(m, "Event");
	message = astman_get_header(m, "Message");

	if (ast_strlen_zero(queuename) || ast_strlen_zero(event)) {
		astman_send_error(s, m, "Need 'Queue' and 'Event' parameters.");
		return 0;
	}

	ast_queue_log(queuename, S_OR(uniqueid, "NONE"), interface, event, "%s", message);
	astman_send_ack(s, m, "Event added successfully");

	return 0;
}

static int manager_queue_reload(struct mansession *s, const struct message *m)
{
	struct ast_flags mask = {0,};
	const char *queuename = NULL;
	int header_found = 0;

	queuename = astman_get_header(m, "Queue");
	if (!strcasecmp(S_OR(astman_get_header(m, "Members"), ""), "yes")) {
		ast_set_flag(&mask, QUEUE_RELOAD_MEMBER);
		header_found = 1;
	}
	if (!strcasecmp(S_OR(astman_get_header(m, "Rules"), ""), "yes")) {
		ast_set_flag(&mask, QUEUE_RELOAD_RULES);
		header_found = 1;
	}
	if (!strcasecmp(S_OR(astman_get_header(m, "Parameters"), ""), "yes")) {
		ast_set_flag(&mask, QUEUE_RELOAD_PARAMETERS);
		header_found = 1;
	}

	if (!header_found) {
		ast_set_flag(&mask, AST_FLAGS_ALL);
	}

	if (!reload_handler(1, &mask, queuename)) {
		astman_send_ack(s, m, "Queue reloaded successfully");
	} else {
		astman_send_error(s, m, "Error encountered while reloading queue");
	}
	return 0;
}

static int manager_queue_reset(struct mansession *s, const struct message *m)
{
	const char *queuename = NULL;
	struct ast_flags mask = {QUEUE_RESET_STATS,};

	queuename = astman_get_header(m, "Queue");

	if (!reload_handler(1, &mask, queuename)) {
		astman_send_ack(s, m, "Queue stats reset successfully");
	} else {
		astman_send_error(s, m, "Error encountered while resetting queue stats");
	}
	return 0;
}

static char *complete_queue_add_member(const char *line, const char *word, int pos, int state)
{
	/* 0 - queue; 1 - add; 2 - member; 3 - <interface>; 4 - to; 5 - <queue>; 6 - penalty; 7 - <penalty>; 8 - as; 9 - <membername> */
	switch (pos) {
	case 3: /* Don't attempt to complete name of interface (infinite possibilities) */
		return NULL;
	case 4: /* only one possible match, "to" */
		return state == 0 ? ast_strdup("to") : NULL;
	case 5: /* <queue> */
		return complete_queue(line, word, pos, state, 0);
	case 6: /* only one possible match, "penalty" */
		return state == 0 ? ast_strdup("penalty") : NULL;
	case 7:
		if (state < 100) {      /* 0-99 */
			char *num;
			if ((num = ast_malloc(3))) {
				sprintf(num, "%d", state);
			}
			return num;
		} else {
			return NULL;
		}
	case 8: /* only one possible match, "as" */
		return state == 0 ? ast_strdup("as") : NULL;
	case 9: /* Don't attempt to complete name of member (infinite possibilities) */
		return NULL;
	default:
		return NULL;
	}
}

static int manager_queue_member_ringinuse(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface, *ringinuse_s;
	int ringinuse;

	interface = astman_get_header(m, "Interface");
	ringinuse_s = astman_get_header(m, "RingInUse");

	/* Optional - if not supplied, set the ringinuse value for the given Interface in all queues */
	queuename = astman_get_header(m, "Queue");

	if (ast_strlen_zero(interface) || ast_strlen_zero(ringinuse_s)) {
		astman_send_error(s, m, "Need 'Interface' and 'RingInUse' parameters.");
		return 0;
	}

	if (ast_true(ringinuse_s)) {
		ringinuse = 1;
	} else if (ast_false(ringinuse_s)) {
		ringinuse = 0;
	} else {
		astman_send_error(s, m, "'RingInUse' parameter must be a truth value (yes/no, on/off, 0/1, etc)");
		return 0;
	}

	if (set_member_value(queuename, interface, MEMBER_RINGINUSE, ringinuse)) {
		astman_send_error(s, m, "Invalid interface, queuename, or ringinuse value\n");
	} else {
		astman_send_ack(s, m, "Interface ringinuse set successfully");
	}

	return 0;
}

static int manager_queue_member_penalty(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface, *penalty_s;
	int penalty;

	interface = astman_get_header(m, "Interface");
	penalty_s = astman_get_header(m, "Penalty");
	/* Optional - if not supplied, set the penalty value for the given Interface in all queues */
	queuename = astman_get_header(m, "Queue");

	if (ast_strlen_zero(interface) || ast_strlen_zero(penalty_s)) {
		astman_send_error(s, m, "Need 'Interface' and 'Penalty' parameters.");
		return 0;
	}

	penalty = atoi(penalty_s);

	if (set_member_value((char *)queuename, (char *)interface, MEMBER_PENALTY, penalty)) {
		astman_send_error(s, m, "Invalid interface, queuename or penalty");
	} else {
		astman_send_ack(s, m, "Interface penalty set successfully");
	}

	return 0;
}

static char *handle_queue_add_member(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *queuename, *interface, *membername = NULL, *state_interface = NULL;
	int penalty;

	switch ( cmd ) {
	case CLI_INIT:
		e->command = "queue add member";
		e->usage =
			"Usage: queue add member <dial string> to <queue> [[[penalty <penalty>] as <membername>] state_interface <interface>]\n"
			"       Add a dial string (Such as a channel,e.g. SIP/6001) to a queue with optionally:  a penalty, membername and a state_interface\n";
		return NULL;
	case CLI_GENERATE:
		return complete_queue_add_member(a->line, a->word, a->pos, a->n);
	}

	if ((a->argc != 6) && (a->argc != 8) && (a->argc != 10) && (a->argc != 12)) {
		return CLI_SHOWUSAGE;
	} else if (strcmp(a->argv[4], "to")) {
		return CLI_SHOWUSAGE;
	} else if ((a->argc >= 8) && strcmp(a->argv[6], "penalty")) {
		return CLI_SHOWUSAGE;
	} else if ((a->argc >= 10) && strcmp(a->argv[8], "as")) {
		return CLI_SHOWUSAGE;
	} else if ((a->argc == 12) && strcmp(a->argv[10], "state_interface")) {
		return CLI_SHOWUSAGE;
	}

	queuename = a->argv[5];
	interface = a->argv[3];
	if (a->argc >= 8) {
		if (sscanf(a->argv[7], "%30d", &penalty) == 1) {
			if (penalty < 0) {
				ast_cli(a->fd, "Penalty must be >= 0\n");
				penalty = 0;
			}
		} else {
			ast_cli(a->fd, "Penalty must be an integer >= 0\n");
			penalty = 0;
		}
	} else {
		penalty = 0;
	}

	if (a->argc >= 10) {
		membername = a->argv[9];
	}

	if (a->argc >= 12) {
		state_interface = a->argv[11];
	}

	switch (add_to_queue(queuename, interface, membername, penalty, 0, queue_persistent_members, state_interface)) {
	case RES_OKAY:
		if (ast_strlen_zero(membername) || !log_membername_as_agent) {
			ast_queue_log(queuename, "CLI", interface, "ADDMEMBER", "%s", "");
		} else {
			ast_queue_log(queuename, "CLI", membername, "ADDMEMBER", "%s", "");
		}
		ast_cli(a->fd, "Added interface '%s' to queue '%s'\n", interface, queuename);
		return CLI_SUCCESS;
	case RES_EXISTS:
		ast_cli(a->fd, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
		return CLI_FAILURE;
	case RES_NOSUCHQUEUE:
		ast_cli(a->fd, "Unable to add interface to queue '%s': No such queue\n", queuename);
		return CLI_FAILURE;
	case RES_OUTOFMEMORY:
		ast_cli(a->fd, "Out of memory\n");
		return CLI_FAILURE;
	case RES_NOT_DYNAMIC:
		ast_cli(a->fd, "Member not dynamic\n");
		return CLI_FAILURE;
	default:
		return CLI_FAILURE;
	}
}

static char *complete_queue_remove_member(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct call_queue *q;
	struct member *m;
	struct ao2_iterator queue_iter;
	struct ao2_iterator mem_iter;
	int wordlen = strlen(word);

	/* 0 - queue; 1 - remove; 2 - member; 3 - <member>; 4 - from; 5 - <queue> */
	if (pos > 5 || pos < 3) {
		return NULL;
	}
	if (pos == 4) {   /* only one possible match, 'from' */
		return (state == 0 ? ast_strdup("from") : NULL);
	}

	if (pos == 5) {   /* No need to duplicate code */
		return complete_queue(line, word, pos, state, 0);
	}

	/* here is the case for 3, <member> */
	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_t_iterator_next(&queue_iter, "Iterate through queues"))) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			if (!strncasecmp(word, m->membername, wordlen) && ++which > state) {
				char *tmp;
				tmp = ast_strdup(m->interface);
				ao2_ref(m, -1);
				ao2_iterator_destroy(&mem_iter);
				ao2_unlock(q);
				queue_t_unref(q, "Done with iterator, returning interface name");
				ao2_iterator_destroy(&queue_iter);
				return tmp;
			}
			ao2_ref(m, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
		queue_t_unref(q, "Done with iterator");
	}
	ao2_iterator_destroy(&queue_iter);

	return NULL;
}

static char *handle_queue_remove_member(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *queuename, *interface;
	struct member *mem = NULL;
	char *res = CLI_FAILURE;

	switch (cmd) {
	case CLI_INIT:
		e->command = "queue remove member";
		e->usage =
			"Usage: queue remove member <channel> from <queue>\n"
			"       Remove a specific channel from a queue.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_queue_remove_member(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 6) {
		return CLI_SHOWUSAGE;
	} else if (strcmp(a->argv[4], "from")) {
		return CLI_SHOWUSAGE;
	}

	queuename = a->argv[5];
	interface = a->argv[3];

	if (log_membername_as_agent) {
		mem = find_member_by_queuename_and_interface(queuename, interface);
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		if (!mem || ast_strlen_zero(mem->membername)) {
			ast_queue_log(queuename, "CLI", interface, "REMOVEMEMBER", "%s", "");
		} else {
			ast_queue_log(queuename, "CLI", mem->membername, "REMOVEMEMBER", "%s", "");
		}
		ast_cli(a->fd, "Removed interface %s from queue '%s'\n", interface, queuename);
		res = CLI_SUCCESS;
		break;
	case RES_EXISTS:
		ast_cli(a->fd, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		break;
	case RES_NOSUCHQUEUE:
		ast_cli(a->fd, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		break;
	case RES_OUTOFMEMORY:
		ast_cli(a->fd, "Out of memory\n");
		break;
	case RES_NOT_DYNAMIC:
		ast_cli(a->fd, "Unable to remove interface '%s' from queue '%s': Member is not dynamic\n", interface, queuename);
		break;
	}

	if (mem) {
		ao2_ref(mem, -1);
	}

	return res;
}

static char *complete_queue_pause_member(const char *line, const char *word, int pos, int state)
{
	/* 0 - queue; 1 - pause; 2 - member; 3 - <interface>; 4 - queue; 5 - <queue>; 6 - reason; 7 - <reason> */
	switch (pos) {
	case 3:	/* Don't attempt to complete name of interface (infinite possibilities) */
		return NULL;
	case 4:	/* only one possible match, "queue" */
		return state == 0 ? ast_strdup("queue") : NULL;
	case 5:	/* <queue> */
		return complete_queue(line, word, pos, state, 0);
	case 6: /* "reason" */
		return state == 0 ? ast_strdup("reason") : NULL;
	case 7: /* Can't autocomplete a reason, since it's 100% customizeable */
		return NULL;
	default:
		return NULL;
	}
}

static char *handle_queue_pause_member(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *queuename, *interface, *reason;
	int paused;

	switch (cmd) {
	case CLI_INIT:
		e->command = "queue {pause|unpause} member";
		e->usage =
			"Usage: queue {pause|unpause} member <member> [queue <queue> [reason <reason>]]\n"
			"	Pause or unpause a queue member. Not specifying a particular queue\n"
			"	will pause or unpause a member across all queues to which the member\n"
			"	belongs.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_queue_pause_member(a->line, a-> word, a->pos, a->n);
	}

	if (a->argc < 4 || a->argc == 5 || a->argc == 7 || a->argc > 8) {
		return CLI_SHOWUSAGE;
	} else if (a->argc >= 5 && strcmp(a->argv[4], "queue")) {
		return CLI_SHOWUSAGE;
	} else if (a->argc == 8 && strcmp(a->argv[6], "reason")) {
		return CLI_SHOWUSAGE;
	}


	interface = a->argv[3];
	queuename = a->argc >= 6 ? a->argv[5] : NULL;
	reason = a->argc == 8 ? a->argv[7] : NULL;
	paused = !strcasecmp(a->argv[1], "pause");

	if (set_member_paused(queuename, interface, reason, paused) == RESULT_SUCCESS) {
		ast_cli(a->fd, "%spaused interface '%s'", paused ? "" : "un", interface);
		if (!ast_strlen_zero(queuename)) {
			ast_cli(a->fd, " in queue '%s'", queuename);
		}
		if (!ast_strlen_zero(reason)) {
			ast_cli(a->fd, " for reason '%s'", reason);
		}
		ast_cli(a->fd, "\n");
		return CLI_SUCCESS;
	} else {
		ast_cli(a->fd, "Unable to %spause interface '%s'", paused ? "" : "un", interface);
		if (!ast_strlen_zero(queuename)) {
			ast_cli(a->fd, " in queue '%s'", queuename);
		}
		if (!ast_strlen_zero(reason)) {
			ast_cli(a->fd, " for reason '%s'", reason);
		}
		ast_cli(a->fd, "\n");
		return CLI_FAILURE;
	}
}

static char *complete_queue_set_member_value(const char *line, const char *word, int pos, int state)
{
	/* 0 - queue; 1 - set; 2 - penalty/ringinuse; 3 - <value>; 4 - on; 5 - <member>; 6 - in; 7 - <queue>;*/
	switch (pos) {
	case 4:
		if (state == 0) {
			return ast_strdup("on");
		} else {
			return NULL;
		}
	case 6:
		if (state == 0) {
			return ast_strdup("in");
		} else {
			return NULL;
		}
	case 7:
		return complete_queue(line, word, pos, state, 0);
	default:
		return NULL;
	}
}

static char *handle_queue_set_member_ringinuse(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *queuename = NULL, *interface;
	int ringinuse;

	switch (cmd) {
	case CLI_INIT:
		e->command = "queue set ringinuse";
		e->usage =
		"Usage: queue set ringinuse <yes/no> on <interface> [in <queue>]\n"
		"	Set a member's ringinuse in the queue specified. If no queue is specified\n"
		"	then that interface's penalty is set in all queues to which that interface is a member.\n";
		break;
		return NULL;
	case CLI_GENERATE:
		return complete_queue_set_member_value(a->line, a->word, a->pos, a->n);
	}

	/* Sensible argument counts */
	if (a->argc != 6 && a->argc != 8) {
		return CLI_SHOWUSAGE;
	}

	/* Uses proper indicational words */
	if (strcmp(a->argv[4], "on") || (a->argc > 6 && strcmp(a->argv[6], "in"))) {
		return CLI_SHOWUSAGE;
	}

	/* Set the queue name if applicale */
	if (a->argc == 8) {
		queuename = a->argv[7];
	}

	/* Interface being set */
	interface = a->argv[5];

	/* Check and set the ringinuse value */
	if (ast_true(a->argv[3])) {
		ringinuse = 1;
	} else if (ast_false(a->argv[3])) {
		ringinuse = 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	switch (set_member_value(queuename, interface, MEMBER_RINGINUSE, ringinuse)) {
	case RESULT_SUCCESS:
		ast_cli(a->fd, "Set ringinuse on interface '%s' from queue '%s'\n", interface, queuename);
		return CLI_SUCCESS;
	case RESULT_FAILURE:
		ast_cli(a->fd, "Failed to set ringinuse on interface '%s' from queue '%s'\n", interface, queuename);
		return CLI_FAILURE;
	default:
		return CLI_FAILURE;
	}
}

static char *handle_queue_set_member_penalty(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *queuename = NULL, *interface;
	int penalty = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "queue set penalty";
		e->usage =
		"Usage: queue set penalty <penalty> on <interface> [in <queue>]\n"
		"	Set a member's penalty in the queue specified. If no queue is specified\n"
		"	then that interface's penalty is set in all queues to which that interface is a member\n";
		return NULL;
	case CLI_GENERATE:
		return complete_queue_set_member_value(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 6 && a->argc != 8) {
		return CLI_SHOWUSAGE;
	} else if (strcmp(a->argv[4], "on") || (a->argc > 6 && strcmp(a->argv[6], "in"))) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 8) {
		queuename = a->argv[7];
	}
	interface = a->argv[5];
	penalty = atoi(a->argv[3]);

	switch (set_member_value(queuename, interface, MEMBER_PENALTY, penalty)) {
	case RESULT_SUCCESS:
		ast_cli(a->fd, "Set penalty on interface '%s' from queue '%s'\n", interface, queuename);
		return CLI_SUCCESS;
	case RESULT_FAILURE:
		ast_cli(a->fd, "Failed to set penalty on interface '%s' from queue '%s'\n", interface, queuename);
		return CLI_FAILURE;
	default:
		return CLI_FAILURE;
	}
}

static char *complete_queue_rule_show(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct rule_list *rl_iter;
	int wordlen = strlen(word);
	char *ret = NULL;
	if (pos != 3) /* Wha? */ {
		return NULL;
	}

	AST_LIST_LOCK(&rule_lists);
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list) {
		if (!strncasecmp(word, rl_iter->name, wordlen) && ++which > state) {
			ret = ast_strdup(rl_iter->name);
			break;
		}
	}
	AST_LIST_UNLOCK(&rule_lists);

	return ret;
}

static char *handle_queue_rule_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *rule;
	struct rule_list *rl_iter;
	struct penalty_rule *pr_iter;
	switch (cmd) {
	case CLI_INIT:
		e->command = "queue show rules";
		e->usage =
		"Usage: queue show rules [rulename]\n"
		"	Show the list of rules associated with rulename. If no\n"
		"	rulename is specified, list all rules defined in queuerules.conf\n";
		return NULL;
	case CLI_GENERATE:
		return complete_queue_rule_show(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 3 && a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	rule = a->argc == 4 ? a->argv[3] : "";
	AST_LIST_LOCK(&rule_lists);
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list) {
		if (ast_strlen_zero(rule) || !strcasecmp(rl_iter->name, rule)) {
			ast_cli(a->fd, "Rule: %s\n", rl_iter->name);
			AST_LIST_TRAVERSE(&rl_iter->rules, pr_iter, list) {
				ast_cli(a->fd, "\tAfter %d seconds, adjust QUEUE_MAX_PENALTY %s %d and adjust QUEUE_MIN_PENALTY %s %d\n", pr_iter->time, pr_iter->max_relative ? "by" : "to", pr_iter->max_value, pr_iter->min_relative ? "by" : "to", pr_iter->min_value);
			}
		}
	}
	AST_LIST_UNLOCK(&rule_lists);
	return CLI_SUCCESS;
}

static char *handle_queue_reset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_flags mask = {QUEUE_RESET_STATS,};
	int i;

	switch (cmd) {
		case CLI_INIT:
			e->command = "queue reset stats";
			e->usage =
				"Usage: queue reset stats [<queuenames>]\n"
				"\n"
				"Issuing this command will reset statistics for\n"
				"<queuenames>, or for all queues if no queue is\n"
				"specified.\n";
			return NULL;
		case CLI_GENERATE:
			if (a->pos >= 3) {
				return complete_queue(a->line, a->word, a->pos, a->n, 17);
			} else {
				return NULL;
			}
	}

	if (a->argc < 3) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 3) {
		reload_handler(1, &mask, NULL);
		return CLI_SUCCESS;
	}

	for (i = 3; i < a->argc; ++i) {
		reload_handler(1, &mask, a->argv[i]);
	}

	return CLI_SUCCESS;
}

static char *handle_queue_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_flags mask = {0,};
	int i;

	switch (cmd) {
		case CLI_INIT:
			e->command = "queue reload {parameters|members|rules|all}";
			e->usage =
				"Usage: queue reload {parameters|members|rules|all} [<queuenames>]\n"
				"Reload queues. If <queuenames> are specified, only reload information pertaining\n"
				"to <queuenames>. One of 'parameters,' 'members,' 'rules,' or 'all' must be\n"
				"specified in order to know what information to reload. Below is an explanation\n"
				"of each of these qualifiers.\n"
				"\n"
				"\t'members' - reload queue members from queues.conf\n"
				"\t'parameters' - reload all queue options except for queue members\n"
				"\t'rules' - reload the queuerules.conf file\n"
				"\t'all' - reload queue rules, parameters, and members\n"
				"\n"
				"Note: the 'rules' qualifier here cannot actually be applied to a specific queue.\n"
				"Use of the 'rules' qualifier causes queuerules.conf to be reloaded. Even if only\n"
				"one queue is specified when using this command, reloading queue rules may cause\n"
				"other queues to be affected\n";
			return NULL;
		case CLI_GENERATE:
			if (a->pos >= 3) {
				/* find the point at which the list of queue names starts */
				const char *command_end = a->line + strlen("queue reload ");
				command_end = strchr(command_end, ' ');
				if (!command_end) {
					command_end = a->line + strlen(a->line);
				}
				return complete_queue(a->line, a->word, a->pos, a->n, command_end - a->line);
			} else {
				return NULL;
			}
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	if (!strcasecmp(a->argv[2], "rules")) {
		ast_set_flag(&mask, QUEUE_RELOAD_RULES);
	} else if (!strcasecmp(a->argv[2], "members")) {
		ast_set_flag(&mask, QUEUE_RELOAD_MEMBER);
	} else if (!strcasecmp(a->argv[2], "parameters")) {
		ast_set_flag(&mask, QUEUE_RELOAD_PARAMETERS);
	} else if (!strcasecmp(a->argv[2], "all")) {
		ast_set_flag(&mask, AST_FLAGS_ALL);
	}

	if (a->argc == 3) {
		reload_handler(1, &mask, NULL);
		return CLI_SUCCESS;
	}

	for (i = 3; i < a->argc; ++i) {
		reload_handler(1, &mask, a->argv[i]);
	}

	return CLI_SUCCESS;
}

static const char qpm_cmd_usage[] =
"Usage: queue pause member <channel> in <queue> reason <reason>\n";

static const char qum_cmd_usage[] =
"Usage: queue unpause member <channel> in <queue> reason <reason>\n";

static const char qsmp_cmd_usage[] =
"Usage: queue set member penalty <channel> from <queue> <penalty>\n";

static struct ast_cli_entry cli_queue[] = {
	AST_CLI_DEFINE(queue_show, "Show status of a specified queue"),
	AST_CLI_DEFINE(handle_queue_rule_show, "Show the rules defined in queuerules.conf"),
	AST_CLI_DEFINE(handle_queue_add_member, "Add a channel to a specified queue"),
	AST_CLI_DEFINE(handle_queue_remove_member, "Removes a channel from a specified queue"),
	AST_CLI_DEFINE(handle_queue_pause_member, "Pause or unpause a queue member"),
	AST_CLI_DEFINE(handle_queue_set_member_penalty, "Set penalty for a channel of a specified queue"),
	AST_CLI_DEFINE(handle_queue_set_member_ringinuse, "Set ringinuse for a channel of a specified queue"),
	AST_CLI_DEFINE(handle_queue_reload, "Reload queues, members, queue rules, or parameters"),
	AST_CLI_DEFINE(handle_queue_reset, "Reset statistics for a queue"),
};

/* struct call_queue astdata mapping. */
#define DATA_EXPORT_CALL_QUEUE(MEMBER)					\
	MEMBER(call_queue, name, AST_DATA_STRING)			\
	MEMBER(call_queue, moh, AST_DATA_STRING)			\
	MEMBER(call_queue, announce, AST_DATA_STRING)			\
	MEMBER(call_queue, context, AST_DATA_STRING)			\
	MEMBER(call_queue, membermacro, AST_DATA_STRING)		\
	MEMBER(call_queue, membergosub, AST_DATA_STRING)		\
	MEMBER(call_queue, defaultrule, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_next, AST_DATA_STRING)			\
	MEMBER(call_queue, sound_thereare, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_calls, AST_DATA_STRING)		\
	MEMBER(call_queue, queue_quantity1, AST_DATA_STRING)		\
	MEMBER(call_queue, queue_quantity2, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_holdtime, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_minutes, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_minute, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_seconds, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_thanks, AST_DATA_STRING)		\
	MEMBER(call_queue, sound_callerannounce, AST_DATA_STRING)	\
	MEMBER(call_queue, sound_reporthold, AST_DATA_STRING)		\
	MEMBER(call_queue, dead, AST_DATA_BOOLEAN)			\
	MEMBER(call_queue, ringinuse, AST_DATA_BOOLEAN)			\
	MEMBER(call_queue, announce_to_first_user, AST_DATA_BOOLEAN)	\
	MEMBER(call_queue, setinterfacevar, AST_DATA_BOOLEAN)		\
	MEMBER(call_queue, setqueuevar, AST_DATA_BOOLEAN)		\
	MEMBER(call_queue, setqueueentryvar, AST_DATA_BOOLEAN)		\
	MEMBER(call_queue, reportholdtime, AST_DATA_BOOLEAN)		\
	MEMBER(call_queue, wrapped, AST_DATA_BOOLEAN)			\
	MEMBER(call_queue, timeoutrestart, AST_DATA_BOOLEAN)		\
	MEMBER(call_queue, announceholdtime, AST_DATA_INTEGER)		\
	MEMBER(call_queue, realtime, AST_DATA_BOOLEAN)			\
	MEMBER(call_queue, found, AST_DATA_BOOLEAN)			\
	MEMBER(call_queue, announcepositionlimit, AST_DATA_INTEGER)	\
	MEMBER(call_queue, announcefrequency, AST_DATA_SECONDS)		\
	MEMBER(call_queue, minannouncefrequency, AST_DATA_SECONDS)	\
	MEMBER(call_queue, periodicannouncefrequency, AST_DATA_SECONDS)	\
	MEMBER(call_queue, numperiodicannounce, AST_DATA_INTEGER)	\
	MEMBER(call_queue, randomperiodicannounce, AST_DATA_INTEGER)	\
	MEMBER(call_queue, roundingseconds, AST_DATA_SECONDS)		\
	MEMBER(call_queue, holdtime, AST_DATA_SECONDS)			\
	MEMBER(call_queue, talktime, AST_DATA_SECONDS)			\
	MEMBER(call_queue, callscompleted, AST_DATA_INTEGER)		\
	MEMBER(call_queue, callsabandoned, AST_DATA_INTEGER)		\
	MEMBER(call_queue, servicelevel, AST_DATA_INTEGER)		\
	MEMBER(call_queue, callscompletedinsl, AST_DATA_INTEGER)	\
	MEMBER(call_queue, monfmt, AST_DATA_STRING)			\
	MEMBER(call_queue, montype, AST_DATA_INTEGER)			\
	MEMBER(call_queue, count, AST_DATA_INTEGER)			\
	MEMBER(call_queue, maxlen, AST_DATA_INTEGER)			\
	MEMBER(call_queue, wrapuptime, AST_DATA_SECONDS)		\
	MEMBER(call_queue, retry, AST_DATA_SECONDS)			\
	MEMBER(call_queue, timeout, AST_DATA_SECONDS)			\
	MEMBER(call_queue, weight, AST_DATA_INTEGER)			\
	MEMBER(call_queue, autopause, AST_DATA_INTEGER)			\
	MEMBER(call_queue, timeoutpriority, AST_DATA_INTEGER)		\
	MEMBER(call_queue, rrpos, AST_DATA_INTEGER)			\
	MEMBER(call_queue, memberdelay, AST_DATA_INTEGER)		\
	MEMBER(call_queue, autofill, AST_DATA_INTEGER)			\
	MEMBER(call_queue, members, AST_DATA_CONTAINER)

AST_DATA_STRUCTURE(call_queue, DATA_EXPORT_CALL_QUEUE);

/* struct member astdata mapping. */
#define DATA_EXPORT_MEMBER(MEMBER)					\
	MEMBER(member, interface, AST_DATA_STRING)			\
	MEMBER(member, state_interface, AST_DATA_STRING)		\
	MEMBER(member, membername, AST_DATA_STRING)			\
	MEMBER(member, penalty, AST_DATA_INTEGER)			\
	MEMBER(member, calls, AST_DATA_INTEGER)				\
	MEMBER(member, dynamic, AST_DATA_INTEGER)			\
	MEMBER(member, realtime, AST_DATA_INTEGER)			\
	MEMBER(member, status, AST_DATA_INTEGER)			\
	MEMBER(member, paused, AST_DATA_BOOLEAN)			\
	MEMBER(member, rt_uniqueid, AST_DATA_STRING)

AST_DATA_STRUCTURE(member, DATA_EXPORT_MEMBER);

#define DATA_EXPORT_QUEUE_ENT(MEMBER)						\
	MEMBER(queue_ent, moh, AST_DATA_STRING)					\
	MEMBER(queue_ent, announce, AST_DATA_STRING)				\
	MEMBER(queue_ent, context, AST_DATA_STRING)				\
	MEMBER(queue_ent, digits, AST_DATA_STRING)				\
	MEMBER(queue_ent, valid_digits, AST_DATA_INTEGER)			\
	MEMBER(queue_ent, pos, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, prio, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, last_pos_said, AST_DATA_INTEGER)			\
	MEMBER(queue_ent, last_periodic_announce_time, AST_DATA_INTEGER)	\
	MEMBER(queue_ent, last_periodic_announce_sound, AST_DATA_INTEGER)	\
	MEMBER(queue_ent, last_pos, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, opos, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, handled, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, pending, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, max_penalty, AST_DATA_INTEGER)			\
	MEMBER(queue_ent, min_penalty, AST_DATA_INTEGER)			\
	MEMBER(queue_ent, linpos, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, linwrapped, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, start, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, expire, AST_DATA_INTEGER)				\
	MEMBER(queue_ent, cancel_answered_elsewhere, AST_DATA_INTEGER)

AST_DATA_STRUCTURE(queue_ent, DATA_EXPORT_QUEUE_ENT);

/*!
 * \internal
 * \brief Add a queue to the data_root node.
 * \param[in] search The search tree.
 * \param[in] data_root The main result node.
 * \param[in] queue The queue to add.
 */
static void queues_data_provider_get_helper(const struct ast_data_search *search,
	struct ast_data *data_root, struct call_queue *queue)
{
	struct ao2_iterator im;
	struct member *member;
	struct queue_ent *qe;
	struct ast_data *data_queue, *data_members = NULL, *enum_node;
	struct ast_data *data_member, *data_callers = NULL, *data_caller, *data_caller_channel;

	data_queue = ast_data_add_node(data_root, "queue");
	if (!data_queue) {
		return;
	}

	ast_data_add_structure(call_queue, data_queue, queue);

	ast_data_add_str(data_queue, "strategy", int2strat(queue->strategy));
	ast_data_add_int(data_queue, "membercount", ao2_container_count(queue->members));

	/* announce position */
	enum_node = ast_data_add_node(data_queue, "announceposition");
	if (!enum_node) {
		return;
	}
	switch (queue->announceposition) {
	case ANNOUNCEPOSITION_LIMIT:
		ast_data_add_str(enum_node, "text", "limit");
		break;
	case ANNOUNCEPOSITION_MORE_THAN:
		ast_data_add_str(enum_node, "text", "more");
		break;
	case ANNOUNCEPOSITION_YES:
		ast_data_add_str(enum_node, "text", "yes");
		break;
	case ANNOUNCEPOSITION_NO:
		ast_data_add_str(enum_node, "text", "no");
		break;
	default:
		ast_data_add_str(enum_node, "text", "unknown");
		break;
	}
	ast_data_add_int(enum_node, "value", queue->announceposition);

	/* add queue members */
	im = ao2_iterator_init(queue->members, 0);
	while ((member = ao2_iterator_next(&im))) {
		if (!data_members) {
			data_members = ast_data_add_node(data_queue, "members");
			if (!data_members) {
				ao2_ref(member, -1);
				continue;
			}
		}

		data_member = ast_data_add_node(data_members, "member");
		if (!data_member) {
			ao2_ref(member, -1);
			continue;
		}

		ast_data_add_structure(member, data_member, member);

		ao2_ref(member, -1);
	}
	ao2_iterator_destroy(&im);

	/* include the callers inside the result. */
	if (queue->head) {
		for (qe = queue->head; qe; qe = qe->next) {
			if (!data_callers) {
				data_callers = ast_data_add_node(data_queue, "callers");
				if (!data_callers) {
					continue;
				}
			}

			data_caller = ast_data_add_node(data_callers, "caller");
			if (!data_caller) {
				continue;
			}

			ast_data_add_structure(queue_ent, data_caller, qe);

			/* add the caller channel. */
			data_caller_channel = ast_data_add_node(data_caller, "channel");
			if (!data_caller_channel) {
				continue;
			}

			ast_channel_data_add_structure(data_caller_channel, qe->chan, 1);
		}
	}

	/* if this queue doesn't match remove the added queue. */
	if (!ast_data_search_match(search, data_queue)) {
		ast_data_remove_node(data_root, data_queue);
	}
}

/*!
 * \internal
 * \brief Callback used to generate the queues tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int queues_data_provider_get(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct ao2_iterator i;
	struct call_queue *queue, *queue_realtime = NULL;
	struct ast_config *cfg;
	char *queuename;

	/* load realtime queues. */
	cfg = ast_load_realtime_multientry("queues", "name LIKE", "%", SENTINEL);
	if (cfg) {
		for (queuename = ast_category_browse(cfg, NULL);
				!ast_strlen_zero(queuename);
				queuename = ast_category_browse(cfg, queuename)) {
			if ((queue = find_load_queue_rt_friendly(queuename))) {
				queue_unref(queue);
			}
		}
		ast_config_destroy(cfg);
	}

	/* static queues. */
	i = ao2_iterator_init(queues, 0);
	while ((queue = ao2_iterator_next(&i))) {
		ao2_lock(queue);
		if (queue->realtime) {
			queue_realtime = find_load_queue_rt_friendly(queue->name);
			if (!queue_realtime) {
				ao2_unlock(queue);
				queue_unref(queue);
				continue;
			}
			queue_unref(queue_realtime);
		}

		queues_data_provider_get_helper(search, data_root, queue);
		ao2_unlock(queue);
		queue_unref(queue);
	}
	ao2_iterator_destroy(&i);

	return 0;
}

static const struct ast_data_handler queues_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = queues_data_provider_get
};

static const struct ast_data_entry queue_data_providers[] = {
	AST_DATA_ENTRY("asterisk/application/queue/list", &queues_data_provider),
};

static struct stasis_message_router *agent_router;
static struct stasis_forward *topic_forwarder;

static int unload_module(void)
{
	stasis_message_router_unsubscribe_and_join(agent_router);
	agent_router = NULL;

	topic_forwarder = stasis_forward_cancel(topic_forwarder);

	STASIS_MESSAGE_TYPE_CLEANUP(queue_caller_join_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_caller_leave_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_caller_abandon_type);

	STASIS_MESSAGE_TYPE_CLEANUP(queue_member_status_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_member_added_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_member_removed_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_member_pause_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_member_penalty_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_member_ringinuse_type);

	STASIS_MESSAGE_TYPE_CLEANUP(queue_agent_called_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_agent_connect_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_agent_complete_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_agent_dump_type);
	STASIS_MESSAGE_TYPE_CLEANUP(queue_agent_ringnoanswer_type);

	ast_cli_unregister_multiple(cli_queue, ARRAY_LEN(cli_queue));
	ast_manager_unregister("QueueStatus");
	ast_manager_unregister("Queues");
	ast_manager_unregister("QueueRule");
	ast_manager_unregister("QueueSummary");
	ast_manager_unregister("QueueAdd");
	ast_manager_unregister("QueueRemove");
	ast_manager_unregister("QueuePause");
	ast_manager_unregister("QueueLog");
	ast_manager_unregister("QueuePenalty");
	ast_manager_unregister("QueueReload");
	ast_manager_unregister("QueueReset");
	ast_manager_unregister("QueueMemberRingInUse");
	ast_unregister_application(app_aqm);
	ast_unregister_application(app_rqm);
	ast_unregister_application(app_pqm);
	ast_unregister_application(app_upqm);
	ast_unregister_application(app_ql);
	ast_unregister_application(app);
	ast_custom_function_unregister(&queueexists_function);
	ast_custom_function_unregister(&queuevar_function);
	ast_custom_function_unregister(&queuemembercount_function);
	ast_custom_function_unregister(&queuemembercount_dep);
	ast_custom_function_unregister(&queuememberlist_function);
	ast_custom_function_unregister(&queuegetchannel_function);
	ast_custom_function_unregister(&queuewaitingcount_function);
	ast_custom_function_unregister(&queuememberpenalty_function);

	ast_data_unregister(NULL);

	device_state_sub = stasis_unsubscribe_and_join(device_state_sub);

	ast_extension_state_del(0, extension_state_cb);

	ast_unload_realtime("queue_members");
	ao2_cleanup(queues);
	queues = NULL;
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
	int err = 0;
	struct ast_flags mask = {AST_FLAGS_ALL, };
	struct ast_config *member_config;
	struct stasis_topic *queue_topic;
	struct stasis_topic *manager_topic;

	queues = ao2_container_alloc(MAX_QUEUE_BUCKETS, queue_hash_cb, queue_cmp_cb);
	if (!queues) {
		return AST_MODULE_LOAD_DECLINE;
	}

	use_weight = 0;

	if (reload_handler(0, &mask, NULL)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_realtime_require_field("queue_members", "paused", RQ_INTEGER1, 1, "uniqueid", RQ_UINTEGER2, 5, SENTINEL);

	/*
	 * This section is used to determine which name for 'ringinuse' to use in realtime members
	 * Necessary for supporting older setups.
	 */
	member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name LIKE", "%", SENTINEL);
	if (!member_config) {
		realtime_ringinuse_field = "ringinuse";
	} else {
		const char *config_val;

		if ((config_val = ast_variable_retrieve(member_config, NULL, "ringinuse"))) {
			ast_log(LOG_NOTICE, "ringinuse field entries found in queue_members table. Using 'ringinuse'\n");
			realtime_ringinuse_field = "ringinuse";
		} else if ((config_val = ast_variable_retrieve(member_config, NULL, "ignorebusy"))) {
			ast_log(LOG_NOTICE, "ignorebusy field found in queue_members table with no ringinuse field. Using 'ignorebusy'\n");
			realtime_ringinuse_field = "ignorebusy";
		} else {
			ast_log(LOG_NOTICE, "No entries were found for ringinuse/ignorebusy in queue_members table. Using 'ringinuse'\n");
			realtime_ringinuse_field = "ringinuse";
		}
	}
	ast_config_destroy(member_config);

	if (queue_persistent_members) {
		reload_queue_members();
	}

	ast_data_register_multiple(queue_data_providers, ARRAY_LEN(queue_data_providers));

	err |= ast_cli_register_multiple(cli_queue, ARRAY_LEN(cli_queue));
	err |= ast_register_application_xml(app, queue_exec);
	err |= ast_register_application_xml(app_aqm, aqm_exec);
	err |= ast_register_application_xml(app_rqm, rqm_exec);
	err |= ast_register_application_xml(app_pqm, pqm_exec);
	err |= ast_register_application_xml(app_upqm, upqm_exec);
	err |= ast_register_application_xml(app_ql, ql_exec);
	err |= ast_manager_register_xml("Queues", 0, manager_queues_show);
	err |= ast_manager_register_xml("QueueStatus", 0, manager_queues_status);
	err |= ast_manager_register_xml("QueueSummary", 0, manager_queues_summary);
	err |= ast_manager_register_xml("QueueAdd", EVENT_FLAG_AGENT, manager_add_queue_member);
	err |= ast_manager_register_xml("QueueRemove", EVENT_FLAG_AGENT, manager_remove_queue_member);
	err |= ast_manager_register_xml("QueuePause", EVENT_FLAG_AGENT, manager_pause_queue_member);
	err |= ast_manager_register_xml("QueueLog", EVENT_FLAG_AGENT, manager_queue_log_custom);
	err |= ast_manager_register_xml("QueuePenalty", EVENT_FLAG_AGENT, manager_queue_member_penalty);
	err |= ast_manager_register_xml("QueueMemberRingInUse", EVENT_FLAG_AGENT, manager_queue_member_ringinuse);
	err |= ast_manager_register_xml("QueueRule", 0, manager_queue_rule_show);
	err |= ast_manager_register_xml("QueueReload", 0, manager_queue_reload);
	err |= ast_manager_register_xml("QueueReset", 0, manager_queue_reset);
	err |= ast_custom_function_register(&queuevar_function);
	err |= ast_custom_function_register(&queueexists_function);
	err |= ast_custom_function_register(&queuemembercount_function);
	err |= ast_custom_function_register(&queuemembercount_dep);
	err |= ast_custom_function_register(&queuememberlist_function);
	err |= ast_custom_function_register(&queuegetchannel_function);
	err |= ast_custom_function_register(&queuewaitingcount_function);
	err |= ast_custom_function_register(&queuememberpenalty_function);

	/* in the following subscribe call, do I use DEVICE_STATE, or DEVICE_STATE_CHANGE? */
	device_state_sub = stasis_subscribe(ast_device_state_topic_all(), device_state_cb, NULL);
	if (!device_state_sub) {
		err = -1;
	}

	manager_topic = ast_manager_get_topic();
	queue_topic = ast_queue_topic_all();
	if (!manager_topic || !queue_topic) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	topic_forwarder = stasis_forward_all(queue_topic, manager_topic);
	if (!topic_forwarder) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!ast_channel_agent_login_type()
		|| !ast_channel_agent_logoff_type()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	agent_router = stasis_message_router_create(ast_channel_topic_all());
	if (!agent_router) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	err |= stasis_message_router_add(agent_router,
		ast_channel_agent_login_type(),
		queue_agent_cb,
		NULL);
	err |= stasis_message_router_add(agent_router,
		ast_channel_agent_logoff_type(),
		queue_agent_cb,
		NULL);

	err |= STASIS_MESSAGE_TYPE_INIT(queue_caller_join_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_caller_leave_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_caller_abandon_type);

	err |= STASIS_MESSAGE_TYPE_INIT(queue_member_status_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_member_added_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_member_removed_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_member_pause_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_member_penalty_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_member_ringinuse_type);

	err |= STASIS_MESSAGE_TYPE_INIT(queue_agent_called_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_agent_connect_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_agent_complete_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_agent_dump_type);
	err |= STASIS_MESSAGE_TYPE_INIT(queue_agent_ringnoanswer_type);

	ast_extension_state_add(NULL, NULL, extension_state_cb, NULL);

	if (err) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	struct ast_flags mask = {AST_FLAGS_ALL & ~QUEUE_RESET_STATS,};
	ast_unload_realtime("queue_members");
	reload_handler(1, &mask, NULL);
	return 0;
}

/* \brief Find a member by looking up queuename and interface.
 * \return Returns a member or NULL if member not found.
*/
static struct member *find_member_by_queuename_and_interface(const char *queuename, const char *interface)
{
	struct member *mem = NULL;
	struct call_queue *q;

	if ((q = find_load_queue_rt_friendly(queuename))) {
		ao2_lock(q);
		mem = ao2_find(q->members, interface, OBJ_KEY);
		ao2_unlock(q);
		queue_t_unref(q, "Expiring temporary reference.");
	}
	return mem;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "True Call Queueing",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_DEVSTATE_CONSUMER,
		.nonoptreq = "res_monitor",
	       );

