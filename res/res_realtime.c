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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"


static int cli_realtime_load(int fd, int argc, char **argv) 
{
	char *header_format = "%30s  %-30s\n";
	struct ast_variable *var=NULL;

	if(argc<5) {
		ast_cli(fd, "You must supply a family name, a column to match on, and a value to match to.\n");
		return RESULT_FAILURE;
	}

	var = ast_load_realtime(argv[2], argv[3], argv[4], NULL);

	if(var) {
		ast_cli(fd, header_format, "Column Name", "Column Value");
		ast_cli(fd, header_format, "--------------------", "--------------------");
		while(var) {
			ast_cli(fd, header_format, var->name, var->value);
			var = var->next;
		}
	} else {
		ast_cli(fd, "No rows found matching search criteria.\n");
	}
	return RESULT_SUCCESS;
}

static int cli_realtime_update(int fd, int argc, char **argv) {
	int res = 0;

	if(argc<7) {
		ast_cli(fd, "You must supply a family name, a column to update on, a new value, column to match, and value to to match.\n");
		ast_cli(fd, "Ex: realtime update sipfriends name bobsphone port 4343\n will execute SQL as UPDATE sipfriends SET port = 4343 WHERE name = bobsphone\n");
		return RESULT_FAILURE;
	}

	res = ast_update_realtime(argv[2], argv[3], argv[4], argv[5], argv[6], NULL);

	if(res < 0) {
		ast_cli(fd, "Failed to update. Check the debug log for possible SQL related entries.\n");
		return RESULT_SUCCESS;
	}

       ast_cli(fd, "Updated %d RealTime record%s.\n", res, ESS(res));

	return RESULT_SUCCESS;
}

static const char cli_realtime_load_usage[] =
"Usage: realtime load <family> <colmatch> <value>\n"
"       Prints out a list of variables using the RealTime driver.\n";

static const char cli_realtime_update_usage[] =
"Usage: realtime update <family> <colmatch> <value>\n"
"       Update a single variable using the RealTime driver.\n";

static struct ast_cli_entry cli_realtime[] = {
	{ { "realtime", "load", NULL, NULL },
	cli_realtime_load, "Used to print out RealTime variables.",
	cli_realtime_load_usage, NULL },

	{ { "realtime", "update", NULL, NULL },
	cli_realtime_update, "Used to update RealTime variables.",
	cli_realtime_update_usage, NULL },
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	ast_module_user_hangup_all();
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Realtime Data Lookup/Rewrite");
