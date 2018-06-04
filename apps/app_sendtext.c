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

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/message.h"

/*** DOCUMENTATION
	<application name="SendText" language="en_US">
		<synopsis>
			Send a Text Message on a channel.
		</synopsis>
		<syntax>
			<parameter name="text" required="false" />
		</syntax>
		<description>
			<para>Sends <replaceable>text</replaceable> to the current channel.</para>
			<note><para><literal>current channel</literal> could be the caller or callee depending
			on the context in which this application is called.</para></note>
			<para>
			</para>
			<para>The following variables can be set:</para>
			<variablelist>
				<variable name="SENDTEXT_FROM_DISPLAYNAME">
					<para>If set and this channel supports enhanced messaging, this value will be
					used as the <literal>From</literal> display name.</para>
				</variable>
				<variable name="SENDTEXT_TO_DISPLAYNAME">
					<para>If set and this channel supports enhanced messaging, this value will be
					used as the <literal>To</literal> display name.</para>
				</variable>
				<variable name="SENDTEXT_CONTENT_TYPE">
					<para>If set and this channel supports enhanced messaging, this value will be
					used as the message <literal>Content-Type</literal>.  If not specified, the
					default of <literal>text/plain</literal> will be used.</para>
					<para><emphasis>Warning:</emphasis> Messages of types other than
					<literal>text/&#42;</literal> cannot be sent via channel drivers that do not
					support Enhanced Messaging. An attempt to do so will be ignored and will result
					in the <literal>SENDTEXTSTATUS</literal> variable being set to
					<literal>UNSUPPORTED</literal>.</para>
				</variable>
				<variable name="SENDTEXT_BODY">
					<para>If set this value will be used as the message body and any text supplied
					as a function parameter will be ignored.
					</para>
				</variable>
			</variablelist>
			<para>
			</para>
			<para>Result of transmission will be stored in the following variables:</para>
			<variablelist>
				<variable name="SENDTEXTTYPE">
					<value name="NONE">
						No message sent.
					</value>
					<value name="BASIC">
						Message body sent without attributes because the channel driver
						doesn't support enhanced messaging.
					</value>
					<value name="ENHANCED">
						The message was sent using enhanced messaging.
					</value>
				</variable>
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
			<para>
			</para>
			<note><para>The text encoding and transmission method is completely at the
			discretion of the channel driver.  chan_pjsip will use in-dialog SIP MESSAGE
			messages always.  chan_sip will use T.140 via RTP if a text media type was
			negotiated and in-dialog SIP MESSAGE messages otherwise.</para></note>
			<para>
			</para>
			<para>Examples:
			</para>
			<example title="Send a simple message">
			 same => n,SendText(Your Text Here)
			</example>
			<para>If the channel driver supports enhanced messaging (currently only chan_pjsip),
			you can set additional variables:</para>
			<example title="Alter the From display name">
			 same => n,Set(SENDTEXT_FROM_DISPLAYNAME=Really From Bob)
			 same => n,SendText(Your Text Here)
			</example>
			<example title="Send a JSON String">
			 same => n,Set(SENDTEXT_CONTENT_TYPE=text/json)
			 same => n,SendText({"foo":a, "bar":23})
			</example>
			<example title="Send a JSON String (alternate)">
			 same => n,Set(SENDTEXT_CONTENT_TYPE=text/json)
			 same => n,Set(SENDTEXT_BODY={"foo":a, "bar":23})
			 same => n,SendText()
			</example>
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
	char *status;
	char *msg_type;
	struct ast_str *str;
	const char *from;
	const char *to;
	const char *content_type;
	const char *body;
	int rc = 0;

	ast_channel_lock(chan);
	from = pbx_builtin_getvar_helper(chan, "SENDTEXT_FROM_DISPLAYNAME");
	to = pbx_builtin_getvar_helper(chan, "SENDTEXT_TO_DISPLAYNAME");
	content_type = pbx_builtin_getvar_helper(chan, "SENDTEXT_CONTENT_TYPE");
	body = S_OR(pbx_builtin_getvar_helper(chan, "SENDTEXT_BODY"), data);
	body = S_OR(body, "");

	if (!(str = ast_str_alloca(strlen(body) + 1))) {
		rc = -1;
		goto cleanup;
	}
	ast_str_get_encoded_str(&str, -1, body);
	body = ast_str_buffer(str);

	msg_type = "NONE";
	status = "UNSUPPORTED";
	if (ast_channel_tech(chan)->send_text_data) {
		struct ast_msg_data *msg;
		struct ast_msg_data_attribute attrs[] =
		{
			{
				.type = AST_MSG_DATA_ATTR_FROM,
				.value = (char *)S_OR(from, ""),
			},
			{
				.type = AST_MSG_DATA_ATTR_TO,
				.value = (char *)S_OR(to, ""),
			},
			{
				.type = AST_MSG_DATA_ATTR_CONTENT_TYPE,
				.value = (char *)S_OR(content_type, ""),
			},
			{
				.type = AST_MSG_DATA_ATTR_BODY,
				.value = (char *)S_OR(body, ""),
			},
		};

		msg_type = "ENHANCED";
		msg = ast_msg_data_alloc(AST_MSG_DATA_SOURCE_TYPE_IN_DIALOG, attrs, ARRAY_LEN(attrs));
		if (msg) {
			if (ast_sendtext_data(chan, msg) == 0) {
				status = "SUCCESS";
			} else {
				status = "FAILURE";
			}

			ast_free(msg);
		} else {
			rc = -1;
			goto cleanup;
		}

	} else if (ast_channel_tech(chan)->send_text) {
		if (!ast_strlen_zero(content_type) && !ast_begins_with(content_type, "text/")) {
			rc = -1;
			goto cleanup;
		}

		msg_type = "BASIC";
		if (ast_sendtext(chan, body) == 0) {
			status = "SUCCESS";
		} else {
			status = "FAILURE";
		}
	}

	pbx_builtin_setvar_helper(chan, "SENDTEXTTYPE", msg_type);
	pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", status);

cleanup:
	pbx_builtin_setvar_helper(chan, "SENDTEXT_FROM_DISPLAYNAME", NULL);
	pbx_builtin_setvar_helper(chan, "SENDTEXT_TO_DISPLAYNAME", NULL);
	pbx_builtin_setvar_helper(chan, "SENDTEXT_CONTENT_TYPE", NULL);
	pbx_builtin_setvar_helper(chan, "SENDTEXT_BODY", NULL);
	ast_channel_unlock(chan);

	return rc;
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
