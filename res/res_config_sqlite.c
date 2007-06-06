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
 * \mainpage res_config_sqlite
 * 
 * \section intro_sec Presentation
 * 
 * res_config_sqlite is a module for the Asterisk Open Source PBX to 
 * support SQLite 2 databases. It can be used to fetch configuration 
 * from a database (static configuration files and/or using the Asterisk 
 * RealTime Architecture - ARA).
 * It can also be used to log CDR entries. Finally, it can be used for simple
 * queries in the Dialplan. Note that Asterisk already comes with a module
 * named cdr_sqlite. There are two reasons for including it in res_sqlite:
 * the first is that rewriting it was a training to learn how to write a
 * simple module for Asterisk, the other is to have the same database open for
 * all kinds of operations, which improves reliability and performance.
 * 
 * There is already a module for SQLite 3 (named res_sqlite3) in the Asterisk
 * addons. res_sqlite was developed because we, at Proformatique, are using
 * PHP 4 in our embedded systems, and PHP 4 has no stable support for SQLite 3
 * at this time. We also needed RealTime support.
 * 
 * \section conf_sec Configuration
 * 
 * The main configuration file is res_config_sqlite.conf. It must be readable or
 * res_sqlite will fail to start. It is suggested to use the sample file
 * in this package as a starting point. The file has only one section
 * named <code>general</code>. Here are the supported parameters :
 * 
 * <dl>
 *	<dt><code>dbfile</code></dt>
 *	<dd>The absolute path to the SQLite database (the file can be non existent,
 *			res_sqlite will create it if is has the appropriate rights)</dd>
 *	<dt><code>config_table</code></dt>
 *	<dd>The table used for static configuration</dd>
 *	<dt><code>cdr_table</code></dt>
 *	<dd>The table used to store CDR entries (if ommitted, CDR support is
 *			disabled)</dd>
 * </dl>
 * 
 * To use res_sqlite for static and/or RealTime configuration, refer to the
 * Asterisk documentation. The file tables.sql can be used to create the
 * needed tables.
 * 
 * The SQLITE() application is very similar to the MYSQL() application. You
 * can find more details at
 * <a href="http://voip-info.org/wiki/view/Asterisk+cmd+MYSQL">http://voip-info.org/wiki/view/Asterisk+cmd+MYSQL</a>.
 * The main difference is that you cannot choose your database - it's the
 * file set in the <code>dbfile</code> parameter. As a result, there is no
 * Connect or Disconnect command, and there is no connid variable.
 * 
 * \section status_sec Driver status
 * 
 * The CLI command <code>show sqlite status</code> returns status information
 * about the running driver. One information is more important than others:
 * the number of registered virtual machines. A SQLite virtual machine is
 * created each time a SQLITE() query command is used. If the number of
 * registered virtual machines isn't 0 (or near 0, since one or more SQLITE()
 * commands can be running when requesting the module status) and increases
 * over time, this probably means that you're badly using the application
 * and you're creating resource leaks. You should check your Dialplan and
 * reload res_sqlite (by unloading and then loading again - reloading isn't
 * supported)
 * 
 * \section credits_sec Credits
 * 
 * res_config_sqlite was developed by Richard Braun at the Proformatique company.
 */

/*!
 * \file res_sqlite.c
 * \brief res_sqlite module.
 */

/*** MODULEINFO
	<depend>sqlite</depend>
 ***/

#include "asterisk.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite.h>

#include "asterisk/pbx.h"
#include "asterisk/cdr.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/linkedlists.h"

#define RES_SQLITE_NAME "res_sqlite"
#define RES_SQLITE_DRIVER "sqlite"
#define RES_SQLITE_APP_DRIVER "SQLITE"
#define RES_SQLITE_DESCRIPTION "Resource Module for SQLite 2"
#define RES_SQLITE_CONF_FILE "res_config_sqlite.conf"
#define RES_SQLITE_APP_SYNOPSIS "Dialplan access to SQLite 2"
#define RES_SQLITE_APP_DESCRIPTION \
"SQLITE(): " RES_SQLITE_APP_SYNOPSIS "\n"
#define RES_SQLITE_STATUS_SUMMARY \
"Show status information about the SQLite 2 driver"
#define RES_SQLITE_STATUS_USAGE \
"Usage: show sqlite status\n" \
"	" RES_SQLITE_STATUS_SUMMARY "\n"

