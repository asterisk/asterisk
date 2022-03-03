/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2004 - 2005, Anthony Minessale II
 * Copyright (C) 2006, Tilghman Lesher
 *
 * Mark Spencer <markster@digium.com>
 * Anthony Minessale <anthmct@yahoo.com>
 * Tilghman Lesher <res_odbc_200603@the-tilghman.com>
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
 * \brief ODBC resource manager
 */

#ifndef _ASTERISK_RES_ODBC_H
#define _ASTERISK_RES_ODBC_H

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include "asterisk/linkedlists.h"
#include "asterisk/strings.h"

typedef enum { ODBC_SUCCESS=0, ODBC_FAIL=-1} odbc_status;

/*! \brief Flags for use with \see ast_odbc_request_obj2 */
enum {
	RES_ODBC_SANITY_CHECK = (1 << 0),
	RES_ODBC_INDEPENDENT_CONNECTION = (1 << 1),
	RES_ODBC_CONNECTED = (1 << 2),
};

/*! \brief ODBC container */
struct odbc_obj {
	SQLHDBC  con;                   /*!< ODBC Connection Handle */
	struct odbc_class *parent;      /*!< Information about the connection is protected */
#ifdef DEBUG_THREADS
	char file[80];
	char function[80];
	int lineno;
#endif
	char *sql_text;					/*!< The SQL text currently executing */
	AST_LIST_ENTRY(odbc_obj) list;
};

/*!\brief These structures are used for adaptive capabilities */
struct odbc_cache_columns {
	char *name;
	SQLSMALLINT type;
	SQLINTEGER size;
	SQLSMALLINT decimals;
	SQLSMALLINT radix;
	SQLSMALLINT nullable;
	SQLINTEGER octetlen;
	AST_RWLIST_ENTRY(odbc_cache_columns) list;
};

struct odbc_cache_tables {
	char *connection;
	char *table;
	AST_RWLIST_HEAD(_columns, odbc_cache_columns) columns;
	AST_RWLIST_ENTRY(odbc_cache_tables) list;
};

/* functions */

/*!
 * \brief Executes a prepared statement handle
 * \param obj The non-NULL result of odbc_request_obj()
 * \param stmt The prepared statement handle
 * \retval 0 on success
 * \retval -1 on failure
 *
 * This function was originally designed simply to execute a prepared
 * statement handle and to retry if the initial execution failed.
 * Unfortunately, it did this by disconnecting and reconnecting the database
 * handle which on most databases causes the statement handle to become
 * invalid.  Therefore, this method has been deprecated in favor of
 * odbc_prepare_and_execute() which allows the statement to be prepared
 * multiple times, if necessary, in case of a loss of connection.
 *
 * This function really only ever worked with MySQL, where the statement handle is
 * not prepared on the server.  If you are not using MySQL, you should avoid it.
 */
int ast_odbc_smart_execute(struct odbc_obj *obj, SQLHSTMT stmt) __attribute__((deprecated));

/*!
 * \brief Retrieves a connected ODBC object
 *
 * \deprecated
 *
 * This is only around for backwards-compatibility with older versions of Asterisk.
 */
#define ast_odbc_request_obj2(name, check) _ast_odbc_request_obj2(name, check, __FILE__, __PRETTY_FUNCTION__, __LINE__)
struct odbc_obj *_ast_odbc_request_obj2(const char *name, struct ast_flags flags, const char *file, const char *function, int lineno);

/*!
 * \brief Get a ODBC connection object
 *
 * The "check" parameter is leftover from an earlier implementation where database connections
 * were cached by res_odbc. Since connections are managed by unixODBC now, this parameter is
 * only kept around for API compatibility.
 *
 * \param name The name of the res_odbc.conf section describing the database to connect to
 * \param check unused
 * \return A connection to the database. Call ast_odbc_release_obj() when finished.
 */
#define ast_odbc_request_obj(name, check) _ast_odbc_request_obj(name, check, __FILE__, __PRETTY_FUNCTION__, __LINE__)
struct odbc_obj *_ast_odbc_request_obj(const char *name, int check, const char *file, const char *function, int lineno);

/*!
 * \brief Releases an ODBC object previously allocated by ast_odbc_request_obj()
 * \param obj The ODBC object
 */
void ast_odbc_release_obj(struct odbc_obj *obj);

/*!
 * \brief Checks an ODBC object to ensure it is still connected
 * \param obj The ODBC object
 * \retval 0 if connected
 * \retval -1 otherwise.
 */
int ast_odbc_sanity_check(struct odbc_obj *obj);

/*! \brief Checks if the database natively supports backslash as an escape character.
 * \param obj The ODBC object
 * \retval 1 if backslash is a native escape character
 * \retval 0 if an ESCAPE clause is needed to support '\'
 */
int ast_odbc_backslash_is_escape(struct odbc_obj *obj);

/*! \brief Executes an non prepared statement and returns the resulting
 * statement handle.
 * \param obj The ODBC object
 * \param exec_cb A function callback, which, when called, should return a statement handle with result columns bound.
 * \param data A parameter to be passed to the exec_cb parameter function, indicating which statement handle is to be prepared.
 * \return a statement handle
 * \retval NULL on error
 */
