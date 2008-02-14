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
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/cdr.h"

enum {
	OPT_RECURSIVE = (1 << 0),
	OPT_UNPARSED = (1 << 1),
	OPT_LAST = (1 << 2),
} cdr_option_flags;

AST_APP_OPTIONS(cdr_func_options, {
	AST_APP_OPTION('l', OPT_LAST),
	AST_APP_OPTION('r', OPT_RECURSIVE),
	AST_APP_OPTION('u', OPT_UNPARSED),
});

static int cdr_read(struct ast_channel *chan, const char *cmd, char *parse,
		    char *buf, size_t len)
{
	char *ret;
	struct ast_flags flags = { 0 };
	struct ast_cdr *cdr = chan ? chan->cdr : NULL;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(variable);
			     AST_APP_ARG(options);
	);

	if (ast_strlen_zero(parse))
		return -1;

	if (!cdr)
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(cdr_func_options, &flags, NULL, args.options);

	if (ast_test_flag(&flags, OPT_LAST))
		while (cdr->next)
			cdr = cdr->next;

	ast_cdr_getvar(cdr, args.variable, &ret, buf, len,
		       ast_test_flag(&flags, OPT_RECURSIVE),
			   ast_test_flag(&flags, OPT_UNPARSED));

	return 0;
}

static int cdr_write(struct ast_channel *chan, const char *cmd, char *parse,
		     const char *value)
{
	struct ast_flags flags = { 0 };
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(variable);
			     AST_APP_ARG(options);
	);

	if (ast_strlen_zero(parse) || !value || !chan)
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(cdr_func_options, &flags, NULL, args.options);

	if (!strcasecmp(args.variable, "accountcode"))
		ast_cdr_setaccount(chan, value);
	else if (!strcasecmp(args.variable, "userfield"))
		ast_cdr_setuserfield(chan, value);
	else if (!strcasecmp(args.variable, "amaflags"))
		ast_cdr_setamaflags(chan, value);
	else if (chan->cdr)
		ast_cdr_setvar(chan->cdr, args.variable, value, ast_test_flag(&flags, OPT_RECURSIVE));
		/* No need to worry about the u flag, as all fields for which setting
		 * 'u' would do anything are marked as readonly. */

	return 0;
}

static struct ast_custom_function cdr_function = {
	.name = "CDR",
	.synopsis = "Gets or sets a CDR variable",
	.syntax = "CDR(<name>[,options])",
	.read = cdr_read,
	.write = cdr_write,
	.desc =
"Options:\n"
"  'l' uses the most recent CDR on a channel with multiple records\n"
"  'r' searches the entire stack of CDRs on the channel\n"
"  'u' retrieves the raw, unprocessed value\n"
"  For example, 'start', 'answer', and 'end' will be retrieved as epoch\n"
"  values, when the 'u' option is passed, but formatted as YYYY-MM-DD HH:MM:SS\n"
"  otherwise.  Similarly, disposition and amaflags will return their raw\n"
"  integral values.\n"
"  Here is a list of all the available cdr field names:\n"
"    clid          lastdata       disposition\n"
"    src           start          amaflags\n"
"    dst           answer         accountcode\n"
"    dcontext      end            uniqueid\n"
"    dstchannel    duration       userfield\n"
"    lastapp       billsec        channel\n"
"  All of the above variables are read-only, except for accountcode,\n"
"  userfield, and amaflags. You may, however,  supply\n"
"  a name not on the above list, and create your own\n"
"  variable, whose value can be changed with this function,\n"
"  and this variable will be stored on the cdr.\n"
"   raw values for disposition:\n"
"       1 = NO ANSWER\n"
"       2 = BUSY\n"
"       3 = FAILED\n"
"       4 = ANSWERED\n"
"    raw values for amaflags:\n"
"       1 = OMIT\n"
"       2 = BILLING\n"
"       3 = DOCUMENTATION\n",
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&cdr_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&cdr_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Call Detail Record (CDR) dialplan function");
