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
 * \brief Loopback PBX Module
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/md5.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/utils.h"
#include "asterisk/crypto.h"
#include "asterisk/astdb.h"

static char *tdesc = "Loopback Switch";

/* Loopback switch substitutes ${EXTEN}, ${CONTEXT}, and ${PRIORITY} into
   the data passed to it to try to get a string of the form:

	[exten]@context[:priority][/extramatch]
   
   Where exten, context, and priority are another extension, context, and priority
   to lookup and "extramatch" is an extra match restriction the *original* number 
   must fit if  specified.  The "extramatch" begins with _ like an exten pattern
   if it is specified.  Note that the search context MUST be a different context
   from the current context or the search will not succeed in an effort to reduce
   the likelihood of loops (they're still possible if you try hard, so be careful!)

*/


#define LOOPBACK_COMMON \
	char buf[1024]; \
	int res; \
	char *newexten=(char *)exten, *newcontext=(char *)context; \
	int newpriority=priority; \
	char *newpattern=NULL; \
	loopback_helper(buf, sizeof(buf), exten, context, priority, data); \
	loopback_subst(&newexten, &newcontext, &newpriority, &newpattern, buf); \
	ast_log(LOG_DEBUG, "Parsed into %s @ %s priority %d\n", newexten, newcontext, newpriority); \
	if (!strcasecmp(newcontext, context)) return -1


static char *loopback_helper(char *buf, int buflen, const char *exten, const char *context, int priority, const char *data)
{
	struct ast_var_t *newvariable;
	struct varshead headp;
	char tmp[80];

	snprintf(tmp, sizeof(tmp), "%d", priority);
	memset(buf, 0, buflen);
	AST_LIST_HEAD_INIT_NOLOCK(&headp);
	newvariable = ast_var_assign("EXTEN", exten);
	AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
	newvariable = ast_var_assign("CONTEXT", context);
	AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
	newvariable = ast_var_assign("PRIORITY", tmp);
	AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
	pbx_substitute_variables_varshead(&headp, data, buf, buflen);
	/* Substitute variables */
	while (!AST_LIST_EMPTY(&headp)) {           /* List Deletion. */
		newvariable = AST_LIST_REMOVE_HEAD(&headp, entries);
		ast_var_delete(newvariable);
	}
	return buf;
}

static void loopback_subst(char **newexten, char **newcontext, int *priority, char **newpattern, char *buf)
{
	char *con;
	char *pri;
	*newpattern = strchr(buf, '/');
	if (*newpattern) {
		*(*newpattern) = '\0';
		(*newpattern)++;
	}
	con = strchr(buf, '@');
	if (con) {
		*con = '\0';
		con++;
		pri = strchr(con, ':');
	} else
		pri = strchr(buf, ':');
	if (!ast_strlen_zero(buf))
		*newexten = buf;
	if (con && !ast_strlen_zero(con))
		*newcontext = con;
	if (pri && !ast_strlen_zero(pri))
		sscanf(pri, "%d", priority);
}

static int loopback_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	LOOPBACK_COMMON;
	res = ast_exists_extension(chan, newcontext, newexten, newpriority, callerid);
	if (newpattern && !ast_extension_match(newpattern, exten))
		res = 0;
	return res;
}

static int loopback_canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	LOOPBACK_COMMON;
	res = ast_canmatch_extension(chan, newcontext, newexten, newpriority, callerid);
	if (newpattern && !ast_extension_match(newpattern, exten))
		res = 0;
	return res;
}

static int loopback_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, int newstack, const char *data)
{
	LOOPBACK_COMMON;
	if (newstack)
		res = ast_spawn_extension(chan, newcontext, newexten, newpriority, callerid);
	else
		res = ast_exec_extension(chan, newcontext, newexten, newpriority, callerid);
	if (newpattern && !ast_extension_match(newpattern, exten))
		res = -1;
	return res;
}

static int loopback_matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	LOOPBACK_COMMON;
	res = ast_matchmore_extension(chan, newcontext, newexten, newpriority, callerid);
	if (newpattern && !ast_extension_match(newpattern, exten))
		res = 0;
	return res;
}

static struct ast_switch loopback_switch =
{
        name:                   "Loopback",
        description:    		"Loopback Dialplan Switch",
        exists:                 loopback_exists,
        canmatch:               loopback_canmatch,
        exec:                   loopback_exec,
        matchmore:              loopback_matchmore,
};

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 1;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

int unload_module(void)
{
	ast_unregister_switch(&loopback_switch);
	return 0;
}

int load_module(void)
{
	ast_register_switch(&loopback_switch);
	return 0;
}

