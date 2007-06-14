/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005
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
 * \brief Asterisk Call Manager CDR records.
 * 
 * See also
 * \arg \ref AstCDR
 * \arg \ref AstAMI
 * \arg \ref Config_ami
 * \ingroup cdr_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"

#define DATE_FORMAT 	"%Y-%m-%d %T"
#define CONF_FILE	"cdr_manager.conf"

static char *name = "cdr_manager";

static int enablecdr = 0;

static int loadconfigurationfile(void)
{
	char *cat;
	struct ast_config *cfg;
	struct ast_variable *v;
	
	cfg = ast_config_load(CONF_FILE);
	if (!cfg) {
		/* Standard configuration */
		enablecdr = 0;
		return 0;
	}
	
	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		if (!strcasecmp(cat, "general")) {
			v = ast_variable_browse(cfg, cat);
			while (v) {
				if (!strcasecmp(v->name, "enabled")) {
					enablecdr = ast_true(v->value);
				}
				
				v = v->next;
			}
		}
	
		/* Next category */
		cat = ast_category_browse(cfg, cat);
	}
	
	ast_config_destroy(cfg);
	return 1;
}

static int manager_log(struct ast_cdr *cdr)
{
	time_t t;
	struct tm timeresult;
	char strStartTime[80] = "";
	char strAnswerTime[80] = "";
	char strEndTime[80] = "";
	
	if (!enablecdr)
		return 0;

	t = cdr->start.tv_sec;
	ast_localtime(&t, &timeresult, NULL);
	strftime(strStartTime, sizeof(strStartTime), DATE_FORMAT, &timeresult);
	
	if (cdr->answer.tv_sec)	{
    		t = cdr->answer.tv_sec;
    		ast_localtime(&t, &timeresult, NULL);
		strftime(strAnswerTime, sizeof(strAnswerTime), DATE_FORMAT, &timeresult);
	}

	t = cdr->end.tv_sec;
	ast_localtime(&t, &timeresult, NULL);
	strftime(strEndTime, sizeof(strEndTime), DATE_FORMAT, &timeresult);

	manager_event(EVENT_FLAG_CALL, "Cdr",
	    "AccountCode: %s\r\n"
	    "Source: %s\r\n"
	    "Destination: %s\r\n"
	    "DestinationContext: %s\r\n"
	    "CallerID: %s\r\n"
	    "Channel: %s\r\n"
	    "DestinationChannel: %s\r\n"
	    "LastApplication: %s\r\n"
	    "LastData: %s\r\n"
	    "StartTime: %s\r\n"
	    "AnswerTime: %s\r\n"
	    "EndTime: %s\r\n"
	    "Duration: %ld\r\n"
	    "BillableSeconds: %ld\r\n"
	    "Disposition: %s\r\n"
	    "AMAFlags: %s\r\n"
	    "UniqueID: %s\r\n"
	    "UserField: %s\r\n",
	    cdr->accountcode, cdr->src, cdr->dst, cdr->dcontext, cdr->clid, cdr->channel,
	    cdr->dstchannel, cdr->lastapp, cdr->lastdata, strStartTime, strAnswerTime, strEndTime,
	    cdr->duration, cdr->billsec, ast_cdr_disp2str(cdr->disposition), 
	    ast_cdr_flags2str(cdr->amaflags), cdr->uniqueid, cdr->userfield);
	    	
	return 0;
}

static int unload_module(void)
{
	ast_cdr_unregister(name);
	return 0;
}

static int load_module(void)
{
	int res;

	/* Configuration file */
	if (!loadconfigurationfile())
		return AST_MODULE_LOAD_DECLINE;
	
	res = ast_cdr_register(name, "Asterisk Manager Interface CDR Backend", manager_log);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register Asterisk Call Manager CDR handling\n");
	}
	
	return res;
}

static int reload(void)
{
	loadconfigurationfile();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk Manager Interface CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
