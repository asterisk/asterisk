/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 1999-2005, Digium, Inc.
 * 
 * Manuel Guesdon <mguesdon@oxymium.net> - PostgreSQL RealTime Driver Author/Adaptor
 * Mark Spencer <markster@digium.com>  - Asterisk Author
 * Matthew Boehm <mboehm@cytelcom.com> - MySQL RealTime Driver Author
 *
 * res_config_pgsql.c <PostgreSQL plugin for RealTime configuration engine>
 *
 * v1.0   - (07-11-05) - Initial version based on res_config_mysql v2.0
 */

/*! \file
 *
 * \brief PostgreSQL plugin for Asterisk RealTime Architecture
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Manuel Guesdon <mguesdon@oxymium.net> - PostgreSQL RealTime Driver Author/Adaptor
 *
 * \arg http://www.postgresql.org
 */

/*** MODULEINFO
	<depend>pgsql</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <libpq-fe.h>			/* PostgreSQL */

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"

AST_MUTEX_DEFINE_STATIC(pgsql_lock);

#define RES_CONFIG_PGSQL_CONF "res_pgsql.conf"

PGconn *pgsqlConn = NULL;

#define MAX_DB_OPTION_SIZE 64

static char dbhost[MAX_DB_OPTION_SIZE] = "";
static char dbuser[MAX_DB_OPTION_SIZE] = "";
static char dbpass[MAX_DB_OPTION_SIZE] = "";
static char dbname[MAX_DB_OPTION_SIZE] = "";
static char dbsock[MAX_DB_OPTION_SIZE] = "";
static int dbport = 5432;
static time_t connect_time = 0;

static int parse_config(int reload);
static int pgsql_reconnect(const char *database);
static char *handle_cli_realtime_pgsql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry cli_realtime[] = {
	AST_CLI_DEFINE(handle_cli_realtime_pgsql_status, "Shows connection information for the PostgreSQL RealTime driver"),
};

static struct ast_variable *realtime_pgsql(const char *database, const char *table, va_list ap)
{
	PGresult *result = NULL;
	int num_rows = 0, pgerror;
	char sql[256], escapebuf[513];
	char *stringp;
	char *chunk;
	char *op;
	const char *newparam, *newval;
	struct ast_variable *var = NULL, *prev = NULL;

	if (!table) {
		ast_log(LOG_WARNING, "PostgreSQL RealTime: No table specified.\n");
		return NULL;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		if (pgsqlConn) {
			PQfinish(pgsqlConn);
			pgsqlConn = NULL;
		};
		return NULL;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */
	op = strchr(newparam, ' ') ? "" : " =";

	PQescapeStringConn(pgsqlConn, escapebuf, newval, (sizeof(escapebuf) - 1) / 2, &pgerror);
	if (pgerror) {
		ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", newval);
		va_end(ap);
		return NULL;
	}

	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s '%s'", table, newparam, op,
			 escapebuf);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		if (!strchr(newparam, ' '))
			op = " =";
		else
			op = "";

