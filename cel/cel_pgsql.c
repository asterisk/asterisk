/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008
 *
 * Steve Murphy - adapted to CEL, from:
 * Matthew D. Hardeman <mhardemn@papersoft.com>
 * Adapted from the MySQL CDR logger originally by James Sharp
 *
 * Modified April, 2007; Dec, 2008
 * Steve Murphy <murf@digium.com>

 * Modified September 2003
 * Matthew D. Hardeman <mhardemn@papersoft.com>
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
 * \brief PostgreSQL CEL logger
 *
 * \author Steve Murphy <murf@digium.com>
 * PostgreSQL http://www.postgresql.org/
 *
 * See also
 * \arg \ref Config_cel
 * PostgreSQL http://www.postgresql.org/
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<depend>pgsql</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <libpq-fe.h>

#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk.h"

#define DATE_FORMAT "%Y-%m-%d %T.%6q"

#define PGSQL_BACKEND_NAME "CEL PGSQL backend"

#define PGSQL_MIN_VERSION_SCHEMA 70300

static char *config = "cel_pgsql.conf";

static char *pghostname;
static char *pgdbname;
static char *pgdbuser;
static char *pgpassword;
static char *pgappname;
static char *pgdbport;
static char *table;
static char *schema;

static int connected = 0;
static int maxsize = 512, maxsize2 = 512;
static int usegmtime = 0;

/*! \brief show_user_def is off by default */
#define CEL_SHOW_USERDEF_DEFAULT	0

/*! TRUE if we should set the eventtype field to USER_DEFINED on user events. */
static unsigned char cel_show_user_def;

AST_MUTEX_DEFINE_STATIC(pgsql_lock);

static PGconn	*conn = NULL;
static PGresult	*result = NULL;

struct columns {
	char *name;
	char *type;
	int len;
	unsigned int notnull:1;
	unsigned int hasdefault:1;
	AST_RWLIST_ENTRY(columns) list;
};

static AST_RWLIST_HEAD_STATIC(psql_columns, columns);

