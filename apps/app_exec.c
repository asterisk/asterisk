/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, Tilghman Lesher.  All rights reserved.
 * Portions copyright (c) 2006, Philipp Dunkel.
 *
 * Tilghman Lesher <app_exec__v002@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief Exec application
 *
 * \author Tilghman Lesher <app_exec__v002@the-tilghman.com>
 * \author Philipp Dunkel <philipp.dunkel@ebox.at>
 *
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
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

/* Maximum length of any variable */
#define MAXRESULT	1024

static char *tdesc = "Executes dialplan applications";

/*! Note
 *
 * The key difference between these two apps is exit status.  In a
 * nutshell, Exec tries to be transparent as possible, behaving
 * in exactly the same way as if the application it calls was
 * directly invoked from the dialplan.
 *
 * TryExec, on the other hand, provides a way to execute applications
 * and catch any possible fatal error without actually fatally
 * affecting the dialplan.
 */

static char *app_exec = "Exec";
static char *exec_synopsis = "Executes dialplan application";
static char *exec_descrip =
"Usage: Exec(appname(arguments))\n"
"  Allows an arbitrary application to be invoked even when not\n"
"hardcoded into the dialplan.  If the underlying application\n"
"terminates the dialplan, or if the application cannot be found,\n"
"Exec will terminate the dialplan.\n"
"  To invoke external applications, see the application System.\n"
"  If you would like to catch any error instead, see TryExec.\n";

static char *app_tryexec = "TryExec";
static char *tryexec_synopsis = "Executes dialplan application, always returning";
static char *tryexec_descrip =
"Usage: TryExec(appname(arguments))\n"
"  Allows an arbitrary application to be invoked even when not\n"
"hardcoded into the dialplan. To invoke external applications\n"
"see the application System.  Always returns to the dialplan.\n"
"The channel variable TRYSTATUS will be set to:\n"
"    SUCCESS   if the application returned zero\n"
"    FAILED    if the application returned non-zero\n"
"    NOAPP     if the application was not found or was not specified\n"
"    NOMEMORY  if there was not enough memory to execute.\n";

static char *app_execif = "ExecIf";
static char *execif_synopsis = "Executes dialplan application, conditionally";
static char *execif_descrip = 
"Usage:  ExecIF (<expr>|<app>|<data>)\n"
"If <expr> is true, execute and return the result of <app>(<data>).\n"
"If <expr> is true, but <app> is not found, then the application\n"
"will return a non-zero value.\n";

LOCAL_USER_DECL;

static int exec_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s, *appname, *endargs, args[MAXRESULT] = "";
	struct ast_app *app;

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data) {
		if ((s = ast_strdupa(data))) {
			appname = strsep(&s, "(");
			if (s) {
				endargs = strrchr(s, ')');
				if (endargs)
					*endargs = '\0';
				pbx_substitute_variables_helper(chan, s, args, MAXRESULT - 1);
			}
			if (appname) {
				app = pbx_findapp(appname);
				if (app) {
					res = pbx_exec(chan, app, args);
				} else {
					ast_log(LOG_WARNING, "Could not find application (%s)\n", appname);
					res = -1;
				}
			}
		} else
			res = -1;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int tryexec_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s, *appname, *endargs, args[MAXRESULT] = "";
	struct ast_app *app;

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data) {
		if ((s = ast_strdupa(data))) {
			appname = strsep(&s, "(");
			if (s) {
				endargs = strrchr(s, ')');
				if (endargs)
					*endargs = '\0';
				pbx_substitute_variables_helper(chan, s, args, MAXRESULT - 1);
			}
			if (appname) {
				app = pbx_findapp(appname);
				if (app) {
					res = pbx_exec(chan, app, args);
					pbx_builtin_setvar_helper(chan, "TRYSTATUS", res ? "FAILED" : "SUCCESS");
				} else {
					ast_log(LOG_WARNING, "Could not find application (%s)\n", appname);
					pbx_builtin_setvar_helper(chan, "TRYSTATUS", "NOAPP");
				}
			}
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
			pbx_builtin_setvar_helper(chan, "TRYSTATUS", "NOMEMORY");
		}
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int execif_exec(struct ast_channel *chan, void *data) {
	int res=0;
	struct localuser *u;
	char *myapp = NULL;
	char *mydata = NULL;
	char *expr = NULL;
	struct ast_app *app = NULL;

	LOCAL_USER_ADD(u);

	if (!(expr = ast_strdupa(data))) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if ((myapp = strchr(expr,'|'))) {
		*myapp = '\0';
		myapp++;
		if ((mydata = strchr(myapp,'|'))) {
			*mydata = '\0';
			mydata++;
		} else
			mydata = "";

		if (pbx_checkcondition(expr)) { 
			if ((app = pbx_findapp(myapp))) {
				res = pbx_exec(chan, app, mydata);
			} else {
				ast_log(LOG_WARNING, "Count not find application! (%s)\n", myapp);
				res = -1;
			}
		}
	} else {
		ast_log(LOG_ERROR,"Invalid Syntax.\n");
		res = -1;
	}
		
	LOCAL_USER_REMOVE(u);
	return res;
}

static int unload_module(void *mod)
{
	int res;

	res = ast_unregister_application(app_exec);
	res |= ast_unregister_application(app_tryexec);
	res |= ast_unregister_application(app_execif);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

static int load_module(void *mod)
{
	int res = ast_register_application(app_exec, exec_exec, exec_synopsis, exec_descrip);
	res |= ast_register_application(app_tryexec, tryexec_exec, tryexec_synopsis, tryexec_descrip);
	res |= ast_register_application(app_execif, execif_exec, execif_synopsis, execif_descrip);
	return res;
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD1;
