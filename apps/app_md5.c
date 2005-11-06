/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Olle E. Johansson, Edvina.net
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
 * \brief MD5 checksum application
 * 
 * \todo Remove this deprecated application in 1.3dev
 * \ingroup applications
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"

static char *tdesc_md5 = "MD5 checksum applications";
static char *app_md5 = "MD5";
static char *desc_md5 = "Calculate MD5 checksum";
static char *synopsis_md5 = 
"  MD5(<var>=<string>): Calculates a MD5 checksum on <string>.\n"
"Returns hash value in a channel variable. Always return 0\n";

static char *app_md5check = "MD5Check";
static char *desc_md5check = "Check MD5 checksum";
static char *synopsis_md5check = 
"  MD5Check(<md5hash>|<string>[|options]): Calculates a MD5 checksum on <string>\n"
"and compares it with the hash. Returns 0 if <md5hash> is correct for <string>.\n"
"The option string may contain zero or more of the following characters:\n"
"	'j' -- jump to priority n+101 if the hash and string do not match \n"
"This application sets the following channel variable upon completion:\n"
"	CHECKMD5STATUS	The status of the MD5 check, one of the following\n"
"		MATCH | NOMATCH\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

/*--- md5_exec: Calculate MD5 checksum (hash) on given string and
	return it in channel variable ---*/
static int md5_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *varname= NULL; /* Variable to set */
	char *string = NULL; /* String to calculate on */
	char retvar[50]; /* Return value */
	static int dep_warning = 0;

	if (!dep_warning) {
		ast_log(LOG_WARNING, "This application has been deprecated, please use the MD5 function instead.\n");
		dep_warning = 1;
	}	

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: md5(<varname>=<string>) - missing argument!\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	memset(retvar,0, sizeof(retvar));
	string = ast_strdupa(data);
	varname = strsep(&string,"=");
	if (ast_strlen_zero(varname)) {
		ast_log(LOG_WARNING, "Syntax: md5(<varname>=<string>) - missing argument!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	ast_md5_hash(retvar, string);
	pbx_builtin_setvar_helper(chan, varname, retvar);
	LOCAL_USER_REMOVE(u);
	return res;
}

/*--- md5check_exec: Calculate MD5 checksum and compare it with
	existing checksum. ---*/
static int md5check_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *string = NULL; /* String to calculate on */
	char newhash[50]; /* Return value */
	static int dep_warning = 0;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(md5hash);
		AST_APP_ARG(string);
		AST_APP_ARG(options);
	);

	if (!dep_warning) {
		ast_log(LOG_WARNING, "This application has been deprecated, please use the CHECK_MD5 function instead.\n");
		dep_warning = 1;
	}
	
	LOCAL_USER_ADD(u);

	if (!(string = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, string);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (ast_strlen_zero(args.md5hash) || ast_strlen_zero(args.string)) {
		ast_log(LOG_WARNING, "Syntax: MD5Check(<md5hash>|<string>[|options]) - missing argument!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	memset(newhash,0, sizeof(newhash));

	ast_md5_hash(newhash, args.string);
	if (!strcmp(newhash, args.md5hash)) {	/* Verification ok */
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "MD5 verified ok: %s -- %s\n", args.md5hash, args.string);
		pbx_builtin_setvar_helper(chan, "CHECKMD5STATUS", "MATCH");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "ERROR: MD5 not verified: %s -- %s\n", args.md5hash, args.string);
	pbx_builtin_setvar_helper(chan, "CHECKMD5STATUS", "NOMATCH");		
	if (priority_jump || option_priority_jumping) {
		if (!ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101))
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "ERROR: Can't jump to exten+101 (e%s,p%d), sorry\n", chan->exten,chan->priority+101);
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_md5);
	res |= ast_unregister_application(app_md5check);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_register_application(app_md5check, md5check_exec, desc_md5check, synopsis_md5check);
	res |= ast_register_application(app_md5, md5_exec, desc_md5, synopsis_md5);
	
	return res;
}

char *description(void)
{
	return tdesc_md5;
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
