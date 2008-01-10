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

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/channel.h"

static const char *app_gosub = "Gosub";
static const char *app_gosubif = "GosubIf";
static const char *app_return = "Return";
static const char *app_pop = "StackPop";

static const char *gosub_synopsis = "Jump to label, saving return address";
static const char *gosubif_synopsis = "Conditionally jump to label, saving return address";
static const char *return_synopsis = "Return from gosub routine";
static const char *pop_synopsis = "Remove one address from gosub stack";

static const char *gosub_descrip =
"  Gosub([[context,]exten,]priority[(arg1[,...][,argN])]):\n"
"Jumps to the label specified, saving the return address.\n";
static const char *gosubif_descrip =
"  GosubIf(condition?labeliftrue[(arg1[,...])][:labeliffalse[(arg1[,...])]]):\n"
"If the condition is true, then jump to labeliftrue.  If false, jumps to\n"
"labeliffalse, if specified.  In either case, a jump saves the return point\n"
"in the dialplan, to be returned to with a Return.\n";
static const char *return_descrip =
"  Return([return-value]):\n"
"Jumps to the last label on the stack, removing it.  The return value, if\n"
"any, is saved in the channel variable GOSUB_RETVAL.\n";
static const char *pop_descrip =
"  StackPop():\n"
"Removes last label on the stack, discarding it.\n";


static void gosub_free(void *data);

static struct ast_datastore_info stack_info = {
	.type = "GOSUB",
	.destroy = gosub_free,
};

struct gosub_stack_frame {
	AST_LIST_ENTRY(gosub_stack_frame) entries;
	/* 100 arguments is all that we support anyway, but this will handle up to 255 */
	unsigned char arguments;
	struct varshead varshead;
	int priority;
	char *context;
	char extension[0];
};

static int frame_set_var(struct ast_channel *chan, struct gosub_stack_frame *frame, const char *var, const char *value)
{
	struct ast_var_t *variables;
	int found = 0;

	/* Does this variable already exist? */
	AST_LIST_TRAVERSE(&frame->varshead, variables, entries) {
		if (!strcmp(var, ast_var_name(variables))) {
			found = 1;
			break;
		}
	}

	if (!ast_strlen_zero(value)) {
		if (!found) {
			variables = ast_var_assign(var, "");
			AST_LIST_INSERT_HEAD(&frame->varshead, variables, entries);
			pbx_builtin_pushvar_helper(chan, var, value);
		} else
			pbx_builtin_setvar_helper(chan, var, value);

		manager_event(EVENT_FLAG_DIALPLAN, "VarSet", 
			"Channel: %s\r\n"
			"Variable: LOCAL(%s)\r\n"
			"Value: %s\r\n"
			"Uniqueid: %s\r\n", 
			chan->name, var, value, chan->uniqueid);
	}
	return 0;
}

static void gosub_release_frame(struct ast_channel *chan, struct gosub_stack_frame *frame)
{
	unsigned char i;
	char argname[15];
	struct ast_var_t *vardata;

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

	/* Delete local variables */
	while ((vardata = AST_LIST_REMOVE_HEAD(&frame->varshead, entries))) {
		if (chan)
			pbx_builtin_setvar_helper(chan, ast_var_name(vardata), NULL);	
		ast_var_delete(vardata);
	}

	ast_free(frame);
}

static struct gosub_stack_frame *gosub_allocate_frame(const char *context, const char *extension, int priority, unsigned char arguments)
{
	struct gosub_stack_frame *new = NULL;
	int len_extension = strlen(extension), len_context = strlen(context);

