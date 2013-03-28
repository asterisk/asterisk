/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Proformatique
 *
 * Written by Richard Braun <rbraun@proformatique.com>
 *
 * Based on res_sqlite3 by Anthony Minessale II,
 * and res_config_mysql by Matthew Boehm
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

/*!
 * \page res_config_sqlite
 *
 * \section intro_sec Presentation
 *
 * res_config_sqlite is a module for the Asterisk Open Source PBX to
 * support SQLite 2 databases. It can be used to fetch configuration
 * from a database (static configuration files and/or using the Asterisk
 * RealTime Architecture - ARA).  It can also be used to log CDR entries. 
 * Note that Asterisk already comes with a module named cdr_sqlite.
 * There are two reasons for including it in res_config_sqlite:
 * the first is that rewriting it was a training to learn how to write a
 * simple module for Asterisk, the other is to have the same database open for
 * all kinds of operations, which improves reliability and performance.
 *
 * \section conf_sec Configuration
 *
 * The main configuration file is res_config_sqlite.conf. It must be readable or
 * res_config_sqlite will fail to start. It is suggested to use the sample file
 * in this package as a starting point. The file has only one section
 * named <code>general</code>. Here are the supported parameters :
 *
 * <dl>
 *	<dt><code>dbfile</code></dt>
 *	<dd>The absolute path to the SQLite database (the file can be non existent,
 *			res_config_sqlite will create it if it has the appropriate rights)</dd>
 *	<dt><code>config_table</code></dt>
 *	<dd>The table used for static configuration</dd>
 *	<dt><code>cdr_table</code></dt>
 *	<dd>The table used to store CDR entries (if ommitted, CDR support is
 *			disabled)</dd>
 * </dl>
 *
 * To use res_config_sqlite for static and/or RealTime configuration, refer to the
 * Asterisk documentation. The file tables.sql can be used to create the
 * needed tables.
 *
 * \section status_sec Driver status
 *
 * The CLI command <code>show sqlite status</code> returns status information
 * about the running driver.
 *
 * \section credits_sec Credits
 *
 * res_config_sqlite was developed by Richard Braun at the Proformatique company.
 */

/*!
 * \file
 * \brief res_config_sqlite module.
 */

/*** MODULEINFO
	<depend>sqlite</depend>
	<support_level>extended</support_level>
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sqlite.h>

#include "asterisk/logger.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/cdr.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"

#define MACRO_BEGIN	do {
#define MACRO_END	} while (0)

#define RES_CONFIG_SQLITE_NAME "res_config_sqlite"
#define RES_CONFIG_SQLITE_DRIVER "sqlite"
#define RES_CONFIG_SQLITE_DESCRIPTION "Resource Module for SQLite 2"
#define RES_CONFIG_SQLITE_CONF_FILE "res_config_sqlite.conf"

enum {
	RES_CONFIG_SQLITE_CONFIG_ID,
	RES_CONFIG_SQLITE_CONFIG_CAT_METRIC,
	RES_CONFIG_SQLITE_CONFIG_VAR_METRIC,
	RES_CONFIG_SQLITE_CONFIG_COMMENTED,
	RES_CONFIG_SQLITE_CONFIG_FILENAME,
	RES_CONFIG_SQLITE_CONFIG_CATEGORY,
	RES_CONFIG_SQLITE_CONFIG_VAR_NAME,
	RES_CONFIG_SQLITE_CONFIG_VAR_VAL,
	RES_CONFIG_SQLITE_CONFIG_COLUMNS,
};

#define SET_VAR(config, to, from)			\
MACRO_BEGIN						\
	int __error;					\
							\
	__error = set_var(&to, #to, from->value);	\
							\
	if (__error) {					\
		ast_config_destroy(config);		\
		unload_config();			\
		return 1;				\
	}						\
MACRO_END

AST_THREADSTORAGE(sql_buf);
AST_THREADSTORAGE(where_buf);

/*!
 * Maximum number of loops before giving up executing a query. Calls to
 * sqlite_xxx() functions which can return SQLITE_BUSY
 * are enclosed by RES_CONFIG_SQLITE_BEGIN and RES_CONFIG_SQLITE_END, e.g.
 * <pre>
 * char *errormsg;
 * int error;
 *
 * RES_CONFIG_SQLITE_BEGIN
 *	 error = sqlite_exec(db, query, NULL, NULL, &errormsg);
 * RES_CONFIG_SQLITE_END(error)
 *
 * if (error)
 *	 ...;
 * </pre>
 */
#define RES_CONFIG_SQLITE_MAX_LOOPS 10

/*!
 * Macro used before executing a query.
 *
 * \see RES_CONFIG_SQLITE_MAX_LOOPS.
 */
#define RES_CONFIG_SQLITE_BEGIN						\
MACRO_BEGIN								\
	int __i;							\
									\
	for (__i = 0; __i < RES_CONFIG_SQLITE_MAX_LOOPS; __i++)	{

/*!
 * Macro used after executing a query.
 *
 * \see RES_CONFIG_SQLITE_MAX_LOOPS.
 */
#define RES_CONFIG_SQLITE_END(error)					\
		if (error != SQLITE_BUSY)	\
			break;						\
		usleep(1000);						\
	}								\
MACRO_END;

/*!
 * Structure sent to the SQLite callback function for static configuration.
 *
 * \see add_cfg_entry()
 */
struct cfg_entry_args {
	struct ast_config *cfg;
	struct ast_category *cat;
	char *cat_name;
	struct ast_flags flags;
	const char *who_asked;
};

/*!
 * Structure sent to the SQLite callback function for RealTime configuration.
 *
 * \see add_rt_cfg_entry()
 */
struct rt_cfg_entry_args {
	struct ast_variable *var;
	struct ast_variable *last;
};

/*!
 * Structure sent to the SQLite callback function for RealTime configuration
 * (realtime_multi_handler()).
 *
 * \see add_rt_multi_cfg_entry()
 */
struct rt_multi_cfg_entry_args {
	struct ast_config *cfg;
	char *initfield;
};

/*!
 * \brief Allocate a variable.
 * \param var the address of the variable to set (it will be allocated)
 * \param name the name of the variable (for error handling)
 * \param value the value to store in var
 * \retval 0 on success
 * \retval 1 if an allocation error occurred
 */
static int set_var(char **var, const char *name, const char *value);

/*!
 * \brief Load the configuration file.
 * \see unload_config()
 *
 * This function sets dbfile, config_table, and cdr_table. It calls
 * check_vars() before returning, and unload_config() if an error occurred.
 *
 * \retval 0 on success
 * \retval 1 if an error occurred
 */
static int load_config(void);

