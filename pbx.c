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
#include <asterisk/callerid.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <ctype.h>
#include <errno.h>
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
	int matchcid;
	char cidmatch[AST_MAX_EXTENSION];
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

struct ast_sw {
	char name[AST_MAX_EXTENSION];
	char *registrar;
	char data[AST_MAX_EXTENSION];
	struct ast_sw *next;
};

struct ast_ignorepat {
	char pattern[AST_MAX_EXTENSION];
	char *registrar;
	struct ast_ignorepat *next;
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
	/* Patterns for which to continue playing dialtone */
	struct ast_ignorepat *ignorepats;
	/* Registrar */
	char *registrar;
	/* Alternative switches */
	struct ast_sw *alts;
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
"  Answer(): If the channel is ringing, answer it, otherwise do nothing. \n"
"Returns 0 unless it tries to answer the channel and fails.\n"   },

	{ "Goto", pbx_builtin_goto, 
"Goto a particular priority, extension, or context",
"  Goto([[context|]extension|]priority):  Set the  priority to the specified\n"
"value, optionally setting the extension and optionally the context as well.\n"
"The extension BYEXTENSION is special in that it uses the current extension,\n"
"thus  permitting  you  to go to a different  context, without  specifying a\n"
"specific extension. Always returns 0, even if the given context, extension,\n"
"or priority is invalid.\n" },

	{ "Hangup", pbx_builtin_hangup,
"Unconditional hangup",
"  Hangup(): Unconditionally hangs up a given channel by returning -1 always.\n" },

	{ "DigitTimeout", pbx_builtin_dtimeout,
"Set maximum timeout between digits",
"  DigitTimeout(seconds): Set the  maximum  amount of time permitted between\n"
"digits when the user is typing in an extension.  When this timeout expires,\n"
"after the user has started to  type  in an extension, the extension will be\n"
"considered  complete, and  will be interpreted.  Note that if an  extension\n"
"typed in is valid, it will not have to timeout to be tested,  so  typically\n"
"at  the  expiry of  this timeout, the  extension will be considered invalid\n"
"(and  thus  control  would be passed to the 'i' extension, or if it doesn't\n"
"exist the call would be terminated).  Always returns 0.\n" },

	{ "ResponseTimeout", pbx_builtin_rtimeout,
"Set maximum timeout awaiting response",
"  ResponseTimeout(seconds): Set the maximum amount of time permitted after\n"
"falling through a series of priorities for a channel in which the user may\n"
"begin typing an extension.  If the user does not type an extension in this\n"
"amount of time, control will pass to the 't' extension if  it  exists, and\n"
"if not the call would be terminated.  Always returns 0.\n"  },

	{ "BackGround", pbx_builtin_background,
"Play a file while awaiting extension",
"  Background(filename): Plays a given file, while simultaneously waiting for\n"
"the user to begin typing an extension. The  timeouts  do not count until the\n"
"last BackGround application as ended. Always returns 0.\n" },

	{ "Wait", pbx_builtin_wait, 
"Waits for some time", 
"  Wait(seconds): Waits for a specified number of seconds, then returns 0.\n" },

	{ "StripMSD", pbx_builtin_stripmsd,
"Strip leading digits",
"  StripMSD(count): Strips the leading  'count'  digits  from  the  channel's\n"
"associated extension. For example, the  number  5551212 when stripped with a\n"
"count of 3 would be changed to 1212.  This app always returns 0, and the PBX\n"
"will continue processing at the next priority for the *new* extension.\n"
"  So, for  example, if  priority 3 of 5551212  is  StripMSD 3, the next step\n"
"executed will be priority 4 of 1212.  If you switch into an  extension which\n"
"has no first step, the PBX will treat it as though the user dialed an\n"
"invalid extension.\n" },

	{ "Prefix", pbx_builtin_prefix, 
"Prepend leading digits",
"  Prefix(digits): Prepends the  digit  string  specified  by  digits to the\n"
"channel's associated extension. For example, the number 1212 when  prefixed\n"
"with '555' will become 5551212. This app always returns 0, and the PBX will\n"
"continue processing at the next priority for the *new* extension.\n"
"  So, for example, if priority  3  of 1212 is  Prefix  555, the  next  step\n"
"executed will be priority 4 of 5551212. If  you  switch  into an  extension\n"
"which has no first step, the PBX will treat it as though the user dialed an\n"
"invalid extension.\n" },

	{ "SetLanguage", pbx_builtin_setlanguage,
"Sets user language",
"  SetLanguage(language):  Set  the  channel  language to 'language'.  This\n"
"information is used for the generation of numbers, and to select a natural\n"
"language file when available.  For example, if language is set to 'fr' and\n"
"the file 'demo-congrats' is requested  to  be  played,  if the file 'demo-\n"
"congrats-fr' exists, then it will play that file, and if not will play the\n"
"normal 'demo-congrats'. Always returns 0.\n"  },

	{ "Ringing", pbx_builtin_ringing,
"Indicate ringing tone",
"  Ringing(): Request that the channel indicate ringing tone to the user.\n"
"Always returns 0.\n" },

	{ "Congestion", pbx_builtin_congestion,
"Indicate congestion and stop",
"  Congestion(): Requests that the channel indicate congestion and then\n"
"waits for the user to hang up.  Always returns -1." },

	{ "Busy", pbx_builtin_busy,
"Indicate busy condition and stop",
"  Busy(): Requests that the channel indicate busy condition and then waits\n"
"for the user to hang up.  Always returns -1." },

};

/* Lock for the application list */
static pthread_mutex_t applock = PTHREAD_MUTEX_INITIALIZER;
static struct ast_context *contexts = NULL;
/* Lock for the ast_context list */
static pthread_mutex_t conlock = PTHREAD_MUTEX_INITIALIZER;
static struct ast_app *apps = NULL;

