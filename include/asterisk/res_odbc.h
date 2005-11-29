/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2004 - 2005, Anthony Minessale II
 *
 * Mark Spencer <markster@digium.com>
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief ODBC resource manager
 */

#ifndef _ASTERISK_RES_ODBC_H
#define _ASTERISK_RES_ODBC_H

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
void destroy_odbc_obj(odbc_obj **obj);
int register_odbc_obj(char *name,odbc_obj *obj);
odbc_obj *fetch_odbc_obj(const char *name, int check);
int odbc_dump_fd(int fd,odbc_obj *obj);
int odbc_sanity_check(odbc_obj *obj);
int odbc_smart_execute(odbc_obj *obj, SQLHSTMT stmt);
int odbc_smart_direct_execute(odbc_obj *obj, SQLHSTMT stmt, char *sql);

#endif /* _ASTERISK_RES_ODBC_H */
