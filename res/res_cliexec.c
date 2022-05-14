/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \brief Execute dialplan applications from the CLI
 *
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"

static const struct ast_channel_tech mock_channel_tech = {
};

static int cli_chan = 0;

/*! \brief CLI support for executing application */
static char *handle_exec(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c = NULL;
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	char *app_name, *app_args;
	int ret = 0;
	struct ast_app *app;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan exec application";
		e->usage =
			"Usage: dialplan exec application <appname> [<args>]\n"
			"       Execute a single dialplan application call for\n"
			"       testing. A mock channel is used to execute\n"
			"       the application, so it may not make\n"
			"       sense to use all applications, and only\n"
			"       global variables should be used.\n"
			"       The ulaw, alaw, and h264 codecs are available.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args + 1 && a->argc != e->args + 2) {
		return CLI_SHOWUSAGE;
	}

	app_name = (char *) a->argv[3];
	app_args = a->argc == e->args + 2 ? (char *) a->argv[4] : NULL;

	if (!app_name) {
		return CLI_FAILURE;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_log(LOG_WARNING, "Could not allocate an empty format capabilities structure\n");
		return CLI_FAILURE;
	}

	if (ast_format_cap_append(caps, ast_format_ulaw, 0)) {
		ast_log(LOG_WARNING, "Failed to append a ulaw format to capabilities for channel nativeformats\n");
		return CLI_FAILURE;
	}

	if (ast_format_cap_append(caps, ast_format_alaw, 0)) {
		ast_log(LOG_WARNING, "Failed to append an alaw format to capabilities for channel nativeformats\n");
		return CLI_FAILURE;
	}

	if (ast_format_cap_append(caps, ast_format_h264, 0)) {
		ast_log(LOG_WARNING, "Failed to append an h264 format to capabilities for channel nativeformats\n");
		return CLI_FAILURE;
	}

	c = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "CLIExec/%d", ++cli_chan);
	if (!c) {
		ast_cli(a->fd, "Unable to allocate mock channel for application execution.\n");
		return CLI_FAILURE;
	}
	ast_channel_tech_set(c, &mock_channel_tech);
	ast_channel_nativeformats_set(c, caps);
	ast_channel_set_writeformat(c, ast_format_slin);
	ast_channel_set_rawwriteformat(c, ast_format_slin);
	ast_channel_set_readformat(c, ast_format_slin);
	ast_channel_set_rawreadformat(c, ast_format_slin);
	ast_channel_unlock(c);

	app = pbx_findapp(app_name);
	if (!app) {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", app_name);
		ast_hangup(c);
		return CLI_FAILURE;
	} else {
		struct ast_str *substituted_args = ast_str_create(16);

		if (substituted_args) {
			ast_str_substitute_variables(&substituted_args, 0, c, app_args);
			ast_cli(a->fd, "Executing: %s(%s)\n", app_name, ast_str_buffer(substituted_args));
			ret = pbx_exec(c, app, ast_str_buffer(substituted_args));
			ast_free(substituted_args);
		} else {
			ast_log(LOG_WARNING, "Could not substitute application argument variables for %s\n", app_name);
			ast_cli(a->fd, "Executing: %s(%s)\n", app_name, app_args);
			ret = pbx_exec(c, app, app_args);
		}
	}

	ast_hangup(c); /* no need to unref separately */

	ast_cli(a->fd, "Return Value: %s (%d)\n", ret ? "Failure" : "Success", ret);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_cliorig[] = {
	AST_CLI_DEFINE(handle_exec, "Execute a dialplan application"),
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

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Simple dialplan execution from the CLI");
