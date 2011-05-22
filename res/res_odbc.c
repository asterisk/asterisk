/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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

/*** MODULEINFO
	<depend>generic_odbc</depend>
	<depend>ltdl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include "asterisk/data.h"

/*** DOCUMENTATION
	<function name="ODBC" language="en_US">
		<synopsis>
			Controls ODBC transaction properties.
		</synopsis>
		<syntax>
			<parameter name="property" required="true">
				<enumlist>
					<enum name="transaction">
						<para>Gets or sets the active transaction ID.  If set, and the transaction ID does not
						exist and a <replaceable>database name</replaceable> is specified as an argument, it will be created.</para>
					</enum>
					<enum name="forcecommit">
						<para>Controls whether a transaction will be automatically committed when the channel
						hangs up.  Defaults to false.  If a <replaceable>transaction ID</replaceable> is specified in the optional argument,
						the property will be applied to that ID, otherwise to the current active ID.</para>
					</enum>
					<enum name="isolation">
						<para>Controls the data isolation on uncommitted transactions.  May be one of the
						following: <literal>read_committed</literal>, <literal>read_uncommitted</literal>,
						<literal>repeatable_read</literal>, or <literal>serializable</literal>.  Defaults to the
						database setting in <filename>res_odbc.conf</filename> or <literal>read_committed</literal>
						if not specified.  If a <replaceable>transaction ID</replaceable> is specified as an optional argument, it will be
						applied to that ID, otherwise the current active ID.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="argument" required="false" />
		</syntax>
		<description>
			<para>The ODBC() function allows setting several properties to influence how a connected
			database processes transactions.</para>
		</description>
	</function>
	<application name="ODBC_Commit" language="en_US">
		<synopsis>
			Commits a currently open database transaction.
		</synopsis>
		<syntax>
			<parameter name="transaction ID" required="no" />
		</syntax>
		<description>
			<para>Commits the database transaction specified by <replaceable>transaction ID</replaceable>
			or the current active transaction, if not specified.</para>
		</description>
	</application>
	<application name="ODBC_Rollback" language="en_US">
		<synopsis>
			Rollback a currently open database transaction.
		</synopsis>
		<syntax>
			<parameter name="transaction ID" required="no" />
		</syntax>
		<description>
			<para>Rolls back the database transaction specified by <replaceable>transaction ID</replaceable>
			or the current active transaction, if not specified.</para>
		</description>
	</application>
 ***/

struct odbc_class
{
	AST_LIST_ENTRY(odbc_class) list;
	char name[80];
	char dsn[80];
	char *username;
	char *password;
	char *sanitysql;
	SQLHENV env;
	unsigned int haspool:1;              /*!< Boolean - TDS databases need this */
	unsigned int delme:1;                /*!< Purge the class */
	unsigned int backslash_is_escape:1;  /*!< On this database, the backslash is a native escape sequence */
	unsigned int forcecommit:1;          /*!< Should uncommitted transactions be auto-committed on handle release? */
	unsigned int isolation;              /*!< Flags for how the DB should deal with data in other, uncommitted transactions */
	unsigned int limit;                  /*!< Maximum number of database handles we will allow */
	int count;                           /*!< Running count of pooled connections */
	unsigned int idlecheck;              /*!< Recheck the connection if it is idle for this long (in seconds) */
	unsigned int conntimeout;            /*!< Maximum time the connection process should take */
	/*! When a connection fails, cache that failure for how long? */
	struct timeval negative_connection_cache;
	/*! When a connection fails, when did that last occur? */
	struct timeval last_negative_connect;
	/*! List of handles associated with this class */
	struct ao2_container *obj_container;
};

static struct ao2_container *class_container;

static AST_RWLIST_HEAD_STATIC(odbc_tables, odbc_cache_tables);

static odbc_status odbc_obj_connect(struct odbc_obj *obj);
static odbc_status odbc_obj_disconnect(struct odbc_obj *obj);
static int odbc_register_class(struct odbc_class *class, int connect);
static void odbc_txn_free(void *data);
static void odbc_release_obj2(struct odbc_obj *obj, struct odbc_txn_frame *tx);

AST_THREADSTORAGE(errors_buf);

static struct ast_datastore_info txn_info = {
	.type = "ODBC_Transaction",
	.destroy = odbc_txn_free,
};

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

#define DATA_EXPORT_ODBC_CLASS(MEMBER)				\
	MEMBER(odbc_class, name, AST_DATA_STRING)		\
	MEMBER(odbc_class, dsn, AST_DATA_STRING)		\
	MEMBER(odbc_class, username, AST_DATA_STRING)		\
	MEMBER(odbc_class, password, AST_DATA_PASSWORD)		\
	MEMBER(odbc_class, limit, AST_DATA_INTEGER)		\
	MEMBER(odbc_class, count, AST_DATA_INTEGER)		\
	MEMBER(odbc_class, forcecommit, AST_DATA_BOOLEAN)

AST_DATA_STRUCTURE(odbc_class, DATA_EXPORT_ODBC_CLASS);

static const char *isolation2text(int iso)
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

static int text2isolation(const char *txt)
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

