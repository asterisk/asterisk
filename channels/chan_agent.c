/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
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
 * \brief Implementation of Agents (proxy channel)
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * This file is the implementation of Agents modules.
 * It is a dynamic module that is loaded by Asterisk. 
 * \par See also
 * \arg \ref Config_agent
 *
 * \ingroup channel_drivers
 */
/*** MODULEINFO
        <depend>chan_local</depend>
        <depend>res_monitor</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/features.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astdb.h"
#include "asterisk/devicestate.h"
#include "asterisk/monitor.h"
#include "asterisk/stringfields.h"
#include "asterisk/event.h"
#include "asterisk/data.h"

/*** DOCUMENTATION
	<application name="AgentLogin" language="en_US">
		<synopsis>
			Call agent login.
		</synopsis>
		<syntax>
			<parameter name="AgentNo" />
			<parameter name="options">
				<optionlist>
					<option name="s">
						<para>silent login - do not announce the login ok segment after
						agent logged on/off</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Asks the agent to login to the system. Always returns <literal>-1</literal>.
			While logged in, the agent can receive calls and will hear a <literal>beep</literal>
			when a new call comes in. The agent can dump the call by pressing the star key.</para>
		</description>
		<see-also>
			<ref type="application">Queue</ref>
			<ref type="application">AddQueueMember</ref>
			<ref type="application">RemoveQueueMember</ref>
			<ref type="application">PauseQueueMember</ref>
			<ref type="application">UnpauseQueueMember</ref>
			<ref type="function">AGENT</ref>
			<ref type="filename">agents.conf</ref>
			<ref type="filename">queues.conf</ref>
		</see-also>
	</application>
	<application name="AgentMonitorOutgoing" language="en_US">
		<synopsis>
			Record agent's outgoing call.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="d">
						<para>make the app return <literal>-1</literal> if there is an error condition.</para>
					</option>
					<option name="c">
						<para>change the CDR so that the source of the call is
						<literal>Agent/agent_id</literal></para>
					</option>
					<option name="n">
						<para>don't generate the warnings when there is no callerid or the
						agentid is not known. It's handy if you want to have one context
						for agent and non-agent calls.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Tries to figure out the id of the agent who is placing outgoing call based on
			comparison of the callerid of the current interface and the global variable
			placed by the AgentCallbackLogin application. That's why it should be used only
			with the AgentCallbackLogin app. Uses the monitoring functions in chan_agent
			instead of Monitor application. That has to be configured in the
			<filename>agents.conf</filename> file.</para>
			<para>Normally the app returns <literal>0</literal> unless the options are passed.</para>
		</description>
		<see-also>
			<ref type="filename">agents.conf</ref>
		</see-also>
	</application>
	<function name="AGENT" language="en_US">
		<synopsis>
			Gets information about an Agent
		</synopsis>
		<syntax argsep=":">
			<parameter name="agentid" required="true" />
			<parameter name="item">
				<para>The valid items to retrieve are:</para>
				<enumlist>
					<enum name="status">
						<para>(default) The status of the agent (LOGGEDIN | LOGGEDOUT)</para>
					</enum>
					<enum name="password">
						<para>The password of the agent</para>
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
			<para>Will list info about all possible agents.</para>
		</description>
	</manager>
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
 ***/

static const char tdesc[] = "Call Agent Proxy Channel";
static const char config[] = "agents.conf";

static const char app[] = "AgentLogin";
static const char app3[] = "AgentMonitorOutgoing";

static char moh[80] = "default";

#define AST_MAX_AGENT	80                          /*!< Agent ID or Password max length */
#define AST_MAX_BUF	256
#define AST_MAX_FILENAME_LEN	256

#define PA_MAX_LEN 2048                             /*!< The maximum length of each persistent member agent database entry */

#define DEFAULT_ACCEPTDTMF '#'
#define DEFAULT_ENDDTMF '*'

static ast_group_t group;
static int autologoff;
static int wrapuptime;
static int ackcall;
static int endcall;
static int autologoffunavail = 0;
static char acceptdtmf = DEFAULT_ACCEPTDTMF;
static char enddtmf = DEFAULT_ENDDTMF;

static int maxlogintries = 3;
static char agentgoodbye[AST_MAX_FILENAME_LEN] = "vm-goodbye";

static int recordagentcalls = 0;
static char recordformat[AST_MAX_BUF] = "";
static char recordformatext[AST_MAX_BUF] = "";
static char urlprefix[AST_MAX_BUF] = "";
static char savecallsin[AST_MAX_BUF] = "";
static int updatecdr = 0;
static char beep[AST_MAX_BUF] = "beep";

#define GETAGENTBYCALLERID	"AGENTBYCALLERID"

enum {
	AGENT_FLAG_ACKCALL = (1 << 0),
	AGENT_FLAG_AUTOLOGOFF = (1 << 1),
	AGENT_FLAG_WRAPUPTIME = (1 << 2),
	AGENT_FLAG_ACCEPTDTMF = (1 << 3),
	AGENT_FLAG_ENDDTMF = (1 << 4),
};

/*! \brief Structure representing an agent.  */
struct agent_pvt {
	ast_mutex_t lock;              /*!< Channel private lock */
	int dead;                      /*!< Poised for destruction? */
	int pending;                   /*!< Not a real agent -- just pending a match */
	int abouttograb;               /*!< About to grab */
	int autologoff;                /*!< Auto timeout time */
	int ackcall;                   /*!< ackcall */
	int deferlogoff;               /*!< Defer logoff to hangup */
	char acceptdtmf;
	char enddtmf;
	time_t loginstart;             /*!< When agent first logged in (0 when logged off) */
	time_t start;                  /*!< When call started */
	struct timeval lastdisc;       /*!< When last disconnected */
	int wrapuptime;                /*!< Wrapup time in ms */
	ast_group_t group;             /*!< Group memberships */
	int acknowledged;              /*!< Acknowledged */
	char moh[80];                  /*!< Which music on hold */
	char agent[AST_MAX_AGENT];     /*!< Agent ID */
	char password[AST_MAX_AGENT];  /*!< Password for Agent login */
	char name[AST_MAX_AGENT];
	int app_lock_flag;
	ast_cond_t app_complete_cond;
	ast_cond_t login_wait_cond;
	int app_sleep_cond;            /*!< Non-zero if the login app should sleep. */
	struct ast_channel *owner;     /*!< Agent */
	struct ast_channel *chan;      /*!< Channel we use */
	unsigned int flags;            /*!< Flags show if settings were applied with channel vars */
	AST_LIST_ENTRY(agent_pvt) list;/*!< Next Agent in the linked list. */
};

#define DATA_EXPORT_AGENT(MEMBER)				\
	MEMBER(agent_pvt, autologoff, AST_DATA_INTEGER)		\
	MEMBER(agent_pvt, ackcall, AST_DATA_BOOLEAN)		\
	MEMBER(agent_pvt, deferlogoff, AST_DATA_BOOLEAN)	\
	MEMBER(agent_pvt, wrapuptime, AST_DATA_MILLISECONDS)	\
	MEMBER(agent_pvt, acknowledged, AST_DATA_BOOLEAN)	\
	MEMBER(agent_pvt, name, AST_DATA_STRING)		\
	MEMBER(agent_pvt, password, AST_DATA_PASSWORD)		\
	MEMBER(agent_pvt, acceptdtmf, AST_DATA_CHARACTER)

AST_DATA_STRUCTURE(agent_pvt, DATA_EXPORT_AGENT);

static AST_LIST_HEAD_STATIC(agents, agent_pvt);	/*!< Holds the list of agents (loaded form agents.conf). */

#define CHECK_FORMATS(ast, p) do { \
	if (p->chan) {\
		if (!(ast_format_cap_identical(ast_channel_nativeformats(ast), ast_channel_nativeformats(p->chan)))) { \
			char tmp1[256], tmp2[256]; \
			ast_debug(1, "Native formats changing from '%s' to '%s'\n", ast_getformatname_multiple(tmp1, sizeof(tmp1), ast_channel_nativeformats(ast)), ast_getformatname_multiple(tmp2, sizeof(tmp2), ast_channel_nativeformats(p->chan))); \
			/* Native formats changed, reset things */ \
			ast_format_cap_copy(ast_channel_nativeformats(ast), ast_channel_nativeformats(p->chan)); \
			ast_debug(1, "Resetting read to '%s' and write to '%s'\n", ast_getformatname(ast_channel_readformat(ast)), ast_getformatname(ast_channel_writeformat(ast)));\
			ast_set_read_format(ast, ast_channel_readformat(ast)); \
			ast_set_write_format(ast, ast_channel_writeformat(ast)); \
		} \
		if ((ast_format_cmp(ast_channel_readformat(p->chan), ast_channel_rawreadformat(ast)) != AST_FORMAT_CMP_EQUAL) && !ast_channel_generator(p->chan))  \
			ast_set_read_format(p->chan, ast_channel_rawreadformat(ast)); \
		if ((ast_format_cmp(ast_channel_writeformat(p->chan), ast_channel_rawwriteformat(ast)) != AST_FORMAT_CMP_EQUAL) && !ast_channel_generator(p->chan)) \
			ast_set_write_format(p->chan, ast_channel_rawwriteformat(ast)); \
	} \
} while(0)

/*! \brief Cleanup moves all the relevant FD's from the 2nd to the first, but retains things
   properly for a timingfd XXX This might need more work if agents were logged in as agents or other
   totally impractical combinations XXX */

#define CLEANUP(ast, p) do { \
	int x; \
	if (p->chan) { \
		for (x = 0; x < AST_MAX_FDS; x++) { \
			if (x != AST_TIMING_FD) { \
				ast_channel_set_fd(ast, x, ast_channel_fd(p->chan, x)); \
			} \
		} \
		ast_channel_set_fd(ast, AST_AGENT_FD, ast_channel_fd(p->chan, AST_TIMING_FD)); \
	} \
} while(0)

/*--- Forward declarations */
static struct ast_channel *agent_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
static int agent_devicestate(const char *data);
static int agent_digit_begin(struct ast_channel *ast, char digit);
static int agent_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int agent_call(struct ast_channel *ast, const char *dest, int timeout);
static int agent_hangup(struct ast_channel *ast);
static int agent_answer(struct ast_channel *ast);
static struct ast_frame *agent_read(struct ast_channel *ast);
static int agent_write(struct ast_channel *ast, struct ast_frame *f);
static int agent_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);
static int agent_sendtext(struct ast_channel *ast, const char *text);
static int agent_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int agent_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static struct ast_channel *agent_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge);
static char *complete_agent_logoff_cmd(const char *line, const char *word, int pos, int state);
static struct ast_channel* agent_get_base_channel(struct ast_channel *chan);
static int agent_logoff(const char *agent, int soft);

