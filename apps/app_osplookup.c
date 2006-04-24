/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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

/*!
 * \file
 * \brief Open Settlement Protocol Applications
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>libosptk</depend>
	<depend>ssl</depend>
 ***/

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

static char *app1= "OSPAuth";
static char *synopsis1 = "OSP authentication";
static char *descrip1 = 
"  OSPAuth([provider[|options]]):  Authenticate a SIP INVITE by OSP and sets\n"
"the variables:\n"
" ${OSPINHANDLE}:  The in_bound call transaction handle\n"
" ${OSPINTIMELIMIT}:  The in_bound call duration limit in seconds\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the authentication was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPAUTHSTATUS	The status of the OSP Auth attempt as a text string, one of\n"
"		SUCCESS | FAILED | ERROR\n";

static char *app2= "OSPLookup";
static char *synopsis2 = "Lookup destination by OSP";
static char *descrip2 = 
"  OSPLookup(exten[|provider[|options]]):  Looks up an extension via OSP and sets\n"
"the variables, where 'n' is the number of the result beginning with 1:\n"
" ${OSPOUTHANDLE}:  The OSP Handle for anything remaining\n"
" ${OSPTECH}:  The technology to use for the call\n"
" ${OSPDEST}:  The destination to use for the call\n"
" ${OSPCALLING}:  The calling number to use for the call\n"
" ${OSPOUTTOKEN}:  The actual OSP token as a string\n"
" ${OSPOUTTIMELIMIT}:  The out_bound call duration limit in seconds\n"
" ${OSPRESULTS}:  The number of OSP results total remaining\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the lookup was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPLOOKUPSTATUS	The status of the OSP Lookup attempt as a text string, one of\n"
"		SUCCESS | FAILED | ERROR\n";

static char *app3 = "OSPNext";
static char *synopsis3 = "Lookup next destination by OSP";
static char *descrip3 = 
"  OSPNext(cause[|options]):  Looks up the next OSP Destination for ${OSPOUTHANDLE}\n"
"See OSPLookup for more information\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the lookup was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPNEXTSTATUS	The status of the OSP Next attempt as a text string, one of\n"
"		SUCCESS | FAILED |ERROR\n";

static char *app4 = "OSPFinish";
static char *synopsis4 = "Record OSP entry";
static char *descrip4 = 
"  OSPFinish([status[|options]]):  Records call state for ${OSPINHANDLE}, according to\n"
"status, which should be one of BUSY, CONGESTION, ANSWER, NOANSWER, or CHANUNAVAIL\n"
"or coincidentally, just what the Dial application stores in its ${DIALSTATUS}.\n"
"\n"
"The option string may contain the following character:\n"
"	'j' -- jump to n+101 priority if the finish attempt was NOT successful\n"
"This application sets the following channel variable upon completion:\n"
"	OSPFINISHSTATUS	The status of the OSP Finish attempt as a text string, one of\n"
"		SUCCESS | FAILED |ERROR \n";

LOCAL_USER_DECL;

