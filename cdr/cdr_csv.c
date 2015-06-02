/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Includes code and algorithms from the Zapata library.
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
 * \brief Comma Separated Value CDR records.
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg See also \ref AstCDR
 * \ingroup cdr_drivers
 */

/*! \li \ref cdr_csv.c uses the configuration file \ref cdr.conf
 * \addtogroup configuration_file Configuration Files
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"

#define CSV_LOG_DIR "/cdr-csv"
#define CSV_MASTER  "/Master.csv"

#define DATE_FORMAT "%Y-%m-%d %T"

static int usegmtime = 0;
static int accountlogs = 1;
static int loguniqueid = 0;
static int loguserfield = 0;
static int loaded = 0;
static int newcdrcolumns = 0;
static const char config[] = "cdr.conf";
static char file_csv_master[PATH_MAX];

/* #define CSV_LOGUNIQUEID 1 */
/* #define CSV_LOGUSERFIELD 1 */

/*----------------------------------------------------
  The values are as follows:


  "accountcode", 	accountcode is the account name of detail records, Master.csv contains all records *
  			Detail records are configured on a channel basis, IAX and SIP are determined by user *
			DAHDI is determined by channel in dahdi.conf
  "source",
  "destination",
  "destination context",
  "callerid",
  "channel",
  "destination channel",	(if applicable)
  "last application",	Last application run on the channel
  "last app argument",	argument to the last channel
  "start time",
  "answer time",
  "end time",
  duration,   		Duration is the whole length that the entire call lasted. ie. call rx'd to hangup
  			"end time" minus "start time"
  billable seconds, 	the duration that a call was up after other end answered which will be <= to duration
  			"end time" minus "answer time"
  "disposition",    	ANSWERED, NO ANSWER, BUSY
  "amaflags",       	DOCUMENTATION, BILL, IGNORE etc, specified on a per channel basis like accountcode.
  "uniqueid",           unique call identifier
  "userfield"		user field set via SetCDRUserField
----------------------------------------------------------*/

static char *name = "csv";

AST_MUTEX_DEFINE_STATIC(f_lock);

static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if (!(cfg = ast_config_load(config, config_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "unable to load config: %s\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 1;
	}

	accountlogs = 1;
	usegmtime = 0;
	loguniqueid = 0;
	loguserfield = 0;
	newcdrcolumns = 0;

	if (!(v = ast_variable_browse(cfg, "csv"))) {
		ast_config_destroy(cfg);
		return 0;
	}

	/* compute the location of the csv master file */
	ast_mutex_lock(&f_lock);
	snprintf(file_csv_master, sizeof(file_csv_master),
		"%s/%s/%s", ast_config_AST_LOG_DIR, CSV_LOG_DIR, CSV_MASTER);
	ast_mutex_unlock(&f_lock);

	for (; v; v = v->next) {
		if (!strcasecmp(v->name, "usegmtime")) {
			usegmtime = ast_true(v->value);
		} else if (!strcasecmp(v->name, "accountlogs")) {
			/* Turn on/off separate files per accountcode. Default is on (as before) */
			accountlogs = ast_true(v->value);
		} else if (!strcasecmp(v->name, "loguniqueid")) {
			loguniqueid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "loguserfield")) {
			loguserfield = ast_true(v->value);
		} else if (!strcasecmp(v->name, "newcdrcolumns")) {
			newcdrcolumns = ast_true(v->value);
		}

	}
	ast_config_destroy(cfg);
	return 1;
}

static int append_string(char *buf, const char *s, size_t bufsize)
{
	int pos = strlen(buf), spos = 0, error = -1;

	if (pos >= bufsize - 4)
		return -1;

	buf[pos++] = '\"';

	while(pos < bufsize - 3) {
		if (!s[spos]) {
			error = 0;
			break;
		}
		if (s[spos] == '\"')
			buf[pos++] = '\"';
		buf[pos++] = s[spos];
		spos++;
	}

	buf[pos++] = '\"';
	buf[pos++] = ',';
	buf[pos++] = '\0';

	return error;
}

static int append_int(char *buf, int s, size_t bufsize)
{
	char tmp[32];
	int pos = strlen(buf);

	snprintf(tmp, sizeof(tmp), "%d", s);

	if (pos + strlen(tmp) > bufsize - 3)
		return -1;

	strncat(buf, tmp, bufsize - strlen(buf) - 1);
	pos = strlen(buf);
	buf[pos++] = ',';
	buf[pos++] = '\0';

	return 0;
}

static int append_date(char *buf, struct timeval when, size_t bufsize)
{
	char tmp[80] = "";
	struct ast_tm tm;

	if (strlen(buf) > bufsize - 3)
		return -1;

	if (ast_tvzero(when)) {
		strncat(buf, ",", bufsize - strlen(buf) - 1);
		return 0;
	}

	ast_localtime(&when, &tm, usegmtime ? "GMT" : NULL);
	ast_strftime(tmp, sizeof(tmp), DATE_FORMAT, &tm);

	return append_string(buf, tmp, bufsize);
}

