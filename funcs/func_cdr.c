/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2006, Digium, Inc.
 *
 * Portions Copyright (C) 2005, Anthony Minessale II
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
 * \brief  Call Detail Record related dialplan functions
 *
 * \author Anthony Minessale II
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/cdr.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"

/*** DOCUMENTATION
	<function name="CDR" language="en_US">
		<synopsis>
			Gets or sets a CDR variable.
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>CDR field name:</para>
				<enumlist>
					<enum name="clid">
						<para>Caller ID.</para>
					</enum>
					<enum name="lastdata">
						<para>Last application arguments.</para>
					</enum>
					<enum name="disposition">
						<para>The final state of the CDR.</para>
						<enumlist>
							<enum name="0">
								<para><literal>NO ANSWER</literal></para>
							</enum>
							<enum name="1">
								<para><literal>NO ANSWER</literal> (NULL record)</para>
							</enum>
							<enum name="2">
								<para><literal>FAILED</literal></para>
							</enum>
							<enum name="4">
								<para><literal>BUSY</literal></para>
							</enum>
							<enum name="8">
								<para><literal>ANSWERED</literal></para>
							</enum>
							<enum name="16">
								<para><literal>CONGESTION</literal></para>
							</enum>
						</enumlist>
					</enum>
					<enum name="src">
						<para>Source.</para>
					</enum>
					<enum name="start">
						<para>Time the call started.</para>
					</enum>
					<enum name="amaflags">
						<para>R/W the Automatic Message Accounting (AMA) flags on the channel.
						When read from a channel, the integer value will always be returned.
						When written to a channel, both the string format or integer value
						is accepted.</para>
						<enumlist>
							<enum name="1"><para><literal>OMIT</literal></para></enum>
							<enum name="2"><para><literal>BILLING</literal></para></enum>
							<enum name="3"><para><literal>DOCUMENTATION</literal></para></enum>
						</enumlist>
						<warning><para>Accessing this setting is deprecated in CDR. Please use the CHANNEL function instead.</para></warning>
					</enum>
					<enum name="dst">
						<para>Destination.</para>
					</enum>
					<enum name="answer">
						<para>Time the call was answered.</para>
					</enum>
					<enum name="accountcode">
						<para>The channel's account code.</para>
						<warning><para>Accessing this setting is deprecated in CDR. Please use the CHANNEL function instead.</para></warning>
					</enum>
					<enum name="dcontext">
						<para>Destination context.</para>
					</enum>
					<enum name="end">
						<para>Time the call ended.</para>
					</enum>
					<enum name="uniqueid">
						<para>The channel's unique id.</para>
					</enum>
					<enum name="dstchannel">
						<para>Destination channel.</para>
					</enum>
					<enum name="duration">
						<para>Duration of the call.</para>
					</enum>
					<enum name="userfield">
						<para>The channel's user specified field.</para>
					</enum>
					<enum name="lastapp">
						<para>Last application.</para>
					</enum>
					<enum name="billsec">
						<para>Duration of the call once it was answered.</para>
					</enum>
					<enum name="channel">
						<para>Channel name.</para>
					</enum>
					<enum name="sequence">
						<para>CDR sequence number.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="f">
						<para>Returns billsec or duration fields as floating point values.</para>
					</option>
					<option name="u">
						<para>Retrieves the raw, unprocessed value.</para>
						<para>For example, 'start', 'answer', and 'end' will be retrieved as epoch
						values, when the <literal>u</literal> option is passed, but formatted as YYYY-MM-DD HH:MM:SS
						otherwise.  Similarly, disposition and amaflags will return their raw
						integral values.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>All of the CDR field names are read-only, except for <literal>accountcode</literal>,
			<literal>userfield</literal>, and <literal>amaflags</literal>. You may, however, supply
			a name not on the above list, and create your own variable, whose value can be changed
			with this function, and this variable will be stored on the CDR.</para>
			<note><para>CDRs can only be modified before the bridge between two channels is
			torn down. For example, CDRs may not be modified after the <literal>Dial</literal>
			application has returned.</para></note>
			<para>Example: exten => 1,1,Set(CDR(userfield)=test)</para>
		</description>
	</function>
	<function name="CDR_PROP" language="en_US">
		<synopsis>
			Set a property on a channel's CDR.
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The property to set on the CDR.</para>
				<enumlist>
					<enum name="party_a">
						<para>Set this channel as the preferred Party A when
						channels are associated together.</para>
						<para>Write-Only</para>
					</enum>
					<enum name="disable">
						<para>Disable CDRs for this channel.</para>
						<para>Write-Only</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>This function sets a property on a channel's CDR. Properties
			alter the behavior of how the CDR operates for that channel.</para>
		</description>
	</function>
 ***/

