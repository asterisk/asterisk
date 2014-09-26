/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004, Constantine Filin and Christos Ricudis
 *
 * Christos Ricudis <ricudis@itc.auth.gr>
 * Constantine Filin <cf@intermedia.net>
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
 * \file
 * \brief MYSQL dialplan application
 * \ingroup applications
 */

/*! \li \ref app_mysql.c uses the configuration file \ref app_mysql.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page app_mysql.conf app_mysql.conf
 * \verbinclude app_mysql.conf.sample
 */

/*** MODULEINFO
	<depend>mysqlclient</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>deprecated</support_level>
	<replacement>func_odbc</replacement>
 ***/

#include "asterisk.h"

#include <mysql/mysql.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/config.h"

#define EXTRA_LOG 0

enum { NULLSTRING, NULLVALUE, EMPTYSTRING } nullvalue = NULLSTRING;

static const char app[] = "MYSQL";

static const char synopsis[] = "Do several mySQLy things";

static const char descrip[] =
"MYSQL():  Do several mySQLy things\n"
"Syntax:\n"
"  MYSQL(Set timeout <num>)\n"
"    Set the connection timeout, in seconds.\n"
"  MYSQL(Connect connid dhhost[:dbport] dbuser dbpass dbname [dbcharset])\n"
"    Connects to a database.  Arguments contain standard MySQL parameters\n"
"    passed to function mysql_real_connect.  Optional parameter dbcharset\n"
"    defaults to 'latin1'.  Connection identifer returned in ${connid}\n"
"  MYSQL(Query resultid ${connid} query-string)\n"
"    Executes standard MySQL query contained in query-string using established\n"
"    connection identified by ${connid}. Result of query is stored in ${resultid}.\n"
"  MYSQL(Nextresult resultid ${connid}\n"
"    If last query returned more than one result set, it stores the next\n"
"    result set in ${resultid}. It's useful with stored procedures\n"
"  MYSQL(Fetch fetchid ${resultid} var1 var2 ... varN)\n"
"    Fetches a single row from a result set contained in ${result_identifier}.\n"
"    Assigns returned fields to ${var1} ... ${varn}.  ${fetchid} is set TRUE\n"
"    if additional rows exist in result set.\n"
"  MYSQL(Clear ${resultid})\n"
"    Frees memory and datastructures associated with result set.\n"
"  MYSQL(Disconnect ${connid})\n"
"    Disconnects from named connection to MySQL.\n"
"  On exit, always returns 0. Sets MYSQL_STATUS to 0 on success and -1 on error.\n";

/*
EXAMPLES OF USE :

exten => s,2,MYSQL(Connect connid localhost asterisk mypass credit utf8)
exten => s,3,MYSQL(Query resultid ${connid} SELECT username,credit FROM credit WHERE callerid=${CALLERIDNUM})
exten => s,4,MYSQL(Fetch fetchid ${resultid} datavar1 datavar2)
exten => s,5,GotoIf(${fetchid}?6:8)
exten => s,6,Festival("User ${datavar1} currently has credit balance of ${datavar2} dollars.")
exten => s,7,Goto(s,4)
exten => s,8,MYSQL(Clear ${resultid})
exten => s,9,MYSQL(Disconnect ${connid})
*/

AST_MUTEX_DEFINE_STATIC(_mysql_mutex);

#define MYSQL_CONFIG "app_mysql.conf"
#define MYSQL_CONFIG_OLD "mysql.conf"
#define AST_MYSQL_ID_DUMMY   0
#define AST_MYSQL_ID_CONNID  1
#define AST_MYSQL_ID_RESID   2
#define AST_MYSQL_ID_FETCHID 3

static int autoclear = 0;

static void mysql_ds_destroy(void *data);
static void mysql_ds_fixup(void *data, struct ast_channel *oldchan, struct ast_channel *newchan);

static const struct ast_datastore_info mysql_ds_info = {
	.type = "APP_ADDON_SQL_MYSQL",
	.destroy = mysql_ds_destroy,
	.chan_fixup = mysql_ds_fixup,
};

struct ast_MYSQL_id {
	struct ast_channel *owner;
	int identifier_type; /* 0=dummy, 1=connid, 2=resultid */
	int identifier;
	void *data;
	AST_LIST_ENTRY(ast_MYSQL_id) entries;
} *ast_MYSQL_id;

AST_LIST_HEAD(MYSQLidshead,ast_MYSQL_id) _mysql_ids_head;

