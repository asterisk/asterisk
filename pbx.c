/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Core PBX routines.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <pthread.h>
#include <asterisk/pbx.h>
#include <asterisk/channel.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>
#include <asterisk/file.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <ctype.h>
#include "asterisk.h"

/*
 * I M P O R T A N T :
 *
 *		The speed of extension handling will likely be among the most important
 * aspects of this PBX.  The switching scheme as it exists right now isn't
 * terribly bad (it's O(N+M), where N is the # of extensions and M is the avg #
 * of priorities, but a constant search time here would be great ;-) 
 *
 */


struct ast_context;

struct ast_pbx {
	int dtimeout;					/* Timeout between digits (seconds) */
	int rtimeout;					/* Timeout for response (seconds) */
};

/* An extension */
struct ast_exten {
	char exten[AST_MAX_EXTENSION];
	int priority;
	/* An extension */
	struct ast_context *parent;
	/* Application to execute */
	char app[AST_MAX_EXTENSION];
	/* Data to use */
	void *data;
	/* Data destructor */
	void (*datad)(void *);
	/* Next highest priority with our extension */
	struct ast_exten *peer;
	/* Extension with a greater ID */
	struct ast_exten *next;
};

/* An extension context */
struct ast_context {
	/* Name of the context */
	char name[AST_MAX_EXTENSION];
	/* A lock to prevent multiple threads from clobbering the context */
	pthread_mutex_t lock;
	/* The root of the list of extensions */
	struct ast_exten *root;
	/* Link them together */
	struct ast_context *next;
};


/* An application */
struct ast_app {
	/* Name of the application */
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, void *data);
	struct ast_app *next;
};

static int pbx_builtin_answer(struct ast_channel *, void *);
static int pbx_builtin_goto(struct ast_channel *, void *);
static int pbx_builtin_hangup(struct ast_channel *, void *);
static int pbx_builtin_background(struct ast_channel *, void *);
static int pbx_builtin_dtimeout(struct ast_channel *, void *);
static int pbx_builtin_rtimeout(struct ast_channel *, void *);
static int pbx_builtin_wait(struct ast_channel *, void *);

static struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, void *data);
} builtins[] = 
{
	/* These applications are built into the PBX core and do not
	   need separate modules */
	{ "Answer", pbx_builtin_answer },
	{ "Goto", pbx_builtin_goto },
	{ "Hangup", pbx_builtin_hangup },
	{ "DigitTimeout", pbx_builtin_dtimeout },
	{ "ResponseTimeout", pbx_builtin_rtimeout },
	{ "BackGround", pbx_builtin_background },
	{ "Wait", pbx_builtin_wait },
};

/* Lock for the application list */
static pthread_mutex_t applock = PTHREAD_MUTEX_INITIALIZER;
static struct ast_context *contexts = NULL;
/* Lock for the ast_context list */
static pthread_mutex_t conlock = PTHREAD_MUTEX_INITIALIZER;
static struct ast_app *apps = NULL;

static int pbx_exec(struct ast_channel *c, /* Channel */
					int (*execute)(struct ast_channel *chan, void *data), 
					void *data,				/* Data for execution */
					int newstack)			/* Force stack increment */
{
	/* This function is special.  It saves the stack so that no matter
	   how many times it is called, it returns to the same place */
	int res;
	int stack = c->stack;
	if (newstack && stack > AST_CHANNEL_MAX_STACK - 2) {
		/* Don't allow us to go over the max number of stacks we
		   permit saving. */
		ast_log(LOG_WARNING, "Stack overflow, cannot create another stack\n");
		return -1;
	}
	if (newstack && (res = setjmp(c->jmp[++c->stack]))) {
		/* Okay, here's where it gets weird.  If newstack is non-zero, 
		   then we increase the stack increment, but setjmp is not going
		   to return until longjmp is called -- when the application
		   exec'd is finished running. */
		if (res == 1)
			res = 0;
		if (c->stack != stack + 1) 
			ast_log(LOG_WARNING, "Stack returned to an unexpected place!\n");
		else if (c->app[c->stack])
			ast_log(LOG_WARNING, "Application may have forgotten to free its memory\n");
		c->stack = stack;
		return res;
	} else {
		res = execute(c, data);
		/* Any application that returns, we longjmp back, just in case. */
		if (c->stack != stack + 1)
			ast_log(LOG_WARNING, "Stack is not at expected value\n");
		longjmp(c->jmp[stack+1], res);
		/* Never returns */
	}
}