SQLHSTMT ast_odbc_direct_execute(struct odbc_obj *obj, SQLHSTMT (*exec_cb)(struct odbc_obj *obj, void *data), void *data);

/*!
 * \brief Prepares, executes, and returns the resulting statement handle.
 * \param obj The ODBC object
 * \param prepare_cb A function callback, which, when called, should return a statement handle prepared, with any necessary parameters or result columns bound.
 * \param data A parameter to be passed to the prepare_cb parameter function, indicating which statement handle is to be prepared.
 * \return a statement handle
 * \retval NULL on error
 */
SQLHSTMT ast_odbc_prepare_and_execute(struct odbc_obj *obj, SQLHSTMT (*prepare_cb)(struct odbc_obj *obj, void *data), void *data);

/*!
 * \brief Prepares a SQL query on a statement.
 * \param obj The ODBC object
 * \param stmt The statement
 * \param sql The SQL query
 * \note This should be used in place of SQLPrepare
 */
int ast_odbc_prepare(struct odbc_obj *obj, SQLHSTMT *stmt, const char *sql);

/*! \brief Execute a unprepared SQL query.
 * \param obj The ODBC object
 * \param stmt The statement
 * \param sql The SQL query
 * \note This should be used in place of SQLExecDirect
 */
SQLRETURN ast_odbc_execute_sql(struct odbc_obj *obj, SQLHSTMT *stmt, const char *sql);

/*!
 * \brief Find or create an entry describing the table specified.
 * \param database Name of an ODBC class on which to query the table
 * \param tablename Tablename to describe
 * \return A structure describing the table layout.
 * \retval NULL if the table is not found or another error occurs.
 * When a structure is returned, the contained columns list will be
 * rdlock'ed, to ensure that it will be retained in memory.  The information
 * will be cached until a reload event or when ast_odbc_clear_cache() is called
 * with the relevant parameters.
 * \since 1.6.1
 */
struct odbc_cache_tables *ast_odbc_find_table(const char *database, const char *tablename);

/*!
 * \brief Find a column entry within a cached table structure
 * \param table Cached table structure, as returned from ast_odbc_find_table()
 * \param colname The column name requested
 * \return A structure describing the column type, or NULL, if the column is not found.
 * \since 1.6.1
 */
struct odbc_cache_columns *ast_odbc_find_column(struct odbc_cache_tables *table, const char *colname);

/*!
 * \brief Remove a cache entry from memory
 * This function may be called to clear entries created and cached by the
 * ast_odbc_find_table() API call.
 * \param database Name of an ODBC class (used to ensure like-named tables in different databases are not confused)
 * \param tablename Tablename for which a cached record should be removed
 * \retval 0 if the cache entry was removed.
 * \retval -1 if no matching entry was found.
 * \since 1.6.1
 */
int ast_odbc_clear_cache(const char *database, const char *tablename);

/*!
 * \brief Release a table returned from ast_odbc_find_table
 */
#define ast_odbc_release_table(ptr) if (ptr) { AST_RWLIST_UNLOCK(&(ptr)->columns); }

/*!\brief Wrapper for SQLGetData to use with dynamic strings
 * \param buf Address of the pointer to the ast_str structure.
 * \param pmaxlen The maximum size of the resulting string, or 0 for no limit.
 * \param StatementHandle The statement handle from which to retrieve data.
 * \param ColumnNumber Column number (1-based offset) for which to retrieve data.
 * \param TargetType The SQL constant indicating what kind of data is to be retrieved (usually SQL_CHAR)
 * \param StrLen_or_Ind A pointer to a length indicator, specifying the total length of data.
 */
SQLRETURN ast_odbc_ast_str_SQLGetData(struct ast_str **buf, int pmaxlen, SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber, SQLSMALLINT TargetType, SQLLEN *StrLen_or_Ind);

/*!
 * \brief Shortcut for printing errors to logs after a failed SQL operation.
 *
 * \param handle_type The type of SQL handle on which to gather diagnostics
 * \param handle The SQL handle to gather diagnostics from
 * \param operation The name of the failed operation.
 * \return The error string that was printed to the logs
 */
struct ast_str *ast_odbc_print_errors(SQLSMALLINT handle_type, SQLHANDLE handle, const char *operation);

/*!
 * \brief Get the transaction isolation setting for an ODBC class
 */
unsigned int ast_odbc_class_get_isolation(struct odbc_class *class);

/*!
 * \brief Get the transaction forcecommit setting for an ODBC class
 */
unsigned int ast_odbc_class_get_forcecommit(struct odbc_class *class);

/*!
 * \brief Get the name of an ODBC class.
 */
const char *ast_odbc_class_get_name(struct odbc_class *class);

/*!
 * \brief Convert from textual transaction isolation values to their numeric constants
 */
int ast_odbc_text2isolation(const char *txt);

/*!
 * \brief Convert from numeric transaction isolation values to their textual counterparts
 */
const char *ast_odbc_isolation2text(int iso);

/*!
 * \brief Return the current configured maximum number of connections for a class
 */
unsigned int ast_odbc_get_max_connections(const char *name);

#endif /* _ASTERISK_RES_ODBC_H */