/*!
 * \brief Free resources related to configuration.
 * \see load_config()
 */
static void unload_config(void);

/*!
 * \brief Asterisk callback function for CDR support.
 * \param cdr the CDR entry Asterisk sends us.
 *
 * Asterisk will call this function each time a CDR entry must be logged if
 * CDR support is enabled.
 *
 * \retval 0 on success
 * \retval 1 if an error occurred
 */
static int cdr_handler(struct ast_cdr *cdr);

/*!
 * \brief SQLite callback function for static configuration.
 *
 * This function is passed to the SQLite engine as a callback function to
 * parse a row and store it in a struct ast_config object. It relies on
 * resulting rows being sorted by category.
 *
 * \param arg a pointer to a struct cfg_entry_args object
 * \param argc number of columns
 * \param argv values in the row
 * \param columnNames names and types of the columns
 * \retval 0 on success
 * \retval 1 if an error occurred
 * \see cfg_entry_args
 * \see sql_get_config_table
 * \see config_handler()
 */
static int add_cfg_entry(void *arg, int argc, char **argv, char **columnNames);

/*!
 * \brief Asterisk callback function for static configuration.
 *
 * Asterisk will call this function when it loads its static configuration,
 * which usually happens at startup and reload.
 *
 * \param database the database to use (ignored)
 * \param table the table to use
 * \param file the file to load from the database
 * \param cfg the struct ast_config object to use when storing variables
 * \param flags Optional flags.  Not used.
 * \param suggested_incl suggest include.
 * \param who_asked
 * \retval cfg object
 * \retval NULL if an error occurred
 * \see add_cfg_entry()
 */
static struct ast_config * config_handler(const char *database, const char *table, const char *file,
	struct ast_config *cfg, struct ast_flags flags, const char *suggested_incl, const char *who_asked);

/*!
 * \brief Helper function to parse a va_list object into 2 dynamic arrays of
 * strings, parameters and values.
 *
 * ap must have the following format : param1 val1 param2 val2 param3 val3 ...
 * arguments will be extracted to create 2 arrays:
 *
 * <ul>
 *	<li>params : param1 param2 param3 ...</li>
 *	<li>vals : val1 val2 val3 ...</li>
 * </ul>
 *
 * The address of these arrays are stored in params_ptr and vals_ptr. It
 * is the responsibility of the caller to release the memory of these arrays.
 * It is considered an error that va_list has a null or odd number of strings.
 *
 * \param ap the va_list object to parse
 * \param params_ptr where the address of the params array is stored
 * \param vals_ptr where the address of the vals array is stored
 * \param warn
 * \retval the number of elements in the arrays (which have the same size).
 * \retval 0 if an error occurred.
 */
static size_t get_params(va_list ap, const char ***params_ptr,
	const char ***vals_ptr, int warn);

/*!
 * \brief SQLite callback function for RealTime configuration.
 *
 * This function is passed to the SQLite engine as a callback function to
 * parse a row and store it in a linked list of struct ast_variable objects.
 *
 * \param arg a pointer to a struct rt_cfg_entry_args object
 * \param argc number of columns
 * \param argv values in the row
 * \param columnNames names and types of the columns
 * \retval 0 on success.
 * \retval 1 if an error occurred.
 * \see rt_cfg_entry_args
 * \see realtime_handler()
 */
static int add_rt_cfg_entry(void *arg, int argc, char **argv,
	char **columnNames);

/*!
 * \brief Asterisk callback function for RealTime configuration.
 *
 * Asterisk will call this function each time it requires a variable
 * through the RealTime architecture. ap is a list of parameters and
 * values used to find a specific row, e.g one parameter "name" and
 * one value "123" so that the SQL query becomes <code>SELECT * FROM
 * table WHERE name = '123';</code>.
 *
 * \param database the database to use (ignored)
 * \param table the table to use
 * \param ap list of parameters and values to match
 *
 * \retval a linked list of struct ast_variable objects
 * \retval NULL if an error occurred
 * \see add_rt_cfg_entry()
 */
static struct ast_variable * realtime_handler(const char *database,
	const char *table, va_list ap);

/*!
 * \brief SQLite callback function for RealTime configuration.
 *
 * This function performs the same actions as add_rt_cfg_entry() except
 * that the rt_multi_cfg_entry_args structure is designed to store
 * categories in addition to variables.
 *
 * \param arg a pointer to a struct rt_multi_cfg_entry_args object
 * \param argc number of columns
 * \param argv values in the row
 * \param columnNames names and types of the columns
 * \retval 0 on success.
 * \retval 1 if an error occurred.
 * \see rt_multi_cfg_entry_args
 * \see realtime_multi_handler()
 */
static int add_rt_multi_cfg_entry(void *arg, int argc, char **argv,
	char **columnNames);

/*!
 * \brief Asterisk callback function for RealTime configuration.
 *
 * This function performs the same actions as realtime_handler() except
 * that it can store variables per category, and can return several
 * categories.
 *
 * \param database the database to use (ignored)
 * \param table the table to use
 * \param ap list of parameters and values to match
 * \retval a struct ast_config object storing categories and variables.
 * \retval NULL if an error occurred.
 *
 * \see add_rt_multi_cfg_entry()
 */
static struct ast_config * realtime_multi_handler(const char *database,
	const char *table, va_list ap);

/*!
 * \brief Asterisk callback function for RealTime configuration (variable
 * update).
 *
 * Asterisk will call this function each time a variable has been modified
 * internally and must be updated in the backend engine. keyfield and entity
 * are used to find the row to update, e.g. <code>UPDATE table SET ... WHERE
 * keyfield = 'entity';</code>. ap is a list of parameters and values with the
 * same format as the other realtime functions.
 *
 * \param database the database to use (ignored)
 * \param table the table to use
 * \param keyfield the column of the matching cell
 * \param entity the value of the matching cell
 * \param ap list of parameters and new values to update in the database
 * \retval the number of affected rows.
 * \retval -1 if an error occurred.
 */
static int realtime_update_handler(const char *database, const char *table,
	const char *keyfield, const char *entity, va_list ap);
static int realtime_update2_handler(const char *database, const char *table,
	va_list ap);

/*!
 * \brief Asterisk callback function for RealTime configuration (variable
 * create/store).
 *
 * Asterisk will call this function each time a variable has been created
 * internally and must be stored in the backend engine.
 * are used to find the row to update, e.g. ap is a list of parameters and
 * values with the same format as the other realtime functions.
 *
 * \param database the database to use (ignored)
 * \param table the table to use
 * \param ap list of parameters and new values to insert into the database
 * \retval the rowid of inserted row.
 * \retval -1 if an error occurred.
 */