#define HELPER_EXISTS 0
#define HELPER_SPAWN 1
#define HELPER_EXEC 2

static struct ast_app *pbx_findapp(char *app) 
{
	struct ast_app *tmp;
	if (pthread_mutex_lock(&applock)) {
		ast_log(LOG_WARNING, "Unable to obtain application lock\n");
		return NULL;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(tmp->name, app))
			break;
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&applock);
	return tmp;
}

static void pbx_destroy(struct ast_pbx *p)
{
	free(p);
}

static int extension_match(char *pattern, char *data)
{
	int match;
	/* If they're the same return */
	if (!strcasecmp(pattern, data))
		return 1;
	/* All patterns begin with _ */
	if (pattern[0] != '_') 
		return 0;
	/* Obviously must be the same length */
	if (strlen(pattern) != strlen(data) + 1)
		return 0;
	/* Start optimistic */
	match=1;
	pattern++;
	while(match && *data && *pattern) {
		switch(toupper(*pattern)) {
		case 'N':
			if ((*data < '2') || (*data > '9'))
				match=0;
			break;
		case 'X':
			if ((*data < '0') || (*data > '9'))
				match = 0;
			break;
		default:
			if (*data != *pattern)
				match =0;
		}
		data++;
		pattern++;
	}
	return match;
}

static int pbx_extension_helper(struct ast_channel *c, char *context, char *exten, int priority, int action) 
{
	struct ast_context *tmp;
	struct ast_exten *e;
	struct ast_app *app;
	int newstack = 0;
	int res;
	if (pthread_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		if (action == HELPER_EXISTS)
			return 0;
		else
			return -1;
	}
	tmp = contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, context)) {
			/* By locking tmp, not only can the state of its entries not
			   change, but it cannot be destroyed either. */
			pthread_mutex_lock(&tmp->lock);
			/* But we can relieve the conlock, as tmp will not change */
			pthread_mutex_unlock(&conlock);
			e = tmp->root;
			while(e) {
				if (extension_match(e->exten, exten)) {
					while(e) {
						if (e->priority == priority) {
							pthread_mutex_unlock(&tmp->lock);
							/* We have a winner! Maybe there are some races
							   in here though. XXX */
							switch(action) {
							case HELPER_EXISTS:
								return -1;
							case HELPER_SPAWN:
								newstack++;
								/* Fall through */
							case HELPER_EXEC:
								app = pbx_findapp(e->app);
								if (app) {
									strncpy(c->context, context, sizeof(c->context));
									strncpy(c->exten, exten, sizeof(c->exten));
									c->priority = priority;
									if (option_debug)
										ast_log(LOG_DEBUG, "Launching '%s'\n", app->name);
									else if (option_verbose > 2)
										ast_verbose( VERBOSE_PREFIX_3 "Executing %s(\"%s\", \"%s\") %s\n", 
												app->name, c->name, (e->data ? (char *)e->data : NULL), (newstack ? "in new stack" : "in same stack"));
									c->appl = app->name;
									c->data = e->data;		
									res = pbx_exec(c, app->execute, e->data, newstack);
									c->appl = NULL;
									c->data = NULL;
									return res;
								} else {
									ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
									return -1;
								}
							default:
								ast_log(LOG_WARNING, "Huh (%d)?\n", action);
							}
						}
						e = e->peer;
					}
					pthread_mutex_unlock(&tmp->lock);
					if (action != HELPER_EXISTS) {
						ast_log(LOG_WARNING, "No such priority '%d' in '%s' in '%s'\n", priority, exten, context);
						return -1;
					} else
						return 0;
				}
				e = e->next;
			}
			pthread_mutex_unlock(&tmp->lock);
			if (action != HELPER_EXISTS) {
				ast_log(LOG_WARNING, "No such extension '%s' in '%s'\n", exten, context);
				return -1;
			} else
				return 0;
		}
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&conlock);
	if (action != HELPER_EXISTS) {
		ast_log(LOG_WARNING, "No such context '%s'\n", context);
		return -1;
	} else
		return 0;
}
int ast_pbx_longest_extension(char *context) 
{
	struct ast_context *tmp;
	struct ast_exten *e;
	int len = 0;
	if (pthread_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		return -1;
	}
	tmp = contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, context)) {
			/* By locking tmp, not only can the state of its entries not
			   change, but it cannot be destroyed either. */
			pthread_mutex_lock(&tmp->lock);
			/* But we can relieve the conlock, as tmp will not change */
			pthread_mutex_unlock(&conlock);
			e = tmp->root;
			while(e) {
				if (strlen(e->exten) > len)
					len = strlen(e->exten);
				e = e->next;
			}
			pthread_mutex_unlock(&tmp->lock);
			return len;
		}
		tmp = tmp->next;
	}
	ast_log(LOG_WARNING, "No such context '%s'\n", context);
	return -1;
}

