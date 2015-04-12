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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cdr.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"

/*** DOCUMENTATION
	<application name="ForkCDR" language="en_US">
		<synopsis>
			Forks the current Call Data Record for this channel.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>If the channel is answered, set the answer time on
						the forked CDR to the current time. If this option is
						not used, the answer time on the forked CDR will be the
						answer time on the original CDR. If the channel is not
						answered, this option has no effect.</para>
						<para>Note that this option is implicitly assumed if the
						<literal>r</literal> option is used.</para>
					</option>
					<option name="e">
						<para>End (finalize) the original CDR.</para>
					</option>
					<option name="r">
						<para>Reset the start and answer times on the forked CDR.
						This will set the start and answer times (if the channel
						is answered) to be set to the current time.</para>
						<para>Note that this option implicitly assumes the
						<literal>a</literal> option.</para>
					</option>
					<option name="v">
						<para>Do not copy CDR variables and attributes from the
						original CDR to the forked CDR.</para>
						<warning><para>This option has changed. Previously, the
						variables were removed from the original CDR. This no
						longer occurs - this option now controls whether or not
						a forked CDR inherits the variables from the original
						CDR.</para></warning>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Causes the Call Data Record engine to fork a new CDR starting
			from the time the application is executed. The forked CDR will be
			linked to the end of the CDRs associated with the channel.</para>
		</description>
		<see-also>
			<ref type="function">CDR</ref>
			<ref type="application">NoCDR</ref>
			<ref type="application">ResetCDR</ref>
		</see-also>
	</application>
 ***/

static char *app = "ForkCDR";

AST_APP_OPTIONS(forkcdr_exec_options, {
	AST_APP_OPTION('a', AST_CDR_FLAG_SET_ANSWER),
	AST_APP_OPTION('e', AST_CDR_FLAG_FINALIZE),
	AST_APP_OPTION('r', AST_CDR_FLAG_RESET),
	AST_APP_OPTION('v', AST_CDR_FLAG_KEEP_VARS),
});

STASIS_MESSAGE_TYPE_DEFN_LOCAL(forkcdr_message_type);

/*! \internal \brief Message payload for the Stasis message sent to fork the CDR */
struct fork_cdr_message_payload {
	/*! The name of the channel whose CDR will be forked */
	const char *channel_name;
	/*! Option flags that control how the CDR will be forked */
	struct ast_flags *flags;
};

static void forkcdr_callback(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct fork_cdr_message_payload *payload;

	if (stasis_message_type(message) != forkcdr_message_type()) {
		return;
	}

	payload = stasis_message_data(message);
	if (!payload) {
		return;
	}

	if (ast_cdr_fork(payload->channel_name, payload->flags)) {
		ast_log(AST_LOG_WARNING, "Failed to fork CDR for channel %s\n",
			payload->channel_name);
	}
}

static int forkcdr_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct fork_cdr_message_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);

	char *parse;
	struct ast_flags flags = { 0, };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
	);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(forkcdr_exec_options, &flags, NULL, args.options);
	}

	if (!forkcdr_message_type()) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: no message type\n",
			ast_channel_name(chan));
		return -1;
	}

	payload = ao2_alloc(sizeof(*payload), NULL);
	if (!payload) {
		return -1;
	}

	if (!router) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: no message router\n",
			ast_channel_name(chan));
		return -1;
	}

	payload->channel_name = ast_channel_name(chan);
	payload->flags = &flags;
	message = stasis_message_create(forkcdr_message_type(), payload);
	if (!message) {
		ast_log(AST_LOG_WARNING, "Failed to fork CDR for channel %s: unable to create message\n",
			ast_channel_name(chan));
		return -1;
	}
	stasis_message_router_publish_sync(router, message);

	return 0;
}

static int unload_module(void)
{
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);

	if (router) {
		stasis_message_router_remove(router, forkcdr_message_type());
	}
	STASIS_MESSAGE_TYPE_CLEANUP(forkcdr_message_type);
	ast_unregister_application(app);
	return 0;
}

static int load_module(void)
{
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);
	int res = 0;

	if (!router) {
		return AST_MODULE_LOAD_FAILURE;
	}

	res |= STASIS_MESSAGE_TYPE_INIT(forkcdr_message_type);
	res |= ast_register_application_xml(app, forkcdr_exec);
	res |= stasis_message_router_add(router, forkcdr_message_type(),
	                                 forkcdr_callback, NULL);

	if (res) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Fork The CDR into 2 separate entities");
