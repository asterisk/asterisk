/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief Call Completion Supplementary Services implementation
 * \author Mark Michelson <mmichelson@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/ccss.h"
#include "asterisk/pbx.h"

/*** DOCUMENTATION
	<function name="CALLCOMPLETION" language="en_US">
		<synopsis>
			Get or set a call completion configuration parameter for a channel.
		</synopsis>
		<syntax>
			<parameter name="option" required="true">
				<para>The allowable options are:</para>
				<enumlist>
					<enum name="cc_agent_policy" />
					<enum name="cc_monitor_policy" />
					<enum name="cc_offer_timer" />
					<enum name="ccnr_available_timer" />
					<enum name="ccbs_available_timer" />
					<enum name="cc_recall_timer" />
					<enum name="cc_max_agents" />
					<enum name="cc_max_monitors" />
					<enum name="cc_callback_macro" />
					<enum name="cc_agent_dialstring" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>The CALLCOMPLETION function can be used to get or set a call
			completion configuration parameter for a channel. Note that setting
			a configuration parameter will only change the parameter for the
			duration of the call.

			For more information see <filename>doc/AST.pdf</filename>.
			For more information on call completion parameters, see <filename>configs/ccss.conf.sample</filename>.</para>
		</description>
	</function>
 ***/

static int acf_cc_read(struct ast_channel *chan, const char *name, char *data,
		char *buf, size_t buf_len)
{
	struct ast_cc_config_params *cc_params;
	int res;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", name);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(cc_params = ast_channel_get_cc_config_params(chan))) {
		ast_channel_unlock(chan);
		return -1;
	}

	res = ast_cc_get_param(cc_params, data, buf, buf_len);
	ast_channel_unlock(chan);
	return res;
}

static int acf_cc_write(struct ast_channel *chan, const char *cmd, char *data,
		const char *value)
{
	struct ast_cc_config_params *cc_params;
	int res;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(cc_params = ast_channel_get_cc_config_params(chan))) {
		ast_channel_unlock(chan);
		return -1;
	}

	res = ast_cc_set_param(cc_params, data, value);
	ast_channel_unlock(chan);
	return res;
}

static struct ast_custom_function cc_function = {
	.name = "CALLCOMPLETION",
	.read = acf_cc_read,
	.write = acf_cc_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&cc_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&cc_function) == 0 ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Call Control Configuration Function");
