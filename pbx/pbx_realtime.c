/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Realtime PBX Module
 *
 * \arg See also: \ref AstARA
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/md5.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/utils.h"
#include "asterisk/crypto.h"
#include "asterisk/astdb.h"

#define MODE_MATCH 		0
#define MODE_MATCHMORE 	1
#define MODE_CANMATCH 	2

#define EXT_DATA_SIZE 256

static char *tdesc = "Realtime Switch";

/* Realtime switch looks up extensions in the supplied realtime table.

	[context@][realtimetable][/options]

	If the realtimetable is omitted it is assumed to be "extensions".  If no context is 
	specified the context is assumed to be whatever is the container.

	The realtime table should have entries for context,exten,priority,app,args
	
	The realtime table currently does not support callerid fields.

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
		if (ast_strlen_zero(cxt)) \
			cxt = context;\
		if (ast_strlen_zero(table)) \
			table = "extensions"; \
		var = realtime_switch_common(table, cxt, exten, priority, mode); \
	} else \
		res = -1; 

static struct ast_variable *realtime_switch_common(const char *table, const char *context, const char *exten, int priority, int mode)
{
	struct ast_variable *var;
	struct ast_config *cfg;
	char pri[20];
	char *ematch;
	char rexten[AST_MAX_EXTENSION + 20]="";
	int match;
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
	var = ast_load_realtime(table, ematch, rexten, "context", context, "priority", pri, NULL);
	if (!var) {
		cfg = ast_load_realtime_multientry(table, "exten LIKE", "\\_%", "context", context, "priority", pri, NULL);	
		if (cfg) {
			char *cat = ast_category_browse(cfg, NULL);

			while(cat) {
				switch(mode) {
				case MODE_MATCHMORE:
					match = ast_extension_close(cat, exten, 1);
					break;
				case MODE_CANMATCH:
					match = ast_extension_close(cat, exten, 0);
					break;
				case MODE_MATCH:
				default:
					match = ast_extension_match(cat, exten);
				}
				if (match) {
					var = ast_category_detach_variables(ast_category_get(cfg, cat));
					break;
				}
				cat = ast_category_browse(cfg, cat);
			}
			ast_config_destroy(cfg);
		}
	}
	return var;
}

static int realtime_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	REALTIME_COMMON(MODE_MATCH);
	if (var) ast_variables_destroy(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static int realtime_canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	REALTIME_COMMON(MODE_CANMATCH);
	if (var) ast_variables_destroy(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static int realtime_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, int newstack, const char *data)
{
	char app[256];
	char appdata[512]="";
	char *tmp="";
    char tmp1[80];
    char tmp2[80];
    char tmp3[EXT_DATA_SIZE];
	struct ast_app *a;
	struct ast_variable *v;
	REALTIME_COMMON(MODE_MATCH);
	if (var) {
		v = var;
		while(v) {
			if (!strcasecmp(v->name, "app"))
				strncpy(app, v->value, sizeof(app) -1 );
			else if (!strcasecmp(v->name, "appdata"))
				tmp = ast_strdupa(v->value);
			v = v->next;
		}
		ast_variables_destroy(var);
		if (!ast_strlen_zero(app)) {
			a = pbx_findapp(app);
			if (a) {
				if(!ast_strlen_zero(tmp))
				   pbx_substitute_variables_helper(chan, tmp, appdata, sizeof(appdata) - 1);
                if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_3 "Executing %s(\"%s\", \"%s\")\n",
								 term_color(tmp1, app, COLOR_BRCYAN, 0, sizeof(tmp1)),
								 term_color(tmp2, chan->name, COLOR_BRMAGENTA, 0, sizeof(tmp2)),
								 term_color(tmp3, (!ast_strlen_zero(appdata) ? (char *)appdata : ""), COLOR_BRMAGENTA, 0, sizeof(tmp3)));
                manager_event(EVENT_FLAG_CALL, "Newexten",
							  "Channel: %s\r\n"
							  "Context: %s\r\n"
							  "Extension: %s\r\n"
							  "Priority: %d\r\n"
							  "Application: %s\r\n"
							  "AppData: %s\r\n"
							  "Uniqueid: %s\r\n",
							  chan->name, chan->context, chan->exten, chan->priority, app, appdata ? appdata : "(NULL)", chan->uniqueid);
				
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
	if (var) ast_variables_destroy(var);
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