enum {
	RES_SQLITE_CONFIG_ID,
	RES_SQLITE_CONFIG_COMMENTED,
	RES_SQLITE_CONFIG_FILENAME,
	RES_SQLITE_CONFIG_CATEGORY,
	RES_SQLITE_CONFIG_VAR_NAME,
	RES_SQLITE_CONFIG_VAR_VAL,
	RES_SQLITE_CONFIG_COLUMNS,
};

/*!
 * Limit the number of maximum simultaneous registered SQLite VMs to avoid
 * a denial of service attack.
 */
#define RES_SQLITE_VM_MAX 1024

#define SET_VAR(config, to, from) \
do \
	{ \
		int __error; \
		__error = set_var(&to, #to, from->value); \
		if (__error) \
			{ \
				ast_config_destroy(config); \
				unload_config(); \
				return 1; \
			} \
	} \
while (0)

/*!
 * Maximum number of loops before giving up executing a query. Calls to
 * sqlite_xxx() functions which can return SQLITE_BUSY or SQLITE_LOCKED
 * are enclosed by RES_SQLITE_BEGIN and RES_SQLITE_END, e.g.
 * <pre>
 * char *errormsg;
 * int error;
 * 
 * RES_SQLITE_BEGIN
 *	 error = sqlite_exec(db, query, NULL, NULL, &errormsg);
 * RES_SQLITE_END(error)
 * 
 * if (error)
 *	 ...;
 * </pre>
 */
#define RES_SQLITE_MAX_LOOPS 10

/*!
 * Macro used before executing a query.
 * 
 * \see RES_SQLITE_MAX_LOOPS.
 */
#define RES_SQLITE_BEGIN \
{ \
	int __i; \
	for (__i = 0; __i < RES_SQLITE_MAX_LOOPS; __i++) \
		{

/*!
 * Macro used after executing a query.
 * 
 * \see RES_SQLITE_MAX_LOOPS.
 */
#define RES_SQLITE_END(error) \
			if (error != SQLITE_BUSY && error != SQLITE_LOCKED) \
				break; \
			usleep(1000); \
		} \
}

/*!
 * Structure sent to the SQLite callback function for static configuration.
 * 
 * \see add_cfg_entry()
 */
struct cfg_entry_args {
	struct ast_config *cfg;
	struct ast_category *cat;
	char *cat_name;
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
 * Allocate a variable.
 * 
 * \param var	 the address of the variable to set (it will be allocated)
 * \param name	the name of the variable (for error handling)
 * \param value the value to store in var
 * \return 1 if an allocation error occurred, 0 otherwise
 */
static int set_var(char **var, char *name, char *value);

/*!
 * Load the configuration file.
 * 
 * This function sets dbfile, config_table, and cdr_table. It calls
 * check_vars() before returning, and unload_config() if an error occurred.
 * 
 * \return 1 if an error occurred, 0 otherwise
 * \see unload_config()
 */
static int load_config(void);

/*!
 * Free resources related to configuration.
 * 
 * \see load_config()
 */
static void unload_config(void);

/*!
 * Asterisk callback function for CDR support.
 * 
 * Asterisk will call this function each time a CDR entry must be logged if
 * CDR support is enabled.
 * 
 * \param cdr the CDR entry Asterisk sends us
 * \return 1 if an error occurred, 0 otherwise
 */
static int cdr_handler(struct ast_cdr *cdr);

/*!
 * SQLite callback function for static configuration.
 * 
 * This function is passed to the SQLite engine as a callback function to
 * parse a row and store it in a struct ast_config object. It relies on
 * resulting rows	being sorted by category.
 * 
 * \param arg				 a pointer to a struct cfg_entry_args object
 * \param argc				number of columns
 * \param argv				values in the row
 * \param columnNames names and types of the columns
 * \return 1 if an error occurred, 0 otherwise
 * \see cfg_entry_args
 * \see sql_get_config_table
 * \see config_handler()
 */
static int add_cfg_entry(void *arg, int argc, char **argv, char **columnNames);

/*!
 * Asterisk callback function for static configuration.
 * 
 * Asterisk will call this function when it loads its static configuration,
 * which usually happens at startup and reload.
 * 
 * \param database the database to use (ignored)
 * \param table		the table to use
 * \param file		 the file to load from the database
 * \param cfg			the struct ast_config object to use when storing variables
 * \return NULL if an error occurred, cfg otherwise
 * \see add_cfg_entry()
 */
static struct ast_config * config_handler(const char *database,
	const char *table, const char *file,
	struct ast_config *cfg, int withcomments);

/*!
 * Helper function to parse a va_list object into 2 dynamic arrays of
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
 * \param ap				 the va_list object to parse
 * \param params_ptr where the address of the params array is stored
 * \param vals_ptr	 where the address of the vals array is stored
 * \return 0 if an error occurred, the number of elements in the arrays (which
 *				 have the same size) otherwise
 */
static size_t get_params(va_list ap, const char ***params_ptr,
	const char ***vals_ptr);

/*!
 * SQLite callback function for RealTime configuration.
 * 
 * This function is passed to the SQLite engine as a callback function to
 * parse a row and store it in a linked list of struct ast_variable objects.
 * 
 * \param arg				 a pointer to a struct rt_cfg_entry_args object
 * \param argc				number of columns
 * \param argv				values in the row
 * \param columnNames names and types of the columns
 * \return 1 if an error occurred, 0 otherwise
 * \see rt_cfg_entry_args
 * \see realtime_handler()
 */
static int add_rt_cfg_entry(void *arg, int argc, char **argv,
	char **columnNames);

/*!
 * Asterisk callback function for RealTime configuration.
 * 
 * Asterisk will call this function each time it requires a variable
 * through the RealTime architecture. ap is a list of parameters and
 * values used to find a specific row, e.g one parameter "name" and
 * one value "123" so that the SQL query becomes <code>SELECT * FROM
 * table WHERE name = '123';</code>.
 * 
 * \param database the database to use (ignored)
 * \param table		the table to use
 * \param ap			 list of parameters and values to match
 * \return NULL if an error occurred, a linked list of struct ast_variable
 *				 objects otherwise
 * \see add_rt_cfg_entry()
 */
static struct ast_variable * realtime_handler(const char *database,
	const char *table, va_list ap);

/*!
 * SQLite callback function for RealTime configuration.
 * 
 * This function performs the same actions as add_rt_cfg_entry() except
 * that the rt_multi_cfg_entry_args structure is designed to store
 * categories in addition of variables.
 * 
 * \param arg				 a pointer to a struct rt_multi_cfg_entry_args object
 * \param argc				number of columns
 * \param argv				values in the row
 * \param columnNames names and types of the columns
 * \return 1 if an error occurred, 0 otherwise
 * \see rt_multi_cfg_entry_args
 * \see realtime_multi_handler()
 */
static int add_rt_multi_cfg_entry(void *arg, int argc, char **argv,
	char **columnNames);

/*!
 * Asterisk callback function for RealTime configuration.
 * 
 * This function performs the same actions as realtime_handler() except
 * that it can store variables per category, and can return several
 * categories.
 * 
 * \param database the database to use (ignored)
 * \param table		the table to use
 * \param ap			 list of parameters and values to match
 * \return NULL if an error occurred, a struct ast_config object storing
 *				 categories and variables
 * \see add_rt_multi_cfg_entry()
 */
static struct ast_config * realtime_multi_handler(const char *database,
	const char *table,
	va_list ap);

/*!
 * Asterisk callback function for RealTime configuration (variable
 * update).
 * 
 * Asterisk will call this function each time a variable has been modified
 * internally and must be updated in the backend engine. keyfield and entity
 * are used to find the row to update, e.g. <code>UPDATE table SET ... WHERE
 * keyfield = 'entity';</code>. ap is a list of parameters and values with the
 * same format as the other realtime functions.
 * 
 * \param database the database to use (ignored)
 * \param table		the table to use
 * \param keyfield the column of the matching cell
 * \param entity	 the value of the matching cell
 * \param ap			 list of parameters and new values to update in the database
 * \return -1 if an error occurred, the number of affected rows otherwise
 */
static int realtime_update_handler(const char *database, const char *table,
	const char *keyfield, const char *entity,
	va_list ap);

/*!
 * Asterisk callback function for the CLI status command.
 * 
 * \param fd	 file descriptor provided by Asterisk to use with ast_cli()
 * \param argc number of arguments
 * \param argv arguments list
 * \return RESULT_SUCCESS
 */
static int cli_status(int fd, int argc, char *argv[]);

/*!
 * The SQLite database object.
 */
static sqlite *db;

/*!
 * Set to 1 if CDR support is enabled.
 */
static int use_cdr;

/*!
 * Set to 1 if the CDR callback function was registered.
 */
static int cdr_registered;

/*!
 * Set to 1 if the CLI status command callback function was registered.
 */
static int cli_status_registered;

/*!
 * The path of the database file.
 */
static char *dbfile;

/*!
 * The name of the static configuration table.
 */
static char *config_table;

/*!
 * The name of the table used to store CDR entries.
 */
static char *cdr_table;

/*!
 * The number of registered virtual machines.
 */
static int vm_count;

/*!
 * The structure specifying all callback functions used by Asterisk for static
 * and RealTime configuration.
 */
static struct ast_config_engine sqlite_engine =
{
	.name = RES_SQLITE_DRIVER,
	.load_func = config_handler,
	.realtime_func = realtime_handler,
	.realtime_multi_func = realtime_multi_handler,
	.update_func = realtime_update_handler
};

/*!
 * The mutex used to prevent simultaneous access to the SQLite database.
 * SQLite isn't always compiled with thread safety.
 */
AST_MUTEX_DEFINE_STATIC(mutex);

/*!
 * Structure containing details and callback functions for the CLI status
 * command.
 */
static struct ast_cli_entry cli_status_cmd =
{
	.cmda = {"show", "sqlite", "status", NULL},
	.handler = cli_status,
	.summary = RES_SQLITE_STATUS_SUMMARY,
	.usage = RES_SQLITE_STATUS_USAGE
};

/*
 * Taken from Asterisk 1.2 cdr_sqlite.so.
 */

/*!
 * SQL query format to create the CDR table if non existent.
 */
static char *sql_create_cdr_table =
"CREATE TABLE '%q' ("
"	id		INTEGER PRIMARY KEY,"
"	clid		VARCHAR(80) NOT NULL DEFAULT '',"
"	src		VARCHAR(80) NOT NULL DEFAULT '',"
"	dst		VARCHAR(80) NOT NULL DEFAULT '',"
"	dcontext	VARCHAR(80) NOT NULL DEFAULT '',"
"	channel		VARCHAR(80) NOT NULL DEFAULT '',"
"	dstchannel	VARCHAR(80) NOT NULL DEFAULT '',"
"	lastapp		VARCHAR(80) NOT NULL DEFAULT '',"
"	lastdata	VARCHAR(80) NOT NULL DEFAULT '',"
"	start		CHAR(19) NOT NULL DEFAULT '0000-00-00 00:00:00',"
"	answer		CHAR(19) NOT NULL DEFAULT '0000-00-00 00:00:00',"
"	end		CHAR(19) NOT NULL DEFAULT '0000-00-00 00:00:00',"
"	duration	INT(11) NOT NULL DEFAULT '0',"
"	billsec		INT(11) NOT NULL DEFAULT '0',"
"	disposition	INT(11) NOT NULL DEFAULT '0',"
"	amaflags	INT(11) NOT NULL DEFAULT '0',"
"	accountcode	VARCHAR(20) NOT NULL DEFAULT '',"
"	uniqueid	VARCHAR(32) NOT NULL DEFAULT '',"
"	userfield	VARCHAR(255) NOT NULL DEFAULT ''"
");";

/*!
 * SQL query format to insert a CDR entry.
 */
static char *sql_add_cdr_entry =
"INSERT INTO '%q' ("
"			 clid,"
"	src,"
"	dst,"
"	dcontext,"
"	channel,"
"	dstchannel,"
"	lastapp,"
"	lastdata,"
"	start,"
"	answer,"
"	end,"
"	duration,"
"	billsec,"
"	disposition,"
"	amaflags,"
"	accountcode,"
"	uniqueid,"
"	userfield"
") VALUES ("
"	'%q',"
"	'%q',"
"	'%q',"
"	'%q',"
"	'%q',"
"	'%q',"
"	'%q',"
"	'%q',"
"	datetime(%d,'unixepoch'),"
"	datetime(%d,'unixepoch'),"
"	datetime(%d,'unixepoch'),"
"	'%ld',"
"	'%ld',"
"	'%ld',"
"	'%ld',"
"	'%q',"
"	'%q',"
"	'%q'"
");";

/*!
 * SQL query format to fetch the static configuration of a file.
 * Rows must be sorted by category.
 * 
 * @see add_cfg_entry()
 */
static char *sql_get_config_table =
"SELECT *"
"	FROM '%q'"
"	WHERE filename = '%q' AND commented = 0"
"	ORDER BY category;";

static int set_var(char **var, char *name, char *value)
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
		ast_log(LOG_ERROR, "Undefined parameter %s\n", dbfile);
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

	config = ast_config_load(RES_SQLITE_CONF_FILE);

	if (!config) {
		ast_log(LOG_ERROR, "Unable to load " RES_SQLITE_CONF_FILE "\n");
		return 1;
	}

	for (var = ast_variable_browse(config, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "dbfile"))
			SET_VAR(config, dbfile, var);
		else if (!strcasecmp(var->name, "config_table"))
			SET_VAR(config, config_table, var);
		else if (!strcasecmp(var->name, "cdr_table"))
			SET_VAR(config, cdr_table, var);
		else
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
	ast_free(dbfile);
	dbfile = NULL;
	ast_free(config_table);
	config_table = NULL;
	ast_free(cdr_table);
	cdr_table = NULL;
}

static int cdr_handler(struct ast_cdr *cdr)
{
	char *errormsg;
	int error;

	ast_mutex_lock(&mutex);

	RES_SQLITE_BEGIN
		error = sqlite_exec_printf(db, sql_add_cdr_entry, NULL, NULL, &errormsg,
					 cdr_table, cdr->clid, cdr->src, cdr->dst,
					 cdr->dcontext, cdr->channel, cdr->dstchannel,
					 cdr->lastapp, cdr->lastdata, cdr->start.tv_sec,
					 cdr->answer.tv_sec, cdr->end.tv_sec,
					 cdr->duration, cdr->billsec, cdr->disposition,
					 cdr->amaflags, cdr->accountcode, cdr->uniqueid,
					 cdr->userfield);
	RES_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	if (error) {
		ast_log(LOG_ERROR, "%s\n", errormsg);
		ast_free(errormsg);
		return 1;
	}

	return 0;
}

static int add_cfg_entry(void *arg, int argc, char **argv, char **columnNames)
{
	struct cfg_entry_args *args;
	struct ast_variable *var;

	if (argc != RES_SQLITE_CONFIG_COLUMNS) {
		ast_log(LOG_WARNING, "Corrupt table\n");
		return 1;
	}

	args = arg;

	if (!args->cat_name || strcmp(args->cat_name, argv[RES_SQLITE_CONFIG_CATEGORY])) {
		args->cat = ast_category_new(argv[RES_SQLITE_CONFIG_CATEGORY]);

		if (!args->cat) {
			ast_log(LOG_WARNING, "Unable to allocate category\n");
			return 1;
		}

		ast_free(args->cat_name);
		args->cat_name = ast_strdup(argv[RES_SQLITE_CONFIG_CATEGORY]);

		if (!args->cat_name) {
			ast_category_destroy(args->cat);
			return 1;
		}

		ast_category_append(args->cfg, args->cat);
	}

	var = ast_variable_new(argv[RES_SQLITE_CONFIG_VAR_NAME],
		 argv[RES_SQLITE_CONFIG_VAR_VAL]);

	if (!var) {
		ast_log(LOG_WARNING, "Unable to allocate variable");
		return 1;
	}

	ast_variable_append(args->cat, var);
	
	return 0;
}

static struct ast_config *config_handler(const char *database, 
	const char *table, const char *file, struct ast_config *cfg, int withcomments)
{
	struct cfg_entry_args args;
	char *errormsg;
	int error;

	if (!config_table) {
		if (!table) {
			ast_log(LOG_ERROR, "Table name unspecified\n");
			return NULL;
		}
	} else
		table = config_table;

	args.cfg = cfg;
	args.cat = NULL;
	args.cat_name = NULL;

	ast_mutex_lock(&mutex);

	RES_SQLITE_BEGIN
		error = sqlite_exec_printf(db, sql_get_config_table, add_cfg_entry,
					&args, &errormsg, table, file);
	RES_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	ast_free(args.cat_name);

	if (error) {
		ast_log(LOG_ERROR, "%s\n", errormsg);
		ast_free(errormsg);
		return NULL;
	}

	return cfg;
}

static size_t get_params(va_list ap, const char ***params_ptr, const char ***vals_ptr)
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

	if (params_count)
		ast_log(LOG_WARNING, "1 parameter and 1 value at least required\n");
	else {
		*params_ptr = params;
		*vals_ptr = vals;
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

		if (!(var = ast_variable_new(columnNames[i], argv[i])))
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

static struct ast_variable *
realtime_handler(const char *database, const char *table, va_list ap)
{
	char *query, *errormsg, *op, *tmp_str;
	struct rt_cfg_entry_args args;
	const char **params, **vals;
	size_t params_count;
	int error;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return NULL;
	}

	params_count = get_params(ap, &params, &vals);

	if (params_count == 0)
		return NULL;

	op = (strchr(params[0], ' ') == NULL) ? " =" : "";

/* \cond DOXYGEN_CAN_PARSE_THIS */
#undef QUERY
#define QUERY "SELECT * FROM '%q' WHERE commented = 0 AND %q%s '%q'"
/* \endcond */

	query = sqlite_mprintf(QUERY, table, params[0], op, vals[0]);

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
			tmp_str = sqlite_mprintf("%s AND %q%s '%q'", query, params[i], op,
															 vals[i]);
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
	ast_log(LOG_DEBUG, "SQL query: %s\n", query);
	args.var = NULL;
	args.last = NULL;

	ast_mutex_lock(&mutex);

	RES_SQLITE_BEGIN
		error = sqlite_exec(db, query, add_rt_cfg_entry, &args, &errormsg);
	RES_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", errormsg);
		ast_free(errormsg);
		ast_variables_destroy(args.var);
		return NULL;
	}

	return args.var;
}

