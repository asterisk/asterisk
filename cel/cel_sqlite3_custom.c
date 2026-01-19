/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com> borrowed code from cdr,
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

/*! \file
 *
 * \brief Custom SQLite3 CEL records.
 *
 * \author Adapted by Steve Murphy <murf@digium.com> from
 *  Alejandro Rios <alejandro.rios@avatar.com.co> and
 *  Russell Bryant <russell@digium.com> from
 *  cdr_mysql_custom by Edward Eastman <ed@dm3.co.uk>,
 *	and cdr_sqlite by Holger Schurig <hs4233@mail.mn-solutions.de>
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<depend>sqlite3</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <sqlite3.h>

#include "asterisk/paths.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/options.h"
#include "asterisk/stringfields.h"

#include "custom_common.h"

#define SQLITE_BACKEND_NAME "CEL sqlite3 custom backend"

static const char config_file[] = "cel_sqlite3_custom.conf";

static sqlite3 *db = NULL;

static char table[80];
/*!
 * \bug Handling of this var is crash prone on reloads
 */
static char *columns;
static int busy_timeout;

/*
 * We only support one config for now.
 */
static struct cel_config *config = NULL;

static void free_config(void);

static int load_column_config(const char *tmp, int *column_count)
{
	char *col = NULL;
	char *cols = NULL;
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
	cols = ast_strdupa(tmp);
	*column_count = 0;
	while((col = ast_strsep_quoted(&cols, ',', '"', AST_STRSEP_ALL))) {
		escaped = sqlite3_mprintf("%q", col);
		if (!escaped) {
			ast_log(LOG_ERROR, "Out of memory creating entry for column '%s' in table '%s.'\n", col, table);
			ast_free(column_string);
			return -1;
		}
		ast_str_append(&column_string, 0, "%s%s", ast_str_strlen(column_string) ? "," : "", escaped);
		sqlite3_free(escaped);
		(*column_count)++;
	}
	if (!(columns = ast_strdup(ast_str_buffer(column_string)))) {
		ast_log(LOG_ERROR, "Out of memory copying columns string for table '%s.'\n", table);
		ast_free(column_string);
		return -1;
	}
	ast_free(column_string);

	return 0;
}

static int load_values_config(const char *tmp, int *value_count)
{
	char *field = NULL;
	char *fields = NULL;
	int res = 0;

	fields = ast_strdupa(tmp);
	*value_count = 0;

	while((field = ast_strsep_quoted(&fields, ',', '\'', AST_STRSEP_ALL))) {
		struct cel_field *cel_field = cel_field_alloc(field, cel_format_sql, "master");
		if (!cel_field) {
			continue;
		}

		res = AST_VECTOR_APPEND(&config->fields, cel_field);
		if (res != 0) {
			return -1;
		}
		(*value_count)++;
	}

	return 0;
}

static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *mappingvar;
	int column_count = 0;
	int value_count = 0;
	const char *tmp;

	if ((cfg = ast_config_load(config_file, config_flags)) == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Failed to %sload configuration file. %s\n",
			reload ? "re" : "", reload ? "" : "Module not activated.");
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (reload) {
		free_config();
	}

	if (!(mappingvar = ast_variable_browse(cfg, "master"))) {
		/* Nothing configured */
		ast_config_destroy(cfg);
		return -1;
	}

	/* Mapping must have a table name */
	if (!ast_strlen_zero(tmp = ast_variable_retrieve(cfg, "master", "table"))) {
		ast_copy_string(table, tmp, sizeof(table));
	} else {
		ast_log(LOG_WARNING, "Table name not specified.  Assuming cel.\n");
		strcpy(table, "cel");
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
	if (load_column_config(ast_variable_retrieve(cfg, "master", "columns"), &column_count)) {
		ast_config_destroy(cfg);
		free_config();
		return -1;
	}

	config = ast_calloc_with_stringfields(1, struct cel_config, 1024);
	if (!config) {
		ast_config_destroy(cfg);
		free_config();
		return -1;
	}
	config->sink_type = cel_sink_legacy;
	config->format_type = cel_format_sql;
	strcpy(config->separator, ","); /* Safe */
	strcpy(config->quote, "'"); /* Safe */
	strcpy(config->quote_escape, config->quote); /* Safe */
	config->quoting_method = cel_quoting_method_all;
	ast_mutex_init(&config->lock);

	if (AST_VECTOR_INIT(&config->fields, 20) != 0) {
		ast_config_destroy(cfg);
		free_config();
		return -1;
	}

	tmp = ast_variable_retrieve(cfg, "master", "values");
	if (!ast_strlen_zero(tmp)) {
		config->sink_type = cel_sink_legacy;
	} else {
		tmp = ast_variable_retrieve(cfg, "master", "fields");
		if (!ast_strlen_zero(tmp)) {
			config->sink_type = cel_sink_advanced;
		}
	}
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_WARNING, "Neither 'values' nor 'fields' specified. Module not loaded.\n");
		ast_config_destroy(cfg);
		free_config();
		return -1;
	}

	if (load_values_config(tmp, &value_count)) {
		ast_config_destroy(cfg);
		free_config();
		return -1;
	}

	if (value_count != column_count) {
		ast_log(LOG_WARNING, "There are %d columns but %d values. Module not loaded.\n",
			column_count, value_count);
		ast_config_destroy(cfg);
		free_config();
		return -1;
	}

	ast_verb(3, "Logging CEL records to table '%s' in 'master.db'\n", table);

	ast_config_destroy(cfg);

	return 0;
}

