/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>  - Asterisk Author
 * Matthew Boehm <mboehm@cytelcom.com> - MySQL RealTime Driver Author
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
 * \brief MySQL CDR backend
 */

/*** MODULEINFO
	<depend>mysqlclient</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include <mysql/mysql.h>
#include <mysql/mysql_version.h>
#include <mysql/errmsg.h>

#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"

#define RES_CONFIG_MYSQL_CONF "res_config_mysql.conf"
#define RES_CONFIG_MYSQL_CONF_OLD "res_mysql.conf"
#define	READHANDLE	0
#define	WRITEHANDLE	1

#define ESCAPE_STRING(buf, var) \
	do { \
		struct ast_str *semi = ast_str_thread_get(&scratch2_buf, strlen(var) * 3 + 1); \
		const char *chunk = var; \
		ast_str_reset(semi); \
		for (; *chunk; chunk++) { \
			if (strchr(";^", *chunk)) { \
				ast_str_append(&semi, 0, "^%02hhX", *chunk); \
			} else { \
				ast_str_append(&semi, 0, "%c", *chunk); \
			} \
		} \
		if (ast_str_strlen(semi) * 2 + 1 > ast_str_size(buf)) { \
			ast_str_make_space(&(buf), ast_str_strlen(semi) * 2 + 1); \
		} \
		mysql_real_escape_string(&dbh->handle, ast_str_buffer(buf), ast_str_buffer(semi), ast_str_strlen(semi)); \
	} while (0)

AST_THREADSTORAGE(sql_buf);
AST_THREADSTORAGE(sql2_buf);
AST_THREADSTORAGE(find_buf);
AST_THREADSTORAGE(scratch_buf);
AST_THREADSTORAGE(scratch2_buf);
AST_THREADSTORAGE(modify_buf);
AST_THREADSTORAGE(modify2_buf);
AST_THREADSTORAGE(modify3_buf);

enum requirements { RQ_WARN, RQ_CREATECLOSE, RQ_CREATECHAR };

struct mysql_conn {
	AST_RWLIST_ENTRY(mysql_conn) list;
	ast_mutex_t	lock;
	MYSQL       handle;
	char        host[50];
	char        name[50];
	char        user[50];
	char        pass[50];
	char        sock[50];
	char        charset[50];
	int         port;
	int         connected;
	time_t      connect_time;
	enum requirements requirements;
	char        unique_name[0];
};

struct columns {
	char *name;
	char *type;
	char *dflt;
	char null;
	int len;
	AST_LIST_ENTRY(columns) list;
};

struct tables {
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(mysql_columns, columns) columns;
	AST_LIST_ENTRY(tables) list;
	struct mysql_conn *database;
	char name[0];
};

static AST_LIST_HEAD_STATIC(mysql_tables, tables);
static AST_RWLIST_HEAD_STATIC(databases, mysql_conn);

static int parse_config(int reload);
static int mysql_reconnect(struct mysql_conn *conn);
static char *handle_cli_realtime_mysql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_realtime_mysql_cache(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static int load_mysql_config(struct ast_config *config, const char *category, struct mysql_conn *conn);
static int require_mysql(const char *database, const char *tablename, va_list ap);
static int internal_require(const char *database, const char *table, ...) attribute_sentinel;

static struct ast_cli_entry cli_realtime_mysql_status[] = {
	AST_CLI_DEFINE(handle_cli_realtime_mysql_status, "Shows connection information for the MySQL RealTime driver"),
	AST_CLI_DEFINE(handle_cli_realtime_mysql_cache, "Shows cached tables within the MySQL realtime driver"),
};

static struct mysql_conn *find_database(const char *database, int for_write)
{
	char *whichdb;
	const char *ptr;
	struct mysql_conn *cur;

	if ((ptr = strchr(database, '/'))) {
		/* Multiple databases encoded within string */
		if (for_write) {
			whichdb = ast_strdupa(ptr + 1);
		} else {
			whichdb = ast_alloca(ptr - database + 1);
			strncpy(whichdb, database, ptr - database);
			whichdb[ptr - database] = '\0';
		}
	} else {
		whichdb = ast_strdupa(database);
	}

	AST_RWLIST_RDLOCK(&databases);
	AST_RWLIST_TRAVERSE(&databases, cur, list) {
		if (!strcmp(cur->unique_name, whichdb)) {
			ast_mutex_lock(&cur->lock);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&databases);
	return cur;
}

#define release_database(a)	ast_mutex_unlock(&(a)->lock)

static int internal_require(const char *database, const char *table, ...)
{
	va_list ap;
	int res;
	va_start(ap, table);
	res = require_mysql(database, table, ap);
	va_end(ap);
	return res;
}

static void destroy_table(struct tables *table)
{
	struct columns *column;
	ast_mutex_lock(&table->lock);
	while ((column = AST_LIST_REMOVE_HEAD(&table->columns, list))) {
		ast_free(column);
	}
	ast_mutex_unlock(&table->lock);
	ast_mutex_destroy(&table->lock);
	ast_free(table);
}

static struct tables *find_table(const char *database, const char *tablename)
{
	struct columns *column;
	struct tables *table;
	struct ast_str *sql = ast_str_thread_get(&find_buf, 30);
	char *fname, *ftype, *flen, *fdflt, *fnull;
	struct mysql_conn *dbh;
	MYSQL_RES *result;
	MYSQL_ROW row;

	if (!(dbh = find_database(database, 1))) {
		return NULL;
	}

	AST_LIST_LOCK(&mysql_tables);
	AST_LIST_TRAVERSE(&mysql_tables, table, list) {
		if (!strcasecmp(table->name, tablename)) {
			ast_mutex_lock(&table->lock);
			AST_LIST_UNLOCK(&mysql_tables);
			release_database(dbh);
			return table;
		}
	}

	/* Not found, scan the table */
	ast_str_set(&sql, 0, "DESC %s", tablename);

	if (!mysql_reconnect(dbh)) {
		release_database(dbh);
		AST_LIST_UNLOCK(&mysql_tables);
		return NULL;
	}

	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_ERROR, "Failed to query database '%s', table '%s' columns: %s\n", database, tablename, mysql_error(&dbh->handle));
		release_database(dbh);
		AST_LIST_UNLOCK(&mysql_tables);
		return NULL;
	}

	if (!(table = ast_calloc(1, sizeof(*table) + strlen(tablename) + 1))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for new table structure\n");
		release_database(dbh);
		AST_LIST_UNLOCK(&mysql_tables);
		return NULL;
	}
	strcpy(table->name, tablename); /* SAFE */
	table->database = dbh;
	ast_mutex_init(&table->lock);
	AST_LIST_HEAD_INIT_NOLOCK(&table->columns);

	if ((result = mysql_store_result(&dbh->handle))) {
		while ((row = mysql_fetch_row(result))) {
			fname = row[0];
			ftype = row[1];
			fnull = row[2];
			fdflt = row[4];
			ast_verb(4, "Found column '%s' of type '%s'\n", fname, ftype);

			if (fdflt == NULL) {
				fdflt = "";
			}

			if (!(column = ast_calloc(1, sizeof(*column) + strlen(fname) + strlen(ftype) + strlen(fdflt) + 3))) {
				ast_log(LOG_ERROR, "Unable to allocate column element %s for %s\n", fname, tablename);
				destroy_table(table);
				release_database(dbh);
				AST_LIST_UNLOCK(&mysql_tables);
				return NULL;
			}

			if ((flen = strchr(ftype, '('))) {
				sscanf(flen, "(%30d)", &column->len);
			} else {
				/* Columns like dates, times, and timestamps don't have a length */
				column->len = -1;
			}

			column->name = (char *)column + sizeof(*column);
			column->type = (char *)column + sizeof(*column) + strlen(fname) + 1;
			column->dflt = (char *)column + sizeof(*column) + strlen(fname) + 1 + strlen(ftype) + 1;
			strcpy(column->name, fname);
			strcpy(column->type, ftype);
			strcpy(column->dflt, fdflt);
			column->null = (strcmp(fnull, "YES") == 0 ? 1 : 0);
			AST_LIST_INSERT_TAIL(&table->columns, column, list);
		}
		mysql_free_result(result);
	}

	AST_LIST_INSERT_TAIL(&mysql_tables, table, list);
	ast_mutex_lock(&table->lock);
	AST_LIST_UNLOCK(&mysql_tables);
	release_database(dbh);
	return table;
}

