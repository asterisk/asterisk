/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Macro Implementation
 * 
 * Copyright (C) 2003-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define MAX_ARGS 80

static char *tdesc = "Extension Macros";

static char *descrip =
"  Macro(macroname|arg1|arg2...): Executes a macro using the context\n"
"'macro-<macroname>', jumping to the 's' extension of that context and\n"
"executing each step, then returning when the steps end.  The calling\n"
"extension, context, and priority are stored in ${MACRO_EXTEN}, \n"
"${MACRO_CONTEXT} and ${MACRO_PRIORITY} respectively.  Arguments become\n"
"${ARG1}, ${ARG2}, etc in the macro context.  Macro returns -1 if\n"
"any step in the macro returns -1, and 0 otherwise.  If you Goto out\n"
"of the Macro context, the Macro will terminate and control will be return\n"
"at the location of the Goto.  Otherwise if ${MACRO_OFFSET} is set at\n"
"termination, Macro will attempt to continue at priority\n"
"MACRO_OFFSET + N + 1 if such a step exists, and N + 1 otherwise.\n";

static char *app = "Macro";

static char *synopsis = "Macro Implementation";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int macro_exec(struct ast_channel *chan, void *data)
{
  char tmp[256] = "";
  char *cur, *rest;
  char *macro;
  char fullmacro[80];
  char varname[80];
  char *oldargs[MAX_ARGS + 1] = { NULL, };
  int argc, x;
  int res=0;
  char oldexten[256]="";
  int oldpriority;
  char pc[80];
  char oldcontext[256] = "";
  char *offsets;
  int offset;
  int setmacrocontext=0;
  
  char *save_macro_exten;
  char *save_macro_context;
  char *save_macro_priority;
  char *save_macro_offset;
  
  if (!data || ast_strlen_zero(data)) {
    ast_log(LOG_WARNING, "Invalid Macro incantation\n");
    return 0;
  }
  strncpy(tmp, data, sizeof(tmp) - 1);
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
  /* Save old info */
  oldpriority = chan->priority;
  strncpy(oldexten, chan->exten, sizeof(oldexten) - 1);
  strncpy(oldcontext, chan->context, sizeof(oldcontext) - 1);
  if (ast_strlen_zero(chan->macrocontext)) {
	strncpy(chan->macrocontext, chan->context, sizeof(chan->macrocontext) - 1);
	strncpy(chan->macroexten, chan->exten, sizeof(chan->macroexten) - 1);
	chan->macropriority = chan->priority;
	setmacrocontext=1;
  }
  argc = 1;
  /* Save old macro variables */
  save_macro_exten = pbx_builtin_getvar_helper(chan, "MACRO_EXTEN");
  if (save_macro_exten) save_macro_exten = strdup(save_macro_exten);
  pbx_builtin_setvar_helper(chan, "MACRO_EXTEN", oldexten);

  save_macro_context = pbx_builtin_getvar_helper(chan, "MACRO_CONTEXT");
  if (save_macro_context) save_macro_context = strdup(save_macro_context);
  pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", oldcontext);

  save_macro_priority = pbx_builtin_getvar_helper(chan, "MACRO_PRIORITY");
  if (save_macro_priority) save_macro_priority = strdup(save_macro_priority);
  snprintf(pc, sizeof(pc), "%d", oldpriority);
  pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", pc);
  
  save_macro_offset = pbx_builtin_getvar_helper(chan, "MACRO_OFFSET");
  if (save_macro_offset) save_macro_offset = strdup(save_macro_offset);
  pbx_builtin_setvar_helper(chan, "MACRO_OFFSET", NULL);

  /* Setup environment for new run */
  chan->exten[0] = 's';
  chan->exten[1] = '\0';
  strncpy(chan->context, fullmacro, sizeof(chan->context) - 1);
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
  while(ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
	if ((res = ast_spawn_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num))) {
		/* Something bad happened, or a hangup has been requested. */
		if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F'))) {
			/* Just return result as to the previous application as if it had been dialed */
			ast_log(LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
			break;
		}
		switch(res) {
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
  for (x=1;x<argc;x++) {
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
  if (save_macro_exten) free(save_macro_exten);
  pbx_builtin_setvar_helper(chan, "MACRO_CONTEXT", save_macro_context);
  if (save_macro_context) free(save_macro_context);
  pbx_builtin_setvar_helper(chan, "MACRO_PRIORITY", save_macro_priority);
  if (save_macro_priority) free(save_macro_priority);
  if (setmacrocontext) {
	chan->macrocontext[0] = '\0';
	chan->macroexten[0] = '\0';
	chan->macropriority = 0;
  }

  if (!strcasecmp(chan->context, fullmacro)) {
  	/* If we're leaving the macro normally, restore original information */
	chan->priority = oldpriority;
	strncpy(chan->context, oldcontext, sizeof(chan->context) - 1);
	if (!(chan->_softhangup & AST_SOFTHANGUP_ASYNCGOTO)) {
		/* Copy the extension, so long as we're not in softhangup, where we could be given an asyncgoto */
		strncpy(chan->exten, oldexten, sizeof(chan->exten) - 1);
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
  if (save_macro_offset) free(save_macro_offset);
  return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, macro_exec, synopsis, descrip);
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
