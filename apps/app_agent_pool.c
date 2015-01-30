/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Call center agent pool.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 * \arg \ref Config_agent
 */
/*** MODULEINFO
	<support_level>core</support_level>
 ***/


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/bridge_after.h"
#include "asterisk/config_options.h"
#include "asterisk/features_config.h"
#include "asterisk/astobj2.h"
#include "asterisk/stringfields.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/causes.h"

/*** DOCUMENTATION
	<application name="AgentLogin" language="en_US">
		<synopsis>
			Login an agent.
		</synopsis>
		<syntax argsep=",">
			<parameter name="AgentId" required="true" />
			<parameter name="options">
				<optionlist>
					<option name="s">
						<para>silent login - do not announce the login ok segment after
						agent logged on.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Login an agent to the system.  Any agent authentication is assumed to
			already be done by dialplan.  While logged in, the agent can receive calls
			and will hear the sound file specified by the config option custom_beep
			when a new call comes in for the agent.  Login failures will continue in
			the dialplan with <variable>AGENT_STATUS</variable> set.</para>
			<para>Before logging in, you can setup on the real agent channel the
			CHANNEL(dtmf-features) an agent will have when talking to a caller
			and you can setup on the channel running this application the
			CONNECTEDLINE() information the agent will see while waiting for a
			caller.</para>
			<para><variable>AGENT_STATUS</variable> enumeration values:</para>
			<enumlist>
				<enum name = "INVALID"><para>The specified agent is invalid.</para></enum>
				<enum name = "ALREADY_LOGGED_IN"><para>The agent is already logged in.</para></enum>
			</enumlist>
			<note><para>The Agents:<replaceable>AgentId</replaceable> device state is
			available to monitor the status of the agent.</para></note>
		</description>
		<see-also>
			<ref type="application">Authenticate</ref>
			<ref type="application">Queue</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">AGENT</ref>
			<ref type="function">CHANNEL(dtmf-features)</ref>
			<ref type="function">CONNECTEDLINE()</ref>
			<ref type="filename">agents.conf</ref>
			<ref type="filename">queues.conf</ref>
		</see-also>
	</application>
	<application name="AgentRequest" language="en_US">
		<synopsis>
			Request an agent to connect with the channel.
		</synopsis>
		<syntax argsep=",">
			<parameter name="AgentId" required="true" />
		</syntax>
		<description>
			<para>Request an agent to connect with the channel.  Failure to find,
			alert the agent, or acknowledge the call will continue in the dialplan
			with <variable>AGENT_STATUS</variable> set.</para>
			<para><variable>AGENT_STATUS</variable> enumeration values:</para>
			<enumlist>
				<enum name = "INVALID"><para>The specified agent is invalid.</para></enum>
				<enum name = "NOT_LOGGED_IN"><para>The agent is not available.</para></enum>
				<enum name = "BUSY"><para>The agent is on another call.</para></enum>
				<enum name = "NOT_CONNECTED"><para>The agent did not connect with the
				call.  The agent most likely did not acknowledge the call.</para></enum>
				<enum name = "ERROR"><para>Alerting the agent failed.</para></enum>
			</enumlist>
		</description>
		<see-also>
			<ref type="application">AgentLogin</ref>
		</see-also>
	</application>
	<function name="AGENT" language="en_US">
		<synopsis>
			Gets information about an Agent
		</synopsis>
		<syntax argsep=":">
			<parameter name="AgentId" required="true" />
			<parameter name="item">
				<para>The valid items to retrieve are:</para>
				<enumlist>
					<enum name="status">
						<para>(default) The status of the agent (LOGGEDIN | LOGGEDOUT)</para>
					</enum>
					<enum name="password">
						<para>Deprecated.  The dialplan handles any agent authentication.</para>
					</enum>
					<enum name="name">
						<para>The name of the agent</para>
					</enum>
					<enum name="mohclass">
						<para>MusicOnHold class</para>
					</enum>
					<enum name="channel">
						<para>The name of the active channel for the Agent (AgentLogin)</para>
					</enum>
					<enum name="fullchannel">
						<para>The untruncated name of the active channel for the Agent (AgentLogin)</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description></description>
	</function>
	<manager name="Agents" language="en_US">
		<synopsis>
			Lists agents and their status.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Will list info about all defined agents.</para>
		</description>
		<see-also>
			<ref type="managerEvent">Agents</ref>
			<ref type="managerEvent">AgentsComplete</ref>
		</see-also>
	</manager>
	<managerEvent language="en_US" name="Agents">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>
				Response event in a series to the Agents AMI action containing
				information about a defined agent.
			</synopsis>
			<syntax>
				<parameter name="Agent">
					<para>Agent ID of the agent.</para>
				</parameter>
				<parameter name="Name">
					<para>User friendly name of the agent.</para>
				</parameter>
				<parameter name="Status">
					<para>Current status of the agent.</para>
					<para>The valid values are:</para>
					<enumlist>
						<enum name="AGENT_LOGGEDOFF" />
						<enum name="AGENT_IDLE" />
						<enum name="AGENT_ONCALL" />
					</enumlist>
				</parameter>
				<parameter name="TalkingToChan">
					<para><variable>BRIDGEPEER</variable> value on agent channel.</para>
					<para>Present if Status value is <literal>AGENT_ONCALL</literal>.</para>
				</parameter>
				<parameter name="CallStarted">
					<para>Epoche time when the agent started talking with the caller.</para>
					<para>Present if Status value is <literal>AGENT_ONCALL</literal>.</para>
				</parameter>
				<parameter name="LoggedInTime">
					<para>Epoche time when the agent logged in.</para>
					<para>Present if Status value is <literal>AGENT_IDLE</literal> or <literal>AGENT_ONCALL</literal>.</para>
				</parameter>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			</syntax>
			<description>
				<para>The channel snapshot is present if the Status value is <literal>AGENT_IDLE</literal> or <literal>AGENT_ONCALL</literal>.</para>
			</description>
			<see-also>
				<ref type="manager">Agents</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AgentsComplete">
		<managerEventInstance class="EVENT_FLAG_AGENT">
			<synopsis>
				Final response event in a series of events to the Agents AMI action.
			</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			</syntax>
			<see-also>
				<ref type="manager">Agents</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<manager name="AgentLogoff" language="en_US">
		<synopsis>
			Sets an agent as no longer logged in.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Agent" required="true">
				<para>Agent ID of the agent to log off.</para>
			</parameter>
			<parameter name="Soft">
				<para>Set to <literal>true</literal> to not hangup existing calls.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sets an agent as no longer logged in.</para>
		</description>
	</manager>
	<configInfo name="app_agent_pool" language="en_US">
		<synopsis>Agent pool applications</synopsis>
		<description>
			<note><para>Option changes take effect on agent login or after an agent
			disconnects from a call.</para></note>
		</description>
		<configFile name="agents.conf">
			<configObject name="global">
				<synopsis>Unused, but reserved.</synopsis>
			</configObject>
			<configObject name="agent-id">
				<synopsis>Configure an agent for the pool.</synopsis>
				<description>
					<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
				</description>
				<configOption name="ackcall">
					<synopsis>Enable to require the agent to acknowledge a call.</synopsis>
					<description>
						<para>Enable to require the agent to give a DTMF acknowledgement
						when the agent receives a call.</para>
						<note><para>The option is overridden by <variable>AGENTACKCALL</variable> on agent login.</para></note>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="acceptdtmf">
					<synopsis>DTMF key sequence the agent uses to acknowledge a call.</synopsis>
					<description>
						<note><para>The option is overridden by <variable>AGENTACCEPTDTMF</variable> on agent login.</para></note>
						<note><para>The option is ignored unless the ackcall option is enabled.</para></note>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="autologoff">
					<synopsis>Time the agent has to acknowledge a call before being logged off.</synopsis>
					<description>
						<para>Set how many seconds a call for the agent has to wait for the
						agent to acknowledge the call before the agent is automatically
						logged off.  If set to zero then the call will wait forever for
						the agent to acknowledge.</para>
						<note><para>The option is overridden by <variable>AGENTAUTOLOGOFF</variable> on agent login.</para></note>
						<note><para>The option is ignored unless the ackcall option is enabled.</para></note>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="wrapuptime">
					<synopsis>Minimum time the agent has between calls.</synopsis>
					<description>
						<para>Set the minimum amount of time in milliseconds after
						disconnecting a call before the agent can receive a new call.</para>
						<note><para>The option is overridden by <variable>AGENTWRAPUPTIME</variable> on agent login.</para></note>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="musiconhold">
					<synopsis>Music on hold class the agent listens to between calls.</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="recordagentcalls">
					<synopsis>Enable to automatically record calls the agent takes.</synopsis>
					<description>
						<para>Enable recording calls the agent takes automatically by
						invoking the automixmon DTMF feature when the agent connects
						to a caller.  See <filename>features.conf.sample</filename> for information about
						the automixmon feature.</para>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="custom_beep">
					<synopsis>Sound file played to alert the agent when a call is present.</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
				<configOption name="fullname">
					<synopsis>A friendly name for the agent used in log messages.</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='app_agent_pool']/description/note)" />
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/* ------------------------------------------------------------------- */

#define AST_MAX_BUF	256

/*! Maximum wait time (in ms) for the custom_beep file to play announcing the caller. */
#define CALLER_SAFETY_TIMEOUT_TIME	(2 * 60 * 1000)

/*! Number of seconds to wait for local channel optimizations to complete. */
#define LOGIN_WAIT_TIMEOUT_TIME		5

static const char app_agent_login[] = "AgentLogin";
static const char app_agent_request[] = "AgentRequest";

