/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Originate calls via the CLI
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/frame.h"

/*! The timeout for originated calls, in seconds */
#define TIMEOUT 30

/*!
 * \brief orginate a call from the CLI
 * \param fd file descriptor for cli
 * \param chan channel to create type/data
 * \param app application you want to run
 * \param appdata data for application
 * \retval CLI_SUCCESS on success.
 * \retval CLI_SHOWUSAGE on failure.
*/
static char *orig_app(int fd, const char *chan, const char *app, const char *appdata)
{
	char *chantech;
	char *chandata;
	int reason = 0;

	if (ast_strlen_zero(app))
		return CLI_SHOWUSAGE;

	chandata = ast_strdupa(chan);

	chantech = strsep(&chandata, "/");
	if (!chandata) {
		ast_cli(fd, "*** No data provided after channel type! ***\n");
		return CLI_SHOWUSAGE;
	}

	ast_pbx_outgoing_app(chantech, AST_FORMAT_SLINEAR, chandata, TIMEOUT * 1000, app, appdata, &reason, 0, NULL, NULL, NULL, NULL, NULL);

	return CLI_SUCCESS;
}

/*!
 * \brief orginate from extension
 * \param fd file descriptor for cli
 * \param chan channel to create type/data
 * \param data contains exten\@context
 * \retval CLI_SUCCESS on success.
 * \retval CLI_SHOWUSAGE on failure.
*/
static char *orig_exten(int fd, const char *chan, const char *data)
{
	char *chantech;
	char *chandata;
	char *exten = NULL;
	char *context = NULL;
	int reason = 0;

	chandata = ast_strdupa(chan);

	chantech = strsep(&chandata, "/");
	if (!chandata) {
		ast_cli(fd, "*** No data provided after channel type! ***\n");
		return CLI_SHOWUSAGE;
	}

	if (!ast_strlen_zero(data)) {
		context = ast_strdupa(data);
		exten = strsep(&context, "@");
	}

	if (ast_strlen_zero(exten))
		exten = "s";
	if (ast_strlen_zero(context))
		context = "default";

	ast_pbx_outgoing_exten(chantech, AST_FORMAT_SLINEAR, chandata, TIMEOUT * 1000, context, exten, 1, &reason, 0, NULL, NULL, NULL, NULL, NULL);

	return CLI_SUCCESS;
}

/*!
 * \brief handle for orgination app or exten.
 * \param e pointer to the CLI structure to initialize
 * \param cmd operation to execute
 * \param a structure that contains either application or extension arguments
 * \retval CLI_SUCCESS on success.
 * \retval CLI_SHOWUSAGE on failure.*/
static char *handle_orig(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static const char * const choices[] = { "application", "extension", NULL };
	char *res = NULL;
	switch (cmd) {
	case CLI_INIT:
		e->command = "channel originate";
		e->usage =
			"  There are two ways to use this command. A call can be originated between a\n"
			"channel and a specific application, or between a channel and an extension in\n"
			"the dialplan. This is similar to call files or the manager originate action.\n"
			"Calls originated with this command are given a timeout of 30 seconds.\n\n"

			"Usage1: channel originate <tech/data> application <appname> [appdata]\n"
			"  This will originate a call between the specified channel tech/data and the\n"
			"given application. Arguments to the application are optional. If the given\n"
			"arguments to the application include spaces, all of the arguments to the\n"
			"application need to be placed in quotation marks.\n\n"

			"Usage2: channel originate <tech/data> extension [exten@][context]\n"
			"  This will originate a call between the specified channel tech/data and the\n"
			"given extension. If no context is specified, the 'default' context will be\n"
			"used. If no extension is given, the 's' extension will be used.\n";
		return NULL;
	case CLI_GENERATE:
		/* ugly, can be removed when CLI entries have ast_module pointers */
		ast_module_ref(ast_module_info->self);
		if (a->pos == 3) {
			res = ast_cli_complete(a->word, choices, a->n);
		} else if (a->pos == 4) {
			if (!strcasecmp("application", a->argv[3])) {
				res = ast_complete_applications(a->line, a->word, a->n);
			}
		}
		ast_module_unref(ast_module_info->self);
		return res;
	}

	if (ast_strlen_zero(a->argv[2]) || ast_strlen_zero(a->argv[3]))
		return CLI_SHOWUSAGE;

	/* ugly, can be removed when CLI entries have ast_module pointers */
	ast_module_ref(ast_module_info->self);

	if (!strcasecmp("application", a->argv[3])) {
		res = orig_app(a->fd, a->argv[2], a->argv[4], a->argv[5]);
	} else if (!strcasecmp("extension", a->argv[3])) {
		res = orig_exten(a->fd, a->argv[2], a->argv[4]);
	} else {
		ast_log(LOG_WARNING, "else");
		res = CLI_SHOWUSAGE;
	}

	ast_module_unref(ast_module_info->self);

	return res;
}

static char *handle_redirect(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *name, *dest;
	struct ast_channel *chan;
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "channel redirect";
		e->usage = ""
		"Usage: channel redirect <channel> <[[context,]exten,]priority>\n"
		"    Redirect an active channel to a specified extension.\n";
		/*! \todo It would be nice to be able to redirect 2 channels at the same
		 *  time like you can with AMI redirect.  However, it is not possible to acquire
		 *  two channels without the potential for a deadlock with how ast_channel structs
		 *  are managed today.  Once ast_channel is a refcounted object, this command
		 *  will be able to support that. */
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}

	if (a->argc != e->args + 2) {
		return CLI_SHOWUSAGE;
	}

	name = a->argv[2];
	dest = a->argv[3];

	if (!(chan = ast_channel_get_by_name(name))) {
		ast_cli(a->fd, "Channel '%s' not found\n", name);
		return CLI_FAILURE;
	}

	res = ast_async_parseable_goto(chan, dest);

	chan = ast_channel_unref(chan);

	if (!res) {
		ast_cli(a->fd, "Channel '%s' successfully redirected to %s\n", name, dest);
	} else {
		ast_cli(a->fd, "Channel '%s' failed to be redirected to %s\n", name, dest);
	}

	return res ? CLI_FAILURE : CLI_SUCCESS;
}

static struct ast_cli_entry cli_cliorig[] = {
	AST_CLI_DEFINE(handle_orig, "Originate a call"),
	AST_CLI_DEFINE(handle_redirect, "Redirect a call"),
};

static int unload_module(void)
{
	return ast_cli_unregister_multiple(cli_cliorig, ARRAY_LEN(cli_cliorig));
}

static int load_module(void)
{
	int res;
	res = ast_cli_register_multiple(cli_cliorig, ARRAY_LEN(cli_cliorig));
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Call origination and redirection from the CLI");
