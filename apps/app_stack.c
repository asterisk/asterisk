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

/*** MODULEINFO
	<use>res_agi</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
 
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/channel.h"
#include "asterisk/agi.h"

/*** DOCUMENTATION
	<application name="Gosub" language="en_US">
		<synopsis>
			Jump to label, saving return address.
		</synopsis>
		<syntax>
			<parameter name="context" />
			<parameter name="exten" />
			<parameter name="priority" required="true" hasparams="optional">
				<argument name="arg1" multiple="true" required="true" />
				<argument name="argN" />
			</parameter>
		</syntax>
		<description>
			<para>Jumps to the label specified, saving the return address.</para>
		</description>
		<see-also>
			<ref type="application">GosubIf</ref>
			<ref type="application">Macro</ref>
			<ref type="application">Goto</ref>
			<ref type="application">Return</ref>
			<ref type="application">StackPop</ref>
		</see-also>
	</application>
	<application name="GosubIf" language="en_US">
		<synopsis>
			Conditionally jump to label, saving return address.
		</synopsis>
		<syntax argsep="?">
			<parameter name="condition" required="true" />
			<parameter name="destination" required="true" argsep=":">
				<argument name="labeliftrue" hasparams="optional">
					<argument name="arg1" required="true" multiple="true" />
					<argument name="argN" />
				</argument>
				<argument name="labeliffalse" hasparams="optional">
					<argument name="arg1" required="true" multiple="true" />
					<argument name="argN" />
				</argument>
			</parameter>
		</syntax>
		<description>
			<para>If the condition is true, then jump to labeliftrue.  If false, jumps to
			labeliffalse, if specified.  In either case, a jump saves the return point
			in the dialplan, to be returned to with a Return.</para>
		</description>
		<see-also>
			<ref type="application">Gosub</ref>
			<ref type="application">Return</ref>
			<ref type="application">MacroIf</ref>
			<ref type="function">IF</ref>
			<ref type="application">GotoIf</ref>
		</see-also>
	</application>
	<application name="Return" language="en_US">
		<synopsis>
			Return from gosub routine.
		</synopsis>
		<syntax>
			<parameter name="value">
				<para>Return value.</para>
			</parameter>
		</syntax>
		<description>
			<para>Jumps to the last label on the stack, removing it. The return <replaceable>value</replaceable>, if
			any, is saved in the channel variable <variable>GOSUB_RETVAL</variable>.</para>
		</description>
		<see-also>
			<ref type="application">Gosub</ref>
			<ref type="application">StackPop</ref>
		</see-also>
	</application>
	<application name="StackPop" language="en_US">
		<synopsis>
			Remove one address from gosub stack.
		</synopsis>
		<syntax />
		<description>
			<para>Removes last label on the stack, discarding it.</para>
		</description>
		<see-also>
			<ref type="application">Return</ref>
			<ref type="application">Gosub</ref>
		</see-also>
	</application>
	<function name="LOCAL" language="en_US">
		<synopsis>
			Manage variables local to the gosub stack frame.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
		</syntax>
		<description>
			<para>Read and write a variable local to the gosub stack frame, once we Return() it will be lost
			(or it will go back to whatever value it had before the Gosub()).</para>
		</description>
		<see-also>
			<ref type="application">Gosub</ref>
			<ref type="application">GosubIf</ref>
			<ref type="application">Return</ref>
		</see-also>
	</function>
	<function name="LOCAL_PEEK" language="en_US">
		<synopsis>
			Retrieve variables hidden by the local gosub stack frame.
		</synopsis>
		<syntax>
			<parameter name="n" required="true" />
			<parameter name="varname" required="true" />
		</syntax>
		<description>
			<para>Read a variable <replaceable>varname</replaceable> hidden by
			<replaceable>n</replaceable> levels of gosub stack frames.  Note that ${LOCAL_PEEK(0,foo)}
			is the same as <variable>foo</variable>, since the value of <replaceable>n</replaceable>
			peeks under 0 levels of stack frames; in other words, 0 is the current level.  If
			<replaceable>n</replaceable> exceeds the available number of stack frames, then an empty
			string is returned.</para>
		</description>
		<see-also>
			<ref type="application">Gosub</ref>
			<ref type="application">GosubIf</ref>
			<ref type="application">Return</ref>
		</see-also>
	</function>
	<agi name="gosub" language="en_US">
		<synopsis>
			Cause the channel to execute the specified dialplan subroutine.
		</synopsis>
		<syntax>
			<parameter name="context" required="true" />
			<parameter name="extension" required="true" />
			<parameter name="priority" required="true" />
			<parameter name="optional-argument" />
		</syntax>
		<description>
			<para>Cause the channel to execute the specified dialplan subroutine,
			returning to the dialplan with execution of a Return().</para>
		</description>
	</agi>
 ***/

