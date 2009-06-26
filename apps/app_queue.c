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
 * \arg Config in \ref Config_qu queues.conf
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

/*** MODULEINFO
	<depend>res_monitor</depend>
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
#include "asterisk/event.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/global_datastores.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/callerid.h"
#include "asterisk/cel.h"

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
				<para>Will run a macro on the calling party's channel once they are connected to a queue member.</para>
			</parameter>
			<parameter name="gosub">
				<para>Will run a gosub on the calling party's channel once they are connected to a queue member.</para>
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
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="application">AgentLogin</ref>
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
			<ref type="function">QUEUE_MEMBER_LIST</ref>
			<ref type="function">QUEUE_WAITING_COUNT</ref>
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
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="application">AgentLogin</ref>
		</see-also>
	</application>
	<application name="RemoveQueueMember" language="en_US">
		<synopsis>
			Dynamically removes queue members.
		</synopsis>
		<syntax>
			<parameter name="queuename" required="true" />
			<parameter name="interface" />
			<parameter name="options" />
		</syntax>
		<description>
			<para>If the interface is <emphasis>NOT</emphasis> in the queue it will return an error.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="RQMSTATUS">
					<value name="REMOVED" />
					<value name="NOTINQUEUE" />
					<value name="NOSUCHQUEUE" />
				</variable>
			</variablelist>
			<para>Example: RemoveQueueMember(techsupport,SIP/3000)</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
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
			<ref type="application">UnpauseQueueMember</ref>
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
			<ref type="application">PauseQueueMember</ref>
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
						<para>Returns the number of logged-in members for the specified queue available to take a call.</para>
					</enum>
					<enum name="count">
						<para>Returns the total number of members for the specified queue.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Returns the number of members currently associated with the specified <replaceable>queuename</replaceable>.</para>
		</description>
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
			<ref type="function">QUEUE_MEMBER_LIST</ref>
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
			<ref type="function">QUEUE_MEMBER_COUNT</ref>
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
		</description>
	</function>
	<manager name="Queues" language="en_US">
		<synopsis>
			Queues.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueStatus" language="en_US">
		<synopsis>
			Show queue status.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" />
			<parameter name="Member" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueSummary" language="en_US">
		<synopsis>
			Show queue summary.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueAdd" language="en_US">
		<synopsis>
			Add interface to queue.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" required="true" />
			<parameter name="Interface" required="true" />
			<parameter name="Penalty" />
			<parameter name="Paused" />
			<parameter name="MemberName" />
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
			<parameter name="Queue" required="true" />
			<parameter name="Interface" required="true" />
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
			<parameter name="Interface" required="true" />
			<parameter name="Paused" required="true" />
			<parameter name="Queue" />
			<parameter name="Reason" />
		</syntax>
		<description>
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
			<parameter name="Interface" required="true" />
			<parameter name="Penalty" required="true" />
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
			<parameter name="Rule" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="QueueReload" language="en_US">
		<synopsis>
			Reload a queue, queues, or any sub-section of a queue or queues.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Queue" />
			<parameter name="Members">
				<enumlist>
					<enum name="yes" />
					<enum name="no" />
				</enumlist>
			</parameter>
			<parameter name="Rules">
				<enumlist>
					<enum name="yes" />
					<enum name="no" />
				</enumlist>
			</parameter>
			<parameter name="Parameters">
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
			<parameter name="Queue" />
		</syntax>
		<description>
		</description>
	</manager>
 ***/

enum {
	QUEUE_STRATEGY_RINGALL = 0,
	QUEUE_STRATEGY_LEASTRECENT,
	QUEUE_STRATEGY_FEWESTCALLS,
	QUEUE_STRATEGY_RANDOM,
	QUEUE_STRATEGY_RRMEMORY,
	QUEUE_STRATEGY_LINEAR,
	QUEUE_STRATEGY_WRANDOM
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
};

static struct ast_taskprocessor *devicestate_tps;

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK			1		/*!< Recheck every second to see we we're at the top yet */
#define MAX_PERIODIC_ANNOUNCEMENTS 10           /*!< The maximum periodic announcements we can have */
#define DEFAULT_MIN_ANNOUNCE_FREQUENCY 15       /*!< The minimum number of seconds between position announcements \
                                                     The default value of 15 provides backwards compatibility */
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
/* The maximum length of each persistent member queue database entry */
#define PM_MAX_LEN 8192

/*! \brief queues.conf [general] option */
static int queue_persistent_members = 0;

/*! \brief queues.conf per-queue weight option */
static int use_weight = 0;

/*! \brief queues.conf [general] option */
static int autofill_default = 0;

/*! \brief queues.conf [general] option */
static int montype_default = 0;

/*! \brief queues.conf [general] option */
static int shared_lastcall = 0;

/*! \brief Subscription to device state change events */
static struct ast_event_sub *device_state_sub;

/*! \brief queues.conf [general] option */
static int update_cdr = 0;

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
	char interface[256];
	int stillgoing;
	int metric;
	int oldstatus;
	time_t lastcall;
	struct call_queue *lastqueue;
	struct member *member;
	unsigned int update_connectedline:1;
	struct ast_party_connected_line connected;
};


struct queue_ent {
	struct call_queue *parent;             /*!< What queue is our parent */
	char moh[80];                          /*!< Name of musiconhold to be used */
	char announce[80];                     /*!< Announcement to play for member when call is answered */
	char context[AST_MAX_CONTEXT];         /*!< Context when user exits queue */
	char digits[AST_MAX_EXTENSION];        /*!< Digits entered while in queue */
	int valid_digits;                      /*!< Digits entered correspond to valid extension. Exited */
	int pos;                               /*!< Where we are in the queue */
	int prio;                              /*!< Our priority */
	int last_pos_said;                     /*!< Last position we told the user */
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
	char interface[80];                 /*!< Technology/Location to dial to reach this member*/
	char state_interface[80];           /*!< Technology/Location from which to read devicestate changes */
	char membername[80];                /*!< Member name to use in queue logs */
	int penalty;                        /*!< Are we a last resort? */
	int calls;                          /*!< Number of calls serviced by this member */
	int dynamic;                        /*!< Are we dynamically added? */
	int realtime;                       /*!< Is this member realtime? */
	int status;                         /*!< Status of queue member */
	int paused;                         /*!< Are we paused (not accepting calls)? */
	time_t lastcall;                    /*!< When last successful call was hungup */
	struct call_queue *lastqueue;	    /*!< Last queue we received a call */
	unsigned int dead:1;                /*!< Used to detect members deleted in realtime */
	unsigned int delme:1;               /*!< Flag to delete entry on reload */
	char rt_uniqueid[80];               /*!< Unique id of realtime member entry */
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
	unsigned int eventwhencalled:2;
	unsigned int ringinuse:1;
	unsigned int setinterfacevar:1;
	unsigned int setqueuevar:1;
	unsigned int setqueueentryvar:1;
	unsigned int reportholdtime:1;
	unsigned int wrapped:1;
	unsigned int timeoutrestart:1;
	unsigned int announceholdtime:2;
	unsigned int announceposition:3;
	int strategy:4;
	unsigned int maskmemberstatus:1;
	unsigned int realtime:1;
	unsigned int found:1;
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

	int retry;                          /*!< Retry calling everyone after this amount of time */
	int timeout;                        /*!< How long to wait for an answer */
	int weight;                         /*!< Respective weight */
	int autopause;                      /*!< Auto pause queue members if they fail to answer */
	int timeoutpriority;                /*!< Do we allow a fraction of the timeout to occur for a ring? */

	/* Queue strategy things */
	int rrpos;                          /*!< Round Robin - position */
	int memberdelay;                    /*!< Seconds to delay connecting member to caller */
	int autofill;                       /*!< Ignore the head call status and ring an available agent */
	
	struct ao2_container *members;             /*!< Head of the list of members */
	/*! 
	 * \brief Number of members _logged in_
	 * \note There will be members in the members container that are not logged
	 *       in, so this can not simply be replaced with ao2_container_count(). 
	 */
	int membercount;
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
static int set_member_paused(const char *queuename, const char *interface, const char *reason, int paused);

static void queue_transfer_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan); 
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
		if (strategy == strategies[x].strategy)
			return strategies[x].name;
	}

	return "<unknown>";
}

static int strat2int(const char *strategy)
{
	int x;

	for (x = 0; x < ARRAY_LEN(strategies); x++) {
		if (!strcasecmp(strategy, strategies[x].name))
			return strategies[x].strategy;
	}

	return -1;
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

static inline struct call_queue *queue_ref(struct call_queue *q)
{
	ao2_ref(q, 1);
	return q;
}

static inline struct call_queue *queue_unref(struct call_queue *q)
{
	ao2_ref(q, -1);
	return q;
}

/*! \brief Set variables of queue */
static void set_queue_variables(struct call_queue *q, struct ast_channel *chan)
{
	char interfacevar[256]="";
	float sl = 0;

	if (q->setqueuevar) {
		sl = 0;
		if (q->callscompleted > 0) 
			sl = 100 * ((float) q->callscompletedinsl / (float) q->callscompleted);

		snprintf(interfacevar, sizeof(interfacevar),
			"QUEUENAME=%s,QUEUEMAX=%d,QUEUESTRATEGY=%s,QUEUECALLS=%d,QUEUEHOLDTIME=%d,QUEUETALKTIME=%d,QUEUECOMPLETED=%d,QUEUEABANDONED=%d,QUEUESRVLEVEL=%d,QUEUESRVLEVELPERF=%2.1f",
			q->name, q->maxlen, int2strat(q->strategy), q->count, q->holdtime, q->talktime, q->callscompleted, q->callsabandoned,  q->servicelevel, sl);
	
		pbx_builtin_setvar_multiple(chan, interfacevar); 
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
	new->parent = q;
	new->pos = ++(*pos);
	new->opos = *pos;
}

/*! \brief Check if members are available
 *
 * This function checks to see if members are available to be called. If any member
 * is available, the function immediately returns 0. If no members are available,
 * then -1 is returned.
 */
static int get_member_status(struct call_queue *q, int max_penalty, int min_penalty, enum empty_conditions conditions)
{
	struct member *member;
	struct ao2_iterator mem_iter;

	ao2_lock(q);
	mem_iter = ao2_iterator_init(q->members, 0);
	for (; (member = ao2_iterator_next(&mem_iter)); ao2_ref(member, -1)) {
		if ((max_penalty && (member->penalty > max_penalty)) || (min_penalty && (member->penalty < min_penalty))) {
			if (conditions & QUEUE_EMPTY_PENALTY) {
				ast_debug(4, "%s is unavailable because his penalty is not between %d and %d\n", member->membername, min_penalty, max_penalty);
				continue;
			}
		}

		switch (member->status) {
		case AST_DEVICE_INVALID:
			if (conditions & QUEUE_EMPTY_INVALID) {
				ast_debug(4, "%s is unavailable because his device state is 'invalid'\n", member->membername);
				break;
			}
		case AST_DEVICE_UNAVAILABLE:
			if (conditions & QUEUE_EMPTY_UNAVAILABLE) {
				ast_debug(4, "%s is unavailable because his device state is 'unavailable'\n", member->membername);
				break;
			}
		case AST_DEVICE_INUSE:
			if (conditions & QUEUE_EMPTY_INUSE) {
				ast_debug(4, "%s is unavailable because his device state is 'inuse'\n", member->membername);
				break;
			}
		case AST_DEVICE_UNKNOWN:
			if (conditions & QUEUE_EMPTY_UNKNOWN) {
				ast_debug(4, "%s is unavailable because his device state is 'unknown'\n", member->membername);
				break;
			}
		default:
			if (member->paused && (conditions & QUEUE_EMPTY_PAUSED)) {
				ast_debug(4, "%s is unavailable because he is paused'\n", member->membername);
				break;
			} else if ((conditions & QUEUE_EMPTY_WRAPUP) && member->lastcall && q->wrapuptime && (time(NULL) - q->wrapuptime < member->lastcall)) {
				ast_debug(4, "%s is unavailable because it has only been %d seconds since his last call (wrapup time is %d)\n", member->membername, (int) (time(NULL) - member->lastcall), q->wrapuptime);
				break;
			} else {
				ao2_unlock(q);
				ao2_ref(member, -1);
				ast_debug(4, "%s is available.\n", member->membername);
				return 0;
			}
			break;
		}
	}

	ao2_unlock(q);
	return -1;
}

struct statechange {
	AST_LIST_ENTRY(statechange) entry;
	int state;
	char dev[0];
};

/*! \brief set a member's status based on device state of that member's state_interface.
 *  
 * Lock interface list find sc, iterate through each queues queue_member list for member to
 * update state inside queues
*/
static int update_status(struct call_queue *q, struct member *m, const int status)
{
	m->status = status;

	if (q->maskmemberstatus)
		return 0;

	manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
		"Queue: %s\r\n"
		"Location: %s\r\n"
		"MemberName: %s\r\n"
		"Membership: %s\r\n"
		"Penalty: %d\r\n"
		"CallsTaken: %d\r\n"
		"LastCall: %d\r\n"
		"Status: %d\r\n"
		"Paused: %d\r\n",
		q->name, m->interface, m->membername, m->dynamic ? "dynamic" : m->realtime ? "realtime" : "static",
		m->penalty, m->calls, (int)m->lastcall, m->status, m->paused
	);

	return 0;
}

/*! \brief set a member's status based on device state of that member's interface*/
static int handle_statechange(void *datap)
{
	struct statechange *sc = datap;
	struct ao2_iterator miter, qiter;
	struct member *m;
	struct call_queue *q;
	char interface[80], *slash_pos;
	int found = 0;

	qiter = ao2_iterator_init(queues, 0);

	while ((q = ao2_iterator_next(&qiter))) {
		ao2_lock(q);

		miter = ao2_iterator_init(q->members, 0);
		for (; (m = ao2_iterator_next(&miter)); ao2_ref(m, -1)) {
			ast_copy_string(interface, m->state_interface, sizeof(interface));

			if ((slash_pos = strchr(interface, '/')))
				if (!strncasecmp(interface, "Local/", 6) && (slash_pos = strchr(slash_pos + 1, '/')))
					*slash_pos = '\0';

			if (!strcasecmp(interface, sc->dev)) {
				found = 1;
				update_status(q, m, sc->state);
				ao2_ref(m, -1);
				break;
			}
		}

		ao2_unlock(q);
		ao2_ref(q, -1);
	}

	if (found)
		ast_debug(1, "Device '%s' changed to state '%d' (%s)\n", sc->dev, sc->state, ast_devstate2str(sc->state));
	else
		ast_debug(3, "Device '%s' changed to state '%d' (%s) but we don't care because they're not a member of any queue.\n", sc->dev, sc->state, ast_devstate2str(sc->state));

	ast_free(sc);
	return 0;
}

static void device_state_cb(const struct ast_event *event, void *unused)
{
	enum ast_device_state state;
	const char *device;
	struct statechange *sc;
	size_t datapsize;

	state = ast_event_get_ie_uint(event, AST_EVENT_IE_STATE);
	device = ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE);

	if (ast_strlen_zero(device)) {
		ast_log(LOG_ERROR, "Received invalid event that had no device IE\n");
		return;
	}
	datapsize = sizeof(*sc) + strlen(device) + 1;
	if (!(sc = ast_calloc(1, datapsize))) {
		ast_log(LOG_ERROR, "failed to calloc a state change struct\n");
		return;
	}
	sc->state = state;
	strcpy(sc->dev, device);
	if (ast_taskprocessor_push(devicestate_tps, handle_statechange, sc) < 0) {
		ast_free(sc);
	}
}

/*! \brief allocate space for new queue member and set fields based on parameters passed */
static struct member *create_queue_member(const char *interface, const char *membername, int penalty, int paused, const char *state_interface)
{
	struct member *cur;
	
	if ((cur = ao2_alloc(sizeof(*cur), NULL))) {
		cur->penalty = penalty;
		cur->paused = paused;
		ast_copy_string(cur->interface, interface, sizeof(cur->interface));
		if (!ast_strlen_zero(state_interface))
			ast_copy_string(cur->state_interface, state_interface, sizeof(cur->state_interface));
		else
			ast_copy_string(cur->state_interface, interface, sizeof(cur->state_interface));
		if (!ast_strlen_zero(membername))
			ast_copy_string(cur->membername, membername, sizeof(cur->membername));
		else
			ast_copy_string(cur->membername, interface, sizeof(cur->membername));
		if (!strchr(cur->interface, '/'))
			ast_log(LOG_WARNING, "No location at interface '%s'\n", interface);
		cur->status = ast_device_state(cur->state_interface);
	}

	return cur;
}


static int compress_char(const char c)
{
	if (c < 32)
		return 0;
	else if (c > 96)
		return c - 64;
	else
		return c - 32;
}

static int member_hash_fn(const void *obj, const int flags)
{
	const struct member *mem = obj;
	const char *chname = strchr(mem->interface, '/');
	int ret = 0, i;
	if (!chname)
		chname = mem->interface;
	for (i = 0; i < 5 && chname[i]; i++)
		ret += compress_char(chname[i]) << (i * 6);
	return ret;
}