enum cdr_option_flags {
	OPT_UNPARSED = (1 << 1),
	OPT_FLOAT = (1 << 2),
};

AST_APP_OPTIONS(cdr_func_options, {
	AST_APP_OPTION('f', OPT_FLOAT),
	AST_APP_OPTION('u', OPT_UNPARSED),
});

struct cdr_func_payload {
	struct ast_channel *chan;
	const char *cmd;
	const char *arguments;
	const char *value;
	void *data;
};

struct cdr_func_data {
	char *buf;
	size_t len;
};

STASIS_MESSAGE_TYPE_DEFN_LOCAL(cdr_read_message_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(cdr_write_message_type);
STASIS_MESSAGE_TYPE_DEFN_LOCAL(cdr_prop_write_message_type);

static void cdr_read_callback(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct cdr_func_payload *payload = stasis_message_data(message);
	struct cdr_func_data *output;
	char *info;
	char *value = NULL;
	struct ast_flags flags = { 0 };
	char tempbuf[512];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(variable);
		AST_APP_ARG(options);
	);

	if (cdr_read_message_type() != stasis_message_type(message)) {
		return;
	}

	ast_assert(payload != NULL);
	output = payload->data;
	ast_assert(output != NULL);

	if (ast_strlen_zero(payload->arguments)) {
		ast_log(AST_LOG_WARNING, "%s requires a variable (%s(variable[,option]))\n)",
			payload->cmd, payload->cmd);
		return;
	}
	info = ast_strdupa(payload->arguments);
	AST_STANDARD_APP_ARGS(args, info);

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(cdr_func_options, &flags, NULL, args.options);
	}

	if (ast_strlen_zero(ast_channel_name(payload->chan))) {
		/* Format request on a dummy channel */
		ast_cdr_format_var(ast_channel_cdr(payload->chan), args.variable, &value, tempbuf, sizeof(tempbuf), 0);
		if (ast_strlen_zero(value)) {
			return;
		}
		ast_copy_string(tempbuf, value, sizeof(tempbuf));
		ast_set_flag(&flags, OPT_UNPARSED);
	} else if (ast_cdr_getvar(ast_channel_name(payload->chan), args.variable, tempbuf, sizeof(tempbuf))) {
		return;
	}

	if (ast_test_flag(&flags, OPT_FLOAT)
		&& (!strcasecmp("billsec", args.variable) || !strcasecmp("duration", args.variable))) {
		long ms;
		double dtime;

		if (sscanf(tempbuf, "%30ld", &ms) != 1) {
			ast_log(AST_LOG_WARNING, "Unable to parse %s (%s) from the CDR for channel %s\n",
				args.variable, tempbuf, ast_channel_name(payload->chan));
			return;
		}
		dtime = (double)(ms / 1000.0);
		snprintf(tempbuf, sizeof(tempbuf), "%lf", dtime);
	} else if (!ast_test_flag(&flags, OPT_UNPARSED)) {
		if (!strcasecmp("start", args.variable)
			|| !strcasecmp("end", args.variable)
			|| !strcasecmp("answer", args.variable)) {
			struct timeval fmt_time;
			struct ast_tm tm;
			/* tv_usec is suseconds_t, which could be int or long */
			long int tv_usec;

			if (sscanf(tempbuf, "%ld.%ld", &fmt_time.tv_sec, &tv_usec) != 2) {
				ast_log(AST_LOG_WARNING, "Unable to parse %s (%s) from the CDR for channel %s\n",
					args.variable, tempbuf, ast_channel_name(payload->chan));
				return;
			}
			fmt_time.tv_usec = tv_usec;
			ast_localtime(&fmt_time, &tm, NULL);
			ast_strftime(tempbuf, sizeof(*tempbuf), "%Y-%m-%d %T", &tm);
		} else if (!strcasecmp("disposition", args.variable)) {
			int disposition;

			if (sscanf(tempbuf, "%8d", &disposition) != 1) {
				ast_log(AST_LOG_WARNING, "Unable to parse %s (%s) from the CDR for channel %s\n",
					args.variable, tempbuf, ast_channel_name(payload->chan));
				return;
			}
			snprintf(tempbuf, sizeof(tempbuf), "%s", ast_cdr_disp2str(disposition));
		} else if (!strcasecmp("amaflags", args.variable)) {
			int amaflags;

			if (sscanf(tempbuf, "%8d", &amaflags) != 1) {
				ast_log(AST_LOG_WARNING, "Unable to parse %s (%s) from the CDR for channel %s\n",
					args.variable, tempbuf, ast_channel_name(payload->chan));
				return;
			}
			snprintf(tempbuf, sizeof(tempbuf), "%s", ast_channel_amaflags2string(amaflags));
		}
	}

	ast_copy_string(output->buf, tempbuf, output->len);
}

