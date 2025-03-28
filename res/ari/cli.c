/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Command line for ARI.
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/stasis_app.h"
#include "asterisk/uuid.h"
#include "internal.h"
#include "ari_websockets.h"

static char *ari_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ari_conf_general *, general, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show status";
		e->usage =
			"Usage: ari show status\n"
			"       Shows all ARI settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	general = ari_conf_get_general();

	if (!general) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "ARI Status:\n");
	ast_cli(a->fd, "Enabled: %s\n", AST_CLI_YESNO(general->enabled));
	ast_cli(a->fd, "Output format: ");
	if (general->format & AST_JSON_PRETTY) {
		ast_cli(a->fd, "pretty");
	} else {
		ast_cli(a->fd, "compact");
	}
	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "Auth realm: %s\n", general->auth_realm);
	ast_cli(a->fd, "Allowed Origins: %s\n", general->allowed_origins);
	return CLI_SUCCESS;
}

static int show_users_cb(void *obj, void *arg, int flags)
{
	struct ari_conf_user *user = obj;
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, "%-4s  %s\n",
		AST_CLI_YESNO(user->read_only),
		ast_sorcery_object_get_id(user));
	return 0;
}

static char *ari_show_users(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, users, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show users";
		e->usage =
			"Usage: ari show users\n"
			"       Shows all ARI users\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	users = ari_conf_get_users();
	if (!users) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "r/o?  Username\n");
	ast_cli(a->fd, "----  --------\n");

	ao2_callback(users, OBJ_NODATA, show_users_cb, a);

	return CLI_SUCCESS;
}

static void complete_sorcery_object(struct ao2_container *container,
	const char *word)
{
	size_t wordlen = strlen(word);
	void *object;
	struct ao2_iterator i = ao2_iterator_init(container, 0);

	while ((object = ao2_iterator_next(&i))) {
		const char *id = ast_sorcery_object_get_id(object);
		if (!strncasecmp(word, id, wordlen)) {
			ast_cli_completion_add(ast_strdup(id));
		}
		ao2_ref(object, -1);
	}
	ao2_iterator_destroy(&i);
}

static char *ari_show_user(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ari_conf_user *, user, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, users, ari_conf_get_users(), ao2_cleanup);

	if (!users) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show user";
		e->usage =
			"Usage: ari show user <username>\n"
			"       Shows a specific ARI user\n";
		return NULL;
	case CLI_GENERATE:
		complete_sorcery_object(users, a->word);
		return NULL;
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	user = ari_conf_get_user(a->argv[3]);
	if (!user) {
		ast_cli(a->fd, "User '%s' not found\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Username: %s\n", ast_sorcery_object_get_id(user));
	ast_cli(a->fd, "Read only?: %s\n", AST_CLI_YESNO(user->read_only));

	return CLI_SUCCESS;
}

static char *ari_mkpasswd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(char *, crypted, NULL, ast_free);

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari mkpasswd";
		e->usage =
			"Usage: ari mkpasswd <password>\n"
			"       Encrypts a password for use in ari.conf\n"
			"       Be aware that the password will be shown in the\n"
			"       command line history. The mkpasswd shell command\n"
			"       may be preferable.\n"
			;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	crypted = ast_crypt_encrypt(a->argv[2]);
	if (!crypted) {
		ast_cli(a->fd, "Failed to encrypt password\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd,
		"; Copy the following two lines into ari.conf\n");
	ast_cli(a->fd, "password_format = crypt\n");
	ast_cli(a->fd, "password = %s\n", crypted);

	return CLI_SUCCESS;
}

