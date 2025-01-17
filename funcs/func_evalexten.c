/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, 2024, Naveen Albert
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
 * \brief Dialplan extension evaluation function
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="EVAL_EXTEN" language="en_US">
		<since><version>16.26.0</version><version>18.12.0</version><version>19.4.0</version></since>
		<synopsis>
			Evaluates the contents of a dialplan extension and returns it as a string.
		</synopsis>
		<syntax>
			<parameter name="context" />
			<parameter name="extensions" />
			<parameter name="priority" required="true" />
		</syntax>
		<description>
			<para>The EVAL_EXTEN function looks up a dialplan entry by context,extension,priority,
			evaluates the contents of a Return statement to resolve any variable or function
			references, and returns the result as a string.</para>
			<para>You can use this function to create simple user-defined lookup tables or
			user-defined functions.</para>
			<example title="Custom dialplan functions">
			[call-types]
			exten => _1NNN,1,Return(internal)
			exten => _NXXNXXXXXX,1,Return(external)

			[udf]
			exten => calleridlen,1,Return(${LEN(${CALLERID(num)})})

			[default]
			exten => _X!,1,Verbose(Call type ${EVAL_EXTEN(call-types,${EXTEN},1)} - ${EVAL_EXTEN(udf,calleridlen,1)})
			</example>
			<para>Any variables in the evaluated data will be resolved in the context of
			that extension. For example, <literal>${EXTEN}</literal> would refer to the
			EVAL_EXTEN extension, not the extension in the context invoking the function.
			This behavior is similar to other applications, e.g. <literal>Gosub</literal>.</para>
			<example title="Choosing which prompt to use">
			same => n,Read(input,${EVAL_EXTEN(prompts,${CALLERID(num)},1)})

			[prompts]
			exten => _X!,1,Return(default)
			exten => _20X,1,Return(welcome)
			exten => _2XX,1,Return(${DB(promptsettings/${EXTEN})})
			exten => _3XX,1,Return(${ODBC_MYFUNC(${EXTEN})})
			</example>
			<para>Extensions on which EVAL_EXTEN is invoked are not different from other
			extensions. However, the application at that extension is not executed.
			Only the application data is parsed and evaluated.</para>
			<para>A limitation of this function is that the application at the specified
			extension isn't actually executed, and thus unlike a Gosub, you can't pass
			arguments in the EVAL_EXTEN function.</para>
			<para>If you need the ability to evaluate more complex logic that cannot be done
			purely using functions, see <literal>EVAL_SUB</literal>.</para>
		</description>
		<see-also>
			<ref type="function">EVAL</ref>
			<ref type="function">EVAL_SUB</ref>
		</see-also>
	</function>
	<function name="EVAL_SUB" language="en_US">
		<since><version>20.11.0</version><version>21.6.0</version><version>22.1.0</version></since>
		<synopsis>
			Executes a Gosub and provides its return value as a string
		</synopsis>
		<syntax>
			<parameter name="context" />
			<parameter name="extensions" />
			<parameter name="priority" required="true" />
		</syntax>
		<description>
			<para>The EVAL_SUB function executes up a dialplan location by context,extension,priority, with optional arguments
			and returns the contents of the Return statement. The arguments to <literal>EVAL_SUB</literal>
			are exactly like they are with <literal>Gosub</literal>.</para>
			<para>This function is complementary to <literal>EVAL_EXTEN</literal>. However, it is more powerful,
			since it allows executing arbitrary dialplan and capturing some outcome as a dialplan function's
			return value, allowing it to be used in a variety of scenarios that do not allow executing dialplan
			directly but allow variables and functions to be used, and where using <literal>EVAL_EXTEN</literal>
			would be difficult or impossible.</para>
			<para>Consequently, this function also allows you to implement your own arbitrary functions
			in dialplan, which can then be wrapped using the Asterisk function interface using <literal>EVAL_SUB</literal>.</para>
			<para>While this function is primarily intended to be used for executing Gosub routines that are quick
			and do not interact with the channel, it is safe to execute arbitrary, even blocking, dialplan in the
			called subroutine. That said, this kind of usage is not recommended.</para>
			<para>This function will always return, even if the channel is hung up.</para>
			<example title="Record whether a PSTN call is local">
			[islocal]
			exten => _X!,1,ExecIf($[${LEN(${EXTEN})}&lt;10]?Return(1))
			same => n,Set(LOCAL(npanxx)=${EXTEN:-10:6})
			same => n,ReturnIf(${EXISTS(${DB(localcall/${npanxx})})}?${DB(localcall/${npanxx})})
			same => n,Set(LOCAL(islocal)=${SHELL(curl "https://example.com/islocal?npanxx=${EXTEN:-10:6}")})
			same => n,Set(LOCAL(islocal)=${FILTER(A-Z,${islocal})})
			same => n,Set(DB(localcall/${npanxx})=${islocal})
			same => n,Return(${islocal})

			[outgoing]
			exten => _1NXXNXXXXXX,1,Set(CDR(toll)=${IF($["${EVAL_SUB(islocal,${EXTEN},1)}"="Y"]?0:1)})
			same => n,Dial(DAHDI/1/${EXTEN})
			same => n,Hangup()
			</example>
			<para>This example illustrates an example of logic that would be difficult to capture
			in a way that a single call to <literal>EVAL_EXTEN</literal> would return the same result. For one, conditionals
			are involved, and due to the way Asterisk parses dialplan, all functions in an application call are evaluated all the
			time, which may be undesirable if they cause side effects (e.g. making a cURL request) that should only happen in certain circumstances.</para>
			<para>The above example, of course, does not require the use of this function, as it could have been invoked
			using the Gosub application directly. However, if constrained to just using variables or functions,
			<literal>EVAL_SUB</literal> would be required.</para>
		</description>
		<see-also>
			<ref type="function">EVAL_EXTEN</ref>
			<ref type="application">Return</ref>
		</see-also>
	</function>
 ***/

