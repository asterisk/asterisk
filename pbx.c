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
#include <asterisk/cli.h>
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
	/* Next higher priority with our extension */
	struct ast_exten *peer;
	/* Registrar */
	char *registrar;
	/* Extension with a greater ID */
	struct ast_exten *next;
};

struct ast_include {
	char name[AST_MAX_EXTENSION];
	char *registrar;
	struct ast_include *next;
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
	/* Include other contexts */
	struct ast_include *includes;
	/* Registrar */
	char *registrar;
};


/* An application */
struct ast_app {
	/* Name of the application */
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, void *data);
	char *synopsis;
	char *description;
	struct ast_app *next;
};

static int pbx_builtin_prefix(struct ast_channel *, void *);
static int pbx_builtin_stripmsd(struct ast_channel *, void *);
static int pbx_builtin_answer(struct ast_channel *, void *);
static int pbx_builtin_goto(struct ast_channel *, void *);
static int pbx_builtin_hangup(struct ast_channel *, void *);
static int pbx_builtin_background(struct ast_channel *, void *);
static int pbx_builtin_dtimeout(struct ast_channel *, void *);
static int pbx_builtin_rtimeout(struct ast_channel *, void *);
static int pbx_builtin_wait(struct ast_channel *, void *);
static int pbx_builtin_setlanguage(struct ast_channel *, void *);
static int pbx_builtin_ringing(struct ast_channel *, void *);
static int pbx_builtin_congestion(struct ast_channel *, void *);
static int pbx_builtin_busy(struct ast_channel *, void *);

static struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, void *data);
	char *synopsis;
	char *description;
} builtins[] = 
{
	/* These applications are built into the PBX core and do not
	   need separate modules */
	{ "Answer", pbx_builtin_answer, 
			"Answer a channel if ringing", 
			"  Answer(): If the channel is ringing, answer it, otherwise do nothing.  Returns 0 unless it\n"
			"  tries to answer the channel and fails.\n"   },
	{ "Goto", pbx_builtin_goto, 
			"Goto a particular priority, extension, or context",
			"  Goto([[context|]extension|]priority): Set the priority to the specified value, optionally setting\n"
			"  the extension and optionally the context as well.  The extension BYEXTENSION is special in that it\n"
			"  uses the current extension, thus permitting you to go to a different context, without specifying a\n"
			"  specific extension.  Always returns 0, even if the given context, extension, or priority is invalid.\n" },
	{ "Hangup", pbx_builtin_hangup,
			"Unconditional hangup",
			"  Hangup(): Unconditionally hangs up a given channel by returning -1 always.\n" },
	{ "DigitTimeout", pbx_builtin_dtimeout,
			"Set maximum timeout between digits",
			"  DigitTimeout(seconds): Set the maximum amount of time permitted between digits when the user is\n" 
			"  typing in an extension.  When this timeout expires, after the user has started to type in an\n"
			"  extension, the extension will be considered complete, and will be interpreted.  Note that if an\n"
			"  extension typed in is valid, it will not have to timeout to be tested, so typically at the expiry\n"
			"  of this timeout, the extension will be considered invalid (and thus control would be passed to the\n"
			"  'i' extension, or if it doesn't exist the call would be terminated).  Always returns 0.\n" },
	{ "ResponseTimeout", pbx_builtin_rtimeout,
			"Set maximum timeout awaiting response",
			"  ResponseTimeout(seconds): Set the maximum amount of time permitted after falling through a series\n"
			"  of priorities for a channel in which the user may begin typing an extension.  If the user does not\n"
			"  type an extension in this amount of time, control will pass to the 't' extension if it exists, and\n"
			"  if not the call would be terminated.  Always returns 0.\n"  },
	{ "BackGround", pbx_builtin_background,
			"Play a file while awaiting extension",
			"  Background(filename): Plays a given file, while simultaneously waiting for the user to begin typing\n"
			"  an extension.  The timeouts do not count until the last BackGround application as ended.  Always\n"
			"  returns 0.\n" },
	{ "Wait", pbx_builtin_wait, 
			"Waits for some time", 
			"  Wait(seconds): Waits for a specified number of seconds, then returns 0.\n" },
	{ "StripMSD", pbx_builtin_stripmsd, "Strip leading digits",
			"  StripMSD(count): Strips the leading 'count' digits from the channel's associated extension.  For\n"
			"  example, the number 5551212 when stripped with a count of 3 would be changed to 1212.  This app\n"
			"  always returns 0, and the PBX will continue processing at the next priority for the *new* extension.\n"
			"  So, for example, if priority 3 of 5551212 is StripMSD 3, the next step executed will be priority 4 of\n"
			"  1212.  If you switch into an extension which has no first step, the PBX will treat it as though\n"
			"  the user dialed an invalid extension.\n" },
	{ "Prefix", pbx_builtin_prefix, "Prepend leading digits",
			"  Prefix(digits): Prepends the digit string specified by digits to the channel's associated\n"
			"  extension.  For example, the number 1212 when prefixed with '555' will become 5551212.  This app\n"
			"  always returns 0, and the PBX will continue processing at the next priority for the *new* extension.\n"
			"  So, for example, if priority 3 of 1212 is Prefix 555, the next step executed will be priority 4 of\n"
			"  5551212.  If you switch into an extension which has no first step, the PBX will treat it as though\n"
			"  the user dialed an invalid extension.\n" },
	{ "SetLanguage", pbx_builtin_setlanguage, "Sets user language",
			"  SetLanguage(language): Set the channel language to 'language'.  This information is used for the\n"
			"  generation of numbers, and to select a natural language file when available.  For example, if\n"
			"  language is set to 'fr' and the file 'demo-congrats' is requested to be played, if the file \n"
			"  'demo-congrats-fr' exists, then it will play that file, and if not will play the normal \n"
			"  'demo-congrats'.  Always returns 0.\n"  },
	{ "Ringing", pbx_builtin_ringing, "Indicate ringing tone",
			"  Ringing(): Request that the channel indicate ringing tone to the user.  Always returns 0.\n" },
	{ "Congestion", pbx_builtin_congestion, "Indicate congestion and stop",
			"  Congestion(): Requests that the channel indicate congestion and then waits for the user to\n"
			"  hang up.  Always returns -1." },
	{ "Busy", pbx_builtin_busy, "Indicate busy condition and stop",
			"  Busy(): Requests that the channel indicate busy condition and then waits for the user to\n"
			"  hang up.  Always returns -1." },
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


/* Go no deeper than this through includes (not counting loops) */
#define AST_PBX_MAX_STACK	64

#define HELPER_EXISTS 0
#define HELPER_SPAWN 1
#define HELPER_EXEC 2
#define HELPER_CANMATCH 3

static struct ast_app *pbx_findapp(char *app) 
{
	struct ast_app *tmp;
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_WARNING, "Unable to obtain application lock\n");
		return NULL;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(tmp->name, app))
			break;
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&applock);
	return tmp;
}

