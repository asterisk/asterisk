/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MD5 checksum application
 * 
 * Copyright (C) 2005, Olle E. Johansson, Edvina.net
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static char *tdesc_md5 = "MD5 checksum application";
static char *app_md5 = "MD5";
static char *synopsis_md5 = 
"  MD5(<var>=<string>): Calculates a MD5 checksum on <string>.\n"
"Returns hash value in a channel variable. Always return 0\n";

static char *tdesc_md5check = "MD5 checksum verification application";
static char *app_md5check = "MD5Check";
static char *synopsis_md5check = 
"  MD5Check(<md5hash>,<string>): Calculates a MD5 checksum on <string>\n"
"and compares it with the hash. Returns 0 if <md5hash> is correct for <string>.\n"
"Jumps to priority+101 if incorrect.\n";

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

	if (!data) {
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
	char *hash= NULL; /* Hash to compare with */
	char *string = NULL; /* String to calculate on */
	char newhash[50]; /* Return value */

	if (!data) {
		ast_log(LOG_WARNING, "Syntax: MD5Check(<md5hash>,<string>) - missing argument!\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	memset(newhash,0, sizeof(newhash));

	string = ast_strdupa(data);
	hash = strsep(&string,"|");
	if (ast_strlen_zero(hash)) {
		ast_log(LOG_WARNING, "Syntax: MD5Check(<md5hash>,<string>) - missing argument!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	ast_md5_hash(newhash, string);
	if (!strcmp(newhash, hash)) {	/* Verification ok */
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "MD5 verified ok: %s -- %s\n", hash, string);
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "ERROR: MD5 not verified: %s -- %s\n", hash, string);
	if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
		chan->priority += 100;
	else if (option_debug > 2)
		ast_log(LOG_DEBUG, "ERROR: Can't jump to exten+101 (e%s,p%d), sorry\n", chan->exten,chan->priority+101);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;

	STANDARD_HANGUP_LOCALUSERS;
	res =ast_unregister_application(app_md5);
	res |= ast_unregister_application(app_md5check);
	return res;
}

int load_module(void)
{
	int res;

	res = ast_register_application(app_md5check, md5check_exec, synopsis_md5check, tdesc_md5check);
	res |= ast_register_application(app_md5, md5_exec, synopsis_md5, tdesc_md5);
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
