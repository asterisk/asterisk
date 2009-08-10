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
 * \brief Generate Random Number
 * 
 * \author Claude Patry <cpatry@gmail.com>
 * \author Tilghman Lesher ( http://asterisk.drunkcoder.com/ )
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int acf_rand_exec(struct ast_channel *chan, char *cmd,
			 char *parse, char *buffer, size_t buflen)
{
	struct ast_module_user *u;
	int min_int, response_int, max_int;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(min);
			     AST_APP_ARG(max);
	);

	u = ast_module_user_add(chan);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.min) || sscanf(args.min, "%30d", &min_int) != 1)
		min_int = 0;

	if (ast_strlen_zero(args.max) || sscanf(args.max, "%30d", &max_int) != 1)
		max_int = RAND_MAX;

	if (max_int < min_int) {
		int tmp = max_int;

		max_int = min_int;
		min_int = tmp;
		ast_log(LOG_DEBUG, "max<min\n");
	}

	response_int = min_int + (ast_random() % (max_int - min_int + 1));
	ast_log(LOG_DEBUG, "%d was the lucky number in range [%d,%d]\n",
		response_int, min_int, max_int);
	snprintf(buffer, buflen, "%d", response_int);

	ast_module_user_remove(u);

	return 0;
}

static struct ast_custom_function acf_rand = {
	.name = "RAND",
	.synopsis = "Choose a random number in a range",
	.syntax = "RAND([min][|max])",
	.desc =
		"Choose a random number between min and max.  Min defaults to 0, if not\n"
		"specified, while max defaults to RAND_MAX (2147483647 on many systems).\n"
		"  Example:  Set(junky=${RAND(1|8)}); \n"
		"  Sets junky to a random number between 1 and 8, inclusive.\n",
	.read = acf_rand_exec,
};

static int unload_module(void)
{
	ast_custom_function_unregister(&acf_rand);

	return 0;
}

static int load_module(void)
{
	return ast_custom_function_register(&acf_rand);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Random number dialplan function");
