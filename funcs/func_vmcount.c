/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2006 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <asterisk-vmcount-func@the-tilghman.com>
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
 * \brief VMCOUNT dialplan function
 *
 * \author Tilghman Lesher <asterisk-vmcount-func@the-tilghman.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <dirent.h>

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="VMCOUNT" language="en_US">
		<synopsis>
			Count the voicemails in a specified mailbox.
		</synopsis>
		<syntax>
			<parameter name="vmbox" required="true" />
			<parameter name="folder" required="false">
				<para>If not specified, defaults to <literal>INBOX</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>Count the number of voicemails in a specified mailbox, you could also specify
			the mailbox <replaceable>folder</replaceable>.</para>
			<para>Example: <literal>exten => s,1,Set(foo=${VMCOUNT(125@default)})</literal></para>
		</description>
	</function>
 ***/

static int acf_vmcount_exec(struct ast_channel *chan, const char *cmd, char *argsstr, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vmbox);
		AST_APP_ARG(folder);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(argsstr))
		return -1;

	AST_STANDARD_APP_ARGS(args, argsstr);

	if (ast_strlen_zero(args.vmbox)) {
		return -1;
	}

	if (ast_strlen_zero(args.folder)) {
		args.folder = "INBOX";
	}

	snprintf(buf, len, "%d", ast_app_messagecount(args.vmbox, args.folder));

	return 0;
}

static struct ast_custom_function acf_vmcount = {
	.name = "VMCOUNT",
	.read = acf_vmcount_exec,
	.read_max = 12,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&acf_vmcount);
}

static int load_module(void)
{
	return ast_custom_function_register(&acf_vmcount);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Indicator for whether a voice mailbox has messages in a given folder.");
