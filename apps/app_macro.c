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

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"

/*** DOCUMENTATION
	<application name="Macro" language="en_US">
		<synopsis>
			Macro Implementation.
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The name of the macro</para>
			</parameter>
			<parameter name="args">
				<argument name="arg1" required="true" />
				<argument name="arg2" multiple="true" />
			</parameter>
		</syntax>
		<description>
			<para>Executes a macro using the context macro-<replaceable>name</replaceable>,
			jumping to the <literal>s</literal> extension of that context and executing each step,
			then returning when the steps end.</para>
			<para>The calling extension, context, and priority are stored in <variable>MACRO_EXTEN</variable>,
			<variable>MACRO_CONTEXT</variable> and <variable>MACRO_PRIORITY</variable> respectively. Arguments
			become <variable>ARG1</variable>, <variable>ARG2</variable>, etc in the macro context.</para>
			<para>If you Goto out of the Macro context, the Macro will terminate and control will be returned
			at the location of the Goto.</para>
			<para>If <variable>MACRO_OFFSET</variable> is set at termination, Macro will attempt to continue
			at priority MACRO_OFFSET + N + 1 if such a step exists, and N + 1 otherwise.</para>
			<warning><para>Because of the way Macro is implemented (it executes the priorities contained within
			it via sub-engine), and a fixed per-thread memory stack allowance, macros are limited to 7 levels
			of nesting (macro calling macro calling macro, etc.); It may be possible that stack-intensive
			applications in deeply nested macros could cause asterisk to crash earlier than this limit.
			It is advised that if you need to deeply nest macro calls, that you use the Gosub application
			(now allows arguments like a Macro) with explict Return() calls instead.</para></warning>
			<warning><para>Use of the application <literal>WaitExten</literal> within a macro will not function
			as expected. Please use the <literal>Read</literal> application in order to read DTMF from a channel
			currently executing a macro.</para></warning>
		</description>
		<see-also>
			<ref type="application">MacroExit</ref>
			<ref type="application">Goto</ref>
			<ref type="application">Gosub</ref>
		</see-also>
	</application>
	<application name="MacroIf" language="en_US">
		<synopsis>
			Conditional Macro implementation.
		</synopsis>
		<syntax argsep="?">
			<parameter name="expr" required="true" />
			<parameter name="destination" required="true" argsep=":">
				<argument name="macroiftrue" required="true">
					<argument name="macroiftrue" required="true" />
					<argument name="arg1" multiple="true" />
				</argument>
				<argument name="macroiffalse">
					<argument name="macroiffalse" required="true" />
					<argument name="arg1" multiple="true" />
				</argument>
			</parameter>
		</syntax>
		<description>
			<para>Executes macro defined in <replaceable>macroiftrue</replaceable> if
			<replaceable>expr</replaceable> is true (otherwise <replaceable>macroiffalse</replaceable>
			if provided)</para>
			<para>Arguments and return values as in application Macro()</para>
			<xi:include xpointer="xpointer(/docs/application[@name='Macro']/description/warning[2])" />
		</description>
		<see-also>
			<ref type="application">GotoIf</ref>
			<ref type="application">GosubIf</ref>
			<ref type="function">IF</ref>
		</see-also>
	</application>
	<application name="MacroExclusive" language="en_US">
		<synopsis>
			Exclusive Macro Implementation.
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The name of the macro</para>
			</parameter>
			<parameter name="arg1" />
			<parameter name="arg2" multiple="true" />
		</syntax>
		<description>
			<para>Executes macro defined in the context macro-<replaceable>name</replaceable>.
			Only one call at a time may run the macro. (we'll wait if another call is busy
			executing in the Macro)</para>
			<para>Arguments and return values as in application Macro()</para>
			<xi:include xpointer="xpointer(/docs/application[@name='Macro']/description/warning[2])" />
		</description>
		<see-also>
			<ref type="application">Macro</ref>
		</see-also>
	</application>
	<application name="MacroExit" language="en_US">
		<synopsis>
			Exit from Macro.
		</synopsis>
		<syntax />
		<description>
			<para>Causes the currently running macro to exit as if it had
			ended normally by running out of priorities to execute.
			If used outside a macro, will likely cause unexpected behavior.</para>
		</description>
		<see-also>
			<ref type="application">Macro</ref>
		</see-also>
	</application>
 ***/

#define MAX_ARGS 80

/* special result value used to force macro exit */
#define MACRO_EXIT_RESULT 1024

static char *app = "Macro";
static char *if_app = "MacroIf";
static char *exclusive_app = "MacroExclusive";
static char *exit_app = "MacroExit";

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

