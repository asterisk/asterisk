/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale anthmct@yahoo.com
 * Development of this app Sponsered/Funded  by TAAN Softworks Corp
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
 * \brief Fork CDR application
 *
 * \author Anthony Minessale anthmct@yahoo.com
 *
 * \note Development of this app Sponsored/Funded by TAAN Softworks Corp
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cdr.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

/*** DOCUMENTATION
	<application name="ForkCDR" language="en_US">
		<synopsis>
			Forks the Call Data Record.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Update the answer time on the NEW CDR just after it's been inited.
						The new CDR may have been answered already. The reset that forkcdr does
						will erase the answer time. This will bring it back, but the answer time
						will be a copy of the fork/start time. It will only do this if the initial
						cdr was indeed already answered.</para>
					</option>
					<option name="A">
						<para>Lock the original CDR against the answer time being updated. This
						will allow the disposition on the original CDR to remain the same.</para>
					</option>
					<option name="d">
						<para>Copy the disposition forward from the old cdr, after the init.</para>
					</option>
					<option name="D">
						<para>Clear the <literal>dstchannel</literal> on the new CDR after
						reset.</para>
					</option>
					<option name="e">
						<para>End the original CDR. Do this after all the neccessry data is copied
						from the original CDR to the new forked CDR.</para>
					</option>
					<option name="r">
						<para>Do <emphasis>NOT</emphasis> reset the new cdr.</para>
					</option>
					<option name="s(name=val)">
						<para>Set the CDR var <replaceable>name</replaceable> in the original CDR,
						with value <replaceable>val</replaceable>.</para>
					</option>
					<option name="T">
						<para>Mark the original CDR with a DONT_TOUCH flag. setvar, answer, and end
						cdr funcs will obey this flag; normally they don't honor the LOCKED flag
						set on the original CDR record.</para>
						<note><para>Using this flag may cause CDR's not to have their end times
						updated! It is suggested that if you specify this flag, you might wish
						to use the <literal>e</literal> flag as well!.</para></note>
					</option>
					<option name="v">
						<para>When the new CDR is forked, it gets a copy of the vars attached to
						the current CDR. The vars attached to the original CDR are removed unless
						this option is specified.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para> Causes the Call Data Record to fork an additional cdr record starting from the time
			of the fork call. This new cdr record will be linked to end of the list of cdr records attached
			to the channel.	The original CDR has a LOCKED flag set, which forces most cdr operations to skip
			it, except for the functions that set the answer and end times, which ignore the LOCKED flag. This
			allows all the cdr records in the channel to be 'ended' together when the channel is closed.</para>
			<para>The CDR() func (when setting CDR values) normally ignores the LOCKED flag also, but has options
			to vary its behavior. The 'T' option (described below), can override this behavior, but beware
			the risks.</para>
			<para>First, this app finds the last cdr record in the list, and makes a copy of it. This new copy
			will be the newly forked cdr record. Next, this new record is linked to the end of the cdr record list.
			Next, The new cdr record is RESET (unless you use an option to prevent this)</para>
			<para>This means that:</para>
			<para>   1. All flags are unset on the cdr record</para>
			<para>   2. the start, end, and answer times are all set to zero.</para>
			<para>   3. the billsec and duration fields are set to zero.</para>
			<para>   4. the start time is set to the current time.</para>
			<para>   5. the disposition is set to NULL.</para>
			<para>Next, unless you specified the <literal>v</literal> option, all variables will be removed from
			the original cdr record. Thus, the <literal>v</literal> option allows any CDR variables to be replicated
			to all new forked cdr records. Without the <literal>v</literal> option, the variables on the original
			are effectively moved to the new forked cdr record.</para>
			<para>Next, if the <literal>s</literal> option is set, the provided variable and value are set on the
			original cdr record.</para>
			<para>Next, if the <literal>a</literal> option is given, and the original cdr record has an answer time
			set, then the new forked cdr record will have its answer time set to its start time. If the old answer
			time were carried forward, the answer time would be earlier than the start time, giving strange
			duration and billsec times.</para>
			<para>If the <literal>d</literal> option was specified, the disposition is copied from
			the original cdr record to the new forked cdr. If the <literal>D</literal> option was specified,
			the destination channel field in the new forked CDR is erased. If the <literal>e</literal> option
			was specified, the 'end' time for the original cdr record is set to the current time. Future hang-up or
			ending events will not override this time stamp. If the <literal>A</literal> option is specified,
			the original cdr record will have it ANS_LOCKED flag set, which prevent future answer events from updating
			the original cdr record's disposition. Normally, an <literal>ANSWERED</literal> event would mark all cdr
			records in the chain as <literal>ANSWERED</literal>. If the <literal>T</literal> option is specified,
			the original cdr record will have its <literal>DONT_TOUCH</literal> flag set, which will force the
			cdr_answer, cdr_end, and cdr_setvar functions to leave that cdr record alone.</para>
			<para>And, last but not least, the original cdr record has its LOCKED flag set. Almost all internal
			CDR functions (except for the funcs that set the end, and answer times, and set a variable) will honor
			this flag and leave a LOCKED cdr record alone. This means that the newly created forked cdr record
			will be affected by events transpiring within Asterisk, with the previously noted exceptions.</para>
		</description>
		<see-also>
			<ref type="function">CDR</ref>
			<ref type="application">NoCDR</ref>
			<ref type="application">ResetCDR</ref>
		</see-also>
	</application>
 ***/