/*! \brief Channel interface description for PBX integration */
static struct ast_channel_tech agent_tech = {
	.type = "Agent",
	.description = tdesc,
	.requester = agent_request,
	.devicestate = agent_devicestate,
	.send_digit_begin = agent_digit_begin,
	.send_digit_end = agent_digit_end,
	.call = agent_call,
	.hangup = agent_hangup,
	.answer = agent_answer,
	.read = agent_read,
	.write = agent_write,
	.write_video = agent_write,
	.send_html = agent_sendhtml,
	.send_text = agent_sendtext,
	.exception = agent_read,
	.indicate = agent_indicate,
	.fixup = agent_fixup,
	.bridged_channel = agent_bridgedchannel,
	.get_base_channel = agent_get_base_channel,
};

/*!
 * \brief Locks the owning channel for a LOCKED pvt while obeying locking order. The pvt
 * must enter this function locked and will be returned locked, but this function will
 * unlock the pvt for a short time, so it can't be used while expecting the pvt to remain
 * static. If function returns a non NULL channel, it will need to be unlocked and
 * unrefed once it is no longer needed.
 *
 * \param pvt Pointer to the LOCKED agent_pvt for which the owner is needed
 * \ret locked channel which owns the pvt at the time of completion. NULL if not available.
 */
static struct ast_channel *agent_lock_owner(struct agent_pvt *pvt)
{
	struct ast_channel *owner;

	for (;;) {
		if (!pvt->owner) { /* No owner. Nothing to do. */
			return NULL;
		}

		/* If we don't ref the owner, it could be killed when we unlock the pvt. */
		owner = ast_channel_ref(pvt->owner);

		/* Locking order requires us to lock channel, then pvt. */
		ast_mutex_unlock(&pvt->lock);
		ast_channel_lock(owner);
		ast_mutex_lock(&pvt->lock);

		/* Check if owner changed during pvt unlock period */
		if (owner != pvt->owner) { /* Channel changed. Unref and do another pass. */
			ast_channel_unlock(owner);
			owner = ast_channel_unref(owner);
		} else { /* Channel stayed the same. Return it. */
			return owner;
		}
	}
}

/*!
 * \internal
 * \brief Destroy an agent pvt struct.
 *
 * \param doomed Agent pvt to destroy.
 *
 * \return Nothing
 */
static void agent_pvt_destroy(struct agent_pvt *doomed)
{
	ast_mutex_destroy(&doomed->lock);
	ast_cond_destroy(&doomed->app_complete_cond);
	ast_cond_destroy(&doomed->login_wait_cond);
	ast_free(doomed);
}

/*!
 * Adds an agent to the global list of agents.
 *
 * \param agent A string with the username, password and real name of an agent. As defined in agents.conf. Example: "13,169,John Smith"
 * \param pending If it is pending or not.
 * @return The just created agent.
 * \sa agent_pvt, agents.
 */
static struct agent_pvt *add_agent(const char *agent, int pending)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(agt);
		AST_APP_ARG(password);
		AST_APP_ARG(name);
	);
	char *password = NULL;
	char *name = NULL;
	char *agt = NULL;
	struct agent_pvt *p;

	parse = ast_strdupa(agent);

	/* Extract username (agt), password and name from agent (args). */
	AST_STANDARD_APP_ARGS(args, parse);

	if(args.argc == 0) {
		ast_log(LOG_WARNING, "A blank agent line!\n");
		return NULL;
	}

	if(ast_strlen_zero(args.agt) ) {
		ast_log(LOG_WARNING, "An agent line with no agentid!\n");
		return NULL;
	} else
		agt = args.agt;

	if(!ast_strlen_zero(args.password)) {
		password = args.password;
		while (*password && *password < 33) password++;
	}
	if(!ast_strlen_zero(args.name)) {
		name = args.name;
		while (*name && *name < 33) name++;
	}

	if (!pending) {
		/* Are we searching for the agent here ? To see if it exists already ? */
		AST_LIST_TRAVERSE(&agents, p, list) {
			if (!strcmp(p->agent, agt)) {
				break;
			}
		}
	} else {
		p = NULL;
	}
	if (!p) {
		// Build the agent.
		if (!(p = ast_calloc(1, sizeof(*p))))
			return NULL;
		ast_copy_string(p->agent, agt, sizeof(p->agent));
		ast_mutex_init(&p->lock);
		ast_cond_init(&p->app_complete_cond, NULL);
		ast_cond_init(&p->login_wait_cond, NULL);
		p->app_lock_flag = 0;
		p->app_sleep_cond = 1;
		p->group = group;
		p->pending = pending;
		AST_LIST_INSERT_TAIL(&agents, p, list);
	}
	
	ast_copy_string(p->password, password ? password : "", sizeof(p->password));
	ast_copy_string(p->name, name ? name : "", sizeof(p->name));
	ast_copy_string(p->moh, moh, sizeof(p->moh));
	if (!ast_test_flag(p, AGENT_FLAG_ACKCALL)) {
		p->ackcall = ackcall;
	}
	if (!ast_test_flag(p, AGENT_FLAG_AUTOLOGOFF)) {
		p->autologoff = autologoff;
	}
	if (!ast_test_flag(p, AGENT_FLAG_ACCEPTDTMF)) {
		p->acceptdtmf = acceptdtmf;
	}
	if (!ast_test_flag(p, AGENT_FLAG_ENDDTMF)) {
		p->enddtmf = enddtmf;
	}

	/* If someone reduces the wrapuptime and reloads, we want it
	 * to change the wrapuptime immediately on all calls */
	if (!ast_test_flag(p, AGENT_FLAG_WRAPUPTIME) && p->wrapuptime > wrapuptime) {
		struct timeval now = ast_tvnow();
		/* XXX check what is this exactly */

		/* We won't be pedantic and check the tv_usec val */
		if (p->lastdisc.tv_sec > (now.tv_sec + wrapuptime/1000)) {
			p->lastdisc.tv_sec = now.tv_sec + wrapuptime/1000;
			p->lastdisc.tv_usec = now.tv_usec;
		}
	}
	p->wrapuptime = wrapuptime;

	if (pending)
		p->dead = 1;
	else
		p->dead = 0;
	return p;
}

/*!
 * Deletes an agent after doing some clean up.
 * Further documentation: How safe is this function ? What state should the agent be to be cleaned.
 *
 * \warning XXX This function seems to be very unsafe.
 * Potential for double free and use after free among other
 * problems.
 *
 * \param p Agent to be deleted.
 * \returns Always 0.
 */
static int agent_cleanup(struct agent_pvt *p)
{
	struct ast_channel *chan;

	ast_mutex_lock(&p->lock);
	chan = p->owner;
	p->owner = NULL;
	/* Release ownership of the agent to other threads (presumably running the login app). */
	p->app_sleep_cond = 1;
	p->app_lock_flag = 0;
	ast_cond_signal(&p->app_complete_cond);
	if (chan) {
		ast_channel_tech_pvt_set(chan, NULL);
		chan = ast_channel_release(chan);
	}
	if (p->dead) {
		ast_mutex_unlock(&p->lock);
		agent_pvt_destroy(p);
	} else {
		ast_mutex_unlock(&p->lock);
	}
	return 0;
}

static int agent_answer(struct ast_channel *ast)
{
	ast_log(LOG_WARNING, "Huh?  Agent is being asked to answer?\n");
	return -1;
}

static int __agent_start_monitoring(struct ast_channel *ast, struct agent_pvt *p, int needlock)
{
	char tmp[AST_MAX_BUF],tmp2[AST_MAX_BUF], *pointer;
	char filename[AST_MAX_BUF];
	int res = -1;
	if (!p)
		return -1;
	if (!ast_channel_monitor(ast)) {
		snprintf(filename, sizeof(filename), "agent-%s-%s",p->agent, ast_channel_uniqueid(ast));
		/* substitute . for - */
		if ((pointer = strchr(filename, '.')))
			*pointer = '-';
		snprintf(tmp, sizeof(tmp), "%s%s", savecallsin, filename);
		ast_monitor_start(ast, recordformat, tmp, needlock, X_REC_IN | X_REC_OUT);
		ast_monitor_setjoinfiles(ast, 1);
		snprintf(tmp2, sizeof(tmp2), "%s%s.%s", urlprefix, filename, recordformatext);
#if 0
		ast_verbose("name is %s, link is %s\n",tmp, tmp2);
#endif
		if (!ast_channel_cdr(ast))
			ast_channel_cdr_set(ast, ast_cdr_alloc());
		ast_cdr_setuserfield(ast, tmp2);
		res = 0;
	} else
		ast_log(LOG_ERROR, "Recording already started on that call.\n");
	return res;
}

static int agent_start_monitoring(struct ast_channel *ast, int needlock)
{
	return __agent_start_monitoring(ast, ast_channel_tech_pvt(ast), needlock);
}

