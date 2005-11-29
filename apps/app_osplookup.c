/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
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

/*! \file
 *
 * \brief Open Settlement Protocol Lookup
 * 
 * \ingroup applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astosp.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

static char *tdesc = "OSP Lookup";

static char *app = "OSPLookup";
static char *app2 = "OSPNext";
static char *app3 = "OSPFinish";

static char *synopsis = "Lookup number in OSP";
static char *synopsis2 = "Lookup next OSP entry";
static char *synopsis3 = "Record OSP entry";

static char *descrip = 
"  OSPLookup(exten[|provider[|options]]):  Looks up an extension via OSP and sets\n"
"the variables, where 'n' is the number of the result beginning with 1:\n"
" ${OSPTECH}:   The technology to use for the call\n"
" ${OSPDEST}:   The destination to use for the call\n"
" ${OSPTOKEN}:  The actual OSP token as a string\n"
" ${OSPHANDLE}: The OSP Handle for anything remaining\n"
" ${OSPRESULTS}: The number of OSP results total remaining\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the lookup was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPLOOKUPSTATUS	The status of the OSP Lookup attempt as a text string, one of\n"
"		SUCCESS | FAILED \n";


static char *descrip2 = 
"  OSPNext(cause[|options]):  Looks up the next OSP Destination for ${OSPHANDLE}\n"
"See OSPLookup for more information\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the lookup was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPNEXTSTATUS	The status of the OSP Next attempt as a text string, one of\n"
"		SUCCESS | FAILED \n";

static char *descrip3 = 
"  OSPFinish(status[|options]):  Records call state for ${OSPHANDLE}, according to\n"
"status, which should be one of BUSY, CONGESTION, ANSWER, NOANSWER, or CHANUNAVAIL\n"
"or coincidentally, just what the Dial application stores in its ${DIALSTATUS}.\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the finish attempt was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPFINISHSTATUS	The status of the OSP Finish attempt as a text string, one of\n"
"		SUCCESS | FAILED \n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int str2cause(char *cause)
{
	if (!strcasecmp(cause, "BUSY"))
		return AST_CAUSE_BUSY;
	if (!strcasecmp(cause, "CONGESTION"))
		return AST_CAUSE_CONGESTION;
	if (!strcasecmp(cause, "ANSWER"))
		return AST_CAUSE_NORMAL;
	if (!strcasecmp(cause, "CANCEL"))
		return AST_CAUSE_NORMAL;
	if (!strcasecmp(cause, "NOANSWER"))
		return AST_CAUSE_NOANSWER;
	if (!strcasecmp(cause, "NOCHANAVAIL"))
		return AST_CAUSE_CONGESTION;
	ast_log(LOG_WARNING, "Unknown cause '%s', using NORMAL\n", cause);
	return AST_CAUSE_NORMAL;
}

static int osplookup_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *temp;
	struct ast_osp_result result;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(extension);
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPLookup requires an argument OSPLookup(exten[|provider[|options]])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	temp = ast_strdupa(data);
	if (!temp) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, temp);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	ast_log(LOG_DEBUG, "Whoo hoo, looking up OSP on '%s' via '%s'\n", args.extension, args.provider ? args.provider : "<default>");
	if ((res = ast_osp_lookup(chan, args.provider, args.extension, chan->cid.cid_num, &result)) > 0) {
		char tmp[80];
		snprintf(tmp, sizeof(tmp), "%d", result.handle);
		pbx_builtin_setvar_helper(chan, "_OSPHANDLE", tmp);
		pbx_builtin_setvar_helper(chan, "_OSPTECH", result.tech);
		pbx_builtin_setvar_helper(chan, "_OSPDEST", result.dest);
		pbx_builtin_setvar_helper(chan, "_OSPTOKEN", result.token);
		snprintf(tmp, sizeof(tmp), "%d", result.numresults);
		pbx_builtin_setvar_helper(chan, "_OSPRESULTS", tmp);
		pbx_builtin_setvar_helper(chan, "OSPLOOKUPSTATUS", "SUCCESS");

	} else {
		if (!res) {
			ast_log(LOG_NOTICE, "OSP Lookup failed for '%s' (provider '%s')\n", args.extension, args.provider ? args.provider : "<default>");
			pbx_builtin_setvar_helper(chan, "OSPLOOKUPSTATUS", "FAILED");
		} else
			ast_log(LOG_DEBUG, "Got hangup on '%s' while doing OSP Lookup for '%s' (provider '%s')!\n", chan->name, args.extension, args.provider ? args.provider : "<default>" );
	}
	if (!res) {
		/* Look for a "busy" place */
		if (priority_jump || option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}