static const char * const app_gosub = "Gosub";
static const char * const app_gosubif = "GosubIf";
static const char * const app_return = "Return";
static const char * const app_pop = "StackPop";

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
	unsigned int is_agi:1;
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

	if (!found) {
		variables = ast_var_assign(var, "");
		AST_LIST_INSERT_HEAD(&frame->varshead, variables, entries);
		pbx_builtin_pushvar_helper(chan, var, value);
	} else {
		pbx_builtin_setvar_helper(chan, var, value);
	}

	manager_event(EVENT_FLAG_DIALPLAN, "VarSet",
		"Channel: %s\r\n"
		"Variable: LOCAL(%s)\r\n"
		"Value: %s\r\n"
		"Uniqueid: %s\r\n",
		chan->name, var, value, chan->uniqueid);
	return 0;
}

static void gosub_release_frame(struct ast_channel *chan, struct gosub_stack_frame *frame)
{
	struct ast_var_t *vardata;

	/* If chan is not defined, then we're calling it as part of gosub_free,
	 * and the channel variables will be deallocated anyway.  Otherwise, we're
	 * just releasing a single frame, so we need to clean up the arguments for
	 * that frame, so that we re-expose the variables from the previous frame
	 * that were hidden by this one.
	 */
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

static int pop_exec(struct ast_channel *chan, const char *data)
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

static int return_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	struct gosub_stack_frame *oldframe;
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;
	const char *retval = data;
	int res = 0;

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
	} else if (oldframe->is_agi) {
		/* Exit from AGI */
		res = -1;
	}

	ast_explicit_goto(chan, oldframe->context, oldframe->extension, oldframe->priority);
	gosub_release_frame(chan, oldframe);

	/* Set a return value, if any */
	pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", S_OR(retval, ""));
	return res;
}

static int gosub_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	AST_LIST_HEAD(, gosub_stack_frame) *oldlist;
	struct gosub_stack_frame *newframe, *lastframe;
	char argname[15], *tmp = ast_strdupa(data), *label, *endparen;
	int i, max_argc = 0;
	AST_DECLARE_APP_ARGS(args2,
		AST_APP_ARG(argval)[100];
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: %s([[context,]exten,]priority[(arg1[,...][,argN])])\n", app_gosub, app_gosub);
		return -1;
	}

	if (!stack_store) {
		ast_debug(1, "Channel %s has no datastore, so we're allocating one.\n", chan->name);
		stack_store = ast_datastore_alloc(&stack_info, NULL);
		if (!stack_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  Gosub will fail.\n");
			return -1;
		}

		oldlist = ast_calloc(1, sizeof(*oldlist));
		if (!oldlist) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  Gosub will fail.\n");
			ast_datastore_free(stack_store);
			return -1;
		}

		stack_store->data = oldlist;
		AST_LIST_HEAD_INIT(oldlist);
		ast_channel_datastore_add(chan, stack_store);
	} else {
		oldlist = stack_store->data;
	}

	if ((lastframe = AST_LIST_FIRST(oldlist))) {
		max_argc = lastframe->arguments;
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
		AST_STANDARD_RAW_ARGS(args2, tmp);
	} else
		args2.argc = 0;

	/* Mask out previous arguments in this invocation */
	if (args2.argc > max_argc) {
		max_argc = args2.argc;
	}

	/* Create the return address, but don't save it until we know that the Gosub destination exists */
	newframe = gosub_allocate_frame(chan->context, chan->exten, chan->priority + 1, max_argc);

	if (!newframe) {
		return -1;
	}

	if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_ERROR, "Gosub address is invalid: '%s'\n", (char *)data);
		ast_free(newframe);
		return -1;
	}

	if (!ast_exists_extension(chan, chan->context, chan->exten,
		ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP) ? chan->priority + 1 : chan->priority,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
		ast_log(LOG_ERROR, "Attempt to reach a non-existent destination for gosub: (Context:%s, Extension:%s, Priority:%d)\n",
				chan->context, chan->exten, ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP) ? chan->priority + 1 : chan->priority);
		ast_copy_string(chan->context, newframe->context, sizeof(chan->context));
		ast_copy_string(chan->exten, newframe->extension, sizeof(chan->exten));
		chan->priority = newframe->priority;
		ast_free(newframe);
		return -1;
	}

	/* Now that we know for certain that we're going to a new location, set our arguments */
	for (i = 0; i < max_argc; i++) {
		snprintf(argname, sizeof(argname), "ARG%d", i + 1);
		frame_set_var(chan, newframe, argname, i < args2.argc ? args2.argval[i] : "");
		ast_debug(1, "Setting '%s' to '%s'\n", argname, i < args2.argc ? args2.argval[i] : "");
	}
	snprintf(argname, sizeof(argname), "%d", args2.argc);
	frame_set_var(chan, newframe, "ARGC", argname);

	/* And finally, save our return address */
	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	AST_LIST_INSERT_HEAD(oldlist, newframe, entries);
	AST_LIST_UNLOCK(oldlist);

	return 0;
}