static struct ast_frame *agent_read(struct ast_channel *ast)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_frame *f = NULL;
	static struct ast_frame answer_frame = { AST_FRAME_CONTROL, { AST_CONTROL_ANSWER } };
	int cur_time = time(NULL);
	struct ast_channel *owner;

	ast_mutex_lock(&p->lock);
	owner = agent_lock_owner(p);

	CHECK_FORMATS(ast, p);
	if (!p->start) {
		p->start = cur_time;
	}
	if (p->chan) {
		ast_copy_flags(ast_channel_flags(p->chan), ast_channel_flags(ast), AST_FLAG_EXCEPTION);
		ast_channel_fdno_set(p->chan, (ast_channel_fdno(ast) == AST_AGENT_FD) ? AST_TIMING_FD : ast_channel_fdno(ast));
		f = ast_read(p->chan);
		ast_channel_fdno_set(ast, -1);
	} else
		f = &ast_null_frame;
	if (f) {
		/* if acknowledgement is not required, and the channel is up, we may have missed
			an AST_CONTROL_ANSWER (if there was one), so mark the call acknowledged anyway */
		if (!p->ackcall && !p->acknowledged && p->chan && (ast_channel_state(p->chan) == AST_STATE_UP)) {
			p->acknowledged = 1;
		}

		if (!p->acknowledged) {
			int howlong = cur_time - p->start;
			if (p->autologoff && (howlong >= p->autologoff)) {
				ast_log(LOG_NOTICE, "Agent '%s' didn't answer/confirm within %d seconds (waited %d)\n", p->name, p->autologoff, howlong);
				if (owner || p->chan) {
					if (owner) {
						ast_softhangup(owner, AST_SOFTHANGUP_EXPLICIT);
						ast_channel_unlock(owner);
						owner = ast_channel_unref(owner);
					}

					while (p->chan && ast_channel_trylock(p->chan)) {
						DEADLOCK_AVOIDANCE(&p->lock);
					}
					if (p->chan) {
						ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
						ast_channel_unlock(p->chan);
					}
				}
			}
		}
		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass.integer == AST_CONTROL_ANSWER) {
				if (p->ackcall) {
					ast_verb(3, "%s answered, waiting for '%c' to acknowledge\n", ast_channel_name(p->chan), p->acceptdtmf);
					/* Don't pass answer along */
					ast_frfree(f);
					f = &ast_null_frame;
				} else {
					p->acknowledged = 1;
					/* Use the builtin answer frame for the 
					   recording start check below. */
					ast_frfree(f);
					f = &answer_frame;
				}
			}
			break;
		case AST_FRAME_DTMF_BEGIN:
			/*ignore DTMF begin's as it can cause issues with queue announce files*/
			if((!p->acknowledged && f->subclass.integer == p->acceptdtmf) || (f->subclass.integer == p->enddtmf && endcall)){
				ast_frfree(f);
				f = &ast_null_frame;
			}
			break;
		case AST_FRAME_DTMF_END:
			if (!p->acknowledged && (f->subclass.integer == p->acceptdtmf)) {
				if (p->chan) {
					ast_verb(3, "%s acknowledged\n", ast_channel_name(p->chan));
				}
				p->acknowledged = 1;
				ast_frfree(f);
				f = &answer_frame;
			} else if (f->subclass.integer == p->enddtmf && endcall) {
				/* terminates call */
				ast_frfree(f);
				f = NULL;
			}
			break;
		case AST_FRAME_VOICE:
		case AST_FRAME_VIDEO:
			/* don't pass voice or video until the call is acknowledged */
			if (!p->acknowledged) {
				ast_frfree(f);
				f = &ast_null_frame;
			}
		default:
			/* pass everything else on through */
			break;
		}
	}

	if (owner) {
		ast_channel_unlock(owner);
		owner = ast_channel_unref(owner);
	}

	CLEANUP(ast,p);
	if (p->chan && !ast_channel_internal_bridged_channel(p->chan)) {
		if (strcasecmp(ast_channel_tech(p->chan)->type, "Local")) {
			ast_channel_internal_bridged_channel_set(p->chan, ast);
			ast_debug(1, "Bridge on '%s' being set to '%s' (3)\n", ast_channel_name(p->chan), ast_channel_name(ast_channel_internal_bridged_channel(p->chan)));
		}
	}
	ast_mutex_unlock(&p->lock);
	if (recordagentcalls && f == &answer_frame)
		agent_start_monitoring(ast,0);
	return f;
}

static int agent_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan) 
		res = ast_channel_sendhtml(p->chan, subclass, data, datalen);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_sendtext(struct ast_channel *ast, const char *text)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan) 
		res = ast_sendtext(p->chan, text);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	CHECK_FORMATS(ast, p);
	ast_mutex_lock(&p->lock);
	if (!p->chan) 
		res = 0;
	else {
		if ((f->frametype != AST_FRAME_VOICE) ||
		    (f->frametype != AST_FRAME_VIDEO) ||
		    (ast_format_cmp(&f->subclass.format, ast_channel_writeformat(p->chan)) != AST_FORMAT_CMP_NOT_EQUAL)) {
			res = ast_write(p->chan, f);
		} else {
			ast_debug(1, "Dropping one incompatible %s frame on '%s' to '%s'\n", 
				f->frametype == AST_FRAME_VOICE ? "audio" : "video",
				ast_channel_name(ast), ast_channel_name(p->chan));
			res = 0;
		}
	}
	CLEANUP(ast, p);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct agent_pvt *p = ast_channel_tech_pvt(newchan);
	ast_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	p->owner = newchan;
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int agent_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;

	ast_mutex_lock(&p->lock);
	if (p->chan && !ast_check_hangup(p->chan)) {
		ast_channel_unlock(ast);
		ast_channel_lock(p->chan);
		res = ast_channel_tech(p->chan)->indicate
			? ast_channel_tech(p->chan)->indicate(p->chan, condition, data, datalen)
			: -1;
		ast_channel_unlock(p->chan);
		ast_mutex_unlock(&p->lock);
		ast_channel_lock(ast);
	} else {
		ast_mutex_unlock(&p->lock);
		res = 0;
	}
	return res;
}

static int agent_digit_begin(struct ast_channel *ast, char digit)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	ast_mutex_lock(&p->lock);
	if (p->chan) {
		ast_senddigit_begin(p->chan, digit);
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int agent_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	ast_mutex_lock(&p->lock);
	if (p->chan) {
		ast_senddigit_end(p->chan, digit, duration);
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int agent_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	int res;
	int newstate=0;

	ast_mutex_lock(&p->lock);
	p->acknowledged = 0;

	if (p->pending) {
		ast_log(LOG_DEBUG, "Pretending to dial on pending agent\n");
		ast_mutex_unlock(&p->lock);
		ast_setstate(ast, AST_STATE_DIALING);
		return 0;
	}

	ast_assert(p->chan != NULL);
	ast_verb(3, "agent_call, call to agent '%s' call on '%s'\n", p->agent, ast_channel_name(p->chan));
	ast_debug(3, "Playing beep, lang '%s'\n", ast_channel_language(p->chan));

	ast_mutex_unlock(&p->lock);

	res = ast_streamfile(p->chan, beep, ast_channel_language(p->chan));
	ast_debug(3, "Played beep, result '%d'\n", res);
	if (!res) {
		res = ast_waitstream(p->chan, "");
		ast_debug(3, "Waited for stream, result '%d'\n", res);
	}
	
	ast_mutex_lock(&p->lock);

	if (!res) {
		struct ast_format tmpfmt;
		res = ast_set_read_format_from_cap(p->chan, ast_channel_nativeformats(p->chan));
		ast_debug(3, "Set read format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set read format to %s\n", ast_getformatname(&tmpfmt));
	}

	if (!res) {
		struct ast_format tmpfmt;
		res = ast_set_write_format_from_cap(p->chan, ast_channel_nativeformats(p->chan));
		ast_debug(3, "Set write format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set write format to %s\n", ast_getformatname(&tmpfmt));
	}
	if(!res) {
		/* Call is immediately up, or might need ack */
		if (p->ackcall) {
			newstate = AST_STATE_RINGING;
		} else {
			newstate = AST_STATE_UP;
			if (recordagentcalls)
				agent_start_monitoring(ast, 0);
			p->acknowledged = 1;
		}
	}
	CLEANUP(ast, p);
	ast_mutex_unlock(&p->lock);
	if (newstate)
		ast_setstate(ast, newstate);
	return res ? -1 : 0;
}

/*! \brief return the channel or base channel if one exists.  This function assumes the channel it is called on is already locked */
struct ast_channel* agent_get_base_channel(struct ast_channel *chan)
{
	struct agent_pvt *p;
	struct ast_channel *base = chan;

	/* chan is locked by the calling function */
	if (!chan || !ast_channel_tech_pvt(chan)) {
		ast_log(LOG_ERROR, "whoa, you need a channel (0x%ld) with a tech_pvt (0x%ld) to get a base channel.\n", (long)chan, (chan)?(long)ast_channel_tech_pvt(chan):(long)NULL);
		return NULL;
	}
	p = ast_channel_tech_pvt(chan);
	if (p->chan) 
		base = p->chan;
	return base;
}

static int agent_hangup(struct ast_channel *ast)
{
	struct agent_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_channel *indicate_chan = NULL;
	char *tmp_moh; /* moh buffer for indicating after unlocking p */

	if (p->pending) {
		AST_LIST_LOCK(&agents);
		AST_LIST_REMOVE(&agents, p, list);
		AST_LIST_UNLOCK(&agents);
	}

	ast_mutex_lock(&p->lock);
	p->owner = NULL;
	ast_channel_tech_pvt_set(ast, NULL);
	p->acknowledged = 0;

	/* if they really are hung up then set start to 0 so the test
	 * later if we're called on an already downed channel
	 * doesn't cause an agent to be logged out like when
	 * agent_request() is followed immediately by agent_hangup()
	 * as in apps/app_chanisavail.c:chanavail_exec()
	 */

	ast_debug(1, "Hangup called for state %s\n", ast_state2str(ast_channel_state(ast)));
	p->start = 0;
	if (p->chan) {
		ast_channel_internal_bridged_channel_set(p->chan, NULL);
		/* If they're dead, go ahead and hang up on the agent now */
		if (p->dead) {
			ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
		} else if (p->loginstart) {
			indicate_chan = ast_channel_ref(p->chan);
			tmp_moh = ast_strdupa(p->moh);
		}
	}
	ast_mutex_unlock(&p->lock);

	if (indicate_chan) {
		ast_indicate_data(indicate_chan, AST_CONTROL_HOLD,
			S_OR(tmp_moh, NULL),
			!ast_strlen_zero(tmp_moh) ? strlen(tmp_moh) + 1 : 0);
		indicate_chan = ast_channel_unref(indicate_chan);
	}

	ast_mutex_lock(&p->lock);
	if (p->abouttograb) {
		/* Let the "about to grab" thread know this isn't valid anymore, and let it
		   kill it later */
		p->abouttograb = 0;
	} else if (p->dead) {
		ast_mutex_unlock(&p->lock);
		agent_pvt_destroy(p);
		return 0;
	} else {
		/* Store last disconnect time */
		p->lastdisc = ast_tvadd(ast_tvnow(), ast_samp2tv(p->wrapuptime, 1000));
	}

	/* Release ownership of the agent to other threads (presumably running the login app). */
	p->app_sleep_cond = 1;
	p->app_lock_flag = 0;
	ast_cond_signal(&p->app_complete_cond);

	ast_mutex_unlock(&p->lock);
	return 0;
}

static int agent_cont_sleep(void *data)
{
	struct agent_pvt *p;
	int res;

	p = (struct agent_pvt *) data;

	ast_mutex_lock(&p->lock);
	res = p->app_sleep_cond;
	if (res && p->lastdisc.tv_sec) {
		if (ast_tvdiff_ms(ast_tvnow(), p->lastdisc) > 0) {
			res = 0;
		}
	}
	ast_mutex_unlock(&p->lock);

	if (!res) {
		ast_debug(5, "agent_cont_sleep() returning %d\n", res);
	}

	return res;
}

static int agent_ack_sleep(struct agent_pvt *p)
{
	int digit;
	int to = 1000;
	struct ast_frame *f;
	struct timeval start = ast_tvnow();
	int ms;

	/* Wait a second and look for something */
	while ((ms = ast_remaining_ms(start, to))) {
		ms = ast_waitfor(p->chan, ms);
		if (ms < 0) {
			return -1;
		}
		if (ms == 0) {
			return 0;
		}
		f = ast_read(p->chan);
		if (!f) {
			return -1;
		}
		if (f->frametype == AST_FRAME_DTMF) {
			digit = f->subclass.integer;
		} else {
			digit = 0;
		}
		ast_frfree(f);
		ast_mutex_lock(&p->lock);
		if (!p->app_sleep_cond) {
			ast_mutex_unlock(&p->lock);
			return 0;
		}
		if (digit == p->acceptdtmf) {
			ast_mutex_unlock(&p->lock);
			return 1;
		}
		if (p->lastdisc.tv_sec) {
			if (ast_tvdiff_ms(ast_tvnow(), p->lastdisc) > 0) {
				ast_mutex_unlock(&p->lock);
				return 0;
			}
		}
		ast_mutex_unlock(&p->lock);
	}
	return 0;
}

static struct ast_channel *agent_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge)
{
	struct agent_pvt *p = ast_channel_tech_pvt(bridge);
	struct ast_channel *ret = NULL;

