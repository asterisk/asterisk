/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * unixODBC CDR Backend
 * 
 * Brian K. West <brian@bkw.org>
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

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "unixODBC CDR Backend";
static char *name = "unixODBC";
static char *config = "cdr_unixodbc.conf";
static char *dsn = NULL, *username = NULL, *password = NULL, *loguniqueid = NULL;
static int dsn_alloc = 0, username_alloc = 0, password_alloc = 0;
static int connected = 0;

static ast_mutex_t unixodbc_lock = AST_MUTEX_INITIALIZER;

extern int unixodbc_do_query(char *sqlcmd);
extern int unixodbc_init(void);

static SQLHENV	ODBC_env = SQL_NULL_HANDLE;	/* global ODBC Environment */
static int 	ODBC_res;			/* global ODBC Result of Functions */
static SQLHDBC	ODBC_con;			/* global ODBC Connection Handle */
static SQLHSTMT	ODBC_stmt;			/* global ODBC Statement Handle */

static int unixodbc_log(struct ast_cdr *cdr)
{
	int res;
	/*
        long int ODBC_err, ODBC_id;
        short int ODBC_mlen;
        char ODBC_msg[200], ODBC_buffer[200], ODBC_stat[10];
	*/
	struct tm tm;
	struct timeval tv;
	time_t t;
	char sqlcmd[2048], timestr[128];
	
	ast_mutex_lock(&unixodbc_lock);

	gettimeofday(&tv,NULL);
	t = tv.tv_sec;
	localtime_r(&t,&tm);
	strftime(timestr,128,DATE_FORMAT,&tm);

	memset(sqlcmd,0,2048);

	if((strcmp(loguniqueid, "1") == 0) || (strcmp(loguniqueid, "yes") == 0))
	{
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,%i,%i,'%s','%s')", timestr, cdr->clid, cdr->src, cdr->dst, cdr->dcontext, cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata, cdr->duration, cdr->billsec, cdr->disposition, cdr->amaflags, cdr->accountcode, cdr->uniqueid);
	}
	else
	{
		sprintf(sqlcmd,"INSERT INTO cdr (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,%i,%i,'%s')", timestr, cdr->clid, cdr->src, cdr->dst, cdr->dcontext, cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata, cdr->duration, cdr->billsec, cdr->disposition, cdr->amaflags, cdr->accountcode);
	}

	if(connected)
	{
		res = unixodbc_do_query(sqlcmd);
		if(res < 0)
		{
			if(option_verbose > 3)		
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Query FAILED Call not logged!\n");
			res = unixodbc_init();
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Reconnecting to dsn %s\n", dsn);
			if(res < 0)
			{
				if(option_verbose > 3)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: %s has gone away!\n", dsn);
				connected = 0;
			}
			else
			{
				if(option_verbose > 3)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Trying Query again!\n");
				res = unixodbc_do_query(sqlcmd);
				if(res < 0)
				{
					if(option_verbose > 3)
						ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Query FAILED Call not logged!\n");
				}
			}
		}
	}
	else
	{
		if(option_verbose > 3)
			 ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Reconnecting to dsn %s\n", dsn);
		res = unixodbc_init();
		if(res < 0)
		{
			if(option_verbose > 3)
			{
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: %s has gone away!\n", dsn);
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Call not logged!\n");
			}
		}
		else
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Trying Query again!\n");
			res = unixodbc_do_query(sqlcmd);
			if(res < 0)
			{
				if(option_verbose > 3)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Query FAILED Call not logged!\n");
			}
		}
	}
	ast_mutex_unlock(&unixodbc_lock);
	return 0;
}

char *description(void)
{
	return desc;
}

static int unixodbc_unload_module(void)
{
	if (connected)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Disconnecting from %s\n", dsn);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		SQLDisconnect(ODBC_con);
		SQLFreeHandle(SQL_HANDLE_DBC, ODBC_con);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
	}
	if (dsn && dsn_alloc)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: free dsn\n");
		free(dsn);
		dsn = NULL;
		dsn_alloc = 0;
	}
	if (username && username_alloc)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: free username\n");
		free(username);
		username = NULL;
		username_alloc = 0;
	}
	if (password && password_alloc)
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: free password\n");
		free(password);
		password = NULL;
		password_alloc = 0;
	}
	ast_cdr_unregister(name);
	return 0;
}