static void cdr_write_callback(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct cdr_func_payload *payload = stasis_message_data(message);
	struct ast_flags flags = { 0 };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(variable);
		AST_APP_ARG(options);
	);
	char *parse;

	if (cdr_write_message_type() != stasis_message_type(message)) {
		return;
	}

	if (!payload) {
		return;
	}

	if (ast_strlen_zero(payload->arguments)) {
		ast_log(AST_LOG_WARNING, "%s requires a variable (%s(variable)=value)\n)",
			payload->cmd, payload->cmd);
		return;
	}
	if (ast_strlen_zero(payload->value)) {
		ast_log(AST_LOG_WARNING, "%s requires a value (%s(variable)=value)\n)",
			payload->cmd, payload->cmd);
		return;
	}
	parse = ast_strdupa(payload->arguments);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(cdr_func_options, &flags, NULL, args.options);
	}

	if (!strcasecmp(args.variable, "accountcode")) {
		ast_log(AST_LOG_WARNING, "Using the CDR function to set 'accountcode' is deprecated. Please use the CHANNEL function instead.\n");
		ast_channel_lock(payload->chan);
		ast_channel_accountcode_set(payload->chan, payload->value);
		ast_channel_unlock(payload->chan);
	} else if (!strcasecmp(args.variable, "peeraccount")) {
		ast_log(AST_LOG_WARNING, "The 'peeraccount' setting is not supported. Please set the 'accountcode' on the appropriate channel using the CHANNEL function.\n");
	} else if (!strcasecmp(args.variable, "userfield")) {
		ast_cdr_setuserfield(ast_channel_name(payload->chan), payload->value);
	} else if (!strcasecmp(args.variable, "amaflags")) {
		ast_log(AST_LOG_WARNING, "Using the CDR function to set 'amaflags' is deprecated. Please use the CHANNEL function instead.\n");
		if (isdigit(*payload->value)) {
			int amaflags;
			sscanf(payload->value, "%30d", &amaflags);
			ast_channel_lock(payload->chan);
			ast_channel_amaflags_set(payload->chan, amaflags);
			ast_channel_unlock(payload->chan);
		} else {
			ast_channel_lock(payload->chan);
			ast_channel_amaflags_set(payload->chan, ast_channel_string2amaflag(payload->value));
			ast_channel_unlock(payload->chan);
		}
	} else {
		ast_cdr_setvar(ast_channel_name(payload->chan), args.variable, payload->value);
	}
	return;
}

static void cdr_prop_write_callback(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct cdr_func_payload *payload = stasis_message_data(message);
	enum ast_cdr_options option;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(variable);
		AST_APP_ARG(options);
	);

	if (cdr_prop_write_message_type() != stasis_message_type(message)) {
		return;
	}

	if (!payload) {
		return;
	}

	if (ast_strlen_zero(payload->arguments)) {
		ast_log(AST_LOG_WARNING, "%s requires a variable (%s(variable)=value)\n)",
			payload->cmd, payload->cmd);
		return;
	}
	if (ast_strlen_zero(payload->value)) {
		ast_log(AST_LOG_WARNING, "%s requires a value (%s(variable)=value)\n)",
			payload->cmd, payload->cmd);
		return;
	}
	parse = ast_strdupa(payload->arguments);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!strcasecmp("party_a", args.variable)) {
		option = AST_CDR_FLAG_PARTY_A;
	} else if (!strcasecmp("disable", args.variable)) {
		option = AST_CDR_FLAG_DISABLE_ALL;
	} else {
		ast_log(AST_LOG_WARNING, "Unknown option %s used with %s\n", args.variable, payload->cmd);
		return;
	}

	if (ast_true(payload->value)) {
		ast_cdr_set_property(ast_channel_name(payload->chan), option);
	} else {
		ast_cdr_clear_property(ast_channel_name(payload->chan), option);
	}
}


