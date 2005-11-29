/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * PostgreSQL CDR logger 
 *
 * Matthew D. Hardeman <mhardemn@papersoft.com> 
 * Adapted from the MySQL CDR logger originally by James Sharp 
 *
 * Modified September 2003
 * Matthew D. Hardeman <mhardemn@papersoft.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <libpq-fe.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk.h"

#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "PostgreSQL CDR Backend";
static char *name = "pgsql";
static char *config = "cdr_pgsql.conf";
static char *pghostname = NULL, *pgdbname = NULL, *pgdbuser = NULL, *pgpassword = NULL, *pgdbsock = NULL, *pgdbport = NULL, *table = NULL;
static int connected = 0;

AST_MUTEX_DEFINE_STATIC(pgsql_lock);

PGconn		*conn;
PGresult	*result;

static int pgsql_log(struct ast_cdr *cdr)
{
	struct tm tm;
	char sqlcmd[2048] = "", timestr[128];
	char *pgerror;

	ast_mutex_lock(&pgsql_lock);

	localtime_r(&cdr->start.tv_sec,&tm);
	strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

	if ((!connected) && pghostname && pgdbuser && pgpassword && pgdbname) {
		conn = PQsetdbLogin(pghostname, pgdbport, NULL, NULL, pgdbname, pgdbuser, pgpassword);
		if (PQstatus(conn) != CONNECTION_BAD) {
			connected = 1;
		} else {
			pgerror = PQerrorMessage(conn);
			ast_log(LOG_ERROR, "cdr_pgsql: Unable to connect to database server %s.  Calls will not be logged!\n", pghostname);
                        ast_log(LOG_ERROR, "cdr_pgsql: Reason: %s\n", pgerror);
		}
	}

	if (connected) {
		char *clid=NULL, *dcontext=NULL, *channel=NULL, *dstchannel=NULL, *lastapp=NULL, *lastdata=NULL;
		char *uniqueid=NULL, *userfield=NULL;

		/* Maximum space needed would be if all characters needed to be escaped, plus a trailing NULL */
		if ((clid = alloca(strlen(cdr->clid) * 2 + 1)) != NULL)
			PQescapeString(clid, cdr->clid, strlen(cdr->clid));
		if ((dcontext = alloca(strlen(cdr->dcontext) * 2 + 1)) != NULL)
			PQescapeString(dcontext, cdr->dcontext, strlen(cdr->dcontext));
		if ((channel = alloca(strlen(cdr->channel) * 2 + 1)) != NULL)
			PQescapeString(channel, cdr->channel, strlen(cdr->channel));
		if ((dstchannel = alloca(strlen(cdr->dstchannel) * 2 + 1)) != NULL)
			PQescapeString(dstchannel, cdr->dstchannel, strlen(cdr->dstchannel));
		if ((lastapp = alloca(strlen(cdr->lastapp) * 2 + 1)) != NULL)
			PQescapeString(lastapp, cdr->lastapp, strlen(cdr->lastapp));
		if ((lastdata = alloca(strlen(cdr->lastdata) * 2 + 1)) != NULL)
			PQescapeString(lastdata, cdr->lastdata, strlen(cdr->lastdata));
		if ((uniqueid = alloca(strlen(cdr->uniqueid) * 2 + 1)) != NULL)
			PQescapeString(uniqueid, cdr->uniqueid, strlen(cdr->uniqueid));
		if ((userfield = alloca(strlen(cdr->userfield) * 2 + 1)) != NULL)
			PQescapeString(userfield, cdr->userfield, strlen(cdr->userfield));

		/* Check for all alloca failures above at once */
		if ((!clid) || (!dcontext) || (!channel) || (!dstchannel) || (!lastapp) || (!lastdata) || (!uniqueid) || (!userfield)) {
			ast_log(LOG_ERROR, "cdr_pgsql:  Out of memory error (insert fails)\n");
			ast_mutex_unlock(&pgsql_lock);
			return -1;
		}

		ast_log(LOG_DEBUG,"cdr_pgsql: inserting a CDR record.\n");

		snprintf(sqlcmd,sizeof(sqlcmd),"INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,"
				 "lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield) VALUES"
				 " ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%d,%d,'%s',%d,'%s','%s','%s')",
				 table,timestr,clid,cdr->src, cdr->dst, dcontext,channel, dstchannel, lastapp, lastdata,
				 cdr->duration,cdr->billsec,ast_cdr_disp2str(cdr->disposition),cdr->amaflags, cdr->accountcode, uniqueid, userfield);
		
		ast_log(LOG_DEBUG,"cdr_pgsql: SQL command executed:  %s\n",sqlcmd);
		
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
				connected = 0;
				ast_mutex_unlock(&pgsql_lock);
				return -1;
			}
		}
		result = PQexec(conn, sqlcmd);
		if ( PQresultStatus(result) != PGRES_COMMAND_OK) {
                        pgerror = PQresultErrorMessage(result);
			ast_log(LOG_ERROR,"cdr_pgsql: Failed to insert call detail record into database!\n");
                        ast_log(LOG_ERROR,"cdr_pgsql: Reason: %s\n", pgerror);
			ast_log(LOG_ERROR,"cdr_pgsql: Connection may have been lost... attempting to reconnect.\n");
			PQreset(conn);
			if (PQstatus(conn) == CONNECTION_OK) {
				ast_log(LOG_ERROR, "cdr_pgsql: Connection reestablished.\n");
				connected = 1;
				result = PQexec(conn, sqlcmd);
				if ( PQresultStatus(result) != PGRES_COMMAND_OK)
				{
					pgerror = PQresultErrorMessage(result);
					ast_log(LOG_ERROR,"cdr_pgsql: HARD ERROR!  Attempted reconnection failed.  DROPPING CALL RECORD!\n");
					ast_log(LOG_ERROR,"cdr_pgsql: Reason: %s\n", pgerror);
				}
			}
			ast_mutex_unlock(&pgsql_lock);
			return -1;
		}
	}
	ast_mutex_unlock(&pgsql_lock);
	return 0;
}