/* Lock for switches */
static pthread_mutex_t switchlock = PTHREAD_MUTEX_INITIALIZER;
struct ast_switch *switches = NULL;

int pbx_exec(struct ast_channel *c, /* Channel */
					struct ast_app *app,
					void *data,				/* Data for execution */
					int newstack)			/* Force stack increment */
{
	/* This function is special.  It saves the stack so that no matter
	   how many times it is called, it returns to the same place */
	int res;
	int stack = c->stack;
	int (*execute)(struct ast_channel *chan, void *data) = app->execute; 
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
		c->appl = app->name;
		c->data = data;		
		res = execute(c, data);
		c->appl = NULL;
		c->data = NULL;		
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

struct ast_app *pbx_findapp(char *app) 
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

static struct ast_switch *pbx_findswitch(char *sw)
{
	struct ast_switch *asw;
	if (ast_pthread_mutex_lock(&switchlock)) {
		ast_log(LOG_WARNING, "Unable to obtain application lock\n");
		return NULL;
	}
	asw = switches;
	while(asw) {
		if (!strcasecmp(asw->name, sw))
			break;
		asw = asw->next;
	}
	ast_pthread_mutex_unlock(&switchlock);
	return asw;
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
	/* Start optimistic */
	match=1;
	pattern++;
	while(match && *data && *pattern && (*pattern != '/')) {
		switch(toupper(*pattern)) {
		case 'N':
			if ((*data < '2') || (*data > '9'))
				match=0;
			break;
		case 'X':
			if ((*data < '0') || (*data > '9'))
				match = 0;
			break;
		case '.':
			/* Must match */
			return 1;
		case ' ':
		case '-':
			/* Ignore these characters */
			data--;
			break;
		default:
			if (*data != *pattern)
				match =0;
		}
		data++;
		pattern++;
	}
	/* Must be at the end of both */
	if (*data || (*pattern && (*pattern != '/')))
		match = 0;
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
	while(match && *data && *pattern && (*pattern != '/')) {
		switch(toupper(*pattern)) {
		case 'N':
			if ((*data < '2') || (*data > '9'))
				match=0;
			break;
		case 'X':
			if ((*data < '0') || (*data > '9'))
				match = 0;
			break;
		case '.':
			/* Must match */
			return 1;
		case ' ':
		case '-':
			/* Ignore these characters */
			data--;
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

static int matchcid(char *cidpattern, char *callerid)
{
	char tmp[AST_MAX_EXTENSION];
	int failresult;
	char *name, *num;
	
	/* If the Caller*ID pattern is empty, then we're matching NO Caller*ID, so
	   failing to get a number should count as a match, otherwise not */


	if (strlen(cidpattern))
		failresult = 0;
	else
		failresult = 1;

	if (!callerid)
		return failresult;

	/* Copy original Caller*ID */
	strncpy(tmp, callerid, sizeof(tmp));
	/* Parse Number */
	if (ast_callerid_parse(tmp, &name, &num)) 
		return failresult;
	if (!num)
		return failresult;
	ast_shrink_phone_number(num);
	return ast_extension_match(cidpattern, num);
}

static struct ast_exten *pbx_find_extension(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, int action, char *incstack[], int *stacklen, int *status, struct ast_switch **swo, char **data)
{
	int x, res;
	struct ast_context *tmp;
	struct ast_exten *e, *eroot;
	struct ast_include *i;
	struct ast_sw *sw;
	struct ast_switch *asw;
	/* Initialize status if appropriate */
	if (!*stacklen) {
		*status = STATUS_NO_CONTEXT;
		*swo = NULL;
		*data = NULL;
	}
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
				if ((ast_extension_match(eroot->exten, exten) ||
						((action == HELPER_CANMATCH) && (extension_close(eroot->exten, exten)))) &&
						(!eroot->matchcid || matchcid(eroot->cidmatch, callerid))) {
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
			/* Check alternative switches */
			sw = tmp->alts;
			while(sw) {
				if ((asw = pbx_findswitch(sw->name))) {
					if (action == HELPER_CANMATCH)
						res = asw->canmatch ? asw->canmatch(chan, context, exten, priority, callerid, sw->data) : 0;
					else
						res = asw->exists ? asw->exists(chan, context, exten, priority, callerid, sw->data) : 0;
					if (res) {
						/* Got a match */
						*swo = asw;
						*data = sw->data;
						return NULL;
					}
				} else {
					ast_log(LOG_WARNING, "No such switch '%s'\n", sw->name);
				}
				sw = sw->next;
			}
			/* Setup the stack */
			incstack[*stacklen] = tmp->name;
			(*stacklen)++;
			/* Now try any includes we have in this context */
			i = tmp->includes;
			while(i) {
				if ((e = pbx_find_extension(chan, i->name, exten, priority, callerid, action, incstack, stacklen, status, swo, data))) 
					return e;
				if (*swo) 
					return NULL;
				i = i->next;
			}
		}
		tmp = tmp->next;
	}
	return NULL;
}

static int pbx_extension_helper(struct ast_channel *c, char *context, char *exten, int priority, char *callerid, int action) 
{
	struct ast_exten *e;
	struct ast_app *app;
	struct ast_switch *sw;
	char *data;
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
	e = pbx_find_extension(c, context, exten, priority, callerid, action, incstack, &stacklen, &status, &sw, &data);
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
				res = pbx_exec(c, app, e->data, newstack);
				return res;
			} else {
				ast_log(LOG_WARNING, "No application '%s' for extension (%s, %s, %d)\n", e->app, context, exten, priority);
				return -1;
			}
		default:
			ast_log(LOG_WARNING, "Huh (%d)?\n", action);
			return -1;
		}
	} else if (sw) {
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
			pthread_mutex_unlock(&conlock);
			if (sw->exec)
				res = sw->exec(c, context, exten, priority, callerid, newstack, data);
			else {
				ast_log(LOG_WARNING, "No execution engine for switch %s\n", sw->name);
				res = -1;
			}
			return res;
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

}

#if 0
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
#endif

int ast_exists_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid) 
{
	return pbx_extension_helper(c, context, exten, priority, callerid, HELPER_EXISTS);
}

