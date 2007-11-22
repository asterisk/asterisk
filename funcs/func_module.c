/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Simple module check function
 * \author Olle E. Johansson, Edvina.net
 *
 * \ingroup functions
 */
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/pbx.h"

static int ifmodule_read(struct ast_channel *chan, const char *cmd, char *data,
		    char *buf, size_t len)
{
	char *ret = "0";

	*buf = '\0';

	if (data)
		if (ast_module_check(data))
			ret = "1";

	ast_copy_string(buf, ret, len);

	return 0;
}

static struct ast_custom_function ifmodule_function = {
	.name = "IFMODULE",
	.synopsis = "Checks if an Asterisk module is loaded in memory",
	.syntax = "IFMODULE(<modulename.so>)",
	.read = ifmodule_read,
	.desc = "Checks if a module is loaded. Use the full module name\n"
		"as shown by the list in \"module list\". \n"
		"Returns \"1\" if module exists in memory, otherwise \"0\".\n",
};


static int unload_module(void)
{
	return ast_custom_function_unregister(&ifmodule_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&ifmodule_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Checks if Asterisk module is loaded in memory");