static void release_table(struct tables *table)
{
	if (table) {
		ast_mutex_unlock(&table->lock);
	}
}

static struct columns *find_column(struct tables *table, const char *colname)
{
	struct columns *column;

	AST_LIST_TRAVERSE(&table->columns, column, list) {
		if (strcmp(column->name, colname) == 0) {
			break;
		}
	}

	return column;
}

static char *decode_chunk(char *chunk)
{
	char *orig = chunk;
	for (; *chunk; chunk++) {
		if (*chunk == '^' && strchr("0123456789ABCDEFabcdef", chunk[1]) && strchr("0123456789ABCDEFabcdef", chunk[2])) {
			sscanf(chunk + 1, "%02hhX", chunk);
			memmove(chunk + 1, chunk + 3, strlen(chunk + 3) + 1);
		}
	}
	return orig;
}

static struct ast_variable *realtime_mysql(const char *database, const char *table, va_list ap)
{
	struct mysql_conn *dbh;
	MYSQL_RES *result;
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	int numFields, i;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 16);
	struct ast_str *buf = ast_str_thread_get(&scratch_buf, 16);
	char *stringp;
	char *chunk;
	char *op;
	const char *newparam, *newval;
	struct ast_variable *var=NULL, *prev=NULL;
	va_list aq;

	if (!(dbh = find_database(database, 0))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Invalid database specified: %s (check res_mysql.conf)\n", database);
		return NULL;
	}

	if (!table) {
		ast_log(LOG_WARNING, "MySQL RealTime: No table specified.\n");
		release_database(dbh);
		return NULL;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	va_copy(aq, ap);
	if (!(newparam = va_arg(aq, const char *)) || !(newval = va_arg(aq, const char *)))  {
		ast_log(LOG_WARNING, "MySQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		va_end(aq);
		release_database(dbh);
		return NULL;
	}

	/* Must connect to the server before anything else, as the escape function requires the mysql handle. */
	if (!mysql_reconnect(dbh)) {
		va_end(aq);
		release_database(dbh);
		return NULL;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	if (!strchr(newparam, ' ')) 
		op = " ="; 
	else 
		op = "";

	ESCAPE_STRING(buf, newval);
	ast_str_set(&sql, 0, "SELECT * FROM %s WHERE %s%s '%s'", table, newparam, op, ast_str_buffer(buf));
	while ((newparam = va_arg(aq, const char *))) {
		newval = va_arg(aq, const char *);
		if (!strchr(newparam, ' ')) 
			op = " ="; 
		else
			op = "";
		ESCAPE_STRING(buf, newval);
		ast_str_append(&sql, 0, " AND %s%s '%s'", newparam, op, ast_str_buffer(buf));
	}

	ast_debug(1, "MySQL RealTime: Retrieve SQL: %s\n", ast_str_buffer(sql));

	/* Execution. */
	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to query database: %s\n", mysql_error(&dbh->handle));
		va_end(aq);
		release_database(dbh);
		return NULL;
	}

	if ((result = mysql_store_result(&dbh->handle))) {
		numFields = mysql_num_fields(result);
		fields = mysql_fetch_fields(result);

		while ((row = mysql_fetch_row(result))) {
			for (i = 0; i < numFields; i++) {
				/* Encode NULL values separately from blank values, for the Realtime API */
				if (row[i] == NULL) {
					row[i] = "";
				} else if (ast_strlen_zero(row[i])) {
					row[i] = " ";
				}
				for (stringp = row[i], chunk = strsep(&stringp, ";"); chunk; chunk = strsep(&stringp, ";")) {
					if (prev) {
						if ((prev->next = ast_variable_new(fields[i].name, decode_chunk(chunk), ""))) {
							prev = prev->next;
						}
					} else {
						prev = var = ast_variable_new(fields[i].name, decode_chunk(chunk), "");
					}
				}
			}
		}
	} else {
		ast_debug(1, "MySQL RealTime: Could not find any rows in table %s.\n", table);
	}

	va_end(aq);
	release_database(dbh);
	mysql_free_result(result);

	return var;
}

static struct ast_config *realtime_multi_mysql(const char *database, const char *table, va_list ap)
{
	struct mysql_conn *dbh;
	MYSQL_RES *result;
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	int numFields, i;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 16);
	struct ast_str *buf = ast_str_thread_get(&scratch_buf, 16);
	const char *initfield = NULL;
	char *stringp;
	char *chunk;
	char *op;
	const char *newparam, *newval;
	struct ast_variable *var = NULL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;
	va_list aq;

	if (!(dbh = find_database(database, 0))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Invalid database specified: '%s' (check res_mysql.conf)\n", database);
		return NULL;
	}

	if (!table) {
		ast_log(LOG_WARNING, "MySQL RealTime: No table specified.\n");
		release_database(dbh);
		return NULL;
	}
	
	if (!(cfg = ast_config_new())) {
		/* If I can't alloc memory at this point, why bother doing anything else? */
		ast_log(LOG_WARNING, "Out of memory!\n");
		release_database(dbh);
		return NULL;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	va_copy(aq, ap);
	if (!(newparam = va_arg(aq, const char *)) || !(newval = va_arg(aq, const char *)))  {
		ast_log(LOG_WARNING, "MySQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		va_end(aq);
		ast_config_destroy(cfg);
		release_database(dbh);
		return NULL;
	}

	initfield = ast_strdupa(newparam);
	if ((op = strchr(initfield, ' '))) {
		*op = '\0';
	}

	/* Must connect to the server before anything else, as the escape function requires the mysql handle. */
	if (!mysql_reconnect(dbh)) {
		va_end(aq);
		release_database(dbh);
		ast_config_destroy(cfg);
		return NULL;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	if (!strchr(newparam, ' '))
		op = " =";
	else
		op = "";

	ESCAPE_STRING(buf, newval);
	ast_str_set(&sql, 0, "SELECT * FROM %s WHERE %s%s '%s'", table, newparam, op, ast_str_buffer(buf));
	while ((newparam = va_arg(aq, const char *))) {
		newval = va_arg(aq, const char *);
		if (!strchr(newparam, ' ')) op = " ="; else op = "";
		ESCAPE_STRING(buf, newval);
		ast_str_append(&sql, 0, " AND %s%s '%s'", newparam, op, ast_str_buffer(buf));
	}

	if (initfield) {
		ast_str_append(&sql, 0, " ORDER BY %s", initfield);
	}

	ast_debug(1, "MySQL RealTime: Retrieve SQL: %s\n", ast_str_buffer(sql));

	/* Execution. */
	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to query database: %s\n", mysql_error(&dbh->handle));
		va_end(aq);
		release_database(dbh);
		ast_config_destroy(cfg);
		return NULL;
	}

	if ((result = mysql_store_result(&dbh->handle))) {
		numFields = mysql_num_fields(result);
		fields = mysql_fetch_fields(result);

		while ((row = mysql_fetch_row(result))) {
			var = NULL;
			cat = ast_category_new("", "", -1);
			if (!cat) {
				ast_log(LOG_WARNING, "Out of memory!\n");
				continue;
			}
			for (i = 0; i < numFields; i++) {
				if (ast_strlen_zero(row[i]))
					continue;
				for (stringp = row[i], chunk = strsep(&stringp, ";"); chunk; chunk = strsep(&stringp, ";")) {
					if (chunk && !ast_strlen_zero(decode_chunk(ast_strip(chunk)))) {
						if (initfield && !strcmp(initfield, fields[i].name)) {
							ast_category_rename(cat, chunk);
						}
						var = ast_variable_new(fields[i].name, chunk, "");
						ast_variable_append(cat, var);
					}
				}
			}
			ast_category_append(cfg, cat);
		}
	} else {
		ast_debug(1, "MySQL RealTime: Could not find any rows in table %s.\n", table);
	}

	va_end(aq);
	release_database(dbh);
	mysql_free_result(result);

	return cfg;
}

static int update_mysql(const char *database, const char *tablename, const char *keyfield, const char *lookup, va_list ap)
{
	struct mysql_conn *dbh;
	my_ulonglong numrows;
	const char *newparam, *newval;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 100), *buf = ast_str_thread_get(&scratch_buf, 100);
	struct tables *table;
	struct columns *column = NULL;
	va_list aq;

	if (!(dbh = find_database(database, 1))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Invalid database specified: '%s' (check res_mysql.conf)\n", database);
		return -1;
	}

	if (!tablename) {
		ast_log(LOG_WARNING, "MySQL RealTime: No table specified.\n");
		release_database(dbh);
		return -1;
	}

	if (!(table = find_table(database, tablename))) {
		ast_log(LOG_ERROR, "Table '%s' does not exist!!\n", tablename);
		release_database(dbh);
		return -1;
	}

	if (!(column = find_column(table, keyfield))) {
		ast_log(LOG_ERROR, "MySQL RealTime: Updating on column '%s', but that column does not exist within the table '%s' (db '%s')!\n", keyfield, tablename, database);
		release_table(table);
		release_database(dbh);
		return -1;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	va_copy(aq, ap);
	if (!(newparam = va_arg(aq, const char *)) || !(newval = va_arg(aq, const char *)))  {
		ast_log(LOG_WARNING, "MySQL RealTime: Realtime update requires at least 1 parameter and 1 value to update.\n");
		va_end(aq);
		release_table(table);
		release_database(dbh);
		return -1;
	}

	/* Check that the column exists in the table */
	if (!(column = find_column(table, newparam))) {
		ast_log(LOG_ERROR, "MySQL RealTime: Updating column '%s', but that column does not exist within the table '%s' (first pair MUST exist)!\n", newparam, tablename);
		va_end(aq);
		release_table(table);
		release_database(dbh);
		return -1;
	}

	/* Must connect to the server before anything else, as the escape function requires the mysql handle. */
	if (!mysql_reconnect(dbh)) {
		va_end(aq);
		release_table(table);
		release_database(dbh);
		return -1;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	ESCAPE_STRING(buf, newval);
	ast_str_set(&sql, 0, "UPDATE %s SET `%s` = '%s'", tablename, newparam, ast_str_buffer(buf));

	/* If the column length isn't long enough, give a chance to lengthen it. */
	if (strncmp(column->type, "char", 4) == 0 || strncmp(column->type, "varchar", 7) == 0) {
		internal_require(database, tablename, newparam, RQ_CHAR, ast_str_strlen(buf), SENTINEL);
	}

	while ((newparam = va_arg(aq, const char *))) {
		newval = va_arg(aq, const char *);

		/* If the column is not within the table, then skip it */
		if (!(column = find_column(table, newparam))) {
			ast_log(LOG_WARNING, "Attempted to update column '%s' in table '%s', but column does not exist!\n", newparam, tablename);
			continue;
		}

		ESCAPE_STRING(buf, newval);
		ast_str_append(&sql, 0, ", `%s` = '%s'", newparam, ast_str_buffer(buf));

		/* If the column length isn't long enough, give a chance to lengthen it. */
		if (strncmp(column->type, "char", 4) == 0 || strncmp(column->type, "varchar", 7) == 0) {
			internal_require(database, tablename, newparam, RQ_CHAR, ast_str_strlen(buf), SENTINEL);
		}
	}

	va_end(aq);

	ESCAPE_STRING(buf, lookup);
	ast_str_append(&sql, 0, " WHERE `%s` = '%s'", keyfield, ast_str_buffer(buf));

	ast_debug(1, "MySQL RealTime: Update SQL: %s\n", ast_str_buffer(sql));

	/* Execution. */
	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to update database: %s\n", mysql_error(&dbh->handle));
		release_table(table);
		release_database(dbh);
		return -1;
	}

	numrows = mysql_affected_rows(&dbh->handle);
	release_table(table);
	release_database(dbh);

	ast_debug(1, "MySQL RealTime: Updated %llu rows on table: %s\n", numrows, tablename);

	/* From http://dev.mysql.com/doc/mysql/en/mysql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	*/

	return (int)numrows;
}

static int update2_mysql(const char *database, const char *tablename, va_list ap)
{
	struct mysql_conn *dbh;
	my_ulonglong numrows;
	int first;
	const char *newparam, *newval;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 100), *buf = ast_str_thread_get(&scratch_buf, 100);
	struct ast_str *where = ast_str_thread_get(&sql2_buf, 100);
	struct tables *table;
	struct columns *column = NULL;
	va_list aq;

	if (!tablename) {
		ast_log(LOG_WARNING, "MySQL RealTime: No table specified.\n");
		return -1;
	}

	if (!(dbh = find_database(database, 1))) {
		ast_log(LOG_ERROR, "Invalid database specified: %s\n", database);
		return -1;
	}

	if (!(table = find_table(database, tablename))) {
		ast_log(LOG_ERROR, "Table '%s' does not exist!!\n", tablename);
		release_database(dbh);
		return -1;
	}

	if (!sql || !buf || !where) {
		release_database(dbh);
		release_table(table);
		return -1;
	}

	ast_str_set(&sql, 0, "UPDATE %s SET", tablename);
	ast_str_set(&where, 0, "WHERE");

	/* Must connect to the server before anything else, as the escape function requires the mysql handle. */
	if (!mysql_reconnect(dbh)) {
		release_table(table);
		release_database(dbh);
		return -1;
	}

	first = 1;
	va_copy(aq, ap);
	while ((newparam = va_arg(aq, const char *))) {
		if (!(column = find_column(table, newparam))) {
			ast_log(LOG_ERROR, "Updating on column '%s', but that column does not exist within the table '%s'!\n", newparam, tablename);
			va_end(aq);
			release_table(table);
			release_database(dbh);
			return -1;
		}
		if (!(newval = va_arg(aq, const char *))) {
			ast_log(LOG_ERROR, "Invalid arguments: no value specified for column '%s' on '%s@%s'\n", newparam, tablename, database);
			va_end(aq);
			release_table(table);
			release_database(dbh);
			return -1;
		}
		ESCAPE_STRING(buf, newval);
		ast_str_append(&where, 0, "%s `%s` = '%s'", first ? "" : " AND", newparam, ast_str_buffer(buf));
		first = 0;

		/* If the column length isn't long enough, give a chance to lengthen it. */
		if (strncmp(column->type, "char", 4) == 0 || strncmp(column->type, "varchar", 7) == 0) {
			internal_require(database, tablename, newparam, RQ_CHAR, ast_str_strlen(buf), SENTINEL);
		}
	}

	first = 1;
	while ((newparam = va_arg(aq, const char *))) {
		if (!(newval = va_arg(aq, const char *))) {
			ast_log(LOG_ERROR, "Invalid arguments: no value specified for column '%s' on '%s@%s'\n", newparam, tablename, database);
			va_end(aq);
			release_table(table);
			release_database(dbh);
			return -1;
		}

		/* If the column is not within the table, then skip it */
		if (!(column = find_column(table, newparam))) {
			ast_log(LOG_WARNING, "Attempted to update column '%s' in table '%s', but column does not exist!\n", newparam, tablename);
			continue;
		}

		ESCAPE_STRING(buf, newval);
		ast_str_append(&sql, 0, "%s `%s` = '%s'", first ? "" : ",", newparam, ast_str_buffer(buf));
		first = 0;

		/* If the column length isn't long enough, give a chance to lengthen it. */
		if (strncmp(column->type, "char", 4) == 0 || strncmp(column->type, "varchar", 7) == 0) {
			internal_require(database, tablename, newparam, RQ_CHAR, ast_str_strlen(buf), SENTINEL);
		}
	}

	va_end(aq);

	release_table(table);

	ast_str_append(&sql, 0, " %s", ast_str_buffer(where));

	ast_debug(1, "MySQL RealTime: Update SQL: %s\n", ast_str_buffer(sql));

	/* Execution. */
	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to update database: %s\n", mysql_error(&dbh->handle));
		release_table(table);
		release_database(dbh);
		return -1;
	}

	numrows = mysql_affected_rows(&dbh->handle);
	release_database(dbh);

	ast_debug(1, "MySQL RealTime: Updated %llu rows on table: %s\n", numrows, tablename);

	/* From http://dev.mysql.com/doc/mysql/en/mysql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	*/

	return (int)numrows;
}
 
