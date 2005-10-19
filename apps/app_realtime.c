/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 * Mark Spencer <markster@digium.com>
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

/*
 *
 * RealTime App
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"

#define next_one(var) var = var->next
#define crop_data(str) { *(str) = '\0' ; (str)++; }

static char *tdesc = "Realtime Data Lookup/Rewrite";
static char *app = "RealTime";
static char *uapp = "RealTimeUpdate";
static char *synopsis = "Realtime Data Lookup";
static char *usynopsis = "Realtime Data Rewrite";
static char *USAGE = "RealTime(<family>|<colmatch>|<value>[|<prefix>])";
static char *UUSAGE = "RealTimeUpdate(<family>|<colmatch>|<value>|<newcol>|<newval>)";
static char *desc = "Use the RealTime config handler system to read data into channel variables.\n"
"RealTime(<family>|<colmatch>|<value>[|<prefix>])\n\n"
"All unique column names will be set as channel variables with optional prefix to the name.\n"
"e.g. prefix of 'var_' would make the column 'name' become the variable ${var_name}\n\n";
static char *udesc = "Use the RealTime config handler system to update a value\n"
"RealTimeUpdate(<family>|<colmatch>|<value>|<newcol>|<newval>)\n\n"
"The column <newcol> in 'family' matching column <colmatch>=<value> will be updated to <newval>\n";

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

static int cli_load_realtime(int fd, int argc, char **argv) 
{
	char *header_format = "%30s  %-30s\n";
	struct ast_variable *var=NULL;

	if(argc<5) {
		ast_cli(fd, "You must supply a family name, a column to match on, and a value to match to.\n");
		return RESULT_FAILURE;
	}

	var = ast_load_realtime(argv[2], argv[3], argv[4], NULL);

	if(var) {
		ast_cli(fd, header_format, "Column Name", "Column Value");
		ast_cli(fd, header_format, "--------------------", "--------------------");
		while(var) {
			ast_cli(fd, header_format, var->name, var->value);
			var = var->next;
		}
	} else {
		ast_cli(fd, "No rows found matching search criteria.\n");
	}
	return RESULT_SUCCESS;
}

static int cli_update_realtime(int fd, int argc, char **argv) {
	int res = 0;

	if(argc<7) {
		ast_cli(fd, "You must supply a family name, a column to update on, a new value, column to match, and value to to match.\n");
		ast_cli(fd, "Ex: realtime update sipfriends name bobsphone port 4343\n will execute SQL as UPDATE sipfriends SET port = 4343 WHERE name = bobsphone\n");
		return RESULT_FAILURE;
	}

	res = ast_update_realtime(argv[2], argv[3], argv[4], argv[5], argv[6], NULL);

	if(res < 0) {
		ast_cli(fd, "Failed to update. Check the debug log for possible SQL related entries.\n");
		return RESULT_SUCCESS;
	}

       ast_cli(fd, "Updated %d RealTime record%s.\n", res, (res != 1) ? "s" : "");

	return RESULT_SUCCESS;
}

static char cli_load_realtime_usage[] =
"Usage: realtime load <family> <colmatch> <value>\n"
"       Prints out a list of variables using the RealTime driver.\n";

static struct ast_cli_entry cli_load_realtime_cmd = {
        { "realtime", "load", NULL, NULL }, cli_load_realtime,
        "Used to print out RealTime variables.", cli_load_realtime_usage, NULL };

static char cli_update_realtime_usage[] =
"Usage: realtime update <family> <colmatch> <value>\n"
"       Update a single variable using the RealTime driver.\n";

static struct ast_cli_entry cli_update_realtime_cmd = {
        { "realtime", "update", NULL, NULL }, cli_update_realtime,
        "Used to update RealTime variables.", cli_update_realtime_usage, NULL };

static int realtime_update_exec(struct ast_channel *chan, void *data) 
{
	char *family=NULL, *colmatch=NULL, *value=NULL, *newcol=NULL, *newval=NULL;
	struct localuser *u;
	int res = 0;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",UUSAGE);
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	if ((family = ast_strdupa(data))) {
		if ((colmatch = strchr(family,'|'))) {
			crop_data(colmatch);
			if ((value = strchr(colmatch,'|'))) {
				crop_data(value);
				if ((newcol = strchr(value,'|'))) {
					crop_data(newcol);
					if ((newval = strchr(newcol,'|'))) 
						crop_data(newval);
				}
			}
		}
	}
	if (! (family && value && colmatch && newcol && newval) ) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",UUSAGE);
		res = -1;
	} else {
		ast_update_realtime(family,colmatch,value,newcol,newval,NULL);
	}

	LOCAL_USER_REMOVE(u);
	
	return res;
}


static int realtime_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct ast_variable *var, *itt;
	char *family=NULL, *colmatch=NULL, *value=NULL, *prefix=NULL, *vname=NULL;
	size_t len;
		
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	if ((family = ast_strdupa(data))) {
		if ((colmatch = strchr(family,'|'))) {
			crop_data(colmatch);
			if ((value = strchr(colmatch,'|'))) {
				crop_data(value);
				if ((prefix = strchr(value,'|')))
					crop_data(prefix);
			}
		}
	}
	if (! (family && value && colmatch) ) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		res = -1;
	} else {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4"Realtime Lookup: family:'%s' colmatch:'%s' value:'%s'\n",family,colmatch,value);
		if ((var = ast_load_realtime(family, colmatch, value, NULL))) {
			for (itt = var; itt; itt = itt->next) {
				if(prefix) {
					len = strlen(prefix) + strlen(itt->name) + 2;
					vname = alloca(len);
					snprintf(vname,len,"%s%s",prefix,itt->name);
					
				} else 
					vname = itt->name;

				pbx_builtin_setvar_helper(chan, vname, itt->value);
			}
			ast_variables_destroy(var);
		} else if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4"No Realtime Matches Found.\n");
	}
	
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;

	res = ast_cli_unregister(&cli_load_realtime_cmd);
	res |= ast_cli_unregister(&cli_update_realtime_cmd);
	res |= ast_unregister_application(uapp);
	res |= ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_cli_register(&cli_load_realtime_cmd);
	res |= ast_cli_register(&cli_update_realtime_cmd);
	res |= ast_register_application(uapp, realtime_update_exec, usynopsis, udesc);
	res |= ast_register_application(app, realtime_exec, synopsis, desc);

	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