		PQescapeStringConn(pgsqlConn, escapebuf, newval, (sizeof(escapebuf) - 1) / 2, &pgerror);
		if (pgerror) {
			ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", newval);
			va_end(ap);
			return NULL;
		}

		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s '%s'", newparam,
				 op, escapebuf);
	}
	va_end(ap);

	/* We now have our complete statement; Lets connect to the server and execute it. */
	ast_mutex_lock(&pgsql_lock);
	if (!pgsql_reconnect(database)) {
		ast_mutex_unlock(&pgsql_lock);
		return NULL;
	}

	if (!(result = PQexec(pgsqlConn, sql))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
		ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s\n", PQerrorMessage(pgsqlConn));
		ast_mutex_unlock(&pgsql_lock);
		return NULL;
	} else {
		ExecStatusType result_status = PQresultStatus(result);
		if (result_status != PGRES_COMMAND_OK
			&& result_status != PGRES_TUPLES_OK
			&& result_status != PGRES_NONFATAL_ERROR) {
			ast_log(LOG_WARNING,
					"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
			ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
			ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s (%s)\n",
						PQresultErrorMessage(result), PQresStatus(result_status));
			ast_mutex_unlock(&pgsql_lock);
			return NULL;
		}
	}

	ast_debug(1, "PostgreSQL RealTime: Result=%p Query: %s\n", result, sql);

	if ((num_rows = PQntuples(result)) > 0) {
		int i = 0;
		int rowIndex = 0;
		int numFields = PQnfields(result);
		char **fieldnames = NULL;

		ast_debug(1, "PostgreSQL RealTime: Found %d rows.\n", num_rows);

		if (!(fieldnames = ast_calloc(1, numFields * sizeof(char *)))) {
			ast_mutex_unlock(&pgsql_lock);
			PQclear(result);
			return NULL;
		}
		for (i = 0; i < numFields; i++)
			fieldnames[i] = PQfname(result, i);
		for (rowIndex = 0; rowIndex < num_rows; rowIndex++) {
			for (i = 0; i < numFields; i++) {
				stringp = PQgetvalue(result, rowIndex, i);
				while (stringp) {
					chunk = strsep(&stringp, ";");
					if (!ast_strlen_zero(ast_strip(chunk))) {
						if (prev) {
							prev->next = ast_variable_new(fieldnames[i], chunk, "");
							if (prev->next) {
								prev = prev->next;
							}
						} else {
							prev = var = ast_variable_new(fieldnames[i], chunk, "");
						}
					}
				}
			}
		}
		ast_free(fieldnames);
	} else {
		ast_debug(1, "Postgresql RealTime: Could not find any rows in table %s.\n", table);
	}

	ast_mutex_unlock(&pgsql_lock);
	PQclear(result);

	return var;
}

static struct ast_config *realtime_multi_pgsql(const char *database, const char *table, va_list ap)
{
	PGresult *result = NULL;
	int num_rows = 0, pgerror;
	char sql[256], escapebuf[513];
	const char *initfield = NULL;
	char *stringp;
	char *chunk;
	char *op;
	const char *newparam, *newval;
	struct ast_variable *var = NULL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;

	if (!table) {
		ast_log(LOG_WARNING, "PostgreSQL RealTime: No table specified.\n");
		return NULL;
	}

