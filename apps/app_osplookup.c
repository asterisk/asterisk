/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Open Settlement Protocol Lookup
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

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
"If the lookup was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

static char *descrip2 = 
"  OSPNext:  Looks up the next OSP Destination for ${OSPHANDLE}\n"
"See OSPLookup for more information\n"
"\n"
"If the lookup was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

static char *descrip3 = 
"  OSPFinish(status):  Records call state for ${OSPHANDLE}, according to\n"
"status, which should be one of BUSY, CONGESTION, ANSWER, NOANSWER, or NOCHANAVAIL\n"
"or coincidentally, just what the Dial application stores in its ${DIALSTATUS}\n"
"\n"
"If the finishing was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

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
	char *provider, *opts=NULL;
	struct ast_osp_result result;
	if (!data || ast_strlen_zero(data) || !(temp = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "OSPLookup requires an argument (extension)\n");
		return -1;
	}
	provider = strchr(temp, '|');
	if (provider) {
		*provider = '\0';
		provider++;
		opts = strchr(provider, '|');
		if (opts) {
			*opts = '\0';
			opts++;
		}
	}
	LOCAL_USER_ADD(u);
	ast_log(LOG_DEBUG, "Whoo hoo, looking up OSP on '%s' via '%s'\n", temp, provider ? provider : "<default>");
	if ((res = ast_osp_lookup(chan, provider, temp, chan->cid.cid_num, &result)) > 0) {
		char tmp[80];
		snprintf(tmp, sizeof(tmp), "%d", result.handle);
		pbx_builtin_setvar_helper(chan, "_OSPHANDLE", tmp);
		pbx_builtin_setvar_helper(chan, "_OSPTECH", result.tech);
		pbx_builtin_setvar_helper(chan, "_OSPDEST", result.dest);
		pbx_builtin_setvar_helper(chan, "_OSPTOKEN", result.token);
		snprintf(tmp, sizeof(tmp), "%d", result.numresults);
		pbx_builtin_setvar_helper(chan, "_OSPRESULTS", tmp);

	} else {
		if (!res)
			ast_log(LOG_NOTICE, "OSP Lookup failed for '%s' (provider '%s')\n", temp, provider ? provider : "<default>");
		else
			ast_log(LOG_DEBUG, "Got hangup on '%s' while doing OSP Lookup for '%s' (provider '%s')!\n", chan->name, temp, provider ? provider : "<default>" );
	}
	if (!res) {
		/* Look for a "busy" place */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
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
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPNext should have an argument (cause)\n");
	}
	LOCAL_USER_ADD(u);
	cause = str2cause((char *)data);
	temp = pbx_builtin_getvar_helper(chan, "OSPHANDLE");
	result.handle = -1;
	if (temp && strlen(temp) && (sscanf(temp, "%d", &result.handle) == 1) && (result.handle > -1)) {
		if ((res = ast_osp_next(&result, cause)) > 0) {
			char tmp[80];
			snprintf(tmp, sizeof(tmp), "%d", result.handle);
			pbx_builtin_setvar_helper(chan, "_OSPHANDLE", tmp);
			pbx_builtin_setvar_helper(chan, "_OSPTECH", result.tech);
			pbx_builtin_setvar_helper(chan, "_OSPDEST", result.dest);
			pbx_builtin_setvar_helper(chan, "_OSPTOKEN", result.token);
			snprintf(tmp, sizeof(tmp), "%d", result.numresults);
			pbx_builtin_setvar_helper(chan, "_OSPRESULTS", tmp);
		}
	} else {
		if (!res) {
			if (result.handle < 0)
				ast_log(LOG_NOTICE, "OSP Lookup Next failed for handle '%d'\n", result.handle);
			else
				ast_log(LOG_DEBUG, "No OSP handle specified\n");
		} else
			ast_log(LOG_DEBUG, "Got hangup on '%s' while doing OSP Next!\n", chan->name);
	}
	if (!res) {
		/* Look for a "busy" place */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
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
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "OSPFinish should have an argument (cause)\n");
	}
	if (chan->cdr) {
		start = chan->cdr->answer.tv_sec;
		if (start)
			duration = time(NULL) - start;
		else
			duration = 0;
	} else
		ast_log(LOG_WARNING, "OSPFinish called on channel '%s' with no CDR!\n", chan->name);
	LOCAL_USER_ADD(u);
	cause = str2cause((char *)data);
	temp = pbx_builtin_getvar_helper(chan, "OSPHANDLE");
	result.handle = -1;
	if (temp && strlen(temp) && (sscanf(temp, "%d", &result.handle) == 1) && (result.handle > -1)) {
		if (!ast_osp_terminate(result.handle, cause, start, duration)) {
			pbx_builtin_setvar_helper(chan, "_OSPHANDLE", "");
			res = 1;
		}
	} else {
		if (!res) {
			if (result.handle > -1)
				ast_log(LOG_NOTICE, "OSP Finish failed for handle '%d'\n", result.handle);
			else
				ast_log(LOG_DEBUG, "No OSP handle specified\n");
		} else
			ast_log(LOG_DEBUG, "Got hangup on '%s' while doing OSP Terminate!\n", chan->name);
	}
	if (!res) {
		/* Look for a "busy" place */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}


int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app3);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app);
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, osplookup_exec, synopsis, descrip);
	if (res)
		return(res);
	res = ast_register_application(app2, ospnext_exec, synopsis2, descrip2);
	if (res)
		return(res);
	res = ast_register_application(app3, ospfinished_exec, synopsis3, descrip3);
	if (res)
		return(res);
	return(0);
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