static void pbx_destroy(struct ast_pbx *p)
{
	free(p);
}

int ast_extension_match(char *pattern, char *data)
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

static int extension_close(char *pattern, char *data)
{
	int match;
	/* If "data" is longer, it can'be a subset of pattern */
	if (strlen(pattern) < strlen(data)) 
		return 0;
	
	
	if (!strlen((char *)data) || !strncasecmp(pattern, data, strlen(data))) {
		return 1;
	}
	/* All patterns begin with _ */
	if (pattern[0] != '_') 
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

struct ast_context *ast_context_find(char *name)
{
	struct ast_context *tmp;
	ast_pthread_mutex_lock(&conlock);
	if (name) {
		tmp = contexts;
		while(tmp) {
			if (!strcasecmp(name, tmp->name))
				break;
			tmp = tmp->next;
		}
	} else
		tmp = contexts;
	ast_pthread_mutex_unlock(&conlock);
	return tmp;
}

#define STATUS_NO_CONTEXT   1
#define STATUS_NO_EXTENSION 2
#define STATUS_NO_PRIORITY  3
#define STATUS_SUCCESS	    4

static struct ast_exten *pbx_find_extension(char *context, char *exten, int priority, int action, char *incstack[], int *stacklen, int *status)
{
	int x;
	struct ast_context *tmp;
	struct ast_exten *e, *eroot;
	struct ast_include *i;
	/* Initialize status if appropriate */
	if (!*stacklen)
		*status = STATUS_NO_CONTEXT;
	/* Check for stack overflow */
	if (*stacklen >= AST_PBX_MAX_STACK) {
		ast_log(LOG_WARNING, "Maximum PBX stack exceeded\n");
		return NULL;
	}
	/* Check first to see if we've already been checked */
	for (x=0;x<*stacklen;x++) {
		if (!strcasecmp(incstack[x], context))
			return NULL;
	}
	tmp = contexts;
	while(tmp) {
		/* Match context */
		if (!strcasecmp(tmp->name, context)) {
			if (*status < STATUS_NO_EXTENSION)
				*status = STATUS_NO_EXTENSION;
			eroot = tmp->root;
			while(eroot) {
				/* Match extension */
				if (ast_extension_match(eroot->exten, exten) ||
						((action == HELPER_CANMATCH) && (extension_close(eroot->exten, exten)))) {
						e = eroot;
						if (*status < STATUS_NO_PRIORITY)
							*status = STATUS_NO_PRIORITY;
						while(e) {
							/* Match priority */
							if (e->priority == priority) {
								*status = STATUS_SUCCESS;
								return e;
							}
							e = e->peer;
						}
				}
				eroot = eroot->next;
			}
			/* Setup the stack */
			incstack[*stacklen] = tmp->name;
			(*stacklen)++;
			/* Now try any includes we have in this context */
			i = tmp->includes;
			while(i) {
				if ((e = pbx_find_extension(i->name, exten, priority, action, incstack, stacklen, status))) 
					return e;
				i = i->next;
			}
		}
		tmp = tmp->next;
	}
	return NULL;
}

static int pbx_extension_helper(struct ast_channel *c, char *context, char *exten, int priority, int action) 
{
	struct ast_exten *e;
	struct ast_app *app;
	int newstack = 0;
	int res;
	int status = 0;
	char *incstack[AST_PBX_MAX_STACK];
	int stacklen = 0;
	if (ast_pthread_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		if ((action == HELPER_EXISTS) || (action == HELPER_CANMATCH))
			return 0;
		else
			return -1;
	}
	e = pbx_find_extension(context, exten, priority, action, incstack, &stacklen, &status);
	if (e) {
		switch(action) {
		case HELPER_CANMATCH:
			pthread_mutex_unlock(&conlock);
			return -1;
		case HELPER_EXISTS:
			pthread_mutex_unlock(&conlock);
			return -1;
		case HELPER_SPAWN:
			newstack++;
			/* Fall through */
		case HELPER_EXEC:
			app = pbx_findapp(e->app);
			pthread_mutex_unlock(&conlock);
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
				pthread_mutex_unlock(&conlock);
				return res;
			} else {
				ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
				return -1;
			}
		default:
			ast_log(LOG_WARNING, "Huh (%d)?\n", action);
			return -1;
		}
	} else {
		pthread_mutex_unlock(&conlock);
		switch(status) {
		case STATUS_NO_CONTEXT:
			if (action != HELPER_EXISTS)
				ast_log(LOG_NOTICE, "Cannot find extension context '%s'\n", context);
			break;
		case STATUS_NO_EXTENSION:
			if ((action != HELPER_EXISTS) && (action !=  HELPER_CANMATCH))
				ast_log(LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, context);
			break;
		case STATUS_NO_PRIORITY:
			if ((action != HELPER_EXISTS) && (action !=  HELPER_CANMATCH))
				ast_log(LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, context);
			break;
		default:
			ast_log(LOG_DEBUG, "Shouldn't happen!\n");
		}
		if ((action != HELPER_EXISTS) && (action != HELPER_CANMATCH))
			return -1;
		else
			return 0;
	}

#if 0		
	tmp = contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, context)) {
#if 0
			/* By locking tmp, not only can the state of its entries not
			   change, but it cannot be destroyed either. */
			ast_pthread_mutex_lock(&tmp->lock);
#endif
			e = tmp->root;
			while(e) {
				if (extension_match(e->exten, exten) || 
					((action == HELPER_CANMATCH) && extension_close(e->exten, exten))) {
					reale = e;
					while(e) {
						if (e->priority == priority) {
							/* We have a winner! Maybe there are some races
							   in here though. XXX */
							switch(action) {
							case HELPER_CANMATCH:
								ast_pthread_mutex_unlock(&conlock);
								return -1;
							case HELPER_EXISTS:
								ast_pthread_mutex_unlock(&conlock);
								return -1;
							case HELPER_SPAWN:
								newstack++;
								/* Fall through */
							case HELPER_EXEC:
								app = pbx_findapp(e->app);
								ast_pthread_mutex_unlock(&conlock);
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
									ast_pthread_mutex_unlock(&conlock);
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
					ast_pthread_mutex_unlock(&tmp->lock);
					if ((action != HELPER_EXISTS) && (action != HELPER_CANMATCH)) {
						ast_log(LOG_WARNING, "No such priority '%d' in '%s' in '%s'\n", priority, exten, context);
						ast_pthread_mutex_unlock(&conlock);
						return -1;
					} else if (action != HELPER_CANMATCH) {
						ast_pthread_mutex_unlock(&conlock);
						return 0;
					} else e = reale; /* Keep going */
				}
				e = e->next;
			}
			if ((action != HELPER_EXISTS) && (action != HELPER_CANMATCH)) {
				ast_pthread_mutex_unlock(&conlock);
				ast_log(LOG_WARNING, "No such extension '%s' in '%s'\n", exten, context);
				return -1;
			} else {
				ast_pthread_mutex_unlock(&conlock);
				return 0;
			}
		}
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&conlock);
	if (action != HELPER_EXISTS) {
		ast_log(LOG_WARNING, "No such context '%s'\n", context);
		return -1;
	} else
		return 0;
#endif
}

int ast_pbx_longest_extension(char *context) 
{
	/* XXX Not include-aware XXX */
	struct ast_context *tmp;
	struct ast_exten *e;
	int len = 0;
	if (ast_pthread_mutex_lock(&conlock)) {
		ast_log(LOG_WARNING, "Unable to obtain lock\n");
		return -1;
	}
	tmp = contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, context)) {
			/* By locking tmp, not only can the state of its entries not
			   change, but it cannot be destroyed either. */
			ast_pthread_mutex_lock(&tmp->lock);
			/* But we can relieve the conlock, as tmp will not change */
			ast_pthread_mutex_unlock(&conlock);
			e = tmp->root;
			while(e) {
				if (strlen(e->exten) > len)
					len = strlen(e->exten);
				e = e->next;
			}
			ast_pthread_mutex_unlock(&tmp->lock);
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

int ast_canmatch_extension(struct ast_channel *c, char *context, char *exten, int priority)
{
	return pbx_extension_helper(c, context, exten, priority, HELPER_CANMATCH);
}

int ast_spawn_extension(struct ast_channel *c, char *context, char *exten, int priority) 
{
	return pbx_extension_helper(c, context, exten, priority, HELPER_SPAWN);
}

int ast_pbx_run(struct ast_channel *c)
{
	int firstpass = 1;
	char digit;
	char exten[256];
	int pos;
	int waittime;
	int res=0;

	/* A little initial setup here */
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

	if (option_debug)
		ast_log(LOG_DEBUG, "PBX_THREAD(%s)\n", c->name);
	else if (option_verbose > 1) {
		if (c->callerid)
			ast_verbose( VERBOSE_PREFIX_2 "Accepting call on '%s' (%s)\n", c->name, c->callerid);
		else
			ast_verbose( VERBOSE_PREFIX_2 "Accepting call on '%s'\n", c->name);
	}
		
	
	/* Start by trying whatever the channel is set to */
	if (!ast_exists_extension(c, c->context, c->exten, c->priority)) {
		strncpy(c->context, "default", sizeof(c->context));
		strncpy(c->exten, "s", sizeof(c->exten));
		c->priority = 1;
	}
	for(;;) {
		pos = 0;
		digit = 0;
		while(ast_exists_extension(c, c->context, c->exten, c->priority)) {
			memset(exten, 0, sizeof(exten));
			if ((res = ast_spawn_extension(c, c->context, c->exten, c->priority))) {
				/* Something bad happened, or a hangup has been requested. */
				switch(res) {
				case AST_PBX_KEEPALIVE:
					if (option_debug)
						ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
					else if (option_verbose > 1)
						ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE on '%s'\n", c->context, c->exten, c->priority, c->name);
				break;
				default:
					if (option_debug)
						ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
					else if (option_verbose > 1)
						ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", c->context, c->exten, c->priority, c->name);
				}
				goto out;
			}
			if (c->softhangup) {
				ast_log(LOG_WARNING, "Extension %s, priority %d returned normally even though call was hung up\n",
					c->exten, c->priority);
				goto out;
			}
			/* If we're playing something in the background, wait for it to finish or for a digit */
			if (c->stream) {
				digit = ast_waitstream(c, AST_DIGIT_ANY);
				ast_stopstream(c);
				/* Hang up if something goes wrong */
				if (digit < 0) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Lost connection on %s\n", c->name);
					goto out;
				}
				else if (digit) {
					exten[pos++] = digit;
					break;
				}
			}
			firstpass = 0;
			c->priority++;
		}
		if (!ast_exists_extension(c, c->context, c->exten, 1)) {
			/* It's not a valid extension anymore */
			if (ast_exists_extension(c, c->context, "i", 1)) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Sent into invalid extension '%s' in context '%s' on %s\n", c->exten, c->context, c->name);
				strncpy(c->exten, "i", sizeof(c->exten));
				c->priority = 1;
			} else {
				ast_log(LOG_WARNING, "Channel '%s' sent into invalid extension '%s' in context '%s', but no invalid handler\n",
					c->name, c->exten, c->context);
				goto out;
			}
		} else {
			/* Done, wait for an extension */
			if (digit)
				waittime = c->pbx->dtimeout;
			else
				waittime = c->pbx->rtimeout;
			while(!ast_exists_extension(c, c->context, exten, 1) && 
			       ast_canmatch_extension(c, c->context, exten, 1)) {
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
	}
	if (firstpass) 
		ast_log(LOG_WARNING, "Don't know what to do with '%s'\n", c->name);
out:
	pbx_destroy(c->pbx);
	c->pbx = NULL;
	if (res != AST_PBX_KEEPALIVE)
		ast_hangup(c);
	return 0;
}

static void *pbx_thread(void *data)
{
	/* Oh joyeous kernel, we're a new thread, with nothing to do but
	   answer this channel and get it going.  The setjmp stuff is fairly
	   confusing, but necessary to get smooth transitions between
	   the execution of different applications (without the use of
	   additional threads) */
	struct ast_channel *c = data;
	ast_pbx_run(c);
	pthread_exit(NULL);
	return NULL;
}

int ast_pbx_start(struct ast_channel *c)
{
	pthread_t t;
	pthread_attr_t attr;
	if (!c) {
		ast_log(LOG_WARNING, "Asked to start thread on NULL channel?\n");
		return -1;
	}
	/* Start a new thread, and get something handling this channel. */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, pbx_thread, c)) {
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
int ast_register_application(char *app, int (*execute)(struct ast_channel *, void *), char *synopsis, char *description)
{
	struct ast_app *tmp;
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(app, tmp->name)) {
			ast_log(LOG_WARNING, "Already have an application '%s'\n", app);
			ast_pthread_mutex_unlock(&applock);
			return -1;
		}
		tmp = tmp->next;
	}
	tmp = malloc(sizeof(struct ast_app));
	if (tmp) {
		strncpy(tmp->name, app, sizeof(tmp->name));
		tmp->execute = execute;
		tmp->synopsis = synopsis;
		tmp->description = description;
		tmp->next = apps;
		apps = tmp;
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_pthread_mutex_unlock(&applock);
		return -1;
	}
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered application '%s'\n", tmp->name);
	ast_pthread_mutex_unlock(&applock);
	return 0;
}

