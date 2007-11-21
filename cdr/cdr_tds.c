/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2006, Digium, Inc.
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
 *
 * \brief FreeTDS CDR logger
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.freetds.org/
 * \ingroup cdr_drivers
 */

/*! \verbatim
 *
 * Table Structure for `cdr`
 *
 * Created on: 05/20/2004 16:16
 * Last changed on: 07/27/2004 20:01

CREATE TABLE [dbo].[cdr] (
	[accountcode] [varchar] (20) NULL ,
	[src] [varchar] (80) NULL ,
	[dst] [varchar] (80) NULL ,
	[dcontext] [varchar] (80) NULL ,
	[clid] [varchar] (80) NULL ,
	[channel] [varchar] (80) NULL ,
	[dstchannel] [varchar] (80) NULL ,
	[lastapp] [varchar] (80) NULL ,
	[lastdata] [varchar] (80) NULL ,
	[start] [datetime] NULL ,
	[answer] [datetime] NULL ,
	[end] [datetime] NULL ,
	[duration] [int] NULL ,
	[billsec] [int] NULL ,
	[disposition] [varchar] (20) NULL ,
	[amaflags] [varchar] (16) NULL ,
	[uniqueid] [varchar] (32) NULL
) ON [PRIMARY]

\endverbatim

*/

/*** MODULEINFO
	<depend>freetds</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>
#include <math.h>

#include <tds.h>
#include <tdsconvert.h>
#include <ctype.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"

#ifdef FREETDS_PRE_0_62
#warning "You have older TDS, you should upgrade!"
#endif

#define DATE_FORMAT "%Y/%m/%d %T"

static char *name = "mssql";
static char *config = "cdr_tds.conf";

static char *hostname = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL, *charset = NULL, *language = NULL;
static char *table = NULL;

static int connected = 0;

AST_MUTEX_DEFINE_STATIC(tds_lock);

static TDSSOCKET *tds;
static TDSLOGIN *login;
static TDSCONTEXT *context;

static char *anti_injection(const char *, int);
static void get_date(char *, struct timeval);

static int mssql_connect(void);
static int mssql_disconnect(void);

static int tds_log(struct ast_cdr *cdr)
{
	char sqlcmd[2048], start[80], answer[80], end[80];
	char *accountcode, *src, *dst, *dcontext, *clid, *channel, *dstchannel, *lastapp, *lastdata, *uniqueid;
	int res = 0;
	int retried = 0;
#ifdef FREETDS_PRE_0_62
	TDS_INT result_type;
#endif

	ast_mutex_lock(&tds_lock);

	memset(sqlcmd, 0, 2048);

	accountcode = anti_injection(cdr->accountcode, 20);
	src = anti_injection(cdr->src, 80);
	dst = anti_injection(cdr->dst, 80);
	dcontext = anti_injection(cdr->dcontext, 80);
	clid = anti_injection(cdr->clid, 80);
	channel = anti_injection(cdr->channel, 80);
	dstchannel = anti_injection(cdr->dstchannel, 80);
	lastapp = anti_injection(cdr->lastapp, 80);
	lastdata = anti_injection(cdr->lastdata, 80);
	uniqueid = anti_injection(cdr->uniqueid, 32);

	get_date(start, cdr->start);
	get_date(answer, cdr->answer);
	get_date(end, cdr->end);

	sprintf(
		sqlcmd,
		"INSERT INTO %s "
		"("
			"accountcode, "
			"src, "
			"dst, "
			"dcontext, "
			"clid, "
			"channel, "
			"dstchannel, "
			"lastapp, "
			"lastdata, "
			"start, "
			"answer, "
			"[end], "
			"duration, "
			"billsec, "
			"disposition, "
			"amaflags, "
			"uniqueid"
		") "
		"VALUES "
		"("
			"'%s', "	/* accountcode */
			"'%s', "	/* src */
			"'%s', "	/* dst */
			"'%s', "	/* dcontext */
			"'%s', "	/* clid */
			"'%s', "	/* channel */
			"'%s', "	/* dstchannel */
			"'%s', "	/* lastapp */
			"'%s', "	/* lastdata */
			"%s, "		/* start */
			"%s, "		/* answer */
			"%s, "		/* end */
			"%ld, "		/* duration */
			"%ld, "		/* billsec */
			"'%s', "	/* disposition */
			"'%s', "	/* amaflags */
			"'%s'"		/* uniqueid */
		")",
		table,
		accountcode,
		src,
		dst,
		dcontext,
		clid,
		channel,
		dstchannel,
		lastapp,
		lastdata,
		start,
		answer,
		end,
		cdr->duration,
		cdr->billsec,
		ast_cdr_disp2str(cdr->disposition),
		ast_cdr_flags2str(cdr->amaflags),
		uniqueid
	);

	do {
		if (!connected) {
			if (mssql_connect())
				ast_log(LOG_ERROR, "Failed to reconnect to SQL database.\n");
			else
				ast_log(LOG_WARNING, "Reconnected to SQL database.\n");

			retried = 1;	/* note that we have now tried */
		}

#ifdef FREETDS_PRE_0_62
		if (!connected || (tds_submit_query(tds, sqlcmd) != TDS_SUCCEED) || (tds_process_simple_query(tds, &result_type) != TDS_SUCCEED || result_type != TDS_CMD_SUCCEED))
#else
		if (!connected || (tds_submit_query(tds, sqlcmd) != TDS_SUCCEED) || (tds_process_simple_query(tds) != TDS_SUCCEED))
#endif
		{
			ast_log(LOG_ERROR, "Failed to insert Call Data Record into SQL database.\n");

			mssql_disconnect();	/* this is ok even if we are already disconnected */
		}
	} while (!connected && !retried);

	ast_free(accountcode);
	ast_free(src);
	ast_free(dst);
	ast_free(dcontext);
	ast_free(clid);
	ast_free(channel);
	ast_free(dstchannel);
	ast_free(lastapp);
	ast_free(lastdata);
	ast_free(uniqueid);

	ast_mutex_unlock(&tds_lock);

	return res;
}