static int build_csv_record(char *buf, size_t bufsize, struct ast_cdr *cdr)
{

	buf[0] = '\0';
	/* Account code */
	append_string(buf, cdr->accountcode, bufsize);
	/* Source */
	append_string(buf, cdr->src, bufsize);
	/* Destination */
	append_string(buf, cdr->dst, bufsize);
	/* Destination context */
	append_string(buf, cdr->dcontext, bufsize);
	/* Caller*ID */
	append_string(buf, cdr->clid, bufsize);
	/* Channel */
	append_string(buf, cdr->channel, bufsize);
	/* Destination Channel */
	append_string(buf, cdr->dstchannel, bufsize);
	/* Last Application */
	append_string(buf, cdr->lastapp, bufsize);
	/* Last Data */
	append_string(buf, cdr->lastdata, bufsize);
	/* Start Time */
	append_date(buf, cdr->start, bufsize);
	/* Answer Time */
	append_date(buf, cdr->answer, bufsize);
	/* End Time */
	append_date(buf, cdr->end, bufsize);
	/* Duration */
	append_int(buf, cdr->duration, bufsize);
	/* Billable seconds */
	append_int(buf, cdr->billsec, bufsize);
	/* Disposition */
	append_string(buf, ast_cdr_disp2str(cdr->disposition), bufsize);
	/* AMA Flags */
	append_string(buf, ast_channel_amaflags2string(cdr->amaflags), bufsize);
	/* Unique ID */
	if (loguniqueid)
		append_string(buf, cdr->uniqueid, bufsize);
	/* append the user field */
	if(loguserfield)
		append_string(buf, cdr->userfield, bufsize);
	if (newcdrcolumns) {
		append_string(buf, cdr->peeraccount, bufsize);
		append_string(buf, cdr->linkedid, bufsize);
		append_int(buf, cdr->sequence, bufsize);
	}
	/* If we hit the end of our buffer, log an error */
	if (strlen(buf) < bufsize - 5) {
		/* Trim off trailing comma */
		buf[strlen(buf) - 1] = '\0';
		strncat(buf, "\n", bufsize - strlen(buf) - 1);
		return 0;
	}
	return -1;
}

static int writefile(char *s, char *file_path)
{
	FILE *f;
	/* because of the absolutely unconditional need for the
	   highest reliability possible in writing billing records,
	   we open write and close the log file each time */
	if (!(f = fopen(file_path, "a"))) {
		ast_log(LOG_ERROR, "Unable to open file %s : %s\n", file_path, strerror(errno));
		return -1;
	}
	fputs(s, f);
	fflush(f); /* be particularly anal here */
	fclose(f);

	return 0;
}


static int writefile_account(char *s, char *acc)
{
	char file_account[PATH_MAX];
	if (strchr(acc, '/') || (acc[0] == '.')) {
		ast_log(LOG_WARNING, "Account code '%s' insecure for writing file\n", acc);
		return -1;
	}
	snprintf(file_account, sizeof(file_account), "%s/%s/%s.csv", ast_config_AST_LOG_DIR,CSV_LOG_DIR, acc);
	return writefile(s, file_account);
}

static int csv_log(struct ast_cdr *cdr)
{
	/* Make sure we have a big enough buf */
	char buf[1024];
	if (build_csv_record(buf, sizeof(buf), cdr)) {
		ast_log(LOG_WARNING, "Unable to create CSV record in %d bytes.  CDR not recorded!\n", (int)sizeof(buf));
		return 0;
	}

	ast_mutex_lock(&f_lock);
	if (writefile(buf, file_csv_master))
		ast_log(LOG_WARNING, "Unable to write CSV record to master '%s' : %s\n", file_csv_master, strerror(errno));

	if (accountlogs && !ast_strlen_zero(cdr->accountcode)) {
		if (writefile_account(buf, cdr->accountcode))
			ast_log(LOG_WARNING, "Unable to write CSV record to account file '%s' : %s\n", cdr->accountcode, strerror(errno));
	}
	ast_mutex_unlock(&f_lock);
	return 0;
}

static int unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	loaded = 0;
	return 0;
}

static int load_module(void)
{
	int res;

	if (!load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if ((res = ast_cdr_register(name, ast_module_info->description, csv_log))) {
		ast_log(LOG_ERROR, "Unable to register CSV CDR handling\n");
	} else {
		loaded = 1;
	}
	return res;
}

static int reload(void)
{
	if (load_config(1)) {
		loaded = 1;
	} else {
		loaded = 0;
		ast_log(LOG_WARNING, "No [csv] section in cdr.conf.  Unregistering backend.\n");
		ast_cdr_unregister(name);
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Comma Separated Values CDR Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
