/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Agents
 * 
 * Copyright (C) 1999-2004, Digium Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/lock.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/rtp.h>
#include <asterisk/acl.h>
#include <asterisk/callerid.h>
#include <asterisk/file.h>
#include <asterisk/cli.h>
#include <asterisk/app.h>
#include <asterisk/musiconhold.h>
#include <asterisk/manager.h>
#include <asterisk/features.h>
#include <asterisk/utils.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/signal.h>

static char *desc = "Agent Proxy Channel";
static char *type = "Agent";
static char *tdesc = "Call Agent Proxy Channel";
static char *config = "agents.conf";

static char *app = "AgentLogin";
static char *app2 = "AgentCallbackLogin";
static char *app3 = "AgentMonitorOutgoing";

static char *synopsis = "Call agent login";
static char *synopsis2 = "Call agent callback login";
static char *synopsis3 = "Record agent's outgoing call";

static char *descrip =
"  AgentLogin([AgentNo][|options]):\n"
"Asks the agent to login to the system.  Always returns -1.  While\n"
"logged in, the agent can receive calls and will hear a 'beep'\n"
"when a new call comes in.  The agent can dump the call by pressing\n"
"the star key.\n"
"The option string may contain zero or more of the following characters:\n"
"      's' -- silent login - do not announce the login ok segment\n";

static char *descrip2 =
"  AgentCallbackLogin([AgentNo][|[options][exten]@context]):\n"
"Asks the agent to login to the system with callback.  Always returns -1.\n"
"The agent's callback extension is called (optionally with the specified\n"
"context. \n";

static char *descrip3 =
"  AgentMonitorOutgoing([options]):\n"
"Tries to figure out the id of the agent who is placing outgoing call based on comparision of the callerid of the current interface and the global variable placed by the AgentCallbackLogin application. That's why it should be used only with the AgentCallbackLogin app. Uses the monitoring functions in chan_agent instead of Monitor application. That have to be configured in the agents.conf file. Normally the app returns 0 unless the options are passed. Also if the callerid or the agentid are not specified it'll look for n+101 priority. The options are:\n"
"	'd' - make the app return -1 if there is an error condition and there is no extension n+101\n"
"	'n' - don't generate the warnings when there is no callerid or the agentid is not known. It's handy if you want to have one context for agent and non-agent calls.\n";

static char moh[80] = "default";

#define AST_MAX_AGENT	80		/* Agent ID or Password max length */
#define AST_MAX_BUF	256

static int capability = -1;

static unsigned int group;
static int autologoff;
static int wrapuptime;
static int ackcall;

static int usecnt =0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of sip_pvt's) */
AST_MUTEX_DEFINE_STATIC(agentlock);

static int recordagentcalls = 0;
static char recordformat[AST_MAX_BUF] = "";
static char recordformatext[AST_MAX_BUF] = "";
static int createlink = 0;
static char urlprefix[AST_MAX_BUF] = "";
static char savecallsin[AST_MAX_BUF] = "";
static int updatecdr = 0;
static char beep[AST_MAX_BUF] = "beep";

#define GETAGENTBYCALLERID	"AGENTBYCALLERID"

static struct agent_pvt {
	ast_mutex_t lock;				/* Channel private lock */
	int dead;							/* Poised for destruction? */
	int pending;						/* Not a real agent -- just pending a match */
	int abouttograb;					/* About to grab */
	int autologoff;					/* Auto timeout time */
	int ackcall;					/* ackcall */
	time_t loginstart;					/* When agent first logged in (0 when logged off) */
	time_t start;						/* When call started */
	struct timeval lastdisc;			/* When last disconnected */
	int wrapuptime;						/* Wrapup time in ms */
	unsigned int group;					/* Group memberships */
	int acknowledged;					/* Acknowledged */
	char moh[80];						/* Which music on hold */
	char agent[AST_MAX_AGENT];			/* Agent ID */
	char password[AST_MAX_AGENT];		/* Password for Agent login */
	char name[AST_MAX_AGENT];
	ast_mutex_t app_lock;			/* Synchronization between owning applications */
	volatile pthread_t owning_app;		/* Owning application thread id */
	volatile int app_sleep_cond;		/* Sleep condition for the login app */
	struct ast_channel *owner;			/* Agent */
	char loginchan[80];
	struct ast_channel *chan;			/* Channel we use */
	struct agent_pvt *next;				/* Agent */
} *agents = NULL;

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
		if (p->chan->readformat != ast->pvt->rawreadformat)  \
			ast_set_read_format(p->chan, ast->pvt->rawreadformat); \
		if (p->chan->writeformat != ast->pvt->rawwriteformat) \
			ast_set_write_format(p->chan, ast->pvt->rawwriteformat); \
	} \
} while(0)

/* Cleanup moves all the relevant FD's from the 2nd to the first, but retains things
   properly for a timingfd XXX This might need more work if agents were logged in as agents or other
   totally impractical combinations XXX */

#define CLEANUP(ast, p) do { \
	int x; \
	if (p->chan) { \
		for (x=0;x<AST_MAX_FDS;x++) {\
			if (x != AST_MAX_FDS - 2) \
				ast->fds[x] = p->chan->fds[x]; \
		} \
		ast->fds[AST_MAX_FDS - 3] = p->chan->fds[AST_MAX_FDS - 2]; \
	} \
} while(0)


static void agent_unlink(struct agent_pvt *agent)
{
	struct agent_pvt *p, *prev;
	prev = NULL;
	p = agents;
	while(p) {
		if (p == agent) {
			if (prev)
				prev->next = agent->next;
			else
				agents = agent->next;
			break;
		}
		prev = p;
		p = p->next;
	}
}

static struct agent_pvt *add_agent(char *agent, int pending)
{
	char tmp[AST_MAX_BUF] = "";
	char *password=NULL, *name=NULL;
	struct agent_pvt *p, *prev;
	
