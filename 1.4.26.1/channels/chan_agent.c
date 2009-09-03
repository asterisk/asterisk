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
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp.h"
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

static const char tdesc[] = "Call Agent Proxy Channel";
static const char config[] = "agents.conf";

static const char app[] = "AgentLogin";
static const char app2[] = "AgentCallbackLogin";
static const char app3[] = "AgentMonitorOutgoing";

static const char synopsis[] = "Call agent login";
static const char synopsis2[] = "Call agent callback login";
static const char synopsis3[] = "Record agent's outgoing call";

static const char descrip[] =
"  AgentLogin([AgentNo][|options]):\n"
"Asks the agent to login to the system.  Always returns -1.  While\n"
"logged in, the agent can receive calls and will hear a 'beep'\n"
"when a new call comes in. The agent can dump the call by pressing\n"
"the star key.\n"
"The option string may contain zero or more of the following characters:\n"
"      's' -- silent login - do not announce the login ok segment after agent logged in/off\n";

static const char descrip2[] =
"  AgentCallbackLogin([AgentNo][|[options][|[exten]@context]]):\n"
"Asks the agent to login to the system with callback.\n"
"The agent's callback extension is called (optionally with the specified\n"
"context).\n"
"The option string may contain zero or more of the following characters:\n"
"      's' -- silent login - do not announce the login ok segment agent logged in/off\n";

static const char descrip3[] =
"  AgentMonitorOutgoing([options]):\n"
"Tries to figure out the id of the agent who is placing outgoing call based on\n"
"comparison of the callerid of the current interface and the global variable \n"
"placed by the AgentCallbackLogin application. That's why it should be used only\n"
"with the AgentCallbackLogin app. Uses the monitoring functions in chan_agent \n"
"instead of Monitor application. That have to be configured in the agents.conf file.\n"
"\nReturn value:\n"
"Normally the app returns 0 unless the options are passed. Also if the callerid or\n"
"the agentid are not specified it'll look for n+101 priority.\n"
"\nOptions:\n"
"	'd' - make the app return -1 if there is an error condition and there is\n"
"	      no extension n+101\n"
"	'c' - change the CDR so that the source of the call is 'Agent/agent_id'\n"
"	'n' - don't generate the warnings when there is no callerid or the\n"
"	      agentid is not known.\n"
"             It's handy if you want to have one context for agent and non-agent calls.\n";

static const char mandescr_agents[] =
"Description: Will list info about all possible agents.\n"
"Variables: NONE\n";

static const char mandescr_agent_logoff[] =
"Description: Sets an agent as no longer logged in.\n"
"Variables: (Names marked with * are required)\n"
"	*Agent: Agent ID of the agent to log off\n"
"	Soft: Set to 'true' to not hangup existing calls\n";

static const char mandescr_agent_callback_login[] =
"Description: Sets an agent as logged in with callback.\n"
"Variables: (Names marked with * are required)\n"
"	*Agent: Agent ID of the agent to login\n"
"	*Exten: Extension to use for callback\n"
"	Context: Context to use for callback\n"
"	AckCall: Set to 'true' to require an acknowledgement by '#' when agent is called back\n"
"	WrapupTime: the minimum amount of time after disconnecting before the caller can receive a new call\n";

static char moh[80] = "default";

#define AST_MAX_AGENT	80                          /*!< Agent ID or Password max length */
#define AST_MAX_BUF	256
#define AST_MAX_FILENAME_LEN	256

static const char pa_family[] = "Agents";          /*!< Persistent Agents astdb family */
#define PA_MAX_LEN 2048                             /*!< The maximum length of each persistent member agent database entry */

static int persistent_agents = 0;                   /*!< queues.conf [general] option */
static void dump_agents(void);

static ast_group_t group;
static int autologoff;
static int wrapuptime;
static int ackcall;
static int endcall;
static int multiplelogin = 1;
static int autologoffunavail = 0;

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

/*! \brief Structure representing an agent.  */
struct agent_pvt {
	ast_mutex_t lock;              /*!< Channel private lock */
	int dead;                      /*!< Poised for destruction? */
	int pending;                   /*!< Not a real agent -- just pending a match */
	int abouttograb;               /*!< About to grab */
	int autologoff;                /*!< Auto timeout time */
	int ackcall;                   /*!< ackcall */
	int deferlogoff;               /*!< Defer logoff to hangup */
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
	int inherited_devicestate;     /*!< Does the underlying channel have a devicestate to pass? */
	ast_mutex_t app_lock;          /**< Synchronization between owning applications */
	int app_lock_flag;
	ast_cond_t app_complete_cond;
	volatile int app_sleep_cond;   /**< Sleep condition for the login app */
	struct ast_channel *owner;     /**< Agent */
	/**! channel they logged in from. This may also be used to tell if an agent
	 * is a callback agent or not. If this field is not zero-length, then this is
	 * a callback agent */
	char loginchan[80];
	char logincallerid[80];        /**< Caller ID they had when they logged in */
	struct ast_channel *chan;      /**< Channel we use */
	AST_LIST_ENTRY(agent_pvt) list;	/**< Next Agent in the linked list. */
};

static AST_LIST_HEAD_STATIC(agents, agent_pvt);	/*!< Holds the list of agents (loaded form agents.conf). */

#define CHECK_FORMATS(ast, p) do { \
	if (p->chan) {\
		if (ast->nativeformats != p->chan->nativeformats) { \
			ast_log(LOG_DEBUG, "Native formats changing from %d to %d\n", ast->nativeformats, p->chan->nativeformats); \
			/* Native formats changed, reset things */ \
			ast->nativeformats = p->chan->nativeformats; \
			ast_log(LOG_DEBUG, "Resetting read to %d and write to %d\n", ast->readformat, ast->writeformat);\
			ast_set_read_format(ast, ast->readformat); \
			ast_set_write_format(ast, ast->writeformat); \
		} \
		if (p->chan->readformat != ast->rawreadformat && !p->chan->generator)  \
			ast_set_read_format(p->chan, ast->rawreadformat); \
		if (p->chan->writeformat != ast->rawwriteformat && !p->chan->generator) \
			ast_set_write_format(p->chan, ast->rawwriteformat); \
	} \
} while(0)

/*! \brief Cleanup moves all the relevant FD's from the 2nd to the first, but retains things
   properly for a timingfd XXX This might need more work if agents were logged in as agents or other
   totally impractical combinations XXX */

#define CLEANUP(ast, p) do { \
	int x; \
	if (p->chan) { \
		for (x=0;x<AST_MAX_FDS;x++) {\
			if (x != AST_TIMING_FD) \
				ast->fds[x] = p->chan->fds[x]; \
		} \
		ast->fds[AST_AGENT_FD] = p->chan->fds[AST_TIMING_FD]; \
	} \
} while(0)

/*--- Forward declarations */
static struct ast_channel *agent_request(const char *type, int format, void *data, int *cause);
static int agent_devicestate(void *data);
static void agent_logoff_maintenance(struct agent_pvt *p, char *loginchan, long logintime, const char *uniqueid, char *logcommand);
static int agent_digit_begin(struct ast_channel *ast, char digit);
static int agent_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int agent_call(struct ast_channel *ast, char *dest, int timeout);
static int agent_hangup(struct ast_channel *ast);
static int agent_answer(struct ast_channel *ast);
static struct ast_frame *agent_read(struct ast_channel *ast);
static int agent_write(struct ast_channel *ast, struct ast_frame *f);
static int agent_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);
static int agent_sendtext(struct ast_channel *ast, const char *text);
static int agent_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int agent_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static struct ast_channel *agent_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge);
static void set_agentbycallerid(const char *callerid, const char *agent);
static struct ast_channel* agent_get_base_channel(struct ast_channel *chan);
static int agent_set_base_channel(struct ast_channel *chan, struct ast_channel *base);
static int agent_logoff(const char *agent, int soft);

/*! \brief Channel interface description for PBX integration */
static const struct ast_channel_tech agent_tech = {
	.type = "Agent",
	.description = tdesc,
	.capabilities = -1,
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
	.set_base_channel = agent_set_base_channel,
};

static int agent_devicestate_cb(const char *dev, int state, void *data)
{
	int res, i;
	struct agent_pvt *p;
	char basename[AST_CHANNEL_NAME], *tmp;

	/* Skip Agent status */
	if (!strncasecmp(dev, "Agent/", 6)) {
		return 0;
	}

	/* Try to be safe, but don't deadlock */
	for (i = 0; i < 10; i++) {
		if ((res = AST_LIST_TRYLOCK(&agents)) == 0) {
			break;
		}
	}
	if (res) {
		return -1;
	}

	AST_LIST_TRAVERSE(&agents, p, list) {
		ast_mutex_lock(&p->lock);
		if (p->chan && !ast_strlen_zero(p->loginchan)) {
			ast_copy_string(basename, p->chan->name, sizeof(basename));
			if ((tmp = strrchr(basename, '-'))) {
				*tmp = '\0';
			}
			if (strcasecmp(p->chan->name, dev) == 0 || strcasecmp(basename, dev) == 0) {
				p->inherited_devicestate = state;
				ast_device_state_changed("Agent/%s", p->agent);
			}
		}
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	return 0;
}

/*!
 * Adds an agent to the global list of agents.
 *
 * \param agent A string with the username, password and real name of an agent. As defined in agents.conf. Example: "13,169,John Smith"
 * \param pending If it is pending or not.
 * @return The just created agent.
 * \sa agent_pvt, agents.
 */
static struct agent_pvt *add_agent(char *agent, int pending)
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
	AST_NONSTANDARD_APP_ARGS(args, parse, ',');

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
	
	/* Are we searching for the agent here ? To see if it exists already ? */
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (!pending && !strcmp(p->agent, agt))
			break;
	}
	if (!p) {
		// Build the agent.
		if (!(p = ast_calloc(1, sizeof(*p))))
			return NULL;
		ast_copy_string(p->agent, agt, sizeof(p->agent));
		ast_mutex_init(&p->lock);
		ast_mutex_init(&p->app_lock);
		ast_cond_init(&p->app_complete_cond, NULL);
		p->app_lock_flag = 0;
		p->app_sleep_cond = 1;
		p->group = group;
		p->pending = pending;
		p->inherited_devicestate = -1;
		AST_LIST_INSERT_TAIL(&agents, p, list);
	}
	
	ast_copy_string(p->password, password ? password : "", sizeof(p->password));
	ast_copy_string(p->name, name ? name : "", sizeof(p->name));
	ast_copy_string(p->moh, moh, sizeof(p->moh));
	p->ackcall = ackcall;
	p->autologoff = autologoff;

	/* If someone reduces the wrapuptime and reloads, we want it
	 * to change the wrapuptime immediately on all calls */
	if (p->wrapuptime > wrapuptime) {
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
 * \param p Agent to be deleted.
 * \returns Always 0.
 */
static int agent_cleanup(struct agent_pvt *p)
{
	struct ast_channel *chan = p->owner;
	p->owner = NULL;
	chan->tech_pvt = NULL;
	p->app_sleep_cond = 1;
	/* Release ownership of the agent to other threads (presumably running the login app). */
	p->app_lock_flag = 0;
	ast_cond_signal(&p->app_complete_cond);
	if (chan)
		ast_channel_free(chan);
	if (p->dead) {
		ast_mutex_destroy(&p->lock);
		ast_mutex_destroy(&p->app_lock);
		ast_cond_destroy(&p->app_complete_cond);
		free(p);
        }
	return 0;
}

static int check_availability(struct agent_pvt *newlyavailable, int needlock);

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
	if (!ast->monitor) {
		snprintf(filename, sizeof(filename), "agent-%s-%s",p->agent, ast->uniqueid);
		/* substitute . for - */
		if ((pointer = strchr(filename, '.')))
			*pointer = '-';
		snprintf(tmp, sizeof(tmp), "%s%s", savecallsin, filename);
		ast_monitor_start(ast, recordformat, tmp, needlock);
		ast_monitor_setjoinfiles(ast, 1);
		snprintf(tmp2, sizeof(tmp2), "%s%s.%s", urlprefix, filename, recordformatext);
#if 0
		ast_verbose("name is %s, link is %s\n",tmp, tmp2);
#endif
		if (!ast->cdr)
			ast->cdr = ast_cdr_alloc();
		ast_cdr_setuserfield(ast, tmp2);
		res = 0;
	} else
		ast_log(LOG_ERROR, "Recording already started on that call.\n");
	return res;
}

