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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/res_odbc.h"
#include "asterisk/time.h"

struct odbc_class
{
	AST_LIST_ENTRY(odbc_class) list;
	char name[80];
	char dsn[80];
	char username[80];
	char password[80];
	SQLHENV env;
	unsigned int haspool:1;         /* Boolean - TDS databases need this */
	unsigned int limit:10;          /* Gives a limit of 1023 maximum */
	unsigned int count:10;          /* Running count of pooled connections */
	unsigned int delme:1;			/* Purge the class */
	unsigned int backslash_is_escape:1;	/* On this database, the backslash is a native escape sequence */
	unsigned int idlecheck;			/* Recheck the connection if it is idle for this long */
	AST_LIST_HEAD(, odbc_obj) odbc_obj;
};

AST_LIST_HEAD_STATIC(odbc_list, odbc_class);

static odbc_status odbc_obj_connect(struct odbc_obj *obj);
static odbc_status odbc_obj_disconnect(struct odbc_obj *obj);
static int odbc_register_class(struct odbc_class *class, int connect);


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
				/* XXX Actually, it might, if we're using a non-pooled connection. Possible race here. XXX */
				odbc_obj_disconnect(obj);
				odbc_obj_connect(obj);
				continue;
			} else
				obj->last_used = ast_tvnow();
			break;
		} else {
			ast_log(LOG_WARNING, "SQL Prepare failed.  Attempting a reconnect...\n");
			odbc_obj_disconnect(obj);
			odbc_obj_connect(obj);
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
#if 0
		/* This is a really bad method of trying to correct a dead connection.  It
		 * only ever really worked with MySQL.  It will not work with any other
		 * database, since most databases prepare their statements on the server,
		 * and if you disconnect, you invalidate the statement handle.  Hence, if
		 * you disconnect, you're going to fail anyway, whether you try to execute
		 * a second time or not.
		 */
		ast_log(LOG_WARNING, "SQL Execute error %d! Attempting a reconnect...\n", res);
		ast_mutex_lock(&obj->lock);
		obj->up = 0;
		ast_mutex_unlock(&obj->lock);
		odbc_obj_disconnect(obj);
		odbc_obj_connect(obj);
		res = SQLExecute(stmt);
#endif
	} else
		obj->last_used = ast_tvnow();
	
	return res;
}


int ast_odbc_sanity_check(struct odbc_obj *obj) 
{
	char *test_sql = "select 1";
	SQLHSTMT stmt;
	int res = 0;

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
	char *cat, *dsn, *username, *password;
	int enabled, pooling, limit, bse;
	unsigned int idlecheck;
	int connect = 0, res = 0;

	struct odbc_class *new;

	config = ast_config_load(cfg);
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
			dsn = username = password = NULL;
			enabled = 1;
			connect = idlecheck = 0;
			pooling = 0;
			limit = 0;
			bse = 1;
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "pooling")) {
					if (ast_true(v->value))
						pooling = 1;
				} else if (!strcasecmp(v->name, "limit")) {
					sscanf(v->value, "%4d", &limit);
					if (ast_true(v->value) && !limit) {
						ast_log(LOG_WARNING, "Limit should be a number, not a boolean: '%s'.  Setting limit to 1023 for ODBC class '%s'.\n", v->value, cat);
						limit = 1023;
					} else if (ast_false(v->value)) {
						/* Limit=no probably means "no limit", which is the maximum */
						ast_log(LOG_WARNING, "Limit should be a number, not a boolean: '%s'.  Setting limit to 1023 for ODBC class '%s'.\n", v->value, cat);
						limit = 1023;
						break;
					} else if (limit > 1023) {
						ast_log(LOG_WARNING, "Maximum limit in 1.4 is 1023.  Setting limit to 1023 for ODBC class '%s'.\n", cat);
						limit = 1023;
					}
				} else if (!strcasecmp(v->name, "idlecheck")) {
					sscanf(v->value, "%30u", &idlecheck);
				} else if (!strcasecmp(v->name, "enabled")) {
					enabled = ast_true(v->value);
				} else if (!strcasecmp(v->name, "pre-connect")) {
					connect = ast_true(v->value);
				} else if (!strcasecmp(v->name, "dsn")) {
					dsn = v->value;
				} else if (!strcasecmp(v->name, "username")) {
					username = v->value;
				} else if (!strcasecmp(v->name, "password")) {
					password = v->value;
				} else if (!strcasecmp(v->name, "backslash_is_escape")) {
					bse = ast_true(v->value);
				}
			}

			if (enabled && !ast_strlen_zero(dsn)) {
				new = ast_calloc(1, sizeof(*new));

				if (!new) {
					res = -1;
					break;
				}

				if (cat)
					ast_copy_string(new->name, cat, sizeof(new->name));
				if (dsn)
					ast_copy_string(new->dsn, dsn, sizeof(new->dsn));
				if (username)
					ast_copy_string(new->username, username, sizeof(new->username));
				if (password)
					ast_copy_string(new->password, password, sizeof(new->password));

				SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &new->env);
				res = SQLSetEnvAttr(new->env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);

				if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
					ast_log(LOG_WARNING, "res_odbc: Error SetEnv\n");
					SQLFreeHandle(SQL_HANDLE_ENV, new->env);
					return res;
				}

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

				odbc_register_class(new, connect);
				ast_log(LOG_NOTICE, "Registered ODBC class '%s' dsn->[%s]\n", cat, dsn);
			}
		}
	}
	ast_config_destroy(config);
	return res;
}