char *description(void)
{
	return desc;
}

static int my_unload_module(void)
{ 
	if (conn)
		PQfinish(conn);
	if (pghostname)
		free(pghostname);
	if (pgdbname)
		free(pgdbname);
	if (pgdbuser)
		free(pgdbuser);
	if (pgdbsock)
		free(pgdbsock);
	if (pgpassword)
		free(pgpassword);
	if (pgdbport)
		free(pgdbport);
	if (table)
		free(table);
	ast_cdr_unregister(name);
	return 0;
}

static int process_my_load_module(struct ast_config *cfg)
{
	int res;
	struct ast_variable *var;
        char *pgerror;
	char *tmp;

	var = ast_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		return 0;
	}

	tmp = ast_variable_retrieve(cfg,"global","hostname");
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"PostgreSQL server hostname not specified.  Assuming localhost\n");
		tmp = "localhost";
	}
	pghostname = strdup(tmp);
	if (pghostname == NULL) {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","dbname");
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"PostgreSQL database not specified.  Assuming asterisk\n");
		tmp = "asteriskcdrdb";
	}
	pgdbname = strdup(tmp);
	if (pgdbname == NULL) {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","user");
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"PostgreSQL database user not specified.  Assuming root\n");
		tmp = "root";
	}
	pgdbuser = strdup(tmp);
	if (pgdbuser == NULL) {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","password");
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"PostgreSQL database password not specified.  Assuming blank\n");
		tmp = "";
	}
	pgpassword = strdup(tmp);
	if (pgpassword == NULL) {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","port");
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"PostgreSQL database port not specified.  Using default 5432.\n");
		tmp = "5432";
	}
	pgdbport = strdup(tmp);
	if (pgdbport == NULL) {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","table");
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"CDR table not specified.  Assuming cdr\n");
		tmp = "cdr";
	}
	table = strdup(tmp);
	if (table == NULL) {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	ast_log(LOG_DEBUG,"cdr_pgsql: got hostname of %s\n",pghostname);
	ast_log(LOG_DEBUG,"cdr_pgsql: got port of %s\n",pgdbport);
	if (pgdbsock)
		ast_log(LOG_DEBUG,"cdr_pgsql: got sock file of %s\n",pgdbsock);
	ast_log(LOG_DEBUG,"cdr_pgsql: got user of %s\n",pgdbuser);
	ast_log(LOG_DEBUG,"cdr_pgsql: got dbname of %s\n",pgdbname);
	ast_log(LOG_DEBUG,"cdr_pgsql: got password of %s\n",pgpassword);
	ast_log(LOG_DEBUG,"cdr_pgsql: got sql table name of %s\n",table);
	
	conn = PQsetdbLogin(pghostname, pgdbport, NULL, NULL, pgdbname, pgdbuser, pgpassword);
	if (PQstatus(conn) != CONNECTION_BAD) {
		ast_log(LOG_DEBUG,"Successfully connected to PostgreSQL database.\n");
		connected = 1;
	} else {
                pgerror = PQerrorMessage(conn);
		ast_log(LOG_ERROR, "cdr_pgsql: Unable to connect to database server %s.  CALLS WILL NOT BE LOGGED!!\n", pghostname);
                ast_log(LOG_ERROR, "cdr_pgsql: Reason: %s\n", pgerror);
		connected = 0;
	}

	res = ast_cdr_register(name, desc, pgsql_log);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register PGSQL CDR handling\n");
	}
	return res;
}

static int my_load_module(void)
{
	struct ast_config *cfg;
	int res;
	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_WARNING, "Unable to load config for PostgreSQL CDR's: %s\n", config);
		return 0;
	}
	res = process_my_load_module(cfg);
	ast_config_destroy(cfg);
	return res;
}

int load_module(void)
{
	return my_load_module();
}

int unload_module(void)
{
	return my_unload_module();
}

int reload(void)
{
	my_unload_module();
	return my_load_module();
}

int usecount(void)
{
	/* To be able to unload the module */
	if ( ast_mutex_trylock(&pgsql_lock) ) {
		return 1;
	} else {
		ast_mutex_unlock(&pgsql_lock);
		return 0;
	}
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