	if (!(cfg = ast_config_new()))
		return NULL;

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		if (pgsqlConn) {
			PQfinish(pgsqlConn);
			pgsqlConn = NULL;
		};
		return NULL;
	}

	initfield = ast_strdupa(newparam);
	if ((op = strchr(initfield, ' '))) {
		*op = '\0';
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	if (!strchr(newparam, ' '))
		op = " =";
	else
		op = "";

	PQescapeStringConn(pgsqlConn, escapebuf, newval, (sizeof(escapebuf) - 1) / 2, &pgerror);
	if (pgerror) {
		ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", newval);
		va_end(ap);
		return NULL;
	}

	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s%s '%s'", table, newparam, op,
			 escapebuf);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		if (!strchr(newparam, ' '))
			op = " =";
		else
			op = "";

		PQescapeStringConn(pgsqlConn, escapebuf, newval, (sizeof(escapebuf) - 1) / 2, &pgerror);
		if (pgerror) {
			ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", newval);
			va_end(ap);
			return NULL;
		}

		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND %s%s '%s'", newparam,
				 op, escapebuf);
	}

	if (initfield) {
		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY %s", initfield);
	}

	va_end(ap);

	/* We now have our complete statement; Lets connect to the server and execute it. */
	ast_mutex_lock(&pgsql_lock);
	if (!pgsql_reconnect(database)) {
		ast_mutex_unlock(&pgsql_lock);
		return NULL;
	}

	if (!(result = PQexec(pgsqlConn, sql))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
		ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s\n", PQerrorMessage(pgsqlConn));
		ast_mutex_unlock(&pgsql_lock);
		return NULL;
	} else {
		ExecStatusType result_status = PQresultStatus(result);
		if (result_status != PGRES_COMMAND_OK
			&& result_status != PGRES_TUPLES_OK
			&& result_status != PGRES_NONFATAL_ERROR) {
			ast_log(LOG_WARNING,
					"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
			ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
			ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s (%s)\n",
						PQresultErrorMessage(result), PQresStatus(result_status));
			ast_mutex_unlock(&pgsql_lock);
			return NULL;
		}
	}

	ast_debug(1, "PostgreSQL RealTime: Result=%p Query: %s\n", result, sql);

	if ((num_rows = PQntuples(result)) > 0) {
		int numFields = PQnfields(result);
		int i = 0;
		int rowIndex = 0;
		char **fieldnames = NULL;

		ast_debug(1, "PostgreSQL RealTime: Found %d rows.\n", num_rows);

		if (!(fieldnames = ast_calloc(1, numFields * sizeof(char *)))) {
			ast_mutex_unlock(&pgsql_lock);
			PQclear(result);
			return NULL;
		}
		for (i = 0; i < numFields; i++)
			fieldnames[i] = PQfname(result, i);

		for (rowIndex = 0; rowIndex < num_rows; rowIndex++) {
			var = NULL;
			if (!(cat = ast_category_new("","",99999)))
				continue;
			for (i = 0; i < numFields; i++) {
				stringp = PQgetvalue(result, rowIndex, i);
				while (stringp) {
					chunk = strsep(&stringp, ";");
					if (!ast_strlen_zero(ast_strip(chunk))) {
						if (initfield && !strcmp(initfield, fieldnames[i])) {
							ast_category_rename(cat, chunk);
						}
						var = ast_variable_new(fieldnames[i], chunk, "");
						ast_variable_append(cat, var);
					}
				}
			}
			ast_category_append(cfg, cat);
		}
		ast_free(fieldnames);
	} else {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Could not find any rows in table %s.\n", table);
	}

	ast_mutex_unlock(&pgsql_lock);
	PQclear(result);

	return cfg;
}

static int update_pgsql(const char *database, const char *table, const char *keyfield,
						const char *lookup, va_list ap)
{
	PGresult *result = NULL;
	int numrows = 0, pgerror;
	char sql[256], escapebuf[513];
	const char *newparam, *newval;

	if (!table) {
		ast_log(LOG_WARNING, "PostgreSQL RealTime: No table specified.\n");
		return -1;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Realtime retrieval requires at least 1 parameter and 1 value to search on.\n");
		if (pgsqlConn) {
			PQfinish(pgsqlConn);
			pgsqlConn = NULL;
		};
		return -1;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	PQescapeStringConn(pgsqlConn, escapebuf, newval, (sizeof(escapebuf) - 1) / 2, &pgerror);
	if (pgerror) {
		ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", newval);
		va_end(ap);
		return -1;
	}
	snprintf(sql, sizeof(sql), "UPDATE %s SET %s = '%s'", table, newparam, escapebuf);

	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);

		PQescapeStringConn(pgsqlConn, escapebuf, newval, (sizeof(escapebuf) - 1) / 2, &pgerror);
		if (pgerror) {
			ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", newval);
			va_end(ap);
			return -1;
		}

		snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), ", %s = '%s'", newparam,
				 escapebuf);
	}
	va_end(ap);

	PQescapeStringConn(pgsqlConn, escapebuf, lookup, (sizeof(escapebuf) - 1) / 2, &pgerror);
	if (pgerror) {
		ast_log(LOG_ERROR, "Postgres detected invalid input: '%s'\n", lookup);
		va_end(ap);
		return -1;
	}

	snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " WHERE %s = '%s'", keyfield,
			 escapebuf);

	ast_debug(1, "PostgreSQL RealTime: Update SQL: %s\n", sql);

	/* We now have our complete statement; Lets connect to the server and execute it. */
	ast_mutex_lock(&pgsql_lock);
	if (!pgsql_reconnect(database)) {
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(result = PQexec(pgsqlConn, sql))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
		ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s\n", PQerrorMessage(pgsqlConn));
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	} else {
		ExecStatusType result_status = PQresultStatus(result);
		if (result_status != PGRES_COMMAND_OK
			&& result_status != PGRES_TUPLES_OK
			&& result_status != PGRES_NONFATAL_ERROR) {
			ast_log(LOG_WARNING,
					"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
			ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
			ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s (%s)\n",
						PQresultErrorMessage(result), PQresStatus(result_status));
			ast_mutex_unlock(&pgsql_lock);
			return -1;
		}
	}

	numrows = atoi(PQcmdTuples(result));
	ast_mutex_unlock(&pgsql_lock);

	ast_debug(1, "PostgreSQL RealTime: Updated %d rows on table: %s\n", numrows, table);

	/* From http://dev.pgsql.com/doc/pgsql/en/pgsql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	 */

	if (numrows >= 0)
		return (int) numrows;

	return -1;
}

