/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Russell Bryant <russelb@clemson.edu> 
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
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk.h"

/* ASTERISK_FILE_VERSION(__FILE__, "$Revision$") */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/astdb.h"

static char *function_db_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int argc;	
	char *args;
	char *argv[2];
	char *family;
	char *key;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)\n");
		return buf;
	}

	args = ast_strdupa(data);
	argc = ast_separate_app_args(args, '/', argv, sizeof(argv) / sizeof(argv[0]));
	
	if (argc > 1) {
		family = argv[0];
		key = argv[1];
	} else {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)\n");
		return buf;
	}

	if (ast_db_get(family, key, buf, len-1)) {
		ast_log(LOG_DEBUG, "DB: %s/%s not found in database.\n", family, key);
	} else
		pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);

	
	return buf;
}

static void function_db_write(struct ast_channel *chan, char *cmd, char *data, const char *value) 
{
	int argc;	
	char *args;
	char *argv[2];
	char *family;
	char *key;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)=<value>\n");
		return;
	}

	args = ast_strdupa(data);
	argc = ast_separate_app_args(args, '/', argv, sizeof(argv) / sizeof(argv[0]));
	
	if (argc > 1) {
		family = argv[0];
		key = argv[1];
	} else {
		ast_log(LOG_WARNING, "DB requires an argument, DB(<family>/<key>)=value\n");
		return;
	}

	if (ast_db_put(family, key, (char*)value)) {
		ast_log(LOG_WARNING, "DB: Error writing value to database.\n");
	}
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function db_function = {
	.name = "DB",
	.synopsis = "Read or Write from/to the Asterisk database",
	.syntax = "DB(<family>/<key>)",
	.desc = "This function will read or write a value from/to the Asterisk database.\n"
		"DB(...) will read a value from the database, while DB(...)=value\n"
		"will write a value to the database.  On a read, this function\n"
		"returns the value from the datase, or NULL if it does not exist.\n"
		"On a write, this function will always return NULL.  Reading a database value\n"
		"will also set the variable DB_RESULT.\n",
	.read = function_db_read,
	.write = function_db_write,
};

static char *function_db_exists(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int argc;	
	char *args;
	char *argv[2];
	char *family;
	char *key;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "DB_EXISTS requires an argument, DB(<family>/<key>)\n");
		return buf;
	}

	args = ast_strdupa(data);
	argc = ast_separate_app_args(args, '/', argv, sizeof(argv) / sizeof(argv[0]));
	
	if (argc > 1) {
		family = argv[0];
		key = argv[1];
	} else {
		ast_log(LOG_WARNING, "DB_EXISTS requires an argument, DB(<family>/<key>)\n");
		return buf;
	}

	if (ast_db_get(family, key, buf, len-1))
		ast_copy_string(buf, "0", len);	
	else {
		pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);
		ast_copy_string(buf, "1", len);
	}
	
	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function db_exists_function = {
	.name = "DB_EXISTS",
	.synopsis = "Check to see if a key exists in the Asterisk database",
	.syntax = "DB_EXISTS(<family>/<key>)",
	.desc = "This function will check to see if a key exists in the Asterisk\n"
		"database. If it exists, the function will return \"1\". If not,\n"
		"it will return \"0\".  Checking for existence of a database key will\n"
		"also set the variable DB_RESULT to the key's value if it exists.\n",
	.read = function_db_exists,
};