static int store_mysql(const char *database, const char *table, va_list ap)
{
	struct mysql_conn *dbh;
	my_ulonglong insertid;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 16);
	struct ast_str *sql2 = ast_str_thread_get(&sql2_buf, 16);
	struct ast_str *buf = ast_str_thread_get(&scratch_buf, 16);
	const char *newparam, *newval;
	va_list aq;

	if (!(dbh = find_database(database, 1))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Invalid database specified: '%s' (check res_mysql.conf)\n", database);
		return -1;
	}

	if (!table) {
		ast_log(LOG_WARNING, "MySQL RealTime: No table specified.\n");
		release_database(dbh);
		return -1;
	}
	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	va_copy(aq, ap);
	if (!(newparam = va_arg(aq, const char *)) || !(newval = va_arg(aq, const char *))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Realtime storage requires at least 1 parameter and 1 value to search on.\n");
		va_end(aq);
		release_database(dbh);
		return -1;
	}
	/* Must connect to the server before anything else, as the escape function requires the mysql handle. */
	if (!mysql_reconnect(dbh)) {
		va_end(aq);
		release_database(dbh);
		return -1;
	}
	/* Create the first part of the query using the first parameter/value pairs we just extracted
		If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */
	ESCAPE_STRING(buf, newval);
	ast_str_set(&sql, 0, "INSERT INTO %s (`%s`", table, newparam);
	ast_str_set(&sql2, 0, ") VALUES ('%s'", ast_str_buffer(buf));

	internal_require(database, table, newparam, RQ_CHAR, ast_str_strlen(buf), SENTINEL);

	while ((newparam = va_arg(aq, const char *))) {
		if ((newval = va_arg(aq, const char *))) {
			ESCAPE_STRING(buf, newval);
		} else {
			ast_str_reset(buf);
		}
		if (internal_require(database, table, newparam, RQ_CHAR, ast_str_strlen(buf), SENTINEL) == 0) {
			ast_str_append(&sql, 0, ", `%s`", newparam);
			ast_str_append(&sql2, 0, ", '%s'", ast_str_buffer(buf));
		}
	}
	va_end(aq);
	ast_str_append(&sql, 0, "%s)", ast_str_buffer(sql2));
	ast_debug(1,"MySQL RealTime: Insert SQL: %s\n", ast_str_buffer(sql));

	/* Execution. */
	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to insert into database: %s\n", mysql_error(&dbh->handle));
		release_database(dbh);
		return -1;
	}

	/*!\note The return value is non-portable and may change in future versions. */
	insertid = mysql_insert_id(&dbh->handle);
	release_database(dbh);

	ast_debug(1, "MySQL RealTime: row inserted on table: %s, id: %llu\n", table, insertid);

	/* From http://dev.mysql.com/doc/mysql/en/mysql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	*/
	return (int)insertid;
}

