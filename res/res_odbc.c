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
 * \arg See also: \ref cdr_odbc.c
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

/*** DOCUMENTATION
	<configInfo name="res_odbc" language="en_US">
		<synopsis>ODBC resource manager</synopsis>
		<configFile name="res_odbc.conf">
			<configObject name="env">
				<since>
					<version>1.6.0</version>
				</since>
				<synopsis>Environment variable injection</synopsis>
				<description>
					<para>The <literal>[ENV]</literal> section is special: every name/value pair
					in it is pushed into the process environment via <literal>setenv()</literal>
					before any DSN is opened. This is useful for passing site-specific tunables
					(for example <literal>PGCONNECT_TIMEOUT</literal>) to the underlying ODBC
					driver without having to set them in the shell that launches Asterisk.</para>
					<para>Variable names are not validated; anything you put in this section becomes
					an environment variable.</para>
				</description>
			</configObject>
			<configObject name="class">
				<since>
					<version>1.6.0</version>
				</since>
				<synopsis>One ODBC connection class (a named DSN with its connection pool)</synopsis>
				<description>
					<para>Each non-<literal>[ENV]</literal> section in <filename>res_odbc.conf</filename>
					defines an ODBC class. The section name is the identifier consumers
					(<literal>func_odbc</literal>, <literal>res_config_odbc</literal>,
					<literal>cdr_adaptive_odbc</literal>, etc.) use to refer to this connection.</para>
				</description>
				<configOption name="enabled" default="yes">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Whether this class is loaded</synopsis>
					<description>
						<para>Set to <literal>no</literal> to leave the class defined in the
						file but not registered. Useful for staging DSN changes without removing
						the section.</para>
					</description>
				</configOption>
				<configOption name="dsn" default="">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Name of the DSN to look up in <filename>/etc/odbc.ini</filename></synopsis>
					<description>
						<para>Required. If empty, the class is silently skipped at load time.</para>
					</description>
				</configOption>
				<configOption name="username" default="">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Username passed to the ODBC driver at connect time</synopsis>
				</configOption>
				<configOption name="password" default="">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Password passed to the ODBC driver at connect time</synopsis>
				</configOption>
				<configOption name="sanitysql" default="">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>SQL fragment to run before reusing a pooled connection</synopsis>
					<description>
						<para>If set, the connection is validated by running this SQL each time
						it is reused. A typical value is <literal>SELECT 1</literal>. Connections
						whose sanity check fails are closed and reopened.</para>
					</description>
				</configOption>
				<configOption name="pre-connect" default="no">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Open one connection at module load time</synopsis>
					<description>
						<para>If <literal>yes</literal>, one connection is opened immediately at
						module load (or reload) so the first request does not pay the connect
						cost. The remainder of the pool is filled lazily as load demands.</para>
					</description>
				</configOption>
				<configOption name="max_connections" default="1">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Maximum size of the connection pool</synopsis>
					<description>
						<para>Hard upper bound on simultaneous open ODBC connections for this
						class. Requests beyond this limit block waiting for a connection to be
						released. Must be a positive integer; values below 1 are coerced to 1
						with a warning.</para>
					</description>
				</configOption>
				<configOption name="connect_timeout" default="10">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Per-connect timeout in seconds</synopsis>
					<description>
						<para>Maximum time the underlying <literal>SQLConnect</literal> may take
						before being abandoned. Must be a positive integer; values below 1 are
						coerced to 10 with a warning.</para>
					</description>
				</configOption>
				<configOption name="negative_connection_cache" default="0">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Seconds to suppress reconnect attempts after a failure</synopsis>
					<description>
						<para>When a connect attempt fails, further attempts to that DSN are
						short-circuited for this many seconds (fractional values accepted) so
						a flapping database does not turn into a tight reconnect loop. Must be
						a non-negative number; bad input falls back to 300 seconds with a
						warning.</para>
					</description>
				</configOption>
				<configOption name="forcecommit" default="no">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Auto-commit dangling transactions on connection release</synopsis>
					<description>
						<para>If <literal>yes</literal>, an uncommitted transaction is committed
						(rather than rolled back) when the connection is released back to the
						pool. Defaults to rollback because committing a transaction the caller
						forgot to finish is usually worse than dropping its work.</para>
					</description>
				</configOption>
				<configOption name="isolation" default="read_committed">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Default transaction isolation level</synopsis>
					<description>
						<para>Accepted values: <literal>read_committed</literal>,
						<literal>read_uncommitted</literal>, <literal>repeatable_read</literal>,
						<literal>serializable</literal>. Matching is case-insensitive and
						loose-prefix; for example <literal>ser</literal> selects serializable.</para>
					</description>
				</configOption>
				<configOption name="backslash_is_escape" default="yes">
					<since>
						<version>1.6.0</version>
					</since>
					<synopsis>Whether the database treats <literal>\</literal> as a SQL escape</synopsis>
					<description>
						<para>Affects how <literal>res_config_odbc</literal>,
						<literal>cdr_adaptive_odbc</literal> and <literal>cel_odbc</literal>
						quote LIKE-pattern values. Set to <literal>no</literal> for engines like
						PostgreSQL that need an explicit <literal>ESCAPE</literal> clause.</para>
					</description>
				</configOption>
				<configOption name="logging" default="no">
					<since>
						<version>13.0.0</version>
					</since>
					<synopsis>Track per-class query/prepare counters and slow queries</synopsis>
					<description>
						<para>When enabled, <literal>odbc show</literal> reports the number of
						prepares and queries executed and tracks the longest-running query
						observed. Mostly useful for diagnostics; carries a small per-query
						overhead.</para>
					</description>
				</configOption>
				<configOption name="slow_query_limit" default="5000">
					<since>
						<version>13.0.0</version>
					</since>
					<synopsis>Threshold in milliseconds for slow-query reporting</synopsis>
					<description>
						<para>Only consulted when <literal>logging</literal> is enabled. Queries
						that exceed this duration are recorded as the longest-running query for
						the class and reported by <literal>odbc show</literal>.</para>
					</description>
				</configOption>
				<configOption name="cache_type" default="stack">
					<since>
						<version>11.0.0</version>
					</since>
					<synopsis>How the connection pool reuses idle connections</synopsis>
					<description>
						<para>Accepted values:</para>
						<enumlist>
							<enum name="stack">
								<para>LIFO — the most-recently-released connection is reused
								first. Concentrates work on a small number of connections,
								letting idle ones drop out of the cache.</para>
							</enum>
							<enum name="queue"><para>FIFO round-robin (alias: <literal>roundrobin</literal>,
							<literal>rr</literal>). Spreads work evenly across the whole pool.</para>
							</enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="cache_size" default="-1">
					<since>
						<version>11.0.0</version>
					</since>
					<synopsis>Maximum number of cached idle connections</synopsis>
					<description>
						<para>When more than this many connections are idle, the oldest is closed.
						The special value <literal>-1</literal> means uncapped (every connection
						up to <literal>max_connections</literal> is cached).</para>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/res_odbc.h"
#include "asterisk/time.h"
#include "asterisk/astobj2.h"
#include "asterisk/app.h"
#include "asterisk/strings.h"
#include "asterisk/stringfields.h"
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
	unsigned int cache_is_queue:1;       /*!< Connection cache should be a queue (round-robin use) rather than a stack (last release, first re-use) */
	unsigned int preconnect:1;           /*!< Open one connection immediately when registering this class */
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
	/*! Maximum number of cached connections, default is maxconnections */
	unsigned int max_cache_size;
	/*! Current cached connection count, when cache_size will exceed max_cache_size, longest-idle connection will be dropped from the cache */
	unsigned int cur_cache;
};

