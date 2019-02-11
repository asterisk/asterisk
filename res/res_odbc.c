/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * res_odbc.c <ODBC resource manager>
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
 * \brief ODBC resource manager
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Anthony Minessale II <anthmct@yahoo.com>
 * \author Tilghman Lesher <tilghman@digium.com>
 *
 * \arg See also: \ref cdr_odbc
 */

/*! \li \ref res_odbc.c uses the configuration file \ref res_odbc.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page res_odbc.conf res_odbc.conf
 * \verbinclude res_odbc.conf.sample
 */

/*** MODULEINFO
	<depend>generic_odbc</depend>
	<depend>res_odbc_transaction</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/res_odbc.h"
#include "asterisk/time.h"
#include "asterisk/astobj2.h"
#include "asterisk/app.h"
#include "asterisk/strings.h"
#include "asterisk/threadstorage.h"

struct odbc_class
{
	AST_LIST_ENTRY(odbc_class) list;
	char name[80];
	char dsn[80];
	char *username;
	char *password;
	char *sanitysql;
	SQLHENV env;
	unsigned int delme:1;                /*!< Purge the class */
	unsigned int backslash_is_escape:1;  /*!< On this database, the backslash is a native escape sequence */
	unsigned int forcecommit:1;          /*!< Should uncommitted transactions be auto-committed on handle release? */
	unsigned int isolation;              /*!< Flags for how the DB should deal with data in other, uncommitted transactions */
	unsigned int conntimeout;            /*!< Maximum time the connection process should take */
	unsigned int maxconnections;         /*!< Maximum number of allowed connections */
	/*! When a connection fails, cache that failure for how long? */
	struct timeval negative_connection_cache;
	/*! When a connection fails, when did that last occur? */
	struct timeval last_negative_connect;
	/*! A pool of available connections */
	AST_LIST_HEAD_NOLOCK(, odbc_obj) connections;
	/*! Lock to protect the connections */
	ast_mutex_t lock;
	/*! Condition to notify any pending connection requesters */
	ast_cond_t cond;
	/*! The total number of current connections */
	size_t connection_cnt;
	/*! Whether logging is enabled on this class or not */
	unsigned int logging;
	/*! The number of prepares executed on this class (total from all connections */
	int prepares_executed;
	/*! The number of queries executed on this class (total from all connections) */
	int queries_executed;
	/*! The longest execution time for a query executed on this class */
	long longest_query_execution_time;
	/*! The SQL query that took the longest to execute */
	char *sql_text;
	/*! Slow query limit (in milliseconds) */
	unsigned int slowquerylimit;
};

static struct ao2_container *class_container;

static AST_RWLIST_HEAD_STATIC(odbc_tables, odbc_cache_tables);

static odbc_status odbc_obj_connect(struct odbc_obj *obj);
static odbc_status odbc_obj_disconnect(struct odbc_obj *obj);
static void odbc_register_class(struct odbc_class *class, int connect);

AST_THREADSTORAGE(errors_buf);

struct odbc_txn_frame {
	AST_LIST_ENTRY(odbc_txn_frame) list;
	struct ast_channel *owner;
	struct odbc_obj *obj;        /*!< Database handle within which transacted statements are run */
	/*!\brief Is this record the current active transaction within the channel?
	 * Note that the active flag is really only necessary for statements which
	 * are triggered from the dialplan, as there isn't a direct correlation
	 * between multiple statements.  Applications wishing to use transactions
	 * may simply perform each statement on the same odbc_obj, which keeps the
	 * transaction persistent.
	 */
	unsigned int active:1;
	unsigned int forcecommit:1;     /*!< Should uncommitted transactions be auto-committed on handle release? */
	unsigned int isolation;         /*!< Flags for how the DB should deal with data in other, uncommitted transactions */
	char name[0];                   /*!< Name of this transaction ID */
};

const char *ast_odbc_isolation2text(int iso)
{
	if (iso == SQL_TXN_READ_COMMITTED) {
		return "read_committed";
	} else if (iso == SQL_TXN_READ_UNCOMMITTED) {
		return "read_uncommitted";
	} else if (iso == SQL_TXN_SERIALIZABLE) {
		return "serializable";
	} else if (iso == SQL_TXN_REPEATABLE_READ) {
		return "repeatable_read";
	} else {
		return "unknown";
	}
}

int ast_odbc_text2isolation(const char *txt)
{
	if (strncasecmp(txt, "read_", 5) == 0) {
		if (strncasecmp(txt + 5, "c", 1) == 0) {
			return SQL_TXN_READ_COMMITTED;
		} else if (strncasecmp(txt + 5, "u", 1) == 0) {
			return SQL_TXN_READ_UNCOMMITTED;
		} else {
			return 0;
		}
	} else if (strncasecmp(txt, "ser", 3) == 0) {
		return SQL_TXN_SERIALIZABLE;
	} else if (strncasecmp(txt, "rep", 3) == 0) {
		return SQL_TXN_REPEATABLE_READ;
	} else {
		return 0;
	}
}

