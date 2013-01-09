/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Digium, Inc.
 * Copyright (C) 2005, Olle E. Johansson, Edvina.net
 * Copyright (C) 2005, Russell Bryant <russelb@clemson.edu> 
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
 * \brief MD5 digest related dialplan functions
 * 
 * \author Olle E. Johansson <oej@edvina.net>
 * \author Russell Bryant <russelb@clemson.edu>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/pbx.h"

/*** DOCUMENTATION
	<function name="MD5" language="en_US">
		<synopsis>
			Computes an MD5 digest.
		</synopsis>
		<syntax>
			<parameter name="data" required="true" />
		</syntax>
		<description>
			<para>Computes an MD5 digest.</para>
		</description>
	</function>
 ***/

static int md5(struct ast_channel *chan, const char *cmd, char *data,
	       char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: MD5(<data>) - missing argument!\n");
		return -1;
	}

	ast_md5_hash(buf, data);
	buf[32] = '\0';

	return 0;
}

static struct ast_custom_function md5_function = {
	.name = "MD5",
	.read = md5,
	.read_max = 33,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&md5_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&md5_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "MD5 digest dialplan functions");