static int _macro_exec(struct ast_channel *chan, const char *data, int exclusive)
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
	struct ast_str *tmp_subst = NULL;
  
	char *save_macro_exten;
	char *save_macro_context;
	char *save_macro_priority;
	char *save_macro_offset;
	struct ast_datastore *macro_store = ast_channel_datastore_find(chan, &macro_ds_info, NULL);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Macro() requires arguments. See \"core show application macro\" for help.\n");
		return -1;
	}

	do {
		if (macro_store) {
			break;
		}
		if (!(macro_store = ast_datastore_alloc(&macro_ds_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to allocate new datastore.\n");
			break;
		}
		/* Just the existence of this datastore is enough. */
		macro_store->inheritance = DATASTORE_INHERIT_FOREVER;
		ast_channel_datastore_add(chan, macro_store);
	} while (0);

	/* does the user want a deeper rabbit hole? */
	ast_channel_lock(chan);
	if ((s = pbx_builtin_getvar_helper(chan, "MACRO_RECURSION"))) {
		sscanf(s, "%d", &maxdepth);
	}
	
	/* Count how many levels deep the rabbit hole goes */
	if ((s = pbx_builtin_getvar_helper(chan, "MACRO_DEPTH"))) {
		sscanf(s, "%d", &depth);
	}
	
	/* Used for detecting whether to return when a Macro is called from another Macro after hangup */
	if (strcmp(chan->exten, "h") == 0)
		pbx_builtin_setvar_helper(chan, "MACRO_IN_HANGUP", "1");
	
	if ((inhangupc = pbx_builtin_getvar_helper(chan, "MACRO_IN_HANGUP"))) {
		sscanf(inhangupc, "%d", &inhangup);
	}
	ast_channel_unlock(chan);

	if (depth >= maxdepth) {
		ast_log(LOG_ERROR, "Macro():  possible infinite loop detected.  Returning early.\n");
		return 0;
	}
	snprintf(depthc, sizeof(depthc), "%d", depth + 1);

	tmp = ast_strdupa(data);
	rest = tmp;
	macro = strsep(&rest, ",");
	if (ast_strlen_zero(macro)) {
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

	/* If we are to run the macro exclusively, take the mutex */
	if (exclusive) {
		ast_debug(1, "Locking macrolock for '%s'\n", fullmacro);
		ast_autoservice_start(chan);
		if (ast_context_lockmacro(fullmacro)) {
			ast_log(LOG_WARNING, "Failed to lock macro '%s' as in-use\n", fullmacro);
			ast_autoservice_stop(chan);
			return 0;
		}
		ast_autoservice_stop(chan);
	}

	if (!(tmp_subst = ast_str_create(16))) {
		return -1;
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

	pbx_builtin_setvar_helper(chan, "MACRO_DEPTH", depthc);

	/* Setup environment for new run */
	chan->exten[0] = 's';
	chan->exten[1] = '\0';
	ast_copy_string(chan->context, fullmacro, sizeof(chan->context));
	chan->priority = 1;

	ast_channel_lock(chan);
	while((cur = strsep(&rest, ",")) && (argc < MAX_ARGS)) {
		const char *argp;
  		/* Save copy of old arguments if we're overwriting some, otherwise
	   	let them pass through to the other macro */
  		snprintf(varname, sizeof(varname), "ARG%d", argc);
		if ((argp = pbx_builtin_getvar_helper(chan, varname))) {
			oldargs[argc] = ast_strdup(argp);
		}
		pbx_builtin_setvar_helper(chan, varname, cur);
		argc++;
	}
	ast_channel_unlock(chan);
	autoloopflag = ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(chan, AST_FLAG_IN_AUTOLOOP);
	while(ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
		struct ast_context *c;
		struct ast_exten *e;
		int foundx;
		runningapp[0] = '\0';
		runningdata[0] = '\0';

		/* What application will execute? */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
		} else {
			for (c = ast_walk_contexts(NULL), e = NULL; c; c = ast_walk_contexts(c)) {
				if (!strcmp(ast_get_context_name(c), chan->context)) {
					if (ast_rdlock_context(c)) {
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

		if ((res = ast_spawn_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num, &foundx,1))) {
			/* Something bad happened, or a hangup has been requested. */
			if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F')) ||
		    	(res == '*') || (res == '#')) {
				/* Just return result as to the previous application as if it had been dialed */
				ast_debug(1, "Oooh, got something to jump out with ('%c')!\n", res);
				break;
			}
			switch(res) {
			case MACRO_EXIT_RESULT:
				res = 0;
				goto out;
			default:
				ast_debug(2, "Spawn extension (%s,%s,%d) exited non-zero on '%s' in macro '%s'\n", chan->context, chan->exten, chan->priority, chan->name, macro);
				ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s' in macro '%s'\n", chan->context, chan->exten, chan->priority, chan->name, macro);
				goto out;
			}
		}

		ast_debug(1, "Executed application: %s\n", runningapp);

		if (!strcasecmp(runningapp, "GOSUB")) {
			gosub_level++;
			ast_debug(1, "Incrementing gosub_level\n");
		} else if (!strcasecmp(runningapp, "GOSUBIF")) {
			char *cond, *app_arg;
			char *app2;
			ast_str_substitute_variables(&tmp_subst, 0, chan, runningdata);
			app2 = ast_str_buffer(tmp_subst);
			cond = strsep(&app2, "?");
			app_arg = strsep(&app2, ":");
			if (pbx_checkcondition(cond)) {
				if (!ast_strlen_zero(app_arg)) {
					gosub_level++;
					ast_debug(1, "Incrementing gosub_level\n");
				}
			} else {
				if (!ast_strlen_zero(app2)) {
					gosub_level++;
					ast_debug(1, "Incrementing gosub_level\n");
				}
			}
		} else if (!strcasecmp(runningapp, "RETURN")) {
			gosub_level--;
			ast_debug(1, "Decrementing gosub_level\n");
		} else if (!strcasecmp(runningapp, "STACKPOP")) {
			gosub_level--;
			ast_debug(1, "Decrementing gosub_level\n");
		} else if (!strncasecmp(runningapp, "EXEC", 4)) {
			/* Must evaluate args to find actual app */
			char *tmp2, *tmp3 = NULL;
			ast_str_substitute_variables(&tmp_subst, 0, chan, runningdata);
			tmp2 = ast_str_buffer(tmp_subst);
			if (!strcasecmp(runningapp, "EXECIF")) {
				if ((tmp3 = strchr(tmp2, '|'))) {
					*tmp3++ = '\0';
				}
				if (!pbx_checkcondition(tmp2)) {
					tmp3 = NULL;
				}
			} else {
				tmp3 = tmp2;
			}

			if (tmp3) {
				ast_debug(1, "Last app: %s\n", tmp3);
			}

			if (tmp3 && !strncasecmp(tmp3, "GOSUB", 5)) {
				gosub_level++;
				ast_debug(1, "Incrementing gosub_level\n");
			} else if (tmp3 && !strncasecmp(tmp3, "RETURN", 6)) {
				gosub_level--;
				ast_debug(1, "Decrementing gosub_level\n");
			} else if (tmp3 && !strncasecmp(tmp3, "STACKPOP", 8)) {
				gosub_level--;
				ast_debug(1, "Decrementing gosub_level\n");
			}
		}

		if (gosub_level == 0 && strcasecmp(chan->context, fullmacro)) {
			ast_verb(2, "Channel '%s' jumping out of macro '%s'\n", chan->name, macro);
			break;
		}

		/* don't stop executing extensions when we're in "h" */
		if (ast_check_hangup(chan) && !inhangup) {
			ast_debug(1, "Extension %s, macroexten %s, priority %d returned normally even though call was hung up\n", chan->exten, chan->macroexten, chan->priority);
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
			ast_free(oldargs[x]);
		} else {
			pbx_builtin_setvar_helper(chan, varname, NULL);
		}
  	}

	/* Restore macro variables */
	pbx_builtin_setvar_helper(chan, "MACRO_EXTEN", save_macro_exten);
	pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", save_macro_context);
	pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", save_macro_priority);
	if (save_macro_exten)
		ast_free(save_macro_exten);
	if (save_macro_context)
		ast_free(save_macro_context);
	if (save_macro_priority)
		ast_free(save_macro_priority);

	if (setmacrocontext) {
		chan->macrocontext[0] = '\0';
		chan->macroexten[0] = '\0';
		chan->macropriority = 0;
	}

	if (!strcasecmp(chan->context, fullmacro)) {
  		/* If we're leaving the macro normally, restore original information */
		chan->priority = oldpriority;
		ast_copy_string(chan->context, oldcontext, sizeof(chan->context));
		if (!(ast_check_hangup(chan) & AST_SOFTHANGUP_ASYNCGOTO)) {
			/* Copy the extension, so long as we're not in softhangup, where we could be given an asyncgoto */
			const char *offsets;
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
		ast_free(save_macro_offset);

	/* Unlock the macro */
	if (exclusive) {
		ast_debug(1, "Unlocking macrolock for '%s'\n", fullmacro);
		if (ast_context_unlockmacro(fullmacro)) {
			ast_log(LOG_ERROR, "Failed to unlock macro '%s' - that isn't good\n", fullmacro);
			res = 0;
		}
	}
	ast_channel_unlock(chan);
	ast_free(tmp_subst);

	return res;
}

static int macro_exec(struct ast_channel *chan, const char *data)
{
	return _macro_exec(chan, data, 0);
}

static int macroexclusive_exec(struct ast_channel *chan, const char *data)
{
	return _macro_exec(chan, data, 1);
}

static int macroif_exec(struct ast_channel *chan, const char *data) 
{
	char *expr = NULL, *label_a = NULL, *label_b = NULL;
	int res = 0;

	if (!(expr = ast_strdupa(data)))
		return -1;

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

	return res;
}
			
static int macro_exit_exec(struct ast_channel *chan, const char *data)
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

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(exit_app, macro_exit_exec);
	res |= ast_register_application_xml(if_app, macroif_exec);
	res |= ast_register_application_xml(exclusive_app, macroexclusive_exec);
	res |= ast_register_application_xml(app, macro_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Extension Macros");