static void odbc_class_destructor(void *data)
{
	struct odbc_class *class = data;
	struct odbc_obj *obj;

	/* Due to refcounts, we can safely assume that any objects with a reference
	 * to us will prevent our destruction, so we don't need to worry about them.
	 */
	if (class->username) {
		ast_free(class->username);
	}
	if (class->password) {
		ast_free(class->password);
	}
	if (class->sanitysql) {
		ast_free(class->sanitysql);
	}

	while ((obj = AST_LIST_REMOVE_HEAD(&class->connections, list))) {
		ao2_ref(obj, -1);
	}

	SQLFreeHandle(SQL_HANDLE_ENV, class->env);
	ast_mutex_destroy(&class->lock);
	ast_cond_destroy(&class->cond);
	ast_free(class->sql_text);
}

static void odbc_obj_destructor(void *data)
{
	struct odbc_obj *obj = data;

	odbc_obj_disconnect(obj);
}

static void destroy_table_cache(struct odbc_cache_tables *table)
{
	struct odbc_cache_columns *col;

	ast_debug(1, "Destroying table cache for %s\n", table->table);

	AST_RWLIST_WRLOCK(&table->columns);
	while ((col = AST_RWLIST_REMOVE_HEAD(&table->columns, list))) {
		ast_free(col);
	}
	AST_RWLIST_UNLOCK(&table->columns);
	AST_RWLIST_HEAD_DESTROY(&table->columns);

	ast_free(table);
}

/*!
 * \brief Find or create an entry describing the table specified.
 * \param database Name of an ODBC class on which to query the table
 * \param tablename Tablename to describe
 * \retval A structure describing the table layout, or NULL, if the table is not found or another error occurs.
 * When a structure is returned, the contained columns list will be
 * rdlock'ed, to ensure that it will be retained in memory.
 *
 * XXX This creates a connection and disconnects it. In some situations, the caller of
 * this function has its own connection and could donate it to this function instead of
 * needing to create another one.
 *
 * XXX The automatic readlock of the columns is awkward. It's done because it's possible for
 * multiple threads to have references to the table, and the table is not refcounted. Possible
 * changes here would be
 * * Eliminate the table cache entirely. The use of ast_odbc_find_table() is generally
 *   questionable. The only real good use right now is from ast_realtime_require_field() in
 *   order to make sure the DB has the expected columns in it. Since that is only used sparingly,
 *   the need to cache tables is questionable. Instead, the table structure can be fetched from
 *   the DB directly each time, resulting in a single owner of the data.
 * * Make odbc_cache_tables a refcounted object.
 *
 * \since 1.6.1
 */
struct odbc_cache_tables *ast_odbc_find_table(const char *database, const char *tablename)
{
	struct odbc_cache_tables *tableptr;
	struct odbc_cache_columns *entry;
	char columnname[80];
	SQLLEN sqlptr;
	SQLHSTMT stmt = NULL;
	int res = 0, error = 0;
	struct odbc_obj *obj;

	AST_RWLIST_RDLOCK(&odbc_tables);
	AST_RWLIST_TRAVERSE(&odbc_tables, tableptr, list) {
		if (strcmp(tableptr->connection, database) == 0 && strcmp(tableptr->table, tablename) == 0) {
			break;
		}
	}
	if (tableptr) {
		AST_RWLIST_RDLOCK(&tableptr->columns);
		AST_RWLIST_UNLOCK(&odbc_tables);
		return tableptr;
	}

	if (!(obj = ast_odbc_request_obj(database, 0))) {
		ast_log(LOG_WARNING, "Unable to retrieve database handle for table description '%s@%s'\n", tablename, database);
		AST_RWLIST_UNLOCK(&odbc_tables);
		return NULL;
	}

	/* Table structure not already cached; build it now. */
	do {
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed on connection '%s'!\n", database);
			break;
		}

		res = SQLColumns(stmt, NULL, 0, NULL, 0, (unsigned char *)tablename, SQL_NTS, (unsigned char *)"%", SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			ast_log(LOG_ERROR, "Unable to query database columns on connection '%s'.\n", database);
			break;
		}

		if (!(tableptr = ast_calloc(sizeof(char), sizeof(*tableptr) + strlen(database) + 1 + strlen(tablename) + 1))) {
			ast_log(LOG_ERROR, "Out of memory creating entry for table '%s' on connection '%s'\n", tablename, database);
			break;
		}

		tableptr->connection = (char *)tableptr + sizeof(*tableptr);
		tableptr->table = (char *)tableptr + sizeof(*tableptr) + strlen(database) + 1;
		strcpy(tableptr->connection, database); /* SAFE */
		strcpy(tableptr->table, tablename); /* SAFE */
		AST_RWLIST_HEAD_INIT(&(tableptr->columns));

