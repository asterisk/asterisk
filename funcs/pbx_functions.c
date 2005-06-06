/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Builtin dialplan functions
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION("$Revision$")

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