static int agent_start_monitoring(struct ast_channel *ast, int needlock)
{
	return __agent_start_monitoring(ast, ast->tech_pvt, needlock);
}

static struct ast_frame *agent_read(struct ast_channel *ast)
{
	struct agent_pvt *p = ast->tech_pvt;
	struct ast_frame *f = NULL;
	static struct ast_frame answer_frame = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
	const char *status;
	int cur_time = time(NULL);
	ast_mutex_lock(&p->lock);
	CHECK_FORMATS(ast, p);
	if (!p->start) {
		p->start = cur_time;
	}
	if (p->chan) {
		ast_copy_flags(p->chan, ast, AST_FLAG_EXCEPTION);
		p->chan->fdno = (ast->fdno == AST_AGENT_FD) ? AST_TIMING_FD : ast->fdno;
		f = ast_read(p->chan);
	} else
		f = &ast_null_frame;
	if (!f) {
		/* If there's a channel, hang it up (if it's on a callback) make it NULL */
		if (p->chan) {
			p->chan->_bridge = NULL;
			/* Note that we don't hangup if it's not a callback because Asterisk will do it
			   for us when the PBX instance that called login finishes */
			if (!ast_strlen_zero(p->loginchan)) {
				if (p->chan)
					ast_log(LOG_DEBUG, "Bridge on '%s' being cleared (2)\n", p->chan->name);
				if (p->owner->_state != AST_STATE_UP) {
					int howlong = cur_time - p->start;
					if (p->autologoff && howlong >= p->autologoff) {
						p->loginstart = 0;
							ast_log(LOG_NOTICE, "Agent '%s' didn't answer/confirm within %d seconds (waited %d)\n", p->name, p->autologoff, howlong);
						agent_logoff_maintenance(p, p->loginchan, (cur_time = p->loginstart), ast->uniqueid, "Autologoff");
					}
				}
				status = pbx_builtin_getvar_helper(p->chan, "CHANLOCALSTATUS");
				if (autologoffunavail && status && !strcasecmp(status, "CHANUNAVAIL")) {
					long logintime = cur_time - p->loginstart;
					p->loginstart = 0;
					ast_log(LOG_NOTICE, "Agent read: '%s' is not available now, auto logoff\n", p->name);
					agent_logoff_maintenance(p, p->loginchan, logintime, ast->uniqueid, "Chanunavail");
				}
				ast_hangup(p->chan);
				if (p->wrapuptime && p->acknowledged)
					p->lastdisc = ast_tvadd(ast_tvnow(), ast_samp2tv(p->wrapuptime, 1000));
			}
			p->chan = NULL;
			p->inherited_devicestate = -1;
			ast_device_state_changed("Agent/%s", p->agent);
			p->acknowledged = 0;
		}
	} else {
		/* if acknowledgement is not required, and the channel is up, we may have missed
 		   an AST_CONTROL_ANSWER (if there was one), so mark the call acknowledged anyway */
		if (!p->ackcall && !p->acknowledged && p->chan && (p->chan->_state == AST_STATE_UP)) {
			p->acknowledged = 1;
		}
		if (!p->acknowledged) {
			int howlong = cur_time - p->start;
			if (p->autologoff && (howlong >= p->autologoff)) {
				ast_log(LOG_NOTICE, "Agent '%s' didn't answer/confirm within %d seconds (waited %d)\n", p->name, p->autologoff, howlong);
				agent_logoff_maintenance(p, p->loginchan, (cur_time - p->loginstart), ast->uniqueid, "Autologoff");
				agent_logoff(p->agent, 0);
			}
		}
		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass == AST_CONTROL_ANSWER) {
				if (p->ackcall) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s answered, waiting for '#' to acknowledge\n", p->chan->name);
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
			if((!p->acknowledged && f->subclass == '#') || (f->subclass == '*' && endcall)){
				ast_frfree(f);
				f = &ast_null_frame;
			}
			break;
		case AST_FRAME_DTMF_END:
			if (!p->acknowledged && (f->subclass == '#')) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "%s acknowledged\n", p->chan->name);
				p->acknowledged = 1;
				ast_frfree(f);
				f = &answer_frame;
			} else if (f->subclass == '*' && endcall) {
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

	CLEANUP(ast,p);
	if (p->chan && !p->chan->_bridge) {
		if (strcasecmp(p->chan->tech->type, "Local")) {
			p->chan->_bridge = ast;
			if (p->chan)
				ast_log(LOG_DEBUG, "Bridge on '%s' being set to '%s' (3)\n", p->chan->name, p->chan->_bridge->name);
		}
	}
	ast_mutex_unlock(&p->lock);
	if (recordagentcalls && f == &answer_frame)
		agent_start_monitoring(ast,0);
	return f;
}

static int agent_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	struct agent_pvt *p = ast->tech_pvt;
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan) 
		res = ast_channel_sendhtml(p->chan, subclass, data, datalen);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_sendtext(struct ast_channel *ast, const char *text)
{
	struct agent_pvt *p = ast->tech_pvt;
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan) 
		res = ast_sendtext(p->chan, text);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct agent_pvt *p = ast->tech_pvt;
	int res = -1;
	CHECK_FORMATS(ast, p);
	ast_mutex_lock(&p->lock);
	if (!p->chan) 
		res = 0;
	else {
		if ((f->frametype != AST_FRAME_VOICE) ||
		    (f->frametype != AST_FRAME_VIDEO) ||
		    (f->subclass == p->chan->writeformat)) {
			res = ast_write(p->chan, f);
		} else {
			ast_log(LOG_DEBUG, "Dropping one incompatible %s frame on '%s' to '%s'\n", 
				f->frametype == AST_FRAME_VOICE ? "audio" : "video",
				ast->name, p->chan->name);
			res = 0;
		}
	}
	CLEANUP(ast, p);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct agent_pvt *p = newchan->tech_pvt;
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
	struct agent_pvt *p = ast->tech_pvt;
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan && !ast_check_hangup(p->chan)) {
		while (ast_channel_trylock(p->chan)) {
			ast_channel_unlock(ast);
			usleep(1);
			ast_channel_lock(ast);
		}
  		res = p->chan->tech->indicate ? p->chan->tech->indicate(p->chan, condition, data, datalen) : -1;
		ast_channel_unlock(p->chan);
	} else
		res = 0;
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_digit_begin(struct ast_channel *ast, char digit)
{
	struct agent_pvt *p = ast->tech_pvt;
	ast_mutex_lock(&p->lock);
	if (p->chan) {
		ast_senddigit_begin(p->chan, digit);
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int agent_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct agent_pvt *p = ast->tech_pvt;
	ast_mutex_lock(&p->lock);
	if (p->chan) {
		ast_senddigit_end(p->chan, digit, duration);
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int agent_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct agent_pvt *p = ast->tech_pvt;
	int res = -1;
	int newstate=0;
	ast_mutex_lock(&p->lock);
	p->acknowledged = 0;
	if (!p->chan) {
		if (p->pending) {
			ast_log(LOG_DEBUG, "Pretending to dial on pending agent\n");
			newstate = AST_STATE_DIALING;
			res = 0;
		} else {
			ast_log(LOG_NOTICE, "Whoa, they hung up between alloc and call...  what are the odds of that?\n");
			res = -1;
		}
		ast_mutex_unlock(&p->lock);
		if (newstate)
			ast_setstate(ast, newstate);
		return res;
	} else if (!ast_strlen_zero(p->loginchan)) {
		time(&p->start);
		/* Call on this agent */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "outgoing agentcall, to agent '%s', on '%s'\n", p->agent, p->chan->name);
		ast_set_callerid(p->chan,
			ast->cid.cid_num, ast->cid.cid_name, NULL);
		ast_channel_inherit_variables(ast, p->chan);
		res = ast_call(p->chan, p->loginchan, 0);
		CLEANUP(ast,p);
		ast_mutex_unlock(&p->lock);
		return res;
	}
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "agent_call, call to agent '%s' call on '%s'\n", p->agent, p->chan->name);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Playing beep, lang '%s'\n", p->chan->language);
	res = ast_streamfile(p->chan, beep, p->chan->language);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Played beep, result '%d'\n", res);
	if (!res) {
		res = ast_waitstream(p->chan, "");
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Waited for stream, result '%d'\n", res);
	}
	if (!res) {
		res = ast_set_read_format(p->chan, ast_best_codec(p->chan->nativeformats));
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Set read format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set read format to %s\n", ast_getformatname(ast_best_codec(p->chan->nativeformats)));
	} else {
		/* Agent hung-up */
		p->chan = NULL;
		p->inherited_devicestate = -1;
		ast_device_state_changed("Agent/%s", p->agent);
	}

	if (!res) {
		res = ast_set_write_format(p->chan, ast_best_codec(p->chan->nativeformats));
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Set write format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set write format to %s\n", ast_getformatname(ast_best_codec(p->chan->nativeformats)));
	}
	if(!res) {
		/* Call is immediately up, or might need ack */
		if (p->ackcall > 1)
			newstate = AST_STATE_RINGING;
		else {
			newstate = AST_STATE_UP;
			if (recordagentcalls)
				agent_start_monitoring(ast, 0);
			p->acknowledged = 1;
		}
		res = 0;
	}
	CLEANUP(ast, p);
	ast_mutex_unlock(&p->lock);
	if (newstate)
		ast_setstate(ast, newstate);
	return res;
}

