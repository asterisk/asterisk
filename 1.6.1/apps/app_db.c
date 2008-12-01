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
 * \author Mark Spencer <markster@digium.com>
 * \author Jefferson Noxon <jeff@debian.org>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/lock.h"

/*! \todo XXX Remove this application after 1.4 is relased */
static char *d_descrip =
"  DBdel(family/key): This application will delete a key from the Asterisk\n"
"database.\n"
"  This application has been DEPRECATED in favor of the DB_DELETE function.\n";

static char *dt_descrip =
"  DBdeltree(family[/keytree]): This application will delete a family or keytree\n"
"from the Asterisk database\n";

static char *d_app = "DBdel";
static char *dt_app = "DBdeltree";

static char *d_synopsis = "Delete a key from the database";
static char *dt_synopsis = "Delete a family or keytree from the database";


static int deltree_exec(struct ast_channel *chan, void *data)
{
	char *argv, *family, *keytree;

	argv = ast_strdupa(data);

	if (strchr(argv, '/')) {
		family = strsep(&argv, "/");
		keytree = strsep(&argv, "\0");
		if (!family || !keytree) {
			ast_debug(1, "Ignoring; Syntax error in argument\n");
			return 0;
		}
		if (ast_strlen_zero(keytree))
			keytree = 0;
	} else {
		family = argv;
		keytree = 0;
	}

	if (keytree)
		ast_verb(3, "DBdeltree: family=%s, keytree=%s\n", family, keytree);
	else
		ast_verb(3, "DBdeltree: family=%s\n", family);

	if (ast_db_deltree(family, keytree))
		ast_verb(3, "DBdeltree: Error deleting key from database.\n");

	return 0;
}

static int del_exec(struct ast_channel *chan, void *data)
{
	char *argv, *family, *key;
	static int deprecation_warning = 0;

	if (!deprecation_warning) {
		deprecation_warning = 1;
		ast_log(LOG_WARNING, "The DBdel application has been deprecated in favor of the DB_DELETE dialplan function!\n");
	}

	argv = ast_strdupa(data);

	if (strchr(argv, '/')) {
		family = strsep(&argv, "/");
		key = strsep(&argv, "\0");
		if (!family || !key) {
			ast_debug(1, "Ignoring; Syntax error in argument\n");
			return 0;
		}
		ast_verb(3, "DBdel: family=%s, key=%s\n", family, key);
		if (ast_db_del(family, key))
			ast_verb(3, "DBdel: Error deleting key from database.\n");
	} else {
		ast_debug(1, "Ignoring, no parameters\n");
	}

	return 0;
}

static int unload_module(void)
{
	int retval;

	retval = ast_unregister_application(dt_app);
	retval |= ast_unregister_application(d_app);

	return retval;
}

static int load_module(void)
{
	int retval;

	retval = ast_register_application(d_app, del_exec, d_synopsis, d_descrip);
	retval |= ast_register_application(dt_app, deltree_exec, dt_synopsis, dt_descrip);

	return retval;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Database Access Functions");