#define ESCAPE_STRING(buffer, stringname) \
	do { \
		int len; \
		if ((len = strlen(stringname)) > (buffer->len - 1) / 2) { \
			ast_str_make_space(&buffer, len * 2 + 1); \
		} \
		PQescapeStringConn(pgsqlConn, buffer->str, stringname, len, &pgresult); \
	} while (0)

static int store_pgsql(const char *database, const char *table, va_list ap)
{
	PGresult *result = NULL;
	Oid insertid;
	struct ast_str *buf = ast_str_create(256);
	struct ast_str *sql1 = ast_str_create(256);
	struct ast_str *sql2 = ast_str_create(256);
	int pgresult;
	const char *newparam, *newval;

	if (!table) {
		ast_log(LOG_WARNING, "PostgreSQL RealTime: No table specified.\n");
		return -1;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Realtime storage requires at least 1 parameter and 1 value to store.\n");
		if (pgsqlConn) {
			PQfinish(pgsqlConn);
			pgsqlConn = NULL;
		}
		return -1;
	}

	/* Must connect to the server before anything else, as the escape function requires the connection handle.. */
	ast_mutex_lock(&pgsql_lock);
	if (!pgsql_reconnect(database)) {
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */
	ESCAPE_STRING(buf, newparam);
	ast_str_set(&sql1, 0, "INSERT INTO %s (%s", table, buf->str);
	ESCAPE_STRING(buf, newval);
	ast_str_set(&sql2, 0, ") VALUES ('%s'", buf->str);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		ESCAPE_STRING(buf, newparam);
		ast_str_append(&sql1, 0, ", %s", buf->str);
		ESCAPE_STRING(buf, newval);
		ast_str_append(&sql2, 0, ", '%s'", buf->str);
	}
	va_end(ap);
	ast_str_append(&sql1, 0, "%s)", sql2->str);

	ast_debug(1, "PostgreSQL RealTime: Insert SQL: %s\n", sql1->str);

	if (!(result = PQexec(pgsqlConn, sql1->str))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql1->str);
		ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s\n", PQerrorMessage(pgsqlConn));
		ast_mutex_unlock(&pgsql_lock);
		ast_free(sql1);
		ast_free(sql2);
		ast_free(buf);
		return -1;
	} else {
		ExecStatusType result_status = PQresultStatus(result);
		if (result_status != PGRES_COMMAND_OK
			&& result_status != PGRES_TUPLES_OK
			&& result_status != PGRES_NONFATAL_ERROR) {
			ast_log(LOG_WARNING,
					"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
			ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql1->str);
			ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s (%s)\n",
						PQresultErrorMessage(result), PQresStatus(result_status));
			ast_mutex_unlock(&pgsql_lock);
			ast_free(sql1);
			ast_free(sql2);
			ast_free(buf);
			return -1;
		}
	}

	insertid = PQoidValue(result);
	ast_mutex_unlock(&pgsql_lock);
	ast_free(sql1);
	ast_free(sql2);
	ast_free(buf);

	ast_debug(1, "PostgreSQL RealTime: row inserted on table: %s, id: %u\n", table, insertid);

	/* From http://dev.pgsql.com/doc/pgsql/en/pgsql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	 */

	if (insertid >= 0)
		return (int) insertid;

	return -1;
}

