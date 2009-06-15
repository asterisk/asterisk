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
 * \brief ASTdb Management
 *
 * \author Mark Spencer <markster@digium.com> 
 *
 * \note DB3 is licensed under Sleepycat Public License and is thus incompatible
 * with GPL.  To avoid having to make another exception (and complicate 
 * licensing even further) we elect to use DB1 which is BSD licensed 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_DB */
#include <sys/time.h>
#include <signal.h>
#include <dirent.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/astdb.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/manager.h"
#include "db1-ast/include/db.h"

/*** DOCUMENTATION
	<manager name="DBGet" language="en_US">
		<synopsis>
			Get DB Entry.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBPut" language="en_US">
		<synopsis>
			Put DB entry.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" required="true" />
			<parameter name="Val" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBDel" language="en_US">
		<synopsis>
			Delete DB entry.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBDelTree" language="en_US">
		<synopsis>
			Delete DB Tree.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" />
		</syntax>
		<description>
		</description>
	</manager>
 ***/

static DB *astdb;
AST_MUTEX_DEFINE_STATIC(dblock);

static int dbinit(void) 
{
	if (!astdb && !(astdb = dbopen(ast_config_AST_DB, O_CREAT | O_RDWR, AST_FILE_MODE, DB_BTREE, NULL))) {
		ast_log(LOG_WARNING, "Unable to open Asterisk database '%s': %s\n", ast_config_AST_DB, strerror(errno));
		return -1;
	}
	return 0;
}


static inline int keymatch(const char *key, const char *prefix)
{
	int preflen = strlen(prefix);
	if (!preflen)
		return 1;
	if (!strcasecmp(key, prefix))
		return 1;
	if ((strlen(key) > preflen) && !strncasecmp(key, prefix, preflen)) {
		if (key[preflen] == '/')
			return 1;
	}
	return 0;
}

static inline int subkeymatch(const char *key, const char *suffix)
{
	int suffixlen = strlen(suffix);
	if (suffixlen) {
		const char *subkey = key + strlen(key) - suffixlen;
		if (subkey < key)
			return 0;
		if (!strcasecmp(subkey, suffix))
			return 1;
	}
	return 0;
}

int ast_db_deltree(const char *family, const char *keytree)
{
	char prefix[256];
	DBT key, data;
	char *keys;
	int res;
	int pass;
	int counter = 0;
	
	if (family) {
		if (keytree) {
			snprintf(prefix, sizeof(prefix), "/%s/%s", family, keytree);
		} else {
			snprintf(prefix, sizeof(prefix), "/%s", family);
		}
	} else if (keytree) {
		return -1;
	} else {
		prefix[0] = '\0';
	}
	
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}
	
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (keymatch(keys, prefix)) {
			astdb->del(astdb, &key, 0);
			counter++;
		}
	}
	astdb->sync(astdb, 0);
	ast_mutex_unlock(&dblock);
	return counter;
}

int ast_db_put(const char *family, const char *keys, const char *value)
{
	char fullkey[256];
	DBT key, data;
	int res, fullkeylen;

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}

	fullkeylen = snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, keys);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data = fullkey;
	key.size = fullkeylen + 1;
	data.data = (char *) value;
	data.size = strlen(value) + 1;
	res = astdb->put(astdb, &key, &data, 0);
	astdb->sync(astdb, 0);
	ast_mutex_unlock(&dblock);
	if (res)
		ast_log(LOG_WARNING, "Unable to put value '%s' for key '%s' in family '%s'\n", value, keys, family);
	return res;
}

int ast_db_get(const char *family, const char *keys, char *value, int valuelen)
{
	char fullkey[256] = "";
	DBT key, data;
	int res, fullkeylen;

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}

	fullkeylen = snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, keys);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	memset(value, 0, valuelen);
	key.data = fullkey;
	key.size = fullkeylen + 1;

	res = astdb->get(astdb, &key, &data, 0);

	/* Be sure to NULL terminate our data either way */
	if (res) {
		ast_debug(1, "Unable to find key '%s' in family '%s'\n", keys, family);
	} else {
#if 0
		printf("Got value of size %d\n", data.size);
#endif
		if (data.size) {
			((char *)data.data)[data.size - 1] = '\0';
			/* Make sure that we don't write too much to the dst pointer or we don't read too much from the source pointer */
			ast_copy_string(value, data.data, (valuelen > data.size) ? data.size : valuelen);
		} else {
			ast_log(LOG_NOTICE, "Strange, empty value for /%s/%s\n", family, keys);
		}
	}

	/* Data is not fully isolated for concurrency, so the lock must be extended
	 * to after the copy to the output buffer. */
	ast_mutex_unlock(&dblock);

	return res;
}