static int member_cmp_fn(void *obj1, void *obj2, int flags)
{
	struct member *mem1 = obj1, *mem2 = obj2;
	return strcasecmp(mem1->interface, mem2->interface) ? 0 : CMP_MATCH | CMP_STOP;
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
	q->setinterfacevar = 0;
	q->setqueuevar = 0;
	q->setqueueentryvar = 0;
	q->autofill = autofill_default;
	q->montype = montype_default;
	q->monfmt[0] = '\0';
	q->reportholdtime = 0;
	q->wrapuptime = 0;
	q->joinempty = 0;
	q->leavewhenempty = 0;
	q->memberdelay = 0;
	q->maskmemberstatus = 0;
	q->eventwhencalled = 0;
	q->weight = 0;
	q->timeoutrestart = 0;
	q->periodicannouncefrequency = 0;
	q->randomperiodicannounce = 0;
	q->numperiodicannounce = 0;
	q->timeoutpriority = TIMEOUT_PRIORITY_APP;
	if (!q->members) {
		if (q->strategy == QUEUE_STRATEGY_LINEAR)
			/* linear strategy depends on order, so we have to place all members in a single bucket */
			q->members = ao2_container_alloc(1, member_hash_fn, member_cmp_fn);
		else
			q->members = ao2_container_alloc(37, member_hash_fn, member_cmp_fn);
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

	if ((q->sound_periodicannounce[0] = ast_str_create(32)))
		ast_str_set(&q->sound_periodicannounce[0], 0, "queue-periodic-announce");

	for (i = 1; i < MAX_PERIODIC_ANNOUNCEMENTS; i++) {
		if (q->sound_periodicannounce[i])
			ast_str_set(&q->sound_periodicannounce[i], 0, "%s", "");
	}

	while ((pr_iter = AST_LIST_REMOVE_HEAD(&q->rules,list)))
		ast_free(pr_iter);
}

static void clear_queue(struct call_queue *q)
{
	q->holdtime = 0;
	q->callscompleted = 0;
	q->callsabandoned = 0;
	q->callscompletedinsl = 0;
	q->wrapuptime = 0;
	q->talktime = 0;

	if (q->members) {
		struct member *mem;
		struct ao2_iterator mem_iter = ao2_iterator_init(q->members, 0);
		while ((mem = ao2_iterator_next(&mem_iter))) {
			mem->calls = 0;
			ao2_ref(mem, -1);
		}
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
static int insert_penaltychange (const char *list_name, const char *content, const int linenum)
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

	if ((minstr = strchr(maxstr,',')))
		*minstr++ = '\0';
	
	/* The last check will evaluate true if either no penalty change is indicated for a given rule
	 * OR if a min penalty change is indicated but no max penalty change is */
	if (*maxstr == '+' || *maxstr == '-' || *maxstr == '\0') {
		rule->max_relative = 1;
	}

	rule->max_value = atoi(maxstr);

	if (!ast_strlen_zero(minstr)) {
		if (*minstr == '+' || *minstr == '-')
			rule->min_relative = 1;
		rule->min_value = atoi(minstr);
	} else /*there was no minimum specified, so assume this means no change*/
		rule->min_relative = 1;

	/*We have the rule made, now we need to insert it where it belongs*/
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list){
		if (strcasecmp(rl_iter->name, list_name))
			continue;

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
		}
	}

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
		if (q->timeout < 0)
			q->timeout = DEFAULT_TIMEOUT;
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
		if (!strcasecmp(val, "once"))
			q->announceholdtime = ANNOUNCEHOLDTIME_ONCE;
		else if (ast_true(val))
			q->announceholdtime = ANNOUNCEHOLDTIME_ALWAYS;
		else
			q->announceholdtime = 0;
	} else if (!strcasecmp(param, "announce-position")) {
		if (!strcasecmp(val, "limit"))
			q->announceposition = ANNOUNCEPOSITION_LIMIT;
		else if (!strcasecmp(val, "more"))
			q->announceposition = ANNOUNCEPOSITION_MORE_THAN;
		else if (ast_true(val))
			q->announceposition = ANNOUNCEPOSITION_YES;
		else
			q->announceposition = ANNOUNCEPOSITION_NO;
	} else if (!strcasecmp(param, "announce-position-limit")) {
		q->announcepositionlimit = atoi(val);
	} else if (!strcasecmp(param, "periodic-announce")) {
		if (strchr(val, ',')) {
			char *s, *buf = ast_strdupa(val);
			unsigned int i = 0;

			while ((s = strsep(&buf, ",|"))) {
				if (!q->sound_periodicannounce[i])
					q->sound_periodicannounce[i] = ast_str_create(16);
				ast_str_set(&q->sound_periodicannounce[i], 0, "%s", s);
				i++;
				if (i == MAX_PERIODIC_ANNOUNCEMENTS)
					break;
			}
			q->numperiodicannounce = i;
		} else {
			ast_str_set(&q->sound_periodicannounce[0], 0, "%s", val);
			q->numperiodicannounce = 1;
		}
	} else if (!strcasecmp(param, "periodic-announce-frequency")) {
		q->periodicannouncefrequency = atoi(val);
	} else if (!strcasecmp(param, "random-periodic-announce")) {
		q->randomperiodicannounce = ast_true(val);
	} else if (!strcasecmp(param, "retry")) {
		q->retry = atoi(val);
		if (q->retry <= 0)
			q->retry = DEFAULT_RETRY;
	} else if (!strcasecmp(param, "wrapuptime")) {
		q->wrapuptime = atoi(val);
	} else if (!strcasecmp(param, "autofill")) {
		q->autofill = ast_true(val);
	} else if (!strcasecmp(param, "monitor-type")) {
		if (!strcasecmp(val, "mixmonitor"))
			q->montype = 1;
	} else if (!strcasecmp(param, "autopause")) {
		q->autopause = ast_true(val);
	} else if (!strcasecmp(param, "maxlen")) {
		q->maxlen = atoi(val);
		if (q->maxlen < 0)
			q->maxlen = 0;
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
	} else if (!strcasecmp(param, "eventmemberstatus")) {
		q->maskmemberstatus = !ast_true(val);
	} else if (!strcasecmp(param, "eventwhencalled")) {
		if (!strcasecmp(val, "vars")) {
			q->eventwhencalled = QUEUE_EVENT_VARIABLES;
		} else {
			q->eventwhencalled = ast_true(val) ? 1 : 0;
		}
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

/*!
 * \brief Find rt member record to update otherwise create one.
 *
 * Search for member in queue, if found update penalty/paused state,
 * if no memeber exists create one flag it as a RT member and add to queue member list. 
*/
static void rt_handle_member_record(struct call_queue *q, char *interface, const char *rt_uniqueid, const char *membername, const char *penalty_str, const char *paused_str, const char* state_interface)
{
	struct member *m;
	struct ao2_iterator mem_iter;
	int penalty = 0;
	int paused  = 0;
	int found = 0;

	if (penalty_str) {
		penalty = atoi(penalty_str);
		if (penalty < 0)
			penalty = 0;
	}

	if (paused_str) {
		paused = atoi(paused_str);
		if (paused < 0)
			paused = 0;
	}

 	/* Find member by realtime uniqueid and update */
 	mem_iter = ao2_iterator_init(q->members, 0);
 	while ((m = ao2_iterator_next(&mem_iter))) {
 		if (!strcasecmp(m->rt_uniqueid, rt_uniqueid)) {
 			m->dead = 0;	/* Do not delete this one. */
 			ast_copy_string(m->rt_uniqueid, rt_uniqueid, sizeof(m->rt_uniqueid));
 			if (paused_str)
 				m->paused = paused;
 			if (strcasecmp(state_interface, m->state_interface)) {
 				ast_copy_string(m->state_interface, state_interface, sizeof(m->state_interface));
 			}	   
 			m->penalty = penalty;
 			found = 1;
 			ao2_ref(m, -1);
 			break;
 		}
 		ao2_ref(m, -1);
 	}

 	/* Create a new member */
 	if (!found) {
		if ((m = create_queue_member(interface, membername, penalty, paused, state_interface))) {
			m->dead = 0;
			m->realtime = 1;
			ast_copy_string(m->rt_uniqueid, rt_uniqueid, sizeof(m->rt_uniqueid));
			ast_queue_log(q->name, "REALTIME", m->interface, "ADDMEMBER", "%s", "");
			ao2_link(q->members, m);
			ao2_ref(m, -1);
			m = NULL;
			q->membercount++;
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
			ao2_unlink(q->members, cur);
			q->membercount--;
		}
		ao2_ref(cur, -1);
	}
}

/*! \brief Free queue's member list then its string fields */
static void destroy_queue(void *obj)
{
	struct call_queue *q = obj;
	int i;

	free_members(q, 1);
	ast_string_field_free_memory(q);
	for (i = 0; i < MAX_PERIODIC_ANNOUNCEMENTS; i++) {
		if (q->sound_periodicannounce[i])
			free(q->sound_periodicannounce[i]);
	}
	ao2_ref(q->members, -1);
}

static struct call_queue *alloc_queue(const char *queuename)
{
	struct call_queue *q;

	if ((q = ao2_alloc(sizeof(*q), destroy_queue))) {
		if (ast_string_field_init(q, 64)) {
			ao2_ref(q, -1);
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
	if ((q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
		ao2_lock(q);
		if (!q->realtime) {
			if (q->dead) {
				ao2_unlock(q);
				queue_unref(q);
				return NULL;
			} else {
				ast_log(LOG_WARNING, "Static queue '%s' already exists. Not loading from realtime\n", q->name);
				ao2_unlock(q);
				return q;
			}
		}
		queue_unref(q);
	} else if (!member_config)
		/* Not found in the list, and it's not realtime ... */
		return NULL;

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
			ao2_unlink(queues, q);
			ao2_unlock(q);
			queue_unref(q);
		}
		return NULL;
	}

	/* Create a new queue if an in-core entry does not exist yet. */
	if (!q) {
		struct ast_variable *tmpvar = NULL;
		if (!(q = alloc_queue(queuename)))
			return NULL;
		ao2_lock(q);
		clear_queue(q);
		q->realtime = 1;
		q->membercount = 0;
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
		if (!tmpvar)
			q->strategy = QUEUE_STRATEGY_RINGALL;
		ao2_link(queues, q);
	}
	init_queue(q);		/* Ensure defaults for all parameters not set explicitly. */

	memset(tmpbuf, 0, sizeof(tmpbuf));
	for (v = queue_vars; v; v = v->next) {
		/* Convert to dashes `-' from underscores `_' as the latter are more SQL friendly. */
		if ((tmp = strchr(v->name, '_'))) {
			ast_copy_string(tmpbuf, v->name, sizeof(tmpbuf));
			tmp_name = tmpbuf;
			tmp = tmpbuf;
			while ((tmp = strchr(tmp, '_')))
				*tmp++ = '-';
		} else
			tmp_name = v->name;

		if (!ast_strlen_zero(v->value)) {
			/* Don't want to try to set the option if the value is empty */
			queue_set_param(q, tmp_name, v->value, -1, 0);
		}
	}

	/* Temporarily set realtime members dead so we can detect deleted ones. 
	 * Also set the membercount correctly for realtime*/
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		q->membercount++;
		if (m->realtime)
			m->dead = 1;
		ao2_ref(m, -1);
	}

	while ((interface = ast_category_browse(member_config, interface))) {
		rt_handle_member_record(q, interface,
			ast_variable_retrieve(member_config, interface, "uniqueid"),
			S_OR(ast_variable_retrieve(member_config, interface, "membername"),interface),
			ast_variable_retrieve(member_config, interface, "penalty"),
			ast_variable_retrieve(member_config, interface, "paused"),
			S_OR(ast_variable_retrieve(member_config, interface, "state_interface"),interface));
	}

	/* Delete all realtime members that have been deleted in DB. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->dead) {
			ast_queue_log(q->name, "REALTIME", m->interface, "REMOVEMEMBER", "%s", "");
			ao2_unlink(q->members, m);
			q->membercount--;
		}
		ao2_ref(m, -1);
	}

	ao2_unlock(q);

	return q;
}

static struct call_queue *load_realtime_queue(const char *queuename)
{
	struct ast_variable *queue_vars;
	struct ast_config *member_config = NULL;
	struct call_queue *q = NULL, tmpq = {
		.name = queuename,	
	};
	int prev_weight = 0;

	/* Find the queue in the in-core list first. */
	q = ao2_find(queues, &tmpq, OBJ_POINTER);

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
				ast_log(LOG_ERROR, "no queue_members defined in your config (extconfig.conf).\n");
				ast_variables_destroy(queue_vars);
				return NULL;
			}
		}
		if (q) {
			prev_weight = q->weight ? 1 : 0;
		}

		ao2_lock(queues);

		q = find_queue_by_name_rt(queuename, queue_vars, member_config);
		if (member_config) {
			ast_config_destroy(member_config);
		}
		if (queue_vars) {
			ast_variables_destroy(queue_vars);
		}
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
		ao2_unlock(queues);

	} else {
		update_realtime_members(q);
	}
	return q;
}

static int update_realtime_member_field(struct member *mem, const char *queue_name, const char *field, const char *value)
{
	int ret = -1;

	if (ast_strlen_zero(mem->rt_uniqueid))
 		return ret;

	if ((ast_update_realtime("queue_members", "uniqueid", mem->rt_uniqueid, field, value, SENTINEL)) > 0)
		ret = 0;

	return ret;
}


static void update_realtime_members(struct call_queue *q)
{
	struct ast_config *member_config = NULL;
	struct member *m;
	char *interface = NULL;
	struct ao2_iterator mem_iter;

	if (!(member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", q->name , SENTINEL))) {
		/*This queue doesn't have realtime members*/
		ast_debug(3, "Queue %s has no realtime members defined. No need for update\n", q->name);
		return;
	}

	ao2_lock(queues);
	ao2_lock(q);
	
	/* Temporarily set realtime  members dead so we can detect deleted ones.*/ 
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->realtime)
			m->dead = 1;
		ao2_ref(m, -1);
	}

	while ((interface = ast_category_browse(member_config, interface))) {
		rt_handle_member_record(q, interface,
			ast_variable_retrieve(member_config, interface, "uniqueid"),
			S_OR(ast_variable_retrieve(member_config, interface, "membername"), interface),
			ast_variable_retrieve(member_config, interface, "penalty"),
			ast_variable_retrieve(member_config, interface, "paused"),
			S_OR(ast_variable_retrieve(member_config, interface, "state_interface"), interface));
	}

	/* Delete all realtime members that have been deleted in DB. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->dead) {
			ast_queue_log(q->name, "REALTIME", m->interface, "REMOVEMEMBER", "%s", "");
			ao2_unlink(q->members, m);
			q->membercount--;
		}
		ao2_ref(m, -1);
	}
	ao2_unlock(q);
	ao2_unlock(queues);
	ast_config_destroy(member_config);
}

static int join_queue(char *queuename, struct queue_ent *qe, enum queue_result *reason, int position)
{
	struct call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;

	if (!(q = load_realtime_queue(queuename)))
		return res;

	ao2_lock(queues);
	ao2_lock(q);

	/* This is our one */
	if (q->joinempty) {
		int status = 0;
		if ((status = get_member_status(q, qe->max_penalty, qe->min_penalty, q->joinempty))) {
			*reason = QUEUE_JOINEMPTY;
			ao2_unlock(q);
			ao2_unlock(queues);
			return res;
		}
	}
	if (*reason == QUEUE_UNKNOWN && q->maxlen && (q->count >= q->maxlen))
		*reason = QUEUE_FULL;
	else if (*reason == QUEUE_UNKNOWN) {
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
			if (!inserted && (qe->prio <= cur->prio) && position && (position <= pos + 1)) {
				insert_entry(q, prev, qe, &pos);
				/*pos is incremented inside insert_entry, so don't need to add 1 here*/
				if (position < pos) {
					ast_log(LOG_NOTICE, "Asked to be inserted at position %d but forced into position %d due to higher priority callers\n", position, pos);
				}
				inserted = 1;
			}
			cur->pos = ++pos;
			prev = cur;
			cur = cur->next;
		}
		/* No luck, join at the end of the queue */
		if (!inserted)
			insert_entry(q, prev, qe, &pos);
		ast_copy_string(qe->moh, q->moh, sizeof(qe->moh));
		ast_copy_string(qe->announce, q->announce, sizeof(qe->announce));
		ast_copy_string(qe->context, q->context, sizeof(qe->context));
		q->count++;
		res = 0;
		manager_event(EVENT_FLAG_CALL, "Join",
			"Channel: %s\r\nCallerIDNum: %s\r\nCallerIDName: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\nUniqueid: %s\r\n",
			qe->chan->name,
			S_OR(qe->chan->cid.cid_num, "unknown"), /* XXX somewhere else it is <unknown> */
			S_OR(qe->chan->cid.cid_name, "unknown"),
			q->name, qe->pos, q->count, qe->chan->uniqueid );
		ast_debug(1, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
	}
	ao2_unlock(q);
	ao2_unlock(queues);

	return res;
}

