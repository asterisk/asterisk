/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004-2006 Tilghman Lesher <app_stack_v002@the-tilghman.com>.
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
 * \author Tilghman Lesher <app_stack_v002@the-tilghman.com>
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
"Gosub([[context|]exten|]priority)\n"
"  Jumps to the label specified, saving the return address.\n";
static const char *gosubif_descrip =
"GosubIf(condition?labeliftrue[:labeliffalse])\n"
"  If the condition is true, then jump to labeliftrue.  If false, jumps to\n"
"labeliffalse, if specified.  In either case, a jump saves the return point\n"
"in the dialplan, to be returned to with a Return.\n";
static const char *return_descrip =
"Return()\n"
"  Jumps to the last label on the stack, removing it.\n";
static const char *pop_descrip =
"StackPop()\n"
"  Removes last label on the stack, discarding it.\n";


static int pop_exec(struct ast_channel *chan, void *data)
{
	pbx_builtin_setvar_helper(chan, STACKVAR, NULL);

	return 0;
}

static int return_exec(struct ast_channel *chan, void *data)
{
	const char *label = pbx_builtin_getvar_helper(chan, STACKVAR);

	if (ast_strlen_zero(label)) {
		ast_log(LOG_ERROR, "Return without Gosub: stack is empty\n");
		return -1;
	} else if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_WARNING, "No next statement after Gosub?\n");
		return -1;
	}

	pbx_builtin_setvar_helper(chan, STACKVAR, NULL);
	return 0;
}

static int gosub_exec(struct ast_channel *chan, void *data)
{
	char newlabel[AST_MAX_EXTENSION * 2 + 3 + 11];
	struct ast_module_user *u;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: %s([[context|]exten|]priority)\n", app_gosub, app_gosub);
		return -1;
	}

	u = ast_module_user_add(chan);
	snprintf(newlabel, sizeof(newlabel), "%s|%s|%d", chan->context, chan->exten, chan->priority + 1);

	if (ast_parseable_goto(chan, data)) {
		ast_module_user_remove(u);
		return -1;
	}

	pbx_builtin_pushvar_helper(chan, STACKVAR, newlabel);
	ast_module_user_remove(u);

	return 0;
}

static int gosubif_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *condition="", *label1, *label2, *args;
	int res=0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "GosubIf requires an argument\n");
		return 0;
	}

	args = ast_strdupa(data);

	u = ast_module_user_add(chan);

	condition = strsep(&args, "?");
	label1 = strsep(&args, ":");
	label2 = args;

	if (pbx_checkcondition(condition)) {
		if (!ast_strlen_zero(label1)) {
			res = gosub_exec(chan, label1);
		}
	} else if (!ast_strlen_zero(label2)) {
		res = gosub_exec(chan, label2);
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