	strncpy(tmp, agent, sizeof(tmp) - 1);
	if ((password = strchr(tmp, ','))) {
		*password = '\0';
		password++;
		while (*password < 33) password++;
	}
	if (password && (name = strchr(password, ','))) {
		*name = '\0';
		name++;
		while (*name < 33) name++; 
	}
	prev=NULL;
	p = agents;
	while(p) {
		if (!pending && !strcmp(p->agent, tmp))
			break;
		prev = p;
		p = p->next;
	}
	if (!p) {
		p = malloc(sizeof(struct agent_pvt));
		if (p) {
			memset(p, 0, sizeof(struct agent_pvt));
			strncpy(p->agent, tmp, sizeof(p->agent) -1);
			ast_mutex_init(&p->lock);
			ast_mutex_init(&p->app_lock);
			p->owning_app = (pthread_t) -1;
			p->app_sleep_cond = 1;
			p->group = group;
			p->pending = pending;
			p->next = NULL;
			if (prev)
				prev->next = p;
			else
				agents = p;
			
		}
	}
	if (!p)
		return NULL;
	strncpy(p->password, password ? password : "", sizeof(p->password) - 1);
	strncpy(p->name, name ? name : "", sizeof(p->name) - 1);
	strncpy(p->moh, moh, sizeof(p->moh) - 1);
	p->ackcall = ackcall;
	p->autologoff = autologoff;
	p->wrapuptime = wrapuptime;
	if (pending)
		p->dead = 1;
	else
		p->dead = 0;
	return p;
}

static int agent_cleanup(struct agent_pvt *p)
{
	struct ast_channel *chan = p->owner;
	p->owner = NULL;
	chan->pvt->pvt = NULL;
	p->app_sleep_cond = 1;
	/* Release ownership of the agent to other threads (presumably running the login app). */
	ast_mutex_unlock(&p->app_lock);
	if (chan)
		ast_channel_free(chan);
	if (p->dead) {
		ast_mutex_destroy(&p->lock);
		ast_mutex_destroy(&p->app_lock);
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
		snprintf(tmp, sizeof(tmp), "%s%s",savecallsin ? savecallsin : "", filename);
		ast_monitor_start(ast, recordformat, tmp, needlock);
		ast_monitor_setjoinfiles(ast, 1);
		snprintf(tmp2, sizeof(tmp2), "%s%s.%s", urlprefix ? urlprefix : "", filename, recordformatext);
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
	return __agent_start_monitoring(ast, ast->pvt->pvt, needlock);
}

static struct ast_frame  *agent_read(struct ast_channel *ast)
{
	struct agent_pvt *p = ast->pvt->pvt;
	struct ast_frame *f = NULL;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	static struct ast_frame answer_frame = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
	ast_mutex_lock(&p->lock); 
	CHECK_FORMATS(ast, p);
	if (p->chan) {
		p->chan->exception = ast->exception;
		if (ast->fdno == AST_MAX_FDS - 3)
			p->chan->fdno = AST_MAX_FDS - 2;
		else
			p->chan->fdno = ast->fdno;
		f = ast_read(p->chan);
	} else
		f = &null_frame;
	if (!f) {
		/* If there's a channel, hang it up  (if it's on a callback) make it NULL */
		if (p->chan) {
			/* Note that we don't hangup if it's not a callback because Asterisk will do it
			   for us when the PBX instance that called login finishes */
			if (!ast_strlen_zero(p->loginchan)) {
				ast_hangup(p->chan);
				if (p->wrapuptime) {
					gettimeofday(&p->lastdisc, NULL);
					p->lastdisc.tv_usec += (p->wrapuptime % 1000) * 1000;
					if (p->lastdisc.tv_usec > 1000000) {
						p->lastdisc.tv_usec -= 1000000;
						p->lastdisc.tv_sec++;
					}
					p->lastdisc.tv_sec += (p->wrapuptime / 1000);
				}
			}
			p->chan = NULL;
			p->acknowledged = 0;
		}
	}
	if (f && (f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_ANSWER)) {
/* TC */
		if (p->ackcall) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "%s answered, waiting for '#' to acknowledge\n", p->chan->name);
			/* Don't pass answer along */
			ast_frfree(f);
			f = &null_frame;
	        }
        else {
			p->acknowledged = 1;
			f = &answer_frame;
        }
	}
	if (f && (f->frametype == AST_FRAME_DTMF) && (f->subclass == '#')) {
		if (!p->acknowledged) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "%s acknowledged\n", p->chan->name);
			p->acknowledged = 1;
			ast_frfree(f);
			f = &answer_frame;
		}
	}
	if (f && (f->frametype == AST_FRAME_DTMF) && (f->subclass == '*')) {
		/* * terminates call */
		ast_frfree(f);
		f = NULL;
	}
	CLEANUP(ast,p);
	ast_mutex_unlock(&p->lock);
	if (recordagentcalls && f == &answer_frame)
		agent_start_monitoring(ast,0);
	return f;
}

static int agent_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	CHECK_FORMATS(ast, p);
	ast_mutex_lock(&p->lock);
	if (p->chan) {
		if ((f->frametype != AST_FRAME_VOICE) ||
			(f->subclass == p->chan->writeformat)) {
			res = ast_write(p->chan, f);
		} else {
			ast_log(LOG_DEBUG, "Dropping one incompatible voice frame on '%s' to '%s'\n", ast->name, p->chan->name);
			res = 0;
		}
	} else
		res = 0;
	CLEANUP(ast, p);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct agent_pvt *p = newchan->pvt->pvt;
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