static struct odbc_txn_frame *find_transaction(struct ast_channel *chan, struct odbc_obj *obj, const char *name, int active)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *txn = NULL;

	if (!chan && obj && obj->txf && obj->txf->owner) {
		chan = obj->txf->owner;
	} else if (!chan) {
		/* No channel == no transaction */
		return NULL;
	}

	ast_channel_lock(chan);
	if ((txn_store = ast_channel_datastore_find(chan, &txn_info, NULL))) {
		oldlist = txn_store->data;
	} else {
		/* Need to create a new datastore */
		if (!(txn_store = ast_datastore_alloc(&txn_info, NULL))) {
			ast_log(LOG_ERROR, "Unable to allocate a new datastore.  Cannot create a new transaction.\n");
			ast_channel_unlock(chan);
			return NULL;
		}

		if (!(oldlist = ast_calloc(1, sizeof(*oldlist)))) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  Cannot create a new transaction.\n");
			ast_datastore_free(txn_store);
			ast_channel_unlock(chan);
			return NULL;
		}

		txn_store->data = oldlist;
		AST_LIST_HEAD_INIT(oldlist);
		ast_channel_datastore_add(chan, txn_store);
	}

	AST_LIST_LOCK(oldlist);
	ast_channel_unlock(chan);

	/* Scanning for an object is *fast*.  Scanning for a name is much slower. */
	if (obj != NULL || active == 1) {
		AST_LIST_TRAVERSE(oldlist, txn, list) {
			if (txn->obj == obj || txn->active) {
				AST_LIST_UNLOCK(oldlist);
				return txn;
			}
		}
	}

	if (name != NULL) {
		AST_LIST_TRAVERSE(oldlist, txn, list) {
			if (!strcasecmp(txn->name, name)) {
				AST_LIST_UNLOCK(oldlist);
				return txn;
			}
		}
	}

	/* Nothing found, create one */
	if (name && obj && (txn = ast_calloc(1, sizeof(*txn) + strlen(name) + 1))) {
		struct odbc_txn_frame *otxn;

		strcpy(txn->name, name); /* SAFE */
		txn->obj = obj;
		txn->isolation = obj->parent->isolation;
		txn->forcecommit = obj->parent->forcecommit;
		txn->owner = chan;
		txn->active = 1;

		/* On creation, the txn becomes active, and all others inactive */
		AST_LIST_TRAVERSE(oldlist, otxn, list) {
			otxn->active = 0;
		}
		AST_LIST_INSERT_TAIL(oldlist, txn, list);

		obj->txf = txn;
		obj->tx = 1;
	}
	AST_LIST_UNLOCK(oldlist);

	return txn;
}

static struct odbc_txn_frame *release_transaction(struct odbc_txn_frame *tx)
{
	if (!tx) {
		return NULL;
	}

	ast_debug(2, "release_transaction(%p) called (tx->obj = %p, tx->obj->txf = %p)\n", tx, tx->obj, tx->obj ? tx->obj->txf : NULL);

	/* If we have an owner, disassociate */
	if (tx->owner) {
		struct ast_datastore *txn_store;
		AST_LIST_HEAD(, odbc_txn_frame) *oldlist;

		ast_channel_lock(tx->owner);
		if ((txn_store = ast_channel_datastore_find(tx->owner, &txn_info, NULL))) {
			oldlist = txn_store->data;
			AST_LIST_LOCK(oldlist);
			AST_LIST_REMOVE(oldlist, tx, list);
			AST_LIST_UNLOCK(oldlist);
		}
		ast_channel_unlock(tx->owner);
		tx->owner = NULL;
	}

	if (tx->obj) {
		/* If we have any uncommitted transactions, they are handled when we release the object */
		struct odbc_obj *obj = tx->obj;
		/* Prevent recursion during destruction */
		tx->obj->txf = NULL;
		tx->obj = NULL;
		odbc_release_obj2(obj, tx);
	}
	ast_free(tx);
	return NULL;
}

static void odbc_txn_free(void *vdata)
{
	struct odbc_txn_frame *tx;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist = vdata;

	ast_debug(2, "odbc_txn_free(%p) called\n", vdata);

	AST_LIST_LOCK(oldlist);
	while ((tx = AST_LIST_REMOVE_HEAD(oldlist, list))) {
		release_transaction(tx);
	}
	AST_LIST_UNLOCK(oldlist);
	AST_LIST_HEAD_DESTROY(oldlist);
	ast_free(oldlist);
}

static int mark_transaction_active(struct ast_channel *chan, struct odbc_txn_frame *tx)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *active = NULL, *txn;

	if (!chan && tx && tx->owner) {
		chan = tx->owner;
	}

	ast_channel_lock(chan);
	if (!(txn_store = ast_channel_datastore_find(chan, &txn_info, NULL))) {
		ast_channel_unlock(chan);
		return -1;
	}

	oldlist = txn_store->data;
	AST_LIST_LOCK(oldlist);
	AST_LIST_TRAVERSE(oldlist, txn, list) {
		if (txn == tx) {
			txn->active = 1;
			active = txn;
		} else {
			txn->active = 0;
		}
	}
	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);
	return active ? 0 : -1;
}

static void odbc_class_destructor(void *data)
{
	struct odbc_class *class = data;
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
	ao2_ref(class->obj_container, -1);
	SQLFreeHandle(SQL_HANDLE_ENV, class->env);
}

static int null_hash_fn(const void *obj, const int flags)
{
	return 0;
}

static void odbc_obj_destructor(void *data)
{
	struct odbc_obj *obj = data;
	struct odbc_class *class = obj->parent;
	obj->parent = NULL;
	odbc_obj_disconnect(obj);
	ast_mutex_destroy(&obj->lock);
	ao2_ref(class, -1);
}

