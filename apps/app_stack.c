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
	<use type="module">res_agi</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/channel.h"
#include "asterisk/agi.h"
#include "asterisk/stasis_channels.h"

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
					<para>Continue at <replaceable>labeliftrue</replaceable> if the condition is true.
					Takes the form similar to Goto() of [[context,]extension,]priority.</para>
					<argument name="arg1" required="true" multiple="true" />
					<argument name="argN" />
				</argument>
				<argument name="labeliffalse" hasparams="optional">
					<para>Continue at <replaceable>labeliffalse</replaceable> if the condition is false.
					Takes the form similar to Goto() of [[context,]extension,]priority.</para>
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
			<ref type="application">Goto</ref>
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
	<function name="STACK_PEEK" language="en_US">
		<synopsis>
			View info about the location which called Gosub
		</synopsis>
		<syntax>
			<parameter name="n" required="true" />
			<parameter name="which" required="true" />
			<parameter name="suppress" required="false" />
		</syntax>
		<description>
			<para>Read the calling <literal>c</literal>ontext, <literal>e</literal>xtension,
			<literal>p</literal>riority, or <literal>l</literal>abel, as specified by
			<replaceable>which</replaceable>, by going up <replaceable>n</replaceable> frames
			in the Gosub stack.  If <replaceable>suppress</replaceable> is true, then if the
			number of available stack frames is exceeded, then no error message will be
			printed.</para>
		</description>
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
		<see-also>
			<ref type="application">GoSub</ref>
		</see-also>
	</agi>
	<managerEvent language="en_US" name="VarSet">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>Raised when a variable local to the gosub stack frame is set due to a subroutine call.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Variable">
					<para>The LOCAL variable being set.</para>
					<note><para>The variable name will always be enclosed with
					<literal>LOCAL()</literal></para></note>
				</parameter>
				<parameter name="Value">
					<para>The new value of the variable.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">GoSub</ref>
				<ref type="agi">gosub</ref>
				<ref type="function">LOCAL</ref>
				<ref type="function">LOCAL_PEEK</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

static const char app_gosub[] = "Gosub";
static const char app_gosubif[] = "GosubIf";
static const char app_return[] = "Return";
static const char app_pop[] = "StackPop";

static void gosub_free(void *data);

static const struct ast_datastore_info stack_info = {
	.type = "GOSUB",
	.destroy = gosub_free,
};

struct gosub_stack_frame {
	AST_LIST_ENTRY(gosub_stack_frame) entries;
	/* 100 arguments is all that we support anyway, but this will handle up to 255 */
	unsigned char arguments;
	struct varshead varshead;
	int priority;
	/*! TRUE if the return location marks the end of a special routine. */
	unsigned int is_special:1;
	/*! Whether or not we were in a subroutine when this one was created */
	unsigned int in_subroutine:1;
	char *context;
	char extension[0];
};

AST_LIST_HEAD(gosub_stack_list, gosub_stack_frame);

