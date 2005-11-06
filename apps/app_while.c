/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright 2004 - 2005, Anthony Minessale <anthmct@yahoo.com>
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief While Loop and ExecIf Implementations
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"

#define ALL_DONE(u,ret) {LOCAL_USER_REMOVE(u); return ret;}


static char *exec_app = "ExecIf";
static char *exec_desc = 
"Usage:  ExecIF (<expr>|<app>|<data>)\n"
"If <expr> is true, execute and return the result of <app>(<data>).\n"
"If <expr> is true, but <app> is not found, then the application\n"
"will return a non-zero value.";
static char *exec_synopsis = "Conditional exec";

static char *start_app = "While";
static char *start_desc = 
"Usage:  While(<expr>)\n"
"Start a While Loop.  Execution will return to this point when\n"
"EndWhile is called until expr is no longer true.\n";

static char *start_synopsis = "Start A While Loop";


static char *stop_app = "EndWhile";
static char *stop_desc = 
"Usage:  EndWhile()\n"
"Return to the previous called While\n\n";

static char *stop_synopsis = "End A While Loop";

static char *tdesc = "While Loops and Conditional Execution";



STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int execif_exec(struct ast_channel *chan, void *data) {
	int res=0;
	struct localuser *u;
	char *myapp = NULL;
	char *mydata = NULL;
	char *expr = NULL;
	struct ast_app *app = NULL;

	LOCAL_USER_ADD(u);

	expr = ast_strdupa(data);
	if (!expr) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if ((myapp = strchr(expr,'|'))) {
		*myapp = '\0';
		myapp++;
		if ((mydata = strchr(myapp,'|'))) {
			*mydata = '\0';
			mydata++;
		} else
			mydata = "";

		if (ast_true(expr)) { 
			if ((app = pbx_findapp(myapp))) {
				res = pbx_exec(chan, app, mydata, 1);
			} else {
				ast_log(LOG_WARNING, "Count not find application! (%s)\n", myapp);
				res = -1;
			}
		}
	} else {
		ast_log(LOG_ERROR,"Invalid Syntax.\n");
		res = -1;
	}
		
	ALL_DONE(u,res);
}

#define VAR_SIZE 64


static char *get_index(struct ast_channel *chan, const char *prefix, int index) {
	char varname[VAR_SIZE];

	snprintf(varname, VAR_SIZE, "%s_%d", prefix, index);
	return pbx_builtin_getvar_helper(chan, varname);
}

static struct ast_exten *find_matching_priority(struct ast_context *c, const char *exten, int priority, const char *callerid)
{
	struct ast_exten *e;
	struct ast_include *i;
	struct ast_context *c2;

	for (e=ast_walk_context_extensions(c, NULL); e; e=ast_walk_context_extensions(c, e)) {
		if (ast_extension_match(ast_get_extension_name(e), exten)) {
			int needmatch = ast_get_extension_matchcid(e);
			if ((needmatch && ast_extension_match(ast_get_extension_cidmatch(e), callerid)) ||
				(!needmatch)) {
				/* This is the matching extension we want */
				struct ast_exten *p;
				for (p=ast_walk_extension_priorities(e, NULL); p; p=ast_walk_extension_priorities(e, p)) {
					if (priority != ast_get_extension_priority(p))
						continue;
					return p;
				}
			}
		}
	}

	/* No match; run through includes */
	for (i=ast_walk_context_includes(c, NULL); i; i=ast_walk_context_includes(c, i)) {
		for (c2=ast_walk_contexts(NULL); c2; c2=ast_walk_contexts(c2)) {
			if (!strcmp(ast_get_context_name(c2), ast_get_include_name(i))) {
				e = find_matching_priority(c2, exten, priority, callerid);
				if (e)
					return e;
			}
		}
	}
	return NULL;
}

static int find_matching_endwhile(struct ast_channel *chan)
{
	struct ast_context *c;
	int res=-1;

	if (ast_lock_contexts()) {
		ast_log(LOG_ERROR, "Failed to lock contexts list\n");
		return -1;
	}

	for (c=ast_walk_contexts(NULL); c; c=ast_walk_contexts(c)) {
		struct ast_exten *e;

		if (!ast_lock_context(c)) {
			if (!strcmp(ast_get_context_name(c), chan->context)) {
				/* This is the matching context we want */
				int cur_priority = chan->priority + 1, level=1;

				for (e = find_matching_priority(c, chan->exten, cur_priority, chan->cid.cid_num); e; e = find_matching_priority(c, chan->exten, ++cur_priority, chan->cid.cid_num)) {
					if (!strcasecmp(ast_get_extension_app(e), "WHILE")) {
						level++;
					} else if (!strcasecmp(ast_get_extension_app(e), "ENDWHILE")) {
						level--;
					}

					if (level == 0) {
						res = cur_priority;
						break;
					}
				}
			}
			ast_unlock_context(c);
			if (res > 0) {
				break;
			}
		}
	}
	ast_unlock_contexts();
	return res;
}

