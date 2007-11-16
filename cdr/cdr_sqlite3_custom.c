/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com> and others.
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
 * \brief Custom SQLite3 CDR records.
 *
 * \author Adapted by Alejandro Rios <alejandro.rios@avatar.com.co> and
 *  Russell Bryant <russell@digium.com> from 
 *  cdr_mysql_custom by Edward Eastman <ed@dm3.co.uk>,
 *	and cdr_sqlite by Holger Schurig <hs4233@mail.mn-solutions.de>
 *	
 *
 * \arg See also \ref AstCDR
 *
 *
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>sqlite3</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <errno.h>
#include <time.h>
#include <sqlite3.h>

#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/options.h"

AST_MUTEX_DEFINE_STATIC(lock);

static const char config_file[] = "cdr_sqlite3_custom.conf";

static char *desc = "Customizable SQLite3 CDR Backend";
static char *name = "cdr_sqlite3_custom";
static sqlite3 *db = NULL;

static char table[80];
static char columns[1024];
static char values[1024];

static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *mappingvar;
	const char *tmp;

	if (!(cfg = ast_config_load(config_file, config_flags))) {
		if (reload)
			ast_log(LOG_WARNING, "%s: Failed to reload configuration file.\n", name);
		else {
			ast_log(LOG_WARNING,
					"%s: Failed to load configuration file. Module not activated.\n",
					name);
		}
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	ast_mutex_lock(&lock);

	if (!(mappingvar = ast_variable_browse(cfg, "master"))) {
		/* nothing configured */
		ast_mutex_unlock(&lock);
		ast_config_destroy(cfg);
		return 0;
	}
	
	/* Mapping must have a table name */
	tmp = ast_variable_retrieve(cfg, "master", "table");
	if (!ast_strlen_zero(tmp))
		ast_copy_string(table, tmp, sizeof(table));
	else {
		ast_log(LOG_WARNING, "%s: Table name not specified.  Assuming cdr.\n", name);
		strcpy(table, "cdr");
	}

	tmp = ast_variable_retrieve(cfg, "master", "columns");
	if (!ast_strlen_zero(tmp))
		ast_copy_string(columns, tmp, sizeof(columns));
	else {
		ast_log(LOG_WARNING, "%s: Column names not specified. Module not loaded.\n",
				name);
		ast_mutex_unlock(&lock);
		ast_config_destroy(cfg);
		return -1;
	}

	tmp = ast_variable_retrieve(cfg, "master", "values");
	if (!ast_strlen_zero(tmp))
		ast_copy_string(values, tmp, sizeof(values));
	else {
		ast_log(LOG_WARNING, "%s: Values not specified. Module not loaded.\n", name);
		ast_mutex_unlock(&lock);
		ast_config_destroy(cfg);
		return -1;
	}

	ast_mutex_unlock(&lock);

	ast_config_destroy(cfg);

	return 0;
}

/* assumues 'to' buffer is at least strlen(from) * 2 + 1 bytes */
static int do_escape(char *to, const char *from)
{
	char *out = to;

	for (; *from; from++) {
		if (*from == '\'' || *from == '\\')
			*out++ = *from;
		*out++ = *from;
	}
	*out = '\0';

	return 0;
}

static int sqlite3_log(struct ast_cdr *cdr)
{
	int res = 0;
	char *zErr = 0;
	char *sql_cmd;
	struct ast_channel dummy = { 0, };
	int count;

	{ /* Make it obvious that only sql_cmd should be used outside of this block */
		char *sql_tmp_cmd;
		char sql_insert_cmd[2048];
		sql_tmp_cmd = sqlite3_mprintf("INSERT INTO %q (%q) VALUES (%q)", table, columns, values);
		dummy.cdr = cdr;
		pbx_substitute_variables_helper(&dummy, sql_tmp_cmd, sql_insert_cmd, sizeof(sql_insert_cmd) - 1);
		sqlite3_free(sql_tmp_cmd);
		sql_cmd = alloca(strlen(sql_insert_cmd) * 2 + 1);
		do_escape(sql_cmd, sql_insert_cmd);
	}

	ast_mutex_lock(&lock);

	for (count = 0; count < 5; count++) {
		res = sqlite3_exec(db, sql_cmd, NULL, NULL, &zErr);
		if (res != SQLITE_BUSY && res != SQLITE_LOCKED)
			break;
		usleep(200);
	}

	if (zErr) {
		ast_log(LOG_ERROR, "%s: %s. sentence: %s.\n", name, zErr, sql_cmd);
		sqlite3_free(zErr);
	}

	ast_mutex_unlock(&lock);

	return res;
}

static int unload_module(void)
{
	if (db)
		sqlite3_close(db);

	ast_cdr_unregister(name);

	return 0;
}

static int load_module(void)
{
	char *zErr;
	char fn[PATH_MAX];
	int res;
	char *sql_cmd;

	if (!load_config(0)) {
		res = ast_cdr_register(name, desc, sqlite3_log);
		if (res) {
			ast_log(LOG_ERROR, "%s: Unable to register custom SQLite3 CDR handling\n", name);
			return AST_MODULE_LOAD_DECLINE;
		}
	} else
		return AST_MODULE_LOAD_DECLINE;

	/* is the database there? */
	snprintf(fn, sizeof(fn), "%s/master.db", ast_config_AST_LOG_DIR);
	res = sqlite3_open(fn, &db);
	if (!db) {
		ast_log(LOG_ERROR, "%s: Could not open database %s.\n", name, fn);
		sqlite3_free(zErr);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* is the table there? */
	sql_cmd = sqlite3_mprintf("SELECT COUNT(AcctId) FROM %q;", table);
	res = sqlite3_exec(db, sql_cmd, NULL, NULL, NULL);
	sqlite3_free(sql_cmd);
	if (res) {
		sql_cmd = sqlite3_mprintf("CREATE TABLE %q (AcctId INTEGER PRIMARY KEY,%q)", table, columns);
		res = sqlite3_exec(db, sql_cmd, NULL, NULL, &zErr);
		sqlite3_free(sql_cmd);
		if (zErr) {
			ast_log(LOG_WARNING, "%s: %s.\n", name, zErr);
			sqlite3_free(zErr);
			return 0;
		}

		if (res) {
			ast_log(LOG_ERROR, "%s: Unable to create table '%s': %s.\n", name, table, zErr);
			sqlite3_free(zErr);
			if (db)
				sqlite3_close(db);
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	return 0;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "SQLite3 Custom CDR Module",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