static int frame_set_var(struct ast_channel *chan, struct gosub_stack_frame *frame, const char *var, const char *value)
{
	struct ast_var_t *variables;
	int found = 0;
	int len;
	RAII_VAR(char *, local_buffer, NULL, ast_free);

	/* Does this variable already exist? */
	AST_LIST_TRAVERSE(&frame->varshead, variables, entries) {
		if (!strcmp(var, ast_var_name(variables))) {
			found = 1;
			break;
		}
	}

	if (!found) {
		if ((variables = ast_var_assign(var, ""))) {
			AST_LIST_INSERT_HEAD(&frame->varshead, variables, entries);
		}
		pbx_builtin_pushvar_helper(chan, var, value);
	} else {
		pbx_builtin_setvar_helper(chan, var, value);
	}

	len = 8 + strlen(var); /* LOCAL() + var */
	local_buffer = ast_malloc(len);
	if (!local_buffer) {
		return 0;
	}
	sprintf(local_buffer, "LOCAL(%s)", var);
	ast_channel_publish_varset(chan, local_buffer, value);
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

static struct gosub_stack_frame *gosub_allocate_frame(const char *context, const char *extension, int priority, int in_subroutine, unsigned char arguments)
{
	struct gosub_stack_frame *new = NULL;
	int len_extension = strlen(extension) + 1;
	int len_context = strlen(context) + 1;

	if ((new = ast_calloc(1, sizeof(*new) + len_extension + len_context))) {
		AST_LIST_HEAD_INIT_NOLOCK(&new->varshead);
		ast_copy_string(new->extension, extension, len_extension);
		new->context = new->extension + len_extension;
		ast_copy_string(new->context, context, len_context);
		new->priority = priority;
		new->in_subroutine = in_subroutine ? 1 : 0;
		new->arguments = arguments;
	}
	return new;
}

static void gosub_free(void *data)
{
	struct gosub_stack_list *oldlist = data;
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
	struct ast_datastore *stack_store;
	struct gosub_stack_frame *oldframe;
	struct gosub_stack_list *oldlist;
	int res = 0;

	ast_channel_lock(chan);
	if (!(stack_store = ast_channel_datastore_find(chan, &stack_info, NULL))) {
		ast_log(LOG_WARNING, "%s called with no gosub stack allocated.\n", app_pop);
		ast_channel_unlock(chan);
		return 0;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	oldframe = AST_LIST_FIRST(oldlist);
	if (oldframe) {
		if (oldframe->is_special) {
			ast_debug(1, "%s attempted to pop special return location.\n", app_pop);

			/* Abort the special routine dialplan execution.  Dialplan programming error. */
			res = -1;
		} else {
			AST_LIST_REMOVE_HEAD(oldlist, entries);
			gosub_release_frame(chan, oldframe);
		}
	} else {
		ast_debug(1, "%s called with an empty gosub stack\n", app_pop);
	}
	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);
	return res;
}

static int return_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *stack_store;
	struct gosub_stack_frame *oldframe;
	struct gosub_stack_list *oldlist;
	const char *retval = data;
	int res = 0;

	ast_channel_lock(chan);
	if (!(stack_store = ast_channel_datastore_find(chan, &stack_info, NULL))) {
		ast_log(LOG_ERROR, "Return without Gosub: stack is unallocated\n");
		ast_channel_unlock(chan);
		return -1;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	oldframe = AST_LIST_REMOVE_HEAD(oldlist, entries);
	AST_LIST_UNLOCK(oldlist);

	if (!oldframe) {
		ast_log(LOG_ERROR, "Return without Gosub: stack is empty\n");
		ast_channel_unlock(chan);
		return -1;
	}
	if (oldframe->is_special) {
		/* Exit from special routine. */
		res = -1;
	}

	/*
	 * We cannot use ast_explicit_goto() because we MUST restore
	 * what was there before.  Channels that do not have a PBX may
	 * not have the context or exten set.
	 */
	ast_channel_context_set(chan, oldframe->context);
	ast_channel_exten_set(chan, oldframe->extension);
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP)) {
		--oldframe->priority;
	}
	ast_channel_priority_set(chan, oldframe->priority);
	ast_set2_flag(ast_channel_flags(chan), oldframe->in_subroutine, AST_FLAG_SUBROUTINE_EXEC);

	gosub_release_frame(chan, oldframe);

	/* Set a return value, if any */
	pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", S_OR(retval, ""));
	ast_channel_unlock(chan);
	return res;
}

/*!
 * \internal
 * \brief Add missing context and/or exten to Gosub application argument string.
 * \since 11.0
 *
 * \param chan Channel to obtain context/exten.
 * \param args Gosub application argument string.
 *
 * \details
 * Fills in the optional context and exten from the given channel.
 * Convert: [[context,]exten,]priority[(arg1[,...][,argN])]
 * To: context,exten,priority[(arg1[,...][,argN])]
 *
 * \retval expanded Gosub argument string on success.  Must be freed.
 * \retval NULL on error.
 *
 * \note The parsing needs to be kept in sync with the
 * gosub_exec() argument format.
 */
