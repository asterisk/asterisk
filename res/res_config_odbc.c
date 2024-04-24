/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2004 - 2005 Anthony Minessale II <anthmct@yahoo.com>
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
 * \brief odbc+odbc plugin for portable configuration engine
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Anthony Minessale II <anthmct@yahoo.com>
 *
 * \arg http://www.unixodbc.org
 */

/*** MODULEINFO
	<depend>res_odbc</depend>
	<depend>generic_odbc</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/res_odbc.h"
#include "asterisk/utils.h"
#include "asterisk/stringfields.h"

/*! Initial SQL query buffer size to allocate. */
#define SQL_BUF_SIZE	1024

static const char *res_config_odbc_conf = "res_config_odbc.conf";
static int order_multi_row_results_by_initial_column = 1;

AST_THREADSTORAGE(sql_buf);
AST_THREADSTORAGE(rowdata_buf);

struct custom_prepare_struct {
	const char *sql;
	const char *extra;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(encoding)[256];
	);
	const struct ast_variable *fields;
	unsigned long long skip;
};

#define ENCODE_CHUNK(buffer, s) \
	do { \
		char *eptr = buffer; \
		const char *vptr = s; \
		for (; *vptr && eptr < buffer + sizeof(buffer); vptr++) { \
			if (strchr("^;", *vptr)) { \
				/* We use ^XX, instead of %XX because '%' is a special character in SQL */ \
				snprintf(eptr, buffer + sizeof(buffer) - eptr, "^%02hhX", *vptr); \
				eptr += 3; \
			} else { \
				*eptr++ = *vptr; \
			} \
		} \
		if (eptr < buffer + sizeof(buffer)) { \
			*eptr = '\0'; \
		} else { \
			buffer[sizeof(buffer) - 1] = '\0'; \
		} \
	} while(0)

static void decode_chunk(char *chunk)
{
	for (; *chunk; chunk++) {
		if (*chunk == '^' && strchr("0123456789ABCDEF", chunk[1]) && strchr("0123456789ABCDEF", chunk[2])) {
			sscanf(chunk + 1, "%02hhX", (unsigned char *)chunk);
			memmove(chunk + 1, chunk + 3, strlen(chunk + 3) + 1);
		}
	}
}

static inline int is_text(const struct odbc_cache_columns *column)
{
	return column->type == SQL_CHAR || column->type == SQL_VARCHAR || column->type == SQL_LONGVARCHAR
		|| column->type == SQL_WCHAR || column->type == SQL_WVARCHAR || column->type == SQL_WLONGVARCHAR;
}

static SQLHSTMT custom_prepare(struct odbc_obj *obj, void *data)
{
	int res, x = 1, count = 0;
	struct custom_prepare_struct *cps = data;
	const struct ast_variable *field;
	char encodebuf[1024];
	SQLHSTMT stmt;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	ast_debug(1, "Skip: %llu; SQL: %s\n", cps->skip, cps->sql);

	res = ast_odbc_prepare(obj, stmt, cps->sql);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		if (res == SQL_ERROR) {
			ast_odbc_print_errors(SQL_HANDLE_STMT, stmt, "SQL Prepare");
		}
		ast_log(LOG_WARNING, "SQL Prepare failed! [%s]\n", cps->sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	for (field = cps->fields; field; field = field->next) {
		const char *newval = field->value;

		if ((1LL << count++) & cps->skip) {
			ast_debug(1, "Skipping field '%s'='%s' (%llo/%llo)\n", field->name, newval, 1ULL << (count - 1), cps->skip);
			continue;
		}
		ast_debug(1, "Parameter %d ('%s') = '%s'\n", x, field->name, newval);
		if (strchr(newval, ';') || strchr(newval, '^')) {
			ENCODE_CHUNK(encodebuf, newval);
			ast_string_field_set(cps, encoding[x], encodebuf);
			newval = cps->encoding[x];
		}
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(newval), 0, (void *)newval, 0, NULL);
	}

	if (!ast_strlen_zero(cps->extra)) {
		const char *newval = cps->extra;
		ast_debug(1, "Parameter %d = '%s'\n", x, newval);
		if (strchr(newval, ';') || strchr(newval, '^')) {
			ENCODE_CHUNK(encodebuf, newval);
			ast_string_field_set(cps, encoding[x], encodebuf);
			newval = cps->encoding[x];
		}
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(newval), 0, (void *)newval, 0, NULL);
	}

	return stmt;
}

/*!
 * \brief Execute an SQL query and return ast_variable list
 * \param database
 * \param table
 * \param fields list containing one or more field/operator/value set.
 *
 * Select database and preform query on table, prepare the sql statement
 * Sub-in the values to the prepared statement and execute it. Return results
 * as a ast_variable list.
 *
 * \return var on success
 * \retval NULL on failure
 */