/*! \brief store/clear the global variable that stores agentid based on the callerid */
static void set_agentbycallerid(const char *callerid, const char *agent)
{
	char buf[AST_MAX_BUF];

	/* if there is no Caller ID, nothing to do */
	if (ast_strlen_zero(callerid))
		return;

	snprintf(buf, sizeof(buf), "%s_%s", GETAGENTBYCALLERID, callerid);
	pbx_builtin_setvar_helper(NULL, buf, agent);
}

/*! \brief return the channel or base channel if one exists.  This function assumes the channel it is called on is already locked */
struct ast_channel* agent_get_base_channel(struct ast_channel *chan)
{
	struct agent_pvt *p = NULL;
	struct ast_channel *base = chan;

	/* chan is locked by the calling function */
	if (!chan || !chan->tech_pvt) {
		ast_log(LOG_ERROR, "whoa, you need a channel (0x%ld) with a tech_pvt (0x%ld) to get a base channel.\n", (long)chan, (chan)?(long)chan->tech_pvt:(long)NULL);
		return NULL;
	}
	p = chan->tech_pvt;
	if (p->chan) 
		base = p->chan;
	return base;
}

int agent_set_base_channel(struct ast_channel *chan, struct ast_channel *base)
{
	struct agent_pvt *p = NULL;
	
	if (!chan || !base) {
		ast_log(LOG_ERROR, "whoa, you need a channel (0x%ld) and a base channel (0x%ld) for setting.\n", (long)chan, (long)base);
		return -1;
	}
	p = chan->tech_pvt;
	if (!p) {
		ast_log(LOG_ERROR, "whoa, channel %s is missing his tech_pvt structure!!.\n", chan->name);
		return -1;
	}
	p->chan = base;
	return 0;
}

static int agent_hangup(struct ast_channel *ast)
{
	struct agent_pvt *p = ast->tech_pvt;
	int howlong = 0;
	const char *status;
	ast_mutex_lock(&p->lock);
	p->owner = NULL;
	ast->tech_pvt = NULL;
	p->app_sleep_cond = 1;
	p->acknowledged = 0;

	/* if they really are hung up then set start to 0 so the test
	 * later if we're called on an already downed channel
	 * doesn't cause an agent to be logged out like when
	 * agent_request() is followed immediately by agent_hangup()
	 * as in apps/app_chanisavail.c:chanavail_exec()
	 */

	if (option_debug)
		ast_log(LOG_DEBUG, "Hangup called for state %s\n", ast_state2str(ast->_state));
	if (p->start && (ast->_state != AST_STATE_UP)) {
		howlong = time(NULL) - p->start;
		p->start = 0;
	} else if (ast->_state == AST_STATE_RESERVED) 
		howlong = 0;
	else
		p->start = 0; 
	if (p->chan) {
		p->chan->_bridge = NULL;
		/* If they're dead, go ahead and hang up on the agent now */
		if (!ast_strlen_zero(p->loginchan)) {
			/* Store last disconnect time */
			if (p->wrapuptime)
				p->lastdisc = ast_tvadd(ast_tvnow(), ast_samp2tv(p->wrapuptime, 1000));
			else
				p->lastdisc = ast_tv(0,0);
			if (p->chan) {
				status = pbx_builtin_getvar_helper(p->chan, "CHANLOCALSTATUS");
				if (autologoffunavail && status && !strcasecmp(status, "CHANUNAVAIL")) {
					long logintime = time(NULL) - p->loginstart;
					p->loginstart = 0;
					ast_log(LOG_NOTICE, "Agent hangup: '%s' is not available now, auto logoff\n", p->name);
					agent_logoff_maintenance(p, p->loginchan, logintime, ast->uniqueid, "Chanunavail");
				}
				/* Recognize the hangup and pass it along immediately */
				ast_hangup(p->chan);
				p->chan = NULL;
				p->inherited_devicestate = -1;
				ast_device_state_changed("Agent/%s", p->agent);
			}
			ast_log(LOG_DEBUG, "Hungup, howlong is %d, autologoff is %d\n", howlong, p->autologoff);
			if ((p->deferlogoff) || (howlong && p->autologoff && (howlong > p->autologoff))) {
				long logintime = time(NULL) - p->loginstart;
				p->loginstart = 0;
				if (!p->deferlogoff)
					ast_log(LOG_NOTICE, "Agent '%s' didn't answer/confirm within %d seconds (waited %d)\n", p->name, p->autologoff, howlong);
				p->deferlogoff = 0;
				agent_logoff_maintenance(p, p->loginchan, logintime, ast->uniqueid, "Autologoff");
				if (persistent_agents)
					dump_agents();
			}
		} else if (p->dead) {
			ast_channel_lock(p->chan);
			ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
			ast_channel_unlock(p->chan);
		} else if (p->loginstart) {
			ast_channel_lock(p->chan);
			ast_indicate_data(p->chan, AST_CONTROL_HOLD, 
				S_OR(p->moh, NULL),
				!ast_strlen_zero(p->moh) ? strlen(p->moh) + 1 : 0);
			ast_channel_unlock(p->chan);
		}
	}
	ast_mutex_unlock(&p->lock);

	/* Only register a device state change if the agent is still logged in */
	if (!p->loginstart) {
		p->loginchan[0] = '\0';
		p->logincallerid[0] = '\0';
		if (persistent_agents)
			dump_agents();
	} else {
		ast_device_state_changed("Agent/%s", p->agent);
	}

	if (p->pending) {
		AST_LIST_LOCK(&agents);
		AST_LIST_REMOVE(&agents, p, list);
		AST_LIST_UNLOCK(&agents);
	}
	if (p->abouttograb) {
		/* Let the "about to grab" thread know this isn't valid anymore, and let it
		   kill it later */
		p->abouttograb = 0;
	} else if (p->dead) {
		ast_mutex_destroy(&p->lock);
		ast_mutex_destroy(&p->app_lock);
		ast_cond_destroy(&p->app_complete_cond);
		free(p);
	} else {
		if (p->chan) {
			/* Not dead -- check availability now */
			ast_mutex_lock(&p->lock);
			/* Store last disconnect time */
			p->lastdisc = ast_tvadd(ast_tvnow(), ast_samp2tv(p->wrapuptime, 1000));
			ast_mutex_unlock(&p->lock);
		}
		/* Release ownership of the agent to other threads (presumably running the login app). */
		if (ast_strlen_zero(p->loginchan)) {
			p->app_lock_flag = 0;
			ast_cond_signal(&p->app_complete_cond);
		}
	}
	return 0;
}

static int agent_cont_sleep( void *data )
{
	struct agent_pvt *p;
	int res;

	p = (struct agent_pvt *)data;

	ast_mutex_lock(&p->lock);
	res = p->app_sleep_cond;
	if (p->lastdisc.tv_sec) {
		if (ast_tvdiff_ms(ast_tvnow(), p->lastdisc) > 0) 
			res = 1;
	}
	ast_mutex_unlock(&p->lock);

	if(option_debug > 4 && !res )
		ast_log(LOG_DEBUG, "agent_cont_sleep() returning %d\n", res );

	return res;
}

static int agent_ack_sleep(void *data)
{
	struct agent_pvt *p;
	int res=0;
	int to = 1000;
	struct ast_frame *f;

	/* Wait a second and look for something */

	p = (struct agent_pvt *) data;
	if (!p->chan) 
		return -1;

	for(;;) {
		to = ast_waitfor(p->chan, to);
		if (to < 0) 
			return -1;
		if (!to) 
			return 0;
		f = ast_read(p->chan);
		if (!f) 
			return -1;
		if (f->frametype == AST_FRAME_DTMF)
			res = f->subclass;
		else
			res = 0;
		ast_frfree(f);
		ast_mutex_lock(&p->lock);
		if (!p->app_sleep_cond) {
			ast_mutex_unlock(&p->lock);
			return 0;
		} else if (res == '#') {
			ast_mutex_unlock(&p->lock);
			return 1;
		}
		ast_mutex_unlock(&p->lock);
		res = 0;
	}
	return res;
}

static struct ast_channel *agent_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge)
{
	struct agent_pvt *p = bridge->tech_pvt;
	struct ast_channel *ret = NULL;

	if (p) {
		if (chan == p->chan)
			ret = bridge->_bridge;
		else if (chan == bridge->_bridge)
			ret = p->chan;
	}

	if (option_debug)
		ast_log(LOG_DEBUG, "Asked for bridged channel on '%s'/'%s', returning '%s'\n", chan->name, bridge->name, ret ? ret->name : "<none>");
	return ret;
}

/*! \brief Create new agent channel */
static struct ast_channel *agent_new(struct agent_pvt *p, int state)
{
	struct ast_channel *tmp;
	int alreadylocked;
#if 0
	if (!p->chan) {
		ast_log(LOG_WARNING, "No channel? :(\n");
		return NULL;
	}
#endif	
	if (p->pending)
		tmp = ast_channel_alloc(0, state, 0, 0, "", p->chan ? p->chan->exten:"", p->chan ? p->chan->context:"", 0, "Agent/P%s-%d", p->agent, (int) ast_random() & 0xffff);
	else
		tmp = ast_channel_alloc(0, state, 0, 0, "", p->chan ? p->chan->exten:"", p->chan ? p->chan->context:"", 0, "Agent/%s", p->agent);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate agent channel structure\n");
		return NULL;
	}

	tmp->tech = &agent_tech;
	if (p->chan) {
		tmp->nativeformats = p->chan->nativeformats;
		tmp->writeformat = p->chan->writeformat;
		tmp->rawwriteformat = p->chan->writeformat;
		tmp->readformat = p->chan->readformat;
		tmp->rawreadformat = p->chan->readformat;
		ast_string_field_set(tmp, language, p->chan->language);
		ast_copy_string(tmp->context, p->chan->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, p->chan->exten, sizeof(tmp->exten));
		/* XXX Is this really all we copy form the originating channel?? */
	} else {
		tmp->nativeformats = AST_FORMAT_SLINEAR;
		tmp->writeformat = AST_FORMAT_SLINEAR;
		tmp->rawwriteformat = AST_FORMAT_SLINEAR;
		tmp->readformat = AST_FORMAT_SLINEAR;
		tmp->rawreadformat = AST_FORMAT_SLINEAR;
	}
	/* Safe, agentlock already held */
	tmp->tech_pvt = p;
	p->owner = tmp;
	/* XXX: this needs fixing */
