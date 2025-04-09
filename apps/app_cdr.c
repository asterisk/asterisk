/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Martin Pycko <martinp@digium.com>
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
 * \brief Applications connected with CDR engine
 *
 * \author Martin Pycko <martinp@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"

/*** DOCUMENTATION
	<application name="ResetCDR" language="en_US">
		<since>
			<version>12.0.0</version>
		</since>
		<synopsis>
			Resets the Call Data Record.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="v">
						<para>Save the CDR variables during the reset.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application causes the Call Data Record to be reset.
			Depending on the flags passed in, this can have several effects.
			With no options, a reset does the following:</para>
			<para>1. The <literal>start</literal> time is set to the current time.</para>
			<para>2. If the channel is answered, the <literal>answer</literal> time is set to the
			current time.</para>
			<para>3. All variables are wiped from the CDR. Note that this step
			can be prevented with the <literal>v</literal> option.</para>
		</description>
		<see-also>
			<ref type="application">ForkCDR</ref>
			<ref type="function">CDR_PROP</ref>
		</see-also>
	</application>
 ***/

static const char resetcdr_app[] = "ResetCDR";

enum reset_cdr_options {
	OPT_DISABLE_DISPATCH = (1 << 0),
	OPT_KEEP_VARS = (1 << 1),
	OPT_ENABLE = (1 << 2),
};

AST_APP_OPTIONS(resetcdr_opts, {
	AST_APP_OPTION('v', AST_CDR_FLAG_KEEP_VARS),
});

STASIS_MESSAGE_TYPE_DEFN_LOCAL(appcdr_message_type);

/*! \internal \brief Payload for the Stasis message sent to manipulate a CDR */
struct app_cdr_message_payload {
	/*! The name of the channel to be manipulated */
	const char *channel_name;
	/*! Reset the CDR */
	unsigned int reset:1;
	/*! If resetting the CDR, keep the variables */
	unsigned int keep_variables:1;
};

static void appcdr_callback(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct app_cdr_message_payload *payload;

	if (stasis_message_type(message) != appcdr_message_type()) {
		return;
	}

	payload = stasis_message_data(message);
	if (!payload) {
		return;
	}

	if (payload->reset) {
		if (ast_cdr_reset(payload->channel_name, payload->keep_variables)) {
			ast_log(AST_LOG_WARNING, "Failed to reset CDRs on channel %s\n", payload->channel_name);
		}
	}
}

static int publish_app_cdr_message(struct ast_channel *chan, struct app_cdr_message_payload *payload)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);

	if (!router) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: no message router\n",
			ast_channel_name(chan));
		return -1;
	}

	message = stasis_message_create(appcdr_message_type(), payload);
	if (!message) {
		ast_log(AST_LOG_WARNING, "Failed to manipulate CDR for channel %s: unable to create message\n",
			payload->channel_name);
		return -1;
	}
	stasis_message_router_publish_sync(router, message);

	return 0;
}

static int resetcdr_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct app_cdr_message_payload *, payload,
		ao2_alloc(sizeof(*payload), NULL), ao2_cleanup);
	char *args;
	struct ast_flags flags = { 0 };

	if (!payload) {
		return -1;
	}

	if (!ast_strlen_zero(data)) {
		args = ast_strdupa(data);
		ast_app_parse_options(resetcdr_opts, &flags, NULL, args);
	}

	payload->channel_name = ast_channel_name(chan);
	payload->reset = 1;

	if (ast_test_flag(&flags, AST_CDR_FLAG_KEEP_VARS)) {
		payload->keep_variables = 1;
	}

	return publish_app_cdr_message(chan, payload);
}

static int unload_module(void)
{
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);

	if (router) {
		stasis_message_router_remove(router, appcdr_message_type());
	}
	STASIS_MESSAGE_TYPE_CLEANUP(appcdr_message_type);
	ast_unregister_application(resetcdr_app);
	return 0;
}

static int load_module(void)
{
	RAII_VAR(struct stasis_message_router *, router, ast_cdr_message_router(), ao2_cleanup);
	int res = 0;

	if (!router) {
		return AST_MODULE_LOAD_DECLINE;
	}

	res |= STASIS_MESSAGE_TYPE_INIT(appcdr_message_type);
	res |= ast_register_application_xml(resetcdr_app, resetcdr_exec);
	res |= stasis_message_router_add(router, appcdr_message_type(), appcdr_callback, NULL);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Tell Asterisk to not maintain a CDR for the current call");