static struct ao2_container *class_container;

static struct ao2_container *odbc_class_container_alloc(void)
{
	return ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, ao2_match_by_addr);
}

/*! \brief Parser-only config object for one [class] section in res_odbc.conf.
 *
 * Populated by the config_options framework (one instance per category)
 * and consumed by odbc_apply_config() to build the runtime odbc_class
 * in class_container. Released once the config snapshot has been
 * applied.
 */
struct odbc_class_cfg {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(dsn);
		AST_STRING_FIELD(username);
		AST_STRING_FIELD(password);
		AST_STRING_FIELD(sanitysql);
	);
	int enabled;
	int preconnect;
	int backslash_is_escape;
	int forcecommit;
	int cache_is_queue;
	int logging;
	unsigned int isolation;
	unsigned int conntimeout;
	unsigned int maxconnections;
	unsigned int slowquerylimit;
	unsigned int max_cache_size;
	struct timeval negative_connection_cache;
};

/*! \brief Snapshot of the parsed res_odbc.conf — held by aco's global obj. */
struct odbc_config {
	struct ao2_container *classes;
};

static AO2_GLOBAL_OBJ_STATIC(odbc_global_cfg);

static AST_RWLIST_HEAD_STATIC(odbc_tables, odbc_cache_tables);

static odbc_status odbc_obj_connect(struct odbc_obj *obj);
static odbc_status odbc_obj_disconnect(struct odbc_obj *obj);

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
		if (!SQL_SUCCEEDED(res)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed on connection '%s'!\n", database);
			break;
		}

		res = SQLColumns(stmt, NULL, 0, NULL, 0, (unsigned char *)tablename, SQL_NTS, (unsigned char *)"%", SQL_NTS);
		if (!SQL_SUCCEEDED(res)) {
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
	if (!SQL_SUCCEEDED(res) && (res != SQL_NO_DATA)) {
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
	if (!SQL_SUCCEEDED(res) && (res != SQL_NO_DATA)) {
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

/*! \internal \brief Destroy a parsed odbc_class_cfg item. */
static void odbc_class_cfg_destructor(void *obj)
{
	struct odbc_class_cfg *cfg = obj;
	ast_string_field_free_memory(cfg);
}

/*! \internal \brief Allocate a parsed odbc_class_cfg item for category \a cat. */
static void *odbc_class_cfg_alloc(const char *cat)
{
	struct odbc_class_cfg *cfg;

	cfg = ao2_alloc(sizeof(*cfg), odbc_class_cfg_destructor);
	if (!cfg) {
		return NULL;
	}
	if (ast_string_field_init(cfg, 256)) {
		ao2_ref(cfg, -1);
		return NULL;
	}
	ast_string_field_set(cfg, name, cat);
	return cfg;
}

static int odbc_class_cfg_hash(const void *obj, int flags)
{
	const struct odbc_class_cfg *cfg;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		cfg = obj;
		key = cfg->name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_case_hash(key);
}

static int odbc_class_cfg_cmp(void *obj, void *arg, int flags)
{
	const struct odbc_class_cfg *cfg = obj;
	const struct odbc_class_cfg *other;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = arg;
		break;
	case OBJ_SEARCH_OBJECT:
		other = arg;
		key = other->name;
		break;
	default:
		return CMP_STOP;
	}
	return strcasecmp(cfg->name, key) ? 0 : CMP_MATCH;
}

/*! \internal \brief aco item_find callback: look up a cfg by category name. */
static void *odbc_class_cfg_find(struct ao2_container *container, const char *cat)
{
	return ao2_find(container, cat, OBJ_SEARCH_KEY);
}

/*! \internal \brief Free a snapshot of the whole config (called by ao2). */
static void odbc_config_destructor(void *obj)
{
	struct odbc_config *cfg = obj;
	ao2_cleanup(cfg->classes);
}

/*! \internal \brief Snapshot allocator (used by aco). */
static void *odbc_config_alloc(void)
{
	struct odbc_config *cfg;

	cfg = ao2_alloc(sizeof(*cfg), odbc_config_destructor);
	if (!cfg) {
		return NULL;
	}
	cfg->classes = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 11,
		odbc_class_cfg_hash, NULL, odbc_class_cfg_cmp);
	if (!cfg->classes) {
		ao2_ref(cfg, -1);
		return NULL;
	}
	return cfg;
}

/*! \internal \brief Custom handler: isolation level (text → SQL_TXN_* enum).
 *
 * Performs the loose-prefix matching documented in res_odbc.conf.sample
 * (e.g. 'serpent' → SERIALIZABLE, 'reptile' → REPEATABLE_READ) via
 * ast_odbc_text2isolation. Unrecognized input logs LOG_ERROR and the
 * field is left at READ_COMMITTED so the rest of the class still
 * registers; rejecting the parse for a single bad value would take the
 * whole DSN offline, which is worse than running with a default.
 */
static int isolation_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct odbc_class_cfg *cfg = obj;
	int iso = ast_odbc_text2isolation(var->value);
	if (iso == 0) {
		ast_log(LOG_ERROR, "Unrecognized value for 'isolation': '%s' in section '%s'\n",
			var->value, cfg->name);
		cfg->isolation = SQL_TXN_READ_COMMITTED;
		return 0;
	}
	cfg->isolation = iso;
	return 0;
}

