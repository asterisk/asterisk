/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * While Loop and ExecIf Implementations
 * 
 * Copyright 2004 - 2005, Anthony Minessale <anthmct@yahoo.com>
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */


#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/utils.h>
#include <asterisk/config.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define ALL_DONE(u,ret) {LOCAL_USER_REMOVE(u); return ret;}


static char *exec_app = "ExecIf";
static char *exec_desc = "  ExecIF (<expr>|<app>|<data>)\n"
"If <expr> is true, execute and return the result of <app>(<data>)\n\n";
static char *exec_synopsis = "ExecIF (<expr>|<app>|<data>)";

static char *start_app = "While";
static char *start_desc = "  While(<expr>)\n"
"Start a While Loop.  Execution will return to this point when\n"
"EndWhile is called until expr is no longer true.\n";

static char *start_synopsis = "Start A While Loop";


static char *stop_app = "EndWhile";
static char *stop_desc = "  EndWhile()\n"
"Return to the previous called While\n\n";

static char *stop_synopsis = "End A While Loop";

static char *tdesc = "";



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
	expr = ast_strdupa((char *) data);
	if ((myapp = strchr(expr,'|'))) {
		*myapp = '\0';
		myapp++;
		if ((mydata = strchr(myapp,'|'))) {
			*mydata = '\0';
			mydata++;
		} else
			mydata = "";

		if(ast_true(expr) && (app = pbx_findapp(myapp))) {
			res = pbx_exec(chan, app, mydata, 1);
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
	
	if (!label || ast_strlen_zero(label)) {
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
	STANDARD_HANGUP_LOCALUSERS;
	ast_unregister_application(start_app);
	ast_unregister_application(exec_app);
	return ast_unregister_application(stop_app);
}

int load_module(void)
{
	ast_register_application(start_app, while_start_exec, start_synopsis, start_desc);
	ast_register_application(exec_app, execif_exec, exec_synopsis, exec_desc);
	return ast_register_application(stop_app, while_end_exec, stop_synopsis, stop_desc);
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