static struct ast_variable *realtime_odbc(const char *database, const char *table, const struct ast_variable *fields)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	char coltitle[256];
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	struct ast_str *rowdata = ast_str_thread_get(&rowdata_buf, 128);
	char *op;
	const struct ast_variable *field = fields;
	char *stringp;
	char *chunk;
	SQLSMALLINT collen;
	int res;
	int x;
	struct ast_variable *var=NULL, *prev=NULL;
	SQLULEN colsize;
	SQLSMALLINT colcount=0;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	SQLLEN indicator;
	struct custom_prepare_struct cps = { .fields = fields, };
	struct ast_flags connected_flag = { RES_ODBC_CONNECTED };

	if (!table || !field || !sql || !rowdata) {
		return NULL;
	}

	obj = ast_odbc_request_obj2(database, connected_flag);
	if (!obj) {
		ast_log(LOG_ERROR, "No database handle available with the name of '%s' (check res_odbc.conf)\n", database);
		return NULL;
	}

	op = !strchr(field->name, ' ') ? " =" : "";
	ast_str_set(&sql, 0, "SELECT * FROM %s WHERE %s%s ?%s", table, field->name, op,
		strcasestr(field->name, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\\\'" : "");
	while ((field = field->next)) {
		op = !strchr(field->name, ' ') ? " =" : "";
		ast_str_append(&sql, 0, " AND %s%s ?%s", field->name, op,
			strcasestr(field->name, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\\\'" : "");
	}

	cps.sql = ast_str_buffer(sql);

	if (ast_string_field_init(&cps, 256)) {
		ast_odbc_release_obj(obj);
		return NULL;
	}
	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);
	ast_string_field_free_memory(&cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error! [%s]\n", ast_str_buffer(sql));
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	res = SQLFetch(stmt);
	if (res == SQL_NO_DATA) {
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Fetch error! [%s]\n", ast_str_buffer(sql));
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}
	for (x = 0; x < colcount; x++) {
		colsize = 0;
		collen = sizeof(coltitle);
		res = SQLDescribeCol(stmt, x + 1, (unsigned char *)coltitle, sizeof(coltitle), &collen,
					&datatype, &colsize, &decimaldigits, &nullable);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Describe Column error! [%s]\n", ast_str_buffer(sql));
			if (var)
				ast_variables_destroy(var);
			ast_odbc_release_obj(obj);
			return NULL;
		}

		ast_str_reset(rowdata);
		indicator = 0;

		res = SQLGetData(stmt, x + 1, SQL_CHAR, ast_str_buffer(rowdata), ast_str_size(rowdata), &indicator);
		ast_str_update(rowdata);
		if (indicator == SQL_NULL_DATA) {
			ast_str_reset(rowdata);
		} else if (!ast_str_strlen(rowdata)) {
			/* Because we encode the empty string for a NULL, we will encode
			 * actual empty strings as a string containing a single whitespace. */
			ast_str_set(&rowdata, -1, "%s", " ");
		} else if ((res == SQL_SUCCESS) || (res == SQL_SUCCESS_WITH_INFO)) {
			if (indicator != ast_str_strlen(rowdata)) {
				/* If the available space was not enough to contain the row data enlarge and read in the rest */
				ast_str_make_space(&rowdata, indicator + 1);
				res = SQLGetData(stmt, x + 1, SQL_CHAR, ast_str_buffer(rowdata) + ast_str_strlen(rowdata),
					ast_str_size(rowdata) - ast_str_strlen(rowdata), &indicator);
				ast_str_update(rowdata);
			}
		}

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error! [%s]\n", ast_str_buffer(sql));
			if (var)
				ast_variables_destroy(var);
			ast_odbc_release_obj(obj);
			return NULL;
		}

		stringp = ast_str_buffer(rowdata);
		if (!strncmp(coltitle, "@", 1)) {
			/* The '@' prefix indicates it's a sorcery extended field.
			 * Because ast_load_realtime_fields eliminates empty entries and makes blank (single whitespace)
			 * entries empty and keeps them, the empty or NULL values are encoded
			 * as a string containing a single whitespace. */
			if (prev) {
				prev->next = ast_variable_new(coltitle, S_OR(stringp," "), "");
				if (prev->next) {
					prev = prev->next;
				}
			} else {
				prev = var = ast_variable_new(coltitle, S_OR(stringp," "), "");
			}
		} else {
			while (stringp) {
				chunk = strsep(&stringp, ";");
				if (!ast_strlen_zero(ast_strip(chunk))) {
					if (strchr(chunk, '^')) {
						decode_chunk(chunk);
					}
					if (prev) {
						prev->next = ast_variable_new(coltitle, chunk, "");
						if (prev->next) {
							prev = prev->next;
						}
					} else {
						prev = var = ast_variable_new(coltitle, chunk, "");
					}
				}
			}
		}
	}

	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	return var;
}

/*!
 * \brief Execute an Select query and return ast_config list
 * \param database
 * \param table
 * \param fields list containing one or more field/operator/value set.
 *
 * Select database and preform query on table, prepare the sql statement
 * Sub-in the values to the prepared statement and execute it.
 * Execute this prepared query against several ODBC connected databases.
 * Return results as an ast_config variable.
 *
 * \return var on success
 * \retval NULL on failure
 */
static struct ast_config *realtime_multi_odbc(const char *database, const char *table, const struct ast_variable *fields)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	char coltitle[256];
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	struct ast_str *rowdata = ast_str_thread_get(&rowdata_buf, 128);
	const char *initfield;
	char *op;
	const struct ast_variable *field = fields;
	char *stringp;
	char *chunk;
	SQLSMALLINT collen;
	int res;
	int x;
	struct ast_variable *var=NULL;
	struct ast_config *cfg=NULL;
	struct ast_category *cat=NULL;
	struct ast_flags connected_flag = { RES_ODBC_CONNECTED };
	SQLULEN colsize;
	SQLSMALLINT colcount=0;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	SQLLEN indicator;
	struct custom_prepare_struct cps = { .fields = fields, };

	if (!table || !field || !sql || !rowdata) {
		return NULL;
	}

	obj = ast_odbc_request_obj2(database, connected_flag);
	if (!obj) {
		return NULL;
	}

	initfield = ast_strdupa(field->name);
	if ((op = strchr(initfield, ' '))) {
		*op = '\0';
	}

	op = !strchr(field->name, ' ') ? " =" : "";
	ast_str_set(&sql, 0, "SELECT * FROM %s WHERE %s%s ?%s", table, field->name, op,
		strcasestr(field->name, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\\\'" : "");
	while ((field = field->next)) {
		op = !strchr(field->name, ' ') ? " =" : "";
		ast_str_append(&sql, 0, " AND %s%s ?%s", field->name, op,
			strcasestr(field->name, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\\\'" : "");
	}

	if (order_multi_row_results_by_initial_column) {
		ast_str_append(&sql, 0, " ORDER BY %s", initfield);
	}

	cps.sql = ast_str_buffer(sql);

	if (ast_string_field_init(&cps, 256)) {
		ast_odbc_release_obj(obj);
		return NULL;
	}
	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);
	ast_string_field_free_memory(&cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error! [%s]\n", ast_str_buffer(sql));
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	cfg = ast_config_new();
	if (!cfg) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	while ((res=SQLFetch(stmt)) != SQL_NO_DATA) {
		var = NULL;
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error! [%s]\n", ast_str_buffer(sql));
			continue;
		}
		cat = ast_category_new_anonymous();
		if (!cat) {
			continue;
		}
		for (x=0;x<colcount;x++) {
			colsize = 0;
			collen = sizeof(coltitle);
			res = SQLDescribeCol(stmt, x + 1, (unsigned char *)coltitle, sizeof(coltitle), &collen,
						&datatype, &colsize, &decimaldigits, &nullable);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Describe Column error! [%s]\n", ast_str_buffer(sql));
				ast_category_destroy(cat);
				goto next_sql_fetch;
			}

			ast_str_reset(rowdata);
			indicator = 0;

			res = SQLGetData(stmt, x + 1, SQL_CHAR, ast_str_buffer(rowdata), ast_str_size(rowdata), &indicator);
			ast_str_update(rowdata);
			if (indicator == SQL_NULL_DATA) {
				continue;
			}

			if ((res == SQL_SUCCESS) || (res == SQL_SUCCESS_WITH_INFO)) {
				if (indicator != ast_str_strlen(rowdata)) {
					/* If the available space was not enough to contain the row data enlarge and read in the rest */
					ast_str_make_space(&rowdata, indicator + 1);
					res = SQLGetData(stmt, x + 1, SQL_CHAR, ast_str_buffer(rowdata) + ast_str_strlen(rowdata),
						ast_str_size(rowdata) - ast_str_strlen(rowdata), &indicator);
					ast_str_update(rowdata);
				}
			}

			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Get Data error! [%s]\n", ast_str_buffer(sql));
				ast_category_destroy(cat);
				goto next_sql_fetch;
			}
			stringp = ast_str_buffer(rowdata);
			if (!strncmp(coltitle, "@", 1)) {
				/* The '@' prefix indicates it's a sorcery extended field.
				 * Because ast_load_realtime_fields eliminates empty entries and makes blank (single whitespace)
				 * entries empty and keeps them, the empty or NULL values are encoded
				 * as a string containing a single whitespace. */
				var = ast_variable_new(coltitle, S_OR(stringp," "), "");
				ast_variable_append(cat, var);
			} else {
				while (stringp) {
					chunk = strsep(&stringp, ";");
					if (!ast_strlen_zero(ast_strip(chunk))) {
						if (strchr(chunk, '^')) {
							decode_chunk(chunk);
						}
						if (!strcmp(initfield, coltitle)) {
							ast_category_rename(cat, chunk);
						}
						var = ast_variable_new(coltitle, chunk, "");
						ast_variable_append(cat, var);
					}
				}
			}
		}
		ast_category_append(cfg, cat);
next_sql_fetch:;
	}

	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	return cfg;
}

/*!
 * \brief Execute an UPDATE query
 * \param database
 * \param table
 * \param keyfield where clause field
 * \param lookup value of field for where clause
 * \param fields list containing one or more field/value set(s).
 *
 * Update a database table, prepare the sql statement using keyfield and lookup
 * control the number of records to change. All values to be changed are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \return number of rows affected
 * \retval -1 on failure
 */
static int update_odbc(const char *database, const char *table, const char *keyfield, const char *lookup, const struct ast_variable *fields)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	SQLLEN rowcount=0;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	const struct ast_variable *field = fields;
	int res, count = 0, paramcount = 0;
	struct custom_prepare_struct cps = { .extra = lookup, .fields = fields, };
	struct odbc_cache_tables *tableptr;
	struct odbc_cache_columns *column = NULL;
	struct ast_flags connected_flag = { RES_ODBC_CONNECTED };

	if (!table || !field || !keyfield || !sql) {
		return -1;
	}

	tableptr = ast_odbc_find_table(database, table);
	if (!(obj = ast_odbc_request_obj2(database, connected_flag))) {
		ast_odbc_release_table(tableptr);
		return -1;
	}

	if (tableptr && !ast_odbc_find_column(tableptr, keyfield)) {
		ast_log(LOG_WARNING, "Key field '%s' does not exist in table '%s@%s'.  Update will fail\n", keyfield, table, database);
	}

	ast_str_set(&sql, 0, "UPDATE %s SET ", table);
	while (field) {
		if ((tableptr && (column = ast_odbc_find_column(tableptr, field->name))) || count >= 64) {
			if (paramcount++) {
				ast_str_append(&sql, 0, ", ");
			}
			/* NULL test for non-text columns */
			if (count < 64 && ast_strlen_zero(field->value) && column->nullable && !is_text(column)) {
				ast_str_append(&sql, 0, "%s=NULL", field->name);
				cps.skip |= (1LL << count);
			} else {
				/* Value is not an empty string, or column is of text type, or we couldn't fit any more into cps.skip (count >= 64 ?!). */
				ast_str_append(&sql, 0, "%s=?", field->name);
			}
		} else { /* the column does not exist in the table */
			cps.skip |= (1LL << count);
		}
		++count;
		field = field->next;
	}
	ast_str_append(&sql, 0, " WHERE %s=?", keyfield);
	ast_odbc_release_table(tableptr);

	cps.sql = ast_str_buffer(sql);

	if (ast_string_field_init(&cps, 256)) {
		ast_odbc_release_obj(obj);
		return -1;
	}
	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);
	ast_string_field_free_memory(&cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		return -1;
	}

	res = SQLRowCount(stmt, &rowcount);
	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error! [%s]\n", ast_str_buffer(sql));
		return -1;
	}

	if (rowcount >= 0) {
		return (int) rowcount;
	}

	return -1;
}

