/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * res_config_odbc.c <odbc+odbc plugin for portable configuration engine >
 * Copyright (C) 2004 Anthony Minessale II <anthmct@yahoo.com>
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/config_pvt.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <asterisk/options.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asterisk/res_odbc.h>
#include <asterisk/utils.h>

static char *tdesc = "ODBC Configuration";
static struct ast_config_reg reg1;

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
	SQLLEN rowcount=0;
	SQLULEN colsize;
	SQLSMALLINT colcount=0;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	va_list aq;
	
	va_copy(aq, ap);
	
	
	if (!table)
		return NULL;

	obj = fetch_odbc_obj(database);
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
		
	res = SQLExecute(stmt);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLRowCount(stmt, &rowcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	if (rowcount) {
		res = SQLFetch(stmt);
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
					ast_destroy_realtime(var);
				return NULL;
			}
			res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
				if (var)
					ast_destroy_realtime(var);
				return NULL;
			}
			stringp = rowdata;
			while(stringp) {
				chunk = strsep(&stringp, ";");
				if (chunk && !ast_strlen_zero(ast_strip(chunk))) {
					if (prev) {
						prev->next = ast_new_variable(coltitle, chunk);
						if (prev->next)
							prev = prev->next;
					} else 
						prev = var = ast_new_variable(coltitle, chunk);
					
				}
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
	char *title=NULL;
	const char *initfield=NULL;
	char *op;
	const char *newparam, *newval;
	char *stringp;
	char *chunk;
	SQLSMALLINT collen;
	int res;
	int x;
	struct ast_variable *var=NULL, *prev=NULL;
	struct ast_config *cfg=NULL;
	struct ast_category *cat=NULL;
	struct ast_realloca ra;
	SQLLEN rowcount=0;
	SQLULEN colsize;
	SQLSMALLINT colcount=0;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;

	va_list aq;
	va_copy(aq, ap);
	
	
	if (!table)
		return NULL;
	memset(&ra, 0, sizeof(ra));

	obj = fetch_odbc_obj(database);
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
		
	res = SQLExecute(stmt);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLRowCount(stmt, &rowcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Row Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	while (rowcount--) {
		var = NULL;
		prev = NULL;
		title = NULL;
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			continue;
		}
		for (x=0;x<colcount;x++) {
			rowdata[0] = '\0';
			collen = sizeof(coltitle);
			res = SQLDescribeCol(stmt, x + 1, coltitle, sizeof(coltitle), &collen, 
						&datatype, &colsize, &decimaldigits, &nullable);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
				if (var)
					ast_destroy_realtime(var);
				continue;
			}
			res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
				if (var)
					ast_destroy_realtime(var);
				continue;
			}
			stringp = rowdata;
			while(stringp) {
				chunk = strsep(&stringp, ";");
				if (chunk && !ast_strlen_zero(ast_strip(chunk))) {
					if (initfield && !strcmp(initfield, coltitle) && !title)
						title = ast_restrdupa(&ra, chunk);
					if (prev) {
						prev->next = ast_new_variable(coltitle, chunk);
						if (prev->next)
							prev = prev->next;
					} else 
						prev = var = ast_new_variable(coltitle, chunk);
					
				}
			}
		}
		if (var) {
			cat = ast_new_category(title ? title : "");
			if (cat) {
				cat->root = var;
				if (!cfg) 
					cfg = ast_new_config();
				if (cfg)
					ast_category_append(cfg, cat);
				else 
					ast_category_destroy(cat);
			} else {
				ast_log(LOG_WARNING, "Out of memory!\n");
				ast_destroy_realtime(var);
			}
		}
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

	obj = fetch_odbc_obj (database);
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

	res = SQLExecute(stmt);

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

	if (rowcount) 
		return 0;
	return -1;
}

static struct ast_config *config_odbc(const char *database, const char *table, const char *file, struct ast_config *new_config_s, struct ast_category **new_cat_p, int recur)
{
	struct ast_config *new;
	struct ast_variable *cur_v, *new_v;
	struct ast_category *cur_cat, *new_cat;
	int res = 0;
	odbc_obj *obj;
	SQLINTEGER err=0, commented=0, cat_metric=0, var_metric=0, last_cat_metric=0;
	SQLBIGINT id;
	char sql[255] = "", filename[128], category[128], var_name[128], var_val[512];
	SQLSMALLINT rowcount=0;
	SQLHSTMT stmt;
	char last[80] = "";
	int cat_started = 0;
	int var_started = 0;


	if (!file || !strcmp (file, "res_config_odbc.conf"))
		return NULL;		/* cant configure myself with myself ! */

	obj = fetch_odbc_obj(database);
	if (!obj)
		return NULL;

	last[0] = '\0';

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
	res = SQLExecDirect (stmt, sql, SQL_NTS);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log (LOG_WARNING, "SQL select error!\n[%s]\n\n", sql);
		return NULL;
	}

	res = SQLNumResultCols (stmt, &rowcount);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log (LOG_WARNING, "SQL select error!\n[%s]\n\n", sql);
		return NULL;
	}

	if (new_config_s) {
		new = new_config_s;
		cat_started++;
	} else {
		new = ast_new_config ();
	}
	
	if (!new) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	if (rowcount) {
		res = SQLFetch (stmt);
		cat_started = 0;

		cur_cat = *new_cat_p;
		cur_v = NULL;

		if (cur_cat)
			cat_started = 1;
		if (cur_v)
			var_started = 1;

		while (res != SQL_NO_DATA) {
			if (!strcmp (var_name, "#include") && recur < MAX_INCLUDE_LEVEL) {

				config_odbc(database, table, var_val, new, &cur_cat, recur + 1);
			} else {
				if (strcmp (last, category) || last_cat_metric != cat_metric) {
					strncpy(last, category, sizeof(last) - 1);
					last_cat_metric	= cat_metric;
					new_cat = (struct ast_category *) ast_new_category (category);

					if (!cat_started) {
						cat_started++;
						new->root = new_cat;
						cur_cat = new->root;
					} else {
						cur_cat->next = new_cat;
						cur_cat = cur_cat->next;
					}
					var_started = 0;

				}

				new_v = ast_new_variable (var_name, var_val);

				if (!var_started) {
					var_started++;
					cur_cat->root = new_v;
					cur_v = cur_cat->root;
				} else {
					cur_v->next = new_v;
					cur_v = cur_v->next;
				}
			}

		/* next row  */
			res = SQLFetch (stmt);
		}

		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else {
		ast_log (LOG_NOTICE, "found nothing\n");
	}
	return new;

}

int unload_module (void)
{
	ast_cust_config_deregister (&reg1);
	if (option_verbose)
		ast_verbose("res_config_odbc unloaded.\n");
	STANDARD_HANGUP_LOCALUSERS;
	return 0;
}

int load_module (void)
{
	memset (&reg1, 0, sizeof (struct ast_config_reg));
	strncpy(reg1.name, "odbc", sizeof(reg1.name) - 1);
	reg1.static_func = config_odbc;
	reg1.realtime_func = realtime_odbc;
	reg1.realtime_multi_func = realtime_multi_odbc;
	reg1.update_func = update_odbc;
	ast_cust_config_register (&reg1);
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