static int play_file(struct ast_channel *chan, const char *filename)
{
	int res;

	if (ast_strlen_zero(filename)) {
		return 0;
	}

	ast_stopstream(chan);

	res = ast_streamfile(chan, filename, chan->language);
	if (!res)
		res = ast_waitstream(chan, AST_DIGIT_ANY);

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
	if (ast_strlen_zero(qe->context))
		return 0;

	/* If the extension is bad, then reset the digits to blank */
	if (!ast_canmatch_extension(qe->chan, qe->context, qe->digits, 1, qe->chan->cid.cid_num)) {
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
	if ((now - qe->last_pos) < qe->parent->minannouncefrequency)
		return 0;

	/* If either our position has changed, or we are over the freq timer, say position */
	if ((qe->last_pos_said == qe->pos) && ((now - qe->last_pos) < qe->parent->announcefrequency))
		return 0;

	if (ringing) {
		ast_indicate(qe->chan,-1);
	} else {
		ast_moh_stop(qe->chan);
	}

	if (qe->parent->announceposition == ANNOUNCEPOSITION_YES ||
		qe->parent->announceposition == ANNOUNCEPOSITION_MORE_THAN ||
		(qe->parent->announceposition == ANNOUNCEPOSITION_LIMIT &&
		qe->pos <= qe->parent->announcepositionlimit))
			announceposition = 1;


	if (announceposition == 1) {
		/* Say we're next, if we are */
		if (qe->pos == 1) {
			res = play_file(qe->chan, qe->parent->sound_next);
			if (res)
				goto playout;
			else
				goto posout;
		} else {
			if (qe->parent->announceposition == ANNOUNCEPOSITION_MORE_THAN && qe->pos > qe->parent->announcepositionlimit){
				/* More than Case*/
				res = play_file(qe->chan, qe->parent->queue_quantity1);
				if (res)
					goto playout;
				res = ast_say_number(qe->chan, qe->parent->announcepositionlimit, AST_DIGIT_ANY, qe->chan->language, NULL); /* Needs gender */
				if (res)
					goto playout;
			} else {
				/* Normal Case */
				res = play_file(qe->chan, qe->parent->sound_thereare);
				if (res)
					goto playout;
				res = ast_say_number(qe->chan, qe->pos, AST_DIGIT_ANY, qe->chan->language, NULL); /* Needs gender */
				if (res)
					goto playout;
			}
			if (qe->parent->announceposition == ANNOUNCEPOSITION_MORE_THAN && qe->pos > qe->parent->announcepositionlimit){
				/* More than Case*/
				res = play_file(qe->chan, qe->parent->queue_quantity2);
				if (res)
					goto playout;
			} else {
				res = play_file(qe->chan, qe->parent->sound_calls);
				if (res)
					goto playout;
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
		if (res)
			goto playout;

		if (avgholdmins >= 1) {
			res = ast_say_number(qe->chan, avgholdmins, AST_DIGIT_ANY, qe->chan->language, NULL);
			if (res)
				goto playout;

			if (avgholdmins == 1) {
				res = play_file(qe->chan, qe->parent->sound_minute);
				if (res)
					goto playout;
			} else {
				res = play_file(qe->chan, qe->parent->sound_minutes);
				if (res)
					goto playout;
			}
		}
		if (avgholdsecs >= 1) {
			res = ast_say_number(qe->chan, avgholdmins > 1 ? avgholdsecs : avgholdmins * 60 + avgholdsecs, AST_DIGIT_ANY, qe->chan->language, NULL);
			if (res)
				goto playout;

			res = play_file(qe->chan, qe->parent->sound_seconds);
			if (res)
				goto playout;
		}
	} else if (qe->parent->announceholdtime && !qe->parent->announceposition) {
		say_thanks = 0;
	}

posout:
	if (qe->parent->announceposition) {
		ast_verb(3, "Told %s in %s their queue position (which was %d)\n",
			qe->chan->name, qe->parent->name, qe->pos);
	}
	if (say_thanks) {
		res = play_file(qe->chan, qe->parent->sound_thanks);
	}
playout:
	if ((res > 0 && !valid_exit(qe, res)) || res < 0)
		res = 0;

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

	if (!(q = qe->parent))
		return;
	queue_ref(q);
	ao2_lock(q);

	prev = NULL;
	for (current = q->head; current; current = current->next) {
		if (current == qe) {
			char posstr[20];
			q->count--;

			/* Take us out of the queue */
			manager_event(EVENT_FLAG_CALL, "Leave",
				"Channel: %s\r\nQueue: %s\r\nCount: %d\r\nPosition: %d\r\nUniqueid: %s\r\n",
				qe->chan->name, q->name,  q->count, qe->pos, qe->chan->uniqueid);
			ast_debug(1, "Queue '%s' Leave, Channel '%s'\n", q->name, qe->chan->name );
			/* Take us out of the queue */
			if (prev)
				prev->next = current->next;
			else
				q->head = current->next;
			/* Free penalty rules */
			while ((pr_iter = AST_LIST_REMOVE_HEAD(&qe->qe_rules, list)))
				ast_free(pr_iter);
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
		ao2_unlink(queues, q);
	}
	/* unref the explicit ref earlier in the function */
	queue_unref(q);
}

/*! \brief Hang up a list of outgoing calls */
static void hangupcalls(struct callattempt *outgoing, struct ast_channel *exception, int cancel_answered_elsewhere)
{
	struct callattempt *oo;

	while (outgoing) {
		/* If someone else answered the call we should indicate this in the CANCEL */
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception || cancel_answered_elsewhere)) {
			if (exception || cancel_answered_elsewhere)
				ast_set_flag(outgoing->chan, AST_FLAG_ANSWERED_ELSEWHERE);
			ast_hangup(outgoing->chan);
		}
		oo = outgoing;
		outgoing = outgoing->q_next;
		if (oo->member)
			ao2_ref(oo->member, -1);
		ast_free(oo);
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
		switch (mem->status) {
		case AST_DEVICE_INUSE:
			if (!q->ringinuse)
				break;
			/* else fall through */
		case AST_DEVICE_NOT_INUSE:
		case AST_DEVICE_UNKNOWN:
			if (!mem->paused) {
				avl++;
			}
			break;
		}
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
	
	/* q's lock and rq's lock already set by try_calling()
	 * to solve deadlock */
	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
		if (q == rq) { /* don't check myself, could deadlock */
			queue_unref(q);
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
		queue_unref(q);
		if (found) {
			break;
		}
	}
	return found;
}

/*! \brief common hangup actions */
static void do_hang(struct callattempt *o)
{
	o->stillgoing = 0;
	ast_hangup(o->chan);
	o->chan = NULL;
}

/*! \brief convert "\n" to "\nVariable: " ready for manager to use */
static char *vars2manager(struct ast_channel *chan, char *vars, size_t len)
{
	struct ast_str *buf = ast_str_thread_get(&ast_str_thread_global_buf, len + 1);
	const char *tmp;

	if (pbx_builtin_serialize_variables(chan, &buf)) {
		int i, j;

		/* convert "\n" to "\nVariable: " */
		strcpy(vars, "Variable: ");
		tmp = ast_str_buffer(buf);

		for (i = 0, j = 10; (i < len - 1) && (j < len - 1); i++, j++) {
			vars[j] = tmp[i];

			if (tmp[i + 1] == '\0')
				break;
			if (tmp[i] == '\n') {
				vars[j++] = '\r';
				vars[j++] = '\n';

				ast_copy_string(&(vars[j]), "Variable: ", len - j);
				j += 9;
			}
		}
		if (j > len - 3)
			j = len - 3;
		vars[j++] = '\r';
		vars[j++] = '\n';
		vars[j] = '\0';
	} else {
		/* there are no channel variables; leave it blank */
		*vars = '\0';
	}
	return vars;
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

	/* on entry here, we know that tmp->chan == NULL */
	if ((tmp->lastqueue && tmp->lastqueue->wrapuptime && (time(NULL) - tmp->lastcall < tmp->lastqueue->wrapuptime)) ||
		(!tmp->lastqueue && qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime))) {
		ast_debug(1, "Wrapuptime not yet expired on queue %s for %s\n", 
				(tmp->lastqueue ? tmp->lastqueue->name : qe->parent->name), tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	if (!qe->parent->ringinuse && (tmp->member->status != AST_DEVICE_NOT_INUSE) && (tmp->member->status != AST_DEVICE_UNKNOWN)) {
		ast_debug(1, "%s in use, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}

	if (tmp->member->paused) {
		ast_debug(1, "%s paused, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
	if (use_weight && compare_weight(qe->parent,tmp->member)) {
		ast_debug(1, "Priority queue delaying call to %s:%s\n", qe->parent->name, tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	ast_copy_string(tech, tmp->interface, sizeof(tech));
	if ((location = strchr(tech, '/')))
		*location++ = '\0';
	else
		location = "";

	/* Request the peer */
	tmp->chan = ast_request(tech, qe->chan->nativeformats, qe->chan, location, &status);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;	

		ao2_lock(qe->parent);
		update_status(qe->parent, tmp->member, ast_device_state(tmp->member->state_interface));
		qe->parent->rrpos++;
		qe->linpos++;
		ao2_unlock(qe->parent);

		(*busies)++;
		return 0;
	}

	ast_channel_lock(tmp->chan);
	while (ast_channel_trylock(qe->chan)) {
		CHANNEL_DEADLOCK_AVOIDANCE(tmp->chan);
	}

	if (qe->cancel_answered_elsewhere) {
		ast_set_flag(tmp->chan, AST_FLAG_ANSWERED_ELSEWHERE);
	}
	tmp->chan->appl = "AppQueue";
	tmp->chan->data = "(Outgoing Line)";
	memset(&tmp->chan->whentohangup, 0, sizeof(tmp->chan->whentohangup));

	/* If the new channel has no callerid, try to guess what it should be */
	if (ast_strlen_zero(tmp->chan->cid.cid_num)) {
		if (!ast_strlen_zero(qe->chan->connected.id.number)) {
			ast_set_callerid(tmp->chan, qe->chan->connected.id.number, qe->chan->connected.id.name, qe->chan->connected.ani);
			tmp->chan->cid.cid_pres = qe->chan->connected.id.number_presentation;
		} else if (!ast_strlen_zero(qe->chan->cid.cid_dnid)) {
			ast_set_callerid(tmp->chan, qe->chan->cid.cid_dnid, NULL, NULL);
		} else if (!ast_strlen_zero(S_OR(qe->chan->macroexten, qe->chan->exten))) {
			ast_set_callerid(tmp->chan, S_OR(qe->chan->macroexten, qe->chan->exten), NULL, NULL); 
		}
		tmp->update_connectedline = 0;
	}

	if (tmp->chan->cid.cid_rdnis)
		ast_free(tmp->chan->cid.cid_rdnis);
	tmp->chan->cid.cid_rdnis = ast_strdup(qe->chan->cid.cid_rdnis);
	ast_party_redirecting_copy(&tmp->chan->redirecting, &qe->chan->redirecting);

	tmp->chan->cid.cid_tns = qe->chan->cid.cid_tns;

	ast_connected_line_copy_from_caller(&tmp->chan->connected, &qe->chan->cid);

	/* Inherit specially named variables from parent channel */
	ast_channel_inherit_variables(qe->chan, tmp->chan);

	/* Presense of ADSI CPE on outgoing channel follows ours */
	tmp->chan->adsicpe = qe->chan->adsicpe;

	/* Inherit context and extension */
	macrocontext = pbx_builtin_getvar_helper(qe->chan, "MACRO_CONTEXT");
	ast_string_field_set(tmp->chan, dialcontext, ast_strlen_zero(macrocontext) ? qe->chan->context : macrocontext);
	macroexten = pbx_builtin_getvar_helper(qe->chan, "MACRO_EXTEN");
	if (!ast_strlen_zero(macroexten))
		ast_copy_string(tmp->chan->exten, macroexten, sizeof(tmp->chan->exten));
	else
		ast_copy_string(tmp->chan->exten, qe->chan->exten, sizeof(tmp->chan->exten));
	if (ast_cdr_isset_unanswered()) {
		/* they want to see the unanswered dial attempts! */
		/* set up the CDR fields on all the CDRs to give sensical information */
		ast_cdr_setdestchan(tmp->chan->cdr, tmp->chan->name);
		strcpy(tmp->chan->cdr->clid, qe->chan->cdr->clid);
		strcpy(tmp->chan->cdr->channel, qe->chan->cdr->channel);
		strcpy(tmp->chan->cdr->src, qe->chan->cdr->src);
		strcpy(tmp->chan->cdr->dst, qe->chan->exten);
		strcpy(tmp->chan->cdr->dcontext, qe->chan->context);
		strcpy(tmp->chan->cdr->lastapp, qe->chan->cdr->lastapp);
		strcpy(tmp->chan->cdr->lastdata, qe->chan->cdr->lastdata);
		tmp->chan->cdr->amaflags = qe->chan->cdr->amaflags;
		strcpy(tmp->chan->cdr->accountcode, qe->chan->cdr->accountcode);
		strcpy(tmp->chan->cdr->userfield, qe->chan->cdr->userfield);
	}

	/* Place the call, but don't wait on the answer */
	if ((res = ast_call(tmp->chan, location, 0))) {
		/* Again, keep going even if there's an error */
		ast_debug(1, "ast call on peer returned %d\n", res);
		ast_verb(3, "Couldn't call %s\n", tmp->interface);
		ast_channel_unlock(tmp->chan);
		ast_channel_unlock(qe->chan);
		do_hang(tmp);
		(*busies)++;
		update_status(qe->parent, tmp->member, ast_device_state(tmp->member->state_interface));
		return 0;
	} else if (qe->parent->eventwhencalled) {
		char vars[2048];

		manager_event(EVENT_FLAG_AGENT, "AgentCalled",
					"Queue: %s\r\n"
					"AgentCalled: %s\r\n"
					"AgentName: %s\r\n"
					"ChannelCalling: %s\r\n"
					"DestinationChannel: %s\r\n"
					"CallerIDNum: %s\r\n"
					"CallerIDName: %s\r\n"
					"Context: %s\r\n"
					"Extension: %s\r\n"
					"Priority: %d\r\n"
					"Uniqueid: %s\r\n"
					"%s",
					qe->parent->name, tmp->interface, tmp->member->membername, qe->chan->name, tmp->chan->name,
					tmp->chan->cid.cid_num ? tmp->chan->cid.cid_num : "unknown",
					tmp->chan->cid.cid_name ? tmp->chan->cid.cid_name : "unknown",
					qe->chan->context, qe->chan->exten, qe->chan->priority, qe->chan->uniqueid,
					qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
		ast_verb(3, "Called %s\n", tmp->interface);
	}
	ast_channel_unlock(tmp->chan);
	ast_channel_unlock(qe->chan);

	update_status(qe->parent, tmp->member, ast_device_state(tmp->member->state_interface));
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

/*! \brief Playback announcement to queued members if peroid has elapsed */
static int say_periodic_announcement(struct queue_ent *qe, int ringing)
{
	int res = 0;
	time_t now;

	/* Get the current time */
	time(&now);

	/* Check to see if it is time to announce */
	if ((now - qe->last_periodic_announce_time) < qe->parent->periodicannouncefrequency)
		return 0;

	/* Stop the music on hold so we can play our own file */
	if (ringing)
		ast_indicate(qe->chan,-1);
	else
		ast_moh_stop(qe->chan);

	ast_verb(3, "Playing periodic announcement\n");
	
	if (qe->parent->randomperiodicannounce) {
		qe->last_periodic_announce_sound = ((unsigned long) ast_random()) % qe->parent->numperiodicannounce;
	} else if (qe->last_periodic_announce_sound >= qe->parent->numperiodicannounce || 
		ast_str_strlen(qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound]) == 0) {
		qe->last_periodic_announce_sound = 0;
	}
	
	/* play the announcement */
	res = play_file(qe->chan, ast_str_buffer(qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound]));

	if ((res > 0 && !valid_exit(qe, res)) || res < 0)
		res = 0;

	/* Resume Music on Hold if the caller is going to stay in the queue */
	if (!res) {
		if (ringing)
			ast_indicate(qe->chan, AST_CONTROL_RINGING);
		else
			ast_moh_start(qe->chan, qe->moh, NULL);
	}

	/* update last_periodic_announce_time */
	qe->last_periodic_announce_time = now;

	/* Update the current periodic announcement to the next announcement */
	if (!qe->parent->randomperiodicannounce) {
		qe->last_periodic_announce_sound++;
	}
	
	return res;
}

/*! \brief Record that a caller gave up on waiting in queue */
static void record_abandoned(struct queue_ent *qe)
{
	ao2_lock(qe->parent);
	set_queue_variables(qe->parent, qe->chan);
	manager_event(EVENT_FLAG_AGENT, "QueueCallerAbandon",
		"Queue: %s\r\n"
		"Uniqueid: %s\r\n"
		"Position: %d\r\n"
		"OriginalPosition: %d\r\n"
		"HoldTime: %d\r\n",
		qe->parent->name, qe->chan->uniqueid, qe->pos, qe->opos, (int)(time(NULL) - qe->start));

	qe->parent->callsabandoned++;
	ao2_unlock(qe->parent);
}

/*! \brief RNA == Ring No Answer. Common code that is executed when we try a queue member and they don't answer. */
static void rna(int rnatime, struct queue_ent *qe, char *interface, char *membername, int pause)
{
	ast_verb(3, "Nobody picked up in %d ms\n", rnatime);
	if (qe->parent->eventwhencalled) {
		char vars[2048];

		manager_event(EVENT_FLAG_AGENT, "AgentRingNoAnswer",
						"Queue: %s\r\n"
						"Uniqueid: %s\r\n"
						"Channel: %s\r\n"
						"Member: %s\r\n"
						"MemberName: %s\r\n"
						"Ringtime: %d\r\n"
						"%s",
						qe->parent->name,
						qe->chan->uniqueid,
						qe->chan->name,
						interface,
						membername,
						rnatime,
						qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
	}
	ast_queue_log(qe->parent->name, qe->chan->uniqueid, membername, "RINGNOANSWER", "%d", rnatime);
	if (qe->parent->autopause && pause) {
		if (!set_member_paused(qe->parent->name, interface, "Auto-Pause", 1)) {
			ast_verb(3, "Auto-Pausing Queue Member %s in queue %s since they failed to answer.\n", interface, qe->parent->name);
		} else {
			ast_verb(3, "Failed to pause Queue Member %s in queue %s!\n", interface, qe->parent->name);
		}
	}
	return;
}

#define AST_MAX_WATCHERS 256
/*! \brief Wait for a member to answer the call
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
static struct callattempt *wait_for_answer(struct queue_ent *qe, struct callattempt *outgoing, int *to, char *digit, int prebusies, int caller_disconnect, int forwardsallowed, int update_connectedline)
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

	ast_party_connected_line_init(&connected_caller);

	ast_channel_lock(qe->chan);
	inchan_name = ast_strdupa(qe->chan->name);
	ast_channel_unlock(qe->chan);

	starttime = (long) time(NULL);
#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->q_next) {
		if (epollo->chan)
			ast_poll_channel_add(in, epollo->chan);
	}
#endif
	
	while (*to && !peer) {
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
						watchers[pos++] = o->chan;
						if (!start)
							start = o;
						else
							prev->call_next = o;
						prev = o;
					}
				}
				numlines++;
			}
			if (pos > 1 /* found */ || !stillgoing /* nobody listening */ ||
				(qe->parent->strategy != QUEUE_STRATEGY_RINGALL) /* ring would not be delivered */)
				break;
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
		winner = ast_waitfor_n(watchers, pos, to);
		for (o = start; o; o = o->call_next) {
			/* We go with a static buffer here instead of using ast_strdupa. Using
			 * ast_strdupa in a loop like this one can cause a stack overflow
			 */
			char ochan_name[AST_CHANNEL_NAME];
			if (o->chan) {
				ast_channel_lock(o->chan);
				ast_copy_string(ochan_name, o->chan->name, sizeof(ochan_name));
				ast_channel_unlock(o->chan);
			}
			if (o->stillgoing && (o->chan) &&  (o->chan->_state == AST_STATE_UP)) {
				if (!peer) {
					ast_verb(3, "%s answered %s\n", ochan_name, inchan_name);
					if (update_connectedline) {
						if (o->connected.id.number) {
							if (ast_channel_connected_line_macro(o->chan, in, &o->connected, 1, 0)) {
								ast_channel_update_connected_line(in, &o->connected);
							}
						} else if (o->update_connectedline) {
							ast_channel_lock(o->chan);
							ast_connected_line_copy_from_caller(&connected_caller, &o->chan->cid);
							ast_channel_unlock(o->chan);
							connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
							ast_channel_update_connected_line(in, &connected_caller);
							ast_party_connected_line_free(&connected_caller);
						}
					}
					peer = o;
				}
			} else if (o->chan && (o->chan == winner)) {

				ast_copy_string(on, o->member->interface, sizeof(on));
				ast_copy_string(membername, o->member->membername, sizeof(membername));

				if (!ast_strlen_zero(o->chan->call_forward) && !forwardsallowed) {
					ast_verb(3, "Forwarding %s to '%s' prevented.\n", inchan_name, o->chan->call_forward);
					numnochan++;
					do_hang(o);
					winner = NULL;
					continue;
				} else if (!ast_strlen_zero(o->chan->call_forward)) {
					struct ast_party_redirecting *apr = &o->chan->redirecting;
					struct ast_party_connected_line *apc = &o->chan->connected;
					struct ast_channel *original = o->chan;
					char tmpchan[256];
					char *stuff;
					char *tech;

					ast_copy_string(tmpchan, o->chan->call_forward, sizeof(tmpchan));
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff++ = '\0';
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
						stuff = tmpchan;
						tech = "Local";
					}

					ast_cel_report_event(in, AST_CEL_FORWARD, NULL, o->chan->call_forward, NULL);

					/* Before processing channel, go ahead and check for forwarding */
					ast_verb(3, "Now forwarding %s to '%s/%s' (thanks to %s)\n", inchan_name, tech, stuff, ochan_name);
					/* Setup parameters */
					o->chan = ast_request(tech, in->nativeformats, in, stuff, &status);
					if (!o->chan) {
						ast_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s'\n", tech, stuff);
						o->stillgoing = 0;
						numnochan++;
					} else {
						ast_channel_lock(o->chan);
						while (ast_channel_trylock(in)) {
							CHANNEL_DEADLOCK_AVOIDANCE(o->chan);
						}
						ast_channel_inherit_variables(in, o->chan);
						ast_channel_datastore_inherit(in, o->chan);

						ast_string_field_set(o->chan, accountcode, in->accountcode);
						o->chan->cdrflags = in->cdrflags;

						ast_channel_set_redirecting(o->chan, apr);

						if (o->chan->cid.cid_rdnis)
							ast_free(o->chan->cid.cid_rdnis);
						o->chan->cid.cid_rdnis = ast_strdup(S_OR(original->cid.cid_rdnis,S_OR(in->macroexten, in->exten)));

						o->chan->cid.cid_tns = in->cid.cid_tns;

						ast_party_caller_copy(&o->chan->cid, &in->cid);
						ast_party_connected_line_copy(&o->chan->connected, apc);

						ast_channel_update_redirecting(in, apr);
						if (in->cid.cid_rdnis) {
							ast_free(in->cid.cid_rdnis);
						}
						in->cid.cid_rdnis = ast_strdup(o->chan->cid.cid_rdnis);

						update_connectedline = 1;

						if (ast_call(o->chan, tmpchan, 0)) {
							ast_log(LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
							ast_channel_unlock(o->chan);
							do_hang(o);
							numnochan++;
						} else {
							ast_channel_unlock(o->chan);
						}
						ast_channel_unlock(in);
					}
					/* Hangup the original channel now, in case we needed it */
					ast_hangup(winner);
					continue;
				}
				f = ast_read(winner);
				if (f) {
					if (f->frametype == AST_FRAME_CONTROL) {
						switch (f->subclass) {
						case AST_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								ast_verb(3, "%s answered %s\n", ochan_name, inchan_name);
								if (update_connectedline) {
									if (o->connected.id.number) {
										if (ast_channel_connected_line_macro(o->chan, in, &o->connected, 1, 0)) {
											ast_channel_update_connected_line(in, &o->connected);
										}
									} else if (o->update_connectedline) {
										ast_channel_lock(o->chan);
										ast_connected_line_copy_from_caller(&connected_caller, &o->chan->cid);
										ast_channel_unlock(o->chan);
										connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
										ast_channel_update_connected_line(in, &connected_caller);
										ast_party_connected_line_free(&connected_caller);
									}
								}
								peer = o;
							}
							break;
						case AST_CONTROL_BUSY:
							ast_verb(3, "%s is busy\n", ochan_name);
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							do_hang(o);
							endtime = (long) time(NULL);
							endtime -= starttime;
							rna(endtime * 1000, qe, on, membername, 0);
							if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								ring_one(qe, outgoing, &numbusies);
							}
							numbusies++;
							break;
						case AST_CONTROL_CONGESTION:
							ast_verb(3, "%s is circuit-busy\n", ochan_name);
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							endtime = (long) time(NULL);
							endtime -= starttime;
							rna(endtime * 1000, qe, on, membername, 0);
							do_hang(o);
							if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								ring_one(qe, outgoing, &numbusies);
							}
							numbusies++;
							break;
						case AST_CONTROL_RINGING:
							ast_verb(3, "%s is ringing\n", ochan_name);
							break;
						case AST_CONTROL_OFFHOOK:
							/* Ignore going off hook */
							break;
						case AST_CONTROL_CONNECTED_LINE:
							if (!update_connectedline) {
								ast_verb(3, "Connected line update to %s prevented.\n", inchan_name);
							} else if (qe->parent->strategy == QUEUE_STRATEGY_RINGALL) {
								struct ast_party_connected_line connected;
								ast_verb(3, "%s connected line has changed. Saving it until answer for %s\n", ochan_name, inchan_name);
								ast_party_connected_line_set_init(&connected, &o->connected);
								ast_connected_line_parse_data(f->data.ptr, f->datalen, &connected);
								ast_party_connected_line_set(&o->connected, &connected);
								ast_party_connected_line_free(&connected);
							} else {
								if (ast_channel_connected_line_macro(o->chan, in, f, 1, 1)) {
									ast_indicate_data(in, AST_CONTROL_CONNECTED_LINE, f->data.ptr, f->datalen);
								}
							}
							break;
						case AST_CONTROL_REDIRECTING:
							if (!update_connectedline) {
								ast_verb(3, "Redirecting update to %s prevented\n", inchan_name);
							} else {
								ast_verb(3, "%s redirecting info has changed, passing it to %s\n", ochan_name, inchan_name);
								ast_indicate_data(in, AST_CONTROL_REDIRECTING, f->data.ptr, f->datalen);
							}
							break;
						default:
							ast_debug(1, "Dunno what to do with control type %d\n", f->subclass);
							break;
						}
					}
					ast_frfree(f);
				} else {
					endtime = (long) time(NULL) - starttime;
					rna(endtime * 1000, qe, on, membername, 1);
					do_hang(o);
					if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
						if (qe->parent->timeoutrestart)
							*to = orig;
						ring_one(qe, outgoing, &numbusies);
					}
				}
			}
		}
		if (winner == in) {
			f = ast_read(in);
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to = -1;
				if (f) {
					if (f->data.uint32) {
						in->hangupcause = f->data.uint32;
					}
					ast_frfree(f);
				}
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && caller_disconnect && (f->subclass == '*')) {
				ast_verb(3, "User hit %c to disconnect call.\n", f->subclass);
				*to = 0;
				ast_frfree(f);
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && valid_exit(qe, f->subclass)) {
				ast_verb(3, "User pressed digit: %c\n", f->subclass);
				*to = 0;
				*digit = f->subclass;
				ast_frfree(f);
				return NULL;
			}
			ast_frfree(f);
		}
		if (!*to) {
			for (o = start; o; o = o->call_next)
				rna(orig, qe, o->interface, o->member->membername, 1);
		}
	}

#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->q_next) {
		if (epollo->chan)
			ast_poll_channel_del(in, epollo->chan);
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
		if (!ch->pending)
			idx++;
		ch = ch->next;			
	}

	ao2_unlock(qe->parent);

	/* If the queue entry is within avl [the number of available members] calls from the top ... */
	if (ch && idx < avl) {
		ast_debug(1, "It's our turn (%s).\n", qe->chan->name);
		res = 1;
	} else {
		ast_debug(1, "It's not our turn (%s).\n", qe->chan->name);
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
	int max_penalty = qe->pr->max_relative ? qe->max_penalty + qe->pr->max_value : qe->pr->max_value;
	int min_penalty = qe->pr->min_relative ? qe->min_penalty + qe->pr->min_value : qe->pr->min_value;
	char max_penalty_str[20], min_penalty_str[20]; 
	/* a relative change to the penalty could put it below 0 */
	if (max_penalty < 0)
		max_penalty = 0;
	if (min_penalty < 0)
		min_penalty = 0;
	if (min_penalty > max_penalty)
		min_penalty = max_penalty;
	snprintf(max_penalty_str, sizeof(max_penalty_str), "%d", max_penalty);
	snprintf(min_penalty_str, sizeof(min_penalty_str), "%d", min_penalty);
	pbx_builtin_setvar_helper(qe->chan, "QUEUE_MAX_PENALTY", max_penalty_str);
	pbx_builtin_setvar_helper(qe->chan, "QUEUE_MIN_PENALTY", min_penalty_str);
	qe->max_penalty = max_penalty;
	qe->min_penalty = min_penalty;
	ast_debug(3, "Setting max penalty to %d and min penalty to %d for caller %s since %d seconds have elapsed\n", qe->max_penalty, qe->min_penalty, qe->chan->name, qe->pr->time);
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

		if (is_our_turn(qe))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}

		if (qe->parent->leavewhenempty) {
			int status = 0;

			if ((status = get_member_status(qe->parent, qe->max_penalty, qe->min_penalty, qe->parent->leavewhenempty))) {
				*reason = QUEUE_LEAVEEMPTY;
				ast_queue_log(qe->parent->name, qe->chan->uniqueid, "NONE", "EXITEMPTY", "%d|%d|%ld", qe->pos, qe->opos, (long) time(NULL) - qe->start);
				leave_queue(qe);
				break;
			}
		}

		/* Make a position announcement, if enabled */
		if (qe->parent->announcefrequency &&
			(res = say_position(qe,ringing)))
			break;

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
			if (res > 0 && !valid_exit(qe, res))
				res = 0;
			else
				break;
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
		while ((qtmp = ao2_iterator_next(&queue_iter))) {
			ao2_lock(qtmp);
			if ((mem = ao2_find(qtmp->members, member, OBJ_POINTER))) {
				time(&mem->lastcall);
				mem->calls++;
				mem->lastqueue = q;
				ao2_ref(mem, -1);
			}
			ao2_unlock(qtmp);
			ao2_ref(qtmp, -1);
		}
	} else {
		ao2_lock(q);
		time(&member->lastcall);
		member->calls++;
		member->lastqueue = q;
		ao2_unlock(q);
	}	
	ao2_lock(q);
	q->callscompleted++;
	if (callcompletedinsl)
		q->callscompletedinsl++;
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
	if ((qe->max_penalty && (mem->penalty > qe->max_penalty)) || (qe->min_penalty && (mem->penalty < qe->min_penalty)))
		return -1;

	switch (q->strategy) {
	case QUEUE_STRATEGY_RINGALL:
		/* Everyone equal, except for penalty */
		tmp->metric = mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_LINEAR:
		if (pos < qe->linpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > qe->linpos)
				/* Indicate there is another priority */
				qe->linwrapped = 1;
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_RRMEMORY:
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos)
				/* Indicate there is another priority */
				q->wrapped = 1;
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_RANDOM:
		tmp->metric = ast_random() % 1000;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_WRANDOM:
		tmp->metric = ast_random() % ((1 + mem->penalty) * 1000);
		break;
	case QUEUE_STRATEGY_FEWESTCALLS:
		tmp->metric = mem->calls;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_LEASTRECENT:
		if (!mem->lastcall)
			tmp->metric = 0;
		else
			tmp->metric = 1000000 - (time(NULL) - mem->lastcall);
		tmp->metric += mem->penalty * 1000000;
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
static void send_agent_complete(const struct queue_ent *qe, const char *queuename,
	const struct ast_channel *peer, const struct member *member, time_t callstart,
	char *vars, size_t vars_len, enum agent_complete_reason rsn)
{
	const char *reason = NULL;	/* silence dumb compilers */

	if (!qe->parent->eventwhencalled)
		return;

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

	manager_event(EVENT_FLAG_AGENT, "AgentComplete",
		"Queue: %s\r\n"
		"Uniqueid: %s\r\n"
		"Channel: %s\r\n"
		"Member: %s\r\n"
		"MemberName: %s\r\n"
		"HoldTime: %ld\r\n"
		"TalkTime: %ld\r\n"
		"Reason: %s\r\n"
		"%s",
		queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
		(long)(callstart - qe->start), (long)(time(NULL) - callstart), reason,
		qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, vars_len) : "");
}

