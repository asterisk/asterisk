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

/*! \file
 *
 * \brief Comma Separated Value CDR records.
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \arg See also \ref AstCDR
 * \ingroup cdr_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"

#define CSV_LOG_DIR "/cdr-csv"
#define CSV_MASTER  "/Master.csv"

#define DATE_FORMAT "%Y-%m-%d %T"

static int usegmtime = 0;
static int loguniqueid = 0;
static int loguserfield = 0;
static int loaded = 0;
static char *config = "cdr.conf";

/* #define CSV_LOGUNIQUEID 1 */
/* #define CSV_LOGUSERFIELD 1 */

/*----------------------------------------------------
  The values are as follows:


  "accountcode", 	accountcode is the account name of detail records, Master.csv contains all records *
  			Detail records are configured on a channel basis, IAX and SIP are determined by user *
			DAHDI is determined by channel in chan_dahdi.conf 
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

AST_MUTEX_DEFINE_STATIC(mf_lock);
AST_MUTEX_DEFINE_STATIC(acf_lock);

static int load_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *tmp;

	usegmtime = 0;
	loguniqueid = 0;
	loguserfield = 0;
	
	cfg = ast_config_load(config);
	
	if (!cfg) {
		ast_log(LOG_WARNING, "unable to load config: %s\n", config);
		return 0;
	} 
	
	var = ast_variable_browse(cfg, "csv");
	if (!var) {
		ast_config_destroy(cfg);
		return 0;
	}
	
	tmp = ast_variable_retrieve(cfg, "csv", "usegmtime");
	if (tmp) {
		usegmtime = ast_true(tmp);
		if (usegmtime) {
			ast_log(LOG_DEBUG, "logging time in GMT\n");
		}
	}

	tmp = ast_variable_retrieve(cfg, "csv", "loguniqueid");
	if (tmp) {
		loguniqueid = ast_true(tmp);
		if (loguniqueid) {
			ast_log(LOG_DEBUG, "logging CDR field UNIQUEID\n");
		}
	}

	tmp = ast_variable_retrieve(cfg, "csv", "loguserfield");
	if (tmp) {
		loguserfield = ast_true(tmp);
		if (loguserfield) {
			ast_log(LOG_DEBUG, "logging CDR user-defined field\n");
		}
	}

	ast_config_destroy(cfg);
	return 1;
}

static int append_string(char *buf, char *s, size_t bufsize)
{
	int pos = strlen(buf);
	int spos = 0;
	int error = 0;
	if (pos >= bufsize - 4)
		return -1;
	buf[pos++] = '\"';
	error = -1;
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

static int append_date(char *buf, struct timeval tv, size_t bufsize)
{
	char tmp[80] = "";
	struct tm tm;
	time_t t;
	t = tv.tv_sec;
	if (strlen(buf) > bufsize - 3)
		return -1;
	if (ast_tvzero(tv)) {
		strncat(buf, ",", bufsize - strlen(buf) - 1);
		return 0;
	}
	if (usegmtime) {
		gmtime_r(&t,&tm);
	} else {
		ast_localtime(&t, &tm, NULL);
	}
	strftime(tmp, sizeof(tmp), DATE_FORMAT, &tm);
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
	append_string(buf, ast_cdr_flags2str(cdr->amaflags), bufsize);
	/* Unique ID */
	if (loguniqueid)
		append_string(buf, cdr->uniqueid, bufsize);
	/* append the user field */
	if(loguserfield)
		append_string(buf, cdr->userfield,bufsize);	
	/* If we hit the end of our buffer, log an error */
	if (strlen(buf) < bufsize - 5) {
		/* Trim off trailing comma */
		buf[strlen(buf) - 1] = '\0';
		strncat(buf, "\n", bufsize - strlen(buf) - 1);
		return 0;
	}
	return -1;
}

static int writefile(char *s, char *acc)
{
	char tmp[PATH_MAX];
	FILE *f;
	if (strchr(acc, '/') || (acc[0] == '.')) {
		ast_log(LOG_WARNING, "Account code '%s' insecure for writing file\n", acc);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "%s/%s/%s.csv", (char *)ast_config_AST_LOG_DIR,CSV_LOG_DIR, acc);

	ast_mutex_lock(&acf_lock);
	f = fopen(tmp, "a");
	if (!f) {
		ast_mutex_unlock(&acf_lock);
		ast_log(LOG_ERROR, "Unable to open file %s : %s\n", tmp, strerror(errno));
		return -1;
	}
	fputs(s, f);
	fflush(f);
	fclose(f);
	ast_mutex_unlock(&acf_lock);

	return 0;
}


static int csv_log(struct ast_cdr *cdr)
{
	FILE *mf = NULL;
	/* Make sure we have a big enough buf */
	char buf[1024];
	char csvmaster[PATH_MAX];
	snprintf(csvmaster, sizeof(csvmaster),"%s/%s/%s", ast_config_AST_LOG_DIR, CSV_LOG_DIR, CSV_MASTER);
#if 0
	printf("[CDR] %s ('%s' -> '%s') Dur: %ds Bill: %ds Disp: %s Flags: %s Account: [%s]\n", cdr->channel, cdr->src, cdr->dst, cdr->duration, cdr->billsec, ast_cdr_disp2str(cdr->disposition), ast_cdr_flags2str(cdr->amaflags), cdr->accountcode);
#endif
	if (build_csv_record(buf, sizeof(buf), cdr)) {
		ast_log(LOG_WARNING, "Unable to create CSV record in %d bytes.  CDR not recorded!\n", (int)sizeof(buf));
	} else {
		/* because of the absolutely unconditional need for the
		   highest reliability possible in writing billing records,
		   we open write and close the log file each time */
		ast_mutex_lock(&mf_lock);
		mf = fopen(csvmaster, "a");
		if (mf) {
			fputs(buf, mf);
			fflush(mf); /* be particularly anal here */
			fclose(mf);
			mf = NULL;
			ast_mutex_unlock(&mf_lock);
		} else {
			ast_mutex_unlock(&mf_lock);
			ast_log(LOG_ERROR, "Unable to re-open master file %s : %s\n", csvmaster, strerror(errno));
		}

		if (!ast_strlen_zero(cdr->accountcode)) {
			if (writefile(buf, cdr->accountcode))
				ast_log(LOG_WARNING, "Unable to write CSV record to account file '%s' : %s\n", cdr->accountcode, strerror(errno));
		}
	}
	return 0;
}

static int unload_module(void)
{
	ast_cdr_unregister(name);
	loaded = 0;
	return 0;
}

static int load_module(void)
{
	int res;
	
	if(!load_config())
		return AST_MODULE_LOAD_DECLINE;

	res = ast_cdr_register(name, ast_module_info->description, csv_log);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register CSV CDR handling\n");
	} else {
		loaded = 1;
	}
	return res;
}

static int reload(void)
{
	if (load_config()) {
		loaded = 1;
	} else {
		loaded = 0;
		ast_log(LOG_WARNING, "No [csv] section in cdr.conf.  Unregistering backend.\n");
		ast_cdr_unregister(name);
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Comma Separated Values CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