/*! \internal \brief Custom handler: cache_type (text → bool cache_is_queue). */
static int cache_type_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct odbc_class_cfg *cfg = obj;
	cfg->cache_is_queue = !strcasecmp(var->value, "rr") ||
		!strcasecmp(var->value, "roundrobin") ||
		!strcasecmp(var->value, "queue");
	return 0;
}

/*! \internal \brief Custom handler: cache_size with "-1" → UINT_MAX. */
static int cache_size_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct odbc_class_cfg *cfg = obj;
	if (!strcasecmp(var->value, "-1")) {
		cfg->max_cache_size = UINT_MAX;
		return 0;
	}
	if (sscanf(var->value, "%u", &cfg->max_cache_size) != 1) {
		ast_log(LOG_WARNING, "cache_size must be a non-negative integer or -1 (infinite)\n");
	}
	return 0;
}

/*! \internal \brief Custom handler: negative_connection_cache (double seconds → struct timeval).
 *
 * Bad input logs a WARNING and the field falls back to 5 minutes —
 * permissive on unparseable input rather than failing the parse,
 * because a misconfigured negative-cache value is not severe enough
 * to take a working DSN offline.
 */
static int negative_connection_cache_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct odbc_class_cfg *cfg = obj;
	double dncache;

	if (sscanf(var->value, "%lf", &dncache) != 1 || dncache < 0) {
		ast_log(LOG_WARNING, "negative_connection_cache must be a non-negative integer\n");
		cfg->negative_connection_cache.tv_sec = 300;
		cfg->negative_connection_cache.tv_usec = 0;
		return 0;
	}
	cfg->negative_connection_cache.tv_sec = (int)dncache;
	cfg->negative_connection_cache.tv_usec =
		(dncache - cfg->negative_connection_cache.tv_sec) * 1000000;
	return 0;
}