struct queue_transfer_ds {
	struct queue_ent *qe;
	struct member *member;
	time_t starttime;
	int callcompletedinsl;
};

static void queue_transfer_destroy(void *data)
{
	struct queue_transfer_ds *qtds = data;
	ast_free(qtds);
}

/*! \brief a datastore used to help correctly log attended transfers of queue callers
 */
static const struct ast_datastore_info queue_transfer_info = {
	.type = "queue_transfer",
	.chan_fixup = queue_transfer_fixup,
	.destroy = queue_transfer_destroy,
};

/*! \brief Log an attended transfer when a queue caller channel is masqueraded
 *
 * When a caller is masqueraded, we want to log a transfer. Fixup time is the closest we can come to when
 * the actual transfer occurs. This happens during the masquerade after datastores are moved from old_chan
 * to new_chan. This is why new_chan is referenced for exten, context, and datastore information.
 *
 * At the end of this, we want to remove the datastore so that this fixup function is not called on any
 * future masquerades of the caller during the current call.
 */
static void queue_transfer_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct queue_transfer_ds *qtds = data;
	struct queue_ent *qe = qtds->qe;
	struct member *member = qtds->member;
	time_t callstart = qtds->starttime;
	int callcompletedinsl = qtds->callcompletedinsl;
	struct ast_datastore *datastore;

	ast_queue_log(qe->parent->name, qe->chan->uniqueid, member->membername, "TRANSFER", "%s|%s|%ld|%ld|%d",
				new_chan->exten, new_chan->context, (long) (callstart - qe->start),
				(long) (time(NULL) - callstart), qe->opos);

	update_queue(qe->parent, member, callcompletedinsl, (time(NULL) - callstart));
	
	/* No need to lock the channels because they are already locked in ast_do_masquerade */
	if ((datastore = ast_channel_datastore_find(old_chan, &queue_transfer_info, NULL))) {
		ast_channel_datastore_remove(old_chan, datastore);
	} else {
		ast_log(LOG_WARNING, "Can't find the queue_transfer datastore.\n");
	}
}