static const char *expand_gosub_args(struct ast_channel *chan, const char *args)
{
	int len;
	char *parse;
	char *label;
	char *new_args;
	const char *context;
	const char *exten;
	const char *pri;

	/* Separate the context,exten,pri from the optional routine arguments. */
	parse = ast_strdupa(args);
	label = strsep(&parse, "(");
	if (parse) {
		char *endparen;

		endparen = strrchr(parse, ')');
		if (endparen) {
			*endparen = '\0';
		} else {
			ast_log(LOG_WARNING, "Ouch.  No closing paren: '%s'?\n", args);
		}
	}

	/* Split context,exten,pri */
	context = strsep(&label, ",");
	exten = strsep(&label, ",");
	pri = strsep(&label, ",");
	if (!exten) {
		/* Only a priority in this one */
		pri = context;
		exten = NULL;
		context = NULL;
	} else if (!pri) {
		/* Only an extension and priority in this one */
		pri = exten;
		exten = context;
		context = NULL;
	}

	ast_channel_lock(chan);
	if (ast_strlen_zero(exten)) {
		exten = ast_channel_exten(chan);
	}
	if (ast_strlen_zero(context)) {
		context = ast_channel_context(chan);
	}
	len = strlen(context) + strlen(exten) + strlen(pri) + 3;
	if (!ast_strlen_zero(parse)) {
		len += 2 + strlen(parse);
	}
	new_args = ast_malloc(len);
	if (new_args) {
		if (ast_strlen_zero(parse)) {
			snprintf(new_args, len, "%s,%s,%s", context, exten, pri);
		} else {
			snprintf(new_args, len, "%s,%s,%s(%s)", context, exten, pri, parse);
		}
	}
	ast_channel_unlock(chan);

	ast_debug(4, "Gosub args:%s new_args:%s\n", args, new_args ? new_args : "");

	return new_args;
}

static int gosub_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *stack_store;
	struct gosub_stack_list *oldlist;
	struct gosub_stack_frame *newframe;
	struct gosub_stack_frame *lastframe;
	char argname[15];
	char *parse;
	char *label;
	char *caller_id;
	char *orig_context;
	char *orig_exten;
	char *dest_context;
	char *dest_exten;
	int orig_in_subroutine;
	int orig_priority;
	int dest_priority;
	int i;
	int max_argc = 0;
	AST_DECLARE_APP_ARGS(args2,
		AST_APP_ARG(argval)[100];
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: %s([[context,]exten,]priority[(arg1[,...][,argN])])\n", app_gosub, app_gosub);
		return -1;
	}

	/*
	 * Separate the arguments from the label
	 *
	 * NOTE:  You cannot use ast_app_separate_args for this, because
	 * '(' cannot be used as a delimiter.
	 */
	parse = ast_strdupa(data);
	label = strsep(&parse, "(");
	if (parse) {
		char *endparen;

		endparen = strrchr(parse, ')');
		if (endparen) {
			*endparen = '\0';
		} else {
			ast_log(LOG_WARNING, "Ouch.  No closing paren: '%s'?\n", data);
		}
		AST_STANDARD_RAW_ARGS(args2, parse);
	} else {
		args2.argc = 0;
	}

	ast_channel_lock(chan);
	orig_context = ast_strdupa(ast_channel_context(chan));
	orig_exten = ast_strdupa(ast_channel_exten(chan));
	orig_priority = ast_channel_priority(chan);
	orig_in_subroutine = ast_test_flag(ast_channel_flags(chan), AST_FLAG_SUBROUTINE_EXEC);
	ast_channel_unlock(chan);

	if (ast_parseable_goto(chan, label)) {
		ast_log(LOG_ERROR, "%s address is invalid: '%s'\n", app_gosub, data);
		goto error_exit;
	}

	ast_channel_lock(chan);
	dest_context = ast_strdupa(ast_channel_context(chan));
	dest_exten = ast_strdupa(ast_channel_exten(chan));
	dest_priority = ast_channel_priority(chan);
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP)) {
		++dest_priority;
	}
	caller_id = S_COR(ast_channel_caller(chan)->id.number.valid,
		ast_channel_caller(chan)->id.number.str, NULL);
	if (caller_id) {
		caller_id = ast_strdupa(caller_id);
	}
	ast_channel_unlock(chan);

	if (!ast_exists_extension(chan, dest_context, dest_exten, dest_priority, caller_id)) {
		ast_log(LOG_ERROR, "%s attempted to reach non-existent destination '%s,%s,%d' from '%s,%s,%d'",
			app_gosub, dest_context, dest_exten, dest_priority, orig_context, orig_exten, orig_priority);
		goto error_exit;
	}

	/* Now we know that we're going to a new location */

	ast_channel_lock(chan);

	/* Find stack datastore return list. */
	if (!(stack_store = ast_channel_datastore_find(chan, &stack_info, NULL))) {
		ast_debug(1, "Channel %s has no datastore, so we're allocating one.\n",
			ast_channel_name(chan));
		stack_store = ast_datastore_alloc(&stack_info, NULL);
		if (!stack_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  %s failed.\n",
				app_gosub);
			goto error_exit_locked;
		}

		oldlist = ast_calloc(1, sizeof(*oldlist));
		if (!oldlist) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  %s failed.\n",
				app_gosub);
			ast_datastore_free(stack_store);
			goto error_exit_locked;
		}
		AST_LIST_HEAD_INIT(oldlist);

		stack_store->data = oldlist;
		ast_channel_datastore_add(chan, stack_store);
	} else {
		oldlist = stack_store->data;
	}

	if ((lastframe = AST_LIST_FIRST(oldlist))) {
		max_argc = lastframe->arguments;
	}

	/* Mask out previous Gosub arguments in this invocation */
	if (args2.argc > max_argc) {
		max_argc = args2.argc;
	}

	/* Create the return address */
	newframe = gosub_allocate_frame(orig_context, orig_exten, orig_priority + 1, orig_in_subroutine, max_argc);
	if (!newframe) {
		goto error_exit_locked;
	}

	/* Set our arguments */
	for (i = 0; i < max_argc; i++) {
		snprintf(argname, sizeof(argname), "ARG%d", i + 1);
		frame_set_var(chan, newframe, argname, i < args2.argc ? args2.argval[i] : "");
		ast_debug(1, "Setting '%s' to '%s'\n", argname, i < args2.argc ? args2.argval[i] : "");
	}
	snprintf(argname, sizeof(argname), "%u", args2.argc);
	frame_set_var(chan, newframe, "ARGC", argname);

	ast_set_flag(ast_channel_flags(chan), AST_FLAG_SUBROUTINE_EXEC);

	/* And finally, save our return address */
	AST_LIST_LOCK(oldlist);
	AST_LIST_INSERT_HEAD(oldlist, newframe, entries);
	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);

	return 0;

