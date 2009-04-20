/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2005, 2006 Tilghman Lesher
 *
 * Tilghman Lesher <func_odbc__200508@the-tilghman.com>
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
 *
 * \brief ODBC lookups
 *
 * \author Tilghman Lesher <func_odbc__200508@the-tilghman.com>
 */

/*** MODULEINFO
	<depend>unixodbc</depend>
	<depend>ltdl</depend>
	<depend>res_odbc</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/res_odbc.h"
#include "asterisk/app.h"

static char *config = "func_odbc.conf";

enum {
	OPT_ESCAPECOMMAS =	(1 << 0),
} odbc_option_flags;

struct acf_odbc_query {
	AST_LIST_ENTRY(acf_odbc_query) list;
	char dsn[30];
	char sql_read[2048];
	char sql_write[2048];
	unsigned int flags;
	struct ast_custom_function *acf;
};

AST_LIST_HEAD_STATIC(queries, acf_odbc_query);

static SQLHSTMT generic_prepare(struct odbc_obj *obj, void *data)
{
	int res;
	char *sql = data;
	SQLHSTMT stmt;

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	res = SQLPrepare(stmt, (unsigned char *)sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
		SQLCloseCursor(stmt);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	return stmt;
}

/*
 * Master control routine
 */
static int acf_odbc_write(struct ast_channel *chan, char *cmd, char *s, const char *value)
{
	struct odbc_obj *obj;
	struct acf_odbc_query *query;
	char *t, buf[2048]="", varname[15];
	int i, bogus_chan = 0;
	AST_DECLARE_APP_ARGS(values,
		AST_APP_ARG(field)[100];
	);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field)[100];
	);
	SQLHSTMT stmt;
	SQLLEN rows=0;

	AST_LIST_LOCK(&queries);
	AST_LIST_TRAVERSE(&queries, query, list) {
		if (!strcmp(query->acf->name, cmd)) {
			break;
		}
	}

	if (!query) {
		ast_log(LOG_ERROR, "No such function '%s'\n", cmd);
		AST_LIST_UNLOCK(&queries);
		return -1;
	}

	obj = ast_odbc_request_obj(query->dsn, 0);

	if (!obj) {
		ast_log(LOG_ERROR, "No database handle available with the name of '%s' (check res_odbc.conf)\n", query->dsn);
		AST_LIST_UNLOCK(&queries);
		return -1;
	}

	if (!chan) {
		if ((chan = ast_channel_alloc(0, 0, "", "", "", "", "", 0, "Bogus/func_odbc")))
			bogus_chan = 1;
	}

	if (chan)
		ast_autoservice_start(chan);

	/* Parse our arguments */
	t = value ? ast_strdupa(value) : "";

	if (!s || !t) {
		ast_log(LOG_ERROR, "Out of memory\n");
		AST_LIST_UNLOCK(&queries);
		if (chan)
			ast_autoservice_stop(chan);
		if (bogus_chan)
			ast_channel_free(chan);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, s);
	for (i = 0; i < args.argc; i++) {
		snprintf(varname, sizeof(varname), "ARG%d", i + 1);
		pbx_builtin_pushvar_helper(chan, varname, args.field[i]);
	}

	/* Parse values, just like arguments */
	/* Can't use the pipe, because app Set removes them */
	AST_NONSTANDARD_APP_ARGS(values, t, ',');
	for (i = 0; i < values.argc; i++) {
		snprintf(varname, sizeof(varname), "VAL%d", i + 1);
		pbx_builtin_pushvar_helper(chan, varname, values.field[i]);
	}

	/* Additionally set the value as a whole (but push an empty string if value is NULL) */
	pbx_builtin_pushvar_helper(chan, "VALUE", value ? value : "");

	pbx_substitute_variables_helper(chan, query->sql_write, buf, sizeof(buf) - 1);

	/* Restore prior values */
	for (i = 0; i < args.argc; i++) {
		snprintf(varname, sizeof(varname), "ARG%d", i + 1);
		pbx_builtin_setvar_helper(chan, varname, NULL);
	}

	for (i = 0; i < values.argc; i++) {
		snprintf(varname, sizeof(varname), "VAL%d", i + 1);
		pbx_builtin_setvar_helper(chan, varname, NULL);
	}
	pbx_builtin_setvar_helper(chan, "VALUE", NULL);

	AST_LIST_UNLOCK(&queries);

	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, buf);

	if (stmt) {
		/* Rows affected */
		SQLRowCount(stmt, &rows);
	}

	/* Output the affected rows, for all cases.  In the event of failure, we
	 * flag this as -1 rows.  Note that this is different from 0 affected rows
	 * which would be the case if we succeeded in our query, but the values did
	 * not change. */
	snprintf(varname, sizeof(varname), "%d", (int)rows);
	pbx_builtin_setvar_helper(chan, "ODBCROWS", varname);

	if (stmt) {
		SQLCloseCursor(stmt);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	}
	if (obj)
		ast_odbc_release_obj(obj);

	if (chan)
		ast_autoservice_stop(chan);
	if (bogus_chan)
		ast_channel_free(chan);

	return 0;
}

