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

#define DATE_FORMAT "%Y-%m-%d %T"

static char *name = "pgsql";
static char *config = "cdr_pgsql.conf";
static char *pghostname = NULL, *pgdbname = NULL, *pgdbuser = NULL, *pgpassword = NULL, *pgdbport = NULL, *table = NULL;
static int connected = 0;

AST_MUTEX_DEFINE_STATIC(pgsql_lock);

static PGconn	*conn = NULL;

static int pgsql_log(struct ast_cdr *cdr)
{
	struct ast_tm tm;
	char sqlcmd[2048] = "", timestr[128];
	char *pgerror;
	PGresult *result;

	ast_mutex_lock(&pgsql_lock);

	ast_localtime(&cdr->start, &tm, NULL);
	ast_strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

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
		char *clid=NULL, *dcontext=NULL, *channel=NULL, *dstchannel=NULL, *lastapp=NULL, *lastdata=NULL;
		char *src=NULL, *dst=NULL, *uniqueid=NULL, *userfield=NULL;
		int pgerr;

		/* Maximum space needed would be if all characters needed to be escaped, plus a trailing NULL */
		if ((clid = alloca(strlen(cdr->clid) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, clid, cdr->clid, strlen(cdr->clid), &pgerr);
		if ((dcontext = alloca(strlen(cdr->dcontext) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, dcontext, cdr->dcontext, strlen(cdr->dcontext), &pgerr);
		if ((channel = alloca(strlen(cdr->channel) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, channel, cdr->channel, strlen(cdr->channel), &pgerr);
		if ((dstchannel = alloca(strlen(cdr->dstchannel) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, dstchannel, cdr->dstchannel, strlen(cdr->dstchannel), &pgerr);
		if ((lastapp = alloca(strlen(cdr->lastapp) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, lastapp, cdr->lastapp, strlen(cdr->lastapp), &pgerr);
		if ((lastdata = alloca(strlen(cdr->lastdata) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, lastdata, cdr->lastdata, strlen(cdr->lastdata), &pgerr);
		if ((uniqueid = alloca(strlen(cdr->uniqueid) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, uniqueid, cdr->uniqueid, strlen(cdr->uniqueid), &pgerr);
		if ((userfield = alloca(strlen(cdr->userfield) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, userfield, cdr->userfield, strlen(cdr->userfield), &pgerr);
		if ((src = alloca(strlen(cdr->src) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, src, cdr->src, strlen(cdr->src), &pgerr);
		if ((dst = alloca(strlen(cdr->dst) * 2 + 1)) != NULL)
			PQescapeStringConn(conn, dst, cdr->dst, strlen(cdr->dst), &pgerr);

		/* Check for all alloca failures above at once */
		if ((!clid) || (!dcontext) || (!channel) || (!dstchannel) || (!lastapp) || (!lastdata) || (!uniqueid) || (!userfield) || (!src) || (!dst)) {
			ast_log(LOG_ERROR, "cdr_pgsql:  Out of memory error (insert fails)\n");
			ast_mutex_unlock(&pgsql_lock);
			return -1;
		}

		ast_debug(2, "cdr_pgsql: inserting a CDR record.\n");

		snprintf(sqlcmd,sizeof(sqlcmd),"INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,"
				 "lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield) VALUES"
				 " ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%ld,%ld,'%s',%ld,'%s','%s','%s')",
				 table, timestr, clid, src, dst, dcontext, channel, dstchannel, lastapp, lastdata,
				 cdr->duration,cdr->billsec,ast_cdr_disp2str(cdr->disposition),cdr->amaflags, cdr->accountcode, uniqueid, userfield);
		
		ast_debug(3, "cdr_pgsql: SQL command executed:  %s\n",sqlcmd);
		
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
		result = PQexec(conn, sqlcmd);
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
				result = PQexec(conn, sqlcmd);
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
	ast_cdr_unregister(name);
	return 0;
}

static int config_module(int reload)
{
	struct ast_variable *var;
	char *pgerror;
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
		ast_debug(1, "Successfully connected to PostgreSQL database.\n");
		connected = 1;
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
