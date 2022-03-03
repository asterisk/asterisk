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

/*!
 * \file
 * \brief FreeTDS CDR logger
 * http://www.freetds.org/
 * \ingroup cdr_drivers
 */

/*!
 * \verbatim
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
	[uniqueid] [varchar] (32) NULL ,
	[userfield] [varchar] (256) NULL
) ON [PRIMARY]

\endverbatim

*/

/*** MODULEINFO
	<depend>freetds</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"

#include <sqlfront.h>
#include <sybdb.h>

#define DATE_FORMAT "%Y/%m/%d %T"

static const char name[] = "FreeTDS (MSSQL)";
static const char config[] = "cdr_tds.conf";

struct cdr_tds_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(hostname);
		AST_STRING_FIELD(database);
		AST_STRING_FIELD(username);
		AST_STRING_FIELD(password);
		AST_STRING_FIELD(table);
		AST_STRING_FIELD(charset);
		AST_STRING_FIELD(language);
		AST_STRING_FIELD(hrtime);
	);
	DBPROCESS *dbproc;
	unsigned int connected:1;
	unsigned int has_userfield:1;
};

AST_MUTEX_DEFINE_STATIC(tds_lock);

static struct cdr_tds_config *settings;

static char *anti_injection(const char *, int);
static void get_date(char *, size_t len, struct timeval);

static int execute_and_consume(DBPROCESS *dbproc, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static int mssql_connect(void);
static int mssql_disconnect(void);

static int tds_log(struct ast_cdr *cdr)
{
	char start[80], answer[80], end[80];
	char *accountcode, *src, *dst, *dcontext, *clid, *channel, *dstchannel, *lastapp, *lastdata, *uniqueid, *userfield = NULL;
	RETCODE erc;
	int res = -1;
	int attempt = 1;

	accountcode = anti_injection(cdr->accountcode, 20);
	src         = anti_injection(cdr->src, 80);
	dst         = anti_injection(cdr->dst, 80);
	dcontext    = anti_injection(cdr->dcontext, 80);
	clid        = anti_injection(cdr->clid, 80);
	channel     = anti_injection(cdr->channel, 80);
	dstchannel  = anti_injection(cdr->dstchannel, 80);
	lastapp     = anti_injection(cdr->lastapp, 80);
	lastdata    = anti_injection(cdr->lastdata, 80);
	uniqueid    = anti_injection(cdr->uniqueid, 32);

	get_date(start, sizeof(start), cdr->start);
	get_date(answer, sizeof(answer), cdr->answer);
	get_date(end, sizeof(end), cdr->end);

	ast_mutex_lock(&tds_lock);

	if (settings->has_userfield) {
		userfield = anti_injection(cdr->userfield, AST_MAX_USER_FIELD);
	}

retry:
	/* Ensure that we are connected */
	if (!settings->connected) {
		ast_log(LOG_NOTICE, "Attempting to reconnect to %s (Attempt %d)\n", settings->hostname, attempt);
		if (mssql_connect()) {
			/* Connect failed */
			if (attempt++ < 3) {
				goto retry;
			}
			goto done;
		}
	}

	if (settings->has_userfield) {
		if (settings->hrtime) {
			double hrbillsec = 0.0;
			double hrduration;

			if (!ast_tvzero(cdr->answer)) {
				hrbillsec = (double)(ast_tvdiff_us(cdr->end, cdr->answer) / 1000000.0);
			}
			hrduration = (double)(ast_tvdiff_us(cdr->end, cdr->start) / 1000000.0);

			erc = dbfcmd(settings->dbproc,
					 "INSERT INTO %s "
					 "("
					 "accountcode, src, dst, dcontext, clid, channel, "
					 "dstchannel, lastapp, lastdata, start, answer, [end], duration, "
					 "billsec, disposition, amaflags, uniqueid, userfield"
					 ") "
					 "VALUES "
					 "("
					 "'%s', '%s', '%s', '%s', '%s', '%s', "
					 "'%s', '%s', '%s', %s, %s, %s, %lf, "
					 "%lf, '%s', '%s', '%s', '%s'"
					 ")",
					 settings->table,
					 accountcode, src, dst, dcontext, clid, channel,
					 dstchannel, lastapp, lastdata, start, answer, end, hrduration,
					 hrbillsec, ast_cdr_disp2str(cdr->disposition), ast_channel_amaflags2string(cdr->amaflags), uniqueid,
					 userfield
			);
		} else {
			erc = dbfcmd(settings->dbproc,
					 "INSERT INTO %s "
					 "("
					 "accountcode, src, dst, dcontext, clid, channel, "
					 "dstchannel, lastapp, lastdata, start, answer, [end], duration, "
					 "billsec, disposition, amaflags, uniqueid, userfield"
					 ") "
					 "VALUES "
					 "("
					 "'%s', '%s', '%s', '%s', '%s', '%s', "
					 "'%s', '%s', '%s', %s, %s, %s, %ld, "
					 "%ld, '%s', '%s', '%s', '%s'"
					 ")",
					 settings->table,
					 accountcode, src, dst, dcontext, clid, channel,
					 dstchannel, lastapp, lastdata, start, answer, end, cdr->duration,
					 cdr->billsec, ast_cdr_disp2str(cdr->disposition), ast_channel_amaflags2string(cdr->amaflags), uniqueid,
					 userfield
			);
		}
	} else {
		if (settings->hrtime) {
			double hrbillsec = 0.0;
			double hrduration;

			if (!ast_tvzero(cdr->answer)) {
				hrbillsec = (double)(ast_tvdiff_us(cdr->end, cdr->answer) / 1000000.0);
			}
			hrduration = (double)(ast_tvdiff_us(cdr->end, cdr->start) / 1000000.0);

			erc = dbfcmd(settings->dbproc,
					 "INSERT INTO %s "
					 "("
					 "accountcode, src, dst, dcontext, clid, channel, "
					 "dstchannel, lastapp, lastdata, start, answer, [end], duration, "
					 "billsec, disposition, amaflags, uniqueid"
					 ") "
					 "VALUES "
					 "("
					 "'%s', '%s', '%s', '%s', '%s', '%s', "
					 "'%s', '%s', '%s', %s, %s, %s, %lf, "
					 "%lf, '%s', '%s', '%s'"
					 ")",
					 settings->table,
					 accountcode, src, dst, dcontext, clid, channel,
					 dstchannel, lastapp, lastdata, start, answer, end, hrduration,
					 hrbillsec, ast_cdr_disp2str(cdr->disposition), ast_channel_amaflags2string(cdr->amaflags), uniqueid
			);
		} else {
			erc = dbfcmd(settings->dbproc,
					 "INSERT INTO %s "
					 "("
					 "accountcode, src, dst, dcontext, clid, channel, "
					 "dstchannel, lastapp, lastdata, start, answer, [end], duration, "
					 "billsec, disposition, amaflags, uniqueid"
					 ") "
					 "VALUES "
					 "("
					 "'%s', '%s', '%s', '%s', '%s', '%s', "
					 "'%s', '%s', '%s', %s, %s, %s, %ld, "
					 "%ld, '%s', '%s', '%s'"
					 ")",
					 settings->table,
					 accountcode, src, dst, dcontext, clid, channel,
					 dstchannel, lastapp, lastdata, start, answer, end, cdr->duration,
					 cdr->billsec, ast_cdr_disp2str(cdr->disposition), ast_channel_amaflags2string(cdr->amaflags), uniqueid
			);
		}
	}

	if (erc == FAIL) {
		if (attempt++ < 3) {
			ast_log(LOG_NOTICE, "Failed to build INSERT statement, retrying...\n");
			mssql_disconnect();
			goto retry;
		} else {
			ast_log(LOG_ERROR, "Failed to build INSERT statement, no CDR was logged.\n");
			goto done;
		}
	}

	if (dbsqlexec(settings->dbproc) == FAIL) {
		if (attempt++ < 3) {
			ast_log(LOG_NOTICE, "Failed to execute INSERT statement, retrying...\n");
			mssql_disconnect();
			goto retry;
		} else {
			ast_log(LOG_ERROR, "Failed to execute INSERT statement, no CDR was logged.\n");
			goto done;
		}
	}

	/* Consume any results we might get back (this is more of a sanity check than
	 * anything else, since an INSERT shouldn't return results). */
	while (dbresults(settings->dbproc) != NO_MORE_RESULTS) {
		while (dbnextrow(settings->dbproc) != NO_MORE_ROWS);
	}

	res = 0;

