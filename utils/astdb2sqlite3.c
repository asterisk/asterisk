/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Berkeley DB to SQLite3 converter
 *
 * \author Terry Wilson <twilson@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sqlite3.h>
#include <libgen.h> /* OS X doesn't have the basename function in strings.h */

#include "db1-ast/include/db.h"

#define MAX_DB_FIELD 256
#define MIN(a,b) \
({ typeof (a) _a = (a); \
	typeof (b) _b = (b); \
	a < _b ? _a : _b; })

static sqlite3 *astdb;

#define DEFINE_SQL_STATEMENT(stmt,sql) static sqlite3_stmt *stmt; \
	const char stmt##_sql[] = sql;

DEFINE_SQL_STATEMENT(put_stmt, "INSERT OR REPLACE INTO astdb (key, value) VALUES (?, ?)")
DEFINE_SQL_STATEMENT(create_astdb_stmt, "CREATE TABLE IF NOT EXISTS astdb(key VARCHAR(256), value VARCHAR(256), PRIMARY KEY(key))")

static int db_execute_transaction_sql(const char *sql)
{
	char *errmsg = NULL;
	int res =0;

	sqlite3_exec(astdb, sql, NULL, NULL, &errmsg);
	if (errmsg) {
		fprintf(stderr, "Error executing SQL: %s\n", errmsg);
		sqlite3_free(errmsg);
		res = -1;
	}

	return res;
}

static int ast_db_begin_transaction(void)
{
	return db_execute_transaction_sql("BEGIN TRANSACTION");
}

static int ast_db_commit_transaction(void)
{
	return db_execute_transaction_sql("COMMIT");
}

static int ast_db_rollback_transaction(void)
{
	return db_execute_transaction_sql("ROLLBACK");
}

static int db_put_raw(const char *key, size_t keylen, const char *value, size_t valuelen)
{
	int res = 0;

	if (sqlite3_bind_text(put_stmt, 1, key, keylen, SQLITE_STATIC) != SQLITE_OK) {
		fprintf(stderr, "Couldn't bind key to stmt: %s\n", sqlite3_errmsg(astdb));
		res = -1;
	} else if (sqlite3_bind_text(put_stmt, 2, value, valuelen, SQLITE_STATIC) != SQLITE_OK) {
		fprintf(stderr, "Couldn't bind value to stmt: %s\n", sqlite3_errmsg(astdb));
		res = -1;
	} else if (sqlite3_step(put_stmt) != SQLITE_DONE) {
		fprintf(stderr, "Couldn't execute statment: %s\n", sqlite3_errmsg(astdb));
		res = -1;
	}
	sqlite3_reset(put_stmt);

	return res;
}

static int convert_bdb_to_sqlite3(const char *bdb_dbname)
{
	DB *bdb;
	DBT key = { 0, }, value = { 0, }, last_key = { 0, };
	int res, last = 0;
	char last_key_s[MAX_DB_FIELD];

	if (!(bdb = dbopen(bdb_dbname, O_RDONLY, AST_FILE_MODE, DB_BTREE, NULL))) {
		fprintf(stderr, "Unable to open Asterisk database '%s'\n", bdb_dbname);
		return -1;
	}

	if (bdb->seq(bdb, &last_key, &value, R_LAST)) {
		/* Empty database */
		return 0;
	}

	memcpy(last_key_s, last_key.data, MIN(last_key.size - 1, sizeof(last_key_s)));
	last_key_s[last_key.size - 1] = '\0';
	for (res = bdb->seq(bdb, &key, &value, R_FIRST);
			!res; res = bdb->seq(bdb, &key, &value, R_NEXT)) {
		last = !strcmp(key.data, last_key_s);
		db_put_raw((const char *) key.data, key.size - 1, (const char *) value.data, value.size - 1);
		if (last) {
			break;
		}
	}

	bdb->close(bdb);

	return 0;
}

static int init_stmt(sqlite3_stmt **stmt, const char *sql, size_t len)
{
	if (sqlite3_prepare(astdb, sql, len, stmt, NULL) != SQLITE_OK) {
		fprintf(stderr, "Couldn't prepare statement '%s': %s\n", sql, sqlite3_errmsg(astdb));
		return -1;
	}

	return 0;
}

static int db_create_astdb(void)
{
	if (init_stmt(&create_astdb_stmt, create_astdb_stmt_sql, sizeof(create_astdb_stmt_sql))) {
		return -1;
	}

	ast_db_begin_transaction();
	if (sqlite3_step(create_astdb_stmt) != SQLITE_DONE) {
		fprintf(stderr, "Couldn't create astdb table: %s\n", sqlite3_errmsg(astdb));
		ast_db_rollback_transaction();
		sqlite3_reset(create_astdb_stmt);
		return -1;
	}

	ast_db_commit_transaction();
	sqlite3_reset(create_astdb_stmt);

	return 0;
}

static int init_statements(void)
{
	/* Don't initialize create_astdb_statment here as the astdb table needs to exist
	 * brefore these statments can be initialized */
	return init_stmt(&put_stmt, put_stmt_sql, sizeof(put_stmt_sql));
}

static int db_open(const char *dbname)
{
	if (sqlite3_open(dbname, &astdb) != SQLITE_OK) {
		fprintf(stderr, "Unable to open Asterisk database '%s': %s\n", dbname, sqlite3_errmsg(astdb));
		sqlite3_close(astdb);
		return -1;
	}

	return 0;
}

static int sql_db_init(const char *dbname)
{
	if (db_open(dbname) || db_create_astdb() || init_statements()) {
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char *dbname;
	struct stat dont_care;

	if (argc != 2) {
		fprintf(stderr, "%s takes the path of astdb as its only argument\n", basename(argv[0]));
		exit(-1);
	}

	if (stat(argv[1], &dont_care)) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(-1);
	}

	if (!(dbname = alloca(strlen(argv[1]) + sizeof(".sqlite3")))) {
		exit(-1);
	}

	strcpy(dbname, argv[1]);
	strcat(dbname, ".sqlite3");

	if (!stat(dbname, &dont_care)) {
		fprintf(stderr, "%s already exists!\n", dbname);
		exit(-1);
	}

	if (sql_db_init(dbname)) {
		exit(-1);
	}

	if (convert_bdb_to_sqlite3(argv[1])) {
		fprintf(stderr, "Database conversion failed!\n");
		exit(-1);
		sqlite3_close(astdb);
	}

	sqlite3_close(astdb);
	return 0;
}