static void destroy_table_cache(struct odbc_cache_tables *table) {
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
 * \since 1.6.1
 */
struct odbc_cache_tables *ast_odbc_find_table(const char *database, const char *tablename)
{
	struct odbc_cache_tables *tableptr;
	struct odbc_cache_columns *entry;
	char columnname[80];
	SQLLEN sqlptr;
	SQLHSTMT stmt = NULL;
	int res = 0, error = 0, try = 0;
	struct odbc_obj *obj = ast_odbc_request_obj(database, 0);

	AST_RWLIST_RDLOCK(&odbc_tables);
	AST_RWLIST_TRAVERSE(&odbc_tables, tableptr, list) {
		if (strcmp(tableptr->connection, database) == 0 && strcmp(tableptr->table, tablename) == 0) {
			break;
		}
	}
	if (tableptr) {
		AST_RWLIST_RDLOCK(&tableptr->columns);
		AST_RWLIST_UNLOCK(&odbc_tables);
		if (obj) {
			ast_odbc_release_obj(obj);
		}
		return tableptr;
	}

	if (!obj) {
		ast_log(LOG_WARNING, "Unable to retrieve database handle for table description '%s@%s'\n", tablename, database);
		AST_RWLIST_UNLOCK(&odbc_tables);
		return NULL;
	}

	/* Table structure not already cached; build it now. */
	do {
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			if (try == 0) {
				try = 1;
				ast_odbc_sanity_check(obj);
				continue;
			}
			ast_log(LOG_WARNING, "SQL Alloc Handle failed on connection '%s'!\n", database);
			break;
		}

		res = SQLColumns(stmt, NULL, 0, NULL, 0, (unsigned char *)tablename, SQL_NTS, (unsigned char *)"%", SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			if (try == 0) {
				try = 1;
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				ast_odbc_sanity_check(obj);
				continue;
			}
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

			ast_verb(10, "Found %s column with type %hd with len %ld, octetlen %ld, and numlen (%hd,%hd)\n", entry->name, entry->type, (long) entry->size, (long) entry->octetlen, entry->decimals, entry->radix);
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
	if (obj) {
		ast_odbc_release_obj(obj);
	}
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
	int attempt;
	SQLHSTMT stmt;

	for (attempt = 0; attempt < 2; attempt++) {
		stmt = exec_cb(obj, data);

		if (stmt) {
			break;
		} else if (obj->tx) {
			ast_log(LOG_WARNING, "Failed to execute, but unable to reconnect, as we're transactional.\n");
			break;
		} else if (attempt == 0) {
			ast_log(LOG_WARNING, "SQL Execute error! Verifying connection to %s [%s]...\n", obj->parent->name, obj->parent->dsn);
		}
		if (!ast_odbc_sanity_check(obj)) {
			break;
		}
	}

	return stmt;
}

SQLHSTMT ast_odbc_prepare_and_execute(struct odbc_obj *obj, SQLHSTMT (*prepare_cb)(struct odbc_obj *obj, void *data), void *data)
{
	int res = 0, i, attempt;
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0;
	unsigned char state[10], diagnostic[256];
	SQLHSTMT stmt;

	for (attempt = 0; attempt < 2; attempt++) {
		/* This prepare callback may do more than just prepare -- it may also
		 * bind parameters, bind results, etc.  The real key, here, is that
		 * when we disconnect, all handles become invalid for most databases.
		 * We must therefore redo everything when we establish a new
		 * connection. */
		stmt = prepare_cb(obj, data);

		if (stmt) {
			res = SQLExecute(stmt);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO) && (res != SQL_NO_DATA)) {
				if (res == SQL_ERROR) {
					SQLGetDiagField(SQL_HANDLE_STMT, stmt, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
					for (i = 0; i < numfields; i++) {
						SQLGetDiagRec(SQL_HANDLE_STMT, stmt, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
						ast_log(LOG_WARNING, "SQL Execute returned an error %d: %s: %s (%d)\n", res, state, diagnostic, diagbytes);
						if (i > 10) {
							ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
							break;
						}
					}
				}

				if (obj->tx) {
					ast_log(LOG_WARNING, "SQL Execute error, but unable to reconnect, as we're transactional.\n");
					break;
				} else {
					ast_log(LOG_WARNING, "SQL Execute error %d! Verifying connection to %s [%s]...\n", res, obj->parent->name, obj->parent->dsn);
					SQLFreeHandle(SQL_HANDLE_STMT, stmt);
					stmt = NULL;

					obj->up = 0;
					/*
					 * While this isn't the best way to try to correct an error, this won't automatically
					 * fail when the statement handle invalidates.
					 */
					if (!ast_odbc_sanity_check(obj)) {
						break;
					}
					continue;
				}
			} else {
				obj->last_used = ast_tvnow();
			}
			break;
		} else if (attempt == 0) {
			ast_odbc_sanity_check(obj);
		}
	}

	return stmt;
}

int ast_odbc_smart_execute(struct odbc_obj *obj, SQLHSTMT stmt)
{
	int res = 0, i;
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0;
	unsigned char state[10], diagnostic[256];

	res = SQLExecute(stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO) && (res != SQL_NO_DATA)) {
		if (res == SQL_ERROR) {
			SQLGetDiagField(SQL_HANDLE_STMT, stmt, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_STMT, stmt, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SQL Execute returned an error %d: %s: %s (%d)\n", res, state, diagnostic, diagbytes);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		}
	} else {
		obj->last_used = ast_tvnow();
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

int ast_odbc_sanity_check(struct odbc_obj *obj) 
{
	char *test_sql = "select 1";
	SQLHSTMT stmt;
	int res = 0;

	if (!ast_strlen_zero(obj->parent->sanitysql))
		test_sql = obj->parent->sanitysql;

	if (obj->up) {
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			obj->up = 0;
		} else {
			res = SQLPrepare(stmt, (unsigned char *)test_sql, SQL_NTS);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				obj->up = 0;
			} else {
				res = SQLExecute(stmt);
				if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
					obj->up = 0;
				}
			}
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	}

	if (!obj->up && !obj->tx) { /* Try to reconnect! */
		ast_log(LOG_WARNING, "Connection is down attempting to reconnect...\n");
		odbc_obj_disconnect(obj);
		odbc_obj_connect(obj);
	}
	return obj->up;
}

static int load_odbc_config(void)
{
	static char *cfg = "res_odbc.conf";
	struct ast_config *config;
	struct ast_variable *v;
	char *cat;
	const char *dsn, *username, *password, *sanitysql;
	int enabled, pooling, limit, bse, conntimeout, forcecommit, isolation;
	struct timeval ncache = { 0, 0 };
	unsigned int idlecheck;
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
			preconnect = idlecheck = 0;
			pooling = 0;
			limit = 0;
			bse = 1;
			conntimeout = 10;
			forcecommit = 0;
			isolation = SQL_TXN_READ_COMMITTED;
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "pooling")) {
					if (ast_true(v->value))
						pooling = 1;
				} else if (!strncasecmp(v->name, "share", 5)) {
					/* "shareconnections" is a little clearer in meaning than "pooling" */
					if (ast_false(v->value))
						pooling = 1;
				} else if (!strcasecmp(v->name, "limit")) {
					sscanf(v->value, "%30d", &limit);
					if (ast_true(v->value) && !limit) {
						ast_log(LOG_WARNING, "Limit should be a number, not a boolean: '%s'.  Setting limit to 1023 for ODBC class '%s'.\n", v->value, cat);
						limit = 1023;
					} else if (ast_false(v->value)) {
						ast_log(LOG_WARNING, "Limit should be a number, not a boolean: '%s'.  Disabling ODBC class '%s'.\n", v->value, cat);
						enabled = 0;
						break;
					}
				} else if (!strcasecmp(v->name, "idlecheck")) {
					sscanf(v->value, "%30u", &idlecheck);
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
					if ((isolation = text2isolation(v->value)) == 0) {
						ast_log(LOG_ERROR, "Unrecognized value for 'isolation': '%s' in section '%s'\n", v->value, cat);
						isolation = SQL_TXN_READ_COMMITTED;
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

				new->obj_container = ao2_container_alloc(1, null_hash_fn, ao2_match_by_addr);

				if (pooling) {
					new->haspool = pooling;
					if (limit) {
						new->limit = limit;
					} else {
						ast_log(LOG_WARNING, "Pooling without also setting a limit is pointless.  Changing limit from 0 to 5.\n");
						new->limit = 5;
					}
				}

				new->backslash_is_escape = bse ? 1 : 0;
				new->forcecommit = forcecommit ? 1 : 0;
				new->isolation = isolation;
				new->idlecheck = idlecheck;
				new->conntimeout = conntimeout;
				new->negative_connection_cache = ncache;

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
	struct ao2_iterator aoi = ao2_iterator_init(class_container, 0);
	struct odbc_class *class;
	struct odbc_obj *current;
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
			int count = 0;
			char timestr[80];
			struct ast_tm tm;

			ast_localtime(&class->last_negative_connect, &tm, NULL);
			ast_strftime(timestr, sizeof(timestr), "%Y-%m-%d %T", &tm);
			ast_cli(a->fd, "  Name:   %s\n  DSN:    %s\n", class->name, class->dsn);
			ast_cli(a->fd, "    Last connection attempt: %s\n", timestr);

			if (class->haspool) {
				struct ao2_iterator aoi2 = ao2_iterator_init(class->obj_container, 0);

				ast_cli(a->fd, "  Pooled: Yes\n  Limit:  %d\n  Connections in use: %d\n", class->limit, class->count);

				while ((current = ao2_iterator_next(&aoi2))) {
					ast_mutex_lock(&current->lock);
#ifdef DEBUG_THREADS
					ast_cli(a->fd, "    - Connection %d: %s (%s:%d %s)\n", ++count,
						current->used ? "in use" :
						current->up && ast_odbc_sanity_check(current) ? "connected" : "disconnected",
						current->file, current->lineno, current->function);
#else
					ast_cli(a->fd, "    - Connection %d: %s\n", ++count,
						current->used ? "in use" :
						current->up && ast_odbc_sanity_check(current) ? "connected" : "disconnected");
#endif
					ast_mutex_unlock(&current->lock);
					ao2_ref(current, -1);
				}
				ao2_iterator_destroy(&aoi2);
			} else {
				/* Should only ever be one of these (unless there are transactions) */
				struct ao2_iterator aoi2 = ao2_iterator_init(class->obj_container, 0);
				while ((current = ao2_iterator_next(&aoi2))) {
					ast_cli(a->fd, "  Pooled: No\n  Connected: %s\n", current->used ? "In use" :
						current->up && ast_odbc_sanity_check(current) ? "Yes" : "No");
					ao2_ref(current, -1);
				}
				ao2_iterator_destroy(&aoi2);
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

static int odbc_register_class(struct odbc_class *class, int preconnect)
{
	struct odbc_obj *obj;
	if (class) {
		ao2_link(class_container, class);
		/* I still have a reference in the caller, so a deref is NOT missing here. */

		if (preconnect) {
			/* Request and release builds a connection */
			obj = ast_odbc_request_obj(class->name, 0);
			if (obj) {
				ast_odbc_release_obj(obj);
			}
		}

		return 0;
	} else {
		ast_log(LOG_WARNING, "Attempted to register a NULL class?\n");
		return -1;
	}
}

static void odbc_release_obj2(struct odbc_obj *obj, struct odbc_txn_frame *tx)
{
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0, i;
	unsigned char state[10], diagnostic[256];

	ast_debug(2, "odbc_release_obj2(%p) called (obj->txf = %p)\n", obj, obj->txf);
	if (tx) {
		ast_debug(1, "called on a transactional handle with %s\n", tx->forcecommit ? "COMMIT" : "ROLLBACK");
		if (SQLEndTran(SQL_HANDLE_DBC, obj->con, tx->forcecommit ? SQL_COMMIT : SQL_ROLLBACK) == SQL_ERROR) {
			/* Handle possible transaction commit failure */
			SQLGetDiagField(SQL_HANDLE_DBC, obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SQLEndTran returned an error: %s: %s\n", state, diagnostic);
				if (!strcmp((char *)state, "25S02") || !strcmp((char *)state, "08007")) {
					/* These codes mean that a commit failed and a transaction
					 * is still active. We must rollback, or things will get
					 * very, very weird for anybody using the handle next. */
					SQLEndTran(SQL_HANDLE_DBC, obj->con, SQL_ROLLBACK);
				}
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		}

		/* Transaction is done, reset autocommit */
		if (SQLSetConnectAttr(obj->con, SQL_ATTR_AUTOCOMMIT, (void *)SQL_AUTOCOMMIT_ON, 0) == SQL_ERROR) {
			SQLGetDiagField(SQL_HANDLE_DBC, obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SetConnectAttr (Autocommit) returned an error: %s: %s\n", state, diagnostic);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		}
	}

#ifdef DEBUG_THREADS
	obj->file[0] = '\0';
	obj->function[0] = '\0';
	obj->lineno = 0;
#endif

	/* For pooled connections, this frees the connection to be
	 * reused.  For non-pooled connections, it does nothing. */
	obj->used = 0;
	if (obj->txf) {
		/* Prevent recursion -- transaction is already closed out. */
		obj->txf->obj = NULL;
		obj->txf = release_transaction(obj->txf);
	}
	ao2_ref(obj, -1);
}

void ast_odbc_release_obj(struct odbc_obj *obj)
{
	struct odbc_txn_frame *tx = find_transaction(NULL, obj, NULL, 0);
	odbc_release_obj2(obj, tx);
}

int ast_odbc_backslash_is_escape(struct odbc_obj *obj)
{
	return obj->parent->backslash_is_escape;
}

static int commit_exec(struct ast_channel *chan, const char *data)
{
	struct odbc_txn_frame *tx;
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0, i;
	unsigned char state[10], diagnostic[256];

	if (ast_strlen_zero(data)) {
		tx = find_transaction(chan, NULL, NULL, 1);
	} else {
		tx = find_transaction(chan, NULL, data, 0);
	}

	pbx_builtin_setvar_helper(chan, "COMMIT_RESULT", "OK");

	if (tx) {
		if (SQLEndTran(SQL_HANDLE_DBC, tx->obj->con, SQL_COMMIT) == SQL_ERROR) {
			struct ast_str *errors = ast_str_thread_get(&errors_buf, 16);
			ast_str_reset(errors);

			/* Handle possible transaction commit failure */
			SQLGetDiagField(SQL_HANDLE_DBC, tx->obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, tx->obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_str_append(&errors, 0, "%s%s", ast_str_strlen(errors) ? "," : "", state);
				ast_log(LOG_WARNING, "SQLEndTran returned an error: %s: %s\n", state, diagnostic);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
			pbx_builtin_setvar_helper(chan, "COMMIT_RESULT", ast_str_buffer(errors));
		}
	}
	return 0;
}

static int rollback_exec(struct ast_channel *chan, const char *data)
{
	struct odbc_txn_frame *tx;
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0, i;
	unsigned char state[10], diagnostic[256];

	if (ast_strlen_zero(data)) {
		tx = find_transaction(chan, NULL, NULL, 1);
	} else {
		tx = find_transaction(chan, NULL, data, 0);
	}

	pbx_builtin_setvar_helper(chan, "ROLLBACK_RESULT", "OK");

	if (tx) {
		if (SQLEndTran(SQL_HANDLE_DBC, tx->obj->con, SQL_ROLLBACK) == SQL_ERROR) {
			struct ast_str *errors = ast_str_thread_get(&errors_buf, 16);
			ast_str_reset(errors);

			/* Handle possible transaction commit failure */
			SQLGetDiagField(SQL_HANDLE_DBC, tx->obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, tx->obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_str_append(&errors, 0, "%s%s", ast_str_strlen(errors) ? "," : "", state);
				ast_log(LOG_WARNING, "SQLEndTran returned an error: %s: %s\n", state, diagnostic);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
			pbx_builtin_setvar_helper(chan, "ROLLBACK_RESULT", ast_str_buffer(errors));
		}
	}
	return 0;
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

#define USE_TX (void *)(long)1
#define NO_TX  (void *)(long)2
#define EOR_TX (void *)(long)3

static int aoro2_obj_cb(void *vobj, void *arg, int flags)
{
	struct odbc_obj *obj = vobj;
	ast_mutex_lock(&obj->lock);
	if ((arg == NO_TX && !obj->tx) || (arg == EOR_TX && !obj->used) || (arg == USE_TX && obj->tx && !obj->used)) {
		obj->used = 1;
		ast_mutex_unlock(&obj->lock);
		return CMP_MATCH | CMP_STOP;
	}
	ast_mutex_unlock(&obj->lock);
	return 0;
}

struct odbc_obj *_ast_odbc_request_obj2(const char *name, struct ast_flags flags, const char *file, const char *function, int lineno)
{
	struct odbc_obj *obj = NULL;
	struct odbc_class *class;
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0, i;
	unsigned char state[10], diagnostic[256];

	if (!(class = ao2_callback(class_container, 0, aoro2_class_cb, (char *) name))) {
		ast_debug(1, "Class '%s' not found!\n", name);
		return NULL;
	}

	ast_assert(ao2_ref(class, 0) > 1);

	if (class->haspool) {
		/* Recycle connections before building another */
		obj = ao2_callback(class->obj_container, 0, aoro2_obj_cb, EOR_TX);

		if (obj) {
			ast_assert(ao2_ref(obj, 0) > 1);
		}
		if (!obj && (ast_atomic_fetchadd_int(&class->count, +1) < class->limit) &&
				(time(NULL) > class->last_negative_connect.tv_sec + class->negative_connection_cache.tv_sec)) {
			obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor);
			if (!obj) {
				class->count--;
				ao2_ref(class, -1);
				ast_debug(3, "Unable to allocate object\n");
				ast_atomic_fetchadd_int(&class->count, -1);
				return NULL;
			}
			ast_assert(ao2_ref(obj, 0) == 1);
			ast_mutex_init(&obj->lock);
			/* obj inherits the outstanding reference to class */
			obj->parent = class;
			class = NULL;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ast_assert(ao2_ref(obj->parent, 0) > 0);
				/* Because it was never within the container, we have to manually decrement the count here */
				ast_atomic_fetchadd_int(&obj->parent->count, -1);
				ao2_ref(obj, -1);
				obj = NULL;
			} else {
				obj->used = 1;
				ao2_link(obj->parent->obj_container, obj);
			}
		} else {
			/* If construction fails due to the limit (or negative timecache), reverse our increment. */
			if (!obj) {
				ast_atomic_fetchadd_int(&class->count, -1);
			}
			/* Object is not constructed, so delete outstanding reference to class. */
			ao2_ref(class, -1);
			class = NULL;
		}

		if (obj && ast_test_flag(&flags, RES_ODBC_INDEPENDENT_CONNECTION)) {
			/* Ensure this connection has autocommit turned off. */
			if (SQLSetConnectAttr(obj->con, SQL_ATTR_AUTOCOMMIT, (void *)SQL_AUTOCOMMIT_OFF, 0) == SQL_ERROR) {
				SQLGetDiagField(SQL_HANDLE_DBC, obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
				for (i = 0; i < numfields; i++) {
					SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
					ast_log(LOG_WARNING, "SQLSetConnectAttr (Autocommit) returned an error: %s: %s\n", state, diagnostic);
					if (i > 10) {
						ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
						break;
					}
				}
			}
		}
	} else if (ast_test_flag(&flags, RES_ODBC_INDEPENDENT_CONNECTION)) {
		/* Non-pooled connections -- but must use a separate connection handle */
		if (!(obj = ao2_callback(class->obj_container, 0, aoro2_obj_cb, USE_TX))) {
			ast_debug(1, "Object not found\n");
			obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor);
			if (!obj) {
				ao2_ref(class, -1);
				ast_debug(3, "Unable to allocate object\n");
				return NULL;
			}
			ast_mutex_init(&obj->lock);
			/* obj inherits the outstanding reference to class */
			obj->parent = class;
			class = NULL;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ao2_ref(obj, -1);
				obj = NULL;
			} else {
				obj->used = 1;
				ao2_link(obj->parent->obj_container, obj);
				ast_atomic_fetchadd_int(&obj->parent->count, +1);
			}
		}

		if (obj && SQLSetConnectAttr(obj->con, SQL_ATTR_AUTOCOMMIT, (void *)SQL_AUTOCOMMIT_OFF, 0) == SQL_ERROR) {
			SQLGetDiagField(SQL_HANDLE_DBC, obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SetConnectAttr (Autocommit) returned an error: %s: %s\n", state, diagnostic);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		}
	} else {
		/* Non-pooled connection: multiple modules can use the same connection. */
		if ((obj = ao2_callback(class->obj_container, 0, aoro2_obj_cb, NO_TX))) {
			/* Object is not constructed, so delete outstanding reference to class. */
			ast_assert(ao2_ref(class, 0) > 1);
			ao2_ref(class, -1);
			class = NULL;
		} else {
			/* No entry: build one */
			if (!(obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor))) {
				ast_assert(ao2_ref(class, 0) > 1);
				ao2_ref(class, -1);
				ast_debug(3, "Unable to allocate object\n");
				return NULL;
			}
			ast_mutex_init(&obj->lock);
			/* obj inherits the outstanding reference to class */
			obj->parent = class;
			class = NULL;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ao2_ref(obj, -1);
				obj = NULL;
			} else {
				ao2_link(obj->parent->obj_container, obj);
				ast_assert(ao2_ref(obj, 0) > 1);
			}
		}

		if (obj && SQLSetConnectAttr(obj->con, SQL_ATTR_AUTOCOMMIT, (void *)SQL_AUTOCOMMIT_ON, 0) == SQL_ERROR) {
			SQLGetDiagField(SQL_HANDLE_DBC, obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SetConnectAttr (Autocommit) returned an error: %s: %s\n", state, diagnostic);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		}
	}

	/* Set the isolation property */
	if (obj && SQLSetConnectAttr(obj->con, SQL_ATTR_TXN_ISOLATION, (void *)(long)obj->parent->isolation, 0) == SQL_ERROR) {
		SQLGetDiagField(SQL_HANDLE_DBC, obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
		for (i = 0; i < numfields; i++) {
			SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
			ast_log(LOG_WARNING, "SetConnectAttr (Txn isolation) returned an error: %s: %s\n", state, diagnostic);
			if (i > 10) {
				ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
				break;
			}
		}
	}

	if (obj && ast_test_flag(&flags, RES_ODBC_CONNECTED) && !obj->up) {
		/* Check if this connection qualifies for reconnection, with negative connection cache time */
		if (time(NULL) > obj->parent->last_negative_connect.tv_sec + obj->parent->negative_connection_cache.tv_sec) {
			odbc_obj_connect(obj);
		}
	} else if (obj && ast_test_flag(&flags, RES_ODBC_SANITY_CHECK)) {
		ast_odbc_sanity_check(obj);
	} else if (obj && obj->parent->idlecheck > 0 && ast_tvdiff_sec(ast_tvnow(), obj->last_used) > obj->parent->idlecheck) {
		odbc_obj_connect(obj);
	}

#ifdef DEBUG_THREADS
	if (obj) {
		ast_copy_string(obj->file, file, sizeof(obj->file));
		ast_copy_string(obj->function, function, sizeof(obj->function));
		obj->lineno = lineno;
	}
#endif
	ast_assert(class == NULL);

	if (obj) {
		ast_assert(ao2_ref(obj, 0) > 1);
	}
	return obj;
}

struct odbc_obj *_ast_odbc_request_obj(const char *name, int check, const char *file, const char *function, int lineno)
{
	struct ast_flags flags = { check ? RES_ODBC_SANITY_CHECK : 0 };
	return _ast_odbc_request_obj2(name, flags, file, function, lineno);
}

struct odbc_obj *ast_odbc_retrieve_transaction_obj(struct ast_channel *chan, const char *objname)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *txn = NULL;

	if (!chan) {
		/* No channel == no transaction */
		return NULL;
	}

	ast_channel_lock(chan);
	if ((txn_store = ast_channel_datastore_find(chan, &txn_info, NULL))) {
		oldlist = txn_store->data;
	} else {
		ast_channel_unlock(chan);
		return NULL;
	}

	AST_LIST_LOCK(oldlist);
	ast_channel_unlock(chan);

	AST_LIST_TRAVERSE(oldlist, txn, list) {
		if (txn->obj && txn->obj->parent && !strcmp(txn->obj->parent->name, objname)) {
			AST_LIST_UNLOCK(oldlist);
			return txn->obj;
		}
	}
	AST_LIST_UNLOCK(oldlist);
	return NULL;
}

static odbc_status odbc_obj_disconnect(struct odbc_obj *obj)
{
	int res;
	SQLINTEGER err;
	short int mlen;
	unsigned char msg[200], state[10];

	/* Nothing to disconnect */
	if (!obj->con) {
		return ODBC_SUCCESS;
	}

	ast_mutex_lock(&obj->lock);

	res = SQLDisconnect(obj->con);

	if (obj->parent) {
		if (res == SQL_SUCCESS || res == SQL_SUCCESS_WITH_INFO) {
			ast_debug(1, "Disconnected %d from %s [%s]\n", res, obj->parent->name, obj->parent->dsn);
		} else {
			ast_debug(1, "res_odbc: %s [%s] already disconnected\n", obj->parent->name, obj->parent->dsn);
		}
	}

	if ((res = SQLFreeHandle(SQL_HANDLE_DBC, obj->con) == SQL_SUCCESS)) {
		obj->con = NULL;
		ast_debug(1, "Database handle deallocated\n");
	} else {
		SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, 1, state, &err, msg, 100, &mlen);
		ast_log(LOG_WARNING, "Unable to deallocate database handle? %d errno=%d %s\n", res, (int)err, msg);
	}

	obj->up = 0;
	ast_mutex_unlock(&obj->lock);
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
	ast_mutex_lock(&obj->lock);

	if (obj->up) {
		odbc_obj_disconnect(obj);
		ast_log(LOG_NOTICE, "Re-connecting %s\n", obj->parent->name);
	} else {
		ast_log(LOG_NOTICE, "Connecting %s\n", obj->parent->name);
	}

	res = SQLAllocHandle(SQL_HANDLE_DBC, obj->parent->env, &obj->con);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "res_odbc: Error AllocHDB %d\n", res);
		obj->parent->last_negative_connect = ast_tvnow();
		ast_mutex_unlock(&obj->lock);
		return ODBC_FAIL;
	}
	SQLSetConnectAttr(obj->con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)(long) obj->parent->conntimeout, 0);
	SQLSetConnectAttr(obj->con, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER *)(long) obj->parent->conntimeout, 0);
#ifdef NEEDTRACE
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACE, &enable, SQL_IS_INTEGER);
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACEFILE, tracefile, strlen(tracefile));
#endif

	res = SQLConnect(obj->con,
		   (SQLCHAR *) obj->parent->dsn, SQL_NTS,
		   (SQLCHAR *) obj->parent->username, SQL_NTS,
		   (SQLCHAR *) obj->parent->password, SQL_NTS);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, 1, state, &err, msg, 100, &mlen);
		obj->parent->last_negative_connect = ast_tvnow();
		ast_mutex_unlock(&obj->lock);
		ast_log(LOG_WARNING, "res_odbc: Error SQLConnect=%d errno=%d %s\n", res, (int)err, msg);
		return ODBC_FAIL;
	} else {
		ast_log(LOG_NOTICE, "res_odbc: Connected to %s [%s]\n", obj->parent->name, obj->parent->dsn);
		obj->up = 1;
		obj->last_used = ast_tvnow();
	}

	ast_mutex_unlock(&obj->lock);
	return ODBC_SUCCESS;
}

static int acf_transaction_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(property);
		AST_APP_ARG(opt);
	);
	struct odbc_txn_frame *tx;

	AST_STANDARD_APP_ARGS(args, data);
	if (strcasecmp(args.property, "transaction") == 0) {
		if ((tx = find_transaction(chan, NULL, NULL, 1))) {
			ast_copy_string(buf, tx->name, len);
			return 0;
		}
	} else if (strcasecmp(args.property, "isolation") == 0) {
		if (!ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, NULL, args.opt, 0);
		} else {
			tx = find_transaction(chan, NULL, NULL, 1);
		}
		if (tx) {
			ast_copy_string(buf, isolation2text(tx->isolation), len);
			return 0;
		}
	} else if (strcasecmp(args.property, "forcecommit") == 0) {
		if (!ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, NULL, args.opt, 0);
		} else {
			tx = find_transaction(chan, NULL, NULL, 1);
		}
		if (tx) {
			ast_copy_string(buf, tx->forcecommit ? "1" : "0", len);
			return 0;
		}
	}
	return -1;
}

