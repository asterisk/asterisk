/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Session Initiation Protocol
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <pthread.h>
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

static char *synopsis = "Call agent login";

static char *descrip =
"  AgentLogin([AgentNo][|options]):\n"
"Asks the agent to login to the system.  Always returns -1.  While\n"
"logged in, the agent can receive calls and will hear a 'beep'\n"
"when a new call comes in.  The agent can dump the call by pressing\n"
"the star key.\n"
"The option string may contain zero or more of the following characters:\n"
"      's' -- silent login - do not announce the login ok segment\n";

static char moh[80] = "default";

#define AST_MAX_AGENT	80		/* Agent ID or Password max length */

static int capability = -1;

static int usecnt =0;
static pthread_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

/* Protect the interface list (of sip_pvt's) */
static pthread_mutex_t agentlock = AST_MUTEX_INITIALIZER;

static struct agent_pvt {
	pthread_mutex_t lock;				/* Channel private lock */
	int dead;							/* Poised for destruction? */
	char moh[80];						/* Which music on hold */
	char agent[AST_MAX_AGENT];			/* Agent ID */
	char password[AST_MAX_AGENT];		/* Password for Agent login */
	char name[AST_MAX_AGENT];
	pthread_mutex_t app_lock;			/* Synchronization between owning applications */
	volatile pthread_t owning_app;		/* Owning application thread id */
	volatile int app_sleep_cond;		/* Sleep condition for the login app */
	struct ast_channel *owner;			/* Agent */
	struct ast_channel *chan;			/* Channel we use */
	struct agent_pvt *next;				/* Agent */
} *agents = NULL;

#define CLEANUP(ast, p) do { \
	int x; \
	if (p->chan) { \
		for (x=0;x<AST_MAX_FDS;x++) \
			ast->fds[x] = p->chan->fds[x]; \
	} \
} while(0)


static int add_agent(struct ast_variable *var)
{
	char tmp[256];
	char *password=NULL, *name=NULL;
	struct agent_pvt *p;
	
	strncpy(tmp, var->value, sizeof(tmp));
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
	p = agents;
	while(p) {
		if (!strcmp(p->agent, tmp))
			break;
		p = p->next;
	}
	if (!p) {
		p = malloc(sizeof(struct agent_pvt));
		if (p) {
			memset(p, 0, sizeof(struct agent_pvt));
			strncpy(p->agent, tmp, sizeof(p->agent) -1);
			ast_pthread_mutex_init( &p->lock );
			ast_pthread_mutex_init( &p->app_lock );
			p->owning_app = -1;
			p->app_sleep_cond = 1;
			p->next = agents;
			agents = p;
			
		}
	}
	if (!p)
		return -1;
	strncpy(p->password, password ? password : "", sizeof(p->password) - 1);
	strncpy(p->name, name ? name : "", sizeof(p->name) - 1);
	strncpy(p->moh, moh, sizeof(p->moh) - 1);
	p->dead = 0;
	return 0;
}

static int agent_answer(struct ast_channel *ast)
{
	ast_log(LOG_WARNING, "Huh?  Agent is being asked to answer?\n");
	return -1;
}

static struct ast_frame  *agent_read(struct ast_channel *ast)
{
	struct agent_pvt *p = ast->pvt->pvt;
	struct ast_frame *f = NULL;
	ast_pthread_mutex_lock(&p->lock);
	if (p->chan)
		f = ast_read(p->chan);
	if (!f) {
		/* If there's a channel, make it NULL */
		if (p->chan)
			p->chan = NULL;
	}
	if (f && (f->frametype == AST_FRAME_DTMF) && (f->subclass == '*')) {
		/* * terminates call */
		ast_frfree(f);
		f = NULL;
	}
	CLEANUP(ast,p);
	ast_pthread_mutex_unlock(&p->lock);
	return f;
}

static int agent_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_pthread_mutex_lock(&p->lock);
	if (p->chan)
		res = ast_write(p->chan, f);
	CLEANUP(ast, p);
	ast_pthread_mutex_unlock(&p->lock);
	return res;
}

static int agent_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct agent_pvt *p = newchan->pvt->pvt;
	ast_pthread_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		ast_pthread_mutex_unlock(&p->lock);
		return -1;
	}
	p->owner = newchan;
	ast_pthread_mutex_unlock(&p->lock);
	return 0;
}

static int agent_indicate(struct ast_channel *ast, int condition)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_pthread_mutex_lock(&p->lock);
	if (p->chan)
		res = ast_indicate(p->chan, condition);
	ast_pthread_mutex_unlock(&p->lock);
	return res;
}