static char *app = "ForkCDR";

enum {
	OPT_SETANS =            (1 << 0),
	OPT_SETDISP =           (1 << 1),
	OPT_RESETDEST =         (1 << 2),
	OPT_ENDCDR =            (1 << 3),
	OPT_NORESET =           (1 << 4),
	OPT_KEEPVARS =          (1 << 5),
	OPT_VARSET =            (1 << 6),
	OPT_ANSLOCK =           (1 << 7),
	OPT_DONTOUCH =          (1 << 8),
};

enum {
	OPT_ARG_VARSET = 0,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(forkcdr_exec_options, {
	AST_APP_OPTION('a', OPT_SETANS),
	AST_APP_OPTION('A', OPT_ANSLOCK),
	AST_APP_OPTION('d', OPT_SETDISP),
	AST_APP_OPTION('D', OPT_RESETDEST),
	AST_APP_OPTION('e', OPT_ENDCDR),
	AST_APP_OPTION('R', OPT_NORESET),
	AST_APP_OPTION_ARG('s', OPT_VARSET, OPT_ARG_VARSET),
	AST_APP_OPTION('T', OPT_DONTOUCH),
	AST_APP_OPTION('v', OPT_KEEPVARS),
});

static void ast_cdr_fork(struct ast_channel *chan, struct ast_flags optflags, char *set) 
{
	struct ast_cdr *cdr;
	struct ast_cdr *newcdr;
	struct ast_flags flags = { AST_CDR_FLAG_KEEP_VARS };

	cdr = chan->cdr;

	while (cdr->next)
		cdr = cdr->next;
	
	if (!(newcdr = ast_cdr_dup(cdr)))
		return;
	
	ast_cdr_append(cdr, newcdr);

	if (!ast_test_flag(&optflags, OPT_NORESET))
		ast_cdr_reset(newcdr, &flags);
		
	if (!ast_test_flag(cdr, AST_CDR_FLAG_KEEP_VARS))
		ast_cdr_free_vars(cdr, 0);
	
	if (!ast_strlen_zero(set)) {
		char *varname = ast_strdupa(set), *varval;
		varval = strchr(varname,'=');
		if (varval) {
			*varval = 0;
			varval++;
			ast_cdr_setvar(cdr, varname, varval, 0);
		}
	}
	
	if (ast_test_flag(&optflags, OPT_SETANS) && !ast_tvzero(cdr->answer))
		newcdr->answer = newcdr->start;

	if (ast_test_flag(&optflags, OPT_SETDISP))
		newcdr->disposition = cdr->disposition;
	
	if (ast_test_flag(&optflags, OPT_RESETDEST))
		newcdr->dstchannel[0] = 0;
	
	if (ast_test_flag(&optflags, OPT_ENDCDR))
		ast_cdr_end(cdr);

	if (ast_test_flag(&optflags, OPT_ANSLOCK))
		ast_set_flag(cdr, AST_CDR_FLAG_ANSLOCKED);
	
	if (ast_test_flag(&optflags, OPT_DONTOUCH))
		ast_set_flag(cdr, AST_CDR_FLAG_DONT_TOUCH);
		
	ast_set_flag(cdr, AST_CDR_FLAG_CHILD | AST_CDR_FLAG_LOCKED);
}

static int forkcdr_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *argcopy = NULL;
	struct ast_flags flags = {0};
	char *opts[OPT_ARG_ARRAY_SIZE];
	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(options);
	);

	if (!chan->cdr) {
		ast_log(LOG_WARNING, "Channel does not have a CDR\n");
		return 0;
	}

	argcopy = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(arglist, argcopy);

	opts[OPT_ARG_VARSET] = 0;

	if (!ast_strlen_zero(arglist.options))
		ast_app_parse_options(forkcdr_exec_options, &flags, opts, arglist.options);

	if (!ast_strlen_zero(data)) {
		int keepvars = ast_test_flag(&flags, OPT_KEEPVARS) ? 1 : 0;
		ast_set2_flag(chan->cdr, keepvars, AST_CDR_FLAG_KEEP_VARS);
	}
	
	ast_cdr_fork(chan, flags, opts[OPT_ARG_VARSET]);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, forkcdr_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Fork The CDR into 2 separate entities");