static int acf_odbc_read(struct ast_channel *chan, char *cmd, char *s, char *buf, size_t len)
{
	struct odbc_obj *obj;
	struct acf_odbc_query *query;
	char sql[2048] = "", varname[15];
	int res, x, buflen = 1, escapecommas, bogus_chan = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field)[100];
	);
	SQLHSTMT stmt;
	SQLSMALLINT colcount=0;
	SQLLEN indicator;

	/* Reset, in case of an error */
	pbx_builtin_setvar_helper(chan, "~ODBCVALUES~", "");

	AST_LIST_LOCK(&queries);
	AST_LIST_TRAVERSE(&queries, query, list) {
		if (!strcmp(query->acf->name, cmd)) {
			break;
		}
	}

	if (!query) {
		ast_log(LOG_ERROR, "No such function '%s'\n", cmd);
		AST_LIST_UNLOCK(&queries);
		return -1;
	}

	obj = ast_odbc_request_obj(query->dsn, 0);

	if (!obj) {
		ast_log(LOG_ERROR, "No such DSN registered (or out of connections): %s (check res_odbc.conf)\n", query->dsn);
		AST_LIST_UNLOCK(&queries);
		return -1;
	}

	if (!chan) {
		if ((chan = ast_channel_alloc(0, 0, "", "", "", "", "", 0, "Bogus/func_odbc")))
			bogus_chan = 1;
	}

	if (chan)
		ast_autoservice_start(chan);

	AST_STANDARD_APP_ARGS(args, s);
	for (x = 0; x < args.argc; x++) {
		snprintf(varname, sizeof(varname), "ARG%d", x + 1);
		pbx_builtin_pushvar_helper(chan, varname, args.field[x]);
	}

	pbx_substitute_variables_helper(chan, query->sql_read, sql, sizeof(sql) - 1);

	/* Restore prior values */
	for (x = 0; x < args.argc; x++) {
		snprintf(varname, sizeof(varname), "ARG%d", x + 1);
		pbx_builtin_setvar_helper(chan, varname, NULL);
	}

	/* Save this flag, so we can release the lock */
	escapecommas = ast_test_flag(query, OPT_ESCAPECOMMAS);

	AST_LIST_UNLOCK(&queries);

	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, sql);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		if (chan)
			ast_autoservice_stop(chan);
		if (bogus_chan)
			ast_channel_free(chan);
		return -1;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLCloseCursor(stmt);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		if (chan)
			ast_autoservice_stop(chan);
		if (bogus_chan)
			ast_channel_free(chan);
		return -1;
	}

	*buf = '\0';

	res = SQLFetch(stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		int res1 = -1;
		if (res == SQL_NO_DATA) {
			if (option_verbose > 3) {
				ast_verbose(VERBOSE_PREFIX_4 "Found no rows [%s]\n", sql);
			}
			res1 = 0;
		} else if (option_verbose > 3) {
			ast_log(LOG_WARNING, "Error %d in FETCH [%s]\n", res, sql);
		}
		SQLCloseCursor(stmt);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		if (chan)
			ast_autoservice_stop(chan);
		if (bogus_chan)
			ast_channel_free(chan);
		return res1;
	}

	for (x = 0; x < colcount; x++) {
		int i;
		char coldata[256];

		buflen = strlen(buf);
		res = SQLGetData(stmt, x + 1, SQL_CHAR, coldata, sizeof(coldata), &indicator);
		if (indicator == SQL_NULL_DATA) {
			coldata[0] = '\0';
			res = SQL_SUCCESS;
		}

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLCloseCursor(stmt);
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			if (chan)
				ast_autoservice_stop(chan);
			if (bogus_chan)
				ast_channel_free(chan);
			return -1;
		}

		/* Copy data, encoding '\', ',', '"', and '|' for the argument parser */
		for (i = 0; i < sizeof(coldata); i++) {
			if (escapecommas && strchr("\\,|\"", coldata[i])) {
				buf[buflen++] = '\\';
			}
			buf[buflen++] = coldata[i];

			if (buflen >= len - 2)
				break;

			if (coldata[i] == '\0')
				break;
		}

		buf[buflen - 1] = ',';
		buf[buflen] = '\0';
	}
	/* Trim trailing comma */
	buf[buflen - 1] = '\0';

	SQLCloseCursor(stmt);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);

	/* Pass an unadulterated string to ARRAY, if needed.  This is only needed
	 * in 1.4, because of the misfeature in Set. */
	pbx_builtin_setvar_helper(chan, "~ODBCVALUES~", buf);
	if (chan)
		ast_autoservice_stop(chan);
	if (bogus_chan)
		ast_channel_free(chan);
	return 0;
}

