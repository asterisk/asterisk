/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<Your Email Here>>
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
 * \brief Skeleton application
 * 
 * This is a skeleton for development of an Asterisk application 
 * \ingroup applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"

static char *tdesc = "Trivial skeleton Application";
static char *app = "Skel";
static char *synopsis = 
"Skeleton application.";
static char *descrip = "This application is a template to build other applications from.\n"
 " It shows you the basic structure to create your own Asterisk applications.\n";

#define OPTION_A	(1 << 0)	/* Option A */
#define OPTION_B	(1 << 1)	/* Option B(n) */
#define OPTION_C	(1 << 2)	/* Option C(str) */
#define OPTION_NULL	(1 << 3)	/* Dummy Termination */

AST_DECLARE_OPTIONS(app_opts,{
	['a'] = { OPTION_A },
	['b'] = { OPTION_B, 1 },
	['c'] = { OPTION_C, 2 }
});

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int app_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_flags flags;
	struct localuser *u;
	char *options = NULL;
	char *dummy = NULL;
	char *args;
	int argc = 0;
	char *opts[2];
	char *argv[2];

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (dummy|[options])\n",app);
		return -1;
	}

	LOCAL_USER_ADD(u);

	/* Do our thing here */

	/* We need to make a copy of the input string if we are going to modify it! */
	args = ast_strdupa(data);	
	if (!args) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	
	if ((argc = ast_app_separate_args(args, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		dummy = argv[0];
		options = argv[1];
		ast_parseoptions(app_opts, &flags, opts, options);
	}

	if (!ast_strlen_zero(dummy)) 
		ast_log(LOG_NOTICE, "Dummy value is : %s\n", dummy);

	if (ast_test_flag(&flags, OPTION_A))
		ast_log(LOG_NOTICE, "Option A is set\n");

	if (ast_test_flag(&flags, OPTION_B))
		ast_log(LOG_NOTICE,"Option B is set with : %s\n", opts[0] ? opts[0] : "<unspecified>");

	if (ast_test_flag(&flags, OPTION_C))
		ast_log(LOG_NOTICE,"Option C is set with : %s\n", opts[1] ? opts[1] : "<unspecified>");

	LOCAL_USER_REMOVE(u);

	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

int load_module(void)
{
	return ast_register_application(app, app_exec, synopsis, descrip);
}

int reload(void)
{
	/* This function will be called if a 'reload' is requested */

	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
