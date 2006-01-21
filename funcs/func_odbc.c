/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * func_odbc
 * 
 * Copyright (c) 2005 Tilghman Lesher
 *
 * Tilghman Lesher <func_odbc__200508@the-tilghman.com>
 *
 * Special thanks to Anthony Minessale II for debugging help.
 */

/*!
 * \file
 *
 * \brief ODBC lookups
 *
 * \author Tilghman Lesher <func_odbc__200508@the-tilghman.com>
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/config.h>
#include <asterisk/res_odbc.h>

static char *tdesc = "ODBC lookups";

static char *config = "func_odbc.conf";

struct acf_odbc_query {
	char name[30];
	char dsn[30];
	char sql_read[512];
	char sql_write[512];
	struct ast_custom_function *acf;
	unsigned int deleteme:1;
	struct acf_odbc_query *next;
};

static struct acf_odbc_query *queries = NULL;
AST_MUTEX_DEFINE_STATIC(query_lock);

#ifdef NEEDTRACE
static void acf_odbc_error(SQLHSTMT stmt, int res)
{
	char state[10] = "", diagnostic[256] = "";
	SQLINTEGER nativeerror = 0;
	SQLSMALLINT diagbytes = 0;
	SQLGetDiagRec(SQL_HANDLE_STMT, stmt, 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
	ast_log(LOG_WARNING, "SQL return value %d: error %s: %s (len %d)\n", res, state, diagnostic, diagbytes);
}
#endif

/*
 * Master control routine
 */
