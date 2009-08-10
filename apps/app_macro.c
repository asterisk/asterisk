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
 * \brief Dial plan macro Implementation
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

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

static char *descrip =
"  Macro(macroname|arg1|arg2...): Executes a macro using the context\n"
"'macro-<macroname>', jumping to the 's' extension of that context and\n"
"executing each step, then returning when the steps end. \n"
"The calling extension, context, and priority are stored in ${MACRO_EXTEN}, \n"
"${MACRO_CONTEXT} and ${MACRO_PRIORITY} respectively.  Arguments become\n"
"${ARG1}, ${ARG2}, etc in the macro context.\n"
"If you Goto out of the Macro context, the Macro will terminate and control\n"
"will be returned at the location of the Goto.\n"
"If ${MACRO_OFFSET} is set at termination, Macro will attempt to continue\n"
"at priority MACRO_OFFSET + N + 1 if such a step exists, and N + 1 otherwise.\n"
"WARNING: Because of the way Macro is implemented (it executes the priorities\n"
"         contained within it via sub-engine), and a fixed per-thread\n"
"         memory stack allowance, macros are limited to 7 levels\n"
"         of nesting (macro calling macro calling macro, etc.); It\n"
"         may be possible that stack-intensive applications in deeply nested\n"
"         macros could cause asterisk to crash earlier than this limit.\n"
"NOTE: a bug existed in earlier versions of Asterisk that caused Macro not\n"
"to reset its context and extension correctly upon exit.  This meant that\n"
"the 'h' extension within a Macro sometimes would execute, when the dialplan\n"
"exited while that Macro was running.  However, since this bug has been in\n"
"Asterisk for so long, users started to depend upon this behavior.  Therefore,\n"
"when a channel hangs up when in the midst of executing a Macro, the macro\n"
"context will first be checked for an 'h' extension, followed by the main\n"
"context from which the Macro was originally called.  This behavior in 1.4\n"
"exists only for compatibility with earlier versions.  You are strongly\n"
"encouraged to make use of the 'h' extension only in the context from which\n"
"Macro was originally called.\n";

static char *if_descrip =
"  MacroIf(<expr>?macroname_a[|arg1][:macroname_b[|arg1]])\n"
"Executes macro defined in <macroname_a> if <expr> is true\n"
"(otherwise <macroname_b> if provided)\n"
"Arguments and return values as in application macro()\n";

static char *exclusive_descrip =
"  MacroExclusive(macroname|arg1|arg2...):\n"
"Executes macro defined in the context 'macro-macroname'\n"
"Only one call at a time may run the macro.\n"
"(we'll wait if another call is busy executing in the Macro)\n"
"Arguments and return values as in application Macro()\n";

static char *exit_descrip =
"  MacroExit():\n"
"Causes the currently running macro to exit as if it had\n"
"ended normally by running out of priorities to execute.\n"
"If used outside a macro, will likely cause unexpected\n"
"behavior.\n";

static char *app = "Macro";
static char *if_app = "MacroIf";
static char *exclusive_app = "MacroExclusive";
static char *exit_app = "MacroExit";

static char *synopsis = "Macro Implementation";
static char *if_synopsis = "Conditional Macro Implementation";
static char *exclusive_synopsis = "Exclusive Macro Implementation";
static char *exit_synopsis = "Exit From Macro";

static void macro_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan);

struct ast_datastore_info macro_ds_info = {
	.type = "MACRO",
	.chan_fixup = macro_fixup,
};

static void macro_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	int i;
	char varname[10];
	pbx_builtin_setvar_helper(new_chan, "MACRO_DEPTH", "0");
	pbx_builtin_setvar_helper(new_chan, "MACRO_CONTEXT", NULL);
	pbx_builtin_setvar_helper(new_chan, "MACRO_EXTEN", NULL);
	pbx_builtin_setvar_helper(new_chan, "MACRO_PRIORITY", NULL);
	pbx_builtin_setvar_helper(new_chan, "MACRO_OFFSET", NULL);
	for (i = 1; i < 100; i++) {
		snprintf(varname, sizeof(varname), "ARG%d", i);
		while (pbx_builtin_getvar_helper(new_chan, varname)) {
			/* Kill all levels of arguments */
			pbx_builtin_setvar_helper(new_chan, varname, NULL);
		}
	}
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