static int cdr_read(struct ast_channel *chan, const char *cmd, char *parse,
		    char *buf, size_t len)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct cdr_func_payload *, payload,
		ao2_alloc(sizeof(*payload), NULL), ao2_cleanup);
	struct cdr_func_data output = { 0, };

	if (!payload) {
		return -1;
	}
	payload->chan = chan;
	payload->cmd = cmd;
	payload->arguments = parse;
	payload->data = &output;

	buf[0] = '\0';/* Ensure the buffer is initialized. */
	output.buf = buf;
	output.len = len;

	message = stasis_message_create(cdr_read_message_type(), payload);
	if (!message) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: unable to create message\n",
			ast_channel_name(chan));
		return -1;
	}

	/* If this is a request on a dummy channel, we're doing post-processing on an
	 * already dispatched CDR. Simply call the callback to calculate the value and
	 * return, instead of posting to Stasis as we would for a running channel.
	 */
	if (ast_strlen_zero(ast_channel_name(chan))) {
		cdr_read_callback(NULL, NULL, message);
	} else {
		RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);

		if (!router) {
			ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: no message router\n",
				ast_channel_name(chan));
			return -1;
		}
		stasis_message_router_publish_sync(router, message);
	}

	return 0;
}

static int cdr_write(struct ast_channel *chan, const char *cmd, char *parse,
		     const char *value)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct cdr_func_payload *, payload,
		     ao2_alloc(sizeof(*payload), NULL), ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, router,
		     ast_cdr_message_router(), ao2_cleanup);

	if (!router) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: no message router\n",
			ast_channel_name(chan));
		return -1;
	}

	if (!payload) {
		return -1;
	}
	payload->chan = chan;
	payload->cmd = cmd;
	payload->arguments = parse;
	payload->value = value;

	message = stasis_message_create(cdr_write_message_type(), payload);
	if (!message) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: unable to create message\n",
			ast_channel_name(chan));
		return -1;
	}
	stasis_message_router_publish_sync(router, message);

	return 0;
}

static int cdr_prop_write(struct ast_channel *chan, const char *cmd, char *parse,
		     const char *value)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct cdr_func_payload *, payload,
		ao2_alloc(sizeof(*payload), NULL), ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);

	if (!router) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: no message router\n",
			ast_channel_name(chan));
		return -1;
	}

	if (!payload) {
		return -1;
	}
	payload->chan = chan;
	payload->cmd = cmd;
	payload->arguments = parse;
	payload->value = value;

	message = stasis_message_create(cdr_prop_write_message_type(), payload);
	if (!message) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: unable to create message\n",
			ast_channel_name(chan));
		return -1;
	}
	stasis_message_router_publish_sync(router, message);

	return 0;
}

static struct ast_custom_function cdr_function = {
	.name = "CDR",
	.read = cdr_read,
	.write = cdr_write,
};

static struct ast_custom_function cdr_prop_function = {
	.name = "CDR_PROP",
	.read = NULL,
	.write = cdr_prop_write,
};

static int unload_module(void)
{
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);
	int res = 0;

	if (router) {
		stasis_message_router_remove(router, cdr_prop_write_message_type());
		stasis_message_router_remove(router, cdr_write_message_type());
		stasis_message_router_remove(router, cdr_read_message_type());
	}
	STASIS_MESSAGE_TYPE_CLEANUP(cdr_read_message_type);
	STASIS_MESSAGE_TYPE_CLEANUP(cdr_write_message_type);
	STASIS_MESSAGE_TYPE_CLEANUP(cdr_prop_write_message_type);
	res |= ast_custom_function_unregister(&cdr_function);
	res |= ast_custom_function_unregister(&cdr_prop_function);

	return res;
}

static int load_module(void)
{
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);
	int res = 0;

	if (!router) {
		return AST_MODULE_LOAD_FAILURE;
	}

	res |= STASIS_MESSAGE_TYPE_INIT(cdr_read_message_type);
	res |= STASIS_MESSAGE_TYPE_INIT(cdr_write_message_type);
	res |= STASIS_MESSAGE_TYPE_INIT(cdr_prop_write_message_type);
	res |= ast_custom_function_register(&cdr_function);
	res |= ast_custom_function_register(&cdr_prop_function);
	res |= stasis_message_router_add(router, cdr_prop_write_message_type(),
	                                 cdr_prop_write_callback, NULL);
	res |= stasis_message_router_add(router, cdr_write_message_type(),
	                                 cdr_write_callback, NULL);
	res |= stasis_message_router_add(router, cdr_read_message_type(),
	                                 cdr_read_callback, NULL);

	if (res) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Call Detail Record (CDR) dialplan functions");