static char app_help[] = 
"Usage: show application <application>\n"
"       Describes a particular application.\n";

static char apps_help[] =
"Usage: show applications\n"
"       List applications which are currently available.\n";

static char dialplan_help[] =
"Usage: show dialplan [[exten@]context]\n"
"       Displays dialplan.  Optionally takes a context (possibly preceeded by\n"
"       an extension) to limit the scope of the plan that is displayed.\n";

static int handle_show_applications(int fd, int argc, char *argv[])
{
	struct ast_app *tmp;
	char buf[256];
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	ast_cli(fd, "\n    -= Registered Asterisk Applications =-\n");
	while(tmp) {
		snprintf(buf, sizeof(buf), "  %15s: %s\n", tmp->name, tmp->synopsis ? tmp->synopsis : "<Synopsis not available>");
		ast_cli(fd, buf);
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&applock);
	return RESULT_SUCCESS;
}

static char *complete_app(char *line, char *word, int pos, int state)
{
	struct ast_app *tmp;
	char *ret;
	int which = 0;
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return NULL;
	}
	tmp = apps;
	while(tmp) {
		if (!strncasecmp(word, tmp->name, strlen(word))) {
			if (++which > state)
				break;
		}
		tmp = tmp->next;
	}
	if (tmp)
		ret = tmp->name;
	else
		ret = NULL;
	ast_pthread_mutex_unlock(&applock);
	
	return ret ? strdup(ret) : ret;
}