struct update2_prepare_struct {
	const char *database;
	const char *table;
	const struct ast_variable *lookup_fields;
	const struct ast_variable *update_fields;
	struct odbc_cache_tables *tableptr;
};

static SQLHSTMT update2_prepare(struct odbc_obj *obj, void *data)
{
	int res, x = 1, first = 1;
	struct update2_prepare_struct *ups = data;
	const struct ast_variable *field;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	SQLHSTMT stmt;

	if (!sql) {
		return NULL;
	}

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	ast_str_set(&sql, 0, "UPDATE %s SET ", ups->table);

	for (field = ups->update_fields; field; field = field->next) {
		if (ast_odbc_find_column(ups->tableptr, field->name)) {
			ast_str_append(&sql, 0, "%s%s=? ", first ? "" : ", ", field->name);
			SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(field->name), 0, (void *)field->value, 0, NULL);
			first = 0;
		} else {
			ast_log(LOG_NOTICE, "Not updating column '%s' in '%s@%s' because that column does not exist!\n", field->name, ups->table, ups->database);
		}
	}

	ast_str_append(&sql, 0, "WHERE");
	first = 1;

	for (field = ups->lookup_fields; field; field = field->next) {
		if (!ast_odbc_find_column(ups->tableptr, field->name)) {
			ast_log(LOG_ERROR, "One or more of the criteria columns '%s' on '%s@%s' for this update does not exist!\n", field->name, ups->table, ups->database);
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			return NULL;
		}
		ast_str_append(&sql, 0, "%s %s=?", first ? "" : " AND", field->name);
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(field->value), 0, (void *)field->value, 0, NULL);
		first = 0;
	}

	res = ast_odbc_prepare(obj, stmt, ast_str_buffer(sql));
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		if (res == SQL_ERROR) {
			ast_odbc_print_errors(SQL_HANDLE_STMT, stmt, "SQL Prepare");
		}
		ast_log(LOG_WARNING, "SQL Prepare failed! [%s]\n", ast_str_buffer(sql));
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	return stmt;
}