/*! Agent config parameters. */
struct agent_cfg {
	AST_DECLARE_STRING_FIELDS(
		/*! Identification of the agent.  (agents config container key) */
		AST_STRING_FIELD(username);
		/*! Name of agent for logging and querying purposes */
		AST_STRING_FIELD(full_name);

		/*!
		 * \brief DTMF string for an agent to accept a call.
		 *
		 * \note The channel variable AGENTACCEPTDTMF overrides on login.
		 */
		AST_STRING_FIELD(dtmf_accept);
		/*! Beep sound file to use.  Alert the agent a call is waiting. */
		AST_STRING_FIELD(beep_sound);
		/*! MOH class to use while agent waiting for call. */
		AST_STRING_FIELD(moh);
	);
	/*!
	 * \brief Number of seconds for agent to ack a call before being logged off.
	 *
	 * \note The channel variable AGENTAUTOLOGOFF overrides on login.
	 * \note If zero then timer is disabled.
	 */
	unsigned int auto_logoff;
	/*!
	 * \brief Time after a call in ms before the agent can get a new call.
	 *
	 * \note The channel variable AGENTWRAPUPTIME overrides on login.
	 */
	unsigned int wrapup_time;
	/*!
	 * \brief TRUE if agent needs to ack a call to accept it.
	 *
	 * \note The channel variable AGENTACKCALL overrides on login.
	 */
	int ack_call;
	/*! TRUE if agent calls are automatically recorded. */
	int record_agent_calls;
};

/*!
 * \internal
 * \brief Agent config ao2 container sort function.
 * \since 12.0.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_POINTER - if set, 'obj_right', is an object.
 *   OBJ_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int agent_cfg_sort_cmp(const void *obj_left, const void *obj_right, int flags)
{
	const struct agent_cfg *cfg_left = obj_left;
	const struct agent_cfg *cfg_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = cfg_right->username;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(cfg_left->username, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(cfg_left->username, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

static void agent_cfg_destructor(void *vdoomed)
{
	struct agent_cfg *doomed = vdoomed;

	ast_string_field_free_memory(doomed);
}

static void *agent_cfg_alloc(const char *name)
{
	struct agent_cfg *cfg;

	cfg = ao2_alloc_options(sizeof(*cfg), agent_cfg_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg || ast_string_field_init(cfg, 64)) {
		return NULL;
	}
	ast_string_field_set(cfg, username, name);
	return cfg;
}

static void *agent_cfg_find(struct ao2_container *agents, const char *username)
{
	return ao2_find(agents, username, OBJ_KEY);
}

/*! Agents configuration */
struct agents_cfg {
	/*! Master configured agents container. */
	struct ao2_container *agents;
};

static struct aco_type agent_type = {
	.type = ACO_ITEM,
	.name = "agent-id",
	.category_match = ACO_BLACKLIST,
	.category = "^(general|agents)$",
	.item_alloc = agent_cfg_alloc,
	.item_find = agent_cfg_find,
	.item_offset = offsetof(struct agents_cfg, agents),
};

static struct aco_type *agent_types[] = ACO_TYPES(&agent_type);

/* The general category is reserved, but unused */
static struct aco_type general_type = {
	.type = ACO_GLOBAL,
	.name = "global",
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

static struct aco_file agents_conf = {
	.filename = "agents.conf",
	.types = ACO_TYPES(&general_type, &agent_type),
};

static AO2_GLOBAL_OBJ_STATIC(cfg_handle);

static void agents_cfg_destructor(void *vdoomed)
{
	struct agents_cfg *doomed = vdoomed;

	ao2_cleanup(doomed->agents);
	doomed->agents = NULL;
}

/*!
 * \internal
 * \brief Create struct agents_cfg object.
 * \since 12.0.0
 *
 * \note A lock is not needed for the object or any secondary
 * created cfg objects.  These objects are immutable after the
 * config is loaded and applied.
 *
 * \retval New struct agents_cfg object.
 * \retval NULL on error.
 */
static void *agents_cfg_alloc(void)
{
	struct agents_cfg *cfg;

	cfg = ao2_alloc_options(sizeof(*cfg), agents_cfg_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg) {
		return NULL;
	}
	cfg->agents = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, agent_cfg_sort_cmp, NULL);
	if (!cfg->agents) {
		ao2_ref(cfg, -1);
		cfg = NULL;
	}
	return cfg;
}

static void agents_post_apply_config(void);

CONFIG_INFO_STANDARD(cfg_info, cfg_handle, agents_cfg_alloc,
	.files = ACO_FILES(&agents_conf),
	.post_apply_config = agents_post_apply_config,
);

static void destroy_config(void)
{
	ao2_global_obj_release(cfg_handle);
	aco_info_destroy(&cfg_info);
}

static int load_config(void)
{
	if (aco_info_init(&cfg_info)) {
		return -1;
	}

	/* Agent options */
	aco_option_register(&cfg_info, "ackcall", ACO_EXACT, agent_types, "no", OPT_BOOL_T, 1, FLDSET(struct agent_cfg, ack_call));
	aco_option_register(&cfg_info, "acceptdtmf", ACO_EXACT, agent_types, "#", OPT_STRINGFIELD_T, 1, STRFLDSET(struct agent_cfg, dtmf_accept));
	aco_option_register(&cfg_info, "autologoff", ACO_EXACT, agent_types, "0", OPT_UINT_T, 0, FLDSET(struct agent_cfg, auto_logoff));
	aco_option_register(&cfg_info, "wrapuptime", ACO_EXACT, agent_types, "0", OPT_UINT_T, 0, FLDSET(struct agent_cfg, wrapup_time));
	aco_option_register(&cfg_info, "musiconhold", ACO_EXACT, agent_types, "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct agent_cfg, moh));
	aco_option_register(&cfg_info, "recordagentcalls", ACO_EXACT, agent_types, "no", OPT_BOOL_T, 1, FLDSET(struct agent_cfg, record_agent_calls));
	aco_option_register(&cfg_info, "custom_beep", ACO_EXACT, agent_types, "beep", OPT_STRINGFIELD_T, 0, STRFLDSET(struct agent_cfg, beep_sound));
	aco_option_register(&cfg_info, "fullname", ACO_EXACT, agent_types, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct agent_cfg, full_name));

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		return -1;
	}

	return 0;
}

enum agent_state {
	/*! The agent is defined but an agent is not present. */
	AGENT_STATE_LOGGED_OUT,
	/*! Forced initial login wait to allow any local channel optimizations to happen. */
	AGENT_STATE_PROBATION_WAIT,
	/*! The agent is ready for a call. */
	AGENT_STATE_READY_FOR_CALL,
	/*! The agent has a call waiting to connect. */
	AGENT_STATE_CALL_PRESENT,
	/*! The agent needs to ack the call. */
	AGENT_STATE_CALL_WAIT_ACK,
	/*! The agent is connected with a call. */
	AGENT_STATE_ON_CALL,
	/*! The agent is resting between calls. */
	AGENT_STATE_CALL_WRAPUP,
	/*! The agent is being kicked out. */
	AGENT_STATE_LOGGING_OUT,
};

/*! Agent config option override flags. */
enum agent_override_flags {
	AGENT_FLAG_ACK_CALL = (1 << 0),
	AGENT_FLAG_DTMF_ACCEPT = (1 << 1),
	AGENT_FLAG_AUTO_LOGOFF = (1 << 2),
	AGENT_FLAG_WRAPUP_TIME = (1 << 3),
};

/*! \brief Structure representing an agent. */
struct agent_pvt {
	AST_DECLARE_STRING_FIELDS(
		/*! Identification of the agent.  (agents container key) */
		AST_STRING_FIELD(username);
		/*! Login override DTMF string for an agent to accept a call. */
		AST_STRING_FIELD(override_dtmf_accept);
	);
	/*! Connected line information to send when reentering the holding bridge. */
	struct ast_party_connected_line waiting_colp;
	/*! Flags show if settings were overridden by channel vars. */
	unsigned int flags;
	/*! Login override number of seconds for agent to ack a call before being logged off. */
	unsigned int override_auto_logoff;
	/*! Login override time after a call in ms before the agent can get a new call. */
	unsigned int override_wrapup_time;
	/*! Login override if agent needs to ack a call to accept it. */
	unsigned int override_ack_call:1;

	/*! TRUE if the agent is requested to logoff when the current call ends. */
	unsigned int deferred_logoff:1;

	/*! Mark and sweep config update to determine if an agent is dead. */
	unsigned int the_mark:1;
	/*!
	 * \brief TRUE if the agent is no longer configured and is being destroyed.
	 *
	 * \note Agents cannot log in if they are dead.
	 */
	unsigned int dead:1;

	/*! Agent control state variable. */
	enum agent_state state;
	/*! Custom device state of agent. */
	enum ast_device_state devstate;

	/*! When agent first logged in */
	time_t login_start;
	/*! When agent login probation started. */
	time_t probation_start;
	/*! When call started */
	time_t call_start;
	/*! When ack timer started */
	struct timeval ack_time;
	/*! When last disconnected */
	struct timeval last_disconnect;

	/*! Caller is waiting in this bridge for agent to join. (Holds ref) */
	struct ast_bridge *caller_bridge;
	/*! Agent is logged in with this channel. (Holds ref) (NULL if not logged in.) */
	struct ast_channel *logged;
	/*! Active config values from config file. (Holds ref) */
	struct agent_cfg *cfg;
};

/*! Container of defined agents. */
static struct ao2_container *agents;

/*!
 * \brief Lock the agent.
 *
 * \param agent Agent to lock
 *
 * \return Nothing
 */
#define agent_lock(agent)	_agent_lock(agent, __FILE__, __PRETTY_FUNCTION__, __LINE__, #agent)
static inline void _agent_lock(struct agent_pvt *agent, const char *file, const char *function, int line, const char *var)
{
	__ao2_lock(agent, AO2_LOCK_REQ_MUTEX, file, function, line, var);
}

/*!
 * \brief Unlock the agent.
 *
 * \param agent Agent to unlock
 *
 * \return Nothing
 */
#define agent_unlock(agent)	_agent_unlock(agent, __FILE__, __PRETTY_FUNCTION__, __LINE__, #agent)
static inline void _agent_unlock(struct agent_pvt *agent, const char *file, const char *function, int line, const char *var)
{
	__ao2_unlock(agent, file, function, line, var);
}

/*!
 * \internal
 * \brief Obtain the agent logged in channel lock if it exists.
 * \since 12.0.0
 *
 * \param agent Pointer to the LOCKED agent_pvt.
 *
 * \note Assumes the agent lock is already obtained.
 *
 * \note Defined locking order is channel lock then agent lock.
 *
 * \return Nothing
 */
