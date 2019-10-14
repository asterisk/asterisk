/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2003 - 2012
 *
 * Matthew D. Hardeman <mhardemn@papersoft.com>
 * Adapted from the MySQL CDR logger originally by James Sharp
 *
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

/*!
 * \file
 * \brief PostgreSQL CDR logger
 *
 * \author Matthew D. Hardeman <mhardemn@papersoft.com>
 * PostgreSQL http://www.postgresql.org/
 *
 * See also
 * \arg \ref Config_cdr
 * PostgreSQL http://www.postgresql.org/
 * \ingroup cdr_drivers
 */

/*! \li \ref cdr_pgsql.c uses the configuration file \ref cdr_pgsql.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr_pgsql.conf cdr_pgsql.conf
 * \verbinclude cdr_pgsql.conf.sample
 */

/*** MODULEINFO
	<depend>pgsql</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <libpq-fe.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"

#define DATE_FORMAT "'%Y-%m-%d %T'"

#define PGSQL_MIN_VERSION_SCHEMA 70300

static const char name[] = "pgsql";
static const char config[] = "cdr_pgsql.conf";

static char *pghostname;
static char *pgdbname;
static char *pgdbuser;
static char *pgpassword;
static char *pgappname;
static char *pgdbport;
static char *table;
static char *encoding;
static char *tz;

static int connected = 0;
/* Optimization to reduce number of memory allocations */
static int maxsize = 512, maxsize2 = 512;
static time_t connect_time = 0;
static int totalrecords = 0;
static int records;

static char *handle_cdr_pgsql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static struct ast_cli_entry cdr_pgsql_status_cli[] = {
        AST_CLI_DEFINE(handle_cdr_pgsql_status, "Show connection status of the PostgreSQL CDR driver (cdr_pgsql)"),
};

AST_MUTEX_DEFINE_STATIC(pgsql_lock);

static PGconn	*conn = NULL;

struct columns {
	char *name;
	char *type;
	int len;
	unsigned int notnull:1;
	unsigned int hasdefault:1;
	AST_RWLIST_ENTRY(columns) list;
};

static AST_RWLIST_HEAD_STATIC(psql_columns, columns);

#define LENGTHEN_BUF(size, var_sql) \
			do { \
				/* Lengthen buffer, if necessary */ \
				if (ast_str_strlen(var_sql) + size + 1 > ast_str_size(var_sql)) { \
					if (ast_str_make_space(&var_sql, ((ast_str_size(var_sql) + size + 3) / 512 + 1) * 512) != 0) { \
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory. Insert CDR '%s:%s' failed.\n", pghostname, table); \
						ast_free(sql); \
						ast_free(sql2); \
						AST_RWLIST_UNLOCK(&psql_columns); \
						ast_mutex_unlock(&pgsql_lock); \
						return -1; \
					} \
				} \
			} while (0)

#define LENGTHEN_BUF1(size) \
	LENGTHEN_BUF(size, sql);
#define LENGTHEN_BUF2(size) \
	LENGTHEN_BUF(size, sql2);

/*! \brief Handle the CLI command cdr show pgsql status */
static char *handle_cdr_pgsql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "cdr show pgsql status";
		e->usage =
			"Usage: cdr show pgsql status\n"
			"       Shows current connection status for cdr_pgsql\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (connected) {
		char status[256];
		char status2[100] = "";
		char buf[362]; /* 256+100+" for "+NULL */
		int ctime = time(NULL) - connect_time;

		if (pgdbport) {
			snprintf(status, 255, "Connected to %s@%s, port %s", pgdbname, pghostname, pgdbport);
		} else {
			snprintf(status, 255, "Connected to %s@%s", pgdbname, pghostname);
		}

		if (pgdbuser && *pgdbuser) {
			snprintf(status2, 99, " with username %s", pgdbuser);
		}
		if (table && *table) {
			snprintf(status2, 99, " using table %s", table);
		}

		snprintf(buf, sizeof(buf), "%s%s for ", status, status2);
		ast_cli_print_timestr_fromseconds(a->fd, ctime, buf);

		if (records == totalrecords) {
			ast_cli(a->fd, "  Wrote %d records since last restart.\n", totalrecords);
		} else {
			ast_cli(a->fd, "  Wrote %d records since last restart and %d records since last reconnect.\n", totalrecords, records);
		}
	} else {
		ast_cli(a->fd, "Not currently connected to a PgSQL server.\n");
	}
	return CLI_SUCCESS;
}

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

	if (!ast_strlen_zero(pghostname)) {
		ast_str_append(&conn_info, 0, "host=%s ", pghostname);
	}
	if (!ast_strlen_zero(pgdbport)) {
		ast_str_append(&conn_info, 0, "port=%s ", pgdbport);
	}
	if (!ast_strlen_zero(pgdbname)) {
		ast_str_append(&conn_info, 0, "dbname=%s ", pgdbname);
	}
	if (!ast_strlen_zero(pgdbuser)) {
		ast_str_append(&conn_info, 0, "user=%s ", pgdbuser);
	}
	if (!ast_strlen_zero(pgappname)) {
		ast_str_append(&conn_info, 0, "application_name=%s ", pgappname);
	}
	if (!ast_strlen_zero(pgpassword)) {
		ast_str_append(&conn_info, 0, "password=%s", pgpassword);
	}
	if (ast_str_strlen(conn_info) == 0) {
		ast_log(LOG_ERROR, "Connection string is blank.\n");
		return;
	}

	conn = PQconnectdb(ast_str_buffer(conn_info));
	ast_free(conn_info);
}

