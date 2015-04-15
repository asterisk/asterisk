/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2003-2005, Digium, Inc.
 *
 * Brian K. West <brian@bkw.org>
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

/*!
 * \file
 * \brief ODBC CDR Backend
 *
 * \author Brian K. West <brian@bkw.org>
 *
 * See also:
 * \arg http://www.unixodbc.org
 * \arg \ref Config_cdr
 * \ingroup cdr_drivers
 */

/*! \li \ref cdr_odbc.c uses the configuration file \ref cdr_odbc.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr_odbc.conf cdr_odbc.conf
 * \verbinclude cdr_odbc.conf.sample
 */

/*** MODULEINFO
	<depend>res_odbc</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/res_odbc.h"

#define DATE_FORMAT "%Y-%m-%d %T"

static const char name[] = "ODBC";
static const char config_file[] = "cdr_odbc.conf";
static char *dsn = NULL, *table = NULL;

enum {
	CONFIG_LOGUNIQUEID =       1 << 0,
	CONFIG_USEGMTIME =         1 << 1,
	CONFIG_DISPOSITIONSTRING = 1 << 2,
	CONFIG_HRTIME =            1 << 3,
	CONFIG_REGISTERED =        1 << 4,
	CONFIG_NEWCDRCOLUMNS =     1 << 5,
};

static struct ast_flags config = { 0 };

static SQLHSTMT execute_cb(struct odbc_obj *obj, void *data)
{
	struct ast_cdr *cdr = data;
	SQLRETURN ODBC_res;
	char sqlcmd[2048] = "", timestr[128], new_columns[120] = "", new_values[7] = "";
	struct ast_tm tm;
	SQLHSTMT stmt;
	int i = 0;

	ast_localtime(&cdr->start, &tm, ast_test_flag(&config, CONFIG_USEGMTIME) ? "GMT" : NULL);
	ast_strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

	if (ast_test_flag(&config, CONFIG_NEWCDRCOLUMNS)) {
		snprintf(new_columns, sizeof(new_columns), "%s" ,",peeraccount,linkedid,sequence");
		snprintf(new_values, sizeof(new_values), "%s" ,",?,?,?");
	}

	if (ast_test_flag(&config, CONFIG_LOGUNIQUEID)) {
		snprintf(sqlcmd,sizeof(sqlcmd),"INSERT INTO %s "
		"(calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,"
		"lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield%s) "
		"VALUES ({ts '%s'},?,?,?,?,?,?,?,?,?,?,?,?,?,?,? %s)",table, new_columns, timestr, new_values);
	} else {
		snprintf(sqlcmd,sizeof(sqlcmd),"INSERT INTO %s "
		"(calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,"
		"duration,billsec,disposition,amaflags,accountcode%s) "
		"VALUES ({ts '%s'},?,?,?,?,?,?,?,?,?,?,?,?,?%s)", table, new_columns, timestr, new_values);
	}

	ODBC_res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);

	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "cdr_odbc: Failure in AllocStatement %d\n", ODBC_res);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->clid), 0, cdr->clid, 0, NULL);
	SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->src), 0, cdr->src, 0, NULL);
	SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dst), 0, cdr->dst, 0, NULL);
	SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dcontext), 0, cdr->dcontext, 0, NULL);
	SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->channel), 0, cdr->channel, 0, NULL);
	SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->dstchannel), 0, cdr->dstchannel, 0, NULL);
	SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->lastapp), 0, cdr->lastapp, 0, NULL);
	SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->lastdata), 0, cdr->lastdata, 0, NULL);

	if (ast_test_flag(&config, CONFIG_HRTIME)) {
		double hrbillsec = 0.0;
		double hrduration;

		if (!ast_tvzero(cdr->answer)) {
			hrbillsec = (double) ast_tvdiff_us(cdr->end, cdr->answer) / 1000000.0;
		}
		hrduration = (double) ast_tvdiff_us(cdr->end, cdr->start) / 1000000.0;

		SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_FLOAT, 0, 0, &hrduration, 0, NULL);
		SQLBindParameter(stmt, 10, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_FLOAT, 0, 0, &hrbillsec, 0, NULL);
	} else {
		SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->duration, 0, NULL);
		SQLBindParameter(stmt, 10, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->billsec, 0, NULL);
	}

	if (ast_test_flag(&config, CONFIG_DISPOSITIONSTRING)) {
		char *disposition;
		disposition = ast_strdupa(ast_cdr_disp2str(cdr->disposition));
		SQLBindParameter(stmt, 11, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(disposition) + 1, 0, disposition, 0, NULL);
	} else {
		SQLBindParameter(stmt, 11, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->disposition, 0, NULL);
	}
	SQLBindParameter(stmt, 12, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->amaflags, 0, NULL);
	SQLBindParameter(stmt, 13, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->accountcode), 0, cdr->accountcode, 0, NULL);

	i = 14;
	if (ast_test_flag(&config, CONFIG_LOGUNIQUEID)) {
		SQLBindParameter(stmt, 14, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->uniqueid), 0, cdr->uniqueid, 0, NULL);
		SQLBindParameter(stmt, 15, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->userfield), 0, cdr->userfield, 0, NULL);
		i = 16;
	}

	if (ast_test_flag(&config, CONFIG_NEWCDRCOLUMNS)) {
		SQLBindParameter(stmt, i, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->peeraccount), 0, cdr->peeraccount, 0, NULL);
		SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, sizeof(cdr->linkedid), 0, cdr->linkedid, 0, NULL);
		SQLBindParameter(stmt, i + 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &cdr->sequence, 0, NULL);
	}

	ODBC_res = SQLExecDirect(stmt, (unsigned char *)sqlcmd, SQL_NTS);

	if ((ODBC_res != SQL_SUCCESS) && (ODBC_res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "cdr_odbc: Error in ExecDirect: %d, query is: %s\n", ODBC_res, sqlcmd);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	return stmt;
}


static int odbc_log(struct ast_cdr *cdr)
{
	struct odbc_obj *obj = ast_odbc_request_obj(dsn, 0);
	SQLHSTMT stmt;

	if (!obj) {
		ast_log(LOG_ERROR, "Unable to retrieve database handle.  CDR failed.\n");
		return -1;
	}

	stmt = ast_odbc_direct_execute(obj, execute_cb, cdr);
	if (stmt) {
		SQLLEN rows = 0;

		SQLRowCount(stmt, &rows);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);

		if (rows == 0)
			ast_log(LOG_WARNING, "CDR successfully ran, but inserted 0 rows?\n");
	} else
		ast_log(LOG_ERROR, "CDR direct execute failed\n");
	ast_odbc_release_obj(obj);
	return 0;
}

static int odbc_load_module(int reload)
{
	int res = 0;
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *tmp;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	do {
		cfg = ast_config_load(config_file, config_flags);
		if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_WARNING, "cdr_odbc: Unable to load config for ODBC CDR's: %s\n", config_file);
			res = AST_MODULE_LOAD_DECLINE;
			break;
		} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
			break;

		var = ast_variable_browse(cfg, "global");
		if (!var) {
			/* nothing configured */
			break;
		}

		if ((tmp = ast_variable_retrieve(cfg, "global", "dsn")) == NULL) {
			ast_log(LOG_WARNING, "cdr_odbc: dsn not specified.  Assuming asteriskdb\n");
			tmp = "asteriskdb";
		}
		if (dsn)
			ast_free(dsn);
		dsn = ast_strdup(tmp);
		if (dsn == NULL) {
			res = -1;
			break;
		}

		if (((tmp = ast_variable_retrieve(cfg, "global", "dispositionstring"))) && ast_true(tmp))
			ast_set_flag(&config, CONFIG_DISPOSITIONSTRING);
		else
			ast_clear_flag(&config, CONFIG_DISPOSITIONSTRING);

		if (((tmp = ast_variable_retrieve(cfg, "global", "loguniqueid"))) && ast_true(tmp)) {
			ast_set_flag(&config, CONFIG_LOGUNIQUEID);
			ast_debug(1, "cdr_odbc: Logging uniqueid\n");
		} else {
			ast_clear_flag(&config, CONFIG_LOGUNIQUEID);
			ast_debug(1, "cdr_odbc: Not logging uniqueid\n");
		}

		if (((tmp = ast_variable_retrieve(cfg, "global", "usegmtime"))) && ast_true(tmp)) {
			ast_set_flag(&config, CONFIG_USEGMTIME);
			ast_debug(1, "cdr_odbc: Logging in GMT\n");
		} else {
			ast_clear_flag(&config, CONFIG_USEGMTIME);
			ast_debug(1, "cdr_odbc: Logging in local time\n");
		}

		if (((tmp = ast_variable_retrieve(cfg, "global", "hrtime"))) && ast_true(tmp)) {
			ast_set_flag(&config, CONFIG_HRTIME);
			ast_debug(1, "cdr_odbc: Logging billsec and duration fields as floats\n");
		} else {
			ast_clear_flag(&config, CONFIG_HRTIME);
			ast_debug(1, "cdr_odbc: Logging billsec and duration fields as integers\n");
		}

		if ((tmp = ast_variable_retrieve(cfg, "global", "table")) == NULL) {
			ast_log(LOG_WARNING, "cdr_odbc: table not specified.  Assuming cdr\n");
			tmp = "cdr";
		}
		if (table)
			ast_free(table);
		table = ast_strdup(tmp);
		if (table == NULL) {
			res = -1;
			break;
		}

		if (!ast_test_flag(&config, CONFIG_REGISTERED)) {
			res = ast_cdr_register(name, ast_module_info->description, odbc_log);
			if (res) {
				ast_log(LOG_ERROR, "cdr_odbc: Unable to register ODBC CDR handling\n");
			} else {
				ast_set_flag(&config, CONFIG_REGISTERED);
			}
		}
		if (((tmp = ast_variable_retrieve(cfg, "global", "newcdrcolumns"))) && ast_true(tmp)) {
			ast_set_flag(&config, CONFIG_NEWCDRCOLUMNS);
			ast_debug(1, "cdr_odbc: Add new cdr fields\n");
		} else {
			ast_clear_flag(&config, CONFIG_NEWCDRCOLUMNS);
			ast_debug(1, "cdr_odbc: Not add new cdr fields\n");
		}
	} while (0);

	if (ast_test_flag(&config, CONFIG_REGISTERED) && (!cfg || dsn == NULL || table == NULL)) {
		ast_cdr_backend_suspend(name);
		ast_clear_flag(&config, CONFIG_REGISTERED);
	} else {
		ast_cdr_backend_unsuspend(name);
	}

	if (cfg && cfg != CONFIG_STATUS_FILEUNCHANGED && cfg != CONFIG_STATUS_FILEINVALID) {
		ast_config_destroy(cfg);
	}
	return res;
}

static int load_module(void)
{
	return odbc_load_module(0);
}

static int unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	if (dsn) {
		ast_free(dsn);
	}
	if (table) {
		ast_free(table);
	}

	return 0;
}

static int reload(void)
{
	return odbc_load_module(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "ODBC CDR Backend",
		.support_level = AST_MODULE_SUPPORT_EXTENDED,
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CDR_DRIVER,
	       );
