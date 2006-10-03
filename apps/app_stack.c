/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004-2006 Tilghman Lesher <app_stack_v003@the-tilghman.com>.
 *
 * This code is released by the author with no restrictions on usage.
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
 * \brief Stack applications Gosub, Return, etc.
 *
 * \author Tilghman Lesher <app_stack_v003@the-tilghman.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"
 
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/chanvars.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"

#define STACKVAR	"~GOSUB~STACK~"


static const char *app_gosub = "Gosub";
static const char *app_gosubif = "GosubIf";
static const char *app_return = "Return";
static const char *app_pop = "StackPop";

static const char *gosub_synopsis = "Jump to label, saving return address";
static const char *gosubif_synopsis = "Conditionally jump to label, saving return address";
static const char *return_synopsis = "Return from gosub routine";
static const char *pop_synopsis = "Remove one address from gosub stack";

static const char *gosub_descrip =
"Gosub([[context|]exten|]priority[(arg1[|...][|argN])])\n"
"  Jumps to the label specified, saving the return address.\n";
static const char *gosubif_descrip =
"GosubIf(condition?labeliftrue[(arg1[|...])][:labeliffalse[(arg1[|...])]])\n"
"  If the condition is true, then jump to labeliftrue.  If false, jumps to\n"
"labeliffalse, if specified.  In either case, a jump saves the return point\n"
"in the dialplan, to be returned to with a Return.\n";
static const char *return_descrip =
"Return([return-value])\n"
"  Jumps to the last label on the stack, removing it.  The return value, if\n"
"any, is saved in the channel variable GOSUB_RETVAL.\n";
static const char *pop_descrip =
"StackPop()\n"
"  Removes last label on the stack, discarding it.\n";


static int pop_exec(struct ast_channel *chan, void *data)
{
	const char *frame = pbx_builtin_getvar_helper(chan, STACKVAR);
	int numargs = 0, i;
	char argname[15];

	/* Pop any arguments for this stack frame off the variable stack */
	if (frame) {
		numargs = atoi(frame);
		for (i = 1; i <= numargs; i++) {
			snprintf(argname, sizeof(argname), "ARG%d", i);
			pbx_builtin_setvar_helper(chan, argname, NULL);
		}
	}

	/* Remove the last frame from the Gosub stack */
	pbx_builtin_setvar_helper(chan, STACKVAR, NULL);

	return 0;
}

static int return_exec(struct ast_channel *chan, void *data)
{
	const char *label = pbx_builtin_getvar_helper(chan, STACKVAR);
	char argname[15], *retval = data;
	int numargs, i;

	if (ast_strlen_zero(label)) {
		ast_log(LOG_ERROR, "Return without Gosub: stack is empty\n");
		return -1;
	}

	/* Pop any arguments for this stack frame off the variable stack */
	numargs = atoi(label);
	for (i = 1; i <= numargs; i++) {
		snprintf(argname, sizeof(argname), "ARG%d", i);
		pbx_builtin_setvar_helper(chan, argname, NULL);
	}

	/* If the label exists, it will always have a ':' */
	label = strchr(label, ':') + 1;

	if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_WARNING, "No next statement after Gosub?\n");
		return -1;
	}

	/* Remove the current frame from the Gosub stack */
	pbx_builtin_setvar_helper(chan, STACKVAR, NULL);

	/* Set a return value, if any */
	pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", S_OR(retval, ""));
	return 0;
}

static int gosub_exec(struct ast_channel *chan, void *data)
{
	char newlabel[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 11 + 11 + 4];
	char argname[15], *tmp = ast_strdupa(data), *label, *endparen;
	int i;
	struct ast_module_user *u;
	AST_DECLARE_APP_ARGS(args2,
		AST_APP_ARG(argval)[100];
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: %s([[context|]exten|]priority[(arg1[|...][|argN])])\n", app_gosub, app_gosub);
		return -1;
	}

	u = ast_module_user_add(chan);

	/* Separate the arguments from the label */
	/* NOTE:  you cannot use ast_app_separate_args for this, because '(' cannot be used as a delimiter. */
	label = strsep(&tmp, "(");
	if (tmp) {
		endparen = strrchr(tmp, ')');
		if (endparen)
			*endparen = '\0';
		else
			ast_log(LOG_WARNING, "Ouch.  No closing paren: '%s'?\n", (char *)data);
		AST_STANDARD_APP_ARGS(args2, tmp);
	} else
		args2.argc = 0;

	/* Create the return address, but don't save it until we know that the Gosub destination exists */
	snprintf(newlabel, sizeof(newlabel), "%d:%s|%s|%d", args2.argc, chan->context, chan->exten, chan->priority + 1);

	if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_ERROR, "Gosub address is invalid: '%s'\n", (char *)data);
		ast_module_user_remove(u);
		return -1;
	}

	/* Now that we know for certain that we're going to a new location, set our arguments */
	for (i = 0; i < args2.argc; i++) {
		snprintf(argname, sizeof(argname), "ARG%d", i + 1);
		pbx_builtin_pushvar_helper(chan, argname, args2.argval[i]);
		ast_log(LOG_DEBUG, "Setting '%s' to '%s'\n", argname, args2.argval[i]);
	}

	/* And finally, save our return address */
	pbx_builtin_pushvar_helper(chan, STACKVAR, newlabel);
	ast_log(LOG_DEBUG, "Setting gosub return address to '%s'\n", newlabel);
	ast_module_user_remove(u);

	return 0;
}

static int gosubif_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *args;
	int res=0;
	AST_DECLARE_APP_ARGS(cond,
		AST_APP_ARG(ition);
		AST_APP_ARG(labels);
	);
	AST_DECLARE_APP_ARGS(label,
		AST_APP_ARG(iftrue);
		AST_APP_ARG(iffalse);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "GosubIf requires an argument: GosubIf(cond?label1(args):label2(args)\n");
		return 0;
	}

	u = ast_module_user_add(chan);

	args = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(cond, args, '?');
	if (cond.argc != 2) {
		ast_log(LOG_WARNING, "GosubIf requires an argument: GosubIf(cond?label1(args):label2(args)\n");
		ast_module_user_remove(u);
		return 0;
	}

	AST_NONSTANDARD_APP_ARGS(label, cond.labels, ':');

	if (pbx_checkcondition(cond.ition)) {
		if (!ast_strlen_zero(label.iftrue))
			res = gosub_exec(chan, label.iftrue);
	} else if (!ast_strlen_zero(label.iffalse)) {
		res = gosub_exec(chan, label.iffalse);
	}

	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	ast_unregister_application(app_return);
	ast_unregister_application(app_pop);
	ast_unregister_application(app_gosubif);
	ast_unregister_application(app_gosub);

	ast_module_user_hangup_all();

	return 0;
}

static int load_module(void)
{
	ast_register_application(app_pop, pop_exec, pop_synopsis, pop_descrip);
	ast_register_application(app_return, return_exec, return_synopsis, return_descrip);
	ast_register_application(app_gosubif, gosubif_exec, gosubif_synopsis, gosubif_descrip);
	ast_register_application(app_gosub, gosub_exec, gosub_synopsis, gosub_descrip);

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Stack Routines");