static int destroy_mysql(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
{
	struct mysql_conn *dbh;
	my_ulonglong numrows;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 16);
	struct ast_str *buf = ast_str_thread_get(&scratch_buf, 16);
	const char *newparam, *newval;
	va_list aq;

	if (!(dbh = find_database(database, 1))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Invalid database specified: '%s' (check res_mysql.conf)\n", database);
		return -1;
	}

	if (!table) {
		ast_log(LOG_WARNING, "MySQL RealTime: No table specified.\n");
		release_database(dbh);
		return -1;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	/* newparam = va_arg(aq, const char *);
	newval = va_arg(aq, const char *);*/
	if (ast_strlen_zero(keyfield) || ast_strlen_zero(lookup))  {
		ast_log(LOG_WARNING, "MySQL RealTime: Realtime destroying requires at least 1 parameter and 1 value to search on.\n");
		release_database(dbh);
		return -1;
	}

	/* Must connect to the server before anything else, as the escape function requires the mysql handle. */
	if (!mysql_reconnect(dbh)) {
		release_database(dbh);
		return -1;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */
	ESCAPE_STRING(buf, lookup);
	ast_str_set(&sql, 0, "DELETE FROM %s WHERE `%s` = '%s'", table, keyfield, ast_str_buffer(buf));
	va_copy(aq, ap);
	while ((newparam = va_arg(aq, const char *))) {
		newval = va_arg(aq, const char *);
		ESCAPE_STRING(buf, newval);
		ast_str_append(&sql, 0, " AND `%s` = '%s'", newparam, ast_str_buffer(buf));
	}
	va_end(aq);

	ast_debug(1, "MySQL RealTime: Delete SQL: %s\n", ast_str_buffer(sql));

	/* Execution. */
	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to delete from database: %s\n", mysql_error(&dbh->handle));
		release_database(dbh);
		return -1;
	}

	numrows = mysql_affected_rows(&dbh->handle);
	release_database(dbh);

	ast_debug(1, "MySQL RealTime: Deleted %llu rows on table: %s\n", numrows, table);

	/* From http://dev.mysql.com/doc/mysql/en/mysql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	*/

	return (int)numrows;
}
 
static struct ast_config *config_mysql(const char *database, const char *table, const char *file, struct ast_config *cfg, struct ast_flags config_flags, const char *unused, const char *who_asked)
{
	struct mysql_conn *dbh;
	MYSQL_RES *result;
	MYSQL_ROW row;
	my_ulonglong num_rows;
	struct ast_variable *new_v;
	struct ast_category *cur_cat = NULL;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 200);
	char last[80] = "";
	int last_cat_metric = 0;

	ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);

	if (!file || !strcmp(file, RES_CONFIG_MYSQL_CONF)) {
		ast_log(LOG_WARNING, "MySQL RealTime: Cannot configure myself.\n");
		return NULL;
	}

	if (!(dbh = find_database(database, 0))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Invalid database specified: '%s' (check res_mysql.conf)\n", database);
		return NULL;
	}

	ast_str_set(&sql, 0, "SELECT category, var_name, var_val, cat_metric FROM %s WHERE filename='%s' and commented=0 ORDER BY filename, category, cat_metric desc, var_metric asc, var_name, var_val, id", table, file);

	ast_debug(1, "MySQL RealTime: Static SQL: %s\n", ast_str_buffer(sql));

	/* We now have our complete statement; Lets connect to the server and execute it. */
	if (!mysql_reconnect(dbh)) {
		return NULL;
	}

	if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
		ast_log(LOG_WARNING, "MySQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "MySQL RealTime: Query: %s\n", ast_str_buffer(sql));
		ast_debug(1, "MySQL RealTime: Query Failed because: %s\n", mysql_error(&dbh->handle));
		release_database(dbh);
		return NULL;
	}

	if ((result = mysql_store_result(&dbh->handle))) {
		num_rows = mysql_num_rows(result);
		ast_debug(1, "MySQL RealTime: Found %llu rows.\n", num_rows);

		/* There might exist a better way to access the column names other than counting,
		 * but I believe that would require another loop that we don't need. */

		while ((row = mysql_fetch_row(result))) {
			if (!strcmp(row[1], "#include")) {
				if (!ast_config_internal_load(row[2], cfg, config_flags, "", who_asked)) {
					mysql_free_result(result);
					release_database(dbh);
					return NULL;
				}
				continue;
			}

			if (strcmp(last, row[0]) || last_cat_metric != atoi(row[3])) {
				if (!(cur_cat = ast_category_new(row[0], "", -1))) {
					ast_log(LOG_WARNING, "Out of memory!\n");
					break;
				}
				strcpy(last, row[0]);
				last_cat_metric = atoi(row[3]);
				ast_category_append(cfg, cur_cat);
			}
			new_v = ast_variable_new(row[1], row[2], "");
			if (cur_cat)
				ast_variable_append(cur_cat, new_v);
		}
	} else {
		ast_log(LOG_WARNING, "MySQL RealTime: Could not find config '%s' in database.\n", file);
	}

	mysql_free_result(result);
	release_database(dbh);

	return cfg;
}

