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

#define MAX_DB_FIELD 256

static DB *astdb;
AST_MUTEX_DEFINE_STATIC(dblock);
static ast_cond_t dbcond;
typedef int (*process_keys_cb)(DBT *key, DBT *value, const char *filter, void *data);

static void db_sync(void);

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

static const char *dbt_data2str(DBT *dbt)
{
	char *data = "";

	if (dbt->size) {
		data = dbt->data;
		data[dbt->size - 1] = '\0';
	}

	return data;
}

static inline const char *dbt_data2str_full(DBT *dbt, const char *def)
{
	return S_OR(dbt_data2str(dbt), def);
}

static int process_db_keys(process_keys_cb cb, void *data, const char *filter, int sync)
{
	DBT key = { 0, }, value = { 0, }, last_key = { 0, };
	int counter = 0;
	int res, last = 0;
	char last_key_s[MAX_DB_FIELD];

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}

	/* Somehow, the database can become corrupted such that astdb->seq will continue looping through
	 * the database indefinitely. The pointer to last_key.data ends up getting re-used by the BDB lib
	 * so this specifically queries for the last entry, makes a copy of the key, and then uses it as
	 * a sentinel to avoid repeatedly looping over the list. */

	if (astdb->seq(astdb, &last_key, &value, R_LAST)) {
		/* Empty database */
		ast_mutex_unlock(&dblock);
		return 0;
	}

	memcpy(last_key_s, last_key.data, MIN(last_key.size - 1, sizeof(last_key_s)));
	last_key_s[last_key.size - 1] = '\0';
	for (res = astdb->seq(astdb, &key, &value, R_FIRST);
			!res;
			res = astdb->seq(astdb, &key, &value, R_NEXT)) {
		/* The callback might delete the key, so we have to check it before calling */
		last = !strcmp(dbt_data2str_full(&key, "<bad key>"), last_key_s);
		counter += cb(&key, &value, filter, data);
		if (last) {
			break;
		}
	}

	if (sync) {
		db_sync();
	}

	ast_mutex_unlock(&dblock);

	return counter;
}

static int db_deltree_cb(DBT *key, DBT *value, const char *filter, void *data)
{
	int res = 0;

	if (keymatch(dbt_data2str_full(key, "<bad key>"), filter)) {
		astdb->del(astdb, key, 0);
		res = 1;
	}
	return res;
}

int ast_db_deltree(const char *family, const char *keytree)
{
	char prefix[MAX_DB_FIELD];

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

	return process_db_keys(db_deltree_cb, NULL, prefix, 1);
}

int ast_db_put(const char *family, const char *keys, const char *value)
{
	char fullkey[MAX_DB_FIELD];
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
	db_sync();
	ast_mutex_unlock(&dblock);
	if (res)
		ast_log(LOG_WARNING, "Unable to put value '%s' for key '%s' in family '%s'\n", value, keys, family);

	return res;
}

int ast_db_get(const char *family, const char *keys, char *value, int valuelen)
{
	char fullkey[MAX_DB_FIELD] = "";
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
	char fullkey[MAX_DB_FIELD];
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
	db_sync();
	
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
	char tmp[MAX_DB_FIELD];

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

static int db_show_cb(DBT *key, DBT *value, const char *filter, void *data)
{
	struct ast_cli_args *a = data;
	const char *key_s = dbt_data2str_full(key, "<bad key>");
	const char *value_s = dbt_data2str_full(value, "<bad value>");

	if (keymatch(key_s, filter)) {
		ast_cli(a->fd, "%-50s: %-25s\n", key_s, value_s);
		return 1;
	}

	return 0;
}

static char *handle_cli_database_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char prefix[MAX_DB_FIELD];
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

	if((counter = process_db_keys(db_show_cb, a, prefix, 0)) < 0) {
		ast_cli(a->fd, "Database unavailable\n");
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "%d results found.\n", counter);
	return CLI_SUCCESS;
}

static int db_showkey_cb(DBT *key, DBT *value, const char *filter, void *data)
{
	struct ast_cli_args *a = data;
	const char *key_s = dbt_data2str_full(key, "<bad key>");
	const char *value_s = dbt_data2str_full(value, "<bad value>");

	if (subkeymatch(key_s, filter)) {
		ast_cli(a->fd, "%-50s: %-25s\n", key_s, value_s);
		return 1;
	}

	return 0;
}

static char *handle_cli_database_showkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char suffix[MAX_DB_FIELD];
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

	if ((counter = process_db_keys(db_showkey_cb, a, suffix, 0)) < 0) {
		ast_cli(a->fd, "Database unavailable\n");
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "%d results found.\n", counter);
	return CLI_SUCCESS;
}

static int db_gettree_cb(DBT *key, DBT *value, const char *filter, void *data)
{
	struct ast_db_entry **ret = data;
	struct ast_db_entry *cur;
	const char *key_s = dbt_data2str_full(key, "<bad key>");
	const char *value_s = dbt_data2str_full(value, "<bad value>");
	size_t key_slen = strlen(key_s) + 1, value_slen = strlen(value_s) + 1;

	if (keymatch(key_s, filter) && (cur = ast_malloc(sizeof(*cur) + key_slen + value_slen))) {
		cur->next = *ret;
		cur->key = cur->data + value_slen;
		strcpy(cur->data, value_s);
		strcpy(cur->key, key_s);
		*ret = cur;
		return 1;
	}

	return 0;
}

struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree)
{
	char prefix[MAX_DB_FIELD];
	struct ast_db_entry *ret = NULL;

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

	if (process_db_keys(db_gettree_cb, &ret, prefix, 0) < 0) {
		ast_log(LOG_WARNING, "Database unavailable\n");
		return NULL;
	}

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
	char tmp[MAX_DB_FIELD];
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
		astman_append(s, "Event: DBGetComplete\r\n"
				"%s"
				"\r\n",
				idText);
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

/*!
 * \internal
 * \brief Signal the astdb sync thread to do its thing.
 *
 * \note dblock is assumed to be held when calling this function.
 */
static void db_sync(void)
{
	ast_cond_signal(&dbcond);
}

/*!
 * \internal
 * \brief astdb sync thread
 *
 * This thread is in charge of syncing astdb to disk after a change.
 * By pushing it off to this thread to take care of, this I/O bound operation
 * will not block other threads from performing other critical processing.
 * If changes happen rapidly, this thread will also ensure that the sync
 * operations are rate limited.
 */
static void *db_sync_thread(void *data)
{
	ast_mutex_lock(&dblock);
	for (;;) {
		ast_cond_wait(&dbcond, &dblock);
		ast_mutex_unlock(&dblock);
		sleep(1);
		ast_mutex_lock(&dblock);
		astdb->sync(astdb, 0);
	}

	return NULL;
}

int astdb_init(void)
{
	pthread_t dont_care;

	ast_cond_init(&dbcond, NULL);
	if (ast_pthread_create_background(&dont_care, NULL, db_sync_thread, NULL)) {
		return -1;
	}

	dbinit();
	ast_cli_register_multiple(cli_database, ARRAY_LEN(cli_database));
	ast_manager_register_xml("DBGet", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_dbget);
	ast_manager_register_xml("DBPut", EVENT_FLAG_SYSTEM, manager_dbput);
	ast_manager_register_xml("DBDel", EVENT_FLAG_SYSTEM, manager_dbdel);
	ast_manager_register_xml("DBDelTree", EVENT_FLAG_SYSTEM, manager_dbdeltree);
	return 0;
}
