/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2003 - 2006
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
 * \extref PostgreSQL http://www.postgresql.org/
 *
 * See also
 * \arg \ref Config_cdr
 * \extref PostgreSQL http://www.postgresql.org/
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>pgsql</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <libpq-fe.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"

#define DATE_FORMAT "'%Y-%m-%d %T'"

static const char name[] = "pgsql";
static const char config[] = "cdr_pgsql.conf";
static char *pghostname = NULL, *pgdbname = NULL, *pgdbuser = NULL, *pgpassword = NULL, *pgdbport = NULL, *table = NULL, *encoding = NULL;
static int connected = 0;
static int maxsize = 512, maxsize2 = 512;

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

#define LENGTHEN_BUF1(size)                                               \
			do {                                                          \
				/* Lengthen buffer, if necessary */                       \
				if (ast_str_strlen(sql) + size + 1 > ast_str_size(sql)) { \
					if (ast_str_make_space(&sql, ((ast_str_size(sql) + size + 3) / 512 + 1) * 512) != 0) {	\
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CDR failed.\n"); \
						ast_free(sql);                                    \
						ast_free(sql2);                                   \
						AST_RWLIST_UNLOCK(&psql_columns);                 \
						return -1;                                        \
					}                                                     \
				}                                                         \
			} while (0)

#define LENGTHEN_BUF2(size)                               \
			do {                                          \
				if (ast_str_strlen(sql2) + size + 1 > ast_str_size(sql2)) {  \
					if (ast_str_make_space(&sql2, ((ast_str_size(sql2) + size + 3) / 512 + 1) * 512) != 0) {	\
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CDR failed.\n");	\
						ast_free(sql);                    \
						ast_free(sql2);                   \
						AST_RWLIST_UNLOCK(&psql_columns); \
						return -1;                        \
					}                                     \
				}                                         \
			} while (0)

static int pgsql_log(struct ast_cdr *cdr)
{
	struct ast_tm tm;
	char *pgerror;
	PGresult *result;

	ast_mutex_lock(&pgsql_lock);

	if ((!connected) && pghostname && pgdbuser && pgpassword && pgdbname) {
		conn = PQsetdbLogin(pghostname, pgdbport, NULL, NULL, pgdbname, pgdbuser, pgpassword);
		if (PQstatus(conn) != CONNECTION_BAD) {
			connected = 1;
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
		char buf[257], escapebuf[513], *value;
		int first = 1;
  
		if (!sql || !sql2) {
			if (sql) {
				ast_free(sql);
			}
			if (sql2) {
				ast_free(sql2);
			}
			return -1;
		}

		ast_str_set(&sql, 0, "INSERT INTO %s (", table);
		ast_str_set(&sql2, 0, " VALUES (");

		AST_RWLIST_RDLOCK(&psql_columns);
		AST_RWLIST_TRAVERSE(&psql_columns, cur, list) {
			/* For fields not set, simply skip them */
			ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
			if (strcmp(cur->name, "calldate") == 0 && !value) {
				ast_cdr_getvar(cdr, "start", &value, buf, sizeof(buf), 0, 0);
			}
			if (!value) {
				if (cur->notnull && !cur->hasdefault) {
					/* Field is NOT NULL (but no default), must include it anyway */
					LENGTHEN_BUF1(strlen(cur->name) + 2);
					ast_str_append(&sql, 0, "%s\"%s\"", first ? "" : ",", cur->name);
					LENGTHEN_BUF2(3);
					ast_str_append(&sql2, 0, "%s''", first ? "" : ",");
					first = 0;
				}
				continue;
			}

			LENGTHEN_BUF1(strlen(cur->name) + 2);
			ast_str_append(&sql, 0, "%s\"%s\"", first ? "" : ",", cur->name);

			if (strcmp(cur->name, "start") == 0 || strcmp(cur->name, "calldate") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", first ? "" : ",", (long) cdr->start.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", first ? "" : ",", (double)cdr->start.tv_sec + (double)cdr->start.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&cdr->start, &tm, NULL);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s%s", first ? "" : ",", buf);
				}
			} else if (strcmp(cur->name, "answer") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", first ? "" : ",", (long) cdr->answer.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", first ? "" : ",", (double)cdr->answer.tv_sec + (double)cdr->answer.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&cdr->start, &tm, NULL);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s%s", first ? "" : ",", buf);
				}
			} else if (strcmp(cur->name, "end") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", first ? "" : ",", (long) cdr->end.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", first ? "" : ",", (double)cdr->end.tv_sec + (double)cdr->end.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&cdr->end, &tm, NULL);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s%s", first ? "" : ",", buf);
				}
			} else if (strcmp(cur->name, "duration") == 0 || strcmp(cur->name, "billsec") == 0) {
				if (cur->type[0] == 'i') {
					/* Get integer, no need to escape anything */
					ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%s", first ? "" : ",", value);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					struct timeval *when = cur->name[0] == 'd' ? &cdr->start : &cdr->answer;
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", first ? "" : ",", (double) (ast_tvdiff_us(cdr->end, *when) / 1000000.0));
				} else {
					/* Char field, probably */
					struct timeval *when = cur->name[0] == 'd' ? &cdr->start : &cdr->answer;
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s'%f'", first ? "" : ",", (double) (ast_tvdiff_us(cdr->end, *when) / 1000000.0));
				}
			} else if (strcmp(cur->name, "disposition") == 0 || strcmp(cur->name, "amaflags") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					/* Integer, no need to escape anything */
					ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 1);
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%s", first ? "" : ",", value);
				} else {
					/* Although this is a char field, there are no special characters in the values for these fields */
					ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s'%s'", first ? "" : ",", value);
				}
			} else {
				/* Arbitrary field, could be anything */
				ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
				if (strncmp(cur->type, "int", 3) == 0) {
					long long whatever;
					if (value && sscanf(value, "%30lld", &whatever) == 1) {
						LENGTHEN_BUF2(26);
						ast_str_append(&sql2, 0, "%s%lld", first ? "" : ",", whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", first ? "" : ",");
					}
				} else if (strncmp(cur->type, "float", 5) == 0) {
					long double whatever;
					if (value && sscanf(value, "%30Lf", &whatever) == 1) {
						LENGTHEN_BUF2(51);
						ast_str_append(&sql2, 0, "%s%30Lf", first ? "" : ",", whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", first ? "" : ",");
					}
				/* XXX Might want to handle dates, times, and other misc fields here XXX */
				} else {
					if (value)
						PQescapeStringConn(conn, escapebuf, value, strlen(value), NULL);
					else
						escapebuf[0] = '\0';
					LENGTHEN_BUF2(strlen(escapebuf) + 3);
					ast_str_append(&sql2, 0, "%s'%s'", first ? "" : ",", escapebuf);
				}
			}
			first = 0;
  		}
		AST_RWLIST_UNLOCK(&psql_columns);
		LENGTHEN_BUF1(ast_str_strlen(sql2) + 2);
		ast_str_append(&sql, 0, ")%s)", ast_str_buffer(sql2));
		ast_verb(11, "[%s]\n", ast_str_buffer(sql));

		ast_debug(2, "inserting a CDR record.\n");

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
			} else {
				pgerror = PQerrorMessage(conn);
				ast_log(LOG_ERROR, "Unable to reconnect to database server %s. Calls will not be logged!\n", pghostname);
				ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
				PQfinish(conn);
				conn = NULL;
				connected = 0;
				ast_mutex_unlock(&pgsql_lock);
				ast_free(sql);
				ast_free(sql2);
				return -1;
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
				PQclear(result);
				result = PQexec(conn, ast_str_buffer(sql));
				if (PQresultStatus(result) != PGRES_COMMAND_OK) {
					pgerror = PQresultErrorMessage(result);
					ast_log(LOG_ERROR, "HARD ERROR!  Attempted reconnection failed.  DROPPING CALL RECORD!\n");
					ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
				}
			}
			ast_mutex_unlock(&pgsql_lock);
			PQclear(result);
			ast_free(sql);
			ast_free(sql2);
			return -1;
		}
		PQclear(result);
		ast_free(sql);
		ast_free(sql2);
	}
	ast_mutex_unlock(&pgsql_lock);
	return 0;
}

