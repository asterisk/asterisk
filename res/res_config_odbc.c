/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 *	http://www.unixodbc.org
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

static char *tdesc = "ODBC Configuration";
STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct ast_variable *realtime_odbc(const char *database, const char *table, va_list ap)
{
	odbc_obj *obj;
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
	SQLINTEGER indicator;
	va_list aq;
	
	va_copy(aq, ap);
	
	
	if (!table)
		return NULL;

	obj = fetch_odbc_obj(database, 0);
	if (!obj)
		return NULL;

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	newparam = va_arg(aq, const char *);
	if (!newparam)  {
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	newval = va_arg(aq, const char *);
	if (!strchr(newparam, ' ')) op = " ="; else op = "";
	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s ?", table, newparam, op);
	while((newparam = va_arg(aq, const char *))) {
		if (!strchr(newparam, ' ')) op = " ="; else op = "";
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s ?", newparam, op);
		newval = va_arg(aq, const char *);
	}
	va_end(aq);
	res = SQLPrepare(stmt, sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	
	/* Now bind the parameters */
	x = 1;

	while((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(newval), 0, (void *)newval, 0, NULL);
	}
	
	res = odbc_smart_execute(obj, stmt);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLFetch(stmt);
	if (res == SQL_NO_DATA) {
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
                return NULL;
	}
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	for (x=0;x<colcount;x++) {
		rowdata[0] = '\0';
		collen = sizeof(coltitle);
		res = SQLDescribeCol(stmt, x + 1, coltitle, sizeof(coltitle), &collen, 
					&datatype, &colsize, &decimaldigits, &nullable);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
			if (var)
				ast_variables_destroy(var);
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
			return NULL;
		}
		stringp = rowdata;
		while(stringp) {
			chunk = strsep(&stringp, ";");
			if (!ast_strlen_zero(ast_strip(chunk))) {
				if (prev) {
					prev->next = ast_variable_new(coltitle, chunk);
					if (prev->next)
						prev = prev->next;
					} else 
						prev = var = ast_variable_new(coltitle, chunk);
					
			}
		}
	}


	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	return var;
}

static struct ast_config *realtime_multi_odbc(const char *database, const char *table, va_list ap)
{
	odbc_obj *obj;
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
	SQLINTEGER indicator;

	va_list aq;
	va_copy(aq, ap);
	
	
	if (!table)
		return NULL;
	memset(&ra, 0, sizeof(ra));

	obj = fetch_odbc_obj(database, 0);
	if (!obj)
		return NULL;

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	newparam = va_arg(aq, const char *);
	if (!newparam)  {
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	initfield = ast_strdupa(newparam);
	if (initfield && (op = strchr(initfield, ' '))) 
		*op = '\0';
	newval = va_arg(aq, const char *);
	if (!strchr(newparam, ' ')) op = " ="; else op = "";
	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s ?", table, newparam, op);
	while((newparam = va_arg(aq, const char *))) {
		if (!strchr(newparam, ' ')) op = " ="; else op = "";
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s ?", newparam, op);
		newval = va_arg(aq, const char *);
	}
	if (initfield)
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY %s", initfield);
	va_end(aq);
	res = SQLPrepare(stmt, sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	
	/* Now bind the parameters */
	x = 1;

	while((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(newval), 0, (void *)newval, 0, NULL);
	}
		
	res = odbc_smart_execute(obj, stmt);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	cfg = ast_config_new();
	if (!cfg) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
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
			collen = sizeof(coltitle);
			res = SQLDescribeCol(stmt, x + 1, coltitle, sizeof(coltitle), &collen, 
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
			while(stringp) {
				chunk = strsep(&stringp, ";");
				if (!ast_strlen_zero(ast_strip(chunk))) {
					if (initfield && !strcmp(initfield, coltitle))
						ast_category_rename(cat, chunk);
					var = ast_variable_new(coltitle, chunk);
					ast_variable_append(cat, var);
				}
			}
		}
		ast_category_append(cfg, cat);
	}

	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	return cfg;
}

static int update_odbc(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
{
	odbc_obj *obj;
	SQLHSTMT stmt;
	char sql[256];
	SQLLEN rowcount=0;
	const char *newparam, *newval;
	int res;
	int x;
	va_list aq;
	
	va_copy(aq, ap);
	
	if (!table)
		return -1;

	obj = fetch_odbc_obj (database, 0);
	if (!obj)
		return -1;

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return -1;
	}

	newparam = va_arg(aq, const char *);
	if (!newparam)  {
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
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
	
	res = SQLPrepare(stmt, sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return -1;
	}
	
	/* Now bind the parameters */
	x = 1;

	while((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(newval), 0, (void *)newval, 0, NULL);
	}
		
	SQLBindParameter(stmt, x++, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(lookup), 0, (void *)lookup, 0, NULL);

	res = odbc_smart_execute(obj, stmt);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return -1;
	}

	res = SQLRowCount(stmt, &rowcount);
	SQLFreeHandle (SQL_HANDLE_STMT, stmt);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error!\n[%s]\n\n", sql);
		return -1;
	}

       if (rowcount >= 0)
               return (int)rowcount;

	return -1;
}

static struct ast_config *config_odbc(const char *database, const char *table, const char *file, struct ast_config *cfg)
{
	struct ast_variable *new_v;
	struct ast_category *cur_cat;
	int res = 0;
	odbc_obj *obj;
	SQLINTEGER err=0, commented=0, cat_metric=0, var_metric=0, last_cat_metric=0;
	SQLBIGINT id;
	char sql[255] = "", filename[128], category[128], var_name[128], var_val[512];
	SQLSMALLINT rowcount=0;
	SQLHSTMT stmt;
	char last[128] = "";

	if (!file || !strcmp (file, "res_config_odbc.conf"))
		return NULL;		/* cant configure myself with myself ! */

	obj = fetch_odbc_obj(database, 0);
	if (!obj)
		return NULL;

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);

	SQLBindCol (stmt, 1, SQL_C_ULONG, &id, sizeof (id), &err);
	SQLBindCol (stmt, 2, SQL_C_ULONG, &cat_metric, sizeof (cat_metric), &err);
	SQLBindCol (stmt, 3, SQL_C_ULONG, &var_metric, sizeof (var_metric), &err);
	SQLBindCol (stmt, 4, SQL_C_ULONG, &commented, sizeof (commented), &err);
	SQLBindCol (stmt, 5, SQL_C_CHAR, &filename, sizeof (filename), &err);
	SQLBindCol (stmt, 6, SQL_C_CHAR, &category, sizeof (category), &err);
	SQLBindCol (stmt, 7, SQL_C_CHAR, &var_name, sizeof (var_name), &err);
	SQLBindCol (stmt, 8, SQL_C_CHAR, &var_val, sizeof (var_val), &err);
	
	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE filename='%s' and commented=0 ORDER BY filename,cat_metric desc,var_metric asc,category,var_name,var_val,id", table, file);

	res = odbc_smart_direct_execute(obj, stmt, sql);
	
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log (LOG_WARNING, "SQL select error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLNumResultCols (stmt, &rowcount);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log (LOG_WARNING, "SQL NumResultCols error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	if (!rowcount) {
		ast_log (LOG_NOTICE, "found nothing\n");
		return cfg;
	}

	cur_cat = ast_config_get_current_category(cfg);

	while ((res = SQLFetch(stmt)) != SQL_NO_DATA) {
		if (!strcmp (var_name, "#include")) {
			if (!ast_config_internal_load(var_val, cfg)) {
				SQLFreeHandle (SQL_HANDLE_STMT, stmt);
				return NULL;
			}
			continue;
		} 
		if (strcmp(last, category) || last_cat_metric != cat_metric) {
			cur_cat = ast_category_new(category);
			if (!cur_cat) {
				ast_log(LOG_WARNING, "Out of memory!\n");
				break;
			}
			strcpy(last, category);
			last_cat_metric	= cat_metric;
			ast_category_append(cfg, cur_cat);
		}

		new_v = ast_variable_new(var_name, var_val);
		ast_variable_append(cur_cat, new_v);
	}

	SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	return cfg;
}

static struct ast_config_engine odbc_engine = {
	.name = "odbc",
	.load_func = config_odbc,
	.realtime_func = realtime_odbc,
	.realtime_multi_func = realtime_multi_odbc,
	.update_func = update_odbc
};

int unload_module (void)
{
	ast_config_engine_deregister(&odbc_engine);
	if (option_verbose)
		ast_verbose("res_config_odbc unloaded.\n");
	STANDARD_HANGUP_LOCALUSERS;
	return 0;
}

int load_module (void)
{
	ast_config_engine_register(&odbc_engine);
	if (option_verbose)
		ast_verbose("res_config_odbc loaded.\n");
	return 0;
}

char *description (void)
{
	return tdesc;
}

int usecount (void)
{
	/* never unload a config module */
	return 1;
}

char *key ()
{
	return ASTERISK_GPL_KEY;
}