static char *anti_injection(const char *str, int len)
{
	/* Reference to http://www.nextgenss.com/papers/advanced_sql_injection.pdf */

	char *buf;
	char *buf_ptr, *srh_ptr;
	char *known_bad[] = {"select", "insert", "update", "delete", "drop", ";", "--", "\0"};
	int idx;

	if ((buf = ast_malloc(len + 1)) == NULL)
	{
		ast_log(LOG_ERROR, "cdr_tds:  Out of memory error\n");
		return NULL;
	}
	memset(buf, 0, len);

	buf_ptr = buf;

	/* Escape single quotes */
	for (; *str && strlen(buf) < len; str++)
	{
		if (*str == '\'')
			*buf_ptr++ = '\'';
		*buf_ptr++ = *str;
	}
	*buf_ptr = '\0';

	/* Erase known bad input */
	for (idx=0; *known_bad[idx]; idx++)
	{
		while((srh_ptr = strcasestr(buf, known_bad[idx])))
		{
			memmove(srh_ptr, srh_ptr+strlen(known_bad[idx]), strlen(srh_ptr+strlen(known_bad[idx]))+1);
		}
	}

	return buf;
}

static void get_date(char *dateField, struct timeval tv)
{
	struct ast_tm tm;
	char buf[80];

	/* To make sure we have date variable if not insert null to SQL */
	if (!ast_tvzero(tv))
	{
		ast_localtime(&tv, &tm, NULL);
		ast_strftime(buf, 80, DATE_FORMAT, &tm);
		sprintf(dateField, "'%s'", buf);
	}
	else
	{
		strcpy(dateField, "null");
	}
}

static int mssql_disconnect(void)
{
	if (tds) {
		tds_free_socket(tds);
		tds = NULL;
	}

	if (context) {
		tds_free_context(context);
		context = NULL;
	}

	if (login) {
		tds_free_login(login);
		login = NULL;
	}

	connected = 0;

	return 0;
}

