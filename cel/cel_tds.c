/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
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
 * \brief FreeTDS CEL logger
 *
 * See also
 * \arg \ref Config_cel
 * \arg http://www.freetds.org/
 * \ingroup cel_drivers
 */

/*! \verbatim
 *
 * Table Structure for `cel`
 *

CREATE TABLE [dbo].[cel] (
	[accountcode] [varchar] (20) NULL ,
	[cidname] [varchar] (80) NULL ,
	[cidnum] [varchar] (80) NULL ,
	[cidani] [varchar] (80) NULL ,
	[cidrdnis] [varchar] (80) NULL ,
	[ciddnid] [varchar] (80) NULL ,
	[exten] [varchar] (80) NULL ,
	[context] [varchar] (80) NULL ,
	[channame] [varchar] (80) NULL ,
	[appname] [varchar] (80) NULL ,
	[appdata] [varchar] (80) NULL ,
	[eventtime] [datetime] NULL ,
	[eventtype] [varchar] (32) NULL ,
	[uniqueid] [varchar] (32) NULL ,
	[linkedid] [varchar] (32) NULL ,
	[amaflags] [varchar] (16) NULL ,
	[userfield] [varchar] (32) NULL ,
	[peer] [varchar] (32) NULL
) ON [PRIMARY]

\endverbatim

*/

/*** MODULEINFO
	<depend>freetds</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>
#include <math.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"

#include <sqlfront.h>
#include <sybdb.h>

#ifdef FREETDS_PRE_0_62
#warning "You have older TDS, you should upgrade!"
#endif

#define DATE_FORMAT "%Y/%m/%d %T"

#define TDS_BACKEND_NAME "CEL TDS logging backend"

static char *config = "cel_tds.conf";

struct cel_tds_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(connection);
		AST_STRING_FIELD(database);
		AST_STRING_FIELD(username);
		AST_STRING_FIELD(password);
		AST_STRING_FIELD(table);
		AST_STRING_FIELD(charset);
		AST_STRING_FIELD(language);
	);
	DBPROCESS *dbproc;
	unsigned int connected:1;
};

AST_MUTEX_DEFINE_STATIC(tds_lock);

static struct cel_tds_config *settings;

static char *anti_injection(const char *, int);
static void get_date(char *, size_t len, struct timeval);

static int execute_and_consume(DBPROCESS *dbproc, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static int mssql_connect(void);
static int mssql_disconnect(void);

static void tds_log(struct ast_event *event)
{
	char start[80];
	char *accountcode_ai, *clidnum_ai, *exten_ai, *context_ai, *clid_ai, *channel_ai, *app_ai, *appdata_ai, *uniqueid_ai, *linkedid_ai, *cidani_ai, *cidrdnis_ai, *ciddnid_ai, *peer_ai, *userfield_ai;
	RETCODE erc;
	int attempt = 1;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_mutex_lock(&tds_lock);

	accountcode_ai = anti_injection(record.account_code, 20);
	clidnum_ai     = anti_injection(record.caller_id_num, 80);
	clid_ai        = anti_injection(record.caller_id_name, 80);
	cidani_ai      = anti_injection(record.caller_id_ani, 80);
	cidrdnis_ai    = anti_injection(record.caller_id_rdnis, 80);
	ciddnid_ai     = anti_injection(record.caller_id_dnid, 80);
	exten_ai       = anti_injection(record.extension, 80);
	context_ai     = anti_injection(record.context, 80);
	channel_ai     = anti_injection(record.channel_name, 80);
	app_ai         = anti_injection(record.application_name, 80);
	appdata_ai     = anti_injection(record.application_data, 80);
	uniqueid_ai    = anti_injection(record.unique_id, 32);
	linkedid_ai    = anti_injection(record.linked_id, 32);
	userfield_ai   = anti_injection(record.user_field, 32);
	peer_ai        = anti_injection(record.peer, 32);

	get_date(start, sizeof(start), record.event_time);

retry:
	/* Ensure that we are connected */
	if (!settings->connected) {
		ast_log(LOG_NOTICE, "Attempting to reconnect to %s (Attempt %d)\n", settings->connection, attempt);
		if (mssql_connect()) {
			/* Connect failed */
			if (attempt++ < 3) {
				goto retry;
			}
			goto done;
		}
	}

	erc = dbfcmd(settings->dbproc,
		"INSERT INTO %s "
		"("
		"accountcode,"
		"cidnum,"
		"cidname,"
		"cidani,"
		"cidrdnis,"
		"ciddnid,"
		"exten,"
		"context,"
		"channel,"
		"appname,"
		"appdata,"
		"eventtime,"
		"eventtype,"
		"amaflags, "
		"uniqueid,"
		"linkedid,"
		"userfield,"
		"peer"
		") "
		"VALUES "
		"("
		"'%s',"	/* accountcode */
		"'%s',"	/* clidnum */
		"'%s',"	/* clid */
		"'%s',"	/* cid-ani */
		"'%s',"	/* cid-rdnis */
		"'%s',"	/* cid-dnid */
		"'%s',"	/* exten */
		"'%s',"	/* context */
		"'%s',"	/* channel */
		"'%s',"	/* app */
		"'%s',"	/* appdata */
		"%s, "	/* eventtime */
		"'%s',"	/* eventtype */
		"'%s',"	/* amaflags */
		"'%s',"	/* uniqueid */
		"'%s',"	/* linkedid */
		"'%s',"	/* userfield */
		"'%s'"	/* peer */
		")",
		settings->table, accountcode_ai, clidnum_ai, clid_ai, cidani_ai, cidrdnis_ai,
		ciddnid_ai, exten_ai, context_ai, channel_ai, app_ai, appdata_ai, start,
		(record.event_type == AST_CEL_USER_DEFINED)
			? record.user_defined_name : record.event_name,
					ast_channel_amaflags2string(record.amaflag), uniqueid_ai, linkedid_ai,
		userfield_ai, peer_ai);

	if (erc == FAIL) {
		if (attempt++ < 3) {
			ast_log(LOG_NOTICE, "Failed to build INSERT statement, retrying...\n");
			mssql_disconnect();
			goto retry;
		} else {
			ast_log(LOG_ERROR, "Failed to build INSERT statement, no CEL was logged.\n");
			goto done;
		}
	}

	if (dbsqlexec(settings->dbproc) == FAIL) {
		if (attempt++ < 3) {
			ast_log(LOG_NOTICE, "Failed to execute INSERT statement, retrying...\n");
			mssql_disconnect();
			goto retry;
		} else {
			ast_log(LOG_ERROR, "Failed to execute INSERT statement, no CEL was logged.\n");
			goto done;
		}
	}

	/* Consume any results we might get back (this is more of a sanity check than
	 * anything else, since an INSERT shouldn't return results). */
	while (dbresults(settings->dbproc) != NO_MORE_RESULTS) {
		while (dbnextrow(settings->dbproc) != NO_MORE_ROWS);
	}