	if (p) {
		if (chan == p->chan)
			ret = ast_channel_internal_bridged_channel(bridge);
		else if (chan == ast_channel_internal_bridged_channel(bridge))
			ret = p->chan;
	}

	ast_debug(1, "Asked for bridged channel on '%s'/'%s', returning '%s'\n", ast_channel_name(chan), ast_channel_name(bridge), ret ? ast_channel_name(ret) : "<none>");
	return ret;
}

/*! \brief Create new agent channel */
static struct ast_channel *agent_new(struct agent_pvt *p, int state, const char *linkedid, struct ast_callid *callid)
{
	struct ast_channel *tmp;
#if 0
	if (!p->chan) {
		ast_log(LOG_WARNING, "No channel? :(\n");
		return NULL;
	}
#endif	
	if (p->pending)
		tmp = ast_channel_alloc(0, state, 0, 0, "", p->chan ? ast_channel_exten(p->chan):"", p->chan ? ast_channel_context(p->chan):"", linkedid, 0, "Agent/P%s-%d", p->agent, (int) ast_random() & 0xffff);
	else
		tmp = ast_channel_alloc(0, state, 0, 0, "", p->chan ? ast_channel_exten(p->chan):"", p->chan ? ast_channel_context(p->chan):"", linkedid, 0, "Agent/%s", p->agent);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate agent channel structure\n");
		return NULL;
	}

	if (callid) {
		ast_channel_callid_set(tmp, callid);
	}

	ast_channel_tech_set(tmp, &agent_tech);
	if (p->chan) {
		ast_format_cap_copy(ast_channel_nativeformats(tmp), ast_channel_nativeformats(p->chan));
		ast_format_copy(ast_channel_writeformat(tmp), ast_channel_writeformat(p->chan));
		ast_format_copy(ast_channel_rawwriteformat(tmp), ast_channel_writeformat(p->chan));
		ast_format_copy(ast_channel_readformat(tmp), ast_channel_readformat(p->chan));
		ast_format_copy(ast_channel_rawreadformat(tmp), ast_channel_readformat(p->chan));
		ast_channel_language_set(tmp, ast_channel_language(p->chan));
		ast_channel_context_set(tmp, ast_channel_context(p->chan));
		ast_channel_exten_set(tmp, ast_channel_exten(p->chan));
		/* XXX Is this really all we copy form the originating channel?? */
	} else {
		ast_format_set(ast_channel_writeformat(tmp), AST_FORMAT_SLINEAR, 0);
		ast_format_set(ast_channel_rawwriteformat(tmp), AST_FORMAT_SLINEAR, 0);
		ast_format_set(ast_channel_readformat(tmp), AST_FORMAT_SLINEAR, 0);
		ast_format_set(ast_channel_rawreadformat(tmp), AST_FORMAT_SLINEAR, 0);
		ast_format_cap_add(ast_channel_nativeformats(tmp), ast_channel_writeformat(tmp));
	}
	/* Safe, agentlock already held */
	ast_channel_tech_pvt_set(tmp, p);
	p->owner = tmp;
	ast_channel_priority_set(tmp, 1);
	return tmp;
}


/*!
 * Read configuration data. The file named agents.conf.
 *
 * \returns Always 0, or so it seems.
 */
static int read_agent_config(int reload)
{
	struct ast_config *cfg;
	struct ast_config *ucfg;
	struct ast_variable *v;
	struct agent_pvt *p;
	const char *catname;
	const char *hasagent;
	int genhasagent;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	group = 0;
	autologoff = 0;
	wrapuptime = 0;
	ackcall = 0;
	endcall = 1;
	cfg = ast_config_load(config, config_flags);
	if (!cfg) {
		ast_log(LOG_NOTICE, "No agent configuration found -- agent support disabled\n");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "%s contains a parsing error.  Aborting\n", config);
		return 0;
	}
	if ((ucfg = ast_config_load("users.conf", config_flags))) {
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED) {
			ucfg = NULL;
		} else if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "users.conf contains a parsing error.  Aborting\n");
			return 0;
		}
	}

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		p->dead = 1;
	}
	strcpy(moh, "default");
	/* set the default recording values */
	recordagentcalls = 0;
	strcpy(recordformat, "wav");
	strcpy(recordformatext, "wav");
	urlprefix[0] = '\0';
	savecallsin[0] = '\0';

	/* Read in the [agents] section */
	v = ast_variable_browse(cfg, "agents");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "agent")) {
			add_agent(v->value, 0);
		} else if (!strcasecmp(v->name, "group")) {
			group = ast_get_group(v->value);
		} else if (!strcasecmp(v->name, "autologoff")) {
			autologoff = atoi(v->value);
			if (autologoff < 0)
				autologoff = 0;
		} else if (!strcasecmp(v->name, "ackcall")) {
			if (ast_true(v->value) || !strcasecmp(v->value, "always")) {
				ackcall = 1;
			}
		} else if (!strcasecmp(v->name, "endcall")) {
			endcall = ast_true(v->value);
		} else if (!strcasecmp(v->name, "acceptdtmf")) {
			acceptdtmf = *(v->value);
			ast_log(LOG_NOTICE, "Set acceptdtmf to %c\n", acceptdtmf);
		} else if (!strcasecmp(v->name, "enddtmf")) {
			enddtmf = *(v->value);
		} else if (!strcasecmp(v->name, "wrapuptime")) {
			wrapuptime = atoi(v->value);
			if (wrapuptime < 0)
				wrapuptime = 0;
		} else if (!strcasecmp(v->name, "maxlogintries") && !ast_strlen_zero(v->value)) {
			maxlogintries = atoi(v->value);
			if (maxlogintries < 0)
				maxlogintries = 0;
		} else if (!strcasecmp(v->name, "goodbye") && !ast_strlen_zero(v->value)) {
			strcpy(agentgoodbye,v->value);
		} else if (!strcasecmp(v->name, "musiconhold")) {
			ast_copy_string(moh, v->value, sizeof(moh));
		} else if (!strcasecmp(v->name, "updatecdr")) {
			if (ast_true(v->value))
				updatecdr = 1;
			else
				updatecdr = 0;
		} else if (!strcasecmp(v->name, "autologoffunavail")) {
			if (ast_true(v->value))
				autologoffunavail = 1;
			else
				autologoffunavail = 0;
		} else if (!strcasecmp(v->name, "recordagentcalls")) {
			recordagentcalls = ast_true(v->value);
		} else if (!strcasecmp(v->name, "recordformat")) {
			ast_copy_string(recordformat, v->value, sizeof(recordformat));
			if (!strcasecmp(v->value, "wav49"))
				strcpy(recordformatext, "WAV");
			else
				ast_copy_string(recordformatext, v->value, sizeof(recordformatext));
		} else if (!strcasecmp(v->name, "urlprefix")) {
			ast_copy_string(urlprefix, v->value, sizeof(urlprefix));
			if (urlprefix[strlen(urlprefix) - 1] != '/')
				strncat(urlprefix, "/", sizeof(urlprefix) - strlen(urlprefix) - 1);
		} else if (!strcasecmp(v->name, "savecallsin")) {
			if (v->value[0] == '/')
				ast_copy_string(savecallsin, v->value, sizeof(savecallsin));
			else
				snprintf(savecallsin, sizeof(savecallsin) - 2, "/%s", v->value);
			if (savecallsin[strlen(savecallsin) - 1] != '/')
				strncat(savecallsin, "/", sizeof(savecallsin) - strlen(savecallsin) - 1);
		} else if (!strcasecmp(v->name, "custom_beep")) {
			ast_copy_string(beep, v->value, sizeof(beep));
		}
		v = v->next;
	}
	if (ucfg) {
		genhasagent = ast_true(ast_variable_retrieve(ucfg, "general", "hasagent"));
		catname = ast_category_browse(ucfg, NULL);
		while(catname) {
			if (strcasecmp(catname, "general")) {
				hasagent = ast_variable_retrieve(ucfg, catname, "hasagent");
				if (ast_true(hasagent) || (!hasagent && genhasagent)) {
					char tmp[256];
					const char *fullname = ast_variable_retrieve(ucfg, catname, "fullname");
					const char *secret = ast_variable_retrieve(ucfg, catname, "secret");
					if (!fullname)
						fullname = "";
					if (!secret)
						secret = "";
					snprintf(tmp, sizeof(tmp), "%s,%s,%s", catname, secret,fullname);
					add_agent(tmp, 0);
				}
			}
			catname = ast_category_browse(ucfg, catname);
		}
		ast_config_destroy(ucfg);
	}
	AST_LIST_TRAVERSE_SAFE_BEGIN(&agents, p, list) {
		if (p->dead) {
			AST_LIST_REMOVE_CURRENT(list);
			/* Destroy if  appropriate */
			if (!p->owner) {
				if (!p->chan) {
					agent_pvt_destroy(p);
				} else {
					/* Cause them to hang up */
					ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
				}
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&agents);
	ast_config_destroy(cfg);
	return 1;
}

static int check_availability(struct agent_pvt *newlyavailable, int needlock)
{
	struct ast_channel *chan=NULL, *parent=NULL;
	struct agent_pvt *p;
	int res;

	ast_debug(1, "Checking availability of '%s'\n", newlyavailable->agent);
	if (needlock)
		AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (p == newlyavailable) {
			continue;
		}
		ast_mutex_lock(&p->lock);
		if (!p->abouttograb && p->pending && ((p->group && (newlyavailable->group & p->group)) || !strcmp(p->agent, newlyavailable->agent))) {
			ast_debug(1, "Call '%s' looks like a winner for agent '%s'\n", ast_channel_name(p->owner), newlyavailable->agent);
			/* We found a pending call, time to merge */
			chan = agent_new(newlyavailable, AST_STATE_DOWN, p->owner ? ast_channel_linkedid(p->owner) : NULL, NULL);
			parent = p->owner;
			p->abouttograb = 1;
			ast_mutex_unlock(&p->lock);
			break;
		}
		ast_mutex_unlock(&p->lock);
	}
	if (needlock)
		AST_LIST_UNLOCK(&agents);
	if (parent && chan)  {
		if (newlyavailable->ackcall) {
			/* Don't do beep here */
			res = 0;
		} else {
			ast_debug(3, "Playing beep, lang '%s'\n", ast_channel_language(newlyavailable->chan));
			res = ast_streamfile(newlyavailable->chan, beep, ast_channel_language(newlyavailable->chan));
			ast_debug(3, "Played beep, result '%d'\n", res);
			if (!res) {
				res = ast_waitstream(newlyavailable->chan, "");
				ast_debug(1, "Waited for stream, result '%d'\n", res);
			}
		}
		if (!res) {
			/* Note -- parent may have disappeared */
			if (p->abouttograb) {
				newlyavailable->acknowledged = 1;
				/* Safe -- agent lock already held */
				ast_setstate(parent, AST_STATE_UP);
				ast_setstate(chan, AST_STATE_UP);
				ast_channel_context_set(parent, ast_channel_context(chan));
				ast_channel_masquerade(parent, chan);
				ast_hangup(chan);
				p->abouttograb = 0;
			} else {
				ast_debug(1, "Sneaky, parent disappeared in the mean time...\n");
				agent_cleanup(newlyavailable);
			}
		} else {
			ast_debug(1, "Ugh...  Agent hung up at exactly the wrong time\n");
			agent_cleanup(newlyavailable);
		}
	}
	return 0;
}