static int agent_digit(struct ast_channel *ast, char digit)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_pthread_mutex_lock(&p->lock);
	if (p->chan)
		res = p->chan->pvt->send_digit(p->chan, digit);
	ast_pthread_mutex_unlock(&p->lock);
	return res;
}

static int agent_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct agent_pvt *p = ast->pvt->pvt;
	int res = -1;
	ast_pthread_mutex_lock(&p->lock);
	ast_verbose( VERBOSE_PREFIX_3 "agent_call, call to agent '%s' call on '%s'\n", p->agent, p->chan->name);
	ast_log( LOG_DEBUG, "Playing beep, lang '%s'\n", p->chan->language);
	res = ast_streamfile(p->chan, "beep", p->chan->language);
	ast_log( LOG_DEBUG, "Played beep, result '%d'\n", res);
	if (!res) {
		res = ast_waitstream(p->chan, "");
		ast_log( LOG_DEBUG, "Waited for stream, result '%d'\n", res);
	}
	if (!res) {
		res = ast_set_read_format(p->chan, ast_best_codec(p->chan->nativeformats));
		ast_log( LOG_DEBUG, "Set read format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set read format to %d\n", ast_best_codec(p->chan->nativeformats));
	}
	else {
		// Agent hung-up
		p->chan = NULL;
	}

	if (!res) {
		ast_set_write_format(p->chan, ast_best_codec(p->chan->nativeformats));
		ast_log( LOG_DEBUG, "Set write format, result '%d'\n", res);
		if (res)
			ast_log(LOG_WARNING, "Unable to set write format to %d\n", ast_best_codec(p->chan->nativeformats));
	}
	if( !res )
	{
		/* Call is immediately up */
		ast_setstate(ast, AST_STATE_UP);
	}
	CLEANUP(ast,p);
	ast_pthread_mutex_unlock(&p->lock);
	return res;
}

static int agent_hangup(struct ast_channel *ast)
{
	struct agent_pvt *p = ast->pvt->pvt;
	ast_pthread_mutex_lock(&p->lock);
	p->owner = NULL;
	ast->pvt->pvt = NULL;
	p->app_sleep_cond = 1;
	if (p->chan) {
		/* If they're dead, go ahead and hang up on the agent now */
		ast_pthread_mutex_lock(&p->chan->lock);
		if (p->dead)
			ast_softhangup(p->chan, AST_SOFTHANGUP_EXPLICIT);
		ast_moh_start(p->chan, p->moh);
		ast_pthread_mutex_unlock(&p->chan->lock);
		ast_pthread_mutex_unlock(&p->lock);
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_pthread_mutex_unlock(&p->app_lock);
	} else if (p->dead) {
		/* Go ahead and lose it */
		ast_pthread_mutex_unlock(&p->lock);
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_pthread_mutex_unlock(&p->app_lock);
		free(p);
	} else {
		ast_pthread_mutex_unlock(&p->lock);
		/* Release ownership of the agent to other threads (presumably running the login app). */
		ast_pthread_mutex_unlock(&p->app_lock);
	}

	return 0;
}

static int agent_cont_sleep( void *data )
{
	struct agent_pvt *p;
	int res;

	p = (struct agent_pvt *)data;

	ast_pthread_mutex_lock(&p->lock);
	res = p->app_sleep_cond;
	ast_pthread_mutex_unlock(&p->lock);
	if( !res )
		ast_log( LOG_DEBUG, "agent_cont_sleep() returning %d\n", res );
	return res;
}

