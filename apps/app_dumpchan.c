/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Application to dump channel variables
 * 
 * Copyright (C) 2004, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License (and disclaimed to Digium)
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/utils.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static char *tdesc = "Dump Info About The Calling Channel";
static char *app = "DumpChan";
static char *synopsis = "Dump Info About The Calling Channel";
static char *desc = 
"   DumpChan([<min_verbose_level>])\n"
"Displays information on channel and listing of all channel\n"
"variables. If min_verbose_level is specified, output is only\n"
"displayed when the verbose level is currently set to that number\n"
"or greater. Always returns 0.\n\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int ast_serialize_showchan(struct ast_channel *c, char *buf, size_t size)
{
	struct timeval now;
	long elapsed_seconds=0;
	int hour=0, min=0, sec=0;
	char cgrp[256];
	char pgrp[256];
	
	gettimeofday(&now, NULL);
	memset(buf,0,size);
	if (!c)
		return 0;

	if (c->cdr) {
		elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
	}

	snprintf(buf,size, 
			 "Name=               %s\n"
			 "Type=               %s\n"
			 "UniqueID=           %s\n"
			 "CallerID=           %s\n"
			 "CallerIDName=       %s\n"
			 "DNIDDigits=         %s\n"
			 "State=              %s (%d)\n"
			 "Rings=              %d\n"
			 "NativeFormat=       %d\n"
			 "WriteFormat=        %d\n"
			 "ReadFormat=         %d\n"
			 "1stFileDescriptor=  %d\n"
			 "Framesin=           %d %s\n"
			 "Framesout=          %d %s\n"
			 "TimetoHangup=       %ld\n"
			 "ElapsedTime=        %dh%dm%ds\n"
			 "Context=            %s\n"
			 "Extension=          %s\n"
			 "Priority=           %d\n"
			 "CallGroup=          %s\n"
			 "PickupGroup=        %s\n"
			 "Application=        %s\n"
			 "Data=               %s\n"
			 "Blocking_in=        %s\n",
			 c->name,
			 c->type,
			 c->uniqueid,
			 (c->cid.cid_num ? c->cid.cid_num : "(N/A)"),
			 (c->cid.cid_name ? c->cid.cid_name : "(N/A)"),
			 (c->cid.cid_dnid ? c->cid.cid_dnid : "(N/A)" ),
			 ast_state2str(c->_state),
			 c->_state,
			 c->rings,
			 c->nativeformats,
			 c->writeformat,
			 c->readformat,
			 c->fds[0], c->fin & 0x7fffffff, (c->fin & 0x80000000) ? " (DEBUGGED)" : "",
			 c->fout & 0x7fffffff, (c->fout & 0x80000000) ? " (DEBUGGED)" : "", (long)c->whentohangup,
			 hour,
			 min,
			 sec,
			 c->context,
			 c->exten,
			 c->priority,
			 ast_print_group(cgrp, sizeof(cgrp), c->callgroup),
			 ast_print_group(pgrp, sizeof(pgrp), c->pickupgroup),
			 ( c->appl ? c->appl : "(N/A)" ),
			 ( c-> data ? (!ast_strlen_zero(c->data) ? c->data : "(Empty)") : "(None)"),
			 (ast_test_flag(c, AST_FLAG_BLOCKING) ? c->blockproc : "(Not Blocking)"));

	return 0;
}

static int dumpchan_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char vars[1024];
	char info[1024];
	int level = 0;
	static char *line = "================================================================================";
	LOCAL_USER_ADD(u);

	if (data) {
		level = atoi(data);
	}

	pbx_builtin_serialize_variables(chan, vars, sizeof(vars));
	ast_serialize_showchan(chan, info, sizeof(info));
	if (option_verbose >= level)
		ast_verbose("\nDumping Info For Channel: %s:\n%s\nInfo:\n%s\nVariables:\n%s%s\n",chan->name, line, info, vars, line);

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, dumpchan_exec, synopsis, desc);
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