static char *ari_show_apps(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *apps;
	struct ao2_iterator it_apps;
	char *app;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show apps";
		e->usage =
			"Usage: ari show apps\n"
			"       Lists all registered applications.\n"
			;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	apps = stasis_app_get_all();
	if (!apps) {
		ast_cli(a->fd, "Unable to retrieve registered applications!\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Application Name         \n");
	ast_cli(a->fd, "=========================\n");
	it_apps = ao2_iterator_init(apps, 0);
	while ((app = ao2_iterator_next(&it_apps))) {
		ast_cli(a->fd, "%s\n", app);
		ao2_ref(app, -1);
	}

	ao2_iterator_destroy(&it_apps);
	ao2_ref(apps, -1);

	return CLI_SUCCESS;
}

static void complete_app(struct ao2_container *container,
	const char *word)
{
	size_t wordlen = strlen(word);
	void *object;
	struct ao2_iterator i = ao2_iterator_init(container, 0);

	while ((object = ao2_iterator_next(&i))) {
		if (!strncasecmp(word, object, wordlen)) {
			ast_cli_completion_add(ast_strdup(object));
		}
		ao2_ref(object, -1);
	}
	ao2_iterator_destroy(&i);
}

static char *ari_show_app(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	void *app;
	RAII_VAR(struct ao2_container *, apps, stasis_app_get_all(), ao2_cleanup);

	if (!apps) {
		ast_cli(a->fd, "Error getting ARI applications\n");
		return CLI_FAILURE;
	}

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show app";
		e->usage =
			"Usage: ari show app <application>\n"
			"       Provide detailed information about a registered application.\n"
			;
		return NULL;
	case CLI_GENERATE:
		complete_app(apps, a->word);
		return NULL;
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	app = stasis_app_get_by_name(a->argv[3]);
	if (!app) {
		return CLI_FAILURE;
	}

	stasis_app_to_cli(app, a);

	ao2_ref(app, -1);

	return CLI_SUCCESS;
}

static char *ari_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, apps, stasis_app_get_all(), ao2_cleanup);
	void *app;
	int debug;

	if (!apps) {
		ast_cli(a->fd, "Error getting ARI applications\n");
		return CLI_FAILURE;
	}

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari set debug";
		e->usage =
			"Usage: ari set debug <application|all> <on|off>\n"
			"       Enable or disable debugging on a specific application.\n"
			;
		return NULL;
	case CLI_GENERATE:
		if (a->argc == 3) {
			ast_cli_completion_add(ast_strdup("all"));
			complete_app(apps, a->word);
		} else if (a->argc == 4) {
			ast_cli_completion_add(ast_strdup("on"));
			ast_cli_completion_add(ast_strdup("off"));
		}
		return NULL;
	default:
		break;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	debug = !strcmp(a->argv[4], "on");

	if (!strcmp(a->argv[3], "all")) {
		stasis_app_set_global_debug(debug);
		ast_cli(a->fd, "Debugging on all applications %s\n",
			debug ? "enabled" : "disabled");
		return CLI_SUCCESS;
	}

	app = stasis_app_get_by_name(a->argv[3]);
	if (!app) {
		return CLI_FAILURE;
	}

	stasis_app_set_debug(app, debug);
	ast_cli(a->fd, "Debugging on '%s' %s\n",
		stasis_app_name(app),
		debug ? "enabled" : "disabled");

	ao2_ref(app, -1);

	return CLI_SUCCESS;
}

static int show_owc_cb(void *obj, void *arg, int flags)
{
	struct ari_conf_outbound_websocket *owc = obj;
	const char *id = ast_sorcery_object_get_id(owc);
	enum ari_conf_owc_fields invalid_fields = ari_conf_owc_get_invalid_fields(id);
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, "%-32s %-15s %-32s %-7s %s\n",
		id,
		ari_websocket_type_to_str(owc->websocket_client->connection_type),
		owc->apps,
		invalid_fields == ARI_OWC_FIELD_NONE ? "valid" : "INVALID",
		owc->websocket_client->uri);
	return 0;
}

#define DASHES "----------------------------------------------------------------------"
static char *ari_show_owcs(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, owcs, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show outbound-websockets";
		e->usage =
			"Usage: ari show outbound-websockets\n"
			"       Shows all ARI outbound-websockets\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	owcs = ari_conf_get_owcs();
	if (!owcs) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "%-32s %-15s %-32s %-7s %s\n", "Name", "Type", "Apps", "Status", "URI");
	ast_cli(a->fd, "%.*s %.*s %.*s %.*s %.*s\n", 32, DASHES, 15, DASHES, 32, DASHES, 7, DASHES, 64, DASHES);

	ao2_callback(owcs, OBJ_NODATA, show_owc_cb, a);

	return CLI_SUCCESS;
}