static int agent_indicate(struct ast_channel *ast, int condition)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan)
		res = ast_indicate(p->chan, condition);
	else
		res = 0;
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_digit(struct ast_channel *ast, char digit)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_mutex_lock(&p->lock);
	if (p->chan)
		res = p->chan->pvt->send_digit(p->chan, digit);
	else
		res = 0;
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_mutex_lock(&p->lock);
	p->acknowledged = 0;
	if (!p->chan) {
		if (p->pending) {
			ast_log(LOG_DEBUG, "Pretending to dial on pending agent\n");
			ast_setstate(ast, AST_STATE_DIALING);
			res = 0;
		} else {
			ast_log(LOG_NOTICE, "Whoa, they hung up between alloc and call...  what are the odds of that?\n");
			res = -1;
		}
		ast_mutex_unlock(&p->lock);
		return res;
	} else if (!ast_strlen_zero(p->loginchan)) {
		time(&p->start);
		/* Call on this agent */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "outgoing agentcall, to agent '%s', on '%s'\n", p->agent, p->chan->name);
		if (p->chan->callerid)
			free(p->chan->callerid);
		if (ast->callerid)
			p->chan->callerid = strdup(ast->callerid);
		else
			p->chan->callerid = NULL;
		res = ast_call(p->chan, p->loginchan, 0);
		CLEANUP(ast,p);
		ast_mutex_unlock(&p->lock);
		return res;
	}
	ast_verbose( VERBOSE_PREFIX_3 "agent_call, call to agent '%s' call on '%s'\n", p->agent, p->chan->name);
	ast_log( LOG_DEBUG, "Playing beep, lang '%s'\n", p->chan->language);
	res = ast_streamfile(p->chan, beep, p->chan->language);
	ast_log( LOG_DEBUG, "Played beep, result '%d'\n", res);
	if (!res) {
		res = ast_waitstream(p->chan, "");
		ast_log( LOG_DEBUG, "Waited for stream, result '%d'\n", res);
	}
	if (!res) {
		res = ast_set_read_format(p->chan, ast_best_codec(p->chan->nativeformats));
		ast_log( LOG_DEBUG, "Set read format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set read format to %s\n", ast_getformatname(ast_best_codec(p->chan->nativeformats)));
	} else {
		// Agent hung-up
		p->chan = NULL;
	}

	if (!res) {
		ast_set_write_format(p->chan, ast_best_codec(p->chan->nativeformats));
		ast_log( LOG_DEBUG, "Set write format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set write format to %s\n", ast_getformatname(ast_best_codec(p->chan->nativeformats)));
	}
	if( !res )
	{
		/* Call is immediately up, or might need ack */
		if (p->ackcall > 1)
			ast_setstate(ast, AST_STATE_RINGING);
		else {
			ast_setstate(ast, AST_STATE_UP);
			if (recordagentcalls)
				agent_start_monitoring(ast,0);
			p->acknowledged = 1;
		}
		res = 0;
	}
	CLEANUP(ast,p);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int agent_hangup(struct ast_channel *ast)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int howlong = 0;
	ast_mutex_lock(&p->lock);
	p->owner = NULL;
	ast->pvt->pvt = NULL;
	p->app_sleep_cond = 1;
	p->acknowledged = 0;
	if (p->start && (ast->_state != AST_STATE_UP))
		howlong = time(NULL) - p->start;
	time(&p->start);
	if (p->chan) {
		/* If they're dead, go ahead and hang up on the agent now */
		if (!ast_strlen_zero(p->loginchan)) {
			/* Store last disconnect time */
			if (p->wrapuptime) {
				gettimeofday(&p->lastdisc, NULL);
				p->lastdisc.tv_usec += (p->wrapuptime % 1000) * 1000;
				if (p->lastdisc.tv_usec >= 1000000) {
					p->lastdisc.tv_usec -= 1000000;
					p->lastdisc.tv_sec++;
				}
				p->lastdisc.tv_sec += (p->wrapuptime / 1000);
			} else
				memset(&p->lastdisc, 0, sizeof(p->lastdisc));
			if (p->chan) {
				/* Recognize the hangup and pass it along immediately */
				ast_hangup(p->chan);
				p->chan = NULL;
			}
			ast_log(LOG_DEBUG, "Hungup, howlong is %d, autologoff is %d\n", howlong, p->autologoff);
			if (howlong  && p->autologoff && (howlong > p->autologoff)) {
				char agent[AST_MAX_AGENT] = "";
				long logintime = time(NULL) - p->loginstart;
				p->loginstart = 0;
				ast_log(LOG_NOTICE, "Agent '%s' didn't answer/confirm within %d seconds (waited %d)\n", p->name, p->autologoff, howlong);
				manager_event(EVENT_FLAG_AGENT, "Agentcallbacklogoff",
					"Agent: %s\r\n"
					"Loginchan: %s\r\n"
					"Logintime: %ld\r\n"
					"Reason: Autologoff\r\n"
					"Uniqueid: %s\r\n",
					p->agent, p->loginchan, logintime, ast->uniqueid);
				snprintf(agent, sizeof(agent), "Agent/%s", p->agent);
				ast_queue_log("NONE", ast->uniqueid, agent, "AGENTCALLBACKLOGOFF", "%s|%ld|%s", p->loginchan, logintime, "Autologoff");
				p->loginchan[0] = '\0';
			}
		} else if (p->dead) {
			ast_mutex_lock(&p->chan->lock);
			ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
			ast_mutex_unlock(&p->chan->lock);
		} else {
			ast_mutex_lock(&p->chan->lock);
			ast_moh_start(p->chan, p->moh);
			ast_mutex_unlock(&p->chan->lock);
		}
	}
#if 0
		ast_mutex_unlock(&p->lock);
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_mutex_unlock(&p->app_lock);
	} else if (p->dead) {
		/* Go ahead and lose it */
		ast_mutex_unlock(&p->lock);
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_mutex_unlock(&p->app_lock);
	} else {
		ast_mutex_unlock(&p->lock);
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_mutex_unlock(&p->app_lock);
	}
