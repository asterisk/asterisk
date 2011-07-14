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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/md5.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/utils.h"
#include "asterisk/astdb.h"


/* Loopback switch creates a 'tunnel' to another context.  When extension
   lookups pass through the 'tunnel', Asterisk expressions can be used
   to modify the target extension, context, and priority in any way desired.
   If there is a match at the far end, execution jumps through the 'tunnel'
   to the matched context, extension, and priority.
 
   Global variables as well as ${CONTEXT}, ${EXTEN}, and ${PRIORITY} are 
   available for substitution.  After substitution Loopback expects to get
   a string of the form:

	[exten]@context[:priority][/extramatch]
   
   Where exten, context, and priority are another extension, context, and priority
   to lookup and "extramatch" is a dialplan extension pattern which the *original*
   number must match.  If exten or priority are empty, the original values are 
   used.

   Note that the search context MUST be a different context from the current
   context or the search will not succeed.  This is intended to reduce the
   likelihood of loops (they're still possible if you try hard, so be careful!)

*/


#define LOOPBACK_COMMON \
	char buf[1024]; \
	int res; \
	char *newexten=(char *)exten, *newcontext=(char *)context; \
	int newpriority=priority; \
	char *newpattern=NULL; \
	loopback_subst(buf, sizeof(buf), exten, context, priority, data); \
	loopback_parse(&newexten, &newcontext, &newpriority, &newpattern, buf); \
	ast_log(LOG_DEBUG, "Parsed into %s @ %s priority %d\n", newexten, newcontext, newpriority); \
	if (!strcasecmp(newcontext, context)) return -1

static char *loopback_subst(char *buf, int buflen, const char *exten, const char *context, int priority, const char *data)
{
	struct ast_var_t *newvariable;
	struct varshead headp;
	char tmp[80];

	snprintf(tmp, sizeof(tmp), "%d", priority);
	AST_LIST_HEAD_INIT_NOLOCK(&headp);
	newvariable = ast_var_assign("EXTEN", exten);
	AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
	newvariable = ast_var_assign("CONTEXT", context);
	AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
	newvariable = ast_var_assign("PRIORITY", tmp);
	AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
	/* Substitute variables */
	pbx_substitute_variables_varshead(&headp, data, buf, buflen);
	/* free the list */
	while ((newvariable = AST_LIST_REMOVE_HEAD(&headp, entries)))
                ast_var_delete(newvariable);
	return buf;
}

static void loopback_parse(char **newexten, char **newcontext, int *priority, char **newpattern, char *buf)
{
	char *con;
	char *pri;
	*newpattern = strchr(buf, '/');
	if (*newpattern)
		*(*newpattern)++ = '\0';
	con = strchr(buf, '@');
	if (con) {
		*con++ = '\0';
		pri = strchr(con, ':');
	} else
		pri = strchr(buf, ':');
	if (!ast_strlen_zero(buf))
		*newexten = buf;
	if (!ast_strlen_zero(con))
		*newcontext = con;
	if (!ast_strlen_zero(pri))
		sscanf(pri, "%30d", priority);
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

static int loopback_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int found;
	LOOPBACK_COMMON;
	res = ast_spawn_extension(chan, newcontext, newexten, newpriority, callerid, &found, 0);
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
        description:   		"Loopback Dialplan Switch",
        exists:                 loopback_exists,
        canmatch:               loopback_canmatch,
        exec:                   loopback_exec,
        matchmore:              loopback_matchmore,
};

static int unload_module(void)
{
	ast_unregister_switch(&loopback_switch);
	return 0;
}

static int load_module(void)
{
	if (ast_register_switch(&loopback_switch))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Loopback Switch");
