/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ODBC CDR Backend
 * 
 * Brian K. West <brian@bkw.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Copyright (c) 2003 Digium, Inc.
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

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "ODBC CDR Backend";
static char *name = "ODBC";
static char *config = "cdr_odbc.conf";
static char *dsn = NULL, *username = NULL, *password = NULL, *loguniqueid = NULL;
static int dsn_alloc = 0, username_alloc = 0, password_alloc = 0;
static int connected = 0;

static ast_mutex_t odbc_lock = AST_MUTEX_INITIALIZER;

static int odbc_do_query(char *sqlcmd);
static int odbc_init(void);
static size_t escape_string(char *to, const char *from, size_t length);

static SQLHENV	ODBC_env = SQL_NULL_HANDLE;	/* global ODBC Environment */
static int 	ODBC_res;			/* global ODBC Result of Functions */
static SQLHDBC	ODBC_con;			/* global ODBC Connection Handle */
static SQLHSTMT	ODBC_stmt;			/* global ODBC Statement Handle */

static int odbc_log(struct ast_cdr *cdr)
{
	int res;
	struct tm tm;
	struct timeval tv;
	time_t t;
	char sqlcmd[2048], timestr[128];
	char *clid=NULL, *dcontext=NULL, *channel=NULL, *dstchannel=NULL, *lastapp=NULL, *lastdata=NULL, *uniqueid=NULL;
	
	ast_mutex_lock(&odbc_lock);

	gettimeofday(&tv,NULL);
	t = tv.tv_sec;
	localtime_r(&t,&tm);
	strftime(timestr,128,DATE_FORMAT,&tm);

	memset(sqlcmd,0,2048);

	if((clid = alloca(strlen(cdr->clid) * 2 + 1)) != NULL)
		escape_string(clid, cdr->clid, strlen(cdr->clid));
	if((dcontext = alloca(strlen(cdr->dcontext) * 2 + 1)) != NULL)
		escape_string(dcontext, cdr->dcontext, strlen(cdr->dcontext));
	if((channel = alloca(strlen(cdr->channel) * 2 + 1)) != NULL)
		escape_string(channel, cdr->channel, strlen(cdr->channel));
	if((dstchannel = alloca(strlen(cdr->dstchannel) * 2 + 1)) != NULL)
		escape_string(dstchannel, cdr->dstchannel, strlen(cdr->dstchannel));
	if((lastapp = alloca(strlen(cdr->lastapp) * 2 + 1)) != NULL)
		escape_string(lastapp, cdr->lastapp, strlen(cdr->lastapp));
	if((lastdata = alloca(strlen(cdr->lastdata) * 2 + 1)) != NULL)
        	escape_string(lastdata, cdr->lastdata, strlen(cdr->lastdata));
	if((uniqueid = alloca(strlen(cdr->uniqueid) * 2 + 1)) != NULL)
		escape_string(uniqueid, cdr->uniqueid, strlen(cdr->uniqueid));

	if ((!clid) || (!dcontext) || (!channel) || (!dstchannel) || (!lastapp) || (!lastdata) || (!uniqueid))
	{
		ast_log(LOG_ERROR, "cdr_odbc:  Out of memory error (insert fails)\n");
		ast_mutex_unlock(&odbc_lock);
		return -1;
	}

	if((strcmp(loguniqueid, "1") == 0) || (strcmp(loguniqueid, "yes") == 0))
	{
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid) VALUES ('%s','%s','%s','%s','%s','%s','%s','%s','%s',%i,%i,%i,%i,'%s','%s')", timestr, clid, cdr->src, cdr->dst, dcontext, channel, dstchannel, lastapp, lastdata, cdr->duration, cdr->billsec, cdr->disposition, cdr->amaflags, cdr->accountcode, uniqueid);
	}
	else
	{
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode) VALUES ('%s','%s','%s','%s','%s','%s','%s','%s','%s',%i,%i,%i,%i,'%s')", timestr, clid, cdr->src, cdr->dst, dcontext, channel, dstchannel, lastapp, lastdata, cdr->duration, cdr->billsec, cdr->disposition, cdr->amaflags, cdr->accountcode);
	}

	if(connected)
	{
		res = odbc_do_query(sqlcmd);
		if(res < 0)
		{
			if(option_verbose > 3)		
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
			res = odbc_init();
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Reconnecting to dsn %s\n", dsn);
			if(res < 0)
			{
				if(option_verbose > 3)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: %s has gone away!\n", dsn);
				connected = 0;
			}
			else
			{
				if(option_verbose > 3)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Trying Query again!\n");
				res = odbc_do_query(sqlcmd);
				if(res < 0)
				{
					if(option_verbose > 3)
						ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
				}
			}
		}
	}
	else
	{
		if(option_verbose > 3)
			 ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Reconnecting to dsn %s\n", dsn);
		res = odbc_init();
		if(res < 0)
		{
			if(option_verbose > 3)
			{
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: %s has gone away!\n", dsn);
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Call not logged!\n");
			}
		}
		else
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Trying Query again!\n");
			res = odbc_do_query(sqlcmd);
			if(res < 0)
			{
				if(option_verbose > 3)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
			}
		}
	}
	ast_mutex_unlock(&odbc_lock);
	return 0;
}

char *description(void)
{
	return desc;
}

static int odbc_unload_module(void)
{
	if (connected)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Disconnecting from %s\n", dsn);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		SQLDisconnect(ODBC_con);
		SQLFreeHandle(SQL_HANDLE_DBC, ODBC_con);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
	}
	if (dsn && dsn_alloc)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free dsn\n");
		free(dsn);
		dsn = NULL;
		dsn_alloc = 0;
	}
	if (username && username_alloc)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free username\n");
		free(username);
		username = NULL;
		username_alloc = 0;
	}
	if (password && password_alloc)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free password\n");
		free(password);
		password = NULL;
		password_alloc = 0;
	}
	ast_cdr_unregister(name);
	return 0;
}