		while ((res = SQLFetch(stmt)) != SQL_NO_DATA && res != SQL_ERROR) {
			SQLGetData(stmt,  4, SQL_C_CHAR, columnname, sizeof(columnname), &sqlptr);

			if (!(entry = ast_calloc(sizeof(char), sizeof(*entry) + strlen(columnname) + 1))) {
				ast_log(LOG_ERROR, "Out of memory creating entry for column '%s' in table '%s' on connection '%s'\n", columnname, tablename, database);
				error = 1;
				break;
			}
			entry->name = (char *)entry + sizeof(*entry);
			strcpy(entry->name, columnname);

			SQLGetData(stmt,  5, SQL_C_SHORT, &entry->type, sizeof(entry->type), NULL);
			SQLGetData(stmt,  7, SQL_C_LONG, &entry->size, sizeof(entry->size), NULL);
			SQLGetData(stmt,  9, SQL_C_SHORT, &entry->decimals, sizeof(entry->decimals), NULL);
			SQLGetData(stmt, 10, SQL_C_SHORT, &entry->radix, sizeof(entry->radix), NULL);
			SQLGetData(stmt, 11, SQL_C_SHORT, &entry->nullable, sizeof(entry->nullable), NULL);
			SQLGetData(stmt, 16, SQL_C_LONG, &entry->octetlen, sizeof(entry->octetlen), NULL);

			/* Specification states that the octenlen should be the maximum number of bytes
			 * returned in a char or binary column, but it seems that some drivers just set
			 * it to NULL. (Bad Postgres! No biscuit!) */
			if (entry->octetlen == 0) {
				entry->octetlen = entry->size;
			}

			ast_debug(3, "Found %s column with type %hd with len %ld, octetlen %ld, and numlen (%hd,%hd)\n", entry->name, entry->type, (long) entry->size, (long) entry->octetlen, entry->decimals, entry->radix);
			/* Insert column info into column list */
			AST_LIST_INSERT_TAIL(&(tableptr->columns), entry, list);
		}
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);

		AST_RWLIST_INSERT_TAIL(&odbc_tables, tableptr, list);
		AST_RWLIST_RDLOCK(&(tableptr->columns));
		break;
	} while (1);

	AST_RWLIST_UNLOCK(&odbc_tables);

	if (error) {
		destroy_table_cache(tableptr);
		tableptr = NULL;
	}
	ast_odbc_release_obj(obj);
	return tableptr;
}

struct odbc_cache_columns *ast_odbc_find_column(struct odbc_cache_tables *table, const char *colname)
{
	struct odbc_cache_columns *col;
	AST_RWLIST_TRAVERSE(&table->columns, col, list) {
		if (strcasecmp(col->name, colname) == 0) {
			return col;
		}
	}
	return NULL;
}

int ast_odbc_clear_cache(const char *database, const char *tablename)
{
	struct odbc_cache_tables *tableptr;

	AST_RWLIST_WRLOCK(&odbc_tables);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&odbc_tables, tableptr, list) {
		if (strcmp(tableptr->connection, database) == 0 && strcmp(tableptr->table, tablename) == 0) {
			AST_LIST_REMOVE_CURRENT(list);
			destroy_table_cache(tableptr);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END
	AST_RWLIST_UNLOCK(&odbc_tables);
	return tableptr ? 0 : -1;
}

SQLHSTMT ast_odbc_direct_execute(struct odbc_obj *obj, SQLHSTMT (*exec_cb)(struct odbc_obj *obj, void *data), void *data)
{
	struct timeval start;
	SQLHSTMT stmt;

	if (obj->parent->logging) {
		start = ast_tvnow();
	}

	stmt = exec_cb(obj, data);

	if (obj->parent->logging) {
		long execution_time = ast_tvdiff_ms(ast_tvnow(), start);

		if (obj->parent->slowquerylimit && execution_time > obj->parent->slowquerylimit) {
			ast_log(LOG_WARNING, "SQL query '%s' took %ld milliseconds to execute on class '%s', this may indicate a database problem\n",
				obj->sql_text, execution_time, obj->parent->name);
		}

		ast_mutex_lock(&obj->parent->lock);
		if (execution_time > obj->parent->longest_query_execution_time || !obj->parent->sql_text) {
			obj->parent->longest_query_execution_time = execution_time;
			/* Due to the callback nature of the res_odbc API it's not possible to ensure that
			 * the SQL text is removed from the connection in all cases, so only if it becomes the
			 * new longest executing query do we steal the SQL text. In other cases what will happen
			 * is that the SQL text will be freed if the connection is released back to the class or
			 * if a new query is done on the connection.
			 */
			ast_free(obj->parent->sql_text);
			obj->parent->sql_text = obj->sql_text;
			obj->sql_text = NULL;
		}
		ast_mutex_unlock(&obj->parent->lock);
	}

	return stmt;
}

SQLHSTMT ast_odbc_prepare_and_execute(struct odbc_obj *obj, SQLHSTMT (*prepare_cb)(struct odbc_obj *obj, void *data), void *data)
{
	struct timeval start;
	int res = 0;
	SQLHSTMT stmt;

	if (obj->parent->logging) {
		start = ast_tvnow();
	}

	/* This prepare callback may do more than just prepare -- it may also
	 * bind parameters, bind results, etc.  The real key, here, is that
	 * when we disconnect, all handles become invalid for most databases.
	 * We must therefore redo everything when we establish a new
	 * connection. */
	stmt = prepare_cb(obj, data);
	if (!stmt) {
		return NULL;
	}

	res = SQLExecute(stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO) && (res != SQL_NO_DATA)) {
		if (res == SQL_ERROR) {
			ast_odbc_print_errors(SQL_HANDLE_STMT, stmt, "SQL Execute");
		}

		ast_log(LOG_WARNING, "SQL Execute error %d!\n", res);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		stmt = NULL;
	} else if (obj->parent->logging) {
		long execution_time = ast_tvdiff_ms(ast_tvnow(), start);

		if (obj->parent->slowquerylimit && execution_time > obj->parent->slowquerylimit) {
			ast_log(LOG_WARNING, "SQL query '%s' took %ld milliseconds to execute on class '%s', this may indicate a database problem\n",
				obj->sql_text, execution_time, obj->parent->name);
		}

		ast_mutex_lock(&obj->parent->lock);

		/* If this takes the record on longest query execution time, update the parent class
		 * with the information.
		 */
		if (execution_time > obj->parent->longest_query_execution_time || !obj->parent->sql_text) {
			obj->parent->longest_query_execution_time = execution_time;
			ast_free(obj->parent->sql_text);
			obj->parent->sql_text = obj->sql_text;
			obj->sql_text = NULL;
		}
		ast_mutex_unlock(&obj->parent->lock);

		ast_atomic_fetchadd_int(&obj->parent->queries_executed, +1);
	}

	return stmt;
}