/*!
 * \brief Execute an UPDATE query
 * \param database, table, lookup_fields
 * \param update_fields list containing one or more field/value set(s).
 *
 * Update a database table, preparing the sql statement from a list of
 * key/value pairs specified in ap.  The lookup pairs are specified first
 * and are separated from the update pairs by a sentinel value.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \return number of rows affected
 * \retval -1 on failure
*/
static int update2_odbc(const char *database, const char *table, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	struct update2_prepare_struct ups = {
		.database = database,
		.table = table,
		.lookup_fields = lookup_fields,
		.update_fields = update_fields,
	};
	struct ast_str *sql;
	int res;
	SQLLEN rowcount = 0;

	ups.tableptr = ast_odbc_find_table(database, table);
	if (!ups.tableptr) {
		ast_log(LOG_ERROR, "Could not retrieve metadata for table '%s@%s'. Update will fail!\n", table, database);
		return -1;
	}

	if (!(obj = ast_odbc_request_obj(database, 0))) {
		ast_odbc_release_table(ups.tableptr);
		return -1;
	}

	if (!(stmt = ast_odbc_prepare_and_execute(obj, update2_prepare, &ups))) {
		ast_odbc_release_obj(obj);
		ast_odbc_release_table(ups.tableptr);
		return -1;
	}

	/* We don't need the table anymore */
	ast_odbc_release_table(ups.tableptr);

	res = SQLRowCount(stmt, &rowcount);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		/* Since only a single thread can access this memory, we can retrieve what would otherwise be lost. */
		sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
		ast_assert(sql != NULL);
		ast_log(LOG_WARNING, "SQL Row Count error! [%s]\n", ast_str_buffer(sql));
		return -1;
	}

	if (rowcount >= 0) {
		return (int) rowcount;
	}

	return -1;
}