static int realtime_store_handler(const char *database, const char *table,
	va_list ap);

/*!
 * \brief Asterisk callback function for RealTime configuration (destroys
 * variable).
 *
 * Asterisk will call this function each time a variable has been destroyed
 * internally and must be removed from the backend engine. keyfield and entity
 * are used to find the row to delete, e.g. <code>DELETE FROM table WHERE
 * keyfield = 'entity';</code>. ap is a list of parameters and values with the
 * same format as the other realtime functions.
 *
 * \param database the database to use (ignored)
 * \param table the table to use
 * \param keyfield the column of the matching cell
 * \param entity the value of the matching cell
 * \param ap list of additional parameters for cell matching
 * \retval the number of affected rows.
 * \retval -1 if an error occurred.
 */
static int realtime_destroy_handler(const char *database, const char *table,
	const char *keyfield, const char *entity, va_list ap);

/*!
 * \brief Asterisk callback function for the CLI status command.
 *
 * \param e CLI command
 * \param cmd 
 * \param a CLI argument list
 * \return RESULT_SUCCESS
 */
static char *handle_cli_show_sqlite_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_sqlite_show_tables(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static int realtime_require_handler(const char *database, const char *table, va_list ap);
static int realtime_unload_handler(const char *unused, const char *tablename);

/*! The SQLite database object. */
static sqlite *db;

/*! Set to 1 if CDR support is enabled. */
static int use_cdr;

/*! Set to 1 if the CDR callback function was registered. */
static int cdr_registered;

/*! Set to 1 if the CLI status command callback function was registered. */
static int cli_status_registered;

/*! The path of the database file. */
static char *dbfile;

/*! The name of the static configuration table. */
static char *config_table;

/*! The name of the table used to store CDR entries. */
static char *cdr_table;

/*!
 * The structure specifying all callback functions used by Asterisk for static
 * and RealTime configuration.
 */
static struct ast_config_engine sqlite_engine =
{
	.name = RES_CONFIG_SQLITE_DRIVER,
	.load_func = config_handler,
	.realtime_func = realtime_handler,
	.realtime_multi_func = realtime_multi_handler,
	.store_func = realtime_store_handler,
	.destroy_func = realtime_destroy_handler,
	.update_func = realtime_update_handler,
	.update2_func = realtime_update2_handler,
	.require_func = realtime_require_handler,
	.unload_func = realtime_unload_handler,
};

/*!
 * The mutex used to prevent simultaneous access to the SQLite database.
 */
AST_MUTEX_DEFINE_STATIC(mutex);

/*!
 * Structure containing details and callback functions for the CLI status
 * command.
 */
static struct ast_cli_entry cli_status[] = {
	AST_CLI_DEFINE(handle_cli_show_sqlite_status, "Show status information about the SQLite 2 driver"),
	AST_CLI_DEFINE(handle_cli_sqlite_show_tables, "Cached table information about the SQLite 2 driver"),
};

struct sqlite_cache_columns {
	char *name;
	char *type;
	unsigned char isint;    /*!< By definition, only INTEGER PRIMARY KEY is an integer; everything else is a string. */
	AST_RWLIST_ENTRY(sqlite_cache_columns) list;
};

struct sqlite_cache_tables {
	char *name;
	AST_RWLIST_HEAD(_columns, sqlite_cache_columns) columns;
	AST_RWLIST_ENTRY(sqlite_cache_tables) list;
};

static AST_RWLIST_HEAD_STATIC(sqlite_tables, sqlite_cache_tables);

/*
 * Taken from Asterisk 1.2 cdr_sqlite.so.
 */

/*! SQL query format to create the CDR table if non existent. */
static char *sql_create_cdr_table =
"CREATE TABLE '%q' (\n"
"	id		INTEGER,\n"
"	clid		VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	src		VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	dst		VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	dcontext	VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	channel		VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	dstchannel	VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	lastapp		VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	lastdata	VARCHAR(80)	NOT NULL	DEFAULT '',\n"
"	start		DATETIME	NOT NULL	DEFAULT '0000-00-00 00:00:00',\n"
"	answer		DATETIME	NOT NULL	DEFAULT '0000-00-00 00:00:00',\n"
"	end		DATETIME	NOT NULL	DEFAULT '0000-00-00 00:00:00',\n"
"	duration	INT(11)		NOT NULL	DEFAULT 0,\n"
"	billsec		INT(11)		NOT NULL	DEFAULT 0,\n"
"	disposition	VARCHAR(45)	NOT NULL	DEFAULT '',\n"
"	amaflags	INT(11)		NOT NULL	DEFAULT 0,\n"
"	accountcode	VARCHAR(20)	NOT NULL	DEFAULT '',\n"
"	uniqueid	VARCHAR(32)	NOT NULL	DEFAULT '',\n"
"	userfield	VARCHAR(255)	NOT NULL	DEFAULT '',\n"
"	PRIMARY KEY	(id)\n"
");";

/*!
 * SQL query format to describe the table structure
 */
#define sql_table_structure "SELECT sql FROM sqlite_master WHERE type='table' AND tbl_name='%s'"

/*!
 * SQL query format to fetch the static configuration of a file.
 * Rows must be sorted by category.
 *
 * \see add_cfg_entry()
 */
#define sql_get_config_table \
	"SELECT *" \
	"	FROM '%q'" \
	"	WHERE filename = '%q' AND commented = 0" \
	"	ORDER BY cat_metric ASC, var_metric ASC;"

static void free_table(struct sqlite_cache_tables *tblptr)
{
	struct sqlite_cache_columns *col;

	/* Obtain a write lock to ensure there are no read locks outstanding */
	AST_RWLIST_WRLOCK(&(tblptr->columns));
	while ((col = AST_RWLIST_REMOVE_HEAD(&(tblptr->columns), list))) {
		ast_free(col);
	}
	AST_RWLIST_UNLOCK(&(tblptr->columns));
	AST_RWLIST_HEAD_DESTROY(&(tblptr->columns));
	ast_free(tblptr);
}

static int find_table_cb(void *vtblptr, int argc, char **argv, char **columnNames)
{
	struct sqlite_cache_tables *tblptr = vtblptr;
	char *sql = ast_strdupa(argv[0]), *start, *end, *type, *remainder;
	int i;
	AST_DECLARE_APP_ARGS(fie,
		AST_APP_ARG(ld)[100]; /* This means we support up to 100 columns per table */
	);
	struct sqlite_cache_columns *col;

	/* This is really fun.  We get to parse an SQL statement to figure out
	 * what columns are in the table.
	 */
	if ((start = strchr(sql, '(')) && (end = strrchr(sql, ')'))) {
		start++;
		*end = '\0';
	} else {
		/* Abort */
		return -1;
	}

	AST_STANDARD_APP_ARGS(fie, start);
	for (i = 0; i < fie.argc; i++) {
		fie.ld[i] = ast_skip_blanks(fie.ld[i]);
		ast_debug(5, "Found field: %s\n", fie.ld[i]);
		if (strncasecmp(fie.ld[i], "PRIMARY KEY", 11) == 0 && (start = strchr(fie.ld[i], '(')) && (end = strchr(fie.ld[i], ')'))) {
			*end = '\0';
			AST_RWLIST_TRAVERSE(&(tblptr->columns), col, list) {
				if (strcasecmp(start + 1, col->name) == 0 && strcasestr(col->type, "INTEGER")) {
					col->isint = 1;
				}
			}
			continue;
		}
		/* type delimiter could be any space character */
		for (type = fie.ld[i]; *type > 32; type++);
		*type++ = '\0';
		type = ast_skip_blanks(type);
		for (remainder = type; *remainder > 32; remainder++);
		*remainder = '\0';
		if (!(col = ast_calloc(1, sizeof(*col) + strlen(fie.ld[i]) + strlen(type) + 2))) {
			return -1;
		}
		col->name = (char *)col + sizeof(*col);
		col->type = (char *)col + sizeof(*col) + strlen(fie.ld[i]) + 1;
		strcpy(col->name, fie.ld[i]); /* SAFE */
		strcpy(col->type, type); /* SAFE */
		if (strcasestr(col->type, "INTEGER") && strcasestr(col->type, "PRIMARY KEY")) {
			col->isint = 1;
		}
		AST_LIST_INSERT_TAIL(&(tblptr->columns), col, list);
	}
	return 0;
}

static struct sqlite_cache_tables *find_table(const char *tablename)
{
	struct sqlite_cache_tables *tblptr;
	int i, err;
	char *sql, *errstr = NULL;

	AST_RWLIST_RDLOCK(&sqlite_tables);

	for (i = 0; i < 2; i++) {
		AST_RWLIST_TRAVERSE(&sqlite_tables, tblptr, list) {
			if (strcmp(tblptr->name, tablename) == 0) {
				break;
			}
		}
		if (tblptr) {
			AST_RWLIST_RDLOCK(&(tblptr->columns));
			AST_RWLIST_UNLOCK(&sqlite_tables);
			return tblptr;
		}

		if (i == 0) {
			AST_RWLIST_UNLOCK(&sqlite_tables);
			AST_RWLIST_WRLOCK(&sqlite_tables);
		}
	}

	/* Table structure not cached; build the structure now */
	if (ast_asprintf(&sql, sql_table_structure, tablename) < 0) {
		sql = NULL;
	}
	if (!(tblptr = ast_calloc(1, sizeof(*tblptr) + strlen(tablename) + 1))) {
		AST_RWLIST_UNLOCK(&sqlite_tables);
		ast_log(LOG_ERROR, "Memory error.  Cannot cache table '%s'\n", tablename);
		ast_free(sql);
		return NULL;
	}
	tblptr->name = (char *)tblptr + sizeof(*tblptr);
	strcpy(tblptr->name, tablename); /* SAFE */
	AST_RWLIST_HEAD_INIT(&(tblptr->columns));

	ast_debug(1, "About to query table structure: %s\n", sql);

	ast_mutex_lock(&mutex);
	if ((err = sqlite_exec(db, sql, find_table_cb, tblptr, &errstr))) {
		ast_mutex_unlock(&mutex);
		ast_log(LOG_WARNING, "SQLite error %d: %s\n", err, errstr);
		ast_free(errstr);
		free_table(tblptr);
		AST_RWLIST_UNLOCK(&sqlite_tables);
		ast_free(sql);
		return NULL;
	}
	ast_mutex_unlock(&mutex);
	ast_free(sql);

	if (AST_LIST_EMPTY(&(tblptr->columns))) {
		free_table(tblptr);
		AST_RWLIST_UNLOCK(&sqlite_tables);
		return NULL;
	}

	AST_RWLIST_INSERT_TAIL(&sqlite_tables, tblptr, list);
	AST_RWLIST_RDLOCK(&(tblptr->columns));
	AST_RWLIST_UNLOCK(&sqlite_tables);
	return tblptr;
}

#define release_table(a)	AST_RWLIST_UNLOCK(&((a)->columns))

static int set_var(char **var, const char *name, const char *value)
{
	if (*var)
		ast_free(*var);

	*var = ast_strdup(value);

	if (!*var) {
		ast_log(LOG_WARNING, "Unable to allocate variable %s\n", name);
		return 1;
	}

	return 0;
}

static int check_vars(void)
{
	if (!dbfile) {
		ast_log(LOG_ERROR, "Required parameter undefined: dbfile\n");
		return 1;
	}

	use_cdr = (cdr_table != NULL);

	return 0;
}

static int load_config(void)
{
	struct ast_config *config;
	struct ast_variable *var;
	int error;
	struct ast_flags config_flags = { 0 };

	config = ast_config_load(RES_CONFIG_SQLITE_CONF_FILE, config_flags);

	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load " RES_CONFIG_SQLITE_CONF_FILE "\n");
		return 1;
	}

	for (var = ast_variable_browse(config, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "dbfile"))
			SET_VAR(config, dbfile, var);
		else if (!strcasecmp(var->name, "config_table"))
			SET_VAR(config, config_table, var);
		else if (!strcasecmp(var->name, "cdr_table")) {
			SET_VAR(config, cdr_table, var);
		} else
			ast_log(LOG_WARNING, "Unknown parameter : %s\n", var->name);
	}

	ast_config_destroy(config);
	error = check_vars();

	if (error) {
		unload_config();
		return 1;
	}

	return 0;
}

