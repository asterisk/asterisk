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

/*
 *
 * Macro Implementation
 * 
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"

#define MAX_ARGS 80

/* special result value used to force macro exit */
#define MACRO_EXIT_RESULT 1024

static char *tdesc = "Extension Macros";

static char *descrip =
"  Macro(macroname|arg1|arg2...): Executes a macro using the context\n"
"'macro-<macroname>', jumping to the 's' extension of that context and\n"
"executing each step, then returning when the steps end. \n"
"The calling extension, context, and priority are stored in ${MACRO_EXTEN}, \n"
"${MACRO_CONTEXT} and ${MACRO_PRIORITY} respectively.  Arguments become\n"
"${ARG1}, ${ARG2}, etc in the macro context.\n"
"If you Goto out of the Macro context, the Macro will terminate and control\n"
"will be returned at the location of the Goto.\n"
"Macro returns -1 if any step in the macro returns -1, and 0 otherwise.\n" 
"If ${MACRO_OFFSET} is set at termination, Macro will attempt to continue\n"
"at priority MACRO_OFFSET + N + 1 if such a step exists, and N + 1 otherwise.\n";

static char *if_descrip =
"  MacroIf(<expr>?macroname_a[|arg1][:macroname_b[|arg1]])\n"
"Executes macro defined in <macroname_a> if <expr> is true\n"
"(otherwise <macroname_b> if provided)\n"
"Arguments and return values as in application macro()\n";

static char *exit_descrip =
"  MacroExit():\n"
"Causes the currently running macro to exit as if it had\n"
"ended normally by running out of priorities to execute.\n"
"If used outside a macro, will likely cause unexpected\n"
"behavior.\n";

static char *app = "Macro";
static char *if_app = "MacroIf";
static char *exit_app = "MacroExit";