/*!
 * \brief Execute an INSERT query
 * \param database
 * \param table
 * \param fields list containing one or more field/value set(s)
 *
 * Insert a new record into database table, prepare the sql statement.
 * All values to be changed are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \return number of rows affected
 * \retval -1 on failure
 */
static int store_odbc(const char *database, const char *table, const struct ast_variable *fields)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	SQLLEN rowcount=0;
	const struct ast_variable *field = fields;
	struct ast_str *keys;
	struct ast_str *vals;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	int res;
	struct custom_prepare_struct cps = { .fields = fields, };
	struct ast_flags connected_flag = { RES_ODBC_CONNECTED };

	keys = ast_str_create(SQL_BUF_SIZE / 2);
	vals = ast_str_create(SQL_BUF_SIZE / 4);
	if (!table || !field || !keys || !vals || !sql) {
		ast_free(vals);
		ast_free(keys);
		return -1;
	}

	obj = ast_odbc_request_obj2(database, connected_flag);
	if (!obj) {
		ast_free(vals);
		ast_free(keys);
		return -1;
	}

	ast_str_set(&keys, 0, "%s", field->name);
	ast_str_set(&vals, 0, "?");
	while ((field = field->next)) {
		ast_str_append(&keys, 0, ", %s", field->name);
		ast_str_append(&vals, 0, ", ?");
	}
	ast_str_set(&sql, 0, "INSERT INTO %s (%s) VALUES (%s)",
		table, ast_str_buffer(keys), ast_str_buffer(vals));

	ast_free(vals);
	ast_free(keys);
	cps.sql = ast_str_buffer(sql);

	if (ast_string_field_init(&cps, 256)) {
		ast_odbc_release_obj(obj);
		return -1;
	}
	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);
	ast_string_field_free_memory(&cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		return -1;
	}

	res = SQLRowCount(stmt, &rowcount);
	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error! [%s]\n", ast_str_buffer(sql));
		return -1;
	}

	if (rowcount >= 0)
		return (int)rowcount;

	return -1;
}

/*!
 * \brief Execute an DELETE query
 * \param database
 * \param table
 * \param keyfield where clause field
 * \param lookup value of field for where clause
 * \param fields list containing one or more field/value set(s)
 *
 * Delete a row from a database table, prepare the sql statement using keyfield and lookup
 * control the number of records to change. Additional params to match rows are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \return number of rows affected
 * \retval -1 on failure
 */
