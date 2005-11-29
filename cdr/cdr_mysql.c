/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MySQL CDR logger 
 * 
 * James Sharp <jsharp@psychoses.org>
 *
 * Modified August 2003
 * Tilghman Lesher <asterisk__cdr__cdr_mysql__200308@the-tilghman.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 */

#include <sys/types.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include "../asterisk.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <mysql.h>
#include <errmsg.h>

#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "MySQL CDR Backend";
static char *name = "mysql";
static char *config = "cdr_mysql.conf";
static char *hostname = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL, *dbsock = NULL;
static int hostname_alloc = 0, dbname_alloc = 0, dbuser_alloc = 0, password_alloc = 0, dbsock_alloc = 0;
static int dbport = 0;
static int connected = 0;

static ast_mutex_t mysql_lock = AST_MUTEX_INITIALIZER;

static MYSQL mysql;

static int mysql_log(struct ast_cdr *cdr)
{
	struct tm tm;
	struct timeval tv;
	char sqlcmd[2048], timestr[128];
	time_t t;

	ast_mutex_lock(&mysql_lock);

	memset(sqlcmd,0,2048);

	gettimeofday(&tv,NULL);
	t = tv.tv_sec;
	localtime_r(&t,&tm);
	strftime(timestr,128,DATE_FORMAT,&tm);

	if ((!connected) && (hostname || dbsock) && dbuser && password && dbname) {
		/* Attempt to connect */
		mysql_init(&mysql);
		if (mysql_real_connect(&mysql, hostname, dbuser, password, dbname, dbport, dbsock, 0)) {
			connected = 1;
		} else {
			ast_log(LOG_ERROR, "cdr_mysql: cannot connect to database server %s.  Call will not be logged\n", hostname);
		}
	} else {
		/* Long connection - ping the server */
		int error;
		if ((error = mysql_ping(&mysql))) {
			connected = 0;
			switch (error) {
				case CR_SERVER_GONE_ERROR:
					ast_log(LOG_ERROR, "cdr_mysql: Server has gone away\n");
					break;
				default:
					ast_log(LOG_ERROR, "cdr_mysql: Unknown connection error\n");
			}
		}
	}

	if (connected) {
		char *clid=NULL, *dcontext=NULL, *channel=NULL, *dstchannel=NULL, *lastapp=NULL, *lastdata=NULL;
#ifdef MYSQL_LOGUNIQUEID
		char *uniqueid=NULL;
#endif

		/* Maximum space needed would be if all characters needed to be escaped, plus a trailing NULL */
		if ((clid = alloca(strlen(cdr->clid) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, clid, cdr->clid, strlen(cdr->clid));
		if ((dcontext = alloca(strlen(cdr->dcontext) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, dcontext, cdr->dcontext, strlen(cdr->dcontext));
		if ((channel = alloca(strlen(cdr->channel) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, channel, cdr->channel, strlen(cdr->channel));
		if ((dstchannel = alloca(strlen(cdr->dstchannel) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, dstchannel, cdr->dstchannel, strlen(cdr->dstchannel));
		if ((lastapp = alloca(strlen(cdr->lastapp) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, lastapp, cdr->lastapp, strlen(cdr->lastapp));
		if ((lastdata = alloca(strlen(cdr->lastdata) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, lastdata, cdr->lastdata, strlen(cdr->lastdata));
#ifdef MYSQL_LOGUNIQUEID
		if ((uniqueid = alloca(strlen(cdr->uniqueid) * 2 + 1)) != NULL)
			mysql_real_escape_string(&mysql, uniqueid, cdr->uniqueid, strlen(cdr->uniqueid));
#endif

		/* Check for all alloca failures above at once */
#ifdef MYSQL_LOGUNIQUEID
		if ((!clid) || (!dcontext) || (!channel) || (!dstchannel) || (!lastapp) || (!lastdata) || (!uniqueid)) {
#else
		if ((!clid) || (!dcontext) || (!channel) || (!dstchannel) || (!lastapp) || (!lastdata)) {
#endif
			ast_log(LOG_ERROR, "cdr_mysql:  Out of memory error (insert fails)\n");
			ast_mutex_unlock(&mysql_lock);
			return -1;
		}

		ast_log(LOG_DEBUG,"cdr_mysql: inserting a CDR record.\n");

#ifdef MYSQL_LOGUNIQUEID
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s','%s')",timestr,clid,cdr->src, cdr->dst, dcontext,channel, dstchannel, lastapp, lastdata,cdr->duration,cdr->billsec,ast_cdr_disp2str(cdr->disposition),cdr->amaflags, cdr->accountcode, uniqueid);
#else
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s')",timestr,clid,cdr->src, cdr->dst, dcontext,channel, dstchannel, lastapp, lastdata,cdr->duration,cdr->billsec,ast_cdr_disp2str(cdr->disposition),cdr->amaflags, cdr->accountcode);
#endif  
		ast_log(LOG_DEBUG,"cdr_mysql: SQL command as follows:  %s\n",sqlcmd);
	
		if (mysql_real_query(&mysql,sqlcmd,strlen(sqlcmd))) {
			ast_log(LOG_ERROR,"Failed to insert into database.");
			ast_mutex_unlock(&mysql_lock);
			return -1;
		}
	}
	ast_mutex_unlock(&mysql_lock);
	return 0;
}

char *description(void)
{
	return desc;
}

static int my_unload_module(void)
{ 
	if (connected) {
		mysql_close(&mysql);
		connected = 0;
	}
	if (hostname && hostname_alloc) {
		free(hostname);
		hostname = NULL;
		hostname_alloc = 0;
	}
	if (dbname && dbname_alloc) {
		free(dbname);
		dbname = NULL;
		dbname_alloc = 0;
	}
	if (dbuser && dbuser_alloc) {
		free(dbuser);
		dbuser = NULL;
		dbuser_alloc = 0;
	}
	if (dbsock && dbsock_alloc) {
		free(dbsock);
		dbsock = NULL;
		dbsock_alloc = 0;
	}
	if (password && password_alloc) {
		free(password);
		password = NULL;
		password_alloc = 0;
	}
	dbport = 0;
	ast_cdr_unregister(name);
	return 0;
}

static int my_load_module(void)
{
	int res;
	struct ast_config *cfg;
	struct ast_variable *var;
	char *tmp;

	cfg = ast_load(config);
	if (!cfg) {
		ast_log(LOG_WARNING, "Unable to load config for mysql CDR's: %s\n", config);
		return 0;
	}
	
	var = ast_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		return 0;
	}

	tmp = ast_variable_retrieve(cfg,"global","hostname");
	if (tmp) {
		hostname = malloc(strlen(tmp) + 1);
		if (hostname != NULL) {
			hostname_alloc = 1;
			strcpy(hostname,tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"MySQL server hostname not specified.  Assuming localhost\n");
		hostname = "localhost";
	}

	tmp = ast_variable_retrieve(cfg,"global","dbname");
	if (tmp) {
		dbname = malloc(strlen(tmp) + 1);
		if (dbname != NULL) {
			dbname_alloc = 1;
			strcpy(dbname,tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"MySQL database not specified.  Assuming asteriskcdrdb\n");
		dbname = "asteriskcdrdb";
	}

	tmp = ast_variable_retrieve(cfg,"global","user");
	if (tmp) {
		dbuser = malloc(strlen(tmp) + 1);
		if (dbuser != NULL) {
			dbuser_alloc = 1;
			strcpy(dbuser,tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"MySQL database user not specified.  Assuming root\n");
		dbuser = "root";
	}

	tmp = ast_variable_retrieve(cfg,"global","sock");
	if (tmp) {
		dbsock = malloc(strlen(tmp) + 1);
		if (dbsock != NULL) {
			dbsock_alloc = 1;
			strcpy(dbsock,tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"MySQL database sock file not specified.  Using default\n");
		dbsock = NULL;
	}

	tmp = ast_variable_retrieve(cfg,"global","password");
	if (tmp) {
		password = malloc(strlen(tmp) + 1);
		if (password != NULL) {
			password_alloc = 1;
			strcpy(password,tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"MySQL database password not specified.  Assuming blank\n");
		password = "";
	}

	tmp = ast_variable_retrieve(cfg,"global","port");
	if (tmp) {
		if (sscanf(tmp,"%d",&dbport) < 1) {
			ast_log(LOG_WARNING,"Invalid MySQL port number.  Using default\n");
			dbport = 0;
		}
	}

	ast_destroy(cfg);

	ast_log(LOG_DEBUG,"cdr_mysql: got hostname of %s\n",hostname);
	ast_log(LOG_DEBUG,"cdr_mysql: got port of %d\n",dbport);
	if (dbsock)
		ast_log(LOG_DEBUG,"cdr_mysql: got sock file of %s\n",dbsock);
	ast_log(LOG_DEBUG,"cdr_mysql: got user of %s\n",dbuser);
	ast_log(LOG_DEBUG,"cdr_mysql: got dbname of %s\n",dbname);
	ast_log(LOG_DEBUG,"cdr_mysql: got password of %s\n",password);

	mysql_init(&mysql);

	if (!mysql_real_connect(&mysql, hostname, dbuser, password, dbname, dbport, dbsock, 0)) {
		ast_log(LOG_ERROR, "Failed to connect to mysql database %s on %s.\n", dbname, hostname);
		connected = 0;
	} else {
		ast_log(LOG_DEBUG,"Successfully connected to MySQL database.\n");
		connected = 1;
	}

	res = ast_cdr_register(name, desc, mysql_log);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register MySQL CDR handling\n");
	}
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
	return connected;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