static int ospnext_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *temp;
	int cause;
	struct ast_osp_result result;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cause);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPNext should have an argument (cause[|options])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	temp = ast_strdupa(data);
	if (!temp) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, temp);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	cause = str2cause(args.cause);
	temp = pbx_builtin_getvar_helper(chan, "OSPHANDLE");
	result.handle = -1;
	if (!ast_strlen_zero(temp) && (sscanf(temp, "%d", &result.handle) == 1) && (result.handle > -1)) {
		temp = pbx_builtin_getvar_helper(chan, "OSPRESULTS");
		if (ast_strlen_zero(temp) || (sscanf(temp, "%d", &result.numresults) != 1)) {
			result.numresults = 0;
		}
		if ((res = ast_osp_next(&result, cause)) > 0) {
			char tmp[80];
			snprintf(tmp, sizeof(tmp), "%d", result.handle);
			pbx_builtin_setvar_helper(chan, "_OSPHANDLE", tmp);
			pbx_builtin_setvar_helper(chan, "_OSPTECH", result.tech);
			pbx_builtin_setvar_helper(chan, "_OSPDEST", result.dest);
			pbx_builtin_setvar_helper(chan, "_OSPTOKEN", result.token);
			snprintf(tmp, sizeof(tmp), "%d", result.numresults);
			pbx_builtin_setvar_helper(chan, "_OSPRESULTS", tmp);
			pbx_builtin_setvar_helper(chan, "OSPNEXTSTATUS", "SUCCESS");
		}
	} else {
		if (!res) {
			if (result.handle < 0)
				ast_log(LOG_NOTICE, "OSP Lookup Next failed for handle '%d'\n", result.handle);
			else
				ast_log(LOG_DEBUG, "No OSP handle specified\n");
			pbx_builtin_setvar_helper(chan, "OSPNEXTSTATUS", "FAILED");	
		} else
			ast_log(LOG_DEBUG, "Got hangup on '%s' while doing OSP Next!\n", chan->name);
	}
	if (!res) {
		/* Look for a "busy" place */
		if (priority_jump || option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}

static int ospfinished_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *temp;
	int cause;
	time_t start=0, duration=0;
	struct ast_osp_result result;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(status);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPFinish should have an argument (status[|options])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	temp = ast_strdupa(data);
	if (!temp) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, temp);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (chan->cdr) {
		start = chan->cdr->answer.tv_sec;
		if (start)
			duration = time(NULL) - start;
		else
			duration = 0;
	} else
		ast_log(LOG_WARNING, "OSPFinish called on channel '%s' with no CDR!\n", chan->name);
	
	cause = str2cause(args.status);
	temp = pbx_builtin_getvar_helper(chan, "OSPHANDLE");
	result.handle = -1;
	if (!ast_strlen_zero(temp) && (sscanf(temp, "%d", &result.handle) == 1) && (result.handle > -1)) {
		if (!ast_osp_terminate(result.handle, cause, start, duration)) {
			pbx_builtin_setvar_helper(chan, "_OSPHANDLE", "");
			pbx_builtin_setvar_helper(chan, "OSPFINISHSTATUS", "SUCCESS");
			res = 1;
		}
	} else {
		if (!res) {
			if (result.handle > -1)
				ast_log(LOG_NOTICE, "OSP Finish failed for handle '%d'\n", result.handle);
			else
				ast_log(LOG_DEBUG, "No OSP handle specified\n");
			pbx_builtin_setvar_helper(chan, "OSPFINISHSTATUS", "FAILED");
		} else
			ast_log(LOG_DEBUG, "Got hangup on '%s' while doing OSP Terminate!\n", chan->name);
	}
	if (!res) {
		/* Look for a "busy" place */
		if (priority_jump || option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}


int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(app3);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;
	
	res = ast_register_application(app, osplookup_exec, synopsis, descrip);
	res |= ast_register_application(app2, ospnext_exec, synopsis2, descrip2);
	res |= ast_register_application(app3, ospfinished_exec, synopsis3, descrip3);
	
	return res;
}

int reload(void)
{
	return 0;
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