done:
	ast_mutex_unlock(&tds_lock);

	ast_free(accountcode_ai);
	ast_free(clidnum_ai);
	ast_free(clid_ai);
	ast_free(cidani_ai);
	ast_free(cidrdnis_ai);
	ast_free(ciddnid_ai);
	ast_free(exten_ai);
	ast_free(context_ai);
	ast_free(channel_ai);
	ast_free(app_ai);
	ast_free(appdata_ai);
	ast_free(uniqueid_ai);
	ast_free(linkedid_ai);
	ast_free(userfield_ai);
	ast_free(peer_ai);

	return;
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

	DBSETLAPP(login,  "TSQL");
	DBSETLUSER(login, (char *) settings->username);
	DBSETLPWD(login,  (char *) settings->password);

	if (!ast_strlen_zero(settings->charset)) {
		DBSETLCHARSET(login, (char *) settings->charset);
	}

	if (!ast_strlen_zero(settings->language)) {
		DBSETLNATLANG(login, (char *) settings->language);
	}

	if ((settings->dbproc = dbopen(login, (char *) settings->connection)) == NULL) {
		ast_log(LOG_ERROR, "Unable to connect to %s\n", settings->connection);
		dbloginfree(login);
		return -1;
	}

	dbloginfree(login);

	if (dbuse(settings->dbproc, (char *) settings->database) == FAIL) {
		ast_log(LOG_ERROR, "Unable to select database %s\n", settings->database);
		goto failed;
	}

	if (execute_and_consume(settings->dbproc, "SELECT 1 FROM [%s]", settings->table)) {
		ast_log(LOG_ERROR, "Unable to find table '%s'\n", settings->table);
		goto failed;
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
	ast_cel_backend_unregister(TDS_BACKEND_NAME);

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
		ast_log(LOG_NOTICE, "Unable to load TDS config for CELs: %s\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (!ast_variable_browse(cfg, "global")) {
		/* nothing configured */
		ast_config_destroy(cfg);
		ast_log(LOG_NOTICE, "cel_tds has no global category, nothing to configure.\n");
		return 0;
	}

	ast_mutex_lock(&tds_lock);

	/* Clear out any existing settings */
	ast_string_field_init(settings, 0);

	ptr = ast_variable_retrieve(cfg, "global", "connection");
	if (ptr) {
		ast_string_field_set(settings, connection, ptr);
	} else {
		ast_log(LOG_ERROR, "Failed to connect: Database connection name not specified.\n");
		goto failed;
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
	}

	ptr = ast_variable_retrieve(cfg, "global", "language");
	if (ptr) {
		ast_string_field_set(settings, language, ptr);
	}

	ptr = ast_variable_retrieve(cfg, "global", "table");
	if (ptr) {
		ast_string_field_set(settings, table, ptr);
	} else {
		ast_log(LOG_NOTICE, "Table name not specified, using 'cel' by default.\n");
		ast_string_field_set(settings, table, "cel");
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

	settings = ast_calloc_with_stringfields(1, struct cel_tds_config, 256);

	if (!settings) {
		dbexit();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!tds_load_module(0)) {
		ast_string_field_free_memory(settings);
		ast_free(settings);
		settings = NULL;
		dbexit();
		ast_log(LOG_WARNING,"cel_tds module had config problems; declining load\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Register MSSQL CEL handler */
	if (ast_cel_backend_register(TDS_BACKEND_NAME, tds_log)) {
		ast_log(LOG_ERROR, "Unable to register MSSQL CEL handling\n");
		ast_string_field_free_memory(settings);
		ast_free(settings);
		settings = NULL;
		dbexit();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return tds_unload_module();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "FreeTDS CEL Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
