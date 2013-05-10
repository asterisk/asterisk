/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Stasis dialplan application.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend>res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_app.h"

/*** DOCUMENTATION
	<application name="Stasis" language="en_US">
		<synopsis>Invoke an external Stasis application.</synopsis>
		<syntax>
			<parameter name="app_name" required="true">
				<para>Name of the application to invoke.</para>
			</parameter>
			<parameter name="args">
				<para>Optional comma-delimited arguments for the
				application invocation.</para>
			</parameter>
		</syntax>
		<description>
			<para>
				Invoke a Stasis application.
			</para>
		</description>
	</application>
 ***/

/*! \brief Maximum number of arguments for the Stasis dialplan application */
#define MAX_ARGS 128

/*! \brief Dialplan application name */
static const char *stasis = "Stasis";

/*! /brief Stasis dialplan application callback */
static int app_exec(struct ast_channel *chan, const char *data)
{
	char *parse = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(app_name);
		AST_APP_ARG(app_argv)[MAX_ARGS];
	);

	ast_assert(chan != NULL);
	ast_assert(data != NULL);

	/* parse the arguments */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 1) {
		ast_log(LOG_WARNING, "Stasis app_name argument missing\n");
		return -1;
	}

	return stasis_app_exec(
		chan, args.app_name, args.argc - 1, args.app_argv);
}

static int load_module(void)
{
	int r = 0;

	stasis_app_ref();
	r |= ast_register_application_xml(stasis, app_exec);
	return r;
}

static int unload_module(void)
{
	int r = 0;
	r |= ast_unregister_application(stasis);
	stasis_app_unref();
	return r;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY,
	AST_MODFLAG_DEFAULT,
	"Stasis dialplan application",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis",
	);