static struct ast_channel *agent_new(struct agent_pvt *p, int state)
{
	struct ast_channel *tmp;
	struct ast_frame null_frame = { AST_FRAME_NULL };
	if (!p->chan) {
		ast_log(LOG_WARNING, "No channel? :(\n");
		return NULL;
	}
	tmp = ast_channel_alloc(0);
	if (tmp) {
		tmp->nativeformats = p->chan->nativeformats;
		snprintf(tmp->name, sizeof(tmp->name), "Agent/%s", p->agent);
		tmp->type = type;
		ast_setstate(tmp, state);
		tmp->writeformat = p->chan->writeformat;
		tmp->pvt->rawwriteformat = p->chan->writeformat;
		tmp->readformat = p->chan->readformat;
		tmp->pvt->rawreadformat = p->chan->readformat;
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
		strncpy(tmp->language, p->chan->language, sizeof(tmp->language)-1);
		p->owner = tmp;
		ast_pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(tmp->context, p->chan->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, p->chan->exten, sizeof(tmp->exten)-1);
		tmp->priority = 1;
		/* Wake up and wait for other applications (by definition the login app)
		 * to release this channel). Takes ownership of the agent channel
		 * to this thread only.
		 * For signalling the other thread, ast_queue_frame is used until we
		 * can safely use signals for this purpose. The pselect() needs to be
		 * implemented in the kernel for this.
		 */
		p->app_sleep_cond = 0;
		if( pthread_mutex_trylock(&p->app_lock) )
		{
			ast_queue_frame(p->chan, &null_frame, 1);
			ast_pthread_mutex_unlock(&p->lock);	/* For other thread to read the condition. */
			ast_pthread_mutex_lock(&p->app_lock);
			ast_pthread_mutex_lock(&p->lock);
			if( !p->chan )
			{
				ast_log(LOG_WARNING, "Agent disconnected while we were connecting the call\n");
				p->owner = NULL;
				tmp->pvt->pvt = NULL;
				p->app_sleep_cond = 1;
				ast_channel_free( tmp );
				return NULL;
			}
		}
		p->owning_app = pthread_self();
		/* After the above step, there should not be any blockers. */
		if (p->chan->blocking) {
			ast_log( LOG_ERROR, "A blocker exists after agent channel ownership acquired\n" );
			CRASH;
		}
		ast_moh_stop(p->chan);
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static int read_agent_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct agent_pvt *p, *pl, *pn;
	cfg = ast_load(config);
	if (!cfg) {
		ast_log(LOG_NOTICE, "No agent configuration found -- agent support disabled\n");
		return 0;
	}
	ast_pthread_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		p->dead = 1;
		p = p->next;
	}
	strcpy(moh, "default");
	v = ast_variable_browse(cfg, "agents");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "agent")) {
			add_agent(v);
		} else if (!strcasecmp(v->name, "musiconhold")) {
			strncpy(moh, v->value, sizeof(moh) - 1);
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
	ast_pthread_mutex_unlock(&agentlock);
	ast_destroy(cfg);
	return 0;
}

static struct ast_channel *agent_request(char *type, int format, void *data)
{
	struct agent_pvt *p;
	struct ast_channel *chan = NULL;
	ast_pthread_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		if (!strcmp(data, p->agent)) {
			ast_pthread_mutex_lock(&p->lock);
			/* Agent must be registered, but not have any active call */
			if (!p->owner && p->chan) {
				chan = agent_new(p, AST_STATE_DOWN);
			}
			ast_pthread_mutex_unlock(&p->lock);
			break;
		}
		p = p->next;
	}
	ast_pthread_mutex_unlock(&agentlock);
	return chan;
}

