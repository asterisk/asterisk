/*
 * Asterisk -- An open source telephony toolkit.
 *
 * James Sharp <jsharp@psychoses.org>
 *
 * Modified August 2003
 * Tilghman Lesher <asterisk__cdr__cdr_mysql__200308@the-tilghman.com>
 *
 * Modified August 6, 2005
 * Joseph Benden <joe@thrallingpenguin.com>
 * Added mysql connection timeout parameter
 * Added an automatic reconnect as to not lose a cdr record
 * Cleaned up the original code to match the coding guidelines
 *
 * Modified Juli 2006
 * Martin Portmann <map@infinitum.ch>
 * Added mysql ssl support
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
 * \brief MySQL CDR backend
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>mysqlclient</depend>
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/strings.h"
#include "asterisk/linkedlists.h"
#include "asterisk/threadstorage.h"

#define DATE_FORMAT "%Y-%m-%d %T"

AST_THREADSTORAGE(sql1_buf);
AST_THREADSTORAGE(sql2_buf);
AST_THREADSTORAGE(escape_buf);

static const char desc[] = "MySQL CDR Backend";
static const char name[] = "mysql";
static const char config[] = "cdr_mysql.conf";

static struct ast_str *hostname = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL, *dbsock = NULL, *dbtable = NULL, *dbcharset = NULL;

static struct ast_str *ssl_ca = NULL, *ssl_cert = NULL, *ssl_key = NULL;

static int dbport = 0;
static int connected = 0;
static time_t connect_time = 0;
static int records = 0;
static int totalrecords = 0;
static int timeout = 0;
static int calldate_compat = 0;

AST_MUTEX_DEFINE_STATIC(mysql_lock);

struct unload_string {
	AST_LIST_ENTRY(unload_string) entry;
	struct ast_str *str;
};

static AST_LIST_HEAD_STATIC(unload_strings, unload_string);

struct column {
	char *name;
	char *cdrname;
	char *staticvalue;
	char *type;
	AST_LIST_ENTRY(column) list;
};

/* Protected with mysql_lock */
static AST_RWLIST_HEAD_STATIC(columns, column);

static MYSQL mysql = { { NULL }, };