static void unload_config(void)
{
	struct sqlite_cache_tables *tbl;
	ast_free(dbfile);
	dbfile = NULL;
	ast_free(config_table);
	config_table = NULL;
	ast_free(cdr_table);
	cdr_table = NULL;
	AST_RWLIST_WRLOCK(&sqlite_tables);
	while ((tbl = AST_RWLIST_REMOVE_HEAD(&sqlite_tables, list))) {
		free_table(tbl);
	}
	AST_RWLIST_UNLOCK(&sqlite_tables);
}

static int cdr_handler(struct ast_cdr *cdr)
{
	char *errormsg = NULL, *tmp, workspace[500];
	int error, scannum;
	struct sqlite_cache_tables *tbl = find_table(cdr_table);
	struct sqlite_cache_columns *col;
	struct ast_str *sql1 = ast_str_create(160), *sql2 = ast_str_create(16);
	int first = 1;

	if (!tbl) {
		ast_log(LOG_WARNING, "No such table: %s\n", cdr_table);
		return -1;
	}

	ast_str_set(&sql1, 0, "INSERT INTO %s (", cdr_table);
	ast_str_set(&sql2, 0, ") VALUES (");

	AST_RWLIST_TRAVERSE(&(tbl->columns), col, list) {
		if (col->isint) {
			ast_cdr_getvar(cdr, col->name, &tmp, workspace, sizeof(workspace), 0, 1);
			if (!tmp) {
				continue;
			}
			if (sscanf(tmp, "%30d", &scannum) == 1) {
				ast_str_append(&sql1, 0, "%s%s", first ? "" : ",", col->name);
				ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", scannum);
			}
		} else {
			ast_cdr_getvar(cdr, col->name, &tmp, workspace, sizeof(workspace), 0, 0);
			if (!tmp) {
				continue;
			}
			ast_str_append(&sql1, 0, "%s%s", first ? "" : ",", col->name);
			tmp = sqlite_mprintf("%Q", tmp);
			ast_str_append(&sql2, 0, "%s%s", first ? "" : ",", tmp);
			sqlite_freemem(tmp);
		}
		first = 0;
	}
	release_table(tbl);

	ast_str_append(&sql1, 0, "%s)", ast_str_buffer(sql2));
	ast_free(sql2);

	ast_debug(1, "SQL query: %s\n", ast_str_buffer(sql1));

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, ast_str_buffer(sql1), NULL, NULL, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	ast_free(sql1);

	if (error) {
		ast_log(LOG_ERROR, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
		sqlite_freemem(errormsg);
		return 1;
	}
	sqlite_freemem(errormsg);

	return 0;
}

static int add_cfg_entry(void *arg, int argc, char **argv, char **columnNames)
{
	struct cfg_entry_args *args;
	struct ast_variable *var;

	if (argc != RES_CONFIG_SQLITE_CONFIG_COLUMNS) {
		ast_log(LOG_WARNING, "Corrupt table\n");
		return 1;
	}

	args = arg;

	if (!strcmp(argv[RES_CONFIG_SQLITE_CONFIG_VAR_NAME], "#include")) {
		struct ast_config *cfg;
		char *val;

		val = argv[RES_CONFIG_SQLITE_CONFIG_VAR_VAL];
		cfg = ast_config_internal_load(val, args->cfg, args->flags, "", args->who_asked);

		if (!cfg) {
			ast_log(LOG_WARNING, "Unable to include %s\n", val);
			return 1;
		} else {
			args->cfg = cfg;
			return 0;
		}
	}

	if (!args->cat_name || strcmp(args->cat_name, argv[RES_CONFIG_SQLITE_CONFIG_CATEGORY])) {
		args->cat = ast_category_new(argv[RES_CONFIG_SQLITE_CONFIG_CATEGORY], "", 99999);

		if (!args->cat) {
			ast_log(LOG_WARNING, "Unable to allocate category\n");
			return 1;
		}

		ast_free(args->cat_name);
		args->cat_name = ast_strdup(argv[RES_CONFIG_SQLITE_CONFIG_CATEGORY]);

		if (!args->cat_name) {
			ast_category_destroy(args->cat);
			return 1;
		}

		ast_category_append(args->cfg, args->cat);
	}

	var = ast_variable_new(argv[RES_CONFIG_SQLITE_CONFIG_VAR_NAME], argv[RES_CONFIG_SQLITE_CONFIG_VAR_VAL], "");

	if (!var) {
		ast_log(LOG_WARNING, "Unable to allocate variable\n");
		return 1;
	}

	ast_variable_append(args->cat, var);

	return 0;
}

static struct ast_config *config_handler(const char *database,	const char *table, const char *file,
	struct ast_config *cfg, struct ast_flags flags, const char *suggested_incl, const char *who_asked)
{
	struct cfg_entry_args args;
	char *query, *errormsg = NULL;
	int error;

	if (!config_table) {
		if (!table) {
			ast_log(LOG_ERROR, "Table name unspecified\n");
			return NULL;
		}
	} else
		table = config_table;

	query = sqlite_mprintf(sql_get_config_table, table, file);

	if (!query) {
		ast_log(LOG_WARNING, "Unable to allocate SQL query\n");
		return NULL;
	}

	ast_debug(1, "SQL query: %s\n", query);
	args.cfg = cfg;
	args.cat = NULL;
	args.cat_name = NULL;
	args.flags = flags;
	args.who_asked = who_asked;

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, query, add_cfg_entry, &args, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	ast_free(args.cat_name);
	sqlite_freemem(query);

	if (error) {
		ast_log(LOG_ERROR, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
		sqlite_freemem(errormsg);
		return NULL;
	}
	sqlite_freemem(errormsg);

	return cfg;
}

static size_t get_params(va_list ap, const char ***params_ptr, const char ***vals_ptr, int warn)
{
	const char **tmp, *param, *val, **params, **vals;
	size_t params_count;

	params = NULL;
	vals = NULL;
	params_count = 0;

	while ((param = va_arg(ap, const char *)) && (val = va_arg(ap, const char *))) {
		if (!(tmp = ast_realloc(params, (params_count + 1) * sizeof(char *)))) {
			ast_free(params);
			ast_free(vals);
			return 0;
		}
		params = tmp;

		if (!(tmp = ast_realloc(vals, (params_count + 1) * sizeof(char *)))) {
			ast_free(params);
			ast_free(vals);
			return 0;
		}
		vals = tmp;

		params[params_count] = param;
		vals[params_count] = val;
		params_count++;
	}

	if (params_count > 0) {
		*params_ptr = params;
		*vals_ptr = vals;
	} else if (warn) {
		ast_log(LOG_WARNING, "1 parameter and 1 value at least required\n");
	}

	return params_count;
}

static int add_rt_cfg_entry(void *arg, int argc, char **argv, char **columnNames)
{
	struct rt_cfg_entry_args *args;
	struct ast_variable *var;
	int i;

	args = arg;

	for (i = 0; i < argc; i++) {
		if (!argv[i])
			continue;

		if (!(var = ast_variable_new(columnNames[i], argv[i], "")))
			return 1;

		if (!args->var)
			args->var = var;

		if (!args->last)
			args->last = var;
		else {
			args->last->next = var;
			args->last = var;
		}
	}

	return 0;
}

static struct ast_variable * realtime_handler(const char *database, const char *table, va_list ap)
{
	char *query, *errormsg = NULL, *op, *tmp_str;
	struct rt_cfg_entry_args args;
	const char **params, **vals;
	size_t params_count;
	int error;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return NULL;
	}

	params_count = get_params(ap, &params, &vals, 1);

	if (params_count == 0)
		return NULL;

	op = (strchr(params[0], ' ') == NULL) ? " =" : "";

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "SELECT * FROM '%q' WHERE%s %q%s '%q'"
/* \endcond */

	query = sqlite_mprintf(QUERY, table, (config_table && !strcmp(config_table, table)) ? " commented = 0 AND" : "", params[0], op, vals[0]);

	if (!query) {
		ast_log(LOG_WARNING, "Unable to allocate SQL query\n");
		ast_free(params);
		ast_free(vals);
		return NULL;
	}

	if (params_count > 1) {
		size_t i;

		for (i = 1; i < params_count; i++) {
			op = (strchr(params[i], ' ') == NULL) ? " =" : "";
			tmp_str = sqlite_mprintf("%s AND %q%s '%q'", query, params[i], op, vals[i]);
			sqlite_freemem(query);

			if (!tmp_str) {
				ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
				ast_free(params);
				ast_free(vals);
				return NULL;
			}

			query = tmp_str;
		}
	}

	ast_free(params);
	ast_free(vals);

	tmp_str = sqlite_mprintf("%s LIMIT 1;", query);
	sqlite_freemem(query);

	if (!tmp_str) {
		ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
		return NULL;
	}

	query = tmp_str;
	ast_debug(1, "SQL query: %s\n", query);
	args.var = NULL;
	args.last = NULL;

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, query, add_rt_cfg_entry, &args, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
		sqlite_freemem(errormsg);
		ast_variables_destroy(args.var);
		return NULL;
	}
	sqlite_freemem(errormsg);

	return args.var;
}