static char *complete_context(char *line, char *word, int pos, int state)
{
	struct ast_context *tmp;
	char *ret;
	int which = 0;
	if (ast_pthread_mutex_lock(&conlock)) {
		ast_log(LOG_ERROR, "Unable to lock context list\n");
		return NULL;
	}
	tmp = contexts;
	while(tmp) {
		if (!strncasecmp(word, tmp->name, strlen(word))) {
			if (++which > state)
				break;
		}
		tmp = tmp->next;
	}
	if (tmp)
		ret = tmp->name;
	else
		ret = NULL;
	ast_pthread_mutex_unlock(&conlock);
	
	return ret ? strdup(ret) : ret;
}

static int handle_show_application(int fd, int argc, char *argv[])
{
	struct ast_app *tmp;
	char buf[2048];
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}
	tmp = apps;
	while(tmp) {
		if (!strcasecmp(tmp->name, argv[2])) {
			snprintf(buf, sizeof(buf), "\n  -= About Application '%s' =- \n\n"
									   "[Synopsis]:\n  %s\n\n"
									   "[Description:]\n%s\n\n", tmp->name, tmp->synopsis ? 
									   		tmp->synopsis : "Not available", tmp->description ?
												tmp->description : "Not available\n");
			break;
		}
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&applock);
	if (!tmp) 
		snprintf(buf, sizeof(buf), "No such application '%s' is registered.\n", argv[2]);
	ast_cli(fd, buf);
	return RESULT_SUCCESS;
}