#define LENGTHEN_BUF1(size) \
	do { \
		/* Lengthen buffer, if necessary */ \
		if (ast_str_strlen(sql) + size + 1 > ast_str_size(sql)) { \
			if (ast_str_make_space(&sql, ((ast_str_size(sql) + size + 3) / 512 + 1) * 512) != 0) { \
				ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CEL '%s:%s' failed.\n", pghostname, table); \
				ast_free(sql); \
				ast_free(sql2); \
				AST_RWLIST_UNLOCK(&psql_columns); \
				return; \
			} \
		} \
	} while (0)

#define LENGTHEN_BUF2(size) \
	do { \
		if (ast_str_strlen(sql2) + size + 1 > ast_str_size(sql2)) { \
			if (ast_str_make_space(&sql2, ((ast_str_size(sql2) + size + 3) / 512 + 1) * 512) != 0) { \
				ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CEL '%s:%s' failed.\n", pghostname, table); \
				ast_free(sql); \
				ast_free(sql2); \
				AST_RWLIST_UNLOCK(&psql_columns); \
				return; \
			} \
		} \
	} while (0)

static void pgsql_reconnect(void)
{
	struct ast_str *conn_info = ast_str_create(128);
	if (!conn_info) {
		ast_log(LOG_ERROR, "Failed to allocate memory for connection string.\n");
		return;
	}

	if (conn) {
		PQfinish(conn);
		conn = NULL;
	}

	ast_str_set(&conn_info, 0, "host=%s port=%s dbname=%s user=%s",
		pghostname, pgdbport, pgdbname, pgdbuser);

	if (!ast_strlen_zero(pgappname)) {
		ast_str_append(&conn_info, 0, " application_name=%s", pgappname);
	}

	if (!ast_strlen_zero(pgpassword)) {
		ast_str_append(&conn_info, 0, " password=%s", pgpassword);
	}

	conn = PQconnectdb(ast_str_buffer(conn_info));
	ast_free(conn_info);
}


static void pgsql_log(struct ast_event *event)
{
	struct ast_tm tm;
	char timestr[128];
	char *pgerror;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_mutex_lock(&pgsql_lock);

	ast_localtime(&record.event_time, &tm, usegmtime ? "GMT" : NULL);
	ast_strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

	if ((!connected) && pghostname && pgdbuser && pgpassword && pgdbname) {
		pgsql_reconnect();
		if (PQstatus(conn) != CONNECTION_BAD) {
			connected = 1;
		} else {
			pgerror = PQerrorMessage(conn);
			ast_log(LOG_ERROR, "cel_pgsql: Unable to connect to database server %s.  Calls will not be logged!\n", pghostname);
			ast_log(LOG_ERROR, "cel_pgsql: Reason: %s\n", pgerror);
			PQfinish(conn);
			conn = NULL;
		}
	}
	if (connected) {
		struct columns *cur;
		struct ast_str *sql = ast_str_create(maxsize), *sql2 = ast_str_create(maxsize2);
		char buf[257], escapebuf[513];
		const char *value;
		int first = 1;

		if (!sql || !sql2) {
			goto ast_log_cleanup;
		}

		ast_str_set(&sql, 0, "INSERT INTO %s (", table);
		ast_str_set(&sql2, 0, " VALUES (");

#define SEP (first ? "" : ",")

		AST_RWLIST_RDLOCK(&psql_columns);
		AST_RWLIST_TRAVERSE(&psql_columns, cur, list) {
			LENGTHEN_BUF1(strlen(cur->name) + 2);
			ast_str_append(&sql, 0, "%s\"%s\"", first ? "" : ",", cur->name);

			if (strcmp(cur->name, "eventtime") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", SEP, (long) record.event_time.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f",
						SEP,
						(double) record.event_time.tv_sec +
						(double) record.event_time.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&record.event_time, &tm, usegmtime ? "GMT" : NULL);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s'%s'", SEP, buf);
				}
			} else if (strcmp(cur->name, "eventtype") == 0) {
				if (cur->type[0] == 'i') {
					/* Get integer, no need to escape anything */
					LENGTHEN_BUF2(5);
					ast_str_append(&sql2, 0, "%s%d", SEP, (int) record.event_type);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", SEP, (double) record.event_type);
				} else {
					/* Char field, probably */
					const char *event_name;

					event_name = (!cel_show_user_def
						&& record.event_type == AST_CEL_USER_DEFINED)
						? record.user_defined_name : record.event_name;
					LENGTHEN_BUF2(strlen(event_name) + 1);
					ast_str_append(&sql2, 0, "%s'%s'", SEP, event_name);
				}
			} else if (strcmp(cur->name, "amaflags") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					/* Integer, no need to escape anything */
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%u", SEP, record.amaflag);
				} else {
					/* Although this is a char field, there are no special characters in the values for these fields */
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s'%u'", SEP, record.amaflag);
				}
			} else {
				/* Arbitrary field, could be anything */
				if (strcmp(cur->name, "userdeftype") == 0) {
					value = record.user_defined_name;
				} else if (strcmp(cur->name, "cid_name") == 0) {
					value = record.caller_id_name;
				} else if (strcmp(cur->name, "cid_num") == 0) {
					value = record.caller_id_num;
				} else if (strcmp(cur->name, "cid_ani") == 0) {
					value = record.caller_id_ani;
				} else if (strcmp(cur->name, "cid_rdnis") == 0) {
					value = record.caller_id_rdnis;
				} else if (strcmp(cur->name, "cid_dnid") == 0) {
					value = record.caller_id_dnid;
				} else if (strcmp(cur->name, "exten") == 0) {
					value = record.extension;
				} else if (strcmp(cur->name, "context") == 0) {
					value = record.context;
				} else if (strcmp(cur->name, "channame") == 0) {
					value = record.channel_name;
				} else if (strcmp(cur->name, "appname") == 0) {
					value = record.application_name;
				} else if (strcmp(cur->name, "appdata") == 0) {
					value = record.application_data;
				} else if (strcmp(cur->name, "accountcode") == 0) {
					value = record.account_code;
				} else if (strcmp(cur->name, "peeraccount") == 0) {
					value = record.peer_account;
				} else if (strcmp(cur->name, "uniqueid") == 0) {
					value = record.unique_id;
				} else if (strcmp(cur->name, "linkedid") == 0) {
					value = record.linked_id;
				} else if (strcmp(cur->name, "userfield") == 0) {
					value = record.user_field;
				} else if (strcmp(cur->name, "peer") == 0) {
					value = record.peer;
				} else if (strcmp(cur->name, "extra") == 0) {
					value = record.extra;
				} else {
					value = NULL;
				}

				if (value == NULL) {
					ast_str_append(&sql2, 0, "%sDEFAULT", SEP);
				} else if (strncmp(cur->type, "int", 3) == 0) {
					long long whatever;
					if (value && sscanf(value, "%30lld", &whatever) == 1) {
						LENGTHEN_BUF2(26);
						ast_str_append(&sql2, 0, "%s%lld", SEP, whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", SEP);
					}
				} else if (strncmp(cur->type, "float", 5) == 0) {
					long double whatever;
					if (value && sscanf(value, "%30Lf", &whatever) == 1) {
						LENGTHEN_BUF2(51);
						ast_str_append(&sql2, 0, "%s%30Lf", SEP, whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", SEP);
					}
					/* XXX Might want to handle dates, times, and other misc fields here XXX */
				} else {
					if (value) {
						PQescapeStringConn(conn, escapebuf, value, strlen(value), NULL);
					} else {
						escapebuf[0] = '\0';
					}
					LENGTHEN_BUF2(strlen(escapebuf) + 3);
					ast_str_append(&sql2, 0, "%s'%s'", SEP, escapebuf);
				}
			}
			first = 0;
		}
		AST_RWLIST_UNLOCK(&psql_columns);
		LENGTHEN_BUF1(ast_str_strlen(sql2) + 2);
		ast_str_append(&sql, 0, ")%s)", ast_str_buffer(sql2));

		ast_debug(3, "Inserting a CEL record: [%s].\n", ast_str_buffer(sql));
		/* Test to be sure we're still connected... */
		/* If we're connected, and connection is working, good. */
		/* Otherwise, attempt reconnect.  If it fails... sorry... */
		if (PQstatus(conn) == CONNECTION_OK) {
			connected = 1;
		} else {
			ast_log(LOG_WARNING, "Connection was lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_NOTICE, "Connection reestablished.\n");
				connected = 1;
			} else {
				pgerror = PQerrorMessage(conn);
				ast_log(LOG_ERROR, "Unable to reconnect to database server %s. Calls will not be logged!\n", pghostname);
				ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
				PQfinish(conn);
				conn = NULL;
				connected = 0;
				goto ast_log_cleanup;
			}
		}
		result = PQexec(conn, ast_str_buffer(sql));
		if (PQresultStatus(result) != PGRES_COMMAND_OK) {
			pgerror = PQresultErrorMessage(result);
			ast_log(LOG_WARNING, "Failed to insert call detail record into database!\n");
			ast_log(LOG_WARNING, "Reason: %s\n", pgerror);
			ast_log(LOG_WARNING, "Connection may have been lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_NOTICE, "Connection reestablished.\n");
				connected = 1;
				PQclear(result);
				result = PQexec(conn, ast_str_buffer(sql));
				if (PQresultStatus(result) != PGRES_COMMAND_OK) {
					pgerror = PQresultErrorMessage(result);
					ast_log(LOG_ERROR, "HARD ERROR!  Attempted reconnection failed.  DROPPING CALL RECORD!\n");
					ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
				}
			}
			PQclear(result);
			goto ast_log_cleanup;
		}
		PQclear(result);

ast_log_cleanup:
		ast_free(sql);
		ast_free(sql2);
	}

	ast_mutex_unlock(&pgsql_lock);
}

static int my_unload_module(void)
{
	struct columns *current;

	ast_cel_backend_unregister(PGSQL_BACKEND_NAME);
	AST_RWLIST_WRLOCK(&psql_columns);
	if (conn) {
		PQfinish(conn);
		conn = NULL;
	}
	if (pghostname) {
		ast_free(pghostname);
		pghostname = NULL;
	}
	if (pgdbname) {
		ast_free(pgdbname);
		pgdbname = NULL;
	}
	if (pgdbuser) {
		ast_free(pgdbuser);
		pgdbuser = NULL;
	}
	if (pgpassword) {
		ast_free(pgpassword);
		pgpassword = NULL;
	}
	if (pgappname) {
		ast_free(pgappname);
		pgappname = NULL;
	}
	if (pgdbport) {
		ast_free(pgdbport);
		pgdbport = NULL;
	}
	if (table) {
		ast_free(table);
		table = NULL;
	}
	if (schema) {
		ast_free(schema);
		schema = NULL;
	}
	while ((current = AST_RWLIST_REMOVE_HEAD(&psql_columns, list))) {
		ast_free(current);
	}
	AST_RWLIST_UNLOCK(&psql_columns);
	return 0;
}

static int unload_module(void)
{
	return my_unload_module();
}

static int process_my_load_module(struct ast_config *cfg)
{
	struct ast_variable *var;
	char *pgerror;
	const char *tmp;
	PGresult *result;
	struct columns *cur;

	if (!(var = ast_variable_browse(cfg, "global"))) {
		ast_log(LOG_WARNING,"CEL pgsql config file missing global section.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg,"global","hostname"))) {
		ast_log(LOG_WARNING,"PostgreSQL server hostname not specified.  Assuming unix socket connection\n");
		tmp = "";	/* connect via UNIX-socket by default */
	}
	if (pghostname)
		ast_free(pghostname);
	if (!(pghostname = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying host info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "dbname"))) {
		ast_log(LOG_WARNING,"PostgreSQL database not specified.  Assuming asterisk\n");
		tmp = "asteriskceldb";
	}
	if (pgdbname)
		ast_free(pgdbname);
	if (!(pgdbname = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying dbname info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "user"))) {
		ast_log(LOG_WARNING,"PostgreSQL database user not specified.  Assuming asterisk\n");
		tmp = "asterisk";
	}
	if (pgdbuser)
		ast_free(pgdbuser);
	if (!(pgdbuser = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying user info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "password"))) {
		ast_log(LOG_WARNING, "PostgreSQL database password not specified.  Assuming blank\n");
		tmp = "";
	}
	if (pgpassword)
		ast_free(pgpassword);
	if (!(pgpassword = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying password info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "appname"))) {
		tmp = "";
	}
	if (pgappname) {
		ast_free(pgappname);
	}
	if (!(pgappname = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying appname info\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(tmp = ast_variable_retrieve(cfg,"global","port"))) {
		ast_log(LOG_WARNING,"PostgreSQL database port not specified.  Using default 5432.\n");
		tmp = "5432";
	}
	if (pgdbport)
		ast_free(pgdbport);
	if (!(pgdbport = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying port info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "table"))) {
		ast_log(LOG_WARNING,"CEL table not specified.  Assuming cel\n");
		tmp = "cel";
	}
	if (table)
		ast_free(table);
	if (!(table = ast_strdup(tmp))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	cel_show_user_def = CEL_SHOW_USERDEF_DEFAULT;
	if ((tmp = ast_variable_retrieve(cfg, "global", "show_user_defined"))) {
		cel_show_user_def = ast_true(tmp) ? 1 : 0;
	}
	if ((tmp = ast_variable_retrieve(cfg, "global", "usegmtime"))) {
		usegmtime = ast_true(tmp);
	} else {
		usegmtime = 0;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "schema"))) {
		tmp = "";
	}
	if (schema) {
		ast_free(schema);
	}
	if (!(schema = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"PostgreSQL Ran out of memory copying schema info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (option_debug) {
		if (ast_strlen_zero(pghostname)) {
			ast_debug(3, "cel_pgsql: using default unix socket\n");
		} else {
			ast_debug(3, "cel_pgsql: got hostname of %s\n", pghostname);
		}
		ast_debug(3, "cel_pgsql: got port of %s\n", pgdbport);
		ast_debug(3, "cel_pgsql: got user of %s\n", pgdbuser);
		ast_debug(3, "cel_pgsql: got dbname of %s\n", pgdbname);
		ast_debug(3, "cel_pgsql: got password of %s\n", pgpassword);
		ast_debug(3, "cel_pgsql: got sql table name of %s\n", table);
		ast_debug(3, "cel_pgsql: got show_user_defined of %s\n",
			cel_show_user_def ? "Yes" : "No");
	}

	pgsql_reconnect();
	if (PQstatus(conn) != CONNECTION_BAD) {
		char sqlcmd[768];
		char *fname, *ftype, *flen, *fnotnull, *fdef, *tablename, *tmp_tablename;
		int i, rows, version, int_flen;

		ast_debug(1, "Successfully connected to PostgreSQL database.\n");
		connected = 1;

		version = PQserverVersion(conn);
		/* Remove any schema name from the table */
		if ((tmp_tablename = strrchr(table, '.'))) {
			tmp_tablename++;
		} else {
			tmp_tablename = table;
		}
		tablename = ast_alloca(strlen(tmp_tablename) * 2 + 1);
		PQescapeStringConn(conn, tablename, tmp_tablename, strlen(tmp_tablename), NULL);
		if (version >= PGSQL_MIN_VERSION_SCHEMA) {
			char *schemaname;
			int lenschema;
			lenschema = strlen(schema);
			schemaname = ast_alloca(lenschema * 2 + 1);
			PQescapeStringConn(conn, schemaname, schema, lenschema, NULL);

			snprintf(sqlcmd, sizeof(sqlcmd),
			"SELECT a.attname, t.typname, a.attlen, a.attnotnull, d.adsrc, a.atttypmod "
			"FROM (((pg_catalog.pg_class c INNER JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
			         "AND c.relname = '%s' AND n.nspname = %s%s%s) "
			       "INNER JOIN pg_catalog.pg_attribute a ON ("
			           "NOT a.attisdropped) AND a.attnum > 0 AND a.attrelid = c.oid) "
			    "INNER JOIN pg_catalog.pg_type t ON t.oid = a.atttypid) "
			"LEFT OUTER JOIN pg_attrdef d ON a.atthasdef AND d.adrelid = a.attrelid "
			  "AND d.adnum = a.attnum "
			"ORDER BY n.nspname, c.relname, attnum",
				tablename,
				lenschema == 0 ? "" : "'", lenschema == 0 ? "current_schema()" : schemaname, lenschema == 0 ? "" : "'");
		} else {
			snprintf(sqlcmd, sizeof(sqlcmd),
			"SELECT a.attname, t.typname, a.attlen, a.attnotnull, d.adsrc, a.atttypmod "
			"FROM pg_class c, pg_type t, pg_attribute a "
			"LEFT OUTER JOIN pg_attrdef d ON a.atthasdef AND d.adrelid = a.attrelid "
			"AND d.adnum = a.attnum WHERE c.oid = a.attrelid AND a.atttypid = t.oid "
			"AND (a.attnum > 0) AND c.relname = '%s' ORDER BY c.relname, attnum", tablename);
		}
		/* Query the columns */
		result = PQexec(conn, sqlcmd);
		if (PQresultStatus(result) != PGRES_TUPLES_OK) {
			pgerror = PQresultErrorMessage(result);
			ast_log(LOG_ERROR, "Failed to query database columns: %s\n", pgerror);
			PQclear(result);
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}

		rows = PQntuples(result);
		for (i = 0; i < rows; i++) {
			fname = PQgetvalue(result, i, 0);
			ftype = PQgetvalue(result, i, 1);
			flen = PQgetvalue(result, i, 2);
			fnotnull = PQgetvalue(result, i, 3);
			fdef = PQgetvalue(result, i, 4);
			int_flen = 0;
			if (sscanf(flen, "%d", &int_flen) == 1 && int_flen == -1) {
				/* For varchar columns, the maximum length is encoded in a different field */
				flen = PQgetvalue(result, i, 5);
			}
			ast_verb(4, "Found column '%s' of type '%s'\n", fname, ftype);
			cur = ast_calloc(1, sizeof(*cur) + strlen(fname) + strlen(ftype) + 2);
			if (cur) {
				sscanf(flen, "%30d", &cur->len);
				cur->name = (char *)cur + sizeof(*cur);
				cur->type = (char *)cur + sizeof(*cur) + strlen(fname) + 1;
				strcpy(cur->name, fname);
				strcpy(cur->type, ftype);
				if (*fnotnull == 't') {
					cur->notnull = 1;
				} else {
					cur->notnull = 0;
				}
				if (!ast_strlen_zero(fdef)) {
					cur->hasdefault = 1;
				} else {
					cur->hasdefault = 0;
				}
				AST_RWLIST_INSERT_TAIL(&psql_columns, cur, list);
			}
		}
		PQclear(result);
	} else {
		pgerror = PQerrorMessage(conn);
		ast_log(LOG_ERROR, "cel_pgsql: Unable to connect to database server %s.  CALLS WILL NOT BE LOGGED!!\n", pghostname);
		ast_log(LOG_ERROR, "cel_pgsql: Reason: %s\n", pgerror);
		connected = 0;
		PQfinish(conn);
		conn = NULL;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int my_load_module(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((cfg = ast_config_load(config, config_flags)) == NULL || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load config for PostgreSQL CEL's: %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return AST_MODULE_LOAD_SUCCESS;
	}

	if (reload) {
		my_unload_module();
	}

	process_my_load_module(cfg);
	ast_config_destroy(cfg);

	if (ast_cel_backend_register(PGSQL_BACKEND_NAME, pgsql_log)) {
		ast_log(LOG_WARNING, "Unable to subscribe to CEL events for pgsql\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int load_module(void)
{
	return my_load_module(0);
}

static int reload(void)
{
	return my_load_module(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PostgreSQL CEL Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