done:
	ast_mutex_unlock(&tds_lock);

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

	if (userfield) {
		ast_free(userfield);
	}

	return res;
}

static char *anti_injection(const char *str, int len)
{
	/* Reference to http://www.nextgenss.com/papers/advanced_sql_injection.pdf */
	char *buf;
	char *buf_ptr, *srh_ptr;
	char *known_bad[] = {"select", "insert", "update", "delete", "drop", ";", "--", "\0"};
	int idx;

	if (!(buf = ast_calloc(1, len + 1))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return NULL;
	}

	buf_ptr = buf;

	/* Escape single quotes */
	for (; *str && strlen(buf) < len; str++) {
		if (*str == '\'') {
			*buf_ptr++ = '\'';
		}
		*buf_ptr++ = *str;
	}
	*buf_ptr = '\0';

	/* Erase known bad input */
	for (idx = 0; *known_bad[idx]; idx++) {
		while ((srh_ptr = strcasestr(buf, known_bad[idx]))) {
			memmove(srh_ptr, srh_ptr + strlen(known_bad[idx]), strlen(srh_ptr + strlen(known_bad[idx])) + 1);
		}
	}

	return buf;
}

static void get_date(char *dateField, size_t len, struct timeval when)
{
	/* To make sure we have date variable if not insert null to SQL */
	if (!ast_tvzero(when)) {
		struct ast_tm tm;
		ast_localtime(&when, &tm, NULL);
		ast_strftime(dateField, len, "'" DATE_FORMAT "'", &tm);
	} else {
		ast_copy_string(dateField, "null", len);
	}
}

