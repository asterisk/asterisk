/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming  <kpfleming@digium.com>
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
 * \brief Builtin dialplan functions
 * 
 */

#include <sys/types.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "pbx_functions.h"

static char *tdesc = "Builtin dialplan functions";

int unload_module(void)
{
	int x;

	for (x = 0; x < (sizeof(builtins) / sizeof(builtins[0])); x++) {
		ast_custom_function_unregister(builtins[x]);
	}

	return 0;
}

int load_module(void)
{
	int x;

	for (x = 0; x < (sizeof(builtins) / sizeof(builtins[0])); x++) {
		ast_custom_function_register(builtins[x]);
	}

	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