static int odbc_show_command(int fd, int argc, char **argv)
{
	struct odbc_class *class;
	struct odbc_obj *current;

	AST_LIST_LOCK(&odbc_list);
	AST_LIST_TRAVERSE(&odbc_list, class, list) {
		if ((argc == 2) || (argc == 3 && !strcmp(argv[2], "all")) || (!strcmp(argv[2], class->name))) {
			int count = 0;
			ast_cli(fd, "Name: %s\nDSN: %s\n", class->name, class->dsn);

			if (class->haspool) {
				ast_cli(fd, "Pooled: yes\nLimit: %d\nConnections in use: %d\n", class->limit, class->count);

				AST_LIST_TRAVERSE(&(class->odbc_obj), current, list) {
					ast_cli(fd, "  Connection %d: %s\n", ++count, current->used ? "in use" : current->up && ast_odbc_sanity_check(current) ? "connected" : "disconnected");
				}
			} else {
				/* Should only ever be one of these */
				AST_LIST_TRAVERSE(&(class->odbc_obj), current, list) {
					ast_cli(fd, "Pooled: no\nConnected: %s\n", current->up && ast_odbc_sanity_check(current) ? "yes" : "no");
				}
			}

				ast_cli(fd, "\n");
		}
	}
	AST_LIST_UNLOCK(&odbc_list);

	return 0;
}

static char show_usage[] =
"Usage: odbc show [<class>]\n"
"       List settings of a particular ODBC class.\n"
"       or, if not specified, all classes.\n";

static struct ast_cli_entry cli_odbc[] = {
	{ { "odbc", "show", NULL },
	odbc_show_command, "List ODBC DSN(s)",
	show_usage },
};