int ast_odbc_prepare(struct odbc_obj *obj, SQLHSTMT *stmt, const char *sql)
{
	if (obj->parent->logging) {
		/* It is possible for this connection to be reused without being
		 * released back to the class, so we free what may already exist
		 * and place the new SQL in.
		 */
		ast_free(obj->sql_text);
		obj->sql_text = ast_strdup(sql);
		ast_atomic_fetchadd_int(&obj->parent->prepares_executed, +1);
	}

	return SQLPrepare(stmt, (unsigned char *)sql, SQL_NTS);
}

SQLRETURN ast_odbc_execute_sql(struct odbc_obj *obj, SQLHSTMT *stmt, const char *sql)
{
	if (obj->parent->logging) {
		ast_free(obj->sql_text);
		obj->sql_text = ast_strdup(sql);
		ast_atomic_fetchadd_int(&obj->parent->queries_executed, +1);
	}

	return SQLExecDirect(stmt, (unsigned char *)sql, SQL_NTS);
}

int ast_odbc_smart_execute(struct odbc_obj *obj, SQLHSTMT stmt)
{
	int res = 0;

	res = SQLExecute(stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO) && (res != SQL_NO_DATA)) {
		if (res == SQL_ERROR) {
			ast_odbc_print_errors(SQL_HANDLE_STMT, stmt, "SQL Execute");
		}
	}

	if (obj->parent->logging) {
		ast_atomic_fetchadd_int(&obj->parent->queries_executed, +1);
	}

	return res;
}

SQLRETURN ast_odbc_ast_str_SQLGetData(struct ast_str **buf, int pmaxlen, SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber, SQLSMALLINT TargetType, SQLLEN *StrLen_or_Ind)
{
	SQLRETURN res;

	if (pmaxlen == 0) {
		if (SQLGetData(StatementHandle, ColumnNumber, TargetType, ast_str_buffer(*buf), 0, StrLen_or_Ind) == SQL_SUCCESS_WITH_INFO) {
			ast_str_make_space(buf, *StrLen_or_Ind + 1);
		}
	} else if (pmaxlen > 0) {
		ast_str_make_space(buf, pmaxlen);
	}
	res = SQLGetData(StatementHandle, ColumnNumber, TargetType, ast_str_buffer(*buf), ast_str_size(*buf), StrLen_or_Ind);
	ast_str_update(*buf);

	return res;
}

struct ast_str *ast_odbc_print_errors(SQLSMALLINT handle_type, SQLHANDLE handle, const char *operation)
{
	struct ast_str *errors = ast_str_thread_get(&errors_buf, 16);
	SQLINTEGER nativeerror = 0;
	SQLSMALLINT diagbytes = 0;
	SQLSMALLINT i;
	unsigned char state[10];
	unsigned char diagnostic[256];

	ast_str_reset(errors);
	i = 0;
	while (SQLGetDiagRec(handle_type, handle, ++i, state, &nativeerror,
		diagnostic, sizeof(diagnostic), &diagbytes) == SQL_SUCCESS) {
		ast_str_append(&errors, 0, "%s%s", ast_str_strlen(errors) ? "," : "", state);
		ast_log(LOG_WARNING, "%s returned an error: %s: %s\n", operation, state, diagnostic);
		/* XXX Why is this here? */
		if (i > 10) {
			ast_log(LOG_WARNING, "There are more than 10 diagnostic records! Ignore the rest.\n");
			break;
		}
	}

	return errors;
}

unsigned int ast_odbc_class_get_isolation(struct odbc_class *class)
{
	return class->isolation;
}

unsigned int ast_odbc_class_get_forcecommit(struct odbc_class *class)
{
	return class->forcecommit;
}

const char *ast_odbc_class_get_name(struct odbc_class *class)
{
	return class->name;
}

