/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MySQL CDR logger 
 * 
 * James Sharp <jsharp@psychoses.org>
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
#include <mysql/errmsg.h>

#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "MySQL CDR Backend";
static char *name = "mysql";
static char *config = "cdr_mysql.conf";
static char *hostname = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL;
static int connected = 0;

static pthread_mutex_t mysql_lock = AST_MUTEX_INITIALIZER;

static MYSQL mysql;

static int mysql_log(struct ast_cdr *cdr)
{
	struct tm tm;
	struct timeval tv;
	char sqlcmd[2048], timestr[128];
	time_t t;

	ast_pthread_mutex_lock(&mysql_lock);

	memset(sqlcmd,0,2048);

	gettimeofday(&tv,NULL);
	t = tv.tv_sec;
	localtime_r(&t,&tm);
	strftime(timestr,128,DATE_FORMAT,&tm);

	if ((!connected) && hostname && dbuser && password && dbname) {
		/* Attempt to connect */
		mysql_init(&mysql);
		if (mysql_real_connect(&mysql, hostname, dbuser, password, dbname, 0, NULL, 0)) {
			connected = 1;
		} else {
			ast_log(LOG_ERROR, "cdr_mysql: cannot connect to database %s.  Call will not be logged\n", hostname);
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
		ast_log(LOG_DEBUG,"cdr_mysql: inserting a CDR record.\n");
#ifdef MYSQL_LOGUNIQUEID
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s','%s')",timestr,cdr->clid,cdr->src, cdr->dst, cdr->dcontext,cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata,cdr->duration,cdr->billsec,ast_cdr_disp2str(cdr->disposition),cdr->amaflags, cdr->accountcode, cdr->uniqueid);
#else
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s')",timestr,cdr->clid,cdr->src, cdr->dst, cdr->dcontext,cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata,cdr->duration,cdr->billsec,ast_cdr_disp2str(cdr->disposition),cdr->amaflags, cdr->accountcode);
#endif  
		ast_log(LOG_DEBUG,"cdr_mysql: SQL command as follows:  %s\n",sqlcmd);
	
		if (mysql_real_query(&mysql,sqlcmd,strlen(sqlcmd))) {
			ast_log(LOG_ERROR,"Failed to insert into database.");
			ast_pthread_mutex_unlock(&mysql_lock);
			return -1;
		}
	}
	ast_pthread_mutex_unlock(&mysql_lock);
	return 0;
}

char *description(void)
{
	return desc;
}

int unload_module(void)
{ 
	if (connected) {
		mysql_close(&mysql);
		connected = 0;
	}
	if (hostname) {
		free(hostname);
		hostname = NULL;
	}
	if (dbname) {
		free(dbname);
		dbname = NULL;
	}
	if (dbuser) {
		free(dbuser);
		dbuser = NULL;
	}
	if (password) {
		free(password);
		password = NULL;
	}
	ast_cdr_unregister(name);
	return 0;
}

int load_module(void)
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
	hostname = malloc(strlen(tmp) + 1);
	if ((tmp != NULL) && (hostname != NULL)) {
		strcpy(hostname,tmp);
	} else if (tmp == NULL) {
		ast_log(LOG_ERROR,"Database server hostname not specified.\n");
		return -1;
	} else {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","dbname");
	dbname = malloc(strlen(tmp) + 1);
	if ((tmp != NULL) && (dbname != NULL)) {
		strcpy(dbname,tmp);
	} else if (tmp == NULL) {
		ast_log(LOG_ERROR,"Database dbname not specified.\n");
		return -1;
	} else {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","user");
	dbuser = malloc(strlen(tmp) + 1);
	if ((tmp != NULL) && (dbuser != NULL)) {
		strcpy(dbuser,tmp);
	} else if (tmp == NULL) {
		ast_log(LOG_ERROR,"Database dbuser not specified.\n");
		return -1;
	} else {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	tmp = ast_variable_retrieve(cfg,"global","password");
	password = malloc(strlen(tmp) + 1);
	if ((tmp != NULL) && (password != NULL)) {
		strcpy(password,tmp);
	} else if (tmp == NULL) {
		ast_log(LOG_ERROR,"Database password not specified.\n");
		return -1;
	} else {
		ast_log(LOG_ERROR,"Out of memory error.\n");
		return -1;
	}

	ast_destroy(cfg);

	ast_log(LOG_DEBUG,"cdr_mysql: got hostname of %s\n",hostname);
	ast_log(LOG_DEBUG,"cdr_mysql: got user of %s\n",dbuser);
	ast_log(LOG_DEBUG,"cdr_mysql: got dbname of %s\n",dbname);
	ast_log(LOG_DEBUG,"cdr_mysql: got password of %s\n",password);

	mysql_init(&mysql);

	if (!mysql_real_connect(&mysql, hostname, dbuser, password, dbname, 0, NULL, 0)) {
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

int reload(void)
{
	unload_module();
	return load_module();
}

int usecount(void)
{
	return connected;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
