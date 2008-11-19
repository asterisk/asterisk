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
 * \brief Custom Comma Separated Value CDR records.
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg See also \ref AstCDR
 *
 * Logs in LOG_DIR/cdr_custom
 * \ingroup cdr_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"

#define CUSTOM_LOG_DIR "/cdr_custom"

#define DATE_FORMAT "%Y-%m-%d %T"

AST_MUTEX_DEFINE_STATIC(lock);
AST_MUTEX_DEFINE_STATIC(mf_lock);

static char *name = "cdr-custom";

static char master[PATH_MAX];
static char format[1024]="";

static int load_config(int reload) 
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int res = -1;

	if ((cfg = ast_config_load("cdr_custom.conf", config_flags)) == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Invalid config file\n");
		return 1;
	}

	strcpy(format, "");
	strcpy(master, "");
	ast_mutex_lock(&lock);
	if (cfg) {
		var = ast_variable_browse(cfg, "mappings");
		while(var) {
			if (!ast_strlen_zero(var->name) && !ast_strlen_zero(var->value)) {
				if (strlen(var->value) > (sizeof(format) - 1))
					ast_log(LOG_WARNING, "Format string too long, will be truncated, at line %d\n", var->lineno);
				ast_copy_string(format, var->value, sizeof(format) - 1);
				strcat(format,"\n");
				snprintf(master, sizeof(master),"%s/%s/%s", ast_config_AST_LOG_DIR, name, var->name);
				if (var->next) {
					ast_log(LOG_NOTICE, "Sorry, only one mapping is supported at this time, mapping '%s' will be ignored at line %d.\n", var->next->name, var->next->lineno); 
					break;
				}
			} else
				ast_log(LOG_NOTICE, "Mapping must have both filename and format at line %d\n", var->lineno);
			var = var->next;
		}
		ast_config_destroy(cfg);
		res = 0;
	} else {
		if (reload)
			ast_log(LOG_WARNING, "Failed to reload configuration file.\n");
		else
			ast_log(LOG_WARNING, "Failed to load configuration file. Module not activated.\n");
	}
	ast_mutex_unlock(&lock);
	
	return res;
}



static int custom_log(struct ast_cdr *cdr)
{
	FILE *mf = NULL;

	/* Make sure we have a big enough buf */
	char buf[2048];
	struct ast_channel dummy;

	/* Abort if no master file is specified */
	if (ast_strlen_zero(master))
		return 0;

	/* Quite possibly the first use of a static struct ast_channel, we need it so the var funcs will work */
	memset(&dummy, 0, sizeof(dummy));
	dummy.cdr = cdr;
	pbx_substitute_variables_helper(&dummy, format, buf, sizeof(buf) - 1);

	/* because of the absolutely unconditional need for the
	   highest reliability possible in writing billing records,
	   we open write and close the log file each time */
	ast_mutex_lock(&mf_lock);
	mf = fopen(master, "a");
	if (mf) {
		fputs(buf, mf);
		fflush(mf); /* be particularly anal here */
		fclose(mf);
		mf = NULL;
		ast_mutex_unlock(&mf_lock);
	} else {
		ast_log(LOG_ERROR, "Unable to re-open master file %s : %s\n", master, strerror(errno));
		ast_mutex_unlock(&mf_lock);
	}

	return 0;
}

static int unload_module(void)
{
	ast_cdr_unregister(name);
	return 0;
}

static int load_module(void)
{
	int res = 0;

	if (!load_config(0)) {
		res = ast_cdr_register(name, ast_module_info->description, custom_log);
		if (res)
			ast_log(LOG_ERROR, "Unable to register custom CDR handling\n");
		return res;
	} else 
		return AST_MODULE_LOAD_DECLINE;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Customizable Comma Separated Values CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

