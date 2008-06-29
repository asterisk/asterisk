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

/*! \file
 *
 * \brief PostgreSQL CDR logger 
 * 
 * \author Matthew D. Hardeman <mhardemn@papersoft.com> 
 * \extref PostgreSQL http://www.postgresql.org/
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.postgresql.org/
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>pgsql</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>

#include <libpq-fe.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"

#define DATE_FORMAT "'%Y-%m-%d %T'"

static char *name = "pgsql";
static char *config = "cdr_pgsql.conf";
static char *pghostname = NULL, *pgdbname = NULL, *pgdbuser = NULL, *pgpassword = NULL, *pgdbport = NULL, *table = NULL;
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

#define LENGTHEN_BUF1(size)														\
			do {																\
				/* Lengthen buffer, if necessary */								\
				if ((newsize = lensql + (size) + 3) > sizesql) {	\
					if ((tmp = ast_realloc(sql, (newsize / 512 + 1) * 512))) {	\
						sql = tmp;												\
						sizesql = (newsize / 512 + 1) * 512;					\
					} else {													\
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CDR failed.\n"); \
						ast_free(sql);											\
						ast_free(sql2);											\
						AST_RWLIST_UNLOCK(&psql_columns);						\
						return -1;												\
					}															\
				}																\
			} while (0)

#define LENGTHEN_BUF2(size)														\
			do {																\
				if ((newsize = lensql2 + (size) + 3) > sizesql2) {				\
					if ((tmp = ast_realloc(sql2, (newsize / 512 + 1) * 512))) {	\
						sql2 = tmp;												\
						sizesql2 = (newsize / 512 + 1) * 512;					\
					} else {													\
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CDR failed.\n");	\
						ast_free(sql);											\
						ast_free(sql2);											\
						AST_RWLIST_UNLOCK(&psql_columns);						\
						return -1;												\
					}															\
				}																\
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
		} else {
			pgerror = PQerrorMessage(conn);
			ast_log(LOG_ERROR, "cdr_pgsql: Unable to connect to database server %s.  Calls will not be logged!\n", pghostname);
			ast_log(LOG_ERROR, "cdr_pgsql: Reason: %s\n", pgerror);
			PQfinish(conn);
			conn = NULL;
		}
	}

	if (connected) {
		struct columns *cur;
		int lensql, lensql2, sizesql = maxsize, sizesql2 = maxsize2, newsize;
		char *sql = ast_calloc(sizeof(char), sizesql), *sql2 = ast_calloc(sizeof(char), sizesql2), *tmp, *value;
		char buf[257], escapebuf[513];
  
		lensql = snprintf(sql, sizesql, "INSERT INTO %s (", table);
		lensql2 = snprintf(sql2, sizesql2, " VALUES (");
  
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
					lensql += snprintf(sql + lensql, sizesql - lensql, "\"%s\",", cur->name);
					LENGTHEN_BUF2(3);
					strcat(sql2, "'',");
					lensql2 += 3;
				}
				continue;
			}
			
			LENGTHEN_BUF1(strlen(cur->name) + 2);
			lensql += snprintf(sql + lensql, sizesql - lensql, "\"%s\",", cur->name);

			if (strcmp(cur->name, "start") == 0 || strcmp(cur->name, "calldate") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(12);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%ld", cdr->start.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(30);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%f", (double)cdr->start.tv_sec + (double)cdr->start.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(30);
					ast_localtime(&cdr->start, &tm, NULL);
					lensql2 += ast_strftime(sql2 + lensql2, sizesql2 - lensql2, DATE_FORMAT, &tm);
				}
			} else if (strcmp(cur->name, "answer") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(12);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%ld", cdr->answer.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(30);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%f", (double)cdr->answer.tv_sec + (double)cdr->answer.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(30);
					ast_localtime(&cdr->start, &tm, NULL);
					lensql2 += ast_strftime(sql2 + lensql2, sizesql2 - lensql2, DATE_FORMAT, &tm);
				}
			} else if (strcmp(cur->name, "end") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(12);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%ld", cdr->end.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(30);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%f", (double)cdr->end.tv_sec + (double)cdr->end.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(30);
					ast_localtime(&cdr->end, &tm, NULL);
					lensql2 += ast_strftime(sql2 + lensql2, sizesql2 - lensql2, DATE_FORMAT, &tm);
				}
			} else if (strcmp(cur->name, "duration") == 0 || strcmp(cur->name, "billsec") == 0) {
				if (cur->type[0] == 'i') {
					/* Get integer, no need to escape anything */
					ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
					LENGTHEN_BUF2(12);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%s", value);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					struct timeval *tv = cur->name[0] == 'd' ? &cdr->start : &cdr->answer;
					LENGTHEN_BUF2(30);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%f", (double)cdr->end.tv_sec - tv->tv_sec + cdr->end.tv_usec / 1000000.0 - tv->tv_usec / 1000000.0);
				} else {
					/* Char field, probably */
					struct timeval *tv = cur->name[0] == 'd' ? &cdr->start : &cdr->answer;
					LENGTHEN_BUF2(30);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "'%f'", (double)cdr->end.tv_sec - tv->tv_sec + cdr->end.tv_usec / 1000000.0 - tv->tv_usec / 1000000.0);
				}
			} else if (strcmp(cur->name, "disposition") == 0 || strcmp(cur->name, "amaflags") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					/* Integer, no need to escape anything */
					ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 1);
					LENGTHEN_BUF2(12);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%s", value);
				} else {
					/* Although this is a char field, there are no special characters in the values for these fields */
					ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
					LENGTHEN_BUF2(30);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "'%s'", value);
				}
			} else {
				/* Arbitrary field, could be anything */
				ast_cdr_getvar(cdr, cur->name, &value, buf, sizeof(buf), 0, 0);
				if (strncmp(cur->type, "int", 3) == 0) {
					long long whatever;
					if (value && sscanf(value, "%lld", &whatever) == 1) {
						LENGTHEN_BUF2(25);
						lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%lld", whatever);
					} else {
						LENGTHEN_BUF2(1);
						lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "0");
					}
				} else if (strncmp(cur->type, "float", 5) == 0) {
					long double whatever;
					if (value && sscanf(value, "%Lf", &whatever) == 1) {
						LENGTHEN_BUF2(50);
						lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "%30Lf", whatever);
					} else {
						LENGTHEN_BUF2(1);
						lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "0");
					}
				/* XXX Might want to handle dates, times, and other misc fields here XXX */
				} else {
					if (value)
						PQescapeStringConn(conn, escapebuf, value, strlen(value), NULL);
					else
						escapebuf[0] = '\0';
					LENGTHEN_BUF2(strlen(escapebuf) + 2);
					lensql2 += snprintf(sql2 + lensql2, sizesql2 - lensql2, "'%s'", escapebuf);
				}
			}
			LENGTHEN_BUF2(1);
			strcat(sql2 + lensql2, ",");
			lensql2++;
  		}
		AST_RWLIST_UNLOCK(&psql_columns);
		LENGTHEN_BUF1(lensql2);
		sql[lensql - 1] = ')';
		sql2[lensql2 - 1] = ')';
		strcat(sql + lensql, sql2);
		ast_verb(11, "[%s]\n", sql);

		ast_debug(2, "cdr_pgsql: inserting a CDR record.\n");

		/* Test to be sure we're still connected... */
		/* If we're connected, and connection is working, good. */
		/* Otherwise, attempt reconnect.  If it fails... sorry... */
		if (PQstatus(conn) == CONNECTION_OK) {
			connected = 1;
		} else {
			ast_log(LOG_ERROR, "cdr_pgsql: Connection was lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_ERROR, "cdr_pgsql: Connection reestablished.\n");
				connected = 1;
			} else {
				pgerror = PQerrorMessage(conn);
				ast_log(LOG_ERROR, "cdr_pgsql: Unable to reconnect to database server %s. Calls will not be logged!\n", pghostname);
				ast_log(LOG_ERROR, "cdr_pgsql: Reason: %s\n", pgerror);
				PQfinish(conn);
				conn = NULL;
				connected = 0;
				ast_mutex_unlock(&pgsql_lock);
				return -1;
			}
		}
		result = PQexec(conn, sql);
		if (PQresultStatus(result) != PGRES_COMMAND_OK) {
			pgerror = PQresultErrorMessage(result);
			ast_log(LOG_ERROR,"cdr_pgsql: Failed to insert call detail record into database!\n");
			ast_log(LOG_ERROR,"cdr_pgsql: Reason: %s\n", pgerror);
			ast_log(LOG_ERROR,"cdr_pgsql: Connection may have been lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_ERROR, "cdr_pgsql: Connection reestablished.\n");
				connected = 1;
				PQclear(result);
				result = PQexec(conn, sql);
				if (PQresultStatus(result) != PGRES_COMMAND_OK) {
					pgerror = PQresultErrorMessage(result);
					ast_log(LOG_ERROR,"cdr_pgsql: HARD ERROR!  Attempted reconnection failed.  DROPPING CALL RECORD!\n");
					ast_log(LOG_ERROR,"cdr_pgsql: Reason: %s\n", pgerror);
				}
			}
			ast_mutex_unlock(&pgsql_lock);
			PQclear(result);
			return -1;
		}
		PQclear(result);
	}
	ast_mutex_unlock(&pgsql_lock);
	return 0;
}