static struct ast_channel *agent_lock_logged(struct agent_pvt *agent)
{
	struct ast_channel *logged;

	for (;;) {
		if (!agent->logged) { /* No owner. Nothing to do. */
			return NULL;
		}

		/* If we don't ref the logged, it could be killed when we unlock the agent. */
		logged = ast_channel_ref(agent->logged);

		/* Locking logged requires us to lock channel, then agent. */
		agent_unlock(agent);
		ast_channel_lock(logged);
		agent_lock(agent);

		/* Check if logged changed during agent unlock period */
		if (logged != agent->logged) {
			/* Channel changed. Unref and do another pass. */
			ast_channel_unlock(logged);
			ast_channel_unref(logged);
		} else {
			/* Channel stayed the same. Return it. */
			return logged;
		}
	}
}

/*!
 * \internal
 * \brief Get the Agent:agent_id device state.
 * \since 12.0.0
 *
 * \param agent_id Username of the agent.
 *
 * \details
 * Search the agents container for the agent and return the
 * current state.
 *
 * \return Device state of the agent.
 */
static enum ast_device_state agent_pvt_devstate_get(const char *agent_id)
{
	enum ast_device_state dev_state = AST_DEVICE_INVALID;
	struct agent_pvt *agent;

	agent = ao2_find(agents, agent_id, OBJ_KEY);
	if (agent) {
		agent_lock(agent);
		dev_state = agent->devstate;
		agent_unlock(agent);
		ao2_ref(agent, -1);
	}
	return dev_state;
}

/*!
 * \internal
 * \brief Request an agent device state be updated.
 * \since 12.0.0
 *
 * \param agent_id Which agent needs the device state updated.
 *
 * \return Nothing
 */
static void agent_devstate_changed(const char *agent_id)
{
	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "Agent:%s", agent_id);
}

static void agent_pvt_destructor(void *vdoomed)
{
	struct agent_pvt *doomed = vdoomed;

	/* Make sure device state reflects agent destruction. */
	if (!ast_strlen_zero(doomed->username)) {
		ast_debug(1, "Agent %s: Destroyed.\n", doomed->username);
		agent_devstate_changed(doomed->username);
	}

	ast_party_connected_line_free(&doomed->waiting_colp);
	if (doomed->caller_bridge) {
		ast_bridge_destroy(doomed->caller_bridge, 0);
		doomed->caller_bridge = NULL;
	}
	if (doomed->logged) {
		doomed->logged = ast_channel_unref(doomed->logged);
	}
	ao2_cleanup(doomed->cfg);
	doomed->cfg = NULL;
	ast_string_field_free_memory(doomed);
}

static struct agent_pvt *agent_pvt_new(struct agent_cfg *cfg)
{
	struct agent_pvt *agent;

	agent = ao2_alloc(sizeof(*agent), agent_pvt_destructor);
	if (!agent) {
		return NULL;
	}
	if (ast_string_field_init(agent, 32)) {
		ao2_ref(agent, -1);
		return NULL;
	}
	ast_string_field_set(agent, username, cfg->username);
	ast_party_connected_line_init(&agent->waiting_colp);
	ao2_ref(cfg, +1);
	agent->cfg = cfg;
	agent->devstate = AST_DEVICE_UNAVAILABLE;
	return agent;
}

/*!
 * \internal
 * \brief Agents ao2 container sort function.
 * \since 12.0.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_POINTER - if set, 'obj_right', is an object.
 *   OBJ_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int agent_pvt_sort_cmp(const void *obj_left, const void *obj_right, int flags)
{
	const struct agent_pvt *agent_left = obj_left;
	const struct agent_pvt *agent_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = agent_right->username;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(agent_left->username, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(agent_left->username, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

/*!
 * \internal
 * \brief ao2_find() callback function.
 * \since 12.0.0
 *
 * Usage:
 * found = ao2_find(agents, agent, OBJ_POINTER);
 * found = ao2_find(agents, "agent-id", OBJ_KEY);
 * found = ao2_find(agents, agent->logged, 0);
 */
static int agent_pvt_cmp(void *obj, void *arg, int flags)
{
	const struct agent_pvt *agent = obj;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_POINTER:
	case OBJ_KEY:
	case OBJ_PARTIAL_KEY:
		cmp = CMP_MATCH;
		break;
	default:
		if (agent->logged == arg) {
			cmp = CMP_MATCH;
		} else {
			cmp = 0;
		}
		break;
	}
	return cmp;
}

static int agent_mark(void *obj, void *arg, int flags)
{
	struct agent_pvt *agent = obj;

	agent_lock(agent);
	agent->the_mark = 1;
	agent_unlock(agent);
	return 0;
}

static void agents_mark(void)
{
	ao2_callback(agents, 0, agent_mark, NULL);
}

static int agent_sweep(void *obj, void *arg, int flags)
{
	struct agent_pvt *agent = obj;
	int cmp = 0;

	agent_lock(agent);
	if (agent->the_mark) {
		agent->the_mark = 0;
		agent->dead = 1;
		/* Unlink dead agents immediately. */
		cmp = CMP_MATCH;
	}
	agent_unlock(agent);
	return cmp;
}

static void agents_sweep(void)
{
	struct ao2_iterator *iter;
	struct agent_pvt *agent;
	struct ast_channel *logged;

	iter = ao2_callback(agents, OBJ_MULTIPLE | OBJ_UNLINK, agent_sweep, NULL);
	if (!iter) {
		return;
	}
	for (; (agent = ao2_iterator_next(iter)); ao2_ref(agent, -1)) {
		agent_lock(agent);
		if (agent->logged) {
			logged = ast_channel_ref(agent->logged);
		} else {
			logged = NULL;
		}
		agent_unlock(agent);
		if (!logged) {
			continue;
		}
		ast_log(LOG_NOTICE,
			"Forced logoff of agent %s(%s).  Agent no longer configured.\n",
			agent->username, ast_channel_name(logged));
		ast_softhangup(logged, AST_SOFTHANGUP_EXPLICIT);
		ast_channel_unref(logged);
	}
	ao2_iterator_destroy(iter);
}

static void agents_post_apply_config(void)
{
	struct ao2_iterator iter;
	struct agent_cfg *cfg;
	RAII_VAR(struct agents_cfg *, cfgs, ao2_global_obj_ref(cfg_handle), ao2_cleanup);

	ast_assert(cfgs != NULL);

	agents_mark();
	iter = ao2_iterator_init(cfgs->agents, 0);
	for (; (cfg = ao2_iterator_next(&iter)); ao2_ref(cfg, -1)) {
		RAII_VAR(struct agent_pvt *, agent, ao2_find(agents, cfg->username, OBJ_KEY), ao2_cleanup);

		if (agent) {
			agent_lock(agent);
			agent->the_mark = 0;
			if (!agent->logged) {
				struct agent_cfg *cfg_old;

				/* Replace the config of agents not logged in. */
				cfg_old = agent->cfg;
				ao2_ref(cfg, +1);
				agent->cfg = cfg;
				ao2_cleanup(cfg_old);
			}
			agent_unlock(agent);
			continue;
		}
		agent = agent_pvt_new(cfg);
		if (!agent) {
			continue;
		}
		ao2_link(agents, agent);
		ast_debug(1, "Agent %s: Created.\n", agent->username);
		agent_devstate_changed(agent->username);
	}
	ao2_iterator_destroy(&iter);
	agents_sweep();
}

static int agent_logoff_request(const char *agent_id, int soft)
{
	struct ast_channel *logged;
	RAII_VAR(struct agent_pvt *, agent, ao2_find(agents, agent_id, OBJ_KEY), ao2_cleanup);

	if (!agent) {
		return -1;
	}

	agent_lock(agent);
	logged = agent_lock_logged(agent);
	if (logged) {
		if (soft) {
			agent->deferred_logoff = 1;
		} else {
			ast_softhangup(logged, AST_SOFTHANGUP_EXPLICIT);
		}
		ast_channel_unlock(logged);
		ast_channel_unref(logged);
	}
	agent_unlock(agent);
	return 0;
}

/*! Agent holding bridge instance holder. */
static AO2_GLOBAL_OBJ_STATIC(agent_holding);

/*! Agent holding bridge deferred creation lock. */
AST_MUTEX_DEFINE_STATIC(agent_holding_lock);

/*!
 * \internal
 * \brief Callback to clear AGENT_STATUS on the caller channel.
 *
 * \param bridge_channel Which channel to operate on.
 * \param payload Data to pass to the callback. (NULL if none).
 * \param payload_size Size of the payload if payload is non-NULL.  A number otherwise.
 *
 * \note The payload MUST NOT have any resources that need to be freed.
 *
 * \return Nothing
 */
static void clear_agent_status(struct ast_bridge_channel *bridge_channel, const void *payload, size_t payload_size)
{
	pbx_builtin_setvar_helper(bridge_channel->chan, "AGENT_STATUS", NULL);
}

/*!
 * \internal
 * \brief Connect the agent with the waiting caller.
 * \since 12.0.0
 *
 * \param bridge_channel Agent channel connecting to the caller.
 * \param agent Which agent is connecting to the caller.
 *
 * \note The agent is locked on entry and not locked on exit.
 *
 * \return Nothing
 */
static void agent_connect_caller(struct ast_bridge_channel *bridge_channel, struct agent_pvt *agent)
{
	struct ast_bridge *caller_bridge;
	int record_agent_calls;
	int res;

	record_agent_calls = agent->cfg->record_agent_calls;
	caller_bridge = agent->caller_bridge;
	agent->caller_bridge = NULL;
	agent->state = AGENT_STATE_ON_CALL;
	time(&agent->call_start);
	agent_unlock(agent);

	if (!caller_bridge) {
		/* Reset agent. */
		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
			AST_CAUSE_NORMAL_CLEARING);
		return;
	}
	res = ast_bridge_move(caller_bridge, bridge_channel->bridge, bridge_channel->chan,
		NULL, 0);
	if (res) {
		/* Reset agent. */
		ast_bridge_destroy(caller_bridge, 0);
		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
			AST_CAUSE_NORMAL_CLEARING);
		return;
	}
	res = ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_ANSWER, NULL, 0)
		|| ast_bridge_channel_write_callback(bridge_channel, 0, clear_agent_status, NULL, 0);
	if (res) {
		/* Reset agent. */
		ast_bridge_destroy(caller_bridge, 0);
		return;
	}

	if (record_agent_calls) {
		struct ast_bridge_features_automixmonitor options = {
			.start_stop = AUTO_MONITOR_START,
			};

		/*
		 * The agent is in the new bridge so we can invoke the
		 * mixmonitor hook to only start recording.
		 */
		ast_bridge_features_do(AST_BRIDGE_BUILTIN_AUTOMIXMON, bridge_channel, &options);
	}

	ao2_t_ref(caller_bridge, -1, "Agent successfully in caller_bridge");
}