/*! \internal \brief Catch-all for unrecognized option names.
 *
 * aco's default for an unrecognized option is LOG_ERROR plus a failed
 * parse for the whole category — strict enough to break a class
 * registration on a typo. This handler is registered with an
 * empty-prefix ACO_PREFIX so it matches anything aco's exact-match
 * lookup did not. It accepts the line and discards the value so the
 * rest of the class still parses.
 *
 * The four obsolete pool-related names (pooling, share*, limit,
 * idlecheck) were replaced by max_connections years ago; they get a
 * specific warning pointing at the replacement. Anything else gets a
 * generic "unknown option" warning so operator typos are visible in
 * the log instead of being silently dropped (the legacy parser had
 * neither a typo warning nor an obsolete-name warning beyond the four
 * pool names).
 */
static int unknown_option_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct odbc_class_cfg *cfg = obj;

	if (!strcasecmp(var->name, "pooling") ||
			!strncasecmp(var->name, "share", 5) ||
			!strcasecmp(var->name, "limit") ||
			!strcasecmp(var->name, "idlecheck")) {
		ast_log(LOG_WARNING,
			"The 'pooling', 'shared_connections', 'limit', and 'idlecheck' options "
			"were replaced by 'max_connections'.  See res_odbc.conf.sample.\n");
	} else {
		ast_log(LOG_WARNING,
			"Unknown option '%s' in section '%s' of res_odbc.conf — ignoring.\n",
			var->name, cfg->name);
	}
	return 0;
}

static struct aco_type odbc_class_aco_type = {
	.type = ACO_ITEM,
	.name = "class",
	/* Match every category except [ENV]; the env_ignore_type below
	 * matches and ignores [ENV]. odbc_apply_env_section() does the
	 * actual setenv() work for [ENV] before aco runs. */
	.category_match = ACO_BLACKLIST_EXACT,
	.category = "ENV",
	.item_alloc = odbc_class_cfg_alloc,
	.item_find = odbc_class_cfg_find,
	.item_offset = offsetof(struct odbc_config, classes),
};

static struct aco_type env_ignore_type = {
	.type = ACO_IGNORE,
	.name = "env",
	.category_match = ACO_WHITELIST_EXACT,
	.category = "ENV",
};

static struct aco_type *odbc_class_aco_types[] = ACO_TYPES(&odbc_class_aco_type);

static struct aco_file res_odbc_conf_file = {
	.filename = "res_odbc.conf",
	.types = ACO_TYPES(&odbc_class_aco_type, &env_ignore_type),
};

static void odbc_apply_config(void);

CONFIG_INFO_STANDARD(odbc_cfg_info, odbc_global_cfg, odbc_config_alloc,
	.files = ACO_FILES(&res_odbc_conf_file),
	.post_apply_config = odbc_apply_config,
);

/*! \internal \brief Build a new odbc_class from a parsed cfg item.
 * \retval NULL allocation or env-handle setup failed (already logged).
 */
static struct odbc_class *odbc_class_build(const struct odbc_class_cfg *cfg)
{
	struct odbc_class *class;
	int res;

	class = ao2_alloc(sizeof(*class), odbc_class_destructor);
	if (!class) {
		return NULL;
	}

	SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &class->env);
	res = SQLSetEnvAttr(class->env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(LOG_WARNING, "res_odbc: Error SetEnv\n");
		ao2_ref(class, -1);
		return NULL;
	}

	ast_copy_string(class->name, cfg->name, sizeof(class->name));
	ast_copy_string(class->dsn, cfg->dsn, sizeof(class->dsn));
	if (!ast_strlen_zero(cfg->username) && !(class->username = ast_strdup(cfg->username))) {
		ao2_ref(class, -1);
		return NULL;
	}
	if (!ast_strlen_zero(cfg->password) && !(class->password = ast_strdup(cfg->password))) {
		ao2_ref(class, -1);
		return NULL;
	}
	if (!ast_strlen_zero(cfg->sanitysql) && !(class->sanitysql = ast_strdup(cfg->sanitysql))) {
		ao2_ref(class, -1);
		return NULL;
	}

	class->backslash_is_escape = cfg->backslash_is_escape ? 1 : 0;
	class->forcecommit = cfg->forcecommit ? 1 : 0;
	class->cache_is_queue = cfg->cache_is_queue ? 1 : 0;
	class->preconnect = cfg->preconnect ? 1 : 0;
	class->isolation = cfg->isolation;
	class->conntimeout = cfg->conntimeout;
	class->maxconnections = cfg->maxconnections;
	class->negative_connection_cache = cfg->negative_connection_cache;
	class->logging = cfg->logging;
	class->slowquerylimit = cfg->slowquerylimit;
	class->max_cache_size = cfg->max_cache_size;
	class->cur_cache = 0;

	ast_mutex_init(&class->lock);
	ast_cond_init(&class->cond, NULL);

	return class;
}