static void free_config(void)
{
	if (columns) {
		ast_free(columns);
		columns = NULL;
	}

	cel_free_sink(config);
}

static void write_cel(struct ast_event *event)
{
	char *error = NULL;
	char *sql = NULL;

	if (db == NULL) {
		/* Should not have loaded, but be failsafe. */
		return;
	}

	ast_mutex_lock(&config->lock);

	{ /* Make it obvious that only sql should be used outside of this block */

		int ix = 0;
		char *escaped;
		char subst_buf[2048];
		struct ast_channel *dummy = NULL;
		struct ast_str *value_string = ast_str_create(1024);

		if (config->sink_type == cel_sink_legacy) {
			dummy = ast_cel_fabricate_channel_from_event(event);
			if (!dummy) {
				ast_log(LOG_ERROR, "Unable to fabricate channel from CEL event.\n");
				ast_free(value_string);
				ast_mutex_unlock(&config->lock);
				return;
			}
		}
		for (ix = 0; ix < AST_VECTOR_SIZE(&config->fields); ix++) {
			struct cel_field *cel_field = AST_VECTOR_GET(&config->fields, ix);
			if (config->sink_type == cel_sink_legacy) {
				pbx_substitute_variables_helper(dummy, cel_field->literal_data, subst_buf, sizeof(subst_buf) - 1);
				escaped = sqlite3_mprintf("%q", subst_buf);
				ast_str_append(&value_string, 0, "%s'%s'", ast_str_strlen(value_string) ? "," : "", escaped);
				sqlite3_free(escaped);
			} else {
				cel_field->csv_field_appender(&value_string, event, config, cel_field, ix == 0);
			}
		}
		sql = sqlite3_mprintf("INSERT INTO %q (%s) VALUES (%s)", table, columns, ast_str_buffer(value_string));
		ast_debug(1, "About to log: %s\n", sql);
		if (config->sink_type == cel_sink_legacy) {
			dummy = ast_channel_unref(dummy);
		}
		ast_free(value_string);
	}

	if (sqlite3_exec(db, sql, NULL, NULL, &error) != SQLITE_OK) {
		ast_log(LOG_ERROR, "%s. SQL: %s.\n", error, sql);
		sqlite3_free(error);
	}

	if (sql) {
		sqlite3_free(sql);
	}
	ast_mutex_unlock(&config->lock);

	return;
}

static int unload_module(void)
{
	ast_cel_backend_unregister(SQLITE_BACKEND_NAME);

	if (db) {
		sqlite3_close(db);
		db = NULL;
	}

	free_config();

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
		free_config();
		return AST_MODULE_LOAD_DECLINE;
	}
	sqlite3_busy_timeout(db, busy_timeout);
	/* is the table there? */
	sql = sqlite3_mprintf("SELECT COUNT(*) FROM %q;", table);
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
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	if (ast_cel_backend_register(SQLITE_BACKEND_NAME, write_cel)) {
		ast_log(LOG_ERROR, "Unable to register custom SQLite3 CEL handling\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	int res = 0;

	ast_mutex_lock(&config->lock);
	res = load_config(1);
	ast_mutex_unlock(&config->lock);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SQLite3 Custom CEL Module",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cel",
);