static int destroy_pgsql(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
{
	PGresult *result = NULL;
	int numrows = 0;
	int pgresult;
	struct ast_str *sql = ast_str_create(256);
	struct ast_str *buf1 = ast_str_create(60), *buf2 = ast_str_create(60);
	const char *newparam, *newval;

	if (!table) {
		ast_log(LOG_WARNING, "PostgreSQL RealTime: No table specified.\n");
		return -1;
	}

	/* Get the first parameter and first value in our list of passed paramater/value pairs */
	/*newparam = va_arg(ap, const char *);
	newval = va_arg(ap, const char *);
	if (!newparam || !newval) {*/
	if (ast_strlen_zero(keyfield) || ast_strlen_zero(lookup))  {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Realtime destroy requires at least 1 parameter and 1 value to search on.\n");
		if (pgsqlConn) {
			PQfinish(pgsqlConn);
			pgsqlConn = NULL;
		};
		return -1;
	}

	/* Must connect to the server before anything else, as the escape function requires the connection handle.. */
	ast_mutex_lock(&pgsql_lock);
	if (!pgsql_reconnect(database)) {
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}


	/* Create the first part of the query using the first parameter/value pairs we just extracted
	   If there is only 1 set, then we have our query. Otherwise, loop thru the list and concat */

	ESCAPE_STRING(buf1, keyfield);
	ESCAPE_STRING(buf2, lookup);
	ast_str_set(&sql, 0, "DELETE FROM %s WHERE %s = '%s'", table, buf1->str, buf2->str);
	while ((newparam = va_arg(ap, const char *))) {
		newval = va_arg(ap, const char *);
		ESCAPE_STRING(buf1, newparam);
		ESCAPE_STRING(buf2, newval);
		ast_str_append(&sql, 0, " AND %s = '%s'", buf1->str, buf2->str);
	}
	va_end(ap);

	ast_debug(1, "PostgreSQL RealTime: Delete SQL: %s\n", sql->str);

	if (!(result = PQexec(pgsqlConn, sql->str))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql->str);
		ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s\n", PQerrorMessage(pgsqlConn));
		ast_mutex_unlock(&pgsql_lock);
		ast_free(buf1);
		ast_free(buf2);
		ast_free(sql);
		return -1;
	} else {
		ExecStatusType result_status = PQresultStatus(result);
		if (result_status != PGRES_COMMAND_OK
			&& result_status != PGRES_TUPLES_OK
			&& result_status != PGRES_NONFATAL_ERROR) {
			ast_log(LOG_WARNING,
					"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
			ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql->str);
			ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s (%s)\n",
						PQresultErrorMessage(result), PQresStatus(result_status));
			ast_mutex_unlock(&pgsql_lock);
			ast_free(buf1);
			ast_free(buf2);
			ast_free(sql);
			return -1;
		}
	}

	numrows = atoi(PQcmdTuples(result));
	ast_mutex_unlock(&pgsql_lock);
	ast_free(buf1);
	ast_free(buf2);
	ast_free(sql);

	ast_debug(1, "PostgreSQL RealTime: Deleted %d rows on table: %s\n", numrows, table);

	/* From http://dev.pgsql.com/doc/pgsql/en/pgsql-affected-rows.html
	 * An integer greater than zero indicates the number of rows affected
	 * Zero indicates that no records were updated
	 * -1 indicates that the query returned an error (although, if the query failed, it should have been caught above.)
	 */

	if (numrows >= 0)
		return (int) numrows;

	return -1;
}


static struct ast_config *config_pgsql(const char *database, const char *table,
									   const char *file, struct ast_config *cfg,
									   struct ast_flags flags, const char *suggested_incl, const char *who_asked)
{
	PGresult *result = NULL;
	long num_rows;
	struct ast_variable *new_v;
	struct ast_category *cur_cat = NULL;
	char sqlbuf[1024] = "";
	char *sql = sqlbuf;
	size_t sqlleft = sizeof(sqlbuf);
	char last[80] = "";
	int last_cat_metric = 0;