int ast_exists_extension(struct ast_channel *c, char *context, char *exten, int priority) 
{
	return pbx_extension_helper(c, context, exten, priority, HELPER_EXISTS);
}

int ast_spawn_extension(struct ast_channel *c, char *context, char *exten, int priority) 
{
	return pbx_extension_helper(c, context, exten, priority, HELPER_SPAWN);
}

static void *pbx_thread(void *data)
{
	/* Oh joyeous kernel, we're a new thread, with nothing to do but
	   answer this channel and get it going.  The setjmp stuff is fairly
	   confusing, but necessary to get smooth transitions between
	   the execution of different applications (without the use of
	   additional threads) */
	struct ast_channel *c = data;
	int firstpass = 1;
	char digit;
	char exten[256];
	int pos;
	int waittime;
	if (option_debug)
		ast_log(LOG_DEBUG, "PBX_THREAD(%s)\n", c->name);
	else if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Accepting call on '%s'\n", c->name);
		
	
	/* Start by trying whatever the channel is set to */
	if (!ast_exists_extension(c, c->context, c->exten, c->priority)) {
		strncpy(c->context, "default", sizeof(c->context));
		strncpy(c->exten, "s", sizeof(c->exten));
		c->priority = 1;
	}
	for(;;) {
		memset(exten, 0, sizeof(exten));
		pos = 0;
		digit = 0;
		while(ast_exists_extension(c, c->context, c->exten, c->priority)) {
			if (ast_spawn_extension(c, c->context, c->exten, c->priority)) {
				/* Something bad happened, or a hangup has been requested. */
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				else if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				goto out;
			}
			/* If we're playing something in the background, wait for it to finish or for a digit */
			if (c->stream) {
				digit = ast_waitstream(c, AST_DIGIT_ANY);
				ast_stopstream(c);
				/* Hang up if something goes wrong */
				if (digit < 0)
					goto out;
				else if (digit) {
					ast_stopstream(c);
					exten[pos++] = digit;
					break;
				}
			}
			firstpass = 0;
			c->priority++;
		}
		/* Done, wait for an extension */
		if (digit)
			waittime = c->pbx->dtimeout;
		else
			waittime = c->pbx->rtimeout;
		while(!ast_exists_extension(c, c->context, exten, 1) && (
		       strlen(exten) < ast_pbx_longest_extension(c->context))) {
			/* As long as we're willing to wait, and as long as it's not defined, 
			   keep reading digits until we can't possibly get a right answer anymore.  */
			digit = ast_waitfordigit(c, waittime * 1000);
			if (!digit)
				/* No entry */
				break;
			if (digit < 0)
				/* Error, maybe a  hangup */
				goto out;
			exten[pos++] = digit;
			waittime = c->pbx->dtimeout;
		}
		if (ast_exists_extension(c, c->context, exten, 1)) {
			/* Prepare the next cycle */
			strncpy(c->exten, exten, sizeof(c->exten));
			c->priority = 1;
		} else {
			/* No such extension */
			if (strlen(exten)) {
				/* An invalid extension */
				if (ast_exists_extension(c, c->context, "i", 1)) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Invalid extension '%s' in context '%s' on %s\n", exten, c->context, c->name);
					strncpy(c->exten, "i", sizeof(c->exten));
					c->priority = 1;
				} else {
					ast_log(LOG_WARNING, "Invalid extension, but no rule 'i' in context '%s'\n", c->context);
					goto out;
				}
			} else {
				/* A simple timeout */
				if (ast_exists_extension(c, c->context, "t", 1)) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Timeout on %s\n", c->name);
					strncpy(c->exten, "t", sizeof(c->exten));
					c->priority = 1;
				} else {
					ast_log(LOG_WARNING, "Timeout, but no rule 't' in context '%s'\n", c->context);
					goto out;
				}
			}	
		}
	}
	if (firstpass) 
		ast_log(LOG_WARNING, "Don't know what to do with '%s'\n", c->name);