static int unload_module(void)
{
	struct columns *current;

	ast_cdr_unregister(name);

	PQfinish(conn);

	if (pghostname)
		ast_free(pghostname);
	if (pgdbname)
		ast_free(pgdbname);
	if (pgdbuser)
		ast_free(pgdbuser);
	if (pgpassword)
		ast_free(pgpassword);
	if (pgdbport)
		ast_free(pgdbport);
	if (table)
		ast_free(table);

	AST_RWLIST_WRLOCK(&psql_columns);
	while ((current = AST_RWLIST_REMOVE_HEAD(&psql_columns, list))) {
		ast_free(current);
	}
	AST_RWLIST_UNLOCK(&psql_columns);

	return 0;
}

static int config_module(int reload)
{
	struct ast_variable *var;
	char *pgerror;
	struct columns *cur;
	PGresult *result;
	const char *tmp;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((cfg = ast_config_load(config, config_flags)) == NULL || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load config for PostgreSQL CDR's: %s\n", config);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	if (!(var = ast_variable_browse(cfg, "global"))) {
		ast_config_destroy(cfg);
		return 0;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "hostname"))) {
		ast_log(LOG_WARNING, "PostgreSQL server hostname not specified.  Assuming unix socket connection\n");
		tmp = "";	/* connect via UNIX-socket by default */
	}

	if (pghostname)
		ast_free(pghostname);
	if (!(pghostname = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "dbname"))) {
		ast_log(LOG_WARNING, "PostgreSQL database not specified.  Assuming asterisk\n");
		tmp = "asteriskcdrdb";
	}

	if (pgdbname)
		ast_free(pgdbname);
	if (!(pgdbname = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "user"))) {
		ast_log(LOG_WARNING, "PostgreSQL database user not specified.  Assuming asterisk\n");
		tmp = "asterisk";
	}

	if (pgdbuser)
		ast_free(pgdbuser);
	if (!(pgdbuser = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "password"))) {
		ast_log(LOG_WARNING, "PostgreSQL database password not specified.  Assuming blank\n");
		tmp = "";
	}

	if (pgpassword)
		ast_free(pgpassword);
	if (!(pgpassword = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "port"))) {
		ast_log(LOG_WARNING, "PostgreSQL database port not specified.  Using default 5432.\n");
		tmp = "5432";
	}

	if (pgdbport)
		ast_free(pgdbport);
	if (!(pgdbport = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "table"))) {
		ast_log(LOG_WARNING, "CDR table not specified.  Assuming cdr\n");
		tmp = "cdr";
	}

	if (table)
		ast_free(table);
	if (!(table = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "encoding"))) {
		ast_log(LOG_WARNING, "Encoding not specified.  Assuming LATIN9\n");
		tmp = "LATIN9";
	}

	if (encoding) {
		ast_free(encoding);
	}
	if (!(encoding = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (option_debug) {
		if (ast_strlen_zero(pghostname)) {
			ast_debug(1, "using default unix socket\n");
		} else {
			ast_debug(1, "got hostname of %s\n", pghostname);
		}
		ast_debug(1, "got port of %s\n", pgdbport);
		ast_debug(1, "got user of %s\n", pgdbuser);
		ast_debug(1, "got dbname of %s\n", pgdbname);
		ast_debug(1, "got password of %s\n", pgpassword);
		ast_debug(1, "got sql table name of %s\n", table);
	}

	conn = PQsetdbLogin(pghostname, pgdbport, NULL, NULL, pgdbname, pgdbuser, pgpassword);
	if (PQstatus(conn) != CONNECTION_BAD) {
		char sqlcmd[768];
		char *fname, *ftype, *flen, *fnotnull, *fdef;
		int i, rows, version;
		ast_debug(1, "Successfully connected to PostgreSQL database.\n");
		connected = 1;
		if (PQsetClientEncoding(conn, encoding)) {
#ifdef HAVE_PGSQL_pg_encoding_to_char
			ast_log(LOG_WARNING, "Failed to set encoding to '%s'.  Encoding set to default '%s'\n", encoding, pg_encoding_to_char(PQclientEncoding(conn)));
#else
			ast_log(LOG_WARNING, "Failed to set encoding to '%s'.  Encoding set to default.\n", encoding);
#endif
		}
		version = PQserverVersion(conn);

		if (version >= 70300) {
			char *schemaname, *tablename;
			if (strchr(table, '.')) {
				schemaname = ast_strdupa(table);
				tablename = strchr(schemaname, '.');
				*tablename++ = '\0';
			} else {
				schemaname = "";
				tablename = table;
			}

			/* Escape special characters in schemaname */
			if (strchr(schemaname, '\\') || strchr(schemaname, '\'')) {
				char *tmp = schemaname, *ptr;

				ptr = schemaname = alloca(strlen(tmp) * 2 + 1);
				for (; *tmp; tmp++) {
					if (strchr("\\'", *tmp)) {
						*ptr++ = *tmp;
					}
					*ptr++ = *tmp;
				}
				*ptr = '\0';
			}
			/* Escape special characters in tablename */
			if (strchr(tablename, '\\') || strchr(tablename, '\'')) {
				char *tmp = tablename, *ptr;

				ptr = tablename = alloca(strlen(tmp) * 2 + 1);
				for (; *tmp; tmp++) {
					if (strchr("\\'", *tmp)) {
						*ptr++ = *tmp;
					}
					*ptr++ = *tmp;
				}
				*ptr = '\0';
			}

			snprintf(sqlcmd, sizeof(sqlcmd), "SELECT a.attname, t.typname, a.attlen, a.attnotnull, d.adsrc, a.atttypmod FROM (((pg_catalog.pg_class c INNER JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace AND c.relname = '%s' AND n.nspname = %s%s%s) INNER JOIN pg_catalog.pg_attribute a ON (NOT a.attisdropped) AND a.attnum > 0 AND a.attrelid = c.oid) INNER JOIN pg_catalog.pg_type t ON t.oid = a.atttypid) LEFT OUTER JOIN pg_attrdef d ON a.atthasdef AND d.adrelid = a.attrelid AND d.adnum = a.attnum ORDER BY n.nspname, c.relname, attnum",
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
			return AST_MODULE_LOAD_DECLINE;
		}

		rows = PQntuples(result);
		if (rows == 0) {
			ast_log(LOG_ERROR, "cdr_pgsql: Failed to query database columns. No columns found, does the table exist?\n");
			PQclear(result);
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}

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
		ast_log(LOG_ERROR, "Unable to connect to database server %s.  CALLS WILL NOT BE LOGGED!!\n", pghostname);
		ast_log(LOG_ERROR, "Reason: %s\n", pgerror);
		connected = 0;
	}

	ast_config_destroy(cfg);

	return ast_cdr_register(name, ast_module_info->description, pgsql_log);
}

static int load_module(void)
{
	return config_module(0) ? AST_MODULE_LOAD_DECLINE : 0;
}

static int reload(void)
{
	return config_module(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PostgreSQL CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CDR_DRIVER,
	       );
