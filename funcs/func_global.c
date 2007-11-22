/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Tilghman Lesher
 *
 * Tilghman Lesher <func_global__200605@the-tilghman.com>
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
 * \brief Global variable dialplan functions
 *
 * \author Tilghman Lesher <func_global__200605@the-tilghman.com>
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/pbx.h"

static int global_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	const char *var = pbx_builtin_getvar_helper(NULL, data);

	*buf = '\0';

	if (var)
		ast_copy_string(buf, var, len);

	return 0;
}

static int global_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	pbx_builtin_setvar_helper(NULL, data, value);

	return 0;
}

static struct ast_custom_function global_function = {
	.name = "GLOBAL",
	.synopsis = "Gets or sets the global variable specified",
	.syntax = "GLOBAL(<varname>)",
	.read = global_read,
	.write = global_write,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&global_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&global_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Global variable dialplan functions");
