/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief App to transmit a text message
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \note Requires support of sending text messages from channel driver
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
#include "asterisk/module.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="SendText" language="en_US">
		<synopsis>
			Send a Text Message.
		</synopsis>
		<syntax>
			<parameter name="text" required="true" />
		</syntax>
		<description>
			<para>Sends <replaceable>text</replaceable> to current channel (callee).</para>
			<para>Result of transmission will be stored in the <variable>SENDTEXTSTATUS</variable></para>
			<variablelist>
				<variable name="SENDTEXTSTATUS">
					<value name="SUCCESS">
						Transmission succeeded.
					</value>
					<value name="FAILURE">
						Transmission failed.
					</value>
					<value name="UNSUPPORTED">
						Text transmission not supported by channel.
					</value>
				</variable>
			</variablelist>
			<note><para>At this moment, text is supposed to be 7 bit ASCII in most channels.</para></note>
		</description>
		<see-also>
			<ref type="application">SendImage</ref>
			<ref type="application">SendURL</ref>
		</see-also>
	</application>
 ***/

static const char * const app = "SendText";

static int sendtext_exec(struct ast_channel *chan, const char *data)
{
	char *status = "UNSUPPORTED";
	struct ast_str *str;

	/* NOT ast_strlen_zero, because some protocols (e.g. SIP) MUST be able to
	 * send a zero-length message. */
	if (!data) {
		ast_log(LOG_WARNING, "SendText requires an argument (text)\n");
		return -1;
	}

	if (!(str = ast_str_alloca(strlen(data) + 1))) {
		return -1;
	}

	ast_str_get_encoded_str(&str, -1, data);

	ast_channel_lock(chan);
	if (!ast_channel_tech(chan)->send_text) {
		ast_channel_unlock(chan);
		/* Does not support transport */
		pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", status);
		return 0;
	}
	status = "FAILURE";
	if (!ast_sendtext(chan, ast_str_buffer(str))) {
		status = "SUCCESS";
	}
	ast_channel_unlock(chan);
	pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", status);
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, sendtext_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Send Text Applications");