static void odbc_preconnect_class(struct odbc_class *class)
{
	struct odbc_obj *obj;

	if (!class->preconnect) {
		return;
	}

	/* Request and release builds a connection. */
	obj = ast_odbc_request_obj(class->name, 0);
	if (obj) {
		ast_odbc_release_obj(obj);
	}
}

static int odbc_class_is_new(void *obj, void *arg, int flags)
{
	struct odbc_class *class = obj;
	struct ao2_container *new_classes = arg;
	RAII_VAR(struct odbc_class *, found, ao2_find(new_classes, class, OBJ_POINTER), ao2_cleanup);

	return found ? CMP_MATCH : 0;
}

static int odbc_class_mark_old(void *obj, void *arg, int flags)
{
	struct odbc_class *class = obj;
	struct ao2_container *new_classes = arg;

	if (!odbc_class_is_new(class, new_classes, 0)) {
		ast_mutex_lock(&class->lock);
		class->delme = 1;
		ast_mutex_unlock(&class->lock);
	}

	return 0;
}

static int odbc_class_unlink_delme(void *obj, void *arg, int flags)
{
	struct odbc_class *class = obj;

	return class->delme ? CMP_MATCH : 0;
}

/*! \internal \brief Build runtime classes from the parsed cfg snapshot.
 *
 * The aco snapshot in odbc_global_cfg holds the parsed config;
 * class_container holds the runtime state (env handles, connection
 * pools, counters).
 */
static int odbc_reconcile_config(void)
{
	RAII_VAR(struct odbc_config *, cfg, ao2_global_obj_ref(odbc_global_cfg), ao2_cleanup);
	RAII_VAR(struct ao2_container *, new_classes, NULL, ao2_cleanup);
	struct ao2_iterator iter;
	struct odbc_class_cfg *class_cfg;
	struct odbc_class *class;
	struct odbc_cache_tables *table;
	int res = 0;

	if (!cfg || !cfg->classes) {
		return 0;
	}

	new_classes = odbc_class_container_alloc();
	if (!new_classes) {
		return -1;
	}

	iter = ao2_iterator_init(cfg->classes, 0);
	while ((class_cfg = ao2_iterator_next(&iter))) {
		if (!class_cfg->enabled || ast_strlen_zero(class_cfg->dsn)) {
			ao2_ref(class_cfg, -1);
			continue;
		}

		class = odbc_class_build(class_cfg);
		if (!class) {
			res = -1;
			ao2_ref(class_cfg, -1);
			break;
		}

		if (!ao2_link(new_classes, class)) {
			res = -1;
			ao2_ref(class, -1);
			ao2_ref(class_cfg, -1);
			break;
		}
		ao2_ref(class, -1);
		ao2_ref(class_cfg, -1);
	}
	ao2_iterator_destroy(&iter);

	if (res) {
		return res;
	}

	iter = ao2_iterator_init(new_classes, 0);
	while ((class = ao2_iterator_next(&iter))) {
		if (!ao2_link(class_container, class)) {
			res = -1;
			ao2_ref(class, -1);
			break;
		}
		ast_log(LOG_NOTICE, "Registered ODBC class '%s' dsn->[%s]\n",
			class->name, class->dsn);
		ao2_ref(class, -1);
	}
	ao2_iterator_destroy(&iter);

	if (res) {
		iter = ao2_iterator_init(new_classes, 0);
		while ((class = ao2_iterator_next(&iter))) {
			ao2_unlink(class_container, class);
			ao2_ref(class, -1);
		}
		ao2_iterator_destroy(&iter);
		return res;
	}

	ao2_callback(class_container, OBJ_NODATA | OBJ_MULTIPLE, odbc_class_mark_old, new_classes);

	iter = ao2_iterator_init(new_classes, 0);
	while ((class = ao2_iterator_next(&iter))) {
		odbc_preconnect_class(class);
		ao2_ref(class, -1);
	}
	ao2_iterator_destroy(&iter);

	/* Reap classes whose section was removed, disabled, or replaced. */
	ao2_callback(class_container, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE,
		odbc_class_unlink_delme, NULL);

	/* Empty the table cache; rebuilt lazily on next access. */
	AST_RWLIST_WRLOCK(&odbc_tables);
	while ((table = AST_RWLIST_REMOVE_HEAD(&odbc_tables, list))) {
		destroy_table_cache(table);
	}
	AST_RWLIST_UNLOCK(&odbc_tables);

	return 0;
}

static void odbc_apply_config(void)
{
	if (odbc_reconcile_config()) {
		ast_log(LOG_WARNING, "Errors applying res_odbc.conf; runtime classes were left unchanged\n");
	}
}

