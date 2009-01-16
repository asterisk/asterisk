/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Russell Bryant <russelb@clemson.edu> 
 *
 * func_db.c adapted from the old app_db.c, copyright by the following people 
 * Copyright (C) 2005, Mark Spencer <markster@digium.com>
 * Copyright (C) 2003, Jefferson Noxon <jeff@debian.org>
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
 * \brief Functions for interaction with the Asterisk database
 *
 * \author Russell Bryant <russelb@clemson.edu>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/astdb.h"

static int function_db_read(struct ast_channel *chan, char *cmd,
			    char *parse, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(family);
			     AST_APP_ARG(key);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (args.argc < 2) {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)\n");
		return -1;
	}

	if (ast_db_get(args.family, args.key, buf, len - 1)) {
		ast_log(LOG_DEBUG, "DB: %s/%s not found in database.\n", args.family,
				args.key);
	} else
		pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);

	return 0;
}

static int function_db_write(struct ast_channel *chan, char *cmd, char *parse,
			     const char *value)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(family);
			     AST_APP_ARG(key);
	);

	if (ast_strlen_zero(parse)) {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)=<value>\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (args.argc < 2) {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)=value\n");
		return -1;
	}

	if (ast_db_put(args.family, args.key, (char *) value))
		ast_log(LOG_WARNING, "DB: Error writing value to database.\n");

	return 0;
}

static struct ast_custom_function db_function = {
	.name = "DB",
	.synopsis = "Read from or write to the Asterisk database",
	.syntax = "DB(<family>/<key>)",
	.desc =
"This function will read from or write a value to the Asterisk database.  On a\n"
"read, this function returns the corresponding value from the database, or blank\n"
"if it does not exist.  Reading a database value will also set the variable\n"
"DB_RESULT.  If you wish to find out if an entry exists, use the DB_EXISTS\n"
"function.\n",
	.read = function_db_read,
	.write = function_db_write,
};

static int function_db_exists(struct ast_channel *chan, char *cmd,
			      char *parse, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(family);
			     AST_APP_ARG(key);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(LOG_WARNING, "DB_EXISTS requires an argument, DB(<family>/<key>)\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (args.argc < 2) {
		ast_log(LOG_WARNING, "DB_EXISTS requires an argument, DB(<family>/<key>)\n");
		return -1;
	}

	if (ast_db_get(args.family, args.key, buf, len - 1))
		strcpy(buf, "0");
	else {
		pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);
		strcpy(buf, "1");
	}

	return 0;
}

static struct ast_custom_function db_exists_function = {
	.name = "DB_EXISTS",
	.synopsis = "Check to see if a key exists in the Asterisk database",
	.syntax = "DB_EXISTS(<family>/<key>)",
	.desc =
		"This function will check to see if a key exists in the Asterisk\n"
		"database. If it exists, the function will return \"1\". If not,\n"
		"it will return \"0\".  Checking for existence of a database key will\n"
		"also set the variable DB_RESULT to the key's value if it exists.\n",
	.read = function_db_exists,
};

static int function_db_delete(struct ast_channel *chan, char* cmd,
			      char *parse, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(family);
			     AST_APP_ARG(key);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(LOG_WARNING, "DB_DELETE requires an argument, DB_DELETE(<family>/<key>)\n");
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (args.argc < 2) {
		ast_log(LOG_WARNING, "DB_DELETE requires an argument, DB_DELETE(<family>/<key>)\n");
		return -1;
	}

	if (ast_db_get(args.family, args.key, buf, len - 1)) {
		ast_log(LOG_DEBUG, "DB_DELETE: %s/%s not found in database.\n", args.family, args.key);
	} else {
		if (ast_db_del(args.family, args.key)) {
			ast_log(LOG_DEBUG, "DB_DELETE: %s/%s could not be deleted from the database\n", 
				args.family, args.key);
		}
	}
	pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);

	return 0;
}


static struct ast_custom_function db_delete_function = {
	.name = "DB_DELETE",
	.synopsis = "Return a value from the database and delete it",
	.syntax = "DB_DELETE(<family>/<key>)",
	.desc =
		"This function will retrieve a value from the Asterisk database\n"
		" and then remove that key from the database.  DB_RESULT\n"
		"will be set to the key's value if it exists.\n",
	.read = function_db_delete,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&db_function);
	res |= ast_custom_function_unregister(&db_exists_function);
	res |= ast_custom_function_unregister(&db_delete_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&db_function);
	res |= ast_custom_function_register(&db_exists_function);
	res |= ast_custom_function_register(&db_delete_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Database (astdb) related dialplan functions");