/*! \brief mechanism to tell if a queue caller was atxferred by a queue member.
 *
 * When a caller is atxferred, then the queue_transfer_info datastore
 * is removed from the channel. If it's still there after the bridge is
 * broken, then the caller was not atxferred.
 *
 * \note Only call this with chan locked
 */
static int attended_transfer_occurred(struct ast_channel *chan)
{
	return ast_channel_datastore_find(chan, &queue_transfer_info, NULL) ? 0 : 1;
}

/*! \brief create a datastore for storing relevant info to log attended transfers in the queue_log
 */
static struct ast_datastore *setup_transfer_datastore(struct queue_ent *qe, struct member *member, time_t starttime, int callcompletedinsl)
{
	struct ast_datastore *ds;
	struct queue_transfer_ds *qtds = ast_calloc(1, sizeof(*qtds));

	if (!qtds) {
		ast_log(LOG_WARNING, "Memory allocation error!\n");
		return NULL;
	}

	ast_channel_lock(qe->chan);
	if (!(ds = ast_datastore_alloc(&queue_transfer_info, NULL))) {
		ast_channel_unlock(qe->chan);
		ast_log(LOG_WARNING, "Unable to create transfer datastore. queue_log will not show attended transfer\n");
		return NULL;
	}

	qtds->qe = qe;
	/* This member is refcounted in try_calling, so no need to add it here, too */
	qtds->member = member;
	qtds->starttime = starttime;
	qtds->callcompletedinsl = callcompletedinsl;
	ds->data = qtds;
	ast_channel_datastore_add(qe->chan, ds);
	ast_channel_unlock(qe->chan);
	return ds;
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
		ao2_lock(q);
		set_queue_variables(q, chan);
		ao2_unlock(q);
		/* This unrefs the reference we made in try_calling when we allocated qeb */
		queue_unref(q);
	}
}

/*! \brief A large function which calls members, updates statistics, and bridges the caller and a member
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
 * \param[in] options the options passed as the third parameter to the Queue() application
 * \param[in] announceoverride filename to play to user when waiting 
 * \param[in] url the url passed as the fourth parameter to the Queue() application
 * \param[in,out] tries the number of times we have tried calling queue members
 * \param[out] noption set if the call to Queue() has the 'n' option set.
 * \param[in] agi the agi passed as the fifth parameter to the Queue() application
 * \param[in] macro the macro passed as the sixth parameter to the Queue() application
 * \param[in] gosub the gosub passed as the seventh parameter to the Queue() application
 * \param[in] ringing 1 if the 'r' option is set, otherwise 0
 */