/*! \internal \brief Register every res_odbc.conf option with aco. */
static void odbc_register_options(void)
{
	struct aco_type **types = odbc_class_aco_types;
	struct aco_info *info = &odbc_cfg_info;

	aco_option_register(info, "enabled", ACO_EXACT, types,
		"yes", OPT_BOOL_T, 1, FLDSET(struct odbc_class_cfg, enabled));
	aco_option_register(info, "pre-connect", ACO_EXACT, types,
		"no", OPT_BOOL_T, 1, FLDSET(struct odbc_class_cfg, preconnect));
	aco_option_register(info, "dsn", ACO_EXACT, types,
		"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct odbc_class_cfg, dsn));
	aco_option_register(info, "username", ACO_EXACT, types,
		"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct odbc_class_cfg, username));
	aco_option_register(info, "password", ACO_EXACT, types,
		"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct odbc_class_cfg, password));
	aco_option_register(info, "sanitysql", ACO_EXACT, types,
		"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct odbc_class_cfg, sanitysql));
	aco_option_register(info, "backslash_is_escape", ACO_EXACT, types,
		"yes", OPT_BOOL_T, 1, FLDSET(struct odbc_class_cfg, backslash_is_escape));
	aco_option_register(info, "forcecommit", ACO_EXACT, types,
		"no", OPT_BOOL_T, 1, FLDSET(struct odbc_class_cfg, forcecommit));
	aco_option_register(info, "logging", ACO_EXACT, types,
		"no", OPT_BOOL_T, 1, FLDSET(struct odbc_class_cfg, logging));
	aco_option_register(info, "connect_timeout", ACO_EXACT, types,
		"10", OPT_UINT_T, PARSE_DEFAULT | PARSE_IN_RANGE,
		FLDSET(struct odbc_class_cfg, conntimeout), 10, 1, INT_MAX);
	aco_option_register(info, "max_connections", ACO_EXACT, types,
		"1", OPT_UINT_T, PARSE_DEFAULT | PARSE_IN_RANGE,
		FLDSET(struct odbc_class_cfg, maxconnections), 1, 1, INT_MAX);
	aco_option_register(info, "slow_query_limit", ACO_EXACT, types,
		"5000", OPT_UINT_T, PARSE_DEFAULT,
		FLDSET(struct odbc_class_cfg, slowquerylimit), 5000);
	aco_option_register_custom(info, "isolation", ACO_EXACT, types,
		"read_committed", isolation_handler, 0);
	aco_option_register_custom(info, "cache_type", ACO_EXACT, types,
		"stack", cache_type_handler, 0);
	aco_option_register_custom(info, "cache_size", ACO_EXACT, types,
		"-1", cache_size_handler, 0);
	aco_option_register_custom(info, "negative_connection_cache", ACO_EXACT, types,
		"0", negative_connection_cache_handler, 0);
	/* Empty-prefix catch-all so anything aco's exact-match lookup did
	 * not resolve still parses (the four obsolete pool-related names
	 * get a specific warning; everything else gets a generic
	 * unknown-option warning) instead of taking down the whole class
	 * registration on a typo. */
	aco_option_register_custom_nodoc(info, "", ACO_PREFIX, types,
		"", unknown_option_handler, 0);
}

/*! \internal \brief Apply [ENV] section vars to the process environment.
 *
 * The config_options framework has no native concept of "any variable
 * in this section, just push it to setenv()", so this is handled
 * manually before aco runs. The [ENV] category is excluded from aco's
 * type matching via odbc_class_aco_type.category_match.
 */
static void odbc_apply_env_section(struct ast_config *config)
{
	char *cat;
	struct ast_variable *v;

	for (cat = ast_category_browse(config, NULL); cat; cat = ast_category_browse(config, cat)) {
		if (strcasecmp(cat, "ENV")) {
			continue;
		}

		for (v = ast_variable_browse(config, cat); v; v = v->next) {
			setenv(v->name, v->value, 1);
			ast_log(LOG_NOTICE, "Adding ENV var: %s=%s\n", v->name, v->value);
		}
	}
}

static int load_odbc_config(void)
{
	struct ast_flags config_flags = { 0 };
	struct ast_config *config;
	enum aco_process_status status;

	config = ast_config_load("res_odbc.conf", config_flags);
	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load config file res_odbc.conf\n");
		return -1;
	}
	odbc_apply_env_section(config);

	status = aco_process_ast_config(&odbc_cfg_info, &res_odbc_conf_file, config);
	ast_config_destroy(config);

	if (status == ACO_PROCESS_ERROR) {
		ast_log(LOG_WARNING, "Errors processing res_odbc.conf; some classes may not be available\n");
		return -1;
	}

	/* aco_process_ast_config does not invoke post_apply_config (only
	 * aco_process_config does). Drive reconciliation manually so the
	 * runtime container reflects the freshly-installed snapshot. */
	return odbc_reconcile_config();
}

#ifdef TEST_FRAMEWORK
static void odbc_test_copy_class_config(struct ast_odbc_test_class_config *out,
	const struct odbc_class_cfg *cfg)
{
	ast_copy_string(out->name, cfg->name, sizeof(out->name));
	ast_copy_string(out->dsn, cfg->dsn, sizeof(out->dsn));
	ast_copy_string(out->username, cfg->username, sizeof(out->username));
	ast_copy_string(out->password, cfg->password, sizeof(out->password));
	ast_copy_string(out->sanitysql, cfg->sanitysql, sizeof(out->sanitysql));
	out->enabled = cfg->enabled;
	out->preconnect = cfg->preconnect;
	out->backslash_is_escape = cfg->backslash_is_escape;
	out->forcecommit = cfg->forcecommit;
	out->cache_is_queue = cfg->cache_is_queue;
	out->logging = cfg->logging;
	out->isolation = cfg->isolation;
	out->conntimeout = cfg->conntimeout;
	out->maxconnections = cfg->maxconnections;
	out->slowquerylimit = cfg->slowquerylimit;
	out->max_cache_size = cfg->max_cache_size;
	out->negative_connection_cache_sec = cfg->negative_connection_cache.tv_sec;
	out->negative_connection_cache_usec = cfg->negative_connection_cache.tv_usec;
}

int ast_odbc_test_parse_ast_config(struct ast_config *config, const char *class_name,
	struct ast_odbc_test_class_config *out)
{
	RAII_VAR(struct odbc_config *, old_cfg, ao2_global_obj_ref(odbc_global_cfg), ao2_cleanup);
	RAII_VAR(struct odbc_config *, parsed_cfg, NULL, ao2_cleanup);
	RAII_VAR(struct odbc_class_cfg *, class_cfg, NULL, ao2_cleanup);
	enum aco_process_status status;
	int res = -1;

	if (!config || ast_strlen_zero(class_name) || !out) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	odbc_apply_env_section(config);

	status = aco_process_ast_config(&odbc_cfg_info, &res_odbc_conf_file, config);
	if (status == ACO_PROCESS_ERROR) {
		goto done;
	}

	parsed_cfg = ao2_global_obj_ref(odbc_global_cfg);
	if (!parsed_cfg || !parsed_cfg->classes) {
		goto done;
	}

	class_cfg = ao2_find(parsed_cfg->classes, class_name, OBJ_SEARCH_KEY);
	if (!class_cfg) {
		res = 1;
		goto done;
	}

	odbc_test_copy_class_config(out, class_cfg);
	res = 0;

done:
	ao2_global_obj_replace_unref(odbc_global_cfg, old_cfg);
	return res;
}
#endif


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
			ast_cli(a->fd, "    Cache Type: %s\n", class->cache_is_queue ? "round-robin queue" : "stack (last release, first re-use)");
			ast_cli(a->fd, "    Cache Usage: %u cached out of %u\n", class->cur_cache,
					class->max_cache_size < class->maxconnections ? class->max_cache_size : class->maxconnections);
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
	if (class->cache_is_queue) {
		AST_LIST_INSERT_TAIL(&class->connections, obj, list);
	} else {
		AST_LIST_INSERT_HEAD(&class->connections, obj, list);
	}

	if (class->cur_cache >= class->max_cache_size) {
		/* cache is full */
		if (class->cache_is_queue) {
			/* HEAD will be oldest */
			obj = AST_LIST_REMOVE_HEAD(&class->connections, list);
		} else {
			/* TAIL will be oldest */
			obj = AST_LIST_LAST(&class->connections);
			AST_LIST_REMOVE(&class->connections, obj, list);
		}
		--class->connection_cnt;
		ast_mutex_unlock(&class->lock);

		ast_debug(2, "ODBC Pool '%s' exceeded cache size, dropping '%p', connection count is %zd (%u cached)\n",
			class->name, obj, class->connection_cnt, class->cur_cache);

		ao2_ref(obj, -1);

		ast_mutex_lock(&class->lock);
	} else {
		++class->cur_cache;
	}
	ast_cond_signal(&class->cond);
	ast_mutex_unlock(&class->lock);

	ao2_ref(class, -1);
}

int ast_odbc_backslash_is_escape(struct odbc_obj *obj)
{
	return obj->parent->backslash_is_escape;
}

/*!
 * \internal
 * \brief ao2 matcher used by request paths to look up a class by name.
 *
 * Filters out classes that have been marked for removal so the lookup
 * only returns live, in-service classes. odbc_reconcile_config() links
 * replacements before marking old classes delme=1, so a lookup can
 * continue past an old marked class and find its replacement.
 */
static int aoro2_class_cb(void *obj, void *arg, int flags)
{
	struct odbc_class *class = obj;
	char *name = arg;
	if (!strcmp(class->name, name) && !class->delme) {
		return CMP_MATCH | CMP_STOP;
	}
	return 0;
}

/*!
 * \internal
 * \brief Find an active odbc_class by name.
 *
 * Returns the matched class with its ao2 reference incremented;
 * caller must ao2_ref(class, -1) when done.
 *
 * \param name Class name to look up.
 * \retval NULL no live class with that name in the container.
 * \retval non-NULL the matching class (refcount bumped).
 */
static struct odbc_class *odbc_class_find(const char *name)
{
	return ao2_callback(class_container, 0, aoro2_class_cb, (char *) name);
}

unsigned int ast_odbc_get_max_connections(const char *name)
{
	struct odbc_class *class;
	unsigned int max_connections;

	class = odbc_class_find(name);
	if (!class) {
		return 0;
	}

	max_connections = class->maxconnections;
	ao2_ref(class, -1);

	return max_connections;
}

/*!
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

	if (!(class = odbc_class_find(name))) {
		ast_debug(1, "Class '%s' not found!\n", name);
		return NULL;
	}

	while (!obj) {
		ast_mutex_lock(&class->lock);

		obj = AST_LIST_REMOVE_HEAD(&class->connections, list);
		if (obj) {
			--class->cur_cache;
		}

		ast_mutex_unlock(&class->lock);

		if (!obj) {
			ast_mutex_lock(&class->lock);

			if (class->connection_cnt < class->maxconnections) {
				/* If no connection is immediately available establish a new
				 * one if allowed. If we try and fail we give up completely as
				 * we could go into an infinite loop otherwise.
				 */
				obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor);
				if (!obj) {
					ast_mutex_unlock(&class->lock);
					break;
				}

				obj->parent = ao2_bump(class);

				class->connection_cnt++;

				ast_mutex_unlock(&class->lock);

				if (odbc_obj_connect(obj) == ODBC_FAIL) {
					ast_mutex_lock(&class->lock);
					class->connection_cnt--;
					ast_cond_signal(&class->cond);
					ast_mutex_unlock(&class->lock);
					ao2_ref(obj->parent, -1);
					ao2_ref(obj, -1);
					obj = NULL;
					break;
				}

				ast_mutex_lock(&class->lock);

				ast_debug(2, "Created ODBC handle %p on class '%s', new count is %zd\n", obj,
					name, class->connection_cnt);

			} else {
				/* Otherwise if we're not allowed to create a new one we
				 * wait for another thread to give up the connection they
				 * own.
				 */
				ast_cond_wait(&class->cond, &class->lock);
			}

			ast_mutex_unlock(&class->lock);

		} else if (connection_dead(obj, class)) {
			/* If the connection is dead try to grab another functional one from the
			 * pool instead of trying to resurrect this one.
			 */
			ast_mutex_lock(&class->lock);

			class->connection_cnt--;
			/* this thread will re-acquire, and if that fails will signal,
			 * thus no need to signal class->cond here */
			ast_debug(2, "ODBC handle %p dead - removing from class '%s', new count is %zd\n",
				obj, name, class->connection_cnt);

			ast_mutex_unlock(&class->lock);

			ao2_ref(obj, -1);
			obj = NULL;
		} else {
			/* We successfully grabbed a connection from the pool and all is well!
			 */
			obj->parent = ao2_bump(class);
			ast_debug(2, "Reusing ODBC handle %p from class '%s'\n", obj, name);
		}
	}

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
		char secs[AST_TIME_T_LEN];
		ast_time_t_to_string(negative_cache_expiration - time(NULL), secs, sizeof(secs));
		ast_log(LOG_WARNING, "Not connecting to %s. Negative connection cache for %s seconds\n", obj->parent->name, secs);
		return ODBC_FAIL;
	}

	res = SQLAllocHandle(SQL_HANDLE_DBC, obj->parent->env, &con);

	if (!SQL_SUCCEEDED(res)) {
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

	if (!SQL_SUCCEEDED(res)) {
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
	return load_odbc_config();
}

static int unload_module(void)
{
	aco_info_destroy(&odbc_cfg_info);
	ao2_global_obj_release(odbc_global_cfg);
	ao2_cleanup(class_container);
	ast_cli_unregister_multiple(cli_odbc, ARRAY_LEN(cli_odbc));

	return 0;
}

static int load_module(void)
{
	class_container = odbc_class_container_alloc();
	if (!class_container) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (aco_info_init(&odbc_cfg_info)) {
		ao2_cleanup(class_container);
		class_container = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	odbc_register_options();

	if (load_odbc_config() == -1) {
		aco_info_destroy(&odbc_cfg_info);
		ao2_global_obj_release(odbc_global_cfg);
		ao2_cleanup(class_container);
		class_container = NULL;
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
	.requires = "res_odbc_transaction",
);