error_exit:
	ast_channel_lock(chan);

error_exit_locked:
	/* Restore the original dialplan location. */
	ast_channel_context_set(chan, orig_context);
	ast_channel_exten_set(chan, orig_exten);
	ast_channel_priority_set(chan, orig_priority);
	ast_channel_unlock(chan);
	return -1;
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
	struct ast_datastore *stack_store;
	struct gosub_stack_list *oldlist;
	struct gosub_stack_frame *frame;
	struct ast_var_t *variables;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(stack_store = ast_channel_datastore_find(chan, &stack_info, NULL))) {
		ast_channel_unlock(chan);
		return -1;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	if (!(frame = AST_LIST_FIRST(oldlist))) {
		/* Not within a Gosub routine */
		AST_LIST_UNLOCK(oldlist);
		ast_channel_unlock(chan);
		return -1;
	}

	AST_LIST_TRAVERSE(&frame->varshead, variables, entries) {
		if (!strcmp(data, ast_var_name(variables))) {
			const char *tmp;
			tmp = pbx_builtin_getvar_helper(chan, data);
			ast_copy_string(buf, S_OR(tmp, ""), len);
			break;
		}
	}
	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);
	return 0;
}

static int local_write(struct ast_channel *chan, const char *cmd, char *var, const char *value)
{
	struct ast_datastore *stack_store;
	struct gosub_stack_list *oldlist;
	struct gosub_stack_frame *frame;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(stack_store = ast_channel_datastore_find(chan, &stack_info, NULL))) {
		ast_log(LOG_ERROR, "Tried to set LOCAL(%s), but we aren't within a Gosub routine\n", var);
		ast_channel_unlock(chan);
		return -1;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	frame = AST_LIST_FIRST(oldlist);

	if (frame) {
		frame_set_var(chan, frame, var, value);
	}

	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);

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

	if (ast_strlen_zero(args.n) || ast_strlen_zero(args.name)) {
		ast_log(LOG_ERROR, "LOCAL_PEEK requires parameters n and varname\n");
		return -1;
	}

	n = atoi(args.n);
	*buf = '\0';

	ast_channel_lock(chan);
	AST_LIST_TRAVERSE(ast_channel_varshead(chan), variables, entries) {
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

static int stackpeek_read(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **str, ssize_t len)
{
	struct ast_datastore *stack_store;
	struct gosub_stack_list *oldlist;
	struct gosub_stack_frame *frame;
	int n;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(n);
		AST_APP_ARG(which);
		AST_APP_ARG(suppress);
	);

	if (!chan) {
		ast_log(LOG_ERROR, "STACK_PEEK must be called on an active channel\n");
		return -1;
	}

	data = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.n) || ast_strlen_zero(args.which)) {
		ast_log(LOG_ERROR, "STACK_PEEK requires parameters n and which\n");
		return -1;
	}

	n = atoi(args.n);
	if (n <= 0) {
		ast_log(LOG_ERROR, "STACK_PEEK must be called with a positive peek value\n");
		return -1;
	}

	ast_channel_lock(chan);
	if (!(stack_store = ast_channel_datastore_find(chan, &stack_info, NULL))) {
		if (!ast_true(args.suppress)) {
			ast_log(LOG_ERROR, "STACK_PEEK called on a channel without a gosub stack\n");
		}
		ast_channel_unlock(chan);
		return -1;
	}

	oldlist = stack_store->data;

	AST_LIST_LOCK(oldlist);
	AST_LIST_TRAVERSE(oldlist, frame, entries) {
		if (--n == 0) {
			break;
		}
	}

	if (!frame) {
		/* Too deep */
		if (!ast_true(args.suppress)) {
			ast_log(LOG_ERROR, "Stack peek of '%s' is more stack frames than I have\n", args.n);
		}
		AST_LIST_UNLOCK(oldlist);
		ast_channel_unlock(chan);
		return -1;
	}

	args.which = ast_skip_blanks(args.which);

	switch (args.which[0]) {
	case 'l': /* label */
		ast_str_set(str, len, "%s,%s,%d", frame->context, frame->extension, frame->priority - 1);
		break;
	case 'c': /* context */
		ast_str_set(str, len, "%s", frame->context);
		break;
	case 'e': /* extension */
		ast_str_set(str, len, "%s", frame->extension);
		break;
	case 'p': /* priority */
		ast_str_set(str, len, "%d", frame->priority - 1);
		break;
	default:
		ast_log(LOG_ERROR, "Unknown argument '%s' to STACK_PEEK\n", args.which);
		break;
	}

	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);

	return 0;
}

