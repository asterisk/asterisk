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


static void gosub_free(void *data);

static struct ast_datastore_info stack_info = {
	.type = "GOSUB",
	.destroy = gosub_free,
};

struct gosub_stack_frame {
	AST_LIST_ENTRY(gosub_stack_frame) entries;
	/* 100 arguments is all that we support anyway, but this will handle up to 255 */
	unsigned char arguments;
	int priority;
	char *context;
	char extension[0];
};

static void gosub_release_frame(struct ast_channel *chan, struct gosub_stack_frame *frame)
{
	unsigned char i;
	char argname[15];

	/* If chan is not defined, then we're calling it as part of gosub_free,
	 * and the channel variables will be deallocated anyway.  Otherwise, we're
	 * just releasing a single frame, so we need to clean up the arguments for
	 * that frame, so that we re-expose the variables from the previous frame
	 * that were hidden by this one.
	 */
	if (chan) {
		for (i = 1; i <= frame->arguments && i != 0; i++) {
			snprintf(argname, sizeof(argname), "ARG%hhd", i);
			pbx_builtin_setvar_helper(chan, argname, NULL);
		}
	}
	ast_free(frame);
}

static struct gosub_stack_frame *gosub_allocate_frame(const char *context, const char *extension, int priority, unsigned char arguments)
{
	struct gosub_stack_frame *new = NULL;
	int len_extension = strlen(extension), len_context = strlen(context);

	if ((new = ast_calloc(1, sizeof(*new) + 2 + len_extension + len_context))) {
		strcpy(new->extension, extension);
		new->context = new->extension + len_extension + 1;
		strcpy(new->context, context);
		new->priority = priority;
		new->arguments = arguments;
	}
	return new;
}

static void gosub_free(void *data)
{
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist = data;
	struct gosub_stack_frame *oldframe;
	AST_LIST_LOCK(oldlist);
	while ((oldframe = AST_LIST_REMOVE_HEAD(oldlist, entries))) {
		gosub_release_frame(NULL, oldframe);
	}
	AST_LIST_UNLOCK(oldlist);
	AST_LIST_HEAD_DESTROY(oldlist);
	ast_free(oldlist);
}

static int pop_exec(struct ast_channel *chan, void *data)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	struct gosub_stack_frame *oldframe;
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;

	if (!stack_store) {
		ast_log(LOG_WARNING, "%s called with no gosub stack allocated.\n", app_pop);
		return 0;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	oldframe = AST_LIST_REMOVE_HEAD(oldlist, entries);
	AST_LIST_UNLOCK(oldlist);

	if (oldframe)
		gosub_release_frame(chan, oldframe);
	else if (option_debug)
		ast_log(LOG_DEBUG, "%s called with an empty gosub stack\n", app_pop);

	return 0;
}

static int return_exec(struct ast_channel *chan, void *data)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	struct gosub_stack_frame *oldframe;
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;
	char *retval = data;

	if (!stack_store) {
		ast_log(LOG_ERROR, "Return without Gosub: stack is unallocated\n");
		return -1;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	oldframe = AST_LIST_REMOVE_HEAD(oldlist, entries);
	AST_LIST_UNLOCK(oldlist);

	if (!oldframe) {
		ast_log(LOG_ERROR, "Return without Gosub: stack is empty\n");
		return -1;
	}

	ast_explicit_goto(chan, oldframe->context, oldframe->extension, oldframe->priority);
	gosub_release_frame(chan, oldframe);

	/* Set a return value, if any */
	pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", S_OR(retval, ""));
	return 0;
}

static int gosub_exec(struct ast_channel *chan, void *data)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;
	struct gosub_stack_frame *newframe;
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

	if (!stack_store) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Channel %s has no datastore, so we're allocating one.\n", chan->name);
		stack_store = ast_channel_datastore_alloc(&stack_info, NULL);
		if (!stack_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  Gosub will fail.\n");
			ast_module_user_remove(u);
			return -1;
		}

		oldlist = ast_calloc(1, sizeof(*oldlist));
		if (!oldlist) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  Gosub will fail.\n");
			ast_channel_datastore_free(stack_store);
			ast_module_user_remove(u);
			return -1;
		}

		stack_store->data = oldlist;
		AST_LIST_HEAD_INIT(oldlist);
		ast_channel_datastore_add(chan, stack_store);
	}

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
	newframe = gosub_allocate_frame(chan->context, chan->exten, chan->priority + 1, args2.argc);

	if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_ERROR, "Gosub address is invalid: '%s'\n", (char *)data);
		ast_free(newframe);
		ast_module_user_remove(u);
		return -1;
	}

	/* Now that we know for certain that we're going to a new location, set our arguments */
	for (i = 0; i < args2.argc; i++) {
		snprintf(argname, sizeof(argname), "ARG%d", i + 1);
		pbx_builtin_pushvar_helper(chan, argname, args2.argval[i]);
		if (option_debug)
			ast_log(LOG_DEBUG, "Setting '%s' to '%s'\n", argname, args2.argval[i]);
	}

	/* And finally, save our return address */
	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	AST_LIST_INSERT_HEAD(oldlist, newframe, entries);
	AST_LIST_UNLOCK(oldlist);

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