static char *handle_cli_cdr_mysql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "cdr mysql status";
		e->usage =
			"Usage: cdr mysql status\n"
			"       Shows current connection status for cdr_mysql\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	if (connected) {
		char status[256], status2[100] = "";
		int ctime = time(NULL) - connect_time;
		if (dbport)
			snprintf(status, 255, "Connected to %s@%s, port %d", ast_str_buffer(dbname), ast_str_buffer(hostname), dbport);
		else if (dbsock)
			snprintf(status, 255, "Connected to %s on socket file %s", ast_str_buffer(dbname), S_OR(ast_str_buffer(dbsock), "default"));
		else
			snprintf(status, 255, "Connected to %s@%s", ast_str_buffer(dbname), ast_str_buffer(hostname));

		if (!ast_strlen_zero(ast_str_buffer(dbuser)))
			snprintf(status2, 99, " with username %s", ast_str_buffer(dbuser));
		if (ast_str_strlen(dbtable))
			snprintf(status2, 99, " using table %s", ast_str_buffer(dbtable));
		if (ctime > 31536000) {
			ast_cli(a->fd, "%s%s for %d years, %d days, %d hours, %d minutes, %d seconds.\n", status, status2, ctime / 31536000, (ctime % 31536000) / 86400, (ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
		} else if (ctime > 86400) {
			ast_cli(a->fd, "%s%s for %d days, %d hours, %d minutes, %d seconds.\n", status, status2, ctime / 86400, (ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
		} else if (ctime > 3600) {
			ast_cli(a->fd, "%s%s for %d hours, %d minutes, %d seconds.\n", status, status2, ctime / 3600, (ctime % 3600) / 60, ctime % 60);
		} else if (ctime > 60) {
			ast_cli(a->fd, "%s%s for %d minutes, %d seconds.\n", status, status2, ctime / 60, ctime % 60);
		} else {
			ast_cli(a->fd, "%s%s for %d seconds.\n", status, status2, ctime);
		}
		if (records == totalrecords)
			ast_cli(a->fd, "  Wrote %d records since last restart.\n", totalrecords);
		else
			ast_cli(a->fd, "  Wrote %d records since last restart and %d records since last reconnect.\n", totalrecords, records);
	} else {
		ast_cli(a->fd, "Not currently connected to a MySQL server.\n");
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cdr_mysql_status_cli[] = {
	AST_CLI_DEFINE(handle_cli_cdr_mysql_status, "Show connection status of cdr_mysql"),
};

static int mysql_log(struct ast_cdr *cdr)
{
	struct ast_str *sql1 = ast_str_thread_get(&sql1_buf, 1024), *sql2 = ast_str_thread_get(&sql2_buf, 1024);
	int retries = 5;
#if MYSQL_VERSION_ID >= 50013
	my_bool my_bool_true = 1;
#endif

	if (!sql1 || !sql2) {
		ast_log(LOG_ERROR, "Memory error\n");
		return -1;
	}

	ast_mutex_lock(&mysql_lock);

db_reconnect:
	if ((!connected) && (hostname || dbsock) && dbuser && password && dbname && dbtable ) {
		/* Attempt to connect */
		mysql_init(&mysql);
		/* Add option to quickly timeout the connection */
		if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout) != 0) {
			ast_log(LOG_ERROR, "mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
		}
#if MYSQL_VERSION_ID >= 50013
		/* Add option for automatic reconnection */
		if (mysql_options(&mysql, MYSQL_OPT_RECONNECT, &my_bool_true) != 0) {
			ast_log(LOG_ERROR, "mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
		}
#endif
		if (ssl_ca || ssl_cert || ssl_key) {
			mysql_ssl_set(&mysql, ssl_key ? ast_str_buffer(ssl_key) : NULL, ssl_cert ? ast_str_buffer(ssl_cert) : NULL, ssl_ca ? ast_str_buffer(ssl_ca) : NULL, NULL, NULL);
		}
		if (mysql_real_connect(&mysql, ast_str_buffer(hostname), ast_str_buffer(dbuser), ast_str_buffer(password), ast_str_buffer(dbname), dbport, dbsock && ast_str_strlen(dbsock) ? ast_str_buffer(dbsock) : NULL, ssl_ca ? CLIENT_SSL : 0)) {
			connected = 1;
			connect_time = time(NULL);
			records = 0;
			if (dbcharset) {
				ast_str_set(&sql1, 0, "SET NAMES '%s'", ast_str_buffer(dbcharset));
				mysql_real_query(&mysql, ast_str_buffer(sql1), ast_str_strlen(sql1));
				ast_debug(1, "SQL command as follows: %s\n", ast_str_buffer(sql1));
			}
		} else {
			ast_log(LOG_ERROR, "Cannot connect to database server %s: (%d) %s\n", ast_str_buffer(hostname), mysql_errno(&mysql), mysql_error(&mysql));
			connected = 0;
		}
	} else {
		/* Long connection - ping the server */
		int error;
		if ((error = mysql_ping(&mysql))) {
			connected = 0;
			records = 0;
			switch (mysql_errno(&mysql)) {
				case CR_SERVER_GONE_ERROR:
				case CR_SERVER_LOST:
					ast_log(LOG_ERROR, "Server has gone away. Attempting to reconnect.\n");
					break;
				default:
					ast_log(LOG_ERROR, "Unknown connection error: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
			}
			retries--;
			if (retries) {
				goto db_reconnect;
			} else {
				ast_log(LOG_ERROR, "Retried to connect five times, giving up.\n");
			}
		}
	}

	if (connected) {
		int column_count = 0;
		char *cdrname;
		char workspace[2048], *value = NULL;
		struct column *entry;
		struct ast_str *escape = ast_str_thread_get(&escape_buf, 16);

		ast_str_set(&sql1, 0, "INSERT INTO %s (", AS_OR(dbtable, "cdr"));
		ast_str_set(&sql2, 0, ") VALUES (");

		AST_RWLIST_RDLOCK(&columns);
		AST_RWLIST_TRAVERSE(&columns, entry, list) {
			if (!strcmp(entry->name, "calldate")) {
				/*!\note
				 * For some dumb reason, "calldate" used to be formulated using
				 * the datetime the record was posted, rather than the start
				 * time of the call.  If someone really wants the old compatible
				 * behavior, it's provided here.
				 */
				if (calldate_compat) {
					struct timeval tv = ast_tvnow();
					struct ast_tm tm;
					char timestr[128];
					ast_localtime(&tv, &tm, NULL);
					ast_strftime(timestr, sizeof(timestr), "%Y-%m-%d %T", &tm);
					ast_cdr_setvar(cdr, "calldate", timestr, 0);
					cdrname = "calldate";
				} else {
					cdrname = "start";
				}
			} else {
				cdrname = entry->cdrname;
			}

			/* Construct SQL */

			/* Need the type and value to determine if we want the raw value or not */
			if (entry->staticvalue) {
				value = ast_strdupa(entry->staticvalue);
			} else if ((!strcmp(cdrname, "start") ||
				 !strcmp(cdrname, "answer") ||
				 !strcmp(cdrname, "end") ||
				 !strcmp(cdrname, "disposition") ||
				 !strcmp(cdrname, "amaflags")) &&
				(strstr(entry->type, "int") ||
				 strstr(entry->type, "dec") ||
				 strstr(entry->type, "float") ||
				 strstr(entry->type, "double") ||
				 strstr(entry->type, "real") ||
				 strstr(entry->type, "numeric") ||
				 strstr(entry->type, "fixed"))) {
				ast_cdr_getvar(cdr, cdrname, &value, workspace, sizeof(workspace), 0, 1);
			} else {
				ast_cdr_getvar(cdr, cdrname, &value, workspace, sizeof(workspace), 0, 0);
			}

			if (value) {
				size_t valsz;

				if (column_count++) {
					ast_str_append(&sql1, 0, ",");
					ast_str_append(&sql2, 0, ",");
				}

				ast_str_make_space(&escape, (valsz = strlen(value)) * 2 + 1);
				mysql_real_escape_string(&mysql, ast_str_buffer(escape), value, valsz);

				ast_str_append(&sql1, 0, "%s", entry->name);
				ast_str_append(&sql2, 0, "'%s'", ast_str_buffer(escape));
			}
		}
		AST_RWLIST_UNLOCK(&columns);

		ast_debug(1, "Inserting a CDR record.\n");
		ast_str_append(&sql1, 0, "%s)", ast_str_buffer(sql2));

		ast_debug(1, "SQL command as follows: %s\n", ast_str_buffer(sql1));

		if (mysql_real_query(&mysql, ast_str_buffer(sql1), ast_str_strlen(sql1))) {
			ast_log(LOG_ERROR, "Failed to insert into database: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
			mysql_close(&mysql);
			connected = 0;
		} else {
			records++;
			totalrecords++;
		}
	}
	ast_mutex_unlock(&mysql_lock);
	return 0;
}

static int my_unload_module(int reload)
{ 
	struct unload_string *us;
	struct column *entry;

	ast_cli_unregister_multiple(cdr_mysql_status_cli, sizeof(cdr_mysql_status_cli) / sizeof(struct ast_cli_entry));

	if (connected) {
		mysql_close(&mysql);
		connected = 0;
		records = 0;
	}

	AST_LIST_LOCK(&unload_strings);
	while ((us = AST_LIST_REMOVE_HEAD(&unload_strings, entry))) {
		ast_free(us->str);
		ast_free(us);
	}
	AST_LIST_UNLOCK(&unload_strings);

	if (!reload) {
		AST_RWLIST_WRLOCK(&columns);
	}
	while ((entry = AST_RWLIST_REMOVE_HEAD(&columns, list))) {
		ast_free(entry);
	}
	if (!reload) {
		AST_RWLIST_UNLOCK(&columns);
	}

	dbport = 0;
	ast_cdr_unregister(name);
	
	return 0;
}

static int my_load_config_string(struct ast_config *cfg, const char *category, const char *variable, struct ast_str **field, const char *def)
{
	struct unload_string *us;
	const char *tmp;

	if (!(us = ast_calloc(1, sizeof(*us))))
		return -1;

	if (!(*field = ast_str_create(16))) {
		ast_free(us);
		return -1;
	}

	us->str = *field;

	AST_LIST_LOCK(&unload_strings);
	AST_LIST_INSERT_HEAD(&unload_strings, us, entry);
	AST_LIST_UNLOCK(&unload_strings);

	tmp = ast_variable_retrieve(cfg, category, variable);

	ast_str_set(field, 0, "%s", tmp ? tmp : def);

	return 0;
}

static int my_load_config_number(struct ast_config *cfg, const char *category, const char *variable, int *field, int def)
{
	const char *tmp;

	tmp = ast_variable_retrieve(cfg, category, variable);

	if (!tmp || sscanf(tmp, "%d", field) < 1)
		*field = def;

	return 0;
}

static int my_load_module(int reload)
{
	int res;
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct column *entry;
	char *temp;
	struct ast_str *compat;
	MYSQL_ROW row;
	MYSQL_RES *result;
	char sqldesc[128];
#if MYSQL_VERSION_ID >= 50013
	my_bool my_bool_true = 1;
#endif

	cfg = ast_config_load(config, config_flags);
	if (!cfg) {
		ast_log(LOG_WARNING, "Unable to load config for mysql CDR's: %s\n", config);
		return AST_MODULE_LOAD_SUCCESS;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return AST_MODULE_LOAD_SUCCESS;

	if (reload) {
		AST_RWLIST_WRLOCK(&columns);
		my_unload_module(1);
	}

	var = ast_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		if (reload) {
			AST_RWLIST_UNLOCK(&columns);
		}
		return AST_MODULE_LOAD_SUCCESS;
	}

	res = 0;

	res |= my_load_config_string(cfg, "global", "hostname", &hostname, "localhost");
	res |= my_load_config_string(cfg, "global", "dbname", &dbname, "astriskcdrdb");
	res |= my_load_config_string(cfg, "global", "user", &dbuser, "root");
	res |= my_load_config_string(cfg, "global", "sock", &dbsock, "");
	res |= my_load_config_string(cfg, "global", "table", &dbtable, "cdr");
	res |= my_load_config_string(cfg, "global", "password", &password, "");

	res |= my_load_config_string(cfg, "global", "charset", &dbcharset, "");

	res |= my_load_config_string(cfg, "global", "ssl_ca", &ssl_ca, "");
	res |= my_load_config_string(cfg, "global", "ssl_cert", &ssl_cert, "");
	res |= my_load_config_string(cfg, "global", "ssl_key", &ssl_key, "");

	res |= my_load_config_number(cfg, "global", "port", &dbport, 0);
	res |= my_load_config_number(cfg, "global", "timeout", &timeout, 0);
	res |= my_load_config_string(cfg, "global", "compat", &compat, "no");
	if (ast_true(ast_str_buffer(compat))) {
		calldate_compat = 1;
	} else {
		calldate_compat = 0;
	}

	if (res < 0) {
		if (reload) {
			AST_RWLIST_UNLOCK(&columns);
		}
		return AST_MODULE_LOAD_FAILURE;
	}

	/* Check for any aliases */
	if (!reload) {
		/* Lock, if not already */
		AST_RWLIST_WRLOCK(&columns);
	}
	while ((entry = AST_LIST_REMOVE_HEAD(&columns, list))) {
		ast_free(entry);
	}

	ast_debug(1, "Got hostname of %s\n", ast_str_buffer(hostname));
	ast_debug(1, "Got port of %d\n", dbport);
	ast_debug(1, "Got a timeout of %d\n", timeout);
	if (dbsock)
		ast_debug(1, "Got sock file of %s\n", ast_str_buffer(dbsock));
	ast_debug(1, "Got user of %s\n", ast_str_buffer(dbuser));
	ast_debug(1, "Got dbname of %s\n", ast_str_buffer(dbname));
	ast_debug(1, "Got password of %s\n", ast_str_buffer(password));
	ast_debug(1, "%sunning in calldate compatibility mode\n", calldate_compat ? "R" : "Not r");

	if (dbcharset) {
		ast_debug(1, "Got DB charset of %s\n", ast_str_buffer(dbcharset));
	}

	mysql_init(&mysql);

	if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout) != 0) {
		ast_log(LOG_ERROR, "cdr_mysql: mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
	}

#if MYSQL_VERSION_ID >= 50013
	/* Add option for automatic reconnection */
	if (mysql_options(&mysql, MYSQL_OPT_RECONNECT, &my_bool_true) != 0) {
		ast_log(LOG_ERROR, "cdr_mysql: mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
	}
#endif

	if ((ssl_ca && ast_str_strlen(ssl_ca)) || (ssl_cert && ast_str_strlen(ssl_cert)) || (ssl_key && ast_str_strlen(ssl_key))) {
		mysql_ssl_set(&mysql,
			ssl_key ? ast_str_buffer(ssl_key) : NULL,
			ssl_cert ? ast_str_buffer(ssl_cert) : NULL,
			ssl_ca ? ast_str_buffer(ssl_ca) : NULL,
			NULL, NULL);
	}
	temp = dbsock && ast_str_strlen(dbsock) ? ast_str_buffer(dbsock) : NULL;
	if (!mysql_real_connect(&mysql, ast_str_buffer(hostname), ast_str_buffer(dbuser), ast_str_buffer(password), ast_str_buffer(dbname), dbport, temp, ssl_ca && ast_str_strlen(ssl_ca) ? CLIENT_SSL : 0)) {
		ast_log(LOG_ERROR, "Failed to connect to mysql database %s on %s.\n", ast_str_buffer(dbname), ast_str_buffer(hostname));
		connected = 0;
		records = 0;
	} else {
		ast_debug(1, "Successfully connected to MySQL database.\n");
		connected = 1;
		records = 0;
		connect_time = time(NULL);
		if (dbcharset) {
			snprintf(sqldesc, sizeof(sqldesc), "SET NAMES '%s'", ast_str_buffer(dbcharset));
			mysql_real_query(&mysql, sqldesc, strlen(sqldesc));
			ast_debug(1, "SQL command as follows: %s\n", sqldesc);
		}

		/* Get table description */
		snprintf(sqldesc, sizeof(sqldesc), "DESC %s", dbtable ? ast_str_buffer(dbtable) : "cdr");
		if (mysql_query(&mysql, sqldesc)) {
			ast_log(LOG_ERROR, "Unable to query table description!!  Logging disabled.\n");
			mysql_close(&mysql);
			connected = 0;
			AST_RWLIST_UNLOCK(&columns);
			ast_config_destroy(cfg);
			return AST_MODULE_LOAD_SUCCESS;
		}

		if (!(result = mysql_store_result(&mysql))) {
			ast_log(LOG_ERROR, "Unable to query table description!!  Logging disabled.\n");
			mysql_close(&mysql);
			connected = 0;
			AST_RWLIST_UNLOCK(&columns);
			ast_config_destroy(cfg);
			return AST_MODULE_LOAD_SUCCESS;
		}

		while ((row = mysql_fetch_row(result))) {
			struct column *entry;
			char *cdrvar = "", *staticvalue = "";

			ast_debug(1, "Got a field '%s' of type '%s'\n", row[0], row[1]);
			/* Check for an alias or a static value */
			for (var = ast_variable_browse(cfg, "columns"); var; var = var->next) {
				if (strncmp(var->name, "alias", 5) == 0 && strcasecmp(var->value, row[0]) == 0 ) {
					char *alias = ast_strdupa(var->name + 5);
					cdrvar = ast_strip(alias);
					ast_verb(3, "Found alias %s for column %s\n", cdrvar, row[0]);
					break;
				} else if (strncmp(var->name, "static", 6) == 0 && strcasecmp(var->value, row[0]) == 0) {
					char *item = ast_strdupa(var->name + 6);
					item = ast_strip(item);
					if (item[0] == '"' && item[strlen(item) - 1] == '"') {
						/* Remove surrounding quotes */
						item[strlen(item) - 1] = '\0';
						item++;
					}
					staticvalue = item;
				}
			}

			entry = ast_calloc(sizeof(char), sizeof(*entry) + strlen(row[0]) + 1 + strlen(cdrvar) + 1 + strlen(staticvalue) + 1 + strlen(row[1]) + 1);
			if (!entry) {
				ast_log(LOG_ERROR, "Out of memory creating entry for column '%s'\n", row[0]);
				res = -1;
				break;
			}

			entry->name = (char *)entry + sizeof(*entry);
			strcpy(entry->name, row[0]);

			if (!ast_strlen_zero(cdrvar)) {
				entry->cdrname = entry->name + strlen(row[0]) + 1;
				strcpy(entry->cdrname, cdrvar);
			} else { /* Point to same place as the column name */
				entry->cdrname = (char *)entry + sizeof(*entry);
			}

			if (!ast_strlen_zero(staticvalue)) {
				entry->staticvalue = entry->cdrname + strlen(entry->cdrname) + 1;
				strcpy(entry->staticvalue, staticvalue);
				ast_debug(1, "staticvalue length: %d\n", (int) strlen(staticvalue) );
				entry->type = entry->staticvalue + strlen(entry->staticvalue) + 1;
			} else {
				entry->type = entry->cdrname + strlen(entry->cdrname) + 1;
			}
			strcpy(entry->type, row[1]);

			ast_debug(1, "Entry name '%s'\n", entry->name);
			ast_debug(1, "   cdrname '%s'\n", entry->cdrname);
			ast_debug(1, "    static '%s'\n", entry->staticvalue);
			ast_debug(1, "      type '%s'\n", entry->type);

			AST_LIST_INSERT_TAIL(&columns, entry, list);
		}
		mysql_free_result(result);
	}
	AST_RWLIST_UNLOCK(&columns);
	ast_config_destroy(cfg);
	if (res < 0) {
		return AST_MODULE_LOAD_FAILURE;
	}

	res = ast_cdr_register(name, desc, mysql_log);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register MySQL CDR handling\n");
	} else {
		res = ast_cli_register_multiple(cdr_mysql_status_cli, sizeof(cdr_mysql_status_cli) / sizeof(struct ast_cli_entry));
	}

	return res;
}

static int load_module(void)
{
	return my_load_module(0);
}

static int unload_module(void)
{
	return my_unload_module(0);
}

static int reload(void)
{
	int ret;

	ast_mutex_lock(&mysql_lock);
	ret = my_load_module(1);
	ast_mutex_unlock(&mysql_lock);

	return ret;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MySQL CDR Backend",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);