static int pgsql_log(struct ast_cdr *cdr)
{
	struct ast_tm tm;
	char *pgerror;
	PGresult *result;
	int res = -1;

	ast_mutex_lock(&pgsql_lock);

	if ((!connected) && pghostname && pgdbuser && pgpassword && pgdbname) {
		pgsql_reconnect();

		if (PQstatus(conn) != CONNECTION_BAD) {
			connected = 1;
			connect_time = time(NULL);
			records = 0;
			if (PQsetClientEncoding(conn, encoding)) {
#ifdef HAVE_PGSQL_pg_encoding_to_char
				ast_log(LOG_WARNING, "Failed to set encoding to '%s'.  Encoding set to default '%s'\n", encoding, pg_encoding_to_char(PQclientEncoding(conn)));
#else
				ast_log(LOG_WARNING, "Failed to set encoding to '%s'.  Encoding set to default.\n", encoding);
#endif
			}
		} else {
			pgerror = PQerrorMessage(conn);
			ast_log(LOG_ERROR, "Unable to connect to database server %s.  Calls will not be logged!\n", pghostname);
			ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
			PQfinish(conn);
			conn = NULL;
		}
	}

	if (connected) {
		struct columns *cur;
		struct ast_str *sql = ast_str_create(maxsize), *sql2 = ast_str_create(maxsize2);
		char buf[257];
		char *escapebuf = NULL, *value;
		char *separator = "";
		size_t bufsize = 513;

		escapebuf = ast_malloc(bufsize);
		if (!escapebuf || !sql || !sql2) {
			goto ast_log_cleanup;
		}

		ast_str_set(&sql, 0, "INSERT INTO %s (", table);
		ast_str_set(&sql2, 0, " VALUES (");

		AST_RWLIST_RDLOCK(&psql_columns);
		AST_RWLIST_TRAVERSE(&psql_columns, cur, list) {
			/* For fields not set, simply skip them */
			ast_cdr_format_var(cdr, cur->name, &value, buf, sizeof(buf), 0);
			if (strcmp(cur->name, "calldate") == 0 && !value) {
				ast_cdr_format_var(cdr, "start", &value, buf, sizeof(buf), 0);
			}
			if (!value) {
				if (cur->notnull && !cur->hasdefault) {
					/* Field is NOT NULL (but no default), must include it anyway */
					LENGTHEN_BUF1(strlen(cur->name) + 2);
					ast_str_append(&sql, 0, "%s\"%s\"", separator, cur->name);
					LENGTHEN_BUF2(3);
					ast_str_append(&sql2, 0, "%s''", separator);
					separator = ", ";
				}
				continue;
			}

			LENGTHEN_BUF1(strlen(cur->name) + 2);
			ast_str_append(&sql, 0, "%s\"%s\"", separator, cur->name);

			if (strcmp(cur->name, "start") == 0 || strcmp(cur->name, "calldate") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", separator, (long) cdr->start.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", separator, (double)cdr->start.tv_sec + (double)cdr->start.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&cdr->start, &tm, tz);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s%s", separator, buf);
				}
			} else if (strcmp(cur->name, "answer") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", separator, (long) cdr->answer.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", separator, (double)cdr->answer.tv_sec + (double)cdr->answer.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&cdr->answer, &tm, tz);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s%s", separator, buf);
				}
			} else if (strcmp(cur->name, "end") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", separator, (long) cdr->end.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", separator, (double)cdr->end.tv_sec + (double)cdr->end.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&cdr->end, &tm, tz);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s%s", separator, buf);
				}
			} else if (strcmp(cur->name, "duration") == 0 || strcmp(cur->name, "billsec") == 0) {
				if (cur->type[0] == 'i') {
					/* Get integer, no need to escape anything */
					ast_cdr_format_var(cdr, cur->name, &value, buf, sizeof(buf), 0);
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%s", separator, value);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					struct timeval *when = cur->name[0] == 'd' ? &cdr->start : ast_tvzero(cdr->answer) ? &cdr->end : &cdr->answer;
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", separator, (double) (ast_tvdiff_us(cdr->end, *when) / 1000000.0));
				} else {
					/* Char field, probably */
					struct timeval *when = cur->name[0] == 'd' ? &cdr->start : ast_tvzero(cdr->answer) ? &cdr->end : &cdr->answer;
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s'%f'", separator, (double) (ast_tvdiff_us(cdr->end, *when) / 1000000.0));
				}
			} else if (strcmp(cur->name, "disposition") == 0 || strcmp(cur->name, "amaflags") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					/* Integer, no need to escape anything */
					ast_cdr_format_var(cdr, cur->name, &value, buf, sizeof(buf), 1);
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%s", separator, value);
				} else {
					/* Although this is a char field, there are no special characters in the values for these fields */
					ast_cdr_format_var(cdr, cur->name, &value, buf, sizeof(buf), 0);
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s'%s'", separator, value);
				}
			} else {
				/* Arbitrary field, could be anything */
				ast_cdr_format_var(cdr, cur->name, &value, buf, sizeof(buf), 0);
				if (strncmp(cur->type, "int", 3) == 0) {
					long long whatever;
					if (value && sscanf(value, "%30lld", &whatever) == 1) {
						LENGTHEN_BUF2(26);
						ast_str_append(&sql2, 0, "%s%lld", separator, whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", separator);
					}
				} else if (strncmp(cur->type, "float", 5) == 0) {
					long double whatever;
					if (value && sscanf(value, "%30Lf", &whatever) == 1) {
						LENGTHEN_BUF2(51);
						ast_str_append(&sql2, 0, "%s%30Lf", separator, whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", separator);
					}
				/* XXX Might want to handle dates, times, and other misc fields here XXX */
				} else {
					if (value) {
						size_t required_size = strlen(value) * 2 + 1;

						/* If our argument size exceeds our buffer, grow it,
						 * as PQescapeStringConn() expects the buffer to be
						 * adequitely sized and does *NOT* do size checking.
						 */
						if (required_size > bufsize) {
							char *tmpbuf = ast_realloc(escapebuf, required_size);

							if (!tmpbuf) {
								AST_RWLIST_UNLOCK(&psql_columns);
								goto ast_log_cleanup;
							}

							escapebuf = tmpbuf;
							bufsize = required_size;
						}
						PQescapeStringConn(conn, escapebuf, value, strlen(value), NULL);
					} else {
						escapebuf[0] = '\0';
					}
					LENGTHEN_BUF2(strlen(escapebuf) + 3);
					ast_str_append(&sql2, 0, "%s'%s'", separator, escapebuf);
				}
			}
			separator = ", ";
		}

		LENGTHEN_BUF1(ast_str_strlen(sql2) + 2);
		AST_RWLIST_UNLOCK(&psql_columns);
		ast_str_append(&sql, 0, ")%s)", ast_str_buffer(sql2));

		ast_debug(3, "Inserting a CDR record: [%s]\n", ast_str_buffer(sql));

		/* Test to be sure we're still connected... */
		/* If we're connected, and connection is working, good. */
		/* Otherwise, attempt reconnect.  If it fails... sorry... */
		if (PQstatus(conn) == CONNECTION_OK) {
			connected = 1;
		} else {
			ast_log(LOG_ERROR, "Connection was lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_ERROR, "Connection reestablished.\n");
				connected = 1;
				connect_time = time(NULL);
				records = 0;
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
			ast_log(LOG_ERROR, "Failed to insert call detail record into database!\n");
			ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
			ast_log(LOG_ERROR, "Connection may have been lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_ERROR, "Connection reestablished.\n");
				connected = 1;
				connect_time = time(NULL);
				records = 0;
				PQclear(result);
				result = PQexec(conn, ast_str_buffer(sql));
				if (PQresultStatus(result) != PGRES_COMMAND_OK) {
					pgerror = PQresultErrorMessage(result);
					ast_log(LOG_ERROR, "HARD ERROR!  Attempted reconnection failed.  DROPPING CALL RECORD!\n");
					ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
				} else {
					/* Second try worked out ok */
					totalrecords++;
					records++;
					res = 0;
				}
			}
		} else {
			totalrecords++;
			records++;
			res = 0;
		}
		PQclear(result);

		/* Next time, just allocate buffers that are that big to start with. */
		if (ast_str_strlen(sql) > maxsize) {
			maxsize = ast_str_strlen(sql);
		}
		if (ast_str_strlen(sql2) > maxsize2) {
			maxsize2 = ast_str_strlen(sql2);
		}

ast_log_cleanup:
		ast_free(escapebuf);
		ast_free(sql);
		ast_free(sql2);
	}

	ast_mutex_unlock(&pgsql_lock);
	return res;
}

