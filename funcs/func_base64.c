/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Digium, Inc.
 * Copyright (C) 2005, Claude Patry
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
 * \brief Use the base64 as functions
 * 
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/pbx.h"	/* function register/unregister */
#include "asterisk/utils.h"

/*** DOCUMENTATION
	<function name="BASE64_ENCODE" language="en_US">
		<synopsis>
			Encode a string in base64.
		</synopsis>
		<syntax>
			<parameter name="string" required="true">
				<para>Input string</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns the base64 string.</para>
		</description>
		<see-also>
			<ref type="application">BASE64_DECODE</ref>
			<ref type="application">AES_DECRYPT</ref>
			<ref type="application">AES_ENCRYPT</ref>
		</see-also>
	</function>
	<function name="BASE64_DECODE" language="en_US">
		<synopsis>
			Decode a base64 string.
		</synopsis>
		<syntax>
			<parameter name="string" required="true">
				<para>Input string.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns the plain text string.</para>
		</description>
		<see-also>
			<ref type="application">BASE64_ENCODE</ref>
			<ref type="application">AES_DECRYPT</ref>
			<ref type="application">AES_ENCRYPT</ref>
		</see-also>
	</function>
 ***/

static int base64_encode(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: BASE64_ENCODE(<data>) - missing argument!\n");
		return -1;
	}

	ast_base64encode(buf, (unsigned char *) data, strlen(data), len);

	return 0;
}

static int base64_decode(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, size_t len)
{
	int decoded_len;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: BASE64_DECODE(<base_64 string>) - missing argument!\n");
		return -1;
	}

	decoded_len = ast_base64decode((unsigned char *) buf, data, len);
	if (decoded_len <= (len - 1)) {		/* if not truncated, */
		buf[decoded_len] = '\0';
	} else {
		buf[len - 1] = '\0';
	}

	return 0;
}

static struct ast_custom_function base64_encode_function = {
	.name = "BASE64_ENCODE",
	.read = base64_encode,
};

static struct ast_custom_function base64_decode_function = {
	.name = "BASE64_DECODE",
	.read = base64_decode,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&base64_encode_function) |
		ast_custom_function_unregister(&base64_decode_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&base64_encode_function) |
		ast_custom_function_register(&base64_decode_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "base64 encode/decode dialplan functions");
