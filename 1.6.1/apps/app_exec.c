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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/* Maximum length of any variable */
#define MAXRESULT 1024

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
"  Exec(appname(arguments)):\n"
"Allows an arbitrary application to be invoked even when not\n"
"hardcoded into the dialplan.  If the underlying application\n"
"terminates the dialplan, or if the application cannot be found,\n"
"Exec will terminate the dialplan.\n"
"  To invoke external applications, see the application System.\n"
"  If you would like to catch any error instead, see TryExec.\n";

static char *app_tryexec = "TryExec";
static char *tryexec_synopsis = "Executes dialplan application, always returning";
static char *tryexec_descrip =
"  TryExec(appname(arguments)):\n"
"Allows an arbitrary application to be invoked even when not\n"
"hardcoded into the dialplan. To invoke external applications\n"
"see the application System.  Always returns to the dialplan.\n"
"The channel variable TRYSTATUS will be set to one of:\n"
"    SUCCESS   if the application returned zero\n"
"    FAILED    if the application returned non-zero\n"
"    NOAPP     if the application was not found or was not specified\n";

static char *app_execif = "ExecIf";
static char *execif_synopsis = "Executes dialplan application, conditionally";
static char *execif_descrip =
"  ExecIF (<expr>?<appiftrue>(<args>)[:<appiffalse>(<args>)])\n"
"If <expr> is true, execute and return the result of <appiftrue>(<args>).\n"
"If <expr> is true, but <appiftrue> is not found, then the application\n"
"will return a non-zero value.\n";

static int exec_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *s, *appname, *endargs, args[MAXRESULT];
	struct ast_app *app;

	if (ast_strlen_zero(data))
		return 0;

	s = ast_strdupa(data);
	args[0] = 0;
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

	return res;
}

static int tryexec_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *s, *appname, *endargs, args[MAXRESULT];
	struct ast_app *app;

	if (ast_strlen_zero(data))
		return 0;

	s = ast_strdupa(data);
	args[0] = 0;
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

	return 0;
}

static int execif_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *truedata = NULL, *falsedata = NULL, *end, *firstcomma, *firstquestion;
	struct ast_app *app = NULL;
	AST_DECLARE_APP_ARGS(expr,
		AST_APP_ARG(expr);
		AST_APP_ARG(remainder);
	);
	AST_DECLARE_APP_ARGS(apps,
		AST_APP_ARG(t);
		AST_APP_ARG(f);
	);
	char *parse = ast_strdupa(data);

	firstcomma = strchr(parse, ',');
	firstquestion = strchr(parse, '?');

	if ((firstcomma != NULL && firstquestion != NULL && firstcomma < firstquestion) || (firstquestion == NULL)) {
		/* Deprecated syntax */
		AST_DECLARE_APP_ARGS(depr,
			AST_APP_ARG(expr);
			AST_APP_ARG(appname);
			AST_APP_ARG(appargs);
		);
		AST_STANDARD_APP_ARGS(depr, parse);

		ast_log(LOG_WARNING, "Deprecated syntax found.  Please upgrade to using ExecIf(<expr>?%s(%s))\n", depr.appname, depr.appargs);

		/* Make the two syntaxes look the same */
		expr.expr = depr.expr;
		apps.t = depr.appname;
		apps.f = NULL;
		truedata = depr.appargs;
	} else {
		/* Preferred syntax */

		AST_NONSTANDARD_APP_ARGS(expr, parse, '?');
		if (ast_strlen_zero(expr.remainder)) {
			ast_log(LOG_ERROR, "Usage: ExecIf(<expr>?<appiftrue>(<args>)[:<appiffalse>(<args)])\n");
			return -1;
		}

		AST_NONSTANDARD_APP_ARGS(apps, expr.remainder, ':');

		if (apps.t && (truedata = strchr(apps.t, '('))) {
			*truedata++ = '\0';
			if ((end = strrchr(truedata, ')'))) {
				*end = '\0';
			}
		}

		if (apps.f && (falsedata = strchr(apps.f, '('))) {
			*falsedata++ = '\0';
			if ((end = strrchr(falsedata, ')'))) {
				*end = '\0';
			}
		}
	}

	if (pbx_checkcondition(expr.expr)) {
		if (!ast_strlen_zero(apps.t) && (app = pbx_findapp(apps.t))) {
			res = pbx_exec(chan, app, S_OR(truedata, ""));
		} else {
			ast_log(LOG_WARNING, "Could not find application! (%s)\n", apps.t);
			res = -1;
		}
	} else if (!ast_strlen_zero(apps.f)) {
		if ((app = pbx_findapp(apps.f))) {
			res = pbx_exec(chan, app, S_OR(falsedata, ""));
		} else {
			ast_log(LOG_WARNING, "Could not find application! (%s)\n", apps.f);
			res = -1;
		}
	}

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_exec);
	res |= ast_unregister_application(app_tryexec);
	res |= ast_unregister_application(app_execif);

	return res;
}

static int load_module(void)
{
	int res = ast_register_application(app_exec, exec_exec, exec_synopsis, exec_descrip);
	res |= ast_register_application(app_tryexec, tryexec_exec, tryexec_synopsis, tryexec_descrip);
	res |= ast_register_application(app_execif, execif_exec, execif_synopsis, execif_descrip);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Executes dialplan applications");