static int destroy_odbc(const char *database, const char *table, const char *keyfield, const char *lookup, const struct ast_variable *fields)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	SQLLEN rowcount=0;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	const struct ast_variable *field;
	int res;
	struct custom_prepare_struct cps = { .extra = lookup, .fields = fields, };
	struct ast_flags connected_flag = { RES_ODBC_CONNECTED };

	if (!table || !sql) {
		return -1;
	}

	obj = ast_odbc_request_obj2(database, connected_flag);
	if (!obj) {
		return -1;
	}

	ast_str_set(&sql, 0, "DELETE FROM %s WHERE ", table);
	for (field = fields; field; field = field->next) {
		ast_str_append(&sql, 0, "%s=? AND ", field->name);
	}
	ast_str_append(&sql, 0, "%s=?", keyfield);

	cps.sql = ast_str_buffer(sql);

	if (ast_string_field_init(&cps, 256)) {
		ast_odbc_release_obj(obj);
		return -1;
	}
	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);
	ast_string_field_free_memory(&cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		return -1;
	}

	res = SQLRowCount(stmt, &rowcount);
	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error! [%s]\n", ast_str_buffer(sql));
		return -1;
	}

	if (rowcount >= 0)
		return (int)rowcount;

	return -1;
}

struct config_odbc_obj {
	char *sql;
	unsigned long cat_metric;
	char category[128];
	char var_name[128];
	char *var_val;
	unsigned long var_val_size;
	SQLLEN err;
};


static SQLHSTMT length_determination_odbc_prepare(struct odbc_obj *obj, void *data)
{
	struct config_odbc_obj *q = data;
	SQLHSTMT sth;
	int res;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &sth);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_verb(4, "Failure in AllocStatement %d\n", res);
		return NULL;
	}

	res = ast_odbc_prepare(obj, sth, q->sql);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_verb(4, "Error in PREPARE %d\n", res);
		SQLFreeHandle(SQL_HANDLE_STMT, sth);
		return NULL;
	}

	SQLBindCol(sth, 1, SQL_C_ULONG, &q->var_val_size, sizeof(q->var_val_size), &q->err);

	return sth;
}

static SQLHSTMT config_odbc_prepare(struct odbc_obj *obj, void *data)
{
	struct config_odbc_obj *q = data;
	SQLHSTMT sth;
	int res;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &sth);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_verb(4, "Failure in AllocStatement %d\n", res);
		return NULL;
	}

	res = ast_odbc_prepare(obj, sth, q->sql);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_verb(4, "Error in PREPARE %d\n", res);
		SQLFreeHandle(SQL_HANDLE_STMT, sth);
		return NULL;
	}

	SQLBindCol(sth, 1, SQL_C_ULONG, &q->cat_metric, sizeof(q->cat_metric), &q->err);
	SQLBindCol(sth, 2, SQL_C_CHAR, q->category, sizeof(q->category), &q->err);
	SQLBindCol(sth, 3, SQL_C_CHAR, q->var_name, sizeof(q->var_name), &q->err);
	SQLBindCol(sth, 4, SQL_C_CHAR, q->var_val, q->var_val_size, &q->err);

	return sth;
}