static int check_beep(struct agent_pvt *newlyavailable, int needlock)
{
	struct agent_pvt *p;
	int res=0;

	ast_debug(1, "Checking beep availability of '%s'\n", newlyavailable->agent);
	if (needlock)
		AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (p == newlyavailable) {
			continue;
		}
		ast_mutex_lock(&p->lock);
		if (!p->abouttograb && p->pending && ((p->group && (newlyavailable->group & p->group)) || !strcmp(p->agent, newlyavailable->agent))) {
			ast_debug(1, "Call '%s' looks like a would-be winner for agent '%s'\n", ast_channel_name(p->owner), newlyavailable->agent);
			ast_mutex_unlock(&p->lock);
			break;
		}
		ast_mutex_unlock(&p->lock);
	}
	if (needlock)
		AST_LIST_UNLOCK(&agents);
	if (p) {
		ast_mutex_unlock(&newlyavailable->lock);
		ast_debug(3, "Playing beep, lang '%s'\n", ast_channel_language(newlyavailable->chan));
		res = ast_streamfile(newlyavailable->chan, beep, ast_channel_language(newlyavailable->chan));
		ast_debug(1, "Played beep, result '%d'\n", res);
		if (!res) {
			res = ast_waitstream(newlyavailable->chan, "");
			ast_debug(1, "Waited for stream, result '%d'\n", res);
		}
		ast_mutex_lock(&newlyavailable->lock);
	}
	return res;
}

/*! \brief Part of the Asterisk PBX interface */
static struct ast_channel *agent_request(const char *type, struct ast_format_cap *cap, const struct ast_channel* requestor, const char *data, int *cause)
{
	struct agent_pvt *p;
	struct ast_channel *chan = NULL;
	const char *s;
	ast_group_t groupmatch;
	int groupoff;
	int waitforagent=0;
	int hasagent = 0;
	struct timeval now;
	struct ast_callid *callid = ast_read_threadstorage_callid();

	s = data;
	if ((s[0] == '@') && (sscanf(s + 1, "%30d", &groupoff) == 1)) {
		groupmatch = (1 << groupoff);
	} else if ((s[0] == ':') && (sscanf(s + 1, "%30d", &groupoff) == 1)) {
		groupmatch = (1 << groupoff);
		waitforagent = 1;
	} else 
		groupmatch = 0;

	/* Check actual logged in agents first */
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		ast_mutex_lock(&p->lock);
		if (!p->pending && ((groupmatch && (p->group & groupmatch)) || !strcmp(data, p->agent))) {
			if (p->chan) {
				hasagent++;
			}
			now = ast_tvnow();
			if (p->loginstart
				&& (!p->lastdisc.tv_sec || ast_tvdiff_ms(now, p->lastdisc) > 0)) {
				p->lastdisc = ast_tv(0, 0);
				/* Agent must be registered, but not have any active call, and not be in a waiting state */
				if (!p->owner && p->chan) {
					/* Fixed agent */
					chan = agent_new(p, AST_STATE_DOWN, requestor ? ast_channel_linkedid(requestor) : NULL, callid);
				}
				if (chan) {
					ast_mutex_unlock(&p->lock);
					break;
				}
			}
		}
		ast_mutex_unlock(&p->lock);
	}

	if (!chan && waitforagent) {
		/* No agent available -- but we're requesting to wait for one.
		   Allocate a place holder */
		if (hasagent) {
			ast_debug(1, "Creating place holder for '%s'\n", s);
			p = add_agent(data, 1);
			if (p) {
				p->group = groupmatch;
				chan = agent_new(p, AST_STATE_DOWN, requestor ? ast_channel_linkedid(requestor) : NULL, callid);
				if (!chan) {
					AST_LIST_REMOVE(&agents, p, list);
					agent_pvt_destroy(p);
				}
			}
		} else {
			ast_debug(1, "Not creating place holder for '%s' since nobody logged in\n", s);
		}
	}
	*cause = hasagent ? AST_CAUSE_BUSY : AST_CAUSE_UNREGISTERED;
	AST_LIST_UNLOCK(&agents);

	if (callid) {
		callid = ast_callid_unref(callid);
	}

	if (chan) {
		ast_mutex_lock(&p->lock);
		if (p->pending) {
			ast_mutex_unlock(&p->lock);
			return chan;
		}

		if (!p->chan) {
			ast_debug(1, "Agent disconnected before we could connect the call\n");
			ast_mutex_unlock(&p->lock);
			ast_hangup(chan);
			*cause = AST_CAUSE_UNREGISTERED;
			return NULL;
		}

		/* we need to take control of the channel from the login app
		 * thread */
		p->app_sleep_cond = 0;
		p->app_lock_flag = 1;
		ast_queue_frame(p->chan, &ast_null_frame);
		ast_cond_wait(&p->login_wait_cond, &p->lock);

		if (!p->chan) {
			ast_debug(1, "Agent disconnected while we were connecting the call\n");
			ast_mutex_unlock(&p->lock);
			ast_hangup(chan);
			*cause = AST_CAUSE_UNREGISTERED;
			return NULL;
		}

		ast_indicate(p->chan, AST_CONTROL_UNHOLD);
		ast_mutex_unlock(&p->lock);
	}

	return chan;
}

static force_inline int powerof(unsigned int d)
{
	int x = ffs(d);

	if (x)
		return x - 1;

	return 0;
}

/*!
 * Lists agents and their status to the Manager API.
 * It is registered on load_module() and it gets called by the manager backend.
 * This function locks both the pvt and the channel that owns it for a while, but
 * does not keep these locks.
 * \param s
 * \param m
 * \returns 
 * \sa action_agent_logoff(), load_module().
 */
static int action_agents(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	struct agent_pvt *p;
	char *username = NULL;
	char *loginChan = NULL;
	char *talkingto = NULL;
	char *talkingtoChan = NULL;
	char *status = NULL;
	struct ast_channel *bridge;

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);
	astman_send_ack(s, m, "Agents will follow");
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		struct ast_channel *owner;
		ast_mutex_lock(&p->lock);
		owner = agent_lock_owner(p);

		/* Status Values:
		   AGENT_LOGGEDOFF - Agent isn't logged in
		   AGENT_IDLE      - Agent is logged in, and waiting for call
		   AGENT_ONCALL    - Agent is logged in, and on a call
		   AGENT_UNKNOWN   - Don't know anything about agent. Shouldn't ever get this. */

		username = S_OR(p->name, "None");

		/* Set a default status. It 'should' get changed. */
		status = "AGENT_UNKNOWN";

		if (p->chan) {
			loginChan = ast_strdupa(ast_channel_name(p->chan));
			if (owner && ast_channel_internal_bridged_channel(owner)) {
				talkingto = S_COR(ast_channel_caller(p->chan)->id.number.valid,
					ast_channel_caller(p->chan)->id.number.str, "n/a");
				if ((bridge = ast_bridged_channel(owner))) {
					talkingtoChan = ast_strdupa(ast_channel_name(bridge));
				} else {
					talkingtoChan = "n/a";
				}
				status = "AGENT_ONCALL";
			} else {
				talkingto = "n/a";
				talkingtoChan = "n/a";
				status = "AGENT_IDLE";
			}
		} else {
			loginChan = "n/a";
			talkingto = "n/a";
			talkingtoChan = "n/a";
			status = "AGENT_LOGGEDOFF";
		}

		if (owner) {
			ast_channel_unlock(owner);
			owner = ast_channel_unref(owner);
		}

		astman_append(s, "Event: Agents\r\n"
			"Agent: %s\r\n"
			"Name: %s\r\n"
			"Status: %s\r\n"
			"LoggedInChan: %s\r\n"
			"LoggedInTime: %d\r\n"
			"TalkingTo: %s\r\n"
			"TalkingToChan: %s\r\n"
			"%s"
			"\r\n",
			p->agent, username, status, loginChan, (int)p->loginstart, talkingto, talkingtoChan, idText);
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	astman_append(s, "Event: AgentsComplete\r\n"
		"%s"
		"\r\n",idText);
	return 0;
}

static int agent_logoff(const char *agent, int soft)
{
	struct agent_pvt *p;
	int ret = -1; /* Return -1 if no agent if found */

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (!strcasecmp(p->agent, agent)) {
			ret = 0;
			if (p->owner || p->chan) {
				if (!soft) {
					struct ast_channel *owner;
					ast_mutex_lock(&p->lock);
					owner = agent_lock_owner(p);

					if (owner) {
						ast_softhangup(owner, AST_SOFTHANGUP_EXPLICIT);
						ast_channel_unlock(owner);
						owner = ast_channel_unref(owner);
					}

					while (p->chan && ast_channel_trylock(p->chan)) {
						DEADLOCK_AVOIDANCE(&p->lock);
					}
					if (p->chan) {
						ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
						ast_channel_unlock(p->chan);
					}

					ast_mutex_unlock(&p->lock);
				} else
					p->deferlogoff = 1;
			}
			break;
		}
	}
	AST_LIST_UNLOCK(&agents);

	return ret;
}