int ast_canmatch_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid)
{
	return pbx_extension_helper(c, context, exten, priority, callerid, HELPER_CANMATCH);
}

int ast_spawn_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid) 
{
	return pbx_extension_helper(c, context, exten, priority, callerid, HELPER_SPAWN);
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
	if (!ast_exists_extension(c, c->context, c->exten, c->priority, c->callerid)) {
		strncpy(c->context, "default", sizeof(c->context));
		strncpy(c->exten, "s", sizeof(c->exten));
		c->priority = 1;
	}
	for(;;) {
		pos = 0;
		digit = 0;
		while(ast_exists_extension(c, c->context, c->exten, c->priority, c->callerid)) {
			memset(exten, 0, sizeof(exten));
			if ((res = ast_spawn_extension(c, c->context, c->exten, c->priority, c->callerid))) {
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
		if (!ast_exists_extension(c, c->context, c->exten, 1, c->callerid)) {
			/* It's not a valid extension anymore */
			if (ast_exists_extension(c, c->context, "i", 1, c->callerid)) {
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
			while(!ast_exists_extension(c, c->context, exten, 1, c->callerid) && 
			       ast_canmatch_extension(c, c->context, exten, 1, c->callerid)) {
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
			if (ast_exists_extension(c, c->context, exten, 1, c->callerid)) {
				/* Prepare the next cycle */
				strncpy(c->exten, exten, sizeof(c->exten));
				c->priority = 1;
			} else {
				/* No such extension */
				if (strlen(exten)) {
					/* An invalid extension */
					if (ast_exists_extension(c, c->context, "i", 1, c->callerid)) {
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
					if (ast_exists_extension(c, c->context, "t", 1, c->callerid)) {
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

/*
 * This function locks contexts list by &conlist, search for the rigt context
 * structure, leave context list locked and call ast_context_remove_include2
 * which removes include, unlock contexts list and return ...
 */
int ast_context_remove_include(char *context, char *include, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) return -1;

	/* walk contexts and search for the right one ...*/
	c = ast_walk_contexts(NULL);
	while (c) {
		/* we found one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret;
			/* remove include from this context ... */	
			ret = ast_context_remove_include2(c, include, registrar);

			ast_unlock_contexts();

			/* ... return results */
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right one context */
	ast_unlock_contexts();
	return -1;
}

/*
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * This function locks given context, removes include, unlock context and
 * return.
 */
int ast_context_remove_include2(struct ast_context *con, char *include, char *registrar)
{
	struct ast_include *i, *pi = NULL;

	if (pthread_mutex_lock(&con->lock)) return -1;

	/* walk includes */
	i = con->includes;
	while (i) {
		/* find our include */
		if (!strcmp(i->name, include) && 
			(!strcmp(i->registrar, registrar) || !registrar)) {
			/* remove from list */
			if (pi)
				pi->next = i->next;
			else
				con->includes = i->next;
			/* free include and return */
			free(i);
			ast_pthread_mutex_unlock(&con->lock);
			return 0;
		}
		pi = i;
		i = i->next;
	}

	/* we can't find the right include */
	ast_pthread_mutex_unlock(&con->lock);
	return -1;
}

/*
 * This function locks contexts list by &conlist, search for the rigt context
 * structure, leave context list locked and call ast_context_remove_switch2
 * which removes switch, unlock contexts list and return ...
 */
int ast_context_remove_switch(char *context, char *sw, char *data, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) return -1;

	/* walk contexts and search for the right one ...*/
	c = ast_walk_contexts(NULL);
	while (c) {
		/* we found one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret;
			/* remove switch from this context ... */	
			ret = ast_context_remove_switch2(c, sw, data, registrar);

			ast_unlock_contexts();

			/* ... return results */
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right one context */
	ast_unlock_contexts();
	return -1;
}

/*
 * When we call this function, &conlock lock must be locked, because when
 * we giving *con argument, some process can remove/change this context
 * and after that there can be segfault.
 *
 * This function locks given context, removes switch, unlock context and
 * return.
 */
int ast_context_remove_switch2(struct ast_context *con, char *sw, char *data, char *registrar)
{
	struct ast_sw *i, *pi = NULL;

	if (pthread_mutex_lock(&con->lock)) return -1;

	/* walk switchs */
	i = con->alts;
	while (i) {
		/* find our switch */
		if (!strcmp(i->name, sw) && !strcmp(i->data, data) && 
			(!strcmp(i->registrar, registrar) || !registrar)) {
			/* remove from list */
			if (pi)
				pi->next = i->next;
			else
				con->alts = i->next;
			/* free switch and return */
			free(i);
			ast_pthread_mutex_unlock(&con->lock);
			return 0;
		}
		pi = i;
		i = i->next;
	}

	/* we can't find the right switch */
	ast_pthread_mutex_unlock(&con->lock);
	return -1;
}

/*
 * This functions lock contexts list, search for the right context,
 * call ast_context_remove_extension2, unlock contexts list and return.
 * In this function we are using
 */
int ast_context_remove_extension(char *context, char *extension, int priority, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) return -1;

	/* walk contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* ... search for the right one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			/* ... remove extension ... */
			int ret = ast_context_remove_extension2(c, extension, priority,
				registrar);
			/* ... unlock contexts list and return */
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right context */
	ast_unlock_contexts();
	return -1;
}

/*
 * When do you want to call this function, make sure that &conlock is locked,
 * because some process can handle with your *con context before you lock
 * it.
 *
 * This functionc locks given context, search for the right extension and
 * fires out all peer in this extensions with given priority. If priority
 * is set to 0, all peers are removed. After that, unlock context and
 * return.
 */
int ast_context_remove_extension2(struct ast_context *con, char *extension, int priority, char *registrar)
{
	struct ast_exten *exten, *prev_exten = NULL;

	if (ast_pthread_mutex_lock(&con->lock)) return -1;

	/* go through all extensions in context and search the right one ... */
	exten = con->root;
	while (exten) {

		/* look for right extension */
		if (!strcmp(exten->exten, extension) &&
			(!strcmp(exten->registrar, registrar) || !registrar)) {
			struct ast_exten *peer;

			/* should we free all peers in this extension? (priority == 0)? */
			if (priority == 0) {
				/* remove this extension from context list */
				if (prev_exten)
					prev_exten->next = exten->next;
				else
					con->root = exten->next;

				/* fire out all peers */
				peer = exten; 
				while (peer) {
					exten = peer->peer;

					peer->datad(peer->data);
					free(peer);

					peer = exten;
				}

				ast_pthread_mutex_unlock(&con->lock);
				return 0;
			} else {
				/* remove only extension with exten->priority == priority */
				struct ast_exten *previous_peer = NULL;

				peer = exten;
				while (peer) {
					/* is this our extension? */
					if (peer->priority == priority &&
						(!strcmp(peer->registrar, registrar) || !registrar)) {
						/* we are first priority extension? */
						if (!previous_peer) {
							/* exists previous extension here? */
							if (prev_exten) {
								/* yes, so we must change next pointer in
								 * previous connection to next peer
								 */
								if (peer->peer) {
									prev_exten->next = peer->peer;
									peer->peer->next = exten->next;
								} else
									prev_exten->next = exten->next;
							} else {
								/* no previous extension, we are first
								 * extension, so change con->root ...
								 */
								if (peer->peer)
									con->root = peer->peer;
								else
									con->root = exten->next; 
							}
						} else {
							/* we are not first priority in extension */
							previous_peer->peer = peer->peer;
						}

						/* now, free whole priority extension */
						peer->datad(peer->data);
						free(peer);

						ast_pthread_mutex_unlock(&con->lock);
						return 0;
					} else {
						/* this is not right extension, skip to next peer */
						previous_peer = peer;
						peer = peer->peer;
					}
				}

				ast_pthread_mutex_unlock(&con->lock);
				return -1;
			}
		}

		prev_exten = exten;
		exten = exten->next;
	}

	/* we can't find right extension */
	ast_pthread_mutex_unlock(&con->lock);
	return -1;
}


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

int ast_register_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp, *prev=NULL;
	if (ast_pthread_mutex_lock(&switchlock)) {
		ast_log(LOG_ERROR, "Unable to lock switch lock\n");
		return -1;
	}
	tmp = switches;
	while(tmp) {
		if (!strcasecmp(tmp->name, sw->name))
			break;
		prev = tmp;
		tmp = tmp->next;
	}
	if (tmp) {	
		ast_pthread_mutex_unlock(&switchlock);
		ast_log(LOG_WARNING, "Switch '%s' already found\n", sw->name);
		return -1;
	}
	sw->next = NULL;
	if (prev) 
		prev->next = sw;
	else
		switches = sw;
	ast_pthread_mutex_unlock(&switchlock);
	return 0;
}

void ast_unregister_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp, *prev=NULL;
	if (ast_pthread_mutex_lock(&switchlock)) {
		ast_log(LOG_ERROR, "Unable to lock switch lock\n");
		return;
	}
	tmp = switches;
	while(tmp) {
		if (tmp == sw) {
			if (prev)
				prev->next = tmp->next;
			else
				switches = tmp->next;
			tmp->next = NULL;
			break;			
		}
		prev = tmp;
		tmp = tmp->next;
	}
	ast_pthread_mutex_unlock(&switchlock);
}

/*
 * Help for CLI commands ...
 */
static char show_application_help[] = 
"Usage: show application <application> [<application> [<application> [...]]]\n"
"       Describes a particular application.\n";

static char show_applications_help[] =
"Usage: show applications\n"
"       List applications which are currently available.\n";

static char show_dialplan_help[] =
"Usage: show dialplan [exten@][context]\n"
"       Show dialplan\n";

static char show_switches_help[] = 
"Usage: show switches\n"
"       Show registered switches\n";

/*
 * IMPLEMENTATION OF CLI FUNCTIONS IS IN THE SAME ORDER AS COMMANDS HELPS
 *
 */

/*
 * 'show application' CLI command implementation functions ...
 */

/*
 * There is a possibility to show informations about more than one
 * application at one time. You can type 'show application Dial Echo' and
 * you will see informations about these two applications ...
 */
static char *complete_show_application(char *line, char *word,
	int pos, int state)
{
	struct ast_app *a;
	int which = 0;

	/* try to lock applications list ... */
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return NULL;
	}

	/* ... walk all applications ... */
	a = apps; 
	while (a) {
		/* ... check if word matches this application ... */
		if (!strncasecmp(word, a->name, strlen(word))) {
			/* ... if this is right app serve it ... */
			if (++which > state) {
				char *ret = strdup(a->name);
				ast_pthread_mutex_unlock(&applock);
				return ret;
			}
		}
		a = a->next; 
	}

	/* no application match */
	ast_pthread_mutex_unlock(&applock);
	return NULL; 
}

static int handle_show_application(int fd, int argc, char *argv[])
{
	struct ast_app *a;
	char buf[2048];
	int app, no_registered_app = 1;

	if (argc < 3) return RESULT_SHOWUSAGE;

	/* try to lock applications list ... */
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}

	/* ... go through all applications ... */
	a = apps; 
	while (a) {
		/* ... compare this application name with all arguments given
		 * to 'show application' command ... */
		for (app = 2; app < argc; app++) {
			if (!strcasecmp(a->name, argv[app])) {
				no_registered_app = 0;

				/* ... one of our applications, show info ...*/
				snprintf(buf, sizeof(buf),
					"\n  -= Info about application '%s' =- \n\n"
					"[Synopsis]:\n  %s\n\n"
					"[Description]:\n%s\n",
					a->name,
					a->synopsis ? a->synopsis : "Not available",
					a->description ? a-> description : "Not available");
				ast_cli(fd, buf);
			}
		}
		a = a->next; 
	}

	ast_pthread_mutex_unlock(&applock);

	/* we found at least one app? no? */
	if (no_registered_app) {
		ast_cli(fd, "Your application(s) is (are) not registered\n");
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static int handle_show_switches(int fd, int argc, char *argv[])
{
	struct ast_switch *sw;
	if (!switches) {
		ast_cli(fd, "There are no registered alternative switches\n");
		return RESULT_SUCCESS;
	}
	/* ... we have applications ... */
	ast_cli(fd, "\n    -= Registered Asterisk Alternative Switches =-\n");
	if (ast_pthread_mutex_lock(&switchlock)) {
		ast_log(LOG_ERROR, "Unable to lock switches\n");
		return -1;
	}
	sw = switches;
	while (sw) {
		ast_cli(fd, "%s: %s\n", sw->name, sw->description);
		sw = sw->next;
	}
	ast_pthread_mutex_unlock(&switchlock);
	return RESULT_SUCCESS;
}

/*
 * 'show applications' CLI command implementation functions ...
 */
static int handle_show_applications(int fd, int argc, char *argv[])
{
	struct ast_app *a;

	/* try to lock applications list ... */
	if (ast_pthread_mutex_lock(&applock)) {
		ast_log(LOG_ERROR, "Unable to lock application list\n");
		return -1;
	}

	/* ... go to first application ... */
	a = apps; 

	/* ... have we got at least one application (first)? no? */
	if (!a) {
		ast_cli(fd, "There is no registered applications\n");
		ast_pthread_mutex_unlock(&applock);
		return -1;
	}

	/* ... we have applications ... */
	ast_cli(fd, "\n    -= Registered Asterisk Applications =-\n");

	/* ... go through all applications ... */
	while (a) {
		/* ... show informations about applications ... */
		ast_cli(fd,"  %15s: %s\n",
			a->name,
			a->synopsis ? a->synopsis : "<Synopsis not available>");
		a = a->next; 
	}

	/* ... unlock and return */
	ast_pthread_mutex_unlock(&applock);

	return RESULT_SUCCESS;
}

/*
 * 'show dialplan' CLI command implementation functions ...
 */
static char *complete_show_dialplan_context(char *line, char *word, int pos,
	int state)
{
	struct ast_context *c;
	int which = 0;

	/* we are do completion of [exten@]context on second postion only */
	if (pos != 2) return NULL;

	/* try to lock contexts list ... */
	if (ast_lock_contexts()) {
		ast_log(LOG_ERROR, "Unable to lock context list\n");
		return NULL;
	}

	/* ... walk through all contexts ... */
	c = ast_walk_contexts(NULL);
	while(c) {
		/* ... word matches context name? yes? ... */
		if (!strncasecmp(word, ast_get_context_name(c), strlen(word))) {
			/* ... for serve? ... */
			if (++which > state) {
				/* ... yes, serve this context name ... */
				char *ret = strdup(ast_get_context_name(c));
				ast_unlock_contexts();
				return ret;
			}
		}
		c = ast_walk_contexts(c);
	}

	/* ... unlock and return */
	ast_unlock_contexts();
	return NULL;
}

static int handle_show_dialplan(int fd, int argc, char *argv[])
{
	struct ast_context *c;
	char *exten = NULL, *context = NULL;
	int context_existence = 0, extension_existence = 0;

	if (argc != 3 && argc != 2) return -1;

	/* we obtain [exten@]context? if yes, split them ... */
	if (argc == 3) {
		char *splitter = argv[2];
		/* is there a '@' character? */
		if (strchr(argv[2], '@')) {
			/* yes, split into exten & context ... */
			exten   = strsep(&splitter, "@");
			context = splitter;

			/* check for length and change to NULL if !strlen() */
			if (!strlen(exten))   exten = NULL;
			if (!strlen(context)) context = NULL;
		} else
		{
			/* no '@' char, only context given */
			context = argv[2];
			if (!strlen(context)) context = NULL;
		}
	}

	/* try to lock contexts */
	if (ast_lock_contexts()) {
		ast_cli(LOG_WARNING, "Failed to lock contexts list\n");
		return RESULT_FAILURE;
	}

	/* walk all contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* show this context? */
		if (!context ||
			!strcmp(ast_get_context_name(c), context)) {
			context_existence = 1;

			/* try to lock context before walking in ... */
			if (!ast_lock_context(c)) {
				struct ast_exten *e;
				struct ast_include *i;
				struct ast_ignorepat *ip;
				struct ast_sw *sw;
				char buf[256], buf2[256];
				int context_info_printed = 0;

				/* are we looking for exten too? if yes, we print context
				 * if we our extension only
				 */
				if (!exten) {
					ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
						ast_get_context_name(c), ast_get_context_registrar(c));
					context_info_printed = 1;
				}

				/* walk extensions ... */
				e = ast_walk_context_extensions(c, NULL);
				while (e) {
					struct ast_exten *p;

					/* looking for extension? is this our extension? */
					if (exten &&
						strcmp(ast_get_extension_name(e), exten))
					{
						/* we are looking for extension and it's not our
 						 * extension, so skip to next extension */
						e = ast_walk_context_extensions(c, e);
						continue;
					}

					extension_existence = 1;

					/* may we print context info? */	
					if (!context_info_printed) {
						ast_cli(fd, "[ Context '%s' created by '%s' ]\n",
							ast_get_context_name(c),
							ast_get_context_registrar(c));
						context_info_printed = 1;
					}

					/* write extension name and first peer */	
					bzero(buf, sizeof(buf));		
					snprintf(buf, sizeof(buf), "'%s' =>",
						ast_get_extension_name(e));

					snprintf(buf2, sizeof(buf2),
						"%d. %s(%s)",
						ast_get_extension_priority(e),
						ast_get_extension_app(e),
						(char *)ast_get_extension_app_data(e));

					ast_cli(fd, "  %-17s %-45s [%s]\n", buf, buf2,
						ast_get_extension_registrar(e));

					/* walk next extension peers */
					p = ast_walk_extension_priorities(e, e);
					while (p) {
						bzero((void *)buf2, sizeof(buf2));

						snprintf(buf2, sizeof(buf2),
							"%d. %s(%s)",
							ast_get_extension_priority(p),
							ast_get_extension_app(p),
							(char *)ast_get_extension_app_data(p));

						ast_cli(fd,"  %-17s %-45s [%s]\n",
							"", buf2,
							ast_get_extension_registrar(p));	

						p = ast_walk_extension_priorities(e, p);
					}
					e = ast_walk_context_extensions(c, e);
				}

				/* include & ignorepat we all printing if we are not
				 * looking for exact extension
				 */
				if (!exten) {
					if (ast_walk_context_extensions(c, NULL))
						ast_cli(fd, "\n");

					/* walk included and write info ... */
					i = ast_walk_context_includes(c, NULL);
					while (i) {
						bzero(buf, sizeof(buf));
						snprintf(buf, sizeof(buf), "'%s'",
							ast_get_include_name(i));
						ast_cli(fd, "  Include =>        %-45s [%s]\n",
							buf, ast_get_include_registrar(i));
						i = ast_walk_context_includes(c, i);
					}

					/* walk ignore patterns and write info ... */
					ip = ast_walk_context_ignorepats(c, NULL);
					while (ip) {
						bzero(buf, sizeof(buf));
						snprintf(buf, sizeof(buf), "'%s'",
							ast_get_ignorepat_name(ip));
						ast_cli(fd, "  Ignore pattern => %-45s [%s]\n",
							buf, ast_get_ignorepat_registrar(ip));	
						ip = ast_walk_context_ignorepats(c, ip);
					}
					sw = ast_walk_context_switches(c, NULL);
					while(sw) {
						bzero(buf, sizeof(buf));
						snprintf(buf, sizeof(buf), "'%s/%s'",
							ast_get_switch_name(sw),
							ast_get_switch_data(sw));
						ast_cli(fd, "  Alt. Switch =>    %-45s [%s]\n",
							buf, ast_get_switch_registrar(sw));	
						sw = ast_walk_context_switches(c, sw);
					}
				}
	
				ast_unlock_context(c);

				/* if we print something in context, make an empty line */
				if (context_info_printed) ast_cli(fd, "\n");
			}
		}
		c = ast_walk_contexts(c);
	}
	ast_unlock_contexts();

	/* check for input failure and throw some error messages */
	if (context && !context_existence) {
		ast_cli(fd, "There is no existence of '%s' context\n",
			context);
		return RESULT_FAILURE;
	}

	if (exten && !extension_existence) {
		if (context)
			ast_cli(fd, "There is no existence of %s@%s extension\n",
				exten, context);
		else
			ast_cli(fd,
				"There is no existence of '%s' extension in all contexts\n",
				exten);
		return RESULT_FAILURE;
	}

	/* everything ok */
	return RESULT_SUCCESS;
}

/*
 * CLI entries for upper commands ...
 */
static struct ast_cli_entry show_applications_cli = 
	{ { "show", "applications", NULL }, 
	handle_show_applications, "Shows registered applications",
	show_applications_help };

static struct ast_cli_entry show_application_cli =
	{ { "show", "application", NULL }, 
	handle_show_application, "Describe a specific application",
	show_application_help, complete_show_application };

static struct ast_cli_entry show_dialplan_cli =
	{ { "show", "dialplan", NULL },
		handle_show_dialplan, "Show dialplan",
		show_dialplan_help, complete_show_dialplan_context };

static struct ast_cli_entry show_switches_cli =
	{ { "show", "switches", NULL },
		handle_show_switches, "Show alternative switches",
		show_switches_help, NULL };

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
		memset(tmp, 0, sizeof(struct ast_context));
		pthread_mutex_init(&tmp->lock, NULL);
		strncpy(tmp->name, name, sizeof(tmp->name));
		tmp->root = NULL;
		tmp->registrar = registrar;
		tmp->next = contexts;
		tmp->includes = NULL;
		tmp->ignorepats = NULL;
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

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENODATA - no existence of context
 */
int ast_context_add_include(char *context, char *include, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	/* walk contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* ... search for the right one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret = ast_context_add_include2(c, include, registrar);
			/* ... unlock contexts list and return */
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right context */
	ast_unlock_contexts();
	errno = ENODATA;
	return -1;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int ast_context_add_include2(struct ast_context *con, char *value,
	char *registrar)
{
	struct ast_include *new_include;
	struct ast_include *i, *il = NULL; /* include, include_last */

	/* allocate new include structure ... */
	if (!(new_include = malloc(sizeof(struct ast_include)))) {
		ast_log(LOG_WARNING, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	
	/* ... fill in this structure ... */
	strncpy(new_include->name, value, sizeof(new_include->name));
	new_include->next      = NULL;
	new_include->registrar = registrar;

	/* ... try to lock this context ... */
	if (ast_pthread_mutex_lock(&con->lock)) {
		free(new_include);
		errno = EBUSY;
		return -1;
	}

	/* ... go to last include and check if context is already included too... */
	i = con->includes;
	while (i) {
		if (!strcasecmp(i->name, new_include->name)) {
			free(new_include);
			ast_pthread_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		il = i;
		i = i->next;
	}

	/* ... include new context into context list, unlock, return */
	if (il)
		il->next = new_include;
	else
		con->includes = new_include;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Including context '%s' in context '%s'\n", new_include->name, ast_get_context_name(con)); 
	ast_pthread_mutex_unlock(&con->lock);

	return 0;
}

/*
 * errno values
 *  EBUSY  - can't lock
 *  ENODATA - no existence of context
 */
int ast_context_add_switch(char *context, char *sw, char *data, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	/* walk contexts ... */
	c = ast_walk_contexts(NULL);
	while (c) {
		/* ... search for the right one ... */
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret = ast_context_add_switch2(c, sw, data, registrar);
			/* ... unlock contexts list and return */
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	/* we can't find the right context */
	ast_unlock_contexts();
	errno = ENODATA;
	return -1;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
int ast_context_add_switch2(struct ast_context *con, char *value,
	char *data, char *registrar)
{
	struct ast_sw *new_sw;
	struct ast_sw *i, *il = NULL; /* sw, sw_last */

	/* allocate new sw structure ... */
	if (!(new_sw = malloc(sizeof(struct ast_sw)))) {
		ast_log(LOG_WARNING, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	
	/* ... fill in this structure ... */
	strncpy(new_sw->name, value, sizeof(new_sw->name));
	if (data)
		strncpy(new_sw->data, data, sizeof(new_sw->data));
	else
		strncpy(new_sw->data, "", sizeof(new_sw->data));
	new_sw->next      = NULL;
	new_sw->registrar = registrar;

	/* ... try to lock this context ... */
	if (ast_pthread_mutex_lock(&con->lock)) {
		free(new_sw);
		errno = EBUSY;
		return -1;
	}

	/* ... go to last sw and check if context is already swd too... */
	i = con->alts;
	while (i) {
		if (!strcasecmp(i->name, new_sw->name)) {
			free(new_sw);
			ast_pthread_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		il = i;
		i = i->next;
	}

	/* ... sw new context into context list, unlock, return */
	if (il)
		il->next = new_sw;
	else
		con->alts = new_sw;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Including switch '%s/%s' in context '%s'\n", new_sw->name, new_sw->data, ast_get_context_name(con)); 
	ast_pthread_mutex_unlock(&con->lock);

	return 0;
}

/*
 * EBUSY  - can't lock
 * ENODATA - there is not context existence
 */
int ast_context_remove_ignorepat(char *context, char *ignorepat, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	c = ast_walk_contexts(NULL);
	while (c) {
		if (!strcmp(ast_get_context_name(c), context)) {
			int ret = ast_context_remove_ignorepat2(c, ignorepat, registrar);
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	ast_unlock_contexts();
	errno = ENODATA;
	return -1;
}

int ast_context_remove_ignorepat2(struct ast_context *con, char *ignorepat, char *registrar)
{
	struct ast_ignorepat *ip, *ipl = NULL;

	if (ast_pthread_mutex_lock(&con->lock)) {
		errno = EBUSY;
		return -1;
	}

	ip = con->ignorepats;
	while (ip) {
		if (!strcmp(ip->pattern, ignorepat) &&
			(registrar == ip->registrar || !registrar)) {
			if (ipl) {
				ipl->next = ip->next;
				free(ip);
			} else {
				con->ignorepats = ip->next;
				free(ip);
			}
			ast_pthread_mutex_unlock(&con->lock);
			return 0;
		}
		ipl = ip; ip = ip->next;
	}

	ast_pthread_mutex_unlock(&con->lock);
	errno = EINVAL;
	return -1;
}

/*
 * EBUSY - can't lock
 * ENODATA - there is no existence of context
 */
int ast_context_add_ignorepat(char *con, char *value, char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	c = ast_walk_contexts(NULL);
	while (c) {
		if (!strcmp(ast_get_context_name(c), con)) {
			int ret = ast_context_add_ignorepat2(c, value, registrar);
			ast_unlock_contexts();
			return ret;
		} 
		c = ast_walk_contexts(c);
	}

	ast_unlock_contexts();
	errno = ENODATA;
	return -1;
}

int ast_context_add_ignorepat2(struct ast_context *con, char *value, char *registrar)
{
	struct ast_ignorepat *ignorepat, *ignorepatc, *ignorepatl = NULL;
	ignorepat = malloc(sizeof(struct ast_ignorepat));
	if (!ignorepat) {
		ast_log(LOG_WARNING, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	strncpy(ignorepat->pattern, value, sizeof(ignorepat->pattern));
	ignorepat->next = NULL;
	ignorepat->registrar = registrar;
	pthread_mutex_lock(&con->lock);
	ignorepatc = con->ignorepats;
	while(ignorepatc) {
		ignorepatl = ignorepatc;
		if (!strcasecmp(ignorepatc->pattern, value)) {
			/* Already there */
			pthread_mutex_unlock(&con->lock);
			errno = EEXIST;
			return -1;
		}
		ignorepatc = ignorepatc->next;
	}
	if (ignorepatl) 
		ignorepatl->next = ignorepat;
	else
		con->ignorepats = ignorepat;
	pthread_mutex_unlock(&con->lock);
	return 0;
	
}

int ast_ignore_pattern(char *context, char *pattern)
{
	struct ast_context *con;
	struct ast_ignorepat *pat;
	con = ast_context_find(context);
	if (con) {
		pat = con->ignorepats;
		while (pat) {
			if (ast_extension_match(pat->pattern, pattern))
				return 1;
			pat = pat->next;
		}
	} 
	return 0;
}

/*
 * EBUSY   - can't lock
 * ENODATA  - no existence of context
 *
 */
int ast_add_extension(char *context, int replace, char *extension, int priority, char *callerid,
	char *application, void *data, void (*datad)(void *), char *registrar)
{
	struct ast_context *c;

	if (ast_lock_contexts()) {
		errno = EBUSY;
		return -1;
	}

	c = ast_walk_contexts(NULL);
	while (c) {
		if (!strcmp(context, ast_get_context_name(c))) {
			int ret = ast_add_extension2(c, replace, extension, priority, callerid,
				application, data, datad, registrar);
			ast_unlock_contexts();
			return ret;
		}
		c = ast_walk_contexts(c);
	}

	ast_unlock_contexts();
	errno = ENODATA;
	return -1;
}

static void ext_strncpy(char *dst, char *src, int len)
{
	int count=0;
	while(*src && (count < len - 1)) {
		switch(*src) {
		case ' ':
		case '-':
			/* Ignore */
			break;
		default:
			*dst = *src;
			dst++;
		}
		src++;
		count++;
	}
	*dst = '\0';
}

/*
 * EBUSY - can't lock
 * EEXIST - extension with the same priority exist and no replace is set
 *
 */
int ast_add_extension2(struct ast_context *con,
					  int replace, char *extension, int priority, char *callerid,
					  char *application, void *data, void (*datad)(void *),
					  char *registrar)
{

#define LOG do { 	if (option_debug) {\
		if (tmp->matchcid) { \
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d (CID match '%s') to %s\n", tmp->exten, tmp->priority, tmp->cidmatch, con->name); \
		} else { \
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
		} \
	} else if (option_verbose > 2) { \
		if (tmp->matchcid) { \
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d (CID match '%s')to %s\n", tmp->exten, tmp->priority, tmp->cidmatch, con->name); \
		} else {  \
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d to %s\n", tmp->exten, tmp->priority, con->name); \
		} \
	} } while(0)

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
		ext_strncpy(tmp->exten, extension, sizeof(tmp->exten));
		tmp->priority = priority;
		if (callerid) {
			ext_strncpy(tmp->cidmatch, callerid, sizeof(tmp->cidmatch));
			tmp->matchcid = 1;
		} else {
			strcpy(tmp->cidmatch, "");
			tmp->matchcid = 0;
		}
		strncpy(tmp->app, application, sizeof(tmp->app));
		tmp->data = data;
		tmp->datad = datad;
		tmp->registrar = registrar;
		tmp->peer = NULL;
		tmp->next =  NULL;
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	if (ast_pthread_mutex_lock(&con->lock)) {
		free(tmp);
		/* And properly destroy the data */
		datad(data);
		ast_log(LOG_WARNING, "Failed to lock context '%s'\n", con->name);
		errno = EBUSY;
		return -1;
	}
	e = con->root;
	while(e) {
		res= strcasecmp(e->exten, extension);
		if (!res) {
			if (!e->matchcid && !tmp->matchcid)
				res = 0;
			else if (tmp->matchcid && !e->matchcid)
				res = 1;
			else if (e->matchcid && !tmp->matchcid)
				res = -1;
			else
				res = strcasecmp(e->cidmatch, tmp->cidmatch);
		}
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
						errno = EEXIST;
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
	struct ast_sw *sw, *swl= NULL;
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
			for (sw = tmp->alts; sw; ) {
				swl = sw;
				sw = sw->next;
				free(swl);
				swl = sw;
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
	ast_cli_register(&show_applications_cli);
	ast_cli_register(&show_application_cli);
	ast_cli_register(&show_dialplan_cli);
	ast_cli_register(&show_switches_cli);
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

/*
 * Lock context list functions ...
 */
int ast_lock_contexts()
{
	return ast_pthread_mutex_lock(&conlock);
}

int ast_unlock_contexts()
{
	return ast_pthread_mutex_unlock(&conlock);
}

/*
 * Lock context ...
 */
int ast_lock_context(struct ast_context *con)
{
	return ast_pthread_mutex_lock(&con->lock);
}

int ast_unlock_context(struct ast_context *con)
{
	return ast_pthread_mutex_unlock(&con->lock);
}

/*
 * Name functions ...
 */
char *ast_get_context_name(struct ast_context *con)
{
	return con ? con->name : NULL;
}

char *ast_get_extension_name(struct ast_exten *exten)
{
	return exten ? exten->exten : NULL;
}

char *ast_get_include_name(struct ast_include *inc)
{
	return inc ? inc->name : NULL;
}

char *ast_get_ignorepat_name(struct ast_ignorepat *ip)
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
char *ast_get_context_registrar(struct ast_context *c)
{
	return c ? c->registrar : NULL;
}

char *ast_get_extension_registrar(struct ast_exten *e)
{
	return e ? e->registrar : NULL;
}

char *ast_get_include_registrar(struct ast_include *i)
{
	return i ? i->registrar : NULL;
}

char *ast_get_ignorepat_registrar(struct ast_ignorepat *ip)
{
	return ip ? ip->registrar : NULL;
}

char *ast_get_extension_app(struct ast_exten *e)
{
	return e ? e->app : NULL;
}

void *ast_get_extension_app_data(struct ast_exten *e)
{
	return e ? e->data : NULL;
}

char *ast_get_switch_name(struct ast_sw *sw)
{
	return sw ? sw->name : NULL;
}

char *ast_get_switch_data(struct ast_sw *sw)
{
	return sw ? sw->data : NULL;
}

char *ast_get_switch_registrar(struct ast_sw *sw)
{
	return sw ? sw->registrar : NULL;
}

/*
 * Walking functions ...
 */
struct ast_context *ast_walk_contexts(struct ast_context *con)
{
	if (!con)
		return contexts;
	else
		return con->next;
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
		return con ? con->alts : NULL;
	else
		return sw->next;
}

struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority)
{
	if (!priority)
		return exten;
	else
		return priority->peer;
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