static struct ast_custom_function stackpeek_function = {
	.name = "STACK_PEEK",
	.read2 = stackpeek_read,
};

/*!
 * \internal
 * \brief Pop stack frames until remove a special return location.
 * \since 11.0
 *
 * \param chan Channel to balance stack on.
 *
 * \note The channel is already locked when called.
 */
static void balance_stack(struct ast_channel *chan)
{
	struct ast_datastore *stack_store;
	struct gosub_stack_list *oldlist;
	struct gosub_stack_frame *oldframe;
	int found;

	stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
	if (!stack_store) {
		ast_log(LOG_WARNING, "No %s stack allocated.\n", app_gosub);
		return;
	}

	oldlist = stack_store->data;
	AST_LIST_LOCK(oldlist);
	do {
		oldframe = AST_LIST_REMOVE_HEAD(oldlist, entries);
		if (!oldframe) {
			break;
		}
		found = oldframe->is_special;
		gosub_release_frame(chan, oldframe);
	} while (!found);
	AST_LIST_UNLOCK(oldlist);
}

/*!
 * \internal
 * \brief Run a subroutine on a channel.
 * \since 11.0
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param chan Channel to execute subroutine on.
 * \param sub_args Gosub application argument string.
 * \param ignore_hangup TRUE if a hangup does not stop execution of the routine.
 *
 * \retval 0 success
 * \retval -1 on error
 */