static int bridge_agent_hold_ack(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct agent_pvt *agent = hook_pvt;

	agent_lock(agent);
	switch (agent->state) {
	case AGENT_STATE_CALL_WAIT_ACK:
		/* Connect to caller now. */
		ast_debug(1, "Agent %s: Acked call.\n", agent->username);
		agent_connect_caller(bridge_channel, agent);/* Will unlock agent. */
		return 0;
	default:
		break;
	}
	agent_unlock(agent);
	return 0;
}

static int bridge_agent_hold_heartbeat(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct agent_pvt *agent = hook_pvt;
	int probation_timedout = 0;
	int ack_timedout = 0;
	int wrapup_timedout = 0;
	int deferred_logoff;
	unsigned int wrapup_time;
	unsigned int auto_logoff;

	agent_lock(agent);
	deferred_logoff = agent->deferred_logoff;
	if (deferred_logoff) {
		agent->state = AGENT_STATE_LOGGING_OUT;
	}

	switch (agent->state) {
	case AGENT_STATE_PROBATION_WAIT:
		probation_timedout =
			LOGIN_WAIT_TIMEOUT_TIME <= (time(NULL) - agent->probation_start);
		if (probation_timedout) {
			/* Now ready for a caller. */
			agent->state = AGENT_STATE_READY_FOR_CALL;
			agent->devstate = AST_DEVICE_NOT_INUSE;
		}
		break;
	case AGENT_STATE_CALL_WAIT_ACK:
		/* Check ack call time. */
		auto_logoff = agent->cfg->auto_logoff;
		if (ast_test_flag(agent, AGENT_FLAG_AUTO_LOGOFF)) {
			auto_logoff = agent->override_auto_logoff;
		}
		if (auto_logoff) {
			auto_logoff *= 1000;
			ack_timedout = ast_tvdiff_ms(ast_tvnow(), agent->ack_time) > auto_logoff;
			if (ack_timedout) {
				agent->state = AGENT_STATE_LOGGING_OUT;
			}
		}
		break;
	case AGENT_STATE_CALL_WRAPUP:
		/* Check wrapup time. */
		wrapup_time = agent->cfg->wrapup_time;
		if (ast_test_flag(agent, AGENT_FLAG_WRAPUP_TIME)) {
			wrapup_time = agent->override_wrapup_time;
		}
		wrapup_timedout = ast_tvdiff_ms(ast_tvnow(), agent->last_disconnect) > wrapup_time;
		if (wrapup_timedout) {
			agent->state = AGENT_STATE_READY_FOR_CALL;
			agent->devstate = AST_DEVICE_NOT_INUSE;
		}
		break;
	default:
		break;
	}
	agent_unlock(agent);

	if (deferred_logoff) {
		ast_debug(1, "Agent %s: Deferred logoff.\n", agent->username);
		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
			AST_CAUSE_NORMAL_CLEARING);
	} else if (probation_timedout) {
		ast_debug(1, "Agent %s: Login complete.\n", agent->username);
		agent_devstate_changed(agent->username);
	} else if (ack_timedout) {
		ast_debug(1, "Agent %s: Ack call timeout.\n", agent->username);
		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
			AST_CAUSE_NORMAL_CLEARING);
	} else if (wrapup_timedout) {
		ast_debug(1, "Agent %s: Wrapup timeout. Ready for new call.\n", agent->username);
		agent_devstate_changed(agent->username);
	}

	return 0;
}

static void agent_after_bridge_cb(struct ast_channel *chan, void *data);
static void agent_after_bridge_cb_failed(enum ast_bridge_after_cb_reason reason, void *data);

/*!
 * \internal
 * \brief ast_bridge agent_hold push method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_agent_hold_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	int res = 0;
	unsigned int wrapup_time;
	char dtmf[AST_FEATURE_MAX_LEN];
	struct ast_channel *chan;
	const char *moh_class;
	RAII_VAR(struct agent_pvt *, agent, NULL, ao2_cleanup);

	chan = bridge_channel->chan;

	agent = ao2_find(agents, swap ? swap->chan : chan, 0);
	if (!agent) {
		/* Could not find the agent. */
		return -1;
	}

	/* Setup agent entertainment */
	agent_lock(agent);
	moh_class = ast_strdupa(agent->cfg->moh);
	agent_unlock(agent);
	res |= ast_channel_add_bridge_role(chan, "holding_participant");
	res |= ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "musiconhold");
	res |= ast_channel_set_bridge_role_option(chan, "holding_participant", "moh_class", moh_class);

	/* Add DTMF acknowledge hook. */
	dtmf[0] = '\0';
	agent_lock(agent);
	if (ast_test_flag(agent, AGENT_FLAG_ACK_CALL)
		? agent->override_ack_call : agent->cfg->ack_call) {
		const char *dtmf_accept;

		dtmf_accept = ast_test_flag(agent, AGENT_FLAG_DTMF_ACCEPT)
			? agent->override_dtmf_accept : agent->cfg->dtmf_accept;
		ast_copy_string(dtmf, dtmf_accept, sizeof(dtmf));
	}
	agent_unlock(agent);
	if (!ast_strlen_zero(dtmf)) {
		ao2_ref(agent, +1);
		if (ast_bridge_dtmf_hook(bridge_channel->features, dtmf, bridge_agent_hold_ack,
			agent, __ao2_cleanup, AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
			ao2_ref(agent, -1);
			res = -1;
		}
	}

	/* Add heartbeat interval hook. */
	ao2_ref(agent, +1);
	if (ast_bridge_interval_hook(bridge_channel->features, 0, 1000,
		bridge_agent_hold_heartbeat, agent, __ao2_cleanup, AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		ao2_ref(agent, -1);
		res = -1;
	}

	res |= ast_bridge_base_v_table.push(self, bridge_channel, swap);
	if (res) {
		ast_channel_remove_bridge_role(chan, "holding_participant");
		return -1;
	}

	if (swap) {
		res = ast_bridge_set_after_callback(chan, agent_after_bridge_cb,
			agent_after_bridge_cb_failed, chan);
		if (res) {
			ast_channel_remove_bridge_role(chan, "holding_participant");
			return -1;
		}

		agent_lock(agent);
		ast_channel_unref(agent->logged);
		agent->logged = ast_channel_ref(chan);
		agent_unlock(agent);

		/*
		 * Kick the channel out so it can come back in fully controlled.
		 * Otherwise, the after bridge callback will linger and the
		 * agent will have some slightly different behavior in corner
		 * cases.
		 */
		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END,
			AST_CAUSE_NORMAL_CLEARING);
		return 0;
	}

	agent_lock(agent);
	switch (agent->state) {
	case AGENT_STATE_LOGGED_OUT:
		/*!
		 * \todo XXX the login probation time should be only if it is needed.
		 *
		 * Need to determine if there are any local channels that can
		 * optimize and wait until they actually do before leaving the
		 * AGENT_STATE_PROBATION_WAIT state.  For now, the blind
		 * timer of LOGIN_WAIT_TIMEOUT_TIME will do.
		 */
		/*
		 * Start the login probation timer.
		 *
		 * We cannot handle an agent local channel optimization when the
		 * agent is on a call.  The optimization may kick the agent
		 * channel we know about out of the call without our being able
		 * to switch to the replacement channel.  Get any agent local
		 * channel optimization out of the way while the agent is in the
		 * holding bridge.
		 */
		time(&agent->probation_start);
		agent->state = AGENT_STATE_PROBATION_WAIT;
		agent_unlock(agent);
		break;
	case AGENT_STATE_PROBATION_WAIT:
		/* Restart the probation timer. */
		time(&agent->probation_start);
		agent_unlock(agent);
		break;
	case AGENT_STATE_READY_FOR_CALL:
		/*
		 * Likely someone manually kicked us out of the holding bridge
		 * and we came right back in.
		 */
		agent_unlock(agent);
		break;
	default:
		/* Unexpected agent state. */
		ast_assert(0);
		/* Fall through */
	case AGENT_STATE_CALL_PRESENT:
	case AGENT_STATE_CALL_WAIT_ACK:
		agent->state = AGENT_STATE_READY_FOR_CALL;
		agent->devstate = AST_DEVICE_NOT_INUSE;
		agent_unlock(agent);
		ast_debug(1, "Agent %s: Call abort recovery complete.\n", agent->username);
		agent_devstate_changed(agent->username);
		break;
	case AGENT_STATE_ON_CALL:
	case AGENT_STATE_CALL_WRAPUP:
		wrapup_time = agent->cfg->wrapup_time;
		if (ast_test_flag(agent, AGENT_FLAG_WRAPUP_TIME)) {
			wrapup_time = agent->override_wrapup_time;
		}
		if (wrapup_time) {
			agent->state = AGENT_STATE_CALL_WRAPUP;
		} else {
			agent->state = AGENT_STATE_READY_FOR_CALL;
			agent->devstate = AST_DEVICE_NOT_INUSE;
		}
		agent_unlock(agent);
		if (!wrapup_time) {
			/* No wrapup time. */
			ast_debug(1, "Agent %s: Ready for new call.\n", agent->username);
			agent_devstate_changed(agent->username);
		}
		break;
	}

	return 0;
}

