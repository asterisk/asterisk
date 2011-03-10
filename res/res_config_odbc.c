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
	<depend>unixodbc</depend>
	<depend>ltdl</depend>
	<depend>res_odbc</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/res_odbc.h"
#include "asterisk/utils.h"
#include "asterisk/stringfields.h"

struct custom_prepare_struct {
	const char *sql;
	const char *extra;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(encoding)[256];
	);
	va_list ap;
};

static void decode_chunk(char *chunk)
{
	for (; *chunk; chunk++) {
		if (*chunk == '^' && strchr("0123456789ABCDEF", chunk[1]) && strchr("0123456789ABCDEF", chunk[2])) {
			sscanf(chunk + 1, "%02hhX", chunk);
			memmove(chunk + 1, chunk + 3, strlen(chunk + 3) + 1);
		}
	}
}

static SQLHSTMT custom_prepare(struct odbc_obj *obj, void *data)
{
	int res, x = 1;
	struct custom_prepare_struct *cps = data;
	const char *newparam, *newval;
	char encodebuf[1024];
	SQLHSTMT stmt;
	va_list ap;

	va_copy(ap, cps->ap);

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	res = SQLPrepare(stmt, (unsigned char *)cps->sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", cps->sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		if (strchr(newval, ';') || strchr(newval, '^')) {
			char *eptr = encodebuf;
			const char *vptr = newval;
			for (; *vptr && eptr < encodebuf + sizeof(encodebuf); vptr++) {
				if (strchr("^;", *vptr)) {
					/* We use ^XX, instead of %XX because '%' is a special character in SQL */
					snprintf(eptr, encodebuf + sizeof(encodebuf) - eptr, "^%02hhX", *vptr);
					eptr += 3;
				} else {
					*eptr++ = *vptr;
				}
			}
			if (eptr < encodebuf + sizeof(encodebuf)) {
				*eptr = '\0';
			} else {
				encodebuf[sizeof(encodebuf) - 1] = '\0';
			}
			ast_string_field_set(cps, encoding[x], encodebuf);
			newval = cps->encoding[x];
		}
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(newval), 0, (void *)newval, 0, NULL);
	}
	va_end(ap);

	if (!ast_strlen_zero(cps->extra))
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(cps->extra), 0, (void *)cps->extra, 0, NULL);
	return stmt;
}

static struct ast_variable *realtime_odbc(const char *database, const char *table, va_list ap)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	char sql[1024];
	char coltitle[256];
	char rowdata[2048];
	char *op;
	const char *newparam, *newval;
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
	va_list aq;
	struct custom_prepare_struct cps = { .sql = sql };

	if (ast_string_field_init(&cps, 256)) {
		return NULL;
	}
	va_copy(cps.ap, ap);
	va_copy(aq, ap);

	if (!table) {
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	obj = ast_odbc_request_obj(database, 0);

	if (!obj) {
		ast_log(LOG_ERROR, "No database handle available with the name of '%s' (check res_odbc.conf)\n", database);
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	newparam = va_arg(aq, const char *);
	if (!newparam) {
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}
	newval = va_arg(aq, const char *);
	op = !strchr(newparam, ' ') ? " =" : "";
	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s ?%s", table, newparam, op,
		strcasestr(newparam, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\'" : "");
	while((newparam = va_arg(aq, const char *))) {
		op = !strchr(newparam, ' ') ? " =" : "";
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s ?%s", newparam, op,
			strcasestr(newparam, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\'" : "");
		newval = va_arg(aq, const char *);
	}
	va_end(aq);

	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	res = SQLFetch(stmt);
	if (res == SQL_NO_DATA) {
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}
	for (x = 0; x < colcount; x++) {
		rowdata[0] = '\0';
		colsize = 0;
		collen = sizeof(coltitle);
		res = SQLDescribeCol(stmt, x + 1, (unsigned char *)coltitle, sizeof(coltitle), &collen, 
					&datatype, &colsize, &decimaldigits, &nullable);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
			if (var)
				ast_variables_destroy(var);
			ast_odbc_release_obj(obj);
			ast_string_field_free_memory(&cps);
			return NULL;
		}

		indicator = 0;
		res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), &indicator);
		if (indicator == SQL_NULL_DATA)
			continue;

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			if (var)
				ast_variables_destroy(var);
			ast_odbc_release_obj(obj);
			return NULL;
		}
		stringp = rowdata;
		while (stringp) {
			chunk = strsep(&stringp, ";");
			if (!ast_strlen_zero(ast_strip(chunk))) {
				if (strchr(chunk, '^')) {
					decode_chunk(chunk);
				}
				if (prev) {
					prev->next = ast_variable_new(coltitle, chunk);
					if (prev->next) {
						prev = prev->next;
					}
				} else {
					prev = var = ast_variable_new(coltitle, chunk);
				}
			}
		}
	}


	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	ast_string_field_free_memory(&cps);
	return var;
}