static void acf_odbc_write(struct ast_channel *chan, char *cmd, char *data, const char *value)
{
	odbc_obj *obj;
	struct acf_odbc_query *query;
	char *s, *t, *arg, buf[512]="", varname[15];
	int res, argcount=0, valcount=0, i, retry=0;
	struct ast_channel *ast;
	SQLHSTMT stmt;
	SQLINTEGER nativeerror=0, numfields=0, rows=0;
	SQLSMALLINT diagbytes=0;
	unsigned char state[10], diagnostic[256];
#ifdef NEEDTRACE
	SQLINTEGER enable = 1;
	char *tracefile = "/tmp/odbc.trace";
#endif

	ast_mutex_lock(&query_lock);
	for (query=queries; query; query = query->next) {
		if (!strcasecmp(query->name, cmd + 5)) {
			break;
		}
	}

	if (!query) {
		ast_log(LOG_ERROR, "No such function '%s'\n", cmd);
		ast_mutex_unlock(&query_lock);
		return;
	}

	obj = fetch_odbc_obj(query->dsn, 0);

	if (!obj) {
		ast_log(LOG_ERROR, "No such DSN registered: %s (check res_odbc.conf)\n", query->dsn);
		ast_mutex_unlock(&query_lock);
		return;
	}

	/* Parse our arguments */
	s = ast_strdupa(data);
	if (value) {
		t = ast_strdupa(value);
	} else {
		t = "";
	}

	if (!s || !t) {
		ast_log(LOG_ERROR, "Out of memory\n");
		ast_mutex_unlock(&query_lock);
		return;
	}

	/* XXX You might be tempted to change this section into using
	 * pbx_builtin_pushvar_helper().  However, note that if you try
	 * to set a NULL (like for VALUE), then nothing gets set, and the
	 * value doesn't get masked out.  Even worse, when you subsequently
	 * try to remove the value you just set, you'll wind up unsetting
	 * the previous value (which is wholly undesireable).  Hence, this
	 * has to remain the way it is done here. XXX
	 */

	/* Save old arguments as variables in a fake channel */
	ast = ast_channel_alloc(0);
	while ((arg = strsep(&s, "|"))) {
		argcount++;
		snprintf(varname, sizeof(varname), "ARG%d", argcount);
		pbx_builtin_setvar_helper(ast, varname, pbx_builtin_getvar_helper(chan, varname));
		pbx_builtin_setvar_helper(chan, varname, arg);
	}

	/* Parse values, just like arguments */
	while ((arg = strsep(&t, "|"))) {
		valcount++;
		snprintf(varname, sizeof(varname), "VAL%d", valcount);
		pbx_builtin_setvar_helper(ast, varname, pbx_builtin_getvar_helper(chan, varname));
		pbx_builtin_setvar_helper(chan, varname, arg);
	}

	/* Additionally set the value as a whole */
	/* Note that pbx_builtin_setvar_helper will quite happily take a NULL for the 3rd argument */
	pbx_builtin_setvar_helper(ast, "VALUE", pbx_builtin_getvar_helper(chan, "VALUE"));
	pbx_builtin_setvar_helper(chan, "VALUE", value);

	pbx_substitute_variables_helper(chan, query->sql_write, buf, sizeof(buf) - 1);

	/* Restore prior values */
	for (i=1; i<=argcount; i++) {
		snprintf(varname, sizeof(varname), "ARG%d", argcount);
		pbx_builtin_setvar_helper(chan, varname, pbx_builtin_getvar_helper(ast, varname));
	}

	for (i=1; i<=valcount; i++) {
		snprintf(varname, sizeof(varname), "VAL%d", argcount);
		pbx_builtin_setvar_helper(chan, varname, pbx_builtin_getvar_helper(ast, varname));
	}
	pbx_builtin_setvar_helper(chan, "VALUE", pbx_builtin_getvar_helper(ast, "VALUE"));

	ast_channel_free(ast);
	ast_mutex_unlock(&query_lock);

retry_write:
#ifdef NEEDTRACE
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACE, &enable, SQL_IS_INTEGER);
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACEFILE, tracefile, strlen(tracefile));
#endif

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		pbx_builtin_setvar_helper(chan, "ODBCROWS", "-1");
		return;
	}

	res = SQLPrepare(stmt, (unsigned char *)buf, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", buf);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		pbx_builtin_setvar_helper(chan, "ODBCROWS", "-1");
		return;
	}

	res = SQLExecute(stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		if (res == SQL_ERROR) {
			SQLGetDiagField(SQL_HANDLE_STMT, stmt, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i <= numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_STMT, stmt, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SQL Execute returned an error %d: %s: %s (%d)\n", res, state, diagnostic, diagbytes);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		}
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		odbc_obj_disconnect(obj);
		/* All handles are now invalid (after a disconnect), so we gotta redo all handles */
		odbc_obj_connect(obj);
		if (!retry) {
			retry = 1;
			goto retry_write;
		}
		rows = -1;
	} else {
		/* Rows affected */
		SQLRowCount(stmt, &rows);
	}

	/* Output the affected rows, for all cases.  In the event of failure, we
	 * flag this as -1 rows.  Note that this is different from 0 affected rows
	 * which would be the case if we succeeded in our query, but the values did
	 * not change. */
	snprintf(varname, sizeof(varname), "%d", (int)rows);
	pbx_builtin_setvar_helper(chan, "ODBCROWS", varname);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", buf);
	}

	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

static char *acf_odbc_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	odbc_obj *obj;
	struct acf_odbc_query *query;
	char *s, *arg, sql[512] = "", varname[15];
	int count=0, res, x;
	SQLHSTMT stmt;
	SQLSMALLINT colcount=0;
	SQLINTEGER indicator;
#ifdef NEEDTRACE
	SQLINTEGER enable = 1;
	char *tracefile = "/tmp/odbc.trace";
#endif

	ast_mutex_lock(&query_lock);
	for (query=queries; query; query = query->next) {
		if (!strcasecmp(query->name, cmd + 5)) {
			break;
		}
	}

	if (!query) {
		ast_log(LOG_ERROR, "No such function '%s'\n", cmd);
		ast_mutex_unlock(&query_lock);
		return "";
	}

	obj = fetch_odbc_obj(query->dsn, 0);

	if (!obj) {
		ast_log(LOG_ERROR, "No such DSN registered: %s (check res_odbc.conf)\n", query->dsn);
		ast_mutex_unlock(&query_lock);
		return "";
	}

#ifdef NEEDTRACE
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACE, &enable, SQL_IS_INTEGER);
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACEFILE, tracefile, strlen(tracefile));
#endif

	/* Parse our arguments */
	if (!(s = ast_strdupa(data))) {
		ast_mutex_unlock(&query_lock);
		return "";
	}

	while ((arg = strsep(&s, "|"))) {
		count++;
		snprintf(varname, sizeof(varname), "ARG%d", count);
		/* arg is by definition non-NULL, so this works, here */
		pbx_builtin_pushvar_helper(chan, varname, arg);
	}

	pbx_substitute_variables_helper(chan, query->sql_read, sql, sizeof(sql) - 1);

	/* Restore prior values */
	for (x = 1; x <= count; x++) {
		snprintf(varname, sizeof(varname), "ARG%d", x);
		pbx_builtin_setvar_helper(chan, varname, NULL);
	}

	ast_mutex_unlock(&query_lock);

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return "";
	}

	res = SQLPrepare(stmt, (unsigned char *)sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return "";
	}

	res = odbc_smart_execute(obj, stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return "";
	}

	res = SQLNumResultCols(stmt, &colcount);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return "";
	}

	memset(buf, 0, len);

	res = SQLFetch(stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		if (res == SQL_NO_DATA) {
			if (option_verbose > 3) {
				ast_verbose(VERBOSE_PREFIX_4 "Found no rows [%s]\n", sql);
			}
		} else if (option_verbose > 3) {
			ast_log(LOG_WARNING, "Error %d in FETCH [%s]\n", res, sql);
		}
		goto acf_out;
	}

	for (x=0; x<colcount; x++) {
		int buflen, coldatalen;
		char coldata[256];

		buflen = strlen(buf);
		res = SQLGetData(stmt, x + 1, SQL_CHAR, coldata, sizeof(coldata), &indicator);
		if (indicator == SQL_NULL_DATA) {
			coldata[0] = '\0';
			res = SQL_SUCCESS;
		}

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			return "";
		}

		strncat(buf + buflen, coldata, len - buflen);
		coldatalen = strlen(coldata);
		strncat(buf + buflen + coldatalen, ",", len - buflen - coldatalen);
	}
	/* Trim trailing comma */
	buf[strlen(buf) - 1] = '\0';