/* This function should be called without holding the pgsql_columns lock */
static void empty_columns(void)
{
	struct columns *current;
	AST_RWLIST_WRLOCK(&psql_columns);
	while ((current = AST_RWLIST_REMOVE_HEAD(&psql_columns, list))) {
		ast_free(current);
	}
	AST_RWLIST_UNLOCK(&psql_columns);

}

static int unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	ast_cli_unregister_multiple(cdr_pgsql_status_cli, ARRAY_LEN(cdr_pgsql_status_cli));

	if (conn) {
		PQfinish(conn);
		conn = NULL;
	}
	ast_free(pghostname);
	ast_free(pgdbname);
	ast_free(pgdbuser);
	ast_free(pgpassword);
	ast_free(pgappname);
	ast_free(pgdbport);
	ast_free(table);
	ast_free(encoding);
	ast_free(tz);

	empty_columns();

	return 0;
}

static int config_module(int reload)
{
	char *pgerror;
	struct columns *cur;
	PGresult *result;
	const char *tmp;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((cfg = ast_config_load(config, config_flags)) == NULL || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load config for PostgreSQL CDR's: %s\n", config);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	ast_mutex_lock(&pgsql_lock);

	if (!ast_variable_browse(cfg, "global")) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		ast_log(LOG_NOTICE, "cdr_pgsql configuration contains no global section, skipping module %s.\n",
			reload ? "reload" : "load");
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "hostname"))) {
		ast_log(LOG_WARNING, "PostgreSQL server hostname not specified.  Assuming unix socket connection\n");
		tmp = "";	/* connect via UNIX-socket by default */
	}

	ast_free(pghostname);
	if (!(pghostname = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "dbname"))) {
		ast_log(LOG_WARNING, "PostgreSQL database not specified.  Assuming asterisk\n");
		tmp = "asteriskcdrdb";
	}

	ast_free(pgdbname);
	if (!(pgdbname = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "user"))) {
		ast_log(LOG_WARNING, "PostgreSQL database user not specified.  Assuming asterisk\n");
		tmp = "asterisk";
	}

	ast_free(pgdbuser);
	if (!(pgdbuser = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "appname"))) {
		tmp = "";
	}

	ast_free(pgappname);
	if (!(pgappname = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}


	if (!(tmp = ast_variable_retrieve(cfg, "global", "password"))) {
		ast_log(LOG_WARNING, "PostgreSQL database password not specified.  Assuming blank\n");
		tmp = "";
	}

	ast_free(pgpassword);
	if (!(pgpassword = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "port"))) {
		ast_log(LOG_WARNING, "PostgreSQL database port not specified.  Using default 5432.\n");
		tmp = "5432";
	}

	ast_free(pgdbport);
	if (!(pgdbport = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "table"))) {
		ast_log(LOG_WARNING, "CDR table not specified.  Assuming cdr\n");
		tmp = "cdr";
	}

	ast_free(table);
	if (!(table = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "encoding"))) {
		ast_log(LOG_WARNING, "Encoding not specified.  Assuming LATIN9\n");
		tmp = "LATIN9";
	}

	ast_free(encoding);
	if (!(encoding = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "timezone"))) {
		tmp = "";
	}

	ast_free(tz);
	tz = NULL;

	if (!ast_strlen_zero(tmp) && !(tz = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		ast_mutex_unlock(&pgsql_lock);
		return -1;
	}

	if (DEBUG_ATLEAST(1)) {
		if (ast_strlen_zero(pghostname)) {
			ast_log(LOG_DEBUG, "using default unix socket\n");
		} else {
			ast_log(LOG_DEBUG, "got hostname of %s\n", pghostname);
		}
		ast_log(LOG_DEBUG, "got port of %s\n", pgdbport);
		ast_log(LOG_DEBUG, "got user of %s\n", pgdbuser);
		ast_log(LOG_DEBUG, "got dbname of %s\n", pgdbname);
		ast_log(LOG_DEBUG, "got password of %s\n", pgpassword);
		ast_log(LOG_DEBUG, "got application name of %s\n", pgappname);
		ast_log(LOG_DEBUG, "got sql table name of %s\n", table);
		ast_log(LOG_DEBUG, "got encoding of %s\n", encoding);
		ast_log(LOG_DEBUG, "got timezone of %s\n", tz);
	}

	pgsql_reconnect();

	if (PQstatus(conn) != CONNECTION_BAD) {
		char sqlcmd[768];
		char *fname, *ftype, *flen, *fnotnull, *fdef;
		int i, rows, version;
		ast_debug(1, "Successfully connected to PostgreSQL database.\n");
		connected = 1;
		connect_time = time(NULL);
		records = 0;
		if (PQsetClientEncoding(conn, encoding)) {
#ifdef HAVE_PGSQL_pg_encoding_to_char
			ast_log(LOG_WARNING, "Failed to set encoding to '%s'.  Encoding set to default '%s'\n", encoding, pg_encoding_to_char(PQclientEncoding(conn)));
#else
			ast_log(LOG_WARNING, "Failed to set encoding to '%s'.  Encoding set to default.\n", encoding);
#endif
		}
		version = PQserverVersion(conn);

		if (version >= PGSQL_MIN_VERSION_SCHEMA) {
			char *schemaname, *tablename, *tmp_schemaname, *tmp_tablename;
			if (strchr(table, '.')) {
				tmp_schemaname = ast_strdupa(table);
				tmp_tablename = strchr(tmp_schemaname, '.');
				*tmp_tablename++ = '\0';
			} else {
				tmp_schemaname = "";
				tmp_tablename = table;
			}
			tablename = ast_alloca(strlen(tmp_tablename) * 2 + 1);
			PQescapeStringConn(conn, tablename, tmp_tablename, strlen(tmp_tablename), NULL);

			schemaname = ast_alloca(strlen(tmp_schemaname) * 2 + 1);
			PQescapeStringConn(conn, schemaname, tmp_schemaname, strlen(tmp_schemaname), NULL);

			snprintf(sqlcmd, sizeof(sqlcmd), "SELECT a.attname, t.typname, a.attlen, a.attnotnull, pg_catalog.pg_get_expr(d.adbin, d.adrelid) adsrc, a.atttypmod FROM (((pg_catalog.pg_class c INNER JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace AND c.relname = '%s' AND n.nspname = %s%s%s) INNER JOIN pg_catalog.pg_attribute a ON (NOT a.attisdropped) AND a.attnum > 0 AND a.attrelid = c.oid) INNER JOIN pg_catalog.pg_type t ON t.oid = a.atttypid) LEFT OUTER JOIN pg_attrdef d ON a.atthasdef AND d.adrelid = a.attrelid AND d.adnum = a.attnum ORDER BY n.nspname, c.relname, attnum",
				tablename,
				ast_strlen_zero(schemaname) ? "" : "'", ast_strlen_zero(schemaname) ? "current_schema()" : schemaname, ast_strlen_zero(schemaname) ? "" : "'");
		} else {
			snprintf(sqlcmd, sizeof(sqlcmd), "SELECT a.attname, t.typname, a.attlen, a.attnotnull, d.adsrc, a.atttypmod FROM pg_class c, pg_type t, pg_attribute a LEFT OUTER JOIN pg_attrdef d ON a.atthasdef AND d.adrelid = a.attrelid AND d.adnum = a.attnum WHERE c.oid = a.attrelid AND a.atttypid = t.oid AND (a.attnum > 0) AND c.relname = '%s' ORDER BY c.relname, attnum", table);
		}
		/* Query the columns */
		result = PQexec(conn, sqlcmd);
		if (PQresultStatus(result) != PGRES_TUPLES_OK) {
			pgerror = PQresultErrorMessage(result);
			ast_log(LOG_ERROR, "Failed to query database columns: %s\n", pgerror);
			PQclear(result);
			unload_module();
			ast_mutex_unlock(&pgsql_lock);
			return AST_MODULE_LOAD_DECLINE;
		}

		rows = PQntuples(result);
		if (rows == 0) {
			ast_log(LOG_ERROR, "cdr_pgsql: Failed to query database columns. No columns found, does the table exist?\n");
			PQclear(result);
			unload_module();
			ast_mutex_unlock(&pgsql_lock);
			return AST_MODULE_LOAD_DECLINE;
		}

		/* Clear out the columns list. */
		empty_columns();

		for (i = 0; i < rows; i++) {
			fname = PQgetvalue(result, i, 0);
			ftype = PQgetvalue(result, i, 1);
			flen = PQgetvalue(result, i, 2);
			fnotnull = PQgetvalue(result, i, 3);
			fdef = PQgetvalue(result, i, 4);
			if (atoi(flen) == -1) {
				/* For varchar columns, the maximum length is encoded in a different field */
				flen = PQgetvalue(result, i, 5);
			}

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
				AST_RWLIST_WRLOCK(&psql_columns);
				AST_RWLIST_INSERT_TAIL(&psql_columns, cur, list);
				AST_RWLIST_UNLOCK(&psql_columns);
			}
		}
		PQclear(result);
	} else {
		pgerror = PQerrorMessage(conn);
		ast_log(LOG_ERROR, "Unable to connect to database server %s.  CALLS WILL NOT BE LOGGED!!\n", pghostname);
		ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
		connected = 0;
		PQfinish(conn);
		conn = NULL;
	}

	ast_config_destroy(cfg);

	ast_mutex_unlock(&pgsql_lock);
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cdr_pgsql_status_cli, sizeof(cdr_pgsql_status_cli) / sizeof(struct ast_cli_entry));
	if (config_module(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return ast_cdr_register(name, ast_module_info->description, pgsql_log)
		? AST_MODULE_LOAD_DECLINE : 0;
}

static int reload(void)
{
	return config_module(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PostgreSQL CDR Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cdr",
);
