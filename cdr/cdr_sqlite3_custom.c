/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com> and others.
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
 * \brief Custom SQLite3 CDR records.
 *
 * \author Adapted by Alejandro Rios <alejandro.rios@avatar.com.co> and
 *  Russell Bryant <russell@digium.com> from
 *  cdr_mysql_custom by Edward Eastman <ed@dm3.co.uk>,
 *	and cdr_sqlite by Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 *
 * \arg See also \ref AstCDR
 *
 *
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>sqlite3</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <sqlite3.h>

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"

AST_MUTEX_DEFINE_STATIC(lock);

static const char config_file[] = "cdr_sqlite3_custom.conf";

static const char desc[] = "Customizable SQLite3 CDR Backend";
static const char name[] = "cdr_sqlite3_custom";
static sqlite3 *db = NULL;

static char table[80];
static char *columns;
static int busy_timeout;

struct values {
	AST_LIST_ENTRY(values) list;
	char expression[1];
};

static AST_LIST_HEAD_STATIC(sql_values, values);

static void free_config(int reload);

static int load_column_config(const char *tmp)
{
	char *col = NULL;
	char *cols = NULL, *save = NULL;
	char *escaped = NULL;
	struct ast_str *column_string = NULL;

	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_WARNING, "Column names not specified. Module not loaded.\n");
		return -1;
	}
	if (!(column_string = ast_str_create(1024))) {
		ast_log(LOG_ERROR, "Out of memory creating temporary buffer for column list for table '%s.'\n", table);
		return -1;
	}
	if (!(save = cols = ast_strdup(tmp))) {
		ast_log(LOG_ERROR, "Out of memory creating temporary buffer for column list for table '%s.'\n", table);
		ast_free(column_string);
		return -1;
	}
	while ((col = strsep(&cols, ","))) {
		col = ast_strip(col);
		escaped = sqlite3_mprintf("%q", col);
		if (!escaped) {
			ast_log(LOG_ERROR, "Out of memory creating entry for column '%s' in table '%s.'\n", col, table);
			ast_free(column_string);
			ast_free(save);
			return -1;
		}
		ast_str_append(&column_string, 0, "%s%s", ast_str_strlen(column_string) ? "," : "", escaped);
		sqlite3_free(escaped);
	}
	if (!(columns = ast_strdup(ast_str_buffer(column_string)))) {
		ast_log(LOG_ERROR, "Out of memory copying columns string for table '%s.'\n", table);
		ast_free(column_string);
		ast_free(save);
		return -1;
	}
	ast_free(column_string);
	ast_free(save);

	return 0;
}

static int load_values_config(const char *tmp)
{
	char *vals = NULL, *save = NULL;
	struct values *value = NULL;
	int i;
	AST_DECLARE_APP_ARGS(val,
		AST_APP_ARG(ues)[200]; /* More than 200 columns in this CDR?  Yeah, right... */
	);

	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_WARNING, "Values not specified. Module not loaded.\n");
		return -1;
	}
	if (!(save = vals = ast_strdup(tmp))) {
		ast_log(LOG_ERROR, "Out of memory creating temporary buffer for value '%s'\n", tmp);
		return -1;
	}
	AST_STANDARD_RAW_ARGS(val, vals);
	for (i = 0; i < val.argc; i++) {
		/* Strip the single quotes off if they are there */
		char *v = ast_strip_quoted(val.ues[i], "'", "'");
		value = ast_calloc(sizeof(char), sizeof(*value) + strlen(v));
		if (!value) {
			ast_log(LOG_ERROR, "Out of memory creating entry for value '%s'\n", v);
			ast_free(save);
			return -1;
		}
		strcpy(value->expression, v); /* SAFE */
		AST_LIST_INSERT_TAIL(&sql_values, value, list);
	}
	ast_free(save);

	return 0;
}

static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *tmp;

	if ((cfg = ast_config_load(config_file, config_flags)) == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Failed to %sload configuration file. %s\n", reload ? "re" : "", reload ? "" : "Module not activated.");
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (reload) {
		free_config(1);
	}

	if (!ast_variable_browse(cfg, "master")) {
		/* Nothing configured */
		ast_config_destroy(cfg);
		return -1;
	}

	/* Mapping must have a table name */
	if (!ast_strlen_zero(tmp = ast_variable_retrieve(cfg, "master", "table"))) {
		ast_copy_string(table, tmp, sizeof(table));
	} else {
		ast_log(LOG_WARNING, "Table name not specified.  Assuming cdr.\n");
		strcpy(table, "cdr");
	}

	/* sqlite3_busy_timeout in miliseconds */
	if ((tmp = ast_variable_retrieve(cfg, "master", "busy_timeout")) != NULL) {
		if (ast_parse_arg(tmp, PARSE_INT32|PARSE_DEFAULT, &busy_timeout, 1000) != 0) {
			ast_log(LOG_WARNING, "Invalid busy_timeout value '%s' specified. Using 1000 instead.\n", tmp);
		}
	} else {
		busy_timeout = 1000;
	}

	/* Columns */
	if (load_column_config(ast_variable_retrieve(cfg, "master", "columns"))) {
		ast_config_destroy(cfg);
		free_config(0);
		return -1;
	}

	/* Values */
	if (load_values_config(ast_variable_retrieve(cfg, "master", "values"))) {
		ast_config_destroy(cfg);
		free_config(0);
		return -1;
	}

	ast_verb(4, "cdr_sqlite3_custom: Logging CDR records to table '%s' in 'master.db'\n", table);

	ast_config_destroy(cfg);

	return 0;
}