static struct ast_config *config_odbc(const char *database, const char *table, const char *file, struct ast_config *cfg, struct ast_flags flags, const char *sugg_incl, const char *who_asked)
{
	struct ast_variable *new_v;
	struct ast_category *cur_cat;
	int res = 0;
	struct odbc_obj *obj;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, SQL_BUF_SIZE);
	unsigned int last_cat_metric = 0;
	SQLSMALLINT rowcount = 0;
	SQLHSTMT stmt;
	char last[128] = "";
	struct config_odbc_obj q;
	struct ast_flags loader_flags = { 0 };
	struct ast_flags connected_flag = { RES_ODBC_CONNECTED };

	memset(&q, 0, sizeof(q));

	if (!file || !strcmp (file, res_config_odbc_conf) || !sql) {
		return NULL;		/* cant configure myself with myself ! */
	}

	obj = ast_odbc_request_obj2(database, connected_flag);
	if (!obj)
		return NULL;

	ast_str_set(&sql, 0, "SELECT MAX(LENGTH(var_val)) FROM %s WHERE filename='%s'",
		table, file);
	q.sql = ast_str_buffer(sql);

	stmt = ast_odbc_prepare_and_execute(obj, length_determination_odbc_prepare, &q);
	if (!stmt) {
		ast_log(LOG_WARNING, "SQL select error! [%s]\n", ast_str_buffer(sql));
		ast_odbc_release_obj(obj);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &rowcount);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL NumResultCols error! [%s]\n", ast_str_buffer(sql));
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	if (!rowcount) {
		ast_log(LOG_NOTICE, "found nothing\n");
		ast_odbc_release_obj(obj);
		return cfg;
	}

	/* There will be only one result for this, the maximum length of a variable value */
	if (SQLFetch(stmt) == SQL_NO_DATA) {
		ast_log(LOG_NOTICE, "Failed to determine maximum length of a configuration value\n");
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	/* Reset stuff to a fresh state for the actual query which will retrieve all configuration */
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

	ast_str_set(&sql, 0, "SELECT cat_metric, category, var_name, var_val FROM %s ", table);
	ast_str_append(&sql, 0, "WHERE filename='%s' AND commented=0 ", file);
	ast_str_append(&sql, 0, "ORDER BY cat_metric DESC, var_metric ASC, category, var_name ");
	q.sql = ast_str_buffer(sql);

	q.var_val_size += 1;
	q.var_val = ast_malloc(q.var_val_size);
	if (!q.var_val) {
		ast_log(LOG_WARNING, "Could not create buffer for reading in configuration values for '%s'\n", file);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	stmt = ast_odbc_prepare_and_execute(obj, config_odbc_prepare, &q);
	if (!stmt) {
		ast_log(LOG_WARNING, "SQL select error! [%s]\n", ast_str_buffer(sql));
		ast_odbc_release_obj(obj);
		ast_free(q.var_val);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &rowcount);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL NumResultCols error! [%s]\n", ast_str_buffer(sql));
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		ast_free(q.var_val);
		return NULL;
	}

	if (!rowcount) {
		ast_log(LOG_NOTICE, "found nothing\n");
		ast_odbc_release_obj(obj);
		ast_free(q.var_val);
		return cfg;
	}

	cur_cat = ast_config_get_current_category(cfg);

	while ((res = SQLFetch(stmt)) != SQL_NO_DATA) {
		if (!strcmp (q.var_name, "#include")) {
			if (!ast_config_internal_load(q.var_val, cfg, loader_flags, "", who_asked)) {
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				ast_odbc_release_obj(obj);
				ast_free(q.var_val);
				return NULL;
			}
			continue;
		}
		if (strcmp(last, q.category) || last_cat_metric != q.cat_metric) {
			cur_cat = ast_category_new_dynamic(q.category);
			if (!cur_cat) {
				break;
			}
			strcpy(last, q.category);
			last_cat_metric	= q.cat_metric;
			ast_category_append(cfg, cur_cat);
		}

		new_v = ast_variable_new(q.var_name, q.var_val, "");
		ast_variable_append(cur_cat, new_v);
	}

	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	ast_free(q.var_val);
	return cfg;
}

#define warn_length(col, size)	ast_log(LOG_WARNING, "Realtime table %s@%s: column '%s' is not long enough to contain realtime data (needs %d)\n", table, database, col->name, size)
#define warn_type(col, type)	ast_log(LOG_WARNING, "Realtime table %s@%s: column '%s' is of the incorrect type (%d) to contain the required realtime data\n", table, database, col->name, col->type)