static int odbc_load_module(void)
{
	int res;
	struct ast_config *cfg;
	struct ast_variable *var;
	char *tmp;

	cfg = ast_load(config);
	if (!cfg)
	{
		ast_log(LOG_WARNING, "cdr_odbc: Unable to load config for ODBC CDR's: %s\n", config);
		return 0;
	}
	
	var = ast_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		return 0;
	}

	tmp = ast_variable_retrieve(cfg,"global","dsn");
	if (tmp)
	{
		dsn = malloc(strlen(tmp) + 1);
		if (dsn != NULL)
		{
			dsn_alloc = 1;
			strcpy(dsn,tmp);
		}
		else
		{
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			return -1;
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_odbc: dsn not specified.  Assuming asteriskdb\n");
		dsn = "asteriskdb";
	}

	tmp = ast_variable_retrieve(cfg,"global","username");
	if (tmp)
	{
		username = malloc(strlen(tmp) + 1);
		if (username != NULL)
		{
			username_alloc = 1;
			strcpy(username,tmp);
		}
		else
		{
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			return -1;
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_odbc: username not specified.  Assuming root\n");
		username = "root";
	}

	tmp = ast_variable_retrieve(cfg,"global","password");
	if (tmp)
	{
		password = malloc(strlen(tmp) + 1);
		if (password != NULL)
		{
			password_alloc = 1;
			strcpy(password,tmp);
		}
		else
		{
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			return -1;
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_odbc: database password not specified.  Assuming blank\n");
		password = "";
	}

	tmp = ast_variable_retrieve(cfg,"global","loguniqueid");
	if (tmp)
	{
		loguniqueid = malloc(strlen(tmp) + 1);
		if (loguniqueid != NULL)
		{
			strcpy(loguniqueid,tmp);
			ast_log(LOG_WARNING,"cdr_odbc: Logging uniqueid\n");
		}
		else
		{
			ast_log(LOG_ERROR,"cdr_odbc: Not logging uniqueid\n");
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_odbc: Not logging uniqueid\n");
		loguniqueid = NULL;
	}

	ast_destroy(cfg);
	if(option_verbose > 3)
	{
		ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: dsn is %s\n",dsn);
		ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: username is %s\n",username);
		ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: password is [secret]\n");

	}
	
	res = odbc_init();
	if(res < 0)
	{
		ast_log(LOG_ERROR, "cdr_odbc: Unable to connect to datasource: %s\n", dsn);
		ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Unable to connect to datasource: %s\n", dsn);
	}

	res = ast_cdr_register(name, desc, odbc_log);
	if (res)
	{
		ast_log(LOG_ERROR, "cdr_odbc: Unable to register ODBC CDR handling\n");
	}
	return res;
}

static int odbc_do_query(char *sqlcmd)
{
        long int ODBC_err;
        short int ODBC_mlen;
        char ODBC_msg[200], ODBC_stat[10];

	ODBC_res = SQLAllocHandle(SQL_HANDLE_STMT, ODBC_con, &ODBC_stmt);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Failure in AllocStatement %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);	
		connected = 0;
		return -1;
	}

	ODBC_res = SQLPrepare(ODBC_stmt, sqlcmd, SQL_NTS);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error in PREPARE %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		return -1;
	}

	ODBC_res = SQLExecute(ODBC_stmt);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error in Query %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		connected = 0;
		return -1;
	}
	else
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query Successful!\n");
		connected = 1;
	}
	return 0;
}

static int odbc_init(void)
{
	long int ODBC_err;
	short int ODBC_mlen;
	char ODBC_msg[200], ODBC_stat[10];

	if ( ODBC_env == SQL_NULL_HANDLE || connected == 0)
	{
		ODBC_res = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &ODBC_env);

		if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error AllocHandle\n");
			connected = 0;
			return -1;
		}

		ODBC_res = SQLSetEnvAttr(ODBC_env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);

		if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error SetEnv\n");
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}

		ODBC_res = SQLAllocHandle(SQL_HANDLE_DBC, ODBC_env, &ODBC_con);

		if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error AllocHDB %d\n", ODBC_res);
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}

		SQLSetConnectAttr(ODBC_con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)10, 0);	
	}

	ODBC_res = SQLConnect(ODBC_con, (SQLCHAR*)dsn, SQL_NTS, (SQLCHAR*)username, SQL_NTS, (SQLCHAR*)password, SQL_NTS);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error SQLConnect %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
		return -1;
	}
	else
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Connected to %s\n", dsn);
		connected = 1;
	}

	return 0;
}

static size_t escape_string(char *to, const char *from, size_t length)
{
	const char *source = from;
	char *target = to;
	unsigned int remaining = length;
	while (remaining > 0) {
		switch (*source) {
			case '\\':
				*target = '\\';
				target++;
				*target = '\\';
				break;
			case '\'':
				*target = '\\';
				target++;
				*target = '\'';
				break;
			 case '"':
				*target = '\\';
				target++;
				*target = '"';
				break;
			default:
				*target = *source;
			}
		source++;
		target++;
		remaining--;
	}

	*target = '\0';
 
	return target - to;
}

int load_module(void)
{
	return odbc_load_module();
}

int unload_module(void)
{
	return odbc_unload_module();
}

int reload(void)
{
	odbc_unload_module();
	return odbc_load_module();
}

int usecount(void)
{
	return connected;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