static char *agent_logoff_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int ret;
	const char *agent;

	switch (cmd) {
	case CLI_INIT:
		e->command = "agent logoff";
		e->usage =
			"Usage: agent logoff <channel> [soft]\n"
			"       Sets an agent as no longer logged in.\n"
			"       If 'soft' is specified, do not hangup existing calls.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_agent_logoff_cmd(a->line, a->word, a->pos, a->n); 
	}

	if (a->argc < 3 || a->argc > 4)
		return CLI_SHOWUSAGE;
	if (a->argc == 4 && strcasecmp(a->argv[3], "soft"))
		return CLI_SHOWUSAGE;

	agent = a->argv[2] + 6;
	ret = agent_logoff(agent, a->argc == 4);
	if (ret == 0)
		ast_cli(a->fd, "Logging out %s\n", agent);

	return CLI_SUCCESS;
}

/*!
 * Sets an agent as no longer logged in in the Manager API.
 * It is registered on load_module() and it gets called by the manager backend.
 * \param s
 * \param m
 * \returns 
 * \sa action_agents(), load_module().
 */
static int action_agent_logoff(struct mansession *s, const struct message *m)
{
	const char *agent = astman_get_header(m, "Agent");
	const char *soft_s = astman_get_header(m, "Soft"); /* "true" is don't hangup */
	int soft;
	int ret; /* return value of agent_logoff */

	if (ast_strlen_zero(agent)) {
		astman_send_error(s, m, "No agent specified");
		return 0;
	}

	soft = ast_true(soft_s) ? 1 : 0;
	ret = agent_logoff(agent, soft);
	if (ret == 0)
		astman_send_ack(s, m, "Agent logged out");
	else
		astman_send_error(s, m, "No such agent");

	return 0;
}

static char *complete_agent_logoff_cmd(const char *line, const char *word, int pos, int state)
{
	char *ret = NULL;

	if (pos == 2) {
		struct agent_pvt *p;
		char name[AST_MAX_AGENT];
		int which = 0, len = strlen(word);

		AST_LIST_LOCK(&agents);
		AST_LIST_TRAVERSE(&agents, p, list) {
			snprintf(name, sizeof(name), "Agent/%s", p->agent);
			if (!strncasecmp(word, name, len) && p->loginstart && ++which > state) {
				ret = ast_strdup(name);
				break;
			}
		}
		AST_LIST_UNLOCK(&agents);
	} else if (pos == 3 && state == 0) 
		return ast_strdup("soft");
	
	return ret;
}

/*!
 * Show agents in cli.
 */
static char *agents_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct agent_pvt *p;
	char username[AST_MAX_BUF];
	char location[AST_MAX_BUF] = "";
	char talkingto[AST_MAX_BUF] = "";
	char music[AST_MAX_BUF];
	int count_agents = 0;		/*!< Number of agents configured */
	int online_agents = 0;		/*!< Number of online agents */
	int offline_agents = 0;		/*!< Number of offline agents */

	switch (cmd) {
	case CLI_INIT:
		e->command = "agent show";
		e->usage =
			"Usage: agent show\n"
			"       Provides summary information on agents.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		struct ast_channel *owner;
		ast_mutex_lock(&p->lock);
		owner = agent_lock_owner(p);
		if (p->pending) {
			if (p->group)
				ast_cli(a->fd, "-- Pending call to group %d\n", powerof(p->group));
			else
				ast_cli(a->fd, "-- Pending call to agent %s\n", p->agent);
		} else {
			if (!ast_strlen_zero(p->name))
				snprintf(username, sizeof(username), "(%s) ", p->name);
			else
				username[0] = '\0';
			if (p->chan) {
				snprintf(location, sizeof(location), "logged in on %s", ast_channel_name(p->chan));
				if (owner && ast_bridged_channel(owner)) {
					snprintf(talkingto, sizeof(talkingto), " talking to %s", ast_channel_name(ast_bridged_channel(p->owner)));
				} else {
					strcpy(talkingto, " is idle");
				}
				online_agents++;
			} else {
				strcpy(location, "not logged in");
				talkingto[0] = '\0';
				offline_agents++;
			}
			if (!ast_strlen_zero(p->moh))
				snprintf(music, sizeof(music), " (musiconhold is '%s')", p->moh);
			ast_cli(a->fd, "%-12.12s %s%s%s%s\n", p->agent, 
				username, location, talkingto, music);
			count_agents++;
		}

		if (owner) {
			ast_channel_unlock(owner);
			owner = ast_channel_unref(owner);
		}
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	if ( !count_agents ) 
		ast_cli(a->fd, "No Agents are configured in %s\n",config);
	else 
		ast_cli(a->fd, "%d agents configured [%d online , %d offline]\n",count_agents, online_agents, offline_agents);
	ast_cli(a->fd, "\n");
	                
	return CLI_SUCCESS;
}