static int mssql_connect(void)
{
#if (defined(FREETDS_0_63) || defined(FREETDS_0_64))
	TDSCONNECTION *connection = NULL;
#else
	TDSCONNECTINFO *connection = NULL;
#endif
	char query[128];

	/* Connect to M$SQL Server */
	if (!(login = tds_alloc_login()))
	{
		ast_log(LOG_ERROR, "tds_alloc_login() failed.\n");
		return -1;
	}
	
	tds_set_server(login, hostname);
	tds_set_user(login, dbuser);
	tds_set_passwd(login, password);
	tds_set_app(login, "TSQL");
	tds_set_library(login, "TDS-Library");
#ifndef FREETDS_PRE_0_62
	tds_set_client_charset(login, charset);
#endif
	tds_set_language(login, language);
	tds_set_packet(login, 512);
	tds_set_version(login, 7, 0);

#ifdef FREETDS_0_64
	if (!(context = tds_alloc_context(NULL)))
#else
	if (!(context = tds_alloc_context()))
#endif
	{
		ast_log(LOG_ERROR, "tds_alloc_context() failed.\n");
		goto connect_fail;
	}

	if (!(tds = tds_alloc_socket(context, 512))) {
		ast_log(LOG_ERROR, "tds_alloc_socket() failed.\n");
		goto connect_fail;
	}

	tds_set_parent(tds, NULL);
	connection = tds_read_config_info(tds, login, context->locale);
	if (!connection)
	{
		ast_log(LOG_ERROR, "tds_read_config() failed.\n");
		goto connect_fail;
	}

	if (tds_connect(tds, connection) == TDS_FAIL)
	{
		ast_log(LOG_ERROR, "Failed to connect to MSSQL server.\n");
		tds = NULL;	/* freed by tds_connect() on error */
#if (defined(FREETDS_0_63) || defined(FREETDS_0_64))
		tds_free_connection(connection);
#else
		tds_free_connect(connection);
#endif
		connection = NULL;
		goto connect_fail;
	}
#if (defined(FREETDS_0_63) || defined(FREETDS_0_64))
	tds_free_connection(connection);
#else
	tds_free_connect(connection);
#endif
	connection = NULL;

	sprintf(query, "USE %s", dbname);
#ifdef FREETDS_PRE_0_62
	if ((tds_submit_query(tds, query) != TDS_SUCCEED) || (tds_process_simple_query(tds, &result_type) != TDS_SUCCEED || result_type != TDS_CMD_SUCCEED))
#else
	if ((tds_submit_query(tds, query) != TDS_SUCCEED) || (tds_process_simple_query(tds) != TDS_SUCCEED))
#endif
	{
		ast_log(LOG_ERROR, "Could not change database (%s)\n", dbname);
		goto connect_fail;
	}

	connected = 1;
	return 0;

connect_fail:
	mssql_disconnect();
	return -1;
}

static int tds_unload_module(void)
{
	mssql_disconnect();

	ast_cdr_unregister(name);

	if (hostname) ast_free(hostname);
	if (dbname) ast_free(dbname);
	if (dbuser) ast_free(dbuser);
	if (password) ast_free(password);
	if (charset) ast_free(charset);
	if (language) ast_free(language);
	if (table) ast_free(table);

	return 0;
}

static int tds_load_module(int reload)
{
	int res = 0;
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *ptr = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
#ifdef FREETDS_PRE_0_62
	TDS_INT result_type;
#endif

	cfg = ast_config_load(config, config_flags);
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config for MSSQL CDR's: %s\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	var = ast_variable_browse(cfg, "global");
	if (!var) /* nothing configured */ {
		ast_config_destroy(cfg);
		return 0;
	}
	
	ptr = ast_variable_retrieve(cfg, "global", "hostname");
	if (ptr) {
		if (hostname)
			ast_free(hostname);
		hostname = ast_strdup(ptr);
	} else
		ast_log(LOG_ERROR, "Database server hostname not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "dbname");
	if (ptr) {
		if (dbname)
			ast_free(dbname);
		dbname = ast_strdup(ptr);
	} else
		ast_log(LOG_ERROR, "Database dbname not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "user");
	if (ptr) {
		if (dbuser)
			ast_free(dbuser);
		dbuser = ast_strdup(ptr);
	} else
		ast_log(LOG_ERROR, "Database dbuser not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "password");
	if (ptr) {
		if (password)
			ast_free(password);
		password = ast_strdup(ptr);
	} else
		ast_log(LOG_ERROR,"Database password not specified.\n");

	ptr = ast_variable_retrieve(cfg, "global", "charset");
	if (charset)
		ast_free(charset);
	if (ptr)
		charset = ast_strdup(ptr);
	else
		charset = ast_strdup("iso_1");

	if (language)
		ast_free(language);
	ptr = ast_variable_retrieve(cfg, "global", "language");
	if (ptr)
		language = ast_strdup(ptr);
	else
		language = ast_strdup("us_english");

	ptr = ast_variable_retrieve(cfg, "global", "table");
	if (ptr == NULL) {
		ast_debug(1, "cdr_tds: table not specified.  Assuming cdr\n");
		ptr = "cdr";
	}
	if (table)
		ast_free(table);
	table = ast_strdup(ptr);

	ast_config_destroy(cfg);

	ast_mutex_lock(&tds_lock);
	mssql_disconnect();
	mssql_connect();
	ast_mutex_unlock(&tds_lock);

	return res;
}

static int reload(void)
{
	return tds_load_module(1);
}

static int load_module(void)
{
	if (!tds_load_module(0))
		return AST_MODULE_LOAD_DECLINE;
	ast_cdr_register(name, ast_module_info->description, tds_log);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return tds_unload_module();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MSSQL CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