	last[0] = '\0';

	if (!file || !strcmp(file, RES_CONFIG_PGSQL_CONF)) {
		ast_log(LOG_WARNING, "PostgreSQL RealTime: Cannot configure myself.\n");
		return NULL;
	}

	ast_build_string(&sql, &sqlleft, "SELECT category, var_name, var_val, cat_metric FROM %s ", table);
	ast_build_string(&sql, &sqlleft, "WHERE filename='%s' and commented=0", file);
	ast_build_string(&sql, &sqlleft, "ORDER BY cat_metric DESC, var_metric ASC, category, var_name ");

	ast_debug(1, "PostgreSQL RealTime: Static SQL: %s\n", sqlbuf);

	/* We now have our complete statement; Lets connect to the server and execute it. */
	ast_mutex_lock(&pgsql_lock);
	if (!pgsql_reconnect(database)) {
		ast_mutex_unlock(&pgsql_lock);
		return NULL;
	}

	if (!(result = PQexec(pgsqlConn, sqlbuf))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
		ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
		ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s\n", PQerrorMessage(pgsqlConn));
		ast_mutex_unlock(&pgsql_lock);
		return NULL;
	} else {
		ExecStatusType result_status = PQresultStatus(result);
		if (result_status != PGRES_COMMAND_OK
			&& result_status != PGRES_TUPLES_OK
			&& result_status != PGRES_NONFATAL_ERROR) {
			ast_log(LOG_WARNING,
					"PostgreSQL RealTime: Failed to query database. Check debug for more info.\n");
			ast_debug(1, "PostgreSQL RealTime: Query: %s\n", sql);
			ast_debug(1, "PostgreSQL RealTime: Query Failed because: %s (%s)\n",
						PQresultErrorMessage(result), PQresStatus(result_status));
			ast_mutex_unlock(&pgsql_lock);
			return NULL;
		}
	}

	if ((num_rows = PQntuples(result)) > 0) {
		int rowIndex = 0;

		ast_debug(1, "PostgreSQL RealTime: Found %ld rows.\n", num_rows);

		for (rowIndex = 0; rowIndex < num_rows; rowIndex++) {
			char *field_category = PQgetvalue(result, rowIndex, 0);
			char *field_var_name = PQgetvalue(result, rowIndex, 1);
			char *field_var_val = PQgetvalue(result, rowIndex, 2);
			char *field_cat_metric = PQgetvalue(result, rowIndex, 3);
			if (!strcmp(field_var_name, "#include")) {
				if (!ast_config_internal_load(field_var_val, cfg, flags, "", who_asked)) {
					PQclear(result);
					ast_mutex_unlock(&pgsql_lock);
					return NULL;
				}
				continue;
			}

			if (strcmp(last, field_category) || last_cat_metric != atoi(field_cat_metric)) {
				cur_cat = ast_category_new(field_category, "", 99999);
				if (!cur_cat)
					break;
				strcpy(last, field_category);
				last_cat_metric = atoi(field_cat_metric);
				ast_category_append(cfg, cur_cat);
			}
			new_v = ast_variable_new(field_var_name, field_var_val, "");
			ast_variable_append(cur_cat, new_v);
		}
	} else {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Could not find config '%s' in database.\n", file);
	}

	PQclear(result);
	ast_mutex_unlock(&pgsql_lock);

	return cfg;
}

static struct ast_config_engine pgsql_engine = {
	.name = "pgsql",
	.load_func = config_pgsql,
	.realtime_func = realtime_pgsql,
	.realtime_multi_func = realtime_multi_pgsql,
	.store_func = store_pgsql,
	.destroy_func = destroy_pgsql,
	.update_func = update_pgsql
};

static int load_module(void)
{
	if(!parse_config(0))
		return AST_MODULE_LOAD_DECLINE;

	ast_config_engine_register(&pgsql_engine);
	ast_verb(1, "PostgreSQL RealTime driver loaded.\n");
	ast_cli_register_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));

	return 0;
}