static int handle_dialplan(int fd, int argc, char *argv[])
{
	struct ast_context *con;
	struct ast_exten *eroot, *e;
	struct ast_include *inc;
	char tmp[512];
	char cpy[512];
	char tmp2[512];
	char *context;
	char *exten;
	int spaces;
	
	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;

	if (argc > 2) {
		strncpy(cpy, argv[2], sizeof(cpy));
		if ((context = strchr(cpy, '@'))) {
			*context = '\0';
			context++;
			exten = cpy;
		} else {
			context = cpy;
			exten = NULL;
		}
	} else {
		context = NULL;
		exten = NULL;
	}
	ast_pthread_mutex_lock(&conlock);
	con = contexts;
	while(con) {
		if (!context || (!strcasecmp(context, con->name))) {
			ast_cli(fd, "\n [ Context '%s' created by '%s']\n", con->name, con->registrar);
			eroot = con->root;
			while(eroot) {
				if (!exten || (!strcasecmp(exten, eroot->exten))) {
					memset(tmp, ' ', sizeof(tmp));
					snprintf(tmp, sizeof(tmp), "  '%s' => ", eroot->exten);
					spaces = strlen(tmp);
					tmp[spaces] = ' ';
					if (spaces < 19)
						spaces = 19;
					snprintf(tmp2, sizeof(tmp2), "%d. %s(%s)", eroot->priority, eroot->app, (char *)eroot->data);
					snprintf(tmp + spaces, sizeof(tmp) - spaces,     "%-45s [%s]\n", 
							tmp2, eroot->registrar);
					ast_cli(fd, tmp);
					memset(tmp, ' ', spaces);
					e = eroot->peer;
					while(e) {
						snprintf(tmp2, sizeof(tmp2), "%d. %s(%s)", e->priority, e->app, (char *)e->data);
						snprintf(tmp + spaces, sizeof(tmp) - spaces,     "%-45s [%s]\n", 
							tmp2, e->registrar);
						ast_cli(fd, tmp);
						e = e->peer;
					}
				}
				eroot = eroot->next;
			}
			inc = con->includes;
			while(inc) {
				snprintf(tmp, sizeof(tmp), "   Include =>    '%s'", inc->name);
				ast_cli(fd, "%s [%s]\n", tmp, inc->registrar);
				inc = inc->next;
			}
		}
		con = con->next;
	}
	ast_pthread_mutex_unlock(&conlock);
	return RESULT_SUCCESS;
}