/*!
 * \internal
 * \brief ast_bridge agent_hold pull method.
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \details
 * Remove any channel hooks controlled by the bridge.  Release
 * any resources held by bridge_channel->bridge_pvt and release
 * bridge_channel->bridge_pvt.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_agent_hold_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	ast_channel_remove_bridge_role(bridge_channel->chan, "holding_participant");
	ast_bridge_base_v_table.pull(self, bridge_channel);
}

/*!
 * \brief The bridge is being dissolved.
 *
 * \param self Bridge to operate upon.
 *
 * \details
 * The bridge is being dissolved.  Remove any external
 * references to the bridge so it can be destroyed.
 *
 * \note On entry, self must NOT be locked.
 *
 * \return Nothing
 */
static void bridge_agent_hold_dissolving(struct ast_bridge *self)
{
	ao2_global_obj_release(agent_holding);
	ast_bridge_base_v_table.dissolving(self);
}

static struct ast_bridge_methods bridge_agent_hold_v_table;

static struct ast_bridge *bridge_agent_hold_new(void)
{
	struct ast_bridge *bridge;

	bridge = bridge_alloc(sizeof(struct ast_bridge), &bridge_agent_hold_v_table);
	bridge = bridge_base_init(bridge, AST_BRIDGE_CAPABILITY_HOLDING,
		AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM
			| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_PROHIBITED,
		"AgentPool", NULL, NULL);
	bridge = bridge_register(bridge);
	return bridge;
}

static void bridge_init_agent_hold(void)
{
	/* Setup bridge agent_hold subclass v_table. */
	bridge_agent_hold_v_table = ast_bridge_base_v_table;
	bridge_agent_hold_v_table.name = "agent_hold";
	bridge_agent_hold_v_table.dissolving = bridge_agent_hold_dissolving;
	bridge_agent_hold_v_table.push = bridge_agent_hold_push;
	bridge_agent_hold_v_table.pull = bridge_agent_hold_pull;
}

static int bridge_agent_hold_deferred_create(void)
{
	RAII_VAR(struct ast_bridge *, holding, ao2_global_obj_ref(agent_holding), ao2_cleanup);

	if (!holding) {
		ast_mutex_lock(&agent_holding_lock);
		holding = ao2_global_obj_ref(agent_holding);
		if (!holding) {
			holding = bridge_agent_hold_new();
			ao2_global_obj_replace_unref(agent_holding, holding);
		}
		ast_mutex_unlock(&agent_holding_lock);
		if (!holding) {
			ast_log(LOG_ERROR, "Could not create agent holding bridge.\n");
			return -1;
		}
	}
	return 0;
}

static void send_agent_login(struct ast_channel *chan, const char *agent)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	ast_assert(agent != NULL);

	blob = ast_json_pack("{s: s}",
		"agent", agent);
	if (!blob) {
		return;
	}

	ast_channel_publish_cached_blob(chan, ast_channel_agent_login_type(), blob);
}

static void send_agent_logoff(struct ast_channel *chan, const char *agent, long logintime)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	ast_assert(agent != NULL);

	blob = ast_json_pack("{s: s, s: i}",
		"agent", agent,
		"logintime", logintime);
	if (!blob) {
		return;
	}

	ast_channel_publish_cached_blob(chan, ast_channel_agent_logoff_type(), blob);
}

/*!
 * \internal
 * \brief Logout the agent.
 * \since 12.0.0
 *
 * \param agent Which agent logging out.
 *
 * \note On entry agent is already locked.  On exit it is no longer locked.
 *
 * \return Nothing
 */
static void agent_logout(struct agent_pvt *agent)
{
	struct ast_channel *logged;
	struct ast_bridge *caller_bridge;
	long time_logged_in;

	time_logged_in = time(NULL) - agent->login_start;
	logged = agent->logged;
	agent->logged = NULL;
	caller_bridge = agent->caller_bridge;
	agent->caller_bridge = NULL;
	agent->state = AGENT_STATE_LOGGED_OUT;
	agent->devstate = AST_DEVICE_UNAVAILABLE;
	ast_clear_flag(agent, AST_FLAGS_ALL);
	agent_unlock(agent);
	agent_devstate_changed(agent->username);

	if (caller_bridge) {
		ast_bridge_destroy(caller_bridge, 0);
	}

	ast_channel_lock(logged);
	send_agent_logoff(logged, agent->username, time_logged_in);
	ast_channel_unlock(logged);
	ast_verb(2, "Agent '%s' logged out.  Logged in for %ld seconds.\n",
		agent->username, time_logged_in);
	ast_channel_unref(logged);
}

/*!
 * \internal
 * \brief Agent driver loop.
 * \since 12.0.0
 *
 * \param agent Which agent.
 * \param logged The logged in channel.
 *
 * \return Nothing
 */
static void agent_run(struct agent_pvt *agent, struct ast_channel *logged)
{
	struct ast_bridge_features features;

	if (ast_bridge_features_init(&features)) {
		ast_channel_hangupcause_set(logged, AST_CAUSE_NORMAL_CLEARING);
		goto agent_run_cleanup;
	}
	for (;;) {
		struct agents_cfg *cfgs;
		struct agent_cfg *cfg_new;
		struct agent_cfg *cfg_old;
		struct ast_bridge *holding;
		struct ast_bridge *caller_bridge;

		ast_channel_hangupcause_set(logged, AST_CAUSE_NORMAL_CLEARING);

		holding = ao2_global_obj_ref(agent_holding);
		if (!holding) {
			ast_debug(1, "Agent %s: Someone destroyed the agent holding bridge.\n",
				agent->username);
			break;
		}

		/*
		 * When the agent channel leaves the bridging system we usually
		 * want to put the agent back into the holding bridge for the
		 * next caller.
		 */
		ast_bridge_join(holding, logged, NULL, &features, NULL,
			AST_BRIDGE_JOIN_PASS_REFERENCE);
		if (logged != agent->logged) {
			/* This channel is no longer the logged in agent. */
			break;
		}

		if (agent->dead) {
			/* The agent is no longer configured. */
			break;
		}

		/* Update the agent's config before rejoining the holding bridge. */
		cfgs = ao2_global_obj_ref(cfg_handle);
		if (!cfgs) {
			/* There is no agent configuration.  All agents were destroyed. */
			break;
		}
		cfg_new = ao2_find(cfgs->agents, agent->username, OBJ_KEY);
		ao2_ref(cfgs, -1);
		if (!cfg_new) {
			/* The agent is no longer configured. */
			break;
		}
		agent_lock(agent);
		cfg_old = agent->cfg;
		agent->cfg = cfg_new;

		agent->last_disconnect = ast_tvnow();

		/* Clear out any caller bridge before rejoining the holding bridge. */
		caller_bridge = agent->caller_bridge;
		agent->caller_bridge = NULL;
		agent_unlock(agent);
		ao2_ref(cfg_old, -1);
		if (caller_bridge) {
			ast_bridge_destroy(caller_bridge, 0);
		}

		if (agent->state == AGENT_STATE_LOGGING_OUT
			|| agent->deferred_logoff
			|| ast_check_hangup_locked(logged)) {
			/* The agent was requested to logout or hungup. */
			break;
		}

		/*
		 * It is safe to access agent->waiting_colp without a lock.  It
		 * is only setup on agent login and not changed.
		 */
		ast_channel_update_connected_line(logged, &agent->waiting_colp, NULL);
	}
	ast_bridge_features_cleanup(&features);

agent_run_cleanup:
	agent_lock(agent);
	if (logged != agent->logged) {
		/*
		 * We are no longer the agent channel because of local channel
		 * optimization.
		 */
		agent_unlock(agent);
		ast_debug(1, "Agent %s: Channel %s is no longer the agent.\n",
			agent->username, ast_channel_name(logged));
		return;
	}
	agent_logout(agent);
}

static void agent_after_bridge_cb(struct ast_channel *chan, void *data)
{
	struct agent_pvt *agent;

	agent = ao2_find(agents, chan, 0);
	if (!agent) {
		return;
	}

	ast_debug(1, "Agent %s: New agent channel %s.\n",
		agent->username, ast_channel_name(chan));
	agent_run(agent, chan);
	ao2_ref(agent, -1);
}

static void agent_after_bridge_cb_failed(enum ast_bridge_after_cb_reason reason, void *data)
{
	struct ast_channel *chan = data;
	struct agent_pvt *agent;

	agent = ao2_find(agents, chan, 0);
	if (!agent) {
		return;
	}
	ast_log(LOG_WARNING, "Agent %s: Forced logout.  Lost control of %s because: %s\n",
		agent->username, ast_channel_name(chan),
		ast_bridge_after_cb_reason_string(reason));
	agent_lock(agent);
	agent_logout(agent);
	ao2_ref(agent, -1);
}

/*!
 * \internal
 * \brief Get the lock on the agent bridge_channel and return it.
 * \since 12.0.0
 *
 * \param agent Whose bridge_channel to get.
 *
 * \retval bridge_channel on success (Reffed and locked).
 * \retval NULL on error.
 */
static struct ast_bridge_channel *agent_bridge_channel_get_lock(struct agent_pvt *agent)
{
	struct ast_channel *logged;
	struct ast_bridge_channel *bc;

	for (;;) {
		agent_lock(agent);
		logged = agent->logged;
		if (!logged) {
			agent_unlock(agent);
			return NULL;
		}
		ast_channel_ref(logged);
		agent_unlock(agent);

		ast_channel_lock(logged);
		bc = ast_channel_get_bridge_channel(logged);
		ast_channel_unlock(logged);
		ast_channel_unref(logged);
		if (!bc) {
			if (agent->logged != logged) {
				continue;
			}
			return NULL;
		}

		ast_bridge_channel_lock(bc);
		if (bc->chan != logged || agent->logged != logged) {
			ast_bridge_channel_unlock(bc);
			ao2_ref(bc, -1);
			continue;
		}
		return bc;
	}
}