#if 0
	ast_atomic_fetchadd_int(&__mod_desc->usecnt, +1);
#endif
	ast_update_use_count();
	tmp->priority = 1;
	/* Wake up and wait for other applications (by definition the login app)
	 * to release this channel). Takes ownership of the agent channel
	 * to this thread only.
	 * For signalling the other thread, ast_queue_frame is used until we
	 * can safely use signals for this purpose. The pselect() needs to be
	 * implemented in the kernel for this.
	 */
	p->app_sleep_cond = 0;

	alreadylocked = p->app_lock_flag;
	p->app_lock_flag = 1;

	if(ast_strlen_zero(p->loginchan) && alreadylocked) {
		if (p->chan) {
			ast_queue_frame(p->chan, &ast_null_frame);
			ast_mutex_unlock(&p->lock);	/* For other thread to read the condition. */
			p->app_lock_flag = 1;
			ast_mutex_lock(&p->lock);
		} else {
			ast_log(LOG_WARNING, "Agent disconnected while we were connecting the call\n");
			p->owner = NULL;
			tmp->tech_pvt = NULL;
			p->app_sleep_cond = 1;
			ast_channel_free( tmp );
			ast_mutex_unlock(&p->lock);	/* For other thread to read the condition. */
			p->app_lock_flag = 0;
			ast_cond_signal(&p->app_complete_cond);
			return NULL;
		}
	} else if (!ast_strlen_zero(p->loginchan)) {
		if (p->chan)
			ast_queue_frame(p->chan, &ast_null_frame);
		if (!p->chan) {
			ast_log(LOG_WARNING, "Agent disconnected while we were connecting the call\n");
			p->owner = NULL;
			tmp->tech_pvt = NULL;
			p->app_sleep_cond = 1;
			ast_channel_free( tmp );
			ast_mutex_unlock(&p->lock);     /* For other thread to read the condition. */
			return NULL;
		}	
	} 
	if (p->chan)
		ast_indicate(p->chan, AST_CONTROL_UNHOLD);
	return tmp;
}


/*!
 * Read configuration data. The file named agents.conf.
 *
 * \returns Always 0, or so it seems.
 */
static int read_agent_config(void)
{
	struct ast_config *cfg;
	struct ast_config *ucfg;
	struct ast_variable *v;
	struct agent_pvt *p;
	const char *general_val;
	const char *catname;
	const char *hasagent;
	int genhasagent;

	group = 0;
	autologoff = 0;
	wrapuptime = 0;
	ackcall = 0;
	endcall = 1;
	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_NOTICE, "No agent configuration found -- agent support disabled\n");
		return 0;
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

	/* Read in [general] section for persistence */
	if ((general_val = ast_variable_retrieve(cfg, "general", "persistentagents")))
		persistent_agents = ast_true(general_val);
	multiplelogin = ast_true(ast_variable_retrieve(cfg, "general", "multiplelogin"));

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
			if (!strcasecmp(v->value, "always"))
				ackcall = 2;
			else if (ast_true(v->value))
				ackcall = 1;
			else
				ackcall = 0;
		} else if (!strcasecmp(v->name, "endcall")) {
			endcall = ast_true(v->value);
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
	if ((ucfg = ast_config_load("users.conf"))) {
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
			AST_LIST_REMOVE_CURRENT(&agents, list);
			/* Destroy if  appropriate */
			if (!p->owner) {
				if (!p->chan) {
					ast_mutex_destroy(&p->lock);
					ast_mutex_destroy(&p->app_lock);
					ast_cond_destroy(&p->app_complete_cond);
					free(p);
				} else {
					/* Cause them to hang up */
					ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
				}
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&agents);
	ast_config_destroy(cfg);
	return 1;
}

static int check_availability(struct agent_pvt *newlyavailable, int needlock)
{
	struct ast_channel *chan=NULL, *parent=NULL;
	struct agent_pvt *p;
	int res;

	if (option_debug)
		ast_log(LOG_DEBUG, "Checking availability of '%s'\n", newlyavailable->agent);
	if (needlock)
		AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (p == newlyavailable) {
			continue;
		}
		ast_mutex_lock(&p->lock);
		if (!p->abouttograb && p->pending && ((p->group && (newlyavailable->group & p->group)) || !strcmp(p->agent, newlyavailable->agent))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Call '%s' looks like a winner for agent '%s'\n", p->owner->name, newlyavailable->agent);
			/* We found a pending call, time to merge */
			chan = agent_new(newlyavailable, AST_STATE_DOWN);
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
		if (newlyavailable->ackcall > 1) {
			/* Don't do beep here */
			res = 0;
		} else {
			if (option_debug > 2)
				ast_log( LOG_DEBUG, "Playing beep, lang '%s'\n", newlyavailable->chan->language);
			res = ast_streamfile(newlyavailable->chan, beep, newlyavailable->chan->language);
			if (option_debug > 2)
				ast_log( LOG_DEBUG, "Played beep, result '%d'\n", res);
			if (!res) {
				res = ast_waitstream(newlyavailable->chan, "");
				ast_log( LOG_DEBUG, "Waited for stream, result '%d'\n", res);
			}
		}
		if (!res) {
			/* Note -- parent may have disappeared */
			if (p->abouttograb) {
				newlyavailable->acknowledged = 1;
				/* Safe -- agent lock already held */
				ast_setstate(parent, AST_STATE_UP);
				ast_setstate(chan, AST_STATE_UP);
				ast_copy_string(parent->context, chan->context, sizeof(parent->context));
				/* Go ahead and mark the channel as a zombie so that masquerade will
				   destroy it for us, and we need not call ast_hangup */
				ast_mutex_lock(&parent->lock);
				ast_set_flag(chan, AST_FLAG_ZOMBIE);
				ast_channel_masquerade(parent, chan);
				ast_mutex_unlock(&parent->lock);
				p->abouttograb = 0;
			} else {
				if (option_debug)
					ast_log(LOG_DEBUG, "Sneaky, parent disappeared in the mean time...\n");
				agent_cleanup(newlyavailable);
			}
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "Ugh...  Agent hung up at exactly the wrong time\n");
			agent_cleanup(newlyavailable);
		}
	}
	return 0;
}

static int check_beep(struct agent_pvt *newlyavailable, int needlock)
{
	struct agent_pvt *p;
	int res=0;

	ast_log(LOG_DEBUG, "Checking beep availability of '%s'\n", newlyavailable->agent);
	if (needlock)
		AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (p == newlyavailable) {
			continue;
		}
		ast_mutex_lock(&p->lock);
		if (!p->abouttograb && p->pending && ((p->group && (newlyavailable->group & p->group)) || !strcmp(p->agent, newlyavailable->agent))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Call '%s' looks like a would-be winner for agent '%s'\n", p->owner->name, newlyavailable->agent);
			ast_mutex_unlock(&p->lock);
			break;
		}
		ast_mutex_unlock(&p->lock);
	}
	if (needlock)
		AST_LIST_UNLOCK(&agents);
	if (p) {
		ast_mutex_unlock(&newlyavailable->lock);
		if (option_debug > 2)
			ast_log( LOG_DEBUG, "Playing beep, lang '%s'\n", newlyavailable->chan->language);
		res = ast_streamfile(newlyavailable->chan, beep, newlyavailable->chan->language);
		if (option_debug > 2)
			ast_log( LOG_DEBUG, "Played beep, result '%d'\n", res);
		if (!res) {
			res = ast_waitstream(newlyavailable->chan, "");
			if (option_debug)
				ast_log( LOG_DEBUG, "Waited for stream, result '%d'\n", res);
		}
		ast_mutex_lock(&newlyavailable->lock);
	}
	return res;
}

/* return 1 if multiple login is fine, 0 if it is not and we find a match, -1 if multiplelogin is not allowed and we don't find a match. */
static int allow_multiple_login(char *chan, char *context)
{
	struct agent_pvt *p;
	char loginchan[80];

	if (multiplelogin) {
		return 1;
	}
	if (!chan) {
		return 0;
	}

	snprintf(loginchan, sizeof(loginchan), "%s@%s", chan, S_OR(context, "default"));
	
	AST_LIST_TRAVERSE(&agents, p, list) {
		if(!strcasecmp(loginchan, p->loginchan))
			return 0;
	}
	return -1;
}

/*! \brief Part of the Asterisk PBX interface */
static struct ast_channel *agent_request(const char *type, int format, void *data, int *cause)
{
	struct agent_pvt *p;
	struct ast_channel *chan = NULL;
	char *s;
	ast_group_t groupmatch;
	int groupoff;
	int waitforagent=0;
	int hasagent = 0;
	struct timeval tv;

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
		if (!p->pending && ((groupmatch && (p->group & groupmatch)) || !strcmp(data, p->agent)) &&
		    ast_strlen_zero(p->loginchan)) {
			if (p->chan)
				hasagent++;
			tv = ast_tvnow();
			if (!p->lastdisc.tv_sec || (tv.tv_sec >= p->lastdisc.tv_sec)) {
				p->lastdisc = ast_tv(0, 0);
				/* Agent must be registered, but not have any active call, and not be in a waiting state */
				if (!p->owner && p->chan) {
					/* Fixed agent */
					chan = agent_new(p, AST_STATE_DOWN);
				}
				if (chan) {
					ast_mutex_unlock(&p->lock);
					break;
				}
			}
		}
		ast_mutex_unlock(&p->lock);
	}
	if (!p) {
		AST_LIST_TRAVERSE(&agents, p, list) {
			ast_mutex_lock(&p->lock);
			if (!p->pending && ((groupmatch && (p->group & groupmatch)) || !strcmp(data, p->agent))) {
				if (p->chan || !ast_strlen_zero(p->loginchan))
					hasagent++;
				tv = ast_tvnow();
#if 0
				ast_log(LOG_NOTICE, "Time now: %ld, Time of lastdisc: %ld\n", tv.tv_sec, p->lastdisc.tv_sec);
#endif
				if (!p->lastdisc.tv_sec || (tv.tv_sec >= p->lastdisc.tv_sec)) {
					p->lastdisc = ast_tv(0, 0);
					/* Agent must be registered, but not have any active call, and not be in a waiting state */
					if (!p->owner && p->chan) {
						/* Could still get a fixed agent */
						chan = agent_new(p, AST_STATE_DOWN);
					} else if (!p->owner && !ast_strlen_zero(p->loginchan)) {
						/* Adjustable agent */
						p->chan = ast_request("Local", format, p->loginchan, cause);
						if (p->chan)
							chan = agent_new(p, AST_STATE_DOWN);
					}
					if (chan) {
						ast_mutex_unlock(&p->lock);
						break;
					}
				}
			}
			ast_mutex_unlock(&p->lock);
		}
	}

	if (!chan && waitforagent) {
		/* No agent available -- but we're requesting to wait for one.
		   Allocate a place holder */
		if (hasagent) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Creating place holder for '%s'\n", s);
			p = add_agent(data, 1);
			p->group = groupmatch;
			chan = agent_new(p, AST_STATE_DOWN);
			if (!chan) 
				ast_log(LOG_WARNING, "Weird...  Fix this to drop the unused pending agent\n");
		} else
			ast_log(LOG_DEBUG, "Not creating place holder for '%s' since nobody logged in\n", s);
	}
	*cause = hasagent ? AST_CAUSE_BUSY : AST_CAUSE_UNREGISTERED;
	AST_LIST_UNLOCK(&agents);
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
 * \param s
 * \param m
 * \returns 
 * \sa action_agent_logoff(), action_agent_callback_login(), load_module().
 */