static char *ari_show_owc(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ari_conf_outbound_websocket *, owc, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, owcs, ari_conf_get_owcs(), ao2_cleanup);
	const char *id = NULL;
	enum ari_conf_owc_fields invalid_fields;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show outbound-websocket";
		e->usage =
			"Usage: ari show outbound-websocket <connection id>\n"
			"       Shows a specific ARI outbound websocket\n";
		return NULL;
	case CLI_GENERATE:
		complete_sorcery_object(owcs, a->word);
		return NULL;
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	owc = ari_conf_get_owc(a->argv[3]);
	if (!owc) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}
	id = ast_sorcery_object_get_id(owc);
	invalid_fields = ari_conf_owc_get_invalid_fields(id);

	ast_cli(a->fd, "[%s] %s\n", id,
		invalid_fields == ARI_OWC_FIELD_NONE ? "" : "**INVALID**");
	ast_cli(a->fd, "uri =                    %s\n", owc->websocket_client->uri);
	ast_cli(a->fd, "protocols =              %s\n", owc->websocket_client->protocols);
	ast_cli(a->fd, "apps =                   %s%s\n", owc->apps,
		invalid_fields & ARI_OWC_FIELD_APPS ? " (invalid)" : "");
	ast_cli(a->fd, "username =               %s\n", owc->websocket_client->username);
	ast_cli(a->fd, "password =               %s\n", S_COR(owc->websocket_client->password, "********", ""));
	ast_cli(a->fd, "local_ari_user =         %s%s\n", owc->local_ari_user,
		invalid_fields & ARI_OWC_FIELD_LOCAL_ARI_USER ? " (invalid)" : "");
	ast_cli(a->fd, "connection_type =        %s\n", ari_websocket_type_to_str(owc->websocket_client->connection_type));
	ast_cli(a->fd, "subscribe_all =          %s\n", AST_CLI_YESNO(owc->subscribe_all));
	ast_cli(a->fd, "connec_timeout =         %d\n", owc->websocket_client->connect_timeout);
	ast_cli(a->fd, "reconnect_attempts =     %d\n", owc->websocket_client->reconnect_attempts);
	ast_cli(a->fd, "reconnect_interval =     %d\n", owc->websocket_client->reconnect_interval);
	ast_cli(a->fd, "tls_enabled =            %s\n", AST_CLI_YESNO(owc->websocket_client->tls_enabled));
	ast_cli(a->fd, "ca_list_file =           %s\n", owc->websocket_client->ca_list_file);
	ast_cli(a->fd, "ca_list_path =           %s\n", owc->websocket_client->ca_list_path);
	ast_cli(a->fd, "cert_file =              %s\n", owc->websocket_client->cert_file);
	ast_cli(a->fd, "priv_key_file =          %s\n", owc->websocket_client->priv_key_file);
	ast_cli(a->fd, "verify_server =          %s\n", AST_CLI_YESNO(owc->websocket_client->verify_server_cert));
	ast_cli(a->fd, "verify_server_hostname = %s\n", AST_CLI_YESNO(owc->websocket_client->verify_server_hostname));
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static char *ari_start_owc(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ari_conf_outbound_websocket *, owc, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, owcs, ari_conf_get_owcs(), ao2_cleanup);

	if (!owcs) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE ;
	}

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari start outbound-websocket";
		e->usage =
			"Usage: ari start outbound-websocket <connection id>\n"
			"       Starts a specific ARI outbound websocket\n";
		return NULL;
	case CLI_GENERATE:
		complete_sorcery_object(owcs, a->word);
		return NULL;
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	owc = ari_conf_get_owc(a->argv[3]);
	if (!owc) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}
	ast_cli(a->fd, "Starting websocket session for outbound-websocket '%s'\n", a->argv[3]);

	if (ari_outbound_websocket_start(owc) != 0) {
		ast_cli(a->fd, "Error starting outbound websocket\n");
		return CLI_FAILURE ;
	}

	return CLI_SUCCESS;
}

static int show_sessions_cb(void *obj, void *arg, int flags)
{
	struct ari_ws_session *session = obj;
	struct ast_cli_args *a = arg;
	char *apps = ast_vector_string_join(&session->websocket_apps, ",");

	ast_cli(a->fd, "%-*s %-15s %-32s %-5s %s\n",
		AST_UUID_STR_LEN,
		session->session_id,
		ari_websocket_type_to_str(session->type),
		S_OR(session->remote_addr, "N/A"),
		session->type == AST_WS_TYPE_CLIENT_PER_CALL_CONFIG
			? "N/A" : (session->connected ? "Up" : "Down"),
		S_OR(apps, ""));

	ast_free(apps);
	return 0;
}