static int add_rt_multi_cfg_entry(void *arg, int argc, char **argv, char **columnNames)
{
	struct rt_multi_cfg_entry_args *args;
	struct ast_category *cat;
	struct ast_variable *var;
	char *cat_name;
	size_t i;

	args = arg;
	cat_name = NULL;

	/*
	 * cat_name should always be set here, since initfield is forged from
	 * params[0] in realtime_multi_handler(), which is a search parameter
	 * of the SQL query.
	 */
	for (i = 0; i < argc; i++) {
		if (!strcmp(args->initfield, columnNames[i]))
			cat_name = argv[i];
	}

	if (!cat_name) {
		ast_log(LOG_ERROR, "Bogus SQL results, cat_name is NULL !\n");
		return 1;
	}

	if (!(cat = ast_category_new(cat_name, "", 99999))) {
		ast_log(LOG_WARNING, "Unable to allocate category\n");
		return 1;
	}

	ast_category_append(args->cfg, cat);

	for (i = 0; i < argc; i++) {
		if (!argv[i]) {
			continue;
		}

		if (!(var = ast_variable_new(columnNames[i], argv[i], ""))) {
			ast_log(LOG_WARNING, "Unable to allocate variable\n");
			return 1;
		}

		ast_variable_append(cat, var);
	}

	return 0;
}