static int odbc_register_class(struct odbc_class *class, int connect)
{
	struct odbc_obj *obj;
	if (class) {
		AST_LIST_LOCK(&odbc_list);
		AST_LIST_INSERT_HEAD(&odbc_list, class, list);
		AST_LIST_UNLOCK(&odbc_list);

		if (connect) {
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
}

int ast_odbc_backslash_is_escape(struct odbc_obj *obj)
{
	return obj->parent->backslash_is_escape;
}

struct odbc_obj *ast_odbc_request_obj(const char *name, int check)
{
	struct odbc_obj *obj = NULL;
	struct odbc_class *class;

	AST_LIST_LOCK(&odbc_list);
	AST_LIST_TRAVERSE(&odbc_list, class, list) {
		if (!strcmp(class->name, name))
			break;
	}
	AST_LIST_UNLOCK(&odbc_list);

	if (!class)
		return NULL;

	AST_LIST_LOCK(&class->odbc_obj);
	if (class->haspool) {
		/* Recycle connections before building another */
		AST_LIST_TRAVERSE(&class->odbc_obj, obj, list) {
			if (! obj->used) {
				obj->used = 1;
				break;
			}
		}

		if (!obj && (class->count < class->limit)) {
			class->count++;
			obj = ast_calloc(1, sizeof(*obj));
			if (!obj) {
				AST_LIST_UNLOCK(&class->odbc_obj);
				return NULL;
			}
			ast_mutex_init(&obj->lock);
			obj->parent = class;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ast_mutex_destroy(&obj->lock);
				free(obj);
				obj = NULL;
				class->count--;
			} else {
				obj->used = 1;
				AST_LIST_INSERT_TAIL(&class->odbc_obj, obj, list);
			}
		}
	} else {
		/* Non-pooled connection: multiple modules can use the same connection. */
		AST_LIST_TRAVERSE(&class->odbc_obj, obj, list) {
			/* Non-pooled connection: if there is an entry, return it */
			break;
		}

		if (!obj) {
			/* No entry: build one */
			obj = ast_calloc(1, sizeof(*obj));
			if (!obj) {
				AST_LIST_UNLOCK(&class->odbc_obj);
				return NULL;
			}
			ast_mutex_init(&obj->lock);
			obj->parent = class;
			if (odbc_obj_connect(obj) == ODBC_FAIL) {
				ast_log(LOG_WARNING, "Failed to connect to %s\n", name);
				ast_mutex_destroy(&obj->lock);
				free(obj);
				obj = NULL;
			} else {
				AST_LIST_INSERT_HEAD(&class->odbc_obj, obj, list);
			}
		}
	}
	AST_LIST_UNLOCK(&class->odbc_obj);

	if (obj && check) {
		ast_odbc_sanity_check(obj);
	} else if (obj && obj->parent->idlecheck > 0 && ast_tvdiff_ms(ast_tvnow(), obj->last_used) / 1000 > obj->parent->idlecheck)
		odbc_obj_connect(obj);

	return obj;
}

static odbc_status odbc_obj_disconnect(struct odbc_obj *obj)
{
	int res;
	SQLINTEGER err;
	short int mlen;
	unsigned char msg[200], stat[10];

	/* Nothing to disconnect */
	if (!obj->con) {
		return ODBC_SUCCESS;
	}

	ast_mutex_lock(&obj->lock);

	res = SQLDisconnect(obj->con);

	if (res == SQL_SUCCESS || res == SQL_SUCCESS_WITH_INFO) {
		ast_log(LOG_DEBUG, "Disconnected %d from %s [%s]\n", res, obj->parent->name, obj->parent->dsn);
	} else {
		ast_log(LOG_DEBUG, "res_odbc: %s [%s] already disconnected\n", obj->parent->name, obj->parent->dsn);
	}

	if ((res = SQLFreeHandle(SQL_HANDLE_DBC, obj->con) == SQL_SUCCESS)) {
		obj->con = NULL;
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
	unsigned char msg[200], stat[10];
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
		ast_mutex_unlock(&obj->lock);
		return ODBC_FAIL;
	}
	SQLSetConnectAttr(obj->con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *) 10, 0);
	SQLSetConnectAttr(obj->con, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER *) 10, 0);
#ifdef NEEDTRACE
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACE, &enable, SQL_IS_INTEGER);
	SQLSetConnectAttr(obj->con, SQL_ATTR_TRACEFILE, tracefile, strlen(tracefile));