#endif	
	ast_mutex_unlock(&p->lock);

	if (p->pending) {
		ast_mutex_lock(&agentlock);
		agent_unlink(p);
		ast_mutex_unlock(&agentlock);
	}
	if (p->abouttograb) {
		/* Let the "about to grab" thread know this isn't valid anymore, and let it
		   kill it later */
		p->abouttograb = 0;
	} else if (p->dead) {
		ast_mutex_destroy(&p->lock);
		ast_mutex_destroy(&p->app_lock);
		free(p);
	} else {
		if (p->chan) {
			/* Not dead -- check availability now */
			ast_mutex_lock(&p->lock);
			/* Store last disconnect time */
			gettimeofday(&p->lastdisc, NULL);
			ast_mutex_unlock(&p->lock);
		}
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_mutex_unlock(&p->app_lock);
	}
	return 0;
}

static int agent_cont_sleep( void *data )
{
	struct agent_pvt *p;
	struct timeval tv;
	int res;

	p = (struct agent_pvt *)data;

	ast_mutex_lock(&p->lock);
	res = p->app_sleep_cond;
	if (p->lastdisc.tv_sec) {
		gettimeofday(&tv, NULL);
		if ((tv.tv_sec - p->lastdisc.tv_sec) * 1000 + 
			(tv.tv_usec - p->lastdisc.tv_usec) / 1000 > p->wrapuptime) 
			res = 1;
	}
	ast_mutex_unlock(&p->lock);
#if 0
	if( !res )
		ast_log( LOG_DEBUG, "agent_cont_sleep() returning %d\n", res );
#endif		
	return res;
}

static int agent_ack_sleep( void *data )
{
	struct agent_pvt *p;
	int res=0;
	int to = 1000;
	struct ast_frame *f;

	/* Wait a second and look for something */

	p = (struct agent_pvt *)data;
	if (p->chan) {
		for(;;) {
			to = ast_waitfor(p->chan, to);
			if (to < 0) {
				res = -1;
				break;
			}
			if (!to) {
				res = 0;
				break;
			}
			f = ast_read(p->chan);
			if (!f) {
				res = -1;
				break;
			}
			if (f->frametype == AST_FRAME_DTMF)
				res = f->subclass;
			else
				res = 0;
			ast_frfree(f);
			ast_mutex_lock(&p->lock);
			if (!p->app_sleep_cond) {
				ast_mutex_unlock(&p->lock);
				res = 0;
				break;
			} else if (res == '#') {
				ast_mutex_unlock(&p->lock);
				res = 1;
				break;
			}
			ast_mutex_unlock(&p->lock);
			res = 0;
		}
	} else
		res = -1;
	return res;
}

static struct ast_channel *agent_new(struct agent_pvt *p, int state)
{
	struct ast_channel *tmp;
	struct ast_frame null_frame = { AST_FRAME_NULL };
#if 0
	if (!p->chan) {
		ast_log(LOG_WARNING, "No channel? :(\n");
		return NULL;
	}
#endif	
	tmp = ast_channel_alloc(0);
	if (tmp) {
		if (p->chan) {
			tmp->nativeformats = p->chan->nativeformats;
			tmp->writeformat = p->chan->writeformat;
			tmp->pvt->rawwriteformat = p->chan->writeformat;
			tmp->readformat = p->chan->readformat;
			tmp->pvt->rawreadformat = p->chan->readformat;
			strncpy(tmp->language, p->chan->language, sizeof(tmp->language)-1);
			strncpy(tmp->context, p->chan->context, sizeof(tmp->context)-1);
			strncpy(tmp->exten, p->chan->exten, sizeof(tmp->exten)-1);
		} else {
			tmp->nativeformats = AST_FORMAT_SLINEAR;
			tmp->writeformat = AST_FORMAT_SLINEAR;
			tmp->pvt->rawwriteformat = AST_FORMAT_SLINEAR;
			tmp->readformat = AST_FORMAT_SLINEAR;
			tmp->pvt->rawreadformat = AST_FORMAT_SLINEAR;
		}
		if (p->pending)
			snprintf(tmp->name, sizeof(tmp->name), "Agent/P%s-%d", p->agent, rand() & 0xffff);
		else
			snprintf(tmp->name, sizeof(tmp->name), "Agent/%s", p->agent);
		tmp->type = type;
		ast_setstate(tmp, state);
		tmp->pvt->pvt = p;
		tmp->pvt->send_digit = agent_digit;
		tmp->pvt->call = agent_call;
		tmp->pvt->hangup = agent_hangup;
		tmp->pvt->answer = agent_answer;
		tmp->pvt->read = agent_read;
		tmp->pvt->write = agent_write;
		tmp->pvt->exception = agent_read;
		tmp->pvt->indicate = agent_indicate;
		tmp->pvt->fixup = agent_fixup;
		p->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
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
		if( ast_mutex_trylock(&p->app_lock) )
		{
			if (p->chan) {
				ast_queue_frame(p->chan, &null_frame);
				ast_mutex_unlock(&p->lock);	/* For other thread to read the condition. */
				ast_mutex_lock(&p->app_lock);
				ast_mutex_lock(&p->lock);
			}
			if( !p->chan )
			{
				ast_log(LOG_WARNING, "Agent disconnected while we were connecting the call\n");
				p->owner = NULL;
				tmp->pvt->pvt = NULL;
				p->app_sleep_cond = 1;
				ast_channel_free( tmp );
				ast_mutex_unlock(&p->lock);	/* For other thread to read the condition. */
				ast_mutex_unlock(&p->app_lock);
				return NULL;
			}
		}
		p->owning_app = pthread_self();
		/* After the above step, there should not be any blockers. */
		if (p->chan) {
			if (p->chan->blocking) {
				ast_log( LOG_ERROR, "A blocker exists after agent channel ownership acquired\n" );
				CRASH;
			}
			ast_moh_stop(p->chan);
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static int read_agent_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct agent_pvt *p, *pl, *pn;
	group = 0;
	autologoff = 0;
	wrapuptime = 0;
	ackcall = 1;
	cfg = ast_load(config);
	if (!cfg) {
		ast_log(LOG_NOTICE, "No agent configuration found -- agent support disabled\n");
		return 0;
	}
	ast_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		p->dead = 1;
		p = p->next;
	}
	strncpy(moh, "default", sizeof(moh) - 1);
	/* set the default recording values */
	recordagentcalls = 0;
	createlink = 0;
	strncpy(recordformat, "wav", sizeof(recordformat) - 1);
	strncpy(recordformatext, "wav", sizeof(recordformatext) - 1);
	urlprefix[0] = '\0';
	savecallsin[0] = '\0';

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
		} else if (!strcasecmp(v->name, "wrapuptime")) {
			wrapuptime = atoi(v->value);
			if (wrapuptime < 0)
				wrapuptime = 0;
		} else if (!strcasecmp(v->name, "musiconhold")) {
			strncpy(moh, v->value, sizeof(moh) - 1);
		} else if (!strcasecmp(v->name, "updatecdr")) {
			updatecdr = ast_true(v->value);
		} else if (!strcasecmp(v->name, "recordagentcalls")) {
			recordagentcalls = ast_true(v->value);
		} else if (!strcasecmp(v->name, "createlink")) {
			createlink = ast_true(v->value);
		} else if (!strcasecmp(v->name, "recordformat")) {
			strncpy(recordformat, v->value, sizeof(recordformat) - 1);
			if (!strcasecmp(v->value, "wav49"))
				strncpy(recordformatext, "WAV", sizeof(recordformatext) - 1);
			else
				strncpy(recordformatext, v->value, sizeof(recordformatext) - 1);
		} else if (!strcasecmp(v->name, "urlprefix")) {
			strncpy(urlprefix, v->value, sizeof(urlprefix) - 2);
			if (urlprefix[strlen(urlprefix) - 1] != '/')
				strncat(urlprefix, "/", sizeof(urlprefix) - strlen(urlprefix) - 1);
		} else if (!strcasecmp(v->name, "savecallsin")) {
			if (v->value[0] == '/')
				strncpy(savecallsin, v->value, sizeof(savecallsin) - 2);
			else
				snprintf(savecallsin, sizeof(savecallsin) - 2, "/%s", v->value);
			if (savecallsin[strlen(savecallsin) - 1] != '/')
				strncat(savecallsin, "/", sizeof(savecallsin) - strlen(savecallsin) - 1);
		} else if (!strcasecmp(v->name, "custom_beep")) {
			strncpy(beep, v->value, sizeof(beep) - 1);
		}
		v = v->next;
	}
	p = agents;
	pl = NULL;
	while(p) {
		pn = p->next;
		if (p->dead) {
			/* Unlink */
			if (pl)
				pl->next = p->next;
			else
				agents = p->next;
			/* Destroy if  appropriate */
			if (!p->owner) {
				if (!p->chan) {
					ast_mutex_destroy(&p->lock);
					ast_mutex_destroy(&p->app_lock);
					free(p);
				} else {
					/* Cause them to hang up */
					ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
				}
			}
		} else
			pl = p;
		p = pn;
	}
	ast_mutex_unlock(&agentlock);
	ast_destroy(cfg);
	return 0;
}