static int eval_exten_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *exten, *pri, *context, *parse;
	int ipri;
	char tmpbuf[len];

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "The EVAL_EXTEN function requires an extension\n");
		return -1;
	}

	parse = ast_strdupa(data);
	/* Split context,exten,pri */
	context = strsep(&parse, ",");
	exten = strsep(&parse, ",");
	pri = strsep(&parse, ",");

	if (pbx_parse_location(chan, &context, &exten, &pri, &ipri, NULL, NULL)) {
		return -1;
	}

	if (ast_strlen_zero(exten) || ast_strlen_zero(context)) { /* only lock if we really need to */
		ast_channel_lock(chan);
		if (ast_strlen_zero(exten)) {
			exten = ast_strdupa(ast_channel_exten(chan));
		}
		if (ast_strlen_zero(context)) {
			context = ast_strdupa(ast_channel_context(chan));
		}
		ast_channel_unlock(chan);
	}

	if (ast_get_extension_data(tmpbuf, len, chan, context, exten, ipri)) {
		return -1; /* EVAL_EXTEN failed */
	}

	pbx_substitute_variables_helper_full_location(chan, (chan) ? ast_channel_varshead(chan) : NULL, tmpbuf, buf, len, NULL, context, exten, ipri);

	return 0;
}

static int eval_sub_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int gosub_res;
	const char *retval;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "The EVAL_SUB function requires an extension\n");
		*buf = '\0';
		return -1;
	}

	/* Ignore hangups since we want to retrieve a value, and this function could be called at hangup time */
	gosub_res = ast_app_exec_sub(NULL, chan, data, 1);
	if (gosub_res) {
		ast_log(LOG_WARNING, "Failed to execute Gosub(%s)\n", data);
		*buf = '\0';
		return -1;
	}

	ast_channel_lock(chan);
	retval = pbx_builtin_getvar_helper(chan, "GOSUB_RETVAL");
	ast_copy_string(buf, S_OR(retval, ""), len); /* Overwrite, even if empty, to ensure a stale GOSUB_RETVAL isn't returned as our value */
	ast_channel_unlock(chan);

	return 0;
}

static struct ast_custom_function eval_exten_function = {
	.name = "EVAL_EXTEN",
	.read = eval_exten_read,
};

static struct ast_custom_function eval_sub_function = {
	.name = "EVAL_SUB",
	.read = eval_sub_read,
};

static int unload_module(void)
{
	int res = 0;
	res |= ast_custom_function_unregister(&eval_exten_function);
	res |= ast_custom_function_unregister(&eval_sub_function);
	return res;
}

static int load_module(void)
{
	int res = 0;
	res |= ast_custom_function_register(&eval_exten_function);
	res |= ast_custom_function_register(&eval_sub_function);
	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Extension evaluation function");