static int try_calling(struct queue_ent *qe, const char *options, char *announceoverride, const char *url, int *tries, int *noption, const char *agi, const char *macro, const char *gosub, int ringing)
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
	int ret = 0;
	const char *monitorfilename;
	const char *monitor_exec;
	const char *monitor_options;
	char tmpid[256], tmpid2[256];
	char meid[1024], meid2[1024];
	char mixmonargs[1512];
	struct ast_app *mixmonapp = NULL;
	char *p;
	char vars[2048];
	int forwardsallowed = 1;
	int update_connectedline = 1;
	int callcompletedinsl;
	struct ao2_iterator memi;
	struct ast_datastore *datastore, *transfer_ds;
	struct queue_end_bridge *queue_end_bridge = NULL;

	ast_channel_lock(qe->chan);
	datastore = ast_channel_datastore_find(qe->chan, &dialed_interface_info, NULL);
	ast_channel_unlock(qe->chan);

	memset(&bridge_config, 0, sizeof(bridge_config));
	tmpid[0] = 0;
	meid[0] = 0;
	time(&now);

	/* If we've already exceeded our timeout, then just stop
	 * This should be extremely rare. queue_exec will take care
	 * of removing the caller and reporting the timeout as the reason.
	 */
	if (qe->expire && now >= qe->expire) {
		res = 0;
		goto out;
	}
		
	for (; options && *options; options++)
		switch (*options) {
		case 't':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_REDIRECT);
			break;
		case 'T':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_REDIRECT);
			break;
		case 'w':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_AUTOMON);
			break;
		case 'W':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_AUTOMON);
			break;
		case 'c':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_NO_H_EXTEN);
			break;
		case 'd':
			nondataquality = 0;
			break;
		case 'h':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_DISCONNECT);
			break;
		case 'H':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT);
			break;
		case 'k':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_PARKCALL);
			break;
		case 'K':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_PARKCALL);
			break;
		case 'n':
			if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY || qe->parent->strategy == QUEUE_STRATEGY_LINEAR)
				(*tries)++;
			else
				*tries = qe->parent->membercount;
			*noption = 1;
			break;
		case 'i':
			forwardsallowed = 0;
			break;
		case 'I':
			update_connectedline = 0;
			break;
		case 'x':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_AUTOMIXMON);
			break;
		case 'X':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_AUTOMIXMON);
			break;
		case 'C':
			qe->cancel_answered_elsewhere = 1;
			break;
		}

	/* if the calling channel has the ANSWERED_ELSEWHERE flag set, make sure this is inherited. 
		(this is mainly to support chan_local)
	*/
	if (ast_test_flag(qe->chan, AST_FLAG_ANSWERED_ELSEWHERE)) {
		qe->cancel_answered_elsewhere = 1;
	}

	/* Hold the lock while we setup the outgoing calls */
	if (use_weight)
		ao2_lock(queues);
	ao2_lock(qe->parent);
	ast_debug(1, "%s is trying to call a queue member.\n",
							qe->chan->name);
	ast_copy_string(queuename, qe->parent->name, sizeof(queuename));
	if (!ast_strlen_zero(qe->announce))
		announce = qe->announce;
	if (!ast_strlen_zero(announceoverride))
		announce = announceoverride;

	memi = ao2_iterator_init(qe->parent->members, 0);
	while ((cur = ao2_iterator_next(&memi))) {
		struct callattempt *tmp = ast_calloc(1, sizeof(*tmp));
		struct ast_dialed_interface *di;
		AST_LIST_HEAD(, ast_dialed_interface) *dialed_interfaces;
		if (!tmp) {
			ao2_ref(cur, -1);
			ao2_unlock(qe->parent);
			if (use_weight)
				ao2_unlock(queues);
			goto out;
		}
		if (!datastore) {
			if (!(datastore = ast_datastore_alloc(&dialed_interface_info, NULL))) {
				ao2_ref(cur, -1);
				ao2_unlock(qe->parent);
				if (use_weight)
					ao2_unlock(queues);
				free(tmp);
				goto out;
			}
			datastore->inheritance = DATASTORE_INHERIT_FOREVER;
			if (!(dialed_interfaces = ast_calloc(1, sizeof(*dialed_interfaces)))) {
				ao2_ref(cur, -1);
				ao2_unlock(&qe->parent);
				if (use_weight)
					ao2_unlock(queues);
				free(tmp);
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

		ast_channel_lock(qe->chan);
		/* If any pre-existing connected line information exists on this
		 * channel, like from the CONNECTED_LINE dialplan function, use this
		 * to seed the connected line information. It may, of course, be updated
		 * during the call
		 */
		if (qe->chan->connected.id.number) {
			ast_party_connected_line_copy(&tmp->connected, &qe->chan->connected);
		}
		ast_channel_unlock(qe->chan);
		
		if (di) {
			free(tmp);
			continue;
		}

		/* It is always ok to dial a Local interface.  We only keep track of
		 * which "real" interfaces have been dialed.  The Local channel will
		 * inherit this list so that if it ends up dialing a real interface,
		 * it won't call one that has already been called. */
		if (strncasecmp(cur->interface, "Local/", 6)) {
			if (!(di = ast_calloc(1, sizeof(*di) + strlen(cur->interface)))) {
				ao2_ref(cur, -1);
				ao2_unlock(qe->parent);
				if (use_weight)
					ao2_unlock(queues);
				free(tmp);
				goto out;
			}
			strcpy(di->interface, cur->interface);

			AST_LIST_LOCK(dialed_interfaces);
			AST_LIST_INSERT_TAIL(dialed_interfaces, di, list);
			AST_LIST_UNLOCK(dialed_interfaces);
		}

		tmp->stillgoing = -1;
		tmp->member = cur;
		tmp->oldstatus = cur->status;
		tmp->lastcall = cur->lastcall;
		tmp->lastqueue = cur->lastqueue;
		tmp->update_connectedline = 1;
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
			if (outgoing->chan && (outgoing->chan->_state == AST_STATE_UP))
				break;
		} else {
			ao2_ref(cur, -1);
			ast_free(tmp);
		}
	}

	if (qe->parent->timeoutpriority == TIMEOUT_PRIORITY_APP) {
		/* Application arguments have higher timeout priority (behaviour for <=1.6) */
		if (qe->expire && (!qe->parent->timeout || (qe->expire - now) <= qe->parent->timeout))
			to = (qe->expire - now) * 1000;
		else
			to = (qe->parent->timeout) ? qe->parent->timeout * 1000 : -1;
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
	if (use_weight)
		ao2_unlock(queues);
	lpeer = wait_for_answer(qe, outgoing, &to, &digit, numbusies, ast_test_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT), forwardsallowed, update_connectedline);
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
	if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY) {
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
		if (res == -1)
			ast_debug(1, "%s: Nobody answered.\n", qe->chan->name);
		if (ast_cdr_isset_unanswered()) {
			/* channel contains the name of one of the outgoing channels
			   in its CDR; zero out this CDR to avoid a dual-posting */
			struct callattempt *o;
			for (o = outgoing; o; o = o->q_next) {
				if (!o->chan) {
					continue;
				}
				if (strcmp(o->chan->cdr->dstchannel, qe->chan->cdr->dstchannel) == 0) {
					ast_set_flag(o->chan->cdr, AST_CDR_FLAG_POST_DISABLED);
					break;
				}
			}
		}
	} else { /* peer is valid */
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the
		   conversation.  */
		if (!strcmp(qe->chan->tech->type, "DAHDI"))
			ast_channel_setoption(qe->chan, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		if (!strcmp(peer->tech->type, "DAHDI"))
			ast_channel_setoption(peer, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		/* Update parameters for the queue */
		time(&now);
		recalc_holdtime(qe, (now - qe->start));
		ao2_lock(qe->parent);
		callcompletedinsl = ((now - qe->start) <= qe->parent->servicelevel);
		ao2_unlock(qe->parent);
		member = lpeer->member;
		/* Increment the refcount for this member, since we're going to be using it for awhile in here. */
		ao2_ref(member, 1);
		hangupcalls(outgoing, peer, qe->cancel_answered_elsewhere);
		outgoing = NULL;
		if (announce || qe->parent->reportholdtime || qe->parent->memberdelay) {
			int res2;

			res2 = ast_autoservice_start(qe->chan);
			if (!res2) {
				if (qe->parent->memberdelay) {
					ast_log(LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
					res2 |= ast_safe_sleep(peer, qe->parent->memberdelay * 1000);
				}
				if (!res2 && announce) {
					play_file(peer, announce);
				}
				if (!res2 && qe->parent->reportholdtime) {
					if (!play_file(peer, qe->parent->sound_reporthold)) {
						int holdtime, holdtimesecs;

						time(&now);
						holdtime = abs((now - qe->start) / 60);
						holdtimesecs = abs((now - qe->start));
						if (holdtime == 1) {
							ast_say_number(peer, holdtime, AST_DIGIT_ANY, peer->language, NULL);
							play_file(peer, qe->parent->sound_minute);
						} else {
							ast_say_number(peer, holdtime, AST_DIGIT_ANY, peer->language, NULL);
							play_file(peer, qe->parent->sound_minutes);
						}
						if (holdtimesecs > 1) {
							ast_say_number(peer, holdtimesecs, AST_DIGIT_ANY, peer->language, NULL);
							play_file(peer, qe->parent->sound_seconds);
						}
					}
				}
			}
			res2 |= ast_autoservice_stop(qe->chan);
			if (ast_check_hangup(peer)) {
				/* Agent must have hung up */
				ast_log(LOG_WARNING, "Agent on %s hungup on the customer.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "AGENTDUMP", "%s", "");
				if (qe->parent->eventwhencalled)
					manager_event(EVENT_FLAG_AGENT, "AgentDump",
							"Queue: %s\r\n"
							"Uniqueid: %s\r\n"
							"Channel: %s\r\n"
							"Member: %s\r\n"
							"MemberName: %s\r\n"
							"%s",
							queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
							qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
				ast_hangup(peer);
				ao2_ref(member, -1);
				goto out;
			} else if (res2) {
				/* Caller must have hung up just before being connected*/
				ast_log(LOG_NOTICE, "Caller was about to talk to agent on %s but the caller hungup.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "ABANDON", "%d|%d|%ld", qe->pos, qe->opos, (long) time(NULL) - qe->start);
				record_abandoned(qe);
				ast_cdr_noanswer(qe->chan->cdr);
				ast_hangup(peer);
				ao2_ref(member, -1);
				return -1;
			}
		}
		/* Stop music on hold */
		if (ringing)
			ast_indicate(qe->chan,-1);
		else
			ast_moh_stop(qe->chan);
		/* If appropriate, log that we have a destination channel */
		if (qe->chan->cdr)
			ast_cdr_setdestchan(qe->chan->cdr, peer->name);
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(qe->chan, peer);
		if (res < 0) {
			ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "SYSCOMPAT", "%s", "");
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
			record_abandoned(qe);
			ast_cdr_failed(qe->chan->cdr);
			ast_hangup(peer);
			ao2_ref(member, -1);
			return -1;
		}

		/* Play announcement to the caller telling it's his turn if defined */
		if (!ast_strlen_zero(qe->parent->sound_callerannounce)) {
			if (play_file(qe->chan, qe->parent->sound_callerannounce))
				ast_log(LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", qe->parent->sound_callerannounce);
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
	
		/* try to set queue variables if configured to do so*/
		set_queue_variables(qe->parent, qe->chan);
		set_queue_variables(qe->parent, peer);
		ao2_unlock(qe->parent);
		
		ast_channel_lock(qe->chan);
		if ((monitorfilename = pbx_builtin_getvar_helper(qe->chan, "MONITOR_FILENAME"))) {
				monitorfilename = ast_strdupa(monitorfilename);
		}
		ast_channel_unlock(qe->chan);
		/* Begin Monitoring */
		if (qe->parent->monfmt && *qe->parent->monfmt) {
			if (!qe->parent->montype) {
				const char *monexec, *monargs;
				ast_debug(1, "Starting Monitor as requested.\n");
				ast_channel_lock(qe->chan);
				if ((monexec = pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC")) || (monargs = pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC_ARGS"))) {
					which = qe->chan;
					monexec = monexec ? ast_strdupa(monexec) : NULL;
				}
				else
					which = peer;
				ast_channel_unlock(qe->chan);
				if (monitorfilename) {
					ast_monitor_start(which, qe->parent->monfmt, monitorfilename, 1, X_REC_IN | X_REC_OUT);
				} else if (qe->chan->cdr) {
					ast_monitor_start(which, qe->parent->monfmt, qe->chan->cdr->uniqueid, 1, X_REC_IN | X_REC_OUT);
				} else {
					/* Last ditch effort -- no CDR, make up something */
					snprintf(tmpid, sizeof(tmpid), "chan-%lx", ast_random());
					ast_monitor_start(which, qe->parent->monfmt, tmpid, 1, X_REC_IN | X_REC_OUT);
				}
				if (!ast_strlen_zero(monexec)) {
					ast_monitor_setjoinfiles(which, 1);
				}
			} else {
				mixmonapp = pbx_findapp("MixMonitor");
				
				if (mixmonapp) {
					ast_debug(1, "Starting MixMonitor as requested.\n");
					if (!monitorfilename) {
						if (qe->chan->cdr)
							ast_copy_string(tmpid, qe->chan->cdr->uniqueid, sizeof(tmpid));
						else
							snprintf(tmpid, sizeof(tmpid), "chan-%lx", ast_random());
					} else {
						const char *m = monitorfilename;
						for (p = tmpid2; p < tmpid2 + sizeof(tmpid2) - 1; p++, m++) {
							switch (*m) {
							case '^':
								if (*(m + 1) == '{')
									*p = '$';
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
						if (p == tmpid2 + sizeof(tmpid2))
							tmpid2[sizeof(tmpid2) - 1] = '\0';

						pbx_substitute_variables_helper(qe->chan, tmpid2, tmpid, sizeof(tmpid) - 1);
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
						const char *m = monitor_exec;
						for (p = meid2; p < meid2 + sizeof(meid2) - 1; p++, m++) {
							switch (*m) {
							case '^':
								if (*(m + 1) == '{')
									*p = '$';
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
						if (p == meid2 + sizeof(meid2))
							meid2[sizeof(meid2) - 1] = '\0';

						pbx_substitute_variables_helper(qe->chan, meid2, meid, sizeof(meid) - 1);
					}
	
					snprintf(tmpid2, sizeof(tmpid2), "%s.%s", tmpid, qe->parent->monfmt);

					if (!ast_strlen_zero(monitor_exec))
						snprintf(mixmonargs, sizeof(mixmonargs), "%s,b%s,%s", tmpid2, monitor_options, monitor_exec);
					else
						snprintf(mixmonargs, sizeof(mixmonargs), "%s,b%s", tmpid2, monitor_options);
					
					ast_debug(1, "Arguments being passed to MixMonitor: %s\n", mixmonargs);
					/* We purposely lock the CDR so that pbx_exec does not update the application data */
					if (qe->chan->cdr)
						ast_set_flag(qe->chan->cdr, AST_CDR_FLAG_LOCKED);
					ret = pbx_exec(qe->chan, mixmonapp, mixmonargs);
					if (qe->chan->cdr)
						ast_clear_flag(qe->chan->cdr, AST_CDR_FLAG_LOCKED);

				} else {
					ast_log(LOG_WARNING, "Asked to run MixMonitor on this call, but cannot find the MixMonitor app!\n");
				}
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
			if (qe->parent->membermacro)
				macroexec = ast_strdupa(qe->parent->membermacro);
		}

		if (!ast_strlen_zero(macroexec)) {
			ast_debug(1, "app_queue: macro=%s.\n", macroexec);
			
			res = ast_autoservice_start(qe->chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}
			
			application = pbx_findapp("Macro");

			if (application) {
				res = pbx_exec(peer, application, macroexec);
				ast_debug(1, "Macro exited with status %d\n", res);
				res = 0;
			} else {
				ast_log(LOG_ERROR, "Could not find application Macro\n");
				res = -1;
			}

			if (ast_autoservice_stop(qe->chan) < 0) {
				ast_log(LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res = -1;
			}
		}

		/* run a gosub for this connection if defined. The gosub simply returns, no action is taken on the result */
		/* use gosub from dialplan if passed as a option, otherwise use the default queue gosub */
		if (!ast_strlen_zero(gosub)) {
				gosubexec = ast_strdupa(gosub);
		} else {
			if (qe->parent->membergosub)
				gosubexec = ast_strdupa(qe->parent->membergosub);
		}

		if (!ast_strlen_zero(gosubexec)) {
			ast_debug(1, "app_queue: gosub=%s.\n", gosubexec);
			
			res = ast_autoservice_start(qe->chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}
			
			application = pbx_findapp("Gosub");
			
			if (application) {
				char *gosub_args, *gosub_argstart;

				/* Set where we came from */
				ast_copy_string(peer->context, "app_queue_gosub_virtual_context", sizeof(peer->context));
				ast_copy_string(peer->exten, "s", sizeof(peer->exten));
				peer->priority = 0;

				gosub_argstart = strchr(gosubexec, ',');
				if (gosub_argstart) {
					*gosub_argstart = 0;
					if (asprintf(&gosub_args, "%s,s,1(%s)", gosubexec, gosub_argstart + 1) < 0) {
						ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
						gosub_args = NULL;
					}
					*gosub_argstart = ',';
				} else {
					if (asprintf(&gosub_args, "%s,s,1", gosubexec) < 0) {
						ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
						gosub_args = NULL;
					}
				}
				if (gosub_args) {
					res = pbx_exec(peer, application, gosub_args);
					if (!res) {
						struct ast_pbx_args args;
						memset(&args, 0, sizeof(args));
						args.no_hangup_chan = 1;
						ast_pbx_run_args(peer, &args);
					}
					ast_free(gosub_args);
					ast_debug(1, "Gosub exited with status %d\n", res);
				} else {
					ast_log(LOG_ERROR, "Could not Allocate string for Gosub arguments -- Gosub Call Aborted!\n");
				}
			} else {
				ast_log(LOG_ERROR, "Could not find application Gosub\n");
				res = -1;
			}
		
			if (ast_autoservice_stop(qe->chan) < 0) {
				ast_log(LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res = -1;
			}
		}

		if (!ast_strlen_zero(agi)) {
			ast_debug(1, "app_queue: agi=%s.\n", agi);
			application = pbx_findapp("agi");
			if (application) {
				agiexec = ast_strdupa(agi);
				ret = pbx_exec(qe->chan, application, agiexec);
			} else
				ast_log(LOG_WARNING, "Asked to execute an AGI on this channel, but could not find application (agi)!\n");
		}
		qe->handled++;
		ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "CONNECT", "%ld|%s|%ld", (long) time(NULL) - qe->start, peer->uniqueid,
													(long)(orig - to > 0 ? (orig - to) / 1000 : 0));
		if (update_cdr && qe->chan->cdr) 
			ast_copy_string(qe->chan->cdr->dstchannel, member->membername, sizeof(qe->chan->cdr->dstchannel));
		if (qe->parent->eventwhencalled)
			manager_event(EVENT_FLAG_AGENT, "AgentConnect",
					"Queue: %s\r\n"
					"Uniqueid: %s\r\n"
					"Channel: %s\r\n"
					"Member: %s\r\n"
					"MemberName: %s\r\n"
					"Holdtime: %ld\r\n"
					"BridgedChannel: %s\r\n"
					"Ringtime: %ld\r\n"
					"%s",
					queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
					(long) time(NULL) - qe->start, peer->uniqueid, (long)(orig - to > 0 ? (orig - to) / 1000 : 0),
					qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
		ast_copy_string(oldcontext, qe->chan->context, sizeof(oldcontext));
		ast_copy_string(oldexten, qe->chan->exten, sizeof(oldexten));
	
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
			queue_ref(qe->parent);
		}

		time(&callstart);
		transfer_ds = setup_transfer_datastore(qe, member, callstart, callcompletedinsl);
		bridge = ast_bridge_call(qe->chan,peer, &bridge_config);

		/* If the queue member did an attended transfer, then the TRANSFER already was logged in the queue_log
		 * when the masquerade occurred. These other "ending" queue_log messages are unnecessary
		 */
		ast_channel_lock(qe->chan);
		if (!attended_transfer_occurred(qe->chan)) {
			struct ast_datastore *tds;
			if (strcasecmp(oldcontext, qe->chan->context) || strcasecmp(oldexten, qe->chan->exten)) {
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "TRANSFER", "%s|%s|%ld|%ld|%d",
					qe->chan->exten, qe->chan->context, (long) (callstart - qe->start),
					(long) (time(NULL) - callstart), qe->opos);
				send_agent_complete(qe, queuename, peer, member, callstart, vars, sizeof(vars), TRANSFER);
			} else if (ast_check_hangup(qe->chan)) {
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "COMPLETECALLER", "%ld|%ld|%d",
					(long) (callstart - qe->start), (long) (time(NULL) - callstart), qe->opos);
				send_agent_complete(qe, queuename, peer, member, callstart, vars, sizeof(vars), CALLER);
			} else {
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "COMPLETEAGENT", "%ld|%ld|%d",
					(long) (callstart - qe->start), (long) (time(NULL) - callstart), qe->opos);
				send_agent_complete(qe, queuename, peer, member, callstart, vars, sizeof(vars), AGENT);
			}
			if ((tds = ast_channel_datastore_find(qe->chan, &queue_transfer_info, NULL))) {	
				ast_channel_datastore_remove(qe->chan, tds);
			}
			update_queue(qe->parent, member, callcompletedinsl, (time(NULL) - callstart));
		}

		if (transfer_ds) {
			ast_datastore_free(transfer_ds);
		}
		ast_channel_unlock(qe->chan);
		ast_hangup(peer);
		res = bridge ? bridge : 1;
		ao2_ref(member, -1);
	}
out:
	hangupcalls(outgoing, NULL, qe->cancel_answered_elsewhere);

	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	/* Don't need to hold the lock while we setup the outgoing calls */
	int retrywait = qe->parent->retry * 1000;

	int res = ast_waitfordigit(qe->chan, retrywait);
	if (res > 0 && !valid_exit(qe, res))
		res = 0;

	return res;
}

static struct member *interface_exists(struct call_queue *q, const char *interface)
{
	struct member *mem;
	struct ao2_iterator mem_iter;

	if (!q)
		return NULL;

	mem_iter = ao2_iterator_init(q->members, 0);
	while ((mem = ao2_iterator_next(&mem_iter))) {
		if (!strcasecmp(interface, mem->interface))
			return mem;
		ao2_ref(mem, -1);
	}

	return NULL;
}


/*! \brief Dump all members in a specific queue to the database
 *
 * <pm_family>/<queuename> = <interface>;<penalty>;<paused>;<state_interface>[|...]
 */
static void dump_queue_members(struct call_queue *pm_queue)
{
	struct member *cur_member;
	char value[PM_MAX_LEN];
	int value_len = 0;
	int res;
	struct ao2_iterator mem_iter;

	memset(value, 0, sizeof(value));

	if (!pm_queue)
		return;

	mem_iter = ao2_iterator_init(pm_queue->members, 0);
	while ((cur_member = ao2_iterator_next(&mem_iter))) {
		if (!cur_member->dynamic) {
			ao2_ref(cur_member, -1);
			continue;
		}

		res = snprintf(value + value_len, sizeof(value) - value_len, "%s%s;%d;%d;%s;%s",
			value_len ? "|" : "", cur_member->interface, cur_member->penalty, cur_member->paused, cur_member->membername, cur_member->state_interface);

		ao2_ref(cur_member, -1);

		if (res != strlen(value + value_len)) {
			ast_log(LOG_WARNING, "Could not create persistent member string, out of space\n");
			break;
		}
		value_len += res;
	}
	
	if (value_len && !cur_member) {
		if (ast_db_put(pm_family, pm_queue->name, value))
			ast_log(LOG_WARNING, "failed to create persistent dynamic entry!\n");
	} else
		/* Delete the entry if the queue is empty or there is an error */
		ast_db_del(pm_family, pm_queue->name);
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
	if ((q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
		ao2_lock(queues);
		ao2_lock(q);
		if ((mem = ao2_find(q->members, &tmpmem, OBJ_POINTER))) {
			/* XXX future changes should beware of this assumption!! */
			if (!mem->dynamic) {
				ao2_ref(mem, -1);
				ao2_unlock(q);
				queue_unref(q);
				ao2_unlock(queues);
				return RES_NOT_DYNAMIC;
			}
			q->membercount--;
			manager_event(EVENT_FLAG_AGENT, "QueueMemberRemoved",
				"Queue: %s\r\n"
				"Location: %s\r\n"
				"MemberName: %s\r\n",
				q->name, mem->interface, mem->membername);
			ao2_unlink(q->members, mem);
			ao2_ref(mem, -1);

			if (queue_persistent_members)
				dump_queue_members(q);
			
			res = RES_OKAY;
		} else {
			res = RES_EXISTS;
		}
		ao2_unlock(q);
		ao2_unlock(queues);
		queue_unref(q);
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
	if (!(q = load_realtime_queue(queuename)))
		return res;

	ao2_lock(queues);

	ao2_lock(q);
	if ((old_member = interface_exists(q, interface)) == NULL) {
		if ((new_member = create_queue_member(interface, membername, penalty, paused, state_interface))) {
			new_member->dynamic = 1;
			ao2_link(q->members, new_member);
			q->membercount++;
			manager_event(EVENT_FLAG_AGENT, "QueueMemberAdded",
				"Queue: %s\r\n"
				"Location: %s\r\n"
				"MemberName: %s\r\n"
				"Membership: %s\r\n"
				"Penalty: %d\r\n"
				"CallsTaken: %d\r\n"
				"LastCall: %d\r\n"
				"Status: %d\r\n"
				"Paused: %d\r\n",
				q->name, new_member->interface, new_member->membername,
				"dynamic",
				new_member->penalty, new_member->calls, (int) new_member->lastcall,
				new_member->status, new_member->paused);
			
			ao2_ref(new_member, -1);
			new_member = NULL;

			if (dump)
				dump_queue_members(q);
			
			res = RES_OKAY;
		} else {
			res = RES_OUTOFMEMORY;
		}
	} else {
		ao2_ref(old_member, -1);
		res = RES_EXISTS;
	}
	ao2_unlock(q);
	ao2_unlock(queues);

	return res;
}

static int set_member_paused(const char *queuename, const char *interface, const char *reason, int paused)
{
	int found = 0;
	struct call_queue *q;
	struct member *mem;
	struct ao2_iterator queue_iter;
	int failed;

	/* Special event for when all queues are paused - individual events still generated */
	/* XXX In all other cases, we use the membername, but since this affects all queues, we cannot */
	if (ast_strlen_zero(queuename))
		ast_queue_log("NONE", "NONE", interface, (paused ? "PAUSEALL" : "UNPAUSEALL"), "%s", "");

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
		ao2_lock(q);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename)) {
			if ((mem = interface_exists(q, interface))) {
				if (mem->paused == paused) {
					ast_debug(1, "%spausing already-%spaused queue member %s:%s\n", (paused ? "" : "un"), (paused ? "" : "un"), q->name, interface);
				}

				failed = 0;
				if (mem->realtime) {
					failed = update_realtime_member_field(mem, q->name, "paused", paused ? "1" : "0");
				}
			
				if (failed) {
					ast_log(LOG_WARNING, "Failed %spausing realtime queue member %s:%s\n", (paused ? "" : "un"), q->name, interface);
					ao2_ref(mem, -1);
					ao2_unlock(q);
					continue;
				}	
				found++;
				mem->paused = paused;

				if (queue_persistent_members)
					dump_queue_members(q);

				ast_queue_log(q->name, "NONE", mem->membername, (paused ? "PAUSE" : "UNPAUSE"), "%s", S_OR(reason, ""));
				
				if (!ast_strlen_zero(reason)) {
					manager_event(EVENT_FLAG_AGENT, "QueueMemberPaused",
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"MemberName: %s\r\n"
						"Paused: %d\r\n"
						"Reason: %s\r\n",
							q->name, mem->interface, mem->membername, paused, reason);
				} else {
					manager_event(EVENT_FLAG_AGENT, "QueueMemberPaused",
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"MemberName: %s\r\n"
						"Paused: %d\r\n",
							q->name, mem->interface, mem->membername, paused);
				}
				ao2_ref(mem, -1);
			}
		}
		
		if (!ast_strlen_zero(queuename) && !strcasecmp(queuename, q->name)) {
			ao2_unlock(q);
			queue_unref(q);
			break;
		}
		
		ao2_unlock(q);
		queue_unref(q);
	}

	return found ? RESULT_SUCCESS : RESULT_FAILURE;
}

/* \brief Sets members penalty, if queuename=NULL we set member penalty in all the queues. */
static int set_member_penalty(const char *queuename, const char *interface, int penalty)
{
	int foundinterface = 0, foundqueue = 0;
	struct call_queue *q;
	struct member *mem;
	struct ao2_iterator queue_iter;

	if (penalty < 0) {
		ast_log(LOG_ERROR, "Invalid penalty (%d)\n", penalty);
		return RESULT_FAILURE;
	}

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
		ao2_lock(q);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename)) {
			foundqueue++;
			if ((mem = interface_exists(q, interface))) {
				foundinterface++;
				mem->penalty = penalty;
				
				ast_queue_log(q->name, "NONE", interface, "PENALTY", "%d", penalty);
				manager_event(EVENT_FLAG_AGENT, "QueueMemberPenalty",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"Penalty: %d\r\n",
					q->name, mem->interface, penalty);
				ao2_ref(mem, -1);
			}
		}
		ao2_unlock(q);
		queue_unref(q);
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
	
	if ((q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
		foundqueue = 1;
		ao2_lock(q);
		if ((mem = interface_exists(q, interface))) {
			penalty = mem->penalty;
			ao2_ref(mem, -1);
			ao2_unlock(q);
			queue_unref(q);
			return penalty;
		}
		ao2_unlock(q);
		queue_unref(q);
	}

	/* some useful debuging */
	if (foundqueue) 
		ast_log (LOG_ERROR, "Invalid queuename\n");
	else 
		ast_log (LOG_ERROR, "Invalid interface\n");

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
	char queue_data[PM_MAX_LEN];

	ao2_lock(queues);

	/* Each key in 'pm_family' is the name of a queue */
	db_tree = ast_db_gettree(pm_family, NULL);
	for (entry = db_tree; entry; entry = entry->next) {

		queue_name = entry->key + strlen(pm_family) + 2;

		{
			struct call_queue tmpq = {
				.name = queue_name,
			};
			cur_queue = ao2_find(queues, &tmpq, OBJ_POINTER);
		}	

		if (!cur_queue)
			cur_queue = load_realtime_queue(queue_name);

		if (!cur_queue) {
			/* If the queue no longer exists, remove it from the
			 * database */
			ast_log(LOG_WARNING, "Error loading persistent queue: '%s': it does not exist\n", queue_name);
			ast_db_del(pm_family, queue_name);
			continue;
		} 

		if (ast_db_get(pm_family, queue_name, queue_data, PM_MAX_LEN)) {
			queue_unref(cur_queue);
			continue;
		}

		cur_ptr = queue_data;
		while ((member = strsep(&cur_ptr, ",|"))) {
			if (ast_strlen_zero(member))
				continue;

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
		queue_unref(cur_queue);
	}

	ao2_unlock(queues);
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
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename[,interface[,options]])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(chan->name);
		temppos = strrchr(args.interface, '-');
		if (temppos)
			*temppos = '\0';
	}

	ast_debug(1, "queue: %s, member: %s\n", args.queuename, args.interface);

	switch (remove_from_queue(args.queuename, args.interface)) {
	case RES_OKAY:
		ast_queue_log(args.queuename, chan->uniqueid, args.interface, "REMOVEMEMBER", "%s", "");
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
		args.interface = ast_strdupa(chan->name);
		temppos = strrchr(args.interface, '-');
		if (temppos)
			*temppos = '\0';
	}

	if (!ast_strlen_zero(args.penalty)) {
		if ((sscanf(args.penalty, "%d", &penalty) != 1) || penalty < 0) {
			ast_log(LOG_WARNING, "Penalty '%s' is invalid, must be an integer >= 0\n", args.penalty);
			penalty = 0;
		}
	}

	switch (add_to_queue(args.queuename, args.interface, args.membername, penalty, 0, queue_persistent_members, args.state_interface)) {
	case RES_OKAY:
		ast_queue_log(args.queuename, chan->uniqueid, args.interface, "ADDMEMBER", "%s", "");
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
		ast_log(LOG_ERROR, "Out of memory adding member %s to queue %s\n", args.interface, args.queuename);
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
		if (!strcasecmp(rl_iter->name, tmp))
			break;
	}
	if (rl_iter) {
		AST_LIST_TRAVERSE(&rl_iter->rules, pr_iter, list) {
			struct penalty_rule *new_pr = ast_calloc(1, sizeof(*new_pr));
			if (!new_pr) {
				ast_log(LOG_ERROR, "Memory allocation error when copying penalty rules! Aborting!\n");
				AST_LIST_UNLOCK(&rule_lists);
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
	struct queue_ent qe;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Queue requires an argument: queuename[,options[,URL[,announceoverride[,timeout[,agi[,macro[,gosub[,rule[,position]]]]]]]]]\n");
		return -1;
	}
	
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	/* Setup our queue entry */
	memset(&qe, 0, sizeof(qe));
	qe.start = time(NULL);

	/* set the expire time based on the supplied timeout; */
	if (!ast_strlen_zero(args.queuetimeoutstr))
		qe.expire = qe.start + atoi(args.queuetimeoutstr);
	else
		qe.expire = 0;

	/* Get the priority from the variable ${QUEUE_PRIO} */
	ast_channel_lock(chan);
	user_priority = pbx_builtin_getvar_helper(chan, "QUEUE_PRIO");
	if (user_priority) {
		if (sscanf(user_priority, "%d", &prio) == 1) {
			ast_debug(1, "%s: Got priority %d from ${QUEUE_PRIO}.\n", chan->name, prio);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
				user_priority, chan->name);
			prio = 0;
		}
	} else {
		ast_debug(3, "NO QUEUE_PRIO variable found. Using default.\n");
		prio = 0;
	}

	/* Get the maximum penalty from the variable ${QUEUE_MAX_PENALTY} */

	if ((max_penalty_str = pbx_builtin_getvar_helper(chan, "QUEUE_MAX_PENALTY"))) {
		if (sscanf(max_penalty_str, "%d", &max_penalty) == 1) {
			ast_debug(1, "%s: Got max penalty %d from ${QUEUE_MAX_PENALTY}.\n", chan->name, max_penalty);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_MAX_PENALTY}: Invalid value (%s), channel %s.\n",
				max_penalty_str, chan->name);
			max_penalty = 0;
		}
	} else {
		max_penalty = 0;
	}

	if ((min_penalty_str = pbx_builtin_getvar_helper(chan, "QUEUE_MIN_PENALTY"))) {
		if (sscanf(min_penalty_str, "%d", &min_penalty) == 1) {
			ast_debug(1, "%s: Got min penalty %d from ${QUEUE_MIN_PENALTY}.\n", chan->name, min_penalty);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_MIN_PENALTY}: Invalid value (%s), channel %s.\n",
				min_penalty_str, chan->name);
			min_penalty = 0;
		}
	} else {
		min_penalty = 0;
	}
	ast_channel_unlock(chan);

	if (args.options && (strchr(args.options, 'r')))
		ringing = 1;

	if (args.options && (strchr(args.options, 'c')))
		qcontinue = 1;

	if (args.position) {
		position = atoi(args.position);
		if (position < 0) {
			ast_log(LOG_WARNING, "Invalid position '%s' given for call to queue '%s'. Assuming no preference for position\n", args.position, args.queuename);
			position = 0;
		}
	}

	ast_debug(1, "queue: %s, options: %s, url: %s, announce: %s, expires: %ld, priority: %d\n",
		args.queuename, args.options, args.url, args.announceoverride, (long)qe.expire, prio);

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
	ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", S_OR(args.url, ""),
		S_OR(chan->cid.cid_num, ""));
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
			ast_cdr_noanswer(qe.chan->cdr);
			reason = QUEUE_TIMEOUT;
			res = 0;
			ast_queue_log(args.queuename, chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d|%d|%ld", 
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
		if (qe.parent->periodicannouncefrequency)
			if ((res = say_periodic_announcement(&qe,ringing)))
				goto stop;
	
		/* Leave if we have exceeded our queuetimeout */
		if (qe.expire && (time(NULL) >= qe.expire)) {
			record_abandoned(&qe);
			ast_cdr_noanswer(qe.chan->cdr);
			reason = QUEUE_TIMEOUT;
			res = 0;
			ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
			break;
		}

		/* see if we need to move to the next penalty level for this queue */
		while (qe.pr && ((time(NULL) - qe.start) > qe.pr->time)) {
			update_qe_rule(&qe);
		}

		/* Try calling all queue members for 'timeout' seconds */
		res = try_calling(&qe, args.options, args.announceoverride, args.url, &tries, &noption, args.agi, args.macro, args.gosub, ringing);
		if (res) {
			goto stop;
		}

		if (qe.parent->leavewhenempty) {
			int status = 0;
			if ((status = get_member_status(qe.parent, qe.max_penalty, qe.min_penalty, qe.parent->leavewhenempty))) {
				record_abandoned(&qe);
				ast_cdr_noanswer(qe.chan->cdr);
				reason = QUEUE_LEAVEEMPTY;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITEMPTY", "%d|%d|%ld", qe.pos, qe.opos, (long)(time(NULL) - qe.start));
				res = 0;
				break;
			}
		}

		/* exit after 'timeout' cycle if 'n' option enabled */
		if (noption && tries >= qe.parent->membercount) {
			ast_verb(3, "Exiting on time-out cycle\n");
			ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
			record_abandoned(&qe);
			ast_cdr_noanswer(qe.chan->cdr);
			reason = QUEUE_TIMEOUT;
			res = 0;
			break;
		}

		
		/* Leave if we have exceeded our queuetimeout */
		if (qe.expire && (time(NULL) >= qe.expire)) {
			record_abandoned(&qe);
			ast_cdr_noanswer(qe.chan->cdr);
			reason = QUEUE_TIMEOUT;
			res = 0;
			ast_queue_log(qe.parent->name, qe.chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d|%d|%ld", qe.pos, qe.opos, (long) time(NULL) - qe.start);
			break;
		}

		/* If using dynamic realtime members, we should regenerate the member list for this queue */
		update_realtime_members(qe.parent);
		/* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
		res = wait_a_bit(&qe);
		if (res)
			goto stop;

		/* Since this is a priority queue and
		 * it is not sure that we are still at the head
		 * of the queue, go and check for our turn again.
		 */
		if (!is_our_turn(&qe)) {
			ast_debug(1, "Darn priorities, going back in queue (%s)!\n", qe.chan->name);
			goto check_turns;
		}
	}

stop:
	if (res) {
		if (res < 0) {
			if (!qe.handled) {
				record_abandoned(&qe);
				ast_cdr_noanswer(qe.chan->cdr);
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ABANDON",
					"%d|%d|%ld", qe.pos, qe.opos,
					(long) time(NULL) - qe.start);
				res = -1;
			} else if (qcontinue) {
				reason = QUEUE_CONTINUE;
				res = 0;
			}
		} else if (qe.valid_digits) {
			ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY",
				"%s|%d", qe.digits, qe.pos);
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

	if ((q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
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
		queue_unref(q);
	} else {
		ast_log(LOG_WARNING, "queue %s was not found\n", data);
	}

	snprintf(buf, len, "%d", res);

	return 0;
}

/*! 
 * \brief Get number either busy / free or total members of a specific queue
 * \retval number of members (busy / free / total)
 * \retval -1 on error
*/
static int queue_function_qac(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct member *m;
	struct ao2_iterator mem_iter;
	struct call_queue *q;
	char *option;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	if ((option = strchr(data, ',')))
		*option++ = '\0';
	else
		option = "logged";
	if ((q = load_realtime_queue(data))) {
		ao2_lock(q);
		if (!strcasecmp(option, "logged")) {
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((m = ao2_iterator_next(&mem_iter))) {
				/* Count the agents who are logged in and presently answering calls */
				if ((m->status != AST_DEVICE_UNAVAILABLE) && (m->status != AST_DEVICE_INVALID)) {
					count++;
				}
				ao2_ref(m, -1);
			}
		} else if (!strcasecmp(option, "free")) {
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((m = ao2_iterator_next(&mem_iter))) {
				/* Count the agents who are logged in and presently answering calls */
				if ((m->status == AST_DEVICE_NOT_INUSE) && (!m->paused)) {
					count++;
				}
				ao2_ref(m, -1);
			}
		} else /* must be "count" */
			count = q->membercount;
		ao2_unlock(q);
		queue_unref(q);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	snprintf(buf, len, "%d", count);

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
	
	if ((q = load_realtime_queue(data))) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			/* Count the agents who are logged in and presently answering calls */
			if ((m->status != AST_DEVICE_UNAVAILABLE) && (m->status != AST_DEVICE_INVALID)) {
				count++;
			}
			ao2_ref(m, -1);
		}
		ao2_unlock(q);
		queue_unref(q);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	snprintf(buf, len, "%d", count);

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

	if ((q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
		ao2_lock(q);
		count = q->count;
		ao2_unlock(q);
		queue_unref(q);
	} else if ((var = ast_load_realtime("queues", "name", data, SENTINEL))) {
		/* if the queue is realtime but was not found in memory, this
		 * means that the queue had been deleted from memory since it was 
		 * "dead." This means it has a 0 waiting count
		 */
		count = 0;
		ast_variables_destroy(var);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

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

	if ((q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
		int buflen = 0, count = 0;
		struct ao2_iterator mem_iter = ao2_iterator_init(q->members, 0);

		ao2_lock(q);
		while ((m = ao2_iterator_next(&mem_iter))) {
			/* strcat() is always faster than printf() */
			if (count++) {
				strncat(buf + buflen, ",", len - buflen - 1);
				buflen++;
			}
			strncat(buf + buflen, m->membername, len - buflen - 1);
			buflen += strlen(m->membername);
			/* Safeguard against overflow (negative length) */
			if (buflen >= len - 2) {
				ao2_ref(m, -1);
				ast_log(LOG_WARNING, "Truncating list\n");
				break;
			}
			ao2_ref(m, -1);
		}
		ao2_unlock(q);
		queue_unref(q);
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
	
	if (penalty >= 0) /* remember that buf is already '\0' */
		snprintf (buf, len, "%d", penalty);

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
	if (set_member_penalty(args.queuename, args.interface, penalty)) {
		ast_log(LOG_ERROR, "Invalid interface, queue or penalty\n");
		return -1;
	}

	return 0;
}

static struct ast_custom_function queuevar_function = {
	.name = "QUEUE_VARIABLES",
	.read = queue_function_var,
};

static struct ast_custom_function queuemembercount_function = {
	.name = "QUEUE_MEMBER",
	.read = queue_function_qac,
};

static struct ast_custom_function queuemembercount_dep = {
	.name = "QUEUE_MEMBER_COUNT",
	.read = queue_function_qac_dep,
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
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	
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
		if (!(new_rl = ast_calloc(1, sizeof(*new_rl)))) {
			AST_LIST_UNLOCK(&rule_lists);
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
	AST_LIST_UNLOCK(&rule_lists);

	ast_config_destroy(cfg);

	return AST_MODULE_LOAD_SUCCESS;
}

/*! Set the global queue parameters as defined in the "general" section of queues.conf */
static void queue_set_global_params(struct ast_config *cfg)
{
	const char *general_val = NULL;
	queue_persistent_members = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "persistentmembers")))
		queue_persistent_members = ast_true(general_val);
	autofill_default = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "autofill")))
		autofill_default = ast_true(general_val);
	montype_default = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "monitor-type"))) {
		if (!strcasecmp(general_val, "mixmonitor"))
			montype_default = 1;
	}
	update_cdr = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "updatecdr")))
		update_cdr = ast_true(general_val);
	shared_lastcall = 0;
	if ((general_val = ast_variable_retrieve(cfg, "general", "shared_lastcall")))
		shared_lastcall = ast_true(general_val);
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
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(interface);
		AST_APP_ARG(penalty);
		AST_APP_ARG(membername);
		AST_APP_ARG(state_interface);
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

	/* Find the old position in the list */
	ast_copy_string(tmpmem.interface, interface, sizeof(tmpmem.interface));
	cur = ao2_find(q->members, &tmpmem, OBJ_POINTER | OBJ_UNLINK);
	if ((newm = create_queue_member(interface, membername, penalty, cur ? cur->paused : 0, state_interface))) {
		ao2_link(q->members, newm);
		ao2_ref(newm, -1);
	}
	newm = NULL;

	if (cur) {
		ao2_ref(cur, -1);
	} else {
		q->membercount++;
	}
}

