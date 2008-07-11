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

typedef enum { ODBC_SUCCESS=0, ODBC_FAIL=-1} odbc_status;

/*! \brief ODBC container */
struct odbc_obj {
	ast_mutex_t lock;
	SQLHDBC  con;                   /* ODBC Connection Handle */
	struct odbc_class *parent;      /* Information about the connection is protected */
	struct timeval last_used;
#ifdef DEBUG_THREADS
	char file[80];
	char function[80];
	int lineno;
#endif
	unsigned int used:1;
	unsigned int up:1;
	AST_LIST_ENTRY(odbc_obj) list;
};

/*!\brief These aren't used in any API calls, but they are kept in a common
 * location, simply for convenience and to avoid duplication.
 */
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
int ast_odbc_smart_execute(struct odbc_obj *obj, SQLHSTMT stmt) __attribute__ ((deprecated));

/*! 
 * \brief Retrieves a connected ODBC object
 * \param name The name of the ODBC class for which a connection is needed.
 * \param check Whether to ensure that a connection is valid before returning the handle.  Usually unnecessary.
 * \retval ODBC object 
 * \retval  NULL if there is no connection available with the requested name.
 *
 * Connection classes may, in fact, contain multiple connection handles.  If
 * the connection is pooled, then each connection will be dedicated to the
 * thread which requests it.  Note that all connections should be released
 * when the thread is done by calling odbc_release_obj(), below.
 */
#ifdef DEBUG_THREADS
struct odbc_obj *_ast_odbc_request_obj(const char *name, int check, const char *file, const char *function, int lineno);
#define ast_odbc_request_obj(a, b)	_ast_odbc_request_obj(a, b, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#else
struct odbc_obj *ast_odbc_request_obj(const char *name, int check);
#endif

/*! 
 * \brief Releases an ODBC object previously allocated by odbc_request_obj()
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
 * \return Returns 1 if backslash is a native escape character, 0 if an ESCAPE clause is needed to support '\'
 */
int ast_odbc_backslash_is_escape(struct odbc_obj *obj);

/*! \brief Executes an non prepared statement and returns the resulting
 * statement handle.
 * \param obj The ODBC object
 * \param exec_cb A function callback, which, when called, should return a statement handle with result columns bound.
 * \param data A parameter to be passed to the exec_cb parameter function, indicating which statement handle is to be prepared.
 * \retval a statement handle
 * \retval NULL on error
 */
SQLHSTMT ast_odbc_direct_execute(struct odbc_obj *obj, SQLHSTMT (*exec_cb)(struct odbc_obj *obj, void *data), void *data);

/*! 
 * \brief Prepares, executes, and returns the resulting statement handle.
 * \param obj The ODBC object
 * \param prepare_cb A function callback, which, when called, should return a statement handle prepared, with any necessary parameters or result columns bound.
 * \param data A parameter to be passed to the prepare_cb parameter function, indicating which statement handle is to be prepared.
 * \retval a statement handle 
 * \retval NULL on error
 */
SQLHSTMT ast_odbc_prepare_and_execute(struct odbc_obj *obj, SQLHSTMT (*prepare_cb)(struct odbc_obj *obj, void *data), void *data);

/*!
 * \brief Find or create an entry describing the table specified.
 * \param database Name of an ODBC class on which to query the table
 * \param table Tablename to describe
 * \retval A structure describing the table layout, or NULL, if the table is not found or another error occurs.
 * When a structure is returned, the contained columns list will be
 * rdlock'ed, to ensure that it will be retained in memory.
 */
struct odbc_cache_tables *ast_odbc_find_table(const char *database, const char *tablename);

/*!
 * \brief Find a column entry within a cached table structure
 * \param table Cached table structure, as returned from ast_odbc_find_table()
 * \param colname The column name requested
 * \retval A structure describing the column type, or NULL, if the column is not found.
 */
struct odbc_cache_columns *ast_odbc_find_column(struct odbc_cache_tables *table, const char *colname);

/*!
 * \brief Remove a cache entry from memory
 * \param database Name of an ODBC class (used to ensure like-named tables in different databases are not confused)
 * \param table Tablename for which a cached record should be removed
 * \retval 0 if the cache entry was removed, or -1 if no matching entry was found.
 */
int ast_odbc_clear_cache(const char *database, const char *tablename);

/*!
 * \brief Release a table returned from ast_odbc_find_table
 */
#define ast_odbc_release_table(ptr) if (ptr) { AST_RWLIST_UNLOCK(&(ptr)->columns); }

#endif /* _ASTERISK_RES_ODBC_H */