static int _macro_exec(struct ast_channel *chan, void *data, int exclusive)
{
	const char *s;
	char *tmp;
	char *cur, *rest;
	char *macro;
	char fullmacro[80];
	char varname[80];
	char runningapp[80], runningdata[1024];
	char *oldargs[MAX_ARGS + 1] = { NULL, };
	int argc, x;
	int res=0;
	char oldexten[256]="";
	int oldpriority, gosub_level = 0;
	char pc[80], depthc[12];
	char oldcontext[AST_MAX_CONTEXT] = "";
	const char *inhangupc;
	int offset, depth = 0, maxdepth = 7;
	int setmacrocontext=0;
	int autoloopflag, inhangup = 0;
  
	char *save_macro_exten;
	char *save_macro_context;
	char *save_macro_priority;
	char *save_macro_offset;
	struct ast_module_user *u;
	struct ast_datastore *macro_store = ast_channel_datastore_find(chan, &macro_ds_info, NULL);
 
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Macro() requires arguments. See \"show application macro\" for help.\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	do {
		if (macro_store) {
			break;
		}
		if (!(macro_store = ast_channel_datastore_alloc(&macro_ds_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to allocate new datastore.\n");
			break;
		}
		/* Just the existence of this datastore is enough. */
		macro_store->inheritance = DATASTORE_INHERIT_FOREVER;
		ast_channel_datastore_add(chan, macro_store);
	} while (0);

	/* does the user want a deeper rabbit hole? */
	s = pbx_builtin_getvar_helper(chan, "MACRO_RECURSION");
	if (s)
		sscanf(s, "%30d", &maxdepth);

	/* Count how many levels deep the rabbit hole goes */
	s = pbx_builtin_getvar_helper(chan, "MACRO_DEPTH");
	if (s)
		sscanf(s, "%30d", &depth);
	/* Used for detecting whether to return when a Macro is called from another Macro after hangup */
	if (strcmp(chan->exten, "h") == 0)
		pbx_builtin_setvar_helper(chan, "MACRO_IN_HANGUP", "1");
	inhangupc = pbx_builtin_getvar_helper(chan, "MACRO_IN_HANGUP");
	if (!ast_strlen_zero(inhangupc))
		sscanf(inhangupc, "%30d", &inhangup);

	if (depth >= maxdepth) {
		ast_log(LOG_ERROR, "Macro():  possible infinite loop detected.  Returning early.\n");
		ast_module_user_remove(u);
		return 0;
	}
	snprintf(depthc, sizeof(depthc), "%d", depth + 1);
	pbx_builtin_setvar_helper(chan, "MACRO_DEPTH", depthc);

	tmp = ast_strdupa(data);
	rest = tmp;
	macro = strsep(&rest, "|");
	if (ast_strlen_zero(macro)) {
		ast_log(LOG_WARNING, "Invalid macro name specified\n");
		ast_module_user_remove(u);
		return 0;
	}

	snprintf(fullmacro, sizeof(fullmacro), "macro-%s", macro);
	if (!ast_exists_extension(chan, fullmacro, "s", 1, chan->cid.cid_num)) {
		if (!ast_context_find(fullmacro)) 
			ast_log(LOG_WARNING, "No such context '%s' for macro '%s'\n", fullmacro, macro);
		else
			ast_log(LOG_WARNING, "Context '%s' for macro '%s' lacks 's' extension, priority 1\n", fullmacro, macro);
		ast_module_user_remove(u);
		return 0;
	}

	/* If we are to run the macro exclusively, take the mutex */
	if (exclusive) {
		ast_log(LOG_DEBUG, "Locking macrolock for '%s'\n", fullmacro);
		ast_autoservice_start(chan);
		if (ast_context_lockmacro(fullmacro)) {
			ast_log(LOG_WARNING, "Failed to lock macro '%s' as in-use\n", fullmacro);
			ast_autoservice_stop(chan);
			ast_module_user_remove(u);

			return 0;
		}
		ast_autoservice_stop(chan);
	}
	
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
	save_macro_exten = ast_strdup(pbx_builtin_getvar_helper(chan, "MACRO_EXTEN"));
	pbx_builtin_setvar_helper(chan, "MACRO_EXTEN", oldexten);

	save_macro_context = ast_strdup(pbx_builtin_getvar_helper(chan, "MACRO_CONTEXT"));
	pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", oldcontext);

	save_macro_priority = ast_strdup(pbx_builtin_getvar_helper(chan, "MACRO_PRIORITY"));
	snprintf(pc, sizeof(pc), "%d", oldpriority);
	pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", pc);
  
	save_macro_offset = ast_strdup(pbx_builtin_getvar_helper(chan, "MACRO_OFFSET"));
	pbx_builtin_setvar_helper(chan, "MACRO_OFFSET", NULL);

	/* Setup environment for new run */
	chan->exten[0] = 's';
	chan->exten[1] = '\0';
	ast_copy_string(chan->context, fullmacro, sizeof(chan->context));
	chan->priority = 1;

	while((cur = strsep(&rest, "|")) && (argc < MAX_ARGS)) {
		const char *s;
  		/* Save copy of old arguments if we're overwriting some, otherwise
	   	let them pass through to the other macro */
  		snprintf(varname, sizeof(varname), "ARG%d", argc);
		s = pbx_builtin_getvar_helper(chan, varname);
		if (s)
			oldargs[argc] = ast_strdup(s);
		pbx_builtin_setvar_helper(chan, varname, cur);
		argc++;
	}
	autoloopflag = ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(chan, AST_FLAG_IN_AUTOLOOP);
	while(ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
		struct ast_context *c;
		struct ast_exten *e;
		runningapp[0] = '\0';
		runningdata[0] = '\0';

		/* What application will execute? */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
		} else {
			for (c = ast_walk_contexts(NULL), e = NULL; c; c = ast_walk_contexts(c)) {
				if (!strcmp(ast_get_context_name(c), chan->context)) {
					if (ast_lock_context(c)) {
						ast_log(LOG_WARNING, "Unable to lock context?\n");
					} else {
						e = find_matching_priority(c, chan->exten, chan->priority, chan->cid.cid_num);
						if (e) { /* This will only be undefined for pbx_realtime, which is majorly broken. */
							ast_copy_string(runningapp, ast_get_extension_app(e), sizeof(runningapp));
							ast_copy_string(runningdata, ast_get_extension_app_data(e), sizeof(runningdata));
						}
						ast_unlock_context(c);
					}
					break;
				}
			}
		}
		ast_unlock_contexts();

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
			default:
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s' in macro '%s'\n", chan->context, chan->exten, chan->priority, chan->name, macro);
				else if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s' in macro '%s'\n", chan->context, chan->exten, chan->priority, chan->name, macro);
				goto out;
			}
		}

		ast_log(LOG_DEBUG, "Executed application: %s\n", runningapp);

		if (!strcasecmp(runningapp, "GOSUB")) {
			gosub_level++;
			ast_log(LOG_DEBUG, "Incrementing gosub_level\n");
		} else if (!strcasecmp(runningapp, "GOSUBIF")) {
			char tmp2[1024] = "", *cond, *app, *app2 = tmp2;
			pbx_substitute_variables_helper(chan, runningdata, tmp2, sizeof(tmp2) - 1);
			cond = strsep(&app2, "?");
			app = strsep(&app2, ":");
			if (pbx_checkcondition(cond)) {
				if (!ast_strlen_zero(app)) {
					gosub_level++;
					ast_log(LOG_DEBUG, "Incrementing gosub_level\n");
				}
			} else {
				if (!ast_strlen_zero(app2)) {
					gosub_level++;
					ast_log(LOG_DEBUG, "Incrementing gosub_level\n");
				}
			}
		} else if (!strcasecmp(runningapp, "RETURN")) {
			gosub_level--;
			ast_log(LOG_DEBUG, "Decrementing gosub_level\n");
		} else if (!strcasecmp(runningapp, "STACKPOP")) {
			gosub_level--;
			ast_log(LOG_DEBUG, "Decrementing gosub_level\n");
		} else if (!strncasecmp(runningapp, "EXEC", 4)) {
			/* Must evaluate args to find actual app */
			char tmp2[1024] = "", *tmp3 = NULL;
			pbx_substitute_variables_helper(chan, runningdata, tmp2, sizeof(tmp2) - 1);
			if (!strcasecmp(runningapp, "EXECIF")) {
				tmp3 = strchr(tmp2, '|');
				if (tmp3)
					*tmp3++ = '\0';
				if (!pbx_checkcondition(tmp2))
					tmp3 = NULL;
			} else
				tmp3 = tmp2;

			if (tmp3)
				ast_log(LOG_DEBUG, "Last app: %s\n", tmp3);

			if (tmp3 && !strncasecmp(tmp3, "GOSUB", 5)) {
				gosub_level++;
				ast_log(LOG_DEBUG, "Incrementing gosub_level\n");
			} else if (tmp3 && !strncasecmp(tmp3, "RETURN", 6)) {
				gosub_level--;
				ast_log(LOG_DEBUG, "Decrementing gosub_level\n");
			} else if (tmp3 && !strncasecmp(tmp3, "STACKPOP", 8)) {
				gosub_level--;
				ast_log(LOG_DEBUG, "Decrementing gosub_level\n");
			}
		}

		if (gosub_level == 0 && strcasecmp(chan->context, fullmacro)) {
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Channel '%s' jumping out of macro '%s'\n", chan->name, macro);
			break;
		}

		/* don't stop executing extensions when we're in "h" */
		if (chan->_softhangup && !inhangup) {
			ast_log(LOG_DEBUG, "Extension %s, macroexten %s, priority %d returned normally even though call was hung up\n",
				chan->exten, chan->macroexten, chan->priority);
			goto out;
		}
		chan->priority++;
  	}
	out:

	/* Don't let the channel change now. */
	ast_channel_lock(chan);

	/* Reset the depth back to what it was when the routine was entered (like if we called Macro recursively) */
	snprintf(depthc, sizeof(depthc), "%d", depth);
	pbx_builtin_setvar_helper(chan, "MACRO_DEPTH", depthc);
	ast_set2_flag(chan, autoloopflag, AST_FLAG_IN_AUTOLOOP);

  	for (x = 1; x < argc; x++) {
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
	pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", save_macro_context);
	pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", save_macro_priority);
	if (save_macro_exten)
		free(save_macro_exten);
	if (save_macro_context)
		free(save_macro_context);
	if (save_macro_priority)
		free(save_macro_priority);

	if (setmacrocontext) {
		chan->macrocontext[0] = '\0';
		chan->macroexten[0] = '\0';
		chan->macropriority = 0;
	}

	/*!\note
	 * This section is used to restore a behavior that we mistakenly
	 * changed in issue #6176, then mistakenly reverted in #13962 and
	 * #13363.  A corresponding change is made in main/pbx.c, where we
	 * check this variable for existence, then look for the "h" extension
	 * in that context.
	 */
	if (ast_check_hangup(chan) || res < 0) {
		/* Don't need to lock the channel, as we aren't dereferencing emc.
		 * The intent here is to grab the deepest context, without overwriting
		 * in any above context. */
		const char *emc = pbx_builtin_getvar_helper(chan, "EXIT_MACRO_CONTEXT");
		if (!emc) {
			pbx_builtin_setvar_helper(chan, "EXIT_MACRO_CONTEXT", fullmacro);
		}
	}

	if (!strcasecmp(chan->context, fullmacro)) {
  		/* If we're leaving the macro normally, restore original information */
		chan->priority = oldpriority;
		ast_copy_string(chan->context, oldcontext, sizeof(chan->context));
		if (!(chan->_softhangup & AST_SOFTHANGUP_ASYNCGOTO)) {
			/* Copy the extension, so long as we're not in softhangup, where we could be given an asyncgoto */
			const char *offsets;
			ast_copy_string(chan->exten, oldexten, sizeof(chan->exten));
			if ((offsets = pbx_builtin_getvar_helper(chan, "MACRO_OFFSET"))) {
				/* Handle macro offset if it's set by checking the availability of step n + offset + 1, otherwise continue
			   	normally if there is any problem */
				if (sscanf(offsets, "%30d", &offset) == 1) {
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

	/* Unlock the macro */
	if (exclusive) {
		ast_log(LOG_DEBUG, "Unlocking macrolock for '%s'\n", fullmacro);
		if (ast_context_unlockmacro(fullmacro)) {
			ast_log(LOG_ERROR, "Failed to unlock macro '%s' - that isn't good\n", fullmacro);
			res = 0;
		}
	}
	ast_channel_unlock(chan);
	
	ast_module_user_remove(u);

	return res;
}

static int macro_exec(struct ast_channel *chan, void *data)
{
	return _macro_exec(chan, data, 0);
}

static int macroexclusive_exec(struct ast_channel *chan, void *data)
{
	return _macro_exec(chan, data, 1);
}

static int macroif_exec(struct ast_channel *chan, void *data) 
{
	char *expr = NULL, *label_a = NULL, *label_b = NULL;
	int res = 0;
	struct ast_module_user *u;

	u = ast_module_user_add(chan);

	if (!(expr = ast_strdupa(data))) {
		ast_module_user_remove(u);
		return -1;
	}

	if ((label_a = strchr(expr, '?'))) {
		*label_a = '\0';
		label_a++;
		if ((label_b = strchr(label_a, ':'))) {
			*label_b = '\0';
			label_b++;
		}
		if (pbx_checkcondition(expr))
			res = macro_exec(chan, label_a);
		else if (label_b) 
			res = macro_exec(chan, label_b);
	} else
		ast_log(LOG_WARNING, "Invalid Syntax.\n");

	ast_module_user_remove(u);

	return res;
}
			
static int macro_exit_exec(struct ast_channel *chan, void *data)
{
	return MACRO_EXIT_RESULT;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(if_app);
	res |= ast_unregister_application(exit_app);
	res |= ast_unregister_application(app);
	res |= ast_unregister_application(exclusive_app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application(exit_app, macro_exit_exec, exit_synopsis, exit_descrip);
	res |= ast_register_application(if_app, macroif_exec, if_synopsis, if_descrip);
	res |= ast_register_application(exclusive_app, macroexclusive_exec, exclusive_synopsis, exclusive_descrip);
	res |= ast_register_application(app, macro_exec, synopsis, descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Extension Macros");