static int acf_escape(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char *out = buf;

	for (; *data && out - buf < len; data++) {
		if (*data == '\'') {
			*out = '\'';
			out++;
		}
		*out++ = *data;
	}
	*out = '\0';

	return 0;
}

static struct ast_custom_function escape_function = {
	.name = "SQL_ESC",
	.synopsis = "Escapes single ticks for use in SQL statements",
	.syntax = "SQL_ESC(<string>)",
	.desc =
"Used in SQL templates to escape data which may contain single ticks (') which\n"
"are otherwise used to delimit data.  For example:\n"
"SELECT foo FROM bar WHERE baz='${SQL_ESC(${ARG1})}'\n",
	.read = acf_escape,
	.write = NULL,
};

static int init_acf_query(struct ast_config *cfg, char *catg, struct acf_odbc_query **query)
{
	const char *tmp;
	int res;

	if (!cfg || !catg) {
		return -1;
	}

	*query = ast_calloc(1, sizeof(struct acf_odbc_query));
	if (! (*query))
		return -1;

	if ((tmp = ast_variable_retrieve(cfg, catg, "dsn"))) {
		ast_copy_string((*query)->dsn, tmp, sizeof((*query)->dsn));
	} else if ((tmp = ast_variable_retrieve(cfg, catg, "writehandle")) || (tmp = ast_variable_retrieve(cfg, catg, "readhandle"))) {
		ast_log(LOG_WARNING, "Separate read and write handles are not supported in this version of func_odbc.so\n");
		ast_copy_string((*query)->dsn, tmp, sizeof((*query)->dsn));
	} else {
		free(*query);
		*query = NULL;
		ast_log(LOG_ERROR, "No database handle was specified for func_odbc class '%s'\n", catg);
		return -1;
	}

	if ((tmp = ast_variable_retrieve(cfg, catg, "read")) || (tmp = ast_variable_retrieve(cfg, catg, "readsql"))) {
		ast_copy_string((*query)->sql_read, tmp, sizeof((*query)->sql_read));
	}

	if ((tmp = ast_variable_retrieve(cfg, catg, "write")) || (tmp = ast_variable_retrieve(cfg, catg, "writesql"))) {
		ast_copy_string((*query)->sql_write, tmp, sizeof((*query)->sql_write));
	}

	/* Allow escaping of embedded commas in fields to be turned off */
	ast_set_flag((*query), OPT_ESCAPECOMMAS);
	if ((tmp = ast_variable_retrieve(cfg, catg, "escapecommas"))) {
		if (ast_false(tmp))
			ast_clear_flag((*query), OPT_ESCAPECOMMAS);
	}

	(*query)->acf = ast_calloc(1, sizeof(struct ast_custom_function));
	if (! (*query)->acf) {
		free(*query);
		*query = NULL;
		return -1;
	}

	if ((tmp = ast_variable_retrieve(cfg, catg, "prefix")) && !ast_strlen_zero(tmp)) {
		if (asprintf((char **)&((*query)->acf->name), "%s_%s", tmp, catg) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
		}
	} else {
		if (asprintf((char **)&((*query)->acf->name), "ODBC_%s", catg) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
		}
	}

	if (!((*query)->acf->name)) {
		free((*query)->acf);
		free(*query);
		*query = NULL;
		return -1;
	}

	if (asprintf((char **)&((*query)->acf->syntax), "%s(<arg1>[...[,<argN>]])", (*query)->acf->name) < 0) {
		ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
		(*query)->acf->syntax = NULL;
	}

	if (!((*query)->acf->syntax)) {
		free((char *)(*query)->acf->name);
		free((*query)->acf);
		free(*query);
		*query = NULL;
		return -1;
	}

	res = 0;
	(*query)->acf->synopsis = "Runs the referenced query with the specified arguments";
	if (!ast_strlen_zero((*query)->sql_read) && !ast_strlen_zero((*query)->sql_write)) {
		res = asprintf((char **)&((*query)->acf->desc),
			       "Runs the following query, as defined in func_odbc.conf, performing\n"
			       "substitution of the arguments into the query as specified by ${ARG1},\n"
			       "${ARG2}, ... ${ARGn}.  When setting the function, the values are provided\n"
			       "either in whole as ${VALUE} or parsed as ${VAL1}, ${VAL2}, ... ${VALn}.\n"
			       "\nRead:\n%s\n\nWrite:\n%s\n",
			       (*query)->sql_read,
			       (*query)->sql_write);
	} else if (!ast_strlen_zero((*query)->sql_read)) {
		res = asprintf((char **)&((*query)->acf->desc),
			       "Runs the following query, as defined in func_odbc.conf, performing\n"
			       "substitution of the arguments into the query as specified by ${ARG1},\n"
			       "${ARG2}, ... ${ARGn}.  This function may only be read, not set.\n\nSQL:\n%s\n",
			       (*query)->sql_read);
	} else if (!ast_strlen_zero((*query)->sql_write)) {
		res = asprintf((char **)&((*query)->acf->desc),
			       "Runs the following query, as defined in func_odbc.conf, performing\n"
			       "substitution of the arguments into the query as specified by ${ARG1},\n"
			       "${ARG2}, ... ${ARGn}.  The values are provided either in whole as\n"
			       "${VALUE} or parsed as ${VAL1}, ${VAL2}, ... ${VALn}.\n"
			       "This function may only be set.\nSQL:\n%s\n",
			       (*query)->sql_write);
	} else {
		ast_log(LOG_ERROR, "No SQL was found for func_odbc class '%s'\n", catg);
	}

	if (res < 0) {
		ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
		(*query)->acf->desc = NULL;
	}

	/* Could be out of memory, or could be we have neither sql_read nor sql_write */
	if (!((*query)->acf->desc)) {
		free((char *)(*query)->acf->syntax);
		free((char *)(*query)->acf->name);
		free((*query)->acf);
		free(*query);
		*query = NULL;
		return -1;
	}

	if (ast_strlen_zero((*query)->sql_read)) {
		(*query)->acf->read = NULL;
	} else {
		(*query)->acf->read = acf_odbc_read;
	}

	if (ast_strlen_zero((*query)->sql_write)) {
		(*query)->acf->write = NULL;
	} else {
		(*query)->acf->write = acf_odbc_write;
	}

	return 0;
}