static int load_odbc_config(void)
{
	static char *cfg = "res_odbc.conf";
	struct ast_config *config;
	struct ast_variable *v;
	char *cat;
	const char *dsn, *username, *password, *sanitysql;
	int enabled, bse, conntimeout, forcecommit, isolation, maxconnections, logging, slowquerylimit;
	struct timeval ncache = { 0, 0 };
	int preconnect = 0, res = 0;
	struct ast_flags config_flags = { 0 };

	struct odbc_class *new;

	config = ast_config_load(cfg, config_flags);
	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load config file res_odbc.conf\n");
		return -1;
	}
	for (cat = ast_category_browse(config, NULL); cat; cat=ast_category_browse(config, cat)) {
		if (!strcasecmp(cat, "ENV")) {
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				setenv(v->name, v->value, 1);
				ast_log(LOG_NOTICE, "Adding ENV var: %s=%s\n", v->name, v->value);
			}
		} else {
			/* Reset all to defaults for each class of odbc connections */
			dsn = username = password = sanitysql = NULL;
			enabled = 1;
			preconnect = 0;
			bse = 1;
			conntimeout = 10;
			forcecommit = 0;
			isolation = SQL_TXN_READ_COMMITTED;
			maxconnections = 1;
			logging = 0;
			slowquerylimit = 5000;
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "pooling") ||
						!strncasecmp(v->name, "share", 5) ||
						!strcasecmp(v->name, "limit") ||
						!strcasecmp(v->name, "idlecheck")) {
					ast_log(LOG_WARNING, "The 'pooling', 'shared_connections', 'limit', and 'idlecheck' options were replaced by 'max_connections'.  See res_odbc.conf.sample.\n");
				} else if (!strcasecmp(v->name, "enabled")) {
					enabled = ast_true(v->value);
				} else if (!strcasecmp(v->name, "pre-connect")) {
					preconnect = ast_true(v->value);
				} else if (!strcasecmp(v->name, "dsn")) {
					dsn = v->value;
				} else if (!strcasecmp(v->name, "username")) {
					username = v->value;
				} else if (!strcasecmp(v->name, "password")) {
					password = v->value;
				} else if (!strcasecmp(v->name, "sanitysql")) {
					sanitysql = v->value;
				} else if (!strcasecmp(v->name, "backslash_is_escape")) {
					bse = ast_true(v->value);
				} else if (!strcasecmp(v->name, "connect_timeout")) {
					if (sscanf(v->value, "%d", &conntimeout) != 1 || conntimeout < 1) {
						ast_log(LOG_WARNING, "connect_timeout must be a positive integer\n");
						conntimeout = 10;
					}
				} else if (!strcasecmp(v->name, "negative_connection_cache")) {
					double dncache;
					if (sscanf(v->value, "%lf", &dncache) != 1 || dncache < 0) {
						ast_log(LOG_WARNING, "negative_connection_cache must be a non-negative integer\n");
						/* 5 minutes sounds like a reasonable default */
						ncache.tv_sec = 300;
						ncache.tv_usec = 0;
					} else {
						ncache.tv_sec = (int)dncache;
						ncache.tv_usec = (dncache - ncache.tv_sec) * 1000000;
					}
				} else if (!strcasecmp(v->name, "forcecommit")) {
					forcecommit = ast_true(v->value);
				} else if (!strcasecmp(v->name, "isolation")) {
					if ((isolation = ast_odbc_text2isolation(v->value)) == 0) {
						ast_log(LOG_ERROR, "Unrecognized value for 'isolation': '%s' in section '%s'\n", v->value, cat);
						isolation = SQL_TXN_READ_COMMITTED;
					}
				} else if (!strcasecmp(v->name, "max_connections")) {
					if (sscanf(v->value, "%30d", &maxconnections) != 1 || maxconnections < 1) {
						ast_log(LOG_WARNING, "max_connections must be a positive integer\n");
						maxconnections = 1;
                                        }
				} else if (!strcasecmp(v->name, "logging")) {
					logging = ast_true(v->value);
				} else if (!strcasecmp(v->name, "slow_query_limit")) {
					if (sscanf(v->value, "%30d", &slowquerylimit) != 1) {
						ast_log(LOG_WARNING, "slow_query_limit must be a positive integer\n");
						slowquerylimit = 5000;
					}
				}
			}

			if (enabled && !ast_strlen_zero(dsn)) {
				new = ao2_alloc(sizeof(*new), odbc_class_destructor);

				if (!new) {
					res = -1;
					break;
				}

				SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &new->env);
				res = SQLSetEnvAttr(new->env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);

				if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
					ast_log(LOG_WARNING, "res_odbc: Error SetEnv\n");
					ao2_ref(new, -1);
					return res;
				}

				new->backslash_is_escape = bse ? 1 : 0;
				new->forcecommit = forcecommit ? 1 : 0;
				new->isolation = isolation;
				new->conntimeout = conntimeout;
				new->negative_connection_cache = ncache;
				new->maxconnections = maxconnections;
				new->logging = logging;
				new->slowquerylimit = slowquerylimit;

				if (cat)
					ast_copy_string(new->name, cat, sizeof(new->name));
				if (dsn)
					ast_copy_string(new->dsn, dsn, sizeof(new->dsn));
				if (username && !(new->username = ast_strdup(username))) {
					ao2_ref(new, -1);
					break;
				}
				if (password && !(new->password = ast_strdup(password))) {
					ao2_ref(new, -1);
					break;
				}
				if (sanitysql && !(new->sanitysql = ast_strdup(sanitysql))) {
					ao2_ref(new, -1);
					break;
				}

				ast_mutex_init(&new->lock);
				ast_cond_init(&new->cond, NULL);

				odbc_register_class(new, preconnect);
				ast_log(LOG_NOTICE, "Registered ODBC class '%s' dsn->[%s]\n", cat, dsn);
				ao2_ref(new, -1);
				new = NULL;
			}
		}
	}
	ast_config_destroy(config);
	return res;
}