static void mysql_ds_destroy(void *data)
{
	/* Destroy any IDs owned by the channel */
	struct ast_MYSQL_id *i;
	if (AST_LIST_LOCK(&_mysql_ids_head)) {
		ast_log(LOG_WARNING, "Unable to lock identifiers list\n");
	} else {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&_mysql_ids_head, i, entries) {
			if (i->owner == data) {
				AST_LIST_REMOVE_CURRENT(entries);
				if (i->identifier_type == AST_MYSQL_ID_CONNID) {
					/* Drop connection */
					mysql_close(i->data);
				} else if (i->identifier_type == AST_MYSQL_ID_RESID) {
					/* Drop result */
					mysql_free_result(i->data);
				}
				ast_free(i);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		AST_LIST_UNLOCK(&_mysql_ids_head);
	}
}

static void mysql_ds_fixup(void *data, struct ast_channel *oldchan, struct ast_channel *newchan)
{
	/* Destroy any IDs owned by the channel */
	struct ast_MYSQL_id *i;
	if (AST_LIST_LOCK(&_mysql_ids_head)) {
		ast_log(LOG_WARNING, "Unable to lock identifiers list\n");
	} else {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&_mysql_ids_head, i, entries) {
			if (i->owner == data) {
				AST_LIST_REMOVE_CURRENT(entries);
				if (i->identifier_type == AST_MYSQL_ID_CONNID) {
					/* Drop connection */
					mysql_close(i->data);
				} else if (i->identifier_type == AST_MYSQL_ID_RESID) {
					/* Drop result */
					mysql_free_result(i->data);
				}
				ast_free(i);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		AST_LIST_UNLOCK(&_mysql_ids_head);
	}
}

/* helpful procs */
static void *find_identifier(int identifier, int identifier_type)
{
	struct MYSQLidshead *headp = &_mysql_ids_head;
	struct ast_MYSQL_id *i;
	void *res=NULL;
	int found=0;

	if (AST_LIST_LOCK(headp)) {
		ast_log(LOG_WARNING, "Unable to lock identifiers list\n");
	} else {
		AST_LIST_TRAVERSE(headp, i, entries) {
			if ((i->identifier == identifier) && (i->identifier_type == identifier_type)) {
				found = 1;
				res = i->data;
				break;
			}
		}
		if (!found) {
			ast_log(LOG_WARNING, "Identifier %d, identifier_type %d not found in identifier list\n", identifier, identifier_type);
		}
		AST_LIST_UNLOCK(headp);
	}

	return res;
}

static int add_identifier(struct ast_channel *chan, int identifier_type, void *data)
{
	struct ast_MYSQL_id *i = NULL, *j = NULL;
	struct MYSQLidshead *headp = &_mysql_ids_head;
	int maxidentifier = 0;

	if (AST_LIST_LOCK(headp)) {
		ast_log(LOG_WARNING, "Unable to lock identifiers list\n");
		return -1;
	} else {
		i = ast_malloc(sizeof(*i));
		AST_LIST_TRAVERSE(headp, j, entries) {
			if (j->identifier > maxidentifier) {
				maxidentifier = j->identifier;
			}
		}
		i->identifier = maxidentifier + 1;
		i->identifier_type = identifier_type;
		i->data = data;
		i->owner = chan;
		AST_LIST_INSERT_HEAD(headp, i, entries);
		AST_LIST_UNLOCK(headp);
	}
	return i->identifier;
}

static int del_identifier(int identifier, int identifier_type)
{
	struct ast_MYSQL_id *i;
	struct MYSQLidshead *headp = &_mysql_ids_head;
	int found = 0;

	if (AST_LIST_LOCK(headp)) {
		ast_log(LOG_WARNING, "Unable to lock identifiers list\n");
	} else {
		AST_LIST_TRAVERSE(headp, i, entries) {
			if ((i->identifier == identifier) &&
			    (i->identifier_type == identifier_type)) {
				AST_LIST_REMOVE(headp, i, entries);
				ast_free(i);
				found = 1;
				break;
			}
		}
		AST_LIST_UNLOCK(headp);
	}

	if (found == 0) {
		ast_log(LOG_WARNING, "Could not find identifier %d, identifier_type %d in list to delete\n", identifier, identifier_type);
		return -1;
	} else {
		return 0;
	}
}

static int set_asterisk_int(struct ast_channel *chan, char *varname, int id)
{
	if (id >= 0) {
		char s[12] = "";
		snprintf(s, sizeof(s), "%d", id);
		ast_debug(5, "MYSQL: setting var '%s' to value '%s'\n", varname, s);
		pbx_builtin_setvar_helper(chan, varname, s);
	}
	return id;
}

static int add_identifier_and_set_asterisk_int(struct ast_channel *chan, char *varname, int identifier_type, void *data)
{
	return set_asterisk_int(chan, varname, add_identifier(chan, identifier_type, data));
}

static int safe_scan_int(char **data, char *delim, int def)
{
	char *end;
	int res = def;
	char *s = strsep(data, delim);
	if (s) {
		res = strtol(s, &end, 10);
		if (*end)
			res = def;  /* not an integer */
	}
	return res;
}

static int aMYSQL_set(struct ast_channel *chan, const char *data)
{
	char *var, *tmp, *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(set);
		AST_APP_ARG(variable);
		AST_APP_ARG(value);
	);

	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, ' ');

	if (args.argc == 3) {
		var = ast_alloca(6 + strlen(args.variable) + 1);
		sprintf(var, "MYSQL_%s", args.variable);

		/* Make the parameter case-insensitive */
		for (tmp = var + 6; *tmp; tmp++)
			*tmp = toupper(*tmp);

		pbx_builtin_setvar_helper(chan, var, args.value);
	}
	return 0;
}

