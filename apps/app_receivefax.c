/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
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
 * \brief ReceiveFax application for res_fax
 *
 * \author\verbatim Dwayne M. Hubbard <dhubbard@digium.com> \endverbatim
 * 
 * This is a ReceiveFax application for use with res_fax.  This is probably
 * a throw away application, once res_fax works, it should probably be
 * merged with app_fax.c
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/res_fax.h"

static char *app = "ReceiveFax";
static char *synopsis = 
"ReceiveFax application for res_fax.";
static char *descrip = "This application is a ?temporary? ReceiveFax application.\n"
 " It uses the res_fax module.\n";

enum {
	OPTION_A = (1 << 0),
	OPTION_B = (1 << 1),
	OPTION_C = (1 << 2),
} option_flags;

enum {
	OPTION_ARG_B = 0,
	OPTION_ARG_C = 1,
	/* This *must* be the last value in this enum! */
	OPTION_ARG_ARRAY_SIZE = 2,
} option_args;

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('a', OPTION_A),
	AST_APP_OPTION_ARG('b', OPTION_B, OPTION_ARG_B),
	AST_APP_OPTION_ARG('c', OPTION_C, OPTION_ARG_C),
});


static int receivefax_exec(struct ast_channel *chan, void *data)
{
	int ms = 1000;
	int timeout = 5000; /* 5 seconds */
	int res = 0;
	struct ast_flags flags;
	struct ast_frame *frame = NULL;
	struct ast_fax_session *session;
	struct ast_fax_requirements *sessionreqs;
	char *parse, *opts[OPTION_ARG_ARRAY_SIZE];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dummy);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (filename[,options])\n", app);
		return -1;
	}

	/* set the fax requirements */	
	sessionreqs = ast_calloc(1, sizeof(*sessionreqs));
	if (!sessionreqs) {
		ast_log(LOG_ERROR, "system error\n");
		return -1;
	}
	sessionreqs->type = FAX_SESSION_MULAW;
	sessionreqs->operation = FAX_SESSION_OP_RECEIVE;

	/* obtain a fax resource that can handle said requirements */
	session = ast_fax_session_get(sessionreqs);
	if (!session) {
		ast_log(LOG_ERROR, "failed to retrieve a fax session that is capable of the session requirements.\n");
		ast_free(sessionreqs);
		return -1;
	}
	session->headerinfo = ast_strdup(pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO"));
	session->localstationid = ast_strdup(pbx_builtin_getvar_helper(chan, "LOCALSTATIONID"));
	session->start(session);

	/* if you feed it, it will fax */	
	while (ms > 0) {
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			/* bad stuff happened */
			res = ms;
			break;
		}
		if (!ms) {
			/* nothing happened */
			if (timeout > 0) {
				timeout -= 1000;
				ms = 1000;
				continue;
			} else {
				ast_log(LOG_WARNING, "channel '%s' timed-out waiting to receive the fax.\n", chan->name);
				break;
			}
		}
		if (!(frame = ast_read(chan))) {
			ast_log(LOG_DEBUG, "channel '%s' failed to read frames.\n", chan->name);
			res = -1;
			break;
		}
		ast_frfree(frame);
	}
	
	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc == 2)
		ast_app_parse_options(app_opts, &flags, opts, args.options);

	if (!ast_strlen_zero(args.dummy)) 
		ast_log(LOG_NOTICE, "Dummy value is : %s\n", args.dummy);

	if (ast_test_flag(&flags, OPTION_A))
		ast_log(LOG_NOTICE, "Option A is set\n");

	if (ast_test_flag(&flags, OPTION_B))
		ast_log(LOG_NOTICE, "Option B is set with : %s\n", opts[OPTION_ARG_B] ? opts[OPTION_ARG_B] : "<unspecified>");

	if (ast_test_flag(&flags, OPTION_C))
		ast_log(LOG_NOTICE, "Option C is set with : %s\n", opts[OPTION_ARG_C] ? opts[OPTION_ARG_C] : "<unspecified>");

	ast_free(sessionreqs);
	ast_fax_session_unreference(session);
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, receivefax_exec, synopsis, descrip) ? 
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ReceiveFax Application for res_fax");