static int execute_and_consume(DBPROCESS *dbproc, const char *fmt, ...)
{
	va_list ap;
	char *buffer;

	va_start(ap, fmt);
	if (ast_vasprintf(&buffer, fmt, ap) < 0) {
		va_end(ap);
		return 1;
	}
	va_end(ap);

	if (dbfcmd(dbproc, buffer) == FAIL) {
		ast_free(buffer);
		return 1;
	}

	ast_free(buffer);

	if (dbsqlexec(dbproc) == FAIL) {
		return 1;
	}

	/* Consume the result set (we don't really care about the result, though) */
	while (dbresults(dbproc) != NO_MORE_RESULTS) {
		while (dbnextrow(dbproc) != NO_MORE_ROWS);
	}

	return 0;
}

static int mssql_disconnect(void)
{
	if (settings->dbproc) {
		dbclose(settings->dbproc);
		settings->dbproc = NULL;
	}

	settings->connected = 0;

	return 0;
}

static int mssql_connect(void)
{
	LOGINREC *login;

	if ((login = dblogin()) == NULL) {
		ast_log(LOG_ERROR, "Unable to allocate login structure for db-lib\n");
		return -1;
	}

	DBSETLAPP(login,     "TSQL");
	DBSETLUSER(login,    (char *) settings->username);
	DBSETLPWD(login,     (char *) settings->password);
	DBSETLCHARSET(login, (char *) settings->charset);
	DBSETLNATLANG(login, (char *) settings->language);

	if ((settings->dbproc = dbopen(login, (char *) settings->hostname)) == NULL) {
		ast_log(LOG_ERROR, "Unable to connect to %s\n", settings->hostname);
		dbloginfree(login);
		return -1;
	}

	dbloginfree(login);

	if (dbuse(settings->dbproc, (char *) settings->database) == FAIL) {
		ast_log(LOG_ERROR, "Unable to select database %s\n", settings->database);
		goto failed;
	}

	if (execute_and_consume(settings->dbproc, "SELECT 1 FROM [%s] WHERE 1 = 0", settings->table)) {
		ast_log(LOG_ERROR, "Unable to find table '%s'\n", settings->table);
		goto failed;
	}

	/* Check to see if we have a userfield column in the table */
	if (execute_and_consume(settings->dbproc, "SELECT userfield FROM [%s] WHERE 1 = 0", settings->table)) {
		ast_log(LOG_NOTICE, "Unable to find 'userfield' column in table '%s'\n", settings->table);
		settings->has_userfield = 0;
	} else {
		settings->has_userfield = 1;
	}

	settings->connected = 1;

	return 0;

failed:
	dbclose(settings->dbproc);
	settings->dbproc = NULL;
	return -1;
}

static int tds_unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	if (settings) {
		ast_mutex_lock(&tds_lock);
		mssql_disconnect();
		ast_mutex_unlock(&tds_lock);

		ast_string_field_free_memory(settings);
		ast_free(settings);
	}

	dbexit();

	return 0;
}

static int tds_error_handler(DBPROCESS *dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr)
{
	ast_log(LOG_ERROR, "%s (%d)\n", dberrstr, dberr);

	if (oserr != DBNOERR) {
		ast_log(LOG_ERROR, "%s (%d)\n", oserrstr, oserr);
	}

	return INT_CANCEL;
}