/* MYSQL operations */
static int aMYSQL_connect(struct ast_channel *chan, const char *data)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(connect);
		AST_APP_ARG(connid);
		AST_APP_ARG(dbhost);
		AST_APP_ARG(dbuser);
		AST_APP_ARG(dbpass);
		AST_APP_ARG(dbname);
		AST_APP_ARG(dbcharset);
	);
	MYSQL *mysql;
	int timeout;
	const char *ctimeout;
	unsigned int port = 0;
	char *port_str;
	char *parse = ast_strdupa(data);
 
	AST_NONSTANDARD_APP_ARGS(args, parse, ' ');

	if (args.argc < 6) {
		ast_log(LOG_WARNING, "MYSQL_connect is missing some arguments\n");
		return -1;
	}

	if (!(mysql = mysql_init(NULL))) {
		ast_log(LOG_WARNING, "mysql_init returned NULL\n");
		return -1;
	}

	ctimeout = pbx_builtin_getvar_helper(chan, "MYSQL_TIMEOUT");
	if (ctimeout && sscanf(ctimeout, "%30d", &timeout) == 1) {
		mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, (void *)&timeout);
	}
	if(args.dbcharset && strlen(args.dbcharset) > 2){
		char set_names[255];
		char statement[512];
		snprintf(set_names, sizeof(set_names), "SET NAMES %s", args.dbcharset);
		mysql_real_escape_string(mysql, statement, set_names, sizeof(set_names));
		mysql_options(mysql, MYSQL_INIT_COMMAND, set_names);
		mysql_options(mysql, MYSQL_SET_CHARSET_NAME, args.dbcharset);
	}

	if ((port_str = strchr(args.dbhost, ':'))) {
		*port_str++ = '\0';
		if (sscanf(port_str, "%u", &port) != 1) {
			ast_log(LOG_WARNING, "Invalid port: '%s'\n", port_str);
			port = 0;
		}
	}

	if (!mysql_real_connect(mysql, args.dbhost, args.dbuser, args.dbpass, args.dbname, port, NULL,
#ifdef CLIENT_MULTI_STATEMENTS
			CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS
#elif defined(CLIENT_MULTI_QUERIES)
			CLIENT_MULTI_QUERIES
#else
			0
#endif
		)) {
		ast_log(LOG_WARNING, "mysql_real_connect(mysql,%s,%s,dbpass,%s,...) failed(%d): %s\n",
				args.dbhost, args.dbuser, args.dbname, mysql_errno(mysql), mysql_error(mysql));
		return -1;
	}

	add_identifier_and_set_asterisk_int(chan, args.connid, AST_MYSQL_ID_CONNID, mysql);
	return 0;
}