static int action_agents(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	char chanbuf[256];
	struct agent_pvt *p;
	char *username = NULL;
	char *loginChan = NULL;
	char *talkingtoChan = NULL;
	char *status = NULL;

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);
	astman_send_ack(s, m, "Agents will follow");
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
        	ast_mutex_lock(&p->lock);

		/* Status Values:
		   AGENT_LOGGEDOFF - Agent isn't logged in
		   AGENT_IDLE      - Agent is logged in, and waiting for call
		   AGENT_ONCALL    - Agent is logged in, and on a call
		   AGENT_UNKNOWN   - Don't know anything about agent. Shouldn't ever get this. */

		username = S_OR(p->name, "None");

		/* Set a default status. It 'should' get changed. */
		status = "AGENT_UNKNOWN";

		if (!ast_strlen_zero(p->loginchan) && !p->chan) {
			loginChan = p->loginchan;
			talkingtoChan = "n/a";
			status = "AGENT_IDLE";
			if (p->acknowledged) {
				snprintf(chanbuf, sizeof(chanbuf), " %s (Confirmed)", p->loginchan);
				loginChan = chanbuf;
			}
		} else if (p->chan) {
			loginChan = ast_strdupa(p->chan->name);
			if (p->owner && p->owner->_bridge) {
				if (ast_bridged_channel(p->owner)) {
					talkingtoChan = ast_strdupa(S_OR(ast_bridged_channel(p->owner)->cid.cid_num, ""));
				} else {
					talkingtoChan = "n/a";
				}
        			status = "AGENT_ONCALL";
			} else {
        			talkingtoChan = "n/a";
        			status = "AGENT_IDLE";
			}
		} else {
			loginChan = "n/a";
			talkingtoChan = "n/a";
			status = "AGENT_LOGGEDOFF";
		}

		astman_append(s, "Event: Agents\r\n"
			"Agent: %s\r\n"
			"Name: %s\r\n"
			"Status: %s\r\n"
			"LoggedInChan: %s\r\n"
			"LoggedInTime: %d\r\n"
			"TalkingTo: %s\r\n"
			"%s"
			"\r\n",
			p->agent, username, status, loginChan, (int)p->loginstart, talkingtoChan, idText);
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	astman_append(s, "Event: AgentsComplete\r\n"
		"%s"
		"\r\n",idText);
	return 0;
}

static void agent_logoff_maintenance(struct agent_pvt *p, char *loginchan, long logintime, const char *uniqueid, char *logcommand)
{
	char *tmp = NULL;
	char agent[AST_MAX_AGENT];

	if (!ast_strlen_zero(logcommand))
		tmp = logcommand;
	else
		tmp = ast_strdupa("");

	snprintf(agent, sizeof(agent), "Agent/%s", p->agent);

	if (!ast_strlen_zero(uniqueid)) {
		manager_event(EVENT_FLAG_AGENT, "Agentcallbacklogoff",
				"Agent: %s\r\n"
				"Reason: %s\r\n"
				"Loginchan: %s\r\n"
				"Logintime: %ld\r\n"
				"Uniqueid: %s\r\n", 
				p->agent, tmp, loginchan, logintime, uniqueid);
	} else {
		manager_event(EVENT_FLAG_AGENT, "Agentcallbacklogoff",
				"Agent: %s\r\n"
				"Reason: %s\r\n"
				"Loginchan: %s\r\n"
				"Logintime: %ld\r\n",
				p->agent, tmp, loginchan, logintime);
	}

	ast_queue_log("NONE", ast_strlen_zero(uniqueid) ? "NONE" : uniqueid, agent, "AGENTCALLBACKLOGOFF", "%s|%ld|%s", loginchan, logintime, tmp);
	set_agentbycallerid(p->logincallerid, NULL);
	p->loginchan[0] ='\0';
	p->logincallerid[0] = '\0';
	p->inherited_devicestate = -1;
	ast_device_state_changed("Agent/%s", p->agent);
	if (persistent_agents)
		dump_agents();

}

static int agent_logoff(const char *agent, int soft)
{
	struct agent_pvt *p;
	long logintime;
	int ret = -1; /* Return -1 if no agent if found */

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (!strcasecmp(p->agent, agent)) {
			ret = 0;
			if (p->owner || p->chan) {
				if (!soft) {
					ast_mutex_lock(&p->lock);

					while (p->owner && ast_channel_trylock(p->owner)) {
						DEADLOCK_AVOIDANCE(&p->lock);
					}
					if (p->owner) {
						ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
						ast_channel_unlock(p->owner);
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
			} else {
				logintime = time(NULL) - p->loginstart;
				p->loginstart = 0;
				agent_logoff_maintenance(p, p->loginchan, logintime, NULL, "CommandLogoff");
			}
			break;
		}
	}
	AST_LIST_UNLOCK(&agents);

	return ret;
}

static int agent_logoff_cmd(int fd, int argc, char **argv)
{
	int ret;
	char *agent;

	if (argc < 3 || argc > 4)
		return RESULT_SHOWUSAGE;
	if (argc == 4 && strcasecmp(argv[3], "soft"))
		return RESULT_SHOWUSAGE;

	agent = argv[2] + 6;
	ret = agent_logoff(agent, argc == 4);
	if (ret == 0)
		ast_cli(fd, "Logging out %s\n", agent);

	return RESULT_SUCCESS;
}

/*!
 * Sets an agent as no longer logged in in the Manager API.
 * It is registered on load_module() and it gets called by the manager backend.
 * \param s
 * \param m
 * \returns 
 * \sa action_agents(), action_agent_callback_login(), load_module().
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
			if (!strncasecmp(word, name, len) && ++which > state) {
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
static int agents_show(int fd, int argc, char **argv)
{
	struct agent_pvt *p;
	char username[AST_MAX_BUF];
	char location[AST_MAX_BUF] = "";
	char talkingto[AST_MAX_BUF] = "";
	char moh[AST_MAX_BUF];
	int count_agents = 0;		/*!< Number of agents configured */
	int online_agents = 0;		/*!< Number of online agents */
	int offline_agents = 0;		/*!< Number of offline agents */
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		ast_mutex_lock(&p->lock);
		if (p->pending) {
			if (p->group)
				ast_cli(fd, "-- Pending call to group %d\n", powerof(p->group));
			else
				ast_cli(fd, "-- Pending call to agent %s\n", p->agent);
		} else {
			if (!ast_strlen_zero(p->name))
				snprintf(username, sizeof(username), "(%s) ", p->name);
			else
				username[0] = '\0';
			if (p->chan) {
				snprintf(location, sizeof(location), "logged in on %s", p->chan->name);
				if (p->owner && ast_bridged_channel(p->owner))
					snprintf(talkingto, sizeof(talkingto), " talking to %s", ast_bridged_channel(p->owner)->name);
				 else 
					strcpy(talkingto, " is idle");
				online_agents++;
			} else if (!ast_strlen_zero(p->loginchan)) {
				if (ast_tvdiff_ms(ast_tvnow(), p->lastdisc) > 0 || !(p->lastdisc.tv_sec)) 
					snprintf(location, sizeof(location) - 20, "available at '%s'", p->loginchan);
				else 
					snprintf(location, sizeof(location) - 20, "wrapping up at '%s'", p->loginchan);
				talkingto[0] = '\0';
				online_agents++;
				if (p->acknowledged)
					strncat(location, " (Confirmed)", sizeof(location) - strlen(location) - 1);
			} else {
				strcpy(location, "not logged in");
				talkingto[0] = '\0';
				offline_agents++;
			}
			if (!ast_strlen_zero(p->moh))
				snprintf(moh, sizeof(moh), " (musiconhold is '%s')", p->moh);
			ast_cli(fd, "%-12.12s %s%s%s%s\n", p->agent, 
				username, location, talkingto, moh);
			count_agents++;
		}
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	if ( !count_agents ) 
		ast_cli(fd, "No Agents are configured in %s\n",config);
	else 
		ast_cli(fd, "%d agents configured [%d online , %d offline]\n",count_agents, online_agents, offline_agents);
	ast_cli(fd, "\n");
	                
	return RESULT_SUCCESS;
}


static int agents_show_online(int fd, int argc, char **argv)
{
	struct agent_pvt *p;
	char username[AST_MAX_BUF];
	char location[AST_MAX_BUF] = "";
	char talkingto[AST_MAX_BUF] = "";
	char moh[AST_MAX_BUF];
	int count_agents = 0;           /* Number of agents configured */
	int online_agents = 0;          /* Number of online agents */
	int agent_status = 0;           /* 0 means offline, 1 means online */
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		agent_status = 0;       /* reset it to offline */
		ast_mutex_lock(&p->lock);
		if (!ast_strlen_zero(p->name))
			snprintf(username, sizeof(username), "(%s) ", p->name);
		else
			username[0] = '\0';
		if (p->chan) {
			snprintf(location, sizeof(location), "logged in on %s", p->chan->name);
			if (p->owner && ast_bridged_channel(p->owner)) 
				snprintf(talkingto, sizeof(talkingto), " talking to %s", ast_bridged_channel(p->owner)->name);
			else 
				strcpy(talkingto, " is idle");
			agent_status = 1;
			online_agents++;
		} else if (!ast_strlen_zero(p->loginchan)) {
			snprintf(location, sizeof(location) - 20, "available at '%s'", p->loginchan);
			talkingto[0] = '\0';
			agent_status = 1;
			online_agents++;
			if (p->acknowledged)
				strncat(location, " (Confirmed)", sizeof(location) - strlen(location) - 1);
		}
		if (!ast_strlen_zero(p->moh))
			snprintf(moh, sizeof(moh), " (musiconhold is '%s')", p->moh);
		if (agent_status)
			ast_cli(fd, "%-12.12s %s%s%s%s\n", p->agent, username, location, talkingto, moh);
		count_agents++;
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&agents);
	if (!count_agents) 
		ast_cli(fd, "No Agents are configured in %s\n", config);
	else
		ast_cli(fd, "%d agents online\n", online_agents);
	ast_cli(fd, "\n");
	return RESULT_SUCCESS;
}