static int gosubif_exec(struct ast_channel *chan, const char *data)
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
	AST_NONSTANDARD_RAW_ARGS(cond, args, '?');
	if (cond.argc != 2) {
		ast_log(LOG_WARNING, "GosubIf requires an argument: GosubIf(cond?label1(args):label2(args)\n");
		return 0;
	}

	AST_NONSTANDARD_RAW_ARGS(label, cond.labels, ':');

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
	if (!(frame = AST_LIST_FIRST(oldlist))) {
		/* Not within a Gosub routine */
		AST_LIST_UNLOCK(oldlist);
		return -1;
	}

	AST_LIST_TRAVERSE(&frame->varshead, variables, entries) {
		if (!strcmp(data, ast_var_name(variables))) {
			const char *tmp;
			ast_channel_lock(chan);
			tmp = pbx_builtin_getvar_helper(chan, data);
			ast_copy_string(buf, S_OR(tmp, ""), len);
			ast_channel_unlock(chan);
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
	.write = local_write,
	.read = local_read,
};

static int peek_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int found = 0, n;
	struct ast_var_t *variables;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(n);
		AST_APP_ARG(name);
	);

	if (!chan) {
		ast_log(LOG_ERROR, "LOCAL_PEEK must be called on an active channel\n");
		return -1;
	}

	AST_STANDARD_RAW_ARGS(args, data);
	n = atoi(args.n);
	*buf = '\0';

	ast_channel_lock(chan);
	AST_LIST_TRAVERSE(&chan->varshead, variables, entries) {
		if (!strcmp(args.name, ast_var_name(variables)) && ++found > n) {
			ast_copy_string(buf, ast_var_value(variables), len);
			break;
		}
	}
	ast_channel_unlock(chan);
	return 0;
}

static struct ast_custom_function peek_function = {
	.name = "LOCAL_PEEK",
	.read = peek_read,
};