static char *agents_show_online(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct agent_pvt *p;
	char username[AST_MAX_BUF];
	char location[AST_MAX_BUF] = "";
	char talkingto[AST_MAX_BUF] = "";
	char music[AST_MAX_BUF];
	int count_agents = 0;           /* Number of agents configured */
	int online_agents = 0;          /* Number of online agents */
	int agent_status = 0;           /* 0 means offline, 1 means online */

	switch (cmd) {
	case CLI_INIT:
		e->command = "agent show online";
		e->usage =
			"Usage: agent show online\n"
			"       Provides a list of all online agents.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		struct ast_channel *owner;

		agent_status = 0;       /* reset it to offline */
		ast_mutex_lock(&p->lock);
		owner = agent_lock_owner(p);

		if (!ast_strlen_zero(p->name))
			snprintf(username, sizeof(username), "(%s) ", p->name);
		else
			username[0] = '\0';
		if (p->chan) {
			snprintf(location, sizeof(location), "logged in on %s", ast_channel_name(p->chan));
			if (p->owner && ast_bridged_channel(p->owner)) {
				snprintf(talkingto, sizeof(talkingto), " talking to %s", ast_channel_name(ast_bridged_channel(p->owner)));
			} else {
				strcpy(talkingto, " is idle");
			}
			agent_status = 1;
			online_agents++;
		}

		if (owner) {
			ast_channel_unlock(owner);
			owner = ast_channel_unref(owner);
		}

		if (!ast_strlen_zero(p->moh))
			snprintf(music, sizeof(music), " (musiconhold is '%s')", p->moh);
		if (agent_status)
			ast_cli(a->fd, "%-12.12s %s%s%s%s\n", p->agent, username, location, talkingto, music);
		count_agents++;
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	if (!count_agents) 
		ast_cli(a->fd, "No Agents are configured in %s\n", config);
	else
		ast_cli(a->fd, "%d agents online\n", online_agents);
	ast_cli(a->fd, "\n");
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_agents[] = {
	AST_CLI_DEFINE(agents_show, "Show status of agents"),
	AST_CLI_DEFINE(agents_show_online, "Show all online agents"),
	AST_CLI_DEFINE(agent_logoff_cmd, "Sets an agent offline"),
};

/*!
 * Called by the AgentLogin application (from the dial plan).
 * 
 * \brief Log in agent application.
 *
 * \param chan
 * \param data
 * \returns
 * \sa agentmonitoroutgoing_exec(), load_module().
 */
static int login_exec(struct ast_channel *chan, const char *data)
{
	int res=0;
	int tries = 0;
	int max_login_tries = maxlogintries;
	struct agent_pvt *p;
	char user[AST_MAX_AGENT];
	char pass[AST_MAX_AGENT];
	char xpass[AST_MAX_AGENT];
	char *errmsg;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(agent_id);
			     AST_APP_ARG(options);
			     AST_APP_ARG(extension);
		);
	const char *tmpoptions = NULL;
	int play_announcement = 1;
	char agent_goodbye[AST_MAX_FILENAME_LEN];
	int update_cdr = updatecdr;

	user[0] = '\0';
	xpass[0] = '\0';

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	ast_copy_string(agent_goodbye, agentgoodbye, sizeof(agent_goodbye));

	ast_channel_lock(chan);
	/* Set Channel Specific Login Overrides */
	if (!ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTLMAXLOGINTRIES"))) {
		max_login_tries = atoi(pbx_builtin_getvar_helper(chan, "AGENTMAXLOGINTRIES"));
		if (max_login_tries < 0)
			max_login_tries = 0;
		tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTMAXLOGINTRIES");
		ast_verb(3, "Saw variable AGENTMAXLOGINTRIES=%s, setting max_login_tries to: %d on Channel '%s'.\n",tmpoptions,max_login_tries,ast_channel_name(chan));
	}
	if (!ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR"))) {
		if (ast_true(pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR")))
			update_cdr = 1;
		else
			update_cdr = 0;
		tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR");
		ast_verb(3, "Saw variable AGENTUPDATECDR=%s, setting update_cdr to: %d on Channel '%s'.\n",tmpoptions,update_cdr,ast_channel_name(chan));
	}
	if (!ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTGOODBYE"))) {
		strcpy(agent_goodbye, pbx_builtin_getvar_helper(chan, "AGENTGOODBYE"));
		tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTGOODBYE");
		ast_verb(3, "Saw variable AGENTGOODBYE=%s, setting agent_goodbye to: %s on Channel '%s'.\n",tmpoptions,agent_goodbye,ast_channel_name(chan));
	}
	ast_channel_unlock(chan);
	/* End Channel Specific Login Overrides */
	
	if (!ast_strlen_zero(args.options)) {
		if (strchr(args.options, 's')) {
			play_announcement = 0;
		}
	}

	if (ast_channel_state(chan) != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res) {
		if (!ast_strlen_zero(args.agent_id))
			ast_copy_string(user, args.agent_id, AST_MAX_AGENT);
		else
			res = ast_app_getdata(chan, "agent-user", user, sizeof(user) - 1, 0);
	}
	while (!res && (max_login_tries==0 || tries < max_login_tries)) {
		tries++;
		/* Check for password */
		AST_LIST_LOCK(&agents);
		AST_LIST_TRAVERSE(&agents, p, list) {
			if (!strcmp(p->agent, user) && !p->pending)
				ast_copy_string(xpass, p->password, sizeof(xpass));
		}
		AST_LIST_UNLOCK(&agents);
		if (!res) {
			if (!ast_strlen_zero(xpass))
				res = ast_app_getdata(chan, "agent-pass", pass, sizeof(pass) - 1, 0);
			else
				pass[0] = '\0';
		}
		errmsg = "agent-incorrect";

#if 0
		ast_log(LOG_NOTICE, "user: %s, pass: %s\n", user, pass);
#endif		

		/* Check again for accuracy */
		AST_LIST_LOCK(&agents);
		AST_LIST_TRAVERSE(&agents, p, list) {
			int unlock_channel = 1;

			ast_channel_lock(chan);
			ast_mutex_lock(&p->lock);
			if (!strcmp(p->agent, user) &&
			    !strcmp(p->password, pass) && !p->pending) {

				/* Set Channel Specific Agent Overrides */
				if (!ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTACKCALL"))) {
					if (ast_true(pbx_builtin_getvar_helper(chan, "AGENTACKCALL"))) {
						p->ackcall = 1;
					} else {
						p->ackcall = 0;
					}
					tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTACKCALL");
					ast_verb(3, "Saw variable AGENTACKCALL=%s, setting ackcall to: %d for Agent '%s'.\n", tmpoptions, p->ackcall, p->agent);
					ast_set_flag(p, AGENT_FLAG_ACKCALL);
				} else {
					p->ackcall = ackcall;
				}
				if (!ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF"))) {
					p->autologoff = atoi(pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF"));
					if (p->autologoff < 0)
						p->autologoff = 0;
					tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF");
					ast_verb(3, "Saw variable AGENTAUTOLOGOFF=%s, setting autologff to: %d for Agent '%s'.\n", tmpoptions, p->autologoff, p->agent);
					ast_set_flag(p, AGENT_FLAG_AUTOLOGOFF);
				} else {
					p->autologoff = autologoff;
				}
				if (!ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME"))) {
					p->wrapuptime = atoi(pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME"));
					if (p->wrapuptime < 0)
						p->wrapuptime = 0;
					tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME");
					ast_verb(3, "Saw variable AGENTWRAPUPTIME=%s, setting wrapuptime to: %d for Agent '%s'.\n", tmpoptions, p->wrapuptime, p->agent);
					ast_set_flag(p, AGENT_FLAG_WRAPUPTIME);
				} else {
					p->wrapuptime = wrapuptime;
				}
				tmpoptions = pbx_builtin_getvar_helper(chan, "AGENTACCEPTDTMF");
				if (!ast_strlen_zero(tmpoptions)) {
					p->acceptdtmf = *tmpoptions;
					ast_verb(3, "Saw variable AGENTACCEPTDTMF=%s, setting acceptdtmf to: %c for Agent '%s'.\n", tmpoptions, p->acceptdtmf, p->agent);
					ast_set_flag(p, AGENT_FLAG_ACCEPTDTMF);
				}
				tmpoptions = pbx_builtin_getvar_helper(chan, "AGENTENDDTMF");
				if (!ast_strlen_zero(tmpoptions)) {
					p->enddtmf = *tmpoptions;
					ast_verb(3, "Saw variable AGENTENDDTMF=%s, setting enddtmf to: %c for Agent '%s'.\n", tmpoptions, p->enddtmf, p->agent);
					ast_set_flag(p, AGENT_FLAG_ENDDTMF);
				}
				ast_channel_unlock(chan);
				unlock_channel = 0;
				/* End Channel Specific Agent Overrides */

				if (!p->chan) {
					/* Ensure nobody else can be this agent until we're done. */
					p->chan = chan;

					p->acknowledged = 0;

					if (!res) {
						struct ast_format tmpfmt;
						res = ast_set_read_format_from_cap(chan, ast_channel_nativeformats(chan));
						if (res) {
							ast_log(LOG_WARNING, "Unable to set read format to %s\n", ast_getformatname(&tmpfmt));
						}
					}
					if (!res) {
						struct ast_format tmpfmt;
						res = ast_set_write_format_from_cap(chan, ast_channel_nativeformats(chan));
						if (res) {
							ast_log(LOG_WARNING, "Unable to set write format to %s\n", ast_getformatname(&tmpfmt));
						}
					}
					if (!res && play_announcement == 1) {
						ast_mutex_unlock(&p->lock);
						AST_LIST_UNLOCK(&agents);
						res = ast_streamfile(chan, "agent-loginok", ast_channel_language(chan));
						if (!res) {
							ast_waitstream(chan, "");
						}
						AST_LIST_LOCK(&agents);
						ast_mutex_lock(&p->lock);
					}

					if (!res) {
						long logintime;
						char agent[AST_MAX_AGENT];

						snprintf(agent, sizeof(agent), "Agent/%s", p->agent);

						/* Login this channel and wait for it to go away */
						ast_indicate_data(chan, AST_CONTROL_HOLD, 
							S_OR(p->moh, NULL), 
							!ast_strlen_zero(p->moh) ? strlen(p->moh) + 1 : 0);

						/* Must be done after starting HOLD. */
						p->lastdisc = ast_tvnow();
						time(&p->loginstart);

						/*** DOCUMENTATION
							<managerEventInstance>
								<synopsis>Raised when an Agent has logged in.</synopsis>
								<syntax>
									<parameter name="Agent">
										<para>The name of the agent.</para>
									</parameter>
								</syntax>
								<see-also>
									<ref type="application">AgentLogin</ref>
									<ref type="managerEvent">Agentlogoff</ref>
								</see-also>
							</managerEventInstance>
						***/
						manager_event(EVENT_FLAG_AGENT, "Agentlogin",
							      "Agent: %s\r\n"
							      "Channel: %s\r\n"
							      "Uniqueid: %s\r\n",
							      p->agent, ast_channel_name(chan), ast_channel_uniqueid(chan));
						if (update_cdr && ast_channel_cdr(chan))
							snprintf(ast_channel_cdr(chan)->channel, sizeof(ast_channel_cdr(chan)->channel), "%s", agent);
						ast_queue_log("NONE", ast_channel_uniqueid(chan), agent, "AGENTLOGIN", "%s", ast_channel_name(chan));
						ast_verb(2, "Agent '%s' logged in (format %s/%s)\n", p->agent,
								    ast_getformatname(ast_channel_readformat(chan)), ast_getformatname(ast_channel_writeformat(chan)));

						ast_mutex_unlock(&p->lock);
						AST_LIST_UNLOCK(&agents);

						while (res >= 0) {
							ast_mutex_lock(&p->lock);
							if (p->deferlogoff) {
								p->deferlogoff = 0;
								ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);
								ast_mutex_unlock(&p->lock);
								break;
							}
							ast_mutex_unlock(&p->lock);

							AST_LIST_LOCK(&agents);
							ast_mutex_lock(&p->lock);
							if (p->lastdisc.tv_sec) {
								if (ast_tvdiff_ms(ast_tvnow(), p->lastdisc) > 0) {
									ast_debug(1, "Wrapup time for %s expired!\n", agent);
									p->lastdisc = ast_tv(0, 0);
									ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "%s", agent);
									if (p->ackcall) {
										check_beep(p, 0);
									} else {
										check_availability(p, 0);
									}
								}
							}
							ast_mutex_unlock(&p->lock);
							AST_LIST_UNLOCK(&agents);

							/* Synchronize channel ownership between call to agent and itself. */
							ast_mutex_lock(&p->lock);
							if (p->app_lock_flag) {
								ast_cond_signal(&p->login_wait_cond);
								ast_cond_wait(&p->app_complete_cond, &p->lock);
								if (ast_check_hangup(chan)) {
									/* Agent hungup */
									ast_mutex_unlock(&p->lock);
									break;
								}
							}
							ast_mutex_unlock(&p->lock);

							if (p->ackcall) {
								res = agent_ack_sleep(p);
								if (res == 1) {
									AST_LIST_LOCK(&agents);
									ast_mutex_lock(&p->lock);
									check_availability(p, 0);
									ast_mutex_unlock(&p->lock);
									AST_LIST_UNLOCK(&agents);
								}
							} else {
								res = ast_safe_sleep_conditional( chan, 1000, agent_cont_sleep, p );
							}
						}
						ast_mutex_lock(&p->lock);

						/* Logoff this channel */
						p->chan = NULL;
						logintime = time(NULL) - p->loginstart;
						p->loginstart = 0;

						/* Synchronize channel ownership between call to agent and itself. */
						if (p->app_lock_flag) {
							ast_cond_signal(&p->login_wait_cond);
							ast_cond_wait(&p->app_complete_cond, &p->lock);
						}

						if (p->owner) {
							ast_log(LOG_WARNING, "Huh?  We broke out when there was still an owner?\n");
						}

						p->acknowledged = 0;
						ast_mutex_unlock(&p->lock);

						ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "%s", agent);
						/*** DOCUMENTATION
							<managerEventInstance>
								<synopsis>Raised when an Agent has logged off.</synopsis>
								<syntax>
									<xi:include xpointer="xpointer(/docs/managerEvent[@name='Agentlogin']/managerEventInstance/syntax/parameter[@name='Agent'])" />
								</syntax>
								<see-also>
									<ref type="managerEvent">Agentlogin</ref>
								</see-also>
							</managerEventInstance>
						***/
						manager_event(EVENT_FLAG_AGENT, "Agentlogoff",
							      "Agent: %s\r\n"
							      "Logintime: %ld\r\n"
							      "Uniqueid: %s\r\n",
							      p->agent, logintime, ast_channel_uniqueid(chan));
						ast_queue_log("NONE", ast_channel_uniqueid(chan), agent, "AGENTLOGOFF", "%s|%ld", ast_channel_name(chan), logintime);
						ast_verb(2, "Agent '%s' logged out\n", p->agent);

						/* If there is no owner, go ahead and kill it now */
						if (p->dead && !p->owner) {
							agent_pvt_destroy(p);
						}
						AST_LIST_LOCK(&agents);
					} else {
						/* Agent hung up before could be logged in. */
						p->chan = NULL;

						ast_mutex_unlock(&p->lock);
					}
					res = -1;
				} else {
					ast_mutex_unlock(&p->lock);
					errmsg = "agent-alreadyon";
				}
				break;
			}
			ast_mutex_unlock(&p->lock);
			if (unlock_channel) {
				ast_channel_unlock(chan);
			}
		}
		AST_LIST_UNLOCK(&agents);

		if (!res && (max_login_tries==0 || tries < max_login_tries))
			res = ast_app_getdata(chan, errmsg, user, sizeof(user) - 1, 0);
	}
		
	if (!res)
		res = ast_safe_sleep(chan, 500);

 	return -1;
}

/*!
 *  \brief Called by the AgentMonitorOutgoing application (from the dial plan).
 *
 * \param chan
 * \param data
 * \returns
 * \sa login_exec(), load_module().
 */
static int agentmonitoroutgoing_exec(struct ast_channel *chan, const char *data)
{
	int exitifnoagentid = 0;
	int nowarnings = 0;
	int changeoutgoing = 0;
	int res = 0;
	char agent[AST_MAX_AGENT];

	if (data) {
		if (strchr(data, 'd'))
			exitifnoagentid = 1;
		if (strchr(data, 'n'))
			nowarnings = 1;
		if (strchr(data, 'c'))
			changeoutgoing = 1;
	}
	if (ast_channel_caller(chan)->id.number.valid
		&& !ast_strlen_zero(ast_channel_caller(chan)->id.number.str)) {
		const char *tmp;
		char agentvar[AST_MAX_BUF];
		snprintf(agentvar, sizeof(agentvar), "%s_%s", GETAGENTBYCALLERID,
			ast_channel_caller(chan)->id.number.str);
		if ((tmp = pbx_builtin_getvar_helper(NULL, agentvar))) {
			struct agent_pvt *p;
			ast_copy_string(agent, tmp, sizeof(agent));
			AST_LIST_LOCK(&agents);
			AST_LIST_TRAVERSE(&agents, p, list) {
				if (!strcasecmp(p->agent, tmp)) {
					if (changeoutgoing) snprintf(ast_channel_cdr(chan)->channel, sizeof(ast_channel_cdr(chan)->channel), "Agent/%s", p->agent);
					__agent_start_monitoring(chan, p, 1);
					break;
				}
			}
			AST_LIST_UNLOCK(&agents);
			
		} else {
			res = -1;
			if (!nowarnings)
				ast_log(LOG_WARNING, "Couldn't find the global variable %s, so I can't figure out which agent (if it's an agent) is placing outgoing call.\n", agentvar);
		}
	} else {
		res = -1;
		if (!nowarnings)
			ast_log(LOG_WARNING, "There is no callerid on that call, so I can't figure out which agent (if it's an agent) is placing outgoing call.\n");
	}
	if (res) {
		if (exitifnoagentid)
			return res;
	}
	return 0;
}

/*! \brief Part of PBX channel interface */
static int agent_devicestate(const char *data)
{
	struct agent_pvt *p;
	const char *device = data;
	int res = AST_DEVICE_INVALID;

	if (device[0] == '@' || device[0] == ':') {
		/* Device state of groups not supported. */
		return AST_DEVICE_INVALID;
	}

	/* Want device state of a specific agent. */
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		ast_mutex_lock(&p->lock);
		if (!p->pending && !strcmp(device, p->agent)) {
			if (p->owner) {
				res = AST_DEVICE_BUSY;
			} else if (p->chan) {
				if (p->lastdisc.tv_sec || p->deferlogoff) {
					/* Agent is in wrapup time so unavailable for another call. */
					res = AST_DEVICE_INUSE;
				} else {
					res = AST_DEVICE_NOT_INUSE;
				}
			} else {
				res = AST_DEVICE_UNAVAILABLE;
			}
			ast_mutex_unlock(&p->lock);
			break;
		}
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	return res;
}

/*!
 * \note This function expects the agent list to be locked
 */
static struct agent_pvt *find_agent(char *agentid)
{
	struct agent_pvt *cur;

	AST_LIST_TRAVERSE(&agents, cur, list) {
		if (!strcmp(cur->agent, agentid))
			break;	
	}

	return cur;	
}

static int function_agent(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parse;    
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(agentid);
		AST_APP_ARG(item);
	);
	char *tmp;
	struct agent_pvt *agent;

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "The AGENT function requires an argument - agentid!\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_NONSTANDARD_APP_ARGS(args, parse, ':');
	if (!args.item)
		args.item = "status";

	AST_LIST_LOCK(&agents);

	if (!(agent = find_agent(args.agentid))) {
		AST_LIST_UNLOCK(&agents);
		ast_log(LOG_WARNING, "Agent '%s' not found!\n", args.agentid);
		return -1;
	}

	if (!strcasecmp(args.item, "status")) {
		char *status = "LOGGEDOUT";
		if (agent->chan) {
			status = "LOGGEDIN";
		}
		ast_copy_string(buf, status, len);
	} else if (!strcasecmp(args.item, "password")) 
		ast_copy_string(buf, agent->password, len);
	else if (!strcasecmp(args.item, "name"))
		ast_copy_string(buf, agent->name, len);
	else if (!strcasecmp(args.item, "mohclass"))
		ast_copy_string(buf, agent->moh, len);
	else if (!strcasecmp(args.item, "channel")) {
		if (agent->chan) {
			ast_channel_lock(agent->chan);
			ast_copy_string(buf, ast_channel_name(agent->chan), len);
			ast_channel_unlock(agent->chan);
			tmp = strrchr(buf, '-');
			if (tmp)
				*tmp = '\0';
		} 
	} else if (!strcasecmp(args.item, "fullchannel")) {
		if (agent->chan) {
			ast_channel_lock(agent->chan);
			ast_copy_string(buf, ast_channel_name(agent->chan), len);
			ast_channel_unlock(agent->chan);
		} 
	} else if (!strcasecmp(args.item, "exten")) {
		buf[0] = '\0';
	}

	AST_LIST_UNLOCK(&agents);

	return 0;
}

static struct ast_custom_function agent_function = {
	.name = "AGENT",
	.read = function_agent,
};

/*!
 * \internal
 * \brief Callback used to generate the agents tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int agents_data_provider_get(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct agent_pvt *p;
	struct ast_data *data_agent, *data_channel, *data_talkingto;

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		struct ast_channel *owner;

		data_agent = ast_data_add_node(data_root, "agent");
		if (!data_agent) {
			continue;
		}

		ast_mutex_lock(&p->lock);
		owner = agent_lock_owner(p);

		if (!(p->pending)) {
			ast_data_add_str(data_agent, "id", p->agent);
			ast_data_add_structure(agent_pvt, data_agent, p);

			ast_data_add_bool(data_agent, "logged", p->chan ? 1 : 0);
			if (p->chan) {
				data_channel = ast_data_add_node(data_agent, "loggedon");
				if (!data_channel) {
					ast_mutex_unlock(&p->lock);
					ast_data_remove_node(data_root, data_agent);
					if (owner) {
						ast_channel_unlock(owner);
						owner = ast_channel_unref(owner);
					}
					continue;
				}
				ast_channel_data_add_structure(data_channel, p->chan, 0);
				if (owner && ast_bridged_channel(owner)) {
					data_talkingto = ast_data_add_node(data_agent, "talkingto");
					if (!data_talkingto) {
						ast_mutex_unlock(&p->lock);
						ast_data_remove_node(data_root, data_agent);
						if (owner) {
							ast_channel_unlock(owner);
							owner = ast_channel_unref(owner);
						}
						continue;
					}
					ast_channel_data_add_structure(data_talkingto, ast_bridged_channel(owner), 0);
				}
			} else {
				ast_data_add_node(data_agent, "talkingto");
				ast_data_add_node(data_agent, "loggedon");
			}
			ast_data_add_str(data_agent, "musiconhold", p->moh);
		}

		if (owner) {
			ast_channel_unlock(owner);
			owner = ast_channel_unref(owner);
		}

		ast_mutex_unlock(&p->lock);

		/* if this agent doesn't match remove the added agent. */
		if (!ast_data_search_match(search, data_agent)) {
			ast_data_remove_node(data_root, data_agent);
		}
	}
	AST_LIST_UNLOCK(&agents);

	return 0;
}