static struct ast_config *realtime_multi_handler(const char *database,
	const char *table, va_list ap)
{
	char *query, *errormsg = NULL, *op, *tmp_str, *initfield;
	struct rt_multi_cfg_entry_args args;
	const char **params, **vals;
	struct ast_config *cfg;
	size_t params_count;
	int error;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return NULL;
	}

	if (!(cfg = ast_config_new())) {
		ast_log(LOG_WARNING, "Unable to allocate configuration structure\n");
		return NULL;
	}

	if (!(params_count = get_params(ap, &params, &vals, 1))) {
		ast_config_destroy(cfg);
		return NULL;
	}

	if (!(initfield = ast_strdup(params[0]))) {
		ast_config_destroy(cfg);
		ast_free(params);
		ast_free(vals);
		return NULL;
	}

	tmp_str = strchr(initfield, ' ');

	if (tmp_str)
		*tmp_str = '\0';

	op = (!strchr(params[0], ' ')) ? " =" : "";

	/*
	 * Asterisk sends us an already escaped string when searching for
	 * "exten LIKE" (uh!). Handle it separately.
	 */
	tmp_str = (!strcmp(vals[0], "\\_%")) ? "_%" : (char *)vals[0];

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "SELECT * FROM '%q' WHERE%s %q%s '%q'"
/* \endcond */

	if (!(query = sqlite_mprintf(QUERY, table, (config_table && !strcmp(config_table, table)) ? " commented = 0 AND" : "", params[0], op, tmp_str))) {
		ast_log(LOG_WARNING, "Unable to allocate SQL query\n");
		ast_config_destroy(cfg);
		ast_free(params);
		ast_free(vals);
		ast_free(initfield);
		return NULL;
	}

	if (params_count > 1) {
		size_t i;

		for (i = 1; i < params_count; i++) {
			op = (!strchr(params[i], ' ')) ? " =" : "";
			tmp_str = sqlite_mprintf("%s AND %q%s '%q'", query, params[i], op, vals[i]);
			sqlite_freemem(query);

			if (!tmp_str) {
				ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
				ast_config_destroy(cfg);
				ast_free(params);
				ast_free(vals);
				ast_free(initfield);
				return NULL;
			}

			query = tmp_str;
		}
	}

	ast_free(params);
	ast_free(vals);

	if (!(tmp_str = sqlite_mprintf("%s ORDER BY %q;", query, initfield))) {
		ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
		sqlite_freemem(query);
		ast_config_destroy(cfg);
		ast_free(initfield);
		return NULL;
	}

	sqlite_freemem(query);
	query = tmp_str;
	ast_debug(1, "SQL query: %s\n", query);
	args.cfg = cfg;
	args.initfield = initfield;

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, query, add_rt_multi_cfg_entry, &args, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);
	ast_free(initfield);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
		sqlite_freemem(errormsg);
		ast_config_destroy(cfg);
		return NULL;
	}
	sqlite_freemem(errormsg);

	return cfg;
}

static int realtime_update_handler(const char *database, const char *table,
	const char *keyfield, const char *entity, va_list ap)
{
	char *query, *errormsg = NULL, *tmp_str;
	const char **params, **vals;
	size_t params_count;
	int error, rows_num;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return -1;
	}

	if (!(params_count = get_params(ap, &params, &vals, 1)))
		return -1;

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "UPDATE '%q' SET %q = '%q'"
/* \endcond */

	if (!(query = sqlite_mprintf(QUERY, table, params[0], vals[0]))) {
		ast_log(LOG_WARNING, "Unable to allocate SQL query\n");
		ast_free(params);
		ast_free(vals);
		return -1;
	}

	if (params_count > 1) {
		size_t i;

		for (i = 1; i < params_count; i++) {
			tmp_str = sqlite_mprintf("%s, %q = '%q'", query, params[i], vals[i]);
			sqlite_freemem(query);

			if (!tmp_str) {
				ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
				ast_free(params);
				ast_free(vals);
				return -1;
			}

			query = tmp_str;
		}
	}

	ast_free(params);
	ast_free(vals);

	if (!(tmp_str = sqlite_mprintf("%s WHERE %q = '%q';", query, keyfield, entity))) {
		ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
		sqlite_freemem(query);
		return -1;
	}

	sqlite_freemem(query);
	query = tmp_str;
	ast_debug(1, "SQL query: %s\n", query);

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, query, NULL, NULL, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	if (!error)
		rows_num = sqlite_changes(db);
	else
		rows_num = -1;

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
	}
	sqlite_freemem(errormsg);

	return rows_num;
}