static int mark_member_dead(void *obj, void *arg, int flags)
{
	struct member *member = obj;
	if (!member->dynamic) {
		member->delme = 1;
	}
	return 0;
}

static int kill_dead_members(void *obj, void *arg, int flags)
{
	struct member *member = obj;
	struct call_queue *q = arg;

	if (!member->delme) {
		if (member->dynamic) {
			/* dynamic members were not counted toward the member count
			 * when reloading members from queues.conf, so we do that here
			 */
			q->membercount++;
		}
		member->status = ast_device_state(member->state_interface);
		return 0;
	} else {
		q->membercount--;
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
	if (!(q = ao2_find(queues, &tmpq, OBJ_POINTER))) {
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
		queue_unref(q);
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
		q->membercount = 0;
		ao2_callback(q->members, OBJ_NODATA, mark_member_dead, NULL);
	}
	for (var = ast_variable_browse(cfg, queuename); var; var = var->next) {
		if (member_reload && !strcasecmp(var->name, "member")) {
			reload_single_member(var->value, q);
		} else if (queue_reload) {
			queue_set_param(q, var->name, var->value, var->lineno, 1);
		}
	}
	/* At this point, we've determined if the queue has a weight, so update use_weight
	 * as appropriate
	 */
	if (!q->weight && prev_weight) {
		ast_atomic_fetchadd_int(&use_weight, -1);
	}
	else if (q->weight && !prev_weight) {
		ast_atomic_fetchadd_int(&use_weight, +1);
	}

	/* Free remaining members marked as delme */
	if (member_reload) {
		ao2_callback(q->members, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, kill_dead_members, q);
	}

	if (new) {
		ao2_link(queues, q);
	} else {
		ao2_unlock(q);
	}
	queue_unref(q);
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
		ao2_callback(queues, OBJ_NODATA, mark_dead_and_unfound, (char *) queuename);
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
		ao2_callback(queues, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, kill_dead_queues, (char *) queuename);
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
	struct ao2_iterator queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
		ao2_lock(q);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename))
			clear_queue(q);
		ao2_unlock(q);
	}
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
	if (s)
		astman_append(s, "%s\r\n", str);
	else
		ast_cli(fd, "%s\n", str);
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

	if (argc != 2 && argc != 3)
		return CLI_SHOWUSAGE;

	if (argc == 3)	{ /* specific queue */
		if ((q = load_realtime_queue(argv[2]))) {
			queue_unref(q);
		}
	} else if (ast_check_realtime("queues")) {
		/* This block is to find any queues which are defined in realtime but
		 * which have not yet been added to the in-core container
		 */
		struct ast_config *cfg = ast_load_realtime_multientry("queues", "name LIKE", "%", SENTINEL);
		char *queuename;
		if (cfg) {
			for (queuename = ast_category_browse(cfg, NULL); !ast_strlen_zero(queuename); queuename = ast_category_browse(cfg, queuename)) {
				if ((q = load_realtime_queue(queuename))) {
					queue_unref(q);
				}
			}
			ast_config_destroy(cfg);
		}
	}

	queue_iter = ao2_iterator_init(queues, F_AO2I_DONTLOCK);
	ao2_lock(queues);
	while ((q = ao2_iterator_next(&queue_iter))) {
		float sl;
		struct call_queue *realtime_queue = NULL;

		ao2_lock(q);
		/* This check is to make sure we don't print information for realtime
		 * queues which have been deleted from realtime but which have not yet
		 * been deleted from the in-core container
		 */
		if (q->realtime && !(realtime_queue = load_realtime_queue(q->name))) {
			ao2_unlock(q);
			queue_unref(q);
			continue;
		} else if (q->realtime) {
			queue_unref(realtime_queue);
		}
		if (argc == 3 && strcasecmp(q->name, argv[2])) {
			ao2_unlock(q);
			queue_unref(q);
			continue;
		}
		found = 1;

		ast_str_set(&out, 0, "%-12.12s has %d calls (max ", q->name, q->count);
		if (q->maxlen)
			ast_str_append(&out, 0, "%d", q->maxlen);
		else
			ast_str_append(&out, 0, "unlimited");
		sl = 0;
		if (q->callscompleted > 0)
			sl = 100 * ((float) q->callscompletedinsl / (float) q->callscompleted);
		ast_str_append(&out, 0, ") in '%s' strategy (%ds holdtime, %ds talktime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds",
			int2strat(q->strategy), q->holdtime, q->talktime, q->weight,
			q->callscompleted, q->callsabandoned,sl,q->servicelevel);
		do_print(s, fd, ast_str_buffer(out));
		if (!ao2_container_count(q->members))
			do_print(s, fd, "   No Members");
		else {
			struct member *mem;

			do_print(s, fd, "   Members: ");
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((mem = ao2_iterator_next(&mem_iter))) {
				ast_str_set(&out, 0, "      %s", mem->membername);
				if (strcasecmp(mem->membername, mem->interface)) {
					ast_str_append(&out, 0, " (%s)", mem->interface);
				}
				if (mem->penalty)
					ast_str_append(&out, 0, " with penalty %d", mem->penalty);
				ast_str_append(&out, 0, "%s%s%s (%s)",
					mem->dynamic ? " (dynamic)" : "",
					mem->realtime ? " (realtime)" : "",
					mem->paused ? " (paused)" : "",
					ast_devstate2str(mem->status));
				if (mem->calls)
					ast_str_append(&out, 0, " has taken %d calls (last was %ld secs ago)",
						mem->calls, (long) (time(NULL) - mem->lastcall));
				else
					ast_str_append(&out, 0, " has taken no calls yet");
				do_print(s, fd, ast_str_buffer(out));
				ao2_ref(mem, -1);
			}
		}
		if (!q->head)
			do_print(s, fd, "   No Callers");
		else {
			struct queue_ent *qe;
			int pos = 1;

			do_print(s, fd, "   Callers: ");
			for (qe = q->head; qe; qe = qe->next) {
				ast_str_set(&out, 0, "      %d. %s (wait: %ld:%2.2ld, prio: %d)",
					pos++, qe->chan->name, (long) (now - qe->start) / 60,
					(long) (now - qe->start) % 60, qe->prio);
				do_print(s, fd, ast_str_buffer(out));
			}
		}
		do_print(s, fd, "");	/* blank line between entries */
		ao2_unlock(q);
		queue_unref(q); /* Unref the iterator's reference */
	}
	ao2_unlock(queues);
	if (!found) {
		if (argc == 3)
			ast_str_set(&out, 0, "No such queue: %s.", argv[2]);
		else
			ast_str_set(&out, 0, "No queues.");
		do_print(s, fd, ast_str_buffer(out));
	}
	return CLI_SUCCESS;
}