static struct ast_cli_entry showapps = { { "show", "applications", NULL }, 
	handle_show_applications, "Shows registered applications", apps_help };

static struct ast_cli_entry showdialplan = { { "show", "dialplan", NULL }, 
	handle_dialplan, "Displays all or part of dialplan", dialplan_help, complete_context };

static struct ast_cli_entry showapp = { { "show", "application", NULL }, 
	handle_show_application, "Describe a specific application", app_help, complete_app };
	
int ast_unregister_application(char *app) {
	struct ast_app *tmp, *tmpl = NULL;
	if (ast_pthread_mutex_lock(&applock)) {
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
			ast_pthread_mutex_unlock(&applock);
			return 0;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&applock);
	return -1;
}

struct ast_context *ast_context_create(char *name, char *registrar)
{
	struct ast_context *tmp;
	
	ast_pthread_mutex_lock(&conlock);
	tmp = contexts;
	while(tmp) {
		if (!strcasecmp(tmp->name, name)) {
			ast_pthread_mutex_unlock(&conlock);
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
		tmp->registrar = registrar;
		tmp->next = contexts;
		tmp->includes = NULL;
		contexts = tmp;
		if (option_debug)
			ast_log(LOG_DEBUG, "Registered context '%s'\n", tmp->name);
		else if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Registered extension context '%s'\n", tmp->name);
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	
	ast_pthread_mutex_unlock(&conlock);
	return tmp;
}

int ast_context_add_include2(struct ast_context *con, char *value, char *registrar)
{
	struct ast_include *inc, *incc, *incl = NULL;
	inc = malloc(sizeof(struct ast_include));
	if (!inc) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	strncpy(inc->name, value, sizeof(inc->name));
	inc->next = NULL;
	inc->registrar = registrar;
	pthread_mutex_lock(&con->lock);
	incc = con->includes;
	while(incc) {
		incl = incc;
		if (!strcasecmp(incc->name, value)) {
			/* Already there */
			pthread_mutex_unlock(&con->lock);
			return 0;
		}
		incc = incc->next;
	}
	if (incl) 
		incl->next = inc;
	else
		con->includes = inc;
	pthread_mutex_unlock(&con->lock);
	return 0;
	
}

int ast_add_extension2(struct ast_context *con,
					  int replace, char *extension, int priority,
					  char *application, void *data, void (*datad)(void *),
					  char *registrar)
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
		tmp->registrar = registrar;
		tmp->peer = NULL;
		tmp->next =  NULL;
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	if (ast_pthread_mutex_lock(&con->lock)) {
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
						ast_pthread_mutex_unlock(&con->lock);
						/* And immediately return success. */
						LOG;
						return 0;
					} else {
						ast_log(LOG_WARNING, "Unable to register extension '%s', priority %d in '%s', already in use\n", tmp->exten, tmp->priority, con->name);
						tmp->datad(tmp->data);
						free(tmp);
						ast_pthread_mutex_unlock(&con->lock);
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
					ast_pthread_mutex_unlock(&con->lock);
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
			ast_pthread_mutex_unlock(&con->lock);
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
			ast_pthread_mutex_unlock(&con->lock);
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
	ast_pthread_mutex_unlock(&con->lock);
	LOG;
	return 0;	
}

void ast_context_destroy(struct ast_context *con, char *registrar)
{
	struct ast_context *tmp, *tmpl=NULL;
	struct ast_include *tmpi, *tmpil= NULL;
	ast_pthread_mutex_lock(&conlock);
	tmp = contexts;
	while(tmp) {
		if (((tmp == con) || !con) &&
		    (!registrar || !strcasecmp(registrar, tmp->registrar))) {
			/* Okay, let's lock the structure to be sure nobody else
			   is searching through it. */
			if (ast_pthread_mutex_lock(&tmp->lock)) {
				ast_log(LOG_WARNING, "Unable to lock context lock\n");
				return;
			}
			if (tmpl)
				tmpl->next = tmp->next;
			else
				contexts = tmp->next;
			/* Okay, now we're safe to let it go -- in a sense, we were
			   ready to let it go as soon as we locked it. */
			ast_pthread_mutex_unlock(&tmp->lock);
			for (tmpi = tmp->includes; tmpi; ) {
				/* Free includes */
				tmpil = tmpi;
				tmpi = tmpi->next;
				free(tmpil);
				tmpil = tmpi;
			}
			free(tmp);
			if (!con) {
				/* Might need to get another one -- restart */
				tmp = contexts;
				tmpl = NULL;
				tmpil = NULL;
				continue;
			}
			ast_pthread_mutex_unlock(&conlock);
			return;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&conlock);
}

static void wait_for_hangup(struct ast_channel *chan)
{
	int res;
	struct ast_frame *f;
	do {
		res = ast_waitfor(chan, -1);
		if (res < 0)
			return;
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
	} while(f);
}

static int pbx_builtin_ringing(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_RINGING);
	return 0;
}

static int pbx_builtin_busy(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_BUSY);		
	wait_for_hangup(chan);
	return -1;
}

static int pbx_builtin_congestion(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	wait_for_hangup(chan);
	return -1;
}

static int pbx_builtin_answer(struct ast_channel *chan, void *data)
{
	if (chan->state != AST_STATE_RING) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Ignoring answer request since line is not ringing\n");
		return 0;
	} else
		return ast_answer(chan);
}