static void caller_abort_agent(struct agent_pvt *agent)
{
	struct ast_bridge_channel *logged;

	logged = agent_bridge_channel_get_lock(agent);
	if (!logged) {
		struct ast_bridge *caller_bridge;

		ast_debug(1, "Agent '%s' no longer logged in.\n", agent->username);

		agent_lock(agent);
		caller_bridge = agent->caller_bridge;
		agent->caller_bridge = NULL;
		agent_unlock(agent);
		if (caller_bridge) {
			ast_bridge_destroy(caller_bridge, 0);
		}
		return;
	}

	/* Kick the agent out of the holding bridge to reset it. */
	ast_bridge_channel_leave_bridge_nolock(logged, BRIDGE_CHANNEL_STATE_END,
		AST_CAUSE_NORMAL_CLEARING);
	ast_bridge_channel_unlock(logged);
}

static int caller_safety_timeout(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct agent_pvt *agent = hook_pvt;

	if (agent->state == AGENT_STATE_CALL_PRESENT) {
		ast_log(LOG_WARNING, "Agent '%s' process did not respond.  Safety timeout.\n",
			agent->username);
		pbx_builtin_setvar_helper(bridge_channel->chan, "AGENT_STATUS", "ERROR");

		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, 0);
		caller_abort_agent(agent);
	}

	return -1;
}

static void agent_alert(struct ast_bridge_channel *bridge_channel, const void *payload, size_t payload_size)
{
	const char *agent_id = payload;
	const char *playfile;
	const char *dtmf_accept;
	struct agent_pvt *agent;
	int digit;
	char dtmf[2];

	agent = ao2_find(agents, agent_id, OBJ_KEY);
	if (!agent) {
		ast_debug(1, "Agent '%s' does not exist.  Where did it go?\n", agent_id);
		return;
	}

	/* Change holding bridge participant role's idle mode to silence */
	ast_bridge_channel_lock_bridge(bridge_channel);
	ast_bridge_channel_clear_roles(bridge_channel);
	ast_channel_set_bridge_role_option(bridge_channel->chan, "holding_participant", "idle_mode", "silence");
	ast_bridge_channel_establish_roles(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);

	agent_lock(agent);
	playfile = ast_strdupa(agent->cfg->beep_sound);

	/* Determine which DTMF digits interrupt the alerting signal. */
	if (ast_test_flag(agent, AGENT_FLAG_ACK_CALL)
		? agent->override_ack_call : agent->cfg->ack_call) {
		dtmf_accept = ast_test_flag(agent, AGENT_FLAG_DTMF_ACCEPT)
			? agent->override_dtmf_accept : agent->cfg->dtmf_accept;

		/* Only the first digit of the ack will stop playback. */
		dtmf[0] = *dtmf_accept;
		dtmf[1] = '\0';
		dtmf_accept = dtmf;
	} else {
		dtmf_accept = NULL;
	}
	agent_unlock(agent);

	/* Alert the agent. */
	digit = ast_stream_and_wait(bridge_channel->chan, playfile,
		ast_strlen_zero(dtmf_accept) ? AST_DIGIT_ANY : dtmf_accept);
	ast_stopstream(bridge_channel->chan);

	agent_lock(agent);
	switch (agent->state) {
	case AGENT_STATE_CALL_PRESENT:
		if (!ast_strlen_zero(dtmf_accept)) {
			agent->state = AGENT_STATE_CALL_WAIT_ACK;
			agent->ack_time = ast_tvnow();

			if (0 < digit) {
				/* Playback was interrupted by a digit. */
				agent_unlock(agent);
				ao2_ref(agent, -1);
				ast_bridge_channel_feature_digit(bridge_channel, digit);
				return;
			}
			break;
		}

		/* Connect to caller now. */
		ast_debug(1, "Agent %s: Immediately connecting to call.\n", agent->username);
		agent_connect_caller(bridge_channel, agent);/* Will unlock agent. */
		ao2_ref(agent, -1);
		return;
	default:
		break;
	}
	agent_unlock(agent);
	ao2_ref(agent, -1);
}

static int send_alert_to_agent(struct ast_bridge_channel *bridge_channel, const char *agent_id)
{
	return ast_bridge_channel_queue_callback(bridge_channel,
		AST_BRIDGE_CHANNEL_CB_OPTION_MEDIA, agent_alert, agent_id, strlen(agent_id) + 1);
}

static int send_colp_to_agent(struct ast_bridge_channel *bridge_channel, struct ast_party_connected_line *connected)
{
	struct ast_set_party_connected_line update = {
		.id.name = 1,
		.id.number = 1,
		.id.subaddress = 1,
	};
	unsigned char data[1024];	/* This should be large enough */
	size_t datalen;

	datalen = ast_connected_line_build_data(data, sizeof(data), connected, &update);
	if (datalen == (size_t) -1) {
		return 0;
	}

	return ast_bridge_channel_queue_control_data(bridge_channel,
		AST_CONTROL_CONNECTED_LINE, data, datalen);
}

/*!
 * \internal
 * \brief Caller joined the bridge event callback.
 *
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
static int caller_joined_bridge(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct agent_pvt *agent = hook_pvt;
	struct ast_bridge_channel *logged;
	int res;

	logged = agent_bridge_channel_get_lock(agent);
	if (!logged) {
		ast_verb(3, "Agent '%s' not logged in.\n", agent->username);
		pbx_builtin_setvar_helper(bridge_channel->chan, "AGENT_STATUS", "NOT_LOGGED_IN");

		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, 0);
		caller_abort_agent(agent);
		return -1;
	}

	res = send_alert_to_agent(logged, agent->username);
	ast_bridge_channel_unlock(logged);
	ao2_ref(logged, -1);
	if (res) {
		ast_verb(3, "Agent '%s': Failed to alert the agent.\n", agent->username);
		pbx_builtin_setvar_helper(bridge_channel->chan, "AGENT_STATUS", "ERROR");

		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, 0);
		caller_abort_agent(agent);
		return -1;
	}

	pbx_builtin_setvar_helper(bridge_channel->chan, "AGENT_STATUS", "NOT_CONNECTED");
	ast_indicate(bridge_channel->chan, AST_CONTROL_RINGING);
	return -1;
}

/*!
 * \brief Dialplan AgentRequest application to locate an agent to talk with.
 *
 * \param chan Channel wanting to talk with an agent.
 * \param data Application parameters
 *
 * \retval 0 To continue in dialplan.
 * \retval -1 To hangup.
 */
static int agent_request_exec(struct ast_channel *chan, const char *data)
{
	struct ast_bridge *caller_bridge;
	struct ast_bridge_channel *logged;
	char *parse;
	int res;
	struct ast_bridge_features caller_features;
	struct ast_party_connected_line connected;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(agent_id);
		AST_APP_ARG(other);		/* Any remaining unused arguments */
	);

	RAII_VAR(struct agent_pvt *, agent, NULL, ao2_cleanup);

	if (bridge_agent_hold_deferred_create()) {
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.agent_id)) {
		ast_log(LOG_WARNING, "AgentRequest requires an AgentId\n");
		return -1;
	}

	/* Find the agent. */
	agent = ao2_find(agents, args.agent_id, OBJ_KEY);
	if (!agent) {
		ast_verb(3, "Agent '%s' does not exist.\n", args.agent_id);
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "INVALID");
		return 0;
	}

	if (ast_bridge_features_init(&caller_features)) {
		return -1;
	}

	/* Add safety timeout hook. */
	ao2_ref(agent, +1);
	if (ast_bridge_interval_hook(&caller_features, 0, CALLER_SAFETY_TIMEOUT_TIME,
		caller_safety_timeout, agent, __ao2_cleanup, AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		ao2_ref(agent, -1);
		ast_bridge_features_cleanup(&caller_features);
		return -1;
	}

	/* Setup the alert agent on caller joining the bridge hook. */
	ao2_ref(agent, +1);
	if (ast_bridge_join_hook(&caller_features, caller_joined_bridge, agent,
		__ao2_cleanup, 0)) {
		ao2_ref(agent, -1);
		ast_bridge_features_cleanup(&caller_features);
		return -1;
	}

	caller_bridge = ast_bridge_basic_new();
	if (!caller_bridge) {
		ast_bridge_features_cleanup(&caller_features);
		return -1;
	}

	agent_lock(agent);
	switch (agent->state) {
	case AGENT_STATE_LOGGED_OUT:
	case AGENT_STATE_LOGGING_OUT:
		agent_unlock(agent);
		ast_bridge_destroy(caller_bridge, 0);
		ast_bridge_features_cleanup(&caller_features);
		ast_verb(3, "Agent '%s' not logged in.\n", agent->username);
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "NOT_LOGGED_IN");
		return 0;
	case AGENT_STATE_READY_FOR_CALL:
		ao2_ref(caller_bridge, +1);
		agent->caller_bridge = caller_bridge;
		agent->state = AGENT_STATE_CALL_PRESENT;
		agent->devstate = AST_DEVICE_INUSE;
		break;
	default:
		agent_unlock(agent);
		ast_bridge_destroy(caller_bridge, 0);
		ast_bridge_features_cleanup(&caller_features);
		ast_verb(3, "Agent '%s' is busy.\n", agent->username);
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "BUSY");
		return 0;
	}
	agent_unlock(agent);
	agent_devstate_changed(agent->username);

	/* Get COLP for agent. */
	ast_party_connected_line_init(&connected);
	ast_channel_lock(chan);
	ast_connected_line_copy_from_caller(&connected, ast_channel_caller(chan));
	ast_channel_unlock(chan);

	logged = agent_bridge_channel_get_lock(agent);
	if (!logged) {
		ast_party_connected_line_free(&connected);
		caller_abort_agent(agent);
		ast_bridge_destroy(caller_bridge, 0);
		ast_bridge_features_cleanup(&caller_features);
		ast_verb(3, "Agent '%s' not logged in.\n", agent->username);
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "NOT_LOGGED_IN");
		return 0;
	}

	send_colp_to_agent(logged, &connected);
	ast_bridge_channel_unlock(logged);
	ao2_ref(logged, -1);
	ast_party_connected_line_free(&connected);

	if (ast_bridge_join(caller_bridge, chan, NULL, &caller_features, NULL,
		AST_BRIDGE_JOIN_PASS_REFERENCE)) {
		caller_abort_agent(agent);
		ast_verb(3, "Agent '%s': Caller %s failed to join the bridge.\n",
			agent->username, ast_channel_name(chan));
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "ERROR");
	}
	ast_bridge_features_cleanup(&caller_features);

	/* Determine if we need to continue in the dialplan after the bridge. */
	ast_channel_lock(chan);
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		/*
		 * The bridge was broken for a hangup that isn't real.
		 * Don't run the h extension, because the channel isn't
		 * really hung up.  This should really only happen with
		 * AST_SOFTHANGUP_ASYNCGOTO.
		 */
		res = 0;
	} else {
		res = ast_check_hangup(chan)
			|| ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE)
			|| ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENT_STATUS"));
	}
	ast_channel_unlock(chan);

	return res ? -1 : 0;
}