static int ospauth_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser* u;
	char* provider = OSP_DEF_PROVIDER;
	int priority_jump = 0;
	struct varshead* headp;
	struct ast_var_t* current;
	const char* source = "";
	const char* token = "";
	int handle;
	unsigned int timelimit;
	char* tmp;
	char buffer[OSP_INTSTR_SIZE];
	char* status;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);

	LOCAL_USER_ADD(u);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return(-1);
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_log(LOG_DEBUG, "OSPAuth: provider '%s'\n", provider);

	if (args.options) {
		if (strchr(args.options, 'j')) {
			priority_jump = 1;
		}
	}
	ast_log(LOG_DEBUG, "OSPAuth: priority jump '%d'\n", priority_jump);

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPPEERIP")) {
			source = ast_var_value(current);
		} else if (!strcasecmp(ast_var_name(current), "OSPINTOKEN")) {
			token = ast_var_value(current);
		}
	}
	ast_log(LOG_DEBUG, "OSPAuth: source '%s'\n", source);
	ast_log(LOG_DEBUG, "OSPAuth: token size '%d'\n", strlen(token));

	res = ast_osp_auth(provider, &handle, source, chan->cid.cid_num, chan->exten, token, &timelimit);
	if (res > 0) {
		status = OSP_APP_SUCCESS;
	} else {
		timelimit = OSP_DEF_TIMELIMIT;
		if (!res) {
			status = OSP_APP_FAILED;
		} else {
			handle = OSP_INVALID_HANDLE;
			status = OSP_APP_ERROR;
		}
	}

	snprintf(buffer, sizeof(buffer), "%d", handle);
	pbx_builtin_setvar_helper(chan, "OSPINHANDLE", buffer);
	ast_log(LOG_DEBUG, "OSPAuth: OSPINHANDLE '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", timelimit);
	pbx_builtin_setvar_helper(chan, "OSPINTIMELIMIT", buffer);
	ast_log(LOG_DEBUG, "OSPAuth: OSPINTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPAUTHSTATUS", status);
	ast_log(LOG_DEBUG, "OSPAuth: %s\n", status);

	if(!res) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		} else {
			res = -1;
		}
	} else if (res > 0) {
		res = 0;
	}

	LOCAL_USER_REMOVE(u);

	return(res);
}

static int osplookup_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser* u;
	char* provider = OSP_DEF_PROVIDER;
	int priority_jump = 0;
	struct varshead* headp;
	struct ast_var_t* current;
	const char* srcdev = "";
	char* tmp;
	char buffer[OSP_TOKSTR_SIZE];
	struct ast_osp_result result;
	char* status;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(exten);
		AST_APP_ARG(provider);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPLookup: Arg required, OSPLookup(exten[|provider[|options]])\n");
		return(-1);
	}

	LOCAL_USER_ADD(u);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return(-1);
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	ast_log(LOG_DEBUG, "OSPLookup: exten '%s'\n", args.exten);

	if (!ast_strlen_zero(args.provider)) {
		provider = args.provider;
	}
	ast_log(LOG_DEBUG, "OSPlookup: provider '%s'\n", provider);

	if (args.options) {
		if (strchr(args.options, 'j')) {
			priority_jump = 1;
		}
	}
	ast_log(LOG_DEBUG, "OSPLookup: priority jump '%d'\n", priority_jump);

	result.inhandle = OSP_INVALID_HANDLE;

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%d", &result.inhandle) != 1) {
				result.inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPINTIMELIMIT")) {
			if (sscanf(ast_var_value(current), "%d", &result.intimelimit) != 1) {
				result.intimelimit = OSP_DEF_TIMELIMIT;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPPEERIP")) {
			srcdev = ast_var_value(current);
		}
	}
	ast_log(LOG_DEBUG, "OSPLookup: OSPINHANDLE '%d'\n", result.inhandle);
	ast_log(LOG_DEBUG, "OSPLookup: OSPINTIMELIMIT '%d'\n", result.intimelimit);
	ast_log(LOG_DEBUG, "OSPLookup: source device '%s'\n", srcdev);

	res = ast_osp_lookup(provider, srcdev, chan->cid.cid_num, args.exten, &result);
	if (res > 0) {
		status = OSP_APP_SUCCESS;
	} else {
		result.tech[0] = '\0';
		result.dest[0] = '\0';
		result.calling[0] = '\0';
		result.token[0] = '\0'; 
		result.numresults = 0;
		result.outtimelimit = OSP_DEF_TIMELIMIT;
		if (!res) {
			status = OSP_APP_FAILED;
		} else {
			result.outhandle = OSP_INVALID_HANDLE;
			status = OSP_APP_ERROR;
		}
	}

	snprintf(buffer, sizeof(buffer), "%d", result.outhandle);
	pbx_builtin_setvar_helper(chan, "OSPOUTHANDLE", buffer);
	ast_log(LOG_DEBUG, "OSPLookup: OSPOUTHANDLE '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPTECH", result.tech);
	ast_log(LOG_DEBUG, "OSPLookup: OSPTECH '%s'\n", result.tech);
	pbx_builtin_setvar_helper(chan, "OSPDEST", result.dest);
	ast_log(LOG_DEBUG, "OSPLookup: OSPDEST '%s'\n", result.dest);
	pbx_builtin_setvar_helper(chan, "OSPCALLING", result.calling);
	ast_log(LOG_DEBUG, "OSPLookup: OSPCALLING '%s'\n", result.calling);
	pbx_builtin_setvar_helper(chan, "OSPOUTTOKEN", result.token);
	ast_log(LOG_DEBUG, "OSPLookup: OSPOUTTOKEN size '%d'\n", strlen(result.token));
	if (!ast_strlen_zero(result.token)) {
		snprintf(buffer, sizeof(buffer), "P-OSP-Auth-Token: %s", result.token);
		pbx_builtin_setvar_helper(chan, "_SIPADDHEADER", buffer);
		ast_log(LOG_DEBUG, "OSPLookup: SIPADDHEADER size '%d'\n", strlen(buffer));
	}
	snprintf(buffer, sizeof(buffer), "%d", result.numresults);
	pbx_builtin_setvar_helper(chan, "OSPRESULTS", buffer);
	ast_log(LOG_DEBUG, "OSPLookup: OSPRESULTS '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", result.outtimelimit);
	pbx_builtin_setvar_helper(chan, "OSPOUTTIMELIMIT", buffer);
	ast_log(LOG_DEBUG, "OSPLookup: OSPOUTTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPLOOKUPSTATUS", status);
	ast_log(LOG_DEBUG, "OSPLookup: %s\n", status);

	if(!res) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		} else {
			res = -1;
		}
	} else if (res > 0) {
		res = 0;
	}

	LOCAL_USER_REMOVE(u);

	return(res);
}

