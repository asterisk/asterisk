/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * res_odbc.h <ODBC resource manager>
 * Copyright (C) 2004 Anthony Minessale II <anthmct@yahoo.com>
 */

#ifndef _RES_ODBC_H
#define _RES_ODBC_H

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>




typedef struct odbc_obj odbc_obj;

typedef enum { ODBC_SUCCESS=0,ODBC_FAIL=-1} odbc_status;

struct odbc_obj {
	char *name;
	char *dsn;
	char *username;
	char *password;
	SQLHENV  env;                   /* ODBC Environment */
	SQLHDBC  con;                   /* ODBC Connection Handle */
	SQLHSTMT stmt;                  /* ODBC Statement Handle */
	ast_mutex_t lock;
	int up;

};




/* functions */
odbc_obj *new_odbc_obj(char *name,char *dsn,char *username, char *password);
odbc_status odbc_obj_connect(odbc_obj *obj);
odbc_status odbc_obj_disconnect(odbc_obj *obj);
void destroy_obdc_obj(odbc_obj **obj);
int register_odbc_obj(char *name,odbc_obj *obj);
odbc_obj *fetch_odbc_obj(char *name);
int odbc_dump_fd(int fd,odbc_obj *obj);

#endif