static int free_acf_query(struct acf_odbc_query *query)
{
	if (query) {
		if (query->acf) {
			if (query->acf->name)
				free((char *)query->acf->name);
			if (query->acf->syntax)
				free((char *)query->acf->syntax);
			if (query->acf->desc)
				free((char *)query->acf->desc);
			free(query->acf);
		}
		free(query);
	}
	return 0;
}

static int odbc_load_module(void)
{
	int res = 0;
	struct ast_config *cfg;
	char *catg;

	AST_LIST_LOCK(&queries);

	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config for func_odbc: %s\n", config);
		AST_LIST_UNLOCK(&queries);
		return AST_MODULE_LOAD_DECLINE;
	}

	for (catg = ast_category_browse(cfg, NULL);
	     catg;
	     catg = ast_category_browse(cfg, catg)) {
		struct acf_odbc_query *query = NULL;

		if (init_acf_query(cfg, catg, &query)) {
			free_acf_query(query);
		} else {
			AST_LIST_INSERT_HEAD(&queries, query, list);
			ast_custom_function_register(query->acf);
		}
	}

	ast_config_destroy(cfg);
	ast_custom_function_register(&escape_function);

	AST_LIST_UNLOCK(&queries);
	return res;
}

static int odbc_unload_module(void)
{
	struct acf_odbc_query *query;

	AST_LIST_LOCK(&queries);
	while (!AST_LIST_EMPTY(&queries)) {
		query = AST_LIST_REMOVE_HEAD(&queries, list);
		ast_custom_function_unregister(query->acf);
		free_acf_query(query);
	}

	ast_custom_function_unregister(&escape_function);

	/* Allow any threads waiting for this lock to pass (avoids a race) */
	AST_LIST_UNLOCK(&queries);
	AST_LIST_LOCK(&queries);

	AST_LIST_UNLOCK(&queries);
	return 0;
}