static char *handle_cli_odbc_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator aoi;
	struct odbc_class *class;
	int length = 0;
	int which = 0;
	char *ret = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "odbc show";
		e->usage =
				"Usage: odbc show [class]\n"
				"       List settings of a particular ODBC class or,\n"
				"       if not specified, all classes.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos != 2)
			return NULL;
		length = strlen(a->word);
		aoi = ao2_iterator_init(class_container, 0);
		while ((class = ao2_iterator_next(&aoi))) {
			if (!strncasecmp(a->word, class->name, length) && ++which > a->n) {
				ret = ast_strdup(class->name);
			}
			ao2_ref(class, -1);
			if (ret) {
				break;
			}
		}
		ao2_iterator_destroy(&aoi);
		if (!ret && !strncasecmp(a->word, "all", length) && ++which > a->n) {
			ret = ast_strdup("all");
		}
		return ret;
	}

	ast_cli(a->fd, "\nODBC DSN Settings\n");
	ast_cli(a->fd,   "-----------------\n\n");
	aoi = ao2_iterator_init(class_container, 0);
	while ((class = ao2_iterator_next(&aoi))) {
		if ((a->argc == 2) || (a->argc == 3 && !strcmp(a->argv[2], "all")) || (!strcmp(a->argv[2], class->name))) {
			char timestr[80];
			struct ast_tm tm;

			ast_cli(a->fd, "  Name:   %s\n  DSN:    %s\n", class->name, class->dsn);

			if (class->last_negative_connect.tv_sec > 0) {
				ast_localtime(&class->last_negative_connect, &tm, NULL);
				ast_strftime(timestr, sizeof(timestr), "%Y-%m-%d %T", &tm);
				ast_cli(a->fd, "    Last fail connection attempt: %s\n", timestr);
			}

			ast_cli(a->fd, "    Number of active connections: %zd (out of %d)\n", class->connection_cnt, class->maxconnections);
			ast_cli(a->fd, "    Logging: %s\n", class->logging ? "Enabled" : "Disabled");
			if (class->logging) {
				ast_cli(a->fd, "    Number of prepares executed: %d\n", class->prepares_executed);
				ast_cli(a->fd, "    Number of queries executed: %d\n", class->queries_executed);
				ast_mutex_lock(&class->lock);
				if (class->sql_text) {
					ast_cli(a->fd, "    Longest running SQL query: %s (%ld milliseconds)\n", class->sql_text, class->longest_query_execution_time);
				}
				ast_mutex_unlock(&class->lock);
			}
			ast_cli(a->fd, "\n");
		}
		ao2_ref(class, -1);
	}
	ao2_iterator_destroy(&aoi);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_odbc[] = {
	AST_CLI_DEFINE(handle_cli_odbc_show, "List ODBC DSN(s)")
};

static void odbc_register_class(struct odbc_class *class, int preconnect)
{
	struct odbc_obj *obj;

	ao2_link(class_container, class);
	/* I still have a reference in the caller, so a deref is NOT missing here. */

	if (!preconnect) {
		return;
	}

	/* Request and release builds a connection */
	obj = ast_odbc_request_obj(class->name, 0);
	if (obj) {
		ast_odbc_release_obj(obj);
	}

	return;
}

void ast_odbc_release_obj(struct odbc_obj *obj)
{
	struct odbc_class *class = obj->parent;

	ast_debug(2, "Releasing ODBC handle %p into pool\n", obj);

	/* The odbc_obj only holds a reference to the class when it is
	 * actively being used. This guarantees no circular reference
	 * between odbc_class and odbc_obj. Since it is being released
	 * we also release our class reference. If a reload occurred before
	 * the class will go away automatically once all odbc_obj are
	 * released back.
	 */
	obj->parent = NULL;

	/* Free the SQL text so that the next user of this connection has
	 * a fresh start.
	 */
	ast_free(obj->sql_text);
	obj->sql_text = NULL;

	ast_mutex_lock(&class->lock);
	AST_LIST_INSERT_HEAD(&class->connections, obj, list);
	ast_cond_signal(&class->cond);
	ast_mutex_unlock(&class->lock);

	ao2_ref(class, -1);
}

