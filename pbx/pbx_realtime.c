/*
 * Realtime PBX Module
 *
 * Copyright (C) 2004, Digium Inc.
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * This program is Free Software distributed under the terms of
 * of the GNU General Public License.
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/frame.h>
#include <asterisk/file.h>
#include <asterisk/cli.h>
#include <asterisk/lock.h>
#include <asterisk/md5.h>
#include <asterisk/linkedlists.h>
#include <asterisk/chanvars.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/utils.h>
#include <asterisk/crypto.h>
#include <asterisk/astdb.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MODE_MATCH 0
#define MODE_MATCHMORE 1
#define MODE_CANMATCH 2

static char *tdesc = "Realtime Switch";

/* Realtime switch looks up extensions in the supplied realtime table.

	[context@][realtimetable][/options]

	If the realtimetable is omitted it is assumed to be "extensions".  If no context is 
	specified the context is assumed to be whatever is the container.

	The realtime table should have entries for context,exten,priority,app,args
	
	The realtime table currently does not support patterns or callerid fields.

*/


#define REALTIME_COMMON(mode) \
	char *buf; \
	char *opts; \
	const char *cxt; \
	char *table; \
	int res=-1; \
	struct ast_variable *var=NULL; \
	buf = ast_strdupa(data); \
	if (buf) { \
		opts = strchr(buf, '/'); \
		if (opts) { \
			*opts='\0'; \
			opts++; \
		} else \
			opts=""; \
		table = strchr(buf, '@'); \
		if (table) { \
			*table = '\0'; \
			table++;\
			cxt = buf; \
		} else cxt = NULL; \
		if (!cxt || ast_strlen_zero(cxt)) \
			cxt = context;\
		if (!table || ast_strlen_zero(table)) \
			table = "extensions"; \
		var = realtime_switch_common(table, cxt, exten, priority, mode); \
	} else \
		res = -1; 

static struct ast_variable *realtime_switch_common(const char *table, const char *context, const char *exten, int priority, int mode)
{
	struct ast_variable *var;
	char pri[20];
	char *ematch;
	char rexten[AST_MAX_EXTENSION + 20]="";
	snprintf(pri, sizeof(pri), "%d", priority);
	switch(mode) {
	case MODE_MATCHMORE:
		ematch = "exten LIKE";
		snprintf(rexten, sizeof(rexten), "%s_%%", exten);
		break;
	case MODE_CANMATCH:
		ematch = "exten LIKE";
		snprintf(rexten, sizeof(rexten), "%s%%", exten);
		break;
	case MODE_MATCH:
	default:
		ematch = "exten";
		strncpy(rexten, exten, sizeof(rexten) - 1);
	}
	var = ast_load_realtime(table, "context", context, ematch, rexten, "priority", pri, NULL);
	return var;
}

static int realtime_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	REALTIME_COMMON(MODE_MATCH);
	if (var) ast_destroy_realtime(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static int realtime_canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	REALTIME_COMMON(MODE_CANMATCH);
	if (var) ast_destroy_realtime(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static int realtime_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, int newstack, const char *data)
{
	char app[256];
	char *appdata="";
	struct ast_app *a;
	struct ast_variable *v;
	REALTIME_COMMON(MODE_MATCH);
	if (var) {
		v = var;
		while(v) {
			if (!strcasecmp(v->name, "app"))
				strncpy(app, v->value, sizeof(app) -1 );
			else if (!strcasecmp(v->name, "appdata"))
				appdata = ast_strdupa(v->value);
			v = v->next;
		}
		ast_destroy_realtime(var);
		if (!ast_strlen_zero(app)) {
			a = pbx_findapp(app);
			if (a) {
				res = pbx_exec(chan, a, appdata, newstack);
			} else
				ast_log(LOG_NOTICE, "No such application '%s' for extension '%s' in context '%s'\n", app, exten, context);
		}
	}
	return res;
}

static int realtime_matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	REALTIME_COMMON(MODE_MATCHMORE);
	if (var) ast_destroy_realtime(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static struct ast_switch realtime_switch =
{
        name:                   "Realtime",
        description:    		"Realtime Dialplan Switch",
        exists:                 realtime_exists,
        canmatch:               realtime_canmatch,
        exec:                   realtime_exec,
        matchmore:              realtime_matchmore,
};

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 1;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

int unload_module(void)
{
	ast_unregister_switch(&realtime_switch);
	return 0;
}

int load_module(void)
{
	ast_register_switch(&realtime_switch);
	return 0;
}