/*!
 * \internal
 * \brief Get agent config values from the login channel.
 * \since 12.0.0
 *
 * \param agent What to setup channel config values on.
 * \param chan Channel logging in as an agent.
 *
 * \return Nothing
 */
static void agent_login_channel_config(struct agent_pvt *agent, struct ast_channel *chan)
{
	struct ast_flags opts = { 0 };
	struct ast_party_connected_line connected;
	unsigned int override_ack_call = 0;
	unsigned int override_auto_logoff = 0;
	unsigned int override_wrapup_time = 0;
	const char *override_dtmf_accept = NULL;
	const char *var;

	ast_party_connected_line_init(&connected);

	/* Get config values from channel. */
	ast_channel_lock(chan);
	ast_party_connected_line_copy(&connected, ast_channel_connected(chan));

	var = pbx_builtin_getvar_helper(chan, "AGENTACKCALL");
	if (!ast_strlen_zero(var)) {
		override_ack_call = ast_true(var) ? 1 : 0;
		ast_set_flag(&opts, AGENT_FLAG_ACK_CALL);
	}

	var = pbx_builtin_getvar_helper(chan, "AGENTACCEPTDTMF");
	if (!ast_strlen_zero(var)) {
		override_dtmf_accept = ast_strdupa(var);
		ast_set_flag(&opts, AGENT_FLAG_DTMF_ACCEPT);
	}

	var = pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF");
	if (!ast_strlen_zero(var)) {
		if (sscanf(var, "%u", &override_auto_logoff) == 1) {
			ast_set_flag(&opts, AGENT_FLAG_AUTO_LOGOFF);
		}
	}

	var = pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME");
	if (!ast_strlen_zero(var)) {
		if (sscanf(var, "%u", &override_wrapup_time) == 1) {
			ast_set_flag(&opts, AGENT_FLAG_WRAPUP_TIME);
		}
	}
	ast_channel_unlock(chan);

	/* Set config values on agent. */
	agent_lock(agent);
	ast_party_connected_line_free(&agent->waiting_colp);
	agent->waiting_colp = connected;

	ast_string_field_set(agent, override_dtmf_accept, override_dtmf_accept);
	ast_copy_flags(agent, &opts, AST_FLAGS_ALL);
	agent->override_auto_logoff = override_auto_logoff;
	agent->override_wrapup_time = override_wrapup_time;
	agent->override_ack_call = override_ack_call;
	agent_unlock(agent);
}

enum AGENT_LOGIN_OPT_FLAGS {
	OPT_SILENT = (1 << 0),
};
AST_APP_OPTIONS(agent_login_opts, BEGIN_OPTIONS
	AST_APP_OPTION('s', OPT_SILENT),
END_OPTIONS);

/*!
 * \brief Dialplan AgentLogin application to log in an agent.
 *
 * \param chan Channel attempting to login as an agent.
 * \param data Application parameters
 *
 * \retval 0 To continue in dialplan.
 * \retval -1 To hangup.
 */
static int agent_login_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	struct ast_flags opts;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(agent_id);
		AST_APP_ARG(options);
		AST_APP_ARG(other);		/* Any remaining unused arguments */
	);

	RAII_VAR(struct agent_pvt *, agent, NULL, ao2_cleanup);

	if (bridge_agent_hold_deferred_create()) {
		return -1;
	}

	if (ast_channel_state(chan) != AST_STATE_UP && ast_answer(chan)) {
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.agent_id)) {
		ast_log(LOG_WARNING, "AgentLogin requires an AgentId\n");
		return -1;
	}

	if (ast_app_parse_options(agent_login_opts, &opts, NULL, args.options)) {
		/* General invalid option syntax. */
		return -1;
	}

	/* Find the agent. */
	agent = ao2_find(agents, args.agent_id, OBJ_KEY);
	if (!agent) {
		ast_verb(3, "Agent '%s' does not exist.\n", args.agent_id);
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "INVALID");
		return 0;
	}

	/* Has someone already logged in as this agent already? */
	agent_lock(agent);
	if (agent->logged) {
		agent_unlock(agent);
		ast_verb(3, "Agent '%s' already logged in.\n", agent->username);
		pbx_builtin_setvar_helper(chan, "AGENT_STATUS", "ALREADY_LOGGED_IN");
		return 0;
	}
	agent->logged = ast_channel_ref(chan);
	agent->last_disconnect = ast_tvnow();
	time(&agent->login_start);
	agent->deferred_logoff = 0;
	agent_unlock(agent);

	agent_login_channel_config(agent, chan);

	if (!ast_test_flag(&opts, OPT_SILENT)) {
		ast_stream_and_wait(chan, "agent-loginok", AST_DIGIT_NONE);
	}

	ast_verb(2, "Agent '%s' logged in (format %s/%s)\n", agent->username,
		ast_format_get_name(ast_channel_readformat(chan)),
		ast_format_get_name(ast_channel_writeformat(chan)));
	ast_channel_lock(chan);
	send_agent_login(chan, agent->username);
	ast_channel_unlock(chan);

	agent_run(agent, chan);
	return -1;
}

static int agent_function_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parse;
	struct agent_pvt *agent;
	struct ast_channel *logged;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(agentid);
		AST_APP_ARG(item);
	);

	buf[0] = '\0';

	parse = ast_strdupa(data ?: "");
	AST_NONSTANDARD_APP_ARGS(args, parse, ':');

	if (ast_strlen_zero(args.agentid)) {
		ast_log(LOG_WARNING, "The AGENT function requires an argument - agentid!\n");
		return -1;
	}
	if (!args.item) {
		args.item = "status";
	}

	agent = ao2_find(agents, args.agentid, OBJ_KEY);
	if (!agent) {
		ast_log(LOG_WARNING, "Agent '%s' not found!\n", args.agentid);
		return -1;
	}

	agent_lock(agent);
	if (!strcasecmp(args.item, "status")) {
		const char *status;

		if (agent->logged) {
			status = "LOGGEDIN";
		} else {
			status = "LOGGEDOUT";
		}
		ast_copy_string(buf, status, len);
	} else if (!strcasecmp(args.item, "name")) {
		ast_copy_string(buf, agent->cfg->full_name, len);
	} else if (!strcasecmp(args.item, "mohclass")) {
		ast_copy_string(buf, agent->cfg->moh, len);
	} else if (!strcasecmp(args.item, "channel")) {
		logged = agent_lock_logged(agent);
		if (logged) {
			char *pos;

			ast_copy_string(buf, ast_channel_name(logged), len);
			ast_channel_unlock(logged);
			ast_channel_unref(logged);

			pos = strrchr(buf, '-');
			if (pos) {
				*pos = '\0';
			}
		}
	} else if (!strcasecmp(args.item, "fullchannel")) {
		logged = agent_lock_logged(agent);
		if (logged) {
			ast_copy_string(buf, ast_channel_name(logged), len);
			ast_channel_unlock(logged);
			ast_channel_unref(logged);
		}
	}
	agent_unlock(agent);
	ao2_ref(agent, -1);

	return 0;
}

static struct ast_custom_function agent_function = {
	.name = "AGENT",
	.read = agent_function_read,
};

struct agent_complete {
	/*! Nth match to return. */
	int state;
	/*! Which match currently on. */
	int which;
};

