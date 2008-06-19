/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 * Mark Spencer <markster@digium.com>
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
 * \brief RealTime CLI
 *
 * \author Anthony Minessale <anthmct@yahoo.com>
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"


static char *cli_realtime_load(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) 
{
#define CRL_HEADER_FORMAT "%30s  %-30s\n"
	struct ast_variable *var=NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "realtime load";
		e->usage =
			"Usage: realtime load <family> <colmatch> <value>\n"
			"       Prints out a list of variables using the RealTime driver.\n"
			"       You must supply a family name, a column to match on, and a value to match to.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}


	if (a->argc < 5) 
		return CLI_SHOWUSAGE;

	var = ast_load_realtime_all(a->argv[2], a->argv[3], a->argv[4], SENTINEL);

	if (var) {
		ast_cli(a->fd, CRL_HEADER_FORMAT, "Column Name", "Column Value");
		ast_cli(a->fd, CRL_HEADER_FORMAT, "--------------------", "--------------------");
		while (var) {
			ast_cli(a->fd, CRL_HEADER_FORMAT, var->name, var->value);
			var = var->next;
		}
	} else {
		ast_cli(a->fd, "No rows found matching search criteria.\n");
	}
	return CLI_SUCCESS;
}

static char *cli_realtime_update(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
	int res = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "realtime update";
		e->usage =
			"Usage: realtime update <family> <colupdate> <newvalue> <colmatch> <valuematch>\n"
			"       Update a single variable using the RealTime driver.\n"
			"       You must supply a family name, a column to update on, a new value, column to match, and value to match.\n"
			"       Ex: realtime update sipfriends name bobsphone port 4343\n"
			"       will execute SQL as UPDATE sipfriends SET port = 4343 WHERE name = bobsphone\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}


	if (a->argc < 7) 
		return CLI_SHOWUSAGE;

	res = ast_update_realtime(a->argv[2], a->argv[3], a->argv[4], a->argv[5], a->argv[6], SENTINEL);

	if(res < 0) {
		ast_cli(a->fd, "Failed to update. Check the debug log for possible SQL related entries.\n");
		return CLI_FAILURE;
	}

       ast_cli(a->fd, "Updated %d RealTime record%s.\n", res, ESS(res));

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_realtime[] = {
	AST_CLI_DEFINE(cli_realtime_load, "Used to print out RealTime variables."),
	AST_CLI_DEFINE(cli_realtime_update, "Used to update RealTime variables."),
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Realtime Data Lookup/Rewrite");