static int agents_show(int fd, int argc, char **argv)
{
	struct agent_pvt *p;
	char username[256];
	char location[256];
	char talkingto[256];

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_pthread_mutex_lock(&agentlock);
	p = agents;
	while(p) {
		ast_pthread_mutex_lock(&p->lock);
		if (strlen(p->name))
			snprintf(username, sizeof(username), "(%s) ", p->name);
		else
			strcpy(username, "");
		if (p->chan) {
			snprintf(location, sizeof(location), "logged in on %s", p->chan->name);
			if (p->owner && p->owner->bridge) {
				snprintf(talkingto, sizeof(talkingto), " talking to %s", p->owner->bridge->name);
			} else {
				strcpy(talkingto, " is idle");
			}
		} else {
			strcpy(location, "not logged in");
			strcpy(talkingto, "");
		}
		ast_pthread_mutex_unlock(&p->lock);
		ast_cli(fd, "%-12.12s %s%s%s\n", p->agent, 
				username, location, talkingto);
		p = p->next;
	}
	ast_pthread_mutex_unlock(&agentlock);
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

static int login_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int tries = 0;
	struct agent_pvt *p;
	struct localuser *u;
	char user[AST_MAX_AGENT];
	char pass[AST_MAX_AGENT];
	char xpass[AST_MAX_AGENT] = "";
	char *errmsg;
	char info[512];
	char *opt_user = NULL;
	char *options = NULL;
	int play_announcement;
	
	LOCAL_USER_ADD(u);

	/* Parse the arguments XXX Check for failure XXX */
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
	opt_user = info;
	if( opt_user ) {
		options = strchr(opt_user, '|');
		if (options) {
			*options = '\0';
			options++;
		}
	}

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res) {
		if( opt_user )
			strncpy( user, opt_user, AST_MAX_AGENT );
		else
			res = ast_app_getdata(chan, "agent-user", user, sizeof(user) - 1, 0);
	}
	while (!res && (tries < 3)) {
		/* Check for password */
		ast_pthread_mutex_lock(&agentlock);
		p = agents;
		while(p) {
			if (!strcmp(p->agent, user))
				strncpy(xpass, p->password, sizeof(xpass) - 1);
			p = p->next;
		}
		ast_pthread_mutex_unlock(&agentlock);
		if (!res) {
			if (strlen(xpass))
				res = ast_app_getdata(chan, "agent-pass", pass, sizeof(pass) - 1, 0);
			else
				strcpy(pass, "");
		}
		errmsg = "agent-incorrect";

#if 0
		ast_log(LOG_NOTICE, "user: %s, pass: %s\n", user, pass);
#endif		

		/* Check again for accuracy */
		ast_pthread_mutex_lock(&agentlock);
		p = agents;
		while(p) {
			ast_pthread_mutex_lock(&p->lock);
			if (!strcmp(p->agent, user) &&
				!strcmp(p->password, pass)) {
					if (!p->chan) {
						play_announcement = 1;
						if( options )
							if( strchr( options, 's' ) )
								play_announcement = 0;
						if( play_announcement )
							res = ast_streamfile(chan, "agent-loginok", chan->language);
						if (!res)
							ast_waitstream(chan, "");
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
						if (!res) {
							ast_moh_start(chan, p->moh);
							manager_event(EVENT_FLAG_AGENT, "Agentlogin",
								"Agent: %s\r\n"
								"Channel: %s\r\n",
								p->agent, chan->name);
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Agent '%s' logged in (format %d/%d)\n", p->agent,
												chan->readformat, chan->writeformat);
							/* Login this channel and wait for it to
							   go away */
							p->chan = chan;
							ast_pthread_mutex_unlock(&p->lock);
							ast_pthread_mutex_unlock(&agentlock);
							while (res >= 0) {
								ast_pthread_mutex_lock(&p->lock);
								if (p->chan != chan)
									res = -1;
								ast_pthread_mutex_unlock(&p->lock);
								/* Yield here so other interested threads can kick in. */
								sched_yield();
								if (res)
									break;

								/*	Synchronize channel ownership between call to agent and itself. */
								pthread_mutex_lock( &p->app_lock );
								ast_pthread_mutex_lock(&p->lock);
								p->owning_app = pthread_self();
								ast_pthread_mutex_unlock(&p->lock);
								res = ast_safe_sleep_conditional( chan, 1000,
														agent_cont_sleep, p );
								pthread_mutex_unlock( &p->app_lock );
								sched_yield();
							}
							ast_pthread_mutex_lock(&p->lock);
							if (res && p->owner) 
								ast_log(LOG_WARNING, "Huh?  We broke out when there was still an owner?\n");
							/* Log us off if appropriate */
							if (p->chan == chan)
								p->chan = NULL;
							ast_pthread_mutex_unlock(&p->lock);
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Agent '%s' logged out\n", p->agent);
							manager_event(EVENT_FLAG_AGENT, "Agentlogoff",
								"Agent: %s\r\n",
								p->agent);
							/* If there is no owner, go ahead and kill it now */
							if (p->dead && !p->owner)
								free(p);
						}
						else {
							ast_pthread_mutex_unlock(&p->lock);
							p = NULL;
						}
						res = -1;
					} else {
						ast_pthread_mutex_unlock(&p->lock);
						errmsg = "agent-alreadyon";
						p = NULL;
					}
					break;
			}
			ast_pthread_mutex_unlock(&p->lock);
			p = p->next;
		}
		if (!p)
			ast_pthread_mutex_unlock(&agentlock);

		if (!res)
			res = ast_app_getdata(chan, errmsg, user, sizeof(user) - 1, 0);
	}
		
	LOCAL_USER_REMOVE(u);
	/* Always hangup */
	return -1;
}


int load_module()
{
	/* Make sure we can register our sip channel type */
	if (ast_channel_register(type, tdesc, capability, agent_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	ast_register_application(app, login_exec, synopsis, descrip);
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
	ast_channel_unregister(type);
	if (!ast_pthread_mutex_lock(&agentlock)) {
		/* Hangup all interfaces if they have an owner */
		p = agents;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		agents = NULL;
		ast_pthread_mutex_unlock(&agentlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}		
	return 0;
}

int usecount()
{
	int res;
	ast_pthread_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_pthread_mutex_unlock(&usecnt_lock);
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