static char *synopsis = "Macro Implementation";
static char *if_synopsis = "Conditional Macro Implementation";
static char *exit_synopsis = "Exit From Macro";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int macro_exec(struct ast_channel *chan, void *data)
{
	char *tmp;
	char *cur, *rest;
	char *macro;
	char fullmacro[80];
	char varname[80];
	char *oldargs[MAX_ARGS + 1] = { NULL, };
	int argc, x;
	int res=0;
	char oldexten[256]="";
	int oldpriority;
	char pc[80], depthc[12];
	char oldcontext[AST_MAX_CONTEXT] = "";
	char *offsets;
	int offset, depth;
	int setmacrocontext=0;
	int autoloopflag;
  
	char *save_macro_exten;
	char *save_macro_context;
	char *save_macro_priority;
	char *save_macro_offset;
	struct localuser *u;
  
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Macro() requires arguments. See \"show application macro\" for help.\n");
		return 0;
	}

	/* Count how many levels deep the rabbit hole goes */
	tmp = pbx_builtin_getvar_helper(chan, "MACRO_DEPTH");
	if (tmp) {
		sscanf(tmp, "%d", &depth);
	} else {
		depth = 0;
	}

	if (depth >= 7) {
		ast_log(LOG_ERROR, "Macro():  possible infinite loop detected.  Returning early.\n");
		return 0;
	}
	snprintf(depthc, sizeof(depthc), "%d", depth + 1);
	pbx_builtin_setvar_helper(chan, "MACRO_DEPTH", depthc);

	tmp = ast_strdupa((char *) data);
	rest = tmp;
	macro = strsep(&rest, "|");
	if (!macro || ast_strlen_zero(macro)) {
		ast_log(LOG_WARNING, "Invalid macro name specified\n");
		return 0;
	}
	snprintf(fullmacro, sizeof(fullmacro), "macro-%s", macro);
	if (!ast_exists_extension(chan, fullmacro, "s", 1, chan->cid.cid_num)) {
  		if (!ast_context_find(fullmacro)) 
			ast_log(LOG_WARNING, "No such context '%s' for macro '%s'\n", fullmacro, macro);
		else
	  		ast_log(LOG_WARNING, "Context '%s' for macro '%s' lacks 's' extension, priority 1\n", fullmacro, macro);
		return 0;
	}

	LOCAL_USER_ADD(u);
	/* Save old info */
	oldpriority = chan->priority;
	ast_copy_string(oldexten, chan->exten, sizeof(oldexten));
	ast_copy_string(oldcontext, chan->context, sizeof(oldcontext));
	if (ast_strlen_zero(chan->macrocontext)) {
		ast_copy_string(chan->macrocontext, chan->context, sizeof(chan->macrocontext));
		ast_copy_string(chan->macroexten, chan->exten, sizeof(chan->macroexten));
		chan->macropriority = chan->priority;
		setmacrocontext=1;
	}
	argc = 1;
	/* Save old macro variables */
	save_macro_exten = pbx_builtin_getvar_helper(chan, "MACRO_EXTEN");
	if (save_macro_exten) 
		save_macro_exten = strdup(save_macro_exten);
	pbx_builtin_setvar_helper(chan, "MACRO_EXTEN", oldexten);

	save_macro_context = pbx_builtin_getvar_helper(chan, "MACRO_CONTEXT");
	if (save_macro_context)
		save_macro_context = strdup(save_macro_context);
	pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", oldcontext);

	save_macro_priority = pbx_builtin_getvar_helper(chan, "MACRO_PRIORITY");
	if (save_macro_priority) 
		save_macro_priority = strdup(save_macro_priority);
	snprintf(pc, sizeof(pc), "%d", oldpriority);
	pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", pc);
  
	save_macro_offset = pbx_builtin_getvar_helper(chan, "MACRO_OFFSET");
	if (save_macro_offset) 
		save_macro_offset = strdup(save_macro_offset);
	pbx_builtin_setvar_helper(chan, "MACRO_OFFSET", NULL);

	/* Setup environment for new run */
	chan->exten[0] = 's';
	chan->exten[1] = '\0';
	ast_copy_string(chan->context, fullmacro, sizeof(chan->context));
	chan->priority = 1;

	while((cur = strsep(&rest, "|")) && (argc < MAX_ARGS)) {
  		/* Save copy of old arguments if we're overwriting some, otherwise
	   	let them pass through to the other macro */
  		snprintf(varname, sizeof(varname), "ARG%d", argc);
		oldargs[argc] = pbx_builtin_getvar_helper(chan, varname);
		if (oldargs[argc])
			oldargs[argc] = strdup(oldargs[argc]);
		pbx_builtin_setvar_helper(chan, varname, cur);
		argc++;
	}
	autoloopflag = ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(chan, AST_FLAG_IN_AUTOLOOP);
	while(ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
		/* Reset the macro depth, if it was changed in the last iteration */
		pbx_builtin_setvar_helper(chan, "MACRO_DEPTH", depthc);
		if ((res = ast_spawn_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num))) {
			/* Something bad happened, or a hangup has been requested. */
			if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F')) ||
		    	(res == '*') || (res == '#')) {
				/* Just return result as to the previous application as if it had been dialed */
				ast_log(LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
				break;
			}
			switch(res) {
	        	case MACRO_EXIT_RESULT:
                        	res = 0;
				goto out;
			case AST_PBX_KEEPALIVE:
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE in macro %s on '%s'\n", chan->context, chan->exten, chan->priority, macro, chan->name);
				else if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE in macro '%s' on '%s'\n", chan->context, chan->exten, chan->priority, macro, chan->name);
				goto out;
				break;
			default:
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s' in macro '%s'\n", chan->context, chan->exten, chan->priority, chan->name, macro);
				else if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s' in macro '%s'\n", chan->context, chan->exten, chan->priority, chan->name, macro);
				goto out;
			}
		}
		if (strcasecmp(chan->context, fullmacro)) {
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Channel '%s' jumping out of macro '%s'\n", chan->name, macro);
			break;
		}
		/* don't stop executing extensions when we're in "h" */
		if (chan->_softhangup && strcasecmp(oldexten,"h")) {
			ast_log(LOG_DEBUG, "Extension %s, priority %d returned normally even though call was hung up\n",
				chan->exten, chan->priority);
			goto out;
		}
		chan->priority++;
  	}
	out:
	/* Reset the depth back to what it was when the routine was entered (like if we called Macro recursively) */
	snprintf(depthc, sizeof(depthc), "%d", depth);
	pbx_builtin_setvar_helper(chan, "MACRO_DEPTH", depthc);

	ast_set2_flag(chan, autoloopflag, AST_FLAG_IN_AUTOLOOP);
  	for (x=1; x<argc; x++) {
  		/* Restore old arguments and delete ours */
		snprintf(varname, sizeof(varname), "ARG%d", x);
  		if (oldargs[x]) {
			pbx_builtin_setvar_helper(chan, varname, oldargs[x]);
			free(oldargs[x]);
		} else {
			pbx_builtin_setvar_helper(chan, varname, NULL);
		}
  	}

	/* Restore macro variables */
	pbx_builtin_setvar_helper(chan, "MACRO_EXTEN", save_macro_exten);
	if (save_macro_exten)
		free(save_macro_exten);
	pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", save_macro_context);
	if (save_macro_context)
		free(save_macro_context);
	pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", save_macro_priority);
	if (save_macro_priority)
		free(save_macro_priority);
	if (setmacrocontext) {
		chan->macrocontext[0] = '\0';
		chan->macroexten[0] = '\0';
		chan->macropriority = 0;
	}

	if (!strcasecmp(chan->context, fullmacro)) {
  		/* If we're leaving the macro normally, restore original information */
		chan->priority = oldpriority;
		ast_copy_string(chan->context, oldcontext, sizeof(chan->context));
		if (!(chan->_softhangup & AST_SOFTHANGUP_ASYNCGOTO)) {
			/* Copy the extension, so long as we're not in softhangup, where we could be given an asyncgoto */
			ast_copy_string(chan->exten, oldexten, sizeof(chan->exten));
			if ((offsets = pbx_builtin_getvar_helper(chan, "MACRO_OFFSET"))) {
				/* Handle macro offset if it's set by checking the availability of step n + offset + 1, otherwise continue
			   	normally if there is any problem */
				if (sscanf(offsets, "%d", &offset) == 1) {
					if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + offset + 1, chan->cid.cid_num)) {
						chan->priority += offset;
					}
				}
			}
		}
	}

	pbx_builtin_setvar_helper(chan, "MACRO_OFFSET", save_macro_offset);
	if (save_macro_offset)
		free(save_macro_offset);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int macroif_exec(struct ast_channel *chan, void *data) 
{
	char *expr = NULL, *label_a = NULL, *label_b = NULL;
	int res = 0;

	if((expr = ast_strdupa((char *) data))) {
		if ((label_a = strchr(expr, '?'))) {
			*label_a = '\0';
			label_a++;
			if ((label_b = strchr(label_a, ':'))) {
				*label_b = '\0';
				label_b++;
			}
			if (ast_true(expr))
				macro_exec(chan, label_a);
			else if (label_b) 
				macro_exec(chan, label_b);
			
		} else
			ast_log(LOG_WARNING, "Invalid Syntax.\n");
	} else 
		ast_log(LOG_ERROR, "Out of Memory!\n");
	return res;
}
			
static int macro_exit_exec(struct ast_channel *chan, void *data)
{
	return MACRO_EXIT_RESULT;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(if_app);
	res |= ast_unregister_application(exit_app);
	res |= ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_register_application(exit_app, macro_exit_exec, exit_synopsis, exit_descrip);
	res |= ast_register_application(if_app, macroif_exec, if_synopsis, if_descrip);
	res |= ast_register_application(app, macro_exec, synopsis, descrip);

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
