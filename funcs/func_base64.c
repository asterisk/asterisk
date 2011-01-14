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
#include "asterisk/strings.h"

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

static int base64_helper(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, struct ast_str **str, ssize_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: %s(<data>) - missing argument!\n", cmd);
		return -1;
	}

	if (cmd[7] == 'E') {
		if (buf) {
			ast_base64encode(buf, (unsigned char *) data, strlen(data), len);
		} else {
			if (len >= 0) {
				ast_str_make_space(str, len ? len : ast_str_strlen(*str) + strlen(data) * 4 / 3 + 2);
			}
			ast_base64encode(ast_str_buffer(*str) + ast_str_strlen(*str), (unsigned char *) data, strlen(data), ast_str_size(*str) - ast_str_strlen(*str));
			ast_str_update(*str);
		}
	} else {
		int decoded_len;
		if (buf) {
			decoded_len = ast_base64decode((unsigned char *) buf, data, len);
			/* add a terminating null at the end of buf, or at the
			 * end of our decoded string, which ever is less */
			buf[decoded_len <= (len - 1) ? decoded_len : len - 1] = '\0';
		} else {
			if (len >= 0) {
				ast_str_make_space(str, len ? len : ast_str_strlen(*str) + strlen(data) * 3 / 4 + 2);
			}
			decoded_len = ast_base64decode((unsigned char *) ast_str_buffer(*str) + ast_str_strlen(*str), data, ast_str_size(*str) - ast_str_strlen(*str));
			if (len)
				/* add a terminating null at the end of our
				 * buffer, or at the end of our decoded string,
				 * which ever is less */
				ast_str_buffer(*str)[decoded_len <= (len - 1) ? decoded_len : len - 1] = '\0';
			else
				/* space for the null is allocated above */
				ast_str_buffer(*str)[decoded_len] = '\0';

			ast_str_update(*str);
		}
	}

	return 0;
}

static int base64_buf_helper(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, size_t len)
{
	return base64_helper(chan, cmd, data, buf, NULL, len);
}

static int base64_str_helper(struct ast_channel *chan, const char *cmd, char *data,
			 struct ast_str **buf, ssize_t len)
{
	return base64_helper(chan, cmd, data, NULL, buf, len);
}

static struct ast_custom_function base64_encode_function = {
	.name = "BASE64_ENCODE",
	.read = base64_buf_helper,
	.read2 = base64_str_helper,
};

static struct ast_custom_function base64_decode_function = {
	.name = "BASE64_DECODE",
	.read = base64_buf_helper,
	.read2 = base64_str_helper,
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
