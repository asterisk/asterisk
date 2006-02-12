/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2006, Digium, Inc.
 *
 * Portions Copyright (C) 2005, Anthony Minessale II
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
 * \brief  Call Detail Record related dialplan functions
 *
 * \author Anthony Minessale II 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/cdr.h"

enum {
	OPT_RECURSIVE = (1 << 0),
} cdr_option_flags;

AST_APP_OPTIONS(cdr_func_options, {
	AST_APP_OPTION('r', OPT_RECURSIVE),
});

static int cdr_read(struct ast_channel *chan, char *cmd, char *parse,
		    char *buf, size_t len)
{
	char *ret;
	struct ast_flags flags = { 0 };
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(variable);
			     AST_APP_ARG(options);
	);

	if (ast_strlen_zero(parse))
		return -1;

	if (!chan->cdr)
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(cdr_func_options, &flags, NULL, args.options);

	ast_cdr_getvar(chan->cdr, args.variable, &ret, buf, len,
		       ast_test_flag(&flags, OPT_RECURSIVE));

	return 0;
}

static int cdr_write(struct ast_channel *chan, char *cmd, char *parse,
		     const char *value)
{
	struct ast_flags flags = { 0 };
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(variable);
			     AST_APP_ARG(options);
	);

	if (ast_strlen_zero(parse) || !value)
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(cdr_func_options, &flags, NULL, args.options);

	if (!strcasecmp(args.variable, "accountcode"))
		ast_cdr_setaccount(chan, value);
	else if (!strcasecmp(args.variable, "userfield"))
		ast_cdr_setuserfield(chan, value);
	else if (chan->cdr)
		ast_cdr_setvar(chan->cdr, args.variable, value,
			       ast_test_flag(&flags, OPT_RECURSIVE));

	return 0;
}

static struct ast_custom_function cdr_function = {
	.name = "CDR",
	.synopsis = "Gets or sets a CDR variable",
	.desc = "Option 'r' searches the entire stack of CDRs on the channel\n",
	.syntax = "CDR(<name>[|options])",
	.read = cdr_read,
	.write = cdr_write,
};

static char *tdesc = "CDR dialplan function";

int unload_module(void)
{
	return ast_custom_function_unregister(&cdr_function);
}

int load_module(void)
{
	return ast_custom_function_register(&cdr_function);
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