static struct ast_config *realtime_multi_odbc(const char *database, const char *table, va_list ap)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	char sql[1024];
	char coltitle[256];
	char rowdata[2048];
	const char *initfield=NULL;
	char *op;
	const char *newparam, *newval;
	char *stringp;
	char *chunk;
	SQLSMALLINT collen;
	int res;
	int x;
	struct ast_variable *var=NULL;
	struct ast_config *cfg=NULL;
	struct ast_category *cat=NULL;
	struct ast_realloca ra;
	SQLULEN colsize;
	SQLSMALLINT colcount=0;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	SQLLEN indicator;
	struct custom_prepare_struct cps = { .sql = sql };
	va_list aq;

	if (!table || ast_string_field_init(&cps, 256)) {
		return NULL;
	}
	va_copy(cps.ap, ap);
	va_copy(aq, ap);

	memset(&ra, 0, sizeof(ra));

	obj = ast_odbc_request_obj(database, 0);
	if (!obj) {
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	newparam = va_arg(aq, const char *);
	if (!newparam)  {
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}
	initfield = ast_strdupa(newparam);
	if ((op = strchr(initfield, ' '))) 
		*op = '\0';
	newval = va_arg(aq, const char *);
	op = !strchr(newparam, ' ') ? " =" : "";
	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s ?%s", table, newparam, op,
		strcasestr(newparam, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\'" : "");
	while((newparam = va_arg(aq, const char *))) {
		op = !strchr(newparam, ' ') ? " =" : "";
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s ?%s", newparam, op,
			strcasestr(newparam, "LIKE") && !ast_odbc_backslash_is_escape(obj) ? " ESCAPE '\\'" : "");
		newval = va_arg(aq, const char *);
	}
	if (initfield)
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY %s", initfield);
	va_end(aq);

	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	cfg = ast_config_new();
	if (!cfg) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return NULL;
	}

	while ((res=SQLFetch(stmt)) != SQL_NO_DATA) {
		var = NULL;
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			continue;
		}
		cat = ast_category_new("");
		if (!cat) {
			ast_log(LOG_WARNING, "Out of memory!\n");
			continue;
		}
		for (x=0;x<colcount;x++) {
			rowdata[0] = '\0';
			colsize = 0;
			collen = sizeof(coltitle);
			res = SQLDescribeCol(stmt, x + 1, (unsigned char *)coltitle, sizeof(coltitle), &collen, 
						&datatype, &colsize, &decimaldigits, &nullable);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
				ast_category_destroy(cat);
				continue;
			}

			indicator = 0;
			res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), &indicator);
			if (indicator == SQL_NULL_DATA)
				continue;

			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
				ast_category_destroy(cat);
				continue;
			}
			stringp = rowdata;
			while (stringp) {
				chunk = strsep(&stringp, ";");
				if (!ast_strlen_zero(ast_strip(chunk))) {
					if (strchr(chunk, '^')) {
						decode_chunk(chunk);
					}
					if (initfield && !strcmp(initfield, coltitle)) {
						ast_category_rename(cat, chunk);
					}
					var = ast_variable_new(coltitle, chunk);
					ast_variable_append(cat, var);
				}
			}
		}
		ast_category_append(cfg, cat);
	}

	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	ast_string_field_free_memory(&cps);
	return cfg;
}

static int update_odbc(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
{
	struct odbc_obj *obj;
	SQLHSTMT stmt;
	char sql[256];
	SQLLEN rowcount=0;
	const char *newparam, *newval;
	int res;
	va_list aq;
	struct custom_prepare_struct cps = { .sql = sql, .extra = lookup };

	if (!table || ast_string_field_init(&cps, 256)) {
		return -1;
	}
	va_copy(cps.ap, ap);
	va_copy(aq, ap);

	if (!(obj = ast_odbc_request_obj(database, 0))) {
		ast_string_field_free_memory(&cps);
		return -1;
	}

	newparam = va_arg(aq, const char *);
	if (!newparam)  {
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return -1;
	}
	newval = va_arg(aq, const char *);
	snprintf(sql, sizeof(sql), "UPDATE %s SET %s=?", table, newparam);
	while((newparam = va_arg(aq, const char *))) {
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), ", %s=?", newparam);
		newval = va_arg(aq, const char *);
	}
	va_end(aq);
	snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " WHERE %s=?", keyfield);

	stmt = ast_odbc_prepare_and_execute(obj, custom_prepare, &cps);

	if (!stmt) {
		ast_odbc_release_obj(obj);
		ast_string_field_free_memory(&cps);
		return -1;
	}

	res = SQLRowCount(stmt, &rowcount);
	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	ast_string_field_free_memory(&cps);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error!\n[%s]\n\n", sql);
		return -1;
	}

	if (rowcount >= 0) {
		return (int) rowcount;
	}

	return -1;
}