static int aMYSQL_query(struct ast_channel *chan, const char *data)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(query);
		AST_APP_ARG(resultid);
		AST_APP_ARG(connid);
		AST_APP_ARG(sql);
	);
	MYSQL       *mysql;
	MYSQL_RES   *mysqlres;
	int connid;
	int mysql_query_res;
	char *parse = ast_strdupa(data);

	AST_NONSTANDARD_APP_ARGS(args, parse, ' ');

	if (args.argc != 4 || (connid = atoi(args.connid)) == 0) {
		ast_log(LOG_WARNING, "missing some arguments\n");
		return -1;
	}

	if (!(mysql = find_identifier(connid, AST_MYSQL_ID_CONNID))) {
		ast_log(LOG_WARNING, "Invalid connection identifier %s passed in aMYSQL_query\n", args.connid);
		return -1;
	}

	if ((mysql_query_res = mysql_query(mysql, args.sql)) != 0) {
		ast_log(LOG_WARNING, "aMYSQL_query: mysql_query failed. Error: %s\n", mysql_error(mysql));
		return -1;
	}

	if ((mysqlres = mysql_store_result(mysql))) {
		add_identifier_and_set_asterisk_int(chan, args.resultid, AST_MYSQL_ID_RESID, mysqlres);
		return 0;
	} else if (!mysql_field_count(mysql)) {
		return 0;
	} else
		ast_log(LOG_WARNING, "mysql_store_result() failed on query %s\n", args.sql);

	return -1;
}

static int aMYSQL_nextresult(struct ast_channel *chan, const char *data)
{
	MYSQL       *mysql;
	MYSQL_RES   *mysqlres;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(nextresult);
		AST_APP_ARG(resultid);
		AST_APP_ARG(connid);
	);
	int connid = -1;
	char *parse = ast_strdupa(data);

	AST_NONSTANDARD_APP_ARGS(args, parse, ' ');
	sscanf(args.connid, "%30d", &connid);

	if (args.argc != 3 || connid <= 0) {
		ast_log(LOG_WARNING, "missing some arguments\n");
		return -1;
	}

	if (!(mysql = find_identifier(connid, AST_MYSQL_ID_CONNID))) {
		ast_log(LOG_WARNING, "Invalid connection identifier %d passed in aMYSQL_query\n", connid);
		return -1;
	}

	if (mysql_more_results(mysql)) {
		mysql_next_result(mysql);
		if ((mysqlres = mysql_store_result(mysql))) {
			add_identifier_and_set_asterisk_int(chan, args.resultid, AST_MYSQL_ID_RESID, mysqlres);
			return 0;
		} else if (!mysql_field_count(mysql)) {
			return 0;
		} else
			ast_log(LOG_WARNING, "mysql_store_result() failed on storing next_result\n");
	} else
		ast_log(LOG_WARNING, "mysql_more_results() result set has no more results\n");

	return 0;
}


static int aMYSQL_fetch(struct ast_channel *chan, const char *data)
{
	MYSQL_RES *mysqlres;
	MYSQL_ROW mysqlrow;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(fetch);
		AST_APP_ARG(resultvar);
		AST_APP_ARG(fetchid);
		AST_APP_ARG(vars);
	);
	char *s5, *parse;
	int resultid = -1, numFields, j;

	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, ' ');
	sscanf(args.fetchid, "%30d", &resultid);

	if (args.resultvar && (resultid >= 0) ) {
		if ((mysqlres = find_identifier(resultid, AST_MYSQL_ID_RESID)) != NULL) {
			/* Grab the next row */
			if ((mysqlrow = mysql_fetch_row(mysqlres)) != NULL) {
				numFields = mysql_num_fields(mysqlres);
				for (j = 0; j < numFields; j++) {
					s5 = strsep(&args.vars, " ");
					if (s5 == NULL) {
						ast_log(LOG_WARNING, "ast_MYSQL_fetch: More fields (%d) than variables (%d)\n", numFields, j);
						break;
					}

					pbx_builtin_setvar_helper(chan, s5, mysqlrow[j] ? mysqlrow[j] :
						nullvalue == NULLSTRING ? "NULL" :
						nullvalue == EMPTYSTRING ? "" :
						NULL);
				}
				ast_debug(5, "ast_MYSQL_fetch: numFields=%d\n", numFields);
				set_asterisk_int(chan, args.resultvar, 1); /* try more rows */
			} else {
				ast_debug(5, "ast_MYSQL_fetch : EOF\n");
				set_asterisk_int(chan, args.resultvar, 0); /* no more rows */
			}
			return 0;
		} else {
			set_asterisk_int(chan, args.resultvar, 0);
			ast_log(LOG_WARNING, "aMYSQL_fetch: Invalid result identifier %d passed\n", resultid);
		}
	} else {
		ast_log(LOG_WARNING, "aMYSQL_fetch: missing some arguments\n");
	}

	return -1;
}