static int acf_transaction_write(struct ast_channel *chan, const char *cmd, char *s, const char *value)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(property);
		AST_APP_ARG(opt);
	);
	struct odbc_txn_frame *tx;
	SQLINTEGER nativeerror=0, numfields=0;
	SQLSMALLINT diagbytes=0, i;
	unsigned char state[10], diagnostic[256];

	AST_STANDARD_APP_ARGS(args, s);
	if (strcasecmp(args.property, "transaction") == 0) {
		/* Set active transaction */
		struct odbc_obj *obj;
		if ((tx = find_transaction(chan, NULL, value, 0))) {
			mark_transaction_active(chan, tx);
		} else {
			/* No such transaction, create one */
			struct ast_flags flags = { RES_ODBC_INDEPENDENT_CONNECTION };
			if (ast_strlen_zero(args.opt) || !(obj = ast_odbc_request_obj2(args.opt, flags))) {
				ast_log(LOG_ERROR, "Could not create transaction: invalid database specification '%s'\n", S_OR(args.opt, ""));
				pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "INVALID_DB");
				return -1;
			}
			if (!(tx = find_transaction(chan, obj, value, 0))) {
				pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "FAILED_TO_CREATE");
				return -1;
			}
			obj->tx = 1;
		}
		pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "OK");
		return 0;
	} else if (strcasecmp(args.property, "forcecommit") == 0) {
		/* Set what happens when an uncommitted transaction ends without explicit Commit or Rollback */
		if (ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, NULL, NULL, 1);
		} else {
			tx = find_transaction(chan, NULL, args.opt, 0);
		}
		if (!tx) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "FAILED_TO_CREATE");
			return -1;
		}
		if (ast_true(value)) {
			tx->forcecommit = 1;
		} else if (ast_false(value)) {
			tx->forcecommit = 0;
		} else {
			ast_log(LOG_ERROR, "Invalid value for forcecommit: '%s'\n", S_OR(value, ""));
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "INVALID_VALUE");
			return -1;
		}

		pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "OK");
		return 0;
	} else if (strcasecmp(args.property, "isolation") == 0) {
		/* How do uncommitted transactions affect reads? */
		int isolation = text2isolation(value);
		if (ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, NULL, NULL, 1);
		} else {
			tx = find_transaction(chan, NULL, args.opt, 0);
		}
		if (!tx) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "FAILED_TO_CREATE");
			return -1;
		}
		if (isolation == 0) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "INVALID_VALUE");
			ast_log(LOG_ERROR, "Invalid isolation specification: '%s'\n", S_OR(value, ""));
		} else if (SQLSetConnectAttr(tx->obj->con, SQL_ATTR_TXN_ISOLATION, (void *)(long)isolation, 0) == SQL_ERROR) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "SQL_ERROR");
			SQLGetDiagField(SQL_HANDLE_DBC, tx->obj->con, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
			for (i = 0; i < numfields; i++) {
				SQLGetDiagRec(SQL_HANDLE_DBC, tx->obj->con, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
				ast_log(LOG_WARNING, "SetConnectAttr (Txn isolation) returned an error: %s: %s\n", state, diagnostic);
				if (i > 10) {
					ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
					break;
				}
			}
		} else {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "OK");
			tx->isolation = isolation;
		}
		return 0;
	} else {
		ast_log(LOG_ERROR, "Unknown property: '%s'\n", args.property);
		return -1;
	}
}