static int pbx_builtin_setlanguage(struct ast_channel *chan, void *data)
{
	/* Copy the language as specified */
	strncpy(chan->language, (char *)data, sizeof(chan->language));
	return 0;
}

static int pbx_builtin_hangup(struct ast_channel *chan, void *data)
{
	/* Just return non-zero and it will hang up */
	return -1;
}

static int pbx_builtin_stripmsd(struct ast_channel *chan, void *data)
{
	char newexten[AST_MAX_EXTENSION] = "";
	if (!data || !atoi(data)) {
		ast_log(LOG_DEBUG, "Ignoring, since number of digits to strip is 0\n");
		return 0;
	}
	if (strlen(chan->exten) > atoi(data)) {
		strncpy(newexten, chan->exten + atoi(data), sizeof(newexten));
	}
	strncpy(chan->exten, newexten, sizeof(chan->exten));
	return 0;
}

static int pbx_builtin_prefix(struct ast_channel *chan, void *data)
{
	char newexten[AST_MAX_EXTENSION] = "";
	if (!data || !strlen(data)) {
		ast_log(LOG_DEBUG, "Ignoring, since there is no prefix to add\n");
		return 0;
	}
	snprintf(newexten, sizeof(newexten), "%s%s", (char *)data, chan->exten);
	strncpy(chan->exten, newexten, sizeof(chan->exten));
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Prepended prefix, new extension is %s\n", chan->exten);
	return 0;
}