int ast_odbc_backslash_is_escape(struct odbc_obj *obj)
{
	return obj->parent->backslash_is_escape;
}

static int aoro2_class_cb(void *obj, void *arg, int flags)
{
	struct odbc_class *class = obj;
	char *name = arg;
	if (!strcmp(class->name, name) && !class->delme) {
		return CMP_MATCH | CMP_STOP;
	}
	return 0;
}

unsigned int ast_odbc_get_max_connections(const char *name)
{
	struct odbc_class *class;
	unsigned int max_connections;

	class = ao2_callback(class_container, 0, aoro2_class_cb, (char *) name);
	if (!class) {
		return 0;
	}

	max_connections = class->maxconnections;
	ao2_ref(class, -1);

	return max_connections;
}

/*
 * \brief Determine if the connection has died.
 *
 * \param connection The connection to check
 * \param class The ODBC class
 * \retval 1 Yep, it's dead
 * \retval 0 It's alive and well
 */
static int connection_dead(struct odbc_obj *connection, struct odbc_class *class)
{
	char *test_sql = "select 1";
	SQLINTEGER dead;
	SQLRETURN res;
	SQLHSTMT stmt;

	res = SQLGetConnectAttr(connection->con, SQL_ATTR_CONNECTION_DEAD, &dead, 0, 0);
	if (SQL_SUCCEEDED(res)) {
		return dead == SQL_CD_TRUE ? 1 : 0;
	}

	/* If the Driver doesn't support SQL_ATTR_CONNECTION_DEAD do a
	 * probing query instead
	 */
	res = SQLAllocHandle(SQL_HANDLE_STMT, connection->con, &stmt);
	if (!SQL_SUCCEEDED(res)) {
		return 1;
	}

	if (!ast_strlen_zero(class->sanitysql)) {
		test_sql = class->sanitysql;
	}

	res = SQLPrepare(stmt, (unsigned char *)test_sql, SQL_NTS);
	if (!SQL_SUCCEEDED(res)) {
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return 1;
	}

	res = SQLExecute(stmt);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

	return SQL_SUCCEEDED(res) ? 0 : 1;
}

struct odbc_obj *_ast_odbc_request_obj2(const char *name, struct ast_flags flags, const char *file, const char *function, int lineno)
{
	struct odbc_obj *obj = NULL;
	struct odbc_class *class;

	if (!(class = ao2_callback(class_container, 0, aoro2_class_cb, (char *) name))) {
		ast_debug(1, "Class '%s' not found!\n", name);
		return NULL;
	}

	ast_mutex_lock(&class->lock);

	while (!obj) {
		obj = AST_LIST_REMOVE_HEAD(&class->connections, list);

		if (!obj) {
			if (class->connection_cnt < class->maxconnections) {
				/* If no connection is immediately available establish a new
				 * one if allowed. If we try and fail we give up completely as
				 * we could go into an infinite loop otherwise.
				 */
				obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor);
				if (!obj) {
					break;
				}

				obj->parent = ao2_bump(class);
				if (odbc_obj_connect(obj) == ODBC_FAIL) {
					ao2_ref(obj->parent, -1);
					ao2_ref(obj, -1);
					obj = NULL;
					break;
				}

				class->connection_cnt++;
				ast_debug(2, "Created ODBC handle %p on class '%s', new count is %zd\n", obj,
					name, class->connection_cnt);
			} else {
				/* Otherwise if we're not allowed to create a new one we
				 * wait for another thread to give up the connection they
				 * own.
				 */
				ast_cond_wait(&class->cond, &class->lock);
			}
		} else if (connection_dead(obj, class)) {
			/* If the connection is dead try to grab another functional one from the
			 * pool instead of trying to resurrect this one.
			 */
			ao2_ref(obj, -1);
			obj = NULL;
			class->connection_cnt--;
			ast_debug(2, "ODBC handle %p dead - removing from class '%s', new count is %zd\n",
				obj, name, class->connection_cnt);
		} else {
			/* We successfully grabbed a connection from the pool and all is well!
			 */
			obj->parent = ao2_bump(class);
			ast_debug(2, "Reusing ODBC handle %p from class '%s'\n", obj, name);
		}
	}

	ast_mutex_unlock(&class->lock);
	ao2_ref(class, -1);

	return obj;
}

struct odbc_obj *_ast_odbc_request_obj(const char *name, int check, const char *file, const char *function, int lineno)
{
	struct ast_flags flags = { check ? RES_ODBC_SANITY_CHECK : 0 };
	/* XXX New flow means that the "check" parameter doesn't do anything. We're requesting
	 * a connection from ODBC. We'll either get a new one, which obviously is already connected, or
	 * we'll get one from the ODBC connection pool. In that case, it will ensure to only give us a
	 * live connection
	 */
	return _ast_odbc_request_obj2(name, flags, file, function, lineno);
}