static const struct ast_data_handler agents_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = agents_data_provider_get
};

static const struct ast_data_entry agents_data_providers[] = {
	AST_DATA_ENTRY("asterisk/channel/agent/list", &agents_data_provider),
};

/*!
 * \brief Initialize the Agents module.
 * This function is being called by Asterisk when loading the module. 
 * Among other things it registers applications, cli commands and reads the cofiguration file.
 *
 * \returns int Always 0.
 */
static int load_module(void)
{
	if (!(agent_tech.capabilities = ast_format_cap_alloc())) {
		ast_log(LOG_ERROR, "ast_format_cap_alloc_nolock fail.\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_add_all(agent_tech.capabilities);
	/* Make sure we can register our agent channel type */
	if (ast_channel_register(&agent_tech)) {
		agent_tech.capabilities = ast_format_cap_destroy(agent_tech.capabilities);
		ast_log(LOG_ERROR, "Unable to register channel class 'Agent'\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	/* Read in the config */
	if (!read_agent_config(0)) {
		agent_tech.capabilities = ast_format_cap_destroy(agent_tech.capabilities);
		return AST_MODULE_LOAD_DECLINE;
	}
	/* Dialplan applications */
	ast_register_application_xml(app, login_exec);
	ast_register_application_xml(app3, agentmonitoroutgoing_exec);

	/* data tree */
	ast_data_register_multiple(agents_data_providers, ARRAY_LEN(agents_data_providers));

	/* Manager commands */
	ast_manager_register_xml("Agents", EVENT_FLAG_AGENT, action_agents);
	ast_manager_register_xml("AgentLogoff", EVENT_FLAG_AGENT, action_agent_logoff);

	/* CLI Commands */
	ast_cli_register_multiple(cli_agents, ARRAY_LEN(cli_agents));

	/* Dialplan Functions */
	ast_custom_function_register(&agent_function);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	return read_agent_config(1);
}

static int unload_module(void)
{
	struct agent_pvt *p;
	/* First, take us out of the channel loop */
	ast_channel_unregister(&agent_tech);
	/* Unregister dialplan functions */
	ast_custom_function_unregister(&agent_function);	
	/* Unregister CLI commands */
	ast_cli_unregister_multiple(cli_agents, ARRAY_LEN(cli_agents));
	/* Unregister dialplan applications */
	ast_unregister_application(app);
	ast_unregister_application(app3);
	/* Unregister manager command */
	ast_manager_unregister("Agents");
	ast_manager_unregister("AgentLogoff");
	/* Unregister the data tree */
	ast_data_unregister(NULL);
	/* Unregister channel */
	AST_LIST_LOCK(&agents);
	/* Hangup all interfaces if they have an owner */
	while ((p = AST_LIST_REMOVE_HEAD(&agents, list))) {
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
		ast_free(p);
	}
	AST_LIST_UNLOCK(&agents);

	agent_tech.capabilities = ast_format_cap_destroy(agent_tech.capabilities);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Agent Proxy Channel",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DRIVER,
		.nonoptreq = "res_monitor,chan_local",
	       );