static int complete_agent_search(void *obj, void *arg, void *data, int flags)
{
	struct agent_complete *search = data;

	if (++search->which > search->state) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_agent(const char *word, int state)
{
	char *ret;
	struct agent_pvt *agent;
	struct agent_complete search = {
		.state = state,
	};

	agent = ao2_callback_data(agents, ast_strlen_zero(word) ? 0 : OBJ_PARTIAL_KEY,
		complete_agent_search, (char *) word, &search);
	if (!agent) {
		return NULL;
	}
	ret = ast_strdup(agent->username);
	ao2_ref(agent, -1);
	return ret;
}

static int complete_agent_logoff_search(void *obj, void *arg, void *data, int flags)
{
	struct agent_pvt *agent = obj;
	struct agent_complete *search = data;

	if (!agent->logged) {
		return 0;
	}
	if (++search->which > search->state) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_agent_logoff(const char *word, int state)
{
	char *ret;
	struct agent_pvt *agent;
	struct agent_complete search = {
		.state = state,
	};

	agent = ao2_callback_data(agents, ast_strlen_zero(word) ? 0 : OBJ_PARTIAL_KEY,
		complete_agent_logoff_search, (char *) word, &search);
	if (!agent) {
		return NULL;
	}
	ret = ast_strdup(agent->username);
	ao2_ref(agent, -1);
	return ret;
}

static void agent_show_requested(struct ast_cli_args *a, int online_only)
{
#define FORMAT_HDR "%-8s %-20s %-11s %-30s %s\n"
#define FORMAT_ROW "%-8s %-20s %-11s %-30s %s\n"

	struct ao2_iterator iter;
	struct agent_pvt *agent;
	struct ast_str *out = ast_str_alloca(512);
	unsigned int agents_total = 0;
	unsigned int agents_logged_in = 0;
	unsigned int agents_talking = 0;

	ast_cli(a->fd, FORMAT_HDR, "Agent-ID", "Name", "State", "Channel", "Talking with");
	iter = ao2_iterator_init(agents, 0);
	for (; (agent = ao2_iterator_next(&iter)); ao2_ref(agent, -1)) {
		struct ast_channel *logged;

		++agents_total;

		agent_lock(agent);
		logged = agent_lock_logged(agent);
		if (logged) {
			const char *talking_with;

			++agents_logged_in;

			talking_with = pbx_builtin_getvar_helper(logged, "BRIDGEPEER");
			if (!ast_strlen_zero(talking_with)) {
				++agents_talking;
			} else {
				talking_with = "";
			}
			ast_str_set(&out, 0, FORMAT_ROW, agent->username, agent->cfg->full_name,
				ast_devstate_str(agent->devstate), ast_channel_name(logged), talking_with);
			ast_channel_unlock(logged);
			ast_channel_unref(logged);
		} else {
			ast_str_set(&out, 0, FORMAT_ROW, agent->username, agent->cfg->full_name,
				ast_devstate_str(agent->devstate), "", "");
		}
		agent_unlock(agent);

		if (!online_only || logged) {
			ast_cli(a->fd, "%s", ast_str_buffer(out));
		}
	}
	ao2_iterator_destroy(&iter);

	ast_cli(a->fd, "\nDefined agents: %u, Logged in: %u, Talking: %u\n",
		agents_total, agents_logged_in, agents_talking);

#undef FORMAT_HDR
#undef FORMAT_ROW
}

static char *agent_handle_show_online(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "agent show online";
		e->usage =
			"Usage: agent show online\n"
			"       Provides summary information for logged in agents.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	agent_show_requested(a, 1);

	return CLI_SUCCESS;
}

static char *agent_handle_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "agent show all";
		e->usage =
			"Usage: agent show all\n"
			"       Provides summary information for all agents.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	agent_show_requested(a, 0);

	return CLI_SUCCESS;
}

static char *agent_handle_show_specific(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct agent_pvt *agent;
	struct ast_channel *logged;
	struct ast_str *out = ast_str_alloca(4096);

	switch (cmd) {
	case CLI_INIT:
		e->command = "agent show";
		e->usage =
			"Usage: agent show <agent-id>\n"
			"       Show information about the <agent-id> agent\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_agent(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	agent = ao2_find(agents, a->argv[2], OBJ_KEY);
	if (!agent) {
		ast_cli(a->fd, "Agent '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	agent_lock(agent);
	logged = agent_lock_logged(agent);
	ast_str_set(&out, 0, "Id: %s\n", agent->username);
	ast_str_append(&out, 0, "Name: %s\n", agent->cfg->full_name);
	ast_str_append(&out, 0, "Beep: %s\n", agent->cfg->beep_sound);
	ast_str_append(&out, 0, "MOH: %s\n", agent->cfg->moh);
	ast_str_append(&out, 0, "RecordCalls: %s\n", AST_CLI_YESNO(agent->cfg->record_agent_calls));
	ast_str_append(&out, 0, "State: %s\n", ast_devstate_str(agent->devstate));
	if (logged) {
		const char *talking_with;

		ast_str_append(&out, 0, "LoggedInChannel: %s\n", ast_channel_name(logged));
		ast_str_append(&out, 0, "LoggedInTime: %ld\n", (long) agent->login_start);
		talking_with = pbx_builtin_getvar_helper(logged, "BRIDGEPEER");
		if (!ast_strlen_zero(talking_with)) {
			ast_str_append(&out, 0, "TalkingWith: %s\n", talking_with);
			ast_str_append(&out, 0, "CallStarted: %ld\n", (long) agent->call_start);
		}
		ast_channel_unlock(logged);
		ast_channel_unref(logged);
	}
	agent_unlock(agent);
	ao2_ref(agent, -1);

	ast_cli(a->fd, "%s", ast_str_buffer(out));

	return CLI_SUCCESS;
}

static char *agent_handle_logoff_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "agent logoff";
		e->usage =
			"Usage: agent logoff <agent-id> [soft]\n"
			"       Sets an agent as no longer logged in.\n"
			"       If 'soft' is specified, do not hangup existing calls.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_agent_logoff(a->word, a->n);
		} else if (a->pos == 3 && a->n == 0
			&& (ast_strlen_zero(a->word)
				|| !strncasecmp("soft", a->word, strlen(a->word)))) {
			return ast_strdup("soft");
		}
		return NULL;
	}

	if (a->argc < 3 || 4 < a->argc) {
		return CLI_SHOWUSAGE;
	}
	if (a->argc == 4 && strcasecmp(a->argv[3], "soft")) {
		return CLI_SHOWUSAGE;
	}

	if (!agent_logoff_request(a->argv[2], a->argc == 4)) {
		ast_cli(a->fd, "Logging out %s\n", a->argv[2]);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_agents[] = {
	AST_CLI_DEFINE(agent_handle_show_online, "Show status of online agents"),
	AST_CLI_DEFINE(agent_handle_show_all, "Show status of all agents"),
	AST_CLI_DEFINE(agent_handle_show_specific, "Show information about an agent"),
	AST_CLI_DEFINE(agent_handle_logoff_cmd, "Sets an agent offline"),
};

static int action_agents(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	char id_text[AST_MAX_BUF];
	struct ao2_iterator iter;
	struct agent_pvt *agent;
	struct ast_str *out = ast_str_alloca(4096);

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}
	astman_send_ack(s, m, "Agents will follow");

	iter = ao2_iterator_init(agents, 0);
	for (; (agent = ao2_iterator_next(&iter)); ao2_ref(agent, -1)) {
		struct ast_channel *logged;

		agent_lock(agent);
		logged = agent_lock_logged(agent);

		/*
		 * Status Values:
		 * AGENT_LOGGEDOFF - Agent isn't logged in
		 * AGENT_IDLE      - Agent is logged in, and waiting for call
		 * AGENT_ONCALL    - Agent is logged in, and on a call
		 * AGENT_UNKNOWN   - Don't know anything about agent. Shouldn't ever get this.
		 */
		ast_str_set(&out, 0, "Agent: %s\r\n", agent->username);
		ast_str_append(&out, 0, "Name: %s\r\n", agent->cfg->full_name);

		if (logged) {
			const char *talking_to_chan;
			struct ast_str *logged_headers;
			RAII_VAR(struct ast_channel_snapshot *, logged_snapshot, ast_channel_snapshot_create(logged), ao2_cleanup);

			if (!logged_snapshot
				|| !(logged_headers =
					 ast_manager_build_channel_state_string(logged_snapshot))) {
				ast_channel_unlock(logged);
				ast_channel_unref(logged);
				agent_unlock(agent);
				continue;
			}

			talking_to_chan = pbx_builtin_getvar_helper(logged, "BRIDGEPEER");
			if (!ast_strlen_zero(talking_to_chan)) {
				ast_str_append(&out, 0, "Status: %s\r\n", "AGENT_ONCALL");
				ast_str_append(&out, 0, "TalkingToChan: %s\r\n", talking_to_chan);
				ast_str_append(&out, 0, "CallStarted: %ld\n", (long) agent->call_start);
			} else {
				ast_str_append(&out, 0, "Status: %s\r\n", "AGENT_IDLE");
			}
			ast_str_append(&out, 0, "LoggedInTime: %ld\r\n", (long) agent->login_start);
			ast_str_append(&out, 0, "%s", ast_str_buffer(logged_headers));
			ast_channel_unlock(logged);
			ast_channel_unref(logged);
			ast_free(logged_headers);
		} else {
			ast_str_append(&out, 0, "Status: %s\r\n", "AGENT_LOGGEDOFF");
		}

		agent_unlock(agent);

		astman_append(s, "Event: Agents\r\n"
			"%s%s\r\n",
			ast_str_buffer(out), id_text);
	}
	ao2_iterator_destroy(&iter);

	astman_append(s, "Event: AgentsComplete\r\n"
		"%s"
		"\r\n", id_text);
	return 0;
}

static int action_agent_logoff(struct mansession *s, const struct message *m)
{
	const char *agent = astman_get_header(m, "Agent");
	const char *soft_s = astman_get_header(m, "Soft"); /* "true" is don't hangup */

	if (ast_strlen_zero(agent)) {
		astman_send_error(s, m, "No agent specified");
		return 0;
	}

	if (!agent_logoff_request(agent, ast_true(soft_s))) {
		astman_send_ack(s, m, "Agent logged out");
	} else {
		astman_send_error(s, m, "No such agent");
	}

	return 0;
}

static int unload_module(void)
{
	struct ast_bridge *holding;

	/* Unregister dialplan applications */
	ast_unregister_application(app_agent_login);
	ast_unregister_application(app_agent_request);

	/* Unregister dialplan functions */
	ast_custom_function_unregister(&agent_function);

	/* Unregister manager command */
	ast_manager_unregister("Agents");
	ast_manager_unregister("AgentLogoff");

	/* Unregister CLI commands */
	ast_cli_unregister_multiple(cli_agents, ARRAY_LEN(cli_agents));

	ast_devstate_prov_del("Agent");

	/* Destroy agent holding bridge. */
	holding = ao2_global_obj_replace(agent_holding, NULL);
	if (holding) {
		ast_bridge_destroy(holding, 0);
	}

	destroy_config();
	ao2_cleanup(agents);
	agents = NULL;
	return 0;
}

static int load_module(void)
{
	int res = 0;

	agents = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, agent_pvt_sort_cmp, agent_pvt_cmp);
	if (!agents) {
		return AST_MODULE_LOAD_FAILURE;
	}

	/* Init agent holding bridge v_table. */
	bridge_init_agent_hold();

	/* Setup to provide Agent:agent-id device state. */
	res |= ast_devstate_prov_add("Agent", agent_pvt_devstate_get);

	/* CLI Commands */
	res |= ast_cli_register_multiple(cli_agents, ARRAY_LEN(cli_agents));

	/* Manager commands */
	res |= ast_manager_register_xml("Agents", EVENT_FLAG_AGENT, action_agents);
	res |= ast_manager_register_xml("AgentLogoff", EVENT_FLAG_AGENT, action_agent_logoff);

	/* Dialplan Functions */
	res |= ast_custom_function_register(&agent_function);

	/* Dialplan applications */
	res |= ast_register_application_xml(app_agent_login, agent_login_exec);
	res |= ast_register_application_xml(app_agent_request, agent_request_exec);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	if (load_config()) {
		ast_log(LOG_ERROR, "Unable to load config. Not loading module.\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		/* Just keep the config we already have in place. */
		return -1;
	}
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Call center agent pool applications",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);