out:
	pbx_destroy(c->pbx);
	c->pbx = NULL;
	ast_hangup(c);
	pthread_exit(NULL);
	
}

int ast_pbx_start(struct ast_channel *c)
{
	pthread_t t;
	if (!c) {
		ast_log(LOG_WARNING, "Asked to start thread on NULL channel?\n");
		return -1;
	}
	if (c->pbx)
		ast_log(LOG_WARNING, "%s already has PBX structure??\n");
	c->pbx = malloc(sizeof(struct ast_pbx));
	if (!c->pbx) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(c->pbx, 0, sizeof(struct ast_pbx));
	/* Set reasonable defaults */
	c->pbx->rtimeout = 10;
	c->pbx->dtimeout = 5;
	/* Start a new thread, and get something handling this channel. */
	if (pthread_create(&t, NULL, pbx_thread, c)) {
		ast_log(LOG_WARNING, "Failed to create new channel thread\n");
		return -1;
	}
	return 0;
}
#if 0
int ast_remove_extension(struct ast_context *con, char *extension, int priority)
{
	/* XXX Implement me XXX */
	return -1;
}
#endif
int ast_register_application(char *app, int (*execute)(struct ast_channel *, void *))
{
	struct ast_app *tmp;
	if (pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(app, tmp->name)) {
			ast_log(LOG_WARNING, "Already have an application '%s'\n", app);
			pthread_mutex_unlock(&applock);
			return -1;
		}
		tmp = tmp->next;
	}
	tmp = malloc(sizeof(struct ast_app));
	if (tmp) {
		strncpy(tmp->name, app, sizeof(tmp->name));
		tmp->execute = execute;
		tmp->next = apps;
		apps = tmp;
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		pthread_mutex_unlock(&applock);
		return -1;
	}
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered application '%s'\n", tmp->name);
	pthread_mutex_unlock(&applock);
	return 0;
}

int ast_unregister_application(char *app) {
	struct ast_app *tmp, *tmpl = NULL;
	if (pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(app, tmp->name)) {
			if (tmpl)
				tmpl->next = tmp->next;
			else
				apps = tmp->next;
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Unregistered application '%s'\n", tmp->name);
			pthread_mutex_unlock(&applock);
			return 0;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&applock);
	return -1;
}

struct ast_context *ast_context_create(char *name)
{
	struct ast_context *tmp;
	
	pthread_mutex_lock(&conlock);
	tmp = contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, name)) {
			pthread_mutex_unlock(&conlock);
			ast_log(LOG_WARNING, "Tried to register context '%s', already in use\n", name);
			return NULL;
		}
		tmp = tmp->next;
	}
	tmp = malloc(sizeof(struct ast_context));
	if (tmp) {
		pthread_mutex_init(&tmp->lock, NULL);
		strncpy(tmp->name, name, sizeof(tmp->name));
		tmp->root = NULL;
		tmp->next = contexts;
		contexts = tmp;
		if (option_debug)
			ast_log(LOG_DEBUG, "Registered context '%s'\n", tmp->name);
		else if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Registered extension context '%s'\n", tmp->name);
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	
	pthread_mutex_unlock(&conlock);
	return tmp;
}