static char show_agents_usage[] = 
"Usage: agent show\n"
"       Provides summary information on agents.\n";

static char show_agents_online_usage[] =
"Usage: agent show online\n"
"	Provides a list of all online agents.\n";

static char agent_logoff_usage[] =
"Usage: agent logoff <channel> [soft]\n"
"       Sets an agent as no longer logged in.\n"
"       If 'soft' is specified, do not hangup existing calls.\n";

static struct ast_cli_entry cli_show_agents_deprecated = {
	{ "show", "agents", NULL },
	agents_show, NULL,
	NULL, NULL };

static struct ast_cli_entry cli_show_agents_online_deprecated = {
	{ "show", "agents", "online" },
	agents_show_online, NULL,
	NULL, NULL };

static struct ast_cli_entry cli_agents[] = {
	{ { "agent", "show", NULL },
	agents_show, "Show status of agents",
	show_agents_usage, NULL, &cli_show_agents_deprecated },

	{ { "agent", "show", "online" },
	agents_show_online, "Show all online agents",
	show_agents_online_usage, NULL, &cli_show_agents_online_deprecated },

	{ { "agent", "logoff", NULL },
	agent_logoff_cmd, "Sets an agent offline",
	agent_logoff_usage, complete_agent_logoff_cmd },
};

/*!
 * \brief Log in agent application.
 *
 * \param chan
 * \param data
 * \param callbackmode non-zero for AgentCallbackLogin
 */
static int __login_exec(struct ast_channel *chan, void *data, int callbackmode)
{
	int res=0;
	int tries = 0;
	int max_login_tries = maxlogintries;
	struct agent_pvt *p;
	struct ast_module_user *u;
	int login_state = 0;
	char user[AST_MAX_AGENT] = "";
	char pass[AST_MAX_AGENT];
	char agent[AST_MAX_AGENT] = "";
	char xpass[AST_MAX_AGENT] = "";
	char *errmsg;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(agent_id);
			     AST_APP_ARG(options);
			     AST_APP_ARG(extension);
		);
	const char *tmpoptions = NULL;
	char *context = NULL;
	int play_announcement = 1;
	char agent_goodbye[AST_MAX_FILENAME_LEN];
	int update_cdr = updatecdr;
	char *filename = "agent-loginok";
	char tmpchan[AST_MAX_BUF] = "";

	u = ast_module_user_add(chan);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	ast_copy_string(agent_goodbye, agentgoodbye, sizeof(agent_goodbye));

	ast_channel_lock(chan);
	/* Set Channel Specific Login Overrides */
	if (pbx_builtin_getvar_helper(chan, "AGENTLMAXLOGINTRIES") && strlen(pbx_builtin_getvar_helper(chan, "AGENTLMAXLOGINTRIES"))) {
		max_login_tries = atoi(pbx_builtin_getvar_helper(chan, "AGENTMAXLOGINTRIES"));
		if (max_login_tries < 0)
			max_login_tries = 0;
		tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTMAXLOGINTRIES");
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Saw variable AGENTMAXLOGINTRIES=%s, setting max_login_tries to: %d on Channel '%s'.\n",tmpoptions,max_login_tries,chan->name);
	}
	if (pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR") && !ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR"))) {
		if (ast_true(pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR")))
			update_cdr = 1;
		else
			update_cdr = 0;
		tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTUPDATECDR");
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Saw variable AGENTUPDATECDR=%s, setting update_cdr to: %d on Channel '%s'.\n",tmpoptions,update_cdr,chan->name);
	}
	if (pbx_builtin_getvar_helper(chan, "AGENTGOODBYE") && !ast_strlen_zero(pbx_builtin_getvar_helper(chan, "AGENTGOODBYE"))) {
		strcpy(agent_goodbye, pbx_builtin_getvar_helper(chan, "AGENTGOODBYE"));
		tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTGOODBYE");
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Saw variable AGENTGOODBYE=%s, setting agent_goodbye to: %s on Channel '%s'.\n",tmpoptions,agent_goodbye,chan->name);
	}
	ast_channel_unlock(chan);
	/* End Channel Specific Login Overrides */
	
	if (callbackmode && args.extension) {
		parse = args.extension;
		args.extension = strsep(&parse, "@");
		context = parse;
	}

	if (!ast_strlen_zero(args.options)) {
		if (strchr(args.options, 's')) {
			play_announcement = 0;
		}
	}

	if (chan->_state != AST_STATE_UP)
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
				login_state = 1; /* Successful Login */

				/* Ensure we can't be gotten until we're done */
				gettimeofday(&p->lastdisc, NULL);
				p->lastdisc.tv_sec++;

				/* Set Channel Specific Agent Overrides */
				if (pbx_builtin_getvar_helper(chan, "AGENTACKCALL") && strlen(pbx_builtin_getvar_helper(chan, "AGENTACKCALL"))) {
					if (!strcasecmp(pbx_builtin_getvar_helper(chan, "AGENTACKCALL"), "always"))
						p->ackcall = 2;
					else if (ast_true(pbx_builtin_getvar_helper(chan, "AGENTACKCALL")))
						p->ackcall = 1;
					else
						p->ackcall = 0;
					tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTACKCALL");
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Saw variable AGENTACKCALL=%s, setting ackcall to: %d for Agent '%s'.\n",tmpoptions,p->ackcall,p->agent);
				} else {
					p->ackcall = ackcall;
				}
				if (pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF") && strlen(pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF"))) {
					p->autologoff = atoi(pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF"));
					if (p->autologoff < 0)
						p->autologoff = 0;
					tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTAUTOLOGOFF");
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Saw variable AGENTAUTOLOGOFF=%s, setting autologff to: %d for Agent '%s'.\n",tmpoptions,p->autologoff,p->agent);
				} else {
					p->autologoff = autologoff;
				}
				if (pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME") && strlen(pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME"))) {
					p->wrapuptime = atoi(pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME"));
					if (p->wrapuptime < 0)
						p->wrapuptime = 0;
					tmpoptions=pbx_builtin_getvar_helper(chan, "AGENTWRAPUPTIME");
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Saw variable AGENTWRAPUPTIME=%s, setting wrapuptime to: %d for Agent '%s'.\n",tmpoptions,p->wrapuptime,p->agent);
				} else {
					p->wrapuptime = wrapuptime;
				}
				ast_channel_unlock(chan);
				unlock_channel = 0;
				/* End Channel Specific Agent Overrides */
				if (!p->chan) {
					char last_loginchan[80] = "";
					long logintime;
					snprintf(agent, sizeof(agent), "Agent/%s", p->agent);

					if (callbackmode) {
						int pos = 0;
						/* Retrieve login chan */
						for (;;) {
							if (!ast_strlen_zero(args.extension)) {
								ast_copy_string(tmpchan, args.extension, sizeof(tmpchan));
								res = 0;
							} else
								res = ast_app_getdata(chan, "agent-newlocation", tmpchan+pos, sizeof(tmpchan) - 2, 0);
							if (ast_strlen_zero(tmpchan) )
								break;
							if(ast_exists_extension(chan, S_OR(context,"default"), tmpchan,1, NULL) ) {
								if(!allow_multiple_login(tmpchan,context) ) {
									args.extension = NULL;
									pos = 0;
								} else
									break;
							}
							if (args.extension) {
								ast_log(LOG_WARNING, "Extension '%s' is not valid for automatic login of agent '%s'\n", args.extension, p->agent);
								args.extension = NULL;
								pos = 0;
							} else {
								ast_log(LOG_WARNING, "Extension '%s@%s' is not valid for automatic login of agent '%s'\n", tmpchan, S_OR(context, "default"), p->agent);
								res = ast_streamfile(chan, "invalid", chan->language);
								if (!res)
									res = ast_waitstream(chan, AST_DIGIT_ANY);
								if (res > 0) {
									tmpchan[0] = res;
									tmpchan[1] = '\0';
									pos = 1;
								} else {
									tmpchan[0] = '\0';
									pos = 0;
								}
							}
						}
						args.extension = tmpchan;
						if (!res) {
							set_agentbycallerid(p->logincallerid, NULL);
							if (!ast_strlen_zero(context) && !ast_strlen_zero(tmpchan))
								snprintf(p->loginchan, sizeof(p->loginchan), "%s@%s", tmpchan, context);
							else {
								ast_copy_string(last_loginchan, p->loginchan, sizeof(last_loginchan));
								ast_copy_string(p->loginchan, tmpchan, sizeof(p->loginchan));
							}
							p->acknowledged = 0;
							if (ast_strlen_zero(p->loginchan)) {
								login_state = 2;
								filename = "agent-loggedoff";
							} else {
								if (chan->cid.cid_num) {
									ast_copy_string(p->logincallerid, chan->cid.cid_num, sizeof(p->logincallerid));
									set_agentbycallerid(p->logincallerid, p->agent);
								} else
									p->logincallerid[0] = '\0';
							}

							if(update_cdr && chan->cdr)
								snprintf(chan->cdr->channel, sizeof(chan->cdr->channel), "Agent/%s", p->agent);

						}
					} else {
						p->loginchan[0] = '\0';
						p->logincallerid[0] = '\0';
						p->acknowledged = 0;
					}
					ast_mutex_unlock(&p->lock);
					AST_LIST_UNLOCK(&agents);
					if( !res && play_announcement==1 )
						res = ast_streamfile(chan, filename, chan->language);
					if (!res)
						ast_waitstream(chan, "");
					AST_LIST_LOCK(&agents);
					ast_mutex_lock(&p->lock);
					if (!res) {
						res = ast_set_read_format(chan, ast_best_codec(chan->nativeformats));
						if (res)
							ast_log(LOG_WARNING, "Unable to set read format to %d\n", ast_best_codec(chan->nativeformats));
					}
					if (!res) {
						res = ast_set_write_format(chan, ast_best_codec(chan->nativeformats));
						if (res)
							ast_log(LOG_WARNING, "Unable to set write format to %d\n", ast_best_codec(chan->nativeformats));
					}
					/* Check once more just in case */
					if (p->chan)
						res = -1;
					if (callbackmode && !res) {
						/* Just say goodbye and be done with it */
						if (!ast_strlen_zero(p->loginchan)) {
							if (p->loginstart == 0)
								time(&p->loginstart);
							manager_event(EVENT_FLAG_AGENT, "Agentcallbacklogin",
								      "Agent: %s\r\n"
								      "Loginchan: %s\r\n"
								      "Uniqueid: %s\r\n",
								      p->agent, p->loginchan, chan->uniqueid);
							ast_queue_log("NONE", chan->uniqueid, agent, "AGENTCALLBACKLOGIN", "%s", p->loginchan);
							if (option_verbose > 1)
								ast_verbose(VERBOSE_PREFIX_2 "Callback Agent '%s' logged in on %s\n", p->agent, p->loginchan);
							ast_device_state_changed("Agent/%s", p->agent);
							if (persistent_agents)
								dump_agents();
						} else {
							logintime = time(NULL) - p->loginstart;
							p->loginstart = 0;

							agent_logoff_maintenance(p, last_loginchan, logintime, chan->uniqueid, NULL);
							if (option_verbose > 1)
								ast_verbose(VERBOSE_PREFIX_2 "Callback Agent '%s' logged out\n", p->agent);
						}
						AST_LIST_UNLOCK(&agents);
						if (!res)
							res = ast_safe_sleep(chan, 500);
						ast_mutex_unlock(&p->lock);
					} else if (!res) {
						ast_indicate_data(chan, AST_CONTROL_HOLD, 
							S_OR(p->moh, NULL), 
							!ast_strlen_zero(p->moh) ? strlen(p->moh) + 1 : 0);
						if (p->loginstart == 0)
							time(&p->loginstart);
						manager_event(EVENT_FLAG_AGENT, "Agentlogin",
							      "Agent: %s\r\n"
							      "Channel: %s\r\n"
							      "Uniqueid: %s\r\n",
							      p->agent, chan->name, chan->uniqueid);
						if (update_cdr && chan->cdr)
							snprintf(chan->cdr->channel, sizeof(chan->cdr->channel), "Agent/%s", p->agent);
						ast_queue_log("NONE", chan->uniqueid, agent, "AGENTLOGIN", "%s", chan->name);
						if (option_verbose > 1)
							ast_verbose(VERBOSE_PREFIX_2 "Agent '%s' logged in (format %s/%s)\n", p->agent,
								    ast_getformatname(chan->readformat), ast_getformatname(chan->writeformat));
						/* Login this channel and wait for it to go away */
						p->chan = chan;
						if (p->ackcall > 1)
							check_beep(p, 0);
						else
							check_availability(p, 0);
						ast_mutex_unlock(&p->lock);
						AST_LIST_UNLOCK(&agents);
						ast_device_state_changed("Agent/%s", p->agent);
						while (res >= 0) {
							ast_mutex_lock(&p->lock);
							if (p->deferlogoff && p->chan) {
								ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
								p->deferlogoff = 0;
							}
							if (p->chan != chan)
								res = -1;
							ast_mutex_unlock(&p->lock);
							/* Yield here so other interested threads can kick in. */
							sched_yield();
							if (res)
								break;

							AST_LIST_LOCK(&agents);
							ast_mutex_lock(&p->lock);
							if (p->lastdisc.tv_sec) {
								if (ast_tvdiff_ms(ast_tvnow(), p->lastdisc) > 0) {
									if (option_debug)
										ast_log(LOG_DEBUG, "Wrapup time for %s expired!\n", p->agent);
									p->lastdisc = ast_tv(0, 0);
									ast_device_state_changed("Agent/%s", p->agent);
									if (p->ackcall > 1)
										check_beep(p, 0);
									else
										check_availability(p, 0);
								}
							}
							ast_mutex_unlock(&p->lock);
							AST_LIST_UNLOCK(&agents);
							/*	Synchronize channel ownership between call to agent and itself. */
							ast_mutex_lock(&p->app_lock);
							if (p->app_lock_flag == 1) {
								ast_cond_wait(&p->app_complete_cond, &p->app_lock);
							}
							ast_mutex_unlock(&p->app_lock);
							ast_mutex_lock(&p->lock);
							ast_mutex_unlock(&p->lock);
							if (p->ackcall > 1) 
								res = agent_ack_sleep(p);
							else
								res = ast_safe_sleep_conditional( chan, 1000, agent_cont_sleep, p );
							if ((p->ackcall > 1)  && (res == 1)) {
								AST_LIST_LOCK(&agents);
								ast_mutex_lock(&p->lock);
								check_availability(p, 0);
								ast_mutex_unlock(&p->lock);
								AST_LIST_UNLOCK(&agents);
								res = 0;
							}
							sched_yield();
						}
						ast_mutex_lock(&p->lock);
						if (res && p->owner) 
							ast_log(LOG_WARNING, "Huh?  We broke out when there was still an owner?\n");
						/* Log us off if appropriate */
						if (p->chan == chan) {
							p->chan = NULL;
							p->inherited_devicestate = -1;
						}
						p->acknowledged = 0;
						logintime = time(NULL) - p->loginstart;
						p->loginstart = 0;
						ast_mutex_unlock(&p->lock);
						manager_event(EVENT_FLAG_AGENT, "Agentlogoff",
							      "Agent: %s\r\n"
							      "Logintime: %ld\r\n"
							      "Uniqueid: %s\r\n",
							      p->agent, logintime, chan->uniqueid);
						ast_queue_log("NONE", chan->uniqueid, agent, "AGENTLOGOFF", "%s|%ld", chan->name, logintime);
						if (option_verbose > 1)
							ast_verbose(VERBOSE_PREFIX_2 "Agent '%s' logged out\n", p->agent);
						/* If there is no owner, go ahead and kill it now */
						ast_device_state_changed("Agent/%s", p->agent);
						if (p->dead && !p->owner) {
							ast_mutex_destroy(&p->lock);
							ast_mutex_destroy(&p->app_lock);
							ast_cond_destroy(&p->app_complete_cond);
							free(p);
						}
					}
					else {
						ast_mutex_unlock(&p->lock);
						p = NULL;
					}
					res = -1;
				} else {
					ast_mutex_unlock(&p->lock);
					errmsg = "agent-alreadyon";
					p = NULL;
				}
				break;
			}
			ast_mutex_unlock(&p->lock);
			if (unlock_channel) {
				ast_channel_unlock(chan);
			}
		}
		if (!p)
			AST_LIST_UNLOCK(&agents);

		if (!res && (max_login_tries==0 || tries < max_login_tries))
			res = ast_app_getdata(chan, errmsg, user, sizeof(user) - 1, 0);
	}
		
	if (!res)
		res = ast_safe_sleep(chan, 500);

	/* AgentLogin() exit */
	if (!callbackmode) {
		ast_module_user_remove(u);
		return -1;
	} else { /* AgentCallbackLogin() exit*/
		/* Set variables */
		if (login_state > 0) {
			pbx_builtin_setvar_helper(chan, "AGENTNUMBER", user);
			if (login_state==1) {
				pbx_builtin_setvar_helper(chan, "AGENTSTATUS", "on");
				pbx_builtin_setvar_helper(chan, "AGENTEXTEN", args.extension);
			} else 
				pbx_builtin_setvar_helper(chan, "AGENTSTATUS", "off");
		} else {
			pbx_builtin_setvar_helper(chan, "AGENTSTATUS", "fail");
		}
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 1, chan->cid.cid_num)) {
			ast_module_user_remove(u);
			return 0;
		}
		/* Do we need to play agent-goodbye now that we will be hanging up? */
		if (play_announcement) {
			if (!res)
				res = ast_safe_sleep(chan, 1000);
			res = ast_streamfile(chan, agent_goodbye, chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			if (!res)
				res = ast_safe_sleep(chan, 1000);
		}
	}

	ast_module_user_remove(u);
	
	/* We should never get here if next priority exists when in callbackmode */
 	return -1;
}