static int unload_module(void)
{
	/* Acquire control before doing anything to the module itself. */
	ast_mutex_lock(&pgsql_lock);

	if (pgsqlConn) {
		PQfinish(pgsqlConn);
		pgsqlConn = NULL;
	}
	ast_cli_unregister_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	ast_config_engine_deregister(&pgsql_engine);
	ast_verb(1, "PostgreSQL RealTime unloaded.\n");

	/* Unlock so something else can destroy the lock. */
	ast_mutex_unlock(&pgsql_lock);

	return 0;
}

static int reload(void)
{
	parse_config(1);

	return 0;
}

static int parse_config(int reload)
{
	struct ast_config *config;
	const char *s;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((config = ast_config_load(RES_CONFIG_PGSQL_CONF, config_flags)) == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	if (!config) {
		ast_log(LOG_WARNING, "Unable to load config %s\n", RES_CONFIG_PGSQL_CONF);
		return 0;
	}

	ast_mutex_lock(&pgsql_lock);

	if (pgsqlConn) {
		PQfinish(pgsqlConn);
		pgsqlConn = NULL;
	}

	if (!(s = ast_variable_retrieve(config, "general", "dbuser"))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: No database user found, using 'asterisk' as default.\n");
		strcpy(dbuser, "asterisk");
	} else {
		ast_copy_string(dbuser, s, sizeof(dbuser));
	}

	if (!(s = ast_variable_retrieve(config, "general", "dbpass"))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: No database password found, using 'asterisk' as default.\n");
		strcpy(dbpass, "asterisk");
	} else {
		ast_copy_string(dbpass, s, sizeof(dbpass));
	}

	if (!(s = ast_variable_retrieve(config, "general", "dbhost"))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: No database host found, using localhost via socket.\n");
		dbhost[0] = '\0';
	} else {
		ast_copy_string(dbhost, s, sizeof(dbhost));
	}

	if (!(s = ast_variable_retrieve(config, "general", "dbname"))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: No database name found, using 'asterisk' as default.\n");
		strcpy(dbname, "asterisk");
	} else {
		ast_copy_string(dbname, s, sizeof(dbname));
	}

	if (!(s = ast_variable_retrieve(config, "general", "dbport"))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: No database port found, using 5432 as default.\n");
		dbport = 5432;
	} else {
		dbport = atoi(s);
	}

	if (!ast_strlen_zero(dbhost)) {
		/* No socket needed */
	} else if (!(s = ast_variable_retrieve(config, "general", "dbsock"))) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: No database socket found, using '/tmp/pgsql.sock' as default.\n");
		strcpy(dbsock, "/tmp/pgsql.sock");
	} else {
		ast_copy_string(dbsock, s, sizeof(dbsock));
	}
	ast_config_destroy(config);

	if (option_debug) {
		if (!ast_strlen_zero(dbhost)) {
			ast_debug(1, "PostgreSQL RealTime Host: %s\n", dbhost);
			ast_debug(1, "PostgreSQL RealTime Port: %i\n", dbport);
		} else {
			ast_debug(1, "PostgreSQL RealTime Socket: %s\n", dbsock);
		}
		ast_debug(1, "PostgreSQL RealTime User: %s\n", dbuser);
		ast_debug(1, "PostgreSQL RealTime Password: %s\n", dbpass);
		ast_debug(1, "PostgreSQL RealTime DBName: %s\n", dbname);
	}

	if (!pgsql_reconnect(NULL)) {
		ast_log(LOG_WARNING,
				"PostgreSQL RealTime: Couldn't establish connection. Check debug.\n");
		ast_debug(1, "PostgreSQL RealTime: Cannot Connect: %s\n", PQerrorMessage(pgsqlConn));
	}

	ast_verb(2, "PostgreSQL RealTime reloaded.\n");

	/* Done reloading. Release lock so others can now use driver. */
	ast_mutex_unlock(&pgsql_lock);

	return 1;
}

