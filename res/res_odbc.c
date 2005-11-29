/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * res_odbc.c <ODBC resource manager>
 * Copyright (C) 2004 Anthony Minessale II <anthmct@yahoo.com>
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/cli.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asterisk/res_odbc.h>
#define MAX_ODBC_HANDLES 25

struct odbc_list
{
	char name[80];
	odbc_obj *obj;
	int used;
};

static struct odbc_list ODBC_REGISTRY[MAX_ODBC_HANDLES];

static void odbc_destroy(void)
{
	int x = 0;

	for (x = 0; x < MAX_ODBC_HANDLES; x++) {
		if (ODBC_REGISTRY[x].obj) {
			destroy_obdc_obj(&ODBC_REGISTRY[x].obj);
			ODBC_REGISTRY[x].obj = NULL;
		}
	}
}

static odbc_obj *odbc_read(struct odbc_list *registry, char *name)
{
	int x = 0;
	for (x = 0; x < MAX_ODBC_HANDLES; x++) {
		if (registry[x].used && !strcmp(registry[x].name, name)) {
			return registry[x].obj;
		}
	}
	return NULL;
}

static int odbc_write(struct odbc_list *registry, char *name, odbc_obj * obj)
{
	int x = 0;
	for (x = 0; x < MAX_ODBC_HANDLES; x++) {
		if (!registry[x].used) {
			strncpy(registry[x].name, name, sizeof(registry[x].name) - 1);
			registry[x].obj = obj;
			registry[x].used = 1;
			return 1;
		}
	}
	return 0;
}

static void odbc_init(void)
{
	int x = 0;
	for (x = 0; x < MAX_ODBC_HANDLES; x++) {
		memset(&ODBC_REGISTRY[x], 0, sizeof(struct odbc_list));
	}
}

static char *tdesc = "ODBC Resource";
/* internal stuff */

static int load_odbc_config(void)
{
	static char *cfg = "res_odbc.conf";
	struct ast_config *config;
	struct ast_variable *v;
	char *cat, *dsn, *username, *password;
	int enabled;
	int connect = 0;
	char *env_var;

	odbc_obj *obj;

	config = ast_load(cfg);
	if (config) {
		for (cat = ast_category_browse(config, NULL); cat; cat=ast_category_browse(config, cat)) {
			if (!strcmp(cat, "ENV")) {
				for (v = ast_variable_browse(config, cat); v; v = v->next) {
					env_var = malloc(strlen(v->name) + strlen(v->value) + 2);
					sprintf(env_var, "%s=%s", v->name, v->value);
					ast_log(LOG_NOTICE, "Adding ENV var: %s=%s\n", v->name, v->value);
					putenv(env_var);
					free(env_var);
				}

			cat = ast_category_browse(config, cat);
			}

			dsn = username = password = NULL;
			enabled = 1;
			connect = 0;
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcmp(v->name, "enabled"))
					enabled = ast_true(v->value);
				if (!strcmp(v->name, "pre-connect"))
					connect = ast_true(v->value);
				if (!strcmp(v->name, "dsn"))
					dsn = v->value;
				if (!strcmp(v->name, "username"))
					username = v->value;
				if (!strcmp(v->name, "password"))
					password = v->value;
			}

			if (enabled && dsn && username && password) {
				obj = new_odbc_obj(cat, dsn, username, password);
				if (obj) {
					register_odbc_obj(cat, obj);
					ast_log(LOG_NOTICE, "registered database handle '%s' dsn->[%s]\n", cat, obj->dsn);
					if (connect) {
						odbc_obj_connect(obj);
					}
				} else {
					ast_log(LOG_WARNING, "Addition of obj %s failed.\n", cat);
				}

			}
		}
		ast_destroy(config);
	}
	return 0;
}

int odbc_dump_fd(int fd, odbc_obj * obj)
{
	ast_cli(fd, "\n\nName: %s\nDSN: %s\nConnected: %s\n\n", obj->name, obj->dsn, obj->up ? "yes" : "no");
	return 0;
}

static int odbc_usage(int fd)
{
	ast_cli(fd, "\n\nusage odbc <command> <arg1> .. <argn>\n\n");
	return 0;
}

static int odbc_command(int fd, int argc, char **argv)
{
	odbc_obj *obj;
	int x = 0;
	if (!argv[1])
		return odbc_usage(fd);

	ast_cli(fd, "\n\n");

	if (!strcmp(argv[1], "connect") || !strcmp(argv[1], "disconnect")) {
		if (!argv[2])
			return odbc_usage(fd);

		obj = odbc_read(ODBC_REGISTRY, argv[2]);
		if (obj) {
			if (!strcmp(argv[1], "connect"))
				odbc_obj_connect(obj);

			if (!strcmp(argv[1], "disconnect"))
				odbc_obj_disconnect(obj);
		}

	} else if (!strcmp(argv[1], "show")) {
		if (!argv[2] || (argv[2] && !strcmp(argv[2], "all"))) {
			for (x = 0; x < MAX_ODBC_HANDLES; x++) {
				if (!ODBC_REGISTRY[x].used)
					break;
				if (ODBC_REGISTRY[x].obj)
					odbc_dump_fd(fd, ODBC_REGISTRY[x].obj);
			}
		} else {
			obj = odbc_read(ODBC_REGISTRY, argv[2]);
			if (obj)
				odbc_dump_fd(fd, obj);
		}

	} else {
		return odbc_usage(fd);
	}
	ast_cli(fd, "\n");
	return 0;
}