static int unload_mysql(const char *database, const char *tablename)
{
	struct tables *cur;
	AST_LIST_LOCK(&mysql_tables);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&mysql_tables, cur, list) {
		if (strcmp(cur->name, tablename) == 0) {
			AST_LIST_REMOVE_CURRENT(list);
			destroy_table(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&mysql_tables);
	return cur ? 0 : -1;
}

static int modify_mysql(const char *database, const char *tablename, struct columns *column, require_type type, int len)
{
	/*!\note Cannot use ANY of the same scratch space as is used in other functions, as this one is interspersed. */
	struct ast_str *sql = ast_str_thread_get(&modify_buf, 100), *escbuf = ast_str_thread_get(&modify2_buf, 100);
	struct ast_str *typestr = ast_str_thread_get(&modify3_buf, 30);
	int waschar = strncasecmp(column->type, "char", 4) == 0 ? 1 : 0;
	int wasvarchar = strncasecmp(column->type, "varchar", 7) == 0 ? 1 : 0;
	int res = 0;
	struct mysql_conn *dbh;

	if (!(dbh = find_database(database, 1))) {
		return -1;
	}

	do {
		if (type == RQ_CHAR || waschar || wasvarchar) {
			if (wasvarchar) {
				ast_str_set(&typestr, 0, "VARCHAR(%d)", len);
			} else {
				ast_str_set(&typestr, 0, "CHAR(%d)", len);
			}
		} else if (type == RQ_UINTEGER1) {
			ast_str_set(&typestr, 0, "tinyint(3) unsigned");
		} else if (type == RQ_INTEGER1) {
			ast_str_set(&typestr, 0, "tinyint(4)");
		} else if (type == RQ_UINTEGER2) {
			ast_str_set(&typestr, 0, "smallint(5) unsigned");
		} else if (type == RQ_INTEGER2) {
			ast_str_set(&typestr, 0, "smallint(6)");
		} else if (type == RQ_UINTEGER3) {
			ast_str_set(&typestr, 0, "mediumint(8) unsigned");
		} else if (type == RQ_INTEGER3) {
			ast_str_set(&typestr, 0, "mediumint(8)");
		} else if (type == RQ_UINTEGER4) {
			ast_str_set(&typestr, 0, "int(10) unsigned");
		} else if (type == RQ_INTEGER4) {
			ast_str_set(&typestr, 0, "int(11)");
		} else if (type == RQ_UINTEGER8) {
			ast_str_set(&typestr, 0, "bigint(19) unsigned");
		} else if (type == RQ_INTEGER8) {
			ast_str_set(&typestr, 0, "bigint(20)");
		} else if (type == RQ_DATETIME) {
			ast_str_set(&typestr, 0, "datetime");
		} else if (type == RQ_DATE) {
			ast_str_set(&typestr, 0, "date");
		} else if (type == RQ_FLOAT) {
			ast_str_set(&typestr, 0, "FLOAT(%d,2)", len);
		} else {
			ast_log(LOG_ERROR, "Unknown type (should NEVER happen)\n");
			res = -1;
			break;
		}
		ast_str_set(&sql, 0, "ALTER TABLE %s MODIFY `%s` %s", tablename, column->name, ast_str_buffer(typestr));
		if (!column->null) {
			ast_str_append(&sql, 0, " NOT NULL");
		}
		if (!ast_strlen_zero(column->dflt)) {
			ESCAPE_STRING(escbuf, column->dflt);
			ast_str_append(&sql, 0, " DEFAULT '%s'", ast_str_buffer(escbuf));
		}

		if (!mysql_reconnect(dbh)) {
			ast_log(LOG_ERROR, "Unable to add column: %s\n", ast_str_buffer(sql));
			res = -1;
			break;
		}

		/* Execution. */
		if (mysql_real_query(&dbh->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
			ast_log(LOG_WARNING, "MySQL RealTime: Failed to modify database: %s\n", mysql_error(&dbh->handle));
			ast_debug(1, "MySQL RealTime: Query: %s\n", ast_str_buffer(sql));
			res = -1;
		}
	} while (0);

	release_database(dbh);
	return res;
}

#define PICK_WHICH_ALTER_ACTION(stringtype) \
	if (table->database->requirements == RQ_WARN) {                                                                       \
		ast_log(LOG_WARNING, "Realtime table %s@%s: column '%s' may not be large enough for "            \
			"the required data length: %d (detected stringtype)\n",                                      \
			tablename, database, column->name, size);                                                    \
		res = -1;                                                                                        \
	} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {     \
		table_altered = 1;                                                                               \
	} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {   \
		table_altered = 1;                                                                               \
	} else {                                                                                             \
		res = -1;                                                                                        \
	}

static int require_mysql(const char *database, const char *tablename, va_list ap)
{
	struct columns *column;
	struct tables *table = find_table(database, tablename);
	char *elm;
	int type, size, res = 0, table_altered = 0;
	va_list aq;

	if (!table) {
		ast_log(LOG_WARNING, "Table %s not found in database.  This table should exist if you're using realtime.\n", tablename);
		return -1;
	}

	va_copy(aq, ap);
	while ((elm = va_arg(aq, char *))) {
		type = va_arg(aq, require_type);
		size = va_arg(aq, int);
		AST_LIST_TRAVERSE(&table->columns, column, list) {
			if (strcmp(column->name, elm) == 0) {
				/* Char can hold anything, as long as it is large enough */
				if (strncmp(column->type, "char", 4) == 0 || strncmp(column->type, "varchar", 7) == 0) {
					if ((size > column->len) && column->len != -1) {
						if (table->database->requirements == RQ_WARN) {
							ast_log(LOG_WARNING, "Realtime table %s@%s: Column '%s' should be at least %d long, but is only %d long.\n", database, tablename, column->name, size, column->len);
							res = -1;
						} else if (modify_mysql(database, tablename, column, type, size) == 0) {
							table_altered = 1;
						} else {
							res = -1;
						}
					}
				} else if (strcasestr(column->type, "unsigned")) {
					if (!ast_rq_is_int(type)) {
						if (table->database->requirements == RQ_WARN) {
							ast_log(LOG_WARNING, "Realtime table %s@%s: column '%s' cannot be type '%s' (need %s)\n",
								database, tablename, column->name, column->type,
								type == RQ_CHAR ? "char" : type == RQ_FLOAT ? "float" :
								type == RQ_DATETIME ? "datetime" : type == RQ_DATE ? "date" : "a rather stiff drink");
							res = -1;
						} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {
							table_altered = 1;
						} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {
							table_altered = 1;
						} else {
							res = -1;
						}
					} else if (strncasecmp(column->type, "tinyint", 1) == 0) {
						if (type != RQ_UINTEGER1) {
							PICK_WHICH_ALTER_ACTION(unsigned tinyint)
						}
					} else if (strncasecmp(column->type, "smallint", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 && type != RQ_UINTEGER2) {
							PICK_WHICH_ALTER_ACTION(unsigned smallint)
						}
					} else if (strncasecmp(column->type, "mediumint", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
							type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
							type != RQ_UINTEGER3) {
							PICK_WHICH_ALTER_ACTION(unsigned mediumint)
						}
					} else if (strncasecmp(column->type, "int", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
							type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
							type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
							type != RQ_UINTEGER4) {
							PICK_WHICH_ALTER_ACTION(unsigned int)
						}
					} else if (strncasecmp(column->type, "bigint", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
							type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
							type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
							type != RQ_UINTEGER4 && type != RQ_INTEGER4 &&
							type != RQ_UINTEGER8) {
							PICK_WHICH_ALTER_ACTION(unsigned bigint)
						}
					}
				} else if (strcasestr(column->type, "int")) {
					if (!ast_rq_is_int(type)) {
						if (table->database->requirements == RQ_WARN) {
							ast_log(LOG_WARNING, "Realtime table %s@%s: column '%s' cannot be type '%s' (need %s)\n",
								database, tablename, column->name, column->type,
								type == RQ_CHAR ? "char" : type == RQ_FLOAT ? "float" :
								type == RQ_DATETIME ? "datetime" : type == RQ_DATE ? "date" :
								"to get a life, rather than writing silly error messages");
							res = -1;
						} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {
							table_altered = 1;
						} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {
							table_altered = 1;
						} else {
							res = -1;
						}
					} else if (strncasecmp(column->type, "tinyint", 1) == 0) {
						if (type != RQ_INTEGER1) {
							PICK_WHICH_ALTER_ACTION(tinyint)
						}
					} else if (strncasecmp(column->type, "smallint", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 && type != RQ_INTEGER2) {
							PICK_WHICH_ALTER_ACTION(smallint)
						}
					} else if (strncasecmp(column->type, "mediumint", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
							type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
							type != RQ_INTEGER3) {
							PICK_WHICH_ALTER_ACTION(mediumint)
						}
					} else if (strncasecmp(column->type, "int", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
							type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
							type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
							type != RQ_INTEGER4) {
							PICK_WHICH_ALTER_ACTION(int)
						}
					} else if (strncasecmp(column->type, "bigint", 1) == 0) {
						if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
							type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
							type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
							type != RQ_UINTEGER4 && type != RQ_INTEGER4 &&
							type != RQ_INTEGER8) {
							PICK_WHICH_ALTER_ACTION(bigint)
						}
					}
				} else if (strncmp(column->type, "float", 5) == 0) {
					if (!ast_rq_is_int(type) && type != RQ_FLOAT) {
						if (table->database->requirements == RQ_WARN) {
							ast_log(LOG_WARNING, "Realtime table %s@%s: Column %s cannot be a %s\n", tablename, database, column->name, column->type);
							res = -1;
						} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {
							table_altered = 1;
						} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {
							table_altered = 1;
						} else {
							res = -1;
						}
					}
				} else if (strncmp(column->type, "datetime", 8) == 0 || strncmp(column->type, "timestamp", 9) == 0) {
					if (type != RQ_DATETIME) {
						if (table->database->requirements == RQ_WARN) {
							ast_log(LOG_WARNING, "Realtime table %s@%s: Column %s cannot be a %s\n", tablename, database, column->name, column->type);
							res = -1;
						} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {
							table_altered = 1;
						} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {
							table_altered = 1;
						} else {
							res = -1;
						}
					}
				} else if (strncmp(column->type, "date", 4) == 0) {
					if (type != RQ_DATE) {
						if (table->database->requirements == RQ_WARN) {
							ast_log(LOG_WARNING, "Realtime table %s@%s: Column %s cannot be a %s\n", tablename, database, column->name, column->type);
							res = -1;
						} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {
							table_altered = 1;
						} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {
							table_altered = 1;
						} else {
							res = -1;
						}
					}
				} else { /* Other, possibly unsupported types? */
					if (table->database->requirements == RQ_WARN) {
						ast_log(LOG_WARNING, "Possibly unsupported column type '%s' on column '%s'\n", column->type, column->name);
						res = -1;
					} else if (table->database->requirements == RQ_CREATECLOSE && modify_mysql(database, tablename, column, type, size) == 0) {
						table_altered = 1;
					} else if (table->database->requirements == RQ_CREATECHAR && modify_mysql(database, tablename, column, RQ_CHAR, size) == 0) {
						table_altered = 1;
					} else {
					}
				}
				break;
			}
		}

		if (!column) {
			if (table->database->requirements == RQ_WARN) {
				ast_log(LOG_WARNING, "Table %s requires a column '%s' of size '%d', but no such column exists.\n", tablename, elm, size);
			} else {
				struct ast_str *sql = ast_str_thread_get(&modify_buf, 100), *fieldtype = ast_str_thread_get(&modify3_buf, 16);

				if (table->database->requirements == RQ_CREATECHAR || type == RQ_CHAR) {
					ast_str_set(&fieldtype, 0, "CHAR(%d)", size);
				} else if (type == RQ_UINTEGER1 || type == RQ_UINTEGER2 || type == RQ_UINTEGER3 || type == RQ_UINTEGER4 || type == RQ_UINTEGER8) {
					if (type == RQ_UINTEGER1) {
						ast_str_set(&fieldtype, 0, "TINYINT(3) UNSIGNED");
					} else if (type == RQ_UINTEGER2) {
						ast_str_set(&fieldtype, 0, "SMALLINT(5) UNSIGNED");
					} else if (type == RQ_UINTEGER3) {
						ast_str_set(&fieldtype, 0, "MEDIUMINT(8) UNSIGNED");
					} else if (type == RQ_UINTEGER4) {
						ast_str_set(&fieldtype, 0, "INT(10) UNSIGNED");
					} else if (type == RQ_UINTEGER8) {
						ast_str_set(&fieldtype, 0, "BIGINT(20) UNSIGNED");
					} else {
						ast_log(LOG_WARNING, "Somebody should check this code for a rather large bug... it's about to squash Tokyo.\n");
						continue;
					}
				} else if (ast_rq_is_int(type)) {
					if (type == RQ_INTEGER1) {
						ast_str_set(&fieldtype, 0, "TINYINT(3)");
					} else if (type == RQ_INTEGER2) {
						ast_str_set(&fieldtype, 0, "SMALLINT(5)");
					} else if (type == RQ_INTEGER3) {
						ast_str_set(&fieldtype, 0, "MEDIUMINT(8)");
					} else if (type == RQ_INTEGER4) {
						ast_str_set(&fieldtype, 0, "INT(10)");
					} else if (type == RQ_INTEGER8) {
						ast_str_set(&fieldtype, 0, "BIGINT(20)");
					} else {
						ast_log(LOG_WARNING, "Somebody should check this code for a rather large bug... it's about to eat Cincinnati.\n");
						continue;
					}
				} else if (type == RQ_FLOAT) {
					ast_str_set(&fieldtype, 0, "FLOAT");
				} else if (type == RQ_DATE) {
					ast_str_set(&fieldtype, 0, "DATE");
				} else if (type == RQ_DATETIME) {
					ast_str_set(&fieldtype, 0, "DATETIME");
				} else {
					continue;
				}
				ast_str_set(&sql, 0, "ALTER TABLE %s ADD COLUMN %s %s", tablename, elm, ast_str_buffer(fieldtype));

				ast_mutex_lock(&table->database->lock);
				if (!mysql_reconnect(table->database)) {
					ast_mutex_unlock(&table->database->lock);
					ast_log(LOG_ERROR, "Unable to add column: %s\n", ast_str_buffer(sql));
					continue;
				}

				/* Execution. */
				if (mysql_real_query(&table->database->handle, ast_str_buffer(sql), ast_str_strlen(sql))) {
					ast_log(LOG_WARNING, "MySQL RealTime: Failed to query database. Check debug for more info.\n");
					ast_debug(1, "MySQL RealTime: Query: %s\n", ast_str_buffer(sql));
					ast_debug(1, "MySQL RealTime: Query Failed because: %s\n", mysql_error(&table->database->handle));
				} else {
					table_altered = 1;
				}
			}
		}
	}
	va_end(aq);
	release_table(table);

	/* If we altered the table, we must refresh the cache */
	if (table_altered) {
		unload_mysql(database, tablename);
		release_table(find_table(database, tablename));
	}
	return res;
}

static struct ast_config_engine mysql_engine = {
	.name = "mysql",
	.load_func = config_mysql,
	.realtime_func = realtime_mysql,
	.realtime_multi_func = realtime_multi_mysql,
	.store_func = store_mysql,
	.destroy_func = destroy_mysql,
	.update_func = update_mysql,
	.update2_func = update2_mysql,
	.require_func = require_mysql,
	.unload_func = unload_mysql,
};

static int load_module(void)
{
	parse_config(0);

	ast_config_engine_register(&mysql_engine);
	ast_verb(2, "MySQL RealTime driver loaded.\n");
	ast_cli_register_multiple(cli_realtime_mysql_status, sizeof(cli_realtime_mysql_status) / sizeof(struct ast_cli_entry));
	return 0;
}

static int unload_module(void)
{
	struct mysql_conn *cur;
	struct tables *table;

	ast_cli_unregister_multiple(cli_realtime_mysql_status, sizeof(cli_realtime_mysql_status) / sizeof(struct ast_cli_entry));
	ast_config_engine_deregister(&mysql_engine);
	ast_verb(2, "MySQL RealTime unloaded.\n");

	AST_RWLIST_WRLOCK(&databases);
	while ((cur = AST_RWLIST_REMOVE_HEAD(&databases, list))) {
		mysql_close(&cur->handle);
		ast_mutex_destroy(&cur->lock);
		ast_free(cur);
	}
	AST_RWLIST_UNLOCK(&databases);

	/* Destroy cached table info */
	AST_LIST_LOCK(&mysql_tables);
	while ((table = AST_LIST_REMOVE_HEAD(&mysql_tables, list))) {
		destroy_table(table);
	}
	AST_LIST_UNLOCK(&mysql_tables);

	return 0;
}

static int reload(void)
{
	parse_config(1);
	ast_verb(2, "MySQL RealTime reloaded.\n");
	return 0;
}

static int parse_config(int reload)
{
	struct ast_config *config = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *catg;
	struct mysql_conn *cur;

	if ((config = ast_config_load(RES_CONFIG_MYSQL_CONF, config_flags)) == CONFIG_STATUS_FILEMISSING) {
		/* Support old config file name */
		config = ast_config_load(RES_CONFIG_MYSQL_CONF_OLD, config_flags);
	}

	if (config == CONFIG_STATUS_FILEMISSING) {
		return 0;
	} else if (config == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Not %sloading " RES_CONFIG_MYSQL_CONF "\n", reload ? "re" : "");
	}

	AST_RWLIST_WRLOCK(&databases);
	for (catg = ast_category_browse(config, NULL); catg; catg = ast_category_browse(config, catg)) {
		/* Does this category already exist? */
		AST_RWLIST_TRAVERSE(&databases, cur, list) {
			if (!strcmp(cur->unique_name, catg)) {
				break;
			}
		}

		if (!cur) {
			if (!(cur = ast_calloc(1, sizeof(*cur) + strlen(catg) + 1))) {
				ast_log(LOG_WARNING, "Could not allocate space for MySQL database '%s'\n", catg);
				continue;
			}

			strcpy(cur->unique_name, catg); /* SAFE */
			ast_mutex_init(&cur->lock);
			AST_RWLIST_INSERT_TAIL(&databases, cur, list);
		}

		load_mysql_config(config, catg, cur);
	}
	AST_RWLIST_UNLOCK(&databases);

	ast_config_destroy(config);

	return 0;
}

static int load_mysql_config(struct ast_config *config, const char *category, struct mysql_conn *conn)
{
	const char *s;

	if (!(s = ast_variable_retrieve(config, category, "dbuser"))) {
		ast_log(LOG_WARNING, "MySQL RealTime: No database user found, using 'asterisk' as default.\n");
		s = "asterisk";
	}
	ast_copy_string(conn->user, s, sizeof(conn->user));

	if (!(s = ast_variable_retrieve(config, category, "dbpass"))) {
		ast_log(LOG_WARNING, "MySQL RealTime: No database password found, using 'asterisk' as default.\n");
		s = "asterisk";
	}
	ast_copy_string(conn->pass, s, sizeof(conn->pass));

	if (!(s = ast_variable_retrieve(config, category, "dbhost"))) {
		ast_log(LOG_WARNING, "MySQL RealTime: No database host found, using localhost via socket.\n");
		s = "";
	}
	ast_copy_string(conn->host, s, sizeof(conn->host));

	if (!(s = ast_variable_retrieve(config, category, "dbname"))) {
		ast_log(LOG_WARNING, "MySQL RealTime: No database name found, using 'asterisk' as default.\n");
		s = "asterisk";
	}
	ast_copy_string(conn->name, s, sizeof(conn->name));

	if (!(s = ast_variable_retrieve(config, category, "dbport"))) {
		ast_log(LOG_WARNING, "MySQL RealTime: No database port found, using 3306 as default.\n");
		conn->port = 3306;
	} else
		conn->port = atoi(s);

	if (!(s = ast_variable_retrieve(config, category, "dbsock"))) {
		if (ast_strlen_zero(conn->host)) {
			char *paths[3] = { "/tmp/mysql.sock", "/var/lib/mysql/mysql.sock", "/var/run/mysqld/mysqld.sock" };
			struct stat st;
			int i;
			for (i = 0; i < 3; i++) {
				if (!stat(paths[i], &st)) {
					ast_log(LOG_WARNING, "MySQL RealTime: No database socket found, using '%s' as default.\n", paths[i]);
					ast_copy_string(conn->sock, paths[i], sizeof(conn->sock));
				}
			}
			if (i == 3) {
				ast_log(LOG_WARNING, "MySQL RealTime: No database socket found (and unable to detect a suitable path).\n");
				return 0;
			}
		}
	} else
		ast_copy_string(conn->sock, s, sizeof(conn->sock));

	if ((s = ast_variable_retrieve(config, category, "dbcharset"))) {
		ast_copy_string(conn->charset, s, sizeof(conn->charset));
	}

	if (!(s = ast_variable_retrieve(config, category, "requirements"))) {
		ast_log(LOG_WARNING, "MySQL realtime: no requirements setting found, using 'warn' as default.\n");
		conn->requirements = RQ_WARN;
	} else if (!strcasecmp(s, "createclose")) {
		conn->requirements = RQ_CREATECLOSE;
	} else if (!strcasecmp(s, "createchar")) {
		conn->requirements = RQ_CREATECHAR;
	} else if (!strcasecmp(s, "warn")) {
		conn->requirements = RQ_WARN;
	} else {
		ast_log(LOG_WARNING, "MySQL realtime: unrecognized requirements setting '%s', using 'warn'\n", s);
		conn->requirements = RQ_WARN;
	}

	if (!ast_strlen_zero(conn->host)) {
		ast_debug(1, "MySQL RealTime host: %s\n", conn->host);
		ast_debug(1, "MySQL RealTime port: %i\n", conn->port);
	} else
		ast_debug(1, "MySQL RealTime socket: %s\n", conn->sock);
	ast_debug(1, "MySQL RealTime database name: %s\n", conn->name);
	ast_debug(1, "MySQL RealTime user: %s\n", conn->user);
	ast_debug(1, "MySQL RealTime password: %s\n", conn->pass);
	if(conn->charset)
		ast_debug(1, "MySQL RealTime charset: %s\n", conn->charset);

	return 1;
}

static int mysql_reconnect(struct mysql_conn *conn)
{
#ifdef MYSQL_OPT_RECONNECT
	my_bool trueval = 1;
#endif

	/* mutex lock should have been locked before calling this function. */

reconnect_tryagain:
	if ((!conn->connected) && (!ast_strlen_zero(conn->host) || conn->sock) && !ast_strlen_zero(conn->user) && !ast_strlen_zero(conn->name)) {
		if (!mysql_init(&conn->handle)) {
			ast_log(LOG_WARNING, "MySQL RealTime: Insufficient memory to allocate MySQL resource.\n");
			conn->connected = 0;
			return 0;
		}
		if(conn->charset && strlen(conn->charset) > 2){
			char set_names[255];
			char statement[512];
			snprintf(set_names, sizeof(set_names), "SET NAMES %s", conn->charset);
			mysql_real_escape_string(&conn->handle, statement, set_names, sizeof(set_names));
			mysql_options(&conn->handle, MYSQL_INIT_COMMAND, set_names);
			mysql_options(&conn->handle, MYSQL_SET_CHARSET_NAME, conn->charset);
		}

		if (mysql_real_connect(&conn->handle, conn->host, conn->user, conn->pass, conn->name, conn->port, conn->sock, 0)) {
#ifdef MYSQL_OPT_RECONNECT
			/* The default is no longer to automatically reconnect on failure,
			 * (as of 5.0.3) so we have to set that option here. */
			mysql_options(&conn->handle, MYSQL_OPT_RECONNECT, &trueval);
#endif
			ast_debug(1, "MySQL RealTime: Successfully connected to database.\n");
			conn->connected = 1;
			conn->connect_time = time(NULL);
			return 1;
		} else {
			ast_log(LOG_ERROR, "MySQL RealTime: Failed to connect database server %s on %s (err %d). Check debug for more info.\n", conn->name, !ast_strlen_zero(conn->host) ? conn->host : conn->sock, mysql_errno(&conn->handle));
			ast_debug(1, "MySQL RealTime: Cannot Connect (%d): %s\n", mysql_errno(&conn->handle), mysql_error(&conn->handle));
			conn->connected = 0;
			conn->connect_time = 0;
			return 0;
		}
	} else {
		/* MySQL likes to return an error, even if it reconnects successfully.
		 * So the postman pings twice. */
		if (mysql_ping(&conn->handle) != 0 && (usleep(1) + 2 > 0) && mysql_ping(&conn->handle) != 0) {
			conn->connected = 0;
			conn->connect_time = 0;
			ast_log(LOG_ERROR, "MySQL RealTime: Ping failed (%d).  Trying an explicit reconnect.\n", mysql_errno(&conn->handle));
			ast_debug(1, "MySQL RealTime: Server Error (%d): %s\n", mysql_errno(&conn->handle), mysql_error(&conn->handle));
			goto reconnect_tryagain;
		}

		if (!conn->connected) {
			conn->connected = 1;
			conn->connect_time = time(NULL);
		}

		if (mysql_select_db(&conn->handle, conn->name) != 0) {
			ast_log(LOG_WARNING, "MySQL RealTime: Unable to select database: %s. Still Connected (%u) - %s.\n", conn->name, mysql_errno(&conn->handle), mysql_error(&conn->handle));
			return 0;
		}

		ast_debug(1, "MySQL RealTime: Connection okay.\n");
		return 1;
	}
}

static char *handle_cli_realtime_mysql_cache(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct tables *cur;
	int l, which;
	char *ret = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "realtime mysql cache";
		e->usage =
			"Usage: realtime mysql cache [<database> <table>]\n"
			"       Shows table cache for the MySQL RealTime driver\n";
		return NULL;
	case CLI_GENERATE:
		if (a->argc < 4 || a->argc > 5) {
			return NULL;
		}
		l = strlen(a->word);
		which = 0;
		if (a->argc == 5) {
			AST_LIST_LOCK(&mysql_tables);
			AST_LIST_TRAVERSE(&mysql_tables, cur, list) {
				if (!strcasecmp(a->argv[3], cur->database->unique_name) && !strncasecmp(a->word, cur->name, l) && ++which > a->n) {
					ret = ast_strdup(cur->name);
					break;
				}
			}
			AST_LIST_UNLOCK(&mysql_tables);
		} else {
			struct mysql_conn *cur;
			AST_RWLIST_RDLOCK(&databases);
			AST_RWLIST_TRAVERSE(&databases, cur, list) {
				if (!strncasecmp(a->word, cur->unique_name, l) && ++which > a->n) {
					ret = ast_strdup(cur->unique_name);
					break;
				}
			}
			AST_RWLIST_UNLOCK(&databases);
		}
		return ret;
	}

	if (a->argc == 3) {
		/* List of tables */
		AST_LIST_LOCK(&mysql_tables);
		AST_LIST_TRAVERSE(&mysql_tables, cur, list) {
			ast_cli(a->fd, "%20.20s %s\n", cur->database->unique_name, cur->name);
		}
		AST_LIST_UNLOCK(&mysql_tables);
	} else if (a->argc == 4) {
		int found = 0;
		/* List of tables */
		AST_LIST_LOCK(&mysql_tables);
		AST_LIST_TRAVERSE(&mysql_tables, cur, list) {
			if (!strcasecmp(cur->database->unique_name, a->argv[3])) {
				ast_cli(a->fd, "%s\n", cur->name);
				found = 1;
			}
		}
		AST_LIST_UNLOCK(&mysql_tables);
		if (!found) {
			ast_cli(a->fd, "No tables cached within %s database\n", a->argv[3]);
		}
	} else if (a->argc == 5) {
		/* List of columns */
		if ((cur = find_table(a->argv[3], a->argv[4]))) {
			struct columns *col;
			ast_cli(a->fd, "Columns for Table Cache '%s':\n", a->argv[3]);
			ast_cli(a->fd, "%-20.20s %-20.20s %-3.3s\n", "Name", "Type", "Len");
			AST_LIST_TRAVERSE(&cur->columns, col, list) {
				ast_cli(a->fd, "%-20.20s %-20.20s %3d\n", col->name, col->type, col->len);
			}
			release_table(cur);
		} else {
			ast_cli(a->fd, "No such table '%s'\n", a->argv[3]);
		}
	}
	return CLI_SUCCESS;
}