acf_out:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	return buf;
}

static char *acf_escape(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char *in, *out = buf;
	for (in = data; *in && out - buf < len; in++) {
		if (*in == '\'') {
			*out = '\'';
			out++;
		}
		*out = *in;
		out++;
	}
	*out = '\0';
	return buf;
}

struct ast_custom_function escape_function = {
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
	char *tmp;

	if (!cfg || !catg) {
		return -1;
	}

	*query = calloc(1, sizeof(struct acf_odbc_query));
	if (! (*query))
		return -1;

	ast_copy_string((*query)->name, catg, sizeof((*query)->name));

	if ((tmp = ast_variable_retrieve(cfg, catg, "dsn"))) {
		ast_copy_string((*query)->dsn, tmp, sizeof((*query)->dsn));
	} else {
		return -1;
	}

	if ((tmp = ast_variable_retrieve(cfg, catg, "read"))) {
		ast_copy_string((*query)->sql_read, tmp, sizeof((*query)->sql_read));
	}

	if ((tmp = ast_variable_retrieve(cfg, catg, "write"))) {
		ast_copy_string((*query)->sql_write, tmp, sizeof((*query)->sql_write));
	}

	(*query)->acf = calloc(1, sizeof(struct ast_custom_function));
	if ((*query)->acf) {
		asprintf(&((*query)->acf->name), "ODBC_%s", catg);
		asprintf(&((*query)->acf->syntax), "ODBC_%s(<arg1>[...[,<argN>]])", catg);
		(*query)->acf->synopsis = "Runs the referenced query with the specified arguments";
		if (!ast_strlen_zero((*query)->sql_read) && !ast_strlen_zero((*query)->sql_write)) {
			asprintf(&((*query)->acf->desc),
						"Runs the following query, as defined in func_odbc.conf, performing\n"
					   	"substitution of the arguments into the query as specified by ${ARG1},\n"
						"${ARG2}, ... ${ARGn}.  When setting the function, the values are provided\n"
						"either in whole as ${VALUE} or parsed as ${VAL1}, ${VAL2}, ... ${VALn}.\n"
						"\nRead:\n%s\n\nWrite:\n%s\n",
						(*query)->sql_read,
						(*query)->sql_write);
		} else if (!ast_strlen_zero((*query)->sql_read)) {
			asprintf(&((*query)->acf->desc),
						"Runs the following query, as defined in func_odbc.conf, performing\n"
					   	"substitution of the arguments into the query as specified by ${ARG1},\n"
						"${ARG2}, ... ${ARGn}.  This function may only be read, not set.\n\nSQL:\n%s\n",
						(*query)->sql_read);
		} else if (!ast_strlen_zero((*query)->sql_write)) {
			asprintf(&((*query)->acf->desc),
						"Runs the following query, as defined in func_odbc.conf, performing\n"
					   	"substitution of the arguments into the query as specified by ${ARG1},\n"
						"${ARG2}, ... ${ARGn}.  The values are provided either in whole as\n"
						"${VALUE} or parsed as ${VAL1}, ${VAL2}, ... ${VALn}.\n"
						"This function may only be set.\nSQL:\n%s\n",
						(*query)->sql_write);
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

		if (! (*query)->acf->name || ! (*query)->acf->syntax || ! (*query)->acf->desc) {
			return -1;
		}
	} else {
		return -1;
	}
	return 0;
}

static int free_acf_query(struct acf_odbc_query *query)
{
	if (query) {
		if (query->acf) {
			if (query->acf->name)
				free(query->acf->name);
			if (query->acf->syntax)
				free(query->acf->syntax);
			if (query->acf->desc)
				free(query->acf->desc);
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

	ast_mutex_lock(&query_lock);

	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_WARNING, "Unable to load config for func_odbc: %s\n", config);
		goto out;
	}

	for (catg = ast_category_browse(cfg, NULL);
		 catg;
		 catg = ast_category_browse(cfg, catg)) {
		struct acf_odbc_query *query=NULL;

		if (init_acf_query(cfg, catg, &query)) {
			ast_log(LOG_ERROR, "Out of memory\n");
			free_acf_query(query);
		} else {
			query->next = queries;
			queries = query;
			ast_custom_function_register(query->acf);
		}
	}

	ast_config_destroy(cfg);
	ast_custom_function_register(&escape_function);
out:
	ast_mutex_unlock(&query_lock);
	return res;
}

static int odbc_unload_module(void)
{
	struct acf_odbc_query *query, *lastquery = NULL;

	ast_mutex_lock(&query_lock);
	for (query = queries; query; query = query->next) {
		if (lastquery)
			free_acf_query(lastquery);
		if (ast_custom_function_unregister(query->acf)) {
			ast_log(LOG_ERROR, "Cannot unregister function '%s'?\n", query->acf->name);
			/* Keep state valid */
			queries = query;
			ast_mutex_unlock(&query_lock);
			return -1;
		} else {
			/* If anything is waiting on this lock, this will let it pass (avoids a race) */
			ast_mutex_unlock(&query_lock);
			ast_mutex_lock(&query_lock);
			lastquery = query;
		}
	}
	if (lastquery)
		free(lastquery);
	queries = NULL;

	ast_custom_function_unregister(&escape_function);

	ast_mutex_unlock(&query_lock);
	return 0;
}

int reload(void)
{
	int res = 0;
	struct ast_config *cfg;
	struct acf_odbc_query *q, *prevq = NULL, *qdel = NULL;
	char *catg;

	ast_mutex_lock(&query_lock);

	for (q = queries; q; q = q->next) {
		q->deleteme = 1;
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

		/* We do this piecemeal, so that we stay in a consistent state, if there's ever an error */
		for (q = queries, prevq=NULL; q; prevq=q, q = q->next) {
			if (!strcasecmp(catg, q->name)) {
				break;
			}
		}

		if (init_acf_query(cfg, catg, &query)) {
			ast_log(LOG_ERROR, "Cannot initialize query ODBC_%s\n", catg);
			free_acf_query(query);
		} else {
			if (q) {
				/* Replacement */
				if (ast_custom_function_unregister(q->acf)) {
					ast_log(LOG_ERROR, "Cannot reload query %s\n", query->acf->name);
					free_acf_query(query);
				} else {
					ast_custom_function_register(query->acf);
					/* Add it to the list */
					if (prevq)
						prevq->next = query;
					else
						queries = query;
					query->next = q->next;
					/* Get rid of the old record */
					free_acf_query(q);
				}
			} else {
				/* New */
				query->next = queries;
				queries = query;
				ast_custom_function_register(query->acf);
			}
		}
	}

	/* Any remaining sets will now be destroyed */
	for (q = queries; q; q = q->next) {
		if (qdel) {
			free_acf_query(qdel);
			qdel = NULL;
		}

		if (q->deleteme) {
			if (ast_custom_function_unregister(q->acf)) {
				ast_log(LOG_ERROR, "Cannot unregister function?  Refusing to make 'ODBC_%s' go away.\n", q->name);
			} else {
				/* If anything is waiting on the lock to execute, this will dispose of it (without a race) */
				ast_mutex_unlock(&query_lock);
				ast_mutex_lock(&query_lock);

				if (prevq) {
					prevq->next = q->next;
				} else {
					queries = q->next;
				}
				qdel = q;
			}
		} else {
			prevq = q;
		}
	}
	if (qdel)
		free_acf_query(qdel);

	ast_config_destroy(cfg);
reload_out:
	ast_mutex_unlock(&query_lock);
	return res;
}

int unload_module(void)
{
	return odbc_unload_module();
}

int load_module(void)
{
	return odbc_load_module();
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	if (! ast_mutex_trylock(&query_lock)) {
		ast_mutex_unlock(&query_lock);
		return 0;
	} else {
		return 1;
	}
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
