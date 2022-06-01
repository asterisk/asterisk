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
			Count the voicemails in a specified mailbox or mailboxes.
		</synopsis>
		<syntax>
			<parameter name="vmbox" required="true" argsep="&amp;">
				<para>A mailbox or list of mailboxes</para>
			</parameter>
			<parameter name="folder" required="false">
				<para>If not specified, defaults to <literal>INBOX</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>Count the number of voicemails in a specified mailbox, you could also specify
			the mailbox <replaceable>folder</replaceable>.</para>
			<example title="Mailbox folder count">
			exten => s,1,Set(foo=${VMCOUNT(125@default)})
			</example>
			<para>An ampersand-separated list of mailboxes may be specified to count voicemails in
			multiple mailboxes. If a folder is specified, this will apply to all mailboxes specified.</para>
                        <example title="Multiple mailbox inbox count">
                        same => n,NoOp(${VMCOUNT(1234@default&amp;1235@default&amp;1236@default,INBOX)})
                        </example>
		</description>
	</function>
 ***/

static int acf_vmcount_exec(struct ast_channel *chan, const char *cmd, char *argsstr, char *buf, size_t len)
{
	int total = 0;
	char *mailbox = NULL;
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

	while ((mailbox = strsep(&args.vmbox, "&"))) {
		int c;
		if (ast_strlen_zero(mailbox)) {
			continue;
		}
		c = ast_app_messagecount(mailbox, args.folder);
		total += (c > 0 ? c : 0);
	}
	snprintf(buf, len, "%d", total);

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