static struct ast_custom_function odbc_function = {
	.name = "ODBC",
	.read = acf_transaction_read,
	.write = acf_transaction_write,
};

static const char * const app_commit = "ODBC_Commit";
static const char * const app_rollback = "ODBC_Rollback";

/*!
 * \internal
 * \brief Implements the channels provider.
 */
static int data_odbc_provider_handler(const struct ast_data_search *search,
		struct ast_data *root)
{
	struct ao2_iterator aoi, aoi2;
	struct odbc_class *class;
	struct odbc_obj *current;
	struct ast_data *data_odbc_class, *data_odbc_connections, *data_odbc_connection;
	struct ast_data *enum_node;
	int count;

	aoi = ao2_iterator_init(class_container, 0);
	while ((class = ao2_iterator_next(&aoi))) {
		data_odbc_class = ast_data_add_node(root, "class");
		if (!data_odbc_class) {
			ao2_ref(class, -1);
			continue;
		}

		ast_data_add_structure(odbc_class, data_odbc_class, class);

		if (!ao2_container_count(class->obj_container)) {
			ao2_ref(class, -1);
			continue;
		}

		data_odbc_connections = ast_data_add_node(data_odbc_class, "connections");
		if (!data_odbc_connections) {
			ao2_ref(class, -1);
			continue;
		}

		ast_data_add_bool(data_odbc_class, "shared", !class->haspool);
		/* isolation */
		enum_node = ast_data_add_node(data_odbc_class, "isolation");
		if (!enum_node) {
			ao2_ref(class, -1);
			continue;
		}
		ast_data_add_int(enum_node, "value", class->isolation);
		ast_data_add_str(enum_node, "text", isolation2text(class->isolation));

		count = 0;
		aoi2 = ao2_iterator_init(class->obj_container, 0);
		while ((current = ao2_iterator_next(&aoi2))) {
			data_odbc_connection = ast_data_add_node(data_odbc_connections, "connection");
			if (!data_odbc_connection) {
				ao2_ref(current, -1);
				continue;
			}

			ast_mutex_lock(&current->lock);
			ast_data_add_str(data_odbc_connection, "status", current->used ? "in use" :
					current->up && ast_odbc_sanity_check(current) ? "connected" : "disconnected");
			ast_data_add_bool(data_odbc_connection, "transactional", current->tx);
			ast_mutex_unlock(&current->lock);

			if (class->haspool) {
				ast_data_add_int(data_odbc_connection, "number", ++count);
			}

			ao2_ref(current, -1);
		}
		ao2_ref(class, -1);

		if (!ast_data_search_match(search, data_odbc_class)) {
			ast_data_remove_node(root, data_odbc_class);
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief /asterisk/res/odbc/listprovider.
 */
static const struct ast_data_handler odbc_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = data_odbc_provider_handler
};

static const struct ast_data_entry odbc_providers[] = {
	AST_DATA_ENTRY("/asterisk/res/odbc", &odbc_provider),
};

static int reload(void)
{
	struct odbc_cache_tables *table;
	struct odbc_class *class;
	struct odbc_obj *current;
	struct ao2_iterator aoi = ao2_iterator_init(class_container, 0);

	/* First, mark all to be purged */
	while ((class = ao2_iterator_next(&aoi))) {
		class->delme = 1;
		ao2_ref(class, -1);
	}
	ao2_iterator_destroy(&aoi);

	load_odbc_config();

	/* Purge remaining classes */

	/* Note on how this works; this is a case of circular references, so we
	 * explicitly do NOT want to use a callback here (or we wind up in
	 * recursive hell).
	 *
	 * 1. Iterate through all the classes.  Note that the classes will currently
	 * contain two classes of the same name, one of which is marked delme and
	 * will be purged when all remaining objects of the class are released, and
	 * the other, which was created above when we re-parsed the config file.
	 * 2. On each class, there is a reference held by the master container and
	 * a reference held by each connection object.  There are two cases for
	 * destruction of the class, noted below.  However, in all cases, all O-refs
	 * (references to objects) will first be freed, which will cause the C-refs
	 * (references to classes) to be decremented (but never to 0, because the
	 * class container still has a reference).
	 *    a) If the class has outstanding objects, the C-ref by the class
	 *    container will then be freed, which leaves only C-refs by any
	 *    outstanding objects.  When the final outstanding object is released
	 *    (O-refs held by applications and dialplan functions), it will in turn
	 *    free the final C-ref, causing class destruction.
	 *    b) If the class has no outstanding objects, when the class container
	 *    removes the final C-ref, the class will be destroyed.
	 */
	aoi = ao2_iterator_init(class_container, 0);
	while ((class = ao2_iterator_next(&aoi))) { /* C-ref++ (by iterator) */
		if (class->delme) {
			struct ao2_iterator aoi2 = ao2_iterator_init(class->obj_container, 0);
			while ((current = ao2_iterator_next(&aoi2))) { /* O-ref++ (by iterator) */
				ao2_unlink(class->obj_container, current); /* unlink O-ref from class (reference handled implicitly) */
				ao2_ref(current, -1); /* O-ref-- (by iterator) */
				/* At this point, either
				 * a) there's an outstanding O-ref, or
				 * b) the object has already been destroyed.
				 */
			}
			ao2_iterator_destroy(&aoi2);
			ao2_unlink(class_container, class); /* unlink C-ref from container (reference handled implicitly) */
			/* At this point, either
			 * a) there's an outstanding O-ref, which holds an outstanding C-ref, or
			 * b) the last remaining C-ref is held by the iterator, which will be
			 * destroyed in the next step.
			 */
		}
		ao2_ref(class, -1); /* C-ref-- (by iterator) */
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
	/* Prohibit unloading */
	return -1;
}

static int load_module(void)
{
	if (!(class_container = ao2_container_alloc(1, null_hash_fn, ao2_match_by_addr)))
		return AST_MODULE_LOAD_DECLINE;
	if (load_odbc_config() == -1)
		return AST_MODULE_LOAD_DECLINE;
	ast_cli_register_multiple(cli_odbc, ARRAY_LEN(cli_odbc));
	ast_data_register_multiple(odbc_providers, ARRAY_LEN(odbc_providers));
	ast_register_application_xml(app_commit, commit_exec);
	ast_register_application_xml(app_rollback, rollback_exec);
	ast_custom_function_register(&odbc_function);
	ast_log(LOG_NOTICE, "res_odbc loaded.\n");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "ODBC resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_REALTIME_DEPEND,
	       );