static int add_rt_multi_cfg_entry(void *arg, int argc, char **argv, char **columnNames)
{
	struct rt_multi_cfg_entry_args *args;
	struct ast_category *cat;
	struct ast_variable *var;
	char *cat_name;
	size_t i;

	args = (struct rt_multi_cfg_entry_args *)arg;
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

	if (!(cat = ast_category_new(cat_name))) {
		ast_log(LOG_WARNING, "Unable to allocate category\n");
		return 1;
	}

	ast_category_append(args->cfg, cat);

	for (i = 0; i < argc; i++) {
		if (!argv[i] || !strcmp(args->initfield, columnNames[i]))
			continue;

		if (!(var = ast_variable_new(columnNames[i], argv[i]))) {
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
	char *query, *errormsg, *op, *tmp_str, *initfield;
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

	if (!(params_count = get_params(ap, &params, &vals))) {
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
#define QUERY "SELECT * FROM '%q' WHERE commented = 0 AND %q%s '%q'"
/* \endcond */

	if (!(query = sqlite_mprintf(QUERY, table, params[0], op, tmp_str))) {
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
			tmp_str = sqlite_mprintf("%s AND %q%s '%q'", query, params[i], op,
															 vals[i]);
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
		ast_config_destroy(cfg);
		ast_free(initfield);
		return NULL;
	}

	sqlite_freemem(query);
	query = tmp_str;
	ast_log(LOG_DEBUG, "SQL query: %s\n", query);
	args.cfg = cfg;
	args.initfield = initfield;

	ast_mutex_lock(&mutex);

	RES_SQLITE_BEGIN
		error = sqlite_exec(db, query, add_rt_multi_cfg_entry, &args, &errormsg);
	RES_SQLITE_END(error)

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);
	ast_free(initfield);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", errormsg);
		ast_free(errormsg);
		ast_config_destroy(cfg);
		return NULL;
	}

	return cfg;
}

static int realtime_update_handler(const char *database, const char *table,
	const char *keyfield, const char *entity,
	va_list ap)
{
	char *query, *errormsg, *tmp_str;
	const char **params, **vals;
	size_t params_count;
	int error, rows_num;

	if (!table) {
		ast_log(LOG_WARNING, "Table name unspecified\n");
		return -1;
	}

	if (!(params_count = get_params(ap, &params, &vals)))
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
			tmp_str = sqlite_mprintf("%s, %q = '%q'", query, params[i],
															 vals[i]);
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
		return -1;
	}

	sqlite_freemem(query);
	query = tmp_str;
	ast_log(LOG_DEBUG, "SQL query: %s\n", query);

	ast_mutex_lock(&mutex);

	RES_SQLITE_BEGIN
		error = sqlite_exec(db, query, NULL, NULL, &errormsg);
	RES_SQLITE_END(error)

	if (!error)
		rows_num = sqlite_changes(db);
	else
		rows_num = -1;

	ast_mutex_unlock(&mutex);

	sqlite_freemem(query);

	if (error) {
		ast_log(LOG_WARNING, "%s\n", errormsg);
		ast_free(errormsg);
	}

	return rows_num;
}

