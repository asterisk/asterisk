/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 * Copyright (C) 2006, Claude Patry
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
 * \brief SHA1 digest related dialplan functions
 *
 * \author Claude Patry <cpatry@gmail.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/pbx.h"

/*** DOCUMENTATION
	<function name="SHA1" language="en_US">
		<synopsis>
			Computes a SHA1 digest.
		</synopsis>
		<syntax>
			<parameter name="data" required="true">
				<para>Input string</para>
			</parameter>
		</syntax>
		<description>
			<para>Generate a SHA1 digest via the SHA1 algorythm.</para>
			<example title="Set sha1hash variable to SHA1 hash of junky">
			exten => s,1,Set(sha1hash=${SHA1(junky)})
			</example>
			<para>The example above sets the asterisk variable sha1hash to the string <literal>60fa5675b9303eb62f99a9cd47f9f5837d18f9a0</literal>
			which is known as its hash</para>
		</description>
	</function>
 ***/

static int sha1(struct ast_channel *chan, const char *cmd, char *data,
		char *buf, size_t len)
{
	*buf = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: SHA1(<data>) - missing argument!\n");
		return -1;
	}

	if (len >= 41)
		ast_sha1_hash(buf, data);
	else {
		ast_log(LOG_ERROR,
				"Insufficient space to produce SHA1 hash result (%d < 41)\n",
				(int) len);
	}

	return 0;
}

static struct ast_custom_function sha1_function = {
	.name = "SHA1",
	.read = sha1,
	.read_max = 42,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&sha1_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&sha1_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SHA-1 computation dialplan function");