static int aMYSQL_clear(struct ast_channel *chan, const char *data)
{
	MYSQL_RES *mysqlres;

	int id;
	char *parse = ast_strdupa(data);
	strsep(&parse, " "); /* eat the first token, we already know it :P */
	id = safe_scan_int(&parse, " \n", -1);
	if ((mysqlres = find_identifier(id, AST_MYSQL_ID_RESID)) == NULL) {
		ast_log(LOG_WARNING, "Invalid result identifier %d passed in aMYSQL_clear\n", id);
	} else {
		mysql_free_result(mysqlres);
		del_identifier(id, AST_MYSQL_ID_RESID);
	}

	return 0;
}

static int aMYSQL_disconnect(struct ast_channel *chan, const char *data)
{
	MYSQL *mysql;
	int id;
	char *parse = ast_strdupa(data);
	strsep(&parse, " "); /* eat the first token, we already know it :P */

	id = safe_scan_int(&parse, " \n", -1);
	if ((mysql = find_identifier(id, AST_MYSQL_ID_CONNID)) == NULL) {
		ast_log(LOG_WARNING, "Invalid connection identifier %d passed in aMYSQL_disconnect\n", id);
	} else {
		mysql_close(mysql);
		del_identifier(id, AST_MYSQL_ID_CONNID);
	}

	return 0;
}

static int MYSQL_exec(struct ast_channel *chan, const char *data)
{
	int result;
	char sresult[10];

	ast_debug(5, "MYSQL: data=%s\n", data);

	if (!data) {
		ast_log(LOG_WARNING, "MYSQL requires an argument (see manual)\n");
		return -1;
	}

	result = 0;

	if (autoclear) {
		struct ast_datastore *mysql_store = NULL;

		ast_channel_lock(chan);
		mysql_store = ast_channel_datastore_find(chan, &mysql_ds_info, NULL);
		if (!mysql_store) {
			if (!(mysql_store = ast_datastore_alloc(&mysql_ds_info, NULL))) {
				ast_log(LOG_WARNING, "Unable to allocate new datastore.\n");
			} else {
				mysql_store->data = chan;
				ast_channel_datastore_add(chan, mysql_store);
			}
		}
		ast_channel_unlock(chan);
	}
	ast_mutex_lock(&_mysql_mutex);

	if (strncasecmp("connect", data, strlen("connect")) == 0) {
		result = aMYSQL_connect(chan, data);
	} else if (strncasecmp("query", data, strlen("query")) == 0) {
		result = aMYSQL_query(chan, data);
	} else if (strncasecmp("nextresult", data, strlen("nextresult")) == 0) {
		result = aMYSQL_nextresult(chan, data);
	} else if (strncasecmp("fetch", data, strlen("fetch")) == 0) {
		result = aMYSQL_fetch(chan, data);
	} else if (strncasecmp("clear", data, strlen("clear")) == 0) {
		result = aMYSQL_clear(chan, data);
	} else if (strncasecmp("disconnect", data, strlen("disconnect")) == 0) {
		result = aMYSQL_disconnect(chan, data);
	} else if (strncasecmp("set", data, 3) == 0) {
		result = aMYSQL_set(chan, data);
	} else {
		ast_log(LOG_WARNING, "Unknown argument to MYSQL application : %s\n", data);
		result = -1;
	}

	ast_mutex_unlock(&_mysql_mutex);

	snprintf(sresult, sizeof(sresult), "%d", result);
	pbx_builtin_setvar_helper(chan, "MYSQL_STATUS", sresult);
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	struct MYSQLidshead *headp = &_mysql_ids_head;
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load(MYSQL_CONFIG, config_flags);
	const char *temp;

	if (!cfg) {
		/* Backwards compatibility ftw */
		cfg = ast_config_load(MYSQL_CONFIG_OLD, config_flags);
	}

	if (cfg) {
		if ((temp = ast_variable_retrieve(cfg, "general", "nullvalue"))) {
			if (!strcasecmp(temp, "nullstring")) {
				nullvalue = NULLSTRING;
			} else if (!strcasecmp(temp, "emptystring")) {
				nullvalue = EMPTYSTRING;
			} else if (!strcasecmp(temp, "null")) {
				nullvalue = NULLVALUE;
			} else {
				ast_log(LOG_WARNING, "Illegal value for 'nullvalue': '%s' (must be 'nullstring', 'null', or 'emptystring')\n", temp);
			}
		}
		if ((temp = ast_variable_retrieve(cfg, "general", "autoclear")) && ast_true(temp)) {
			autoclear = 1;
		}
		ast_config_destroy(cfg);
	}

	AST_LIST_HEAD_INIT(headp);
	return ast_register_application(app, MYSQL_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD_DEPRECATED(ASTERISK_GPL_KEY, "Simple Mysql Interface");