static int reload(void)
{
	int res = 0;
	struct ast_config *cfg;
	struct acf_odbc_query *oldquery;
	char *catg;

	AST_LIST_LOCK(&queries);

	while (!AST_LIST_EMPTY(&queries)) {
		oldquery = AST_LIST_REMOVE_HEAD(&queries, list);
		ast_custom_function_unregister(oldquery->acf);
		free_acf_query(oldquery);
	}

	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_WARNING, "Unable to load config for func_odbc: %s\n", config);
		goto reload_out;
	}

	for (catg = ast_category_browse(cfg, NULL);
	     catg;
	     catg = ast_category_browse(cfg, catg)) {
		struct acf_odbc_query *query = NULL;

		if (init_acf_query(cfg, catg, &query)) {
			ast_log(LOG_ERROR, "Cannot initialize query %s\n", catg);
		} else {
			AST_LIST_INSERT_HEAD(&queries, query, list);
			ast_custom_function_register(query->acf);
		}
	}

	ast_config_destroy(cfg);
reload_out:
	AST_LIST_UNLOCK(&queries);
	return res;
}

static int unload_module(void)
{
	return odbc_unload_module();
}

static int load_module(void)
{
	return odbc_load_module();
}

/* XXX need to revise usecount - set if query_lock is set */

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "ODBC lookups",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

