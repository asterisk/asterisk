/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ODBC CDR Backend
 * 
 * Copyright (C) 2003-2005, Digium, Inc.
 *
 * Brian K. West <brian@bkw.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk.h"

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
static char *dsn = NULL, *username = NULL, *password = NULL, *table = NULL;
static int dsn_alloc = 0, username_alloc = 0, password_alloc = 0, table_alloc = 0;
static int loguniqueid = 0;
static int usegmtime = 0;
static int dispositionstring = 0;
static int connected = 0;

AST_MUTEX_DEFINE_STATIC(odbc_lock);

static int odbc_do_query(void);
static int odbc_init(void);

static SQLHENV	ODBC_env = SQL_NULL_HANDLE;	/* global ODBC Environment */
static SQLHDBC	ODBC_con;			/* global ODBC Connection Handle */
static SQLHSTMT	ODBC_stmt;			/* global ODBC Statement Handle */

static int odbc_log(struct ast_cdr *cdr)
{
	SQLINTEGER ODBC_err;
	short int ODBC_mlen;
	int ODBC_res;
	char ODBC_msg[200], ODBC_stat[10];
	char sqlcmd[2048] = "", timestr[128];
	int res = 0;
	struct tm tm;

	if (usegmtime) 
		gmtime_r(&cdr->start.tv_sec,&tm);
	else
		localtime_r(&cdr->start.tv_sec,&tm);

	ast_mutex_lock(&odbc_lock);
	strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);
	memset(sqlcmd,0,2048);
	if (loguniqueid) {
		snprintf(sqlcmd,sizeof(sqlcmd),"INSERT INTO %s "
		"(calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,"
		"lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield) "
		"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", table);
	} else {
		snprintf(sqlcmd,sizeof(sqlcmd),"INSERT INTO %s "
		"(calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,"
		"duration,billsec,disposition,amaflags,accountcode) "
		"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)", table);
	}

	if (!connected) {
		res = odbc_init();
		if (res < 0) {
			connected = 0;
			ast_mutex_unlock(&odbc_lock);
			return 0;
		}				
	}

	ODBC_res = SQLAllocHandle(SQL_HANDLE_STMT, ODBC_con, &ODBC_stmt);

	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Failure in AllocStatement %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);	
		connected = 0;
		ast_mutex_unlock(&odbc_lock);
		return 0;
	}

	/* We really should only have to do this once.  But for some
	   strange reason if I don't it blows holes in memory like
	   like a shotgun.  So we just do this so its safe. */

	ODBC_res = SQLPrepare(ODBC_stmt, sqlcmd, SQL_NTS);
	
	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error in PREPARE %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		connected = 0;
		ast_mutex_unlock(&odbc_lock);
		return 0;
	}

	SQLBindParameter(ODBC_stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(timestr), 0, &timestr, 0, NULL);
	SQLBindParameter(ODBC_stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->clid), 0, cdr->clid, 0, NULL);
	SQLBindParameter(ODBC_stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->src), 0, cdr->src, 0, NULL);
	SQLBindParameter(ODBC_stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dst), 0, cdr->dst, 0, NULL);
	SQLBindParameter(ODBC_stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dcontext), 0, cdr->dcontext, 0, NULL);
	SQLBindParameter(ODBC_stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->channel), 0, cdr->channel, 0, NULL);
	SQLBindParameter(ODBC_stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dstchannel), 0, cdr->dstchannel, 0, NULL);
	SQLBindParameter(ODBC_stmt, 8, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->lastapp), 0, cdr->lastapp, 0, NULL);
	SQLBindParameter(ODBC_stmt, 9, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->lastdata), 0, cdr->lastdata, 0, NULL);
	SQLBindParameter(ODBC_stmt, 10, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->duration, 0, NULL);
	SQLBindParameter(ODBC_stmt, 11, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->billsec, 0, NULL);
	if (dispositionstring)
		SQLBindParameter(ODBC_stmt, 12, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(ast_cdr_disp2str(cdr->disposition)) + 1, 0, ast_cdr_disp2str(cdr->disposition), 0, NULL);
	else
		SQLBindParameter(ODBC_stmt, 12, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->disposition, 0, NULL);
	SQLBindParameter(ODBC_stmt, 13, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->amaflags, 0, NULL);
	SQLBindParameter(ODBC_stmt, 14, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->accountcode), 0, cdr->accountcode, 0, NULL);

	if (loguniqueid) {
		SQLBindParameter(ODBC_stmt, 15, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->uniqueid), 0, cdr->uniqueid, 0, NULL);
		SQLBindParameter(ODBC_stmt, 16, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->userfield), 0, cdr->userfield, 0, NULL);
	}

	if (connected) {
		res = odbc_do_query();
		if (res < 0) {
			if (option_verbose > 10)		
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
			res = odbc_init();
			if (option_verbose > 10)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Reconnecting to dsn %s\n", dsn);
			if (res < 0) {
				if (option_verbose > 10)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: %s has gone away!\n", dsn);
				connected = 0;
			} else {
				if (option_verbose > 10)
					ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Trying Query again!\n");
				res = odbc_do_query();
				if (res < 0) {
					if (option_verbose > 10)
						ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
				}
			}
		}
	} else {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query FAILED Call not logged!\n");
	}
	SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
	ast_mutex_unlock(&odbc_lock);
	return 0;
}

