/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 1999 - 2005, Digium, Inc.
*
* Mark Spencer <markster@digium.com>
* James Golovich <james@gnuinter.net>
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
 * \brief Check if Channel is Available
 *
 * \author Mark Spencer <markster@digium.com>
 * \author James Golovich <james@gnuinter.net>

 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <sys/ioctl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/devicestate.h"

static const char app[] = "ChanIsAvail";

/*** DOCUMENTATION
	<application name="ChanIsAvail" language="en_US">
		<synopsis>
			Check channel availability
		</synopsis>
		<syntax>
			<parameter name="Technology/Resource" required="false" argsep="&amp;">
				<argument name="Technology/Resource" required="true">
					<para>Specification of the device(s) to check.  These must be in the format of
					<literal>Technology/Resource</literal>, where <replaceable>Technology</replaceable>
					represents a particular channel driver, and <replaceable>Resource</replaceable>
					represents a resource available to that particular channel driver.</para>
				</argument>
				<argument name="Technology2/Resource2" multiple="true">
					<para>Optional extra devices to check</para>
					<para>If you need more than one enter them as
					Technology2/Resource2&amp;Technology3/Resource3&amp;.....</para>
				</argument>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="a">
						<para>Check for all available channels, not only the first one</para>
					</option>
					<option name="s">
						<para>Consider the channel unavailable if the channel is in use at all</para>
					</option>
					<option name="t" implies="s">
						<para>Simply checks if specified channels exist in the channel list</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application will check to see if any of the specified channels are available.</para>
			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="AVAILCHAN">
					<para>The name of the available channel, if one exists</para>
				</variable>
				<variable name="AVAILORIGCHAN">
					<para>The canonical channel name that was used to create the channel</para>
				</variable>
				<variable name="AVAILSTATUS">
					<para>The device state for the device</para>
				</variable>
				<variable name="AVAILCAUSECODE">
				        <para>The cause code returned when requesting the channel</para>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static int chanavail_exec(struct ast_channel *chan, const char *data)
{
	int inuse = -1;
	int option_state = 0;
	int string_compare = 0;
	int option_all_avail = 0;
	int status;
	char *info;
	char trychan[512];
	char *rest;
	char *tech;
	char *number;
	struct ast_str *tmp_availchan = ast_str_alloca(2048);
	struct ast_str *tmp_availorig = ast_str_alloca(2048);
	struct ast_str *tmp_availstat = ast_str_alloca(2048);
	struct ast_str *tmp_availcause = ast_str_alloca(2048);
	struct ast_channel *tempchan;
	struct ast_custom_function *cdr_prop_func = ast_custom_function_find("CDR_PROP");
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(reqchans);
		AST_APP_ARG(options);
	);

	info = ast_strdupa(data ?: "");

	AST_STANDARD_APP_ARGS(args, info);

	if (args.options) {
		if (strchr(args.options, 'a')) {
			option_all_avail = 1;
		}
		if (strchr(args.options, 's')) {
			option_state = 1;
		}
		if (strchr(args.options, 't')) {
			string_compare = 1;
		}
	}

	rest = args.reqchans;
	if (!rest) {
		rest = "";
	}
	while ((tech = strsep(&rest, "&"))) {
		tech = ast_strip(tech);

		number = strchr(tech, '/');
		if (!number) {
			if (!ast_strlen_zero(tech)) {
				ast_log(LOG_WARNING, "Invalid ChanIsAvail technology/resource argument: '%s'\n",
					tech);
			}

			ast_str_append(&tmp_availstat, 0, "%s%d",
				ast_str_strlen(tmp_availstat) ? "&" : "", AST_DEVICE_INVALID);
			continue;
		}
		*number++ = '\0';

		status = AST_DEVICE_UNKNOWN;

		if (string_compare) {
			/* ast_parse_device_state checks for "SIP/1234" as a channel name.
			   ast_device_state will ask the SIP driver for the channel state. */

			snprintf(trychan, sizeof(trychan), "%s/%s", tech, number);
			status = inuse = ast_parse_device_state(trychan);
		} else if (option_state) {
			/* If the pbx says in use then don't bother trying further.
			   This is to permit testing if someone's on a call, even if the
			   channel can permit more calls (ie callwaiting, sip calls, etc).  */

			snprintf(trychan, sizeof(trychan), "%s/%s", tech, number);
			status = inuse = ast_device_state(trychan);
		}
		ast_str_append(&tmp_availstat, 0, "%s%d",
			ast_str_strlen(tmp_availstat) ? "&" : "", status);
		if ((inuse <= (int) AST_DEVICE_NOT_INUSE)
			&& (tempchan = ast_request(tech, ast_channel_nativeformats(chan), NULL, chan, number, &status))) {
			ast_str_append(&tmp_availchan, 0, "%s%s",
				ast_str_strlen(tmp_availchan) ? "&" : "", ast_channel_name(tempchan));

			ast_str_append(&tmp_availorig, 0, "%s%s/%s",
				ast_str_strlen(tmp_availorig) ? "&" : "", tech, number);

			ast_str_append(&tmp_availcause, 0, "%s%d",
				ast_str_strlen(tmp_availcause) ? "&" : "", status);

			/* Disable CDR for this temporary channel. */
			if (cdr_prop_func) {
				ast_func_write(tempchan, "CDR_PROP(disable)", "1");
			}

			ast_hangup(tempchan);
			tempchan = NULL;

			if (!option_all_avail) {
				break;
			}
		}
	}

	pbx_builtin_setvar_helper(chan, "AVAILCHAN", ast_str_buffer(tmp_availchan));
	/* Store the originally used channel too */
	pbx_builtin_setvar_helper(chan, "AVAILORIGCHAN", ast_str_buffer(tmp_availorig));
	pbx_builtin_setvar_helper(chan, "AVAILSTATUS", ast_str_buffer(tmp_availstat));
	pbx_builtin_setvar_helper(chan, "AVAILCAUSECODE", ast_str_buffer(tmp_availcause));

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, chanavail_exec) ?
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Check channel availability",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.optional_modules = "func_cdr"
);
