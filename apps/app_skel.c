/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Skeleton application
 * 
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<You Email Here>>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <asterisk/app.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static char *tdesc = "Trivial skeleton Application";
static char *app = "skel";
static char *synopsis = 
"  This is a skeleton application that shows you the basic structure to create your\n"
"own asterisk applications.\n";

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
	char *options=NULL;
	char *dummy = NULL;
	char *args;
	int argc = 0;
	char *opts[2];
	char *argv[2];

	if (!(args = ast_strdupa((char *)data))) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		return -1;
	}

	if (!data) {
		ast_log(LOG_WARNING, "%s requires an argument (dummy|[options])\n",app);
		return -1;
	}

	LOCAL_USER_ADD(u);
	if ((argc = ast_separate_app_args(args, '|', argv, sizeof(argv) / sizeof(char *)))) {
		dummy = argv[0];
		options = argv[1];
		ast_parseoptions(app_opts, &flags, opts, options);
	}


	if (dummy && !ast_strlen_zero(dummy)) 
		ast_log(LOG_NOTICE, "Dummy value is : %s\n", dummy);

	if (ast_test_flag(&flags, OPTION_A))
		ast_log(LOG_NOTICE, "Option A is set\n");

	if (ast_test_flag(&flags, OPTION_B))
		ast_log(LOG_NOTICE,"Option B is set with : %s\n", opts[0] ? opts[0] : "<unspecified>");

	if (ast_test_flag(&flags, OPTION_C))
		ast_log(LOG_NOTICE,"Option C is set with : %s\n", opts[1] ? opts[1] : "<unspecified>");

	/* Do our thing here */
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, app_exec, tdesc, synopsis);
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
