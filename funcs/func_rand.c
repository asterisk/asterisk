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
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="RAND" language="en_US">
		<synopsis>
			Choose a random number in a range.			
		</synopsis>
		<syntax>
			<parameter name="min" />
			<parameter name="max" />
		</syntax>
		<description>
			<para>Choose a random number between <replaceable>min</replaceable> and <replaceable>max</replaceable>. 
			<replaceable>min</replaceable> defaults to <literal>0</literal>, if not specified, while <replaceable>max</replaceable> defaults 
			to <literal>RAND_MAX</literal> (2147483647 on many systems).</para>
			<para>Example:  Set(junky=${RAND(1,8)});
			Sets junky to a random number between 1 and 8, inclusive.</para>
		</description>
	</function>
 ***/
static int acf_rand_exec(struct ast_channel *chan, const char *cmd,
			 char *parse, char *buffer, size_t buflen)
{
	int min_int, response_int, max_int;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(min);
			     AST_APP_ARG(max);
	);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.min) || sscanf(args.min, "%30d", &min_int) != 1)
		min_int = 0;

	if (ast_strlen_zero(args.max) || sscanf(args.max, "%30d", &max_int) != 1)
		max_int = RAND_MAX;

	if (max_int < min_int) {
		int tmp = max_int;

		max_int = min_int;
		min_int = tmp;
		ast_debug(1, "max<min\n");
	}

	response_int = min_int + (ast_random() % (max_int - min_int + 1));
	ast_debug(1, "%d was the lucky number in range [%d,%d]\n", response_int, min_int, max_int);
	snprintf(buffer, buflen, "%d", response_int);

	return 0;
}

static struct ast_custom_function acf_rand = {
	.name = "RAND",
	.read = acf_rand_exec,
	.read_max = 12,
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