/*!
 * Called by the AgentLogin application (from the dial plan).
 * 
 * \param chan
 * \param data
 * \returns
 * \sa callback_login_exec(), agentmonitoroutgoing_exec(), load_module().
 */
static int login_exec(struct ast_channel *chan, void *data)
{
	return __login_exec(chan, data, 0);
}

static void callback_deprecated(void)
{
	static int depwarning = 0;

	if (!depwarning) {
		depwarning = 1;

		ast_log(LOG_WARNING, "AgentCallbackLogin is deprecated and will be removed in a future release.\n");
		ast_log(LOG_WARNING, "See doc/queues-with-callback-members.txt for an example of how to achieve\n");
		ast_log(LOG_WARNING, "the same functionality using only dialplan logic.\n");
	}
}

/*!
 *  Called by the AgentCallbackLogin application (from the dial plan).
 * 
 * \param chan
 * \param data
 * \returns
 * \sa login_exec(), agentmonitoroutgoing_exec(), load_module().
 */
static int callback_exec(struct ast_channel *chan, void *data)
{
	callback_deprecated();

	return __login_exec(chan, data, 1);
}

/*!
 * Sets an agent as logged in by callback in the Manager API.
 * It is registered on load_module() and it gets called by the manager backend.
 * \param s
 * \param m
 * \returns 
 * \sa action_agents(), action_agent_logoff(), load_module().
 */
static int action_agent_callback_login(struct mansession *s, const struct message *m)
{
	const char *agent = astman_get_header(m, "Agent");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	const char *wrapuptime_s = astman_get_header(m, "WrapupTime");
	const char *ackcall_s = astman_get_header(m, "AckCall");
	struct agent_pvt *p;
	int login_state = 0;

	callback_deprecated();

	if (ast_strlen_zero(agent)) {
		astman_send_error(s, m, "No agent specified");
		return 0;
	}

	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "No extension specified");
		return 0;
	}

	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		if (strcmp(p->agent, agent) || p->pending) 
			continue;
		if (p->chan) {
			login_state = 2; /* already logged in (and on the phone)*/
			break;
		}
		ast_mutex_lock(&p->lock);
		login_state = 1; /* Successful Login */
		
		if (ast_strlen_zero(context))
			ast_copy_string(p->loginchan, exten, sizeof(p->loginchan));
		else
			snprintf(p->loginchan, sizeof(p->loginchan), "%s@%s", exten, context);

		if (!ast_strlen_zero(wrapuptime_s)) {
			p->wrapuptime = atoi(wrapuptime_s);
			if (p->wrapuptime < 0)
				p->wrapuptime = 0;
		}

		if (!strcasecmp(ackcall_s, "always"))
			p->ackcall = 2;
		else if (ast_true(ackcall_s))
			p->ackcall = 1;
		else
			p->ackcall = 0;

		if (p->loginstart == 0)
			time(&p->loginstart);
		manager_event(EVENT_FLAG_AGENT, "Agentcallbacklogin",
			      "Agent: %s\r\n"
			      "Loginchan: %s\r\n",
			      p->agent, p->loginchan);
		ast_queue_log("NONE", "NONE", agent, "AGENTCALLBACKLOGIN", "%s", p->loginchan);
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Callback Agent '%s' logged in on %s\n", p->agent, p->loginchan);
		ast_device_state_changed("Agent/%s", p->agent);
		ast_mutex_unlock(&p->lock);
		if (persistent_agents)
			dump_agents();
	}
	AST_LIST_UNLOCK(&agents);

	if (login_state == 1)
		astman_send_ack(s, m, "Agent logged in");
	else if (login_state == 0)
		astman_send_error(s, m, "No such agent");
	else if (login_state == 2)
		astman_send_error(s, m, "Agent already logged in");

	return 0;
}