static int tds_message_handler(DBPROCESS *dbproc, DBINT msgno, int msgstate, int severity, char *msgtext, char *srvname, char *procname, int line)
{
	ast_debug(1, "Msg %d, Level %d, State %d, Line %d\n", msgno, severity, msgstate, line);
	ast_log(LOG_NOTICE, "%s\n", msgtext);

	return 0;
}

static int tds_load_module(int reload)
{
	struct ast_config *cfg;
	const char *ptr = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	cfg = ast_config_load(config, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_NOTICE, "Unable to load TDS config for CDRs: %s\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	if (!ast_variable_browse(cfg, "global")) {
		/* nothing configured */
		ast_config_destroy(cfg);
		return 0;
	}

	ast_mutex_lock(&tds_lock);

	/* Clear out any existing settings */
	ast_string_field_init(settings, 0);

	/* 'connection' is the new preferred configuration option */
	ptr = ast_variable_retrieve(cfg, "global", "connection");
	if (ptr) {
		ast_string_field_set(settings, hostname, ptr);
	} else {
		/* But we keep 'hostname' for backwards compatibility */
		ptr = ast_variable_retrieve(cfg, "global", "hostname");
		if (ptr) {
			ast_string_field_set(settings, hostname, ptr);
		} else {
			ast_log(LOG_ERROR, "Failed to connect: Database server connection not specified.\n");
			goto failed;
		}
	}

	ptr = ast_variable_retrieve(cfg, "global", "dbname");
	if (ptr) {
		ast_string_field_set(settings, database, ptr);
	} else {
		ast_log(LOG_ERROR, "Failed to connect: Database dbname not specified.\n");
		goto failed;
	}

	ptr = ast_variable_retrieve(cfg, "global", "user");
	if (ptr) {
		ast_string_field_set(settings, username, ptr);
	} else {
		ast_log(LOG_ERROR, "Failed to connect: Database dbuser not specified.\n");
		goto failed;
	}

	ptr = ast_variable_retrieve(cfg, "global", "password");
	if (ptr) {
		ast_string_field_set(settings, password, ptr);
	} else {
		ast_log(LOG_ERROR, "Failed to connect: Database password not specified.\n");
		goto failed;
	}

	ptr = ast_variable_retrieve(cfg, "global", "charset");
	if (ptr) {
		ast_string_field_set(settings, charset, ptr);
	} else {
		ast_string_field_set(settings, charset, "iso_1");
	}

	ptr = ast_variable_retrieve(cfg, "global", "language");
	if (ptr) {
		ast_string_field_set(settings, language, ptr);
	} else {
		ast_string_field_set(settings, language, "us_english");
	}

	ptr = ast_variable_retrieve(cfg, "global", "table");
	if (ptr) {
		ast_string_field_set(settings, table, ptr);
	} else {
		ast_log(LOG_NOTICE, "Table name not specified, using 'cdr' by default.\n");
		ast_string_field_set(settings, table, "cdr");
	}

	ptr = ast_variable_retrieve(cfg, "global", "hrtime");
	if (ptr && ast_true(ptr)) {
		ast_string_field_set(settings, hrtime, ptr);
	} else {
		ast_log(LOG_NOTICE, "High Resolution Time not found, using integers for billsec and duration fields by default.\n");
	}

	mssql_disconnect();

	if (mssql_connect()) {
		/* We failed to connect (mssql_connect takes care of logging it) */
		goto failed;
	}

	ast_mutex_unlock(&tds_lock);
	ast_config_destroy(cfg);

	return 1;

failed:
	ast_mutex_unlock(&tds_lock);
	ast_config_destroy(cfg);

	return 0;
}

static int reload(void)
{
	return tds_load_module(1);
}

static int load_module(void)
{
	if (dbinit() == FAIL) {
		ast_log(LOG_ERROR, "Failed to initialize FreeTDS db-lib\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	dberrhandle(tds_error_handler);
	dbmsghandle(tds_message_handler);

	settings = ast_calloc_with_stringfields(1, struct cdr_tds_config, 256);

	if (!settings) {
		dbexit();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!tds_load_module(0)) {
		ast_string_field_free_memory(settings);
		ast_free(settings);
		settings = NULL;
		dbexit();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cdr_register(name, ast_module_info->description, tds_log);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return tds_unload_module();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "FreeTDS CDR Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cdr",
);
