/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
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
 * \brief Return the current Version strings
 * 
 * \author Steve Murphy (murf@digium.com)
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
#include "asterisk/ast_version.h"
#include "asterisk/build.h"

/*** DOCUMENTATION
	<function name="VERSION" language="en_US">
		<synopsis>
			Return the Version info for this Asterisk.
		</synopsis>
		<syntax>
			<parameter name="info">
				<para>The possible values are:</para>
				<enumlist>
					<enum name="ASTERISK_VERSION_NUM">
						<para>A string of digits is returned (right now fixed at 999999).</para>
					</enum>
					<enum name="BUILD_USER">
						<para>The string representing the user's name whose account
						was used to configure Asterisk, is returned.</para>
					</enum>
					<enum name="BUILD_HOSTNAME">
						<para>The string representing the name of the host on which Asterisk was configured, is returned.</para>
					</enum>
					<enum name="BUILD_MACHINE">
						<para>The string representing the type of machine on which Asterisk was configured, is returned.</para>
					</enum>
					<enum name="BUILD_OS">
						<para>The string representing the OS of the machine on which Asterisk was configured, is returned.</para>
					</enum>
					<enum name="BUILD_DATE">
						<para>The string representing the date on which Asterisk was configured, is returned.</para>
					</enum>
					<enum name="BUILD_KERNEL">
						<para>The string representing the kernel version of the machine on which Asterisk
						was configured, is returned.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>If there are no arguments, return the version of Asterisk in this format: SVN-branch-1.4-r44830M</para>
			<para>Example:  Set(junky=${VERSION()};</para>
			<para>Sets junky to the string <literal>SVN-branch-1.6-r74830M</literal>, or possibly, <literal>SVN-trunk-r45126M</literal>.</para>
		</description>
	</function>
 ***/

static int acf_version_exec(struct ast_channel *chan, const char *cmd,
			 char *parse, char *buffer, size_t buflen)
{
	const char *response_char = ast_get_version();
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(info);
	);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.info) ) {
		if (!strcasecmp(args.info,"ASTERISK_VERSION_NUM"))
			response_char = ast_get_version_num();
		else if (!strcasecmp(args.info,"BUILD_USER"))
			response_char = BUILD_USER;
		else if (!strcasecmp(args.info,"BUILD_HOSTNAME"))
			response_char = BUILD_HOSTNAME;
		else if (!strcasecmp(args.info,"BUILD_MACHINE"))
			response_char = BUILD_MACHINE;
		else if (!strcasecmp(args.info,"BUILD_KERNEL"))
			response_char = BUILD_KERNEL;
		else if (!strcasecmp(args.info,"BUILD_OS"))
			response_char = BUILD_OS;
		else if (!strcasecmp(args.info,"BUILD_DATE"))
			response_char = BUILD_DATE;
	}

	ast_debug(1, "VERSION returns %s result, given %s argument\n", response_char, args.info);

	ast_copy_string(buffer, response_char, buflen);

	return 0;
}

static struct ast_custom_function acf_version = {
	.name = "VERSION",
	.read = acf_version_exec,
};

static int unload_module(void)
{
	ast_custom_function_unregister(&acf_version);

	return 0;
}

static int load_module(void)
{
	return ast_custom_function_register(&acf_version);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Get Asterisk Version/Build Info");
