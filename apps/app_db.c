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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/lock.h"

/*** DOCUMENTATION
	<application name="DBdeltree" language="en_US">
		<synopsis>
			Delete a family or keytree from the asterisk database.
		</synopsis>
		<syntax argsep="/">
			<parameter name="family" required="true" />
			<parameter name="keytree" />
		</syntax>
		<description>
			<para>This application will delete a <replaceable>family</replaceable> or <replaceable>keytree</replaceable>
			from the Asterisk database.</para>
		</description>
		<see-also>
			<ref type="function">DB_DELETE</ref>
			<ref type="function">DB</ref>
		</see-also>
	</application>
 ***/

static const char dt_app[] = "DBdeltree";

static int deltree_exec(struct ast_channel *chan, const char *data)
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

	if (keytree) {
		ast_verb(3, "DBdeltree: family=%s, keytree=%s\n", family, keytree);
	} else {
		ast_verb(3, "DBdeltree: family=%s\n", family);
	}

	if (ast_db_deltree(family, keytree) < 0) {
		ast_verb(3, "DBdeltree: Error deleting key from database.\n");
	}

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(dt_app);
}

static int load_module(void)
{
	return ast_register_application_xml(dt_app, deltree_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Database Access Functions");