static int gosub_run(struct ast_channel *chan, const char *sub_args, int ignore_hangup)
{
	const char *saved_context;
	const char *saved_exten;
	int saved_priority;
	int saved_hangup_flags;
	int saved_autoloopflag;
	int saved_in_subroutine;
	int res;

	ast_channel_lock(chan);

	ast_verb(3, "%s Internal %s(%s) start\n",
		ast_channel_name(chan), app_gosub, sub_args);

	/* Save non-hangup softhangup flags. */
	saved_hangup_flags = ast_channel_softhangup_internal_flag(chan)
		& AST_SOFTHANGUP_ASYNCGOTO;
	if (saved_hangup_flags) {
		ast_channel_clear_softhangup(chan, AST_SOFTHANGUP_ASYNCGOTO);
	}

	/* Save autoloop flag */
	saved_autoloopflag = ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);

	/* Save current dialplan location */
	saved_context = ast_strdupa(ast_channel_context(chan));
	saved_exten = ast_strdupa(ast_channel_exten(chan));
	saved_priority = ast_channel_priority(chan);

	/* Save whether or not we are in a subroutine */
	saved_in_subroutine = ast_test_flag(ast_channel_flags(chan), AST_FLAG_SUBROUTINE_EXEC);

	ast_debug(4, "%s Original location: %s,%s,%d\n", ast_channel_name(chan),
		saved_context, saved_exten, saved_priority);

	ast_channel_unlock(chan);
	res = gosub_exec(chan, sub_args);
	ast_debug(4, "%s exited with status %d\n", app_gosub, res);
	ast_channel_lock(chan);
	if (!res) {
		struct ast_datastore *stack_store;

		/* Mark the return location as special. */
		stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
		if (!stack_store) {
			/* Should never happen! */
			ast_log(LOG_ERROR, "No %s stack!\n", app_gosub);
			res = -1;
		} else {
			struct gosub_stack_list *oldlist;
			struct gosub_stack_frame *cur;

			oldlist = stack_store->data;
			cur = AST_LIST_FIRST(oldlist);
			cur->is_special = 1;
		}
	}
	if (!res) {
		int found = 0;	/* set if we find at least one match */

		/*
		 * Run gosub body autoloop.
		 *
		 * Note that this loop is inverted from the normal execution
		 * loop because we just executed the Gosub application as the
		 * first extension of the autoloop.
		 */
		do {
			/* Check for hangup. */
			if (ast_check_hangup(chan)) {
				if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
					ast_log(LOG_ERROR, "%s An async goto just messed up our execution location.\n",
						ast_channel_name(chan));
					break;
				}
				if (!ignore_hangup) {
					break;
				}
			}

			/* Next dialplan priority. */
			ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);

			ast_channel_unlock(chan);
			res = ast_spawn_extension(chan, ast_channel_context(chan),
				ast_channel_exten(chan), ast_channel_priority(chan),
				S_COR(ast_channel_caller(chan)->id.number.valid,
					ast_channel_caller(chan)->id.number.str, NULL),
				&found, 1);
			ast_channel_lock(chan);
		} while (!res);
		if (found && res) {
			/* Something bad happened, or a hangup has been requested. */
			ast_debug(1, "Spawn extension (%s,%s,%d) exited with %d on '%s'\n",
				ast_channel_context(chan), ast_channel_exten(chan),
				ast_channel_priority(chan), res, ast_channel_name(chan));
			ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n",
				ast_channel_context(chan), ast_channel_exten(chan),
				ast_channel_priority(chan), ast_channel_name(chan));
		}

		/* Did the routine return? */
		if (ast_channel_priority(chan) == saved_priority
			&& !strcmp(ast_channel_context(chan), saved_context)
			&& !strcmp(ast_channel_exten(chan), saved_exten)) {
			ast_verb(3, "%s Internal %s(%s) complete GOSUB_RETVAL=%s\n",
				ast_channel_name(chan), app_gosub, sub_args,
				S_OR(pbx_builtin_getvar_helper(chan, "GOSUB_RETVAL"), ""));
		} else {
			ast_log(LOG_NOTICE, "%s Abnormal '%s(%s)' exit.  Popping routine return locations.\n",
				ast_channel_name(chan), app_gosub, sub_args);
			balance_stack(chan);
			pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", "");
		}

		/* We executed the requested subroutine to the best of our ability. */
		res = 0;
	}

	ast_debug(4, "%s Ending location: %s,%s,%d\n", ast_channel_name(chan),
		ast_channel_context(chan), ast_channel_exten(chan),
		ast_channel_priority(chan));

	/* Restore dialplan location */
	if (!(ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO)) {
		ast_channel_context_set(chan, saved_context);
		ast_channel_exten_set(chan, saved_exten);
		ast_channel_priority_set(chan, saved_priority);
	}

	/* Restore autoloop flag */
	ast_set2_flag(ast_channel_flags(chan), saved_autoloopflag, AST_FLAG_IN_AUTOLOOP);

	/* Restore subroutine flag */
	ast_set2_flag(ast_channel_flags(chan), saved_in_subroutine, AST_FLAG_SUBROUTINE_EXEC);

	/* Restore non-hangup softhangup flags. */
	if (saved_hangup_flags) {
		ast_softhangup_nolock(chan, saved_hangup_flags);
	}

	ast_channel_unlock(chan);

	return res;
}