static struct ast_cli_entry odbc_command_struct = {
	{"odbc", NULL}, odbc_command,
	"Execute ODBC Command", "obdc <command> <arg1> .. <argn>", NULL
};

/* api calls */

int register_odbc_obj(char *name, odbc_obj * obj)
{
	if (obj != NULL)
		return odbc_write(ODBC_REGISTRY, name, obj);
	return 0;
}

odbc_obj *fetch_odbc_obj(char *name)
{
	return (odbc_obj *) odbc_read(ODBC_REGISTRY, name);
}

odbc_obj *new_odbc_obj(char *name, char *dsn, char *username, char *password)
{
	static odbc_obj *new;

	new = malloc(sizeof(odbc_obj));
	memset(new, 0, sizeof(odbc_obj));
	new->env = SQL_NULL_HANDLE;

	new->name = malloc(strlen(name) + 1);
	if (new->name == NULL)
		return NULL;

	new->dsn = malloc(strlen(dsn) + 1);
	if (new->dsn == NULL)
		return NULL;

	new->username = malloc(strlen(username) + 1);
	if (new->username == NULL)
		return NULL;

	new->password = malloc(strlen(password) + 1);
	if (new->password == NULL)
		return NULL;

	strcpy(new->name, name);
	strcpy(new->dsn, dsn);
	strcpy(new->username, username);
	strcpy(new->password, password);
	new->up = 0;
	ast_mutex_init(&new->lock);
	return new;
}

void destroy_obdc_obj(odbc_obj ** obj)
{
	odbc_obj_disconnect(*obj);

	ast_mutex_lock(&(*obj)->lock);
	SQLFreeHandle(SQL_HANDLE_STMT, (*obj)->stmt);
	SQLFreeHandle(SQL_HANDLE_DBC, (*obj)->con);
	SQLFreeHandle(SQL_HANDLE_ENV, (*obj)->env);

	free((*obj)->name);
	free((*obj)->dsn);
	free((*obj)->username);
	free((*obj)->password);
	ast_mutex_unlock(&(*obj)->lock);
	free(*obj);
}

odbc_status odbc_obj_disconnect(odbc_obj * obj)
{
	int res;
	ast_mutex_lock(&obj->lock);

	if (obj->up) {
		res = SQLDisconnect(obj->con);
	} else {
		res = -1;
	}

	if (res == ODBC_SUCCESS) {
		ast_log(LOG_WARNING, "res_odbc: disconnected %d from %s [%s]\n", res, obj->name, obj->dsn);
		obj->up = 0;
	} else {
		ast_log(LOG_WARNING, "res_odbc: %s [%s] already disconnected\n",
		obj->name, obj->dsn);
	}
	ast_mutex_unlock(&obj->lock);
	return ODBC_SUCCESS;
}

odbc_status odbc_obj_connect(odbc_obj * obj)
{
	int res;
	long int err;
	short int mlen;
	char msg[200], stat[10];

	ast_mutex_lock(&obj->lock);

	if (obj->env == SQL_NULL_HANDLE) {
		res = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &obj->env);

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 3)
				ast_log(LOG_WARNING, "res_odbc: Error AllocHandle\n");
			ast_mutex_unlock(&obj->lock);
			return ODBC_FAIL;
		}

		res = SQLSetEnvAttr(obj->env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			if (option_verbose > 3)
				ast_log(LOG_WARNING, "res_odbc: Error SetEnv\n");
			SQLFreeHandle(SQL_HANDLE_ENV, obj->env);
			ast_mutex_unlock(&obj->lock);
			return ODBC_FAIL;
		}

		res = SQLAllocHandle(SQL_HANDLE_DBC, obj->env, &obj->con);

		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {

			if (option_verbose > 3)
				ast_log(LOG_WARNING, "res_odbc: Error AllocHDB %d\n", res);
			SQLFreeHandle(SQL_HANDLE_ENV, obj->env);

			ast_mutex_unlock(&obj->lock);
			return ODBC_FAIL;
		}
		SQLSetConnectAttr(obj->con, SQL_LOGIN_TIMEOUT, (SQLPOINTER *) 10, 0);
	}

	res = SQLConnect(obj->con,
		   (SQLCHAR *) obj->dsn, SQL_NTS,
		   (SQLCHAR *) obj->username, SQL_NTS,
		   (SQLCHAR *) obj->password, SQL_NTS);

	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		SQLGetDiagRec(SQL_HANDLE_DBC, obj->con, 1, stat, &err, msg, 100, &mlen);
		SQLFreeHandle(SQL_HANDLE_ENV, obj->env);
		if (option_verbose > 3)
			ast_log(LOG_WARNING, "res_odbc: Error SQLConnect=%d errno=%ld %s\n", res, err, msg);
		return ODBC_FAIL;
	} else {

		if (option_verbose > 3)
			ast_log(LOG_NOTICE, "res_odbc: Connected to %s [%s]\n", obj->name, obj->dsn);
		obj->up = 1;
	}

	ast_mutex_unlock(&obj->lock);
	return ODBC_SUCCESS;
}

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	odbc_destroy();
	ast_cli_unregister(&odbc_command_struct);
	ast_log(LOG_NOTICE, "res_odbc unloaded.\n");
	return 0;
}

int load_module(void)
{
	odbc_init();
	load_odbc_config();
	ast_cli_register(&odbc_command_struct);
	ast_log(LOG_NOTICE, "res_odbc loaded.\n");
	return 0;
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
