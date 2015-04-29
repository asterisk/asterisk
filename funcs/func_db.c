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
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/astdb.h"

/*** DOCUMENTATION
	<function name="DB" language="en_US">
		<synopsis>
			Read from or write to the Asterisk database.
		</synopsis>
		<syntax argsep="/">
			<parameter name="family" required="true" />
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>This function will read from or write a value to the Asterisk database.  On a
			read, this function returns the corresponding value from the database, or blank
			if it does not exist.  Reading a database value will also set the variable
			DB_RESULT.  If you wish to find out if an entry exists, use the DB_EXISTS
			function.</para>
		</description>
		<see-also>
			<ref type="application">DBdel</ref>
			<ref type="function">DB_DELETE</ref>
			<ref type="application">DBdeltree</ref>
			<ref type="function">DB_EXISTS</ref>
		</see-also>
	</function>
	<function name="DB_EXISTS" language="en_US">
		<synopsis>
			Check to see if a key exists in the Asterisk database.
		</synopsis>
		<syntax argsep="/">
			<parameter name="family" required="true" />
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>This function will check to see if a key exists in the Asterisk
			database. If it exists, the function will return <literal>1</literal>. If not,
			it will return <literal>0</literal>.  Checking for existence of a database key will
			also set the variable DB_RESULT to the key's value if it exists.</para>
		</description>
		<see-also>
			<ref type="function">DB</ref>
		</see-also>
	</function>
	<function name="DB_KEYS" language="en_US">
		<synopsis>
			Obtain a list of keys within the Asterisk database.
		</synopsis>
		<syntax>
			<parameter name="prefix" />
		</syntax>
		<description>
			<para>This function will return a comma-separated list of keys existing
			at the prefix specified within the Asterisk database.  If no argument is
			provided, then a list of key families will be returned.</para>
		</description>
	</function>
	<function name="DB_DELETE" language="en_US">
		<synopsis>
			Return a value from the database and delete it.
		</synopsis>
		<syntax argsep="/">
			<parameter name="family" required="true" />
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>This function will retrieve a value from the Asterisk database
			and then remove that key from the database. <variable>DB_RESULT</variable>
			will be set to the key's value if it exists.</para>
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be read from the
				dialplan, and not directly from external protocols. It can, however, be
				executed as a write operation (<literal>DB_DELETE(family, key)=ignored</literal>)</para>
			</note>
		</description>
		<see-also>
			<ref type="application">DBdel</ref>
			<ref type="function">DB</ref>
			<ref type="application">DBdeltree</ref>
		</see-also>
	</function>
 ***/

static int function_db_read(struct ast_channel *chan, const char *cmd,
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
		ast_debug(1, "DB: %s/%s not found in database.\n", args.family, args.key);
	} else {
		pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);
	}

	return 0;
}

static int function_db_write(struct ast_channel *chan, const char *cmd, char *parse,
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

	if (ast_db_put(args.family, args.key, value)) {
		ast_log(LOG_WARNING, "DB: Error writing value to database.\n");
	}

	return 0;
}

static struct ast_custom_function db_function = {
	.name = "DB",
	.read = function_db_read,
	.write = function_db_write,
};

static int function_db_exists(struct ast_channel *chan, const char *cmd,
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

	if (ast_db_get(args.family, args.key, buf, len - 1)) {
		strcpy(buf, "0");
	} else {
		pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);
		strcpy(buf, "1");
	}

	return 0;
}

static struct ast_custom_function db_exists_function = {
	.name = "DB_EXISTS",
	.read = function_db_exists,
	.read_max = 2,
};

static int function_db_keys(struct ast_channel *chan, const char *cmd, char *parse, struct ast_str **result, ssize_t maxlen)
{
	size_t parselen = strlen(parse);
	struct ast_db_entry *dbe, *orig_dbe;
	struct ast_str *escape_buf = NULL;
	const char *last = "";

	/* Remove leading and trailing slashes */
	while (parse[0] == '/') {
		parse++;
		parselen--;
	}
	while (parse[parselen - 1] == '/') {
		parse[--parselen] = '\0';
	}

	ast_str_reset(*result);

	/* Nothing within the database at that prefix? */
	if (!(orig_dbe = dbe = ast_db_gettree(parse, NULL))) {
		return 0;
	}

	for (; dbe; dbe = dbe->next) {
		/* Find the current component */
		char *curkey = &dbe->key[parselen + 1], *slash;
		if (*curkey == '/') {
			curkey++;
		}
		/* Remove everything after the current component */
		if ((slash = strchr(curkey, '/'))) {
			*slash = '\0';
		}

		/* Skip duplicates */
		if (!strcasecmp(last, curkey)) {
			continue;
		}
		last = curkey;

		if (orig_dbe != dbe) {
			ast_str_append(result, maxlen, ",");
		}
		ast_str_append_escapecommas(result, maxlen, curkey, strlen(curkey));
	}
	ast_db_freetree(orig_dbe);
	ast_free(escape_buf);
	return 0;
}

static struct ast_custom_function db_keys_function = {
	.name = "DB_KEYS",
	.read2 = function_db_keys,
};

static int function_db_delete(struct ast_channel *chan, const char *cmd,
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
		ast_debug(1, "DB_DELETE: %s/%s not found in database.\n", args.family, args.key);
	} else {
		if (ast_db_del(args.family, args.key)) {
			ast_debug(1, "DB_DELETE: %s/%s could not be deleted from the database\n", args.family, args.key);
		}
	}

	pbx_builtin_setvar_helper(chan, "DB_RESULT", buf);

	return 0;
}

/*!
 * \brief Wrapper to execute DB_DELETE from a write operation. Allows execution
 * even if live_dangerously is disabled.
 */
static int function_db_delete_write(struct ast_channel *chan, const char *cmd, char *parse,
	const char *value)
{
	/* Throwaway to hold the result from the read */
	char buf[128];
	return function_db_delete(chan, cmd, parse, buf, sizeof(buf));
}

static struct ast_custom_function db_delete_function = {
	.name = "DB_DELETE",
	.read = function_db_delete,
	.write = function_db_delete_write,
};

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register_escalating(&db_function, AST_CFE_BOTH);
	res |= ast_custom_function_register(&db_exists_function);
	res |= ast_custom_function_register_escalating(&db_delete_function, AST_CFE_READ);
	res |= ast_custom_function_register(&db_keys_function);

	return res;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Database (astdb) related dialplan functions");