static odbc_status odbc_obj_disconnect(struct odbc_obj *obj)
{
	int res;
	SQLINTEGER err;
	short int mlen;
	unsigned char msg[200], state[10];
	SQLHDBC con;

	/* Nothing to disconnect */
	if (!obj->con) {
		return ODBC_SUCCESS;
	}

	con = obj->con;
	obj->con = NULL;
	res = SQLDisconnect(con);

	if ((res = SQLFreeHandle(SQL_HANDLE_DBC, con)) == SQL_SUCCESS) {
		ast_debug(3, "Database handle %p (connection %p) deallocated\n", obj, con);
	} else {
		SQLGetDiagRec(SQL_HANDLE_DBC, con, 1, state, &err, msg, 100, &mlen);
		ast_log(LOG_WARNING, "Unable to deallocate database handle %p? %d errno=%d %s\n", con, res, (int)err, msg);
	}

	return ODBC_SUCCESS;
}

static odbc_status odbc_obj_connect(struct odbc_obj *obj)
{
	int res;
	SQLINTEGER err;
	short int mlen;
	unsigned char msg[200], state[10];
#ifdef NEEDTRACE
	SQLINTEGER enable = 1;
	char *tracefile = "/tmp/odbc.trace";
#endif
	SQLHDBC con;
	long int negative_cache_expiration;

	ast_assert(obj->con == NULL);
	ast_debug(3, "Connecting %s(%p)\n", obj->parent->name, obj);

	/* Dont connect while server is marked as unreachable via negative_connection_cache */
	negative_cache_expiration = obj->parent->last_negative_connect.tv_sec + obj->parent->negative_connection_cache.tv_sec;
	if (time(NULL) < negative_cache_expiration) {
		ast_log(LOG_WARNING, "Not connecting to %s. Negative connection cache for %ld seconds\n", obj->parent->name, negative_cache_expiration - time(NULL));
		return ODBC_FAIL;
	}

	res = SQLAllocHandle(SQL_HANDLE_DBC, obj->parent->env, &con);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "res_odbc: Error AllocHDB %d\n", res);
		obj->parent->last_negative_connect = ast_tvnow();
		return ODBC_FAIL;
	}
	SQLSetConnectAttr(con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)(long) obj->parent->conntimeout, 0);
	SQLSetConnectAttr(con, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER *)(long) obj->parent->conntimeout, 0);
#ifdef NEEDTRACE
	SQLSetConnectAttr(con, SQL_ATTR_TRACE, &enable, SQL_IS_INTEGER);
	SQLSetConnectAttr(con, SQL_ATTR_TRACEFILE, tracefile, strlen(tracefile));
#endif

	res = SQLConnect(con,
		   (SQLCHAR *) obj->parent->dsn, SQL_NTS,
		   (SQLCHAR *) obj->parent->username, SQL_NTS,
		   (SQLCHAR *) obj->parent->password, SQL_NTS);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		SQLGetDiagRec(SQL_HANDLE_DBC, con, 1, state, &err, msg, 100, &mlen);
		obj->parent->last_negative_connect = ast_tvnow();
		ast_log(LOG_WARNING, "res_odbc: Error SQLConnect=%d errno=%d %s\n", res, (int)err, msg);
		if ((res = SQLFreeHandle(SQL_HANDLE_DBC, con)) != SQL_SUCCESS) {
			SQLGetDiagRec(SQL_HANDLE_DBC, con, 1, state, &err, msg, 100, &mlen);
			ast_log(LOG_WARNING, "Unable to deallocate database handle %p? %d errno=%d %s\n", con, res, (int)err, msg);
		}
		return ODBC_FAIL;
	} else {
		ast_debug(3, "res_odbc: Connected to %s [%s (%p)]\n", obj->parent->name, obj->parent->dsn, obj);
	}

	obj->con = con;
	return ODBC_SUCCESS;
}

static int reload(void)
{
	struct odbc_cache_tables *table;
	struct odbc_class *class;
	struct ao2_iterator aoi = ao2_iterator_init(class_container, 0);

	/* First, mark all to be purged */
	while ((class = ao2_iterator_next(&aoi))) {
		class->delme = 1;
		ao2_ref(class, -1);
	}
	ao2_iterator_destroy(&aoi);

	load_odbc_config();

	aoi = ao2_iterator_init(class_container, 0);
	while ((class = ao2_iterator_next(&aoi))) {
		if (class->delme) {
			ao2_unlink(class_container, class);
		}
		ao2_ref(class, -1);
	}
	ao2_iterator_destroy(&aoi);

	/* Empty the cache; it will get rebuilt the next time the tables are needed. */
	AST_RWLIST_WRLOCK(&odbc_tables);
	while ((table = AST_RWLIST_REMOVE_HEAD(&odbc_tables, list))) {
		destroy_table_cache(table);
	}
	AST_RWLIST_UNLOCK(&odbc_tables);

	return 0;
}

static int unload_module(void)
{
	ao2_cleanup(class_container);
	ast_cli_unregister_multiple(cli_odbc, ARRAY_LEN(cli_odbc));

	return 0;
}

static int load_module(void)
{
	class_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, ao2_match_by_addr);
	if (!class_container) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (load_odbc_config() == -1) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_module_shutdown_ref(ast_module_info->self);
	ast_cli_register_multiple(cli_odbc, ARRAY_LEN(cli_odbc));

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "ODBC resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_REALTIME_DEPEND,
);