static char *complete_queue(const char *line, const char *word, int pos, int state)
{
	struct call_queue *q;
	char *ret = NULL;
	int which = 0;
	int wordlen = strlen(word);
	struct ao2_iterator queue_iter;

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
		if (!strncasecmp(word, q->name, wordlen) && ++which > state) {
			ret = ast_strdup(q->name);
			queue_unref(q);
			break;
		}
		queue_unref(q);
	}

	return ret;
}

static char *complete_queue_show(const char *line, const char *word, int pos, int state)
{
	if (pos == 2)
		return complete_queue(line, word, pos, state);
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
	struct rule_list *rl_iter;
	struct penalty_rule *pr_iter;

	AST_LIST_LOCK(&rule_lists);
	AST_LIST_TRAVERSE(&rule_lists, rl_iter, list) {
		if (ast_strlen_zero(rule) || !strcasecmp(rule, rl_iter->name)) {
			astman_append(s, "RuleList: %s\r\n", rl_iter->name);
			AST_LIST_TRAVERSE(&rl_iter->rules, pr_iter, list) {
				astman_append(s, "Rule: %d,%s%d,%s%d\r\n", pr_iter->time, pr_iter->max_relative && pr_iter->max_value >= 0 ? "+" : "", pr_iter->max_value, pr_iter->min_relative && pr_iter->min_value >= 0 ? "+" : "", pr_iter->min_value );
			}
			if (!ast_strlen_zero(rule))
				break;
		}
	}
	AST_LIST_UNLOCK(&rule_lists);

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
	if (!ast_strlen_zero(id))
		snprintf(idText, 256, "ActionID: %s\r\n", id);
	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
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
					if (((mem->status == AST_DEVICE_NOT_INUSE) || (mem->status == AST_DEVICE_UNKNOWN)) && !(mem->paused)) {
						++qmemavail;
					}
				}
				ao2_ref(mem, -1);
			}
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
		queue_unref(q);
	}
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
	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
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
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %d\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n"
						"%s"
						"\r\n",
						q->name, mem->membername, mem->interface, mem->dynamic ? "dynamic" : "static",
						mem->penalty, mem->calls, (int)mem->lastcall, mem->status, mem->paused, idText);
				}
				ao2_ref(mem, -1);
			}
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
					"Wait: %ld\r\n"
					"%s"
					"\r\n",
					q->name, pos++, qe->chan->name, qe->chan->uniqueid,
					S_OR(qe->chan->cid.cid_num, "unknown"),
					S_OR(qe->chan->cid.cid_name, "unknown"),
					(long) (now - qe->start), idText);
			}
		}
		ao2_unlock(q);
		queue_unref(q);
	}

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

	if (ast_strlen_zero(penalty_s))
		penalty = 0;
	else if (sscanf(penalty_s, "%d", &penalty) != 1 || penalty < 0)
		penalty = 0;

	if (ast_strlen_zero(paused_s))
		paused = 0;
	else
		paused = abs(ast_true(paused_s));

	switch (add_to_queue(queuename, interface, membername, penalty, paused, queue_persistent_members, state_interface)) {
	case RES_OKAY:
		ast_queue_log(queuename, "MANAGER", interface, "ADDMEMBER", "%s", "");
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

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");

	if (ast_strlen_zero(queuename) || ast_strlen_zero(interface)) {
		astman_send_error(s, m, "Need 'Queue' and 'Interface' parameters.");
		return 0;
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		ast_queue_log(queuename, "MANAGER", interface, "REMOVEMEMBER", "%s", "");
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

	if (set_member_paused(queuename, interface, reason, paused))
		astman_send_error(s, m, "Interface not found");
	else
		astman_send_ack(s, m, paused ? "Interface paused successfully" : "Interface unpaused successfully");
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
		return complete_queue(line, word, pos, state);
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

	if (set_member_penalty((char *)queuename, (char *)interface, penalty))
		astman_send_error(s, m, "Invalid interface, queuename or penalty");
	else
		astman_send_ack(s, m, "Interface penalty set successfully");

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
			"Usage: queue add member <channel> to <queue> [[[penalty <penalty>] as <membername>] state_interface <interface>]\n"
			"       Add a channel to a queue with optionally:  a penalty, membername and a state_interface\n";
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
		if (sscanf(a->argv[7], "%d", &penalty) == 1) {
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
		ast_queue_log(queuename, "CLI", interface, "ADDMEMBER", "%s", "");
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
	if (pos > 5 || pos < 3)
		return NULL;
	if (pos == 4)   /* only one possible match, 'from' */
		return (state == 0 ? ast_strdup("from") : NULL);

	if (pos == 5)   /* No need to duplicate code */
		return complete_queue(line, word, pos, state);

	/* here is the case for 3, <member> */
	queue_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&queue_iter))) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			if (!strncasecmp(word, m->membername, wordlen) && ++which > state) {
				char *tmp;
				ao2_unlock(q);
				tmp = ast_strdup(m->interface);
				ao2_ref(m, -1);
				queue_unref(q);
				return tmp;
			}
			ao2_ref(m, -1);
		}
		ao2_unlock(q);
		queue_unref(q);
	}

	return NULL;
}

static char *handle_queue_remove_member(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *queuename, *interface;

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

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		ast_queue_log(queuename, "CLI", interface, "REMOVEMEMBER", "%s", "");
		ast_cli(a->fd, "Removed interface '%s' from queue '%s'\n", interface, queuename);
		return CLI_SUCCESS;
	case RES_EXISTS:
		ast_cli(a->fd, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		return CLI_FAILURE;
	case RES_NOSUCHQUEUE:
		ast_cli(a->fd, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		return CLI_FAILURE;
	case RES_OUTOFMEMORY:
		ast_cli(a->fd, "Out of memory\n");
		return CLI_FAILURE;
	case RES_NOT_DYNAMIC:
		ast_cli(a->fd, "Unable to remove interface '%s' from queue '%s': Member is not dynamic\n", interface, queuename);
		return CLI_FAILURE;
	default:
		return CLI_FAILURE;
	}
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
		return complete_queue(line, word, pos, state);
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
		if (!ast_strlen_zero(queuename))
			ast_cli(a->fd, " in queue '%s'", queuename);
		if (!ast_strlen_zero(reason))
			ast_cli(a->fd, " for reason '%s'", reason);
		ast_cli(a->fd, "\n");
		return CLI_SUCCESS;
	} else {
		ast_cli(a->fd, "Unable to %spause interface '%s'", paused ? "" : "un", interface);
		if (!ast_strlen_zero(queuename))
			ast_cli(a->fd, " in queue '%s'", queuename);
		if (!ast_strlen_zero(reason))
			ast_cli(a->fd, " for reason '%s'", reason);
		ast_cli(a->fd, "\n");
		return CLI_FAILURE;
	}
}

static char *complete_queue_set_member_penalty(const char *line, const char *word, int pos, int state)
{
	/* 0 - queue; 1 - set; 2 - penalty; 3 - <penalty>; 4 - on; 5 - <member>; 6 - in; 7 - <queue>;*/
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
		return complete_queue(line, word, pos, state);
	default:
		return NULL;
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
		return complete_queue_set_member_penalty(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 6 && a->argc != 8) {
		return CLI_SHOWUSAGE;
	} else if (strcmp(a->argv[4], "on") || (a->argc > 6 && strcmp(a->argv[6], "in"))) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 8)
		queuename = a->argv[7];
	interface = a->argv[5];
	penalty = atoi(a->argv[3]);

	switch (set_member_penalty(queuename, interface, penalty)) {
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

	if (a->argc != 3 && a->argc != 4)
		return CLI_SHOWUSAGE;

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
				return complete_queue(a->line, a->word, a->pos, a->n);
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
				return complete_queue(a->line, a->word, a->pos, a->n);
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
	AST_CLI_DEFINE(handle_queue_add_member, "Add a channel to a specified queue"),
	AST_CLI_DEFINE(handle_queue_remove_member, "Removes a channel from a specified queue"),
	AST_CLI_DEFINE(handle_queue_pause_member, "Pause or unpause a queue member"),
	AST_CLI_DEFINE(handle_queue_set_member_penalty, "Set penalty for a channel of a specified queue"),
	AST_CLI_DEFINE(handle_queue_rule_show, "Show the rules defined in queuerules.conf"),
	AST_CLI_DEFINE(handle_queue_reload, "Reload queues, members, queue rules, or parameters"),
	AST_CLI_DEFINE(handle_queue_reset, "Reset statistics for a queue"),
};

static int unload_module(void)
{
	int res;
	struct ast_context *con;
	struct ao2_iterator q_iter;
	struct call_queue *q = NULL;

	ast_cli_unregister_multiple(cli_queue, ARRAY_LEN(cli_queue));
	res = ast_manager_unregister("QueueStatus");
	res |= ast_manager_unregister("Queues");
	res |= ast_manager_unregister("QueueRule");
	res |= ast_manager_unregister("QueueSummary");
	res |= ast_manager_unregister("QueueAdd");
	res |= ast_manager_unregister("QueueRemove");
	res |= ast_manager_unregister("QueuePause");
	res |= ast_manager_unregister("QueueLog");
	res |= ast_manager_unregister("QueuePenalty");
	res |= ast_unregister_application(app_aqm);
	res |= ast_unregister_application(app_rqm);
	res |= ast_unregister_application(app_pqm);
	res |= ast_unregister_application(app_upqm);
	res |= ast_unregister_application(app_ql);
	res |= ast_unregister_application(app);
	res |= ast_custom_function_unregister(&queuevar_function);
	res |= ast_custom_function_unregister(&queuemembercount_function);
	res |= ast_custom_function_unregister(&queuemembercount_dep);
	res |= ast_custom_function_unregister(&queuememberlist_function);
	res |= ast_custom_function_unregister(&queuewaitingcount_function);
	res |= ast_custom_function_unregister(&queuememberpenalty_function);

	if (device_state_sub)
		ast_event_unsubscribe(device_state_sub);

	if ((con = ast_context_find("app_queue_gosub_virtual_context"))) {
		ast_context_remove_extension2(con, "s", 1, NULL, 0);
		ast_context_destroy(con, "app_queue"); /* leave no trace */
	}

	q_iter = ao2_iterator_init(queues, 0);
	while ((q = ao2_iterator_next(&q_iter))) {
		ao2_unlink(queues, q);
		queue_unref(q);
	}
	ao2_ref(queues, -1);
	devicestate_tps = ast_taskprocessor_unreference(devicestate_tps);
	ast_unload_realtime("queue_members");
	return res;
}

static int load_module(void)
{
	int res;
	struct ast_context *con;
	struct ast_flags mask = {AST_FLAGS_ALL, };

	queues = ao2_container_alloc(MAX_QUEUE_BUCKETS, queue_hash_cb, queue_cmp_cb);

	use_weight = 0;

	if (reload_handler(0, &mask, NULL))
		return AST_MODULE_LOAD_DECLINE;

	con = ast_context_find_or_create(NULL, NULL, "app_queue_gosub_virtual_context", "app_queue");
	if (!con)
		ast_log(LOG_ERROR, "Queue virtual context 'app_queue_gosub_virtual_context' does not exist and unable to create\n");
	else
		ast_add_extension2(con, 1, "s", 1, NULL, NULL, "NoOp", ast_strdup(""), ast_free_ptr, "app_queue");

	if (queue_persistent_members)
		reload_queue_members();

	ast_cli_register_multiple(cli_queue, ARRAY_LEN(cli_queue));
	res = ast_register_application_xml(app, queue_exec);
	res |= ast_register_application_xml(app_aqm, aqm_exec);
	res |= ast_register_application_xml(app_rqm, rqm_exec);
	res |= ast_register_application_xml(app_pqm, pqm_exec);
	res |= ast_register_application_xml(app_upqm, upqm_exec);
	res |= ast_register_application_xml(app_ql, ql_exec);
	res |= ast_manager_register_xml("Queues", 0, manager_queues_show);
	res |= ast_manager_register_xml("QueueStatus", 0, manager_queues_status);
	res |= ast_manager_register_xml("QueueSummary", 0, manager_queues_summary);
	res |= ast_manager_register_xml("QueueAdd", EVENT_FLAG_AGENT, manager_add_queue_member);
	res |= ast_manager_register_xml("QueueRemove", EVENT_FLAG_AGENT, manager_remove_queue_member);
	res |= ast_manager_register_xml("QueuePause", EVENT_FLAG_AGENT, manager_pause_queue_member);
	res |= ast_manager_register_xml("QueueLog", EVENT_FLAG_AGENT, manager_queue_log_custom);
	res |= ast_manager_register_xml("QueuePenalty", EVENT_FLAG_AGENT, manager_queue_member_penalty);
	res |= ast_manager_register_xml("QueueRule", 0, manager_queue_rule_show);
	res |= ast_manager_register_xml("QueueReload", 0, manager_queue_reload);
	res |= ast_manager_register_xml("QueueReset", 0, manager_queue_reset);
	res |= ast_custom_function_register(&queuevar_function);
	res |= ast_custom_function_register(&queuemembercount_function);
	res |= ast_custom_function_register(&queuemembercount_dep);
	res |= ast_custom_function_register(&queuememberlist_function);
	res |= ast_custom_function_register(&queuewaitingcount_function);
	res |= ast_custom_function_register(&queuememberpenalty_function);

	if (!(devicestate_tps = ast_taskprocessor_get("app_queue", 0))) {
		ast_log(LOG_WARNING, "devicestate taskprocessor reference failed - devicestate notifications will not occur\n");
	}

	/* in the following subscribe call, do I use DEVICE_STATE, or DEVICE_STATE_CHANGE? */
	if (!(device_state_sub = ast_event_subscribe(AST_EVENT_DEVICE_STATE, device_state_cb, "AppQueue Device state", NULL, AST_EVENT_IE_END))) {
		res = -1;
	}

	ast_realtime_require_field("queue_members", "paused", RQ_INTEGER1, 1, "uniqueid", RQ_UINTEGER2, 5, SENTINEL);

	return res ? AST_MODULE_LOAD_DECLINE : 0;
}

static int reload(void)
{
	struct ast_flags mask = {AST_FLAGS_ALL,};
	ast_unload_realtime("queue_members");
	reload_handler(1, &mask, NULL);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "True Call Queueing",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