static int handle_gosub(struct ast_channel *chan, AGI *agi, int argc, const char * const *argv)
{
	int res;
	int priority;
	int old_autoloopflag;
	int old_in_subroutine;
	int old_priority;
	const char *old_context;
	const char *old_extension;
	char *gosub_args;

	if (argc < 4 || argc > 5) {
		return RESULT_SHOWUSAGE;
	}

	ast_debug(1, "Gosub called with %d arguments: 0:%s 1:%s 2:%s 3:%s 4:%s\n", argc, argv[0], argv[1], argv[2], argv[3], argc == 5 ? argv[4] : "");

	if (sscanf(argv[3], "%30d", &priority) != 1 || priority < 1) {
		/* Lookup the priority label */
		priority = ast_findlabel_extension(chan, argv[1], argv[2], argv[3],
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL));
		if (priority < 0) {
			ast_log(LOG_ERROR, "Priority '%s' not found in '%s@%s'\n", argv[3], argv[2], argv[1]);
			ast_agi_send(agi->fd, chan, "200 result=-1 Gosub label not found\n");
			return RESULT_FAILURE;
		}
	} else if (!ast_exists_extension(chan, argv[1], argv[2], priority,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
		ast_agi_send(agi->fd, chan, "200 result=-1 Gosub label not found\n");
		return RESULT_FAILURE;
	}

	if (argc == 5) {
		if (ast_asprintf(&gosub_args, "%s,%s,%d(%s)", argv[1], argv[2], priority, argv[4]) < 0) {
			gosub_args = NULL;
		}
	} else {
		if (ast_asprintf(&gosub_args, "%s,%s,%d", argv[1], argv[2], priority) < 0) {
			gosub_args = NULL;
		}
	}
	if (!gosub_args) {
		ast_agi_send(agi->fd, chan, "503 result=-2 Memory allocation failure\n");
		return RESULT_FAILURE;
	}

	ast_channel_lock(chan);

	ast_verb(3, "%s AGI %s(%s) start\n", ast_channel_name(chan), app_gosub, gosub_args);

	/* Save autoloop flag */
	old_autoloopflag = ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP);

	/* Save subroutine flag */
	old_in_subroutine = ast_test_flag(ast_channel_flags(chan), AST_FLAG_SUBROUTINE_EXEC);

	/* Save previous location, since we're going to change it */
	old_context = ast_strdupa(ast_channel_context(chan));
	old_extension = ast_strdupa(ast_channel_exten(chan));
	old_priority = ast_channel_priority(chan);

	ast_debug(4, "%s Original location: %s,%s,%d\n", ast_channel_name(chan),
		old_context, old_extension, old_priority);
	ast_channel_unlock(chan);

	res = gosub_exec(chan, gosub_args);
	if (!res) {
		struct ast_datastore *stack_store;

		/* Mark the return location as special. */
		ast_channel_lock(chan);
		stack_store = ast_channel_datastore_find(chan, &stack_info, NULL);
		if (!stack_store) {
			/* Should never happen! */
			ast_log(LOG_ERROR, "No %s stack!\n", app_gosub);
			res = -1;
		} else {
			struct gosub_stack_list *oldlist;
			struct gosub_stack_frame *cur;

			oldlist = stack_store->data;
			cur = AST_LIST_FIRST(oldlist);
			cur->is_special = 1;
		}
		ast_channel_unlock(chan);
	}
	if (!res) {
		struct ast_pbx *pbx;
		struct ast_pbx_args args;
		int abnormal_exit;

		memset(&args, 0, sizeof(args));
		args.no_hangup_chan = 1;

		ast_channel_lock(chan);

		/* Next dialplan priority. */
		ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);

		/* Suppress warning about PBX already existing */
		pbx = ast_channel_pbx(chan);
		ast_channel_pbx_set(chan, NULL);
		ast_channel_unlock(chan);

		ast_agi_send(agi->fd, chan, "100 result=0 Trying...\n");
		ast_pbx_run_args(chan, &args);

		ast_channel_lock(chan);
		ast_free(ast_channel_pbx(chan));
		ast_channel_pbx_set(chan, pbx);

		/* Did the routine return? */
		if (ast_channel_priority(chan) == old_priority
			&& !strcmp(ast_channel_context(chan), old_context)
			&& !strcmp(ast_channel_exten(chan), old_extension)) {
			ast_verb(3, "%s AGI %s(%s) complete GOSUB_RETVAL=%s\n",
				ast_channel_name(chan), app_gosub, gosub_args,
				S_OR(pbx_builtin_getvar_helper(chan, "GOSUB_RETVAL"), ""));
			abnormal_exit = 0;
		} else {
			ast_log(LOG_NOTICE, "%s Abnormal AGI %s(%s) exit.  Popping routine return locations.\n",
				ast_channel_name(chan), app_gosub, gosub_args);
			balance_stack(chan);
			pbx_builtin_setvar_helper(chan, "GOSUB_RETVAL", "");
			abnormal_exit = 1;
		}
		ast_channel_unlock(chan);

		ast_agi_send(agi->fd, chan, "200 result=0 Gosub complete%s\n",
			abnormal_exit ? " (abnormal exit)" : "");
	} else {
		ast_agi_send(agi->fd, chan, "200 result=%d Gosub failed\n", res);
	}

	ast_free(gosub_args);

	ast_channel_lock(chan);
	ast_debug(4, "%s Ending location: %s,%s,%d\n", ast_channel_name(chan),
		ast_channel_context(chan), ast_channel_exten(chan),
		ast_channel_priority(chan));

	/* Restore previous location */
	ast_channel_context_set(chan, old_context);
	ast_channel_exten_set(chan, old_extension);
	ast_channel_priority_set(chan, old_priority);

	/* Restore autoloop flag */
	ast_set2_flag(ast_channel_flags(chan), old_autoloopflag, AST_FLAG_IN_AUTOLOOP);

	/* Restore subroutine flag */
	ast_set2_flag(ast_channel_flags(chan), old_in_subroutine, AST_FLAG_SUBROUTINE_EXEC);
	ast_channel_unlock(chan);

	return RESULT_SUCCESS;
}