static int pgsql_reconnect(const char *database)
{
	char my_database[50];

	ast_copy_string(my_database, S_OR(database, dbname), sizeof(my_database));

	/* mutex lock should have been locked before calling this function. */

	if (pgsqlConn && PQstatus(pgsqlConn) != CONNECTION_OK) {
		PQfinish(pgsqlConn);
		pgsqlConn = NULL;
	}

	/* DB password can legitimately be 0-length */
	if ((!pgsqlConn) && (!ast_strlen_zero(dbhost) || !ast_strlen_zero(dbsock)) && !ast_strlen_zero(dbuser) && !ast_strlen_zero(my_database)) {
		struct ast_str *connInfo = ast_str_create(32);

		ast_str_set(&connInfo, 0, "host=%s port=%d dbname=%s user=%s",
			dbhost, dbport, my_database, dbuser);
		if (!ast_strlen_zero(dbpass))
			ast_str_append(&connInfo, 0, " password=%s", dbpass);

		ast_debug(1, "%u connInfo=%s\n", (unsigned int)connInfo->len, connInfo->str);
		pgsqlConn = PQconnectdb(connInfo->str);
		ast_debug(1, "%u connInfo=%s\n", (unsigned int)connInfo->len, connInfo->str);
		ast_free(connInfo);
		connInfo = NULL;

		ast_debug(1, "pgsqlConn=%p\n", pgsqlConn);
		if (pgsqlConn && PQstatus(pgsqlConn) == CONNECTION_OK) {
			ast_debug(1, "PostgreSQL RealTime: Successfully connected to database.\n");
			connect_time = time(NULL);
			return 1;
		} else {
			ast_log(LOG_ERROR,
					"PostgreSQL RealTime: Failed to connect database %s on %s: %s\n",
					dbname, dbhost, PQresultErrorMessage(NULL));
			return 0;
		}
	} else {
		ast_debug(1, "PostgreSQL RealTime: One or more of the parameters in the config does not pass our validity checks.\n");
		return 1;
	}
}

static char *handle_cli_realtime_pgsql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char status[256], credentials[100] = "";
	int ctime = time(NULL) - connect_time;

	switch (cmd) {
	case CLI_INIT:
		e->command = "realtime pgsql status";
		e->usage =
			"Usage: realtime pgsql status\n"
			"       Shows connection information for the PostgreSQL RealTime driver\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	if (pgsqlConn && PQstatus(pgsqlConn) == CONNECTION_OK) {
		if (!ast_strlen_zero(dbhost))
			snprintf(status, sizeof(status), "Connected to %s@%s, port %d", dbname, dbhost, dbport);
		else if (!ast_strlen_zero(dbsock))
			snprintf(status, sizeof(status), "Connected to %s on socket file %s", dbname, dbsock);
		else
			snprintf(status, sizeof(status), "Connected to %s@%s", dbname, dbhost);

		if (!ast_strlen_zero(dbuser))
			snprintf(credentials, sizeof(credentials), " with username %s", dbuser);

		if (ctime > 31536000)
			ast_cli(a->fd, "%s%s for %d years, %d days, %d hours, %d minutes, %d seconds.\n",
					status, credentials, ctime / 31536000, (ctime % 31536000) / 86400,
					(ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
		else if (ctime > 86400)
			ast_cli(a->fd, "%s%s for %d days, %d hours, %d minutes, %d seconds.\n", status,
					credentials, ctime / 86400, (ctime % 86400) / 3600, (ctime % 3600) / 60,
					ctime % 60);
		else if (ctime > 3600)
			ast_cli(a->fd, "%s%s for %d hours, %d minutes, %d seconds.\n", status, credentials,
					ctime / 3600, (ctime % 3600) / 60, ctime % 60);
		else if (ctime > 60)
			ast_cli(a->fd, "%s%s for %d minutes, %d seconds.\n", status, credentials, ctime / 60,
					ctime % 60);
		else
			ast_cli(a->fd, "%s%s for %d seconds.\n", status, credentials, ctime);

		return CLI_SUCCESS;
	} else {
		return CLI_FAILURE;
	}
}

/* needs usecount semantics defined */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "PostgreSQL RealTime Configuration Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload
	       );