static int pbx_builtin_wait(struct ast_channel *chan, void *data)
{
	/* Wait for "n" seconds */
	if (data && atoi((char *)data))
		sleep(atoi((char *)data));
	return 0;
}

static int pbx_builtin_background(struct ast_channel *chan, void *data)
{
	int res;
	/* Answer if need be */
	if (chan->state != AST_STATE_UP)
		if (ast_answer(chan))
			return -1;
	/* Stop anything playing */
	ast_stopstream(chan);
	/* Stream a file */
	res = ast_streamfile(chan, (char *)data, chan->language);
	return res;
}

static int pbx_builtin_rtimeout(struct ast_channel *chan, void *data)
{
	/* Set the timeout for how long to wait between digits */
	chan->pbx->rtimeout = atoi((char *)data);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Response Timeout to %d\n", chan->pbx->rtimeout);
	return 0;
}

static int pbx_builtin_dtimeout(struct ast_channel *chan, void *data)
{
	/* Set the timeout for how long to wait between digits */
	chan->pbx->dtimeout = atoi((char *)data);
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Set Digit Timeout to %d\n", chan->pbx->dtimeout);
	return 0;
}

static int pbx_builtin_goto(struct ast_channel *chan, void *data)
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
	if (exten && strcasecmp(exten, "BYEXTENSION"))
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
	ast_cli_register(&showapps);
	ast_cli_register(&showapp);
	ast_cli_register(&showdialplan);
	for (x=0;x<sizeof(builtins) / sizeof(struct pbx_builtin); x++) {
		if (option_verbose)
			ast_verbose( VERBOSE_PREFIX_1 "[%s]\n", builtins[x].name);
		if (ast_register_application(builtins[x].name, builtins[x].execute, builtins[x].synopsis, builtins[x].description)) {
			ast_log(LOG_ERROR, "Unable to register builtin application '%s'\n", builtins[x].name);
			return -1;
		}
	}
	return 0;
}