/*!
 *  \brief Called by the AgentMonitorOutgoing application (from the dial plan).
 *
 * \param chan
 * \param data
 * \returns
 * \sa login_exec(), callback_login_exec(), load_module().
 */
static int agentmonitoroutgoing_exec(struct ast_channel *chan, void *data)
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
	if (chan->cid.cid_num) {
		const char *tmp;
		char agentvar[AST_MAX_BUF];
		snprintf(agentvar, sizeof(agentvar), "%s_%s", GETAGENTBYCALLERID, chan->cid.cid_num);
		if ((tmp = pbx_builtin_getvar_helper(NULL, agentvar))) {
			struct agent_pvt *p;
			ast_copy_string(agent, tmp, sizeof(agent));
			AST_LIST_LOCK(&agents);
			AST_LIST_TRAVERSE(&agents, p, list) {
				if (!strcasecmp(p->agent, tmp)) {
					if (changeoutgoing) snprintf(chan->cdr->channel, sizeof(chan->cdr->channel), "Agent/%s", p->agent);
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
	/* check if there is n + 101 priority */
	/*! \todo XXX Needs to check option priorityjump etc etc */
	if (res) {
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num)) {
			chan->priority+=100;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Going to %d priority because there is no callerid or the agentid cannot be found.\n",chan->priority);
		} else if (exitifnoagentid)
			return res;
	}
	return 0;
}

/*!
 * \brief Dump AgentCallbackLogin agents to the ASTdb database for persistence
 */
static void dump_agents(void)
{
	struct agent_pvt *cur_agent = NULL;
	char buf[256];

	AST_LIST_TRAVERSE(&agents, cur_agent, list) {
		if (cur_agent->chan)
			continue;

		if (!ast_strlen_zero(cur_agent->loginchan)) {
			snprintf(buf, sizeof(buf), "%s;%s", cur_agent->loginchan, cur_agent->logincallerid);
			if (ast_db_put(pa_family, cur_agent->agent, buf))
				ast_log(LOG_WARNING, "failed to create persistent entry in ASTdb for %s!\n", buf);
			else if (option_debug)
				ast_log(LOG_DEBUG, "Saved Agent: %s on %s\n", cur_agent->agent, cur_agent->loginchan);
		} else {
			/* Delete -  no agent or there is an error */
			ast_db_del(pa_family, cur_agent->agent);
		}
	}
}

/*!
 * \brief Reload the persistent agents from astdb.
 */
static void reload_agents(void)
{
	char *agent_num;
	struct ast_db_entry *db_tree;
	struct ast_db_entry *entry;
	struct agent_pvt *cur_agent;
	char agent_data[256];
	char *parse;
	char *agent_chan;
	char *agent_callerid;

	db_tree = ast_db_gettree(pa_family, NULL);

	AST_LIST_LOCK(&agents);
	for (entry = db_tree; entry; entry = entry->next) {
		agent_num = entry->key + strlen(pa_family) + 2;
		AST_LIST_TRAVERSE(&agents, cur_agent, list) {
			ast_mutex_lock(&cur_agent->lock);
			if (strcmp(agent_num, cur_agent->agent) == 0)
				break;
			ast_mutex_unlock(&cur_agent->lock);
		}
		if (!cur_agent) {
			ast_db_del(pa_family, agent_num);
			continue;
		} else
			ast_mutex_unlock(&cur_agent->lock);
		if (!ast_db_get(pa_family, agent_num, agent_data, sizeof(agent_data)-1)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Reload Agent from AstDB: %s on %s\n", cur_agent->agent, agent_data);
			parse = agent_data;
			agent_chan = strsep(&parse, ";");
			agent_callerid = strsep(&parse, ";");
			ast_copy_string(cur_agent->loginchan, agent_chan, sizeof(cur_agent->loginchan));
			if (agent_callerid) {
				ast_copy_string(cur_agent->logincallerid, agent_callerid, sizeof(cur_agent->logincallerid));
				set_agentbycallerid(cur_agent->logincallerid, cur_agent->agent);
			} else
				cur_agent->logincallerid[0] = '\0';
			if (cur_agent->loginstart == 0)
				time(&cur_agent->loginstart);
			ast_device_state_changed("Agent/%s", cur_agent->agent);	
		}
	}
	AST_LIST_UNLOCK(&agents);
	if (db_tree) {
		ast_log(LOG_NOTICE, "Agents successfully reloaded from database.\n");
		ast_db_freetree(db_tree);
	}
}

/*! \brief Part of PBX channel interface */
static int agent_devicestate(void *data)
{
	struct agent_pvt *p;
	char *s;
	ast_group_t groupmatch;
	int groupoff;
	int waitforagent=0;
	int res = AST_DEVICE_INVALID;
	
	s = data;
	if ((s[0] == '@') && (sscanf(s + 1, "%30d", &groupoff) == 1))
		groupmatch = (1 << groupoff);
	else if ((s[0] == ':') && (sscanf(s + 1, "%30d", &groupoff) == 1)) {
		groupmatch = (1 << groupoff);
		waitforagent = 1;
	} else 
		groupmatch = 0;

	/* Check actual logged in agents first */
	AST_LIST_LOCK(&agents);
	AST_LIST_TRAVERSE(&agents, p, list) {
		ast_mutex_lock(&p->lock);
		if (!p->pending && ((groupmatch && (p->group & groupmatch)) || !strcmp(data, p->agent))) {
			if (p->owner) {
				if (res != AST_DEVICE_INUSE)
					res = AST_DEVICE_BUSY;
			} else if (p->inherited_devicestate > -1) {
				res = p->inherited_devicestate;
			} else {
				if (res == AST_DEVICE_BUSY)
					res = AST_DEVICE_INUSE;
				if (p->chan || !ast_strlen_zero(p->loginchan)) {
					if (res == AST_DEVICE_INVALID)
						res = AST_DEVICE_UNKNOWN;
				} else if (res == AST_DEVICE_INVALID)	
					res = AST_DEVICE_UNAVAILABLE;
			}
			if (!strcmp(data, p->agent)) {
				ast_mutex_unlock(&p->lock);
				break;
			}
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

static int function_agent(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
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
		if (agent->chan || !ast_strlen_zero(agent->loginchan)) 
			status = "LOGGEDIN";	
		ast_copy_string(buf, status, len);
	} else if (!strcasecmp(args.item, "password")) 
		ast_copy_string(buf, agent->password, len);
	else if (!strcasecmp(args.item, "name"))
		ast_copy_string(buf, agent->name, len);
	else if (!strcasecmp(args.item, "mohclass"))
		ast_copy_string(buf, agent->moh, len);
	else if (!strcasecmp(args.item, "channel")) {
		if (agent->chan) {
			ast_copy_string(buf, agent->chan->name, len);
			tmp = strrchr(buf, '-');
			if (tmp)
				*tmp = '\0';
		} 
	} else if (!strcasecmp(args.item, "exten"))
		ast_copy_string(buf, agent->loginchan, len);	

	AST_LIST_UNLOCK(&agents);

	return 0;
}

struct ast_custom_function agent_function = {
	.name = "AGENT",
	.synopsis = "Gets information about an Agent",
	.syntax = "AGENT(<agentid>[:item])",
	.read = function_agent,
	.desc = "The valid items to retrieve are:\n"
	"- status (default)      The status of the agent\n"
	"                          LOGGEDIN | LOGGEDOUT\n"
	"- password              The password of the agent\n"
	"- name                  The name of the agent\n"
	"- mohclass              MusicOnHold class\n"
	"- exten                 The callback extension for the Agent (AgentCallbackLogin)\n"
	"- channel               The name of the active channel for the Agent (AgentLogin)\n"
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
	/* Make sure we can register our agent channel type */
	if (ast_channel_register(&agent_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Agent'\n");
		return -1;
	}
	/* Read in the config */
	if (!read_agent_config())
		return AST_MODULE_LOAD_DECLINE;
	if (persistent_agents)
		reload_agents();
	/* Dialplan applications */
	ast_register_application(app, login_exec, synopsis, descrip);
	ast_register_application(app2, callback_exec, synopsis2, descrip2);
	ast_register_application(app3, agentmonitoroutgoing_exec, synopsis3, descrip3);

	/* Manager commands */
	ast_manager_register2("Agents", EVENT_FLAG_AGENT, action_agents, "Lists agents and their status", mandescr_agents);
	ast_manager_register2("AgentLogoff", EVENT_FLAG_AGENT, action_agent_logoff, "Sets an agent as no longer logged in", mandescr_agent_logoff);
	ast_manager_register2("AgentCallbackLogin", EVENT_FLAG_AGENT, action_agent_callback_login, "Sets an agent as logged in by callback", mandescr_agent_callback_login);

	/* CLI Commands */
	ast_cli_register_multiple(cli_agents, sizeof(cli_agents) / sizeof(struct ast_cli_entry));

	/* Dialplan Functions */
	ast_custom_function_register(&agent_function);

	ast_devstate_add(agent_devicestate_cb, NULL);

	return 0;
}

static int reload(void)
{
	read_agent_config();
	if (persistent_agents)
		reload_agents();
	return 0;
}

static int unload_module(void)
{
	struct agent_pvt *p;
	/* First, take us out of the channel loop */
	ast_channel_unregister(&agent_tech);
	/* Delete devicestate subscription */
	ast_devstate_del(agent_devicestate_cb, NULL);
	/* Unregister dialplan functions */
	ast_custom_function_unregister(&agent_function);	
	/* Unregister CLI commands */
	ast_cli_unregister_multiple(cli_agents, sizeof(cli_agents) / sizeof(struct ast_cli_entry));
	/* Unregister dialplan applications */
	ast_unregister_application(app);
	ast_unregister_application(app2);
	ast_unregister_application(app3);
	/* Unregister manager command */
	ast_manager_unregister("Agents");
	ast_manager_unregister("AgentLogoff");
	ast_manager_unregister("AgentCallbackLogin");
	/* Unregister channel */
	AST_LIST_LOCK(&agents);
	/* Hangup all interfaces if they have an owner */
	while ((p = AST_LIST_REMOVE_HEAD(&agents, list))) {
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
		free(p);
	}
	AST_LIST_UNLOCK(&agents);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Agent Proxy Channel",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
