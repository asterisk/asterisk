/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2003, Jefferson Noxon
 *
 * Mark Spencer <markster@digium.com>
 * Jefferson Noxon <jeff@debian.org>
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
 * \brief Database access functions
 *
 * \ingroup applications
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/options.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"

static char *tdesc = "Database Access Functions";

static char *d_descrip =
"  DBdel(family/key): This applicaiton will delete a key from the Asterisk\n"
"database.\n";

static char *dt_descrip =
"  DBdeltree(family[/keytree]): This application will delete a family or keytree\n"
"from the Asterisk database\n";

static char *d_app = "DBdel";
static char *dt_app = "DBdeltree";

static char *d_synopsis = "Delete a key from the database";
static char *dt_synopsis = "Delete a family or keytree from the database";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int deltree_exec(struct ast_channel *chan, void *data)
{
	char *argv, *family, *keytree;
	struct localuser *u;

	LOCAL_USER_ADD(u);

	argv = ast_strdupa(data);
	if (!argv) {
		ast_log(LOG_ERROR, "Memory allocation failed\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	if (strchr(argv, '/')) {
		family = strsep(&argv, "/");
		keytree = strsep(&argv, "\0");
			if (!family || !keytree) {
				ast_log(LOG_DEBUG, "Ignoring; Syntax error in argument\n");
				LOCAL_USER_REMOVE(u);
				return 0;
			}
		if (ast_strlen_zero(keytree))
			keytree = 0;
	} else {
		family = argv;
		keytree = 0;
	}

	if (option_verbose > 2)	{
		if (keytree)
			ast_verbose(VERBOSE_PREFIX_3 "DBdeltree: family=%s, keytree=%s\n", family, keytree);
		else
			ast_verbose(VERBOSE_PREFIX_3 "DBdeltree: family=%s\n", family);
	}

	if (ast_db_deltree(family, keytree)) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "DBdeltree: Error deleting key from database.\n");
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int del_exec(struct ast_channel *chan, void *data)
{
	char *argv, *family, *key;
	struct localuser *u;

	LOCAL_USER_ADD(u);

	argv = ast_strdupa(data);
	if (!argv) {
		ast_log (LOG_ERROR, "Memory allocation failed\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	if (strchr(argv, '/')) {
		family = strsep(&argv, "/");
		key = strsep(&argv, "\0");
		if (!family || !key) {
			ast_log(LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			LOCAL_USER_REMOVE(u);
			return 0;
		}
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "DBdel: family=%s, key=%s\n", family, key);
		if (ast_db_del(family, key)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "DBdel: Error deleting key from database.\n");
		}
	} else {
		ast_log(LOG_DEBUG, "Ignoring, no parameters\n");
	}

	LOCAL_USER_REMOVE(u);
	
	return 0;
}

int unload_module(void)
{
	int retval;

	retval = ast_unregister_application(dt_app);
	retval |= ast_unregister_application(d_app);

	STANDARD_HANGUP_LOCALUSERS;

	return retval;
}

int load_module(void)
{
	int retval;

	retval |= ast_register_application(d_app, del_exec, d_synopsis, d_descrip);
	retval |= ast_register_application(dt_app, deltree_exec, dt_synopsis, dt_descrip);
	
	return retval;
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