int ast_db_del(const char *family, const char *keys)
{
	char fullkey[256];
	DBT key;
	int res, fullkeylen;

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}
	
	fullkeylen = snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, keys);
	memset(&key, 0, sizeof(key));
	key.data = fullkey;
	key.size = fullkeylen + 1;
	
	res = astdb->del(astdb, &key, 0);
	astdb->sync(astdb, 0);
	
	ast_mutex_unlock(&dblock);

	if (res) {
		ast_debug(1, "Unable to find key '%s' in family '%s'\n", keys, family);
	}
	return res;
}

static char *handle_cli_database_put(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database put";
		e->usage =
			"Usage: database put <family> <key> <value>\n"
			"       Adds or updates an entry in the Asterisk database for\n"
			"       a given family, key, and value.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5)
		return CLI_SHOWUSAGE;
	res = ast_db_put(a->argv[2], a->argv[3], a->argv[4]);
	if (res)  {
		ast_cli(a->fd, "Failed to update entry\n");
	} else {
		ast_cli(a->fd, "Updated database successfully\n");
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_get(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;
	char tmp[256];

	switch (cmd) {
	case CLI_INIT:
		e->command = "database get";
		e->usage =
			"Usage: database get <family> <key>\n"
			"       Retrieves an entry in the Asterisk database for a given\n"
			"       family and key.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	res = ast_db_get(a->argv[2], a->argv[3], tmp, sizeof(tmp));
	if (res) {
		ast_cli(a->fd, "Database entry not found.\n");
	} else {
		ast_cli(a->fd, "Value: %s\n", tmp);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_del(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database del";
		e->usage =
			"Usage: database del <family> <key>\n"
			"       Deletes an entry in the Asterisk database for a given\n"
			"       family and key.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	res = ast_db_del(a->argv[2], a->argv[3]);
	if (res) {
		ast_cli(a->fd, "Database entry does not exist.\n");
	} else {
		ast_cli(a->fd, "Database entry removed.\n");
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_deltree(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database deltree";
		e->usage =
			"Usage: database deltree <family> [keytree]\n"
			"       Deletes a family or specific keytree within a family\n"
			"       in the Asterisk database.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc < 3) || (a->argc > 4))
		return CLI_SHOWUSAGE;
	if (a->argc == 4) {
		res = ast_db_deltree(a->argv[2], a->argv[3]);
	} else {
		res = ast_db_deltree(a->argv[2], NULL);
	}
	if (res < 0) {
		ast_cli(a->fd, "Database entries do not exist.\n");
	} else {
		ast_cli(a->fd, "%d database entries removed.\n",res);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char prefix[256];
	DBT key, data;
	char *keys, *values;
	int res;
	int pass;
	int counter = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database show";
		e->usage =
			"Usage: database show [family [keytree]]\n"
			"       Shows Asterisk database contents, optionally restricted\n"
			"       to a given family, or family and keytree.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 4) {
		/* Family and key tree */
		snprintf(prefix, sizeof(prefix), "/%s/%s", a->argv[2], a->argv[3]);
	} else if (a->argc == 3) {
		/* Family only */
		snprintf(prefix, sizeof(prefix), "/%s", a->argv[2]);
	} else if (a->argc == 2) {
		/* Neither */
		prefix[0] = '\0';
	} else {
		return CLI_SHOWUSAGE;
	}
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		ast_cli(a->fd, "Database unavailable\n");
		return CLI_SUCCESS;	
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (data.size) {
			values = data.data;
			values[data.size - 1]='\0';
		} else {
			values = "<bad value>";
		}
		if (keymatch(keys, prefix)) {
			ast_cli(a->fd, "%-50s: %-25s\n", keys, values);
			counter++;
		}
	}
	ast_mutex_unlock(&dblock);
	ast_cli(a->fd, "%d results found.\n", counter);
	return CLI_SUCCESS;	
}

static char *handle_cli_database_showkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char suffix[256];
	DBT key, data;
	char *keys, *values;
	int res;
	int pass;
	int counter = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database showkey";
		e->usage =
			"Usage: database showkey <keytree>\n"
			"       Shows Asterisk database contents, restricted to a given key.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 3) {
		/* Key only */
		snprintf(suffix, sizeof(suffix), "/%s", a->argv[2]);
	} else {
		return CLI_SHOWUSAGE;
	}
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		ast_cli(a->fd, "Database unavailable\n");
		return CLI_SUCCESS;	
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (data.size) {
			values = data.data;
			values[data.size - 1]='\0';
		} else {
			values = "<bad value>";
		}
		if (subkeymatch(keys, suffix)) {
			ast_cli(a->fd, "%-50s: %-25s\n", keys, values);
			counter++;
		}
	}
	ast_mutex_unlock(&dblock);
	ast_cli(a->fd, "%d results found.\n", counter);
	return CLI_SUCCESS;	
}

struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree)
{
	char prefix[256];
	DBT key, data;
	char *keys, *values;
	int values_len;
	int res;
	int pass;
	struct ast_db_entry *last = NULL;
	struct ast_db_entry *cur, *ret=NULL;

	if (!ast_strlen_zero(family)) {
		if (!ast_strlen_zero(keytree)) {
			/* Family and key tree */
			snprintf(prefix, sizeof(prefix), "/%s/%s", family, keytree);
		} else {
			/* Family only */
			snprintf(prefix, sizeof(prefix), "/%s", family);
		}
	} else {
		prefix[0] = '\0';
	}
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		ast_log(LOG_WARNING, "Database unavailable\n");
		return NULL;	
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (data.size) {
			values = data.data;
			values[data.size - 1] = '\0';
		} else {
			values = "<bad value>";
		}
		values_len = strlen(values) + 1;
		if (keymatch(keys, prefix) && (cur = ast_malloc(sizeof(*cur) + strlen(keys) + 1 + values_len))) {
			cur->next = NULL;
			cur->key = cur->data + values_len;
			strcpy(cur->data, values);
			strcpy(cur->key, keys);
			if (last) {
				last->next = cur;
			} else {
				ret = cur;
			}
			last = cur;
		}
	}
	ast_mutex_unlock(&dblock);
	return ret;	
}

void ast_db_freetree(struct ast_db_entry *dbe)
{
	struct ast_db_entry *last;
	while (dbe) {
		last = dbe;
		dbe = dbe->next;
		ast_free(last);
	}
}

static struct ast_cli_entry cli_database[] = {
	AST_CLI_DEFINE(handle_cli_database_show,    "Shows database contents"),
	AST_CLI_DEFINE(handle_cli_database_showkey, "Shows database contents"),
	AST_CLI_DEFINE(handle_cli_database_get,     "Gets database value"),
	AST_CLI_DEFINE(handle_cli_database_put,     "Adds/updates database value"),
	AST_CLI_DEFINE(handle_cli_database_del,     "Removes database key/value"),
	AST_CLI_DEFINE(handle_cli_database_deltree, "Removes database keytree/values")
};

static int manager_dbput(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	const char *val = astman_get_header(m, "Val");
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified");
		return 0;
	}
	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified");
		return 0;
	}

	res = ast_db_put(family, key, S_OR(val, ""));
	if (res) {
		astman_send_error(s, m, "Failed to update entry");
	} else {
		astman_send_ack(s, m, "Updated database successfully");
	}
	return 0;
}