static int require_odbc(const char *database, const char *table, va_list ap)
{
	struct odbc_cache_tables *tableptr = ast_odbc_find_table(database, table);
	struct odbc_cache_columns *col;
	char *elm;
	int type, size;

	if (!tableptr) {
		return -1;
	}

	while ((elm = va_arg(ap, char *))) {
		type = va_arg(ap, require_type);
		size = va_arg(ap, int);
		/* Check if the field matches the criteria */
		AST_RWLIST_TRAVERSE(&tableptr->columns, col, list) {
			if (strcmp(col->name, elm) == 0) {
				/* Type check, first.  Some fields are more particular than others */
				switch (col->type) {
				case SQL_CHAR:
				case SQL_VARCHAR:
				case SQL_LONGVARCHAR:
#ifdef HAVE_ODBC_WCHAR
				case SQL_WCHAR:
				case SQL_WVARCHAR:
				case SQL_WLONGVARCHAR:
#endif
				case SQL_BINARY:
				case SQL_VARBINARY:
				case SQL_LONGVARBINARY:
				case SQL_GUID:
#define CHECK_SIZE(n) \
						if (col->size < n) {      \
							warn_length(col, n);  \
						}                         \
						break;
					switch (type) {
					case RQ_UINTEGER1: CHECK_SIZE(3)  /*         255 */
					case RQ_INTEGER1:  CHECK_SIZE(4)  /*        -128 */
					case RQ_UINTEGER2: CHECK_SIZE(5)  /*       65535 */
					case RQ_INTEGER2:  CHECK_SIZE(6)  /*      -32768 */
					case RQ_UINTEGER3:                /*    16777215 */
					case RQ_INTEGER3:  CHECK_SIZE(8)  /*    -8388608 */
					case RQ_DATE:                     /*  2008-06-09 */
					case RQ_UINTEGER4: CHECK_SIZE(10) /*  4200000000 */
					case RQ_INTEGER4:  CHECK_SIZE(11) /* -2100000000 */
					case RQ_DATETIME:                 /* 2008-06-09 16:03:47 */
					case RQ_UINTEGER8: CHECK_SIZE(19) /* trust me    */
					case RQ_INTEGER8:  CHECK_SIZE(20) /* ditto       */
					case RQ_FLOAT:
					case RQ_CHAR:      CHECK_SIZE(size)
					}
#undef CHECK_SIZE
					break;
				case SQL_TYPE_DATE:
					if (type != RQ_DATE) {
						warn_type(col, type);
					}
					break;
				case SQL_TYPE_TIMESTAMP:
				case SQL_TIMESTAMP:
				case SQL_DATETIME:
					if (type != RQ_DATE && type != RQ_DATETIME) {
						warn_type(col, type);
					}
					break;
				case SQL_BIT:
					warn_length(col, size);
					break;
#define WARN_TYPE_OR_LENGTH(n)	\
						if (!ast_rq_is_int(type)) {  \
							warn_type(col, type);    \
						} else {                     \
							warn_length(col, n);  \
						}
				case SQL_TINYINT:
					if (type != RQ_UINTEGER1) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_C_STINYINT:
					if (type != RQ_INTEGER1) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_C_USHORT:
					if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 && type != RQ_UINTEGER2) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_SMALLINT:
				case SQL_C_SSHORT:
					if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 && type != RQ_INTEGER2) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_C_ULONG:
					if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
						type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
						type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
						type != RQ_INTEGER4) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_INTEGER:
				case SQL_C_SLONG:
					if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
						type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
						type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
						type != RQ_INTEGER4) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_C_UBIGINT:
					if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
						type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
						type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
						type != RQ_UINTEGER4 && type != RQ_INTEGER4 &&
						type != RQ_INTEGER8) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
				case SQL_BIGINT:
				case SQL_C_SBIGINT:
					if (type != RQ_UINTEGER1 && type != RQ_INTEGER1 &&
						type != RQ_UINTEGER2 && type != RQ_INTEGER2 &&
						type != RQ_UINTEGER3 && type != RQ_INTEGER3 &&
						type != RQ_UINTEGER4 && type != RQ_INTEGER4 &&
						type != RQ_INTEGER8) {
						WARN_TYPE_OR_LENGTH(size)
					}
					break;
#undef WARN_TYPE_OR_LENGTH
				case SQL_NUMERIC:
				case SQL_DECIMAL:
				case SQL_FLOAT:
				case SQL_REAL:
				case SQL_DOUBLE:
					if (!ast_rq_is_int(type) && type != RQ_FLOAT) {
						warn_type(col, type);
					}
					break;
				default:
					ast_log(LOG_WARNING, "Realtime table %s@%s: column type (%d) unrecognized for column '%s'\n", table, database, col->type, elm);
				}
				break;
			}
		}
		if (!col) {
			ast_log(LOG_WARNING, "Realtime table %s@%s requires column '%s', but that column does not exist!\n", table, database, elm);
		}
	}
	AST_RWLIST_UNLOCK(&tableptr->columns);
	return 0;
}
#undef warn_length
#undef warn_type

static int unload_odbc(const char *a, const char *b)
{
	return ast_odbc_clear_cache(a, b);
}

static struct ast_config_engine odbc_engine = {
	.name = "odbc",
	.load_func = config_odbc,
	.realtime_func = realtime_odbc,
	.realtime_multi_func = realtime_multi_odbc,
	.store_func = store_odbc,
	.destroy_func = destroy_odbc,
	.update_func = update_odbc,
	.update2_func = update2_odbc,
	.require_func = require_odbc,
	.unload_func = unload_odbc,
};

static void load_config(const char *filename)
{
	struct ast_config *config;
	struct ast_flags config_flags = { 0 };
	const char *s;

	config = ast_config_load(filename, config_flags);
	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		if (config == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_WARNING, "Unable to load config '%s'. Using defaults.\n", filename);
		}
		order_multi_row_results_by_initial_column = 1;
		return;
	}

	/* Result set ordering is enabled by default */
	s = ast_variable_retrieve(config, "general", "order_multi_row_results_by_initial_column");
	order_multi_row_results_by_initial_column = !s || ast_true(s);

	ast_config_destroy(config);
}

static int load_module(void)
{
	/* We'll either successfully load the configuration or fail with reasonable
	 * defaults */
	load_config(res_config_odbc_conf);
	ast_config_engine_register(&odbc_engine);
	return 0;
}

static int reload_module(void)
{
	load_config(res_config_odbc_conf);
	return 0;
}

static int unload_module(void)
{
	ast_config_engine_deregister(&odbc_engine);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Realtime ODBC configuration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
	.requires = "extconfig,res_odbc",
);