struct config_odbc_obj {
	char *sql;
	unsigned long cat_metric;
	char category[128];
	char var_name[128];
	char var_val[1024]; /* changed from 128 to 1024 via bug 8251 */
	SQLLEN err;
};

static SQLHSTMT config_odbc_prepare(struct odbc_obj *obj, void *data)
{
	struct config_odbc_obj *q = data;
	SQLHSTMT sth;
	int res;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &sth);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "Failure in AllocStatement %d\n", res);
		return NULL;
	}

	res = SQLPrepare(sth, (unsigned char *)q->sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "Error in PREPARE %d\n", res);
		SQLFreeHandle(SQL_HANDLE_STMT, sth);
		return NULL;
	}

	SQLBindCol(sth, 1, SQL_C_ULONG, &q->cat_metric, sizeof(q->cat_metric), &q->err);
	SQLBindCol(sth, 2, SQL_C_CHAR, q->category, sizeof(q->category), &q->err);
	SQLBindCol(sth, 3, SQL_C_CHAR, q->var_name, sizeof(q->var_name), &q->err);
	SQLBindCol(sth, 4, SQL_C_CHAR, q->var_val, sizeof(q->var_val), &q->err);

	return sth;
}

static struct ast_config *config_odbc(const char *database, const char *table, const char *file, struct ast_config *cfg, int withcomments)
{
	struct ast_variable *new_v;
	struct ast_category *cur_cat;
	int res = 0;
	struct odbc_obj *obj;
	char sqlbuf[1024] = "";
	char *sql = sqlbuf;
	size_t sqlleft = sizeof(sqlbuf);
	unsigned int last_cat_metric = 0;
	SQLSMALLINT rowcount = 0;
	SQLHSTMT stmt;
	char last[128] = "";
	struct config_odbc_obj q;

	memset(&q, 0, sizeof(q));

	if (!file || !strcmp (file, "res_config_odbc.conf"))
		return NULL;		/* cant configure myself with myself ! */

	obj = ast_odbc_request_obj(database, 0);
	if (!obj)
		return NULL;

	ast_build_string(&sql, &sqlleft, "SELECT cat_metric, category, var_name, var_val FROM %s ", table);
	ast_build_string(&sql, &sqlleft, "WHERE filename='%s' AND commented=0 ", file);
	ast_build_string(&sql, &sqlleft, "ORDER BY cat_metric DESC, var_metric ASC, category, var_name ");
	q.sql = sqlbuf;

	stmt = ast_odbc_prepare_and_execute(obj, config_odbc_prepare, &q);

	if (!stmt) {
		ast_log(LOG_WARNING, "SQL select error!\n[%s]\n\n", sql);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &rowcount);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL NumResultCols error!\n[%s]\n\n", sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return NULL;
	}

	if (!rowcount) {
		ast_log(LOG_NOTICE, "found nothing\n");
		ast_odbc_release_obj(obj);
		return cfg;
	}

	cur_cat = ast_config_get_current_category(cfg);

	while ((res = SQLFetch(stmt)) != SQL_NO_DATA) {
		if (!strcmp (q.var_name, "#include")) {
			if (!ast_config_internal_load(q.var_val, cfg, 0)) {
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				ast_odbc_release_obj(obj);
				return NULL;
			}
			continue;
		} 
		if (strcmp(last, q.category) || last_cat_metric != q.cat_metric) {
			cur_cat = ast_category_new(q.category);
			if (!cur_cat) {
				ast_log(LOG_WARNING, "Out of memory!\n");
				break;
			}
			strcpy(last, q.category);
			last_cat_metric	= q.cat_metric;
			ast_category_append(cfg, cur_cat);
		}

		new_v = ast_variable_new(q.var_name, q.var_val);
		ast_variable_append(cur_cat, new_v);
	}

	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	return cfg;
}

static struct ast_config_engine odbc_engine = {
	.name = "odbc",
	.load_func = config_odbc,
	.realtime_func = realtime_odbc,
	.realtime_multi_func = realtime_multi_odbc,
	.update_func = update_odbc
};

static int unload_module (void)
{
	ast_module_user_hangup_all();
	ast_config_engine_deregister(&odbc_engine);
	if (option_verbose)
		ast_verbose("res_config_odbc unloaded.\n");
	return 0;
}

static int load_module (void)
{
	ast_config_engine_register(&odbc_engine);
	if (option_verbose)
		ast_verbose("res_config_odbc loaded.\n");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "ODBC Configuration",
		.load = load_module,
		.unload = unload_module,
		);