int ast_add_extension2(struct ast_context *con,
					  int replace, char *extension, int priority,
					  char *application, void *data, void (*datad)(void *))
{

#define LOG { 	if (option_debug) \
		ast_log(LOG_DEBUG, "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
	else if (option_verbose > 2) \
		ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
		}

	/*
	 * This is a fairly complex routine.  Different extensions are kept
	 * in order by the extension number.  Then, extensions of different
	 * priorities (same extension) are kept in a list, according to the
	 * peer pointer.
	 */
	struct ast_exten *tmp, *e, *el = NULL, *ep = NULL;
	int res;
	/* Be optimistic:  Build the extension structure first */
	tmp = malloc(sizeof(struct ast_exten));
	if (tmp) {
		strncpy(tmp->exten, extension, sizeof(tmp->exten));
		tmp->priority = priority;
		strncpy(tmp->app, application, sizeof(tmp->app));
		tmp->data = data;
		tmp->datad = datad;
		tmp->peer = NULL;
		tmp->next =  NULL;
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	if (pthread_mutex_lock(&con->lock)) {
		free(tmp);
		/* And properly destroy the data */
		datad(data);
		ast_log(LOG_WARNING, "Failed to lock context '%s'\n", con->name);
		return -1;
	}
	e = con->root;
	while(e) {
		res= strcasecmp(e->exten, extension);
		if (res == 0) {
			/* We have an exact match, now we find where we are
			   and be sure there's no duplicates */
			while(e) {
				if (e->priority == tmp->priority) {
					/* Can't have something exactly the same.  Is this a
					   replacement?  If so, replace, otherwise, bonk. */
					if (replace) {
						if (ep) {
							/* We're in the peer list, insert ourselves */
							ep->peer = tmp;
							tmp->peer = e->peer;
						} else if (el) {
							/* We're the first extension. Take over e's functions */
							el->next = tmp;
							tmp->next = e->next;
							tmp->peer = e->peer;
						} else {
							/* We're the very first extension.  */
							con->root = tmp;
							tmp->next = e->next;
							tmp->peer = e->peer;
						}
						/* Destroy the old one */
						e->datad(e->data);
						free(e);
						pthread_mutex_unlock(&con->lock);
						/* And immediately return success. */
						LOG;
						return 0;
					} else {
						ast_log(LOG_WARNING, "Unable to register extension '%s', priority %d in '%s', already in use\n", tmp->exten, tmp->priority, con->name);
						tmp->datad(tmp->data);
						free(tmp);
						pthread_mutex_unlock(&con->lock);
						return -1;
					}
				} else if (e->priority > tmp->priority) {
					/* Slip ourselves in just before e */
					if (ep) {
						/* Easy enough, we're just in the peer list */
						ep->peer = tmp;
						tmp->peer = e;
					} else if (el) {
						/* We're the first extension in this peer list */
						el->next = tmp;
						tmp->next = e->next;
						e->next = NULL;
						tmp->peer = e;
					} else {
						/* We're the very first extension altogether */
						tmp->next = con->root;
						/* Con->root must always exist or we couldn't get here */
						tmp->peer = con->root->peer;
						con->root = tmp;
					}
					pthread_mutex_unlock(&con->lock);
					/* And immediately return success. */
					LOG;
					return 0;
				}
				ep = e;
				e = e->peer;
			}
			/* If we make it here, then it's time for us to go at the very end.
			   ep *must* be defined or we couldn't have gotten here. */
			ep->peer = tmp;
			pthread_mutex_unlock(&con->lock);
			/* And immediately return success. */
			LOG;
			return 0;
				
		} else if (res > 0) {
			/* Insert ourselves just before 'e'.  We're the first extension of
			   this kind */
			tmp->next = e;
			if (el) {
				/* We're in the list somewhere */
				el->next = tmp;
			} else {
				/* We're at the top of the list */
				con->root = tmp;
			}
			pthread_mutex_unlock(&con->lock);
			/* And immediately return success. */
			LOG;
			return 0;
		}			
			
		el = e;
		e = e->next;
	}
	/* If we fall all the way through to here, then we need to be on the end. */
	if (el)
		el->next = tmp;
	else
		con->root = tmp;
	pthread_mutex_unlock(&con->lock);
	LOG;
	return 0;	
}

void ast_context_destroy(struct ast_context *con)
{
	struct ast_context *tmp, *tmpl=NULL;
	pthread_mutex_lock(&conlock);
	tmp = contexts;
	while(tmp) {
		if (tmp == con) {
			/* Okay, let's lock the structure to be sure nobody else
			   is searching through it. */
			if (pthread_mutex_lock(&tmp->lock)) {
				ast_log(LOG_WARNING, "Unable to lock context lock\n");
				return;
			}
			if (tmpl)
				tmpl->next = tmp->next;
			else
				contexts = tmp->next;
			/* Okay, now we're safe to let it go -- in a sense, we were
			   ready to let it go as soon as we locked it. */
			pthread_mutex_unlock(&tmp->lock);
			free(tmp);
			pthread_mutex_unlock(&conlock);
			return;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	pthread_mutex_unlock(&conlock);
}

int pbx_builtin_answer(struct ast_channel *chan, void *data)
{
	if (chan->state != AST_STATE_RING) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Ignoring answer request since line is not ringing\n");
		return 0;
	} else
		return ast_answer(chan);
}

int pbx_builtin_hangup(struct ast_channel *chan, void *data)
{
	/* Just return non-zero and it will hang up */
	return -1;
}

int pbx_builtin_wait(struct ast_channel *chan, void *data)
{
	/* Wait for "n" seconds */
	if (data && atoi((char *)data))
		sleep(atoi((char *)data));
	return 0;
}

int pbx_builtin_background(struct ast_channel *chan, void *data)
{
	int res;
	/* Stop anything playing */
	ast_stopstream(chan);
	/* Stream a file */
	res = ast_streamfile(chan, (char *)data);
	return res;
}

int pbx_builtin_rtimeout(struct ast_channel *chan, void *data)
{
	/* Set the timeout for how long to wait between digits */
	chan->pbx->rtimeout = atoi((char *)data);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Response Timeout to %d\n", chan->pbx->rtimeout);
	return 0;
}

int pbx_builtin_dtimeout(struct ast_channel *chan, void *data)
{
	/* Set the timeout for how long to wait between digits */
	chan->pbx->dtimeout = atoi((char *)data);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Digit Timeout to %d\n", chan->pbx->dtimeout);
	return 0;
}

int pbx_builtin_goto(struct ast_channel *chan, void *data)
{
	char *s;
	char *exten, *pri, *context;
	if (!data) {
		ast_log(LOG_WARNING, "Goto requires an argument (optional context|optional extension|priority)\n");
		return -1;
	}
	s = strdup((void *) data);
	context = strtok(s, "|");
	exten = strtok(NULL, "|");
	if (!exten) {
		/* Only a priority in this one */
		pri = context;
		exten = NULL;
		context = NULL;
	} else {
		pri = strtok(NULL, "|");
		if (!pri) {
			/* Only an extension and priority in this one */
			pri = exten;
			exten = context;
			context = NULL;
		}
	}
	if (atoi(pri) < 0) {
		ast_log(LOG_WARNING, "Priority '%s' must be a number > 0\n", pri);
		free(s);
		return -1;
	}
	/* At this point we have a priority and maybe an extension and a context */
	chan->priority = atoi(pri) - 1;
	if (exten)
		strncpy(chan->exten, exten, sizeof(chan->exten));
	if (context)
		strncpy(chan->context, context, sizeof(chan->context));
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Goto (%s,%s,%d)\n", chan->context,chan->exten, chan->priority+1);
	return 0;
}
int load_pbx(void)
{
	int x;
	/* Initialize the PBX */
	if (option_verbose) {
		ast_verbose( "Asterisk PBX Core Initializing\n");
		ast_verbose( "Registering builtin applications:\n");
	}
	for (x=0;x<sizeof(builtins) / sizeof(struct pbx_builtin); x++) {
		if (option_verbose)
			ast_verbose( VERBOSE_PREFIX_1 "[%s]\n", builtins[x].name);
		if (ast_register_application(builtins[x].name, builtins[x].execute)) {
			ast_log(LOG_ERROR, "Unable to register builtin application '%s'\n", builtins[x].name);
			return -1;
		}
	}
	return 0;
}