static int unload_module(void)
{
	struct columns *cur;
	ast_cdr_unregister(name);

	/* Give all threads time to finish */
	usleep(1);
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
	while ((cur = AST_RWLIST_REMOVE_HEAD(&psql_columns, list))) {
		ast_free(cur);
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

	if ((cfg = ast_config_load(config, config_flags)) == NULL) {
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
		ast_log(LOG_WARNING,"PostgreSQL database not specified.  Assuming asterisk\n");
		tmp = "asteriskcdrdb";
	}

	if (pgdbname)
		ast_free(pgdbname);
	if (!(pgdbname = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "user"))) {
		ast_log(LOG_WARNING,"PostgreSQL database user not specified.  Assuming asterisk\n");
		tmp = "asterisk";
	}

	if (pgdbuser)
		ast_free(pgdbuser);
	if (!(pgdbuser = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "password"))) {
		ast_log(LOG_WARNING,"PostgreSQL database password not specified.  Assuming blank\n");
		tmp = "";
	}

	if (pgpassword)
		ast_free(pgpassword);
	if (!(pgpassword = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg,"global","port"))) {
		ast_log(LOG_WARNING,"PostgreSQL database port not specified.  Using default 5432.\n");
		tmp = "5432";
	}

	if (pgdbport)
		ast_free(pgdbport);
	if (!(pgdbport = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "table"))) {
		ast_log(LOG_WARNING,"CDR table not specified.  Assuming cdr\n");
		tmp = "cdr";
	}

	if (table)
		ast_free(table);
	if (!(table = ast_strdup(tmp))) {
		ast_config_destroy(cfg);
		return -1;
	}

	if (option_debug) {
	    	if (ast_strlen_zero(pghostname))
			ast_debug(1, "cdr_pgsql: using default unix socket\n");
		else
			ast_debug(1, "cdr_pgsql: got hostname of %s\n", pghostname);
			ast_debug(1, "cdr_pgsql: got port of %s\n", pgdbport);
			ast_debug(1, "cdr_pgsql: got user of %s\n", pgdbuser);
			ast_debug(1, "cdr_pgsql: got dbname of %s\n", pgdbname);
			ast_debug(1, "cdr_pgsql: got password of %s\n", pgpassword);
			ast_debug(1, "cdr_pgsql: got sql table name of %s\n", table);
	}
	
	conn = PQsetdbLogin(pghostname, pgdbport, NULL, NULL, pgdbname, pgdbuser, pgpassword);
	if (PQstatus(conn) != CONNECTION_BAD) {
		char sqlcmd[512];
		char *fname, *ftype, *flen, *fnotnull, *fdef;
		int i, rows;
		ast_debug(1, "Successfully connected to PostgreSQL database.\n");
		connected = 1;

		/* Query the columns */
		snprintf(sqlcmd, sizeof(sqlcmd), "select a.attname, t.typname, a.attlen, a.attnotnull, d.adsrc from pg_class c, pg_type t, pg_attribute a left outer join pg_attrdef d on a.atthasdef and d.adrelid = a.attrelid and d.adnum = a.attnum where c.oid = a.attrelid and a.atttypid = t.oid and (a.attnum > 0) and c.relname = '%s' order by c.relname, attnum", table);
		result = PQexec(conn, sqlcmd);
		if (PQresultStatus(result) != PGRES_TUPLES_OK) {
			pgerror = PQresultErrorMessage(result);
			ast_log(LOG_ERROR, "cdr_pgsql: Failed to query database columns: %s\n", pgerror);
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
			ast_verb(4, "Found column '%s' of type '%s'\n", fname, ftype);
			cur = ast_calloc(1, sizeof(*cur) + strlen(fname) + strlen(ftype) + 2);
			if (cur) {
				sscanf(flen, "%d", &cur->len);
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
		ast_log(LOG_ERROR, "cdr_pgsql: Unable to connect to database server %s.  CALLS WILL NOT BE LOGGED!!\n", pghostname);
		ast_log(LOG_ERROR, "cdr_pgsql: Reason: %s\n", pgerror);
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "PostgreSQL CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