static int handle_gosub(struct ast_channel *chan, AGI *agi, int argc, const char * const *argv)
{
	int old_priority, priority;
	char old_context[AST_MAX_CONTEXT], old_extension[AST_MAX_EXTENSION];
	struct ast_app *theapp;
	char *gosub_args;

	if (argc < 4 || argc > 5) {
		return RESULT_SHOWUSAGE;
	}

	ast_debug(1, "Gosub called with %d arguments: 0:%s 1:%s 2:%s 3:%s 4:%s\n", argc, argv[0], argv[1], argv[2], argv[3], argc == 5 ? argv[4] : "");

	if (sscanf(argv[3], "%30d", &priority) != 1 || priority < 1) {
		/* Lookup the priority label */
		priority = ast_findlabel_extension(chan, argv[1], argv[2], argv[3],
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL));
		if (priority < 0) {
			ast_log(LOG_ERROR, "Priority '%s' not found in '%s@%s'\n", argv[3], argv[2], argv[1]);
			ast_agi_send(agi->fd, chan, "200 result=-1 Gosub label not found\n");
			return RESULT_FAILURE;
		}
	} else if (!ast_exists_extension(chan, argv[1], argv[2], priority,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
		ast_agi_send(agi->fd, chan, "200 result=-1 Gosub label not found\n");
		return RESULT_FAILURE;
	}

	/* Save previous location, since we're going to change it */
	ast_copy_string(old_context, chan->context, sizeof(old_context));
	ast_copy_string(old_extension, chan->exten, sizeof(old_extension));
	old_priority = chan->priority;

	if (!(theapp = pbx_findapp("Gosub"))) {
		ast_log(LOG_ERROR, "Gosub() cannot be found in the list of loaded applications\n");
		ast_agi_send(agi->fd, chan, "503 result=-2 Gosub is not loaded\n");
		return RESULT_FAILURE;
	}

	/* Apparently, if you run ast_pbx_run on a channel that already has a pbx
	 * structure, you need to add 1 to the priority to get it to go to the
	 * right place.  But if it doesn't have a pbx structure, then leaving off
	 * the 1 is the right thing to do.  See how this code differs when we
	 * call a Gosub for the CALLEE channel in Dial or Queue.
	 */
	if (argc == 5) {
		if (asprintf(&gosub_args, "%s,%s,%d(%s)", argv[1], argv[2], priority + (chan->pbx ? 1 : 0), argv[4]) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			gosub_args = NULL;
		}
	} else {
		if (asprintf(&gosub_args, "%s,%s,%d", argv[1], argv[2], priority + (chan->pbx ? 1 : 0)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			gosub_args = NULL;
		}
	}

	if (gosub_args) {
		int res;

		ast_debug(1, "Trying gosub with arguments '%s'\n", gosub_args);

		if ((res = pbx_exec(chan, theapp, gosub_args)) == 0) {
			struct ast_pbx *pbx = chan->pbx;
			struct ast_pbx_args args;
			struct ast_datastore *stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
			AST_LIST_HEAD(, gosub_stack_frame) *oldlist = stack_store->data;
			struct gosub_stack_frame *cur = AST_LIST_FIRST(oldlist);
			cur->is_agi = 1;

			memset(&args, 0, sizeof(args));
			args.no_hangup_chan = 1;
			/* Suppress warning about PBX already existing */
			chan->pbx = NULL;
			ast_agi_send(agi->fd, chan, "100 result=0 Trying...\n");
			ast_pbx_run_args(chan, &args);
			ast_agi_send(agi->fd, chan, "200 result=0 Gosub complete\n");
			if (chan->pbx) {
				ast_free(chan->pbx);
			}
			chan->pbx = pbx;
		} else {
			ast_agi_send(agi->fd, chan, "200 result=%d Gosub failed\n", res);
		}
		ast_free(gosub_args);
	} else {
		ast_agi_send(agi->fd, chan, "503 result=-2 Memory allocation failure\n");
		return RESULT_FAILURE;
	}

	/* Restore previous location */
	ast_copy_string(chan->context, old_context, sizeof(chan->context));
	ast_copy_string(chan->exten, old_extension, sizeof(chan->exten));
	chan->priority = old_priority;

	return RESULT_SUCCESS;
}

static struct agi_command gosub_agi_command =
	{ { "gosub", NULL }, handle_gosub, NULL, NULL, 0 };

static int unload_module(void)
{
	ast_agi_unregister(ast_module_info->self, &gosub_agi_command);

	ast_unregister_application(app_return);
	ast_unregister_application(app_pop);
	ast_unregister_application(app_gosubif);
	ast_unregister_application(app_gosub);
	ast_custom_function_unregister(&local_function);
	ast_custom_function_unregister(&peek_function);

	return 0;
}

static int load_module(void)
{
	ast_agi_register(ast_module_info->self, &gosub_agi_command);

	ast_register_application_xml(app_pop, pop_exec);
	ast_register_application_xml(app_return, return_exec);
	ast_register_application_xml(app_gosubif, gosubif_exec);
	ast_register_application_xml(app_gosub, gosub_exec);
	ast_custom_function_register(&local_function);
	ast_custom_function_register(&peek_function);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan subroutines (Gosub, Return, etc)",
		.load = load_module,
		.unload = unload_module,
		.nonoptreq = "res_agi",
		);