char *description(void)
{
	return desc;
}

static int odbc_unload_module(void)
{
	ast_mutex_lock(&odbc_lock);
	if (connected) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Disconnecting from %s\n", dsn);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		SQLDisconnect(ODBC_con);
		SQLFreeHandle(SQL_HANDLE_DBC, ODBC_con);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
	}
	if (dsn && dsn_alloc) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free dsn\n");
		free(dsn);
		dsn = NULL;
		dsn_alloc = 0;
	}
	if (username && username_alloc) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free username\n");
		free(username);
		username = NULL;
		username_alloc = 0;
	}
	if (password && password_alloc) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free password\n");
		free(password);
		password = NULL;
		password_alloc = 0;
	}
	if (table && table_alloc) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: free table\n");
		free(table);
		table = NULL;
		table_alloc = 0;
	}
	loguniqueid = 0;
	usegmtime = 0;
	dispositionstring = 0;

	ast_cdr_unregister(name);
	ast_mutex_unlock(&odbc_lock);
	return 0;
}

static int odbc_load_module(void)
{
	int res = 0;
	struct ast_config *cfg;
	struct ast_variable *var;
	char *tmp;

	ast_mutex_lock(&odbc_lock);

	cfg = ast_config_load(config);
	if (!cfg) {
		ast_log(LOG_WARNING, "cdr_odbc: Unable to load config for ODBC CDR's: %s\n", config);
		goto out;
	}
	
	var = ast_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		goto out;
	}

	tmp = ast_variable_retrieve(cfg,"global","dsn");
	if (tmp) {
		dsn = malloc(strlen(tmp) + 1);
		if (dsn != NULL) {
			memset(dsn, 0, strlen(tmp) + 1);
			dsn_alloc = 1;
			strncpy(dsn, tmp, strlen(tmp));
		} else {
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			res = -1;
			goto out;
		}
	} else {
		ast_log(LOG_WARNING,"cdr_odbc: dsn not specified.  Assuming asteriskdb\n");
		dsn = "asteriskdb";
	}

	tmp = ast_variable_retrieve(cfg,"global","dispositionstring");
	if (tmp) {
		dispositionstring = ast_true(tmp);
	} else {
		dispositionstring = 0;
	}
		
	tmp = ast_variable_retrieve(cfg,"global","username");
	if (tmp) {
		username = malloc(strlen(tmp) + 1);
		if (username != NULL) {
			memset(username, 0, strlen(tmp) + 1);
			username_alloc = 1;
			strncpy(username, tmp, strlen(tmp));
		} else {
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			res = -1;
			goto out;
		}
	}

	tmp = ast_variable_retrieve(cfg,"global","password");
	if (tmp) {
		password = malloc(strlen(tmp) + 1);
		if (password != NULL) {
			memset(password, 0, strlen(tmp) + 1);
			password_alloc = 1;
			strncpy(password, tmp, strlen(tmp));
		} else {
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			res = -1;
			goto out;
		}
	}

	tmp = ast_variable_retrieve(cfg,"global","loguniqueid");
	if (tmp) {
		loguniqueid = ast_true(tmp);
		if (loguniqueid) {
			ast_log(LOG_DEBUG,"cdr_odbc: Logging uniqueid\n");
		} else {
			ast_log(LOG_DEBUG,"cdr_odbc: Not logging uniqueid\n");
		}
	} else {
		ast_log(LOG_DEBUG,"cdr_odbc: Not logging uniqueid\n");
		loguniqueid = 0;
	}

	tmp = ast_variable_retrieve(cfg,"global","usegmtime");
	if (tmp) {
		usegmtime = ast_true(tmp);
		if (usegmtime) {
			ast_log(LOG_DEBUG,"cdr_odbc: Logging in GMT\n");
		} else {
			ast_log(LOG_DEBUG,"cdr_odbc: Not logging in GMT\n");
		}
	} else {
		ast_log(LOG_DEBUG,"cdr_odbc: Not logging in GMT\n");
		usegmtime = 0;
	}

	tmp = ast_variable_retrieve(cfg,"global","table");
	if (tmp) {
		table = malloc(strlen(tmp) + 1);
		if (table != NULL) {
			memset(table, 0, strlen(tmp) + 1);
			table_alloc = 1;
			strncpy(table, tmp, strlen(tmp));
		} else {
			ast_log(LOG_ERROR,"cdr_odbc: Out of memory error.\n");
			res = -1;
			goto out;
		}
	} else {
		ast_log(LOG_WARNING,"cdr_odbc: table not specified.  Assuming cdr\n");
		table = "cdr";
	}

	ast_config_destroy(cfg);
	if (option_verbose > 2) {
		ast_verbose( VERBOSE_PREFIX_3 "cdr_odbc: dsn is %s\n",dsn);
		if (username)
		{
			ast_verbose( VERBOSE_PREFIX_3 "cdr_odbc: username is %s\n",username);
			ast_verbose( VERBOSE_PREFIX_3 "cdr_odbc: password is [secret]\n");
		}
		else
			ast_verbose( VERBOSE_PREFIX_3 "cdr_odbc: retreiving username and password from odbc config\n");
		ast_verbose( VERBOSE_PREFIX_3 "cdr_odbc: table is %s\n",table);
	}
	
	res = odbc_init();
	if (res < 0) {
		ast_log(LOG_ERROR, "cdr_odbc: Unable to connect to datasource: %s\n", dsn);
		if (option_verbose > 2) {
			ast_verbose( VERBOSE_PREFIX_3 "cdr_odbc: Unable to connect to datasource: %s\n", dsn);
		}
	}
	res = ast_cdr_register(name, desc, odbc_log);
	if (res) {
		ast_log(LOG_ERROR, "cdr_odbc: Unable to register ODBC CDR handling\n");
	}