static int unixodbc_load_module(void)
{
	int res;
	struct ast_config *cfg;
	struct ast_variable *var;
	char *tmp;

	cfg = ast_load(config);
	if (!cfg)
	{
		ast_log(LOG_WARNING, "cdr_unixodbc: Unable to load config for unixODBC CDR's: %s\n", config);
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
			ast_log(LOG_ERROR,"cdr_unixodbc: Out of memory error.\n");
			return -1;
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_unixodbc: dsn not specified.  Assuming asteriskdb\n");
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
			ast_log(LOG_ERROR,"cdr_unixodbc: Out of memory error.\n");
			return -1;
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_unixodbc: username not specified.  Assuming root\n");
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
			ast_log(LOG_ERROR,"cdr_unixodbc: Out of memory error.\n");
			return -1;
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_unixodbc: database password not specified.  Assuming blank\n");
		password = "";
	}

	tmp = ast_variable_retrieve(cfg,"global","loguniqueid");
	if (tmp)
	{
		loguniqueid = malloc(strlen(tmp) + 1);
		if (loguniqueid != NULL)
		{
			strcpy(loguniqueid,tmp);
			ast_log(LOG_WARNING,"cdr_unixodbc: Logging uniqueid\n");
		}
		else
		{
			ast_log(LOG_ERROR,"cdr_unixodbc: Not logging uniqueid\n");
		}
	}
	else
	{
		ast_log(LOG_WARNING,"cdr_unixodbc: Not logging uniqueid\n");
		loguniqueid = NULL;
	}

	ast_destroy(cfg);
	if(option_verbose > 3)
	{
		ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: dsn is %s\n",dsn);
		ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: username is %s\n",username);
		ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: password is [secret]\n");

	}
	
	res = unixodbc_init();
	if(res < 0)
	{
		ast_log(LOG_ERROR, "cdr_unixodbc: Unable to connect to datasource: %s\n", dsn);
		ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Unable to connect to datasource: %s\n", dsn);
	}

	res = ast_cdr_register(name, desc, unixodbc_log);
	if (res)
	{
		ast_log(LOG_ERROR, "cdr_unixodbc: Unable to register unixODBC CDR handling\n");
	}
	return res;
}

int unixodbc_do_query(char *sqlcmd)
{
        long int ODBC_err;
        short int ODBC_mlen;
        char ODBC_msg[200], ODBC_stat[10];

	ODBC_res = SQLAllocHandle(SQL_HANDLE_STMT, ODBC_con, &ODBC_stmt);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Failure in AllocStatement %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);	
		connected = 0;
		return -1;
	}

	ODBC_res = SQLPrepare(ODBC_stmt, sqlcmd, SQL_NTS);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Error in PREPARE %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		return -1;
	}

	ODBC_res = SQLExecute(ODBC_stmt);

	if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Error in Query %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		connected = 0;
		return -1;
	}
	else
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Query Successful!\n");
		connected = 1;
	}
	return 0;
}

int unixodbc_init()
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
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Error AllocHandle\n");
			connected = 0;
			return -1;
		}

		ODBC_res = SQLSetEnvAttr(ODBC_env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);

		if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Error SetEnv\n");
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}

		ODBC_res = SQLAllocHandle(SQL_HANDLE_DBC, ODBC_env, &ODBC_con);

		if((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO))
		{
			if(option_verbose > 3)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Error AllocHDB %d\n", ODBC_res);
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
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Error SQLConnect %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
		return -1;
	}
	else
	{
		if(option_verbose > 3)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_unixodbc: Connected to %s\n", dsn);
		connected = 1;
	}

	return 0;
}

int load_module(void)
{
	return unixodbc_load_module();
}

int unload_module(void)
{
	return unixodbc_unload_module();
}

int reload(void)
{
	unixodbc_unload_module();
	return unixodbc_load_module();
}

int usecount(void)
{
	return connected;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