static int realtime_update2_handler(const char *database, const char *table,
	va_list ap)
{
	char *errormsg = NULL, *tmp1, *tmp2;
	int error, rows_num, first = 1;
	struct ast_str *sql = ast_str_thread_get(&sql_buf, 100);
	struct ast_str *where = ast_str_thread_get(&where_buf, 100);
	const char *param, *value;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return -1;
	}

	if (!sql) {
		return -1;
	}

	ast_str_set(&sql, 0, "UPDATE %s SET", table);
	ast_str_set(&where, 0, " WHERE");

	while ((param = va_arg(ap, const char *))) {
		value = va_arg(ap, const char *);
		ast_str_append(&where, 0, "%s %s = %s",
			first ? "" : " AND",
			tmp1 = sqlite_mprintf("%q", param),
			tmp2 = sqlite_mprintf("%Q", value));
		sqlite_freemem(tmp1);
		sqlite_freemem(tmp2);
		first = 0;
	}

	if (first) {
		ast_log(LOG_ERROR, "No criteria specified on update to '%s@%s'!\n", table, database);
		return -1;
	}

	first = 1;
	while ((param = va_arg(ap, const char *))) {
		value = va_arg(ap, const char *);
		ast_str_append(&sql, 0, "%s %s = %s",
			first ? "" : ",",
			tmp1 = sqlite_mprintf("%q", param),
			tmp2 = sqlite_mprintf("%Q", value));
		sqlite_freemem(tmp1);
		sqlite_freemem(tmp2);
		first = 0;
	}

	ast_str_append(&sql, 0, " %s", ast_str_buffer(where));
	ast_debug(1, "SQL query: %s\n", ast_str_buffer(sql));

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, ast_str_buffer(sql), NULL, NULL, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	if (!error) {
		rows_num = sqlite_changes(db);
	} else {
		rows_num = -1;
	}

	ast_mutex_unlock(&mutex);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
	}
	sqlite_freemem(errormsg);

	return rows_num;
}

static int realtime_store_handler(const char *database, const char *table, va_list ap)
{
	char *errormsg = NULL, *tmp_str, *tmp_keys = NULL, *tmp_keys2 = NULL, *tmp_vals = NULL, *tmp_vals2 = NULL;
	const char **params, **vals;
	size_t params_count;
	int error, rows_id;
	size_t i;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return -1;
	}

	if (!(params_count = get_params(ap, &params, &vals, 1)))
		return -1;

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "INSERT into '%q' (%s) VALUES (%s);"
/* \endcond */

	for (i = 0; i < params_count; i++) {
		if ( tmp_keys2 ) {
			tmp_keys = sqlite_mprintf("%s, %q", tmp_keys2, params[i]);
			sqlite_freemem(tmp_keys2);
		} else {
			tmp_keys = sqlite_mprintf("%q", params[i]);
		}
		if (!tmp_keys) {
			ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
			sqlite_freemem(tmp_vals);
			ast_free(params);
			ast_free(vals);
			return -1;
		}

		if ( tmp_vals2 ) {
			tmp_vals = sqlite_mprintf("%s, '%q'", tmp_vals2, vals[i]);
			sqlite_freemem(tmp_vals2);
		} else {
			tmp_vals = sqlite_mprintf("'%q'", vals[i]);
		}
		if (!tmp_vals) {
			ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
			sqlite_freemem(tmp_keys);
			ast_free(params);
			ast_free(vals);
			return -1;
		}


		tmp_keys2 = tmp_keys;
		tmp_vals2 = tmp_vals;
	}

	ast_free(params);
	ast_free(vals);

	if (!(tmp_str = sqlite_mprintf(QUERY, table, tmp_keys, tmp_vals))) {
		ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
		sqlite_freemem(tmp_keys);
		sqlite_freemem(tmp_vals);
		return -1;
	}

	sqlite_freemem(tmp_keys);
	sqlite_freemem(tmp_vals);

	ast_debug(1, "SQL query: %s\n", tmp_str);

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, tmp_str, NULL, NULL, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	if (!error) {
		rows_id = sqlite_last_insert_rowid(db);
	} else {
		rows_id = -1;
	}

	ast_mutex_unlock(&mutex);

	sqlite_freemem(tmp_str);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
	}
	sqlite_freemem(errormsg);

	return rows_id;
}

static int realtime_destroy_handler(const char *database, const char *table,
	const char *keyfield, const char *entity, va_list ap)
{
	char *query, *errormsg = NULL, *tmp_str;
	const char **params = NULL, **vals = NULL;
	size_t params_count;
	int error, rows_num;
	size_t i;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return -1;
	}

	params_count = get_params(ap, &params, &vals, 0);

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "DELETE FROM '%q' WHERE"
/* \endcond */

	if (!(query = sqlite_mprintf(QUERY, table))) {
		ast_log(LOG_WARNING, "Unable to allocate SQL query\n");
		ast_free(params);
		ast_free(vals);
		return -1;
	}

	for (i = 0; i < params_count; i++) {
		tmp_str = sqlite_mprintf("%s %q = '%q' AND", query, params[i], vals[i]);
		sqlite_freemem(query);

		if (!tmp_str) {
			ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
			ast_free(params);
			ast_free(vals);
			return -1;
		}

		query = tmp_str;
	}

	ast_free(params);
	ast_free(vals);
	if (!(tmp_str = sqlite_mprintf("%s %q = '%q';", query, keyfield, entity))) {
		ast_log(LOG_WARNING, "Unable to reallocate SQL query\n");
		sqlite_freemem(query);
		return -1;
	}
	sqlite_freemem(query);
	query = tmp_str;
	ast_debug(1, "SQL query: %s\n", query);

	ast_mutex_lock(&mutex);

	RES_CONFIG_SQLITE_BEGIN
		error = sqlite_exec(db, query, NULL, NULL, &errormsg);
	RES_CONFIG_SQLITE_END(error)

	if (!error) {
		rows_num = sqlite_changes(db);
	} else {
		rows_num = -1;
	}

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
	}
	sqlite_freemem(errormsg);

	return rows_num;
}