static int str2cause(char *str)
{
	int cause = AST_CAUSE_NORMAL;

	if (ast_strlen_zero(str)) {
		cause = AST_CAUSE_NOTDEFINED;
	} else if (!strcasecmp(str, "BUSY")) {
		cause = AST_CAUSE_BUSY;
	} else if (!strcasecmp(str, "CONGESTION")) {
		cause = AST_CAUSE_CONGESTION;
	} else if (!strcasecmp(str, "ANSWER")) {
		cause = AST_CAUSE_NORMAL;
	} else if (!strcasecmp(str, "CANCEL")) {
		cause = AST_CAUSE_NORMAL;
	} else if (!strcasecmp(str, "NOANSWER")) {
		cause = AST_CAUSE_NOANSWER;
	} else if (!strcasecmp(str, "NOCHANAVAIL")) {
		cause = AST_CAUSE_CONGESTION;
	} else {
		ast_log(LOG_WARNING, "OSP: Unknown cause '%s', using NORMAL\n", str);
	}

	return(cause);
}

static int ospnext_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	int priority_jump = 0;
	int cause;
	struct varshead* headp;
	struct ast_var_t* current;
	struct ast_osp_result result;
	char *tmp;
	char buffer[OSP_TOKSTR_SIZE];
	char* status;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cause);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPNext: Arg required, OSPNext(cause[|options])\n");
		return(-1);
	}

	LOCAL_USER_ADD(u);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return(-1);
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	cause = str2cause(args.cause);
	ast_log(LOG_DEBUG, "OSPNext: cause '%d'\n", cause);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}
	ast_log(LOG_DEBUG, "OSPNext: priority jump '%d'\n", priority_jump);

	result.inhandle = OSP_INVALID_HANDLE;
	result.outhandle = OSP_INVALID_HANDLE;
	result.numresults = 0;

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%d", &result.inhandle) != 1) {
				result.inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPOUTHANDLE")) {
			if (sscanf(ast_var_value(current), "%d", &result.outhandle) != 1) {
				result.outhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPINTIMEOUT")) {
			if (sscanf(ast_var_value(current), "%d", &result.intimelimit) != 1) {
				result.intimelimit = OSP_DEF_TIMELIMIT;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPRESULTS")) {
			if (sscanf(ast_var_value(current), "%d", &result.numresults) != 1) {
				result.numresults = 0;
			}
		}
	}
	ast_log(LOG_DEBUG, "OSPNext: OSPINHANDLE '%d'\n", result.inhandle);
	ast_log(LOG_DEBUG, "OSPNext: OSPOUTHANDLE '%d'\n", result.outhandle);
	ast_log(LOG_DEBUG, "OSPNext: OSPINTIMELIMIT '%d'\n", result.intimelimit);
	ast_log(LOG_DEBUG, "OSPNext: OSPRESULTS '%d'\n", result.numresults);

	if ((res = ast_osp_next(cause, &result)) > 0) {
		status = OSP_APP_SUCCESS;
	} else {
		result.tech[0] = '\0';
		result.dest[0] = '\0';
		result.calling[0] = '\0';
		result.token[0] = '\0'; 
		result.numresults = 0;
		result.outtimelimit = OSP_DEF_TIMELIMIT;
		if (!res) {
			status = OSP_APP_FAILED;
		} else {
			result.outhandle = OSP_INVALID_HANDLE;
			status = OSP_APP_ERROR;
		}
	}

	pbx_builtin_setvar_helper(chan, "OSPTECH", result.tech);
	ast_log(LOG_DEBUG, "OSPNext: OSPTECH '%s'\n", result.tech);
	pbx_builtin_setvar_helper(chan, "OSPDEST", result.dest);
	ast_log(LOG_DEBUG, "OSPNext: OSPDEST '%s'\n", result.dest);
	pbx_builtin_setvar_helper(chan, "OSPCALLING", result.calling);
	ast_log(LOG_DEBUG, "OSPNext: OSPCALLING '%s'\n", result.calling);
	pbx_builtin_setvar_helper(chan, "OSPOUTTOKEN", result.token);
	ast_log(LOG_DEBUG, "OSPNext: OSPOUTTOKEN size '%d'\n", strlen(result.token));
	if (!ast_strlen_zero(result.token)) {
		snprintf(buffer, sizeof(buffer), "P-OSP-Auth-Token: %s", result.token);
		pbx_builtin_setvar_helper(chan, "_SIPADDHEADER", buffer);
		ast_log(LOG_DEBUG, "OSPNext: SIPADDHEADER size '%d'\n", strlen(buffer));
	}
	snprintf(buffer, sizeof(buffer), "%d", result.numresults);
	pbx_builtin_setvar_helper(chan, "OSPRESULTS", buffer);
	ast_log(LOG_DEBUG, "OSPNext: OSPRESULTS '%s'\n", buffer);
	snprintf(buffer, sizeof(buffer), "%d", result.outtimelimit);
	pbx_builtin_setvar_helper(chan, "OSPOUTTIMELIMIT", buffer);
	ast_log(LOG_DEBUG, "OSPNext: OSPOUTTIMELIMIT '%s'\n", buffer);
	pbx_builtin_setvar_helper(chan, "OSPNEXTSTATUS", status);
	ast_log(LOG_DEBUG, "OSPNext: %s\n", status);

	if(!res) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		} else {
			res = -1;
		}
	} else if (res > 0) {
		res = 0;
	}

	LOCAL_USER_REMOVE(u);

	return(res);
}

