/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 *
 * \arg See also: \ref cdr_odbc
 */

/*** MODULEINFO
	<depend>unixodbc</depend>
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

struct odbc_class
{
	AST_LIST_ENTRY(odbc_class) list;
	char name[80];
	char dsn[80];
	char *username;
	char *password;
	char *sanitysql;
	SQLHENV env;
	unsigned int haspool:1;              /* Boolean - TDS databases need this */
	unsigned int delme:1;                /* Purge the class */
	unsigned int backslash_is_escape:1;  /* On this database, the backslash is a native escape sequence */
	unsigned int limit;                  /* 1023 wasn't enough for some people */
	unsigned int count;                  /* Running count of pooled connections */
	unsigned int idlecheck;              /* Recheck the connection if it is idle for this long */
	struct ao2_container *obj_container;
};

struct ao2_container *class_container;

static AST_RWLIST_HEAD_STATIC(odbc_tables, odbc_cache_tables);

static odbc_status odbc_obj_connect(struct odbc_obj *obj);
static odbc_status odbc_obj_disconnect(struct odbc_obj *obj);
static int odbc_register_class(struct odbc_class *class, int connect);

static void odbc_class_destructor(void *data)
{
	struct odbc_class *class = data;
	/* Due to refcounts, we can safely assume that any objects with a reference
	 * to us will prevent our destruction, so we don't need to worry about them.
	 */
	if (class->username)
		ast_free(class->username);
	if (class->password)
		ast_free(class->password);
	if (class->sanitysql)
		ast_free(class->sanitysql);
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
	odbc_obj_disconnect(obj);
	ast_mutex_destroy(&obj->lock);
	ao2_ref(obj->parent, -1);
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
 * \param obj An active ODBC handle on which to query the table
 * \param table Tablename to describe
 * \retval A structure describing the table layout, or NULL, if the table is not found or another error occurs.
 * When a structure is returned, the contained columns list will be
 * rdlock'ed, to ensure that it will be retained in memory.
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
		return NULL;
	}

	/* Table structure not already cached; build it now. */
	do {
retry:
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			if (try == 0) {
				try = 1;
				ast_odbc_sanity_check(obj);
				goto retry;
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
				goto retry;
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
	} while (0);

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
		} else {
			obj->up = 0;
			ast_log(LOG_WARNING, "SQL Exec Direct failed.  Attempting a reconnect...\n");

			odbc_obj_disconnect(obj);
			odbc_obj_connect(obj);
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

				ast_log(LOG_WARNING, "SQL Execute error %d! Attempting a reconnect...\n", res);
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				stmt = NULL;

				obj->up = 0;
				/*
				 * While this isn't the best way to try to correct an error, this won't automatically
				 * fail when the statement handle invalidates.
				 */
				ast_odbc_sanity_check(obj);
				continue;
			} else
				obj->last_used = ast_tvnow();
			break;
		} else if (attempt == 0)
			ast_odbc_sanity_check(obj);
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
	} else
		obj->last_used = ast_tvnow();
	
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

	if (!obj->up) { /* Try to reconnect! */
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
	int enabled, pooling, limit, bse;
	unsigned int idlecheck;
	int preconnect = 0, res = 0;
	struct ast_flags config_flags = { 0 };

	struct odbc_class *new;

	config = ast_config_load(cfg, config_flags);
	if (!config) {
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
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "pooling")) {
					if (ast_true(v->value))
						pooling = 1;
				} else if (!strncasecmp(v->name, "share", 5)) {
					/* "shareconnections" is a little clearer in meaning than "pooling" */
					if (ast_false(v->value))
						pooling = 1;
				} else if (!strcasecmp(v->name, "limit")) {
					sscanf(v->value, "%d", &limit);
					if (ast_true(v->value) && !limit) {
						ast_log(LOG_WARNING, "Limit should be a number, not a boolean: '%s'.  Setting limit to 1023 for ODBC class '%s'.\n", v->value, cat);
						limit = 1023;
					} else if (ast_false(v->value)) {
						ast_log(LOG_WARNING, "Limit should be a number, not a boolean: '%s'.  Disabling ODBC class '%s'.\n", v->value, cat);
						enabled = 0;
						break;
					}
				} else if (!strcasecmp(v->name, "idlecheck")) {
					sscanf(v->value, "%d", &idlecheck);
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
				new->idlecheck = idlecheck;

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
				ao2_ref(class, -1);
				break;
			}
			ao2_ref(class, -1);
		}
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
			ast_cli(a->fd, "  Name:   %s\n  DSN:    %s\n", class->name, class->dsn);

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
			} else {
				/* Should only ever be one of these */
				struct ao2_iterator aoi2 = ao2_iterator_init(class->obj_container, 0);
				while ((current = ao2_iterator_next(&aoi2))) {
					ast_cli(a->fd, "  Pooled: No\n  Connected: %s\n", current->up && ast_odbc_sanity_check(current) ? "Yes" : "No");
					ao2_ref(current, -1);
				}
			}
			ast_cli(a->fd, "\n");
		}
		ao2_ref(class, -1);
	}

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
			if (obj)
				ast_odbc_release_obj(obj);
		}

		return 0;
	} else {
		ast_log(LOG_WARNING, "Attempted to register a NULL class?\n");
		return -1;
	}
}

void ast_odbc_release_obj(struct odbc_obj *obj)
{
	/* For pooled connections, this frees the connection to be
	 * reused.  For non-pooled connections, it does nothing. */
	obj->used = 0;
#ifdef DEBUG_THREADS
	obj->file[0] = '\0';
	obj->function[0] = '\0';
	obj->lineno = 0;
#endif
	ao2_ref(obj, -1);
}

