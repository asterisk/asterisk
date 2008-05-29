/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale anthmct@yahoo.com
 * Development of this app Sponsered/Funded  by TAAN Softworks Corp
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
 * \brief Fork CDR application
 *
 * \author Anthony Minessale anthmct@yahoo.com
 *
 * \note Development of this app Sponsored/Funded by TAAN Softworks Corp
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cdr.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

static char *app = "ForkCDR";
static char *synopsis = 
"Forks the Call Data Record";
static char *descrip = 
"  ForkCDR([options]):  Causes the Call Data Record to fork an additional\n"
"cdr record starting from the time of the fork call\n"
"  Options:\n"
"    a  - update the answer time on the NEW CDR just after it's been inited..\n"
"         The new CDR may have been answered already, the reset that forkcdr.\n"
"         does will erase the answer time. This will bring it back, but.\n"
"         the answer time will be a copy of the fork/start time. It will.\n"
"         only do this if the initial cdr was indeed already answered..\n"
"    D -  Copy the disposition forward from the old cdr, after the .\n"
"         init..\n"
"    d -  Clear the dstchannel on the new CDR after reset..\n"
"    e -  end the original CDR. Do this after all the necc. data.\n"
"         is copied from the original CDR to the new forked CDR..\n"
"    R -  do NOT reset the new cdr..\n"
"    s(name=val) - Set the CDR var 'name' in the original CDR, with value.\n"
"                  'val'.\n"
"    v  - When the new CDR is forked, it gets a copy of the vars attached\n"
"         to the current CDR. The vars attached to the original CDR are removed\n"
"         unless this option is specified.\n";


enum {
	OPT_SETANS =            (1 << 0),
	OPT_SETDISP =           (1 << 1),
	OPT_RESETDEST =         (1 << 2),
	OPT_ENDCDR =            (1 << 3),
	OPT_NORESET =           (1 << 4),
	OPT_KEEPVARS =          (1 << 5),
	OPT_VARSET =            (1 << 6),
};

enum {
	OPT_ARG_VARSET = 0,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(forkcdr_exec_options, {
	AST_APP_OPTION('a', OPT_SETANS),
	AST_APP_OPTION('d', OPT_SETDISP),
	AST_APP_OPTION('D', OPT_RESETDEST),
	AST_APP_OPTION('e', OPT_ENDCDR),
	AST_APP_OPTION('R', OPT_NORESET),
	AST_APP_OPTION_ARG('s', OPT_VARSET, OPT_ARG_VARSET),
	AST_APP_OPTION('v', OPT_KEEPVARS),
});

static void ast_cdr_fork(struct ast_channel *chan, struct ast_flags optflags, char *set) 
{
	struct ast_cdr *cdr;
	struct ast_cdr *newcdr;
	struct ast_flags flags = { AST_CDR_FLAG_KEEP_VARS };

	cdr = chan->cdr;

	while (cdr->next)
		cdr = cdr->next;
	
	if (!(newcdr = ast_cdr_dup(cdr)))
		return;
	
	ast_cdr_append(cdr, newcdr);

	if (!ast_test_flag(&optflags, OPT_NORESET))
		ast_cdr_reset(newcdr, &flags);
		
	if (!ast_test_flag(cdr, AST_CDR_FLAG_KEEP_VARS))
		ast_cdr_free_vars(cdr, 0);
	
	if (!ast_strlen_zero(set)) {
		char *varname = ast_strdupa(set), *varval;
		varval = strchr(varname,'=');
		if (varval) {
			*varval = 0;
			varval++;
			ast_cdr_setvar(cdr, varname, varval, 0);
		}
	}
	
	if (ast_test_flag(&optflags, OPT_SETANS) && !ast_tvzero(cdr->answer))
		newcdr->answer = newcdr->start;

	if (ast_test_flag(&optflags, OPT_SETDISP))
		newcdr->disposition = cdr->disposition;
	
	if (ast_test_flag(&optflags, OPT_RESETDEST))
		newcdr->dstchannel[0] = 0;
	
	if (ast_test_flag(&optflags, OPT_ENDCDR))
		ast_cdr_end(cdr);

	ast_set_flag(cdr, AST_CDR_FLAG_CHILD | AST_CDR_FLAG_LOCKED);
}

static int forkcdr_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	char *argcopy = NULL;
	struct ast_flags flags = {0};
	char *opts[OPT_ARG_ARRAY_SIZE];
	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(options);
	);

	if (!chan->cdr) {
		ast_log(LOG_WARNING, "Channel does not have a CDR\n");
		return 0;
	}

	u = ast_module_user_add(chan);

	argcopy = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(arglist, argcopy);

	if (!ast_strlen_zero(arglist.options)) {
		ast_app_parse_options(forkcdr_exec_options, &flags, opts, arglist.options);
	} else
		opts[OPT_ARG_VARSET] = 0;
	
	if (!ast_strlen_zero(data))
		ast_set2_flag(chan->cdr, ast_test_flag(&flags, OPT_KEEPVARS), AST_CDR_FLAG_KEEP_VARS);
	
	ast_cdr_fork(chan, flags, opts[OPT_ARG_VARSET]);

	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	return ast_register_application(app, forkcdr_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Fork The CDR into 2 separate entities");