#endif

	res = SQLConnect(obj->con,
		   (SQLCHAR *) obj->parent->dsn, SQL_NTS,
		   (SQLCHAR *) obj->parent->username, SQL_NTS,
		   (SQLCHAR *) obj->parent->password, SQL_NTS);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, 1, stat, &err, msg, 100, &mlen);
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
	static char *cfg = "res_odbc.conf";
	struct ast_config *config;
	struct ast_variable *v;
	char *cat, *dsn, *username, *password;
	int enabled, pooling, limit, bse;
	unsigned int idlecheck;
	int connect = 0, res = 0;

	struct odbc_class *new, *class;
	struct odbc_obj *current;

	/* First, mark all to be purged */
	AST_LIST_LOCK(&odbc_list);
	AST_LIST_TRAVERSE(&odbc_list, class, list) {
		class->delme = 1;
	}

	config = ast_config_load(cfg);
	if (config) {
		for (cat = ast_category_browse(config, NULL); cat; cat=ast_category_browse(config, cat)) {
			if (!strcasecmp(cat, "ENV")) {
				for (v = ast_variable_browse(config, cat); v; v = v->next) {
					setenv(v->name, v->value, 1);
					ast_log(LOG_NOTICE, "Adding ENV var: %s=%s\n", v->name, v->value);
				}
			} else {
				/* Reset all to defaults for each class of odbc connections */
				dsn = username = password = NULL;
				enabled = 1;
				connect = idlecheck = 0;
				pooling = 0;
				limit = 0;
				bse = 1;
				for (v = ast_variable_browse(config, cat); v; v = v->next) {
					if (!strcasecmp(v->name, "pooling")) {
						pooling = 1;
					} else if (!strcasecmp(v->name, "limit")) {
						sscanf(v->value, "%4d", &limit);
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
						connect = ast_true(v->value);
					} else if (!strcasecmp(v->name, "dsn")) {
						dsn = v->value;
					} else if (!strcasecmp(v->name, "username")) {
						username = v->value;
					} else if (!strcasecmp(v->name, "password")) {
						password = v->value;
					} else if (!strcasecmp(v->name, "backslash_is_escape")) {
						bse = ast_true(v->value);
					}
				}

				if (enabled && !ast_strlen_zero(dsn)) {
					/* First, check the list to see if it already exists */
					AST_LIST_TRAVERSE(&odbc_list, class, list) {
						if (!strcmp(class->name, cat)) {
							class->delme = 0;
							break;
						}
					}

					if (class) {
						new = class;
					} else {
						new = ast_calloc(1, sizeof(*new));
					}

					if (!new) {
						res = -1;
						break;
					}

					if (cat)
						ast_copy_string(new->name, cat, sizeof(new->name));
					if (dsn)
						ast_copy_string(new->dsn, dsn, sizeof(new->dsn));
					if (username)
						ast_copy_string(new->username, username, sizeof(new->username));
					if (password)
						ast_copy_string(new->password, password, sizeof(new->password));

					if (!class) {
						SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &new->env);
						res = SQLSetEnvAttr(new->env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);

						if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
							ast_log(LOG_WARNING, "res_odbc: Error SetEnv\n");
							SQLFreeHandle(SQL_HANDLE_ENV, new->env);
							AST_LIST_UNLOCK(&odbc_list);
							return res;
						}
					}

					if (pooling) {
						new->haspool = pooling;
						if (limit) {
							new->limit = limit;
						} else {
							ast_log(LOG_WARNING, "Pooling without also setting a limit is pointless.  Changing limit from 0 to 5.\n");
							new->limit = 5;
						}
					}

					new->backslash_is_escape = bse;
					new->idlecheck = idlecheck;

					if (class) {
						ast_log(LOG_NOTICE, "Refreshing ODBC class '%s' dsn->[%s]\n", cat, dsn);
					} else {
						odbc_register_class(new, connect);
						ast_log(LOG_NOTICE, "Registered ODBC class '%s' dsn->[%s]\n", cat, dsn);
					}
				}
			}
		}
		ast_config_destroy(config);
	}

	/* Purge classes that we know can go away (pooled with 0, only) */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&odbc_list, class, list) {
		if (class->delme && class->haspool && class->count == 0) {
			AST_LIST_TRAVERSE_SAFE_BEGIN(&(class->odbc_obj), current, list) {
				AST_LIST_REMOVE_CURRENT(&(class->odbc_obj), list);
				odbc_obj_disconnect(current);
				ast_mutex_destroy(&current->lock);
				free(current);
			}
			AST_LIST_TRAVERSE_SAFE_END;

			AST_LIST_REMOVE_CURRENT(&odbc_list, list);
			free(class);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&odbc_list);

	return 0;
}

static int unload_module(void)
{
	/* Prohibit unloading */
	return -1;
}

static int load_module(void)
{
	if(load_odbc_config() == -1)
		return AST_MODULE_LOAD_DECLINE;
	ast_cli_register_multiple(cli_odbc, sizeof(cli_odbc) / sizeof(struct ast_cli_entry));
	ast_log(LOG_NOTICE, "res_odbc loaded.\n");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "ODBC Resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