static struct agi_command gosub_agi_command =
	{ { "gosub", NULL }, handle_gosub, NULL, NULL, 0 };

static int unload_module(void)
{
	ast_install_stack_functions(NULL);

	ast_agi_unregister(&gosub_agi_command);

	ast_unregister_application(app_return);
	ast_unregister_application(app_pop);
	ast_unregister_application(app_gosubif);
	ast_unregister_application(app_gosub);
	ast_custom_function_unregister(&local_function);
	ast_custom_function_unregister(&peek_function);
	ast_custom_function_unregister(&stackpeek_function);

	return 0;
}

static int load_module(void)
{
	/* Setup the stack application callback functions. */
	static struct ast_app_stack_funcs funcs = {
		.run_sub = gosub_run,
		.expand_sub_args = expand_gosub_args,
	};

	ast_agi_register(ast_module_info->self, &gosub_agi_command);

	ast_register_application_xml(app_pop, pop_exec);
	ast_register_application_xml(app_return, return_exec);
	ast_register_application_xml(app_gosubif, gosubif_exec);
	ast_register_application_xml(app_gosub, gosub_exec);
	ast_custom_function_register(&local_function);
	ast_custom_function_register(&peek_function);
	ast_custom_function_register(&stackpeek_function);

	funcs.module = ast_module_info->self,
	ast_install_stack_functions(&funcs);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT | AST_MODFLAG_LOAD_ORDER, "Dialplan subroutines (Gosub, Return, etc)",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.optional_modules = "res_agi",
);