out:
	ast_mutex_unlock(&odbc_lock);
	return res;
}

static int odbc_do_query(void)
{
	SQLINTEGER ODBC_err;
	int ODBC_res;
	short int ODBC_mlen;
	char ODBC_msg[200], ODBC_stat[10];
	
	ODBC_res = SQLExecute(ODBC_stmt);
	
	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error in Query %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_STMT, ODBC_stmt);
		connected = 0;
		return -1;
	} else {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Query Successful!\n");
		connected = 1;
	}
	return 0;
}

static int odbc_init(void)
{
	SQLINTEGER ODBC_err;
	short int ODBC_mlen;
	int ODBC_res;
	char ODBC_msg[200], ODBC_stat[10];

	if (ODBC_env == SQL_NULL_HANDLE || connected == 0) {
		ODBC_res = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &ODBC_env);
		if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 10)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error AllocHandle\n");
			connected = 0;
			return -1;
		}

		ODBC_res = SQLSetEnvAttr(ODBC_env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);

		if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 10)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error SetEnv\n");
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}

		ODBC_res = SQLAllocHandle(SQL_HANDLE_DBC, ODBC_env, &ODBC_con);

		if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 10)
				ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error AllocHDB %d\n", ODBC_res);
			SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
			connected = 0;
			return -1;
		}
		SQLSetConnectAttr(ODBC_con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)10, 0);	
	}

	ODBC_res = SQLConnect(ODBC_con, (SQLCHAR*)dsn, SQL_NTS, (SQLCHAR*)username, SQL_NTS, (SQLCHAR*)password, SQL_NTS);

	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Error SQLConnect %d\n", ODBC_res);
		SQLGetDiagRec(SQL_HANDLE_DBC, ODBC_con, 1, ODBC_stat, &ODBC_err, ODBC_msg, 100, &ODBC_mlen);
		SQLFreeHandle(SQL_HANDLE_ENV, ODBC_env);
		connected = 0;
		return -1;
	} else {
		if (option_verbose > 10)
			ast_verbose( VERBOSE_PREFIX_4 "cdr_odbc: Connected to %s\n", dsn);
		connected = 1;
	}
	return 0;
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
	/* Simplistic use count */
	if (ast_mutex_trylock(&odbc_lock)) {
		return 1;
	} else {
		ast_mutex_unlock(&odbc_lock);
		return 0;
	}
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