static int check_availability(struct agent_pvt *newlyavailable, int needlock)
{
	struct ast_channel *chan=NULL, *parent=NULL;
	struct agent_pvt *p;
	int res;
	ast_log(LOG_DEBUG, "Checking availability of '%s'\n", newlyavailable->agent);
	if (needlock)
		ast_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		if (p == newlyavailable) {
			p = p->next;
			continue;
		}
		ast_mutex_lock(&p->lock);
		if (!p->abouttograb && p->pending && ((p->group && (newlyavailable->group & p->group)) || !strcmp(p->agent, newlyavailable->agent))) {
			ast_log(LOG_DEBUG, "Call '%s' looks like a winner for agent '%s'\n", p->owner->name, newlyavailable->agent);
			/* We found a pending call, time to merge */
			chan = agent_new(newlyavailable, AST_STATE_DOWN);
			parent = p->owner;
			p->abouttograb = 1;
			ast_mutex_unlock(&p->lock);
			break;
		}
		ast_mutex_unlock(&p->lock);
		p = p->next;
	}
	if (needlock)
		ast_mutex_unlock(&agentlock);
	if (parent && chan)  {
		if (newlyavailable->ackcall > 1) {
			/* Don't do beep here */
			res = 0;
		} else {
			ast_log( LOG_DEBUG, "Playing beep, lang '%s'\n", newlyavailable->chan->language);
			res = ast_streamfile(newlyavailable->chan, beep, newlyavailable->chan->language);
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
				ast_setstate(parent, AST_STATE_UP);
				ast_setstate(chan, AST_STATE_UP);
				strncpy(parent->context, chan->context, sizeof(parent->context) - 1);
				/* Go ahead and mark the channel as a zombie so that masquerade will
				   destroy it for us, and we need not call ast_hangup */
				ast_mutex_lock(&parent->lock);
				chan->zombie = 1;
				ast_channel_masquerade(parent, chan);
				ast_mutex_unlock(&parent->lock);
				p->abouttograb = 0;
			} else {
				ast_log(LOG_DEBUG, "Sneaky, parent disappeared in the mean time...\n");
				agent_cleanup(newlyavailable);
			}
		} else {
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
		ast_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		if (p == newlyavailable) {
			p = p->next;
			continue;
		}
		ast_mutex_lock(&p->lock);
		if (!p->abouttograb && p->pending && ((p->group && (newlyavailable->group & p->group)) || !strcmp(p->agent, newlyavailable->agent))) {
			ast_log(LOG_DEBUG, "Call '%s' looks like a would-be winner for agent '%s'\n", p->owner->name, newlyavailable->agent);
			ast_mutex_unlock(&p->lock);
			break;
		}
		ast_mutex_unlock(&p->lock);
		p = p->next;
	}
	if (needlock)
		ast_mutex_unlock(&agentlock);
	if (p) {
		ast_mutex_unlock(&newlyavailable->lock);
		ast_log( LOG_DEBUG, "Playing beep, lang '%s'\n", newlyavailable->chan->language);
		res = ast_streamfile(newlyavailable->chan, beep, newlyavailable->chan->language);
		ast_log( LOG_DEBUG, "Played beep, result '%d'\n", res);
		if (!res) {
			res = ast_waitstream(newlyavailable->chan, "");
			ast_log( LOG_DEBUG, "Waited for stream, result '%d'\n", res);
		}
		ast_mutex_lock(&newlyavailable->lock);
	}
	return res;
}