static int realtime_require_handler(const char *unused, const char *tablename, va_list ap)
{
	struct sqlite_cache_tables *tbl = find_table(tablename);
	struct sqlite_cache_columns *col;
	char *elm;
	int type, res = 0;

	if (!tbl) {
		return -1;
	}

	while ((elm = va_arg(ap, char *))) {
		type = va_arg(ap, require_type);
		va_arg(ap, int);
		/* Check if the field matches the criteria */
		AST_RWLIST_TRAVERSE(&tbl->columns, col, list) {
			if (strcmp(col->name, elm) == 0) {
				/* SQLite only has two types - the 32-bit integer field that
				 * is the key column, and everything else (everything else
				 * being a string).
				 */
				if (col->isint && !ast_rq_is_int(type)) {
					ast_log(LOG_WARNING, "Realtime table %s: column '%s' is an integer field, but Asterisk requires that it not be!\n", tablename, col->name);
					res = -1;
				}
				break;
			}
		}
		if (!col) {
			ast_log(LOG_WARNING, "Realtime table %s requires column '%s', but that column does not exist!\n", tablename, elm);
		}
	}
	AST_RWLIST_UNLOCK(&(tbl->columns));
	return res;
}

static int realtime_unload_handler(const char *unused, const char *tablename)
{
	struct sqlite_cache_tables *tbl;
	AST_RWLIST_WRLOCK(&sqlite_tables);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&sqlite_tables, tbl, list) {
		if (!strcasecmp(tbl->name, tablename)) {
			AST_RWLIST_REMOVE_CURRENT(list);
			free_table(tbl);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END
	AST_RWLIST_UNLOCK(&sqlite_tables);
	return 0;
}

static char *handle_cli_show_sqlite_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "sqlite show status";
		e->usage =
			"Usage: sqlite show status\n"
			"       Show status information about the SQLite 2 driver\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "SQLite database path: %s\n", dbfile);
	ast_cli(a->fd, "config_table: ");

	if (!config_table)
		ast_cli(a->fd, "unspecified, must be present in extconfig.conf\n");
	else
		ast_cli(a->fd, "%s\n", config_table);

	ast_cli(a->fd, "cdr_table: ");

	if (!cdr_table)
		ast_cli(a->fd, "unspecified, CDR support disabled\n");
	else
		ast_cli(a->fd, "%s\n", cdr_table);

	return CLI_SUCCESS;
}

static char *handle_cli_sqlite_show_tables(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sqlite_cache_tables *tbl;
	struct sqlite_cache_columns *col;
	int found = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sqlite show tables";
		e->usage =
			"Usage: sqlite show tables\n"
			"       Show table information about the SQLite 2 driver\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	AST_RWLIST_RDLOCK(&sqlite_tables);
	AST_RWLIST_TRAVERSE(&sqlite_tables, tbl, list) {
		found++;
		ast_cli(a->fd, "Table %s:\n", tbl->name);
		AST_RWLIST_TRAVERSE(&(tbl->columns), col, list) {
			fprintf(stderr, "%s\n", col->name);
			ast_cli(a->fd, "  %20.20s  %-30.30s\n", col->name, col->type);
		}
	}
	AST_RWLIST_UNLOCK(&sqlite_tables);

	if (!found) {
		ast_cli(a->fd, "No tables currently in cache\n");
	}

	return CLI_SUCCESS;
}

static int unload_module(void)
{
	if (cli_status_registered)
		ast_cli_unregister_multiple(cli_status, ARRAY_LEN(cli_status));

	if (cdr_registered)
		ast_cdr_unregister(RES_CONFIG_SQLITE_NAME);

	ast_config_engine_deregister(&sqlite_engine);

	if (db)
		sqlite_close(db);

	unload_config();

	return 0;
}

static int load_module(void)
{
	char *errormsg = NULL;
	int error;

	db = NULL;
	cdr_registered = 0;
	cli_status_registered = 0;
	dbfile = NULL;
	config_table = NULL;
	cdr_table = NULL;
	error = load_config();

	if (error)
		return AST_MODULE_LOAD_DECLINE;

	if (!(db = sqlite_open(dbfile, 0660, &errormsg))) {
		ast_log(LOG_ERROR, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
		sqlite_freemem(errormsg);
		unload_module();
		return 1;
	}

	sqlite_freemem(errormsg);
	errormsg = NULL;
	ast_config_engine_register(&sqlite_engine);

	if (use_cdr) {
		char *query;

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "SELECT COUNT(id) FROM %Q;"
/* \endcond */

		query = sqlite_mprintf(QUERY, cdr_table);

		if (!query) {
			ast_log(LOG_ERROR, "Unable to allocate SQL query\n");
			unload_module();
			return 1;
		}

		ast_debug(1, "SQL query: %s\n", query);

		RES_CONFIG_SQLITE_BEGIN
			error = sqlite_exec(db, query, NULL, NULL, &errormsg);
		RES_CONFIG_SQLITE_END(error)

		sqlite_freemem(query);

		if (error) {
			/*
			 * Unexpected error.
			 */
			if (error != SQLITE_ERROR) {
				ast_log(LOG_ERROR, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
				sqlite_freemem(errormsg);
				unload_module();
				return 1;
			}

			sqlite_freemem(errormsg);
			errormsg = NULL;
			query = sqlite_mprintf(sql_create_cdr_table, cdr_table);

			if (!query) {
				ast_log(LOG_ERROR, "Unable to allocate SQL query\n");
				unload_module();
				return 1;
			}

			ast_debug(1, "SQL query: %s\n", query);

			RES_CONFIG_SQLITE_BEGIN
				error = sqlite_exec(db, query, NULL, NULL, &errormsg);
			RES_CONFIG_SQLITE_END(error)

			sqlite_freemem(query);

			if (error) {
				ast_log(LOG_ERROR, "%s\n", S_OR(errormsg, sqlite_error_string(error)));
				sqlite_freemem(errormsg);
				unload_module();
				return 1;
			}
		}
		sqlite_freemem(errormsg);
		errormsg = NULL;

		error = ast_cdr_register(RES_CONFIG_SQLITE_NAME, RES_CONFIG_SQLITE_DESCRIPTION, cdr_handler);

		if (error) {
			unload_module();
			return 1;
		}

		cdr_registered = 1;
	}

	error = ast_cli_register_multiple(cli_status, ARRAY_LEN(cli_status));

	if (error) {
		unload_module();
		return 1;
	}

	cli_status_registered = 1;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Realtime SQLite configuration",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
