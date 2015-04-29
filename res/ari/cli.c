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

ASTERISK_REGISTER_FILE()

#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "internal.h"

static char *ari_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);

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

	conf = ast_ari_config_get();

	if (!conf) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "ARI Status:\n");
	ast_cli(a->fd, "Enabled: %s\n", AST_CLI_YESNO(conf->general->enabled));
	ast_cli(a->fd, "Output format: ");
	switch (conf->general->format) {
	case AST_JSON_COMPACT:
		ast_cli(a->fd, "compact");
		break;
	case AST_JSON_PRETTY:
		ast_cli(a->fd, "pretty");
		break;
	}
	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "Auth realm: %s\n", conf->general->auth_realm);
	ast_cli(a->fd, "Allowed Origins: %s\n", conf->general->allowed_origins);
	ast_cli(a->fd, "User count: %d\n", ao2_container_count(conf->users));
	return CLI_SUCCESS;
}

static int show_users_cb(void *obj, void *arg, int flags)
{
	struct ast_ari_conf_user *user = obj;
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, "%-4s  %s\n",
		AST_CLI_YESNO(user->read_only),
		user->username);
	return 0;
}

static char *ari_show_users(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);

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

	conf = ast_ari_config_get();
	if (!conf) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "r/o?  Username\n");
	ast_cli(a->fd, "----  --------\n");

	ao2_callback(conf->users, OBJ_NODATA, show_users_cb, a);

	return CLI_SUCCESS;
}

struct user_complete {
	/*! Nth user to search for */
	int state;
	/*! Which user currently on */
	int which;
};

static int complete_ari_user_search(void *obj, void *arg, void *data, int flags)
{
	struct user_complete *search = data;

	if (++search->which > search->state) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_ari_user(struct ast_cli_args *a)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);
	RAII_VAR(struct ast_ari_conf_user *, user, NULL, ao2_cleanup);

	struct user_complete search = {
		.state = a->n,
	};

	conf = ast_ari_config_get();
	if (!conf) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	user = ao2_callback_data(conf->users,
		ast_strlen_zero(a->word) ? 0 : OBJ_PARTIAL_KEY,
		complete_ari_user_search, (char*)a->word, &search);

	return user ? ast_strdup(user->username) : NULL;
}

static char *complete_ari_show_user(struct ast_cli_args *a)
{
	if (a->pos == 3) {
		return complete_ari_user(a);
	}

	return NULL;
}

static char *ari_show_user(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);
	RAII_VAR(struct ast_ari_conf_user *, user, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show user";
		e->usage =
			"Usage: ari show user <username>\n"
			"       Shows a specific ARI user\n";
		return NULL;
	case CLI_GENERATE:
		return complete_ari_show_user(a);
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	conf = ast_ari_config_get();

	if (!conf) {
		ast_cli(a->fd, "Error getting ARI configuration\n");
		return CLI_FAILURE;
	}

	user = ao2_find(conf->users, a->argv[3], OBJ_KEY);
	if (!user) {
		ast_cli(a->fd, "User '%s' not found\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Username: %s\n", user->username);
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

static struct ast_cli_entry cli_ari[] = {
	AST_CLI_DEFINE(ari_show, "Show ARI settings"),
	AST_CLI_DEFINE(ari_show_users, "List ARI users"),
	AST_CLI_DEFINE(ari_show_user, "List single ARI user"),
	AST_CLI_DEFINE(ari_mkpasswd, "Encrypts a password"),
};

int ast_ari_cli_register(void) {
	return ast_cli_register_multiple(cli_ari, ARRAY_LEN(cli_ari));
}