	if ((new = ast_calloc(1, sizeof(*new) + 2 + len_extension + len_context))) {
		AST_LIST_HEAD_INIT_NOLOCK(&new->varshead);
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

	if (oldframe) {
		gosub_release_frame(chan, oldframe);
	} else {
		ast_debug(1, "%s called with an empty gosub stack\n", app_pop);
	}
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
	AST_DECLARE_APP_ARGS(args2,
		AST_APP_ARG(argval)[100];
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: %s([[context|]exten|]priority[(arg1[|...][|argN])])\n", app_gosub, app_gosub);
		return -1;
	}

	if (!stack_store) {
		ast_debug(1, "Channel %s has no datastore, so we're allocating one.\n", chan->name);
		stack_store = ast_channel_datastore_alloc(&stack_info, NULL);
		if (!stack_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  Gosub will fail.\n");
			return -1;
		}

		oldlist = ast_calloc(1, sizeof(*oldlist));
		if (!oldlist) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  Gosub will fail.\n");
			ast_channel_datastore_free(stack_store);
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

	if (!newframe)
		return -1;

	if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_ERROR, "Gosub address is invalid: '%s'\n", (char *)data);
		ast_free(newframe);
		return -1;
	}

	/* Now that we know for certain that we're going to a new location, set our arguments */
	for (i = 0; i < args2.argc; i++) {
		snprintf(argname, sizeof(argname), "ARG%d", i + 1);
		frame_set_var(chan, newframe, argname, args2.argval[i]);
		ast_debug(1, "Setting '%s' to '%s'\n", argname, args2.argval[i]);
	}

	/* And finally, save our return address */
	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	AST_LIST_INSERT_HEAD(oldlist, newframe, entries);
	AST_LIST_UNLOCK(oldlist);

	return 0;
}

static int gosubif_exec(struct ast_channel *chan, void *data)
{
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

	args = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(cond, args, '?');
	if (cond.argc != 2) {
		ast_log(LOG_WARNING, "GosubIf requires an argument: GosubIf(cond?label1(args):label2(args)\n");
		return 0;
	}

	AST_NONSTANDARD_APP_ARGS(label, cond.labels, ':');

	if (pbx_checkcondition(cond.ition)) {
		if (!ast_strlen_zero(label.iftrue))
			res = gosub_exec(chan, label.iftrue);
	} else if (!ast_strlen_zero(label.iffalse)) {
		res = gosub_exec(chan, label.iffalse);
	}

	return res;
}

static int local_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;
	struct gosub_stack_frame *frame;
	struct ast_var_t *variables;

	if (!stack_store)
		return -1;

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	frame = AST_LIST_FIRST(oldlist);
	AST_LIST_TRAVERSE(&frame->varshead, variables, entries) {
		if (!strcmp(data, ast_var_name(variables))) {
			const char *tmp = pbx_builtin_getvar_helper(chan, data);
			ast_copy_string(buf, S_OR(tmp, ""), len);
			break;
		}
	}
	AST_LIST_UNLOCK(oldlist);
	return 0;
}

static int local_write(struct ast_channel *chan, const char *cmd, char *var, const char *value)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;
	struct gosub_stack_frame *frame;

	if (!stack_store) {
		ast_log(LOG_ERROR, "Tried to set LOCAL(%s), but we aren't within a Gosub routine\n", var);
		return -1;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	frame = AST_LIST_FIRST(oldlist);

	if (frame)
		frame_set_var(chan, frame, var, value);

	AST_LIST_UNLOCK(oldlist);

	return 0;
}

static struct ast_custom_function local_function = {
	.name = "LOCAL",
	.synopsis = "Variables local to the gosub stack frame",
	.syntax = "LOCAL(<varname>)",
	.write = local_write,
	.read = local_read,
};

static int unload_module(void)
{
	ast_unregister_application(app_return);
	ast_unregister_application(app_pop);
	ast_unregister_application(app_gosubif);
	ast_unregister_application(app_gosub);
	ast_custom_function_unregister(&local_function);

	return 0;
}

static int load_module(void)
{
	ast_register_application(app_pop, pop_exec, pop_synopsis, pop_descrip);
	ast_register_application(app_return, return_exec, return_synopsis, return_descrip);
	ast_register_application(app_gosubif, gosubif_exec, gosubif_synopsis, gosubif_descrip);
	ast_register_application(app_gosub, gosub_exec, gosub_synopsis, gosub_descrip);
	ast_custom_function_register(&local_function);

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialplan subroutines (Gosub, Return, etc)");
