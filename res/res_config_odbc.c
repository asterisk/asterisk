/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asterisk/res_odbc.h>

static char *tdesc = "ODBC Configuration";
static struct ast_config_reg reg1;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct ast_config *config_odbc (char *file, struct ast_config *new_config_s, struct ast_category **new_cat_p, struct ast_variable **new_v_p, int recur
#ifdef PRESERVE_COMMENTS
	, struct ast_comment_struct *acs
#endif
)
{
	struct ast_config *config, *new;
	struct ast_variable *v, *cur_v, *new_v;
	struct ast_category *cur_cat, *new_cat;
	char table[128] = "";
	char connection[128] = "";
	int configured = 0, res = 0;
	odbc_obj *obj;
	SQLINTEGER err=0, commented=0, cat_metric=0, var_metric=0, last_cat_metric=0;
	SQLBIGINT id;
	char sql[255] = "", filename[128], category[128], var_name[128], var_val[128];
	SQLSMALLINT rowcount=0;
	SQLHSTMT stmt;
	char last[80] = "";
	int cat_started = 0;
	int var_started = 0;

	if (new_config_s) {
		new = new_config_s;
		cat_started++;
	} else {
		new = ast_new_config ();
	}

	last[0] = '\0';

	if (!file || !strcmp (file, "res_config_odbc.conf"))
		return NULL;		// cant configure myself with myself !

	config = ast_load ("res_config_odbc.conf");

	if (config) {
		for (v = ast_variable_browse (config, "settings"); v; v = v->next) {
			if (!strcmp (v->name, "table")) {
				strncpy(table, v->value, sizeof(table) - 1);
				configured++;
			} else if (!strcmp (v->name, "connection")) {
				strncpy(connection, v->value, sizeof(connection) - 1);
				configured++;
			}
		}
	ast_destroy (config);
	}

	if (configured < 2)
		return NULL;

	obj = fetch_odbc_obj (connection);
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

	snprintf(sql, sizeof(sql), "select * from %s where filename='%s' and commented=0 order by filename,cat_metric desc,var_metric asc,category,var_name,var_val,id", table, file);
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

	if (rowcount) {
		res = SQLFetch (stmt);
		cat_started = 0;

		cur_cat = *new_cat_p;
		cur_v = *new_v_p;

		if (cur_cat)
			cat_started = 1;
		if (cur_v)
			var_started = 1;

		while (res != SQL_NO_DATA) {
			if (!strcmp (var_name, "#include") && recur < MAX_INCLUDE_LEVEL) {

				config_odbc (var_val, new, &cur_cat, &cur_v, recur + 1
#ifdef PRESERVE_COMMENTS
							, acs
#endif
				);
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

		// next row 
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
	ast_log (LOG_NOTICE, "res_config_odbc unloaded.\n");
	STANDARD_HANGUP_LOCALUSERS;
	return 0;
}

int load_module (void)
{
	memset (&reg1, 0, sizeof (struct ast_config_reg));
	strncpy(reg1.name, "odbc", sizeof(reg1.name) - 1);
	reg1.func = config_odbc;
	ast_cust_config_register (&reg1);
	ast_log (LOG_NOTICE, "res_config_odbc loaded.\n");
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