static char *handle_cli_realtime_mysql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char status[256], status2[100] = "", type[20];
	char *ret = NULL;
	int ctime = 0, found = 0;
	struct mysql_conn *cur;
	int l = 0, which = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "realtime mysql status";
		e->usage =
			"Usage: realtime mysql status [<database>]\n"
			"       Shows connection information for the MySQL RealTime driver\n";
		return NULL;
	case CLI_GENERATE:
		if (a->argc == 4) {
			AST_RWLIST_RDLOCK(&databases);
			AST_RWLIST_TRAVERSE(&databases, cur, list) {
				if (!strncasecmp(a->word, cur->unique_name, l) && ++which > a->n) {
					ret = ast_strdup(cur->unique_name);
					break;
				}
			}
			AST_RWLIST_UNLOCK(&databases);
		}
		return ret;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	AST_RWLIST_RDLOCK(&databases);
	AST_RWLIST_TRAVERSE(&databases, cur, list) {
		if (a->argc == 3 || (a->argc == 4 && !strcasecmp(a->argv[3], cur->unique_name))) {
			found = 1;

			if (mysql_reconnect(cur)) {
				snprintf(type, sizeof(type), "connected to");
				ctime = time(NULL) - cur->connect_time;
			} else {
				snprintf(type, sizeof(type), "configured for");
				ctime = -1;
			}

			if (!ast_strlen_zero(cur->host)) {
				snprintf(status, sizeof(status), "%s %s %s@%s, port %d", cur->unique_name, type, cur->name, cur->host, cur->port);
			} else {
				snprintf(status, sizeof(status), "%s %s %s on socket file %s", cur->unique_name, type, cur->name, cur->sock);
			}

			if (!ast_strlen_zero(cur->user)) {
				snprintf(status2, sizeof(status2), " with username %s", cur->user);
			} else {
				status2[0] = '\0';
			}

			if (ctime > 31536000) {
				ast_cli(a->fd, "%s%s for %.1f years.\n", status, status2, (double)ctime / 31536000.0);
			} else if (ctime > 86400 * 30) {
				ast_cli(a->fd, "%s%s for %d days.\n", status, status2, ctime / 86400);
			} else if (ctime > 86400) {
				ast_cli(a->fd, "%s%s for %d days, %d hours.\n", status, status2, ctime / 86400, (ctime % 86400) / 3600);
			} else if (ctime > 3600) {
				ast_cli(a->fd, "%s%s for %d hours, %d minutes.\n", status, status2, ctime / 3600, (ctime % 3600) / 60);
			} else if (ctime > 60) {
				ast_cli(a->fd, "%s%s for %d minutes.\n", status, status2, ctime / 60);
			} else if (ctime > -1) {
				ast_cli(a->fd, "%s%s for %d seconds.\n", status, status2, ctime);
			} else {
				ast_cli(a->fd, "%s%s.\n", status, status2);
			}
		}
	}
	AST_RWLIST_UNLOCK(&databases);

	if (!found) {
		ast_cli(a->fd, "No connections configured.\n");
	}
	return CLI_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "MySQL RealTime Configuration Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_REALTIME_DRIVER,
		);

