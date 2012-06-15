/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Mark Spencer <twilson@digium.com>
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
 * \brief SQLite 3 astdb to Berkeley DB converter
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

static sqlite3 *sql3db;
static DB *bdb;

static int add_row_to_bdb(void *arg, int columns, char **values, char **column_names)
{
	DBT key = { 0, }, value = { 0, };

	if (columns != 2 || strcmp(column_names[0], "key") || strcmp(column_names[1], "value")) {
		fprintf(stderr, "Unknown row type\n");
		return SQLITE_ABORT;
	}

	key.data = values[0];
	key.size = strlen(values[0]) + 1;
	value.data = values[1];
	value.size = strlen(values[1]) + 1;

	if (bdb->put(bdb, &key, &value, 0)) {
		return SQLITE_ABORT;
	}

	bdb->sync(bdb, 0);

	return 0;
}

static int convert_bdb_to_sqlite3(void)
{
	char *errmsg = NULL;
	if (sqlite3_exec(sql3db, "SELECT key,value FROM astdb", add_row_to_bdb, NULL, &errmsg) != SQLITE_OK) {
		fprintf(stderr, "Could not add row to Berkeley DB: %s\n", errmsg);
		return -1;
	}

	return 0;
}

static int db_open_sqlite3(const char *dbname)
{
	if (sqlite3_open(dbname, &sql3db) != SQLITE_OK) {
		fprintf(stderr, "Unable to open Asterisk database '%s': %s\n", dbname, sqlite3_errmsg(sql3db));
		sqlite3_close(sql3db);
		return -1;
	}

	return 0;
}

static int create_bdb_astdb(void)
{
	if (!bdb && !(bdb = dbopen("astdb", O_CREAT | O_RDWR | O_TRUNC, AST_FILE_MODE, DB_BTREE, NULL))) {
		fprintf(stderr, "Unable to create astdb: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct stat dont_care;

	if (argc != 2) {
		fprintf(stderr, "%s takes the path of SQLite3 astdb as its only argument\n", basename(argv[0]));
		fprintf(stderr, "and will produce a file 'astdb' in the current directory\n"
				"Make a backup of any existing Berkeley DB astdb you have and copy\n"
				"the new astdb to its location: often /var/lib/asterisk/astdb\n");
		exit(-1);
	}

	if (stat(argv[1], &dont_care)) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(-1);
	}

	if (db_open_sqlite3(argv[1])) {
		exit(-1);
	}

	if (create_bdb_astdb()) {
		exit(-1);
	}

	if (convert_bdb_to_sqlite3()) {
		fprintf(stderr, "Database conversion failed!\n");
		exit(-1);
		sqlite3_close(sql3db);
	}

	printf("Created ./astdb. Back up any existing astdb and copy the created\n");
	printf("astdb file to the original's location. Often /var/lib/asterisk/astdb.\n");

	sqlite3_close(sql3db);
	return 0;
}