int ast_odbc_backslash_is_escape(struct odbc_obj *obj)
{
	return obj->parent->backslash_is_escape;
}

#ifdef DEBUG_THREADS
struct odbc_obj *_ast_odbc_request_obj(const char *name, int check, const char *file, const char *function, int lineno)
#else
struct odbc_obj *ast_odbc_request_obj(const char *name, int check)
#endif
{
	struct odbc_obj *obj = NULL;
	struct odbc_class *class;
	struct ao2_iterator aoi = ao2_iterator_init(class_container, 0);

	while ((class = ao2_iterator_next(&aoi))) {
		if (!strcmp(class->name, name) && !class->delme) {
			break;
		}
		ao2_ref(class, -1);
	}

	if (!class)
		return NULL;

	if (class->haspool) {
		/* Recycle connections before building another */
		aoi = ao2_iterator_init(class->obj_container, 0);
		while ((obj = ao2_iterator_next(&aoi))) {
			if (! obj->used) {
				ast_mutex_lock(&obj->lock);
				obj->used = 1;
				ast_mutex_unlock(&obj->lock);
				break;
			}
			ao2_ref(obj, -1);
		}

		if (!obj && (class->count < class->limit)) {
			class->count++;
			obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor);
			if (!obj) {
				ao2_ref(class, -1);
				return NULL;
			}
			ast_mutex_init(&obj->lock);
			/* obj inherits the outstanding reference to class */
			obj->parent = class;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ao2_ref(obj, -1);
				obj = NULL;
				class->count--;
			} else {
				obj->used = 1;
				ao2_link(class->obj_container, obj);
			}
		}
	} else {
		/* Non-pooled connection: multiple modules can use the same connection. */
		aoi = ao2_iterator_init(class->obj_container, 0);
		while ((obj = ao2_iterator_next(&aoi))) {
			/* Non-pooled connection: if there is an entry, return it */
			break;
		}

		if (!obj) {
			/* No entry: build one */
			obj = ao2_alloc(sizeof(*obj), odbc_obj_destructor);
			if (!obj) {
				ao2_ref(class, -1);
				return NULL;
			}
			ast_mutex_init(&obj->lock);
			/* obj inherits the outstanding reference to class */
			obj->parent = class;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ao2_ref(obj, -1);
				obj = NULL;
			} else {
				ao2_link(class->obj_container, obj);
			}
		}
	}

	if (obj && check) {
		ast_odbc_sanity_check(obj);
	} else if (obj && obj->parent->idlecheck > 0 && ast_tvdiff_sec(ast_tvnow(), obj->last_used) > obj->parent->idlecheck)
		odbc_obj_connect(obj);

#ifdef DEBUG_THREADS
	if (obj) {
		ast_copy_string(obj->file, file, sizeof(obj->file));
		ast_copy_string(obj->function, function, sizeof(obj->function));
		obj->lineno = lineno;
	}
#endif

	return obj;
}

static odbc_status odbc_obj_disconnect(struct odbc_obj *obj)
{
	int res;
	SQLINTEGER err;
	short int mlen;
	unsigned char msg[200], stat[10];

	ast_mutex_lock(&obj->lock);

	res = SQLDisconnect(obj->con);

	if (res == ODBC_SUCCESS) {
		ast_log(LOG_DEBUG, "Disconnected %d from %s [%s]\n", res, obj->parent->name, obj->parent->dsn);
	} else {
		ast_log(LOG_DEBUG, "res_odbc: %s [%s] already disconnected\n", obj->parent->name, obj->parent->dsn);
	}

	if ((res = SQLFreeHandle(SQL_HANDLE_DBC, obj->con) == ODBC_SUCCESS)) {
		ast_log(LOG_DEBUG, "Database handle deallocated\n");
	} else {
		SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, 1, stat, &err, msg, 100, &mlen);
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

	res = SQLAllocHandle(SQL_HANDLE_DBC, obj->parent->env, &obj->con);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "res_odbc: Error AllocHDB %d\n", res);
		ast_mutex_unlock(&obj->lock);
		return ODBC_FAIL;
	}
	SQLSetConnectAttr(obj->con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *) 10, 0);
	SQLSetConnectAttr(obj->con, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER *) 10, 0);
#ifdef NEEDTRACE
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACE, &enable, SQL_IS_INTEGER);
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACEFILE, tracefile, strlen(tracefile));
#endif

	if (obj->up) {
		odbc_obj_disconnect(obj);
		ast_log(LOG_NOTICE, "Re-connecting %s\n", obj->parent->name);
	} else {
		ast_log(LOG_NOTICE, "Connecting %s\n", obj->parent->name);
	}

	res = SQLConnect(obj->con,
		   (SQLCHAR *) obj->parent->dsn, SQL_NTS,
		   (SQLCHAR *) obj->parent->username, SQL_NTS,
		   (SQLCHAR *) obj->parent->password, SQL_NTS);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, 1, state, &err, msg, 100, &mlen);
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
			ao2_unlink(class_container, class); /* unlink C-ref from container (reference handled implicitly) */
			/* At this point, either
			 * a) there's an outstanding O-ref, which holds an outstanding C-ref, or
			 * b) the last remaining C-ref is held by the iterator, which will be
			 * destroyed in the next step.
			 */
		}
		ao2_ref(class, -1); /* C-ref-- (by iterator) */
	}

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
	ast_cli_register_multiple(cli_odbc, sizeof(cli_odbc) / sizeof(struct ast_cli_entry));
	ast_log(LOG_NOTICE, "res_odbc loaded.\n");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "ODBC resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
