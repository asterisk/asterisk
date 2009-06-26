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

#include <time.h>

#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"

#define DATE_FORMAT 	"%Y-%m-%d %T"
#define CONF_FILE	"cdr_manager.conf"
#define CUSTOM_FIELDS_BUF_SIZE 1024

static char *name = "cdr_manager";

static int enablecdr = 0;

static struct ast_str *customfields;
AST_RWLOCK_DEFINE_STATIC(customfields_lock);

static int manager_log(struct ast_cdr *cdr);

static int load_config(int reload)
{
	char *cat = NULL;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int newenablecdr = 0;

	cfg = ast_config_load(CONF_FILE, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file '%s' could not be parsed\n", CONF_FILE);
		return -1;
	}

	if (!cfg) {
		/* Standard configuration */
		ast_log(LOG_WARNING, "Failed to load configuration file. Module not activated.\n");
		if (enablecdr)
			ast_cdr_unregister(name);
		enablecdr = 0;
		return -1;
	}

	if (reload) {
		ast_rwlock_wrlock(&customfields_lock);
	}

	if (reload && customfields) {
		ast_free(customfields);
		customfields = NULL;
	}

	while ( (cat = ast_category_browse(cfg, cat)) ) {
		if (!strcasecmp(cat, "general")) {
			v = ast_variable_browse(cfg, cat);
			while (v) {
				if (!strcasecmp(v->name, "enabled"))
					newenablecdr = ast_true(v->value);

				v = v->next;
			}
		} else if (!strcasecmp(cat, "mappings")) {
			customfields = ast_str_create(CUSTOM_FIELDS_BUF_SIZE);
			v = ast_variable_browse(cfg, cat);
			while (v) {
				if (customfields && !ast_strlen_zero(v->name) && !ast_strlen_zero(v->value)) {
					if ((ast_str_strlen(customfields) + strlen(v->value) + strlen(v->name) + 14) < ast_str_size(customfields)) {
						ast_str_append(&customfields, -1, "%s: ${CDR(%s)}\r\n", v->value, v->name);
						ast_log(LOG_NOTICE, "Added mapping %s: ${CDR(%s)}\n", v->value, v->name);
					} else {
						ast_log(LOG_WARNING, "No more buffer space to add other custom fields\n");
						break;
					}

				}
				v = v->next;
			}
		}
	}

	if (reload) {
		ast_rwlock_unlock(&customfields_lock);
	}

	ast_config_destroy(cfg);

	if (enablecdr && !newenablecdr)
		ast_cdr_unregister(name);
	else if (!enablecdr && newenablecdr)
		ast_cdr_register(name, "Asterisk Manager Interface CDR Backend", manager_log);
	enablecdr = newenablecdr;

	return 0;
}

static int manager_log(struct ast_cdr *cdr)
{
	struct ast_tm timeresult;
	char strStartTime[80] = "";
	char strAnswerTime[80] = "";
	char strEndTime[80] = "";
	char buf[CUSTOM_FIELDS_BUF_SIZE];

	if (!enablecdr)
		return 0;

	ast_localtime(&cdr->start, &timeresult, NULL);
	ast_strftime(strStartTime, sizeof(strStartTime), DATE_FORMAT, &timeresult);

	if (cdr->answer.tv_sec)	{
		ast_localtime(&cdr->answer, &timeresult, NULL);
		ast_strftime(strAnswerTime, sizeof(strAnswerTime), DATE_FORMAT, &timeresult);
	}

	ast_localtime(&cdr->end, &timeresult, NULL);
	ast_strftime(strEndTime, sizeof(strEndTime), DATE_FORMAT, &timeresult);

	buf[0] = '\0';
	ast_rwlock_rdlock(&customfields_lock);
	if (customfields && ast_str_strlen(customfields)) {
		struct ast_channel *dummy = ast_dummy_channel_alloc();
		if (!dummy) {
			ast_log(LOG_ERROR, "Unable to allocate channel for variable substitution.\n");
			return 0;
		}
		dummy->cdr = ast_cdr_dup(cdr);
		pbx_substitute_variables_helper(dummy, ast_str_buffer(customfields), buf, sizeof(buf) - 1);
		ast_channel_release(dummy);
	}
	ast_rwlock_unlock(&customfields_lock);

	manager_event(EVENT_FLAG_CDR, "Cdr",
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
	    "UserField: %s\r\n"
	    "%s",
	    cdr->accountcode, cdr->src, cdr->dst, cdr->dcontext, cdr->clid, cdr->channel,
	    cdr->dstchannel, cdr->lastapp, cdr->lastdata, strStartTime, strAnswerTime, strEndTime,
	    cdr->duration, cdr->billsec, ast_cdr_disp2str(cdr->disposition),
	    ast_cdr_flags2str(cdr->amaflags), cdr->uniqueid, cdr->userfield,buf);

	return 0;
}

static int unload_module(void)
{
	ast_cdr_unregister(name);
	if (customfields)
		ast_free(customfields);

	return 0;
}

static int load_module(void)
{
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk Manager Interface CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
