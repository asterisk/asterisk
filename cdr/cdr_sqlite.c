/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Store CDR records in a SQLite database.
 * 
 * Copyright (C) 2004, Holger Schurig
 *
 * Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Ideas taken from other cdr_*.c files
 */

#include <sys/types.h>
#include <asterisk/cdr.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>
#include "../asterisk.h"
#include "../astconf.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite.h>


#define LOG_UNIQUEID	0
#define LOG_USERFIELD	0

/* When you change the DATE_FORMAT, be sure to change the CHAR(19) below to something else */
#define DATE_FORMAT "%Y-%m-%d %T"

static char *desc = "SQLite CDR Backend";
static char *name = "sqlite";
static sqlite* db = NULL;

AST_MUTEX_DEFINE_STATIC(sqlite_lock);

static char sql_create_table[] = "CREATE TABLE cdr ("
"	AcctId		INTEGER PRIMARY KEY,"
"	clid		VARCHAR(80),"
"	src		VARCHAR(80),"
"	dst		VARCHAR(80),"
"	dcontext	VARCHAR(80),"
"	channel		VARCHAR(80),"
"	dstchannel	VARCHAR(80),"
"	lastapp		VARCHAR(80),"
"	lastdata	VARCHAR(80),"
"	start		CHAR(19),"
"	answer		CHAR(19),"
"	end		CHAR(19),"
"	duration	INTEGER,"
"	billsec		INTEGER,"
"	disposition	INTEGER,"
"	amaflags	INTEGER,"
"	accountcode	VARCHAR(20)"
#if LOG_UNIQUEID
"	,uniqueid	VARCHAR(32),"
#endif
#if LOG_USERFIELD
"	,userfield	VARCHAR(255)"
#endif
");";

static int sqlite_log(struct ast_cdr *cdr)
{
	int res = 0;
	char *zErr = 0;
	struct tm tm;
	time_t t;
	char startstr[80], answerstr[80], endstr[80];
	int count;

	ast_mutex_lock(&sqlite_lock);

	t = cdr->start.tv_sec;
	localtime_r(&t, &tm);
	strftime(startstr, sizeof(startstr), DATE_FORMAT, &tm);

	t = cdr->answer.tv_sec;
	localtime_r(&t, &tm);
	strftime(answerstr, sizeof(answerstr), DATE_FORMAT, &tm);

	t = cdr->end.tv_sec;
	localtime_r(&t, &tm);
	strftime(endstr, sizeof(endstr), DATE_FORMAT, &tm);

	for(count=0; count<5; count++) {
		res = sqlite_exec_printf(db,
			"INSERT INTO cdr ("
				"clid,src,dst,dcontext,"
				"channel,dstchannel,lastapp,lastdata, "
				"start,answer,end,"
				"duration,billsec,disposition,amaflags, "
				"accountcode"
#				if LOG_UNIQUEID
				",uniqueid"
#				endif
#				if LOG_USERFIELD
				",userfield"
#				endif
			") VALUES ("
				"'%q', '%q', '%q', '%q', "
				"'%q', '%q', '%q', '%q', "
				"'%q', '%q', '%q', "
				"%d, %d, %d, %d, "
				"'%q'"
#				if LOG_UNIQUEID
				",'%q'"
#				endif
#				if LOG_USERFIELD
				",'%q'"
#				endif
			")", NULL, NULL, &zErr,
				cdr->clid, cdr->src, cdr->dst, cdr->dcontext,
				cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata,
				startstr, answerstr, endstr,
				cdr->duration, cdr->billsec, cdr->disposition, cdr->amaflags,
				cdr->accountcode
#				if LOG_UNIQUEID
				,cdr->uniqueid
#				endif
#				if LOG_USERFIELD
				,cdr->userfield
#				endif
			);
		if (res != SQLITE_BUSY && res != SQLITE_LOCKED)
			break;
		usleep(200);
	}
	
	if (zErr) {
		ast_log(LOG_ERROR, "cdr_sqlite: %s\n", zErr);
		free(zErr);
	}

	ast_mutex_unlock(&sqlite_lock);
	return res;
}


char *description(void)
{
	return desc;
}

int unload_module(void)
{
	if (db)
		sqlite_close(db);
	ast_cdr_unregister(name);
	return 0;
}

int load_module(void)
{
	char *zErr;
	char fn[PATH_MAX];
	int res;

	/* is the database there? */
	snprintf(fn, sizeof(fn), "%s/cdr.db", ast_config_AST_LOG_DIR);
	db = sqlite_open(fn, 0660, &zErr);
	if (!db) {
		ast_log(LOG_ERROR, "cdr_sqlite: %s\n", zErr);
		free(zErr);
		return -1;
	}

	/* is the table there? */
	res = sqlite_exec(db, "SELECT COUNT(AcctId) FROM cdr;", NULL, NULL, NULL);
	if (res) {
		res = sqlite_exec(db, sql_create_table, NULL, NULL, &zErr);
		if (res) {
			ast_log(LOG_ERROR, "cdr_sqlite: Unable to create table 'cdr': %s\n", zErr);
			free(zErr);
			goto err;
		}

		/* TODO: here we should probably create an index */
	}
	
	res = ast_cdr_register(name, desc, sqlite_log);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register SQLite CDR handling\n");
		return -1;
	}
	return 0;

err:
	if (db)
		sqlite_close(db);
	return -1;
}

int reload(void)
{
	return 0;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
