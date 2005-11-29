/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Database access functions
 *
 * Copyright (C) 1999, Mark Spencer
 * Copyright (C) 2003, Jefferson Noxon
 *
 * Mark Spencer <markster@linux-support.net>
 * Jefferson Noxon <jeff@debian.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/options.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/astdb.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Database access functions for Asterisk extension logic";

static char *g_descrip =
	"  DBget(varname=family/key): Retrieves a value from the Asterisk\n"
	"database and stores it in the given variable.  Always returns 0.  If the\n"
	"requested key is not found, jumps to priority n+101 if available.\n";

static char *p_descrip =
	"  DBput(family/key=value): Stores the given value in the Asterisk\n"
	"database.  Always returns 0.\n";

static char *d_descrip =
	"  DBdel(family/key): Deletes a key from the Asterisk database.  Always\n"
	"returns 0.\n";

static char *dt_descrip =
	"  DBdeltree(family[/keytree]): Deletes a family or keytree from the Asterisk\n"
	"database.  Always returns 0.\n";

static char *g_app = "DBget";
static char *p_app = "DBput";
static char *d_app = "DBdel";
static char *dt_app = "DBdeltree";

static char *g_synopsis = "Retrieve a value from the database";
static char *p_synopsis = "Store a value in the database";
static char *d_synopsis = "Delete a key from the database";
static char *dt_synopsis = "Delete a family or keytree from the database";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int deltree_exec (struct ast_channel *chan, void *data)
{
	int arglen;
	char *argv, *family, *keytree;

	arglen = strlen (data);
	argv = alloca (arglen + 1);
	if (!argv) {	/* Why would this fail? */
		ast_log (LOG_DEBUG, "Memory allocation failed\n");
		return 0;
	}
	memcpy (argv, data, arglen + 1);

	if (strchr (argv, '/')) {
		family = strsep (&argv, "/");
		keytree = strsep (&argv, "\0");
			if (!family || !keytree) {
				ast_log (LOG_DEBUG, "Ignoring; Syntax error in argument\n");
				return 0;
			}
		if (!strlen (keytree))
			keytree = 0;
	} else {
		family = argv;
		keytree = 0;
	}

	if (option_verbose > 2)	{
		if (keytree)
			ast_verbose (VERBOSE_PREFIX_3 "DBdeltree: family=%s, keytree=%s\n", family, keytree);
		else
			ast_verbose (VERBOSE_PREFIX_3 "DBdeltree: family=%s\n", family);
	}

	if (ast_db_deltree (family, keytree)) {
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "DBdeltree: Error deleting key from database.\n");
	}

	return 0;
}

static int del_exec (struct ast_channel *chan, void *data)
{
	int arglen;
	char *argv, *family, *key;

	arglen = strlen (data);
	argv = alloca (arglen + 1);
	if (!argv) {	/* Why would this fail? */
		ast_log (LOG_DEBUG, "Memory allocation failed\n");
		return 0;
	}
	memcpy (argv, data, arglen + 1);

	if (strchr (argv, '/')) {
		family = strsep (&argv, "/");
		key = strsep (&argv, "\0");
		if (!family || !key) {
			ast_log (LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			return 0;
		}
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "DBdel: family=%s, key=%s\n", family, key);
		if (ast_db_del (family, key)) {
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "DBdel: Error deleting key from database.\n");
		}
	} else {
		ast_log (LOG_DEBUG, "Ignoring, no parameters\n");
	}
	return 0;
}

static int put_exec (struct ast_channel *chan, void *data)
{
	int arglen;
	char *argv, *value, *family, *key;

	arglen = strlen (data);
	argv = alloca (arglen + 1);
	if (!argv) {	/* Why would this fail? */
		ast_log (LOG_DEBUG, "Memory allocation failed\n");
		return 0;
	}
	memcpy (argv, data, arglen + 1);

	if (strchr (argv, '/') && strchr (argv, '=')) {
		family = strsep (&argv, "/");
		key = strsep (&argv, "=");
		value = strsep (&argv, "\0");
		if (!value || !family || !key) {
			ast_log (LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			return 0;
		}
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "DBput: family=%s, key=%s, value=%s\n", family, key, value);
		if (ast_db_put (family, key, value)) {
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "DBput: Error writing value to database.\n");
		}

	} else	{
		ast_log (LOG_DEBUG, "Ignoring, no parameters\n");
	}
	return 0;
}

static int get_exec (struct ast_channel *chan, void *data)
{
	int arglen;
	char *argv, *varname, *family, *key;
	char dbresult[256];

	arglen = strlen (data);
	argv = alloca (arglen + 1);
	if (!argv) {	/* Why would this fail? */
		ast_log (LOG_DEBUG, "Memory allocation failed\n");
		return 0;
	}
	memcpy (argv, data, arglen + 1);

	if (strchr (argv, '=') && strchr (argv, '/')) {
		varname = strsep (&argv, "=");
		family = strsep (&argv, "/");
		key = strsep (&argv, "\0");
		if (!varname || !family || !key) {
			ast_log (LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			return 0;
		}
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "DBget: varname=%s, family=%s, key=%s\n", varname, family, key);
		if (!ast_db_get (family, key, dbresult, sizeof (dbresult) - 1)) {
			pbx_builtin_setvar_helper (chan, varname, dbresult);
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "DBget: set variable %s to %s\n", varname, dbresult);
		} else {
			if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "DBget: Value not found in database.\n");
			/* Send the call to n+101 priority, where n is the current priority */
			if (ast_exists_extension (chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
				chan->priority += 100;
		}

	} else {
		ast_log (LOG_DEBUG, "Ignoring, no parameters\n");
	}
	return 0;
}

int unload_module (void)
{
	int retval;

	STANDARD_HANGUP_LOCALUSERS;
	retval = ast_unregister_application (dt_app);
	retval |= ast_unregister_application (d_app);
	retval |= ast_unregister_application (p_app);
	retval |= ast_unregister_application (g_app);

	return retval;
}

int load_module (void)
{
	int retval;

	retval = ast_register_application (g_app, get_exec, g_synopsis, g_descrip);
	if (!retval)
		retval = ast_register_application (p_app, put_exec, p_synopsis, p_descrip);
	if (!retval)
		retval = ast_register_application (d_app, del_exec, d_synopsis, d_descrip);
	if (!retval)
		retval = ast_register_application (dt_app, deltree_exec, dt_synopsis, dt_descrip);
	return retval;
}

char *description (void)
{
	return tdesc;
}

int usecount (void)
{
	int res;
	STANDARD_USECOUNT (res);
	return res;
}

char *key ()
{
	return ASTERISK_GPL_KEY;
}