static int _while_exec(struct ast_channel *chan, void *data, int end)
{
	int res=0;
	struct localuser *u;
	char *while_pri = NULL;
	char *goto_str = NULL, *my_name = NULL;
	char *condition = NULL, *label = NULL;
	char varname[VAR_SIZE], end_varname[VAR_SIZE];
	const char *prefix = "WHILE";
	size_t size=0;
	int used_index_i = -1, x=0;
	char used_index[VAR_SIZE] = "0", new_index[VAR_SIZE] = "0";
	
	if (!chan) {
		/* huh ? */
		return -1;
	}

	LOCAL_USER_ADD(u);

	/* dont want run away loops if the chan isn't even up
	   this is up for debate since it slows things down a tad ......
	*/
	if (ast_waitfordigit(chan,1) < 0)
		ALL_DONE(u,-1);


	for (x=0;;x++) {
		if (get_index(chan, prefix, x)) {
			used_index_i = x;
		} else 
			break;
	}
	
	snprintf(used_index, VAR_SIZE, "%d", used_index_i);
	snprintf(new_index, VAR_SIZE, "%d", used_index_i + 1);
	
	if (!end) {
		condition = ast_strdupa((char *) data);
	}

	size = strlen(chan->context) + strlen(chan->exten) + 32;
	my_name = alloca(size);
	memset(my_name, 0, size);
	snprintf(my_name, size, "%s_%s_%d", chan->context, chan->exten, chan->priority);
	
	if (ast_strlen_zero(label)) {
		if (end) 
			label = used_index;
		else if (!(label = pbx_builtin_getvar_helper(chan, my_name))) {
			label = new_index;
			pbx_builtin_setvar_helper(chan, my_name, label);
		}
		
	}
	
	snprintf(varname, VAR_SIZE, "%s_%s", prefix, label);
	while_pri = pbx_builtin_getvar_helper(chan, varname);
	
	if ((while_pri = pbx_builtin_getvar_helper(chan, varname)) && !end) {
		snprintf(end_varname,VAR_SIZE,"END_%s",varname);
	}
	

	if (!end && !ast_true(condition)) {
		/* Condition Met (clean up helper vars) */
		pbx_builtin_setvar_helper(chan, varname, NULL);
		pbx_builtin_setvar_helper(chan, my_name, NULL);
        snprintf(end_varname,VAR_SIZE,"END_%s",varname);
		if ((goto_str=pbx_builtin_getvar_helper(chan, end_varname))) {
			pbx_builtin_setvar_helper(chan, end_varname, NULL);
			ast_parseable_goto(chan, goto_str);
		} else {
			int pri = find_matching_endwhile(chan);
			if (pri > 0) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Jumping to priority %d\n", pri);
				chan->priority = pri;
			} else {
				ast_log(LOG_WARNING, "Couldn't find matching EndWhile? (While at %s@%s priority %d)\n", chan->context, chan->exten, chan->priority);
			}
		}
		ALL_DONE(u,res);
	}

	if (!end && !while_pri) {
		size = strlen(chan->context) + strlen(chan->exten) + 32;
		goto_str = alloca(size);
		memset(goto_str, 0, size);
		snprintf(goto_str, size, "%s|%s|%d", chan->context, chan->exten, chan->priority);
		pbx_builtin_setvar_helper(chan, varname, goto_str);
	} 

	else if (end && while_pri) {
		/* END of loop */
		snprintf(end_varname, VAR_SIZE, "END_%s", varname);
		if (! pbx_builtin_getvar_helper(chan, end_varname)) {
			size = strlen(chan->context) + strlen(chan->exten) + 32;
			goto_str = alloca(size);
			memset(goto_str, 0, size);
			snprintf(goto_str, size, "%s|%s|%d", chan->context, chan->exten, chan->priority+1);
			pbx_builtin_setvar_helper(chan, end_varname, goto_str);
		}
		ast_parseable_goto(chan, while_pri);
	}
	



	ALL_DONE(u, res);
}

static int while_start_exec(struct ast_channel *chan, void *data) {
	return _while_exec(chan, data, 0);
}

static int while_end_exec(struct ast_channel *chan, void *data) {
	return _while_exec(chan, data, 1);
}


int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(start_app);
	res |= ast_unregister_application(exec_app);
	res |= ast_unregister_application(stop_app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_register_application(start_app, while_start_exec, start_synopsis, start_desc);
	res |= ast_register_application(exec_app, execif_exec, exec_synopsis, exec_desc);
	res |= ast_register_application(stop_app, while_end_exec, stop_synopsis, stop_desc);

	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