static int cli_status(int fd, int argc, char *argv[])
{
	ast_cli(fd, "SQLite database path: %s\n", dbfile);
	ast_cli(fd, "config_table: ");

	if (!config_table)
		ast_cli(fd, "unspecified, must be present in extconfig.conf\n");
	else
		ast_cli(fd, "%s\n", config_table);

	ast_cli(fd, "cdr_table: ");

	if (!cdr_table)
		ast_cli(fd, "unspecified, CDR support disabled\n");
	else
		ast_cli(fd, "%s\n", cdr_table);

	return RESULT_SUCCESS;
}

static int unload_module(void)
{
	if (cli_status_registered)
		ast_cli_unregister(&cli_status_cmd);

	if (cdr_registered)
		ast_cdr_unregister(RES_SQLITE_NAME);

	ast_config_engine_deregister(&sqlite_engine);

	if (db)
		sqlite_close(db);

	unload_config();

	return 0;
}

static int load_module(void)
{
	char *errormsg;
	int error;

	db = NULL;
	cdr_registered = 0;
	cli_status_registered = 0;
	dbfile = NULL;
	config_table = NULL;
	cdr_table = NULL;
	vm_count = 0;
	error = load_config();

	if (error)
		return AST_MODULE_LOAD_DECLINE;

	if (!(db = sqlite_open(dbfile, 0660, &errormsg))) {
		ast_log(LOG_ERROR, "%s\n", errormsg);
		ast_free(errormsg);
		unload_module();
		return 1;
	}

	ast_config_engine_register(&sqlite_engine);

	if (use_cdr) {
		RES_SQLITE_BEGIN
			error = sqlite_exec_printf(db, "SELECT COUNT(id) FROM %Q;", NULL, NULL,
																 &errormsg, cdr_table);
		RES_SQLITE_END(error)

		if (error) {
			/*
			 * Unexpected error.
			 */
			if (error != SQLITE_ERROR) {
				ast_log(LOG_ERROR, "%s\n", errormsg);
				ast_free(errormsg);
				unload_module();
				return 1;
			}

			RES_SQLITE_BEGIN
				error = sqlite_exec_printf(db, sql_create_cdr_table, NULL, NULL,
								&errormsg, cdr_table);
			RES_SQLITE_END(error)

			if (error) {
				ast_log(LOG_ERROR, "%s\n", errormsg);
				ast_free(errormsg);
				unload_module();
				return 1;
			}
		}

		error = ast_cdr_register(RES_SQLITE_NAME, RES_SQLITE_DESCRIPTION,
														 cdr_handler);

		if (error) {
			unload_module();
			return 1;
		}

		cdr_registered = 1;
	}

	error = ast_cli_register(&cli_status_cmd);

	if (error) {
		unload_module();
		return 1;
	}

	cli_status_registered = 1;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Realtime SQLite configuration",
		.load = load_module,
		.unload = unload_module,
);