#define DASHES "----------------------------------------------------------------------"
static char *ari_show_sessions(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, sessions, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show websocket sessions";
		e->usage =
			"Usage: ari show websocket sessions\n"
			"       Shows all ARI websocket sessions\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	sessions = ari_websocket_get_sessions();
	if (!sessions) {
		ast_cli(a->fd, "Error getting websocket sessions\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "%-*.*s %-15.15s %-32.32s %-5.5s %-16.16s\n",
		AST_UUID_STR_LEN, AST_UUID_STR_LEN,
		"Connection ID",
		"Type",
		"RemoteAddr",
		"State",
		"Apps"
		);
	ast_cli(a->fd, "%-*.*s %-15.15s %-32.32s %-5.5s %-16.16s\n",
		AST_UUID_STR_LEN, AST_UUID_STR_LEN, DASHES, DASHES, DASHES, DASHES, DASHES);

	ao2_callback(sessions, OBJ_NODATA, show_sessions_cb, a);

	return CLI_SUCCESS;
}

static char *ari_shut_sessions(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari shutdown websocket sessions";
		e->usage =
			"Usage: ari shutdown websocket sessions\n"
			"       Shuts down all ARI websocket sessions\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "Shutting down all websocket sessions\n");
	ari_websocket_shutdown_all();

	return CLI_SUCCESS;
}

static void complete_session(struct ao2_container *container,
	const char *word)
{
	size_t wordlen = strlen(word);
	struct ari_ws_session *session;
	struct ao2_iterator i = ao2_iterator_init(container, 0);

	while ((session = ao2_iterator_next(&i))) {
		if (!strncasecmp(word, session->session_id, wordlen)) {
			ast_cli_completion_add(ast_strdup(session->session_id));
		}
		ao2_ref(session, -1);
	}
	ao2_iterator_destroy(&i);
}

static char *ari_shut_session(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	RAII_VAR(struct ari_ws_session *, session, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, sessions, ari_websocket_get_sessions(), ao2_cleanup);

	if (!sessions) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE ;
	}

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari shutdown websocket session";
		e->usage =
			"Usage: ari shutdown websocket session <id>\n"
			"       Shuts down ARI websocket session\n";
		return NULL;
	case CLI_GENERATE:
		complete_session(sessions, a->word);
		return NULL;
	default:
		break;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	session = ari_websocket_get_session(a->argv[4]);
	if (!session) {
		ast_cli(a->fd, "Websocket session '%s' not found\n", a->argv[4]);
		return CLI_FAILURE ;
	}
	ast_cli(a->fd, "Shutting down websocket session '%s'\n", a->argv[4]);
	ari_websocket_shutdown(session);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_ari[] = {
	AST_CLI_DEFINE(ari_show, "Show ARI settings"),
	AST_CLI_DEFINE(ari_show_users, "List ARI users"),
	AST_CLI_DEFINE(ari_show_user, "List single ARI user"),
	AST_CLI_DEFINE(ari_mkpasswd, "Encrypts a password"),
	AST_CLI_DEFINE(ari_show_apps, "List registered ARI applications"),
	AST_CLI_DEFINE(ari_show_app, "Display details of a registered ARI application"),
	AST_CLI_DEFINE(ari_set_debug, "Enable/disable debugging of an ARI application"),
	AST_CLI_DEFINE(ari_show_owcs, "List outbound websocket connections"),
	AST_CLI_DEFINE(ari_show_owc, "Show outbound websocket connection"),
	AST_CLI_DEFINE(ari_start_owc, "Start outbound websocket connection"),
	AST_CLI_DEFINE(ari_show_sessions, "Show websocket sessions"),
	AST_CLI_DEFINE(ari_shut_session, "Shutdown websocket session"),
	AST_CLI_DEFINE(ari_shut_sessions, "Shutdown websocket sessions"),
};

int ari_cli_register(void) {
	return ast_cli_register_multiple(cli_ari, ARRAY_LEN(cli_ari));
}

void ari_cli_unregister(void) {
	ast_cli_unregister_multiple(cli_ari, ARRAY_LEN(cli_ari));
}