static void free_config(int reload)
{
	struct values *value;

	if (!reload && db) {
		sqlite3_close(db);
		db = NULL;
	}

	if (columns) {
		ast_free(columns);
		columns = NULL;
	}

	while ((value = AST_LIST_REMOVE_HEAD(&sql_values, list))) {
		ast_free(value);
	}
}

static int write_cdr(struct ast_cdr *cdr)
{
	int res = 0;
	char *error = NULL;
	char *sql = NULL;

	if (db == NULL) {
		/* Should not have loaded, but be failsafe. */
		return 0;
	}

	ast_mutex_lock(&lock);

	{ /* Make it obvious that only sql should be used outside of this block */
		char *escaped;
		char subst_buf[2048];
		struct values *value;
		struct ast_channel *dummy;
		struct ast_str *value_string = ast_str_create(1024);

		dummy = ast_dummy_channel_alloc();
		if (!dummy) {
			ast_log(LOG_ERROR, "Unable to allocate channel for variable subsitution.\n");
			ast_free(value_string);
			ast_mutex_unlock(&lock);
			return 0;
		}
		ast_channel_cdr_set(dummy, ast_cdr_dup(cdr));
		AST_LIST_TRAVERSE(&sql_values, value, list) {
			pbx_substitute_variables_helper(dummy, value->expression, subst_buf, sizeof(subst_buf) - 1);
			escaped = sqlite3_mprintf("%q", subst_buf);
			ast_str_append(&value_string, 0, "%s'%s'", ast_str_strlen(value_string) ? "," : "", escaped);
			sqlite3_free(escaped);
		}
		sql = sqlite3_mprintf("INSERT INTO %q (%s) VALUES (%s)", table, columns, ast_str_buffer(value_string));
		ast_debug(1, "About to log: %s\n", sql);
		ast_channel_unref(dummy);
		ast_free(value_string);
	}

	if (sqlite3_exec(db, sql, NULL, NULL, &error) != SQLITE_OK) {
		ast_log(LOG_ERROR, "%s. SQL: %s.\n", error, sql);
		sqlite3_free(error);
	}

	if (sql) {
		sqlite3_free(sql);
	}

	ast_mutex_unlock(&lock);

	return res;
}

static int unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	free_config(0);

	return 0;
}

static int load_module(void)
{
	char *error;
	char filename[PATH_MAX];
	int res;
	char *sql;

	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	/* is the database there? */
	snprintf(filename, sizeof(filename), "%s/master.db", ast_config_AST_LOG_DIR);
	res = sqlite3_open(filename, &db);
	if (res != SQLITE_OK) {
		ast_log(LOG_ERROR, "Could not open database %s.\n", filename);
		free_config(0);
		return AST_MODULE_LOAD_DECLINE;
	}
	sqlite3_busy_timeout(db, busy_timeout);
	/* is the table there? */
	sql = sqlite3_mprintf("SELECT COUNT(AcctId) FROM %q;", table);
	res = sqlite3_exec(db, sql, NULL, NULL, NULL);
	sqlite3_free(sql);
	if (res != SQLITE_OK) {
		/* We don't use %q for the column list here since we already escaped when building it */
		sql = sqlite3_mprintf("CREATE TABLE %q (AcctId INTEGER PRIMARY KEY, %s)", table, columns);
		res = sqlite3_exec(db, sql, NULL, NULL, &error);
		sqlite3_free(sql);
		if (res != SQLITE_OK) {
			ast_log(LOG_WARNING, "Unable to create table '%s': %s.\n", table, error);
			sqlite3_free(error);
			free_config(0);
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	res = ast_cdr_register(name, desc, write_cdr);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register custom SQLite3 CDR handling\n");
		free_config(0);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	int res = 0;

	ast_mutex_lock(&lock);
	res = load_config(1);
	ast_mutex_unlock(&lock);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SQLite3 Custom CDR Module",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cdr",
);
