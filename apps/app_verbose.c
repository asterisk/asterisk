/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_verbose_v001@the-tilghman.com>
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
 * \brief Verbose logging application
 *
 * \author Tilghman Lesher <app_verbose_v001@the-tilghman.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"

static char *app_verbose = "Verbose";
static char *app_log = "Log";

/*** DOCUMENTATION
	<application name="Verbose" language="en_US">
 		<synopsis>
			Send arbitrary text to verbose output.
		</synopsis>
		<syntax>
			<parameter name="level">
				<para>Must be an integer value.  If not specified, defaults to 0.</para>
			</parameter>
			<parameter name="message" required="true">
				<para>Output text message.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends an arbitrary text message to verbose output.</para>
		</description>
	</application>
	<application name="Log" language="en_US">
		<synopsis>
			Send arbitrary text to a selected log level.
		</synopsis>
		<syntax>
			<parameter name="level" required="true">
				<para>Level must be one of <literal>ERROR</literal>, <literal>WARNING</literal>, <literal>NOTICE</literal>,
				<literal>DEBUG</literal>, <literal>VERBOSE</literal> or <literal>DTMF</literal>.</para>
			</parameter>
			<parameter name="message" required="true">
				<para>Output text message.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends an arbitrary text message to a selected log level.</para>
		</description>
	</application>
 ***/


static int verbose_exec(struct ast_channel *chan, const char *data)
{
	unsigned int vsize;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(level);
		AST_APP_ARG(msg);
	);

	if (ast_strlen_zero(data)) {
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc == 1) {
		args.msg = args.level;
		args.level = "0";
	}

	if (sscanf(args.level, "%30u", &vsize) != 1) {
		vsize = 0;
		ast_log(LOG_WARNING, "'%s' is not a verboser number\n", args.level);
	} else if (4 < vsize) {
		vsize = 4;
	}

	ast_verb(vsize, "%s\n", args.msg);

	return 0;
}

static int log_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	int lnum = -1;
	char extension[AST_MAX_EXTENSION + 5], context[AST_MAX_EXTENSION + 2];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(level);
		AST_APP_ARG(msg);
	);

	if (ast_strlen_zero(data))
		return 0;

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!strcasecmp(args.level, "ERROR")) {
		lnum = __LOG_ERROR;
	} else if (!strcasecmp(args.level, "WARNING")) {
		lnum = __LOG_WARNING;
	} else if (!strcasecmp(args.level, "NOTICE")) {
		lnum = __LOG_NOTICE;
	} else if (!strcasecmp(args.level, "DEBUG")) {
		lnum = __LOG_DEBUG;
	} else if (!strcasecmp(args.level, "VERBOSE")) {
		lnum = __LOG_VERBOSE;
	} else if (!strcasecmp(args.level, "DTMF")) {
		lnum = __LOG_DTMF;
	} else {
		ast_log(LOG_ERROR, "Unknown log level: '%s'\n", args.level);
	}

	if (lnum > -1) {
		snprintf(context, sizeof(context), "@ %s", ast_channel_context(chan));
		snprintf(extension, sizeof(extension), "Ext. %s", ast_channel_exten(chan));

		ast_log(lnum, extension, ast_channel_priority(chan), context, "%s\n", args.msg);
	}

	return 0;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app_log, log_exec);
	res |= ast_register_application_xml(app_verbose, verbose_exec);

	return res;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Send verbose output");