static int ospfinished_exec(struct ast_channel *chan, void *data)
{
	int res = 1;
	struct localuser* u;
	int priority_jump = 0;
	int cause;
	struct varshead* headp;
	struct ast_var_t* current;
	int inhandle = OSP_INVALID_HANDLE;
	int outhandle = OSP_INVALID_HANDLE;
	int recorded = 0;
	time_t start, connect, end;
	char* tmp;
	char* str = "";
	char buffer[OSP_INTSTR_SIZE];
	char* status;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(status);
		AST_APP_ARG(options);
	);
	
	LOCAL_USER_ADD(u);

	if (!(tmp = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return(-1);
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}
	ast_log(LOG_DEBUG, "OSPFinish: priority jump '%d'\n", priority_jump);

	headp = &chan->varshead;
	AST_LIST_TRAVERSE(headp, current, entries) {
		if (!strcasecmp(ast_var_name(current), "OSPINHANDLE")) {
			if (sscanf(ast_var_value(current), "%d", &inhandle) != 1) {
				inhandle = OSP_INVALID_HANDLE;
			}
		} else if (!strcasecmp(ast_var_name(current), "OSPOUTHANDLE")) {
			if (sscanf(ast_var_value(current), "%d", &outhandle) != 1) {
				outhandle = OSP_INVALID_HANDLE;
			}
		} else if (!recorded &&
			(!strcasecmp(ast_var_name(current), "OSPAUTHSTATUS") ||
			!strcasecmp(ast_var_name(current), "OSPLOOKUPSTATUS") || 
			!strcasecmp(ast_var_name(current), "OSPNEXTSTATUS"))) 
		{
			if (strcasecmp(ast_var_value(current), OSP_APP_SUCCESS)) {
				recorded = 1;
			}
		}
	}
	ast_log(LOG_DEBUG, "OSPFinish: OSPINHANDLE '%d'\n", inhandle);
	ast_log(LOG_DEBUG, "OSPFinish: OSPOUTHANDLE '%d'\n", outhandle);
	ast_log(LOG_DEBUG, "OSPFinish: recorded '%d'\n", recorded);

	if (!recorded) {
		str = args.status;
	}
	cause = str2cause(str);
	ast_log(LOG_DEBUG, "OSPFinish: cause '%d'\n", cause);

	if (chan->cdr) {
		start = chan->cdr->start.tv_sec;
		connect = chan->cdr->answer.tv_sec;
		if (connect) {
			end = time(NULL);
		} else {
			end = connect;
		}
	} else {
		start = 0;
		connect = 0;
		end = 0;
	}
	ast_log(LOG_DEBUG, "OSPFinish: start '%ld'\n", start);
	ast_log(LOG_DEBUG, "OSPFinish: connect '%ld'\n", connect);
	ast_log(LOG_DEBUG, "OSPFinish: end '%ld'\n", end);

	if (ast_osp_finish(outhandle, cause, start, connect, end) <= 0) {
		ast_log(LOG_DEBUG, "OSPFinish: Unable to report usage for out_bound call\n");
	}
	if (ast_osp_finish(inhandle, cause, start, connect, end) <= 0) {
		ast_log(LOG_DEBUG, "OSPFinish: Unable to report usage for in_bound call\n");
	}
	snprintf(buffer, sizeof(buffer), "%d", OSP_INVALID_HANDLE);
	pbx_builtin_setvar_helper(chan, "OSPOUTHANDLE", buffer);
	pbx_builtin_setvar_helper(chan, "OSPINHANDLE", buffer);

	if (res > 0) {
		status = OSP_APP_SUCCESS;
	} else if (!res) {
		status = OSP_APP_FAILED;
	} else {
		status = OSP_APP_ERROR;
	}
	pbx_builtin_setvar_helper(chan, "OSPFINISHSTATUS", status);

	if(!res) {
		if (priority_jump || ast_opt_priority_jumping) {
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		} else {
			res = -1;
		}
	} else if (res > 0) {
		res = 0;
	}

	LOCAL_USER_REMOVE(u);

	return(res);
}

static int load_module(void *mod)
{
	int res;
	
	ast_osp_adduse();

	res = ast_register_application(app1, ospauth_exec, synopsis1, descrip1);
	res |= ast_register_application(app2, osplookup_exec, synopsis2, descrip2);
	res |= ast_register_application(app3, ospnext_exec, synopsis3, descrip3);
	res |= ast_register_application(app4, ospfinished_exec, synopsis4, descrip4);

	return(res);
}

static int unload_module(void *mod)
{
	int res;
	
	res = ast_unregister_application(app4);
	res |= ast_unregister_application(app3);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app1);

	STANDARD_HANGUP_LOCALUSERS;

	ast_osp_deluse();

	return(res);
}

static const char *description(void)
{
	return "Open Settlement Protocol Applications";
}

static const char *key(void)
{
	return(ASTERISK_GPL_KEY);
}

STD_MOD1;