static int manager_dbget(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	char tmp[256];
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}
	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified.");
		return 0;
	}

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);

	res = ast_db_get(family, key, tmp, sizeof(tmp));
	if (res) {
		astman_send_error(s, m, "Database entry not found");
	} else {
		astman_send_ack(s, m, "Result will follow");
		astman_append(s, "Event: DBGetResponse\r\n"
				"Family: %s\r\n"
				"Key: %s\r\n"
				"Val: %s\r\n"
				"%s"
				"\r\n",
				family, key, tmp, idText);
	}
	return 0;
}

static int manager_dbdel(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}

	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified.");
		return 0;
	}

	res = ast_db_del(family, key);
	if (res)
		astman_send_error(s, m, "Database entry not found");
	else
		astman_send_ack(s, m, "Key deleted successfully");

	return 0;
}

static int manager_dbdeltree(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}

	if (!ast_strlen_zero(key))
		res = ast_db_deltree(family, key);
	else
		res = ast_db_deltree(family, NULL);

	if (res < 0)
		astman_send_error(s, m, "Database entry not found");
	else
		astman_send_ack(s, m, "Key tree deleted successfully");
	
	return 0;
}

int astdb_init(void)
{
	dbinit();
	ast_cli_register_multiple(cli_database, ARRAY_LEN(cli_database));
	ast_manager_register_xml("DBGet", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_dbget);
	ast_manager_register_xml("DBPut", EVENT_FLAG_SYSTEM, manager_dbput);
	ast_manager_register_xml("DBDel", EVENT_FLAG_SYSTEM, manager_dbdel);
	ast_manager_register_xml("DBDelTree", EVENT_FLAG_SYSTEM, manager_dbdeltree);
	return 0;
}