static struct ast_channel *agent_request(char *type, int format, void *data)
{
	struct agent_pvt *p;
	struct ast_channel *chan = NULL;
	char *s;
	unsigned int groupmatch;
	int waitforagent=0;
	int hasagent = 0;
	struct timeval tv;
	s = data;
	if ((s[0] == '@') && (sscanf(s + 1, "%d", &groupmatch) == 1)) {
		groupmatch = (1 << groupmatch);
	} else if ((s[0] == ':') && (sscanf(s + 1, "%d", &groupmatch) == 1)) {
		groupmatch = (1 << groupmatch);
		waitforagent = 1;
	} else {
		groupmatch = 0;
	}

	/* Check actual logged in agents first */
	ast_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		ast_mutex_lock(&p->lock);
		if (!p->pending && ((groupmatch && (p->group & groupmatch)) || !strcmp(data, p->agent)) &&
				ast_strlen_zero(p->loginchan)) {
			if (p->chan)
				hasagent++;
			if (!p->lastdisc.tv_sec) {
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
		p = p->next;
	}
	if (!p) {
		p = agents;
		while(p) {
			ast_mutex_lock(&p->lock);
			if (!p->pending && ((groupmatch && (p->group & groupmatch)) || !strcmp(data, p->agent))) {
				if (p->chan || !ast_strlen_zero(p->loginchan))
					hasagent++;
				gettimeofday(&tv, NULL);
#if 0
				ast_log(LOG_NOTICE, "Time now: %ld, Time of lastdisc: %ld\n", tv.tv_sec, p->lastdisc.tv_sec);
#endif
				if (!p->lastdisc.tv_sec || (tv.tv_sec > p->lastdisc.tv_sec)) {
					memset(&p->lastdisc, 0, sizeof(p->lastdisc));
					/* Agent must be registered, but not have any active call, and not be in a waiting state */
					if (!p->owner && p->chan) {
						/* Could still get a fixed agent */
						chan = agent_new(p, AST_STATE_DOWN);
					} else if (!p->owner && !ast_strlen_zero(p->loginchan)) {
						/* Adjustable agent */
						p->chan = ast_request("Local", format, p->loginchan);
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
			p = p->next;
		}
	}

	if (!chan && waitforagent) {
		/* No agent available -- but we're requesting to wait for one.
		   Allocate a place holder */
		if (hasagent) {
			ast_log(LOG_DEBUG, "Creating place holder for '%s'\n", s);
			p = add_agent(data, 1);
			p->group = groupmatch;
			chan = agent_new(p, AST_STATE_DOWN);
			if (!chan) {
				ast_log(LOG_WARNING, "Weird...  Fix this to drop the unused pending agent\n");
			}
		} else
			ast_log(LOG_DEBUG, "Not creating place holder for '%s' since nobody logged in\n", s);
	}
	ast_mutex_unlock(&agentlock);
	return chan;
}

static int powerof(unsigned int v)
{
	int x;
	for (x=0;x<32;x++) {
		if (v & (1 << x)) return x;
	}
	return 0;
}

static int agents_show(int fd, int argc, char **argv)
{
	struct agent_pvt *p;
	char username[AST_MAX_BUF];
	char location[AST_MAX_BUF] = "";
	char talkingto[AST_MAX_BUF] = "";
	char moh[AST_MAX_BUF];

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&agentlock);
	p = agents;
	while(p) {
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
				if (p->owner && p->owner->bridge) {
					snprintf(talkingto, sizeof(talkingto), " talking to %s", p->owner->bridge->name);
				} else {
					strncpy(talkingto, " is idle", sizeof(talkingto) - 1);
				}
			} else if (!ast_strlen_zero(p->loginchan)) {
				snprintf(location, sizeof(location) - 20, "available at '%s'", p->loginchan);
				talkingto[0] = '\0';
				if (p->acknowledged)
					strncat(location, " (Confirmed)", sizeof(location) - strlen(location) - 1);
			} else {
				strncpy(location, "not logged in", sizeof(location) - 1);
				talkingto[0] = '\0';
			}
			if (!ast_strlen_zero(p->moh))
				snprintf(moh, sizeof(moh), " (musiconhold is '%s')", p->moh);
			ast_cli(fd, "%-12.12s %s%s%s%s\n", p->agent, 
					username, location, talkingto, moh);
		}
		ast_mutex_unlock(&p->lock);
		p = p->next;
	}
	ast_mutex_unlock(&agentlock);
	return RESULT_SUCCESS;
}

static char show_agents_usage[] = 
"Usage: show agents\n"
"       Provides summary information on agents.\n";

static struct ast_cli_entry cli_show_agents = {
	{ "show", "agents", NULL }, agents_show, 
	"Show status of agents", show_agents_usage, NULL };

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

static int __login_exec(struct ast_channel *chan, void *data, int callbackmode)
{
	int res=0;
	int tries = 0;
	struct agent_pvt *p;
	struct localuser *u;
	struct timeval tv;
	char user[AST_MAX_AGENT] = "";
	char pass[AST_MAX_AGENT];
	char agent[AST_MAX_AGENT] = "";
	char xpass[AST_MAX_AGENT] = "";
	char *errmsg;
	char info[512];
	char *opt_user = NULL;
	char *options = NULL;
	char *context = NULL;
	char *exten = NULL;
	int play_announcement;
	char *filename = "agent-loginok";
	
	LOCAL_USER_ADD(u);

	/* Parse the arguments XXX Check for failure XXX */
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
	opt_user = info;
	if( opt_user ) {
		options = strchr(opt_user, '|');
		if (options) {
			*options = '\0';
			options++;
			if (callbackmode) {
				context = strchr(options, '@');
				if (context) {
					*context = '\0';
					context++;
				}
				exten = options;
				while(*exten && ((*exten < '0') || (*exten > '9'))) exten++;
				if (!*exten)
					exten = NULL;
			}
		}
	}

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res) {
		if( opt_user && !ast_strlen_zero(opt_user))
			strncpy( user, opt_user, AST_MAX_AGENT - 1);
		else
			res = ast_app_getdata(chan, "agent-user", user, sizeof(user) - 1, 0);
	}
	while (!res && (tries < 3)) {
		/* Check for password */
		ast_mutex_lock(&agentlock);
		p = agents;
		while(p) {
			if (!strcmp(p->agent, user) && !p->pending)
				strncpy(xpass, p->password, sizeof(xpass) - 1);
			p = p->next;
		}
		ast_mutex_unlock(&agentlock);
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
		ast_mutex_lock(&agentlock);
		p = agents;
		while(p) {
			ast_mutex_lock(&p->lock);
			if (!strcmp(p->agent, user) &&
				!strcmp(p->password, pass) && !p->pending) {
					if (!p->chan) {
						char last_loginchan[80] = "";
						long logintime;
						snprintf(agent, sizeof(agent), "Agent/%s", p->agent);

						if (callbackmode) {
							char tmpchan[AST_MAX_BUF] = "";
							int pos = 0;
							/* Retrieve login chan */
							for (;;) {
								if (exten) {
									strncpy(tmpchan, exten, sizeof(tmpchan) - 1);
									res = 0;
								} else
									res = ast_app_getdata(chan, "agent-newlocation", tmpchan+pos, sizeof(tmpchan) - 2, 0);
								if (ast_strlen_zero(tmpchan) || ast_exists_extension(chan, context && !ast_strlen_zero(context) ? context : "default", tmpchan,
											1, NULL))
									break;
								if (exten) {
									ast_log(LOG_WARNING, "Extension '%s' is not valid for automatic login of agent '%s'\n", exten, p->agent);
									exten = NULL;
									pos = 0;
								} else {
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
							if (!res) {
								if (context && !ast_strlen_zero(context) && !ast_strlen_zero(tmpchan))
									snprintf(p->loginchan, sizeof(p->loginchan), "%s@%s", tmpchan, context);
								else {
	                                                                strncpy(last_loginchan, p->loginchan, sizeof(last_loginchan) - 1);
									strncpy(p->loginchan, tmpchan, sizeof(p->loginchan) - 1);
								}
								if (ast_strlen_zero(p->loginchan))
									filename = "agent-loggedoff";
								p->acknowledged = 0;
								/* store/clear the global variable that stores agentid based on the callerid */
								if (chan->callerid) {
									char agentvar[AST_MAX_BUF];
									snprintf(agentvar, sizeof(agentvar), "%s_%s",GETAGENTBYCALLERID, chan->callerid);
									if (ast_strlen_zero(p->loginchan))
										pbx_builtin_setvar_helper(NULL, agentvar, NULL);
									else
										pbx_builtin_setvar_helper(NULL, agentvar, p->agent);
								}
								if(updatecdr && chan->cdr)
									snprintf(chan->cdr->channel, sizeof(chan->cdr->channel), "Agent/%s", p->agent);

							}
						} else {
							p->loginchan[0] = '\0';
							p->acknowledged = 0;
						}
						play_announcement = 1;
						if( options )
							if( strchr( options, 's' ) )
								play_announcement = 0;
						ast_mutex_unlock(&p->lock);
						ast_mutex_unlock(&agentlock);
						if( !res && play_announcement )
							res = ast_streamfile(chan, filename, chan->language);
						if (!res)
							ast_waitstream(chan, "");
						ast_mutex_lock(&agentlock);
						ast_mutex_lock(&p->lock);
						if (!res) {
							res = ast_set_read_format(chan, ast_best_codec(chan->nativeformats));
							if (res)
								ast_log(LOG_WARNING, "Unable to set read format to %d\n", ast_best_codec(chan->nativeformats));
						}
						if (!res) {
							ast_set_write_format(chan, ast_best_codec(chan->nativeformats));
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
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Callback Agent '%s' logged in on %s\n", p->agent, p->loginchan);
							} else {
								logintime = time(NULL) - p->loginstart;
								p->loginstart = 0;
								manager_event(EVENT_FLAG_AGENT, "Agentcallbacklogoff",
									"Agent: %s\r\n"
									"Loginchan: %s\r\n"
									"Logintime: %ld\r\n"
									"Uniqueid: %s\r\n",
									p->agent, last_loginchan, logintime, chan->uniqueid);
								ast_queue_log("NONE", chan->uniqueid, agent, "AGENTCALLBACKLOGOFF", "%s|%ld|", last_loginchan, logintime);
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Callback Agent '%s' logged out\n", p->agent);
							}
							ast_mutex_unlock(&agentlock);
							if (!res)
								res = ast_safe_sleep(chan, 500);
							res = ast_streamfile(chan, "vm-goodbye", chan->language);
							if (!res)
								res = ast_waitstream(chan, "");
							if (!res)
								res = ast_safe_sleep(chan, 1000);
							ast_mutex_unlock(&p->lock);
						} else if (!res) {
#ifdef HONOR_MUSIC_CLASS
							/* check if the moh class was changed with setmusiconhold */
							if (*(chan->musicclass))
								strncpy(p->moh, chan->musicclass, sizeof(p->moh) - 1);
#endif								
							ast_moh_start(chan, p->moh);
							if (p->loginstart == 0)
								time(&p->loginstart);
							manager_event(EVENT_FLAG_AGENT, "Agentlogin",
								"Agent: %s\r\n"
								"Channel: %s\r\n"
								"Uniqueid: %s\r\n",
								p->agent, chan->name, chan->uniqueid);
							if (updatecdr && chan->cdr)
								snprintf(chan->cdr->channel, sizeof(chan->cdr->channel), "Agent/%s", p->agent);
							ast_queue_log("NONE", chan->uniqueid, agent, "AGENTLOGIN", "%s", chan->name);
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Agent '%s' logged in (format %s/%s)\n", p->agent,
												ast_getformatname(chan->readformat), ast_getformatname(chan->writeformat));
							/* Login this channel and wait for it to
							   go away */
							p->chan = chan;
							if (p->ackcall > 1)
								check_beep(p, 0);
							else
								check_availability(p, 0);
							ast_mutex_unlock(&p->lock);
							ast_mutex_unlock(&agentlock);
							while (res >= 0) {
								ast_mutex_lock(&p->lock);
								if (p->chan != chan)
									res = -1;
								ast_mutex_unlock(&p->lock);
								/* Yield here so other interested threads can kick in. */
								sched_yield();
								if (res)
									break;

								ast_mutex_lock(&agentlock);
								ast_mutex_lock(&p->lock);
								if (p->lastdisc.tv_sec) {
									gettimeofday(&tv, NULL);
									if ((tv.tv_sec - p->lastdisc.tv_sec) * 1000 + 
										(tv.tv_usec - p->lastdisc.tv_usec) / 1000 > p->wrapuptime) {
											ast_log(LOG_DEBUG, "Wrapup time expired!\n");
										memset(&p->lastdisc, 0, sizeof(p->lastdisc));
										if (p->ackcall > 1)
											check_beep(p, 0);
										else
											check_availability(p, 0);
									}
								}
								ast_mutex_unlock(&p->lock);
								ast_mutex_unlock(&agentlock);
								/*	Synchronize channel ownership between call to agent and itself. */
								ast_mutex_lock( &p->app_lock );
								ast_mutex_lock(&p->lock);
								p->owning_app = pthread_self();
								ast_mutex_unlock(&p->lock);
								if (p->ackcall > 1) 
									res = agent_ack_sleep(p);
								else
									res = ast_safe_sleep_conditional( chan, 1000,
													agent_cont_sleep, p );
								ast_mutex_unlock( &p->app_lock );
								if ((p->ackcall > 1)  && (res == 1)) {
									ast_mutex_lock(&agentlock);
									ast_mutex_lock(&p->lock);
									check_availability(p, 0);
									ast_mutex_unlock(&p->lock);
									ast_mutex_unlock(&agentlock);
									res = 0;
								}
								sched_yield();
							}
							ast_mutex_lock(&p->lock);
							if (res && p->owner) 
								ast_log(LOG_WARNING, "Huh?  We broke out when there was still an owner?\n");
							/* Log us off if appropriate */
							if (p->chan == chan)
								p->chan = NULL;
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
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Agent '%s' logged out\n", p->agent);
							/* If there is no owner, go ahead and kill it now */
							if (p->dead && !p->owner) {
								ast_mutex_destroy(&p->lock);
								ast_mutex_destroy(&p->app_lock);
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
			p = p->next;
		}
		if (!p)
			ast_mutex_unlock(&agentlock);

		if (!res)
			res = ast_app_getdata(chan, errmsg, user, sizeof(user) - 1, 0);
	}
		
	LOCAL_USER_REMOVE(u);
	/* Always hangup */
	return -1;
}

static int login_exec(struct ast_channel *chan, void *data)
{
	return __login_exec(chan, data, 0);
}

static int callback_exec(struct ast_channel *chan, void *data)
{
	return __login_exec(chan, data, 1);
}

static int agentmonitoroutgoing_exec(struct ast_channel *chan, void *data)
{
	int exitifnoagentid = 0;
	int nowarnings = 0;
	int res = 0;
	char agent[AST_MAX_AGENT], *tmp;
	if (data) {
		if (strchr(data, 'd'))
			exitifnoagentid = 1;
		if (strchr(data, 'n'))
			nowarnings = 1;
	}
	if (chan->callerid) {
		char agentvar[AST_MAX_BUF];
		snprintf(agentvar, sizeof(agentvar), "%s_%s", GETAGENTBYCALLERID, chan->callerid);
		if ((tmp = pbx_builtin_getvar_helper(NULL, agentvar))) {
			struct agent_pvt *p = agents;
			strncpy(agent, tmp, sizeof(agent) - 1);
			ast_mutex_lock(&agentlock);
			while (p) {
				if (!strcasecmp(p->agent, tmp)) {
					__agent_start_monitoring(chan, p, 1);
					break;
				}
				p = p->next;
			}
			ast_mutex_unlock(&agentlock);
			
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
	if (res) {
	       if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid)) {
			chan->priority+=100;
			ast_verbose(VERBOSE_PREFIX_3 "Going to %d priority because there is no callerid or the agentid cannot be found.\n",chan->priority);
	       }
		else if (exitifnoagentid)
			return res;
	}
	return 0;
}

int load_module()
{
	/* Make sure we can register our sip channel type */
	if (ast_channel_register(type, tdesc, capability, agent_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	ast_register_application(app, login_exec, synopsis, descrip);
	ast_register_application(app2, callback_exec, synopsis2, descrip2);
	ast_register_application(app3, agentmonitoroutgoing_exec, synopsis3, descrip3);
	ast_cli_register(&cli_show_agents);
	/* Read in the config */
	read_agent_config();
	return 0;
}

int reload()
{
	read_agent_config();
	return 0;
}

int unload_module()
{
	struct agent_pvt *p;
	/* First, take us out of the channel loop */
	ast_cli_unregister(&cli_show_agents);
	ast_unregister_application(app);
	ast_unregister_application(app2);
	ast_unregister_application(app3);
	ast_channel_unregister(type);
	if (!ast_mutex_lock(&agentlock)) {
		/* Hangup all interfaces if they have an owner */
		p = agents;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		agents = NULL;
		ast_mutex_unlock(&agentlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}		
	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

char *description()
{
	return desc;
}

